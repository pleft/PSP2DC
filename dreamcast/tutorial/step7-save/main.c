/*
 * PSP2DC tutorial, step 7: save
 *
 * Files of the program's own on the PSP's Memory Stick: far more room than a
 * VMU, and plain files a player can copy off with a computer.
 *
 * NEW IN THIS STEP
 *   psp2dc_files()         the program's folder
 *   psp2dc_file_write()    a whole file written (or replaced)
 *   psp2dc_file_read()     a file, or any part of one, read
 *   psp2dc_file_delete()   a file deleted
 *   psp2dc_file_list()     the folder's files, as text
 *   psp2dc_files_free()    how much the folder may still take
 *   psp2dc_file_status()   how the operation going has ended
 *   (psp2dc_file_size() works like the others: the size comes back as n)
 *
 * YOU SEE (television)  a counter that is kept in the file
 * ms0:/PSP2DC/games/tutorial7/counter.sav, and what the last operation did.
 *
 * TRY  A adds one and saves. B deletes the file. Y lists the folder and says
 * how much room is left. Start the disc again: the counter is as you left it.
 *
 * HOW FILE OPERATIONS WORK  Nothing waits. You START an operation, and it
 * goes on inside psp2dc_update() over the next frames. Each frame you ask
 * psp2dc_file_status() how it stands:
 *
 *     PSP2DC_FILE_BUSY        still going
 *     PSP2DC_FILE_DONE        finished; n is its result (bytes, a size)
 *     PSP2DC_FILE_NOT_FOUND   no such file
 *     PSP2DC_FILE_NO_ROOM     the folder's limit (16 MB) or the Memory Stick is full
 *     PSP2DC_FILE_GONE        the PSP went away in the middle: start it again
 *     PSP2DC_FILE_FAILED      the Memory Stick failed
 *
 * One operation at a time. A start returns 0 when it began, -1 for "not now"
 * (one is going, or the PSP has only just come: try again next frame) and -2
 * for "cannot be" (no PSP, a PSP application without files, a bad name).
 *
 * TWO RULES
 * 1. A buffer you hand over (to write from, to read into) is used over many
 *    frames. It must stay where it is until the operation ends: not a local
 *    variable of a function that returns.
 * 2. A write replaces the file only once ALL of it has arrived and been
 *    checked. If the PSP is unplugged half way, the old file is still there,
 *    whole. So a save is never half written.
 *
 * NAMES  Up to 63 of a-z 0-9 . - _ and / (so "levels/map01.dat" is fine),
 * small letters only, no part starting with a dot. The folder's name: up to
 * 16 of a-z 0-9 - _.
 *
 * NEXT  dreamcast/demo uses everything from steps 1 to 6 in a small game.
 */
#include <kos.h>
#include <stdio.h>
#include <string.h>
#include "psp2dc.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define FILE_NAME "counter.sav"

static void line(int row, const char *text)
{
    char s[41];
    snprintf(s, sizeof(s), "%-40.40s", text);
    bfont_draw_str(vram_s + (40 + row * 24) * 640 + 40, 640, 1, s);
}
static const char *status_text(int st)
{
    switch (st) {
    case PSP2DC_FILE_DONE:      return "done";
    case PSP2DC_FILE_NOT_FOUND: return "no such file";
    case PSP2DC_FILE_NO_ROOM:   return "no room";
    case PSP2DC_FILE_GONE:      return "the PSP went away: try again";
    case PSP2DC_FILE_FAILED:    return "the Memory Stick failed";
    default:                    return "?";
    }
}

/* What the one operation going is, so that its end is understood. */
enum { NOTHING, READING, WRITING, DELETING, LISTING, ASKING_FREE };

/* Rule 1: these are handed to the library and must outlive the operation, so they are
 * static, not locals of a function that could return meanwhile. */
static uint32_t loaded, saving;
static char listing[256];

/* START + A + B + X + Y together: back to the Dreamcast's menu, as a commercial game does
 * (with GDEMU and openMenu: to choose the next disc). KallistiOS watches for the buttons. */
static void to_menu(uint8_t addr, uint32_t buttons) { (void)addr; (void)buttons; arch_menu(); }

int main(int argc, char **argv)
{
    int doing = NOTHING;
    uint32_t counter = 0, n;
    int known = 0;                 /* the file has been read (or found missing) */
    int want_free = 0;             /* after a listing: ask for the room left */
    unsigned last = 0;
    char s[64];
    (void)argc; (void)argv;

    line(0, "PSP2DC tutorial 7: save");
    line(1, "A: +1 and save   B: delete   Y: list");
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, to_menu);
    psp2dc_init();

    /* The program's folder on the Memory Stick: ms0:/PSP2DC/games/tutorial7/. Once, before
     * anything else with files. It is made when it is not there, and nothing outside it
     * can be named. */
    psp2dc_files("tutorial7");

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0, pressed = b & ~last;
        int st;
        last = b;

        vid_waitvbl();
        psp2dc_update();

        /* THE END OF AN OPERATION. Each frame: is the one we started still going? */
        st = psp2dc_file_status(&n);
        if (doing != NOTHING && st != PSP2DC_FILE_BUSY) {
            switch (doing) {
            case READING:
                /* n is how many bytes were read: fewer than asked when the file is shorter */
                if (st == PSP2DC_FILE_DONE && n == sizeof(loaded)) { counter = loaded; known = 1; }
                if (st == PSP2DC_FILE_NOT_FOUND) { counter = 0; known = 1; }   /* a first run: no save yet */
                snprintf(s, sizeof(s), "read: %s", status_text(st));
                break;
            case WRITING:
                /* only now is the new value on the Memory Stick: only now is it ours */
                if (st == PSP2DC_FILE_DONE) counter = saving;
                snprintf(s, sizeof(s), "write: %s", status_text(st));
                break;
            case DELETING:
                if (st == PSP2DC_FILE_DONE || st == PSP2DC_FILE_NOT_FOUND) counter = 0;
                snprintf(s, sizeof(s), "delete: %s", status_text(st));
                break;
            case LISTING:
                /* n bytes of text, a line a file: "name size\n". Not ended by a 0, so we
                 * end it ourselves (the buffer was given one byte short for that). */
                if (st == PSP2DC_FILE_DONE) {
                    char *nl;
                    listing[n] = 0;
                    if ((nl = strchr(listing, '\n'))) *nl = 0;      /* the first file is enough here */
                    snprintf(s, sizeof(s), "first file: %.40s", n ? listing : "(none)");
                    want_free = 1;
                } else snprintf(s, sizeof(s), "list: %s", status_text(st));
                break;
            default:               /* ASKING_FREE: n is the bytes the folder may still take */
                if (st == PSP2DC_FILE_DONE) snprintf(s, sizeof(s), "room left: %u KB", (unsigned)(n / 1024));
                else snprintf(s, sizeof(s), "room: %s", status_text(st));
                line(5, s);
                s[0] = 0;
                break;
            }
            if (s[0]) line(4, s);
            doing = NOTHING;
        }

        /* STARTING THE NEXT ONE. Only when none is going. Each start is tried and, when it
         * returns 0, remembered in `doing`; when it returns -1 ("not now") nothing is
         * remembered and the same wish simply comes up again next frame. */
        if (doing == NOTHING) {
            if (!known) {
                /* (name, where in the file to start, where to put it, how much) */
                if (psp2dc_file_read(FILE_NAME, 0, &loaded, sizeof(loaded)) == 0) doing = READING;
            } else if (want_free) {
                if (psp2dc_files_free() == 0) { doing = ASKING_FREE; want_free = 0; }
            } else if (pressed & CONT_A) {
                saving = counter + 1;
                /* (name, the bytes, how many): the whole file, replacing what was there */
                if (psp2dc_file_write(FILE_NAME, &saving, sizeof(saving)) == 0) doing = WRITING;
            } else if (pressed & CONT_B) {
                if (psp2dc_file_delete(FILE_NAME) == 0) doing = DELETING;
            } else if (pressed & CONT_Y) {
                if (psp2dc_file_list(listing, sizeof(listing) - 1) == 0) doing = LISTING;
            }
        }

        /* No PSP: what we know of the file is from before. Read it again when one comes
         * (another PSP, another Memory Stick, may hold another save). */
        if (!psp2dc_psp()) known = 0;

        line(2, psp2dc_psp() ? "PSP: there" : "PSP: not there");
        snprintf(s, sizeof(s), "counter: %u%s", (unsigned)counter, known ? "" : " (not read yet)");
        line(3, s);
    }
    return 0;
}
