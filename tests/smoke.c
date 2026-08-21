/* Does the recompiled ROM boot and keep running?
 *
 * tama_step drives the timers and delivers interrupts, so this is the whole
 * machine, not just the CPU. What it checks is the thing that would be broken
 * if the emitter were wrong: that control flow stays inside the ROM. A bad
 * jump resolution or a mangled stack shows up immediately as a trap, because
 * the dispatch switch covers every one of the 6144 words and refuses anything
 * else.
 */
#include <stdio.h>
#include <stdlib.h>

#include "tamarecomp/e0c6200.h"

int main(void)
{
    tama_t *t = calloc(1, sizeof(tama_t));
    if (!t)
        return 2;

    tama_reset(t);
    if (t->pc != TAMA_VEC_RESET) {
        printf("FAIL: reset did not arm the reset vector\n");
        return 1;
    }

    /* Sixty seconds of Tamagotchi time, one second at a time. */
    for (int i = 0; i < 60; i++) {
        tama_step(t, TAMA_OSC1_HZ);
        if (t->trapped) {
            printf("FAIL: trapped at pc=0x%04X after %llu cycles\n",
                   t->pc, (unsigned long long)t->cycles);
            return 1;
        }
    }

    /* Display RAM is only ever written by the LCD driver routines, so a
     * nonzero count means the boot path got far enough to draw. */
    int lcd = 0;
    for (int i = 0xE00; i <= 0xECF; i++)
        if (t->mem[i])
            lcd++;

    printf("ok: %llu cycles (%llus), pc=0x%04X, sp=0x%02X, a=%X b=%X "
           "x=%03X y=%03X, tm=%02X, EIT=%X, %d display nibbles, no traps\n",
           (unsigned long long)t->cycles,
           (unsigned long long)t->cycles / TAMA_OSC1_HZ,
           t->pc, t->sp, t->a, t->b, t->x, t->y,
           t->hw.tm, t->hw.eit, lcd);

    if (t->cycles < 60ULL * TAMA_OSC1_HZ) {
        printf("FAIL: did not run the full sixty seconds\n");
        return 1;
    }
    /* The clock timer is the device's heartbeat. If the ROM never unmasked it,
     * nothing time-driven can ever happen and the rest is theatre. */
    if (!t->hw.eit) {
        printf("FAIL: ROM never enabled the clock timer interrupt\n");
        return 1;
    }
    free(t);
    return 0;
}
