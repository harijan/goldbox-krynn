#!/usr/bin/env python3
"""Companions in Throtl (README, Checks against the original): #104 "Let
Skyla join in ECL2 block 50 and fight: she should have no combat icon
(`CPIC2` has no record 44). Let the kender join in Throtl (ECL1 block 32):
his combat icon should be `CPIC1` record 15"; #106 "Leave Throtl (ECL1 block 32) with the kender and
Strangbourn: `YOUR COMPANIONS LEAVE.`, and both should be gone from the
party."

A saved game inside Throtl at 2,4 facing north (var 0x4c4c bit 64: the pit
at 2,5 already sprung), E. Both games: a step north (the kender, special
14: Yes, KILDIRF joins), turn about, three steps south (Strangbourn,
special 17: Yes), the screens compared. The port then saves that party of
eight as F, which is copied to G; F is moved in the file to 7,15 facing
south (the gate) and G to 8,8 facing north (below special 2, the
hobgoblin who growls). F: a step south leaves the map: `YOUR COMPANIONS
INSIST THAT THEY MUST REMAIN`, LEAVE: `YOUR COMPANIONS LEAVE.`; the
original's party is read in memory after. G: a step north, ATTACK: the
battle's first screen at the first character's turn, the kender's icon on
the map.
"""
import argparse
import shutil
import sys

import c_geo
import c_places as cp

JOIN = [("kender", r"m\^"), ("ask", r"\r"), ("kildirf", "Y"),
        ("walk", r"\>\>\^\^"), ("knight", r"\^"), ("ask2", r"\r"), ("story", "Y"),
        ("joined", r"\r")]


def names(box):
    return [cp.name_of(raw) for _, raw in cp.party_records(box)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="join, leave, fight or skyla")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("npc", args.workdir)
    saves, _ = cp.throtl_save(work / "save-e", "E", at=(2, 4, 0), var={0x4C4C: 64})
    status = 0
    if args.only in (None, "join"):
        c = cp.Compare(work / "join", saves, [("loaded", cp.load_keys("E"))] + JOIN +
                       [("party", "", names)], seed=1, verbose=args.verbose)
        status |= c.run()
        print("original's party after the joins:", c.probes["party"])
        print("port:", [l for l in c.port_text.splitlines() if l.startswith("party:")])

    keys = cp.load_keys("E") + "".join(k for _, k in JOIN) + "eESF"
    _, psaves, _ = cp.cokbox.eclplay(keys, work / "save-f", saves=[saves], shots=False,
                                     args=["--seed", "1"])
    if not (psaves / "SAVGAMF.DAT").exists():
        raise SystemExit("the port saved no game F")
    shutil.copyfile(psaves / "SAVGAMF.DAT", psaves / "SAVGAMG.DAT")
    for f in list(psaves.glob("CHRDATF*")):
        shutil.copyfile(f, psaves / f.name.replace("CHRDATF", "CHRDATG"))
    geo = c_geo.load(1, 32)
    cp.edit_save(psaves, "F", at=(7, 15, 4), geo=geo)
    cp.edit_save(psaves, "G", at=(8, 8, 0), geo=geo)

    if args.only in (None, "leave"):
        c = cp.Compare(work / "leave", psaves, [
            ("loaded", cp.load_keys("F"), names),
            ("insist", r"m\^"),             # YOUR COMPANIONS INSIST THAT THEY MUST REMAIN.
            ("leave", "L"),                 # YOUR COMPANIONS LEAVE.
            ("overland", r"\r", names),
        ], seed=1, verbose=args.verbose)
        status |= c.run()
        print("#106 original's party before and after:", c.probes["loaded"],
              c.probes["overland"])
        print("#106 port:", [l for l in c.port_text.splitlines()
                             if "COMPANIONS" in l or l.startswith(("party:", "monster:"))])

    if args.only in (None, "fight"):
        c = cp.Compare(work / "fight", psaves, [
            ("loaded", cp.load_keys("G")),
            ("growl", r"m\^"),              # A HOBGOBLIN GROWLS ... ATTACK LEAVE PARLAY
            ("battle", "A"),                # the first turn: the map
        ], seed=1, verbose=args.verbose)
        status |= c.run()
        print("#104 port:", [l for l in c.port_text.splitlines()
                             if l.startswith(("combat: 7 ", "combat: 8 ", "turn:"))][:4])
    if args.only in (None, "skyla"):
        # Skyla: the port writes a game in ECL2 block 50 entered with vars
        # 0x4c3a 0 and 0x4c5c 1, so that the guide at the gate (special 1,
        # 895c) introduces her and ADD NPC 44 adds her; moved in the file to
        # 4,5 facing north, below an open grave (special 10 at 4,4, 91c3).
        ssaves, _ = cp.port_save(work / "save-skyla", r"\r\r\r\r\rESE", [
            "--load", cp.SAVE_A, "--play", "--file", "2", "--set", "4be6=1", "--set", "4c3a=0",
            "--set", "4c5c=1"], block=50)
        cp.edit_save(ssaves, "E", at=(4, 5, 0), var={0x4C3A: 0, 0x4C5C: 1},
                     geo=c_geo.load(2, 50))
        c = cp.Compare(work / "skyla", ssaves, [
            ("loaded", cp.load_keys("E"), names),
            ("graves", r"m\^"),            # UNDEAD RISE FROM THE OPEN GRAVES AROUND YOU.
            ("battle", r"\r"),              # MOLLY's turn: the map, Skyla on it or not
        ], seed=1, box_seed=(1 * 0x8088405 + 1) & 0xFFFFFFFF, verbose=args.verbose)
        # The port rolls once on resuming (its location vector at 4,5,
        # special 0: 8787's RANDOM 99), the original not at all before the
        # first command: the original starts from the port's seed after
        # that roll, so that their rolls in the fight agree.
        status |= c.run()
        print("#104 original's party:", c.probes["loaded"])
        print("#104 port:", [l for l in c.port_text.splitlines()
                             if "SKYLA" in l or l.startswith(("error:", "turn:"))][:6])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())
