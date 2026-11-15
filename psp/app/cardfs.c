/* See cardfs.h. The FAT and directory are found where the root block says
 * (dmitry.gr, "VMU Hacking"), when that makes sense. */
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
int cardfs_copy(const unsigned char *src, const CardFile *f, unsigned char *dst)
{
    int from[USER_BLOCKS], to[USER_BLOCKS], n, i, k, off, slot = -1, b;
    CardFile there[200];
    if (f->prot || !formatted(dst) || (n = cardfs_chain(src, f, from)) < 0) return CARDFS_FAIL;
    k = cardfs_list(dst, there, 200);
    for (i = 0; i < k; i++) if (!memcmp(there[i].name, f->name, 12)) return CARDFS_SAME;
    for (k = 0; (off = entry_off(dst, k)) >= 0; k++) if (dst[off] == 0) { slot = off; break; }
    if (slot < 0) return CARDFS_ROOM;
    if (f->type == 0xCC) {
        if (cardfs_has_game(dst) || n > GAME_MAX) return CARDFS_ROOM;
        for (i = 0; i < n; i++) { if (fat(dst, i) != FAT_FREE) return CARDFS_ROOM; to[i] = i; }
    } else {
        for (i = 0, b = USER_BLOCKS - 1; i < n && b >= 0; b--) if (fat(dst, b) == FAT_FREE) to[i++] = b;
        if (i < n) return CARDFS_ROOM;
    }
    for (i = 0; i < n; i++) {
        memcpy(dst + to[i] * BLK, src + from[i] * BLK, BLK);
        fat_set(dst, to[i], i < n - 1 ? to[i + 1] : FAT_LAST);
    }
    memcpy(dst + slot, src + f->off, 32);
    dst[slot + 2] = to[0] & 255; dst[slot + 3] = to[0] >> 8;
    return CARDFS_OK;
}

/* f off the card: its blocks freed in the FAT, its entry cleared */
void cardfs_delete(unsigned char *c, const CardFile *f)
{
    int b = f->first, n = 0, next;
    while (b >= 0 && b < USER_BLOCKS && n++ < USER_BLOCKS) {
        next = fat(c, b);
        if (next == FAT_FREE) break;
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
