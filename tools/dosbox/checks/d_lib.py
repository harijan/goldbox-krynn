"""Shared helpers for batch D's combat checks (tools/dosbox/checks/d_*.py).

Each check plays the same keys from the title in eclplay (--start-menu
--seed N, SAVE/'s games) and in the original under DOSBox-X (Box(seed=N)),
so that the dice agree as long as the port follows the original; it then
compares the screens (eclplay's frame that matches best), the combat log
eclplay prints and the original's memory: the combatants (DS:623f, 68d1),
the map (DS:6a2e) and the records.
"""
import os
import pathlib
import struct
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

os.environ.setdefault("COKBOX_DISPLAYS", "140-149")

SAVES = cokbox.ROOT / "SAVE"
# The title, Play, Load Saved Game A, Begin Adventuring (as load_game_screen).
LOAD_A = r"\kxP\v\v\rA\v\v\v\v\v\r"
# Game A's start: Solace's pages to the outpost's menu.
A_PAGES = r"\r\r\r\r\r\r\r"
# Leave the outpost, decline it, Move, north-east: the caravan's draconians
# (4 baaz on open ground), "PRESS ENTER" then the battle.
TO_CARAVAN = A_PAGES + r"Lnm9\r"
# Throtl's gate: as the README's recipe from the first outpost, the caravan
# met (it is game A's first step), enter Throtl at 7,15 N; ATTACK.
DS_COMBATANTS, DS_RECORDS, DS_COUNT = 0x623F, 0x68D1, 0x6242
DS_MAP = 0x6A2E


def workdir(name, given=None):
    return pathlib.Path(given or tempfile.mkdtemp(prefix=f"check-{name}-"))


def need_saves():
    if not (SAVES / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        sys.exit(2)


def port(keys, out, seed, args=(), machine="tandy", shots=True, saves=(), pauses=False):
    """eclplay --start-menu with SAVE/'s games (and SAVES, a list of more
    files or directories) and seed: (stdout lines, shots). With PAUSES, a
    build of eclplay that also saves a shot at each of the game's delays
    (pause_player), so that texts shown during a pause have a frame."""
    extra = ["--seed", str(seed), *map(str, args)]
    out = pathlib.Path(out)
    stdout, _, shots_ = cokbox.eclplay(keys, out, machine=machine,
                                       saves=[SAVES, *saves], args=extra, shots=shots,
                                       binary=pause_player() if pauses else None)
    (out / "stdout.txt").write_text(stdout)
    return stdout.splitlines(), sorted(shots_, key=lambda p: int(p.stem))


class Box(cokbox.Box):
    """cokbox.Box, started again (up to three times) when DOSBox-X shows no
    window: under a heavy load its SDL now and then falls back to its
    offscreen driver."""

    def start(self):
        for attempt in range(3):
            try:
                return super().start()
            except cokbox.BoxError as e:
                if "window" not in str(e) or attempt == 2:
                    raise
                self.log(f"{e}: starting again")
                self.close()


def pause_player():
    """build/eclplay-pauses: eclplay built from a copy of src/eclplay.c whose
    delay hook (Crt.Delay's) also saves a --shots frame; src/ is not
    changed. Rebuilt when a source is newer."""
    import re
    import subprocess
    root = cokbox.ROOT
    out = root / "build" / "eclplay-pauses"
    make = (root / "Makefile").read_text()
    sources = ["src/keyscript.c"] + re.search(r"^ADVENTURE_SRC = (.*)$", make, re.M).group(1).split()
    newest = max((root / f).stat().st_mtime for f in sources + ["src/eclplay.c"])
    if out.exists() and out.stat().st_mtime > newest:
        return out
    code = (root / "src" / "eclplay.c").read_text()
    hook = ("static void delay(cok_adventure *game, unsigned ms, void *context)\n{\n"
            "    (void)game;\n    player *p = context;\n")
    if hook not in code:
        raise SystemExit("src/eclplay.c's delay hook changed: update d_lib.pause_player")
    code = code.replace(hook, hook + (
        "    if (p->shots != NULL && p->game != NULL && ms > 0) {\n"
        "        char path[4096];\n"
        "        snprintf(path, sizeof path, \"%s/%03u.bmp\", p->shots, p->shot++);\n"
        "        save(p, path);\n    }\n"))
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="eclplay-pauses-"))
    (tmp / "eclplay_pauses.c").write_text(code)
    subprocess.run(["cc", f"-I{root / 'src'}", "-std=c17", "-O2", str(tmp / "eclplay_pauses.c"),
                    *[str(root / f) for f in sources], "-o", str(out)], check=True)
    return out


def box(work, seed, machine="tandy", verbose=False, saves=(), **kw):
    kw.setdefault("timeout", 900)
    return Box(workdir=pathlib.Path(work) / "box", machine=machine,
                      saves=[SAVES, *saves], seed=seed, verbose=verbose, **kw)


# A port-written game saved in the outpost inn's camp: loading it from the
# start menu and Begin Adventuring resumes at the outpost's menu.
TO_INN_CAMP = A_PAGES + r"I\r"


def load(letter):
    """The title, Play, Load Saved Game LETTER, Begin Adventuring."""
    return rf"\kxP\v\v\r{letter}\v\v\v\v\v\r"


def make_save(letter, out, args=(), keys=TO_INN_CAMP, block=None):
    """Game A as eclplay loads it with ARGS (--effect, --set) and BLOCK,
    saved as game LETTER in the camp KEYS reach (by default the outpost
    inn's); returns the directory of its files."""
    out = pathlib.Path(out)
    stdout, saves, _ = cokbox.eclplay(keys + "S" + letter, out, shots=False,
                                      start_menu=False, block=block,
                                      args=["--load", SAVES / "SAVGAMA.DAT", "--play", *args])
    if not (saves / f"SAVGAM{letter}.DAT").exists():
        raise SystemExit("eclplay wrote no saved game: " + stdout[-300:])
    return saves


def patch_record(saves, letter, member, offset, data):
    """Write DATA at OFFSET in the .SAV of party member MEMBER (1 the first)
    of saved game LETTER in directory SAVES."""
    path = pathlib.Path(saves) / f"CHRDAT{letter}{member}.SAV"
    raw = bytearray(path.read_bytes())
    raw[offset:offset + len(data)] = bytes(data)
    path.write_bytes(bytes(raw))


def best(shown, shots):
    """(count, area, path) of eclplay's shot nearest SHOWN."""
    scored = [(shown.diff(load_bmp(p)), p) for p in shots]
    (count, area), path = min(scored, key=lambda t: t[0][0])
    return count, area, path


def frames_after(shots, shown, count=200):
    """{pixels: path} of the COUNT shots after the last one equal to SHOWN
    (a screen the original waited at), for exact matches of what follows."""
    loaded = None
    for i in range(len(shots) - 1, -1, -1):
        if load_bmp(shots[i]).pixels == shown.pixels:
            loaded = i
            break
    out = {}
    for path in shots[(loaded or 0):(loaded or 0) + count]:
        out.setdefault(load_bmp(path).pixels, path)
    return out


def between_frames(screen, frames):
    """The name of the first of two frames in a row of FRAMES (frames_after's
    {pixels: path}, in order) that SCREEN lies between, each of its pixels one
    of theirs (caught while the game drew the second), or None."""
    pixels = list(frames)
    for a, b in zip(pixels, pixels[1:]):
        if all(p == q or p == r for p, q, r in zip(screen.pixels, a, b)):
            return frames[a].name + "~"
    return None


def compare(shown, shots, name, work):
    count, area, path = best(shown, shots)
    print(f"  {name}: {count} pixels differ from eclplay's {path.name}"
          + (f" in {area}" if area else ""))
    if count:
        shown.save_png(pathlib.Path(work) / f"diff-{name}.png",
                       highlight=cokbox.Screen.load(path))
    return count


def pstr(raw):
    return raw[1:1 + min(raw[0], len(raw) - 1)].decode("latin-1").rstrip("\0")


def combatants(b):
    """The original's combatants: list of dicts n, name, x, y, size, record
    address, hp (+0x197), status (+0x188)."""
    count = b.ds_byte(DS_COUNT)
    out = []
    for n in range(1, min(count, 64)):
        x, y, nn, size = b.ds_read(DS_COMBATANTS + 4 * n, 4)
        rec = b.far(b.ds * 16 + DS_RECORDS + 4 * n)
        if rec is None:
            continue
        raw = b.read(rec, 0x1a0)
        out.append(dict(n=n, name=pstr(raw[:16]), x=x, y=y, size=size, rec=rec,
                        hp=raw[0x197], status=raw[0x188], raw=raw))
    return out


def battle_map(b):
    """The battle's terrain, 25 rows of 50 bytes (DS:6a2e's map + 7)."""
    m = b.far(b.ds * 16 + DS_MAP)
    data = b.read(m + 7, 50 * 25)
    return [data[r * 50:(r + 1) * 50] for r in range(25)]


def port_map(path):
    """eclplay --combat-map's text, as written."""
    return pathlib.Path(path).read_text()


def word(raw, off):
    return struct.unpack_from("<H", raw, off)[0]


def run_steps(b, steps, shots, work, settle=1.0):
    """Type each step's keys in the original, then screenshot the screen it
    waits at and compare it with eclplay's nearest frame; returns {name:
    (count, Screen)}."""
    out = {}
    for name, keys in steps:
        b.keys(keys)
        b.wait_input(settle=settle)
        shown = b.screenshot(pathlib.Path(work) / f"{name}.png")
        out[name] = (compare(shown, shots, name, work), shown)
    return out


def verdict(ok, what):
    print(("AGREES: " if ok else "PORT DIFFERS: ") + what)
    return 0 if ok else 1


def combat_record(b, rec):
    """The 22-byte combat record of the record at linear REC (+0x183)."""
    p = b.far(rec + 0x183)
    return b.read(p, 22) if p is not None else None


def turn_entry(b):
    """The turn order's entry whose turn it is (DS:72ce) and the order's
    records' names (DS:71ae + 4(i - 1))."""
    idx = b.ds_byte(0x72CE)
    names = []
    for i in range(1, 73):
        rec = b.far(b.ds * 16 + 0x71AE + 4 * (i - 1))
        if rec is None:
            break
        names.append(pstr(b.read(rec, 16)))
    return idx, names


def by_name(cs, name):
    return [c for c in cs if c["name"] == name]


# ---------------------------------------------------------------- reading text

def dax_record(path, record_id):
    """A DAX file's record, decoded (src/dax.c): the directory of 9-byte
    entries (id, offset, decoded size, packed size) and the run-length code."""
    data = pathlib.Path(path).read_bytes()
    count = struct.unpack_from("<H", data, 0)[0] // 9
    for i in range(count):
        rid, offset, size, packed = struct.unpack_from("<BIHH", data, 2 + 9 * i)
        if rid != record_id:
            continue
        src = data[2 + 9 * count + offset:2 + 9 * count + offset + packed]
        out = bytearray()
        j = 0
        while j < len(src):
            c = src[j]
            j += 1
            if c in (127, 128):
                continue
            if c < 127:
                out += src[j:j + c + 1]
                j += c + 1
            else:
                out += bytes([src[j]]) * (256 - c)
                j += 1
        return bytes(out[:size])
    raise KeyError(record_id)


_FONT = None


def font():
    """The game's 8x8 font (8X8D1.DAX record 201), 64 glyphs of 8 bytes: a
    character's glyph is its code % 64, lower case folded to upper."""
    global _FONT
    if _FONT is None:
        _FONT = dax_record(cokbox.ROOT / "Assets" / "8X8D1.DAX", 201)[:512]
    return _FONT


def read_cell(shown, col, row):
    """The character drawn in text cell COL, ROW of a 320x200 Screen, and its
    colours (fg, bg): ('?', None) when no glyph fits."""
    pix = [shown.at(col * 8 + i, row * 8 + j) for j in range(8) for i in range(8)]
    f = font()
    for g in range(64):
        on, off = set(), set()
        for j in range(8):
            bits = f[g * 8 + j]
            for i in range(8):
                (on if bits >> (7 - i) & 1 else off).add(pix[j * 8 + i])
        if on and off and len(on) == 1 and len(off) == 1 and on != off:
            return chr(g + 64 if g < 32 else g), (on.pop(), off.pop())
    if len(set(pix)) == 1:
        return " ", (None, pix[0])
    return "?", None


def read_text(shown, row, col0=0, col1=40):
    """Row ROW's characters from column COL0 to COL1 (exclusive), and the
    foreground colours of the non-blank ones."""
    chars, colours = [], set()
    for c in range(col0, col1):
        ch, cols = read_cell(shown, c, row)
        chars.append(ch)
        if ch not in " ?" and cols:
            colours.add(cols[0])
    return "".join(chars), colours


def screen_text(shown, rows=range(25), col0=0, col1=40):
    """{row: text} of the rows that read as more than blanks."""
    out = {}
    for r in rows:
        t, _ = read_text(shown, r, col0, col1)
        if t.strip(" ?"):
            out[r] = t.rstrip()
    return out


def burst(b, seconds, until=None, work=None, prefix="b"):
    """DOSBox-X's screenshots (F12+P) taken as fast as it writes them for
    SECONDS (or until UNTIL() is true), decoded only afterwards; the distinct
    screens in order (saved as PREFIX-NN.png in WORK)."""
    import shutil
    import time
    pngs = []
    end = time.monotonic() + seconds
    while time.monotonic() < end and not (until and until()):
        before = set(b.captures.glob("*.png"))
        b._hotkey("p")
        new = b._until(lambda: (set(b.captures.glob("*.png")) - before) or None, 10,
                       "the screenshot")
        png = sorted(new)[-1]
        b._until(lambda: cokbox._complete_png(png), 30, "the screenshot to be written")
        pngs.append(png)
    seen = []
    for png in pngs:
        shot = cokbox.Screen.load(png)
        if not seen or shot.pixels != seen[-1].pixels:
            if work is not None:
                shutil.copyfile(png, pathlib.Path(work) / f"{prefix}-{len(seen):02d}.png")
            seen.append(shot)
    print(f"  {len(pngs)} screenshots in the burst, {len(seen)} different")
    return seen


def watch(b, until, seconds=60, work=None, prefix="w"):
    """Screenshots as fast as they come until UNTIL() is true (or SECONDS
    pass); the distinct ones, in order (saved as PREFIX-NN.png in WORK)."""
    import time
    seen = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        shot = b.screenshot()
        if not seen or shot.pixels != seen[-1].pixels:
            if work is not None:
                shot.save_png(pathlib.Path(work) / f"{prefix}-{len(seen):02d}.png")
            seen.append(shot)
        if until():
            break
    return seen


# ---------------------------------------------------------------- traps

def find_code(b, seg, off, size=24):
    """The linear addresses where START_FULL.EXE's code at Ghidra SEG:OFF lies
    in the guest's memory now (an overlay's wherever its buffer holds it; its
    offsets are kept, so the address less OFF is a paragraph's)."""
    import re
    probe = b._exe_bytes(seg, off, size)
    data = b.read(0, 0xA0000)
    return [m.start() for m in re.finditer(re.escape(probe), data) if (m.start() - off) % 16 == 0]


def trap(b, seg, off, until, seconds=30):
    """Make the routine at Ghidra SEG:OFF spin where it starts (EB FE, a jump
    to itself): every copy of its code found in memory is patched, again and
    again as the overlay manager loads it afresh, until UNTIL() is true (or
    SECONDS pass). The game then stops there, its screen as it was, until
    untrap. Returns the patched (address, the two bytes replaced)."""
    import time
    patched = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        # Only in the overlay buffer (Turbo Pascal's OvrHeapOrg and OvrHeapEnd,
        # DS:437a and 437e): stale copies of the code elsewhere may lie in
        # memory the game has since allocated and reads uninitialized.
        lo, hi = b.ds_word(0x437A) * 16, b.ds_word(0x437E) * 16
        for addr in find_code(b, seg, off):
            if not lo <= addr < hi:
                continue
            patched.append((addr, b.read(addr, 2)))
            b.write(addr, b"\xeb\xfe")
        if patched and until():
            break
        time.sleep(0.002)
    return patched


def untrap(b, handle):
    for addr, orig in handle:
        if b.read(addr, 2) == b"\xeb\xfe":
            b.write(addr, orig)


def stable(b, seconds=20, interval=0.3):
    """Screenshots until two running are the same; that screen."""
    import time
    last = b.screenshot()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        time.sleep(interval)
        shot = b.screenshot()
        if shot.pixels == last.pixels:
            return shot
        last = shot
    return last


_RGB = {bytes((b, g, r)): i for i, (r, g, b) in enumerate(cokbox.PALETTE)}


def load_bmp(path):
    """eclplay's 320x200 24-bit BMP as a Screen, faster than Screen.load
    (exact palette colours; others go through cokbox.nearest)."""
    data = pathlib.Path(path).read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    if (width, abs(height)) != (320, 200):
        return cokbox.Screen.load(path)
    stride = (width * 3 + 3) & ~3
    out = bytearray(64000)
    k = 0
    for y in range(200):
        src = offset + ((199 - y) if height > 0 else y) * stride
        row = data[src:src + 960]
        for x in range(0, 960, 3):
            px = row[x:x + 3]
            v = _RGB.get(px)
            if v is None:
                v = cokbox.nearest((px[2], px[1], px[0]))
            out[k] = v
            k += 1
    return cokbox.Screen(320, 200, out)


def effects(b, rec):
    """The record's spell effects: (id, minutes, value) along +0xe3's list."""
    out = []
    eff = b.far(rec + 0xE3)
    while eff is not None and len(out) < 30:
        e = b.read(eff, 9)
        out.append((e[0], struct.unpack_from("<H", e, 1)[0], e[3]))
        eff = b.far(eff + 5)
    return out
