#!/bin/sh
# Copies dist/PSP2DC/ onto the PSP's Memory Stick (USB mode) and checks it.
#   scripts/install-psp.sh [volume]      default: /Volumes/NO NAME
# macOS leaves a "._NAME" file beside every file it writes to a FAT volume
# (the file's extended attributes, e.g. com.apple.provenance, which even
# cp -X does not avoid); they are deleted after the copy.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VOL="${1:-/Volumes/NO NAME}"
DST="$VOL/PSP/GAME/PSP2DC"
[ -d "$VOL/PSP/GAME" ] || { echo "no PSP at $VOL (USB mode on?)" >&2; exit 1; }
mkdir -p "$DST"
cp -X "$ROOT"/dist/PSP2DC/* "$DST"/
find "$DST" -name '._*' -type f -delete
rm -f "$VOL/PSP/GAME/._PSP2DC"
sync
for f in EBOOT.PBP psp2dc_app.prx pspexp_usb.prx; do
    if [ "$(md5 -q "$ROOT/dist/PSP2DC/$f")" = "$(md5 -q "$DST/$f")" ]; then echo "$f ok"; else echo "$f DIFFERS" >&2; exit 1; fi
done
left=$(find "$DST" -name '._*' | wc -l | tr -d ' ')
echo "installed in $DST (._ files: $left)"
