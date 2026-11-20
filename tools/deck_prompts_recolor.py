"""Builds assets/deck_prompts/dark/ from the Steam Deck glyph pack's dark set.

The pack's dark glyphs are near-black buttons (14,20,27) with white letters,
which look harsh next to Fable II's art. This maps each pixel by where it sits
between those two colours onto #404040..white, so the buttons become dark grey
and the letters and anti-aliased edges stay as they are. Alpha is unchanged.

Usage: python tools/deck_prompts_recolor.py <pack dark folder>
       (the folder with sd_l1_lg.png, shared_button_a_lg.png, ...)
Needs Pillow. Only the glyphs already in assets/deck_prompts/dark/ are rebuilt.
"""
import sys
from pathlib import Path

from PIL import Image

BODY = (14, 20, 27)    # the pack's button colour
LETTER = (255, 255, 255)
TARGET = (64, 64, 64)  # #404040


def luma(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def recolor(src: Path, dst: Path):
    im = Image.open(src).convert("RGBA")
    lo, hi = luma(BODY), luma(LETTER)
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            t = min(max((luma((r, g, b)) - lo) / (hi - lo), 0.0), 1.0)
            px[x, y] = tuple(round(TARGET[i] + (LETTER[i] - TARGET[i]) * t) for i in range(3)) + (a,)
    im.save(dst)


def main():
    pack = Path(sys.argv[1])
    out = Path(__file__).resolve().parents[1] / "assets/deck_prompts/dark"
    for dst in sorted(out.glob("*.png")):
        recolor(pack / f"{dst.stem}_lg.png", dst)
        print(dst.name)


if __name__ == "__main__":
    main()
