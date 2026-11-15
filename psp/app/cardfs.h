/*
 * A memory card's file system, on its 128 KB image: the saves listed,
 * copied to another card, deleted, and exported as files other tools load.
 * Plain C over the image (no PSP calls), so it is tested on the computer
 * (scripts/dev/cardfs-test.c). Used by the VMU's FILE mode (vmu_sa.c) and the
 * Saves page (main.c).
 *
 * The layout is mc.pp.se's (dc/vms/flashmem.html, fileheader.html): 200 user
 * blocks of 512 bytes, the FAT (16-bit entries, 0xFFFC free, 0xFFFA a file's
 * last block), the directory of 32-byte entries, the root block.
 */
#ifndef PSP2DC_CARDFS_H
#define PSP2DC_CARDFS_H

typedef struct CardFileTag {
    char name[13];              /* the 12-character file name */
    unsigned char date[8];      /* BCD: century, year, month, day, hour, minute, second, weekday (0 Monday) */
    int type;                   /* 0x33 data, 0xCC game */
    int prot;                   /* copy-protected (directory byte 0x01 = 0xFF) */
    int first, size, hdr;       /* first block, blocks, the header's offset in blocks */
    int off;                    /* the directory entry's byte offset in the image: names the save */
} CardFile;

enum { CARDFS_OK, CARDFS_SAME, CARDFS_ROOM, CARDFS_FAIL };

/* The saves, in the directory's order; how many. */
int  cardfs_list(const unsigned char *card, CardFile *out, int max);
/* The save whose directory entry is at off: 1, or 0 when there is none. */
int  cardfs_find(const unsigned char *card, int off, CardFile *out);
/* A save's blocks in order, following the FAT: how many, or -1 when the
 * chain is not the size the directory says. */
int  cardfs_chain(const unsigned char *card, const CardFile *f, int *blocks);
int  cardfs_free(const unsigned char *card);          /* free blocks of the 200 */
int  cardfs_has_game(const unsigned char *card);
int  cardfs_game_room(const unsigned char *card);     /* room for a mini-game (0 with one there) */
void cardfs_comment(const unsigned char *card, const CardFile *f, char out[17]);
/* f from src into dst as the BIOS places files (a game from block 0 up, data
 * in the highest free blocks): CARDFS_OK, _SAME (that name is there), _ROOM,
 * _FAIL (copy-protected, dst not formatted, a broken chain). */
int  cardfs_copy(const unsigned char *src, const CardFile *f, unsigned char *dst);
void cardfs_delete(unsigned char *card, const CardFile *f);

/* Exports, into out (room for 32 + 512 * f->size bytes); the length, or -1.
 * VMS: the save's blocks as they are, the file the Dreamcast's browser
 * downloaded. DCI (Nexus): the 32-byte directory entry, then the blocks
 * with every 4-byte word reversed (libevmu's EvmuNexus_applyByteOrdering;
 * checked against a .DCI and .VMS of the same game). */
int  cardfs_export_vms(const unsigned char *card, const CardFile *f, unsigned char *out);
int  cardfs_export_dci(const unsigned char *card, const CardFile *f, unsigned char *out);
/* The 108-byte VMI that goes with a VMS (mc.pp.se/dc/vms/vmi.html): res, the
 * VMS file's name without .VMS (8 characters at most); the save's long
 * description; its date; the checksum the first four characters of res
 * ANDed with "SEGA" (as in five .VMI files from the web). */
void cardfs_vmi(const unsigned char *card, const CardFile *f, const char *res, int vms_bytes,
                unsigned char out[108]);

#endif
