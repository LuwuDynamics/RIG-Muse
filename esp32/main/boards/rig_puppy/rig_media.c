// Hardware reference: RIG-Omni/main/boards/common/config.h,
// puppy/puppy_board.cc InitializeLcdDisplay and audio/codecs/no_audio_codec.cc.
// Dedicated screen + effects only: no microphone, TTS, LVGL or network assets.
#include "rig_media.h"
#include "board_config.h"
#include <stdatomic.h>
#include "driver/spi_master.h"
#include "driver/i2s_std.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#if CONFIG_RIG_PARTICLE_FACE
#include "rig_pface.h"
#if CONFIG_RIG_PARTICLE_FACE_BURST
#define PFACE_STYLE RIG_PFACE_BURST
#elif CONFIG_RIG_PARTICLE_FACE_DUST
#define PFACE_STYLE RIG_PFACE_DUST
#else
#define PFACE_STYLE RIG_PFACE_VORTEX
#endif
#define PFACE_STEP_S (1.0f/30.0f)   // physics step, the frame rate the Omni emote engine targets
#define PFACE_FRAME_MS 33
static rig_pface_t *pface;          // NULL: draw the original flat eyes instead
#endif
static const char *TAG="rig.media";
static SemaphoreHandle_t lock, drawn;
static esp_lcd_panel_handle_t panel;
static i2s_chan_handle_t tx;
static uint16_t *strip;
#if CONFIG_RIG_PARTICLE_FACE
static uint16_t *strip2;            // second strip buffer, optional
#endif
static atomic_bool screen_ok, audio_ok;
static bool connected;
static rig_face_t face;
static rig_sound_t sound;
static uint32_t sample_index;
static int64_t face_start;
static bool transfer_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *ctx) {
    (void)io; (void)event; (void)ctx;
    BaseType_t wake=pdFALSE;
    xSemaphoreGiveFromISR(drawn,&wake);
    return wake==pdTRUE;
}
#if CONFIG_RIG_PARTICLE_FACE_BENCH
// What the face costs when nothing preempts it: min of 10 runs with interrupts off on this core (~3 ms at a time, once).
static void pface_bench(uint16_t *buf) {
    static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
    int64_t step_us=INT64_MAX,render_us=INT64_MAX,flat_us=INT64_MAX;
    for(int i=0;i<10;++i) {
        portENTER_CRITICAL(&mux);
        int64_t t0=esp_timer_get_time();
        rig_pface_step(pface,PFACE_STEP_S);
        int64_t t1=esp_timer_get_time();
        for(int y=0;y<240;y+=16)rig_pface_render(pface,buf,y,16);
        int64_t t2=esp_timer_get_time();
        for(int y=0;y<240;y+=16)rig_face_render(buf,y,16,FACE_IDLE,1000,true); // the original flat eyes, for comparison
        int64_t t3=esp_timer_get_time();
        portEXIT_CRITICAL(&mux);
        if(t1-t0<step_us)step_us=t1-t0;
        if(t2-t1<render_us)render_us=t2-t1;
        if(t3-t2<flat_us)flat_us=t3-t2;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    unsigned core10=(unsigned)((step_us+render_us)*30/1000); // tenths of a percent of one core at 30 frames per second
    // The original face rendered one frame, then waited on 15 strip transfers (~25 ms) and slept 50 ms.
    unsigned flat_fps10=(unsigned)(10000000/(flat_us+75000)), flat_core10=(unsigned)(flat_us*flat_fps10/10000);
    ESP_LOGI(TAG,"particle face CPU, no preemption: sim %u us per step + render %u us per frame = %u.%u%% of one core at 30 fps",
        (unsigned)step_us,(unsigned)render_us,core10/10,core10%10);
    ESP_LOGI(TAG,"original flat eyes, same method: render %u us per frame, at its ~%u.%u fps = %u.%u%% of one core",
        (unsigned)flat_us,flat_fps10/10,flat_fps10%10,flat_core10/10,flat_core10%10);
}
#endif
static void lcd_failed(void) {
    xSemaphoreTake(lock,portMAX_DELAY);screen_ok=false;xSemaphoreGive(lock);
    ESP_LOGE(TAG,"LCD transfer failed");
    vTaskDelete(NULL); // Keep the DMA buffers alive even if a transfer timed out.
}
static void screen_worker(void *unused) {
    (void)unused;
#if CONFIG_RIG_PARTICLE_FACE
    rig_face_t shown=FACE_IDLE;
    int64_t started=esp_timer_get_time(), last=started, busy_us=0, step_us=0, render_us=0, xfer_us=0;
    float owed=0; // real time not yet simulated
    unsigned frames=0;
#endif
    for(;;) {
        rig_face_t f; bool c; uint32_t ms=0;
#if CONFIG_RIG_PARTICLE_FACE
        if(pface) { // two independent words: no need to queue behind audio_worker, which holds the lock while it writes I2S
            f=__atomic_load_n(&face,__ATOMIC_RELAXED);c=__atomic_load_n(&connected,__ATOMIC_RELAXED);
        } else
#endif
        {
            xSemaphoreTake(lock,portMAX_DELAY);
            f=face;c=connected;ms=(esp_timer_get_time()-face_start)/1000;
            xSemaphoreGive(lock);
        }
#if CONFIG_RIG_PARTICLE_FACE
        if(pface) {
            int64_t frame_start=esp_timer_get_time();
            owed+=(float)(frame_start-last)*1e-6f;last=frame_start;
            if(owed>0.1f)owed=0.1f; // after a stall, do not fast-forward
            if(f!=shown){rig_pface_set_face(pface,f);shown=f;}
            rig_pface_set_connected(pface,c);
            for(;owed>=PFACE_STEP_S;owed-=PFACE_STEP_S)rig_pface_step(pface,PFACE_STEP_S);
            step_us+=esp_timer_get_time()-frame_start;
            // Render strip k+1 while strip k is on the wire. Without a second buffer this degrades to one at a time.
            uint16_t *buf[2]={strip,strip2?strip2:strip};
            int depth=strip2?2:1,inflight=0,k=0;
            for(int y=0;y<240;y+=16,++k) {
                uint16_t *b=buf[k&1];
                int64_t t0=esp_timer_get_time();
                if(inflight>=depth){if(xSemaphoreTake(drawn,pdMS_TO_TICKS(200))!=pdTRUE)lcd_failed();--inflight;}
                int64_t t1=esp_timer_get_time();
                rig_pface_render(pface,b,y,16);
                int64_t t2=esp_timer_get_time();
                if(esp_lcd_panel_draw_bitmap(panel,0,y,240,y+16,b)!=ESP_OK)lcd_failed();
                ++inflight;
                xfer_us+=(t1-t0)+(esp_timer_get_time()-t2);render_us+=t2-t1;
            }
            int64_t t3=esp_timer_get_time();
            for(;inflight>0;--inflight)if(xSemaphoreTake(drawn,pdMS_TO_TICKS(200))!=pdTRUE)lcd_failed();
            xfer_us+=esp_timer_get_time()-t3;
            int64_t spent=esp_timer_get_time()-frame_start;
            busy_us+=spent;
#if CONFIG_RIG_PARTICLE_FACE_BENCH
            if(frames==300)pface_bench(buf[0]); // buf[0] is idle here: every strip of this frame has completed
#endif
            if(++frames==300) { // once: what the real board sustains. Integer maths, newlib-nano safe.
                unsigned fps10=(unsigned)((int64_t)frames*10000000/(esp_timer_get_time()-started));
                ESP_LOGI(TAG,"particle face: %u frames, %u.%u fps; per frame: busy %u us = sim %u + render %u + waiting on the screen %u; stack headroom %u bytes",
                    frames,fps10/10,fps10%10,(unsigned)(busy_us/frames),(unsigned)(step_us/frames),(unsigned)(render_us/frames),
                    (unsigned)(xfer_us/frames),(unsigned)uxTaskGetStackHighWaterMark(NULL));
            }
            vTaskDelay(pdMS_TO_TICKS(spent<(PFACE_FRAME_MS-1)*1000?PFACE_FRAME_MS-spent/1000:1));
            continue;
        }
#endif
        for(int y=0;y<240;y+=16) {
            rig_face_render(strip,y,16,f,ms,c);
            esp_err_t err=esp_lcd_panel_draw_bitmap(panel,0,y,240,y+16,strip);
            if(err!=ESP_OK || xSemaphoreTake(drawn,pdMS_TO_TICKS(200))!=pdTRUE)lcd_failed();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
static void audio_worker(void *unused) {
    (void)unused;
    int32_t samples[160]; // 10 ms blocks, 32-bit mono LEFT slot, as the vendor firmware.
    for(;;) {
        xSemaphoreTake(lock,portMAX_DELAY);
        for(size_t i=0;i<160;++i) {
            samples[i]=(int32_t)rig_sound_sample(sound,sample_index)*65536;
            if(sample_index<16000*6)++sample_index;
        }
        size_t written=0;
        // Serialize submission with stop. DMA holds at most 30 ms of old audio.
        esp_err_t err=i2s_channel_write(tx,samples,sizeof(samples),&written,30);
        if(err!=ESP_OK || written!=sizeof(samples))audio_ok=false;
        xSemaphoreGive(lock);
        if(err!=ESP_OK || written!=sizeof(samples)) {
            i2s_channel_disable(tx);
            ESP_LOGE(TAG,"I2S output failed");vTaskDelete(NULL);
        }
        vTaskDelay(1); // Let cancellation and display updates acquire the mutex.
    }
}
bool rig_media_ready(void) {
    if(!lock)return false;
    return atomic_load(&screen_ok) && atomic_load(&audio_ok);
}
void rig_media_start(const rig_show_t *show) {
    if(!lock || !show)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    face=show->face;sound=show->sound;sample_index=0;face_start=esp_timer_get_time();
    xSemaphoreGive(lock);
}
void rig_media_stop(void) {
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    face=FACE_IDLE;sound=SOUND_NONE;sample_index=0;face_start=esp_timer_get_time();
    xSemaphoreGive(lock);
}
void rig_media_connected(bool value) {
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);connected=value;xSemaphoreGive(lock);
}
#define INIT(call) do { esp_err_t e=(call); if(e!=ESP_OK) { ESP_LOGE(TAG,"%s: %s",#call,esp_err_to_name(e));return; } } while(0)
void rig_media_init(void) {
    lock=xSemaphoreCreateMutex();drawn=xSemaphoreCreateCounting(4,0); // counts completions: two strips can finish between waits
    strip=heap_caps_malloc(240*16*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
#if CONFIG_RIG_PARTICLE_FACE
    strip2=heap_caps_malloc(240*16*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
#endif
    if(!lock||!drawn||!strip){ESP_LOGE(TAG,"media allocation failed");return;}
    spi_bus_config_t bus={.mosi_io_num=PUPPY_LCD_MOSI,.miso_io_num=-1,.sclk_io_num=PUPPY_LCD_CLK,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=240*16*2};
    INIT(spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t cfg={.cs_gpio_num=PUPPY_LCD_CS,.dc_gpio_num=PUPPY_LCD_DC,.spi_mode=0,
        .pclk_hz=40000000,.trans_queue_depth=1,.lcd_cmd_bits=8,.lcd_param_bits=8,
        .on_color_trans_done=transfer_done};
    INIT(esp_lcd_new_panel_io_spi(SPI3_HOST,&cfg,&io));
    esp_lcd_panel_dev_config_t pc={.reset_gpio_num=PUPPY_LCD_RESET,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR,.bits_per_pixel=16};
    INIT(esp_lcd_new_panel_gc9a01(io,&pc,&panel));
    INIT(esp_lcd_panel_reset(panel));INIT(esp_lcd_panel_init(panel));
    INIT(esp_lcd_panel_invert_color(panel,true));INIT(esp_lcd_panel_swap_xy(panel,false));
    INIT(esp_lcd_panel_mirror(panel,false,true));INIT(esp_lcd_panel_disp_on_off(panel,true));
    screen_ok=true;
#if CONFIG_RIG_PARTICLE_FACE
    int64_t build_start=esp_timer_get_time();
    pface=rig_pface_create(PFACE_STYLE,0x5EEDFACEu);
    if(pface) {
        rig_pface_stats_t s=rig_pface_stats(pface);
        ESP_LOGI(TAG,"particle face ready: %u particles, %u KB, built in %u ms",
            s.particles,(unsigned)(s.bytes/1024),(unsigned)((esp_timer_get_time()-build_start)/1000));
    } else ESP_LOGW(TAG,"particle face unavailable (out of memory); drawing the original eyes");
#endif
    if(xTaskCreate(screen_worker,"rig_face",6144,NULL,2,NULL)!=pdPASS){screen_ok=false;return;}
    i2s_chan_config_t cc=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    cc.dma_desc_num=3;cc.dma_frame_num=160;cc.auto_clear_after_cb=true;
    INIT(i2s_new_channel(&cc,&tx,NULL));
    i2s_std_config_t audio={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_MONO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=PUPPY_SPEAKER_BCLK,.ws=PUPPY_SPEAKER_WS,.dout=PUPPY_SPEAKER_DOUT,.din=I2S_GPIO_UNUSED}};
    audio.slot_cfg.slot_mask=I2S_STD_SLOT_LEFT;
    INIT(i2s_channel_init_std_mode(tx,&audio));INIT(i2s_channel_enable(tx));
    audio_ok=true;
    if(xTaskCreate(audio_worker,"rig_sound",4096,NULL,3,NULL)!=pdPASS){audio_ok=false;i2s_channel_disable(tx);return;}
    ESP_LOGI(TAG,"GC9A01 240x240 and 16kHz sound effects ready; silent boot");
}
