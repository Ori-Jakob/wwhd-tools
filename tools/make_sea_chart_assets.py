"""Builds data/sea_chart.bin and the sea chart icons. Needs Pillow and NumPy.

Usage: python tools/make_sea_chart_assets.py [chart.png] [link_icon.png] [boat_icon.png]
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "data")
PICTURES = os.path.join(os.path.expanduser("~"), "Pictures")

GRID = 7
TILE = 160
OVERVIEW = 448
ICON = 96


def find_lines(profile, count, first_guess, step_guess):
    lines = []
    for k in range(count):
        expect = first_guess + k * step_guess
        lo = max(0, int(expect - 30))
        hi = min(len(profile), int(expect + 30))
        window = profile[lo:hi]
        lines.append(lo + int(np.argmin(window)))
    return lines


def grid_lines(rgb):
    lum = rgb.astype(np.float64).sum(axis=2)
    cols = lum.mean(axis=0)
    rows = lum.mean(axis=1)
    size = rgb.shape[1]
    step = size / (GRID + 0.2)
    first = (size - step * GRID) / 2.0
    xs = find_lines(cols, GRID + 1, first, step)
    ys = find_lines(rows, GRID + 1, first, step)
    return xs, ys


def build_chart(path):
    src = Image.open(path).convert("RGB")
    xs, ys = grid_lines(np.asarray(src))
    print("grid columns", xs)
    print("grid rows   ", ys)
    side = TILE * GRID
    out = Image.new("RGB", (side, side))
    for r in range(GRID):
        for c in range(GRID):
            box = (xs[c], ys[r], xs[c + 1], ys[r + 1])
            cell = src.resize((TILE, TILE), Image.LANCZOS, box=box)
            out.paste(cell, (c * TILE, r * TILE))
    return out


def palette_words(pal_rgb, alphas=None):
    words = []
    for i in range(len(pal_rgb) // 3):
        r, g, b = pal_rgb[i * 3:i * 3 + 3]
        a = 255 if alphas is None else alphas[i]
        words.append(bytes((a, b, g, r)))
    return b"".join(words)


def write_chart(chart, out_path):
    quant = chart.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.Dither.NONE)
    pal = quant.getpalette()[:256 * 3]
    count = len(pal) // 3

    overview = chart.resize((OVERVIEW, OVERVIEW), Image.LANCZOS)
    overview_q = overview.quantize(palette=quant, dither=Image.Dither.NONE)

    full = np.asarray(quant, dtype=np.uint8)
    blob = bytearray()
    blob += b"WSC1"
    blob += struct.pack(">HHHH", TILE, GRID, OVERVIEW, count)
    blob += palette_words(pal[:count * 3])
    blob += np.asarray(overview_q, dtype=np.uint8).tobytes()
    for r in range(GRID):
        for c in range(GRID):
            tile = full[r * TILE:(r + 1) * TILE, c * TILE:(c + 1) * TILE]
            blob += np.ascontiguousarray(tile).tobytes()
    with open(out_path, "wb") as f:
        f.write(blob)
    print("wrote %s (%d bytes, %d colours)" % (out_path, len(blob), count))


def bleed(rgba):
    # Fill transparent pixels with neighbour colours so filtering does not darken the edges.
    arr = np.asarray(rgba).astype(np.int32).copy()
    alpha = arr[:, :, 3]
    known = alpha > 0
    for _ in range(8):
        if known.all():
            break
        acc = np.zeros(arr.shape[:2] + (3,), np.int64)
        cnt = np.zeros(arr.shape[:2], np.int64)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                sk = np.roll(np.roll(known, dy, 0), dx, 1)
                sc = np.roll(np.roll(arr[:, :, :3], dy, 0), dx, 1)
                acc += sc * sk[:, :, None]
                cnt += sk
        grow = (~known) & (cnt > 0)
        arr[grow, :3] = (acc[grow] // cnt[grow][:, None])
        known = known | grow
    return Image.fromarray(arr.astype(np.uint8), "RGBA")


def build_icon(path, out_path):
    src = Image.open(path).convert("RGBA")
    bbox = src.getchannel("A").point(lambda a: 255 if a > 16 else 0).getbbox()
    if bbox:
        src = src.crop(bbox)
    w, h = src.size
    side = max(w, h)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(src, ((side - w) // 2, (side - h) // 2))
    icon = square.convert("RGBa").resize((ICON, ICON), Image.LANCZOS).convert("RGBA")
    icon = bleed(icon)

    quant = icon.quantize(colors=256, method=Image.FASTOCTREE, dither=Image.Dither.NONE)
    pal = quant.getpalette(rawmode="RGBA")
    count = len(pal) // 4
    idx = np.asarray(quant, dtype=np.uint8)
    words = b"".join(bytes((pal[i * 4 + 3], pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4]))
                     for i in range(count))
    blob = struct.pack(">HHHH", ICON, ICON, count, 0) + words + idx.tobytes()
    with open(out_path, "wb") as f:
        f.write(blob)
    print("wrote %s (%d bytes, %d colours)" % (out_path, len(blob), count))


def main():
    chart = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PICTURES, "great_sea_chart.png")
    link = sys.argv[2] if len(sys.argv) > 2 else os.path.join(PICTURES, "link_icon.png")
    boat = sys.argv[3] if len(sys.argv) > 3 else os.path.join(PICTURES, "boat_icon.png")
    write_chart(build_chart(chart), os.path.join(DATA, "sea_chart.bin"))
    build_icon(link, os.path.join(DATA, "sea_link_icon.bin"))
    build_icon(boat, os.path.join(DATA, "sea_boat_icon.bin"))


if __name__ == "__main__":
    main()
