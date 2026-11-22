// The adapter's Maple receiver on the computer: the tables of joypad-os's
// maple_state_machine.c and the loop of dreamcast_device.c's core1_rx_task,
// over frames of every length (0 to 255 words) made here as the line changes
// the receiver's PIO would push, at each of its four alignments.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "maple_state_machine.h"

static uint8_t tr[400000]; static int ntr;
static int cur;
static void go(int s) { if (s != cur) { tr[ntr++] = s; cur = s; } }
// states: bit1 = pin 5, bit0 = pin 1
static void frame(const uint8_t *b, int n)
{
    int i, k;
    go(3);
    for (i = 0; i < 4; i++) { go(2); go(0); }
    go(2); go(3);
    for (i = 0; i < n; i++)
        for (k = 7; k >= 0; k--) {
            int d = b[i] >> k & 1;
            if ((7 - k) % 2 == 0) {            // phase A: clock pin 1, data pin 5
                go(cur | 1);                   // pin 1 high
                go(d ? 3 : 1);                 // pin 5 the data
                go(d ? 2 : 0);                 // pin 1 falls
            } else {                           // phase B: clock pin 5, data pin 1
                go(cur | 2);
                go(d ? 3 : 2);
                go(d ? 1 : 0);
            }
        }
    go(cur | 1); go(3); go(1); go(0); go(1); go(0); go(1); go(3);   // the end
}

static uint8_t rx[4096]; static int got_len, ends, good, errors, resets;
static void receive(int offset)
{
    uint32_t State = 0; uint8_t Byte = 0, XOR = 0; int Offset = 0, i;
    ends = good = errors = resets = 0; got_len = -1;
    for (i = -offset; i < ntr; i += 4) {
        int k, v = 0;
        for (k = 0; k < 4; k++) { int j = i + k; v = v << 2 | (j < 0 ? 3 : j < ntr ? tr[j] : 3); }
        // (the PIO pushes changes only; padding with the idle state stands for the next frame's wait)
        MapleStateMachine M = MapleMachine[State][v];
        State = M.NewState;
        if (M.Error) errors++;
        if (M.Reset) { resets++; Offset = 0; Byte = 0; XOR = 0; }
        Byte |= MapleSetBits[M.SetBitsIndex][0];
        if (M.Push) { rx[Offset++ & 4095] = Byte; XOR ^= Byte; Byte = MapleSetBits[M.SetBitsIndex][1]; }
        if (M.End) { ends++; if (XOR == 0) { good++; got_len = Offset; } }
    }
}

int main(void)
{
    int words, off, fails = 0, first_fail = -1, tested = 0;
    maple_build_state_machine_tables();
    srand(1);
    for (words = 0; words <= 255; words++) {
        uint8_t b[4 + 255 * 4 + 1]; int n = 4 + words * 4, i; uint8_t x = 0;
        int rep;
        for (rep = 0; rep < 6; rep++) {
            b[0] = words; b[1] = 0; b[2] = 0x01; b[3] = 0x22;      // length, origin, destination, command
            for (i = 4; i < n; i++) b[i] = rep == 0 ? 0 : rep == 1 ? 0xFF : rand();
            for (x = 0, i = 0; i < n; i++) x ^= b[i];
            b[n] = x;
            for (off = 0; off < 4; off++) {
                ntr = 0; cur = 3;
                frame(b, n + 1);
                receive(off);
                tested++;
                if (!(good == 1 && got_len == n + 1 && !memcmp(rx, b, n + 1))) {
                    fails++;
                    if (first_fail < 0) { first_fail = words; printf("first failure: %d words (rep %d, offset %d): ends %d good %d errors %d len %d\n", words, rep, off, ends, good, errors, got_len); }
                }
            }
        }
    }
    {   // which lengths fail at all
        int lo = -1, hi = -1;
        for (words = 0; words <= 255; words++) {
            uint8_t b[4 + 255 * 4 + 1]; int n = 4 + words * 4, i; uint8_t x = 0;
            memset(b, 0x5A, sizeof(b)); b[0] = words;
            for (i = 0; i < n; i++) x ^= b[i];
            b[n] = x; ntr = 0; cur = 3; frame(b, n + 1); receive(0);
            if (good != 1) { if (lo < 0) lo = words; hi = words; }
        }
        if (lo < 0) printf("%d frames tried, none failed\n", tested);
        else printf("%d frames tried, %d failed; lengths failing: %d to %d words\n", tested, fails, lo, hi);
    }
    return fails != 0;
}
