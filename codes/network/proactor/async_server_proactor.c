// async_server_proactor.c - 只包含服务器特定实现
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "proactor.h"

#define PORT 8080
#define BUFFER_SIZE 4096

void submit_next_read_operation(proactor_t *proactor, connection_ctx_t *ctx) {
    async_operation_t *read_op = malloc(sizeof(async_operation_t));
    if (!read_op) {
        printf("Failed to allocate read operation for fd=%d\n", ctx->fd);
        return;
    }
    
    memset(read_op, 0, sizeof(async_operation_t));
    read_op->type = OP_READ;
    read_op->fd = ctx->fd;
    read_op->handler = ctx->handler;
    read_op->buffer = ctx->read_buf;
    read_op->size = BUFFER_SIZE - 1; // 留出空间给null终止符
    read_op->offset = 0;
    
    proactor_submit_operation(proactor, read_op);
    // printf("Submitted read operation for fd=%d\n", ctx->fd);
}

// 完成处理器实现
void handle_read_completion(completion_handler_t *handler, int fd, 
                           void *data, ssize_t bytes) {
    connection_ctx_t *ctx = (connection_ctx_t *)data;
    if (!ctx) {
        printf("Context is NULL in read completion for fd=%d\n", fd);
        return;
    }
    
    proactor_t *proactor = (proactor_t *)ctx->proactor;
    if (!proactor) {
        printf("Proactor is NULL in read completion for fd=%d\n", fd);
        return;
    }
    
    // 检查连接是否仍然有效
    if (fd < 0 || fd >= proactor->max_connections || proactor->connections[fd] != ctx) {
        printf("Connection invalid in read completion for fd=%d\n", fd);
        return;
    }
    
    if (bytes == 0) {
        // 对端正常关闭连接
        printf("Client fd=%d closed connection gracefully\n", fd);
        proactor_remove_connection(proactor, fd);
        return;
    } else if (bytes < 0) {
        int error_code = -bytes;
        
        // *** 在worker线程中已处理，这里不用再处理了
        // if (error_code == EAGAIN || error_code == EWOULDBLOCK) {// 处理EAGAIN错误 - 这不是致命错误，应该重试
        //     // printf("Read would block for fd=%d, resubmitting read operation\n", fd);
            
        //     // 延迟后重新提交读操作
        //     usleep(10000); // 延迟10ms
        //     submit_next_read_operation(proactor, ctx);
        //     return;
        // } else {
            // 其他错误，关闭连接
            printf("Read error for fd=%d: %s\n", fd, strerror(error_code));
            proactor_remove_connection(proactor, fd);
            return;
        // }
    }
    
    // 确保字符串以null结尾
    size_t safe_bytes = bytes < BUFFER_SIZE ? bytes : BUFFER_SIZE - 1;
    ctx->read_buf[safe_bytes] = '\0';
    
    printf("Received from fd=%d: %.*s", fd, (int)safe_bytes, ctx->read_buf);
    
    // 回显数据
    const char *echo_prefix = "Echo: ";
    size_t prefix_len = strlen(echo_prefix);
    size_t total_len = prefix_len + safe_bytes;
    
    if (total_len < BUFFER_SIZE) {
        memcpy(ctx->write_buf, echo_prefix, prefix_len);
        memcpy(ctx->write_buf + prefix_len, ctx->read_buf, safe_bytes);
        
        // 提交写操作
        async_operation_t *write_op = malloc(sizeof(async_operation_t));
        if (write_op) {
            write_op->type = OP_WRITE;
            write_op->fd = fd;
            write_op->handler = ctx->handler;
            write_op->buffer = ctx->write_buf;
            write_op->size = total_len;
            write_op->offset = 0;
            
            proactor_submit_operation(proactor, write_op);
        }
    } else {
        // 如果缓冲区不够，放弃回写，直接继续读取下一条
        submit_next_read_operation(proactor, ctx);
    }
}

void handle_write_completion(completion_handler_t *handler, int fd, 
                            void *data, ssize_t bytes) {
    connection_ctx_t *ctx = (connection_ctx_t *)data;
    if (!ctx) {
        printf("Context is NULL in write completion for fd=%d\n", fd);
        return;
    }
    
    proactor_t *proactor = (proactor_t *)ctx->proactor;
    if (!proactor) {
        printf("Proactor is NULL in write completion for fd=%d\n", fd);
        return;
    }
    
    // 检查连接是否仍然有效
    if (fd < 0 || fd >= proactor->max_connections || proactor->connections[fd] != ctx) {
        printf("Connection invalid in write completion for fd=%d\n", fd);
        return;
    }
    
    if (bytes <= 0) {
        int error_code = bytes == 0 ? EPIPE : -bytes;
        
        // *** 在worker线程中已处理，这里不用再处理了
        // if (error_code == EAGAIN || error_code == EWOULDBLOCK) {// 处理EAGAIN错误
        //     printf("Write would block for fd=%d, resubmitting write operation\n", fd);
            
        //     // 重新提交相同的写操作
        //     async_operation_t *write_op = malloc(sizeof(async_operation_t));
        //     if (write_op) {
        //         write_op->type = OP_WRITE;
        //         write_op->fd = fd;
        //         write_op->handler = handler;
        //         write_op->buffer = ctx->write_buf;
        //         write_op->size = ctx->write_len; // 使用之前保存的长度
        //         write_op->offset = 0;
                
        //         proactor_submit_operation(proactor, write_op);
        //     }
        //     return;
        // } else {
            printf("Write failed for fd=%d: %s\n", fd, strerror(error_code));
            proactor_remove_connection(proactor, fd);
            return;
        // }
    }

    printf("Sent %zd bytes to fd=%d\n", bytes, fd);
    
    // 继续读取
    submit_next_read_operation(proactor, ctx);
}

void handle_error_completion(completion_handler_t *handler, int fd, 
                            void *data, int error) {
    connection_ctx_t *ctx = (connection_ctx_t *)data;
    if (!ctx) {
        printf("Context is NULL in error completion for fd=%d\n", fd);
        return;
    }
    
    proactor_t *proactor = (proactor_t *)ctx->proactor;
    if (!proactor) {
        printf("Proactor is NULL in error completion for fd=%d\n", fd);
        return;
    }
    
    printf("Error on fd=%d: %s\n", fd, strerror(error));
    proactor_remove_connection(proactor, fd);
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
    
    // 设置为非阻塞
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        close(fd);
        return -1;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL failed");
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

// 提交欢迎消息
void submit_welcome_message(proactor_t *proactor, connection_ctx_t *ctx) {
    // const char *welcome = "Welcome to Proactor async server! Type something and press enter.\r\n";
    // size_t welcome_len = strlen(welcome);
    
    // if (welcome_len < BUFFER_SIZE) {
    //     memcpy(ctx->write_buf, welcome, welcome_len);
    //     ctx->write_len = welcome_len; // 保存消息长度
        
    //     async_operation_t *write_op = malloc(sizeof(async_operation_t));
    //     if (write_op) {
    //         memset(write_op, 0, sizeof(async_operation_t));
    //         write_op->type = OP_WRITE;
    //         write_op->fd = ctx->fd;
    //         write_op->handler = ctx->handler;
    //         write_op->buffer = ctx->write_buf;
    //         write_op->size = welcome_len;
    //         write_op->offset = 0;
            
    //         printf("Submitting welcome message to fd=%d\n", ctx->fd);
    //         proactor_submit_operation(proactor, write_op);
    //     } else {
    //         printf("Failed to allocate write operation for welcome message\n");
    //         // 如果欢迎消息发送失败，延迟后开始读取
    //         usleep(100000); // 延迟100ms
    //         submit_next_read_operation(proactor, ctx);
    //     }
    // } else {
    //     // 欢迎消息太长，延迟后开始读取
    //     usleep(100000); // 延迟100ms
        submit_next_read_operation(proactor, ctx);
    // }
}
// 设置新连接（只在async_server_proactor.c中定义）
void setup_new_connection(proactor_t *proactor, int fd, struct sockaddr_in *addr) {
    // 首先添加到连接管理
    if (proactor_add_connection(proactor, fd, addr) < 0) {
        fprintf(stderr, "Failed to add connection for fd=%d\n", fd);
        close(fd);
        return;
    }
    
    connection_ctx_t *ctx = proactor->connections[fd];
    if (!ctx) {
        fprintf(stderr, "Connection context not found for fd=%d\n", fd);
        return;
    }
    
    // 创建完成处理器
    completion_handler_t *handler = malloc(sizeof(completion_handler_t));
    if (!handler) {
        fprintf(stderr, "Failed to allocate handler for fd=%d\n", fd);
        proactor_remove_connection(proactor, fd);
        return;
    }

    memset(handler, 0, sizeof(completion_handler_t));
    handler->handle_read = handle_read_completion;
    handler->handle_write = handle_write_completion;
    handler->handle_error = handle_error_completion;
    handler->user_data = ctx;
    
    ctx->handler = handler;
    
    printf("Connection setup complete for fd=%d\n", fd);

    // 只发送欢迎消息，不立即开始读取
    submit_welcome_message(proactor, ctx);
    
}

proactor_t g_proactor;

void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_proactor.running = 0;
}

int main() {
    // 信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Initializing Proactor server...\n");
    
    // 初始化Proactor
    if (proactor_init(&g_proactor, 4, 10000) < 0) {
        fprintf(stderr, "Proactor initialization failed\n");
        return -1;
    }
    
    // 创建服务器socket
    g_proactor.listen_fd = create_server_socket(PORT);
    if (g_proactor.listen_fd < 0) {
        fprintf(stderr, "Server socket creation failed\n");
        proactor_stop(&g_proactor);
        return -1;
    }
    
    // 启动Proactor
    if (proactor_start(&g_proactor) < 0) {
        fprintf(stderr, "Proactor start failed\n");
        close(g_proactor.listen_fd);
        proactor_stop(&g_proactor);
        return -1;
    }
    
    printf("Proactor server running on port %d. Press Ctrl+C to stop.\n", PORT);
    
    // 等待Proactor停止
    while (g_proactor.running) {
        sleep(1);
    }
    
    printf("Shutting down server...\n");
    proactor_stop(&g_proactor);
        
    printf("Server shutdown complete.\n");
    return 0;
}