#!/usr/bin/env python3
"""Pixel-art source for the diver and the sea creatures, in the chunky
black-outline style of Pebble Peaks' climber.

Edit the art here, then run:
    python3 tools/sprites.py                     # writes src/c/sprites.h
    python3 tools/sprites.py --preview out.png   # also a 6x preview sheet

Every sprite has a centre (cx, cy): the point the game positions and
collides on. Colours "1", "2", "3" are tint slots (light, mid, dark) that the
game fills in at draw time, so one shark serves both its calm and its
about-to-strike colours.
"""
import math
import os
import sys

# char -> (GColor name, RGB for the preview)
PALETTE = {
    "K": ("GColorBlack", (0x00, 0x00, 0x00)),
    "W": ("GColorWhite", (0xFF, 0xFF, 0xFF)),
    "L": ("GColorLightGray", (0xAA, 0xAA, 0xAA)),
    "D": ("GColorDarkGray", (0x55, 0x55, 0x55)),
    "Y": ("GColorYellow", (0xFF, 0xFF, 0x00)),
    "i": ("GColorIcterine", (0xFF, 0xFF, 0x55)),
    "A": ("GColorBrass", (0xAA, 0xAA, 0x55)),
    "a": ("GColorArmyGreen", (0x55, 0x55, 0x00)),
    "S": ("GColorRajah", (0xFF, 0xAA, 0x55)),
    "s": ("GColorOrange", (0xFF, 0x55, 0x00)),
    "y": ("GColorChromeYellow", (0xFF, 0xAA, 0x00)),
    "N": ("GColorWindsorTan", (0xAA, 0x55, 0x00)),
    "n": ("GColorBulgarianRose", (0x55, 0x00, 0x00)),
    "R": ("GColorRed", (0xFF, 0x00, 0x00)),
    "o": ("GColorMelon", (0xFF, 0xAA, 0xAA)),
    "b": ("GColorOxfordBlue", (0x00, 0x00, 0x55)),
    "c": ("GColorPictonBlue", (0x55, 0xAA, 0xFF)),
    "C": ("GColorCeleste", (0xAA, 0xFF, 0xFF)),
    "Z": ("GColorShockingPink", (0xFF, 0x55, 0xFF)),
    "z": ("GColorPurple", (0xAA, 0x00, 0xAA)),
    "v": ("GColorRichBrilliantLavender", (0xFF, 0xAA, 0xFF)),
    "P": ("GColorIslamicGreen", (0x00, 0xAA, 0x00)),
    "p": ("GColorDarkGreen", (0x00, 0x55, 0x00)),
    # Tint slots, filled in by the game (preview: the shark's calm colours).
    "1": ("GColorClear", (0xAA, 0xAA, 0xAA)),
    "2": ("GColorClear", (0x55, 0xAA, 0xAA)),
    "3": ("GColorClear", (0x00, 0x55, 0x55)),
}
TINTS = "123"


# ---------------------------------------------------------------------------
# Drawing kit: sprites are built from shapes, the way a pixel artist layers
# them. Every part gets its own black outline and is painted back to front,
# so parts stay separated by a dark line (the Sega look of Peaks' climber).
# ---------------------------------------------------------------------------
def canvas(w, h):
    return [["."] * w for _ in range(h)]


def disc(g, cx, cy, r):
    return {(x, y) for y in range(len(g)) for x in range(len(g[0]))
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r}


def ell(g, cx, cy, rx, ry):
    return {(x, y) for y in range(len(g)) for x in range(len(g[0]))
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1}


def rect(x0, y0, x1, y1):
    return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}


def part(g, cells, shade, outline="K"):
    w, h = len(g[0]), len(g)
    if outline:
        for (x, y) in cells:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                p = (x + dx, y + dy)
                if p not in cells and 0 <= p[0] < w and 0 <= p[1] < h:
                    g[p[1]][p[0]] = outline
    for (x, y) in cells:
        if 0 <= x < w and 0 <= y < h:
            g[y][x] = shade(x, y) if callable(shade) else shade


def put(g, pixels):
    for (x, y), c in pixels.items():
        g[y][x] = c


def rows(g):
    return ["".join(r) for r in g]


# ---------------------------------------------------------------------------
# The diver swims right: brass helmet with a big porthole (wide-eyed face
# inside), grey tank on the back, orange suit, yellow fins that kick up and
# down between the frames.
# ---------------------------------------------------------------------------
def diver(kick):
    g = canvas(31, 18)
    # Legs and fins (back leg first, darker); the fins tilt up or down.
    for ly, tilt, dark in ((8, -1 if kick == 0 else 1, True),
                           (11, 1 if kick == 0 else -1, False)):
        part(g, rect(6, ly, 9, ly + 1),
             lambda x, y, d=dark, ly=ly: "N" if d else ("s" if y == ly + 1 else "S"))
        fin = set()
        for x in range(0, 6):
            spread = (5 - x) // 2
            off = tilt * ((6 - x) // 2)
            fin |= rect(x, ly - spread + off, x, ly + 1 + spread + off)
        part(g, fin, lambda x, y, d=dark, ly=ly, tilt=tilt: ("N" if x > 1 else "n") if d else
             ("Y" if y <= ly + (6 - x) // 2 * tilt else "y"))
    # Air tank on the back
    part(g, rect(7, 2, 17, 5), lambda x, y: "W" if y == 2 and x < 15 else ("L" if y < 5 else "D"))
    # Suit
    part(g, rect(8, 6, 18, 11) | rect(9, 12, 17, 12),
         lambda x, y: "S" if y == 6 else ("s" if y < 11 else "N"))
    # Helmet
    cx, cy = 22.5, 7.5
    part(g, disc(g, cx, cy, 7.2), lambda x, y: "i" if (x - cx) + (y - cy) < -6 else
         ("A" if (x - cx) + 0.7 * (y - cy) < 3.2 else "a"))
    # Porthole with the face inside
    port = disc(g, 24, 7.5, 3.7)
    part(g, port, "S")
    face = {
        (24, 5): "W", (25, 5): "W", (24, 6): "W", (25, 6): "K",
        (24, 7): "W", (25, 7): "K", (26, 5): "S",
        (26, 9): "o", (27, 8): "o", (23, 10): "N", (24, 10): "N",
        (22, 5): "C", (21, 6): "C", (22, 6): "W",
    }
    put(g, {p: c for p, c in face.items() if p in port})
    # Rivets
    for (x, y) in ((17, 4), (17, 11), (21, 1)):
        if g[y][x] in "Aia":
            g[y][x] = "D"
    # Arm with glove, reaching forward under the helmet
    part(g, rect(13, 12, 17, 13), lambda x, y: "s" if y == 12 else "N")
    part(g, rect(18, 12, 19, 14), lambda x, y: "L" if y < 14 else "D")
    return rows(g)


# ---------------------------------------------------------------------------
# Hazards and pickups
# ---------------------------------------------------------------------------
def rock(r):
    """Lumpy boulder of radius r: lit top-left, a crack, a tuft of weed. The
    light body against a black rim reads on every water colour, shallow cyan
    to the deep Oxford blue."""
    n = 2 * r + 3
    g = canvas(n, n)
    c = n / 2 - 0.5
    cells = set()
    for y in range(n):
        for x in range(n):
            a = math.atan2(y - c, x - c)
            rr = r * (1 + 0.10 * math.sin(3 * a + r) + 0.06 * math.cos(5 * a))
            if (x - c) ** 2 + (y - c) ** 2 <= rr * rr:
                cells.add((x, y))

    def shade(x, y):
        lit = (x - c) + (y - c)
        if (x - c + r * 0.45) ** 2 + (y - c + r * 0.45) ** 2 < 3:
            return "W"
        return "L" if lit < r * 0.35 else "D"
    part(g, cells, shade)
    # crack
    for k in range(r // 2 + 1):
        x, y = int(c + 2 + k // 2), int(c - 2 + k)
        if (x, y) in cells:
            g[y][x] = "K"
    # weed on the bottom-left
    for (x, y) in ((int(c - r * 0.5), int(c + r * 0.6)), (int(c - r * 0.5) + 1, int(c + r * 0.6)),
                   (int(c - r * 0.5), int(c + r * 0.6) - 1)):
        if (x, y) in cells:
            g[y][x] = "P"
    return rows(g)


def jelly(frame):
    """Pink jellyfish with a cute face; tentacles sway between the frames.
    Centre = middle of the bell's bottom edge (the game's old circle centre)."""
    g = canvas(17, 19)
    # tentacles first, behind the bell
    for i, tx in enumerate((4, 8, 12)):
        cells = set()
        for k in range(9):
            sway = round(math.sin((k / 2.5) + i * 1.7 + frame * math.pi) * 1.2)
            cells.add((tx + sway, 9 + k))
        part(g, cells, "v")
    # bell
    bell = {(x, y) for (x, y) in ell(g, 8, 8, 7.4, 7.6) if y <= 8}
    part(g, bell, lambda x, y: "W" if (x - 5) ** 2 + (y - 3) ** 2 < 2.5 else
         ("Z" if y < 6 or x < 11 else "z"))
    # frilly rim
    part(g, rect(1, 9, 15, 9), lambda x, y: "z" if x % 2 else "Z")
    put(g, {(5, 5): "K", (5, 6): "K", (11, 5): "K", (11, 6): "K",
            (6, 5): "W", (12, 5): "W", (6, 6): "K", (12, 6): "K",
            (4, 7): "v", (13, 7): "v", (8, 7): "z"})
    return rows(g)


def shark(wag):
    """Swims left. Tint 1/2/3 = belly-side light, body, back dark; the game
    swaps them to reds when the shark is about to strike."""
    g = canvas(34, 20)
    cy = 11
    # forked tail (behind), its tips swinging up or down
    tail = set()
    for x in range(24, 34):
        k = x - 24
        up = cy - 1 - k * 0.8 + wag * k * 0.25
        dn = cy + k * 0.55 + wag * k * 0.25
        tail |= {(x, y) for y in range(int(round(up)) - 1, int(round(up)) + 2)}
        if k < 8:
            tail |= {(x, y) for y in range(int(round(dn)) - 1, int(round(dn)) + 2)}
    part(g, tail, lambda x, y: "2" if y < cy else "3")
    # dorsal fin
    fin = {(x, y) for y in range(1, 8) for x in range(14, 22) if x - 14 >= (7 - y) * 0.5 and x <= 20 - (7 - y) * 0.2}
    part(g, fin, "3")
    # pectoral fin (behind body, below)
    part(g, {(x, y) for y in range(15, 19) for x in range(13, 19) if x - 13 >= (y - 15)}, "3")
    # body: pointed nose left
    body = set()
    for x in range(2, 27):
        t = (x - 2) / 24
        half = 5.5 * math.sin(math.pi * min(1, t * 1.6) * 0.5) if t < 0.62 else 5.5 - (t - 0.62) * 9
        body |= {(x, y) for y in range(int(cy - half), int(cy + half * 0.8) + 1)}
    part(g, body, lambda x, y: "W" if y >= cy + 1 and x < 20 else ("1" if y == cy else ("2" if y > cy - 4 else "3")))
    # angry eye and brow, gills, toothy grin
    put(g, {(8, 8): "W", (9, 8): "W", (8, 9): "W", (9, 9): "K",
            (7, 7): "K", (8, 7): "K", (9, 6): "K", (10, 6): "K",
            (13, 9): "3", (13, 10): "3", (15, 9): "3", (15, 10): "3"})
    for x in range(4, 12):
        g[cy + 1][x] = "K"
    for x in (5, 7, 9, 11):
        g[cy + 2][x] = "W"
        g[cy + 2][x - 1] = "K" if g[cy + 2][x - 1] == "W" else g[cy + 2][x - 1]
    return rows(g)


PEARL = [
    "..KKKKK..",
    ".KWWWLLK.",
    "KWWWWWLLK",
    "KWWWWWWLK",
    "KWWWWWWLK",
    "KLWWWWLLK",
    "KLLWWLLDK",
    ".KLLLLDK.",
    "..KKKKK..",
]
BUBBLE = [
    "...CCCCC...",
    "..C.....C..",
    ".C.WW....C.",
    "C.W.......C",
    "C.W.......C",
    "C.........C",
    "C.........C",
    "C.........C",
    ".C.......C.",
    "..C.....C..",
    "...CCCCC...",
]

SPRITES = {
    "SWIM0": (diver(0), (16, 8)),
    "SWIM1": (diver(1), (16, 8)),
    "JELLY0": (jelly(0), (8, 8)),
    "JELLY1": (jelly(1), (8, 8)),
    "SHARK0": (shark(0), (17, 11)),
    "SHARK1": (shark(1), (17, 11)),
    "PEARL": (PEARL, (4, 4)),
    "BUBBLE": (BUBBLE, (5, 5)),
}
for r in range(9, 14):
    SPRITES["ROCK%d" % r] = (rock(r), (r + 1, r + 1))

ALL = {k: v[0] for k, v in SPRITES.items()}


def check():
    for name, rows_ in ALL.items():
        for i, row in enumerate(rows_):
            if len(row) != len(rows_[0]):
                sys.exit("%s row %d is %d wide, expected %d: %r"
                         % (name, i, len(row), len(rows_[0]), row))
            for ch in row:
                if ch != "." and ch not in PALETTE:
                    sys.exit("%s row %d: unknown colour %r" % (name, i, ch))


def runs(rows_):
    """Horizontal colour runs per row: (row, x, len, colour index)."""
    keys = list(PALETTE)
    out = []
    for y, row in enumerate(rows_):
        x = 0
        while x < len(row):
            ch = row[x]
            if ch == ".":
                x += 1
                continue
            n = 1
            while x + n < len(row) and row[x + n] == ch:
                n += 1
            out.append((y, x, n, keys.index(ch)))
            x += n
    return out


def write_header(path):
    keys = list(PALETTE)
    lines = [
        "// Generated by tools/sprites.py - edit the pixel art there, not here.",
        "#pragma once",
        "",
        "// One horizontal run of same-coloured pixels: row, x, length, colour.",
        "typedef struct { uint8_t y, x, len, col; } SpriteRun;",
        "typedef struct { const SpriteRun *runs; uint16_t count; uint8_t w, h, cx, cy; } Sprite;",
        "",
        "// Colours from SPRITE_TINT on are tint slots (light, mid, dark).",
        "#define SPRITE_TINT %d" % keys.index(TINTS[0]),
        "static GColor sprite_palette(uint8_t i) {",
        "  switch (i) {",
    ]
    for i, k in enumerate(keys):
        if k not in TINTS:
            lines.append("    case %d: return %s;" % (i, PALETTE[k][0]))
    lines += ["    default: return GColorClear;", "  }", "}", ""]
    for name, (rows_, (cx, cy)) in SPRITES.items():
        rs = runs(rows_)
        lines.append("static const SpriteRun RUNS_%s[] = {" % name)
        for y, x, n, c in rs:
            lines.append("  {%d, %d, %d, %d}," % (y, x, n, c))
        lines.append("};")
        lines.append("static const Sprite SPR_%s = { RUNS_%s, %d, %d, %d, %d, %d };"
                     % (name, name, len(rs), len(rows_[0]), len(rows_), cx, cy))
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def render(rows_, scale=1):
    from PIL import Image
    img = Image.new("RGBA", (len(rows_[0]) * scale, len(rows_) * scale), (0, 0, 0, 0))
    for y, row in enumerate(rows_):
        for x, ch in enumerate(row):
            if ch != ".":
                for j in range(scale):
                    for i in range(scale):
                        img.putpixel((x * scale + i, y * scale + j), PALETTE[ch][1] + (255,))
    return img


def preview(path, scale=5):
    """All sprites at 5x and 1x on shallow cyan and deep blue water."""
    from PIL import Image
    names = list(ALL)
    w = max(len(r[0]) for r in ALL.values())
    h = max(len(r) for r in ALL.values())
    cell_w, cell_h = (w + 2) * scale, (h + 2) * scale
    per = 7
    nrow = (len(names) + per - 1) // per
    img = Image.new("RGBA", (cell_w * per, cell_h * nrow * 2 + 2 * (h + 4)))
    for bi, bg in enumerate([(0x00, 0xAA, 0xFF), (0x00, 0x00, 0x55)]):
        img.paste(bg + (255,), (0, bi * cell_h * nrow, img.width, (bi + 1) * cell_h * nrow))
        sy = cell_h * nrow * 2 + bi * (h + 4)
        img.paste(bg + (255,), (0, sy, img.width, sy + h + 4))
        ox = 2
        for i, n in enumerate(names):
            img.alpha_composite(render(ALL[n], scale),
                                ((i % per) * cell_w + scale, (bi * nrow + i // per) * cell_h + scale))
            if ox + len(ALL[n][0]) < img.width:
                img.alpha_composite(render(ALL[n]), (ox, sy + 2))
            ox += len(ALL[n][0]) + 4
    img.save(path)


if __name__ == "__main__":
    check()
    here = os.path.dirname(os.path.abspath(__file__))
    write_header(os.path.join(here, "..", "src", "c", "sprites.h"))
    if "--preview" in sys.argv:
        preview(sys.argv[sys.argv.index("--preview") + 1])
