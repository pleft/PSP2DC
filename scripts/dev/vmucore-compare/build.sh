#!/bin/sh
# Builds the comparison of psp/app/vmu_core.c with libevmu (the core it was
# derived from) on the Mac: both run the same game with the same buttons and
# their screens, state and flash are compared every 1/60 s.
#
#   scripts/dev/vmucore-compare/build.sh            build (libevmu for the Mac into third_party/evmu-host)
#   build/vmucore-compare GAME.vms [SECONDS] [MODE_AT_SECOND]
#
# libevmu's source comes from scripts/build-evmu.sh (third_party/libevmu).
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/third_party/libevmu"
OUT="$ROOT/third_party/evmu-host"
[ -d "$SRC/.git" ] || { echo "run scripts/build-evmu.sh first (it fetches libevmu)" >&2; exit 1; }
cmake -S "$SRC" -B "$OUT" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$OUT" -j8 >/dev/null
F="$OUT/lib/CMakeFiles/libLibElysianVMU.dir/flags.make"
INCS=$(grep "^C_INCLUDES" "$F" | sed 's/^C_INCLUDES = //')
LIBS=$(find "$OUT" -name "*.a" | grep -v ElysianVMU | tr '\n' ' ')
mkdir -p "$ROOT/build"
# shellcheck disable=SC2086
cc -std=gnu17 -O2 -include stdlib.h \
   -DGBL_CONFIG_ASSERT_ERROR_ENABLED=0 -DGBL_CONFIG_ASSERT_PARTIAL_ENABLED=0 -DGBL_CONFIG_ASSERT_UNKNOWN_ENABLED=0 \
   -DGBL_CONFIG_ERRNO_CHECKS=1 -DGBL_CONFIG_LOG_ERROR_ENABLED=1 -DGBL_CONFIG_LOG_ERROR_LEVEL=GBL_LOG_LEVEL_ERROR \
   -DGBL_CONFIG_LOG_PARTIAL_ENABLED=1 -DGBL_CONFIG_LOG_PARTIAL_LEVEL=GBL_LOG_LEVEL_WARNING -DGBL_CONFIG_LOG_UNKNOWN_ENABLED=1 \
   -DGBL_CONFIG_LOG_UNKNOWN_LEVEL=GBL_LOG_LEVEL_ERROR -DGBL_CONFIG_PREFETCH_ENABLED=1 -DGBL_TLS_EMULATED=0 -DGBL_BIG_ENDIAN=0 \
   -I"$ROOT/psp/app" $INCS \
   "$ROOT/scripts/dev/vmucore-compare/cmp.c" "$ROOT/scripts/dev/vmucore-compare/core_dbg.c" \
   "$OUT/lib/liblibLibElysianVMU.a" $LIBS $LIBS -lm -lc++ -o "$ROOT/build/vmucore-compare"
echo "built build/vmucore-compare"
