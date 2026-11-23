/*
 * The companion (psp2dc_protocol.h; docs/psp2dc-companion.md, Appendices B
 * and C): what a Dreamcast program built for it sends through the adapter.
 * Rows of text, test data that is checked and counted, bytes to send back;
 * and the screen: widgets the program made, and regions it draws into.
 * Nothing here is the PSP's own, so the same file runs on the computer
 * (scripts/dev/companion-sim).
 */
#ifndef PSP2DC_COMPANION_H
#define PSP2DC_COMPANION_H

#include "../../protocol/psp2dc_protocol.h"

#define COMP_ROWS 12
#define COMP_COLS 66
typedef struct {
    unsigned char used, type, flags;   /* PSP2DC_WIDGET_*, PSP2DC_WF_* */
    short x, y;
    unsigned short w, h, colour;
    int value, max;
    char text[PSP2DC_WIDGET_TEXT_MAX + 1];
} CompWidget;
typedef struct {
    short x, y;
    unsigned short w, h;               /* w 0: none */
    short cx0, cy0, cx1, cy1;          /* drawing is kept inside (cx1 and cy1 are outside) */
    unsigned *back, *front;            /* w * h pixels each (ABGR): the one drawn into, the one shown */
    unsigned pal[256];                 /* its palette (ABGR) */
} CompRegion;
typedef struct {
    unsigned short w, h;               /* w 0: none */
    unsigned char format;              /* PSP2DC_FMT_8 (a byte a pixel, whatever it came as) or PSP2DC_FMT_565 */
    unsigned char *pixels;
} CompSprite;
typedef struct {
    char rows[COMP_ROWS][COMP_COLS + 1];
    unsigned bytes, records;           /* of the stream, since the session began */
    unsigned test_records, test_bytes; /* TEST records, since the last CLEAR */
    unsigned test_bad;                 /* ... out of turn, or with a wrong byte */
    unsigned stream_bad;               /* records that made no sense (the rest of the session is dropped) */
    unsigned sessions;
    unsigned last_us;                  /* when bytes last came (0: never) */
    /* the screen */
    CompWidget widgets[PSP2DC_WIDGETS];
    CompRegion regions[PSP2DC_REGIONS];
    CompSprite sprites[PSP2DC_SPRITES];
    unsigned sprite_bytes;             /* of PSP2DC_SPRITE_BYTES, taken */
    int screen;                        /* a widget or a region is there: the screen is the program's */
    char notice[64];                   /* a NOTIFY's text */
    unsigned notice_us;                /* ... and when it came (0: none) */
    unsigned screen_bad;               /* screen records that made no sense (each is skipped) */
    volatile unsigned gen;             /* moves when anything of the screen changes */
    volatile unsigned focus;           /* the PSP's buttons the program took (FOCUS), this session */
} Comp;
extern Comp g_comp;

void comp_reset(void);                             /* a session begins: nothing half read, an empty screen */
void comp_feed(const unsigned char *p, int n);     /* the stream's next bytes (the reader thread) */
/* a record for the Dreamcast, ready to go (the answer to a QUERY, an ECHO's
 * bytes back): its length, or 0 */
int  comp_event_take(unsigned char out[PSP2DC_COMP_EVENT_MAX]);

#endif
