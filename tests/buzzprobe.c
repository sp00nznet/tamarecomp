/* The buzzer.
 *
 * The Tamagotchi sounds a power-on chime a fraction of a second after reset:
 * the ROM selects a tone in BZ1, pulls R4 bit 3 low to enable the buzzer, and
 * releases it again about a second and a third later. That is deterministic,
 * so it makes a decent regression check on the whole path -- the R4 write, the
 * active-low enable, and the tone table.
 *
 * Pass a duration to use it as a probe instead, and it will report every
 * distinct tone over a longer run.
 */
#include <stdio.h>
#include <stdlib.h>

#include "tamarecomp/lcd.h"

#define MAX_EVENTS 64

struct ring { unsigned hz; double at, len; };

static int collect(tama_t *t, int secs, struct ring *out, int max)
{
    unsigned prev = 0;
    uint64_t began = 0;
    int n = 0;

    /* Sample finely: a one-shot beep is about 1024 cycles, 1/32 of a second. */
    for (long i = 0; i < (long)secs * 256; i++) {
        tama_step(t, TAMA_OSC1_HZ / 256);
        unsigned hz = tama_buzzer_hz(t);
        if (hz == prev)
            continue;
        if (prev && n < max) {
            out[n].hz = prev;
            out[n].at = (double)began / TAMA_OSC1_HZ;
            out[n].len = (double)(t->cycles - began) / TAMA_OSC1_HZ;
            n++;
        }
        if (hz)
            began = t->cycles;
        prev = hz;
    }
    return n;
}

int main(int argc, char **argv)
{
    int secs = (argc > 1) ? atoi(argv[1]) : 5;
    tama_t *t = calloc(1, sizeof(tama_t));
    struct ring ev[MAX_EVENTS];
    if (!t)
        return 2;
    tama_reset(t);

    if (tama_buzzer_hz(t) != 0) {
        printf("FAIL: the buzzer is sounding at reset\n");
        return 1;
    }

    int n = collect(t, secs, ev, MAX_EVENTS);
    for (int i = 0; i < n; i++)
        printf("  %7.3fs  %5u Hz for %.3f s\n", ev[i].at, ev[i].hz, ev[i].len);
    printf("\n%d buzzer events in %d s\n", n, secs);

    if (argc > 1)
        return 0;                       /* probe mode: report only */

    if (n < 1) {
        printf("FAIL: the ROM never sounded the buzzer\n");
        return 1;
    }
    /* The power-on chime, as the ROM actually drives it. */
    if (ev[0].at > 1.0) {
        printf("FAIL: no chime in the first second\n");
        return 1;
    }
    if (ev[0].hz != 2340) {
        printf("FAIL: chime is %u Hz, expected 2340\n", ev[0].hz);
        return 1;
    }
    if (ev[0].len < 1.0) {
        printf("FAIL: chime lasted %.3f s, expected over a second\n", ev[0].len);
        return 1;
    }
    printf("ok: power-on chime, %u Hz for %.2f s\n", ev[0].hz, ev[0].len);
    free(t);
    return 0;
}
