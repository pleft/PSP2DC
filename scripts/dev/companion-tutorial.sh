#!/bin/sh
# Runs every step of dreamcast/tutorial on the computer, against the simulated
# adapter and PSP (scripts/dev/companion-sim/tutorial-run.c), presses what
# each step is about and checks what it then shows. With "pictures" as its
# argument it also saves the PSP's screen of the steps that have one, as
# docs/images/tutorial-stepN.png.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/scripts/dev/companion-sim"
OUT="$ROOT/build/companion-sim"
"$SIM/build.sh" "$OUT"
export PSP2DC_SIM_FILES="$OUT/tutorial-files"
rm -rf "$PSP2DC_SIM_FILES"
failed=0
run() {     # run <step> <events...>: the step with those presses; its output in $OUT/<step>.txt
    step="$1"; shift
    cc -O1 -g -Wall -Dmain=step_main -I "$SIM/stubs" -I "$ROOT/protocol" -I "$ROOT/dreamcast/libpsp2dc" \
       -I "$ROOT/dreamcast/tutorial/$step" -c -o "$OUT/$step.o" "$ROOT/dreamcast/tutorial/$step/main.c"
    cc -O1 -g -Wall -I "$SIM/stubs" -I "$SIM" -I "$ROOT/protocol" -I "$ROOT/dreamcast/libpsp2dc" \
       -o "$OUT/$step" "$SIM/tutorial-run.c" "$OUT/$step.o" "$ROOT/dreamcast/libpsp2dc/psp2dc.c" "$OUT/libpsp2dcsim.a"
    "$OUT/$step" "$OUT/$step.bmp" "$@" > "$OUT/$step.txt"
    if [ "$PICTURES" = 1 ] && [ -n "$PICTURE" ]; then
        sips -s format png "$OUT/$step.bmp" --out "$ROOT/docs/images/tutorial-${step%%-*}.png" >/dev/null
    fi
    PICTURE=
}
check() {   # check <step> <text its output must hold>
    if grep -qF -- "$2" "$OUT/$1.txt"; then echo "ok   $1: $2"; else echo "FAIL $1: no \"$2\""; failed=1; fi
}
[ "$1" = pictures ] && PICTURES=1

# 1: the text; the PSP unplugged and back, and the text is there again
run step1-hello 150-200:unplug
check step1-hello " 0 |Hello from the Dreamcast!"
check step1-hello " 2 |This is row 2 of the PSP's 12 rows."
check step1-hello "PSP: there"

# 2: X pressed three times on the PSP, L held at the end; the PSP's answer
run step2-buttons 100-105:pX 120-125:pX 140-145:pX 380-400:pL
check step2-buttons "X pressed 3 times"
check step2-buttons " 2 |X pressed 3 times"
check step2-buttons "held: L"
check step2-buttons "PSP: level 5, screen 480x272"

# 3: the trigger full (the bar red), A held, the list's mark down twice, the light removed
PICTURE=1 run step3-dashboard 100-400:RTRIG 100-400:A 150:DOWN 170:DOWN 200:START
check step3-dashboard "value 255 of 255 \"Right trigger\""
check step3-dashboard "flags 11 at 40,80"                  # the bar's own colour: COLOUR | PANEL
check step3-dashboard "value 1 of 0 \"A held\""
check step3-dashboard "value 2 of 0 \"Sword|Shield|Bow|Potion\""
check step3-dashboard "\"Marked: Bow\""
check step3-dashboard "notice: \"START pressed on the Dreamcast\""
run step3-dashboard 150:B
if grep -q "A held" "$OUT/step3-dashboard.txt"; then echo "FAIL step3-dashboard: the light was not removed"; failed=1; else echo "ok   step3-dashboard: B removes the light"; fi

# 4: the region made, the dot moved; drawn again after the PSP was away
PICTURE=1 run step4-drawing 100-140:RIGHT 100-120:DOWN 200-240:unplug
check step4-drawing "region 0: 16,36 240x200"
check step4-drawing "screen: the program's, 0 bad records"

# 5: the hero walks onto two coins
PICTURE=1 run step5-pictures 100:LEFT 120:DOWN 140:DOWN 160:LEFT 180:LEFT 200:RIGHT 220:RIGHT 240:RIGHT 260:RIGHT 280:RIGHT
check step5-pictures "region 0: 16,40 224x176"
check step5-pictures "value 2 of 0 \"Coins\""
check step5-pictures "screen: the program's, 0 bad records"

# 6: the menu opened (Y), the PSP's down twice and X; the buttons taken; closed again in a second run
PICTURE=1 run step6-input 100:Y 150-155:pDOWN 180-185:pDOWN 220-225:pX
check step6-input "\"MENU OPEN\""
check step6-input "buttons taken: 4050"                    # up, down, X
check step6-input "value 2 of 0 \"Potion|Map|Torch|Rope\""
check step6-input "\"Picked: Torch\""
run step6-input 100:Y 150-155:pDOWN 200:Y
check step6-input "\"menu closed\""
if grep -q "buttons taken" "$OUT/step6-input.txt"; then echo "FAIL step6-input: the buttons were not given back"; failed=1; else echo "ok   step6-input: closing gives the buttons back"; fi

# 7: saved twice; a second run finds it and adds two more; listed; then deleted
run step7-save 150:A 220:A
check step7-save "counter: 2"
check step7-save "write: done"
run step7-save 150:A 220:A 300:Y
check step7-save "counter: 4"
check step7-save "first file: counter.sav 4"
check step7-save "room left: 16383 KB"
[ "$(od -An -tu4 "$PSP2DC_SIM_FILES/tutorial7/counter.sav" | tr -d ' ')" = 4 ] && echo "ok   step7-save: the file holds 4" || { echo "FAIL step7-save: the file"; failed=1; }
run step7-save 150:B
check step7-save "delete: done"
check step7-save "counter: 0"
exit $failed
