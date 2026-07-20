#!/usr/bin/env python3
"""Generates PebbleDiver's launcher menu icon: a tiny pixel-art diver,
matching the in-game sprite's colors (see draw_diver() in src/c/main.c) —
an orange rounded body, a brass diving helmet with a dark visor, a gray air
tank, and a couple of trailing bubbles.

Output:
  resources/images/menu_icon.png   25x25   diver on transparent

Rendered supersampled and downsampled for clean edges. Needs Pillow; run
from the project root inside the icon venv:
    source .art-venv/bin/activate && python3 tools/gen_icon.py
"""
import os
from PIL import Image, ImageDraw

SS = 8

ORANGE = (0xFF, 0xAA, 0x00, 255)      # GColorOrange
TAN = (0xAA, 0x55, 0x00, 255)         # GColorWindsorTan (fins)
LIGHT_GRAY = (0xAA, 0xAA, 0xAA, 255)  # GColorLightGray (tank)
BRASS = (0xAA, 0xAA, 0x55, 255)       # GColorBrass (helmet)
OXFORD_BLUE = (0x00, 0x00, 0x55, 255) # GColorOxfordBlue (visor)
WHITE = (255, 255, 255, 255)


def draw_diver(W):
    """Draws the diver sprite filling a WxW transparent RGBA image."""
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def s(v):
        return v * W

    cx, cy = s(0.46), s(0.56)  # body center

    # Fin (back, lower-left).
    d.rounded_rectangle([cx - s(0.34), cy + s(0.06), cx - s(0.16), cy + s(0.24)],
                         radius=s(0.03), fill=TAN)
    # Air tank (above the body).
    d.rounded_rectangle([cx - s(0.20), cy - s(0.34), cx + s(0.06), cy - s(0.14)],
                         radius=s(0.05), fill=LIGHT_GRAY)
    # Body.
    d.rounded_rectangle([cx - s(0.22), cy - s(0.14), cx + s(0.18), cy + s(0.20)],
                         radius=s(0.10), fill=ORANGE)
    # Helmet (brass diving helmet, upper right).
    hx, hy, hr = cx + s(0.20), cy - s(0.10), s(0.19)
    d.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill=BRASS)
    # Visor.
    vr = s(0.075)
    d.ellipse([hx + s(0.03) - vr, hy - vr, hx + s(0.03) + vr, hy + vr], fill=OXFORD_BLUE)

    # Bubbles trailing up-right.
    for bx, by, br in ((s(0.82), s(0.20), s(0.055)), (s(0.70), s(0.06), s(0.035))):
        d.ellipse([bx - br, by - br, bx + br, by + br], outline=WHITE, width=max(1, int(s(0.012))))

    return img


def menu_icon(size):
    W = size * SS
    glyph = draw_diver(W)
    return glyph.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs("resources/images", exist_ok=True)

    menu_icon(25).save("resources/images/menu_icon.png")
    print("wrote resources/images/menu_icon.png (25x25)")


if __name__ == "__main__":
    main()
