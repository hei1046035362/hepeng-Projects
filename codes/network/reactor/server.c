#include "reactor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>


#define SERVER_PORT 8080

// 创建服务器socket
int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }
    
    // 设置服务器socket为非阻塞
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        close(server_fd);
        return -1;
    }
    
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 65535) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }
    
    printf("Server listening on port %d\n", port);
    return server_fd;
}

// 接受连接线程
void* accept_thread_main(void *arg) {
    reactor_t *reactor = (reactor_t*)arg;
    int server_fd = create_server_socket(SERVER_PORT);
    
    if (server_fd == -1) {
        printf("Failed to create server socket\n");
        return NULL;
    }
    
    printf("Accept thread started\n");
    
    while (atomic_load(&reactor->running)) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有新连接，短暂休眠后继续
                usleep(1000);
                continue;
            }
            perror("accept");
            break;
        }
        
        printf("Accepted new connection: fd=%d\n", client_fd);
        
        // 使用无锁方式添加到reactor
        if (reactor_add_connection(reactor, client_fd) != 0) {
            close(client_fd);
            printf("Failed to add connection to reactor\n");
        }
    }
    
    close(server_fd);
    printf("Accept thread stopped\n");
    return NULL;
}

int main() {
    // 获取CPU核心数
    int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
    int thread_count = cpu_cores > 0 ? cpu_cores : 4;
    
    printf("Creating reactor with %d threads\n", thread_count);
    
    // 创建高性能Reactor
    reactor_t *reactor = reactor_create(thread_count);
    if (!reactor) {
        printf("Failed to create reactor\n");
        return 1;
    }
    
    // 启动Reactor
    if (reactor_run(reactor) != 0) {
        printf("Failed to start reactor\n");
        reactor_destroy(reactor);
        return 1;
    }
    
    printf("Reactor started successfully\n");
    
    // 等待Reactor线程完全启动
    printf("Waiting for reactor threads to start...\n");
    usleep(500000); // 500ms 确保Reactor线程进入循环
    
    // 启动接受连接线程
    pthread_t accept_thread;
    if (pthread_create(&accept_thread, NULL, accept_thread_main, reactor) != 0) {
        perror("pthread_create");
        printf("Failed to create accept thread\n");
        reactor_stop(reactor);
        reactor_destroy(reactor);
        return 1;
    }
    
    printf("Server running. Press Enter to stop...\n");
    
    // 等待用户输入
    getchar();
    
    // 清理
    printf("Stopping server...\n");
    reactor_stop(reactor);
    pthread_join(accept_thread, NULL);
    reactor_destroy(reactor);
    
    printf("Server stopped\n");
    return 0;
}