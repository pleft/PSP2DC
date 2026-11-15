/* See vmu_sa.h.
 *
 * The modes after the Sega VMU manual (VMU-7): GAME starts the executable
 * on the card (not yet: it needs an emulated VMU CPU), TIME shows the clock,
 * FILE shows the card's files. GAME runs the card's game on an emulated VMU
 * (vmu_game.c): A starts it; it is left when the game hands back to the
 * BIOS (MODE, in most games), or with MODE held 2 s whatever the game does.
 * The screens' layout follows photographs of a
 * real VMU (FILE: "#001/001", the name over two lines, the date), in an 8x4
 * text grid of 6x8 cells; the font is drawn here, not Sega's.
 *
 * FILE follows the VMU's instruction sheet (Sega 670-14102A, "File Mode",
 * "Managing Files"): the free blocks ("Free 200", "Game 128"), A or right to
 * the files (up/down pick one), right to its comment, right to "Copy" /
 * "Delete", B or left back a screen. A VMU copies to a second VMU plugged
 * into it; here the other VMU is another card in ms0:/PSP2DC/cards, picked
 * before the sheet's "Copy ?". Its errors are the sheet's: "Same file
 * exist", "Not enough free memory", "Copy failed". A copy-protected file's
 * name is shown inverted and it cannot be copied. The sheet shows no delete
 * screens: "Delete ?" mirrors "Copy ?", with No first. */
#include <pspkernel.h>
#include <psprtc.h>
#include <string.h>
#include <stdio.h>
#include "vmu_sa.h"
#include "vmu_game.h"
#include "cards.h"
#include <strings.h>

enum { MODE_GAME, MODE_TIME, MODE_FILE, MODES };
static int g_mode = MODE_TIME, g_sleep, g_file;
/* FILE's screens, in the sheet's order, then the copy and delete steps */
enum { FS_FREE, FS_LIST, FS_COMMENT, FS_ACTION, FS_TARGET, FS_ASK, FS_BUSY, FS_DONE };
static int g_fs, g_act, g_yes, g_target, g_ntargets, g_busy_drawn;
static const char *g_result[4];            /* FS_DONE's lines */
static char g_done_name[13];               /* and the file's name over them, if any */
static const char *g_card_name = "";
static char g_targets[CARDS_MAX][CARD_NAME_MAX];
static unsigned char g_other[256 * 512];   /* the card copied to; a game's flash going to its own card */
static char g_game_card[CARD_NAME_MAX];    /* the card the running game was started from */
static unsigned g_mode_held_ms;            /* MODE held while a game runs */
#define LEAVE_GAME_MS 2000
static const char *const g_names[MODES] = { "GAME", "TIME", "FILE" };

void sa_reset(void)
{
    g_mode = MODE_TIME; g_sleep = 0;
    g_fs = FS_FREE; g_file = g_act = g_yes = g_target = g_ntargets = 0;
    g_mode_held_ms = 0;
}
void sa_card_name(const char *name) { g_card_name = name ? name : ""; }

int sa_stop(unsigned char *card)
{
    if (!game_running()) return 0;
    g_mode_held_ms = 0;
    if (!card) { game_stop(g_other); return 0; }
    return game_stop(card);
}
const char *sa_mode_name(void) { return game_running() ? "PLAYING" : g_sleep ? "SLEEP" : g_names[g_mode]; }

/* ---- the card's files (mc.pp.se/dc/vms/flashmem.html; dmitry.gr "VMU
 * Hacking": the FAT and directory are where the root block says) ---- */
typedef struct {
    char name[13]; unsigned char date[8];
    int type, prot, first, size, hdr, off;  /* off: the entry's byte offset */
} SaFile;

#define BLK 512
#define USER_BLOCKS 200
#define FAT_FREE 0xFFFC
#define FAT_LAST 0xFFFA
#define GAME_MAX 128                       /* 64 KB, bank 0 (Sega VMU manual) */

static int rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
/* Where the FAT and directory are: the root block's fields (FAT 0x46, its
 * size 0x48, directory 0x4a, its size 0x4c: mc.pp.se, dmitry.gr, libevmu's
 * EvmuRootBlock) when they make sense together, else the usual places
 * (254; 253 down to 241). The adapter's preformat, and cards_format after
 * it, write those fields 4 bytes late; the Dreamcast does not read them. */
static void layout(const unsigned char *c, int *fat_at, int *dir_at, int *dir_len)
{
    const unsigned char *r = c + 255 * BLK;
    int f = rd16(r + 0x46), fl = rd16(r + 0x48), d = rd16(r + 0x4a), dl = rd16(r + 0x4c);
    if (f >= USER_BLOCKS && f < 255 && fl == 1 && d >= USER_BLOCKS && d < f && dl >= 1 && d - dl + 1 >= USER_BLOCKS) {
        *fat_at = f; *dir_at = d; *dir_len = dl;
    } else { *fat_at = 254; *dir_at = 253; *dir_len = 13; }
}
static int fat_blk(const unsigned char *c) { int f, d, l; layout(c, &f, &d, &l); return f; }
static int fat(const unsigned char *c, int b) { return rd16(c + fat_blk(c) * BLK + b * 2); }
static void fat_set(unsigned char *c, int b, int v)
{
    unsigned char *p = c + fat_blk(c) * BLK + b * 2;
    p[0] = v & 255; p[1] = v >> 8;
}
static int formatted(const unsigned char *c)
{
    int i;
    for (i = 0; i < 16; i++) if (c[255 * BLK + i] != 0x55) return 0;
    return 1;
}
/* the directory's entries in the BIOS's order: its last block first */
static int dir_entries(const unsigned char *c, int *first_off)
{
    int f, at, len;
    layout(c, &f, &at, &len);
    *first_off = at * BLK;
    return len * 16;
}
static int entry_off(const unsigned char *c, int k)
{
    int top, n = dir_entries(c, &top);
    return k < n ? top - (k / 16) * BLK + (k % 16) * 32 : -1;
}

static int files(const unsigned char *card, SaFile *out, int max)
{
    int k, n = 0, off;
    if (!card) return 0;
    for (k = 0; n < max && (off = entry_off(card, k)) >= 0; k++) {
        const unsigned char *e = card + off;
        if (e[0] != 0x33 && e[0] != 0xCC) continue;
        out[n].type = e[0];
        out[n].prot = e[1] == 0xFF;
        out[n].first = rd16(e + 2);
        memcpy(out[n].name, e + 4, 12);
        out[n].name[12] = 0;
        memcpy(out[n].date, e + 0x10, 8);
        out[n].size = rd16(e + 0x18);
        out[n].hdr = rd16(e + 0x1a);
        out[n].off = off;
        n++;
    }
    return n;
}

/* a file's blocks, following the FAT: how many, or -1 when the chain is
 * not the size the directory says */
static int chain(const unsigned char *c, const SaFile *f, int *blocks)
{
    int b = f->first, n = 0;
    while (n < f->size) {
        if (b < 0 || b >= USER_BLOCKS) return -1;
        blocks[n++] = b;
        b = fat(c, b);
        if (b == FAT_LAST) break;
    }
    return n == f->size && b == FAT_LAST ? n : -1;
}

static int free_blocks(const unsigned char *c)
{
    int b, n = 0;
    for (b = 0; b < USER_BLOCKS; b++) n += fat(c, b) == FAT_FREE;
    return n;
}

static int has_game(const unsigned char *card);
/* room for a mini-game: one to a card, from block 0 up, unbroken, at most
 * 128 blocks (Sega VMU manual: "contiguous blocks starting at block 00H",
 * "maximum size ... 64 KB (= 128 blocks)") */
static int game_room(const unsigned char *c)
{
    int b = 0;
    if (has_game(c)) return 0;
    while (b < GAME_MAX && fat(c, b) == FAT_FREE) b++;
    return b;
}

/* the 16-character comment ("Description of file (shown in VMS file
 * menu)", mc.pp.se/dc/vms/fileheader.html), in the file's header block */
static void comment(const unsigned char *c, const SaFile *f, char out[17])
{
    int blocks[USER_BLOCKS], i;
    memset(out, ' ', 16); out[16] = 0;
    if (chain(c, f, blocks) < 0 || f->hdr >= f->size) return;
    for (i = 0; i < 16; i++) {
        unsigned char ch = c[blocks[f->hdr] * BLK + i];
        out[i] = ch >= 32 && ch < 127 ? ch : ' ';
    }
}

/* ---- copy and delete ---- */
enum { CP_OK, CP_SAME, CP_ROOM, CP_FAIL };

/* f from card src into dst, as the BIOS places files: a game from block 0
 * up, data in the highest free blocks (mc.pp.se) */
static int copy_file(const unsigned char *src, const SaFile *f, unsigned char *dst)
{
    int from[USER_BLOCKS], to[USER_BLOCKS], n, i, k, off, slot = -1, b;
    SaFile there[200];
    if (f->prot || !formatted(dst) || (n = chain(src, f, from)) < 0) return CP_FAIL;
    k = files(dst, there, 200);
    for (i = 0; i < k; i++) if (!memcmp(there[i].name, f->name, 12)) return CP_SAME;
    for (k = 0; (off = entry_off(dst, k)) >= 0; k++) if (dst[off] == 0) { slot = off; break; }
    if (slot < 0) return CP_ROOM;
    if (f->type == 0xCC) {
        if (has_game(dst) || n > GAME_MAX) return CP_ROOM;
        for (i = 0; i < n; i++) { if (fat(dst, i) != FAT_FREE) return CP_ROOM; to[i] = i; }
    } else {
        for (i = 0, b = USER_BLOCKS - 1; i < n && b >= 0; b--) if (fat(dst, b) == FAT_FREE) to[i++] = b;
        if (i < n) return CP_ROOM;
    }
    for (i = 0; i < n; i++) {
        memcpy(dst + to[i] * BLK, src + from[i] * BLK, BLK);
        fat_set(dst, to[i], i < n - 1 ? to[i + 1] : FAT_LAST);
    }
    memcpy(dst + slot, src + f->off, 32);
    dst[slot + 2] = to[0] & 255; dst[slot + 3] = to[0] >> 8;
    return CP_OK;
}

/* f off the card: its blocks freed in the FAT, its entry cleared */
static void delete_file(unsigned char *c, const SaFile *f)
{
    int b = f->first, n = 0, next;
    while (b >= 0 && b < USER_BLOCKS && n++ < USER_BLOCKS) {
        next = fat(c, b);
        if (next == FAT_FREE) break;
        fat_set(c, b, FAT_FREE);
        if (next == FAT_LAST) break;
        b = next;
    }
    memset(c + f->off, 0, 32);
}

static int has_game(const unsigned char *card)
{
    SaFile f[200];
    int n = files(card, f, 200), i;
    for (i = 0; i < n; i++) if (f[i].type == 0xCC) return 1;
    return 0;
}

static void done(const char *a, const char *b, const char *c, const char *d)
{
    g_result[0] = a; g_result[1] = b; g_result[2] = c; g_result[3] = d;
    g_done_name[0] = 0;
    g_fs = FS_DONE;
}

/* FILE's buttons. 1 when the card was changed (a file deleted). */
static int file_input(unsigned pressed, unsigned char *card)
{
    SaFile f[200];
    int n = files(card, f, 200), back = pressed & (SA_B | SA_LEFT), i, k;
    if (g_file >= n) g_file = n ? n - 1 : 0;
    if (!n && g_fs != FS_DONE) g_fs = FS_FREE;
    switch (g_fs) {
    case FS_FREE:
        if ((pressed & (SA_A | SA_RIGHT)) && n) g_fs = FS_LIST;
        break;
    case FS_LIST:
        if (pressed & SA_DOWN) g_file = (g_file + 1) % n;
        if (pressed & SA_UP) g_file = (g_file + n - 1) % n;
        if (pressed & SA_RIGHT) g_fs = FS_COMMENT;
        else if (back) g_fs = FS_FREE;
        break;
    case FS_COMMENT:
        if (pressed & SA_RIGHT) { g_fs = FS_ACTION; g_act = f[g_file].prot; }
        else if (back) g_fs = FS_LIST;
        break;
    case FS_ACTION:                        /* 0 Copy, 1 Delete; a protected file has no Copy */
        if (pressed & (SA_UP | SA_DOWN)) g_act = f[g_file].prot ? 1 : !g_act;
        if (pressed & SA_A) {
            if (g_act == 0) {
                k = cards_list(g_targets);
                for (i = g_ntargets = 0; i < k; i++)
                    if (strcasecmp(g_targets[i], g_card_name))
                        memcpy(g_targets[g_ntargets++], g_targets[i], CARD_NAME_MAX);
                g_target = 0;
                g_fs = FS_TARGET;
            } else { g_yes = 0; g_fs = FS_ASK; }
        } else if (back) g_fs = FS_COMMENT;
        break;
    case FS_TARGET:
        if (g_ntargets && (pressed & SA_DOWN)) g_target = (g_target + 1) % g_ntargets;
        if (g_ntargets && (pressed & SA_UP)) g_target = (g_target + g_ntargets - 1) % g_ntargets;
        if (g_ntargets && (pressed & (SA_A | SA_RIGHT))) { g_yes = 1; g_fs = FS_ASK; }
        else if (back) g_fs = FS_ACTION;
        break;
    case FS_ASK:                           /* "Yes No": left and right, A enters */
        if (pressed & SA_LEFT) g_yes = 1;
        if (pressed & SA_RIGHT) g_yes = 0;
        if (pressed & SA_A) {
            if (!g_yes) g_fs = g_act ? FS_ACTION : FS_TARGET;   /* "No": the previous screen */
            else if (g_act) {
                delete_file(card, &f[g_file]);
                done(NULL, NULL, "Deleted", NULL);
                memcpy(g_done_name, f[g_file].name, 13);
                return 1;
            } else { g_fs = FS_BUSY; g_busy_drawn = 0; }  /* "Copying!" drawn, then the copy */
        } else if (pressed & SA_B) g_fs = g_act ? FS_ACTION : FS_TARGET;
        break;
    case FS_DONE:
        if (pressed & (SA_A | SA_B)) g_fs = n ? FS_LIST : FS_FREE;
        break;
    }
    return 0;
}

/* The copy, run once "Copying!" has been drawn: the other card read, the
 * file added, the card written back (cards_write: .new, then renamed). */
static void file_copy(const unsigned char *card)
{
    SaFile f[200];
    int n = files(card, f, 200), rc;
    if (g_file >= n || g_target >= g_ntargets) { done("Copy", "failed", NULL, NULL); return; }
    if (!cards_read(g_targets[g_target], g_other)) { done("Copy", "failed", NULL, NULL); return; }
    rc = copy_file(card, &f[g_file], g_other);
    if (rc == CP_OK && cards_write(g_targets[g_target], g_other) < 0) rc = CP_FAIL;
    if (rc == CP_OK) { done(NULL, NULL, "Copied", NULL); memcpy(g_done_name, f[g_file].name, 13); }
    else if (rc == CP_SAME) done("Same", "file", "exist", NULL);
    else if (rc == CP_ROOM) done("Not", "enough", "free", "memory");
    else done("Copy", "failed", NULL, NULL);
}

int sa_input(unsigned pressed, unsigned held, unsigned char *card, unsigned ms)
{
    if (game_running()) {
        if (strcasecmp(g_game_card, g_card_name)) {
            /* The card in use is not the game's (main.c stops games before a
             * card changes, so this should not happen): its flash goes to its
             * own card's file, never into another card (2026-11-15: a game
             * left running over a card switch wrote itself over two cards). */
            if (game_stop(g_other)) cards_write(g_game_card, g_other);
            g_mode_held_ms = 0;
            return 0;
        }
        g_mode_held_ms = held & SA_MODE ? g_mode_held_ms + ms : 0;
        /* back to the VMU's own GAME screen: when the game hands back to the
         * BIOS (MODE, in most games), or MODE held 2 s whatever the game does */
        if (game_ended() || g_mode_held_ms >= LEAVE_GAME_MS) {
            g_mode_held_ms = 0;
            return card ? game_stop(card) : 0;
        }
        game_frame(held, ms);
        return 0;
    }
    if (pressed & SA_SLEEP) { g_sleep = !g_sleep; return 0; }
    if (g_sleep) { if (pressed) g_sleep = 0; return 0; }   /* any button wakes it */
    if (g_fs == FS_BUSY) {                 /* the copy, once "Copying!" is on the screen */
        if (g_busy_drawn && card) file_copy(card);
        else if (!card) done("Copy", "failed", NULL, NULL);
        return 0;
    }
    if (pressed & SA_MODE) { g_mode = (g_mode + 1) % MODES; g_file = 0; g_fs = FS_FREE; return 0; }
    if (g_mode == MODE_GAME && (pressed & SA_A) && card && has_game(card)) {
        snprintf(g_game_card, sizeof(g_game_card), "%s", g_card_name);
        game_start(card);
        return 0;
    }
    if (g_mode == MODE_FILE) return file_input(pressed, card);
    return 0;
}

/* ---- drawing on the 48x32 screen ---- */

/* 5x7 letters in 6x8 cells; a row a byte, bit 4 the leftmost. The lower
 * case as an HD44780's, so they keep within the 7 rows. \x02 is the
 * sheet's bullet, \x03 its arrow. */
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
    {'a',{0,0,14,1,15,17,15}},    {'b',{16,16,22,25,17,17,30}}, {'c',{0,0,14,16,16,17,14}},
    {'d',{1,1,13,19,17,17,15}},   {'e',{0,0,14,17,31,16,14}}, {'f',{6,9,8,28,8,8,8}},
    {'g',{0,15,17,17,15,1,14}},   {'h',{16,16,22,25,17,17,17}}, {'i',{4,0,12,4,4,4,14}},
    {'j',{2,0,6,2,2,18,12}},      {'k',{16,16,18,20,24,20,18}}, {'l',{12,4,4,4,4,4,14}},
    {'m',{0,0,26,21,21,17,17}},   {'n',{0,0,22,25,17,17,17}}, {'o',{0,0,14,17,17,17,14}},
    {'p',{0,0,30,17,30,16,16}},   {'q',{0,0,13,19,15,1,1}},  {'r',{0,0,22,25,16,16,16}},
    {'s',{0,0,14,16,14,1,30}},    {'t',{8,8,28,8,8,9,6}},    {'u',{0,0,17,17,17,19,13}},
    {'v',{0,0,17,17,17,10,4}},    {'w',{0,0,17,17,21,21,10}}, {'x',{0,0,17,10,4,10,17}},
    {'y',{0,0,17,17,15,1,14}},    {'z',{0,0,31,2,4,8,31}},
    {'#',{10,10,31,10,31,10,10}}, {'/',{1,1,2,4,8,16,16}},   {':',{0,12,12,0,12,12,0}},
    {'_',{0,0,0,0,0,0,31}},       {'.',{0,0,0,0,0,12,12}},   {'-',{0,0,0,31,0,0,0}},
    {'!',{4,4,4,4,0,0,4}},        {'?',{14,17,1,2,4,0,4}},   {'\'',{12,4,8,0,0,0,0}},
    {'(',{2,4,8,8,8,4,2}},        {')',{8,4,2,2,2,4,8}},     {'+',{0,4,4,31,4,4,0}},
    {',',{0,0,0,0,12,4,8}},       {'=',{0,0,31,0,31,0,0}},   {'&',{12,18,20,8,21,18,13}},
    {'*',{0,4,21,14,21,4,0}},     {'"',{10,10,10,0,0,0,0}},  {'%',{24,25,2,4,8,19,3}},
    {'<',{2,4,8,16,8,4,2}},       {'>',{8,4,2,1,2,4,8}},
    {'\x02',{0,14,31,31,31,14,0}}, {'\x03',{8,12,14,15,14,12,8}},
};

/* a dot at (x, y) as the player sees it, stored as the Dreamcast sends it
 * (turned half round) so ui_lcd draws it the right way up */
static void dot_set(unsigned char *lcd, int x, int y, int on)
{
    int sx = 47 - x, sy = 31 - y;
    if (x < 0 || y < 0 || x > 47 || y > 31) return;
    if (on) lcd[sy * 6 + sx / 8] |= 0x80 >> (sx & 7);
    else lcd[sy * 6 + sx / 8] &= ~(0x80 >> (sx & 7));
}
static void dot(unsigned char *lcd, int x, int y) { dot_set(lcd, x, y, 1); }

/* text in the 8x4 grid; inv: lit cells with dark letters, as the sheet's
 * highlighted lines */
static void text_at(unsigned char *lcd, int col, int row, const char *s, int inv)
{
    for (; *s && col < 8; s++, col++) {
        unsigned i, r, c;
        if (inv) for (r = 0; r < 8; r++) for (c = 0; c < 6; c++) dot(lcd, col * 6 + c, row * 8 + r);
        for (i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
            if (font[i].c != *s) continue;
            for (r = 0; r < 7; r++)
                for (c = 0; c < 5; c++)
                    if (font[i].rows[r] & (16 >> c)) dot_set(lcd, col * 6 + c, row * 8 + r, !inv);
            break;
        }
    }
}
static void text(unsigned char *lcd, int col, int row, const char *s) { text_at(lcd, col, row, s, 0); }

static int bcd(unsigned char v) { return (v >> 4) * 10 + (v & 15); }

/* the file's name over two lines, inverted when it may not be copied */
static void name_lines(unsigned char *lcd, int row, const SaFile *f, int inv)
{
    char line[9];
    memcpy(line, f->name, 8); line[8] = 0;     text_at(lcd, 0, row, line, inv);
    memcpy(line, f->name + 8, 4); line[4] = 0; text_at(lcd, 0, row + 1, line, inv);
}

static unsigned file_render(unsigned char *lcd, const unsigned char *card)
{
    SaFile f[200];
    char line[40];
    int n = files(card, f, 200), i;
    const SaFile *e;
    if (!card) { text(lcd, 0, 1, "No card"); return SA_ICON_FILE; }
    if (g_file >= n) g_file = n ? n - 1 : 0;
    e = &f[g_file];
    switch (n || g_fs == FS_DONE ? g_fs : FS_FREE) {
    case FS_FREE:                          /* "Free 200" / "Game 128" */
        snprintf(line, sizeof(line), "Free %3d", free_blocks(card)); text(lcd, 0, 0, line);
        snprintf(line, sizeof(line), "Game %3d", game_room(card));   text(lcd, 0, 1, line);
        return SA_ICON_FILE;
    case FS_LIST:                          /* number/total, name, date (month/day/year) */
        snprintf(line, sizeof(line), "#%03d/%03d", g_file + 1, n);    text(lcd, 0, 0, line);
        name_lines(lcd, 1, e, e->prot);
        snprintf(line, sizeof(line), "%02d/%02d/%02d", bcd(e->date[2]), bcd(e->date[3]), bcd(e->date[1]));
        text(lcd, 0, 3, line);
        return SA_ICON_FILE;
    case FS_COMMENT: {                     /* comment, blocks used, type */
        char c[17];
        comment(card, e, c);
        memcpy(line, c, 8); line[8] = 0;     text(lcd, 0, 0, line);
        memcpy(line, c + 8, 8); line[8] = 0; text(lcd, 0, 1, line);
        snprintf(line, sizeof(line), "%8d", e->size);                text(lcd, 0, 2, line);
        text(lcd, 0, 3, e->type == 0xCC ? "Game" : "Data");
        text(lcd, 7, 3, "\x03");
        return SA_ICON_FILE;
    }
    case FS_ACTION:                        /* the chosen line lit across */
        for (i = 0; i < 2; i++) {
            int c;
            if (g_act == i) for (c = 0; c < 8; c++) text_at(lcd, c, i, " ", 1);
            text_at(lcd, 0, i, i ? "\x02" "Delete" : "\x02" "Copy", g_act == i);
        }
        text(lcd, 0, 2, "A:Yes");
        text(lcd, 0, 3, "B:No");
        return SA_ICON_FILE;
    case FS_TARGET:                        /* (the PSP's stand-in for a second VMU) */
        text(lcd, 0, 0, "Copy to");
        if (!g_ntargets) { text(lcd, 0, 1, "no other"); text(lcd, 0, 2, "card"); return SA_ICON_FILE; }
        snprintf(line, sizeof(line), "%.8s", g_targets[g_target]);                text(lcd, 0, 1, line);
        if (strlen(g_targets[g_target]) > 8) { snprintf(line, sizeof(line), "%.8s", g_targets[g_target] + 8); text(lcd, 0, 2, line); }
        snprintf(line, sizeof(line), "%d/%d", g_target + 1, g_ntargets);            text(lcd, 0, 3, line);
        return SA_ICON_FILE;
    case FS_ASK:                           /* "Copy ?" / "Yes No" */
        name_lines(lcd, 0, e, 0);
        text(lcd, 0, 2, g_act ? "Delete ?" : "Copy ?");
        text_at(lcd, 0, 3, "Yes", g_yes);
        text_at(lcd, 5, 3, "No", !g_yes);
        return SA_ICON_FILE;
    case FS_BUSY:                          /* "Copying!", the warning icon lit (sheet) */
        name_lines(lcd, 0, e, 0);
        text(lcd, 0, 2, "Copying!");
        g_busy_drawn = 1;
        return SA_ICON_FILE | SA_ICON_ATTENTION;
    case FS_DONE:
        if (g_done_name[0]) {              /* "Copied" under the file's name */
            SaFile d;
            memcpy(d.name, g_done_name, 13);
            name_lines(lcd, 0, &d, 0);
        }
        for (i = 0; i < 4; i++) if (g_result[i]) text(lcd, 0, i, g_result[i]);
        return SA_ICON_FILE;
    }
    return SA_ICON_FILE;
}

unsigned sa_render(unsigned char lcd[PSP2DC_LCD_BYTES], const unsigned char *card)
{
    char line[40];
    memset(lcd, 0, PSP2DC_LCD_BYTES);
    if (game_running()) return game_lcd(lcd);
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
    if (g_mode == MODE_FILE) return file_render(lcd, card);
    /* GAME: the card's executable, which cannot run yet */
    {
        SaFile f[200];
        int n = files(card, f, 200), i;
        for (i = 0; i < n && f[i].type != 0xCC; i++) ;
        text(lcd, 0, 0, "  GAME");
        if (i < n) { memcpy(line, f[i].name, 8); line[8] = 0; text(lcd, 0, 2, line); text(lcd, 0, 3, " A: PLAY"); }
        else text(lcd, 0, 2, "NO GAME");
        return SA_ICON_GAME;
    }
}
