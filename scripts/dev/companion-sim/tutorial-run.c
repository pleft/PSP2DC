/*
 * A step of dreamcast/tutorial run on the computer against the simulator
 * (scripts/dev/companion-tutorial.sh): its main loop as it is, with what is
 * pressed and when given as arguments, and at the end what the television
 * and the PSP show.
 *
 *   tutorial-run [screen.bmp] <event>...
 *     F:NAME or F-G:NAME   held from frame F (to G, not included; else one frame)
 *        the Dreamcast's   A B X Y START UP DOWN LEFT RIGHT   RTRIG (fully)
 *        the PSP's         pUP pDOWN pLEFT pRIGHT pX pO pTRI pL pR
 *        F-G:unplug        the PSP off the adapter from F, back at G
 *     end=N                the last frame (400)
 * A name ending in .bmp: the PSP's screen is saved there at the end.
 */
#include <stdio.h>
#include "kos_mock.h"

static uint16_t tv[640 * 480];
uint16_t *vram_s = tv;
static char tv_text[20][64];
void bfont_draw_str(void *buffer, int width, int opaque, const char *str)
{
    long row = (((uint16_t *)buffer - tv) / 640 - 40) / 24;   /* (each step's rows: 24 pixels apart from 40) */
    (void)width; (void)opaque;
    if (row >= 0 && row < 20) snprintf(tv_text[row], sizeof(tv_text[row]), "%s", str);
}

static const struct { const char *name; unsigned dc, psp; } keys[] = {
    { "A", CONT_A, 0 }, { "B", CONT_B, 0 }, { "X", CONT_X, 0 }, { "Y", CONT_Y, 0 }, { "START", CONT_START, 0 },
    { "UP", CONT_DPAD_UP, 0 }, { "DOWN", CONT_DPAD_DOWN, 0 }, { "LEFT", CONT_DPAD_LEFT, 0 }, { "RIGHT", CONT_DPAD_RIGHT, 0 },
    { "pUP", 0, 0x10 }, { "pRIGHT", 0, 0x20 }, { "pDOWN", 0, 0x40 }, { "pLEFT", 0, 0x80 }, { "pL", 0, 0x100 }, { "pR", 0, 0x200 },
    { "pTRI", 0, 0x1000 }, { "pO", 0, 0x2000 }, { "pX", 0, 0x4000 },
};
static struct { unsigned from, to, dc, psp; int rtrig, unplug; } ev[64];
static int events;
static unsigned last_frame = 400;
static const char *screen;

void vid_waitvbl(void)
{
    static unsigned frame;
    unsigned dc = 0, psp = 0;
    int rtrig = 0, i;
    vblank();
    for (i = 0; i < events; i++) {
        if (ev[i].unplug) {
            if (frame == ev[i].from) sim_psp_plug(0);
            if (frame == ev[i].to) sim_psp_plug(1);
        } else if (frame >= ev[i].from && frame < ev[i].to) {
            dc |= ev[i].dc; psp |= ev[i].psp;
            if (ev[i].rtrig) rtrig = 255;
        }
    }
    pad_state.buttons = dc;
    pad_state.rtrig = rtrig;
    sim_psp_buttons(psp);
    if (++frame >= last_frame) {
        printf("television:\n");
        for (i = 0; i < 20; i++) if (tv_text[i][0]) printf("  %s\n", tv_text[i]);
        sim_psp_print(stdout);
        if (screen && sim_psp_screen_save(screen)) { perror(screen); exit(1); }
        exit(0);
    }
}

int step_main(int argc, char **argv);
int main(int argc, char **argv)
{
    int a;
    unsigned k;
    for (a = 1; a < argc; a++) {
        unsigned from, to;
        char name[32];
        size_t n = strlen(argv[a]);
        if (n > 4 && !strcmp(argv[a] + n - 4, ".bmp")) { screen = argv[a]; continue; }
        if (sscanf(argv[a], "end=%u", &last_frame) == 1) continue;
        if (sscanf(argv[a], "%u-%u:%31s", &from, &to, name) != 3) {
            if (sscanf(argv[a], "%u:%31s", &from, name) != 2) { fprintf(stderr, "what is \"%s\"?\n", argv[a]); return 2; }
            to = from + 1;
        }
        ev[events].from = from; ev[events].to = to;
        if (!strcmp(name, "unplug")) ev[events].unplug = 1;
        else if (!strcmp(name, "RTRIG")) ev[events].rtrig = 1;
        else {
            for (k = 0; k < sizeof(keys) / sizeof(keys[0]) && strcmp(keys[k].name, name); k++) {}
            if (k == sizeof(keys) / sizeof(keys[0])) { fprintf(stderr, "no button \"%s\"\n", name); return 2; }
            ev[events].dc = keys[k].dc; ev[events].psp = keys[k].psp;
        }
        if (++events == 64) break;
    }
    sim_psp_plug(1);
    return step_main(0, NULL);
}
