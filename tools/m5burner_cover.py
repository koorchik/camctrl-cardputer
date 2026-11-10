#!/usr/bin/env python3
"""Cover image for the M5Burner listing (docs/m5burner/cover.png), 800 x 450.

    tools/m5burner_cover.py OUT.png "Title" "accented end of title" "line|line|line" SCREENSHOT.png

Needs Pillow and the Noto Sans fonts (fonts-noto-core).
"""
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
out, title, accent_part, subtitle, shot = sys.argv[1:6]
W, H = 800, 450
img = Image.new("RGBA", (W, H), (13, 15, 18, 255))
d = ImageDraw.Draw(img)
# soft amber glow behind the screen
glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse([380, 60, 820, 420], fill=(255, 191, 0, 60))
img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(80)))
d = ImageDraw.Draw(img)
F = "/usr/share/fonts/truetype/noto/"
big = ImageFont.truetype(F + "NotoSans-Bold.ttf", 54)
mid = ImageFont.truetype(F + "NotoSans-Regular.ttf", 22)
x, y = 40, 60
first = title[: len(title) - len(accent_part)] if accent_part else title
d.text((x, y), first, font=big, fill=(232, 235, 239))
if accent_part:
    w = d.textlength(first, font=big)
    d.text((x + w, y), accent_part, font=big, fill=(255, 191, 0))
yy = 140
for line in subtitle.split("|"):
    d.text((x, yy), line, font=mid, fill=(154, 164, 177))
    yy += 32
s = Image.open(shot).convert("RGBA")
s = s.resize((int(s.width * 0.52), int(s.height * 0.52)), Image.LANCZOS)
sx, sy = W - s.width - 28, H - s.height - 34
shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(shadow).rounded_rectangle([sx + 6, sy + 10, sx + s.width + 6, sy + s.height + 10], 16, fill=(0, 0, 0, 160))
img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(10)))
img.alpha_composite(s, (sx, sy))
img.convert("RGB").save(out, optimize=True)
print("saved", out)
