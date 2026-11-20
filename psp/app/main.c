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
#include <psprtc.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "pspexp_usb_driver.h"
#include "../../protocol/psp2dc_protocol.h"
#include "tools.h"
#include "ui.h"
#include "vmu_sa.h"
#include "vmu_game.h"
#include "companion.h"
#include "cards.h"
#include "card_sync.h"
#include "cardfs.h"

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
static int g_title_due = 1;                /* a card's game's name changed (card_title) */
/* the card in use's disc game (cards_title), or NULL */
static const char *card_title(void)
{
    static char of[CARD_NAME_MAX], title[CARD_TITLE_MAX];
    if (g_title_due || strcmp(of, g_card_name)) {
        snprintf(of, sizeof(of), "%s", g_card_name);
        cards_title(g_card_name, title);
        g_title_due = 0;
    }
    return title[0] ? title : NULL;
}
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
static char g_session_backup[CARD_NAME_MAX];   /* the card backed up this session (the saver) */
static void saves_reset(void);             /* the Saves page's choice, forgotten when the card changes */
static const char *g_card_note = "new";

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
    if (cards_read(g_card_name, g_card)) { g_card_have = 1; g_card_note = "on the Memory Stick"; return; }
    if ((n = cards_list(names)) > 0 && cards_read(names[0], g_card)) {
        snprintf(g_card_name, sizeof(g_card_name), "%s", names[0]);
        g_card_have = 1; g_card_note = "on the Memory Stick";
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
        saves_reset();
        g_card_have = 1;
        g_card_note = "on the Memory Stick";
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
                /* the file as it was, kept once a session before it is first
                 * written (a session: the application's start, or a link) */
                if (strcasecmp(g_session_backup, g_card_name)) {
                    cards_backup(g_card_name, NULL, 1);
                    snprintf(g_session_backup, sizeof(g_session_backup), "%s", g_card_name);
                }
                if (cards_write(g_card_name, snap) < 0) { g_card_write_err++; g_card_dirty = 1; g_card_last_us = now_us(); }
                else {
                    g_card_writes++; g_card_note = "saved";
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
enum { PAGE_VMU, PAGE_SAVES, PAGE_CARDS, PAGE_SYSTEM, PAGES };
static int g_page = PAGE_VMU, g_theme, g_saves_first, g_saves_count;
static int g_per_game = 1;                /* openMenu's Game ID puts that game's own card in */
/* The Dreamcast keyboard (keyboard.c): on (PSP2DC_KBD_ONLY) or off as the
 * System setting has it; while on, playing is typing. */
static int g_kbd_mode, g_typing;
static Kb g_kb;
static Psp2dcGameId g_gid;                /* the last Game ID from the adapter (reader thread) */
static volatile unsigned g_gid_seq;
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

/* the USB started afresh away from the main loop (see there) */
static volatile int g_usb_restart_req, g_usb_rc;
static int usb_restart_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    while (g_running) {
        sceKernelDelayThread(100000);
        if (!g_usb_restart_req) continue;
        if (pspexpUsbConnected() || !pspexpUsbAttached()) { g_usb_restart_req = 0; continue; }   /* (a link came meanwhile) */
        pspexpUsbStop();
        sceKernelDelayThread(500000);
        g_usb_rc = usb_start();
        g_usb_restart_req = 0;
    }
    return 0;
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
    static unsigned char stream[4096];   /* (a transfer: a screen and the companion's bytes, 1280 at most) */
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
            if (p[2] == PSP2DC_MSG_STATUS && (p[3] == sizeof(Psp2dcStatus) || p[3] == PSP2DC_STATUS_V6_LEN ||
                                              p[3] == PSP2DC_STATUS_V5_LEN || p[3] == PSP2DC_STATUS_OLD_LEN)) {
                Psp2dcStatus st;
                memset(&st, 0, sizeof(st));
                memcpy(&st, p + PSP2DC_HEADER_LEN, p[3]);
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
            } else if (p[2] == PSP2DC_MSG_GAME_ID && p[3] == sizeof(Psp2dcGameId)) {
                memcpy(&g_gid, p + PSP2DC_HEADER_LEN, sizeof(g_gid));   /* (acted on by the main loop) */
                g_gid_seq++;
            } else if (p[2] == PSP2DC_MSG_COMP_RESET) {
                comp_reset();
            } else if (p[2] == PSP2DC_MSG_COMP_DATA) {
                comp_feed(p + PSP2DC_HEADER_LEN, p[3]);
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

/* how long the main loop's turns take: the longest, and how many went over
 * 20 ms, in the last five seconds (System, Details) */
static unsigned g_loop_max_us, g_loop_slow, g_loop_shown_max_us, g_loop_shown_slow, g_loop_window_us;
static void details_view_rows(int standalone);
static void show(int usb_rc, const Psp2dcInput *in, unsigned sent, unsigned errors, int standalone)
{
    PspExpUsbStats st;
    const char *link;
    memset(&st, 0, sizeof(st));
    if (!g_no_usb) pspexpUsbGetStats(&st);
    ui_begin(0xFF000000);
    ui_header(3, &ui_themes[g_theme]);
    ui_text_margin(6);                     /* (the menu's frame covered the first letters) */
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    link = g_no_usb ? "no USB (emulator?)            "
         : pspexpUsbConnected() ? "CONNECTED to the adapter      "
         : pspexpUsbAttached()  ? "cable in, waiting for adapter "
         :                        "waiting for the USB cable     ";
    pspDebugScreenSetXY(0, 3);
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
    printf("maple    bad %u  unanswered %u  late %u  card off %u      \n", g_status.maple_bad,
           g_status.maple_unanswered, g_status.maple_skipped, g_status.vmu_drops);
    printf("         replies over 1 ms: %u; longest in the last second %u us      \n", g_status.maple_slow, g_status.maple_max_us);
    if (g_status.maple_unanswered)
        printf("         last unanswered: cmd %02X to %02X, %u words, func %08X  \n", g_status.last_cmd,
               g_status.last_dest, g_status.last_words, (unsigned)g_status.last_func);
    if (g_comp.sessions || g_status.comp_cmds)
        printf("companion %u commands, %u again, %u bytes; here %u bytes, %u bad    \n", g_status.comp_cmds,
               g_status.comp_agains, (unsigned)g_status.comp_bytes, g_comp.bytes, g_comp.test_bad + g_comp.stream_bad);
    if (g_kbd_mode || g_status.kbd_mode)
        printf("keyboard mode %u here, %u on the adapter; read %u times; LEDs set %02X      \n", g_kbd_mode,
               g_status.kbd_mode, g_status.kbd_reads, g_status.kbd_leds);
    printf("vmu      %u screens here; adapter: asked %u wrote %u took %u\n", g_lcd_count,
           g_status.lcd_media, g_status.lcd_writes, g_status.lcd_screens);
    printf("card     %s; %s%u to come      \n", g_card_note,
           (g_status.flags & PSP2DC_STATUS_CARD) ? "in the Dreamcast; " : "not shown yet; ", g_status.card_pending);
    printf("         %u blocks kept, file written %u times, %u errors      \n",
           g_card_blocks_in, g_card_writes, g_card_write_err);
    printf("draw     %u.%u ms a frame, longest %u.%u ms; VMU CPU %u.%u ms      \n", ui_draw_us() / 1000,
           ui_draw_us() / 100 % 10, ui_draw_max_us() / 1000, ui_draw_max_us() / 100 % 10,
           game_frame_us() / 1000, game_frame_us() / 100 % 10);
    printf("loop     longest %u ms in the last 5 s; over 20 ms: %u      \n",
           g_loop_shown_max_us / 1000, g_loop_shown_slow);
    printf("tools    %-44s\n", demo_on() ? demo_note() : rec_on() ? "RECORDING the VMU's screens"
                                  : rec_note()[0] ? rec_note() : "recording off, demo off");
    printf("shots    %-44s\n", shot_note()[0] ? shot_note() : "none yet (triangle, in the menu)");
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
    /* (no VMU screen here: Details is in the menu, where the VMU is not
     * played, and it covered the lines; the VMU tab shows it) */
    ui_text_margin(0);                     /* (the panels and keys line up as elsewhere) */
    details_view_rows(standalone);
    ui_keys(UI_KEYS_DETAILS, &ui_themes[g_theme]);
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
    if ((p = strstr(buf, "pergame="))) g_per_game = atoi(p + 8) != 0;
    if ((p = strstr(buf, "keyboard="))) g_kbd_mode = atoi(p + 9) ? PSP2DC_KBD_ONLY : PSP2DC_KBD_OFF;
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
    int n = snprintf(buf, sizeof(buf), "page=%d\ntheme=%d\npergame=%d\nkeyboard=%d\ncard=%s\n", g_page, g_theme, g_per_game,
                     g_kbd_mode, g_card_name);
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
/* The companion is in use: its bytes came in the last three seconds. */
static int comp_on(void) { return g_comp.last_us && now_us() - g_comp.last_us < 3000000; }
/* ms0:/PSP2DC/dev_companion.txt, for seeing the companion's page without a
 * Dreamcast: its lines as PRINT records, row by row, once at the start and
 * kept as if still arriving. Absent, nothing. */
static int g_dev_comp;
static void dev_companion_load(void)
{
    char buf[1024], *line, *save;
    unsigned char r[4 + COMP_COLS];
    SceUID fd = sceIoOpen("ms0:/PSP2DC/dev_companion.txt", PSP_O_RDONLY, 0);
    int n, row = 0;
    if (fd < 0) return;
    n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = 0;
    comp_reset();
    for (line = strtok_r(buf, "\n", &save); line && row < COMP_ROWS; line = strtok_r(NULL, "\n", &save), row++) {
        int len = (int)strlen(line) < COMP_COLS ? (int)strlen(line) : COMP_COLS;
        r[0] = PSP2DC_REC_PRINT; r[1] = (unsigned char)(len + 1); r[2] = 0; r[3] = (unsigned char)row;
        memcpy(r + 4, line, len);
        comp_feed(r, 4 + len);
    }
    g_dev_comp = 1;
}

/* ms0:/PSP2DC/dev_gameid.txt, for trying Card per game without openMenu:
 * lines "<ms after the start> <ID> [name]", each taken as openMenu's Game ID
 * at that time. Absent, nothing. */
static struct { unsigned ms; char id[12], name[128]; } g_dev_gid[8];
static int g_dev_gid_n, g_dev_gid_i;
static void dev_gameid_load(void)
{
    char buf[1024], *line, *save;
    SceUID fd = sceIoOpen("ms0:/PSP2DC/dev_gameid.txt", PSP_O_RDONLY, 0);
    int n;
    if (fd < 0) return;
    n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = 0;
    for (line = strtok_r(buf, "\n", &save); line && g_dev_gid_n < 8; line = strtok_r(NULL, "\n", &save)) {
        char id[16] = "", *name = NULL;
        unsigned ms;
        if (sscanf(line, "%u %15s", &ms, id) < 2) continue;
        name = strstr(line, id) + strlen(id);
        while (*name == ' ') name++;
        memset(&g_dev_gid[g_dev_gid_n], 0, sizeof(g_dev_gid[0]));
        g_dev_gid[g_dev_gid_n].ms = ms;
        memcpy(g_dev_gid[g_dev_gid_n].id, id, strlen(id) < 12 ? strlen(id) : 12);
        snprintf(g_dev_gid[g_dev_gid_n].name, 128, "%s", name);
        g_dev_gid_n++;
    }
}

#define DEV_STEPS 256
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
    static char buf[8192];
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

/* ---- the Saves and Cards pages, standalone ------------------------------
 * A save, or a card, chosen; a box of what to do with it. Not connected: the
 * Dreamcast has the card then. Not while a VMU game runs: it holds its own
 * copy of the card, which it writes back when it ends. Before anything that
 * takes something off a card, the card is backed up (cards_backup, -auto).
 *
 * The box is one at a time (g_box): what it is about (a save or a card),
 * what it shows, and what each choice does. */
static int g_alone = 1;                    /* standalone this frame (main loop) */

/* The buttons, read by a thread of their own at every controller sample: a
 * press is kept until the main loop takes it, so a tap made while a frame is
 * slow (a file written, a page drawn) still counts. The main loop reading the
 * pad itself lost quick taps on hardware (2026-11-16, MODE on the VMU). */
static SceCtrlData g_in_now;               /* the latest sample */
static unsigned g_in_made;                 /* buttons pressed since the main loop last looked */
static unsigned g_in_broke;                /* ... and let go since then (a second tap is a new press) */
static SceUID g_in_lock = -1;
static volatile int g_in_running = 1;
static int input_thread(SceSize args, void *argp)
{
    SceCtrlData s;
    unsigned prev = 0;
    (void)args; (void)argp;
    while (g_in_running) {
        sceCtrlReadBufferPositive(&s, 1);  /* waits for the next sample */
        sceKernelWaitSema(g_in_lock, 1, NULL);
        g_in_made |= s.Buttons & ~prev;
        g_in_broke |= prev & ~s.Buttons;
        prev = s.Buttons;
        g_in_now = s;
        sceKernelSignalSema(g_in_lock, 1);
    }
    return 0;
}
/* this frame's pad: the latest sample, with every button pressed since the
 * last call held too (a tap already let go counts for one frame) */
static void input_take(SceCtrlData *pad, unsigned *broke)
{
    sceKernelWaitSema(g_in_lock, 1, NULL);
    *pad = g_in_now;
    pad->Buttons |= g_in_made;
    *broke = g_in_broke;
    g_in_made = g_in_broke = 0;
    sceKernelSignalSema(g_in_lock, 1);
}


static int g_menu;                         /* SELECT: the menu (1) or playing (0) */
enum { BOX_NONE, BOX_SAVE_ACTIONS, BOX_CARD_LIST, BOX_SAVE_DELETE, BOX_RESULT, BOX_INFO, BOX_IMPORT,
       BOX_CARD_ACTIONS, BOX_RENAME, BOX_RESTORE, BOX_RESTORE_ASK, BOX_REPAIR, BOX_CARD_DELETE,
       BOX_IMPORT_REPLACE };
enum { SA_INFO, SA_COPY, SA_MOVE, SA_EXPORT, SA_DELETE, SAS };
enum { CA_USE, CA_IMPORT, CA_RENAME, CA_DUPLICATE, CA_BACKUP, CA_RESTORE, CA_DEFRAG, CA_CHECK, CA_DELETE, CAS };
static int g_box, g_box_item, g_box_move;
static char g_box_title[48], g_box_line[8][64];
static int g_box_lines;
static int g_sv_sel;                       /* the Saves page's chosen save */
static CardFile g_sv_file;                 /* the save the box is about */
static char g_sv_cards[CARDS_MAX][CARD_NAME_MAX];
static int g_sv_ncards;
static int g_cd_sel, g_cd_first;           /* the Cards page: row 0 "New card", 1 + i card i */
static char g_cd_names[CARDS_MAX][CARD_NAME_MAX];
static int g_cd_n, g_cd_saves[CARDS_MAX], g_cd_free[CARDS_MAX];
static char g_cd_game[CARDS_MAX][13];
static char g_cd_title[CARDS_MAX][CARD_TITLE_MAX];   /* each card's disc game (cards_title) */
static int g_cd_due = 1;                   /* the cards' summaries to be read again */
static char g_box_card[CARD_NAME_MAX];     /* the card the box is about */
static CardImport g_imports[UI_BOX_ITEMS];
static int g_nimports;
static CardBackup g_backups[BACKUPS_MAX];
static int g_nbackups;
static char g_edit[CARD_NAME_MAX];
static int g_edit_key;                     /* the Rename keyboard's key chosen (ui.h) */
static int g_box_back;                     /* the page the box returns to: 0 Saves, 1 Cards */
static int g_restore;                      /* the backup chosen */
static unsigned short g_picture[72 * 56];  /* the save's large picture (Info), when it has one */
static int g_has_picture;

static void saves_reset(void) { g_sv_sel = g_box = g_box_item = 0; g_saves_first = 0; g_cd_due = 1; }

static void box_lines(const char *title, const char *a, const char *b, const char *c)
{
    snprintf(g_box_title, sizeof(g_box_title), "%s", title);
    g_box_lines = 0;
    if (a) snprintf(g_box_line[g_box_lines++], sizeof(g_box_line[0]), "%s", a);
    if (b) snprintf(g_box_line[g_box_lines++], sizeof(g_box_line[0]), "%s", b);
    if (c) snprintf(g_box_line[g_box_lines++], sizeof(g_box_line[0]), "%s", c);
}
/* what came of an action: a few words in the middle of the screen for two
 * seconds (any button hides them), the box closed. In the header alone they
 * went unseen (2026-11-16: "I press X and nothing shows"). */
static char g_toast[48];
static unsigned g_toast_at;
static void say(const char *fmt, ...)
{
    char buf[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    snprintf(g_toast, sizeof(g_toast), "%s", buf);   /* (in the middle of the screen, 2 s) */
    g_toast_at = now_us() | 1;
    g_box = BOX_NONE;
}

/* a save's box: its description as the title (the file name when it has none) */
static const char *save_title(void)
{
    static char t[48];
    CardInfo in;
    if (g_card_have && cardfs_info(g_card, &g_sv_file, &in) && in.dc_desc[0]) snprintf(t, sizeof(t), "%s", in.dc_desc);
    else snprintf(t, sizeof(t), "%s", g_sv_file.name);
    return t;
}

/* the save the box is about, as the card now has it (0: gone) */
static int saves_file(CardFile *f)
{
    return g_card_have && cardfs_find(g_card, g_sv_file.off, f) && !strcmp(f->name, g_sv_file.name);
}

/* A card to change: the one in use is g_card (copied out), another its file. */
static int card_get(const char *name, unsigned char *img)
{
    if (g_card_have && !strcasecmp(name, g_card_name)) {
        card_lock(); memcpy(img, g_card, PSP2DC_CARD_BYTES); card_unlock();
        return 1;
    }
    return cards_read(name, img);
}
/* ... and put back: g_card (written by the saver) or the file. 0 or < 0. */
static int card_put(const char *name, const unsigned char *img, const char *note)
{
    if (g_card_have && !strcasecmp(name, g_card_name)) {
        card_lock();
        memcpy(g_card, img, PSP2DC_CARD_BYTES);
        g_card_dirty = 1;
        g_card_last_us = now_us();
        g_card_note = note;
        card_unlock();
        return 0;
    }
    return cards_write(name, img);
}
/* a card as it is now, kept before something is taken off it */
static void card_keep(const char *name)
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    if (card_get(name, img)) cards_backup(name, img, 1);
}

/* the save is on that card's file, the same bytes as here (read back) */
static int saves_landed(const char *to, const CardFile *f)
{
    static unsigned char back[PSP2DC_CARD_BYTES], a[200 * 512], b[200 * 512];
    static CardFile there[200];
    int n, i, la, lb;
    if (!cards_read(to, back)) return 0;
    n = cardfs_list(back, there, 200);
    for (i = 0; i < n; i++)
        if (!strcmp(there[i].name, f->name)) {
            la = cardfs_export_vms(g_card, f, a);
            lb = cardfs_export_vms(back, &there[i], b);
            return la > 0 && la == lb && !memcmp(a, b, la);
        }
    return 0;
}

/* Copy, or move: copy, read back, and only then delete it here */
static void saves_copy(const char *to, int move)
{
    static unsigned char other[PSP2DC_CARD_BYTES];
    CardFile f;
    int rc;
    if (!saves_file(&f) || !cards_read(to, other)) { say(move ? "move failed" : "copy failed"); return; }
    rc = cardfs_copy(g_card, &f, other);
    if (rc == CARDFS_OK && cards_write(to, other) < 0) rc = CARDFS_FAIL;
    if (rc == CARDFS_OK && move && !saves_landed(to, &f)) rc = CARDFS_FAIL;
    if (rc == CARDFS_OK && move) {
        card_keep(g_card_name);
        card_lock();
        cardfs_delete(g_card, &f);
        g_card_dirty = 1;
        g_card_last_us = now_us();
        g_card_note = "save moved";
        card_unlock();
        say("moved to %s", to);
    } else if (rc == CARDFS_OK) say("copied to %s", to);
    else if (rc == CARDFS_SAME) say("%s has it already", to);
    else if (rc == CARDFS_ROOM && f.type == 0xCC && cardfs_has_game(other)) say("%s has a game", to);
    else if (rc == CARDFS_ROOM) say("no room on %s", to);
    else say(move ? "move failed" : "copy failed");
    g_cd_due = 1;
}

static void saves_info(void)
{
    CardFile f;
    CardInfo in;
    static const char *const pic[4] = { "none", "16-bit colour", "256 colours", "16 colours" };
    char a[64], b[64], c[64], d[64], e[64], g[64];
    if (!saves_file(&f) || !cardfs_info(g_card, &f, &in)) { say("no information"); return; }
    g_has_picture = cardfs_eyecatch(g_card, &f, g_picture) > 0;
    snprintf(g_box_title, sizeof(g_box_title), "%s", in.dc_desc[0] ? in.dc_desc : f.name);
    snprintf(a, sizeof(a), "File: %s", f.name);
    snprintf(b, sizeof(b), "On the VMU: %s", in.vms_desc);
    snprintf(c, sizeof(c), "%s, %d block%s%s", f.type == 0xCC ? "a game" : "a save", f.size, f.size == 1 ? "" : "s",
             f.prot ? ", copy-protected" : "");
    snprintf(d, sizeof(d), "Saved %02x%02x-%02x-%02x %02x:%02x", f.date[0], f.date[1], f.date[2], f.date[3], f.date[4], f.date[5]);
    snprintf(e, sizeof(e), "%d icon%s, large picture: %s", in.icons, in.icons == 1 ? "" : "s", pic[in.eyecatch & 3]);
    if (in.crc_computed < 0) snprintf(g, sizeof(g), "Check: %s", f.type == 0xCC ? "games carry none" : "cannot be made");
    else if (in.crc_computed == in.crc_stored) snprintf(g, sizeof(g), "Check: good (CRC %04X)", in.crc_stored);
    else snprintf(g, sizeof(g), "Check: DAMAGED (CRC %04X, should be %04X)", in.crc_computed, in.crc_stored);
    g_box_lines = 0;
    snprintf(g_box_line[g_box_lines++], 64, "%s", a);
    snprintf(g_box_line[g_box_lines++], 64, "%s", b);
    if (in.app[0]) snprintf(g_box_line[g_box_lines++], 64, "Made by: %s", in.app);
    snprintf(g_box_line[g_box_lines++], 64, "%s", c);
    snprintf(g_box_line[g_box_lines++], 64, "%s", d);
    snprintf(g_box_line[g_box_lines++], 64, "%s", e);
    snprintf(g_box_line[g_box_lines++], 64, "%s", g);
    g_box = BOX_INFO;
}

/* the saves to import, listed in the box */
static void import_list(const char *into)
{
    g_nimports = cards_imports(g_imports, UI_BOX_ITEMS);
    if (into != g_box_card) snprintf(g_box_card, sizeof(g_box_card), "%s", into);   /* (the Cards page passes it) */
    if (!g_nimports) { say("nothing in PSP2DC/saves"); return; }
    snprintf(g_box_title, sizeof(g_box_title), "Import into %s", into);
    g_box_lines = 0;
    g_box = BOX_IMPORT;
    g_box_item = 0;
}

/* The card with a new game in place of its old one, ready once the player
 * says yes (BOX_IMPORT_REPLACE): made first, so it is asked only when the
 * import would then go through. */
static unsigned char g_replace_img[PSP2DC_CARD_BYTES];
static int g_replace_from;                 /* the import list's line, to go back to */

static int import_into(unsigned char *img, const CardImport *im, const unsigned char *data, int len,
                       const unsigned char *vmi)
{
    unsigned char date[8];
    char name[13];
    ScePspDateTime t;
    int i, k;
    if (im->dci) return cardfs_import_dci(img, data, len);
    /* without a VMI: named after the file, dated now */
    for (i = k = 0; im->file[i] && im->file[i] != '.' && k < 12; i++) {
        char ch = im->file[i];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        name[k++] = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ? ch : '_';
    }
    name[k] = 0;
    memset(&t, 0, sizeof(t));
    sceRtcGetCurrentClockLocalTime(&t);
    date[0] = (unsigned char)((t.year / 1000) << 4 | (t.year / 100) % 10);
    date[1] = (unsigned char)(((t.year / 10) % 10) << 4 | t.year % 10);
    date[2] = (unsigned char)((t.month / 10) << 4 | t.month % 10);
    date[3] = (unsigned char)((t.day / 10) << 4 | t.day % 10);
    date[4] = (unsigned char)((t.hour / 10) << 4 | t.hour % 10);
    date[5] = (unsigned char)((t.minute / 10) << 4 | t.minute % 10);
    date[6] = (unsigned char)((t.second / 10) << 4 | t.second % 10);
    date[7] = (unsigned char)((sceRtcGetDayOfWeek(t.year, t.month, t.day) + 6) % 7);   /* 0 Monday */
    return cardfs_import_vms(img, data, len, im->vmi ? vmi : NULL, name, date);
}

static void import_one(const CardImport *im)
{
    static unsigned char img[PSP2DC_CARD_BYTES], data[32 + 200 * 512];
    static CardFile f[200];
    unsigned char vmi[108];
    int len, rc, n, k;
    if (!card_get(g_box_card, img) || (len = cards_import_read(im, data, sizeof(data), vmi)) <= 0) { say("import failed"); return; }
    memcpy(g_replace_img, img, sizeof(img));
    rc = import_into(img, im, data, len, vmi);
    if (rc == CARDFS_ROOM && cardfs_has_game(g_replace_img)) {
        /* a card holds one game: try it with the card's game taken off, and
         * if it then goes in, ask before replacing it */
        n = cardfs_list(g_replace_img, f, 200);
        for (k = 0; k < n && f[k].type != 0xCC; k++) ;
        if (k < n) {
            char title[48];
            cardfs_delete(g_replace_img, &f[k]);
            /* asked only when what goes in is a game: the card's game was
             * taken off, so a game there now is the one imported (no room for
             * a save is not a reason to take the game off) */
            if (import_into(g_replace_img, im, data, len, vmi) == CARDFS_OK && cardfs_has_game(g_replace_img)) {
                int e = (int)strlen(f[k].name);
                while (e > 0 && f[k].name[e - 1] == ' ') e--;   /* (names are padded with spaces) */
                snprintf(title, sizeof(title), "Replace the game %.*s?", e, f[k].name);
                g_replace_from = g_box_item;
                box_lines(title, NULL, NULL, NULL);
                g_box = BOX_IMPORT_REPLACE;
                g_box_item = 0;            /* No first */
                return;
            }
        }
    }
    if (rc == CARDFS_OK && card_put(g_box_card, img, "save imported") == 0) say("imported into %s", g_box_card);
    else if (rc == CARDFS_SAME) say("%s has it already", g_box_card);
    else if (rc == CARDFS_ROOM) say("no room on %s", g_box_card);
    else say("not a save file");
    g_cd_due = 1;
}

/* ---- the Saves page ---- */
static void cards_summary(void);
static unsigned card_off(void);
/* a save's actions that cannot be done now (greyed): connected, or while a
 * VMU game runs, the card is not ours to change; copy-protected saves stay */
static unsigned save_off(void)
{
    unsigned off = 0;
    if (g_cd_due) cards_summary();
    if (g_sv_file.prot || g_cd_n < 2) off |= 1u << SA_COPY | 1u << SA_MOVE;
    if (!g_alone || game_running()) off |= 1u << SA_COPY | 1u << SA_MOVE | 1u << SA_EXPORT | 1u << SA_DELETE;
    return off;
}

/* a box that lists files or cards: how long the list is (0: not a list) */
static int box_list_len(void)
{
    return g_box == BOX_IMPORT ? g_nimports : g_box == BOX_CARD_LIST ? g_sv_ncards
         : g_box == BOX_RESTORE ? g_nbackups : 0;
}
/* L / R in a list: a page (nine) up or down */
static void box_page(int dir)
{
    int n = box_list_len();
    if (n <= 0) return;
    g_box_item += dir * 9;
    if (g_box_item > n - 1) g_box_item = n - 1;
    if (g_box_item < 0) g_box_item = 0;
}

/* up or down a box's choices, over the greyed ones (-1: none to choose) */
static void box_move(int dir, int n, unsigned off)
{
    int k;
    if ((off & ((1u << n) - 1)) == (1u << n) - 1) { g_box_item = -1; return; }
    for (k = 0; k < n; k++) {
        if (g_box_item < 0) g_box_item = dir > 0 ? 0 : n - 1;
        else g_box_item = (g_box_item + (dir > 0 ? 1 : n - 1)) % n;
        if (!((off >> g_box_item) & 1)) return;
    }
}

static void saves_input(unsigned pressed)
{
    static CardFile f[200];
    int n = g_card_have ? cardfs_list(g_card, f, 200) : 0;
    if (g_sv_sel >= n) g_sv_sel = n ? n - 1 : 0;
    g_box_back = 0;
    switch (g_box) {
    case BOX_NONE:
        if ((pressed & PSP_CTRL_DOWN) && g_sv_sel < n - 1) g_sv_sel++;
        if ((pressed & PSP_CTRL_UP) && g_sv_sel > 0) g_sv_sel--;
        if (g_sv_sel < g_saves_first) g_saves_first = g_sv_sel;
        if (g_sv_sel > g_saves_first + 2) g_saves_first = g_sv_sel - 2;
        if ((pressed & PSP_CTRL_CROSS) && n) {
            g_sv_file = f[g_sv_sel];
            snprintf(g_box_title, sizeof(g_box_title), "%s", save_title());
            g_box_lines = 0;
            g_box = BOX_SAVE_ACTIONS;
            g_box_item = SA_INFO;
        }
        break;
    case BOX_SAVE_ACTIONS: {               /* a copy-protected save cannot be copied or moved, as on the VMU */
        unsigned off = save_off();
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) box_move(pressed & PSP_CTRL_DOWN ? 1 : -1, SAS, off);
        if (pressed & PSP_CTRL_CIRCLE) g_box = BOX_NONE;
        else if ((pressed & PSP_CTRL_CROSS) && g_box_item >= 0 && !((off >> g_box_item) & 1)) {
            if (g_box_item == SA_INFO) saves_info();
            else if (g_box_item == SA_COPY || g_box_item == SA_MOVE) {
                char names[CARDS_MAX][CARD_NAME_MAX];
                int m = cards_list(names), k;
                g_box_move = g_box_item == SA_MOVE;
                for (k = g_sv_ncards = 0; k < m; k++)
                    if (strcasecmp(names[k], g_card_name)) memcpy(g_sv_cards[g_sv_ncards++], names[k], CARD_NAME_MAX);
                if (!g_sv_ncards) say("no other card");
                else { box_lines(g_box_move ? "Move to" : "Copy to", NULL, NULL, NULL); g_box = BOX_CARD_LIST; g_box_item = 0; }
            } else if (g_box_item == SA_EXPORT) {
                CardFile cf;
                char base[9];
                if (saves_file(&cf) && cards_export(g_card, &cf, base) == 0) say("exported as %s", base);
                else say("export failed");
            } else { box_lines("Delete this save?", NULL, NULL, NULL); g_box = BOX_SAVE_DELETE; g_box_item = 0; }
        }
        break;
    }
    case BOX_CARD_LIST:
        if (pressed & PSP_CTRL_DOWN) g_box_item = (g_box_item + 1) % g_sv_ncards;
        if (pressed & PSP_CTRL_UP) g_box_item = (g_box_item + g_sv_ncards - 1) % g_sv_ncards;
        if (pressed & PSP_CTRL_CIRCLE) { box_lines(save_title(), NULL, NULL, NULL); g_box = BOX_SAVE_ACTIONS; g_box_item = g_box_move ? SA_MOVE : SA_COPY; }
        else if (pressed & PSP_CTRL_CROSS) saves_copy(g_sv_cards[g_box_item], g_box_move);
        break;
    case BOX_SAVE_DELETE:                  /* No first */
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) g_box_item = !g_box_item;
        if ((pressed & PSP_CTRL_CIRCLE) || ((pressed & PSP_CTRL_CROSS) && !g_box_item)) {
            box_lines(save_title(), NULL, NULL, NULL); g_box = BOX_SAVE_ACTIONS; g_box_item = SA_DELETE;
        } else if (pressed & PSP_CTRL_CROSS) {
            CardFile cf;
            if (!saves_file(&cf)) { say("delete failed"); break; }
            card_keep(g_card_name);
            card_lock();
            cardfs_delete(g_card, &cf);
            g_card_dirty = 1;
            g_card_last_us = now_us();
            g_card_note = "save deleted";
            card_unlock();
            say("save deleted");
            g_cd_due = 1;
        }
        break;
    case BOX_IMPORT:
        if (pressed & PSP_CTRL_DOWN) g_box_item = (g_box_item + 1) % g_nimports;
        if (pressed & PSP_CTRL_UP) g_box_item = (g_box_item + g_nimports - 1) % g_nimports;
        if (pressed & PSP_CTRL_CIRCLE) g_box = BOX_NONE;
        else if (pressed & PSP_CTRL_CROSS) import_one(&g_imports[g_box_item]);
        break;
    default:                               /* a result, the info */
        if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) g_box = BOX_NONE;
        break;
    }
}

/* the import list's lines: each file, and what it holds */
static const char *import_item(int k)
{
    static char lines[UI_BOX_ITEMS][64];
    const CardImport *im = &g_imports[k];
    long blocks = ((im->dci ? im->size - 32 : im->size) + 511) / 512;
    int stem = (int)strlen(im->file) - 4;  /* (the name without .VMS / .DCI) */
    snprintf(lines[k], 64, "%-31.*s %-7s %3ld", stem < 31 ? stem : 31, im->file,
             im->dci ? "DCI" : im->vmi ? "VMS+VMI" : "VMS", blocks);
    return lines[k];
}

static void box_view(UiBox *b)
{
    static const char *const sacts[SAS] = { "Info", "Copy to another card", "Move to another card",
                                            "Export to the Memory Stick", "Delete" };
    static const char *const cacts[CAS] = { "Use this card", "Import a save into it", "Rename", "Duplicate",
                                            "Back up now", "Restore a backup", "Defragment", "Check and repair",
                                            "Delete this card" };
    static char backup_lines[BACKUPS_MAX][64];
    int k;
    memset(b, 0, sizeof(*b));
    if (g_box == BOX_NONE) return;
    b->title = g_box_title;
    b->item = g_box_item;
    for (k = 0; k < g_box_lines && k < 8; k++) b->lines[k] = g_box_line[k];
    switch (g_box) {
    case BOX_SAVE_ACTIONS:
        for (k = 0; k < SAS; k++) b->items[k] = sacts[k];
        b->nitems = SAS;
        b->sep_items = 1u << SA_COPY | 1u << SA_DELETE;
        b->danger_items = 1u << SA_DELETE;
        b->off_items = save_off();
        break;
    case BOX_CARD_LIST:
        for (k = 0; k < g_sv_ncards && k < UI_BOX_ITEMS; k++) b->items[k] = g_sv_cards[k];
        b->nitems = k;
        break;
    case BOX_SAVE_DELETE: case BOX_CARD_DELETE: case BOX_RESTORE_ASK: case BOX_IMPORT_REPLACE:
        b->items[0] = "No, keep it";
        b->items[1] = g_box == BOX_RESTORE_ASK ? "Yes, restore it" : g_box == BOX_IMPORT_REPLACE ? "Yes, replace it"
                    : "Yes, delete it";
        b->nitems = 2;
        b->danger_items = 1u << 1;
        break;
    case BOX_IMPORT:
        for (k = 0; k < g_nimports && k < UI_BOX_ITEMS; k++) b->items[k] = import_item(k);
        b->nitems = k;
        break;
    case BOX_CARD_ACTIONS:
        for (k = 0; k < CAS; k++) b->items[k] = cacts[k];
        b->nitems = CAS;
        b->sep_items = 1u << CA_RENAME | 1u << CA_BACKUP | 1u << CA_DEFRAG | 1u << CA_DELETE;
        b->danger_items = 1u << CA_DELETE;
        b->off_items = card_off();
        break;
    case BOX_RESTORE:
        for (k = 0; k < g_nbackups && k < UI_BOX_ITEMS; k++) {
            snprintf(backup_lines[k], 64, "%-14.14s %s%s", g_backups[k].card, g_backups[k].when,
                     g_backups[k].automatic ? " auto" : "");
            b->items[k] = backup_lines[k];
        }
        b->nitems = k;
        break;
    case BOX_REPAIR:
        b->items[0] = "Repair it";
        b->items[1] = "Leave it";
        b->nitems = 2;
        break;
    case BOX_INFO:
        if (g_has_picture) b->picture = g_picture;
        break;
    case BOX_RENAME:
        b->edit = g_edit;
        b->key = g_edit_key;
        break;
    }
}

static void saves_view(UiSaves *v, int standalone)
{
    memset(v, 0, sizeof(*v));
    if (g_saves_first > g_saves_count - 3) g_saves_first = g_saves_count - 3;
    if (g_saves_first < 0) g_saves_first = 0;
    v->first = g_saves_first;
    v->sel = g_sv_sel;
    (void)standalone;
    v->keys = g_box == BOX_NONE ? UI_KEYS_SAVES : (g_box == BOX_RESULT || g_box == BOX_INFO) ? UI_KEYS_OK
            : box_list_len() > 9 ? UI_KEYS_LIST : UI_KEYS_MENU;
    box_view(&v->box);
}

/* ---- the Cards page ---- */

/* each card's saves, free blocks and game, read once when they may have changed */
static void cards_summary(void)
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    static CardFile f[200];
    int i, k, n;
    g_cd_n = cards_list(g_cd_names);
    for (i = 0; i < g_cd_n; i++) {
        g_cd_saves[i] = g_cd_free[i] = 0;
        g_cd_game[i][0] = 0;
        cards_title(g_cd_names[i], g_cd_title[i]);
        if (!card_get(g_cd_names[i], img)) continue;
        n = cardfs_list(img, f, 200);
        g_cd_saves[i] = n;
        g_cd_free[i] = cardfs_free(img);
        for (k = 0; k < n; k++) if (f[k].type == 0xCC) { memcpy(g_cd_game[i], f[k].name, 13); }
    }
    g_cd_due = 0;
}

static int card_use(const char *name)    /* (the caller says what came of it) */
{
    return card_change(name) < 0 ? -1 : 0;
}

static void card_eject(const char *to, int automatic, int discard);

/* The card in use changed to another (the Cards page, either mode).
 * Standalone at once: 0, or -1 if it could not be read. Connected the
 * adapter lets go of its card first, once every block has reached us, and
 * the change happens on its answer: 1 begun, -2 busy (try again). */
static int card_select(const char *to)
{
    if (!g_no_usb && pspexpUsbConnected()) {
        if (g_switch_to[0] || g_return_to[0] || g_status.card_pending || g_card_dirty ||
            now_us() - g_card_last_us < 2000000) return -2;
        card_eject(to, 0, 0);
        return 1;
    }
    g_return_to[0] = 0;                    /* the player chose: nothing to go back to */
    return card_use(to);
}

/* what became of a card_select, in the header */
static void card_selected(int rc, const char *name)
{
    if (rc == 0) say("%s in use", name);
    else if (rc == 1) say("%s going in", name);
    else if (rc == -2) say("card busy, try again");
    else say("could not read %s", name);
}

/* openMenu's Game ID: that game's own card put in (Card per game, System).
 * The card is named after the disc's product number ("T-8101N" -> t-8101n),
 * made empty the first time; the change goes as changing card by hand does
 * (connected: the adapter lets the card go first), tried again for ten
 * seconds while the card is busy. openMenu's own ID at its start is left.
 * The game's name, when openMenu sends one, is kept beside its card. */
static char g_gid_card[CARD_NAME_MAX];
static unsigned g_gid_until;
static void game_id_got(const char *id12, const char *name128)
{
    char id[13], name[25], full[129], card[CARD_NAME_MAX];
    int i, k;
    memcpy(id, id12, 12); id[12] = 0;
    for (i = (int)strlen(id); i > 0 && id[i - 1] == ' '; i--) id[i - 1] = 0;
    memcpy(full, name128, 128); full[128] = 0;
    snprintf(name, sizeof(name), "%.24s", full);
    if (!g_per_game || !id[0] || !strcasecmp(id, "openmenu")) return;
    for (i = k = 0; id[i] && k < CARD_NAME_MAX - 1; i++) {
        char c = id[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        card[k++] = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '_';
    }
    card[k] = 0;
    if (g_card_have && !strcasecmp(card, g_card_name)) {
        if (full[0] && cards_set_title(card, full) == 0) g_cd_due = g_title_due = 1;
        if (name[0]) say("%s", name);
        return;
    }
    if (cards_create(card) < 0) { say("could not make card %s", card); return; }
    if (full[0]) cards_set_title(card, full);
    g_cd_due = g_title_due = 1;
    snprintf(g_gid_card, sizeof(g_gid_card), "%s", card);
    g_gid_until = now_us() + 10000000;
    if (name[0]) notice(name);
}
/* the change itself, once the card is free (main loop, every turn) */
static void game_id_turn(int *settings_due)
{
    int rc;
    if (!g_gid_card[0]) return;
    if (g_switch_to[0] || g_return_to[0]) return;   /* (a change on its way) */
    rc = card_select(g_gid_card);
    if (rc == -2 && (int)(g_gid_until - now_us()) > 0) return;   /* busy: again next turn */
    if (rc >= 0) *settings_due = 1;
    if (rc == 0) say("game's card: %s", g_gid_card);
    else if (rc == 1) say("game's card %s going in", g_gid_card);
    else say("card %s not put in", g_gid_card);
    g_gid_card[0] = 0;
}

/* a card's actions that cannot be done now (greyed): connected only Use;
 * not the card in use while a VMU game runs on it; Restore with no
 * backups, Import with nothing to import (both counted as the box opens) */
static unsigned card_off(void)
{
    unsigned off = 0;
    int in_use = g_card_have && !strcasecmp(g_box_card, g_card_name);
    if (in_use) off |= 1u << CA_USE | 1u << CA_DELETE;
    if (g_cd_n < 2) off |= 1u << CA_DELETE;
    if (!g_nbackups) off |= 1u << CA_RESTORE;
    if (!g_nimports) off |= 1u << CA_IMPORT;
    /* a VMU game running holds the card in use: what would change it waits;
     * Use and New card end the game (saved to its card) and go on */
    if (game_running() && in_use) off |= ~(1u << CA_DUPLICATE | 1u << CA_BACKUP) & ((1u << CAS) - 1);
    if (!g_alone) off |= ~(1u << CA_USE) & ((1u << CAS) - 1);
    return off;
}

static void cards_input(unsigned pressed, int *settings_due)
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    CardCheck ck;
    char line[64], made[CARD_NAME_MAX];
    int rows, k;
    if (g_cd_due) cards_summary();
    rows = g_cd_n + 1;
    g_box_back = 1;
    switch (g_box) {
    case BOX_NONE:
        if ((pressed & PSP_CTRL_DOWN) && g_cd_sel < rows - 1) g_cd_sel++;
        if ((pressed & PSP_CTRL_UP) && g_cd_sel > 0) g_cd_sel--;
        if (g_cd_sel < g_cd_first) g_cd_first = g_cd_sel;
        if (g_cd_sel > g_cd_first + 5) g_cd_first = g_cd_sel - 5;
        if (!(pressed & PSP_CTRL_CROSS)) break;
        if (g_cd_sel == 0) {               /* New card: made, and in use */
            if (cards_new(made) == 0) {
                int rc = card_select(made);
                if (rc >= 0) *settings_due = 1;
                cards_summary();
                for (k = 0; k < g_cd_n; k++) if (!strcasecmp(g_cd_names[k], made)) g_cd_sel = k + 1;
                if (rc == 0) say("%s made, in use", made); else card_selected(rc, made);
            } else say("could not make a card");
            break;
        }
        snprintf(g_box_card, sizeof(g_box_card), "%s", g_cd_names[g_cd_sel - 1]);
        g_nbackups = cards_backups(g_backups, BACKUPS_MAX);
        g_nimports = cards_imports(g_imports, UI_BOX_ITEMS);
        box_lines(g_box_card, NULL, NULL, NULL);
        g_box = BOX_CARD_ACTIONS;
        g_box_item = -1;
        box_move(1, CAS, card_off());
        break;
    case BOX_CARD_ACTIONS: {
        unsigned off = card_off();
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) box_move(pressed & PSP_CTRL_DOWN ? 1 : -1, CAS, off);
        if (pressed & PSP_CTRL_CIRCLE) { g_box = BOX_NONE; break; }
        if (!(pressed & PSP_CTRL_CROSS) || g_box_item < 0 || ((off >> g_box_item) & 1)) break;
        switch (g_box_item) {
        case CA_USE: {
            int rc = card_select(g_box_card);
            if (rc >= 0) *settings_due = 1;
            card_selected(rc, g_box_card);
            break;
        }
        case CA_IMPORT: import_list(g_box_card); break;
        case CA_RENAME:
            snprintf(g_edit, sizeof(g_edit), "%s", g_box_card);
            for (k = 0; g_edit[k]; k++) if (g_edit[k] >= 'A' && g_edit[k] <= 'Z') g_edit[k] += 32;
            g_edit_key = 0;
            snprintf(line, sizeof(line), "Rename %s", g_box_card);
            box_lines(line, NULL, NULL, NULL);
            g_box = BOX_RENAME;
            break;
        case CA_DUPLICATE:
            if (cards_duplicate(g_box_card, made) == 0) say("copied as %s", made);
            else say("duplicate failed");
            g_cd_due = 1;
            break;
        case CA_BACKUP:
            if (card_get(g_box_card, img) && cards_backup(g_box_card, img, 0) == 0)
                say("backed up");
            else say("backup failed");
            break;
        case CA_RESTORE:
            snprintf(line, sizeof(line), "Restore %s from", g_box_card);
            box_lines(line, NULL, NULL, NULL);
            g_box = BOX_RESTORE;
            g_box_item = 0;
            for (k = 0; k < g_nbackups; k++) if (!strcasecmp(g_backups[k].card, g_box_card)) { g_box_item = k; break; }
            break;
        case CA_DEFRAG: {
            int before, after;
            if (!card_get(g_box_card, img)) { say("defragment failed"); break; }
            before = cardfs_game_room(img);
            card_keep(g_box_card);
            if ((after = cardfs_defrag(img)) < 0) { say("a save is broken"); break; }
            if (card_put(g_box_card, img, "defragmented") < 0) { say("defragment failed"); break; }
            say("game room %d, was %d", after, before);
            g_cd_due = 1;
            break;
        }
        case CA_CHECK:
            if (!card_get(g_box_card, img)) { say("check failed"); break; }
            cardfs_check(img, &ck);
            if (!ck.n) { say("no problems found"); break; }
            snprintf(g_box_title, sizeof(g_box_title), "%s", g_box_card);
            g_box_lines = 0;
            for (k = 0; k < ck.n && g_box_lines < 7; k++) snprintf(g_box_line[g_box_lines++], 64, "%s", ck.text[k]);
            if (ck.repairable) { g_box = BOX_REPAIR; g_box_item = 0; }
            else g_box = BOX_RESULT;
            break;
        case CA_DELETE:
            snprintf(line, sizeof(line), "Delete %s?", g_box_card);
            box_lines(line, NULL, NULL, NULL);
            g_box = BOX_CARD_DELETE;
            g_box_item = 0;
            break;
        }
        break;
    }
    case BOX_RENAME: {                     /* the keyboard (ui.h): the d-pad a key, X types it */
        int len = (int)strlen(g_edit), row = g_edit_key / UI_KB_COLS, col = g_edit_key % UI_KB_COLS, done = 0;
        int keys = row < 3 ? (int)strlen(ui_kb_rows[row]) : 2;
        if (pressed & PSP_CTRL_LEFT) col = (col + keys - 1) % keys;
        if (pressed & PSP_CTRL_RIGHT) col = (col + 1) % keys;
        if ((pressed & PSP_CTRL_DOWN) && row < UI_KB_ROWS - 1) {
            row++;
            if (row == 3) col = col * 2 / UI_KB_COLS;   /* Delete under the left half, Done the right */
        } else if ((pressed & PSP_CTRL_UP) && row > 0) {
            if (row == 3) col = col ? 9 : 3;
            row--;
        }
        if (row < 3 && col >= (int)strlen(ui_kb_rows[row])) col = (int)strlen(ui_kb_rows[row]) - 1;
        g_edit_key = row * UI_KB_COLS + col;
        if (pressed & PSP_CTRL_CROSS) {
            if (row < 3) {
                if (len < CARD_NAME_MAX - 1) { g_edit[len] = ui_kb_rows[row][col]; g_edit[len + 1] = 0; }
            } else if (col == 0) { if (len) g_edit[len - 1] = 0; }
            else done = 1;
        }
        if ((pressed & PSP_CTRL_SQUARE) && len) g_edit[len - 1] = 0;
        if (pressed & PSP_CTRL_START) done = 1;
        if (pressed & PSP_CTRL_CIRCLE) { box_lines(g_box_card, NULL, NULL, NULL); g_box = BOX_CARD_ACTIONS; g_box_item = CA_RENAME; }
        else if (done) {
            int rc, in_use = g_card_have && !strcasecmp(g_box_card, g_card_name);
            if (!g_edit[0]) { notice("type a name"); break; }
            if (!strcasecmp(g_edit, g_box_card)) { say("name unchanged"); break; }
            rc = cards_rename(g_box_card, g_edit);
            if (rc == 0) {
                if (in_use) { snprintf(g_card_name, sizeof(g_card_name), "%s", g_edit); *settings_due = 1; }
                if (!strcasecmp(g_base_name, g_box_card)) { snprintf(g_base_name, sizeof(g_base_name), "%s", g_edit); *settings_due = 1; }
                say("renamed %s", g_edit);
                cards_summary();                   /* (sorted again: the chosen row follows the card) */
                for (k = 0; k < g_cd_n; k++) if (!strcasecmp(g_cd_names[k], g_edit)) g_cd_sel = k + 1;
            } else if (rc == -2) notice("name taken");
            else say("rename failed");
        }
        break;
    }
    case BOX_RESTORE:
        if (pressed & PSP_CTRL_DOWN) g_box_item = (g_box_item + 1) % g_nbackups;
        if (pressed & PSP_CTRL_UP) g_box_item = (g_box_item + g_nbackups - 1) % g_nbackups;
        if (pressed & PSP_CTRL_CIRCLE) { box_lines(g_box_card, NULL, NULL, NULL); g_box = BOX_CARD_ACTIONS; g_box_item = CA_RESTORE; }
        else if (pressed & PSP_CTRL_CROSS) {
            k = g_box_item;
            snprintf(line, sizeof(line), "Restore %s?", g_box_card);
            box_lines(line, NULL, NULL, NULL);
            snprintf(g_box_line[0], 64, "%s %s", g_backups[k].card, g_backups[k].when);
            g_box_lines = 1;
            g_box = BOX_RESTORE_ASK;
            g_restore = k;
            g_box_item = 0;
        }
        break;
    case BOX_RESTORE_ASK:
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) g_box_item = !g_box_item;
        if ((pressed & PSP_CTRL_CIRCLE) || ((pressed & PSP_CTRL_CROSS) && !g_box_item)) {
            box_lines(g_box_card, NULL, NULL, NULL); g_box = BOX_CARD_ACTIONS; g_box_item = CA_RESTORE;
        } else if (pressed & PSP_CTRL_CROSS) {
            if (!cards_backup_read(&g_backups[g_restore], img)) { say("restore failed"); break; }
            card_keep(g_box_card);
            if (card_put(g_box_card, img, "restored") < 0) { say("restore failed"); break; }
            say("restored");
            g_cd_due = 1;
        }
        break;
    case BOX_REPAIR:
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) g_box_item = !g_box_item;
        if ((pressed & PSP_CTRL_CIRCLE) || ((pressed & PSP_CTRL_CROSS) && g_box_item)) g_box = BOX_NONE;
        else if (pressed & PSP_CTRL_CROSS) {
            if (!card_get(g_box_card, img)) { say("repair failed"); break; }
            card_keep(g_box_card);
            cardfs_repair(img, &ck);
            if (card_put(g_box_card, img, "repaired") < 0) { say("repair failed"); break; }
            say("repaired");
            g_cd_due = 1;
        }
        break;
    case BOX_CARD_DELETE:
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) g_box_item = !g_box_item;
        if ((pressed & PSP_CTRL_CIRCLE) || ((pressed & PSP_CTRL_CROSS) && !g_box_item)) {
            box_lines(g_box_card, NULL, NULL, NULL); g_box = BOX_CARD_ACTIONS; g_box_item = CA_DELETE;
        } else if (pressed & PSP_CTRL_CROSS) {
            if (g_card_have && !strcasecmp(g_box_card, g_card_name)) { say("in use"); break; }
            if (cards_backup(g_box_card, NULL, 1) < 0 || cards_delete(g_box_card) < 0) { say("delete failed"); break; }
            say("%s deleted", g_box_card);
            g_cd_due = 1;
            if (g_cd_sel > g_cd_n - 1) g_cd_sel = g_cd_n - 1;
        }
        break;
    case BOX_IMPORT:
        if (pressed & PSP_CTRL_DOWN) g_box_item = (g_box_item + 1) % g_nimports;
        if (pressed & PSP_CTRL_UP) g_box_item = (g_box_item + g_nimports - 1) % g_nimports;
        if (pressed & PSP_CTRL_CIRCLE) { box_lines(g_box_card, NULL, NULL, NULL); g_box = BOX_CARD_ACTIONS; g_box_item = CA_IMPORT; }
        else if (pressed & PSP_CTRL_CROSS) import_one(&g_imports[g_box_item]);
        break;
    case BOX_IMPORT_REPLACE:               /* No first; yes: the card made by import_one, its old game backed up */
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) g_box_item = !g_box_item;
        if ((pressed & PSP_CTRL_CIRCLE) || ((pressed & PSP_CTRL_CROSS) && !g_box_item)) {
            snprintf(g_box_title, sizeof(g_box_title), "Import into %s", g_box_card);
            g_box_lines = 0;
            g_box = BOX_IMPORT;
            g_box_item = g_replace_from;
        } else if (pressed & PSP_CTRL_CROSS) {
            card_keep(g_box_card);
            if (card_put(g_box_card, g_replace_img, "game replaced") == 0) say("game replaced on %s", g_box_card);
            else say("import failed");
            g_cd_due = 1;
        }
        break;
    default:
        if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) g_box = BOX_NONE;
        break;
    }
    if (g_cd_due) cards_summary();
    if (g_cd_sel > g_cd_n) g_cd_sel = g_cd_n;
}

/* FILE mode is about to delete a file from the card in use */
static void file_mode_delete(const unsigned char *card) { cards_backup(g_card_name, card, 1); }

static void cards_view(UiCards *v, int standalone)
{
    int i;
    if (g_cd_due) cards_summary();
    memset(v, 0, sizeof(*v));
    v->n = g_cd_n;
    for (i = 0; i < g_cd_n && i < UI_CARDS_MAX; i++) {
        v->name[i] = g_cd_names[i];
        v->saves[i] = g_cd_saves[i];
        v->free[i] = g_cd_free[i];
        v->in_use[i] = g_card_have && !strcasecmp(g_cd_names[i], g_card_name);
        v->game[i] = g_cd_game[i][0] ? g_cd_game[i] : NULL;
        v->title[i] = g_cd_title[i][0] ? g_cd_title[i] : NULL;
    }
    if (g_cd_sel < g_cd_first) g_cd_first = g_cd_sel;   /* (the chosen row in sight, wherever it moved) */
    if (g_cd_sel > g_cd_first + 5) g_cd_first = g_cd_sel - 5;
    v->first = g_cd_first;
    v->sel = g_cd_sel;
    v->new_off = 0;
    v->keys = g_box == BOX_NONE ? UI_KEYS_CARDS
            : g_box == BOX_CARD_ACTIONS && g_box_item < 0 ? UI_KEYS_BACK   /* (all greyed) */
            : g_box == BOX_RENAME ? UI_KEYS_EDIT : (g_box == BOX_RESULT || g_box == BOX_INFO) ? UI_KEYS_OK
            : box_list_len() > 9 ? UI_KEYS_LIST : UI_KEYS_MENU;
    box_view(&v->box);
    (void)standalone;
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
        g_card_note = "from the Dreamcast";
        return;
    case SYNC_SAME:
        base_set(a_name, st.a);
        if (!in_use) { card_eject(mine, 1, 1); return; }   /* (in use: a copy left half come) */
        snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", a_name);
        card_send(0);
        g_card_note = "same as the Dreamcast's";
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

/* a message just given (two seconds), or NULL: the status shows it in place
 * of the card's (at the footer's right) */
static const char *notice_now(void)
{
    return g_notice_at && now_us() - g_notice_at < 2000000 ? g_notice : NULL;
}

/* The link: its colour (UI_LINK_*), and in words, what it is and what that means */
static int link_state(const char **text, const char **hint)
{
    if (demo_on()) { *text = "Demo"; *hint = "VMU recordings played back"; return UI_LINK_DEMO; }
    if (!g_no_usb && pspexpUsbConnected()) { *text = "Connected"; *hint = "the PSP is the Dreamcast's pad"; return UI_LINK_UP; }
    if (!g_no_usb && pspexpUsbAttached()) { *text = "Cable in"; *hint = "waiting for the adapter"; return UI_LINK_WAITING; }
    *text = "Standalone";
    *hint = g_no_usb ? "no USB here (an emulator)" : "plug the adapter in to play";
    return UI_LINK_NONE;
}

/* ---- the System page: the settings, and the details ---- */
enum { SYS_THEME, SYS_PERGAME, SYS_KEYBOARD, SYS_DETAILS, SYSS };
enum { DET_RECORD, DET_DEMO, DETS };      /* Details' tools, for testing: not for playing */
static int g_sys_sel, g_sys_details, g_det_sel;

static void page_turn(int d)
{
    g_page = (g_page + d + PAGES) % PAGES;
    g_box = 0;                             /* (a box belongs to the page it was opened on) */
    g_cd_due = 1;
    g_sys_details = 0;
}

static int det_off(int row)                /* a row of Details that cannot be used now */
{
    return row == DET_DEMO && !demo_on() && !g_no_usb && pspexpUsbConnected();
}

/* Details' tools: recording the screens the Dreamcast draws on the VMU
 * (capture/vmu-NNN.rec), and playing recordings back as if connected (the
 * connected pages seen without an adapter) */
static void details_do(void)
{
    if (g_det_sel == DET_RECORD) {
        if (rec_on()) { rec_stop(); notice(rec_note()); } else { rec_start(); notice("recording the VMU"); }
    } else if (!det_off(DET_DEMO)) {
        if (demo_on()) demo_stop(); else { demo_start(); if (!demo_on()) notice(demo_note()); }
    }
}

static void details_view_rows(int standalone)
{
    static const char *const rows[DETS] = { "Record the VMU", "Demo" };
    static char rec[24];
    const char *values[DETS];
    if (rec_on()) snprintf(rec, sizeof(rec), "on, %u", rec_count()); else snprintf(rec, sizeof(rec), "off");
    values[DET_RECORD] = rec;
    values[DET_DEMO] = demo_on() ? "on" : "off";
    ui_settings(4, 188, 268, "TOOLS, FOR TESTING", rows, values, DETS, g_det_sel,
                det_off(DET_DEMO) ? 1u << DET_DEMO : 0, 0, &ui_themes[g_theme]);
    (void)standalone;
}

/* a setting changed: d -1 or 1 (left or right), 0 for X */
static void system_do(int d, int *settings_due)
{
    switch (g_sys_sel) {
    case SYS_THEME:
        g_theme = (g_theme + (d ? d : 1) + ui_theme_count) % ui_theme_count;
        *settings_due = 1;
        break;
    case SYS_PERGAME:                      /* openMenu's Game ID: each game its own card */
        g_per_game = !g_per_game;
        *settings_due = 1;
        break;
    case SYS_KEYBOARD:                     /* the Dreamcast keyboard, instead of the pad and VMU */
        g_kbd_mode = g_kbd_mode ? PSP2DC_KBD_OFF : PSP2DC_KBD_ONLY;
        if (!g_kbd_mode) g_typing = 0;
        *settings_due = 1;
        break;
    case SYS_DETAILS:
        if (d >= 0) g_sys_details = 1;     /* (X, or right: in) */
        break;
    }
}

static void system_input(unsigned pressed, int *settings_due)
{
    if (g_sys_details) {
        if (pressed & PSP_CTRL_DOWN) g_det_sel = (g_det_sel + 1) % DETS;
        if (pressed & PSP_CTRL_UP) g_det_sel = (g_det_sel + DETS - 1) % DETS;
        if (pressed & PSP_CTRL_CROSS) details_do();
        if (pressed & PSP_CTRL_CIRCLE) g_sys_details = 0;
        return;
    }
    if (pressed & PSP_CTRL_DOWN) g_sys_sel = (g_sys_sel + 1) % SYSS;
    if (pressed & PSP_CTRL_UP) g_sys_sel = (g_sys_sel + SYSS - 1) % SYSS;
    if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) system_do(pressed & PSP_CTRL_RIGHT ? 1 : -1, settings_due);
    if (pressed & PSP_CTRL_CROSS) system_do(0, settings_due);
}

/* the menu's buttons, to the page shown (up, down, X OK, O back; left/right
 * on the System page's settings; the keyboard's) */
static void page_keys(unsigned k, int *settings_due)
{
    if (g_page == PAGE_SAVES) saves_input(k);
    else if (g_page == PAGE_CARDS) cards_input(k, settings_due);
    else if (g_page == PAGE_SYSTEM) system_input(k, settings_due);
}

static void system_view(UiSystem *v, int standalone, unsigned sent, unsigned errors)
{
    static char l0[64], l1[64], l2[64];
    int k = 0;
    memset(v, 0, sizeof(*v));
    v->link = link_state(&v->link_text, &v->link_hint);
    if (!g_status_count) snprintf(l0, sizeof(l0), "Adapter: not heard from yet");
    else if (g_adapter_version != PSP2DC_PROTOCOL_VERSION)
        snprintf(l0, sizeof(l0), "Adapter: old firmware (%u), update it", g_adapter_version);
    else snprintf(l0, sizeof(l0), "Adapter: firmware %u, up %u min", g_adapter_version, g_adapter_uptime_ms / 60000);
    v->lines[k++] = l0;
    snprintf(l1, sizeof(l1), "Card: %s, %s", g_card_have ? g_card_name : "none", g_card_have ? g_card_note : "none yet");
    v->lines[k++] = l1;
    if (v->link == UI_LINK_UP) { snprintf(l2, sizeof(l2), "%u inputs sent, %u errors", sent, errors); v->lines[k++] = l2; }
    v->rows[SYS_THEME] = "Theme";         v->values[SYS_THEME] = ui_themes[g_theme].name;
    v->rows[SYS_PERGAME] = "Card per game"; v->values[SYS_PERGAME] = g_per_game ? "on" : "off";
    {
        v->rows[SYS_KEYBOARD] = "Keyboard"; v->values[SYS_KEYBOARD] = g_kbd_mode ? "on" : "off";
    }
    v->rows[SYS_DETAILS] = "Details";      v->values[SYS_DETAILS] = "for a report";
    v->nrows = SYSS;
    v->sel = g_sys_sel;
    v->arrow_rows = 1u << SYS_THEME;
    v->keys = g_sys_sel == SYS_THEME ? UI_KEYS_SYSTEM : UI_KEYS_SYSTEM_OPEN;
    (void)standalone;
}

int main(int argc, char *argv[])
{
    SceCtrlData pad;
    Psp2dcInput in;
    unsigned generation = 0, sent = 0, errors = 0, frame = 0, last_buttons = 0, lcd_shown = ~0u, combo = 0, broke = 0;
    unsigned state_seen = 0, waiting_since = 0, usb_restarts = 0, restart_wait = 5000000;
    unsigned char seq = 0;
    int connected = 0, shot_due = 0, autoshot = 0, i, settings_due = 0, version_told = 0, saves_due = 0;
    int standalone = 0, was_standalone = 0;
    unsigned vmu_held = 0, vmu_last = 0, vmu_icons = 0, last_frame_us = 0, kb_sent_us = 0;
    unsigned kb_reads_seen = 0, kb_reads_us = 0, kb_status_seen = 0, kb_shown = 0;
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
    g_page = PAGE_VMU;                     /* (it starts playing) */
    sa_before_delete(file_mode_delete);
    thid = sceKernelCreateThread("cb", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    g_in_lock = sceKernelCreateSema("input", 0, 1, 1, NULL);
    thid = sceKernelCreateThread("input", input_thread, 0x12, 0x1000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);

    g_card_lock = sceKernelCreateSema("card", 0, 1, 1, NULL);
    dev_input_load();
    dev_gameid_load();
    dev_companion_load();
    card_load();
    game_audio_start();
    thid = sceKernelCreateThread("save", saver_thread, 0x20, 0x4000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    if (g_no_usb) {
        /* no USB (PPSSPP): standalone, as with no Dreamcast */
    } else {
        g_usb_rc = usb_start();
        if ((unsigned)g_usb_kernel == 0x8002013A) {
            /* "library not linked": the driver is loaded but its library is
             * not there to call (PPSSPP loads the module, its functions are
             * stubs). Nothing can come over USB: demo mode. */
            g_no_usb = 1;   /* standalone, as with no Dreamcast */
        } else {
            thid = sceKernelCreateThread("rx", reader_thread, 0x18, 0x4000, 0, NULL);
            if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
            thid = sceKernelCreateThread("usbfix", usb_restart_thread, 0x30, 0x2000, 0, NULL);
            if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
        }
    }

    memset(&in, 0, sizeof(in));
    while (g_running) {
        sceDisplayWaitVblankStart();           /* once a frame */
        input_take(&pad, &broke);              /* (read by input_thread at every sample) */
        last_buttons &= ~broke;                /* let go since: pressed again is a new press */
        combo &= ~broke;
        {
            static unsigned last_turn;
            unsigned t = now_us(), dt = last_turn ? t - last_turn : 0;
            last_turn = t;
            if (dt > g_loop_max_us) g_loop_max_us = dt;
            if (dt > 20000) g_loop_slow++;
            if ((g_loop_window_us += dt) >= 5000000) {
                g_loop_shown_max_us = g_loop_max_us; g_loop_shown_slow = g_loop_slow;
                g_loop_max_us = g_loop_slow = 0; g_loop_window_us = 0;
            }
        }
        if (g_dev_count) pad.Buttons |= dev_input((now_us() - start_us) / 1000);
        g_alone = !demo_on() && (g_no_usb || !pspexpUsbConnected());
        if (g_alone != was_standalone) g_box = BOX_NONE;   /* (a box belongs to the mode it was opened in) */
        {
            /* SELECT switches between playing and the menu, the same in both
             * modes. Playing, the buttons are the game's (the Dreamcast's
             * pad, or the VMU's standalone) and the VMU tab is shown; in the
             * menu they are PSP2DC's: L/R the tab, the d-pad, X OK, O back,
             * triangle a screenshot, and nothing goes to the Dreamcast.
             * (SELECT sits on the right under the face buttons: a toggle
             * rather than a key held with another.) */
            unsigned pressed = pad.Buttons & ~last_buttons, held_before = last_buttons;
            last_buttons = pad.Buttons;
            if (pressed && g_toast_at && now_us() - g_toast_at > 150000) g_toast_at = 0;   /* any button hides a result */
            if (pressed & PSP_CTRL_SELECT) {
                g_menu = !g_menu;
                if (!g_menu) {
                    g_page = PAGE_VMU; g_box = BOX_NONE; g_sys_details = 0;
                    g_typing = g_kbd_mode != PSP2DC_KBD_OFF;   /* (the keyboard is all there is) */
                }
                combo = held_before & pad.Buttons & ~PSP_CTRL_SELECT;   /* held across the switch: not pressed until let go
                                                                     * (one pressed with SELECT in the same turn counts) */
                settings_due = saves_due = 1;
            }
            combo &= pad.Buttons;
            pressed &= ~(combo | PSP_CTRL_SELECT);
            if (g_menu) {
                if (pressed & PSP_CTRL_TRIANGLE) shot_due = 1;
                if ((pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && g_box == BOX_NONE) {
                    page_turn(pressed & PSP_CTRL_RTRIGGER ? 1 : -1);
                    settings_due = 1;
                } else if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))
                    box_page(pressed & PSP_CTRL_RTRIGGER ? 1 : -1);   /* (a long list: a page at a time) */
                pressed &= ~(PSP_CTRL_TRIANGLE | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER);
                if (pressed) page_keys(pressed, &settings_due);
                if (pressed) saves_due = 1;   /* shown at once */
                pad.Buttons = 0;
                pad.Lx = pad.Ly = 128;
            } else pad.Buttons &= ~(combo | PSP_CTRL_SELECT);
        }
        /* typing: the buttons are the keyboard's, the pad left alone */
        if (g_alone || !g_kbd_mode) g_typing = 0;
        if (g_typing && !g_menu) {
            kb_frame(&g_kb, pad.Buttons, pad.Lx, pad.Ly);
            pad.Buttons = 0;
            pad.Lx = pad.Ly = 128;
        } else kb_reset(&g_kb);
        map_input(&pad, &in);

        /* No Dreamcast: the PSP is the VMU standalone (vmu_sa.c), its buttons
         * the VMU's, while playing (not in the menu). */
        standalone = g_alone;
        vmu_held = 0;
        if (standalone) {
            unsigned b = pad.Buttons;   /* (none in the menu) */
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
        if (g_dev_comp) g_comp.last_us = now_us() | 1;
        {   /* openMenu's Game ID (from the adapter, or dev_gameid.txt in testing) */
            static unsigned gid_seen;
            if (g_gid_seq != gid_seen) { gid_seen = g_gid_seq; game_id_got(g_gid.id, g_gid.name); }
            if (g_dev_gid_n && g_dev_gid_i < g_dev_gid_n && (now_us() - start_us) / 1000 >= g_dev_gid[g_dev_gid_i].ms) {
                game_id_got(g_dev_gid[g_dev_gid_i].id, g_dev_gid[g_dev_gid_i].name);
                g_dev_gid_i++;
            }
            game_id_turn(&settings_due);
        }
        if (g_no_usb) {
            /* nothing to send */
        } else if (pspexpUsbConnected()) {
            restart_wait = 5000000;           /* (a link: the next restart waits the least again) */
            g_usb_restart_req = 0;
            if (demo_on()) demo_stop();       /* the real thing wins */
            unsigned gen = pspexpUsbGeneration();
            if (!connected || gen != generation) {   /* a new link: say who we are */
                Psp2dcHello hello = { PSP2DC_PROTOCOL_VERSION, g_card_have ? PSP2DC_HELLO_HAS_CARD : 0, {0, 0} };
                generation = gen;
                connected = 1;
                g_card_rx_ok = 0;                /* until card_decide or an upload says */
                g_session_backup[0] = 0;         /* a new session: the card is backed up before it is written */
                g_adapter_card[0] = g_switch_to[0] = 0;   /* (g_return_to stays: card_decide goes on) */
                state_seen = g_card_state_seq;   /* the answer to this hello is the next one */
                send_msg(PSP2DC_MSG_HELLO, &hello, sizeof(hello));
                kb_sent_us = 0;                  /* (and the keyboard's mode, below) */
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
                            g_card_note = "in the Dreamcast";
                            base_set(g_card_name, crc);
                            snprintf(g_adapter_card, sizeof(g_adapter_card), "%s", g_card_name);
                            g_card_rx_ok = 1;
                            send_msg(PSP2DC_MSG_CARD_DONE, &done, sizeof(done));
                        } else g_card_note = "not sent to the adapter";
                    } else {
                        g_card_note = "new, from the Dreamcast";
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
            {   /* the keyboard: when it changes, and every half second */
                static Psp2dcKeyboard last_kb;
                Psp2dcKeyboard kb;
                memset(&kb, 0, sizeof(kb));
                kb.mode = (unsigned char)g_kbd_mode;
                memcpy(kb.cond, g_kb.cond, sizeof(kb.cond));
                if (!kb_sent_us || memcmp(&kb, &last_kb, sizeof(kb)) || now_us() - kb_sent_us > 500000)
                    if (send_msg(PSP2DC_MSG_KEYBOARD, &kb, sizeof(kb)) >= 0) { last_kb = kb; kb_sent_us = now_us() | 1; }
            }
            if (comp_on()) {   /* the companion: the buttons when they change, an ECHO's bytes back */
                static unsigned comp_buttons = ~0u;
                unsigned char ev[PSP2DC_COMP_EVENT_MAX];
                int n = comp_echo_take(ev);
                if (n) send_msg(PSP2DC_MSG_COMP_EVENT, ev, n);
                if (pad.Buttons != comp_buttons) {
                    ev[0] = PSP2DC_REC_BUTTONS; ev[1] = 4; ev[2] = 0;
                    ev[3] = (unsigned char)pad.Buttons; ev[4] = (unsigned char)(pad.Buttons >> 8);
                    ev[5] = (unsigned char)(pad.Buttons >> 16); ev[6] = (unsigned char)(pad.Buttons >> 24);
                    if (send_msg(PSP2DC_MSG_COMP_EVENT, ev, 7) >= 0) comp_buttons = pad.Buttons;
                }
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
            /* Done by usb_restart_thread, not here: the stop, its 0.5 s wait
             * and the start froze the whole loop, a VMU game too, every 5 s
             * while the cable went to something that never syncs (a
             * computer). The wait between restarts doubles, up to a minute,
             * until a link comes up. */
            if (!pspexpUsbAttached()) waiting_since = 0;
            else if (!waiting_since) waiting_since = now_us() | 1;
            else if (!g_usb_restart_req && now_us() - waiting_since > restart_wait) {
                g_usb_restart_req = 1;
                usb_restarts++;
                waiting_since = 0;
                restart_wait = restart_wait * 2 > 60000000 ? 60000000 : restart_wait * 2;
            }
        }
        g_usb_restarts = usb_restarts;
        if (settings_due) { settings_due = 0; settings_save(); }
        frame++;
        {
            const char *lt, *lh;
            {   /* the battery, every two seconds */
                static unsigned batt_at;
                static int pct = -1, chg;
                if (!batt_at || now_us() - batt_at > 2000000) {
                    batt_at = now_us() | 1;
                    pct = scePowerIsBatteryExist() > 0 ? scePowerGetBatteryLifePercent() : -1;
                    chg = scePowerIsBatteryCharging() > 0;
                }
                ui_battery(pct, chg);
            }
            ui_frame(standalone, g_menu, link_state(&lt, &lh), g_card_have ? g_card_name : NULL,
                     g_card_have ? g_card_note : "no memory card yet", notice_now());
            ui_toast(g_toast_at && now_us() - g_toast_at < 2000000 ? g_toast : NULL);
        }
        if (g_page == PAGE_SYSTEM && g_sys_details) {
            if (frame % 6 == 0 || g_lcd_count != lcd_shown || saves_due) {
                lcd_shown = g_lcd_count;
                saves_due = 0;
                show(g_usb_rc, &in, sent, errors, standalone);
            }
        } else if (g_page == PAGE_SYSTEM) {
            if (frame % 10 == 0 || saves_due) {
                UiSystem v;
                system_view(&v, standalone, sent, errors);
                saves_due = 0;
                ui_page_system(&v, &ui_themes[g_theme]);
            }
        } else if (g_page == PAGE_CARDS) {
            if (frame % 10 == 0 || saves_due) {
                UiCards v;
                cards_view(&v, standalone);
                saves_due = 0;
                ui_page_cards(&v, &ui_themes[g_theme]);
            }
        } else if (g_page == PAGE_SAVES) {
            if (frame % 4 == 0 || saves_due) {   /* (the icons animate) */
                UiSaves v;
                saves_view(&v, standalone);
                saves_due = 0;
                g_saves_count = ui_page_saves(g_card, g_card_have, &v, &ui_themes[g_theme],
                                              now_us() / 1000, card_title() ? card_title() : g_card_name);
            }
        } else if (g_typing && !g_menu) {
            /* the keyboard: drawn when something on it changes, or every 3 frames */
            unsigned sig = (unsigned)g_kb.row << 24 ^ (unsigned)g_kb.col << 16 ^ g_kb.cond[0] << 8 ^ g_kb.cond[2] ^
                           (unsigned)g_kb.cond[3] << 4 ^ (unsigned)g_kb.caps << 12;
            if (g_status_count != kb_status_seen) {   /* is the Dreamcast reading it? */
                kb_status_seen = g_status_count;
                if (g_status.kbd_reads != kb_reads_seen) { kb_reads_seen = g_status.kbd_reads; kb_reads_us = now_us() | 1; }
            }
            if (sig != kb_shown || frame % 3 == 0 || g_lcd_count != lcd_shown) {
                UiKeys k;
                int up = !g_no_usb && pspexpUsbConnected();
                kb_shown = sig;
                lcd_shown = g_lcd_count;
                memset(&k, 0, sizeof(k));
                k.lcd = g_lcd;
                k.kb = &g_kb;
                k.state_ok = up && kb_reads_us && now_us() - kb_reads_us < 2500000;
                k.state = !up ? "not connected to the Dreamcast"
                        : g_status_count && g_adapter_version < 6 ? "the adapter's firmware is too old for it"
                        : g_status.kbd_mode != g_kbd_mode ? "plugging it in..."
                        : k.state_ok ? "the Dreamcast is reading it"
                        : "not read: the game is not asking for a keyboard";
                ui_page_keys(&k, &ui_themes[g_theme]);
            }
        } else if (comp_on() && !g_menu) {
            /* a Dreamcast program is using the companion: its page, and what arrives a second */
            static unsigned at, bytes0, recs0, rate_b, rate_r;
            if (!at || now_us() - at >= 1000000) {
                rate_b = g_comp.bytes - bytes0; rate_r = g_comp.records - recs0;
                bytes0 = g_comp.bytes; recs0 = g_comp.records;
                at = now_us() | 1;
            }
            if (frame % 4 == 0) ui_page_companion(&g_comp, rate_b, rate_r, &ui_themes[g_theme]);
        } else if (frame % 3 == 0 || g_lcd_count != lcd_shown) {   /* (the controller's state moves) */
            UiHome h;
            int up = !g_no_usb && pspexpUsbConnected();
            lcd_shown = g_lcd_count;
            memset(&h, 0, sizeof(h));
            h.lcd = g_lcd;
            h.link = link_state(&h.link_text, &h.link_hint);
            h.in = &in;
            h.card = g_card;
            h.card_have = g_card_have;
            h.card_note = g_card_note;
            h.card_name = card_title() ? card_title() : g_card_name;
            h.notice = g_notice_at && now_us() - g_notice_at < 2000000 ? g_notice : NULL;
            /* the icons: the VMU's own when on its own; connected, ATTENTION
             * while the card is being written, as the manual has it (VMU-7:
             * "Attention ... lights for Read/Write while flash memory is
             * being accessed"), here for a second after a block came */
            h.icons = standalone ? vmu_icons
                    : (up && g_card_last_us && now_us() - g_card_last_us < 1000000) ? SA_ICON_ATTENTION : 0;
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
