/*
 * The Dreamcast keyboard, typed on the PSP (keyboard.h). The layout is a US
 * keyboard's, every row 15 keys wide; the key codes are USB's, which Sega's
 * keyboard uses too ("Maple Bus 1.0 Function Type Specifications FT6:
 * Keyboard", rev. 0.90, 4.3).
 *
 * The buttons, while typing: the d-pad chooses a key (held, it repeats), X
 * presses it for as long as X is held. A modifier chosen so (Shift, Ctrl,
 * Alt) is latched instead, for the next key, as a phone's Shift; Caps Lock
 * stays on until pressed again. Square is Backspace, triangle Space, START
 * Enter, O Esc, L a Shift held, and the nub the arrow keys.
 */
#include <string.h>
#include <pspctrl.h>
#include "keyboard.h"

static const KbKey r0[] = {
    { "Esc", 0, 0x29, 4 },
    { "F1", 0, 0x3A, 4 }, { "F2", 0, 0x3B, 4 }, { "F3", 0, 0x3C, 4 }, { "F4", 0, 0x3D, 4 },
    { "F5", 0, 0x3E, 4 }, { "F6", 0, 0x3F, 4 }, { "F7", 0, 0x40, 4 }, { "F8", 0, 0x41, 4 },
    { "F9", 0, 0x42, 4 }, { "F10", 0, 0x43, 4 }, { "F11", 0, 0x44, 4 }, { "F12", 0, 0x45, 4 },
    { "Del", 0, 0x4C, 8 },
};
static const KbKey r1[] = {
    { "`", "~", 0x35, 4 }, { "1", "!", 0x1E, 4 }, { "2", "@", 0x1F, 4 }, { "3", "#", 0x20, 4 },
    { "4", "$", 0x21, 4 }, { "5", "%", 0x22, 4 }, { "6", "^", 0x23, 4 }, { "7", "&", 0x24, 4 },
    { "8", "*", 0x25, 4 }, { "9", "(", 0x26, 4 }, { "0", ")", 0x27, 4 }, { "-", "_", 0x2D, 4 },
    { "=", "+", 0x2E, 4 }, { "Bksp", 0, 0x2A, 8 },
};
#define LETTER(c, C, code) { c, C, code, 4 }
static const KbKey r2[] = {
    { "Tab", 0, 0x2B, 6 },
    LETTER("q", "Q", 0x14), LETTER("w", "W", 0x1A), LETTER("e", "E", 0x08), LETTER("r", "R", 0x15),
    LETTER("t", "T", 0x17), LETTER("y", "Y", 0x1C), LETTER("u", "U", 0x18), LETTER("i", "I", 0x0C),
    LETTER("o", "O", 0x12), LETTER("p", "P", 0x13),
    { "[", "{", 0x2F, 4 }, { "]", "}", 0x30, 4 }, { "\\", "|", 0x31, 6 },
};
static const KbKey r3[] = {
    { "Caps", 0, 0x39, 7 },
    LETTER("a", "A", 0x04), LETTER("s", "S", 0x16), LETTER("d", "D", 0x07), LETTER("f", "F", 0x09),
    LETTER("g", "G", 0x0A), LETTER("h", "H", 0x0B), LETTER("j", "J", 0x0D), LETTER("k", "K", 0x0E),
    LETTER("l", "L", 0x0F),
    { ";", ":", 0x33, 4 }, { "'", "\"", 0x34, 4 }, { "Enter", 0, 0x28, 9 },
};
static const KbKey r4[] = {
    { "Shift", 0, KB_MOD + 1, 9 },
    LETTER("z", "Z", 0x1D), LETTER("x", "X", 0x1B), LETTER("c", "C", 0x06), LETTER("v", "V", 0x19),
    LETTER("b", "B", 0x05), LETTER("n", "N", 0x11), LETTER("m", "M", 0x10),
    { ",", "<", 0x36, 4 }, { ".", ">", 0x37, 4 }, { "/", "?", 0x38, 4 },
    { "Shift", 0, KB_MOD + 5, 11 },
};
static const KbKey r5[] = {
    { "Ctrl", 0, KB_MOD + 0, 6 }, { "Alt", 0, KB_MOD + 2, 6 }, { "Space", 0, 0x2C, 28 },
    { "<", 0, 0x50, 5 }, { "v", 0, 0x51, 5 }, { "^", 0, 0x52, 5 }, { ">", 0, 0x4F, 5 },
};
#define N(a) ((int)(sizeof(a) / sizeof(a[0])))
const KbKey *const kb_rows[KB_ROWS] = { r0, r1, r2, r3, r4, r5 };
const int kb_row_len[KB_ROWS] = { N(r0), N(r1), N(r2), N(r3), N(r4), N(r5) };

#define CAPS_LOCK 0x39
#define NUB_KEYS  60       /* the nub this far from the middle: an arrow key */

void kb_reset(Kb *k)
{
    int caps = k->caps, row = k->row, col = k->col;
    memset(k, 0, sizeof(*k));
    k->caps = caps; k->row = row; k->col = col;
    k->cond[1] = caps ? 0x02 : 0;
}

/* the key in another row under the middle of the one chosen */
static int col_below(int from_row, int col, int to_row)
{
    int q = 0, i, mid;
    for (i = 0; i < col; i++) q += kb_rows[from_row][i].w;
    mid = 2 * q + kb_rows[from_row][col].w;          /* in eighths */
    for (i = 0, q = 0; i < kb_row_len[to_row]; i++) {
        q += kb_rows[to_row][i].w;
        if (2 * q > mid) return i;
    }
    return kb_row_len[to_row] - 1;
}

static void move(Kb *k, unsigned dirs)
{
    if (dirs & PSP_CTRL_LEFT)  k->col = (k->col + kb_row_len[k->row] - 1) % kb_row_len[k->row];
    if (dirs & PSP_CTRL_RIGHT) k->col = (k->col + 1) % kb_row_len[k->row];
    if (dirs & (PSP_CTRL_UP | PSP_CTRL_DOWN)) {
        int to = (k->row + (dirs & PSP_CTRL_UP ? KB_ROWS - 1 : 1)) % KB_ROWS;
        k->col = col_below(k->row, k->col, to);
        k->row = to;
    }
}

static void add(unsigned char *keys, int *n, unsigned char code)
{
    int i;
    for (i = 0; i < *n && i < 7; i++) if (keys[i] == code) return;
    if (*n < 7) keys[*n] = code;
    (*n)++;
}

void kb_frame(Kb *k, unsigned held, int nub_x, int nub_y)
{
    static const struct { unsigned button; unsigned char code; } on_buttons[] = {
        { PSP_CTRL_SQUARE, 0x2A }, { PSP_CTRL_TRIANGLE, 0x2C }, { PSP_CTRL_START, 0x28 }, { PSP_CTRL_CIRCLE, 0x29 },
    };
    const unsigned dpad = PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT;
    unsigned pressed = held & ~k->last, dirs = held & dpad, step = pressed & dpad;
    unsigned char keys[8], mods;
    const KbKey *cur;
    int n = 0, i, typing = 0;

    /* the d-pad: a step a press, then a step every 4 frames once held 0.3 s */
    k->repeat = dirs ? k->repeat + 1 : 0;
    if (k->repeat > 18 && (k->repeat - 18) % 4 == 0) step |= dirs;
    move(k, step);
    cur = &kb_rows[k->row][k->col];

    if (cur->code >= KB_MOD) {
        if (pressed & PSP_CTRL_CROSS) k->latched ^= (unsigned char)(1 << (cur->code - KB_MOD));
    } else if (held & PSP_CTRL_CROSS) {
        add(keys, &n, cur->code);
        if (pressed & PSP_CTRL_CROSS && cur->code == CAPS_LOCK) k->caps = !k->caps;
        if (cur->code != CAPS_LOCK) typing = 1;
    }
    for (i = 0; i < N(on_buttons); i++)
        if (held & on_buttons[i].button) { add(keys, &n, on_buttons[i].code); typing = 1; }
    if (nub_x < 128 - NUB_KEYS) add(keys, &n, 0x50);
    if (nub_x > 128 + NUB_KEYS) add(keys, &n, 0x4F);
    if (nub_y < 128 - NUB_KEYS) add(keys, &n, 0x52);
    if (nub_y > 128 + NUB_KEYS) add(keys, &n, 0x51);

    mods = k->latched;
    if (held & PSP_CTRL_LTRIGGER) mods |= 0x02;
    /* a latched modifier lasts until the key typed with it is let go */
    if (!typing && k->typed) k->latched = 0;

    memset(k->cond, 0, sizeof(k->cond));
    k->cond[0] = mods;
    k->cond[1] = k->caps ? 0x02 : 0;   /* Caps Lock's LED */
    if (n > 6) memset(k->cond + 2, 0x01, 6);   /* more than six: rollover error (FT6 2.2) */
    else memcpy(k->cond + 2, keys, n);
    k->last = held;
    k->typed = typing;
}

int kb_down(const Kb *k, const KbKey *key)
{
    int i;
    if (key->code >= KB_MOD) return (k->cond[0] >> (key->code - KB_MOD)) & 1;
    if (key->code == CAPS_LOCK && k->caps) return 1;
    for (i = 2; i < 8; i++) if (k->cond[i] == key->code) return 1;
    return 0;
}
