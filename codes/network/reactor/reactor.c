#include "reactor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>

// 设置非阻塞
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return (flags == -1) ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 获取当前时间
static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 创建连接
static connection_t* connection_create(int fd, int thread_id) {
    connection_t *conn = calloc(1, sizeof(connection_t));
    if (!conn) return NULL;
    
    conn->fd = fd;
    conn->thread_id = thread_id;
    atomic_store(&conn->state, 0);
    atomic_store(&conn->read_len, 0);
    atomic_store(&conn->write_len, 0);
    atomic_store(&conn->write_sent, 0);
    conn->last_active_time = get_current_time_ms();
    
    return conn;
}

// 处理连接关闭
void handle_close_event(reactor_thread_t *thread, connection_t *conn) {
    if (!thread || !conn) return;
    
    // 从epoll中移除
    epoll_ctl(thread->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    
    // 关闭socket
    close(conn->fd);
    
    // 从连接数组中移除
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (thread->connections[i] == conn) {
            thread->connections[i] = NULL;
            atomic_fetch_sub(&thread->connection_count, 1);
            atomic_fetch_sub(&thread->active_connections, 1);
            break;
        }
    }
    
    free(conn);
}

// 创建Reactor
reactor_t* reactor_create(int thread_count) {
    if (thread_count <= 0 || thread_count > MAX_REACTOR_THREADS) {
        return NULL;
    }
    
    reactor_t *reactor = calloc(1, sizeof(reactor_t));
    if (!reactor) return NULL;
    
    reactor->thread_count = thread_count;
    atomic_store(&reactor->running, false);
    atomic_store(&reactor->next_thread, 0);
    
    for (int i = 0; i < thread_count; i++) {
        reactor_thread_t *thread = &reactor->threads[i];
        thread->id = i;
        atomic_store(&thread->running, false);
        atomic_store(&thread->connection_count, 0);
        
        // 初始化环形队列
        ring_queue_init(&thread->accept_queue);
        
        thread->epoll_fd = epoll_create1(0);
        if (thread->epoll_fd == -1) {
            // 清理已创建的资源
            for (int j = 0; j < i; j++) {
                close(reactor->threads[j].epoll_fd);
            }
            free(reactor);
            return NULL;
        }
        
        // 初始化统计信息
        atomic_store(&thread->total_connections, 0);
        atomic_store(&thread->active_connections, 0);
        atomic_store(&thread->processed_events, 0);
        atomic_store(&thread->batch_processed, 0);
        
        // 初始化连接数组
        for (int j = 0; j < MAX_CONNECTIONS; j++) {
            thread->connections[j] = NULL;
        }
    }
    
    return reactor;
}

// 线程本地的连接添加
static int thread_add_connection(reactor_thread_t *thread, int fd) {
    // 查找空闲槽位
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!thread->connections[i]) {
            connection_t *conn = connection_create(fd, thread->id);
            if (!conn) return -1;
            
            if (set_nonblocking(fd) == -1) {
                free(conn);
                return -1;
            }
            
            // 添加到epoll（边缘触发）
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET;
            ev.data.ptr = conn;
            
            if (epoll_ctl(thread->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                free(conn);
                return -1;
            }
            
            thread->connections[i] = conn;
            atomic_fetch_add(&thread->connection_count, 1);
            atomic_fetch_add(&thread->total_connections, 1);
            atomic_fetch_add(&thread->active_connections, 1);
            
            return 0;
        }
    }
    return -1;
}

// 批量处理新连接
static uint32_t process_new_connections_batch(reactor_thread_t *thread) {
    int fds[BATCH_SIZE];
    uint32_t count = ring_queue_pop_batch(&thread->accept_queue, fds, BATCH_SIZE);
    
    
    if (count == 0) {
        return 0;
    }
    // 添加调试信息
    printf("DEBUG: Thread %d - ring_queue_pop_batch returned count=%u\n", thread->id, count);
    
    uint32_t success_count = 0;
    printf("DEBUG: Thread %d - Processing %u new connections\n", thread->id, count);
    
    for (uint32_t i = 0; i < count; i++) {
        if (thread_add_connection(thread, fds[i]) == 0) {
            success_count++;
            printf("DEBUG: Thread %d - Successfully added fd=%d\n", thread->id, fds[i]);
        } else {
            close(fds[i]);
            printf("DEBUG: Thread %d - Failed to add fd=%d, closed\n", thread->id, fds[i]);
        }
    }
    
    if (count > 1) {
        atomic_fetch_add(&thread->batch_processed, 1);
    }
    
    printf("DEBUG: Thread %d - Successfully processed %u/%u connections\n", 
           thread->id, success_count, count);
    return success_count;
}

static void handle_add_write(reactor_thread_t *thread, connection_t *conn)
{
    uint32_t write_sent = atomic_load(&conn->write_sent);
    uint32_t read_len = atomic_load(&conn->read_len);
    if(conn->write_len == 0 && write_sent + read_len < BUFFER_SIZE) {
        memcpy(conn->write_buf + write_sent, conn->read_buf, read_len);
        atomic_store(&conn->read_len, 0);
        atomic_fetch_add(&conn->write_len, read_len);
    }
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.ptr = conn;
    epoll_ctl(thread->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);

}

// 处理读事件（边缘触发，批量读取）
static void handle_read_event(reactor_thread_t *thread, connection_t *conn) {
    while (1) {
        uint32_t read_len = atomic_load(&conn->read_len);
        size_t remaining = BUFFER_SIZE - read_len;
        
        if (remaining == 0) {
            // 缓冲区满，需要处理数据
            printf("DEBUG: Thread %d - Buffer full for fd=%d\n", thread->id, conn->fd);
            break;
        }
        
        ssize_t n = read(conn->fd, conn->read_buf + read_len, remaining);
        
        if (n > 0) {
            atomic_fetch_add(&conn->read_len, n);
            printf("DEBUG: Thread %d - Read %zd bytes from fd=%d\n", thread->id, n, conn->fd);
            handle_add_write(thread, conn);
        } else if (n == 0) {
            // 连接关闭
            printf("DEBUG: Thread %d - Connection closed by peer, fd=%d\n", thread->id, conn->fd);
            handle_close_event(thread, conn);
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据已读完
                break;
            } else {
                printf("DEBUG: Thread %d - Read error on fd=%d: %s\n", thread->id, conn->fd, strerror(errno));
                handle_close_event(thread, conn);
                break;
            }
        }
    }
}

static void handle_add_read_event(reactor_thread_t *thread, connection_t *conn)
{
    if (atomic_load(&conn->write_sent) >= atomic_load(&conn->write_len)) {
        // 发送完成，改为监听读
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = conn;
        epoll_ctl(thread->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
        
        atomic_store(&conn->write_len, 0);
        atomic_store(&conn->write_sent, 0);
        printf("DEBUG: Thread %d - All data sent to fd=%d, switching to read mode\n", thread->id, conn->fd);
    }

}

// 批量处理写事件
static void handle_write_events(reactor_thread_t *thread, connection_t **conns, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        connection_t *conn = conns[i];
        if (!conn) continue;
        
        uint32_t write_len = atomic_load(&conn->write_len);
        uint32_t write_sent = atomic_load(&conn->write_sent);
        
        if (write_sent < write_len) {
            ssize_t n = write(conn->fd, 
                            conn->write_buf + write_sent,
                            write_len - write_sent);
            
            if (n > 0) {
                atomic_fetch_add(&conn->write_sent, n);
                printf("DEBUG: Thread %d - Wrote %zd bytes to fd=%d\n", thread->id, n, conn->fd);
                handle_add_read_event(thread, conn);
            } else if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 写缓冲区满，下次再试
                    continue;
                } else {
                    printf("DEBUG: Thread %d - Write error on fd=%d: %s\n", thread->id, conn->fd, strerror(errno));
                    handle_close_event(thread, conn);
                }
            }
        }
    }
}

// Reactor线程主循环
static void* reactor_thread_main(void *arg) {
    reactor_thread_t *thread = (reactor_thread_t*)arg;
    struct epoll_event events[MAX_EVENTS];
    
    atomic_store(&thread->running, true);
    printf("DEBUG: Reactor thread %d started\n", thread->id);
    
    uint32_t loop_count = 0;
    
    while (atomic_load(&thread->running)) {
        loop_count++;
        
        // 1. 批量处理新连接（每次循环都处理）
        uint32_t new_conns = process_new_connections_batch(thread);
        if (new_conns > 0) {
            printf("DEBUG: Thread %d processed %u new connections\n", thread->id, new_conns);
        }
        
        // 2. 等待事件（短超时，及时响应新连接）
        int nfds = epoll_wait(thread->epoll_fd, events, MAX_EVENTS, 10); // 10ms超时
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        
        if (nfds > 0) {
            atomic_fetch_add(&thread->processed_events, nfds);
            printf("DEBUG: Thread %d got %d events\n", thread->id, nfds);
        }
        
        // 3. 预处理：分类事件
        connection_t *read_conns[MAX_EVENTS];
        connection_t *write_conns[MAX_EVENTS];
        uint32_t read_count = 0, write_count = 0;
        
        for (int i = 0; i < nfds; i++) {
            connection_t *conn = (connection_t*)events[i].data.ptr;
            if (!conn) continue;
            
            conn->last_active_time = get_current_time_ms();
            
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                printf("DEBUG: Thread %d - connection error/hup on fd=%d, closing\n", thread->id, conn->fd);
                handle_close_event(thread, conn);
                continue;
            }
            
            if (events[i].events & EPOLLIN) {
                read_conns[read_count++] = conn;
            }
            
            if (events[i].events & EPOLLOUT) {
                write_conns[write_count++] = conn;
            }
        }
        
        // 4. 处理读事件
        for (uint32_t i = 0; i < read_count; i++) {
            handle_read_event(thread, read_conns[i]);
        }
        
        // 5. 处理写事件
        if (write_count > 0) {
            handle_write_events(thread, write_conns, write_count);
        }
        
        // 6. 定时器检查（每100次循环检查一次）
        if (loop_count % 100 == 0) {
            uint64_t current_time = get_current_time_ms();
            for (int i = 0; i < MAX_CONNECTIONS; i++) {
                connection_t *conn = thread->connections[i];
                if (conn && current_time - conn->last_active_time > 30000) {
                    printf("DEBUG: Thread %d - connection timeout on fd=%d, closing\n", thread->id, conn->fd);
                    handle_close_event(thread, conn);
                }
            }
        }
    }
    
    printf("DEBUG: Reactor thread %d stopped\n", thread->id);
    return NULL;
}

// 添加连接
int reactor_add_connection(reactor_t *reactor, int fd) {
    if (!reactor || fd < 0) {
        printf("ERROR: Invalid parameters to reactor_add_connection\n");
        return -1;
    }
    
    if (!atomic_load(&reactor->running)) {
        printf("ERROR: Reactor not running when adding connection\n");
        return -1;
    }
    
    // 负载均衡：简单的轮询
    uint32_t thread_index = atomic_fetch_add(&reactor->next_thread, 1) % reactor->thread_count;
    reactor_thread_t *thread = &reactor->threads[thread_index];
    
    printf("DEBUG: Adding fd=%d to thread %d\n", fd, thread_index);
    
    // 使用无锁队列
    if (ring_queue_push(&thread->accept_queue, fd)) {
        printf("DEBUG: Successfully queued fd=%d to thread %d\n", fd, thread_index);
        return 0;
    }
    
    printf("ERROR: Failed to queue fd=%d to thread %d (queue full?)\n", fd, thread_index);
    return -1;
}

// 启动Reactor
int reactor_run(reactor_t *reactor) {
    if (!reactor || atomic_load(&reactor->running)) {
        printf("ERROR: Reactor already running or invalid\n");
        return -1;
    }
    
    atomic_store(&reactor->running, true);
    printf("DEBUG: Starting %d reactor threads\n", reactor->thread_count);
    
    for (int i = 0; i < reactor->thread_count; i++) {
        reactor_thread_t *thread = &reactor->threads[i];
        if (pthread_create(&thread->thread_id, NULL, reactor_thread_main, thread) != 0) {
            perror("pthread_create");
            reactor_stop(reactor);
            return -1;
        }
    }
    
    printf("DEBUG: All reactor threads started successfully\n");
    return 0;
}

// 停止Reactor
int reactor_stop(reactor_t *reactor) {
    if (!reactor || !atomic_load(&reactor->running)) {
        return -1;
    }
    
    printf("DEBUG: Stopping reactor\n");
    atomic_store(&reactor->running, false);
    
    for (int i = 0; i < reactor->thread_count; i++) {
        reactor_thread_t *thread = &reactor->threads[i];
        atomic_store(&thread->running, false);
        if (thread->thread_id) {
            pthread_join(thread->thread_id, NULL);
        }
    }
    
    printf("DEBUG: Reactor stopped\n");
    return 0;
}

// 销毁Reactor
int reactor_destroy(reactor_t *reactor) {
    if (!reactor) return -1;
    
    reactor_stop(reactor);
    
    for (int i = 0; i < reactor->thread_count; i++) {
        reactor_thread_t *thread = &reactor->threads[i];
        close(thread->epoll_fd);
        
        // 关闭所有剩余连接
        for (int j = 0; j < MAX_CONNECTIONS; j++) {
            if (thread->connections[j]) {
                close(thread->connections[j]->fd);
                free(thread->connections[j]);
            }
        }
    }
    
    free(reactor);
    printf("DEBUG: Reactor destroyed\n");
    return 0;
}

// 性能监控
void reactor_stats(reactor_t *reactor) {
    if (!reactor) return;
    
    printf("=== Reactor Statistics ===\n");
    for (int i = 0; i < reactor->thread_count; i++) {
        reactor_thread_t *thread = &reactor->threads[i];
        printf("Thread %d: conns=%u, events=%llu, batches=%llu\n", 
               i, 
               atomic_load(&thread->connection_count),
               atomic_load(&thread->processed_events),
               atomic_load(&thread->batch_processed));
    }
}