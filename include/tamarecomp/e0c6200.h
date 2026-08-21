/* E0C6200 execution context for recompiled Tamagotchi code.
 *
 * The recompiled ROM is one C function with a label per instruction word, so
 * this holds only the architectural state -- there is no instruction pointer to
 * maintain except at a computed transfer, where `pc` is set and the generated
 * dispatch takes over.
 *
 * Nibbles are stored one per byte. The core is 4-bit; packing two per byte
 * would save 2KB and cost a shift on every access, on a machine that has
 * gigabytes and did not have 2KB.
 */
#ifndef TAMARECOMP_E0C6200_H
#define TAMARECOMP_E0C6200_H

#include <stdint.h>

#define TAMA_ROM_WORDS   6144      /* 0x1800, the whole address space */
#define TAMA_MEM_NIBBLES 0x1000    /* RAM, display RAM and I/O share one map */

typedef struct tama {
    uint8_t  a, b;          /* 4-bit accumulators */
    uint16_t x, y;          /* 12-bit index registers (page:high:low) */
    uint8_t  sp;            /* stack pointer, indexes RAM nibbles */

    uint8_t  cf, zf, df, iff;  /* flags, one byte each -- branch-friendly */
    uint8_t  if_delay;      /* PSET and SET F,I hold interrupts off one step */

    uint8_t  halted;        /* HALT executed; resumes on interrupt */
    uint8_t  trapped;       /* dispatched to an address with no code */
    uint16_t pc;            /* only meaningful at a computed transfer */
    uint64_t cycles;

    uint8_t  mem[TAMA_MEM_NIBBLES];
    void    *hw;            /* opaque peripheral state, see hw.h */
} tama_t;

/* Memory goes through these because the E0C6S46 maps display RAM at 0xE00 and
 * its I/O registers at 0xF00 into the same space as work RAM. */
uint8_t tama_mem_read(tama_t *t, uint16_t addr);
void    tama_mem_write(tama_t *t, uint16_t addr, uint8_t v);

/* Generated. Runs from t->pc until HALT, a trap, or `budget` cycles elapse. */
void tama_run(tama_t *t, uint64_t budget);

/* Hardware reset: PC to the reset vector, flags and registers cleared. */
void tama_reset(tama_t *t);

/* Vector addresses. The second word of each slot is unused padding. */
#define TAMA_VEC_RESET       0x0100
#define TAMA_VEC_CLOCK       0x0102
#define TAMA_VEC_STOPWATCH   0x0104
#define TAMA_VEC_PROG_TIMER  0x0106
#define TAMA_VEC_SERIAL      0x0108
#define TAMA_VEC_INPUT_K0    0x010A
#define TAMA_VEC_INPUT_K1    0x010C

#endif
