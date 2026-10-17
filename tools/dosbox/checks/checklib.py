"""What every DOSBox-X check (tools/dosbox/checks/*.py) shares: how it ends,
how it judges what it compared, the port's screen at the same point of the
keys, and the builds of the port it compares with.

A check script ends through run(main): main returns 0 (the original and
the port agree in everything compared) or 1 (something differs); an
exception, a signal or any other return is an error, exit 3, never taken
for a difference. Its Verdict prints each difference as
`DIFFERENCE: WHAT: DETAIL`, and last `VERDICT: AGREES` or `VERDICT: DIFFERS
new (...)`, which run_all.py checks against what it recorded for the script.
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
SAVES = ROOT / "SAVE"

# The 3D view's cells (3-13 across and down) and its frame.
VIEW_AREA = (24, 24, 88, 88)


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

    def ok(self, what, detail=""):
        print(f"  agrees: {what}" + (f": {detail}" if detail else ""), flush=True)

    def differ(self, what, detail=""):
        self.new.append(what)
        print(f"DIFFERENCE: {what}" + (f": {detail}" if detail else ""), flush=True)

    def check(self, good, what, detail=""):
        """One assertion: GOOD true agrees; else a difference."""
        if good:
            self.ok(what, detail)
        else:
            self.differ(what, detail)
        return bool(good)

    def value(self, what, original, port):
        """The original's value against the port's."""
        if original == port:
            self.ok(what, f"{original!r}")
            return True
        self.differ(what, f"original {original!r}, port {port!r}")
        return False

    def screen(self, what, original, port, masks=(), work=None):
        """The original's screen against the port's at the same point
        (frames_at). MASKS: (x, y, w, h, why) areas not compared, each said
        why (DOSBox-X's own cursor, ...). Returns the count of pixels
        differing from the port's."""
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
        self.differ(what, f"{count} pixels differ from the port's in {box}" + note)
        return count

    def finish(self):
        if self.new:
            print(f"VERDICT: DIFFERS new ({len(self.new)}: {', '.join(self.new[:6])}"
                  f"{' ...' if len(self.new) > 6 else ''})", flush=True)
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


# ------------------------------------------------ the port at the same point

MARK = re.compile(r"\\s([^.]*)\.")


def port_keys(keys):
    """KEYS without the box's screenshots (\\sNAME.), for eclplay."""
    return MARK.sub("", keys)


def frames_at(keys, out, binary=None, names=None, **kw):
    """The port's screen at each \\sNAME. of KEYS (or only NAMES): eclplay
    run on the keys before the mark stops at the key read that follows, and
    its last shot there is the screen the box's \\sNAME. takes at that wait.
    BINARY: another eclplay; by default stopping_eclplay(), the port that
    ends where the keys ran out. KW go
    to cokbox.eclplay (machine, saves, args, start_menu, block). Returns
    {name: Screen, or None when eclplay took no shot}."""
    out = pathlib.Path(out)
    if binary is None:
        binary = stopping_eclplay()
    marks = [(m.group(1), keys[:m.start()]) for m in MARK.finditer(keys)
             if names is None or m.group(1) in names]

    def one(item):
        name, prefix = item
        return name, _read_shot(port_keys(prefix), out / f"at-{name}", binary, kw)

    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        return dict(pool.map(one, marks))


def frame_after(keys, out, binary=None, **kw):
    """The port's screen at the key read after all of KEYS."""
    if binary is None:
        binary = stopping_eclplay()
    return _read_shot(port_keys(keys), out, binary, kw)


def _read_shot(keys, out, binary, kw):
    """The shot of the last key read (the stopping build says which, as a
    battle's setup may take one more, `battlefield`'s); else the last."""
    stdout, _, shots = cokbox.eclplay(keys, out, binary=binary, shots=True, **kw)
    if not shots:
        return None
    reads = re.findall(r"^read-shot: (\d+)$", stdout, re.M)
    if reads:
        path = pathlib.Path(out) / "shots" / f"{int(reads[-1]):03d}.bmp"
        return cokbox.Screen.load(path)
    return cokbox.Screen.load(shots[-1])


# --------------------------------------------------------- builds of the port

def _sources(kind):
    make = (ROOT / "Makefile").read_text()
    adventure = re.search(r"^ADVENTURE_SRC = (.*)$", make, re.M).group(1).split()
    if kind == "eclplay":
        return ["src/eclplay.c", "src/keyscript.c", *adventure]
    return ["src/cok.c", "src/keymap.c", "src/keyscript.c", *adventure]


def build(kind="eclplay", transforms=()):
    """KIND (eclplay or cok) built from a copy of the port's source
    (cokbox.PORT_SRC) with TRANSFORMS: (file name, function of its text
    returning the new text), each of which must change something; the
    port's own build without any. Cached under build/checks/ by the
    sources' hash. Returns the executable's path."""
    src = pathlib.Path(cokbox.PORT_SRC)
    if not transforms:
        return cokbox.COK if kind == "cok" else cokbox.ECLPLAY
    digest = hashlib.sha256()
    for f in sorted(src.glob("*.[ch]")):
        digest.update(f.name.encode() + f.read_bytes())
    for name, fn in transforms:
        digest.update(name.encode() + fn(name, (src / name).read_text()).encode())
    digest.update(kind.encode())
    out = ROOT / "build" / "checks" / digest.hexdigest()[:16]
    exe = out / kind
    if exe.exists():
        return _recorded(exe)
    import fcntl
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(str(out) + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)   # checks side by side build it once
        if not exe.exists():
            _build(out, exe, kind, src, transforms)
    return _recorded(exe)


def _recorded(exe):
    """EXE, or with COKBOX_RECORD (a program: a recorder of the port's runs,
    as the golden harness's portrec.py) a script beside it that runs
    COKBOX_RECORD with EXE and the arguments. COKBOX_ECLPLAY and COKBOX_COK
    can name such stand-ins for the port's own builds."""
    record = os.environ.get("COKBOX_RECORD")
    if not record:
        return exe
    stand_in = exe.with_name(exe.name + "-recorded")
    text = f'#!/bin/sh\nexec "{record}" "{exe}" "$@"\n'
    if not stand_in.exists() or stand_in.read_text() != text:  # another recorder's
        tmp = stand_in.with_name(f"{stand_in.name}.{os.getpid()}")
        tmp.write_text(text)
        tmp.chmod(0o755)
        os.replace(tmp, stand_in)
    return stand_in


def _build(out, exe, kind, src, transforms):
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(src, out / "src")
    for name, fn in transforms:
        path = out / "src" / name
        text = path.read_text()
        new = fn(name, text)
        if new == text:
            raise cokbox.BoxError(f"a transform of {name} changed nothing: update it")
        path.write_text(new)
    files = [out / f for f in _sources(kind)]
    cflags, libs = [], []
    if kind == "cok":
        def sdl(what):
            return subprocess.run(["pkg-config", what, "sdl2"], capture_output=True, text=True,
                                  check=True).stdout.split()
        cflags, libs = sdl("--cflags"), sdl("--libs")

    # Each file compiled on its own, side by side on half the cores (and
    # below the boxes running beside it), then linked.
    def compile_one(f):
        obj = f.with_suffix(".o")
        subprocess.run([*cokbox.BELOW_BOXES, "cc", f"-I{out / 'src'}", "-std=c17", "-O2",
                        *cflags, "-c", str(f), "-o", str(obj)], check=True)
        return str(obj)

    with concurrent.futures.ThreadPoolExecutor(max(1, (os.cpu_count() or 4) // 2)) as pool:
        objs = list(pool.map(compile_one, files))
    subprocess.run(["cc", *objs, *libs, "-lm", "-o", str(exe) + ".tmp"], check=True)
    os.replace(str(exe) + ".tmp", exe)


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


READ_SHOT = '''        snprintf(path, sizeof path, "%s/%03u.bmp", p->shots, p->shot++);
        if (p->crt != NULL)
'''


def print_read_shot(name, text):
    """src/eclplay.c: print `read-shot: N` for the shot of each key read
    (next_key's), which frames_at takes."""
    if READ_SHOT not in text:
        raise cokbox.BoxError("src/eclplay.c's next_key changed: update print_read_shot")
    return text.replace(READ_SHOT, READ_SHOT.replace(
        "p->shot++);\n", "p->shot++);\n        printf(\"read-shot: %u\\n\", p->shot - 1);\n"))


def stopping_eclplay():
    """The port's eclplay that stops when the keys run out in the
    adventure and says which shot is each key read's: frames_at's default."""
    return build("eclplay", transforms=[("start.c", stop_at_input_end),
                                        ("eclplay.c", print_read_shot)])


def eclplay_with_pauses():
    """eclplay saving a shot at each key read and each delay."""
    return build("eclplay", transforms=[("eclplay.c", shot_at_delays)])


# ------------------------------------------------------------- a few helpers

def watch(box, seconds, out=None, prefix="watch", until=None):
    """Screenshots as fast as they come for SECONDS (or until UNTIL()):
    the distinct ones in order (saved as PREFIX-NN.png in OUT), each with
    .seen, the screenshots in a row that showed it (1: a screen the game was
    drawing through, caught once)."""
    import time
    seen = []
    end = time.monotonic() + seconds
    while time.monotonic() < end and box.running and not (until and until()):
        shot = box.screenshot()
        if not seen or shot.pixels != seen[-1].pixels:
            if out:
                shot.save_png(pathlib.Path(out) / f"{prefix}-{len(seen):02d}.png")
            shot.seen = 1
            seen.append(shot)
        else:
            seen[-1].seen += 1
    return seen


def workdir(given, name):
    import tempfile
    if not given:
        return pathlib.Path(tempfile.mkdtemp(prefix=f"check-{name}-"))
    path = pathlib.Path(given).resolve()
    path.mkdir(parents=True, exist_ok=True)
    return path
