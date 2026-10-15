#!/usr/bin/env python3
"""Check #8, the other parts: Cause Disease's 0x22 and a constitution of 20
(DEVELOPERS.md, Checks against the original, "Timers": "Cast Cause Disease and
wait out 0x22 (10-60 minutes): 'is weakened', strength a point lower each
hour, and a hit point lost each ten minutes down to one. A character of
constitution 20 should heal a point each hour ('is partially healed'),
also after its constitution drops.").

Copies of SAVE/'s game A, SIR STRONGSWORD changed in the files:
  D  given 0x22 for 20 minutes, value 5 (a level-5 caster's), its handler
     run on removal, as Cause Disease leaves it; rested 5 hours.
  E  constitution 20 (+0x18, +0x19) and 12 hit points: the load adds 0x3e;
     rested 3 hours.
  F  constitution 19 and 12 hit points, with 0x3e (60 minutes, run on
     removal) in its .SFX, as a character whose constitution dropped below
     20 keeps it; rested 3 hours.
Each game is loaded in the original (DOSBox-X) and the port (eclplay), taken
to the outpost inn's camp, the record read in memory, rested, read again
(strength, constitution, hit points, status, effects: these must be the
port's, from its G), and saved as G, which must be the port's, masked.

In camp the timers count only the members whose flag the rest set at its
start (57e4:0171), so the effects an ending one adds are not counted down
in that rest. Outside camp every call sets them: two more cases travel
the overland map instead, three steps north from the first outpost, each
passing twelve hours in one call (57e4:0549 unit 3, count 12):
  H  as D, and J  as F, saved first by the port with var 0x4c2d set (the
     caravan met, so that no fight comes) as games I and J; the party
     leaves the outpost (Leave, No), Move, up three times, Exit, Encamp,
     and saves as G.
"""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402


def rest_hours(n):
    return "RH" + "A" * n + "R"


OVERLAND = r"\r\r\r\r\r\r\rLnm\^\^\^EE"   # game A's start, Leave, No, Move, 3 steps, Exit, Encamp
CASES = {   # file edits, .SFX, camp rest hours or "overland" (the port's save letter)
    "D": ({}, [(0x22, 20, 5, 1)], 5),
    "E": ({0x18: 20, 0x19: 20, 0x197: 12}, None, 3),
    "F": ({0x18: 19, 0x19: 19, 0x197: 12}, [(0x3E, 60, 0xFF, 1)], 3),
    "H": ({}, [(0x22, 20, 5, 1)], "I"),
    "J": ({0x18: 19, 0x19: 19, 0x197: 12}, [(0x3E, 60, 0xFF, 1)], "J"),
}


def state(raw, effects):
    return (f"str {raw[0x11]}/{raw[0x1c]} con {raw[0x19]} hp {raw[0x197]}/{raw[0x62]} "
            f"status {raw[0x188]} effects {[(hex(e[0]), e[1], e[2]) for e in effects]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--cases", default="DEFHJ")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-timers")
    v = checklib.Verdict()
    for letter in args.cases:
        edits, effects, hours = CASES[letter]
        game = work / f"game{letter}"
        if game.exists():
            shutil.rmtree(game)
        L.copy_game(L.SAVE, "A", game, letter)
        if edits:
            L.patch(game / f"CHRDAT{letter}1.SAV", edits)
        if effects:
            L.write_sfx(game / f"CHRDAT{letter}1.SFX", effects)
        overland = isinstance(hours, str)
        if overland:
            # The port saves the game in the inn's camp with the caravan met.
            out, made, _ = L.port(L.A_TO_CAMP + "S" + hours, work / f"make{letter}",
                                  ["--load", game / f"SAVGAM{letter}.DAT", "--set", "4c2d=1",
                                   "--play"], saves=[game])
            shutil.rmtree(game)
            L.copy_game(made, hours, game)
            # A camp's save resumes at the outpost's menu.
            letter, route, action = hours, "", OVERLAND[len(r"\r") * 7:]
            print(f"game {letter}: SIR STRONGSWORD {edits} {effects}, saved by the port with "
                  "0x4c2d=1, three overland steps")
        else:
            route, action = L.A_TO_CAMP, rest_hours(hours)
            print(f"game {letter}: SIR STRONGSWORD {edits} {effects}, rest {hours} hours")
        keys = route + action + L.save_keys("G")
        pout, psaves, _ = L.port(keys, work / f"port{letter}",
                                 ["--load", game / f"SAVGAM{letter}.DAT", "--play"], saves=[game])
        said = [ln[7:] for ln in pout.splitlines() if ln.startswith(("print: ", "overlan"))]
        start = max(i for i, s in enumerate(said) if "makes camp" in s or "PRESS" in s
                    or "WHERE DO YOU" in s) if not overland else 0
        print("  port's text:", " / ".join(s.strip() for s in said[start + 1:] if s.strip()))
        port_after = state(L.sav(psaves, "G", 1), [(i, m, x) for i, m, x, _ in L.sfx(psaves, "G", 1)])
        with L.box(work / f"run{letter}", [game], verbose=args.verbose, turbo=True) as box:
            box.keys(L.load_keys(letter) + route)
            box.wait_input(settle=0.3)
            addr, raw = L.member(box, "SIR STRONGSWORD")
            print("  original, before:", state(raw, L.effects_at(box, addr + 0xE3)))
            shots = {"n": 0, "last": None}

            def each(box):
                s = box.screenshot()
                if shots["last"] is None or s.pixels != shots["last"].pixels:
                    s.save_png(work / f"run{letter}" / f"rest-{shots['n']:03d}.png")
                    shots["n"] += 1
                    shots["last"] = s
            if overland:
                box.keys(action)
                box.wait_input(settle=0.3)
                c = L.clock(box)
            else:
                box.keys(action)
                c = L.wait_rest(box, each=each)
            raw = box.read(addr, L.REC)
            orig_after = state(raw, L.effects_at(box, addr + 0xE3))
            print(f"  original, after: the clock {c[1:5]}")
            box.keys(L.save_keys("G"))
            box.wait_input(settle=0.3)
            odir = L.keep_saves(box, work / f"run{letter}" / "saved")
        v.value(f"game {letter}: SIR STRONGSWORD after", orig_after, port_after)
        L.judge_games(v, f"game {letter}: saved G", odir, "G", psaves, "G")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
