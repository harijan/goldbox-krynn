#!/usr/bin/env python3
"""Check #46: Strength on a knight with no other class and a strength of 3
(docs/checks.md, Checks against the original: "Cast Strength on a knight or ranger
with no other class and a strength of 3").

With no mage, cleric, thief or fighter level the roll is an uninitialized
local (5b04:2e4e's [bp-6], [bp-0x36] of 5b04:1415): the low byte of the
frame pointer 6346:161b pushed there, 0x74 from the commands' Cast and
0x9a from camp's Magic, says docs/camp.md. The effect 0x26 it adds is valued
the roll + 100, so its value byte shows the roll: 0xd8 for 0x74, 0xfe for
0x9a.

Game A's files patched: SIR STRONGSWORD (a knight of level 1 alone)
strength 3/00, KAL given two Strengths in memory. The port fights the
guards at Throtl's gate (block 32, 4be6=1) and camps and saves the game
there as C; the original and the port (eclplay --start-menu) load C and
cast from the commands' Cast, then (a second run) from camp's Magic: the
screens at the load and after the cast against the port's at the same
key reads, and the knight's strength, exceptional strength and effects
(read in memory) against those in the port's game saved right after.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import e_common as E  # noqa: E402
cokbox = E.cokbox

PATCHES = {"CHRDATA1.SAV": {0x10: 3, 0x11: 3, 0x1C: 0, 0x1D: 0},
           # KAL: two Strengths (0x23) in the first empty memory bytes
           "CHRDATA5.SAV": {0x1E: [0x23, 0x23]}}
SELECT_KAL = r"\v\v\v\v"
STRENGTH = r"\v\v\v\v\v\v\v\v\v\r"                # the ninth spell: Strength
ON_KNIGHT = r"\^\^\^\^S"                          # up to SIR STRONGSWORD, Select
PATHS = {"commands": SELECT_KAL + "C" + STRENGTH + ON_KNIGHT,
         "camp": "EM" + SELECT_KAL + "C" + STRENGTH + ON_KNIGHT}
SAVE_AFTER = {"commands": r"\eESD", "camp": r"\e\eSD"}
# The roll each path leaves (docs/camp.md): the effect's value is it + 100.
ROLL = {"commands": 0x74, "camp": 0x9A}


def port_knight(saves):
    """The knight in the port's saved game D: (strength, exceptional, the
    effects (id, minutes, value))."""
    rec = (saves / "CHRDATD1.SAV").read_bytes()
    sfx = (saves / "CHRDATD1.SFX").read_bytes() if (saves / "CHRDATD1.SFX").exists() else b""
    effects = [(sfx[i], int.from_bytes(sfx[i + 1:i + 3], "little"), sfx[i + 3])
               for i in range(0, len(sfx), 9)]
    return rec[0x11], rec[0x1C], effects


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-strength")
    checklib.need_saves()
    v = checklib.Verdict()
    saves = E.make_c(work, E.patched_saves(work / "patched", PATCHES))
    load = E.LOAD.format("C")
    for path in PATHS:
        pair = E.Pair(load + r"\sloaded." + PATHS[path] + r"\scast.", work / path,
                      saves=[saves])
        text, psaves, _ = E.run_port(load + PATHS[path] + SAVE_AFTER[path],
                                     work / f"port-{path}-saved", E.eclplay(), saves=[saves])
        if not (psaves / "CHRDATD1.SAV").exists():
            raise cokbox.BoxError(f"the port did not save game D ({path}):\n" + text[-1500:])
        p_str, p_exc, p_eff = port_knight(psaves)
        print(f"port, {path}: strength {p_str}/{p_exc}, effects {p_eff}")
        with cokbox.Box(workdir=work / f"box-{path}", saves=[saves], verbose=args.verbose,
                        timeout=600) as box:
            box.keys(load)
            box.wait_input(settle=0.5)
            pair.judge(v, "loaded", box.screenshot(work / f"{path}-loaded.png"),
                       f"{path}: game C loaded")
            print(f"original, {path}, before:", E.party(box)[0])
            box.keys(PATHS[path])
            box.wait_input(settle=0.5)
            pair.judge(v, "cast", box.screenshot(work / f"{path}-cast.png"),
                       f"{path}: after Strength")
            n, _, _, s, x, eff = E.party(box)[0]
            print(f"original, {path}: {n} strength {s}/{x}, effects {eff}; waits at "
                  f"{box.where()}")
        o_eff = [(e[0], e[1], e[2]) for e in eff]
        v.value(f"{path}: the knight's strength", s, p_str)
        v.value(f"{path}: the knight's exceptional strength", x, p_exc)
        v.value(f"{path}: the knight's effects (id, minutes, value)", o_eff, p_eff)
        v.check([e for e in o_eff if e[0] == 0x26 and e[2] == ROLL[path] + 100],
                f"{path}: effect 0x26 valued {ROLL[path]:#04x} + 100 (docs/camp.md's roll)",
                str(o_eff))
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
