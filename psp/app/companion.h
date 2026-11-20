/*
 * The companion (psp2dc_protocol.h; docs/psp2dc-companion.md, Appendix B):
 * what a Dreamcast program built for it sends through the adapter. Phase 1:
 * rows of text, test data that is checked and counted, and bytes to send
 * back.
 */
#ifndef PSP2DC_COMPANION_H
#define PSP2DC_COMPANION_H

#include "../../protocol/psp2dc_protocol.h"

#define COMP_ROWS 12
#define COMP_COLS 66
typedef struct {
    char rows[COMP_ROWS][COMP_COLS + 1];
    unsigned bytes, records;           /* of the stream, since the session began */
    unsigned test_records, test_bytes; /* TEST records, since the last CLEAR */
    unsigned test_bad;                 /* ... out of turn, or with a wrong byte */
    unsigned stream_bad;               /* records that made no sense (the rest of the session is dropped) */
    unsigned sessions;
    unsigned last_us;                  /* when bytes last came (0: never) */
} Comp;
extern Comp g_comp;

void comp_reset(void);                             /* a session begins: nothing half read */
void comp_feed(const unsigned char *p, int n);     /* the stream's next bytes (the reader thread) */
/* an ECHO's bytes to send back, as a record ready to go: its length, or 0 */
int  comp_echo_take(unsigned char out[PSP2DC_COMP_EVENT_MAX]);

#endif
