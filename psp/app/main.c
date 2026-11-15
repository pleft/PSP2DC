/*
 * PSP2DC application: the PSP as a Dreamcast controller.
 *
 * Every frame it reads the PSP's controls, turns them into a Dreamcast
 * controller state and sends that to the adapter over PSPExp's USB link
 * (protocol/psp2dc_protocol.h). A second thread reads what the adapter sends
 * back: a status message once a second and the VMU's screen each time the
 * Dreamcast draws one, shown enlarged at the bottom right of a text status page,
 * and the blocks of the memory card to keep.
 *
 * The memory card is a file on the Memory Stick (cards.c: one of several in
 * ms0:/PSP2DC/cards, a plain 128 KB
 * image). The adapter answers the Dreamcast from a copy in its RAM; this side
 * sends the file up when the adapter has none and writes back what a game
 * saves. The order of things is in the protocol header.
 *
 * User mode; the kernel USB driver (pspexp_usb.prx) was loaded by the boot
 * module and is reached through its exported library (libpspexpusb.a). When
 * it could not be loaded (PPSSPP), the boot module passes --no-usb and the
 * application starts in demo mode, touching no USB at all.
 *
 * SELECT is a shift key: while it is held nothing is sent to the Dreamcast.
 * Tapped alone it changes the stick's source; with Square it starts or stops
 * recording the VMU's screens, with Triangle it takes a screenshot, with
 * Circle it starts or stops the demo (tools.c).
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspusb.h>
#include <pspge.h>
#include <psppower.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pspexp_usb_driver.h"
#include "../../protocol/psp2dc_protocol.h"
#include "tools.h"
#include "ui.h"
#include "vmu_sa.h"
#include "vmu_game.h"
#include "cards.h"
#include "card_sync.h"

PSP_MODULE_INFO("PSP2DC", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(2048);   /* the emulated VMU (vmu_core, ~200 KB) and the tools' buffers */

#define printf pspDebugScreenPrintf

/* The analog nub rests a little off centre and never reaches the corners:
 * a dead zone in the middle, and the rest stretched to the full range. */
#define NUB_DEAD 28
#define NUB_FULL 112

static volatile int g_running = 1;

/* what the adapter last told us (reader thread writes, main thread shows) */
static volatile unsigned g_status_count, g_adapter_uptime_ms, g_adapter_version;
static volatile unsigned g_rx_bad;
static Psp2dcStatus g_status;   /* the last one whole, for its LCD counters */

/* The memory card in use: one of the files in ms0:/PSP2DC/cards (cards.c),
 * outside the application's own folder, which an update replaces as a whole.
 * g_card_lock keeps the saver thread from writing while the card is changed. */
#define CARD_DIR   "ms0:/PSP2DC"
#define CARD_QUIET_US 700000   /* this long with no block coming before the file is written */
static char g_card_name[CARD_NAME_MAX] = "card1";
static SceUID g_card_lock = -1;
static char g_switch_to[CARD_NAME_MAX];    /* connected: the card to change to once the adapter has let go */

static unsigned char g_card[PSP2DC_CARD_BYTES] __attribute__((aligned(64)));
static int g_card_have;                    /* g_card holds a card (from the file, or from the adapter) */
static volatile int g_card_dirty;          /* blocks came since the file was written */
static volatile unsigned g_card_last_us;   /* when the last one came */
static volatile unsigned g_card_blocks_in, g_card_writes, g_card_write_err;
static volatile int g_card_state = -1;     /* the adapter's CARD_STATE, -1: none yet */
static volatile unsigned g_card_state_seq; /* +1 each CARD_STATE */
static volatile unsigned g_adapter_crc;    /* and, LOADED, the card it holds: its CRC-32 and name */
static char g_adapter_name[PSP2DC_CARD_NAME_LEN];
/* Blocks from the adapter are taken only once asked for (CARD_SEND, or the
 * CARD_DONE of an upload): an adapter of protocol 4 sends its card unasked,
 * and that is how one card was written over others (2026-11-15). */
static volatile int g_card_rx_ok;
/* The base: the name and CRC-32 of the last copy of a card both the adapter
 * and the PSP had (after an upload; each time the file is written while the
 * adapter holds that card). Against it, on reconnecting, each side's copy
 * tells whether it changed meanwhile (card_decide). Kept in the settings. */
static char g_base_name[CARD_NAME_MAX];
static unsigned g_base_crc;
static volatile int g_base_due;            /* changed: the settings are to be written */
static char g_adapter_card[CARD_NAME_MAX]; /* connected: the card the adapter holds, once both agree on it */
static char g_return_to[CARD_NAME_MAX];    /* connected: the card to change back to once the adapter's has come */
static unsigned g_dump_from;               /* g_card_blocks_in when the adapter's whole card was asked for */
static unsigned g_return_after_us;         /* not before (a refused change is tried again a moment later) */
static int g_switch_auto;                  /* g_switch_to is ours (card_decide), not the player's */
static const char *g_card_note = "no file yet";

static unsigned now_us(void) { return (unsigned)sceKernelGetSystemTimeLow(); }

static void card_lock(void) { if (g_card_lock >= 0) sceKernelWaitSema(g_card_lock, 1, NULL); }
static void card_unlock(void) { if (g_card_lock >= 0) sceKernelSignalSema(g_card_lock, 1); }

/* The card named in the settings at the start (or the first there is). With
 * none at all the adapter's freshly formatted card becomes card1. */
static void card_load(void)
{
    char names[CARDS_MAX][CARD_NAME_MAX];
    int n;
    cards_migrate();
    if (cards_read(g_card_name, g_card)) { g_card_have = 1; g_card_note = "from the file"; return; }
    if ((n = cards_list(names)) > 0 && cards_read(names[0], g_card)) {
        snprintf(g_card_name, sizeof(g_card_name), "%s", names[0]);
        g_card_have = 1; g_card_note = "from the file";
    }
}

static int card_write(void) { return cards_write(g_card_name, g_card); }
static unsigned card_crc(const unsigned char *img) { return psp2dc_crc32(0, img, PSP2DC_CARD_BYTES); }
static void base_set(const char *name, unsigned crc)
{
    snprintf(g_base_name, sizeof(g_base_name), "%s", name);
    g_base_crc = crc;
    g_base_due = 1;
}

/* A VMU game still running ends, its flash put back into g_card, the card
 * it was started from: before g_card becomes another card or the
 * Dreamcast's. Left running, it once wrote the games card over the card
 * switched to when it ended (2026-11-15). */
static void vmu_game_end(void)
{
    if (sa_stop(g_card_have ? g_card : NULL)) {
        g_card_dirty = 1;
        g_card_last_us = now_us();
        g_card_note = "changed on the VMU";
    }
}

/* Puts another card in g_card: the one in use written first if it changed.
 * The caller makes sure nothing is on its way into it. 0, or < 0 when the
 * other one could not be read (and nothing changed). */
static int card_change(const char *name)
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    int rc = -1;
    vmu_game_end();                        /* its card is the one in use: written below */
    if (!cards_read(name, img)) return -1;
    card_lock();
    if (g_card_dirty && g_card_have && card_write() == 0) g_card_dirty = 0;
    if (!g_card_dirty) {
        memcpy(g_card, img, sizeof(img));
        snprintf(g_card_name, sizeof(g_card_name), "%s", name);
        sa_reset();                        /* another card, another VMU: nothing of the last one stays */
        g_card_have = 1;
        g_card_note = "changed";
        rc = 0;
    }
    card_unlock();
    return rc;
}

/* Writes the file a moment after the adapter's blocks stop coming: a game's
 * save is a run of them, and the whole card after a reconnection. */
static int saver_thread(SceSize args, void *argp)
{
    while (g_running) {
        sceKernelDelayThread(200000);
        if (g_card_dirty && now_us() - g_card_last_us > CARD_QUIET_US) {
            card_lock();
            if (g_card_dirty) {
                /* a copy, so the bytes written are the ones the CRC is of
                 * (blocks may come while it is written) */
                static unsigned char snap[PSP2DC_CARD_BYTES];
                g_card_dirty = 0;   /* before the copy: a block coming after it sets it again */
                memcpy(snap, g_card, sizeof(snap));
                if (cards_write(g_card_name, snap) < 0) { g_card_write_err++; g_card_dirty = 1; g_card_last_us = now_us(); }
                else {
                    g_card_writes++; g_card_note = "saved to the file";
                    /* what the adapter holds, or older: never ahead of it */
                    if (g_adapter_card[0] && !strcasecmp(g_adapter_card, g_card_name) && pspexpUsbConnected())
                        base_set(g_card_name, card_crc(snap));
                }
            }
            card_unlock();
        }
    }
    return 0;
}

/* The VMU's screen as the adapter last sent it, and how many came. */
static unsigned char g_lcd[PSP2DC_LCD_BYTES];
static volatile unsigned g_lcd_count;

/* How the USB came up, for the status page when it did not: the driver's own
 * start, then each step by hand (bus driver, PSPExp's driver, activation). */
#define USB_ALREADY 0x80243001   /* "already done": that step was already in effect */
#define PSPEXP_DRIVER_NAME "PSPExpUSBDriver"
#define PSPEXP_DRIVER_PID  0x1CA
static int g_usb_kernel, g_usb_bus, g_usb_drv, g_usb_act, g_usb_tries;
static unsigned g_usb_restarts;   /* times the USB was started afresh for an adapter that did not sync */
static int g_no_usb;              /* --no-usb: the driver is not there (PPSSPP); demo only */

/* What is on the screen: the VMU large, the memory card's saves, or the
 * status page. Kept with the theme and the stick's source in SETTINGS. */
enum { PAGE_VMU, PAGE_SAVES, PAGE_STATUS, PAGES };
static int g_page = PAGE_VMU, g_theme, g_saves_first, g_saves_count;
#define SETTINGS "ms0:/PSP2DC/settings.txt"

/* The driver's start does its three steps in one go and stops at the first
 * error, "already done" included. When that fails, the steps one by one, with
 * "already done" not counted as a failure (PSPExp's plugin does the same after
 * the HOME menu). 0 when the device ended up started and activated. */
static int usb_start(void)
{
    g_usb_kernel = pspexpUsbStart();
    g_usb_bus = g_usb_drv = g_usb_act = 0;
    if (g_usb_kernel >= 0) return 0;
    for (g_usb_tries = 1; g_usb_tries <= 3; g_usb_tries++) {
        g_usb_bus = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
        g_usb_drv = sceUsbStart(PSPEXP_DRIVER_NAME, 0, 0);
        g_usb_act = sceUsbActivate(PSPEXP_DRIVER_PID);
        if ((g_usb_drv >= 0 || (unsigned)g_usb_drv == USB_ALREADY) &&
            (g_usb_act >= 0 || (unsigned)g_usb_act == USB_ALREADY)) return 0;
        pspexpUsbStop();
        sceKernelDelayThread(500000);
    }
    return -1;
}

static int exit_callback(int a, int b, void *c) { g_running = 0; sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}


static unsigned char nub(unsigned char raw)
{
    int v = (int)raw - 128, out;
    if (v > -NUB_DEAD && v < NUB_DEAD) return 128;
    out = v > 0 ? (v - NUB_DEAD) * 127 / (NUB_FULL - NUB_DEAD) : (v + NUB_DEAD) * 128 / (NUB_FULL - NUB_DEAD);
    if (out > 127) out = 127;
    if (out < -128) out = -128;
    return (unsigned char)(128 + out);
}

/* The default mapping. L and R are digital on the PSP: fully pressed or not. */
static void map_input(const SceCtrlData *pad, Psp2dcInput *in)
{
    unsigned b = pad->Buttons;
    in->buttons = 0;
    if (b & PSP_CTRL_CROSS)    in->buttons |= PSP2DC_BTN_A;
    if (b & PSP_CTRL_CIRCLE)   in->buttons |= PSP2DC_BTN_B;
    if (b & PSP_CTRL_SQUARE)   in->buttons |= PSP2DC_BTN_X;
    if (b & PSP_CTRL_TRIANGLE) in->buttons |= PSP2DC_BTN_Y;
    if (b & PSP_CTRL_START)    in->buttons |= PSP2DC_BTN_START;
    if (b & PSP_CTRL_UP)       in->buttons |= PSP2DC_BTN_UP;
    if (b & PSP_CTRL_DOWN)     in->buttons |= PSP2DC_BTN_DOWN;
    if (b & PSP_CTRL_LEFT)     in->buttons |= PSP2DC_BTN_LEFT;
    if (b & PSP_CTRL_RIGHT)    in->buttons |= PSP2DC_BTN_RIGHT;
    in->stick_x = nub(pad->Lx);
    in->stick_y = nub(pad->Ly);
    in->trigger_l = (b & PSP_CTRL_LTRIGGER) ? 255 : 0;
    in->trigger_r = (b & PSP_CTRL_RTRIGGER) ? 255 : 0;
    in->reserved = 0;
}

static int send_msg(unsigned char type, const void *payload, int len)
{
    static unsigned char buf[64] __attribute__((aligned(64)));
    buf[0] = PSP2DC_MAGIC0; buf[1] = PSP2DC_MAGIC1; buf[2] = type; buf[3] = (unsigned char)len;
    if (len) memcpy(buf + PSP2DC_HEADER_LEN, payload, len);
    return pspexpUsbWrite(buf, PSP2DC_HEADER_LEN + len);
}

/* The adapter's messages, out of the byte stream. */
static int reader_thread(SceSize args, void *argp)
{
    static unsigned char stream[1024];
    int have = 0;
    while (g_running) {
        int n, pos = 0;
        if (!pspexpUsbConnected()) { have = 0; sceKernelDelayThread(100000); continue; }
        n = pspexpUsbRead(stream + have, sizeof(stream) - have, 500000);
        if (n <= 0) { if (n < 0) have = 0; continue; }
        have += n;
        while (have - pos >= PSP2DC_HEADER_LEN) {
            const unsigned char *p = stream + pos;
            int total;
            if (p[0] != PSP2DC_MAGIC0 || p[1] != PSP2DC_MAGIC1) { pos++; g_rx_bad++; continue; }
            total = PSP2DC_HEADER_LEN + p[3];
            if (have - pos < total) break;
            if (p[2] == PSP2DC_MSG_STATUS && p[3] == sizeof(Psp2dcStatus)) {
                Psp2dcStatus st;
                memcpy(&st, p + PSP2DC_HEADER_LEN, sizeof(st));
                g_status = st;
                g_adapter_uptime_ms = st.uptime_ms;
                g_adapter_version = st.version;
                g_status_count++;
            } else if (p[2] == PSP2DC_MSG_CARD_STATE && p[3] == sizeof(Psp2dcCardState)) {
                Psp2dcCardState cs;
                memcpy(&cs, p + PSP2DC_HEADER_LEN, sizeof(cs));
                cs.name[sizeof(cs.name) - 1] = 0;
                memcpy(g_adapter_name, cs.name, sizeof(g_adapter_name));
                g_adapter_crc = cs.crc;
                g_card_state = cs.state;
                g_card_state_seq++;
            } else if (p[2] == PSP2DC_MSG_CARD_SAVE && p[3] == sizeof(Psp2dcCardPart)) {
                const Psp2dcCardPart *part = (const Psp2dcCardPart *)(p + PSP2DC_HEADER_LEN);
                if (g_card_rx_ok && part->part < PSP2DC_CARD_PARTS) {
                    memcpy(g_card + part->block * PSP2DC_CARD_BLOCK_SIZE + part->part * PSP2DC_CARD_PART_SIZE,
                           part->data, PSP2DC_CARD_PART_SIZE);
                    g_card_have = 1;
                    g_card_last_us = now_us();
                    g_card_dirty = 1;
                    if (part->part == PSP2DC_CARD_PARTS - 1) g_card_blocks_in++;
                }
            } else if (p[2] == PSP2DC_MSG_LCD && p[3] == PSP2DC_LCD_BYTES) {
                memcpy(g_lcd, p + PSP2DC_HEADER_LEN, PSP2DC_LCD_BYTES);
                g_lcd_count++;
                rec_frame(g_lcd);
            }
            pos += total;
        }
        memmove(stream, stream + pos, have - pos);
        have -= pos;
        if (have == (int)sizeof(stream)) have = 0;   /* a message never is this long */
    }
    return 0;
}

/* The card file to the adapter: a block a transfer (its four parts), then the
 * end. Nothing else is sent meanwhile; a second or two at full speed. */
static int card_upload(void)
{
    static unsigned char buf[PSP2DC_CARD_BLOCK_XFER] __attribute__((aligned(64)));
    int block, part;
    for (block = 0; block < PSP2DC_CARD_BLOCKS; block++) {
        unsigned char *m = buf;
        for (part = 0; part < PSP2DC_CARD_PARTS; part++) {
            m[0] = PSP2DC_MAGIC0; m[1] = PSP2DC_MAGIC1; m[2] = PSP2DC_MSG_CARD_PART; m[3] = sizeof(Psp2dcCardPart);
            m[4] = (unsigned char)block; m[5] = (unsigned char)part;
            memcpy(m + 6, g_card + block * PSP2DC_CARD_BLOCK_SIZE + part * PSP2DC_CARD_PART_SIZE, PSP2DC_CARD_PART_SIZE);
            m += PSP2DC_HEADER_LEN + sizeof(Psp2dcCardPart);
        }
        if (pspexpUsbWrite(buf, sizeof(buf)) < 0) return -1;
    }
    return 0;
}

static void show(int usb_rc, const Psp2dcInput *in, unsigned sent, unsigned errors)
{
    PspExpUsbStats st;
    const char *link;
    memset(&st, 0, sizeof(st));
    if (!g_no_usb) pspexpUsbGetStats(&st);
    ui_begin(0xFF000000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    link = g_no_usb ? "no USB (emulator?)            "
         : pspexpUsbConnected() ? "CONNECTED to the adapter      "
         : pspexpUsbAttached()  ? "cable in, waiting for adapter "
         :                        "waiting for the USB cable     ";
    pspDebugScreenSetXY(0, 0);
    printf("PSP2DC  -  the PSP as a Dreamcast controller\n\n");
    if (g_usb_kernel < 0) {
        printf("usb start %s: driver %08X, then by hand (try %d)\n", usb_rc < 0 ? "FAILED" : "ok",
               (unsigned)g_usb_kernel, g_usb_tries);
        printf("  bus %08X  device %08X  activate %08X  state %08X\n", (unsigned)g_usb_bus,
               (unsigned)g_usb_drv, (unsigned)g_usb_act, (unsigned)sceUsbGetState());
    }
    printf("link     %s\n", link);
    printf("usb      packet %u  attach %u  detach %u  sync %u  restarts %u    \n",
           (unsigned)st.packet_size, (unsigned)st.attaches, (unsigned)st.detaches, (unsigned)st.resyncs,
           g_usb_restarts);
    printf("sent     %u inputs  %u errors  tx timeouts %u      \n", sent, errors, (unsigned)st.tx_timeouts);
    printf("adapter  %u status msgs  up %u s  protocol %u  bad %u      \n",
           g_status_count, g_adapter_uptime_ms / 1000, g_adapter_version, g_rx_bad);
    printf("vmu      %u screens here; adapter: asked %u wrote %u took %u\n", g_lcd_count,
           g_status.lcd_media, g_status.lcd_writes, g_status.lcd_screens);
    printf("card     %s; %s%u to come      \n", g_card_note,
           (g_status.flags & PSP2DC_STATUS_CARD) ? "in the Dreamcast; " : "not shown yet; ", g_status.card_pending);
    printf("         %u blocks kept, file written %u times, %u errors      \n",
           g_card_blocks_in, g_card_writes, g_card_write_err);
    printf("draw     %u.%u ms a frame, longest %u.%u ms; VMU CPU %u.%u ms      \n", ui_draw_us() / 1000,
           ui_draw_us() / 100 % 10, ui_draw_max_us() / 1000, ui_draw_max_us() / 100 % 10,
           game_frame_us() / 1000, game_frame_us() / 100 % 10);
    printf("tools    %-44s\n", demo_on() ? demo_note() : rec_on() ? "RECORDING the VMU's screens" : rec_note());
    printf("         %-44s\n", rec_on() ? "" : shot_note());
    if (rec_on()) { pspDebugScreenSetXY(37, pspDebugScreenGetY() - 1); printf("%4u", rec_count()); pspDebugScreenSetXY(0, pspDebugScreenGetY() + 1); }
    printf("Dreamcast controller state\n");
    printf("  buttons  %c %c %c %c  %s   dpad %c%c%c%c      \n",
           in->buttons & PSP2DC_BTN_A ? 'A' : '.', in->buttons & PSP2DC_BTN_B ? 'B' : '.',
           in->buttons & PSP2DC_BTN_X ? 'X' : '.', in->buttons & PSP2DC_BTN_Y ? 'Y' : '.',
           in->buttons & PSP2DC_BTN_START ? "START" : ".....",
           in->buttons & PSP2DC_BTN_UP ? 'U' : '.', in->buttons & PSP2DC_BTN_DOWN ? 'D' : '.',
           in->buttons & PSP2DC_BTN_LEFT ? 'L' : '.', in->buttons & PSP2DC_BTN_RIGHT ? 'R' : '.');
    printf("  stick    x %3u  y %3u      \n", in->stick_x, in->stick_y);
    printf("  triggers L %3u  R %3u      \n", in->trigger_l, in->trigger_r);
    printf("Cross=A Circle=B Square=X Triangle=Y\n");
    printf("SELECT+Left/Right page  +Triangle shot\n");
    printf("SELECT+L/R card  +Start new card\n");
    printf("SELECT+Circle demo   HOME quit");
    ui_lcd(g_lcd, 480 - 48 * 4 - 8, 272 - 32 * 4 - 8, 4, &ui_themes[g_theme]);
    ui_end();
}

static void settings_load(void)
{
    char buf[256];
    const char *p;
    int n;
    SceUID fd = sceIoOpen(SETTINGS, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = 0;
    if ((p = strstr(buf, "page=")))  g_page = atoi(p + 5) % PAGES;
    if ((p = strstr(buf, "theme="))) g_theme = atoi(p + 6) % ui_theme_count;
    if ((p = strstr(buf, "card="))) {
        int i;
        for (i = 0; i < CARD_NAME_MAX - 1 && p[5 + i] && p[5 + i] != '\n' && p[5 + i] != '\r'; i++) g_card_name[i] = p[5 + i];
        g_card_name[i] = 0;
    }
    if ((p = strstr(buf, "base=")) && strlen(p) > 14 && p[13] == ' ') {   /* base=CRC NAME */
        int i;
        g_base_crc = (unsigned)strtoul(p + 5, NULL, 16);
        for (i = 0; i < CARD_NAME_MAX - 1 && p[14 + i] && p[14 + i] != '\n' && p[14 + i] != '\r'; i++) g_base_name[i] = p[14 + i];
        g_base_name[i] = 0;
    }
}

static void settings_save(void)
{
    char buf[200];
    SceUID fd;
    int n = snprintf(buf, sizeof(buf), "page=%d\ntheme=%d\ncard=%s\n", g_page, g_theme, g_card_name);
    if (g_base_name[0]) n += snprintf(buf + n, sizeof(buf) - n, "base=%08x %s\n", g_base_crc, g_base_name);
    sceIoMkdir(CARD_DIR, 0777);
    if ((fd = sceIoOpen(SETTINGS, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777)) < 0) return;
    sceIoWrite(fd, buf, n);
    sceIoClose(fd);
}

/* A word on what just changed (the stick's source, a screenshot), shown in
 * the top line for two seconds in place of the card's state. */
static char g_notice[48];
static unsigned g_notice_at;
static void notice(const char *s) { snprintf(g_notice, sizeof(g_notice), "%s", s); g_notice_at = now_us() | 1; }

/* ms0:/PSP2DC/dev_input.txt, for trying button combinations from PPSSPP
 * (a key held there cannot be): lines "<ms after the start> <buttons>", the
 * buttons joined by + (up down left right cross circle square triangle
 * start select l r, or none), each held until the next line; "shot" in a
 * line also takes a screenshot (ms0:/PSP2DC/shots) as it starts, without
 * the notice. Absent, nothing. */
#define DEV_STEPS 128
#define DEV_SHOT 0x80000000u
static int g_dev_shot_done = -1;           /* the last step whose screenshot was taken */
static struct { unsigned at, buttons; } g_dev_steps[DEV_STEPS];
static int g_dev_count;

static void dev_input_load(void)
{
    static const struct { const char *name; unsigned bit; } keys[] = {
        {"up", PSP_CTRL_UP}, {"down", PSP_CTRL_DOWN}, {"left", PSP_CTRL_LEFT}, {"right", PSP_CTRL_RIGHT},
        {"cross", PSP_CTRL_CROSS}, {"circle", PSP_CTRL_CIRCLE}, {"square", PSP_CTRL_SQUARE},
        {"triangle", PSP_CTRL_TRIANGLE}, {"start", PSP_CTRL_START}, {"select", PSP_CTRL_SELECT},
        {"l", PSP_CTRL_LTRIGGER}, {"r", PSP_CTRL_RTRIGGER}, {"none", 0}, {"shot", DEV_SHOT},
    };
    static char buf[2048];
    char *line, *next;
    int n;
    SceUID fd = sceIoOpen(CARD_DIR "/dev_input.txt", PSP_O_RDONLY, 0);
    if (fd < 0) return;
    n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = 0;
    for (line = buf; line && *line && g_dev_count < DEV_STEPS; line = next) {
        char *tok;
        unsigned at = (unsigned)atoi(line), bits = 0, k;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!(tok = strchr(line, ' '))) continue;
        for (tok = strtok(tok + 1, "+ \r"); tok; tok = strtok(NULL, "+ \r"))
            for (k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
                if (!strcmp(tok, keys[k].name)) bits |= keys[k].bit;
        g_dev_steps[g_dev_count].at = at;
        g_dev_steps[g_dev_count].buttons = bits;
        g_dev_count++;
    }
}

static unsigned dev_input(unsigned ms_since_start)
{
    unsigned bits = 0;
    int i;
    for (i = 0; i < g_dev_count && g_dev_steps[i].at <= ms_since_start; i++) bits = g_dev_steps[i].buttons;
    if ((bits & DEV_SHOT) && g_dev_shot_done != i - 1) { g_dev_shot_done = i - 1; shot_take(); }
    return bits & ~DEV_SHOT;
}

/* The adapter is to let go of its card so the PSP's goes in (the answer,
 * CARD_STATE, is handled in the main loop: EMPTY, then the upload). auto: ours,
 * tried again when refused; discard: the adapter's blocks not yet sent are
 * not needed (card_decide found its copy no newer). */
static void card_eject(const char *to, int automatic, int discard)
{
    Psp2dcCardEject ej = { discard ? PSP2DC_EJECT_DISCARD : 0, {0, 0, 0} };
    snprintf(g_switch_to, sizeof(g_switch_to), "%s", to);
    g_switch_auto = automatic;
    send_msg(PSP2DC_MSG_CARD_EJECT, &ej, sizeof(ej));   /* (blocks until the answer are still its card's) */
}

static void card_send(int all)
{
    Psp2dcCardSend cs = { (unsigned char)all, {0, 0, 0} };
    if (all) g_dump_from = g_card_blocks_in;
    g_card_rx_ok = 1;
    send_msg(PSP2DC_MSG_CARD_SEND, &cs, sizeof(cs));
}

/* The adapter still holds a card from before (the PSP was unplugged or
 * restarted): which copy counts. A, the adapter's card by name; a, its CRC;
 * ours, the PSP's copy of A (the card in use, or A's file); the base, the
 * last copy of A both had. Nothing is ever written over a copy that changed:
 *   the same               nothing comes; the PSP's card in use goes in
 *   only the adapter's     it comes, into A (the card in use for the while)
 *     changed              and the PSP's card in use goes in after
 *   only ours changed      the PSP's card in use goes in
 *   both, or not known     it comes into a new card (A-dc), and the PSP's
 *                          card in use goes in after */
static void card_decide(void)
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    char a_name[CARD_NAME_MAX], kept[CARD_NAME_MAX], mine[CARD_NAME_MAX], msg[48];

    SyncState st;
    int in_use;
    snprintf(a_name, sizeof(a_name), "%s", g_adapter_name[0] ? g_adapter_name : "dreamcast");
    /* the player's card: the one in use, or the one to go back to when the
     * link broke while the adapter's was coming into another */
    snprintf(mine, sizeof(mine), "%s", g_return_to[0] ? g_return_to : g_card_name);
    g_return_to[0] = 0;
    in_use = g_card_have && !strcasecmp(a_name, g_card_name);
    card_lock();                           /* the card in use in its file first: A's file is then ours of A */
    if (g_card_dirty && g_card_have && card_write() == 0) g_card_dirty = 0;
    card_unlock();
    memset(&st, 0, sizeof(st));
    st.have_card = g_card_have;
    st.a_is_mine = !strcasecmp(a_name, mine);
    if (in_use) { st.ours = card_crc(g_card); st.have_ours = 1; }
    else if (cards_read(a_name, img)) { st.ours = card_crc(img); st.have_ours = 1; }
    st.a = g_adapter_crc;
    st.base_is_a = g_base_name[0] && !strcasecmp(g_base_name, a_name);
    st.base = g_base_crc;

    switch (card_sync_choose(&st)) {
    case SYNC_ADOPT:                       /* no card at all on the PSP: the adapter's becomes it */
        snprintf(g_card_name, sizeof(g_card_name), "%s", a_name);
        snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", a_name);
        card_send(1);
        g_card_note = "from the adapter";
        return;
    case SYNC_SAME:
        base_set(a_name, st.a);
        if (!in_use) { card_eject(mine, 1, 1); return; }   /* (in use: a copy left half come) */
        snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", a_name);
        card_send(0);
        g_card_note = "same as the adapter's";
        return;
    case SYNC_PUT_OURS:
        if (st.have_ours && st.ours == st.a) base_set(a_name, st.a);
        card_eject(mine, 1, 1);
        return;
    case SYNC_TAKE:
        if (!in_use && card_change(a_name) < 0) { notice("could not open that card"); return; }
        if (!st.a_is_mine) snprintf(g_return_to, sizeof(g_return_to), "%s", mine);
        snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", a_name);
        card_send(1);
        snprintf(msg, sizeof(msg), "saves from the Dreamcast");
        notice(msg);
        return;
    case SYNC_KEEP_BOTH:
        break;
    }
    /* both changed, or not known: the adapter's copy into a card of its own */
    if (cards_unique(a_name, "-dc", kept) < 0) { notice("no name for the DC copy"); return; }
    card_lock();
    if (g_card_dirty && card_write() == 0) g_card_dirty = 0;
    if (g_card_dirty) { card_unlock(); notice("could not save the card"); return; }
    if (st.have_ours && !in_use) memcpy(g_card, img, sizeof(img));   /* (every block is sent: only a start) */
    snprintf(g_card_name, sizeof(g_card_name), "%s", kept);
    card_unlock();
    snprintf(g_return_to, sizeof(g_return_to), "%s", mine);
    snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", a_name);   /* (not the card in use: no base) */
    card_send(1);
    g_card_note = "the Dreamcast's copy";
    snprintf(msg, sizeof(msg), "Dreamcast copy: %s", kept);
    notice(msg);
}

/* The line at the top of the VMU and saves pages */
static const char *top_line(void)
{
    static char buf[80];
    const char *link = demo_on() ? "DEMO" : g_no_usb ? "no USB" : pspexpUsbConnected() ? "CONNECTED"
                     : pspexpUsbAttached() ? "cable in, waiting" : "no cable";
    if (g_notice_at && now_us() - g_notice_at < 2000000)
        snprintf(buf, sizeof(buf), "PSP2DC  %-17s %s", link, g_notice);
    else
        snprintf(buf, sizeof(buf), "PSP2DC  %-17s %s: %s", link, g_card_name, g_card_have ? g_card_note : "none yet");
    return buf;
}

int main(int argc, char *argv[])
{
    SceCtrlData pad;
    Psp2dcInput in;
    unsigned generation = 0, sent = 0, errors = 0, frame = 0, last_buttons = 0, lcd_shown = ~0u;
    unsigned state_seen = 0, waiting_since = 0, usb_restarts = 0;
    unsigned char seq = 0;
    int usb_rc = 0, connected = 0, shot_due = 0, autoshot = 0, i, settings_due = 0, version_told = 0;
    int standalone = 0, was_standalone = 0;
    unsigned vmu_held = 0, vmu_last = 0, vmu_icons = 0, last_frame_us = 0;
    static unsigned char sa_lcd[PSP2DC_LCD_BYTES];
    unsigned start_us = now_us();
    SceUID thid;
    SceIoStat ist;

    for (i = 1; i < argc; i++) if (!strcmp(argv[i], "--no-usb")) g_no_usb = 1;
    /* ms0:/PSP2DC/dev_autoshot: screenshots taken by themselves, to look at a
     * layout from PPSSPP without pressing anything */
    {
        SceUID fd = sceIoOpen(TOOLS_DIR "/dev_autoshot", PSP_O_RDONLY, 0);
        if (fd >= 0) { autoshot = 1; sceIoClose(fd); }
    }
    (void)ist;

    /* Every page is drawn whole into the frame not on screen and shown at the
     * next vblank (ui.c): the status text once blanked the VMU picture's top
     * rows at each redraw, a flickering band (Crazy Taxi 2, 2026-11-15). */
    /* the full speed: the shaded VMU and its panels are redrawn many times a
     * second (PSPExp's runtime runs at 333 MHz too) */
    scePowerSetClockFrequency(333, 333, 166);
    ui_init();
    settings_load();
    thid = sceKernelCreateThread("cb", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    g_card_lock = sceKernelCreateSema("card", 0, 1, 1, NULL);
    dev_input_load();
    card_load();
    game_audio_start();
    thid = sceKernelCreateThread("save", saver_thread, 0x20, 0x4000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    if (g_no_usb) {
        /* no USB (PPSSPP): standalone, as with no Dreamcast */
    } else {
        usb_rc = usb_start();
        if ((unsigned)g_usb_kernel == 0x8002013A) {
            /* "library not linked": the driver is loaded but its library is
             * not there to call (PPSSPP loads the module, its functions are
             * stubs). Nothing can come over USB: demo mode. */
            g_no_usb = 1;   /* standalone, as with no Dreamcast */
        } else {
            thid = sceKernelCreateThread("rx", reader_thread, 0x18, 0x4000, 0, NULL);
            if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
        }
    }

    memset(&in, 0, sizeof(in));
    while (g_running) {
        sceCtrlReadBufferPositive(&pad, 1);   /* waits for the next sample: once a frame */
        if (g_dev_count) pad.Buttons |= dev_input((now_us() - start_us) / 1000);
        if (pad.Buttons & PSP_CTRL_SELECT) {  /* the shift key: nothing goes to the Dreamcast */
            unsigned pressed = pad.Buttons & ~last_buttons;
            if (pressed & PSP_CTRL_SQUARE) { if (rec_on()) rec_stop(); else rec_start(); }
            if (pressed & PSP_CTRL_TRIANGLE) { shot_due = 1; }
            if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
                g_page = (g_page + (pressed & PSP_CTRL_RIGHT ? 1 : PAGES - 1)) % PAGES;
                settings_due = 1;
            }
            if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) {
                int d = pressed & PSP_CTRL_DOWN ? 1 : -1;
                if (g_page == PAGE_SAVES) {
                    g_saves_first += d;
                    if (g_saves_first > g_saves_count - 3) g_saves_first = g_saves_count - 3;
                    if (g_saves_first < 0) g_saves_first = 0;
                } else {
                    g_theme = (g_theme + d + ui_theme_count) % ui_theme_count;
                    settings_due = 1;
                }
               
            }
            if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START)) {
                char names[CARDS_MAX][CARD_NAME_MAX], made[CARD_NAME_MAX], msg[48];
                int n = cards_list(names), cur = 0, k;
                const char *to = NULL;
               
                if (pressed & PSP_CTRL_START) {          /* a new, empty card, and changed to it */
                    if (cards_new(made) == 0) { to = made; n = cards_list(names); }
                    else notice("could not make a card");
                } else if (n > 1) {
                    for (k = 0; k < n; k++) if (!strcasecmp(names[k], g_card_name)) cur = k;   /* (FAT names: any case) */
                    to = names[(cur + (pressed & PSP_CTRL_RTRIGGER ? 1 : n - 1)) % n];
                } else notice("only one card so far");
                if (to && game_running()) { notice("leave the VMU game first"); to = NULL; }
                if (to && !g_no_usb && pspexpUsbConnected()) {
                    /* the adapter lets go of the card first, once every block
                     * has reached us; the change happens on its answer */
                    if (g_switch_to[0] || g_return_to[0] || g_status.card_pending || g_card_dirty ||
                        now_us() - g_card_last_us < 2000000)
                        notice("card busy, try again");
                    else {
                        card_eject(to, 0, 0);
                        notice("changing card...");
                    }
                } else if (to) {
                    g_return_to[0] = 0;                  /* the player chose: nothing to go back to */
                    if (card_change(to) == 0) {
                        for (k = 0; k < n; k++) if (!strcasecmp(names[k], to)) cur = k;
                        snprintf(msg, sizeof(msg), "card %d of %d", cur + 1, n);
                        notice(msg);
                        settings_due = 1;
                    } else notice("could not read that card");
                }
            }
            if (pressed & PSP_CTRL_CIRCLE) {
                if (demo_on()) demo_stop();
                else if (!connected) demo_start();
               
            }
            last_buttons = pad.Buttons;
            pad.Buttons = 0;
            pad.Lx = pad.Ly = 128;
        } else last_buttons = pad.Buttons;
        map_input(&pad, &in);

        /* No Dreamcast: the PSP is the VMU standalone (vmu_sa.c), its buttons
         * the VMU's. (SELECT, held, is still the shift key.) */
        standalone = !demo_on() && (g_no_usb || !pspexpUsbConnected());
        vmu_held = 0;
        if (standalone) {
            unsigned b = pad.Buttons;
            if (b & PSP_CTRL_UP) vmu_held |= SA_UP;
            if (b & PSP_CTRL_DOWN) vmu_held |= SA_DOWN;
            if (b & PSP_CTRL_LEFT) vmu_held |= SA_LEFT;
            if (b & PSP_CTRL_RIGHT) vmu_held |= SA_RIGHT;
            if (b & PSP_CTRL_CROSS) vmu_held |= SA_A;
            if (b & PSP_CTRL_CIRCLE) vmu_held |= SA_B;
            /* SLEEP and MODE sit side by side above A and B on the VMU:
             * Square and Triangle, the PSP's buttons nearest that */
            if (b & PSP_CTRL_TRIANGLE) vmu_held |= SA_MODE;
            if (b & PSP_CTRL_SQUARE) vmu_held |= SA_SLEEP;
            if (!was_standalone) sa_reset();
            {
                unsigned t = now_us(), ms = last_frame_us ? (t - last_frame_us) / 1000 : 16;
                if (ms > 50) ms = 50;              /* (a stall is not VMU time) */
                sa_card_name(g_card_name);
                if (sa_input(vmu_held & ~vmu_last, vmu_held, g_card_have ? g_card : NULL, ms)) {
                    g_card_dirty = 1;              /* a game's save, a deleted file: the file is written */
                    g_card_last_us = now_us();
                    g_card_note = "changed on the VMU";
                }
            }
            vmu_icons = sa_render(sa_lcd, g_card_have ? g_card : NULL);
            if (memcmp(sa_lcd, g_lcd, sizeof(sa_lcd))) { memcpy(g_lcd, sa_lcd, sizeof(sa_lcd)); g_lcd_count++; }
        } else if (was_standalone) {
            vmu_game_end();                    /* before the card goes to the adapter, below */
            memset(g_lcd, 0, sizeof(g_lcd));   /* the Dreamcast's screen from here on */
            g_lcd_count++;
        }
        vmu_last = vmu_held;
        was_standalone = standalone;
        last_frame_us = now_us();

        if (demo_on() && demo_frame(g_lcd)) g_lcd_count++;
        if (g_no_usb) {
            /* nothing to send */
        } else if (pspexpUsbConnected()) {
            if (demo_on()) demo_stop();       /* the real thing wins */
            unsigned gen = pspexpUsbGeneration();
            if (!connected || gen != generation) {   /* a new link: say who we are */
                Psp2dcHello hello = { PSP2DC_PROTOCOL_VERSION, g_card_have ? PSP2DC_HELLO_HAS_CARD : 0, {0, 0} };
                generation = gen;
                connected = 1;
                g_card_rx_ok = 0;                /* until card_decide or an upload says */
                g_adapter_card[0] = g_switch_to[0] = 0;   /* (g_return_to stays: card_decide goes on) */
                state_seen = g_card_state_seq;   /* the answer to this hello is the next one */
                send_msg(PSP2DC_MSG_HELLO, &hello, sizeof(hello));
            }
            if (g_card_state_seq != state_seen) {   /* the adapter said which card it holds */
                int answered = 0;
                state_seen = g_card_state_seq;
                if (g_switch_to[0]) {                  /* the answer to a CARD_EJECT */
                    answered = 1;
                    if (g_card_state == PSP2DC_CARD_EMPTY) {
                        char msg[48];
                        g_adapter_card[0] = 0;
                        if (strcasecmp(g_switch_to, g_card_name)) {
                            if (card_change(g_switch_to) == 0) {
                                snprintf(msg, sizeof(msg), "card changed");
                                settings_due = 1;
                            } else snprintf(msg, sizeof(msg), "could not read it: kept");
                            notice(msg);               /* (either way the adapter gets the card in use) */
                        }
                    } else if (g_switch_auto) {        /* ours: again in a moment */
                        snprintf(g_return_to, sizeof(g_return_to), "%s", g_switch_to);
                        g_return_after_us = now_us() + 1000000;
                    } else notice("card busy, try again");
                    g_switch_to[0] = 0;
                    g_switch_auto = 0;
                }
                if (g_card_state == PSP2DC_CARD_EMPTY) {
                    Psp2dcCardDone done;
                    g_card_rx_ok = 0;                  /* it has no card: nothing is to come */
                    memset(&done, 0, sizeof(done));
                    snprintf(done.name, sizeof(done.name), "%s", g_card_name);
                    if (g_card_have) {
                        unsigned crc = card_crc(g_card);
                        /* a broken upload ends here, CARD_DONE unsent: the adapter
                         * stays empty and the next link tries again (with "no
                         * file", it would send its formatted card over ours) */
                        if (card_upload() == 0) {
                            done.sent = 1;
                            g_card_note = "sent to the adapter";
                            base_set(g_card_name, crc);
                            snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", g_card_name);
                            g_card_rx_ok = 1;
                            send_msg(PSP2DC_MSG_CARD_DONE, &done, sizeof(done));
                        } else g_card_note = "could not send it";
                    } else {
                        g_card_note = "new, from the adapter";
                        snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", g_card_name);
                        g_card_rx_ok = 1;
                        send_msg(PSP2DC_MSG_CARD_DONE, &done, sizeof(done));
                    }
                } else if (!answered) {
                    card_decide();
                    settings_due = 1;
                }
            }
            /* back to the PSP's card once the adapter's has all come */
            if (g_return_to[0] && !g_switch_to[0] && (int)(now_us() - g_return_after_us) >= 0 &&
                g_card_blocks_in - g_dump_from >= PSP2DC_CARD_BLOCKS && !g_status.card_pending &&
                now_us() - g_card_last_us > CARD_QUIET_US) {
                card_eject(g_return_to, 1, 0);
                g_return_to[0] = 0;
            }
            if (g_base_due) { g_base_due = 0; settings_due = 1; }
            if (g_status_count && g_adapter_version != PSP2DC_PROTOCOL_VERSION && !version_told) {
                notice("update adapter firmware");   /* (both protocols: the status page) */
                version_told = 1;
            }
            in.seq = seq++;
            if (send_msg(PSP2DC_MSG_INPUT, &in, sizeof(in)) < 0) errors++;
            else sent++;
        } else {
            connected = 0;
            g_switch_to[0] = 0;
            /* Cable in and configured, but the adapter's sync does not come (or
             * is not taken): the PSP's USB is started afresh. PSPExp's runtime
             * does the same when a board's syncs go unanswered; here it was a
             * driver left deaf after many attach/detach rounds (2026-11-15). */
            if (!pspexpUsbAttached()) waiting_since = 0;
            else if (!waiting_since) waiting_since = now_us() | 1;
            else if (now_us() - waiting_since > 5000000) {
                pspexpUsbStop();
                sceKernelDelayThread(500000);
                usb_rc = usb_start();
                usb_restarts++;
                waiting_since = 0;
            }
        }
        g_usb_restarts = usb_restarts;
        if (settings_due) { settings_due = 0; settings_save(); }
        frame++;
        if (g_page == PAGE_STATUS) {
            if (frame % 6 == 0 || g_lcd_count != lcd_shown) { lcd_shown = g_lcd_count; show(usb_rc, &in, sent, errors); }
        } else if (g_page == PAGE_SAVES) {
            if (frame % 4 == 0) {          /* (the icons animate) */
                g_saves_count = ui_page_saves(g_card, g_card_have, g_saves_first, &ui_themes[g_theme],
                                              now_us() / 1000, top_line());
            }
        } else if (frame % 3 == 0 || g_lcd_count != lcd_shown) {   /* (the controller's state moves) */
            UiHome h;
            int up = !g_no_usb && pspexpUsbConnected();
            lcd_shown = g_lcd_count;
            memset(&h, 0, sizeof(h));
            h.lcd = g_lcd;
            h.link = demo_on() ? UI_LINK_DEMO : up ? UI_LINK_UP
                   : (!g_no_usb && pspexpUsbAttached()) ? UI_LINK_WAITING : UI_LINK_NONE;
            h.link_text = demo_on() ? "demo, no adapter" : up ? "connected to the adapter"
                        : g_no_usb ? "no USB: standalone" : h.link == UI_LINK_WAITING ? "cable in, waiting"
                        : "standalone";
            h.in = &in;
            h.card = g_card;
            h.card_have = g_card_have;
            h.card_note = g_card_note;
            h.card_name = g_card_name;
            h.notice = g_notice_at && now_us() - g_notice_at < 2000000 ? g_notice : NULL;
            /* the icons: the VMU's own when on its own; connected, ATTENTION
             * while the card is being written, as the manual has it (VMU-7:
             * "Attention ... lights for Read/Write while flash memory is
             * being accessed"), here for a second after a block came */
            h.icons = standalone ? vmu_icons
                    : (up && g_card_last_us && now_us() - g_card_last_us < 1000000) ? SA_ICON_ATTENTION : 0;
            h.vmu_pressed = standalone ? vmu_held : 0;
            h.standalone = standalone ? sa_mode_name() : NULL;
            h.now_ms = now_us() / 1000;
            ui_page_home(&h, &ui_themes[g_theme]);
        }
        if (autoshot) {   /* 3, 6 and 10 s after the start */
            unsigned t = (now_us() - start_us) / 1000000;
            if ((autoshot == 1 && t >= 3) || (autoshot == 2 && t >= 6) || (autoshot == 3 && t >= 10)) {
                shot_due = 1;
                autoshot = autoshot == 3 ? 0 : autoshot + 1;
            }
        }
        if (shot_due) { shot_due = 0; shot_take(); notice(shot_note()); }
    }
    if (rec_on()) rec_stop();
    if (!g_no_usb) pspexpUsbStop();
    sceKernelExitGame();
    return 0;
}
