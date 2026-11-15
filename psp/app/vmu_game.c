/* See vmu_game.h. */
#include <pspkernel.h>
#include <psprtc.h>
#include <pspaudio.h>
#include <string.h>
#include <evmu/evmu_api.h>
#include <evmu/hw/evmu_device.h>
#include <gimbal/utils/gimbal_date_time.h>
#include "vmu_game.h"
#include "vmu_sa.h"

static EvmuDevice *g_dev;
static int g_running;
static unsigned char g_card_at_start[PSP2DC_CARD_BYTES];
static unsigned g_frame_us;

int game_running(void) { return g_running; }
unsigned game_frame_us(void) { return g_frame_us; }

int game_start(const unsigned char *card)
{
    size_t bytes = PSP2DC_CARD_BYTES;
    ScePspDateTime t;
    GblDateTime dt;
    if (!card) return -1;
    if (!g_dev && !(g_dev = EvmuDevice_create())) return -2;   /* (made once, kept) */
    memcpy(g_card_at_start, card, PSP2DC_CARD_BYTES);
    if (!GBL_RESULT_SUCCESS(EvmuFlash_writeBytes(g_dev->pFlash, 0, card, &bytes))) return -3;
    EvmuIBehavior_reset(EVMU_IBEHAVIOR(g_dev));
    memset(&t, 0, sizeof(t));                                  /* the VMU's clock: the PSP's */
    sceRtcGetCurrentClockLocalTime(&t);
    GblDateTime_set(&dt, t.year, t.month, t.day, t.hour, t.minute, t.second, 0, 0);
    EvmuRom_setDateTime(g_dev->pRom, &dt);
    g_running = 1;
    return 0;
}

void game_frame(unsigned held, unsigned ms)
{
    EvmuGamepad *p;
    unsigned t0;
    if (!g_running) return;
    p = g_dev->pGamepad;
    p->up = !!(held & SA_UP);       p->down = !!(held & SA_DOWN);
    p->left = !!(held & SA_LEFT);   p->right = !!(held & SA_RIGHT);
    p->a = !!(held & SA_A);         p->b = !!(held & SA_B);
    p->mode = !!(held & SA_MODE);   p->sleep = !!(held & SA_SLEEP);
    t0 = (unsigned)sceKernelGetSystemTimeLow();
    /* (EvmuTicks is documented as milliseconds but the CPU's update divides it
     * by 10^9: nanoseconds. Given ms, a frame ran a few instructions and games
     * never got past their start.) */
    EvmuIBehavior_update(EVMU_IBEHAVIOR(g_dev), (EvmuTicks)ms * 1000000u);
    g_frame_us = (unsigned)sceKernelGetSystemTimeLow() - t0;
}

unsigned game_lcd(unsigned char lcd[PSP2DC_LCD_BYTES])
{
    int x, y;
    memset(lcd, 0, PSP2DC_LCD_BYTES);
    if (!g_running) return 0;
    if (EvmuLcd_screenEnabled(g_dev->pLcd))
        for (y = 0; y < 32; y++)
            for (x = 0; x < 48; x++)
                /* (x, y): the header calls them row and col, the code takes x < 48, y < 32 */
                if (EvmuLcd_pixel(g_dev->pLcd, x, y)) {        /* stored turned half round, as the Dreamcast sends */
                    int sx = 47 - x, sy = 31 - y;
                    lcd[sy * 6 + sx / 8] |= 0x80 >> (sx & 7);
                }
    return (unsigned)EvmuLcd_icons(g_dev->pLcd) & 0x0F;      /* (the same four bits: file, game, clock, flash) */
}

int game_stop(unsigned char *card)
{
    size_t bytes = PSP2DC_CARD_BYTES;
    if (!g_running) return 0;
    g_running = 0;
    EvmuBuzzer_stopTone(g_dev->pBuzzer);
    if (!GBL_RESULT_SUCCESS(EvmuFlash_readBytes(g_dev->pFlash, 0, card, &bytes))) {
        memcpy(card, g_card_at_start, PSP2DC_CARD_BYTES);    /* (could not read it back: leave the card as it was) */
        return 0;
    }
    return memcmp(card, g_card_at_start, PSP2DC_CARD_BYTES) != 0;
}

int game_tone(unsigned *hz, unsigned *duty)
{
    uint16_t period = 0;
    uint8_t inv = 0;
    if (!g_running || !EvmuBuzzer_isActive(g_dev->pBuzzer)) return 0;
    EvmuBuzzer_tone(g_dev->pBuzzer, &period, &inv);
    if (!period) return 0;
    /* the period in the buzzer's time base, 183 us a count (libevmu's PCM wave) */
    *hz = 1000000u / (183u * period);
    *duty = inv >= period ? 128 : 256u * inv / period;
    return *hz >= 20 && *hz <= 20000;
}

/* ---- the buzzer ------------------------------------------------------------
 * A square wave of the game's tone, 44.1 kHz mono, made here (libevmu also
 * keeps a PCM wave of it; generating it directly avoids resampling). */
#define AUDIO_SAMPLES 512
static int audio_thread(SceSize args, void *argp)
{
    static short buf[AUDIO_SAMPLES] __attribute__((aligned(64)));
    unsigned phase = 0;                    /* 16.16 fixed point, one period = 1 << 16 */
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_SAMPLES, PSP_AUDIO_FORMAT_MONO);
    if (ch < 0) return 0;
    for (;;) {
        unsigned hz = 0, duty = 128;
        int i, on = game_tone(&hz, &duty);
        unsigned step = on ? (unsigned)(((unsigned long long)hz << 16) / 44100) : 0;
        for (i = 0; i < AUDIO_SAMPLES; i++) {
            if (on) {
                buf[i] = ((phase & 0xFFFF) >> 8) < duty ? 6000 : -6000;
                phase += step;
            } else buf[i] = 0;
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}

void game_audio_start(void)
{
    SceUID t = sceKernelCreateThread("buzzer", audio_thread, 0x12, 0x4000, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}
