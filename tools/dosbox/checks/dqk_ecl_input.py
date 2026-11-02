#!/usr/bin/env python3
"""The Dark Queen of Krynn's input and menu opcodes run in the original and in
eclplay (DARK-QUEEN.md's step 3c: dqk_ecl_input; docs/checks.md, "Checks
against the original"): test blocks (tools/dqkecl.py) that replace block 1 of a
copy of DISK3\\ECL.GLB, as dqk_ecl does, with the keys typed after the block's
PRESS KEY (the flow's keys: the box types them as the game waits, eclplay
--keys reads them). The machinery is dqk_ecl's (run_block, port, original).

The blocks, by opcode (the handlers are dqk_ecl's table, dark_queen_opcodes):
INPUT NUMBER (0x0f, 6c70:2e7c), INPUT STRING (0x10, 6c70:2ec8), COMPARE AND
(0x14, 5f07:05be), VERTICAL MENU (0x15, 6c70:1e91), HORIZONTAL MENU (0x2b,
6c70:1ff7), ASK MENU (0x31, 6c70:1ff7 with 1), YES NO (0x22, 5f07:13c4). Each
checks the cases its code shows: an empty line, the digit limit, a suffix base,
a negative or too big number asked again, Escape, Backspace, a string kept
whole at its limit, a string pair never equal, a wide variable's high byte kept
by a byte store, Escape and the arrows at a menu, a menu's choice stored in its
header's variable, a hotkey picking the first item with that letter. Checked
as dqk_ecl checks its blocks: the original waits in the block's PRESS KEY, its
ECL stops at the block's end, the variables 0x100-0x17f and 0x29 agree, and the
values each block computes are the expected ones.

Not run here: LOAD CHARACTER (0x0a, 5f07:0422). Its observable is which member
is selected, which needs the same party on both sides (eclplay --party or --load
of the box's saved game); this flow has no party. Screens are not compared: the
menus' and input lines' screens are drawn by dqk_menus' and dqk_views' flows.

    python3 tools/dosbox/checks/dqk_ecl_input.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import string
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_lib  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, OPCODES, assemble, imm, text, var, wvar, word  # noqa: E402

# Each block: (Block, expect, the keys after its PRESS KEY). The keys are
# eclplay's escapes: \r Return, \e Escape, \b Backspace, \^ \v the arrows.
BLOCKS = {}


def block(name):
    def add(make):
        BLOCKS[name] = make
        return make
    return add


def start():
    b = Block()
    b.op("PRESS_KEY")
    dqk_ecl.prelude(b)
    return b


@block("input_number")
def input_number():
    """INPUT NUMBER: 42; an empty line gives 0; 1234567 takes five digits
    (12345, the wide store's two bytes); "-5" is negative, asked again, and
    Escape asks again, then "7x" is 7 (x ends the number); "1FH" is 31 (hex);
    40000 is out of range, asked again, then 9."""
    b, expect = start(), {}
    b.op("SAVE", imm(0x55), var(0x101))
    b.op("SAVE", imm(0x55), var(0x104))
    b.op("INPUT_NUMBER", imm(0), var(0x100))
    b.op("INPUT_NUMBER", imm(0), var(0x101))
    b.op("INPUT_NUMBER", imm(0), wvar(0x102))
    b.op("INPUT_NUMBER", imm(0), var(0x104))
    b.op("INPUT_NUMBER", imm(0), var(0x106))
    b.op("INPUT_NUMBER", imm(0), var(0x107))
    b.op("EXIT")
    expect.update({0x100: 42, 0x101: 0, 0x102: 0x39, 0x103: 0x30, 0x104: 7, 0x106: 31,
                   0x107: 9})
    keys = "42\\r\\r1234567\\r-5\\r\\e7x\\r1FH\\r40000\\r9\\r"
    return b, expect, keys


@block("input_string")
def input_string():
    """INPUT STRING: "ab c" is "AB C"; an empty line is " " (one space, its
    terminator stored too); Escape ends it, keeping what was typed; 40 letters
    keep the first 35 (the limit: 35 characters at most, dqk_ecl's read
    line); Backspace takes a letter back."""
    b, expect = start(), {}
    b.op("SAVE", imm(0x55), var(0x118))
    b.op("SAVE", imm(0x55), var(0x119))
    b.op("INPUT_STRING", imm(0), var(0x110))
    b.op("INPUT_STRING", imm(0), var(0x118))
    b.op("INPUT_STRING", imm(0), var(0x11A))
    b.op("INPUT_STRING", imm(0), var(0x120))
    b.op("INPUT_STRING", imm(0), var(0x150))
    b.op("EXIT")
    letters = (string.ascii_uppercase * 2)[:40]
    expect.update({0x110: 0x41, 0x111: 0x42, 0x112: 0x20, 0x113: 0x43, 0x114: 0,
                   0x118: 0x20, 0x119: 0, 0x11A: 0x58, 0x11B: 0x59, 0x11C: 0,
                   0x150: 0x41, 0x151: 0x43, 0x152: 0})
    for i, c in enumerate(letters[:35]):
        expect[0x120 + i] = ord(c)
    expect[0x120 + 35] = 0
    keys = "ab c\\r\\r" + "XY\\e" + letters + "\\r" + "AB\\bC\\r"
    return b, expect, keys


@block("compare_and")
def compare_and():
    """COMPARE AND: two pairs, both equal sets flag 0 (IF_EQUAL); a pair of
    strings is never equal (5f07:05be's addresses), nor a string second pair;
    a wide variable against a word is equal by value. The last block (a number
    against a string) has no expectation: its outcome is the original's, and
    eclplay must agree with it."""
    b, expect = start(), {}
    b.op("SAVE", word(0x1234), wvar(0x120))

    def then(if_, at):
        """The next instruction runs if IF_ holds: SAVE 1 to AT."""
        b.op(if_)
        b.op("SAVE", imm(1), var(at))

    b.op("COMPARE_AND", imm(1), imm(1), imm(2), imm(2))
    then("IF_EQUAL", 0x110)
    b.op("COMPARE_AND", imm(1), imm(2), imm(2), imm(2))
    then("IF_NOT_EQUAL", 0x111)
    then("IF_EQUAL", 0x112)
    b.op("COMPARE_AND", imm(1), imm(1), imm(3), imm(4))
    then("IF_NOT_EQUAL", 0x113)
    b.op("COMPARE_AND", text("DURFEY"), text("DURFEY"), imm(1), imm(1))
    then("IF_NOT_EQUAL", 0x114)
    b.op("COMPARE_AND", imm(1), imm(1), text("DURFEY"), text("DURFEY"))
    then("IF_NOT_EQUAL", 0x115)
    b.op("COMPARE_AND", wvar(0x120), word(0x1234), imm(0), imm(0))
    then("IF_EQUAL", 0x116)
    b.op("COMPARE_AND", imm(1), text("A"), imm(1), imm(1))
    then("IF_EQUAL", 0x117)
    then("IF_NOT_EQUAL", 0x118)
    b.op("EXIT")
    expect.update({0x110: 1, 0x111: 1, 0x112: 0, 0x113: 1, 0x114: 1, 0x115: 1, 0x116: 1})
    return b, expect, ""


@block("vertical_menu")
def vertical_menu():
    """VERTICAL MENU: Down then Return picks "TWO" (1); the store takes the
    item's type (a string, 0x80, which ecl.c's wide_type makes a word store),
    so the byte after a wide header variable is cleared too (0x101 0, the
    original's store by 6d2 the same); Down, Down, Escape keeps the current
    item (2); Up from the first wraps to the last (2)."""
    b, expect = start(), {}
    b.op("SAVE", word(0x1234), wvar(0x100))
    b.op("SAVE", imm(0x55), var(0x102))
    b.op("SAVE", imm(0x55), var(0x104))

    def menu(at):
        b.op("VERTICAL_MENU", at, text("PICK"), imm(3), text("ONE"), text("TWO"), text("THREE"))

    menu(wvar(0x100))
    menu(var(0x102))
    menu(var(0x104))
    b.op("EXIT")
    expect.update({0x100: 1, 0x101: 0, 0x102: 2, 0x104: 2})
    keys = "\\v\\r" + "\\v\\v\\e" + "\\^\\r"
    return b, expect, keys


@block("horizontal_menu")
def horizontal_menu():
    """HORIZONTAL MENU: "n" picks NO (1); "g" picks GO, the first item with
    that letter, not GIVE (0); Escape and Down do nothing, "l" picks LOOK (1);
    "x" is no hotkey, asked again, "n" is NO. The header's wide variable takes
    the choice as a word (0x104 1, 0x105 0)."""
    b, expect = start(), {}
    b.op("SAVE", imm(5), var(0x101))
    b.op("SAVE", imm(0x55), var(0x104))
    b.op("SAVE", imm(0x66), var(0x105))
    b.op("HORIZONTAL_MENU", var(0x100), imm(2), text("YES"), text("NO"))
    b.op("HORIZONTAL_MENU", var(0x101), imm(2), text("GO"), text("GIVE"))
    b.op("HORIZONTAL_MENU", var(0x102), imm(3), text("TALK"), text("LOOK"), text("NO"))
    b.op("HORIZONTAL_MENU", wvar(0x104), imm(2), text("YES"), text("NO"))
    b.op("EXIT")
    expect.update({0x100: 1, 0x101: 0, 0x102: 1, 0x104: 1, 0x105: 0})
    keys = "n" + "g" + "\\e\\vl" + "xn"
    return b, expect, keys


@block("ask_menu")
def ask_menu():
    """ASK MENU: "what do you do?" first, then the bar: "s" picks STAY (1)."""
    b, expect = start(), {}
    b.op("ASK_MENU", var(0x106), imm(2), text("GO"), text("STAY"))
    b.op("EXIT")
    expect.update({0x106: 1})
    return b, expect, "s"


@block("yes_no")
def yes_no():
    """YES NO: "y" is Yes (flag 0); Escape is No (flag 1); Down is ignored,
    "n" is No; "x" is ignored, "Y" is Yes. The flags are cleared each time."""
    b, expect = start(), {}
    b.op("YES_NO")
    b.op("IF_EQUAL"); b.op("SAVE", imm(1), var(0x100))
    b.op("IF_NOT_EQUAL"); b.op("SAVE", imm(1), var(0x101))
    b.op("YES_NO")
    b.op("IF_EQUAL"); b.op("SAVE", imm(1), var(0x102))
    b.op("IF_NOT_EQUAL"); b.op("SAVE", imm(1), var(0x103))
    b.op("YES_NO")
    b.op("IF_EQUAL"); b.op("SAVE", imm(1), var(0x104))
    b.op("IF_NOT_EQUAL"); b.op("SAVE", imm(1), var(0x105))
    b.op("YES_NO")
    b.op("IF_EQUAL"); b.op("SAVE", imm(1), var(0x106))
    b.op("IF_NOT_EQUAL"); b.op("SAVE", imm(1), var(0x107))
    b.op("EXIT")
    expect.update({0x100: 1, 0x101: 0, 0x102: 0, 0x103: 1, 0x104: 0, 0x105: 1, 0x106: 1,
                   0x107: 0})
    return b, expect, "y" + "\\e" + "\\vn" + "xY"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--only", metavar="NAME", action="append",
                        help="run only this block (repeatable; the baseline always runs)")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_ecl_input")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    # The baseline of the variables outside WINDOW for eclplay (dqk_ecl's).
    empty = Block()
    empty.op("PRESS_KEY")
    base_vars, base_last = dqk_ecl.port(work, "empty", hlib.replace(glb, dqk_ecl.JUMP,
                                                                   assemble(empty)))
    verdict.check(len(base_vars) == dqk_ecl.VARIABLES and "(done in block 1)" in base_last,
                  "eclplay runs an empty block to its EXIT (the baseline)", base_last)
    for name, make in BLOCKS.items():
        if opts.only and name not in opts.only:
            continue
        b, expect, keys = make()
        dqk_ecl.run_block(verdict, work, name, glb, b, expect, keys, base_vars, opts.verbose)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
