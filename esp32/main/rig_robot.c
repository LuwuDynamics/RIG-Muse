// Muse transport calls this board-independent facade. Exactly one board is linked.
#include "rig_robot.h"
#include "rig_board.h"
void rig_robot_init(void) { rig_selected_board.init(); }
void rig_robot_set_connected(bool value) { rig_selected_board.set_connected(value); }
void rig_robot_stop(void) { rig_selected_board.stop(); }
cJSON *rig_robot_command(const char *command, const cJSON *params) {
    return rig_selected_board.command(command, params);
}
void rig_robot_register_commands(cJSON *commands) {
    rig_selected_board.register_commands(commands);
}
