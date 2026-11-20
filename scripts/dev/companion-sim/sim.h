/*
 * The adapter and a PSP on the computer, for trying the companion's
 * Dreamcast side without either. The adapter's part is the firmware's own
 * vmu.c and the PSP's is psp/app/companion.c; what joins them (a USB
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

/* One exchange between the adapter and the PSP: about 60 a second. */
void sim_usb(void);
void sim_usb_fail(void);              /* the next transfer to the PSP is lost */

/* The PSP: on the adapter or not, its buttons, its companion page. */
void sim_psp_plug(int in);
void sim_psp_buttons(unsigned buttons);
const void *sim_psp_page(void);       /* a Comp (psp/app/companion.h) */
void sim_psp_print(FILE *f);
void sim_adapter_stats(uint32_t out[4]);   /* commands, "again" answers, bytes in, bytes out */

/* Behind an emulator's memory card: a command in, the reply out (its code
 * as sim_maple's). The PSP is plugged in at the first one and an exchange
 * runs after each. The environment sets the rest:
 *   PSP2DC_SIM_PAGE=file    the PSP's page, written there once a second
 *   PSP2DC_SIM_SLOW=n       an exchange every n commands only (the PSP reads slowly)
 *   PSP2DC_SIM_REPLUG=n     the PSP unplugged and back every n seconds
 *   PSP2DC_SIM_NO_PSP=1     no PSP on the adapter */
int  sim_emulator(const uint8_t *in, int in_bytes, uint8_t *out, int *out_bytes);

#endif
