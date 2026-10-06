#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cJSON cJSON;
void rig_robot_init(void);
void rig_console_init(void);
void rig_robot_register_commands(cJSON *commands);
cJSON *rig_robot_command(const char *command, const cJSON *params);
void rig_robot_set_connected(bool connected);
void rig_robot_stop(void);
#ifdef __cplusplus
}
#endif
