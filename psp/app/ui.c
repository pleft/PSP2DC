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
    /*  name        bg          paper       ink         frame       text        grid body        panel */
    { "Classic",  0xFF181818, 0xFFC0D4A8, 0xFFB43C3C, 0xFF505050, 0xFFC0C0C0, 1, 0xFFE4E6E6, 0xFF2A2A2A },
    { "Backlit",  0xFF2A1408, 0xFFFFBE78, 0xFF3C140A, 0xFF8A5A30, 0xFFFFD8B0, 0, 0xFFB08050, 0xFF40200E },
    { "Pocket",   0xFF1E2A1E, 0xFF0FBC9B, 0xFF0F380F, 0xFF306230, 0xFF9BCC9B, 1, 0xFFB8C0B8, 0xFF2A382A },
    { "Amber",    0xFF000000, 0xFF000C14, 0xFF00B0FF, 0xFF003050, 0xFF0090D0, 0, 0xFF102028, 0xFF081018 },
    { "Phosphor", 0xFF000000, 0xFF051405, 0xFF78FF50, 0xFF205020, 0xFF60C040, 0, 0xFF102010, 0xFF081408 },
    { "Paper",    0xFFD0D8D8, 0xFFE1EBEB, 0xFF141414, 0xFF606868, 0xFF303030, 0, 0xFFF4F8F8, 0xFFC0C8C8 },
};
const int ui_theme_count = sizeof(ui_themes) / sizeof(ui_themes[0]);

static unsigned *g_fb[2];              /* uncached addresses */
static int g_shown;                    /* the buffer on screen, or about to be */
static unsigned *g_back;
static int g_swap_vcount = -1;         /* the vblank count when the last swap was asked for */
static unsigned g_draw_us, g_draw_max_us;   /* how long the last frame took to draw, and the longest */
static unsigned g_t0;

unsigned ui_draw_us(void) { return g_draw_us; }
unsigned ui_draw_max_us(void) { return g_draw_max_us; }

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
    /* The swap asked for at the last ui_end happens at the next vblank. Until
     * then the other buffer is still on screen: drawing into it would show,
     * half drawn (a flicker on hardware, not in PPSSPP). Wait for it. */
    if (g_swap_vcount >= 0 && (int)sceDisplayGetVcount() == g_swap_vcount) sceDisplayWaitVblankStart();
    g_t0 = (unsigned)sceKernelGetSystemTimeLow();
    g_back = g_fb[g_shown ^ 1];
    for (i = 0; i < FB_PIXELS; i++) g_back[i] = bg;
    pspDebugScreenSetBase((u32 *)g_back);
}

void ui_end(void)
{
    g_draw_us = (unsigned)sceKernelGetSystemTimeLow() - g_t0;
    if (g_draw_us > g_draw_max_us) g_draw_max_us = g_draw_us;
    sceKernelDcacheWritebackAll();   /* (all drawing is to uncached VRAM; this is cheap insurance) */
    sceDisplaySetFrameBuf((void *)((unsigned)g_back & 0x0FFFFFFF), LINE, PSP_DISPLAY_PIXEL_FORMAT_8888,
                          PSP_DISPLAY_SETBUF_NEXTFRAME);
    g_swap_vcount = (int)sceDisplayGetVcount();
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
    ui_lcd_dots(lcd, x0, y0, scale, scale, t);
}

/* the 48x32 dots spread over w x h pixels: a dot is w/48 wide and h/32 tall,
 * rounded row by row and column by column where that is not whole */
void ui_lcd_fit(const unsigned char *lcd, int x0, int y0, int w, int h, const UiTheme *t)
{
    int x, y, i, j, gap = t->grid && w >= 4 * PSP2DC_LCD_WIDTH ? 1 : 0;
    ui_fill(x0 - 4, y0 - 4, w + 8, h + 8, t->frame);
    if (gap) ui_fill(x0, y0, w, h, darker(t->paper));
    for (y = 0; y < PSP2DC_LCD_HEIGHT; y++) {
        int top = y0 + y * h / PSP2DC_LCD_HEIGHT, bottom = y0 + (y + 1) * h / PSP2DC_LCD_HEIGHT - gap;
        for (x = 0; x < PSP2DC_LCD_WIDTH; x++) {
            int left = x0 + x * w / PSP2DC_LCD_WIDTH, right = x0 + (x + 1) * w / PSP2DC_LCD_WIDTH - gap;
            int sx = PSP2DC_LCD_WIDTH - 1 - x, sy = PSP2DC_LCD_HEIGHT - 1 - y;   /* turned half round */
            int on = (lcd[sy * (PSP2DC_LCD_WIDTH / 8) + sx / 8] >> (7 - (sx & 7))) & 1;
            unsigned c = on ? t->ink : t->paper;
            for (j = top; j < bottom; j++) {
                unsigned *p = g_back + j * LINE;
                for (i = left; i < right; i++) p[i] = c;
            }
        }
    }
}

/* each dot sx pixels wide and sy tall */
void ui_lcd_dots(const unsigned char *lcd, int x0, int y0, int sx_, int sy_, const UiTheme *t)
{
    int x, y, i, j, dw = t->grid && sx_ >= 4 ? sx_ - 1 : sx_, dh = t->grid && sy_ >= 4 ? sy_ - 1 : sy_;
    unsigned gap = darker(t->paper);
    ui_fill(x0 - 4, y0 - 4, PSP2DC_LCD_WIDTH * sx_ + 8, PSP2DC_LCD_HEIGHT * sy_ + 8, t->frame);
    if (dw != sx_ || dh != sy_) ui_fill(x0, y0, PSP2DC_LCD_WIDTH * sx_, PSP2DC_LCD_HEIGHT * sy_, gap);
    for (y = 0; y < PSP2DC_LCD_HEIGHT; y++) {
        for (x = 0; x < PSP2DC_LCD_WIDTH; x++) {
            int sx = PSP2DC_LCD_WIDTH - 1 - x, sy = PSP2DC_LCD_HEIGHT - 1 - y;   /* turned half round */
            int on = (lcd[sy * (PSP2DC_LCD_WIDTH / 8) + sx / 8] >> (7 - (sx & 7))) & 1;
            unsigned c = on ? t->ink : t->paper;
            for (j = 0; j < dh; j++) {
                unsigned *p = g_back + (y0 + y * sy_ + j) * LINE + x0 + x * sx_;
                for (i = 0; i < dw; i++) p[i] = c;
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
            int step = (f->speed > 0 ? f->speed : 8) * 1000 / 30;   /* n/30 s (Sega VMU manual, VMT-18) */
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


/* ---- the home page --------------------------------------------------------
 * Left, a drawing of the VMU (240x260) with its screen four times its size;
 * right, the controller as the Dreamcast sees it now, and the memory card. */

static void disc(int cx, int cy, int r, unsigned c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < 480 && py >= 0 && py < 272) g_back[py * LINE + px] = c;
            }
}

static void ring(int cx, int cy, int r, int w, unsigned c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (d <= r * r + r && d >= (r - w) * (r - w)) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < 480 && py >= 0 && py < 272) g_back[py * LINE + px] = c;
            }
        }
}

/* a rectangle with its corners rounded by r */
static void round_rect(int x0, int y0, int w, int h, int r, unsigned c)
{
    ui_fill(x0 + r, y0, w - 2 * r, h, c);
    ui_fill(x0, y0 + r, r, h - 2 * r, c);
    ui_fill(x0 + w - r, y0 + r, r, h - 2 * r, c);
    disc(x0 + r, y0 + r, r, c);
    disc(x0 + w - r - 1, y0 + r, r, c);
    disc(x0 + r, y0 + h - r - 1, r, c);
    disc(x0 + w - r - 1, y0 + h - r - 1, r, c);
}

/* a colour at a quarter of its brightness, for what is not pressed */
static unsigned dim(unsigned c)
{
    unsigned r = (c & 0xFF) / 4, g = ((c >> 8) & 0xFF) / 4, b = ((c >> 16) & 0xFF) / 4;
    return 0xFF000000 | b << 16 | g << 8 | r;
}

/* half its brightness, for the lesser lines of text */
static unsigned half(unsigned c)
{
    unsigned r = (c & 0xFF) * 5 / 8, g = ((c >> 8) & 0xFF) * 5 / 8, b = ((c >> 16) & 0xFF) * 5 / 8;
    return 0xFF000000 | b << 16 | g << 8 | r;
}

/* is the colour light (for text on it)? */
static int light(unsigned c) { return ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF)) > 3 * 140; }

static void label(int x, int y, unsigned c, const char *s)
{
    for (; *s; s++, x += 7) pspDebugScreenPutChar(x, y, c, (unsigned char)*s);
}

/* ---- the VMU, drawn ------------------------------------------------------
 * As the real one looks from the front: a case with a flat top and a
 * rounded bottom, lit from the top left (lighter above, darker below, a
 * shadow under and to the right), the screen sunk in a dark surround, a
 * d-pad in a dished well, round A and B, and the small MODE and SLEEP. */

static unsigned mix(unsigned a, unsigned b, int k)   /* a to b, k of 16 */
{
    unsigned r = ((a & 0xFF) * (16 - k) + (b & 0xFF) * k) / 16;
    unsigned g = (((a >> 8) & 0xFF) * (16 - k) + ((b >> 8) & 0xFF) * k) / 16;
    unsigned bl = (((a >> 16) & 0xFF) * (16 - k) + ((b >> 16) & 0xFF) * k) / 16;
    return 0xFF000000 | bl << 16 | g << 8 | r;
}

/* how far in a row of a rounded shape starts: r the corner's radius, d the
 * row's distance into the corner (0 at the edge) */
static int corner_inset(int r, int d)
{
    int x = 0;
    if (d >= r) return 0;
    while (x < r && (r - x) * (r - x) + (r - d) * (r - d) > r * r) x++;   /* (small: a few dozen steps) */
    return x;
}

/* a shape with top corners of radius rt and bottom ones of rb, shaded from
 * top to bottom between c_top and c_bottom */
static void shape(int x0, int y0, int w, int h, int rt, int rb, unsigned c_top, unsigned c_bottom)
{
    int y;
    for (y = 0; y < h; y++) {
        int in = y < rt ? corner_inset(rt, y) : y >= h - rb ? corner_inset(rb, h - 1 - y) : 0;
        ui_fill(x0 + in, y0 + y, w - 2 * in, 1, mix(c_top, c_bottom, y * 16 / h));
    }
}

/* a round button: its shadow, its edge, its face lit from the top left */
static void button(int cx, int cy, int r, unsigned face, unsigned bg)
{
    disc(cx + 1, cy + 2, r + 1, mix(bg, 0xFF000000, 6));
    disc(cx, cy, r, mix(face, 0xFF000000, 5));
    disc(cx - 1, cy - 1, r - 2, face);
    disc(cx - r / 3, cy - r / 3, r / 3, mix(face, 0xFFFFFFFF, 6));
}

/* The LCD's four icons under the dots, after the Sega VMU manual's drawing
 * (Specifications, VMU-12: "File management", "Executable file initiation",
 * "Clock display", "Writing in progress"): a lined page with a sheet folded
 * over its top, slanting off to the left (the page's edge and shadow on the right),
 * two cards with a spade, a clock, a burst with "!". Drawn twice their size.
 * The System-BIOS drives them (the manual: their use by a program is
 * prohibited); the Dreamcast cannot, so they are drawn unlit: a faint ghost,
 * as unlit segments show on the real screen. */
static const char *const vmu_icons[4][13] = {   /* 15 wide, 13 tall */
    { "......XXXXXXXX.", ".....X......XXX", "....X......X.XX", "...X......X..XX", "..X......XXX.XX",
      ".X......X....XX", "XXXXXXXX.....XX", "......X..XXX.XX", "......X......XX", "......X..XXX.XX",
      "......X......XX", "......X......XX", "......XXXXXXXXX" },
    { ".XXXXXXXXX.....", ".X.......X.....", ".X...X...XXX...", ".X..XXX..X.X...", ".X.XXXXX.X.X...",
      ".X.XXXXX.X.X...", ".X..X.X..X.X...", ".X...X...X.X...", ".X..XXX..X.X...", ".X.......X.X...",
      ".XXXXXXXXX.X...", "...XXXXXXXXX...", "..............." },
    { ".....XXXXX.....", "...XX.....XX...", "..X....X....X..", "..X....X....X..", ".X.....X.....X.",
      ".X.....X.....X.", ".X.....XXXXX.X.", ".X...........X.", ".X...........X.", "..X.........X..",
      "..X.........X..", "...XX.....XX...", ".....XXXXX....." },
    { ".......X.......", "....X.XXX.X....", "....XXXXXXX....", "..XXXXX.XXXXX..", "...XXXX.XXXX...",
      "..XXXXX.XXXXX..", ".XXXXXX.XXXXXX.", "..XXXXX.XXXXX..", "...XXXXXXXXX...", "..XXXXX.XXXXX..",
      "....XXXXXXX....", "....X.XXX.X....", ".......X......." },
};

static void draw_icon_row(int x0, int y0, int w, const UiTheme *t, unsigned lit)
{
    unsigned ghost = mix(t->paper, t->ink, 3);
    int k, r, c;
    for (k = 0; k < 4; k++) {
        unsigned colour = lit & (1u << k) ? t->ink : ghost;
        int cx = x0 + w * (2 * k + 1) / 8 - 15;
        for (r = 0; r < 13; r++)
            for (c = 0; c < 15; c++)
                if (vmu_icons[k][r][c] == 'X') ui_fill(cx + c * 2, y0 + r * 2, 2, 2, colour);
    }
}

static void draw_vmu(const unsigned char *lcd, const UiTheme *t, unsigned icons_lit, unsigned pressed)
{
    /* Laid out after photographs of the real one, without its connector and
     * squeezed in height so its screen can be four times the LCD. The screen's
     * window is nearly square (about 1.2:1 on a photograph of a VMU held
     * square): the LCD holds a row of icons (FILE, GAME, CLOCK, FLASH) under
     * the 48x32 dots, which the VMU drives itself (libElysianVMU, XRAM bank 2).
     * So: the dots at the top, square, 192x128 (the dot area is given as
     * 37 x 26 mm, its dots very nearly square), and the icon row under them
     * drawn unlit (draw_icon_row). */
    const int X = 8, Y = 4, W = 236, H = 264, WY = Y + 19;   /* WY: the screen window's top */
    unsigned body = t->body, lit = mix(body, 0xFFFFFFFF, 4), shade = mix(body, 0xFF000000, 2);
    unsigned edge = mix(body, 0xFF000000, 5), seam = mix(body, 0xFF000000, 3);
    unsigned ink = light(body) ? 0xFF5A5A5A : 0xFFB0B0B0;
    unsigned lav = light(body) ? 0xFFCAB8AC : mix(0xFFCAB8AC, 0xFF000000, 9);   /* the buttons' lavender grey */
    unsigned lav_lit = mix(lav, 0xFFFFFFFF, 4), lav_dark = mix(lav, 0xFF000000, 4);
    const int DX = X + 52, DY = Y + 231;                    /* the d-pad's centre */
    int i;

    /* the case: a shadow, its edge, the face lit from above */
    shape(X + 3, Y + 4, W, H, 14, 42, mix(t->bg, 0xFF000000, 8), mix(t->bg, 0xFF000000, 8));
    shape(X, Y, W, H, 14, 42, edge, edge);
    shape(X + 1, Y + 1, W - 2, H - 3, 13, 40, lit, shade);
    /* the front panel between two seams, and the band at the top */
    for (i = Y + 8; i < Y + H - 22; i++) {
        g_back[i * LINE + X + 9] = seam;  g_back[i * LINE + X + 10] = lit;
        g_back[i * LINE + X + W - 11] = seam; g_back[i * LINE + X + W - 10] = lit;
    }
    ui_fill(X + 10, Y + 14, W - 20, 1, seam);
    label(X + W / 2 - 45, Y + 4, ink, "VISUAL MEMORY");

    /* the screen in its stepped window, centred on the case */
    shape(X + 15, WY, 206, 172, 4, 4, mix(body, 0xFF000000, 4), mix(body, 0xFF000000, 2));   /* the step, shaded above */
    shape(X + 17, WY + 2, 202, 168, 3, 3, 0xFF7A8A86, 0xFF6A7A76);                         /* the glass's edge */
    ui_lcd_fit(lcd, X + 22, WY + 8, 192, 128, t);                                             /* the dots: 192x128 */
    ui_fill(X + 18, WY + 3, 200, 5, t->paper);                                                /* the glass round them, */
    ui_fill(X + 18, WY + 136, 200, 32, t->paper);                                             /* the icon row under them, */
    ui_fill(X + 18, WY + 3, 4, 165, t->paper);                                                /* and the sides */
    ui_fill(X + 214, WY + 3, 4, 165, t->paper);
    draw_icon_row(X + 22, WY + 140, 192, t, icons_lit);
    ui_fill(X + 18, WY + 3, 200, 2, mix(t->paper, 0xFF000000, 5));                           /* the glass's inner shadow */
    ui_fill(X + 18, WY + 3, 2, 165, mix(t->paper, 0xFF000000, 5));
    ui_fill(X + 15, WY + 172, 206, 1, lit);                                                   /* the window's lower lip */
    ui_fill(X + 10, WY + 176, W - 20, 1, seam);                                               /* the seam under the screen */

    /* the d-pad: a disc in a well, the cross moulded into it */
    disc(DX + 2, DY + 2, 31, mix(shade, 0xFF000000, 5));                                     /* the well */
    disc(DX, DY, 28, lav_dark);
    disc(DX - 1, DY - 1, 27, lav);
    /* the cross: its outline, a shadow under and right, the face lit from above */
    shape(DX - 11 + 1, DY - 25 + 2, 22, 50, 7, 7, lav_dark, lav_dark);
    shape(DX - 25 + 1, DY - 11 + 2, 50, 22, 7, 7, lav_dark, lav_dark);
    shape(DX - 11, DY - 25, 22, 50, 7, 7, mix(lav_dark, lav, 8), mix(lav_dark, lav, 8));
    shape(DX - 25, DY - 11, 50, 22, 7, 7, mix(lav_dark, lav, 8), mix(lav_dark, lav, 8));
    shape(DX - 10, DY - 24, 20, 48, 6, 6, lav_lit, lav);
    shape(DX - 24, DY - 10, 48, 20, 6, 6, mix(lav_lit, lav, 6), mix(lav_lit, lav, 10));
    ui_fill(DX - 10, DY - 10, 20, 20, mix(lav_lit, lav, 8));
    disc(DX, DY, 2, lav_dark);                                                                /* the dot */

    /* a held button is drawn sunk: darker, no light on it */
    #define FACE(bit) (pressed & (bit) ? lav_dark : lav)
    if (pressed & 0x0F) {                                       /* the d-pad's held arm */
        if (pressed & 0x01) ui_fill(DX - 10, DY - 24, 20, 14, lav_dark);
        if (pressed & 0x02) ui_fill(DX - 10, DY + 10, 20, 14, lav_dark);
        if (pressed & 0x04) ui_fill(DX - 24, DY - 10, 14, 20, lav_dark);
        if (pressed & 0x08) ui_fill(DX + 10, DY - 10, 14, 20, lav_dark);
    }

    /* SLEEP and MODE, small, labels above */
    label(X + 98, Y + 199, ink, "SLEEP");
    label(X + 142, Y + 199, ink, "MODE");
    disc(X + 116, Y + 215, 8, mix(shade, 0xFF000000, 4));
    disc(X + 116, Y + 215, 6, FACE(0x80));
    disc(X + 114, Y + 213, 2, lav_lit);
    disc(X + 156, Y + 215, 8, mix(shade, 0xFF000000, 4));
    disc(X + 156, Y + 215, 6, FACE(0x40));
    disc(X + 154, Y + 213, 2, lav_lit);

    /* A and B, low and to the right, labels below; B sits higher than A, by
     * about three quarters of a button's radius (photographs of real VMUs) */
    if (pressed & 0x10) disc(X + 136, Y + 238, 13, lav_dark); else button(X + 136, Y + 238, 13, lav, body);
    if (pressed & 0x20) disc(X + 186, Y + 228, 13, lav_dark); else button(X + 186, Y + 228, 13, lav, body);
    #undef FACE
    label(X + 133, Y + 254, ink, "A");
    label(X + 183, Y + 244, ink, "B");
}

static void draw_pad(const UiHome *h, const UiTheme *t, int x0, int y0)
{
    const Psp2dcInput *in = h->in;
    unsigned b = in ? in->buttons : 0, base = t->text;
    int sx = in ? in->stick_x : 128, sy = in ? in->stick_y : 128, l = in ? in->trigger_l : 0, r = in ? in->trigger_r : 0;
    /* L and R: bars that fill */
    ui_fill(x0 + 8, y0 + 22, 80, 8, dim(base));
    ui_fill(x0 + 8, y0 + 22, l * 80 / 255, 8, base);
    ui_fill(x0 + 122, y0 + 22, 80, 8, dim(base));
    ui_fill(x0 + 122 + 80 - r * 80 / 255, y0 + 22, r * 80 / 255, 8, base);
    label(x0 + 8, y0 + 12, base, "L");
    label(x0 + 196, y0 + 12, base, "R");
    /* the stick: a ring and where it is */
    ring(x0 + 40, y0 + 64, 24, 2, dim(base) | 0xFF000000);
    ring(x0 + 40, y0 + 64, 24, 1, base);
    disc(x0 + 40 + (sx - 128) * 18 / 128, y0 + 64 + (sy - 128) * 18 / 128, 7, base);
    /* where the stick comes from, under it, when it is not the nub */
    if (strstr(h->stick_source, "d-pad")) label(x0 + 5, y0 + 91, base, "from d-pad");
    else if (strstr(h->stick_source, "off")) label(x0 + 26, y0 + 92, half(base), "off");
    /* the d-pad */
    ui_fill(x0 + 28, y0 + 108, 26, 8, dim(base));
    ui_fill(x0 + 37, y0 + 99, 8, 26, dim(base));
    if (b & PSP2DC_BTN_UP)    ui_fill(x0 + 37, y0 + 99, 8, 9, base);
    if (b & PSP2DC_BTN_DOWN)  ui_fill(x0 + 37, y0 + 116, 8, 9, base);
    if (b & PSP2DC_BTN_LEFT)  ui_fill(x0 + 28, y0 + 108, 9, 8, base);
    if (b & PSP2DC_BTN_RIGHT) ui_fill(x0 + 45, y0 + 108, 9, 8, base);
    /* Start */
    round_rect(x0 + 92, y0 + 100, 26, 10, 5, b & PSP2DC_BTN_START ? base : dim(base));
    label(x0 + 87, y0 + 114, base, "START");
    /* A B X Y in the Dreamcast's colours */
    {
        static const struct { int bit, dx, dy; unsigned c; char ch; } k[] = {
            { PSP2DC_BTN_Y, 0, -18, 0xFF30C030, 'Y' }, { PSP2DC_BTN_X, -18, 0, 0xFF20C8F0, 'X' },
            { PSP2DC_BTN_B, 18, 0, 0xFFE06020, 'B' },  { PSP2DC_BTN_A, 0, 18, 0xFF2020E0, 'A' },
        };
        int i;
        for (i = 0; i < 4; i++) {
            int cx = x0 + 170 + k[i].dx, cy = y0 + 76 + k[i].dy, on = (b & k[i].bit) != 0;
            char s[2] = { k[i].ch, 0 };
            disc(cx, cy, 9, on ? k[i].c : dim(k[i].c));
            label(cx - 3, cy - 4, on ? 0xFF000000 : k[i].c, s);
        }
    }
}

void ui_page_home(const UiHome *h, const UiTheme *t)
{
    static SaveFile files[200];
    /* (ABGR) green connected, orange demo, yellow waiting, red nothing */
    unsigned dot = h->link == UI_LINK_UP ? 0xFF40D040 : h->link == UI_LINK_DEMO ? 0xFF288CF0
                 : h->link == UI_LINK_WAITING ? 0xFF28C8F0 : 0xFF3232DC;
    int x0 = 252, n = 0, free_blocks = 0;

    ui_begin(t->bg);
    draw_vmu(h->lcd, t, h->icons, h->vmu_pressed);

    /* the link */
    round_rect(x0, 10, 220, 30, 6, t->panel);
    disc(x0 + 16, 25, 6, dot);
    label(x0 + 30, 15, t->text, "PSP2DC");
    label(x0 + 30, 26, t->text, h->link_text);
    label(x0 + 212 - 7 * (int)strlen(t->name), 15, half(t->text), t->name);

    if (h->standalone) {   /* no Dreamcast: the PSP is the VMU's buttons */
        static const char *const map[] = { "d-pad     VMU d-pad", "Cross     A", "Circle    B",
                                           "Square    SLEEP", "Triangle  MODE" };
        int i;
        round_rect(x0, 46, 220, 146, 6, t->panel);
        label(x0 + 8, 52, t->text, "VMU ON ITS OWN");
        label(x0 + 212 - 7 * (int)strlen(h->standalone), 52, 0xFF28C8F0, h->standalone);
        for (i = 0; i < 5; i++) label(x0 + 12, 74 + i * 14, half(t->text), map[i]);
        label(x0 + 12, 154, half(t->text), "MODE: GAME > TIME > FILE");
        label(x0 + 12, 168, half(t->text), "File: up/down to browse");
        goto card_panel;
    }
    /* the controller */
    round_rect(x0, 46, 220, 146, 6, t->panel);
    label(x0 + 8, 52, t->text, "CONTROLLER");
    /* the stick's source: bright when it is not the nub, which is easy to miss */
    label(x0 + 212 - 7 * (int)strlen(h->stick_source), 52,
          strstr(h->stick_source, "nub") ? half(t->text) : 0xFF28C8F0, h->stick_source);
    draw_pad(h, t, x0 + 4, 50);

card_panel:
    /* the memory card */
    round_rect(x0, 198, 220, 50, 6, t->panel);
    label(x0 + 8, 204, t->text, "MEMORY CARD");
    if (h->card_have) {
        char s[40];
        n = card_files(h->card, files, 200, &free_blocks);
        snprintf(s, sizeof(s), "%d save%s, %d blocks free", n, n == 1 ? "" : "s", free_blocks);
        label(x0 + 8, 218, t->text, s);
        if (n && files[0].icons) {     /* the first save's icon, animated */
            int step = (files[0].speed > 0 ? files[0].speed : 8) * 1000 / 30;   /* n/30 s */
            draw_icon(h->card, &files[0], (h->now_ms / step) % files[0].icons, x0 + 186, 206, 1);
        }
    } else {
        label(x0 + 8, 218, t->text, "none yet");
    }
    label(x0 + 8, 232, half(t->text), h->notice ? "" : h->card_note);
    if (h->notice) label(x0 + 8, 232, t->text, h->notice);

    /* the keys */
    label(x0 + 4, 256, half(t->text), "SELECT + <> page   ^v theme");
    ui_end();
}
