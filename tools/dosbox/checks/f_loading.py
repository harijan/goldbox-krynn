#!/usr/bin/env python3
"""Check: `Loading...Please Wait` (README, Checks against the original: "it
should show on row 24 in light green at startup before the title, in each
step into a new block (NEWECL), and with Pics animation on while a small
picture or a monster's sprite loads; with animation on, text left on row 24
should be gone after a picture loads, and with it off stay").

The original runs in DOSBox-X as a Tandy, whose video memory lies in the
guest's RAM (0x98000, 32 KB, four banks of 8 KB, 4 bits a pixel), so row 24
(pixel rows 192-199) can be read through /proc/PID/mem thousands of times a
second by a thread while the keys go in: every change of the row is logged
with its time, and a row holding light green (colour 10) pixels is matched
against the message as startup draws it (the first such row seen, saved as
loading.png). DOSBox-X's disk reads take emulated time but not a real
disk's, so how long the message shows is not a real machine's.

Phases: startup to the title; game A loaded and Begin Adventuring (block
36, NEWECL), Solace's pages to the outpost (block 17), Leave (the overland
map, block 16), the steps to the caravan (its picture); then the same with
animation off (camp's Alter, Pics, F7) from the outpost's inn.
The screens at the key waits after each are compared with eclplay's.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402


VRAM = 0x98000
DS_ANIMATION = 0x4B4F


def strip_raw(box_mem, base):
    """Pixel rows 192-199 of the Tandy's 320x200x16 screen, as colours."""
    parts = []
    for y in range(192, 200):
        off = (y % 4) * 0x2000 + (y // 4) * 160
        box_mem.seek(base + VRAM + off)
        parts.append(box_mem.read(160))
    out = bytearray()
    for part in parts:
        for b in part:
            out += bytes((b >> 4, b & 15))
    return bytes(out)


class Poller(threading.Thread):
    """Reads row 24 as fast as it can; logs (time, row) at each change."""

    def __init__(self, box):
        super().__init__(daemon=True)
        self.box = box
        self.mem = open(f"/proc/{box.dosbox.pid}/mem", "rb", buffering=0)
        self.log = []
        self.stop = False
        self.reads = 0

    def run(self):
        last = None
        while not self.stop:
            try:
                row = strip_raw(self.mem, self.box.base)
            except OSError:
                break
            self.reads += 1
            if row != last:
                self.log.append((time.monotonic(), row))
                last = row


def is_message(row):
    """Row 24 holding the whole message: 479 pixels lit, all light green
    (startup's `Loading...Please Wait`, as loading.png shows it)."""
    return row.count(10) == MESSAGE_PIXELS and sum(1 for p in row if p) == MESSAGE_PIXELS


MESSAGE_PIXELS = 479


def save_strip(row, path):
    cokbox.Screen(320, 8, row).save_png(path)


# Game A from the title to the outpost's inn's camp, Alter, Pics, (F7: the
# animation off), back to the outpost (its picture 58 loaded again), the
# Commandant (57), back (58), Leave (NEWECL: the overland map's block 16),
# decline, Move, three steps to the caravan.
PHASES = [
    ("title", r"\kx"),
    ("game A loaded", r"P\v\v\rA"),
    ("Solace (NEWECL 36)", r"\v\v\v\v\v\r"),
    ("outpost (NEWECL 17)", r"\r\r\r\r\r\r\r"),
    ("inn camp", r"I\r"),
    ("alter", "A"),
    ("pics", "P"),
    ("animation off", r"\#41"),
    ("back to alter", r"\e"),
    ("camp menu", r"\e"),
    ("outpost again (PIC 58)", "E"),
    ("commandant (PIC 57)", "C"),
    ("outpost menu (PIC 58)", r"\r"),
    ("near outpost (NEWECL 16)", "L"),
    ("overland", "N"),
    ("move", "M"),
    ("steps to the caravan", r"\>\>\^"),
]


def instrumented_eclplay(out):
    """eclplay built from src/ with a line printed each time the port draws
    the message (cok_adventure_loading), in OUT."""
    src = out / "src"
    if src.exists():
        shutil.rmtree(src)
    shutil.copytree(cokbox.ROOT / "src", src)
    adv = src / "adventure.c"
    text = adv.read_text()
    anchor = '"Loading...Please Wait", 0, 24, 10, 0);\n'
    if anchor not in text:
        raise SystemExit("src/adventure.c: cok_adventure_loading not found")
    adv.write_text(text.replace(anchor, anchor + '    printf("LOADING-MESSAGE\\n");\n', 1))
    skip = {"cok.c", "keymap.c", "daxcheck.c", "daxcompose.c", "daximages.c", "ecldump.c"}
    files = [str(f) for f in sorted(src.glob("*.c")) if f.name not in skip]
    exe = out / "eclplay-loading"
    subprocess.run(["cc", "-I", str(src), "-std=c17", "-O2", *files, "-o", str(exe)], check=True)
    return exe


def port_counts(exe, phases, work):
    """The port's messages in each phase: eclplay --start-menu run on the keys
    up to each phase's end (it stops at the next key read), counted."""
    saves = work / "port-saves"
    cfg = work / "krynn.cfg"
    cfg.write_bytes(cokbox.krynn_cfg("T").encode())
    counts, total = [], 0
    for i in range(len(phases)):
        if saves.exists():
            shutil.rmtree(saves)
        saves.mkdir()
        for f in (cokbox.ROOT / "SAVE").iterdir():
            if f.is_file():
                shutil.copyfile(f, saves / f.name)
        keys = "".join(k for _, k in phases[:i + 1])
        res = subprocess.run([str(exe), "--start-menu", "--config", str(cfg), "--saves", str(saves),
                              "--seed", "1", "--keys", keys, str(cokbox.ROOT / "Assets")],
                             capture_output=True, text=True, timeout=300)
        n = res.stdout.count("LOADING-MESSAGE")
        counts.append(n - total)
        total = n
    return counts


def run(name, phases, work, verbose, port):
    work.mkdir(parents=True, exist_ok=True)
    keys = "".join(k for _, k in phases)
    _, _, eshots = cokbox.eclplay(keys, work / "eclplay", saves=[cokbox.ROOT / "SAVE"],
                                  args=["--seed", "1"])
    expected = [cokbox.Screen.load(p) for p in eshots]
    marks = []
    with cokbox.Box(workdir=work / "box", saves=[cokbox.ROOT / "SAVE"], verbose=verbose) as box:
        # Polling from HOOK.COM's start, before START.EXE's startup; the seed
        # (as Box(seed=) sets it) at the first wait for a key.
        poller = Poller(box)
        poller.start()
        t0 = time.monotonic()
        box.wait_input()
        box.set_seed(1)
        for phase, k in phases:
            box.keys(k)
            box.wait_input(settle=1.0)
            shown = box.screenshot(work / f"{phase.split(' (')[0].replace(' ', '-')}.png")
            best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
            count, area = shown.diff(expected[best])
            marks.append((time.monotonic(), phase, box.ds_byte(DS_ANIMATION), count, area,
                          eshots[best].name))
        poller.stop = True
        poller.join(2)
    print(f"{name}: {poller.reads} reads of row 24 in {time.monotonic() - t0:.0f} s, "
          f"{len(poller.log)} changes")
    shown_rows = [row for _, row in poller.log if is_message(row)]
    if shown_rows:
        save_strip(shown_rows[0], work / "loading.png")
    status = 0
    idx = 0
    per_phase = [[] for _ in marks]
    for i, (t, row) in enumerate(poller.log):
        while idx < len(marks) and marks[idx][0] < t:
            idx += 1
        if idx < len(marks) and is_message(row):
            until = poller.log[i + 1][0] if i + 1 < len(poller.log) else t
            prev = poller.log[i - 1][1] if i else b""
            before = "blank" if not any(prev) else f"{sum(1 for p in prev if p)} px lit"
            per_phase[idx].append(f"{(until - t) * 1000:.0f} ms after {before}")
    for m, (tm, phase, anim, count, area, ename) in enumerate(marks):
        shows = per_phase[m]
        agree = len(shows) == port[m]
        if not agree:
            status = 1
        print(f"{name}: {phase}: animation {anim}; the original shows it {len(shows)}x "
              f"({', '.join(shows) or '-'}); the port {port[m]}x"
              f"{'' if agree else ' DIFFERS'}; screen vs eclplay's {ename}: {count} px"
              + (f" in {area}" if area else ""))
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-loading-")).resolve()
    work.mkdir(parents=True, exist_ok=True)
    exe = instrumented_eclplay(work)
    status = 0
    for name, phases in (("animation on", [p for p in PHASES if p[0] != "animation off"]),
                         ("animation off", PHASES)):
        port = port_counts(exe, phases, work)
        status |= run(name, phases, work / name.split()[1], args.verbose, port)
    print("verdict:", "AGREES" if status == 0 else "the counts differ (see above)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())
