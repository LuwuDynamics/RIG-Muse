#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Negotiate HTTP CONNECT on an already connected socket. Returns 0 on success.
// Does not consume tunnel bytes or own/close fd. Restores original fd flags.
// status is 0 until a complete HTTP response header is received.
int proxy_http_connect(int fd, const char *host, size_t host_len, int port,
                       int timeout_ms, int *status);
#ifdef __cplusplus
}
#endif
