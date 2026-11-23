#!/bin/sh
# Builds everything from the sources as they are and puts a release together:
#   scripts/make-release.sh <version>      e.g. 1.0.0
# The result is build/release/PSP2DC-<version>/ and the same as a zip beside
# it: the PSP application, the adapter's firmware, the Dreamcast discs, and
# the licences of what they are built from. It refuses to run with changes
# that are not committed, so a release is always a commit. With --pack it
# only puts together what is built already (.github/workflows/release.yml).
# A release proper is made by that workflow from a tag: docs/releasing.md.
# Needs what the three build scripts need: the pspdev toolchain, the Arm
# toolchain with firmware/joypad-os set up (firmware/README.md), and Docker.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PACK=
[ "$1" = --pack ] && { PACK=1; shift; }
V="$1"
[ -n "$V" ] || { echo "usage: $0 [--pack] <version>" >&2; exit 2; }
cd "$ROOT"
export PSP2DC_RELEASE="$V"   # (scripts/version.sh: the PSP application says it)
if [ -n "$PACK" ]; then
    # what is built already (the release workflow's jobs): dist/PSP2DC, build/*.cdi, build/samples, one build/psp2dc_rp2040zero_*.uf2
    UF2="$(ls build/psp2dc_rp2040zero_*.uf2 | tail -1)"
else
[ -z "$(git status --porcelain)" ] || { echo "there are changes that are not committed" >&2; exit 1; }
FW="$ROOT/firmware/joypad-os"
[ -z "$(git -C "$FW" status --porcelain --ignore-submodules)" ] || { echo "firmware/joypad-os has changes that are not committed" >&2; exit 1; }
# the firmware's branch must be what firmware/patches holds
[ "$(git -C "$FW" log --oneline 9fa79f45..HEAD | wc -l | tr -d ' ')" = "$(ls firmware/patches/*.patch | wc -l | tr -d ' ')" ] ||
    { echo "firmware/joypad-os and firmware/patches differ in their number of commits" >&2; exit 1; }

scripts/build-psp.sh
UF2="$(scripts/build-firmware.sh | sed -n 's/^firmware: //p')"
[ -f "$UF2" ] || { echo "no firmware built" >&2; exit 1; }
scripts/build-dreamcast.sh
fi
OUT="$ROOT/build/release/PSP2DC-$V"
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/psp/PSP/GAME/PSP2DC" "$OUT/adapter" "$OUT/dreamcast/samples" "$OUT/licences"
cp dist/PSP2DC/EBOOT.PBP dist/PSP2DC/psp2dc_app.prx dist/PSP2DC/pspexp_usb.prx "$OUT/psp/PSP/GAME/PSP2DC/"
cp "$UF2" "$OUT/adapter/psp2dc_rp2040zero-$V.uf2"
cp build/psp2dc_demo.cdi build/psp2dc_test.cdi "$OUT/dreamcast/"
cp build/samples/*.cdi "$OUT/dreamcast/samples/"

cp LICENSE "$OUT/licences/LICENSE.txt"
cp THIRD_PARTY.md "$OUT/licences/THIRD_PARTY.txt"
cp licenses/* "$OUT/licences/"
cat > "$OUT/README.txt" <<TXT
PSP2DC $V: a PSP-1000 as a Dreamcast controller, VMU, memory card and keyboard.
How to build the adapter and how to use it: https://github.com/pleft/PSP2DC

psp/        Copy the PSP folder onto the PSP's Memory Stick (it holds
            PSP/GAME/PSP2DC). Needs custom firmware (made on 6.60 PRO-C).
adapter/    The RP2040-Zero's firmware: hold BOOT while plugging the board into
            a computer and copy the .uf2 onto the drive that appears.
dreamcast/  For homebrew developers: disc images (GDEMU and the like) of the
            companion's demo, its test and the library's samples.
licences/   PSP2DC's licence (MIT) and those of what it is built from
            (THIRD_PARTY.txt says which is which).

The PSP application and the adapter must be of the same release.
TXT
(cd "$ROOT/build/release" && zip -q -r -X "PSP2DC-$V.zip" "PSP2DC-$V")
(cd "$ROOT/build/release" && find "PSP2DC-$V" -type f | sort && ls -l "PSP2DC-$V.zip" && shasum -a 256 "PSP2DC-$V.zip")
