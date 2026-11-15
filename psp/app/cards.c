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
        if (len > 4 && len - 4 < CARD_NAME_MAX && !strcasecmp(e.d_name + len - 4, ".bin")
            && e.d_stat.st_size == PSP2DC_CARD_BYTES) {
            memcpy(names[n], e.d_name, len - 4);
            names[n][len - 4] = 0;
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
