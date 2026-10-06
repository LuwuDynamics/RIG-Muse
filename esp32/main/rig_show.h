#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
// The first six are the original faces; keep their values. The rest exist only
// in the particle face (rig_pface.h). rig_face_render() draws them as FACE_IDLE.
typedef enum {
    FACE_IDLE, FACE_HAPPY, FACE_SLEEP, FACE_CURIOUS, FACE_SURPRISE, FACE_SHY,
    FACE_SAD, FACE_ANGRY, FACE_LAUGH, FACE_THINK, FACE_LOVE, FACE_WINK,
    FACE_COUNT
} rig_face_t;
typedef enum { SOUND_NONE, SOUND_CHIRP, SOUND_SNORE, SOUND_SPARKLE, SOUND_QUESTION, SOUND_BOOP, SOUND_RECORD, SOUND_SEND } rig_sound_t;
typedef struct {
    const char *name;
    const char *description;
    rig_face_t face;
    rig_sound_t sound;
    unsigned duration_ms;
    bool motion;
} rig_show_t;
extern const rig_show_t rig_shows[];
extern const size_t rig_show_count;
const rig_show_t *rig_show_find(const char *name);
// RGB565 in wire byte order, independently renderable strips.
void rig_face_render(uint16_t *pixels, int y, int rows, rig_face_t face, uint32_t ms, bool connected);
// Original procedural effects, mono 16 kHz. Bounded to +/- 5000 (16-bit PCM).
int16_t rig_sound_sample(rig_sound_t sound, uint32_t sample);
