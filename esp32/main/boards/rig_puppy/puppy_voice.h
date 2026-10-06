#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
void puppy_voice_init(void);
bool puppy_voice_busy(void);
bool puppy_voice_recording(void);
bool puppy_voice_start(unsigned duration_ms,bool transmit);
const char *puppy_voice_phase(void);
// Called only after the start cue has been submitted to the media owner.
void puppy_voice_cue_ready(void);
void puppy_voice_send(void);
void puppy_voice_cancel(void);
cJSON *puppy_voice_status(void);
