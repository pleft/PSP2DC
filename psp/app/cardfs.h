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

/* Adds a file: its 32-byte directory entry (the first block ignored) and n
 * blocks, placed as cardfs_copy places them. CARDFS_OK, _SAME, _ROOM, _FAIL. */
int  cardfs_add(unsigned char *card, const unsigned char entry[32], const unsigned char *data, int n);

/* What a save's header says (mc.pp.se fileheader.html). 1, or 0 when it has
 * none that can be read. */
typedef struct {
    char vms_desc[17];          /* shown by the VMU */
    char dc_desc[33];           /* shown by the Dreamcast's file manager */
    char app[17];               /* the application that made it */
    int icons, speed;           /* icons (0-3) and their speed, n/30 s */
    int eyecatch;               /* its large picture (72x56): 0 none, 1 16-bit colour, 2 256 colours, 3 16 */
    long data_bytes;
    int crc_stored, crc_computed;   /* computed -1: not checkable (a game: games do not use it) */
    int header_ok;
} CardInfo;
int  cardfs_info(const unsigned char *card, const CardFile *f, CardInfo *out);
/* The large picture as 72x56 ARGB4444 pixels (as the icons' palette), row by
 * row; its type, or 0 when it has none. After libevmu's
 * EvmuVms_createEyecatchArgb4444; no save with one has been seen here. */
int  cardfs_eyecatch(const unsigned char *card, const CardFile *f, unsigned short out[72 * 56]);

/* Imports. DCI: as exported (the words turned back). VMS: with its VMI when
 * there is one (name, date, game or not, protected or not); without, a save
 * when it starts with a save's header and a game otherwise, named name and
 * dated date (BCD, as a directory entry). CARDFS_OK, _SAME, _ROOM, _FAIL. */
int  cardfs_import_dci(unsigned char *card, const unsigned char *dci, int len);
int  cardfs_import_vms(unsigned char *card, const unsigned char *vms, int len, const unsigned char *vmi,
                       const char *name, const unsigned char date[8]);
int  cardfs_vms_is_game(const unsigned char *vms, int len);

/* Every save's blocks together: a game from block 0 up, the saves packed from
 * block 199 down, the bytes unchanged. The room for a game after it, or -1
 * (a save's blocks broken: check first). */
int  cardfs_defrag(unsigned char *card);

/* Checks a card: what is wrong, in words; repair puts right what can be put
 * right without losing anything (the root block's layout, the system blocks'
 * FAT entries, lost blocks freed) and leaves broken saves to be deleted. */
#define CARDFS_ISSUES 12
typedef struct {
    int n;
    char text[CARDFS_ISSUES][48];
    int repairable, broken, lost_blocks;
} CardCheck;
void cardfs_check(const unsigned char *card, CardCheck *out);
int  cardfs_repair(unsigned char *card, CardCheck *out);

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
