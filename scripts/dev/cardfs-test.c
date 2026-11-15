/* Tests psp/app/cardfs.c on the computer against real files.
 *   python3 scripts/dev/card-add-game.py SAVES.bin GAME.vms build/cardfs-test.bin NAME
 *   cc -I protocol -o build/cardfs-test scripts/dev/cardfs-test.c psp/app/cardfs.c
 *   build/cardfs-test build/cardfs-test.bin EMPTY.bin GAME.vms GAME.dci GAME.vmi
 * CARD: a card with saves and that game; EMPTY: a formatted empty card; the
 * game's .vms, and the same game as .dci and .vmi from elsewhere (the web).
 * Nothing given is written. */
#include <stdio.h>
#include <string.h>
#include "../../psp/app/cardfs.h"

static unsigned char card[131072], empty[131072], vms[131072], dci[131072 + 32], vmi[108], out[131072 + 32];
static int failed;
static void check(const char *what, int ok) { printf("%-4s %s\n", ok ? "ok" : "FAIL", what); if (!ok) failed++; }
static long load(const char *path, unsigned char *buf, long max)
{
    FILE *f = fopen(path, "rb");
    long n = f ? (long)fread(buf, 1, max, f) : -1;
    if (f) fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    static CardFile f[200], g[200];
    int n, i, game = -1, save = -1, len, freed;
    long vms_n, dci_n;
    if (argc < 6 || load(argv[1], card, sizeof(card)) != 131072 || load(argv[2], empty, sizeof(empty)) != 131072) {
        fprintf(stderr, "usage: see the top of the file\n"); return 2;
    }
    vms_n = load(argv[3], vms, sizeof(vms)); dci_n = load(argv[4], dci, sizeof(dci));
    if (load(argv[5], vmi, sizeof(vmi)) != 108 || vms_n <= 0 || dci_n <= 32) { fprintf(stderr, "game files?\n"); return 2; }

    n = cardfs_list(card, f, 200);
    for (i = 0; i < n; i++) {
        printf("     %-12s type %02x blocks %3d first %3d\n", f[i].name, f[i].type, f[i].size, f[i].first);
        if (f[i].type == 0xCC) game = i; else if (save < 0) save = i;
    }
    check("the card has a game and a save", game >= 0 && save >= 0);
    if (game < 0 || save < 0) return 1;

    /* exports of the game, against the same game's files from the web */
    len = cardfs_export_vms(card, &f[game], out);
    check("VMS export: the game's .vms byte for byte", len == vms_n && !memcmp(out, vms, len));
    len = cardfs_export_dci(card, &f[game], out);
    check("DCI export: as long as the .dci", len == dci_n);
    check("DCI export: its blocks (word-reversed) byte for byte", len == dci_n && !memcmp(out + 32, dci + 32, len - 32));
    check("DCI export: the entry's type, protect and first block", !memcmp(out, dci, 4));
    check("DCI export: the entry's size and header offset", !memcmp(out + 0x18, dci + 0x18, 4));
    cardfs_vmi(card, &f[game], (const char *)vmi + 0x50, (int)vms_n, out);
    check("VMI: the checksum of the same resource name", !memcmp(out, vmi, 4));
    check("VMI: mode (game, not protected) as the .vmi", out[0x64] == vmi[0x64] && out[0x65] == vmi[0x65]);
    check("VMI: size as the .vmi", !memcmp(out + 0x68, vmi + 0x68, 4));
    check("VMI: file number as the .vmi", !memcmp(out + 0x4e, vmi + 0x4e, 2));

    /* copy a save to the empty card, twice; the game where one is */
    {
        int fb[200], gb[200], m, k, a, b, same = 1, dst_n;
        int free0 = cardfs_free(empty);
        check("copy a save to the empty card", cardfs_copy(card, &f[save], empty) == CARDFS_OK);
        dst_n = cardfs_list(empty, g, 200);
        check("  it is there, under its name", dst_n == 1 && !strcmp(g[0].name, f[save].name));
        a = cardfs_chain(card, &f[save], fb); b = cardfs_chain(empty, &g[0], gb);
        for (k = 0; k < a && a == b; k++) if (memcmp(card + fb[k] * 512, empty + gb[k] * 512, 512)) same = 0;
        check("  the same bytes, block for block", a == b && same);
        check("  in the highest free blocks", gb[0] == 199);
        check("  free blocks down by its size", cardfs_free(empty) == free0 - f[save].size);
        check("copy it again: Same file exist", cardfs_copy(card, &f[save], empty) == CARDFS_SAME);
        {   /* a card with another game: the game copied, then renamed there */
            static unsigned char other[131072];
            CardFile h[2];
            memcpy(other, empty, sizeof(other));
            check("copy the game to a card with no game", cardfs_copy(card, &f[game], other) == CARDFS_OK);
            for (k = 0; k < cardfs_list(other, h, 2); k++)
                if (h[k].type == 0xCC) memcpy(other + h[k].off + 4, "OTHERGAME   ", 12);
            check("copy it where another game is: no room (one game a card)", cardfs_copy(card, &f[game], other) == CARDFS_ROOM);
        }
        m = cardfs_find(empty, g[0].off, &g[1]);
        check("find a save by its entry", m && !strcmp(g[1].name, f[save].name));
        cardfs_delete(empty, &g[0]);
        freed = cardfs_free(empty);
        check("delete it: the card empty again", cardfs_list(empty, g, 200) == 0 && freed == free0);
    }
    printf("%s\n", failed ? "FAILED" : "all as expected");
    return failed != 0;
}
