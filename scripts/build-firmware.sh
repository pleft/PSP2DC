#!/bin/sh
# Builds the adapter firmware (firmware/joypad-os, target usb2dc_superpico) and
# copies the result to build/ under a name of its own: the time it was built.
# Always from src/build, never from joypad-os's releases/ folder: the names
# there carry the commit, so after a commit an older file can look like the
# new one (that sent a stale build to the board twice, 2026-11-15).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="${PICO_TOOLCHAIN_PATH:-$HOME/Dev/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi}"
[ -x "$T/bin/arm-none-eabi-gcc" ] || { echo "no Arm toolchain at $T (set PICO_TOOLCHAIN_PATH)" >&2; exit 1; }
cd "$ROOT/firmware/joypad-os"
PICO_TOOLCHAIN_PATH="$T" PATH="$T/bin:$PATH" make usb2dc_superpico
mkdir -p "$ROOT/build"
OUT="$ROOT/build/psp2dc_superpico_$(date +%Y%m%d_%H%M).uf2"
cp src/build/joypad_dc_superpico.uf2 "$OUT"
"$T/bin/arm-none-eabi-size" src/build/joypad_dc_superpico.elf | tail -1
echo "firmware: $OUT"
