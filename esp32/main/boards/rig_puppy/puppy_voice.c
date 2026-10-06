// INMP441-style 32-bit LEFT input on I2S1, pins and >>16 conversion from
// RIG-Omni NoAudioCodecSimplex. I2S0 remains exclusively owned by sound effects.
#include "puppy_voice.h"
#include "board_config.h"
#include "voice_muse_chat.h"
#include "muse_chat.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
enum {IDLE,QUEUED,RECORDING,SENDING,SENT,FAILED,CANCELLED,TESTED};
static const char *names[]={"idle","queued","recording","sending","sent","failed","cancelled","tested"};
static atomic_int state;
static atomic_bool cancelled,end_recording,ready;
static atomic_bool cue_ready;
static unsigned duration;
static bool transmit;
static SemaphoreHandle_t lock;
static unsigned samples,peak;
static double rms;
static char detail[96],transcript[512];
static i2s_chan_handle_t rx;
static const char *TAG="rig.voice";
static bool mic_init(void) {
    i2s_chan_config_t c=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,I2S_ROLE_MASTER);
    c.dma_desc_num=4;c.dma_frame_num=320;
    if(i2s_new_channel(&c,NULL,&rx)!=ESP_OK)return false;
    i2s_std_config_t a={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_MONO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=PUPPY_MIC_BCLK,.ws=PUPPY_MIC_WS,.dout=I2S_GPIO_UNUSED,.din=PUPPY_MIC_DIN}};
    a.slot_cfg.slot_mask=I2S_STD_SLOT_LEFT;
    if(i2s_channel_init_std_mode(rx,&a)!=ESP_OK){i2s_del_channel(rx);rx=NULL;return false;}
    return true;
}
static void result(int value,const char *why) {
    xSemaphoreTake(lock,portMAX_DELAY);strlcpy(detail,why,sizeof(detail));xSemaphoreGive(lock);
    atomic_store(&state,value);ESP_LOGI(TAG,"%s: %s",names[value],why);
}
static void run(void) {
    // The 120 ms start tone plus DMA drain completes before enabling capture.
    int64_t cue_deadline=esp_timer_get_time()/1000+1000;
    while(!atomic_load(&cue_ready) && !atomic_load(&cancelled) && esp_timer_get_time()/1000<cue_deadline)
        vTaskDelay(pdMS_TO_TICKS(10));
    if(atomic_load(&cancelled)){result(CANCELLED,"cancelled_before_recording");return;}
    if(!atomic_load(&cue_ready)){result(FAILED,"recording_cue_unavailable");return;}
    vTaskDelay(pdMS_TO_TICKS(250));
    if(atomic_load(&cancelled)){result(CANCELLED,"cancelled_before_recording");return;}
    int16_t *pcm=heap_caps_malloc(16000*15*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pcm){result(FAILED,"recording_memory");return;}
    xSemaphoreTake(lock,portMAX_DELAY);samples=peak=0;rms=0;detail[0]=transcript[0]=0;xSemaphoreGive(lock);
    if(i2s_channel_enable(rx)!=ESP_OK){free(pcm);result(FAILED,"microphone_start");return;}
    atomic_store(&state,RECORDING);ESP_LOGI(TAG,"recording, max %u ms",duration);
    unsigned count=0,max=duration*16,maximum=0;double squares=0;
    int64_t deadline=esp_timer_get_time()/1000+duration+1000;
    bool ok=true;
    while(count<max && !atomic_load(&end_recording) && !atomic_load(&cancelled)) {
        int32_t raw[320];size_t n=0;
        if(esp_timer_get_time()/1000>deadline || i2s_channel_read(rx,raw,sizeof(raw),&n,100)!=ESP_OK || !n){ok=false;break;}
        for(unsigned i=0;i<n/sizeof(raw[0]) && count<max;++i) {
            int value=raw[i]>>16;pcm[count++]=(int16_t)value;
            unsigned a=abs(value);if(a>maximum)maximum=a;squares+=(double)value*value;
        }
        xSemaphoreTake(lock,portMAX_DELAY);samples=count;peak=maximum;rms=count?sqrt(squares/count):0;xSemaphoreGive(lock);
    }
    i2s_channel_disable(rx);
    if(atomic_load(&cancelled)){free(pcm);result(CANCELLED,"cancelled_before_send");return;}
    if(!ok || count<4800){free(pcm);result(FAILED,ok?"recording_too_short":"microphone_read");return;}
    if(!transmit){free(pcm);result(TESTED,"local_microphone_test_only_not_sent");return;}
    atomic_store(&state,SENDING);
    muse_hatch_turn_begin();
    unsigned sent=0;deadline=esp_timer_get_time()/1000+30000;
    while(sent<count && !atomic_load(&cancelled) && esp_timer_get_time()/1000<deadline) {
        unsigned n=count-sent;if(n>320)n=320;
        sent+=muse_hatch_turn_audio_wait(pcm+sent,n,100);
    }
    free(pcm);
    if(sent<count || atomic_load(&cancelled)) {
        muse_hatch_turn_cancel();result(atomic_load(&cancelled)?CANCELLED:FAILED,"voice_upload_interrupted");return;
    }
    muse_hatch_turn_end();deadline=esp_timer_get_time()/1000+90000;
    while(!atomic_load(&cancelled) && esp_timer_get_time()/1000<deadline) {
        char text[512];muse_hatch_ev_t ev=muse_hatch_turn_event(text,sizeof(text));
        if(ev==MUSE_HATCH_EV_HEARD) {
            xSemaphoreTake(lock,portMAX_DELAY);strlcpy(transcript,text,sizeof(transcript));xSemaphoreGive(lock);
        } else if(ev==MUSE_HATCH_EV_SENT) {result(SENT,"muse_acknowledged_voice_note");return;}
        else if(ev==MUSE_HATCH_EV_ERROR) {muse_hatch_turn_cancel();result(FAILED,"muse_voice_error");return;}
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    muse_hatch_turn_cancel();result(atomic_load(&cancelled)?CANCELLED:FAILED,"voice_ack_timeout_or_cancel");
}
static void task(void *arg) {
    (void)arg;
    if(!mic_init()){result(FAILED,"microphone_init");vTaskDelete(NULL);return;}
    atomic_store(&ready,true);ESP_LOGI(TAG,"16 kHz microphone ready; capture only on request");
    for(;;){if(atomic_load(&state)==QUEUED)run();vTaskDelay(pdMS_TO_TICKS(20));}
}
void puppy_voice_init(void) {
    lock=xSemaphoreCreateMutex();if(!lock)return;
    voice_hatch_refresh();muse_hatch_start();
    xTaskCreate(task,"rig_voice",6144,NULL,3,NULL);
}
bool puppy_voice_busy(void){int s=atomic_load(&state);return s==QUEUED || s==RECORDING || s==SENDING;}
bool puppy_voice_recording(void){return atomic_load(&state)==RECORDING || atomic_load(&state)==QUEUED;}
bool puppy_voice_start(unsigned ms,bool send) {
    if(!atomic_load(&ready) || puppy_voice_busy() || ms<300 || ms>15000)return false;
    if(send){voice_hatch_refresh();if(!muse_hatch_ready())return false;}
    duration=ms;transmit=send;atomic_store(&cancelled,false);atomic_store(&end_recording,false);atomic_store(&cue_ready,false);atomic_store(&state,QUEUED);return true;
}
const char *puppy_voice_phase(void){return names[atomic_load(&state)];}
void puppy_voice_cue_ready(void){atomic_store(&cue_ready,true);}
void puppy_voice_send(void){atomic_store(&end_recording,true);}
void puppy_voice_cancel(void){atomic_store(&cancelled,true);}
cJSON *puppy_voice_status(void) {
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"state",names[atomic_load(&state)]);
    cJSON_AddBoolToObject(p,"microphone_ready",atomic_load(&ready));
    muse_hatch_status_t hs;muse_hatch_status(&hs);cJSON_AddStringToObject(p,"connection",muse_hatch_state_name(hs.state));
    if(lock){xSemaphoreTake(lock,portMAX_DELAY);
        cJSON_AddNumberToObject(p,"recorded_ms",samples/16);cJSON_AddNumberToObject(p,"peak",peak);cJSON_AddNumberToObject(p,"rms",rms);
        cJSON_AddStringToObject(p,"detail",detail);cJSON_AddStringToObject(p,"transcript",transcript);xSemaphoreGive(lock);}
    return p;
}
