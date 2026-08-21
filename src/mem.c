/* Memory map for the E0C6S46.
 *
 * Work RAM, display RAM and the I/O registers all live in one 12-bit nibble
 * space, which is why the recompiled code goes through these two functions
 * instead of touching an array directly:
 *
 *   0x000-0x27F  work RAM (640 nibbles)
 *   0xE00-0xE4F  display RAM, one nibble per 4 LCD segments
 *   0xF00-0xF7F  I/O registers (timers, buzzer, key input, LCD control)
 *
 * ponytail: for now every address is plain storage, so the recompiled ROM can
 * be built and run against a flat memory. The peripheral behaviour that hangs
 * off 0xF00-0xF7F -- timer reads, key state, buzzer, the interrupt latches --
 * lands in hw.c next, hooked in right here.
 */
#include <string.h>

#include "tamarecomp/e0c6200.h"

uint8_t tama_mem_read(tama_t *t, uint16_t addr)
{
    return t->mem[addr & (TAMA_MEM_NIBBLES - 1)] & 0xF;
}

void tama_mem_write(tama_t *t, uint16_t addr, uint8_t v)
{
    t->mem[addr & (TAMA_MEM_NIBBLES - 1)] = v & 0xF;
}

void tama_reset(tama_t *t)
{
    memset(t, 0, sizeof(*t));
    t->pc = TAMA_VEC_RESET;
    t->sp = 0xFF;   /* the ROM reloads this itself; a sane value until it does */
}
