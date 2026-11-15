/* See vmu_sa.h.
 *
 * The modes after the Sega VMU manual (VMU-7): GAME starts the executable
 * on the card (not yet: it needs an emulated VMU CPU), TIME shows the clock,
 * FILE shows the card's files. The screens' layout follows photographs of a
 * real VMU (FILE: "#001/001", the name over two lines, the date), in an 8x4
 * text grid of 6x8 cells; the font is drawn here, not Sega's. */
#include <pspkernel.h>
#include <psprtc.h>
#include <string.h>
#include <stdio.h>
#include "vmu_sa.h"

enum { MODE_GAME, MODE_TIME, MODE_FILE, MODES };
static int g_mode = MODE_TIME, g_sleep, g_file;
static const char *const g_names[MODES] = { "GAME", "TIME", "FILE" };

void sa_reset(void) { g_mode = MODE_TIME; g_sleep = 0; }
const char *sa_mode_name(void) { return g_sleep ? "SLEEP" : g_names[g_mode]; }

/* ---- the card's files (mc.pp.se flashmem; see ui.c) ---- */
typedef struct { char name[13]; unsigned char date[8]; int type; } SaFile;

static int files(const unsigned char *card, SaFile *out, int max)
{
    int blk, i, n = 0;
    if (!card) return 0;
    for (blk = 253; blk >= 241 && n < max; blk--)
        for (i = 0; i < 16 && n < max; i++) {
            const unsigned char *e = card + blk * 512 + i * 32;
            if (e[0] != 0x33 && e[0] != 0xCC) continue;
            out[n].type = e[0];
            memcpy(out[n].name, e + 4, 12);
            out[n].name[12] = 0;
            memcpy(out[n].date, e + 0x10, 8);
            n++;
        }
    return n;
}

void sa_input(unsigned pressed, const unsigned char *card)
{
    SaFile f[200];
    int n;
    if (pressed & SA_SLEEP) { g_sleep = !g_sleep; return; }
    if (g_sleep) { if (pressed) g_sleep = 0; return; }   /* any button wakes it */
    if (pressed & SA_MODE) { g_mode = (g_mode + 1) % MODES; g_file = 0; return; }
    if (g_mode == MODE_FILE) {
        n = files(card, f, 200);
        if (pressed & (SA_DOWN | SA_RIGHT)) g_file++;
        if (pressed & (SA_UP | SA_LEFT)) g_file--;
        if (g_file >= n) g_file = 0;
        if (g_file < 0) g_file = n ? n - 1 : 0;
    }
}

/* ---- drawing on the 48x32 screen ---- */

/* 5x7 letters in 6x8 cells; a row a byte, bit 4 the leftmost */
static const struct { char c; unsigned char rows[7]; } font[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},   {'2',{14,17,1,2,4,8,31}},
    {'3',{31,2,4,2,1,17,14}},     {'4',{2,6,10,18,31,2,2}},  {'5',{31,16,30,1,1,17,14}},
    {'6',{6,8,16,30,17,17,14}},   {'7',{31,1,2,4,8,8,8}},    {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,2,12}},   {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{28,18,17,17,17,18,28}}, {'E',{31,16,16,30,16,16,31}},
    {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}},      {'J',{7,2,2,2,2,18,12}},   {'K',{17,18,20,24,20,18,17}},
    {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}}, {'N',{17,17,25,21,19,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}},
    {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,21,10}},
    {'X',{17,17,10,4,10,17,17}},  {'Y',{17,17,10,4,4,4,4}},  {'Z',{31,1,2,4,8,16,31}},
    {'#',{10,10,31,10,31,10,10}}, {'/',{1,1,2,4,8,16,16}},   {':',{0,12,12,0,12,12,0}},
    {'_',{0,0,0,0,0,0,31}},       {'.',{0,0,0,0,0,12,12}},   {'-',{0,0,0,31,0,0,0}},
};

/* a dot at (x, y) as the player sees it, stored as the Dreamcast sends it
 * (turned half round) so ui_lcd draws it the right way up */
static void dot(unsigned char *lcd, int x, int y)
{
    int sx = 47 - x, sy = 31 - y;
    if (x < 0 || y < 0 || x > 47 || y > 31) return;
    lcd[sy * 6 + sx / 8] |= 0x80 >> (sx & 7);
}

static void text(unsigned char *lcd, int col, int row, const char *s)
{
    for (; *s && col < 8; s++, col++) {
        unsigned i, r, c, ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        for (i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
            if (font[i].c != (char)ch) continue;
            for (r = 0; r < 7; r++)
                for (c = 0; c < 5; c++)
                    if (font[i].rows[r] & (16 >> c)) dot(lcd, col * 6 + c, row * 8 + r);
            break;
        }
    }
}

static int bcd(unsigned char v) { return (v >> 4) * 10 + (v & 15); }

unsigned sa_render(unsigned char lcd[PSP2DC_LCD_BYTES], const unsigned char *card)
{
    char line[40];
    memset(lcd, 0, PSP2DC_LCD_BYTES);
    if (g_sleep) return 0;                 /* screen and icons off */
    if (g_mode == MODE_TIME) {
        ScePspDateTime t;
        static const char *const days[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
        memset(&t, 0, sizeof(t));
        sceRtcGetCurrentClockLocalTime(&t);
        snprintf(line, sizeof(line), "  %04d", t.year);                       text(lcd, 0, 0, line);
        snprintf(line, sizeof(line), " %02d/%02d", t.month, t.day);           text(lcd, 0, 1, line);
        snprintf(line, sizeof(line), "%02d:%02d:%02d", t.hour, t.minute, t.second); text(lcd, 0, 2, line);
        snprintf(line, sizeof(line), "   %s", days[sceRtcGetDayOfWeek(t.year, t.month, t.day) % 7]);
        text(lcd, 0, 3, line);
        return SA_ICON_CLOCK;
    }
    if (g_mode == MODE_FILE) {
        SaFile f[200];
        int n = files(card, f, 200);
        if (!n) { text(lcd, 0, 1, card ? "NO FILES" : "NO CARD"); return SA_ICON_FILE; }
        if (g_file >= n) g_file = 0;
        snprintf(line, sizeof(line), "#%03d/%03d", g_file + 1, n);            text(lcd, 0, 0, line);
        memcpy(line, f[g_file].name, 8); line[8] = 0;                         text(lcd, 0, 1, line);
        memcpy(line, f[g_file].name + 8, 4); line[4] = 0;                     text(lcd, 0, 2, line);
        snprintf(line, sizeof(line), "%02d/%02d/%02d", bcd(f[g_file].date[1]), bcd(f[g_file].date[2]),
                 bcd(f[g_file].date[3]));
        text(lcd, 0, 3, line);
        return SA_ICON_FILE;
    }
    /* GAME: the card's executable, which cannot run yet */
    {
        SaFile f[200];
        int n = files(card, f, 200), i;
        for (i = 0; i < n && f[i].type != 0xCC; i++) ;
        text(lcd, 0, 0, "  GAME");
        if (i < n) { memcpy(line, f[i].name, 8); line[8] = 0; text(lcd, 0, 2, line); text(lcd, 0, 3, "NOT YET"); }
        else text(lcd, 0, 2, "NO GAME");
        return SA_ICON_GAME;
    }
}
