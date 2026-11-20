#!/bin/sh
# Builds the companion's Dreamcast test program (dreamcast/test) with
# KallistiOS in Docker, and a disc image of it for GDEMU:
#   build/psp2dc_test.cdi
# The image (kazade/dreamcast-sdk) carries KallistiOS, its compiler and
# mkdcdisc; it is for amd64 and runs emulated on an Arm Mac.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${KOS_IMAGE:-kazade/dreamcast-sdk:latest}"
mkdir -p "$ROOT/build"
docker run --rm --platform linux/amd64 -v "$ROOT:/src" -w /src/dreamcast/test "$IMAGE" bash -c '
set -e
. /opt/toolchains/dc/kos/environ.sh >/dev/null
make clean >/dev/null
make
rm -f /src/build/psp2dc_test.cdi
$KOS_BASE/utils/mkdcdisc/build/mkdcdisc -q -N -e psp2dc_test.elf -o /src/build/psp2dc_test.cdi -n "PSP2DC TEST" -a "PSP2DC"
make clean >/dev/null
'
ls -l "$ROOT/build/psp2dc_test.cdi"
