"""Resolve the E0C6200's one computed branch, JPBA.

`JPBA` sets PCS from the register pair B:A while the bank:page still comes from
the NPC latch, so the analyzer already knows the page -- what it does not know
is which of that page's 256 steps are real entry points.

In practice the ROM only ever points JPBA at a table, and there are exactly
three shapes:

  jump     a leading run of `JP s`, or of `PSET p` + `JP s` pairs. Classic
           dispatch. The run length is the table length, so the target set is
           small and exact.

  byte     a leading run of `RETD l` / `LBPX MX, l`. This is not data that
           happens to disassemble -- it *is* how the core stores constants.
           `RETD l` writes l's two nibbles to M(X), M(X+1), bumps X by 2 and
           returns, so each slot is a one-word subroutine that hands the caller
           a byte. `LBPX` is the same store without the return, so a run of
           LBPX ending in a RETD emits a multi-byte string. Every slot in such
           a page is a valid, trivially liftable target.

  tramp    a leading run of 4-word `LD A,Mn; LD B,Mm; PSET p; JPBA` entries.
           A re-dispatcher: bank 1 of this ROM is reachable *only* through the
           eight-entry one at the top of page 0x10, one entry per bank-1 page.

  code     neither: the page is ordinary code and JPBA is dispatching to entry
           points scattered through it. Falls back to all 256 steps, which is
           sound but wide.
"""
import sys
from e0c6200 import decode, load_rom

JUMP_MNEMS = ("JP",)
BYTE_MNEMS = ("RETD", "LBPX")


def _jump_table(ops, page):
    """Length of the leading dispatch run, and its entry stride (1 or 2)."""
    # two-word form first: PSET p / JP s pairs
    n = 0
    while n < 128:
        a, b = ops[page + 2 * n], ops[page + 2 * n + 1]
        if a.kind == "pset" and b.kind == "jp":
            n += 1
        else:
            break
    if n:
        return n, 2
    n = 0
    while n < 256:
        o = ops[page + n]
        # a leading RET is a common "index 0 means do nothing" slot
        if o.kind == "jp" or (n == 0 and o.mnem == "RET"):
            n += 1
        else:
            break
    return (n, 1) if n else (0, 0)


def _byte_table(ops, page):
    """Slots in the page that are byte emitters, and the leading run length.

    A byte page is often two-level: a leading run of `RETD offset` entries, then
    an embedded JPBA, then the string bodies (`LBPX` runs closed by a `RETD`)
    that those offsets point at. Both levels are indexed by a JPBA, so every
    emitter slot in the page is a target -- not just the leading run.
    """
    lead = 0
    while lead < 256 and ops[page + lead].mnem in BYTE_MNEMS:
        lead += 1
    slots = [i for i in range(256) if ops[page + i].mnem in BYTE_MNEMS]
    return lead, slots


def _trampoline_table(ops, page):
    """Length of a leading run of 4-word `LD A,Mn; LD B,Mm; PSET p; JPBA` entries.

    Bank 1 of this ROM is reachable only through one of these, sitting at the
    top of page 0x10: eight entries, one per bank-1 page, each re-dispatching to
    a step held in RAM. Recognising the shape matters -- treating the page as
    256 loose targets lands the trace in the middle of a trampoline and invents
    control flow that then invents more pages.
    """
    n = 0
    while n < 64:
        b = page + 4 * n
        a, c, d, e = (ops[b + i] for i in range(4))
        if (a.mnem == "LD" and a.args[0] == "A" and a.args[1].startswith("M(")
                and c.mnem == "LD" and c.args[0] == "B" and c.args[1].startswith("M(")
                and d.kind == "pset" and e.kind == "jpba"):
            n += 1
        else:
            break
    return n


def classify(ops, page):
    """-> (kind, entries, stride) where entries is the list of target steps."""
    n = _trampoline_table(ops, page)
    if n >= 2:
        return "tramp", [i * 4 for i in range(n)], 4
    n, stride = _jump_table(ops, page)
    if n >= 2:
        return "jump", [i * stride for i in range(n)], stride
    lead, slots = _byte_table(ops, page)
    if lead >= 8 and len(slots) >= 128:
        return "byte", slots, 1
    return "code", list(range(256)), 1


def targets(ops, page):
    """Steps in `page` that a JPBA can plausibly land on.

    ponytail: for `jump` and `byte` pages this trusts the table's shape rather
    than proving the index range, which no analysis of this ROM can do. The
    fully sound answer is all 256 steps, but feeding that back into the trace
    invents control flow -- entering the middle of a 4-word trampoline, say --
    which then invents more JPBA pages until the whole ROM looks like code. If a
    future ROM breaks the assumption it shows up as an unreachable table entry,
    not as silently wrong output. Pages classed `code` still get all 256.
    """
    return classify(ops, page)[1]


def resolve(words, sites):
    """sites: {addr: {page}} from analyze.JPBA_SITES. -> {addr: (kind, targets)}"""
    ops = {a: decode(a, w) for a, w in enumerate(words)}
    out = {}
    for a, pages in sites.items():
        for p in sorted(pages):
            kind, steps, stride = classify(ops, p)
            out.setdefault(a, []).append((p, kind, [p | s for s in steps], stride))
    return out


def main(path):
    import analyze
    words = load_rom(path)
    analyze.analyze(words)
    res = resolve(words, analyze.JPBA_SITES)
    ops = {a: decode(a, w) for a, w in enumerate(words)}

    by_page = {}
    for a, lst in res.items():
        for p, kind, targets, stride in lst:
            by_page.setdefault(p, (kind, targets, stride, []))[3].append(a)

    total = narrowed = 0
    print(f"{'page':>6}  {'kind':<5} {'entries':>7} {'stride':>6}  used by")
    for p in sorted(by_page):
        kind, targets, stride, users = by_page[p]
        total += 256
        narrowed += len(targets)
        who = " ".join(f"0x{u:04X}" for u in sorted(users))
        print(f"0x{p:04X}  {kind:<5} {len(targets):>7} {stride:>6}  {who}")
    print()
    print(f"JPBA target slots: {narrowed} of {total} "
          f"({100 * narrowed / total:.0f}% of the naive fan-out)")

    print("\nresolved jump tables:")
    for p in sorted(by_page):
        kind, targets, stride, _ = by_page[p]
        if kind != "jump":
            continue
        print(f"  page 0x{p:04X}, {len(targets)} entries, {stride}-word:")
        for i, t in enumerate(targets):
            if stride == 2:
                dest = ((ops[t].word & 0x1F) << 8) | (ops[t + 1].word & 0xFF)
                print(f"    [{i:>3}] 0x{t:04X}  {ops[t]}; {ops[t + 1]}  -> 0x{dest:04X}")
            else:
                o = ops[t]
                dest = (p & 0x1F00) | (o.word & 0xFF)
                tail = f"-> 0x{dest:04X}" if o.kind == "jp" else ""
                print(f"    [{i:>3}] 0x{t:04X}  {o}  {tail}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
