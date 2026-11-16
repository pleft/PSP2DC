#!/bin/sh
# Builds the PSP side into dist/PSP2DC/ (copy that folder to ms0:/PSP/GAME/).
# Needs the pspdev toolchain (../pspdev, or PSPDEV=...). The kernel USB driver
# (psp/usb, from PSPExp) is built first: pspexp_usb.prx and its stub library.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PSPDEV="${PSPDEV:-$ROOT/../pspdev}"
export PATH="$PSPDEV/bin:$PATH"
USB="$ROOT/psp/usb"

command -v psp-gcc >/dev/null || { echo "psp-gcc not found (PSPDEV=$PSPDEV)" >&2; exit 1; }

make -C "$USB"
make -C "$ROOT/psp/boot"
make -C "$ROOT/psp/app" PSPEXP_USB="$USB"

OUT="$ROOT/dist/PSP2DC"
mkdir -p "$OUT"
cp "$ROOT/psp/boot/EBOOT.PBP" "$ROOT/psp/app/psp2dc_app.prx" "$USB/pspexp_usb.prx" "$OUT/"
ls -l "$OUT"
