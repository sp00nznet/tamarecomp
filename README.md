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

Decoder and control-flow analysis are done and self-checking. The C emitter is
next.

| Stage | State |
|---|---|
| **E0C6200 decoder** — all 4096 opcodes | ✅ complete, validated |
| **Control-flow analysis** — NPC propagation, reachability | ✅ complete, 0 unresolved |
| **JPBA jump-table resolution** — classify and resolve all target pages | ✅ complete |
| **C emitter** — ROM → native C | 🔨 next |
| **E0C6S46 runtime** — LCD, timers, buzzer, buttons, interrupts | ⬜ not started |
| **Host frontend** — render the 32×16 dot matrix + icons | ⬜ not started |

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
│   └── jumptables.py    JPBA target-page classification and table resolution
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
python tools/jumptables.py path/to/tama.b  # classify and dump every jump table
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
- `RETD` and `LBPX` look like nonsense when you meet them in a disassembly.
  They are the ROM's data encoding, not stray decodes — see above.

## Credits

- Opcode table cross-checked against **[BrickEmuPy](https://github.com/azya52/BrickEmuPy)**'s
  `E0C6200dasm.py` (CC0) and the Epson E0C6200/E0C6200A core manual.
- **[TamaLIB](https://github.com/jcrona/tamalib)** by Jean-Christophe Rona is the
  reference emulator this project validates against.

## License

MIT. See [LICENSE](LICENSE).
