/* See comp_screen.h. */
#include <stdio.h>
#include <string.h>
#include "comp_screen.h"

extern unsigned char msx[];            /* the debug screen's font */
static unsigned *g_fb;
static int g_stride;

static void fill(int x, int y, int w, int h, unsigned c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PSP2DC_SCREEN_W) w = PSP2DC_SCREEN_W - x;
    if (y + h > PSP2DC_SCREEN_H) h = PSP2DC_SCREEN_H - y;
    for (j = 0; j < h; j++) {
        unsigned *p = g_fb + (y + j) * g_stride + x;
        for (i = 0; i < w; i++) p[i] = c;
    }
}
/* how far a corner of radius r is cut in, d lines from the rectangle's edge */
static int inset(int r, int d)
{
    int k = 0, dy = r - d;
    while (k < r && (r - k) * (r - k) + dy * dy > r * r + r) k++;
    return k;
}
static void round_rect(int x, int y, int w, int h, int r, unsigned c)
{
    int j;
    if (w <= 0 || h <= 0) return;
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    for (j = 0; j < h; j++) {
        int d = j < r ? j : j >= h - r ? h - 1 - j : r, k = d < r ? inset(r, d) : 0;
        fill(x + k, y + j, w - 2 * k, 1, c);
    }
}
/* n characters, each 7 * size wide; no further right than x1 */
static void text(int x, int y, int size, unsigned c, const char *s, int n, int x1)
{
    int i, row, col;
    for (i = 0; i < n && x + 7 * size <= x1; i++, x += 7 * size) {
        const unsigned char *g = msx + (unsigned char)(s[i] >= 0x20 && s[i] <= 0x7E ? s[i] : '?') * 8;
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if (g[row] & (0x80 >> col)) fill(x + col * size, y + row * size, size, size, c);
    }
}
static int line_len(const char *s) { int n = 0; while (s[n] && s[n] != '\n') n++; return n; }

static void widget(const CompWidget *w, const CompColours *col)
{
    int x = w->x, y = w->y, ww = w->w, h = w->h;
    unsigned own = 0xFF000000 | (((w->colour & 31) << 3 | (w->colour & 31) >> 2) << 16)
                 | ((((w->colour >> 5) & 63) << 2 | ((w->colour >> 5) & 63) >> 4) << 8)
                 | ((w->colour >> 11) << 3 | (w->colour >> 11) >> 2);
    int has = w->flags & PSP2DC_WF_COLOUR, big = w->flags & PSP2DC_WF_BIG ? 2 : 1;
    int n = line_len(w->text);
    char num[32];

    if (w->flags & PSP2DC_WF_PANEL) {
        round_rect(x, y, ww, h, 5, col->panel);
        x += 6; ww -= 12;
        if (h > 14) { y += 3; h -= 6; }
    }
    if (ww <= 0 || h <= 0) return;
    switch (w->type) {
    case PSP2DC_WIDGET_LABEL: {
        int fit = ww / (7 * big), tw;
        if (n > fit) n = fit;
        tw = n * 7 * big;
        text(w->flags & PSP2DC_WF_CENTRE ? x + (ww - tw) / 2 : w->flags & PSP2DC_WF_RIGHT ? x + ww - tw : x,
             y + (h > 8 * big ? (h - 8 * big) / 2 : 0), big, has ? own : col->text, w->text, n, x + ww);
        break;
    }
    case PSP2DC_WIDGET_NUMBER: {
        int vn = snprintf(num, sizeof(num), "%d", w->value), vw = vn * 7 * big;
        text(x, y + (h > 8 ? (h - 8) / 2 : 0), 1, col->muted, w->text, n, x + ww - vw - 7);
        text(x + ww - vw, y + (h > 8 * big ? (h - 8 * big) / 2 : 0), big, has ? own : col->strong, num, vn, x + ww);
        break;
    }
    case PSP2DC_WIDGET_BAR: {
        int max = w->max > 0 ? w->max : 1, v = w->value < 0 ? 0 : w->value > max ? max : w->value;
        int by = y, bh = h, full;
        if (h >= 20) {                             /* the text and the numbers above the bar */
            int vn = snprintf(num, sizeof(num), "%d/%d", v, max);
            text(x, y, 1, col->muted, w->text, n, x + ww - vn * 7 - 7);
            text(x + ww - vn * 7, y, 1, col->text, num, vn, x + ww);
            by = y + 12; bh = h - 12;
        }
        full = (int)((long long)ww * v / max);
        round_rect(x, by, ww, bh, 3, col->faint);
        if (full > 0) round_rect(x, by, full < 6 ? 6 : full, bh, 3, has ? own : col->accent);
        break;
    }
    case PSP2DC_WIDGET_LIGHT:
        round_rect(x, y + h / 2 - 4, 9, 9, 4, w->value ? (has ? own : col->ok) : col->faint);
        text(x + 15, y + (h > 8 ? (h - 8) / 2 : 0), 1, w->value ? col->text : col->muted, w->text, n, x + ww);
        break;
    case PSP2DC_WIDGET_LIST: {
        int rows = h / 12 > 0 ? h / 12 : 1, first = w->value >= 0 ? w->value / rows * rows : 0, i = 0;
        const char *s = w->text;
        while (*s && i < first + rows) {           /* (a page at a time: the page the marked line is on) */
            int len = line_len(s);
            if (i >= first) {
                int ry = y + (i - first) * 12, on = i == w->value;
                if (on) round_rect(x, ry, ww, 12, 3, col->accent);
                text(x + 4, ry + 2, 1, on ? col->on_accent : has ? own : col->text, s, len, x + ww - 2);
            }
            s += len;
            if (*s == '\n') s++;
            i++;
        }
        break;
    }
    }
}

void comp_screen_draw(const Comp *c, unsigned *fb, int stride, const CompColours *col, unsigned now_us)
{
    int i, j;
    g_fb = fb; g_stride = stride;
    for (i = 0; i < PSP2DC_REGIONS; i++) {
        const CompRegion *r = &c->regions[i];
        int x0 = r->x < 0 ? -r->x : 0, x1 = r->x + r->w > PSP2DC_SCREEN_W ? PSP2DC_SCREEN_W - r->x : r->w;
        if (!r->w || x1 <= x0) continue;
        for (j = 0; j < r->h; j++) {
            int y = r->y + j;
            if (y < 0 || y >= PSP2DC_SCREEN_H) continue;
            memcpy(fb + y * stride + r->x + x0, r->front + j * r->w + x0, (size_t)(x1 - x0) * sizeof(unsigned));
        }
    }
    for (i = 0; i < PSP2DC_WIDGETS; i++)
        if (c->widgets[i].used) widget(&c->widgets[i], col);
    if (c->notice_us && now_us - c->notice_us < 2500000) {   /* a NOTIFY: one line, framed, in the middle */
        int n = (int)strlen(c->notice), w = 7 * n + 40, x = PSP2DC_SCREEN_W / 2 - w / 2;
        round_rect(x - 2, 118, w + 4, 34, 8, col->accent);
        round_rect(x, 120, w, 30, 6, col->panel);
        text(x + 20, 131, 1, col->strong, c->notice, n, x + w);
        text(x + 21, 131, 1, col->strong, c->notice, n, x + w + 1);
    }
}
