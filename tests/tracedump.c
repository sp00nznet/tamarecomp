/* Dump the recompiled core's per-instruction state for differential testing.
 *
 * Build with -DTAMA_TRACING so the generated code's trace hook is live, then:
 *
 *     tracedump <count> <out.bin>
 *
 * Interrupts are deliberately not delivered here. The reference core checks
 * for them after every instruction while tama_step only stops at timer edges,
 * so leaving them on would compare interrupt scheduling rather than the thing
 * under test -- the ALU, the flags, and the resolved control flow.
 */
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>

#include "tamarecomp/e0c6200.h"

#define REC 10                    /* addr, a, b, x, y, sp, flags */

static unsigned char *buf;
static long cap, n;

void tama_trace_hook(const tama_t *t, uint16_t addr)
{
    if (n >= cap)
        return;
    unsigned char *p = buf + n * REC;
    p[0] = addr & 0xFF;
    p[1] = addr >> 8;
    p[2] = t->a;
    p[3] = t->b;
    p[4] = t->x & 0xFF;
    p[5] = (t->x >> 8) & 0xF;
    p[6] = t->y & 0xFF;
    p[7] = (t->y >> 8) & 0xF;
    p[8] = t->sp;
    p[9] = (unsigned char)(t->cf | (t->zf << 1) | (t->df << 2) | (t->iff << 3));
    n++;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: tracedump <count> <out.bin>\n");
        return 2;
    }
    cap = strtol(argv[1], NULL, 10);
    const char *out = argv[2];
    buf = malloc((size_t)cap * REC);
    tama_t *t = calloc(1, sizeof(tama_t));
    if (!buf || !t)
        return 2;

    tama_reset(t);
    /* One huge budget: run flat out until the trace buffer is full. */
    while (n < cap && !t->trapped && !t->halted)
        tama_run(t, t->cycles + 1000000);

    /* A named file, not stdout: on Windows stdout is a text stream and would
     * expand every 0x0A in the trace into 0x0D 0x0A, which corrupts the
     * records and then looks exactly like a CPU divergence. */
    FILE *f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", out);
        return 2;
    }
    fwrite(buf, REC, (size_t)n, f);
    fclose(f);
    fprintf(stderr, "traced %ld instructions%s%s\n", n,
            t->trapped ? " (TRAPPED)" : "", t->halted ? " (halted)" : "");
    return 0;
}
