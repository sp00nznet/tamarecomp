"""Translate an E0C6S46 ROM into C.

One C function, one label per instruction word, straight-line C per instruction.
Sequential instructions fall through with no branch at all; direct jumps become
`goto`. That is the whole point -- 95% of this ROM's control flow costs nothing
at runtime because the compiler sees it as ordinary structured code.

Two things are not statically known and go through a shared dispatch:

  RET/RETS/RETD  the return address is popped out of RAM, where the program is
                 free to have altered it. We keep the hardware stack faithfully
                 (three nibbles at SP) rather than mapping CALL/RET onto the C
                 call stack, which would need a proof of stack discipline the
                 ROM does not offer.

  JPBA           the step comes from B:A.

Both set `pc` and jump to a dense switch covering *every* word in the ROM, so a
computed transfer can never land somewhere without code. That is what makes the
output sound without the jump-table analysis having to be right --
tools/jumptables.py explains what the tables are, but the emitter does not
depend on it.
"""
import sys
from e0c6200 import decode, load_rom
from cycles import CYCLES

# register selector -> (read expression, write template)
REG = {
    "A":  ("t->a", "t->a = %s;"),
    "B":  ("t->b", "t->b = %s;"),
    "MX": ("MR(t->x)", "MW(t->x, %s);"),
    "MY": ("MR(t->y)", "MW(t->y, %s);"),
    "XP": ("(t->x >> 8)", "t->x = (t->x & 0x0FF) | ((%s) << 8);"),
    "XH": ("((t->x >> 4) & 0xF)", "t->x = (t->x & 0xF0F) | ((%s) << 4);"),
    "XL": ("(t->x & 0xF)", "t->x = (t->x & 0xFF0) | (%s);"),
    "YP": ("(t->y >> 8)", "t->y = (t->y & 0x0FF) | ((%s) << 8);"),
    "YH": ("((t->y >> 4) & 0xF)", "t->y = (t->y & 0xF0F) | ((%s) << 4);"),
    "YL": ("(t->y & 0xF)", "t->y = (t->y & 0xFF0) | (%s);"),
    "SPH": ("(t->sp >> 4)", "t->sp = (t->sp & 0x0F) | ((%s) << 4);"),
    "SPL": ("(t->sp & 0xF)", "t->sp = (t->sp & 0xF0) | (%s);"),
}


def rd(name):
    if name.startswith("M(0x"):
        return "MR(0x%s)" % name[4:-1]
    if name.startswith("0x"):
        return name
    return REG[name][0]


def wr(name, expr):
    if name.startswith("M(0x"):
        return "MW(0x%s, %s);" % (name[4:-1], expr)
    return REG[name][1] % expr


def alu(mnem, d, s):
    """ALU body writing back to `d`. `s` is an already-rendered expression."""
    g = rd(d)
    if mnem in ("ADD", "ADC"):
        c = " + t->cf" if mnem == "ADC" else ""
        return ["r = %s + %s%s;" % (g, s, c),
                "t->cf = r > 15;",
                "if (t->df && r > 9) { r += 6; t->cf = 1; }",
                "t->zf = (r & 0xF) == 0;",
                wr(d, "r & 0xF")]
    if mnem in ("SUB", "SBC"):
        c = " - t->cf" if mnem == "SBC" else ""
        return ["r = %s - (%s)%s;" % (g, s, c),
                "t->cf = r < 0;",
                "if (t->df && t->cf) r += 10;",
                "t->zf = (r & 0xF) == 0;",
                wr(d, "r & 0xF")]
    if mnem in ("AND", "OR", "XOR"):
        op = {"AND": "&", "OR": "|", "XOR": "^"}[mnem]
        return ["r = %s %s (%s);" % (g, op, s), "t->zf = r == 0;", wr(d, "r")]
    if mnem == "CP":
        return ["r = %s - (%s);" % (g, s), "t->zf = r == 0;", "t->cf = r < 0;"]
    if mnem == "FAN":
        return ["t->zf = (%s & (%s)) == 0;" % (g, s)]
    raise AssertionError(mnem)


def body(op):
    """C statements for one instruction: no label, no cycles, no control flow."""
    m, args = op.mnem, op.args

    if m in ("ADD", "ADC", "SUB", "SBC", "AND", "OR", "XOR", "CP", "FAN"):
        # ADC/CP against XH/XL/YH/YL set Z before C and never mask the result,
        # so they are their own case rather than a variant of the r/q form.
        if args[0] in ("XH", "XL", "YH", "YL"):
            g = REG[args[0]][0]
            if m == "ADC":
                return ["r = %s + %s + t->cf;" % (g, rd(args[1])),
                        "t->zf = (r & 0xF) == 0;",
                        "t->cf = r > 15;",
                        REG[args[0]][1] % "r & 0xF"]
            return ["r = %s - %s;" % (g, rd(args[1])),
                    "t->zf = r == 0;", "t->cf = r < 0;"]
        return alu(m, args[0], rd(args[1]))

    if m == "LD":
        d, s = args
        if d == "X":
            return ["t->x = (t->x & 0xF00) | %s;" % s]
        if d == "Y":
            return ["t->y = (t->y & 0xF00) | %s;" % s]
        return [wr(d, rd(s))]

    if m in ("LDPX", "LDPY"):
        bump = ("t->x = (t->x & 0xF00) | ((t->x + 1) & 0xFF);" if m == "LDPX"
                else "t->y = (t->y & 0xF00) | ((t->y + 1) & 0xFF);")
        return [wr(args[0], rd(args[1])), bump]

    if m == "LBPX":
        v = int(args[1], 16)
        return ["MW(t->x, 0x%X);" % (v & 0xF),
                "MW((t->x & 0xF00) | ((t->x + 1) & 0xFF), 0x%X);" % (v >> 4),
                "t->x = (t->x & 0xF00) | ((t->x + 2) & 0xFF);"]

    if m in ("ACPX", "ACPY", "SCPX", "SCPY"):
        p = "x" if m[-1] == "X" else "y"
        g, s = "MR(t->%s)" % p, rd(args[1])
        if m[0] == "A":
            pre = ["r = %s + %s + t->cf;" % (g, s), "t->cf = r > 15;",
                   "if (t->df && r > 9) { r += 6; t->cf = 1; }"]
        else:
            pre = ["r = %s - %s - t->cf;" % (g, s), "t->cf = r < 0;",
                   "if (t->df && t->cf) r += 10;"]
        return pre + ["t->zf = (r & 0xF) == 0;",
                      "MW(t->%s, r & 0xF);" % p,
                      "t->%s = (t->%s & 0xF00) | ((t->%s + 1) & 0xFF);" % (p, p, p)]

    if m in ("INC", "DEC"):
        if args[0] == "SP":
            return ["t->sp = (t->sp %s 1) & 0xFF;" % ("+" if m == "INC" else "-")]
        n = args[0][4:-1]
        return ["r = MR(0x%s) %s 1;" % (n, "+" if m == "INC" else "-"),
                "t->zf = (r & 0xF) == 0;",
                "t->cf = %s;" % ("r > 15" if m == "INC" else "r < 0"),
                "MW(0x%s, r & 0xF);" % n]

    if m == "RLC":
        g = rd(args[0])
        return ["r = (%s << 1) + t->cf;" % g, "t->cf = r > 15;",
                wr(args[0], "r & 0xF")]
    if m == "RRC":
        g = rd(args[0])
        return ["r = %s + (t->cf << 4);" % g, "t->cf = r & 1;",
                wr(args[0], "r >> 1")]

    if m in ("SET", "RST"):
        i = int(args[1], 16)
        out = []
        for bit, fl in ((1, "cf"), (2, "zf"), (4, "df")):
            if i & bit:
                out.append("t->%s = %d;" % (fl, 1 if m == "SET" else 0))
        if i & 8:
            if m == "SET":
                out += ["t->if_delay = !t->iff;", "t->iff = 1;"]
            else:
                out.append("t->iff = 0;")
        return out or ["/* no flag bits selected */"]

    if m == "PUSH":
        v = "PACK_F(t)" if args[0] == "F" else rd(args[0])
        return ["t->sp = (t->sp - 1) & 0xFF;", "MW(t->sp, %s);" % v]
    if m == "POP":
        if args[0] == "F":
            return ["UNPACK_F(t, MR(t->sp));", "t->sp = (t->sp + 1) & 0xFF;"]
        return [wr(args[0], "MR(t->sp)"), "t->sp = (t->sp + 1) & 0xFF;"]

    if m == "PSET":
        # Nothing survives: the page it loads is resolved at build time, and
        # its one-instruction interrupt hold-off is redundant here. That
        # hold-off exists to stop an interrupt landing between a PSET and the
        # jump that consumes the page it latched -- and generated code has no
        # interrupt point there, because every reachable PSET in this ROM is
        # immediately followed by a control transfer (asserted in the tests)
        # and only a *taken* transfer consumes the latch. `SET F, I` still
        # raises if_delay; that one is real.
        return []

    if m in ("NOP5", "NOP7"):
        return []

    raise AssertionError("no emitter for %s at 0x%04X" % (m, op.addr))


PUSH_PC = [
    "MW((t->sp - 1) & 0xFF, (%(ret)s >> 8) & 0xF);",
    "MW((t->sp - 2) & 0xFF, (%(ret)s >> 4) & 0xF);",
    "t->sp = (t->sp - 3) & 0xFF;",
    "MW(t->sp, %(ret)s & 0xF);",
]

# The popped return address carries only the 12-bit page:step; the bank bit
# comes from the PC the RET is executing at. Generated code has no live PC
# between dispatch points, so the bank is baked in from the RET's own address.
POP_PC = [
    "t->pc = 0x%(bank)04X | MR(t->sp) | (MR((t->sp + 1) & 0xFF) << 4)"
    " | (MR((t->sp + 2) & 0xFF) << 8);",
    "t->sp = (t->sp + 3) & 0xFF;",
]

HEADER = '''/* Generated by tools/emit.py -- do not edit.
 *
 * The Tamagotchi P1 ROM as C. One label per instruction word; sequential
 * instructions fall through, direct jumps are `goto`, and only RET and JPBA
 * reach the dispatch switch at the bottom.
 */
#include "tamarecomp/e0c6200.h"

#define MR(a)          tama_mem_read(t, (uint16_t)(a))
#define MW(a, v)       tama_mem_write(t, (uint16_t)(a), (uint8_t)(v))
#define PACK_F(t)      ((t)->cf | ((t)->zf << 1) | ((t)->df << 2) | ((t)->iff << 3))
#define UNPACK_F(t, v) do { (t)->cf = (v) & 1; (t)->zf = ((v) >> 1) & 1;   \\
                            (t)->df = ((v) >> 2) & 1; (t)->iff = ((v) >> 3) & 1; \\
                       } while (0)

/* Tested only where a straight run is ending anyway, so the common path stays
 * a plain fallthrough with no branch at all. */
#define CHECK_BUDGET(next) \\
    do { if (t->cycles >= budget) { t->pc = (next); return; } } while (0)

void tama_run(tama_t *t, uint64_t budget)
{
    int r;
    (void)r;
    goto dispatch;

'''

FOOTER = '''    default:
        t->trapped = 1;
        return;
    }
}
'''


def emit(words, out):
    ops = {a: decode(a, w) for a, w in enumerate(words)}

    def page_at(a):
        """The NPC page in force at `a`: from a preceding PSET, else the address's."""
        p = ops.get(a - 1)
        return ((p.word & 0x1F) << 8) if (p and p.kind == "pset") else (a & 0x1F00)

    w = out.write
    w(HEADER)
    for a in range(len(words)):
        op = ops[a]
        nxt = (a & 0x1000) | ((a + 1) & 0xFFF)   # PC+1 wraps inside the bank
        w("L_%04X: /* %03X  %s */\n" % (a, op.word, op))
        w("    t->cycles += %d;\n" % CYCLES[op.word])

        if op.kind == "pset":
            continue                       # falls through to the next label

        if op.kind in ("jp", "jpc"):
            tgt = page_at(a) | (op.word & 0xFF)
            cond = {"C": "t->cf", "NC": "!t->cf",
                    "Z": "t->zf", "NZ": "!t->zf"}.get(op.cond)
            if cond:
                w("    if (%s) goto L_%04X;\n" % (cond, tgt))
                w("    CHECK_BUDGET(0x%04X);\n" % nxt)
            else:
                w("    goto L_%04X;\n" % tgt)
            continue

        if op.kind in ("call", "calz"):
            base = page_at(a) if op.kind == "call" else (page_at(a) & 0x1000)
            for line in PUSH_PC:
                w("    " + line % {"ret": "0x%04X" % nxt} + "\n")
            w("    goto L_%04X;\n" % (base | (op.word & 0xFF)))
            continue

        if op.kind == "jpba":
            w("    t->pc = 0x%04X | (t->b << 4) | t->a;\n" % page_at(a))
            w("    goto dispatch;\n")
            continue

        if op.kind == "ret":
            if op.mnem == "RETD":
                v = op.word & 0xFF
                w("    MW(t->x, 0x%X);\n" % (v & 0xF))
                w("    MW((t->x & 0xF00) | ((t->x + 1) & 0xFF), 0x%X);\n" % (v >> 4))
                w("    t->x = (t->x & 0xF00) | ((t->x + 2) & 0xFF);\n")
            for line in POP_PC:
                w("    " + line % {"bank": a & 0x1000} + "\n")
            if op.mnem == "RETS":
                w("    t->pc = (t->pc & 0x1000) | ((t->pc + 1) & 0xFFF);\n")
            w("    goto dispatch;\n")
            continue

        if op.kind == "halt":
            w("    t->halted = 1;\n")
            w("    t->pc = 0x%04X;\n" % nxt)
            w("    return;\n")
            continue

        for line in body(op):
            w("    " + line + "\n")
        if nxt != a + 1:                       # bank wrap: not the next label
            w("    goto L_%04X;\n" % nxt)
        else:
            w("    CHECK_BUDGET(0x%04X);\n" % nxt)

    w("\ndispatch:\n    switch (t->pc) {\n")
    for a in range(len(words)):
        w("    case 0x%04X: goto L_%04X;\n" % (a, a))
    w(FOOTER)


def main(rom, dest):
    words = load_rom(rom)
    with open(dest, "w", encoding="utf-8") as f:
        emit(words, f)
    with open(dest, encoding="utf-8") as f:
        n = sum(1 for _ in f)
    print("%s: %d lines from %d ROM words" % (dest, n, len(words)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1],
                  sys.argv[2] if len(sys.argv) > 2 else "generated/tama_rom.c"))
