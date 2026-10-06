// QMI8658C registers/ranges and axis mapping from RIG-Omni common/imu.cc.
// Independent I2C0 task; never blocks the motor UART worker.
#include "puppy_imu.h"
#include "board_config.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
static SemaphoreHandle_t lock;
static puppy_imu_model_t model;
static i2c_master_dev_handle_t device;
static const char *TAG="rig.imu";
static void task(void *arg) {
    (void)arg;i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t c={.i2c_port=I2C_NUM_0,.sda_io_num=PUPPY_IMU_SDA,.scl_io_num=PUPPY_IMU_SCL,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    if(i2c_new_master_bus(&c,&bus)!=ESP_OK)goto failed;
    unsigned address=0x6b;
    if(i2c_master_probe(bus,address,50)!=ESP_OK)address=0x6a;
    i2c_device_config_t d={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=address,.scl_speed_hz=400000};
    if(i2c_master_bus_add_device(bus,&d,&device)!=ESP_OK){i2c_del_master_bus(bus);goto failed;}
    uint8_t reg=0,id=0;
    if(i2c_master_transmit_receive(device,&reg,1,&id,1,50)!=ESP_OK || id!=5)goto cleanup;
    const uint8_t setup[][2]={{2,0x40},{8,3},{3,4},{4,0x64},{6,0}};
    for(unsigned i=0;i<sizeof(setup)/sizeof(setup[0]);++i)
        if(i2c_master_transmit(device,setup[i],2,50)!=ESP_OK)goto cleanup;
    ESP_LOGI(TAG,"QMI8658C ready at 0x%02x",address);
    for(;;) {
        uint8_t data[12];reg=0x35;
        if(i2c_master_transmit_receive(device,&reg,1,data,sizeof(data),20)==ESP_OK) {
            float a[3],g[3];
            for(int i=0;i<3;++i){
                int16_t raw=(int16_t)(data[i*2]|data[i*2+1]<<8);
                a[i]=raw/16384.0f*(i==2?1:-1);
                raw=(int16_t)(data[6+i*2]|data[7+i*2]<<8);g[i]=raw/32.0f;
            }
            xSemaphoreTake(lock,portMAX_DELAY);puppy_imu_update(&model,a,g,esp_timer_get_time()/1000);xSemaphoreGive(lock);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
cleanup:
    i2c_master_bus_rm_device(device);i2c_del_master_bus(bus);device=NULL;
failed:
    ESP_LOGW(TAG,"IMU unavailable");vTaskDelete(NULL);
}
void puppy_imu_init(void) {
    lock=xSemaphoreCreateMutex();
    if(lock)xTaskCreate(task,"rig_imu",3072,NULL,2,NULL);
}
void puppy_imu_get(puppy_imu_sample_t *out) {
    memset(out,0,sizeof(*out));if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);*out=model.sample;xSemaphoreGive(lock);
    out->ready=out->ready && esp_timer_get_time()/1000-out->sampled_ms<200;
}
