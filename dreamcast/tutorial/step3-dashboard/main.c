/*
 * PSP2DC tutorial, step 3: dashboard
 *
 * Widgets: things the PSP draws for you, in its own theme. You make each one
 * once and then only tell it its value; the library sends what changed.
 *
 * NEW IN THIS STEP
 *   psp2dc_widget_create()      a label, a number, a bar, a light, a list
 *   psp2dc_widget_set_u32()     its value
 *   psp2dc_widget_set_text()    its text
 *   psp2dc_widget_set_range()   a bar's full value
 *   psp2dc_widget_set_colour()  a colour of its own
 *   psp2dc_widget_remove()      gone
 *   psp2dc_notify()             a line over the screen for a moment
 *
 * YOU SEE (PSP)  a title, a frame counter, a bar that follows the right
 * trigger and turns red when nearly full, a light that is on while A is
 * held, and a list whose mark the d-pad moves.
 *
 * TRY  The right trigger, A, d-pad up and down, B (the light goes away and
 * comes back), START (a notice). Then unplug the PSP and plug it back.
 *
 * WHY WIDGETS  The link carries about 59 KB a second. A value that changes
 * is 8 bytes as a widget, against hundreds if you drew the bar yourself. And
 * the library remembers every widget: when the PSP comes back they are all
 * sent again without the program doing anything. That is why this step has
 * no psp2dc_session() in it, unlike steps 1 and 2.
 *
 * NEXT  Step 4 draws its own pictures, for what no widget shows.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

/* Widget ids are yours to choose, 0 to 63. They are drawn in the order of their ids, so a
 * higher id is on top where two overlap. */
enum { W_TITLE, W_FRAMES, W_TRIGGER, W_HELD, W_LIST, W_HINT };

#define LINES 4
static const char *list_text = "Sword\nShield\nBow\nPotion";   /* a list's lines, parted by '\n' */
static const char *names[LINES] = { "Sword", "Shield", "Bow", "Potion" };

int main(int argc, char **argv)
{
    unsigned frame = 0, last = 0;
    int marked = 0, light_there = 1;
    char s[64];
    (void)argc; (void)argv;

    bfont_draw_str(vram_s + 40 * 640 + 40, 640, 1, "PSP2DC tutorial 3: dashboard (see the PSP)");
    psp2dc_init();

    /* MAKING WIDGETS. (id, kind, flags, x, y, width, height, text), in pixels of the PSP's
     * 480 x 272 screen. This works with no PSP there: they go when one comes.
     *
     * What each kind shows:
     *   LABEL   its text; PSP2DC_WF_BIG twice the size, _CENTRE or _RIGHT within its width
     *   NUMBER  its text at the left, its value at the right
     *   BAR     its value of its range as a bar; 20 pixels or taller, the text and
     *           "value/range" above the bar
     *   LIGHT   a dot, lit when the value is not 0, and its text
     *   LIST    its lines, the one numbered by the value marked (-1: none)
     * PSP2DC_WF_PANEL puts a panel behind any of them. */
    psp2dc_widget_create(W_TITLE, PSP2DC_WIDGET_LABEL, PSP2DC_WF_BIG | PSP2DC_WF_CENTRE, 0, 12, 480, 24, "DASHBOARD");
    psp2dc_widget_create(W_FRAMES, PSP2DC_WIDGET_NUMBER, PSP2DC_WF_PANEL, 40, 48, 400, 24, "Frames");
    psp2dc_widget_create(W_TRIGGER, PSP2DC_WIDGET_BAR, PSP2DC_WF_PANEL, 40, 80, 400, 36, "Right trigger");
    psp2dc_widget_set_range(W_TRIGGER, 255);     /* a bar's range is 100 unless you say */
    psp2dc_widget_create(W_HELD, PSP2DC_WIDGET_LIGHT, 0, 40, 124, 400, 14, "A held");
    psp2dc_widget_create(W_LIST, PSP2DC_WIDGET_LIST, PSP2DC_WF_PANEL, 40, 146, 400, 80, list_text);
    psp2dc_widget_create(W_HINT, PSP2DC_WIDGET_LABEL, PSP2DC_WF_CENTRE, 0, 236, 480, 12, "");

    for (;;) {
        /* the Dreamcast's controller, read as any KallistiOS program reads it */
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0, pressed = b & ~last;
        int trigger = cs ? cs->rtrig : 0;
        last = b;

        vid_waitvbl();
        frame++;
        psp2dc_update();

        if (pressed & CONT_DPAD_UP) marked = (marked + LINES - 1) % LINES;
        if (pressed & CONT_DPAD_DOWN) marked = (marked + 1) % LINES;

        /* SETTING VALUES. Every one, every frame: it costs nothing unless the value is new,
         * and then only that value is sent. No need to keep track yourself. */
        psp2dc_widget_set_u32(W_FRAMES, frame);
        psp2dc_widget_set_u32(W_TRIGGER, (uint32_t)trigger);
        psp2dc_widget_set_u32(W_LIST, (uint32_t)marked);

        /* TEXT is set the same way, and likewise sent only when it differs. */
        snprintf(s, sizeof(s), "Marked: %s", names[marked]);
        psp2dc_widget_set_text(W_HINT, s);

        /* A COLOUR OF ITS OWN. Widgets take the PSP theme's colours; -1 hands the choice
         * back to the theme. */
        psp2dc_widget_set_colour(W_TRIGGER, trigger > 200 ? PSP2DC_RGB565(230, 60, 50) : -1);

        /* REMOVING AND MAKING AGAIN. A removed widget is gone from the PSP's screen;
         * setting its value then returns -1 and does nothing. */
        if (pressed & CONT_B) {
            light_there = !light_there;
            if (light_there) psp2dc_widget_create(W_HELD, PSP2DC_WIDGET_LIGHT, 0, 40, 124, 400, 14, "A held");
            else psp2dc_widget_remove(W_HELD);
        }
        psp2dc_widget_set_u32(W_HELD, (b & CONT_A) != 0);

        /* A NOTICE: a line over the PSP's screen for a couple of seconds. Unlike widgets
         * it is not remembered: it returns -2 when no PSP is there, and is then lost. */
        if (pressed & CONT_START) psp2dc_notify("START pressed on the Dreamcast");
    }
    return 0;
}
