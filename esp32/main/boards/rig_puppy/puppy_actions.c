#include "puppy_actions.h"
#include "puppy_native.h"
#include "puppy_gait.h"
#include "rig_motion.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
static const char *names[]={"wave","naughty","lookup","swing","rolling","angry",
    "swimming","pee","stretch","bouncing","shaking","sit","scratch","hug","keep_sit","sit_reset"};
unsigned puppy_action_id(const char *name) {
    if(!name)return 0;
    if(!strcmp(name,"greet"))return 1;
    if(!strcmp(name,"wake"))return 9;
    if(!strcmp(name,"happy"))return 4;
    if(!strcmp(name,"reset"))return 255;
    for(unsigned i=0;i<PUPPY_ACTION_COUNT;++i)if(!strcmp(name,names[i]))return i+1;
    return 0;
}
unsigned puppy_action_tick_ms(unsigned tick) {
    // xgo_control: action at control counters %5==1 or 3; loop delay 1+4 ms.
    return tick/2*25+(tick%2)*10;
}
unsigned puppy_action_nominal_ms(unsigned id) {
    static const unsigned ticks[]={420,200,550,400,600,250,600,450,550,500,500,550,750,800,400,150};
    return id>=1 && id<=16?puppy_action_tick_ms(ticks[id-1]):0;
}
unsigned puppy_action_due_ms(const puppy_action_plan_t *p,unsigned tick) {
    return p->id==254?tick*5:puppy_action_tick_ms(tick);
}
bool puppy_move_plan(int forward,int turn,unsigned duration,const int zero[5],puppy_action_plan_t *p) {
    if(duration<100 || duration>5000 || duration%5 || abs(forward)>100 || abs(turn)>100 || (!forward && !turn))return false;
    memset(p,0,sizeof(*p));p->id=254;p->duration_ms=duration;p->count=duration/5+1;
    p->forward=forward;p->turn=turn;p->returns_standing=true;
    int target[5];
    for(unsigned i=0;i<74;++i)if(!puppy_gait_target(forward,turn,i,zero,target))return false;
    return puppy_gait_target(0,0,0,zero,target);
}
static void bounded_offsets(unsigned id,int offsets[5]) {
    // Retain the board's established safe Wave endpoint without changing timing.
    if(id==1 && offsets[1]<-400)offsets[1]=-400;
}
bool puppy_action_sample(const puppy_action_plan_t *p,const int zero[5],
                         puppy_native_t *ctx,int target[5],unsigned *speed) {
    if(p->id==254) {
        ctx->done=ctx->tick*5>=p->duration_ms;*speed=0;
        bool ok=puppy_gait_target(ctx->done?0:p->forward,ctx->done?0:p->turn,ctx->tick,zero,target);
        ++ctx->tick;return ok;
    }
    if(p->id==255){ctx->done=true;ctx->speed=1000;ctx->tick=1;}
    else if(!ctx->done)puppy_native_tick(p->id,ctx);
    int offsets[5];memcpy(offsets,ctx->offsets,sizeof(offsets));
    bounded_offsets(p->id,offsets);
    const int stand[5]={-550,550,-550,550,0};
    for(int i=0;i<5;++i) {
        target[i]=zero[i]+(ctx->done && p->returns_standing?stand[i]:(int)(offsets[i]*p->scale[i]));
        if(target[i]<200 || target[i]>2800)return false;
    }
    *speed=ctx->speed;
    return true;
}
bool puppy_action_plan(const char *name,const int zero[5],puppy_action_plan_t *p) {
    memset(p,0,sizeof(*p));p->id=puppy_action_id(name);p->returns_standing=p->id!=15;
    if(!p->id)return false;
    const int stand[5]={-550,550,-550,550,0};
    for(int i=0;i<5;++i) {
        if(zero[i]<200 || zero[i]>2800 || zero[i]+stand[i]<200 || zero[i]+stand[i]>2800)return false;
        p->scale[i]=1;
    }
    if(p->id==255){p->count=1;return true;}
    puppy_native_t ctx;puppy_native_init(&ctx);int lo[5]={0},hi[5]={0};
    for(unsigned t=0;t<=900;++t) {
        puppy_native_tick(p->id,&ctx);bounded_offsets(p->id,ctx.offsets);
        for(int i=0;i<5;++i) {
            if(ctx.offsets[i]<lo[i])lo[i]=ctx.offsets[i];
            if(ctx.offsets[i]>hi[i])hi[i]=ctx.offsets[i];
        }
        if(ctx.done){p->count=t+1;p->duration_ms=puppy_action_tick_ms(t);break;}
    }
    if(!p->count)return false;
    p->scaled=p->id==1;
    for(int i=0;i<5;++i) {
        if(hi[i]>2800-zero[i])p->scale[i]=(double)(2800-zero[i])/hi[i];
        if(lo[i]<200-zero[i]) {
            double lower=(double)(zero[i]-200)/-lo[i];
            if(lower<p->scale[i])p->scale[i]=lower;
        }
        if(p->scale[i]<0.5)return false;
        p->scaled |= p->scale[i]<1;
    }
    return true;
}

const rig_show_t rig_shows[]={
    {"greet","RIG-Omni Wave: shift weight, raise front paw, wave and stand.",FACE_HAPPY,SOUND_CHIRP,3000,true},
    {"sleep","Pretend sleep with a short snore; no body motion.",FACE_SLEEP,SOUND_SNORE,6000,false},
    {"wake","Wake up with the original Stretch body sequence.",FACE_SURPRISE,SOUND_SPARKLE,3000,true},
    {"happy","Celebrate with the original Swing body sequence.",FACE_HAPPY,SOUND_SPARKLE,3000,true},
    {"curious","Curious eyes and whistle; no body motion.",FACE_CURIOUS,SOUND_QUESTION,3000,false},
    {"shy","Blushing eyes and boops; no body motion.",FACE_SHY,SOUND_BOOP,3000,false},
    {"surprised","Wide eyes and chirps; no body motion.",FACE_SURPRISE,SOUND_CHIRP,3000,false},
    {"wave","RIG-Omni: Wave the front paw.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"naughty","RIG-Omni: Playful body wiggle.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"lookup","RIG-Omni: Look upward.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"swing","RIG-Omni: Swing the body.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"rolling","RIG-Omni: Rock the body.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"angry","RIG-Omni: Angry pose.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"swimming","RIG-Omni: Swimming motion.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"pee","RIG-Omni: Raise a hind leg.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"stretch","RIG-Omni: Stretch the body.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"bouncing","RIG-Omni: Bounce in place.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"shaking","RIG-Omni: Shake the body.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"sit","RIG-Omni: Sit then stand up.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"scratch","RIG-Omni: Sit and scratch.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"hug","RIG-Omni: Sit and hug.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"keep_sit","RIG-Omni: Sit and remain seated until another command.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"sit_reset","RIG-Omni: Rise from the current seated pose.",FACE_HAPPY,SOUND_BOOP,3000,true},
    {"reset","Return to the calibrated standing pose.",FACE_IDLE,SOUND_NONE,3000,true},
};
const size_t rig_show_count=sizeof(rig_shows)/sizeof(rig_shows[0]);
const rig_show_t *rig_show_find(const char *name) {
    if(name)for(size_t i=0;i<rig_show_count;++i)if(!strcmp(name,rig_shows[i].name))return &rig_shows[i];
    return NULL;
}
