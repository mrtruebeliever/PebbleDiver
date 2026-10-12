#!/usr/bin/env python3
"""Generates PebbleDiver's Rebble appstore art from the in-game sprites
(tools/sprites.py), scaled up with NEAREST so it stays crisp pixel art, in
the same style as Pebble Peaks' store art.

Outputs:
  store/icon-80.png            80x80    (40x40 canvas, x2)
  store/icon-144.png           144x144  (36x36 canvas, x4)
  store/banner-720x320.png     720x320  (180x80 canvas, x4)

Run from the project root (needs Pillow):  python3 tools/gen_marketing.py
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sprites import ALL, PALETTE  # noqa: E402

C = {k: v[1] + (255,) for k, v in PALETTE.items()}
C.update({
    "j": (0xAA, 0x00, 0xAA, 255),   # Purple
    "M": (0xFF, 0x00, 0xFF, 255),   # Magenta (magic Pebble)
    "G": (0x00, 0xAA, 0x55, 255),   # JaegerGreen (kelp)
    "g": (0x00, 0x55, 0x00, 255),   # DarkGreen
})
# The game's water darkens with depth: these are its bands, top to bottom.
WATER = [(0x00, 0xAA, 0xFF), (0x00, 0x55, 0xFF), (0x00, 0x55, 0xAA),
         (0x00, 0x00, 0xAA), (0x00, 0x00, 0x55)]
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def water(img):
    w, h = img.size
    px = img.load()
    n = len(WATER)
    for y in range(h):
        t = (y / max(1, h - 1)) * (n - 1)
        i = min(int(t), n - 2)
        frac = t - i
        for x in range(w):
            # ordered 2x2 dither between neighbouring palette bands
            thr = ((x & 1) + 2 * (y & 1)) / 4.0 + 0.125
            px[x, y] = WATER[i + (1 if frac > thr else 0)] + (255,)


def blit(img, name, x, y, scale=1):
    for j, row in enumerate(ALL[name]):
        for i, ch in enumerate(row):
            if ch != ".":
                for sy in range(scale):
                    for sx in range(scale):
                        xx, yy = x + i * scale + sx, y + j * scale + sy
                        if 0 <= xx < img.width and 0 <= yy < img.height:
                            img.putpixel((xx, yy), C[ch])


def stamp(img, rows, x, y):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch != "." and 0 <= x + i < img.width and 0 <= y + j < img.height:
                img.putpixel((x + i, y + j), C[ch])


BUBBLE = [".WW.", "W..W", "W..W", ".WW."]
BUBBLE_S = [".W.", "W.W", ".W."]
MAGIC = [
    "..KKKKK..",
    ".KMMMMMK.",
    "KMWWMMMMK",
    "KMWMMMMMK",
    "KMMMMMMjK",
    "KMMMMMjjK",
    ".KMMMjjK.",
    "..KKKKK..",
]


def kelp(img, x, base, h, phase):
    for k in range(h):
        sway = (1 if ((k + phase) // 3) % 2 else 0)
        y = base - k
        for dx, c in ((0, "K"), (1, "G"), (2, "g"), (3, "K")):
            xx = x + sway + dx
            if 0 <= xx < img.width and 0 <= y < img.height:
                img.putpixel((xx, y), C[c])
    for dx in range(4):
        img.putpixel((x + dx + (1 if ((h - 1 + phase) // 3) % 2 else 0), base - h), C["K"])


def seabed(img, top):
    px = img.load()
    for y in range(top, img.height):
        for x in range(img.width):
            edge = top + (1 if (x // 7) % 3 == 0 else 0) + (1 if (x // 3) % 5 == 0 else 0)
            if y < edge:
                continue
            if y == edge:
                c = "K"
            elif y == edge + 1:
                c = "S" if x % 4 else "y"
            else:
                c = "N" if (x * 3 + y * 5) % 7 else "S"
            px[x, y] = C[c]


def rays(img, xs, top, bottom):
    """Faint light shafts: every other pixel lightened one band."""
    px = img.load()
    for x0 in xs:
        for y in range(top, bottom):
            for k in range(4):
                x = x0 + k + (y - top) // 3
                if 0 <= x < img.width and (x + y) % 2 == 0:
                    r, g, b, a = px[x, y]
                    px[x, y] = (min(255, r + 0x55), min(255, g + 0x55), b, 255)


def text(img, xy, s, size, fill, shade=None, shadow=(0, 0, 0x55, 255)):
    """Thresholded bold text with a black outline and a drop shadow."""
    W, H = img.size
    f = ImageFont.truetype(FONT, size)
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).text(xy, s, font=f, fill=255)
    mp = mask.point(lambda v: 255 if v > 127 else 0).load()
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ol = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    op, olp, shp = out.load(), ol.load(), sh.load()
    for y in range(H):
        for x in range(W):
            if mp[x, y]:
                op[x, y] = C[shade] if shade and y > xy[1] + size * 0.52 else C[fill]
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        if 0 <= x + dx < W and 0 <= y + dy < H:
                            olp[x + dx, y + dy] = C["K"]
    for y in range(H):
        for x in range(W):
            if olp[x, y][3] and x + 1 < W and y + 2 < H:
                shp[x + 1, y + 2] = shadow
    img.alpha_composite(sh)
    img.alpha_composite(ol)
    img.alpha_composite(out)


def icon_canvas(n):
    img = Image.new("RGBA", (n, n))
    water(img)
    rays(img, [n // 4, n // 2], 0, n)
    seabed(img, n - 6)
    kelp(img, n - 8, n - 6, n // 3, 0)
    blit(img, "SWIM0", (n - 31) // 2, n // 2 - 11)
    stamp(img, BUBBLE_S, n // 2 + 10, 3)
    stamp(img, BUBBLE, n // 2 + 13, 8)
    blit(img, "PEARL", 3, n - 14)
    return img


def banner():
    W, H = 180, 80
    img = Image.new("RGBA", (W, H))
    water(img)
    rays(img, [86, 112, 140, 165], 0, 62)
    seabed(img, 68)
    blit(img, "ROCK11", 90, 56)
    blit(img, "ROCK9", 162, 59)
    kelp(img, 88, 68, 22, 0)
    kelp(img, 114, 69, 14, 2)
    kelp(img, 150, 68, 26, 1)
    kelp(img, 174, 68, 18, 3)
    blit(img, "JELLY0", 158, 3)
    blit(img, "JELLY1", 96, 1)
    blit(img, "SHARK0", 138, 38)
    stamp(img, MAGIC, 120, 58)
    for (x, y) in ((119, 55), (130, 62), (123, 67)):
        img.putpixel((x, y), C["W"])
    blit(img, "SWIM0", 104, 22)
    for (x, y, big) in ((134, 18, False), (138, 11, True), (133, 5, False)):
        stamp(img, BUBBLE if big else BUBBLE_S, x, y)
    blit(img, "PEARL", 150, 26)
    blit(img, "PEARL", 172, 30)
    text(img, (6, 6), "PEBBLE", 17, "W", "L", shadow=(0, 0, 0x55, 255))
    text(img, (6, 22), "DIVER", 28, "y", "s", shadow=(0, 0, 0x55, 255))
    text(img, (8, 60), "Dive deep. Grab pearls.", 7, "W")
    text(img, (8, 69), "Mind the air!", 7, "C")
    return img


def main():
    os.makedirs("store", exist_ok=True)
    icon_canvas(40).resize((80, 80), Image.NEAREST).save("store/icon-80.png")
    icon_canvas(36).resize((144, 144), Image.NEAREST).save("store/icon-144.png")
    banner().resize((720, 320), Image.NEAREST).save("store/banner-720x320.png")
    print("wrote store/icon-80.png, icon-144.png, banner-720x320.png")


if __name__ == "__main__":
    main()
