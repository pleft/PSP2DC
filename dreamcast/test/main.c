/*
 * PSP2DC companion test (docs/psp2dc-companion.md, Appendix B): finds the
 * companion, prints on the PSP, shows the PSP's buttons, and measures what
 * the path carries: TEST records of several sizes, one a frame, five seconds
 * a size, and the round trip of a small ECHO. The controller's state is
 * shown throughout, and Y draws the VMU's screen every frame as a game
 * might, to see the three together.
 */
#include <kos.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "psp2dc.h"
#include "psp2dc_protocol.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define COLS 50
static void line(int row, const char *fmt, ...)
{
    char s[COLS + 1];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(s, sizeof(s), fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n > COLS) n = COLS;
    memset(s + n, ' ', COLS - n);          /* (what was there before is written over) */
    s[COLS] = 0;
    bfont_draw_str(vram_s + (16 + row * 24) * 640 + 20, 640, 1, s);
}

/* a TEST record's data; 0: polls only. A command is the record (3 bytes more) after 8 of
 * its own, in words: 497 makes 127 words, 501 makes 128, the first whose length byte, the
 * frame's first, begins with a 1. */
static const int sizes[] = { 0, 64, 256, 384, 497, 501, 640, 768, 896, 996 };
#define PHASES ((int)(sizeof(sizes) / sizeof(sizes[0])))
#define PHASE_FRAMES 300
#define QUEUE_EMPTY 4096   /* psp2dc_room() with nothing waiting */

int main(int argc, char **argv)
{
    static uint8_t test[1000], lcd[192];
    struct { int done; unsigned cmds, bytes, errors; } res[PHASES];
    psp2dc_stats_t st, st0;
    psp2dc_event_t ev;
    unsigned frame = 0, phase_frame = 0, number = 0, last_cont = 0;
    unsigned echo_sent = 0, echo_last = 0, echo_min = 0, echo_max = 0, echoes = 0;
    int phase = 0, running = 0, finished = 0, told = 0, best = -1, paused = 0, draw_vmu = 0, p, u, i;
    (void)argc; (void)argv;

    memset(res, 0, sizeof(res));
    line(0, "PSP2DC companion test");
    line(1, "looking for the companion...");
    psp2dc_init();
    psp2dc_stats(&st0);

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0, pressed = b & ~last_cont;
        last_cont = b;
        vid_waitvbl();
        frame++;
        psp2dc_update();

        if (pressed & CONT_X) paused = !paused;
        if (pressed & CONT_Y) draw_vmu = !draw_vmu;

        while (psp2dc_poll(&ev)) {
            if (ev.type == PSP2DC_REC_ECHO && ev.len == 4) {
                unsigned f = ev.data[0] | ev.data[1] << 8 | ev.data[2] << 16 | (unsigned)ev.data[3] << 24;
                echo_last = frame - f;
                if (!echoes || echo_last < echo_min) echo_min = echo_last;
                if (!echoes || echo_last > echo_max) echo_max = echo_last;
                echoes++;
            }
        }

        /* A size runs its frames whatever becomes of it: one the adapter cannot take is
         * counted (the library gives the companion up and finds it again), not waited for.
         * The next begins once the companion is back and nothing of the last is left. */
        if (!paused && !finished) {
            if (!running) {
                if (psp2dc_psp() && psp2dc_room() == QUEUE_EMPTY && psp2dc_clear() == 0) {
                    char s[64];
                    snprintf(s, sizeof(s), "PSP2DC companion test: %d-byte records", sizes[phase]);
                    psp2dc_print(0, s);
                    /* a command no longer than this size's record (or, for the small ones, a row of text) */
                    psp2dc_send_max(sizes[phase] < 61 ? 64 : sizes[phase] + 3);
                    running = 1;
                    phase_frame = 0;
                    number = 0;
                    psp2dc_stats(&st0);
                }
            } else if (phase_frame >= PHASE_FRAMES) {
                psp2dc_stats(&st);
                res[phase].done = 1;
                res[phase].cmds = (st.commands - st0.commands) * 60 / PHASE_FRAMES;
                res[phase].bytes = (st.bytes_out - st0.bytes_out) * 60 / PHASE_FRAMES;
                res[phase].errors = st.errors - st0.errors;
                running = 0;
                psp2dc_send_max(64);                  /* (what is left of it, and the next size's first words) */
                if (++phase == PHASES) { phase = PHASES - 1; finished = 1; }
            } else {
                phase_frame++;
                if (sizes[phase] && psp2dc_room() > 3000) {   /* a TEST record a frame, unless they pile up */
                    int n = sizes[phase];
                    test[0] = number; test[1] = number >> 8; test[2] = number >> 16; test[3] = number >> 24;
                    for (i = 4; i < n; i++) test[i] = (number + i - 4) & 0xFF;
                    if (psp2dc_record(PSP2DC_REC_TEST, test, n) == 0) number++;
                }
                if (frame % 60 == 0) {                        /* an ECHO a second */
                    uint8_t e[4] = { frame, frame >> 8, frame >> 16, frame >> 24 };
                    char s[64];
                    if (psp2dc_record(PSP2DC_REC_ECHO, e, 4) == 0) echo_sent++;
                    snprintf(s, sizeof(s), "round trip %u frames (min %u, max %u)", echo_last, echo_min, echo_max);
                    psp2dc_print(2, s);
                }
            }
        }
        /* the largest size that ran without an error, every smaller one too */
        for (best = -1, i = 0; i < PHASES && res[i].done && !res[i].errors && (!sizes[i] || res[i].bytes); i++) best = i;
        if (finished && !told && psp2dc_psp()) {
            char s[64];
            snprintf(s, sizeof(s), "done: records up to %d bytes without an error", best < 0 ? 0 : sizes[best]);
            if (psp2dc_print(4, s) == 0) told = 1;
        }

        if (draw_vmu) {                                       /* a bar moving down the VMU's screen */
            maple_device_t *vmu = maple_enum_type(0, MAPLE_FUNC_LCD);
            memset(lcd, 0, sizeof(lcd));
            memset(lcd + (frame % 32) * 6, 0xFF, 6);
            if (vmu) vmu_draw_lcd(vmu, lcd);
        }

        psp2dc_where(&p, &u);
        psp2dc_stats(&st);
        if (psp2dc_found()) line(1, "companion: port %c unit %d, adapter protocol %d", 'A' + p, u, psp2dc_version());
        else line(1, "companion: not found, looking...");
        if (!psp2dc_found()) line(2, " ");
        else if (psp2dc_psp()) line(2, "PSP: there, session %d%s", psp2dc_session(), paused ? "   (paused)" : "");
        else line(2, "PSP: not on the adapter");
        line(3, "  size  commands/s  bytes/s  errors");
        for (i = 0; i < PHASES; i++) {
            char mark = !finished && i == phase ? '>' : ' ';
            if (res[i].done) line(4 + i, "%c %4d  %10u  %7u  %6u", mark, sizes[i], res[i].cmds, res[i].bytes, res[i].errors);
            else line(4 + i, "%c %4d           -        -       -", mark, sizes[i]);
        }
        line(14, "round trip: %u frames (min %u, max %u; %u of %u)", echo_last, echo_min, echo_max, echoes, echo_sent);
        line(15, "totals: %u sent, %u out, %u in, %u errors", (unsigned)st.commands, (unsigned)st.bytes_out,
             (unsigned)st.bytes_in, (unsigned)st.errors);
        if (finished) line(16, "DONE. Without an error: up to %d bytes", best < 0 ? 0 : sizes[best]);
        else line(16, "running: size %d of %d%s", phase + 1, PHASES, running ? "" : " (waiting for the companion)");
        line(17, "X: pause   Y: VMU screen every frame (%s)", draw_vmu ? "on" : "off");
    }
    return 0;
}
