/* See psp2dc.h. */
#include <string.h>
#include <kos.h>
#include <dc/maple.h>
#include "psp2dc.h"
#include "psp2dc_protocol.h"

static maple_frame_t frame;              /* the library's own: the VMU driver's is left alone */
static uint32_t send_buf[2 + PSP2DC_COMP_DATA_MAX / 4] __attribute__((aligned(32)));

static volatile int port = -1, unit;     /* where the companion is (port < 0: not found) */
static int hello_port, hello_unit;       /* where the HELLO that is out went */
static int look_next;                    /* the memory card unit asked next, while looking */
static volatile int busy;                /* a command is out */
static volatile int found, psp, version, session = -1;
static int epoch;                        /* +1 each time records that were queued are dropped */
static volatile int lost;                /* commands in a row with no good answer */
static volatile int failed;              /* the command that is out got none: its records go again */
static uint8_t seq;                      /* the last SEND's (1 to 255) */

/* records waiting to go (whole ones only) */
static uint8_t out[4096];
static int out_len;
static int sent_len;                     /* of them, in the command that is out */

/* bytes from the PSP: the reply handler puts (an interrupt), psp2dc_poll takes */
#define IN_SIZE 2048
static uint8_t in[IN_SIZE];
static volatile uint32_t in_put, in_got;

static psp2dc_stats_t stats;

/* The screen as the program made it, kept here so that it can be sent again from the start
 * whenever the PSP starts over: the program then only redraws its regions. */
#define D_ALL    1                       /* a widget's dirt: all of it is to go, */
#define D_VALUE  2                       /* its value, */
#define D_TEXT   4                       /* its text, */
#define D_REMOVE 8                       /* its removal */
typedef struct {
    uint8_t used, dirty;
    Psp2dcCompWidget w;
    char text[PSP2DC_WIDGET_TEXT_MAX + 1];
} widget_t;
static widget_t widgets[PSP2DC_WIDGETS];
static Psp2dcCompRect regions[PSP2DC_REGIONS];   /* w 0: none */
static uint16_t palettes[PSP2DC_REGIONS][256];   /* what the program set of each region's palette, */
static int palette_n[PSP2DC_REGIONS];            /* up to this entry (0: nothing) */
static struct {
    const uint8_t *pixels;               /* the program's own: it must stay where it is (NULL: no sprite) */
    uint16_t w, h, stride;
    uint8_t format;
    int sent;                            /* rows the PSP has been sent this session */
} sprites[PSP2DC_SPRITES];
#define RESERVE 1024                     /* room pictures leave in the queue, for widgets and what is small */
static int told = -1;                    /* the session the PSP's screen was started for: psp2dc_session() */
static int reset_due;                    /* psp2dc_screen_reset(): the RESET is still to go */
static int quiet;                        /* frames with nothing sent */
static psp2dc_caps_t caps;
static int caps_have;
static uint32_t in_seen;                 /* how far psp2dc_update has looked for the PSP's CAPS */

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
        if (port >= 0) {                         /* (while looking, a memory card that is no companion says so) */
            stats.errors++;
            lost++;
            failed = 1;
        }
        busy = 0;
        return;
    }
    lost = failed = 0;
    stats.commands++;
    if (r->session != session) {                 /* the PSP started over: what it sent before is gone */
        in_got = in_put;
        session = r->session;
    }
    if (op == PSP2DC_COMP_OP_HELLO) {
        const Psp2dcCompHello *h = (const Psp2dcCompHello *)data;
        if (r->len >= sizeof(*h) && !memcmp(h->magic, "PSP2DC", 6)) {
            version = h->version;
            unit = hello_unit;
            port = hello_port;
            found = 1;
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
    /* A SEND's seq moves on with each new one, so the adapter can tell one sent again (its reply
     * was lost) and take its data once. Only SENDs count: a seq that also moved with every poll
     * would come round to the last SEND's after 255 commands, and that SEND would be dropped. */
    if (op == PSP2DC_COMP_OP_SEND && !again) seq = seq % 255 + 1;
    req->op = op; req->seq = op == PSP2DC_COMP_OP_SEND ? seq : 0; req->len = len;
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

/* While looking: HELLO to the next memory card unit. Its reply says whether it is the companion. */
static void ask_next(void)
{
    maple_device_t *dev = maple_enum_type(look_next, MAPLE_FUNC_MEMCARD);
    if (!dev) {
        look_next = 0;
        if (!(dev = maple_enum_type(0, MAPLE_FUNC_MEMCARD))) return;
    }
    look_next++;
    hello_port = dev->port;
    hello_unit = dev->unit;
    seq = 0;
    command(dev->port, dev->unit, PSP2DC_COMP_OP_HELLO, NULL, 0, 0);
}

int psp2dc_init(void)
{
    int n, i;
    frame.state = MAPLE_FRAME_VACANT;
    port = -1;
    found = psp = busy = 0;
    out_len = sent_len = 0;
    for (n = 0; maple_enum_type(n, MAPLE_FUNC_MEMCARD); n++) {   /* every memory card unit once, each waited for */
        look_next = n;
        ask_next();
        for (i = 0; i < 25 && busy; i++) thd_sleep(20);
        if (port >= 0) return 0;
    }
    return -1;
}

static void caps_look(void);
static int send_max = PSP2DC_COMP_DATA_MAX;   /* a command's data, at most: psp2dc_send_max() */
static int palette_send(int id, int first, int n);
static int sprite_send(int id);
static void caps_take(const uint8_t *d, int len);
static void screen_flush(void);

void psp2dc_update(void)
{
    static int frames;
    frames++;
    caps_look();
    screen_flush();
    if (out_len) quiet = 0;
    else if (psp2dc_psp() && ++quiet >= 60) {    /* nothing for a second: a word, so the PSP keeps the screen up */
        psp2dc_record(PSP2DC_REC_NOP, NULL, 0);
        quiet = 0;
    }
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
    if (port >= 0 && lost > 30) {                /* gone: look again */
        port = -1; found = psp = 0; lost = failed = 0;
        out_len = 0; epoch++;                    /* (what was queued, and maybe what was last sent) */
    }
    if (port < 0) {
        if (frames % 60 == 0) ask_next();        /* a memory card unit a second, in turn; not waited for */
        return;
    }
    if (out_len && psp) {
        /* whole records, as many as fit a command */
        int n = 0;
        while (n < out_len) {
            int rec = 3 + (out[n + 1] | out[n + 2] << 8);
            if (n && n + rec > send_max) break;  /* (a record longer than that goes alone) */
            n += rec;
        }
        sent_len = n;
        if (command(port, unit, PSP2DC_COMP_OP_SEND, out, n, 0) < 0) { sent_len = 0; lost++; }
    } else {
        if (!psp && out_len) { out_len = 0; epoch++; }
        if (command(port, unit, PSP2DC_COMP_OP_POLL, NULL, 0, 0) < 0) lost++;
    }
}

void psp2dc_send_max(int bytes) { send_max = bytes < 1 ? 1 : bytes > PSP2DC_COMP_DATA_MAX ? PSP2DC_COMP_DATA_MAX : bytes; }
int psp2dc_found(void) { return found; }
int psp2dc_psp(void) { return found && psp; }
int psp2dc_version(void) { return version; }
int psp2dc_session(void) { return told; }
void psp2dc_where(int *p, int *u) { *p = port; *u = port < 0 ? 0 : unit; }

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
int psp2dc_idle(void) { return out_len == 0; }   /* (what a command took stays until its reply) */

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
    if (ev->type == PSP2DC_REC_CAPS) {           /* the library's own: psp2dc_get_caps() */
        caps_take(ev->data, len);
        return psp2dc_poll(ev);
    }
    return 1;
}

void psp2dc_stats(psp2dc_stats_t *o) { *o = stats; }

/* ---- the screen ------------------------------------------------------------ */
static int session_now(void) { return epoch << 8 | (session & 0xFF); }
/* the PSP is there and its screen was started for this session: records for it can go */
static int live(void) { return psp2dc_psp() && told == session_now(); }

static void caps_take(const uint8_t *d, int len)
{
    Psp2dcCompCaps c;
    if (len < 20) return;                        /* (level 2's: without the last fields) */
    memset(&c, 0, sizeof(c));
    memcpy(&c, d, len < (int)sizeof(c) ? len : (int)sizeof(c));
    caps.level = c.level; caps.widgets = c.widgets; caps.regions = c.regions; caps.text_max = c.text_max;
    caps.width = c.width; caps.height = c.height; caps.region_pixels = c.region_pixels;
    caps.widget_types = c.widget_types; caps.formats = c.formats;
    caps.sprites = c.sprites; caps.sprite_bytes = c.sprite_bytes;
    caps_have = 1;
}
/* The PSP's CAPS among what has come, for a program that does not call psp2dc_poll() (which
 * finds it too, and keeps it from the program). */
static void caps_look(void)
{
    uint32_t put = in_put, i;
    if ((int32_t)(in_seen - in_got) < 0 || (int32_t)(put - in_seen) < 0) in_seen = in_got;
    while (put - in_seen >= 3) {
        uint8_t d[sizeof(Psp2dcCompCaps)];
        uint32_t len = in[(in_seen + 1) & (IN_SIZE - 1)] | in[(in_seen + 2) & (IN_SIZE - 1)] << 8;
        if (len > PSP2DC_COMP_EVENT_MAX) { in_seen = put; break; }
        if (put - in_seen < 3 + len) break;
        if (in[in_seen & (IN_SIZE - 1)] == PSP2DC_REC_CAPS) {
            uint32_t n = len < sizeof(d) ? len : sizeof(d);
            for (i = 0; i < n; i++) d[i] = in[(in_seen + 3 + i) & (IN_SIZE - 1)];
            caps_take(d, (int)n);
        }
        in_seen += 3 + len;
    }
}
int psp2dc_get_caps(psp2dc_caps_t *o)
{
    if (!caps_have || !live()) return 0;
    *o = caps;
    return 1;
}

/* a widget's dirt as records; 0 when all of it went */
static int widget_send(int id)
{
    widget_t *w = &widgets[id];
    uint8_t d[sizeof(Psp2dcCompWidget) + PSP2DC_WIDGET_TEXT_MAX];
    int n = (int)strlen(w->text);
    d[0] = (uint8_t)id;
    if (w->dirty & D_REMOVE) {
        if (psp2dc_record(PSP2DC_REC_REMOVE, d, 1) < 0) return -1;
        w->dirty &= ~D_REMOVE;
    }
    if (w->dirty & D_ALL) {
        memcpy(d, &w->w, sizeof(w->w));
        memcpy(d + sizeof(w->w), w->text, n);
        if (psp2dc_record(PSP2DC_REC_WIDGET, d, (int)sizeof(w->w) + n) < 0) return -1;
        w->dirty = 0;
    }
    if (w->dirty & D_VALUE) {
        memcpy(d + 1, &w->w.value, 4);
        if (psp2dc_record(PSP2DC_REC_VALUE, d, 5) < 0) return -1;
        w->dirty &= ~D_VALUE;
    }
    if (w->dirty & D_TEXT) {
        memcpy(d + 1, w->text, n);
        if (psp2dc_record(PSP2DC_REC_TEXT, d, 1 + n) < 0) return -1;
        w->dirty &= ~D_TEXT;
    }
    return 0;
}

static int palette_send(int id, int first, int n)
{
    uint8_t d[2 + 2 * 256];
    d[0] = (uint8_t)id; d[1] = (uint8_t)first;
    memcpy(d + 2, &palettes[id][first], 2 * n);
    return psp2dc_record(PSP2DC_REC_PALETTE, d, 2 + 2 * n);
}
/* rows of a picture as records of `type` (BLIT or SPRITE: its header is `head`, whose two
 * bytes at `at` are the first row of each piece, and its count of rows at `count` when
 * there is one), as many as fit while the queue keeps its reserve. How many rows went. */
static int rows_send(int type, uint8_t *head, int head_len, int at, int count, int y0,
                     const uint8_t *pixels, int stride, int row, int rows)
{
    uint8_t d[PSP2DC_RECORD_MAX];
    int done = 0;
    while (done < rows) {
        int fit = (PSP2DC_RECORD_MAX - head_len) / row, room = (psp2dc_room() - RESERVE - 3 - head_len) / row, n, j;
        n = rows - done < fit ? rows - done : fit;
        if (n > room) n = room;
        if (n <= 0) break;
        head[at] = (uint8_t)(y0 + done); head[at + 1] = (uint8_t)((y0 + done) >> 8);
        if (count >= 0) { head[count] = (uint8_t)n; head[count + 1] = (uint8_t)(n >> 8); }
        memcpy(d, head, head_len);
        for (j = 0; j < n; j++) memcpy(d + head_len + j * row, pixels + (size_t)(done + j) * stride, row);
        if (psp2dc_record(type, d, head_len + n * row) < 0) break;
        done += n;
    }
    return done;
}
/* more of a sprite's rows; 0 when all of it has gone */
static int sprite_send(int id)
{
    Psp2dcCompSprite h;
    int row = PSP2DC_FMT_ROW(sprites[id].format, sprites[id].w);
    h.id = (uint8_t)id; h.format = sprites[id].format; h.w = sprites[id].w; h.h = sprites[id].h; h.y = 0;
    sprites[id].sent += rows_send(PSP2DC_REC_SPRITE, (uint8_t *)&h, sizeof(h), 6, -1, sprites[id].sent,
                                  sprites[id].pixels + (size_t)sprites[id].sent * sprites[id].stride,
                                  sprites[id].stride, row, sprites[id].h - sprites[id].sent);
    return sprites[id].sent == sprites[id].h ? 0 : -1;
}

/* Once a frame: a new session's screen started (the PSP's is empty: RESET, the regions, every
 * widget), then what changed since. */
static void screen_flush(void)
{
    int i;
    if (!psp2dc_psp()) return;
    if (told != session_now()) {
        int need = 16;
        for (i = 0; i < PSP2DC_REGIONS; i++)
            if (regions[i].w) need += 3 + (int)sizeof(Psp2dcCompRect) + 5 + 2 * palette_n[i];
        if (psp2dc_room() < need) return;
        psp2dc_record(PSP2DC_REC_RESET, NULL, 0);
        psp2dc_record(PSP2DC_REC_QUERY, NULL, 0);
        for (i = 0; i < PSP2DC_REGIONS; i++) {
            if (!regions[i].w) continue;
            psp2dc_record(PSP2DC_REC_REGION, &regions[i], sizeof(regions[i]));
            if (palette_n[i]) palette_send(i, 0, palette_n[i]);
        }
        for (i = 0; i < PSP2DC_WIDGETS; i++) widgets[i].dirty = widgets[i].used ? D_ALL : 0;
        for (i = 0; i < PSP2DC_SPRITES; i++) sprites[i].sent = 0;
        reset_due = 0;
        caps_have = 0;
        told = session_now();                    /* (from here the program's own records are in their place) */
    }
    if (reset_due) {
        if (psp2dc_record(PSP2DC_REC_RESET, NULL, 0) < 0) return;
        reset_due = 0;
    }
    for (i = 0; i < PSP2DC_WIDGETS; i++)
        if (widgets[i].dirty && widget_send(i) < 0) return;
    for (i = 0; i < PSP2DC_SPRITES; i++)         /* the sprites' pixels, behind everything else */
        if (sprites[i].pixels && sprites[i].sent < sprites[i].h && sprite_send(i) < 0) return;
}

static widget_t *widget_at(int id) { return id >= 0 && id < PSP2DC_WIDGETS && widgets[id].used ? &widgets[id] : NULL; }
static void text_copy(char *dst, const char *src)
{
    int n = src ? (int)strlen(src) : 0;
    if (n > PSP2DC_WIDGET_TEXT_MAX) n = PSP2DC_WIDGET_TEXT_MAX;
    if (n) memcpy(dst, src, n);
    memset(dst + n, 0, PSP2DC_WIDGET_TEXT_MAX + 1 - n);
}

int psp2dc_widget_create(int id, int type, int flags, int x, int y, int w, int h, const char *text)
{
    widget_t *wd, was;
    if (id < 0 || id >= PSP2DC_WIDGETS || type < 0 || type > PSP2DC_WIDGET_LIST || w < 0 || h < 0) return -1;
    wd = &widgets[id];
    was = *wd;
    if (!wd->used) memset(&wd->w, 0, sizeof(wd->w));   /* (made again: its value, range and colour stay) */
    wd->w.id = (uint8_t)id; wd->w.type = (uint8_t)type;
    wd->w.flags = (uint8_t)((flags & ~PSP2DC_WF_COLOUR) | (wd->w.flags & PSP2DC_WF_COLOUR));
    wd->w.x = (int16_t)x; wd->w.y = (int16_t)y; wd->w.w = (uint16_t)w; wd->w.h = (uint16_t)h;
    if (!wd->used && type == PSP2DC_WIDGET_BAR) wd->w.max = 100;
    if (!wd->used && type == PSP2DC_WIDGET_LIST) wd->w.value = -1;
    text_copy(wd->text, text);
    wd->used = 1;
    if (!was.used || memcmp(&was.w, &wd->w, sizeof(wd->w)) || strcmp(was.text, wd->text))
        wd->dirty = (uint8_t)((wd->dirty & D_REMOVE) | D_ALL);
    return 0;
}
int psp2dc_widget_set_u32(int id, uint32_t value)
{
    widget_t *w = widget_at(id);
    if (!w) return -1;
    if (w->w.value != (int32_t)value) { w->w.value = (int32_t)value; w->dirty |= D_VALUE; }
    return 0;
}
int psp2dc_widget_set_text(int id, const char *text)
{
    widget_t *w = widget_at(id);
    char t[PSP2DC_WIDGET_TEXT_MAX + 1];
    if (!w) return -1;
    text_copy(t, text);
    if (strcmp(t, w->text)) { memcpy(w->text, t, sizeof(t)); w->dirty |= D_TEXT; }
    return 0;
}
int psp2dc_widget_set_range(int id, uint32_t max)
{
    widget_t *w = widget_at(id);
    if (!w) return -1;
    if (w->w.max != (int32_t)max) { w->w.max = (int32_t)max; w->dirty |= D_ALL; }
    return 0;
}
int psp2dc_widget_set_colour(int id, int rgb565)
{
    widget_t *w = widget_at(id);
    uint8_t flags;
    uint16_t colour;
    if (!w) return -1;
    flags = (uint8_t)(rgb565 < 0 ? w->w.flags & ~PSP2DC_WF_COLOUR : w->w.flags | PSP2DC_WF_COLOUR);
    colour = (uint16_t)(rgb565 < 0 ? 0 : rgb565);
    if (flags != w->w.flags || colour != w->w.colour) { w->w.flags = flags; w->w.colour = colour; w->dirty |= D_ALL; }
    return 0;
}
int psp2dc_widget_remove(int id)
{
    widget_t *w = widget_at(id);
    if (!w) return -1;
    w->used = 0;
    w->dirty = D_REMOVE;
    return 0;
}
void psp2dc_screen_reset(void)
{
    memset(widgets, 0, sizeof(widgets));
    memset(regions, 0, sizeof(regions));
    memset(palette_n, 0, sizeof(palette_n));
    memset(sprites, 0, sizeof(sprites));
    reset_due = 1;
}
int psp2dc_notify(const char *text)
{
    int n = (int)strlen(text);
    if (!live()) return -2;
    return psp2dc_record(PSP2DC_REC_NOTIFY, text, n > 63 ? 63 : n);
}

/* a record of a region's, in its place after whatever started the screen */
static int region_record(int type, const void *d, int len)
{
    if (!live()) return -2;
    if (reset_due) {
        if (psp2dc_record(PSP2DC_REC_RESET, NULL, 0) < 0) return -1;
        reset_due = 0;
    }
    return psp2dc_record(type, d, len);
}
static Psp2dcCompRect rect(int id, int x, int y, int w, int h)
{
    Psp2dcCompRect r;
    r.id = (uint8_t)id; r.x = (int16_t)x; r.y = (int16_t)y; r.w = (uint16_t)w; r.h = (uint16_t)h;
    return r;
}
int psp2dc_region_create(int id, int x, int y, int w, int h)
{
    Psp2dcCompRect r = rect(id, x, y, w, h);
    if (id < 0 || id >= PSP2DC_REGIONS || w < 0 || h < 0 || w > PSP2DC_SCREEN_W || h > PSP2DC_SCREEN_H) return -1;
    if (!w || !h) r.w = r.h = 0;
    if (live() && region_record(PSP2DC_REC_REGION, &r, sizeof(r)) < 0) return -1;
    regions[id] = r;                             /* (no PSP now: it goes when one starts) */
    palette_n[id] = 0;                           /* (a region made has the palette it starts with) */
    return 0;
}
int psp2dc_region_remove(int id) { return psp2dc_region_create(id, 0, 0, 0, 0); }
static int region_ok(int id) { return id >= 0 && id < PSP2DC_REGIONS && regions[id].w; }
int psp2dc_draw_rect(int id, int x, int y, int w, int h, int rgb565)
{
    uint8_t d[sizeof(Psp2dcCompRect) + 2];
    Psp2dcCompRect r = rect(id, x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
    if (!region_ok(id)) return -1;
    memcpy(d, &r, sizeof(r));
    d[sizeof(r)] = (uint8_t)rgb565; d[sizeof(r) + 1] = (uint8_t)(rgb565 >> 8);
    return region_record(PSP2DC_REC_FILL, d, sizeof(d));
}
int psp2dc_draw_line(int id, int x0, int y0, int x1, int y1, int rgb565)
{
    uint8_t d[sizeof(Psp2dcCompRect) + 2];
    Psp2dcCompRect r = rect(id, x0, y0, x1, y1);
    if (!region_ok(id)) return -1;
    memcpy(d, &r, sizeof(r));
    d[sizeof(r)] = (uint8_t)rgb565; d[sizeof(r) + 1] = (uint8_t)(rgb565 >> 8);
    return region_record(PSP2DC_REC_LINE, d, sizeof(d));
}
int psp2dc_draw_text(int id, int x, int y, int rgb565, int size, const char *text)
{
    uint8_t d[8 + 96];
    int n = (int)strlen(text);
    if (!region_ok(id)) return -1;
    if (n > 96) n = 96;
    d[0] = (uint8_t)id;
    d[1] = (uint8_t)x; d[2] = (uint8_t)(x >> 8); d[3] = (uint8_t)y; d[4] = (uint8_t)(y >> 8);
    d[5] = (uint8_t)rgb565; d[6] = (uint8_t)(rgb565 >> 8);
    d[7] = (uint8_t)size;
    memcpy(d + 8, text, n);
    return region_record(PSP2DC_REC_DRAWTEXT, d, 8 + n);
}
int psp2dc_region_clip(int id, int x, int y, int w, int h)
{
    Psp2dcCompRect r = rect(id, x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
    if (!region_ok(id)) return -1;
    return region_record(PSP2DC_REC_CLIP, &r, sizeof(r));
}
int psp2dc_present(int id)
{
    uint8_t d = (uint8_t)id;
    if (!region_ok(id)) return -1;
    return region_record(PSP2DC_REC_PRESENT, &d, 1);
}

/* ---- pictures -------------------------------------------------------------- */
int psp2dc_region_palette(int id, int first, int count, const uint16_t *rgb565)
{
    if (!region_ok(id) || first < 0 || count < 1 || first + count > 256) return -1;
    if (first > palette_n[id])                   /* (what lies between is as the PSP starts it) */
        return -1;
    memcpy(&palettes[id][first], rgb565, 2 * count);
    if (live()) {
        if (reset_due || psp2dc_room() < 5 + 2 * count) return -1;
        palette_send(id, first, count);
    }
    if (first + count > palette_n[id]) palette_n[id] = first + count;
    return 0;
}
int psp2dc_draw_bitmap(int id, int x, int y, int w, int h, int format, const void *pixels, int stride)
{
    uint8_t head[sizeof(Psp2dcCompRect) + 1];
    Psp2dcCompRect r = rect(id, x, y, w, 0);
    int row;
    if (!region_ok(id) || w < 1 || h < 1 || format < 0 || format > PSP2DC_FMT_565) return -1;
    row = PSP2DC_FMT_ROW(format, w);
    if (row > PSP2DC_RECORD_MAX - (int)sizeof(head)) return -1;
    if (!live() || reset_due) return live() ? 0 : -2;
    memcpy(head, &r, sizeof(r));
    head[sizeof(r)] = (uint8_t)format;
    /* each piece: its own y (2 bytes at 3 in the rectangle) and its rows (the rectangle's h, at 7) */
    return rows_send(PSP2DC_REC_BLIT, head, sizeof(head), 3, 7, y, pixels, stride, row, h);
}
int psp2dc_sprite_create(int id, int w, int h, int format, const void *pixels, int stride)
{
    if (id < 0 || id >= PSP2DC_SPRITES || w < 1 || h < 1 || w > 1024 || h > 1024 || format < 0 || format > PSP2DC_FMT_565 ||
        !pixels || PSP2DC_FMT_ROW(format, w) > PSP2DC_RECORD_MAX - (int)sizeof(Psp2dcCompSprite)) return -1;
    sprites[id].pixels = pixels;
    sprites[id].w = (uint16_t)w; sprites[id].h = (uint16_t)h; sprites[id].stride = (uint16_t)stride;
    sprites[id].format = (uint8_t)format;
    sprites[id].sent = 0;
    return 0;
}
int psp2dc_sprite_ready(int id)
{
    return id >= 0 && id < PSP2DC_SPRITES && sprites[id].pixels && live() && sprites[id].sent == sprites[id].h;
}
int psp2dc_draw_sprite(int region, int id, int x, int y, int key)
{
    uint8_t d[9];
    if (!region_ok(region) || id < 0 || id >= PSP2DC_SPRITES || !sprites[id].pixels) return -1;
    if (!psp2dc_sprite_ready(id)) return live() ? -1 : -2;
    d[0] = (uint8_t)region; d[1] = (uint8_t)id;
    d[2] = (uint8_t)x; d[3] = (uint8_t)(x >> 8); d[4] = (uint8_t)y; d[5] = (uint8_t)(y >> 8);
    d[6] = key >= 0; d[7] = (uint8_t)key; d[8] = (uint8_t)(key >> 8);
    return region_record(PSP2DC_REC_DRAWSPRITE, d, sizeof(d));
}
