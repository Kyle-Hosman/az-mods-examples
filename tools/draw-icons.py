#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draw the mods' 256x256 icons as plain PNGs (IHDR, IDAT, IEND only).

    python3 tools/draw-icons.py          writes <mod>/icon.png for every example

Pure Python, no imaging library: each pixel is the coverage of a few shapes, sampled 4x4.
"""
import math
import os
import struct
import zlib

SIZE = 256
SS = 4                                  # samples per pixel, per axis

BG = (30, 31, 36)
WHITE = (240, 240, 240)
ORANGE = (242, 140, 40)
BLUE = (79, 163, 247)
DIM = (92, 96, 108)


def rounded_rect(x0, y0, x1, y1, r):
    def inside(x, y):
        cx = min(max(x, x0 + r), x1 - r)
        cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return inside


def ring(cx, cy, r_out, r_in):
    def inside(x, y):
        d = (x - cx) ** 2 + (y - cy) ** 2
        return r_in * r_in <= d <= r_out * r_out
    return inside


def capsule(ax, ay, bx, by, r):
    """A line segment from a to b with round ends, r thick on each side."""
    def inside(x, y):
        dx, dy = bx - ax, by - ay
        t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)
        t = min(max(t, 0.0), 1.0)
        px, py = ax + t * dx, ay + t * dy
        return (x - px) ** 2 + (y - py) ** 2 <= r * r
    return inside


def render(layers):
    """layers: [(shape, rgb)], painted in order over a transparent canvas."""
    rows = []
    for py in range(SIZE):
        row = bytearray([0])            # filter type 0 for every scanline
        for px in range(SIZE):
            r = g = b = a = 0.0
            for sy in range(SS):
                for sx in range(SS):
                    x = px + (sx + 0.5) / SS
                    y = py + (sy + 0.5) / SS
                    col = None
                    for shape, rgb in layers:
                        if shape(x, y):
                            col = rgb
                    if col:
                        r += col[0]; g += col[1]; b += col[2]; a += 1
            n = SS * SS
            if a:
                row += bytes((round(r / a), round(g / a), round(b / a), round(255 * a / n)))
            else:
                row += bytes((0, 0, 0, 0))
        rows.append(bytes(row))
    return b"".join(rows)


def png(raw):
    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)    # 8-bit RGBA, not interlaced
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def tile():
    return (rounded_rect(8, 8, 248, 248, 44), BG)


def set_clock():
    c = SIZE / 2
    layers = [tile(), (ring(c, c, 92, 76), ORANGE)]
    for k in range(12):                 # hour marks, longer at 12, 3, 6 and 9
        a = k * math.pi / 6
        r0 = 52 if k % 3 == 0 else 60
        layers.append((capsule(c + r0 * math.sin(a), c - r0 * math.cos(a),
                               c + 66 * math.sin(a), c - 66 * math.cos(a), 3.5), DIM))
    hour, minute = math.radians(300), math.radians(60)     # ten past ten
    layers.append((capsule(c, c, c + 38 * math.sin(hour), c - 38 * math.cos(hour), 8), WHITE))
    layers.append((capsule(c, c, c + 58 * math.sin(minute), c - 58 * math.cos(minute), 6), WHITE))
    layers.append((ring(c, c, 11, 0), ORANGE))
    return layers


def deck_overview():
    layers = [tile()]
    lengths = (0.85, 0.6, 0.95, 0.45)
    for i, frac in enumerate(lengths):
        y = 62 + i * 44
        accent = BLUE if i == 1 else DIM
        layers.append((rounded_rect(44, y, 76, y + 32, 8), accent))
        x1 = 92 + (212 - 92) * frac
        layers.append((rounded_rect(92, y + 6, x1, y + 26, 10), WHITE if i == 1 else DIM))
    return layers


def triangle(ax, ay, bx, by, cx, cy):
    def inside(x, y):
        d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by)
        d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy)
        d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay)
        neg = d1 < 0 or d2 < 0 or d3 < 0
        pos = d1 > 0 or d2 > 0 or d3 > 0
        return not (neg and pos)
    return inside


def surface_demo():
    """The deck's screen, with a small panel of your own over its corner."""
    return [tile(), (rounded_rect(40, 64, 216, 192, 14), DIM),
            (rounded_rect(56, 96, 200, 104, 4), BG), (rounded_rect(56, 120, 176, 128, 4), BG),
            (rounded_rect(56, 144, 188, 152, 4), BG),
            (rounded_rect(132, 40, 232, 108, 14), BLUE), (rounded_rect(150, 66, 214, 82, 8), WHITE)]


def deck_events():
    """Play, then a notice: the events a mod hears about."""
    return [tile(), (triangle(60, 70, 60, 150, 124, 110), WHITE),
            (rounded_rect(140, 132, 220, 196, 16), ORANGE), (rounded_rect(158, 156, 202, 170, 7), BG)]


def library_browser():
    """A folder and the tracks in it."""
    layers = [tile(), (rounded_rect(40, 52, 96, 66, 6), ORANGE), (rounded_rect(40, 60, 128, 100, 10), ORANGE)]
    for i in range(3):
        y = 124 + i * 36
        layers.append((ring(58, y + 10, 9, 0), DIM))
        layers.append((rounded_rect(80, y + 2, 216 - i * 28, y + 18, 8), WHITE if i == 0 else DIM))
    return layers


def level_meter():
    """Four deck meters at different levels."""
    layers = [tile()]
    for i, frac in enumerate((0.55, 0.85, 0.35, 0.7)):
        x = 52 + i * 42
        layers.append((rounded_rect(x, 48, x + 26, 208, 8), DIM))
        top = 208 - 160 * frac
        layers.append((rounded_rect(x, top, x + 26, 208, 8), ORANGE if frac > 0.8 else BLUE))
    return layers


ICONS = (("set-clock", set_clock), ("deck-overview", deck_overview), ("surface-demo", surface_demo),
         ("deck-events", deck_events), ("library-browser", library_browser), ("level-meter", level_meter))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for mod, draw in ICONS:
        layers = draw()
        path = os.path.join(root, mod, "icon.png")
        with open(path, "wb") as f:
            f.write(png(render(layers)))
        print(path)


if __name__ == "__main__":
    main()
