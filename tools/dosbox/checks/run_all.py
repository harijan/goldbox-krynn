#!/usr/bin/env python3
"""Run every DOSBox-X check script (tools/dosbox/checks/) and print one line
per script: DEVELOPERS.md's checks it covers, its verdict, and whether that is
the verdict recorded (DEVELOPERS.md, "Checks against the original").

    python3 tools/dosbox/checks/run_all.py [--jobs N] [--only NAME ...] [--out DIR]

Each script runs with --workdir OUT/NAME (default OUT build/dosbox-checks),
its output in OUT/NAME.log, and ends with a line `VERDICT: AGREES` or
`VERDICT: DIFFERS new (...)`, with exit status 0 or 1.
Anything else (another status, no verdict line, a timeout, a crash) is an
ERROR, never a recorded verdict. COKBOX_DISPLAYS (e.g. 180-189) keeps the X
displays to a range; --jobs defaults to the range's size less two (else 6).
Needs `make build/eclplay build/cok build/START_FULL.EXE` (make dosbox-checks
does it; `make dosbox-checks ARGS="--only b_amount"` passes options). Exit 0
when every script gave its recorded verdict."""

import argparse
import os
import pathlib
import re
import signal
import subprocess
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]

# (script, the checks it covers, the verdict recorded: "" agrees, or "new"
# for a port difference found and recorded in DEVELOPERS.md but not yet fixed;
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

# Checks that catch a text shown for a few seconds in a burst of screenshots
# (d_lib.burst): run alone after the rest, as with others running beside them
# on a loaded host DOSBox-X wrote ten times fewer and missed some.
ALONE = {"d_arrows", "d_aurak", "d_bolt", "d_bozak", "d_cast", "d_gate", "d_hold",
         "d_turning"}
# Minutes each took in a full run (the rest a few each), for the order.
LONG = {"a_demo": 6, "c_wander": 6, "f_frames": 5, "f_display_modes": 5, "f_sound": 4,
        "a_ctrl_s": 3, "e_bolt_bounce": 2, "f_mouse": 2, "e_manual": 2}
VERDICT = re.compile(r"^VERDICT: (AGREES|DIFFERS new.*|ERROR.*)$", re.M)


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


def self_test():
    """judge_log and recorded_as on made-up logs; no DOSBox-X needed."""
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
    return 1 if bad else 0


def run_one(name, out, timeout):
    """Run one script: (verdict text, status) with verdict None on an error."""
    pidfile = out / f"{name}.pids"
    pidfile.unlink(missing_ok=True)
    env = dict(os.environ, COKBOX_PIDFILE=str(pidfile))
    with open(out / f"{name}.log", "wb") as log:
        with RUNNING_LOCK:
            if STOPPING.is_set():
                return None, "stopped"
            proc = subprocess.Popen(
                [sys.executable, str(HERE / f"{name}.py"), "--workdir", str(out / name)],
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


def line(name, checks, want, verdict, code, took):
    if verdict is None:
        shown, note = f"ERROR ({'timed out' if code == 'timeout' else f'status {code}'})", ""
    else:
        shown = "agrees" if verdict == "AGREES" else verdict.split(" (")[0].lower()
        got = recorded_as(verdict)
        note = ("as recorded" if got == want else
                f"CHANGED (recorded: {'agrees' if not want else 'differs new'})")
    return f"{name:20} {checks:18} {shown:28} {took / 60:5.1f} min  {note}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--only", nargs="*", help="these scripts (names without .py)")
    parser.add_argument("--out", default=str(ROOT / "build" / "dosbox-checks"))
    parser.add_argument("--timeout", type=float, default=3 * 3600,
                        help="seconds a script may take (default three hours)")
    parser.add_argument("--self-test", action="store_true",
                        help="check the verdict and status parsing only (no DOSBox-X)")
    opts = parser.parse_args()
    if opts.self_test:
        return self_test()
    n = displays()
    jobs = opts.jobs or (max(1, n - 2) if n else 6)
    todo = [c for c in CHECKS if not opts.only or c[0] in opts.only]
    unknown = set(opts.only or ()) - {c[0] for c in CHECKS}
    if unknown:
        parser.error(f"no such check script: {', '.join(sorted(unknown))}")
    out = pathlib.Path(opts.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    # The port's builds the checks share (checklib.build), once here.
    sys.path.insert(0, str(HERE))
    import checklib
    checklib.stopping_eclplay()
    checklib.eclplay_with_pauses()
    # After importing checklib: cokbox sets its own SIGTERM handler.
    for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(signum, stop)

    results, lock = {}, threading.Lock()
    # The long ones first, so that they do not end the run alone.
    queue = sorted((c for c in todo if c[0] not in ALONE), key=lambda c: -LONG.get(c[0], 0))
    alone = [c for c in todo if c[0] in ALONE]
    t0 = time.monotonic()

    def worker():
        while True:
            with lock:
                if not queue or STOPPING.is_set():
                    return
                name, checks, want = queue.pop(0)
            start = time.monotonic()
            try:
                verdict, code = run_one(name, out, opts.timeout)
            except Exception as e:  # noqa: BLE001 (an error of the runner's: reported)
                verdict, code = None, f"runner: {e}"
            took = time.monotonic() - start
            with lock:
                results[name] = (verdict, code, took)
                print(line(name, checks, want, verdict, code, took), flush=True)

    print(f"{len(todo)} scripts, {jobs} at a time, then {len(alone)} alone; logs in {out}; "
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
    queue.extend(alone)
    work(1)
    print(f"\nall done in {(time.monotonic() - t0) / 60:.0f} min (load average "
          f"{os.getloadavg()[0]:.0f}); in the table's order:")
    changed = 0
    for name, checks, want in todo:
        verdict, code, took = results.get(name, (None, "not run", 0))
        print(line(name, checks, want, verdict, code, took))
        changed += verdict is None or recorded_as(verdict) != want
    print(f"{len(todo) - changed} as recorded, {changed} changed or in error")
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
