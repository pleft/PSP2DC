/*
 * libpsp2dc: the PSP2DC companion, from a KallistiOS program
 * (docs/psp2dc-companion.md; the wire format is protocol/psp2dc_protocol.h).
 *
 * Phase 1: find the companion, send it records (text rows, test data, bytes
 * to send back), and take what the PSP sends (its buttons, the bytes back).
 *
 * Call psp2dc_update() once a frame. It sends one Maple command per vblank
 * at most: what was queued since the last one, or an empty poll, and every
 * reply brings what the PSP sent meanwhile. Nothing here waits, except
 * psp2dc_init().
 */
#ifndef PSP2DC_H
#define PSP2DC_H

#include <stdint.h>

#define PSP2DC_RECORD_MAX 1000   /* a record's data, at most */

/* Looks for the companion on every memory card unit, waiting up to half a
 * second for each. 0 when found, -1 when there is none: psp2dc_update()
 * goes on looking, a unit a second, without waiting. */
int  psp2dc_init(void);
/* Once a frame. */
void psp2dc_update(void);

/* What the last reply said. */
int  psp2dc_found(void);          /* an adapter that answers the companion's command */
int  psp2dc_psp(void);            /* ... with a PSP on it */
int  psp2dc_version(void);        /* the adapter's protocol version */
int  psp2dc_session(void);        /* changes when the PSP starts over, or records queued here were
                                   * dropped (the companion went away): send the screen again */
void psp2dc_where(int *port, int *unit);

/* A record for the PSP, queued for the next command. 0, or -1 when it does
 * not fit yet (nothing queued: try again next frame), -2 without a PSP. */
int  psp2dc_record(int type, const void *data, int len);
int  psp2dc_clear(void);
int  psp2dc_print(int row, const char *text);
/* How much of the queue is free; whether everything queued has been sent. */
int  psp2dc_room(void);
int  psp2dc_idle(void);

/* The next record from the PSP: its type and data (len bytes, at most 56).
 * 1 when there was one, 0 when not. */
typedef struct {
    int type;                     /* PSP2DC_REC_BUTTONS, PSP2DC_REC_ECHO */
    int len;
    uint8_t data[56];
} psp2dc_event_t;
int  psp2dc_poll(psp2dc_event_t *ev);

/* Counts, for measuring: commands answered, bytes of records sent, bytes
 * received, commands that got no answer or a wrong one. */
typedef struct {
    uint32_t commands, bytes_out, bytes_in, errors;
} psp2dc_stats_t;
void psp2dc_stats(psp2dc_stats_t *out);

#endif
