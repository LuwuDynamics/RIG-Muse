#include "puppy_gait.h"
#include <math.h>
#include <stdlib.h>
bool puppy_gait_target(int forward,int turn,unsigned tick,const int zero[5],int out[5]) {
    if(abs(forward)>100 || abs(turn)>100)return false;
    const double pi=3.14159265358979323846;
    const float lp[4][5]={{3*pi/4,3*pi/4,3*pi/4,3*pi/4,pi/4},
        {-pi/4,-pi/4,-pi/4,-pi/4,pi/4},{-pi/4,3*pi/4,3*pi/4,-pi/4,pi/4},
        {3*pi/4,-pi/4,-pi/4,3*pi/4,pi/4}};
    int vx=(int)(2.2*forward),vyaw=(int)(2.8*turn);
    float phase=0;
    // Vendor wraps before increment, at 74 ticks; preserve float accumulation.
    for(unsigned i=0;i<=tick%74;++i)phase+=0.085;
    if(abs(vx)<=15 && abs(vyaw)<=15) {
        const int stand[5]={-550,550,-550,550,0};
        for(int i=0;i<5;++i)out[i]=zero[i]+stand[i];
    } else {
        float ratio=(float)abs(vx)/(abs(vx)+abs(vyaw));
        float step=sqrt(vx*vx+vyaw*vyaw);
        int xi=vx>0?0:1,yi=vyaw>0?3:2;
        for(int i=0;i<5;++i) {
            int offset=(int)((i==4?200.0:step)*cos(phase+ratio*lp[xi][i]+(1-ratio)*lp[yi][i]));
            out[i]=zero[i]+(i==4?offset:(i%2?700:-700)+(i<2?offset:-offset));
        }
    }
    for(int i=0;i<5;++i)if(out[i]<200 || out[i]>2800)return false;
    return true;
}
