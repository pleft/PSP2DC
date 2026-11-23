#!/bin/sh
# Runs each sample of dreamcast/samples on the computer, against the
# simulated adapter and PSP (scripts/dev/companion-sim/sample-run.c), and
# checks what it should have done. The save sample runs twice: the second
# run must find what the first one saved.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/scripts/dev/companion-sim"
OUT="$ROOT/build/companion-sim"
"$SIM/build.sh" "$OUT"
export PSP2DC_SIM_FILES="$OUT/sample-files"
rm -rf "$PSP2DC_SIM_FILES"
failed=0
check() {   # check <sample> <text the output must hold>
    if grep -qF -- "$2" "$OUT/sample-$1.txt"; then echo "ok   $1: $2"; else echo "FAIL $1: no \"$2\""; failed=1; fi
}
for s in hello dashboard save; do
    cc -O1 -g -Wall -Dmain=sample_main -I "$SIM/stubs" -I "$ROOT/protocol" -I "$ROOT/dreamcast/libpsp2dc" \
       -c -o "$OUT/sample-$s.o" "$ROOT/dreamcast/samples/$s/main.c"
    cc -O1 -g -Wall -I "$SIM/stubs" -I "$SIM" -I "$ROOT/protocol" -I "$ROOT/dreamcast/libpsp2dc" \
       -o "$OUT/sample-$s" "$SIM/sample-run.c" "$OUT/sample-$s.o" "$ROOT/dreamcast/libpsp2dc/psp2dc.c" "$OUT/libpsp2dcsim.a"
done
"$OUT/sample-hello" > "$OUT/sample-hello.txt"
check hello " 0 |Hello from the Dreamcast!"
check hello "PSP: there"
"$OUT/sample-dashboard" > "$OUT/sample-dashboard.txt"
check dashboard "value 2 of 0 \"First line|Second line|Third line\""   # L once: the mark from the first line to the last
check dashboard "of 255 \"Right trigger\""             # its range set
check dashboard "value 39"                              # the frames counted: 39x, a frame or two behind the 400th
"$OUT/sample-save" > "$OUT/sample-save.txt"
check save "counter: 2"
"$OUT/sample-save" > "$OUT/sample-save.txt"
check save "counter: 4"                  # it read the 2 the first run saved
[ "$(od -An -tu4 "$PSP2DC_SIM_FILES/psp2dcsave/counter.sav" | tr -d ' ')" = 4 ] && echo "ok   save: the file holds 4" || { echo "FAIL save: the file"; failed=1; }
exit $failed
