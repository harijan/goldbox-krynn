#!/usr/bin/env python3
"""Checks #15 and #18: Aim's Manual left with Escape, then Down or Target
(README, Checks against the original: "The player's commands in a battle
... in Aim, Manual, then Escape, then Down" and "`Target` after Manual's
Exit (no target, the cursor's last cell valid)").

The battle: game A's first step, the caravan's four baaz (Lnm9 from the
outpost), seed 2 (eclplay and the original agree on it frame for frame:
ISTAN HORBIN acts first). At his turn: Aim, Next (SIR STRONGSWORD beside
him, Target offered), Manual, Escape: nothing picked, the cell still
valid. Every case compares the screen there with the port's at the same
key read, and the combat's entries (DS:6242's count, the entry after the
last combatant: its record NULL and its cell 0,0, as the port has them).

#15 down-center: Down (0x50) takes the cell of no target: 6beb:0c43
looks for the NULL record among the entries 1 to DS:6242's count and
finds the entry after the last combatant, whose cell this first battle
left 0, 0; then Center. Both screens against the port's.

#15 down-center-stale: where an earlier battle left a record in that entry
(a later battle with fewer combatants) 6beb:0c43 finds none and takes
entry 0's cell (DS:623f, 6240), other data: the port stops. That entry is
written here in the original (a far pointer that is not NULL, as an
earlier battle's record would be; 6beb:0c43 only compares it) and in the
port (the entry held, combat.entry_held): the port must stop there; the
original's recorded behaviour is asserted instead of compared: entry 0's
cell (here 0, 231: y -25) taken, the view as the port's for 0,0 but "RANGE
= 52" for 34 on row 23 (the distance to 0,-25), the Aim menu waiting
again, low memory and the heap unchanged, the battle going on to its end.

#18 target-no and target-yes: Target: 432f:2f12 reads the NULL record's
side (+0x18a, +0x18b) from 0000:018a; the port stops there. The original
(both bytes 0 under DOSBox-X: INT 62h's vector is 0000:0000) asks "ATTACK
ALLY: YES NO" (432f:2f5c's menu) over the screen the port showed at the
read before (compared but row 24). No: back to ISTAN HORBIN's combat menu,
the screen as the port's at that menu (the turn's first read), low memory
and the heap unchanged. Yes: the attack "hits" a record at 0000:0000,
writing over the interrupt table exactly as recorded (README), the turn
ends: MOLLY's menu, as the port's after ISTAN HORBIN's Done, Quit but for
two red @ the attack leaves at column 39 (rows 12 and 14); the heap
unchanged. Both: RandSeed as the port's at
its stop, and the battle goes on to its end (Alt-Q, the party on Auto).
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import e_common as E  # noqa: E402
cokbox = E.cokbox

SEED = 2
TO_BATTLE = E.LOAD.format("A") + r"\r\r\r\r\r\r\rLnm9\r"
MANUAL = r"ANM\e"                 # Aim, Next, Manual, Escape
COUNT, CELLS, RECORDS = 0x6242, 0x623F, 0x68D1
CASES = {"down-center": (15, [(r"\v", "down"), ("C", "center")]),
         "down-center-stale": (15, [(r"\v", "down"), ("C", "center")]),
         "target-no": (18, [("T", "target"), ("N", "no")]),
         "target-yes": (18, [("T", "target"), ("Y", "yes")])}
MENU, AIM_MENU, ATTACK_ALLY = "3995:0b01", "432f:320d", "432f:2f5c"
STALE_POKE = "game->combat.entry_held[game->combat.count] = true;"
STALE_STOP = "the cell of no target: the entry after the last combatant holds"
NULL_STOP = "an attack aimed at no one reads its record through NULL (432f:2f12, 0000:018a)"
# Yes's writes over the interrupt table (words, as low_changes gives them),
# the same in the five runs of batch E (README, #18).
YES_WRITES = (["0008: 0e00 -> 0e07", "000e: 7000 -> 7001", "0012: 7000 -> 0300",
               "00ce: ffbf -> 01bf", "0142: 00f0 -> 01f0"]
              + [f"{a:04x}: {'00f0' if a % 4 == 2 else '60ca'} -> 0000"
                 for a in range(0x146, 0x17C, 2)]
              + ["017c: 60ca -> 002e", "017e: 00f0 -> 25f0", "018c: 0000 -> 01f0",
                 "018e: 0000 -> ee00", "0190: 0000 -> 00ca", "0192: 0000 -> 00f0",
                 "0194: 0000 -> 6efa", "0198: 60ca -> 03ca"])
ROW23, ROW24 = (0, 184, 320, 8), (0, 192, 320, 8)


def entries(box):
    n = box.ds_byte(COUNT)
    cells = [tuple(box.ds_read(CELLS + 4 * i, 2)) for i in range(n + 1)]
    recs = [struct.unpack("<HH", box.ds_read(RECORDS + 4 * i, 4))[::-1] for i in range(n + 1)]
    return n, cells, recs


def port_count(text):
    """The port's DS:6242: its combatants (eclplay's `combat: N NAME at`
    lines of this battle) plus one."""
    return 1 + sum(1 for line in text.splitlines()
                   if line.startswith("combat: ") and " at " in line
                   and line.split()[1].isdigit())


def run_case(v, name, work, verbose):
    check, after = CASES[name]
    print(f"#{check} {name}")
    keys = TO_BATTLE + r"\sturn." + MANUAL + r"\sleft." + E.marked(after)
    kw = dict(saves=[E.SAVE], args=["--seed", str(SEED)])
    pair = E.Pair(keys, work / name, **kw)
    stop = None
    if name == "down-center-stale":
        # The port with the entry held from the read after Escape on.
        _, _, shots = E.run_port(TO_BATTLE + MANUAL, work / "port-stale-0", E.eclplay(), **kw)
        E.poke_at(len(shots))
        stop, _, _ = E.run_port(TO_BATTLE + MANUAL + after[0][0], work / "port-stale",
                                E.eclplay(poke=STALE_POKE), **kw)
        E.poke_at(None)
    with cokbox.Box(workdir=work / f"box-{name}", saves=[E.SAVE], seed=SEED, exit_after=False,
                    verbose=verbose, timeout=900) as box:
        box.keys(TO_BATTLE)
        box.wait_input(settle=0.7)
        pair.judge(v, "turn", box.screenshot(work / f"{name}-turn.png"),
                   f"{name}: ISTAN HORBIN's combat menu")
        box.keys(MANUAL)
        box.wait_input(settle=0.7)
        pair.judge(v, "left", box.screenshot(work / f"{name}-left.png"),
                   f"{name}: after Manual's Escape")
        n, cells, recs = entries(box)
        print(f"  original: count byte {n}, entry 0's cell {cells[0]} (other data), entry {n}: "
              f"cell {cells[n]}, record {recs[n][0]:04x}:{recs[n][1]:04x}")
        v.value(f"{name}: DS:6242, the combatants plus one", n, port_count(pair.text("left")))
        v.check(cells[n] == (0, 0) and recs[n] == (0, 0),
                f"{name}: the entry after the last combatant: cell 0,0, record NULL "
                "(the port's empty entry)", f"cell {cells[n]}, record {recs[n]}")
        if stop is not None:
            box.ds_write(RECORDS + 4 * n, box.ds_read(RECORDS + 4 * (n - 1), 4))
            print(f"  entry {n}'s record set to "
                  f"{box.ds_word(RECORDS + 4 * n + 2):04x}:{box.ds_word(RECORDS + 4 * n):04x}")
        low, heap = E.low_memory(box), E.heap(box)
        shots, wheres, seeds = {}, {}, {}
        for key, mark in after:
            box.keys(key)
            took, where = E.takes_keys(box, 30)
            v.check(took, f"{name}: the game waits for a key after {mark}",
                    ", ".join(where) if took else where)
            if not took:
                break
            box.wait_input(settle=0.7)
            shots[mark] = box.screenshot(work / f"{name}-{mark}.png")
            wheres[mark], seeds[mark] = where, box.ds_dword(E.RANDSEED)
            print(f"  original after {mark}: waits at {', '.join(where)}; row 23 "
                  f"{E.text_row(shots[mark], 23)!r}, row 24 {E.text_row(shots[mark], 24)!r}")
        else:
            if name == "down-center":
                for _, mark in after:
                    pair.judge(v, mark, shots[mark], f"{name}: after {mark}")
                    v.check(AIM_MENU in wheres[mark], f"{name}: Aim's menu waits after {mark}")
            elif stop is not None:
                stale_case(v, name, pair, shots, wheres, stop)
            else:
                target_case(v, name, pair, shots, wheres, seeds)
        changed = E.low_changes(low, E.low_memory(box))
        print(f"  low memory changed: {', '.join(changed) or 'nothing'}")
        if name == "target-yes":
            v.check(changed == YES_WRITES, f"{name}: Yes writes the interrupt table as recorded "
                    "(the port stops: not compared)",
                    f"not recorded: {sorted(set(changed) - set(YES_WRITES))}; recorded, not "
                    f"written: {sorted(set(YES_WRITES) - set(changed))}")
        else:
            v.check(not changed, f"{name}: low memory unchanged (but the BIOS's own counters)",
                    ", ".join(changed))
        after_heap = E.heap(box)
        v.check(after_heap == heap, f"{name}: the heap unchanged",
                f"before {E.heap_text(heap)}, after {E.heap_text(after_heap)}")
        if name != "down-center":
            turns, where = E.to_battle_end(box)
            box.screenshot(work / f"{name}-end.png")
            print(f"  after Alt-Q ({turns} times): waits at {where}")
            v.check(E.BATTLE_OVER in where, f"{name}: the battle goes on to its end (Alt-Q, "
                    "the party on Auto, to 3995:0c9c's menu; recorded)", f"waits at {where}")


def stale_case(v, name, pair, shots, wheres, stop):
    print("  the port is not compared after Down: it stops there, as the README says; the "
          "original's recorded behaviour is asserted")
    v.check(STALE_STOP in (E.port_error(stop) or ""),
            f"{name}: the port stops at the stale entry", E.port_error(stop) or "no stop")
    for mark in ("down", "center"):
        # The port's frame without the stale entry: the cell 0,0, the view
        # clamped to the map's corner, as the original's for 0,-25.
        v.screen(f"{name}: after {mark}, as the port's for the cell 0,0 but row 23",
                 shots[mark], pair.frame(mark),
                 masks=[ROW23 + ("row 23: RANGE, to entry 0's cell, which the port does not "
                                 "reach (it stops)",)])
        v.check(E.text_row(shots[mark], 23) == "RANGE = 52",
                f"{name}: after {mark}, row 23 RANGE = 52 (the distance to 0,-25; recorded)",
                repr(E.text_row(shots[mark], 23)))
        v.check(AIM_MENU in wheres[mark], f"{name}: Aim's menu waits after {mark} (recorded)")


def target_case(v, name, pair, shots, wheres, seeds):
    print("  the port is not compared after Target: it stops there, as the README says; the "
          "original's recorded behaviour is asserted")
    text = pair.text("target")
    v.check(E.port_error(text) == NULL_STOP, f"{name}: the port stops at Target",
            E.port_error(text) or "no stop")
    v.value(f"{name}: RandSeed at the question (the port's at its stop)", seeds["target"],
            E.port_seed(text))
    # The question over the screen the port showed at the read before.
    v.screen(f"{name}: the screen at Attack Ally but row 24", shots["target"],
             pair.frame("left"), masks=[ROW24 + ("row 24: the question, which the port does "
                                                 "not reach (it stops)",)])
    v.check(E.text_row(shots["target"], 24) == "ATTACK ALLY: YES NO",
            f"{name}: row 24 asks ATTACK ALLY: YES NO (recorded)",
            repr(E.text_row(shots["target"], 24)))
    v.check(ATTACK_ALLY in wheres["target"], f"{name}: 432f:2f5c's menu waits (recorded)")
    if name == "target-no":
        v.screen(f"{name}: No gives the turn back (the port's frame of ISTAN HORBIN's menu)",
                 shots["no"], pair.frame("turn"))
        v.check(MENU in wheres["no"], f"{name}: the combat menu waits (recorded)")
    else:
        # MOLLY's menu: the port's frame of it after ISTAN HORBIN quits his
        # turn (Done, Quit) instead, but for the two red glyphs the attack
        # left at column 39, rows 12 and 14 (the same in batch E's 5 runs).
        _, _, quit_shots = E.run_port(TO_BATTLE + "DQ", pair.out / "port-quit", E.eclplay(),
                                      **pair.kw)
        marks = [(312, 96, 8, 8, "column 39, row 12: a glyph the attack left (asserted below)"),
                 (312, 112, 8, 8, "column 39, row 14: the same")]
        v.screen(f"{name}: Yes ends ISTAN HORBIN's turn: MOLLY's menu (the port's after his "
                 "Done, Quit)", shots["yes"], cokbox.Screen.load(quit_shots[-1]), masks=marks)
        glyphs = [d_lib_cell(shots["yes"], 39, r) for r in (12, 14)]
        v.check(glyphs == [("@", 12), ("@", 12)], f"{name}: the attack's two red @ at column "
                "39, rows 12 and 14 (recorded)", str(glyphs))
        v.check(MENU in wheres["yes"], f"{name}: the combat menu waits (recorded)")


def d_lib_cell(shot, col, row):
    """The character at COL, ROW and its colour (d_lib's font reader)."""
    import d_lib
    ch, colours = d_lib.read_cell(shot, col, row)
    return ch, colours[0] if colours else None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-manual")
    checklib.need_saves()
    v = checklib.Verdict()
    for name in args.case or CASES:
        run_case(v, name, work, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
