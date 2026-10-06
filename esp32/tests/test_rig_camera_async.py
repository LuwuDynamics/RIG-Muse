"""Compile the actual app dispatch/worker to check async camera ownership."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
JSON=ROOT/'managed_components/espressif__cjson/cJSON'
class RigCameraAsyncTest(unittest.TestCase):
    def test_one_worker_and_session_bound_results(self):
        app=(ROOT/'main/app.c').read_text()
        task=app.split('// Capture/compress off the Noise control task',1)[1].split('#endif',1)[0]
        task='// Capture/compress off the Noise control task'+task
        dispatch=app.split('    if(!strcmp(command,"camera.capture") || !strcmp(command,"rig.camera")) {',1)[1].split('    cJSON *rig_result',1)[0]
        dispatch='if(!strcmp(command,"camera.capture") || !strcmp(command,"rig.camera")) {'+dispatch
        code=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include "cJSON.h"
typedef uint64_t noise_ctrl_session_generation_t;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdPASS 1
static void (*pending)(void *);
static void *pending_arg;
static bool fail_task;
static int captures,results,deletes;
static uint64_t generation;
static cJSON *command_error(const char *code,const char *message){(void)message;cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"error",code);return r;}
static cJSON *rig_robot_command(const char *command,const cJSON *p){(void)p;assert(!strcmp(command,"camera.capture"));++captures;return cJSON_CreateObject();}
static void noise_ctrl_send_command_result(uint64_t g,const char *id,cJSON *r){assert(!strcmp(id,"request-a"));generation=g;++results;cJSON_Delete(r);}
static void stack_monitor_record(void *p){(void)p;}
static void vTaskDeleteWithCaps(void *p){assert(!p);++deletes;}
static int xTaskCreateWithCaps(void (*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle,int caps){(void)name;(void)handle;assert(stack==8192 && priority<5 && caps==3);if(fail_task)return 0;pending=fn;pending_arg=arg;return pdPASS;}
'''+task+'''
static cJSON *dispatch(const char *command,const char *request_id,uint64_t session_generation){
'''+dispatch+'''return NULL;}
'''+r'''
int main(void){
 cJSON *r=dispatch("camera.capture","request-a",7);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"_async")));cJSON_Delete(r);assert(captures==0);
 r=dispatch("camera.capture","request-a",8);assert(!strcmp(cJSON_GetObjectItem(r,"error")->valuestring,"camera_busy"));cJSON_Delete(r);
 assert(!dispatch("rig.stop","request-a",8)); // stop dispatch is never queued behind capture
 pending(pending_arg);assert(captures==1 && results==1 && generation==7 && deletes==1);
 fail_task=true;r=dispatch("camera.capture","request-a",8);assert(cJSON_GetObjectItem(r,"error"));cJSON_Delete(r);
 fail_task=false;r=dispatch("rig.camera","request-a",9);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"_async")));cJSON_Delete(r);
 pending(pending_arg);assert(captures==2 && generation==9 && deletes==2);
}
'''
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);(d/'test.c').write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(JSON),str(d/'test.c'),str(JSON/'cJSON.c'),'-o',str(d/'test')],check=True)
            subprocess.run([str(d/'test')],check=True)
