// Pins, baud, state registers and calibration address verified against
// RIG-Omni/main/boards/puppy/{board_config.h,puppy_board.cc,xgo.cc,xgo.h}.
// One UART owner. Calibrated standing preparation, then bounded waist gestures. No gait task.
#include "rig_board.h"
#include "puppy_actions.h"
#include "board_config.h"
#include "rig_motion.h"
#include "rig_media.h"
#include "puppy_camera.h"
#include "puppy_laser.h"
#include "puppy_imu.h"
#include "puppy_voice.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "link.rig.puppy";
static SemaphoreHandle_t s_lock;
static bool s_ready, s_calibrated, s_connected, s_active, s_requested, s_stop;
static bool s_have_feedback;
static bool s_seen_positive, s_seen_negative;
static int s_position, s_torque, s_origin, s_last_target = -1;
static int64_t s_feedback_ms, s_started_ms, s_settled_ms, s_last_write_ms;
static unsigned s_job;
static int s_enabled=-1, s_arm_stage;
static int64_t s_enable_feedback_ms, s_enable_write_ms, s_enable_poll_ms;
static const rig_show_t *s_show;
static bool s_performing, s_media_started, s_motion_done;
static int64_t s_show_started;
typedef struct { bool seen; int position,torque,enabled; int64_t feedback,enable_feedback; } motor_state_t;
static motor_state_t s_motors[5];
static int s_zero[5],s_stand[5],s_prepare_origin[5];
static int64_t s_prepare_started,s_ramp_started,s_prepare_settled,s_prepare_written,s_arm_attempt[5];
static unsigned s_ramp_duration;
static bool s_preparing,s_prepare_requested,s_prepare_only,s_standing;
static bool s_wave;
static puppy_action_plan_t s_plan;
static puppy_native_t s_native;
static unsigned s_wave_phase,s_native_speed;
static int s_wave_target[5],s_seen_min[5],s_seen_max[5],s_goal_min[5],s_goal_max[5];
static int64_t s_wave_started,s_native_done_ms,s_wave_settled;
static bool s_have_fault;
static motor_state_t s_fault_motors[5];
static int s_fault_targets[5];
static unsigned s_fault_phase;
static int64_t s_fault_ms;
static int s_battery_mv;
static int64_t s_battery_at,s_battery_poll,s_battery_wait;
static bool s_battery_pending,s_reactions=true;
static const char *s_voice_phase="idle";
static unsigned s_imu_sequence;
static int64_t s_reaction_until;
static const rig_show_t move_show={"move","Timed Puppy gait",FACE_HAPPY,SOUND_NONE,100,true};
static const rig_show_t handled_show={"handled","Handling reaction",FACE_SHY,SOUND_BOOP,2000,false};
static const rig_show_t shaken_show={"shaken","Shake reaction",FACE_SURPRISE,SOUND_QUESTION,2000,false};
static const rig_show_t tilted_show={"tilted","Tilt reaction",FACE_SURPRISE,SOUND_CHIRP,2000,false};
static const rig_show_t recording_show={"recording","Listening",FACE_CURIOUS,SOUND_NONE,15000,false};
static const rig_show_t voice_ready_show={"voice_ready","Ready to listen",FACE_CURIOUS,SOUND_RECORD,250,false};
static const rig_show_t voice_send_show={"voice_sending","Sending",FACE_SHY,SOUND_SEND,90000,false};
static const rig_show_t voice_sent_show={"voice_sent","Muse received voice",FACE_HAPPY,SOUND_SPARKLE,1500,false};
static const rig_show_t voice_failed_show={"voice_failed","Voice unavailable or failed",FACE_SURPRISE,SOUND_QUESTION,1500,false};
static const rig_show_t voice_cancelled_show={"voice_cancelled","Recording cancelled",FACE_IDLE,SOUND_BOOP,800,false};
static const char *s_state = "idle";
static const char *s_reason = "none";

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static bool fresh(int64_t now) {
    return s_have_feedback && now - s_feedback_ms <= RIG_FEEDBACK_TIMEOUT_MS;
}

static bool body_fresh(int64_t now) {
    for(int i=0;i<5;++i)if(!s_motors[i].seen || now-s_motors[i].feedback>300)return false;
    return true;
}
// Torque is telemetry only; it does not reject, abort, or unload a motion.
static bool body_enabled(int64_t now) {
    for(int i=0;i<5;++i)if(s_motors[i].enabled!=1 || now-s_motors[i].enable_feedback>300)return false;
    return true;
}
static bool stand_valid(void) {
    for(int i=0;i<5;++i)if(s_stand[i]<200 || s_stand[i]>2800)return false;
    return true;
}
static bool send_packet(const uint8_t *data, size_t size) {
    bool ok=size && uart_write_bytes(UART_NUM_2,data,size)==(int)size &&
        uart_wait_tx_done(UART_NUM_2,pdMS_TO_TICKS(20))==ESP_OK;
    // Allow a half-duplex servo reply to finish before another transaction.
    if(ok)vTaskDelay(pdMS_TO_TICKS(2));
    return ok;
}

// Worker owns all UART writes. Cancellation cannot race a later motion write.
static void finish(const char *state, const char *reason, int64_t now) {
    uint8_t frame[RIG_PACKET_MAX];
    if(!strcmp(state,"failed")) {
        s_have_fault=true;s_fault_ms=now;s_fault_phase=s_wave?s_wave_phase+1:0;
        memcpy(s_fault_motors,s_motors,sizeof(s_fault_motors));
        for(int i=0;i<5;++i) {
            s_fault_targets[i]=s_wave?s_wave_target[i]:s_preparing?s_stand[i]:s_motors[i].position;
            ESP_LOGI(TAG,"failure joint %d pos=%d goal=%d torque=%d enabled=%d age=%d ms",
                     i+1,s_motors[i].position,s_fault_targets[i],s_motors[i].torque,
                     s_motors[i].enabled,(int)(now-s_motors[i].feedback));
        }
    }
    if(s_preparing || s_wave || (s_active && s_standing && s_performing)) {
        int positions[5]={0};unsigned mask=0;bool ok=true;
        for(int i=0;i<5;++i) {
            motor_state_t *m=&s_motors[i];
            if(m->seen && now-m->feedback<=300 &&
               m->enabled==1 && now-m->enable_feedback<=300) {
                positions[i]=m->position;mask|=1u<<i;
            } else if(!send_packet(frame,rig_torque_id_packet(frame,i+1,false)))ok=false;
        }
        if(mask && !send_packet(frame,rig_sync_packet(frame,positions,mask,RIG_SERVO_SPEED)))ok=false;
        if(!ok){state="failed";reason="stop_uart_error";}
    } else if (s_active) {
        // Hold measured position on cancellation; on stale feedback,
        // The legacy single-joint diagnostic releases only servo 5.
        size_t n = s_arm_stage == 3 && s_enabled == 1 && now-s_enable_feedback_ms<=300 &&
                   fresh(now)
            ? rig_position_packet(frame, s_position, RIG_SERVO_SPEED)
            : rig_torque_packet(frame, false);
        if (!send_packet(frame, n)) {
            state = "failed";
            reason = "stop_uart_error";
        }
    }
    s_active = s_requested = s_stop = false;
    s_wave=false;
    s_preparing=s_prepare_requested=false;
    if(strcmp(state,"completed"))s_standing=false;
    if (s_performing && !strcmp(state, "completed")) {
        s_motion_done = true;
        return; // The screen/audio timeline is still running.
    }
    s_performing = false;
    if (strcmp(state, "completed")) rig_media_stop();
    ESP_LOGI(TAG, "job %u %s: %s", s_job, state, reason);
    s_state = state;
    s_reason = reason;
}

static void wave_start(int64_t now) {
    puppy_native_init(&s_native);s_native_done_ms=s_wave_settled=0;s_last_write_ms=0;
    for(int i=0;i<5;++i) {
        s_seen_min[i]=s_seen_max[i]=s_goal_min[i]=s_goal_max[i]=s_motors[i].position;
        s_wave_target[i]=s_motors[i].position;
    }
    // Console replies share the stdout lock with ESP_LOGI and can take >100 ms
    // at 115200 baud. Never log while starting or advancing motor timelines.
    (void)now;
}
static void wave_tick(int64_t now) {
    if(!body_fresh(now) || !body_enabled(now)) {
        finish("failed",!body_fresh(now)?"body_feedback_timeout":
               "body_enable_lost",now);return;
    }
    puppy_imu_sample_t imu;puppy_imu_get(&imu);
    if(s_plan.id==254 && imu.ready && imu.tilted){finish("failed","body_tilted",now);return;}
    unsigned elapsed=(unsigned)(now-s_wave_started);
    if(elapsed>s_plan.duration_ms+PUPPY_ACTION_SETTLE_MS) {
        finish("failed","action_motion_timeout",now);return;
    }
    // Advance original formulas by elapsed time, independent of servo arrival.
    // Evaluate skipped logical ticks, but submit only the latest target; no
    // catch-up burst and no accumulated per-frame ramp/dwell delay.
    if(!s_native.done && elapsed>puppy_action_due_ms(&s_plan,s_native.tick)+100) {
        finish("failed","action_scheduler_lag",now);return;
    }
    while(!s_native.done && puppy_action_due_ms(&s_plan,s_native.tick)<=elapsed) {
        if(!puppy_action_sample(&s_plan,s_zero,&s_native,s_wave_target,&s_native_speed)) {
            finish("failed","action_position_limit",now);return;
        }
        for(int i=0;i<5;++i) {
            if(s_wave_target[i]<s_goal_min[i])s_goal_min[i]=s_wave_target[i];
            if(s_wave_target[i]>s_goal_max[i])s_goal_max[i]=s_wave_target[i];
        }
        s_wave_phase=s_native.tick-1;
        if(s_native.done)s_native_done_ms=now;
    }
    for(int i=0;i<5;++i) {
        if(s_motors[i].position<s_seen_min[i])s_seen_min[i]=s_motors[i].position;
        if(s_motors[i].position>s_seen_max[i])s_seen_max[i]=s_motors[i].position;
    }
    if(now-s_last_write_ms>=5) {
        uint8_t frame[RIG_PACKET_MAX];
        if(!send_packet(frame,rig_sync_packet(frame,s_wave_target,31,s_native_speed))) {
            finish("failed","uart_error",now);return;
        }
        s_last_write_ms=now;
    }
    if(!s_native.done)return;
    bool arrived=true,moved=true;
    for(int i=0;i<5;++i) {
        arrived &= abs(s_motors[i].position-s_wave_target[i])<=60;
        int excursion=s_goal_max[i]-s_goal_min[i];
        if(excursion>120)moved &= (s_seen_max[i]-s_seen_min[i])*10>=excursion*3;
    }
    if(!arrived || !moved){s_wave_settled=0;return;}
    if(!s_wave_settled)s_wave_settled=now;
    bool observed=now-s_wave_settled>=150;
    for(int i=0;i<5;++i)observed &= s_motors[i].feedback>=s_wave_settled+100 && s_motors[i].feedback>=s_native_done_ms;
    if(observed) {
        s_standing=s_plan.returns_standing;
        finish("completed","native_timeline_and_final_pose_observed",now);
    }
}

// Preparation uses the vendor idle standing pose (zero +/- 550), with an
// explicit ramp and feedback confirmation. It never writes calibration.
static void prepare_tick(int64_t now) {
    uint8_t frame[RIG_PACKET_MAX];
    if(s_prepare_requested) {
        if(!body_fresh(now)) {
            finish("failed","body_feedback_timeout",now);return;
        }
        s_prepare_requested=false;s_preparing=true;s_state="running";
        s_prepare_started=now;s_ramp_started=s_prepare_settled=0;s_standing=false;
        int distance=0;
        for(int i=0;i<5;++i) {
            s_prepare_origin[i]=s_motors[i].position;s_arm_attempt[i]=0;
            int d=abs(s_stand[i]-s_prepare_origin[i]);if(d>distance)distance=d;
        }
        s_ramp_duration=(unsigned)distance*1000/350+300;
        if(!send_packet(frame,rig_sync_packet(frame,s_prepare_origin,31,RIG_SERVO_SPEED))) {
            finish("failed","uart_error",now);return;
        }
        s_prepare_written=now;
        ESP_LOGI(TAG,"job %u preparing calibrated standing pose",s_job);
        return;
    }
    if(!s_preparing)return;
    if(!body_fresh(now)) {
        finish("failed","body_feedback_timeout",now);return;
    }
    if(now-s_prepare_started>=10000){finish("failed","standing_timeout",now);return;}
    if(!s_ramp_started) {
        bool enabled=true;
        for(int i=0;i<5;++i) {
            motor_state_t *m=&s_motors[i];
            if(m->enabled!=1 || now-m->enable_feedback>300) {
                enabled=false;
                if(now-s_prepare_started>=20 && now-s_arm_attempt[i]>=100) {
                    if(!send_packet(frame,rig_torque_id_packet(frame,i+1,true))) {
                        finish("failed","uart_error",now);return;
                    }
                    s_arm_attempt[i]=now;break; // One enable write per iteration.
                }
            }
        }
        if(!enabled) {
            if(now-s_prepare_started>=1000)finish("failed","body_enable_unconfirmed",now);
            return;
        }
        s_ramp_started=now;
    }
    for(int i=0;i<5;++i)if(s_motors[i].enabled!=1 || now-s_motors[i].enable_feedback>300) {
        finish("failed","body_enable_lost",now);return;
    }
    unsigned elapsed=(unsigned)(now-s_ramp_started);
    if(now-s_prepare_written>=10) {
        int target[5];for(int i=0;i<5;++i)
            target[i]=rig_pose_ramp(s_prepare_origin[i],s_stand[i],elapsed,s_ramp_duration);
        if(!send_packet(frame,rig_sync_packet(frame,target,31,RIG_SERVO_SPEED))) {
            finish("failed","uart_error",now);return;
        }
        s_prepare_written=now;
    }
    bool settled=elapsed>=s_ramp_duration;
    for(int i=0;i<5;++i)settled &= abs(s_motors[i].position-s_stand[i])<=60;
    if(!settled){s_prepare_settled=0;return;}
    if(!s_prepare_settled)s_prepare_settled=now;
    bool observed=now-s_prepare_settled>=200;
    for(int i=0;i<5;++i)observed &= s_motors[i].feedback>=s_prepare_settled+100;
    if(observed) {
        s_preparing=false;s_standing=true;
        ESP_LOGI(TAG,"job %u standing pose observed on all five joints",s_job);
        if(s_prepare_only)finish("completed","standing_pose_observed",now);
        else s_requested=true;
    }
}

static void worker(void *unused) {
    (void)unused;
    rig_rx_t parser = {0};
    static int64_t last_poll;
    static unsigned position_id=1, enable_id=1;
    for (;;) {
        uint8_t incoming[128];
        int count = uart_read_bytes(UART_NUM_2, incoming, sizeof(incoming), 0);
        int64_t now = now_ms();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < count; ++i) {
            int id,position,torque,enabled;
            int event=rig_rx_motor_event(&parser,incoming[i],&id,&position,&torque,&enabled);
            if(event==3) {
                if(id==1 && s_battery_pending && now<=s_battery_wait){s_battery_mv=position;s_battery_at=now;s_battery_pending=false;}
                continue;
            }
            if(event) {
                motor_state_t *m=&s_motors[id-1];
                if(event==2){m->enabled=enabled;m->enable_feedback=now;}
                else {m->position=position;m->torque=torque;m->feedback=now;m->seen=true;}
                if(id==5) {
                    if(event==2){s_enabled=enabled;s_enable_feedback_ms=now;}
                    else {s_position=position;s_torque=torque;s_feedback_ms=now;s_have_feedback=true;}
                }
            }
        }
        puppy_laser_tick(now);
        puppy_imu_sample_t imu;puppy_imu_get(&imu);
        bool idle=!(s_active || s_requested || s_performing || s_preparing || s_prepare_requested);
        bool voice=puppy_voice_busy();
        const char *phase=puppy_voice_phase();
        if(strcmp(phase,s_voice_phase)) {
            s_voice_phase=phase;
            const rig_show_t *feedback=NULL;
            if(!strcmp(phase,"recording"))feedback=&recording_show;
            else if(!strcmp(phase,"sending"))feedback=&voice_send_show;
            else if(!strcmp(phase,"sent") || !strcmp(phase,"tested"))feedback=&voice_sent_show;
            else if(!strcmp(phase,"failed"))feedback=&voice_failed_show;
            else if(!strcmp(phase,"cancelled"))feedback=&voice_cancelled_show;
            if(idle && feedback){rig_media_start(feedback);s_reaction_until=voice?0:now+feedback->duration_ms;}
        }
        if(imu.sequence!=s_imu_sequence) {
            s_imu_sequence=imu.sequence;
            if(imu.ready && now-imu.event_ms<500 && idle && !voice && s_reactions && !s_stop && !s_reaction_until) {
                rig_media_start(imu.event==IMU_HANDLED?&handled_show:imu.event==IMU_SHAKEN?&shaken_show:&tilted_show);
                s_reaction_until=now+2000;
                ESP_LOGI(TAG,"IMU reaction: %s",puppy_imu_event_name(imu.event));
            }
        }
        if(s_reaction_until && now>=s_reaction_until){s_reaction_until=0;if(idle && !voice)rig_media_stop();}
        if ((s_active || s_requested || s_performing || s_preparing || s_prepare_requested) && (s_stop || !s_connected)) {
            finish("cancelled", s_connected ? "stop_requested" : "disconnected", now);
        } else if (s_performing && !rig_media_ready()) {
            finish("failed", "media_unavailable", now);
        }
        if(s_preparing || s_prepare_requested)prepare_tick(now);
        if (s_performing && !s_media_started && !s_preparing && !s_prepare_requested) {
            rig_media_start(s_show);
            s_show_started = now;
            s_media_started = true;
            s_state = "running";
        }
        if (s_performing && s_motion_done && now - s_show_started >= s_show->duration_ms) {
            s_performing = false;
            // Sleep keeps closed eyes until the next show or stop; its snore is finite.
            if (s_show->face != FACE_SLEEP) rig_media_stop();
            finish("completed", s_show->motion ? "show_and_action_completed" : "show_timeline_completed", now);
        }
        if(s_requested && s_performing && s_show->motion) {
            s_requested=false;s_active=s_wave=true;s_wave_phase=0;
            s_wave_started=now;s_arm_stage=3;
            wave_start(now);
        }
        if(s_wave)wave_tick(now);
        if (!s_wave && (s_active || s_requested)) {
            if(s_standing && s_performing && (!body_fresh(now))) {
                finish("failed","body_feedback_timeout",now);
            } else if(s_standing && s_performing && !body_enabled(now)) {
                finish("failed","body_enable_lost",now);
            } else if (!fresh(now)) {
                finish("failed", "feedback_timeout", now);
            } else if (s_requested) {
                s_origin = s_position;
                if (!rig_gesture_origin_valid(s_origin)) {
                    finish("failed", "position_near_limit", now);
                } else {
                    uint8_t frame[RIG_PACKET_MAX];
                    s_active = true;
                    s_requested = false;
                    s_started_ms = now;
                    s_settled_ms = 0;
                    s_seen_positive = s_seen_negative = false;
                    s_last_target = -1;
                    s_state = "running";
                    s_arm_stage=1;s_enabled=-1;
                    // Preload a hold at the measured position before enabling torque.
                    size_t n = rig_position_packet(frame, s_origin, RIG_SERVO_SPEED);
                    if (!send_packet(frame, n)) finish("failed", "uart_error", now);
                }
            }
            if (s_active && s_arm_stage < 3) {
                uint8_t frame[RIG_PACKET_MAX];
                if(now-s_started_ms>=500) finish("failed","torque_enable_unconfirmed",now);
                else if(s_arm_stage==2 && s_enabled==1 && s_enable_feedback_ms>s_enable_write_ms) {
                    s_arm_stage=3;
                    ESP_LOGI(TAG,"job %u servo 5 torque enabled and read back",s_job);
                } else if((s_arm_stage==1 && now-s_started_ms>=20) ||
                          (s_arm_stage==2 && now-s_enable_write_ms>=100)) {
                    if(!send_packet(frame,rig_torque_packet(frame,true)))finish("failed","uart_error",now);
                    else {s_arm_stage=2;s_enable_write_ms=now;}
                }
            }
            if(s_active && s_arm_stage==3 &&
               (s_enabled!=1 || now-s_enable_feedback_ms>300))
                finish("failed","torque_enable_lost",now);
            if (s_active && s_arm_stage==3) {
                uint32_t elapsed = (uint32_t)(now - s_started_ms);
                // Advance only after observing the commanded pose, rather than
                // skipping a slow servo at fixed 700 ms wall-clock boundaries.
                int target = rig_gesture_target(s_origin, s_seen_positive, s_seen_negative);
                if (s_feedback_ms > s_started_ms && abs(s_position - target) <= 20) {
                    if (!s_seen_positive) s_seen_positive = true;
                    else if (!s_seen_negative) s_seen_negative = true;
                }
                // Vendor xgo_control continuously refreshes motor setpoints.
                // Keep the same bounded target alive; only this worker can write.
                if (target != s_last_target || now - s_last_write_ms >= 10) {
                    uint8_t frame[RIG_PACKET_MAX];
                    size_t n;
                    if(s_standing && s_performing) {
                        int pose[5];memcpy(pose,s_stand,sizeof(pose));pose[4]=target;
                        n=rig_sync_packet(frame,pose,31,RIG_SERVO_SPEED);
                    } else n=rig_position_packet(frame,target,RIG_SERVO_SPEED);
                    if (!send_packet(frame, n)) finish("failed", "uart_error", now);
                    else {s_last_target = target;s_last_write_ms = now;}
                }
                if (s_active && s_seen_positive && s_seen_negative &&
                    elapsed >= 300 && abs(s_position - s_origin) <= 20) {
                    if (!s_settled_ms) s_settled_ms = now;
                    // Wait for multiple fresh observations at the return pose.
                    if (now - s_settled_ms >= 150 && s_feedback_ms >= s_settled_ms + 100)
                        finish("completed", "gesture_positions_observed", now);
                } else s_settled_ms = 0;
                if (s_active && elapsed >= RIG_GESTURE_TIMEOUT_MS)
                    finish("failed", "motion_timeout", now);
            }
        }
        if (!s_active && !s_requested && !s_performing && !s_preparing && !s_prepare_requested && s_stop) {
            rig_media_stop();s_stop = false;s_standing=false;
        }
        if(s_battery_pending && now>s_battery_wait)s_battery_pending=false;
        bool bus_ready=(!s_active || s_arm_stage!=2 || now-s_enable_write_ms>=20) && !s_battery_pending;
        // Five round-robin reads must stay within the 300 ms freshness window
        // even between shows; a 100 ms idle interval makes older joints stale.
        bool enable_due=now-s_enable_poll_ms >= 20;
        bool position_due=now-last_poll>=10;
        // Serve the oldest overdue read. Fixed enable priority can starve
        // position feedback when the media scheduler lengthens an iteration.
        if(bus_ready && idle && now-s_battery_poll>=5000) {
            uint8_t frame[RIG_PACKET_MAX];
            if(send_packet(frame,rig_voltage_packet(frame))){s_battery_pending=true;s_battery_wait=now+30;}
            s_battery_poll=now;
        } else if(bus_ready && position_due && (!enable_due || last_poll<=s_enable_poll_ms)) {
            uint8_t frame[RIG_PACKET_MAX];send_packet(frame,rig_read_id_packet(frame,position_id,false));last_poll=now;position_id=position_id%5+1;
        } else if(bus_ready && enable_due) {
            uint8_t frame[RIG_PACKET_MAX];send_packet(frame,rig_read_id_packet(frame,enable_id,true));s_enable_poll_ms=now;enable_id=enable_id%5+1;
        }
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void puppy_init(void) {
    puppy_laser_init();
    puppy_camera_register();
    puppy_imu_init();
    puppy_voice_init();
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return;
    uint32_t zero[5]={0};
    s_calibrated = esp_flash_read(NULL, zero, PUPPY_CALIBRATION_ADDRESS, sizeof(zero)) == ESP_OK;
    for (size_t i = 0; i < 5 && s_calibrated; ++i)
        s_calibrated = zero[i] >= 200 && zero[i] <= 2800;
    const int offsets[5]={-550,550,-550,550,0};
    for(int i=0;i<5;++i){s_zero[i]=(int)zero[i];s_stand[i]=s_zero[i]+offsets[i];}
    uart_config_t config = {
        .baud_rate = PUPPY_UART_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(UART_NUM_2, 1024, 1024, 0, NULL, 0) != ESP_OK) return;
    if (uart_param_config(UART_NUM_2, &config) != ESP_OK ||
        uart_set_pin(UART_NUM_2, PUPPY_UART_TX, PUPPY_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        xTaskCreate(worker, "rig_robot", 4096, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(UART_NUM_2);
        return;
    }
    s_ready = true;
    rig_media_init();
    ESP_LOGI(TAG, "Puppy UART ready; calibration %s; no motion at boot",
             s_calibrated ? "valid" : "missing (motion disabled)");
}

void puppy_connected(bool connected) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_connected = connected;
    rig_media_connected(connected);
    // Latch a disconnect even if a reconnect arrives before the worker runs.
    if (!connected) {s_stop = true;puppy_laser_stop();puppy_voice_cancel();}
    xSemaphoreGive(s_lock);
}

void puppy_stop(void) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stop = true;
    puppy_laser_stop();puppy_voice_cancel();
    xSemaphoreGive(s_lock);
}

static cJSON *error_result(const char *code) {
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", false);
    cJSON *error = cJSON_AddObjectToObject(out, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", code);
    return out;
}

void puppy_register(cJSON *commands);

cJSON *puppy_command(const char *command, const cJSON *params) {
    if(!strcmp(command,"camera.capture") || !strcmp(command,"rig.camera"))return puppy_camera_capture();
    if (strcmp(command,"rig.move") && strcmp(command,"rig.battery") && strcmp(command,"rig.imu") && strcmp(command,"rig.voice") && strcmp(command,"rig.laser") && strcmp(command, "rig.status") && strcmp(command, "rig.gesture") &&
        strcmp(command, "rig.stop") && strcmp(command, "rig.perform") &&
        strcmp(command, "rig.catalog") && strcmp(command,"rig.prepare")) return NULL;
    if (!s_lock) return error_result("unavailable");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const char *error = NULL;
    if (!strcmp(command, "rig.catalog")) {
        cJSON *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"ok",true);
        cJSON *payload=cJSON_AddObjectToObject(out,"payload");
        cJSON_AddStringToObject(payload,"description","Full Puppy capability directory. The 24 shows are preset performances only, not the complete tool list. Use rig.move for forward/backward walking and left/right turns; camera.capture for photos; rig.battery, rig.imu and rig.voice for battery, interaction and voice notes. No TTS.");
        cJSON_AddNumberToObject(payload,"preset_count",rig_show_count);
        cJSON *registered=cJSON_CreateObject();puppy_register(registered);
        cJSON *tools=cJSON_AddArrayToObject(payload,"tools");
        cJSON *spec=NULL;
        cJSON_ArrayForEach(spec,registered)cJSON_AddItemToArray(tools,cJSON_CreateString(spec->string));
        cJSON_Delete(registered);
        cJSON *moves=cJSON_AddArrayToObject(payload,"movements");
        const char *directions[]={"forward","backward","left","right"};
        const char *descriptions[]={"Walk forward","Walk backward","Turn left (not strafe)","Turn right (not strafe)"};
        for(unsigned i=0;i<4;++i) {
            cJSON *move=cJSON_CreateObject();cJSON_AddItemToArray(moves,move);
            cJSON_AddStringToObject(move,"command","rig.move");
            cJSON_AddStringToObject(move,"description",descriptions[i]);
            cJSON *args=cJSON_AddObjectToObject(move,"example_params");
            cJSON_AddStringToObject(args,"direction",directions[i]);
            cJSON_AddNumberToObject(args,"speed",30);cJSON_AddNumberToObject(args,"duration_ms",500);
        }
        cJSON *list=cJSON_AddArrayToObject(payload,"shows");
        for(size_t i=0;i<rig_show_count;++i) {
            cJSON *item=cJSON_CreateObject();cJSON_AddItemToArray(list,item);
            cJSON_AddStringToObject(item,"name",rig_shows[i].name);
            cJSON_AddStringToObject(item,"description",rig_shows[i].description);
            cJSON_AddNumberToObject(item,"duration_ms",rig_shows[i].duration_ms);
            cJSON_AddNumberToObject(item,"nominal_motion_ms",puppy_action_nominal_ms(puppy_action_id(rig_shows[i].name)));
            cJSON_AddNumberToObject(item,"preparation_max_ms",rig_shows[i].motion?10000:0);
            cJSON_AddNumberToObject(item,"motion_max_ms",rig_shows[i].motion?puppy_action_nominal_ms(puppy_action_id(rig_shows[i].name))+PUPPY_ACTION_SETTLE_MS:0);
        }
        xSemaphoreGive(s_lock);return out;
    }
    if(!strcmp(command,"rig.voice")) {
        const cJSON *op=cJSON_GetObjectItemCaseSensitive(params,"operation");
        const cJSON *ms=cJSON_GetObjectItemCaseSensitive(params,"duration_ms");
        const char *name=cJSON_IsString(op)?op->valuestring:"status";
        bool moving=s_active || s_requested || s_performing || s_preparing || s_prepare_requested;
        if(ms && (!cJSON_IsNumber(ms) || ms->valuedouble!=ms->valueint || ms->valueint<300 || ms->valueint>15000))error="invalid_params";
        else if(!strcmp(name,"cancel"))puppy_voice_cancel();
        else if(!strcmp(name,"send"))puppy_voice_send();
        else if(!strcmp(name,"toggle") && moving){s_stop=true;puppy_laser_stop();puppy_voice_cancel();}
        else if(!strcmp(name,"toggle") && puppy_voice_recording())puppy_voice_send();
        else if(!strcmp(name,"toggle") && puppy_voice_busy())puppy_voice_cancel();
        else if(!strcmp(name,"start") || !strcmp(name,"test") || !strcmp(name,"toggle")) {
            if(moving || s_stop)error="busy";
            else if(!s_connected)error="unavailable";
            else if(!puppy_voice_start(ms?ms->valueint:15000,strcmp(name,"test")!=0))error="voice_unavailable";
            else {s_reaction_until=0;rig_media_start(&voice_ready_show);puppy_voice_cue_ready();}
        } else if(strcmp(name,"status"))error="invalid_params";
        if(error && !moving && !puppy_voice_busy()) {
            rig_media_start(&voice_failed_show);s_reaction_until=now_ms()+voice_failed_show.duration_ms;
        }
    } else if(!strcmp(command,"rig.imu")) {
        const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(params,"reactions");
        if(enabled && !cJSON_IsBool(enabled))error="invalid_params";
        else if(enabled){s_reactions=cJSON_IsTrue(enabled);if(!s_reactions && s_reaction_until){s_reaction_until=0;rig_media_stop();}}
    } else if(!strcmp(command,"rig.laser")) {
        const cJSON *mode=cJSON_GetObjectItemCaseSensitive(params,"mode");
        const cJSON *duration=cJSON_GetObjectItemCaseSensitive(params,"duration_ms");
        if(!cJSON_IsNumber(mode) || mode->valuedouble!=mode->valueint || mode->valueint<0 || mode->valueint>2 ||
           (duration && (!cJSON_IsNumber(duration) || duration->valuedouble!=duration->valueint || duration->valueint<100 || duration->valueint>30000)))error="invalid_params";
        else if(mode->valueint && (!s_connected || s_stop))error="unavailable";
        else if(!puppy_laser_set(mode->valueint,duration?duration->valueint:5000,now_ms()))error="laser_unavailable";
    } else if(!strcmp(command,"rig.prepare")) {
        if(!s_ready || !s_connected)error="unavailable";
        else if(!s_calibrated || !stand_valid())error="calibration_required";
        else if(s_active || s_requested || s_performing || s_preparing || s_prepare_requested || puppy_voice_busy())error="busy";
        else if(!body_fresh(now_ms()))error="body_feedback_unavailable";
        else {
            if(++s_job==0)++s_job;
            s_prepare_only=true;s_prepare_requested=true;s_stop=false;s_have_fault=false;
            s_show=NULL;s_state="accepted";s_reason="none";
        }
    } else if (!strcmp(command, "rig.move") || !strcmp(command, "rig.perform") || !strcmp(command, "rig.gesture")) {
        bool move=!strcmp(command,"rig.move");
        bool perform = move || !strcmp(command, "rig.perform");
        int forward=0,turn=0;unsigned move_ms=1000;bool move_valid=true;
        if(move) {
            const cJSON *d=cJSON_GetObjectItemCaseSensitive(params,"direction");
            const cJSON *v=cJSON_GetObjectItemCaseSensitive(params,"speed");
            const cJSON *t=cJSON_GetObjectItemCaseSensitive(params,"duration_ms");
            int speed=v?v->valueint:40;
            move_valid=cJSON_IsString(d) && (!v || (cJSON_IsNumber(v) && v->valuedouble==v->valueint && speed>=10 && speed<=100)) &&
                (!t || (cJSON_IsNumber(t) && t->valuedouble==t->valueint && t->valueint>=100 && t->valueint<=5000 && t->valueint%5==0));
            if(move_valid) {
                if(!strcmp(d->valuestring,"forward"))forward=speed;
                else if(!strcmp(d->valuestring,"backward"))forward=-speed;
                else if(!strcmp(d->valuestring,"left"))turn=speed;
                else if(!strcmp(d->valuestring,"right"))turn=-speed;
                else move_valid=false;
                if(t)move_ms=t->valueint;
            }
        }
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
        const rig_show_t *show = move?&move_show:cJSON_IsString(name) ? rig_show_find(name->valuestring) : NULL;
        bool motion = !perform || (show && show->motion);
        if(move && !move_valid)error="invalid_params";
        else if (!show || (!perform && strcmp(show->name,"greet"))) error = "invalid_gesture";
        else if (!s_ready || !s_connected) error = "unavailable";
        else if (perform && !rig_media_ready()) error = "media_unavailable";
        else if (motion && !s_calibrated) error = "calibration_required";
        else if (s_requested || s_active || s_performing || s_preparing || s_prepare_requested || puppy_voice_busy()) error = "busy";
        else if(perform && motion && (!stand_valid() || !body_fresh(now_ms())))error="body_feedback_unavailable";

        else if (motion && !fresh(now_ms())) error = "feedback_unavailable";
        else if (!perform && !rig_gesture_origin_valid(s_position)) error = "position_near_limit";
        else if(perform && motion && !(move?puppy_move_plan(forward,turn,move_ms,s_zero,&s_plan):puppy_action_plan(show->name,s_zero,&s_plan)))error="action_position_limit";
        else {
            if (++s_job == 0) ++s_job;
            s_prepare_requested=perform && motion && (!s_standing || !body_enabled(now_ms()));
            // Rising must start from the actual seated pose, not pre-stand first.
            if(perform && s_plan.id==16 && body_enabled(now_ms()))s_prepare_requested=false;
            s_prepare_only=false;
            s_requested = motion && !s_prepare_requested;
            s_performing = perform;s_have_fault=false;
            s_media_started = false;
            s_motion_done = !motion;
            s_show = show;
            s_seen_positive=s_seen_negative=false;
            s_stop = false;
            s_state = "accepted";
            s_reason = "none";
        }
    } else if (strcmp(command, "rig.stop") == 0) {s_stop = true;puppy_laser_stop();puppy_voice_cancel();}
    cJSON *out = error ? error_result(error) : cJSON_CreateObject();
    if (!error) {
        cJSON_AddBoolToObject(out, "ok", true);
        cJSON *payload = cJSON_AddObjectToObject(out, "payload");
        cJSON_AddStringToObject(payload, "board", "RIG-Puppy");
        cJSON_AddStringToObject(payload,"board_id","rig-puppy");
        cJSON_AddBoolToObject(payload, "ready", s_ready);
        cJSON_AddBoolToObject(payload, "calibrated", s_calibrated);
        cJSON_AddBoolToObject(payload, "connected", s_connected);
        cJSON_AddBoolToObject(payload, "media_ready", rig_media_ready());
        cJSON *laser=cJSON_AddObjectToObject(payload,"laser");
        cJSON_AddBoolToObject(laser,"ready",puppy_laser_ready());
        cJSON_AddBoolToObject(laser,"on",puppy_laser_on());
        cJSON_AddNumberToObject(laser,"remaining_ms",puppy_laser_remaining(now_ms()));
        cJSON *battery=cJSON_AddObjectToObject(payload,"battery");
        bool bf=s_battery_at && now_ms()-s_battery_at<15000;
        cJSON_AddBoolToObject(battery,"fresh",bf);
        if(bf){cJSON_AddNumberToObject(battery,"voltage",s_battery_mv/1000.0);int pct=(s_battery_mv-6600)*100/1800;
            cJSON_AddNumberToObject(battery,"percent_estimate",pct<0?0:pct>100?100:pct);
            cJSON_AddBoolToObject(battery,"low",s_battery_mv<7000);}
        else {cJSON_AddNullToObject(battery,"voltage");cJSON_AddNullToObject(battery,"percent_estimate");}
        cJSON_AddBoolToObject(battery,"percentage_is_estimated",true);
        cJSON_AddNullToObject(battery,"charging");
        puppy_imu_sample_t im;puppy_imu_get(&im);cJSON *ij=cJSON_AddObjectToObject(payload,"imu");
        cJSON_AddBoolToObject(ij,"ready",im.ready);cJSON_AddBoolToObject(ij,"reactions",s_reactions);
        if(im.ready){cJSON_AddNumberToObject(ij,"roll",im.roll);cJSON_AddNumberToObject(ij,"pitch",im.pitch);
            cJSON_AddNumberToObject(ij,"ax_g",im.accel[0]);cJSON_AddNumberToObject(ij,"ay_g",im.accel[1]);cJSON_AddNumberToObject(ij,"az_g",im.accel[2]);
            cJSON_AddBoolToObject(ij,"tilted",im.tilted);}
        cJSON_AddStringToObject(ij,"last_event",puppy_imu_event_name(im.event));cJSON_AddNumberToObject(ij,"event_sequence",im.sequence);
        cJSON_AddItemToObject(payload,"voice",puppy_voice_status());
        cJSON_AddBoolToObject(payload, "performing", s_performing);
        cJSON_AddBoolToObject(payload,"preparing",s_preparing||s_prepare_requested);
        cJSON_AddBoolToObject(payload,"standing",s_standing);
        if(s_wave) {
            cJSON_AddNumberToObject(payload,"action_phase",s_wave_phase+1);
            cJSON_AddNumberToObject(payload,"action_phases",s_plan.count);
            cJSON_AddNumberToObject(payload,"action_elapsed_ms",now_ms()-s_wave_started);
            cJSON_AddNumberToObject(payload,"nominal_duration_ms",s_plan.duration_ms);
            cJSON_AddBoolToObject(payload,"finishing",s_native.done);
            if(s_plan.id==1)cJSON_AddNumberToObject(payload,"wave_phase",s_wave_phase+1);
        }
        if(s_show && s_show->motion) {
            cJSON_AddBoolToObject(payload,"limit_scaled",s_plan.scaled);
            cJSON_AddNumberToObject(payload,"action_id",s_plan.id);
        }
        cJSON *motors=cJSON_AddArrayToObject(payload,"motors");
        for(int i=0;i<5;++i) {
            cJSON *m=cJSON_CreateObject();cJSON_AddItemToArray(motors,m);
            cJSON_AddNumberToObject(m,"id",i+1);
            cJSON_AddBoolToObject(m,"fresh",s_motors[i].seen && now_ms()-s_motors[i].feedback<=300);
            if(s_motors[i].seen){cJSON_AddNumberToObject(m,"position",s_motors[i].position);cJSON_AddNumberToObject(m,"torque",s_motors[i].torque);}
            if(s_motors[i].enable_feedback)cJSON_AddBoolToObject(m,"enabled",s_motors[i].enabled==1);
        }
        if(s_have_fault) {
            cJSON *fault=cJSON_AddObjectToObject(payload,"fault");
            cJSON_AddNumberToObject(fault,"phase",s_fault_phase);
            cJSON *joints=cJSON_AddArrayToObject(fault,"joints");
            for(int i=0;i<5;++i) {
                cJSON *j=cJSON_CreateObject();cJSON_AddItemToArray(joints,j);
                cJSON_AddNumberToObject(j,"id",i+1);
                cJSON_AddNumberToObject(j,"position",s_fault_motors[i].position);
                cJSON_AddNumberToObject(j,"target",s_fault_targets[i]);
                cJSON_AddNumberToObject(j,"torque",s_fault_motors[i].torque);
                cJSON_AddNumberToObject(j,"feedback_age_ms",s_fault_ms-s_fault_motors[i].feedback);
                cJSON_AddBoolToObject(j,"enabled",s_fault_motors[i].enabled==1);
            }
        }
        if(s_show)cJSON_AddStringToObject(payload,"show",s_show->name);
        cJSON_AddBoolToObject(payload, "feedback_fresh", fresh(now_ms()));
        if(s_have_feedback)cJSON_AddNumberToObject(payload,"feedback_age_ms",now_ms()-s_feedback_ms);
        cJSON_AddNumberToObject(payload, "job_id", s_job);
        cJSON_AddStringToObject(payload, "state", s_state);
        cJSON_AddStringToObject(payload, "reason", s_reason);
        cJSON_AddBoolToObject(payload, "stop_pending", s_stop);
        if (s_have_feedback) {
            cJSON_AddNumberToObject(payload, "servo5_position", s_position);
            cJSON_AddNumberToObject(payload, "servo5_torque", s_torque);
        }
        if(s_enabled>=0)cJSON_AddBoolToObject(payload,"servo5_enabled",s_enabled==1);
        cJSON_AddBoolToObject(payload,"enable_feedback_fresh",s_enabled>=0 && now_ms()-s_enable_feedback_ms<=300);
        cJSON_AddBoolToObject(payload,"arming",s_active && s_arm_stage<3);
        cJSON_AddNumberToObject(payload, "servo5_target", s_last_target);
        cJSON_AddBoolToObject(payload, "observed_positive", s_seen_positive);
        cJSON_AddBoolToObject(payload, "observed_negative", s_seen_negative);
    }
    xSemaphoreGive(s_lock);
    return out;
}

static cJSON *add_command(cJSON *commands, const char *name, const char *description) {
    cJSON *spec = cJSON_AddObjectToObject(commands, name);
    cJSON_AddStringToObject(spec, "description", description);
    cJSON_AddObjectToObject(spec, "optional");
    return cJSON_AddObjectToObject(spec, "required");
}

void puppy_register(cJSON *commands) {
    cJSON *ma=add_command(commands,"rig.move","Finite Puppy gait: forward/backward or left/right turns, then verify standing. duration_ms default 1000, 100..5000 in multiples of 5; speed default 40, 10..100. Returns accepted; poll rig.status. Completion verifies joint motion and stance, not distance or heading. Stop cancels immediately. No automatic retry.");
    cJSON *dir=cJSON_AddObjectToObject(ma,"direction");cJSON_AddStringToObject(dir,"type","string");cJSON_AddStringToObject(dir,"description","forward, backward, left (turn), right (turn)");
    cJSON *opt=cJSON_GetObjectItem(cJSON_GetObjectItem(commands,"rig.move"),"optional");
    cJSON *v=cJSON_AddObjectToObject(opt,"speed");cJSON_AddStringToObject(v,"type","integer");cJSON_AddStringToObject(v,"description","10..100, default 40");
    v=cJSON_AddObjectToObject(opt,"duration_ms");cJSON_AddStringToObject(v,"type","integer");cJSON_AddStringToObject(v,"description","100..5000, multiple of 5, default 1000");
    add_command(commands,"rig.battery","Read servo-bus battery voltage; percentage is a rough 2-cell voltage estimate, not measured capacity. Null when stale; charging state unknown.");
    add_command(commands,"rig.imu","Read QMI8658 acceleration and tilt, latest handled/shaken/tilted event. Handling is a motion heuristic, not proof of being held. Reactions are eyes/sound only while idle.");
    opt=cJSON_GetObjectItem(cJSON_GetObjectItem(commands,"rig.imu"),"optional");v=cJSON_AddObjectToObject(opt,"reactions");cJSON_AddStringToObject(v,"type","boolean");cJSON_AddStringToObject(v,"description","Enable or disable local eyes/sound reactions.");
    add_command(commands,"rig.voice","Record a voice note for Muse. operation=status/start/send/cancel; test records locally without sending. Default maximum 15 seconds. Sent means Muse acknowledged the note, not that its requested task succeeded. Static double-click starts/ends recording; during motion double-click stops motion. No TTS.");
    opt=cJSON_GetObjectItem(cJSON_GetObjectItem(commands,"rig.voice"),"optional");v=cJSON_AddObjectToObject(opt,"operation");cJSON_AddStringToObject(v,"type","string");cJSON_AddStringToObject(v,"description","status (default), start, send, cancel, test");
    v=cJSON_AddObjectToObject(opt,"duration_ms");cJSON_AddStringToObject(v,"type","integer");cJSON_AddStringToObject(v,"description","300..15000, default 15000; automatically sends when duration is reached");
    add_command(commands,"camera.capture","Take a fresh 240x240 JPEG from the RIG-Puppy GC0308 camera. Returns payload.format=jpeg-base64 and payload.data_base64 for image understanding. Capture only when requested; no background image upload.");
    cJSON_AddNumberToObject(cJSON_GetObjectItem(commands,"camera.capture"),"timeout_ms",15000);
    cJSON *laser_args=add_command(commands,"rig.laser","Control the light sword: mode 0=off, 1=on, 2=cycle lighting mode. Automatically turns off after duration_ms (default 5000, 100..30000). Stop or disconnect also turns it off. Status reports commanded output, not optical confirmation.");
    cJSON *mode=cJSON_AddObjectToObject(laser_args,"mode");
    cJSON_AddStringToObject(mode,"type","integer");
    cJSON_AddStringToObject(mode,"description","0 off, 1 on, 2 cycle mode.");
    cJSON *optional=cJSON_GetObjectItem(cJSON_GetObjectItem(commands,"rig.laser"),"optional");
    cJSON *duration=cJSON_AddObjectToObject(optional,"duration_ms");
    cJSON_AddStringToObject(duration,"type","integer");
    cJSON_AddStringToObject(duration,"description","100..30000 ms; defaults to 5000 ms.");
    add_command(commands, "rig.status", "Read RIG-Puppy readiness and latest show or gesture job state. "
        "accepted/running are not completion. For jobs with motion, completed requires servo feedback to observe "
        "joint movement and the final position after the native timeline (keep_sit remains seated); it is not visual confirmation.");
    cJSON *required = add_command(commands, "rig.gesture", "Run the bounded greet gesture "
        "on RIG-Puppy servo 5 (small left/right movement, then return). No leg motion. "
        "Returns accepted and job_id immediately; poll rig.status for completion. "
        "Requires calibration and fresh servo feedback. Never retry automatically after a timeout.");
    cJSON *name = cJSON_AddObjectToObject(required, "name");
    cJSON_AddStringToObject(name, "type", "string");
    cJSON_AddStringToObject(name, "description", "Only greet is supported.");
    add_command(commands,"rig.prepare","Use the stored calibration to slowly bring all five Puppy joints to the vendor standing pose. "
        "No recalibration or zero writes. Returns accepted; poll rig.status for standing_pose_observed. "
        "Keep Puppy on a clear level surface. Maximum preparation time is 10 seconds; never retry failures automatically.");
    add_command(commands, "rig.catalog", "List ALL Puppy capabilities: registered tool names, four rig.move directions with call examples, and 24 preset shows with durations. The 24 shows are NOT the full capability list. Walking/turning uses rig.move, photos use camera.capture; battery, IMU and microphone voice notes use rig.battery, rig.imu and rig.voice. No TTS.");
    required = add_command(commands, "rig.perform", "Perform animated eyes, a short sound effect and optional motion. Motion shows first prepare all five joints into the calibrated standing pose if needed (up to 10 seconds). "
        "Use rig.catalog for preset shows; use rig.move for forward/backward walking or left/right turns. greet=wave, wake=stretch, happy=swing. keep_sit stays seated; sit_reset rises from sitting. Actions follow the original continuous timing and speed fields; final pose is verified within 3 seconds after the timeline. Query rig.catalog for timing. sleep/curious/shy/surprised are media only. "
        "Only one performance at a time; returns accepted with job_id. Poll rig.status until completed/failed/cancelled before another show. "
        "Never report accepted as completed. Completion records the local timeline and, when moving, servo feedback; not visual or acoustic confirmation. "
        "Sleep keeps closed eyes until the next show or rig.stop. No automatic retry on failure.");
    name = cJSON_AddObjectToObject(required, "name");
    cJSON_AddStringToObject(name, "type", "string");
    cJSON_AddStringToObject(name, "description", "Use a name from rig.catalog: wave, naughty, lookup, swing, rolling, angry, swimming, pee, stretch, bouncing, shaking, sit, scratch, hug, keep_sit, sit_reset, reset; or greet, wake, happy, sleep, curious, shy, surprised.");
    add_command(commands, "rig.stop", "Stop the current show, sound and gesture; return eyes to idle. Cancellation holds current healthy joints; it does not automatically stand up. "
        "Poll rig.status until stop_pending is false. Preparation and performances share one motor controller.");
}

const rig_board_ops_t rig_selected_board = {
    .id="rig-puppy", .name="RIG-Puppy", .init=puppy_init,
    .set_connected=puppy_connected, .stop=puppy_stop,
    .command=puppy_command, .register_commands=puppy_register,
};
