# Firmware

The adapter firmware is a branch of [joypad-os](https://github.com/joypad-ai/joypad-os)
(Apache-2.0). It is not copied into this repository: `firmware/joypad-os/` is
a clone of its own, ignored here, on branch `superpico` (named after the
prototype board; it holds both boards' targets).

`patches/` holds that branch's commits on top of upstream commit `9fa79f45`
(11 October 2026), so the firmware can be rebuilt from scratch:

```sh
cd firmware
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os
git checkout -b superpico 9fa79f45
git am ../patches/*.patch
make init
```

Then build with `scripts/build-firmware.sh` (the RP2040-Zero; `superpico` as
its argument for the prototype). The targets and what the patches change are
in `docs/psp2dc.md`, sections 3.5 and 4.3. After committing in
`firmware/joypad-os`, refresh the patches:

```sh
rm ../patches/*.patch && git format-patch 9fa79f45 -o ../patches
```
