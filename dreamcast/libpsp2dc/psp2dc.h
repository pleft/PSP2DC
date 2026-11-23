/*
 * libpsp2dc: the PSP2DC companion, from a KallistiOS program
 * (docs/psp2dc-companion.md; the wire format is protocol/psp2dc_protocol.h).
 *
 * Find the companion; put widgets on the PSP's screen and keep them up to
 * date, draw into regions of it; take what the PSP sends (its buttons); keep
 * files on its Memory Stick. And
 * the plain records underneath: text rows, test data, bytes to send back.
 *
 * Call psp2dc_update() once a frame. It sends one Maple command per vblank
 * at most: what was queued since the last one, or an empty poll, and every
 * reply brings what the PSP sent meanwhile. Nothing here waits, except
 * psp2dc_init().
 */
#ifndef PSP2DC_H
#define PSP2DC_H

#include <stdint.h>
#include "psp2dc_protocol.h"   /* the widgets' types and flags, PSP2DC_RGB565 */

#define PSP2DC_RECORD_MAX 1000   /* a record's data, at most */

/* Looks for the companion on every memory card unit, waiting up to half a
 * second for each. 0 when found, -1 when there is none: psp2dc_update()
 * goes on looking, a unit a second, without waiting. */
int  psp2dc_init(void);
/* Once a frame. */
void psp2dc_update(void);

/* What the last reply said. */
int  psp2dc_found(void);          /* an adapter that answers the companion's command */
int  psp2dc_psp(void);            /* ... with a PSP on it */
int  psp2dc_version(void);        /* the adapter's protocol version */
int  psp2dc_session(void);        /* changes when the PSP starts over, or records queued here were
                                   * dropped (the companion went away): print the rows again, draw
                                   * the regions again (the widgets and the regions themselves are
                                   * sent again for you) */
void psp2dc_where(int *port, int *unit);

/* A record for the PSP, queued for the next command. 0, or -1 when it does
 * not fit yet (nothing queued: try again next frame), -2 without a PSP. */
int  psp2dc_record(int type, const void *data, int len);
int  psp2dc_clear(void);
int  psp2dc_print(int row, const char *text);
/* How much of the queue is free; whether everything queued has been sent. */
int  psp2dc_room(void);
int  psp2dc_idle(void);

/* The next record from the PSP: its type and data (len bytes, at most 56).
 * 1 when there was one, 0 when not. */
typedef struct {
    int type;                     /* PSP2DC_REC_BUTTONS, PSP2DC_REC_ECHO */
    int len;
    uint8_t data[56];
} psp2dc_event_t;
int  psp2dc_poll(psp2dc_event_t *ev);

/*
 * The screen: the PSP's whole screen (480 x 272) is the program's once it
 * has a widget or a region. Colours are PSP2DC_RGB565(r, g, b).
 *
 * Widgets are remembered here: these calls only note what changed, and
 * psp2dc_update() sends it when it can, and all of it again when the PSP
 * starts over. So they work with no PSP there, and setting a value every
 * frame costs nothing unless it changed. 0, or -1 for a wrong id or type.
 *   type   PSP2DC_WIDGET_LABEL, _NUMBER, _BAR, _LIGHT, _LIST
 *   flags  PSP2DC_WF_BIG, _CENTRE, _RIGHT, _PANEL
 */
int  psp2dc_widget_create(int id, int type, int flags, int x, int y, int w, int h, const char *text);
int  psp2dc_widget_set_u32(int id, uint32_t value);     /* a number, a bar's fill, a light on or off, a list's marked line */
int  psp2dc_widget_set_text(int id, const char *text);  /* (a list: its lines, parted by '\n') */
int  psp2dc_widget_set_range(int id, uint32_t max);     /* a bar's full value (100 at first) */
int  psp2dc_widget_set_colour(int id, int rgb565);      /* -1: the PSP's theme chooses again */
int  psp2dc_widget_remove(int id);
void psp2dc_screen_reset(void);                         /* every widget and region gone */

/* A region is a rectangle of the screen the program draws into, under the
 * widgets. Making one empties every region. It is remembered and made
 * again when the PSP starts over, but not what was drawn: draw it again
 * when psp2dc_session() changes. 0, -1 for a wrong id or size or when the
 * queue is full (try again next frame). */
int  psp2dc_region_create(int id, int x, int y, int w, int h);
int  psp2dc_region_remove(int id);
/* Drawing, in the region's own coordinates; shown at the next
 * psp2dc_present(). 0, -1 when the queue is full or there is no such
 * region, -2 without a PSP (or one whose screen is not started yet). */
int  psp2dc_draw_rect(int region, int x, int y, int w, int h, int rgb565);
int  psp2dc_draw_line(int region, int x0, int y0, int x1, int y1, int rgb565);
int  psp2dc_draw_text(int region, int x, int y, int rgb565, int size, const char *text);   /* size 1 to 4 */
int  psp2dc_region_clip(int region, int x, int y, int w, int h);   /* drawing kept inside, from here on */
int  psp2dc_present(int region);
/* The PSP's buttons the program takes (PSP2DC_PSP_*, or-ed; 0: none): they
 * reach it through psp2dc_poll() and no longer the Dreamcast's controller.
 * Remembered, and sent again when the PSP starts over; SELECT and the PSP's
 * own buttons cannot be taken. A PSP whose CAPS level is under 5 ignores it:
 * the buttons then go to both. */
void psp2dc_focus(uint32_t buttons);
/* A line of text over the PSP's screen for a moment. */
int  psp2dc_notify(const char *text);

/*
 * Pictures. format: PSP2DC_FMT_1, _4, _8 (indexes into the region's palette)
 * or _565; rows are `stride` bytes apart, each starting on a byte.
 *
 * A region's palette: `count` colours from entry `first` on. Remembered and
 * sent again with the region; set it from entry 0 up. 0, or -1 (try again).
 */
int  psp2dc_region_palette(int region, int first, int count, const uint16_t *rgb565);
/* A bitmap into a region, as many of its rows as the queue takes now: how
 * many went (go on with the rest next frame), or -1 for what cannot be, -2
 * without a PSP. Room is left in the queue, so widgets are never held up. */
int  psp2dc_draw_bitmap(int region, int x, int y, int w, int h, int format, const void *pixels, int stride);
/* A sprite: a picture the PSP keeps and draws by its id (0 to 63). The
 * pixels are not copied: they must stay where they are. psp2dc_update()
 * sends them a little at a time, and again when the PSP starts over;
 * psp2dc_sprite_ready() says when the PSP has them all. */
int  psp2dc_sprite_create(int id, int w, int h, int format, const void *pixels, int stride);
int  psp2dc_sprite_ready(int id);
/* key: a palette index (or with PSP2DC_FMT_565 a colour) not drawn, or -1.
 * 0, -1 when the sprite is not ready or the queue is full, -2 without a PSP. */
int  psp2dc_draw_sprite(int region, int id, int x, int y, int key);

/* What the PSP can show, once it has said (shortly after each new session):
 * 1 and the answer, or 0 (not yet, or a PSP whose application is older than
 * the screen: then nothing above shows). */
typedef struct {
    int level;                    /* 2: widgets and regions; 3: pictures too; 4: files too; 5: focus too */
    int width, height;            /* the screen */
    int widgets, regions;         /* how many of each */
    int region_pixels;            /* all regions together, at most */
    int text_max;                 /* a widget's text, at most */
    uint32_t widget_types;        /* bit n: PSP2DC_WIDGET_ type n */
    uint32_t formats;             /* bit n: PSP2DC_FMT_ format n */
    int sprites, sprite_bytes;    /* how many sprites, and their pixels together at most */
    uint32_t files_max;           /* level 4: files can be kept, this many bytes of them */
} psp2dc_caps_t;
int  psp2dc_get_caps(psp2dc_caps_t *out);

/*
 * Files: the program's own, on the PSP's Memory Stick, in a folder of its own
 * (ms0:/PSP2DC/games/<folder>/). One operation at a time; each starts here
 * and goes on in psp2dc_update(), and psp2dc_file_status() says how it
 * ended. A buffer handed over stays the program's, and must stay where it is
 * until then. A write replaces the file only once all of it has arrived: a
 * file is what it was or what was written, whenever the PSP goes.
 *
 * Names: up to 63 of a-z 0-9 . - _ and /, no part empty or starting with a
 * dot ("levels/map01.dat"). The folder's: up to 16 of a-z 0-9 - _.
 *
 * A start returns 0; -1 when not now (another operation is going, or the PSP
 * has just come and not yet said what it can: try again next frame); -2 when
 * it cannot be (no PSP, one whose application keeps no files, no folder
 * given, a name not allowed).
 */
int  psp2dc_files(const char *folder);           /* once, before the rest: 0, or -1 for a name not allowed */
int  psp2dc_file_write(const char *name, const void *data, uint32_t size);
int  psp2dc_file_read(const char *name, uint32_t offset, void *buf, uint32_t len);   /* n: the bytes read */
int  psp2dc_file_size(const char *name);         /* n: its size */
int  psp2dc_file_delete(const char *name);
int  psp2dc_file_list(char *buf, uint32_t len);  /* a line a file, "name size\n", subfolders' with their
                                                  * path; n: its bytes (not ended by a 0) */
int  psp2dc_files_free(void);                    /* n: the bytes the folder may still take */
#define PSP2DC_FILE_DONE       0
#define PSP2DC_FILE_BUSY       1
#define PSP2DC_FILE_NONE       2                 /* nothing started yet */
#define PSP2DC_FILE_NOT_FOUND -1
#define PSP2DC_FILE_BAD_NAME  -2
#define PSP2DC_FILE_NO_ROOM   -3                 /* the folder's limit, or the Memory Stick full */
#define PSP2DC_FILE_GONE      -4                 /* the PSP went, or started over: start it again */
#define PSP2DC_FILE_FAILED    -5                 /* the Memory Stick failed */
int  psp2dc_file_status(uint32_t *n);            /* PSP2DC_FILE_*; *n as each start says, once DONE */
/* A read's bytes asked for and not yet answered, at most (3 KB as the library starts). The
 * adapter drops what the PSP sends past its 4 KB queue, so more than that loses bytes. For tests. */
void psp2dc_file_window(int bytes);

/* Counts, for measuring: commands answered, bytes of records sent, bytes
 * received, commands that got no answer or a wrong one. */
typedef struct {
    uint32_t commands, bytes_out, bytes_in, errors;
} psp2dc_stats_t;
void psp2dc_stats(psp2dc_stats_t *out);
/* The most data a command carries (records are sent as many whole ones as fit;
 * one longer than this goes alone). As the library starts: all the protocol
 * allows, PSP2DC_COMP_DATA_MAX. For tests of what the bus carries. */
void psp2dc_send_max(int bytes);

#endif
