#!/usr/bin/env python3
"""Puts a screenshot onto the screen of M5Stack's Cardputer ADV product photo.

The photo is M5Stack's front view (docs/images/m5stack-cardputer-adv.webp,
from docs.m5stack.com); the screenshot replaces its screen content.

    tools/product_shot.py SCREEN.png OUT.png [--width 1200]

SCREEN.png is a 240 x 135 screenshot or a multiple of it (docs/images/*.png
are 3x with rounded corners). Needs Pillow.
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFilter

PHOTO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images", "m5stack-cardputer-adv.webp")
# In the 1200 x 1200 photo: the display window and the device with a margin.
SCREEN_BOX = (388, 362, 634, 500)
CROP = (150, 300, 1050, 900)
UPSCALE = 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("screen")
    ap.add_argument("out")
    ap.add_argument("--width", type=int, default=1200)
    args = ap.parse_args()

    photo = Image.open(PHOTO).convert("RGBA")
    photo = photo.resize((photo.width * UPSCALE, photo.height * UPSCALE), Image.LANCZOS)
    x0, y0, x1, y1 = (v * UPSCALE for v in SCREEN_BOX)
    w, h = x1 - x0, y1 - y0
    shot = Image.open(args.screen).convert("RGBA")
    black = Image.new("RGBA", shot.size, (0, 0, 0, 255))
    shot = Image.alpha_composite(black, shot).resize((w, h), Image.LANCZOS)
    photo.paste(shot, (x0, y0))
    out = photo.crop(tuple(v * UPSCALE for v in CROP))
    out = out.resize((args.width, int(args.width * out.height / out.width)), Image.LANCZOS)
    # The photo sits on white: make the background around the device
    # transparent (flood fill from the corners), so it works on dark pages.
    rgb = out.convert("RGB")
    marker = (255, 0, 255)
    for corner in ((0, 0), (rgb.width - 1, 0), (0, rgb.height - 1), (rgb.width - 1, rgb.height - 1)):
        ImageDraw.floodfill(rgb, corner, marker, thresh=18)
    alpha = rgb.point(lambda _: 255).convert("L")
    px, ap = rgb.load(), alpha.load()
    for y in range(rgb.height):
        for x in range(rgb.width):
            if px[x, y] == marker:
                ap[x, y] = 0
    out.putalpha(alpha.filter(ImageFilter.GaussianBlur(0.7)))
    out.save(args.out, optimize=True)
    print("saved", args.out)


if __name__ == "__main__":
    main()
