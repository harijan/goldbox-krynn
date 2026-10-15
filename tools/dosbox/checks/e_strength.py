#!/usr/bin/env python3
"""Check #46: Strength on a knight with no other class and a strength of 3
(README, Checks against the original: "Cast Strength on a knight or ranger
with no other class and a strength of 3").

With no mage, cleric, thief or fighter level the roll is an uninitialized
local (5b04:2e4e's [bp-6], [bp-0x36] of 5b04:1415): the low byte of the
frame pointer 6346:161b pushed there, 0x74 from the commands' Cast and
0x9a from camp's Magic, says the README. The effect 0x26 it adds is valued
the roll + 100, so its value byte shows the roll: 0xd8 for 0x74, 0xfe for
0x9a.

Game A's files patched: SIR STRONGSWORD (a knight of level 1 alone)
strength 3/00, KAL given two Strengths in memory. The port fights the
guards at Throtl's gate (block 32, 4be6=1) and camps and saves the game
there as C; the original loads C and casts from the commands' Cast, then
(a second run) from camp's Magic. The knight's strength, exceptional
strength and effects are read in memory, and compared with the port's
files saved after the same casts.
"""
import argparse
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import e_common as E  # noqa: E402
cokbox = E.cokbox

PATCHES = {"CHRDATA1.SAV": {0x10: 3, 0x11: 3, 0x1C: 0, 0x1D: 0},
           # KAL: two Strengths (0x23) in the first empty memory bytes
           "CHRDATA5.SAV": {0x1E: [0x23, 0x23]}}
FIGHT_AND_SAVE = r"\r\rEESC"                      # gate fight, Encamp, Save C
SELECT_KAL = r"\v\v\v\v"
STRENGTH = r"\v\v\v\v\v\v\v\v\v\r"                # the ninth spell: Strength
ON_KNIGHT = r"\^\^\^\^S"                          # up to SIR STRONGSWORD, Select
PATHS = {"commands": SELECT_KAL + "C" + STRENGTH + ON_KNIGHT,
         "camp": "EM" + SELECT_KAL + "C" + STRENGTH + ON_KNIGHT}
SAVE_AFTER = {"commands": r"\eESD", "camp": r"\e\eSD"}


def port_result(saves_c, path, work):
    out, saves, _ = E.eclplay_block(PATHS[path] + SAVE_AFTER[path], work / f"port-{path}",
                                    ["--load", saves_c / "SAVGAMC.DAT", "--play"], 32)
    rec = (saves / "CHRDATD1.SAV").read_bytes()
    sfx = (saves / "CHRDATD1.SFX").read_bytes() if (saves / "CHRDATD1.SFX").exists() else b""
    effects = [(sfx[i], int.from_bytes(sfx[i + 1:i + 3], "little"), sfx[i + 3])
               for i in range(0, len(sfx), 9)]
    return rec[0x11], rec[0x1C], effects, out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-strength-")).resolve()
    if not (E.SAVE / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    patched = E.patched_saves(work / "patched", PATCHES)
    out, saves_c, _ = E.eclplay_block(
        FIGHT_AND_SAVE, work / "port-save",
        ["--combat", "pass", "--party", patched / "SAVGAMA.DAT", "--play", "--set", "4be6=1"], 32)
    assert (saves_c / "SAVGAMC.DAT").exists(), out[-2000:]
    status = 0
    for path in PATHS:
        p_str, p_exc, p_eff, _ = port_result(saves_c, path, work)
        print(f"port, {path}: strength {p_str}/{p_exc}, effects {p_eff}")
        with E.Box(workdir=work / f"box-{path}", saves=[saves_c], verbose=args.verbose,
                        timeout=600) as box:
            box.keys(E.LOAD.format("C"))
            box.wait_input(settle=0.5)
            box.screenshot(work / f"{path}-loaded.png")
            knight = E.records(box)[0]
            print(f"original, {path}, before:", E.party(box)[0])
            box.keys(PATHS[path])
            box.wait_input(settle=0.5)
            box.screenshot(work / f"{path}-cast.png")
            n, st, hp, s, x, eff = E.party(box)[0]
            print(f"original, {path}: {n} strength {s}/{x}, effects {eff}; waits at "
                  f"{box.where()}")
            o_eff = [(e[0], e[1], e[2]) for e in eff]
            ok = (s, x) == (p_str, p_exc) and o_eff == p_eff
            print(f"  {'AGREES' if ok else 'DIFFERS'} with the port")
            if not ok:
                status = 1
            del knight
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())
