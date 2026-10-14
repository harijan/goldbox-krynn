"""Helpers for batch B's checks (camp, magic, items, effects and saving).

Each check script runs alone (`python3 tools/dosbox/checks/b_<name>.py`),
reaching a camp in the original (DOSBox-X, cokbox.Box) and in the port
(eclplay) the same way: a saved game in SAVE/ or one the port wrote,
loaded from the start menu, then camp keys. The state is compared through
the files a camp `Save` writes in both (masked as save_again.py masks
them), the records read in memory, and screenshots.
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


def game_files(directory, letter):
    d = pathlib.Path(directory)
    return {p.name: p.read_bytes() for p in d.iterdir()
            if p.is_file() and (p.name.startswith(f"CHRDAT{letter}") or
                                p.name == f"SAVGAM{letter}.DAT")}


def compare_games(orig_dir, orig_letter, port_dir, port_letter, report=print):
    """Compare two saved games file by file, masked; returns the differing
    (name, offsets) list. Names are compared with the letters made equal."""
    a = {"CHRDAT?" + k[7:] if k.startswith("CHRDAT") else "SAVGAM?.DAT": v
         for k, v in game_files(orig_dir, orig_letter).items()}
    b = {"CHRDAT?" + k[7:] if k.startswith("CHRDAT") else "SAVGAM?.DAT": v
         for k, v in game_files(port_dir, port_letter).items()}
    differ = []
    for name in sorted(set(a) | set(b)):
        if name not in a or name not in b:
            report(f"  {name}: only in {'the original' if name in a else 'the port'}'s")
            differ.append((name, None))
            continue
        x, y = masked(name, a[name]), masked(name, b[name])
        if x != y:
            offs = [i for i in range(max(len(x), len(y)))
                    if i >= len(x) or i >= len(y) or x[i] != y[i]]
            report(f"  {name}: {len(offs)} bytes differ (sizes {len(x)}, {len(y)}): " +
                   " ".join(f"{i:#x}:{x[i] if i < len(x) else '-'}/{y[i] if i < len(y) else '-'}"
                            for i in offs[:16]) + (" ..." if len(offs) > 16 else ""))
            differ.append((name, offs))
    report(f"  {len(set(a) | set(b)) - len(differ)} files the same (masked), {len(differ)} differ")
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

def port(keys, out, args, saves=(), shots=False, block=None, machine="tandy"):
    """eclplay from a block or --load (not the start menu), BLOCK after the
    assets; returns (stdout, saves dir, shots)."""
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
    result = subprocess.run(
        [str(ROOT / "build/eclplay"), "--config", str(cfg), "--saves", str(save_dir),
         *(["--shots", str(shot_dir)] if shots else []), *map(str, args), "--keys", keys,
         str(ROOT / "Assets"), *([str(block)] if block is not None else [])],
        check=False, capture_output=True, text=True, timeout=600)
    if result.returncode and not result.stdout:
        raise RuntimeError("eclplay: " + result.stderr.strip())
    return result.stdout + result.stderr, save_dir, sorted(shot_dir.glob("*.bmp"))


def port_menu(keys, out, saves=(), shots=True):
    """eclplay --start-menu (the title first), as the Box runs."""
    return cokbox.eclplay(keys, out, saves=saves, shots=shots)


def best_match(screen, bmps):
    """The eclplay shot nearest SCREEN: (pixels differing, box, path)."""
    best = None
    for p in bmps:
        n, area = screen.diff(cokbox.Screen.load(p))
        if best is None or n < best[0]:
            best = (n, area, p)
    return best


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
