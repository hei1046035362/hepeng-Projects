import socket

SERVER_IP = '192.168.40.128'
SERVER_PORT = 8080

def main():
    # 创建TCP socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        # 连接本地8080端口
        client_socket.connect((SERVER_IP, SERVER_PORT))
        print("已连接到服务器，输入消息开始对话（输入'exit'退出）")

        while True:
            # 获取用户输入
            message = input("发送: ")

            # 如果输入exit则退出循环
            if message.lower() == 'exit':
                break

            # 发送消息到服务器
            client_socket.send(message.encode())

            # 接收服务器响应（可选）
            response = client_socket.recv(1024).decode()
            if response:
                print(f"收到回复: {response}")

    except Exception as e:
        print(f"连接错误: {e}")
    finally:
        # 关闭连接
        client_socket.close()
        print("连接已关闭")

if __name__ == "__main__":
    main()