#!/usr/bin/env python3
"""Draws a Cardputer-style handheld with a screenshot on its screen.

An original illustration (not a product photo): graphite body, the 240 x 135
screen, the 4 x 14 keyboard from src/logic/keymap.cpp and the side G0 button.
Keys the remote uses can be highlighted in amber.

    tools/device_mockup.py SCREEN.png OUT.png [--keys ";./,\\n h"] [--g0]

SCREEN.png is a 240 x 135 screenshot or any multiple of it (docs/images/*.png
are 3x with rounded corners). Needs Pillow and the Noto Sans fonts.
"""
import argparse

from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 2  # supersampling: draw at 2x, scale down at the end
FONT = "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf"

# Legends per key, same order as kLayout in src/logic/keymap.cpp.
ROWS = [
    ["`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "del"],
    ["tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\"],
    ["fn", "shift", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "enter"],
    ["ctrl", "opt", "alt", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "space"],
]
ARROWS = {";": "up", ".": "down", ",": "left", "/": "right"}

BODY_TOP = (52, 58, 66)
BODY_BOTTOM = (30, 34, 40)
KEY = (24, 27, 32)
KEY_EDGE = (62, 68, 78)
LEGEND = (205, 211, 219)
AMBER = (255, 191, 0)


def arrow(d, cx, cy, r, direction, fill):
    pts = {
        "up": [(cx - r, cy + r * 0.6), (cx + r, cy + r * 0.6), (cx, cy - r * 0.8)],
        "down": [(cx - r, cy - r * 0.6), (cx + r, cy - r * 0.6), (cx, cy + r * 0.8)],
        "left": [(cx + r * 0.6, cy - r), (cx + r * 0.6, cy + r), (cx - r * 0.8, cy)],
        "right": [(cx - r * 0.6, cy - r), (cx - r * 0.6, cy + r), (cx + r * 0.8, cy)],
    }[direction]
    d.polygon(pts, fill=fill)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("screen")
    ap.add_argument("out")
    ap.add_argument("--keys", default="", help="legends to highlight, space separated (e.g. '; . , / enter space h')")
    ap.add_argument("--g0", action="store_true", help="highlight the side G0 button")
    ap.add_argument("--width", type=int, default=1100, help="output width in pixels")
    args = ap.parse_args()
    hot = set(args.keys.split())

    kw, kh, gap = 68 * S, 58 * S, 8 * S
    kb_w = 14 * kw + 13 * gap
    pad = 44 * S
    scr_w, scr_h = 240 * 3 * S, 135 * 3 * S
    bezel = 22 * S
    body_w = kb_w + 2 * pad
    body_h = pad + scr_h + 2 * bezel + 36 * S + 4 * kh + 3 * gap + pad
    margin = 40 * S
    W, H = body_w + 2 * margin, body_h + 2 * margin + 20 * S
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    bx, by = margin, margin + 20 * S
    # Shadow.
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([bx + 10 * S, by + 24 * S, bx + body_w, by + body_h + 10 * S], 56 * S, fill=(0, 0, 0, 150))
    img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(22 * S)))

    # G0 button on the top edge, right side.
    d = ImageDraw.Draw(img)
    g0x = bx + body_w - 210 * S
    d.rounded_rectangle([g0x, by - 12 * S, g0x + 70 * S, by + 20 * S], 8 * S, fill=AMBER if args.g0 else KEY_EDGE)

    # Body with a vertical gradient.
    body = Image.new("RGBA", (body_w, body_h))
    bd = ImageDraw.Draw(body)
    for y in range(body_h):
        t = y / body_h
        bd.line([(0, y), (body_w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BODY_TOP, BODY_BOTTOM)) + (255,))
    mask = Image.new("L", (body_w, body_h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, body_w - 1, body_h - 1], 56 * S, fill=255)
    img.paste(body, (bx, by), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([bx, by, bx + body_w - 1, by + body_h - 1], 56 * S, outline=(80, 88, 99), width=2 * S)
    font = ImageFont.truetype(FONT, 15 * S)
    d.text((g0x + 35 * S, by + 34 * S), "G0", font=ImageFont.truetype(FONT, 13 * S),
           fill=AMBER if args.g0 else (120, 128, 140), anchor="mm")

    # Screen with bezel.
    sx = bx + (body_w - scr_w) // 2 - bezel
    sy = by + pad
    d.rounded_rectangle([sx, sy, sx + scr_w + 2 * bezel, sy + scr_h + 2 * bezel], 22 * S, fill=(6, 7, 9))
    shot = Image.open(args.screen).convert("RGBA").resize((scr_w, scr_h), Image.NEAREST)
    black = Image.new("RGBA", shot.size, (0, 0, 0, 255))
    shot = Image.alpha_composite(black, shot)  # transparent rounded corners -> black
    smask = Image.new("L", (scr_w, scr_h), 0)
    ImageDraw.Draw(smask).rounded_rectangle([0, 0, scr_w - 1, scr_h - 1], 10 * S, fill=255)
    img.paste(shot, (sx + bezel, sy + bezel), smask)

    # Keyboard.
    ky0 = sy + scr_h + 2 * bezel + 36 * S
    kx0 = bx + pad
    for r, row in enumerate(ROWS):
        for c, legend in enumerate(row):
            x = kx0 + c * (kw + gap)
            y = ky0 + r * (kh + gap)
            on = legend in hot
            d.rounded_rectangle([x, y + 3 * S, x + kw, y + kh + 3 * S], 10 * S, fill=(12, 14, 17))
            d.rounded_rectangle([x, y, x + kw, y + kh], 10 * S, fill=(58, 46, 10) if on else KEY,
                                outline=AMBER if on else KEY_EDGE, width=2 * S)
            colour = AMBER if on else LEGEND
            cx, cy = x + kw // 2, y + kh // 2
            if legend in ARROWS:
                arrow(d, cx - 8 * S, cy, 9 * S, ARROWS[legend], colour)
                d.text((x + kw - 14 * S, y + kh - 14 * S), legend, font=ImageFont.truetype(FONT, 13 * S), fill=colour, anchor="mm")
            else:
                f = font if len(legend) <= 2 else ImageFont.truetype(FONT, 12 * S)
                d.text((cx, cy), legend, font=f, fill=colour, anchor="mm")

    out = img.resize((args.width, int(args.width * H / W)), Image.LANCZOS)
    out.save(args.out, optimize=True)
    print("saved", args.out)


if __name__ == "__main__":
    main()
