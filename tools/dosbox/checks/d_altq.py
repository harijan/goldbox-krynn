#!/usr/bin/env python3
"""Check #25 (DEVELOPERS.md, Checks against the original): "With Alt-Q, the first
combatant's turn runs twice: compare whether its effects for event 7 and
0x0f act twice (a held one, a snake bite) in that turn."

The port writes game C: game A with ISTAN HORBIN given effect 0x30
(immolation, a monster's effect run at the start of a turn, event 0x0f),
saved in the outpost inn's camp. Both load C, leave the outpost and meet
the caravan's baaz (seed 7). ISTAN's turn is the first: he immolates
(SIR STRONGSWORD and KAL beside him take 1d6), the menu shows; Alt-Q gives
the turn back and the computer runs it again from its start. The original's
hit points are sampled in memory as fast as the host can after Alt-Q
(against those the port's log gives), the battle then running on Auto to
its end; the screens and the memory (d_lib.compare_state) at the menu and
at the end against the port's.

An effect of event 7 (held, asleep, paralysed: 0x1b, 0x1f, 0x33-0x35, 3)
ends the turn before the menu, so Alt-Q cannot be pressed in such a turn:
that half of the bullet cannot arise.
"""
import argparse
import sys
import time

import checklib
import d_lib


def port_hp(lines):
    """SIR STRONGSWORD's and KAL's hit points after ISTAN's immolation before
    his menu, and after it again at Alt-Q, from the port's log."""
    i = lines.index("turn: ISTAN HORBIN (initiative 9)")
    j = lines.index("turn: ISTAN HORBIN (initiative 19)")
    pre = [line[7:] for line in lines[i - 7:i] if line.startswith("print:")]
    again = [line[7:] for line in lines[i + 2:j] if line.startswith("print:")]
    print("port, before the menu:", pre)
    print("port, after Alt-Q:", again)
    hp = {"SIR STRONGSWORD": 27, "KAL": 21}
    out = []
    for chunk in (pre, again):
        chunk = chunk[chunk.index("immolates") + 1:]
        for who, text in zip(chunk[0::2], chunk[1::2]):
            hp[who] -= int(text.split()[1])
        out.append((hp["SIR STRONGSWORD"], hp["KAL"]))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-altq")
    saves = d_lib.make_save("C", work / "save", args=["--effect", "2,30,0,0"])
    route = d_lib.load("C") + r"Lnm9\r"
    port = d_lib.Port(work / "eclplay", d_lib.marked([("menu", route), ("end", r"\@q")]),
                      args.seed, saves=[saves])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        shown = d_lib.step(b, route, "menu", work)
        d_lib.screen(v, "screen menu", shown, port, "menu", work)
        d_lib.compare_state(v, b, port, "menu")
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
        b.wait_input(seconds=300, settle=1.0)
        shown = b.screenshot(work / "end.png")
        d_lib.screen(v, "screen end", shown, port, "end", work)
        d_lib.compare_state(v, b, port, "end")
    print("original: SIR STRONGSWORD's and KAL's hit points at the menu, then after Alt-Q:",
          seen)
    want = port_hp(port.lines)
    print("port: the same after each immolation:", want)
    # The second immolation hits SIR then KAL: the original's samples pass
    # through SIR's new value with KAL's old one.
    v.check(seen[:3] == [want[0], (want[1][0], want[0][1]), want[1]],
            "#25 Alt-Q: ISTAN's immolation (event 0x0f) acts again at the first turn's start "
            "in the original, the same damage as the port's log")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
