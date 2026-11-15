/*
 * Which copy of a memory card counts when the PSP finds the adapter still
 * holding one from before (it was unplugged or restarted meanwhile). Pure
 * logic, so it is tested on the computer (scripts/dev/card-sync-test.c).
 *
 * A: the adapter's card, by name; a, its CRC-32 (CARD_STATE). Ours: the
 * PSP's copy of A, the card in use when that is A, else A's file. The base:
 * the last copy of A both had (main.c keeps it). Nothing is ever written over
 * a copy that changed since the base.
 */
#ifndef PSP2DC_CARD_SYNC_H
#define PSP2DC_CARD_SYNC_H

typedef enum {
    SYNC_ADOPT,       /* the PSP has no card at all: the adapter's becomes its card A */
    SYNC_SAME,        /* the copies are the same, A is the player's card: only new blocks are to come */
    SYNC_PUT_OURS,    /* the adapter's is no newer: it lets go, the PSP's card in use goes in */
    SYNC_TAKE,        /* only the adapter's changed: it comes into A, then the PSP's card in use goes in */
    SYNC_KEEP_BOTH,   /* both changed, or not known: it comes into a new card, then the PSP's goes in */
} SyncChoice;

typedef struct {
    int have_card;            /* the PSP has a card in use */
    int a_is_mine;            /* A is the player's card (the one in use, or to go back to) */
    int have_ours;            /* the PSP has a copy of A (in use, or A's file) */
    unsigned ours, a;         /* their CRC-32s */
    int base_is_a;            /* the base is of card A */
    unsigned base;            /* its CRC-32 */
} SyncState;

SyncChoice card_sync_choose(const SyncState *s);

#endif
