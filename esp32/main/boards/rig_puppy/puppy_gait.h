#pragma once
#include <stdbool.h>
// Pure port of RIG-Omni Puppy SetDogSpeed/move; elapsed time advances 5 ms ticks.
bool puppy_gait_target(int vx,int vyaw,unsigned tick,const int zero[5],int target[5]);
