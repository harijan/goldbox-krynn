#!/usr/bin/env python3
"""Check: load saved game A, save it again (README, Checks against the
original: "Load saved game A, save it again, and see whether KAL and SIRRION
now know spell 8").

The original in DOSBox-X and eclplay both load SAVE/'s game A from the start
menu and save it as A again; the files are compared byte for byte, leaving
out what the README says the original writes from memory (Camp, Save:
the far pointers in records, items and effects; in the .DAT the stack's
bytes after each name and in the slots past the party). Spell 8 is `+0x6a` of
a character's .SAV.
"""
import argparse
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

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
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-save-again-"))
    saves = cokbox.ROOT / "SAVE"
    if not (saves / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    stdout, port_dir, _ = cokbox.eclplay(KEYS, work / "eclplay", machine=args.machine,
                                         saves=[saves], shots=False)
    with cokbox.Box(workdir=work / "box", machine=args.machine, saves=[saves],
                    verbose=args.verbose) as box:
        box.keys(KEYS + r"\sfinal.", shots=work)
        # Saving writes the files before the menu waits again.
        orig_dir = box.game / "SAVE"
        files = {p.name: p.read_bytes() for p in orig_dir.iterdir()}
    before = {p.name: p.read_bytes() for p in saves.iterdir() if p.is_file()}
    port = {p.name: p.read_bytes() for p in port_dir.iterdir()}
    learnt = True
    for who, n in (("KAL", 5), ("SIRRION", 6)):
        name = f"CHRDATA{n}.SAV"
        learnt = learnt and files[name][0x6A] != 0
        print(f"{who} ({name}) +0x6a: SAVE/ {before[name][0x6a]:#04x}, "
              f"original saved {files[name][0x6a]:#04x}, port saved {port[name][0x6a]:#04x}")
    same = differ = 0
    for name in sorted(set(files) | set(port)):
        if name not in files or name not in port:
            print(f"{name}: only in {'the original' if name in files else 'the port'}'s")
            differ += 1
            continue
        a, b = masked(name, files[name]), masked(name, port[name])
        if a == b:
            same += 1
            continue
        differ += 1
        offs = [i for i in range(max(len(a), len(b)))
                if i >= len(a) or i >= len(b) or a[i] != b[i]]
        print(f"{name}: {len(offs)} bytes differ (sizes {len(a)}, {len(b)}) at "
              + " ".join(f"{i:#x}" for i in offs[:24]) + (" ..." if len(offs) > 24 else ""))
    print(f"{same} files the same (pointers masked), {differ} differ; files in {work}")
    return 0 if learnt and differ == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
