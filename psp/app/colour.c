/*
 * Colour pictures for the VMU's screen (colour.h).
 *
 * The folder is read once at the start, names only, into a sorted list of
 * CRCs: finding whether a screen has a picture is a search, no file opened.
 * A picture is read by a thread of its own (libpng's simplified API) and
 * kept in a small cache, so a screen never waits for the Memory Stick: until
 * its picture is in, the dots are drawn as before. The CRC is zlib's CRC-32
 * of the 192 bytes as they come (from the adapter, or the VMU on its own).
 */
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <png.h>
#include <zlib.h>
#include "colour.h"

#define INDEX_MAX 4096
#define CACHE     24

static unsigned g_index[INDEX_MAX];        /* colour/: their CRCs, sorted */
static int g_nindex;
static unsigned g_seen[INDEX_MAX];         /* colour/new/ and colour/: not collected again */
static int g_nseen;
static int g_collect;
static unsigned g_collected;

static struct { unsigned crc, used; unsigned px[COLOUR_W * COLOUR_H]; } g_cache[CACHE];
static unsigned g_tick;

/* the loader: one picture asked for at a time */
static SceUID g_ask = -1;
static volatile unsigned g_want, g_got_crc;
static volatile int g_busy, g_got;         /* g_got: g_out holds g_got_crc's picture (or failed: g_got < 0) */
static unsigned g_out[COLOUR_W * COLOUR_H];
static unsigned char g_raw[COLOUR_W * COLOUR_H * 4];

static int cmp(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return x < y ? -1 : x > y;
}
static int has(const unsigned *list, int n, unsigned crc)
{
    return n && bsearch(&crc, list, n, sizeof(unsigned), cmp) != NULL;
}

/* "XXXXXXXX.png" (any case): its CRC, or 0 */
static int name_crc(const char *name, unsigned *crc)
{
    char *end;
    if (strlen(name) != 12 || strcasecmp(name + 8, ".png")) return 0;
    *crc = (unsigned)strtoul(name, &end, 16);
    return end == name + 8;
}

static void scan(const char *dir, unsigned *list, int *n)
{
    SceUID d = sceIoDopen(dir);
    SceIoDirent e;
    unsigned crc;
    if (d < 0) return;
    memset(&e, 0, sizeof(e));
    while (*n < INDEX_MAX && sceIoDread(d, &e) > 0) {
        if (e.d_name[0] != '.' && name_crc(e.d_name, &crc)) list[(*n)++] = crc;
        memset(&e, 0, sizeof(e));
    }
    sceIoDclose(d);
}

/* a PNG of 48x32 or 192x128 into g_out at 192x128 (0xAABBGGRR); 0 or -1 */
static int load(unsigned crc)
{
    png_image im;
    char path[64];
    int x, y, k;
    snprintf(path, sizeof(path), "%s/%08X.png", COLOUR_DIR, crc);
    memset(&im, 0, sizeof(im));
    im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, path)) return -1;
    if (!((im.width == 48 && im.height == 32) || (im.width == COLOUR_W && im.height == COLOUR_H))) {
        png_image_free(&im);
        return -1;
    }
    im.format = PNG_FORMAT_RGBA;           /* bytes R G B A: 0xAABBGGRR read as a word */
    if (!png_image_finish_read(&im, NULL, g_raw, 0, NULL)) return -1;
    k = COLOUR_W / (int)im.width;          /* 4 for 48x32, 1 for 192x128 */
    for (y = 0; y < COLOUR_H; y++)
        for (x = 0; x < COLOUR_W; x++) {
            const unsigned char *p = g_raw + ((y / k) * im.width + x / k) * 4;
            g_out[y * COLOUR_W + x] = 0xFF000000u | p[2] << 16 | p[1] << 8 | p[0];
        }
    return 0;
}

static int loader(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        unsigned crc;
        sceKernelWaitSema(g_ask, 1, NULL);
        crc = g_want;
        g_got = load(crc) == 0 ? 1 : -1;
        g_got_crc = crc;
        g_busy = 0;
    }
    return 0;
}

void colour_init(void)
{
    SceUID t;
    g_nindex = g_nseen = 0;
    scan(COLOUR_DIR, g_index, &g_nindex);
    qsort(g_index, g_nindex, sizeof(unsigned), cmp);
    memcpy(g_seen, g_index, g_nindex * sizeof(unsigned));
    g_nseen = g_nindex;
    scan(COLOUR_NEW_DIR, g_seen, &g_nseen);
    qsort(g_seen, g_nseen, sizeof(unsigned), cmp);
    g_ask = sceKernelCreateSema("colour", 0, 0, 1, NULL);
    t = sceKernelCreateThread("colour", loader, 0x30, 0x8000, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

int colour_count(void) { return g_nindex; }

const unsigned *colour_for(const unsigned char lcd[PSP2DC_LCD_BYTES])
{
    unsigned crc;
    int i, old = 0;
    if (!g_nindex) return NULL;
    if (g_got && !g_busy) {                /* a picture just read: into the cache */
        if (g_got > 0) {
            for (i = 1; i < CACHE; i++) if (g_cache[i].used < g_cache[old].used) old = i;
            g_cache[old].crc = g_got_crc;
            g_cache[old].used = ++g_tick;
            memcpy(g_cache[old].px, g_out, sizeof(g_out));
        } else {                           /* not a picture that can be read: out of the list, not tried again */
            for (i = 0; i < g_nindex && g_index[i] != g_got_crc; i++) ;
            if (i < g_nindex) { memmove(g_index + i, g_index + i + 1, (g_nindex - i - 1) * sizeof(unsigned)); g_nindex--; }
        }
        g_got = 0;
    }
    crc = (unsigned)crc32(0L, lcd, PSP2DC_LCD_BYTES);
    if (!has(g_index, g_nindex, crc)) return NULL;
    for (i = 0; i < CACHE; i++)
        if (g_cache[i].used && g_cache[i].crc == crc) { g_cache[i].used = ++g_tick; return g_cache[i].px; }
    if (!g_busy && !g_got && g_ask >= 0) { g_want = crc; g_busy = 1; sceKernelSignalSema(g_ask, 1); }
    return NULL;
}

void colour_collect_set(int on)
{
    if (on && !g_collect) { sceIoMkdir(COLOUR_DIR, 0777); sceIoMkdir(COLOUR_NEW_DIR, 0777); g_collected = 0; }
    g_collect = on;
}
int colour_collect_on(void) { return g_collect; }
unsigned colour_collected(void) { return g_collected; }

/* the screen as seen (turned half round, as ui.c draws it), black on white */
void colour_collect(const unsigned char lcd[PSP2DC_LCD_BYTES])
{
    static unsigned char px[48 * 32 * 3];
    png_image im;
    char path[64];
    unsigned crc;
    int x, y, i;
    if (!g_collect) return;
    crc = (unsigned)crc32(0L, lcd, PSP2DC_LCD_BYTES);
    if (has(g_seen, g_nseen, crc) || g_nseen >= INDEX_MAX) return;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 48; x++) {
            int sx = 47 - x, sy = 31 - y;
            int on = (lcd[sy * 6 + sx / 8] >> (7 - (sx & 7))) & 1;
            unsigned char v = on ? 0 : 255;
            px[(y * 48 + x) * 3] = px[(y * 48 + x) * 3 + 1] = px[(y * 48 + x) * 3 + 2] = v;
        }
    snprintf(path, sizeof(path), "%s/%08X.png", COLOUR_NEW_DIR, crc);
    memset(&im, 0, sizeof(im));
    im.version = PNG_IMAGE_VERSION;
    im.width = 48;
    im.height = 32;
    im.format = PNG_FORMAT_RGB;
    if (!png_image_write_to_file(&im, path, 0, px, 0, NULL)) return;
    /* kept sorted: inserted in place */
    for (i = g_nseen; i > 0 && g_seen[i - 1] > crc; i--) g_seen[i] = g_seen[i - 1];
    g_seen[i] = crc;
    g_nseen++;
    g_collected++;
}
