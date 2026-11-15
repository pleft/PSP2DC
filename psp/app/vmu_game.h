/*
 * The VMU's GAME mode: the card's executable run on an emulated VMU
 * (vmu_core.c, derived from libevmu, the ElysianVMU core, MIT). Its software
 * BIOS stands in for Sega's: no BIOS image is needed.
 */
#ifndef PSP2DC_VMU_GAME_H
#define PSP2DC_VMU_GAME_H

#include "../../protocol/psp2dc_protocol.h"

int  game_start(const unsigned char *card);      /* 0, or < 0 when it could not start */
int  game_running(void);
int  game_ended(void);                           /* the game handed back to the BIOS (MODE, mostly) */
/* one step: the VMU's buttons held (vmu_sa.h's SA_*), ms of VMU time */
void game_frame(unsigned held, unsigned ms);
/* the screen as the Dreamcast would send it, and the lit icons */
unsigned game_lcd(unsigned char lcd[PSP2DC_LCD_BYTES]);
/* stop; the card as the game left it into card. 1 when the game changed it */
int  game_stop(unsigned char *card);
/* the buzzer: 0 when silent, else its tone in Hz and its duty in 1/256 */
int  game_tone(unsigned *hz, unsigned *duty);
unsigned game_frame_us(void);                    /* the last step's time, for the status page */
void game_audio_start(void);                     /* the buzzer through the PSP's speaker (a thread) */

#endif
