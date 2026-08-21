# tamarecomp

**A static-recompilation toolkit for the Tamagotchi P1 — and, as far as we can tell, the first static recompiler ever built for a 4-bit CPU.**

<p align="center">
  <img src="docs/screenshot.png" width="484"
       alt="The Tamagotchi P1 running in a window: a 32x16 dot-matrix LCD in
            olive green showing the hatched creature, with three round buttons
            beneath it.">
</p>

<p align="center"><em>Twenty seconds after power-on. Every instruction on that
screen came out of a C compiler, not an interpreter.</em></p>

The original 1996 Tamagotchi runs an **Epson E0C6S46**: a 4-bit microcontroller
with a 12-bit instruction word, 6144 words of mask ROM, and a 320-segment LCD
driver welded to the side of it. The whole machine is 9,216 bytes of
instruction space and a handful of fixed-function peripherals.

`tamarecomp` turns that ROM into native C. Not an interpreter with a ROM blob
attached — the instructions become C, one label per instruction word, direct
jumps compiled to `goto`.

Two properties of the E0C6200 make it work, and both are unusual:

- **The only page-changing mechanism on the core loads a constant.** Jumps
  carry just the low 8 bits; the rest comes from a latch that only `PSET`
  writes, and always with an immediate. So every jump target in the ROM is
  knowable at build time — the retail ROM has **zero unresolved control
  transfers**.
- **No instruction reads program memory as data.** Not one, in the whole
  36-instruction set. Constants cannot be *loaded* out of ROM, so they are
  *executed* out of it instead, as runs of byte-emitting instructions. That is
  why 95% of this ROM is code, and why there is no data segment to guess at.

> **No ROM data here.** `tama.b` is `.gitignore`d. This repo is the decoder,
> the analyzer, the emitter, the runtime and the frontends — bring your own dump.

---

## Status

It boots, keeps time, animates, takes button presses and sounds its chime.

| Stage | State |
|---|---|
| **E0C6200 decoder** — all 4096 opcodes | ✅ complete, validated |
| **Control-flow analysis** — NPC propagation, reachability | ✅ complete, 0 unresolved |
| **JPBA jump-table resolution** — all target pages classified | ✅ complete |
| **C emitter** — ROM → native C | ✅ complete |
| **E0C6S46 runtime** — timers, interrupts, K0 buttons | ✅ complete |
| **LCD** — the 32×16 dot matrix | ✅ complete |
| **Buzzer** — tone, one-shot, WAV recording | ✅ complete |
| **Frontends** — terminal, and an optional SDL window | ✅ complete |
| **Differential test** — 1M instructions vs an independent core | ✅ zero divergence |
| **Icons** | ✅ none exist — they are drawn in the matrix |

```
$ ctest
1/7 smoke ....... Passed    boots, runs a minute, never leaves the ROM
2/7 lcd ......... Passed    the screen changes over time
3/7 buttons ..... Passed    a press puts something new on screen
4/7 icons ....... Passed    nothing is driven outside the matrix
5/7 buzzer ...... Passed    the power-on chime, 2340 Hz
6/7 difftest .... Passed    1,000,000 instructions, no divergence
7/7 sdl ......... Passed    the window renders, headless

100% tests passed, 0 tests failed out of 7
```

The emitter turns the 6144-word ROM into ~47,500 lines of C that builds clean
under `-Wall -Wextra` at `-O2`. Run flat out with no timers it does 64 million
cycles in under a second — roughly **2,000× the hardware's 32,768 Hz**.

## Usage

```sh
# analysis only -- the ISA self-checks run without a ROM
python tools/analyze.py     path/to/tama.b   # the ROM's control-flow shape
python tools/jumptables.py  path/to/tama.b   # classify and dump every jump table
python tests/test_decode.py path/to/tama.b   # self-checks

# build and run
cmake -B build -DTAMA_ROM=path/to/tama.b
cmake --build build

./build/tama                       # a Tamagotchi in your terminal; 1/2/3 are the buttons
./build/tama --record chime.wav 4  # record what the buzzer did
./build/tama-sdl                   # a window with clickable buttons, if SDL2 was found

ctest --test-dir build             # everything but the differential test
BRICKEMU_DIR=/path/to/BrickEmuPy-main ctest --test-dir build   # + difftest
```

SDL2 is optional. Configure with the vcpkg toolchain or
`-DCMAKE_PREFIX_PATH=<sdl2-prefix>`; without it the rest still builds and only
the `tama-sdl` target is skipped.

## Documentation

| | |
|---|---|
| [**How the analysis works**](docs/ARCHITECTURE.md) | the NPC property, what the analyzer finds, why the ROM is 95% code, the JPBA jump tables, ISA gotchas |
| [**The emitter**](docs/RECOMPILER.md) | what the generated C looks like, how the dispatch stays sound, the bank-bit bug |
| [**The device around the CPU**](docs/HARDWARE.md) | interrupts and timers, the LCD segment map, the buzzer, buttons, both frontends |
| [**Validation**](docs/VALIDATION.md) | a million instructions against an independent core, and the flag bug it found |

## Layout

```
tamarecomp/
├── tools/
│   ├── e0c6200.py       ISA decoder — flat mask table, all 4096 opcodes
│   ├── analyze.py       NPC propagation, reachability fixpoint, JPBA discovery
│   ├── jumptables.py    JPBA target-page classification and table resolution
│   ├── cycles.py        per-opcode cycle counts (generated by gen_cycles.py)
│   ├── emit.py          ROM → C
│   └── difftest.py      per-instruction comparison against a reference core
├── src/
│   ├── hw.c             E0C6S46 I/O map, timers, interrupt delivery, buzzer
│   ├── lcd.c            display RAM → 32×16 pixels
│   ├── main.c           terminal frontend, and WAV recording
│   └── sdl_main.c       windowed frontend (optional)
├── include/tamarecomp/  public headers
├── tests/               five C tests plus the Python self-checks
├── generated/           emitted C (gitignored)
└── docs/
```

## Credits

- Opcode table cross-checked against **[BrickEmuPy](https://github.com/azya52/BrickEmuPy)**'s
  `E0C6200dasm.py` (CC0) and the Epson E0C6200/E0C6200A core manual.
- **[TamaLIB](https://github.com/jcrona/tamalib)** by Jean-Christophe Rona is
  the other open Tamagotchi implementation, and the reason anyone knows this
  hardware is tractable at all.

BrickEmuPy in particular does double duty here: its core is the oracle
`tools/difftest.py` compares against, and its `TamagotchiP1.svg` is where the
LCD segment map came from. Being CC0 is what made both possible.

## License

MIT. See [LICENSE](LICENSE) — that covers the code in this repository: the
decoder, the analyzer, the emitter, the runtime and the frontends.

It does not and cannot cover the ROM. No ROM ships here, `tama.b` is
`.gitignore`d, and the C the emitter produces is a derivative of the ROM you
feed it — so whatever you generate is yours to keep to yourself. The
disassembly quoted in these docs is a couple of dozen words out of 6144, shown
to explain how the analysis works.

*Tamagotchi* is a trademark of Bandai. This project is not affiliated with or
endorsed by them; the name is used only to say which hardware it is.
