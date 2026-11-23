/*
 * PSP2DC tutorial, step 5: pictures
 *
 * Real artwork on the PSP: a palette, sprites the PSP keeps, and a bitmap.
 *
 * NEW IN THIS STEP
 *   psp2dc_region_palette()   the region's colours, for pictures that index them
 *   psp2dc_sprite_create()    a picture the PSP keeps, sent once
 *   psp2dc_sprite_ready()     ... has all of it arrived?
 *   psp2dc_draw_sprite()      draw it anywhere, 12 bytes each time
 *   psp2dc_draw_bitmap()      pixels straight into a region, in pieces
 *   dreamcast/tools/psp2dc-image.py   a PNG as C, for all of the above
 *
 * YOU SEE (PSP)  a field of grass tiles with coins on it, a hero the d-pad
 * moves from tile to tile, and under the field a strip of colour that the
 * program computed and sent as a bitmap.
 *
 * TRY  The d-pad: walk over the coins.
 *
 * THE ARTWORK  art.png is 48 x 16: three 16 x 16 pictures side by side
 * (grass, a coin, a hero). The tool turned it into art.h:
 *
 *     python3 dreamcast/tools/psp2dc-image.py art.png art > art.h
 *
 * It chose the smallest format the colours fit (4 bits a pixel: 14 colours),
 * and gave us art[] (the pixels), art_palette[] (the colours), ART_STRIDE
 * (bytes from one row to the next) and ART_KEY (the "colour" that means
 * transparent: pixels that were see-through in the PNG).
 *
 * SPRITE OR BITMAP?  A sprite is sent once and then costs 12 bytes each time
 * it is drawn: use it for whatever is drawn often (tiles, icons, a player).
 * A bitmap is sent in full each time: use it for a picture drawn once. At
 * 59 KB a second a full 240 x 240 picture in 8 bits takes about a second.
 *
 * NEXT  Step 6 takes some of the PSP's buttons for a menu on the PSP.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"
#include "art.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define R 0
#define TILE 16
#define COLS 14                    /* the field, in tiles */
#define ROWS 10
#define RW (COLS * TILE)           /* 224 */
#define STRIP 16                   /* the bitmap's height, under the field */
#define RH (ROWS * TILE + STRIP)   /* 176 */

enum { S_GRASS, S_COIN, S_HERO };  /* our sprite ids (0 to 63): the three pictures of art.png */

static char coins[ROWS][COLS + 1] = {
    "..............", "..c.......c...", "..............", "......c.......", "..............",
    ".c..........c.", "..............", "....c....c....", "..............", "..............",
};
static uint8_t strip[STRIP][RW];   /* the bitmap: a byte a pixel (PSP2DC_FMT_8) */

/* One tile of the field as it is now: grass, then what stands on it. The grass is drawn
 * whole (key -1: no transparency); the coin and the hero leave out ART_KEY's pixels, so
 * the grass shows around them. 1 when it was queued, 0 when the queue is full. */
static int draw_tile(int tx, int ty, int hero_x, int hero_y)
{
    if (psp2dc_draw_sprite(R, S_GRASS, tx * TILE, ty * TILE, -1)) return 0;
    if (coins[ty][tx] == 'c' && psp2dc_draw_sprite(R, S_COIN, tx * TILE, ty * TILE, ART_KEY)) return 0;
    if (tx == hero_x && ty == hero_y && psp2dc_draw_sprite(R, S_HERO, tx * TILE, ty * TILE, ART_KEY)) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    int hx = 7, hy = 5;            /* the hero's tile */
    int shown_x = -1, shown_y = 0; /* where the PSP's picture has the hero */
    int session = -1;              /* the session the field was drawn for */
    int next_tile = 0;             /* drawing the field: the tile to go on with */
    int strip_rows = 0;            /* drawing the bitmap: rows that have gone */
    int score = 0, i, j;
    unsigned last = 0;
    (void)argc; (void)argv;

    bfont_draw_str(vram_s + 40 * 640 + 40, 640, 1, "PSP2DC tutorial 5: pictures (see the PSP)");
    psp2dc_init();

    /* the bitmap's pixels: a byte each, which the PSP looks up in the region's palette.
     * We use entries 16 to 255 for it (art.png's colours are in 0 to 13) */
    for (j = 0; j < STRIP; j++) for (i = 0; i < RW; i++) strip[j][i] = (uint8_t)(16 + i * 240 / RW);

    psp2dc_region_create(R, 16, 40, RW, RH);
    psp2dc_widget_create(0, PSP2DC_WIDGET_LABEL, PSP2DC_WF_BIG, 16, 8, 240, 24, "COIN FIELD");
    psp2dc_widget_create(1, PSP2DC_WIDGET_NUMBER, PSP2DC_WF_PANEL | PSP2DC_WF_BIG, 256, 40, 208, 32, "Coins");

    /* THE PALETTE: (region, first entry, how many, colours). Pictures of 1, 4 and 8 bits a
     * pixel are numbers into it. It belongs to the region and is remembered with it. Set it
     * from entry 0 up, without gaps. Here: art.png's 14 colours, two spare, then a run of
     * 240 for the bitmap. It returns -1 when it cannot go just now, so it is tried again
     * (a program with one palette can simply call it each frame until it returns 0). */
    {
        static uint16_t pal[256];
        for (i = 0; i < 14; i++) pal[i] = art_palette[i];
        for (i = 16; i < 256; i++) pal[i] = PSP2DC_RGB565(255 - (i - 16), 60, i - 16);   /* red to blue */
        while (psp2dc_region_palette(R, 0, 256, pal) != 0) { vid_waitvbl(); psp2dc_update(); }
    }

    /* SPRITES: (id, width, height, format, pixels, stride). The pixels are NOT copied: the
     * library sends them from where they are, a little each frame, and again whenever the
     * PSP starts over. So they must stay put (here they are constants in art.h).
     *
     * art.png holds three pictures side by side. Each is 16 pixels wide, which is 8 bytes
     * at 4 bits a pixel, so picture n starts n * 8 bytes into each row; the stride stays
     * the whole image's, since that is how far apart its rows are. */
    psp2dc_sprite_create(S_GRASS, TILE, TILE, ART_FORMAT, art + 0, ART_STRIDE);
    psp2dc_sprite_create(S_COIN, TILE, TILE, ART_FORMAT, art + 8, ART_STRIDE);
    psp2dc_sprite_create(S_HERO, TILE, TILE, ART_FORMAT, art + 16, ART_STRIDE);

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0, pressed = b & ~last;
        last = b;

        vid_waitvbl();
        psp2dc_update();

        if ((pressed & CONT_DPAD_LEFT) && hx > 0) hx--;
        if ((pressed & CONT_DPAD_RIGHT) && hx < COLS - 1) hx++;
        if ((pressed & CONT_DPAD_UP) && hy > 0) hy--;
        if ((pressed & CONT_DPAD_DOWN) && hy < ROWS - 1) hy++;
        if (coins[hy][hx] == 'c') { coins[hy][hx] = '.'; score++; }
        psp2dc_widget_set_u32(1, (uint32_t)score);

        if (!psp2dc_psp()) continue;

        /* A new session: everything in the region is to be drawn again (step 4). The
         * palette and the sprites are sent again for us. */
        if (psp2dc_session() != session) {
            session = psp2dc_session();
            next_tile = 0;
            strip_rows = 0;
            shown_x = -1;
        }

        /* A sprite can only be drawn once the PSP has all of it. They arrive within a few
         * frames of a new session; until then there is nothing to do. */
        if (!psp2dc_sprite_ready(S_GRASS) || !psp2dc_sprite_ready(S_COIN) || !psp2dc_sprite_ready(S_HERO)) continue;

        /* THE FIELD, as much of it as the queue takes this frame: 140 tiles at 12 bytes
         * and more each do not always fit at once, so `next_tile` remembers how far it got. */
        while (next_tile < COLS * ROWS && draw_tile(next_tile % COLS, next_tile / COLS, hx, hy)) next_tile++;
        if (next_tile < COLS * ROWS) continue;

        /* THE BITMAP: (region, x, y, width, height, format, pixels, stride). It returns how
         * many ROWS it queued, which may be fewer than asked: go on with the rest next
         * frame. (It always leaves room in the queue, so widgets are never held up.) */
        if (strip_rows < STRIP) {
            int went = psp2dc_draw_bitmap(R, 0, ROWS * TILE + strip_rows, RW, STRIP - strip_rows, PSP2DC_FMT_8,
                                          strip[strip_rows], RW);
            if (went > 0) strip_rows += went;
            if (strip_rows < STRIP) continue;
        }                                        /* (shown by the present below: shown_x is -1) */

        /* THE HERO MOVED: two tiles change, the one left and the one entered. 24 to 48
         * bytes, whatever the artwork's size. */
        if (hx != shown_x || hy != shown_y) {
            if (shown_x >= 0 && !draw_tile(shown_x, shown_y, hx, hy)) continue;
            if (!draw_tile(hx, hy, hx, hy)) continue;
            if (psp2dc_present(R)) continue;
            shown_x = hx; shown_y = hy;
        }
    }
    return 0;
}
