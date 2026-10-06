// RIG-Omni Puppy ControlLaser: GPIO46 low=off, high=on, low/high cycles mode.
// Called only under the board lock (except initialization before the worker).
#include "puppy_laser.h"
#include "board_config.h"
#include "driver/gpio.h"
static bool ready,on,pending;
static int64_t expires,cycle_at;
static bool level(bool value) {
    if(gpio_set_level(PUPPY_LASER_GPIO,value)!=ESP_OK)return false;
    on=value;return true;
}
void puppy_laser_init(void) {
    gpio_config_t c={.pin_bit_mask=1ULL<<PUPPY_LASER_GPIO,.mode=GPIO_MODE_OUTPUT,
                    .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,
                    .intr_type=GPIO_INTR_DISABLE};
    ready=gpio_config(&c)==ESP_OK;
    if(ready)ready=level(false);
}
void puppy_laser_stop(void) {
    pending=false;expires=0;
    if(ready && !level(false))ready=false;
}
bool puppy_laser_set(unsigned mode,unsigned duration_ms,int64_t now) {
    if(!ready || mode>2 || (mode && (!duration_ms || duration_ms>30000)))return false;
    if(!mode){puppy_laser_stop();return ready;}
    pending=false;
    if(!level(mode==1)){ready=false;return false;}
    expires=now+duration_ms;
    if(mode==2){pending=true;cycle_at=now+10;}
    return true;
}
void puppy_laser_tick(int64_t now) {
    if(!ready)return;
    if(expires && now>=expires){puppy_laser_stop();return;}
    if(pending && now>=cycle_at) {
        pending=false;
        if(!level(true)){ready=false;expires=0;}
    }
}
bool puppy_laser_ready(void){return ready;}
bool puppy_laser_on(void){return on;}
unsigned puppy_laser_remaining(int64_t now){return expires>now?(unsigned)(expires-now):0;}
