// proactor_fixed.h
#ifndef PROACTOR_FIXED_H
#define PROACTOR_FIXED_H

#include <libaio.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

// 异步操作类型
typedef enum {
    OP_ACCEPT,
    OP_READ,
    OP_WRITE,
    OP_CLOSE
} operation_type_t;

// 完成处理器接口
typedef struct completion_handler completion_handler_t;
struct completion_handler {
    void (*handle_read)(completion_handler_t *handler, int fd, void *data, ssize_t bytes);
    void (*handle_write)(completion_handler_t *handler, int fd, void *data, ssize_t bytes);
    void (*handle_error)(completion_handler_t *handler, int fd, void *data, int error);
    void *user_data;
};

// 异步操作
typedef struct async_operation async_operation_t;
struct async_operation {
    operation_type_t type;
    int fd;
    completion_handler_t *handler;
    void *buffer;
    size_t size;
    off_t offset;
    struct iocb iocb;
    async_operation_t *next;
};

// 连接上下文
typedef struct connection_context {
    int fd;
    struct sockaddr_in client_addr;
    char read_buf[4096];
    char write_buf[4096];
    // size_t write_len;
    completion_handler_t *handler;
    void *proactor;
} connection_ctx_t;

// Proactor 核心结构
typedef struct proactor {
    io_context_t aio_ctx;
    int epoll_fd;
    int listen_fd;
    int running;
    int exit_event_fd;
    
    // 线程池
    pthread_t *worker_threads;
    pthread_t dispatcher_thread;
    int thread_count;
    
    // 异步操作队列
    async_operation_t *pending_ops;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    
    // 连接管理
    connection_ctx_t **connections;
    int max_connections;
    
} proactor_t;

// 函数声明 - 只在proactor.c中实现
int proactor_init(proactor_t *proactor, int thread_count, int max_conn);
int proactor_start(proactor_t *proactor);
int proactor_stop(proactor_t *proactor);
int proactor_submit_operation(proactor_t *proactor, async_operation_t *op);
int proactor_add_connection(proactor_t *proactor, int fd, struct sockaddr_in *addr);
void proactor_remove_connection(proactor_t *proactor, int fd);

// 服务器特定函数声明 - 在async_server_proactor.c中实现
int create_server_socket(int port);
void setup_new_connection(proactor_t *proactor, int fd, struct sockaddr_in *addr);

#endif