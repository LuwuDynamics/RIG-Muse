// Local provisioning for reusable, credential-free RIG firmware.
#include "rig_setup.h"
#include "config_store.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TOKEN_KEY "rig_sdk_token"
#define PROXY_KEY "rig_proxy"
static char s_token[49], s_proxy_host[128];
static int s_proxy_mode, s_proxy_port;

static bool token_valid(const char *token) {
    if (!token || strlen(token) != 48 || strncmp(token, "mgst_", 5)) return false;
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
    for (int i = 5; i < 48; ++i) if (!strchr(alphabet, token[i])) return false;
    return strchr("AEIMQUYcgkosw048", token[47]) != NULL;
}

#ifdef CONFIG_RIG_HTTP_PROXY
static bool proxy_valid(const cJSON *params, const char **host, int *port) {
    const cJSON *h = cJSON_GetObjectItemCaseSensitive(params, "host");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(params, "port");
    if (!cJSON_IsString(h) || !h->valuestring[0] || strlen(h->valuestring) >= sizeof(s_proxy_host) ||
        !cJSON_IsNumber(p) || p->valuedouble < 1 || p->valuedouble > 65535 ||
        p->valuedouble != p->valueint) return false;
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-";
    for (const char *c = h->valuestring; *c; ++c) if (!strchr(alphabet, *c)) return false;
    *host = h->valuestring; *port = p->valueint;
    return true;
}
#endif

void rig_setup_init(void) {
    s_token[0] = s_proxy_host[0] = 0; s_proxy_mode = 0; s_proxy_port = 0;
    if (config_key_lookup(TOKEN_KEY) == CONFIG_KEY_NOT_FOUND) {
        if (token_valid(CONFIG_GADGET_SDK_TOKEN)) snprintf(s_token, sizeof(s_token), "%s", CONFIG_GADGET_SDK_TOKEN);
    } else if (!config_get_str(TOKEN_KEY, s_token, sizeof(s_token)) || !token_valid(s_token)) {
        s_token[0] = 0; // Invalid or unreadable stored credentials never expose a build token.
    }
#ifdef CONFIG_RIG_HTTP_PROXY
    if (CONFIG_RIG_HTTP_PROXY_HOST[0]) {
        snprintf(s_proxy_host, sizeof(s_proxy_host), "%s", CONFIG_RIG_HTTP_PROXY_HOST);
        s_proxy_port = CONFIG_RIG_HTTP_PROXY_PORT; s_proxy_mode = 1;
    }
    config_key_lookup_t state = config_key_lookup(PROXY_KEY);
    if (state != CONFIG_KEY_NOT_FOUND) {
        char saved[256];
        s_proxy_mode = -1; // An invalid saved proxy must not silently use a direct connection.
        if (config_get_str(PROXY_KEY, saved, sizeof(saved))) {
            if (!strcmp(saved, "off")) { s_proxy_mode = 0; s_proxy_host[0] = 0; s_proxy_port = 0; }
            else {
                cJSON *p = cJSON_Parse(saved); const char *host; int port;
                if (proxy_valid(p, &host, &port)) {
                    snprintf(s_proxy_host, sizeof(s_proxy_host), "%s", host);
                    s_proxy_port = port; s_proxy_mode = 1;
                }
                cJSON_Delete(p);
            }
        }
    }
#endif
}

const char *rig_setup_sdk_token(void) { return s_token[0] ? s_token : NULL; }
int rig_setup_proxy(char *host, size_t capacity, int *port) {
    if (s_proxy_mode != 1) return s_proxy_mode;
    if (!host || !port || capacity <= strlen(s_proxy_host)) return -1;
    snprintf(host, capacity, "%s", s_proxy_host); *port = s_proxy_port;
    return 1;
}

static cJSON *reply(bool ok, const char *error, bool restart) {
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    cJSON_AddBoolToObject(r, "ok", ok);
    if (error) cJSON_AddStringToObject(r, "error", error);
    cJSON_AddBoolToObject(r, "restart_required", restart);
    return r;
}

cJSON *rig_setup_command(const char *command, const cJSON *params) {
    if (!strcmp(command, "setup.status")) {
        cJSON *r = reply(true, NULL, false);
        if (!r) return NULL;
        cJSON_AddBoolToObject(r, "sdk_token_configured", s_token[0] != 0);
        cJSON_AddStringToObject(r, "proxy_mode", s_proxy_mode == 1 ? "proxy" : s_proxy_mode == 0 ? "direct" : "invalid");
#ifdef CONFIG_RIG_HTTP_PROXY
        cJSON_AddBoolToObject(r, "proxy_supported", true);
#else
        cJSON_AddBoolToObject(r, "proxy_supported", false);
#endif
        if (s_proxy_mode == 1) {
            cJSON_AddStringToObject(r, "proxy_host", s_proxy_host);
            cJSON_AddNumberToObject(r, "proxy_port", s_proxy_port);
        }
        return r; // Never return the SDK token, including a prefix.
    }
    if (!strcmp(command, "setup.sdk_token")) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(params, "token");
        if (!cJSON_IsString(t) || !token_valid(t->valuestring)) return reply(false, "invalid_sdk_token", false);
        bool ok = config_set_str(TOKEN_KEY, t->valuestring);
        return reply(ok, ok ? NULL : "storage_error", ok);
    }
    if (!strcmp(command, "setup.proxy")) {
#ifdef CONFIG_RIG_HTTP_PROXY
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(params, "enabled");
        bool ok;
        if (cJSON_IsFalse(enabled)) ok = config_set_str(PROXY_KEY, "off");
        else {
            const char *host; int port;
            if (!proxy_valid(params, &host, &port)) return reply(false, "invalid_proxy", false);
            // Reconstruct the record so unrelated fields and secrets are not saved.
            char saved[192]; snprintf(saved, sizeof(saved), "{\"host\":\"%s\",\"port\":%d}", host, port);
            ok = config_set_str(PROXY_KEY, saved);
        }
        return reply(ok, ok ? NULL : "storage_error", ok);
#else
        return reply(false, "proxy_not_in_this_build", false);
#endif
    }
    if (!strcmp(command, "setup.clear")) {
        bool token_ok = config_erase_key(TOKEN_KEY), proxy_ok = config_erase_key(PROXY_KEY);
        bool ok = token_ok && proxy_ok && config_key_lookup(TOKEN_KEY) == CONFIG_KEY_NOT_FOUND &&
                  config_key_lookup(PROXY_KEY) == CONFIG_KEY_NOT_FOUND;
        return reply(ok, ok ? NULL : "storage_error", ok);
    }
    return reply(false, "unknown_setup_command", false);
}
