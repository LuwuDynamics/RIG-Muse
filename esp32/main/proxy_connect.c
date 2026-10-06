// Bounded HTTP CONNECT negotiation. No target DNS lookup or credentials here.
#include "proxy_connect.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_fd(int fd, int writing, int64_t deadline) {
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0) { errno = ETIMEDOUT; return -1; }
        struct timeval tv = { .tv_sec = left / 1000, .tv_usec = (left % 1000) * 1000 };
        fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
        int n = select(fd + 1, writing ? NULL : &set, writing ? &set : NULL, NULL, &tv);
        if (n > 0) return 0;
        if (n == 0) { errno = ETIMEDOUT; return -1; }
        if (errno != EINTR) return -1;
    }
}

int proxy_http_connect(int fd, const char *host, size_t host_len, int port,
                       int timeout_ms, int *status) {
    if (status) *status = 0;
    if (fd < 0 || fd >= FD_SETSIZE || !host || !host_len || host_len > 253 ||
        port < 1 || port > 65535 || timeout_ms <= 0) { errno = EINVAL; return -1; }
    // Reject whitespace, CR/LF, embedded NUL, URI syntax and header injection.
    int ipv6 = 0;
    for (size_t i = 0; i < host_len; ++i) {
        unsigned char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':' || c == '_')) {
            errno = EINVAL; return -1;
        }
        if (c == ':') ipv6 = 1;
    }
    char authority[272], request[640];
    snprintf(authority, sizeof(authority), ipv6 ? "[%.*s]:%d" : "%.*s:%d", (int)host_len, host, port);
    int request_len = snprintf(request, sizeof(request),
        "CONNECT %s HTTP/1.1\r\nHost: %s\r\n\r\n", authority, authority);
    if (request_len < 0 || request_len >= (int)sizeof(request)) { errno = EINVAL; return -1; }
    char *header = malloc(4097);
    if (!header) { errno = ENOMEM; return -1; }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { free(header); return -1; }
    int result = -1;
    int64_t deadline = now_ms() + timeout_ms;
    size_t sent = 0, used = 0;
    while (sent < (size_t)request_len) {
        if (wait_fd(fd, 1, deadline)) goto done;
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, request + sent, request_len - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, request + sent, request_len - sent, 0);
#endif
        if (n > 0) sent += n;
        else if (n == 0) { errno = ECONNRESET; goto done; }
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) goto done;
    }
    // Read exactly the CONNECT header, leaving all TLS bytes in the socket.
    while (used < 4096) {
        if (wait_fd(fd, 0, deadline)) goto done;
        ssize_t n = recv(fd, header + used, 1, 0);
        if (n == 0) { errno = ECONNRESET; goto done; }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            goto done;
        }
        if (header[used] == '\0') { errno = EPROTO; goto done; }
        ++used;
        if (used >= 4 && memcmp(header + used - 4, "\r\n\r\n", 4) == 0) {
            header[used] = 0;
            if (used < 16 || (memcmp(header, "HTTP/1.1 ", 9) && memcmp(header, "HTTP/1.0 ", 9)) ||
                header[9] < '0' || header[9] > '9' || header[10] < '0' || header[10] > '9' ||
                header[11] < '0' || header[11] > '9' || (header[12] != ' ' && header[12] != '\r')) {
                errno = EPROTO; goto done;
            }
            int code = (header[9]-'0')*100 + (header[10]-'0')*10 + header[11]-'0';
            if (status) *status = code;
            if (code != 200) { errno = EACCES; goto done; }
            result = 0; goto done;
        }
    }
    errno = EMSGSIZE;
done:;
    int saved_errno = errno;
    if (fcntl(fd, F_SETFL, flags) < 0) result = -1;
    else errno = saved_errno;
    free(header);
    return result;
}
