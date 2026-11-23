/*
 * PSP2DC tutorial, step 1: hello
 *
 * The least a program needs: find the adapter, call the library once a frame,
 * and write two rows of text on the PSP.
 *
 * NEW IN THIS STEP
 *   psp2dc_init()       look for the adapter
 *   psp2dc_update()     the one call every frame
 *   psp2dc_found()      is the adapter there?
 *   psp2dc_psp()        ... and a PSP on it?
 *   psp2dc_clear(), psp2dc_print()   rows of text on the PSP
 *   psp2dc_session()    has the PSP started over?
 *
 * YOU SEE
 *   television   whether the adapter and the PSP are there
 *   PSP          "Hello from the Dreamcast!"
 *
 * TRY  Unplug the PSP and plug it back in: the text returns by itself.
 *
 * NEXT  Step 2 reads the PSP's buttons.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

/* A row of text on the television, 24 pixels a row. Padded to its full width, so that a
 * shorter text written later leaves nothing of a longer one behind. Every step has it. */
static void line(int row, const char *text)
{
    char s[41];
    snprintf(s, sizeof(s), "%-40s", text);
    bfont_draw_str(vram_s + (40 + row * 24) * 640 + 40, 640, 1, s);
}

int main(int argc, char **argv)
{
    int printed = -1;             /* the session our text was written for (-1: none yet) */
    (void)argc; (void)argv;

    line(0, "PSP2DC tutorial 1: hello");

    /* Looks for the adapter on every memory card unit, waiting up to half a second for
     * each. Not finding it is no error: psp2dc_update() goes on looking, a unit a second,
     * so the adapter can be plugged in later. */
    psp2dc_init();

    for (;;) {
        vid_waitvbl();

        /* Once a frame, always. It sends one Maple command at most (what the calls below
         * queued, or an empty poll) and takes in what the PSP sent back. Nothing in the
         * library but psp2dc_init() ever waits, so this costs the frame nothing. */
        psp2dc_update();

        /* SESSIONS, the one idea to take from this step.
         *
         * The PSP can go and come back at any moment: unplugged, its application started
         * again, its link reset. Each time it starts over empty, and psp2dc_session()
         * changes. So a program does not print once at its start: it prints whenever the
         * session is not the one it last printed for.
         *
         * psp2dc_clear() and psp2dc_print() return 0 when queued, and not 0 when they
         * could not be (the queue is full for now). `printed` is set only when all of
         * them went, so a failure just means the same lines are tried again next frame. */
        if (psp2dc_psp() && psp2dc_session() != printed &&
            psp2dc_clear() == 0 &&
            psp2dc_print(0, "Hello from the Dreamcast!") == 0 &&      /* (row 0 to 11, text) */
            psp2dc_print(2, "This is row 2 of the PSP's 12 rows.") == 0)
            printed = psp2dc_session();

        line(1, psp2dc_found() ? "adapter: found" : "adapter: looking...");
        line(2, psp2dc_psp() ? "PSP: there" : "PSP: not there");
    }
    return 0;
}
