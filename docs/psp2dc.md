
# PSP Dreamcast Controller & VMU

**Status:** Working on hardware: the PSP plays a real Dreamcast through the adapter, shows the VMU's screen and keeps the memory cards on its Memory Stick; standalone it is a VMU with its games. See section 10.  
**Date:** November 2026 (revised 17 November 2026)  
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

### 3.5 A Second Board: the Waveshare RP2040-Zero (16 November 2026)

A Waveshare RP2040-Zero as a smaller adapter. Found before the board was at hand:

- **joypad-os supports it:** target `usb2dc_rp2040zero`, documented as USB4Maple-compatible ("existing USB4Maple hardware can run Joypad OS firmware as a drop-in replacement"), Maple on GP14/GP15, the WS2812 on GP16 (`docs/adapters/dreamcast.md`, `src/CMakeLists.txt` there). USB4Maple hardware can therefore run this firmware too.
- **The board, from Waveshare's schematic (`RP2040_Zero.pdf`, files.waveshare.com):** the USB-C connector's VBUS pins are the net VSYS, which is also header pin 23, the "5V" edge pin, with no diode between them; VSYS feeds the 3.3 V regulator (RT9013-33 in the schematic; the product page names an ME6217C33). CC1 and CC2 have 5.1 kΩ pull-downs. The WS2812B's data is GPIO16. Header P1: GPIO0–GPIO15, GPIO26–GPIO29, 3V3, GND, VSYS (23 edge pins); GPIO17–GPIO25 and a second GND are pads underneath. Seen from above with the USB-C at the top, as in joypad-os's wiring drawing: left edge 5V, GND, 3V3, GP29, GP28, GP27, GP26, GP15, GP14; bottom GP13–GP9; right GP0–GP8.
- **So:** the Dreamcast's 5 V on the 5V pin powers the board and the USB-C VBUS the PSP is plugged into, as the Pico's VBUS pad does on the SuperPico. The two must never be powered together: unplug the Dreamcast before connecting the board to a computer. The PSP connects through a USB-C OTG adapter and the usual A-to-Mini-B cable.
- **Firmware:** target `usb2dc_psp2dc_rp2040zero` (patch 0009): the SuperPico target's PSP2DC build (PSP host driver, memory card in RAM, no Bluetooth) with Maple on GP14/GP15, the WS2812 on GP16 and the debug UART0 TX on GP0. It builds, 219 KB of static RAM as on the SuperPico; `scripts/build-firmware.sh rp2040zero`. Works on hardware (18 November 2026, build `psp2dc_rp2040zero_20261117_2326`, with patch 0011).
- Wiring drawing: `docs/wiring-rp2040zero.svg`, in the README.

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
- stdio on UART1, TX GP20, no RX; USB stdio off. **Turned off 17 November 2026** (patch 0011, see 5.0e6): printf held up the Maple replies. Re-enable `pico_enable_stdio_uart` only for a debugging session.
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
- The PSP keeps the card as a file in `ms0:/PSP2DC/cards` (section 5.0e; at first `ms0:/PSP2DC/card1.bin`), a plain 128 KB image (the usual VMU dump). It is outside the application's folder so that copying a new version of the application cannot remove it, and it is written to a second name and renamed, so an interrupted write leaves the last good file.
- When the link comes up, the PSP says whether it has a file and the adapter says whether it already holds a card. A freshly powered adapter takes the PSP's file (256 blocks), and only then shows the Dreamcast a memory card; with no file on the PSP, the adapter's freshly formatted card is used and sent to the PSP to keep. An adapter that already holds a card (the PSP was unplugged or restarted mid-session) keeps its own and sends all of it to the PSP.
- Each block a game writes is marked in the adapter and sent to the PSP, one block a transfer, taking turns with the VMU screen; the PSP writes the file 0.7 s after the blocks stop.
- A consequence: with no PSP connected the Dreamcast sees no memory card, only a controller with nothing in its slots.

Limiting the USB host on this target to one device and no hub freed about 30 KB of RAM (static data 249.7 KB to 219.2 KB), ending the earlier shortage.

## 5. PSP Homebrew Application

A dedicated PSP application is being developed using PSPSDK. A first version exists (`psp/boot`, `psp/app`): it sends the controller state every frame and shows a text status page with the link state, counters and the Dreamcast state being sent. Build it with `scripts/build-psp.sh` (needs `../pspdev`; PSPExp's USB driver is copied into `psp/usb`, unchanged, since 16 November 2026, and built there); the result is `dist/PSP2DC/`, to be copied to `ms0:/PSP/GAME/PSP2DC/`.

In full: It consists of a user-mode application (display, UI, configuration) and a kernel-mode PRX containing the USB driver, so the PSP must run custom firmware. The USB driver comes from PSPExp (copied into `psp/usb`); the boot module is shaped like PSPExp's.

The application will:

1. Read PSP buttons and analog input.
2. Translate inputs into Dreamcast controller states.
3. Communicate with the RP2040 over USB.
4. Receive VMU framebuffer updates.
5. Render VMU graphics on the PSP display.
6. Provide controller configuration and save-management interfaces.

### 5.0 Tools and Working Without Hardware

SELECT is a shift key: while it is held nothing is sent to the Dreamcast. (The table is the first version's; since 16 November 2026 recording and the demo are in System → Details, and SELECT opens and closes the menu: 5.0a, *SELECT a switch*.)

| Keys | Action |
|---|---|
| SELECT + Square | Start or stop recording the VMU's screens to `ms0:/PSP2DC/capture/vmu-NNN.rec` (kept in memory while recording, written at the stop, up to 1500 screens) |
| SELECT + Triangle | Screenshot of the PSP's screen to `ms0:/PSP2DC/shots/shot-NNN.bmp` |
| SELECT + Circle | Demo mode on or off (only while not connected) |

Demo mode plays every recording in `capture/` in turn at its recorded pace, or a made-up screen when there are none. It starts by itself when the USB driver is not usable, which is the case in PPSSPP: the driver module loads but its library calls return `0x8002013A`.

`scripts/dev/run-ppsspp.sh` installs `dist/PSP2DC` into PPSSPP and starts it; `scripts/dev/run-ppsspp.sh shot file.png` captures the emulator's window. PPSSPP's memory stick root is `~/.config/ppsspp`, so `ms0:/PSP2DC` is `~/.config/ppsspp/PSP2DC`. An empty file `dev_autoshot` there makes the application take screenshots by itself 3, 6 and 10 s after starting. Verified in PPSSPP on 15 November 2026: demo mode with the made-up screen, and screenshots written correctly. Recording needs the adapter and is untested.

### 5.0a Pages and Themes

Four pages (VMU, Saves, Cards, System), changed with L / R in the menu, which SELECT opens and closes (5.0a, *SELECT a switch*); the page and the theme are kept in `ms0:/PSP2DC/settings.txt`.

- **Tabs (16 November 2026):** SELECT + ◀▶ was hard to discover, so every page has a row of tabs at the top (VMU, Saves, Cards, System), the page shown lit. On the VMU page the row sits over the column beside the VMU (the drawn VMU fills the height). Later the same day the bar was found jumping between pages (short over the VMU page's column, the whole width elsewhere), so there is one header on every tab, the VMU page included, and the drawn VMU's buttons went: the header (L, the tabs, R, then the link's dot and status; connected SELECT ◀ … ▶) is now the same bar in the same place with the same spacing on all four pages, and so is the footer (the whole width). The drawn VMU keeps its case, its top band and its screen (still four times the LCD, with the icon row) from y 23 to 250, beside the right column's panels; its d-pad, SLEEP, MODE, A and B are gone (the panel beside it shows which PSP button is which), and with them the held buttons drawn sunk.
- **The pages redesigned (16 November 2026)**, the key mappings included. An audit of every screen found: seven hidden SELECT combinations (theme, record, demo, screenshot, card, new card, tab), SELECT + ▲▼ meaning the theme on one page and scrolling on another, and nothing listing them; L and R unused standalone while the tabs needed SELECT; one yellow for the tab shown, the row chosen, the card in use and New card; yellow text unreadable on the Paper theme; a Status page of debug text in white on black, the theme reachable only by a hidden combination; the Saves page not naming its card; a card menu of nine undivided choices with Delete like the rest; notes written for the code ("from the file", "sent to the adapter") and "Same file exist". The design:
  - *Keys.* Standalone, the PSP's buttons work the page shown and **L / R change the tab** (not while a box asks something); connected they are the Dreamcast's pad (✕ A, ○ B, □ X, △ Y, START, d-pad, the nub the stick, L R the triggers) and SELECT held works PSP2DC: SELECT + ◀▶ the tab, SELECT + L/R the card, SELECT + START a new card, SELECT + △ a screenshot, SELECT + ▲▼ scrolls a list (or chooses a setting), SELECT + ✕ a setting, SELECT + ○ back from Details. The buttons of a SELECT combination count once: let go of SELECT first and the other button, still held, does nothing more (no second action, nothing to the Dreamcast) until it is let go too. Card names the Memory Stick lists in capitals (short 8.3 names come back so) are shown in small letters, as PSP2DC makes them. The theme moved from a SELECT combination to the System page's settings; recording the VMU and the demo, tools for testing (not for playing), to its Details page. The VMU standalone is unchanged (d-pad, ✕ A, ○ B, △ MODE, □ SLEEP).
  - *Every page the same way.* A header: the tabs with the key that changes them (L … R standalone, SELECT ◀ … ▶ connected), then a dot of the link's colour and the card in use with what became of it (or a message just given). A title line naming what the page shows. A footer listing only the keys that do something now, each drawn as on the PSP (its symbol, the arrow pair, the d-pad, or its name in a key cap).
  - *Colour.* Each theme has an accent (yellow; orange on Paper) for what is chosen and only that: the tab, the row, the setting, the key, the choice in a box (filled, its text black or white as the colour asks). Green is the card in use (its name, an "in use" badge). Red is what deletes (Delete, "Yes, delete it", filled red when chosen). Secondary and greyed text are blended toward the panel, so they are dimmer on light themes too (halving the colour made them darker on Paper).
  - *Saves:* the card's name, its saves and free blocks on the title line; three saves a screen on panels, the description in the strongest text. *Cards:* New card an outlined row with a + (filled only when chosen), each card with an "in use" badge, its game, and a bar of how full it is. *Boxes:* the title in bold (a save's description rather than its file name), the choices in groups (Use, Import | Rename, Duplicate | Back up, Restore | Defragment, Check | Delete), Delete in red.
  - *System* (was Status): the connection in words (Standalone / Connected / Cable in / Demo, and what that means), the adapter's firmware and the card; the settings (Theme ◀ ▶, and Details: the old status page, for a report, with a panel "Tools, for testing": Record the VMU, which writes the screens the Dreamcast draws to capture/vmu-NNN.rec, and Demo, which plays recordings back as if connected; ▲▼ ✕ there, ○ back, or with SELECT held when not standalone); and every key, by mode. The tools were in the settings at first, where their purpose was unclear; they went to Details.
  - *Words:* the card's notes as a player would say them (on the Memory Stick, in the Dreamcast, from the Dreamcast, saved), "Already there", and changing card no longer says "changed".
  - Checked in PPSSPP on every page and box, standalone and in demo mode (which draws as connected), on the Classic, Backlit, Pocket, Amber and Paper themes.

- **Keys, one map for both modes (16 November 2026; replaced the same day by SELECT as a switch, below).** The tab key was counter-intuitive: L / R standalone but SELECT + ◀▶ connected, where SELECT + L / R changed the card instead. One set of keys now serves both modes. On the PSP-1000 SELECT sits on the right, under the face buttons, so SELECT + ✕ (or any face button) takes the same thumb twice: SELECT combinations work with the left hand's keys, the d-pad and L / R. Now, standalone and connected alike:
  - **SELECT + L / R** the tab (not while a box is open). Plain L / R no longer change it standalone.
  - **SELECT + ▲▼** choose in a list or the settings; **SELECT + ▶** OK, open, go in; **SELECT + ◀** back, as in the VMU's own FILE mode (right goes on, left back). On the Theme row ◀▶ change the theme; on the rename keyboard ▶ is Done and ◀ cancels.
  - **SELECT + △** the screenshot, kept there (a testing tool, seldom used).
  - **An open box** (actions, a question, a result, the keyboard) also takes the buttons themselves: ▲▼, ✕ OK, ○ back (the keyboard: d-pad, ✕ type, □ delete, START done). Connected, nothing goes to the Dreamcast while one is open. A box closes when the mode changes.
  - **The plain buttons** are otherwise the game's: the Dreamcast's pad connected; standalone the VMU's on the VMU tab, and nothing on the other tabs (the VMU is not shown there).
  - **The card** is changed on the Cards tab in both modes: SELECT + ▲▼ to it, SELECT + ▶, *Use this card* (connected the adapter lets go of its card first, as before, and "Card busy" asks to try again when a block was written in the last 2 s). **New card** is the tab's first row; SELECT + START and SELECT + L / R for the card are gone. Connected a card's other actions are greyed ("done standalone"), and the card in the Dreamcast has none.
  - **Saves:** importing is on the Cards tab (a card's actions, *Import a save into it*); △ on the Saves page went with SELECT + △ being the screenshot.
  - The header reads SELECT L … R on every tab in both modes; the footers start with SELECT + and the page's keys, a box's footer lists its plain keys; System's key list has two groups, *Holding SELECT: every tab* and *In a box: no SELECT needed*.
  - Checked in PPSSPP with a scripted run, standalone and in demo mode (which works as connected): plain R does nothing, SELECT + R changes the tab; a save's actions opened with SELECT + ▶ and worked with the plain d-pad, ○ closing them, plain ✕ doing nothing outside a box; the rename keyboard typed, SELECT + ◀ back; New card made and put in use; the theme changed with SELECT + ▶; Details opened and Demo turned on with SELECT + ▶, left with SELECT + ◀; in demo the Saves page scrolled, a card's actions showed only Use, it was used, the card in use answered "In the Dreamcast now", and plain ○ closed a box; SELECT + △ took a screenshot three times of three. The connected change through the adapter (CARD_EJECT) is the same code as before, moved, and is not yet tried on hardware.
- **SELECT a switch, short words, greyed choices (16 November 2026).** Holding SELECT with a key was still two buttons at once, and the boxes' long explanations ("In the Dreamcast now. What else can be done with a card is done standalone") read poorly. SELECT now works like caps lock, and what does not apply (connected, say) is greyed rather than explained. This replaces the SELECT combinations above:
  - **SELECT switches between playing and the menu**, in both modes. Playing: the VMU tab, the buttons the game's (the Dreamcast's pad, or the VMU standalone). In the menu: L / R the tab, ▲▼ choose, ✕ OK, ○ back, ◀▶ on the theme, △ a screenshot; nothing goes to the Dreamcast (all released, the stick centred), and the VMU standalone gets no buttons. SELECT again goes back to playing, on the VMU tab, any box closed. A button held across the switch counts only once let go. The Dreamcast pad has no SELECT, so nothing is lost. It starts playing.
  - **The header shows which:** in the menu the bar is lit in the accent with L … R, the tab shown cut out of it; playing it is dim, "SELECT menu", the other tabs faint. Every footer in the menu starts with "SELECT play". The switch was not visible enough: on the VMU tab only the header and footer changed, and the panel beside the VMU still showed the VMU's buttons (or the Dreamcast's controller) though they did nothing in the menu. So in the menu the whole screen is framed in the accent (2 pixels, joined to the lit header), and that panel's buttons are faded toward the panel (`ui_fade`, 11/16), its title kept. Checked in PPSSPP on Classic and Paper, standalone and in demo.
  - **What cannot be done now is greyed, never explained.** Connected: a save's actions but Info, a card's actions but Use. Copy and Move with no other card or on a copy-protected save; Restore with no backups; Import with nothing in `saves/`; Use and Delete on the card in use; while a VMU game runs, the card in use's actions but Duplicate and Back up, Use on others, and New card. With every action greyed (the card in the Dreamcast) nothing is chosen and the footer offers only ○ back.
  - **Results are a few words in the header** for two seconds ("copied to card2", "card2 has it already", "no room on card2", "card3 made, in use", "card busy, try again", "renamed card2b", "no problems found"), the box closed, nothing to dismiss. Boxes remain for choices (actions, lists, "Delete this save?" with No first), Info, and a check that found problems; their titles are short ("Copy to", "Import into card2", "Restore card1 from", "Delete card2?") with no explanatory lines. Page texts shortened too ("No saves", "Memory cards 3", New card "empty, put in use").
  - The System page's key list: the Dreamcast's pad, the VMU's, and *The menu* (SELECT menu / play, L R tab, ▲▼ choose, ✕ OK, ○ back, △ shot).
  - Checked in PPSSPP with a scripted run, standalone and in demo mode (which works as connected): play and menu headers; tabs with L / R in the menu only; a save exported ("exported as C_TAXI02"), its delete question; the card in use's actions with Use, Restore and Delete greyed; an import into another card ("imported into card2"); New card ("card3 made, in use"); the theme and Details; in demo, ✕ while playing lit the Dreamcast's A, a save's actions with only Info, Info shown, another card's actions with only Use, the card in use's all greyed with only ○ back offered. The connected card change through the adapter is unchanged code, not yet tried on hardware.
- **Home:** a drawing of the VMU after a photograph of the real one (off-white case between two seams, without the connector on top to save room, the screen in a stepped window, the lavender d-pad disc with its moulded cross, SLEEP and MODE, A and B), squeezed in height; its screen window nearly square as on the real one (about 1.2:1): the dots at its top, square, 192x128 (the dot area is given as 37 x 26 mm), and the LCD's icon row under them (FILE, GAME, CLOCK, FLASH, drawn after photographs of lit VMUs) as faint unlit ghosts, since the VMU drives them itself and the Dreamcast cannot; Classic uses the lit screen's light teal and blue-violet. Two earlier attempts were wrong: 192x160 from a photograph, and 192x135 from the dot area's published size, which leaves out the icon row, and beside it three panels: the link (a coloured dot: green connected, yellow waiting, orange demo, red nothing; the theme's name), the controller as the Dreamcast sees it right now (L and R filling, the stick moving in its ring, d-pad, Start, and A B X Y lit in the Dreamcast's colours when pressed), and the memory card (saves, blocks free, the first save's animated icon, its state or, for two seconds, what just changed). SELECT + Up/Down changes the theme. (The first version drew the VMU eight times its size across the whole screen; on hardware that was too much.)
- **The right column, reworked (15 November 2026):** the middle panel was too large and wordy; the middle panel is centred between the link and the card and about a third of its old height. Standalone it is titled STANDALONE (it said "VMU on its own") with the mode at the right (TIME, FILE, GAME, PLAYING, SLEEP) and only the button map, drawn with the PSP's symbols in their colours (the d-pad, ✕ A, ○ B, □ SLEEP, △ MODE; while a game plays, △ "hold 2s: leave"); the two lines repeating the modes are gone, as is the VMU CPU time shown while playing (it was a measurement). Connected, the controller is drawn in one row (stick, d-pad, Start, A B X Y) under the L and R bars. The keys at the bottom are drawn with arrows (SELECT+ ◀▶ page, ▲▼ theme, L/R card). Every label is kept within its panel and cut with an ellipsis if longer, the messages are 24 characters or fewer, and the full-width lines of the other pages stop at the screen's edge (messages such as "both changed: Dreamcast's kept as card1-dc" ran off the screen before). The stick-source switch (SELECT tapped alone) was removed as unneeded. The card panel then took the freed space: it stays in one place connected or standalone (the middle panel centred above it) and shows the card's name, its free blocks, and its saves' icons in a grid, five a row and two rows, each animated at its own speed; a save without an icon is a tile with the start of its file name, and with more than ten the last cell counts the rest ("+4"). The names and details are on the Saves page. Checked in PPSSPP with cards of 3, 13 and one save without an icon.
- **Saves:** the memory card's files as the BIOS shows them: the game's icon (animated), the long and short descriptions, file name, date, size, and the blocks free. SELECT + Up/Down scrolls, three files at a time. It reads the card the application holds, so it is up to date with what the adapter has sent.
- **Status:** the diagnostic page, with the VMU small.

Themes: Classic (the VMU's grey-green, dots drawn apart like the real LCD), Backlit, Pocket (handheld green, dots apart), Amber, Phosphor, Paper.

Every page is drawn whole into the frame buffer not on screen and shown at the next vblank, so nothing is seen half drawn. A frame begun before the previous swap had happened was drawn into the buffer still on screen, a flicker on hardware that PPSSPP did not show; `ui_begin` now waits for that vblank, and the CPU runs at 333 MHz. Confirmed on hardware (15 November 2026): no flicker. The status page shows the drawing time per frame. Verified in PPSSPP on 15 November 2026 with the real card (Crazy Taxi 2 and Sonic Adventure 2 saves, both icons correct); not yet on hardware. The icons animate at n/30 s per frame, as the Sega VMU manual specifies (VMT-18).

### 5.0b Facts from the Sega VMU Manual

`docs/VMU.pdf` (778 pages: tutorial, specifications, hardware, programming, BIOS, sound, simulator; not kept in git) settles several points that were guesses before:

| Fact | Where | Used for |
|---|---|---|
| The LCD is a 48x32 dot matrix plus four icons below it: File management, Executable file initiation (game), Clock display, Writing in progress (also listed as File, Game, Time, Attention) | Specifications VMU-11/12, VMU-2 | The drawn VMU's icon row and the icons' shapes |
| "The System-BIOS uses the icons; use by an executable file is prohibited"; connected to a game machine, the BIOS puts the game's drawing data straight on the dot matrix | VMU-11/12 | The icons are drawn unlit |
| LCD: no backlight, no contrast or brightness control; recommended refresh 4 Hz when connected to a game machine (1 Hz standalone) | VMU-12 | (background: games that animate faster ghost on the real LCD) |
| A save's animated icon switches frames every n/30 seconds (n = the header's animation speed, 1 to 65535); 1 to 3 frames | Tutorial VMT-18 | The save browser's icon animation (it had assumed n/15) |
| The visual comment (eyecatch) is 72x56 dots, type A direct ARGB4444, B 256 colours, C 16 colours | VMT-18, VMT-20 | A later eyecatch view |
| A card's volume icon is the file ICONDATA_VMS: a 16-byte comment, a 32x32 monochrome icon and an optional colour icon | VMT-13 | A later card icon in the saves page |
| Clock and alarm are Maple FT3 (Timer); the game machine can set the date and time; the alarm sounds the voltage buzzer, 300 Hz to 4 kHz with a settable duty ("planned") | VMU-16/17 | VMU beeps through the PSP |
| Maple at 2 Mbps; all of it handled by the VMU's System-BIOS | VMU-15 | (background) |

### 5.0c Standalone: the VMU Without a Dreamcast

With no Dreamcast connected (no adapter, or no USB as in PPSSPP), the PSP behaves as a VMU taken out of the controller (`psp/app/vmu_sa.c`, after the Sega manual's mode settings, VMU-7). The PSP's buttons become the VMU's: d-pad, Cross = A, Circle = B, Square = SLEEP, Triangle = MODE (SLEEP and MODE sit side by side above A and B on the VMU) (SELECT is still the shift key). The drawn VMU's buttons sink while held.

- **MODE** goes round GAME, TIME and FILE, lighting that mode's icon; it starts in TIME, as a VMU does on leaving the console.
- **TIME:** year, month/day, hh:mm:ss and the weekday, from the PSP's clock.
- **FILE,** after the VMU's instruction sheet (Sega 670-14102A, "File Mode", from dreamcast.wiki; the developer manual in `docs/VMU.pdf` only says File mode can "display, copy, and delete files"). Screens in the sheet's order:
  - *File selection:* `Free 200` (free blocks of the 200) and `Game 128` (room for a mini-game: one to a card, from block 0 up and unbroken, at most 128 blocks, as the developer manual gives it; 0 when the card has one). A or right goes on.
  - *File display:* `#001/002`, the 12-character name over two lines (shown inverted when the file is copy-protected, directory byte 0x01 = 0xFF), the date month/day/year as the sheet labels it (YY/MM/DD before). Up/down pick a file; right goes on; B or left back.
  - *Comment display:* the file's 16-character comment (the header's "description shown in VMS file menu", mc.pp.se), the blocks it uses, `Data` or `Game`, and the arrow. Right goes on; B or left back.
  - *Action selection:* `Copy` / `Delete`, `A:Yes`, `B:No`; up/down choose (a copy-protected file has no Copy), A takes it, B or left back.
  - *Copy:* a VMU copies to a second VMU plugged into it; here the other "VMU" is another card in `ms0:/PSP2DC/cards`, chosen on a `Copy to` screen (up/down), then the sheet's `Copy ?` with `Yes No` (left/right, A enters; No goes back), `Copying!` with the ATTENTION icon lit, and `Copied`, or its errors: `Same file exist`, `Not enough free memory`, `Copy failed`. The file goes where the BIOS puts it (mc.pp.se): a game from block 0 up, data in the highest free blocks; the other card is written as any card (`.new`, then renamed).
  - *Delete:* the sheet shows no screens for it: `Delete ?` as `Copy ?` but with No chosen first, then `Deleted`; the file's blocks freed in the FAT and its directory entry cleared, and the card written to the Memory Stick.
  - The FAT and directory are found from the root block (0x46 and 0x4a) when its fields make sense together, else at their usual places: cards formatted by the adapter or the PSP before 15 November 2026 have those fields 4 bytes late (5.0f).
  - Checked in PPSSPP with a scripted run and the card files compared byte by byte afterwards: a 20-block save copied to another card the same in every byte (blocks 199 down to 180), copying it again refused with `Same file exist`, deleting it freed its 20 blocks and left the other save untouched.
  - On hardware (15 November 2026): the Sonic Adventure 2 save copied from `card1` to `card2` (`Copied`), copying it again refused (`Same file exist`), and the stray Chao Adventure 2 copy deleted from `card2`, its blocks freed.
- **GAME:** A runs the card's game (5.0d).
- **SLEEP** blanks the screen and the icons; any button wakes it.
- The text uses an 8x4 grid of 6x8 cells in a font drawn for PSP2DC (not Sega's); the layout follows photographs of a real VMU, since the manual does not describe the BIOS screens.

Connected, the ATTENTION icon lights for a second after the card is written, as the manual describes ("lights for Read/Write while flash memory is being accessed"); only writes are seen by the PSP. Checked in PPSSPP (TIME, FILE with browsing, SLEEP); the connected ATTENTION icon is not yet tried on hardware.

`scripts/dev/run-ppsspp.sh press BUTTON` presses a PSP button in the emulator, so screens can be stepped through from the Mac.

### 5.0d GAME Mode: the VMU's Games

In GAME mode, A runs the card's game on an emulated VMU (`psp/app/vmu_game.c` over `psp/app/vmu_core.c`). The PSP's buttons drive it, its screen and icons show on the drawn VMU, the buzzer plays as a square wave through the PSP's speaker, and the game is left when it hands back to the BIOS (MODE in most games, or at its own end) or with MODE held 2 s whatever it does. The card is then read back and, if the game changed it, written to the Memory Stick. No BIOS image is needed: the core's software BIOS stands in for Sega's.

**The core.** `vmu_core.c` (about 900 lines of plain C) is derived from libevmu, the ElysianVMU core (MIT, Copyright (c) 2023 Falco Girgis, github.com/gyrovorbis/libevmu, commit 2b15502; the licence is in the file). libevmu itself was tried first and ran far too slowly on the PSP (about 388 ms to emulate a frame in PPSSPP): a profile showed its support library, libgimbal, doing type lookups, thread lookups and mutex locks on every emulated instruction. `vmu_core` keeps libevmu's emulation (CPU, memory, timers, interrupts, screen, buzzer, its software BIOS) without that, in integer arithmetic only (the PSP's FPU has no double precision).

**Proof.** `scripts/dev/vmucore-compare` runs libevmu and `vmu_core` side by side on the Mac with the same buttons and compares the screen, the icons, the buzzer and the whole machine state every 1/60 s, and the flash at the end. All 127 `.vms` games of a collection matched for 60 s each with MODE pressed at 50 s; Chao Adventure, Chao Adventure 2, Metal Gear Stealth, Bounce and AlienFighter for 180 s; the nine games that saved left byte-identical flash. On the Mac `vmu_core` is 80 to 108 times faster than libevmu. One theoretical difference remains: libevmu ends a step by summing doubles, and a game on the fast CF clock could run one instruction more there at a step's very end; none of the 127 did.

**On the PSP build.** Chao Adventure 2 runs in real time in PPSSPP: the VMU CPU shows 0.0 ms a frame (under 0.1 ms; libevmu took 388 ms). Its title, story text and the Chao were seen. **On hardware (15 November 2026): Chao Adventure 2 plays on the PSP-1000 on its own, the buzzer's mail sound heard through the PSP's speaker.** Measured there while playing: VMU CPU 0.0 to 1.2 ms a frame (more when the game runs the VMU on its fast clock, or a frame covers more VMU time after a stall), drawing 8.4 ms a frame, longest 16.0 ms; together well within the 16.7 ms of a frame. The drawing is now the larger cost, mostly repainting the unchanging VMU case; drawing that once and copying it would cut it, if ever needed. The application module is 241 KB (1.8 MB with libevmu).

Two traps in libevmu's API, found by running games and kept in mind by the comparison: `EvmuTicks` is documented as milliseconds but the CPU's update takes nanoseconds; `EvmuLcd_pixel` is declared `(row, col)` but takes `(x, y)`.

`scripts/build-evmu.sh` fetches libevmu at that commit (into `third_party/`, not in git) for the comparison; the PSP build no longer needs it. `scripts/dev/card-add-game.py` installs a `.vms` game into a copy of a card image.

### 5.0e Several Memory Cards

The cards are files in `ms0:/PSP2DC/cards`: any `NAME.bin` of 128 KB is a card, so they can be named on a computer (`saves.bin`, `games.bin`). The card in use is kept in the settings (`card=`). The single card of earlier versions (`ms0:/PSP2DC/card1.bin`) is copied into the folder as `card1` on the first start and removed once the copy reads back the same (a rename into another folder failed under PPSSPP).

- (Until 16 November 2026; since then the card is changed on the Cards tab, 5.0a.) **SELECT + L / R:** the previous or next card (by name); the panel and the top line show its name, and for two seconds "card: NAME (2 of 3)".
- **SELECT + Start:** a new card, formatted as a Dreamcast formats one (200 blocks free), named `cardN` with the first free N, and changed to.
- **Standalone** (no Dreamcast) the VMU starts afresh with the other card, as another VMU would: TIME mode, nothing chosen in FILE (it kept its FILE screen and file number before). The change is immediate, the card in use written first if it changed; not while a VMU game runs.
- **Connected,** the change looks to the Dreamcast like pulling the VMU out and putting another in (`CARD_EJECT`, since protocol 4): the PSP asks once nothing is on its way (no block pending, none in the last 2 s; otherwise "card busy: try again"), the adapter takes the card away and answers EMPTY, the PSP changes card and sends it, and the adapter shows it again a second later at the earliest.
- **Reconnecting** to an adapter that still holds a card is 5.0f.
- A file is tested for by opening it: `sceIoGetstat` said "not there" for files that were there under PPSSPP, and a new card must never overwrite an old one.

Checked in PPSSPP (moving the old card, changing with SELECT + L/R, making a card), using `ms0:/PSP2DC/dev_input.txt`, a timed list of buttons the application holds when the file is there (combinations cannot be held from the Mac's keyboard). On hardware (15 November 2026): the old card was moved into the folder; a new card made while connected (SELECT + Start) was seen by the Dreamcast, and Sonic Adventure 2 saved onto it; a card made on the Mac (`games.bin`, Chao Adventure 2) was chosen with SELECT + R and its game played.

### 5.0e2 The Saves Page: Copy, Export, Delete

Standalone, the Saves page (SELECT + ◀▶) has a chosen save, framed in yellow; ▲▼ choose and ✕ opens a box of what can be done with it, ○ going back:

- **Copy to another card:** the other cards are listed; the copy is placed as the VMU's BIOS places files, with its errors (`Same file exist`, not enough free blocks, a game where the card already has one). A copy-protected save cannot be copied, as on the VMU (greyed).
- **Move to another card:** a copy, the other card read back to see the save there with the same bytes, and only then the save deleted here; anything short of that leaves it where it was ("It stays on this card"). Copy-protected saves cannot be moved either. Checked in PPSSPP: a move refused (`Same file exist`) left the save in place; a move went through, the save on the other card with the same bytes and gone from this one.
- **Export to the Memory Stick:** the save as files other tools load, in `ms0:/PSP2DC/saves/`: `NAME.DCI` (Nexus: the 32-byte directory entry, then the blocks with every 4-byte word reversed, as libevmu's `EvmuNexus_applyByteOrdering`), and `NAME.VMS` with `NAME.VMI` (the save's blocks as they are, and the 108-byte description of mc.pp.se's `vmi.html`: checksum the first four characters of the name ANDed with "SEGA", the save's long description and date, game and protect bits, size). NAME is the save's file name in 8 characters, numbered rather than written over anything. Copy-protected saves can be exported (the VMI keeps the bit).
- **Delete:** asks first, "No, keep it" chosen; then the card is written.

Connected the page only shows (the Dreamcast has the card then), and no action is taken while a VMU game runs (it holds its own copy of the card and writes it back when it ends). While the Saves, Cards or System page is shown standalone, the buttons are theirs, not the hidden VMU's.

The file system code is now one module, `psp/app/cardfs.c` (list, copy, delete, exports), used by FILE mode and the Saves page alike, and the page lists saves through it so its order is the actions'. `scripts/dev/cardfs-test.c` checks it on the Mac against a .VMS, .DCI and .VMI of the same game from the web (Chao Adventure): the VMS export is the .vms byte for byte, the DCI's blocks the .dci's byte for byte, the VMI's checksum, mode, size and file number the .vmi's; copy, "Same file exist", one game a card, delete: 21 of 21. In PPSSPP the page copied the Sonic Adventure 2 save to another card (the same bytes there), exported it (the three files checked byte for byte against the card) and deleted it.



### 5.0e3 The Cards Page, Imports, Backups, Info, Check and Repair

**The Cards page** (SELECT + ◀▶, between Saves and Status), standalone: **New card** first, in its own colour so it is easy to find (made, formatted as a Dreamcast formats one, and put in use; SELECT + Start still does the same anywhere), then each card with its saves, free blocks and game, the one in use marked. ✕ on a card opens its actions:

- *Use this card*; *Import a save into it*; *Rename* (an on-screen keyboard, 16 November 2026, replacing letter-scrolling, which made typing impractical: the name in a field, under it the keys a card's name may have, a to z, 0 to 9, - and _, then Delete and Done; the d-pad moves among the keys, ✕ types the one lit, □ deletes the last character, START or Done renames, ○ goes back; the list then follows the card to its new place); *Duplicate* (as NAME-copy); *Back up now*; *Restore a backup* (any backup, newest first, asked first, the card backed up before); *Defragment*; *Check and repair*; *Delete this card* (not the one in use, not the last; asked first; a backup is kept).

**The Saves page** gains *Info* (first in a save's actions) and △ to import a save into the card.

- **Import** reads `ms0:/PSP2DC/saves/`: `.DCI` files, and `.VMS` files with their `.VMI` when there is one (its name, date, game and protect bits). A `.VMS` without one is a save when it starts with a save's header and a game otherwise (a game's block 0 is code): all 127 games of a collection were taken for games, 12 of them only once Japanese half-width katakana and NUL padding were allowed in a header's description, and one homebrew has no header at all. Errors as for a copy (name taken, no room, one game a card).
- **Info:** both descriptions, the application that made it, kind, size, protection, date, icons, the large picture's kind, and a **check**: data saves carry a CRC-16 (polynomial 0x1021, from 0, over the header with its CRC field zeroed, the icons, the picture and the data: mc.pp.se's fileheader.html, libevmu's `EvmuVms_computeCrc`), which matched on both real saves tried (Crazy Taxi 2, Sonic Adventure 2); games carry none. The large picture (72x56, 16-bit colour or a 256- or 16-colour palette, after the icons) is decoded after libevmu's `EvmuVms_createEyecatchArgb4444` and tested only with a picture made for the test: no save or game at hand has one. Info shows it twice its size beside the text.
- **Backups** go to `ms0:/PSP2DC/backups/NAME@YYYYMMDD-HHMMSS.bin`. Automatic ones (`-auto`, the newest ten a card kept) are made before anything takes something off a card (a delete or move, FILE mode's delete, a restore, defragmenting, a repair, a card deleted) and once a session (the application's start, each link) before the card in use is first written, so a Dreamcast session that goes wrong can be undone.
- **Defragment:** a game from block 0 up and the saves packed from block 199 down, every byte unchanged: the room for a game (one unbroken run from block 0) is then as large as the free blocks allow.
- **Check and repair:** reports what is wrong in words: a misplaced root layout, the system blocks' FAT entries, lost blocks (marked used, of no save), saves whose blocks are broken or shared with another, saves whose CRC is wrong. Repair puts right only what loses nothing (the root layout, the system entries, lost blocks freed); broken saves are left to be deleted. A delete never frees a block another save also uses.

The logic is in `psp/app/cardfs.c` and `cards.c`; `scripts/dev/cardfs-test2.c` tests it on the Mac with real cards and VMU files (imports from a .DCI and a .VMS+.VMI byte for byte, every .vms taken for a game, the CRC on real saves, the picture, defragmenting a card filled and thinned out (room for a game 40 to 120 blocks, every save's bytes the same), the old root layout, lost blocks, a damaged save and shared blocks found, repair leaving the saves untouched): 31 of 31. In PPSSPP a scripted session made a card, renamed, checked and repaired, defragmented, duplicated, imported a .DCI and a .VMS with its .VMI, restored a backup, deleted a card and showed a save's Info; the files were then compared byte for byte (the backup before the repair the card as it was, the repaired card's saves unchanged, the restored card the backup chosen, the imported game the .vms).

**Long import lists (16 November 2026).** 127 VMU games and their files (133 in all) were copied to `ms0:/PSP2DC/saves/`. The import list read at most 64 files in the order the Memory Stick gave them and the box drew only 12, so most were unreachable and choices past the twelfth were invisible. Now it reads every file (up to 256), sorted by name, and a list box shows nine at a time around the chosen one with its place at the title's right ("19/128"); L / R move a page of nine; the lines show the name without its extension, the kind (VMS, VMS+VMI, DCI) and the blocks. The same applies to the copy-to and backup lists. Reading the list had looked for each VMS's VMI with up to two file opens (about 250 for this folder), which stalled PPSSPP for seconds; it now reads the folder once and pairs VMIs by name. Checked in PPSSPP with the same 133 files: all 128 importable entries listed, paging to the end and wrapping, ChaoAdventure shown as VMS+VMI, a game imported from the nineteenth entry; host tests unchanged.

**Missed presses, unseen results (16 November 2026).** On hardware presses were sometimes not taken, in the menu and on the VMU (MODE needing several presses), with no pattern; in PPSSPP eight 50–80 ms taps of MODE all registered. The main loop read the pad itself once a turn (`sceCtrlReadBufferPositive(&pad, 1)`), so a tap begun and ended while a turn ran long (a file written, a page drawn) could fall between two reads. Now a thread of its own (`input_thread`, priority 0x12) reads every controller sample and keeps each press until the main loop takes it (`input_take`): a tap already let go counts as held for one turn; the main loop paces itself on the vblank. Whether the PSP's latch (`sceCtrlReadLatch`) collects presses between reads could not be confirmed from a primary source (only from a description of PPSSPP's), so it is not relied on. System → Details gains a line, `loop`: the longest turn in the last five seconds and how many went over 20 ms, to see on hardware whether turns run long. Some of the VMU's MODE presses may also have gone to a VMU game still running: there △ is the game's MODE, which many games take only on some screens (holding it 2 s always leaves). Also, results said only in the header went unseen (Defragment or Check seemed to do nothing): they are now shown in the middle of the screen, one line in a frame, for two seconds (any button hides them).

**A game in the way (16 November 2026).** "Use this card" and New card are no longer greyed while a VMU game runs: changing the card ends the game first, its flash written to its own card (`card_change` → `vmu_game_end`), as taking a VMU out would. Importing a game into a card that has one asks "Replace the game NAME?" (No first): the import is first tried on a copy of the card with its game taken off, and the question is asked only when it then goes in; Yes writes that copy, the card backed up before. Checked in PPSSPP: Chao Adventure 2 running on `games`, card2 put in use from the Cards page (the game ended, `games` backed up); Arrow imported into `games`, No going back to the list at Arrow, Yes replacing CHAO2 with ARROW, the card's two saves byte for byte the same.

**The status at the footer (16 November 2026).** The status beside the tabs was easily missed. It moved to the footer's right end: the link's dot, the card in use in the in-use green and what became of it, after a small dot; a message just given (2 s) is a pill in the accent in its place. It is cut short, or left out, where the keys need the room (the keyboard's line). The header holds only the tabs. The place in a long list ("1-3 of 5") moved from the footer to the page's title line. Checked in PPSSPP on Classic and Paper, standalone and in demo (the orange dot).

**Pauses while a VMU game plays (16 November 2026).** Alien Fighter paused now and then on hardware. The main loop restarted the PSP's USB itself when the cable was in but no adapter synced for 5 s: `pspexpUsbStop`, a 0.5 s wait and `usb_start` (up to three tries, 0.5 s apart on failure), every 5 s for as long as the cable went to something that never syncs, such as the Mac after copying files. The whole loop, the VMU game with it, stood still each time. The restart now runs in a thread of its own (`usb_restart_thread`), and the wait between restarts doubles (5, 10, 20, 40 s, then a minute) until a link comes up. In PPSSPP (no USB there) Alien Fighter played with the longest loop turn 16 ms and none over 20 ms, VMU CPU 0.8 ms a frame; screenshots taken during play (a 390 KB file each) made 100 ms turns, so they must not be taken while measuring.

**Header centred, battery shown (16 November 2026).** The tabs are centred in the header (the menu's L … R, or playing's SELECT menu and the tabs), and the PSP's battery is at its right: an outline filled to the level and the percent (`scePowerGetBatteryLifePercent`, read every two seconds, only when `scePowerIsBatteryExist`), the fill green while charging (`scePowerIsBatteryCharging`) and red under 15 %. Checked in PPSSPP (which reports 100 %) on Classic and Paper, playing and in the menu.

### 5.0e4 Colour VMU Screens (17 November 2026)

After DreamColor Plus's "Modo Colorido" (its matching undocumented), as a first "beyond the VMU" feature. Each screen is 192 bytes; zlib's CRC-32 of them names it, and `ms0:/PSP2DC/colour/XXXXXXXX.png` (48x32, drawn four times its size, or 192x128, drawn as it is; upright, as seen) is drawn in place of the dots in the VMU drawing. It applies to whatever the screen shows: the Dreamcast's, the VMU's own BIOS screens, VMU games. `psp/app/colour.c`:

- At the start the folder is read once, names only, into a sorted list; whether a screen has a picture is a binary search, no file opened per frame.
- A picture is read by a thread of its own (libpng's simplified API, `png_image_*`; libpng and zlib come with pspdev) into a cache of 24 (192x128 each, 96 KB); until it is in, the dots are drawn, so a screen never waits on the Memory Stick. A file that cannot be read (not PNG, another size) is dropped from the list and not tried again.
- **Collect screens** (System → Details, a tool): each screen not yet in `colour/` or `colour/new/` is written as `colour/new/XXXXXXXX.png`, 48x32, black on white, upright; colour it and move it into `colour/`. A screen that changes every second (the VMU's clock) makes a file a second while collecting.
- **Colour screens** (System, a setting kept as `colour=` in settings.txt): on (default) or off, and how many pictures there are.
- The application module grew from 322 KB to 657 KB with libpng and zlib.
- Checked in PPSSPP: screens collected from the standalone VMU (clock, FILE, GAME) came out upright; FILE coloured at 48x32 and GAME at 192x128 (a gradient) were shown in colour, the clock in dots; the setting off brought the dots back. Not yet tried on hardware nor with a Dreamcast's screens.

- **Removed (17 November 2026).** It was of little use: a picture shows only for a screen that matches it dot for dot, so gameplay, where nearly every frame differs, is never coloured, and each screen has to be painted by hand. Reverted with libpng and zlib.

### 5.0e5 A Card per Game: openMenu's Game ID (17 November 2026)

From openMenu's source (the Virtual Folder Bundle, `openMenu/src/openmenu/src/vm2/vm2_api.c` and `main.c`, read 17 November 2026):

- **Detection.** openMenu sends the All Status request (ALLINFO) to each memory-card device and compares 16 characters of the reply's payload at byte 112 (after the 112-byte device info), case-insensitively, with "VM2 by Dreamware", "8BITMODS VMUPro ", "USB RP2040 EMU  " (USB4Maple) and "Pico2Maple USBBT".
- **The Game ID.** Maple command 0x21 (33) to that unit: payload word 0 the memory card function, bytes 4–15 the disc's product number (`item->product`, 12 bytes, NUL-padded), and with a name bytes 16–143 (128 bytes; the frame is then 36 words, else 4). openMenu waits for the OK reply (retrying on AGAIN). It sends "openmenu" at its own start, and the game's ID and name as it starts a disc (not for folders), to the first or every such device as its "VMU Game ID" setting says.
- The reverse-engineered Pico2Maple firmware (Microsap project) takes 0x21 the same way; it is a reconstruction and was only used to confirm the layout.

What PSP2DC does (firmware patch 0010, `psp/app/main.c`):

- The adapter's VMU all-status reply carries "USB RP2040 EMU  " at byte 112 (`PSP2DC_VMU_ID` in vmu.c; joypad-os's RP2040 builds are USB4Maple's kin), so openMenu takes it for a virtual VMU. It answers 0x21 with OK and sends the ID and name to the PSP (`PSP2DC_MSG_GAME_ID`, 140 bytes). Strings keep their byte order, as the card's blocks do.
- The PSP, with **Card per game** on (System, default on): openMenu's own ID is left alone; any other ID names a card (`T-8101N` → `t-8101n`), made empty the first time, and put in as a change of card by hand is (connected: the adapter lets its card go first), tried again for ten seconds while the card is busy. The game's name shows briefly.
- `ms0:/PSP2DC/dev_gameid.txt` (lines "<ms> <ID> [name]") feeds IDs to the same code without openMenu, for testing.
- Checked in PPSSPP with dev_gameid.txt: "openmenu" left alone; T-8101N made and put in `t-8101n`, the same again kept it, MK-51000 made `mk-51000`. Both firmware targets build. **On hardware (17 November 2026)**, with GDEMU and the openMenu Virtual Folder Bundle 1.7.2-ateam, the SD card switched from GDMENU 0.6.0:
- openMenu took the adapter for a virtual VMU: its **VMU Game ID** setting (shown only then) was there, on Send to All.
- Starting a disc made and put in that game's card: `mk5113650`, then `t41405n` for Prince of Persia.
- Prince of Persia wrote its options file to the new card at once, with the tick on the VMU, so the card went in before the game first read it.
- Started again, the same card came back with its file.
- The IDs arrived without hyphens (`T41405N`), as openMenu stores them.

### 5.0e6 Controller and Card Dropping Out: Late Maple Replies (17 November 2026)

**Symptom (hardware).** Prince of Persia: Arabian Nights opened its save-slot screen by itself every so often during play, with a large X on the VMU in place of the usual tick, and once said "A controller has been removed, or a VMU is being recognized". Virtua Tennis 2 paused by itself. F355 Challenge was unaffected. An original controller with a VMU played the same game normally.

**Ruled out on the way.**
- Phantom presses from the PSP: an older PSP build (7e7894c, before the input thread) behaved the same, and the game's screen needs four presses.
- The Game ID change (patch 0010): the previous firmware behaved the same.
- The card: `card3` held only the game's options file (`PRINCELV.OPT`, 2 blocks), correct. No save existed yet, so the empty slots were right.
- A USB drop or the card being taken out: Details showed no detach and no card change when it happened.

**Cause.** With `CONFIG_DC_CORE1_TX` off (upstream's choice for usb2dc), Core 0 answers the Dreamcast after its other work. The firmware printed debug text on the UART every 5 s and on events, and `printf` waits for each character at 115200 baud. A reply held up past the Dreamcast's timeout makes the console see the controller, or its card, go and come back. The new counts showed 68 replies over 1 ms in 148 s, and a wait of 65 ms or more, with no bad packets.

**Fix (patch 0011).** No stdio on the UART in the two PSP2DC targets. On hardware Prince of Persia's screens and Virtua Tennis 2's pauses both stopped.
- Tried first: replies from Core 1 (`CONFIG_DC_CORE1_TX`). The controller dropped out for good, as upstream reports for the KB2040, so it was reverted.

**Counts on the PSP.** The status message gains the Maple counts (`Psp2dcStatus` grows from 18 to 36 bytes; the PSP takes either size):
- packets that failed their check;
- packets left unanswered, the adapter's own replies heard back excluded, and the last one's command, address, length and function;
- replies that waited on a busy transmitter;
- replies begun 1 ms or more late, and the longest wait in the last second;
- times the adapter took the card away.

Details shows them on the `maple` lines.

### 5.0e7 The Dreamcast Keyboard (18 November 2026, protocol 6)

Typing on the PSP, for Typing of the Dead, Phantasy Star Online's chat and Quake III.

- **Source:** Sega's "Maple Bus 1.0 Function Type Specifications FT6: Keyboard", rev. 0.90 (segaretro.org, `Ft6_090e.pdf`).
  - Function type 40h.
  - The function definition block is 4 bytes: language KL (02h US), type KT (05h, 104 keys), LED type LD, and LDON (80h: the keyboard keeps its own LEDs).
  - Get Condition answers with 8 bytes: the modifier bits, the LEDs, then six key codes, left-aligned. The codes are USB's (a = 04h). Seven keys at once is a rollover error: 01h in all six.
  - Set Condition sets the LEDs and is answered with a reply.
  - flycast's emulated keyboard sends the same function definition bytes (02 05 00 80 for US).
- **The setting** (System → Keyboard, on/off): on, the adapter is a keyboard alone, the device Sega made, with no pad and no VMU. A change leaves the device unanswered for a second, so the Dreamcast sees it unplugged and reads it again.
- **Pad and keyboard in one device: tried, dropped.** It announced both functions (41h), which Sega's spec allows; the blocks went from the highest function bit down, as on a VMU, and the VMU stayed in.
  - Typing of the Dead never read its keys (the keyboard's Get Condition count stayed still), though it took the keyboard alone at once (18 November 2026).
  - Likely reason: Sega's library seems to file a device under one function (its light gun is a gun and a controller together), here the controller.
  - Not tried, as the odds looked low: other ways of describing the combined device. The alternative that would work is a second Maple cable to port B for the keyboard, as with a real keyboard.
- **PSP:** with the keyboard on, playing is typing: SELECT switches between it and the menu (`keyboard.c`, `ui_page_keys`).
  - Choosing keys: the d-pad chooses a key (held, it repeats) and X presses it for as long as X is held.
  - A modifier chosen on the keyboard is latched for the next key.
  - Buttons: □ Backspace, △ Space, START Enter, ○ Esc, L Shift held, the nub the arrow keys.
  - The PSP sends the 8 bytes when they change and every half second.
  - The adapter lets go of the keys when the PSP goes quiet or away. It keeps the mode, so the Dreamcast is not shown another device.
- **Telling whether a game reads it:** the adapter counts the keyboard's Get Conditions (status, protocol 6). The keyboard page says "the Dreamcast is reading it" while the count rises. Details shows the count and the LEDs the Dreamcast set.
- **Tested:**
  - PPSSPP demo: the page and the buttons.
  - Hardware (18 November 2026, RP2040-Zero): openMenu and Typing of the Dead take the keys.

### 5.0f Which Copy of a Card Counts (protocol 5)

The adapter keeps the card in its RAM for as long as the Dreamcast is on, also while the PSP is unplugged, so a save made meanwhile is not lost. Protocol 4 sent that copy to the PSP at the next connection, and the PSP took it as the card it had in use.

**What went wrong (15 November 2026).** Connected with `games.bin` in use, the PSP was unplugged, changed to `card1` standalone and plugged in again: the adapter's copy of `games` went into `card1` and was saved there; the same again with `card2`. Both lost their saves and became byte-for-byte the games card (a copy of the Memory Stick taken before anything else was changed confirmed it). `card1` was restored from a copy taken earlier that day; `card2`'s save had no copy. The adapter never knew which card it held, and the PSP never asked.

**Now** (`firmware/patches/0008`, `psp/app/card_sync.c`, `main.c` `card_decide`):

- The PSP names the card it sends (`CARD_DONE`); on a new connection the adapter answers with the card's name and CRC-32 (`CARD_STATE`) and sends no block until the PSP asks (`CARD_SEND`) or takes the card out (`CARD_EJECT`, with `DISCARD` when the PSP needs none of it).
- The PSP keeps a **base**: the name and CRC-32 of the last copy both had (after sending a card; each time it writes the file while the adapter holds that card). It is kept in the settings (`base=`).
- On reconnecting it compares the adapter's copy, its own copy of that card (the card in use, or that card's file) and the base:

| | What happens |
|---|---|
| The copies are the same | Nothing comes. If the PSP has another card in use, that one goes in. |
| Only the adapter's changed (the Dreamcast saved meanwhile) | It comes into its own card's file; then the PSP's card in use goes in. |
| Only the PSP's changed (played or changed standalone) | The PSP's card in use goes in. |
| Both changed, or not known | It comes into a new card, `NAME-dc`, and nothing is written over; then the PSP's card in use goes in. |

- Blocks from the adapter are taken only once asked for: an adapter still on protocol 4 sends its card unasked, and the PSP now drops it (and says "adapter protocol 4, PSP 5: update it").
- A card upload cut short no longer ends with "no file", which made the adapter send its blank card over the PSP's.
- A VMU game left running stops before the card in use changes or the Dreamcast takes it, and its flash only ever goes back to its own card.
- `scripts/dev/card-sync-test.c` checks the choice in all these cases on the Mac (with real card images), the incident first: 12 of 12 as intended.
- **On hardware (15 November 2026):** after flashing, the PSP's card went in ("sent to the adapter"); changing to `card1` while connected showed its two saves on the Dreamcast; and the incident repeated (the Dreamcast left on with `card1` in, the PSP unplugged, another card chosen on its own, plugged in again) ended with the chosen card in ("sent to the adapter") and `card1` keeping both saves. A Dreamcast save made while the PSP is unplugged (the "only the adapter's changed" case) cannot be tried here: the PSP is the only controller.

**The format.** The adapter's preformat (from upstream joypad-os, "exactly like MaplePad's CheckFormatted()"), and `cards_format` after it, wrote the root block's file system fields from 0x44 instead of 0x40, so the FAT's location sat at 0x4a instead of 0x46, and ended every system block's FAT chain. mc.pp.se's flashmem.html, dmitry.gr's "VMU Hacking" and libevmu's `EvmuRootBlock` agree on 0x46 for the FAT and 0x4a for the directory, as does a card image formatted by a Dreamcast (DreamPicoPort's `formatted_storage.bin`, stamped 1999-09-09, the Dreamcast's default date): from 0x40, last block 255, partition 0, root 255, FAT 254 and its size 1, directory 253 and its size 13, icon 0, save area 200 and 31, game area 0 and 128; the directory chained 253 down to 241. Both now write that; a new card's system area matches the Dreamcast's but for the date and the optional custom colour. The Dreamcast never noticed (it learns the layout from the Maple media info), but the VMU's own BIOS, emulators and tools read the root block. Cards made before keep their old root block; PSP2DC reads either.

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
- Protocol 5: a card is never written over another on reconnecting (the 15 November incident repeated on hardware without loss); FILE mode copies and deletes on hardware.
- The standalone VMU: TIME, FILE (copy, delete) and GAME mode, VMU games run by `vmu_core` (from libevmu) with the buzzer; Chao Adventure 2 and Alien Fighter played on hardware (5.0c, 5.0d).
- Several cards on the Memory Stick; the Saves and Cards pages: info, copy, move, export, import (127 games in a scrolling list), backups, restore, defragment, check and repair (5.0e–5.0e3).
- The interface (16 November 2026): SELECT switches between playing and the menu; one header and footer on every page, the status at the footer's right, the battery in the header; greyed choices; results in the middle of the screen; the buttons read by a thread of their own (5.0a, 5.0f's neighbours).
- Changing card while connected, through the adapter: works on hardware (17 November 2026, on earlier builds).
- A card per game through openMenu's Game ID (5.0e5): works on hardware with GDEMU and openMenu (17 November 2026).
- The Dreamcast no longer loses the controller or its card now and then (5.0e6, patch 0011).
- The Waveshare RP2040-Zero as the adapter (3.5): works on hardware, with the Dreamcast cable soldered straight to the board (18 November 2026). The SuperPico's breadboard wiring (section 8, Phase 3) is no longer needed.
- The build needs only pspdev: PSPExp's USB driver is in `psp/usb`; `scripts/install-psp.sh` installs without macOS's "._" files.

Not yet completed:

- Real-game compatibility and latency measurements.

## 11. First Major Milestone

The first major milestone is:

**A Sony PSP-1000 connected to a real Sega Dreamcast through the SuperPico, controlling a game while displaying that game's live VMU graphics on its own screen.**

This will establish the core functionality of the project.

The longer-term vision is to transform the PSP into an enhanced Dreamcast controller and second-screen companion, combining the original Dreamcast experience with the PSP's display, controls, storage, and programmable capabilities.
