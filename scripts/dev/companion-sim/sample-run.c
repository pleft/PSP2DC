/*
 * A sample program of dreamcast/samples run on the computer against the
 * simulator (scripts/dev/companion-samples.sh): its main loop as it is, the
 * same few presses for each, and at the end what the television and the PSP
 * show.
 *
 *   frames 100 to 110   the PSP's L held
 *   frames 100 to 300   the Dreamcast's right trigger at 200
 *   frames 150 and 220  the Dreamcast's A pressed (one frame each)
 *   frame 400           the end: the television's text, the PSP's page
 */
#include <stdio.h>
#include "kos_mock.h"

static uint16_t tv[640 * 480];
uint16_t *vram_s = tv;
static char tv_text[20][64];
void bfont_draw_str(void *buffer, int width, int opaque, const char *str)
{
    long row = ((uint16_t *)buffer - tv) / 640 / 24;   /* (each sample's rows are 24 pixels apart) */
    (void)width; (void)opaque;
    if (row >= 0 && row < 20) snprintf(tv_text[row], sizeof(tv_text[row]), "%s", str);
}

void vid_waitvbl(void)
{
    static unsigned frame;
    int i;
    vblank();
    pad_state.buttons = frame == 150 || frame == 220 ? CONT_A : 0;
    pad_state.rtrig = frame >= 100 && frame < 300 ? 200 : 0;
    sim_psp_buttons(frame >= 100 && frame < 110 ? 0x100 : 0);
    if (++frame == 400) {
        printf("television:\n");
        for (i = 0; i < 20; i++) if (tv_text[i][0]) printf("  %s\n", tv_text[i]);
        sim_psp_print(stdout);
        exit(0);
    }
}

int sample_main(int argc, char **argv);
int main(void)
{
    sim_psp_plug(1);
    return sample_main(0, NULL);
}
