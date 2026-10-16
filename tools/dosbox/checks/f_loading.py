#!/usr/bin/env python3
"""Check: `Loading...Please Wait` (DEVELOPERS.md, Checks against the original: "it
should show on row 24 in light green at startup before the title, in each
step into a new block (NEWECL), and with Pics animation on while a small
picture or a monster's sprite loads; with animation on, text left on row 24
should be gone after a picture loads, and with it off stay").

The original runs in DOSBox-X as a Tandy, whose video memory lies in the
guest's RAM (0x98000, 32 KB, four banks of 8 KB, 4 bits a pixel), so row 24
(pixel rows 192-199) can be read through /proc/PID/mem thousands of times a
second by a thread while the keys go in: every change of the row is logged
with its time, and a row of exactly 479 lit pixels, all light green
(colour 10), is the message (as startup draws it; saved as loading.png).
DOSBox-X's disk reads take emulated time but not a real disk's, so how long
the message shows is not a real machine's.

The port's side: eclplay built with a line printed, row 24's pixels with
it, each time cok_adventure_loading has drawn the message (the only draw
of it: start.c, roster.c, adventure.c's pictures with animation on and
NEWECL), run on the keys up to each phase's end: the messages counted
phase by phase must be the original's, each as the original's row pixel
for pixel, and the screen at each phase's key wait the original's (the
frame of the same run: the line printed changes no pixel).

Phases: startup to the title; game A loaded and Begin Adventuring (block
36, NEWECL), Solace's pages to the outpost (block 17), the inn's camp,
Alter, Pics, (F7: animation off, in the second run only), back, the
outpost (its picture 58), the Commandant (57), back (58), Leave (the
overland map, block 16), the steps to the caravan (its picture).
"""
import argparse
import pathlib
import sys
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import f_lib  # noqa: E402

cokbox = checklib.cokbox

VRAM = 0x98000
DS_ANIMATION = 0x4B4F
MESSAGE_PIXELS = 479
SEED = 1


def strip_raw(box_mem, base):
    """Pixel rows 192-199 of the Tandy's 320x200x16 screen, as colours."""
    parts = []
    for y in range(192, 200):
        off = (y % 4) * 0x2000 + (y // 4) * 160
        box_mem.seek(base + VRAM + off)
        parts.append(box_mem.read(160))
    return unpack(b"".join(parts))


def unpack(packed):
    """Two pixels a byte, the left one in the high nibble, as colours."""
    out = bytearray()
    for b in packed:
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
        try:
            while not self.stop:
                row = strip_raw(self.mem, self.box.base)
                self.reads += 1
                if row != last:
                    self.log.append((time.monotonic(), row))
                    last = row
        finally:
            self.mem.close()


def is_message(row):
    """Row 24 holding the whole message: 479 pixels lit, all light green."""
    return row.count(10) == MESSAGE_PIXELS and sum(1 for p in row if p) == MESSAGE_PIXELS


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

LOADING_FN = "void cok_adventure_loading(cok_adventure *game)\n{\n"
LOADING_PRINT = """
    {
        /* DOSBox-X check f_loading.py: the message drawn, row 24's pixels. */
        cok_image shown = cok_picture_frame(&game->screen, 0);
        printf("LOADING-MESSAGE ");
        for (size_t i = 192 * 160; shown.width == 320 && i < 200 * 160; ++i)
            printf("%02x", shown.pixels[i]);
        printf("\\n");
    }"""


def print_loading(name, text):
    """src/adventure.c: a line with row 24's pixels at cok_adventure_loading's
    end (whatever it draws, where and in what colour)."""
    start = text.find(LOADING_FN)
    if start < 0:
        raise cokbox.BoxError("src/adventure.c: cok_adventure_loading not found")
    end = text.index("\n}\n", start)
    return text[:end] + LOADING_PRINT + text[end:]


def port_runs(exe, phases, work):
    """eclplay EXE run on the keys up to each phase's end (it stops at the
    next key read): [(the messages' rows drawn in that phase, the frame at
    that key read)]."""
    out, total = [], 0
    keys = ""
    kw = dict(saves=[checklib.SAVES], args=["--seed", str(SEED)])
    for i, (phase, k) in enumerate(phases):
        keys += k
        stdout, _, _ = cokbox.eclplay(keys, work / f"{i:02d}", binary=exe, shots=False, **kw)
        rows = [unpack(bytes.fromhex(line.split(" ", 1)[1].strip() or ""))
                for line in stdout.splitlines() if line.startswith("LOADING-MESSAGE")]
        out.append((rows[total:], checklib.frame_after(keys, work / f"{i:02d}-shot",
                                                       binary=exe, **kw)))
        total = len(rows)
    return out


def original(phases, work, verbose):
    """The original's run: [(phase, animation byte, the message rows seen in
    that phase (time shown, the row before)...)], its screens, the poller."""
    marks, shots = [], {}
    with cokbox.Box(workdir=work / "box", saves=[checklib.SAVES], verbose=verbose) as box:
        # Polling from HOOK.COM's start, before START.EXE's startup; the seed
        # (as Box(seed=) sets it) at the first wait for a key.
        poller = Poller(box)
        poller.start()
        t0 = time.monotonic()
        box.wait_input()
        box.set_seed(SEED)
        for phase, k in phases:
            box.keys(k)
            box.wait_input(settle=1.0)
            shots[phase] = box.screenshot(work / f"{phase.split(' (')[0].replace(' ', '-')}.png")
            marks.append((time.monotonic(), phase, box.ds_byte(DS_ANIMATION)))
        poller.stop = True
        poller.join(5)
    print(f"  {poller.reads} reads of row 24 in {time.monotonic() - t0:.0f} s, "
          f"{len(poller.log)} changes")
    per_phase = [[] for _ in marks]
    idx = 0
    for i, (t, row) in enumerate(poller.log):
        while idx < len(marks) and marks[idx][0] < t:
            idx += 1
        if idx < len(marks) and is_message(row):
            until = poller.log[i + 1][0] if i + 1 < len(poller.log) else t
            prev = poller.log[i - 1][1] if i else b""
            before = "blank" if not any(prev) else f"{sum(1 for p in prev if p)} px lit"
            per_phase[idx].append((row, f"{(until - t) * 1000:.0f} ms after {before}"))
    return [(phase, anim, per_phase[m]) for m, (_, phase, anim) in enumerate(marks)], shots


def run(v, name, phases, work, verbose):
    work.mkdir(parents=True, exist_ok=True)
    transform = [("adventure.c", print_loading)]
    port = port_runs(f_lib.eclplay(transforms=transform), phases, work / "port")
    seen, shots = original(phases, work, verbose)
    messages = [row for _, _, shows in seen for row, _ in shows]
    if not messages:
        v.differ(f"{name}: the original showed the message", "never seen")
        return
    cokbox.Screen(320, 8, messages[0]).save_png(work / "loading.png")
    for m, (phase, anim, shows) in enumerate(seen):
        rows, frame = port[m]
        print(f"  {name}: {phase}: animation {anim}; the original shows it {len(shows)}x "
              f"({', '.join(d for _, d in shows) or '-'}); the port {len(rows)}x")
        v.value(f"{name}: {phase}: the messages shown", len(shows), len(rows))
        v.check(all(r == messages[0] for r in rows),
                f"{name}: {phase}: the port's messages are the original's row 24, pixel "
                "for pixel", f"{sum(r != messages[0] for r in rows)} of {len(rows)} differ")
        v.screen(f"{name}: {phase}: the screen at the key wait", shots[phase], frame,
                 work=work)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "loading")
    v = checklib.Verdict()
    for name, phases in (("animation on", [p for p in PHASES if p[0] != "animation off"]),
                         ("animation off", PHASES)):
        run(v, name, phases, work / name.split()[1], args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
