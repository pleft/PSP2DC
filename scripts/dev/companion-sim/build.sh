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
cc $FLAGS -c -o "$OUT/comp_screen.o" "$ROOT/psp/app/comp_screen.c"
# the PSP's font (the debug screen's, "msx"), out of the PSP SDK's library
PSPDEV="${PSPDEV:-$ROOT/../pspdev}"
[ -x "$PSPDEV/bin/psp-objcopy" ] || { echo "the PSP toolchain is needed for its font (PSPDEV=$PSPDEV)" >&2; exit 1; }
(cd "$OUT" && "$PSPDEV/bin/psp-ar" x "$PSPDEV/psp/sdk/lib/libpspdebug.a" font.o &&
 "$PSPDEV/bin/psp-objcopy" -O binary -j .data font.o font.bin && rm font.o &&
 { echo 'unsigned char msx[] = {'; xxd -i < font.bin; echo '};'; } > font.c && rm font.bin)
cc $FLAGS -c -o "$OUT/font.o" "$OUT/font.c"
cc $FLAGS -DCONFIG_PSP2DC=1 -c -o "$OUT/sim.o" "$SIM/sim.c"
# one object that shows only the simulator's own names, so it can sit inside an emulator
ld -r -o "$OUT/psp2dcsim.o" "$OUT/vmu.o" "$OUT/companion.o" "$OUT/comp_screen.o" "$OUT/font.o" "$OUT/sim.o" \
   -exported_symbol '_sim_*' -exported_symbol _g_comp
rm -f "$OUT/libpsp2dcsim.a"
ar rcs "$OUT/libpsp2dcsim.a" "$OUT/psp2dcsim.o"
