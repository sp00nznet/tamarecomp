# The emitter

How 6144 instruction words become one C function, and why that is sound.

[&larr; back to the README](../README.md)

---

## The recompiled output

```
$ python tools/emit.py tama.b generated/tama_rom.c
generated/tama_rom.c: 47567 lines from 6144 ROM words
```

Builds clean under `-Wall -Wextra` at `-O2`. Left to run flat out with no
timers it does 64 million cycles in under a second — roughly **2,000× the
hardware's 32,768 Hz**.

## What the generated C looks like

Sequential instructions fall through with no branch at all. Direct jumps are
`goto`. This is `PSET`/`JPBA`/`CALL` around address `0x0546`:

```c
L_0546: /* A80  ADD A, A */
    t->cycles += 7;
    r = t->a + t->a;
    t->cf = r > 15;
    if (t->df && r > 9) { r += 6; t->cf = 1; }
    t->zf = (r & 0xF) == 0;
    t->a = r & 0xF;
    CHECK_BUDGET(0x0547);
L_0548: /* E47  PSET 0x07 */
    t->cycles += 5;                     /* and nothing else -- see below */
L_0549: /* FE8  JPBA */
    t->cycles += 5;
    t->pc = 0x0700 | (t->b << 4) | t->a;
    goto dispatch;
L_054B: /* 407  CALL 0x07 */
    t->cycles += 7;
    MW((t->sp - 1) & 0xFF, (0x054C >> 8) & 0xF);
    MW((t->sp - 2) & 0xFF, (0x054C >> 4) & 0xF);
    t->sp = (t->sp - 3) & 0xFF;
    MW(t->sp, 0x054C & 0xF);
    goto L_0307;                        /* PSET 0x03 + CALL 0x07, resolved */
```

**`PSET` emits nothing at all** — all 162 of them stop being control flow and
become compile-time facts, which is the payoff from the NPC analysis. Not even
its one-instruction interrupt hold-off survives: that exists to keep an
interrupt out of the gap between a `PSET` and the jump consuming the page it
latched, and generated code has no interrupt point there. Every reachable
`PSET` in this ROM is immediately followed by a control transfer, which the
test suite asserts.

(The listing omits one line per instruction: a `TAMA_TRACE` hook that compiles
to nothing unless the build asks for a trace. See
[Validation](VALIDATION.md).)

## Soundness, and the one bug that mattered

Only two things reach the dispatch switch: `RET`/`RETS`/`RETD`, whose return
address is popped out of RAM, and `JPBA`. The switch covers **every one of the
6144 words**, so a computed transfer can never land somewhere without code. The
jump-table analysis is not load-bearing here — it explains what the tables are,
but the emitter would be correct without it.

`CALL`/`RET` are *not* mapped onto the C call stack. The three return-address
nibbles are pushed into RAM at `SP` exactly as the hardware does, because the
program is free to read or rewrite them and proving otherwise would need a
stack-discipline argument this ROM does not offer.

The first build trapped at `pc=0x1CB9` — an address outside a 6144-word ROM —
after 6767 cycles. `RET` takes the bank bit from the *live* PC, but recompiled
code has no live PC between dispatch points, so it was reading a stale value
left by the previous dispatch. The bank is statically known at every `RET`
site, so it is now baked in as a constant. Worth writing down because it is the
characteristic static-recompilation bug: state the hardware keeps implicitly in
a register has to become either an explicit variable or a compile-time
constant, and picking neither fails quietly until it doesn't.
