/* See psp2dc.h. */
#include <string.h>
#include <kos.h>
#include <dc/maple.h>
#include "psp2dc.h"
#include "psp2dc_protocol.h"

static maple_frame_t frame;              /* the library's own: the VMU driver's is left alone */
static uint32_t send_buf[2 + PSP2DC_COMP_DATA_MAX / 4] __attribute__((aligned(32)));

static int port = -1, unit;              /* where the companion is (port < 0: not found) */
static volatile int busy;                /* a command is out */
static volatile int found, psp, version, session = -1;
static volatile int lost;                /* commands in a row with no good answer */
static volatile int failed;              /* the command that is out got none: its records go again */
static uint8_t seq;

/* records waiting to go (whole ones only) */
static uint8_t out[4096];
static int out_len;
static int sent_len;                     /* of them, in the command that is out */

/* bytes from the PSP: the reply handler puts (an interrupt), psp2dc_poll takes */
#define IN_SIZE 2048
static uint8_t in[IN_SIZE];
static volatile uint32_t in_put, in_got;

static psp2dc_stats_t stats;
static volatile int hello_done;          /* 1 the companion, -1 not */

static void reply(maple_state_t *st, maple_frame_t *frm)
{
    maple_response_t *resp = (maple_response_t *)frm->recv_buf;
    const Psp2dcCompReply *r = (const Psp2dcCompReply *)(resp->data + 4);
    const uint8_t *data = resp->data + 4 + sizeof(Psp2dcCompReply);
    int good = resp->response == MAPLE_RESPONSE_DATATRF && resp->data_len >= 3 &&
               r->len <= PSP2DC_COMP_DATA_MAX && (uint32_t)r->len + 12 <= (uint32_t)resp->data_len * 4;
    uint8_t op = ((const uint8_t *)&send_buf[1])[0];
    (void)st;
    maple_frame_unlock(frm);
    if (!good) {
        stats.errors++;
        lost++;
        failed = 1;
        if (op == PSP2DC_COMP_OP_HELLO) hello_done = -1;
        busy = 0;
        return;
    }
    lost = failed = 0;
    stats.commands++;
    if (op == PSP2DC_COMP_OP_HELLO) {
        const Psp2dcCompHello *h = (const Psp2dcCompHello *)data;
        if (r->len >= sizeof(*h) && !memcmp(h->magic, "PSP2DC", 6)) {
            version = h->version;
            found = 1;
            hello_done = 1;
        } else {
            hello_done = -1;
        }
    } else {
        uint32_t i;
        if (r->status == PSP2DC_COMP_OK && sent_len) stats.bytes_out += sent_len;
        for (i = 0; i < r->len; i++)             /* (full: the oldest are written over; never at these sizes) */
            in[(in_put + i) & (IN_SIZE - 1)] = data[i];
        in_put += r->len;
        stats.bytes_in += r->len;
    }
    psp = r->status != PSP2DC_COMP_NO_PSP;
    if (r->session != session) {                 /* the PSP started over: what was on its way is gone */
        if (session >= 0) in_got = in_put;
        session = r->session;
    }
    busy = 0;
}

/* one command out; 0, or -1 when the frame or the unit is not there */
static int command(int p, int u, int op, const uint8_t *data, int len, int again)
{
    Psp2dcCompRequest *req = (Psp2dcCompRequest *)&send_buf[1];
    maple_device_t *dev = maple_enum_dev(p, u);
    if (!dev || !(dev->info.functions & MAPLE_FUNC_MEMCARD)) return -1;
    if (maple_frame_trylock(&frame) < 0) return -1;
    maple_frame_init(&frame);
    memset(send_buf, 0, sizeof(send_buf[0]) * (2 + (len + 3) / 4));
    send_buf[0] = MAPLE_FUNC_MEMCARD;
    if (!again) seq = seq % 255 + 1;             /* (the same seq again: the adapter takes the data once) */
    req->op = op; req->seq = seq; req->len = len;
    if (len) memcpy(req + 1, data, len);
    frame.cmd = PSP2DC_COMP_CMD;
    frame.dst_port = p;
    frame.dst_unit = u;
    frame.length = 2 + (len + 3) / 4;
    frame.send_buf = send_buf;
    frame.callback = reply;
    busy = 1;
    maple_queue_frame(&frame);
    return 0;
}

/* HELLO to one memory card unit, waited for */
static int hello(int p, int u)
{
    int i;
    hello_done = 0;
    seq = 0;
    if (command(p, u, PSP2DC_COMP_OP_HELLO, NULL, 0, 0) < 0) return -1;
    for (i = 0; i < 25 && !hello_done; i++) thd_sleep(20);
    if (hello_done == 1) { port = p; unit = u; return 0; }
    return -1;
}

static int look(void)
{
    maple_device_t *dev;
    int n;
    if (busy) return -1;
    for (n = 0; (dev = maple_enum_type(n, MAPLE_FUNC_MEMCARD)); n++)
        if (hello(dev->port, dev->unit) == 0) return 0;
    return -1;
}

int psp2dc_init(void)
{
    frame.state = MAPLE_FRAME_VACANT;
    port = -1;
    found = psp = 0;
    out_len = sent_len = 0;
    return look();
}

void psp2dc_update(void)
{
    static int frames;
    frames++;
    if (busy) return;                            /* the last command is still out (an "again" is resent for us) */
    if (sent_len && failed && port >= 0 && lost <= 30) {   /* no answer: the same records, the same seq */
        if (command(port, unit, PSP2DC_COMP_OP_SEND, out, sent_len, 1) < 0) lost++;
        return;
    }
    if (sent_len) {                              /* it went: its records leave the queue */
        memmove(out, out + sent_len, out_len - sent_len);
        out_len -= sent_len;
        sent_len = 0;
    }
    if (port >= 0 && lost > 30) { port = -1; found = psp = 0; out_len = 0; lost = failed = 0; }   /* gone: look again */
    if (port < 0) {
        if (frames % 60 == 0) look();            /* (waits up to half a second a unit, once a second) */
        return;
    }
    if (out_len && psp) {
        /* whole records, as many as fit a command */
        int n = 0;
        while (n < out_len) {
            int rec = 3 + (out[n + 1] | out[n + 2] << 8);
            if (n + rec > PSP2DC_COMP_DATA_MAX) break;
            n += rec;
        }
        sent_len = n;
        if (command(port, unit, PSP2DC_COMP_OP_SEND, out, n, 0) < 0) { sent_len = 0; lost++; }
    } else {
        if (!psp) out_len = 0;
        if (command(port, unit, PSP2DC_COMP_OP_POLL, NULL, 0, 0) < 0) lost++;
    }
}

int psp2dc_found(void) { return found; }
int psp2dc_psp(void) { return found && psp; }
int psp2dc_version(void) { return version; }
int psp2dc_session(void) { return session; }
void psp2dc_where(int *p, int *u) { *p = port; *u = unit; }

int psp2dc_record(int type, const void *data, int len)
{
    if (len < 0 || len > PSP2DC_RECORD_MAX - 3) return -1;
    if (!psp2dc_psp()) return -2;
    if (out_len + 3 + len > (int)sizeof(out)) return -1;
    out[out_len] = type; out[out_len + 1] = len & 0xFF; out[out_len + 2] = len >> 8;
    if (len) memcpy(out + out_len + 3, data, len);
    out_len += 3 + len;
    return 0;
}
int psp2dc_clear(void) { return psp2dc_record(PSP2DC_REC_CLEAR, NULL, 0); }
int psp2dc_print(int row, const char *text)
{
    uint8_t d[1 + 80];
    int n = strlen(text);
    if (n > 80) n = 80;
    d[0] = row;
    memcpy(d + 1, text, n);
    return psp2dc_record(PSP2DC_REC_PRINT, d, 1 + n);
}
int psp2dc_room(void) { return (int)sizeof(out) - out_len; }
int psp2dc_idle(void) { return !busy && out_len == 0; }

int psp2dc_poll(psp2dc_event_t *ev)
{
    uint32_t have = in_put - in_got, i;
    int len;
    if (have < 3) return 0;
    len = in[(in_got + 1) & (IN_SIZE - 1)] | in[(in_got + 2) & (IN_SIZE - 1)] << 8;
    if (len > (int)sizeof(ev->data)) { in_got = in_put; return 0; }   /* no such record: drop what is there */
    if (have < 3u + len) return 0;
    ev->type = in[in_got & (IN_SIZE - 1)];
    ev->len = len;
    for (i = 0; i < (uint32_t)len; i++) ev->data[i] = in[(in_got + 3 + i) & (IN_SIZE - 1)];
    in_got += 3 + len;
    return 1;
}

void psp2dc_stats(psp2dc_stats_t *o) { *o = stats; }
