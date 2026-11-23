# Files under their own licences

These keep the licence of the project they come from or change; PSP2DC's MIT licence (LICENSE) does not apply to them:

- firmware/patches/: changes to [joypad-os](https://github.com/joypad-ai/joypad-os), Apache-2.0 (licenses/joypad-os-LICENSE.txt). joypad-os itself is not in this repository; licenses/joypad-os-THIRD_PARTY_LICENSES.txt names what it incorporates. [MaplePad](https://github.com/mackieks/MaplePad), CC BY 4.0, was the reference for the VMU's screen handling in the patches.
- psp/usb/: PSPExp's kernel USB driver for the PSP, MIT, copied unchanged. Its USB descriptors and the driver's start and table come from PSPLINK's usbhostfs, Copyright (c) 2006 James F, BSD (licenses/PSPLINK.txt).
- psp/app/vmu_core.c, vmu_core.h: derived from [libevmu / ElysianVMU](https://github.com/gyrovorbis/libevmu), Copyright (c) 2023 Falco Girgis, MIT (the notice is in the file; licenses/libevmu-LICENSE.txt).

# What a release's programs are built with

The texts are in licenses/ and go with every copy of the programs (a release's zip):

- **The PSP's programs** (EBOOT.PBP, psp2dc_app.prx, pspexp_usb.prx) are built with PSPSDK and link its libraries, BSD (licenses/PSPSDK.txt), and newlib's C library in pspdev's port (licenses/newlib-COPYING.NEWLIB.txt).
- **The adapter's firmware** (.uf2) is joypad-os with the patches above, built with the Raspberry Pi Pico SDK, BSD-3-Clause (licenses/pico-sdk-LICENSE.txt), and TinyUSB, MIT (licenses/tinyusb-LICENSE.txt). "Joypad" is its author's trademark; PSP2DC is not affiliated with or endorsed by the joypad-os project.
- **The Dreamcast programs** (.cdi: the companion's demo, test and tutorial) are built with [KallistiOS](https://github.com/KallistiOS/KallistiOS) and link it, under the KOS License, a BSD licence (licenses/KallistiOS-LICENSE.KOS.txt, its authors in licenses/KallistiOS-AUTHORS.txt).

# Names

PSP and PlayStation are Sony's, Dreamcast and VMU are Sega's. They are used here only to say what PSP2DC works with; PSP2DC is not affiliated with or endorsed by either. No game, BIOS or other copyrighted data of theirs is in this repository or its releases.
