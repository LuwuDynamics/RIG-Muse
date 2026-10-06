#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    unsigned tick, speed;
    int offsets[5];
    bool done;
} puppy_native_t;
void puppy_native_init(puppy_native_t *ctx);
void puppy_native_tick(unsigned id, puppy_native_t *ctx);
