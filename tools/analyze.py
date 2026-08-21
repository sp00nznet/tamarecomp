"""Static control-flow analysis of an E0C6S46 ROM.

The whole point of the project rests on one question: can every instruction in
the ROM be given a statically-known successor set? On this core it can, because
the only page-changing mechanism is PSET, which loads a *constant* into the NPC
(new bank:page) latch. So we trace reachability while propagating NPC as a
single abstract value: either a known 5-bit bank:page, or the address's own page
(what any non-PSET instruction leaves behind).

Result: every JP/CALL target is a constant. JPBA is the sole computed branch,
and even that is bounded to the 256 steps of one statically-known page.
"""
import sys
from collections import defaultdict
from e0c6200 import decode, load_rom

# E0C6S46 vector table: reset plus six interrupt sources, two words apart.
VECTORS = [
    (0x0100, "reset"),
    (0x0102, "clock_timer"),
    (0x0104, "stopwatch"),
    (0x0106, "prog_timer"),
    (0x0108, "serial"),
    (0x010A, "input_k0"),
    (0x010C, "input_k1"),
]


JPBA_SITES = {}     # addr -> set of statically-known target pages


def successors(op, npc):
    """Statically-known successors of `op` given the NPC latch value.

    Returns (list_of_(addr, kind), new_npc_for_fallthrough).
    `npc` is a 13-bit bank:page:00 value.
    """
    a = op.addr
    nxt = (a & 0x1000) | ((a + 1) & 0xFFF)   # PC+1 wraps inside the bank
    page = npc & 0x1F00
    s = op.word & 0xFF

    if op.kind == "pset":
        return [(nxt, "seq")], (op.word & 0x1F) << 8
    if op.kind == "jp":
        return [(page | s, "jp")], nxt & 0x1F00
    if op.kind == "jpc":
        return [(page | s, "jp"), (nxt, "seq")], nxt & 0x1F00
    if op.kind == "call":
        return [(page | s, "call"), (nxt, "seq")], nxt & 0x1F00
    if op.kind == "calz":
        return [((npc & 0x1000) | s, "call"), (nxt, "seq")], nxt & 0x1F00
    if op.kind == "jpba":
        # The one computed branch on this core. The page is still a constant --
        # only the 8-bit step comes from B:A -- so it is a jump table into one
        # known page. Fanning out all 256 slots would mark most of the ROM as
        # code (sprite data lives in those pages too), so the trace records the
        # site and stops; JUMP_TABLES resolves them properly.
        JPBA_SITES.setdefault(a, set()).add(page)
        return [], nxt & 0x1F00
    if op.kind in ("ret", "halt"):
        # HALT resumes at PC+1 once an interrupt returns; RET goes to the caller,
        # which the call edges already cover.
        return ([(nxt, "seq")] if op.kind == "halt" else []), nxt & 0x1F00
    return [(nxt, "seq")], nxt & 0x1F00


def analyze(words):
    JPBA_SITES.clear()
    ops = {}
    illegal = []
    for a, w in enumerate(words):
        op = decode(a, w)
        if op is None:
            illegal.append(a)
        else:
            ops[a] = op

    reached = {}                 # addr -> set of npc values seen on entry
    entries = set()              # call targets and vectors: subroutine heads
    edges = defaultdict(set)
    work = []
    for va, _ in VECTORS:
        entries.add(va)
        work.append((va, va & 0x1F00))

    unresolved = []
    while work:
        a, npc = work.pop()
        if a >= len(words):
            unresolved.append((a, "out of ROM"))
            continue
        seen = reached.setdefault(a, set())
        if npc in seen:
            continue
        seen.add(npc)
        op = ops.get(a)
        if op is None:
            unresolved.append((a, "illegal opcode"))
            continue
        succs, fall_npc = successors(op, npc)
        for t, kind in succs:
            edges[a].add((t, kind))
            if kind == "call":
                entries.add(t)
                work.append((t, t & 0x1F00))
            elif kind == "seq":
                work.append((t, fall_npc if op.kind == "pset" else fall_npc))
            else:
                work.append((t, t & 0x1F00))
        if op.kind == "pset":
            # the fall-through carries the *constant* page, not the address's
            work[-1] = (succs[0][0], fall_npc)
    return ops, illegal, reached, entries, edges, unresolved


def main(path):
    words = load_rom(path)
    ops, illegal, reached, entries, edges, unresolved = analyze(words)
    n = len(words)
    reach = len(reached)

    hist = defaultdict(int)
    for a in sorted(reached):
        hist[ops[a].mnem] += 1
    kinds = defaultdict(int)
    for a in sorted(reached):
        kinds[ops[a].kind] += 1

    print(f"ROM             {path}")
    print(f"words           {n} (0x{n:04X}), {n * 12 // 8} bytes of instruction space")
    print(f"decoded         {len(ops)}  ({100 * len(ops) / n:.1f}%)")
    print(f"illegal words   {len(illegal)}")
    print(f"entry points    {len(entries)} (7 vectors + {len(entries) - 7} call targets)")
    print(f"unresolved      {len(unresolved)}")
    print()
    pages = set()
    for v in JPBA_SITES.values():
        pages |= v
    tbl = set()
    for p in pages:
        tbl |= set(range(p, p + 256))
    upper = set(reached) | tbl
    print(f"JPBA sites      {len(JPBA_SITES)}, each with exactly "
          f"{max(len(v) for v in JPBA_SITES.values()) if JPBA_SITES else 0} target page")
    for a in sorted(JPBA_SITES):
        pgs = ", ".join(f"0x{p:04X}-0x{p | 0xFF:04X}" for p in sorted(JPBA_SITES[a]))
        print(f"  0x{a:04X} -> {pgs}")
    print()
    print(f"proven code     {len(reached):5}  {100 * len(reached) / n:.1f}%")
    print(f"+ JPBA pages    {len(tbl):5}  {100 * len(tbl) / n:.1f}%  ({len(pages)} pages)")
    print(f"code upper bnd  {len(upper):5}  {100 * len(upper) / n:.1f}%")
    print(f"provably data   {n - len(upper):5}  {100 * (n - len(upper)) / n:.1f}%")
    print()
    print("control flow (reachable):")
    for k in ("jp", "jpc", "call", "calz", "jpba", "ret", "halt", "pset"):
        if kinds[k]:
            print(f"  {k:6} {kinds[k]:5}")
    print()
    print("top opcodes:")
    for m, c in sorted(hist.items(), key=lambda kv: -kv[1])[:12]:
        print(f"  {m:6} {c:5}")
    if unresolved:
        print("\nUNRESOLVED:")
        for a, why in unresolved[:20]:
            print(f"  0x{a:04X}  {why}")
    return 0 if not unresolved else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
