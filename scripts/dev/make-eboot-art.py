#!/usr/bin/env python3
"""
The PSP's pictures for the PSP2DC EBOOT:
  ICON0.PNG  144x80   the XMB tile: a VMU reading PSP / 2DC, and the name
  PIC1.PNG   480x272  the XMB background while PSP2DC is highlighted

  scripts/dev/make-eboot-art.py [OUT_DIR]     (OUT_DIR defaults to psp/boot)

Needs Pillow; uses Inter from ../PSPExp (tools/pspexp-term/assets). Drawn 4x
larger and scaled down for smooth edges. The VMU is drawn as in the
application (psp/app/ui.c): white case, lavender buttons, the lit screen's
teal with blue-violet dots, the icon row under them.
"""
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASSETS = os.path.join(ROOT, "..", "PSPExp", "tools", "pspexp-term", "assets")
FONT_B = os.path.join(ASSETS, "Inter-SemiBold.ttf")
FONT_R = os.path.join(ASSETS, "Inter-Regular.ttf")
S = 4

BG_TOP, BG_BOTTOM = (0x1c, 0x1f, 0x2a), (0x0a, 0x0b, 0x10)
ORANGE, BLUE = (0xf3, 0x70, 0x21), (0x3a, 0x6c, 0xff)
CASE, CASE_SHADE, SEAM = (0xe6, 0xe6, 0xe2), (0xc8, 0xc8, 0xc2), (0xb4, 0xb4, 0xae)
LAV, LAV_DARK = (0xac, 0xb8, 0xca), (0x80, 0x8a, 0x9c)
PAPER, INK = (0xa8, 0xd4, 0xc0), (0x3c, 0x3c, 0xb4)
WINDOW = (0x7e, 0x82, 0x72)

# 5x7 letters for the VMU's screen
GLYPHS = {
    "P": ["XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...."],
    "S": [".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."],
    "2": [".XXX.", "X...X", "....X", "...X.", "..X..", ".X...", "XXXXX"],
    "D": ["XXXX.", "X...X", "X...X", "X...X", "X...X", "X...X", "XXXX."],
    "C": [".XXXX", "X....", "X....", "X....", "X....", "X....", ".XXXX"],
}


def backdrop(w, h, glows):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        base = [BG_TOP[i] * (1 - t) + BG_BOTTOM[i] * t for i in range(3)]
        for x in range(w):
            c = list(base)
            for (cx, cy, rad, col, k) in glows:
                d = math.hypot(x - cx, y - cy) / rad
                if d < 1:
                    for i in range(3):
                        c[i] += col[i] * (1 - d) ** 2 * k
            px[x, y] = tuple(min(255, int(v)) for v in c)
    return img


def screen_bitmap():
    """48x32: PSP over 2DC, each letter twice its size (10x14), 2 apart"""
    dots = [[0] * 48 for _ in range(32)]
    for line, (text, y0) in enumerate((("PSP", 1), ("2DC", 17))):
        x0 = (48 - (len(text) * 12 - 2)) // 2
        for k, ch in enumerate(text):
            for r, row in enumerate(GLYPHS[ch]):
                for c, v in enumerate(row):
                    if v == "X":
                        for dy in range(2):
                            for dx in range(2):
                                dots[y0 + r * 2 + dy][x0 + k * 12 + c * 2 + dx] = 1
    return dots


def vmu(d, x, y, w):
    """a VMU, w wide (its height follows), top left at x, y; on draw d"""
    h = int(w * 1.42)
    r_top, r_bottom = w * 0.08, w * 0.22
    # shadow, case, the seams
    d.rounded_rectangle((x + w * 0.04, y + w * 0.05, x + w * 1.04, y + h + w * 0.05), r_bottom, fill=(0, 0, 0, 110))
    d.rounded_rectangle((x, y, x + w, y + h), r_bottom, fill=CASE_SHADE)
    d.rounded_rectangle((x, y, x + w, y + h * 0.6), r_top, fill=CASE)
    d.rectangle((x, y + h * 0.3, x + w, y + h * 0.6), fill=CASE)
    for sx in (x + w * 0.06, x + w * 0.94):
        d.line((sx, y + h * 0.05, sx, y + h * 0.88), fill=SEAM, width=max(1, int(w * 0.008)))
    # the window and the screen
    wx0, wy0, wx1, wy1 = x + w * 0.1, y + h * 0.1, x + w * 0.9, y + h * 0.1 + w * 0.8 / 1.2
    d.rounded_rectangle((wx0 - w * 0.015, wy0 - w * 0.015, wx1 + w * 0.015, wy1 + w * 0.015), w * 0.03, fill=WINDOW)
    d.rectangle((wx0, wy0, wx1, wy1), fill=PAPER)
    dots = screen_bitmap()
    dw = (wx1 - wx0 - w * 0.04) / 48
    gx, gy = wx0 + w * 0.02, wy0 + w * 0.02
    for r in range(32):
        for c in range(48):
            if dots[r][c]:
                d.rectangle((gx + c * dw, gy + r * dw, gx + (c + 1) * dw - dw * 0.15, gy + (r + 1) * dw - dw * 0.15), fill=INK)
    # the icon row, faint
    iy = gy + 32 * dw + (wy1 - gy - 32 * dw) / 2
    for k in range(4):
        cx = gx + (48 * dw) * (2 * k + 1) / 8
        d.ellipse((cx - dw * 3, iy - dw * 3, cx + dw * 3, iy + dw * 3), outline=tuple((PAPER[i] * 5 + INK[i] * 2) // 7 for i in range(3)), width=max(1, int(dw * 0.8)))
    # d-pad, SLEEP and MODE, A and B
    bx, by = x + w * 0.27, y + h * 0.8
    rp = w * 0.16
    d.ellipse((bx - rp, by - rp, bx + rp, by + rp), fill=LAV_DARK)
    d.ellipse((bx - rp * 0.9, by - rp * 0.9, bx + rp * 0.9, by + rp * 0.9), fill=LAV)
    a = rp * 0.75
    t = rp * 0.28
    d.rounded_rectangle((bx - t, by - a, bx + t, by + a), t * 0.5, fill=tuple(min(255, v + 18) for v in LAV))
    d.rounded_rectangle((bx - a, by - t, bx + a, by + t), t * 0.5, fill=tuple(min(255, v + 18) for v in LAV))
    for (cx, cy, rr) in ((0.53, 0.73, 0.045), (0.69, 0.73, 0.045), (0.6, 0.86, 0.075), (0.82, 0.84, 0.075)):
        px, py, pr = x + w * cx, y + h * cy, w * rr
        d.ellipse((px - pr, py - pr + w * 0.01, px + pr, py + pr + w * 0.01), fill=LAV_DARK)
        d.ellipse((px - pr, py - pr, px + pr, py + pr), fill=LAV)
    return h


def icon0(out):
    W, H = 144 * S, 80 * S
    img = backdrop(W, H, [(W * 0.2, H * 0.5, W * 0.5, ORANGE, 0.55), (W * 0.95, H * 0.1, W * 0.6, BLUE, 0.25)]).convert("RGBA")
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    vmu(ImageDraw.Draw(layer), 9 * S, 6 * S, 46 * S)
    img = Image.alpha_composite(img, layer)
    d = ImageDraw.Draw(img)
    room = (144 - 64 - 5) * S                 # the text's width, right of the VMU
    size = 26
    while True:                               # the largest name that fits
        big = ImageFont.truetype(FONT_B, size * S)
        if sum(d.textlength(t, font=big) for t in ("PSP", "2", "DC")) <= room or size <= 12:
            break
        size -= 1
    small = ImageFont.truetype(FONT_R, 9 * S)
    for k in range(9, 6, -1):
        small = ImageFont.truetype(FONT_R, k * S)
        if d.textlength("Dreamcast controller", font=small) <= room:
            break
    x, y = 64 * S, 22 * S
    for part, col in (("PSP", (255, 255, 255)), ("2", ORANGE), ("DC", (255, 255, 255))):
        d.text((x, y), part, font=big, fill=col)
        x += d.textlength(part, font=big)
    d.text((64 * S, 46 * S), "Dreamcast controller", font=small, fill=(0xb0, 0xb6, 0xc4))
    d.text((64 * S, 56 * S), "& VMU on the PSP", font=small, fill=(0x80, 0x86, 0x96))
    img.convert("RGB").resize((144, 80), Image.LANCZOS).save(out)


def pic1(out):
    W, H = 480 * S, 272 * S
    img = backdrop(W, H, [(W * 0.8, H * 0.55, W * 0.5, ORANGE, 0.28), (W * 0.05, H * 0.05, W * 0.6, BLUE, 0.18)]).convert("RGBA")
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    vmu(ImageDraw.Draw(layer), 318 * S, 38 * S, 132 * S)
    layer = layer.rotate(-8, resample=Image.BICUBIC, center=(384 * S, 136 * S))
    img = Image.alpha_composite(img, layer)
    img.convert("RGB").resize((480, 272), Image.LANCZOS).save(out)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "psp", "boot")
    icon0(os.path.join(out, "ICON0.PNG"))
    pic1(os.path.join(out, "PIC1.PNG"))
    print("wrote", os.path.join(out, "ICON0.PNG"), "and PIC1.PNG")
