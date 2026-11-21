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
    unsigned panel;    /* the boxes beside it, the rows of a list */
    unsigned accent;   /* what is chosen: the tab shown, the row, the key (one colour for that only) */
    unsigned ok;       /* the card in use */
    unsigned danger;   /* what deletes or writes over */
} UiTheme;

extern const UiTheme ui_themes[];
extern const int ui_theme_count;

void ui_init(void);
void ui_begin(unsigned bg);          /* a frame: the back buffer, filled with bg; text goes there */
void ui_end(void);                   /* shown from the next vblank */
unsigned ui_draw_us(void);           /* the last frame's drawing time, and the longest so far */
unsigned ui_draw_max_us(void);
void ui_fill(int x, int y, int w, int h, unsigned colour);
/* a rectangle faded k/16 of the way to a colour (shown, not in use now) */
void ui_fade(int x, int y, int w, int h, unsigned to, int k);
void ui_text(int col, int row, unsigned colour, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
/* the VMU's screen, as the Dreamcast sends it (turned half round here) */
void ui_lcd(const unsigned char *lcd, int x, int y, int scale, const UiTheme *t);
/* the 48x32 dots spread over w x h pixels (dots of mixed sizes where that is not whole) */
void ui_lcd_fit(const unsigned char *lcd, int x, int y, int w, int h, const UiTheme *t);
/* the same with dots sx pixels wide and sy tall */
void ui_lcd_dots(const unsigned char *lcd, int x, int y, int sx, int sy, const UiTheme *t);

/* The pages */
enum { UI_LINK_NONE, UI_LINK_WAITING, UI_LINK_UP, UI_LINK_DEMO };

/* What every page shows the same way, set once a frame: standalone or not,
 * the menu or playing (SELECT switches: the header and the keys show
 * which), and the status at the footer's right: the link's dot, the card in
 * use (NULL: none) and what became of it, or a message just given (NULL:
 * none) in its place. */
void ui_frame(int standalone, int menu, int link, const char *card, const char *note, const char *notice);
/* what an action came to, in a few words: shown in the middle of the screen
 * over any page (NULL: nothing) */
void ui_toast(const char *s);
/* the PSP's battery, at the header's right: percent (< 0: none shown), and
 * whether it is charging */
void ui_battery(int percent, int charging);
/* the debug font's text (pspDebugScreen) moved px pixels right, clear of
 * the menu's frame (Details) */
void ui_text_margin(int px);
/* the VMU's screen and its lit icons (1 file, 2 game, 4 clock, 8 attention),
 * set once a frame: the menu's pages show the VMU beside them */
void ui_vmu(const unsigned char *lcd, unsigned icons);
typedef struct {
    const unsigned char *lcd;          /* the VMU's screen */
    int link;                          /* UI_LINK_* */
    const char *link_text;             /* "Standalone", "Connected"... */
    const char *link_hint;             /* under it, what that means */
    const Psp2dcInput *in;             /* what goes to the Dreamcast now */
    const unsigned char *card;         /* the memory card, when card_have */
    int card_have;
    const char *card_note;
    const char *card_name;
    const char *notice;                /* a change just made, or NULL */
    unsigned now_ms;
    unsigned icons;                    /* lit LCD icons: 1 file, 2 game, 4 clock, 8 attention */
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
#define UI_BOX_ITEMS 512
    const char *items[UI_BOX_ITEMS];   /* (a long list scrolls, nine at a time) */
    int nitems, item;
    unsigned off_items;                /* bits: choices that cannot be taken (greyed) */
    const unsigned short *picture;     /* a save's large picture, 72x56 ARGB4444, or NULL */
    unsigned sep_items;                /* bits: a line above that choice (the choices in groups) */
    unsigned danger_items;             /* bits: choices that delete or write over */
    const char *edit;                  /* a name being typed (Rename), or NULL: the keyboard shown */
    int key;                           /* the keyboard's key chosen: row * UI_KB_COLS + column */
} UiBox;
/* The keyboard under a name being typed: three rows of the characters a
 * card's name may have (cards_name_ok), then Delete and Done. */
#define UI_KB_COLS 13
#define UI_KB_ROWS 4
extern const char *const ui_kb_rows[3];

/* The keyboard being typed on (keyboard.h): the VMU small, what the
 * Dreamcast makes of the keyboard, and the keys, the chosen one lit and
 * those held marked. */
#include "keyboard.h"
typedef struct {
    const unsigned char *lcd;          /* the VMU's screen */
    const Kb *kb;
    const char *state;                 /* what the Dreamcast makes of it */
    int state_ok;                      /* 1: it reads the keyboard */
} UiKeys;
void ui_page_keys(const UiKeys *v, const UiTheme *t);

/* The companion's page (companion.h): the rows a Dreamcast program printed,
 * and what arrives: bytes and records in the last second, the test data
 * checked. */
#include "companion.h"
void ui_page_companion(const Comp *c, unsigned bytes_s, unsigned records_s, const UiTheme *t);
/* The companion's screen (comp_screen.h): the program's widgets and regions
 * on the whole screen, in the theme's colours. */
void ui_page_screen(const Comp *c, const UiTheme *t, unsigned now_us);

/* The bar along the top. In the menu it is lit: PSP2DC and where in the menu
 * (title, or NULL); playing it is dim, with SELECT to open the menu. The
 * battery at its right. */
void ui_header(const char *title, const UiTheme *t);

/* The menu, SELECT from playing: the VMU on the left as when playing, and
 * beside it a list of where to go, each with a line on what is there. */
enum { UI_HOME_VMU, UI_HOME_SAVES, UI_HOME_CARDS, UI_HOME_KEYBOARD, UI_HOME_SETTINGS, UI_HOME_ITEMS };
typedef struct {
    int sel;                           /* the item chosen */
    const char *sub[UI_HOME_ITEMS];    /* under each, what is there ("2 saves on card1") */
} UiMenu;
void ui_page_menu(const UiMenu *m, const UiTheme *t);
/* every key, full screen (Settings, Keys) */
void ui_page_keyhelp(const UiTheme *t);
/* the line of keys at the bottom (UI_KEYS_*), for a page drawn elsewhere */
void ui_keys(int keys, const UiTheme *t);
/* the line of keys at the bottom of a page: only the keys that do something now */
enum { UI_KEYS_VIEW, UI_KEYS_HOME, UI_KEYS_SAVES, UI_KEYS_CARDS, UI_KEYS_MENU, UI_KEYS_OK, UI_KEYS_EDIT,
       UI_KEYS_SYSTEM, UI_KEYS_SYSTEM_OPEN, UI_KEYS_DETAILS, UI_KEYS_BACK, UI_KEYS_LIST };

/* The Saves page, beside the VMU: where it is, what is chosen, the box in its place */
#define UI_SAVES_ROWS 4                /* saves shown at once */
typedef struct {
    int first;                         /* the first save shown */
    int sel;                           /* the chosen save, or -1 (connected: view only) */
    UiBox box;
    int keys;                          /* UI_KEYS_* */
} UiSaves;
/* The memory card's saves (card_name's). Returns how many there are. */
int  ui_page_saves(const unsigned char *card, int have, const UiSaves *v, const UiTheme *t, unsigned now_ms,
                   const char *card_name);

/* The Cards page, beside the VMU: "New card" first, then each card */
#define UI_CARDS_ROWS 5                /* rows shown at once ("New card" one of them) */
#define UI_CARDS_MAX 32
typedef struct {
    int n;                             /* cards */
    const char *name[UI_CARDS_MAX];
    int saves[UI_CARDS_MAX], free[UI_CARDS_MAX], in_use[UI_CARDS_MAX];
    const char *game[UI_CARDS_MAX];    /* the VMU game on it, or NULL */
    const char *title[UI_CARDS_MAX];   /* the disc game it is for (Card per game), or NULL */
    int first, sel;                    /* rows: 0 is "New card", 1 + i card i; sel -1: view only */
    int new_off;                       /* "New card" cannot be used now (greyed) */
    UiBox box;
    int keys;
} UiCards;
void ui_page_cards(const UiCards *v, const UiTheme *t);

/* The Settings page, beside the VMU: the settings, then the connection */
#define UI_SYS_ROWS 6
typedef struct {
    int link;
    const char *link_text, *link_hint;
    const char *lines[4];              /* about the adapter and the card, NULL-ended */
    const char *rows[UI_SYS_ROWS], *values[UI_SYS_ROWS];   /* the settings: a name, its value */
    int nrows, sel;                    /* sel: the row chosen */
    unsigned off_rows;                 /* bits: rows that cannot be used now (greyed) */
    unsigned arrow_rows;               /* bits: rows shown as ◀ value ▶ */
    int keys;
} UiSystem;
void ui_page_system(const UiSystem *s, const UiTheme *t);
/* a panel of settings at x, y, w wide: a name and its value a row, the
 * chosen one lit, greyed those in off; the rows in arrows (bits) show
 * ◀ value ▶ */
void ui_settings(int x, int y, int w, const char *head, const char *const *rows, const char *const *values,
                 int n, int sel, unsigned off, unsigned arrows, const UiTheme *t);

#endif
