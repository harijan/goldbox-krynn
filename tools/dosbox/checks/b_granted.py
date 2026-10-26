#!/usr/bin/env python3
"""Check #45: a knight without a cleric level memorizing granted powers
(docs/checks.md, Checks against the original: "With a knight above level 6 and no
cleric level who knows a granted power, memorize it: the port lets it be
marked again and again, counting against 0x71 (4888:0700); see whether the
original does").

Game A, SIR STRONGSWORD made a level 7 knight of order 2 (+0x5c; order 1
gets no cleric spells a day, and with no spells a day of any kind he
"cannot memorize any spells") knowing the seven granted powers 0x65-0x6b
(class 2, level 0). With a cleric level 4888:0700 allows one power a day
(three for deity 4); without one it counts from an uninitialized local,
0x71 in Memorize's call. A power marked leaves the grimoire, so "again and
again" is the seven of them marked one after another: in the outpost inn's
camp, Magic, Memorize, up eleven rows to the first power, Enter eight times
(seven powers, then the cleric's Bless that moves up), Exit, Exit (the
list of those marked), Yes, Exit,
Save as F. The original's memorized bytes (+0x1e-+0x57) are read after the
picks: all seven powers marked, as recorded (the port does not stop at the
picks, so that is the original's observation); read again in camp before
the Save they must be the port's (its F), and the saved game the port's,
masked.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_magic_lib as M  # noqa: E402
import checklib  # noqa: E402

STEPS = [("the picks", "MM" + r"\^" * 11 + r"\r" * 8), ("Exit", "E"),
         ("the list of those marked", "E"), ("Yes", "Y"), ("Exit", "E")]


def marked(raw):
    return [hex(b) for b in raw[0x1E:0x58] if b]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-granted")
    game = L.copy_game(L.SAVE, "A", work / "game")
    edits = {0x100: 7, 0x5C: 2}
    edits.update({0x62 + i: 1 for i in range(0x65, 0x6C)})
    L.patch(game / "CHRDATA1.SAV", edits)
    seen = []

    def probe(box, i):
        _, raw = L.member(box, "SIR STRONGSWORD")
        what = STEPS[i][0] if i >= 0 else "in camp"
        seen.append(marked(raw))
        print(f"original after {what}: memorized/marked bytes {marked(raw)}; cleric spells a day "
              f"{raw[0x11C:0x121].hex()}")

    out, psaves, odir, _ = M.run_both(work, game, "".join(k for _, k in STEPS),
                                      steps=[k for _, k in STEPS], probe=probe,
                                      verbose=args.verbose)
    v = checklib.Verdict()
    print("port's choices:", [ln[8:] for ln in out.splitlines() if ln.startswith("choice: ")][-12:])
    powers = sorted(int(b, 16) for b in seen[1] if 0xE5 <= int(b, 16) <= 0xEB)
    v.check(powers == list(range(0xE5, 0xEC)), "the original marks all seven powers "
            "(0xe5-0xeb) after the picks, as recorded; the port is compared in camp below",
            str([hex(b) for b in powers]))
    v.value("SIR STRONGSWORD's memorized bytes in camp before the Save", seen[-1],
            marked(L.sav(psaves, "F", 1)))
    L.judge_games(v, "saved F", odir, "F", psaves, "F")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
