#!/usr/bin/env python3
"""Render the particle face on the host and save PNGs: no board needed.

It builds main/rig_pface.c with a small driver, renders through the same
16-row RGB565 strips the screen task sends, and masks the result to the round
GC9A01 panel.

  python3 tools/pface_preview.py                   # PNGs in /tmp/pface
  python3 tools/pface_preview.py --style burst --scale 3 --out /tmp/x

Output: faces.png (all faces, settled), trans_<from>_<to>.png (the reassembly
as a filmstrip, 30 fps frames) and status.png (online and offline dot).
"""
import argparse
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FACES = ["idle", "happy", "sleep", "curious", "surprise", "shy",
         "sad", "angry", "laugh", "think", "love", "wink"]
STYLES = {"burst": 0, "vortex": 1, "dust": 2}
TRANSITIONS = [("angry", "happy"), ("sleep", "surprise"), ("idle", "curious"), ("love", "sad")]
TIMES = [0.1, 0.25, 0.4, 0.55, 0.7, 0.9, 1.2, 1.8]
W = 240

DRIVER = r'''
#include "rig_pface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *names[FACE_COUNT] = {"idle","happy","sleep","curious","surprise","shy","sad","angry","laugh","think","love","wink"};
int main(int argc, char **argv) {
    if (argc < 3) return 1;
    rig_pface_t *pf = rig_pface_create((rig_pface_style_t)atoi(argv[1]), (uint32_t)strtoul(argv[2], 0, 10));
    if (!pf) return 2;
    static uint16_t strip[240 * 16];
    static unsigned char frame[240 * 240 * 3];
    char cmd[64], name[32];
    double v;
    while (scanf("%63s", cmd) == 1) {
        if (!strcmp(cmd, "face") && scanf("%31s", name) == 1) {
            for (int f = 0; f < FACE_COUNT; ++f) if (!strcmp(name, names[f])) rig_pface_set_face(pf, (rig_face_t)f);
        } else if (!strcmp(cmd, "run") && scanf("%lf", &v) == 1) {
            for (int i = 0, n = (int)(v * 30 + 0.5); i < n; ++i) rig_pface_step(pf, 1.0f / 30.0f);
        } else if (!strcmp(cmd, "connected") && scanf("%lf", &v) == 1) {
            rig_pface_set_connected(pf, v != 0);
        } else if (!strcmp(cmd, "snap")) {
            for (int y = 0; y < 240; y += 16) {
                rig_pface_render(pf, strip, y, 16);
                for (int i = 0; i < 240 * 16; ++i) {
                    unsigned c = (unsigned)((strip[i] >> 8) | (strip[i] << 8)) & 0xFFFF;   // back to host order
                    unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
                    int px = i % 240, py = y + i / 240, dx = px - 120, dy = py - 120;
                    unsigned char *o = frame + ((size_t)py * 240 + (size_t)px) * 3;
                    int inside = dx * dx + dy * dy <= 119 * 119;
                    o[0] = inside ? (unsigned char)((r << 3) | (r >> 2)) : 0;
                    o[1] = inside ? (unsigned char)((g << 2) | (g >> 4)) : 0;
                    o[2] = inside ? (unsigned char)((b << 3) | (b >> 2)) : 0;
                }
            }
            fwrite(frame, 1, sizeof frame, stdout);
        }
    }
    rig_pface_destroy(pf);
    return 0;
}
'''


def png(path, width, height, rgb):
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def sheet(frames, cols, scale):
    """Tile 240x240 RGB frames into a grid and scale it up (nearest neighbour)."""
    rows = (len(frames) + cols - 1) // cols
    blank = bytes(W * W * 3)
    frames = frames + [blank] * (rows * cols - len(frames))
    out = bytearray()
    for r in range(rows):
        for y in range(W):
            line = b"".join(f[y * W * 3:(y + 1) * W * 3] for f in frames[r * cols:(r + 1) * cols])
            if scale > 1:
                line = b"".join(line[i:i + 3] * scale for i in range(0, len(line), 3))
            out += line * scale
    return cols * W * scale, rows * W * scale, bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="/tmp/pface")
    ap.add_argument("--style", choices=STYLES, default="vortex")
    ap.add_argument("--seed", type=int, default=20261005)
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / "driver.c", Path(tmp) / "driver"
        src.write_text(DRIVER)
        subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-I", str(ROOT / "main"), str(src),
                        str(ROOT / "main/rig_pface.c"), "-lm", "-o", str(exe)], check=True)

        def run(script, shots):
            done = subprocess.run([str(exe), str(STYLES[args.style]), str(args.seed)], input=script.encode(),
                                  capture_output=True, check=True).stdout
            size = W * W * 3
            assert len(done) == shots * size, (len(done), shots)
            return [done[i * size:(i + 1) * size] for i in range(shots)]

        script = "run 0.1\n" + "".join(f"face {f}\nrun 4.5\nsnap\n" for f in FACES)
        w, h, rgb = sheet(run(script, len(FACES)), 4, args.scale)
        png(out / "faces.png", w, h, rgb)
        print("faces.png:", " ".join(FACES), "(4 per row)")

        for a, b in TRANSITIONS:
            script, last = f"face {a}\nrun 4.5\nface {b}\n", 0.0
            for t in TIMES:
                script += f"run {t - last:.3f}\nsnap\n"
                last = t
            w, h, rgb = sheet(run(script, len(TIMES)), 4, args.scale)
            png(out / f"trans_{a}_{b}.png", w, h, rgb)
            print(f"trans_{a}_{b}.png: t =", " ".join(f"{t}s" for t in TIMES))

        script = "run 4.5\nconnected 0\nrun 2\nsnap\nconnected 1\nrun 2\nsnap\n"
        w, h, rgb = sheet(run(script, 2), 2, args.scale)
        png(out / "status.png", w, h, rgb)
        print("status.png: offline (orange dot), online (green dot)")
    print("written to", out)


if __name__ == "__main__":
    sys.exit(main())
