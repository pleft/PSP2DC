/*
 * The memory cards: files in ms0:/PSP2DC/cards, any NAME.bin a card (a plain
 * 128 KB VMU image), so they can be named on a computer (saves.bin,
 * games.bin). One is in use at a time; its name is kept in the settings.
 */
#ifndef PSP2DC_CARDS_H
#define PSP2DC_CARDS_H

#define CARDS_DIR  "ms0:/PSP2DC/cards"
#define SAVES_DIR  "ms0:/PSP2DC/saves"   /* saves exported from a card */
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

#endif
