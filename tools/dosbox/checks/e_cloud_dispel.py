#!/usr/bin/env python3
"""Check #7: Dispel Magic at a cloud in combat (README, Checks against the
original: "Dispel Magic at a cloud in combat: where it takes the cloud away
the port stops").

5b04:3352 walks the clouds (DS:7111) at each green-cloud cell around the
spell's; a cloud dispelled has its caster's effect 0x28 removed, whose end
(3f44:0ff7) frees the node; the walk then reads the freed node's next
(5b04:354a), where FreeMem wrote the block's size: 0002:0000, and goes on
through the interrupt vectors. The port stops there.

Game A's files patched: SIRRION memorizes two Stinking Clouds, KAL two
Dispel Magics (the cleric's). The caravan's baaz, seed 18 (found with
eclplay): SIRRION's turn is the first: a cloud three cells north of him
(27,11); KAL begins casting; GARIN and ISTAN HORBIN quit their turns; KAL's
Dispel Magic at 28,12 (a cell of the cloud) takes it away. The original is
then watched: the screens, whether it waits for keys again, where, the
cloud list and the heap's free list, the interrupt table and BIOS data,
RandSeed.

Camp (the README's second part: outside combat the walk reads the cells
from the interrupt table, the map pointer DS:6a2e NULL; the port takes
none of them to be a green cloud): KAL's Dispel Magic from camp's Magic
in the outpost inn's camp, on the first character; the screen after
compared with the port's (the combat map should not flash over the view).
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

SEED = 18
TO_BATTLE = E.LOAD.format("A") + r"\r\r\r\r\r\r\rLnm9\r"
CLOUD = r"C\v\v\v\v\v\v\v\rM\^\^\^T"       # SIRRION: Stinking Cloud, 3 north
BEGIN = r"C\v\v\v\v\v\v\v\v\v\r"           # KAL: Dispel Magic, Begins Casting
OTHERS = "DQDQ"                             # GARIN, ISTAN HORBIN: Done, Quit
AIM = r"M\<\^\^"                            # KAL's cast: Manual, to 28,12
CAMP = r"\r\r\r\r\r\r\rI\r"                 # game A's start, the outpost inn's camp
CAMP_DISPEL = r"M\v\v\v\vC\v\v\v\v\v\v\v\v\v\r"  # Magic, KAL, Cast, Dispel Magic
CLOUDS = 0x7111


def patched(work):
    d = E.patched_saves(work / "saves", {})
    for f, ids in (("CHRDATA6.SAV", [0x22, 0x22]), ("CHRDATA5.SAV", [0x29, 0x29])):
        raw = bytearray((d / f).read_bytes())
        empty = [j for j in range(58) if raw[0x1E + j] == 0][:len(ids)]
        for j, i in zip(empty, ids):
            raw[0x1E + j] = i
        (d / f).write_bytes(raw)
    return d


def clouds(box):
    """The cloud list: (seg:off, its 30 bytes) each, at most 6."""
    out = []
    p = box.far(box.ds * 16 + CLOUDS)
    while p is not None and len(out) < 6:
        out.append(("%05x" % p, box.read(p, 30).hex()))
        p = box.far(p + 4)
    return out


STEP_X = [0, 1, 1, 1, 0, -1, -1, -1, 0]   # DS:1ed6, 1edf
STEP_Y = [-1, -1, 0, 1, 1, 1, 0, -1, 0]
CELL_DIRS = [8, 2, 3, 4]                   # DS:2092-2095, cells 1-4


def walk_chain(mem, start, x, y, limit=100000):
    """5b04:3416-3555's walk from linear START over memory MEM (1 MB) for
    the cell X, Y: the nodes visited (linear), those with a cell at X, Y and
    +0x1d 0 (which the original tries with a d100, writing +0x1d on a
    failure), and how it ends (NULL, or a node met again: for ever)."""
    def s8(b):
        return b - 256 if b > 127 else b
    seen, tried, p = {}, [], start
    while p is not None and len(seen) < limit:
        if p in seen:
            return list(seen), tried, "a node met again: the walk goes round for ever"
        seen[p] = True
        node = mem[p:p + 0x20] if p + 0x20 <= len(mem) else bytes(0x20)
        for k, d in enumerate(CELL_DIRS, 1):
            if s8(node[0x1A]) + STEP_X[d] == x and s8(node[0x1B]) + STEP_Y[d] == y \
                    and node[0x1D] == 0:
                tried.append((p, k))
        off, seg = struct.unpack_from("<HH", node, 4)
        p = None if off == seg == 0 else (seg * 16 + off) & 0xFFFFF
    return list(seen), tried, "NULL" if p is None else f"stopped after {limit}"


def battle(work, saves, verbose):
    keys = TO_BATTLE + CLOUD + BEGIN + OTHERS + AIM + "T"
    pout, _, pshots = cokbox.eclplay(keys, work / "port-battle", saves=[saves],
                                     args=["--seed", str(SEED)])
    lines = pout.splitlines()
    print("port:", " / ".join(lines[-4:-1]))
    expected = [cokbox.Screen.load(p) for p in pshots]
    with E.Box(workdir=work / "box-battle", saves=[saves], seed=SEED, exit_after=False,
               verbose=verbose, timeout=900) as box:
        box.keys(TO_BATTLE + CLOUD + BEGIN + OTHERS + AIM)
        box.wait_input(settle=0.7)
        shown = box.screenshot(work / "battle-aimed.png")
        best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
        print(f"original, KAL's aim at 28,12: {shown.diff(expected[best])[0]} pixels differ "
              f"from eclplay's {pshots[best].name}")
        before = clouds(box)
        print("  clouds:", before)
        print("  heap:", E.heap(box))
        low = E.low_memory(box)
        seed0 = box.ds_dword(0x43AE)
        hook0 = box.hook_state()
        box.key("T")
        seen = E.watch(box, 8, work, "battle-after")
        took, where = E.takes_keys(box, 60)
        print(f"original after Target: {len(seen)} different screens in 8 s; "
              f"{'waits at ' + ', '.join(where) if took else 'no wait: ' + where}; "
              f"{'running' if box.running else 'ended'}")
        hook1 = box.hook_state()
        print(f"  key reads {hook0['reads']} -> {hook1['reads']}, polls {hook0['polls']} -> "
              f"{hook1['polls']}; RandSeed {seed0:#010x} -> {box.ds_dword(0x43AE):#010x}")
        print("  clouds:", clouds(box))
        print("  heap:", E.heap(box))
        print("  low memory changed:", ", ".join(E.low_changes(low, E.low_memory(box)))
              or "nothing")
        box.screenshot(work / "battle-after.png")
        if before:
            node = int(before[0][0], 16)
            mem = box.read(0, 0x100000)
            (work / "memory-after.bin").write_bytes(mem)
            nxt = struct.unpack_from("<HH", mem, node + 4)
            print(f"  the freed node {node:05x}: {mem[node:node + 30].hex()}; its next "
                  f"{nxt[1]:04x}:{nxt[0]:04x}")
            chain, tried, end = walk_chain(mem, nxt[1] * 16 + nxt[0], 28, 12)
            print(f"  the walk from there (memory as it is after): {len(chain)} nodes, "
                  f"{' '.join('%05x' % a for a in chain[:24])}{' ...' if len(chain) > 24 else ''};"
                  f" ends at {end}; nodes with a cell at 28,12 and +0x1d 0: "
                  f"{[('%05x' % a, k) for a, k in tried]}")
        if took:
            # Does the battle go on? The party on Auto to its end.
            turns = 0
            end = time.monotonic() + 240
            while time.monotonic() < end and box.running:
                took, where = E.takes_keys(box, 30)
                if not took or "3995:0b01" not in where:
                    break
                box.keys(r"\@q")
                turns += 1
            box.screenshot(work / "battle-end.png")
            print(f"  then Alt-Q ({turns}): {'waits at ' + ', '.join(where) if took else where}")
            print("  heap:", E.heap(box))
            print("  low memory changed since the aim:",
                  ", ".join(E.low_changes(low, E.low_memory(box))) or "nothing")


def camp(work, saves, verbose):
    keys = E.LOAD.format("A") + CAMP + CAMP_DISPEL
    pout, _, pshots = cokbox.eclplay(keys + "S", work / "port-camp", saves=[saves])
    print("port, camp:", " / ".join(pout.splitlines()[-5:-1]))
    expected = [cokbox.Screen.load(p) for p in pshots]
    with E.Box(workdir=work / "box-camp", saves=[saves], verbose=verbose, timeout=900) as box:
        box.keys(keys)
        box.wait_input(settle=0.7)
        low = E.low_memory(box)
        # The cells 5b04:3352 reads with the map pointer NULL: 0000:(7 + 50y
        # + x) for the spell's cell (DS:710f, 7110) and its eight
        # neighbours, those off the 50 x 25 map reading as 0.
        x0, y0 = (b - 256 if b > 127 else b for b in box.ds_read(0x710F, 2))
        mapptr = box.ds_read(0x6A2E, 4).hex()
        cells = []
        for dx, dy in ((0, 0), (0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0),
                       (-1, -1)):
            x, y = x0 + dx, y0 + dy
            on = 0 <= x <= 0x31 and 0 <= y <= 0x18
            cells.append((x, y, "%02x" % low[7 + 50 * y + x] if on else "off"))
        print(f"original, camp: map pointer DS:6a2e {mapptr}, spell's cell {x0},{y0}; the "
              f"terrain read at it and around it: {cells} (0x1e would be a green cloud)")
        box.key("S")
        seen = E.watch(box, 4, work, "camp-after")
        took, where = E.takes_keys(box, 30)
        counts = []
        for i, shot in enumerate(seen):
            best = min(range(len(expected)), key=lambda k: shot.diff(expected[k])[0])
            counts.append((shot.diff(expected[best])[0], pshots[best].name))
        print(f"original, camp: {len(seen)} different screens after Select, each against "
              f"eclplay's nearest: {counts}; "
              f"{'waits at ' + ', '.join(where) if took else where}")
        print("  low memory changed:", ", ".join(E.low_changes(low, E.low_memory(box)))
              or "nothing")
        return all(c == 0 for c, _ in counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", choices=["battle", "camp"], action="append")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-cloud-")).resolve()
    if not (E.SAVE / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    saves = patched(work)
    status = 0
    if "battle" in (args.part or ["battle"]):
        battle(work, saves, args.verbose)
    if "camp" in (args.part or ["camp"]):
        if not camp(work, saves, args.verbose):
            status = 1
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())
