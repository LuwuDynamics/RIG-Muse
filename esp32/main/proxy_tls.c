// ESP-IDF 6.0.1 synchronous TLS hook, shared by esp_http_client and Noise WS.
// Link wrapping keeps the upstream HTTP redirects/authentication/parser intact.
// Only built when explicitly enabled. No silent direct fallback on proxy failure.
#include "sdkconfig.h"
#include "proxy_connect.h"
#include "rig_setup.h"
#include "esp_tls.h"
#include "esp_log.h"
#include <errno.h>
#include <string.h>

static const char *TAG = "link.proxy";
int __real_esp_tls_conn_new_sync(const char *, int, int, const esp_tls_cfg_t *, esp_tls_t *);

int __wrap_esp_tls_conn_new_sync(const char *host, int len, int port,
                                const esp_tls_cfg_t *cfg, esp_tls_t *tls) {
    char proxy_host[128]; int proxy_port;
    int mode = rig_setup_proxy(proxy_host, sizeof(proxy_host), &proxy_port);
    if (mode < 0) return -1;
    if (mode == 0) return __real_esp_tls_conn_new_sync(host, len, port, cfg, tls);
    // Current Muse API and Noise callers use blocking TLS. Reject unsupported
    // modes instead of inadvertently connecting directly or sending plaintext.
    esp_tls_conn_state_t state;
    if (!host || len <= 0 || !cfg || !tls || cfg->non_block || cfg->is_plain_tcp ||
        esp_tls_get_conn_state(tls, &state) != ESP_OK || state != ESP_TLS_INIT) return -1;
    int timeout = cfg->timeout_ms > 0 ? cfg->timeout_ms : 15000;
    esp_tls_cfg_t proxy_cfg = *cfg;
    proxy_cfg.is_plain_tcp = true;
    proxy_cfg.timeout_ms = timeout;
    // Native TCP setup preserves socket timeouts, keepalive and interface binding.
    if (__real_esp_tls_conn_new_sync(proxy_host,
            strlen(proxy_host), proxy_port,
            &proxy_cfg, tls) != 1) {
        ESP_LOGW(TAG, "proxy TCP unavailable %s:%d", proxy_host, proxy_port);
        return -1;
    }
    int fd, status = 0;
    if (esp_tls_get_conn_sockfd(tls, &fd) != ESP_OK ||
        proxy_http_connect(fd, host, len, port, timeout, &status) != 0) {
        ESP_LOGW(TAG, "CONNECT failed status=%d errno=%d", status, errno);
        return -1; // caller owns tls, including the proxy socket, on all paths
    }
    // Resume the public ESP-TLS state machine after TCP establishment. IDF 6.0.1
    // initializes mbedTLS in CONNECTING and verifies SNI/certificate against host.
    // esp_tls_init initialized the backend; destroy frees TLS and the owned fd.
    if (esp_tls_set_conn_state(tls, ESP_TLS_CONNECTING) != ESP_OK) return -1;
    ESP_LOGI(TAG, "CONNECT %.*s:%d via %s:%d", len, host, port,
             proxy_host, proxy_port);
    int rc = __real_esp_tls_conn_new_sync(host, len, port, cfg, tls);
    if (rc == 1) ESP_LOGI(TAG, "TLS established through proxy: %.*s", len, host);
    return rc;
}
