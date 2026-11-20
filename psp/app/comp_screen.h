/*
 * The companion's screen drawn (companion.h): the regions, the widgets over
 * them in the look of the PSP's theme, a NOTIFY's text over all. Into any
 * 480 x 272 picture of ABGR pixels, so the PSP (ui.c) and the simulator
 * (scripts/dev/companion-sim) draw the same.
 */
#ifndef PSP2DC_COMP_SCREEN_H
#define PSP2DC_COMP_SCREEN_H

#include "companion.h"

typedef struct {
    unsigned bg, panel;        /* the screen, a widget's panel */
    unsigned text, muted;      /* text, and text that matters less (a caption) */
    unsigned strong;           /* a value */
    unsigned faint;            /* a bar's empty part, a light that is off */
    unsigned accent, on_accent;/* a bar's full part, the list's chosen line and the text on it */
    unsigned ok;               /* a light that is on */
} CompColours;

/* fb: 272 lines of `stride` pixels, already filled with the background. */
void comp_screen_draw(const Comp *c, unsigned *fb, int stride, const CompColours *col, unsigned now_us);

#endif
