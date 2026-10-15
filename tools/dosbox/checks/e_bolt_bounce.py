#!/usr/bin/env python3
"""Check #11: a bolt bouncing for ever between two walls of a 3D area
(README, Checks against the original: "A blue dragon's lightning between
two walls of a 3D area as far apart as its line before the bounce: the
original should bounce for ever (the port stops)").

No natural case was found: the port, searched for its stop ("a bolt
bounces between walls for ever"), never met one with the blue dragons of
ECL3 block 82 (their square 2,10, game A's party and made-up ones, every
facing, 500 seeds; every square, 3 seeds), with every party member given
the lightning breath (0x55) on Auto at Throtl's gate (600 seeds), nor with
Lightning Bolt aimed at every cell from the casters' places at the gate.
So the geometry is made: 5b04:3a6f (the port's bolt() in src/cast.c,
modelled in e_boltmodel.py) bounces for ever from a caster at 31,16 aimed at 26,18
with walls at 25,18 and 27,17 and floor elsewhere: line 1 hits 25,18 (the
line back is longer than 8: no surcharge), line 2 from there toward the
caster hits 27,17 at the same length, line 3 from there away from the
caster hits 25,18 at the same length, each charged nothing.

The original: game A started with Helm, seed 7, the caravan won with the
Gods, then Throtl's gate (as batch D's d_gate.py); game A's files patched
so that KAL knows Lightning Bolt. At KAL's turn (MOLLY, ISTAN HORBIN and
SIRRION quit theirs) the combat map (DS:6a2e) is rewritten: floor (0x17)
everywhere but walls (0x01) at 25,18 and 27,17; KAL
is put at 31,16 (its entry DS:623f and the grid DS:63f3; moving there would
draw attacks from the warrior and hobgoblin beside him) and casts Lightning
Bolt with Manual at 26,18. Then:
whether it ever waits for a key again, the screens, RandSeed, the hook's
counters.
"""
import argparse
import pathlib
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import e_boltmodel  # noqa: E402
import e_common as E  # noqa: E402
cokbox = E.cokbox

SEED = 7
ROUTE = (E.LOAD.format("A") + r"\r\r\r\r\r\r\rLnm9\r" + r"\@x\r\rEN\r\rY\r\r" + r"Lnm98yA")
TO_KAL = "DQDQDQ"                     # MOLLY, ISTAN HORBIN, SIRRION: Done, Quit
KAL, AT = 5, (31, 16)                 # KAL's entry, put at 31,16
CAST = "C" + r"\v" * 9 + r"\r"        # Lightning Bolt, KAL's ninth spell
AIM = r"M\<\<\<\<\<\v\v"              # Manual: 5 west, 2 south: 26,18
WALLS = [(25, 18), (27, 17)]
FLOOR, WALL = 0x17, 0x01
MAP, CELLS, OCCUPANT = 0x6A2E, 0x623F, 0x63F3   # DS:63f3 + 50y + x: who is there


def patched(work):
    d = E.patched_saves(work / "saves", {})
    raw = bytearray((d / "CHRDATA5.SAV").read_bytes())
    empty = [j for j in range(58) if raw[0x1E + j] == 0][:2]
    for j in empty:
        raw[0x1E + j] = 0x33
    (d / "CHRDATA5.SAV").write_bytes(raw)
    return d


def cell_of(box, n):
    return tuple(box.ds_read(CELLS + 4 * n, 2))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--watch", type=float, default=90, help="seconds to watch after Target")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-bolt-")).resolve()
    if not (E.SAVE / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    trace = []
    print("model:", e_boltmodel.bolt(set(WALLS), AT, (26, 18), 7, True, trace), trace[:4])
    saves = patched(work)
    # The port, on the map as it is (no rewrite): where the same keys lead.
    pout, _, pshots = cokbox.eclplay(ROUTE + TO_KAL, work / "port", saves=[saves],
                                     args=["--helm", "--seed", str(SEED)])
    turn = [line for line in pout.splitlines() if line.startswith("turn:")][-1]
    print("port:", turn)
    expected = [cokbox.Screen.load(p) for p in pshots]
    with E.Box(workdir=work / "box", saves=[saves], args="x Helm", seed=SEED,
               exit_after=False, verbose=args.verbose, timeout=1200) as box:
        box.keys(ROUTE + TO_KAL)
        box.wait_input(settle=0.7)
        shown = box.screenshot(work / "kal-turn.png")
        best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
        print(f"original at KAL's turn: {shown.diff(expected[best])[0]} pixels differ from "
              f"eclplay's {pshots[best].name}; KAL at {cell_of(box, 5)}")
        m = box.far(box.ds * 16 + MAP)
        grid = bytearray([FLOOR] * (50 * 25))
        for x, y in WALLS:
            grid[y * 50 + x] = WALL
        box.write(m + 7, grid)
        old = cell_of(box, KAL)
        box.ds_write(OCCUPANT + 50 * old[1] + old[0], b"\0")
        box.ds_write(OCCUPANT + 50 * AT[1] + AT[0], bytes([KAL]))
        box.ds_write(CELLS + 4 * KAL, bytes(AT))
        print(f"  map rewritten; KAL put at {cell_of(box, KAL)} (entry and DS:63f3's grid)")
        box.keys(CAST + AIM)
        box.wait_input(settle=0.7)
        box.screenshot(work / "aimed.png")
        seed0, hook0 = box.ds_dword(0x43AE), box.hook_state()
        print(f"  aimed at 26,18 (screen aimed.png); RandSeed {seed0:#010x}")
        box.key("T")
        start = time.monotonic()
        seen = E.watch(box, 10, work, "bolt")
        took, where = E.takes_keys(box, max(1, args.watch - 10))
        hook1 = box.hook_state()
        print(f"original after Target: {len(seen)} different screens in the first 10 s; "
              f"{'waits at ' + ', '.join(where) if took else 'no key wait'} within "
              f"{time.monotonic() - start:.0f} s; {'running' if box.running else 'ended'}; "
              f"RandSeed {box.ds_dword(0x43AE):#010x}; key reads {hook0['reads']} -> "
              f"{hook1['reads']}, polls {hook0['polls']} -> {hook1['polls']}")
        later = E.watch(box, 5, work, "bolt-later")
        print(f"  {len(later)} different screens in 5 s more")
        samples = []
        for _ in range(4):
            st = box.hook_state()
            site = box.locate(st["pollcs"], st["pollip"])
            samples.append((f"{box.ds_dword(0x43AE):#010x}", st["polls"],
                            "%04x:%04x" % site if site else "?"))
            time.sleep(2)
        print("  RandSeed, KeyPressed's count and its last caller, 2 s apart:", samples)
    print(f"files in {work}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
