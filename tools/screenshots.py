#!/usr/bin/env python3
"""Takes README screenshots from a running Cardputer.

Drives the remote with simulated keys (POST /api/key) and grabs the real
screen (GET /api/screen: 240 x 135 RGB565). Images go to docs/images/ at 3x
with rounded corners.

    tools/screenshots.py            # a computer on the adapter's USB or Wi-Fi
    tools/screenshots.py --adb      # through an Android phone on its Wi-Fi (adb)

Needs Pillow. Plug the Cardputer into a camera first: the screens show live
camera values.
"""
import argparse
import os
import shlex
import subprocess
import sys
import tempfile
import time

from PIL import Image, ImageDraw

ADAPTER = "http://192.168.73.1"
W, H = 240, 135
SCALE = 3
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images")

# (file name, keys to press first, wait before the capture in seconds).
# The detail views close after 3 s idle, so captures follow quickly.
SHOTS = [
    ("overview", ["back", "back"], 1.0),
    ("focus", ["f"], 0.8),
    ("autofocus", ["space"], 0.35),
    ("iris", ["i"], 0.8),
    ("shutter", ["s"], 0.8),
    ("iso", ["o"], 0.8),
    ("white-balance", ["w"], 0.8),
    ("tint", ["t"], 0.8),
    ("clean-feed", ["c"], 0.8),
    ("help", ["back", "h"], 0.5),
    ("intro-autofocus", ["h", "enter"], 0.6),  # Wi-Fi page -> replay the intro
    ("intro-keys", ["enter"], 0.6),
]


class Transport:
    def __init__(self, adb):
        self.adb = adb

    def _curl(self, args):
        base = ["curl", "-s", "-m", "15", "-H", "X-EthAir: 1"]
        if self.adb:
            cmd = ["adb", "shell", " ".join(shlex.quote(a) for a in base + ["--interface", "wlan0"] + args)]
        else:
            cmd = base + args
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

    def key(self, name):
        self._curl(["-X", "POST", f"{ADAPTER}/api/key?k={name}"])

    def screen(self):
        if self.adb:
            remote = "/data/local/tmp/ethair-screen.raw"
            self._curl(["-o", remote, f"{ADAPTER}/api/screen"])
            with tempfile.TemporaryDirectory() as d:
                local = os.path.join(d, "s.raw")
                subprocess.run(["adb", "pull", remote, local], check=True, stdout=subprocess.DEVNULL)
                return open(local, "rb").read()
        return subprocess.run(["curl", "-s", "-m", "15", "-H", "X-EthAir: 1", f"{ADAPTER}/api/screen"],
                              check=True, capture_output=True).stdout


def to_image(raw):
    if len(raw) != W * H * 2:
        raise SystemExit(f"unexpected screen size {len(raw)} bytes")
    img = Image.new("RGB", (W, H))
    px = img.load()
    for i in range(W * H):
        v = (raw[2 * i] << 8) | raw[2 * i + 1]  # RGB565, big-endian
        px[i % W, i // W] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    return img


def framed(img):
    """3x pixels, rounded corners like the real display window."""
    big = img.resize((W * SCALE, H * SCALE), Image.NEAREST).convert("RGBA")
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, big.size[0] - 1, big.size[1] - 1], radius=18, fill=255)
    big.putalpha(mask)
    return big


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adb", action="store_true", help="go through an Android phone (adb) on the adapter's Wi-Fi")
    ap.add_argument("--only", nargs="*", help="take only these shots")
    args = ap.parse_args()
    t = Transport(args.adb)
    os.makedirs(OUT, exist_ok=True)
    for name, keys, wait in SHOTS:
        for k in keys:
            t.key(k)
            time.sleep(0.15)
        time.sleep(wait)
        raw = t.screen()
        if args.only and name not in args.only:
            continue
        path = os.path.join(OUT, f"{name}.png")
        framed(to_image(raw)).save(path, optimize=True)
        print("saved", os.path.relpath(path))
    t.key("back")  # leave the intro / any open view


if __name__ == "__main__":
    sys.exit(main())
