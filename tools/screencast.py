#!/usr/bin/env python3
"""Records short animated GIFs of the remote for the README (docs/images/*.gif).

Each scene is a list of steps: press a key (POST /api/key) or grab the screen
(GET /api/screen). The whole scene runs on an Android phone joined to the
adapter's Wi-Fi (adb), so frames come as fast as the board can send them;
every frame keeps its real time stamp, so the GIF plays at real speed.

    tools/screencast.py              # all scenes
    tools/screencast.py focus        # one scene

Needs adb, Pillow and a Cardputer in a camera (live values).
"""
import os
import shlex
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ADAPTER = "http://192.168.73.1"
W, H = 240, 135
SCALE = 2
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images")
REMOTE_DIR = "/data/local/tmp/ethair-cast"

# Steps: "k:<key>" presses a key, "s" grabs a frame, "s*N" grabs N frames.
SCENES = {
    "navigate": ["k:back", "k:back", "k:f", "k:back", "s*2",
                 "k:down", "s*3", "k:down", "s*3", "k:right", "s*3", "k:up", "s*3", "k:up", "s*3",
                 "k:left", "s*3", "k:enter", "s*3", "k:back", "s*2"],
    "focus": ["k:back", "k:f", "s*2",
              "k:right", "s", "k:right", "s", "k:right", "s", "k:right", "s",
              "k:up", "s*2", "k:down", "s*2",
              "k:space", "s*10"],
    "shutter": ["k:back", "k:s", "s*2",
                "k:right", "s*3", "k:right", "s*3", "k:left", "s*3", "k:left", "s*3",
                "k:back", "s"],
}
# Leave the camera as it was: undo the focus steps and AF cannot be undone,
# so the focus scene ends with an autofocus anyway.


def build_script(steps):
    lines = [
        f"mkdir -p {REMOTE_DIR}; rm -f {REMOTE_DIR}/*",
        "c() { curl -s -m 5 -H 'X-EthAir: 1' --interface wlan0 \"$@\"; }",
        f"k() {{ c -X POST \"{ADAPTER}/api/key?k=$1\" >/dev/null; }}",
        "n=0",
        f"s() {{ n=$((n+1)); c -o {REMOTE_DIR}/$n.raw {ADAPTER}/api/screen; "
        f"cut -d' ' -f1 /proc/uptime >> {REMOTE_DIR}/t.txt; }}",
    ]
    for st in steps:
        if st.startswith("k:"):
            lines.append(f"k {shlex.quote(st[2:])}")
        elif st.startswith("s*"):
            lines.extend(["s"] * int(st[2:]))
        elif st == "s":
            lines.append("s")
    return "\n".join(lines) + "\n"


def to_image(raw):
    img = Image.new("RGB", (W, H))
    px = img.load()
    for i in range(W * H):
        v = (raw[2 * i] << 8) | raw[2 * i + 1]
        px[i % W, i // W] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    return img.resize((W * SCALE, H * SCALE), Image.NEAREST)


def record(name, steps):
    script = build_script(steps)
    subprocess.run(["adb", "shell", script], check=True)
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["adb", "pull", REMOTE_DIR, d], check=True, stdout=subprocess.DEVNULL)
        src = os.path.join(d, os.path.basename(REMOTE_DIR))
        times = [float(t) for t in open(os.path.join(src, "t.txt")).read().split()]
        frames = []
        for i in range(1, len(times) + 1):
            raw = open(os.path.join(src, f"{i}.raw"), "rb").read()
            if len(raw) == W * H * 2:
                frames.append(to_image(raw))
    if not frames:
        raise SystemExit(f"{name}: no frames")
    # Real durations between captures; the last frame stays a bit longer.
    durations = [max(60, int((b - a) * 1000)) for a, b in zip(times, times[1:])] + [1500]
    durations = durations[: len(frames)]
    # Rounded corners like the display window (transparent colour index).
    mask = Image.new("L", frames[0].size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, mask.size[0] - 1, mask.size[1] - 1], radius=12, fill=255)
    pal = []
    for f in frames:
        p = f.convert("P", palette=Image.ADAPTIVE, colors=255)
        p.paste(255, mask=Image.eval(mask, lambda a: 255 - a))
        pal.append(p)
    path = os.path.join(OUT, f"{name}.gif")
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=durations, loop=0,
                transparency=255, disposal=1, optimize=False)
    print(f"saved {os.path.relpath(path)}: {len(frames)} frames, {sum(durations) / 1000:.1f} s")


def main():
    names = sys.argv[1:] or list(SCENES)
    for n in names:
        record(n, SCENES[n])


if __name__ == "__main__":
    main()
