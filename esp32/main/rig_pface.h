#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rig_show.h"
// Stardust "particle face" for the 240x240 round screen. A fixed pool of
// particles forms the eyes, brows and mouth of the current face; the rest orbit
// as dust. Changing the face scatters the resting particles, then pulls every
// particle into the new face with staggered springs. Pure C with no ESP-IDF
// dependency (only the PSRAM allocator hook in rig_pface.c), so the host tests
// run the same code as the board. Call everything from one task.
typedef enum { RIG_PFACE_BURST, RIG_PFACE_VORTEX, RIG_PFACE_DUST } rig_pface_style_t;
typedef struct rig_pface rig_pface_t;
typedef struct {
    unsigned particles;       // pool size
    unsigned face_particles;  // particles owned by the face (the rest are dust)
    unsigned targets;         // targets of the current face
    unsigned assembled;       // face particles within 14 px of their animated target
    float max_radius;         // farthest particle from the screen centre
    bool finite;              // false if any particle state went NaN/inf
    size_t bytes;             // heap used by the engine
} rig_pface_stats_t;
// Builds every face and the particle pool. NULL if memory runs out; the caller
// then keeps the original renderer. Boots as dust that condenses into FACE_IDLE.
rig_pface_t *rig_pface_create(rig_pface_style_t style, uint32_t seed);
void rig_pface_destroy(rig_pface_t *pf);
// A face that is already current is ignored; faces >= FACE_COUNT too.
void rig_pface_set_face(rig_pface_t *pf, rig_face_t face);
rig_face_t rig_pface_face(const rig_pface_t *pf);
void rig_pface_set_connected(rig_pface_t *pf, bool connected);
// Fixed-step physics. The board calls it with 1/30 s; dt is clamped to 0.1 s.
void rig_pface_step(rig_pface_t *pf, float dt);
// RGB565 in wire byte order, like rig_face_render(). Strips render independently
// and join seamlessly (no frame buffer needed). Out-of-range arguments do nothing.
void rig_pface_render(const rig_pface_t *pf, uint16_t *pixels, int y, int rows);
rig_pface_stats_t rig_pface_stats(const rig_pface_t *pf);
