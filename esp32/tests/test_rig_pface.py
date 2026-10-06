"""Particle face engine: settling, transitions, strips, determinism, interruptions, sanitizers."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include "rig_pface.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DT (1.0f / 30.0f)
static const char *names[FACE_COUNT] = {"idle","happy","sleep","curious","surprise","shy","sad","angry","laugh","think","love","wink"};
static void run(rig_pface_t *pf, float seconds) {
    for (int i = 0, n = (int)(seconds * 30.0f + 0.5f); i < n; ++i) rig_pface_step(pf, DT);
}
static void frame(const rig_pface_t *pf, uint16_t *out) {
    for (int y = 0; y < 240; y += 16) rig_pface_render(pf, out + y * 240, y, 16);
}
static float ratio(const rig_pface_t *pf) {
    rig_pface_stats_t s = rig_pface_stats(pf);
    assert(s.finite && s.face_particles > 0);
    return (float)s.assembled / (float)s.face_particles;
}
static int cmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }
// Effects hop around and blinks squash the eyes, so judge a face by the median of many samples.
static float settled_ratio(rig_pface_t *pf) {
    float r[12];
    for (int i = 0; i < 12; ++i) { run(pf, 0.31f); r[i] = ratio(pf); }
    qsort(r, 12, sizeof r[0], cmp);
    return r[6];
}
static void check_bounded(const rig_pface_t *pf, float radius, const char *what) {
    rig_pface_stats_t s = rig_pface_stats(pf);
    if (!s.finite || s.max_radius > radius) {
        fprintf(stderr, "%s: finite=%d max_radius=%.1f\n", what, s.finite, (double)s.max_radius);
        abort();
    }
}
int main(void) {
    assert(!rig_pface_create((rig_pface_style_t)7, 1));
    rig_pface_destroy(NULL);
    rig_pface_set_face(NULL, FACE_HAPPY);
    rig_pface_step(NULL, DT);
    assert(rig_pface_face(NULL) == FACE_IDLE);

    // Every style: boot condenses into FACE_IDLE; every face settles, in every style.
    for (int style = RIG_PFACE_BURST; style <= RIG_PFACE_DUST; ++style) {
        rig_pface_t *pf = rig_pface_create((rig_pface_style_t)style, 20261005u);
        assert(pf);
        rig_pface_stats_t s = rig_pface_stats(pf);
        assert(s.particles >= 280 && s.particles <= 480 && s.particles % 8 == 0);
        assert(s.bytes > 0 && s.bytes < 160000);
        if (style == RIG_PFACE_VORTEX) printf("pool=%u idle_targets=%u heap=%zu bytes\n", s.particles, s.targets, s.bytes);
        run(pf, 4.5f);
        float r = settled_ratio(pf);
        if (r < 0.97f) { fprintf(stderr, "style %d boot: %.3f\n", style, (double)r); abort(); }
        for (int face = FACE_IDLE + 1; face < FACE_COUNT; ++face) {
            rig_pface_set_face(pf, (rig_face_t)face);
            assert(rig_pface_face(pf) == (rig_face_t)face);
            run(pf, 3.0f);
            r = settled_ratio(pf);
            if (r < 0.97f) { fprintf(stderr, "style %d face %s: %.3f\n", style, names[face], (double)r); abort(); }
            check_bounded(pf, 125.0f, names[face]);
            s = rig_pface_stats(pf);
            assert(s.targets >= 60 && s.targets <= 300 && s.face_particles == s.targets && s.particles > s.targets);
        }
        rig_pface_destroy(pf);
    }

    // A change finishes quickly: a vortex change is mostly done 1.8 s later.
    rig_pface_t *pf = rig_pface_create(RIG_PFACE_VORTEX, 5);
    run(pf, 4.5f);
    rig_pface_set_face(pf, FACE_HAPPY);
    rig_pface_face(pf);
    run(pf, 0.2f);
    assert(ratio(pf) < 0.5f);            // scattered, not a face yet
    run(pf, 1.6f);
    float best = 0.0f;
    for (int i = 0; i < 4; ++i) { run(pf, 0.15f); float r = ratio(pf); if (r > best) best = r; }
    assert(best > 0.95f);
    // Same face, or an invalid one: ignored, nothing scatters.
    rig_pface_set_face(pf, FACE_HAPPY);
    rig_pface_set_face(pf, FACE_COUNT);
    rig_pface_set_face(pf, (rig_face_t)-1);
    assert(rig_pface_face(pf) == FACE_HAPPY);
    run(pf, 0.2f);
    assert(settled_ratio(pf) > 0.95f);

    // Strips are independent: the whole frame equals the 16-row strips joined, mid-change included.
    uint16_t *a = malloc(240 * 240 * 2), *b = malloc(240 * 240 * 2);
    for (int face = FACE_IDLE; face < FACE_COUNT; ++face) {
        rig_pface_set_face(pf, (rig_face_t)face);
        for (int t = 0; t < 40; ++t) {
            run(pf, 0.05f);
            rig_pface_render(pf, a, 0, 240);
            frame(pf, b);
            assert(!memcmp(a, b, 240 * 240 * 2));
        }
    }
    // The picture is not empty and stays inside the round screen.
    run(pf, 4.0f);
    frame(pf, a);
    unsigned lit = 0, outside = 0;
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x) {
        if (!a[y * 240 + x]) continue;
        ++lit;
        if ((x - 120) * (x - 120) + (y - 120) * (y - 120) > 126 * 126) ++outside;
    }
    assert(lit > 500 && outside == 0);

    // Bad arguments change nothing.
    uint16_t guard[240 * 16];
    memset(guard, 0xAA, sizeof guard);
    rig_pface_render(pf, guard, -1, 16);
    rig_pface_render(pf, guard, 230, 16);
    rig_pface_render(pf, guard, 0, -1);
    rig_pface_render(pf, NULL, 0, 16);
    rig_pface_render(NULL, guard, 0, 16);
    for (unsigned i = 0; i < sizeof guard / sizeof guard[0]; ++i) assert(guard[i] == 0xAAAA);
    // Odd time steps are clamped or ignored.
    rig_pface_step(pf, 0.0f);
    rig_pface_step(pf, -1.0f);
    rig_pface_step(pf, NAN);
    rig_pface_step(pf, INFINITY);
    rig_pface_step(pf, 10.0f);
    check_bounded(pf, 125.0f, "odd dt");

    // Changing the face faster than it can settle never flings particles away or breaks anything.
    uint32_t seed = 12345;
    for (int gap = 1; gap <= 4; gap += 3) {
        for (int i = 0; i < 300; ++i) {
            seed = seed * 1664525u + 1013904223u;
            rig_pface_set_face(pf, (rig_face_t)((seed >> 16) % FACE_COUNT));
            for (int k = 0; k < gap; ++k) { rig_pface_step(pf, DT); check_bounded(pf, 160.0f, "interrupts"); }
        }
        rig_pface_set_face(pf, FACE_SURPRISE);
        run(pf, 5.0f);
        float r = settled_ratio(pf);
        if (r < 0.97f) { fprintf(stderr, "after interrupts (gap %d): %.3f\n", gap, (double)r); abort(); }
    }

    // A change that interrupts one still in flight must retarget at once: scattered particles turn
    // towards the new face instead of waiting out the full delay again.
    static const float when[3] = {0.6f, 0.8f, 1.0f}, need[3] = {0.65f, 0.9f, 0.8f};
    for (int style = RIG_PFACE_BURST; style <= RIG_PFACE_DUST; ++style) {
        double sum = 0.0;
        int cases = 0;
        for (int from = 0; from < FACE_COUNT; from += 2) for (int to = 0; to < FACE_COUNT; to += 3) {
            rig_face_t mid = (rig_face_t)((from + 5) % FACE_COUNT);
            if ((rig_face_t)to == mid) continue;
            rig_pface_t *q = rig_pface_create((rig_pface_style_t)style, (uint32_t)(7 + from * 13 + to));
            run(q, 4.5f);
            rig_pface_set_face(q, (rig_face_t)from);
            run(q, 3.0f);
            rig_pface_set_face(q, mid);
            run(q, 0.4f);                                   // mid-flight
            rig_pface_set_face(q, (rig_face_t)to);
            run(q, when[style]);
            sum += ratio(q);
            ++cases;
            rig_pface_destroy(q);
        }
        if (sum / cases < (double)need[style]) {
            fprintf(stderr, "style %d: only %.2f assembled %.1fs after an interrupted change\n", style, sum / cases, (double)when[style]);
            abort();
        }
    }

    // The soft wall keeps particles on the round screen while a change scatters them (without it they reach 160..200).
    for (int style = RIG_PFACE_BURST; style <= RIG_PFACE_DUST; ++style) {
        for (int from = 0; from < FACE_COUNT; ++from) {
            rig_pface_t *q = rig_pface_create((rig_pface_style_t)style, (uint32_t)(3 + from * 17));
            run(q, 4.5f);
            rig_pface_set_face(q, (rig_face_t)from);
            run(q, 3.0f);
            rig_pface_set_face(q, (rig_face_t)((from + 5) % FACE_COUNT));
            for (int k = 0; k < 90; ++k) { rig_pface_step(q, DT); check_bounded(q, 150.0f, "face change"); }
            rig_pface_destroy(q);
        }
    }

    // Same seed and calls give the same pixels; another seed does not.
    rig_pface_t *x = rig_pface_create(RIG_PFACE_VORTEX, 99), *y = rig_pface_create(RIG_PFACE_VORTEX, 99), *z = rig_pface_create(RIG_PFACE_VORTEX, 100);
    int differ = 0;
    for (int step = 0; step < 6; ++step) {
        rig_pface_set_face(x, (rig_face_t)step); rig_pface_set_face(y, (rig_face_t)step); rig_pface_set_face(z, (rig_face_t)step);
        run(x, 0.7f); run(y, 0.7f); run(z, 0.7f);
        frame(x, a); frame(y, b);
        assert(!memcmp(a, b, 240 * 240 * 2));
        frame(z, b);
        if (memcmp(a, b, 240 * 240 * 2)) ++differ;
    }
    assert(differ > 0);
    rig_pface_destroy(x); rig_pface_destroy(y); rig_pface_destroy(z);

    // The Muse connection dot: orange offline, green online, under the mouth. Two engines that differ
    // only in `connected` have identical particles, so the pixels that differ are the dot itself
    // (dust drifts through that spot and may be brighter than the dot, so never search for "the brightest").
    rig_pface_t *on = rig_pface_create(RIG_PFACE_VORTEX, 31), *off = rig_pface_create(RIG_PFACE_VORTEX, 31);
    rig_pface_set_connected(on, true);
    run(on, 4.0f);
    run(off, 4.0f);
    frame(on, a);
    frame(off, b);
    int cx = -1, cy = -1, best_delta = 0;
    for (int py = 0; py < 240; ++py) for (int px = 0; px < 240; ++px) {
        unsigned u = (unsigned)((a[py * 240 + px] >> 8) | (a[py * 240 + px] << 8)) & 0xFFFF;
        unsigned v = (unsigned)((b[py * 240 + px] >> 8) | (b[py * 240 + px] << 8)) & 0xFFFF;
        int gu = (int)((u >> 5) & 63) << 2, ru = (int)(u >> 11) << 3, gv = (int)((v >> 5) & 63) << 2, rv = (int)(v >> 11) << 3;
        int delta = abs((gu - ru) - (gv - rv));
        if (delta > best_delta) { best_delta = delta; cx = px; cy = py; }
    }
    assert(best_delta > 100 && abs(cx - 120) <= 3 && abs(cy - 211) <= 3);   // found, and under the mouth
    unsigned u = (unsigned)((a[cy * 240 + cx] >> 8) | (a[cy * 240 + cx] << 8)) & 0xFFFF;
    unsigned v = (unsigned)((b[cy * 240 + cx] >> 8) | (b[cy * 240 + cx] << 8)) & 0xFFFF;
    int gu = (int)((u >> 5) & 63) << 2, ru = (int)(u >> 11) << 3, bu = (int)(u & 31) << 3;
    int gv = (int)((v >> 5) & 63) << 2, rv = (int)(v >> 11) << 3, bv = (int)(v & 31) << 3;
    assert(gu > ru && gu > bu && gu > 120);        // online: green
    assert(rv > gv && gv > bv && rv > 120);        // offline: orange
    rig_pface_destroy(on);
    rig_pface_destroy(off);

    // Two simulated hours: the clock wraps and nothing drifts.
    for (int i = 0; i < 72000; ++i) rig_pface_step(pf, 0.1f);
    rig_pface_set_face(pf, FACE_LOVE);
    run(pf, 4.0f);
    assert(settled_ratio(pf) > 0.97f);
    check_bounded(pf, 125.0f, "after two hours");
    rig_pface_destroy(pf);
    free(a);
    free(b);
    return 0;
}
'''
class RigPfaceTest(unittest.TestCase):
    def test_particle_face_engine_with_sanitizers(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)
            (p / 'test.c').write_text(TEST)
            san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']
            # Float discipline: the S3's FPU is single precision, a stray double is software maths.
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wdouble-promotion', *san,
                            '-I', str(ROOT / 'main'), '-c', str(ROOT / 'main/rig_pface.c'), '-o', str(p / 'engine.o')], check=True)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', *san, '-I', str(ROOT / 'main'),
                            str(p / 'test.c'), str(p / 'engine.o'), '-lm', '-o', str(p / 'test')], check=True)
            done = subprocess.run([str(p / 'test')], check=True, capture_output=True, text=True, timeout=600)
            print(done.stdout.strip())
if __name__ == '__main__':
    unittest.main()
