#!/usr/bin/env python3
"""
Installs a VMU game (.vms) into a copy of a memory card image, as the
Dreamcast does: the game's blocks from block 0 up, chained in the FAT, and a
directory entry of type GAME (0xCC, header at block 1). A card holds one game;
an earlier one is replaced. For trying GAME mode in PPSSPP; it never writes
the card it reads.

  scripts/dev/card-add-game.py CARD_IN GAME.vms CARD_OUT [NAME]

Layout (mc.pp.se/dc/vms/flashmem.html): block 255 root, 254 FAT (16-bit LE;
0xFFFC free, 0xFFFA end of a chain), 253..241 directory (32-byte entries);
saves are allocated from block 199 down, a game from 0 up.
"""
import os, struct, sys, time

B = 512
card = bytearray(open(sys.argv[1], 'rb').read())
game = open(sys.argv[2], 'rb').read()
out = sys.argv[3]
name = (sys.argv[4] if len(sys.argv) > 4 else os.path.splitext(os.path.basename(sys.argv[2]))[0])[:12].upper()
assert len(card) == 256 * B, "not a 128 KB card image"
n = (len(game) + B - 1) // B
fat = list(struct.unpack('<256H', card[254 * B:255 * B]))
dirblocks = range(253, 240, -1)

# an earlier game: freed
for blk in dirblocks:
    for i in range(16):
        e = 32 * i + blk * B
        if card[e] == 0xCC:
            b = struct.unpack('<H', card[e + 2:e + 4])[0]
            while b < 256 and fat[b] not in (0xFFFC,):
                nxt = fat[b]; fat[b] = 0xFFFC
                if nxt == 0xFFFA: break
                b = nxt
            card[e:e + 32] = bytes(32)
for b in range(n):
    assert fat[b] == 0xFFFC, f"block {b} is in use: no room for a {n}-block game"
card[0:len(game)] = game
for b in range(n):
    fat[b] = b + 1 if b < n - 1 else 0xFFFA
card[254 * B:255 * B] = struct.pack('<256H', *fat)

def bcd(v): return ((v // 10) << 4) | (v % 10)
t = time.localtime()
stamp = bytes([bcd(t.tm_year // 100), bcd(t.tm_year % 100), bcd(t.tm_mon), bcd(t.tm_mday),
               bcd(t.tm_hour), bcd(t.tm_min), bcd(t.tm_sec), t.tm_wday])
for blk in dirblocks:
    for i in range(16):
        e = 32 * i + blk * B
        if card[e] == 0:
            card[e:e + 32] = (bytes([0xCC, 0x00]) + struct.pack('<H', 0) + name.ljust(12).encode('ascii')
                              + stamp + struct.pack('<HH', n, 1) + bytes(4))
            open(out, 'wb').write(card)
            print(f"{name}: {n} blocks at 0..{n - 1}, written to {out}")
            sys.exit(0)
sys.exit("the directory is full")
