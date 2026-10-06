"""Run the real recording/upload loop with scripted PCM and Muse events."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class RigVoiceTest(unittest.TestCase):
 def test_record_cancel_backpressure_and_ack_boundaries(self):
  s=(ROOT/'main/boards/rig_puppy/puppy_voice.c').read_text()
  globals=s[s.index('enum {'):s.index('static i2s_chan_handle_t')]
  body=s[s.index('static void result('):s.index('static void task(')]
  code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "host_compat.h"
#define ESP_OK 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(tag,...) ((void)(tag))
typedef void *SemaphoreHandle_t;
#include "muse_chat.h"
static const char *TAG="test";
static void *rx;
static int64_t now;
static int reads,begins,ends,cancels,uploaded,scenario;
static int event_number;
static void xSemaphoreTake(void *l,int t){(void)l;(void)t;}
static void xSemaphoreGive(void *l){(void)l;}
static void *heap_caps_malloc(size_t n,int caps){assert(caps==3);return malloc(n);}
static int64_t esp_timer_get_time(void){return now*1000;}
static void vTaskDelay(int ms){now+=ms;}
static int i2s_channel_enable(void *r){(void)r;assert(now>=1250);return 0;}
static int i2s_channel_disable(void *r){(void)r;return 0;}
'''+globals+r'''
static int i2s_channel_read(void *r,void *buffer,size_t n,size_t *got,int timeout){(void)r;(void)timeout;now+=20;++reads;
 int32_t *p=buffer;for(size_t i=0;i<n/4;++i)p[i]=(i%2?-1000:1000)*65536;*got=n;
 if(scenario==1 && reads==2)atomic_store(&cancelled,true);
 return scenario==4?-1:0;
}
void muse_hatch_turn_begin(void){++begins;}
size_t muse_hatch_turn_audio_wait(const int16_t *p,size_t n,int wait){assert(p[0]==1000);now+=wait;
 if(scenario==2){return 0;}
 if(scenario==3){atomic_store(&cancelled,true);return 0;}
 uploaded+=n;return n;
}
void muse_hatch_turn_end(void){++ends;}
void muse_hatch_turn_cancel(void){++cancels;}
muse_hatch_ev_t muse_hatch_turn_event(char *text,size_t cap){(void)cap;
 if(scenario==5)return MUSE_HATCH_EV_ERROR;
 if(!event_number++){strcpy(text,"hello");return MUSE_HATCH_EV_HEARD;}return MUSE_HATCH_EV_SENT;
}
'''+body+r'''
static void reset(int which,bool send){scenario=which;now=1000;reads=begins=ends=cancels=uploaded=event_number=0;duration=300;transmit=send;atomic_store(&cancelled,false);atomic_store(&end_recording,false);atomic_store(&ready,true);atomic_store(&state,QUEUED);atomic_store(&cue_ready,which!=6);if(which==7)atomic_store(&cancelled,true);run();}
int main(void){assert(!strcmp(names[SENT],"sent"));
 reset(0,true);assert(atomic_load(&state)==SENT && begins==1 && ends==1 && !cancels && uploaded==4800 && samples==4800 && peak==1000 && rms==1000 && !strcmp(transcript,"hello"));
 reset(0,false);assert(atomic_load(&state)==TESTED && !begins && !uploaded);
 reset(1,true);assert(atomic_load(&state)==CANCELLED && !begins && !ends);
 reset(2,true);assert(atomic_load(&state)==FAILED && begins==1 && !ends && cancels==1);
 reset(3,true);assert(atomic_load(&state)==CANCELLED && !ends && cancels==1);
 reset(4,true);assert(atomic_load(&state)==FAILED && !begins);
 reset(5,true);assert(atomic_load(&state)==FAILED && ends==1 && cancels==1);
 reset(6,true);assert(atomic_load(&state)==FAILED && !reads && !begins);
 reset(7,true);assert(atomic_load(&state)==CANCELLED && !reads && !begins);
}
'''
  with tempfile.TemporaryDirectory() as d:
   d=Path(d);(d/'test.c').write_text(code)
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'tests'),'-I',str(ROOT/'components/muse'),str(d/'test.c'),'-lm','-o',str(d/'test')],check=True)
   subprocess.run([str(d/'test')],check=True)
