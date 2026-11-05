#!/usr/bin/env python3
"""The Dark Queen of Krynn's bar message (177e:153c: 1a62:0046 at x 0, row 0x18, white on grey)
where the original draws it (docs/checks.md, "Checks against the original": dqk_bar). Main's
menu, Party (p) and Delete Character (d) with no .QCH in the save folder: "No characters to
delete." (DS:5f5d) on the bar for the speed's time. The spin is written at 177e:1560 (the
instruction after the lcall 1a62:0046: the page flipped, the message shown) before the key, and
the screen taken while it spins: the box's black edges at pixels 7 and 8 + 8 * 24 = 200 (rows
187-197), the top and bottom edges from 7 to 200, the text from pixel 8 (its first lit column
at 9: the N's), the bar's colours left of 7. The port's place is test_dark_queen.c's
test_bar_message_place (the same columns, "Guarding": eclplay's shots are taken at key waits,
after the message's bar is drawn again). About half a minute.

    python3 tools/dosbox/checks/dqk_bar.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402

cokbox = checklib.cokbox
MESSAGE = "No characters to delete."
SPIN = (0x177E, 0x1560)  # 177e:153c's mov sp, bp after the lcall 1a62:0046
BLACK = (0, 0, 0)


class NoRoster(cokbox.Box):
    """The box with no .QCH in its game's folders: Delete Character has none to list."""

    def prepare(self):
        super().prepare()
        for f in self.game.rglob("*"):
            if f.suffix.upper() == ".QCH":
                f.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_bar")
    verdict = checklib.Verdict()
    with NoRoster(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                  timeout=600) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        dqk_lib.escape_title(box)
        box.keys("p")
        box.settled(tolerance=64)
        seg, off = box.from_ghidra(*SPIN)
        addr = seg * 16 + off
        old = box.read(addr, 2)
        if old != b"\x8b\xe5":
            raise cokbox.BoxError(f"177e:1560 holds {old.hex()}, not 8be5 (mov sp, bp)")
        box.write_paused([(addr, b"\xeb\xfe")])
        box.keys("d")
        held = any(L.held(box, [(addr, old)], seconds=0.6)[0] for _ in range(30))
        if not held:
            raise cokbox.BoxError("the spin at 177e:1560 did not hold after Delete")
        shot = box.screenshot(path=str(work / "message.png"))
        L.untrap(box, [(addr, old)])
    # The original's columns against the port's (prompt_box at x 0: test_bar_message_place).
    right = 8 + 8 * len(MESSAGE)
    span = range(0, right + 1)  # the bar's own black starts at right + 1 on rows 194-195
    edges = {y: [x for x in span if shot.at(x, y) == BLACK] for y in range(188, 197)}
    verdict.value("the box's black side edges on rows 188-196 (pixels)",
                  edges, {y: [7, right] for y in edges})
    for y in (187, 197):
        row = [x for x in span if shot.at(x, y) == BLACK]
        verdict.value(f"the box's black edge on row {y} (pixels)", row, list(range(7, right + 1)))
    lit = [x for x in range(320) if any(shot.at(x, y) == (255, 255, 255) for y in range(189, 197))]
    verdict.value("the text's first lit column (the N's, from pixel 8)", lit[0] if lit else None, 9)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
