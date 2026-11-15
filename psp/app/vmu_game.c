/* See vmu_game.h. The emulated VMU is vmu_core.c, the lean core derived from
 * libevmu (it matched libevmu frame for frame on 127 games, ~90x faster:
 * libevmu itself ran ~25x too slow on the PSP, its object system on every
 * instruction). */
#include <pspkernel.h>
#include <psprtc.h>
#include <pspaudio.h>
#include <string.h>
#include "vmu_core.h"
#include "vmu_game.h"
#include "vmu_sa.h"

static VmuCore *g_core;
static int g_running;
static unsigned char g_card_at_start[PSP2DC_CARD_BYTES];
static unsigned g_frame_us;

int game_running(void) { return g_running; }
unsigned game_frame_us(void) { return g_frame_us; }

int game_start(const unsigned char *card)
{
    ScePspDateTime t;
    if (!card) return -1;
    if (!g_core && !(g_core = vmu_core_create())) return -2;   /* (made once, kept) */
    memcpy(g_card_at_start, card, PSP2DC_CARD_BYTES);
    vmu_core_load_flash(g_core, card, PSP2DC_CARD_BYTES);
    vmu_core_reset(g_core);
    memset(&t, 0, sizeof(t));                                  /* the VMU's clock: the PSP's */
    sceRtcGetCurrentClockLocalTime(&t);
    vmu_core_set_datetime(g_core, t.year, t.month, t.day, t.hour, t.minute, t.second);
    g_running = 1;
    return 0;
}

/* (The SA_* bits are the core's VMU_BTN_* bits.) */
void game_frame(unsigned held, unsigned ms)
{
    unsigned t0;
    if (!g_running) return;
    vmu_core_set_buttons(g_core, held & 0xFF);
    t0 = (unsigned)sceKernelGetSystemTimeLow();
    vmu_core_run(g_core, ms * 1000u);
    g_frame_us = (unsigned)sceKernelGetSystemTimeLow() - t0;
}

/* The game handed back to the BIOS (MODE in most games, or at its own end):
 * libevmu and the core both leave the CPU idling in the empty ROM. */
int game_ended(void) { return g_running && vmu_core_bios_exit(g_core) >= 0; }

unsigned game_lcd(unsigned char lcd[PSP2DC_LCD_BYTES])
{
    int x, y;
    memset(lcd, 0, PSP2DC_LCD_BYTES);
    if (!g_running) return 0;
    if (vmu_core_screen_on(g_core))
        for (y = 0; y < 32; y++)
            for (x = 0; x < 48; x++)
                if (vmu_core_pixel(g_core, x, y)) {             /* stored turned half round, as the Dreamcast sends */
                    int sx = 47 - x, sy = 31 - y;
                    lcd[sy * 6 + sx / 8] |= 0x80 >> (sx & 7);
                }
    return vmu_core_icons(g_core) & 0x0F;
}

int game_stop(unsigned char *card)
{
    if (!g_running) return 0;
    g_running = 0;
    memcpy(card, vmu_core_flash(g_core), PSP2DC_CARD_BYTES);
    return memcmp(card, g_card_at_start, PSP2DC_CARD_BYTES) != 0;
}

int game_tone(unsigned *hz, unsigned *duty)
{
    unsigned period = 0, inv = 0;
    if (!g_running || !vmu_core_tone(g_core, &period, &inv) || !period) return 0;
    /* the period in the buzzer's time base, 183 us a count (as libevmu's PCM wave) */
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
