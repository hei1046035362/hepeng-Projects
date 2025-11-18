// mt_server_main.c
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "hybrid_proactor.h"

mt_proactor_t g_proactor;
extern sig_atomic_t graceful_shutdown;

void signal_handler(int sig) {
    if (graceful_shutdown) {
        return;
    }
    graceful_shutdown = 1;
    printf("\nReceived signal %d, initiating shutdown...\n", sig);
    
    // 设置停止标志
    g_proactor.running = 0;
    
    // 触发退出事件
    if (g_proactor.exit_event_fd >= 0) {
        uint64_t value = 1;
        write(g_proactor.exit_event_fd, &value, sizeof(value));
    }
}

int main(int argc, char *argv[]) {
    int num_workers = 4;
    int port = DEFAULT_PORT;
    
    // 解析命令行参数
    if (argc > 1) {
        num_workers = atoi(argv[1]);
        if (num_workers <= 0 || num_workers > MAX_WORKER_THREADS) {
            fprintf(stderr, "Invalid number of workers: %d\n", num_workers);
            fprintf(stderr, "Usage: %s [workers=%d] [port=%d]\n", argv[0], num_workers, port);
            return 1;
        }
    }
    
    if (argc > 2) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %d\n", port);
            return 1;
        }
    }
    
    printf("Starting multi-threaded hybrid proactor server\n");
    printf("Workers: %d, Port: %d\n", num_workers, port);
    
    // 信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN); // 忽略SIGPIPE
    
    // 初始化Proactor
    if (mt_proactor_init(&g_proactor, num_workers) < 0) {
        fprintf(stderr, "Proactor initialization failed\n");
        return 1;
    }
    
    // 创建服务器socket
    g_proactor.listen_fd = create_server_socket(port);
    if (g_proactor.listen_fd < 0) {
        fprintf(stderr, "Server socket creation failed\n");
        mt_proactor_stop(&g_proactor);
        return 1;
    }
    
    // 启动Proactor
    if (mt_proactor_start(&g_proactor) < 0) {
        fprintf(stderr, "Proactor start failed\n");
        close(g_proactor.listen_fd);
        mt_proactor_stop(&g_proactor);
        return 1;
    }
    
    printf("Multi-threaded server running on port %d\n", port);
    printf("Press Ctrl+C to stop the server\n");
    
    // 等待所有线程结束
    pthread_join(g_proactor.accept_thread, NULL);
    
    for (int i = 0; i < g_proactor.num_workers; i++) {
        if (g_proactor.workers[i].thread) {
            pthread_join(g_proactor.workers[i].thread, NULL);
        }
    }
    
    // 清理资源
    mt_proactor_stop(&g_proactor);
    
    printf("Server shutdown complete\n");
    return 0;
}