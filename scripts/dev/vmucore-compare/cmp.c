/* libevmu and vmu_core side by side on the same game and the same buttons */
#include <evmu/evmu_api.h>
#include <evmu/hw/evmu_device.h>
#include <evmu/fs/evmu_file_manager.h>
#include <gimbal/utils/gimbal_date_time.h>
#include "hw/evmu_device_.h"
#include "hw/evmu_ram_.h"
#include "hw/evmu_cpu_.h"
#include "hw/evmu_timers_.h"
#include "hw/evmu_pic_.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "vmu_core.h"
unsigned dbg_pc(const VmuCore *c);
const uint8_t *dbg_ram(const VmuCore *c);
const uint8_t *dbg_sfr(const VmuCore *c);
const uint8_t *dbg_xram(const VmuCore *c);
int dbg_ext_rom(const VmuCore *c);
void dbg_timers(const VmuCore *c, int *v);

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }

static unsigned script(int f, int mode_at)
{
    unsigned b = 0;
    if (f < 30) return 0;
    if (f % 90 < 6) b |= VMU_BTN_A;
    if (f % 300 >= 150 && f % 300 < 156) b |= VMU_BTN_B;
    if (f % 400 >= 100 && f % 400 < 130) b |= VMU_BTN_RIGHT;
    if (f % 400 >= 200 && f % 400 < 230) b |= VMU_BTN_LEFT;
    if (f % 520 >= 300 && f % 520 < 312) b |= VMU_BTN_UP;
    if (f % 520 >= 400 && f % 520 < 412) b |= VMU_BTN_DOWN;
    if (mode_at > 0 && f >= mode_at && f < mode_at + 10) b = VMU_BTN_MODE;
    return b;
}

static void show(EvmuDevice *d, VmuCore *c)
{
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 48; x++) putchar(EvmuLcd_pixel(d->pLcd, x, y) ? '#' : '.');
        printf("   ");
        for (int x = 0; x < 48; x++) putchar(vmu_core_pixel(c, x, y) ? '#' : '.');
        putchar('\n');
    }
}

static unsigned frame_us = 16667;
int main(int argc, char **argv)
{
    static uint8_t img[128 * 1024];
    const char *path = argv[1];
    if (getenv("FRAME_US")) frame_us = atoi(getenv("FRAME_US"));
    int seconds = argc > 2 ? atoi(argv[2]) : 60;
    int mode_at = argc > 3 ? atoi(argv[3]) * 60 : 0;      /* press MODE at this second */
    int frames = seconds * 60, first_screen = -1, first_state = -1, bad_frames = 0;
    double t_evmu = 0, t_core = 0, t0;
    size_t n = sizeof img;

    EvmuDevice *d = EvmuDevice_create();
    if (!GBL_RESULT_SUCCESS(EvmuFileManager_load(d->pFileMgr, path))) { printf("load failed\n"); return 1; }
    printf("game entry: %s\n", EvmuFileManager_game(d->pFileMgr) ? "yes" : "none");
    EvmuFlash_readBytes(d->pFlash, 0, img, &n);
    EvmuIBehavior_reset(EVMU_IBEHAVIOR(d));
    GblDateTime dt;
    GblDateTime_set(&dt, 2026, 3, 14, 12, 0, 0, 0, 0);
    EvmuRom_setDateTime(d->pRom, &dt);

    VmuCore *c = vmu_core_create();
    vmu_core_load_flash(c, img, n);
    vmu_core_reset(c);
    vmu_core_set_datetime(c, 2026, 3, 14, 12, 0, 0);

    EvmuRam_ *er = EVMU_DEVICE_(d)->pRam;
    for (int f = 0; f < frames; f++) {
        unsigned b = script(f, mode_at);
        EvmuGamepad *p = d->pGamepad;
        p->up = !!(b & 1); p->down = !!(b & 2); p->left = !!(b & 4); p->right = !!(b & 8);
        p->a = !!(b & 16); p->b = !!(b & 32); p->mode = !!(b & 64); p->sleep = !!(b & 128);
        vmu_core_set_buttons(c, b);
        t0 = now_us(); EvmuIBehavior_update(EVMU_IBEHAVIOR(d), frame_us * 1000ull); t_evmu += now_us() - t0;
        t0 = now_us(); vmu_core_run(c, frame_us); t_core += now_us() - t0;

        int screen_ok = 1;
        for (int y = 0; y < 32 && screen_ok; y++)
            for (int x = 0; x < 48; x++)
                if (EvmuLcd_pixel(d->pLcd, x, y) != vmu_core_pixel(c, x, y)) { screen_ok = 0; break; }
        if ((unsigned)EvmuLcd_icons(d->pLcd) != vmu_core_icons(c)) screen_ok = 0;
        if (!!EvmuLcd_screenEnabled(d->pLcd) != vmu_core_screen_on(c)) screen_ok = 0;
        uint16_t per = 0; uint8_t inv = 0; unsigned cper = 0, cinv = 0;
        int ea = EvmuBuzzer_isActive(d->pBuzzer), ca = vmu_core_tone(c, &cper, &cinv);
        EvmuBuzzer_tone(d->pBuzzer, &per, &inv);
        if (ea != ca || (ea && (per != cper || inv != cinv))) screen_ok = 0;
        if (!screen_ok) {
            bad_frames++;
            if (first_screen < 0) {
                first_screen = f;
                printf("FIRST SCREEN/ICON/TONE MISMATCH at frame %d (%.2f s): icons %x/%x screen %d/%d tone %d:%u/%u vs %d:%u/%u\n",
                       f, f / 60.0, (unsigned)EvmuLcd_icons(d->pLcd), vmu_core_icons(c),
                       EvmuLcd_screenEnabled(d->pLcd), vmu_core_screen_on(c), ea, per, inv, ca, cper, cinv);
                printf("libevmu                                            vmu_core\n");
                show(d, c);
            }
        }
        if (first_state < 0) {
            int tv[6];
            dbg_timers(c, tv);
            const EvmuTimers_ *et = EVMU_DEVICE_(d)->pTimers;
            const char *what = NULL;
            if (EvmuCpu_pc(d->pCpu) != dbg_pc(c)) what = "pc";
            else if (memcmp(er->ram, dbg_ram(c), 512)) what = "ram";
            else if (memcmp(er->sfr, dbg_sfr(c), 128)) what = "sfr";
            else if (memcmp(er->xram, dbg_xram(c), 384)) what = "xram";
            else if (et->timer0.base.tl != tv[0] || et->timer0.base.th != tv[1] || et->timer1.base.tl != tv[2] ||
                     et->timer1.base.th != tv[3] || et->baseTimer.counter != tv[4]) what = "timers";
            else if (EVMU_DEVICE_(d)->pPic->intReq != tv[5]) what = "irq";
            if (what) {
                first_state = f;
                printf("first state difference at frame %d (%.2f s): %s; pc %04x/%04x\n", f, f / 60.0, what,
                       (unsigned)EvmuCpu_pc(d->pCpu), dbg_pc(c));
                if (!strcmp(what, "sfr"))
                    for (int i = 0; i < 128; i++) if (er->sfr[i] != dbg_sfr(c)[i]) printf("  sfr %03x: %02x / %02x\n", 0x100 + i, er->sfr[i], dbg_sfr(c)[i]);
                if (!strcmp(what, "ram"))
                    for (int i = 0; i < 512; i++) if (((uint8_t *)er->ram)[i] != dbg_ram(c)[i]) printf("  ram[%d][%02x]: %02x / %02x\n", i >> 8, i & 0xff, ((uint8_t *)er->ram)[i], dbg_ram(c)[i]);
            }
        }
    }
    size_t m = sizeof img;
    static uint8_t img2[128 * 1024];
    EvmuFlash_readBytes(d->pFlash, 0, img2, &m);
    int flash_diff = 0;
    for (size_t i = 0; i < m; i++) flash_diff += img2[i] != vmu_core_flash(c)[i];
    int flash_changed = memcmp(img, img2, m) != 0;
    printf("RESULT %s: %d s, screen mismatches in %d of %d frames (first %d), state first differs %d, flash %s and %d bytes differ, bios_exit %d (libevmu pc %04x)\n",
           path, seconds, bad_frames, frames, first_screen, first_state,
           flash_changed ? "changed" : "unchanged", flash_diff, vmu_core_bios_exit(c), (unsigned)EvmuCpu_pc(d->pCpu));
    printf("TIME libevmu %.0f us per emulated s, vmu_core %.0f us per emulated s, ratio %.1f\n",
           t_evmu / seconds, t_core / seconds, t_evmu / t_core);
    printf("final screens:\n");
    show(d, c);
    return 0;
}
