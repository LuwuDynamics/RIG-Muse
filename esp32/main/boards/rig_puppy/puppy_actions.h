#pragma once
#include "rig_show.h"
#include "puppy_native.h"
#define PUPPY_ACTION_COUNT 16
#define PUPPY_ACTION_SETTLE_MS 3000
#define PUPPY_ACTION_TIMEOUT_MS 15000
// Native formulas run on Omni's two action ticks per five 5 ms control cycles.
typedef struct {
    unsigned count,id,duration_ms;
    int forward,turn;
    double scale[5];
    bool scaled,returns_standing;
} puppy_action_plan_t;
unsigned puppy_action_id(const char *name);
unsigned puppy_action_tick_ms(unsigned tick);
unsigned puppy_action_nominal_ms(unsigned id);
bool puppy_action_plan(const char *name,const int zero[5],puppy_action_plan_t *plan);
bool puppy_action_sample(const puppy_action_plan_t *plan,const int zero[5],
                         puppy_native_t *ctx,int target[5],unsigned *speed);

bool puppy_move_plan(int forward,int turn,unsigned duration_ms,const int zero[5],puppy_action_plan_t *plan);
unsigned puppy_action_due_ms(const puppy_action_plan_t *plan,unsigned tick);
