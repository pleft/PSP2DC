#!/bin/sh
# Builds the companion's Dreamcast programs with KallistiOS in Docker, and a
# disc image of each for GDEMU:
#   build/psp2dc_test.cdi   dreamcast/test: what the path carries, measured
#   build/psp2dc_demo.cdi   dreamcast/demo: a small game with its dashboard on the PSP
#   build/samples/psp2dc_hello.cdi, _dashboard.cdi, _save.cdi
#                           dreamcast/samples: the library's samples, one thing each
# The image (kazade/dreamcast-sdk) carries KallistiOS, its compiler and
# mkdcdisc; it is for amd64 and runs emulated on an Arm Mac.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${KOS_IMAGE:-kazade/dreamcast-sdk:latest}"
mkdir -p "$ROOT/build/samples"
docker run --rm --platform linux/amd64 -v "$ROOT:/src" -w /src/dreamcast "$IMAGE" bash -c '
set -e
. /opt/toolchains/dc/kos/environ.sh >/dev/null
for d in test demo samples/hello samples/dashboard samples/save; do
    p=${d#samples/}
    out=/src/build/$(dirname $d | sed "s|^\.$||")
    cd /src/dreamcast/$d
    make clean >/dev/null
    make
    rm -f $out/psp2dc_$p.cdi
    $KOS_BASE/utils/mkdcdisc/build/mkdcdisc -q -N -e psp2dc_$p.elf -o $out/psp2dc_$p.cdi \
        -n "PSP2DC $(echo $p | tr a-z A-Z)" -a "PSP2DC"
    make clean >/dev/null
done
'
ls -l "$ROOT/build/psp2dc_test.cdi" "$ROOT/build/psp2dc_demo.cdi" "$ROOT"/build/samples/*.cdi
