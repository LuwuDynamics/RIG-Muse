#pragma once
#include <stdbool.h>
typedef struct cJSON cJSON;
// The selected backend owns its protocol, calibration, actuator count, jobs,
// media capabilities and command schemas. Transport never touches its UART.
typedef struct {
    const char *id;
    const char *name;
    void (*init)(void);
    void (*set_connected)(bool connected);
    void (*stop)(void);
    cJSON *(*command)(const char *command, const cJSON *params);
    void (*register_commands)(cJSON *commands);
} rig_board_ops_t;
extern const rig_board_ops_t rig_selected_board;
