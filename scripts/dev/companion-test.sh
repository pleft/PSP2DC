#!/bin/sh
# Tests the companion's three parts against each other on the computer:
# dreamcast/libpsp2dc, the firmware's side of it (vmu.c) and the PSP's
# (psp/app/companion.c), joined by scripts/dev/companion-sim.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/scripts/dev/companion-sim"
OUT="$ROOT/build/companion-sim"
"$SIM/build.sh" "$OUT"
cc -O1 -g -Wall -I "$SIM/stubs" -I "$SIM" -I "$ROOT/protocol" -I "$ROOT/dreamcast/libpsp2dc" \
   -o "$OUT/test" "$SIM/test.c" "$ROOT/dreamcast/libpsp2dc/psp2dc.c" "$OUT/libpsp2dcsim.a"
export PSP2DC_SIM_FILES="$OUT/files"
failed=0
for s in $("$OUT/test" || true); do "$OUT/test" "$s" || failed=1; done
exit $failed
