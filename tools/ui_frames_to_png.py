#!/usr/bin/env python3
"""Convert ui_preview's raw RGB565 frames to PNGs, masked to the round screen."""
import glob
import os
import sys

from PIL import Image, ImageDraw

W = H = 466
d = sys.argv[1] if len(sys.argv) > 1 else "."
mask = Image.new("L", (W, H), 0)
ImageDraw.Draw(mask).ellipse((0, 0, W - 1, H - 1), fill=255)
for path in sorted(glob.glob(os.path.join(d, "*.raw"))):
    raw = open(path, "rb").read()
    px = bytearray()
    for i in range(0, len(raw), 2):
        v = raw[i] | (raw[i + 1] << 8)
        px += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    img = Image.frombytes("RGB", (W, H), bytes(px))
    out = Image.new("RGB", (W, H), (40, 40, 40))
    out.paste(img, (0, 0), mask)
    out.save(path[:-4] + ".png")
    print(path[:-4] + ".png")
