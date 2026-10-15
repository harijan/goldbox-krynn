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
modelled in e_boltmodel.py) bounces for ever from a caster at 31,16 aimed
at 26,18 with walls at 25,18 and 27,17 and floor elsewhere: line 1 hits
25,18 (the line back is longer than 8: no surcharge), line 2 from there
toward the caster hits 27,17 at the same length, line 3 from there away
from the caster hits 25,18 at the same length, each charged nothing.

Game A started with Helm, seed 7, the caravan won with the Gods, then
Throtl's gate (as batch D's d_gate.py); game A's files patched so that KAL
knows Lightning Bolt. At KAL's turn (MOLLY, ISTAN HORBIN and SIRRION quit
theirs; the screen against the port's there) the combat map is rewritten
in both, the original's memory (DS:6a2e + 7) and the port's (a poke of
eclplay at the same key read): floor (0x17) everywhere but walls (0x01) at
25,18 and 27,17, and KAL put at 31,16 (his entry and the grid of who
stands where). Lightning Bolt, Manual at 26,18: the aim's screen against
the port's. Then Target: the port must stop ("bounces for ever"), with
RandSeed where the original's stays. The original's recorded behaviour is
asserted, the port not being compared past its stop: no key wait again
(watched), DOSBox-X running, KeyPressed's count frozen, RandSeed constant,
and the bolt animating: its screens the frames the port drew before it
stopped, in order, round the loop the port found again and again.
"""
import argparse
import sys
import time
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
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
VIEW = (8, 8, 168, 168)               # the combat view: 7 x 7 cells of 24 pixels
# The same rewrite in the port (src/combat.h's cok_combat).
POKE = ("memset(game->combat.cells, 0x17, sizeof game->combat.cells); "
        "game->combat.cells[18][25] = 0x01; game->combat.cells[17][27] = 0x01; "
        "cok_combatant *k = &game->combat.combatant[5]; "
        "game->combat.occupant[k->y][k->x] = 0; k->x = 31; k->y = 16; "
        "game->combat.occupant[16][31] = 5; "
        "printf(\"e: combatant 5 is %.*s\\n\", k->character->record[0], "
        "(const char *)k->character->record + 1);")
STOP = "a bolt bounces between walls for ever, and the original with it (5b04:3aea)"
# The original's recorded behaviour: screens in the first 10 s and the 5 s
# after (146 and 74 in batch E, 34 and 19 under a full run's load).
FIRST, LATER = 5, 2


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
    work = checklib.workdir(args.workdir, "e-bolt")
    checklib.need_saves()
    v = checklib.Verdict()
    trace = []
    model = e_boltmodel.bolt(set(WALLS), AT, (26, 18), 7, True, trace)
    print("model of 5b04:3a6f:", model, trace[:4])
    saves = patched(work)
    kw = dict(saves=[saves], args=["--helm", "--seed", str(SEED)])
    # The port's shot number at KAL's turn, after which the poke goes, in
    # the eclplay with a shot at each delay (the bolt's frames) and without.
    paused = E.eclplay(poke=POKE, pauses=True)
    _, _, shots = E.run_port(ROUTE + TO_KAL, work / "port-kal-paused", paused, **kw)
    E.poke_at(len(shots))
    stop, _, frames = E.run_port(ROUTE + TO_KAL + CAST + AIM + "T", work / "port-bolt", paused,
                                 **kw)
    _, _, before = E.run_port(ROUTE + TO_KAL + CAST + AIM, work / "port-bolt-0", paused, **kw)
    # From the aim's key read to the stop: the view centred, line 1, then
    # the loop the port found (a period of frames it drew twice).
    frames = [cokbox.Screen.load(p) for p in frames[len(before) - 1:]]
    loop = E.loop_of(frames)
    v.check(loop is not None, "the port's frames end in a loop", f"{len(frames)} frames")
    _, _, shots = E.run_port(ROUTE + TO_KAL, work / "port-kal", E.eclplay(), **kw)
    E.poke_at(len(shots))   # for the Pair's runs
    keys = ROUTE + TO_KAL + r"\skal." + CAST + AIM + r"\saimed."
    pair = E.Pair(keys, work / "pair", poke=POKE, **kw)
    print("port at the aim:", [line for line in pair.text("aimed").splitlines()
                               if line.startswith("e: ")])
    v.check("e: combatant 5 is KAL" in pair.text("aimed"), "the port's combatant 5 is KAL")
    v.check(E.port_error(stop) == STOP, "the port stops at the bolt's loop",
            E.port_error(stop) or "no stop")
    print(f"  the port drew {len(frames) - 1} frames (its delays) after Target before it "
          f"stopped: {loop and loop[0]} before its loop of {loop and loop[1]}")
    with cokbox.Box(workdir=work / "box", saves=[saves], args="x Helm", seed=SEED,
                    exit_after=False, verbose=args.verbose, timeout=1200) as box:
        box.keys(ROUTE + TO_KAL)
        box.wait_input(settle=0.7)
        pair.judge(v, "kal", box.screenshot(work / "kal-turn.png"), "KAL's turn")
        print(f"  original: KAL at {cell_of(box, KAL)}")
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
        pair.judge(v, "aimed", box.screenshot(work / "aimed.png"), "Lightning Bolt aimed at 26,18")
        seed0, hook0 = box.ds_dword(E.RANDSEED), box.hook_state()
        box.key("T")
        start = time.monotonic()
        seen = checklib.watch(box, 10, work, "bolt")
        took, where = E.takes_keys(box, max(1, args.watch - 10))
        later = checklib.watch(box, 5, work, "bolt-later")
        hook1 = box.hook_state()
        samples = []
        for _ in range(4):
            samples.append((box.ds_dword(E.RANDSEED), box.hook("polls"), box.hook("reads")))
            time.sleep(2)
        running = box.running
    print(f"original after Target: {len(seen)} different screens in the first 10 s, "
          f"{len(later)} in 5 s more; {'waits at ' + ', '.join(where) if took else 'no key wait'}"
          f" within {time.monotonic() - start:.0f} s; RandSeed {seed0:#010x} -> "
          f"{samples[0][0]:#010x}; key reads {hook0['reads']} -> {hook1['reads']}, polls "
          f"{hook0['polls']} -> {hook1['polls']}; 2 s apart: "
          f"{[(f'{s:#010x}', p, r) for s, p, r in samples]}")
    print("  the port is not compared past its stop: the original's recorded behaviour")
    v.check(not took, "the original never waits for a key again (recorded)",
            ", ".join(where) if took else "")
    v.check(running, "DOSBox-X still running (recorded)")
    v.check(hook1["reads"] == hook0["reads"] + 1 and len({p for _, p, _ in samples}) == 1
            and len({r for _, _, r in samples}) == 1,
            "no key read but Target's, KeyPressed's count frozen (recorded)",
            f"reads {hook0['reads']} -> {hook1['reads']}, samples {samples}")
    v.check(len({s for s, _, _ in samples}) == 1, "RandSeed constant (recorded: no one hit)",
            str([hex(s) for s, _, _ in samples]))
    v.value("RandSeed in the loop (the port's at its stop)", samples[0][0], E.port_seed(stop))
    v.check(len(seen) >= FIRST and len(later) >= LATER,
            f"the bolt animating ({FIRST} different screens in 10 s, {LATER} in 5 s more; "
            "recorded)", f"{len(seen)}, {len(later)}")
    if loop is not None:
        start, period = loop
        shots = seen + later
        # The first screen after Target may catch the view being drawn
        # again around the spell's cell (its tiles first, then the
        # combatants on them): compared outside the view's 7 x 7 cells.
        if shots and all(shots[0].pixels != f.pixels for f in frames):
            v.screen("the first screen after Target, outside the view being drawn",
                     shots[0], frames[1], masks=[VIEW + ("the view's 7 x 7 cells, drawn again "
                                                         "(the tiles, then the combatants)",)])
            shots = shots[1:]
        # The rest: the port's frames in order, round its loop again and
        # again (what the original repeats); screens caught as the bolt's
        # picture moved from cell to cell allowed.
        E.in_order(v, "the bolt's screens: the port's frames in order, round its loop", shots,
                   frames[:start + period], loop_from=start, moving=True, settled=False,
                   work=work)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
