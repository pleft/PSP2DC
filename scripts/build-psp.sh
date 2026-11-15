#!/bin/sh
# Builds the PSP side into dist/PSP2DC/ (copy that folder to ms0:/PSP/GAME/).
# Needs the pspdev toolchain (../pspdev, or PSPDEV=...) and a built PSPExp
# (../PSPExp, or PSPEXP=...) for its USB driver: pspexp_usb.prx, the stub
# library and the header.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PSPDEV="${PSPDEV:-$ROOT/../pspdev}"
export PATH="$PSPDEV/bin:$PATH"
PSPEXP="${PSPEXP:-$ROOT/../PSPExp}"
USB="$PSPEXP/psp/transport-usb"

command -v psp-gcc >/dev/null || { echo "psp-gcc not found (PSPDEV=$PSPDEV)" >&2; exit 1; }
for f in pspexp_usb.prx libpspexpusb.a pspexp_usb_driver.h; do
    [ -f "$USB/$f" ] || { echo "$USB/$f missing: build PSPExp first (scripts/build.sh there)" >&2; exit 1; }
done

make -C "$ROOT/psp/boot"
make -C "$ROOT/psp/app" PSPEXP_USB="$USB"

OUT="$ROOT/dist/PSP2DC"
mkdir -p "$OUT"
cp "$ROOT/psp/boot/EBOOT.PBP" "$ROOT/psp/app/psp2dc_app.prx" "$USB/pspexp_usb.prx" "$OUT/"
ls -l "$OUT"
