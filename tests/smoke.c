/* Does the recompiled ROM actually execute?
 *
 * There are no peripherals yet, so the ROM cannot get far into its boot -- it
 * will end up waiting on a timer that never ticks. What this checks is the
 * thing that would be broken if the emitter were wrong: that control flow
 * stays inside the ROM. A bad jump resolution or a mangled stack shows up
 * immediately as a trap, because the dispatch switch covers every one of the
 * 6144 words and refuses anything else.
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

    /* Run in slices so a HALT can be resumed the way an interrupt would. */
    const uint64_t slice = 1000000;
    int resumes = 0;
    for (int i = 0; i < 64; i++) {
        tama_run(t, t->cycles + slice);
        if (t->trapped) {
            printf("FAIL: trapped at pc=0x%04X after %llu cycles\n",
                   t->pc, (unsigned long long)t->cycles);
            return 1;
        }
        if (t->halted) {
            t->halted = 0;      /* stand in for an interrupt waking the core */
            resumes++;
        }
    }

    /* Display RAM at 0xE00 is only ever written by the LCD driver routines, so
     * a nonzero count means the boot path got far enough to draw. */
    int lcd = 0;
    for (int i = 0xE00; i <= 0xE4F; i++)
        if (t->mem[i])
            lcd++;

    printf("ok: %llu cycles, pc=0x%04X, sp=0x%02X, a=%X b=%X x=%03X y=%03X, "
           "%d halt-resumes, %d display nibbles set, no traps\n",
           (unsigned long long)t->cycles, t->pc, t->sp,
           t->a, t->b, t->x, t->y, resumes, lcd);

    if (t->cycles < slice) {
        printf("FAIL: barely executed anything\n");
        return 1;
    }
    free(t);
    return 0;
}
