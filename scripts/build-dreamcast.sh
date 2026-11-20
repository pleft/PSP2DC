#!/bin/sh
# Builds the companion's Dreamcast programs with KallistiOS in Docker, and a
# disc image of each for GDEMU:
#   build/psp2dc_test.cdi   dreamcast/test: what the path carries, measured
#   build/psp2dc_demo.cdi   dreamcast/demo: a small game with its dashboard on the PSP
# The image (kazade/dreamcast-sdk) carries KallistiOS, its compiler and
# mkdcdisc; it is for amd64 and runs emulated on an Arm Mac.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${KOS_IMAGE:-kazade/dreamcast-sdk:latest}"
mkdir -p "$ROOT/build"
docker run --rm --platform linux/amd64 -v "$ROOT:/src" -w /src/dreamcast "$IMAGE" bash -c '
set -e
. /opt/toolchains/dc/kos/environ.sh >/dev/null
for p in test demo; do
    cd /src/dreamcast/$p
    make clean >/dev/null
    make
    rm -f /src/build/psp2dc_$p.cdi
    $KOS_BASE/utils/mkdcdisc/build/mkdcdisc -q -N -e psp2dc_$p.elf -o /src/build/psp2dc_$p.cdi \
        -n "PSP2DC $(echo $p | tr a-z A-Z)" -a "PSP2DC"
    make clean >/dev/null
done
'
ls -l "$ROOT/build/psp2dc_test.cdi" "$ROOT/build/psp2dc_demo.cdi"
