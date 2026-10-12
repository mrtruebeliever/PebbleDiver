#!/usr/bin/env python3
"""Generates PebbleDiver's launcher menu icon: the brass diving helmet
with the wide-eyed diver behind its porthole, matching tools/sprites.py.

The launcher draws menu icons in four greys (black, dark grey, light grey,
white) on white, so the icon is crisp pixel art in Pebble palette colours
chosen for their grey level, with every part outlined in black; pale colours
without an outline would vanish.

Output:
  resources/images/menu_icon.png   25x25 on transparent

Run from the project root (needs Pillow):  python3 tools/gen_icon.py
"""
import os

from PIL import Image

N = 25
PAL = {
    "K": (0x00, 0x00, 0x00), "W": (0xFF, 0xFF, 0xFF), "L": (0xAA, 0xAA, 0xAA),
    "D": (0x55, 0x55, 0x55), "R": (0xFF, 0x00, 0x00), "r": (0xAA, 0x00, 0x00),
    "o": (0xFF, 0x55, 0x55), "Y": (0xFF, 0xFF, 0x00), "y": (0xFF, 0xAA, 0x00),
    "i": (0xFF, 0xFF, 0x55), "A": (0xAA, 0xAA, 0x55), "a": (0x55, 0x55, 0x00),
    "S": (0xFF, 0xAA, 0x55), "s": (0xFF, 0x55, 0x00), "N": (0xAA, 0x55, 0x00),
    "n": (0x55, 0x00, 0x00), "b": (0x00, 0x00, 0x55), "B": (0x00, 0x55, 0xAA),
    "c": (0x00, 0x55, 0xFF), "C": (0xAA, 0xFF, 0xFF), "E": (0x00, 0xFF, 0xAA),
    "e": (0x00, 0xAA, 0x55), "p": (0x00, 0x55, 0x00), "M": (0x55, 0xAA, 0x00),
    "Z": (0xAA, 0xFF, 0x55), "T": (0xAA, 0xFF, 0xAA), "m": (0xAA, 0x00, 0xAA),
    "P": (0x00, 0xAA, 0x00), "G": (0x55, 0xAA, 0xAA), "g": (0x00, 0x55, 0x55),
}


def disc(cx, cy, r):
    return {(x, y) for y in range(N) for x in range(N) if (x - cx) ** 2 + (y - cy) ** 2 <= r * r}


def ell(cx, cy, rx, ry):
    return {(x, y) for y in range(N) for x in range(N)
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1}


def rect(x0, y0, x1, y1):
    return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}


def part(g, cells, shade, diag=False):
    nb = [(1, 0), (-1, 0), (0, 1), (0, -1)] + ([(1, 1), (-1, -1), (1, -1), (-1, 1)] if diag else [])
    for (x, y) in cells:
        for dx, dy in nb:
            p = (x + dx, y + dy)
            if p not in cells and 0 <= p[0] < N and 0 <= p[1] < N:
                g[p[1]][p[0]] = "K"
    for (x, y) in cells:
        if 0 <= x < N and 0 <= y < N:
            g[y][x] = shade(x, y) if callable(shade) else shade


def new():
    return [["."] * N for _ in range(N)]


def to_img(g):
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    for y in range(N):
        for x in range(N):
            if g[y][x] != ".":
                img.putpixel((x, y), PAL[g[y][x]] + (255,))
    return img



def art():
    g = new()
    cx, cy = 12, 12
    helm = disc(cx, cy, 11.3)
    part(g, helm, lambda x, y: "i" if (x - cx) + (y - cy) < -12 else
         ("A" if (x - cx) + 0.8 * (y - cy) < 6 else "a"))
    rim = disc(13, 12, 7.4)
    part(g, rim, "L")
    port = disc(13, 12, 5.8)
    part(g, port, "S")
    for (x, y), c in {(11, 10): "W", (12, 10): "W", (11, 11): "W", (12, 11): "K",
                      (11, 12): "W", (12, 12): "K", (15, 10): "W", (16, 10): "W",
                      (15, 11): "W", (16, 11): "K", (15, 12): "W", (16, 12): "K",
                      (13, 15): "N", (14, 15): "N", (10, 14): "o", (17, 14): "o",
                      (9, 9): "C", (10, 8): "C", (9, 10): "W",
                      }.items():
        g[y][x] = c
    for (x, y) in ((3, 12), (21, 12), (12, 2), (12, 22), (5, 5), (19, 19), (5, 19), (19, 5)):
        if g[y][x] in "Aia":
            g[y][x] = "D"
    return g


def main():
    os.makedirs("resources/images", exist_ok=True)
    to_img(art()).save("resources/images/menu_icon.png")
    print("wrote resources/images/menu_icon.png (25x25)")


if __name__ == "__main__":
    main()
