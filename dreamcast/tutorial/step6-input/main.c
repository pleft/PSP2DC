/*
 * PSP2DC tutorial, step 6: input
 *
 * A menu on the PSP, worked with the PSP's own buttons, without those
 * buttons also pressing the game's controller.
 *
 * NEW IN THIS STEP
 *   psp2dc_focus()    the PSP's buttons the program takes for itself
 *
 * THE PROBLEM  The PSP is the Dreamcast's controller: its d-pad is the
 * controller's d-pad. In step 2 the program saw the PSP's buttons, but so
 * did the game. A menu on the PSP worked with the d-pad would also walk the
 * player around on the television.
 *
 * THE ANSWER  psp2dc_focus(buttons) names the buttons that reach the program
 * only (through psp2dc_poll, as in step 2) and no longer the controller.
 * psp2dc_focus(0) gives them all back. Take them only while the PSP's menu
 * is open, so the game has its whole controller the rest of the time.
 *
 * YOU SEE
 *   television   the Dreamcast's d-pad and A as the game would see them
 *   PSP          a list of items; "MENU OPEN" or "menu closed"
 *
 * TRY  With the menu closed, the PSP's d-pad shows on the television (it is
 * the controller). Press triangle on the PSP (Y to the Dreamcast): the menu
 * opens. Now the PSP's up, down and X work the list and the television shows
 * nothing pressed. Triangle again closes it.
 *
 * WHAT CANNOT BE TAKEN  SELECT, which opens PSP2DC's own menu, and the PSP's
 * system buttons. An older PSP application ignores psp2dc_focus(): the
 * buttons then go to both, as in step 2 (psp2dc_get_caps: level 5 has it).
 *
 * NEXT  Step 7 keeps files on the PSP's Memory Stick.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

static void line(int row, const char *text)
{
    char s[41];
    snprintf(s, sizeof(s), "%-40s", text);
    bfont_draw_str(vram_s + (40 + row * 24) * 640 + 40, 640, 1, s);
}

enum { W_STATE, W_LIST, W_PICKED };
#define ITEMS 4
static const char *items[ITEMS] = { "Potion", "Map", "Torch", "Rope" };

/* the buttons the menu works with: taken while it is open */
#define MENU_BUTTONS (PSP2DC_PSP_UP | PSP2DC_PSP_DOWN | PSP2DC_PSP_CROSS)

/* START + A + B + X + Y together: back to the Dreamcast's menu, as a commercial game does
 * (with GDEMU and openMenu: to choose the next disc). KallistiOS watches for the buttons. */
static void to_menu(uint8_t addr, uint32_t buttons) { (void)addr; (void)buttons; arch_menu(); }

int main(int argc, char **argv)
{
    psp2dc_event_t ev;
    unsigned psp_last = 0, last = 0;
    int open = 0, marked = 0;
    char s[64];
    (void)argc; (void)argv;

    line(0, "PSP2DC tutorial 6: input");
    line(1, "Y (triangle on the PSP): the menu");
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, to_menu);
    psp2dc_init();

    psp2dc_widget_create(W_STATE, PSP2DC_WIDGET_LABEL, PSP2DC_WF_BIG | PSP2DC_WF_CENTRE, 0, 16, 480, 24, "menu closed");
    psp2dc_widget_create(W_LIST, PSP2DC_WIDGET_LIST, PSP2DC_WF_PANEL, 120, 60, 240, 80, "Potion\nMap\nTorch\nRope");
    psp2dc_widget_create(W_PICKED, PSP2DC_WIDGET_LABEL, PSP2DC_WF_CENTRE, 0, 160, 480, 12, "");
    psp2dc_widget_set_u32(W_LIST, (uint32_t)-1);        /* no line marked while it is closed */

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0, pressed = b & ~last;
        last = b;

        vid_waitvbl();
        psp2dc_update();

        /* The menu opens and closes with a button the GAME sees (Y), so it works whatever
         * is taken. Opening takes the menu's buttons; closing gives them back.
         *
         * psp2dc_focus() is remembered like a widget: it is sent when it changes, and
         * again when the PSP starts over. It also ends by itself when the program stops
         * talking to the PSP (it quit, or the Dreamcast was switched off), so a PSP never
         * stays without its d-pad. */
        if (pressed & CONT_Y) {
            open = !open;
            psp2dc_focus(open ? MENU_BUTTONS : 0);
            psp2dc_widget_set_text(W_STATE, open ? "MENU OPEN" : "menu closed");
            psp2dc_widget_set_u32(W_LIST, open ? (uint32_t)marked : (uint32_t)-1);
        }

        /* The PSP's buttons, as in step 2. They come here whether taken or not: taking
         * only stops them from pressing the controller as well. */
        while (psp2dc_poll(&ev)) {
            unsigned now, hit;
            if (ev.type != PSP2DC_REC_BUTTONS || ev.len < 4) continue;
            now = ev.data[0] | ev.data[1] << 8;
            hit = now & ~psp_last;               /* pressed since the last we heard */
            psp_last = now;
            if (!open) continue;
            if (hit & PSP2DC_PSP_UP) marked = (marked + ITEMS - 1) % ITEMS;
            if (hit & PSP2DC_PSP_DOWN) marked = (marked + 1) % ITEMS;
            if (hit & PSP2DC_PSP_CROSS) {
                snprintf(s, sizeof(s), "Picked: %s", items[marked]);
                psp2dc_widget_set_text(W_PICKED, s);
            }
            psp2dc_widget_set_u32(W_LIST, (uint32_t)marked);
        }

        /* what the game sees of the controller: with the menu open, the PSP's up, down
         * and X no longer show here */
        snprintf(s, sizeof(s), "the game sees: %s%s%s%s%s", b & CONT_DPAD_UP ? "up " : "", b & CONT_DPAD_DOWN ? "down " : "",
                 b & CONT_DPAD_LEFT ? "left " : "", b & CONT_DPAD_RIGHT ? "right " : "", b & CONT_A ? "A " : "");
        line(3, s);
        line(4, open ? "menu: OPEN (PSP up, down, X are its own)" : "menu: closed");
    }
    return 0;
}
