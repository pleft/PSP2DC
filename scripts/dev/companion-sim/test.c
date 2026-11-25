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
    CHECK(psp2dc_get_caps(&caps) && caps.level == 5 && caps.width == 480 && caps.height == 272);
    CHECK(caps.files_max == PSP2DC_FILES_MAX);
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

/* ---- focus ---- */
static void focus_test(void)
{
    sim_psp_plug(1);
    psp2dc_init();
    psp2dc_focus(PSP2DC_PSP_L | PSP2DC_PSP_R | 0x0001 | 0x10000);   /* (SELECT and HOME are not the program's) */
    frames(10);
    CHECK(psp->focus == (PSP2DC_PSP_L | PSP2DC_PSP_R));
    sim_psp_plug(0); frames(5); sim_psp_plug(1);           /* a new PSP session: taken again */
    CHECK(psp->focus == 0);
    frames(10);
    CHECK(psp->focus == (PSP2DC_PSP_L | PSP2DC_PSP_R));
    psp2dc_screen_reset();                                 /* the screen emptied: the buttons stay taken */
    frames(10);
    CHECK(psp->focus == (PSP2DC_PSP_L | PSP2DC_PSP_R));
    psp2dc_focus(PSP2DC_PSP_CROSS);
    frames(10);
    CHECK(psp->focus == PSP2DC_PSP_CROSS);
    psp2dc_focus(0);
    frames(10);
    CHECK(psp->focus == 0);
}

/* A program started while the PSP stays linked (the next disc): the PSP's session is the same,
 * and what the program before left is still there. */
static void leftovers(void)
{
    int i;
    Comp *was = (Comp *)psp;                               /* (the test plays the earlier program) */
    sim_psp_plug(1);
    for (i = 0; i < 10; i++) vblank();
    was->focus = PSP2DC_PSP_L | PSP2DC_PSP_R;
    strcpy(was->rows[2], "X pressed 28 times");
    psp2dc_init();
    for (i = 0; i < 10; i++) { vblank(); psp2dc_update(); }   /* (a program that sends no rows and takes no buttons) */
    CHECK(psp2dc_psp());
    CHECK(psp->focus == 0);
    CHECK(psp->rows[2][0] == 0);
}

/* ---- files ---- */
static char files_dir[1024];               /* the simulated Memory Stick's folder for files */
static void files_fresh(void)
{
    char cmd[1100];
    const char *d = getenv("PSP2DC_SIM_FILES");
    snprintf(files_dir, sizeof(files_dir), "%s", d ? d : "build/companion-sim/files");
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", files_dir);
    if (system(cmd) != 0) CHECK(!"the files' folder could not be emptied");
    sim_files_root(files_dir);
}
/* a file's bytes, from a seed */
static uint8_t byte_of(uint32_t seed, uint32_t i) { uint32_t x = (seed + i) * 2654435761u; return (uint8_t)(x >> 13 ^ x >> 24); }
static void bytes_of(uint8_t *p, uint32_t seed, uint32_t n) { uint32_t i; for (i = 0; i < n; i++) p[i] = byte_of(seed, i); }
static long long disk_size(const char *name)
{
    char p[1200];
    FILE *f;
    long long n;
    snprintf(p, sizeof(p), "%s/testgame/%s", files_dir, name);
    if (!(f = fopen(p, "rb"))) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}
static int disk_is(const char *name, uint32_t seed, uint32_t n)   /* the file holds those bytes */
{
    static uint8_t a[1 << 20];
    char p[1200];
    FILE *f;
    uint32_t i, got;
    snprintf(p, sizeof(p), "%s/testgame/%s", files_dir, name);
    if (!(f = fopen(p, "rb"))) return 0;
    got = (uint32_t)fread(a, 1, sizeof(a), f);
    fclose(f);
    if (got != n) return 0;
    for (i = 0; i < n; i++) if (a[i] != byte_of(seed, i)) return 0;
    return 1;
}
static void disk_put(const char *name, uint32_t seed, uint32_t n)
{
    static uint8_t a[1 << 20];
    char p[1200];
    FILE *f;
    snprintf(p, sizeof(p), "%s/testgame/%s", files_dir, name);
    bytes_of(a, seed, n);
    if (!(f = fopen(p, "wb"))) { CHECK(!"a file could not be made"); return; }
    fwrite(a, 1, n, f);
    fclose(f);
}
static int temps_left(void)                /* the PSP's own files left in the folder */
{
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "find '%s/testgame' -name '.*' | grep -q .", files_dir);
    return system(cmd) == 0;
}
/* an operation started (when "not now", frame by frame until it can be) and waited for: its status */
#define START(call) ({ int r_, k_ = 0; while ((r_ = (call)) == -1 && k_++ < 600) frame(); r_; })
static uint32_t op_n;
static unsigned op_frames;
static int wait_op(void)
{
    int i, st;
    unsigned at = vblanks;
    for (i = 0; i < 100000; i++) {
        st = psp2dc_file_status(&op_n);
        if (st != PSP2DC_FILE_BUSY) { op_frames = vblanks - at; return st; }
        frame();
    }
    return 99;
}
#define RUN(call) (START(call) == 0 ? wait_op() : 100)

static void files(void)
{
    static uint8_t a[70000], b[70000];
    static char list[4096];
    int w;
    files_fresh();
    sim_psp_plug(1);
    CHECK(psp2dc_init() == 0);
    frames(10);
    CHECK(psp2dc_file_size("one.dat") == -2);              /* no folder given */
    CHECK(psp2dc_file_status(NULL) == PSP2DC_FILE_NONE);
    CHECK(psp2dc_files("Bad") == -1 && psp2dc_files("a/b") == -1 && psp2dc_files("") == -1);
    CHECK(psp2dc_files("seventeen-letters") == -1 && psp2dc_files("testgame") == 0);
    CHECK(START(psp2dc_file_size("../x")) == -2 && psp2dc_file_size(".hidden") == -2 && psp2dc_file_size("a//b") == -2);
    CHECK(psp2dc_file_size("A.SAV") == -2 && psp2dc_file_size("") == -2 && psp2dc_file_size("a/") == -2);
    CHECK(psp2dc_file_size("a/.b") == -2);
    /* a write, and the file it made */
    bytes_of(a, 7, sizeof(a));
    CHECK(RUN(psp2dc_file_write("save/one.dat", a, sizeof(a))) == PSP2DC_FILE_DONE && op_n == sizeof(a));
    printf("    70000 bytes written in %u frames (%u KB a second)\n", op_frames, (unsigned)(sizeof(a) * 60 / 1024 / op_frames));
    CHECK(disk_is("save/one.dat", 7, sizeof(a)) && !temps_left());
    CHECK(START(psp2dc_file_write("x", a, 1)) == 0 && psp2dc_file_size("x") == -1);   /* (one at a time) */
    CHECK(wait_op() == PSP2DC_FILE_DONE && RUN(psp2dc_file_delete("x")) == PSP2DC_FILE_DONE);
    CHECK(RUN(psp2dc_file_size("save/one.dat")) == PSP2DC_FILE_DONE && op_n == sizeof(a));
    /* reads: whole, a part, past the end, of nothing */
    memset(b, 0, sizeof(b));
    CHECK(RUN(psp2dc_file_read("save/one.dat", 0, b, sizeof(b))) == PSP2DC_FILE_DONE && op_n == sizeof(a));
    CHECK(!memcmp(a, b, sizeof(a)));
    printf("    70000 bytes read in %u frames (%u KB a second)\n", op_frames, (unsigned)(sizeof(a) * 60 / 1024 / op_frames));
    memset(b, 0, sizeof(b));
    CHECK(RUN(psp2dc_file_read("save/one.dat", 1000, b, 5000)) == PSP2DC_FILE_DONE && op_n == 5000);
    CHECK(!memcmp(a + 1000, b, 5000));
    CHECK(RUN(psp2dc_file_read("save/one.dat", 69000, b, 5000)) == PSP2DC_FILE_DONE && op_n == 1000);
    CHECK(!memcmp(a + 69000, b, 1000));
    CHECK(RUN(psp2dc_file_read("save/one.dat", 80000, b, 10)) == PSP2DC_FILE_DONE && op_n == 0);
    CHECK(RUN(psp2dc_file_read("nothing", 0, b, 10)) == PSP2DC_FILE_NOT_FOUND);
    CHECK(RUN(psp2dc_file_size("nothing")) == PSP2DC_FILE_NOT_FOUND);
    /* a file replaced; an empty one; the listing */
    CHECK(RUN(psp2dc_file_write("save/one.dat", a + 5, 100)) == PSP2DC_FILE_DONE && disk_is("save/one.dat", 12, 100));
    CHECK(RUN(psp2dc_file_write("empty", a, 0)) == PSP2DC_FILE_DONE && disk_size("empty") == 0);
    memset(list, 0, sizeof(list));
    CHECK(RUN(psp2dc_file_list(list, sizeof(list) - 1)) == PSP2DC_FILE_DONE && op_n == strlen(list));
    CHECK(strstr(list, "save/one.dat 100\n") && strstr(list, "empty 0\n") && !strstr(list, "/.") && list[0] != '.');
    CHECK(RUN(psp2dc_file_list(list, 5)) == PSP2DC_FILE_DONE && op_n == 5);
    /* room: the folder's limit, then the Memory Stick's free space */
    CHECK(RUN(psp2dc_files_free()) == PSP2DC_FILE_DONE && op_n == PSP2DC_FILES_MAX - 100);
    CHECK(RUN(psp2dc_file_write("big", a, PSP2DC_FILES_MAX)) == PSP2DC_FILE_NO_ROOM && disk_size("big") < 0);
    CHECK(psp2dc_file_write("big", a, PSP2DC_FILES_MAX + 1) == -2);
    sim_files_free(200000);
    CHECK(RUN(psp2dc_files_free()) == PSP2DC_FILE_DONE && op_n == 200000 - 65536);
    CHECK(RUN(psp2dc_file_write("save/one.dat", a, sizeof(a))) == PSP2DC_FILE_DONE);
    sim_files_free(100000);
    CHECK(RUN(psp2dc_file_write("save/one.dat", a + 5, 50000)) == PSP2DC_FILE_NO_ROOM && disk_is("save/one.dat", 7, sizeof(a)));
    sim_files_free(-1);
    /* the Memory Stick failing: the file as it was, nothing of the PSP's left */
    sim_files_fail(1);
    CHECK(RUN(psp2dc_file_write("save/one.dat", a + 5, 3000)) == PSP2DC_FILE_FAILED);
    sim_files_fail(0);
    CHECK(disk_is("save/one.dat", 7, sizeof(a)) && !temps_left());
    /* deleted */
    CHECK(RUN(psp2dc_file_delete("empty")) == PSP2DC_FILE_DONE && disk_size("empty") < 0);
    CHECK(RUN(psp2dc_file_delete("empty")) == PSP2DC_FILE_NOT_FOUND);
    /* the PSP unplugged in the middle of a write: the file as it was; the PSP's half file gone
     * once it is back */
    CHECK(START(psp2dc_file_write("save/one.dat", a + 5, 60000)) == 0);
    frames(20);
    sim_psp_plug(0);
    CHECK(wait_op() == PSP2DC_FILE_GONE);
    CHECK(disk_is("save/one.dat", 7, sizeof(a)));
    sim_psp_plug(1);
    frames(60);
    CHECK(disk_is("save/one.dat", 7, sizeof(a)) && !temps_left());
    /* cut short between its two renames: the whole new file is in its place once the PSP is back */
    disk_put(".one.dat.done", 99, 777);
    disk_put(".half.dat.part", 98, 50);
    sim_psp_plug(0); frames(5); sim_psp_plug(1);
    frames(10);                                            /* (the library hears that the PSP is back) */
    CHECK(RUN(psp2dc_file_size("one.dat")) == PSP2DC_FILE_DONE && op_n == 777);
    CHECK(disk_is("one.dat", 99, 777) && !temps_left());
    /* reads with each window, 64 KB */
    for (w = 1; w <= 3; w++) {
        psp2dc_file_window(w * 1024);
        memset(b, 0, sizeof(b));
        CHECK(RUN(psp2dc_file_read("save/one.dat", 0, b, 65536)) == PSP2DC_FILE_DONE && op_n == 65536);
        CHECK(!memcmp(a, b, 65536));
        printf("    64 KB read with a %d KB window: %u frames (%u KB a second)\n", w, op_frames, 64 * 60 / op_frames);
    }
    psp2dc_file_window(3 * 1024);
}

/* Files through everything at once. A file must be, whenever it is looked at, wholly what it
 * was or wholly what was last written; a read gives its bytes or a status, never wrong bytes. */
#define NAMES 5
#define CANDS 8
static const char *fnames[NAMES] = { "a.sav", "b.dat", "levels/one", "levels/two", "x" };
typedef struct { int exists; uint32_t seed, size; } fstate_t;
static struct { fstate_t c[CANDS]; int n; } fm[NAMES];   /* what each file may be (more than one: an operation's fate unknown) */
static void fm_is(int k, int exists, uint32_t seed, uint32_t size)
{
    fm[k].n = 1; fm[k].c[0].exists = exists; fm[k].c[0].seed = seed; fm[k].c[0].size = size;
}
static void fm_may(int k, int exists, uint32_t seed, uint32_t size)
{
    if (fm[k].n == CANDS) { memmove(fm[k].c, fm[k].c + 1, sizeof(fm[k].c[0]) * (CANDS - 1)); fm[k].n--; }   /* (never this many) */
    fm[k].c[fm[k].n].exists = exists; fm[k].c[fm[k].n].seed = seed; fm[k].c[fm[k].n].size = size;
    fm[k].n++;
}
static void fm_check_disk(void)            /* each file on the Memory Stick is one of what it may be */
{
    int k, i;
    for (k = 0; k < NAMES; k++) {
        long long n = disk_size(fnames[k]);
        for (i = 0; i < fm[k].n; i++) {
            const fstate_t *c = &fm[k].c[i];
            if (n < 0 ? !c->exists : c->exists && disk_is(fnames[k], c->seed, c->size)) break;
        }
        if (i == fm[k].n) { printf("    %s is none of what it may be (%d)\n", fnames[k], fm[k].n); CHECK(0); continue; }
        fm[k].c[0] = fm[k].c[i];
        fm[k].n = 1;
    }
}
static unsigned f_ok, f_gone, f_other, f_reads;
static void files_op(int quiet_link)
{
    static uint8_t wbuf[40000], rbuf[40000];
    int k = rand() % NAMES, what = rand() % 10, st, i, j;
    uint32_t seed = (uint32_t)rand(), size = (uint32_t)(rand() % 3 ? rand() % 4000 : rand() % sizeof(wbuf));
    if (what < 4) {                         /* a write */
        bytes_of(wbuf, seed, size);
        if (START(psp2dc_file_write(fnames[k], wbuf, size)) != 0) return;
        st = wait_op();
        if (st == PSP2DC_FILE_DONE) fm_is(k, 1, seed, size);
        else if (st == PSP2DC_FILE_GONE) fm_may(k, 1, seed, size);
    } else if (what < 8) {                  /* a read, from somewhere */
        uint32_t off = (uint32_t)(rand() % 3 ? 0 : rand() % 5000), len = (uint32_t)(rand() % sizeof(rbuf)), x;
        if (START(psp2dc_file_read(fnames[k], off, rbuf, len)) != 0) return;
        st = wait_op();
        f_reads++;
        if (st == PSP2DC_FILE_DONE || st == PSP2DC_FILE_NOT_FOUND) {
            /* what the file may be, of what the read saw: its bytes, or that it is not there */
            for (i = j = 0; i < fm[k].n; i++) {
                const fstate_t *c = &fm[k].c[i];
                int ok = st == PSP2DC_FILE_NOT_FOUND ? !c->exists : c->exists;
                if (ok && st == PSP2DC_FILE_DONE) {
                    uint32_t want = off >= c->size ? 0 : c->size - off < len ? c->size - off : len;
                    ok = op_n == want;
                    for (x = 0; ok && x < op_n; x++) ok = rbuf[x] == byte_of(c->seed, off + x);
                }
                if (ok) fm[k].c[j++] = *c;
            }
            if (!j) { printf("    a read of %s saw what it never was (status %d)\n", fnames[k], st); CHECK(0); fm_is(k, 0, 0, 0); }
            else fm[k].n = j;
        }
    } else {                                /* deleted */
        if (START(psp2dc_file_delete(fnames[k])) != 0) return;
        st = wait_op();
        if (st == PSP2DC_FILE_DONE || st == PSP2DC_FILE_NOT_FOUND) fm_is(k, 0, 0, 0);
        else if (st == PSP2DC_FILE_GONE) fm_may(k, 0, 0, 0);
    }
    if (st == PSP2DC_FILE_DONE || st == PSP2DC_FILE_NOT_FOUND) f_ok++;
    else if (st == PSP2DC_FILE_GONE) f_gone++;
    else f_other++;
    if (quiet_link && st == PSP2DC_FILE_GONE) { printf("    an operation lost with only commands and replies lost\n"); CHECK(0); }
}
static void files_faults(void)
{
    int round, i;
    files_fresh();
    sim_psp_plug(1);
    psp2dc_init();
    psp2dc_files("testgame");
    for (round = 0; round < NAMES; round++) fm_is(round, 0, 0, 0);
    /* commands and replies lost, and nothing else: no operation may fail for it */
    lose_request = lose_reply = 50;
    for (i = 0; i < 400; i++) files_op(1);
    lose_request = lose_reply = 0;
    fm_check_disk();
    printf("    commands and replies lost (5 %% each): %u operations, %u failed\n", f_ok + f_gone + f_other, f_gone + f_other);
    /* everything at once */
    for (round = 0; round < 30 && !failures; round++) {
        lose_request = lose_reply = 20;
        for (i = 0; i < 60; i++) {
            if (rand() % 6 == 0) usb_every = 1 + rand() % 3;
            if (rand() % 10 == 0) sim_psp_up_budget(64 + rand() % 1200);
            if (rand() % 12 == 0) {         /* the PSP away in the middle of an operation */
                int k = rand() % NAMES;
                uint32_t seed = (uint32_t)rand(), size = 20000 + rand() % 20000;
                static uint8_t buf[40000];
                bytes_of(buf, seed, size);
                if (START(psp2dc_file_write(fnames[k], buf, size)) == 0) {
                    frames(rand() % 30);
                    if (rand() % 2) { sim_psp_plug(0); frames(rand() % 100); sim_psp_plug(1); }
                    else sim_usb_fail();
                    int st = wait_op();
                    if (st == PSP2DC_FILE_DONE) fm_is(k, 1, seed, size);
                    else if (st == PSP2DC_FILE_GONE) fm_may(k, 1, seed, size);
                }
            }
            if (rand() % 25 == 0) { card_there = 0; frames(10 + rand() % 100); card_there = 1; }
            files_op(0);
        }
        lose_request = lose_reply = 0; usb_every = 1; sim_psp_up_budget(1024);
        frames(60);
        fm_check_disk();
    }
    printf("    %u operations: %u ended well, %u lost with the PSP (started again), %u otherwise; %u reads\n",
           f_ok + f_gone + f_other, f_ok, f_gone, f_other, f_reads);
    printf("    %u vblanks, %u + %u lost\n", vblanks, lost_requests, lost_replies);
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
    { "focus",      "the PSP's buttons a program takes: sent, sent again, never SELECT", focus_test },
    { "leftovers",  "a program started on a PSP another has used: its rows and buttons gone", leftovers },
    { "files",      "each operation, names refused, the limits, a write cut short", files },
    { "files-faults", "files through everything at once: whole or as they were", files_faults },
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
