"""Helpers for batch A's checks (startup, title, setup, the start menu):
the same keys in eclplay and in the original under DOSBox-X, screens taken
at `\\sNAME.` and compared with eclplay's best-matching frame, files compared.
"""
import pathlib
import re
import shutil
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
