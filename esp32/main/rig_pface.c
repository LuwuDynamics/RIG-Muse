// Particle face engine, see rig_pface.h. Distances are pixels on the 240x240
// screen (origin top-left, +y down); faces are authored relative to the centre.
// Ported from the rig-particle-face HTML prototype; keep the two in step.
#include "rig_pface.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define CX 120.0f
#define CY 120.0f
#define PI_F 3.14159265f
#define TAU_F 6.28318531f
#define SPACING 4.4f        // dot pitch of filled shapes
#define POLY_MAX 128        // vertices of one authored shape
#define RS_MAX 192          // resampled points of one stroke or outline
#define TARGETS_MAX 400     // targets of one face
#define POOL_FACTOR 1.45f   // pool = largest face * this, so about a third is dust
#define LUT_N 96
#define SIN_N 512       // sin() table size
#define EASE_N 32       // table size of the gather ease curve s^2.2
#define K_HOLD 520.0f       // spring stiffness of a settled particle
#define ZETA 0.82f
#define VMAX 560.0f
#define FLOW_A 110.0f
#define TIME_WRAP 4096.0f   // keeps float trig accurate after days of uptime
enum { F_KICK = 1, F_DRIFT = 2, F_REL = 4 };
// Roles group the particles that blink, look around or animate together.
enum { R_DUST, R_EYE_L, R_EYE_R, R_BROW_L, R_BROW_R, R_MOUTH, R_PUPIL_L, R_PUPIL_R, R_BLUSH,
       R_TEAR, R_DOT0, R_DOT1, R_DOT2, R_Z0, R_Z1, R_Z2, R_H0, R_H1, R_SPARK, R_COUNT };

typedef struct { float x, y; } v2;
typedef struct { float x, y; uint8_t w, role, accent; } target_t;
typedef struct { float tx, ty, sx, sy, rot, a, c, s; } xf_t;
typedef struct {
    float x, y, vx, vy, bx, by, rel, gat, lvl, flash, bb, cr, cg, cb;
    float dang, dr, drt, ph, f1, f2, tw, dustw, dom;
    uint8_t tc[3], role, w, flags;
} particle_t;
// What the renderer needs from a particle, so drawing never touches the (PSRAM) particle array.
typedef struct { float x, y, tx, ty; uint8_t r, g, b, dust, ghosts, show; } draw_t;   // tx,ty: step between ghost dots
typedef struct { target_t *t; unsigned n; float pivot[R_COUNT][2]; } expr_t;
// How resting particles are scattered and how everyone is pulled back.
typedef struct {
    float radial, tang, up, noise, vmin, vmax, drag, flow, gat_min, gat_max, tg, k0, swirl, wave;
} style_t;
static const style_t styles[] = {
    [RIG_PFACE_BURST]  = {1.0f, 0.25f, 0.0f,  0.45f, 110, 260, 3.0f, 0.5f, 0.26f, 0.46f, 0.50f, 16, 0.0f, 0.10f},
    [RIG_PFACE_VORTEX] = {0.3f, 1.0f,  0.0f,  0.30f, 70,  170, 1.8f, 0.8f, 0.30f, 0.54f, 0.56f, 10, 1.0f, 0.14f},
    [RIG_PFACE_DUST]   = {0.2f, 0.2f, -0.9f,  1.00f, 35,  100, 1.1f, 1.4f, 0.42f, 0.82f, 0.70f, 6,  0.3f, 0.20f},
};
// Per-face look: colours, how the eyes move, how it blinks.
typedef struct {
    uint8_t color[3], accent[3];
    float gaze, gaze_x, gaze_y, scan_hz, shimmer, bright, breath;
    float blink_gap0, blink_gap1, blink_close, blink_hold, blink_open, blink_depth;
    uint32_t blink_roles;
} face_cfg_t;
#define BOTH ((1u << R_EYE_L) | (1u << R_EYE_R))
#define BLINK_STD 2.6f, 5.5f, .07f, .04f, .12f, .92f   // gap min/max, close, hold, open (s), depth
static const face_cfg_t face_cfg[FACE_COUNT] = {
    [FACE_IDLE]     = {{104, 228, 255}, {170, 240, 255}, 1.0f, 0, 0, 0, .5f, 1.0f, 1.0f, BLINK_STD, BOTH},
    [FACE_HAPPY]    = {{110, 255, 205}, {255, 140, 175}, .35f, 0, 0, 0, .5f, 1.0f, 1.0f, BLINK_STD, 0},
    [FACE_SLEEP]    = {{140, 150, 238}, {186, 196, 255}, .15f, 0, 0, 0, .35f, .85f, 2.2f,
                       2.5f, 5.0f, .7f, .5f, .12f, .97f, BOTH},
    [FACE_CURIOUS]  = {{96, 236, 214},  {190, 255, 240}, 1.3f, 0, 0, .24f, .5f, 1.0f, 1.0f, BLINK_STD, BOTH},
    [FACE_SURPRISE] = {{190, 240, 255}, {255, 245, 190}, .8f, 0, 0, 0, .5f, 1.0f, 1.0f,
                       4.0f, 8.0f, .07f, .04f, .12f, .92f, BOTH},
    [FACE_SHY]      = {{255, 176, 206}, {255, 96, 140},  .3f, 0, 0, 0, .5f, 1.0f, 1.0f, BLINK_STD, 0},
    [FACE_SAD]      = {{118, 156, 255}, {170, 225, 255}, .6f, 0, 0, 0, .45f, .95f, 1.0f, BLINK_STD, BOTH},
    [FACE_ANGRY]    = {{255, 96, 74},   {255, 176, 90},  .5f, 0, 0, 0, .9f, 1.05f, 1.0f, BLINK_STD, BOTH},
    [FACE_LAUGH]    = {{255, 224, 112}, {255, 130, 150}, .2f, 0, 0, 0, .7f, 1.0f, 1.0f, BLINK_STD, 0},
    [FACE_THINK]    = {{182, 146, 255}, {120, 232, 255}, 1.3f, .8f, -.8f, 0, .5f, 1.0f, 1.0f, BLINK_STD, BOTH},
    [FACE_LOVE]     = {{255, 100, 170}, {255, 172, 206}, .5f, 0, 0, 0, .5f, 1.0f, 1.0f, BLINK_STD, 0},
    [FACE_WINK]     = {{124, 255, 226}, {255, 240, 160}, .6f, 0, 0, 0, .5f, 1.0f, 1.0f, BLINK_STD, 1u << R_EYE_L},
};

struct rig_pface {
    const style_t *st;
    uint32_t rng;
    particle_t *p;
    unsigned n;
    expr_t expr[FACE_COUNT];
    rig_face_t face;
    float time, tin, scan_y;              // tin: seconds since the last face change
    struct { float x, y, tx, ty, t; } gz;
    struct { float t, next, amt; } bl;
    xf_t xf[R_COUNT];
    struct { float s, tx, ty, a; } g;
    bool connected;
    float status[3];
    uint8_t lut_face[LUT_N], lut_dust[LUT_N], lut_dot[LUT_N];
    float sin_tab[SIN_N + 1], ease[EASE_N + 1];   // libm is slow on the S3's single-precision FPU, so hot paths use tables
    draw_t *dl;                                   // one entry per particle, refreshed by every step
    uint8_t *used;
    uint16_t *order;
    size_t bytes;
};

static void *pf_calloc(size_t size) {
#ifdef ESP_PLATFORM
    void *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(1, size, MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}
static void pf_free(void *p) {
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    free(p);
#endif
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smoothf(float t) { return t * t * (3.0f - 2.0f * t); }
static float fsin(const float *tab, float x) {          // table sine, error below 2e-5
    float t = x * (1.0f / TAU_F);
    if (!(t > -1.0e6f && t < 1.0e6f)) return 0.0f;       // also catches NaN
    t -= (float)(int)t;
    if (t < 0.0f) t += 1.0f;
    t *= (float)SIN_N;
    int i = (int)t;
    float f = t - (float)i;
    if (i >= SIN_N) { i = SIN_N - 1; f = 1.0f; }
    return tab[i] + (tab[i + 1] - tab[i]) * f;
}
static float fcos(const float *tab, float x) { return fsin(tab, x + 0.25f * TAU_F); }
static float ease_at(const float *tab, float s) {         // s in [0,1)
    float u = s * (float)EASE_N;
    int i = (int)u;
    float f = u - (float)i;
    if (i >= EASE_N) { i = EASE_N - 1; f = 1.0f; }
    return tab[i] + (tab[i + 1] - tab[i]) * f;
}
// mulberry32
static uint32_t pf_next(uint32_t *s) {
    uint32_t t = (*s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}
static float pf_rand(uint32_t *s) { return (float)(pf_next(s) >> 8) * (1.0f / 16777216.0f); }

// ---- shapes: authored as polygons / polylines relative to the screen centre ----
static int g_ellipse(v2 *o, float cx, float cy, float rx, float ry, float rot, int seg) {
    float c = cosf(rot), s = sinf(rot);
    for (int i = 0; i < seg; ++i) {
        float a = (float)i / (float)seg * TAU_F, x = cosf(a) * rx, y = sinf(a) * ry;
        o[i].x = cx + x * c - y * s;
        o[i].y = cy + x * s + y * c;
    }
    return seg;
}
static int g_arc(v2 *o, float cx, float cy, float rx, float ry, float a0, float a1, int seg) {
    for (int i = 0; i <= seg; ++i) {            // angle 0 = +x, pi/2 = down
        float a = a0 + (a1 - a0) * (float)i / (float)seg;
        o[i].x = cx + cosf(a) * rx;
        o[i].y = cy + sinf(a) * ry;
    }
    return seg + 1;
}
static int g_quad(v2 *o, float x0, float y0, float x1, float y1, float x2, float y2, int seg) {
    for (int i = 0; i <= seg; ++i) {
        float t = (float)i / (float)seg, u = 1.0f - t;
        o[i].x = u * u * x0 + 2.0f * u * t * x1 + t * t * x2;
        o[i].y = u * u * y0 + 2.0f * u * t * y1 + t * t * y2;
    }
    return seg + 1;
}
static int g_line(v2 *o, float x0, float y0, float x1, float y1) {
    o[0] = (v2){x0, y0};
    o[1] = (v2){x1, y1};
    return 2;
}
static int g_heart(v2 *o, float cx, float cy, float size, int seg) {
    float k = size / 32.0f;
    for (int i = 0; i < seg; ++i) {
        float t = (float)i / (float)seg * TAU_F, s = sinf(t);
        float x = 16.0f * s * s * s;
        float y = -(13.0f * cosf(t) - 5.0f * cosf(2.0f * t) - 2.0f * cosf(3.0f * t) - cosf(4.0f * t));
        o[i].x = cx + x * k;
        o[i].y = cy + (y - 2.5f) * k;
    }
    return seg;
}
static int g_star4(v2 *o, float cx, float cy, float big, float small) {
    for (int i = 0; i < 8; ++i) {
        float a = -PI_F / 2.0f + (float)i * PI_F / 4.0f, r = (i & 1) ? small : big;
        o[i].x = cx + cosf(a) * r;
        o[i].y = cy + sinf(a) * r;
    }
    return 8;
}
static int g_drop(v2 *o, float cx, float cy, float a, int seg) {   // teardrop, tip up
    for (int i = 0; i < seg; ++i) {
        float t = (float)i / (float)seg * TAU_F;
        o[i].x = cx + a * 0.9f * sinf(t) * powf(sinf(t * 0.5f), 1.4f);
        o[i].y = cy - a * cosf(t);
    }
    return seg;
}
static int g_zig(v2 *o, float cx, float cy, float w, float amp, int n) {
    for (int i = 0; i <= n; ++i) {
        o[i].x = cx - w * 0.5f + w * (float)i / (float)n;
        o[i].y = cy + ((i & 1) ? amp : -amp);
    }
    return n + 1;
}
static int g_glyph_z(v2 *o, float cx, float cy, float size) {
    float h = size * 0.5f;
    o[0] = (v2){cx - h, cy - h};
    o[1] = (v2){cx + h, cy - h};
    o[2] = (v2){cx - h, cy + h};
    o[3] = (v2){cx + h, cy + h};
    return 4;
}
static int g_clip(const v2 *in, int n, v2 *out, float nx, float ny, float d) {   // keep n.p <= d
    int m = 0;
    for (int i = 0; i < n; ++i) {
        v2 a = in[i], b = in[(i + 1) % n];
        float da = nx * a.x + ny * a.y - d, db = nx * b.x + ny * b.y - d;
        if (da <= 0.0f) out[m++] = a;
        if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f)) {
            float t = da / (da - db);
            out[m++] = (v2){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        }
    }
    return m;
}
// Eyelid: keep what is below a line through (cx, cy+off). deg > 0 slopes down to the right.
static int g_lid(const v2 *in, int n, v2 *out, float cx, float cy, float deg, float off) {
    float t = deg * PI_F / 180.0f, nx = sinf(t), ny = -cosf(t);
    return g_clip(in, n, out, nx, ny, nx * cx + ny * (cy + off));
}

// ---- sampler: shapes -> particle targets ----
typedef struct {
    float S;
    uint32_t rng;
    target_t t[TARGETS_MAX];
    int n;
    bool overflow;
    v2 a[POLY_MAX], b[POLY_MAX];
    struct { float x, y, tx, ty; } rs[RS_MAX];
    float cum[POLY_MAX + 1];
} build_t;

static bool pip(const v2 *poly, int n, float x, float y) {
    bool c = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = poly[i].x, yi = poly[i].y, xj = poly[j].x, yj = poly[j].y;
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) c = !c;
    }
    return c;
}
static float edge_d2(const v2 *poly, int n, float x, float y) {   // squared distance to the outline
    float m = 1e18f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float ax = poly[j].x, ay = poly[j].y, dx = poly[i].x - ax, dy = poly[i].y - ay;
        float l = dx * dx + dy * dy, t = 0.0f;
        if (l > 0.0f) t = clampf(((x - ax) * dx + (y - ay) * dy) / l, 0.0f, 1.0f);
        float ex = ax + dx * t - x, ey = ay + dy * t - y, d = ex * ex + ey * ey;
        if (d < m) m = d;
    }
    return m;
}
static int resample(build_t *b, const v2 *poly, int n, float step, bool closed) {
    int m = closed ? n + 1 : n;
    float *L = b->cum;
    L[0] = 0.0f;
    for (int i = 1; i < m; ++i) {
        const v2 *p0 = &poly[i - 1], *p1 = &poly[i == n ? 0 : i];
        L[i] = L[i - 1] + sqrtf((p1->x - p0->x) * (p1->x - p0->x) + (p1->y - p0->y) * (p1->y - p0->y));
    }
    float total = L[m - 1];
    int segs = (int)floorf(total / step + 0.5f), min_segs = closed ? 3 : 1;
    if (segs < min_segs) segs = min_segs;
    int cnt = closed ? segs : segs + 1;
    if (cnt > RS_MAX) { cnt = RS_MAX; segs = closed ? cnt : cnt - 1; }
    int j = 0;
    for (int i = 0; i < cnt; ++i) {
        float d = (float)i * total / (float)segs;
        while (j < m - 2 && L[j + 1] < d) ++j;
        const v2 *p0 = &poly[j], *p1 = &poly[j + 1 == n ? 0 : j + 1];
        float len = L[j + 1] - L[j], t = (d - L[j]) / (len > 0.0f ? len : 1.0f);
        float tx = p1->x - p0->x, ty = p1->y - p0->y, tl = sqrtf(tx * tx + ty * ty);
        if (tl <= 0.0f) tl = 1.0f;
        b->rs[i].x = lerpf(p0->x, p1->x, t);
        b->rs[i].y = lerpf(p0->y, p1->y, t);
        b->rs[i].tx = tx / tl;
        b->rs[i].ty = ty / tl;
    }
    return cnt;
}
static void put(build_t *b, float x, float y, float w, int role, bool accent) {
    if (b->n >= TARGETS_MAX) { b->overflow = true; return; }
    b->t[b->n++] = (target_t){x, y, (uint8_t)(clampf(w, 0.0f, 1.0f) * 255.0f + 0.5f), (uint8_t)role, (uint8_t)accent};
}
static float jit(build_t *b) { return (pf_rand(&b->rng) - 0.5f) * b->S * 0.2f; }
// Bright rim plus a dimmer hex grid inside: reads like beads of light.
static void fill(build_t *b, int role, bool accent, float w, const v2 *poly, int n) {
    const float S = b->S;
    int cnt = resample(b, poly, n, S * 0.82f, true);
    for (int i = 0; i < cnt; ++i) {
        float jx = jit(b) * 0.5f, jy = jit(b) * 0.5f;
        put(b, b->rs[i].x + jx, b->rs[i].y + jy, w, role, accent);
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; ++i) {
        x0 = fminf(x0, poly[i].x); x1 = fmaxf(x1, poly[i].x);
        y0 = fminf(y0, poly[i].y); y1 = fmaxf(y1, poly[i].y);
    }
    const float dy = S * 0.866f, edge2 = (S * 0.72f) * (S * 0.72f);
    int row = 0, inside = 0;
    for (float y = y0 + dy * 0.5f; y < y1; y += dy, ++row) {
        for (float x = x0 + ((row & 1) ? S * 0.5f : 0.0f) + S * 0.25f; x < x1; x += S) {
            float jx = x + (pf_rand(&b->rng) - 0.5f) * S * 0.28f;
            float jy = y + (pf_rand(&b->rng) - 0.5f) * S * 0.28f;
            if (!pip(poly, n, jx, jy) || edge_d2(poly, n, jx, jy) < edge2) continue;
            put(b, jx, jy, w * 0.62f, role, accent);
            ++inside;
        }
    }
    if (!inside) {                              // a very small dot: add its centre, not a hollow ring
        float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
        if (pip(poly, n, cx, cy) && edge_d2(poly, n, cx, cy) > (S * 0.5f) * (S * 0.5f)) put(b, cx, cy, w, role, accent);
    }
}
static void stroke(build_t *b, int role, bool accent, float w, const v2 *poly, int n, int rows, bool closed) {
    const float step = b->S * 0.82f, gap = b->S * 0.78f;
    int cnt = resample(b, poly, n, step, closed);
    for (int r = 0; r < rows; ++r) {
        float off = ((float)r - (float)(rows - 1) * 0.5f) * gap, stag = (r & 1) ? step * 0.5f : 0.0f;
        for (int i = 0; i < cnt; ++i) {
            float jx = jit(b), jy = jit(b);
            float x = b->rs[i].x - b->rs[i].ty * off + b->rs[i].tx * stag + jx;
            float y = b->rs[i].y + b->rs[i].tx * off + b->rs[i].ty * stag + jy;
            put(b, x, y, (rows == 1 || fabsf(off) < 1e-6f) ? w : w * 0.84f, role, accent);
        }
    }
}
// Drops targets (added since `from`) that fall inside `poly` or within `pad` of it.
static void except(build_t *b, int from, const v2 *poly, int n, float pad) {
    int k = from;
    for (int i = from; i < b->n; ++i) {
        float x = b->t[i].x, y = b->t[i].y;
        if (pip(poly, n, x, y) || edge_d2(poly, n, x, y) < pad * pad) continue;
        b->t[k++] = b->t[i];
    }
    b->n = k;
}

// ---- the faces: eyes, brows, mouth and effects ----
#define EXX 48.0f
#define EYY -10.0f
static void b_idle(build_t *b) {
    int n = g_ellipse(b->a, -EXX, EYY, 15, 24, 0, 56); fill(b, R_EYE_L, 0, 1, b->a, n);
    n = g_ellipse(b->a, EXX, EYY, 15, 24, 0, 56);      fill(b, R_EYE_R, 0, 1, b->a, n);
    n = g_quad(b->a, -19, 46, 0, 56, 19, 46, 28);      stroke(b, R_MOUTH, 0, 1, b->a, n, 2, false);
}
static void b_happy(build_t *b) {
    int n = g_arc(b->a, -EXX, -2, 16, 14, PI_F, TAU_F, 36); stroke(b, R_EYE_L, 0, 1, b->a, n, 2, false);
    n = g_arc(b->a, EXX, -2, 16, 14, PI_F, TAU_F, 36);      stroke(b, R_EYE_R, 0, 1, b->a, n, 2, false);
    n = g_quad(b->a, -27, 38, 0, 68, 27, 38, 28);           stroke(b, R_MOUTH, 0, 1, b->a, n, 2, false);
    n = g_ellipse(b->a, -70, 30, 10, 6, 0, 56);             fill(b, R_BLUSH, 1, 0.75f, b->a, n);
    n = g_ellipse(b->a, 70, 30, 10, 6, 0, 56);              fill(b, R_BLUSH, 1, 0.75f, b->a, n);
}
static void b_laugh(build_t *b) {
    int n = g_arc(b->a, -EXX, -2, 17, 15, PI_F, TAU_F, 36); stroke(b, R_EYE_L, 0, 1, b->a, n, 3, false);
    n = g_arc(b->a, EXX, -2, 17, 15, PI_F, TAU_F, 36);      stroke(b, R_EYE_R, 0, 1, b->a, n, 3, false);
    n = g_arc(b->a, 0, 34, 30, 26, 0, PI_F, 40);            // open D-shaped mouth, tongue cut out of it
    int nt = g_ellipse(b->b, 0, 53, 12, 6.5f, 0, 56), from = b->n;
    fill(b, R_MOUTH, 0, 1, b->a, n);
    except(b, from, b->b, nt, 1.5f);
    fill(b, R_MOUTH, 1, 1, b->b, nt);
    n = g_ellipse(b->a, -72, 28, 10, 6, 0, 56);             fill(b, R_BLUSH, 1, 0.7f, b->a, n);
    n = g_ellipse(b->a, 72, 28, 10, 6, 0, 56);              fill(b, R_BLUSH, 1, 0.7f, b->a, n);
}
static void b_sad(build_t *b) {
    int n = g_ellipse(b->b, -EXX, -6, 15, 23, 0, 56), m = g_lid(b->b, n, b->a, -EXX, -6, -20, -8);
    fill(b, R_EYE_L, 0, 1, b->a, m);
    n = g_ellipse(b->b, EXX, -6, 15, 23, 0, 56);       m = g_lid(b->b, n, b->a, EXX, -6, 20, -8);
    fill(b, R_EYE_R, 0, 1, b->a, m);
    n = g_line(b->a, -68, -38, -30, -52);              stroke(b, R_BROW_L, 0, 1, b->a, n, 2, false);
    n = g_line(b->a, 30, -52, 68, -38);                stroke(b, R_BROW_R, 0, 1, b->a, n, 2, false);
    n = g_quad(b->a, -18, 58, 0, 46, 18, 58, 28);      stroke(b, R_MOUTH, 0, 1, b->a, n, 2, false);
    n = g_drop(b->a, -58, 26, 8, 40);                  fill(b, R_TEAR, 1, 1, b->a, n);
}
static void b_angry(build_t *b) {
    int n = g_ellipse(b->b, -EXX, -8, 16, 22, 0, 56), m = g_lid(b->b, n, b->a, -EXX, -8, 26, -6);
    fill(b, R_EYE_L, 0, 1, b->a, m);
    n = g_ellipse(b->b, EXX, -8, 16, 22, 0, 56);       m = g_lid(b->b, n, b->a, EXX, -8, -26, -6);
    fill(b, R_EYE_R, 0, 1, b->a, m);
    n = g_line(b->a, -70, -36, -28, -16);              stroke(b, R_BROW_L, 0, 1, b->a, n, 2, false);
    n = g_line(b->a, 28, -16, 70, -36);                stroke(b, R_BROW_R, 0, 1, b->a, n, 2, false);
    n = g_zig(b->a, 0, 56, 48, 3.5f, 6);               stroke(b, R_MOUTH, 0, 1, b->a, n, 1, false);
}
static void b_surprise(build_t *b) {
    int n = g_ellipse(b->a, -EXX, -10, 20, 20, 0, 56); stroke(b, R_EYE_L, 0, 1, b->a, n, 2, true);
    n = g_ellipse(b->a, EXX, -10, 20, 20, 0, 56);      stroke(b, R_EYE_R, 0, 1, b->a, n, 2, true);
    n = g_ellipse(b->a, -EXX, -10, 6, 6, 0, 24);       fill(b, R_PUPIL_L, 0, 1, b->a, n);
    n = g_ellipse(b->a, EXX, -10, 6, 6, 0, 24);        fill(b, R_PUPIL_R, 0, 1, b->a, n);
    n = g_quad(b->a, -64, -48, -48, -62, -32, -48, 28); stroke(b, R_BROW_L, 0, 1, b->a, n, 1, false);
    n = g_quad(b->a, 32, -48, 48, -62, 64, -48, 28);   stroke(b, R_BROW_R, 0, 1, b->a, n, 1, false);
    n = g_ellipse(b->a, 0, 52, 10, 14, 0, 56);         stroke(b, R_MOUTH, 0, 1, b->a, n, 2, true);
}
static void b_think(build_t *b) {
    int n = g_ellipse(b->a, -EXX, -8, 14, 21, 0, 56);  fill(b, R_EYE_L, 0, 1, b->a, n);
    n = g_ellipse(b->a, EXX, -8, 14, 21, 0, 56);       fill(b, R_EYE_R, 0, 1, b->a, n);
    n = g_line(b->a, -66, -42, -32, -44);              stroke(b, R_BROW_L, 0, 1, b->a, n, 2, false);
    n = g_quad(b->a, 32, -50, 50, -66, 68, -52, 28);   stroke(b, R_BROW_R, 0, 1, b->a, n, 2, false);
    for (int i = 0; i < 3; ++i) {                      // the "..." that hops while Muse thinks
        n = g_ellipse(b->a, -16.0f + 16.0f * (float)i, 52, 4.6f, 4.6f, 0, 20);
        fill(b, R_DOT0 + i, 1, 1, b->a, n);
    }
}
static void b_sleep(build_t *b) {
    int n = g_ellipse(b->b, -EXX, -6, 16, 16, 0, 56), m = g_lid(b->b, n, b->a, -EXX, -6, -7, 0);
    fill(b, R_EYE_L, 0, 1, b->a, m);
    n = g_ellipse(b->b, EXX, -6, 16, 16, 0, 56);       m = g_lid(b->b, n, b->a, EXX, -6, 7, 0);
    fill(b, R_EYE_R, 0, 1, b->a, m);
    n = g_ellipse(b->a, 0, 55, 6, 8, 0, 56);           stroke(b, R_MOUTH, 0, 1, b->a, n, 1, true);
    n = g_glyph_z(b->a, 46, -28, 10);                  stroke(b, R_Z0, 1, 1, b->a, n, 1, false);   // Zzz grows as it floats up
    n = g_glyph_z(b->a, 62, -44, 13);                  stroke(b, R_Z1, 1, 1, b->a, n, 1, false);
    n = g_glyph_z(b->a, 79, -62, 16);                  stroke(b, R_Z2, 1, 1, b->a, n, 1, false);
}
static void b_love(build_t *b) {
    int n = g_heart(b->a, -EXX, -8, 40, 72);           fill(b, R_EYE_L, 0, 1, b->a, n);
    n = g_heart(b->a, EXX, -8, 40, 72);                fill(b, R_EYE_R, 0, 1, b->a, n);
    n = g_quad(b->a, -15, 46, 0, 58, 15, 46, 28);      stroke(b, R_MOUTH, 0, 1, b->a, n, 2, false);
    n = g_ellipse(b->a, -72, 32, 9, 5.5f, 0, 56);      fill(b, R_BLUSH, 1, 0.7f, b->a, n);
    n = g_ellipse(b->a, 72, 32, 9, 5.5f, 0, 56);       fill(b, R_BLUSH, 1, 0.7f, b->a, n);
    n = g_heart(b->a, -84, 24, 13, 72);                fill(b, R_H0, 1, 1, b->a, n);
    n = g_heart(b->a, 84, 16, 11, 72);                 fill(b, R_H1, 1, 1, b->a, n);
}
static void b_wink(build_t *b) {
    int n = g_ellipse(b->a, -EXX, EYY, 15, 24, 0, 56); fill(b, R_EYE_L, 0, 1, b->a, n);
    n = g_arc(b->a, EXX, 0, 16, 14, PI_F, TAU_F, 36);  stroke(b, R_EYE_R, 0, 1, b->a, n, 2, false);
    n = g_quad(b->a, -18, 46, 8, 62, 26, 40, 28);      stroke(b, R_MOUTH, 0, 1, b->a, n, 2, false);
    n = g_star4(b->a, 82, -40, 12, 3.2f);              fill(b, R_SPARK, 1, 1, b->a, n);
}
// Listening: one eye wider than the other, one brow up, eyes sweeping side to side.
static void b_curious(build_t *b) {
    int n = g_ellipse(b->a, -EXX, EYY, 15, 24, 0, 56); fill(b, R_EYE_L, 0, 1, b->a, n);
    n = g_ellipse(b->a, EXX, -6, 14, 17, 0, 56);       fill(b, R_EYE_R, 0, 1, b->a, n);
    n = g_quad(b->a, -68, -42, -50, -54, -32, -42, 28); stroke(b, R_BROW_L, 0, 1, b->a, n, 1, false);
    n = g_quad(b->a, -10, 50, 4, 55, 16, 47, 28);      stroke(b, R_MOUTH, 0, 1, b->a, n, 2, false);
}
// Handled or waiting: squinting arcs, big blush, a small shy smile.
static void b_shy(build_t *b) {
    int n = g_arc(b->a, -EXX, 4, 13, 11, PI_F, TAU_F, 36); stroke(b, R_EYE_L, 0, 1, b->a, n, 2, false);
    n = g_arc(b->a, EXX, 4, 13, 11, PI_F, TAU_F, 36);      stroke(b, R_EYE_R, 0, 1, b->a, n, 2, false);
    n = g_quad(b->a, -12, 48, 0, 55, 12, 48, 28);          stroke(b, R_MOUTH, 0, 1, b->a, n, 2, false);
    n = g_ellipse(b->a, -66, 28, 12, 7, 0, 56);            fill(b, R_BLUSH, 1, 0.9f, b->a, n);
    n = g_ellipse(b->a, 66, 28, 12, 7, 0, 56);             fill(b, R_BLUSH, 1, 0.9f, b->a, n);
}
static void (*const builders[FACE_COUNT])(build_t *) = {
    [FACE_IDLE] = b_idle, [FACE_HAPPY] = b_happy, [FACE_SLEEP] = b_sleep, [FACE_CURIOUS] = b_curious,
    [FACE_SURPRISE] = b_surprise, [FACE_SHY] = b_shy, [FACE_SAD] = b_sad, [FACE_ANGRY] = b_angry,
    [FACE_LAUGH] = b_laugh, [FACE_THINK] = b_think, [FACE_LOVE] = b_love, [FACE_WINK] = b_wink,
};

static bool build_face(rig_pface_t *pf, build_t *b, int face) {
    b->n = 0;
    b->overflow = false;
    b->rng = 0x9E3779B9u * (uint32_t)(face + 1);
    builders[face](b);
    if (b->overflow || b->n == 0) return false;
    expr_t *e = &pf->expr[face];
    e->t = pf_calloc((size_t)b->n * sizeof *e->t);
    if (!e->t) return false;
    e->n = (unsigned)b->n;
    float sum[R_COUNT][2] = {{0}};
    int cnt[R_COUNT] = {0};
    for (int i = 0; i < b->n; ++i) {
        e->t[i] = b->t[i];
        e->t[i].x += CX;
        e->t[i].y += CY;
        sum[b->t[i].role][0] += e->t[i].x;
        sum[b->t[i].role][1] += e->t[i].y;
        ++cnt[b->t[i].role];
    }
    for (int r = 0; r < R_COUNT; ++r) {
        if (!cnt[r]) continue;
        e->pivot[r][0] = sum[r][0] / (float)cnt[r];
        e->pivot[r][1] = sum[r][1] / (float)cnt[r];
    }
    pf->bytes += (size_t)b->n * sizeof *e->t;
    return true;
}

// ---- assignment: the change of face ----
static void dust_color(int face, uint8_t out[3]) {      // the face colour, washed out
    for (int i = 0; i < 3; ++i) out[i] = (uint8_t)lerpf((float)face_cfg[face].color[i], 255.0f, 0.45f);
}
static void to_dust(rig_pface_t *pf, particle_t *p) {
    dust_color(pf->face, p->tc);
    float dx = p->x - CX, dy = p->y - CY;
    p->role = R_DUST;
    p->dang = atan2f(dy, dx);
    p->dr = clampf(sqrtf(dx * dx + dy * dy), 30.0f, 112.0f);
    p->drt = 78.0f + pf_rand(&pf->rng) * 30.0f;
}
// Still on its way from the last change (scattered, drifting, or not yet arrived)?
// An interrupted particle only changes target; re-kicking it would push it ever further away.
static bool in_flight(const rig_pface_t *pf, const particle_t *p) {
    if (p->role == R_DUST) return (p->flags & F_KICK) ? pf->tin < p->gat : ((p->flags & F_DRIFT) && pf->tin < p->gat);
    if (pf->tin < p->gat) return (p->flags & F_KICK) ? pf->tin >= p->rel : (p->flags & F_DRIFT) != 0;
    return pf->tin < p->gat + pf->st->tg * 0.6f;
}
static void arm(rig_pface_t *pf, particle_t *p, bool was_face, bool fly, bool intro) {
    const style_t *st = pf->st;
    bool kick = !fly && was_face;
    p->flags = (uint8_t)((kick ? F_KICK : 0) | (fly ? F_DRIFT : 0));
    p->rel = 0.0f;
    if (kick) {
        float dx = p->x - CX, dy = p->y - CY, d = sqrtf(dx * dx + dy * dy);
        p->rel = st->wave * d / 120.0f;
        p->rel += pf_rand(&pf->rng) * 0.03f;
    }
    if (fly) {
        p->gat = pf_rand(&pf->rng) * 0.08f;             // turn towards the new target at once
    } else {
        p->gat = lerpf(st->gat_min, st->gat_max, pf_rand(&pf->rng));
        if (intro) p->gat += pf_rand(&pf->rng) * 0.9f;
    }
}
static void assign(rig_pface_t *pf, rig_face_t face, bool intro) {
    expr_t *e = &pf->expr[face];
    const face_cfg_t *cf = &face_cfg[face];
    pf->face = face;
    unsigned m = e->n < pf->n ? e->n : pf->n;
    for (unsigned i = 0; i < e->n; ++i) pf->order[i] = (uint16_t)i;
    for (unsigned i = e->n; i > 1; --i) {
        unsigned j = (unsigned)(pf_rand(&pf->rng) * (float)i);
        uint16_t tmp = pf->order[i - 1];
        pf->order[i - 1] = pf->order[j];
        pf->order[j] = tmp;
    }
    memset(pf->used, 0, pf->n);
    for (unsigned k = 0; k < m; ++k) {                  // each target takes the nearest free particle
        const target_t *t = &e->t[pf->order[k]];
        unsigned best = 0;
        float bd = 1e30f;
        for (unsigned i = 0; i < pf->n; ++i) {
            if (pf->used[i]) continue;
            float dx = pf->p[i].x - t->x, dy = pf->p[i].y - t->y, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = i; }
        }
        pf->used[best] = 1;
        particle_t *p = &pf->p[best];
        bool was_face = p->role != R_DUST, fly = in_flight(pf, p);
        p->role = t->role;
        p->bx = t->x;
        p->by = t->y;
        p->w = t->w;
        memcpy(p->tc, t->accent ? cf->accent : cf->color, 3);
        arm(pf, p, was_face, fly, intro);
    }
    for (unsigned i = 0; i < pf->n; ++i) {              // the rest become dust
        if (pf->used[i]) continue;
        particle_t *p = &pf->p[i];
        bool was_face = p->role != R_DUST, fly = in_flight(pf, p);
        if (was_face || fly) {
            to_dust(pf, p);
            arm(pf, p, was_face, fly, intro);
        } else {
            dust_color(face, p->tc);
            p->flags = 0;
        }
    }
    pf->tin = 0.0f;
}

// ---- per-frame animation: gaze, blink, breathing and each face's own motion ----
static void update_gaze(rig_pface_t *pf, float dt) {
    const face_cfg_t *cf = &face_cfg[pf->face];
    if (cf->scan_hz > 0.0f) {
        pf->gz.tx = sinf(pf->time * TAU_F * cf->scan_hz);
        pf->gz.ty = 0.0f;
    } else if ((pf->gz.t -= dt) <= 0.0f) {
        pf->gz.t = 1.2f + pf_rand(&pf->rng) * 2.6f;
        pf->gz.tx = clampf(cf->gaze_x + (pf_rand(&pf->rng) - 0.5f) * 1.6f, -1.0f, 1.0f);
        pf->gz.ty = clampf(cf->gaze_y + (pf_rand(&pf->rng) - 0.5f), -1.0f, 1.0f);
        if (pf_rand(&pf->rng) < 0.35f) { pf->gz.tx = cf->gaze_x; pf->gz.ty = cf->gaze_y; }
    }
    float k = 1.0f - expf(-dt * 12.0f);
    pf->gz.x += (pf->gz.tx - pf->gz.x) * k;
    pf->gz.y += (pf->gz.ty - pf->gz.y) * k;
}
static void update_blink(rig_pface_t *pf, float dt, bool assembled) {
    const face_cfg_t *cf = &face_cfg[pf->face];
    if (pf->bl.t < 0.0f) {
        pf->bl.amt = 0.0f;
        if (assembled && (pf->bl.next -= dt) <= 0.0f) pf->bl.t = 0.0f;
        return;
    }
    float t = (pf->bl.t += dt), c = cf->blink_close, h = cf->blink_hold, o = cf->blink_open;
    if (t < c) pf->bl.amt = smoothf(t / c);
    else if (t < c + h) pf->bl.amt = 1.0f;
    else if (t < c + h + o) pf->bl.amt = 1.0f - smoothf((t - c - h) / o);
    else {
        pf->bl.amt = 0.0f;
        pf->bl.t = -1.0f;
        pf->bl.next = pf_rand(&pf->rng) < 0.18f ? 0.2f : lerpf(cf->blink_gap0, cf->blink_gap1, pf_rand(&pf->rng));
    }
}
static void anim_face(rig_pface_t *pf, float ta) {
    xf_t *X = pf->xf;
    float t = pf->time, arr = clampf(ta / 0.45f, 0.0f, 1.0f);
    switch (pf->face) {
    case FACE_HAPPY: {
        float b = sinf(t * 3.4f) * 1.3f * arr;
        X[R_EYE_L].ty += b; X[R_EYE_R].ty += b; X[R_MOUTH].ty += b * 0.5f;
        X[R_BLUSH].a = 0.72f + 0.28f * sinf(t * 2.2f);
        break;
    }
    case FACE_LAUGH: {
        float s = fabsf(sinf(t * TAU_F * 1.7f)) * arr;
        pf->g.ty -= s * 3.4f;
        X[R_MOUTH].sy = 0.9f + 0.2f * s;
        X[R_BLUSH].a = 0.7f + 0.3f * s;
        break;
    }
    case FACE_SAD: {                                    // the tear falls; invisible in the last 20% so it can spring back
        pf->g.ty += 2.2f * arr;
        float ph = fmodf(t / 2.8f, 1.0f);
        xf_t *tr = &X[R_TEAR];
        if (ph < 0.8f) {
            float u = ph / 0.8f;
            tr->ty = u * 34.0f;
            tr->a = powf(fmaxf(0.0f, sinf(PI_F * u)), 0.7f);
            tr->sx = tr->sy = 0.85f + 0.25f * u;
        } else {
            tr->a = 0.0f;
        }
        break;
    }
    case FACE_ANGRY:
        pf->g.tx += sinf(t * 83.0f) * 0.9f * arr;
        pf->g.ty += cosf(t * 71.0f) * 0.7f * arr;
        pf->g.a *= 1.0f + 0.07f * sinf(t * 7.0f) * arr;
        break;
    case FACE_SURPRISE: {                               // a pop once everything has arrived
        float u = fmaxf(0.0f, ta);
        if (ta > 0.0f) pf->g.s *= 1.0f + 0.13f * expf(-u * 5.5f) * cosf(u * 17.0f);
        float p = 1.0f + 0.12f * sinf(t * 2.4f);
        X[R_PUPIL_L].sx = X[R_PUPIL_L].sy = X[R_PUPIL_R].sx = X[R_PUPIL_R].sy = p;
        break;
    }
    case FACE_THINK:
        for (int i = 0; i < 3; ++i) {
            float k = fmaxf(0.0f, sinf((t * 1.15f - (float)i * 0.2f) * TAU_F));
            xf_t *d = &X[R_DOT0 + i];
            d->sx = d->sy = 1.0f + 0.45f * k;
            d->ty = -3.2f * k;
            d->a = 0.45f + 0.55f * k;
        }
        break;
    case FACE_SLEEP: {
        float T = fmodf(t, 4.0f);
        for (int i = 0; i < 3; ++i) {                   // Zzz appear one by one, fade out together
            float on = 0.2f + 0.55f * (float)i, a = T > on ? fminf(1.0f, (T - on) / 0.4f) : 0.0f;
            if (T > 3.2f) a *= fmaxf(0.0f, 1.0f - (T - 3.2f) / 0.7f);
            xf_t *z = &X[R_Z0 + i];
            z->a = a;
            z->ty = -sinf(t * 1.4f + (float)i) * 1.8f - (T > on ? (T - on) * 3.0f : 0.0f);
            z->sx = z->sy = 0.85f + 0.15f * a;
        }
        break;
    }
    case FACE_LOVE: {
        float ph = fmodf(t * 1.2f, 1.0f), e1 = (ph - 0.1f) / 0.09f, e2 = (ph - 0.34f) / 0.1f;
        float beat = expf(-e1 * e1) + 0.65f * expf(-e2 * e2), s = 1.0f + 0.16f * beat * arr;
        X[R_EYE_L].sx = X[R_EYE_L].sy = X[R_EYE_R].sx = X[R_EYE_R].sy = s;
        for (int i = 0; i < 2; ++i) {                   // small hearts float up, hidden in the last 20%
            float q = fmodf(t / 3.2f + (float)i * 0.5f, 1.0f);
            xf_t *h = &X[R_H0 + i];
            if (q < 0.8f) {
                float u = q / 0.8f;
                h->ty = -u * 50.0f;
                h->tx = sinf(u * 5.0f + (float)i * 3.0f) * 5.0f;
                h->a = powf(fmaxf(0.0f, sinf(PI_F * u)), 0.8f);
                h->sx = h->sy = 0.7f + 0.4f * u;
            } else {
                h->a = 0.0f;
            }
        }
        break;
    }
    case FACE_WINK: {
        xf_t *s = &X[R_SPARK];
        s->sx = s->sy = 0.8f + 0.2f * sinf(t * 5.0f);
        s->rot = sinf(t * 2.0f) * 0.25f;
        s->a = 0.55f + 0.45f * sinf(t * 4.4f + 1.0f);
        break;
    }
    case FACE_CURIOUS:
        X[R_BROW_L].ty -= 1.5f * fabsf(sinf(t * 1.5f));
        break;
    case FACE_SHY:
        pf->g.tx += sinf(t * 1.1f) * 2.0f * arr;
        pf->g.ty += 2.0f * arr;
        X[R_BLUSH].a = 0.75f + 0.25f * sinf(t * 2.6f);
        break;
    default:
        break;
    }
}
static void compute_transforms(rig_pface_t *pf, float ta) {
    const face_cfg_t *cf = &face_cfg[pf->face];
    for (int r = 0; r < R_COUNT; ++r) pf->xf[r] = (xf_t){0, 0, 1, 1, 0, 1, 1, 0};
    float breath = TAU_F * (cf->breath > 1.5f ? 0.14f : 0.22f) * pf->time;
    pf->g.s = 1.0f + 0.012f * cf->breath * sinf(breath);
    pf->g.tx = 0.0f;
    pf->g.ty = 1.4f * cf->breath * sinf(breath + 0.8f);
    pf->g.a = 1.0f;
    static const struct { uint8_t role; float gain; } look[] = {
        {R_EYE_L, 1.0f}, {R_EYE_R, 1.0f}, {R_PUPIL_L, 1.9f}, {R_PUPIL_R, 1.9f},
        {R_BROW_L, 0.35f}, {R_BROW_R, 0.35f}, {R_MOUTH, 0.1f},
    };
    for (unsigned i = 0; i < sizeof look / sizeof look[0]; ++i) {
        float k = look[i].gain * cf->gaze;
        pf->xf[look[i].role].tx += pf->gz.x * 6.0f * k;
        pf->xf[look[i].role].ty += pf->gz.y * 4.0f * k;
    }
    for (int r = 0; r < R_COUNT; ++r) {
        if (cf->blink_roles & (1u << r)) pf->xf[r].sy *= 1.0f - cf->blink_depth * pf->bl.amt;
    }
    anim_face(pf, ta);
    for (int r = 0; r < R_COUNT; ++r) {
        xf_t *x = &pf->xf[r];
        x->c = x->rot != 0.0f ? cosf(x->rot) : 1.0f;
        x->s = x->rot != 0.0f ? sinf(x->rot) : 0.0f;
    }
}
// Where a particle wants to be right now: its base target through the role's
// transform and the whole-face transform, plus a faint shimmer.
static void target_of(const rig_pface_t *pf, const particle_t *p, float *ox, float *oy) {
    const xf_t *t = &pf->xf[p->role];
    const float *pv = pf->expr[pf->face].pivot[p->role];
    float dx = (p->bx - pv[0]) * t->sx, dy = (p->by - pv[1]) * t->sy;
    if (t->rot != 0.0f) {
        float x = dx * t->c - dy * t->s, y = dx * t->s + dy * t->c;
        dx = x;
        dy = y;
    }
    float x = pv[0] + dx + t->tx, y = pv[1] + dy + t->ty;
    x = CX + (x - CX) * pf->g.s + pf->g.tx;
    y = CY + (y - CY) * pf->g.s + pf->g.ty;
    float sh = face_cfg[pf->face].shimmer;
    *ox = x + fsin(pf->sin_tab, pf->time * p->f1 + p->ph) * sh;
    *oy = y + fcos(pf->sin_tab, pf->time * p->f2 + p->ph * 1.7f) * sh;
}

// ---- particle physics ----
typedef struct { float dt, hold, damp, flow, kdust, kwait, kgather, flash, lvl, dr, inv_tg, damp_hold; } stepk_t;
static void flow_at(const float *tab, float x, float y, float t, float *fx, float *fy) {
    *fx = (fsin(tab, y * 0.031f + t * 1.7f) + fsin(tab, (x + y) * 0.017f - t * 1.1f)) * 0.5f;
    *fy = (fcos(tab, x * 0.029f - t * 1.3f) + fcos(tab, (x - y) * 0.019f + t * 0.9f)) * 0.5f;
}
static void kick(rig_pface_t *pf, particle_t *p) {
    const style_t *st = pf->st;
    float dx = p->x - CX, dy = p->y - CY, d = sqrtf(dx * dx + dy * dy);
    if (d <= 0.0f) d = 1.0f;
    float rx = dx / d, ry = dy / d, sp = lerpf(st->vmin, st->vmax, pf_rand(&pf->rng));
    float n1 = (pf_rand(&pf->rng) - 0.5f) * 2.0f * st->noise;
    float vx = rx * st->radial - ry * st->tang + n1;
    float n2 = (pf_rand(&pf->rng) - 0.5f) * 2.0f * st->noise;
    float vy = ry * st->radial + rx * st->tang + n2 + st->up;
    float m = sqrtf(vx * vx + vy * vy);
    if (m <= 0.0f) m = 1.0f;
    p->vx += vx / m * sp;
    p->vy += vy / m * sp;
    float v = sqrtf(p->vx * p->vx + p->vy * p->vy);
    if (v > st->vmax) { p->vx *= st->vmax / v; p->vy *= st->vmax / v; }   // repeated changes must not pile up speed
    p->flash = 1.0f;
}
static void edge_push(particle_t *p, float dt) {        // soft wall: stay on the round screen
    float dx = p->x - CX, dy = p->y - CY, d2 = dx * dx + dy * dy;
    if (d2 > 112.0f * 112.0f) {
        float d = sqrtf(d2), k = (d - 112.0f) * 24.0f * dt / d;
        p->vx -= dx * k;
        p->vy -= dy * k;
    }
}
static void scatter_step(const rig_pface_t *pf, particle_t *p, const stepk_t *k) {
    float fx, fy;
    flow_at(pf->sin_tab, p->x, p->y, pf->time, &fx, &fy);
    p->vx = (p->vx + fx * k->flow) * k->damp;
    p->vy = (p->vy + fy * k->flow) * k->damp;
    edge_push(p, k->dt);
}
static void dust_step(const rig_pface_t *pf, particle_t *p, const stepk_t *k) {
    p->dr += (p->drt - p->dr) * k->dr;
    p->dang += p->dom * k->dt;
    if (p->dang > PI_F) p->dang -= TAU_F;
    else if (p->dang < -PI_F) p->dang += TAU_F;
    const float *tab = pf->sin_tab;
    float rr = p->dr * (1.0f + 0.05f * fsin(tab, pf->time * 0.6f + p->ph));
    float hx = CX + fcos(tab, p->dang) * rr, hy = CY + fsin(tab, p->dang) * rr, fx, fy;
    flow_at(tab, p->x, p->y, pf->time, &fx, &fy);
    p->vx += ((hx - p->x) * 2.4f + fx * 16.0f - p->vx * 1.7f) * k->dt;
    p->vy += ((hy - p->y) * 2.4f + fy * 16.0f - p->vy * 1.7f) * k->dt;
    edge_push(p, k->dt);
}
static void step_particle(rig_pface_t *pf, particle_t *p, draw_t *d, const stepk_t *k) {
    const style_t *st = pf->st;
    const float tin = pf->tin;
    if (p->role == R_DUST) {
        if (p->flags & F_KICK) {                        // surplus face particle: scatter, then join the dust ring
            if (tin < p->rel) {
                p->vx *= k->hold; p->vy *= k->hold;
            } else {
                if (!(p->flags & F_REL)) { kick(pf, p); p->flags |= F_REL; }
                if (tin < p->gat) scatter_step(pf, p, k);
                else { p->flags &= (uint8_t)~F_KICK; dust_step(pf, p, k); }
            }
        } else if ((p->flags & F_DRIFT) && tin < p->gat) {
            scatter_step(pf, p, k);                     // interrupted in flight: keep drifting a moment
        } else {
            p->flags &= (uint8_t)~F_DRIFT;
            dust_step(pf, p, k);
        }
    } else if (tin < p->gat) {
        if (p->flags & F_KICK) {                        // face particle: scattered in a wave from the centre
            if (tin < p->rel) {
                p->vx *= k->hold; p->vy *= k->hold;
            } else {
                if (!(p->flags & F_REL)) { kick(pf, p); p->flags |= F_REL; }
                scatter_step(pf, p, k);
            }
        } else if (p->flags & F_DRIFT) {
            scatter_step(pf, p, k);
        } else {
            dust_step(pf, p, k);                        // was dust: keep orbiting until its turn to gather
        }
    } else {
        // Gather: stiffness grows from weak to strong, like gravity getting closer.
        float s = clampf((tin - p->gat) * k->inv_tg, 0.0f, 1.0f), stiff = K_HOLD, damping = k->damp_hold, tx, ty;
        if (s < 1.0f) {                                 // still arriving; a settled particle skips this
            stiff = lerpf(st->k0, K_HOLD, ease_at(pf->ease, s));
            damping = fmaxf(1.4f, 2.0f * ZETA * sqrtf(stiff));
        }
        target_of(pf, p, &tx, &ty);
        float ex = tx - p->x, ey = ty - p->y;
        float ax = stiff * ex - damping * p->vx, ay = stiff * ey - damping * p->vy;
        if (s < 1.0f) {
            float fx, fy, fw = (1.0f - s) * st->flow * FLOW_A * 0.5f;
            flow_at(pf->sin_tab, p->x, p->y, pf->time, &fx, &fy);
            ax += fx * fw;
            ay += fy * fw;
            if (st->swirl > 0.0f) {
                float dist = sqrtf(ex * ex + ey * ey);
                if (dist <= 0.0f) dist = 1.0f;
                float sw = st->swirl * (1.0f - s) * fminf(1.0f, dist / 80.0f) * 420.0f / dist;
                ax += -ey * sw;
                ay += ex * sw;
            }
        }
        p->vx += ax * k->dt;
        p->vy += ay * k->dt;
        float v2 = p->vx * p->vx + p->vy * p->vy;
        if (v2 > VMAX * VMAX) { float sp = sqrtf(v2); p->vx *= VMAX / sp; p->vy *= VMAX / sp; }
    }
    p->x += p->vx * k->dt;
    p->y += p->vy * k->dt;
    float kk = p->role == R_DUST ? k->kdust : tin < p->gat ? k->kwait : k->kgather;      // colour follows the target
    p->cr += ((float)p->tc[0] - p->cr) * kk;
    p->cg += ((float)p->tc[1] - p->cg) * kk;
    p->cb += ((float)p->tc[2] - p->cb) * kk;
    p->flash *= k->flash;
    float lt = (p->role == R_DUST || (tin < p->gat && !(p->flags & F_KICK))) ? p->dustw
             : (float)p->w * (1.0f / 255.0f) * face_cfg[pf->face].bright;
    p->lvl += (lt - p->lvl) * k->lvl;

    // Final brightness, and the compact entry the renderer draws from.
    float b;
    const float *tab = pf->sin_tab;
    if (p->role == R_DUST) {
        b = p->lvl * (0.65f + 0.35f * fsin(tab, pf->time * p->tw * 0.7f + p->ph * 2.0f)) + p->flash * 0.5f;
    } else {
        b = p->lvl * pf->xf[p->role].a * pf->g.a * (0.84f + 0.16f * fsin(tab, pf->time * p->tw + p->ph));
        if (p->lvl > 0.3f) {
            float v2 = p->vx * p->vx + p->vy * p->vy;
            b += (v2 >= 44100.0f ? 0.5f : v2 > 4.0f ? sqrtf(v2) * (1.0f / 420.0f) : 0.0f) + p->flash * 0.7f;
            float dd = (p->y - pf->scan_y) * (1.0f / 9.0f);        // a hologram scan band sweeps down the face
            if (dd > -3.0f && dd < 3.0f) b += 0.28f * expf(-dd * dd);
        }
    }
    p->bb = b;
    d->show = 0;
    if (b >= 0.04f && p->x > -40.0f && p->x < 280.0f && p->y > -40.0f && p->y < 280.0f) {   // rejects NaN too
        float gain = (b < 1.25f ? b : 1.25f) * (p->role == R_DUST ? 1.2f : 1.4f);
        float r = p->cr * gain, g = p->cg * gain, bl = p->cb * gain;
        d->r = (uint8_t)(r < 255.0f ? r : 255.0f);
        d->g = (uint8_t)(g < 255.0f ? g : 255.0f);
        d->b = (uint8_t)(bl < 255.0f ? bl : 255.0f);
        d->x = p->x;
        d->y = p->y;
        d->tx = -p->vx * k->dt;                          // ghost dots trail behind a moving particle
        d->ty = -p->vy * k->dt;
        d->ghosts = p->vx * p->vx + p->vy * p->vy > 4900.0f;
        d->dust = p->role == R_DUST;
        d->show = 1;
    }
}

void rig_pface_step(rig_pface_t *pf, float dt) {
    if (!pf || !(dt > 0.0f)) return;
    if (dt > 0.1f) dt = 0.1f;
    const style_t *st = pf->st;
    pf->time += dt;
    if (pf->time > TIME_WRAP) pf->time -= TIME_WRAP;
    if (pf->tin < 1.0e6f) pf->tin += dt;
    pf->scan_y = -30.0f + fmodf(pf->time * 52.0f, 300.0f);
    float ta = pf->tin - (st->gat_max + st->tg);
    update_gaze(pf, dt);
    update_blink(pf, dt, ta > 0.0f);
    compute_transforms(pf, ta);
    float kn = 1.0f - expf(-dt * 4.0f);                 // status dot colour follows the connection
    const float *goal = pf->connected ? (const float[]){62, 210, 118} : (const float[]){242, 165, 56};
    for (int i = 0; i < 3; ++i) pf->status[i] += (goal[i] - pf->status[i]) * kn;
    stepk_t k = {
        .dt = dt, .hold = expf(-3.8f * dt), .damp = expf(-st->drag * dt), .flow = FLOW_A * st->flow * dt,
        .kdust = 1.0f - expf(-dt * 1.5f), .kwait = 1.0f - expf(-dt * 1.2f), .kgather = 1.0f - expf(-dt * 5.5f),
        .flash = expf(-dt * 3.2f), .lvl = 1.0f - expf(-dt * 5.0f), .dr = fminf(1.0f, dt * 0.5f),
        .inv_tg = 1.0f / st->tg, .damp_hold = 2.0f * ZETA * sqrtf(K_HOLD),
    };
    for (unsigned i = 0; i < pf->n; ++i) step_particle(pf, &pf->p[i], &pf->dl[i], &k);
}

// ---- rendering: gaussian dots, max-blended, one strip at a time ----
static void make_lut(uint8_t *lut, float d2_per_entry, float sigma) {
    float inv = 1.0f / (2.0f * sigma * sigma);
    for (int i = 0; i < LUT_N; ++i) {
        float w = expf(-((float)i * d2_per_entry) * inv);
        lut[i] = w < 0.05f ? 0 : (uint8_t)(w * 255.0f + 0.5f);
    }
}
static uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
// The strip holds wire-order pixels (byte-swapped RGB565), so read and write through swap16.
static void put_max(uint16_t *px, int r5, int g6, int b5) {
    uint16_t c = swap16(*px);
    int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    if (r5 > r) r = r5;
    if (g6 > g) g = g6;
    if (b5 > b) b = b5;
    *px = swap16((uint16_t)((r << 11) | (g << 5) | b));
}
// scale: 256 = full brightness. lo/hi: pixel window around the dot (at most 12 wide).
static void splat(uint16_t *px, int y0, int rows, float x, float y, int r8, int g8, int b8,
                  const uint8_t *lut, float per_d2, int lo, int hi, int scale) {
    int ix = (int)x, iy = (int)y, n = hi - lo + 1;
    if ((float)ix > x) --ix;                            // floor() without a libm call
    if ((float)iy > y) --iy;
    float dx2[12];
    for (int i = 0; i < n; ++i) {
        float dx = (float)(ix + lo + i) + 0.5f - x;
        dx2[i] = dx * dx;
    }
    for (int oy = lo; oy <= hi; ++oy) {
        int py = iy + oy;
        if (py < y0 || py >= y0 + rows) continue;
        float dy = (float)py + 0.5f - y, dy2 = dy * dy;
        uint16_t *row = px + (py - y0) * 240;
        for (int i = 0; i < n; ++i) {
            int pxx = ix + lo + i;
            if (pxx < 0 || pxx >= 240) continue;
            int idx = (int)((dx2[i] + dy2) * per_d2);
            if (idx >= LUT_N) continue;
            int w = (lut[idx] * scale) >> 8;
            if (w) put_max(&row[pxx], (r8 * w) >> 11, (g8 * w) >> 10, (b8 * w) >> 11);
        }
    }
}
void rig_pface_render(const rig_pface_t *pf, uint16_t *pixels, int y0, int rows) {
    if (!pf || !pixels || y0 < 0 || rows < 0 || y0 + rows > 240) return;
    memset(pixels, 0, (size_t)rows * 240 * sizeof *pixels);
    const float yb = (float)y0, ye = (float)(y0 + rows);
    for (unsigned i = 0; i < pf->n; ++i) {
        const draw_t *d = &pf->dl[i];
        if (!d->show) continue;
        float lo = d->y, hi = d->y;                     // rows this dot and its ghosts can touch
        if (d->ghosts) {
            float e = d->y + d->ty * 3.0f;
            if (e < lo) lo = e; else hi = e;
        }
        if (hi + 3.0f < yb || lo - 3.0f >= ye) continue;
        const uint8_t *lut = d->dust ? pf->lut_dust : pf->lut_face;
        splat(pixels, y0, rows, d->x, d->y, d->r, d->g, d->b, lut, 16.0f, -1, 2, 256);
        if (!d->ghosts) continue;
        for (int g = 1; g <= 3; ++g) {                  // ghost dots trail behind a moving particle
            float gx = d->x + d->tx * (float)g, gy = d->y + d->ty * (float)g;
            if (gx > -40.0f && gx < 280.0f && gy > -40.0f && gy < 280.0f)
                splat(pixels, y0, rows, gx, gy, d->r, d->g, d->b, lut, 16.0f, -1, 2, 256 >> g);
        }
    }
    const float sx = 120.0f, sy = 211.0f;               // Muse connection dot: green = online, orange = offline
    if (!(sy + 7.0f < yb || sy - 6.0f >= ye)) {
        float pulse = 0.85f + 0.15f * fsin(pf->sin_tab, pf->time * 2.5f);
        splat(pixels, y0, rows, sx, sy, (int)(pf->status[0] * pulse), (int)(pf->status[1] * pulse), (int)(pf->status[2] * pulse),
              pf->lut_dot, 6.0f, -5, 6, 256);
    }
}

// ---- public API ----
void rig_pface_destroy(rig_pface_t *pf) {
    if (!pf) return;
    for (int f = 0; f < FACE_COUNT; ++f) pf_free(pf->expr[f].t);
    pf_free(pf->p);
    pf_free(pf->used);
    pf_free(pf->order);
    pf_free(pf->dl);
    pf_free(pf);
}
rig_pface_t *rig_pface_create(rig_pface_style_t style, uint32_t seed) {
    if ((unsigned)style > (unsigned)RIG_PFACE_DUST) return NULL;
    rig_pface_t *pf = pf_calloc(sizeof *pf);
    build_t *b = pf_calloc(sizeof *b);
    if (!pf || !b) {
        pf_free(pf);
        pf_free(b);
        return NULL;
    }
    pf->bytes = sizeof *pf;
    pf->st = &styles[style];
    pf->rng = seed;
    b->S = SPACING;
    unsigned largest = 0;
    bool ok = true;
    for (int f = 0; ok && f < FACE_COUNT; ++f) {
        ok = build_face(pf, b, f);
        if (ok && pf->expr[f].n > largest) largest = pf->expr[f].n;
    }
    pf_free(b);
    pf->n = ok ? ((unsigned)ceilf((float)largest * POOL_FACTOR / 8.0f)) * 8u : 0;
    if (ok) {
        pf->p = pf_calloc(pf->n * sizeof *pf->p);
        pf->used = pf_calloc(pf->n);
        pf->order = pf_calloc(largest * sizeof *pf->order);
        pf->dl = pf_calloc(pf->n * sizeof *pf->dl);
        ok = pf->p && pf->used && pf->order && pf->dl;
    }
    if (!ok) {
        rig_pface_destroy(pf);
        return NULL;
    }
    pf->bytes += pf->n * (sizeof *pf->p + sizeof *pf->dl + 1) + largest * sizeof *pf->order;
    for (int i = 0; i <= SIN_N; ++i) pf->sin_tab[i] = sinf(TAU_F * (float)i / (float)SIN_N);
    for (int i = 0; i <= EASE_N; ++i) pf->ease[i] = powf((float)i / (float)EASE_N, 2.2f);
    make_lut(pf->lut_face, 1.0f / 16.0f, 0.95f);
    make_lut(pf->lut_dust, 1.0f / 16.0f, 0.55f);
    make_lut(pf->lut_dot, 1.0f / 6.0f, 1.7f);
    pf->gz.t = 1.0f;
    pf->bl.t = -1.0f;
    pf->bl.next = 2.5f;
    pf->scan_y = -40.0f;
    pf->g.s = 1.0f;
    pf->g.a = 1.0f;
    pf->status[0] = 242.0f; pf->status[1] = 165.0f; pf->status[2] = 56.0f;
    for (unsigned i = 0; i < pf->n; ++i) {              // start as a ring of dust
        particle_t *p = &pf->p[i];
        float a = pf_rand(&pf->rng) * TAU_F, r = 80.0f + pf_rand(&pf->rng) * 30.0f;
        float om = 0.10f + pf_rand(&pf->rng) * 0.18f;
        if (pf_rand(&pf->rng) < 0.08f) om = -om;
        p->x = CX + cosf(a) * r;
        p->y = CY + sinf(a) * r;
        p->tc[0] = 150; p->tc[1] = 200; p->tc[2] = 230;
        p->cr = 150.0f; p->cg = 200.0f; p->cb = 230.0f;
        p->ph = pf_rand(&pf->rng) * TAU_F;
        p->f1 = 0.7f + pf_rand(&pf->rng) * 1.6f;
        p->f2 = 0.7f + pf_rand(&pf->rng) * 1.6f;
        p->tw = 1.6f + pf_rand(&pf->rng) * 3.0f;
        p->dang = a;
        p->dom = om;
        p->dr = r;
        p->drt = 78.0f + pf_rand(&pf->rng) * 30.0f;
        p->dustw = 0.18f + 0.32f * powf(pf_rand(&pf->rng), 1.5f);
        p->lvl = 0.25f;
    }
    assign(pf, FACE_IDLE, true);                        // boot: the dust condenses into a face
    return pf;
}
void rig_pface_set_face(rig_pface_t *pf, rig_face_t face) {
    if (!pf || (unsigned)face >= (unsigned)FACE_COUNT || face == pf->face) return;
    assign(pf, face, false);
}
rig_face_t rig_pface_face(const rig_pface_t *pf) { return pf ? pf->face : FACE_IDLE; }
void rig_pface_set_connected(rig_pface_t *pf, bool connected) {
    if (pf) pf->connected = connected;
}
rig_pface_stats_t rig_pface_stats(const rig_pface_t *pf) {
    rig_pface_stats_t s = {0};
    s.finite = true;
    if (!pf) return s;
    s.particles = pf->n;
    s.targets = pf->expr[pf->face].n;
    s.bytes = pf->bytes;
    for (unsigned i = 0; i < pf->n; ++i) {
        const particle_t *p = &pf->p[i];
        if (!isfinite(p->x) || !isfinite(p->y) || !isfinite(p->vx) || !isfinite(p->vy) || !isfinite(p->bb)) {
            s.finite = false;
            continue;
        }
        float dx = p->x - CX, dy = p->y - CY, r = sqrtf(dx * dx + dy * dy);
        if (r > s.max_radius) s.max_radius = r;
        if (p->role == R_DUST) continue;
        ++s.face_particles;
        float tx, ty;
        target_of(pf, p, &tx, &ty);
        if ((p->x - tx) * (p->x - tx) + (p->y - ty) * (p->y - ty) < 14.0f * 14.0f) ++s.assembled;
    }
    return s;
}
