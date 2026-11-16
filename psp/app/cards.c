/* See cards.h. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>
#include "cards.h"
#include "cardfs.h"
#include "../../protocol/psp2dc_protocol.h"

/* Whether a file is there, by opening it: sceIoGetstat failed for files that
 * were there under PPSSPP, and a wrong "not there" here would let a new card
 * overwrite an old one. */
static int exists(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoClose(fd);
    return 1;
}

static void path_of(char *out, int max, const char *name, const char *ext)
{
    snprintf(out, max, "%s/%s.%s", CARDS_DIR, name, ext);
}

static int read_file(const char *path, unsigned char *img)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int n;
    if (fd < 0) return 0;
    n = sceIoRead(fd, img, PSP2DC_CARD_BYTES);
    sceIoClose(fd);
    return n == PSP2DC_CARD_BYTES;
}

void cards_migrate(void)
{
    sceIoMkdir("ms0:/PSP2DC", 0777);
    sceIoMkdir(CARDS_DIR, 0777);
    /* Copied, not renamed (a rename into another folder failed), and the old
     * file removed only once the copy reads back the same. (.new: see
     * cards_read.) */
    if (!exists(CARDS_DIR "/card1.bin") && !exists(CARDS_DIR "/card1.new")) {
        static unsigned char img[PSP2DC_CARD_BYTES], check[PSP2DC_CARD_BYTES];
        const char *from = read_file("ms0:/PSP2DC/card1.bin", img) ? "ms0:/PSP2DC/card1.bin"
                         : read_file("ms0:/PSP2DC/card1.new", img) ? "ms0:/PSP2DC/card1.new" : NULL;
        if (from && cards_write("card1", img) == 0 && cards_read("card1", check) && !memcmp(img, check, sizeof(img))) {
            sceIoRemove("ms0:/PSP2DC/card1.bin");
            sceIoRemove("ms0:/PSP2DC/card1.new");
        }
    }
}

int cards_list(char names[CARDS_MAX][CARD_NAME_MAX])
{
    SceUID d = sceIoDopen(CARDS_DIR);
    SceIoDirent e;
    int n = 0, i, j;
    if (d < 0) return 0;
    memset(&e, 0, sizeof(e));
    while (n < CARDS_MAX && sceIoDread(d, &e) > 0) {
        int len = strlen(e.d_name);
        /* (names starting with a dot are hidden files: macOS leaves a "._"
         * file beside every file it copies onto the stick) */
        if (e.d_name[0] != '.' && len > 4 && len - 4 < CARD_NAME_MAX && !strcasecmp(e.d_name + len - 4, ".bin")
            && e.d_stat.st_size == PSP2DC_CARD_BYTES) {
            int k, lower = 0;
            memcpy(names[n], e.d_name, len - 4);
            names[n][len - 4] = 0;
            /* a short (8.3) name comes back in capitals whatever it was
             * written as: shown in small letters, as the cards are made */
            for (k = 0; names[n][k]; k++) if (names[n][k] >= 'a' && names[n][k] <= 'z') lower = 1;
            if (!lower) for (k = 0; names[n][k]; k++) if (names[n][k] >= 'A' && names[n][k] <= 'Z') names[n][k] += 32;
            n++;
        }
        memset(&e, 0, sizeof(e));
    }
    sceIoDclose(d);
    for (i = 1; i < n; i++)                /* by name (few: insertion) */
        for (j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--) {
            char t[CARD_NAME_MAX];
            strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t);
        }
    return n;
}

int cards_read(const char *name, unsigned char *img)
{
    char path[128];
    path_of(path, sizeof(path), name, "bin");
    if (read_file(path, img)) return 1;
    path_of(path, sizeof(path), name, "new");
    return read_file(path, img);
}

int cards_write(const char *name, const unsigned char *img)
{
    char path[128], tmp[128];
    SceUID fd;
    int n;
    sceIoMkdir(CARDS_DIR, 0777);
    path_of(path, sizeof(path), name, "bin");
    path_of(tmp, sizeof(tmp), name, "new");
    if ((fd = sceIoOpen(tmp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777)) < 0) return -1;
    n = sceIoWrite(fd, img, PSP2DC_CARD_BYTES);
    sceIoClose(fd);
    if (n != PSP2DC_CARD_BYTES) return -1;
    sceIoRemove(path);
    return sceIoRename(tmp, path) < 0 ? -1 : 0;
}

/* As a card a Dreamcast formats (and the adapter's preformat, firmware vmu.c,
 * vmu_preformat): everything 0xFF, the system area (blocks 241-255) zeroed,
 * the root block's magic and file system info from 0x40 (FAT at 0x46,
 * directory at 0x4a: mc.pp.se/dc/vms/flashmem.html, dmitry.gr "VMU
 * Hacking", libevmu's EvmuRootBlock), the FAT with blocks 0-240 free and the
 * directory chained from 253 down to 241; no files. Written 4 bytes late
 * before, as the adapter did (2026-11-15). */
void cards_format(unsigned char *img)
{
    unsigned char *root = img + 255 * 512, *fat = img + 254 * 512;
    static const unsigned short info[12] = { 255, 0, 255, 254, 1, 253, 13, 0, 200, 31, 0, 128 };
    ScePspDateTime t;
    int i;
    memset(img, 0xFF, PSP2DC_CARD_BYTES);
    memset(img + 241 * 512, 0, 15 * 512);
    memset(root, 0x55, 16);
    memset(&t, 0, sizeof(t));
    sceRtcGetCurrentClockLocalTime(&t);
    {   /* the format's date, BCD */
        int v[8] = { t.year / 100, t.year % 100, t.month, t.day, t.hour, t.minute, t.second, 0 };
        for (i = 0; i < 8; i++) root[0x30 + i] = (unsigned char)((v[i] / 10) << 4 | v[i] % 10);
    }
    for (i = 0; i < 12; i++) { root[0x40 + 2 * i] = info[i] & 0xFF; root[0x41 + 2 * i] = info[i] >> 8; }
    for (i = 0; i < 256; i++) {
        unsigned v = i < 241 ? 0xFFFC : (i >= 242 && i <= 253) ? (unsigned)(i - 1) : 0xFFFA;
        fat[2 * i] = v & 0xFF; fat[2 * i + 1] = v >> 8;
    }
}

int cards_unique(const char *base, const char *suffix, char out[CARD_NAME_MAX])
{
    char path[128], tmp[128], num[8];
    int i, room;
    for (i = 1; i < 100; i++) {
        num[0] = 0;
        if (i > 1) snprintf(num, sizeof(num), "%d", i);
        room = CARD_NAME_MAX - 1 - (int)strlen(suffix) - (int)strlen(num);
        snprintf(out, CARD_NAME_MAX, "%.*s%s%s", room, base, suffix, num);
        path_of(path, sizeof(path), out, "bin");
        path_of(tmp, sizeof(tmp), out, "new");
        if (!exists(path) && !exists(tmp)) return 0;
    }
    return -1;
}

int cards_new(char out[CARD_NAME_MAX])
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    char path[128], tmp[128];
    int i;
    for (i = 1; i < 100; i++) {
        snprintf(out, CARD_NAME_MAX, "card%d", i);
        path_of(path, sizeof(path), out, "bin");
        path_of(tmp, sizeof(tmp), out, "new");
        if (!exists(path) && !exists(tmp)) break;
    }
    if (i == 100) return -1;
    cards_format(img);
    return cards_write(out, img);
}

int cards_create(const char *name)
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    char path[128], tmp[128];
    path_of(path, sizeof(path), name, "bin");
    path_of(tmp, sizeof(tmp), name, "new");
    if (exists(path) || exists(tmp)) return 1;
    cards_format(img);
    return cards_write(name, img) < 0 ? -1 : 0;
}

/* ---- a save, out of the card ---- */

static int write_file(const char *path, const unsigned char *data, int len)
{
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    int n;
    if (fd < 0) return -1;
    n = sceIoWrite(fd, data, len);
    sceIoClose(fd);
    return n == len ? 0 : -1;
}

int cards_export(const unsigned char *card, const CardFile *f, char base[9])
{
    static unsigned char buf[32 + 200 * 512];
    unsigned char vmi[108];
    char path[96], stem[9];
    int i, n, len, k;
    /* the name: the save's file name in letters, digits and _, 8 at most (the
     * VMI's resource name), then numbered while any of the three is there */
    for (i = n = 0; f->name[i] && n < 8; i++) {
        char c = f->name[i];
        if (c == ' ') continue;
        stem[n++] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : '_';
    }
    if (!n) stem[n++] = 'S';
    stem[n] = 0;
    for (k = 0; k < 100; k++) {
        static const char *const ext[3] = { "DCI", "VMS", "VMI" };
        int e, taken = 0;
        if (k) snprintf(base, 9, "%.*s%02d", n > 6 ? 6 : n, stem, k);
        else snprintf(base, 9, "%s", stem);
        for (e = 0; e < 3; e++) {
            snprintf(path, sizeof(path), "%s/%s.%s", SAVES_DIR, base, ext[e]);
            if (exists(path)) taken = 1;
        }
        if (!taken) break;
    }
    if (k == 100) return -1;
    sceIoMkdir(SAVES_DIR, 0777);
    if ((len = cardfs_export_dci(card, f, buf)) < 0) return -1;
    snprintf(path, sizeof(path), "%s/%s.DCI", SAVES_DIR, base);
    if (write_file(path, buf, len) < 0) return -1;
    if ((len = cardfs_export_vms(card, f, buf)) < 0) return -1;
    snprintf(path, sizeof(path), "%s/%s.VMS", SAVES_DIR, base);
    if (write_file(path, buf, len) < 0) return -1;
    cardfs_vmi(card, f, base, len, vmi);
    snprintf(path, sizeof(path), "%s/%s.VMI", SAVES_DIR, base);
    return write_file(path, vmi, sizeof(vmi));
}

/* ---- backups: ms0:/PSP2DC/backups/NAME@YYYYMMDD-HHMMSS[-auto].bin ---- */

int cards_backup(const char *name, const unsigned char *img, int automatic)
{
    static unsigned char file[PSP2DC_CARD_BYTES];
    static CardBackup all[BACKUPS_MAX];
    ScePspDateTime t;
    char path[160];
    int n, i, kept = 0;
    if (!img) { if (!cards_read(name, file)) return -1; img = file; }
    memset(&t, 0, sizeof(t));
    sceRtcGetCurrentClockLocalTime(&t);
    sceIoMkdir(BACKUPS_DIR, 0777);
    snprintf(path, sizeof(path), "%s/%s@%04d%02d%02d-%02d%02d%02d%s.bin", BACKUPS_DIR, name, t.year, t.month, t.day,
             t.hour, t.minute, t.second, automatic ? "-auto" : "");
    if (!exists(path) && write_file(path, img, PSP2DC_CARD_BYTES) < 0) return -1;
    if (!automatic) return 0;
    /* the automatic ones: the newest BACKUPS_AUTO_KEEP of this card kept */
    n = cards_backups(all, BACKUPS_MAX);
    for (i = 0; i < n; i++) {
        if (!all[i].automatic || strcasecmp(all[i].card, name)) continue;
        if (++kept > BACKUPS_AUTO_KEEP) {
            snprintf(path, sizeof(path), "%s/%s", BACKUPS_DIR, all[i].file);
            sceIoRemove(path);
        }
    }
    return 0;
}

int cards_backups(CardBackup *out, int max)
{
    SceUID d = sceIoDopen(BACKUPS_DIR);
    SceIoDirent e;
    int n = 0, i, j;
    if (d < 0) return 0;
    memset(&e, 0, sizeof(e));
    while (n < max && sceIoDread(d, &e) > 0) {
        int len = strlen(e.d_name);
        char *at = strrchr(e.d_name, '@');
        if (e.d_name[0] != '.' && len > 4 && len < (int)sizeof(out[0].file) && at && !strcasecmp(e.d_name + len - 4, ".bin") &&
            e.d_stat.st_size == PSP2DC_CARD_BYTES && at - e.d_name < CARD_NAME_MAX && strlen(at + 1) >= 19) {
            CardBackup *b = &out[n];
            memset(b, 0, sizeof(*b));
            strcpy(b->file, e.d_name);
            memcpy(b->card, e.d_name, at - e.d_name);
            /* YYYYMMDD-HHMMSS as "YYYY-MM-DD HH:MM" */
            snprintf(b->when, sizeof(b->when), "%.4s-%.2s-%.2s %.2s:%.2s", at + 1, at + 5, at + 7, at + 10, at + 12);
            memcpy(b->stamp, at + 1, 15);
            b->automatic = strstr(at, "-auto") != NULL;
            n++;
        }
        memset(&e, 0, sizeof(e));
    }
    sceIoDclose(d);
    for (i = 1; i < n; i++)                /* newest first */
        for (j = i; j > 0 && strcmp(out[j - 1].stamp, out[j].stamp) < 0; j--) {
            CardBackup t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

int cards_backup_read(const CardBackup *b, unsigned char *img)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", BACKUPS_DIR, b->file);
    return read_file(path, img);
}

/* ---- the cards themselves ---- */

int cards_name_ok(const char *name)
{
    int i;
    if (!name[0] || strlen(name) >= CARD_NAME_MAX) return 0;
    for (i = 0; name[i]; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '-' || name[i] == '_')) return 0;
    return 1;
}

int cards_rename(const char *from, const char *to)
{
    char a[128], b[128];
    if (!cards_name_ok(to)) return -1;
    path_of(a, sizeof(a), from, "bin");
    path_of(b, sizeof(b), to, "bin");
    if (strcasecmp(from, to) && exists(b)) return -2;   /* (FAT names: any case) */
    return sceIoRename(a, b) < 0 ? -1 : 0;
}

int cards_delete(const char *name)
{
    char path[128];
    path_of(path, sizeof(path), name, "bin");
    return sceIoRemove(path) < 0 ? -1 : 0;
}

int cards_duplicate(const char *name, char out[CARD_NAME_MAX])
{
    static unsigned char img[PSP2DC_CARD_BYTES];
    if (!cards_read(name, img) || cards_unique(name, "-copy", out) < 0) return -1;
    return cards_write(out, img);
}

/* ---- saves to import: in SAVES_DIR, .DCI, or .VMS (with a .VMI of the same
 * name, when there is one) ---- */

int cards_imports(CardImport *out, int max)
{
    /* one pass over the folder: the VMIs are matched by name afterwards, not
     * looked for one open at a time (127 games took ~250 opens, seconds) */
    static char vmis[512][64];
    SceUID d = sceIoDopen(SAVES_DIR);
    SceIoDirent e;
    int n = 0, nv = 0, i, j;
    if (d < 0) return 0;
    memset(&e, 0, sizeof(e));
    while (sceIoDread(d, &e) > 0) {
        int len = strlen(e.d_name), dci, vms;
        if (len < 5 || len >= (int)sizeof(out[0].file) || e.d_name[0] == '.') { memset(&e, 0, sizeof(e)); continue; }   /* (hidden, "._" from macOS) */
        dci = !strcasecmp(e.d_name + len - 4, ".dci");
        vms = !strcasecmp(e.d_name + len - 4, ".vms");
        if (!strcasecmp(e.d_name + len - 4, ".vmi") && nv < 512) snprintf(vmis[nv++], 64, "%.*s", len - 4, e.d_name);
        else if (n < max && (dci || vms) && e.d_stat.st_size > 0 && e.d_stat.st_size <= 32 + 200 * 512) {
            CardImport *im = &out[n++];
            memset(im, 0, sizeof(*im));
            strcpy(im->file, e.d_name);
            im->dci = dci;
            im->size = (long)e.d_stat.st_size;
        }
        memset(&e, 0, sizeof(e));
    }
    sceIoDclose(d);
    for (i = 0; i < n; i++)                /* a VMS's VMI: the same name (any case) */
        if (!out[i].dci) {
            int stem = (int)strlen(out[i].file) - 4;
            for (j = 0; j < nv && !out[i].vmi; j++)
                out[i].vmi = (int)strlen(vmis[j]) == stem && !strncasecmp(vmis[j], out[i].file, stem);
        }
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && strcasecmp(out[j - 1].file, out[j].file) > 0; j--) {
            CardImport t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

int cards_import_read(const CardImport *im, unsigned char *data, int max, unsigned char vmi[108])
{
    char path[160];
    SceUID fd;
    int len, k = (int)strlen(im->file) - 4;
    snprintf(path, sizeof(path), "%s/%s", SAVES_DIR, im->file);
    if ((fd = sceIoOpen(path, PSP_O_RDONLY, 0)) < 0) return -1;
    len = sceIoRead(fd, data, max);
    sceIoClose(fd);
    if (im->vmi) {
        snprintf(path, sizeof(path), "%s/%.*s.VMI", SAVES_DIR, k, im->file);
        if ((fd = sceIoOpen(path, PSP_O_RDONLY, 0)) < 0) {
            snprintf(path, sizeof(path), "%s/%.*s.vmi", SAVES_DIR, k, im->file);
            fd = sceIoOpen(path, PSP_O_RDONLY, 0);
        }
        if (fd < 0 || sceIoRead(fd, vmi, 108) != 108) { if (fd >= 0) sceIoClose(fd); return -1; }
        sceIoClose(fd);
    }
    return len;
}
