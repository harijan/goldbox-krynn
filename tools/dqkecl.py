#!/usr/bin/env python3
"""Write The Dark Queen of Krynn's ECL blocks (stdlib only): the test blocks
that tools/dosbox/checks/dqk_ecl.py runs in the original and in eclplay.

    from dqkecl import Block, Ref, assemble, imm, var
    b = Block()
    b.op("PRESS_KEY")
    b.op("ADD", imm(3), var(0x10), var(0x11))
    b.op("GOTO", Ref("end"))
    b.label("end")
    record = assemble(b)     # an ECL.GLB record, for hlib.replace

A record is the block as the game loads it (block_header 0, rules_dark_queen.c):
five vectors of four bytes each (GOTO's opcode, a WORD, the target), then the
code from 0x8014. The code starts with an EXIT, which vectors 0-3 go to; the
body follows it from Block's 'entry' label (0x8015), which vector 4 (the load
vector) goes to. Block.label(name) marks a place, Block.address(name) is its
address in the game, and Ref(name) is a WORD holding its address, for GOTO,
GOSUB and the ON targets.

Operands are read as src/ecl.c's cok_ecl_operands reads them (dqk 5f07:0f46):
a type byte, the low byte, and for the types 1-3 the high byte too. imm(n) is
type 0, one byte; word(n) type 2, two bytes (an immediate or an address); var(a)
type 1 and wvar(a) type 3, the variable at address a. Dark Queen's variables
are bytes (rules_dark_queen.c's variable_bytes): a narrow store keeps the low
byte, a wide one (wvar, word) puts the high byte in the next address too.
Opcodes are by their byte, as rules_dark_queen.c's dark_queen_opcodes has them.
"""

BASE = 0x8000
VECTORS = 5
CODE_START = BASE + VECTORS * 4  # the code's first address: the block's EXIT (0x8014)
CODE_LIMIT = 0x2EE3 - VECTORS * 4  # rules->ecl.code_size, less the vectors

OPCODES = {
    "EXIT": 0x00, "GOTO": 0x01, "GOSUB": 0x02, "COMPARE": 0x03, "ADD": 0x04,
    "SUBTRACT": 0x05, "DIVIDE": 0x06, "MULTIPLY": 0x07, "RANDOM": 0x08, "SAVE": 0x09,
    "RETURN": 0x13, "IF_EQUAL": 0x16, "IF_NOT_EQUAL": 0x17, "IF_LESS": 0x18,
    "IF_GREATER": 0x19, "IF_LESS_EQUAL": 0x1A, "IF_GREATER_EQUAL": 0x1B, "PRESS_KEY": 0x1D,
    "ON_GOTO": 0x25, "ON_GOSUB": 0x26, "GETTABLE": 0x2A, "AND": 0x2F, "OR": 0x30,
    "SAVE_TABLE": 0x35, "LOAD_CHARACTER": 0x0A, "INPUT_NUMBER": 0x0F, "INPUT_STRING": 0x10,
    "COMPARE_AND": 0x14, "VERTICAL_MENU": 0x15, "YES_NO": 0x22, "HORIZONTAL_MENU": 0x2B,
    "ASK_MENU": 0x31,
}


def imm(n):
    assert 0 <= n <= 0xFF, n
    return bytes([0, n])


def word(n):
    assert 0 <= n <= 0xFFFF, n
    return bytes([2, n & 0xFF, n >> 8])


def var(a):
    assert 0 <= a <= 0xFFFF, a
    return bytes([1, a & 0xFF, a >> 8])


def wvar(a):
    assert 0 <= a <= 0xFFFF, a
    return bytes([3, a & 0xFF, a >> 8])


def text(s):
    """A string operand (type 0x80, its low byte the packed byte count): the
    6-bit packing of ecl.c's unpack, four codes in three bytes. Upper case
    letters, digits and punctuation only (a lower-case letter has no code)."""
    codes = []
    for ch in s:
        c = ord(ch)
        assert 0x20 <= c <= 0x5F and c != 0x40, ch
        codes.append(c - 0x40 if c >= 0x40 else c)
    codes += [0] * (-len(codes) % 4)
    packed = bytearray()
    for i in range(0, len(codes), 4):
        c0, c1, c2, c3 = codes[i:i + 4]
        packed += bytes([c0 << 2 | c1 >> 4, (c1 & 15) << 4 | c2 >> 2, (c2 & 3) << 6 | c3])
    assert len(packed) <= 0xFF, s
    return bytes([0x80, len(packed)]) + bytes(packed)


class Ref:
    """A label's address as a WORD operand (three bytes), set by assemble."""

    def __init__(self, name):
        self.name = name


class Block:
    """A block's code: the EXIT, then the body from label 'entry'."""

    def __init__(self):
        self.parts = [bytes([OPCODES["EXIT"]])]
        self.size = 1
        self.labels = {"entry": 1}

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.size

    def address(self, name):
        """A label's address in the game (CODE_START plus its offset)."""
        return CODE_START + self.labels[name]

    def op(self, name, *operands):
        """One instruction: its opcode, then its operands (bytes, or Ref)."""
        self._add(bytes([OPCODES[name]]))
        for o in operands:
            self._add(o)

    def _add(self, part):
        self.parts.append(part)
        self.size += 3 if isinstance(part, Ref) else len(part)


def assemble(block):
    """The block's record: the five vectors, then the code."""
    if block.size > CODE_LIMIT:
        raise ValueError(f"the block is {block.size} bytes; the limit is {CODE_LIMIT}")
    entry = block.labels["entry"]
    vectors = b"".join(
        bytes([OPCODES["GOTO"]]) + word(CODE_START + (entry if i == VECTORS - 1 else 0))
        for i in range(VECTORS))
    code = b"".join(word(CODE_START + block.labels[p.name]) if isinstance(p, Ref) else p
                    for p in block.parts)
    return vectors + code
