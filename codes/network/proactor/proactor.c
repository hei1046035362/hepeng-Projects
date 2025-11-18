// proactor.c - 只包含Proactor核心实现
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

#include "proactor.h"

// 设置文件描述符为非阻塞
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 初始化 Proactor
int proactor_init(proactor_t *proactor, int thread_count, int max_conn) {
    memset(proactor, 0, sizeof(proactor_t));
    
    // 初始化 AIO 上下文
    if (io_setup(10000, &proactor->aio_ctx) < 0) {
        perror("io_setup failed");
        return -1;
    }
    
    // 创建 epoll 实例
    proactor->epoll_fd = epoll_create1(0);
    if (proactor->epoll_fd < 0) {
        perror("epoll_create1 failed");
        io_destroy(proactor->aio_ctx);
        return -1;
    }
    
    // 创建退出事件fd
    proactor->exit_event_fd = eventfd(0, EFD_NONBLOCK);
    if (proactor->exit_event_fd < 0) {
        perror("eventfd failed");
        close(proactor->epoll_fd);
        io_destroy(proactor->aio_ctx);
        return -1;
    }
    
    // 初始化连接管理
    proactor->max_connections = max_conn;
    proactor->connections = calloc(max_conn, sizeof(connection_ctx_t*));
    if (!proactor->connections) {
        perror("calloc failed");
        close(proactor->epoll_fd);
        close(proactor->exit_event_fd);
        io_destroy(proactor->aio_ctx);
        return -1;
    }
    
    // 初始化队列和互斥锁
    pthread_mutex_init(&proactor->queue_mutex, NULL);
    pthread_cond_init(&proactor->queue_cond, NULL);
    
    proactor->thread_count = thread_count;
    proactor->worker_threads = malloc(thread_count * sizeof(pthread_t));
    if (!proactor->worker_threads) {
        perror("malloc failed");
        free(proactor->connections);
        close(proactor->epoll_fd);
        close(proactor->exit_event_fd);
        io_destroy(proactor->aio_ctx);
        return -1;
    }
    
    proactor->running = 1;
    
    printf("Proactor initialized: %d threads, %d max connections\n", 
           thread_count, max_conn);
    return 0;
}

// 工作者线程函数
static void *worker_thread_func(void *arg) {
    proactor_t *proactor = (proactor_t *)arg;
    
    while (proactor->running) {
        pthread_mutex_lock(&proactor->queue_mutex);
        
        // 使用超时等待，避免永久阻塞
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // 1秒超时
        
        int wait_result = 0;
        while (proactor->pending_ops == NULL && proactor->running) {
            wait_result = pthread_cond_timedwait(&proactor->queue_cond, 
                                               &proactor->queue_mutex, &ts);
            if (wait_result == ETIMEDOUT) {
                break;
            }
        }
        
        if (!proactor->running) {
            pthread_mutex_unlock(&proactor->queue_mutex);
            break;
        }
        
        // 获取操作
        async_operation_t *op = proactor->pending_ops;
        if (op) proactor->pending_ops = op->next;
        pthread_mutex_unlock(&proactor->queue_mutex);
        
        if(!op) {
            continue;
        }
        // 执行异步操作
        struct iocb *iocbs[1] = { &op->iocb };
        int ret;
        
        switch (op->type) {
            case OP_READ:
                io_prep_pread(&op->iocb, op->fd, op->buffer, op->size, op->offset);
                op->iocb.data = op;
                ret = io_submit(proactor->aio_ctx, 1, iocbs);
                if (ret != 1) {
                    if (ret == -EAGAIN) {
                        printf("io_submit read EAGAIN for fd=%d, retrying...\n", op->fd);
                        // 重新提交操作
                        usleep(10000);
                        proactor_submit_operation(proactor, op);
                    } else {
                        fprintf(stderr, "io_submit read failed: %d\n", ret);
                        free(op);
                    }
                } else {
                    // printf("Read operation submitted successfully for fd=%d\n", op->fd);
                }
                break;
                
            case OP_WRITE:
                io_prep_pwrite(&op->iocb, op->fd, op->buffer, op->size, op->offset);
                op->iocb.data = op;
                ret = io_submit(proactor->aio_ctx, 1, iocbs);
                if (ret != 1) {
                    if (ret == -EAGAIN) {
                        printf("io_submit write EAGAIN for fd=%d, retrying...\n", op->fd);
                        // 重新提交操作
                        usleep(10000);
                        proactor_submit_operation(proactor, op);
                    } else {
                        fprintf(stderr, "io_submit write failed: %d\n", ret);
                        free(op);
                    }
                } else {
                    printf("Write operation submitted successfully for fd=%d\n", op->fd);
                }
                break;
                
            default:
                fprintf(stderr, "Unknown operation type: %d\n", op->type);
                free(op);
                break;
        }
    }
    
    return NULL;
}

// 处理连接事件
static void handle_connection_event(proactor_t *proactor, int fd, uint32_t events) {
    if (events & (EPOLLERR | EPOLLHUP)) {
        printf("Connection error or hangup on fd=%d\n", fd);
        proactor_remove_connection(proactor, fd);
        return;
    }
}

// 处理新连接
static void handle_new_connection(proactor_t *proactor) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(proactor->listen_fd, 
                          (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept failed");
        }
        return;
    }
    
    // 设置为非阻塞
    if (set_nonblock(client_fd) < 0) {
        perror("set_nonblock failed");
        close(client_fd);
        return;
    }
    
    printf("New connection from %s:%d, fd=%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);
    
    // 设置新连接（在async_server_proactor.c中实现）
    setup_new_connection(proactor, client_fd, &client_addr);
    
    // 将新连接添加到epoll监控（主要用于错误检测）
    struct epoll_event ev;
    ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    ev.data.fd = client_fd;
    if (epoll_ctl(proactor->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
        perror("epoll_ctl for client failed");
    }
}

// 分发线程函数
static void *dispatcher_thread_func(void *arg) {
    proactor_t *proactor = (proactor_t *)arg;
    struct io_event events[64];
    struct timespec timeout = { 0, 10000000 }; // 10ms
    
    // 添加退出事件到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = proactor->exit_event_fd;
    if (epoll_ctl(proactor->epoll_fd, EPOLL_CTL_ADD, proactor->exit_event_fd, &ev) < 0) {
        perror("epoll_ctl exit_event_fd failed");
    }
    
    // 添加监听socket到epoll
    ev.events = EPOLLIN;
    ev.data.fd = proactor->listen_fd;
    if (epoll_ctl(proactor->epoll_fd, EPOLL_CTL_ADD, proactor->listen_fd, &ev) < 0) {
        perror("epoll_ctl listen_fd failed");
    }
    
    while (proactor->running) {
        // 处理epoll事件
        struct epoll_event epoll_events[64];
        int nfds = epoll_wait(proactor->epoll_fd, epoll_events, 64, 10); // 10ms超时
        
        for (int i = 0; i < nfds; i++) {
            int fd = epoll_events[i].data.fd;
            
            if (fd == proactor->exit_event_fd) {
                printf("Exit event received\n");
                proactor->running = 0;
                break;
            } else if (fd == proactor->listen_fd) {
                handle_new_connection(proactor);
            } else {
                // 连接退出，连接错误等事件
                //(在同步模式中是通过read或者write的返回值来判断，在异步编程中只能通过连接事件来判断)
                handle_connection_event(proactor, fd, epoll_events[i].events);
            }
        }
        
        if (!proactor->running) break;
        
        // 获取完成的AIO事件
        int num_events = io_getevents(proactor->aio_ctx, 0, 64, events, &timeout);
        if (num_events < 0) {
            if (errno != EINTR) {
                perror("io_getevents failed");
            }
            continue;
        }
        
        for (int i = 0; i < num_events; i++) {
            async_operation_t *op = (async_operation_t *)events[i].data;
            if (!op) continue;

            int fd = op->fd;
            if (fd < 0 || fd >= proactor->max_connections) {
                printf("Invalid fd in completion event: %d\n", fd);
                free(op);
                continue;
            }

            connection_ctx_t *ctx = proactor->connections[fd];
            if (!ctx) {
                printf("Connection already closed for fd=%d, skipping completion\n", fd);
                free(op);
                continue;
            }

            completion_handler_t *handler = op->handler;
            if (!handler) {
                free(op);
                continue;
            }

            // 验证handler是否与连接上下文匹配
            if (handler->user_data != ctx) {
                printf("Handler mismatch for fd=%d\n", fd);
                free(op);
                continue;
            }

            if (events[i].res < 0) {
                // 错误处理
                if (handler->handle_error) {
                    handler->handle_error(handler, op->fd, handler->user_data, -events[i].res);
                }
            } else {
                int error_code = -events[i].res;
                if(error_code == EAGAIN || error_code == EWOULDBLOCK) {
                    usleep(10000);
                    proactor_submit_operation(proactor, op);
                    continue;
                }
                // 成功处理
                switch (op->type) {
                    case OP_READ:
                        if (handler->handle_read) {
                            handler->handle_read(handler, op->fd, handler->user_data, events[i].res);
                        }
                        break;
                        
                    case OP_WRITE:
                        if (handler->handle_write) {
                            handler->handle_write(handler, op->fd, handler->user_data, events[i].res);
                        }
                        break;
                        
                    default:
                        printf("Unknown operation type in completion: %d\n", op->type);
                        break;
                }
            }
            
            free(op);
        }
    }
    
    return NULL;
}

// 启动Proactor
int proactor_start(proactor_t *proactor) {
    // 启动工作者线程
    for (int i = 0; i < proactor->thread_count; i++) {
        if (pthread_create(&proactor->worker_threads[i], NULL, 
                          worker_thread_func, proactor) != 0) {
            perror("pthread_create worker failed");
            return -1;
        }
    }
    
    // 启动分发线程
    if (pthread_create(&proactor->dispatcher_thread, NULL, 
                      dispatcher_thread_func, proactor) != 0) {
        perror("pthread_create dispatcher failed");
        return -1;
    }
    
    printf("Proactor started\n");
    return 0;
}

// 专门的资源清理函数
static void cleanup_proactor_resources(proactor_t *proactor) {
    // 关闭文件描述符
    if (proactor->epoll_fd >= 0) {
        close(proactor->epoll_fd);
        proactor->epoll_fd = -1;
    }
    
    if (proactor->exit_event_fd >= 0) {
        close(proactor->exit_event_fd);
        proactor->exit_event_fd = -1;
    }
    
    if (proactor->listen_fd >= 0) {
        close(proactor->listen_fd);
        proactor->listen_fd = -1;
    }
    
    // 销毁AIO上下文
    if (proactor->aio_ctx) {
        io_destroy(proactor->aio_ctx);
        proactor->aio_ctx = 0;
    }
    
    // 清理连接
    if (proactor->connections) {
        for (int i = 0; i < proactor->max_connections; i++) {
            if (proactor->connections[i]) {
                close(i);
                if (proactor->connections[i]->handler) {
                    free(proactor->connections[i]->handler);
                }
                free(proactor->connections[i]);
                proactor->connections[i] = NULL;
            }
        }
        free(proactor->connections);
        proactor->connections = NULL;
    }
    
    // 清理线程数组
    if (proactor->worker_threads) {
        free(proactor->worker_threads);
        proactor->worker_threads = NULL;
    }
    
    // 销毁互斥锁和条件变量
    pthread_mutex_destroy(&proactor->queue_mutex);
    pthread_cond_destroy(&proactor->queue_cond);
}

// 使用标准POSIX函数的线程等待（兼容性更好）
static int wait_thread_with_timeout(pthread_t thread, int timeout_seconds) {
    time_t start_time = time(NULL);
    
    while (time(NULL) - start_time < timeout_seconds) {
        void *retval;
        int result = pthread_join(thread, &retval);
        if (result == 0) {
            printf("Thread joined successfully\n");
            return 0;
        }
        usleep(100000); // 等待100ms
    }
    
    printf("Thread did not exit within timeout\n");
    return -1;
}

// 改进的停止函数（使用标准POSIX函数）
int proactor_stop(proactor_t *proactor) {
    if (!proactor || !proactor->running) {
        return 0;
    }
    
    printf("Initiating proactor shutdown...\n");
    
    // 1. 设置停止标志
    proactor->running = 0;
    
    // 2. 广播条件变量唤醒所有等待的线程
    printf("Broadcasting condition variable to wake up threads...\n");
    pthread_mutex_lock(&proactor->queue_mutex);
    pthread_cond_broadcast(&proactor->queue_cond);
    pthread_mutex_unlock(&proactor->queue_mutex);
    
    // 3. 触发退出事件唤醒分发线程
    printf("Triggering exit event...\n");
    uint64_t value = 1;
    if (proactor->exit_event_fd >= 0) {
        if (write(proactor->exit_event_fd, &value, sizeof(value)) < 0) {
            perror("Warning: write to exit_event_fd failed");
        }
    }
    
    // 4. 等待线程自然退出（5秒超时）
    printf("Waiting for threads to exit...\n");
    
    // 终止分发线程
    if (proactor->dispatcher_thread) {
        if (wait_thread_with_timeout(proactor->dispatcher_thread, 2) != 0) {
            printf("Cancelling dispatcher thread...\n");
            pthread_cancel(proactor->dispatcher_thread);
            sleep(1);
            pthread_detach(proactor->dispatcher_thread);
        } else {
            printf("Dispatcher thread exit naturally\n");
        }
    }
    
    // 终止工作者线程
    for (int i = 0; i < proactor->thread_count; i++) {
        if (proactor->worker_threads[i]) {
            if (wait_thread_with_timeout(proactor->worker_threads[i], 2) != 0) {
                printf("Cancelling worker thread %d...\n", i);
                pthread_cancel(proactor->worker_threads[i]);
                sleep(1);
                pthread_detach(proactor->worker_threads[i]);
            } else {
                printf("Worker thread exit naturally\n");
            }
        }
    }
    
    // 5. 清理资源
    printf("Cleaning up resources...\n");
    cleanup_proactor_resources(proactor);
    
    printf("Proactor shutdown complete\n");
    return 0;
}


// 提交异步操作
int proactor_submit_operation(proactor_t *proactor, async_operation_t *op) {
    pthread_mutex_lock(&proactor->queue_mutex);
    
    op->next = proactor->pending_ops;
    proactor->pending_ops = op;
    
    pthread_cond_signal(&proactor->queue_cond);
    pthread_mutex_unlock(&proactor->queue_mutex);
    
    return 0;
}

// 添加连接（只在proactor.c中定义）
int proactor_add_connection(proactor_t *proactor, int fd, struct sockaddr_in *addr) {
    if (fd < 0 || fd >= proactor->max_connections) {
        fprintf(stderr, "Invalid file descriptor: %d\n", fd);
        return -1;
    }
    
    if (proactor->connections[fd] != NULL) {
        fprintf(stderr, "Connection already exists for fd: %d\n", fd);
        return -1;
    }
    
    connection_ctx_t *ctx = malloc(sizeof(connection_ctx_t));
    if (!ctx) {
        fprintf(stderr, "Failed to allocate connection context\n");
        return -1;
    }
    
    memset(ctx, 0, sizeof(connection_ctx_t));
    ctx->fd = fd;
    if (addr) {
        ctx->client_addr = *addr;
    }
    ctx->proactor = proactor;
    
    proactor->connections[fd] = ctx;
    
    return 0;
}

// 移除连接
void proactor_remove_connection(proactor_t *proactor, int fd) {
    if (fd < 0 || fd >= proactor->max_connections) {
        printf("Invalid fd in remove_connection: %d\n", fd);
        return;
    }
    
    connection_ctx_t *ctx = proactor->connections[fd];
    if (!ctx) {
        printf("Connection already removed for fd=%d\n", fd);
        return;
    }
    
    printf("Removing connection fd=%d\n", fd);
    
    // 先从epoll中移除
    if (proactor->epoll_fd >= 0) {
        epoll_ctl(proactor->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    }
    
    // 关闭文件描述符
    close(fd);
    
    // 清理handler
    if (ctx->handler) {
        free(ctx->handler);
        ctx->handler = NULL;
    }
    
    // 从连接数组中移除
    proactor->connections[fd] = NULL;
    
    // 最后释放上下文
    free(ctx);
}