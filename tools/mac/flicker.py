#!/usr/bin/env python3
"""Flicker metric for a burst of window captures (tools/mac/prof3d.sh FLICKER=N).

usage: flicker.py <dir with NN.png>   ->  prints frame-to-frame change stats

Each capture is downscaled to 160 px wide (sips, BMP) and compared with the
previous one: mean absolute luminance difference (0-255) over the frame minus
the top-left overlay corner. With the camera still, normal animation (snow,
fire) stays low; a flashing frame shows up as a spike in both directions.
"""
import os
import struct
import subprocess
import sys
import tempfile


def luma(path, tmp):
    bmp = os.path.join(tmp, os.path.basename(path) + ".bmp")
    subprocess.run(["sips", "-Z", "160", "-s", "format", "bmp", path, "--out", bmp],
                   check=True, capture_output=True)
    b = open(bmp, "rb").read()
    off = struct.unpack_from("<I", b, 10)[0]
    w, h = struct.unpack_from("<ii", b, 18)
    bpp = struct.unpack_from("<H", b, 28)[0] // 8
    row = (w * bpp + 3) & ~3
    rows = []
    for y in range(abs(h)):
        r = b[off + y * row: off + y * row + w * bpp]
        rows.append([0.114 * r[x] + 0.587 * r[x + 1] + 0.299 * r[x + 2]
                     for x in range(0, w * bpp, bpp)])
    if h > 0:  # bottom-up BMP
        rows.reverse()
    return rows


def diff(a, b):
    h, w = len(a), len(a[0])
    total = n = 0
    for y in range(h):
        for x in range(w):
            if y < h // 6 and x < w // 3:  # FPS overlay
                continue
            total += abs(a[y][x] - b[y][x])
            n += 1
    return total / n


def main():
    d = sys.argv[1]
    files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    with tempfile.TemporaryDirectory() as tmp:
        frames = [luma(os.path.join(d, f), tmp) for f in files]
    diffs = [diff(frames[i - 1], frames[i]) for i in range(1, len(frames))]
    if not diffs:
        print("flicker: not enough captures")
        return
    s = sorted(diffs)
    med = s[len(s) // 2]
    spikes = sum(1 for v in diffs if v > max(8.0, 3 * med))
    print(f"flicker: n={len(diffs)} median={med:.1f} max={max(diffs):.1f} spikes={spikes}")


if __name__ == "__main__":
    main()
