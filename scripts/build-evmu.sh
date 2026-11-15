#!/bin/sh
# Fetches libevmu (the ElysianVMU emulator core, MIT, github.com/gyrovorbis/libevmu)
# at the version PSP2DC was tested with, and builds it for the PSP into
# third_party/evmu-psp. The VMU's CPU for the standalone GAME mode.
# Needs the pspdev toolchain (../pspdev, or PSPDEV=...). third_party/ is not in git.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PSPDEV="${PSPDEV:-$ROOT/../pspdev}"
export PATH="$PSPDEV/bin:$PATH"
REV=2b155021472a11785a4297cc43f4d5c7fca7a5d4      # libevmu, 24 October 2026
SRC="$ROOT/third_party/libevmu"
OUT="$ROOT/third_party/evmu-psp"
if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$ROOT/third_party"
    git clone -q https://github.com/gyrovorbis/libevmu.git "$SRC"
fi
git -C "$SRC" fetch -q origin "$REV" 2>/dev/null || true
git -C "$SRC" checkout -q "$REV"
git -C "$SRC" submodule update --init --recursive -q
cmake -S "$SRC" -B "$OUT" -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$OUT" -j8 >/dev/null
find "$OUT" -name "*.a" | sed "s|$ROOT/||"
