"""Instruction cycle counts for the E0C6200, indexed by opcode.

Extracted mechanically from BrickEmuPy's E0C6200.py (CC0) rather than typed
out, because a hand-copied 4096-entry timing table is a guaranteed source of
silent drift. Stored as (first, last, cycles) runs -- the ISA is regular enough
that 19 of them cover all 4096 opcodes.

Regenerate with tools/gen_cycles.py.
"""

RUNS = (
    (0x000, 0x0FF, 5),
    (0x100, 0x1FF, 12),
    (0x200, 0x3FF, 5),
    (0x400, 0x5FF, 7),
    (0x600, 0x9FF, 5),
    (0xA00, 0xAFF, 7),
    (0xB00, 0xBFF, 5),
    (0xC00, 0xDFF, 7),
    (0xE00, 0xEFF, 5),
    (0xF00, 0xF1F, 7),
    (0xF20, 0xF27, 5),
    (0xF28, 0xF2F, 7),
    (0xF30, 0xF37, 5),
    (0xF38, 0xF7F, 7),
    (0xF80, 0xFDD, 5),
    (0xFDE, 0xFDE, 12),
    (0xFDF, 0xFDF, 7),
    (0xFE0, 0xFFE, 5),
    (0xFFF, 0xFFF, 7),
)

CYCLES = [0] * 4096
for _a, _b, _c in RUNS:
    for _i in range(_a, _b + 1):
        CYCLES[_i] = _c
