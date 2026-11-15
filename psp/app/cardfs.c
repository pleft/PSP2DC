/* See cardfs.h. The FAT and directory are found where the root block says
 * (dmitry.gr, "VMU Hacking"), when that makes sense. */
#include <stdio.h>
#include <string.h>
#include "cardfs.h"

#define BLK 512
#define USER_BLOCKS 200
#define FAT_FREE 0xFFFC
#define FAT_LAST 0xFFFA
#define GAME_MAX 128                       /* 64 KB, bank 0 (Sega VMU manual) */

static int rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
/* Where the FAT and directory are: the root block's fields (FAT 0x46, its
 * size 0x48, directory 0x4a, its size 0x4c: mc.pp.se, dmitry.gr, libevmu's
 * EvmuRootBlock) when they make sense together, else the usual places
 * (254; 253 down to 241). The adapter's preformat, and cards_format after
 * it, write those fields 4 bytes late; the Dreamcast does not read them. */
static void layout(const unsigned char *c, int *fat_at, int *dir_at, int *dir_len)
{
    const unsigned char *r = c + 255 * BLK;
    int f = rd16(r + 0x46), fl = rd16(r + 0x48), d = rd16(r + 0x4a), dl = rd16(r + 0x4c);
    if (f >= USER_BLOCKS && f < 255 && fl == 1 && d >= USER_BLOCKS && d < f && dl >= 1 && d - dl + 1 >= USER_BLOCKS) {
        *fat_at = f; *dir_at = d; *dir_len = dl;
    } else { *fat_at = 254; *dir_at = 253; *dir_len = 13; }
}
static int fat_blk(const unsigned char *c) { int f, d, l; layout(c, &f, &d, &l); return f; }
static int fat(const unsigned char *c, int b) { return rd16(c + fat_blk(c) * BLK + b * 2); }
static void fat_set(unsigned char *c, int b, int v)
{
    unsigned char *p = c + fat_blk(c) * BLK + b * 2;
    p[0] = v & 255; p[1] = v >> 8;
}
static int formatted(const unsigned char *c)
{
    int i;
    for (i = 0; i < 16; i++) if (c[255 * BLK + i] != 0x55) return 0;
    return 1;
}
/* the directory's entries in the BIOS's order: its last block first */
static int dir_entries(const unsigned char *c, int *first_off)
{
    int f, at, len;
    layout(c, &f, &at, &len);
    *first_off = at * BLK;
    return len * 16;
}
static int entry_off(const unsigned char *c, int k)
{
    int top, n = dir_entries(c, &top);
    return k < n ? top - (k / 16) * BLK + (k % 16) * 32 : -1;
}

int cardfs_list(const unsigned char *card, CardFile *out, int max)
{
    int k, n = 0, off;
    if (!card) return 0;
    for (k = 0; n < max && (off = entry_off(card, k)) >= 0; k++) {
        const unsigned char *e = card + off;
        if (e[0] != 0x33 && e[0] != 0xCC) continue;
        out[n].type = e[0];
        out[n].prot = e[1] == 0xFF;
        out[n].first = rd16(e + 2);
        memcpy(out[n].name, e + 4, 12);
        out[n].name[12] = 0;
        memcpy(out[n].date, e + 0x10, 8);
        out[n].size = rd16(e + 0x18);
        out[n].hdr = rd16(e + 0x1a);
        out[n].off = off;
        n++;
    }
    return n;
}

/* a file's blocks, following the FAT: how many, or -1 when the chain is
 * not the size the directory says */
int cardfs_chain(const unsigned char *c, const CardFile *f, int *blocks)
{
    int b = f->first, n = 0;
    while (n < f->size) {
        if (b < 0 || b >= USER_BLOCKS) return -1;
        blocks[n++] = b;
        b = fat(c, b);
        if (b == FAT_LAST) break;
    }
    return n == f->size && b == FAT_LAST ? n : -1;
}

int cardfs_free(const unsigned char *c)
{
    int b, n = 0;
    for (b = 0; b < USER_BLOCKS; b++) n += fat(c, b) == FAT_FREE;
    return n;
}

/* room for a mini-game: one to a card, from block 0 up, unbroken, at most
 * 128 blocks (Sega VMU manual: "contiguous blocks starting at block 00H",
 * "maximum size ... 64 KB (= 128 blocks)") */
int cardfs_game_room(const unsigned char *c)
{
    int b = 0;
    if (cardfs_has_game(c)) return 0;
    while (b < GAME_MAX && fat(c, b) == FAT_FREE) b++;
    return b;
}

/* the 16-character comment ("Description of file (shown in VMS file
 * menu)", mc.pp.se/dc/vms/fileheader.html), in the file's header block */
void cardfs_comment(const unsigned char *c, const CardFile *f, char out[17])
{
    int blocks[USER_BLOCKS], i;
    memset(out, ' ', 16); out[16] = 0;
    if (cardfs_chain(c, f, blocks) < 0 || f->hdr >= f->size) return;
    for (i = 0; i < 16; i++) {
        unsigned char ch = c[blocks[f->hdr] * BLK + i];
        out[i] = ch >= 32 && ch < 127 ? ch : ' ';
    }
}

/* ---- copy and delete ---- */

/* f from card src into dst, as the BIOS places files: a game from block 0
 * up, data in the highest free blocks (mc.pp.se) */
/* Which save owns each user block (the index in list, -1 none, -2 more
 * than one), from the saves whose chains are whole. */
static void owners(const unsigned char *c, const CardFile *list, int n, int own[USER_BLOCKS])
{
    int blocks[USER_BLOCKS], i, k, m;
    for (i = 0; i < USER_BLOCKS; i++) own[i] = -1;
    for (i = 0; i < n; i++) {
        if ((m = cardfs_chain(c, &list[i], blocks)) < 0) continue;
        for (k = 0; k < m; k++) own[blocks[k]] = own[blocks[k]] == -1 ? i : -2;
    }
}

int cardfs_add(unsigned char *dst, const unsigned char entry[32], const unsigned char *data, int n)
{
    static CardFile there[200];
    int to[USER_BLOCKS], i, k, off, slot = -1, b;
    if (!formatted(dst) || n < 1 || n > USER_BLOCKS) return CARDFS_FAIL;
    k = cardfs_list(dst, there, 200);
    for (i = 0; i < k; i++) if (!memcmp(there[i].name, entry + 4, 12)) return CARDFS_SAME;
    for (k = 0; (off = entry_off(dst, k)) >= 0; k++) if (dst[off] == 0) { slot = off; break; }
    if (slot < 0) return CARDFS_ROOM;
    if (entry[0] == 0xCC) {
        if (cardfs_has_game(dst) || n > GAME_MAX) return CARDFS_ROOM;
        for (i = 0; i < n; i++) { if (fat(dst, i) != FAT_FREE) return CARDFS_ROOM; to[i] = i; }
    } else {
        for (i = 0, b = USER_BLOCKS - 1; i < n && b >= 0; b--) if (fat(dst, b) == FAT_FREE) to[i++] = b;
        if (i < n) return CARDFS_ROOM;
    }
    for (i = 0; i < n; i++) {
        memcpy(dst + to[i] * BLK, data + i * BLK, BLK);
        fat_set(dst, to[i], i < n - 1 ? to[i + 1] : FAT_LAST);
    }
    memcpy(dst + slot, entry, 32);
    dst[slot + 2] = to[0] & 255; dst[slot + 3] = to[0] >> 8;
    return CARDFS_OK;
}

int cardfs_copy(const unsigned char *src, const CardFile *f, unsigned char *dst)
{
    static unsigned char data[USER_BLOCKS * BLK];
    int len;
    if (f->prot || (len = cardfs_export_vms(src, f, data)) < 0) return CARDFS_FAIL;
    return cardfs_add(dst, src + f->off, data, len / BLK);
}

/* f off the card: its entry cleared, and its blocks freed in the FAT, but
 * never one another save also uses (a damaged card can share blocks) */
void cardfs_delete(unsigned char *c, const CardFile *f)
{
    static CardFile all[200];
    int own[USER_BLOCKS], n = cardfs_list(c, all, 200), me = -1, i, b = f->first, steps = 0, next;
    for (i = 0; i < n; i++) if (all[i].off == f->off) me = i;
    owners(c, all, n, own);
    for (i = 0; i < n; i++) {          /* (a broken chain: none of its blocks are marked) */
        int blocks[USER_BLOCKS], m = i == me ? -1 : cardfs_chain(c, &all[i], blocks), k;
        for (k = 0; k < m; k++) own[blocks[k]] = -3;     /* someone else's */
    }
    while (b >= 0 && b < USER_BLOCKS && steps++ < USER_BLOCKS) {
        next = fat(c, b);
        if (next == FAT_FREE || own[b] == -3) break;
        fat_set(c, b, FAT_FREE);
        if (next == FAT_LAST) break;
        b = next;
    }
    memset(c + f->off, 0, 32);
}

int cardfs_has_game(const unsigned char *card)
{
    CardFile f[200];
    int n = cardfs_list(card, f, 200), i;
    for (i = 0; i < n; i++) if (f[i].type == 0xCC) return 1;
    return 0;
}


int cardfs_find(const unsigned char *card, int off, CardFile *out)
{
    static CardFile all[200];
    int n = cardfs_list(card, all, 200), i;
    for (i = 0; i < n; i++) if (all[i].off == off) { *out = all[i]; return 1; }
    return 0;
}

/* ---- exports ---- */

int cardfs_export_vms(const unsigned char *card, const CardFile *f, unsigned char *out)
{
    int blocks[USER_BLOCKS], n = cardfs_chain(card, f, blocks), i;
    if (n < 0) return -1;
    for (i = 0; i < n; i++) memcpy(out + i * BLK, card + blocks[i] * BLK, BLK);
    return n * BLK;
}

int cardfs_export_dci(const unsigned char *card, const CardFile *f, unsigned char *out)
{
    int len = cardfs_export_vms(card, f, out + 32), i;
    if (len < 0) return -1;
    memcpy(out, card + f->off, 32);
    for (i = 0; i < len; i += 4) {
        unsigned char *w = out + 32 + i, t;
        t = w[0]; w[0] = w[3]; w[3] = t;
        t = w[1]; w[1] = w[2]; w[2] = t;
    }
    return 32 + len;
}

static int unbcd(unsigned char v) { return (v >> 4) * 10 + (v & 15); }

void cardfs_vmi(const unsigned char *card, const CardFile *f, const char *res, int vms_bytes,
                unsigned char out[108])
{
    static const char sega[4] = { 'S', 'E', 'G', 'A' };
    int blocks[USER_BLOCKS], i, year;
    memset(out, 0, 108);
    strncpy((char *)out + 0x50, res, 8);
    for (i = 0; i < 4; i++) out[i] = (unsigned char)(out[0x50 + i] & sega[i]);
    /* the description: the save's long one (shown by the Dreamcast's file
     * manager, the header's 0x10), trimmed of its padding */
    if (cardfs_chain(card, f, blocks) >= 0 && f->hdr < f->size) {
        const unsigned char *h = card + blocks[f->hdr] * BLK + 0x10;
        int n = 32;
        while (n && (h[n - 1] == ' ' || !h[n - 1])) n--;
        memcpy(out + 0x04, h, n);
    }
    strncpy((char *)out + 0x24, "PSP2DC", 32);
    year = unbcd(f->date[0]) * 100 + unbcd(f->date[1]);
    out[0x44] = year & 255; out[0x45] = year >> 8;
    out[0x46] = unbcd(f->date[2]); out[0x47] = unbcd(f->date[3]);
    out[0x48] = unbcd(f->date[4]); out[0x49] = unbcd(f->date[5]); out[0x4a] = unbcd(f->date[6]);
    out[0x4b] = (unsigned char)((unbcd(f->date[7]) + 1) % 7);   /* the VMU's 0 is Monday, the VMI's Sunday */
    out[0x4e] = 1;                                              /* file number */
    memcpy(out + 0x58, f->name, 12);
    out[0x64] = (unsigned char)((f->type == 0xCC ? 2 : 0) | (f->prot ? 1 : 0));
    out[0x68] = vms_bytes & 255; out[0x69] = (vms_bytes >> 8) & 255;
    out[0x6a] = (vms_bytes >> 16) & 255; out[0x6b] = (vms_bytes >> 24) & 255;
}

/* ---- what a save's header says, and its large picture ---- */

static unsigned crc16(const unsigned char *p, int n, unsigned crc)
{
    int i, k;
    for (i = 0; i < n; i++) {
        crc ^= (unsigned)p[i] << 8;
        for (k = 0; k < 8; k++) crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
        crc &= 0xFFFF;
    }
    return crc;
}

static int eyecatch_bytes(int type)
{
    return type == 1 ? 72 * 56 * 2 : type == 2 ? 512 + 72 * 56 : type == 3 ? 32 + 72 * 56 / 2 : 0;
}

int cardfs_info(const unsigned char *card, const CardFile *f, CardInfo *out)
{
    static unsigned char data[USER_BLOCKS * BLK];
    const unsigned char *h;
    int len = cardfs_export_vms(card, f, data), i, total;
    memset(out, 0, sizeof(*out));
    out->crc_computed = -1;
    if (len < 0 || f->hdr >= f->size) return 0;
    h = data + f->hdr * BLK;
    for (i = 0; i < 16; i++) out->vms_desc[i] = h[i] >= 32 && h[i] < 127 ? h[i] : ' ';
    for (i = 0; i < 32; i++) out->dc_desc[i] = h[16 + i] >= 32 && h[16 + i] < 127 ? h[16 + i] : ' ';
    for (i = 0; i < 16; i++) out->app[i] = h[48 + i] >= 32 && h[48 + i] < 127 ? h[48 + i] : 0;
    for (i = 15; i >= 0 && (out->vms_desc[i] == ' '); i--) out->vms_desc[i] = 0;
    for (i = 31; i >= 0 && (out->dc_desc[i] == ' '); i--) out->dc_desc[i] = 0;
    out->icons = rd16(h + 0x40);
    out->speed = rd16(h + 0x42);
    out->eyecatch = rd16(h + 0x44);
    out->crc_stored = rd16(h + 0x46);
    if (out->icons > 3) out->icons = 0;
    if (out->eyecatch > 3) out->eyecatch = 0;
    out->data_bytes = (long)(h[0x48] | h[0x49] << 8 | h[0x4a] << 16 | (unsigned long)h[0x4b] << 24);
    /* the CRC (mc.pp.se fileheader.html; libevmu's EvmuVms_computeCrc): the
     * header with the CRC field zeroed, the icons, the picture and the data,
     * not the last block's padding. Games do not use it. */
    total = 0x80 + out->icons * 512 + eyecatch_bytes(out->eyecatch) + (int)out->data_bytes;
    if (f->type == 0x33 && out->data_bytes >= 0 && total <= len - f->hdr * BLK) {
        unsigned crc = crc16(h, 0x46, 0);
        crc = crc16((const unsigned char *)"\0\0", 2, crc);
        out->crc_computed = (int)crc16(h + 0x48, total - 0x48, crc);
    }
    out->header_ok = 1;
    return 1;
}

int cardfs_eyecatch(const unsigned char *card, const CardFile *f, unsigned short out[72 * 56])
{
    static unsigned char data[USER_BLOCKS * BLK];
    CardInfo in;
    const unsigned char *e;
    int len, i;
    if (!cardfs_info(card, f, &in) || !in.eyecatch) return 0;
    len = cardfs_export_vms(card, f, data);
    e = data + f->hdr * BLK + 0x80 + in.icons * 512;
    if (e + eyecatch_bytes(in.eyecatch) > data + len) return 0;
    for (i = 0; i < 72 * 56; i++) {
        int k = in.eyecatch == 1 ? i : in.eyecatch == 2 ? e[512 + i] : (i & 1 ? e[32 + i / 2] & 15 : e[32 + i / 2] >> 4);
        const unsigned char *c = in.eyecatch == 1 ? e + 2 * i : e + 2 * k;
        out[i] = (unsigned short)(c[0] | c[1] << 8);
    }
    return in.eyecatch;
}

/* ---- imports ---- */

static unsigned char bcd(int v) { return (unsigned char)((v / 10) << 4 | v % 10); }

int cardfs_import_dci(unsigned char *card, const unsigned char *dci, int len)
{
    static unsigned char data[USER_BLOCKS * BLK + 4];
    int n, i;
    if (len < 32 + BLK || (dci[0] != 0x33 && dci[0] != 0xCC)) return CARDFS_FAIL;
    n = rd16(dci + 0x18);
    if (n < 1 || n > USER_BLOCKS || len - 32 < n * BLK) return CARDFS_FAIL;
    for (i = 0; i < n * BLK; i += 4) {      /* each word's bytes the other way round again */
        data[i] = dci[32 + i + 3]; data[i + 1] = dci[32 + i + 2];
        data[i + 2] = dci[32 + i + 1]; data[i + 3] = dci[32 + i];
    }
    return cardfs_add(card, dci, data, n);
}

/* a VMS header at h: one to three icons, a picture type, and a description
 * in ASCII or half-width katakana (Japanese games' are), padded with spaces
 * or NULs */
static int header_at(const unsigned char *h)
{
    int i, icons = rd16(h + 0x40);
    if (icons < 1 || icons > 3 || rd16(h + 0x44) > 3) return 0;
    for (i = 0; i < 16; i++)
        if (h[i] && (h[i] < 32 || h[i] > 126) && (h[i] < 0xA1 || h[i] > 0xDF)) return 0;
    return h[0] != 0;
}

/* A save starts with its header; anything else is a game (its block 0 is
 * code, its header, when it has one, in block 1): 115 of 127 games from the
 * web had a header in block 1 an ASCII test passes, 11 more Japanese text,
 * and one homebrew none at all. */
int cardfs_vms_is_game(const unsigned char *vms, int len)
{
    return !(len >= 0x80 && header_at(vms));
}

int cardfs_import_vms(unsigned char *card, const unsigned char *vms, int len, const unsigned char *vmi,
                      const char *name, const unsigned char date[8])
{
    static unsigned char data[USER_BLOCKS * BLK];
    unsigned char e[32];
    int n = (len + BLK - 1) / BLK, game, i;
    if (len < 0x80 || n > USER_BLOCKS) return n > USER_BLOCKS ? CARDFS_ROOM : CARDFS_FAIL;
    memset(e, 0, sizeof(e));
    if (vmi) {
        int mode = rd16(vmi + 0x64), year = rd16(vmi + 0x44);
        game = (mode & 2) != 0;
        e[1] = mode & 1 ? 0xFF : 0;
        for (i = 0; i < 12; i++) e[4 + i] = vmi[0x58 + i] ? vmi[0x58 + i] : ' ';
        e[0x10] = bcd(year / 100); e[0x11] = bcd(year % 100);
        e[0x12] = bcd(vmi[0x46]); e[0x13] = bcd(vmi[0x47]); e[0x14] = bcd(vmi[0x48]);
        e[0x15] = bcd(vmi[0x49]); e[0x16] = bcd(vmi[0x4a]);
        e[0x17] = (unsigned char)((vmi[0x4b] + 6) % 7);   /* the VMI's 0 is Sunday, the VMU's Monday */
    } else {
        game = cardfs_vms_is_game(vms, len);
        for (i = 0; i < 12; i++) e[4 + i] = name[i] ? (unsigned char)name[i] : ' ';
        for (i = 0; i < 12 && name[i]; i++) ;
        for (; i < 12; i++) e[4 + i] = ' ';
        memcpy(e + 0x10, date, 8);
    }
    e[0] = game ? 0xCC : 0x33;
    e[0x18] = n & 255; e[0x19] = n >> 8;
    e[0x1a] = game ? 1 : 0;
    memset(data, 0, n * BLK);
    memcpy(data, vms, len);
    return cardfs_add(card, e, data, n);
}

/* ---- defragmenting ---- */

int cardfs_defrag(unsigned char *card)
{
    static unsigned char out[256 * BLK], data[USER_BLOCKS * BLK];
    static CardFile f[200];
    int n = cardfs_list(card, f, 200), i, pass, b;
    for (i = 0; i < n; i++) if (cardfs_chain(card, &f[i], (int[USER_BLOCKS]){0}) < 0) return -1;
    memcpy(out, card, sizeof(out));
    for (b = 0; b < USER_BLOCKS; b++) fat_set(out, b, FAT_FREE);
    for (pass = 0; pass < 2; pass++)       /* the game first, from block 0; then the saves from the top */
        for (i = 0; i < n; i++) {
            int len, m, to[USER_BLOCKS], k;
            if ((pass == 0) != (f[i].type == 0xCC)) continue;
            len = cardfs_export_vms(card, &f[i], data);
            m = len / BLK;
            if (pass == 0) { for (k = 0; k < m; k++) to[k] = k; }
            else {
                for (k = 0, b = USER_BLOCKS - 1; k < m && b >= 0; b--) if (fat(out, b) == FAT_FREE) to[k++] = b;
                if (k < m) return -1;
            }
            for (k = 0; k < m; k++) {
                memcpy(out + to[k] * BLK, data + k * BLK, BLK);
                fat_set(out, to[k], k < m - 1 ? to[k + 1] : FAT_LAST);
            }
            out[f[i].off + 2] = to[0] & 255; out[f[i].off + 3] = to[0] >> 8;
        }
    memcpy(card, out, sizeof(out));
    return cardfs_game_room(card);
}

/* ---- checking and repairing ---- */

static void issue(CardCheck *c, const char *text, const char *name)
{
    if (c->n >= CARDFS_ISSUES) return;
    if (name) {
        char nm[13];
        int k;
        memcpy(nm, name, 13);
        for (k = 11; k >= 0 && nm[k] == ' '; k--) nm[k] = 0;
        snprintf(c->text[c->n], sizeof(c->text[0]), "%s: %s", nm, text);
    } else snprintf(c->text[c->n], sizeof(c->text[0]), "%s", text);
    c->n++;
}

/* the system blocks' FAT entries as a formatted card has them */
static int system_fat_ok(const unsigned char *c, int fix, unsigned char *w)
{
    int f, d, l, i, ok = 1;
    layout(c, &f, &d, &l);
    for (i = d - l + 1; i <= d; i++) {
        int want = i == d - l + 1 ? FAT_LAST : i - 1;
        if (fat(c, i) != want) { ok = 0; if (fix) fat_set(w, i, want); }
    }
    if (fat(c, f) != FAT_LAST) { ok = 0; if (fix) fat_set(w, f, FAT_LAST); }
    if (fat(c, 255) != FAT_LAST) { ok = 0; if (fix) fat_set(w, 255, FAT_LAST); }
    return ok;
}

static int root_layout_ok(const unsigned char *c)
{
    const unsigned char *r = c + 255 * BLK;
    return rd16(r + 0x46) == 254 && rd16(r + 0x48) == 1 && rd16(r + 0x4a) == 253 && rd16(r + 0x4c) == 13;
}

static void check_or_repair(unsigned char *c, CardCheck *out, int fix)
{
    static CardFile f[200];
    CardInfo in;
    int own[USER_BLOCKS], n, i, b, lost = 0;
    memset(out, 0, sizeof(*out));
    if (!formatted(c)) { issue(out, "not a formatted card", NULL); return; }
    n = cardfs_list(c, f, 200);
    if (!root_layout_ok(c)) {
        static const unsigned short info[12] = { 255, 0, 255, 254, 1, 253, 13, 0, 200, 31, 0, 128 };
        issue(out, fix ? "root block's layout: put right" : "root block's layout is misplaced", NULL);
        out->repairable++;
        if (fix) for (i = 0; i < 12; i++) { c[255 * BLK + 0x40 + 2 * i] = info[i] & 255; c[255 * BLK + 0x41 + 2 * i] = info[i] >> 8; }
    }
    if (!system_fat_ok(c, fix, c)) {
        issue(out, fix ? "system blocks in the FAT: put right" : "system blocks wrong in the FAT", NULL);
        out->repairable++;
    }
    owners(c, f, n, own);
    for (i = 0; i < n; i++) {
        int blocks[USER_BLOCKS], m = cardfs_chain(c, &f[i], blocks), k, shared = 0;
        if (m < 0) { issue(out, "its blocks are broken", f[i].name); out->broken++; continue; }
        for (k = 0; k < m; k++) if (own[blocks[k]] == -2) shared = 1;
        if (shared) { issue(out, "shares blocks with another", f[i].name); out->broken++; continue; }
        if (cardfs_info(c, &f[i], &in) && in.crc_computed >= 0 && in.crc_computed != in.crc_stored) {
            issue(out, "damaged (its CRC is wrong)", f[i].name);
            out->broken++;
        }
    }
    for (b = 0; b < USER_BLOCKS; b++)       /* used in the FAT, by no save: lost */
        if (fat(c, b) != FAT_FREE && own[b] == -1) {
            int broken_owner = 0;
            for (i = 0; i < n && !broken_owner; i++) {      /* (perhaps a broken save's: then left be) */
                int x = f[i].first, steps = 0;
                if (cardfs_chain(c, &f[i], (int[USER_BLOCKS]){0}) >= 0) continue;
                while (x >= 0 && x < USER_BLOCKS && steps++ < USER_BLOCKS) {
                    if (x == b) { broken_owner = 1; break; }
                    x = fat(c, x);
                }
            }
            if (broken_owner) continue;
            lost++;
            if (fix) fat_set(c, b, FAT_FREE);
        }
    if (lost) {
        char t[48];
        snprintf(t, sizeof(t), fix ? "%d lost block%s freed" : "%d lost block%s (used by no save)", lost, lost == 1 ? "" : "s");
        issue(out, t, NULL);
        out->repairable++;
        out->lost_blocks = lost;
    }
}

void cardfs_check(const unsigned char *card, CardCheck *out)
{
    static unsigned char copy[256 * BLK];
    memcpy(copy, card, sizeof(copy));      /* (the same code as the repair, on a copy) */
    check_or_repair(copy, out, 0);
}

int cardfs_repair(unsigned char *card, CardCheck *out)
{
    check_or_repair(card, out, 1);
    return out->repairable;
}
