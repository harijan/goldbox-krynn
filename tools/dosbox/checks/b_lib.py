"""Helpers for batch B's checks (camp, magic, items, effects and saving).

Each check script runs alone (`python3 tools/dosbox/checks/b_<name>.py`),
reaching a camp in the original (DOSBox-X, cokbox.Box) and in the port
(eclplay) the same way: a saved game in SAVE/ or one the port wrote,
loaded from the start menu, then camp keys. The state is compared through
the files a camp `Save` writes in both (judge_games, masked as
save_again.py masks them), the records read in memory against the port's
game saved at the same step (saved_by_port), and screenshots (each against
the port's at the same point of the keys: b_screens_lib, in_order). Each
check judges through a checklib.Verdict and ends through checklib.run.
"""
import pathlib
import shutil
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import cokbox  # noqa: E402
from save_again import masked  # noqa: E402,F401

ROOT = cokbox.ROOT
SAVE = ROOT / "SAVE"

# Game A's start (the outpost) to the inn's camp, as slow_poison.py goes.
A_TO_CAMP = r"\r\r\r\r\r\r\rI\r"
# A saved game's start at the outpost (a game the port saved in the inn's
# camp resumes at the outpost's menu): the inn, its Enter, the camp.
INN = r"I\r"


def load_keys(letter):
    """The title, Play, Load Saved Game LETTER, Begin Adventuring."""
    return r"\kxP\v\v\r" + letter + r"\v\v\v\v\v\r"


def save_keys(letter):
    """The camp's Save as LETTER, then No to `Quit TO DOS`."""
    return "S" + letter + "N"


# ------------------------------------------------------------ records

REC = 409


def name_of(raw):
    return raw[1:1 + min(raw[0], 15)].decode("latin-1")


def effects_at(box, ptr_addr, limit=24):
    """The effect list from the far pointer at linear PTR_ADDR: (id, minutes,
    value, on_remove, 'seg:off')."""
    out = []
    eff = box.far(ptr_addr)
    while eff is not None and len(out) < limit:
        e = box.read(eff, 9)
        out.append((e[0], struct.unpack_from("<H", e, 1)[0], e[3], e[4],
                    "%04x:%04x" % (eff >> 4, eff & 15)))
        eff = box.far(eff + 5)
        if eff is not None and eff < 0x600:
            out.append(("next into low memory", "%05x" % eff))
            break
    return out


def members(box, limit=8):
    """The party in memory: list of (linear address, 409 raw bytes)."""
    out = []
    rec = box.far(box.ds * 16 + 0x609A)
    while rec is not None and len(out) < limit:
        out.append((rec, box.read(rec, REC)))
        rec = box.far(rec + 0x17F)
    return out


def member(box, name):
    for addr, raw in members(box):
        if name_of(raw) == name:
            return addr, raw
    raise KeyError(name)


def items_of(box, rec_addr, limit=20):
    """A member's items in memory, from the far pointer at +0x143, next at +0x2a."""
    out = []
    it = box.far(rec_addr + 0x143)
    while it is not None and len(out) < limit:
        out.append((it, box.read(it, 63)))
        it = box.far(it + 0x2A)
    return out


def ecl_var(box, var):
    """An ECL variable word 0x4b00-0x4eff (the block at DS:4b28)."""
    base = box.far(box.ds * 16 + 0x4B28)
    return box.word(base + (var - 0x4B00) * 2)


def clock(box):
    """The clock's seven words from 0x4bc6: (?, minutes, tens, hours, days,
    months, years)."""
    return tuple(ecl_var(box, 0x4BC6 + i) for i in range(7))


# ------------------------------------------------------------ files

def sav(directory, letter, n):
    return (pathlib.Path(directory) / f"CHRDAT{letter}{n}.SAV").read_bytes()


def sfx(directory, letter, n):
    p = pathlib.Path(directory) / f"CHRDAT{letter}{n}.SFX"
    data = p.read_bytes() if p.exists() else b""
    return [(data[i], struct.unpack_from("<H", data, i + 1)[0], data[i + 3], data[i + 4])
            for i in range(0, len(data) - 8, 9)]


def dat_var(directory, letter, var):
    """ECL variable VAR (0x4b00-0x4eff) as SAVGAM<LETTER>.DAT holds it (a
    word each from offset 1)."""
    data = (pathlib.Path(directory) / f"SAVGAM{letter}.DAT").read_bytes()
    return struct.unpack_from("<H", data, 1 + 2 * (var - 0x4B00))[0]


def dat_clock(directory, letter):
    """The clock's seven words from 0x4bc6 in a saved game, as clock()."""
    return tuple(dat_var(directory, letter, 0x4BC6 + i) for i in range(7))


def minutes(c):
    """A clock (clock(), dat_clock()) in minutes, to the days."""
    return c[1] + 10 * c[2] + 60 * c[3] + 1440 * c[4]


def game_files(directory, letter):
    d = pathlib.Path(directory)
    return {p.name: p.read_bytes() for p in d.iterdir()
            if p.is_file() and (p.name.startswith(f"CHRDAT{letter}") or
                                p.name == f"SAVGAM{letter}.DAT")}


def _lettered(directory, letter):
    """A saved game's files, the letter in their names made a ?."""
    return {"CHRDAT?" + k[7:] if k.startswith("CHRDAT") else "SAVGAM?.DAT": v
            for k, v in game_files(directory, letter).items()}


def _offsets(x, y):
    return [i for i in range(max(len(x), len(y)))
            if i >= len(x) or i >= len(y) or x[i] != y[i]]


def judge_games(verdict, what, orig_dir, orig_letter, port_dir, port_letter):
    """The original's saved game against the port's, file by file, masked
    (save_again.masked: what the original writes from memory). Each file
    that differs is a difference in VERDICT. Returns the names that differ."""
    a, b = _lettered(orig_dir, orig_letter), _lettered(port_dir, port_letter)
    if not a:
        raise cokbox.BoxError(f"the original saved no game {orig_letter} in {orig_dir}")
    if not b:
        raise cokbox.BoxError(f"the port saved no game {port_letter} in {port_dir}")
    differ = []
    for name in sorted(set(a) | set(b)):
        if name not in a or name not in b:
            verdict.differ(f"{what}: {name}",
                           f"only in {'the original' if name in a else 'the port'}'s")
            differ.append(name)
            continue
        x, y = masked(name, a[name]), masked(name, b[name])
        if x == y:
            continue
        offs = _offsets(x, y)
        verdict.differ(f"{what}: {name}", f"{len(offs)} bytes differ (sizes {len(x)}, {len(y)}), "
                       "original/port: " +
                       " ".join(f"{i:#x}:{x[i] if i < len(x) else '-'}/{y[i] if i < len(y) else '-'}"
                                for i in offs[:16]) + (" ..." if len(offs) > 16 else ""))
        differ.append(name)
    same = len(set(a) | set(b)) - len(differ)
    if differ:
        print(f"  {what}: {same} files the same (masked), {len(differ)} differ", flush=True)
    else:
        verdict.ok(what, f"all {same} files the same (masked)")
    return differ


def copy_game(src_dir, letter, dst_dir, new_letter=None):
    """Copy saved game LETTER's files into DST_DIR (as NEW_LETTER)."""
    dst = pathlib.Path(dst_dir)
    dst.mkdir(parents=True, exist_ok=True)
    new_letter = new_letter or letter
    for p in pathlib.Path(src_dir).iterdir():
        if p.is_file() and (p.name.startswith(f"CHRDAT{letter}") or p.name == f"SAVGAM{letter}.DAT"):
            name = "CHRDAT" + new_letter + p.name[7:] if p.name.startswith("CHRDAT") else \
                f"SAVGAM{new_letter}.DAT"
            shutil.copyfile(p, dst / name)
    if new_letter != letter:  # the .DAT names the files: CHRDAT<letter><n>
        dat = dst / f"SAVGAM{new_letter}.DAT"
        data = bytearray(dat.read_bytes())
        for k in range(data[0x1414]):
            slot = 0x1415 + 41 * k
            if data[slot + 1:slot + 7] == b"CHRDAT":
                data[slot + 7] = ord(new_letter)
        dat.write_bytes(bytes(data))
    return dst


def patch(path, edits):
    """Write bytes into a file: EDITS {offset: bytes or int}."""
    p = pathlib.Path(path)
    data = bytearray(p.read_bytes())
    for off, v in edits.items():
        v = bytes([v]) if isinstance(v, int) else bytes(v)
        data[off:off + len(v)] = v
    p.write_bytes(bytes(data))


def write_sfx(path, effects):
    """A .SFX of EFFECTS (id, minutes, value, on_remove)."""
    pathlib.Path(path).write_bytes(b"".join(
        struct.pack("<BHBB4x", i, m, v, r) for i, m, v, r in effects))


# ------------------------------------------------------------ the port

def port(keys, out, args, saves=(), shots=False, block=None, machine="tandy", binary=None):
    """eclplay (cokbox.ECLPLAY, or BINARY)
    from a block or --load (not the start menu), BLOCK after the assets;
    returns (stdout and stderr, saves dir, shots)."""
    import subprocess
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    shot_dir, save_dir = out / "shots", out / "saves"
    for d in (shot_dir, save_dir):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir()
    for s in saves:
        s = pathlib.Path(s)
        for f in ([s] if s.is_file() else sorted(s.iterdir())):
            if f.is_file():
                shutil.copyfile(f, save_dir / f.name.upper())
    cfg = out / "krynn.cfg"
    cfg.write_bytes(cokbox.krynn_cfg(cokbox.MACHINES[machine][1]).encode())
    exe = binary or cokbox.ECLPLAY
    result = subprocess.run(
        [str(exe), "--config", str(cfg), "--saves", str(save_dir),
         *(["--shots", str(shot_dir)] if shots else []), *map(str, args), "--keys", keys,
         str(ROOT / "Assets"), *([str(block)] if block is not None else [])],
        check=False, capture_output=True, text=True, timeout=600)
    if result.returncode and not result.stdout:
        raise cokbox.BoxError("eclplay: " + result.stderr.strip())
    return result.stdout + result.stderr, save_dir, sorted(shot_dir.glob("*.bmp"),
                                                         key=lambda p: int(p.stem))


def saved_by_port(game, letter, keys, out, save_as="F"):
    """The game the port saves (as SAVE_AS) after loading GAME's LETTER,
    the outpost to the inn's camp and KEYS (which end in the camp's menu):
    its saves directory: the port's state at a step."""
    _, saves, _ = port(A_TO_CAMP + keys + save_keys(save_as), out,
                       ["--load", pathlib.Path(game) / f"SAVGAM{letter}.DAT", "--play"],
                       saves=[game])
    return saves


def keep_saves(box, dst):
    """The original's SAVE directory as it is now, copied to DST."""
    shutil.copytree(box.game / "SAVE", dst, dirs_exist_ok=True)
    return pathlib.Path(dst)


# The screen's frame: the pillars at columns 0-7 and 312-319.
PILLARS = ((0, 8), (312, 320))


def in_order(verdict, what, seen, frames, counts=None):
    """Screens the original showed between two key reads (SEEN, distinct, in
    order) against the port's frames there (FRAMES, in order: its shots at
    each delay and the key read, eclplay_with_pauses): each must be one of
    the port's, at or after the previous one's; or a screen caught while the
    game drew (see caught; COUNTS: the screenshots running that showed each
    of SEEN). Returns the indexes matched (None for those caught while
    drawing)."""
    found = []
    at = 0
    for shot in seen:
        k = next((k for k in range(at, len(frames)) if shot.pixels == frames[k].pixels), None)
        found.append(k)
        at = k if k is not None else at
    matched = []
    for i, (shot, k) in enumerate(zip(seen, found)):
        if k is not None:
            verdict.ok(f"{what} {i}", f"the port's frame {k}")
            matched.append(k)
            continue
        before = next((found[j] for j in range(i - 1, -1, -1) if found[j] is not None), 0)
        after = next((found[j] for j in range(i + 1, len(seen)) if found[j] is not None), None)
        why = caught(shot, frames, before, after, counts[i] if counts else None)
        matched.append(None)
        if why:
            verdict.ok(f"{what} {i}", why)
        else:
            best = min(range(len(frames)), key=lambda n: shot.diff(frames[n])[0])
            verdict.differ(f"{what} {i}", f"not one of the port's frames {before}-"
                           f"{len(frames) - 1} (nearest of all: {best}, "
                           f"{shot.diff(frames[best])[0]} pixels)")
    return matched


def caught(shot, frames, before, after, count=None):
    """Why SHOT, which is none of the port's FRAMES, was caught while the
    original drew between the port's frames BEFORE and AFTER (the matches
    of the screens around it), or None. Each of its pixels must be one of
    two frames in a row between them, K and K + 1 (the next drawn over the
    last; a frame the original showed too briefly to be shot may lie
    between BEFORE and AFTER); or black or a pillar's: the original then
    redraws the screen's frame and clears its window (the pillars at
    columns 0-7 and 312-319, which a full-width window such as the spell
    list covers) before it draws the next, slowly enough to be seen at
    normal speed, where the port has no frame. The screens before and after
    must match those frames, so nothing the port lacks is left on them.
    Last, a screen seen in one screenshot only (COUNT 1) between frames one
    apart: the original was drawing the next (a list's rows, then its
    highlight), as a screen held for a pause or a key wait mostly stays for
    several screenshots running (b_item_spell0 prints the counts); it is
    not compared."""
    if after is None or after < before:
        return None
    w = shot.width
    pairs = [(k, min(k + 1, after)) for k in range(before, max(after, before + 1))]
    for k, n in pairs:
        if all(p == x or p == y for p, x, y in zip(shot.pixels, frames[k].pixels,
                                                    frames[n].pixels)):
            return f"caught between the port's {k} and {n}"
    for k, n in pairs:
        if all(p == x or p == y or p == 0 or any(lo <= i % w < hi for lo, hi in PILLARS)
               for i, (p, x, y) in enumerate(zip(shot.pixels, frames[k].pixels,
                                                 frames[n].pixels))):
            return (f"caught between the port's {k} and {n} as the original redraws the "
                    "frame and clears the window")
    if count == 1 and after == before + 1:
        return (f"seen in one screenshot between the port's {before} and {after}, as the "
                f"original draws the next: not compared ({shot.diff(frames[after])[0]} pixels "
                "differ from the next)")
    return None
    a, b = frames[before].pixels, frames[after].pixels
    if all(p == x or p == y for p, x, y in zip(shot.pixels, a, b)):
        return f"caught between the port's {before} and {after}"
    w = shot.width
    if all(p == x or p == y or p == 0 or any(lo <= n % w < hi for lo, hi in PILLARS)
           for n, (p, x, y) in enumerate(zip(shot.pixels, a, b))):
        return (f"caught between the port's {before} and {after} as the original redraws "
                "the frame and clears the window")
    if count == 1 and after == before + 1:
        return (f"seen in one screenshot between the port's {before} and {after}, as the "
                f"original draws the next: not compared ({shot.diff(frames[after])[0]} pixels "
                "differ from the next)")
    return None


def box(work, saves, **kw):
    kw.setdefault("timeout", 900)
    return cokbox.Box(workdir=pathlib.Path(work) / "box", saves=saves, **kw)


def wait_rest(box, seconds=1500, still=2.0, each=None):
    """After a rest is started: wait until the clock has not moved for STILL
    seconds while the game waits for a key (a rest polls the keyboard each
    tick, so waiting alone does not say it ended). EACH(box) is called each
    round (screenshots, watching a record). Returns the clock."""
    import time
    end = time.monotonic() + seconds
    last, since = clock(box), time.monotonic()
    while box.running and time.monotonic() < end:
        if each:
            each(box)
        now = clock(box)
        if now != last:
            last, since = now, time.monotonic()
        elif time.monotonic() - since > still and box.waiting():
            break
        time.sleep(0.1)
    return last
