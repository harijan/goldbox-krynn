#!/usr/bin/env python3
"""The Dark Queen of Krynn's party opcodes run in the original and in eclplay
(DARK-QUEEN.md's step 3g: dqk_ecl_party; docs/checks.md, "Checks against the
original"): two test blocks (tools/dqkecl.py) that replace block 1 of a copy of
DISK3\\ECL.GLB, as dqk_ecl.py does, with the party of the original's saved game A
on both sides: eclplay takes SAVE\\SAVGAMA.QSV's six members' files, in the
saved game's order (ROLANDOF, TIMASALI, 'CINDER', BALTHIRN, ROARK'CR, WALKERKE).

- party (variables): LOAD CHARACTER 0, 1, 5 and 0x0a (past the end: the
  selection stays) with the selected member's race and class (fields 0xad and
  0xae) saved to the variables; FIND NPC for a member's name (none has the NPC
  bit, so 0xff) and for a name not in the party; and who_select: WHO with Down
  and S picks the second member.
- who_add_ep (screens): the words of ADD EP for one member and for the party
  (colour 7, the window drawn by the Dark Queen's own routine), and WHO's pick
  with its prompt (the original's "who will attempt to disarm the trap?" is shown
  as "Who will try to disarm?"); each wait compared as dqk_ecl_text does, with the
  pick's bottom bar left out.

ADD EP's experience and may-train are not checked here: the ECL cannot read them
(the Dark Queen's character variables do not reach the experience), so the port's
arithmetic is checked by tests/test_adventure.c's test_dark_queen_party.

    python3 tools/dosbox/checks/dqk_ecl_party.py [--workdir DIR] [--verbose]
        [--only NAME]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_ecl_text  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_views  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm, var, word, text  # noqa: E402

cokbox = checklib.cokbox
GAME = ["--game", "dark-queen"]
# The saved game A's party in its order: SAVE\SAVGAMA.QSV's records (0x40f, 0x5f1,
# 0x7ed, 0x9e9, 0xbee, 0xdda: each one its file's bytes, the pointers aside).
PARTY = ["ROLANDOF.QCH", "TIMASALI.QCH", "'CINDER'.QCH", "BALTHIRN.QCH", "ROARK'CR.QCH",
         "WALKERKE.QCH"]
JUMP = dqk_ecl.JUMP
WINDOW = dqk_ecl.WINDOW
# The selected member's race and class: fields 0xad and 0xae, at 0x401 + the field.
RACE, CLASS = 0x4AE, 0x4AF
NOT_FOUND = 0xFF

dqkecl.OPCODES.update({"WHO": 0x39, "FIND_NPC": 0x37, "ADD_EP": 0x41})
dqk_ecl_text.WAIT_NAMES[0x39] = "WHO"
# The pick (WHO) takes its selection with S, as the Dark Queen's menus do (dqk_items);
# every other wait takes a Return. The port's keys are the same, in order.
WHO_OPCODE = 0x39
WAIT_KEYS = "\\r\\r\\rs"


def wait_key(opcode):
    return "s" if opcode == WHO_OPCODE else "\\r"


def party_args():
    return [arg for name in PARTY
            for arg in ("--character", str(dqk_lib.ASSETS / "SAVE" / name))]


def party_block():
    """LOAD CHARACTER 0, 1, 5 and 0x0a (past the end: the selection stays, WALKERKE),
    each member's race (field 0xad) and class (0xae) saved to the variables; FIND NPC
    for a member's name (none has the NPC bit) and for a name not in the party."""
    b = Block()
    b.op("PRESS_KEY")
    dqk_ecl.prelude(b)
    at = 0x100
    for member in (0, 1, 5, 0x0A):
        b.op("LOAD_CHARACTER", imm(member))
        b.op("SAVE", var(RACE), var(at))
        b.op("SAVE", var(CLASS), var(at + 1))
        at += 2
    b.op("FIND_NPC", text("ROLAND OF DUST"), var(0x108))
    b.op("FIND_NPC", text("NOBODY"), var(0x109))
    b.op("EXIT")
    # ROLANDOF (race 6, class 1), TIMASALI (1, 9), WALKERKE (5, 12) from their files.
    expect = {0x100: 6, 0x101: 1, 0x102: 1, 0x103: 9, 0x104: 5, 0x105: 12, 0x106: 5,
              0x107: 12, 0x108: NOT_FOUND, 0x109: NOT_FOUND}
    return b, expect


def screens_block():
    """The jumper's start (LOAD FILES, the SAVEs, a PRESS KEY: wait 1), member 0's
    ADD EP 3 (wait 2), the party's ADD EP 257 (wait 3), and WHO's pick with the
    game's prompt (wait 4; Enter picks the selected member)."""
    b = Block()
    dqk_ecl_text.start(b)
    b.op("LOAD_CHARACTER", imm(0))
    b.op("ADD_EP", imm(0), word(3))
    b.op("PRESS_KEY")
    b.op("ADD_EP", imm(1), word(257))
    b.op("PRESS_KEY")
    b.op("WHO", text("WHO WILL ATTEMPT TO DISARM THE TRAP?"))
    b.op("EXIT")
    return b


def who_block():
    """LOAD CHARACTER 0, then WHO: the keys typed (Down, then S) pick member 1, whose
    race and class are saved (the pick's selection, and that its keys are all read)."""
    b = Block()
    b.op("PRESS_KEY")
    dqk_ecl.prelude(b)
    b.op("LOAD_CHARACTER", imm(0))
    b.op("WHO", text("WHO WILL ATTEMPT TO DISARM THE TRAP?"))
    b.op("SAVE", var(RACE), var(0x110))
    b.op("SAVE", var(CLASS), var(0x111))
    b.op("EXIT")
    return b, {0x110: 1, 0x111: 9}  # TIMASALI


def variables(verdict, work, glb, verbose):
    base = dqk_ecl_base(work, glb)
    block, expect = party_block()
    dqk_ecl.run_block(verdict, work, "party", glb, block, expect, "", base, verbose)
    block, expect = who_block()
    dqk_ecl.run_block(verdict, work, "who_select", glb, block, expect, "\\vs", base, verbose)


def dqk_ecl_base(work, glb):
    """eclplay's variables at an empty block's end, with the party (the baseline)."""
    empty = Block()
    empty.op("PRESS_KEY")
    base_vars, base_last, base_unread = dqk_ecl.port(
        work, "empty", hlib.replace(glb, JUMP, dqkecl.assemble(empty)))
    if len(base_vars) != dqk_ecl.VARIABLES or "(done in block 1)" not in base_last:
        raise cokbox.BoxError(f"eclplay runs an empty block with the party: {base_last}")
    return base_vars


def screens(verdict, work, glb, verbose):
    name = "who_add_ep"
    block = screens_block()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
    shots, names, final, end, after, lamps, square = dqk_ecl_text.original(
        work, name, block, patched, verbose, key_for=wait_key)
    stop = dqk_ecl_text.stop_of(block)
    verdict.check(end == stop, f"{name}: the original's ECL stopped at the block's last "
                  "instruction", f"pc {end[0]:#x}, opcode {end[1]:#x}, not "
                  f"{stop[0]:#x}, {stop[1]:#x}")
    mine, _, port_vars, port_last = dqk_ecl_text.port(work, name, patched, lamps, square,
                                                     len(shots), keys=WAIT_KEYS)
    verdict.check(len(mine) == len(shots), f"{name}: eclplay waits where the original does "
                  f"({len(shots)} waits)", f"{len(mine)} waits; {port_last}")
    for i, (o, p) in enumerate(zip(shots, mine)):
        o.save_png(work / f"original-{name}-{i + 1}.png")
        masks = dqk_ecl_text.MASKS + ([dqk_views.BAR] if names[i] == "WHO" else [])
        verdict.screen(f"{name}: wait {i + 1} ({names[i]})", o, p, masks=masks, work=work)
    if len(port_vars) == dqk_ecl.VARIABLES:
        differing = [a for a in WINDOW if after[a - 1] != port_vars[a - 1]]
        verdict.check(not differing, f"{name}: the variables 0x100-0x17f agree",
                      ", ".join(f"{a:x}: original {after[a - 1]:02x}, port {port_vars[a - 1]:02x}"
                                for a in differing[:8]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--only", nargs="*", choices=["party", "who_add_ep"],
                        help="these parts (party: the variables; who_add_ep: the screens)")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_ecl_party")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    # Every eclplay run of this flow has the saved game's party.
    dqk_ecl.GAME = GAME + party_args()
    dqk_ecl_text.GAME = dqk_ecl.GAME
    run = opts.only or ["party", "who_add_ep"]
    if "party" in run:
        variables(verdict, work, glb, opts.verbose)
    if "who_add_ep" in run:
        screens(verdict, work, glb, opts.verbose)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
