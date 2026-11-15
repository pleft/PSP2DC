/*
 * A VMU (Sanyo LC86K CPU, timers, buttons, screen, buzzer) emulated in plain
 * C, with the software BIOS standing in for Sega's. Its behaviour is that of
 * libevmu (MIT), from which it is derived; see vmu_core.c. No floating point,
 * no heap after vmu_core_create, no other library.
 */
#ifndef PSP2DC_VMU_CORE_H
#define PSP2DC_VMU_CORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct VmuCore VmuCore;

#define VMU_CORE_FLASH_BYTES (128 * 1024)

/* the buttons, as vmu_core_set_buttons takes them */
#define VMU_BTN_UP    0x01
#define VMU_BTN_DOWN  0x02
#define VMU_BTN_LEFT  0x04
#define VMU_BTN_RIGHT 0x08
#define VMU_BTN_A     0x10
#define VMU_BTN_B     0x20
#define VMU_BTN_MODE  0x40
#define VMU_BTN_SLEEP 0x80

/* the icons, as vmu_core_icons gives them */
#define VMU_ICON_FILE  0x1
#define VMU_ICON_GAME  0x2
#define VMU_ICON_CLOCK 0x4
#define VMU_ICON_FLASH 0x8

VmuCore *vmu_core_create(void);                  /* NULL when out of memory */
void     vmu_core_destroy(VmuCore *c);

/* the whole card (a VMU file system with the game at block 0); a shorter
 * image is padded with zeroes */
void     vmu_core_load_flash(VmuCore *c, const uint8_t *img, size_t bytes);
/* power on: the game starts at 0, as the BIOS leaves it; the buttons are released */
void     vmu_core_reset(VmuCore *c);
/* the VMU's clock (the BIOS keeps it in RAM); after vmu_core_reset */
void     vmu_core_set_datetime(VmuCore *c, int year, int month, int day,
                               int hour, int min, int sec);
void     vmu_core_set_buttons(VmuCore *c, unsigned bits);   /* VMU_BTN_*, 1 = held */
/* advance the VMU by this much of its time */
void     vmu_core_run(VmuCore *c, uint32_t microseconds);

int      vmu_core_pixel(const VmuCore *c, int x, int y);    /* x < 48, y < 32; 1 = dark */
void     vmu_core_lcd(const VmuCore *c, uint8_t out[32][48]);
unsigned vmu_core_icons(const VmuCore *c);                  /* VMU_ICON_* */
int      vmu_core_screen_on(const VmuCore *c);
/* the buzzer: 0 when silent, else 1 with its period and the length of its
 * low part, both in counts of the 1/6 quartz cycle (183 us) */
int      vmu_core_tone(const VmuCore *c, unsigned *period, unsigned *inv_pulse);
const uint8_t *vmu_core_flash(const VmuCore *c);            /* VMU_CORE_FLASH_BYTES */
/* -1 while the game runs; once it has left (MODE's exit, SLEEP, or any BIOS
 * entry the emulated BIOS does not provide) the BIOS address it called. The
 * VMU then idles in the BIOS and the screen keeps its last picture. */
int      vmu_core_bios_exit(const VmuCore *c);

#endif
