#!/usr/bin/env python3
"""The Dark Queen of Krynn's ECL arithmetic, comparisons, jumps and tables
run in the original and in eclplay (DARK-QUEEN.md's step 3a: dqk_ecl;
docs/checks.md, "Checks against the original"): four test blocks
(tools/dqkecl.py) that replace block 1 of a copy of DISK3\\ECL.GLB
(tools/hlib.py's replace). In the original, the block variable is set to 1
before Begin Adventuring, as dqk_views' jumper does (5f07:10cd), and the
block's PRESS KEY waits for the random seed (DS:ca0e, set to SEED there, as
eclplay's --seed sets its own). Checked:

- the original waits in the block's PRESS KEY, and its ECL stops at the
  block's last instruction (its EXIT, or the RETURN at depth 0 labelled
  'last') before the variables are read;
- the variables 0x100-0x17f and 0x29 (WINDOW; the blocks zero them first),
  byte for byte, the original's 1 KB at far DS:b20f against eclplay --vars's;
- every other variable (the 1 KB less WINDOW) changes in neither side from
  its baseline: the original's at its PRESS KEY, eclplay's at an empty
  block's end (the same starting state, the blocks being run from the same
  one);
- both leave the values each block computes (expect, written by hand, and
  the random draws by 2102:0234's formula).

The blocks: ADD, SUBTRACT, MULTIPLY, DIVIDE, AND, OR, SAVE and RANDOM, narrow
and wide (both widths of a variable and an immediate); COMPARE with every IF
(9 against 5, and 0xffff against 1, greater as unsigned words); GOTO, GOSUB
(nested too), RETURN, ON GOTO and ON GOSUB in range, at the count and out of
range; the flags AND and OR leave; a RANDOM into a wide variable;
RETURN at depth 0 ending the block; GETTABLE and SAVE TABLE; and the wrap-around
of 16-bit sums and products. DIVIDE by zero is not here: the original hangs
in it (docs/checks.md). About 50 seconds for the four blocks.

    python3 tools/dosbox/checks/dqk_ecl.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import OPCODES, Block, Ref, imm, var, wvar, word  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ["--game", "dark-queen"]
# The block replaced: the developers' jumper (dqk_views), by the block
# variable 0x16 at DS:b20f + 0x16.
JUMP = 1
DS_VARS = 0xB20F
VARIABLES = 0x400  # the 1 KB at DS:b20f, variables 1 to 0x400
# Each block's seed: eclplay's --seed and the original's RandSeed.
SEED = 12345
# The variables the blocks write and the check compares byte for byte: 0x100-0x17f
# (each block's prelude zeroes them first) and 0x29 (DIVIDE's remainder). A block
# that changes any other variable is a difference.
WINDOW = list(range(0x100, 0x180)) + [0x29]


def draw(seed, n):
    """RANDOM's value for range N and the seed after it (dqk 2102:0234, as
    src/ecl.c's cok_tp_random for the Dark Queen's generator): a range of 1 or
    less draws nothing."""
    if n <= 1:
        return 0, seed
    divisor = (0x3FFFFFFF + n) // n
    seed = (seed * 0x6D25 + 1) & 0xFFFFFFFF
    return (seed & 0x3FFFFFFF) // divisor, seed


def prelude(b):
    """Zero the compared variables, so that the original's and eclplay's
    starting values (the original's saved game, eclplay's none) agree."""
    for a in WINDOW:
        b.op("SAVE", imm(0), var(a))


def arithmetic():
    b, expect = Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    b.op("SAVE", imm(200), var(0x100))
    b.op("SAVE", imm(3), var(0x101))
    b.op("ADD", imm(3), var(0x100), var(0x102))              # 203
    b.op("ADD", word(1000), var(0x100), wvar(0x103))         # 1200 = 0x04b0, wide
    b.op("SUBTRACT", imm(7), var(0x100), var(0x105))         # 200 - 7 = 193
    b.op("MULTIPLY", var(0x101), imm(100), wvar(0x106))      # 300 = 0x012c, wide
    b.op("MULTIPLY", var(0x101), imm(100), var(0x108))       # 300, narrow: 0x2c
    b.op("DIVIDE", var(0x100), imm(7), var(0x109))           # 28, remainder 4
    b.op("DIVIDE", wvar(0x106), imm(7), wvar(0x10A))         # 42, remainder 6
    b.op("AND", imm(0x0F), var(0x100), var(0x10C))           # 0x0f & 200 = 8
    b.op("OR", imm(0x0F), var(0x100), var(0x10D))            # 0x0f | 200 = 0xcf
    b.op("SAVE", word(1234), wvar(0x10E))                    # 0x04d2, wide
    b.op("SAVE", var(0x100), var(0x113))                     # 200
    b.op("RANDOM", imm(9), var(0x110))                       # range 10
    b.op("RANDOM", imm(0xFF), var(0x111))                    # range 255
    b.op("RANDOM", var(0x100), var(0x112))                   # range 201
    b.op("RANDOM", imm(0), var(0x114))                       # range 1: 0, no draw
    b.op("EXIT")
    seed = SEED
    expect[0x110], seed = draw(seed, 10)
    expect[0x111], seed = draw(seed, 255)
    expect[0x112], seed = draw(seed, 201)
    expect[0x114] = 0
    expect.update({0x100: 200, 0x101: 3, 0x102: 203, 0x103: 0xB0, 0x104: 0x04, 0x105: 193,
                   0x106: 0x2C, 0x107: 0x01, 0x108: 0x2C, 0x109: 28, 0x10A: 42, 0x10B: 0,
                   0x10C: 8, 0x10D: 0xCF, 0x10E: 0xD2, 0x10F: 0x04, 0x113: 200, 0x29: 6})
    return b, expect


def control():
    b, expect = Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    b.op("SAVE", imm(200), var(0x100))
    # COMPARE 5 with 5: equal. Each IF's next instruction runs if it holds.
    b.op("COMPARE", imm(5), imm(5))
    b.op("IF_EQUAL"); b.op("SAVE", imm(1), var(0x120))
    b.op("IF_NOT_EQUAL"); b.op("SAVE", imm(1), var(0x121))
    b.op("IF_LESS"); b.op("SAVE", imm(1), var(0x122))
    b.op("IF_GREATER"); b.op("SAVE", imm(1), var(0x123))
    b.op("IF_LESS_EQUAL"); b.op("SAVE", imm(1), var(0x124))
    b.op("IF_GREATER_EQUAL"); b.op("SAVE", imm(1), var(0x125))
    # 200 against 300: less.
    b.op("COMPARE", var(0x100), word(300))
    b.op("IF_LESS"); b.op("SAVE", imm(1), var(0x126))
    b.op("IF_GREATER"); b.op("SAVE", imm(1), var(0x127))
    b.op("IF_LESS_EQUAL"); b.op("SAVE", imm(1), var(0x128))
    b.op("IF_GREATER_EQUAL"); b.op("SAVE", imm(1), var(0x129))
    b.op("IF_NOT_EQUAL"); b.op("SAVE", imm(1), var(0x12A))
    b.op("IF_EQUAL"); b.op("SAVE", imm(1), var(0x12B))
    # GOSUB returns to the instruction after it.
    b.op("GOSUB", Ref("sub"))
    b.op("SAVE", imm(0x66), var(0x12D))
    b.op("GOTO", Ref("after_sub"))
    b.label("sub")
    b.op("SAVE", imm(0x55), var(0x12C))
    b.op("RETURN")
    b.label("after_sub")
    # ON GOTO with 2 of 3 targets: to the third.
    b.op("SAVE", imm(2), var(0x12E))
    b.op("ON_GOTO", var(0x12E), imm(3), Ref("l0"), Ref("l1"), Ref("l2"))
    b.op("SAVE", imm(0xEE), var(0x12F))
    b.op("GOTO", Ref("done_on"))
    b.label("l0")
    b.op("SAVE", imm(0xA0), var(0x130))
    b.op("GOTO", Ref("done_on"))
    b.label("l1")
    b.op("SAVE", imm(0xA1), var(0x130))
    b.op("GOTO", Ref("done_on"))
    b.label("l2")
    b.op("SAVE", imm(0xA2), var(0x130))
    b.label("done_on")
    # ON GOTO out of range: it falls through.
    b.op("ON_GOTO", imm(5), imm(2), Ref("m0"), Ref("m1"))
    b.op("SAVE", imm(0xEE), var(0x131))
    b.op("GOTO", Ref("done_oor"))
    b.label("m0")
    b.op("SAVE", imm(0xD0), var(0x132))
    b.op("GOTO", Ref("done_oor"))
    b.label("m1")
    b.op("SAVE", imm(0xD1), var(0x132))
    b.label("done_oor")
    # ON GOSUB to the second target: the instruction after the list runs
    # once it returns.
    b.op("ON_GOSUB", imm(1), imm(2), Ref("s0"), Ref("s1"))
    b.op("SAVE", imm(0xB9), var(0x133))
    b.op("GOTO", Ref("done_ons"))
    b.label("s0")
    b.op("SAVE", imm(0xB0), var(0x134))
    b.op("RETURN")
    b.label("s1")
    b.op("SAVE", imm(0xB1), var(0x134))
    b.op("RETURN")
    b.label("done_ons")
    # GETTABLE a byte of a table at 0x140 (narrow, then wide over two).
    for i, v in enumerate((0x11, 0x22, 0x33, 0x44)):
        b.op("SAVE", imm(v), var(0x140 + i))
    b.op("GETTABLE", word(0x140), imm(2), var(0x135))        # 0x33
    b.op("GETTABLE", word(0x140), imm(1), wvar(0x136))       # 0x3322 over 0x141-0x142
    # SAVE TABLE: a byte at 0x150 + 4, and a word over 0x150 + 6 and 7.
    b.op("SAVE_TABLE", imm(0x77), word(0x150), imm(4))
    b.op("SAVE_TABLE", word(0xBEEF), word(0x150), imm(6))
    # COMPARE 9 with 5: greater. Each IF in turn, into 0x170-0x175.
    b.op("COMPARE", imm(9), imm(5))
    for i, f in enumerate(("IF_EQUAL", "IF_NOT_EQUAL", "IF_LESS", "IF_GREATER",
                           "IF_LESS_EQUAL", "IF_GREATER_EQUAL")):
        b.op(f); b.op("SAVE", imm(1), var(0x170 + i))
    # COMPARE 0xffff with 1: greater as unsigned words (signed, it would be less).
    b.op("COMPARE", word(0xFFFF), imm(1))
    b.op("IF_GREATER"); b.op("SAVE", imm(1), var(0x176))
    b.op("IF_LESS"); b.op("SAVE", imm(1), var(0x177))
    # ON GOTO at the count: 2 of 2 falls through (the targets write 0x179).
    b.op("ON_GOTO", imm(2), imm(2), Ref("c0"), Ref("c1"))
    b.op("SAVE", imm(0xEE), var(0x178))
    b.op("GOTO", Ref("done_cnt"))
    b.label("c0")
    b.op("SAVE", imm(0xD0), var(0x179))
    b.op("GOTO", Ref("done_cnt"))
    b.label("c1")
    b.op("SAVE", imm(0xD1), var(0x179))
    b.label("done_cnt")
    # ON GOSUB at the count (2 of 2) and above (3 of 2): both fall through
    # (the subroutines write 0x17b).
    b.op("ON_GOSUB", imm(2), imm(2), Ref("g0"), Ref("g1"))
    b.op("SAVE", imm(0xB9), var(0x17A))
    b.op("ON_GOSUB", imm(3), imm(2), Ref("g0"), Ref("g1"))
    b.op("SAVE", imm(0xB8), var(0x17C))
    b.op("GOTO", Ref("done_gos"))
    b.label("g0")
    b.op("SAVE", imm(0xB0), var(0x17B))
    b.op("RETURN")
    b.label("g1")
    b.op("SAVE", imm(0xB1), var(0x17B))
    b.op("RETURN")
    b.label("done_gos")
    # Nested GOSUB: the inner one returns to the outer, the outer to here.
    b.op("GOSUB", Ref("outer"))
    b.op("SAVE", imm(0x66), var(0x17F))
    b.op("GOTO", Ref("done_nest"))
    b.label("outer")
    b.op("GOSUB", Ref("inner"))
    b.op("SAVE", imm(0x55), var(0x17D))
    b.op("RETURN")
    b.label("inner")
    b.op("SAVE", imm(0x44), var(0x17E))
    b.op("RETURN")
    b.label("done_nest")
    b.op("EXIT")
    expect.update({0x100: 200, 0x120: 1, 0x121: 0, 0x122: 0, 0x123: 0, 0x124: 1, 0x125: 1,
                   0x126: 1, 0x127: 0, 0x128: 1, 0x129: 0, 0x12A: 1, 0x12B: 0, 0x12C: 0x55,
                   0x12D: 0x66, 0x12E: 2, 0x12F: 0, 0x130: 0xA2, 0x131: 0xEE, 0x132: 0,
                   0x133: 0xB9, 0x134: 0xB1, 0x135: 0x33, 0x136: 0x22, 0x137: 0x33,
                   0x140: 0x11, 0x141: 0x22, 0x142: 0x33, 0x143: 0x44, 0x154: 0x77,
                   0x156: 0xEF, 0x157: 0xBE,
                   # COMPARE 9 with 5: not equal, greater, greater or equal.
                   0x170: 0, 0x171: 1, 0x172: 0, 0x173: 1, 0x174: 0, 0x175: 1,
                   # COMPARE 0xffff with 1: greater, not less.
                   0x176: 1, 0x177: 0,
                   # ON GOTO 2 of 2 and ON GOSUB 2 and 3 of 2: they fall through.
                   0x178: 0xEE, 0x179: 0, 0x17A: 0xB9, 0x17B: 0, 0x17C: 0xB8,
                   # Nested GOSUB.
                   0x17D: 0x55, 0x17E: 0x44, 0x17F: 0x66})
    return b, expect


def flags():
    """AND's and OR's flags (their result compared with 0, as 2fd3:0ed3 does),
    a RANDOM into a wide variable, and a RETURN at depth 0, which ends the block:
    the RETURN is the block's last instruction (labelled 'last'), and the
    SAVE after it must not run."""
    b, expect = Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    b.op("SAVE", imm(0x10), var(0x100))
    b.op("AND", imm(0x0F), var(0x100), var(0x101))          # 0x10 & 0x0f = 0: equal
    b.op("IF_EQUAL"); b.op("SAVE", imm(1), var(0x102))
    b.op("IF_NOT_EQUAL"); b.op("SAVE", imm(1), var(0x103))
    b.op("OR", imm(1), imm(2), var(0x104))                  # 1 | 2 = 3: 0 is less
    b.op("IF_LESS"); b.op("SAVE", imm(1), var(0x105))
    b.op("IF_GREATER"); b.op("SAVE", imm(1), var(0x106))
    b.op("SAVE", imm(0x77), var(0x108))
    b.op("RANDOM", imm(9), wvar(0x107))                     # range 10, into 0x107-0x108
    b.label("last")
    b.op("RETURN")                                          # depth 0: the block ends
    b.op("SAVE", imm(0xEE), var(0x109))                     # not run
    b.op("EXIT")
    expect.update({0x100: 0x10, 0x101: 0, 0x102: 1, 0x103: 0, 0x104: 3, 0x105: 1, 0x106: 0,
                   0x107: draw(SEED, 10)[0], 0x108: 0, 0x109: 0})
    return b, expect


def edges():
    b, expect = Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    b.op("ADD", word(0xFFFF), imm(2), wvar(0x100))           # 0x10001 -> 1
    b.op("SUBTRACT", imm(7), imm(5), wvar(0x102))            # 5 - 7 -> 0xfffe
    b.op("MULTIPLY", word(0xFFFF), imm(2), wvar(0x104))      # 0x1fffe -> 0xfffe
    b.op("ADD", imm(0xFF), imm(1), var(0x106))               # 256 narrow -> 0
    b.op("ADD", imm(0xFF), imm(1), wvar(0x107))              # 256 wide -> 0x0100
    b.op("SAVE", word(0xABCD), var(0x109))                   # narrow: 0xcd
    b.op("EXIT")
    expect.update({0x100: 1, 0x101: 0, 0x102: 0xFE, 0x103: 0xFF, 0x104: 0xFE, 0x105: 0xFF,
                   0x106: 0, 0x107: 0x00, 0x108: 0x01, 0x109: 0xCD})
    return b, expect


BLOCKS = {"arithmetic": arithmetic, "control": control, "flags": flags, "edges": edges}


def port(work, name, patched, typed="", extra=()):
    """eclplay's run of the block (from a copy of the assets with the block
    replaced): its variables (--vars), its output's last line, and whether
    eclplay reports keys left unread (TYPED are the keys the block reads after
    its PRESS KEY, eclplay --keys escapes; EXTRA are more eclplay arguments)."""
    assets = work / f"assets-{name}"
    if assets.exists():
        shutil.rmtree(assets)
    assets.mkdir(parents=True)
    for entry in dqk_lib.ASSETS.iterdir():
        if entry.name != "DISK3":
            (assets / entry.name).symlink_to(entry)
            continue
        (assets / "DISK3").mkdir()
        for f in entry.iterdir():
            if f.name != "ECL.GLB":
                (assets / "DISK3" / f.name).symlink_to(f)
    (assets / "DISK3" / "ECL.GLB").write_bytes(patched)
    vars_path = work / f"port-{name}.vars"
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--seed", str(SEED), "--vars",
                             str(vars_path), "--keys", "\\r" + typed, *extra, str(assets),
                             str(JUMP)],
                            capture_output=True, text=True, cwd=ROOT, timeout=300)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    last = (result.stdout.strip().splitlines() or [""])[-1]
    unread = "(keys left unread" in result.stderr
    return (vars_path.read_bytes() if vars_path.exists() else b""), last, unread


def original(work, name, patched, verbose, typed=""):
    """The original's run of the block from its PRESS KEY (where the box sets
    the seed) to its end, TYPED keys after the PRESS KEY's. Returns (the state
    where it waits, the variables there, the state at its end, the variables
    then); a state is (pc, opcode) from DS:b7f9 and DS:b1c7 (see stop_at), and
    the variables are the 1 KB at DS:b20f."""
    with cokbox.Box(workdir=work / f"box-{name}", game="dark-queen", verbose=verbose,
                    timeout=900, files={"DISK3/ECL.GLB": patched}) as box:
        variables = enter_block(box)
        # The block's first instruction: the PRESS KEY, where the game
        # waits; the seed is set there, after the copy protection's draws.
        wait = dqk_lib.ecl_state(box)
        box.set_seed(SEED)
        before = box.read(variables + 1, VARIABLES)
        box.keys("\\r" + typed)
        box.settled(tolerance=64)
        end = dqk_lib.ecl_state(box)
        after = box.read(variables + 1, VARIABLES)
        return ((wait["pc"], wait["opcode"]), before,
                (end["pc"], end["opcode"]), after)


def enter_block(box):
    """The original from its start to the block's PRESS KEY (the jumper set to
    JUMP and Begin Adventuring, the copy protection answered): the box waits in
    the PRESS KEY, the seed not yet set. Returns the address of the block
    variables (DS:b20f)."""
    for _ in range(600):
        shot = box.screenshot()
        if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
            break
        box.settle(0.1)
    box.keys("\\e")
    box.settled(tolerance=64)
    freeze = (box.ds - 0x422E + 0x261B) * 16 + 0x05BE  # dqk_menus' freeze_cycles
    if box.read(freeze, 1) != b"\x55":
        raise cokbox.BoxError("261b:05be is not where it was expected")
    box.write(freeze, b"\xcb")
    box.keys("plta")
    box.settled(tolerance=64)
    variables = box.far(box.ds * 16 + DS_VARS)
    box.write(variables + 0x16, bytes([JUMP]))
    box.keys("b")
    box.settled(tolerance=64)
    _, _, answer = dqk_lib.protection(box)
    box.keys(answer + "\\r")
    # The copy protection's draws are done; the block's PRESS KEY is next.
    box.settled(tolerance=64)
    return variables


def stop_at(block):
    """Where the block's ECL stops, as the original's DS:b7f9 (pc) and DS:b1c7
    (opcode) read there: the RETURN labelled 'last' (a RETURN at depth 0 ends a
    block), else the block's final EXIT. pc is the address past the opcode
    (PRESS KEY at 0x8015 reads 0x8016, the final EXIT at 0x83a1 reads 0x83a2);
    a RETURN at depth 0 reads two past its own, as the port's exit_script
    steps past the RETURN and then past the EXIT it is (ecl.c:923, 1032)."""
    if "last" in block.labels:
        return block.address("last") + 2, OPCODES["RETURN"]
    return dqkecl.CODE_START + block.size, OPCODES["EXIT"]


def changed(before, after):
    """The variables (1 to 0x400) whose values differ."""
    return [a for a in range(1, VARIABLES + 1) if before[a - 1] != after[a - 1]]


def compare(verdict, name, orig, ported, port_last, expect, window=WINDOW):
    """ORIG and PORTED: (before, after), the variables at the block's PRESS
    KEY (the port's: its empty block's end) and at its end, 1 KB each."""
    (o_before, o_after), (p_before, p_after) = orig, ported
    if len(p_after) != VARIABLES or len(o_after) != VARIABLES:
        verdict.check(False, f"{name}: both sides read the variables",
                      port_last or "eclplay saved no variables")
        return
    verdict.check("(done in block 1)" in port_last, f"{name}: eclplay ran the block to its EXIT",
                  port_last)
    for side, before, after in (("the original", o_before, o_after),
                                ("eclplay", p_before, p_after)):
        outside = [a for a in changed(before, after) if a not in window]
        verdict.check(not outside, f"{name}: {side} changes nothing outside the block's window",
                      ", ".join(f"{a:x}" for a in outside[:8]))
    differing = [a for a in window if o_after[a - 1] != p_after[a - 1]]
    verdict.check(not differing, f"{name}: the block's window agrees",
                  ", ".join(f"{a:x}: original {o_after[a - 1]:02x}, port {p_after[a - 1]:02x}"
                            for a in differing[:8]))
    for side, vars_ in (("eclplay", p_after), ("the original", o_after)):
        wrong = [a for a, v in sorted(expect.items()) if vars_[a - 1] != v]
        verdict.check(not wrong, f"{name}: {side} leaves the values the block computes",
                      ", ".join(f"{a:x}: {vars_[a - 1]:x} not {expect[a]:x}" for a in wrong[:8]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_ecl")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    # The baseline of the variables outside WINDOW for eclplay: an empty block's
    # end (the blocks start from the same state, so it is every block's).
    empty = Block()
    empty.op("PRESS_KEY")
    base_vars, base_last, base_unread = port(work, "empty",
                                             hlib.replace(glb, JUMP, dqkecl.assemble(empty)))
    verdict.check(len(base_vars) == VARIABLES and "(done in block 1)" in base_last
                  and not base_unread, "eclplay runs an empty block to its EXIT (the baseline)",
                  base_last)
    for name, make in BLOCKS.items():
        block, expect = make()
        run_block(verdict, work, name, glb, block, expect, "", base_vars, opts.verbose)
    return verdict.finish()


def run_block(verdict, work, name, glb, block, expect, typed, base_vars, verbose, window=WINDOW):
    """One block run in the original and in eclplay, its ECL.GLB GLB with the
    block replacing block 1 (TYPED: the keys after its PRESS KEY, see
    dqk_ecl_input.py), judged against BASE_VARS (the empty block's end in
    eclplay)."""
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
    port_vars, port_last, port_unread = port(work, name, patched, typed)
    verdict.check(not port_unread, f"{name}: eclplay leaves no keys unread",
                  "the block took fewer keys than were typed")
    wait, before, end, after = original(work, name, patched, verbose, typed)
    # The PRESS KEY is the block's first instruction (its entry, 0x8015): pc reads past it.
    verdict.check(wait == (block.address("entry") + 1, OPCODES["PRESS_KEY"]),
                  f"{name}: the original waits in the block's PRESS KEY",
                  f"pc {wait[0]:#x}, opcode {wait[1]:#x}, not {block.address('entry') + 1:#x}, "
                  f"{OPCODES['PRESS_KEY']:#x}")
    stop = stop_at(block)
    verdict.check(end == stop, f"{name}: the original's ECL stopped at the block's last "
                  "instruction", f"pc {end[0]:#x}, opcode {end[1]:#x}, not "
                  f"{stop[0]:#x}, {stop[1]:#x}")
    compare(verdict, name, (before, after), (base_vars, port_vars), port_last, expect, window)


if __name__ == "__main__":
    sys.exit(checklib.run(main))
