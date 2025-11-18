// mt_hybrid_proactor.h
#ifndef MT_HYBRID_PROACTOR_H
#define MT_HYBRID_PROACTOR_H

#include <libaio.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#define MAX_EVENTS 64
#define BUFFER_SIZE 4096
#define DEFAULT_PORT 8080
#define MAX_WORKER_THREADS 16

// 连接状态
typedef enum {
    CONN_ACTIVE,
    CONN_IDLE, 
    CONN_SLOW,
    CONN_SUSPENDED
} conn_state_t;

// 连接结构
typedef struct mt_connection {
    int fd;
    struct sockaddr_in client_addr;
    conn_state_t state;
    int readable;
    int writable;
    char read_buf[BUFFER_SIZE];
    char write_buf[BUFFER_SIZE];
    size_t write_pending;
    time_t last_activity;
    
    // 线程安全字段
    atomic_int is_removing;
    atomic_int ref_count;
    pthread_mutex_t lock;
    
    // 所属工作线程
    int worker_id;
    
    struct mt_connection *next;
} mt_connection_t;

// 工作线程上下文
typedef struct {
    int id;
    pthread_t thread;
    int running;
    
    // 每个工作线程有自己的epoll实例
    int epoll_fd;
    
    // 每个工作线程有自己的AIO上下文
    io_context_t aio_ctx;
    
    // 工作线程管理的连接列表
    mt_connection_t *connections;
    pthread_mutex_t conn_list_lock;
    
    // 统计信息
    unsigned long total_operations;
    unsigned long eagain_errors;
    unsigned long successful_ops;
    
    // 指向主proactor的指针
    struct mt_proactor *proactor;
} worker_context_t;

// 主Proactor结构
typedef struct mt_proactor {
    int listen_fd;
    int running;
    int exit_event_fd;
    
    // 工作线程管理
    int num_workers;
    worker_context_t *workers;
    
    // 接受连接线程
    pthread_t accept_thread;
    
    // 连接分配策略
    int next_worker; // 轮询分配
    
    // 全局统计
    unsigned long total_connections;
    unsigned long total_operations;
    
    // 同步原语
    pthread_mutex_t accept_lock;
    pthread_cond_t accept_cond;
} mt_proactor_t;

// 函数声明
// 初始化
int mt_proactor_init(mt_proactor_t *proactor, int num_workers);
int mt_proactor_start(mt_proactor_t *proactor);
int mt_proactor_stop(mt_proactor_t *proactor);

// 网络
int create_server_socket(int port);

// 工作线程
void *worker_thread_func(void *arg);
void *accept_thread_func(void *arg);

// 连接管理
mt_connection_t *mt_create_connection(int fd, struct sockaddr_in *addr, int worker_id);
void mt_remove_connection_safe(worker_context_t *worker, mt_connection_t *conn);
void mt_add_connection_to_worker(worker_context_t *worker, mt_connection_t *conn);

// 事件处理
void mt_handle_connection_event(worker_context_t *worker, mt_connection_t *conn, uint32_t events);
void mt_try_read(worker_context_t *worker, mt_connection_t *conn);
void mt_try_write(worker_context_t *worker, mt_connection_t *conn);
void mt_process_data(worker_context_t *worker, mt_connection_t *conn, const char *data, size_t len);

// 异步操作
void mt_submit_async_read(worker_context_t *worker, mt_connection_t *conn);
void mt_submit_async_write(worker_context_t *worker, mt_connection_t *conn);
void mt_handle_aio_completion(worker_context_t *worker, struct io_event *event);

// 工具函数
int set_nonblock(int fd);
void mt_monitor_performance(mt_proactor_t *proactor);

#endif