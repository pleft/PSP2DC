/*
 * PSP2DC companion test (docs/psp2dc-companion.md, Appendix B): finds the
 * companion, prints on the PSP, shows the PSP's buttons, and measures what
 * the path carries: TEST records of several sizes, one a frame, five seconds
 * a size, and the round trip of a small ECHO. The controller's state is
 * shown throughout, and Y draws the VMU's screen every frame as a game
 * might, to see the three together. Then files (Appendix E): 64 KB written
 * to the PSP's Memory Stick, read back with each window and compared, listed
 * and deleted.
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

/* The files' part: its steps, one operation each, and what each gave. */
#define FILE_BYTES 65536
#define FILE_NAME  "test/speed.bin"
enum { F_WRITE, F_READ1, F_READ2, F_READ3, F_LIST, F_DELETE, F_GONE, F_STEPS };
static const char *step_name[F_STEPS] = { "write", "read 1K", "read 2K", "read 3K", "list", "delete", "gone" };
static struct { int done, status; unsigned frames, n, wrong; } fres[F_STEPS];
static uint8_t fbuf[FILE_BYTES], fback[FILE_BYTES];
static char flist[1024];
static uint8_t fbyte(unsigned i) { unsigned x = (i + 1) * 2654435761u; return (uint8_t)(x >> 13 ^ x >> 24); }
static int fstep = -1, fstarted;             /* -1: not begun; F_STEPS: all done */
static unsigned fstart;
static int files_step(unsigned frame)        /* once a frame: 1 when every step is done */
{
    int r;
    uint32_t n;
    if (fstep == F_STEPS) return 1;
    if (fstep < 0) {
        unsigned i;
        for (i = 0; i < FILE_BYTES; i++) fbuf[i] = fbyte(i);
        psp2dc_send_max(PSP2DC_COMP_DATA_MAX);
        fstep = 0;
    }
    if (!fstarted) {
        switch (fstep) {
        case F_WRITE:  r = psp2dc_file_write(FILE_NAME, fbuf, FILE_BYTES); break;
        case F_READ1: case F_READ2: case F_READ3:
            psp2dc_file_window((fstep - F_READ1 + 1) * 1024);
            memset(fback, 0, sizeof(fback));
            r = psp2dc_file_read(FILE_NAME, 0, fback, FILE_BYTES);
            break;
        case F_LIST:   memset(flist, 0, sizeof(flist)); r = psp2dc_file_list(flist, sizeof(flist) - 1); break;
        case F_DELETE: r = psp2dc_file_delete(FILE_NAME); break;
        default:       r = psp2dc_file_size(FILE_NAME); break;
        }
        if (r == -1) return 0;               /* (not now: next frame) */
        if (r < 0) {                         /* cannot be: a PSP application without files */
            fres[fstep].done = 1; fres[fstep].status = 100;
            fstep = F_STEPS;
            return 1;
        }
        fstarted = 1;
        fstart = frame;
        return 0;
    }
    r = psp2dc_file_status(&n);
    if (r == PSP2DC_FILE_BUSY) return 0;
    fres[fstep].done = 1;
    fres[fstep].status = r;
    fres[fstep].frames = frame - fstart;
    fres[fstep].n = n;
    if (fstep >= F_READ1 && fstep <= F_READ3 && r == PSP2DC_FILE_DONE) {
        unsigned i;
        for (i = 0; i < FILE_BYTES; i++) if (fback[i] != fbuf[i]) fres[fstep].wrong++;
    }
    if (fstep == F_LIST && r == PSP2DC_FILE_DONE && !strstr(flist, FILE_NAME " 65536\n")) fres[fstep].wrong = 1;
    fstarted = 0;
    fstep++;
    psp2dc_file_window(3 * 1024);
    return fstep == F_STEPS;
}
/* each step as it should have ended */
static int step_ok(int i)
{
    if (!fres[i].done) return 0;
    if (i == F_GONE) return fres[i].status == PSP2DC_FILE_NOT_FOUND;
    return fres[i].status == PSP2DC_FILE_DONE && !fres[i].wrong &&
           (i > F_READ3 || fres[i].n == FILE_BYTES);
}
static unsigned kbs(int i) { return fres[i].frames ? FILE_BYTES * 60u / 1024 / fres[i].frames : 0; }

int main(int argc, char **argv)
{
    static uint8_t test[1000], lcd[192];
    struct { int done; unsigned cmds, bytes, errors; } res[PHASES];
    psp2dc_stats_t st, st0;
    psp2dc_event_t ev;
    unsigned frame = 0, phase_frame = 0, number = 0, last_cont = 0;
    unsigned echo_sent = 0, echo_last = 0, echo_min = 0, echo_max = 0, echoes = 0;
    int phase = 0, running = 0, finished = 0, told = 0, best = -1, paused = 0, draw_vmu = 0, p, u, i;
    int files_done = 0, files_ok = 0;
    (void)argc; (void)argv;

    memset(res, 0, sizeof(res));
    line(0, "PSP2DC companion test");
    line(1, "looking for the companion...");
    psp2dc_init();
    psp2dc_files("psp2dctest");
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
        /* then the files, once every size is done */
        if (finished && !paused && !files_done && (fstep >= 0 || psp2dc_room() == QUEUE_EMPTY)) files_done = files_step(frame);
        for (files_ok = 1, i = 0; i < F_STEPS; i++) if (!step_ok(i)) files_ok = 0;

        /* the largest size that ran without an error, every smaller one too */
        for (best = -1, i = 0; i < PHASES && res[i].done && !res[i].errors && (!sizes[i] || res[i].bytes); i++) best = i;
        if (files_done && !told && psp2dc_psp()) {
            char s[80];
            snprintf(s, sizeof(s), "done: records up to %d bytes without an error; files %s", best < 0 ? 0 : sizes[best],
                     files_ok ? "ok" : "FAILED");
            if (psp2dc_print(4, s) == 0) told = 1;
            snprintf(s, sizeof(s), "files: write %u KB/s, read %u, %u, %u KB/s (1, 2, 3 KB windows)", kbs(F_WRITE),
                     kbs(F_READ1), kbs(F_READ2), kbs(F_READ3));
            psp2dc_print(5, s);
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
        /* the files: each step's speed or status as it ends */
        {
            char s[COLS + 1];
            int k = snprintf(s, sizeof(s), "files:");
            for (i = 0; i < F_STEPS && k < COLS; i++) {
                if (!fres[i].done) break;
                if (i <= F_READ3 && step_ok(i)) k += snprintf(s + k, sizeof(s) - k, " %u", kbs(i));
                else k += snprintf(s + k, sizeof(s) - k, " %s", step_ok(i) ? "ok" : "BAD");
            }
            if (fstep >= 0 && fstep < F_STEPS && k < COLS) snprintf(s + k, sizeof(s) - k, " (%s...)", step_name[fstep]);
            line(16, "%s", s);
        }
        if (files_done) line(17, "DONE. Up to %d bytes; files %s", best < 0 ? 0 : sizes[best],
                             files_ok ? "ok (KB/s: write, reads 1/2/3K)" : "FAILED");
        else if (finished) line(17, "sizes done (up to %d bytes); files running", best < 0 ? 0 : sizes[best]);
        else line(17, "running: size %d of %d%s", phase + 1, PHASES, running ? "" : " (waiting for the companion)");
        line(18, "X: pause   Y: VMU screen every frame (%s)", draw_vmu ? "on" : "off");
    }
    return 0;
}
