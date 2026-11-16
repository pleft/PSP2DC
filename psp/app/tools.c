/* See tools.h. */
#include <pspkernel.h>
#include <pspge.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tools.h"

static unsigned now_ms(void) { return (unsigned)(sceKernelGetSystemTimeWide() / 1000); }

/* The next free name: dir/prefix-NNN.ext, NNN from 001. */
static int next_name(const char *dir, const char *prefix, const char *ext, char *out, int max)
{
    int i;
    sceIoMkdir(TOOLS_DIR, 0777);
    sceIoMkdir(dir, 0777);
    for (i = 1; i < 1000; i++) {
        SceUID fd;
        snprintf(out, max, "%s/%s-%03d.%s", dir, prefix, i, ext);
        if ((fd = sceIoOpen(out, PSP_O_RDONLY, 0)) < 0) return 0;   /* (by opening: see cards.c) */
        sceIoClose(fd);
    }
    return -1;
}

/* ---- recording ---------------------------------------------------------
 * A .rec file: "P2DCVMU1", then for each screen its time in milliseconds
 * since the first (u32, little endian) and its 192 bytes, as the Dreamcast
 * sent them. Kept in memory while recording and written at the stop, so the
 * Memory Stick is never written while a game is being played. */
#define REC_MAGIC  "P2DCVMU1"
#define REC_RECORD (4 + PSP2DC_LCD_BYTES)
#define REC_MAX    1500                    /* ~290 KB; at 30 screens a second, 50 s */

static unsigned char *g_rec;
static volatile unsigned g_rec_n;
static volatile int g_rec_on;
static unsigned g_rec_t0;
static char g_rec_note[96] = "";   /* (SELECT+Square: a development aid, not in the help) */

void rec_start(void)
{
    if (g_rec_on) return;
    if (!g_rec) g_rec = malloc(REC_MAX * REC_RECORD);
    if (!g_rec) { snprintf(g_rec_note, sizeof(g_rec_note), "no memory to record"); return; }
    g_rec_n = 0;
    g_rec_t0 = now_ms();
    g_rec_on = 1;
    snprintf(g_rec_note, sizeof(g_rec_note), "recording");
}

void rec_frame(const unsigned char *lcd)
{
    unsigned char *r;
    unsigned t;
    if (!g_rec_on || g_rec_n >= REC_MAX) return;
    r = g_rec + g_rec_n * REC_RECORD;
    t = now_ms() - g_rec_t0;
    r[0] = t; r[1] = t >> 8; r[2] = t >> 16; r[3] = t >> 24;
    memcpy(r + 4, lcd, PSP2DC_LCD_BYTES);
    g_rec_n++;
}

void rec_stop(void)
{
    char path[128];
    SceUID fd;
    unsigned n;
    if (!g_rec_on) return;
    g_rec_on = 0;
    n = g_rec_n;
    if (!n) { snprintf(g_rec_note, sizeof(g_rec_note), "recording stopped: no screens came"); return; }
    if (next_name(TOOLS_REC_DIR, "vmu", "rec", path, sizeof(path)) < 0 ||
        (fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777)) < 0) {
        snprintf(g_rec_note, sizeof(g_rec_note), "recording NOT saved (Memory Stick)");
        return;
    }
    sceIoWrite(fd, REC_MAGIC, 8);
    sceIoWrite(fd, g_rec, n * REC_RECORD);
    sceIoClose(fd);
    snprintf(g_rec_note, sizeof(g_rec_note), "saved %u screens: %s", n, strrchr(path, '/') + 1);
}

int rec_on(void) { return g_rec_on; }
unsigned rec_count(void) { return g_rec_n; }
const char *rec_note(void) { return g_rec_note; }

/* ---- demo --------------------------------------------------------------
 * Every recording in capture/ is read into one buffer, back to back, and
 * played in turn at the pace it was recorded, round and round. With none, a
 * made-up screen: the word DEMO and a dot going along. */
#define DEMO_MAX   (2 * REC_MAX)
static unsigned char *g_demo;          /* records, as in a .rec file without the magic */
static unsigned g_demo_n, g_demo_i, g_demo_t0, g_demo_files;
static int g_demo_on, g_demo_shown = -1;
static char g_demo_note[96];

static void demo_load(void)
{
    SceUID dir = sceIoDopen(TOOLS_REC_DIR);
    SceIoDirent e;
    g_demo_n = g_demo_files = 0;
    if (dir < 0) return;
    if (!g_demo) g_demo = malloc(DEMO_MAX * REC_RECORD);
    if (!g_demo) { sceIoDclose(dir); return; }
    memset(&e, 0, sizeof(e));
    while (sceIoDread(dir, &e) > 0) {
        char path[160], magic[8];
        int len = strlen(e.d_name), n;
        SceUID fd;
        if (len < 5 || e.d_name[0] == '.' || strcmp(e.d_name + len - 4, ".rec")) continue;   /* (hidden: "._" from macOS) */
        snprintf(path, sizeof(path), "%s/%s", TOOLS_REC_DIR, e.d_name);
        if ((fd = sceIoOpen(path, PSP_O_RDONLY, 0)) < 0) continue;
        if (sceIoRead(fd, magic, 8) == 8 && !memcmp(magic, REC_MAGIC, 8)) {
            n = sceIoRead(fd, g_demo + g_demo_n * REC_RECORD, (DEMO_MAX - g_demo_n) * REC_RECORD);
            if (n > 0) {
                unsigned first = g_demo_n, i, base;
                g_demo_n += n / REC_RECORD;
                g_demo_files++;
                /* times made to follow on from the file before */
                base = 0;
                if (first) {
                    unsigned char *p = g_demo + (first - 1) * REC_RECORD;
                    base = p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
                    base += 1000;   /* a second on the last screen of each */
                }
                for (i = first; i < g_demo_n; i++) {
                    unsigned char *p = g_demo + i * REC_RECORD;
                    unsigned t = (p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24) + base;
                    p[0] = t; p[1] = t >> 8; p[2] = t >> 16; p[3] = t >> 24;
                }
            }
        }
        sceIoClose(fd);
        memset(&e, 0, sizeof(e));
        if (g_demo_n >= DEMO_MAX) break;
    }
    sceIoDclose(dir);
}

void demo_start(void)
{
    demo_load();
    g_demo_i = 0;
    g_demo_t0 = now_ms();
    g_demo_shown = -1;
    g_demo_on = 1;
    if (g_demo_n) snprintf(g_demo_note, sizeof(g_demo_note), "DEMO: %u recordings, %u screens", g_demo_files, g_demo_n);
    else snprintf(g_demo_note, sizeof(g_demo_note), "DEMO: no recordings, a made-up screen");
}

void demo_stop(void) { g_demo_on = 0; }
int demo_on(void) { return g_demo_on; }
const char *demo_note(void) { return g_demo_on ? g_demo_note : ""; }

/* 5x7 letters, a row a byte, bit 4 the leftmost */
static const unsigned char glyph_D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
static const unsigned char glyph_E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
static const unsigned char glyph_M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
static const unsigned char glyph_O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};

/* A dot at (x, y) as the player sees it; stored as the Dreamcast sends it,
 * which is turned half round. */
static void dot(unsigned char *lcd, int x, int y)
{
    int sx = PSP2DC_LCD_WIDTH - 1 - x, sy = PSP2DC_LCD_HEIGHT - 1 - y;
    if (x < 0 || y < 0 || x >= PSP2DC_LCD_WIDTH || y >= PSP2DC_LCD_HEIGHT) return;
    lcd[sy * (PSP2DC_LCD_WIDTH / 8) + sx / 8] |= 0x80 >> (sx & 7);
}

static void made_up(unsigned char *lcd, unsigned step)
{
    const unsigned char *word[4] = { glyph_D, glyph_E, glyph_M, glyph_O };
    int l, r, c, x;
    memset(lcd, 0, PSP2DC_LCD_BYTES);
    for (x = 0; x < PSP2DC_LCD_WIDTH; x++) { dot(lcd, x, 0); dot(lcd, x, PSP2DC_LCD_HEIGHT - 1); }
    for (x = 0; x < PSP2DC_LCD_HEIGHT; x++) { dot(lcd, 0, x); dot(lcd, PSP2DC_LCD_WIDTH - 1, x); }
    for (l = 0; l < 4; l++)            /* each letter twice its size: 10x14, 1 apart */
        for (r = 0; r < 7; r++)
            for (c = 0; c < 5; c++)
                if (word[l][r] & (0x10 >> c)) {
                    int x0 = 2 + l * 11 + c * 2, y0 = 4 + r * 2;
                    dot(lcd, x0, y0); dot(lcd, x0 + 1, y0); dot(lcd, x0, y0 + 1); dot(lcd, x0 + 1, y0 + 1);
                }
    x = 2 + (int)(step % 42);           /* the dot along the bottom */
    for (r = 0; r < 3; r++) for (c = 0; c < 3; c++) dot(lcd, x + c, 23 + r);
}

int demo_frame(unsigned char out[PSP2DC_LCD_BYTES])
{
    unsigned t = now_ms() - g_demo_t0;
    if (!g_demo_on) return 0;
    if (!g_demo_n) {                    /* the made-up screen: a new one every 100 ms */
        int step = t / 100;
        if (step == g_demo_shown) return 0;
        g_demo_shown = step;
        made_up(out, step);
        return 1;
    }
    for (;;) {                          /* the last screen whose time has come */
        unsigned char *p = g_demo + g_demo_i * REC_RECORD;
        unsigned at = p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
        if (at > t) break;
        if (g_demo_i + 1 >= g_demo_n) { /* the end: round again after a second */
            if (t > at + 1000) { g_demo_i = 0; g_demo_t0 = now_ms(); t = 0; continue; }
            break;
        }
        g_demo_i++;
    }
    if ((int)g_demo_i == g_demo_shown) return 0;
    g_demo_shown = g_demo_i;
    memcpy(out, g_demo + g_demo_i * REC_RECORD + 4, PSP2DC_LCD_BYTES);
    return 1;
}

/* ---- screenshots -------------------------------------------------------
 * The frame on screen (ui.c draws two in turn): 32 bits a pixel (A B G R from the top byte), 512
 * pixels a line, at the start of VRAM. A BMP is 24 bits, bottom line first. */
static char g_shot_note[96] = "SELECT+Triangle takes a screenshot";

int shot_take(void)
{
    static unsigned char line[480 * 3];
    const unsigned int *vram;
    void *top = NULL;
    int width = 512, format = 3;
    sceDisplayGetFrameBuf(&top, &width, &format, PSP_DISPLAY_SETBUF_NEXTFRAME);   /* the one on screen */
    vram = (const unsigned int *)(0x40000000 | (unsigned int)(top ? top : sceGeEdramGetAddr()));
    unsigned char head[54];
    unsigned size = 54 + sizeof(line) * 272;
    char path[128];
    SceUID fd;
    int x, y;
    if (next_name(TOOLS_SHOT_DIR, "shot", "bmp", path, sizeof(path)) < 0 ||
        (fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777)) < 0) {
        snprintf(g_shot_note, sizeof(g_shot_note), "screenshot NOT saved (Memory Stick)");
        return -1;
    }
    memset(head, 0, sizeof(head));
    head[0] = 'B'; head[1] = 'M';
    head[2] = size; head[3] = size >> 8; head[4] = size >> 16;
    head[10] = 54; head[14] = 40;
    head[18] = 480 & 0xFF; head[19] = 480 >> 8;
    head[22] = 272 & 0xFF; head[23] = 272 >> 8;
    head[26] = 1; head[28] = 24;
    sceIoWrite(fd, head, sizeof(head));
    for (y = 271; y >= 0; y--) {
        for (x = 0; x < 480; x++) {
            unsigned int c = vram[y * 512 + x];
            line[x * 3 + 0] = c >> 16;   /* B */
            line[x * 3 + 1] = c >> 8;    /* G */
            line[x * 3 + 2] = c;         /* R */
        }
        sceIoWrite(fd, line, sizeof(line));
    }
    sceIoClose(fd);
    snprintf(g_shot_note, sizeof(g_shot_note), "saved %s", strrchr(path, '/') + 1);
    return 0;
}

const char *shot_note(void) { return g_shot_note; }
