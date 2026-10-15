#!/usr/bin/env python3
"""Check #25 (README, Checks against the original): "With Alt-Q, the first
combatant's turn runs twice: compare whether its effects for event 7 and
0x0f act twice (a held one, a snake bite) in that turn."

The port writes game C: game A with ISTAN HORBIN given effect 0x30
(immolation, a monster's effect run at the start of a turn, event 0x0f),
saved in the outpost inn's camp. Both load C, leave the outpost and meet
the caravan's baaz (seed 7). ISTAN's turn is the first: he immolates
(SIR STRONGSWORD and KAL beside him take 1d6), the menu shows; Alt-Q gives
the turn back and the computer runs it again from its start. The original's
hit points are sampled in memory as fast as the host can after Alt-Q, the
battle then running on Auto to its end, whose screen is compared too.

An effect of event 7 (held, asleep, paralysed: 0x1b, 0x1f, 0x33-0x35, 3)
ends the turn before the menu, so Alt-Q cannot be pressed in such a turn:
that half of the bullet cannot arise.
"""
import argparse
import sys
import time

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("altq", args.workdir)
    saves = d_lib.make_save("C", work / "save", args=["--effect", "2,30,0,0"])
    route = d_lib.load("C") + r"Lnm9\r"
    lines, shots = d_lib.port(route + r"\@q", work / "port", args.seed, saves=[saves])
    i = lines.index("turn: ISTAN HORBIN (initiative 9)")
    pre = [l[7:] for l in lines[i - 7:i] if l.startswith("print:")]
    j = lines.index("turn: ISTAN HORBIN (initiative 19)")
    again = [l[7:] for l in lines[i + 2:j] if l.startswith("print:")]
    print("port, before the menu:", pre)
    print("port, after Alt-Q:", again)
    port_hp = []
    hp = {"SIR STRONGSWORD": 27, "KAL": 21}
    for chunk in (pre, again):
        chunk = chunk[chunk.index("immolates") + 1:]
        for who, text in zip(chunk[0::2], chunk[1::2]):
            hp[who] -= int(text.split()[1])
        port_hp.append((hp["SIR STRONGSWORD"], hp["KAL"]))
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        res = d_lib.run_steps(b, [("menu", route)], shots, work)
        cs = d_lib.combatants(b)
        recs = [d_lib.by_name(cs, n)[0]["rec"] for n in ("SIR STRONGSWORD", "KAL")]
        def sample():
            return tuple(b.read(r + 0x197, 1)[0] for r in recs)
        seen = [sample()]
        b.push_key(0x1000)  # Alt-Q
        end = time.monotonic() + 8
        while time.monotonic() < end:
            s = sample()
            if s != seen[-1]:
                seen.append(s)
        print("original: SIR STRONGSWORD's and KAL's hit points at the menu, then after "
              "Alt-Q:", seen)
        b.wait_input(seconds=300, settle=1.0)
        shown = b.screenshot(work / "end.png")
        res["end"] = (d_lib.compare(shown, shots, "end", work), shown)
    print("port: the same after each immolation:", port_hp)
    ok = seen[0] == port_hp[0] and len(seen) > 1 and seen[1] == (port_hp[1][0], seen[0][1]) \
        and seen[2] == port_hp[1] and all(c == 0 for c, _ in res.values())
    status = d_lib.verdict(ok, "#25 Alt-Q: the first turn's start runs again: ISTAN's "
                           "immolation (event 0x0f) acts twice in the original as in the "
                           "port, same damage; the battle on Auto ends on the same screen")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())
