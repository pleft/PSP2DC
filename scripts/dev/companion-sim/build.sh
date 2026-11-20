#!/bin/sh
# Builds the simulator (sim.h) as a library: build.sh <output folder>
set -e
SIM="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SIM/../../.." && pwd)"
FW="$ROOT/firmware/joypad-os/src"
OUT="$1"
mkdir -p "$OUT"
FLAGS="-O1 -g -Wall -fno-common -I $SIM/stubs -I $ROOT/protocol -I $FW"
# -w: the firmware's own warnings, on a compiler it was not written for
cc $FLAGS -w -DCONFIG_PSP2DC=1 -DCONFIG_VMU=1 '-D__not_in_flash_func(f)=f' -c -o "$OUT/vmu.o" "$FW/native/device/dreamcast/vmu.c"
cc $FLAGS -c -o "$OUT/companion.o" "$ROOT/psp/app/companion.c"
cc $FLAGS -DCONFIG_PSP2DC=1 -c -o "$OUT/sim.o" "$SIM/sim.c"
# one object that shows only the simulator's own names, so it can sit inside an emulator
ld -r -o "$OUT/psp2dcsim.o" "$OUT/vmu.o" "$OUT/companion.o" "$OUT/sim.o" \
   -exported_symbol '_sim_*' -exported_symbol _g_comp
rm -f "$OUT/libpsp2dcsim.a"
ar rcs "$OUT/libpsp2dcsim.a" "$OUT/psp2dcsim.o"
