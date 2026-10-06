"""Bounded procedural audio and strip rendering, with sanitizers."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class RigShowTest(unittest.TestCase):
    def test_effect_bounds_and_strip_consistency(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)
            (p/'test.c').write_text(r'''
#include "rig_show.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    assert(!rig_show_find(NULL) && !rig_show_find("unknown"));
    for(size_t i=0;i<rig_show_count;++i) {
        assert(rig_show_find(rig_shows[i].name)==&rig_shows[i]);
        assert(rig_shows[i].duration_ms<=6000);
    }
    for(int sound=SOUND_CHIRP;sound<=SOUND_SEND;++sound) {
        int peak=0;
        for(unsigned i=0;i<96000;++i) {
            int sample=rig_sound_sample(sound,i);
            assert(abs(sample)<=4800);
            if(abs(sample)>peak)peak=abs(sample);
            if(i>=80000)assert(sample==0);
        }
        assert(peak>1000);
        assert(rig_sound_sample(sound,0)==0);
        assert(rig_sound_sample(sound,UINT32_MAX)==0);
    }
    uint16_t *a=malloc(240*240*2),*b=malloc(240*240*2);
    for(int face=FACE_IDLE;face<=FACE_SHY;++face) {
        for(unsigned t=0;t<5000;t+=71) {
            rig_face_render(a,0,240,face,t,true);
            for(int y=0;y<240;y+=16)rig_face_render(b+y*240,y,16,face,t,true);
            assert(!memcmp(a,b,240*240*2));
        }
    }
    free(a);free(b);
}
''')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'main'),str(p/'test.c'),str(ROOT/'main/rig_show.c'),'-I',str(ROOT/'main/boards/rig_puppy'),str(ROOT/'main/boards/rig_puppy/puppy_actions.c'),str(ROOT/'main/boards/rig_puppy/puppy_native.c'),str(ROOT/'main/boards/rig_puppy/puppy_gait.c'),str(ROOT/'main/boards/rig_puppy/rig_motion.c'),'-lm','-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
