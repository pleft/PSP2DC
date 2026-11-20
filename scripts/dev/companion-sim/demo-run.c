/*
 * dreamcast/demo run on the computer against the simulator
 * (scripts/dev/companion-demo.sh): its main loop as it is, a controller that
 * walks a fixed way through the maze, and at the end the PSP's screen as
 * pictures and the records the PSP got as a file.
 *
 *   demo-run <folder>    writes demo-1.bmp (a key just found), demo-2.bmp
 *                        (later, the PSP's L pressed) and demo.bin
 */
#include <stdio.h>
#include "kos_mock.h"

static uint16_t tv[640 * 480];
uint16_t *vram_s = tv;
void bfont_draw_str(void *buffer, int width, int opaque, const char *str) { (void)buffer; (void)width; (void)opaque; (void)str; }

/* to the first three keys, the third in the lava, and a step on into the lava */
static const char walk[] = "DDRRDDLLDDDDRDDRRRDDLLRRUULLLUULUURRRDDRRRRRDD";
#define START 60                           /* frames before the first step */
#define STEP  8                            /* frames from one step to the next */
static const char *folder;
static FILE *stream;

static void save(const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    if (sim_psp_screen_save(path)) { perror(path); exit(1); }
}

void vid_waitvbl(void)
{
    static unsigned frame;
    unsigned end = START + STEP * (sizeof(walk) - 1);
    vblank();
    pad_state.buttons = 0;
    if (frame >= START && frame < end && (frame - START) % STEP == 0) {
        char c = walk[(frame - START) / STEP];
        pad_state.buttons = c == 'U' ? CONT_DPAD_UP : c == 'D' ? CONT_DPAD_DOWN : c == 'L' ? CONT_DPAD_LEFT : CONT_DPAD_RIGHT;
    }
    if (frame == end + 30) save("demo-1.bmp");
    sim_psp_buttons(frame >= end + 200 && frame < end + 210 ? 0x100 : 0);
    if (frame == end + 300) {
        save("demo-2.bmp");
        fclose(stream);
        sim_psp_print(stdout);
        exit(0);
    }
    frame++;
}

int demo_main(int argc, char **argv);
int main(int argc, char **argv)
{
    char path[1024];
    if (argc < 2) { fprintf(stderr, "demo-run <folder>\n"); return 2; }
    folder = argv[1];
    snprintf(path, sizeof(path), "%s/demo.bin", folder);
    if (!(stream = fopen(path, "wb"))) { perror(path); return 1; }
    sim_psp_plug(1);
    sim_psp_log(stream);
    return demo_main(0, NULL);
}
