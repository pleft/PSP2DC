/*
 * libpsp2dc against the adapter and the PSP of sim.c, on the computer
 * (scripts/dev/companion-test.sh): a Dreamcast program's frames with a
 * Maple bus that loses commands and replies, a PSP that reads slowly,
 * restarts or is not there, and a memory card unit that goes away.
 * KallistiOS is the few functions of kos_mock.h, with its own checks.
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

#include "kos_mock.h"

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

/* ---- the screen: widgets and regions ---- */
typedef struct { int used, type, flags, x, y, w, h, value, max, colour; char text[PSP2DC_WIDGET_TEXT_MAX + 1]; } Want;
static Want want[PSP2DC_WIDGETS];          /* what the program made: what the PSP must hold in the end */
static int pic, pic_shown = -1, region_made;

static unsigned abgr(int c)                /* RGB565 as the PSP's pixel */
{
    unsigned r = (unsigned)c >> 11, g = ((unsigned)c >> 5) & 63, b = (unsigned)c & 31;
    return 0xFF000000 | (b << 3 | b >> 2) << 16 | (g << 2 | g >> 4) << 8 | (r << 3 | r >> 2);
}
static int pic_colour(int n, int k) { return (n * 2654435761u >> (k * 5)) & 0xFFFF; }
/* picture n in region 0, whole: 0 when all of it is queued */
static int draw(int n)
{
    char s[16];
    if (psp2dc_room() < 400) return -1;
    snprintf(s, sizeof(s), "%d", n);
    if (psp2dc_region_clip(0, 0, 0, 240, 240)) return -1;
    if (psp2dc_draw_rect(0, -5, -5, 300, 300, pic_colour(n, 0))) return -1;
    if (psp2dc_draw_rect(0, 10, 10, 50, 50, pic_colour(n, 1))) return -1;
    if (psp2dc_draw_line(0, -20, 239, 300, 239, pic_colour(n, 2))) return -1;
    if (psp2dc_draw_line(0, 100, 100, 200, 180, pic_colour(n, 2))) return -1;
    if (psp2dc_region_clip(0, 60, 60, 40, 40)) return -1;
    if (psp2dc_draw_rect(0, 0, 0, 240, 240, pic_colour(n, 1))) return -1;   /* (only inside the clip) */
    if (psp2dc_draw_text(0, 62, 62, pic_colour(n, 0), 2, s)) return -1;
    return psp2dc_present(0);
}
static void screen_frame(void)
{
    psp2dc_event_t ev;
    vblank();
    psp2dc_update();
    while (psp2dc_poll(&ev)) CHECK(ev.type == PSP2DC_REC_BUTTONS && ev.len == 4);
    if (psp2dc_psp() && psp2dc_session() != session) { session = psp2dc_session(); pic_shown = -1; restarts++; }
    if (!region_made && psp2dc_region_create(0, 8, 16, 240, 240) == 0) { region_made = 1; pic_shown = -1; }
    if (region_made && psp2dc_psp() && pic_shown != pic && draw(pic) == 0) pic_shown = pic;
}
static void screen_frames(int n) { while (n-- > 0) screen_frame(); }
static void want_widget(int id, int type, int flags, int x, int y, int w, int h, const char *text)
{
    Want *v = &want[id];
    CHECK(psp2dc_widget_create(id, type, flags, x, y, w, h, text) == 0);
    if (!v->used) { v->value = type == PSP2DC_WIDGET_LIST ? -1 : 0; v->max = type == PSP2DC_WIDGET_BAR ? 100 : 0; v->colour = -1; }
    v->used = 1; v->type = type; v->flags = flags; v->x = x; v->y = y; v->w = w; v->h = h;
    snprintf(v->text, sizeof(v->text), "%s", text);
}
/* the PSP holds what the program made, and shows its last picture */
static void screen_same(void)
{
    int i;
    for (i = 0; i < PSP2DC_WIDGETS; i++) {
        const CompWidget *w = &psp->widgets[i];
        const Want *v = &want[i];
        CHECK(w->used == v->used);
        if (!w->used || !v->used) continue;
        CHECK(w->type == v->type && (w->flags & ~PSP2DC_WF_COLOUR) == v->flags);
        CHECK(w->x == v->x && w->y == v->y && w->w == v->w && w->h == v->h);
        CHECK(w->value == v->value && w->max == v->max);
        CHECK(!(w->flags & PSP2DC_WF_COLOUR) == (v->colour < 0) && (v->colour < 0 || w->colour == v->colour));
        CHECK(!strcmp(w->text, v->text));
    }
    CHECK(psp->screen_bad == 0 && psp->stream_bad == 0);
    if (region_made) {
        const CompRegion *r = &psp->regions[0];
        CHECK(r->w == 240 && r->h == 240 && r->x == 8 && r->y == 16);
        CHECK(pic_shown == pic);
        if (r->w == 240 && r->h == 240) {
            CHECK(r->front[0] == abgr(pic_colour(pic, 0)) && r->front[239 * 240 + 239] == abgr(pic_colour(pic, 2)));
            CHECK(r->front[20 * 240 + 20] == abgr(pic_colour(pic, 1)) && r->front[238 * 240 + 100] == abgr(pic_colour(pic, 0)));
            CHECK(r->front[140 * 240 + 150] == abgr(pic_colour(pic, 2)));          /* on the slanted line */
            CHECK(r->front[99 * 240 + 99] == abgr(pic_colour(pic, 1)) && r->front[100 * 240 + 100] == abgr(pic_colour(pic, 2)));
        }
    } else CHECK(psp->regions[0].w == 0);
}
static void random_text(char *t, int lines)
{
    int n = rand() % 4 ? rand() % 30 : rand() % (PSP2DC_WIDGET_TEXT_MAX + 1), i;
    for (i = 0; i < n; i++) t[i] = lines && rand() % 12 == 0 ? '\n' : (char)(0x20 + rand() % 95);
    t[n] = 0;
}
/* something a program does to its screen */
static void screen_op(void)
{
    int id = rand() % 4 ? rand() % 6 : rand() % PSP2DC_WIDGETS, op = rand() % 12;   /* (a few of them often) */
    Want *v = &want[id];
    char t[PSP2DC_WIDGET_TEXT_MAX + 1];
    if (op < 3 || !v->used) {
        int type = rand() % 5;
        random_text(t, type == PSP2DC_WIDGET_LIST);
        want_widget(id, type, (rand() % 16) << 1, rand() % 600 - 60, rand() % 400 - 60, rand() % 500, rand() % 300, t);
    } else if (op < 7) {
        v->value = rand() % 3 ? rand() % 1000 : rand();
        CHECK(psp2dc_widget_set_u32(id, (uint32_t)v->value) == 0);
    } else if (op < 9) {
        random_text(v->text, v->type == PSP2DC_WIDGET_LIST);
        CHECK(psp2dc_widget_set_text(id, v->text) == 0);
    } else if (op == 9) {
        v->max = 1 + rand() % 5000;
        CHECK(psp2dc_widget_set_range(id, (uint32_t)v->max) == 0);
    } else if (op == 10) {
        v->colour = rand() % 4 ? rand() & 0xFFFF : -1;
        CHECK(psp2dc_widget_set_colour(id, v->colour) == 0);
    } else {
        v->used = 0;
        CHECK(psp2dc_widget_remove(id) == 0);
    }
}

static void screen(void)
{
    psp2dc_caps_t caps;
    psp2dc_stats_t st;
    sim_psp_plug(0);
    CHECK(psp2dc_init() == 0);
    /* made with no PSP there: it all goes when one comes */
    want_widget(0, PSP2DC_WIDGET_LABEL, PSP2DC_WF_BIG | PSP2DC_WF_CENTRE, 256, 8, 216, 24, "DASHBOARD");
    want_widget(1, PSP2DC_WIDGET_BAR, PSP2DC_WF_PANEL, 256, 40, 216, 36, "Health");
    want_widget(2, PSP2DC_WIDGET_NUMBER, PSP2DC_WF_PANEL | PSP2DC_WF_BIG, 256, 80, 216, 28, "Score");
    want_widget(3, PSP2DC_WIDGET_LIGHT, 0, 256, 112, 216, 14, "Danger");
    want_widget(63, PSP2DC_WIDGET_LIST, PSP2DC_WF_PANEL, 256, 130, 216, 80, "Sword\nShield\nKey");
    want[1].value = 84; psp2dc_widget_set_u32(1, 84);
    CHECK(psp2dc_widget_set_u32(5, 1) == -1 && psp2dc_widget_create(64, 0, 0, 0, 0, 1, 1, "") == -1);
    CHECK(psp2dc_draw_rect(0, 0, 0, 1, 1, 0) == -1);       /* no such region yet */
    screen_frames(30);
    CHECK(!psp2dc_get_caps(&caps) && !psp->screen);
    sim_psp_plug(1);
    screen_frames(30);
    CHECK(psp2dc_psp() && psp->screen);
    CHECK(psp2dc_get_caps(&caps) && caps.level == 3 && caps.width == 480 && caps.height == 272);
    CHECK(caps.formats == 0xF && caps.sprites == PSP2DC_SPRITES && caps.sprite_bytes == PSP2DC_SPRITE_BYTES);
    CHECK(caps.widgets == PSP2DC_WIDGETS && caps.regions == PSP2DC_REGIONS && caps.widget_types == 0x1F);
    screen_same();
    /* what changes goes alone: a value is 8 bytes on the way, and one that did not change is none */
    psp2dc_stats(&st);
    {
        unsigned before = st.bytes_out;
        int i;
        for (i = 0; i < 50; i++) { psp2dc_widget_set_u32(2, 1234); want[2].value = 1234; screen_frame(); }
        psp2dc_stats(&st);
        CHECK(st.bytes_out - before == 8);
        printf("    50 frames setting a number to the same value: %u bytes sent\n", (unsigned)st.bytes_out - before);
    }
    want[63].value = 2; psp2dc_widget_set_u32(63, 2);
    snprintf(want[0].text, sizeof(want[0].text), "PAUSED"); psp2dc_widget_set_text(0, "PAUSED");
    want[3].colour = PSP2DC_RGB565(255, 0, 0); psp2dc_widget_set_colour(3, want[3].colour);
    want[2].used = 0; psp2dc_widget_remove(2);
    pic = 7;
    CHECK(psp2dc_notify("Key found") == 0);
    screen_frames(30);
    screen_same();
    CHECK(!strcmp(psp->notice, "Key found"));
    /* a region too large, and regions beyond what the PSP has room for, are refused there */
    CHECK(psp2dc_region_create(1, 0, 0, 481, 10) == -1 && psp2dc_region_create(4, 0, 0, 10, 10) == -1);
    CHECK(psp2dc_region_create(1, 0, 0, 480, 272) == 0);
    screen_frames(30);
    CHECK(psp->screen_bad == 1 && psp->regions[1].w == 0);
    CHECK(psp2dc_region_remove(1) == 0);
    pic_shown = -1;                                        /* (making a region emptied the other) */
    screen_frames(30);
    psp->screen_bad == 1 ? (void)(((Comp *)psp)->screen_bad = 0) : (void)0;
    screen_same();
    /* nothing sent for a while: the PSP still hears of the program every second */
    {
        unsigned before = psp->records;
        screen_frames(600);
        CHECK(psp->records - before >= 9 && psp->records - before <= 11);
    }
    /* all of it gone */
    psp2dc_screen_reset();
    memset(want, 0, sizeof(want));
    region_made = 1; pic_shown = pic;                      /* (no region made again here) */
    screen_frames(30);
    region_made = 0;
    screen_same();
    CHECK(!psp->screen);
    sim_psp_screen_save("/dev/null");
}

/* the screen through everything at once: in the end the PSP holds what the program made */
static void screen_faults(void)
{
    int i, away = 0, round;
    sim_psp_plug(1);
    psp2dc_init();
    for (round = 0; round < 20; round++) {
        lose_request = lose_reply = 20;
        for (i = 0; i < 5000; i++) {
            screen_frame();
            if (rand() % 3 == 0) { int k = 1 + rand() % 6; while (k--) screen_op(); }   /* (several between two frames) */
            if (rand() % 40 == 0) pic++;
            if (rand() % 3000 == 0) {
                psp2dc_screen_reset();
                memset(want, 0, sizeof(want));
                region_made = 0;
            }
            if (rand() % 2000 == 0) usb_every = 1 + rand() % 4;
            if (rand() % 1500 == 0) { sim_psp_plug(0); screen_frames(rand() % 200); sim_psp_plug(1); }
            if (rand() % 1200 == 0) sim_usb_fail();
            if (!away && rand() % 2000 == 0) { card_there = 0; away = 10 + rand() % 120; }
            if (away && --away == 0) card_there = 1;
            if (rand() % 500 == 0) sim_psp_buttons((unsigned)rand());
        }
        card_there = 1; away = 0; usb_every = 1; lose_request = lose_reply = 0;
        screen_frames(400);
        screen_same();
        if (failures) break;
    }
    printf("    %u vblanks, %d screen starts, %u \"again\", %u + %u lost\n", vblanks, restarts, agains, lost_requests,
           lost_replies);
}

/* ---- pictures: bitmaps, palettes, sprites ---- */
/* The program's region as it must look, kept here by the same steps done plainly. */
#define PW 200
#define PH 120
static unsigned model[PW * PH], model_pal[256];
static uint16_t my_pal[256];
static uint8_t art[4][64 * 64 * 2];        /* a picture in each format, 64 x 64 at most */
static int art_w[4], art_h[4];
static int pictures_due, pictures_step;    /* the whole picture is to be drawn; how far that is */
static int palette_due;                    /* the palette is to be set: once, the library sends it again */

static unsigned art_pixel(int f, int x, int y)   /* as the program holds it: an index, or a colour */
{
    const uint8_t *row = art[f] + y * PSP2DC_FMT_ROW(f, art_w[f]);
    return f == 0 ? (row[x >> 3] >> (7 - (x & 7))) & 1 : f == 1 ? (row[x >> 1] >> (x & 1 ? 0 : 4)) & 15
         : f == 2 ? row[x] : (unsigned)(row[2 * x] | row[2 * x + 1] << 8);
}
static void model_put(int x, int y, unsigned c) { if (x >= 0 && y >= 0 && x < PW && y < PH) model[y * PW + x] = c; }
static void model_art(int f, int x, int y, int key)
{
    int i, j;
    for (j = 0; j < art_h[f]; j++)
        for (i = 0; i < art_w[f]; i++) {
            unsigned v = art_pixel(f, i, j);
            if (key >= 0 && v == (unsigned)key) continue;
            model_put(x + i, y + j, f == 3 ? abgr((int)v) : model_pal[v]);
        }
}
static void pictures_new(void)             /* other pictures, another palette */
{
    int f, i;
    for (f = 0; f < 4; f++) {
        art_w[f] = 1 + rand() % 64; art_h[f] = 1 + rand() % 64;
        for (i = 0; i < (int)sizeof(art[f]); i++) art[f][i] = (uint8_t)rand();
        CHECK(psp2dc_sprite_create(f, art_w[f], art_h[f], f, art[f], PSP2DC_FMT_ROW(f, art_w[f])) == 0);
    }
    for (i = 0; i < 256; i++) my_pal[i] = (uint16_t)rand();
    pictures_due = palette_due = 1;
}
/* The whole picture, a step at a time as the queue takes it: the palette, a bitmap in each
 * format, then each sprite twice (once keyed). 1 when it is all queued and presented. */
static int pictures_draw(void)
{
    static int rows;
    int f, i;
    if (pictures_due) { pictures_due = 0; pictures_step = 0; rows = 0; }
    for (;;) {
        if (pictures_step == 0) {
            if (psp2dc_draw_rect(0, 0, 0, PW, PH, 0x1234)) return 0;
            for (i = 0; i < PW * PH; i++) model[i] = abgr(0x1234);
            for (i = 0; i < 256; i++) model_pal[i] = abgr(my_pal[i]);
        } else if (pictures_step <= 4) {
            int n;
            f = pictures_step - 1;
            n = psp2dc_draw_bitmap(0, f * 45 - 10, f * 20 - 5 + rows, art_w[f], art_h[f] - rows, f,
                                   art[f] + rows * PSP2DC_FMT_ROW(f, art_w[f]), PSP2DC_FMT_ROW(f, art_w[f]));
            if (n < 0) return 0;
            rows += n;
            if (rows < art_h[f]) return 0;
            model_art(f, f * 45 - 10, f * 20 - 5, -1);
            rows = 0;
        } else if (pictures_step <= 8) {
            f = pictures_step - 5;
            if (!psp2dc_sprite_ready(f) || psp2dc_room() < 100) return 0;
            if (psp2dc_draw_sprite(0, f, 150 - f * 40, 70 - f * 9, -1)) return 0;
            if (psp2dc_draw_sprite(0, f, 20 + f * 30, 60, f == 3 ? art[3][0] | art[3][1] << 8 : 1)) return 0;
            model_art(f, 150 - f * 40, 70 - f * 9, -1);
            model_art(f, 20 + f * 30, 60, f == 3 ? art[3][0] | art[3][1] << 8 : 1);
        } else {
            if (psp2dc_present(0)) return 0;
            return 1;
        }
        pictures_step++;
    }
}
static int pictures_shown;
static void pictures_frame(void)
{
    vblank();
    psp2dc_update();
    if (psp2dc_psp() && psp2dc_session() != session) { session = psp2dc_session(); pictures_due = 1; pictures_shown = 0; restarts++; }
    if (!region_made && psp2dc_region_create(0, 30, 40, PW, PH) == 0) { region_made = 1; pictures_due = palette_due = 1; pictures_shown = 0; }
    if (region_made && palette_due && psp2dc_region_palette(0, 0, 256, my_pal) == 0) palette_due = 0;
    if (pictures_due) pictures_shown = 0;
    if (region_made && !palette_due && psp2dc_psp() && !pictures_shown) pictures_shown = pictures_draw();
}
static void pictures_same(void)
{
    const CompRegion *r = &psp->regions[0];
    int i, bad = 0;
    CHECK(pictures_shown && r->w == PW && r->h == PH && psp->screen_bad == 0 && psp->stream_bad == 0);
    if (r->w != PW || r->h != PH) return;
    for (i = 0; i < PW * PH; i++) bad += r->front[i] != model[i];
    CHECK(bad == 0);
    if (bad) printf("    %d pixels differ\n", bad);
}

static void pictures(void)
{
    psp2dc_stats_t st;
    int i;
    sim_psp_plug(1);
    psp2dc_init();
    pictures_new();
    for (i = 0; i < 400 && !pictures_shown; i++) pictures_frame();
    psp2dc_stats(&st);
    printf("    the picture: %u bytes, queued in %d frames\n", (unsigned)st.bytes_out, i);
    for (i = 0; i < 20; i++) pictures_frame();
    pictures_same();
    CHECK(psp->sprite_bytes == (unsigned)(art_w[0] * art_h[0] + art_w[1] * art_h[1] + art_w[2] * art_h[2] + 2 * art_w[3] * art_h[3]));
    /* a widget set while a large bitmap goes is not held up by it */
    {
        static uint8_t big[240 * 2 * 100];
        unsigned at = vblanks;
        int rows = 0, n, seen = 0;
        want_widget(9, PSP2DC_WIDGET_NUMBER, 0, 300, 10, 100, 10, "n");
        for (i = 0; i < 20; i++) pictures_frame();
        while (rows < 100) {
            n = psp2dc_draw_bitmap(0, 0, rows, 200, 100 - rows, PSP2DC_FMT_565, big + rows * 480, 480);
            CHECK(n >= 0 && psp2dc_room() >= 1024);          /* (room is left for what is small) */
            rows += n;
            if (!seen && rows > 20) { psp2dc_widget_set_u32(9, 77); at = vblanks; seen = 1; }
            pictures_frame();
            if (seen == 1 && psp->widgets[9].value == 77) { seen = 2; printf("    a value set amid a bitmap arrives in %u frames\n", vblanks - at); CHECK(vblanks - at <= 6); }
        }
        CHECK(seen == 2);
    }
    /* the palette a region starts with: red in an index's top 3 bits, green in the next 3, blue in the low 2 */
    {
        static const uint8_t four[4] = { 0xE0, 0x1C, 0x03, 0xFF };
        CHECK(psp2dc_region_create(1, 0, 0, 4, 1) == 0);
        CHECK(psp2dc_draw_bitmap(1, 0, 0, 4, 1, PSP2DC_FMT_8, four, 4) == 1 && psp2dc_present(1) == 0);
        for (i = 0; i < 20; i++) { vblank(); psp2dc_update(); }
        CHECK(psp->regions[1].w == 4 && psp->regions[1].front[0] == 0xFF0000FF && psp->regions[1].front[1] == 0xFF00FF00);
        CHECK(psp->regions[1].w == 4 && psp->regions[1].front[2] == 0xFFFF0000 && psp->regions[1].front[3] == 0xFFFFFFFF);
    }
    /* what cannot be */
    CHECK(psp2dc_draw_bitmap(0, 0, 0, 500, 1, PSP2DC_FMT_565, art[3], 1000) == -1);   /* a row longer than a record */
    CHECK(psp2dc_draw_bitmap(2, 0, 0, 4, 4, PSP2DC_FMT_8, art[2], 4) == -1);
    CHECK(psp2dc_draw_sprite(0, 9, 0, 0, -1) == -1 && psp2dc_sprite_create(64, 1, 1, 0, art[0], 1) == -1);
    CHECK(psp2dc_region_palette(0, 300, 1, my_pal) == -1);
}

/* pictures through everything at once: in the end the region is, pixel for pixel, what the program drew */
static void pictures_faults(void)
{
    int i, away = 0, round;
    sim_psp_plug(1);
    psp2dc_init();
    pictures_new();
    for (round = 0; round < 15; round++) {
        lose_request = lose_reply = 20;
        for (i = 0; i < 4000; i++) {
            pictures_frame();
            if (rand() % 150 == 0) pictures_new();
            if (rand() % 2500 == 0) { psp2dc_screen_reset(); region_made = 0; pictures_new(); }
            if (rand() % 2000 == 0) usb_every = 1 + rand() % 4;
            if (rand() % 1500 == 0) { sim_psp_plug(0); frames(rand() % 200); sim_psp_plug(1); }
            if (rand() % 1200 == 0) sim_usb_fail();
            if (!away && rand() % 2000 == 0) { card_there = 0; away = 10 + rand() % 120; }
            if (away && --away == 0) card_there = 1;
        }
        card_there = 1; away = 0; usb_every = 1; lose_request = lose_reply = 0;
        for (i = 0; i < 600; i++) pictures_frame();
        pictures_same();
        if (failures) break;
    }
    printf("    %u vblanks, %d screen starts, %u \"again\", %u + %u lost\n", vblanks, restarts, agains, lost_requests,
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
    { "screen",     "widgets and a region: made, changed, removed; what the PSP can show", screen },
    { "screen-faults", "the screen through everything at once, 100000 frames", screen_faults },
    { "pictures",   "a palette, bitmaps in every format, sprites: pixel for pixel", pictures },
    { "pictures-faults", "pictures through everything at once, 60000 frames", pictures_faults },
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
