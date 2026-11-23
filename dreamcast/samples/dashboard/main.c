/*
 * PSP2DC sample: dashboard. Widgets on the PSP's screen, made once and then
 * only given values: the library sends what changed, and everything again
 * when the PSP comes back. The controller drives them; the PSP's L and R
 * move the mark in a list, and are taken from the controller for it.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

enum { W_TITLE, W_FRAMES, W_TRIGGER, W_HELD, W_LIST };   /* widget ids, 0 to 63 */

int main(int argc, char **argv)
{
    psp2dc_event_t ev;
    unsigned frame = 0, psp_last = 0;
    int marked = 0;
    (void)argc; (void)argv;

    bfont_draw_str(vram_s + 40 * 640 + 40, 640, 1, "PSP2DC dashboard: look at the PSP");
    psp2dc_init();

    /* Made once, with or without a PSP there: they go when one comes. Places are pixels of
     * the PSP's 480 x 272 screen. */
    psp2dc_widget_create(W_TITLE, PSP2DC_WIDGET_LABEL, PSP2DC_WF_BIG | PSP2DC_WF_CENTRE, 0, 16, 480, 24, "DASHBOARD");
    psp2dc_widget_create(W_FRAMES, PSP2DC_WIDGET_NUMBER, PSP2DC_WF_PANEL, 40, 60, 400, 24, "Frames");
    psp2dc_widget_create(W_TRIGGER, PSP2DC_WIDGET_BAR, PSP2DC_WF_PANEL, 40, 94, 400, 36, "Right trigger");
    psp2dc_widget_set_range(W_TRIGGER, 255);
    psp2dc_widget_create(W_HELD, PSP2DC_WIDGET_LIGHT, 0, 40, 140, 400, 14, "A held");
    psp2dc_widget_create(W_LIST, PSP2DC_WIDGET_LIST, PSP2DC_WF_PANEL, 40, 164, 400, 80,
                         "First line\nSecond line\nThird line");
    psp2dc_focus(PSP2DC_PSP_L | PSP2DC_PSP_R);   /* L and R move the mark, and do not press the triggers */

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        vid_waitvbl();
        frame++;
        psp2dc_update();

        while (psp2dc_poll(&ev)) {
            unsigned pb;
            if (ev.type != PSP2DC_REC_BUTTONS || ev.len < 4) continue;
            pb = ev.data[0] | ev.data[1] << 8;
            if ((pb & PSP2DC_PSP_L) && !(psp_last & PSP2DC_PSP_L)) marked = (marked + 2) % 3;
            if ((pb & PSP2DC_PSP_R) && !(psp_last & PSP2DC_PSP_R)) marked = (marked + 1) % 3;
            psp_last = pb;
        }

        /* every value, every frame: only what changed is sent */
        psp2dc_widget_set_u32(W_FRAMES, frame);
        psp2dc_widget_set_u32(W_TRIGGER, cs ? (uint32_t)cs->rtrig : 0);
        psp2dc_widget_set_u32(W_HELD, cs && (cs->buttons & CONT_A));
        psp2dc_widget_set_u32(W_LIST, (uint32_t)marked);
    }
    return 0;
}
