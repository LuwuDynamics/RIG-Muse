#!/usr/bin/env python3
"""Generate independent trajectory digests by compiling unmodified vendor C++.
Usage: python tools/puppy_reference_vectors.py /path/to/puppy/xgo_action.cc
Only regenerates a test fixture; not part of firmware build.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path)
parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'tests/fixtures/puppy_reference.json')
args=parser.parse_args()
source=args.source; body=source.read_text()
names=['Wave','Naughty','Lookup','Swing','Rolling','Angry','Swimming','Pee','Stretch','Bouncing','Shaking','Sit','Scratch','Hug','Keep_Sit','Sit_Reset']
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory); (d/'vendor.cc').write_text(body)
 (d/'xgo_action.h').write_text(source.with_suffix('.h').read_text())
 (d/'esp_log.h').write_text('#define ESP_LOGI(...) ((void)0)\n')
 (d/'xgo.h').write_text('''#include <stdint.h>
#define PI 3.14159
struct Motor {short ZeroPos;short DesPos;};
extern Motor motor[5];extern uint16_t motor_speed;
extern float vx,vyaw;extern int control_mode;
extern uint8_t actionLoop_FLAG,Action_ID;
''')
 harness='''#include "xgo_action.h"
#include <stdio.h>
Motor motor[5]={};uint16_t motor_speed;
float vx,vyaw;int control_mode;uint8_t actionLoop_FLAG,Action_ID;
extern uint16_t Action_Counter[ACTION_NUMBER];extern uint8_t ACTION_DONE;
static uint64_t hash_value(uint64_t h,int value) {
 for(int b=0;b<4;++b){h^=((uint32_t)value>>(b*8))&255;h*=1099511628211ULL;}return h;
}
int main(){
 void (*functions[])()={'''+','.join(names)+'''};
 for(unsigned id=1;id<=16;++id){
  normal_state();Action_ID=id;ACTION_DONE=0;uint64_t h=14695981039346656037ULL;
  for(unsigned t=0;t<=900;++t){Action_Counter[id]=t;functions[id-1]();
   for(int i=0;i<5;++i)h=hash_value(h,motor[i].DesPos);
   h=hash_value(h,motor_speed);
  }
  printf("%u %016llx\\n",id,(unsigned long long)h);
 }
}
'''
 (d/'check.cc').write_text(harness)
 subprocess.run(['c++','-std=c++17','-I',str(d),str(d/'vendor.cc'),str(d/'check.cc'),'-o',str(d/'check')],check=True)
 out=subprocess.check_output([str(d/'check')],text=True)
 result={'source_sha256':hashlib.sha256(body.encode()).hexdigest(),'ticks_per_action':901,'digests':{names[int(l.split()[0])-1]:l.split()[1] for l in out.splitlines()}}
 dest=args.output;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(result,indent=2)+'\n')
 print('Stored 16 independent vendor trajectory digests.')
