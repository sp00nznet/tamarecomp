# How the analysis works

What makes this ROM statically recompilable at all, and what the analyzer
finds when it looks.

[&larr; back to the README](../README.md)

---

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

## What the analysis says about the retail ROM

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

## Why the ROM is 95% code

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

## JPBA and the jump tables

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

## Notes on the ISA

Two encodings are worth knowing about because they are where a hand-written
decoder goes wrong:

- **`RLC r` encodes its register twice** (`1010 1111 r1r0 r1r0`). A word with a
  mismatched pair is not an instruction. There are 12 of these.
- The E0C6200 has **exactly 74 unassigned opcodes** out of 4096. The test suite
  asserts that number — if a decoder edit moves it, the edit invented or lost an
  instruction.
- `RETD` and `LBPX` look like nonsense when you meet them in a disassembly.
  They are the ROM's data encoding, not stray decodes — see
  [Why the ROM is 95% code](#why-the-rom-is-95-code).
