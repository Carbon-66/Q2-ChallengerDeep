#!/usr/bin/env python3
"""The card's icons, drawn: a ship's wheel over a night sea, in a rounded square.
card/icon.png (52 px, Library's Games row) and card/icon-40.png (40 px). Usage: tools/icon.py"""
import math, pathlib
from PIL import Image, ImageDraw

def icon(size):
    S = size * 8  # drawn large, then scaled down smooth
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    bg = Image.new('RGB', (S, S))
    px = bg.load()
    for y in range(S):
        t = y / S
        top, bot = (14, 34, 58), (18, 92, 110)
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        for x in range(S):
            px[x, y] = c
    d = ImageDraw.Draw(bg)
    # a low moon and its path on the water
    d.ellipse([S * 0.62, S * 0.12, S * 0.84, S * 0.34], fill=(236, 226, 190))
    for k in range(6):
        y = S * (0.62 + k * 0.06)
        w = S * (0.05 + k * 0.012)
        d.rectangle([S * 0.73 - w, y, S * 0.73 + w, y + S * 0.012], fill=(150, 190, 190))
    # the wheel: a rim, a hub and eight spokes with handles
    cx, cy, r = S * 0.44, S * 0.56, S * 0.27
    wood, dark = (196, 140, 64), (92, 56, 22)
    for k in range(8):
        a = k * math.pi / 4 + math.pi / 8
        x1, y1 = cx + math.cos(a) * r * 1.38, cy + math.sin(a) * r * 1.38
        d.line([cx, cy, x1, y1], fill=dark, width=int(S * 0.07))
        d.line([cx, cy, x1, y1], fill=wood, width=int(S * 0.045))
        d.ellipse([x1 - S * 0.045, y1 - S * 0.045, x1 + S * 0.045, y1 + S * 0.045], fill=wood, outline=dark, width=int(S * 0.012))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=dark, width=int(S * 0.09))
    d.ellipse([cx - r + S * 0.012, cy - r + S * 0.012, cx + r - S * 0.012, cy + r - S * 0.012], outline=wood, width=int(S * 0.06))
    d.ellipse([cx - S * 0.08, cy - S * 0.08, cx + S * 0.08, cy + S * 0.08], fill=wood, outline=dark, width=int(S * 0.02))
    d.ellipse([cx - S * 0.03, cy - S * 0.03, cx + S * 0.03, cy + S * 0.03], fill=(242, 212, 138))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.22), fill=255)
    im.paste(bg, (0, 0), mask)
    return im.resize((size, size), Image.LANCZOS)

root = pathlib.Path(__file__).resolve().parent.parent
icon(52).save(root / 'card' / 'icon.png')
icon(40).save(root / 'card' / 'icon-40.png')
