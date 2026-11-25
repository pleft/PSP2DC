/*
 * PSP2DC tutorial, step 2: buttons
 *
 * What comes back from the PSP: its buttons, and what it says it can do.
 *
 * NEW IN THIS STEP
 *   psp2dc_poll()        the next thing the PSP sent
 *   PSP2DC_REC_BUTTONS   ... its buttons, PSP2DC_PSP_* bits
 *   psp2dc_get_caps()    what the PSP's application can do
 *
 * YOU SEE
 *   television   the PSP's level and screen size, and which PSP buttons are held
 *   PSP          a row of text, and a second row that counts your presses of X
 *
 * TRY  Hold buttons on the PSP. Press X a few times.
 *
 * NOTE  The PSP's buttons also press the Dreamcast's controller, as always:
 * the PSP is the controller. Step 6 shows how a program takes some of them
 * for itself.
 *
 * NEXT  Step 3 puts widgets on the PSP's screen.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

static void line(int row, const char *text)
{
    char s[41];
    snprintf(s, sizeof(s), "%-40.40s", text);
    bfont_draw_str(vram_s + (40 + row * 24) * 640 + 40, 640, 1, s);
}

/* START + A + B + X + Y together: back to the Dreamcast's menu, as a commercial game does
 * (with GDEMU and openMenu: to choose the next disc). KallistiOS watches for the buttons. */
static void to_menu(uint8_t addr, uint32_t buttons) { (void)addr; (void)buttons; arch_menu(); }

int main(int argc, char **argv)
{
    psp2dc_event_t ev;
    psp2dc_caps_t caps;
    unsigned buttons = 0;         /* the PSP's buttons as last heard */
    int presses = 0, shown = -1;  /* presses of X, and the count the PSP's row shows */
    int printed = -1;
    char s[64];
    (void)argc; (void)argv;

    line(0, "PSP2DC tutorial 2: buttons");
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, to_menu);
    psp2dc_init();

    for (;;) {
        vid_waitvbl();
        psp2dc_update();

        /* EVENTS. Whatever the PSP sent since the last frame comes out of psp2dc_poll(),
         * one record a call, until it returns 0. Always empty it in a loop like this.
         *
         * The PSP sends BUTTONS when its buttons change, not every frame: 4 bytes, of
         * which the low two hold the PSP2DC_PSP_* bits. You get the state, so a press is
         * a bit that is set now and was not before. */
        while (psp2dc_poll(&ev)) {
            if (ev.type == PSP2DC_REC_BUTTONS && ev.len >= 4) {
                unsigned now = ev.data[0] | ev.data[1] << 8;
                if ((now & PSP2DC_PSP_CROSS) && !(buttons & PSP2DC_PSP_CROSS)) presses++;
                buttons = now;
            }
        }

        /* The session pattern of step 1. A new session also forgets the count's row, so
         * `shown` is reset and the row below goes again. */
        if (psp2dc_psp() && psp2dc_session() != printed &&
            psp2dc_clear() == 0 && psp2dc_print(0, "Press X on the PSP.") == 0) {
            printed = psp2dc_session();
            shown = -1;
        }
        /* A row that changes: sent when its value is not the one shown, and only then. */
        if (psp2dc_psp() && printed == psp2dc_session() && presses != shown) {
            snprintf(s, sizeof(s), "X pressed %d times", presses);
            if (psp2dc_print(2, s) == 0) shown = presses;
        }

        /* CAPABILITIES. A moment after each new session the PSP says what it can do:
         *   level 2  widgets and regions (steps 3 and 4)
         *   level 3  pictures (step 5)
         *   level 4  files (step 7)
         *   level 5  taking buttons (step 6)
         * It returns 0 until the answer has come, and for ever with a PSP application too
         * old to answer. You do not have to ask: a PSP ignores what it does not know. Ask
         * when you want to tell the player to update, or to lay things out for the size. */
        if (psp2dc_get_caps(&caps))
            snprintf(s, sizeof(s), "PSP: level %d, screen %dx%d", caps.level, caps.width, caps.height);
        else
            snprintf(s, sizeof(s), psp2dc_psp() ? "PSP: there, no answer yet" : "PSP: not there");
        line(1, s);

        snprintf(s, sizeof(s), "held: %s%s%s%s%s%s%s%s%s%s",
                 buttons & PSP2DC_PSP_UP ? "up " : "", buttons & PSP2DC_PSP_DOWN ? "down " : "",
                 buttons & PSP2DC_PSP_LEFT ? "left " : "", buttons & PSP2DC_PSP_RIGHT ? "right " : "",
                 buttons & PSP2DC_PSP_CROSS ? "X " : "", buttons & PSP2DC_PSP_CIRCLE ? "O " : "",
                 buttons & PSP2DC_PSP_SQUARE ? "[] " : "", buttons & PSP2DC_PSP_TRIANGLE ? "/\\ " : "",
                 buttons & PSP2DC_PSP_L ? "L " : "", buttons & PSP2DC_PSP_R ? "R " : "");
        line(2, s);
        snprintf(s, sizeof(s), "X pressed %d times", presses);
        line(3, s);
    }
    return 0;
}
