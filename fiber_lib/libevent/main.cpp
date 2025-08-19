#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080

struct ConnCtx {
    int fd;
    struct event *read_ev;
    struct event *timer_ev;
};

static void delayed_send_cb(evutil_socket_t fd_unused, short ev, void *arg) {
    struct ConnCtx *ctx = (struct ConnCtx*)arg;

    static const char *body = "Hello, World!";
    char header[256];
    int hl = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n", strlen(body));

    /* 简单发送：处理短写/EAGAIN（客户端慢时会发生） */
    const char *p = header; size_t left = (size_t)hl;
    while (left) {
        ssize_t n = send(ctx->fd, p, left, 0);
        if (n > 0) { p += n; left -= (size_t)n; continue; }
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        break; /* 其它错误，直接退出 */
    }
    p = body; left = strlen(body);
    while (left) {
        ssize_t n = send(ctx->fd, p, left, 0);
        if (n > 0) { p += n; left -= (size_t)n; continue; }
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        break;
    }

    close(ctx->fd);
    if (ctx->read_ev)  event_free(ctx->read_ev);
    if (ctx->timer_ev) event_free(ctx->timer_ev);
    free(ctx);
}

static void http_read_cb(evutil_socket_t fd, short ev, void *arg) {
    struct ConnCtx *ctx = (struct ConnCtx*)arg;
    char buf[1024];
    int len = recv(fd, buf, sizeof(buf), 0);
    if (len <= 0) {
        /* 出错/对端关：清理 */
        close(fd);
        if (ctx->read_ev)  event_free(ctx->read_ev);
        if (ctx->timer_ev) event_free(ctx->timer_ev);
        free(ctx);
        return;
    }

    /* 不要阻塞 usleep(20000) —— 改用一次性定时器延迟 20ms */
    if (ctx->read_ev) event_del(ctx->read_ev);

    struct timeval tv = {0, 20000}; /* 20ms */
    evtimer_add(ctx->timer_ev, &tv);
}

static void accept_cb(evutil_socket_t listener, short ev, void *arg) {
    struct event_base *base = (struct event_base*)arg;
    struct sockaddr_storage ss; socklen_t slen = sizeof(ss);
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0) { perror("accept"); return; }

    /* 关键：客户端 fd 设为非阻塞 */
    evutil_make_socket_nonblocking(fd);

    struct ConnCtx *ctx = (struct ConnCtx*)calloc(1, sizeof(*ctx));
    ctx->fd = fd;
    ctx->read_ev  = event_new(base, fd, EV_READ|EV_PERSIST, http_read_cb, ctx);
    ctx->timer_ev = evtimer_new(base, delayed_send_cb, ctx);

    event_add(ctx->read_ev, NULL);
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }
    evutil_make_socket_nonblocking(listener);
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(PORT);
    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) { perror("bind"); return 1; }
    if (listen(listener, 1024) < 0) { perror("listen"); return 1; }

    struct event_base *base = event_base_new();
    struct event *lev = event_new(base, listener, EV_READ|EV_PERSIST, accept_cb, base);
    event_add(lev, NULL);

    event_base_dispatch(base);

    event_free(lev);
    event_base_free(base);
    close(listener);
    return 0;
}
