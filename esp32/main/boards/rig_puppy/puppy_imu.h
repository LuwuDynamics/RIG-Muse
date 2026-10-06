#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {IMU_NONE,IMU_HANDLED,IMU_SHAKEN,IMU_TILTED} puppy_imu_event_t;
typedef struct {
    bool ready,tilted;
    float accel[3],gyro[3],roll,pitch;
    int64_t sampled_ms,event_ms;
    unsigned sequence;
    puppy_imu_event_t event;
} puppy_imu_sample_t;
typedef struct {
    puppy_imu_sample_t sample;
    int64_t tilt_since,upright_since,shock_at,handling_at,stable_since,cooldown;
    unsigned shocks;
    float previous[3];
    bool have_previous;
} puppy_imu_model_t;
void puppy_imu_update(puppy_imu_model_t *m,const float accel[3],const float gyro[3],int64_t now);
void puppy_imu_init(void);
void puppy_imu_get(puppy_imu_sample_t *out);
const char *puppy_imu_event_name(puppy_imu_event_t event);
