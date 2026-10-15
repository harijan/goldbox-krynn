"""Helpers for batch A's checks (startup, title, setup, the start menu):
the same keys in eclplay and in the original under DOSBox-X, screens taken
at `\\sNAME.` and compared with eclplay's best-matching frame, files compared.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402


def port_keys(keys):
    """KEYS without the box's own escapes (\\sNAME. screenshots, \\pN. pauses
    stay: eclplay takes them)."""
    return re.sub(r"\\s[^.]*\.", "", keys)


def workdir(arg, name):
    return pathlib.Path(arg).resolve() if arg else pathlib.Path(tempfile.mkdtemp(prefix=f"check-{name}-"))


class Pair:
    """eclplay's run (stdout, saves, shots) and the box's screens and files for
    the same keys."""

    def __init__(self, keys, work, seed=None, machine="tandy", saves=(), args=(), box_args="",
                 verbose=False, eclplay_args=(), start_menu=True, **box_kw):
        self.work = pathlib.Path(work)
        self.keys = keys
        eargs = list(eclplay_args) + (["--seed", str(seed)] if seed is not None else [])
        self.out, self.port_saves, self.port_shots = cokbox.eclplay(
            port_keys(keys), self.work / "eclplay", machine=machine, saves=saves, args=eargs,
            start_menu=start_menu)
        self.expected = [cokbox.Screen.load(p) for p in self.port_shots]
        self.box = cokbox.Box(workdir=self.work / "box", machine=machine, saves=saves,
                              args=box_args, seed=seed, verbose=verbose, **box_kw)
        self.shots = {}
        shutil.rmtree(self.work / "shots", ignore_errors=True)
        for old in self.work.glob("diff-*.png"):
            old.unlink()

    def __enter__(self):
        self.box.start()
        return self

    def __exit__(self, *exc):
        self.box.close()

    def run(self, keys=None):
        """Type KEYS (default: all of them) in the box; screens at \\sNAME."""
        self.box.keys(self.keys if keys is None else keys, shots=self.work / "shots")

    def shot(self, name):
        self.box.wait_input(settle=0.5)
        return self.box.screenshot(self.work / "shots" / f"{name}.png")

    def compare(self, name, screen=None, quiet=False):
        """The screen NAME (taken) against eclplay's best frame: (count, index, box)."""
        if screen is None:
            screen = cokbox.Screen.load(self.work / "shots" / f"{name}.png")
        if not self.expected:
            return None
        diffs = [screen.diff(e) if (screen.width, screen.height) == (e.width, e.height)
                 else (10 ** 9, None) for e in self.expected]
        best = min(range(len(diffs)), key=lambda i: diffs[i][0])
        count, area = diffs[best]
        if count:
            screen.save_png(self.work / f"diff-{name}.png", highlight=self.expected[best])
        if not quiet:
            print(f"  screen {name}: {count} pixels differ from eclplay's "
                  f"{self.port_shots[best].name}" + (f" in {area}" if area else ""))
        return count, best, area

    def box_files(self):
        return {p.name: p.read_bytes() for p in (self.box.game / "SAVE").iterdir() if p.is_file()}

    def port_files(self):
        return {p.name: p.read_bytes() for p in self.port_saves.iterdir() if p.is_file()}


def compare_files(orig, port, mask=None, show=True):
    """Compare two {name: bytes}; MASK(name, data) zeroes bytes written from
    memory. Returns the number of files that differ."""
    differ = 0
    for name in sorted(set(orig) | set(port)):
        if name not in orig or name not in port:
            if show:
                print(f"  {name}: only in {'the original' if name in orig else 'the port'}'s")
            differ += 1
            continue
        a, b = orig[name], port[name]
        if mask:
            a, b = mask(name, a), mask(name, b)
        offs = [i for i in range(max(len(a), len(b)))
                if i >= len(a) or i >= len(b) or a[i] != b[i]]
        if offs:
            differ += 1
        if show:
            print(f"  {name}: " + ("identical" if not offs else
                  f"{len(offs)} bytes differ (sizes {len(a)}, {len(b)}) at "
                  + " ".join(f"{i:#x}:{a[i] if i < len(a) else '-'}/{b[i] if i < len(b) else '-'}"
                             for i in offs[:16]) + (" ..." if len(offs) > 16 else "")))
    return differ


# The 409-byte record's bytes written from memory (pointers): see save_again.py.
WHO_POINTERS = [range(0xE3, 0xE7), range(0x143, 0x147), range(0x147, 0x17B),
                range(0x17F, 0x183), range(0x183, 0x187)]


def mask_roster(name, data):
    data = bytearray(data)
    if name.endswith((".WHO", ".SAV")):
        spans = WHO_POINTERS
    elif name.endswith(".STF"):
        spans = [range(i + 0x2A, i + 0x2E) for i in range(0, len(data), 63)]
    elif name.endswith(".SFX"):
        spans = [range(i + 5, i + 9) for i in range(0, len(data), 9)]
    else:
        spans = []
    for r in spans:
        for i in r:
            if i < len(data):
                data[i] = 0
    return bytes(data)


NAMES = 0x1415  # SAVGAM?.DAT: the party's size, then eight string[40] names


def mask_saved(name, data):
    """As mask_roster, and in SAVGAM?.DAT the stack's bytes after each name
    and the slots past the party (save_again.py)."""
    if not name.endswith(".DAT"):
        return mask_roster(name, data)
    data = bytearray(data)
    size = data[NAMES - 1]
    for k in range(8):
        slot = NAMES + 41 * k
        keep = 1 + min(data[slot], 40) if k < size else 0
        for i in range(slot + keep, slot + 41):
            data[i] = 0
    return bytes(data)


def delay_eclplay(out):
    """Build into OUT a copy of eclplay that also saves the screen at each of
    the game's delays of 100 ms or more (Crt.Delay: CALL [6803]'s frames,
    notices, the demonstration's pauses), as ECLPLAY_DELAY_SHOTS/dNNNNN.bmp,
    printing `delayshot: N MS` there: eclplay's own shots come only at key
    reads, which the demonstration has none of; with ECLPLAY_FRAMES=FILE,
    every different 320x200 screen at any delay is appended to FILE (32,000
    bytes each, two pixels a byte, as cok_picture_frame holds them). Returns
    its path."""
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    root = cokbox.ROOT
    src = (root / "src/eclplay.c").read_text()
    hook = ("static void delay(cok_adventure *game, unsigned ms, void *context)\n{\n"
            "    (void)game;\n    player *p = context;\n")
    if hook not in src:
        raise cokbox.BoxError("eclplay.c's delay hook changed: update delay_eclplay")
    src = src.replace(hook, hook + (
        "    {\n        static unsigned n;\n        const char *dir = getenv(\"ECLPLAY_DELAY_SHOTS\");\n"
        "        const char *raw = getenv(\"ECLPLAY_FRAMES\");\n"
        "        if (raw != NULL && p->game != NULL) {\n"
        "            static uint8_t last[320 * 200 / 2];\n            static FILE *frames;\n"
        "            cok_image im = cok_picture_frame(&p->game->screen, 0);\n"
        "            if (im.width == 320 && im.height == 200 && memcmp(last, im.pixels, sizeof last)) {\n"
        "                memcpy(last, im.pixels, sizeof last);\n"
        "                if (frames == NULL) frames = fopen(raw, \"wb\");\n"
        "                if (frames != NULL) { fwrite(last, 1, sizeof last, frames); fflush(frames); }\n"
        "            }\n        }\n"
        "        if (dir != NULL && ms >= 100) {\n            char path[4096];\n"
        "            snprintf(path, sizeof path, \"%s/d%05u.bmp\", dir, n);\n"
        "            printf(\"delayshot: %u %u\\n\", n++, ms);\n            save(p, path);\n"
        "        }\n    }\n"))
    # With ECLPLAY_SOUND_STATE set (and --wav, which gives eclplay its sound),
    # each call to the sound driver prints DS:577a and 577b after it.
    call = "    cok_sound_call_at(p->sound, call, value);\n"
    if call not in src:
        raise cokbox.BoxError("eclplay.c's sound hook changed: update delay_eclplay")
    src = src.replace(call, call + (
        "    if (getenv(\"ECLPLAY_SOUND_STATE\") != NULL)\n"
        "        printf(\"sound-state: call %d, 577a %u, 577b %u\\n\", (int)call, p->sound->device,"
        " p->sound->saved);\n"))
    # With ECLPLAY_CRT_DUMP set, each key read in the text mode prints the
    # screen: `crtdump: ROW|chars|attrs` for each row, then the cursor.
    read = "    release(p);\n    if (!cok_key_script_owed(&p->keys) && p->shots != NULL) {\n"
    if read not in src:
        raise cokbox.BoxError("eclplay.c's next_key changed: update delay_eclplay")
    src = src.replace(read, (
        "    if (getenv(\"ECLPLAY_CRT_DUMP\") != NULL && p->crt != NULL) {\n"
        "        for (int r = 0; r < COK_CRT_ROWS; ++r) {\n"
        "            printf(\"crtdump: %02d|\", r);\n"
        "            for (int c = 0; c < COK_CRT_COLUMNS; ++c) printf(\"%02x\", p->crt->chars[r][c]);\n"
        "            printf(\"|\");\n"
        "            for (int c = 0; c < COK_CRT_COLUMNS; ++c) printf(\"%02x\", p->crt->attrs[r][c]);\n"
        "            printf(\"\\n\");\n        }\n"
        "        printf(\"crtdump: cursor %d %d\\n\", p->crt->x, p->crt->y);\n    }\n") + read)
    if "#include <stdlib.h>" not in src:
        src = "#include <stdlib.h>\n" + src
    (out / "eclplay_delays.c").write_text(src)
    make = (root / "Makefile").read_text()
    sources = re.search(r"^ADVENTURE_SRC = (.*)$", make, re.M).group(1).split()
    exe = out / "eclplay-delays"
    subprocess.run(["cc", "-I", str(root / "src"), "-std=c17", "-O2", str(out / "eclplay_delays.c"),
                    str(root / "src/keyscript.c"), *(str(root / s) for s in sources), "-lm",
                    "-o", str(exe)], check=True)
    return exe
