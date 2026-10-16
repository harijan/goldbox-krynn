#!/usr/bin/env python3
"""Check #5: Ctrl-S and Alt-R (DEVELOPERS.md, Checks against the original: "Ctrl-S:
turn the sound off, let the title's menu run out into the demonstration and
come back; Ctrl-S should no longer turn it on (the install after the title
copies the device, off, to `DS:577b`). And Alt-R ... should turn the sound
off as Ctrl-S does.").

KRYNN.CFG's sound `T` (the Tandy's, device 3). Run 1 (turbo): the title
skipped with a key; at its menu Ctrl-S; the menu runs out, the
demonstration plays, the title and its menu come back; Ctrl-S twice. Run 2:
Play, then Alt-R twice and Ctrl-S twice in the start menu. The original's
DS:577a (the device) and DS:577b (the device saved) are read at each wait
and compared with the port's at the same key read (a_common's probe build,
with --wav, which gives eclplay its sound).
"""
import argparse
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402
from a_demo import at_title_menu, party  # noqa: E402

cokbox = A.cokbox
CTRL_S, ALT_R = 0x1F13, 0x1300
# Run 1 in the port: \t lets the title's menu run out (the box waits for it).
RUN1 = r"\kx\smenu.\cS\sctrl-s.\t\smenu-again.\cS\sagain.\cS\sthird."
RUN2 = r"\kxP\sstart.\@r\salt-r.\@r\salt-r-2.\cS\sctrl-s.\cS\sctrl-s-2."


def port_states(work, keys):
    """{mark: (577a, 577b)} of the probe build at each mark of KEYS, its
    sound the Tandy's as KRYNN.CFG's `T` gives the box's."""
    probes = A.probes_at(keys, work, args=["--wav", os.devnull, "--sound", "tandy"])
    return {name: (p.get("577a"), p.get("577b")) for name, p in probes.items()}


def state(box):
    return (box.ds_byte(0x577A), box.ds_byte(0x577B))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "ctrl-s")
    v = checklib.Verdict()
    orig = {}

    print("original, run 1 (turbo):")
    with cokbox.Box(workdir=work / "box1", sound="T", turbo=True, verbose=args.verbose,
                    timeout=900) as box:
        box.keys("x")
        box._until(lambda: at_title_menu(box, work), 60, "the title's menu", 0.02)
        orig["menu"] = state(box)
        box.key(CTRL_S)
        box.wait_input(settle=0.2)
        orig["ctrl-s"] = state(box)
        names = box._until(lambda: "SIR RINGWALD" in party(box) and party(box), 120,
                           "the demonstration", 0.05)
        print(f"  the menu ran out: the demonstration plays ({names}): 577a/577b {state(box)}")
        box._until(lambda: not party(box), 600, "the demonstration's end", 0.2)
        box._until(lambda: at_title_menu(box, work), 300, "the title's menu again", 0.02)
        orig["menu-again"] = state(box)
        for name in ("again", "third"):
            box.key(CTRL_S)
            box.wait_input(settle=0.2)
            orig[name] = state(box)
    print("original, run 2:")
    with cokbox.Box(workdir=work / "box2", sound="T", verbose=args.verbose) as box:
        box.keys(r"\kxP")
        box.wait_input(settle=0.3)
        orig["start"] = state(box)
        for name, k in (("alt-r", ALT_R), ("alt-r-2", ALT_R), ("ctrl-s", CTRL_S),
                        ("ctrl-s-2", CTRL_S)):
            box.key(k)
            box.wait_input(settle=0.2)
            orig[name + " (run 2)" if name == "ctrl-s" else name] = state(box)

    port1 = port_states(work / "port1", RUN1)
    port2 = port_states(work / "port2", RUN2)
    for name in ("menu", "ctrl-s", "menu-again", "again", "third"):
        v.value(f"run 1, {name}: 577a, 577b", orig[name], port1[name])
    for name in ("start", "alt-r", "alt-r-2", "ctrl-s", "ctrl-s-2"):
        v.value(f"run 2, {name}: 577a, 577b", orig[name + " (run 2)" if name == "ctrl-s" else name],
                port2[name])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
