# PSP2DC Companion

**Extending the Dreamcast with a Programmable Second Screen**

| | |
|---|---|
| Document type | Concept and Architecture Proposal |
| Status | Draft v0.1: not implemented |
| Target platforms | Sega Dreamcast, KallistiOS, Sony PSP, RP2040 |
| Project | PSP2DC |

---

## 1. Executive Summary

PSP2DC Companion is a proposed extension to PSP2DC that allows Sega Dreamcast games to use a connected Sony PSP as a programmable, full-color second screen and extended storage device.

The project builds upon PSP2DC's existing ability to transform a PSP into a Dreamcast controller, Visual Memory Unit (VMU), keyboard, and virtual memory card manager through an RP2040-based Maple adapter.

While the original Dreamcast VMU provides a monochrome 48 × 32 display and approximately 100 KB of usable save storage, the PSP offers a 480 × 272 color display, considerably more processing power, and removable Memory Stick storage.

PSP2DC Companion aims to make these capabilities directly accessible to Dreamcast homebrew developers through a dedicated KallistiOS library and an extended Maple communication protocol.

Rather than transmitting a continuous video stream, games will send compact graphics commands, UI updates, events, and data. The PSP will render and manage the resulting interface locally.

The long-term objective is to establish an open, extensible second-screen platform for Dreamcast homebrew while maintaining compatibility with existing Dreamcast games and peripherals.

## 2. Background

### 2.1 The Original Dreamcast VMU

The Sega Dreamcast introduced the Visual Memory Unit as a memory card with a small LCD, controls, and standalone functionality.

Games could use the VMU to display information independently of the television, including animations, status indicators, and gameplay-related content.

However, the original hardware imposed significant limitations:

- 48 × 32 monochrome LCD.
- Limited memory card capacity.
- Restricted graphical capabilities.
- Small physical display.
- Limited interaction possibilities.

Despite these constraints, the VMU demonstrated the potential of a secondary display integrated into a console's controller ecosystem.

### 2.2 The Existing PSP2DC Project

PSP2DC already provides a working connection between a Sony PSP and a real Dreamcast through an RP2040 adapter.

Its existing capabilities include:

- Dreamcast controller emulation using PSP controls.
- Live VMU display on the PSP.
- Color themes and enhanced VMU presentation.
- Virtual memory cards stored on the PSP Memory Stick.
- Multiple memory cards, backups, and save management.
- Automatic per-game VMU selection through openMenu Game IDs.
- Standalone VMU emulation and minigames.
- Dreamcast keyboard emulation.

Communication between the PSP and RP2040 uses a custom bidirectional USB protocol rather than USB HID.

The RP2040 acts as a USB host and translates between the PSP and the Dreamcast Maple bus.

This architecture provides a foundation for extending the PSP's role beyond emulating existing Dreamcast peripherals.

## 3. Vision and Objectives

The central idea is to evolve the PSP from a replacement VMU into an intelligent, programmable Dreamcast companion device.

A Dreamcast game should be able to detect a connected PSP2DC device and use its display, storage, and input capabilities through a simple programming interface.

Potential applications include:

- Live maps and navigation.
- Character statistics and status indicators.
- Inventory management.
- Quest journals and objectives.
- Racing telemetry.
- Strategy game minimaps.
- Game-specific menus.
- Secondary player information.
- Extended save storage.
- Interactive companion applications.

The companion interface should operate alongside standard Dreamcast controller and VMU functionality.

### 3.1 Primary Objectives

1. Expose PSP2DC companion functionality through KallistiOS.
2. Enable bidirectional communication between Dreamcast applications and the PSP.
3. Support rich graphical interfaces without requiring continuous framebuffer transmission.
4. Allow developers to use the PSP Memory Stick for extended game storage.
5. Preserve normal controller and VMU compatibility.
6. Provide an accessible API for Dreamcast homebrew developers.
7. Establish an open protocol that can evolve independently of the PSP2DC application.

### 3.2 Non-Goals

The initial implementation will not attempt to:

- Stream the Dreamcast's video output to the PSP.
- Mirror the Dreamcast framebuffer.
- Replace the standard Maple controller protocol.
- Modify commercial Dreamcast games automatically.
- Provide arbitrary access to Dreamcast memory.
- Require companion support for normal gameplay.

Existing games will continue to benefit from PSP2DC's standard controller and VMU functionality. The new companion features will require explicit integration by game developers.

## 4. Proposed Architecture

The system will consist of four principal components.

### 4.1 Dreamcast Application

A KallistiOS-based game uses the PSP2DC Companion API to send graphics, text, game state, and storage requests.

The application does not need to understand USB communication or the internal implementation of the RP2040 adapter.

### 4.2 KallistiOS Companion Library

A proposed library, `libpsp2dc`, provides:

- Device detection.
- Capability negotiation.
- Display and UI commands.
- Input events.
- Extended storage access.
- Asynchronous communication.
- Error handling.

The library communicates with a PSP2DC-specific Maple function exposed by the adapter.

### 4.3 RP2040 Maple-to-USB Bridge

The RP2040 adapter continues to emulate standard Dreamcast peripherals while adding support for the companion protocol.

Its responsibilities include:

- Maple function advertisement.
- Companion command handling.
- Packet forwarding.
- Message fragmentation and reassembly.
- Flow control.
- Prioritization of critical controller and VMU traffic.

The existing PSP2DC USB protocol will be extended to carry companion messages.

### 4.4 PSP2DC Application

The PSP application receives companion commands and provides:

- Local graphics rendering.
- UI layout and compositing.
- Cached assets.
- Interactive controls.
- Memory Stick storage.
- Device status and system overlays.

The PSP maintains the current companion interface locally, allowing it to update individual elements without retransmitting the entire display.

### 4.5 Communication Flow

```
+---------------------------------------+
|          Dreamcast Homebrew           |
|                                       |
|          KallistiOS + libpsp2dc       |
+---------------------------------------+
                    |
                    | Companion API
                    v
+---------------------------------------+
|             Maple Protocol            |
|                                       |
| Controller / VMU / Companion Function |
+---------------------------------------+
                    |
                    v
+---------------------------------------+
|              RP2040-Zero              |
|                                       |
|       Maple Peripheral Emulator       |
|          USB Host / Bridge            |
+---------------------------------------+
                    |
                    | Bidirectional USB
                    v
+---------------------------------------+
|               Sony PSP                |
|                                       |
|            PSP2DC Runtime             |
|                                       |
|  Rendering | UI | Assets | Storage    |
+---------------------------------------+
```

## 5. Companion Display Model

### 5.1 Display Partitioning

The PSP has a native resolution of 480 × 272 pixels.

Rather than treating the entire display as a single framebuffer, PSP2DC Companion will support independently managed display regions.

An initial recommended layout is:

| Region | Dimensions | Purpose |
|---|---|---|
| Game viewport | 240 × 240 | Dynamic graphical content |
| Information area | Approximately 240 × 272 | Status, statistics, inventory, text |
| System overlay | Configurable | PSP2DC controls and notifications |

The game viewport could display a minimap, animation, radar, or other dynamic graphical content.

The information area would contain structured UI elements that change relatively infrequently.

The PSP2DC system overlay would remain under the control of the PSP application.

These dimensions are proposed defaults, not fixed protocol limitations.

### 5.2 Independent Region Updates

Each display region will support independent updates.

For example:

- A minimap updates several times per second.
- Health changes only when the player takes damage.
- Inventory updates when items are added or removed.
- Quest text changes when an objective is completed.
- Connection and VMU status are managed locally.

This approach reduces communication overhead and allows each region to use an appropriate update frequency.

### 5.3 Local Rendering

The PSP will be responsible for rendering text, shapes, widgets, sprites, and cached graphics.

Instead of transmitting a complete image whenever a value changes, the Dreamcast sends a compact update.

For example:

```c
psp2dc_widget_set_u32(WIDGET_HEALTH, 84);
```

The PSP updates the corresponding health indicator using its local rendering system.

This separation makes the interface more efficient and simplifies game-side integration.

## 6. Graphics and Rendering Protocol

The proposed protocol will support several rendering mechanisms.

### 6.1 Structured UI Commands

These are intended for common interface elements:

- Text labels.
- Numeric values.
- Progress bars.
- Status indicators.
- Lists.
- Inventory grids.
- Icons.
- Notifications.

The PSP renders the elements according to the active layout and theme.

### 6.2 Drawing Commands

Games may issue basic drawing operations:

- Draw rectangle.
- Draw line.
- Draw text.
- Draw sprite.
- Clear region.
- Set palette.
- Set clipping rectangle.
- Present changes.

These commands allow custom interfaces without requiring developers to generate raw framebuffers.

### 6.3 Indexed-Color Graphics

The dynamic game viewport may support indexed-color bitmap data.

Potential formats include:

- 1-bit monochrome.
- 4-bit indexed color.
- 8-bit indexed color.
- RGB565 for selected operations.

Indexed-color formats reduce bandwidth and are suitable for maps, pixel art, and many game-specific interfaces.

### 6.4 Partial Framebuffer Updates

For applications requiring direct pixel manipulation, the protocol will support dirty-rectangle updates.

Only modified portions of a region will be transmitted.

For example, updating a 32 × 32 region in 8-bit indexed color requires 1,024 bytes of pixel data, excluding protocol overhead.

This is substantially smaller than transmitting the entire 240 × 240 viewport.

### 6.5 Asset Caching

Graphics assets may be stored on the PSP Memory Stick.

Examples include:

- Background images.
- Maps.
- Sprites.
- Icons.
- Fonts.
- Interface themes.

A game can reference these assets using identifiers rather than retransmitting them.

Asset packages may be installed separately or transferred through the companion protocol.

The protocol should include asset-version and availability checks so games can detect missing or incompatible assets.

## 7. Bandwidth Strategy

Bandwidth efficiency is a fundamental design requirement.

A complete 480 × 272 RGB565 framebuffer requires 261,120 bytes.

A 240 × 240 RGB565 viewport requires 115,200 bytes.

Even the reduced viewport would be expensive to transmit continuously over the proposed Maple communication path.

Therefore, the protocol will prioritize:

1. Structured UI updates.
2. Cached asset references.
3. Sprite positioning commands.
4. Partial framebuffer updates.
5. Indexed-color graphics.
6. Full-frame transfers only when necessary.

The actual achievable throughput must be measured on the RP2040-Zero hardware.

### 7.1 Traffic Prioritization

Communication should follow this priority order:

1. Time-critical controller responses.
2. VMU memory reads and writes.
3. Companion input events and control messages.
4. Small UI and game-state updates.
5. Graphics transfers.
6. Background asset transfers.

Large transfers must be fragmented and rate-limited.

Companion operations should be asynchronous wherever possible to avoid blocking the Dreamcast game loop.

## 8. KallistiOS Integration

### 8.1 Proposed Library

A dedicated library, tentatively named `libpsp2dc`, will provide a high-level interface to companion functionality.

The initial implementation may be distributed independently of KallistiOS.

Once the protocol and API have stabilized, integration into the upstream KallistiOS project can be considered.

### 8.2 Device Detection

Games should detect whether a compatible PSP2DC companion is connected.

The detection process should include:

- Device identification.
- Protocol version.
- Supported capabilities.
- Display resolution.
- Supported graphics formats.
- Storage availability.
- Input capabilities.

Applications must remain functional when the companion device is absent.

### 8.3 Proposed API

The following API is illustrative and subject to design changes.

```c
#include <psp2dc/psp2dc.h>

int main(void) {
    if (psp2dc_init() == 0) {
        psp2dc_set_title("My Dreamcast Game");
        psp2dc_text(10, 10, "Health: 85/100");
        psp2dc_progress(10, 35, 85, 100);
        psp2dc_present();
    }
    // Continue normal game execution.
}
```

A more complete API could provide:

```c
psp2dc_detect();
psp2dc_get_caps();
psp2dc_region_create();
psp2dc_region_clear();
psp2dc_draw_text();
psp2dc_draw_rect();
psp2dc_draw_sprite();
psp2dc_draw_bitmap();
psp2dc_widget_create();
psp2dc_widget_set_u32();
psp2dc_widget_set_text();
psp2dc_present();
psp2dc_poll_event();
psp2dc_storage_read();
psp2dc_storage_write();
```

The API should favor nonblocking operations, explicit status reporting, and predictable resource usage.

## 9. Maple Protocol Extension

The preferred approach is to introduce a PSP2DC-specific companion function through the Maple peripheral interface.

The adapter would advertise the function alongside supported standard peripheral functionality.

The design must establish:

- A suitable function identifier.
- Device enumeration behavior.
- Command and response formats.
- Maximum payload sizes.
- Fragmentation and reassembly.
- Protocol version negotiation.
- Error detection and recovery.
- Compatibility with existing Maple devices.

The exact mechanism requires validation against the Maple protocol and KallistiOS implementation.

Normal VMU block addresses and filesystem operations should not be repurposed for companion traffic.

### 9.1 Bidirectional Communication

The protocol must support both directions.

Dreamcast to PSP:

- UI commands.
- Graphics updates.
- Game-state values.
- Asset requests.
- Storage operations.
- Notifications.

PSP to Dreamcast:

- User interaction events.
- Command acknowledgements.
- Storage results.
- Device status.
- Capability information.

This would allow interactive companion applications rather than passive displays.

## 10. Extended Storage

The PSP Memory Stick can provide storage beyond the limits of standard Dreamcast VMUs.

PSP2DC Companion will distinguish between two storage systems.

### 10.1 Standard Virtual VMU Storage

Existing Dreamcast games continue to access standard VMU images.

PSP2DC manages these images through its current memory card system.

Compatibility with commercial Dreamcast software remains a priority.

### 10.2 Companion Extended Storage

Companion-aware homebrew games may use additional Memory Stick capacity through a dedicated API.

Potential applications include:

- Larger save files.
- User-generated levels.
- Custom maps.
- Replay recordings.
- Game configuration.
- Downloadable content.
- Companion graphics assets.

A proposed storage API could resemble:

```c
psp2dc_storage_write(
    "levels/map01.dat",
    data,
    size
);
```

Each game should receive an isolated storage namespace.

The implementation should provide:

- Per-game directories.
- File-size and quota controls.
- Read/write error handling.
- Atomic writes where practical.
- Data integrity checks.
- Protection against path traversal.
- Clear behavior when the PSP is disconnected.

Extended storage is not intended to transparently enlarge the standard VMU filesystem. It is a separate capability available to explicitly compatible games.

## 11. Companion Input

The PSP's controls can also be used to interact with the companion interface.

Examples include:

- Navigating an inventory.
- Selecting map markers.
- Scrolling a quest journal.
- Choosing items.
- Confirming secondary actions.

The protocol should distinguish companion input from standard Dreamcast controller input.

This avoids unintended gameplay actions while navigating PSP-specific interfaces.

Developers should be able to define which buttons are assigned to companion interaction and how input focus is handled.

## 12. Developer Experience

PSP2DC Companion should be straightforward for homebrew developers to adopt.

The proposed developer package will include:

- KallistiOS library.
- Public protocol specification.
- API reference.
- Example applications.
- Sample companion layouts.
- Asset conversion tools.
- Debugging utilities.
- Reference firmware implementation.

A small example should demonstrate how to detect the device, display a message, update a value, and receive an input event.

More advanced examples could demonstrate maps, sprite rendering, and extended storage.

The protocol should remain open so that other devices or emulators could potentially implement compatible companion functionality.

## 13. Reference Demonstration

The initial demonstration should be a simple KallistiOS game featuring a live companion dashboard.

The Dreamcast television displays the main gameplay.

The PSP displays:

- A 240 × 240 live minimap.
- Player position.
- Health and status.
- Current objective.
- Inventory information.

The player can navigate the companion interface without interrupting gameplay.

The demonstration should verify:

1. Successful device detection.
2. Bidirectional command exchange.
3. Independent display region updates.
4. Acceptable communication latency.
5. Reliable controller and VMU operation.
6. Graceful behavior when the PSP is disconnected.
7. Extended storage read/write operations.

The reference application should also measure Maple throughput, message latency, and the impact of companion traffic on controller responsiveness.

## 14. Development Roadmap

### Phase 1: Protocol Proof of Concept

- Define the companion device identity.
- Implement basic Maple command exchange.
- Extend RP2040 USB forwarding.
- Display text on the PSP.
- Return a simple PSP input event.

**Milestone:** A KallistiOS application sends a message to the PSP and receives a response.

### Phase 2: Structured Display

- Implement display regions.
- Add text and drawing commands.
- Introduce basic UI widgets.
- Support independent updates.
- Add capability negotiation.

**Milestone:** A Dreamcast game displays a live dashboard on the PSP.

### Phase 3: Graphics and Assets

- Add indexed-color graphics.
- Implement partial framebuffer updates.
- Support cached sprites and backgrounds.
- Develop asset conversion tools.
- Measure and optimize throughput.

**Milestone:** A game displays a live graphical minimap with acceptable performance.

### Phase 4: Extended Storage

- Implement file operations.
- Add per-game storage namespaces.
- Support asynchronous transfers.
- Implement integrity and recovery mechanisms.

**Milestone:** A KallistiOS game stores and retrieves data from the PSP Memory Stick.

### Phase 5: Developer SDK

- Stabilize the API.
- Publish documentation.
- Provide sample applications.
- Develop a reference game.
- Gather feedback from Dreamcast homebrew developers.

**Milestone:** External developers can integrate PSP2DC Companion into their games.

### Phase 6: KallistiOS Integration

- Review the Maple driver design.
- Evaluate upstream integration.
- Refine the public API.
- Establish compatibility and versioning policies.

**Milestone:** PSP2DC Companion becomes a reusable, documented extension for Dreamcast homebrew development.

## 15. Technical Challenges

Several areas require investigation before implementation.

**Maple throughput.** Actual usable bandwidth and command latency must be measured under realistic gameplay conditions.

**Peripheral enumeration.** The proposed companion function must coexist with standard controller and VMU functionality without confusing Dreamcast software.

**RP2040 resources.** Firmware memory, buffering, USB scheduling, and PIO timing must be evaluated.

**PSP rendering performance.** The PSP must process companion updates while maintaining controller input, VMU emulation, and other PSP2DC features.

**Connection reliability.** The system must handle temporary disconnections, incomplete transfers, timeouts, and device resets.

**Compatibility.** Companion functionality must not compromise the operation of existing Dreamcast games or standard Maple peripherals.

## 16. Long-Term Possibilities

Once the basic companion protocol is established, additional capabilities become possible:

- Game-specific UI themes.
- Interactive maps and inventories.
- Companion minigames.
- Game-specific save inspectors.
- Replay and telemetry visualization.
- Shared companion asset libraries.
- More sophisticated second-screen applications.
- Support for additional companion devices.
- Emulator integration for development and testing.

The protocol could eventually become independent of the PSP hardware, allowing other clients to implement compatible displays and storage services.

## 17. Conclusion

PSP2DC has already demonstrated that a Sony PSP can function as a sophisticated Dreamcast peripheral through an RP2040-based Maple adapter.

PSP2DC Companion proposes the next step: allowing Dreamcast homebrew developers to use the PSP as a programmable second screen and extended storage device.

By combining a KallistiOS library, an extensible Maple protocol, efficient graphics commands, and PSP-side rendering, the system could provide functionality far beyond the original Dreamcast VMU.

The design deliberately avoids continuous framebuffer streaming, favoring independently updated display regions, structured UI elements, and cached graphical assets.

The initial objective is modest but important:

> Enable a Dreamcast homebrew application to detect PSP2DC, display a rich interactive dashboard on the PSP, exchange input events, and access extended storage while preserving normal controller and VMU operation.

Achieving this would establish the foundation for an open second-screen ecosystem for Dreamcast homebrew.

## 18. Open Design Decisions

**Widgets first, or raw graphics first.** Whether the companion protocol exposes high-level UI widgets as its primary interface, with raw graphics as an optional capability. Widgets first reduces bandwidth, simplifies development, and leaves room for devices beyond the PSP. To be settled before implementation.

---

## Appendix A. Constraints Checked So Far (20 November 2026)

Facts that bear on sections 7, 9 and 15, from the KallistiOS source (master, 15 November 2026), Marcus Comstedt's Maple pages, openMenu's source, and PSP2DC's own firmware and hardware tests. "Derived" marks a figure worked out from the sources, not measured.

### A.1 Where the companion function can live

- **Not as an extra function bit on the controller.** KallistiOS binds one driver to a device ("For now we just pick the first matching driver", `maple_driver.c`), so a controller that also advertised a companion function would get the controller driver only. Commercial games are a second risk: with the pad and the keyboard advertised as one device, Typing of the Dead never read the keyboard (docs/psp2dc.md, 5.0e7).
- **Custom commands to a unit that already exists need no driver and no new function.** openMenu sends command 0x21 (the Game ID) to a memory card with `MAPLE_FUNC_MEMCARD`, using the unit's own frame (`vm2_api.c`); it picks its targets by the name in the unit's all-status reply. PSP2DC's adapter already takes that command during normal play with no effect on games (5.0e5). This is the lowest-risk start: a few new command numbers, clear of Sega's (1 to 14), of 0x21, and of the NAOMI ones (0x80 to 0x87).
- **A sub-unit of its own with an unassigned function code** is the cleaner end state. KallistiOS leaves several codes unassigned (for example 0x00100000 to 0x00800000 in its byte order); no convention for vendor codes was found. Two costs: a driver must be registered before `maple_init`, which runs before `main()`, and a sub-unit whose function has no driver is probed once and then ignored until replugged. How commercial games treat an unknown sub-unit is untested.

### A.2 Throughput, Dreamcast to PSP

- A Maple frame carries at most 255 words (1020 bytes) of payload; KallistiOS's frame buffers are sized for that.
- KallistiOS sends queued frames once per vertical blank. A unit has one frame of its own, so a driver using it sends one command per vblank: about 60 commands and about 61 KB a second at most (derived). More than one frame to a unit per vblank is possible in the queue (an application can own extra frames) but is not verified on hardware.
- At that ceiling a full 240 × 240 viewport takes about 1 s in 8-bit colour and about 2 s in RGB565; a 32 × 32 tile in 8-bit colour fits one frame. Live content therefore has to be commands and cached assets, as section 7 proposes; bitmaps suit tiles and one-off pictures.
- From the adapter to the PSP the USB link is half duplex: the adapter sends when a message from the PSP arrives, which is once per PSP frame today (about 60 transfers a second; a card block is 536 bytes in one transfer). The pacing and the transfer size can be raised; what the link then carries is to be measured.

### A.3 Replies and the PSP to Dreamcast direction

- Maple is polled: a peripheral speaks only in reply to a command. Input events, storage results and acknowledgements reach the game only when it asks, so the library needs a poll command (once per vblank, or riding on every reply).
- The Dreamcast waits about 1 ms for a reply (KallistiOS sets the timeout register to 50000 units of 20 ns; derived). Late replies made games see the controller leave and return (5.0e6). A round trip to the PSP takes a frame or more, so the adapter must answer every companion command at once from its own buffers and forward afterwards. Anything that needs the PSP's answer (a storage read, a capability query) is a request now and a result on a later poll.
- Back-pressure exists already: a peripheral may answer "again" (0xFC), and KallistiOS then resends the frame at the next vblank without the driver seeing it, with no retry limit. The adapter can use it while its buffer is full, and must answer an error instead when the PSP is not there, or the frame is resent forever.

### A.4 What this suggests

- **Section 18:** the limits favour widgets and drawing commands as the primary interface, with bitmaps as an optional capability.
- **Section 9:** start with custom commands to an existing unit (A.1); consider a function code of its own with phase 6.
- **Roadmap:** put a measurement step before phase 2: a KallistiOS test program that sends commands of 4 to 1020 bytes and counts what arrives on the PSP, with a game's normal controller and VMU traffic running. It settles the frame budget every later phase depends on.
- **Testing:** no emulator has this adapter, so each test runs on a Dreamcast (a disc image on the GDEMU card). KallistiOS's toolchain is not set up for this project yet.

### A.5 Sources

- KallistiOS: `kernel/arch/dreamcast/include/dc/maple.h`, `hardware/maple/maple_irq.c`, `maple_queue.c`, `maple_driver.c` (github.com/KallistiOS/KallistiOS).
- Marcus Comstedt, Maple bus: mc.pp.se/dc/maplebus.html, mc.pp.se/dc/maplewire.html.
- openMenu Virtual Folder Bundle, `vm2_api.c` (github.com/DerekPascarella/openMenu-Virtual-Folder-Bundle).
- The timeout's unit: the Linux Maple driver's comment, `drivers/sh/maple/maple.c`.
- PSP2DC: docs/psp2dc.md, sections 5.0e5 to 5.0e7.

---

## Appendix B. Phase 1 Design (20 November 2026)

The proof of concept and the measurement step in one: a KallistiOS program finds the companion, sends text and test data to the PSP, and gets PSP button events back, while the controller and the VMU work as before.

### B.1 The Maple side

- **One new Maple command, 0x50**, sent to the memory card unit with the memory card's function code in word 0, as openMenu's 0x21 is (A.1). The library uses a frame of its own, so it does not wait on the VMU driver's.
- **Request**, after the function word: `op` (1 byte), `seq` (1), `len` (2, little-endian), then `len` bytes of data, padded to a word. `op` 0 HELLO, 1 SEND (data for the PSP), 2 POLL (nothing to send).
- **Reply**, a data transfer (8), after the function word: `status` (0 fine, 1 no PSP), `session` (changes each time the PSP's link opens, so both sides know to start over), `seq` echoed, a spare byte, `room` (2: bytes free in the adapter's buffer towards the PSP), `len` (2), then `len` bytes from the PSP. Every reply carries what the PSP has sent since the last one, so any command is also a poll. HELLO's reply data is `PSP2DC`, the protocol version and a spare byte.
- **Full buffer:** the adapter answers "again" (0xFC) and KallistiOS resends at the next vblank (A.3). **No PSP:** status 1, the data dropped.
- **A lost reply:** the library sends the command again with the same `seq`, and the adapter takes a SEND's data once per `seq`.
- The adapter answers from two byte queues in its RAM, one each way, and never waits for the PSP.

### B.2 The USB side (protocol 7)

- `COMP_DATA` (adapter to PSP) and `COMP_EVENT` (PSP to adapter): each a piece of the byte stream in that direction, up to 255 bytes a message, several messages a transfer.
- The stream is a run of records: `type` (1), `len` (2), data. A record is at most 1000 bytes, so one fits a Maple frame. A record from the PSP is at most 56 bytes and goes whole in one message.
- `COMP_RESET` (adapter to PSP) comes before a session's first bytes. A session begins when the PSP's link opens, when the PSP is back after a silence, and when a transfer carrying companion bytes failed; what was on its way either way is dropped, and the Dreamcast program sees the new session number in the next reply.
- The adapter's transfers to the PSP grow from 576 to 1280 bytes at most: the companion's bytes ride after whatever else a transfer carries (a VMU screen, a status), so a game drawing the VMU every frame does not hold them back.

### B.3 Records in phase 1

| Direction | Record | Content |
|---|---|---|
| Dreamcast to PSP | CLEAR | none |
| Dreamcast to PSP | PRINT | a row, then text for that row |
| Dreamcast to PSP | TEST | a running number, then a known pattern (the PSP checks both and counts) |
| Dreamcast to PSP | ECHO | any bytes: the PSP sends them back |
| PSP to Dreamcast | BUTTONS | the PSP's buttons, when they change |
| PSP to Dreamcast | ECHO | the bytes of an ECHO |

### B.4 The three programs

- **Adapter:** the command above in the VMU's command handler, and the two queues, in one new file; the USB driver forwards them.
- **PSP:** while companion data arrives, the VMU page gives way to a companion page: the printed rows, and counts (bytes and records a second, errors found in TEST). The buttons still go to the Dreamcast as the controller's; focus is a later phase (section 11).
- **Dreamcast (`dreamcast/`):** `libpsp2dc` with detect, send, poll; and a test program that shows on the TV what it finds, then sends TEST records of 4, 64, 256 and 1000 bytes for a few seconds each, one command a vblank, and reports commands and bytes a second, the ECHO round trip in frames, the PSP's button events, and the controller's state throughout.

### B.5 What phase 1 answers

- Whether KallistiOS and the adapter exchange the new command with no effect on the controller and the VMU.
- The real throughput each way, and where it is lost (Maple, the adapter's queue, or USB: today the adapter sends the PSP one transfer of up to 576 bytes per PSP frame, about half the Maple ceiling).
- The round-trip time of a small message.

### B.6 As built (20 November 2026)

Built as designed, with what B.1 and B.2 now say. Not yet tried on a Dreamcast.

- **Adapter** (joypad-os patch 0014): the command and the two 4 KB queues are in the VMU's file (`vmu.c`), which has the unit's address and the RAM-resident copy routines the Maple path needs; the USB side is in `psp2dc_host.c`. Both targets build.
- **PSP:** `psp/app/companion.c` reads the stream; the page shows while bytes arrived in the last three seconds. System, Details has a line of the companion's counts on both ends. `ms0:/PSP2DC/dev_companion.txt` shows the page in PPSSPP without a Dreamcast (its lines as printed rows).
- **Dreamcast:** `dreamcast/libpsp2dc` (`psp2dc_init`, `psp2dc_update` once a frame, `psp2dc_print`, `psp2dc_record`, `psp2dc_poll`) and `dreamcast/test`. Only `psp2dc_init` waits (up to half a second for each memory card unit); after it, a program with no companion asks one memory card unit a second and goes on. `scripts/build-dreamcast.sh` builds the test with KallistiOS in Docker (the `kazade/dreamcast-sdk` image) and makes `build/psp2dc_test.cdi` for GDEMU.
- **The test** runs five sizes in turn, five seconds each: polls only, then TEST records of 4, 64, 256 and 996 bytes, one a frame. The TV shows commands and bytes a second for each size, errors, the ECHO round trip in frames, the PSP's buttons and the controller's state; X pauses, Y draws the VMU's screen every frame. The PSP shows what arrives a second and whether the TEST records came whole and in order.
- **Checked without hardware:** the PSP page in PPSSPP with sample rows; the rest is B.7.

### B.7 Checked without hardware (20 November 2026)

The three parts were run against each other on the computer. Nothing here says anything about the bus itself: the timing of a reply, a 1 KB frame on the wire and the real speeds still need a Dreamcast.

**The simulator** (`scripts/dev/companion-sim`) is the adapter and a PSP as one small library. The adapter's part is the firmware's own `vmu.c`, built for the computer, and the PSP's is `psp/app/companion.c`; the USB transfer between them is written again by hand after `psp2dc_host.c` (the same message sizes, the reset, the lost transfer).

**The library's test** (`scripts/dev/companion-test.sh`) runs `libpsp2dc` on that, with KallistiOS replaced by the seven functions it uses (and their checks). Ten cases, each from nothing:

| Case | What happens | What must hold |
|---|---|---|
| plain | 3000 frames of records of every size up to 1000 bytes | each arrives once, whole, in order; the printed rows are there |
| slow | the PSP takes a quarter of what is sent | "again" answers, nothing lost |
| lossy | a tenth of the commands and a tenth of the replies are lost | nothing lost, nothing twice |
| far-apart | two sends with 240 to 270 polls between them | both arrive |
| restarts | the PSP unplugged and back, transfers to it lost | the PSP's stream never loses its place; the program starts its page again and all is whole |
| psp-later | no PSP at first | records are refused until it is there |
| no-companion | a memory card that is no companion, then the adapter beside it in port B | no waiting, no errors counted, found within seconds |
| events | six button changes, six ECHOs | in order, the same bytes back |
| card-away | the memory card unit gone for up to two seconds, as when the PSP changes its card | found again; the program starts its page again |
| everything | all of it at once, 300 000 frames | as above |

**What it found**, all in `libpsp2dc`, now fixed:

- The command's `seq` moved on with every poll, so a SEND exactly 255 commands after the last one carried the same `seq`, and the adapter took it for a repeat and dropped it. Only SENDs count now.
- With no companion found, `psp2dc_update` waited up to half a second for each memory card unit, once a second: a game would have stopped that long. It now sends one HELLO a second and does not wait.
- When the companion went away for more than half a second, the records queued were dropped and the program was not told. `psp2dc_session()` now changes then too, so the program sends its page again.
- `psp2dc_idle()` was never true after `psp2dc_update()` (a poll is always out).

The same test fails on the library as it was before these.

**In Flycast.** `scripts/dev/flycast-companion.sh` builds Flycast with the simulator behind port A's first memory card (`companion-sim/flycast.patch`, 54 lines), so the test disc runs on the real KallistiOS: its frame queue, its interrupt handlers, its resend of "again". The test disc there:

| Record size | Commands/s | Bytes/s | Errors |
|---|---|---|---|
| polls only | 60 | 79 | 0 |
| 4 | 60 | 498 | 0 |
| 64 | 60 | 4088 | 0 |
| 256 | 60 | 15 581 | 0 |
| 996 | 60 | 58 873 | 0 |

The round trip of an ECHO was 4 frames, the PSP's buttons arrived, and the simulated PSP counted no bad record. With a PSP reading a third as fast and unplugged every 11 seconds: 37 commands and 36 644 bytes a second at 996 bytes (the rest answered "again"), still no errors and no bad record. In the unchanged Flycast (its memory cards answer the command with "unknown"), the disc reports no companion, counts no errors and keeps running.

These are an emulator's figures: one command a vblank is KallistiOS's own pace, and the emulated bus neither loses nor delays anything. They show the library, the protocol and the adapter's queues agree; the 60 KB/s is the ceiling A.2 derived, not a measurement.

**Read in the source, for the same reason:** nothing in the adapter's receive path is sized below a 255-word frame (the ring is 4096 bytes, the packet buffer 1032, the reply goes out by DMA at any length), and the PSP's USB driver reads up to 64 KB a transfer, so the 1280-byte transfers fit.

**Still open, for the Dreamcast:**

- Whether the adapter's reply to a 1 KB command starts within the bus's timeout (about 1 ms): the handler copies up to 1000 bytes each way and sums the reply first.
- Whether frames this long arrive whole on the real wire, at the rate of one a vblank beside the controller's and the VMU's.
- The real speed to the PSP, and the round trip.
- The adapter's RAM: about 230 KB of 264 KB is in use before the stacks.

