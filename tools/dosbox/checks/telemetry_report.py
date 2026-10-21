"""run_all.py's telemetry: the machine's load while the checks run
(LoadSampler) and the report of where their time went (write), from the
scripts' traces (tools/dosbox/telemetry.py), OUT/load.jsonl and
OUT/schedule.json. Stdlib only."""
import collections
import json
import os
import pathlib
import threading
import time

# How a span's (kind, what) is shown in the report's columns.
WAIT_GAME = ("the game to wait for a key", "the game to take the key")
COLUMNS = ["boot", "game", "shots", "watch", "sleep", "port", "pause", "x", "close", "wait"]


def column(kind, what):
    if kind == "wait" and what in WAIT_GAME:
        return "game"
    if kind == "screenshot":
        return "shots"
    if kind == "box":
        return "close"
    return kind


class LoadSampler:
    """Every PERIOD seconds a line in PATH: seconds since start, the load
    average, the CPU's busy share since the last line, DOSBox-X's processes
    and the scripts running (RUNNING())."""

    PERIOD = 2.0

    def __init__(self, path, running):
        self.path, self.running = pathlib.Path(path), running
        self.done = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True, name="load")

    def start(self):
        self.path.unlink(missing_ok=True)
        self.thread.start()

    def stop(self):
        self.done.set()
        self.thread.join(5)

    @staticmethod
    def cpu():
        fields = [int(v) for v in pathlib.Path("/proc/stat").read_text().split("\n", 1)[0].split()[1:]]
        idle = fields[3] + fields[4]
        return sum(fields) - idle, sum(fields)

    @staticmethod
    def boxes():
        count = 0
        for d in pathlib.Path("/proc").iterdir():
            if d.name.isdigit():
                try:
                    count += (d / "comm").read_text().strip() == "dosbox-x"
                except OSError:
                    pass
        return count

    def loop(self):
        t0, last = time.monotonic(), self.cpu()
        with open(self.path, "a") as f:
            while not self.done.wait(self.PERIOD):
                now = self.cpu()
                busy = (now[0] - last[0]) / max(1, now[1] - last[1])
                last = now
                f.write(json.dumps({"at": round(time.monotonic() - t0, 1),
                                    "load": round(os.getloadavg()[0], 1),
                                    "busy": round(busy, 3), "boxes": self.boxes(),
                                    "running": self.running()}) + "\n")
                f.flush()


def read_trace(path):
    """(summary, timeline) of a script's trace; summary None if it never
    wrote one (killed hard)."""
    summary, timeline = None, []
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return None, []
    for line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if record.get("summary"):
            summary = record
        else:
            timeline.append(record)
    return summary, timeline


def minutes(s):
    return f"{s / 60:5.1f}"


def secs(s):
    return f"{s:5.0f}"


def write(out, names, jobs=None):
    """Write OUT/telemetry.txt from the run's files; return a short summary
    of it for the console."""
    out = pathlib.Path(out)
    lines = []
    put = lines.append
    try:
        schedule = json.loads((out / "schedule.json").read_text())
    except (OSError, ValueError):
        schedule = {"jobs": jobs, "wall": None, "runs": []}
    load = []
    try:
        load = [json.loads(x) for x in (out / "load.jsonl").read_text().splitlines() if x]
    except (OSError, ValueError):
        pass
    runs = {r["name"]: r for r in schedule["runs"] if not r["alone"]}
    traces = {}
    for name in names:
        summary, timeline = read_trace(out / f"{name}.trace.jsonl")
        if summary is not None:
            traces[name] = (summary, timeline)

    # The run as a whole.
    jobs = schedule.get("jobs") or jobs or 1
    wall = schedule.get("wall") or 0
    took = [r["took"] for r in runs.values()]
    total = sum(took)
    longest = max(runs.values(), key=lambda r: r["took"], default=None)
    put("== The run")
    if took:
        bound = max(longest["took"], total / jobs)
        put(f"wall {minutes(wall)} min, {len(took)} scripts, {jobs} at a time; their time "
            f"summed {minutes(total)} min (/{jobs}: {minutes(total / jobs)}); longest "
            f"{longest['name']} {minutes(longest['took'])} min; so at best {minutes(bound)} "
            f"min at {jobs} side by side")
        put(f"slots used: {total / max(1e-9, wall * jobs):.0%} of {jobs} x the wall time")
        last = max(runs.values(), key=lambda r: r["start"] + r["took"])
        put(f"last to end: {last['name']} (started at {minutes(last['start'])} min, "
            f"took {minutes(last['took'])})")
    alone = [r for r in schedule["runs"] if r["alone"]]
    if alone:
        put(f"run again alone: {', '.join(r['name'] for r in alone)} "
            f"({minutes(sum(r['took'] for r in alone))} min)")
    if load:
        busy = sum(x["busy"] for x in load) / len(load)
        put(f"machine: CPU busy {busy:.0%} on average (of {os.cpu_count()} cores), load "
            f"average {sum(x['load'] for x in load) / len(load):.1f}, DOSBox-X running "
            f"{sum(x['boxes'] for x in load) / len(load):.1f} on average, at most "
            f"{max(x['boxes'] for x in load)}")
        # How many scripts ran, over time: the tail where few run is the
        # place to start the long ones earlier or split them.
        by_count = collections.Counter(len(x["running"]) for x in load)
        put("scripts running, share of the time: " + ", ".join(
            f"{n}: {by_count[n] * LoadSampler.PERIOD / max(1, load[-1]['at']):.0%}"
            for n in sorted(by_count)))
    put("")

    # Each script, the longest first.
    put("== Each script (seconds but where it says; game = waiting for the original to "
        "want a key or take one; other = not in any span: the check's own sleeps and "
        "reads, see its lines below)")
    head = f"{'script':20} {'wall':>5} {'boxes':>5} {'dbx%':>5} " + " ".join(
        f"{c:>5}" for c in COLUMNS) + f" {'other':>5} {'cpu':>5} {'kids':>5}"
    put(head)
    order = sorted(traces, key=lambda n: -traces[n][0]["wall"])
    grand = collections.Counter()
    for name in order:
        summary = traces[name][0]
        cols = collections.Counter()
        for t in summary["totals"]:
            cols[column(t["kind"], t["what"])] += t["outer"]
        other = summary["wall"] - sum(cols.values())
        grand.update(cols)
        grand["other"] += other
        grand["wall"] += summary["wall"]
        boxes = summary["boxes"]
        alive = sum(b["alive"] for b in boxes)
        dbx = f"{sum(b['cpu'] for b in boxes) / alive:5.0%}" if alive else "    -"
        put(f"{name:20} {secs(summary['wall'])} {len(boxes):5} {dbx} "
            + " ".join(secs(cols[c]) for c in COLUMNS)
            + f" {secs(other)} {secs(summary['cpu_self'])} "
            f"{secs(summary['cpu_children'])}")
    missing = [n for n in names if n not in traces]
    if missing:
        put(f"no trace: {', '.join(missing)}")
    if grand["wall"]:
        put("all: " + ", ".join(f"{c} {grand[c] / grand['wall']:.0%}"
                                for c in COLUMNS + ["other"] if grand[c]))
    put("(dbx%: DOSBox-X's CPU over its boxes' lifetime: near 100% a box the host runs "
        "flat out, low one idling at its fixed cycles)")
    put("")

    # The spans across all scripts.
    put("== Spans, all scripts (outermost seconds; count; all seconds, inner too)")
    spans = collections.defaultdict(lambda: [0, 0.0, 0.0])
    for summary, _ in traces.values():
        for t in summary["totals"]:
            s = spans[(t["kind"], t["what"])]
            s[0] += t["count"]
            s[1] += t["seconds"]
            s[2] += t["outer"]
    for (kind, what), (count, seconds, outer) in sorted(spans.items(), key=lambda kv: -kv[1][2]):
        put(f"{outer:7.0f} s  {count:7}  {seconds:7.0f} s  {kind}: {what}")
    put("")

    # Where in the checks' code: the sampler's lines.
    put("== Lines, all scripts (the sampler's: the main thread in a check's file)")
    hot = collections.Counter()
    for name, (summary, _) in traces.items():
        for line, count in summary["lines"].items():
            hot[(name, line)] += count * summary["sample"]
    for (name, line), seconds in hot.most_common(40):
        put(f"{seconds:6.0f} s  {name:18} {line}")
    put("")
    put("== Each script's top lines, and its cokbox operations")
    for name in order:
        summary = traces[name][0]
        top = sorted(summary["lines"].items(), key=lambda kv: -kv[1])[:5]
        put(f"{name} ({summary['wall']:.0f} s):")
        for line, count in top:
            put(f"    {count * summary['sample']:5.0f} s  {line}")
        ops = sorted(summary["ops"].items(), key=lambda kv: -kv[1])[:4]
        put("    cokbox: " + ", ".join(f"{op} {c * summary['sample']:.0f} s" for op, c in ops))
    put("")

    # The longest spans of the longest scripts, in order.
    put("== Timelines (spans of a second or more) of the ten longest scripts")
    for name in order[:10]:
        summary, timeline = traces[name]
        put(f"{name} ({summary['wall']:.0f} s):")
        for t in timeline:
            put(f"    at {t['at']:7.1f}s  {t['took']:7.1f}s  {t['kind']}: {t['what']}")
    (out / "telemetry.txt").write_text("\n".join(lines) + "\n")
    head_lines = lines[:lines.index("") if "" in lines else len(lines)]
    return "\n".join(head_lines + [f"telemetry: {out / 'telemetry.txt'}"])
