#include "puppy_imu.h"
#include <math.h>
#include <string.h>
const char *puppy_imu_event_name(puppy_imu_event_t e) {
    return e==IMU_HANDLED?"handled":e==IMU_SHAKEN?"shaken":e==IMU_TILTED?"tilted":"none";
}
static void event(puppy_imu_model_t *m,puppy_imu_event_t e,int64_t now) {
    if(now<m->cooldown)return;
    m->sample.event=e;m->sample.event_ms=now;++m->sample.sequence;
    m->cooldown=now+3000;m->handling_at=0;m->shocks=0;
}
void puppy_imu_update(puppy_imu_model_t *m,const float a[3],const float g[3],int64_t now) {
    float norm=sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    if(!isfinite(norm) || norm<0.05f || norm>4.0f)return;
    if(m->sample.sampled_ms && now-m->sample.sampled_ms>200) {
        m->tilt_since=m->upright_since=m->handling_at=m->stable_since=0;
        m->shocks=0;m->have_previous=false;
    }
    memcpy(m->sample.accel,a,3*sizeof(float));memcpy(m->sample.gyro,g,3*sizeof(float));
    m->sample.ready=true;m->sample.sampled_ms=now;
    m->sample.roll=atan2f(a[1],a[2])*57.29578f;
    m->sample.pitch=atan2f(-a[0],sqrtf(a[1]*a[1]+a[2]*a[2]))*57.29578f;
    float vertical=a[2]/norm;
    if(vertical<0.5f) {
        m->upright_since=0;if(!m->tilt_since)m->tilt_since=now;
        if(now-m->tilt_since>=600 && !m->sample.tilted){m->sample.tilted=true;event(m,IMU_TILTED,now);}
    } else if(vertical>0.8f) {
        m->tilt_since=0;if(!m->upright_since)m->upright_since=now;
        if(now-m->upright_since>=600)m->sample.tilted=false;
    } else {m->tilt_since=m->upright_since=0;}
    float jerk=0;for(int i=0;i<3;++i)jerk+=fabsf(a[i]-m->previous[i]);
    if(m->have_previous && jerk>0.55f && now-m->shock_at>=80) {
        m->shocks=now-m->shock_at<700?m->shocks+1:1;m->shock_at=now;
        m->handling_at=now;
        if(m->shocks>=3 && !m->sample.tilted)event(m,IMU_SHAKEN,now);
    } else if(m->have_previous && fabsf(norm-1)>0.22f && jerk>0.15f && !m->handling_at)m->handling_at=now;
    if(fabsf(norm-1)<0.10f && jerk<0.10f) {
        if(!m->stable_since)m->stable_since=now;
        if(m->handling_at && now-m->stable_since>=300 && now-m->handling_at<1600 && vertical>0.7f)
            event(m,IMU_HANDLED,now);
    } else m->stable_since=0;
    if(m->handling_at && now-m->handling_at>=1600)m->handling_at=0;
    memcpy(m->previous,a,3*sizeof(float));m->have_previous=true;
}
