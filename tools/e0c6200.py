"""E0C6200 (Epson 4-bit) instruction decoder.

The core inside the E0C6S46 that runs the Tamagotchi P1. 12-bit instruction
words, 4-bit data. The entire ISA is 4096 opcodes wide with no prefixes and no
variable-length encoding, so a flat mask table decodes every word exactly once.

Opcode map cross-checked against BrickEmuPy's E0C6200dasm.py (CC0) and the
Epson E0C6200/E0C6200A core manual.
"""

# --- operand field helpers -------------------------------------------------
RQ = ("A", "B", "MX", "MY")          # the r/q register selector, 2 bits


class Op:
    """One decoded instruction word.

    kind drives control-flow analysis:
      seq   falls through to the next word
      pset  falls through, but sets the static page for the *next* jump/call
      jp    unconditional transfer          (target resolved by the analyzer)
      jpc   conditional transfer            (falls through too)
      call  subroutine call, returns to +1
      calz  subroutine call to page 0
      jpba  computed jump, step = B:A       (256 targets in a known page)
      ret   return (ret / rets / retd)
      halt  stops the core until an interrupt
    """

    __slots__ = ("addr", "word", "mnem", "args", "kind", "cond")

    def __init__(self, addr, word, mnem, args=(), kind="seq", cond=None):
        self.addr, self.word = addr, word
        self.mnem, self.args, self.kind, self.cond = mnem, args, kind, cond

    def __str__(self):
        a = ", ".join(self.args)
        return f"{self.mnem} {a}".strip()

    def __repr__(self):
        return f"<{self.addr:04X}: {self.word:03X}  {self}>"


def decode(addr, w):
    """Decode a 12-bit instruction word. Returns an Op, or None if illegal."""
    w &= 0xFFF
    hi = w >> 8
    lo = w & 0xFF
    n = w & 0xF
    r = (w >> 2) & 3
    q = w & 3

    # 0x0nn - 0x7nn : one instruction per top nibble, 8-bit immediate
    if hi == 0x0:
        return Op(addr, w, "JP", (f"0x{lo:02X}",), "jp")
    if hi == 0x1:
        return Op(addr, w, "RETD", (f"0x{lo:02X}",), "ret")
    if hi == 0x2:
        return Op(addr, w, "JP", ("C", f"0x{lo:02X}"), "jpc", "C")
    if hi == 0x3:
        return Op(addr, w, "JP", ("NC", f"0x{lo:02X}"), "jpc", "NC")
    if hi == 0x4:
        return Op(addr, w, "CALL", (f"0x{lo:02X}",), "call")
    if hi == 0x5:
        return Op(addr, w, "CALZ", (f"0x{lo:02X}",), "calz")
    if hi == 0x6:
        return Op(addr, w, "JP", ("Z", f"0x{lo:02X}"), "jpc", "Z")
    if hi == 0x7:
        return Op(addr, w, "JP", ("NZ", f"0x{lo:02X}"), "jpc", "NZ")
    if hi == 0x8:
        return Op(addr, w, "LD", ("Y", f"0x{lo:02X}"))
    if hi == 0x9:
        return Op(addr, w, "LBPX", ("MX", f"0x{lo:02X}"))

    # 0xAxx : index-register immediates, then the r/q ALU block
    if hi == 0xA:
        if w < 0xA80:
            op = ("ADC", "CP")[(w >> 6) & 1]
            reg = ("XH", "XL", "YH", "YL")[(w >> 4) & 3]
            return Op(addr, w, op, (reg, f"0x{n:X}"))
        sel = (w >> 4) & 7
        if sel == 7:
            if r != q:                      # RLC encodes r twice; anything else is illegal
                return None
            return Op(addr, w, "RLC", (RQ[r],))
        op = ("ADD", "ADC", "SUB", "SBC", "AND", "OR", "XOR")[sel]
        return Op(addr, w, op, (RQ[r], RQ[q]))

    if hi == 0xB:
        return Op(addr, w, "LD", ("X", f"0x{lo:02X}"))

    # 0xCxx/0xDxx : ALU with 4-bit immediate, r in bits 5:4
    if hi in (0xC, 0xD):
        op = ("ADD", "ADC", "AND", "OR", "XOR", "SBC", "FAN", "CP")[(w >> 6) & 7]
        return Op(addr, w, op, (RQ[(w >> 4) & 3], f"0x{n:X}"))

    if hi == 0xE:
        if w < 0xE40:
            return Op(addr, w, "LD", (RQ[(w >> 4) & 3], f"0x{n:X}"))
        if w < 0xE60:
            return Op(addr, w, "PSET", (f"0x{w & 0x1F:02X}",), "pset")
        if w < 0xE70:
            return Op(addr, w, "LDPX", ("MX", f"0x{n:X}"))
        if w < 0xE80:
            return Op(addr, w, "LDPY", ("MY", f"0x{n:X}"))
        if w < 0xEC0:                       # LD between r and X*/Y* parts
            grp, sel = (w >> 4) & 3, (w >> 2) & 3
            names = ("XP", "XH", "XL")
            if grp == 0:                    # E8x: LD X?,r  and RRC r
                if sel == 3:
                    return Op(addr, w, "RRC", (RQ[q],))
                return Op(addr, w, "LD", (names[sel], RQ[q]))
            if sel == 3:
                return None
            if grp == 1:
                return Op(addr, w, "LD", (("YP", "YH", "YL")[sel], RQ[q]))
            if grp == 2:
                return Op(addr, w, "LD", (RQ[q], names[sel]))
            return Op(addr, w, "LD", (RQ[q], ("YP", "YH", "YL")[sel]))
        if w < 0xED0:
            return Op(addr, w, "LD", (RQ[r], RQ[q]))
        if w < 0xEE0:
            return None
        if w < 0xEF0:
            return Op(addr, w, "LDPX", (RQ[r], RQ[q]))
        return Op(addr, w, "LDPY", (RQ[r], RQ[q]))

    # 0xFxx
    if w < 0xF10:
        return Op(addr, w, "CP", (RQ[r], RQ[q]))
    if w < 0xF20:
        return Op(addr, w, "FAN", (RQ[r], RQ[q]))
    if w < 0xF40:                           # ACPX/ACPY/SCPX/SCPY, only the 0x8-0xF half is legal
        if (w & 0x8) == 0:
            return None
        op = "ACP" if w < 0xF30 else "SCP"
        xy = "X" if (w & 0x4) == 0 else "Y"
        return Op(addr, w, op + xy, ("M" + xy, RQ[q]))
    if w < 0xF80:
        op = ("SET", "RST", "INC", "DEC")[(w >> 4) & 3]
        if op in ("SET", "RST"):
            return Op(addr, w, op, ("F", f"0x{n:X}"))
        return Op(addr, w, op, (f"M(0x{n:X})",))
    if w < 0xFC0:
        sel = (w >> 4) & 3
        mn = f"M(0x{n:X})"
        return Op(addr, w, "LD", (mn, "A") if sel == 0 else
                                (mn, "B") if sel == 1 else
                                ("A", mn) if sel == 2 else ("B", mn))
    if w < 0xFE0:                           # PUSH/POP block, plus RET/RETS
        push = w < 0xFD0
        k = w & 0xF
        if k < 4:
            return Op(addr, w, "PUSH" if push else "POP", (RQ[q],))
        if k < 0xB:
            reg = ("XP", "XH", "XL", "YP", "YH", "YL", "F")[k - 4]
            return Op(addr, w, "PUSH" if push else "POP", (reg,))
        if k == 0xB:
            return Op(addr, w, "DEC" if push else "INC", ("SP",))
        if push or k < 0xE:
            return None
        return Op(addr, w, "RETS" if k == 0xE else "RET", (), "ret")
    # FE0-FFF : SP transfers, JPBA, HALT, NOP
    spx = "SPH" if w < 0xFF0 else "SPL"
    k = w & 0xF
    if k < 4:
        return Op(addr, w, "LD", (spx, RQ[q]))
    if k < 8:
        return Op(addr, w, "LD", (RQ[q], spx))
    if w == 0xFE8:
        return Op(addr, w, "JPBA", (), "jpba")
    if w == 0xFF8:
        return Op(addr, w, "HALT", (), "halt")
    if w == 0xFFB:
        return Op(addr, w, "NOP5")
    if w == 0xFFF:
        return Op(addr, w, "NOP7")
    return None


def load_rom(path):
    """Read a .b ROM: big-endian 16-bit words holding 12-bit opcodes."""
    data = open(path, "rb").read()
    if len(data) % 2:
        raise ValueError(f"{path}: odd length {len(data)}")
    words = [(data[i] << 8) | data[i + 1] for i in range(0, len(data), 2)]
    bad = [i for i, w in enumerate(words) if w > 0xFFF]
    if bad:
        raise ValueError(f"{path}: {len(bad)} words exceed 12 bits "
                         f"(first at 0x{bad[0]:04X}) - wrong endianness?")
    return words
