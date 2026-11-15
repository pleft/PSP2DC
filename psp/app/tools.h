/*
 * Tools around the VMU's screen that need no adapter: recording the screens a
 * game draws, playing recordings back (demo mode), and screenshots of the
 * PSP's own screen. All files live in CARD_DIR on the Memory Stick.
 */
#ifndef PSP2DC_TOOLS_H
#define PSP2DC_TOOLS_H

#include "../../protocol/psp2dc_protocol.h"

#define TOOLS_DIR      "ms0:/PSP2DC"
#define TOOLS_REC_DIR  TOOLS_DIR "/capture"
#define TOOLS_SHOT_DIR TOOLS_DIR "/shots"

/* Recording: each screen the adapter sends while on, with its time. */
void rec_start(void);
void rec_stop(void);                       /* writes capture/vmu-NNN.rec */
int  rec_on(void);
void rec_frame(const unsigned char *lcd);  /* reader thread: one screen */
unsigned rec_count(void);
const char *rec_note(void);                /* the last thing that happened, for the status page */

/* Demo: the recordings in capture/ in turn, or a made-up screen with none. */
void demo_start(void);
void demo_stop(void);
int  demo_on(void);
/* The screen to show now; returns 1 when it changed since the last call. */
int  demo_frame(unsigned char out[PSP2DC_LCD_BYTES]);
const char *demo_note(void);

/* A screenshot of what the PSP shows, as shots/shot-NNN.bmp. 0 or < 0. */
int  shot_take(void);
const char *shot_note(void);

#endif
