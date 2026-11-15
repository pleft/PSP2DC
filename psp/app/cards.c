/* See cards.h. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>
#include "cards.h"
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

/* As the adapter's own preformat (firmware vmu.c, vmu_preformat; the layout
 * from mc.pp.se/dc/vms/flashmem.html): everything 0xFF, the system area
 * (blocks 241-255) zeroed, the root block's magic and file system info, the
 * FAT with blocks 0-240 free and the system blocks chained; no files. */
void cards_format(unsigned char *img)
{
    unsigned char *root = img + 255 * 512, *fat = img + 254 * 512;
    static const unsigned short info[10] = { 254, 0, 255, 254, 1, 253, 13, 0, 200, 31 };
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
    for (i = 0; i < 10; i++) { root[0x44 + 2 * i] = info[i] & 0xFF; root[0x45 + 2 * i] = info[i] >> 8; }
    root[0x62] = 0x80;
    for (i = 0; i < 256; i++) {
        unsigned v = i < 241 ? 0xFFFC : 0xFFFA;
        fat[2 * i] = v & 0xFF; fat[2 * i + 1] = v >> 8;
    }
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
