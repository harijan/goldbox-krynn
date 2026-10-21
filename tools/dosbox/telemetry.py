"""Where a DOSBox-X check's time goes, for speeding the checks up (stdlib only).

Off unless COKBOX_TRACE names a file (run_all.py sets one for each script,
OUT/NAME.trace.jsonl). Then, in the check's process:

- span(kind, what): a context manager timing one of cokbox's or checklib's
  steps (a box's boot, a wait for the game, a screenshot, a run or build of
  the port...); totals by (kind, what), only the main thread's outermost
  spans counted as the script's own time (the rest is "other"), and each
  outermost span of a second or more logged with its start, as a timeline;
- a sampler thread: every SAMPLE seconds the main thread's stack, by the
  check's own lines (the innermost frame in tools/dosbox/checks/) and
  cokbox's function, so that "other" and long waits point at a line;
- at exit one "summary" line: the wall time, the totals, the samples, the
  process's CPU and its children's (DOSBox-X, Xvfb, eclplay, cc), and each
  box's (DOSBox-X's CPU against its lifetime: near 1 a box the host runs
  flat out, low one that idles at its fixed cycles).

run_all.py reads the files back into OUT/telemetry.txt (see report there).
"""
import atexit
import collections
import contextlib
import json
import os
import pathlib
import resource
import sys
import threading
import time

PATH = os.environ.get("COKBOX_TRACE")
ENABLED = bool(PATH)
SAMPLE = 0.1          # seconds between the sampler's looks
LOG_AT_LEAST = 1.0    # outermost spans this long or longer go in the timeline

CHECKS = pathlib.Path(__file__).resolve().parent / "checks"
_T0 = time.monotonic()
_MAIN = threading.main_thread()
_local = threading.local()
_lock = threading.Lock()
_totals = collections.defaultdict(lambda: [0, 0.0, 0.0])  # (kind, what): count, s, outer s
_lines = collections.Counter()   # "file:line func" of the check's innermost frame
_ops = collections.Counter()     # cokbox's innermost function (or "-")
_boxes = []
_live = {}   # DOSBox-X's pid: its CPU seconds last seen (it may be reaped by close)


def _write(record):
    with _lock, open(PATH, "a") as f:
        f.write(json.dumps(record) + "\n")


@contextlib.contextmanager
def span(kind, what=""):
    if not ENABLED:
        yield
        return
    depth = getattr(_local, "depth", 0)
    outer = depth == 0 and threading.current_thread() is _MAIN
    _local.depth = depth + 1
    start = time.monotonic()
    try:
        yield
    finally:
        took = time.monotonic() - start
        _local.depth = depth
        with _lock:
            t = _totals[(kind, what)]
            t[0] += 1
            t[1] += took
            if outer:
                t[2] += took
        if outer and took >= LOG_AT_LEAST:
            _write({"at": round(start - _T0, 2), "took": round(took, 2), "kind": kind,
                    "what": what})


def add(kind, what, seconds):
    """Count SECONDS under (KIND, WHAT) as an inner span would (not the
    script's own time)."""
    if ENABLED:
        with _lock:
            t = _totals[(kind, what)]
            t[0] += 1
            t[1] += seconds


def proc_cpu(pid):
    """A live process's CPU seconds (user and system), or None."""
    try:
        fields = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    except OSError:
        return None
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def box_started(pid):
    if ENABLED:
        with _lock:
            _live[pid] = 0.0


def box_closed(pid, what, alive):
    """A box ended: DOSBox-X's PID, WHAT (its machine), ALIVE seconds."""
    if ENABLED:
        cpu = proc_cpu(pid)
        with _lock:
            cpu = max(cpu or 0.0, _live.pop(pid, 0.0))
            _boxes.append({"what": what, "alive": round(alive, 2), "cpu": round(cpu, 2)})


def _sampler():
    while True:
        time.sleep(SAMPLE)
        frame = sys._current_frames().get(_MAIN.ident)
        line, op = None, "-"
        while frame is not None:
            code = frame.f_code
            path = pathlib.Path(code.co_filename)
            if line is None and path.parent == CHECKS:
                line = f"{path.name}:{frame.f_lineno} {code.co_name}"
            if op == "-" and path.name == "cokbox.py":
                op = code.co_name
            frame = frame.f_back
        with _lock:
            _lines[line or "-"] += 1
            _ops[op] += 1
            pids = list(_live)
        for pid in pids:
            cpu = proc_cpu(pid)
            if cpu is not None:
                with _lock:
                    if pid in _live:
                        _live[pid] = cpu


def _summary():
    wall = time.monotonic() - _T0
    me, kids = resource.getrusage(resource.RUSAGE_SELF), resource.getrusage(resource.RUSAGE_CHILDREN)
    with _lock:
        totals = [{"kind": k, "what": w, "count": c, "seconds": round(s, 2),
                   "outer": round(o, 2)} for (k, w), (c, s, o) in _totals.items()]
        record = {"summary": True, "script": pathlib.Path(sys.argv[0]).stem,
                  "wall": round(wall, 2), "sample": SAMPLE,
                  "cpu_self": round(me.ru_utime + me.ru_stime, 2),
                  "cpu_children": round(kids.ru_utime + kids.ru_stime, 2),
                  "totals": totals, "boxes": list(_boxes),
                  "lines": dict(_lines.most_common(40)), "ops": dict(_ops.most_common(20))}
    _write(record)


if ENABLED:
    threading.Thread(target=_sampler, daemon=True, name="telemetry").start()
    # Registered at import, before cokbox's _cleanup: atexit runs it last,
    # after the boxes are closed and counted.
    atexit.register(_summary)
