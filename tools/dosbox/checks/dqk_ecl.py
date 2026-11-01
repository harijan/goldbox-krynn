#!/usr/bin/env python3
"""The Dark Queen of Krynn's ECL arithmetic, comparisons, jumps and tables
run in the original and in eclplay (DARK-QUEEN.md's step 3a: dqk_ecl;
docs/checks.md, "Checks against the original"): three test blocks
(tools/dqkecl.py) that replace block 1 of a copy of DISK3\\ECL.GLB
(tools/hlib.py's replace). In the original, the block variable is set to 1
before Begin Adventuring, as dqk_views' jumper does (5f07:10cd), and the
block's PRESS KEY waits for the random seed (DS:ca0e, set to SEED there, as
eclplay's --seed sets its own). Compared: the variables 0x100-0x17f and
0x29 (the blocks zero them first), byte for byte, the original's 1 KB at far
DS:b20f against eclplay --vars's, and eclplay's values against what each
block should leave (the random draws by 2102:0234's formula).

The blocks: ADD, SUBTRACT, MULTIPLY, DIVIDE, AND, OR, SAVE and RANDOM, narrow
and wide (both widths of a variable and an immediate); COMPARE with every
IF, GOTO, GOSUB and RETURN, ON GOTO and ON GOSUB in and out of range,
GETTABLE and SAVE TABLE; and the wrap-around of 16-bit sums and products.
DIVIDE by zero is not here: the original hangs in it (docs/checks.md). About
35 seconds for the three blocks.

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
from dqkecl import Block, Ref, imm, var, wvar, word  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ["--game", "dark-queen"]
# The block replaced: the developers' jumper (dqk_views), by the block
# variable 0x16 at DS:b20f + 0x16.
JUMP = 1
DS_VARS = 0xB20F
# Each block's seed: eclplay's --seed and the original's RandSeed.
SEED = 12345
# The variables a block's prelude zeroes and the check compares: 0x100-0x17f
# and the remainder (0x29, DIVIDE's).
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
    b.op("EXIT")
    expect.update({0x100: 200, 0x120: 1, 0x121: 0, 0x122: 0, 0x123: 0, 0x124: 1, 0x125: 1,
                   0x126: 1, 0x127: 0, 0x128: 1, 0x129: 0, 0x12A: 1, 0x12B: 0, 0x12C: 0x55,
                   0x12D: 0x66, 0x12E: 2, 0x12F: 0, 0x130: 0xA2, 0x131: 0xEE, 0x132: 0,
                   0x133: 0xB9, 0x134: 0xB1, 0x135: 0x33, 0x136: 0x22, 0x137: 0x33,
                   0x140: 0x11, 0x141: 0x22, 0x142: 0x33, 0x143: 0x44, 0x154: 0x77,
                   0x156: 0xEF, 0x157: 0xBE})
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


BLOCKS = {"arithmetic": arithmetic, "control": control, "edges": edges}


def port(work, name, patched):
    """eclplay's run of the block (from a copy of the assets with the block
    replaced): its variables (--vars) and its output's last line."""
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
                             str(vars_path), "--keys", "\\r", str(assets), str(JUMP)],
                            capture_output=True, text=True, cwd=ROOT, timeout=300)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    last = (result.stdout.strip().splitlines() or [""])[-1]
    return (vars_path.read_bytes() if vars_path.exists() else b""), last


def original(work, name, patched, verbose):
    """The original's 1 KB of variables after the block (at its end, the
    game waiting in its adventure loop), from the block's PRESS KEY on."""
    with cokbox.Box(workdir=work / f"box-{name}", game="dark-queen", verbose=verbose,
                    timeout=900, files={"DISK3/ECL.GLB": patched}) as box:
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
        # The block's first instruction: the PRESS KEY, where the game
        # waits; the seed is set there, after the copy protection's draws.
        box.settled(tolerance=64)
        opcode = dqk_lib.ecl_state(box)["opcode"]
        box.set_seed(SEED)
        box.keys("\\r")
        box.settled(tolerance=64)
        return box.read(variables + 1, 0x400), opcode


def compare(verdict, name, original_vars, port_vars, port_last, expect):
    if len(port_vars) != 0x400:
        verdict.check(False, f"{name}: eclplay ran the block", port_last or "no variables saved")
        return
    verdict.check("(done in block 1)" in port_last, f"{name}: eclplay ran the block to its EXIT",
                  port_last)
    differing = [a for a in WINDOW if original_vars[a - 1] != port_vars[a - 1]]
    verdict.check(len(original_vars) == 0x400 and not differing,
                  f"{name}: the variables 0x100-0x17f and 0x29 agree",
                  ", ".join(f"{a:x}: original {original_vars[a - 1]:02x}, port {port_vars[a - 1]:02x}"
                            for a in differing[:8]))
    for side, vars_ in (("eclplay", port_vars), ("the original", original_vars)):
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
    for name, make in BLOCKS.items():
        block, expect = make()
        patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
        port_vars, port_last = port(work, name, patched)
        original_vars, opcode = original(work, name, patched, opts.verbose)
        verdict.check(opcode == 0x1D, f"{name}: the original waits in the block's PRESS KEY",
                      f"opcode {opcode:#x}")
        compare(verdict, name, original_vars, port_vars, port_last, expect)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
