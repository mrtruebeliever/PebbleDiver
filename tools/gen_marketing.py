#!/usr/bin/env python3
"""Generates PebbleDiver's Rebble appstore marketing art: the two store icons
and the listing banner. Pixel-art style matching the in-game diver sprite
(see draw_diver() in src/c/main.c) on a deep-ocean vertical gradient, plus a
glowing magic Pebble teaser (see the endgame's color-shifting magic Pebble).

Outputs:
  store/icon-80.png            80x80    diver + magic-pebble teaser
  store/icon-144.png           144x144  same composition, larger
  store/banner-720x320.png     720x320  wide scene + title/subtitle text

Rendered supersampled and downsampled for clean edges. Needs Pillow; run
from the project root inside the icon venv, or with the pebble-tool venv
python (it already has Pillow):
    ~/.local/share/uv/tools/pebble-tool/bin/python tools/gen_marketing.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont, ImageFilter

SS = 4  # supersampling factor for the icons

# Colors matching the in-game palette (Pebble's 64-color RGB values).
ORANGE = (0xFF, 0xAA, 0x00, 255)       # body
TAN = (0xAA, 0x55, 0x00, 255)          # fin
LIGHT_GRAY = (0xAA, 0xAA, 0xAA, 255)   # tank
BRASS = (0xAA, 0xAA, 0x55, 255)        # helmet
OXFORD_BLUE = (0x00, 0x00, 0x55, 255)  # visor
WHITE = (255, 255, 255, 255)
MAGENTA = (0xFF, 0x00, 0xFF, 255)      # magic Pebble

SKY_BLUE = (0x55, 0xB6, 0xE8)          # gradient top (shallow water)
DEEP_BLUE = (0x03, 0x08, 0x22)         # gradient bottom (deep water)

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def vertical_gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        row = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = row
    return img.convert("RGBA")


def draw_diver(canvas, cx, cy, scale):
    """Draws the diver sprite centered at (cx, cy), `scale` px tall (roughly)."""
    d = ImageDraw.Draw(canvas)

    def s(v):
        return v * scale

    # Fin (back, lower-left).
    d.rounded_rectangle([cx - s(0.34), cy + s(0.06), cx - s(0.16), cy + s(0.24)],
                         radius=s(0.03), fill=TAN)
    # Air tank.
    d.rounded_rectangle([cx - s(0.20), cy - s(0.34), cx + s(0.06), cy - s(0.14)],
                         radius=s(0.05), fill=LIGHT_GRAY)
    # Body.
    d.rounded_rectangle([cx - s(0.22), cy - s(0.14), cx + s(0.18), cy + s(0.20)],
                         radius=s(0.10), fill=ORANGE)
    # Helmet.
    hx, hy, hr = cx + s(0.20), cy - s(0.10), s(0.19)
    d.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill=BRASS)
    # Visor.
    vr = s(0.075)
    d.ellipse([hx + s(0.03) - vr, hy - vr, hx + s(0.03) + vr, hy + vr], fill=OXFORD_BLUE)


def draw_bubble(canvas, x, y, r, width=2):
    d = ImageDraw.Draw(canvas)
    d.ellipse([x - r, y - r, x + r, y + r], outline=WHITE, width=width)


def draw_magic_pebble(canvas, x, y, r):
    """A small glowing magenta pebble with a soft glow halo and a white
    sparkle ring, teasing the endgame's color-shifting magic Pebble."""
    W, H = canvas.size
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gr = r * 2.6
    gd.ellipse([x - gr, y - gr, x + gr, y + gr], fill=(*MAGENTA[:3], 130))
    glow = glow.filter(ImageFilter.GaussianBlur(r * 0.9))
    canvas.alpha_composite(glow)

    d = ImageDraw.Draw(canvas)
    d.ellipse([x - r, y - r, x + r, y + r], fill=MAGENTA)
    hl = r * 0.35
    d.ellipse([x - r * 0.35 - hl * 0.5, y - r * 0.35 - hl * 0.5,
               x - r * 0.35 + hl * 0.5, y - r * 0.35 + hl * 0.5], fill=WHITE)

    # Sparkle flares (four-pointed twinkle), not a full ring, so it doesn't
    # read as a reticle.
    def flare(fx, fy, length, lw):
        d.line([(fx - length, fy), (fx + length, fy)], fill=WHITE, width=lw)
        d.line([(fx, fy - length), (fx, fy + length)], fill=WHITE, width=lw)

    flare(x + r * 1.9, y - r * 1.4, r * 0.55, max(1, int(r * 0.10)))
    flare(x - r * 1.6, y + r * 1.7, r * 0.32, max(1, int(r * 0.07)))
    d.ellipse([x + r * 2.6 - r * 0.1, y - r * 0.3 - r * 0.1,
               x + r * 2.6 + r * 0.1, y - r * 0.3 + r * 0.1], fill=WHITE)


def store_icon(size):
    W = size * SS
    canvas = vertical_gradient(W, W, SKY_BLUE, DEEP_BLUE)

    # A few ambient bubbles.
    for bx, by, br in ((0.18, 0.15, 0.03), (0.28, 0.30, 0.02), (0.15, 0.42, 0.018)):
        draw_bubble(canvas, bx * W, by * W, br * W, width=max(1, int(W * 0.006)))

    draw_diver(canvas, 0.42 * W, 0.58 * W, 0.62 * W)
    draw_magic_pebble(canvas, 0.78 * W, 0.24 * W, 0.09 * W)

    return canvas.resize((size, size), Image.LANCZOS)


def banner():
    W, H = 720, 320
    canvas = vertical_gradient(W, H, SKY_BLUE, DEEP_BLUE)

    # Ambient bubbles trailing up from the diver.
    for bx, by, br in ((85, 55, 7), (120, 90, 5), (70, 130, 4), (145, 35, 4),
                        (45, 85, 4), (105, 175, 4)):
        draw_bubble(canvas, bx, by, br, width=2)

    draw_diver(canvas, 110, 200, 210)
    draw_magic_pebble(canvas, 600, 250, 30)

    d = ImageDraw.Draw(canvas)
    title_font = ImageFont.truetype(FONT_PATH, 54)
    subtitle_font = ImageFont.truetype(FONT_PATH, 22)

    title = "PebbleDiver"
    subtitle = "A one-button deep-sea dive"
    text_cx = 500  # right-of-center column, clear of the diver sprite

    tb = d.textbbox((0, 0), title, font=title_font)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    sb = d.textbbox((0, 0), subtitle, font=subtitle_font)
    sw, sh = sb[2] - sb[0], sb[3] - sb[1]

    block_h = th + 14 + sh
    ty = H / 2 - block_h / 2 - tb[1]
    tx = text_cx - tw / 2

    # Soft dark backing so the text stays legible over the gradient/bubbles.
    pad = 18
    backing = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    bd = ImageDraw.Draw(backing)
    bx0 = min(tx, text_cx - sw / 2) - pad
    bx1 = max(tx + tw, text_cx - sw / 2 + sw) + pad
    by0 = ty + tb[1] - pad
    by1 = ty + tb[1] + block_h + pad
    bd.rounded_rectangle([bx0, by0, bx1, by1], radius=14, fill=(0, 0, 0, 90))
    canvas.alpha_composite(backing)

    d.text((tx, ty), title, font=title_font, fill=WHITE)
    d.text((text_cx - sw / 2, ty + th + 14), subtitle, font=subtitle_font, fill=(0xDD, 0xEE, 0xFF, 255))

    return canvas


def main():
    os.makedirs("store", exist_ok=True)

    for sz in (80, 144):
        store_icon(sz).save("store/icon-%d.png" % sz)
        print("wrote store/icon-%d.png (%dx%d)" % (sz, sz, sz))

    banner().save("store/banner-720x320.png")
    print("wrote store/banner-720x320.png (720x320)")


if __name__ == "__main__":
    main()
