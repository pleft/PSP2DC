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
unsigned ui_draw_us(void);           /* the last frame's drawing time, and the longest so far */
unsigned ui_draw_max_us(void);
void ui_fill(int x, int y, int w, int h, unsigned colour);
void ui_text(int col, int row, unsigned colour, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
/* the VMU's screen, as the Dreamcast sends it (turned half round here) */
void ui_lcd(const unsigned char *lcd, int x, int y, int scale, const UiTheme *t);
/* the 48x32 dots spread over w x h pixels (dots of mixed sizes where that is not whole) */
void ui_lcd_fit(const unsigned char *lcd, int x, int y, int w, int h, const UiTheme *t);
/* the same with dots sx pixels wide and sy tall */
void ui_lcd_dots(const unsigned char *lcd, int x, int y, int sx, int sy, const UiTheme *t);

/* The pages */
enum { UI_LINK_NONE, UI_LINK_WAITING, UI_LINK_UP, UI_LINK_DEMO };
typedef struct {
    const unsigned char *lcd;          /* the VMU's screen */
    int link;                          /* UI_LINK_* */
    const char *link_text;
    const Psp2dcInput *in;             /* what goes to the Dreamcast now */
    const unsigned char *card;         /* the memory card, when card_have */
    int card_have;
    const char *card_note;
    const char *card_name;
    const char *notice;                /* a change just made, or NULL */
    unsigned now_ms;
    unsigned icons;                    /* lit LCD icons: 1 file, 2 game, 4 clock, 8 attention */
    unsigned vmu_pressed;              /* the drawn VMU's buttons held: vmu_sa.h's SA_* bits */
    const char *standalone;            /* the VMU's own mode when no Dreamcast is there, or NULL */
} UiHome;
/* The VMU four times its size in a drawing of the VMU, the controller's
 * state and the memory card beside it. */
void ui_page_home(const UiHome *h, const UiTheme *t);
/* A box over a page: a title, lines of text, choices (one chosen, some
 * greyed), or a name being edited (Rename) */
typedef struct {
    const char *title;                 /* NULL: no box */
    const char *lines[8];              /* NULL-ended */
    const char *items[12];
    int nitems, item;
    unsigned off_items;                /* bits: choices that cannot be taken (greyed) */
    const unsigned short *picture;     /* a save's large picture, 72x56 ARGB4444, or NULL */
    const char *edit;                  /* a name being edited, or NULL */
    int cursor;                        /* the character being changed */
} UiBox;
/* the line of keys at the bottom of the Saves and Cards pages */
enum { UI_KEYS_VIEW, UI_KEYS_BROWSE, UI_KEYS_MENU, UI_KEYS_OK, UI_KEYS_EDIT };

/* The Saves page: where it is, what is chosen, the box over it */
typedef struct {
    int first;                         /* the first save shown */
    int sel;                           /* the chosen save, or -1 (connected: view only) */
    UiBox box;
    int keys;                          /* UI_KEYS_* */
} UiSaves;
/* The memory card's saves. Returns how many there are. */
int  ui_page_saves(const unsigned char *card, int have, const UiSaves *v, const UiTheme *t, unsigned now_ms,
                   const char *top);

/* The Cards page: "New card" first, then each card */
#define UI_CARDS_MAX 32
typedef struct {
    int n;                             /* cards */
    const char *name[UI_CARDS_MAX];
    int saves[UI_CARDS_MAX], free[UI_CARDS_MAX], in_use[UI_CARDS_MAX];
    const char *game[UI_CARDS_MAX];    /* its game's name, or NULL */
    int first, sel;                    /* rows: 0 is "New card", 1 + i card i; sel -1: view only */
    UiBox box;
    int keys;
} UiCards;
void ui_page_cards(const UiCards *v, const UiTheme *t, const char *top);

#endif
