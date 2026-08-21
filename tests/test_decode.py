"""Self-checks for the E0C6200 decoder and the ROM analysis.

Run:  python tests/test_decode.py [path/to/tama.b]
The ROM is optional -- the ISA checks run without it.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from e0c6200 import decode, load_rom          # noqa: E402
import analyze                                # noqa: E402

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

    assert len(reached) > 1900, f"only {len(reached)} words proven reachable"
    print("rom      ok  (%d words, %d proven code, %d JPBA sites, 0 unresolved)"
          % (len(words), len(reached), len(analyze.JPBA_SITES)))


if __name__ == "__main__":
    test_isa()
    rom = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("TAMA_ROM", "rom/tama.b")
    if os.path.exists(rom):
        test_rom(rom)
    else:
        print("rom      skipped (no ROM at %s)" % rom)
    print("all checks passed")
