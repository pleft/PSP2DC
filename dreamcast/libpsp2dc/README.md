# libpsp2dc

A KallistiOS library for Dreamcast programs that use a PSP as a second screen through the PSP2DC adapter: text, widgets, drawing and pictures on the PSP's 480 × 272 screen, the PSP's buttons, and files on its Memory Stick. Version 1.0 (`PSP2DC_LIB_MAJOR`, `PSP2DC_LIB_MINOR` in `psp2dc.h`).

What it needs at the other end: the PSP2DC adapter on a controller port, with the PSP2DC application running on a PSP connected to it. Without them the library keeps looking, quietly, and the program runs as it would anyway. How the parts work together is in [the companion's design](../../docs/psp2dc-companion.md).

## Adding it to a program

Three files: `psp2dc.c`, `psp2dc.h` and `protocol/psp2dc_protocol.h`. In a KallistiOS Makefile:

```make
OBJS = main.o ../libpsp2dc/psp2dc.o
KOS_CFLAGS += -I../libpsp2dc -I../../protocol
```

Or as a kos-ports port (`dreamcast/kos-ports/libpsp2dc`, not yet in kos-ports itself): `make install clean` there, then `#include <psp2dc/psp2dc.h>` and link `-lpsp2dc`.

It uses KallistiOS's Maple driver with a frame of its own, and leaves the controller, the VMU and the memory card drivers alone.

## The frame loop

```c
#include "psp2dc.h"

psp2dc_init();                  /* looks for the adapter: up to half a second for each memory card unit */
for (;;) {
    vid_waitvbl();
    psp2dc_update();            /* once a frame: one command goes, its reply brings what the PSP sent */
    /* ... the game, and calls of the library: none of them waits */
}
```

`psp2dc_update()` sends at most one command a vertical blank: what was queued since the last one, or an empty poll. Everything else only queues, remembers, or reads what has come.

## Sessions: when the PSP starts over

The PSP can be unplugged, its application started again, its link reset. Each time the library sees a new session (`psp2dc_session()` changes), and the PSP has nothing of the program's.

- **Sent again for you:** widgets, regions (made again, empty), palettes, sprites, the input focus, the files' folder.
- **For the program to do again:** what it drew into regions, and rows of text. Look at `psp2dc_session()` each frame and redraw when it changes.
- **Ended:** a file operation that was going ends with `PSP2DC_FILE_GONE`; start it again.

## What the calls return

| Kind | Returns |
|---|---|
| Widgets, focus, palettes kept | 0, or -1 for a wrong id or value. They never fail for want of room: `psp2dc_update()` sends them when it can |
| Records, drawing, bitmaps | 0 (queued); -1 when the queue is full (try again next frame) or the thing does not exist; -2 without a PSP |
| File operations (starting one) | 0 started; -1 not now (another is going, or the PSP has just come); -2 cannot be (no PSP, no files on it, a name not allowed) |

## The parts

**Text rows** (`psp2dc_clear`, `psp2dc_print`): 12 rows of the PSP's companion page. The simplest output; the page shows while the program sends anything.

**The PSP's buttons** (`psp2dc_poll`): each change of the PSP's buttons comes as a `PSP2DC_REC_BUTTONS` event, `PSP2DC_PSP_*` bits. They also press the Dreamcast's controller as usual, unless the program takes them: `psp2dc_focus(PSP2DC_PSP_L | PSP2DC_PSP_R)` gives L and R to the program alone (for a list on the PSP) until it gives them back with `psp2dc_focus(0)`. SELECT, PSP2DC's menu, cannot be taken.

**Widgets** (`psp2dc_widget_create`, `_set_u32`, `_set_text`, `_set_range`, `_set_colour`, `_remove`): a label, a number, a bar, a light, a list, drawn by the PSP in its theme. Made once; then set values as often as wanted: only what changed is sent (a value is 8 bytes on the way). 64 of them, ids 0 to 63, drawn in the order of their ids.

**Regions** (`psp2dc_region_create`, `psp2dc_draw_rect`, `_draw_line`, `_draw_text`, `psp2dc_region_clip`, `psp2dc_present`): up to 4 rectangles of pixels under the widgets, drawn into and shown at `psp2dc_present`. The PSP keeps the pixels: draw only what changes.

**Pictures** (`psp2dc_region_palette`, `psp2dc_draw_bitmap`, `psp2dc_sprite_create`, `psp2dc_sprite_ready`, `psp2dc_draw_sprite`): 1, 4 or 8 bits a pixel with a palette, or RGB565. A bitmap is drawn into a region in pieces, as the queue takes them. A sprite is sent once, a little each frame, and then drawn anywhere for 12 bytes: tiles, icons, markers. `dreamcast/tools/psp2dc-image.py picture.png name > name.h` turns a PNG into pixels and a palette in the smallest format its colours fit.

**Files** (`psp2dc_files`, `psp2dc_file_write`, `_read`, `_size`, `_delete`, `_list`, `psp2dc_files_free`, `psp2dc_file_status`): the program's own files in `ms0:/PSP2DC/games/<folder>/` on the Memory Stick. One operation at a time: start it, then ask `psp2dc_file_status()` each frame. A write replaces a file only once all of it arrived, so a file is its old self or its new self whenever the PSP goes. The buffer handed over must stay where it is until the operation ends.

**What the PSP can do** (`psp2dc_get_caps`): its answer shortly after each new session: the level, the screen's size, how many widgets, regions and sprites, the files' limit.

## Limits

| What | Limit |
|---|---|
| Widgets | 64; text 200 characters |
| Regions | 4; their pixels together at most 480 × 272 |
| Sprites | 64; their pixels together 512 KB (a byte a pixel, two in RGB565) |
| A record | 1000 bytes of data |
| Files | 16 MB a folder; names up to 63 of `a-z 0-9 . - _ /`, folders up to 16 of `a-z 0-9 - _` |
| To the PSP | about 59 KB a second (one command a frame, measured) |
| From the PSP | about 54 KB a second for a file read (measured) |

So a value that changes is a widget, a map is sprites placed in a region, and a whole-screen bitmap is for a still (about a second in 8 bits).

## Compatibility

The PSP says its level in CAPS; the adapter its protocol in its HELLO (`psp2dc_version()`).

| PSP level | Has |
|---|---|
| 2 | widgets and regions |
| 3 | pictures |
| 4 | files |
| 5 | input focus |

An older PSP application ignores what it does not know: nothing shows, file operations cannot start (-2), focus does nothing. Adapter protocol 7 works; protocol 8 also sends again a reply that was lost on the bus, so nothing the PSP sends is lost with it.

## Samples, and trying without the hardware

- `dreamcast/samples/hello`: found, a page of text, the PSP's buttons.
- `dreamcast/samples/dashboard`: widgets driven by the controller; L and R taken for a list.
- `dreamcast/samples/save`: a counter kept in a file.
- `dreamcast/demo`: a small game with a map, a dashboard and a list on the PSP.

`scripts/build-dreamcast.sh` builds them all with KallistiOS in Docker, each as a disc image for GDEMU. On a computer, `scripts/dev/companion-samples.sh` runs the samples against a simulated adapter and PSP, `scripts/dev/companion-test.sh` tests the library, and `scripts/dev/flycast-companion.sh` builds Flycast with the simulator behind its memory card, so a disc can be tried in the emulator.
