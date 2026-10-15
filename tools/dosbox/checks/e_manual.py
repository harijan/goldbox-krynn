#!/usr/bin/env python3
"""Checks #15 and #18: Aim's Manual left with Escape, then Down or Target
(README, Checks against the original: "The player's commands in a battle
... in Aim, Manual, then Escape, then Down" and "`Target` after Manual's
Exit (no target, the cursor's last cell valid)").

The battle: game A's first step, the caravan's four baaz (Lnm9 from the
outpost), seed 2 (eclplay and the original agree on it frame for frame:
ISTAN HORBIN acts first). At his turn: Aim, Next (SIR STRONGSWORD beside
him, Target offered), Manual, Escape: nothing picked, the cell still
valid.

#15: Down (0x50) takes the cell of no target: 6beb:0c43 looks for the NULL
record among the entries 1 to DS:6242's count and finds the entry after
the last combatant, whose cell this first battle left 0, 0; then Center.
Where an earlier battle left a record in that entry (a later battle with
fewer combatants) it finds none and takes entry 0's cell (DS:623f, 6240),
other data: the port stops. That entry is written here (a far pointer
that is not NULL, as an earlier battle's record would be; 6beb:0c43 only
compares it), to see what the original does then.

#18: Target: 432f:2f12 reads the NULL record's side (+0x18a, +0x18b) from
0000:018a; the port stops. Each answer to what the original asks is
followed: the screens, the interrupt table and BIOS data (0000:0000-05ff)
before and after, the heap, and whether the battle goes on (Alt-Q puts the
party on Auto; the battle should end and the game wait at its results).
"""
import argparse
import pathlib
import struct
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import e_common as E  # noqa: E402
cokbox = E.cokbox

SEED = 2
TO_BATTLE = E.LOAD.format("A") + r"\r\r\r\r\r\r\rLnm9\r"
MANUAL = r"ANM\e"                 # Aim, Next, Manual, Escape
COUNT, CELLS, RECORDS = 0x6242, 0x623F, 0x68D1
CASES = {"down-center": (15, [r"\v", "C"]), "down-center-stale": (15, [r"\v", "C"]),
         "target-no": (18, ["T", "N"]), "target-yes": (18, ["T", "Y"])}


def entries(box):
    n = box.ds_byte(COUNT)
    cells = [tuple(box.ds_read(CELLS + 4 * i, 2)) for i in range(n + 1)]
    recs = [struct.unpack("<HH", box.ds_read(RECORDS + 4 * i, 4))[::-1] for i in range(n + 1)]
    return n, cells, recs


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-manual-")).resolve()
    if not (E.SAVE / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    status = 0
    for name in args.case or CASES:
        check, after = CASES[name]
        pout, _, pshots = cokbox.eclplay(TO_BATTLE + MANUAL + "".join(after), work / f"port-{name}",
                                         saves=[E.SAVE], args=["--seed", str(SEED)])
        tail = [line for line in pout.splitlines() if line.startswith(("menu:", "error:"))]
        print(f"#{check} {name}: port: {tail[-1]}")
        expected = [cokbox.Screen.load(p) for p in pshots]

        def compare(shown):
            best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
            return shown.diff(expected[best])[0], pshots[best].name

        with E.Box(workdir=work / f"box-{name}", saves=[E.SAVE], seed=SEED, exit_after=False,
                   verbose=args.verbose, timeout=900) as box:
            box.keys(TO_BATTLE + MANUAL)
            box.wait_input(settle=0.7)
            count, shot = compare(box.screenshot(work / f"{name}-manual-left.png"))
            n, cells, recs = entries(box)
            print(f"  original after Manual's Escape: {count} pixels differ from eclplay's "
                  f"{shot}; count byte {n}, entry 0's cell {cells[0]}, entry {n}: cell "
                  f"{cells[n]}, record {recs[n][0]:04x}:{recs[n][1]:04x}")
            if name == "down-center-stale":
                # An earlier battle's record left in the entry after the last
                # combatant: entry n - 1's pointer copied there.
                box.ds_write(RECORDS + 4 * n, box.ds_read(RECORDS + 4 * (n - 1), 4))
                print(f"  entry {n}'s record set to "
                      f"{box.ds_word(RECORDS + 4 * n + 2):04x}:{box.ds_word(RECORDS + 4 * n):04x}")
            low = E.low_memory(box)
            heap = E.heap(box)
            for i, key in enumerate(after):
                box.keys(key)
                took, where = E.takes_keys(box, 30)
                shown = box.screenshot(work / f"{name}-{i}.png")
                count, shot = compare(shown)
                print(f"  {key}: {count} pixels differ from eclplay's {shot}; "
                      f"{'waits at ' + ', '.join(where) if took else where}")
                if not took:
                    break
            if name.startswith("target"):
                seen = E.watch(box, 3, work, f"{name}-after")
                print(f"  {len(seen)} different screens in 3 s after; waits at {box.where()}")
            changed = E.low_changes(low, E.low_memory(box))
            print(f"  low memory changed: {', '.join(changed) or 'nothing'}")
            print(f"  heap before: {heap}\n  heap after:  {E.heap(box)}")
            if name.startswith("target") or name == "down-center-stale":
                # Does the battle go on? The rest of it on Auto (Alt-Q at a
                # player's turn), then its results.
                end = time.monotonic() + 180
                where, turns = None, 0
                while time.monotonic() < end and box.running:
                    took, where = E.takes_keys(box, 30)
                    if not took or "3995:0b01" not in where and "432f:320d" not in where:
                        break
                    # At a player's menu: Escape out of Aim, then Alt-Q.
                    box.keys(r"\e\@q" if "432f:320d" in where else r"\@q")
                    turns += 1
                box.screenshot(work / f"{name}-end.png")
                print(f"  after Alt-Q ({turns} times): {'running' if box.running else 'ended'}, waits at "
                      f"{where}; low memory changed since the start of the turn: "
                      f"{', '.join(E.low_changes(low, E.low_memory(box))) or 'nothing'}")
                print(f"  heap at the end: {E.heap(box)}")
            if name == "down-center":
                print("  " + ("AGREES" if count == 0 else "DIFFERS") + " with the port")
                if count:
                    status = 1
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())
