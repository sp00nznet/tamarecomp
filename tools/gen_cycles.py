import re, collections
B = "C:/Users/nedch/AppData/Local/Temp/claude/G--recomp-mystery/79d1da60-57de-45c9-b22c-7331d831b88f/scratchpad/ref/BrickEmuPy-main/cores/"
src = open(B+"E0C6200.py", encoding='utf-8').read()
cyc = {}
for m in re.finditer(r'def (_\w+)\(self, opcode\):(.*?)(?=\n    def |\Z)', src, re.S):
    rets = re.findall(r'return (\d+)', m.group(2))
    if rets: cyc[m.group(1)] = int(rets[-1])
print('functions with cycle counts:', len(cyc), collections.Counter(cyc.values()).most_common())
d = open(B+"E0C6200dasm.py", encoding='utf-8').read()
tbl = d[d.index('_instruction_tbl = ('):]
tbl = tbl[:tbl.index('\n        )')]
order = []
for line in tbl.splitlines():
    m = re.search(r'E0C6200dasm\.(_\w+)\] \* (\d+)', line)
    if m: order += [m.group(1)] * int(m.group(2)); continue
    m = re.search(r'E0C6200dasm\.(_\w+),?\s*(?:#.*)?$', line.strip())
    if m: order.append(m.group(1))
print('table entries:', len(order))
assert len(order) == 4096, len(order)
print('missing:', sorted({n for n in order if n not in cyc and n != '_dummy'}))
tab = [cyc.get(n, 5) for n in order]
# The reference misnames SCPY's handler `_scpx_my_r`, so it has no cycle count
# of its own and would fall through to the 5-cycle default. It is a 7-cycle
# op like its SCPX twin.
for i in range(0xF3C, 0xF40):
    tab[i] = 7
runs=[]; s=0
for i in range(1,4097):
    if i==4096 or tab[i]!=tab[s]: runs.append((s,i-1,tab[s])); s=i
hdr = '"""Instruction cycle counts for the E0C6200, indexed by opcode.\n\nExtracted mechanically from BrickEmuPy\'s E0C6200.py (CC0) rather than typed\nout, because a hand-copied 4096-entry timing table is a guaranteed source of\nsilent drift. Stored as (first, last, cycles) runs -- the ISA is regular enough\nthat %d of them cover all 4096 opcodes.\n\nRegenerate with tools/gen_cycles.py.\n"""\n\nRUNS = (\n' % len(runs)
body = "".join("    (0x%03X, 0x%03X, %d),\n" % r for r in runs)
tail = ')\n\nCYCLES = [0] * 4096\nfor _a, _b, _c in RUNS:\n    for _i in range(_a, _b + 1):\n        CYCLES[_i] = _c\n'
open('cycles.py','w',encoding='utf-8').write(hdr+body+tail)
print('runs:', len(runs), '-> cycles.py')
