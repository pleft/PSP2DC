/*
 * PSP2DC sample: hello. The least a program needs: find the companion, call
 * psp2dc_update() once a frame, write on the PSP, and read the PSP's buttons.
 * The television shows what the program knows.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

static void line(int row, const char *text)
{
    char s[41];
    snprintf(s, sizeof(s), "%-40s", text);       /* (what was there before is written over) */
    bfont_draw_str(vram_s + (40 + row * 24) * 640 + 40, 640, 1, s);
}

int main(int argc, char **argv)
{
    psp2dc_event_t ev;
    unsigned psp_buttons = 0;
    int printed = -1;                            /* the session the PSP's page was written for */
    char s[64];
    (void)argc; (void)argv;

    line(0, "PSP2DC hello");
    psp2dc_init();                               /* looks for the adapter; it goes on looking if not found */

    for (;;) {
        vid_waitvbl();
        psp2dc_update();                         /* once a frame: what is queued goes, what came is taken */

        /* A PSP that is new, or back after being unplugged, starts empty: write the page again. */
        if (psp2dc_psp() && psp2dc_session() != printed &&
            psp2dc_clear() == 0 && psp2dc_print(0, "Hello from the Dreamcast!") == 0 &&
            psp2dc_print(2, "Press the PSP's buttons: the television shows them.") == 0)
            printed = psp2dc_session();

        /* what the PSP sent: its buttons (PSP2DC_PSP_*), whenever they change */
        while (psp2dc_poll(&ev))
            if (ev.type == PSP2DC_REC_BUTTONS && ev.len >= 4)
                psp_buttons = ev.data[0] | ev.data[1] << 8;

        line(1, psp2dc_found() ? "adapter: found" : "adapter: looking...");
        line(2, psp2dc_psp() ? "PSP: there" : "PSP: not there");
        snprintf(s, sizeof(s), "PSP: %s%s%s%s%s%s", psp_buttons & PSP2DC_PSP_CROSS ? "X " : "",
                 psp_buttons & PSP2DC_PSP_CIRCLE ? "O " : "", psp_buttons & PSP2DC_PSP_SQUARE ? "[] " : "",
                 psp_buttons & PSP2DC_PSP_TRIANGLE ? "/\\ " : "", psp_buttons & PSP2DC_PSP_L ? "L " : "",
                 psp_buttons & PSP2DC_PSP_R ? "R " : "");
        line(3, s);
    }
    return 0;
}
