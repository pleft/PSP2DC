/*
 * The adapter and a PSP on the computer, for trying the companion's
 * Dreamcast side without either. The adapter's part is the firmware's own
 * vmu.c and the PSP's is psp/app/companion.c and comp_screen.c; what joins them (a USB
 * transfer's messages, what the PSP sends back) follows psp2dc_host.c and
 * psp/app/main.c by hand.
 *
 *   scripts/dev/companion-test.sh   libpsp2dc against it, with faults
 *   flycast.patch                   behind Flycast's memory card, for the
 *                                   test disc (scripts/dev/flycast-companion.sh)
 */
#ifndef PSP2DC_SIM_H
#define PSP2DC_SIM_H

#include <stdint.h>
#include <stdio.h>

/* A companion command as the adapter's Maple side gets it: the payload's
 * words as they are in the Dreamcast's memory, the function word first.
 * The reply's payload goes to out (255 words at most). The reply's code:
 * 8 a data transfer, 0xFC again, -1 none (not the companion's command). */
int  sim_maple(const uint32_t *payload, int words, uint32_t *out, int *out_words);

/* The memory card's BLOCK READ as the adapter answers it: the payload is the function word and
 * the location word, as sim_maple's. block (512 bytes, or NULL) is put on the card there first.
 * The reply's payload goes to out; the reply's code is returned. */
int  sim_card_read(const uint32_t *payload, const uint8_t *block, uint32_t *out, int *out_words);

/* One exchange between the adapter and the PSP: about 60 a second. */
void sim_usb(void);
void sim_usb_fail(void);              /* the next transfer to the PSP is lost */

/* The PSP: on the adapter or not, its buttons, its companion page. */
void sim_psp_plug(int in);
void sim_psp_buttons(unsigned buttons);
const void *sim_psp_page(void);       /* a Comp (psp/app/companion.h) */
void sim_psp_print(FILE *f);
void sim_adapter_stats(uint32_t out[4]);   /* commands, "again" answers, bytes in, bytes out */
/* The PSP's screen as it would draw the program's widgets and regions: 480 x 272
 * pixels (0xAABBGGRR), or saved as a BMP file (0, or -1). */
const unsigned *sim_psp_screen(void);
int  sim_psp_screen_save(const char *file);
/* The Dreamcast program's records as the PSP gets them, written to f from here on: fed to
 * the PSP application as ms0:/PSP2DC/dev_companion.bin, it shows what it would on a Dreamcast. */
void sim_psp_log(FILE *f);

/* The PSP's files (psp/app/comp_files.c): its Memory Stick is the folder given (made when it
 * is not there; at first $PSP2DC_SIM_FILES, or psp2dc-sim-files). Free space as given (< 0: not
 * known, the default); writes to it failing or not. The bytes the PSP sends the adapter in an
 * exchange, at most (1024 at first). */
void sim_files_root(const char *dir);
void sim_files_free(long long bytes);
void sim_files_fail(int on);
void sim_psp_up_budget(unsigned bytes);

/* Behind an emulator's memory card: a command in, the reply out (its code
 * as sim_maple's). The PSP is plugged in at the first one and an exchange
 * runs after each. The environment sets the rest:
 *   PSP2DC_SIM_PAGE=file    the PSP's page as text, written there once a second
 *   PSP2DC_SIM_SCREEN=file  the PSP's screen as a picture (BMP), ten times a second
 *   PSP2DC_SIM_SLOW=n       an exchange every n commands only (the PSP reads slowly)
 *   PSP2DC_SIM_REPLUG=n     the PSP unplugged and back every n seconds
 *   PSP2DC_SIM_NO_PSP=1     no PSP on the adapter
 *   PSP2DC_SIM_MAX_WORDS=n  commands longer than n words go unanswered (127: the receiver before its fix)
 *   PSP2DC_SIM_FILES=dir    the PSP's Memory Stick for files (sim_files_root)
 *   PSP2DC_SIM_UP=n         bytes the PSP sends the adapter in an exchange, at most (sim_psp_up_budget) */
int  sim_emulator(const uint8_t *in, int in_bytes, uint8_t *out, int *out_bytes);

#endif
