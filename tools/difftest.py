"""Differential test: the recompiled core against an independent reference.

The emitter turns 36 opcodes' worth of flag semantics into C by hand, and the
decimal-mode and carry edges are exactly where a bug hides without ever
crashing. The only way to know they are right is to run someone else's
implementation beside ours and compare every instruction.

The reference is BrickEmuPy's E0C6200 core (CC0), driven here through a stub
interconnect. It was written from the same Epson documentation but by someone
else, from scratch, in another language -- which is what makes agreement worth
anything.

    python tools/difftest.py <rom> <tracedump.exe> [count]

Interrupts and timers are frozen on both sides. The reference advances its
oscillator inside every clock() while our runtime only moves time in
tama_step, so leaving them running would compare timer scheduling rather than
the CPU -- the ROM reads the interrupt-factor register at 0xF00, and a single
tick of difference shows up there as a bogus divergence.
"""
import os
import subprocess
import sys

REC = 10


class StubInterconnect:
    """The reference core wants a port fabric. Nothing here drives any pin."""

    def register_port_device(self, dev):
        pass

    def port_read(self, port):
        return 0

    def port_write(self, port, value, pushpull=None):
        pass

    def __getattr__(self, _name):
        # The core calls out to emit_port, emit_audio and friends as it runs.
        # A trace comparison cares about none of them, so swallow the lot
        # rather than chase each one as it turns up.
        return lambda *a, **kw: None


def load_reference(refdir):
    """Import BrickEmuPy's core from an extracted checkout."""
    sys.path.insert(0, refdir)
    pkg = os.path.join(refdir, "cores")
    if not os.path.isdir(pkg):
        raise SystemExit("no cores/ directory under %s" % refdir)
    open(os.path.join(pkg, "__init__.py"), "a").close()
    from cores.E0C6200 import E0C6200
    return E0C6200


def reference_trace(E0C6200, rom, count):
    """Run the reference for `count` instructions, yielding state per step."""
    mask = {"rom_path": rom, "port_pullup": {"K0": 15, "K1": 15},
            "p3_dedicated": 0}
    core = E0C6200(mask, 32768, StubInterconnect())
    core._IF = 0                       # interrupts off; see the module docstring
    core._clock_OSC1 = lambda: None    # and freeze the timers with them

    out = []
    for _ in range(count):
        core._IF = 0
        pc = core._PC
        out.append((pc, core._A, core._B, core._IX, core._IY, core._SP,
                    int(core._CF) | (int(core._ZF) << 1) | (int(core._DF) << 2)))
        if core._HALT:
            break
        core.clock()
    return out


def ours(exe, count):
    import tempfile
    path = os.path.join(tempfile.gettempdir(), "tama_trace.bin")
    subprocess.run([exe, str(count), path], check=True)
    with open(path, "rb") as f:
        raw = f.read()
    out = []
    for i in range(len(raw) // REC):
        b = raw[i * REC:(i + 1) * REC]
        out.append((b[0] | (b[1] << 8), b[2], b[3],
                    b[4] | (b[5] << 8), b[6] | (b[7] << 8), b[8], b[9] & 7))
    return out


FIELDS = ("pc", "A", "B", "X", "Y", "SP", "flags")


def main(rom, exe, count=200000, refdir=None):
    count = int(count)
    refdir = refdir or os.environ.get("BRICKEMU_DIR")
    if not refdir:
        raise SystemExit("set BRICKEMU_DIR to an extracted BrickEmuPy checkout")

    E0C6200 = load_reference(refdir)
    print("running reference for %d instructions..." % count)
    ref = reference_trace(E0C6200, rom, count)
    print("running recompiled core...")
    got = ours(exe, count)

    n = min(len(ref), len(got))
    print("comparing %d instructions" % n)

    for i in range(n):
        if ref[i] == got[i]:
            continue
        print("\nDIVERGENCE at instruction %d (pc=0x%04X)" % (i, ref[i][0]))
        for j, name in enumerate(FIELDS):
            mark = "  <--" if ref[i][j] != got[i][j] else ""
            print("  %-5s reference %04X   ours %04X%s"
                  % (name, ref[i][j], got[i][j], mark))
        print("\n  last 4 agreeing instructions:")
        for k in range(max(0, i - 4), i):
            print("    %5d  pc=%04X A=%X B=%X X=%03X Y=%03X SP=%02X F=%X"
                  % (k, ref[k][0], ref[k][1], ref[k][2], ref[k][3],
                     ref[k][4], ref[k][5], ref[k][6]))
        return 1

    print("\nno divergence in %d instructions" % n)
    if len(ref) != len(got):
        print("(streams ended at different lengths: reference %d, ours %d)"
              % (len(ref), len(got)))
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
