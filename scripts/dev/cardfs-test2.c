/* Tests psp/app/cardfs.c's imports, info, picture, defragmenting, checking
 * and repairing on the computer, with real files and damaged copies.
 *   cc -I protocol -o build/cardfs-test2 scripts/dev/cardfs-test2.c psp/app/cardfs.c
 *   build/cardfs-test2 SAVES.bin EMPTY.bin VMU_FOLDER
 * SAVES: a card with data saves (the one with Crazy Taxi 2 and Sonic Adventure
 * 2); EMPTY: a formatted empty card; VMU_FOLDER: .vms/.vmi/.dci files, with
 * ChaoAdventure.dci and .vms, and swampy.vms and .vmi. Nothing given is written. */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "../../psp/app/cardfs.h"

#define CARD 131072
static unsigned char saves[CARD], empty[CARD], w[CARD], w2[CARD], buf[CARD + 64], buf2[CARD + 64];
static int failed;
static void check(const char *what, int ok) { printf("%-4s %s\n", ok ? "ok" : "FAIL", what); if (!ok) failed++; }
static long load(const char *path, unsigned char *b, long max)
{
    FILE *f = fopen(path, "rb");
    long n = f ? (long)fread(b, 1, max, f) : -1;
    if (f) fclose(f);
    return n;
}
static int find(const unsigned char *card, const char *name, CardFile *out)
{
    static CardFile all[200];
    int n = cardfs_list(card, all, 200), i;
    for (i = 0; i < n; i++) if (!strncmp(all[i].name, name, strlen(name))) { *out = all[i]; return 1; }
    return 0;
}
static int fat(const unsigned char *c, int b) { return c[254 * 512 + 2 * b] | c[254 * 512 + 2 * b + 1] << 8; }
static void fat_set(unsigned char *c, int b, int v) { c[254 * 512 + 2 * b] = v & 255; c[254 * 512 + 2 * b + 1] = v >> 8; }

int main(int argc, char **argv)
{
    static const unsigned char date[8] = { 0x20, 0x26, 0x11, 0x15, 0x12, 0x00, 0x00, 0x06 };
    char path[1024];
    CardFile f, g;
    CardInfo in;
    CardCheck ck;
    long n, m;
    int i, games = 0, notgames = 0, total = 0;
    if (argc < 4 || load(argv[1], saves, CARD) != CARD || load(argv[2], empty, CARD) != CARD) { fprintf(stderr, "usage: see the top\n"); return 2; }
    cardfs_repair(empty, &ck);                 /* (an empty card of the old format: put right first) */
    cardfs_check(empty, &ck);
    check("the empty card checks clean once repaired", ck.n == 0);

    /* info and the CRC on real saves */
    check("info: Sonic Adventure 2's save", find(saves, "SONIC2", &f) && cardfs_info(saves, &f, &in));
    printf("     \"%s\" / \"%s\", app \"%s\", icons %d, picture %d, crc %04x computed %04x\n",
           in.vms_desc, in.dc_desc, in.app, in.icons, in.eyecatch, in.crc_stored, in.crc_computed);
    check("  its CRC as stored", in.crc_computed == in.crc_stored);
    check("info: Crazy Taxi 2's save, CRC as stored", find(saves, "C_TAXI", &f) && cardfs_info(saves, &f, &in) && in.crc_computed == in.crc_stored);

    /* DCI: exported, imported again, the same */
    memcpy(w, empty, CARD);
    find(saves, "SONIC2", &f);
    n = cardfs_export_dci(saves, &f, buf);
    check("DCI round trip: imported", cardfs_import_dci(w, buf, (int)n) == CARDFS_OK);
    check("  the same bytes and entry (but the first block)", find(w, "SONIC2", &g) &&
          cardfs_export_vms(w, &g, buf2) == cardfs_export_vms(saves, &f, buf) && !memcmp(buf, buf2, f.size * 512) &&
          !memcmp(w + g.off + 4, saves + f.off + 4, 28) && w[g.off] == saves[f.off]);
    check("  again: Same file exist", cardfs_import_dci(w, buf2, 0) == CARDFS_FAIL &&
          (n = cardfs_export_dci(saves, &f, buf), cardfs_import_dci(w, buf, (int)n)) == CARDFS_SAME);

    /* a DCI and a VMS+VMI from the web */
    snprintf(path, sizeof(path), "%s/ChaoAdventure.dci", argv[3]); n = load(path, buf, sizeof(buf));
    snprintf(path, sizeof(path), "%s/ChaoAdventure.vms", argv[3]); m = load(path, buf2, sizeof(buf2));
    memcpy(w, empty, CARD);
    check("ChaoAdventure.dci imported", n > 0 && cardfs_import_dci(w, buf, (int)n) == CARDFS_OK);
    check("  a game at block 0, its bytes the .vms's", find(w, "SONICADV", &g) && g.type == 0xCC && g.first == 0 &&
          cardfs_export_vms(w, &g, buf) == m && !memcmp(buf, buf2, m));
    {
        unsigned char vmi[108];
        snprintf(path, sizeof(path), "%s/swampy.vms", argv[3]); n = load(path, buf, sizeof(buf));
        snprintf(path, sizeof(path), "%s/swampy.vmi", argv[3]);
        memcpy(w, empty, CARD);
        check("swampy.vms with its .vmi imported", n > 0 && load(path, vmi, 108) == 108 &&
              cardfs_import_vms(w, buf, (int)n, vmi, NULL, date) == CARDFS_OK);
        check("  SWAMPY, a game, protected (the VMI's mode 3), its bytes", find(w, "SWAMPY", &g) && g.type == 0xCC &&
              g.prot && g.first == 0 && cardfs_export_vms(w, &g, buf2) == (int)((n + 511) / 512 * 512) && !memcmp(buf, buf2, n));
        check("  the date from the VMI (2000-12-17, a Sunday)", g.date[0] == 0x20 && g.date[1] == 0x00 && g.date[2] == 0x12 &&
              g.date[3] == 0x17 && g.date[7] == 6);
        check("  a second game where one is: no room", cardfs_import_vms(w, buf, (int)n, NULL, "OTHER", date) == CARDFS_ROOM);
    }
    /* every .vms of the folder without its VMI: games, by where the header is */
    {
        DIR *d = opendir(argv[3]);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l < 5 || strcasecmp(e->d_name + l - 4, ".vms")) continue;
            snprintf(path, sizeof(path), "%s/%s", argv[3], e->d_name);
            n = load(path, buf, sizeof(buf));
            total++;
            if (cardfs_vms_is_game(buf, (int)n)) games++; else { notgames++; printf("     not taken for a game: %s\n", e->d_name); }
        }
        if (d) closedir(d);
        printf("     %d .vms files: %d taken for games, %d not\n", total, games, notgames);
        check("  every .vms in the folder taken for a game", total > 100 && notgames == 0);
    }
    find(saves, "SONIC2", &f);
    n = cardfs_export_vms(saves, &f, buf);
    check("a save's VMS (no VMI) not taken for a game", !cardfs_vms_is_game(buf, (int)n));
    memcpy(w, empty, CARD);
    check("  imported as a save named as given", cardfs_import_vms(w, buf, (int)n, NULL, "SA2SAVE", date) == CARDFS_OK &&
          find(w, "SA2SAVE", &g) && g.type == 0x33 && g.size == f.size);

    /* the large picture: a save given a 16-colour one, read back */
    {
        unsigned short px[72 * 56];
        unsigned char *h;
        int ok = 1;
        memcpy(w, saves, CARD);
        find(w, "SONIC2", &f);                 /* its header in its first block; 1 icon */
        h = w + f.first * 512;
        cardfs_info(w, &f, &in);
        if (0x80 + in.icons * 512 + 32 + 2016 <= f.size * 512 - 512) {
            /* (the picture overwrites the start of its data: only the picture is checked) */
            int b0 = f.first, k;
            static unsigned char flat[200 * 512];
            int len = cardfs_export_vms(w, &f, flat), blocks[200], cnt = cardfs_chain(w, &f, blocks);
            unsigned char *e = flat + 0x80 + in.icons * 512;
            flat[0x44] = 3; flat[0x45] = 0;
            for (k = 0; k < 16; k++) { e[2 * k] = (unsigned char)(k * 17); e[2 * k + 1] = 0xF0 | (k & 15); }
            for (k = 0; k < 2016; k++) e[32 + k] = (unsigned char)((k % 16) << 4 | ((k + 1) % 16));
            for (k = 0; k < cnt; k++) memcpy(w + blocks[k] * 512, flat + k * 512, 512);
            (void)b0; (void)len; (void)h;
            check("picture: read as 16 colours", cardfs_eyecatch(w, &f, px) == 3);
            for (k = 0; k < 72 * 56; k++) {
                int idx = k & 1 ? (k / 2 + 1) % 16 : (k / 2) % 16;
                if (px[k] != (unsigned short)(idx * 17 | (0xF0 | idx) << 8)) ok = 0;
            }
            check("  every pixel its palette colour, high nibble first", ok);
        } else check("picture: room in the save to try it", 0);
        memcpy(w, saves, CARD);
        find(w, "SONIC2", &f);
        check("no picture in the real save", cardfs_eyecatch(w, &f, px) == 0);
    }

    /* defragmenting: a card filled with saves, every other one deleted */
    {
        static CardFile all[200], before[200];
        static unsigned char data[200][200 * 512];
        unsigned char entry[32];
        int k, cnt, room0, room1, same = 1, nb;
        memcpy(w, empty, CARD);
        find(saves, "C_TAXI", &f);             /* 20 blocks */
        n = cardfs_export_vms(saves, &f, buf);
        memcpy(entry, saves + f.off, 32);
        for (k = 0; k < 9; k++) {              /* 180 blocks of saves */
            char nm[13];
            snprintf(nm, sizeof(nm), "FILL%02d      ", k);
            memcpy(entry + 4, nm, 12);
            buf[f.size * 512 - 1] = (unsigned char)k;   /* (each a little different, past what its CRC covers) */
            cardfs_add(w, entry, buf, f.size);
        }
        cnt = cardfs_list(w, all, 200);
        for (k = 0; k < cnt; k += 2) cardfs_delete(w, &all[k]);
        nb = cardfs_list(w, before, 200);
        for (k = 0; k < nb; k++) cardfs_export_vms(w, &before[k], data[k]);
        room0 = cardfs_game_room(w);
        room1 = cardfs_defrag(w);
        printf("     %d saves left, %d blocks free; room for a game %d before, %d after\n", nb, cardfs_free(w), room0, room1);
        check("defrag: more room for a game", room1 > room0 && room1 == (cardfs_free(w) < 128 ? cardfs_free(w) : 128));
        for (k = 0; k < nb; k++) {
            CardFile x;
            static unsigned char again[200 * 512];
            if (!find(w, before[k].name, &x) || cardfs_export_vms(w, &x, again) != before[k].size * 512 ||
                memcmp(again, data[k], before[k].size * 512)) same = 0;
        }
        check("  every save's bytes unchanged", same);
        cardfs_check(w, &ck);
        for (i = 0; i < ck.n; i++) printf("     found: %s\n", ck.text[i]);
        check("  and the card checks clean", ck.n == 0);
    }

    /* checking and repairing */
    memcpy(w, saves, CARD);                    /* (formatted before 15 Nov: the root's layout 4 bytes late) */
    cardfs_check(w, &ck);
    for (i = 0; i < ck.n; i++) printf("     found: %s\n", ck.text[i]);
    check("check: the old root layout found", ck.n >= 1 && ck.repairable >= 1);
    fat_set(w, 10, 11); fat_set(w, 11, 0xFFFA);  /* two lost blocks */
    cardfs_check(w, &ck);
    check("check: two lost blocks found", ck.lost_blocks == 2);
    memcpy(w2, w, CARD);
    cardfs_repair(w2, &ck);
    for (i = 0; i < ck.n; i++) printf("     repaired: %s\n", ck.text[i]);
    cardfs_check(w2, &ck);
    check("repair: the card checks clean after", ck.n == 0);
    check("  the saves untouched", find(w2, "SONIC2", &g) && find(saves, "SONIC2", &f) &&
          cardfs_export_vms(w2, &g, buf) == cardfs_export_vms(saves, &f, buf2) && !memcmp(buf, buf2, f.size * 512));
    memcpy(w, empty, CARD);                    /* a damaged save: one byte of its data changed */
    find(saves, "SONIC2", &f);
    n = cardfs_export_vms(saves, &f, buf);
    cardfs_add(w, saves + f.off, buf, f.size);
    find(w, "SONIC2", &g);
    w[g.first * 512 + 0x1F0] ^= 1;          /* (inside its first block: its icon) */
    cardfs_check(w, &ck);
    for (i = 0; i < ck.n; i++) printf("     found: %s\n", ck.text[i]);
    check("check: a save with a wrong CRC found", ck.broken == 1 && ck.n == 1 && strstr(ck.text[0], "CRC"));
    /* two saves sharing a block; deleting one keeps the other's */
    {
        CardFile a, b;
        memcpy(w, empty, CARD);
        find(saves, "SONIC2", &f); n = cardfs_export_vms(saves, &f, buf); cardfs_add(w, saves + f.off, buf, f.size);
        find(saves, "C_TAXI", &f); n = cardfs_export_vms(saves, &f, buf); cardfs_add(w, saves + f.off, buf, f.size);
        find(w, "SONIC2", &a); find(w, "C_TAXI", &b);
        /* the save C_TAXI's last block made SONIC2's last block too */
        {
            int ab[200], bb[200], na = cardfs_chain(w, &a, ab), nb2 = cardfs_chain(w, &b, bb);
            fat_set(w, bb[nb2 - 2], ab[na - 1]);   /* b's second last now leads to a's last */
            fat_set(w, bb[nb2 - 1], 0xFFFA);       /* (b's own last block left lost) */
            (void)fat;
        }
        cardfs_check(w, &ck);
        for (i = 0; i < ck.n; i++) printf("     found: %s\n", ck.text[i]);
        check("check: saves sharing blocks found", ck.broken >= 2);
        find(w, "C_TAXI", &b);
        cardfs_delete(w, &b);
        find(w, "SONIC2", &a);
        check("delete one: the other's blocks still in its chain", cardfs_chain(w, &a, (int[200]){0}) == a.size);
    }
    printf("%s\n", failed ? "FAILED" : "all as expected");
    return failed != 0;
}
