/* Tests psp/app/card_sync.c on the computer: which copy of a memory card
 * counts when the PSP reconnects to an adapter that still holds one.
 *   cc -I protocol -o build/card-sync-test scripts/dev/card-sync-test.c psp/app/card_sync.c
 *   build/card-sync-test CARD_A.bin CARD_B.bin
 * Two card images (any 128 KB VMU images); changed copies are made here. */
#include <stdio.h>
#include <string.h>
#include "psp2dc_protocol.h"
#include "../../psp/app/card_sync.h"

static unsigned char A[PSP2DC_CARD_BYTES], B[PSP2DC_CARD_BYTES];
static const char *names[] = { "ADOPT", "SAME", "PUT_OURS", "TAKE", "KEEP_BOTH" };
static int failed;

static unsigned crc_of(const unsigned char *img) { return psp2dc_crc32(0, img, PSP2DC_CARD_BYTES); }
static unsigned crc_changed(const unsigned char *img, int at)   /* the card with one byte changed: a new save */
{
    static unsigned char c[PSP2DC_CARD_BYTES];
    memcpy(c, img, sizeof(c)); c[at] ^= 0x5A;
    return crc_of(c);
}
static void expect(const char *what, SyncState s, SyncChoice want)
{
    SyncChoice got = card_sync_choose(&s);
    printf("%-4s %-62s %s\n", got == want ? "ok" : "FAIL", what, names[got]);
    if (got != want) failed++;
}
static int load(const char *path, unsigned char *img)
{
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(img, 1, PSP2DC_CARD_BYTES, f) : 0;
    if (f) fclose(f);
    return n == PSP2DC_CARD_BYTES;
}

int main(int argc, char **argv)
{
    unsigned a, b, a_dc, a_psp;
    if (argc < 3 || !load(argv[1], A) || !load(argv[2], B)) { fprintf(stderr, "two card images, please\n"); return 2; }
    a = crc_of(A); b = crc_of(B);
    a_dc = crc_changed(A, 100);            /* A after a Dreamcast save */
    a_psp = crc_changed(A, 7000);          /* A after a change on the PSP (a VMU game, FILE mode) */
    printf("A %08x  B %08x\n", a, b);

    /* 2026-11-15: connected with A (the games card), unplugged, the PSP
     * changed to B in standalone mode, plugged in again. Version 4 wrote
     * the adapter's A over B. ours = A's file. */
    expect("the incident: A held, B chosen unplugged, A unchanged", (SyncState){1, 0, 1, a, a, 1, a}, SYNC_PUT_OURS);
    expect("  ... and the adapter's A had a Dreamcast save meanwhile", (SyncState){1, 0, 1, a, a_dc, 1, a}, SYNC_TAKE);
    expect("  ... and A's file also changed on the PSP meanwhile", (SyncState){1, 0, 1, a_psp, a_dc, 1, a}, SYNC_KEEP_BOTH);
    expect("  ... and A's file is not on the PSP any more", (SyncState){1, 0, 0, 0, a, 1, a}, SYNC_KEEP_BOTH);
    /* the same card in use */
    expect("same card, nothing changed", (SyncState){1, 1, 1, a, a, 1, a}, SYNC_SAME);
    expect("same card, a Dreamcast save while the PSP was away", (SyncState){1, 1, 1, a, a_dc, 1, a}, SYNC_TAKE);
    expect("same card, changed on the PSP while away (Chao standalone)", (SyncState){1, 1, 1, a_psp, a, 1, a}, SYNC_PUT_OURS);
    expect("same card, changed on both", (SyncState){1, 1, 1, a_psp, a_dc, 1, a}, SYNC_KEEP_BOTH);
    /* no base, or a base of another card (first run of version 5, settings lost) */
    expect("no base, the copies the same", (SyncState){1, 1, 1, a, a, 0, 0}, SYNC_SAME);
    expect("no base, the copies differ", (SyncState){1, 1, 1, a, b, 0, 0}, SYNC_KEEP_BOTH);
    expect("the base is of another card, the copies differ", (SyncState){1, 1, 1, a_psp, a_dc, 0, b}, SYNC_KEEP_BOTH);
    /* nothing on the PSP */
    expect("no card on the PSP at all", (SyncState){0, 0, 0, 0, a, 0, 0}, SYNC_ADOPT);
    printf("%s\n", failed ? "FAILED" : "all as expected");
    return failed != 0;
}
