#include "ioscheduler.h"
#include "hook.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <stack>
#include <cstring>
#include <chrono>
#include <thread>
#include <memory>

static int sock_listen_fd = -1;

void test_accept();
void error(const char *msg)
{
    perror(msg);
    printf("erreur...\n");
    exit(1);
}

void watch_io_read()
{
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

void test_accept()
{
    // 确保当前线程启用 hook
    if (sylar::is_hook_enable && !sylar::is_hook_enable()) {
        sylar::set_hook_enable(true);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);

    // 1) accept 循环到 EAGAIN（排干本批就绪连接）
    for (;;) {
        int fd = accept(sock_listen_fd, (struct sockaddr *)&addr, &len);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 队列空了
            else break;
        }

        // ★ 关键：不要把已接入 fd 设为非阻塞，让 hook 接管等待点
        // fcntl(fd, F_SETFL, O_NONBLOCK);

        // 2) 用同步风格处理该连接：recv → usleep(20ms) → send → close
        sylar::IOManager::GetThis()->addEvent(fd, sylar::IOManager::READ, [fd]() {
            // 回调所在的工作线程也要确保 hook 已开启
            if (sylar::is_hook_enable && !sylar::is_hook_enable()) {
                sylar::set_hook_enable(true);
            }

            char buffer[1024];
            // 同步读：无数据时 hook 会等 EPOLLIN 并让出，不阻塞线程
            ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) { close(fd); return; }

            // 统一 20ms 等待：hook 转成定时器等待 + 让出
            usleep(20000);

            // 构建响应
            static const char *body = "Hello, World!";
            char header[256];
            int hl = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              strlen(body));

            // 循环发送，处理短写/EINTR/EAGAIN；不可写时 hook 会等 EPOLLOUT 并让出
            size_t off = 0;
            while (off < (size_t)hl) {
                ssize_t m = send(fd, header + off, hl - off, 0);
                if (m > 0) { off += (size_t)m; continue; }
                if (m < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                // 其它错误
                close(fd);
                return;
            }
            off = 0;
            while (off < strlen(body)) {
                ssize_t m = send(fd, body + off, strlen(body) - off, 0);
                if (m > 0) { off += (size_t)m; continue; }
                if (m < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                // 其它错误
                close(fd);
                return;
            }

            close(fd);
            // 短连接完成后不需要重挂 fd 的 READ
        });
    }

    // 监听 fd 的 READ 是一次性触发，这里需要重挂
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

void test_iomanager()
{
    int portno = 8080;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 设置套接字
    sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen_fd < 0) {
        error("Error creating socket..\n");
    }

    int yes = 1;
    // 解决 "address already in use" 错误
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定套接字并监听连接
    if (bind(sock_listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        error("Error binding socket..\n");

    if (listen(sock_listen_fd, 1024) < 0) {
        error("Error listening..\n");
    }

    // printf("epoll echo server listening for connections on port: %d\n", portno);
    // 监听 fd 设为非阻塞 + accept 到 EAGAIN
    int fl = fcntl(sock_listen_fd, F_GETFL, 0);
    fcntl(sock_listen_fd, F_SETFL, fl | O_NONBLOCK);

    // 3) 线程数按需调整；保持和你原来一致（1），便于对比
    sylar::IOManager iom(2);

    // 确保主调度线程也开启 hook（保险）
    if (sylar::is_hook_enable && !sylar::is_hook_enable()) {
        sylar::set_hook_enable(true);
    }

    iom.addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

int main(int argc, char *argv[])
{
    test_iomanager();
    return 0;
}
