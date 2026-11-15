/*
 * See vmu_core.h.
 *
 * Derived from libevmu, the ElysianVMU emulator core
 * (https://github.com/gyrovorbis/libevmu, commit 2b155021): its CPU, memory,
 * interrupt controller, timers, buttons, buzzer and software BIOS, rewritten
 * without libgimbal and without floating point, to behave the same instruction
 * for instruction. Where libevmu does something a real VMU might not (time
 * left over at the end of a step is dropped, a halted CPU ticks the timers
 * with the last instruction's cycles), this does the same, so the two can be
 * compared frame by frame.
 *
 * libevmu's licence:
 *
 * MIT License
 *
 * Copyright (c) 2023 Falco Girgis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <stdlib.h>
#include <string.h>
#include "vmu_core.h"

/* the special function registers used here (0x100 + offset in sfr[]) */
enum {
    ACC = 0x100, PSW = 0x101, B = 0x102, C = 0x103, TRL = 0x104, TRH = 0x105,
    SP = 0x106, PCON = 0x107, IE = 0x108, IP = 0x109, EXT = 0x10d, OCR = 0x10e,
    T0CNT = 0x110, T0PRR = 0x111, T0L = 0x112, T0LR = 0x113, T0H = 0x114, T0HR = 0x115,
    T1CNT = 0x118, T1LC = 0x11a, T1L = 0x11b, T1LR = 0x11b, T1HC = 0x11c, T1H = 0x11d, T1HR = 0x11d,
    MCR = 0x120, XBNK = 0x125, VCCR = 0x127, P1 = 0x144, P1DDR = 0x145, P1FCR = 0x146,
    P3 = 0x14c, P3DDR = 0x14d, P3INT = 0x14e, FPR = 0x154, P7 = 0x15c, ISL = 0x15f,
    VSEL = 0x163, VRMAD1 = 0x164, VRMAD2 = 0x165, VTRBF = 0x166, BTCR = 0x17f
};
#define R(a) c->sfr[(a) - 0x100]                 /* a register as stored */

/* the BIOS's variables (RAM bank 0) and the game's flash call block (bank 1) */
enum {
    SYS_YEAR_MSB_BCD = 0x10, SYS_YEAR_LSB_BCD, SYS_MONTH_BCD, SYS_DAY_BCD,
    SYS_HOUR_BCD, SYS_MINUTE_BCD, SYS_SEC_BCD, SYS_YEAR_MSB, SYS_YEAR_LSB,
    SYS_MONTH, SYS_DAY, SYS_HOUR, SYS_MINUTE, SYS_SEC, SYS_HALF_SEC, SYS_LEAP_YEAR,
    SYS_DATE_SET = 0x31,
    USR_FMBANK = 0x7d, USR_FMADD_H = 0x7e, USR_FMADD_L = 0x7f, USR_FM_BUF = 0x80
};

/* the interrupt sources, as libevmu numbers them */
enum { IRQ_INT0 = 1, IRQ_INT1 = 2, IRQ_INT2_T0L = 3, IRQ_INT3_TBASE = 4, IRQ_T0H = 5,
       IRQ_T1 = 6, IRQ_SIO0 = 7, IRQ_SIO1 = 8, IRQ_P3 = 10, IRQ_COUNT = 16 };

/* per first byte: the instruction it is, and its length << 4 | cycles
 * (dumped from libevmu's EvmuIsa_format) */
static const uint8_t op_of[256] = {
0x00, 0x01, 0x02, 0x02, 0x04, 0x04, 0x04, 0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
0x10, 0x11, 0x12, 0x12, 0x14, 0x14, 0x14, 0x14, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
0x20, 0x21, 0x22, 0x22, 0x24, 0x24, 0x24, 0x24, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
0x30, 0x31, 0x32, 0x32, 0x34, 0x34, 0x34, 0x34, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
0x40, 0x41, 0x42, 0x42, 0x44, 0x44, 0x44, 0x44, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
0x50, 0x51, 0x52, 0x52, 0x54, 0x54, 0x54, 0x54, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
0x60, 0x60, 0x62, 0x62, 0x64, 0x64, 0x64, 0x64, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68,
0x70, 0x70, 0x72, 0x72, 0x74, 0x74, 0x74, 0x74, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68,
0x80, 0x81, 0x82, 0x82, 0x84, 0x84, 0x84, 0x84, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
0x90, 0x91, 0x92, 0x92, 0x94, 0x94, 0x94, 0x94, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
0xa0, 0xa1, 0xa2, 0xa2, 0xa4, 0xa4, 0xa4, 0xa4, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8,
0xb0, 0xb1, 0xb2, 0xb2, 0xb4, 0xb4, 0xb4, 0xb4, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8,
0xc0, 0xc1, 0xc2, 0xc2, 0xc4, 0xc4, 0xc4, 0xc4, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8,
0xd0, 0xd1, 0xd2, 0xd2, 0xd4, 0xd4, 0xd4, 0xd4, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8, 0xc8,
0xe0, 0xe1, 0xe2, 0xe2, 0xe4, 0xe4, 0xe4, 0xe4, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8,
0xf0, 0xf1, 0xf2, 0xf2, 0xf4, 0xf4, 0xf4, 0xf4, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8,
};
static const uint8_t len_cc[256] = {
0x11, 0x22, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
0x34, 0x34, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
0x32, 0x32, 0x32, 0x32, 0x21, 0x21, 0x21, 0x21, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
0x17, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
0x17, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
0x12, 0x12, 0x32, 0x32, 0x22, 0x22, 0x22, 0x22, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
0x22, 0x22, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
0x22, 0x22, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
0x22, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
0x22, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
0x12, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
0x12, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
0x11, 0x12, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
0x11, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
0x11, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
0x11, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
};

static const uint16_t isr_addr[IRQ_COUNT] = {
    0x00, 0x03, 0x0b, 0x13, 0x1b, 0x23, 0x2b, 0x33,
    0x3b, 0x43, 0x4b, 0x4f, 0x52, 0x55, 0x5a, 0x5d
};

struct VmuCore {
    uint8_t  flash[VMU_CORE_FLASH_BYTES];
    uint8_t  ram[2][256];
    uint8_t  sfr[128];
    uint8_t  xram[3][128];
    uint8_t  wram[512];
    uint8_t *seg[4];             /* what 0x000, 0x080, 0x100, 0x180 map to */
    int      ext_rom;            /* running from the (empty) ROM: the BIOS */
    uint16_t pc;
    uint8_t  cc;                 /* cycles of the last instruction fetched */
    unsigned buttons;
    int      bios_exit;
    /* interrupt controller */
    uint16_t int_req;
    uint16_t int_stack[3];
    uint8_t  int_process;
    uint8_t  int_prev_prio;
    /* timers */
    int      t0l, t0h, t0base, t0scale;
    unsigned t0delay;
    int      t1l, t1h;
    unsigned t1delay;
    uint16_t bt_counter;
    uint32_t bt_rem;
    unsigned bt_delay;
    /* buzzer */
    int      bz_active;
    uint8_t  bz_t1lr, bz_t1lc;
    unsigned bz_period, bz_inv;
    /* flash programming (STF) sequence; a reset leaves it, as libevmu does */
    unsigned prg_state;
};

static int parity(unsigned v)
{
    v ^= v >> 4;
    return (0x6996 >> (v & 0xf)) & 1;
}

/* ---- the buzzer -----------------------------------------------------------
 * Output only: what libevmu's EvmuBuzzer would be playing. Its PCM buffer is
 * flat (silent) when the low part is 0 or the whole period. */

static void bz_set_tone(VmuCore *c, unsigned period, unsigned inv)
{
    if (c->bz_period == period && c->bz_inv == inv) return;
    if (period < inv) period = inv;
    c->bz_period = period;
    c->bz_inv = inv;
    if (c->bz_active && (!inv || inv == period)) c->bz_active = 0;
}

static void bz_stop(VmuCore *c)
{
    if (c->bz_active) bz_set_tone(c, 255, 255);
}

static int bz_configured(const VmuCore *c)
{
    return (R(P1DDR) & 0x80) && (R(P1FCR) & 0x80) && !(R(P1) & 0x80) && !(R(T1CNT) & 0x20);
}

static void bz_update_tone(VmuCore *c)
{
    bz_set_tone(c, 256u - c->bz_t1lr, (uint8_t)(c->bz_t1lc - c->bz_t1lr));
}

/* T1L overflowed in mode 1: a new PWM cycle takes the reload, and the compare
 * value only when ELDT1C allows it */
static void bz_t1_reload(VmuCore *c)
{
    c->bz_t1lr = R(T1LR);
    if (R(T1CNT) & 0x10) c->bz_t1lc = R(T1LC);
    if (bz_configured(c)) bz_update_tone(c);
}

static void bz_sink(VmuCore *c, unsigned addr)
{
    int running;
    if (addr != T1LR && addr != T1LC && addr != T1CNT && addr != P1DDR && addr != P1FCR && addr != P1)
        return;
    running = R(T1CNT) & 0x40;
    if (!running) { c->bz_t1lr = R(T1LR); c->bz_t1lc = R(T1LC); }
    if (!bz_configured(c) || !running) {
        bz_stop(c);
    } else {
        /* (the reload and compare of a running timer wait for its next overflow) */
        if (addr != T1LR && addr != T1LC) bz_update_tone(c);
        if (!c->bz_active) c->bz_active = 1;
    }
}

/* ---- memory --------------------------------------------------------------- */

static unsigned port3(const VmuCore *c)
{
    return (R(P3DDR) | (uint8_t)~R(P3) | (~c->buttons & 0xff)) & 0xff;
}

static unsigned wram_addr(const VmuCore *c)
{
    return (R(VRMAD2) & 1) << 8 | R(VRMAD1);
}

static void wram_step(VmuCore *c)
{
    if (R(VSEL) & 0x10)
        if (!++R(VRMAD1)) R(VRMAD2) ^= 1;
}

static unsigned read_data(VmuCore *c, unsigned addr)
{
    unsigned v;
    if (addr < 0x100 || (addr >= 0x180 && addr < 0x200)) return c->seg[addr >> 7][addr & 0x7f];
    switch (addr) {
    case P1DDR: case P1FCR: case P3DDR: case MCR: case VCCR:
        return 0xff;                                      /* write-only */
    case VTRBF:
        v = c->wram[wram_addr(c)];
        wram_step(c);
        return v;
    case T0L: return (uint8_t)c->t0l;
    case T0H: return (uint8_t)c->t0h;
    case T1L: return (uint8_t)c->t1l;
    case T1H: return (uint8_t)c->t1h;
    case VRMAD2: return 0xfe | (R(VRMAD2) & 1);
    case P1: return 0;
    case P3: return port3(c);
    case P7: return 0xf0 | R(P7);
    case OCR: return 0x4c | R(OCR);
    default:
        if (addr >= 0x200) return 0;
        return c->sfr[addr - 0x100];
    }
}

/* the latches, not the pins, for read-modify-write instructions */
static unsigned read_latch(VmuCore *c, unsigned addr)
{
    if (addr == T1L || addr == T1H || addr == P1 || addr == P3 || addr == P7)
        return c->sfr[addr - 0x100];
    return read_data(c, addr);
}

static void set_acc(VmuCore *c, unsigned v)
{
    v &= 0xff;
    if (R(ACC) != v) R(PSW) = (R(PSW) & 0xfe) | parity(v);
    R(ACC) = v;
}

static uint8_t ext_byte(const VmuCore *c, unsigned addr)
{
    return c->ext_rom ? 0 : c->flash[addr & 0x1ffff];
}

static void write_data(VmuCore *c, unsigned addr, unsigned val)
{
    val &= 0xff;
    if (addr < 0x100) { c->seg[addr >> 7][addr & 0x7f] = val; return; }
    switch (addr) {
    case ACC: set_acc(c, val); return;
    case VTRBF:
        c->wram[wram_addr(c)] = val;
        wram_step(c);
        return;
    case VRMAD2: val &= 1; break;
    case EXT:
        if ((R(EXT) & 1) != (val & 1)) {
            /* the program memory changes under the CPU: the JMPF that must
             * follow is taken now */
            if (ext_byte(c, c->pc) == 0x21)
                c->pc = ext_byte(c, c->pc + 1u) << 8 | ext_byte(c, c->pc + 2u);
            c->ext_rom = !(val & 1);
        }
        break;
    case XBNK:
        if (R(XBNK) != val) {
            if (val > 2) return;                 /* (libevmu refuses the whole write) */
            c->seg[3] = c->xram[val];
        }
        break;
    case PSW:
        if ((R(PSW) ^ val) & 0x02) {
            c->seg[0] = c->ram[(val & 2) >> 1];
            c->seg[1] = c->ram[(val & 2) >> 1] + 0x80;
        }
        break;
    case T0PRR:
        c->t0scale = 256 - (int)val;
        c->t0base = 0;
        c->bt_rem = 0;
        break;
    case T0CNT: {
        unsigned prev = R(T0CNT) & 0xc0, next = val & 0xc0;
        if (!prev && next) c->t0delay = 1;
        else if (!next) c->t0delay = 0;
        if (!(val & 0x40)) c->t0l = R(T0LR);
        if (!(val & 0x80)) c->t0h = R(T0HR);
        break;
    }
    case T0LR: case T0L:
        if (!(R(T0CNT) & 0x40)) c->t0l = val;
        break;
    case T0HR: case T0H:
        if (!(R(T0CNT) & 0x80)) c->t0h = val;
        break;
    case T1CNT: {
        unsigned prev = R(T1CNT) & 0xc0, next = val & 0xc0;
        if (!prev && next) c->t1delay = 1;
        else if (!next) c->t1delay = 0;
        if (!(val & 0x40)) c->t1l = R(T1LR);
        if (!(val & 0x80)) c->t1h = R(T1HR);
        break;
    }
    case T1LR:                                   /* (also T1L) */
        if (!(R(T1CNT) & 0x40)) c->t1l = val;
        break;
    case T1HR:                                   /* (also T1H) */
        if (!(R(T1CNT) & 0x80)) c->t1h = val;
        break;
    case BTCR: {
        int was = R(BTCR) & 0x40, is = val & 0x40;
        if (!was && is) c->bt_delay = c->cc;
        else if (!is) c->bt_delay = 0;
        if (!is) c->bt_counter = 0;
        break;
    }
    case ISL: case OCR:
        c->bt_rem = 0;
        break;
    default:
        break;
    }
    if (addr >= 0x200) return;
    if (addr >= 0x180) {
        if (addr <= 0x1fb && (R(VCCR) & 0x40)) goto sink;   /* XRAM locked */
        c->seg[3][addr & 0x7f] = val;
        return;                                  /* (no buzzer register up here) */
    }
    c->sfr[addr - 0x100] = val;
sink:
    bz_sink(c, addr);
}

static unsigned indirect(VmuCore *c, unsigned mode)
{
    return read_data(c, mode | ((R(PSW) & 0x18) >> 1)) | (mode & 2) << 7;
}

static void push(VmuCore *c, unsigned v)
{
    c->ram[0][++R(SP)] = v;
}

static unsigned pop(VmuCore *c)
{
    return c->ram[0][R(SP)--];
}

static void set_psw_bit(VmuCore *c, unsigned mask, int on)
{
    write_data(c, PSW, (read_data(c, PSW) & ~mask) | (on ? mask : 0));
}

/* ---- the emulated BIOS ---------------------------------------------------- */

static int leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m)
{
    static const uint8_t d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && leap_year(y) ? 29 : d[(m - 1) % 12];
}

static int datetime_valid(int y, int mo, int d, int h, int mi, int s)
{
    return y >= 0 && y <= 9999 && mo >= 1 && mo <= 12 && d >= 1 && d <= days_in_month(y, mo)
        && h >= 0 && h < 24 && mi >= 0 && mi < 60 && s >= 0 && s < 60;
}

#define BCD(n) ((((n) / 10) << 4) | ((n) % 10))

static void bios_set_datetime(VmuCore *c, int y, int mo, int d, int h, int mi, int s)
{
    uint8_t *r = c->ram[0];
    if (!datetime_valid(y, mo, d, h, mi, s)) return;
    r[SYS_YEAR_MSB_BCD] = BCD(y / 100);
    r[SYS_YEAR_LSB_BCD] = BCD(y % 100);
    r[SYS_MONTH_BCD] = BCD(mo);
    r[SYS_DAY_BCD] = BCD(d);
    r[SYS_HOUR_BCD] = BCD(h);
    r[SYS_MINUTE_BCD] = BCD(mi);
    r[SYS_SEC_BCD] = BCD(s);
    r[SYS_YEAR_MSB] = y >> 8;
    r[SYS_YEAR_LSB] = y & 0xff;
    r[SYS_MONTH] = mo;
    r[SYS_DAY] = d;
    r[SYS_HOUR] = h;
    r[SYS_MINUTE] = mi;
    r[SYS_SEC] = s;
    r[SYS_HALF_SEC] = 0;
    r[SYS_LEAP_YEAR] = leap_year(y);
    r[SYS_DATE_SET] = 0xff;
}

/* a second later, carried through the calendar as libevmu's mktime does */
static void bios_tick_second(VmuCore *c)
{
    const uint8_t *r = c->ram[0];
    int y = r[SYS_YEAR_MSB] << 8 | r[SYS_YEAR_LSB], mo = r[SYS_MONTH], d = r[SYS_DAY];
    int h = r[SYS_HOUR], mi = r[SYS_MINUTE], s = r[SYS_SEC] + 1;
    if (mo < 1) mo = 1;
    if (d < 1) d = 1;
    mi += s / 60; s %= 60;
    h += mi / 60; mi %= 60;
    d += h / 24; h %= 24;
    for (;;) {
        while (mo > 12) { mo -= 12; y++; }
        if (d <= days_in_month(y, mo)) break;
        d -= days_in_month(y, mo);
        mo++;
    }
    bios_set_datetime(c, y, mo, d, h, mi, s);
}

/* the size in blocks of the card's GAME file, from its directory */
static int game_blocks(const VmuCore *c)
{
    const uint8_t *root = c->flash + 255 * 512;
    unsigned dir_last = root[0x4a] | root[0x4b] << 8, dir_size = root[0x4c] | root[0x4d] << 8;
    unsigned first = dir_last - dir_size + 1, e;
    for (e = 0; e < dir_size * 512 / 32; e++) {
        unsigned off = first * 512 + e * 32;
        if (off + 32 > VMU_CORE_FLASH_BYTES) break;
        if (c->flash[off] == 0xcc) return c->flash[off + 0x18] | c->flash[off + 0x19] << 8;
    }
    return 0;
}

/* FM_WRT_EX: 128 bytes from the game's RAM into its own blocks (bank 0) */
static void bios_write_flash(VmuCore *c)
{
    unsigned bank = c->ram[1][USR_FMBANK], hi = c->ram[1][USR_FMADD_H];
    unsigned lo = c->ram[1][USR_FMADD_L] & ~127u;
    int last = game_blocks(c) - 1, i;
    if (bank || last <= 0 || last >= 128 || (int)hi > last * 2 + 1) {
        write_data(c, ACC, 0xff);
        return;
    }
    write_data(c, ACC, 0);
    for (i = 0; i < 128; i++) c->flash[(hi << 8 | lo) + i] = c->ram[1][USR_FM_BUF + i];
}

static unsigned fm_addr(const VmuCore *c)
{
    return (c->ram[1][USR_FMBANK] << 16 | c->ram[1][USR_FMADD_H] << 8 | c->ram[1][USR_FMADD_L]) & 0x1ffff;
}

/* the BIOS routine at pc, done here; where the game resumes, or 0 for none */
static unsigned call_bios(VmuCore *c, unsigned pc)
{
    unsigned a, i, r = 0;
    switch (pc) {
    case 0x100:                                  /* FM_WRT_EX */
        bios_write_flash(c);
        return 0x105;
    case 0x108:                                  /* FM_WRTA_EX */
        bios_write_flash(c);
        return 0x10b;
    case 0x110:                                  /* FM_VRF_EX */
        a = fm_addr(c);
        for (i = 0; i < 0x80; i++)
            if ((r = c->flash[(a & ~0xffu) | ((a + i) & 0xff)] ^ c->ram[1][USR_FM_BUF + i]) != 0) break;
        write_data(c, ACC, r);
        return 0x115;
    case 0x120:                                  /* FM_PRD_EX */
        a = fm_addr(c);
        for (i = 0; i < 0x80; i++) c->ram[1][USR_FM_BUF + i] = c->flash[(a & ~0xffu) | ((a + i) & 0xff)];
        return 0x125;
    case 0x130:                                  /* TIMER_EX: the clock, every other half second */
        if (!((c->ram[0][SYS_HALF_SEC] ^= 1) & 1)) bios_tick_second(c);
        return 0x139;
    default:
        /* RESET, SLEEP_EX, EXIT_EX (MODE) and anything else: libevmu has
         * nothing there, and the CPU idles in the empty ROM from now on */
        if (c->bios_exit < 0) c->bios_exit = (int)pc;
        return 0;
    }
}

/* ---- interrupts ----------------------------------------------------------- */

static unsigned irqs_enabled(const VmuCore *c, int prio)
{
    unsigned ie = R(IE), ip = R(IP), mask = 0, inv = !prio;
    if (prio == 2) {
        if (!(ie & 1)) mask |= 1u << IRQ_INT0;
        if (!(ie & 2) && !(ie & 1)) mask |= 1u << IRQ_INT1;
        return mask;
    }
    if (prio == 1 || (ie & 0x80)) {
        mask |= (((ip >> 7) & 1) ^ inv) << IRQ_P3;
        mask |= (((ip >> 5) & 1) ^ inv) << IRQ_SIO1;
        mask |= (((ip >> 4) & 1) ^ inv) << IRQ_SIO0;
        mask |= (((ip >> 3) & 1) ^ inv) << IRQ_T1;
        mask |= (((ip >> 2) & 1) ^ inv) << IRQ_T0H;
        mask |= (((ip >> 1) & 1) ^ inv) << IRQ_INT3_TBASE;
        mask |= (((ip >> 0) & 1) ^ inv) << IRQ_INT2_T0L;
        if (prio == 0) {
            if (ie & 1) mask |= 1u << IRQ_INT0 | 1u << IRQ_INT1;
            if (ie & 2) mask |= 1u << IRQ_INT1;
        }
    }
    return mask;
}

static int take_irq(VmuCore *c, int prio)
{
    unsigned mask = irqs_enabled(c, prio) & c->int_req, i;
    if (!mask) return 0;
    for (i = 0; !(mask & (1u << i)); i++) {}
    c->int_req &= ~(1u << i);
    c->int_stack[prio] = 1u << i;
    push(c, c->pc & 0xff);
    push(c, c->pc >> 8);
    write_data(c, PCON, read_data(c, PCON) & ~1u);       /* wakes from HALT */
    c->pc = isr_addr[i];
    return 1;
}

static void pic_update(VmuCore *c)
{
    int p;
    if (!c->int_process) { c->int_process = 1; return; }   /* (the instruction after a RETI) */
    if (!c->int_req) return;
    if (!(c->int_stack[0] | c->int_stack[1] | c->int_stack[2])) {
        /* (as libevmu: with none active, the highest level is not looked at) */
        for (p = c->int_prev_prio - 1; p >= 0; --p) if (take_irq(c, p)) return;
        for (p = 1; p >= c->int_prev_prio; --p) if (take_irq(c, p)) return;
    } else {
        for (p = 2; p >= 0; --p) {
            if (c->int_stack[p]) break;
            if (take_irq(c, p)) return;
        }
    }
}

static void reti(VmuCore *c)
{
    int p;
    c->pc = pop(c) << 8;
    c->pc |= pop(c);
    c->int_process = 0;
    for (p = 2; p >= 0; --p)
        if (c->int_stack[p]) { c->int_prev_prio = p; c->int_stack[p] = 0; return; }
}

/* ---- timers --------------------------------------------------------------- */

static unsigned consume_delay(unsigned *delay, unsigned ticks)
{
    unsigned skipped = *delay < ticks ? *delay : ticks;
    *delay -= skipped;
    return ticks - skipped;
}

/* count up by ticks, reloading on each overflow; the overflows */
static unsigned advance_reload(unsigned *counter, unsigned ticks, unsigned modulus, unsigned reload)
{
    unsigned start = *counter % modulus, to_ovf, period;
    if (!ticks) { *counter = start; return 0; }
    to_ovf = modulus - start;
    if (ticks < to_ovf) { *counter = start + ticks; return 0; }
    ticks -= to_ovf;
    period = modulus - reload;
    *counter = reload + ticks % period;
    return 1 + ticks / period;
}

static unsigned advance8(int *counter, unsigned ticks, unsigned reload)
{
    unsigned v = (unsigned)*counter & 0xff, n = advance_reload(&v, ticks, 0x100, reload);
    *counter = (int)v;
    return n;
}

static unsigned osc_hz(unsigned ocr)
{
    switch ((ocr >> 4) & 3) {
    case 1: case 3: return 6000000;              /* CF */
    case 2: return 32768;                        /* quartz */
    default: return 879236;                      /* RC */
    }
}

static unsigned base_timer_ticks(VmuCore *c, unsigned cc)
{
    unsigned num, hz;
    switch ((R(ISL) >> 4) & 3) {
    case 1:                                      /* the CPU's cycles */
        return consume_delay(&c->bt_delay, cc);
    case 3:                                      /* timer 0's prescaler */
        c->bt_delay = 0;
        num = c->bt_rem + cc;
        c->bt_rem = num % (unsigned)c->t0scale;
        return num / (unsigned)c->t0scale;
    default:                                     /* quartz */
        c->bt_delay = 0;
        num = c->bt_rem + cc * 32768u * ((R(OCR) & 0x80) ? 6u : 12u);
        hz = osc_hz(R(OCR));
        if (hz == 32768) { c->bt_rem = num & 32767; return num >> 15; }
        c->bt_rem = num % hz;
        return num / hz;
    }
}

static void base_timer_update(VmuCore *c, unsigned cc)
{
    unsigned btcr = R(BTCR), n = base_timer_ticks(c, cc), cur, next, r0, r1;
    if (!(btcr & 0x40) || !n) return;
    cur = c->bt_counter;
    next = cur + n;
    r0 = (btcr & 0x80) ? 0x40 : 0x4000;
    switch (btcr & 0xb0) {
    case 0x00: case 0x80: r1 = 0x20; break;
    case 0x10: case 0x90: r1 = 0x80; break;
    case 0x20: r1 = 0x200; break;
    case 0x30: r1 = 0x800; break;
    case 0xa0: r1 = 0x2; break;
    case 0xb0: r1 = 0x8; break;
    default: r1 = 0; break;
    }
    if (r1 && cur / r1 < next / r1) {
        R(BTCR) |= 0x08;
        if (btcr & 0x04) c->int_req |= 1u << IRQ_INT3_TBASE;
    }
    if (cur / r0 < next / r0) {
        R(BTCR) |= 0x02;
        if (btcr & 0x01) c->int_req |= 1u << IRQ_INT3_TBASE;
    }
    c->bt_counter = next & 0x3fff;
}

static void timer0_update(VmuCore *c, unsigned cc)
{
    unsigned c0 = 0;
    if (!(R(T0CNT) & 0xc0)) return;
    if ((c->t0base += (int)consume_delay(&c->t0delay, cc)) >= c->t0scale)
        do c0++; while ((c->t0base -= c->t0scale) >= c->t0scale);
    if (!c0) return;
    if ((R(T0CNT) & 0xe0) == 0xe0) {
        unsigned v = ((unsigned)c->t0h & 0xff) << 8 | ((unsigned)c->t0l & 0xff);
        unsigned n = advance_reload(&v, c0, 0x10000, (unsigned)R(T0HR) << 8 | R(T0LR));
        c->t0l = v & 0xff;
        c->t0h = (v >> 8) & 0xff;
        if (n) {
            R(T0CNT) |= 0x0a;
            if (R(T0CNT) & 0x04) c->int_req |= 1u << IRQ_T0H;
        }
    } else {
        if ((R(T0CNT) & 0x40) && advance8(&c->t0l, c0, R(T0LR))) {
            R(T0CNT) |= 0x02;
            if (R(T0CNT) & 0x01) c->int_req |= 1u << IRQ_INT2_T0L;
        }
        if ((R(T0CNT) & 0x80) && advance8(&c->t0h, c0, R(T0HR))) {
            R(T0CNT) |= 0x08;
            if (R(T0CNT) & 0x04) c->int_req |= 1u << IRQ_T0H;
        }
    }
}

static void timer1_update(VmuCore *c, unsigned cc)
{
    unsigned cy = consume_delay(&c->t1delay, cc);
    if (!cy || !(R(T1CNT) & 0xc0)) return;
    if ((R(T1CNT) & 0xe0) == 0xe0) {
        unsigned low = advance8(&c->t1l, cy, R(T1LR));
        if (advance8(&c->t1h, low, R(T1HR))) {
            R(T1CNT) |= 0x08;
            if (R(T1CNT) & 0x04) c->int_req |= 1u << IRQ_T1;
        }
        if (low) R(T1CNT) |= 0x02;
    } else {
        if ((R(T1CNT) & 0x40) && advance8(&c->t1l, cy, R(T1LR))) {
            R(T1CNT) |= 0x02;
            if (!(R(T1CNT) & 0x20)) bz_t1_reload(c);
            if (R(T1CNT) & 0x01) c->int_req |= 1u << IRQ_T1;
        }
        if ((R(T1CNT) & 0x80) && advance8(&c->t1h, cy, R(T1HR))) {
            R(T1CNT) |= 0x08;
            if (R(T1CNT) & 0x04) c->int_req |= 1u << IRQ_T1;
        }
    }
}

/* ---- the CPU -------------------------------------------------------------- */

#define DIRECT9  ((b0 & 1u) << 8 | b1)                /* 9-bit address, 2nd byte */
#define DIRECTB  ((b0 & 0x10u) << 4 | b1)             /* the bit instructions' */
#define BIT3     (b0 & 7u)
#define IND      (b0 & 3u)
#define REL8(b)  ((unsigned)(int8_t)(b))
#define BR(cond, rel) do { if (cond) c->pc += (rel); } while (0)

static void arith(VmuCore *c, unsigned b, int carry, int sub)
{
    unsigned a = R(ACC), cy = carry ? (R(PSW) >> 7) & 1 : 0, r, p;
    int icy, iac, iov;
    if (!sub) {
        r = a + b + cy;
        icy = (int)(a + b + cy) > 255;
        iac = (int)((a & 0xf) + ((b + cy) & 0xf)) > 0xf;
        iov = (0x80 & (~a ^ (b + cy)) & ((b + cy) ^ (a + (b + cy)))) != 0;
    } else {
        r = a - b - cy;
        icy = (int)a - (int)b - (int)cy < 0;
        iac = (int)(a & 0xf) - (int)(b & 0xf) - (int)cy < 0;
        iov = (int8_t)(a ^ (b + cy)) < 0 && (int8_t)((b + cy) ^ (a - (b + cy))) >= 0;
    }
    p = R(PSW) & ~(0x80u | 0x40u | 0x04u);          /* (read before ACC's parity lands, as libevmu) */
    p |= icy ? 0x80 : 0;
    p |= iac ? 0x40 : 0;
    p |= iov ? 0x04 : 0;
    write_data(c, ACC, r);
    write_data(c, PSW, p);
}

static void br_cmp(VmuCore *c, unsigned v1, unsigned v2, int equal, unsigned rel)
{
    set_psw_bit(c, 0x80, v1 < v2);
    BR(equal ? v1 == v2 : v1 != v2, rel);
}

static void run_next(VmuCore *c)
{
    unsigned b0 = ext_byte(c, c->pc), b1 = ext_byte(c, c->pc + 1u), b2 = ext_byte(c, c->pc + 2u);
    unsigned a, v, m;
    c->cc = len_cc[b0] & 0xf;
    c->pc += len_cc[b0] >> 4;
    switch (op_of[b0]) {
    case 0x00: break;                                            /* NOP */
    case 0x01: c->pc += REL8(b1); break;                         /* BR */
    case 0x02: write_data(c, ACC, read_data(c, DIRECT9)); break; /* LD */
    case 0x04: write_data(c, ACC, read_data(c, indirect(c, IND))); break;
    case 0x08:                                                   /* CALL */
        push(c, c->pc & 0xff); push(c, c->pc >> 8);
        c->pc = (c->pc & ~0xfffu) | ((b0 & 0x10u) << 7 | (b0 & 7u) << 8 | b1);
        break;
    case 0x10:                                                   /* CALLR */
        push(c, c->pc & 0xff); push(c, c->pc >> 8);
        c->pc += (b2 << 8 | b1) - 1u;
        break;
    case 0x11: c->pc += (b2 << 8 | b1) - 1u; break;              /* BRF */
    case 0x12: write_data(c, DIRECT9, R(ACC)); break;            /* ST */
    case 0x14: write_data(c, indirect(c, IND), R(ACC)); break;
    case 0x20:                                                   /* CALLF */
        push(c, c->pc & 0xff); push(c, c->pc >> 8);
        c->pc = b1 << 8 | b2;
        break;
    case 0x21: c->pc = b1 << 8 | b2; break;                      /* JMPF */
    case 0x22: write_data(c, DIRECT9, b2); break;                /* MOV */
    case 0x24: write_data(c, indirect(c, IND), b1); break;
    case 0x28:                                                   /* JMP */
        c->pc = (c->pc & ~0xfffu) | ((b0 & 0x10u) << 7 | (b0 & 7u) << 8 | b1);
        break;
    case 0x30: {                                                 /* MUL */
        unsigned t = (R(C) | R(ACC) << 8) * R(B);
        write_data(c, C, t & 0xff);
        write_data(c, ACC, (t >> 8) & 0xff);
        write_data(c, B, (t >> 16) & 0xff);
        set_psw_bit(c, 0x80, 0);
        set_psw_bit(c, 0x04, t > 65535);
        break;
    }
    case 0x31: br_cmp(c, R(ACC), b1, 1, REL8(b2)); break;        /* BEI */
    case 0x32: a = read_data(c, DIRECT9); br_cmp(c, R(ACC), a, 1, REL8(b2)); break;
    case 0x34: a = read_data(c, indirect(c, IND)); br_cmp(c, a, b1, 1, REL8(b2)); break;
    case 0x40: {                                                 /* DIV */
        unsigned d = R(B);
        if (d) {
            unsigned n = R(C) | R(ACC) << 8, q = n / d;
            write_data(c, B, n % d);
            write_data(c, C, q & 0xff);
            write_data(c, ACC, (q >> 8) & 0xff);
        } else {
            write_data(c, ACC, 0xff);
        }
        set_psw_bit(c, 0x80, 0);
        set_psw_bit(c, 0x04, !d);
        break;
    }
    case 0x41: br_cmp(c, R(ACC), b1, 0, REL8(b2)); break;        /* BNEI */
    case 0x42: a = read_data(c, DIRECT9); br_cmp(c, R(ACC), a, 0, REL8(b2)); break;
    case 0x44: a = read_data(c, indirect(c, IND)); br_cmp(c, a, b1, 0, REL8(b2)); break;
    case 0x48:                                                   /* BPC */
        a = DIRECTB; v = read_latch(c, a); m = 1u << BIT3;
        if (v & m) { write_data(c, a, v & ~m); c->pc += REL8(b2); }
        break;
    case 0x50:                                                   /* LDF */
        write_data(c, ACC, c->flash[(R(FPR) & 1u) << 16 | R(TRH) << 8 | R(TRL)]);
        break;
    case 0x51: {                                                 /* STF */
        unsigned addr = R(TRH) << 8 | R(TRL), acc = R(ACC), fpr = R(FPR);
        if (R(EXT) & 1) break;                   /* (only from the BIOS) */
        if (fpr & 2) {
            switch (c->prg_state) {
            case 0: if (addr == 0x5555 && acc == 0xaa) c->prg_state = 1; break;
            case 1: c->prg_state = (addr == 0x2aaa && acc == 0x55) ? 2 : 0; break;
            case 2: c->prg_state = (addr == 0x5555 && acc == 0xa0) ? 3 : 0; break;
            default: c->prg_state = 0; break;
            }
        } else if (c->prg_state < 3) {
            c->prg_state = 0;
        } else {
            addr |= (fpr & 1u) << 16;
            if (c->prg_state == 3 && (addr & 0x7f)) {
                c->prg_state = 0;
            } else {
                c->flash[addr] = acc;
                if (++c->prg_state == 128 + 3) c->prg_state = 0;
            }
        }
        break;
    }
    case 0x52:                                                   /* DBNZ */
        a = DIRECT9; v = (read_latch(c, a) - 1) & 0xff;
        write_data(c, a, v); BR(v != 0, REL8(b2));
        break;
    case 0x54:
        a = indirect(c, IND); v = (read_latch(c, a) - 1) & 0xff;
        write_data(c, a, v); BR(v != 0, REL8(b1));
        break;
    case 0x60: push(c, read_data(c, DIRECT9)); break;            /* PUSH */
    case 0x62: a = DIRECT9; write_data(c, a, read_latch(c, a) + 1); break;   /* INC */
    case 0x64: a = indirect(c, IND); write_data(c, a, read_latch(c, a) + 1); break;
    case 0x68: BR(read_data(c, DIRECTB) & (1u << BIT3), REL8(b2)); break;    /* BP */
    case 0x70: a = DIRECT9; write_data(c, a, pop(c)); break;     /* POP */
    case 0x72: a = DIRECT9; write_data(c, a, read_latch(c, a) - 1); break;   /* DEC */
    case 0x74: a = indirect(c, IND); write_data(c, a, read_latch(c, a) - 1); break;
    case 0x80: BR(!R(ACC), REL8(b1)); break;                     /* BZ */
    case 0x81: arith(c, b1, 0, 0); break;                        /* ADD */
    case 0x82: arith(c, read_data(c, DIRECT9), 0, 0); break;
    case 0x84: arith(c, read_data(c, indirect(c, IND)), 0, 0); break;
    case 0x88: BR(!(read_data(c, DIRECTB) & (1u << BIT3)), REL8(b2)); break; /* BN */
    case 0x90: BR(R(ACC), REL8(b1)); break;                      /* BNZ */
    case 0x91: arith(c, b1, 1, 0); break;                        /* ADDC */
    case 0x92: arith(c, read_data(c, DIRECT9), 1, 0); break;
    case 0x94: arith(c, read_data(c, indirect(c, IND)), 1, 0); break;
    case 0xa0: c->pc = pop(c) << 8; c->pc |= pop(c); break;      /* RET */
    case 0xa1: arith(c, b1, 0, 1); break;                        /* SUB */
    case 0xa2: arith(c, read_data(c, DIRECT9), 0, 1); break;
    case 0xa4: arith(c, read_data(c, indirect(c, IND)), 0, 1); break;
    case 0xa8: a = DIRECTB; write_data(c, a, read_latch(c, a) ^ (1u << BIT3)); break;  /* NOT1 */
    case 0xb0: reti(c); break;
    case 0xb1: arith(c, b1, 1, 1); break;                        /* SUBC */
    case 0xb2: arith(c, read_data(c, DIRECT9), 1, 1); break;
    case 0xb4: arith(c, read_data(c, indirect(c, IND)), 1, 1); break;
    case 0xc0: v = R(ACC); write_data(c, ACC, (v & 1) << 7 | v >> 1); break;  /* ROR */
    case 0xc1:                                                   /* LDC */
        a = R(ACC) + (R(TRL) | R(TRH) << 8);
        if (R(EXT) == 0) a += 0x10000;           /* (flash bank 1) */
        write_data(c, ACC, c->ext_rom || a >= VMU_CORE_FLASH_BYTES ? 0 : c->flash[a]);
        break;
    case 0xc2:                                                   /* XCH */
        a = DIRECT9; v = R(ACC); m = read_data(c, a);
        write_data(c, ACC, m); write_data(c, a, v);
        break;
    case 0xc4:
        a = indirect(c, IND); v = R(ACC); m = read_data(c, a);
        write_data(c, ACC, m); write_data(c, a, v);
        break;
    case 0xc8: a = DIRECTB; write_data(c, a, read_latch(c, a) & ~(1u << BIT3)); break; /* CLR1 */
    case 0xd0:                                                   /* RORC */
        v = R(ACC); m = R(PSW);
        write_data(c, PSW, (m & ~0x80u) | (v & 1) << 7);
        write_data(c, ACC, v >> 1 | (m & 0x80));
        break;
    case 0xd1: write_data(c, ACC, R(ACC) | b1); break;           /* OR */
    case 0xd2: write_data(c, ACC, R(ACC) | read_data(c, DIRECT9)); break;
    case 0xd4: write_data(c, ACC, R(ACC) | read_data(c, indirect(c, IND))); break;
    case 0xe0: v = R(ACC); write_data(c, ACC, v << 1 | (v & 0x80) >> 7); break; /* ROL */
    case 0xe1: write_data(c, ACC, R(ACC) & b1); break;           /* AND */
    case 0xe2: write_data(c, ACC, R(ACC) & read_data(c, DIRECT9)); break;
    case 0xe4: write_data(c, ACC, R(ACC) & read_data(c, indirect(c, IND))); break;
    case 0xe8: a = DIRECTB; write_data(c, a, read_latch(c, a) | (1u << BIT3)); break;  /* SET1 */
    case 0xf0:                                                   /* ROLC */
        v = R(ACC); m = R(PSW);
        write_data(c, PSW, (m & ~0x80u) | (v & 0x80));
        write_data(c, ACC, v << 1 | (m & 0x80) >> 7);
        break;
    case 0xf1: write_data(c, ACC, R(ACC) ^ b1); break;           /* XOR */
    case 0xf2: write_data(c, ACC, R(ACC) ^ read_data(c, DIRECT9)); break;
    case 0xf4: write_data(c, ACC, R(ACC) ^ read_data(c, indirect(c, IND))); break;
    }
    if (c->ext_rom) {                            /* into the BIOS: done here */
        if ((c->pc = call_bios(c, c->pc)) != 0)
            write_data(c, EXT, read_data(c, EXT) | 1);
    }
}

/* ns per CPU cycle for OCR's clock and divider (libevmu's figures) */
static uint32_t cycle_ns(unsigned ocr)
{
    int div6 = (ocr & 0x80) != 0;
    switch ((ocr >> 4) & 3) {
    case 1: case 3: return div6 ? 1000 : 2000;           /* CF */
    case 2: return div6 ? 183105 : 366210;               /* quartz */
    default: return div6 ? 6824 : 13648;                 /* RC */
    }
}

static void poll_buttons(VmuCore *c)
{
    unsigned p3int;
    if (!(uint8_t)~R(P3DDR) || !(uint8_t)~port3(c)) return;
    p3int = R(P3INT);
    if (!(p3int & 0x04)) return;
    write_data(c, P3INT, p3int | 0x02);
    write_data(c, PCON, R(PCON) & ~0x02u);                 /* out of HOLD */
    if (p3int & 0x01) c->int_req |= 1u << IRQ_P3;
}

void vmu_core_run(VmuCore *c, uint32_t microseconds)
{
    /* (each step starts from zero and stops past its end, the excess dropped:
     * libevmu's EvmuCpu update) */
    const uint64_t end = (uint64_t)microseconds * 1000u;
    uint64_t t = 0;
    poll_buttons(c);
    while (t < end) {
        unsigned pcon = R(PCON);
        if (!(pcon & 2)) {
            pic_update(c);
            base_timer_update(c, c->cc);
            timer0_update(c, c->cc);
            timer1_update(c, c->cc);
        }
        if (!(pcon & 3)) run_next(c);
        t += cycle_ns(R(OCR)) * ((R(PCON) & 3) ? 1u : c->cc);
    }
}

/* ---- the rest of the API -------------------------------------------------- */

VmuCore *vmu_core_create(void)
{
    VmuCore *c = (VmuCore *)calloc(1, sizeof(VmuCore));
    if (!c) return NULL;
    c->bios_exit = -1;
    vmu_core_reset(c);
    return c;
}

void vmu_core_destroy(VmuCore *c)
{
    free(c);
}

void vmu_core_load_flash(VmuCore *c, const uint8_t *img, size_t bytes)
{
    if (bytes > VMU_CORE_FLASH_BYTES) bytes = VMU_CORE_FLASH_BYTES;
    memcpy(c->flash, img, bytes);
    memset(c->flash + bytes, 0, VMU_CORE_FLASH_BYTES - bytes);
}

/* libevmu's reset with its emulated BIOS: memory first (in this order, the
 * writes with their side effects), then the CPU, buzzer, buttons, timers,
 * interrupts and work RAM */
void vmu_core_reset(VmuCore *c)
{
    memset(c->ram, 0, sizeof c->ram);
    memset(c->sfr, 0, sizeof c->sfr);
    memset(c->xram, 0, sizeof c->xram);
    c->seg[2] = c->sfr;
    c->seg[3] = c->xram[0];
    R(SP) = 0x7f;
    R(P3) = 0xff;
    R(PSW) = 0x02;
    c->seg[0] = c->ram[1];                       /* (libevmu points it here a few writes on) */
    c->seg[1] = c->ram[1] + 0x80;
    c->ext_rom = 0;
    write_data(c, P7, 0x02);
    write_data(c, IE, 0xff);
    write_data(c, IP, 0x00);
    write_data(c, P1FCR, 0xbf);
    write_data(c, P3INT, 0xfd);
    write_data(c, ISL, 0xc0);
    write_data(c, VSEL, 0xfc);
    c->pc = 0;
    R(EXT) = 1;
    write_data(c, XBNK, 2);
    write_data(c, 0x182, 0x10);                  /* the GAME icon */
    write_data(c, P1DDR, 0xff);
    write_data(c, P1FCR, 0xbf);
    write_data(c, P3INT, 0xfd);
    write_data(c, P3DDR, 0x00);
    write_data(c, ISL, 0xc0);
    write_data(c, VSEL, 0xfc);
    write_data(c, BTCR, 0x41);
    write_data(c, IE, 0xff);
    write_data(c, IP, 0x00);
    write_data(c, OCR, 0x80 | 0x20 | 0x02 | 0x01);   /* quartz, /6, RC and CF stopped */
    R(P7) = 0x02;
    write_data(c, XBNK, 0);
    write_data(c, VCCR, 0x80);                   /* screen on */
    write_data(c, MCR, 0x08);
    write_data(c, PCON, 0);
    c->cc = 1;                                   /* the CPU: a NOP "last" */
    bz_stop(c);                                  /* the buzzer */
    c->bz_active = 0;
    c->bz_t1lr = c->bz_t1lc = 0;
    c->bz_period = c->bz_inv = 0;
    c->buttons = 0;                              /* the buttons */
    c->t0l = c->t0h = c->t0base = 0;             /* the timers */
    c->t0scale = 256;
    c->t0delay = 0;
    c->t1l = c->t1h = 0;
    c->t1delay = 0;
    c->bt_counter = 0;
    c->bt_rem = 0;
    c->bt_delay = 0;
    c->int_req = 0;                              /* the interrupt controller */
    memset(c->int_stack, 0, sizeof c->int_stack);
    c->int_process = 1;
    c->int_prev_prio = 0;
    memset(c->wram, 0, sizeof c->wram);
    c->bios_exit = -1;
}

void vmu_core_set_datetime(VmuCore *c, int year, int month, int day, int hour, int min, int sec)
{
    bios_set_datetime(c, year, month, day, hour, min, sec);
}

void vmu_core_set_buttons(VmuCore *c, unsigned bits)
{
    c->buttons = bits & 0xff;
}

int vmu_core_pixel(const VmuCore *c, int x, int y)
{
    int row = y & 15;
    if ((unsigned)x >= 48 || (unsigned)y >= 32) return 0;
    return (c->xram[y >> 4][(row >> 1) * 16 + (row & 1) * 6 + (x >> 3)] >> (7 - (x & 7))) & 1;
}

void vmu_core_lcd(const VmuCore *c, uint8_t out[32][48])
{
    int x, y;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 48; x++) out[y][x] = (uint8_t)vmu_core_pixel(c, x, y);
}

unsigned vmu_core_icons(const VmuCore *c)
{
    const uint8_t *k = c->xram[2];
    return ((k[1] >> 6) & 1) | ((k[2] >> 4) & 1) << 1 | ((k[3] >> 2) & 1) << 2 | (k[4] & 1) << 3;
}

int vmu_core_screen_on(const VmuCore *c)
{
    return R(VCCR) >> 7;
}

int vmu_core_tone(const VmuCore *c, unsigned *period, unsigned *inv_pulse)
{
    if (!c->bz_active) return 0;
    if (period) *period = c->bz_period;
    if (inv_pulse) *inv_pulse = c->bz_inv;
    return 1;
}

const uint8_t *vmu_core_flash(const VmuCore *c)
{
    return c->flash;
}

int vmu_core_bios_exit(const VmuCore *c)
{
    return c->bios_exit;
}
