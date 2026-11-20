#!/usr/bin/env python3
"""A PNG as a C header for libpsp2dc: pixels for psp2dc_draw_bitmap() and
psp2dc_sprite_create(), and the palette for psp2dc_region_palette().

    psp2dc-image.py picture.png name [-f 1|4|8|565] > name.h

The format is the smallest the picture's colours fit: 1, 4 or 8 bits a
pixel with a palette (up to 2, 16, 256 colours), else RGB565. Pixels less
than half opaque become one colour of their own, NAME_KEY, to pass as the
key when drawing. The PNG must be 8 bits a channel and not interlaced;
nothing but Python is needed.
"""
import struct, sys, zlib

def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, plte, trns = 8, b"", None, None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
    if depth != 8 or interlace or ctype not in (0, 2, 3, 4, 6):
        sys.exit(f"{path}: only 8 bits a channel, not interlaced")
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    pixels = []                            # (r, g, b, a) each
    for line in rows:
        for x in range(w):
            p = line[x * bpp:(x + 1) * bpp]
            if ctype == 0: pixels.append((p[0], p[0], p[0], 255))
            elif ctype == 2: pixels.append((p[0], p[1], p[2], 255))
            elif ctype == 3: pixels.append((*plte[p[0] * 3:p[0] * 3 + 3], trns[p[0]] if trns and p[0] < len(trns) else 255))
            elif ctype == 4: pixels.append((p[0], p[0], p[0], p[1]))
            else: pixels.append(tuple(p))
    return w, h, pixels

def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    force = sys.argv[sys.argv.index("-f") + 1] if "-f" in sys.argv else None
    if force: args.remove(force)
    if len(args) != 2:
        sys.exit(__doc__)
    w, h, pixels = read_png(args[0])
    name = args[1]
    clear = any(p[3] < 128 for p in pixels)
    cols = [None if p[3] < 128 else rgb565(*p[:3]) for p in pixels]
    palette = ([None] if clear else []) + sorted({c for c in cols if c is not None})
    fmt = force or ("1" if len(palette) <= 2 else "4" if len(palette) <= 16 else "8" if len(palette) <= 256 else "565")
    limit = {"1": 2, "4": 16, "8": 256, "565": 65536}.get(fmt) or sys.exit("-f: 1, 4, 8 or 565")
    if fmt != "565" and len(palette) > limit:
        sys.exit(f"{args[0]}: {len(palette)} colours, more than {limit}")
    out, key = bytearray(), None
    if fmt == "565":
        if clear:                          # a colour the picture does not use, for what is clear
            key = next(c for c in (0xF81F, 0x07FF, 0xFFE0, *range(65536)) if c not in palette)
        for c in cols:
            out += struct.pack("<H", key if c is None else c)
        row = 2 * w
    else:
        index = {c: i for i, c in enumerate(palette)}
        key = 0 if clear else None
        bits = int(fmt)
        row = (w * bits + 7) // 8
        for y in range(h):
            line = bytearray(row)
            for x in range(w):
                i = index[cols[y * w + x]]
                if bits == 8: line[x] = i
                elif bits == 4: line[x >> 1] |= i << (0 if x & 1 else 4)
                else: line[x >> 3] |= i << (7 - (x & 7))
            out += line
    up = name.upper()
    print(f"/* {args[0].split('/')[-1]}, by psp2dc-image.py: {w} x {h}, " +
          ("RGB565" if fmt == "565" else f"{fmt} bits a pixel, {len(palette)} colours") + " */")
    print(f"#define {up}_W {w}\n#define {up}_H {h}\n#define {up}_STRIDE {row}")
    print(f"#define {up}_FORMAT PSP2DC_FMT_{fmt}")
    if key is not None:
        print(f"#define {up}_KEY {key}   /* not drawn when passed as the key */")
    if fmt != "565":
        print(f"static const uint16_t {name}_palette[{len(palette)}] = {{")
        print("    " + ", ".join("0x%04X" % (c or 0) for c in palette))
        print("};")
    print(f"static const uint8_t {name}[{len(out)}] = {{")
    for i in range(0, len(out), 16):
        print("    " + ", ".join("0x%02X" % b for b in out[i:i + 16]) + ",")
    print("};")

main()
