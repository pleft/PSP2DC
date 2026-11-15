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
 * The memory card is a file on the Memory Stick (CARD_FILE, a plain 128 KB
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
#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pspexp_usb_driver.h"
#include "../../protocol/psp2dc_protocol.h"
#include "tools.h"
#include "ui.h"

PSP_MODULE_INFO("PSP2DC", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(1024);

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

/* The memory card: outside the application's own folder, which an update
 * replaces as a whole. Written as CARD_NEW first and renamed, so a write cut
 * short (the battery, the switch) leaves the last good file in place. */
#define CARD_DIR   "ms0:/PSP2DC"
#define CARD_FILE  CARD_DIR "/card1.bin"
#define CARD_NEW   CARD_DIR "/card1.new"
#define CARD_QUIET_US 700000   /* this long with no block coming before the file is written */

static unsigned char g_card[PSP2DC_CARD_BYTES] __attribute__((aligned(64)));
static int g_card_have;                    /* g_card holds a card (from the file, or from the adapter) */
static volatile int g_card_dirty;          /* blocks came since the file was written */
static volatile unsigned g_card_last_us;   /* when the last one came */
static volatile unsigned g_card_blocks_in, g_card_writes, g_card_write_err;
static volatile int g_card_state = -1;     /* the adapter's CARD_STATE, -1: none yet */
static volatile unsigned g_card_state_seq; /* +1 each CARD_STATE */
static const char *g_card_note = "no file yet";

static unsigned now_us(void) { return (unsigned)sceKernelGetSystemTimeLow(); }

static int card_read(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int n;
    if (fd < 0) return 0;
    n = sceIoRead(fd, g_card, sizeof(g_card));
    sceIoClose(fd);
    return n == (int)sizeof(g_card);
}

/* The file at the start; a write that was cut after its last byte but before
 * the rename left CARD_NEW whole, and that is the newer one. */
static void card_load(void)
{
    sceIoMkdir(CARD_DIR, 0777);
    if (card_read(CARD_FILE)) { g_card_have = 1; g_card_note = "from the file"; }
    else if (card_read(CARD_NEW)) { g_card_have = 1; g_card_note = "from the file (.new)"; }
}

static int card_write(void)
{
    SceUID fd = sceIoOpen(CARD_NEW, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    int n;
    if (fd < 0) return -1;
    n = sceIoWrite(fd, g_card, sizeof(g_card));
    sceIoClose(fd);
    if (n != (int)sizeof(g_card)) return -1;
    sceIoRemove(CARD_FILE);
    return sceIoRename(CARD_NEW, CARD_FILE) < 0 ? -1 : 0;
}

/* Writes the file a moment after the adapter's blocks stop coming: a game's
 * save is a run of them, and the whole card after a reconnection. */
static int saver_thread(SceSize args, void *argp)
{
    while (g_running) {
        sceKernelDelayThread(200000);
        if (g_card_dirty && now_us() - g_card_last_us > CARD_QUIET_US) {
            g_card_dirty = 0;   /* before the write: a block coming during it sets it again */
            if (card_write() < 0) { g_card_write_err++; g_card_dirty = 1; g_card_last_us = now_us(); }
            else { g_card_writes++; g_card_note = "saved to the file"; }
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

/* Where the stick's values come from; SELECT goes round them. A worn nub that
 * sticks to one side is why there is a choice at all. Its resting position is
 * not measured: on the PSP-1000 this was written on, the nub's true centre is
 * 128,128 and it is the stuck position that is wrong (it frees itself when
 * turned once round), so a measurement taken while stuck would spoil the
 * centre for as long as it was kept (2026-11-15). */
enum { STICK_NUB, STICK_DPAD, STICK_OFF, STICK_MODES };
static int g_stick_mode = STICK_NUB;

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
    in->stick_x = in->stick_y = 128;
    if (g_stick_mode == STICK_NUB) {
        in->stick_x = nub(pad->Lx);
        in->stick_y = nub(pad->Ly);
    } else if (g_stick_mode == STICK_DPAD) {   /* the d-pad pushes the stick all the way, and is no d-pad */
        if (b & PSP_CTRL_LEFT)  in->stick_x = 0;
        if (b & PSP_CTRL_RIGHT) in->stick_x = 255;
        if (b & PSP_CTRL_UP)    in->stick_y = 0;
        if (b & PSP_CTRL_DOWN)  in->stick_y = 255;
        in->buttons &= ~(PSP2DC_BTN_UP | PSP2DC_BTN_DOWN | PSP2DC_BTN_LEFT | PSP2DC_BTN_RIGHT);
    }
    in->trigger_l = (b & PSP_CTRL_LTRIGGER) ? 255 : 0;
    in->trigger_r = (b & PSP_CTRL_RTRIGGER) ? 255 : 0;
    in->reserved = 0;
}

static int send_msg(unsigned char type, const void *payload, int len)
{
    static unsigned char buf[64] __attribute__((aligned(64)));
    buf[0] = PSP2DC_MAGIC0; buf[1] = PSP2DC_MAGIC1; buf[2] = type; buf[3] = (unsigned char)len;
    memcpy(buf + PSP2DC_HEADER_LEN, payload, len);
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
                g_card_state = p[PSP2DC_HEADER_LEN];
                g_card_state_seq++;
            } else if (p[2] == PSP2DC_MSG_CARD_SAVE && p[3] == sizeof(Psp2dcCardPart)) {
                const Psp2dcCardPart *part = (const Psp2dcCardPart *)(p + PSP2DC_HEADER_LEN);
                if (part->part < PSP2DC_CARD_PARTS) {
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
    if (g_stick_mode == STICK_NUB)       printf("stick from: the nub                           \n");
    else if (g_stick_mode == STICK_DPAD) printf("stick from: the d-pad (the nub is ignored)    \n");
    else                                 printf("stick from: nothing (centred, nub ignored)    \n");
    printf("Cross=A Circle=B Square=X Triangle=Y\n");
    printf("SELECT tap: stick source\n");
    printf("SELECT+Left/Right page  +Triangle shot\n");
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
    if ((p = strstr(buf, "stick="))) g_stick_mode = atoi(p + 6) % STICK_MODES;
}

static void settings_save(void)
{
    char buf[96];
    SceUID fd;
    int n = snprintf(buf, sizeof(buf), "page=%d\ntheme=%d\nstick=%d\n", g_page, g_theme, g_stick_mode);
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

/* The line at the top of the VMU and saves pages */
static const char *top_line(void)
{
    static char buf[80];
    const char *link = demo_on() ? "DEMO" : g_no_usb ? "no USB" : pspexpUsbConnected() ? "CONNECTED"
                     : pspexpUsbAttached() ? "cable in, waiting" : "no cable";
    if (g_notice_at && now_us() - g_notice_at < 2000000)
        snprintf(buf, sizeof(buf), "PSP2DC  %-17s %s", link, g_notice);
    else
        snprintf(buf, sizeof(buf), "PSP2DC  %-17s card: %s", link, g_card_have ? g_card_note : "none yet");
    return buf;
}

int main(int argc, char *argv[])
{
    SceCtrlData pad;
    Psp2dcInput in;
    unsigned generation = 0, sent = 0, errors = 0, frame = 0, last_buttons = 0, lcd_shown = ~0u;
    unsigned state_seen = 0, waiting_since = 0, usb_restarts = 0;
    unsigned char seq = 0;
    int usb_rc = 0, connected = 0, sel_used = 0, shot_due = 0, autoshot = 0, i, settings_due = 0;
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
    ui_init();
    settings_load();
    thid = sceKernelCreateThread("cb", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    card_load();
    thid = sceKernelCreateThread("save", saver_thread, 0x20, 0x4000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    if (g_no_usb) {
        demo_start();
    } else {
        usb_rc = usb_start();
        if ((unsigned)g_usb_kernel == 0x8002013A) {
            /* "library not linked": the driver is loaded but its library is
             * not there to call (PPSSPP loads the module, its functions are
             * stubs). Nothing can come over USB: demo mode. */
            g_no_usb = 1;
            demo_start();
        } else {
            thid = sceKernelCreateThread("rx", reader_thread, 0x18, 0x4000, 0, NULL);
            if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
        }
    }

    memset(&in, 0, sizeof(in));
    while (g_running) {
        sceCtrlReadBufferPositive(&pad, 1);   /* waits for the next sample: once a frame */
        if (pad.Buttons & PSP_CTRL_SELECT) {  /* the shift key: nothing goes to the Dreamcast */
            unsigned pressed = pad.Buttons & ~last_buttons;
            if (!(last_buttons & PSP_CTRL_SELECT)) sel_used = 0;
            if (pressed & PSP_CTRL_SQUARE) { if (rec_on()) rec_stop(); else rec_start(); sel_used = 1; }
            if (pressed & PSP_CTRL_TRIANGLE) { shot_due = 1; sel_used = 1; }
            if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
                g_page = (g_page + (pressed & PSP_CTRL_RIGHT ? 1 : PAGES - 1)) % PAGES;
                settings_due = 1; sel_used = 1;
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
                sel_used = 1;
            }
            if (pressed & PSP_CTRL_CIRCLE) {
                if (demo_on()) demo_stop();
                else if (!connected) demo_start();
                sel_used = 1;
            }
            last_buttons = pad.Buttons;
            pad.Buttons = 0;
            pad.Lx = pad.Ly = 128;
        } else {
            if ((last_buttons & PSP_CTRL_SELECT) && !sel_used) {
                static const char *names[STICK_MODES] = { "stick: the nub", "stick: the d-pad", "stick: none" };
                g_stick_mode = (g_stick_mode + 1) % STICK_MODES;
                notice(names[g_stick_mode]);
                settings_due = 1;
            }
            last_buttons = pad.Buttons;
        }
        map_input(&pad, &in);

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
                state_seen = g_card_state_seq;   /* the answer to this hello is the next one */
                send_msg(PSP2DC_MSG_HELLO, &hello, sizeof(hello));
            }
            if (g_card_state_seq != state_seen) {   /* the adapter said whether it holds a card */
                state_seen = g_card_state_seq;
                if (g_card_state == PSP2DC_CARD_EMPTY) {
                    Psp2dcCardDone done = { 0, {0, 0, 0} };
                    if (g_card_have && card_upload() == 0) { done.sent = 1; g_card_note = "sent to the adapter"; }
                    else if (!g_card_have) g_card_note = "new, from the adapter";
                    send_msg(PSP2DC_MSG_CARD_DONE, &done, sizeof(done));
                } else {
                    g_card_note = "the adapter's, kept here";   /* its copy is the newer one: it sends it all */
                }
            }
            in.seq = seq++;
            if (send_msg(PSP2DC_MSG_INPUT, &in, sizeof(in)) < 0) errors++;
            else sent++;
        } else {
            connected = 0;
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
            static const char *sources[STICK_MODES] = { "stick: nub", "stick: d-pad", "stick: off" };
            UiHome h;
            int up = !g_no_usb && pspexpUsbConnected();
            lcd_shown = g_lcd_count;
            memset(&h, 0, sizeof(h));
            h.lcd = g_lcd;
            h.link = demo_on() ? UI_LINK_DEMO : up ? UI_LINK_UP
                   : (!g_no_usb && pspexpUsbAttached()) ? UI_LINK_WAITING : UI_LINK_NONE;
            h.link_text = demo_on() ? "demo, no adapter" : up ? "connected to the adapter"
                        : g_no_usb ? "no USB driver" : h.link == UI_LINK_WAITING ? "cable in, waiting"
                        : "connect the adapter";
            h.in = &in;
            h.stick_source = sources[g_stick_mode];
            h.card = g_card;
            h.card_have = g_card_have;
            h.card_note = g_card_note;
            h.notice = g_notice_at && now_us() - g_notice_at < 2000000 ? g_notice : NULL;
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
