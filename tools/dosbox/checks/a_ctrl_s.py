#!/usr/bin/env python3
"""Check: Ctrl-S and Alt-R (README, Checks against the original: "Ctrl-S:
turn the sound off, let the title's menu run out into the demonstration and
come back; Ctrl-S should no longer turn it on (the install after the title
copies the device, off, to `DS:577b`). And Alt-R ... should turn the sound
off as Ctrl-S does.").

KRYNN.CFG's sound `T` (the Tandy's, device 3). Run 1 (turbo): the title
skipped with a key; at its menu Ctrl-S; the menu runs out, the
demonstration plays, the title and its menu come back; Ctrl-S twice. Run 2:
Play, then Alt-R twice and Ctrl-S twice in the start menu. The original's
DS:577a (the device) and DS:577b (the device saved) are read after each
step; the port's, from a copy of eclplay that prints them at each call to
the sound driver (a_common.delay_eclplay, ECLPLAY_SOUND_STATE), for the
same keys.
"""
import argparse
import subprocess
import sys
import time

import a_common as A
from a_demo import at_title_menu, party

cokbox = A.cokbox
CTRL_S, ALT_R = 0x1F13, 0x1300


def port_states(work, keys):
    exe = A.delay_eclplay(work / "tool")
    out = work / "port"
    (out / "saves").mkdir(parents=True, exist_ok=True)
    cfg = out / "krynn.cfg"
    cfg.write_bytes(cokbox.krynn_cfg("T", "T").encode())
    run = subprocess.run([str(exe), "--start-menu", "--config", str(cfg), "--saves",
                          str(out / "saves"), "--wav", str(out / "x.wav"), "--keys", keys,
                          str(cokbox.ROOT / "Assets")],
                         env=dict(A.os.environ, ECLPLAY_SOUND_STATE="1"), capture_output=True,
                         text=True, timeout=600)
    # The state after each Ctrl-S (call 5) and each install (call 1).
    lines = [l for l in run.stdout.splitlines()
             if l.startswith(("sound-state: call 5,", "sound-state: call 1,", "menu:", "choice:"))]
    return lines


def state(box):
    return f"577a {box.ds_byte(0x577A)}, 577b {box.ds_byte(0x577B)}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "ctrl-s")
    results = {}
    print("port, run 1 (keys \\cS\\t\\cS\\cS):")
    for l in port_states(work / "p1", r"\cS\t\cS\cS"):
        print("  " + l)
    print("port, run 2 (keys \\kxP\\@r\\@r\\cS\\cS):")
    for l in port_states(work / "p2", r"\kxP\@r\@r\cS\cS"):
        print("  " + l)

    print("original, run 1:")
    with cokbox.Box(workdir=work / "box1", sound="T", turbo=True, verbose=args.verbose,
                    timeout=900) as box:
        box.keys("x")
        box._until(lambda: at_title_menu(box), 60, "the title's menu", 0.02)
        results["menu"] = state(box)
        box.key(CTRL_S)
        time.sleep(0.2)
        results["Ctrl-S at the menu"] = state(box)
        print(f"  at the title's menu: {results['menu']}; after Ctrl-S: "
              f"{results['Ctrl-S at the menu']} (waits at {box.where()})")
        names = box._until(lambda: "SIR RINGWALD" in party(box) and party(box), 120,
                           "the demonstration", 0.05)
        print(f"  the menu ran out: the demonstration plays ({names}): {state(box)}")
        box._until(lambda: not party(box), 600, "the demonstration's end", 0.2)
        box._until(lambda: at_title_menu(box), 300, "the title's menu again", 0.02)
        results["menu again"] = state(box)
        box.key(CTRL_S)
        time.sleep(0.2)
        results["Ctrl-S again"] = state(box)
        box.key(CTRL_S)
        time.sleep(0.2)
        results["Ctrl-S a third time"] = state(box)
        print(f"  the title's menu again: {results['menu again']}; Ctrl-S: "
              f"{results['Ctrl-S again']}; Ctrl-S: {results['Ctrl-S a third time']}")
    print("original, run 2:")
    with cokbox.Box(workdir=work / "box2", sound="T", verbose=args.verbose) as box:
        box.keys(r"\kxP")
        box.wait_input(settle=0.3)
        steps = [("start menu", None), ("Alt-R", ALT_R), ("Alt-R", ALT_R), ("Ctrl-S", CTRL_S),
                 ("Ctrl-S", CTRL_S)]
        out = []
        for label, k in steps:
            if k:
                box.key(k)
                box.wait_input(settle=0.2)
            out.append(f"{label}: {state(box)}")
        print("  " + "; ".join(out) + f" (waits at {box.where()})")
        run2 = out
    want1 = {"menu": "577a 3, 577b 3", "Ctrl-S at the menu": "577a 5, 577b 3",
             "menu again": "577a 5, 577b 5", "Ctrl-S again": "577a 5, 577b 5",
             "Ctrl-S a third time": "577a 5, 577b 5"}
    want2 = ["start menu: 577a 3, 577b 3", "Alt-R: 577a 5, 577b 3", "Alt-R: 577a 3, 577b 3",
             "Ctrl-S: 577a 5, 577b 3", "Ctrl-S: 577a 3, 577b 3"]
    bad = (results != want1) + (run2 != want2)
    print("verdict:", "AGREES with the port's states above" if not bad else
          "DIFFERS from the states the port prints", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
