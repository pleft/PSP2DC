/* See comp_files.h. */
#include <stdio.h>
#include <string.h>
#include "../../protocol/psp2dc_protocol.h"
#include "comp_files.h"
#include "comp_fio.h"

CompFilesStats g_comp_files;
static int (*send_rec)(const unsigned char *rec, int len);

/* the records, from the reader thread to the worker: type (1), length (2), data */
#define RING 65536u                       /* a power of two; more than a write's window and a few reads */
#define MARK_RESET 0xFF                   /* (not a record's type: a session begins) */
static unsigned char ring[RING];
static volatile unsigned ring_put, ring_got;
static volatile int reset_lost;           /* a session began while the ring was full */

#define SLACK (64 * 1024)                 /* left free on the Memory Stick */
#define ACK_EVERY 4096
static char folder[PSP2DC_FOLDER_MAX + 1];   /* this session's ("": none yet) */
static long long used;                    /* the folder's bytes */
static struct {
    int op, fd;                           /* fd < 0: no write going */
    unsigned size, got, acked, crc;
    long long old;                        /* the size of the file it replaces (0: none) */
    char name[PSP2DC_FILES_NAME_MAX + 1];
} w = { 0, -1 };
static struct {
    int op, fd;                           /* the file a read has open */
    long long size;
    char name[PSP2DC_FILES_NAME_MAX + 1];
} r = { 0, -1 };
static char listing[16384];               /* a LIST's text, made at its first piece */
static int listing_len, listing_op = -1;

static unsigned crc_table[256];
static unsigned crc32_more(unsigned crc, const unsigned char *p, int n)   /* crc: ~0 at the start */
{
    while (n-- > 0) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc;
}

void comp_files_init(int (*send)(const unsigned char *rec, int len))
{
    unsigned i, k, c;
    for (i = 0; i < 256; i++) {
        for (c = i, k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    send_rec = send;
}

void comp_files_record(int type, const unsigned char *d, int len)
{
    unsigned p = ring_put, i;
    if (len < 0 || len > PSP2DC_COMP_DATA_MAX) return;
    if (RING - (p - ring_got) < 3u + (unsigned)len) {
        if (type == MARK_RESET) reset_lost = 1;
        else g_comp_files.dropped++;      /* (its operation fails: an offset is missing, or no answer comes) */
        return;
    }
    ring[p & (RING - 1)] = (unsigned char)type;
    ring[(p + 1) & (RING - 1)] = (unsigned char)len;
    ring[(p + 2) & (RING - 1)] = (unsigned char)(len >> 8);
    for (i = 0; i < (unsigned)len; i++) ring[(p + 3 + i) & (RING - 1)] = d[i];
    ring_put = p + 3 + (unsigned)len;
}
void comp_files_reset(void) { comp_files_record(MARK_RESET, NULL, 0); }

/* ---- names and paths ------------------------------------------------------- */
static int name_ok(const char *s, int is_folder)
{
    int n = (int)strlen(s), i, start = 1;
    if (n < 1 || n > (is_folder ? PSP2DC_FOLDER_MAX : PSP2DC_FILES_NAME_MAX)) return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '/') {
            if (is_folder || start) return 0;
            start = 1;
            continue;
        }
        if (start && c == '.') return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || (c == '.' && !is_folder)))
            return 0;
        start = 0;
    }
    return !start;
}
/* a record's name (n bytes, not ended) as a string; "" when too long */
static void name_in(char *dst, const unsigned char *src, int n)
{
    if (n < 0 || n > PSP2DC_FILES_NAME_MAX) n = 0;
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void path_of(char *out, int max, const char *name)
{
    snprintf(out, max, "%s/%s/%s", fio_root(), folder, name);
}
/* the PSP's own name beside it, for a write on its way: ".<name>.part", then ".<name>.done" */
static void temp_of(char *out, int max, const char *name, const char *ext)
{
    const char *base = strrchr(name, '/');
    int dir = base ? (int)(base + 1 - name) : 0;
    snprintf(out, max, "%s/%s/%.*s.%s.%s", fio_root(), folder, dir, name, name + dir, ext);
}
static void dirs_for(const char *name)     /* the subfolders a name has, made */
{
    char p[256];
    const char *s;
    for (s = strchr(name, '/'); s; s = strchr(s + 1, '/')) {
        char sub[PSP2DC_FILES_NAME_MAX + 1];
        memcpy(sub, name, s - name);
        sub[s - name] = 0;
        path_of(p, sizeof(p), sub);
        fio_mkdir(p);
    }
}

/* ---- answers --------------------------------------------------------------- */
static void put32(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static void done(int op, int status, unsigned value, unsigned total)
{
    unsigned char rec[3 + sizeof(Psp2dcCompDone)];
    Psp2dcCompDone d;
    d.op = (unsigned char)op; d.status = (unsigned char)status; d.value = value; d.total = total;
    rec[0] = PSP2DC_REC_DONE; rec[1] = sizeof(d); rec[2] = 0;
    memcpy(rec + 3, &d, sizeof(d));
    send_rec(rec, sizeof(rec));
    g_comp_files.ops++;
    if (status != PSP2DC_FS_OK) g_comp_files.failed++;
}
static void ack(void)
{
    unsigned char rec[8];
    rec[0] = PSP2DC_REC_ACK; rec[1] = 5; rec[2] = 0;
    rec[3] = (unsigned char)w.op;
    put32(rec + 4, w.got);
    send_rec(rec, sizeof(rec));
    w.acked = w.got;
}
/* bytes of a READ or a LIST, in RDATA records */
static void rdata(int op, unsigned offset, const unsigned char *p, int n)
{
    unsigned char rec[3 + 5 + PSP2DC_FILES_RDATA];
    while (n > 0) {
        int k = n > PSP2DC_FILES_RDATA ? PSP2DC_FILES_RDATA : n;
        rec[0] = PSP2DC_REC_RDATA; rec[1] = (unsigned char)(5 + k); rec[2] = 0;
        rec[3] = (unsigned char)op;
        put32(rec + 4, offset);
        memcpy(rec + 8, p, k);
        send_rec(rec, 8 + k);
        offset += k; p += k; n -= k;
    }
}

/* ---- the work -------------------------------------------------------------- */
static void write_drop(void)               /* the write going, given up: its file gone */
{
    char p[256];
    if (w.fd < 0) return;
    fio_close(w.fd);
    w.fd = -1;
    temp_of(p, sizeof(p), w.name, "part");
    fio_remove(p);
}
static void write_fail(int status)
{
    int op = w.op;
    write_drop();
    done(op, status, 0, 0);
}
static void read_close(void)
{
    if (r.fd >= 0) fio_close(r.fd);
    r.fd = -1;
    r.op = 0;
}
static void session_start(void)
{
    write_drop();
    read_close();
    folder[0] = 0;
    listing_op = -1;
}

/* A walk through the folder: writes that were cut short are finished or thrown away (a ".done"
 * is whole and checked, a ".part" is not), its bytes counted, and, for a LIST, a line a file. */
static char pool[32768];                   /* the names of the folders being walked, one after another */
static int pool_n;
static void collect(void *ctx, const char *name, int is_dir, long long size)
{
    int n = (int)strlen(name), i;
    (void)ctx;
    if (pool_n + 10 + n + 1 > (int)sizeof(pool)) return;
    pool[pool_n] = (char)is_dir;
    memcpy(pool + pool_n + 1, &size, 8);
    for (i = 0; i <= n; i++)               /* (the Memory Stick gives short names in capitals) */
        pool[pool_n + 9 + i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + 32) : name[i];
    pool_n += 9 + n + 1;
}
static void walk(const char *sub, int depth, int list)   /* sub: "" or "a/b/" */
{
    char dir[256], p[256], q[256];
    int start = pool_n, at;
    snprintf(dir, sizeof(dir), "%s/%s/%s", fio_root(), folder, sub);
    if (depth > 8 || fio_list(dir, collect, NULL) < 0) { pool_n = start; return; }
    for (at = start; at < pool_n; ) {
        int is_dir = pool[at];
        long long size;
        const char *name = pool + at + 9;
        int n = (int)strlen(name);
        memcpy(&size, pool + at + 1, 8);
        at += 9 + n + 1;
        if (name[0] == '.') {
            if (is_dir) continue;
            snprintf(p, sizeof(p), "%s%s", dir, name);
            if (n > 6 && !strcmp(name + n - 5, ".part")) {
                fio_remove(p);
            } else if (n > 6 && !strcmp(name + n - 5, ".done")) {   /* checked, and not yet in its place */
                snprintf(q, sizeof(q), "%s%.*s", dir, n - 6, name + 1);
                fio_remove(q);
                if (fio_rename(p, q) == 0) used += size;
            }
            continue;
        }
        if (is_dir) {
            char deeper[PSP2DC_FILES_NAME_MAX + 2];
            if ((int)strlen(sub) + n + 1 > PSP2DC_FILES_NAME_MAX) continue;
            snprintf(deeper, sizeof(deeper), "%s%s/", sub, name);
            walk(deeper, depth + 1, list);
            continue;
        }
        used += size;
        if (list) {
            int k = snprintf(listing + listing_len, sizeof(listing) - listing_len, "%s%s %lld\n", sub, name, size);
            if (k > 0 && listing_len + k < (int)sizeof(listing)) listing_len += k;
            else listing[listing_len] = 0;  /* (no room: the listing ends at the last whole line) */
        }
    }
    pool_n = start;
}

static void folder_open(const char *name)
{
    char p[256];
    session_start();
    if (!name_ok(name, 1)) return;
    fio_mkdir(fio_root());
    strcpy(folder, name);
    snprintf(p, sizeof(p), "%s/%s", fio_root(), folder);
    fio_mkdir(p);
    used = 0;
    walk("", 0, 0);
}

static void write_begin(int op, unsigned size, const char *name)
{
    char p[256];
    long long free_b;
    w.op = op;
    if (!name_ok(name, 0)) { done(op, PSP2DC_FS_BAD_NAME, 0, 0); return; }
    path_of(p, sizeof(p), name);
    w.old = fio_size(p);
    if (w.old < 0) w.old = 0;
    free_b = fio_free();
    if (size > PSP2DC_FILES_MAX || used - w.old + size > PSP2DC_FILES_MAX ||
        (free_b >= 0 && (long long)size + SLACK > free_b)) {
        done(op, PSP2DC_FS_NO_ROOM, 0, 0);
        return;
    }
    dirs_for(name);
    strcpy(w.name, name);
    temp_of(p, sizeof(p), name, "part");
    if ((w.fd = fio_open_write(p)) < 0) { done(op, PSP2DC_FS_IO, 0, 0); return; }
    w.size = size; w.got = w.acked = 0; w.crc = ~0u;
}
static void write_data(int op, unsigned offset, const unsigned char *d, int n)
{
    if (op != w.op || w.fd < 0) return;    /* (a write given up: its DONE went) */
    if (offset != w.got || w.got + n > w.size) { write_fail(PSP2DC_FS_IO); return; }
    if (fio_write(w.fd, d, n) != n) { write_fail(PSP2DC_FS_IO); return; }
    w.crc = crc32_more(w.crc, d, n);
    w.got += n;
    g_comp_files.written += n;
    if (w.got - w.acked >= ACK_EVERY) ack();
}
static void write_end(int op, unsigned crc)
{
    char part[256], whole[256], p[256];
    if (op != w.op || w.fd < 0) return;
    if (w.got != w.size || ~w.crc != crc) { write_fail(PSP2DC_FS_IO); return; }
    fio_close(w.fd);
    w.fd = -1;
    temp_of(part, sizeof(part), w.name, "part");
    temp_of(whole, sizeof(whole), w.name, "done");
    path_of(p, sizeof(p), w.name);
    fio_remove(whole);
    if (fio_rename(part, whole) < 0) { fio_remove(part); done(op, PSP2DC_FS_IO, 0, 0); return; }
    /* From here the file is whole on the Memory Stick: if what follows is cut short, the next
     * FOLDER puts it in its place (walk). */
    if (r.fd >= 0 && !strcmp(r.name, w.name)) read_close();
    fio_remove(p);
    if (fio_rename(whole, p) < 0) { done(op, PSP2DC_FS_IO, 0, 0); return; }
    used += (long long)w.size - w.old;
    if (w.acked != w.got) ack();
    done(op, PSP2DC_FS_OK, w.size, w.size);
}

static void read_piece(int op, unsigned offset, int len, const char *name)
{
    static unsigned char buf[PSP2DC_FILES_PIECE];
    char p[256];
    int n;
    if (!name_ok(name, 0)) { done(op, PSP2DC_FS_BAD_NAME, 0, 0); return; }
    if (r.fd < 0 || r.op != op || strcmp(r.name, name)) {
        read_close();
        path_of(p, sizeof(p), name);
        if ((r.size = fio_size(p)) < 0 || (r.fd = fio_open_read(p)) < 0) {
            r.fd = -1;
            done(op, PSP2DC_FS_NOT_FOUND, 0, 0);
            return;
        }
        r.op = op;
        strcpy(r.name, name);
    }
    if (len > PSP2DC_FILES_PIECE) len = PSP2DC_FILES_PIECE;
    n = offset >= r.size ? 0 : fio_read_at(r.fd, offset, buf, len);
    if (n < 0) { read_close(); done(op, PSP2DC_FS_IO, 0, 0); return; }
    rdata(op, offset, buf, n);
    g_comp_files.read += n;
    done(op, PSP2DC_FS_OK, (unsigned)n, (unsigned)r.size);
}
static void list_piece(int op, unsigned offset, int len)
{
    if (listing_op != op) {
        listing_len = 0;
        listing[0] = 0;
        used = 0;                          /* (counted again on the way) */
        walk("", 0, 1);
        listing_op = op;
    }
    if (len > PSP2DC_FILES_PIECE) len = PSP2DC_FILES_PIECE;
    if (offset > (unsigned)listing_len) offset = listing_len;
    if (len > listing_len - (int)offset) len = listing_len - (int)offset;
    rdata(op, offset, (const unsigned char *)listing + offset, len);
    done(op, PSP2DC_FS_OK, (unsigned)len, (unsigned)listing_len);
}
static void stat_of(int op, const char *name)
{
    char p[256];
    long long s;
    if (!name[0]) {                        /* what the folder may still take */
        long long left = PSP2DC_FILES_MAX - used, free_b = fio_free();
        if (left < 0) left = 0;
        if (free_b >= 0 && free_b - SLACK < left) left = free_b > SLACK ? free_b - SLACK : 0;
        done(op, PSP2DC_FS_OK, (unsigned)left, (unsigned)used);
        return;
    }
    if (!name_ok(name, 0)) { done(op, PSP2DC_FS_BAD_NAME, 0, 0); return; }
    path_of(p, sizeof(p), name);
    s = fio_size(p);
    if (s < 0) done(op, PSP2DC_FS_NOT_FOUND, 0, 0);
    else done(op, PSP2DC_FS_OK, (unsigned)s, (unsigned)s);
}
static void delete_of(int op, const char *name)
{
    char p[256];
    long long s;
    if (!name_ok(name, 0)) { done(op, PSP2DC_FS_BAD_NAME, 0, 0); return; }
    path_of(p, sizeof(p), name);
    if ((s = fio_size(p)) < 0) { done(op, PSP2DC_FS_NOT_FOUND, 0, 0); return; }
    if (r.fd >= 0 && !strcmp(r.name, name)) read_close();
    if (fio_remove(p) < 0) { done(op, PSP2DC_FS_IO, 0, 0); return; }
    used -= s;
    done(op, PSP2DC_FS_OK, 0, 0);
}

static void handle(int type, const unsigned char *d, int len)
{
    char name[PSP2DC_FILES_NAME_MAX + 1];
    int op;
    if (type == MARK_RESET) { session_start(); return; }
    if (type == PSP2DC_REC_FOLDER) {
        if (len > PSP2DC_FOLDER_MAX) len = 0;
        name_in(name, d, len);
        folder_open(name);
        return;
    }
    if (len < 1) return;
    op = d[0];
    if (w.fd >= 0 && op != w.op) write_drop();   /* another operation: the program gave that one up */
    if (r.fd >= 0 && op != r.op) read_close();
    if (!folder[0] && type != PSP2DC_REC_WDATA && type != PSP2DC_REC_WEND) {
        done(op, PSP2DC_FS_NO_FOLDER, 0, 0);
        return;
    }
    switch (type) {
    case PSP2DC_REC_WRITE:
        if (len < 5) return;
        write_drop();
        name_in(name, d + 5, len - 5);
        write_begin(op, rd32(d + 1), name);
        break;
    case PSP2DC_REC_WDATA:
        if (len >= 5) write_data(op, rd32(d + 1), d + 5, len - 5);
        break;
    case PSP2DC_REC_WEND:
        if (len >= 5) write_end(op, rd32(d + 1));
        break;
    case PSP2DC_REC_READ:
        if (len < 7) return;
        name_in(name, d + 7, len - 7);
        read_piece(op, rd32(d + 1), d[5] | d[6] << 8, name);
        break;
    case PSP2DC_REC_LIST:
        if (len >= 7) list_piece(op, rd32(d + 1), d[5] | d[6] << 8);
        break;
    case PSP2DC_REC_STAT:
        name_in(name, d + 1, len - 1);
        stat_of(op, name);
        break;
    case PSP2DC_REC_DELETE:
        name_in(name, d + 1, len - 1);
        delete_of(op, name);
        break;
    }
}

int comp_files_work(void)
{
    static unsigned char d[PSP2DC_COMP_DATA_MAX];
    int did = 0;
    if (reset_lost) { reset_lost = 0; session_start(); did = 1; }
    while (ring_got != ring_put) {
        unsigned g = ring_got, i;
        int type = ring[g & (RING - 1)];
        int len = ring[(g + 1) & (RING - 1)] | ring[(g + 2) & (RING - 1)] << 8;
        for (i = 0; i < (unsigned)len; i++) d[i] = ring[(g + 3 + i) & (RING - 1)];
        ring_got = g + 3 + (unsigned)len;
        handle(type, d, len);
        did = 1;
    }
    if (w.fd >= 0 && w.acked != w.got) ack();    /* what this round wrote */
    return did;
}
