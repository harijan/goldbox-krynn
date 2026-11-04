#!/usr/bin/env python3
"""The Dark Queen of Krynn's recompute (dqk 177e:0be2) of a character the
original had not already recomputed, in the original against eclplay --game
dark-queen --start-menu (DARK-QUEEN.md's step 2: dqk-create; docs/checks.md,
"Checks against the original": dqk_stats).

The shipped saved game A, from a patched copy in both, with its first member
made by hand: a cloak (type 0x3a, +3) that magic armour keeps and any other
cloak would lose; a body armour (type 0x1f, armour class 0x34) of +100 as its
last item, so that the armour class part wraps below 0 as a signed byte and
177e:0f5f compares it unsigned (the armour is the last item, or a later item
beats the negative part); and effect 0x4a, which doubles the movement. Load, Save Current Game: the saved game each writes
holds the recomputed record (604b:28f8 at the load), compared but for the
heap's far pointers.

    python3 tools/dosbox/checks/dqk_stats.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_train  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")


def hand_made(data):
    """The saved game with its first member's second item made the cloak and
    its fourth the armour (readied, with their bonus) and an effect 0x4a on it."""
    data = bytearray(data)
    at = 0x40F
    items = at + 0x19E
    for k, (kind, bonus) in ((1, (0x3A, 3)), (3, (0x1F, 100))):
        item = items + 17 * k
        data[item] = kind
        data[item + 8] = bonus
        data[item + 10] = 1  # readied
        data[item + 13] = 0  # no count
    data[at + 4:at + 8] = bytes([0x51, 0x00, 0x71, 0x58])
    end = items + 17 * struct.unpack_from("<H", data, at + 8)[0]
    data[end:end] = bytes([0x4A, 0, 0, 0xFF, 0, 0, 0, 0, 0])
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_stats")
    verdict = checklib.Verdict()
    saves = work / "saves"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(dqk_lib.ASSETS / "SAVE", saves)
    path = saves / "SAVGAMA.QSV"
    path.write_bytes(hand_made(path.read_bytes()))
    own = work / "port-saves"
    if own.exists():
        shutil.rmtree(own)
    shutil.copytree(saves, own)
    keys = "\\k\\ePLTAsa"
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--saves", str(own), "--start-menu",
                             "--keys", keys, str(dqk_lib.ASSETS)], capture_output=True,
                            text=True, cwd=ROOT, timeout=300)
    (work / "port.txt").write_text(result.stdout + result.stderr)
    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                    saves=[saves], timeout=900) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        dqk_lib.escape_title(box)
        dqk_train.freeze_cycles(box)
        for key in "PLTAsa":
            box.keys(key)
            box.settled(tolerance=64)
        box.settle(1.0)
        original = (box.game / "SAVE" / "SAVGAMA.QSV").read_bytes()
    port = (own / "SAVGAMA.QSV").read_bytes()
    mask = dqk_train.pointer_mask(port)
    differing = [i for i in range(min(len(original), len(port)))
                 if i not in mask and original[i] != port[i]]
    verdict.check(len(original) == len(port) and not differing,
                  "the saved game written (SAVGAMA.QSV)",
                  f"{len(original)} and {len(port)} bytes, differing at "
                  f"{[hex(i) for i in differing[:12]]}")
    record = port[0x40F:0x40F + 0x19E]
    print(f"the first member, the port's: AC {record[0x18E]}, behind {record[0x18F]}, "
          f"movement {record[0x199]}")
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
