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
RandSeed. The port stops there; the original's recorded behaviour is
asserted instead: the aim's screen as the port's, then after Target the
game waiting at the next player's combat menu, RandSeed advanced by the
dispel's own d100 alone (as the port's at its stop), the cloud gone with
SIRRION's 0x28 (as the port's), low memory unchanged but the BIOS's own
counters, the heap's free list unchanged (the node, at the heap's top,
merged into HeapPtr), and the battle going on to its end on Auto.

Camp (the README's second part: outside combat the walk reads the cells
from the interrupt table, the map pointer DS:6a2e NULL; the port takes
none of them to be a green cloud): KAL's Dispel Magic from camp's Magic
in the outpost inn's camp, on the first character: the map pointer NULL
and none of the cells read a green cloud (recorded), low memory unchanged,
the screens after Select in the port's order (its frames at each delay
and key read) and the one at the next key read as the port's.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
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
MENU = "3995:0b01"          # a player's combat menu
STOP = ("Dispel Magic reads the next cloud from the one it freed, where FreeMem wrote the "
        "block's size (1a46:03fc)")
GREEN = 0x1E                # a green cloud's terrain


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


def battle(v, work, saves, verbose):
    kw = dict(saves=[saves], args=["--seed", str(SEED)])
    keys = TO_BATTLE + CLOUD + BEGIN + OTHERS + AIM
    pair = E.Pair(keys + r"\saimed.", work / "battle", **kw)
    paused = E.eclplay(pauses=True)
    _, _, upto = E.run_port(keys, work / "port-battle-0", paused, **kw)
    stop, _, frames = E.run_port(keys + "T", work / "port-battle", paused, **kw)
    # The port's frames from the aim's key read to its stop (its delays').
    frames = [cokbox.Screen.load(p) for p in frames[len(upto) - 1:]]
    print("port after Target:", " / ".join(stop.splitlines()[-4:]))
    v.check((E.port_error(stop) or "").startswith(STOP), "battle: the port stops at the walk",
            E.port_error(stop) or "no stop")
    with cokbox.Box(workdir=work / "box-battle", saves=[saves], seed=SEED, exit_after=False,
                    verbose=verbose, timeout=900) as box:
        box.keys(keys)
        box.wait_input(settle=0.7)
        aimed = box.screenshot(work / "battle-aimed.png")
        pair.judge(v, "aimed", aimed, "battle: KAL's Dispel Magic aimed at 28,12")
        before = clouds(box)
        heap0 = E.heap(box)
        print("  clouds:", before)
        print("  heap:", E.heap_text(heap0))
        low = E.low_memory(box)
        seed0 = box.ds_dword(E.RANDSEED)
        box.key("T")
        seen = checklib.watch(box, 8, work, "battle-after")
        took, where = E.takes_keys(box, 60)
        print(f"original after Target: {len(seen)} different screens in 8 s; "
              f"{'waits at ' + ', '.join(where) if took else 'no wait: ' + where}")
        # The screens up to the port's last frame (The air clears a
        # little...) in its order; those after, the next turn, the port
        # does not reach.
        last = next((i for i, s in enumerate(seen) if s.pixels == frames[-1].pixels), None)
        v.check(last is not None, "battle: the original shows the port's last frame before "
                "its stop (The air clears a little...)", f"{len(seen)} screens")
        if last is not None:
            # From the aim's screen (the port's frame 0) on.
            E.in_order(v, "battle: the screens after Target to there, in the port's order",
                       [aimed] + seen[:last + 1], frames, work=work)
        print("  the port is not compared past its stop: the original's recorded behaviour")
        v.check(took and MENU in where, "battle: the game waits at the next player's combat "
                "menu (recorded)", ", ".join(where) if took else where)
        seed1 = box.ds_dword(E.RANDSEED)
        v.value("battle: RandSeed after Target (the port's at its stop)", seed1,
                E.port_seed(stop))
        v.check(seed1 == E.next_seed(seed0), "battle: RandSeed advanced by one Random, the "
                "dispel's d100 (recorded)", f"{seed0:#010x} -> {seed1:#010x}")
        after = clouds(box)
        sirrion = [p for p in E.party(box) if p[0] == "SIRRION"]
        v.check(before and not after, "battle: the cloud gone (as the port's)",
                f"before {before}, after {after}")
        v.check(len(sirrion) == 1 and all(e[0] != 0x28 for e in sirrion[0][5]),
                "battle: SIRRION's 0x28 removed (the port's `SIRRION loses 0x28`)",
                str(sirrion))
        changed = E.low_changes(low, E.low_memory(box))
        v.check(not changed, "battle: low memory unchanged (but the BIOS's own counters; "
                "recorded)", ", ".join(changed))
        heap1 = E.heap(box)
        print("  heap:", E.heap_text(heap1))
        node = int(before[0][0], 16) if before else None
        top = int(heap1[1][:4], 16) * 16 + int(heap1[1][5:], 16)
        v.check(heap1[2] == heap0[2] and top == node,
                "battle: the free list unchanged, the freed node merged into HeapPtr (recorded)",
                f"before {E.heap_text(heap0)}, after {E.heap_text(heap1)}")
        if node is not None:
            # A diagnostic only: memory as it is now, not as the walk read it
            # (the area above HeapPtr is used again in between).
            mem = box.read(0, 0x100000)
            (work / "memory-after.bin").write_bytes(mem)
            nxt = struct.unpack_from("<HH", mem, node + 4)
            chain, tried, end = walk_chain(mem, nxt[1] * 16 + nxt[0], 28, 12)
            print(f"  (diagnostic) the freed node {node:05x} now: {mem[node:node + 30].hex()}; "
                  f"a walk from its next {nxt[1]:04x}:{nxt[0]:04x} now: {len(chain)} nodes, "
                  f"ends at {end}; nodes with a cell at 28,12 and +0x1d 0: "
                  f"{[('%05x' % a, k) for a, k in tried]}")
        turns, where = E.to_battle_end(box)
        box.screenshot(work / "battle-end.png")
        print(f"  then Alt-Q ({turns}): waits at {where}")
        v.check(E.BATTLE_OVER in where, "battle: the battle goes on to its end (Alt-Q, the "
                "party on Auto, to 3995:0c9c's menu; recorded)", f"waits at {where}")


def camp(v, work, saves, verbose):
    keys = E.LOAD.format("A") + CAMP + CAMP_DISPEL
    pair = E.Pair(keys + r"\scast." + r"S\safter.", work / "camp", saves=[saves])
    paused = E.eclplay(pauses=True)
    _, _, upto = E.run_port(keys, work / "port-camp-0", paused, saves=[saves])
    _, _, frames = E.run_port(keys + "S", work / "port-camp", paused, saves=[saves])
    # The port's frames from the read before Select to the read after it.
    frames = [cokbox.Screen.load(p) for p in frames[len(upto) - 1:]]
    with cokbox.Box(workdir=work / "box-camp", saves=[saves], verbose=verbose,
                    timeout=900) as box:
        box.keys(keys)
        box.wait_input(settle=0.7)
        asked = box.screenshot(work / "camp-cast.png")
        pair.judge(v, "cast", asked, "camp: Dispel Magic's target asked")
        low = E.low_memory(box)
        # The cells 5b04:3352 reads with the map pointer NULL: 0000:(7 + 50y
        # + x) for the spell's cell (DS:710f, 7110) and its eight
        # neighbours, those off the 50 x 25 map reading as 0.
        x0, y0 = (b - 256 if b > 127 else b for b in box.ds_read(0x710F, 2))
        mapptr = box.ds_read(0x6A2E, 4)
        cells = []
        for dx, dy in ((0, 0), (0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0),
                       (-1, -1)):
            x, y = x0 + dx, y0 + dy
            on = 0 <= x <= 0x31 and 0 <= y <= 0x18
            cells.append((x, y, low[7 + 50 * y + x] if on else None))
        print(f"original, camp: map pointer DS:6a2e {mapptr.hex()}, spell's cell {x0},{y0}; "
              f"the terrain read at it and around it: {cells}")
        v.check(mapptr == bytes(4), "camp: the map pointer NULL (recorded)", mapptr.hex())
        v.check(all(c != GREEN for _, _, c in cells), "camp: no cell read is a green cloud "
                "(0x1e; recorded: the walk does nothing, as the port)", str(cells))
        box.key("S")
        seen = checklib.watch(box, 4, work, "camp-after")
        took, where = E.takes_keys(box, 30)
        v.check(took, "camp: the game waits for a key after Select",
                ", ".join(where) if took else where)
        box.wait_input(settle=0.5)
        last = box.screenshot(work / "camp-after.png")
        # The spell list is drawn again after the cast: its window (columns
        # 1-38, rows 5-22) cleared, then each row (menu.c's draw_list), with
        # no delay between in the port: a screen caught in between.
        E.in_order(v, "camp: the screens after Select, in the port's order",
                   [asked] + seen + [last], frames, cleared=[(8, 40, 304, 144)], work=work)
        pair.judge(v, "after", last, "camp: the screen at the next key read")
        changed = E.low_changes(low, E.low_memory(box))
        v.check(not changed, "camp: low memory unchanged (but the BIOS's own counters)",
                ", ".join(changed))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", choices=["battle", "camp"], action="append")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-cloud")
    checklib.need_saves()
    v = checklib.Verdict()
    saves = patched(work)
    if "battle" in (args.part or ["battle"]):
        battle(v, work, saves, args.verbose)
    if "camp" in (args.part or ["camp"]):
        camp(v, work, saves, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
