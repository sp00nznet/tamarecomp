# tamarecomp

**A static-recompilation toolkit for the Tamagotchi P1 — and, as far as we can tell, the first static recompiler ever built for a 4-bit CPU.**

The original 1996 Tamagotchi runs an **Epson E0C6S46**: a 4-bit microcontroller
with a 12-bit instruction word, 6144 words of mask ROM, 4-bit data, and a
320-segment LCD driver welded to the side of it. The entire machine is 9,216
bytes of instruction space and a handful of fixed-function peripherals.

That shape is close to ideal for static recompilation. There is one CPU. There
is no cartridge, no bank switching beyond a single bank bit, no self-modifying
code (the ROM is mask ROM — it *cannot* be written), and — the part that
matters — **the only page-changing mechanism on the whole core loads a
constant**. Every jump target in the ROM is knowable at build time.

It also has a quirk that turns out to be the single most important fact about
this ROM: **the E0C6200 has no instruction that reads program memory as data.**
Not one. Constants therefore cannot be *loaded* out of ROM — they have to be
*executed* out of it. See [Why the ROM is 95% code](#why-the-rom-is-95-code).

`tamarecomp` translates that ROM into native C. Not an interpreter with a ROM
blob attached: the instructions become C.

> **No ROM data here.** `tama.b` is `.gitignore`d. This repo is the decoder,
> the analyzer, the runtime, and docs — bring your own dump.

---

## Why this one

The library already covers 6502, Z80, 68k, MIPS, PowerPC, ARM, x86, SH — the
usual suspects, from [lynxrecomp](https://github.com/sp00nznet/lynxrecomp) up to
[ps3recomp](https://github.com/sp00nznet/ps3recomp). A 4-bit Epson core is a
genuinely new notch, and it comes with a property none of the big targets have:

**the ROM is small enough to lift *completely*, and provably so.**

On a PS3 title you recompile what you can find and shim the rest. Here the
whole address space fits in one analysis pass, every instruction word gets
decoded exactly once, and you can print a number that says how much of the
machine you actually own.

## The property that makes it work

The E0C6200 core has a 13-bit PC split into a bank bit, a 4-bit page, and an
8-bit step. Jumps only carry the step — the bank and page come from a latch
called **NPC**, and only two things ever write it:

- `PSET p` loads a **constant** 5-bit bank:page, or
- any other instruction sets it to the address of the instruction itself.

So NPC is a single abstract value that a trace can propagate exactly. Given the
NPC reaching an instruction, `JP s` / `CALL s` targets are constants. `CALZ s`
is always page 0. There is exactly one computed branch on the entire core —
`JPBA`, which takes its 8-bit step from registers `B:A` — and even that lands in
a page the analysis already knows, so it is a bounded jump table rather than an
open indirect branch.

Result on the retail ROM: **zero unresolved control transfers.**

## Status

The ROM compiles to C and the C runs. What is missing is the hardware around
it: the LCD, the timers that drive the clock, and the buttons.

| Stage | State |
|---|---|
| **E0C6200 decoder** — all 4096 opcodes | ✅ complete, validated |
| **Control-flow analysis** — NPC propagation, reachability | ✅ complete, 0 unresolved |
| **JPBA jump-table resolution** — classify and resolve all target pages | ✅ complete |
| **C emitter** — ROM → native C | ✅ complete, runs 64M cycles clean |
| **E0C6S46 runtime** — LCD, timers, buzzer, buttons, interrupts | 🔨 next |
| **Host frontend** — render the 32×16 dot matrix + icons | ⬜ not started |

### The recompiled output

```
$ python tools/emit.py tama.b generated/tama_rom.c
generated/tama_rom.c: 41631 lines from 6144 ROM words

$ clang -std=c11 -O2 -Wall -Wextra -Iinclude ...
$ ./smoke
ok: 64000324 cycles, pc=0x00F0, sp=0xF3, a=0 b=9 x=1B0 y=100,
    0 halt-resumes, 8 display nibbles set, no traps
```

64 million cycles in under a second — roughly **2,000× the real hardware's
32,768 Hz** — with no traps, and eight nibbles of display RAM written, meaning
the boot path got as far as the LCD driver rather than spinning in place.

It builds clean under `-Wall -Wextra` at `-O2`.

### What the generated C looks like

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
    t->cycles += 5;
    t->if_delay = 1;                    /* the page it loads is compile-time */
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

`PSET` leaves nothing behind but its one-instruction interrupt hold-off — all
162 of them stop being control flow and become compile-time facts. That is the
payoff from the NPC analysis.

### Soundness, and the one bug that mattered

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

### What the analysis says about the retail ROM

```
words           6144 (0x1800), 9216 bytes of instruction space
decoded         6144  (100.0%)
illegal words   0
entry points    144 (7 vectors + 137 call targets)
unresolved      0

JPBA sites      25 across 17 pages, each site resolving to 1 page
reachable code   5841  95.1%
unreached         303  4.9%  (mostly end-of-page NOP padding)
```

Every one of the 6144 words is a legal E0C6200 instruction, and 95.1% of them
are reachable from the seven hardware vectors through control flow the analyzer
can prove. Nothing is unresolved. The 303 words left over are end-of-page `NOP7`
padding, the unused second word of each interrupt-vector slot, and a couple of
dead runs — the largest single block is the 220 words of padding that follow the
bank-1 dispatcher at `0x1024`.

### Why the ROM is 95% code

That number looks wrong for a device whose job is mostly drawing sprites. It
isn't, and the reason is the ISA: **there is no load-from-ROM instruction on
this core.** The full mnemonic set is 36 instructions wide and not one of them
can read program memory as data.

So the ROM stores its constants as instructions that emit them:

| | |
|---|---|
| `RETD l` | write `l`'s two nibbles to `M(X)`, `M(X+1)`, bump X by 2, **and return** |
| `LBPX MX, l` | the same store, without the return |

A one-word `RETD` is a subroutine that hands its caller a byte. A run of `LBPX`
closed by a `RETD` is a string. A page of them, entered via `JPBA` with the
index in `B:A`, is an array. The Tamagotchi's sprite tables, animation
sequences and text are all of this shape — which is why `LBPX` and `RETD` are
the two most common opcodes in the ROM, at 1874 and 1035 uses.

This is the property that makes the whole project work. There is no data
segment to guess at and no ambiguity about where code stops. **Almost the
entire ROM is code, by necessity, and all of it is liftable.**

### JPBA and the jump tables

`JPBA` is the core's only computed branch: it takes the 8-bit step from `B:A`
while the bank and page still come from the NPC latch, so the page is always
known and the only question is which of its 256 steps are real entry points.

Resolving them is a **fixpoint**, not a single pass — a resolved table opens
pages that contain further `JPBA` sites. Bank 1 is the reason: its 2048 words
are reachable *only* through one eight-entry re-dispatcher at the top of page
`0x10`, one entry per bank-1 page. Until that table is resolved, a third of the
ROM is invisible. The trace starts at 14 sites and converges at 25 across 17
pages.

`tools/jumptables.py` classifies each target page into one of four shapes:

| page | kind | targets | used by |
|---|---|---|---|
| `0x0300` | **jump** | 7 | `0x030B` |
| `0x0700` | **jump** | 8, 2-word `PSET`+`JP` | `0x0549` `0x07D7` |
| `0x1000` | **tramp** | 8, 4-word re-dispatch | `0x1003` `0x1023` |
| `0x0A00` `0x0B00` `0x0C00` `0x0D00` `0x0E00` `0x1100` `0x1300`–`0x1700` | **byte** | 134–256 | 11 pages |
| `0x0800` `0x0900` `0x1200` | **code** | 256, unnarrowed | 6 sites |

The two `jump` tables resolve to exact target lists:

```
page 0x0300, 7 entries, 1-word       page 0x0700, 8 entries, 2-word
  [0] 0x0300  RET                      [0] 0x0700  PSET 0x04; JP 0xE7 -> 0x04E7
  [1] 0x0301  JP 0x0C -> 0x030C        [1] 0x0702  PSET 0x07; JP 0x10 -> 0x0710
  [2] 0x0302  JP 0x1C -> 0x031C        [2] 0x0704  PSET 0x0A; JP 0xBE -> 0x0ABE
  [3] 0x0303  JP 0x24 -> 0x0324        [3] 0x0706  PSET 0x06; JP 0x24 -> 0x0624
  [4] 0x0304  JP 0x29 -> 0x0329        [4] 0x0708  PSET 0x06; JP 0x00 -> 0x0600
  [5] 0x0305  JP 0x4B -> 0x034B        [5] 0x070A  PSET 0x0D; JP 0x7E -> 0x0D7E
  [6] 0x0306  JP 0x74 -> 0x0374        [6] 0x070C  PSET 0x08; JP 0x14 -> 0x0814
                                       [7] 0x070E  PSET 0x0A; JP 0xDA -> 0x0ADA
```

Slot 0 of page `0x0300` being a bare `RET` is the idiom for "index 0 means do
nothing" — worth knowing, because a classifier that insists every entry be a
`JP` finds a zero-entry table there.

**On soundness.** The fully sound target set for a `JPBA` is all 256 steps of
its page, since nothing in the ROM proves the index range. That answer is
useless in practice: feed it back into the trace and the analyzer lands in the
middle of a 4-word trampoline, which invents control flow, which invents more
`JPBA` pages, until every word in the ROM "is code". The first version of this
analysis reported 99.5% reachable for exactly that reason, and the tell was
`LBPX` appearing 2069 times in a trace that was reading pixels. The classifier
trusts each table's *shape* instead. Pages it cannot classify keep all 256. If a
future ROM breaks an assumption it surfaces as an unreachable table entry, not
as silently wrong output.

## Layout

```
tamarecomp/
├── tools/
│   ├── e0c6200.py       ISA decoder — flat mask table, all 4096 opcodes
│   ├── analyze.py       NPC propagation, reachability fixpoint, JPBA discovery
│   ├── jumptables.py    JPBA target-page classification and table resolution
│   ├── cycles.py        per-opcode cycle counts (generated)
│   ├── gen_cycles.py    regenerates cycles.py from the reference core
│   └── emit.py          ROM → C
├── tests/
│   ├── test_decode.py   self-checks (ISA coverage + whole-ROM properties)
│   └── smoke.c          runs the recompiled ROM, fails on any trap
├── include/tamarecomp/  runtime headers (CPU context, E0C6S46 peripherals)
├── src/                 runtime implementation
├── generated/           emitted C (gitignored)
└── docs/
```

## Usage

```sh
python tools/analyze.py path/to/tama.b     # report the ROM's control-flow shape
python tools/jumptables.py path/to/tama.b  # classify and dump every jump table
python tests/test_decode.py path/to/tama.b # run the analysis self-checks

cmake -B build -DTAMA_ROM=path/to/tama.b   # recompile the ROM and build it
cmake --build build
ctest --test-dir build                     # runs the smoke test
```

The ROM check is skipped if you don't pass a path, so the ISA tests run
anywhere.

## Notes on the ISA

Two encodings are worth knowing about because they are where a hand-written
decoder goes wrong:

- **`RLC r` encodes its register twice** (`1010 1111 r1r0 r1r0`). A word with a
  mismatched pair is not an instruction. There are 12 of these.
- The E0C6200 has **exactly 74 unassigned opcodes** out of 4096. The test suite
  asserts that number — if a decoder edit moves it, the edit invented or lost an
  instruction.
- `RETD` and `LBPX` look like nonsense when you meet them in a disassembly.
  They are the ROM's data encoding, not stray decodes — see above.

## Credits

- Opcode table cross-checked against **[BrickEmuPy](https://github.com/azya52/BrickEmuPy)**'s
  `E0C6200dasm.py` (CC0) and the Epson E0C6200/E0C6200A core manual.
- **[TamaLIB](https://github.com/jcrona/tamalib)** by Jean-Christophe Rona is the
  reference emulator this project validates against.

## License

MIT. See [LICENSE](LICENSE).
