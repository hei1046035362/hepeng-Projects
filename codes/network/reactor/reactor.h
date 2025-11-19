#ifndef REACTOR_H
#define REACTOR_H

#include "ring_queue.h"
#include <sys/epoll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 8192
#define MAX_CONNECTIONS 100000
#define MAX_REACTOR_THREADS 16
#define BATCH_SIZE 64

// 连接结构
typedef struct connection_s {
    int fd;
    atomic_int state;
    
    char read_buf[BUFFER_SIZE];
    atomic_uint read_len;
    
    char write_buf[BUFFER_SIZE];
    atomic_uint write_len;
    atomic_uint write_sent;
    
    void *user_data;
    uint64_t last_active_time;
    int thread_id;
} connection_t;

// Reactor线程上下文
typedef struct reactor_thread_s {
    int id;
    pthread_t thread_id;
    atomic_bool running;
    
    int epoll_fd;
    connection_t *connections[MAX_CONNECTIONS];
    atomic_uint connection_count;
    
    // 使用优化的环形队列
    ring_queue_t accept_queue;
    
    // 每个线程独立的统计信息
    _Alignas(64) atomic_ullong total_connections;
    _Alignas(64) atomic_ullong active_connections;
    _Alignas(64) atomic_ullong processed_events;
    _Alignas(64) atomic_ullong batch_processed;
} reactor_thread_t;

// 主Reactor结构
typedef struct reactor_s {
    reactor_thread_t threads[MAX_REACTOR_THREADS];
    int thread_count;
    atomic_bool running;
    atomic_uint next_thread;
    
    // 回调函数指针
    void (*on_connect)(connection_t *conn);
    void (*on_data)(connection_t *conn);
    void (*on_write)(connection_t *conn);
    void (*on_close)(connection_t *conn);
} reactor_t;

// 前向声明
void handle_close_event(reactor_thread_t *thread, connection_t *conn);

// API
reactor_t* reactor_create(int thread_count);
int reactor_destroy(reactor_t *reactor);
int reactor_add_connection(reactor_t *reactor, int fd);
int reactor_run(reactor_t *reactor);
int reactor_stop(reactor_t *reactor);
void reactor_stats(reactor_t *reactor);

#endif