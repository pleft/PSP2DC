/*
 * Drawing for the PSP's screen: two frame buffers in VRAM, drawn whole and
 * shown at a frame's start, so nothing is ever seen half drawn. Text goes
 * through the debug screen, pointed at the buffer being drawn.
 */
#ifndef PSP2DC_UI_H
#define PSP2DC_UI_H

#include "../../protocol/psp2dc_protocol.h"

typedef struct {
    const char *name;
    unsigned bg;       /* around the VMU; colours are ABGR (0xAABBGGRR) */
    unsigned paper;    /* the LCD where a dot is off */
    unsigned ink;      /* a dot that is on */
    unsigned frame;    /* the LCD's surround */
    unsigned text;     /* the lines above and below */
    int grid;          /* 1: the dots drawn apart, as on the real LCD */
    unsigned body;     /* the VMU's case */
    unsigned panel;    /* the boxes beside it */
} UiTheme;

extern const UiTheme ui_themes[];
extern const int ui_theme_count;

void ui_init(void);
void ui_begin(unsigned bg);          /* a frame: the back buffer, filled with bg; text goes there */
void ui_end(void);                   /* shown from the next vblank */
void ui_fill(int x, int y, int w, int h, unsigned colour);
void ui_text(int col, int row, unsigned colour, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
/* the VMU's screen, as the Dreamcast sends it (turned half round here) */
void ui_lcd(const unsigned char *lcd, int x, int y, int scale, const UiTheme *t);

/* The pages */
enum { UI_LINK_NONE, UI_LINK_WAITING, UI_LINK_UP, UI_LINK_DEMO };
typedef struct {
    const unsigned char *lcd;          /* the VMU's screen */
    int link;                          /* UI_LINK_* */
    const char *link_text;
    const Psp2dcInput *in;             /* what goes to the Dreamcast now */
    const char *stick_source;
    const unsigned char *card;         /* the memory card, when card_have */
    int card_have;
    const char *card_note;
    const char *notice;                /* a change just made, or NULL */
    unsigned now_ms;
} UiHome;
/* The VMU four times its size in a drawing of the VMU, the controller's
 * state and the memory card beside it. */
void ui_page_home(const UiHome *h, const UiTheme *t);
/* The memory card's saves, from 'first' on. Returns how many there are. */
int  ui_page_saves(const unsigned char *card, int have, int first, const UiTheme *t, unsigned now_ms,
                   const char *top);

#endif
