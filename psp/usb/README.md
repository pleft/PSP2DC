# USB driver

The PSP's kernel USB driver, `pspexp_usb.prx`, and the stub library the
application calls it through, `libpspexpusb.a`. Only kernel code can use the
PSP's USB bus driver, so the boot module loads this driver and the
application reaches it through its exported library (`pspexp_usb_driver.h`).

It is copied unchanged from the PSPExp project (MIT): `psp/transport-usb` at
commit `a0d56e1` (14 November 2026), with `protocol/pspexp_usb_link.h` (the
link's rules, here in `../../protocol/`). Its descriptors and driver start
follow PSPLink's usbhostfs (BSD licence, in `LICENSE.PSPLINK`).

The driver has more than PSP2DC uses (the HOME button, clock, volume): it is
kept as it is so that it stays the same as PSPExp's, which runs on the PSP.

`scripts/build-psp.sh` builds it before the application.
