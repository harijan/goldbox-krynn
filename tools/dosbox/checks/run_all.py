#!/usr/bin/env python3
"""Run every DOSBox-X check script (tools/dosbox/checks/) and print one line
per script: docs/checks.md's checks it covers, its verdict, and whether that is
the verdict recorded (docs/checks.md, "Checks against the original").

    python3 tools/dosbox/checks/run_all.py [--set SET] [--jobs N] [--only NAME ...] [--flows F]
        [--out DIR]
    python3 tools/dosbox/checks/run_all.py --report [--set SET] [--out DIR]

SET: champions (CHECKS, the default), dkk (DKK_CHECKS: Death Knights'
scripts, the long ones one per flow; make dkk-checks), all (both), or dqk
(DQK_CHECKS: The Dark Queen of Krynn's, dqk_fight, dqk_treasure and dqk_create one per flow;
make dqk-checks).

Each script runs with --workdir OUT/NAME (default OUT build/dosbox-checks),
its output in OUT/NAME.log, and ends with a line `VERDICT: AGREES` or
`VERDICT: DIFFERS new (...)`, with exit status 0 or 1.
Anything else (another status, no verdict line, a timeout, a crash) is an
ERROR, never a recorded verdict. COKBOX_DISPLAYS (e.g. 180-189) keeps the X
displays to a range; --jobs defaults to the range's size less two, else to
five eighths of the cores (10 of 16; see default_jobs). A script not as
recorded runs once more, alone, at the end (unless --no-again): the
verdict alone is the one that counts, its first shown beside it.
Needs `make build/eclplay build/krynn build/START_FULL.EXE` (make dosbox-checks
does it; `make dosbox-checks ARGS="--only b_amount"` passes options). Exit 0
when every script gave its recorded verdict. --only SCRIPT of a split script runs it whole, as one
row (with --flows F, those flows); --only SCRIPT-FLOW one of its rows.

Telemetry, for speeding the checks up: each script's trace in
OUT/NAME.trace.jsonl (tools/dosbox/telemetry.py: its boxes, waits,
screenshots, port runs and builds, and the lines its time went to), the
machine's load every two seconds in OUT/load.jsonl, when each script ran
in OUT/schedule.json; read together into OUT/telemetry.txt at the end
(--report writes it again from them)."""

import argparse
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

import telemetry_report

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]

# (script, the checks it covers, the verdict recorded: "" agrees, or "new"
# for a port difference found and recorded in docs/checks.md but not yet fixed;
# a fix of the port then makes it "").
CHECKS = [
    # A: startup, title, setup, the start menu
    ("a_setup", "#1", ""),
    ("a_ctrl_s", "#5", ""),
    ("a_debug", "#13", ""),
    ("a_debug_battle", "#13 (battle)", ""),
    ("a_demo", "#107 #108", ""),
    ("a_unsaved", "#109", ""),
    ("a_create", "#110 #111", ""),
    ("a_icon", "#112", ""),
    ("a_roster", "#113 #114 #115", ""),
    ("a_hall", "#116 #117", ""),
    ("a_modify", "#118", ""),
    # B: camp, magic, items, effects, saving
    ("b_slow_poison_63h", "#8", ""),
    ("b_timers", "#8", ""),
    ("b_thief_skills", "#32", ""),
    ("b_rest_day_effects", "#33", ""),
    ("b_effect_order", "#34", ""),
    ("b_camp_screen", "#36 #37", ""),
    ("b_resume", "#38", ""),
    ("b_fix_time", "#39", ""),
    ("b_heal_day", "#40", ""),
    ("b_scribe", "#41", ""),
    ("b_pics", "#42", ""),
    ("b_grimoire", "#43", ""),
    ("b_display_effects", "#44", ""),
    ("b_granted", "#45", ""),
    ("b_item_spell0", "#48", ""),
    ("b_haste", "#49", ""),
    ("b_sheet", "#50", ""),
    ("b_hammer", "#51", ""),
    ("b_amount", "#52", ""),
    # C: places
    ("c_throtl", "#27 #53 #93 #94", ""),
    ("c_map80", "#94", ""),
    ("c_pit", "#30 #35 #105", ""),
    ("c_chest", "#58-62", ""),
    ("c_rob", "#56", ""),
    ("c_outpost", "#29 #66 #68-71", ""),
    ("c_block50", "#67 #72 #103", ""),
    ("c_wander", "#54 #95", ""),
    ("c_temple33", "#55 #54", ""),
    ("c_catacombs", "#96 #97", ""),
    ("c_overland", "#98-102", ""),
    ("c_npc", "#104 #106", ""),
    ("c_ecl3", "#64 #65", ""),
    # D: combat
    ("d_cloud", "#6", ""),
    ("d_lebaum", "#9 (#23)", ""),
    ("d_aurak", "#12", ""),
    ("d_done", "#16", ""),
    ("d_move", "#17 #92", ""),
    ("d_cast", "#22 #119 #128", ""),
    ("d_space", "#24", ""),
    ("d_altq", "#25", ""),
    ("d_minotaurs", "#57", ""),
    ("d_flee", "#63", ""),
    ("d_gate", "#74 #83 #84 #88", ""),
    ("d_ranks", "#75", ""),
    ("d_patrol", "#76 #79", ""),
    ("d_neraka", "#77", ""),
    ("d_dragonfear", "#78", ""),
    ("d_dying", "#80", ""),
    ("d_helm", "#81 #82", ""),
    ("d_arrows", "#89", ""),
    ("d_bozak", "#90", ""),
    ("d_weapon", "#91", ""),
    ("d_hold", "#120", ""),
    ("d_fear", "#122 (#121)", ""),
    ("d_bolt", "#127", ""),
    ("d_turning", "#129", ""),
    ("d_firstblock", "Begin's vectors", ""),
    # E: freed and garbage memory
    ("e_cloud_dispel", "#7", ""),
    ("e_bolt_bounce", "#11", ""),
    ("e_debug_readonly", "#14", ""),
    ("e_manual", "#15 #18", ""),
    ("e_strength", "#46", ""),
    ("e_trade", "#47 #73", ""),
    ("e_kender", "#130", ""),
    ("e_freed_selection", "#131", ""),
    # F: display modes, sound, mouse, frames
    ("f_sound", "#3 #4", ""),
    ("f_mouse", "#20", ""),
    ("f_loading", "#28", ""),
    ("f_display_modes", "#123 #124", ""),
    ("f_frames", "#126", ""),
    # phase 1's proof checks
    ("load_game_screen", "#26 #29", ""),
    ("save_again", "#31", ""),
    ("slow_poison", "#8", ""),
    # Built once here, not by several scripts at once.
    # The long ones first, so that they do not end the run alone.
]

# Death Knights of Krynn's (--set dkk; docs/games.md, "Death Knights"): the
# long scripts split by their --flows, each flow a script of its own here
# (NAME-FLOW: its own box, workdir and log), so that they run side by side.
DKK_SPLIT = {
    "dkk_fight": ["fight", "lost", "river", "ambush", "auto", "keys", "keys2", "cast", "items",
                  "drain", "mage", "dragon", "spells", "priests", "band", "clouds"],
    "dkk_levels": ["train", "memorize", "table", "cast", "fix", "rest", "stop", "interrupted",
                   "menus", "saves", "load"],
    "dkk_treasure": ["treasure", "vault"],
    "dkk_menus": ["lists", "ecl", "post", "clicks", "taskclicks"],
}
DKK_SCRIPTS = ["dkk_views", "dkk_start", "dkk_create", "dkk_treasure", "dkk_items", "dkk_shops",
               "dkk_levels", "dkk_mouse", "dkk_setup", "dkk_fight", "dkk_menus", "dkk_sound",
               "dkk_trade"]
DKK_CHECKS = [(f"{script}-{flow}" if flow else script, "DKK", "") for script in DKK_SCRIPTS
              for flow in DKK_SPLIT.get(script, [""])]
# The Dark Queen of Krynn's (--set dqk; DARK-QUEEN.md): the original's
# start, its first screen, the title, texts and the 3D view, its menus,
# input and big picture, its saved game, camp and party to the first
# fight against eclplay --game dark-queen.
# The long scripts split by their --flows (dqk_fight's defaults, dqk_treasure's and dqk_create's
# flows), each flow a row of its own, as DKK_SPLIT.
DQK_SPLIT = {
    "dqk_fight": ["setup", "load", "clear", "six", "ground", "encounter", "field", "wild", "slots",
                  "icons", "dispatch", "keys", "altq_missile", "turns", "round", "melee", "melee-long",
                  "melee-dying", "won", "drop", "timer", "walk", "mouse", "move", "aim", "turn", "cast",
                  "cast_auto", "cast_auto_area", "band_a1", "band_a2", "band_a3", "band_b1", "band_b2",
                  "band_b3", "band_b4", "band_b5", "band_c1", "band_c2", "band_c4", "effects_1", "effects_2", "effects_4", "effects_3"],
    "dqk_treasure": ["won", "fled", "lost", "pool", "bonus", "zero", "negative", "rest", "gentle",
                     "fledmix", "enemy", "enemyout", "enemyhigh", "take", "share", "gather", "overload",
                     "takeall", "items", "takeitems", "takeboth", "lost9"],
    "dqk_create": ["fighter", "cleric", "dwarf", "kender", "multi", "modify", "knights", "casters",
                   "exits"],
}
DQK_SCRIPTS = ["dqk_boot", "dqk_views", "dqk_menus", "dqk_party", "dqk_view_mouse", "dqk_items",
               "dqk_train", "dqk_add", "dqk_modify", "dqk_stats", "dqk_create", "dqk_ecl",
               "dqk_ecl_input", "dqk_ecl_text", "dqk_ecl_flow", "dqk_ecl_misc", "dqk_ecl_party", "dqk_ecl_items",
               "dqk_fight", "dqk_program0", "dqk_camp", "dqk_shops", "dqk_treasure", "dqk_bar"]
DQK_CHECKS = [(f"{script}-{flow}" if flow else script, "DQK", "") for script in DQK_SCRIPTS
              for flow in DQK_SPLIT.get(script, [""])]
SETS = {"champions": CHECKS, "dkk": DKK_CHECKS, "all": CHECKS + DKK_CHECKS, "dqk": DQK_CHECKS}


# --flows FLOWS: passed to each script run that is not split (with --only, one script's flows).
FLOWS = None


def command(name):
    """A row's script and its arguments: NAME.py, or a split one's
    SCRIPT.py --flows FLOW (else run_all's --flows, if given)."""
    script, _, flow = name.partition("-")
    flow = flow or FLOWS
    return [str(HERE / f"{script}.py")] + (["--flows", flow] if flow else [])


# Minutes each took in a full run (the rest one or less; OUT/telemetry.txt
# has them all), for the order.
LONG = {"a_demo": 5, "f_frames": 4, "f_sound": 3, "c_wander": 2, "e_bolt_bounce": 2,
        "d_bozak": 2, "d_cast": 2, "c_catacombs": 1, "f_loading": 1, "e_cloud_dispel": 1,
        "f_display_modes": 1, "b_item_spell0": 1, "d_bolt": 1, "d_aurak": 1, "d_gate": 1,
        "dkk_create": 8, "dkk_views": 5, "dkk_fight-spells": 5, "dkk_shops": 5,
        "dkk_fight-clouds": 4, "dkk_fight-cast": 3, "dkk_fight-band": 3, "dkk_fight-auto": 3,
        "dkk_fight-mage": 3, "dkk_start": 3, "dkk_mouse": 3, "dkk_items": 3, "dkk_sound": 5,
        "dqk_boot": 1, "dqk_views": 1, "dqk_menus": 1, "dqk_party": 1, "dqk_view_mouse": 1,
        "dqk_ecl": 1, "dqk_ecl_input": 3, "dqk_ecl_text": 1, "dqk_ecl_flow": 2, "dqk_ecl_misc": 1, "dqk_ecl_party": 1, "dqk_ecl_items": 1, "dqk_program0": 1, "dqk_camp": 1, "dqk_shops": 1,
        "dqk_items": 2, "dqk_train": 2, "dqk_add": 4, "dqk_modify": 3, "dqk_stats": 1,
        "dqk_create-fighter": 2, "dqk_create-cleric": 2, "dqk_create-dwarf": 2,
        "dqk_create-kender": 3, "dqk_create-multi": 2, "dqk_create-modify": 2, "dqk_create-knights": 4,
        "dqk_create-casters": 4, "dqk_create-exits": 3,
        "dqk_fight-aim": 5, "dqk_fight-keys": 4, "dqk_fight-cast": 2, "dqk_fight-melee-dying": 2,
        "dqk_fight-melee-long": 2, "dqk_fight-field": 1, "dqk_fight-move": 1, "dqk_fight-turn": 1,
        "dqk_fight-walk": 1, "dqk_fight-icons": 1, "dqk_fight-altq_missile": 1}
VERDICT = re.compile(r"^VERDICT: (AGREES|DIFFERS new.*|ERROR.*)$", re.M)


def default_jobs():
    """Scripts side by side without --jobs or COKBOX_DISPLAYS: five eighths
    of the cores (10 of 16). On 16 cores, with the port's runs and builds
    niced below the boxes (cokbox.BELOW_BOXES), the part side by side took
    8 minutes at 10, 7 at 12 and 14 (it lasts about as long as its longest
    script, a_demo), 13 at 6; at 10 and up, about one run in three had one
    script differ, a different one each time (a_demo, c_pit, d_altq: they
    judge the original's screens or waits as they come), which agreed when
    run again alone (see main)."""
    return max(1, (os.cpu_count() or 8) * 5 // 8)


def displays():
    span = os.environ.get("COKBOX_DISPLAYS")
    if not span:
        return None
    lo, _, hi = span.partition("-")
    return int(hi or lo) - int(lo) + 1


def kill_pids(path):
    """Kill the Xvfb and DOSBox-X a check started (cokbox records them in
    COKBOX_PIDFILE), if still there."""
    try:
        pids = {int(p) for p in path.read_text().split()}
    except (OSError, ValueError):
        return
    for pid in pids:
        try:
            cmd = pathlib.Path(f"/proc/{pid}/cmdline").read_bytes()
        except OSError:
            continue
        if b"Xvfb" in cmd or b"dosbox-x" in cmd:
            try:
                os.killpg(pid, signal.SIGKILL)
            except OSError:
                pass


# The scripts running now: {name: (Popen, pid file)}, for stop().
RUNNING = {}
RUNNING_LOCK = threading.Lock()
STOPPING = threading.Event()


def stop_group(proc, pidfile):
    """End a script: SIGTERM to its process group (its handler closes its
    boxes), SIGKILL after 15 s, then the boxes' Xvfb and DOSBox-X it
    recorded."""
    try:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(15)
    except (OSError, subprocess.TimeoutExpired):
        pass
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except OSError:
        pass
    proc.wait()
    kill_pids(pidfile)


def stop(signum, frame):
    """run_all ended by a signal: every running script's group ended too,
    and no other started."""
    STOPPING.set()
    with RUNNING_LOCK:
        running = list(RUNNING.values())
    for proc, pidfile in running:
        stop_group(proc, pidfile)
    sys.exit(128 + signum)


def judge_log(text, code):
    """The verdict of a script that ended with status CODE and wrote TEXT:
    `AGREES` (status 0), `DIFFERS new ...` (status 1), from its last
    VERDICT line; None (an error) for anything else."""
    found = VERDICT.findall(text)
    verdict = found[-1] if found else None
    if code == 0 and verdict == "AGREES":
        return verdict
    if code == 1 and verdict is not None and verdict.startswith("DIFFERS new"):
        return verdict
    return None


def flows_problem(flows, only):
    """The error for --flows FLOWS with --only ONLY, or None: the flows go to
    one --only script that is not split (a split row has its own flow), since
    every other script would get --flows it does not know."""
    if not flows or (only and len(only) == 1 and "-" not in only[0]):
        return None
    return "--flows needs exactly one --only script, not split (e.g. --only dqk_fight --flows walk)"


def selected(rows, only):
    """The rows --only ONLY picks from ROWS (all of them without it), and the names in ONLY
    that are no row and no script: a split script's own name (dqk_fight) is one row of the
    whole script (its workdir OUT/dqk_fight; --flows run_all's, else the script's own
    defaults), as before the split, e.g. --only dqk_fight --flows walk."""
    names = {c[0] for c in rows}
    split = {}
    for name, checks, _ in rows:
        script, _, flow = name.partition("-")
        if flow:
            split.setdefault(script, checks)
    todo = [c for c in rows if not only or c[0] in only]
    todo += [(name, split[name], "") for name in dict.fromkeys(only or ())
             if name not in names and name in split]
    return todo, set(only or ()) - names - set(split)


def self_test():
    """judge_log and recorded_as on made-up logs; no DOSBox-X needed."""
    flow_cases = [
        (None, None, True), ("", None, True), ("walk", None, False), ("walk", [], False),
        ("walk", ["dqk_fight", "dqk_camp"], False), ("walk", ["dqk_fight-turns"], False),
        ("walk", ["dqk_fight"], True), (None, ["dqk_fight", "dqk_camp"], True),
    ]
    flow_bad = sum((flows_problem(flows, only) is None) != ok for flows, only, ok in flow_cases)
    print(f"run_all --flows self-test: {len(flow_cases) - flow_bad} of {len(flow_cases)} cases right")
    # --only on the rows: a split script's name picks its whole row, its flows' names their rows
    selection = [
        (DQK_CHECKS, None, {c[0] for c in DQK_CHECKS}, set()),
        (DQK_CHECKS, ["dqk_fight"], {"dqk_fight"}, set()),
        (DQK_CHECKS, ["dqk_fight-aim"], {"dqk_fight-aim"}, set()),
        (DQK_CHECKS, ["dqk_fight", "dqk_boot"], {"dqk_fight", "dqk_boot"}, set()),
        (DQK_CHECKS, ["dqk_treasure"], {"dqk_treasure"}, set()),
        (DQK_CHECKS, ["dqk_nope", "dqk_fight-nope"], set(), {"dqk_nope", "dqk_fight-nope"}),
        (DKK_CHECKS, ["dkk_fight"], {"dkk_fight"}, set()),
    ]
    sel_bad = 0
    for rows, only, want, want_unknown in selection:
        todo, unknown = selected(rows, only)
        if {c[0] for c in todo} != want or unknown != want_unknown:
            sel_bad += 1
            print(f"self-test FAILS: --only {only}: rows {sorted(c[0] for c in todo)}, "
                  f"unknown {sorted(unknown)}")
    if selected(DQK_CHECKS, ["dqk_fight"])[0] != [("dqk_fight", "DQK", "")]:
        sel_bad += 1
        print("self-test FAILS: --only dqk_fight is not the whole row")
    print(f"run_all self-test of --only: {len(selection) + 1 - sel_bad} of {len(selection) + 1} "
          "cases right")
    # DQK_SPLIT's lists are the flows of dqk_fight and dqk_treasure (imported here: cokbox)
    import dqk_fight
    import dqk_treasure
    split_bad = 0
    for script, flows in (("dqk_fight", dqk_fight.DEFAULT_FLOWS), ("dqk_treasure", dqk_treasure.FLOWS)):
        if DQK_SPLIT[script] != list(flows):
            split_bad += 1
            diff = sorted(set(DQK_SPLIT[script]) ^ set(flows)) or "the same names, in another order"
            print(f"self-test FAILS: DQK_SPLIT[{script!r}] is not the script's flows: {diff}")
    print(f"run_all self-test of DQK_SPLIT: {2 - split_bad} of 2 cases right")
    cases = [
        ("  agrees: x\nVERDICT: AGREES\n", 0, "AGREES", ""),
        ("DIFFERENCE: x\nVERDICT: DIFFERS new (1: x)\n", 1, "DIFFERS new (1: x)", "new"),
        ("VERDICT: AGREES\n", 1, None, None),                 # wrong status
        ("VERDICT: DIFFERS new (1: x)\n", 0, None, None),     # wrong status
        ("VERDICT: AGREES\n", 3, None, None),
        ("Traceback ...\nVERDICT: ERROR (status 3)\n", 3, None, None),
        ("VERDICT: ERROR (status 2)\n", 1, None, None),       # an error's line
        ("agrees: everything\n", 0, None, None),               # no verdict
        ("", 1, None, None),
        ("", "timeout", None, None),
        ("VERDICT: DIFFERS new (1: x)\nVERDICT: AGREES\n", 0, "AGREES", ""),   # the last
        ("VERDICT: AGREESX\n", 0, None, None),
        (" VERDICT: AGREES\n", 0, None, None),                # not at a line's start
    ]
    bad = 0
    for text, code, want, recorded in cases:
        got = judge_log(text, code)
        rec = recorded_as(got) if got is not None else None
        if got != want or rec != recorded:
            bad += 1
            print(f"self-test FAILS: status {code!r}, log {text!r}: {got!r}/{rec!r}, "
                  f"wanted {want!r}/{recorded!r}")
    print(f"run_all self-test: {len(cases) - bad} of {len(cases)} cases right")
    bad += self_test_again() + flow_bad + sel_bad + split_bad
    return 1 if bad else 0


def self_test_again():
    """run_checks' run again alone, with made-up scripts: a flaky one (differs,
    then agrees alone) ends as recorded and is named; a real difference
    (differs both times) and an error twice stay changed, exit 1; with
    again=False the first verdicts stand. Returns the failures' count."""
    import contextlib
    import io
    import tempfile

    def runner(script):
        calls = {}

        def run(name, out):
            calls[name] = calls.get(name, 0) + 1
            (out / f"{name}.log").write_text(f"run {calls[name]}\n")
            (out / name).mkdir(exist_ok=True)
            got = script[name][min(calls[name], len(script[name])) - 1]
            return (got, 0 if got == "AGREES" else 1) if got else (None, 3)
        return run, calls

    rows = [c for c in CHECKS if c[0] in ("b_amount", "c_rob", "d_move")]
    cases = [  # (each script's verdicts run after run, again, status, summary's end)
        ({"b_amount": ["DIFFERS new (1: x)", "AGREES"], "c_rob": ["AGREES"],
          "d_move": ["AGREES"]}, True, 0, "(run again alone: b_amount)"),
        ({"b_amount": ["AGREES"], "c_rob": ["DIFFERS new (1: y)"], "d_move": [None]},
         True, 1, "(run again alone: c_rob, d_move)"),
        ({"b_amount": ["DIFFERS new (1: x)", "AGREES"], "c_rob": ["AGREES"],
          "d_move": ["AGREES"]}, False, 1, "changed or in error"),
    ]
    bad = 0
    for script, again, want, summary in cases:
        run, calls = runner(script)
        with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()) as o:
            got = run_checks(rows, 2, pathlib.Path(d), run, again=again)
            kept = (pathlib.Path(d) / "b_amount.first.log").exists()
        text = o.getvalue().splitlines()
        flaky = again and len(script["b_amount"]) > 1
        if (got != want or not text[-1].endswith(summary) or kept != flaky or
                (flaky and "at first: differs new" not in "\n".join(text))):
            bad += 1
            print(f"self-test FAILS: run again {again}, {script}: status {got}, {text[-1]!r}")
    print(f"run_all self-test of the run again: {len(cases) - bad} of {len(cases)} cases right")
    # An earlier run's NAME.first.* (a retry's leftovers) is removed before the first
    # pass: here every script agrees the first time, so none of them may survive.
    run, _ = runner({"b_amount": ["AGREES"], "c_rob": ["AGREES"], "d_move": ["AGREES"]})
    with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()):
        d = pathlib.Path(d)
        stale = [d / "b_amount.first.log", d / "b_amount.first.trace.jsonl", d / "b_amount.first"]
        stale[0].write_text("an earlier run's retry\n")
        stale[1].write_text("{}\n")
        stale[2].mkdir()
        got = run_checks(rows, 2, d, run, again=True)
        left = [p.name for p in stale if p.exists()]
    if got != 0 or left:
        bad += 1
        print(f"self-test FAILS: stale files of an earlier run left: {left}, status {got}")
    else:
        print("run_all self-test of the stale files: 1 of 1 cases right")
    return bad


def run_one(name, out, timeout):
    """Run one script: (verdict text, status) with verdict None on an error."""
    pidfile = out / f"{name}.pids"
    pidfile.unlink(missing_ok=True)
    trace = out / f"{name}.trace.jsonl"
    trace.unlink(missing_ok=True)
    env = dict(os.environ, COKBOX_PIDFILE=str(pidfile), COKBOX_TRACE=str(trace))
    with open(out / f"{name}.log", "wb") as log:
        with RUNNING_LOCK:
            if STOPPING.is_set():
                return None, "stopped"
            proc = subprocess.Popen(
                [sys.executable, *command(name), "--workdir", str(out / name)],
                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, env=env,
                start_new_session=True)
            RUNNING[name] = (proc, pidfile)
        try:
            code = proc.wait(timeout)
        except subprocess.TimeoutExpired:
            stop_group(proc, pidfile)
            code = "timeout"
        finally:
            with RUNNING_LOCK:
                RUNNING.pop(name, None)
    kill_pids(pidfile)
    return judge_log((out / f"{name}.log").read_text(errors="replace"), code), code


def recorded_as(verdict):
    return "" if verdict == "AGREES" else "new" if verdict.startswith("DIFFERS new") else None


def shown_as(verdict, code):
    if verdict is None:
        return f"ERROR ({'timed out' if code == 'timeout' else f'status {code}'})"
    return "agrees" if verdict == "AGREES" else verdict.split(" (")[0].lower()


def line(name, checks, want, verdict, code, took, first=None):
    """A script's line; FIRST: its first (verdict, code, took) when it ran
    again alone."""
    shown, note = shown_as(verdict, code), ""
    if verdict is not None:
        got = recorded_as(verdict)
        note = ("as recorded" if got == want else
                f"CHANGED (recorded: {'agrees' if not want else 'differs new'})")
    if first is not None:
        note += f"; run again alone, at first: {shown_as(*first[:2])}"
    return f"{name:20} {checks:18} {shown:28} {took / 60:5.1f} min  {note}"


def as_recorded(want, result):
    verdict = result[0]
    return verdict is not None and recorded_as(verdict) == want


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--set", choices=sorted(SETS), default="champions",
                        help="Champions' scripts (the default), Death Knights' (dkk), both "
                             "(all) or Dark Queen's (dqk)")
    parser.add_argument("--only", nargs="*", help="these scripts (names without .py)")
    parser.add_argument("--flows", help="the flows of the scripts run (their --flows; with --only "
                                        "a script that has them, e.g. --only dqk_fight --flows walk)")
    parser.add_argument("--out", default=str(ROOT / "build" / "dosbox-checks"))
    parser.add_argument("--timeout", type=float, default=3 * 3600,
                        help="seconds a script may take (default three hours)")
    parser.add_argument("--no-again", action="store_true",
                        help="keep each script's first verdict (no run again alone)")
    parser.add_argument("--self-test", action="store_true",
                        help="check the verdict and status parsing only (no DOSBox-X)")
    parser.add_argument("--report", action="store_true",
                        help="write OUT/telemetry.txt again from the last run's files")
    opts = parser.parse_args()
    global FLOWS
    FLOWS = opts.flows
    if opts.self_test:
        return self_test()
    if opts.report:
        print(telemetry_report.write(pathlib.Path(opts.out).resolve(),
                                     [c[0] for c in SETS[opts.set]]))
        return 0
    n = displays()
    jobs = opts.jobs or (max(1, n - 2) if n else default_jobs())
    rows = SETS[opts.set]
    todo, unknown = selected(rows, opts.only)
    if unknown:
        parser.error(f"no such check script: {', '.join(sorted(unknown))}")
    problem = flows_problem(opts.flows, opts.only)
    if problem:
        parser.error(problem)
    out =pathlib.Path(opts.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    # The port's builds the checks share (checklib.build), once here.
    sys.path.insert(0, str(HERE))
    import checklib
    if opts.set != "dqk":  # Dark Queen's compare with no port build yet
        checklib.stopping_eclplay()
        checklib.eclplay_with_pauses()
    # After importing checklib: cokbox sets its own SIGTERM handler.
    for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(signum, stop)
    sampler = telemetry_report.LoadSampler(out / "load.jsonl", lambda: sorted(RUNNING))
    sampler.start()
    try:
        code = run_checks(todo, jobs, out, lambda name, out: run_one(name, out, opts.timeout),
                          again=not opts.no_again)
    finally:
        sampler.stop()
    print("\n" + telemetry_report.write(out, [c[0] for c in todo], jobs))
    return code


def run_checks(todo, jobs, out, run, again=True):
    """Run TODO's scripts (CHECKS' rows) with RUN(name, out) -> (verdict,
    code), JOBS at a time, then (with AGAIN) those
    not as recorded once more, alone; print the table. Returns the exit
    status: 1 if any is not as recorded at the end."""
    results, lock = {}, threading.Lock()
    schedule = []   # (name, start, took, alone) for the telemetry's report
    first = {}      # the first (verdict, code, took) of those run again alone
    # The long ones first, so that they do not end the run alone.
    queue = sorted(todo, key=lambda c: -LONG.get(c[0], 0))
    t0 = time.monotonic()
    # A leftover of an earlier run's retry (NAME.first.*) is not this run's first
    # attempt: removed before the first pass, so that it is never read as one.
    for name, _, _ in todo:
        for stale in (out / f"{name}.first.log", out / f"{name}.first.trace.jsonl",
                      out / f"{name}.first"):
            if stale.is_dir():
                shutil.rmtree(stale)
            elif stale.exists():
                stale.unlink()

    def worker():
        while True:
            with lock:
                if not queue or STOPPING.is_set():
                    return
                name, checks, want = queue.pop(0)
            start = time.monotonic()
            try:
                verdict, code = run(name, out)
            except Exception as e:  # noqa: BLE001 (an error of the runner's: reported)
                verdict, code = None, f"runner: {e}"
            took = time.monotonic() - start
            with lock:
                results[name] = (verdict, code, took)
                schedule.append({"name": name, "start": round(start - t0, 1),
                                 "took": round(took, 1), "alone": bool(first)})
                print(line(name, checks, want, verdict, code, took), flush=True)

    print(f"{len(todo)} scripts, {jobs} at a time; logs in {out}; "
          f"load average {os.getloadavg()[0]:.0f}", flush=True)
    def work(count):
        # Joined with a timeout, so that the main thread takes a signal (stop).
        threads = [threading.Thread(target=worker, daemon=True) for _ in range(count)]
        for t in threads:
            t.start()
        while any(t.is_alive() for t in threads):
            for t in threads:
                t.join(0.5)

    work(jobs)
    # A script not as recorded runs once more, alone (its first log and
    # folder kept as NAME.first.log, NAME.first): some judge the original's
    # screens or its waits as they come, and now and then one of them
    # differed beside others, a different one each run (docs/checks.md,
    # Checks against the original); a difference of the port's differs
    # again. The verdict alone counts; the line and the summary show the
    # first too, so that a script that keeps needing it stands out. A
    # script that timed out gets the whole timeout again.
    again = [] if not again or STOPPING.is_set() else [
        c for c in todo if c[0] in results and not as_recorded(c[2], results[c[0]])]
    if again:
        print(f"\n{len(again)} not as recorded: each again, alone", flush=True)
        for name, _, _ in again:
            first[name] = results.pop(name)
            for old, new in ((out / f"{name}.log", out / f"{name}.first.log"),
                             (out / f"{name}.trace.jsonl", out / f"{name}.first.trace.jsonl"),
                             (out / name, out / f"{name}.first")):
                if new.is_dir():
                    shutil.rmtree(new)
                if old.exists():
                    os.replace(old, new)
        queue.extend(again)
        work(1)
    (out / "schedule.json").write_text(json.dumps(
        {"jobs": jobs, "wall": round(time.monotonic() - t0, 1), "runs": schedule}, indent=1))
    print(f"\nall done in {(time.monotonic() - t0) / 60:.0f} min (load average "
          f"{os.getloadavg()[0]:.0f}); in the table's order:")
    changed = 0
    for name, checks, want in todo:
        verdict, code, took = results.get(name, (None, "not run", 0))
        print(line(name, checks, want, verdict, code, took, first.get(name)))
        changed += verdict is None or recorded_as(verdict) != want
    print(f"{len(todo) - changed} as recorded, {changed} changed or in error"
          + (f" (run again alone: {', '.join(n for n, _, _ in todo if n in first)})"
             if first else ""))
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
