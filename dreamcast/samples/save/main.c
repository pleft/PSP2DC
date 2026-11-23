/*
 * PSP2DC sample: save. A counter kept in a file on the PSP's Memory Stick
 * (ms0:/PSP2DC/games/psp2dcsave/counter.sav): read when the PSP is there,
 * A adds one and writes it, B deletes the file. One file operation at a
 * time, started and then asked after each frame; nothing waits.
 */
#include <kos.h>
#include <stdio.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define FILE_NAME "counter.sav"

static void line(int row, const char *text)
{
    char s[41];
    snprintf(s, sizeof(s), "%-40s", text);
    bfont_draw_str(vram_s + (40 + row * 24) * 640 + 40, 640, 1, s);
}
static const char *status_text(int st)
{
    switch (st) {
    case PSP2DC_FILE_DONE:      return "done";
    case PSP2DC_FILE_BUSY:      return "busy";
    case PSP2DC_FILE_NOT_FOUND: return "no file yet";
    case PSP2DC_FILE_NO_ROOM:   return "no room";
    case PSP2DC_FILE_GONE:      return "PSP gone: again";
    case PSP2DC_FILE_FAILED:    return "Memory Stick failed";
    default:                    return "-";
    }
}

int main(int argc, char **argv)
{
    enum { NOTHING, READING, WRITING, DELETING } doing = NOTHING;
    uint32_t counter = 0, loaded = 0, saving = 0, n;   /* (a buffer handed over must stay until the end) */
    int known = 0;                               /* the file has been read (or found missing) */
    unsigned last = 0;
    char s[64];
    (void)argc; (void)argv;

    line(0, "PSP2DC save: A +1 and save, B delete");
    psp2dc_init();
    psp2dc_files("psp2dcsave");                  /* this program's folder on the Memory Stick */

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0, pressed = b & ~last;
        int st;
        last = b;
        vid_waitvbl();
        psp2dc_update();

        /* the operation going: how it ended */
        st = psp2dc_file_status(&n);
        if (doing != NOTHING && st != PSP2DC_FILE_BUSY) {
            if (doing == READING && st == PSP2DC_FILE_DONE && n == sizeof(loaded)) { counter = loaded; known = 1; }
            if (doing == READING && st == PSP2DC_FILE_NOT_FOUND) { counter = 0; known = 1; }
            if (doing == WRITING && st == PSP2DC_FILE_DONE) counter = saving;
            if (doing == DELETING && (st == PSP2DC_FILE_DONE || st == PSP2DC_FILE_NOT_FOUND)) counter = 0;
            snprintf(s, sizeof(s), "last: %s %s", doing == READING ? "read" : doing == WRITING ? "write" : "delete",
                     status_text(st));
            line(3, s);
            doing = NOTHING;
        }

        /* the next one. A start gives -1 while it cannot yet (the PSP has just come, or another
         * operation is going): it is simply tried again next frame. */
        if (doing == NOTHING) {
            if (!known) {
                if (psp2dc_file_read(FILE_NAME, 0, &loaded, sizeof(loaded)) == 0) doing = READING;
            } else if (pressed & CONT_A) {
                saving = counter + 1;                /* the counter changes once the file holds it */
                if (psp2dc_file_write(FILE_NAME, &saving, sizeof(saving)) == 0) doing = WRITING;
            } else if (pressed & CONT_B) {
                if (psp2dc_file_delete(FILE_NAME) == 0) doing = DELETING;
            }
        }
        if (!psp2dc_psp()) known = 0;            /* read again when a PSP comes */

        line(1, psp2dc_psp() ? "PSP: there" : "PSP: not there");
        snprintf(s, sizeof(s), "counter: %u%s", (unsigned)counter, known ? "" : " (not read yet)");
        line(2, s);
    }
    return 0;
}
