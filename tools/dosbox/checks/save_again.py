#!/usr/bin/env python3
"""Check: load saved game A, save it again (DEVELOPERS.md, Checks against the
original: "Load saved game A, save it again, and see whether KAL and SIRRION
now know spell 8").

The original in DOSBox-X and eclplay both load SAVE/'s game A from the start
menu and save it as A again; the files are compared byte for byte, leaving
out what DEVELOPERS.md says the original writes from memory (Camp, Save:
the far pointers in records, items and effects; in the .DAT the stack's
bytes after each name and in the slots past the party). Spell 8 is `+0x6a` of
a character's .SAV: KAL and SIRRION must know it in the original's files and
the port's.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402

cokbox = checklib.cokbox

KEYS = r"\kxP\v\v\rA\v\v\v\v\rA"
# Bytes of a 409-byte record the original writes from memory (Camp, Save).
SAV_POINTERS = [range(0xE3, 0xE7), range(0x143, 0x147), range(0x147, 0x17B),
                range(0x17F, 0x183), range(0x183, 0x187)]


NAMES = 0x1415  # SAVGAM?.DAT: the party's size, then eight string[40] names


def masked(name, data):
    """DATA with the bytes the original writes from memory zeroed."""
    data = bytearray(data)
    if name.endswith(".SAV"):
        spans = SAV_POINTERS
    elif name.endswith(".STF"):  # items, 63 bytes each, a pointer at +0x2a
        spans = [range(i + 0x2A, i + 0x2E) for i in range(0, len(data), 63)]
    elif name.endswith(".SFX"):  # effects, 9 bytes each, a pointer at +5
        spans = [range(i + 5, i + 9) for i in range(0, len(data), 9)]
    elif name.endswith(".DAT"):  # past each name, and the slots past the party
        size = data[NAMES - 1]
        spans = []
        for k in range(8):
            slot = NAMES + 41 * k
            keep = 1 + min(data[slot], 40) if k < size else 0
            spans.append(range(slot + keep, slot + 41))
    else:
        spans = []
    for r in spans:
        for i in r:
            if i < len(data):
                data[i] = 0
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", default="tandy")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    import b_lib as L   # its masked() is this file's
    work = checklib.workdir(args.workdir, "save-again")
    saves = cokbox.ROOT / "SAVE"
    v = checklib.Verdict()
    stdout, port_dir, _ = cokbox.eclplay(KEYS, work / "eclplay", machine=args.machine,
                                         saves=[saves], shots=False)
    with cokbox.Box(workdir=work / "box", machine=args.machine, saves=[saves],
                    verbose=args.verbose) as box:
        box.keys(KEYS + r"\sfinal.", shots=work)
        # Saving writes the files before the menu waits again.
        orig_dir = L.keep_saves(box, work / "orig-saves")
    before = {p.name: p.read_bytes() for p in saves.iterdir() if p.is_file()}
    for who, n in (("KAL", 5), ("SIRRION", 6)):
        name = f"CHRDATA{n}.SAV"
        orig = (orig_dir / name).read_bytes()[0x6A]
        print(f"{who} ({name}) +0x6a: SAVE/ {before[name][0x6a]:#04x}")
        v.check(orig != 0, f"{who} knows spell 8 in the original's game saved again",
                f"+0x6a {orig:#04x}")
        v.value(f"{who}'s +0x6a saved", orig, (port_dir / name).read_bytes()[0x6A])
    L.judge_games(v, "saved A", orig_dir, "A", port_dir, "A")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
