#!/bin/sh
# Builds Flycast with the companion's simulator (scripts/dev/companion-sim)
# behind port A's first memory card, to run dreamcast/test, or any program
# built on libpsp2dc, without the adapter and the PSP:
#   scripts/dev/flycast-companion.sh <Flycast source> [cmake options]
# The source is given companion-sim/flycast.patch, once; the build goes to
# build/flycast-sim/b. Then, on a Mac:
#   PSP2DC_SIM_PAGE=page.txt build/flycast-sim/b/Flycast.app/Contents/MacOS/Flycast build/psp2dc_test.cdi
# and page.txt is the PSP's companion page, written once a second (sim.h has
# what else the environment sets: a slow PSP, one unplugged now and then).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/scripts/dev/companion-sim"
OUT="$ROOT/build/flycast-sim"
[ -f "$1/core/hw/maple/maple_devs.cpp" ] || { echo "usage: $0 <Flycast source> [cmake options]" >&2; exit 2; }
SRC="$(cd "$1" && pwd)"
shift
"$SIM/build.sh" "$ROOT/build/companion-sim"
if git -C "$SRC" apply --reverse --check "$SIM/flycast.patch" 2>/dev/null; then
    echo "the patch is there already"
else
    git -C "$SRC" apply "$SIM/flycast.patch"
fi
cmake -S "$SRC" -B "$OUT/b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPSP2DC_SIM="$ROOT/build/companion-sim/libpsp2dcsim.a" "$@"
# (the library is not one of ninja's own files: the program is linked again each time)
rm -f "$OUT/b/Flycast.app/Contents/MacOS/Flycast" "$OUT/b/flycast"
ninja -C "$OUT/b"
echo "built in $OUT/b"
