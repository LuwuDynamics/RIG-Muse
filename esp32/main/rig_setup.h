#pragma once
#include <stddef.h>
typedef struct cJSON cJSON;
// Boot-time snapshot; settings written over USB take effect after a restart.
void rig_setup_init(void);
const char *rig_setup_sdk_token(void);
// 0: direct, 1: proxy, -1: invalid saved settings (fail closed).
int rig_setup_proxy(char *host, size_t capacity, int *port);
// Called only by the local USB console. Never register these as Muse tools.
cJSON *rig_setup_command(const char *command, const cJSON *params);
