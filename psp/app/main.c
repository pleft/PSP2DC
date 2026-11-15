/*
 * PSP2DC application: the PSP as a Dreamcast controller.
 *
 * Every frame it reads the PSP's controls, turns them into a Dreamcast
 * controller state and sends that to the adapter over PSPExp's USB link
 * (protocol/psp2dc_protocol.h). A second thread reads what the adapter sends
 * back: a status message once a second and the VMU's screen each time the
 * Dreamcast draws one, shown enlarged at the bottom right of a text status page.
 *
 * User mode; the kernel USB driver (pspexp_usb.prx) was loaded by the boot
 * module and is reached through its exported library (libpspexpusb.a).
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspusb.h>
#include <pspge.h>
#include <stdio.h>
#include <string.h>
#include "pspexp_usb_driver.h"
#include "../../protocol/psp2dc_protocol.h"

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

/* The VMU's screen as the adapter last sent it, and how many came. */
static unsigned char g_lcd[PSP2DC_LCD_BYTES];
static volatile unsigned g_lcd_count;

/* The VMU's screen on the PSP's: each dot LCD_SCALE pixels square, at the
 * bottom right, in the colours of the real thing. Drawn straight into the
 * debug screen's frame (32-bit, 512 pixels a line). The Dreamcast sends the
 * picture upside down for the player (the VMU sits reversed in the
 * controller), so it is turned half round here: the last dot first. */
#define LCD_SCALE 4
#define LCD_X (480 - PSP2DC_LCD_WIDTH * LCD_SCALE - 8)
#define LCD_Y (272 - PSP2DC_LCD_HEIGHT * LCD_SCALE - 6)
#define LCD_PAPER 0xFF90B09A   /* ABGR: the LCD's grey green */
#define LCD_INK   0xFF402018   /* its dark blue dots */
#define LCD_FRAME 0xFF606060

static void lcd_draw(void)
{
    unsigned int *vram = (unsigned int *)(0x40000000 | (unsigned int)sceGeEdramGetAddr());
    int x, y, i, j;
    for (y = -3; y < PSP2DC_LCD_HEIGHT * LCD_SCALE + 3; y++) {   /* a frame round it */
        unsigned int *line = vram + (LCD_Y + y) * 512 + LCD_X;
        for (x = -3; x < PSP2DC_LCD_WIDTH * LCD_SCALE + 3; x++)
            if (y < 0 || x < 0 || y >= PSP2DC_LCD_HEIGHT * LCD_SCALE || x >= PSP2DC_LCD_WIDTH * LCD_SCALE) line[x] = LCD_FRAME;
    }
    for (y = 0; y < PSP2DC_LCD_HEIGHT; y++) {
        for (x = 0; x < PSP2DC_LCD_WIDTH; x++) {
            int sx = PSP2DC_LCD_WIDTH - 1 - x, sy = PSP2DC_LCD_HEIGHT - 1 - y;   /* turned half round */
            int dark = (g_lcd[sy * (PSP2DC_LCD_WIDTH / 8) + sx / 8] >> (7 - (sx & 7))) & 1;
            unsigned int colour = dark ? LCD_INK : LCD_PAPER;
            for (j = 0; j < LCD_SCALE; j++) {
                unsigned int *px = vram + (LCD_Y + y * LCD_SCALE + j) * 512 + LCD_X + x * LCD_SCALE;
                for (i = 0; i < LCD_SCALE; i++) px[i] = colour;
            }
        }
    }
}

/* How the USB came up, for the status page when it did not: the driver's own
 * start, then each step by hand (bus driver, PSPExp's driver, activation). */
#define USB_ALREADY 0x80243001   /* "already done": that step was already in effect */
#define PSPEXP_DRIVER_NAME "PSPExpUSBDriver"
#define PSPEXP_DRIVER_PID  0x1CA
static int g_usb_kernel, g_usb_bus, g_usb_drv, g_usb_act, g_usb_tries;

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
            } else if (p[2] == PSP2DC_MSG_LCD && p[3] == PSP2DC_LCD_BYTES) {
                memcpy(g_lcd, p + PSP2DC_HEADER_LEN, PSP2DC_LCD_BYTES);
                g_lcd_count++;
            }
            pos += total;
        }
        memmove(stream, stream + pos, have - pos);
        have -= pos;
        if (have == (int)sizeof(stream)) have = 0;   /* a message never is this long */
    }
    return 0;
}

static void show(int usb_rc, const Psp2dcInput *in, unsigned sent, unsigned errors)
{
    PspExpUsbStats st;
    const char *link;
    memset(&st, 0, sizeof(st));
    pspexpUsbGetStats(&st);
    link = pspexpUsbConnected() ? "CONNECTED to the adapter      "
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
    printf("usb      packet %u  attach %u  detach %u  sync %u      \n",
           (unsigned)st.packet_size, (unsigned)st.attaches, (unsigned)st.detaches, (unsigned)st.resyncs);
    printf("sent     %u inputs  %u errors  tx timeouts %u      \n", sent, errors, (unsigned)st.tx_timeouts);
    printf("adapter  %u status msgs  up %u s  protocol %u  bad %u      \n",
           g_status_count, g_adapter_uptime_ms / 1000, g_adapter_version, g_rx_bad);
    printf("vmu      %u screens here; adapter: asked %u wrote %u took %u\n", g_lcd_count,
           g_status.lcd_media, g_status.lcd_writes, g_status.lcd_screens);
    printf("         last write %u words, block %08X      \n", g_status.lcd_last_words,
           (unsigned)g_status.lcd_last_addr);
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
    printf("L/R = triggers   SELECT = stick\n");
    printf("HOME = quit");
}

int main(int argc, char *argv[])
{
    SceCtrlData pad;
    Psp2dcInput in;
    unsigned generation = 0, sent = 0, errors = 0, frame = 0, last_buttons = 0, lcd_shown = ~0u;
    unsigned char seq = 0;
    int usb_rc, connected = 0;
    SceUID thid;

    pspDebugScreenInit();
    /* The debug screen clears the next text line at every newline, across the
     * whole width: the status page's last lines reach the VMU picture's top
     * rows and blanked them at each redraw until the next screen came (a
     * flickering band, seen in Crazy Taxi 2, 2026-11-15). The lines are padded
     * with spaces instead, and the picture is drawn again after the text. */
    pspDebugScreenClearLineDisable();
    thid = sceKernelCreateThread("cb", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    usb_rc = usb_start();
    thid = sceKernelCreateThread("rx", reader_thread, 0x18, 0x4000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);

    memset(&in, 0, sizeof(in));
    while (g_running) {
        sceCtrlReadBufferPositive(&pad, 1);   /* waits for the next sample: once a frame */
        if ((pad.Buttons & PSP_CTRL_SELECT) && !(last_buttons & PSP_CTRL_SELECT)) {
            g_stick_mode = (g_stick_mode + 1) % STICK_MODES;
        }
        last_buttons = pad.Buttons;
        map_input(&pad, &in);

        if (pspexpUsbConnected()) {
            unsigned gen = pspexpUsbGeneration();
            if (!connected || gen != generation) {   /* a new link: say who we are */
                Psp2dcHello hello = { PSP2DC_PROTOCOL_VERSION, {0, 0, 0} };
                generation = gen;
                connected = 1;
                send_msg(PSP2DC_MSG_HELLO, &hello, sizeof(hello));
            }
            in.seq = seq++;
            if (send_msg(PSP2DC_MSG_INPUT, &in, sizeof(in)) < 0) errors++;
            else sent++;
        } else {
            connected = 0;
        }
        if ((frame++ % 6) == 0) { show(usb_rc, &in, sent, errors); lcd_shown = ~0u; }
        if (g_lcd_count != lcd_shown) { lcd_shown = g_lcd_count; lcd_draw(); }
    }
    pspexpUsbStop();
    sceKernelExitGame();
    return 0;
}
