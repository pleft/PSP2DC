/* See companion.h. */
#include <string.h>
#include <pspkernel.h>
#include "companion.h"
#include "comp_files.h"

Comp g_comp;
extern unsigned char msx[];            /* the debug screen's font: 8 bytes a character, the top bit leftmost */

static unsigned char rec[sizeof(Psp2dcCompRecord) + PSP2DC_COMP_DATA_MAX];
static int have, dead;                 /* dead: the stream made no sense, until the next session */
static unsigned test_next;
static unsigned char echo[PSP2DC_COMP_EVENT_MAX];
static volatile int echo_len, caps_asked;
static unsigned pool[2 * PSP2DC_REGION_PIXELS];   /* the regions' pixels, one after another */
static unsigned char sprite_pool[PSP2DC_SPRITE_BYTES];   /* the sprites' pixels, in the order they were made */

static int rd16(const unsigned char *p) { return (short)(p[0] | p[1] << 8); }
static unsigned rgb(const unsigned char *p)        /* RGB565 to the screen's ABGR */
{
    unsigned c = p[0] | p[1] << 8, r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    return 0xFF000000 | (b << 3 | b >> 2) << 16 | (g << 2 | g >> 4) << 8 | (r << 3 | r >> 2);
}
static void text_in(char *dst, const unsigned char *src, int n, int max, int lines)
{
    int i;
    if (n > max) n = max;
    for (i = 0; i < n; i++) dst[i] = (src[i] >= 0x20 && src[i] <= 0x7E) || (lines && src[i] == '\n') ? (char)src[i] : '?';
    dst[n] = 0;
}

/* ---- regions ---------------------------------------------------------------- */
static void palette_default(CompRegion *r);
static void regions_layout(void)       /* each one's place in the pool; all of them black */
{
    unsigned off = 0;
    int i;
    for (i = 0; i < PSP2DC_REGIONS; i++) {
        CompRegion *r = &g_comp.regions[i];
        if (!r->w) continue;
        r->back = pool + off;
        r->front = r->back + r->w * r->h;
        off += 2u * r->w * r->h;
    }
    for (i = 0; i < (int)off; i++) pool[i] = 0xFF000000;
}
static void region_make(int id, int x, int y, int w, int h)
{
    CompRegion *r = &g_comp.regions[id];
    unsigned total = 0;
    int i;
    r->w = 0;
    for (i = 0; i < PSP2DC_REGIONS; i++) total += (unsigned)g_comp.regions[i].w * g_comp.regions[i].h;
    if (w > 0 && h > 0) {
        if (w > PSP2DC_SCREEN_W || h > PSP2DC_SCREEN_H || total + (unsigned)w * h > PSP2DC_REGION_PIXELS) {
            g_comp.screen_bad++;
        } else {
            r->x = (short)x; r->y = (short)y; r->w = (unsigned short)w; r->h = (unsigned short)h;
            r->cx0 = r->cy0 = 0; r->cx1 = (short)w; r->cy1 = (short)h;
            palette_default(r);
        }
    }
    regions_layout();
}
static void region_fill(CompRegion *r, int x, int y, int w, int h, unsigned c)
{
    int x1 = x + w, y1 = y + h, i;
    if (x < r->cx0) x = r->cx0;
    if (y < r->cy0) y = r->cy0;
    if (x1 > r->cx1) x1 = r->cx1;
    if (y1 > r->cy1) y1 = r->cy1;
    for (; y < y1; y++) {
        unsigned *p = r->back + y * r->w;
        for (i = x; i < x1; i++) p[i] = c;
    }
}
static void region_line(CompRegion *r, int x0, int y0, int x1, int y1, unsigned c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, e2;
    for (;;) {
        if (x0 >= r->cx0 && x0 < r->cx1 && y0 >= r->cy0 && y0 < r->cy1) r->back[y0 * r->w + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void region_text(CompRegion *r, int x, int y, unsigned c, int size, const unsigned char *s, int n)
{
    int i, row, col;
    if (size < 1) size = 1;
    if (size > 4) size = 4;
    for (i = 0; i < n; i++, x += 7 * size) {
        const unsigned char *g = msx + (s[i] >= 0x20 && s[i] <= 0x7E ? s[i] : '?') * 8;
        if (x >= r->cx1) break;
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if (g[row] & (0x80 >> col)) region_fill(r, x + col * size, y + row * size, size, size, c);
    }
}

/* ---- pictures ----------------------------------------------------------------- */
static void palette_default(CompRegion *r)
{
    int i;
    for (i = 0; i < 256; i++)
        r->pal[i] = 0xFF000000 | ((i & 3) * 85) << 16 | (((i >> 2) & 7) * 255 / 7) << 8 | ((i >> 5) * 255 / 7);
}
/* pixel i of a row as it is sent: its index, or with RGB565 its colour */
static unsigned pixel_in(const unsigned char *row, int format, int i)
{
    switch (format) {
    case PSP2DC_FMT_1: return (row[i >> 3] >> (7 - (i & 7))) & 1;
    case PSP2DC_FMT_4: return (row[i >> 1] >> (i & 1 ? 0 : 4)) & 15;
    case PSP2DC_FMT_8: return row[i];
    default:           return row[2 * i] | row[2 * i + 1] << 8;
    }
}
static unsigned colour_of(const CompRegion *r, int format, unsigned v)
{
    unsigned char c[2];
    if (format != PSP2DC_FMT_565) return r->pal[v & 255];
    c[0] = (unsigned char)v; c[1] = (unsigned char)(v >> 8);
    return rgb(c);
}
static void region_blit(CompRegion *r, int x, int y, int w, int h, int format, const unsigned char *d)
{
    int row = PSP2DC_FMT_ROW(format, w), i, j;
    for (j = 0; j < h; j++, d += row) {
        if (y + j < r->cy0 || y + j >= r->cy1) continue;
        for (i = 0; i < w; i++)
            if (x + i >= r->cx0 && x + i < r->cx1)
                r->back[(y + j) * r->w + x + i] = colour_of(r, format, pixel_in(d, format, i));
    }
}
/* rows of a sprite; 1 when they made sense */
static int sprite_rows(const unsigned char *d, int len)
{
    Psp2dcCompSprite h;
    CompSprite *sp;
    int row, rows, i, j, stored;
    if (len < (int)sizeof(h)) return 0;
    memcpy(&h, d, sizeof(h));
    if (h.id >= PSP2DC_SPRITES || h.format > PSP2DC_FMT_565 || !h.w || !h.h || h.w > 1024 || h.h > 1024) return 0;
    sp = &g_comp.sprites[h.id];
    stored = h.format == PSP2DC_FMT_565 ? PSP2DC_FMT_565 : PSP2DC_FMT_8;
    if (h.y == 0 && (sp->w != h.w || sp->h != h.h || sp->format != stored)) {   /* a new one: its place */
        unsigned bytes = (unsigned)h.w * h.h * (stored == PSP2DC_FMT_565 ? 2 : 1);
        sp->w = 0;
        if (bytes > PSP2DC_SPRITE_BYTES - g_comp.sprite_bytes) return 0;
        sp->pixels = sprite_pool + g_comp.sprite_bytes;
        g_comp.sprite_bytes += bytes;
        sp->w = h.w; sp->h = h.h; sp->format = (unsigned char)stored;
        memset(sp->pixels, 0, bytes);
    }
    if (sp->w != h.w || sp->h != h.h || sp->format != stored) return sp->w == 0;   /* (its start was lost with a session) */
    row = PSP2DC_FMT_ROW(h.format, h.w);
    rows = (len - (int)sizeof(h)) / row;
    d += sizeof(h);
    for (j = 0; j < rows && h.y + j < h.h; j++, d += row)
        for (i = 0; i < h.w; i++) {
            unsigned v = pixel_in(d, h.format, i);
            if (stored == PSP2DC_FMT_8) sp->pixels[(h.y + j) * h.w + i] = (unsigned char)v;
            else { sp->pixels[2 * ((h.y + j) * h.w + i)] = (unsigned char)v; sp->pixels[2 * ((h.y + j) * h.w + i) + 1] = (unsigned char)(v >> 8); }
        }
    return 1;
}
static void sprite_draw(CompRegion *r, const CompSprite *sp, int x, int y, int keyed, unsigned key)
{
    int i, j;
    for (j = 0; j < sp->h; j++) {
        if (y + j < r->cy0 || y + j >= r->cy1) continue;
        for (i = 0; i < sp->w; i++) {
            unsigned v = pixel_in(sp->pixels + (unsigned)j * sp->w * (sp->format == PSP2DC_FMT_565 ? 2 : 1), sp->format, i);
            if (x + i < r->cx0 || x + i >= r->cx1 || (keyed && v == key)) continue;
            r->back[(y + j) * r->w + x + i] = colour_of(r, sp->format, v);
        }
    }
}

/* ---- records ---------------------------------------------------------------- */
static void screen_changed(void)
{
    int i, on = 0;
    for (i = 0; i < PSP2DC_WIDGETS && !on; i++) on = g_comp.widgets[i].used;
    for (i = 0; i < PSP2DC_REGIONS && !on; i++) on = g_comp.regions[i].w != 0;
    g_comp.screen = on;
    g_comp.gen++;
}
static void screen_reset(void)
{
    memset(g_comp.widgets, 0, sizeof(g_comp.widgets));
    memset(g_comp.regions, 0, sizeof(g_comp.regions));
    memset(g_comp.sprites, 0, sizeof(g_comp.sprites));
    g_comp.sprite_bytes = 0;
    g_comp.notice_us = 0;
    screen_changed();
}

/* a record of the screen: 1 when it made sense */
static int screen_record(int type, const unsigned char *d, int len)
{
    if (type == PSP2DC_REC_RESET) { screen_reset(); return 1; }
    if (type == PSP2DC_REC_NOTIFY) {
        text_in(g_comp.notice, d, len, (int)sizeof(g_comp.notice) - 1, 0);
        g_comp.notice_us = sceKernelGetSystemTimeLow() | 1;
        g_comp.gen++;
        return 1;
    }
    if (len < 1) return 0;
    if (type >= PSP2DC_REC_WIDGET && type <= PSP2DC_REC_REMOVE) {
        CompWidget *w;
        if (d[0] >= PSP2DC_WIDGETS) return 0;
        w = &g_comp.widgets[d[0]];
        if (type == PSP2DC_REC_WIDGET) {
            if (len < (int)sizeof(Psp2dcCompWidget) || d[1] > PSP2DC_WIDGET_LIST) return 0;
            w->type = d[1]; w->flags = d[2];
            w->x = (short)rd16(d + 4); w->y = (short)rd16(d + 6);
            w->w = (unsigned short)rd16(d + 8); w->h = (unsigned short)rd16(d + 10);
            w->colour = (unsigned short)rd16(d + 12);
            w->value = (int)(d[14] | d[15] << 8 | d[16] << 16 | (unsigned)d[17] << 24);
            w->max = (int)(d[18] | d[19] << 8 | d[20] << 16 | (unsigned)d[21] << 24);
            text_in(w->text, d + sizeof(Psp2dcCompWidget), len - (int)sizeof(Psp2dcCompWidget), PSP2DC_WIDGET_TEXT_MAX, 1);
            w->used = 1;
        } else if (!w->used) {
            return 1;                  /* (not there: left over from before the PSP started over) */
        } else if (type == PSP2DC_REC_VALUE) {
            if (len < 5) return 0;
            w->value = (int)(d[1] | d[2] << 8 | d[3] << 16 | (unsigned)d[4] << 24);
        } else if (type == PSP2DC_REC_TEXT) {
            text_in(w->text, d + 1, len - 1, PSP2DC_WIDGET_TEXT_MAX, 1);
        } else {
            w->used = 0;
        }
        screen_changed();
        return 1;
    }
    if (type == PSP2DC_REC_SPRITE) return sprite_rows(d, len);
    {   /* a region's */
        CompRegion *r;
        if (d[0] >= PSP2DC_REGIONS) return 0;
        r = &g_comp.regions[d[0]];
        if (type == PSP2DC_REC_REGION) {
            if (len < (int)sizeof(Psp2dcCompRect)) return 0;
            region_make(d[0], rd16(d + 1), rd16(d + 3), (unsigned short)rd16(d + 5), (unsigned short)rd16(d + 7));
            screen_changed();
            return 1;
        }
        if (!r->w) return 1;           /* (the same) */
        if (type == PSP2DC_REC_PRESENT) {
            memcpy(r->front, r->back, (unsigned)r->w * r->h * sizeof(unsigned));
            g_comp.gen++;
        } else if (type == PSP2DC_REC_DRAWTEXT) {
            if (len < 8) return 0;
            region_text(r, rd16(d + 1), rd16(d + 3), rgb(d + 5), d[7], d + 8, len - 8);
        } else if (type == PSP2DC_REC_PALETTE) {
            int i, first;
            if (len < 2) return 0;
            for (i = 0, first = d[1]; 2 + 2 * i + 1 < len && first + i < 256; i++) r->pal[first + i] = rgb(d + 2 + 2 * i);
        } else if (type == PSP2DC_REC_DRAWSPRITE) {
            const CompSprite *sp;
            if (len < 9 || d[1] >= PSP2DC_SPRITES) return 0;
            sp = &g_comp.sprites[d[1]];
            if (sp->w) sprite_draw(r, sp, rd16(d + 2), rd16(d + 4), d[6], d[7] | d[8] << 8);
        } else if (len < (int)sizeof(Psp2dcCompRect)) {
            return 0;
        } else if (type == PSP2DC_REC_CLIP) {
            int x0 = rd16(d + 1), y0 = rd16(d + 3), x1 = x0 + (unsigned short)rd16(d + 5), y1 = y0 + (unsigned short)rd16(d + 7);
            r->cx0 = (short)(x0 < 0 ? 0 : x0 > r->w ? r->w : x0);
            r->cy0 = (short)(y0 < 0 ? 0 : y0 > r->h ? r->h : y0);
            r->cx1 = (short)(x1 < r->cx0 ? r->cx0 : x1 > r->w ? r->w : x1);
            r->cy1 = (short)(y1 < r->cy0 ? r->cy0 : y1 > r->h ? r->h : y1);
        } else if (len < (int)sizeof(Psp2dcCompRect) + 2) {
            return 0;
        } else if (type == PSP2DC_REC_BLIT) {
            int w = (unsigned short)rd16(d + 5), h = (unsigned short)rd16(d + 7), format = d[9];
            if (format > PSP2DC_FMT_565 || w > PSP2DC_COMP_DATA_MAX || h > PSP2DC_COMP_DATA_MAX ||
                len < 10 + PSP2DC_FMT_ROW(format, w) * h) return 0;
            region_blit(r, rd16(d + 1), rd16(d + 3), w, h, format, d + 10);
        } else if (type == PSP2DC_REC_FILL) {
            region_fill(r, rd16(d + 1), rd16(d + 3), (unsigned short)rd16(d + 5), (unsigned short)rd16(d + 7), rgb(d + 9));
        } else {
            region_line(r, rd16(d + 1), rd16(d + 3), rd16(d + 5), rd16(d + 7), rgb(d + 9));
        }
        return 1;
    }
}

void comp_reset(void)
{
    have = dead = 0;
    test_next = 0;
    echo_len = 0;
    caps_asked = 0;
    g_comp.bytes = g_comp.records = 0;
    g_comp.test_records = g_comp.test_bytes = g_comp.test_bad = 0;
    g_comp.sessions++;
    screen_reset();
    comp_files_reset();
}

static void record(int type, const unsigned char *d, int len)
{
    g_comp.records++;
    if (type == PSP2DC_REC_CLEAR) {
        memset(g_comp.rows, 0, sizeof(g_comp.rows));
        g_comp.test_records = g_comp.test_bytes = g_comp.test_bad = 0;
        test_next = 0;
    } else if (type == PSP2DC_REC_PRINT && len >= 1 && d[0] < COMP_ROWS) {
        text_in(g_comp.rows[d[0]], d + 1, len - 1, COMP_COLS, 0);
    } else if (type == PSP2DC_REC_TEST && len >= 4) {
        unsigned number = d[0] | d[1] << 8 | d[2] << 16 | (unsigned)d[3] << 24;
        int i, bad = number != test_next;
        for (i = 4; i < len && !bad; i++) bad = d[i] != ((number + i - 4) & 0xFF);
        if (bad) g_comp.test_bad++;
        test_next = number + 1;
        g_comp.test_records++;
        g_comp.test_bytes += len;
    } else if (type == PSP2DC_REC_ECHO && len <= PSP2DC_COMP_EVENT_MAX - (int)sizeof(Psp2dcCompRecord)) {
        echo_len = 0;                  /* (one at a time: a second before the first went replaces it) */
        echo[0] = PSP2DC_REC_ECHO; echo[1] = (unsigned char)len; echo[2] = 0;
        memcpy(echo + sizeof(Psp2dcCompRecord), d, len);
        echo_len = (int)sizeof(Psp2dcCompRecord) + len;
    } else if (type == PSP2DC_REC_QUERY) {
        caps_asked = 1;
    } else if (type == PSP2DC_REC_RESET || (type >= PSP2DC_REC_WIDGET && type <= PSP2DC_REC_NOTIFY) ||
               (type >= PSP2DC_REC_REGION && type <= PSP2DC_REC_DRAWSPRITE)) {
        if (!screen_record(type, d, len)) g_comp.screen_bad++;
    } else if (type >= PSP2DC_REC_FOLDER && type <= PSP2DC_REC_LIST) {
        comp_files_record(type, d, len);   /* (done on a thread of its own: comp_files.h) */
    }
}

void comp_feed(const unsigned char *p, int n)
{
    g_comp.bytes += n;
    g_comp.last_us = sceKernelGetSystemTimeLow() | 1;
    while (n > 0 && !dead) {
        int want = have < (int)sizeof(Psp2dcCompRecord) ? (int)sizeof(Psp2dcCompRecord)
                                                        : (int)sizeof(Psp2dcCompRecord) + (rec[1] | rec[2] << 8);
        int take = want - have < n ? want - have : n;
        memcpy(rec + have, p, take);
        have += take; p += take; n -= take;
        if (have < (int)sizeof(Psp2dcCompRecord)) break;
        if ((rec[1] | rec[2] << 8) > PSP2DC_COMP_DATA_MAX) { dead = 1; g_comp.stream_bad++; break; }
        if (have == (int)sizeof(Psp2dcCompRecord) + (rec[1] | rec[2] << 8)) {
            record(rec[0], rec + sizeof(Psp2dcCompRecord), have - (int)sizeof(Psp2dcCompRecord));
            have = 0;
        }
    }
}

int comp_event_take(unsigned char out[PSP2DC_COMP_EVENT_MAX])
{
    int n = echo_len;
    if (caps_asked) {
        Psp2dcCompCaps c;
        memset(&c, 0, sizeof(c));
        c.level = 4;
        c.widgets = PSP2DC_WIDGETS;
        c.regions = PSP2DC_REGIONS;
        c.text_max = PSP2DC_WIDGET_TEXT_MAX;
        c.width = PSP2DC_SCREEN_W;
        c.height = PSP2DC_SCREEN_H;
        c.region_pixels = PSP2DC_REGION_PIXELS;
        c.widget_types = (1u << (PSP2DC_WIDGET_LIST + 1)) - 1;
        c.formats = (1u << (PSP2DC_FMT_565 + 1)) - 1;
        c.sprites = PSP2DC_SPRITES;
        c.sprite_bytes = PSP2DC_SPRITE_BYTES;
        c.files_max = PSP2DC_FILES_MAX;
        out[0] = PSP2DC_REC_CAPS; out[1] = (unsigned char)sizeof(c); out[2] = 0;
        memcpy(out + sizeof(Psp2dcCompRecord), &c, sizeof(c));
        caps_asked = 0;
        return (int)(sizeof(Psp2dcCompRecord) + sizeof(c));
    }
    if (!n) return 0;
    memcpy(out, echo, n);
    echo_len = 0;
    return n;
}
