"""What every DOSBox-X check (tools/dosbox/checks/*.py) shares: how it ends,
how it judges what it compared, the port's screen at the same point of the
keys, and the builds of the port it compares with.

A check script ends through run(main): main returns 0 (the original and
the port agree in everything compared) or 1 (something differs); an
exception, a signal or any other return is an error, exit 3, never taken
for a difference. Its Verdict prints each difference as

    DIFFERENCE: WHAT: DETAIL                     (new: nothing explains it)
    DIFFERENCE (known: walls): WHAT: DETAIL      (a port difference recorded)

and last `VERDICT: AGREES`, `VERDICT: DIFFERS known: TAGS` (only known
differences, the tags sorted) or `VERDICT: DIFFERS new` (at least one new),
which run_all.py checks against what it recorded for the script.

Known differences are those the branch `fixes` (6ba8fcd) corrects, kept as
tools/dosbox/known-fixes.patch: a screen or value of the original that
differs from the port's but equals that of the port built with the patch
(build(fixed=True)) is a known difference, tagged by where it lies:
`fullstop` (row 24 column 34, PRESS <ENTER>/<RETURN> TO CONTINUE.'s full
stop, 2fd3:1146), `walls` (the 3D view: two-set WALLDEF records,
69ea:1124), `resume` (elsewhere: a loaded game's first block runs only its
load vector, 2fd3:3c28, and keeps the last mode, 2fd3:3b47). Once the
source has the fixes (the patch applies in reverse) the fixed build is the
port itself, and those differences are gone.
"""
import concurrent.futures
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import cokbox  # noqa: E402

ROOT = cokbox.ROOT
KNOWN_FIXES = HERE.parent / "known-fixes.patch"
SAVES = ROOT / "SAVE"

# Row 24, column 34: the full stop the port adds (8x8 pixels).
FULLSTOP_CELL = (272, 192, 8, 8)
# The 3D view's cells (3-13 across and down) and its frame.
VIEW_AREA = (24, 24, 88, 88)
KNOWN = {
    "fullstop": "PRESS <ENTER>/<RETURN> TO CONTINUE.'s full stop (2fd3:1146 has none)",
    "walls": "a two-set WALLDEF record's second set offset by its own slot (69ea:1124)",
    "resume": "a loaded game's first block runs its after-move and location vectors, "
              "or sets the last mode (2fd3:3c28 runs only the load vector)",
}


# ------------------------------------------------------------ ending a check

def run(main):
    """Run MAIN (returning 0 or 1) as a check's whole run; the exit status:
    0 agrees, 1 differs, 3 an error (an exception, or another return)."""
    try:
        code = main()
    except SystemExit as e:
        code = e.code if isinstance(e.code, int) else 3
    except BaseException:  # noqa: B036 (an error, whatever it is)
        traceback.print_exc()
        code = 3
    if code not in (0, 1):
        print(f"VERDICT: ERROR (status {code})", flush=True)
        code = 3
    sys.stdout.flush()
    return code


def need_saves():
    if not (SAVES / "SAVGAMA.DAT").exists():
        raise cokbox.BoxError("needs the original's saved games in SAVE/")


class Verdict:
    """The differences one check found. check(), screen() and value() judge
    one thing each; finish() prints the verdict and returns the status."""

    def __init__(self):
        self.new = []
        self.known = set()

    def ok(self, what, detail=""):
        print(f"  agrees: {what}" + (f": {detail}" if detail else ""), flush=True)

    def differ(self, what, detail="", known=None):
        """A difference: new, or KNOWN (a tag of checklib.KNOWN, or a set)."""
        tags = {known} if isinstance(known, str) else set(known or ())
        if tags:
            unknown = tags - set(KNOWN)
            if unknown:
                raise ValueError(f"not a known difference: {unknown}")
            self.known |= tags
            print(f"DIFFERENCE (known: {', '.join(sorted(tags))}): {what}"
                  + (f": {detail}" if detail else ""), flush=True)
        else:
            self.new.append(what)
            print(f"DIFFERENCE: {what}" + (f": {detail}" if detail else ""), flush=True)

    def check(self, good, what, detail="", known=None):
        """One assertion: GOOD true agrees; else a difference (KNOWN, if the
        caller has shown it is that)."""
        if good:
            self.ok(what, detail)
        else:
            self.differ(what, detail, known)
        return bool(good)

    def value(self, what, original, port, fixed=None, known_tag=None):
        """The original's value against the port's: equal agrees; equal to
        the fixed build's (FIXED, when given; a function is called only when
        needed) a known difference, tagged KNOWN_TAG (default: resume)."""
        if original == port:
            self.ok(what, f"{original!r}")
            return True
        if callable(fixed):
            fixed = fixed()
        if fixed is not None and original == fixed:
            self.differ(what, f"original {original!r}, port {port!r}, the fixed port's "
                        f"{fixed!r}", known_tag or "resume")
        else:
            self.differ(what, f"original {original!r}, port {port!r}")
        return False

    def screen(self, what, original, port, fixed=None, masks=(), work=None):
        """The original's screen against the port's at the same point
        (frames_at). MASKS: (x, y, w, h, why) areas not compared, each said
        why (DOSBox-X's own cursor, ...). Pixels that differ from the port's
        but equal FIXED's frame are a known difference, tagged by where they
        lie (FIXED may be a function, called only when needed). Returns the
        count of pixels differing from the port's."""
        if port is None:
            self.differ(what, "the port has no screen there")
            return None
        count, box = masked_diff(original, port, masks)
        note = "".join(f"; not compared: {m[4]}" for m in masks if len(m) > 4)
        if count == 0:
            self.ok(what, "0 pixels differ" + note)
            return 0
        if work is not None:
            original.save_png(pathlib.Path(work) / f"diff-{slug(what)}.png", highlight=port)
        if callable(fixed):
            fixed = fixed()
        if fixed is not None and masked_diff(original, fixed, masks)[0] == 0:
            tags = region_tags(original, port, masks)
            self.differ(what, f"{count} pixels in {box}, as the fixed port's frame", tags)
        else:
            self.differ(what, f"{count} pixels differ from the port's in {box}" + note)
        return count

    def finish(self):
        if self.new:
            print(f"VERDICT: DIFFERS new ({len(self.new)}: {', '.join(self.new[:6])}"
                  f"{' ...' if len(self.new) > 6 else ''})", flush=True)
            return 1
        if self.known:
            print(f"VERDICT: DIFFERS known: {', '.join(sorted(self.known))}", flush=True)
            return 1
        print("VERDICT: AGREES", flush=True)
        return 0


def slug(text):
    return re.sub(r"[^A-Za-z0-9]+", "-", text).strip("-")[:40] or "screen"


def _inside(x, y, area):
    ax, ay, aw, ah = area[:4]
    return ax <= x < ax + aw and ay <= y < ay + ah


def masked_diff(a, b, masks=()):
    """(pixels of A and B that differ outside MASKS, their box or None)."""
    if (a.width, a.height) != (b.width, b.height):
        return a.width * a.height, (0, 0, a.width - 1, a.height - 1)
    n, x0, y0, x1, y1 = 0, a.width, a.height, -1, -1
    w = a.width
    for i, (p, q) in enumerate(zip(a.pixels, b.pixels)):
        if p != q:
            x, y = i % w, i // w
            if any(_inside(x, y, m) for m in masks):
                continue
            n += 1
            x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    return n, ((x0, y0, x1, y1) if n else None)


def region_tags(a, b, masks=()):
    """The known tags of the pixels where A and B differ: fullstop in its
    cell, walls in the 3D view, resume elsewhere."""
    tags = set()
    w = a.width
    for i, (p, q) in enumerate(zip(a.pixels, b.pixels)):
        if p != q:
            x, y = i % w, i // w
            if any(_inside(x, y, m) for m in masks):
                continue
            tags.add("fullstop" if _inside(x, y, FULLSTOP_CELL)
                     else "walls" if _inside(x, y, VIEW_AREA) else "resume")
    return tags


# ------------------------------------------------ the port at the same point

MARK = re.compile(r"\\s([^.]*)\.")


def port_keys(keys):
    """KEYS without the box's screenshots (\\sNAME.), for eclplay."""
    return MARK.sub("", keys)


def frames_at(keys, out, binary=None, names=None, fixed=False, **kw):
    """The port's screen at each \\sNAME. of KEYS (or only NAMES): eclplay
    run on the keys before the mark stops at the key read that follows, and
    its last shot there is the screen the box's \\sNAME. takes at that wait.
    BINARY: another eclplay; by default stopping_eclplay(FIXED), the port
    (with the known fixes when FIXED) that ends where the keys ran out. KW go
    to cokbox.eclplay (machine, saves, args, start_menu, block). Returns
    {name: Screen, or None when eclplay took no shot}."""
    out = pathlib.Path(out)
    if binary is None:
        binary = stopping_eclplay(fixed)
    marks = [(m.group(1), keys[:m.start()]) for m in MARK.finditer(keys)
             if names is None or m.group(1) in names]

    def one(item):
        name, prefix = item
        _, _, shots = cokbox.eclplay(port_keys(prefix), out / f"at-{name}", binary=binary,
                                     shots=True, **kw)
        return name, (cokbox.Screen.load(shots[-1]) if shots else None)

    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        return dict(pool.map(one, marks))


def frame_after(keys, out, binary=None, fixed=False, **kw):
    """The port's screen at the key read after all of KEYS."""
    if binary is None:
        binary = stopping_eclplay(fixed)
    _, _, shots = cokbox.eclplay(port_keys(keys), out, binary=binary, shots=True, **kw)
    return cokbox.Screen.load(shots[-1]) if shots else None


# --------------------------------------------------------- builds of the port

def _sources(kind):
    make = (ROOT / "Makefile").read_text()
    adventure = re.search(r"^ADVENTURE_SRC = (.*)$", make, re.M).group(1).split()
    if kind == "eclplay":
        return ["src/eclplay.c", "src/keyscript.c", *adventure]
    return ["src/cok.c", "src/keymap.c", "src/keyscript.c", *adventure]


def _patch(src_dir, reverse=False, dry=True):
    args = ["patch", "-p1", "-s", "-f", "--no-backup-if-mismatch", "-d", str(src_dir.parent),
            "-i", str(KNOWN_FIXES)]
    if reverse:
        args.append("-R")
    if dry:
        args.append("--dry-run")
    return subprocess.run(args, capture_output=True, text=True).returncode == 0


def fixes_state(src=None):
    """'applied' when the source has the known fixes already, 'missing' when
    the patch applies to it, else an error (the patch is stale)."""
    src = pathlib.Path(src or cokbox.PORT_SRC)
    if _patch(src, reverse=True):
        return "applied"
    if _patch(src):
        return "missing"
    raise cokbox.BoxError(f"{KNOWN_FIXES.name} neither applies to {src} nor is in it: "
                          "update it from the fixes")


def build(kind="eclplay", fixed=False, transforms=()):
    """KIND (eclplay or cok) built from a copy of the port's source
    (cokbox.PORT_SRC), with the known fixes when FIXED (none needed once the
    source has them: the port's own build then, unless TRANSFORMS) and
    TRANSFORMS: (file name, function of its text returning the new text),
    each of which must change something. Cached under build/checks/ by the
    sources' hash. Returns the executable's path."""
    src = pathlib.Path(cokbox.PORT_SRC)
    if fixed and fixes_state(src) == "applied":
        fixed = False
    if not fixed and not transforms:
        return cokbox.COK if kind == "cok" else cokbox.ECLPLAY
    digest = hashlib.sha256()
    for f in sorted(src.glob("*.[ch]")):
        digest.update(f.name.encode() + f.read_bytes())
    digest.update(KNOWN_FIXES.read_bytes() if fixed else b"-")
    for name, fn in transforms:
        digest.update(name.encode() + fn(name, (src / name).read_text()).encode())
    digest.update(kind.encode())
    out = ROOT / "build" / "checks" / digest.hexdigest()[:16]
    exe = out / kind
    if exe.exists():
        return exe
    import fcntl
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(str(out) + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)   # checks side by side build it once
        if not exe.exists():
            _build(out, exe, kind, src, fixed, transforms)
    return exe


def _build(out, exe, kind, src, fixed, transforms):
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(src, out / "src")
    if fixed and not _patch(out / "src", dry=False):
        raise cokbox.BoxError(f"{KNOWN_FIXES.name} did not apply")
    for name, fn in transforms:
        path = out / "src" / name
        text = path.read_text()
        new = fn(name, text)
        if new == text:
            raise cokbox.BoxError(f"a transform of {name} changed nothing: update it")
        path.write_text(new)
    files = [str(out / f) for f in _sources(kind)]
    flags = []
    if kind == "cok":
        flags = subprocess.run(["pkg-config", "--cflags", "--libs", "sdl2"], capture_output=True,
                               text=True, check=True).stdout.split()
    subprocess.run(["cc", f"-I{out / 'src'}", "-std=c17", "-O2", *files, *flags, "-lm",
                    "-o", str(exe) + ".tmp"], check=True)
    os.replace(str(exe) + ".tmp", exe)


def fixed_eclplay(transforms=()):
    return build("eclplay", fixed=True, transforms=transforms)


# Instrumentation of src/eclplay.c, for build(transforms=...).
DELAY_HOOK = ("static void delay(cok_adventure *game, unsigned ms, void *context)\n{\n"
              "    (void)game;\n    player *p = context;\n")


def shot_at_delays(name, text):
    """src/eclplay.c: a --shots frame at each of the game's delays as well
    (Crt.Delay's pauses), so that texts shown during a pause have one."""
    if DELAY_HOOK not in text:
        raise cokbox.BoxError("src/eclplay.c's delay hook changed: update shot_at_delays")
    return text.replace(DELAY_HOOK, DELAY_HOOK + (
        "    if (p->shots != NULL && p->game != NULL && ms > 0) {\n"
        "        char path[4096];\n"
        "        snprintf(path, sizeof path, \"%s/%03u.bmp\", p->shots, p->shot++);\n"
        "        save(p, path);\n    }\n"))


# eclplay --start-menu goes back to the start menu when the keys run out in
# the adventure (its loop goes on after an adventure whose input ended) and
# takes a last shot there, which frames_at would take for the screen at the
# mark (batch B found it). This build ends where the keys ran out.
STOP_LINE = "        if (status != COK_ECL_OK || vm->abort || game->quit) return status;\n"


def stop_at_input_end(name, text):
    """src/start.c: cok_start_game ends when the adventure's keys ran out."""
    if STOP_LINE not in text:
        raise cokbox.BoxError("src/start.c's adventure loop changed: update stop_at_input_end")
    return text.replace(STOP_LINE, STOP_LINE.replace("game->quit)", "game->quit ||\n"
                                                     "            game->input_ended)"))


def stopping_eclplay(fixed=False):
    """The port's eclplay (with the known fixes when FIXED) that stops when
    the keys run out in the adventure: frames_at's default."""
    return build("eclplay", fixed=fixed, transforms=[("start.c", stop_at_input_end)])


def eclplay_with_pauses(fixed=False):
    """eclplay saving a shot at each key read and each delay."""
    return build("eclplay", fixed=fixed, transforms=[("eclplay.c", shot_at_delays)])


# ------------------------------------------------------------- a few helpers

def watch(box, seconds, out=None, prefix="watch", until=None):
    """Screenshots as fast as they come for SECONDS (or until UNTIL()):
    the distinct ones in order (saved as PREFIX-NN.png in OUT)."""
    import time
    seen = []
    end = time.monotonic() + seconds
    while time.monotonic() < end and box.running and not (until and until()):
        shot = box.screenshot()
        if not seen or shot.pixels != seen[-1].pixels:
            if out:
                shot.save_png(pathlib.Path(out) / f"{prefix}-{len(seen):02d}.png")
            seen.append(shot)
    return seen


def workdir(given, name):
    import tempfile
    return (pathlib.Path(given).resolve() if given
            else pathlib.Path(tempfile.mkdtemp(prefix=f"check-{name}-")))
