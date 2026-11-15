#!/bin/sh
# Runs PSP2DC in PPSSPP (demo mode: the emulator has no USB driver), to try
# the PSP side's screens without the adapter or a Dreamcast.
#
#   scripts/dev/run-ppsspp.sh          install dist/PSP2DC and start it
#   scripts/dev/run-ppsspp.sh shot F   save an image of the emulator's window as F (PNG)
#   scripts/dev/run-ppsspp.sh stop     stop PPSSPP
#   scripts/dev/run-ppsspp.sh press K  press a PSP button in the emulator: up down left right
#                                       cross circle square triangle start select (held 0.25 s;
#                                       PPSSPP's keyboard map; macOS asks once to allow it)
#
# PPSSPP's memory stick root (ms0:) is ~/.config/ppsspp: the game goes in its
# PSP/GAME, and the application's own folder ms0:/PSP2DC is ~/.config/ppsspp/PSP2DC.
# Put recordings (.rec) in its capture/ and a card image as card1.bin there.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PPSSPP="${PPSSPP:-/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL}"
MS="${PPSSPP_MS:-$HOME/.config/ppsspp}"
GAME="$MS/PSP/GAME/PSP2DC"
case "$1" in
stop) pkill -f "$(basename "$PPSSPP")" || true; exit 0 ;;
press)
	case "$2" in
	up) K="key code 126" ;; down) K="key code 125" ;; left) K="key code 123" ;; right) K="key code 124" ;;
	cross) K='keystroke "z"' ;; circle) K='keystroke "x"' ;; square) K='keystroke "a"' ;;
	triangle) K='keystroke "s"' ;; start) K="key code 49" ;; select) K="key code 36" ;;
	*) echo "press: unknown button $2" >&2; exit 1 ;;
	esac
	case "$K" in
	keystroke*) C=$(echo "$K" | sed 's/keystroke "\(.\)"/\1/'); D="key down \"$C\""; U="key up \"$C\"" ;;
	*) D="key down (${K#key code })"; U="key up (${K#key code })" ;;
	esac
	osascript -e 'tell application "System Events" to set frontmost of (first process whose name contains "PPSSPP") to true' \
		-e 'delay 0.3' -e "tell application \"System Events\" to $D" -e 'delay 0.25' -e "tell application \"System Events\" to $U"
	exit 0 ;;
shot)
	ID=$(swift "$ROOT/scripts/dev/ppsspp-window.swift" 2>/dev/null)
	[ -n "$ID" ] || { echo "no PPSSPP window" >&2; exit 1; }
	screencapture -x -o -l"$ID" "${2:-ppsspp.png}"
	exit 0 ;;
esac
[ -x "$PPSSPP" ] || { echo "PPSSPP not found at $PPSSPP" >&2; exit 1; }
[ -f "$ROOT/dist/PSP2DC/EBOOT.PBP" ] || { echo "run scripts/build-psp.sh first" >&2; exit 1; }
pkill -f "$(basename "$PPSSPP")" 2>/dev/null || true
mkdir -p "$GAME" "$MS/PSP2DC" "$ROOT/build"
cp "$ROOT/dist/PSP2DC/"* "$GAME/"
"$PPSSPP" "$GAME/EBOOT.PBP" > "$ROOT/build/ppsspp.log" 2>&1 &
echo "PPSSPP started (log: build/ppsspp.log); ms0:/PSP2DC is $MS/PSP2DC"
