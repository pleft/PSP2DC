/*
 * The VMU on its own: what the PSP shows and does while no Dreamcast is
 * there, as a VMU does when it is taken out of the controller (Sega VMU
 * manual, Specifications VMU-7, "Mode Settings"). MODE goes round GAME,
 * TIME and FILE, lighting that mode's icon; SLEEP blanks the screen.
 */
#ifndef PSP2DC_VMU_SA_H
#define PSP2DC_VMU_SA_H

#include "../../protocol/psp2dc_protocol.h"

/* the VMU's buttons, as the PSP's are mapped to them */
#define SA_UP    0x01
#define SA_DOWN  0x02
#define SA_LEFT  0x04
#define SA_RIGHT 0x08
#define SA_A     0x10
#define SA_B     0x20
#define SA_MODE  0x40
#define SA_SLEEP 0x80

/* the icons under the dots, lit: the bits as ui_page_home takes them */
#define SA_ICON_FILE      0x01
#define SA_ICON_GAME      0x02
#define SA_ICON_CLOCK     0x04
#define SA_ICON_ATTENTION 0x08

void sa_reset(void);                       /* back to TIME, as on leaving the Dreamcast (VMU-7) */
/* One frame: the buttons pressed since the last call (edges) and held, the
 * memory card (or NULL), the ms since the last call. In GAME mode A starts
 * the card's game (vmu_game.c); holding MODE 2 s leaves it. Returns 1 when
 * the game changed the card (written back into card). */
int sa_input(unsigned pressed, unsigned held, unsigned char *card, unsigned ms);
/* the screen as the Dreamcast would send it (for ui_lcd) and the lit icons */
unsigned sa_render(unsigned char lcd[PSP2DC_LCD_BYTES], const unsigned char *card);
const char *sa_mode_name(void);

#endif
