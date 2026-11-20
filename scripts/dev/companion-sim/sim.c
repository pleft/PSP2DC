/* See sim.h. */
#include <stdlib.h>
#include <string.h>
#include "psp2dc_protocol.h"
#include "native/device/dreamcast/vmu.h"
#include "native/device/dreamcast/vmu_storage.h"
#include "../../../psp/app/companion.h"
#include "../../../psp/app/comp_screen.h"
#include "sim.h"

/* what vmu.c and companion.c want from around them */
void vmu_storage_init(void) {}
void vmu_storage_task(void) {}
vmu_backend_t vmu_storage_backend(void) { return (vmu_backend_t)0; }
static unsigned exchanges;                 /* the clock: 60 a second */
unsigned sceKernelGetSystemTimeLow(void) { return exchanges * 16667u; }

#define TX_SIZE 1280                       /* psp2dc_host.c's PSP2DC_TX_SIZE */
static int plugged, reset_due, fail_next;
static unsigned buttons, buttons_sent = ~0u;
static FILE *stream_log;
void sim_psp_log(FILE *f) { stream_log = f; }

int sim_maple(const uint32_t *payload, int words, uint32_t *out, int *out_words)
{
    const uint32_t *pkt;
    const uint8_t *head;
    uint32_t size, i, x = 0;
    *out_words = 0;
    /* dreamcast_device.c: the memory card's function word, or it is not the companion's */
    if (words < 1 || words > 255 || __builtin_bswap32(payload[0]) != 0x00000002) return -1;
    vmu_handle_companion(payload, (uint32_t)words);
    pkt = vmu_get_companion_packet(&size);
    head = (const uint8_t *)&pkt[1];       /* Command, Destination, Origin, NumWords */
    /* the packet as the firmware's sender needs it: its length in pairs of bits, its check */
    for (i = 0; i < (head[3] + 1u) * 4; i++) x ^= head[i];
    if (size != head[3] + 3u || pkt[0] != (head[3] * 4u + 5) * 4 - 1 || pkt[head[3] + 2] >> 24 != x) {
        fprintf(stderr, "sim: the adapter's reply is not a packet (%u words, %u in its header)\n",
                (unsigned)size, head[3]);
        abort();
    }
    *out_words = head[3];
    memcpy(out, &pkt[2], head[3] * 4u);
    return head[0];
}

static unsigned put_msg(uint8_t *at, int type, const void *payload, unsigned len)
{
    at[0] = PSP2DC_MAGIC0; at[1] = PSP2DC_MAGIC1; at[2] = (uint8_t)type; at[3] = (uint8_t)len;
    if (payload) memcpy(at + PSP2DC_HEADER_LEN, payload, len);
    return PSP2DC_HEADER_LEN + len;
}

void sim_usb(void)
{
    static uint8_t tx[TX_SIZE];
    unsigned n = 0, pos;
    int sent = 0;
    exchanges++;
    if (!plugged) return;

    /* The adapter to the PSP. Something else first, some of the time (a status, a VMU screen),
     * so the companion's bytes are cut at different places. */
    if (exchanges % 5 == 0) n += put_msg(tx, PSP2DC_MSG_STATUS, NULL, 48);
    if (exchanges % 7 == 0) n += put_msg(tx + n, PSP2DC_MSG_LCD, NULL, 196);
    /* psp2dc_host.c: comp_append */
    if (reset_due && n + 2 * PSP2DC_HEADER_LEN <= sizeof(tx)) {
        n += put_msg(tx + n, PSP2DC_MSG_COMP_RESET, NULL, 0);
        reset_due = 0;
        sent = 1;
    }
    while (n + 2 * PSP2DC_HEADER_LEN + 1 <= sizeof(tx)) {
        uint32_t max = sizeof(tx) - n - 2 * PSP2DC_HEADER_LEN;
        uint32_t got = vmu_companion_take(tx + n + PSP2DC_HEADER_LEN, max > 255 ? 255 : max);
        if (!got) break;
        n += put_msg(tx + n, PSP2DC_MSG_COMP_DATA, NULL, got);
        sent = 1;
        if (got < 255) break;
    }
    if (sent && n % PSP2DC_USB_PACKET_FS == 0) n += put_msg(tx + n, PSP2DC_MSG_COMP_DATA, NULL, 0);
    if (n > sizeof(tx)) { fprintf(stderr, "sim: a transfer of %u bytes\n", n); abort(); }

    if (fail_next) {                       /* psp2dc_host.c: comp_lost */
        fail_next = 0;
        if (sent) { vmu_companion_link(false); vmu_companion_link(true); reset_due = 1; }
        return;
    }

    /* The PSP's reader (main.c: reader_thread); the other messages are not looked at here. */
    for (pos = 0; pos + PSP2DC_HEADER_LEN <= n; ) {
        const uint8_t *p = tx + pos;
        if (p[2] == PSP2DC_MSG_COMP_RESET) comp_reset();
        else if (p[2] == PSP2DC_MSG_COMP_DATA) {
            comp_feed(p + PSP2DC_HEADER_LEN, p[3]);
            if (stream_log) fwrite(p + PSP2DC_HEADER_LEN, 1, p[3], stream_log);
        }
        pos += PSP2DC_HEADER_LEN + p[3];
    }

    /* The PSP to the adapter (main.c, before its INPUT): while the companion's bytes come. */
    if (g_comp.last_us && sceKernelGetSystemTimeLow() - g_comp.last_us < 3000000) {
        unsigned char ev[PSP2DC_COMP_EVENT_MAX];
        int len;
        while ((len = comp_event_take(ev)) > 0) vmu_companion_put(ev, (uint32_t)len);
        if (buttons != buttons_sent) {
            ev[0] = PSP2DC_REC_BUTTONS; ev[1] = 4; ev[2] = 0;
            ev[3] = (unsigned char)buttons; ev[4] = (unsigned char)(buttons >> 8);
            ev[5] = (unsigned char)(buttons >> 16); ev[6] = (unsigned char)(buttons >> 24);
            vmu_companion_put(ev, 7);
            buttons_sent = buttons;
        }
    }
}
void sim_usb_fail(void) { fail_next = 1; }

void sim_psp_plug(int in)
{
    /* psp2dc_host.c: the PSP's HELLO, or its silence. (A PSP app that starts again starts
     * with nothing: no page, its buttons not yet sent.) */
    vmu_companion_link(false);
    if (in) {
        vmu_companion_link(true);
        reset_due = 1;
        memset(&g_comp, 0, sizeof(g_comp));
        buttons_sent = ~0u;
    }
    plugged = in;
}
void sim_psp_buttons(unsigned b) { buttons = b; }
const void *sim_psp_page(void) { return &g_comp; }
void sim_adapter_stats(uint32_t out[4]) { vmu_companion_stats(out); }

void sim_psp_print(FILE *f)
{
    uint32_t st[4];
    int i;
    vmu_companion_stats(st);
    fprintf(f, "PSP %s, session %u: %u bytes, %u records; test %u records, %u bytes, %u bad; stream bad %u\n",
            plugged ? "on the adapter" : "not there", g_comp.sessions, g_comp.bytes, g_comp.records,
            g_comp.test_records, g_comp.test_bytes, g_comp.test_bad, g_comp.stream_bad);
    fprintf(f, "adapter: %u commands, %u again, %u bytes in, %u out\n", (unsigned)st[0], (unsigned)st[1],
            (unsigned)st[2], (unsigned)st[3]);
    for (i = 0; i < COMP_ROWS; i++) fprintf(f, "%2d |%s\n", i, g_comp.rows[i]);
    fprintf(f, "screen: %s, %u bad records\n", g_comp.screen ? "the program's" : "none", g_comp.screen_bad);
    for (i = 0; i < PSP2DC_REGIONS; i++) {
        const CompRegion *r = &g_comp.regions[i];
        if (r->w) fprintf(f, "region %d: %d,%d %ux%u\n", i, r->x, r->y, r->w, r->h);
    }
    for (i = 0; i < PSP2DC_WIDGETS; i++) {
        const CompWidget *w = &g_comp.widgets[i];
        const char *c;
        if (!w->used) continue;
        fprintf(f, "widget %2d: type %d flags %02X at %d,%d %ux%u value %d of %d \"", i, w->type, w->flags, w->x, w->y,
                w->w, w->h, w->value, w->max);
        for (c = w->text; *c; c++) fputc(*c == '\n' ? '|' : *c, f);
        fprintf(f, "\"\n");
    }
}

/* The PSP's screen as ui.c draws it (ui_page_screen), in the Classic theme's colours. */
static unsigned mix(unsigned a, unsigned b, int k)
{
    unsigned r = ((a & 0xFF) * (16 - k) + (b & 0xFF) * k) / 16;
    unsigned g = (((a >> 8) & 0xFF) * (16 - k) + ((b >> 8) & 0xFF) * k) / 16;
    unsigned bl = (((a >> 16) & 0xFF) * (16 - k) + ((b >> 16) & 0xFF) * k) / 16;
    return 0xFF000000 | bl << 16 | g << 8 | r;
}
const unsigned *sim_psp_screen(void)
{
    static unsigned fb[PSP2DC_SCREEN_W * PSP2DC_SCREEN_H];
    CompColours col;
    int i;
    col.bg = 0xFF181818; col.panel = 0xFF2A2A2A; col.text = 0xFFC0C0C0;
    col.muted = mix(col.panel, col.text, 10); col.faint = mix(col.panel, col.text, 5);
    col.strong = 0xFFFFFFFF; col.accent = 0xFF28C8F0; col.on_accent = 0xFF000000; col.ok = 0xFF60D060;
    for (i = 0; i < PSP2DC_SCREEN_W * PSP2DC_SCREEN_H; i++) fb[i] = col.bg;
    comp_screen_draw(&g_comp, fb, PSP2DC_SCREEN_W, &col, sceKernelGetSystemTimeLow());
    return fb;
}
int sim_psp_screen_save(const char *file)
{
    const unsigned *fb = sim_psp_screen();
    static unsigned char bmp[54 + PSP2DC_SCREEN_W * PSP2DC_SCREEN_H * 3];
    unsigned size = sizeof(bmp);
    char tmp[1024];
    FILE *f;
    int x, y;
    memset(bmp, 0, 54);
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[2] = (unsigned char)size; bmp[3] = (unsigned char)(size >> 8); bmp[4] = (unsigned char)(size >> 16);
    bmp[10] = 54; bmp[14] = 40;
    bmp[18] = PSP2DC_SCREEN_W & 0xFF; bmp[19] = PSP2DC_SCREEN_W >> 8;
    bmp[22] = PSP2DC_SCREEN_H & 0xFF; bmp[23] = PSP2DC_SCREEN_H >> 8;
    bmp[26] = 1; bmp[28] = 24;
    for (y = 0; y < PSP2DC_SCREEN_H; y++)              /* (a BMP's lines go from the bottom up, blue first) */
        for (x = 0; x < PSP2DC_SCREEN_W; x++) {
            unsigned c = fb[(PSP2DC_SCREEN_H - 1 - y) * PSP2DC_SCREEN_W + x];
            unsigned char *p = bmp + 54 + (y * PSP2DC_SCREEN_W + x) * 3;
            p[0] = (unsigned char)(c >> 16); p[1] = (unsigned char)(c >> 8); p[2] = (unsigned char)c;
        }
    snprintf(tmp, sizeof(tmp), "%s.part", file);       /* (whole or not at all, for whoever is watching the file) */
    if (!(f = fopen(tmp, "wb"))) return -1;
    fwrite(bmp, 1, sizeof(bmp), f);
    fclose(f);
    return rename(tmp, file);
}

int sim_emulator(const uint8_t *in, int in_bytes, uint8_t *out, int *out_bytes)
{
    static uint32_t req[256], rep[256];
    static int started, slow = 1, replug, no_psp;
    static unsigned commands;
    static const char *page, *screen;
    int words = in_bytes / 4, out_words, code;
    if (!started) {
        const char *s;
        started = 1;
        page = getenv("PSP2DC_SIM_PAGE");
        screen = getenv("PSP2DC_SIM_SCREEN");
        if ((s = getenv("PSP2DC_SIM_SLOW")) && atoi(s) > 1) slow = atoi(s);
        if ((s = getenv("PSP2DC_SIM_REPLUG"))) replug = atoi(s);
        no_psp = getenv("PSP2DC_SIM_NO_PSP") != NULL;
        if (!no_psp) sim_psp_plug(1);
    }
    *out_bytes = 0;
    if (in_bytes < 4 || (in_bytes & 3) || words > 255) return -1;
    memcpy(req, in, (size_t)in_bytes);
    code = sim_maple(req, words, rep, &out_words);
    if (code < 0) return code;
    memcpy(out, rep, (size_t)out_words * 4);
    *out_bytes = out_words * 4;

    commands++;
    if (replug > 0 && !no_psp && commands % (60u * (unsigned)replug) == 0) { sim_psp_plug(0); sim_psp_plug(1); }
    {   /* a button a second, a second apart: cross, then R */
        static const unsigned press[4] = { 0, 0x4000, 0, 0x200 };
        sim_psp_buttons(press[commands / 60 % 4]);
    }
    if (commands % (unsigned)slow == 0) sim_usb();
    if (page && commands % 60 == 0) {
        FILE *f = fopen(page, "w");
        if (f) { sim_psp_print(f); fclose(f); }
    }
    if (screen && commands % 6 == 0) sim_psp_screen_save(screen);
    return code;
}
