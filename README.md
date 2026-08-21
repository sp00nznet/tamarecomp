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

Decoder and control-flow analysis are done and self-checking. The C emitter is
next.

| Stage | State |
|---|---|
| **E0C6200 decoder** — all 4096 opcodes | ✅ complete, validated |
| **Control-flow analysis** — NPC propagation, reachability | ✅ complete, 0 unresolved |
| **JPBA jump-table resolution** — narrow 14 sites to real targets | 🔨 next |
| **C emitter** — ROM → native C | 🔨 next |
| **E0C6S46 runtime** — LCD, timers, buzzer, buttons, interrupts | ⬜ not started |
| **Host frontend** — render the 32×16 dot matrix + icons | ⬜ not started |

### What the analysis says about the retail ROM

```
words           6144 (0x1800), 9216 bytes of instruction space
decoded         6144  (100.0%)
illegal words   0
entry points    98 (7 vectors + 91 call targets)
unresolved      0

JPBA sites      14, each with exactly 1 target page

proven code      2006  32.6%
+ JPBA pages     2304  37.5%  (9 pages)
code upper bnd   3912  63.7%
provably data    2232  36.3%
```

**Every one of the 6144 words is a legal E0C6200 instruction** — but that is a
statement about the ISA's density, not about the ROM. The number that counts is
the third block: 2006 words are reachable from the seven hardware vectors
through control flow the analyzer can prove, and 2232 words can be proven to be
*data* (sprites, the character tables, the LCD segment maps). The 1906 words in
between sit inside pages that a `JPBA` can reach, and resolving those 14 jump
tables is what closes the gap.

The opcode histogram is the sanity check that the reachable set is real code:

| | reachable-only trace | naive linear sweep |
|---|---|---|
| `LD` | 707 | 1073 |
| `LBPX` | **67** | **2069** |
| `RETD` | — | 1057 |

`LBPX` and `RETD` are encoded as `0x9nn` and `0x1nn`, which is exactly what
sprite data looks like when you disassemble it by accident. A trace that reports
thousands of them is reading pixels. Ours doesn't.

## Layout

```
tamarecomp/
├── tools/
│   ├── e0c6200.py       ISA decoder — flat mask table, all 4096 opcodes
│   └── analyze.py       NPC propagation, reachability, JPBA site discovery
├── tests/
│   └── test_decode.py   self-checks (ISA coverage + whole-ROM properties)
├── include/tamarecomp/  runtime headers (CPU context, E0C6S46 peripherals)
├── src/                 runtime implementation
├── generated/           emitted C (gitignored)
└── docs/
```

## Usage

```sh
python tools/analyze.py path/to/tama.b     # report the ROM's control-flow shape
python tests/test_decode.py path/to/tama.b # run the self-checks
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

## Credits

- Opcode table cross-checked against **[BrickEmuPy](https://github.com/azya52/BrickEmuPy)**'s
  `E0C6200dasm.py` (CC0) and the Epson E0C6200/E0C6200A core manual.
- **[TamaLIB](https://github.com/jcrona/tamalib)** by Jean-Christophe Rona is the
  reference emulator this project validates against.

## License

MIT. See [LICENSE](LICENSE).
