"""Compare the pure port against compiled vendor trajectories and audit every plan."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
BOARD=ROOT/'main/boards/rig_puppy'
class RigActionsTest(unittest.TestCase):
    def test_vendor_parity_and_all_bounded_plans(self):
        code=r'''
#include "puppy_actions.h"
#include "puppy_native.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint64_t hash_value(uint64_t h,int value) {
 for(int b=0;b<4;++b){h^=((uint32_t)value>>(b*8))&255;h*=1099511628211ULL;}return h;
}
int main(void) {
 const int zero[5]={2376,637,2334,511,1505};
 unsigned covered=0;puppy_action_plan_t plan;
 for(size_t n=0;n<rig_show_count;++n) {
  const rig_show_t *s=&rig_shows[n];if(!s->motion)continue;
  assert(puppy_action_plan(s->name,zero,&plan));
  assert(plan.count>0 && plan.count<=901);
  assert(plan.duration_ms==puppy_action_nominal_ms(plan.id));
  if(plan.id<=16)covered|=1u<<(plan.id-1);
  puppy_native_t c,raw;puppy_native_init(&c);puppy_native_init(&raw);
  int target[5];unsigned speed;
  for(unsigned j=0;j<plan.count;++j) {
    assert(puppy_action_sample(&plan,zero,&c,target,&speed));
    for(int i=0;i<5;++i)assert(target[i]>=200 && target[i]<=2800);
    if(plan.id<=16){puppy_native_tick(plan.id,&raw);assert(speed==raw.speed);}
    assert(speed<=6000);
  }
  assert(c.done);
  const int stand[5]={1826,1187,1784,1061,1505};
  if(plan.returns_standing)assert(!memcmp(stand,target,sizeof(stand)));
  else {assert(plan.id==15);assert(target[2]==934);}
 }
 assert(puppy_action_tick_ms(0)==0 && puppy_action_tick_ms(1)==10 && puppy_action_tick_ms(2)==25 && puppy_action_tick_ms(3)==35);
 assert(covered==65535 && rig_show_count==24);
 assert(puppy_action_id("wake")==9 && puppy_action_id("happy")==4);
 assert(!puppy_action_plan("nonexistent",zero,&plan));
 const int invalid[5]={199,600,2400,600,1500};assert(!puppy_action_plan("sit",invalid,&plan));
 for(unsigned id=1;id<=16;++id) {
  puppy_native_t c;puppy_native_init(&c);uint64_t h=14695981039346656037ULL;
  for(unsigned t=0;t<=900;++t) {
   puppy_native_tick(id,&c);
   for(int i=0;i<5;++i){h=hash_value(h,c.offsets[i]);}
   h=hash_value(h,c.speed);
  }
  printf("%u %016llx\n",id,(unsigned long long)h);
 }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            d=Path(folder);(d/'test.c').write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'main'),'-I',str(BOARD),str(d/'test.c'),str(BOARD/'puppy_actions.c'),str(BOARD/'puppy_native.c'),str(BOARD/'puppy_gait.c'),str(BOARD/'rig_motion.c'),'-lm','-o',str(d/'test')],check=True)
            actual=subprocess.check_output([str(d/'test')],text=True)
        fixtures=[json.loads((ROOT/'tests/fixtures'/name).read_text()) for name in
                  ['puppy_reference.json','puppy_reference_linux.json']]
        self.assertEqual(fixtures[0]['source_sha256'],fixtures[1]['source_sha256'])
        self.assertEqual(fixtures[0]['ticks_per_action'],fixtures[1]['ticks_per_action'])
        # Both complete vectors come from independently compiled, unchanged vendor
        # C++. Preserve strict hash matching; do not infer new baselines from the port.
        self.assertIn([line.split()[1] for line in actual.splitlines()],
                      [list(f['digests'].values()) for f in fixtures])
