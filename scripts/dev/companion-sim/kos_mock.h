/*
 * KallistiOS for a Dreamcast program run on the computer: the few functions
 * libpsp2dc uses, with KallistiOS's own checks, and a Maple bus whose other
 * end is the simulator (sim.h). vblank() is a frame's worth of it: what is
 * queued goes out and its reply comes back, unless the knobs here lose it.
 * Included once, by the program's harness (test.c, demo-run.c).
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <kos.h>
#include "psp2dc_protocol.h"
#include "sim.h"

static maple_frame_t *queued;
static maple_device_t card = { 0, 1, { MAPLE_FUNC_MEMCARD } };    /* the adapter's memory card unit */
static maple_device_t sega = { 0, 1, { MAPLE_FUNC_MEMCARD } };   /* a memory card of Sega's, in port A */
static maple_device_t pad = { 0, 0, { MAPLE_FUNC_CONTROLLER } };  /* the controller, and what is held on it */
static cont_state_t pad_state;
static int card_there = 1, sega_there;
static int lose_request, lose_reply;       /* in a thousand */
static int usb_every = 1;                  /* an exchange with the PSP every so many vblanks */
static unsigned vblanks, lost_requests, lost_replies, agains, sleeps;

maple_device_t *maple_enum_dev(int p, int u)
{
    if (sega_there && p == sega.port && u == sega.unit) return &sega;
    return card_there && p == card.port && u == card.unit ? &card : NULL;
}
maple_device_t *maple_enum_type(int n, uint32_t func)
{
    if (func & MAPLE_FUNC_CONTROLLER) return n == 0 ? &pad : NULL;
    if (!(func & MAPLE_FUNC_MEMCARD)) return NULL;
    if (sega_there && n-- == 0) return &sega;
    return card_there && n == 0 ? &card : NULL;
}
int maple_frame_trylock(maple_frame_t *f)
{
    if (f->state != MAPLE_FRAME_VACANT) return -1;
    f->state = MAPLE_FRAME_UNSENT;
    return 0;
}
void maple_frame_init(maple_frame_t *f)
{
    assert(f->state == MAPLE_FRAME_UNSENT);
    assert(!f->queued);
    f->recv_buf = f->recv_buf_arr;
    f->send_buf = (uint32_t *)f->recv_buf;
    f->cmd = f->dst_port = f->dst_unit = -1;
    f->length = 0;
    f->callback = NULL;
}
void maple_frame_unlock(maple_frame_t *f)
{
    assert(f->state == MAPLE_FRAME_RESPONDED);
    f->state = MAPLE_FRAME_VACANT;
}
int maple_queue_frame(maple_frame_t *f)
{
    assert(!queued && f->state == MAPLE_FRAME_UNSENT && f->length <= 255);
    f->queued = 1;
    queued = f;
    return 0;
}

/* a vblank: what is queued goes out, and its reply comes back */
static void vblank(void)
{
    maple_frame_t *f = queued;
    vblanks++;
    if (vblanks % (unsigned)usb_every == 0) sim_usb();
    if (!f) return;
    {
        maple_response_t *resp = (maple_response_t *)f->recv_buf;
        static uint32_t out[256];
        int words = 0, code = -1;
        assert(f->cmd == PSP2DC_COMP_CMD);
        if (sega_there && f->dst_port == sega.port && f->dst_unit == sega.unit) {
            code = 0xFD;                                 /* no such command */
        } else if (card_there && f->dst_port == card.port && f->dst_unit == card.unit) {
            if (rand() % 1000 < lose_request) lost_requests++;
            else {
                code = sim_maple(f->send_buf, f->length, out, &words);
                if (rand() % 1000 < lose_reply) { lost_replies++; code = -1; }
            }
        }
        if (code == 0xFC) { agains++; return; }          /* (KallistiOS sends it again at the next vblank) */
        if (code < 0) memset(resp, 0xFF, 4);
        else {
            resp->response = (int8_t)code; resp->dst_addr = 0; resp->src_addr = 1; resp->data_len = (uint8_t)words;
            memcpy(resp->data, out, (size_t)words * 4);
        }
        f->state = MAPLE_FRAME_RESPONDED;
        f->queued = 0;
        queued = NULL;
        f->callback(NULL, f);
    }
}
void *maple_dev_status(maple_device_t *dev) { return dev == &pad ? &pad_state : NULL; }
void thd_sleep(int ms) { (void)ms; sleeps++; vblank(); }
void arch_menu(void) { exit(0); }
