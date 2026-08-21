/* E0C6200 execution context and E0C6S46 peripherals.
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

/* The oscillator the whole machine hangs off. Instruction cycle counts are
 * treated as OSC1 cycles directly; TAMA_CPU_DIV is the knob for that, because
 * the real part's CPU clock is a divided OSC1 and the exact divider shows up
 * as drift against a wall clock, not as a wrong picture. Turn it if a running
 * Tamagotchi gains or loses time. */
#define TAMA_OSC1_HZ   32768
#define TAMA_CPU_DIV   1

/* Interrupt vectors. Each slot is two words wide; the second is unused
 * padding. Priority runs highest-address-first. */
#define TAMA_VEC_RESET       0x0100
#define TAMA_VEC_CLOCK       0x0102   /* clock timer */
#define TAMA_VEC_STOPWATCH   0x0104
#define TAMA_VEC_INPUT_K0    0x0106
#define TAMA_VEC_INPUT_K1    0x0108
#define TAMA_VEC_SERIAL      0x010A
#define TAMA_VEC_PROG_TIMER  0x010C

typedef struct tama_hw {
    /* Interrupt factor flags. Set by hardware, cleared when the ROM reads
     * them at 0xF00-0xF05. */
    uint8_t it, isw, ipt, isio, ik0, ik1;
    /* Interrupt masks, written by the ROM at 0xF10-0xF15. */
    uint8_t eit, eisw, eipt, eisio, eik0, eik1;

    uint8_t  tm;              /* clock timer, 8-bit, steps at 256 Hz */
    uint8_t  swl, swh;        /* stopwatch, 1/100 and 1/10 second digits */
    uint8_t  pt, pt_reload;   /* programmable timer and its reload value */
    uint8_t  sw_run, pt_run, ptc;

    uint8_t  k0, k1;          /* key input ports; a 0 bit means pressed */
    uint8_t  r[5];            /* output port, drives the buzzer among others */
    uint8_t  lcd_ctrl, lc;    /* LCD control and contrast */
    uint8_t  bz1, bz2;        /* buzzer */

    uint64_t tm_at, sw_at, pt_at;   /* cycle counts of the next timer edges */
} tama_hw_t;

typedef struct tama {
    uint8_t  a, b;          /* 4-bit accumulators */
    uint16_t x, y;          /* 12-bit index registers (page:high:low) */
    uint8_t  sp;            /* stack pointer, indexes RAM nibbles */

    uint8_t  cf, zf, df, iff;  /* flags, one byte each -- branch-friendly */
    uint8_t  if_delay;      /* SET F,I holds interrupts off for one step */

    uint8_t  halted;        /* HALT executed; resumes on interrupt */
    uint8_t  trapped;       /* dispatched to an address with no code */
    uint16_t pc;            /* only meaningful at a computed transfer */
    uint64_t cycles;

    uint8_t  mem[TAMA_MEM_NIBBLES];
    tama_hw_t hw;
} tama_t;

/* Memory goes through these because the E0C6S46 maps display RAM at 0xE00 and
 * its I/O registers at 0xF00 into the same space as work RAM. */
uint8_t tama_mem_read(tama_t *t, uint16_t addr);
void    tama_mem_write(tama_t *t, uint16_t addr, uint8_t v);

/* Generated. Runs from t->pc until HALT, a trap, or `budget` cycles elapse. */
void tama_run(tama_t *t, uint64_t budget);

/* Hardware reset: PC to the reset vector, everything else cleared. */
void tama_reset(tama_t *t);

/* Run the machine for `cycles`, stopping at each timer edge to update the
 * peripherals and deliver any interrupt that has come due. This is the entry
 * point a frontend calls; tama_run on its own has no notion of time passing. */
void tama_step(tama_t *t, uint64_t cycles);

/* Buttons, left to right. A 1 bit means held. */
#define TAMA_BTN_LEFT   0x1
#define TAMA_BTN_MIDDLE 0x2
#define TAMA_BTN_RIGHT  0x4
void tama_set_buttons(tama_t *t, uint8_t mask);

#endif
