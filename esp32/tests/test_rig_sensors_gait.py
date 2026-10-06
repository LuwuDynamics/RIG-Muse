from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
B=ROOT/'main/boards/rig_puppy'
class SensorsGaitTest(unittest.TestCase):
 def test_gait_matches_vendor_and_imu_rearms(self):
  gait=r'''
#include <cassert>
#include <cstdlib>
#include <cmath>
#define PI 3.14159265358979323846
extern "C" {
#include "puppy_gait.h"
}
struct Motor{int ZeroPos,DesPos;}motor[5];
float vx,vyaw,angle1,angle2,angle3,angle4,angle5;int control_mode;
float l_p[][5]={{3*PI/4,3*PI/4,3*PI/4,3*PI/4,PI/4},{-PI/4,-PI/4,-PI/4,-PI/4,PI/4},{-PI/4,3*PI/4,3*PI/4,-PI/4,PI/4},{3*PI/4,-PI/4,-PI/4,3*PI/4,PI/4}};
void set_motor_angle(float,float,float,float,float){}
#include "puppy_gait_reference.inc"
int main(int argc,char **argv){assert(argc==3);int forward=atoi(argv[1]),turn=atoi(argv[2]);vx=int(2.2*forward);vyaw=int(2.8*turn);
 const int zero[5]={2376,637,2334,511,1505};for(int i=0;i<5;++i)motor[i].ZeroPos=zero[i];
 for(unsigned t=0;t<1000;++t){move();int out[5];assert(puppy_gait_target(forward,turn,t,zero,out));for(int i=0;i<5;++i)assert(out[i]==motor[i].DesPos);}
}
'''
  imu=r'''
#include <assert.h>
#include "puppy_imu.h"
int main(void){puppy_imu_model_t m={0};float g[3]={0},up[3]={0,0,1},side[3]={1,0,0};
 for(int t=20;t<2000;t+=20)puppy_imu_update(&m,up,g,t);assert(m.sample.ready && !m.sample.tilted && !m.sample.sequence);
 for(int t=2000;t<2900;t+=20)puppy_imu_update(&m,side,g,t);assert(m.sample.tilted && m.sample.event==IMU_TILTED && m.sample.sequence==1);
 for(int t=2900;t<6500;t+=20)puppy_imu_update(&m,side,g,t);assert(m.sample.sequence==1);
 for(int t=6500;t<7500;t+=20)puppy_imu_update(&m,up,g,t);assert(!m.sample.tilted);
 for(int t=7500;t<8400;t+=20)puppy_imu_update(&m,side,g,t);assert(m.sample.tilted && m.sample.sequence==2);
 puppy_imu_model_t lift={0};puppy_imu_update(&lift,up,g,1000);float pulse[3]={0,0,1.4};puppy_imu_update(&lift,pulse,g,1100);
 for(int t=1120;t<1600;t+=20)puppy_imu_update(&lift,up,g,t);assert(lift.sample.event==IMU_HANDLED);
 puppy_imu_model_t shake={0};puppy_imu_update(&shake,up,g,1000);
 float a[3]={1,0,1},b[3]={-1,0,1};puppy_imu_update(&shake,a,g,1100);puppy_imu_update(&shake,b,g,1200);puppy_imu_update(&shake,a,g,1300);assert(shake.sample.event==IMU_SHAKEN);
}
'''
  with tempfile.TemporaryDirectory() as d:
   d=Path(d);(d/'gait.cpp').write_text(gait);(d/'imu.c').write_text(imu)
   flags=['-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(B)]
   subprocess.run(['cc','-std=c11',*flags,'-c',str(B/'puppy_gait.c'),'-o',str(d/'gait.o')],check=True)
   subprocess.run(['c++','-std=c++17',*flags,'-I',str(ROOT/'tests/fixtures'),str(d/'gait.cpp'),str(d/'gait.o'),'-o',str(d/'gait')],check=True)
   for f,t in [(40,0),(-40,0),(0,40),(0,-40),(100,100),(0,0)]:subprocess.run([str(d/'gait'),str(f),str(t)],check=True)
   subprocess.run(['cc','-std=c11',*flags,str(d/'imu.c'),str(B/'puppy_imu_model.c'),'-lm','-o',str(d/'imu')],check=True)
   subprocess.run([str(d/'imu')],check=True)
