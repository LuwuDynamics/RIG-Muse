// RIG-Puppy native Muse prototype. Protocol reference: RIG-Omni
// main/boards/puppy/xgo.cc (SetMotorPos, ReadMotorState, xgo_rx).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RIG_SERVO_ID 5
#define RIG_POSITION_MIN 200
#define RIG_POSITION_MAX 2800
#define RIG_GESTURE_AMPLITUDE 80
// Vendor xgo_action.cc normal_state() uses 1000.
#define RIG_SERVO_SPEED 1000
#define RIG_SERVO_SPEED_MAX 6000 // largest field used by native Puppy presets
#define RIG_FEEDBACK_TIMEOUT_MS 300
#define RIG_GESTURE_TIMEOUT_MS 3500

typedef struct {
    uint8_t bytes[12];
    size_t used;
} rig_rx_t;

// Output buffers must hold at least 15 bytes for the single-servo SYNC_WRITE.
#define RIG_MOTOR_COUNT 5
#define RIG_PACKET_MAX 43
#define RIG_WAVE_PHASES 8
bool rig_wave_pose(const int zero[5], unsigned phase, int target[5]);
size_t rig_sync_packet(uint8_t *out, const int positions[5], unsigned mask, unsigned speed);
size_t rig_read_id_packet(uint8_t *out, unsigned id, bool enable_register);
size_t rig_torque_id_packet(uint8_t *out, unsigned id, bool enabled);
int rig_rx_motor_event(rig_rx_t *rx, uint8_t value, int *id, int *position, int *torque, int *enabled);
int rig_pose_ramp(int origin, int target, unsigned elapsed_ms, unsigned duration_ms);
size_t rig_position_packet(uint8_t *out, int position, unsigned speed);
size_t rig_read_packet(uint8_t *out);
size_t rig_enable_read_packet(uint8_t *out);
// 1 = position/torque, 2 = enable register, 0 = incomplete/invalid.
int rig_rx_event(rig_rx_t *rx, uint8_t value, int *position, int *torque, int *enabled);
size_t rig_torque_packet(uint8_t *out, bool enabled);
bool rig_rx_byte(rig_rx_t *rx, uint8_t value, int *position, int *torque);
bool rig_gesture_origin_valid(int position);
int rig_gesture_target(int origin, bool seen_positive, bool seen_negative);

size_t rig_voltage_packet(uint8_t *out);
