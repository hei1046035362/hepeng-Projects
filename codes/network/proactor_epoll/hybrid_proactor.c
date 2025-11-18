
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <libaio.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <signal.h>

#include "hybrid_proactor.h"

// 全局变量，用于优雅关闭
volatile sig_atomic_t graceful_shutdown = 0;

void mt_send_welcome_message(worker_context_t *worker, mt_connection_t *conn);

// // 信号处理
// void signal_handler(int sig) {
//     if (graceful_shutdown) {
//         return;
//     }
//     graceful_shutdown = 1;
//     printf("\nReceived signal %d, initiating shutdown...\n", sig);
// }

// 设置非阻塞
int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 创建服务器socket
int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket failed");
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(fd);
        return -1;
    }
    
    if (set_nonblock(fd) < 0) {
        perror("set_nonblock failed");
        close(fd);
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(fd);
        return -1;
    }
    
    if (listen(fd, 1024) < 0) {
        perror("listen failed");
        close(fd);
        return -1;
    }
    
    printf("Server listening on port %d\n", port);
    return fd;
}

// 初始化多线程Proactor
int mt_proactor_init(mt_proactor_t *proactor, int num_workers) {
    if (num_workers <= 0 || num_workers > MAX_WORKER_THREADS) {
        fprintf(stderr, "Invalid number of workers: %d\n", num_workers);
        return -1;
    }
    
    memset(proactor, 0, sizeof(mt_proactor_t));
    proactor->num_workers = num_workers;
    proactor->running = 1;
    proactor->next_worker = 0;
    
    // 创建工作线程数组
    proactor->workers = calloc(num_workers, sizeof(worker_context_t));
    if (!proactor->workers) {
        perror("calloc workers failed");
        return -1;
    }
    
    // 初始化同步原语
    pthread_mutex_init(&proactor->accept_lock, NULL);
    pthread_cond_init(&proactor->accept_cond, NULL);
    
    // 创建退出事件fd
    proactor->exit_event_fd = eventfd(0, EFD_NONBLOCK);
    if (proactor->exit_event_fd < 0) {
        perror("eventfd failed");
        free(proactor->workers);
        return -1;
    }
    
    printf("Multi-threaded proactor initialized with %d workers\n", num_workers);
    return 0;
}

// 启动工作线程
int mt_proactor_start(mt_proactor_t *proactor) {
    // 初始化工作线程
    for (int i = 0; i < proactor->num_workers; i++) {
        worker_context_t *worker = &proactor->workers[i];
        
        worker->id = i;
        worker->running = 1;
        worker->proactor = proactor;
        
        // 初始化每个工作线程的AIO上下文
        if (io_setup(1000, &worker->aio_ctx) < 0) {
            perror("io_setup failed");
            goto cleanup;
        }
        
        // 初始化每个工作线程的epoll实例
        worker->epoll_fd = epoll_create1(0);
        if (worker->epoll_fd < 0) {
            perror("epoll_create1 failed");
            io_destroy(worker->aio_ctx);
            goto cleanup;
        }
        
        // 初始化连接列表锁
        pthread_mutex_init(&worker->conn_list_lock, NULL);
        
        // 启动工作线程
        if (pthread_create(&worker->thread, NULL, worker_thread_func, worker) != 0) {
            perror("pthread_create worker failed");
            close(worker->epoll_fd);
            io_destroy(worker->aio_ctx);
            goto cleanup;
        }
        
        printf("Worker thread %d started\n", i);
    }
    
    // 启动接受连接线程
    if (pthread_create(&proactor->accept_thread, NULL, accept_thread_func, proactor) != 0) {
        perror("pthread_create accept thread failed");
        goto cleanup;
    }
    
    printf("All threads started successfully\n");
    return 0;

cleanup:
    // 清理已创建的资源
    proactor->running = 0;
    mt_proactor_stop(proactor);
    return -1;
}

// 停止Proactor
int mt_proactor_stop(mt_proactor_t *proactor) {
    if (!proactor->running) return 0;
    
    printf("Initiating multi-threaded proactor shutdown...\n");
    proactor->running = 0;
    graceful_shutdown = 1;
    
    // 触发退出事件
    if (proactor->exit_event_fd >= 0) {
        uint64_t value = 1;
        write(proactor->exit_event_fd, &value, sizeof(value));
    }
    
    // 等待接受线程结束
    if (proactor->accept_thread) {
        pthread_join(proactor->accept_thread, NULL);
        printf("Accept thread stopped\n");
    }
    
    // 停止所有工作线程
    for (int i = 0; i < proactor->num_workers; i++) {
        worker_context_t *worker = &proactor->workers[i];
        worker->running = 0;
        
        if (worker->thread) {
            pthread_join(worker->thread, NULL);
            printf("Worker thread %d stopped\n", i);
        }
        
        // 清理工作线程资源
        if (worker->epoll_fd >= 0) {
            close(worker->epoll_fd);
            worker->epoll_fd = -1;
        }
        
        if (worker->aio_ctx) {
            // 取消所有待处理的AIO操作
            struct io_event events[64];
            struct timespec timeout = {0, 0};
            int num_events;
            
            do {
                num_events = io_getevents(worker->aio_ctx, 1, 64, events, &timeout);
                if (num_events > 0) {
                    printf("Worker %d: cleaned up %d pending AIO operations\n", 
                           i, num_events);
                }
            } while (num_events > 0);
            
            io_destroy(worker->aio_ctx);
            worker->aio_ctx = 0;
        }
        
        // 清理连接
        pthread_mutex_lock(&worker->conn_list_lock);
        mt_connection_t *conn = worker->connections;
        while (conn) {
            mt_connection_t *next = conn->next;
            if (conn->fd >= 0) {
                close(conn->fd);
            }
            free(conn);
            conn = next;
        }
        worker->connections = NULL;
        pthread_mutex_unlock(&worker->conn_list_lock);
        
        pthread_mutex_destroy(&worker->conn_list_lock);
    }
    
    // 清理主资源
    if (proactor->exit_event_fd >= 0) {
        close(proactor->exit_event_fd);
        proactor->exit_event_fd = -1;
    }
    
    if (proactor->listen_fd >= 0) {
        close(proactor->listen_fd);
        proactor->listen_fd = -1;
    }
    
    if (proactor->workers) {
        free(proactor->workers);
        proactor->workers = NULL;
    }
    
    pthread_mutex_destroy(&proactor->accept_lock);
    pthread_cond_destroy(&proactor->accept_cond);
    
    printf("Multi-threaded proactor shutdown complete\n");
    return 0;
}

// 接受连接线程函数
void *accept_thread_func(void *arg) {
    mt_proactor_t *proactor = (mt_proactor_t *)arg;
    
    printf("Accept thread started\n");
    
    while (proactor->running && !graceful_shutdown) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(proactor->listen_fd, 
                              (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有新连接，短暂等待
                usleep(1000);
                continue;
            } else {
                perror("accept failed");
                break;
            }
        }
        
        if (graceful_shutdown) {
            close(client_fd);
            break;
        }
        
        // 设置为非阻塞
        if (set_nonblock(client_fd) < 0) {
            perror("set_nonblock failed");
            close(client_fd);
            continue;
        }
        
        printf("New connection from %s:%d, fd=%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);
        
        // 选择工作线程（轮询策略）
        int worker_id = proactor->next_worker;
        proactor->next_worker = (proactor->next_worker + 1) % proactor->num_workers;
        
        worker_context_t *worker = &proactor->workers[worker_id];
        
        // 创建工作线程的连接
        mt_connection_t *conn = mt_create_connection(client_fd, &client_addr, worker_id);
        if (!conn) {
            close(client_fd);
            continue;
        }
        
        // 添加到工作线程
        mt_add_connection_to_worker(worker, conn);
        
        proactor->total_connections++;
        
        // 发送欢迎消息
        mt_send_welcome_message(worker, conn);
    }
    
    printf("Accept thread exiting\n");
    return NULL;
}

// 创建工作线程连接
mt_connection_t *mt_create_connection(int fd, struct sockaddr_in *addr, int worker_id) {
    mt_connection_t *conn = malloc(sizeof(mt_connection_t));
    if (!conn) {
        return NULL;
    }
    
    memset(conn, 0, sizeof(mt_connection_t));
    conn->fd = fd;
    if (addr) {
        conn->client_addr = *addr;
    }
    conn->worker_id = worker_id;
    conn->readable = 1;
    conn->writable = 1;
    conn->state = CONN_ACTIVE;
    conn->last_activity = time(NULL);
    
    // 初始化线程安全字段
    atomic_init(&conn->is_removing, 0);
    atomic_init(&conn->ref_count, 1);
    pthread_mutex_init(&conn->lock, NULL);
    
    return conn;
}

// 添加连接到工作线程
void mt_add_connection_to_worker(worker_context_t *worker, mt_connection_t *conn) {
    pthread_mutex_lock(&worker->conn_list_lock);
    
    // 添加到连接列表头部
    conn->next = worker->connections;
    worker->connections = conn;
    
    // 添加到epoll监控
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ev.data.ptr = conn;
    
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev) < 0) {
        perror("epoll_ctl add failed");
        // 从列表中移除
        worker->connections = conn->next;
        pthread_mutex_unlock(&worker->conn_list_lock);
        mt_remove_connection_safe(worker, conn);
        return;
    }
    
    pthread_mutex_unlock(&worker->conn_list_lock);
    
    printf("Connection fd=%d added to worker %d\n", conn->fd, worker->id);
}

// 发送欢迎消息
void mt_send_welcome_message(worker_context_t *worker, mt_connection_t *conn) {
    const char *welcome = "Welcome to Multi-threaded Hybrid Proactor Server!\r\n"
                         "Type something and press enter to echo.\r\n";
    size_t welcome_len = strlen(welcome);
    
    if (welcome_len < BUFFER_SIZE) {
        memcpy(conn->write_buf, welcome, welcome_len);
        conn->write_pending = welcome_len;
        
        // 尝试立即发送
        mt_try_write(worker, conn);
    }
}
// 工作线程函数
void *worker_thread_func(void *arg) {
    worker_context_t *worker = (worker_context_t *)arg;
    mt_proactor_t *proactor = worker->proactor;
    
    printf("Worker thread %d starting\n", worker->id);
    
    struct epoll_event events[MAX_EVENTS];
    struct io_event aio_events[MAX_EVENTS];
    struct timespec timeout = {0, 1000000}; // 1ms
    
    // 添加退出事件到epoll
    if (proactor->exit_event_fd >= 0) {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = proactor->exit_event_fd;
        
        if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, proactor->exit_event_fd, &ev) < 0) {
            perror("epoll_ctl exit_event_fd failed");
        }
    }
    
    while (worker->running && !graceful_shutdown) {
        // 处理epoll事件
        int nfds = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, 10);
        
        if (nfds < 0) {
            if (errno == EINTR) {
                if (!worker->running || graceful_shutdown) break;
                continue;
            } else {
                perror("epoll_wait failed");
                break;
            }
        }
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == proactor->exit_event_fd) {
                // 退出事件
                worker->running = 0;
                break;
            }
            
            mt_connection_t *conn = (mt_connection_t *)events[i].data.ptr;
            if (!conn) continue;
            
            mt_handle_connection_event(worker, conn, events[i].events);
        }
        
        if (!worker->running || graceful_shutdown) {
            break;
        }
        
        // 处理AIO完成事件
        int num_aio = io_getevents(worker->aio_ctx, 0, MAX_EVENTS, aio_events, &timeout);
        if (num_aio < 0) {
            if (errno == EINTR) {
                if (!worker->running || graceful_shutdown) break;
                continue;
            } else if (errno == EINVAL) {
                printf("Worker %d: AIO context invalid\n", worker->id);
                break;
            } else {
                perror("io_getevents failed");
                continue;
            }
        }
        
        for (int i = 0; i < num_aio; i++) {
            if (!worker->running || graceful_shutdown) break;
            mt_handle_aio_completion(worker, &aio_events[i]);
        }
        
        if (!worker->running || graceful_shutdown) {
            break;
        }
        
        // 性能监控（每5秒一次）
        static time_t last_report = 0;
        time_t now = time(NULL);
        if (now - last_report >= 5) {
            printf("Worker %d: total_ops=%lu, eagain=%lu, success=%lu\n",
                   worker->id, worker->total_operations, 
                   worker->eagain_errors, worker->successful_ops);
            last_report = now;
        }
        
        // 减少CPU使用
        if (nfds == 0 && num_aio == 0) {
            usleep(1000);
        }
    }
    
    printf("Worker thread %d exiting\n", worker->id);
    return NULL;
}

// 处理连接事件
void mt_handle_connection_event(worker_context_t *worker, mt_connection_t *conn, uint32_t events) {
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        printf("Worker %d: Connection error on fd=%d\n", worker->id, conn->fd);
        mt_remove_connection_safe(worker, conn);
        return;
    }
    
    if (events & EPOLLIN) {
        conn->readable = 1;
        mt_try_read(worker, conn);
    }
    
    if (events & EPOLLOUT) {
        conn->writable = 1;
        mt_try_write(worker, conn);
    }
}

// 尝试读取
void mt_try_read(worker_context_t *worker, mt_connection_t *conn) {
    if (!conn->readable) return;
    
    ssize_t n = read(conn->fd, conn->read_buf, BUFFER_SIZE - 1);
    
    if (n > 0) {
        // 同步读取成功
        conn->read_buf[n] = '\0';
        printf("Worker %d: Sync read %zd bytes from fd=%d\n", worker->id, n, conn->fd);
        
        mt_process_data(worker, conn, conn->read_buf, n);
        worker->successful_ops++;
        worker->total_operations++;
        conn->last_activity = time(NULL);
        
    } else if (n == 0) {
        // 连接关闭
        printf("Worker %d: Connection fd=%d closed by peer\n", worker->id, conn->fd);
        mt_remove_connection_safe(worker, conn);
        
    } else if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 暂时不可读
            conn->readable = 0;
            worker->eagain_errors++;
            
            // 使用异步读取
            mt_submit_async_read(worker, conn);
        } else {
            perror("read failed");
            mt_remove_connection_safe(worker, conn);
        }
    }
}

// 尝试写入
void mt_try_write(worker_context_t *worker, mt_connection_t *conn) {
    if (!conn->writable || conn->write_pending == 0) return;
    
    ssize_t n = write(conn->fd, conn->write_buf, conn->write_pending);
    
    if (n > 0) {
        // 同步写入成功
        printf("Worker %d: Sync wrote %zd bytes to fd=%d\n", worker->id, n, conn->fd);
        
        // 移动剩余数据
        if (n < conn->write_pending) {
            memmove(conn->write_buf, conn->write_buf + n, conn->write_pending - n);
        }
        conn->write_pending -= n;
        worker->successful_ops++;
        worker->total_operations++;
        conn->last_activity = time(NULL);
        
    } else if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 暂时不可写
            conn->writable = 0;
            worker->eagain_errors++;
            
            // 使用异步写入
            mt_submit_async_write(worker, conn);
        } else {
            perror("write failed");
            mt_remove_connection_safe(worker, conn);
        }
    }
}

// 处理数据
void mt_process_data(worker_context_t *worker, mt_connection_t *conn, const char *data, size_t len) {
    // 简单回显处理
    printf("Worker %d: Processing data from fd=%d: %.*s\n", 
           worker->id, conn->fd, (int)len, data);
    
    // 准备回显数据
    const char *echo_prefix = "Echo: ";
    size_t prefix_len = strlen(echo_prefix);
    size_t total_len = prefix_len + len;
    
    if (total_len < BUFFER_SIZE) {
        memcpy(conn->write_buf, echo_prefix, prefix_len);
        memcpy(conn->write_buf + prefix_len, data, len);
        conn->write_pending = total_len;
        
        // 尝试立即写入
        mt_try_write(worker, conn);
    }
}

// 提交异步读取
void mt_submit_async_read(worker_context_t *worker, mt_connection_t *conn) {
    struct iocb iocb;
    struct iocb *iocbs[1] = { &iocb };
    
    io_prep_pread(&iocb, conn->fd, conn->read_buf, BUFFER_SIZE - 1, 0);
    iocb.data = conn;
    
    int ret = io_submit(worker->aio_ctx, 1, iocbs);
    if (ret == 1) {
        worker->total_operations++;
        printf("Worker %d: Submitted async read for fd=%d\n", worker->id, conn->fd);
    } else if (ret == -EAGAIN) {
        printf("Worker %d: AIO queue full for fd=%d\n", worker->id, conn->fd);
        worker->eagain_errors++;
    } else {
        printf("Worker %d: AIO submit failed for fd=%d: %d\n", worker->id, conn->fd, ret);
    }
}

// 提交异步写入
void mt_submit_async_write(worker_context_t *worker, mt_connection_t *conn) {
    struct iocb iocb;
    struct iocb *iocbs[1] = { &iocb };
    
    io_prep_pwrite(&iocb, conn->fd, conn->write_buf, conn->write_pending, 0);
    iocb.data = conn;
    
    int ret = io_submit(worker->aio_ctx, 1, iocbs);
    if (ret == 1) {
        worker->total_operations++;
        printf("Worker %d: Submitted async write for fd=%d\n", worker->id, conn->fd);
    } else if (ret == -EAGAIN) {
        printf("Worker %d: AIO queue full for fd=%d\n", worker->id, conn->fd);
        worker->eagain_errors++;
    } else {
        printf("Worker %d: AIO submit failed for fd=%d: %d\n", worker->id, conn->fd, ret);
    }
}

// 处理AIO完成事件
void mt_handle_aio_completion(worker_context_t *worker, struct io_event *event) {
    mt_connection_t *conn = (mt_connection_t *)event->data;
    if (!conn) return;
    
    if (event->res > 0) {
        // AIO操作成功
        printf("Worker %d: AIO completed: %lld bytes for fd=%d\n", 
               worker->id, (long long)event->res, conn->fd);
        
        if (event->res2 == 0) { // 读操作
            conn->read_buf[event->res] = '\0';
            mt_process_data(worker, conn, conn->read_buf, event->res);
        } else { // 写操作
            printf("Worker %d: Async write completed for fd=%d\n", worker->id, conn->fd);
        }
        
        worker->successful_ops++;
        conn->last_activity = time(NULL);
        
    } else if (event->res == -EAGAIN) {
        printf("Worker %d: AIO EAGAIN for fd=%d\n", worker->id, conn->fd);
        worker->eagain_errors++;
    } else {
        printf("Worker %d: AIO error for fd=%d: %lld\n", worker->id, conn->fd, (long long)event->res);
    }
}

// 安全移除连接
void mt_remove_connection_safe(worker_context_t *worker, mt_connection_t *conn) {
    if (!conn) return;
    
    // 原子检查是否已经在移除过程中
    int expected = 0;
    if (!atomic_compare_exchange_strong(&conn->is_removing, &expected, 1)) {
        return; // 已经在移除过程中
    }
    
    printf("Worker %d: Safely removing connection fd=%d\n", worker->id, conn->fd);
    
    // 从epoll移除
    if (worker->epoll_fd >= 0 && conn->fd >= 0) {
        epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    }
    
    // 关闭文件描述符
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    
    // 从连接列表移除
    pthread_mutex_lock(&worker->conn_list_lock);
    mt_connection_t **prev = &worker->connections;
    while (*prev && *prev != conn) {
        prev = &(*prev)->next;
    }
    
    if (*prev == conn) {
        *prev = conn->next;
        conn->next = NULL;
    }
    pthread_mutex_unlock(&worker->conn_list_lock);
    
    // 释放连接资源
    pthread_mutex_destroy(&conn->lock);
    free(conn);
}