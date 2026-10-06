"""Run the real command/worker code with a fake UART, flash and clock."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = Path(os.environ.get("CJSON_SOURCE_DIR", ROOT / "managed_components/espressif__cjson/cJSON"))


class RigRobotTest(unittest.TestCase):
    def test_jobs_cancellation_and_hardware_failures(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            headers = {
                "freertos/FreeRTOS.h": """
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffff
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
""",
                "freertos/semphr.h": """
#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(void *s, unsigned t) { (void)s; (void)t; return 1; }
static inline int xSemaphoreGive(void *s) { (void)s; return 1; }
""",
                "freertos/task.h": """
#pragma once
int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(unsigned);
""",
                "driver/uart.h": """
#pragma once
#include <stddef.h>
#define ESP_OK 0
#define UART_NUM_2 2
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
int uart_driver_install(int, int, int, int, void *, int);
int uart_driver_delete(int);
int uart_param_config(int, const uart_config_t *);
int uart_set_pin(int, int, int, int, int);
int uart_write_bytes(int, const void *, size_t);
int uart_wait_tx_done(int, unsigned);
int uart_read_bytes(int, void *, unsigned, unsigned);
""",
                "driver/gpio.h": """
#pragma once
#include <stdint.h>
#define ESP_OK 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
int gpio_config(const gpio_config_t *);
int gpio_set_level(int,int);
""",
                "esp_flash.h": "#pragma once\n#include <stddef.h>\nint esp_flash_read(void *, void *, unsigned, size_t);\n",
                "esp_timer.h": "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
                "esp_log.h": '#pragma once\nvoid fake_log(void);\n#define ESP_LOGI(tag, ...) ((void)(tag),fake_log())\n',
            }
            for name, text in headers.items():
                path = out / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            source = out / "test.c"
            source.write_text(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "puppy_robot.c"
#include "rig_robot.c"

static puppy_imu_sample_t fake_imu;
void puppy_imu_init(void){}
void puppy_imu_get(puppy_imu_sample_t *out){*out=fake_imu;}
static bool voice_busy,voice_recording;
static const char *voice_phase="idle";
static bool cue_was_ready;
const char *puppy_voice_phase(void){return voice_phase;}
void puppy_voice_cue_ready(void){cue_was_ready=true;}
void puppy_voice_init(void){}
bool puppy_voice_busy(void){return voice_busy;}
bool puppy_voice_recording(void){return voice_recording;}
bool puppy_voice_start(unsigned ms,bool send){(void)ms;(void)send;voice_busy=voice_recording=true;voice_phase="queued";return true;}
void puppy_voice_send(void){voice_busy=voice_recording=false;voice_phase="sent";}
void puppy_voice_cancel(void){if(voice_busy)voice_phase="cancelled";voice_busy=voice_recording=false;}
cJSON *puppy_voice_status(void){return cJSON_CreateObject();}
static int laser_level=-1;
#include "driver/gpio.h"
int gpio_config(const gpio_config_t *c){assert(c->pin_bit_mask==(1ULL<<46));return 0;}
int gpio_set_level(int pin,int level){assert(pin==46);laser_level=level;return 0;}
void puppy_camera_register(void){}
cJSON *puppy_camera_capture(void){return cJSON_CreateObject();}
static bool media_ok=true;
static unsigned media_starts, media_stops;
static const rig_show_t *last_media;
void rig_media_init(void) {}
bool rig_media_ready(void) {return media_ok;}
void rig_media_start(const rig_show_t *show) {assert(show);last_media=show;++media_starts;}
void rig_media_stop(void) {++media_stops;}
void rig_media_connected(bool connected) {(void)connected;}
static int64_t fake_ms = 1000;
static bool slow_console;
void fake_log(void) {if(slow_console && s_performing && !s_motion_done)fake_ms+=180;}
static jmp_buf tick_end;
static uint8_t received[128];
static unsigned received_size;
static uint8_t sent[50000][43];
static size_t sent_sizes[50000];
static unsigned sends;
static bool fail_uart;
static bool powered, allow_enable=true, pending_enable, reply_enable=true;
static bool body_power[4]={true,true,true,true};
static unsigned pending_enable_id=5;
static int body_torque[4]={0};
static int body_position[4]={1850,1150,1850,1150};
static void (*task)(void *);

int64_t esp_timer_get_time(void) { return fake_ms * 1000; }
void vTaskDelay(unsigned ticks) { if(ticks==2){fake_ms+=2;return;} longjmp(tick_end, 1); }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle; task = fn; return 1;
}
int esp_flash_read(void *chip, void *out, unsigned address, size_t n) {
    (void)chip; assert(address == 0xFFF000 && n == 20);
    const uint32_t calibration[5] = {2400,600,2400,600,1500};
    memcpy(out, calibration, n); return 0;
}
int uart_driver_install(int p,int rx,int tx,int q,void *qh,int flags) {
    (void)rx;(void)tx;(void)q;(void)qh;(void)flags;assert(p==2);return 0;
}
int uart_driver_delete(int p) {(void)p;return 0;}
int uart_param_config(int p,const uart_config_t *c) {assert(p==2 && c->baud_rate==1000000);return 0;}
int uart_set_pin(int p,int tx,int rx,int rts,int cts) {
    assert(p==2 && tx==3 && rx==38 && rts==-1 && cts==-1);return 0;
}
int uart_write_bytes(int p,const void *data,size_t n) {
    assert(p==2 && n<=43 && sends<50000);
    memcpy(sent[sends],data,n);sent_sizes[sends++]=n;
    const uint8_t *b=data;
    if(b[4]==0x83) {
        assert(b[2]==0xfe && b[6]==6 && n==b[3]+4u);
        for(size_t j=7;j<n-1;j+=7) {
            assert(b[j]>=1 && b[j]<=5);
            int target=b[j+1]|(b[j+2]<<8);assert(target>=200 && target<=2800);
        }
    } else assert(b[2]>=1 && b[2]<=5);
    if(!fail_uart && b[5]==0x30) {
        if(b[4]==3) {
            if(b[2]==5)powered=b[6] && allow_enable;
            else body_power[b[2]-1]=b[6] && allow_enable;
        }
        if(b[4]==2){pending_enable=true;pending_enable_id=b[2];}
    }
    return fail_uart ? -1 : (int)n;
}
int uart_wait_tx_done(int p,unsigned timeout) {(void)p;(void)timeout;return 0;}
int uart_read_bytes(int p,void *data,unsigned n,unsigned timeout) {
    (void)p;(void)timeout;assert(n>=received_size);
    memcpy(data,received,received_size);int size=received_size;received_size=0;
    if(pending_enable && reply_enable) {
        bool value=pending_enable_id==5?powered:body_power[pending_enable_id-1];
        uint8_t response[]={255,255,pending_enable_id,3,0,value,0};response[6]=(uint8_t)~(pending_enable_id+3+value);
        assert(n>=size+sizeof(response));memcpy((uint8_t *)data+size,response,sizeof(response));size+=sizeof(response);
    }
    pending_enable=false;return size;
}
static void feedback(int position,int torque) {
    received_size=0;
    for(int id=1;id<=5;++id) {
        int pos=id==5?position:body_position[id-1], tor=id==5?torque:body_torque[id-1];
        uint8_t frame[]={255,255,id,8,0,0,0,pos&255,pos>>8,tor&255,tor>>8,0};
        unsigned sum=0;for(int i=2;i<11;++i)sum+=frame[i];frame[11]=(uint8_t)~sum;
        memcpy(received+received_size,frame,12);received_size+=12;
        if(reply_enable) {
            bool enabled=id==5?powered:body_power[id-1];
            uint8_t en[]={255,255,id,3,0,enabled,0};en[6]=(uint8_t)~(id+3+enabled);
            memcpy(received+received_size,en,7);received_size+=7;
        }
    }
}
static void tick(unsigned ms,int position,int torque) {
    fake_ms+=ms;
    if(s_active && s_arm_stage==3){pending_enable=true;pending_enable_id=5;} // periodic readback between sparse test ticks
    if(position>=0)feedback(position,torque);
    if(!setjmp(tick_end))task(NULL);
}
static void arm(int position) {
    tick(10,position,0);
    for(int i=0;i<15 && s_active && s_arm_stage<3;++i)tick(20,position,0);
    if(!s_active || s_arm_stage!=3)fprintf(stderr,"arm failure %s %s stage=%d enabled=%d powered=%d\n",s_state,s_reason,s_arm_stage,s_enabled,powered);
    assert(s_active && s_arm_stage==3);
}
static cJSON *call(const char *command,const char *params) {
    cJSON *p=cJSON_Parse(params);cJSON *r=rig_robot_command(command,p);cJSON_Delete(p);return r;
}
static void expect_error(const char *code) {
    cJSON *r=call("rig.gesture","{\"name\":\"greet\"}");
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));
    assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(r,"error"),"code")->valuestring,code));
    cJSON_Delete(r);
}
static void start(void) {
    cJSON *r=call("rig.gesture","{\"name\":\"greet\"}");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    assert(!strcmp(s_state,"accepted"));
}
int main(void) {
    rig_robot_init();assert(s_ready && s_calibrated && sends==0);
    expect_error("unavailable");
    rig_robot_set_connected(true);expect_error("feedback_unavailable");
    tick(10,1500,0);s_calibrated=false;expect_error("calibration_required");s_calibrated=true;
    cJSON *r=call("rig.gesture","{\"name\":\"run\"}");
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    assert(puppy_laser_ready() && !puppy_laser_on() && laser_level==0);
    r=call("rig.laser","{\"mode\":1,\"duration_ms\":100}");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    assert(laser_level==1);tick(110,1500,0);assert(laser_level==0);
    r=call("rig.laser","{\"mode\":2}");cJSON_Delete(r);assert(laser_level==0);
    tick(11,1500,0);assert(laser_level==1);
    rig_robot_stop();assert(laser_level==0);tick(10,1500,0);
    r=call("rig.laser","{\"mode\":2}");cJSON_Delete(r);rig_robot_stop();
    tick(20,1500,0);assert(laser_level==0); // cancelled pulse must not relight
    r=call("rig.laser","{\"mode\":1}");cJSON_Delete(r);
    rig_robot_set_connected(false);assert(laser_level==0);
    r=call("rig.laser","{\"mode\":1}");assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    r=call("rig.laser","{\"mode\":0}");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    rig_robot_set_connected(true);tick(10,1500,0);
    r=call("rig.laser","{\"mode\":1.5}");assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    r=call("rig.laser","{\"mode\":1,\"duration_ms\":30001}");assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    start();expect_error("busy");arm(1500);assert(s_active);
    unsigned refresh=sends;tick(50,1500,0);
    assert(sends>refresh && sent[refresh][4]==0x83 && sent[refresh][5]==0x35);
    unsigned before=sends;rig_robot_stop();tick(10,1510,0);
    assert(!s_active && !s_stop && !strcmp(s_state,"cancelled"));
    assert(sent_sizes[before]==15 && sent[before][8]==(1510&255));
    start();arm(1510);
    rig_robot_set_connected(false);rig_robot_set_connected(true);tick(10,1510,0);
    assert(!s_active && !strcmp(s_state,"cancelled")); // disconnect stays latched
    start();arm(1510);before=sends;tick(310,-1,0);
    assert(!s_active && !strcmp(s_reason,"feedback_timeout"));
    assert(sent_sizes[before]==8 && sent[before][5]==0x30 && sent[before][6]==0);
    expect_error("feedback_unavailable");
    tick(10,1500,0);start();arm(1500);tick(50,1500,101);
    assert(s_active && s_torque==101);
    tick(10,1500,-101);assert(s_active && s_torque==-101);
    before=sends;rig_robot_stop();tick(10,1500,-101);
    assert(!s_active && !s_stop && !strcmp(s_state,"cancelled"));
    assert(sent_sizes[before]==15 && sent[before][4]==0x83); // hold, no torque-triggered unload
    start();arm(1500);rig_robot_stop();tick(10,1500,0);assert(!s_stop);
    tick(10,279,0);expect_error("position_near_limit");
    tick(10,1500,0);start();arm(1500);
    tick(500,1580,0);tick(200,1580,0);tick(500,1420,0);tick(200,1420,0);
    tick(400,1500,0);tick(160,1500,0);
    assert(!s_active && !strcmp(s_state,"completed"));
    start();arm(1500);tick(3500,1600,0);
    assert(!s_active && !strcmp(s_reason,"motion_timeout"));
    tick(10,1500,0);start();arm(1500);
    tick(1800,1500,0);tick(160,1500,0);assert(s_active);
    tick(1600,1500,0);assert(!strcmp(s_reason,"motion_timeout")); // no false completion if jammed
    tick(10,1500,0);start();fail_uart=true;tick(10,1500,0);fail_uart=false;
    assert(!s_active && !strcmp(s_state,"failed"));
    cJSON *commands=cJSON_CreateObject();rig_robot_register_commands(commands);
    assert(cJSON_GetArraySize(commands)==12);
    assert(cJSON_GetObjectItem(commands,"camera.capture") && cJSON_GetObjectItem(commands,"rig.laser"));
    assert(cJSON_GetObjectItem(commands,"rig.gesture"));cJSON_Delete(commands);
    assert(!call("not.a.rig.command","{}"));
    r=call("rig.perform","{\"name\":\"sleep\"}");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    // Sleep requires no motor feedback and must never enable or move a servo.
    s_calibrated=false;s_have_feedback=false;
    unsigned show_sends=sends;
    tick(10,-1,0);assert(s_performing && !s_active && media_starts==1);
    tick(6000,-1,0);assert(!s_performing && !strcmp(s_state,"completed"));
    for(unsigned i=show_sends;i<sends;++i)assert(sent[i][4]==2);
    r=call("rig.perform","{\"name\":\"curious\"}");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    tick(10,-1,0);rig_robot_stop();unsigned stops=media_stops;tick(10,-1,0);
    assert(!s_performing && media_stops>stops && !strcmp(s_state,"cancelled"));
    r=call("rig.perform","{\"name\":\"sleep\"}");cJSON_Delete(r);
    rig_robot_set_connected(false);rig_robot_set_connected(true);tick(10,-1,0);
    assert(!s_performing && !strcmp(s_state,"cancelled"));
    r=call("rig.perform","{\"name\":\"sleep\"}");cJSON_Delete(r);
    tick(10,-1,0);media_ok=false;tick(10,-1,0);
    assert(!s_performing && !strcmp(s_reason,"media_unavailable"));media_ok=true;
    s_calibrated=true;tick(10,1500,0);s_standing=true;
    r=call("rig.catalog","{}");
    cJSON *catalog=cJSON_GetObjectItem(r,"payload");
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(catalog,"shows"))==24);
    cJSON *moves=cJSON_GetObjectItem(catalog,"movements");assert(cJSON_GetArraySize(moves)==4);
    const char *directions[]={"forward","backward","left","right"};
    for(int i=0;i<4;++i) {
        cJSON *move=cJSON_GetArrayItem(moves,i);assert(!strcmp(cJSON_GetObjectItem(move,"command")->valuestring,"rig.move"));
        cJSON *args=cJSON_GetObjectItem(move,"example_params");assert(!strcmp(cJSON_GetObjectItem(args,"direction")->valuestring,directions[i]));
    }
    cJSON *specs=cJSON_CreateObject();puppy_register(specs);
    cJSON *tool_names=cJSON_GetObjectItem(catalog,"tools");assert(cJSON_GetArraySize(tool_names)==cJSON_GetArraySize(specs));
    for(int i=0;i<cJSON_GetArraySize(tool_names);++i)assert(cJSON_GetObjectItem(specs,cJSON_GetArrayItem(tool_names,i)->valuestring));
    assert(!strstr(cJSON_GetObjectItem(cJSON_GetObjectItem(specs,"rig.catalog"),"description")->valuestring,"No TTS or microphone"));
    cJSON_Delete(specs);cJSON_Delete(r);
    s_standing=false;
    // Delayed arrivals past the old 700/1400 ms boundaries must not be skipped.
    tick(10,1500,0);start();arm(1500);
    tick(900,1580,0);assert(s_seen_positive && !s_seen_negative);
    tick(10,1580,0);assert(s_last_target==1420);
    tick(1200,1420,0);assert(s_seen_negative);
    tick(10,1420,0);assert(s_last_target==1500);
    tick(700,1500,0);tick(160,1500,0);assert(!strcmp(s_state,"completed"));
    // A write success cannot stand in for confirmed torque enable.
    tick(10,1500,0);allow_enable=false;powered=false;start();
    unsigned preload=sends;tick(10,1500,0);tick(20,1500,0);tick(20,1500,0);tick(20,1500,0);
    assert(s_active && s_arm_stage==2 && s_last_target==-1);
    tick(500,1500,0);assert(!s_active && !strcmp(s_reason,"torque_enable_unconfirmed"));
    for(unsigned i=preload;i<sends;++i)if(sent[i][4]==0x83)
        assert((sent[i][8] | (sent[i][9]<<8))==1500);
    allow_enable=true;tick(10,1500,0);start();arm(1500);
    powered=false;tick(50,1500,0);assert(!s_active && !strcmp(s_reason,"torque_enable_lost"));
    tick(10,1500,0);start();tick(10,1500,0);assert(s_arm_stage==1);
    unsigned cancel_before_enable=sends;rig_robot_stop();tick(10,1500,0);tick(50,1500,0);
    assert(!s_active && !strcmp(s_state,"cancelled"));
    for(unsigned i=cancel_before_enable;i<sends;++i)
        assert(!(sent[i][4]==3 && sent[i][5]==0x30 && sent[i][6]==1));
    tick(10,1500,0);start();arm(1500);unsigned coarse=sends;
    for(int i=0;i<8;++i)tick(100,1500,0);
    unsigned states=0,enables=0;
    for(unsigned i=coarse;i<sends;++i)if(sent[i][4]==2){
        states+=sent[i][5]==0x48;enables+=sent[i][5]==0x30;
    }
    assert(states>=3 && enables>=3);rig_robot_stop();tick(10,1500,0);
    // Full-body preparation is explicit and uses existing calibration.
    for(int i=0;i<4;++i)body_torque[i]=i%2?106:-106;
    tick(10,1500,106);s_standing=false;
    r=call("rig.prepare","{}");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    for(int i=0;i<180 && (s_prepare_requested || s_preparing);++i)tick(20,1500,0);
    if(!s_standing)fprintf(stderr,"prepare failure %s %s\n",s_state,s_reason);
    assert(s_standing && !strcmp(s_state,"completed") && !strcmp(s_reason,"standing_pose_observed"));
    for(int i=0;i<4;++i)body_torque[i]=0;
    // Real enable replies come from round-robin polling, not broadcasts.
    // Idle between shows must not age any joint past the 300 ms guard.
    for(int i=0;i<100;++i) {
        tick(10,-1,0);
        if(i>30)assert(body_enabled(fake_ms));
    }
    tick(10,1500,0);

    // High torque remains visible but does not reject or abort native motion.
    for(int i=0;i<4;++i)body_torque[i]=i%2?106:-106;
    tick(10,1500,-106);
    // Native wave advances continuously and finishes near the original 5.25 s.
    r=call("rig.perform","{\"name\":\"greet\"}");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    tick(10,1500,0);assert(s_wave);
    int64_t wave_began=fake_ms;
    for(int step=0;step<700 && s_wave;++step) {
        for(int i=0;i<4;++i)body_position[i]=s_wave_target[i];
        tick(10,s_wave_target[4],106);
    }
    assert(!s_wave && s_motion_done && s_standing);
    assert(fake_ms-wave_began>=5250 && fake_ms-wave_began<5800);
    tick(10,1500,0);assert(!s_performing && !strcmp(s_state,"completed"));
    for(int i=0;i<4;++i)body_torque[i]=0;
    // Frozen joints must fail even when final target equals the initial stance.
    const int restore_stand[4]={1850,1150,1850,1150};memcpy(body_position,restore_stand,sizeof(restore_stand));
    r=call("rig.perform","{\"name\":\"greet\"}");cJSON_Delete(r);tick(10,1500,0);
    for(int i=0;i<200 && s_wave;++i)tick(50,1500,0);
    assert(!s_wave && !strcmp(s_reason,"action_motion_timeout") && s_native.done);
    assert(s_have_fault && s_fault_phase==s_plan.count && s_fault_motors[4].position==1500);
    r=call("rig.status","{}");assert(cJSON_GetObjectItem(cJSON_GetObjectItem(r,"payload"),"fault"));cJSON_Delete(r);
    // A stalled scheduler fails instead of bursting through obsolete commands.
    s_standing=true;tick(10,1500,0);
    r=call("rig.perform","{\"name\":\"greet\"}");cJSON_Delete(r);tick(10,1500,0);
    assert(s_wave);tick(150,1500,0);
    assert(!s_wave && !strcmp(s_reason,"action_scheduler_lag"));
    // Stop and disconnect cancel an in-progress full-body wave.
    for(int disconnect=0;disconnect<2;++disconnect) {
        s_standing=true;tick(10,1500,0);
        r=call("rig.perform","{\"name\":\"greet\"}");cJSON_Delete(r);tick(10,1500,0);assert(s_wave);
        if(disconnect)rig_robot_set_connected(false);else rig_robot_stop();
        tick(10,1500,0);assert(!s_wave && !s_performing && !strcmp(s_state,"cancelled"));
        rig_robot_set_connected(true);
        unsigned stopped=sends;tick(50,1500,0);
        for(unsigned i=stopped;i<sends;++i)assert(sent[i][4]==2);
    }

    // All sixteen native jobs and reset traverse the real worker to completion.
    for(size_t action=0;action<rig_show_count;++action) {
        if(!rig_shows[action].motion)continue;
        const int stand[4]={1850,1150,1850,1150};
        memcpy(body_position,stand,sizeof(stand));powered=true;
        for(int i=0;i<4;++i)body_power[i]=true;
        s_standing=true;tick(10,1500,0);
        char params[96];snprintf(params,sizeof(params),"{\"name\":\"%s\"}",rig_shows[action].name);
        r=call("rig.perform",params);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
        int64_t began=fake_ms;
        for(int step=0;step<4000 && s_performing;++step) {
            int pos=1500;
            if(s_wave) {
                for(int i=0;i<4;++i)body_position[i]=s_wave_target[i];
                pos=s_wave_target[4];
            }
            tick(50,pos,0);
        }
        if(strcmp(s_state,"completed"))fprintf(stderr,"action failed %s: %s\n",rig_shows[action].name,s_reason);
        assert(!s_performing && !strcmp(s_state,"completed"));
        assert(s_native.done && s_native.tick==s_plan.count);
        assert(fake_ms-began<=s_plan.duration_ms+PUPPY_ACTION_SETTLE_MS+200);
        assert(s_standing==s_plan.returns_standing);
    }
    // Model ESP_LOG stdout contention with a long 115200-baud console reply.
    slow_console=true;
    // Timed gait follows the real worker and verifies return to standing.
    const char *dirs[]={"forward","backward","left","right"};
    for(int d=0;d<4;++d) {
        memcpy(body_position,restore_stand,sizeof(restore_stand));powered=true;
        for(int i=0;i<4;++i)body_power[i]=true;
        s_standing=true;tick(10,1500,0);
        char params[128];snprintf(params,sizeof(params),"{\"direction\":\"%s\",\"duration_ms\":500,\"speed\":40}",dirs[d]);
        r=call("rig.move",params);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
        int64_t began=fake_ms;
        for(int step=0;step<160 && s_performing;++step) {
            if(s_wave)for(int i=0;i<4;++i)body_position[i]=s_wave_target[i];
            tick(10,s_wave?s_wave_target[4]:1500,0);
        }
        assert(!s_performing && !strcmp(s_state,"completed") && s_standing && fake_ms-began<1000);
    }
    r=call("rig.move","{\"direction\":\"left\",\"duration_ms\":0}");assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    r=call("rig.voice","{\"operation\":\"toggle\"}");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")) && voice_recording);cJSON_Delete(r);
    assert(cue_was_ready && last_media==&voice_ready_show);
    voice_phase="recording";tick(10,1500,0);assert(last_media==&recording_show);
    voice_phase="sending";voice_recording=false;tick(10,1500,0);assert(last_media==&voice_send_show);
    voice_phase="recording";voice_recording=true;
    r=call("rig.move","{\"direction\":\"forward\"}");assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    r=call("rig.voice","{\"operation\":\"toggle\"}");assert(!voice_busy);cJSON_Delete(r);
    tick(10,1500,0);assert(last_media==&voice_sent_show);
    fake_imu.ready=true;fake_imu.sequence++;fake_imu.event=IMU_SHAKEN;fake_imu.event_ms=fake_ms;
    tick(10,1500,0);assert(last_media==&voice_sent_show);fake_imu.ready=false;
    voice_phase="failed";tick(10,1500,0);assert(last_media==&voice_failed_show);
    voice_phase="cancelled";tick(10,1500,0);assert(last_media==&voice_cancelled_show);
    s_connected=false;r=call("rig.voice","{\"operation\":\"toggle\"}");assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    assert(last_media==&voice_failed_show);s_connected=true;
    r=call("rig.move","{\"direction\":\"left\"}");cJSON_Delete(r);tick(10,1500,0);assert(s_wave);
    r=call("rig.voice","{\"operation\":\"toggle\"}");cJSON_Delete(r);tick(10,1500,0);
    assert(!s_wave && !voice_busy && !strcmp(s_state,"cancelled"));
    // Voltage candidates are not enable replies and require a pending poll.
    s_battery_pending=true;s_battery_wait=fake_ms+100;
    uint8_t voltage[]={255,255,1,3,0,78,173}; // checksum ~(1+3+78)
    memcpy(received,voltage,sizeof(voltage));received_size=sizeof(voltage);
    tick(10,-1,0);assert(s_battery_mv==7800 && s_battery_at);
    // Cancellation during preparation cannot be followed by a later enable.
    s_standing=false;r=call("rig.prepare","{}");cJSON_Delete(r);tick(10,1500,0);
    assert(s_preparing);unsigned prep_cancel=sends;rig_robot_stop();tick(10,1500,0);tick(50,1500,0);
    assert(!s_preparing && !s_prepare_requested && !strcmp(s_state,"cancelled"));
    for(unsigned i=prep_cancel;i<sends;++i)assert(!(sent[i][4]==3 && sent[i][5]==0x30 && sent[i][6]==1));
    // Feedback loss aborts preparation instead of continuing the pose ramp.
    r=call("rig.prepare","{}");cJSON_Delete(r);tick(10,1500,0);tick(310,-1,0);
    assert(!s_preparing && !strcmp(s_reason,"body_feedback_timeout"));
    puts("RIG command/worker tests passed");
}
''')
            binary = out / "test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-deprecated-declarations",  # upstream cJSON uses sprintf on macOS
                "-fsanitize=address,undefined", "-I", str(out), "-I", str(ROOT / "main"), "-I", str(ROOT / "main/boards/rig_puppy"),
                "-I", str(JSON), str(source), str(ROOT / "main/boards/rig_puppy/rig_motion.c"), str(ROOT / "main/boards/rig_puppy/puppy_laser.c"), str(ROOT / "main/rig_show.c"), str(ROOT / "main/boards/rig_puppy/puppy_actions.c"), str(ROOT / "main/boards/rig_puppy/puppy_native.c"), str(ROOT / "main/boards/rig_puppy/puppy_gait.c"), str(ROOT / "main/boards/rig_puppy/puppy_imu_model.c"), "-lm",
                str(JSON / "cJSON.c"), "-o", str(binary)
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
