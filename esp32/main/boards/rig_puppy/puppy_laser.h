#pragma once
#include <stdbool.h>
#include <stdint.h>
void puppy_laser_init(void);
bool puppy_laser_set(unsigned mode,unsigned duration_ms,int64_t now);
void puppy_laser_tick(int64_t now);
void puppy_laser_stop(void);
bool puppy_laser_ready(void);
bool puppy_laser_on(void);
unsigned puppy_laser_remaining(int64_t now);
