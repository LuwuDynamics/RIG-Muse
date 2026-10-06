#!/usr/bin/env python3
"""Import the vendor's pure action formulas, without UART, globals or tasks.
Run with the sibling RIG-Omni/main/boards/puppy/xgo_action.cc path.
The generated module is checked in; firmware builds never need RIG-Omni.
"""
import hashlib
from pathlib import Path
import re
import sys
names = ['Wave','Naughty','Lookup','Swing','Rolling','Angry','Swimming','Pee',
         'Stretch','Bouncing','Shaking','Sit','Scratch','Hug','Keep_Sit','Sit_Reset']
src = Path(sys.argv[1]).read_text()
parts = []
for name in names:
    start = src.index('void '+name+'(){')
    brace = src.index('{',start)
    depth = 1
    end = brace+1
    while depth:
        depth += (src[end]=='{') - (src[end]=='}')
        end += 1
    body = src[brace:end]
    body = re.sub(r'Action_Counter\[\w+\]', 'ctx->tick', body)
    body = body.replace('motor_speed','ctx->speed')
    body = body.replace('Clear_State(', 'clear_state(ctx,').replace('set_motor_pos(', 'set_pose(ctx,')
    # C++ math overloads keep a float argument as float; C otherwise promotes it.
    body = body.replace('sin(phase)', 'sinf(phase)').replace('cos(phase)', 'cosf(phase)')
    parts.append('static void '+name+'(puppy_native_t *ctx) '+body+'\n')
header = '''// Imported pure formulas from RIG-Omni/main/boards/puppy/xgo_action.cc.
// Regenerate with tools/import_puppy_actions.py; no hardware or shared globals.
// Source SHA256: '''+hashlib.sha256(src.encode()).hexdigest()+'''
#include "puppy_native.h"
#include <math.h>
#include <string.h>
#define TS 100
#define PI 3.14159
static void set_pose(puppy_native_t *ctx,int a,int b,int c,int d,int e) {
    const int pose[5]={a,b,c,d,e}; memcpy(ctx->offsets,pose,sizeof(pose));
}
static void clear_state(puppy_native_t *ctx,int done) {
    set_pose(ctx,-600,600,-600,600,0);ctx->speed=1000;ctx->done=done!=0;
}
void puppy_native_init(puppy_native_t *ctx) {
    memset(ctx,0,sizeof(*ctx));clear_state(ctx,0);
}
'''
footer='void puppy_native_tick(unsigned id,puppy_native_t *ctx) {\n    switch(id) {\n'
for id,name in enumerate(names,1):footer+=f'    case {id}: {name}(ctx);break;\n'
footer+='    default:ctx->done=true;break;\n    }\n    if(id==15 && ctx->tick>=400)ctx->done=true; // finite job, keep seated pose\n    ++ctx->tick;\n}\n'
out=Path(__file__).resolve().parents[1]/'main/boards/rig_puppy/puppy_native.c'
out.write_text(header+'\n'.join(parts)+footer)
print('Imported 16 pure action formulas; source hash recorded.')
