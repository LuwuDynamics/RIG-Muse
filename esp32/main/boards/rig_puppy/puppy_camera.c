// GC0308 pins and RGB565 setup from RIG-Omni boards/common/config.h and
// boards/puppy/puppy_board.cc. One camera holder via components/camera.
#include "puppy_camera.h"
#include "board_config.h"
#include "camera.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include <stdlib.h>

static const char *TAG="rig.camera";
static esp_err_t init(void) {
    camera_config_t c={
        .pin_pwdn=-1,.pin_reset=-1,.pin_xclk=PUPPY_CAMERA_XCLK,
        .pin_sccb_sda=PUPPY_CAMERA_SDA,.pin_sccb_scl=PUPPY_CAMERA_SCL,
        .pin_d0=PUPPY_CAMERA_D0,.pin_d1=PUPPY_CAMERA_D1,.pin_d2=PUPPY_CAMERA_D2,.pin_d3=PUPPY_CAMERA_D3,
        .pin_d4=PUPPY_CAMERA_D4,.pin_d5=PUPPY_CAMERA_D5,.pin_d6=PUPPY_CAMERA_D6,.pin_d7=PUPPY_CAMERA_D7,
        .pin_vsync=PUPPY_CAMERA_VSYNC,.pin_href=PUPPY_CAMERA_HREF,.pin_pclk=PUPPY_CAMERA_PCLK,
        .xclk_freq_hz=20000000,.ledc_timer=LEDC_TIMER_0,.ledc_channel=LEDC_CHANNEL_0,
        .pixel_format=PIXFORMAT_RGB565,.frame_size=FRAMESIZE_240X240,
        .jpeg_quality=12,.fb_count=2,.fb_location=CAMERA_FB_IN_PSRAM,
        .grab_mode=CAMERA_GRAB_LATEST,.sccb_i2c_port=1,
    };
    esp_err_t err=esp_camera_init(&c);
    if(err!=ESP_OK)return err;
    sensor_t *sensor=esp_camera_sensor_get();
    if(!sensor || sensor->id.PID!=GC0308_PID) {
        esp_camera_deinit();return ESP_ERR_NOT_SUPPORTED;
    }
    sensor->set_hmirror(sensor,0);
    vTaskDelay(pdMS_TO_TICKS(250)); // let initial exposure settle
    ESP_LOGI(TAG,"GC0308 ready, 240x240 RGB565 to JPEG");
    return ESP_OK;
}
static esp_err_t capture(camera_frame_t *out) {
    // Flush both buffered frames so a later capture cannot return an old photo.
    for(int i=0;i<2;++i) {
        camera_fb_t *old=esp_camera_fb_get();
        if(!old)return ESP_ERR_TIMEOUT;
        esp_camera_fb_return(old);
    }
    camera_fb_t *fb=esp_camera_fb_get();
    if(!fb)return ESP_ERR_TIMEOUT;
    uint8_t *jpeg=NULL;size_t size=0;
    int w=fb->width,h=fb->height;
    bool ok=frame2jpg(fb,65,&jpeg,&size);
    esp_camera_fb_return(fb);
    if(!ok || !jpeg || !size || size>48*1024) {
        free(jpeg);return ESP_FAIL;
    }
    *out=(camera_frame_t){.jpeg=jpeg,.len=size,.width=w,.height=h,.priv=jpeg};
    return ESP_OK;
}
static void release(camera_frame_t *frame) { free(frame->priv); }
static const camera_driver_t driver={
    .name="RIG-Puppy GC0308",.max_width=240,.max_height=240,
    .init=init,.capture=capture,.release=release,
};
void puppy_camera_register(void) { camera_register(&driver); }
cJSON *puppy_camera_capture(void) {
    camera_frame_t frame={0};
    esp_err_t err=camera_capture(&frame);
    char *b64=NULL;size_t written=0;
    if(err==ESP_OK) {
        size_t cap=(frame.len+2)/3*4+1;
        b64=heap_caps_malloc(cap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!b64)err=ESP_ERR_NO_MEM;
        else if(mbedtls_base64_encode((unsigned char *)b64,cap,&written,frame.jpeg,frame.len))err=ESP_FAIL;
        else b64[written]=0;
    }
    cJSON *result=cJSON_CreateObject();
    cJSON_AddBoolToObject(result,"ok",err==ESP_OK);
    if(err==ESP_OK) {
        cJSON *p=cJSON_AddObjectToObject(result,"payload");
        cJSON_AddStringToObject(p,"format","jpeg-base64");
        cJSON_AddStringToObject(p,"data_base64",b64);
        cJSON_AddNumberToObject(p,"width",frame.width);
        cJSON_AddNumberToObject(p,"height",frame.height);
        cJSON_AddNumberToObject(p,"captured_at_ms",esp_timer_get_time()/1000);
        ESP_LOGI(TAG,"captured %dx%d JPEG, %u bytes",frame.width,frame.height,(unsigned)frame.len);
    } else {
        cJSON *e=cJSON_AddObjectToObject(result,"error");
        cJSON_AddStringToObject(e,"code",err==ESP_ERR_INVALID_STATE?"camera_busy":"camera_capture_failed");
        cJSON_AddStringToObject(e,"message",esp_err_to_name(err));
    }
    free(b64);camera_release(&frame);
    return result;
}
