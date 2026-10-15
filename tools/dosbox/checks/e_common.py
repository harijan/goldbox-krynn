"""Helpers shared by batch E's checks (freed and garbage memory where the
port stops): saved games patched, the port's frames at the same point of
the keys from an eclplay that stops there, the party's records and
effects, Turbo Pascal's heap and the interrupt table read in the original,
whether the game still takes keys, and screens caught between key reads
matched in order with the port's.
"""
import concurrent.futures
import pathlib
import shutil
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402

cokbox = checklib.cokbox

SAVE = cokbox.ROOT / "SAVE"
# Turbo Pascal 6's System variables (DS): HeapOrg, HeapPtr, FreeList,
# PrefixSeg (checks/slow_poison.py), and RandSeed.
HEAP_ORG, HEAP_PTR, FREE_LIST, PREFIX_SEG = 0x4386, 0x438A, 0x4392, 0x43A8
RANDSEED = cokbox.RANDSEED
PARTY = 0x609A          # the list of records (far pointer), next at +0x17f
# Title, Play, Load Saved Game LETTER, Begin Adventuring.
LOAD = r"\kxP\v\v\r{}\v\v\v\v\v\r"


def next_seed(seed, steps=1):
    """Turbo Pascal's Random: RandSeed * 0x08088405 + 1, STEPS times."""
    for _ in range(steps):
        seed = (seed * 0x08088405 + 1) & 0xFFFFFFFF
    return seed


# ------------------------------------------------------------ the port's side

# src/eclplay.c's next_key, where the shot of a key read is saved.
_SHOT_SAVED = "        else\n            save(p, path);\n    }\n"
_FRONT_END = ("        }\n        cok_adventure_close(&game);\n        return result;\n    }\n"
              "    if (!has_block)")


def _stop_at_end(poke):
    """A transform of src/eclplay.c: at the first key read with no keys
    left, after its shot, print `e: out of keys, seed N` (Turbo Pascal's
    RandSeed then) and end, as the box's screenshot at that wait ends what
    is compared (eclplay would otherwise go on: in a battle the player's
    turns end and the run reaches the start menu again, whose shot
    checklib.frames_at would take). Where the run stops instead, `e:
    stopped, seed N`. With POKE (C code with `game`, the cok_adventure *),
    it runs at the key read whose shot is number $E_POKE_AT, after that
    shot: the same change the check makes in the original's memory there."""
    def transform(name, text):
        if text.count(_SHOT_SAVED) != 1 or text.count(_FRONT_END) != 1:
            raise cokbox.BoxError("src/eclplay.c's next_key or run changed: update e_common")
        hook = (
            "    if (!cok_key_script_owed(&p->keys)) {\n"
            "        const char *poke_at = getenv(\"E_POKE_AT\");\n"
            "        if (poke_at != NULL && p->game != NULL &&\n"
            "            p->shot == strtoul(poke_at, NULL, 10)) {\n"
            "            cok_adventure *game = p->game;\n"
            f"            {poke or '(void)game;'}\n"
            "            printf(\"e: poked after shot %u\\n\", p->shot - 1u);\n"
            "        }\n"
            "        if (p->buffered == 0 && p->keys.keys != NULL && p->keys.keys[0] == '\\0') {\n"
            "            if (p->game != NULL)\n"
            "                printf(\"e: out of keys, seed %lu\\n\", "
            "(unsigned long)p->game->vm.seed);\n"
            "            fflush(stdout);\n"
            "            exit(0);\n"
            "        }\n"
            "    }\n")
        text = text.replace(_SHOT_SAVED, _SHOT_SAVED + hook)
        return text.replace(_FRONT_END, (
            "        }\n"
            "        printf(\"e: stopped, seed %lu\\n\", (unsigned long)game.vm.seed);\n"
            "        cok_adventure_close(&game);\n        return result;\n    }\n"
            "    if (!has_block)"))
    return transform


def eclplay(fixed=False, poke=None, pauses=False):
    """The port's eclplay (with the known fixes if FIXED) that stops at the
    end of its keys (see _stop_at_end), with POKE, and with PAUSES a shot at
    each of the game's delays too (checklib.shot_at_delays)."""
    transforms = [("eclplay.c", checklib.shot_at_delays)] if pauses else []
    transforms.append(("eclplay.c", _stop_at_end(poke)))
    return checklib.build("eclplay", fixed=fixed, transforms=transforms)


def run_port(keys, out, binary, **kw):
    """eclplay (BINARY) on KEYS (the box's, \\sNAME. dropped): (stdout, its
    saves directory, its shots' paths in the order taken)."""
    return cokbox.eclplay(checklib.port_keys(keys), out, binary=binary, shots=True, **kw)


def port_at(keys, out, binary, names=None, **kw):
    """As checklib.frames_at, with BINARY one of eclplay() (which ends at
    the end of its keys, so its last shot is the screen at that key read),
    and the run's stdout: {name: (Screen or None, stdout)}."""
    out = pathlib.Path(out)
    marks = [(m.group(1), keys[:m.start()]) for m in checklib.MARK.finditer(keys)
             if names is None or m.group(1) in names]

    def one(item):
        name, prefix = item
        text, _, shots = run_port(prefix, out / f"at-{name}", binary, **kw)
        return name, ((cokbox.Screen.load(shots[-1]) if shots else None), text)

    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        return dict(pool.map(one, marks))


def port_seed(text):
    """The seed eclplay() printed where it ended (None if it did not)."""
    for line in text.splitlines()[::-1]:
        if line.startswith(("e: out of keys, seed ", "e: stopped, seed ")):
            return int(line.rsplit(" ", 1)[1])
    return None


def port_error(text):
    """The `error:` line the port's run stopped with, or None."""
    errors = [line for line in text.splitlines() if line.startswith("error:")]
    return errors[-1][len("error: "):] if errors else None


def poke_at(shot):
    """Where eclplay(poke=...) pokes: after its shot number SHOT (None: never)."""
    import os
    if shot is None:
        os.environ.pop("E_POKE_AT", None)
    else:
        os.environ["E_POKE_AT"] = str(shot)


class Pair:
    """The port's frames at the marks (\\sNAME.) of one key string (with
    POKE: see eclplay and poke_at), and the fixed port's on demand:
    judge(verdict, name, the original's screen)."""

    def __init__(self, keys, out, poke=None, **kw):
        self.keys, self.out, self.poke, self.kw = keys, pathlib.Path(out), poke, kw
        self.at = port_at(keys, self.out / "port", eclplay(poke=poke), **kw)
        self._fixed = None

    def frame(self, name):
        return self.at[name][0]

    def text(self, name):
        return self.at[name][1]

    def fixed(self, name):
        if self._fixed is None:
            self._fixed = port_at(self.keys, self.out / "port-fixed",
                                  eclplay(fixed=True, poke=self.poke), **self.kw)
        return self._fixed[name][0]

    def judge(self, verdict, name, shot, what=None, masks=()):
        return verdict.screen(what or f"screen {name}", shot, self.frame(name),
                              fixed=lambda: self.fixed(name), masks=masks, work=self.out)


def marked(steps):
    """STEPS [(keys, name)] as one key string with a \\sNAME. after each."""
    return "".join(rf"{k}\s{name}." for k, name in steps)


def loop_of(frames, least=3):
    """(L, P) when FRAMES from L on repeat with period P (at least LEAST
    frames seen twice), the smallest L, then P; else None."""
    n = len(frames)
    for start in range(n):
        for period in range(1, n - start - least + 1):
            if all(frames[start + i].pixels == frames[start + period + i].pixels
                   for i in range(n - start - period)):
                return start, period
    return None


def in_order(verdict, what, shots, frames, loop_from=None, cleared=(), moving=False,
             settled=True):
    """SHOTS, screens of the original caught between two key reads in
    order, against FRAMES, the port's over the same stretch (its shots at
    each delay and key read): each must equal a frame at or after the one
    the screen before it matched; with LOOP_FROM, after the last frame
    comes frame LOOP_FROM again (a loop the original goes round). A screen
    other than the last (any, unless SETTLED: the last is taken at a key
    wait) may have been caught while DOSBox-X drew the next frame: then each of its pixels must be that of a frame or of the frame
    after it (two in a row), or black inside CLEARED, the areas (x, y, w,
    h) the port fills with black before it draws them again between those
    two frames (a list's window: menu.c's draw_list), which the original
    was caught between clearing and drawing; with MOVING (a missile's
    picture moved from cell to cell: the port's arena.c restores what it
    covered, then draws it at the next cell), inside the box where the two
    frames differ, what one of FRAMES shows there (the picture taken away
    and not yet drawn again: the ground under it). Nothing else."""
    n = len(frames)
    k, matched = 0, []

    def after(j):
        return j + 1 if j + 1 < n else loop_from

    def drawing(shot, a, b):
        w = shot.width
        box = checklib.masked_diff(a, b)[1] if moving else None
        for i, (p, q, r) in enumerate(zip(shot.pixels, a.pixels, b.pixels)):
            if p == q or p == r:
                continue
            x, y = i % w, i // w
            if p == 0 and any(checklib._inside(x, y, c) for c in cleared):
                continue
            if box and box[0] <= x <= box[2] and box[1] <= y <= box[3] and \
                    any(f.pixels[i] == p for f in frames):
                continue
            return False
        return True

    for i, shot in enumerate(shots):
        order = list(range(k, n)) + (list(range(loop_from, k)) if loop_from is not None else [])
        found = next((j for j in order if shot.pixels == frames[j].pixels), None)
        how = "equal"
        if found is None and (i < len(shots) - 1 or not settled):
            for j in order:
                if after(j) is not None and drawing(shot, frames[j], frames[after(j)]):
                    found, how = j, "drawing"
                    break
        if found is None:
            best = min(range(n), key=lambda j: shot.diff(frames[j])[0]) if n else None
            verdict.differ(what, f"screen {i} of {len(shots)} matches no frame of the port's "
                           f"at or after its frame {k}"
                           + (f" (nearest: frame {best}, {shot.diff(frames[best])[0]} pixels)"
                              if best is not None else ""))
            return False
        matched.append(found if how == "equal" else f"{found}+{after(found)}")
        k = found
    verdict.ok(what, f"{len(shots)} screens, the port's frames {compress(matched)}")
    return True


def compress(items):
    """A list as text, runs of an item repeated shown once with a count."""
    out = []
    for x in items:
        if out and out[-1][0] == x:
            out[-1][1] += 1
        else:
            out.append([x, 1])
    return "[" + ", ".join(f"{x}" + (f" x{c}" if c > 1 else "") for x, c in out) + "]"


# ------------------------------------------------------------ saved games

def patched_saves(out, patches, source=SAVE):
    """SOURCE's saved games copied to OUT with PATCHES applied: {file name:
    {offset: bytes}}."""
    out = pathlib.Path(out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for f in pathlib.Path(source).iterdir():
        if f.is_file():
            shutil.copyfile(f, out / f.name)
    for name, changes in patches.items():
        data = bytearray((out / name).read_bytes())
        for off, value in changes.items():
            value = bytes([value]) if isinstance(value, int) else bytes(value)
            data[off:off + len(value)] = value
        (out / name).write_bytes(data)
    return out


def make_c(work, source=SAVE):
    """Game C: SOURCE's game A party after Throtl's gate fight (block 32,
    4be6=1, the combat passed), camped and saved there by the port; a
    directory with SOURCE's games and C."""
    work = pathlib.Path(work)
    text, saves_c, _ = cokbox.eclplay(
        r"\r\rEESC", work / "port-save-c", start_menu=False, shots=False, block=32,
        args=["--combat", "pass", "--party", pathlib.Path(source) / "SAVGAMA.DAT", "--play",
              "--set", "4be6=1"])
    if not (saves_c / "SAVGAMC.DAT").exists():
        raise cokbox.BoxError("the port did not save game C:\n" + text[-2000:])
    out = work / "saves-c"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for d in (pathlib.Path(source), saves_c):
        for f in d.iterdir():
            if f.is_file():
                shutil.copyfile(f, out / f.name)
    return out


# ------------------------------------------------------------ the original

def records(box, limit=40):
    """The linear addresses of the list's records from DS:609a."""
    out = []
    rec = box.far(box.ds * 16 + PARTY)
    while rec is not None and len(out) < limit:
        out.append(rec)
        rec = box.far(rec + 0x17F)
    return out


def effects(box, rec, limit=30):
    """A record's effects: (id, minutes, value, node seg:off)."""
    out = []
    eff = box.far(rec + 0xE3)
    while eff is not None and len(out) < limit:
        e = box.read(eff, 9)
        out.append((e[0], struct.unpack_from("<H", e, 1)[0], e[3], "%05x" % eff))
        nxt = box.far(eff + 5)
        if nxt is not None and nxt < 0x600:
            out.append(("next into low memory", "%05x" % nxt))
            break
        eff = nxt
    return out


def name(box, rec):
    raw = box.read(rec, 16)
    return raw[1:1 + min(raw[0], 15)].decode("latin-1")


def party(box):
    """(name, status, hit points, strength, exceptional, effects) each."""
    out = []
    for rec in records(box):
        raw = box.read(rec, 409)
        out.append((name(box, rec), raw[0x188], raw[0x197], raw[0x11], raw[0x1C],
                    effects(box, rec)))
    return out


def heap(box):
    """(HeapOrg, HeapPtr, the free list [(seg:off, size)]) as text."""
    def ptr(off):
        o, sg = struct.unpack("<HH", box.ds_read(off, 4))
        return sg, o
    blocks = []
    seg, off = ptr(FREE_LIST)
    top = ptr(HEAP_PTR)
    while (seg, off) != top and len(blocks) < 24:
        nxt_o, nxt_s, size_o, size_s = struct.unpack("<4H", box.read(seg * 16 + off, 8))
        blocks.append("%04x:%04x+%x" % (seg, off, size_s * 16 + size_o))
        seg, off = nxt_s, nxt_o
    return "%04x:%04x" % ptr(HEAP_ORG), "%04x:%04x" % top, blocks


def heap_text(h):
    return "HeapOrg %s, HeapPtr %s, free list [%s]" % (h[0], h[1], ", ".join(h[2]) or "empty")


def low_memory(box):
    return box.read(0, 0x600)


# Low memory that changes by itself while the game runs, left out of
# low_changes: the BIOS's keyboard flags and buffer (0040:0017-0018,
# 001a-003f: the keys typed), the diskette motor's count (0040:0040, which
# INT 8 counts down at each tick), the cursor positions (0040:0050-0051:
# the video BIOS's), the clock's ticks (0040:006c-0071) and the
# intra-applications area (0040:00f0-00ff, which DOSBox-X uses).
LOW_SKIP = [(0x417, 0x419), (0x41A, 0x440), (0x440, 0x442), (0x450, 0x452), (0x46C, 0x472),
            (0x4F0, 0x500)]


def low_changes(before, after):
    """Words of the interrupt table and BIOS data that changed (but
    LOW_SKIP's), as 'OFFS: OLD -> NEW'."""
    out = []
    for i in range(0, 0x600, 2):
        if before[i:i + 2] != after[i:i + 2] and not any(a <= i < b for a, b in LOW_SKIP):
            out.append(f"{i:04x}: {before[i:i + 2].hex()} -> {after[i:i + 2].hex()}")
    return out


def takes_keys(box, seconds=20):
    """Whether the game waits for a key within SECONDS, and where (or why
    not). DOSBox-X ending is an error, not an answer."""
    try:
        box.wait_input(seconds=seconds)
        return True, box.where()
    except cokbox.BoxError as e:
        if not box.running:
            raise
        return False, str(e)


def text_row(shot, row):
    """Row ROW of a 320x200 screen read with the game's font (d_lib's)."""
    import d_lib
    return d_lib.read_text(shot, row)[0].rstrip()


def to_battle_end(box, seconds=240):
    """The battle on Auto (Alt-Q at each player's menu, after Escape at
    Aim's): (Alt-Qs typed, where the game then waits). 3995:0b01 is a
    player's combat menu, 432f:320d Aim's."""
    import time
    end = time.monotonic() + seconds
    turns, where = 0, None
    while time.monotonic() < end:
        took, where = takes_keys(box, 30)
        if not took or ("3995:0b01" not in where and "432f:320d" not in where):
            break
        box.keys(r"\e\@q" if "432f:320d" in where else r"\@q")
        turns += 1
    return turns, where


# 3995:0c9c's menu: the battle is over (Continue battle / the results).
BATTLE_OVER = "3995:0c9c"
