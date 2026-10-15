"""Shared steps of batch C's checks (places): a saved game the port writes
at a place, then the same keys in the original (DOSBox-X) and in eclplay
from the title, with screenshots compared to eclplay's frames.

Not a check itself; the c_*.py scripts beside it import it.
"""
import os
import pathlib
import struct
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

SAVE_A = cokbox.ROOT / "SAVE" / "SAVGAMA.DAT"


def load_keys(letter):
    """Title, Play, Load Saved Game LETTER, Begin Adventuring (the list keeps
    the third row selected after Load)."""
    return r"\kxP\v\v\r" + letter + r"\v\v\v\v\v\r"


def workdir(name, given=None):
    work = pathlib.Path(given or tempfile.mkdtemp(prefix=f"check-{name}-"))
    work.mkdir(parents=True, exist_ok=True)
    return work


def need_save_a():
    if not SAVE_A.exists():
        print("needs the original's saved games in SAVE/")
        sys.exit(2)


def port_save(out, keys, args, block=None, expect=None):
    """Write a saved game with eclplay (not from the title: ARGS say where,
    e.g. --load SAVE/SAVGAMA.DAT --play; BLOCK the ECL block), KEYS ending in
    camp's Save and a letter. Returns (the saves directory, stdout)."""
    text, saves, _ = cokbox.eclplay(keys, out, saves=(), shots=False, start_menu=False,
                                    args=list(map(str, args)), block=block)
    if not any(saves.glob("SAVGAM*.DAT")):
        raise SystemExit("the port wrote no saved game:\n" + text[-2000:])
    if expect and expect not in text:
        raise SystemExit(f"the port's run did not say {expect!r}:\n" + text[-2000:])
    return saves, text


# The party's records (see checks/slow_poison.py): DS:609a the first,
# +0x17f the next.
PARTY, NEXT = 0x609A, 0x17F


def party_records(box, size=0x1A0):
    """(linear address, raw bytes) of each party member's record."""
    out = []
    rec = box.far(box.ds * 16 + PARTY)
    while rec is not None and len(out) < 8:
        out.append((rec, box.read(rec, size)))
        rec = box.far(rec + NEXT)
    return out


def name_of(raw):
    return raw[1:1 + min(raw[0], 15)].decode("latin-1")


class Compare:
    """The original and eclplay, from the title, through the same keys.

    steps: (name, keys), (name, keys, probe) or (name, keys, probe, "reads"),
    the last typing its keys only where Crt.ReadKey blocks (text input; the
    menus poll KeyPressed first, so not there): after each step's keys, both
    wait for a key and the original's screen is compared with the eclplay
    frame it matches best; probe(box) is called then (memory reads). saves:
    the saved games (a directory); seed: RandSeed for both (box_seed: the
    original's, where the two take different numbers of rolls); helm: START x
    Helm / eclplay --helm; machine: tandy, ega, cga.
    """

    def __init__(self, work, saves, steps, seed=None, helm=False, machine="tandy",
                 eclargs=(), timeout=900, verbose=False, settle=1.0, retries=8, cycles=None,
                 box_seed=None):
        self.work = pathlib.Path(work)
        self.saves, self.steps, self.seed, self.helm = saves, steps, seed, helm
        self.machine, self.eclargs, self.timeout = machine, list(eclargs), timeout
        self.verbose, self.settle, self.retries = verbose, settle, retries
        self.cycles = cycles
        self.box_seed = seed if box_seed is None else box_seed
        self.results = {}
        self.shown = {}
        self.probes = {}
        self.port_text = ""

    def eclplay(self, binary=None, out="eclplay"):
        keys = "".join(s[1] for s in self.steps)
        args = list(self.eclargs)
        if self.seed is not None:
            args += ["--seed", str(self.seed)]
        if self.helm:
            args += ["--helm"]
        return cokbox.eclplay(keys, self.work / out, machine=self.machine,
                              saves=[self.saves] if self.saves else (), args=args,
                              binary=binary)

    def run(self, box_hook=None):
        """Run both; return the number of steps whose screen differs.

        A screen that differs is taken again for a few seconds (a picture
        animating while a menu waits) and the best is kept. With
        C_PATCHED_ECLPLAY set (an eclplay built with a fix), each screen is
        also compared with that one's frames, to tell a known difference
        from others."""
        self.port_text, _, self.port_shots = self.eclplay()
        expected = [cokbox.Screen.load(p) for p in self.port_shots]
        patched = os.environ.get("C_PATCHED_ECLPLAY")
        if patched:
            self.patched_text, _, pshots = self.eclplay(patched, "eclplay-patched")
            pexpected = [cokbox.Screen.load(p) for p in pshots]
        differ = 0
        with cokbox.Box(workdir=self.work / "box", machine=self.machine,
                        saves=[self.saves] if self.saves else (), seed=self.box_seed,
                        args="x Helm" if self.helm else "", timeout=self.timeout,
                        verbose=self.verbose,
                        **({"cycles": self.cycles} if self.cycles else {})) as box:
            self.box = box
            for step in self.steps:
                name, keys = step[0], step[1]
                reads = len(step) > 3 and step[3] == "reads"
                if reads:  # typed only where ReadKey waits (in a battle: not the polls)
                    for item in cokbox.parse_keys(keys):
                        box.key(item[1], reads_only=True)
                else:
                    box.keys(keys)
                box.wait_input(reads_only=reads, settle=self.settle)
                shown = box.screenshot(self.work / f"{name}.png")
                best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
                count, area = shown.diff(expected[best])
                for _ in range(self.retries if count else 0):
                    again = box.screenshot()
                    i = min(range(len(expected)), key=lambda i: again.diff(expected[i])[0])
                    if again.diff(expected[i])[0] < count:
                        shown, best = again, i
                        count, area = shown.diff(expected[best])
                        shown.save_png(self.work / f"{name}.png")
                    if not count:
                        break
                shot = self.port_shots[best].name
                self.results[name] = (count, area, shot)
                self.shown[name] = shown
                print(f"{self.machine} {name}: {count} pixels differ from eclplay's {shot}" +
                      (f" in {area}" if area else ""), flush=True)
                if patched and count:
                    pc, pa = min((shown.diff(e) for e in pexpected), key=lambda r: r[0])
                    print(f"  against the patched eclplay: {pc}" + (f" in {pa}" if pa else ""))
                if count:
                    differ += 1
                    shown.save_png(self.work / f"diff-{name}.png", highlight=expected[best])
                if len(step) > 2 and step[2]:
                    self.probes[name] = step[2](box)
            if box_hook:
                box_hook(box)
        return differ


def money(raw):
    """A record's coins: seven words from +0xeb (README, ROB): silver, copper, bronze,
    platinum, steel, gems (+0xf5) and jewelry (+0xf7)."""
    return struct.unpack_from("<7H", raw, 0xEB)


# A saved game's layout (src/party.c cok_saved_game_read): the ECL file,
# vars 0x4b00-0x4eff, 0x7c00-0x7fff and 0x7a00-0x7bff as words, then the
# party's square, facing, the wall ahead and the square's byte.
SAVED_POS = 1 + 0x800 + 0x800 + 0x400


def edit_save(saves, letter, at=None, var=None, geo=None):
    """Change saved game LETTER in SAVES: AT (x, y, dir 0-6) moves the party
    (the wall ahead and the square's byte from GEO, the map's tables), VAR
    {address: value} sets vars of 0x4b00-0x4eff and 0x7c00-0x7fff."""
    path = pathlib.Path(saves) / f"SAVGAM{letter}.DAT"
    data = bytearray(path.read_bytes())
    for addr, value in (var or {}).items():
        if 0x4B00 <= addr <= 0x4EFF:
            struct.pack_into("<H", data, 1 + 2 * (addr - 0x4B00), value)
        else:
            assert 0x7C00 <= addr <= 0x7FFF
            struct.pack_into("<H", data, 1 + 0x800 + 2 * (addr - 0x7C00), value)
    if at:
        x, y, d = at
        data[SAVED_POS:SAVED_POS + 3] = bytes([x, y, d])
        if geo is not None:
            import c_geo
            data[SAVED_POS + 3] = c_geo.wall(geo, d, x, y)
            data[SAVED_POS + 4] = c_geo.square(geo, x, y)
    path.write_bytes(bytes(data))


def throtl_save(out, letter, var=None, at=None, extra=()):
    """A saved game LETTER inside Throtl (ECL1 block 32, map GEO1 32) at
    7,15 N, the guards beaten (0x4c4d bit 2), no wandering monsters (0x4c09
    0xff) unless VAR says otherwise; AT moves the party in the file."""
    import c_geo
    sets = {0x4BF2: 0x11, 0x4BC3: 1, 0x4BC4: 3, 0x4C2D: 1, 0x4C4D: 2, 0x4C09: 0xFF}
    sets.update(var or {})
    args = ["--load", SAVE_A, "--play", *extra]
    for a, v in sets.items():
        args += ["--set", f"{a:x}={v:x}"]
    # decline the outpost, Move NE then N onto Throtl, enter, Exit Move, Encamp, Save
    saves, text = port_save(out, "nm98yeES" + letter, args, block=16)
    # The vars again in the file: a run can change them (0x4c09 came back 0).
    inside = {a: v for a, v in sets.items() if a not in (0x4BF2, 0x4BC3, 0x4BC4, 0x4C2D)}
    edit_save(saves, letter, at=at, var=inside, geo=c_geo.load(1, 32))
    return saves, text


def cok_frames(out, keys, saves=None, seed=None, helm=False, machine="tandy", timeout=300,
               patched=False):
    """Every frame build/cok presents for KEYS from the title (SDL's dummy
    driver saving each, 320x200 at --scale 1 --aspect 1:1, real delays), as
    Screens, consecutive repeats dropped. For screens the original shows
    between key reads (delays, running text), which eclplay's shots miss.
    PATCHED: the cok C_PATCHED_COK names instead, if set (built with a
    fix)."""
    import shutil
    import subprocess
    out = pathlib.Path(out)
    if out.exists():
        shutil.rmtree(out)
    (out / "frames").mkdir(parents=True)
    (out / "saves").mkdir()
    if saves:
        for f in pathlib.Path(saves).iterdir():
            shutil.copyfile(f, out / "saves" / f.name)
    cfg = out / "krynn.cfg"
    cfg.write_bytes(cokbox.krynn_cfg(cokbox.MACHINES[machine][1]).encode())
    cok = os.environ.get("C_PATCHED_COK") if patched else None
    args = [str(cok or cokbox.ROOT / "build/cok"), "--assets", str(cokbox.ROOT / "Assets"),
            "--saves", str(out / "saves"), "--config", str(cfg), "--aspect", "1:1",
            "--scale", "1", "--input", "none", "--no-sound", "--keys", keys]
    if seed is not None:
        args += ["--seed", str(seed)]
    if helm:
        args += ["--helm"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", SDL_VIDEO_DUMMY_SAVE_FRAMES="1")
    run = subprocess.run(args, cwd=out / "frames", env=env, capture_output=True, text=True,
                         timeout=timeout)
    frames = []
    for p in sorted((out / "frames").glob("SDL_window*.bmp"),
                    key=lambda p: int("".join(c for c in p.stem if c.isdigit()) or 0)):
        s = cokbox.Screen.load(p)
        if not frames or s.pixels != frames[-1].pixels:
            frames.append(s)
    return frames, run.stdout + run.stderr


def watch(box, seconds, out=None, prefix="watch"):
    """Screenshots as fast as they come for SECONDS: the distinct ones."""
    import time
    seen = []
    end = time.monotonic() + seconds
    while time.monotonic() < end and box.running:
        shot = box.screenshot()
        if not seen or shot.pixels != seen[-1].pixels:
            if out:
                shot.save_png(pathlib.Path(out) / f"{prefix}-{len(seen):02d}.png")
            seen.append(shot)
    return seen


# The 3D view's cells (3-13 across and down, see README 3D view), where the
# port's walls of a second wall set differ (two-set WALLDEF records, 69ea:1124).
VIEW = (24, 24, 88, 88)


def masked_diff(a, b, mask=None):
    """Pixels of screens A and B that differ outside MASK (x, y, w, h)."""
    n = 0
    for i, (p, q) in enumerate(zip(a.pixels, b.pixels)):
        if p != q:
            x, y = i % a.width, i // a.width
            if mask and mask[0] <= x < mask[0] + mask[2] and mask[1] <= y < mask[1] + mask[3]:
                continue
            n += 1
    return n


def match_frames(shots, frames, mask=None):
    """For each screenshot, the fewest pixels it differs from any frame
    (outside MASK), or from two frames in a row taken together: a screen
    caught while the game drew the next (a line half printed) has each pixel
    of one or the other."""
    def between(s, a, b):
        n = 0
        for i, (p, q, r) in enumerate(zip(s.pixels, a.pixels, b.pixels)):
            if p != q and p != r:
                if mask:
                    x, y = i % s.width, i // s.width
                    mx, my, mw, mh = mask
                    if mx <= x < mx + mw and my <= y < my + mh:
                        continue
                n += 1
        return n
    out = []
    for s in shots:
        diffs = [masked_diff(s, f, mask) for f in frames]
        best = min(diffs)
        # The pairs around the nearest frames (the screen drawn from one to
        # the next).
        for i in sorted(range(len(frames)), key=diffs.__getitem__)[:3]:
            for j in (i - 1, i):
                if best and 0 <= j < len(frames) - 1:
                    best = min(best, between(s, frames[j], frames[j + 1]))
        out.append(best)
    return out


def var(box, addr):
    """Game var ADDR (0x4b00-0x4eff): a word in the block DS:4b28 points to
    (2fd3's operands read var 0x4bXX at its +2 * XX)."""
    return box.word(box.far(box.ds * 16 + 0x4B28) + 2 * (addr - 0x4B00))
