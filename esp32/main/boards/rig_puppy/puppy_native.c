// Imported pure formulas from RIG-Omni/main/boards/puppy/xgo_action.cc.
// Regenerate with tools/import_puppy_actions.py; no hardware or shared globals.
// Source SHA256: 9a8bd1f11d7e262cf3472de58bbdff0b5f3f30593a9ccf512435217a573b3f33
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
static void Wave(puppy_native_t *ctx) {
    float duration[] = {0.4, 1.6, 1.8, 0.4};
    uint16_t timepoint[5] = {0};
    for(int i=1;i<5;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        set_pose(ctx,-100, +700, -600, 600, +200);
	}
	else if(counter>=timepoint[1] && counter<timepoint[2]){
        set_pose(ctx,-100, -400, -600, 600, 400);
        ctx->speed = 800;
	}else if(counter>=timepoint[2] && counter<timepoint[3]){
        ctx->speed = 0;
        if(counter%30==0)
            set_pose(ctx,-100, -200, -600, 600, 400);
        if(counter%30==15)
            set_pose(ctx,-100, -600, -600, 600, 400);
    }else if(counter>timepoint[3] && counter<timepoint[4]){
        set_pose(ctx,-300, 600, -600, 600, 200);
        ctx->speed = 0;
	}
    else if(counter==timepoint[4]){
        clear_state(ctx,1);
    }
}

static void Naughty(puppy_native_t *ctx) {
	float duration[] = {2.0};
    uint16_t timepoint[2] = {0};

    for(int i=1;i<2;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = counter*2.0*PI/TS;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 0;
        set_pose(ctx,0, 0, 0, 0, 0);
	}
	else if(counter>timepoint[0] && counter<timepoint[1]){
        set_pose(ctx,100 + 175*cos(4.5*phase), -100 + 175*cos(4.5*phase), 0, 0, -235*cos(4.5*phase));
	}else if(counter==timepoint[1]){
        clear_state(ctx,1);
    }
}

static void Lookup(puppy_native_t *ctx) {
    float duration[] = {0.5, 1.0, 1.5, 1.0, 1.5};
    uint16_t timepoint[6] = {0};
    for(int i=1;i<6;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 0;
        set_pose(ctx,-950, 950, -150, 150, 0);
    }
    else if(counter>timepoint[1] && counter<timepoint[2]){
        if(counter%40==0){
            set_pose(ctx,-950, 950, -100 + 150, 100 + 150, 100);
        }else if(counter%40==20){
            set_pose(ctx,-950, 950, -100 - 150, 100 - 150, -100);
        }
	}else if(counter>timepoint[2] && counter<timepoint[3]){
        set_pose(ctx,-950, 950, -100, 100, 0);
    }else if(counter>timepoint[3] && counter<timepoint[4]){
        if(counter%40==0){
            set_pose(ctx,-950, 950, -100 + 150, 100 + 150, 100);
        }else if(counter%40==20){
            set_pose(ctx,-950, 950, -100 - 150, 100 - 150, -100);
        }
    }else if(counter>timepoint[4] && counter<timepoint[5]){
        set_pose(ctx,-950, 950, -100, 100, 0);
    }else if(counter==timepoint[5]){
        clear_state(ctx,1);
    }
}

static void Swing(puppy_native_t *ctx) {
    float duration[] = {4.0};
    uint16_t timepoint[2] = {0};
    for(int i=1;i<2;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = counter*2.0*PI/TS;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 0;
        set_pose(ctx,-700, 700, -700, 700, 0);
    }else if(counter>timepoint[0] && counter<timepoint[1]){
        set_pose(ctx,-700 - 350*sin(1.4*phase), 700 + 350*sin(1.4*phase), -700 + 350*sin(1.4*phase), 700 - 350*sin(1.4*phase), 0);
	}else if(counter==timepoint[1]){
        clear_state(ctx,1);
    }
}

static void Rolling(puppy_native_t *ctx) {
    float duration[] = {6.0};
    uint16_t timepoint[2] = {0};
    for(int i=1;i<2;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = 0.5*counter*2.0*PI/TS;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
    }else if(counter>timepoint[0] && counter<timepoint[1]){
        ctx->speed = 0;
        set_pose(ctx,-700 - 370*sinf(phase), 700 - 370*sinf(phase), -700 + 370*sinf(phase), 700 + 370*sinf(phase), 0);
	}else if(counter==timepoint[1]){
        clear_state(ctx,1);
    }
}

static void Angry(puppy_native_t *ctx) {
    float duration[] = {0.5, 2.0};
    uint16_t timepoint[3] = {0};
    for(int i=1;i<3;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 0;
        set_pose(ctx,0, 0, -1000, 1000, 0);
    }
    else if(counter>timepoint[1] && counter<timepoint[2]){
        if(counter%20==0){
            set_pose(ctx,-100, -250, -1000, 1000, 0);
        }else if(counter%20==10){
            set_pose(ctx,250, 100, -1000, 1000, 0);
        }
	}else if(counter==timepoint[2]){
        clear_state(ctx,1);
    }
}

static void Swimming(puppy_native_t *ctx) {
    float duration[] = {6.0};
    uint16_t timepoint[2] = {0};
    for(int i=1;i<2;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = counter*2.0*PI/TS;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        set_pose(ctx,0, 0, 0, 0, 0);
    }else if(counter>timepoint[0] && counter<timepoint[1]){
        ctx->speed = 3000;
        set_pose(ctx,200 + 300*sin(0.5*phase),
                     -200 + 300*sin(0.5*phase),
                      100*sin(3.0*phase),
                      100*sin(3.0*phase),
                     -200*sin(0.5*phase));
	}else if(counter==timepoint[1]){
        clear_state(ctx,1);
    }
}

static void Pee(puppy_native_t *ctx) {
    float duration[] = {2.0, 1.0, 0.5, 1.0};
    uint16_t timepoint[5] = {0};
    for(int i=1;i<5;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 700;
        set_pose(ctx,0, 700, -150, 500, -300);
    }else if(counter>timepoint[1] && counter<timepoint[2]){
        ctx->speed = 0;
        if(counter%30==0){
            set_pose(ctx,0, 700, -50, 500, -300);
        }else if(counter%30==15){
            set_pose(ctx,0, 700, -250, 500, -300);
        }
	}else if(counter>timepoint[3] && counter<timepoint[4]){
        if(counter%30==0){
            set_pose(ctx,0, 700, -250, 500, -300);
        }else if(counter%30==15){
            set_pose(ctx,0, 700, -50, 500, -300);
        }
	}else if(counter==timepoint[4]){
        clear_state(ctx,1);
    }
}

static void Stretch(puppy_native_t *ctx) {
    float duration[] = {3.0, 1.5, 1.0};
    uint16_t timepoint[4] = {0};
    for(int i=1;i<4;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = counter*2.0*PI/TS;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 3000;
    }else if(counter>timepoint[0] && counter<timepoint[1]){
        set_pose(ctx,-400 + 2.2*counter, 400 - 2.2*counter, -750 - 1.8*counter, 750 + 1.8*counter, 0);
    }else if(counter>timepoint[1] && counter<timepoint[2]){
        set_pose(ctx,-900, 900, 100, -100, 0);
	}else if(counter>timepoint[2] && counter<timepoint[3]){
        ctx->speed = 5000;
        set_pose(ctx,-900 + 100*cos(phase+3*PI/4.0),
                       900 + 100*cos(phase+3*PI/4.0),
                       100,
                       -100,
                       100*cos(phase+PI/4.0));
    }else if(counter==timepoint[3]){
        clear_state(ctx,1);
    }
}

static void Bouncing(puppy_native_t *ctx) {
    float duration[] = {5.0};
    uint16_t timepoint[2] = {0};
    for(int i=1;i<2;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = counter*2.2*PI/TS;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
    }else if(counter>timepoint[0] && counter<timepoint[1]){
        ctx->speed = 0;
        set_pose(ctx,-500 - 450*sinf(phase),
                       500 + 450*sinf(phase),
                       -500 - 450*sinf(phase),
                       500 + 450*sinf(phase),
                       0);
	}else if(counter==timepoint[1]){
        clear_state(ctx,1);
    }
}

static void Shaking(puppy_native_t *ctx) {
    float duration[] = {5.0};
    uint16_t timepoint[2] = {0};
    for(int i=1;i<2;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = 0.9*counter*2.0*PI/TS;
    if(counter==timepoint[0]){
        clear_state(ctx,0);
    }else if(counter>timepoint[0] && counter<timepoint[1]){
        ctx->speed = 0;
        set_pose(ctx,-500 + 300*sinf(phase),
                       500 + 300*sinf(phase),
                      -500 + 300*sinf(phase),
                       500 + 300*sinf(phase),
                       300*sinf(phase));
    }else if(counter==timepoint[1]){
        clear_state(ctx,1);
    }
}

static void Sit(puppy_native_t *ctx) {
    float duration[] = {4.0, 0.5, 0.5, 0.5};
    uint16_t timepoint[5] = {0};
    for(int i=1;i<5;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 6000;
        set_pose(ctx,-1000, 1000, -1400, 1400, 0);
    }else if(counter>timepoint[1] && counter<timepoint[2]){
        ctx->speed = 4000;
        set_pose(ctx,0, 0, -1500, 1500, 0);
    }else if(counter>timepoint[2] && counter<timepoint[3]){
        set_pose(ctx,0, 0, 0, 1500, 0);
    }else if(counter>timepoint[3] && counter<timepoint[4]){
        set_pose(ctx,0, 0, 0, 0, 0);
    }else if(counter==timepoint[4]){
        clear_state(ctx,1);
    }
}

static void Scratch(puppy_native_t *ctx) {
    float duration[] = {1.5, 2.0, 0.5, 2.0, 1.0, 0.5};
    uint16_t timepoint[7] = {0};
    for(int i=1;i<7;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 6000;
        set_pose(ctx,-1000, 1000, -1400, 1400, 0);
    }else if(counter>timepoint[1] && counter<timepoint[2]){
        ctx->speed = 0;
        if(counter%40==0){
            set_pose(ctx,-550, 750, -1800, 1500, 100);
        }else if(counter%40==20){
            set_pose(ctx,-550, 750, -2100, 1500, -100);
        }
    }else if(counter>timepoint[2] && counter<timepoint[3]){
        ctx->speed = 3000;
        set_pose(ctx,-550, 750, -1500, 1500, 100);
    }else if(counter>timepoint[3] && counter<timepoint[4]){
        ctx->speed = 0;
        if(counter%40==0){
            set_pose(ctx,-550, 750, -1800, 1500, 100);
        }else if(counter%40==20){
            set_pose(ctx,-550, 750, -2100, 1500, -100);
        }
    }else if(counter>timepoint[4] && counter<timepoint[5]){
        ctx->speed = 4000;
        set_pose(ctx,0, 0, 0, 1400, 0);
    }
    else if(counter>timepoint[5] && counter<timepoint[6]){
        ctx->speed = 4000;
        set_pose(ctx,0, 0, 0, 0, 0);
    }
    else if(counter==timepoint[6]){
        clear_state(ctx,1);
    }
}

static void Hug(puppy_native_t *ctx) {
    float duration[] = {1.5, 2.0, 3.0, 0.5, 0.5, 0.5};
    uint16_t timepoint[7] = {0};
    for(int i=1;i<7;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    float phase = counter*2.0*PI/TS;
	if(counter==timepoint[0]){
		clear_state(ctx,0);
        ctx->speed = 6000;
        set_pose(ctx,-1000, 1000, -1400, 1400, 0);
    }else if(counter==timepoint[1]){
        ctx->speed = 800;
        set_pose(ctx,-600, 600, -1150, 1150, 0);
    }else if(counter>timepoint[2] && counter<timepoint[3]){
        ctx->speed = 1500;
        set_pose(ctx,-450 + 300*sinf(phase),
                       450 + 300*sinf(phase),
                       -1150,
                       1150,
                       200*sinf(phase));
    }else if(counter>timepoint[3] && counter<timepoint[4]){
        ctx->speed = 4500;
        set_pose(ctx,0, 0, -1700, 1700, 0);
    }else if(counter>timepoint[4] && counter<timepoint[5]){
        set_pose(ctx,0, 0, 0, 1700, 0);
    }else if(counter>timepoint[5] && counter<timepoint[6]){
        set_pose(ctx,0, 0, 0, 0, 0);
    }else if(counter==timepoint[6]){
        clear_state(ctx,1);
    }
}

static void Keep_Sit(puppy_native_t *ctx) {
    float duration[] = {4.0};
    uint16_t timepoint[2] = {0};
    timepoint[1] = duration[0]*TS;

    uint16_t counter = ctx->tick;
    if(counter==timepoint[0]){
        clear_state(ctx,0);
        ctx->speed = 6000;
        set_pose(ctx,-1000, 1000, -1400, 1400, 0);
    }else if(counter>timepoint[1]){
        // 保持坐姿
        ctx->speed = 4000;
        set_pose(ctx,-1000, 1000, -1400, 1400, 0);
    }
    // 不调用 clear_state(ctx,1)，一直保持坐姿
}

static void Sit_Reset(puppy_native_t *ctx) {
    float duration[] = {0.5, 0.5, 0.5};
    uint16_t timepoint[4] = {0};
    for(int i=1;i<4;i++){
        timepoint[i] = timepoint[i-1] + duration[i-1]*TS;
    }
    uint16_t counter = ctx->tick;
    if(counter==timepoint[0]){
        clear_state(ctx,0);
        ctx->speed = 4000;
        set_pose(ctx,0, 0, -1500, 1500, 0);
    }else if(counter>timepoint[0] && counter<timepoint[1]){
        set_pose(ctx,0, 0, 0, 1500, 0);
    }else if(counter>timepoint[1] && counter<timepoint[2]){
        set_pose(ctx,0, 0, 0, 0, 0);
    }else if(counter==timepoint[3]){
        clear_state(ctx,1);
    }
}
void puppy_native_tick(unsigned id,puppy_native_t *ctx) {
    switch(id) {
    case 1: Wave(ctx);break;
    case 2: Naughty(ctx);break;
    case 3: Lookup(ctx);break;
    case 4: Swing(ctx);break;
    case 5: Rolling(ctx);break;
    case 6: Angry(ctx);break;
    case 7: Swimming(ctx);break;
    case 8: Pee(ctx);break;
    case 9: Stretch(ctx);break;
    case 10: Bouncing(ctx);break;
    case 11: Shaking(ctx);break;
    case 12: Sit(ctx);break;
    case 13: Scratch(ctx);break;
    case 14: Hug(ctx);break;
    case 15: Keep_Sit(ctx);break;
    case 16: Sit_Reset(ctx);break;
    default:ctx->done=true;break;
    }
    if(id==15 && ctx->tick>=400)ctx->done=true; // finite job, keep seated pose
    ++ctx->tick;
}
