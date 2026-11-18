/*
 * The Dreamcast keyboard, typed on the PSP: a US keyboard drawn on the
 * screen (ui_page_keys), a key chosen with the d-pad and pressed with X, a
 * few keys on the PSP's own buttons. What it makes is the 8 bytes a Sega
 * keyboard answers the Dreamcast with (psp2dc_protocol.h, Psp2dcKeyboard).
 */
#ifndef PSP2DC_KEYBOARD_H
#define PSP2DC_KEYBOARD_H

#define KB_ROWS 6
#define KB_MOD  0xE0   /* key codes from here: a modifier, its bit code - KB_MOD */
typedef struct {
    const char *label, *shifted;   /* shifted: shown with Shift, NULL the same */
    unsigned char code;            /* USB's key code, as FT6 has them */
    unsigned char w;               /* width in quarters of a key */
} KbKey;
extern const KbKey *const kb_rows[KB_ROWS];
extern const int kb_row_len[KB_ROWS];
#define KB_ROW_QUARTERS 60         /* every row is 15 keys wide */

typedef struct {
    int row, col;                  /* the key chosen */
    unsigned char latched;         /* modifiers set from the keyboard: for the next key only */
    int caps;                      /* Caps Lock (its LED) */
    unsigned last;                 /* the buttons last time */
    int repeat;                    /* frames the d-pad has been held */
    int typed;                     /* a key was held last time (a latched modifier's end) */
    unsigned char cond[8];         /* what goes to the Dreamcast */
} Kb;

void kb_reset(Kb *k);              /* nothing held, nothing latched (Caps Lock kept) */
/* a frame: the PSP's buttons held (PSP_CTRL_*), the nub (0-255, 128 the
 * middle); fills k->cond */
void kb_frame(Kb *k, unsigned held, int nub_x, int nub_y);
/* is the key held now (sent to the Dreamcast, or a modifier latched)? */
int kb_down(const Kb *k, const KbKey *key);

#endif
