#!/usr/bin/env python3
"""Check #14: DEBUG.TXT where it cannot be made (DEVELOPERS.md, Checks against the
original: "`DEBUG.TXT` in a directory where it cannot be made (read-only)").

The game makes DEBUG.TXT in the current directory (C:\\COK) at Ctrl-D with
Woof (1a46:04f7, 056c), {$I-}, never reading IOResult: Turbo Pascal keeps
the error in InOutRes (DS:43ac) until something reads it. The DEVELOPERS.md asks
whether a later file operation is skipped or stops the game with a runtime
error; the port writes nothing and goes on.

C:\\COK is made read-only on the host (DOSBox-X's local drive then refuses
to create files there; C:\\COK\\SAVE stays writable). START Woof, Ctrl-D at
the start menu, game A loaded, Begin Adventuring: at the first trace Ctrl-Z
(lines to the file) and Ctrl-D (the mode off, the file closed); Solace's
pages to the outpost (pictures read), its inn's camp, and Save as D.
Each step's screen against the port's at the same key read (eclplay
--start-menu --woof), the saved game D against the port's (masked as
save_again.py masks). The port has no directory it cannot write: the
original's recorded behaviour is asserted for the rest: no DEBUG.TXT,
InOutRes (DS:43ac) 0 until Ctrl-D, 2 (the create refused) until Ctrl-D
off, 103 (Close of a file never opened) from then on, never read, with
ExitCode and ErrorAddr 0 and the debug flag (DS:4b51) set from Ctrl-D to
Ctrl-D off.
"""
import argparse
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import e_common as E  # noqa: E402
import save_again  # noqa: E402
cokbox = E.cokbox

# (name, keys, InOutRes then (recorded), the debug flag set)
STEPS = [("start menu", "P", 0, False), ("Ctrl-D", r"\cD", 2, True),
         ("loaded A", r"\v\v\rA", 2, True), ("first trace", r"\v\v\v\v\v\r", 2, True),
         ("Ctrl-Z", r"\cZ", 2, True), ("Ctrl-D off", r"\cD", 103, False),
         ("outpost", r"\r\r\r\r\r\r\r", 103, False), ("inn's camp", r"I\r", 103, False),
         ("saved D", "SD", 103, False)]
IN_OUT_RES, EXIT_CODE, ERROR_ADDR, DEBUG_FLAG = 0x43AC, 0x43A2, 0x43A4, 0x4B51


class ReadOnlyBox(cokbox.Box):
    """A Box whose C:\\COK the host makes read-only (the SAVE directory in
    it stays writable)."""

    def prepare(self):
        if getattr(self, "game", None) is not None and self.game.exists():
            os.chmod(self.game, 0o755)
        super().prepare()
        os.chmod(self.game, 0o555)

    def close(self):
        super().close()
        if getattr(self, "game", None) is not None and self.game.exists():
            os.chmod(self.game, 0o755)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-debug-ro")
    checklib.need_saves()
    v = checklib.Verdict()
    names = {name: name.replace(" ", "-").replace("'", "") for name, _, _, _ in STEPS}
    keys = E.marked([(k, names[name]) for name, k, _, _ in STEPS])
    kw = dict(saves=[E.SAVE], args=["--woof"])
    pair = E.Pair(keys, work / "pair", **kw)
    _, psaves, _ = E.run_port(keys, work / "port-all", E.eclplay(), **kw)
    with ReadOnlyBox(workdir=work / "box", saves=[E.SAVE], args="Woof", exit_after=False,
                     verbose=args.verbose, timeout=600) as box:
        for name, step, res, flag in STEPS:
            box.keys(step)
            took, where = E.takes_keys(box, 30)
            v.check(took, f"{name}: the game waits for a key", ", ".join(where) if took
                    else where)
            box.wait_input(settle=0.5)
            pair.judge(v, names[name], box.screenshot(work / f"{names[name]}.png"),
                       f"{name}: the screen")
            state = (box.ds_word(IN_OUT_RES), box.ds_word(EXIT_CODE),
                     box.ds_read(ERROR_ADDR, 4).hex(), bool(box.ds_byte(DEBUG_FLAG)))
            v.check(state == (res, 0, "00000000", flag),
                    f"{name}: InOutRes {res}, ExitCode 0, ErrorAddr 0000:0000, the debug flag "
                    f"{'set' if flag else 'clear'} (recorded)", str(state))
        made = sorted(f.name for f in box.game.iterdir() if f.name.upper().startswith("DEBUG"))
        v.check(not made, "no DEBUG.TXT in C:\\COK (recorded)", str(made))
        files = sorted(psaves.glob("CHRDATD*")) + sorted(psaves.glob("SAVGAMD.DAT"))
        v.check(files, "the port saved game D", str(files))
        for f in files:
            mine = box.game / "SAVE" / f.name
            v.check(mine.exists() and save_again.masked(f.name, mine.read_bytes()) ==
                    save_again.masked(f.name, f.read_bytes()),
                    f"saved game D: {f.name} as the port's (masked as save_again.py)",
                    "missing" if not mine.exists() else "differs")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
