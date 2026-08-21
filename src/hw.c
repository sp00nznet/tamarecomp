/* E0C6S46 peripherals: the memory map, the timers, and interrupt delivery.
 *
 * The recompiled ROM has no notion of time. It runs until its cycle budget is
 * spent and returns, which turns out to be exactly the shape an interrupt
 * needs: tama_step sets the budget to the next timer edge, so tama_run comes
 * back on its own at the moment something is due. No polling, and nothing in
 * the generated code has to know interrupts exist.
 */
#include <string.h>

#include "tamarecomp/e0c6200.h"

/* Address map. Work RAM is plain storage; so is display RAM, which the LCD
 * side reads straight out of t->mem. Only 0xF00-0xF7F needs behaviour. */
#define IO_BASE  0xF00
#define IO_TOP   0xF7F

/* Timer periods in OSC1 cycles. The clock timer is the one that matters: an
 * 8-bit counter stepping at 256 Hz, whose bits 2, 4, 6 and 7 falling produce
 * the 32/8/2/1 Hz interrupt factors the Tamagotchi runs its whole life on. */
#define TM_PERIOD  (TAMA_OSC1_HZ / 256)
#define SW_PERIOD  (TAMA_OSC1_HZ / 100)

/* Programmable timer divider, selected by PTC at 0xF79. Entries 0 and 1 are
 * not selectable on this part. */
static const uint32_t PT_PERIOD[8] = {
    0, 0,
    TAMA_OSC1_HZ / 256, TAMA_OSC1_HZ / 512, TAMA_OSC1_HZ / 1024,
    TAMA_OSC1_HZ / 2048, TAMA_OSC1_HZ / 4096, TAMA_OSC1_HZ / 8192,
};

/* Interrupt factor bits within the clock-timer factor nibble. */
#define IT_1HZ   8
#define IT_2HZ   4
#define IT_8HZ   2
#define IT_32HZ  1

static uint8_t io_read(tama_t *t, uint16_t addr)
{
    tama_hw_t *h = &t->hw;
    uint8_t v;

    switch (addr) {
    /* Reading an interrupt factor clears it -- that is how the ROM
     * acknowledges. */
    case 0xF00: v = h->it;   h->it = 0;   return v;
    case 0xF01: v = h->isw;  h->isw = 0;  return v;
    case 0xF02: v = h->ipt;  h->ipt = 0;  return v;
    case 0xF03: v = h->isio; h->isio = 0; return v;
    case 0xF04: v = h->ik0;  h->ik0 = 0;  return v;
    case 0xF05: v = h->ik1;  h->ik1 = 0;  return v;

    case 0xF10: return h->eit;
    case 0xF11: return h->eisw;
    case 0xF12: return h->eipt;
    case 0xF13: return h->eisio;
    case 0xF14: return h->eik0;
    case 0xF15: return h->eik1;

    case 0xF20: return h->tm & 0xF;
    case 0xF21: return h->tm >> 4;
    case 0xF22: return h->swl;
    case 0xF23: return h->swh;
    case 0xF24: return h->pt & 0xF;
    case 0xF25: return h->pt >> 4;
    case 0xF26: return h->pt_reload & 0xF;
    case 0xF27: return h->pt_reload >> 4;

    case 0xF40: return h->k0;
    case 0xF42: return h->k1;

    /* Supply voltage detect. Bit 0 set would mean "battery low", which sends
     * the ROM down a different path; we always report a healthy cell. */
    case 0xF73: return 0;

    default:
        return t->mem[addr] & 0xF;
    }
}

static void io_write(tama_t *t, uint16_t addr, uint8_t v)
{
    tama_hw_t *h = &t->hw;

    switch (addr) {
    case 0xF10: h->eit = v;   break;
    case 0xF11: h->eisw = v;  break;
    case 0xF12: h->eipt = v;  break;
    case 0xF13: h->eisio = v; break;
    case 0xF14: h->eik0 = v;  break;
    case 0xF15: h->eik1 = v;  break;

    case 0xF26: h->pt_reload = (h->pt_reload & 0xF0) | v; break;
    case 0xF27: h->pt_reload = (h->pt_reload & 0x0F) | (uint8_t)(v << 4); break;

    case 0xF50: case 0xF51: case 0xF52: case 0xF53: case 0xF54:
        h->r[addr - 0xF50] = v;
        break;

    case 0xF71: h->lcd_ctrl = v; break;
    case 0xF72: h->lc = v;       break;
    case 0xF74: h->bz1 = v;      break;
    case 0xF75: h->bz2 = v;      break;

    case 0xF76:                          /* bit 1 resets the clock timer */
        if (v & 2) {
            h->tm = 0;
            h->tm_at = t->cycles + TM_PERIOD;
        }
        break;
    case 0xF77:                          /* bit 0 runs, bit 1 resets */
        if (v & 2)
            h->swl = h->swh = 0;
        h->sw_run = v & 1;
        h->sw_at = t->cycles + SW_PERIOD;
        break;
    case 0xF78:
        if (v & 2)
            h->pt = h->pt_reload;
        h->pt_run = v & 1;
        h->pt_at = t->cycles + (PT_PERIOD[h->ptc & 7] ? PT_PERIOD[h->ptc & 7]
                                                      : TM_PERIOD);
        break;
    case 0xF79: h->ptc = v; break;

    default:
        break;
    }
    t->mem[addr] = v & 0xF;
}

uint8_t tama_mem_read(tama_t *t, uint16_t addr)
{
    addr &= TAMA_MEM_NIBBLES - 1;
    if (addr >= IO_BASE && addr <= IO_TOP)
        return io_read(t, addr) & 0xF;
    return t->mem[addr] & 0xF;
}

void tama_mem_write(tama_t *t, uint16_t addr, uint8_t v)
{
    addr &= TAMA_MEM_NIBBLES - 1;
    if (addr >= IO_BASE && addr <= IO_TOP)
        io_write(t, addr, v & 0xF);
    else
        t->mem[addr] = v & 0xF;
}

void tama_reset(tama_t *t)
{
    memset(t, 0, sizeof(*t));
    t->pc = TAMA_VEC_RESET;
    t->sp = 0;                 /* undefined on real silicon; the ROM sets it
                                * before use. Zero matches the reference core,
                                * which keeps tools/difftest.py comparable from
                                * the first instruction. */
    t->hw.k0 = 0xF;            /* inputs idle high: no button held */
    t->hw.k1 = 0xF;
    t->hw.tm_at = TM_PERIOD;
    t->hw.sw_at = SW_PERIOD;
    t->hw.pt_at = TM_PERIOD;
}

void tama_set_buttons(tama_t *t, uint8_t mask)
{
    /* Active low: a held button pulls its K0 line down. A high-to-low edge on
     * an enabled line raises the K0 interrupt factor. */
    uint8_t next = (uint8_t)(~mask) & 0xF;
    uint8_t fell = (uint8_t)(t->hw.k0 & ~next);
    if (fell & t->hw.eik0)
        t->hw.ik0 |= fell & t->hw.eik0;
    t->hw.k0 = next;
}

/* Advance the clock timer one step and raise whatever factors its falling
 * edges produce. Bit 2 falls at 32 Hz, bit 4 at 8 Hz, bit 6 at 2 Hz, bit 7 at
 * 1 Hz -- the counter runs at 256 Hz, so each bit halves the one below it. */
static void tick_clock_timer(tama_hw_t *h)
{
    uint8_t old = h->tm;
    uint8_t nw = (uint8_t)(old + 1);

    if ((old & 0x04) && !(nw & 0x04)) h->it |= IT_32HZ;
    if ((old & 0x10) && !(nw & 0x10)) h->it |= IT_8HZ;
    if ((old & 0x40) && !(nw & 0x40)) h->it |= IT_2HZ;
    if ((old & 0x80) && !(nw & 0x80)) h->it |= IT_1HZ;
    h->tm = nw;
}

static void tick_stopwatch(tama_hw_t *h)
{
    if (!h->sw_run)
        return;
    if (++h->swl > 9) {
        h->swl = 0;
        h->isw |= 2;                      /* 1 Hz factor */
        if (++h->swh > 9) {
            h->swh = 0;
            h->isw |= 1;                  /* 10 Hz factor */
        }
    }
}

static void tick_prog_timer(tama_hw_t *h)
{
    if (!h->pt_run)
        return;
    if (--h->pt == 0) {
        h->pt = h->pt_reload;
        h->ipt |= 1;
    }
}

/* Cycles from now until the next timer edge, so the caller knows how long the
 * CPU may run undisturbed. */
static uint64_t next_edge(const tama_t *t)
{
    const tama_hw_t *h = &t->hw;
    uint64_t best = h->tm_at;
    if (h->sw_run && h->sw_at < best)
        best = h->sw_at;
    if (h->pt_run && h->pt_at < best)
        best = h->pt_at;
    return best > t->cycles ? best - t->cycles : 1;
}

static void advance_timers(tama_t *t)
{
    tama_hw_t *h = &t->hw;
    uint32_t pt_period = PT_PERIOD[h->ptc & 7] ? PT_PERIOD[h->ptc & 7] : TM_PERIOD;

    while (h->tm_at <= t->cycles) {
        tick_clock_timer(h);
        h->tm_at += TM_PERIOD;
    }
    while (h->sw_at <= t->cycles) {
        tick_stopwatch(h);
        h->sw_at += SW_PERIOD;
    }
    while (h->pt_at <= t->cycles) {
        tick_prog_timer(h);
        h->pt_at += pt_period;
    }
}

/* Vector the highest-priority pending interrupt, if any. Returns 1 if one was
 * taken. Priority runs from the highest vector address down, which is the
 * order the part's own documentation gives. */
static int take_interrupt(tama_t *t)
{
    tama_hw_t *h = &t->hw;
    uint16_t vec;

    if (!t->iff)
        return 0;
    if (t->if_delay) {              /* SET F,I grants one instruction of grace */
        t->if_delay = 0;
        return 0;
    }

    if (h->ipt & h->eipt)        vec = TAMA_VEC_PROG_TIMER;
    else if (h->isio & h->eisio) vec = TAMA_VEC_SERIAL;
    else if (h->ik1)             vec = TAMA_VEC_INPUT_K1;
    else if (h->ik0)             vec = TAMA_VEC_INPUT_K0;
    else if (h->isw & h->eisw)   vec = TAMA_VEC_STOPWATCH;
    else if (h->it & h->eit)     vec = TAMA_VEC_CLOCK;
    else                         return 0;

    /* Same three-nibble push a CALL does, then vector. */
    tama_mem_write(t, (uint16_t)((t->sp - 1) & 0xFF), (t->pc >> 8) & 0xF);
    tama_mem_write(t, (uint16_t)((t->sp - 2) & 0xFF), (t->pc >> 4) & 0xF);
    t->sp = (uint8_t)((t->sp - 3) & 0xFF);
    tama_mem_write(t, t->sp, t->pc & 0xF);

    t->iff = 0;
    t->halted = 0;
    t->pc = (uint16_t)((t->pc & 0x1000) | vec);
    t->cycles += 13;
    return 1;
}

void tama_step(tama_t *t, uint64_t cycles)
{
    uint64_t end = t->cycles + cycles;

    while (t->cycles < end && !t->trapped) {
        uint64_t budget = t->cycles + next_edge(t);
        if (budget > end)
            budget = end;

        if (t->halted)
            t->cycles = budget;     /* the core is stopped; only time passes */
        else
            tama_run(t, budget);

        advance_timers(t);
        take_interrupt(t);
    }
}
