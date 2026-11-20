/*
 * libpsp2dc against the adapter and the PSP of sim.c, on the computer
 * (scripts/dev/companion-test.sh): a Dreamcast program's frames with a
 * Maple bus that loses commands and replies, a PSP that reads slowly,
 * restarts or is not there, and a memory card unit that goes away.
 * KallistiOS is the few functions below, with its own checks.
 *
 *   test <scenario>      one of them (each starts from nothing)
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <kos.h>
#include "psp2dc.h"
#include "psp2dc_protocol.h"
#include "../../../psp/app/companion.h"
#include "sim.h"

/* ---- KallistiOS ---- */
static maple_frame_t *queued;
static maple_device_t card = { 0, 1, { MAPLE_FUNC_MEMCARD } };    /* the adapter's memory card unit */
static maple_device_t sega = { 0, 1, { MAPLE_FUNC_MEMCARD } };   /* a memory card of Sega's, in port A */
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
void thd_sleep(int ms) { (void)ms; sleeps++; vblank(); }

/* ---- a Dreamcast program ---- */
static const Comp *psp;
static unsigned number;                    /* the next TEST record's */
static int session = -2, restarts;
static int failures;

#define CHECK(what) do { if (!(what)) { printf("    FAIL line %d: %s\n", __LINE__, #what); failures++; } } while (0)

static int send_test(int size)
{
    uint8_t d[PSP2DC_RECORD_MAX];
    int i;
    d[0] = (uint8_t)number; d[1] = (uint8_t)(number >> 8); d[2] = (uint8_t)(number >> 16); d[3] = (uint8_t)(number >> 24);
    for (i = 4; i < size; i++) d[i] = (uint8_t)(number + i - 4);
    if (psp2dc_record(PSP2DC_REC_TEST, d, size) != 0) return 0;
    number++;
    return 1;
}
/* a frame, as dreamcast/test does it: a new session starts the page and the count again */
static void frame(void)
{
    vblank();
    psp2dc_update();
    if (psp2dc_psp() && psp2dc_session() != session && psp2dc_clear() == 0) {
        session = psp2dc_session();
        number = 0;
        restarts++;
    }
}
static void frames(int n) { while (n-- > 0) frame(); }
/* until everything queued has reached the PSP */
static void settle(void)
{
    int i;
    for (i = 0; i < 2000 && !(psp2dc_idle() && psp->test_records == number); i++) frame();
    frames(10);
}
static void all_there(void)
{
    CHECK(psp2dc_idle());
    CHECK(psp->test_records == number);
    CHECK(psp->test_bad == 0);
    CHECK(psp->stream_bad == 0);
}

static void plain(void)
{
    static const int sizes[] = { 4, 64, 256, 996, 5, 131, 1000, 999, 7 };
    psp2dc_stats_t st;
    int i;
    sim_psp_plug(1);
    CHECK(psp2dc_init() == 0);
    CHECK(psp2dc_found() && psp2dc_version() == PSP2DC_PROTOCOL_VERSION);
    frames(3);
    CHECK(psp2dc_psp());
    CHECK(psp2dc_print(0, "the first row") == 0);
    CHECK(psp2dc_print(11, "the last row") == 0);
    CHECK(psp2dc_print(12, "no such row") == 0);
    for (i = 0; i < 3000; i++) {
        frame();
        if (psp2dc_room() > 3000) send_test(sizes[i % 9]);
    }
    settle();
    all_there();
    CHECK(number > 2000);
    CHECK(!strcmp(psp->rows[0], "the first row") && !strcmp(psp->rows[11], "the last row"));
    psp2dc_stats(&st);
    CHECK(st.errors == 0 && agains == 0);
    printf("    %u records, %u bytes; %u commands in %u vblanks\n", number, psp->test_bytes, (unsigned)st.commands, vblanks);
}

static void slow_psp(void)
{
    int i;
    sim_psp_plug(1);
    psp2dc_init();
    usb_every = 4;                         /* the PSP takes a quarter of what the Dreamcast can send */
    for (i = 0; i < 3000; i++) {
        frame();
        if (psp2dc_room() > 1100) send_test(996);
    }
    usb_every = 1;
    settle();
    all_there();
    CHECK(agains > 100);
    printf("    %u records, %u \"again\" answers\n", number, agains);
}

static void lossy(void)
{
    psp2dc_stats_t st;
    int i;
    sim_psp_plug(1);
    psp2dc_init();
    frames(3);
    lose_request = lose_reply = 100;
    for (i = 0; i < 20000; i++) {
        frame();
        if (psp2dc_room() > 3000 && rand() % 3) send_test(4 + rand() % 993);
    }
    lose_request = lose_reply = 0;
    settle();
    all_there();
    psp2dc_stats(&st);
    CHECK(lost_requests > 500 && lost_replies > 500);
    CHECK(st.errors == lost_requests + lost_replies);
    CHECK(psp2dc_found() && restarts == 1);
    printf("    %u records; %u commands and %u replies lost\n", number, lost_requests, lost_replies);
}

/* a SEND, some polls, a SEND: whatever the number of polls between them */
static void far_apart(void)
{
    int gap;
    sim_psp_plug(1);
    psp2dc_init();
    frames(3);
    for (gap = 240; gap <= 270; gap++) {
        send_test(16);
        frames(gap);
    }
    settle();
    all_there();
    CHECK(number == 31);
}

static void psp_restarts(void)
{
    int i, usb_failures = 0;
    sim_psp_plug(1);
    psp2dc_init();
    for (i = 0; i < 30000; i++) {
        frame();
        if (psp2dc_room() > 3000) send_test(4 + rand() % 500);
        if (rand() % 400 == 0) { sim_psp_plug(0); frames(rand() % 100); sim_psp_plug(1); }
        if (rand() % 300 == 0) { sim_usb_fail(); usb_failures++; }
        CHECK(psp->stream_bad == 0);
    }
    frames(100);
    settle();
    all_there();
    CHECK(restarts > 50);
    printf("    %d sessions seen by the program, %d transfers to the PSP lost\n", restarts, usb_failures);
}

static void psp_later(void)
{
    sim_psp_plug(0);
    CHECK(psp2dc_init() == 0);
    frames(100);
    CHECK(psp2dc_found() && !psp2dc_psp());
    CHECK(psp2dc_record(PSP2DC_REC_TEST, "abcd", 4) == -2);
    sim_psp_plug(1);
    frames(3);
    CHECK(psp2dc_psp());
    send_test(100);
    settle();
    all_there();
    CHECK(number == 1);
}

/* a memory card that is no companion, then the adapter put in port B beside it */
static void no_companion(void)
{
    psp2dc_stats_t st;
    unsigned slept;
    int p, u;
    sega_there = 1; card_there = 0; card.port = 1;
    sim_psp_plug(1);
    CHECK(psp2dc_init() == -1);
    slept = sleeps;
    frames(600);
    psp2dc_stats(&st);
    CHECK(!psp2dc_found() && !psp2dc_psp());
    CHECK(sleeps == slept);                /* looking on does not wait */
    CHECK(st.errors == 0 && st.commands == 0);
    CHECK(psp2dc_print(0, "nobody there") == -2);
    card_there = 1;
    frames(200);
    psp2dc_where(&p, &u);
    CHECK(psp2dc_found() && psp2dc_psp() && p == 1 && u == 1);
    CHECK(sleeps == slept);
    send_test(500);
    settle();
    all_there();
}

static void events(void)
{
    static const unsigned presses[] = { 0x4000, 0, 0x8000, 0x8010, 0x10, 0 };
    psp2dc_event_t ev;
    uint8_t e[53];
    unsigned got[16], n = 0, echoes = 0, i, sent_at = 0, trip = 0;
    sim_psp_plug(1);
    psp2dc_init();
    frames(3);
    psp2dc_print(0, "something, so the PSP knows a program is there");
    frames(5);
    for (i = 0; i < 6 * 20; i++) {
        if (i % 20 == 0) sim_psp_buttons(presses[i / 20]);
        if (i % 20 == 10) {
            unsigned k;
            for (k = 0; k < sizeof(e); k++) e[k] = (uint8_t)(i + k);
            CHECK(psp2dc_record(PSP2DC_REC_ECHO, e, sizeof(e)) == 0);
            sent_at = vblanks;
        }
        frame();
        while (psp2dc_poll(&ev)) {
            if (ev.type == PSP2DC_REC_BUTTONS && ev.len == 4 && n < 16)
                got[n++] = ev.data[0] | ev.data[1] << 8 | ev.data[2] << 16 | (unsigned)ev.data[3] << 24;
            else if (ev.type == PSP2DC_REC_ECHO) {
                CHECK(ev.len == (int)sizeof(e) && !memcmp(ev.data, e, sizeof(e)));
                trip = vblanks - sent_at;
                echoes++;
            } else CHECK(!"a record of no kind");
        }
    }
    /* the first is the PSP's buttons as the program begins: nothing held */
    CHECK(n == 7 && got[0] == 0 && !memcmp(got + 1, presses, sizeof(presses)));
    CHECK(echoes == 6);
    printf("    an ECHO comes back in %u frames\n", trip);
}

/* the PSP changes its card: the unit is gone for about a second */
static void card_away(void)
{
    int i, away = 0;
    sim_psp_plug(1);
    psp2dc_init();
    for (i = 0; i < 20000; i++) {
        frame();
        if (psp2dc_room() > 3000) send_test(4 + rand() % 500);
        if (!away && rand() % 700 == 0) { card_there = 0; away = 10 + rand() % 120; }
        if (away && --away == 0) card_there = 1;
        CHECK(psp->stream_bad == 0);
    }
    card_there = 1;
    frames(200);
    settle();
    all_there();
    CHECK(psp2dc_found() && psp2dc_psp());
    printf("    %d times the program started its page again\n", restarts);
}

/* all of it at once, for a long time */
static void everything(void)
{
    int i, away = 0;
    sim_psp_plug(1);
    psp2dc_init();
    lose_request = lose_reply = 20;
    for (i = 0; i < 300000; i++) {
        frame();
        if (psp2dc_room() > 3000 && rand() % 4) send_test(4 + rand() % 997);
        if (rand() % 2000 == 0) usb_every = 1 + rand() % 4;
        if (rand() % 3000 == 0) { sim_psp_plug(0); frames(rand() % 200); sim_psp_plug(1); }
        if (rand() % 2500 == 0) sim_usb_fail();
        if (!away && rand() % 4000 == 0) { card_there = 0; away = 10 + rand() % 120; }
        if (away && --away == 0) card_there = 1;
        if (rand() % 500 == 0) sim_psp_buttons((unsigned)rand());
        { psp2dc_event_t ev; while (psp2dc_poll(&ev)) CHECK(ev.type == PSP2DC_REC_BUTTONS && ev.len == 4); }
        CHECK(psp->stream_bad == 0);
    }
    card_there = 1; usb_every = 1; lose_request = lose_reply = 0;
    frames(200);
    settle();
    all_there();
    printf("    %u vblanks, %d page starts, %u \"again\", %u + %u lost\n", vblanks, restarts, agains, lost_requests,
           lost_replies);
}

static const struct { const char *name, *what; void (*run)(void); } scenarios[] = {
    { "plain",      "found; rows of text; records of every size arrive, each once and whole", plain },
    { "slow",       "a PSP that reads slowly: \"again\", and nothing lost", slow_psp },
    { "lossy",      "a tenth of the commands and a tenth of the replies lost", lossy },
    { "far-apart",  "two sends with 240 to 270 polls between them", far_apart },
    { "restarts",   "the PSP unplugged and back, transfers to it lost", psp_restarts },
    { "psp-later",  "no PSP at first", psp_later },
    { "no-companion", "a plain memory card; then the adapter beside it, in port B", no_companion },
    { "events",     "the PSP's buttons in order; an ECHO's bytes back", events },
    { "card-away",  "the memory card unit gone for up to two seconds", card_away },
    { "everything", "all of it at once, 300000 frames", everything },
};

int main(int argc, char **argv)
{
    unsigned i;
    psp = sim_psp_page();
    srand(1);
    for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        if (argc < 2 || strcmp(argv[1], scenarios[i].name)) continue;
        scenarios[i].run();
        printf("%-4s %-12s %s\n", failures ? "FAIL" : "ok", scenarios[i].name, scenarios[i].what);
        if (failures) sim_psp_print(stdout);
        return failures != 0;
    }
    for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) printf("%s\n", scenarios[i].name);
    return 2;
}
