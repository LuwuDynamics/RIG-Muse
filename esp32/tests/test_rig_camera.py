"""Exercise the Puppy capture backend and shared camera ownership on the host."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
BOARD=ROOT/'main/boards/rig_puppy'
JSON=ROOT/'managed_components/espressif__cjson/cJSON'
class PuppyCameraTest(unittest.TestCase):
    def test_capture_releases_buffers_and_recovers_from_errors(self):
        headers={
            'esp_err.h': '''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
const char *esp_err_to_name(int);
''',
            'esp_camera.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define GC0308_PID 0x9b
#define LEDC_TIMER_0 0
#define LEDC_CHANNEL_0 0
#define PIXFORMAT_RGB565 2
#define FRAMESIZE_240X240 5
#define CAMERA_FB_IN_PSRAM 1
#define CAMERA_GRAB_LATEST 1
typedef struct {int pin_pwdn,pin_reset,pin_xclk,pin_sccb_sda,pin_sccb_scl,pin_d0,pin_d1,pin_d2,pin_d3,pin_d4,pin_d5,pin_d6,pin_d7,pin_vsync,pin_href,pin_pclk,xclk_freq_hz,ledc_timer,ledc_channel,pixel_format,frame_size,jpeg_quality,fb_count,fb_location,grab_mode,sccb_i2c_port;} camera_config_t;
typedef struct sensor {struct {int PID;} id;int (*set_hmirror)(struct sensor *,int);} sensor_t;
typedef struct {int width,height;} camera_fb_t;
int esp_camera_init(const camera_config_t *);
int esp_camera_deinit(void);
sensor_t *esp_camera_sensor_get(void);
camera_fb_t *esp_camera_fb_get(void);
void esp_camera_fb_return(camera_fb_t *);
''',
            'img_converters.h': '#pragma once\n#include <stdbool.h>\n#include "esp_camera.h"\nbool frame2jpg(camera_fb_t *,uint8_t,uint8_t **,size_t *);\n',
            'esp_heap_caps.h': '#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,unsigned);\n',
            'esp_log.h':'#define ESP_LOGI(tag,...) ((void)(tag))\n',
            'esp_timer.h':'#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
            'freertos/FreeRTOS.h':'#define pdMS_TO_TICKS(x) (x)\n',
            'freertos/task.h':'void vTaskDelay(unsigned);\n',
            'mbedtls/base64.h':'#include <stddef.h>\nint mbedtls_base64_encode(unsigned char *,size_t,size_t *,const unsigned char *,size_t);\n',
        }
        code=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "puppy_camera.c"
static int init_error,gets,returns,live,fail_get,quality_error,allocation_error,deinits;
static camera_fb_t fb={240,240};
static int mirror(sensor_t *s,int m){(void)s;assert(m==0);return 0;}
static sensor_t sensor={.id={GC0308_PID},.set_hmirror=mirror};
const char *esp_err_to_name(int e){(void)e;return "test error";}
int64_t esp_timer_get_time(void){return 1234000;}
void vTaskDelay(unsigned ms){assert(ms==250);}
int esp_camera_init(const camera_config_t *c){assert(c->pin_xclk==15 && c->pin_sccb_sda==4 && c->pin_sccb_scl==5 && c->pin_d0==11 && c->pin_d7==16 && c->fb_count==2 && c->sccb_i2c_port==1);return init_error;}
int esp_camera_deinit(void){++deinits;return 0;}
sensor_t *esp_camera_sensor_get(void){return &sensor;}
camera_fb_t *esp_camera_fb_get(void){++gets;if(fail_get)return NULL;assert(!live);++live;return &fb;}
void esp_camera_fb_return(camera_fb_t *f){assert(f==&fb && live==1);--live;++returns;}
bool frame2jpg(camera_fb_t *f,uint8_t q,uint8_t **out,size_t *n){assert(f==&fb && q==65);*out=malloc(3);memcpy(*out,"jpg",3);*n=3;return !quality_error;}
void *heap_caps_malloc(size_t n,unsigned cap){assert(cap==3);return allocation_error?NULL:malloc(n);}
int mbedtls_base64_encode(unsigned char *out,size_t cap,size_t *len,const unsigned char *in,size_t n){assert(n==3 && !memcmp(in,"jpg",3) && cap==5);memcpy(out,"anBn",4);*len=4;return 0;}
static void result(bool expected){cJSON *r=puppy_camera_capture();assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok"))==expected);if(expected){cJSON *p=cJSON_GetObjectItem(r,"payload");assert(!strcmp(cJSON_GetObjectItem(p,"data_base64")->valuestring,"anBn"));assert(cJSON_GetObjectItem(p,"width")->valueint==240);}cJSON_Delete(r);assert(!live);}
int main(void){
 puppy_camera_register();init_error=ESP_FAIL;result(false);assert(gets==0);
 init_error=0;sensor.id.PID=1;result(false);assert(deinits==1);sensor.id.PID=GC0308_PID;
 result(true);assert(gets==3 && returns==3);
 result(true);assert(gets==6 && returns==6);
 camera_frame_t held;assert(camera_capture(&held)==ESP_OK);int before=gets;result(false);assert(gets==before);camera_release(&held);
 fail_get=1;result(false);fail_get=0;
 quality_error=1;result(false);quality_error=0;
 allocation_error=1;result(false);allocation_error=0;
 result(true);return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            d=Path(d)
            for name,content in headers.items():
                p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
            (d/'test.c').write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(d),'-I',str(BOARD),'-I',str(ROOT/'components/camera'),'-I',str(JSON),str(d/'test.c'),str(ROOT/'components/camera/camera.c'),str(JSON/'cJSON.c'),'-o',str(d/'test')],check=True)
            subprocess.run([str(d/'test')],check=True)
