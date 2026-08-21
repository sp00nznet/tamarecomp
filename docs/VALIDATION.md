# Validation

Running an independent implementation of the same ISA beside ours, and what
that turned up.

[&larr; back to the README](../README.md)

---

## One million instructions, no divergence

36 opcodes' worth of flag semantics were written into the emitter by hand, and
the carry and decimal edges are where a bug hides without ever crashing.
`tools/difftest.py` runs BrickEmuPy's E0C6200 core beside ours and compares
`pc, A, B, X, Y, SP, flags` on every instruction. It is a worthwhile check
precisely because that core was written by someone else, from the same Epson
documentation, in another language.

Recompiled code has no per-instruction observation point — that is rather the
point of it — so the emitter carries a `TAMA_TRACE` hook that compiles to
nothing unless `TAMA_TRACING` is defined.

**It found a real bug 1,486 instructions in.**

```
RST F, i   is   F <- F AND i
```

It *keeps* the bits named in `i` and clears the rest, which is the opposite of
the obvious reading, and the emitter had it inverted. Completely silent: the
ROM ran, the screen drew, the device animated, and a comparison flag was
quietly wrong. With it fixed the boot path writes twice as many display
nibbles and the sprite resolves into a coherent creature.

Two more failures showed up first, neither of them a CPU bug, both worth
knowing about because each looks exactly like one:

- Windows `stdout` is a text stream, and it expanded every `0x0A` byte in the
  binary trace into `0x0D 0x0A`. The giveaway was `X = 0x0A0D` — an impossible
  value for a 12-bit register, with the culprit bytes sitting right there in
  it.
- The reference advances its oscillator inside every `clock()` while our
  runtime only moves time in `tama_step`, so the ROM's read of the
  interrupt-factor register at `0xF00` disagreed by one tick. Timers are frozen
  on both sides now; the test is about the CPU.

After that: **1,000,000 instructions, zero divergence.**
