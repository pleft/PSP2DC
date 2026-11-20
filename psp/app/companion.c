/* See companion.h. */
#include <string.h>
#include <pspkernel.h>
#include "companion.h"

Comp g_comp;

static unsigned char rec[sizeof(Psp2dcCompRecord) + PSP2DC_COMP_DATA_MAX];
static int have, dead;                 /* dead: the stream made no sense, until the next session */
static unsigned test_next;
static unsigned char echo[PSP2DC_COMP_EVENT_MAX];
static volatile int echo_len;

void comp_reset(void)
{
    have = dead = 0;
    test_next = 0;
    echo_len = 0;
    g_comp.bytes = g_comp.records = 0;
    g_comp.test_records = g_comp.test_bytes = g_comp.test_bad = 0;
    g_comp.sessions++;
}

static void record(int type, const unsigned char *d, int len)
{
    g_comp.records++;
    if (type == PSP2DC_REC_CLEAR) {
        memset(g_comp.rows, 0, sizeof(g_comp.rows));
        g_comp.test_records = g_comp.test_bytes = g_comp.test_bad = 0;
        test_next = 0;
    } else if (type == PSP2DC_REC_PRINT && len >= 1 && d[0] < COMP_ROWS) {
        int n = len - 1 < COMP_COLS ? len - 1 : COMP_COLS, i;
        char *row = g_comp.rows[d[0]];
        for (i = 0; i < n; i++) row[i] = d[1 + i] >= 0x20 && d[1 + i] <= 0x7E ? (char)d[1 + i] : '?';
        row[n] = 0;
    } else if (type == PSP2DC_REC_TEST && len >= 4) {
        unsigned number = d[0] | d[1] << 8 | d[2] << 16 | (unsigned)d[3] << 24;
        int i, bad = number != test_next;
        for (i = 4; i < len && !bad; i++) bad = d[i] != ((number + i - 4) & 0xFF);
        if (bad) g_comp.test_bad++;
        test_next = number + 1;
        g_comp.test_records++;
        g_comp.test_bytes += len;
    } else if (type == PSP2DC_REC_ECHO && len <= PSP2DC_COMP_EVENT_MAX - (int)sizeof(Psp2dcCompRecord)) {
        echo_len = 0;                  /* (one at a time: a second before the first went replaces it) */
        echo[0] = PSP2DC_REC_ECHO; echo[1] = (unsigned char)len; echo[2] = 0;
        memcpy(echo + sizeof(Psp2dcCompRecord), d, len);
        echo_len = (int)sizeof(Psp2dcCompRecord) + len;
    }
}

void comp_feed(const unsigned char *p, int n)
{
    g_comp.bytes += n;
    g_comp.last_us = sceKernelGetSystemTimeLow() | 1;
    while (n > 0 && !dead) {
        int want = have < (int)sizeof(Psp2dcCompRecord) ? (int)sizeof(Psp2dcCompRecord)
                                                        : (int)sizeof(Psp2dcCompRecord) + (rec[1] | rec[2] << 8);
        int take = want - have < n ? want - have : n;
        memcpy(rec + have, p, take);
        have += take; p += take; n -= take;
        if (have < (int)sizeof(Psp2dcCompRecord)) break;
        if ((rec[1] | rec[2] << 8) > PSP2DC_COMP_DATA_MAX) { dead = 1; g_comp.stream_bad++; break; }
        if (have == (int)sizeof(Psp2dcCompRecord) + (rec[1] | rec[2] << 8)) {
            record(rec[0], rec + sizeof(Psp2dcCompRecord), have - (int)sizeof(Psp2dcCompRecord));
            have = 0;
        }
    }
}

int comp_echo_take(unsigned char out[PSP2DC_COMP_EVENT_MAX])
{
    int n = echo_len;
    if (!n) return 0;
    memcpy(out, echo, n);
    echo_len = 0;
    return n;
}
