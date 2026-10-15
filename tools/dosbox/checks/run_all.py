#!/usr/bin/env python3
"""Run every DOSBox-X check script (tools/dosbox/checks/) side by side and
print one verdict per script: the README's checks it covers, its exit (0: the
original and the port agree; 1: a screen, file or memory read differs) and
whether that is the verdict recorded when the checks were run (README,
"Checks against the original" > "Checked in DOSBox-X").

    python3 tools/dosbox/checks/run_all.py [--jobs N] [--only NAME ...] [--out DIR]

Each script runs with --workdir OUT/NAME (default OUT build/dosbox-checks),
its output in OUT/NAME.log. COKBOX_DISPLAYS (e.g. 180-189) keeps the X
displays to a range; --jobs defaults to the range's size less two (else 6).
Needs `make build/eclplay build/cok build/START_FULL.EXE` (make dosbox-checks
does it). Exit 0 when every script gave its recorded verdict."""

import argparse
import os
import pathlib
import subprocess
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))

# (script, the checks it covers, the exit recorded, why when 1). A known port
# difference (or a difference the check expects, such as DOSBox-X's own CGA
# cursor) makes a script return 1; a fix of the port then changes the
# recorded exit here (and the README's entry).
WALLS = "#27's two-set wall records (Throtl's 3D view)"
STOP = "the full stop of PRESS <ENTER>/<RETURN> TO CONTINUE."
VECTORS = "Begin Adventuring runs the first block's location vector"
CHECKS = [
    # A: startup, title, setup, the start menu
    ("a_setup", "#1", 0, ""),
    ("a_ctrl_s", "#5", 0, ""),
    ("a_debug", "#13", 0, ""),
    ("a_debug_battle", "#13 (battle)", 0, ""),
    ("a_demo", "#107 #108", 0, ""),
    ("a_unsaved", "#109", 0, ""),
    ("a_create", "#110 #111", 0, ""),
    ("a_icon", "#112", 0, ""),
    ("a_roster", "#113 #114 #115", 0, ""),
    ("a_hall", "#116 #117", 0, ""),
    ("a_modify", "#118", 0, ""),
    # B: camp, magic, items, effects, saving
    ("b_slow_poison_63h", "#8", 0, ""),
    ("b_timers", "#8", 0, ""),
    ("b_thief_skills", "#32", 0, ""),
    ("b_rest_day_effects", "#33", 0, ""),
    ("b_effect_order", "#34", 0, ""),
    ("b_camp_screen", "#36 #37", 0, ""),
    ("b_resume", "#38", 1, "#38: the view resumed in Throtl's camp and the saved last mode"),
    ("b_fix_time", "#39", 0, ""),
    ("b_heal_day", "#40", 0, ""),
    ("b_scribe", "#41", 0, ""),
    ("b_pics", "#42", 0, ""),
    ("b_grimoire", "#43", 0, ""),
    ("b_display_effects", "#44", 0, ""),
    ("b_granted", "#45", 0, ""),
    ("b_item_spell0", "#48", 0, ""),
    ("b_haste", "#49", 0, ""),
    ("b_sheet", "#50", 0, ""),
    ("b_hammer", "#51", 0, ""),
    ("b_amount", "#52", 0, ""),
    # C: places
    ("c_throtl", "#27 #53 #93 #94", 1, WALLS),
    ("c_map80", "#94", 0, ""),
    ("c_pit", "#30 #35 #105", 1, WALLS),
    ("c_chest", "#58-62", 1, WALLS + "; " + STOP),
    ("c_rob", "#56", 1, STOP),
    ("c_outpost", "#29 #66 #68-71", 0, ""),
    ("c_block50", "#67 #72 #103", 0, ""),
    ("c_wander", "#54 #95", 1, WALLS + "; the close-up's frames cok does not present"),
    ("c_temple33", "#55 #54", 1, WALLS),
    ("c_catacombs", "#96 #97", 1, STOP),
    ("c_overland", "#98-102", 1, STOP),
    ("c_npc", "#104 #106", 1, WALLS),
    ("c_ecl3", "#64 #65", 1, STOP),
    # D: combat
    ("d_cloud", "#6", 0, ""),
    ("d_lebaum", "#9 (#23)", 0, ""),
    ("d_aurak", "#12", 0, ""),
    ("d_done", "#16", 0, ""),
    ("d_move", "#17 #92", 0, ""),
    ("d_cast", "#22 #119 #128", 0, ""),
    ("d_space", "#24", 0, ""),
    ("d_altq", "#25", 0, ""),
    ("d_minotaurs", "#57", 0, ""),
    ("d_flee", "#63", 0, ""),
    ("d_gate", "#74 #83 #84 #88", 0, ""),
    ("d_ranks", "#75", 0, ""),
    ("d_patrol", "#76 #79", 0, ""),
    ("d_neraka", "#77", 0, ""),
    ("d_dragonfear", "#78", 0, ""),
    ("d_dying", "#80", 0, ""),
    ("d_helm", "#81 #82", 0, ""),
    ("d_arrows", "#89", 0, ""),
    ("d_bozak", "#90", 0, ""),
    ("d_weapon", "#91", 0, ""),
    ("d_hold", "#120", 0, ""),
    ("d_fear", "#122 (#121)", 0, ""),
    ("d_bolt", "#127", 0, ""),
    ("d_turning", "#129", 0, ""),
    ("d_firstblock", "Begin's vectors", 1, VECTORS),
    # E: freed and garbage memory
    ("e_cloud_dispel", "#7", 0, ""),
    ("e_bolt_bounce", "#11", 0, ""),
    ("e_debug_readonly", "#14", 0, ""),
    ("e_manual", "#15 #18", 0, ""),
    ("e_strength", "#46", 0, ""),
    ("e_trade", "#47 #73", 0, ""),
    # F: display modes, sound, mouse, frames
    ("f_sound", "#3 #4", 1, "steps close together merge otherwise in eclplay's clock"),
    ("f_mouse", "#20", 1, STOP + "; DOSBox-X's CGA cursor; the mark under the arrow vanishes"),
    ("f_loading", "#28", 0, ""),
    ("f_display_modes", "#123 #124", 1, WALLS + "; " + VECTORS),
    ("f_frames", "#126", 0, ""),
    # phase 1's proof checks
    ("load_game_screen", "#26 #29", 1, STOP),
    ("save_again", "#31", 0, ""),
    ("slow_poison", "#8", 0, ""),
]

# Minutes each took in a full run (the rest a few each), for the order.
LONG = {"f_frames": 11, "a_demo": 6, "f_sound": 6, "d_bozak": 6, "f_display_modes": 5,
        "a_ctrl_s": 3, "d_cast": 3, "e_bolt_bounce": 2, "f_mouse": 2, "e_manual": 2}

# Checks that catch a text shown for a few seconds in a burst of screenshots
# (d_lib.burst): run alone after the rest, as with others running beside them
# on a loaded host DOSBox-X wrote ten times fewer and missed some.
ALONE = {"d_arrows", "d_bolt", "d_cast", "d_gate", "d_hold", "d_turning"}


def displays():
    span = os.environ.get("COKBOX_DISPLAYS")
    if not span:
        return None
    lo, _, hi = span.partition("-")
    return int(hi or lo) - int(lo) + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--only", nargs="*", help="these scripts (names without .py)")
    parser.add_argument("--out", default=str(ROOT / "build" / "dosbox-checks"))
    parser.add_argument("--timeout", type=float, default=3 * 3600,
                        help="seconds a script may take (default three hours)")
    opts = parser.parse_args()
    n = displays()
    jobs = opts.jobs or (max(1, n - 2) if n else 6)
    todo = [c for c in CHECKS if not opts.only or c[0] in opts.only]
    unknown = set(opts.only or ()) - {c[0] for c in CHECKS}
    if unknown:
        parser.error(f"no such check script: {', '.join(sorted(unknown))}")
    out = pathlib.Path(opts.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Built once here, not by several scripts at once.
    if any(name.startswith("d_") for name, *_ in todo):
        span = os.environ.get("COKBOX_DISPLAYS")
        import d_lib  # sets COKBOX_DISPLAYS to batch D's range when unset
        d_lib.pause_player()
        if span is None:
            os.environ.pop("COKBOX_DISPLAYS", None)

    results, lock = {}, threading.Lock()
    # The long ones first, so that they do not end the run alone.
    queue = sorted((c for c in todo if c[0] not in ALONE), key=lambda c: -LONG.get(c[0], 0))
    alone = [c for c in todo if c[0] in ALONE]
    t0 = time.monotonic()

    def worker():
        while True:
            with lock:
                if not queue:
                    return
                name, checks, want, why = queue.pop(0)
            start = time.monotonic()
            with open(out / f"{name}.log", "wb") as log:
                try:
                    code = subprocess.run(
                        [sys.executable, str(HERE / f"{name}.py"), "--workdir", str(out / name)],
                        cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                        timeout=opts.timeout).returncode
                except subprocess.TimeoutExpired:
                    code = None
            took = time.monotonic() - start
            with lock:
                results[name] = (code, took)
                print(line(name, checks, want, why, code, took), flush=True)

    print(f"{len(todo)} scripts, {jobs} at a time, then {len(alone)} alone; logs in {out}; "
          f"load average {os.getloadavg()[0]:.0f}", flush=True)
    threads = [threading.Thread(target=worker) for _ in range(jobs)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    queue.extend(alone)
    worker()
    print(f"\nall done in {(time.monotonic() - t0) / 60:.0f} min (load average "
          f"{os.getloadavg()[0]:.0f}); in the table's order:")
    changed = 0
    for name, checks, want, why in todo:
        code, took = results[name]
        print(line(name, checks, want, why, code, took))
        changed += code != want
    print(f"{len(todo) - changed} as recorded, {changed} changed")
    return 1 if changed else 0


def line(name, checks, want, why, code, took):
    verdict = {0: "agrees", 1: "differs", None: "TIMED OUT"}.get(code, f"ERROR (exit {code})")
    note = "as recorded" if code == want else f"CHANGED (recorded: {'differs' if want else 'agrees'})"
    known = f" [known: {why}]" if want == 1 and code == 1 else ""
    return f"{name:20} {checks:18} {verdict:14} {took / 60:5.1f} min  {note}{known}"


if __name__ == "__main__":
    sys.exit(main())
