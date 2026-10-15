#!/usr/bin/env python3
"""Check #14: DEBUG.TXT where it cannot be made (README, Checks against the
original: "`DEBUG.TXT` in a directory where it cannot be made (read-only)").

The game makes DEBUG.TXT in the current directory (C:\\COK) at Ctrl-D with
Woof (1a46:04f7, 056c), {$I-}, never reading IOResult: Turbo Pascal keeps
the error in InOutRes (DS:43ac) until something reads it. The README asks
whether a later file operation is skipped or stops the game with a runtime
error; the port writes nothing and goes on.

C:\\COK is made read-only on the host (DOSBox-X's local drive then refuses
to create files there; C:\\COK\\SAVE stays writable). START Woof, Ctrl-D at
the start menu, game A loaded, Begin Adventuring: at the first trace Ctrl-Z
(lines to the file) and Ctrl-D (the mode off, the file closed); Solace's
pages to the outpost (pictures read), its inn's camp, and Save as D.
InOutRes, ExitCode and ErrorAddr are read at each step; the saved game is
compared with the port's for the same keys (eclplay --woof), masked as
save_again.py masks, and the screens with eclplay's frames.
"""
import argparse
import os
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import e_common as E  # noqa: E402
import save_again  # noqa: E402
cokbox = E.cokbox

STEPS = [("start menu", "P"), ("Ctrl-D", r"\cD"), ("loaded A", r"\v\v\rA"),
         ("first trace", r"\v\v\v\v\v\r"), ("Ctrl-Z", r"\cZ"), ("Ctrl-D off", r"\cD"),
         ("outpost", r"\r\r\r\r\r\r\r"), ("inn's camp", r"I\r"), ("saved D", "SD")]
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
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-debug-ro-")).resolve()
    if not (E.SAVE / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    keys = "".join(k for _, k in STEPS)
    pout, psaves, pshots = cokbox.eclplay(keys, work / "eclplay", saves=[E.SAVE],
                                          args=["--woof"])
    print("port:", "; ".join(line for line in pout.splitlines()
                             if line.startswith(("debug:", "error:")))[:300], "...",
          pout.splitlines()[-1])
    expected = [cokbox.Screen.load(p) for p in pshots]
    status = 0
    box = ReadOnlyBox(workdir=work / "box", saves=[E.SAVE], args="Woof", exit_after=False,
                      verbose=args.verbose, timeout=600)
    try:
        box.start()
        for what, step in STEPS:
            box.keys(step)
            took, where = E.takes_keys(box, 30)
            shown = box.screenshot(work / f"{what.replace(' ', '-')}.png")
            best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
            count, _ = shown.diff(expected[best])
            print(f"original, {what}: InOutRes {box.ds_word(IN_OUT_RES)}, ExitCode "
                  f"{box.ds_word(EXIT_CODE)}, ErrorAddr {box.ds_read(ERROR_ADDR, 4).hex()}, "
                  f"debug flag {box.ds_byte(DEBUG_FLAG)}; "
                  f"{'waits at ' + ', '.join(where) if took else 'no wait: ' + where}; "
                  f"{count} pixels differ from eclplay's {pshots[best].name}")
            if count and what != "Ctrl-D off":  # Solace's page: the port's full stop
                status = 1
        made = sorted(f.name for f in box.game.iterdir() if f.name.upper().startswith("DEBUG"))
        print("DEBUG.TXT in C:\\COK:", made or "none")
        differ = []
        files = sorted(psaves.glob("CHRDATD*")) + sorted(psaves.glob("SAVGAMD.DAT"))
        for f in files:
            mine = box.game / "SAVE" / f.name
            if not mine.exists():
                differ.append(f"{f.name} missing")
            elif save_again.masked(f.name, mine.read_bytes()) != \
                    save_again.masked(f.name, f.read_bytes()):
                differ.append(f.name)
        print(f"saved game D: {len(files)} files from the port, "
              f"differing: {differ or 'none'}")
        if differ:
            status = 1
    finally:
        box.close()
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())
