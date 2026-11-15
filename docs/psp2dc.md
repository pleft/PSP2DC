
# PSP Dreamcast Controller & VMU

**Status:** Research and feasibility assessment (source-level review done, no hardware tests yet)  
**Date:** November 2026 (revised 15 November 2026)  
**Target hardware:** Sony PSP-1000, Raspberry Pi Pico (RP2040), Sega Dreamcast

## 1. Project Overview

The goal is to transform a Sony PlayStation Portable (PSP) into a fully functional, enhanced Sega Dreamcast controller, using the PSP's physical controls and built-in screen.

Unlike conventional controller adapters, the project aims to reproduce the Dreamcast's Visual Memory Unit (VMU) functionality directly on the PSP display, while introducing additional capabilities beyond those of the original Dreamcast controller.

The solution should work with real, unmodified Dreamcast hardware and original games, without requiring Dreamcast-side software modifications.

The PSP will function as:

- A Dreamcast game controller.
- A live, full-size VMU display.
- A virtual memory card manager.
- A configurable controller with custom button mappings.
- An enhanced second-screen companion for Dreamcast games.

## 2. System Architecture

```text
+----------------------------------+
|          Sony PSP-1000            |
|                                  |
|  Custom PSP Homebrew Application |
|                                  |
|  - Physical controller input     |
|  - Analog stick and triggers     |
|  - Live VMU display              |
|  - Save management               |
|  - Enhanced user interface       |
+----------------------------------+
                 |
                 | Bidirectional USB
                 |
+----------------------------------+
|       Watara SuperPico            |
|       Raspberry Pi Pico           |
|       RP2040 Microcontroller      |
|                                  |
|  - USB host communication        |
|  - Controller translation        |
|  - Dreamcast Maple protocol      |
|  - VMU emulation                 |
|  - VMU framebuffer forwarding    |
+----------------------------------+
                 |
                 | Maple Bus
                 |
+----------------------------------+
|          Sega Dreamcast           |
|                                  |
|  Original hardware and games     |
+----------------------------------+
```

Communication is bidirectional:

**PSP → Dreamcast**
- Digital button states.
- Analog stick position.
- Emulated analog trigger values.
- Controller configuration commands.

**Dreamcast → PSP**
- VMU display updates.
- VMU-related information.
- Additional data exposed through the Maple protocol.

Controller communication must remain responsive even if VMU display updates are delayed.

## 3. Hardware Platform

### 3.1 Watara SuperPico

The selected prototype platform is an existing Watara SuperPico flash cartridge originally designed for the Watara Supervision handheld.

Project references:

- Hardware: https://www.pcbway.com/project/shareproject/Watara_Supervision_flash_cart_Superpico_5c2a9747.html
- Source: https://github.com/zwenergy/Superpico

The board contains:

- Raspberry Pi Pico with RP2040 microcontroller.
- Dual ARM Cortex-M0+ processors.
- Programmable I/O (PIO).
- Native USB 1.1 controller.
- 264 KB SRAM.
- Three 74LVC245 bus transceivers.
- Accessible BOOTSEL button.

The Raspberry Pi Pico is soldered to the SuperPico PCB and will remain attached.

### 3.2 Schematic Inspection

The original SuperPico schematic (revision 1.0, December 5, 2021) was inspected and cross-checked against the SuperPico firmware (`code/main.c`).

| RP2040 GPIO | SuperPico connection | Cartridge edge pin |
|-------------|----------------------|--------------------|
| GP0–GP7 | Address bus A0–A7 through U1 | 2–9 |
| GP8–GP15 | Address bus A8–A15 through U3 | 10–17 |
| GP16 | Address bus A16 through U4 | 18 |
| GP28 | Read control (#RD) through U4 | 1 |
| GP17 | Data D7, direct | 27 |
| GP18 | Data D6, direct | 28 |
| GP19 | Data D5, direct | 29 |
| GP20 | Data D4, direct | 30 |
| GP21 | Data D3, direct | 31 |
| GP22 | Data D2, direct | 32 |
| GP26 | Data D1, direct | 33 |
| GP27 | Data D0, direct | 34 |

Other edge pins: 5 V on pin 40, ground on pins 21 and 22.

**Buffer hazard.** All three 74LVC245 transceivers have DIR tied to 3.3 V and OE# tied to ground. They are permanently enabled and permanently drive GP0–GP16 and GP28 from the cartridge side. Consequences:

- These pins cannot carry bidirectional Maple signals.
- Firmware must **never configure GP0–GP16 or GP28 as outputs**, or the Pico and a buffer will drive the same net against each other. This rules out the default UART on GP0/GP1, SD card pins on GP10–GP13, and any LED or NeoPixel pin in that range.
- With the cartridge unplugged the buffer inputs float, so these pins read undefined values and are unusable as inputs too.

**Usable pins.** Only the eight direct data-bus pins are free: GP17, GP18, GP19, GP20, GP21, GP22, GP26, GP27. The onboard LED (GP25) is also available.

**Maple pins: GP21 (line A, Dreamcast pin 1) and GP22 (line B, Dreamcast pin 5).** The Maple PIO code requires two consecutive GPIOs. Both are reachable on the cartridge edge (pins 31 and 32), so the Dreamcast cable can be soldered to the edge fingers without touching the Pico.

**Debug UART: GP20 (UART1 TX, edge pin 30), 115200 8N1, transmit only.** The USB port is occupied by the PSP, so serial debug needs a GPIO. GP20 is the only direct pin with a hardware UART TX function, which is why Maple moved from the originally proposed GP20/GP21 to GP21/GP22.

The SuperPico must remain disconnected from the Watara Supervision when used as a Dreamcast adapter.

### 3.3 USB Connectivity

The intended arrangement is:

- RP2040 operating as USB host (full speed only).
- PSP operating as USB device.
- Bidirectional communication over the PSP's Mini-USB port.

**PSP side: already solved in the sibling PSPExp project.** The PSP's USB bus driver API (`sceUsbbdRegister`, `sceUsbbdReqSend`, `sceUsbbdReqRecv`) is kernel-only, so the USB code must be a kernel-mode PRX loaded by the application. PSPExp (`../PSPExp`, MIT, same author) contains exactly that: `psp/transport-usb` builds `pspexp_usb.prx`, a vendor-specific driver with bulk IN and bulk OUT endpoints and both high-speed and full-speed descriptors, loaded by a kernel-mode boot module. It runs on the target PSP-1000 (6.60 PRO-C) and has been measured at about 25 MB/s against high-speed hosts. The pspdev toolchain is at `../pspdev`.

PSP2DC will reuse this driver and boot module rather than derive from third-party code. Two other homebrew projects remain useful references:

| Project | Demonstrates | Notes |
|---------|--------------|-------|
| FuSa GamePad (GPL-3.0), https://github.com/andy-man/psp-fusa-gamepad | PSP as a driverless USB HID gamepad | Kernel PRX, one interrupt IN endpoint, nub and buttons, report sent every 4 ms. Documented for Phat PSPs on CFW 3.80+. |
| usbhostfs (PSPLINK), https://github.com/pspdev/psplinkusb | Bidirectional custom protocol | Vendor-specific interface with bulk IN and bulk OUT endpoints. |

Both register full-speed descriptors alongside high-speed ones, so a full-speed host should be accepted.

Not yet verified:

- Link stability and data transfer at full speed. Enumeration against the RP2040 host is confirmed (Phase 1).
- Whether the PSP needs 5 V on the USB VBUS pin to detect a host. Plan to supply it.
- The PSPExp driver at full speed: it carries full-speed descriptors, but every host used so far was high-speed.

**Pico side.** The cartridge-edge 5 V reaches the Pico, apparently on VSYS (to be confirmed with a multimeter). If so, the Pico's onboard diode prevents it reaching the USB connector, and 5 V must be fed to the Pico's VBUS pin instead to power the host port.

### 3.4 Maple Connectivity

The Dreamcast uses the proprietary Maple bus to communicate with controllers and accessories. The RP2040's PIO hardware is suitable for the timing-sensitive signalling.

Controller plug pinout: pin 1 = line A, pin 5 = line B, pin 3 = +5 V, pins 2 and 4 = ground (pin 2 doubles as device-detect).

The open-source reference designs (MaplePad, joypad-os) connect the two data lines directly to RP2040 GPIOs with the internal pull-ups enabled and no level shifting. This will be the starting point; the bus voltage levels have not been independently measured.

Planned wiring on the SuperPico:

| Dreamcast plug | SuperPico |
|----------------|-----------|
| Pin 1 (line A) | GP21, edge pin 31 |
| Pin 5 (line B) | GP22, edge pin 32 |
| Pin 3 (+5 V) | Pico VBUS (see 3.3) |
| Pins 2, 4 (GND) | GND, edge pins 21/22 |

## 4. Firmware Foundation

### 4.1 Options Reviewed

The original plan was to fork Pico2Maple. **This is not possible:** its repository (https://github.com/cluoma/Pico2Maple-fw) contains only compiled `.uf2` files, and the author states the firmware is not open source. Its only Maple pin options (GP16/GP17 or GP6/GP7) are also behind the SuperPico's buffers, so even the binary cannot be used.

USB4Maple (megavolt85) was checked as well. No source was found: nothing on GitHub, and the project wiki offers binaries only. The forum thread could not be read automatically and remains the one unchecked place.

| Project | Licence | USB host input | VMU storage | VMU screen | Maple pins |
|---------|---------|----------------|-------------|------------|------------|
| Pico2Maple | Closed | Yes | Yes | Yes (OLED) | Fixed, unusable here |
| USB4Maple | No source found | Yes | Yes | Yes (OLED) | Fixed (GP14/GP15), unusable here |
| joypad-os `usb2dc`, https://github.com/joypad-ai/joypad-os | Apache-2.0 | Yes (TinyUSB host, HID/XInput) | Code present but disabled in its release config: flash persistence still causes Maple timing drops | Not handled | Compile-time (`MAPLE_PIN1`/`MAPLE_PIN5`) |
| MaplePad, https://github.com/mackieks/MaplePad | CC BY 4.0 | No (buttons on GPIO) | Yes | Yes (192-byte framebuffer plus update flag) | Compile-time (`MAPLE_A`/`MAPLE_B`) |
| DreamPicoPort, https://github.com/OrangeFox86/DreamPicoPort | MIT | Has a client mode; not examined in depth | — | — | Configurable |

joypad-os and MaplePad share the same Maple PIO program (by Charlie Cole), which eases porting between them.

### 4.2 Selected Approach

**Base: joypad-os `usb2dc`.** USB-host-to-Maple is the harder integration and already exists there. **VMU screen handling will be ported from MaplePad.**

### 4.3 Required Firmware Work

**SuperPico board target — done, builds, not yet flashed**

Target `usb2dc_superpico` (CMake target `joypad_dc_superpico`) on branch `superpico` of `firmware/joypad-os`:

- `MAPLE_PIN1=21`, `MAPLE_PIN5=22`.
- stdio on UART1, TX GP20, no RX; USB stdio off.
- NeoPixel disabled, BOOTSEL as the button, onboard LED GP25.
- No SD card, no PIO-USB, no VMU (as in upstream's release configuration).

A source audit found no remaining code path that drives GP0–GP16 or GP28. This has not been confirmed on hardware.

Build with `scripts/build-firmware.sh`, which copies the fresh output to `build/` under a name carrying the build time. Never take the file from joypad-os's `releases/` folder: its names carry the commit, and after a commit an older file there was twice mistaken for the new build. By hand (the Homebrew `arm-none-eabi-gcc` lacks newlib, so the Arm GNU toolchain is used):

```sh
cd firmware/joypad-os
make init        # once: submodules, pico-sdk 2.2.0, TinyUSB 0.19.0
T=~/Dev/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi
PICO_TOOLCHAIN_PATH=$T PATH=$T/bin:$PATH make usb2dc_superpico
```

Output: `firmware/joypad-os/releases/joypad_os_<commit>_usb2dc_superpico.uf2` (a copy is kept in `build/`).

**PSP controller input — written, builds, not yet tested on hardware**

`src/usb/usbh/psp2dc/psp2dc_host.c` is a TinyUSB host class driver for the PSP (054c:01ca), compiled only into the SuperPico target (`CONFIG_PSP2DC`). It opens PSPExp's link with the sync packet, reads the PSP's messages from bulk IN, feeds controller state to the router, and sends a status message to the PSP once a second.

The RP2040 host controller runs one non-interrupt transfer at a time, so the driver is half duplex: it reads a message, sends whatever is waiting, then reads again. The PSP sends its controller state every frame, which gives the adapter a chance to send at least 60 times a second. If the PSP goes quiet for 300 ms, all buttons are released.

The message format is defined once, in `protocol/psp2dc_protocol.h`, shared by the firmware and the PSP application.

**VMU screen forwarding — works on hardware (15 November 2026)**

- joypad-os's VMU already advertised the LCD function (FT2) but did not answer for it. `vmu.c` now answers a media info request for the LCD and keeps each block write to it as the current screen, following MaplePad.
- The PSP2DC host driver takes each new screen on Core 0 and sends it to the PSP as an `LCD` message (192 bytes) over the bulk OUT endpoint. The Maple core only copies the 192 bytes.
- This needs the memory card emulation switched on (`CONFIG_VMU`), which on this target runs in RAM only and has Bluetooth removed to make room; about 250 KB of the RP2040's RAM is in use.
- The PSP application draws the screen four times enlarged at the bottom right of its status page, turned half round, since the Dreamcast sends it upside down for a VMU sitting reversed in a controller. Confirmed on hardware: GDMENU's screen reads upright and unmirrored.

**VMU storage — works on hardware (15 November 2026)**

joypad-os's VMU code is disabled upstream because writing the image to the Pico's flash disturbs Maple timing, and its SD card option needs pins that are unavailable here. So the card is kept on the PSP's Memory Stick and the adapter writes no flash at all:

- The adapter answers the Dreamcast from the 128 KB image in its RAM, as before.
- The PSP keeps the card as `ms0:/PSP2DC/card1.bin`, a plain 128 KB image (the usual VMU dump). It is outside the application's folder so that copying a new version of the application cannot remove it, and it is written to a second name and renamed, so an interrupted write leaves the last good file.
- When the link comes up, the PSP says whether it has a file and the adapter says whether it already holds a card. A freshly powered adapter takes the PSP's file (256 blocks), and only then shows the Dreamcast a memory card; with no file on the PSP, the adapter's freshly formatted card is used and sent to the PSP to keep. An adapter that already holds a card (the PSP was unplugged or restarted mid-session) keeps its own and sends all of it to the PSP.
- Each block a game writes is marked in the adapter and sent to the PSP, one block a transfer, taking turns with the VMU screen; the PSP writes the file 0.7 s after the blocks stop.
- A consequence: with no PSP connected the Dreamcast sees no memory card, only a controller with nothing in its slots.

Limiting the USB host on this target to one device and no hub freed about 30 KB of RAM (static data 249.7 KB to 219.2 KB), ending the earlier shortage.

## 5. PSP Homebrew Application

A dedicated PSP application is being developed using PSPSDK. A first version exists (`psp/boot`, `psp/app`): it sends the controller state every frame and shows a text status page with the link state, counters and the Dreamcast state being sent. Build it with `scripts/build-psp.sh` (needs `../pspdev` and a built `../PSPExp`); the result is `dist/PSP2DC/`, to be copied to `ms0:/PSP/GAME/PSP2DC/`.

In full: It consists of a user-mode application (display, UI, configuration) and a kernel-mode PRX containing the USB driver, so the PSP must run custom firmware. The USB driver and kernel boot module come from PSPExp.

The application will:

1. Read PSP buttons and analog input.
2. Translate inputs into Dreamcast controller states.
3. Communicate with the RP2040 over USB.
4. Receive VMU framebuffer updates.
5. Render VMU graphics on the PSP display.
6. Provide controller configuration and save-management interfaces.

### 5.0 Tools and Working Without Hardware

SELECT is a shift key: while it is held nothing is sent to the Dreamcast.

| Keys | Action |
|---|---|
| SELECT tapped alone | Cycle the stick source (nub, d-pad, none) |
| SELECT + Square | Start or stop recording the VMU's screens to `ms0:/PSP2DC/capture/vmu-NNN.rec` (kept in memory while recording, written at the stop, up to 1500 screens) |
| SELECT + Triangle | Screenshot of the PSP's screen to `ms0:/PSP2DC/shots/shot-NNN.bmp` |
| SELECT + Circle | Demo mode on or off (only while not connected) |

Demo mode plays every recording in `capture/` in turn at its recorded pace, or a made-up screen when there are none. It starts by itself when the USB driver is not usable, which is the case in PPSSPP: the driver module loads but its library calls return `0x8002013A`.

`scripts/dev/run-ppsspp.sh` installs `dist/PSP2DC` into PPSSPP and starts it; `scripts/dev/run-ppsspp.sh shot file.png` captures the emulator's window. PPSSPP's memory stick root is `~/.config/ppsspp`, so `ms0:/PSP2DC` is `~/.config/ppsspp/PSP2DC`. An empty file `dev_autoshot` there makes the application take screenshots by itself 3, 6 and 10 s after starting. Verified in PPSSPP on 15 November 2026: demo mode with the made-up screen, and screenshots written correctly. Recording needs the adapter and is untested.

### 5.0a Pages and Themes

Three pages (home, saves, status), changed with SELECT + Left/Right; the page, the theme and the stick's source are kept in `ms0:/PSP2DC/settings.txt`.

- **Home:** a drawing of the VMU after a photograph of the real one (off-white case between two seams, the connector's tabs and the notch on top, the screen in a stepped window, the lavender d-pad disc with its moulded cross, SLEEP and MODE, A and B), squeezed in height so its screen can be four times its size (192x128), and beside it three panels: the link (a coloured dot: green connected, yellow waiting, orange demo, red nothing; the theme's name), the controller as the Dreamcast sees it right now (L and R filling, the stick moving in its ring, d-pad, Start, and A B X Y lit in the Dreamcast's colours when pressed, the stick's source), and the memory card (saves, blocks free, the first save's animated icon, its state or, for two seconds, what just changed). SELECT + Up/Down changes the theme. (The first version drew the VMU eight times its size across the whole screen; on hardware that was too much.)
- **Saves:** the memory card's files as the BIOS shows them: the game's icon (animated), the long and short descriptions, file name, date, size, and the blocks free. SELECT + Up/Down scrolls, three files at a time. It reads the card the application holds, so it is up to date with what the adapter has sent.
- **Status:** the diagnostic page, with the VMU small.

Themes: Classic (the VMU's grey-green, dots drawn apart like the real LCD), Backlit, Pocket (handheld green, dots apart), Amber, Phosphor, Paper.

Every page is drawn whole into the frame buffer not on screen and shown at the next vblank, so nothing is seen half drawn. Verified in PPSSPP on 15 November 2026 with the real card (Crazy Taxi 2 and Sonic Adventure 2 saves, both icons correct); not yet on hardware. The icons' animation speed unit is not confirmed (taken as 1/15 s).

### 5.1 Controller Mapping

The initial mapping will include:

| PSP Control | Dreamcast Control |
|-------------|-------------------|
| D-pad | D-pad |
| Analog nub | Analog stick |
| Cross | A |
| Circle | B |
| Square | X |
| Triangle | Y |
| Start | Start |
| L / R | Analog triggers |

The exact face-button mapping will be configurable.

Because the PSP lacks analog triggers, configurable trigger emulation will be required.

### 5.2 VMU Display

The original Dreamcast VMU has a monochrome 48 × 32 pixel LCD.

The PSP has a 480 × 272 color display.

The application can therefore provide:

- Enlarged VMU graphics.
- Pixel-perfect scaling.
- Custom display themes.
- VMU status information.
- Virtual memory card selection.
- Controller profile information.

The original VMU framebuffer will be preserved so that games behave as expected.

## 6. Planned Features

### Essential Features

- PSP recognized as a Dreamcast controller.
- Digital button support.
- Analog nub support.
- Trigger emulation.
- Live VMU display on PSP.
- Virtual VMU save storage.
- Stable, low-latency controller operation.

### Enhanced Features

- Multiple virtual VMU cards.
- Save management and backup.
- Configurable button mapping.
- Per-game controller profiles.
- Custom VMU display themes.
- Controller and connection status.
- Enhanced VMU visualization.

### Future Possibilities

- Game-specific second-screen dashboards.
- Expanded game information.
- Game-specific VMU layouts.
- Advanced controller macros or accessibility features.
- Additional Dreamcast peripheral emulation.

Game-specific features will depend on data actually exposed through Maple/VMU communication.

Arbitrary game state cannot be obtained automatically without additional integration.

## 7. Additional Hardware Requirements

The commercial USB4Maple adapter was initially considered:

https://boutique-dragoncity.myshopify.com/products/dreamcast-usb4maple

Its firmware source is not available (see 4.1), so it offers no advantage over the SuperPico as a development platform.

| Component | Requirement |
|-----------|-------------|
| Watara SuperPico | Already available |
| Sony PSP-1000 with custom firmware | Already available (6.60 PRO-C) |
| Dreamcast controller cable or plug | Required |
| Micro-USB OTG adapter and Mini-USB cable | Required |
| 5 V feed to the Pico's VBUS pin | Required (from Dreamcast pin 3) |
| Maple interface components | None planned (direct connection) |
| USB-to-serial adapter (3.3 V) | Recommended, for debug output on GP20 |
| Spare Raspberry Pi Pico | Not available; would ease bring-up (see below) |
| External OLED display | Not required |
| Additional microcontroller | Not currently planned |

Only the SuperPico is available, so all bring-up happens on it. A spare, unsoldered Pico would still help: it has no buffer hazard and lets firmware problems be separated from SuperPico-specific ones.

A dedicated RP2040 PCB could eventually replace the SuperPico for a compact final adapter.

## 8. Development Roadmap

Each phase ends with a go/no-go check. Phases 1 and 2 are independent and can run in either order.

### Phase 0 — Research

**Status: Complete**

- SuperPico schematic and firmware inspected; pin constraints established.
- Pico2Maple and USB4Maple found to be closed; joypad-os and MaplePad selected.
- PSP custom USB device support confirmed: PSPExp's driver already runs on the target PSP-1000.
- joypad-os SuperPico target written and building.

### Phase 1 — PSP USB Proof of Concept

**Status: Passed (15 November 2026)**

Result: with the `usb2dc_superpico` build flashed and the SuperPico powered by 5 V on the Pico's VBUS pad, the PSP-1000 running PSPExp showed `attached, waiting for host sync` and `usb: packet 64`, i.e. the RP2040 enumerated and configured it at full speed. The counters read `attach 3 detach 2`; whether those extra cycles were re-plugging or spontaneous resets is still to be checked. The firmware booted with the idle LED pattern and no sign of buffer contention. Whether the PSP needs VBUS was not isolated, since 5 V was present.

- Back up the SuperPico flash (`picotool save`).
- Flash the `usb2dc_superpico` build and watch the debug UART on GP20.
- Plug the PSP in running PSPExp and confirm it enumerates at full speed (the firmware logs attached devices); alternatively run FuSa GamePad for a HID test.
- Establish whether VBUS is required.

**Go/no-go:** the PSP enumerates on the RP2040 host.

### Phase 2 — Dreamcast Controller with a Standard Gamepad

**Status: Skipped.** The PSP link was working before the Dreamcast was wired, so the Maple side was first tested with the PSP itself (Phase 3). The steps below remain useful for isolating a fault.

- Add a SuperPico board target to joypad-os (see 4.3). **Done.**
- Wire the Dreamcast cable (see 3.4).
- Test with an ordinary USB gamepad on a real Dreamcast.

**Go/no-go:** the Dreamcast detects the adapter as a controller and a game is playable. This validates the Maple wiring and GP21/GP22 independently of the PSP.

### Phase 3 — PSP as Dreamcast Controller

**Status: First pass achieved (15 November 2026).** A real Dreamcast, powered on with the adapter in a controller port, accepted the PSP as its controller: PSP-1000 → USB → SuperPico (joypad-os `usb2dc_superpico`, Maple on GP21/GP22) → Dreamcast. The SuperPico was powered from the console's 5 V (measured 4.93 V) on the Pico's VBUS pad, with the Maple lines wired directly to the GPIOs and no extra components. Tested by navigating GDMENU and playing Crazy Taxi 2 (stick steering on the nub, L/R as triggers, face buttons), which played perfectly. The game warns that no memory card is present and continues without saving. Latency has not been measured.

The build with the memory card (RAM only) and the VMU screen is `build/psp2dc_superpico_vmu_screen.uf2`; `build/psp2dc_superpico_phase3.uf2` is the controller-only build that played Crazy Taxi 2, kept to go back to.

Cable used: the lead from a third-party Dreamcast PS/2 keyboard adapter, cut from its board. Colours as wired: blue = +5 V (pin 3, measured), black and green = ground (pins 4 and 2), red = line A (pin 1), white = line B (pin 5). Connections are unsoldered jumper pins in the Pico's top-row holes on a breadboard, to be made permanent.

- Write the PSP2DC application on the PSPExp driver: read the controls, send controller packets. **Written.**
- Add the matching vendor host driver to joypad-os. **Written.**
- Test the two together on hardware. **Passed (15 November 2026):** the PSP showed `CONNECTED to the adapter`, `packet 64`, 2909 inputs sent with 0 errors and 0 timeouts, and 48 status messages received at one a second with 0 bad bytes. Data flows both ways at full speed.
- Finding (15 November 2026): after many attach/detach rounds in one run of the application (16, over several firmware flashes) the PSP's USB driver stopped receiving the adapter's sync: cable detected, `sync 0`, with every firmware including the known-good one. A cold boot of the PSP cured it. Three firmware changes were made chasing this before the known-good firmware was tried, which is the test that should have come first. The application now restarts the PSP's USB when the cable is in and no sync has come for 5 s, as PSPExp's runtime does; untested.
- Finding: on this PSP the driver's own start (`pspexpUsbStart`) fails with `0x80243001` because the USB bus driver is already running when the application starts, even after a cold boot, and stays running after a stop. The application now falls back to starting the steps itself and accepts "already done" for the bus; the device driver and activation then succeeded on the second try. What keeps the bus running (possibly the PSPExp plugin) is not established.
- Implement configurable mapping and trigger emulation (the first version has a fixed mapping, L/R fully pressed or released).
- Stick source, added because the target PSP's nub is worn and rests to the left: SELECT cycles between the nub, the d-pad driving the stick at full deflection, and no stick at all. The nub's resting position is deliberately not measured: its true centre is 128,128 and it frees itself when turned once round, so a measurement taken while stuck would be wrong. Built, not yet tried on hardware.
- Measure end-to-end latency.

**Go/no-go:** a real game is playable from the PSP.

### Phase 4 — VMU Screen and Storage

**Status: Complete**

- Port LCD (FT2) handling from MaplePad into joypad-os. **Written.**
- Define the USB protocol: controller IN, VMU frame OUT. **Done.**
- Extend the PSP application to receive and render the frame. **Written.**
- Test on hardware. **Passed (15 November 2026):** the BIOS shows a memory card in A-1 with 200 free blocks; Crazy Taxi 2 saved to it (180 free afterwards) and stopped warning; GDMENU's VMU screen appeared on the PSP, upright (one write of 50 words to block 0, taken whole); the controller stayed solid throughout with the card enabled.
- Crazy Taxi 2's animated screen (its logo with flames) plays on the PSP: 208 screens written, 208 taken, no errors, the controller unaffected. The game never asks for the LCD's media info; it only writes. A flickering band at the picture's top was the status text blanking it at each redraw, fixed in the application and confirmed on hardware.
- Saves kept on the PSP's Memory Stick. **Passed (15 November 2026)** with firmware `psp2dc_superpico_saves_C` and protocol 3: a new card was made and all 256 blocks kept on the PSP; Crazy Taxi 2's save came as 22 more blocks (its 20 plus the FAT and the directory) and the file was written a second time; after switching the Dreamcast off and on the PSP sent the card back (`sent to the adapter`), the BIOS listed CRAZY TAXI 2 SYSTEM DATA, 20 blocks, with its icon, and the game showed 180 free blocks. The VMU screen ran throughout (432 screens written, 432 taken) and no input failed to send.
- The application's USB restart worked on its first outing: the sync did not come at the start, the USB was restarted after 5 s and the link came up (`restarts 1`). So the deaf driver is not only a matter of many reconnections; why it happens at a fresh start is not yet known.
- The BIOS's file menu does draw on the VMU (a picture of a VMU), contrary to what was assumed earlier.
- More games' screens are to be tried.
- Decide and implement VMU save storage.

**Go/no-go:** the first major milestone (section 11).

### Phase 5 — Enhanced Features

**Status: Future**

- Controller configuration UI.
- VMU bank management.
- Game-specific controller profiles.
- Custom VMU themes.
- Second-screen enhancements.
- Performance optimization.

## 9. Technical Risks

1. **PSP against an RP2040 host:** the PSPExp driver is proven on this PSP against high-speed hosts, but not against a full-speed microcontroller host. VBUS detection is open. Addressed first, in Phase 1.

2. **Buffer contention on the SuperPico:** any firmware that drives GP0–GP16 or GP28 fights a permanently enabled buffer. Every inherited pin assignment must be audited before the first flash.

3. **joypad-os VMU maturity:** its VMU code is disabled upstream over Maple timing drops during flash writes, and it has no LCD support. MaplePad is the reference for both; storing saves on the PSP sidesteps the flash writes.

4. **Real-time performance:** USB host, VMU frame forwarding and flash or USB storage must not disturb Maple timing on the other core.

5. **Maple electrical interface:** direct connection works on this hardware in a first test; signal levels have not been measured and long-run reliability is unknown.

6. **USB host power:** the 5 V path to the USB connector depends on where the cartridge-edge 5 V lands on the Pico.

7. **Licensing:** joypad-os is Apache-2.0 and MaplePad CC BY 4.0; firmware combining them must carry both attributions. The PSP side reuses PSPExp (MIT, same author). FuSa GamePad (GPL-3.0) is used only as an unmodified test tool, not as a code base.

8. **Firmware preservation:** flashing replaces the SuperPico firmware. It can be restored from a `picotool` backup or rebuilt from the SuperPico repository.

## 10. Current Project Status

**The first major milestone is reached (15 November 2026):** the PSP controls a real Dreamcast through the SuperPico, shows the VMU's live screen, and keeps the memory card on its Memory Stick, where saves survive switching the console off.

Completed:

- Defined the overall project vision and architecture.
- Inspected the SuperPico schematic; identified usable pins and the buffer hazard.
- Chose GP21/GP22 as Maple pins and GP20 as debug UART, all reachable on the cartridge edge.
- Established that Pico2Maple and USB4Maple have no available source.
- Selected joypad-os as firmware base with MaplePad as the VMU screen reference.
- Found that PSPExp already provides a working kernel USB driver on the target PSP-1000.
- Added, built and flashed the `usb2dc_superpico` firmware target.
- Wired the Dreamcast cable and controlled a real Dreamcast from the PSP.
- The VMU's screen on the PSP and the memory card kept on the PSP's Memory Stick, both working on hardware.
- Wrote the PSP2DC protocol, the firmware's PSP host driver and a first PSP application; verified on hardware that controller data reaches the adapter and status messages come back.
- Phase 1 passed: the RP2040 enumerates the PSP (PSPExp driver) at full speed.

Not yet completed:

- Real-game compatibility and latency measurements.
- Permanent (soldered) wiring.

## 11. First Major Milestone

The first major milestone is:

**A Sony PSP-1000 connected to a real Sega Dreamcast through the SuperPico, controlling a game while displaying that game's live VMU graphics on its own screen.**

This will establish the core functionality of the project.

The longer-term vision is to transform the PSP into an enhanced Dreamcast controller and second-screen companion, combining the original Dreamcast experience with the PSP's display, controls, storage, and programmable capabilities.
