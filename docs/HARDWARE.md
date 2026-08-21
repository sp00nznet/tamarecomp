# The device around the CPU

The E0C6S46 peripherals the recompiled ROM talks to, and the two frontends
that draw the result.

[&larr; back to the README](../README.md)

---

## Interrupts, for free

The recompiled ROM has no notion of time, and nothing in the generated code
knows interrupts exist. It did not need to: `tama_run` already returns when its
cycle budget is spent, which is exactly the shape interrupt delivery wants.
`tama_step` sets the budget to the next timer edge, so the CPU comes back on
its own at the moment something is due, and the runtime pushes three nibbles
and vectors just like a `CALL`. No polling, no per-instruction check, no
regeneration.

The clock timer is the device's heartbeat: an 8-bit counter stepping at 256 Hz
whose bits 2, 4, 6 and 7 falling give the 32/8/2/1 Hz interrupt factors. The
ROM unmasks the **1 Hz** one, and that is what makes a Tamagotchi age, get
hungry, and misbehave.

The peripheral work was scoped by measurement rather than by implementing all
of `0xF00-0xF7F` on spec: an instrumented run showed the ROM touches 21
registers, all of them during init.

## The LCD

The E0C6S46 spreads 640 segments over 160 nibbles in two blocks. Vertically the
arrangement is regular — each screen column owns four nibbles, each holding
four vertically adjacent pixels. **Horizontally there is no formula at all.**
The segment lines are PCB routing: they count up in twos, skip a pair at
x=8, and then run *backwards* from x=16.

```c
nibble = COL_NIBBLE[x] + ((y >> 2) & 1) + (y >= 8 ? 80 : 0);
bit    = y & 3;

{  0,  2,  4,  6,  8, 10, 12, 14,     /* x 0-7:   straight run         */
  18, 20, 22, 24, 26, 28, 30, 32,     /* x 8-15:  after a skipped pair */
  72, 70, 68, 66, 64, 62, 60, 58,     /* x 16-23: and now backwards    */
  54, 52, 50, 48, 46, 44, 42, 40 }    /* x 24-31: still backwards      */
```

`COL_NIBBLE` is read off the segment geometry in BrickEmuPy's
`TamagotchiP1.svg`, whose element ids are `<nibble>_<bit>` and whose positions
resolve to exactly 32 distinct x by 16 distinct y. The table reproduces all 512
cells with nothing left over.

Assuming a formula here is the trap, and it is a quiet one. `2 * x` is correct
for x = 0..7 and wrong for the other 24 columns — which still draws *a*
picture, just not the one the ROM meant. The sprite happened to sit in the
left eighth of the screen, so it looked plausible for a while.

## There are no icons

The P1 has an icon row — food, light, game, medicine and the rest — and the
obvious assumption is that each is its own LCD segment, sitting in the 32
nibbles the dot matrix leaves unused. It is not.

Instrumenting every address in `0xE00-0xEFF` across a run that walks the whole
menu shows the ROM changing 88 addresses, **every one of them inside the matrix
and none outside it**. The icon row is drawn *in the dot matrix*, which is also
why the face SVG defines exactly 512 segments and nothing else. So there is no
icon code, and `tests/iconprobe.c` fails if that ever stops being true.

## The buzzer

`R4` bit 3 is the buzzer enable, **active low**, and `BZ1` picks one of eight
tones by dividing OSC1 — 4096 Hz down to 1170 Hz, the entire vocabulary a
Tamagotchi has for being pleased or annoyed with you. `BZ2` carries the
envelope and a one-shot pulse whose "still ringing" bit reads back, which is
how the ROM waits for a beep to finish.

The device sounds a power-on chime and the register trace shows exactly how:

```
0.1295s  R4=F BZ1=3 BZ2=0  ->    0 Hz    tone selected, buzzer still disabled
0.1299s  R4=7 BZ1=3 BZ2=0  -> 2340 Hz    bit 3 pulled low
1.4728s  R4=F BZ1=3 BZ2=0  ->    0 Hz    released
```

`tama --record out.wav 4` writes that to a mono WAV with no audio library
involved — the buzzer is a square wave and nothing more, so it is a phase
accumulator and a sign. The recorded chime measures 2340 Hz lasting 1.343 s,
matching the register trace.

One trap worth naming: a sample at 22050 Hz is 1.49 CPU cycles, and `tama_run`
cannot stop mid-instruction. Stepping by a fixed 1 or 2 cycles per sample
silently advances 5 to 12 and the recording comes out **4.7× too fast** —
still a clean tone at the right pitch, just wrong about time. Each sample is
anchored to an absolute cycle instead.

## Buttons

Three buttons on `K0`, active low, with a falling edge raising the K0
interrupt factor. **Left is bit 2 and right is bit 0** — taken from
`TamagotchiP1.brick`, because a left-to-right numbering gets them mirrored and
nothing about a mirrored Tamagotchi looks wrong until you try to use a menu.

## Frontends

Nothing that draws lives in the emulator. `src/lcd.c` does not render — it is a
pure accessor over display RAM:

```c
int  tama_lcd_pixel(const tama_t *t, int x, int y);
void tama_lcd_read (const tama_t *t, uint8_t out[16][32]);
```

So a frontend is three calls — `tama_audio_step` to advance time and pull
samples, `tama_lcd_read` for pixels, `tama_set_buttons` for input — and the
core has no idea it is being drawn. There are two, and neither one required
touching anything else.

**`tama`** — the terminal. Two pixel rows share a line via half-block
characters, redrawing only when something changes. No dependencies at all.
`tama --record out.wav 4` writes what the buzzer did.

**`tama-sdl`** — a window with clickable buttons and audible sound. Optional:
without SDL2 the rest of the project still builds and only this target is
skipped.

```
cmake -B build -DTAMA_ROM=tama.b -DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows
./build/tama-sdl
```

Click the three buttons or use 1/2/3; escape quits. Audio is queued rather than
callback-driven, because the emulator already produces samples in step with its
own clock — there is nothing to synchronise and no lock to get wrong.

`tama-sdl --shot out.bmp 2` runs unpaced and writes one frame, which works
under `SDL_VIDEODRIVER=dummy`. That exists because a renderer you cannot look
at is a renderer you have not tested, and it is what the `sdl` ctest runs.

### Time comes from the audio clock

Both frontends drive the machine through `tama_audio_step`, which advances it
to the cycle a given sample falls on and returns that sample. Running the clock
off the audio rate keeps picture and sound in lockstep for free, and it puts
the one subtle bit in exactly one place: a sample is a *fraction* of a CPU
cycle — 1.49 of them at 22050 Hz — and `tama_run` cannot stop mid-instruction.
Step by a fixed amount per sample and it silently advances 5 to 12 instead,
running several times too fast. Getting that wrong once was enough.
