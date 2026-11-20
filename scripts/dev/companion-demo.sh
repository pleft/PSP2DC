#!/bin/sh
# Runs the companion's demo (dreamcast/demo) on the computer, against the
# simulated adapter and PSP (scripts/dev/companion-sim), and leaves in
# build/companion-sim what the PSP would show:
#   demo-1.png, demo-2.png   the PSP's screen at two moments
#   demo.bin                 the records the PSP got; copied to a PSP's (or
#                            PPSSPP's) ms0:/PSP2DC/dev_companion.bin, the PSP
#                            application shows the same without a Dreamcast
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/scripts/dev/companion-sim"
OUT="$ROOT/build/companion-sim"
"$SIM/build.sh" "$OUT"
cc -O1 -g -Wall -Dmain=demo_main -I "$SIM/stubs" -I "$ROOT/protocol" -I "$ROOT/dreamcast/libpsp2dc" \
   -c -o "$OUT/demo.o" "$ROOT/dreamcast/demo/main.c"
cc -O1 -g -Wall -I "$SIM/stubs" -I "$SIM" -I "$ROOT/protocol" -I "$ROOT/dreamcast/libpsp2dc" \
   -o "$OUT/demo-run" "$SIM/demo-run.c" "$OUT/demo.o" "$ROOT/dreamcast/libpsp2dc/psp2dc.c" "$OUT/libpsp2dcsim.a"
"$OUT/demo-run" "$OUT"
for n in 1 2; do
    sips -s format png "$OUT/demo-$n.bmp" --out "$OUT/demo-$n.png" >/dev/null && rm "$OUT/demo-$n.bmp"
done
ls -l "$OUT"/demo-1.png "$OUT"/demo-2.png "$OUT"/demo.bin
