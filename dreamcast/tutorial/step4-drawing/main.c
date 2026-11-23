/*
 * PSP2DC tutorial, step 4: drawing
 *
 * Regions: rectangles of the PSP's screen that the program draws into
 * itself, for what no widget shows (a map, a radar, a graph).
 *
 * NEW IN THIS STEP
 *   psp2dc_region_create()   a rectangle of pixels on the PSP, black at first
 *   psp2dc_draw_rect()       a filled rectangle
 *   psp2dc_draw_line()       a line
 *   psp2dc_draw_text()       text, 1 to 4 times the 8-pixel font
 *   psp2dc_region_clip()     keep drawing inside a part of the region
 *   psp2dc_present()         show what was drawn
 *
 * YOU SEE (PSP)  a radar: a frame, a grid, a dot the d-pad moves, and the
 * dot's position as text.
 *
 * TRY  The d-pad. Then unplug the PSP and plug it back: the whole picture
 * is drawn again.
 *
 * TWO THINGS DIFFER FROM WIDGETS
 *
 * 1. The PSP keeps the pixels, the library does not. Draw only what changes
 *    (here: erase the dot where it was, draw it where it is). But when the
 *    PSP starts over, the region is made again EMPTY, and the program has to
 *    draw everything again: psp2dc_session() is back, as in step 1.
 *
 * 2. A drawing call can fail. It returns 0 when queued, -1 when the queue is
 *    full (try again next frame) and -2 when no PSP is ready. So drawing is
 *    written as "what is still to draw", and tried each frame until it went.
 *
 * NEXT  Step 5 draws pictures instead of rectangles.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define R 0                        /* our region's id (0 to 3) */
#define RW 240                     /* its size: a region is at most the screen, and all */
#define RH 200                     /* regions together at most 480 x 272 pixels */
#define GRID 40

#define BLACK  PSP2DC_RGB565(0, 0, 0)
#define GREEN  PSP2DC_RGB565(40, 200, 90)
#define DIM    PSP2DC_RGB565(20, 70, 40)
#define YELLOW PSP2DC_RGB565(250, 220, 60)

/* The whole picture but the dot: 1 when all of it was queued, 0 when the queue filled up
 * on the way (nothing is lost by drawing some of it twice, so it simply starts over). */
static int draw_background(void)
{
    int i;
    if (psp2dc_region_clip(R, 0, 0, RW, RH)) return 0;   /* (whatever an earlier try left it at) */
    if (psp2dc_draw_rect(R, 0, 0, RW, RH, BLACK)) return 0;
    for (i = GRID; i < RW; i += GRID) if (psp2dc_draw_line(R, i, 0, i, RH - 1, DIM)) return 0;
    for (i = GRID; i < RH; i += GRID) if (psp2dc_draw_line(R, 0, i, RW - 1, i, DIM)) return 0;
    /* the frame: four lines (x0, y0, x1, y1, colour), in the region's own coordinates */
    if (psp2dc_draw_line(R, 0, 0, RW - 1, 0, GREEN) || psp2dc_draw_line(R, 0, RH - 1, RW - 1, RH - 1, GREEN) ||
        psp2dc_draw_line(R, 0, 0, 0, RH - 1, GREEN) || psp2dc_draw_line(R, RW - 1, 0, RW - 1, RH - 1, GREEN)) return 0;
    return 1;
}

/* START + A + B + X + Y together: back to the Dreamcast's menu, as a commercial game does
 * (with GDEMU and openMenu: to choose the next disc). KallistiOS watches for the buttons. */
static void to_menu(uint8_t addr, uint32_t buttons) { (void)addr; (void)buttons; arch_menu(); }

int main(int argc, char **argv)
{
    int x = RW / 2, y = RH / 2;    /* the dot */
    int drawn_x = -1, drawn_y = 0; /* where the PSP's picture has it (-1: nowhere) */
    int session = -1;              /* the session the background was drawn for */
    char s[32];
    (void)argc; (void)argv;

    bfont_draw_str(vram_s + 40 * 640 + 40, 640, 1, "PSP2DC tutorial 4: drawing (see the PSP)");
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, to_menu);
    psp2dc_init();

    /* A REGION: (id, x, y, width, height) on the PSP's screen. Like a widget it is
     * remembered and made again when the PSP starts over; unlike a widget, what was drawn
     * in it is not. Regions are under the widgets, so a label can sit on top of one. */
    psp2dc_region_create(R, 16, 36, RW, RH);
    psp2dc_widget_create(0, PSP2DC_WIDGET_LABEL, PSP2DC_WF_BIG, 16, 8, 240, 24, "RADAR");
    psp2dc_widget_create(1, PSP2DC_WIDGET_LABEL, 0, 272, 44, 200, 12, "The d-pad moves the dot.");

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0;

        vid_waitvbl();
        psp2dc_update();

        if ((b & CONT_DPAD_LEFT) && x > 6) x -= 2;
        if ((b & CONT_DPAD_RIGHT) && x < RW - 7) x += 2;
        if ((b & CONT_DPAD_UP) && y > 6) y -= 2;
        if ((b & CONT_DPAD_DOWN) && y < RH - 7) y += 2;

        if (!psp2dc_psp()) continue;             /* nobody to draw for */

        /* A NEW SESSION: the region is there again, and empty. Everything is to be drawn. */
        if (psp2dc_session() != session) {
            if (!draw_background()) continue;    /* the queue is full: the rest next frame */
            session = psp2dc_session();
            drawn_x = -1;                        /* ... and the dot is on it nowhere */
        }

        /* WHAT CHANGED: the dot, when it moved. Erase it where the PSP has it, draw it
         * where it is, write its position, and show the result.
         *
         * Each call may return not 0 (the queue is full). Then we stop and `drawn_x` stays
         * as it was, so next frame the same steps are tried again. Drawing something a
         * second time does no harm. */
        if (x != drawn_x || y != drawn_y) {
            /* (first the clip opened: a try that stopped half way may have left it small) */
            if (psp2dc_region_clip(R, 0, 0, RW, RH)) continue;
            if (drawn_x >= 0 && psp2dc_draw_rect(R, drawn_x - 4, drawn_y - 4, 9, 9, BLACK)) continue;
            /* (the erasing also took a piece of the grid with it: a real program would
             * draw that piece again; here it shows where the dot has been) */
            if (psp2dc_draw_rect(R, x - 3, y - 3, 7, 7, YELLOW)) continue;

            /* TEXT in a corner. CLIP keeps drawing inside a rectangle from here on, so a
             * long text cannot run over the frame; then the clip is opened up again. */
            snprintf(s, sizeof(s), "%3d,%3d", x, y);
            if (psp2dc_region_clip(R, 4, 4, 64, 10) ||
                psp2dc_draw_rect(R, 4, 4, 64, 10, BLACK) ||
                psp2dc_draw_text(R, 5, 5, GREEN, 1, s) ||      /* (x, y, colour, size 1 to 4, text) */
                psp2dc_region_clip(R, 0, 0, RW, RH)) continue;

            /* PRESENT: nothing drawn shows until this. The PSP draws into a hidden copy
             * and swaps, so the player never sees a picture half done. */
            if (psp2dc_present(R)) continue;
            drawn_x = x; drawn_y = y;
        }
    }
    return 0;
}
