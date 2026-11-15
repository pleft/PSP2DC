/*
 * The memory cards: files in ms0:/PSP2DC/cards, any NAME.bin a card (a plain
 * 128 KB VMU image), so they can be named on a computer (saves.bin,
 * games.bin). One is in use at a time; its name is kept in the settings.
 */
#ifndef PSP2DC_CARDS_H
#define PSP2DC_CARDS_H

#define CARDS_DIR  "ms0:/PSP2DC/cards"
#define SAVES_DIR  "ms0:/PSP2DC/saves"   /* saves exported from a card, and to import */
#define BACKUPS_DIR "ms0:/PSP2DC/backups" /* cards backed up: NAME@YYYYMMDD-HHMMSS[-auto].bin */
#define BACKUPS_MAX 64
#define BACKUPS_AUTO_KEEP 10              /* automatic backups kept a card (the newest) */
#define CARDS_MAX  32
#define CARD_NAME_MAX 32

/* Moves the single card of earlier versions (ms0:/PSP2DC/card1.bin) into the
 * folder, once. */
void cards_migrate(void);
/* The cards' names (without .bin), sorted; returns how many. */
int  cards_list(char names[CARDS_MAX][CARD_NAME_MAX]);
/* Read a card into img (128 KB). 1 when read, 0 when not there or short. A
 * write cut after its last byte but before the rename (NAME.new) counts. */
int  cards_read(const char *name, unsigned char *img);
/* Write it: to NAME.new first, then renamed, so a write cut short leaves the
 * last good file. 0 or < 0. */
int  cards_write(const char *name, const unsigned char *img);
/* A new, freshly formatted card under the first free name cardN; its name in
 * out. 0 or < 0. */
int  cards_new(char out[CARD_NAME_MAX]);
/* A name no card has yet: base and suffix (card1-dc), then a number
 * (card1-dc2...). 0 or < 0. */
int  cards_unique(const char *base, const char *suffix, char out[CARD_NAME_MAX]);
/* A formatted empty card, as a Dreamcast formats one (and the adapter's
 * preformat). */
void cards_format(unsigned char *img);

/* One save out of a card into SAVES_DIR, in the two formats other tools load:
 * BASE.DCI (Nexus), and BASE.VMS with BASE.VMI (as the Dreamcast's browser
 * downloaded them); BASE from the save's file name, numbered rather than
 * written over anything. 0 or < 0; the name in base. */
struct CardFileTag;
int  cards_export(const unsigned char *card, const struct CardFileTag *f, char base[9]);

/* Backups. The card's file (img NULL) or img as it is now, under the card's
 * name and the time; automatic ones (before something that changes a card)
 * marked so, and only the newest BACKUPS_AUTO_KEEP of a card kept. 0 or < 0. */
typedef struct {
    char file[64];
    char card[CARD_NAME_MAX];
    char when[20];                         /* "YYYY-MM-DD HH:MM" */
    char stamp[16];                        /* "YYYYMMDD-HHMMSS", for sorting */
    int automatic;
} CardBackup;
int  cards_backup(const char *name, const unsigned char *img, int automatic);
int  cards_backups(CardBackup *out, int max);   /* all of them, newest first */
int  cards_backup_read(const CardBackup *b, unsigned char *img);

/* Card files: a name is letters, digits, - and _ (CARD_NAME_MAX - 1 at
 * most). Rename: 0, -1, or -2 when that name is taken. */
int  cards_name_ok(const char *name);
int  cards_rename(const char *from, const char *to);
int  cards_delete(const char *name);
int  cards_duplicate(const char *name, char out[CARD_NAME_MAX]);   /* as NAME-copy */

/* Saves to import from SAVES_DIR: .DCI files, and .VMS files (with their .VMI
 * when there is one); by name. Read: the file into data (its length, or -1)
 * and its VMI into vmi when it has one. */
typedef struct {
    char file[64];
    int dci, vmi;
    long size;
} CardImport;
int  cards_imports(CardImport *out, int max);
int  cards_import_read(const CardImport *im, unsigned char *data, int max, unsigned char vmi[108]);

#endif
