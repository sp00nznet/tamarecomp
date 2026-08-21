"""Self-checks for the E0C6200 decoder and the ROM analysis.

Run:  python tests/test_decode.py [path/to/tama.b]
The ROM is optional -- the ISA checks run without it.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from e0c6200 import decode, load_rom          # noqa: E402
import analyze                                # noqa: E402
import jumptables                             # noqa: E402

# The E0C6200 has exactly 74 unassigned opcodes out of 4096. If a decoder edit
# changes this number it has either invented or lost an instruction.
ILLEGAL_OPCODES = 74


def test_isa():
    legal = [w for w in range(4096) if decode(0, w)]
    assert len(legal) == 4096 - ILLEGAL_OPCODES, \
        f"expected {ILLEGAL_OPCODES} illegal opcodes, got {4096 - len(legal)}"

    # spot-check the encodings the control-flow analysis depends on
    assert decode(0, 0x010).kind == "jp"
    assert decode(0, 0x210).cond == "C"
    assert decode(0, 0x710).cond == "NZ"
    assert decode(0, 0x412).kind == "call"
    assert decode(0, 0x512).kind == "calz"
    assert decode(0, 0xE4B).kind == "pset" and decode(0, 0xE4B).args == ("0x0B",)
    assert decode(0, 0xFE8).kind == "jpba"
    assert decode(0, 0xFDF).mnem == "RET"
    assert decode(0, 0xFDE).mnem == "RETS"
    assert decode(0, 0x1AB).mnem == "RETD"
    assert decode(0, 0xFF8).kind == "halt"
    # RLC encodes its register twice; a mismatched pair is not an instruction
    assert decode(0, 0xAF0).mnem == "RLC" and decode(0, 0xAF1) is None
    print("isa      ok  (%d legal, %d illegal)" % (len(legal), ILLEGAL_OPCODES))


def test_rom(path):
    words = load_rom(path)
    assert len(words) == 6144, f"expected 6144 words, got {len(words)}"
    ops, illegal, reached, entries, edges, unresolved = analyze.analyze(words)

    assert not illegal, f"{len(illegal)} words are not valid instructions"
    assert not unresolved, f"{len(unresolved)} control transfers are unresolved"

    # the reset vector is a JP into page 1
    assert ops[0x100].kind == "jp", "reset vector is not a JP"

    # every JPBA lands in a single statically-known page -- the property the
    # whole static-recompilation approach depends on
    assert analyze.JPBA_SITES, "no JPBA sites found"
    for a, pages in analyze.JPBA_SITES.items():
        assert len(pages) == 1, f"JPBA at 0x{a:04X} reaches {len(pages)} pages"

    assert len(reached) > 5800, f"only {len(reached)} words proven reachable"

    # everything the trace misses is padding or an unused vector slot -- no
    # reachable instruction should be left behind
    unreached = [a for a in range(len(words)) if a not in reached]
    assert len(unreached) < 400, f"{len(unreached)} words unreached"

    print("rom      ok  (%d words, %d reachable, %d JPBA sites, 0 unresolved)"
          % (len(words), len(reached), len(analyze.JPBA_SITES)))


def test_jump_tables(path):
    words = load_rom(path)
    analyze.analyze(words)
    ops = {a: decode(a, w) for a, w in enumerate(words)}
    kinds = {}
    for pages in analyze.JPBA_SITES.values():
        for p in pages:
            kinds[p] = jumptables.classify(ops, p)[0]

    # the two dispatch tables, with the targets they must resolve to
    assert kinds[0x0300] == "jump"
    assert kinds[0x0700] == "jump"
    _, steps, stride = jumptables.classify(ops, 0x0300)
    assert len(steps) == 7 and stride == 1
    _, steps, stride = jumptables.classify(ops, 0x0700)
    assert len(steps) == 8 and stride == 2
    assert [(0x0700 | s) for s in steps][:2] == [0x0700, 0x0702]

    # bank 1 is gated by a single 8-entry re-dispatcher at the top of page 0x10
    assert kinds[0x1000] == "tramp"
    _, steps, stride = jumptables.classify(ops, 0x1000)
    assert len(steps) == 8 and stride == 4

    # ... and the pages it opens are byte tables, not loose code
    assert sum(1 for p, k in kinds.items() if k == "byte") >= 10

    print("tables   ok  (%d pages: %s)"
          % (len(kinds), ", ".join(f"{k}={sum(1 for v in kinds.values() if v == k)}"
                                   for k in ("jump", "tramp", "byte", "code"))))


def test_no_rom_reads():
    """The ISA has no load-from-program-memory instruction.

    This is the reason the ROM is ~95% code: constants cannot be read out of
    ROM, so they have to be *executed* out of it as RETD/LBPX byte emitters.
    """
    mnems = {o.mnem for o in (decode(0, w) for w in range(4096)) if o}
    assert not (mnems & {"LDP", "LDR", "MOVP", "LDA"}), "unexpected ROM-read op"
    assert "RETD" in mnems and "LBPX" in mnems


if __name__ == "__main__":
    test_isa()
    test_no_rom_reads()
    print("isaread  ok  (no load-from-ROM instruction exists)")
    rom = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("TAMA_ROM", "rom/tama.b")
    if os.path.exists(rom):
        test_rom(rom)
        test_jump_tables(rom)
    else:
        print("rom      skipped (no ROM at %s)" % rom)
    print("all checks passed")
