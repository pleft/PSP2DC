#!/bin/sh
# Tests the adapter's Maple receiver on the computer: its state machine
# (firmware/joypad-os, maple_state_machine.c) over frames of every length.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DC="$ROOT/firmware/joypad-os/src/native/device/dreamcast"
OUT="$ROOT/build/maple-rx"
mkdir -p "$OUT"
cc -O1 -w -I "$DC" -o "$OUT/test" "$DC/maple_state_machine.c" "$ROOT/scripts/dev/maple-rx/test.c"
"$OUT/test"
