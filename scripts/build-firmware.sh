#!/bin/sh
# Builds the adapter firmware (firmware/joypad-os) for a board and copies the
# result to build/ under a name of its own: the board and the time it was built.
#   scripts/build-firmware.sh             the Waveshare RP2040-Zero (target usb2dc_psp2dc_rp2040zero)
#   scripts/build-firmware.sh superpico   the prototype, a Pico on a SuperPico (usb2dc_superpico)
# Always from src/build, never from joypad-os's releases/ folder: the names
# there carry the commit, so after a commit an older file can look like the
# new one (that sent a stale build to the board twice, 2026-11-15).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD="${1:-rp2040zero}"
case "$BOARD" in
rp2040zero) APP=usb2dc_psp2dc_rp2040zero; TARGET=joypad_dc_psp2dc_rp2040zero ;;
superpico)  APP=usb2dc_superpico;         TARGET=joypad_dc_superpico ;;
*) echo "board: rp2040zero (the default) or superpico" >&2; exit 1 ;;
esac
T="${PICO_TOOLCHAIN_PATH:-$HOME/Dev/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi}"
[ -x "$T/bin/arm-none-eabi-gcc" ] || { echo "no Arm toolchain at $T (set PICO_TOOLCHAIN_PATH)" >&2; exit 1; }
cd "$ROOT/firmware/joypad-os"
PICO_TOOLCHAIN_PATH="$T" PATH="$T/bin:$PATH" make "$APP"
mkdir -p "$ROOT/build"
OUT="$ROOT/build/psp2dc_${BOARD}_$(date +%Y%m%d_%H%M).uf2"
cp "src/build/$TARGET.uf2" "$OUT"
"$T/bin/arm-none-eabi-size" "src/build/$TARGET.elf" | tail -1
echo "firmware: $OUT"
