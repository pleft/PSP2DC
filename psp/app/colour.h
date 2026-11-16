/*
 * Colour pictures for the VMU's screen. Each 48x32 screen a game (or the
 * VMU on its own) shows is 192 bytes; its CRC-32 names it, so a picture made
 * for it can be shown in its place: ms0:/PSP2DC/colour/XXXXXXXX.png, 48x32
 * (drawn four times its size) or 192x128 (drawn as it is), as the screen is
 * seen (upright). Nothing on the Dreamcast changes.
 *
 * To make them: "Collect VMU screens" (System, Details) writes each new
 * screen as colour/new/XXXXXXXX.png, black on white, as seen; colour it and
 * move it up into colour/.
 */
#ifndef PSP2DC_COLOUR_H
#define PSP2DC_COLOUR_H

#include "../../protocol/psp2dc_protocol.h"

#define COLOUR_DIR     "ms0:/PSP2DC/colour"
#define COLOUR_NEW_DIR COLOUR_DIR "/new"
#define COLOUR_W 192
#define COLOUR_H 128

/* The folder read once (names only) and the loader thread started. */
void colour_init(void);
/* The picture for this screen, 192x128 pixels as the frame buffer takes
 * them (0xAABBGGRR), or NULL: none made, or not loaded yet (it is then
 * being loaded, without holding up the caller). */
const unsigned *colour_for(const unsigned char lcd[PSP2DC_LCD_BYTES]);
/* how many pictures colour/ holds */
int colour_count(void);

/* Collecting: each screen not yet in colour/ or colour/new/ written there. */
void colour_collect_set(int on);
int  colour_collect_on(void);
void colour_collect(const unsigned char lcd[PSP2DC_LCD_BYTES]);
unsigned colour_collected(void);           /* written since collecting began */

#endif
