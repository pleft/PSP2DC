/* See ui.h. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

#define LINE 512                       /* pixels a buffer line */
/* the debug screen's text: 8 lines tall, but 7 pixels a column (68 columns) */
#define FB_PIXELS (LINE * 272)

const UiTheme ui_themes[] = {
    /*  name        bg          paper       ink         frame       text        grid */
    { "Classic",  0xFF181818, 0xFF90B09A, 0xFF402018, 0xFF505050, 0xFFC0C0C0, 1 },
    { "Backlit",  0xFF2A1408, 0xFFFFBE78, 0xFF3C140A, 0xFF8A5A30, 0xFFFFD8B0, 0 },
    { "Pocket",   0xFF1E2A1E, 0xFF0FBC9B, 0xFF0F380F, 0xFF306230, 0xFF9BCC9B, 1 },
    { "Amber",    0xFF000000, 0xFF000C14, 0xFF00B0FF, 0xFF003050, 0xFF0090D0, 0 },
    { "Phosphor", 0xFF000000, 0xFF051405, 0xFF78FF50, 0xFF205020, 0xFF60C040, 0 },
    { "Paper",    0xFFD0D8D8, 0xFFE1EBEB, 0xFF141414, 0xFF606868, 0xFF303030, 0 },
};
const int ui_theme_count = sizeof(ui_themes) / sizeof(ui_themes[0]);

static unsigned *g_fb[2];              /* uncached addresses */
static int g_shown;                    /* the buffer on screen */
static unsigned *g_back;

void ui_init(void)
{
    g_fb[0] = (unsigned *)(0x40000000 | (unsigned)sceGeEdramGetAddr());
    g_fb[1] = g_fb[0] + FB_PIXELS;
    pspDebugScreenInit();              /* 32-bit, on buffer 0 */
    pspDebugScreenClearLineDisable();
    pspDebugScreenEnableBackColor(0);  /* text over what is drawn */
    g_shown = 0;
    memset(g_fb[1], 0, FB_PIXELS * 4);
}

void ui_begin(unsigned bg)
{
    int i;
    g_back = g_fb[g_shown ^ 1];
    for (i = 0; i < FB_PIXELS; i++) g_back[i] = bg;
    pspDebugScreenSetBase((u32 *)g_back);
}

void ui_end(void)
{
    sceDisplaySetFrameBuf((void *)((unsigned)g_back & 0x0FFFFFFF), LINE, PSP_DISPLAY_PIXEL_FORMAT_8888,
                          PSP_DISPLAY_SETBUF_NEXTFRAME);
    g_shown ^= 1;
}

void ui_fill(int x, int y, int w, int h, unsigned colour)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 480) w = 480 - x;
    if (y + h > 272) h = 272 - y;
    for (j = 0; j < h; j++) {
        unsigned *p = g_back + (y + j) * LINE + x;
        for (i = 0; i < w; i++) p[i] = colour;
    }
}

void ui_text(int col, int row, unsigned colour, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    pspDebugScreenSetTextColor(colour);
    pspDebugScreenSetXY(col, row);
    pspDebugScreenPuts(buf);
}

/* a colour seven eighths as bright: the gaps between the dots */
static unsigned darker(unsigned c)
{
    unsigned r = (c & 0xFF) * 7 / 8, g = ((c >> 8) & 0xFF) * 7 / 8, b = ((c >> 16) & 0xFF) * 7 / 8;
    return 0xFF000000 | b << 16 | g << 8 | r;
}

void ui_lcd(const unsigned char *lcd, int x0, int y0, int scale, const UiTheme *t)
{
    int x, y, i, j, dot = t->grid && scale >= 4 ? scale - 1 : scale;
    unsigned gap = darker(t->paper);
    ui_fill(x0 - 4, y0 - 4, PSP2DC_LCD_WIDTH * scale + 8, PSP2DC_LCD_HEIGHT * scale + 8, t->frame);
    if (dot != scale) ui_fill(x0, y0, PSP2DC_LCD_WIDTH * scale, PSP2DC_LCD_HEIGHT * scale, gap);
    for (y = 0; y < PSP2DC_LCD_HEIGHT; y++) {
        for (x = 0; x < PSP2DC_LCD_WIDTH; x++) {
            int sx = PSP2DC_LCD_WIDTH - 1 - x, sy = PSP2DC_LCD_HEIGHT - 1 - y;   /* turned half round */
            int on = (lcd[sy * (PSP2DC_LCD_WIDTH / 8) + sx / 8] >> (7 - (sx & 7))) & 1;
            unsigned c = on ? t->ink : t->paper;
            for (j = 0; j < dot; j++) {
                unsigned *p = g_back + (y0 + y * scale + j) * LINE + x0 + x * scale;
                for (i = 0; i < dot; i++) p[i] = c;
            }
        }
    }
}

/* ---- the VMU page --------------------------------------------------------
 * The screen eight times its size: 384x256 between a line of status at the
 * top and a line of keys at the bottom. */
void ui_page_vmu(const unsigned char *lcd, const UiTheme *t, const char *top, const char *bottom)
{
    ui_begin(t->bg);
    ui_lcd(lcd, (480 - 48 * 8) / 2, 8, 8, t);
    ui_fill(0, 0, 480, 8, t->bg);           /* (the frame's top edge stays under the text) */
    ui_fill(0, 264, 480, 8, t->bg);
    ui_text(0, 0, t->text, "%s", top);
    ui_text(68 - (int)strlen(t->name), 0, t->text, "%s", t->name);
    ui_text(0, 33, t->text, "%s", bottom);
    ui_end();
}

/* ---- the saves page ------------------------------------------------------
 * The card's file system (mc.pp.se/dc/vms/flashmem.html): block 255 the
 * root, 254 the FAT (16-bit entries; 0xFFFC free), 253 down to 241 the
 * directory, 32 bytes an entry. Each file's header (fileheader.html) is in
 * its first block for data files, its second for a game: descriptions, then
 * a palette of 16 ARGB4444 colours at 0x60 and 32x32 icons of 4 bits a
 * pixel from 0x80, up to three, animated. */
#define BLK PSP2DC_CARD_BLOCK_SIZE

typedef struct {
    char file[13], short_desc[17], long_desc[33];
    int type, size, header_block, icons, speed;
    unsigned char date[8];             /* BCD: century, year, month, day, hour, minute, second, weekday */
} SaveFile;

static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }

static void trim(char *s) { int n = strlen(s); while (n && (s[n - 1] == ' ' || !s[n - 1])) s[--n] = 0; }

static int card_files(const unsigned char *card, SaveFile *out, int max, int *free_blocks)
{
    const unsigned char *fat = card + 254 * BLK;
    int blk, i, n = 0, b;
    *free_blocks = 0;
    for (b = 0; b < 200; b++) if (rd16(fat + b * 2) == 0xFFFC) (*free_blocks)++;
    for (blk = 253; blk >= 241 && n < max; blk--) {
        for (i = 0; i < 16 && n < max; i++) {
            const unsigned char *e = card + blk * BLK + i * 32;
            SaveFile *f = &out[n];
            int k, hb;
            if (e[0] != 0x33 && e[0] != 0xCC) continue;
            f->type = e[0];
            memcpy(f->file, e + 4, 12); f->file[12] = 0; trim(f->file);
            memcpy(f->date, e + 0x10, 8);
            f->size = rd16(e + 0x18);
            hb = rd16(e + 2);
            for (k = rd16(e + 0x1A); k > 0 && hb < 256; k--) hb = rd16(fat + hb * 2);   /* the header's block */
            if (hb >= 256) continue;
            f->header_block = hb;
            {
                const unsigned char *h = card + hb * BLK;
                memcpy(f->short_desc, h, 16); f->short_desc[16] = 0; trim(f->short_desc);
                memcpy(f->long_desc, h + 16, 32); f->long_desc[32] = 0; trim(f->long_desc);
                f->icons = rd16(h + 0x40);
                f->speed = rd16(h + 0x42);
                if (f->icons < 1 || f->icons > 3) f->icons = 0;
            }
            n++;
        }
    }
    return n;
}

static int bcd(unsigned char v) { return (v >> 4) * 10 + (v & 15); }

/* One frame of a file's icon, scale times its size. The icons follow the
 * header and may run into the file's next block. */
static void draw_icon(const unsigned char *card, const SaveFile *f, int frame, int x0, int y0, int scale)
{
    const unsigned char *fat = card + 254 * BLK, *h = card + f->header_block * BLK;
    unsigned pal[16];
    unsigned char icon[512];
    int i, x, y, j, k, off = 0x80 + frame * 512, blk = f->header_block;
    for (i = 0; i < 16; i++) {
        unsigned c = rd16(h + 0x60 + i * 2);
        unsigned a = (c >> 12) & 15, r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
        pal[i] = a ? 0xFF000000 | (b * 17) << 16 | (g * 17) << 8 | (r * 17) : 0;
    }
    for (i = 0; i < 512; i++) {        /* the icon's bytes, following the chain */
        int at = off + i, bb = blk, hop = at / BLK;
        for (k = 0; k < hop && bb < 256; k++) bb = rd16(fat + bb * 2);
        icon[i] = bb < 256 ? card[bb * BLK + at % BLK] : 0;
    }
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++) {
            int v = icon[y * 16 + x / 2];
            unsigned c = pal[x & 1 ? v & 15 : v >> 4];
            if (!c) continue;          /* transparent */
            for (j = 0; j < scale; j++) {
                unsigned *p = g_back + (y0 + y * scale + j) * LINE + x0 + x * scale;
                for (k = 0; k < scale; k++) p[k] = c;
            }
        }
}

int ui_page_saves(const unsigned char *card, int have, int first, const UiTheme *t, unsigned now_ms,
                  const char *top)
{
    static SaveFile files[200];
    int n = 0, free_blocks = 0, i, row;
    ui_begin(t->bg);
    ui_text(0, 0, t->text, "%s", top);
    if (!have) {
        ui_text(2, 4, t->text, "No memory card yet: it comes from ms0:/PSP2DC/card1.bin");
        ui_text(2, 5, t->text, "or from the adapter once connected.");
        ui_end();
        return 0;
    }
    n = card_files(card, files, 200, &free_blocks);
    ui_text(0, 2, t->text, "MEMORY CARD   %d save%s   %d of 200 blocks free",
            n, n == 1 ? "" : "s", free_blocks);
    if (!n) ui_text(2, 5, t->text, "The card is empty.");
    for (i = first, row = 0; i < n && row < 3; i++, row++) {
        const SaveFile *f = &files[i];
        int y = 32 + row * 76, line = y / 8;
        int frame = 0;
        ui_fill(4, y - 2, 472, 72, t->frame);
        ui_fill(8, y + 2, 64, 64, t->paper);
        if (f->icons) {
            int step = (f->speed > 0 ? f->speed : 8) * 1000 / 15;   /* (the speed's unit is not confirmed) */
            frame = (now_ms / step) % f->icons;
            draw_icon(card, f, frame, 8, y + 2, 2);
        }
        ui_text(12, line + 1, 0xFFFFFFFF, "%s", f->long_desc[0] ? f->long_desc : f->file);
        ui_text(12, line + 3, t->text, "%-12s  %s", f->file, f->short_desc);
        ui_text(12, line + 5, t->text, "%02d/%02d/%02d%02d %02d:%02d   %d block%s   %s",
                bcd(f->date[3]), bcd(f->date[2]), bcd(f->date[0]), bcd(f->date[1]),
                bcd(f->date[4]), bcd(f->date[5]), f->size, f->size == 1 ? "" : "s",
                f->type == 0xCC ? "GAME" : "DATA");
    }
    if (n > 3) ui_text(0, 33, t->text, "SELECT+Up/Down: more (%d-%d of %d)", first + 1, first + row, n);
    else ui_text(0, 33, t->text, "SELECT+Left/Right: page");
    ui_end();
    return n;
}
