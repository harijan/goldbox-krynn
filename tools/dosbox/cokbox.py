#!/usr/bin/env python3
"""Run the original Champions of Krynn in DOSBox-X, unattended (stdlib only).

The game (Assets/: START.EXE, GAME.OVR, the DAX files) is copied into a
scratch C: drive as C:\\COK, with a KRYNN.CFG written for the run and,
optionally, saved games in C:\\COK\\SAVE, and run in DOSBox-X under its own
Xvfb server. The host then:

- reads and writes the guest's memory through /proc/PID/mem (the guest's
  RAM is one block of DOSBox-X's heap, found by the interrupt table and the
  BIOS data area);
- knows when the game waits for a key: HOOK.COM (hook.asm), loaded before
  the game, counts INT 16h's reads and polls in a block the host finds by
  its signature;
- types keys by putting them in the BIOS's keyboard buffer (0040:001E) as
  the game waits, or, for keys the BIOS must see (Ctrl-Break), presses them
  with xdotool;
- takes screenshots with DOSBox-X's own (F12+P, pressed with xdotool),
  read back as 320x200 colour indices (0-15) for comparison with the
  port's `eclplay --shots` BMPs;
- finds the game's data segment by the first bytes of START.EXE's (DS is
  the PSP + 0x0bd6, START_FULL.EXE's 1bc6).

Use it as a library (see `Box`) or from the command line (`cokbox.py run`,
`cokbox.py selftest`); the README's "DOSBox-X harness" says how.
"""
import argparse
import atexit
import os
import pathlib
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parent))
import ovrmerge  # noqa: E402  (tools/ovrmerge.py: EXEPACK unpacking)

# HOOK.COM, assembled from hook.asm with `nasm -f bin`.
HOOK_COM = bytes.fromhex(
    "e9ce0000000000000000000000000000434f4b484f4f4b31000000000000000000000000"
    "000000000000000000000000000000000000000080fc00741480fc10740f80fc01744c80"
    "fc1174472eff2e24012e8c1e1e012e8c162a012e89262c012e892e2e015589e5ff76022e"
    "8f062001ff76042e8f0622015d2ec706180101009c2eff1e24012ec706180100002eff06"
    "1a01cf2e8c1e1e012eff0628019c2eff1e240175282eff061c015589e5ff76082e8f0630"
    "01ff760a2e8f063201ff76002e8f0636012e8c1634015d39c0fbca020031dbbe8100ac3c"
    "2074fb3c3072213c39760e0c203c6172173c6677132c57eb022c30d1e3d1e3d1e3d1e308"
    "c3acebdb53b81635cd21891e24018c062601ba3801b81625cd21ba2602b409cd215a83c2"
    "1eb80031cd21434f4b484f4f4b20696e7374616c6c65640d0a24")
HOOK_SIGNATURE = b"COKHOOK1"
# Offsets in the hook's block, from its signature (hook.asm).
HOOK_FIELDS = dict(waiting=8, reads=10, polls=12, lastds=14, retip=16, retcs=18,
                   calls=24, ss=26, sp=28, bp=30, pollip=32, pollcs=34, pollss=36, pollbp=38)

DS_GHIDRA = 0x1BC6          # START_FULL.EXE's DS at load segment 0x1000
DS_FROM_PSP = 0x0BD6        # DS = PSP + 0x0bd6
RANDSEED = 0x43AE           # Turbo Pascal's RandSeed, DS:43ae (src/ecl.h)

# The 16 colours of the Tandy and EGA modes (src/image.c), CGA's being four of them.
PALETTE = [
    (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
    (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
    (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
    (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]

# KRYNN.CFG's first line for each machine (Startup and the setup).
MACHINES = {"tandy": ("tandy", "T"), "ega": ("ega", "E"), "vga": ("svga_s3", "E"),
            "cga": ("cga", "C")}


class BoxError(Exception):
    pass


# ---------------------------------------------------------------- keys

# US keyboard: a character's scan code (shifted or not).
_ROWS = [
    (0x02, "1234567890-=", "!@#$%^&*()_+"),
    (0x10, "qwertyuiop[]", "QWERTYUIOP{}"),
    (0x1E, "asdfghjkl;'`", 'ASDFGHJKL:"~'),
    (0x2B, "\\zxcvbnm,./", "|ZXCVBNM<>?"),
]
SCAN = {" ": 0x39, "\r": 0x1C, "\x1b": 0x01, "\b": 0x0E, "\t": 0x0F}
for _start, _plain, _shifted in _ROWS:
    for _i, (_p, _s) in enumerate(zip(_plain, _shifted)):
        SCAN[_p] = _start + _i
        SCAN[_s] = _start + _i
# Extended keys: name -> scan code, read as 0 then the code.
EXTENDED = {"up": 0x48, "down": 0x50, "left": 0x4B, "right": 0x4D, "home": 0x47,
            "end": 0x4F, "pgup": 0x49, "pgdn": 0x51, "ins": 0x52, "del": 0x53}
for _n in range(1, 11):
    EXTENDED[f"f{_n}"] = 0x3A + _n
KEYPAD = {"7": 0x47, "8": 0x48, "9": 0x49, "4": 0x4B, "5": 0x4C, "6": 0x4D,
          "1": 0x4F, "2": 0x50, "3": 0x51, "0": 0x52, ".": 0x53}


def parse_keys(text):
    """Turn eclplay's key escapes into a list of BIOS key words and commands.

    Each item is ("key", word) with word = scan << 8 | ascii, or a command:
    ("ahead", [words]) for `\\[...\\]` (typed ahead at once), ("pause",
    hundredths) for `\\pN.`, ("shot", name) for `\\sNAME.` (a screenshot);
    ("poll",) for `\\k`, which changes nothing (every key goes in at a poll
    of KeyPressed as at a read), and ("timeout",) for `\\t`, which Box.keys
    refuses (a timed menu runs out with a pause). eclplay's `\\wN.`,
    `\\n`, `\\aN.`, `\\m` and `\\j` are not taken.
    Escapes: \\r Enter, \\e Escape, \\b Backspace, \\^ \\v \\< \\> arrows,
    \\@X Alt and X, \\cX Ctrl and X, \\#HH extended key HH, \\dC keypad key C
    (NumLock on), \\\\ a backslash.
    """
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c != "\\":
            out.append(("key", key_word(c)))
            i += 1
            continue
        e = text[i + 1]
        i += 2
        if e in "rebt":
            if e == "t":
                out.append(("timeout",))
            else:
                out.append(("key", key_word({"r": "\r", "e": "\x1b", "b": "\b"}[e])))
        elif e in "^v<>":
            out.append(("key", EXTENDED[{"^": "up", "v": "down", "<": "left", ">": "right"}[e]] << 8))
        elif e == "@":
            out.append(("key", SCAN[text[i].lower()] << 8))
            i += 1
        elif e == "c":
            letter = text[i].upper()
            i += 1
            out.append(("key", SCAN[letter.lower()] << 8 | (ord(letter) - 0x40)))
        elif e == "#":
            out.append(("key", int(text[i:i + 2], 16) << 8))
            i += 2
        elif e == "d":
            ch = text[i]
            i += 1
            out.append(("key", KEYPAD[ch] << 8 | ord(ch)))
        elif e == "k":
            out.append(("poll",))
        elif e in "ps":
            end = text.index(".", i)
            arg = text[i:end]
            i = end + 1
            out.append(("pause", int(arg)) if e == "p" else ("shot", arg))
        elif e == "[":
            end = text.index("\\]", i)
            group = [w for kind, *rest in parse_keys(text[i:end]) if kind == "key" for w in rest]
            out.append(("ahead", group))
            i = end + 2
        elif e == "\\":
            out.append(("key", key_word("\\")))
        else:
            raise BoxError(f"unknown key escape \\{e}")
    return out


def key_word(ch):
    """The BIOS's word for a typed character: scan code << 8 | ASCII."""
    if ch not in SCAN:
        raise BoxError(f"no key types {ch!r}")
    return SCAN[ch] << 8 | ord(ch)


# ---------------------------------------------------------------- images

def read_png(path):
    """Return (width, height, rows of (r, g, b) tuples) for an 8-bit PNG."""
    data = pathlib.Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise BoxError(f"{path}: not a PNG")
    pos = 8
    idat = b""
    palette = None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace or ctype not in (2, 3, 6):
                raise BoxError(f"{path}: unsupported PNG (depth {depth}, type {ctype})")
        elif kind == b"PLTE":
            palette = [tuple(body[j:j + 3]) for j in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    bpp = {2: 3, 3: 1, 6: 4}[ctype]
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(height):
        filt = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if filt == 1:
                line[x] = (line[x] + a) & 255
            elif filt == 2:
                line[x] = (line[x] + b) & 255
            elif filt == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif filt == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[x] = (line[x] + pred) & 255
        prev = line
        if ctype == 3:
            rows.append([palette[v] for v in line])
        else:
            rows.append([tuple(line[x:x + 3]) for x in range(0, stride, bpp)])
    return width, height, rows


def read_bmp(path):
    """Return (width, height, rows of (r, g, b)) for a 24-bit BMP (eclplay's)."""
    data = pathlib.Path(path).read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    bits, = struct.unpack_from("<H", data, 28)
    if bits != 24:
        raise BoxError(f"{path}: not a 24-bit BMP")
    stride = (width * 3 + 3) & ~3
    rows = []
    for y in range(abs(height)):
        src = (abs(height) - 1 - y) if height > 0 else y
        line = data[offset + src * stride:offset + src * stride + width * 3]
        rows.append([(line[x + 2], line[x + 1], line[x]) for x in range(0, width * 3, 3)])
    return width, abs(height), rows


def nearest(rgb):
    """The index of the palette colour nearest RGB."""
    return min(range(16), key=lambda i: sum((a - b) ** 2 for a, b in zip(PALETTE[i], rgb)))


class Screen:
    """A screen as colour indices: width, height, and bytes, one per pixel."""

    def __init__(self, width, height, pixels):
        self.width, self.height, self.pixels = width, height, bytes(pixels)

    @classmethod
    def from_rows(cls, width, height, rows, size=None):
        """From RGB rows, scaled down by whole factors to SIZE (default 320x200
        for graphics, 640x200 for an 80-column text screen)."""
        if size is None:
            size = (640, 200) if width in (640, 720) and height in (200, 400) and \
                _looks_textual(rows) else (320, 200)
        fx, fy = width // size[0], height // size[1]
        if fx < 1 or fy < 1 or width % size[0] or height % size[1]:
            size = (width, height)
            fx = fy = 1
        cache = {}
        pixels = bytearray()
        for y in range(size[1]):
            row = rows[y * fy]
            for x in range(size[0]):
                rgb = row[x * fx]
                if rgb not in cache:
                    cache[rgb] = nearest(rgb)
                pixels.append(cache[rgb])
        return cls(size[0], size[1], pixels)

    @classmethod
    def load(cls, path, size=None):
        path = pathlib.Path(path)
        width, height, rows = (read_png if path.suffix.lower() == ".png" else read_bmp)(path)
        return cls.from_rows(width, height, rows, size)

    def at(self, x, y):
        return self.pixels[y * self.width + x]

    def region(self, x, y, w, h):
        return b"".join(self.pixels[(y + j) * self.width + x:(y + j) * self.width + x + w]
                        for j in range(h))

    def diff(self, other):
        """Pixels that differ: (count, (x0, y0, x1, y1) bounding box or None)."""
        if (self.width, self.height) != (other.width, other.height):
            raise BoxError(f"sizes differ: {self.width}x{self.height} and {other.width}x{other.height}")
        count = 0
        box = None
        for i, (a, b) in enumerate(zip(self.pixels, other.pixels)):
            if a != b:
                count += 1
                x, y = i % self.width, i // self.width
                box = (x, y, x, y) if box is None else (min(box[0], x), min(box[1], y),
                                                       max(box[2], x), max(box[3], y))
        return count, box

    def save_bmp(self, path, highlight=None):
        """Write a 24-bit BMP; HIGHLIGHT (another Screen) marks differing pixels
        in the other's colour on a grey copy of this one."""
        stride = (self.width * 3 + 3) & ~3
        body = bytearray()
        for y in range(self.height - 1, -1, -1):
            line = bytearray()
            for x in range(self.width):
                v = self.at(x, y)
                if highlight is not None:
                    w = highlight.at(x, y)
                    rgb = PALETTE[w] if w != v else tuple(c // 3 for c in PALETTE[v])
                else:
                    rgb = PALETTE[v]
                line += bytes((rgb[2], rgb[1], rgb[0]))
            body += line + bytes(stride - len(line))
        header = b"BM" + struct.pack("<IHHI", 54 + len(body), 0, 0, 54) + struct.pack(
            "<IiiHHIIiiII", 40, self.width, self.height, 1, 24, 0, len(body), 0, 0, 0, 0)
        pathlib.Path(path).write_bytes(header + body)

    def save_png(self, path, highlight=None):
        """Write an 8-bit palette PNG; HIGHLIGHT as for save_bmp."""
        colours = list(PALETTE) + [tuple(c // 3 for c in rgb) for rgb in PALETTE]
        raw = bytearray()
        for y in range(self.height):
            raw.append(0)
            for x in range(self.width):
                v = self.at(x, y)
                if highlight is not None:
                    w = highlight.at(x, y)
                    v = w if w != v else v + 16
                raw.append(v)

        def chunk(kind, body):
            return struct.pack(">I", len(body)) + kind + body + struct.pack(
                ">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
        png = (b"\x89PNG\r\n\x1a\n" +
               chunk(b"IHDR", struct.pack(">IIBBBBB", self.width, self.height, 8, 3, 0, 0, 0)) +
               chunk(b"PLTE", b"".join(bytes(c) for c in colours)) +
               chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
        pathlib.Path(path).write_bytes(png)


def _looks_textual(rows):
    """True for a 640-wide screen drawn at 640 (no doubled pixels: text mode)."""
    width = len(rows[0])
    for row in rows[::7]:
        for x in range(0, width - 1, 2):
            if row[x] != row[x + 1]:
                return True
    return False


# ---------------------------------------------------------------- the box

def krynn_cfg(display, sound="S", input="N"):
    """A KRYNN.CFG: the display's, sound's and input's letters, the save path
    C:\\COK\\SAVE\\, F and 14 zeros, CR LF lines (as Assets/KRYNN.CFG)."""
    lines = [display, sound, input, "C:\\COK\\SAVE\\", "F"] + ["0"] * 14
    return "".join(line + "\r\n" for line in lines)


def _game_ds_signature(assets):
    """The first 64 bytes of START.EXE's data segment, as loaded."""
    image = ovrmerge.unexepack((pathlib.Path(assets) / "START.EXE").read_bytes())[0]
    start = (DS_GHIDRA - 0x1000) * 16
    return bytes(image[start:start + 64])


class Box:
    """One run of the original game in DOSBox-X.

    machine: tandy, ega, vga (svga_s3) or cga; KRYNN.CFG's first line follows
    it unless display gives the letter. sound: KRYNN.CFG's second line (S
    silent by default; T the Tandy's, P the speaker); input its third (N).
    cfg: KRYNN.CFG's whole text instead (None writes one from the above;
    False writes none, for the setup). saves: files or directories copied
    into C:\\COK\\SAVE. args: START.EXE's arguments (e.g. "Woof").
    cycles: DOSBox-X's (a fixed count by default: with `max` Turbo Pascal's
    Delay calibration may overflow). turbo: run as fast as the host can
    (emulated time keeps pace with the CPU, so delays shrink too).
    hook_pad: paragraphs HOOK.COM keeps besides its own, to move the game's
    PSP; want_psp: the game's PSP wanted (at or above where it loads with no
    pad), found by starting again once with the pad it needs. seed: Turbo
    Pascal's RandSeed, set as the game first waits for a key (the title's
    first picture), after startup's Randomize. wav: a path to record the
    sound to (F12+W), from start to close.
    timeout: seconds before the run is killed, whatever it is doing.
    pre_commands: DOS commands the autoexec runs in C:\\COK before START.EXE
    (e.g. "ATTRIB +H KRYNN.CFG"); files: {name: bytes} written into C:\\COK.
    mapper: a mapper file's text (e.g. `jaxis_0_0- "key 4"` binds the A key's
    SDL scancode to stick 0 left), used instead of the default mappings.
    workdir: where the C: drive, captures and logs go (default: a temporary
    directory, removed on close unless keep).
    """

    def __init__(self, workdir=None, assets=None, machine="tandy", display=None, sound="S",
                 input="N", cfg=None, saves=(), args="", cycles="fixed 20000", turbo=False,
                 hook_pad=0, memsize=None, wav=None, timeout=600, extra_conf=None,
                 exit_after=True, seed=None, want_psp=None, keep=False, verbose=False,
                 pre_commands=(), files=None, mapper=None):
        self.assets = pathlib.Path(assets or ROOT / "Assets")
        if machine not in MACHINES:
            raise BoxError(f"machine {machine}: one of {', '.join(MACHINES)}")
        self.machine = machine
        self.display = display or MACHINES[machine][1]
        self.sound, self.input, self.cfg = sound, input, cfg
        self.saves, self.args, self.cycles, self.turbo = list(saves), args, cycles, turbo
        self.hook_pad, self.memsize, self.wav = hook_pad, memsize, wav
        self.timeout, self.extra_conf, self.exit_after = timeout, extra_conf or {}, exit_after
        self.verbose, self.seed, self.want_psp = verbose, seed, want_psp
        self.pre_commands = list(pre_commands)
        self.files = dict(files or {})
        self.mapper = mapper
        self._own_workdir = workdir is None and not keep
        self.workdir = pathlib.Path(workdir).resolve() if workdir else None
        self.xvfb = self.dosbox = None
        self.display_name = None
        self.window = None
        self._mem = None
        self.base = None
        self.hook_addr = None
        self._ds = None
        self._deadline = None
        self._shots = 0
        self._ds_sig = _game_ds_signature(self.assets)
        self._last_polls = 0
        self._streak = 0

    # -------------------------------------------------- setup

    def log(self, *what):
        if self.verbose:
            print("cokbox:", *what, file=sys.stderr, flush=True)

    def krynn_cfg(self):
        return self.cfg if self.cfg is not None else krynn_cfg(self.display, self.sound,
                                                                 self.input)

    def prepare(self):
        """Fill the C: drive and write dosbox.conf."""
        if self.workdir is None:
            self.workdir = pathlib.Path(tempfile.mkdtemp(prefix="cokbox-"))
        self.workdir.mkdir(parents=True, exist_ok=True)
        self.cdrive = self.workdir / "c"
        self.game = self.cdrive / "COK"
        self.captures = self.workdir / "capture"
        if self.game.exists():
            shutil.rmtree(self.game)
        (self.game / "SAVE").mkdir(parents=True)
        self.captures.mkdir(exist_ok=True)
        for old in self.captures.glob("*.png"):  # DOSBox-X numbers them up to 999 only
            old.unlink()
        for f in self.assets.iterdir():
            if f.is_file() and f.name.upper() != "KRYNN.CFG":
                shutil.copyfile(f, self.game / f.name.upper())
        if self.cfg is not False:
            data = self.krynn_cfg()
            (self.game / "KRYNN.CFG").write_bytes(data.encode("latin-1") if isinstance(data, str) else data)
        for s in self.saves:
            s = pathlib.Path(s)
            for f in ([s] if s.is_file() else sorted(s.iterdir())):
                if f.is_file():
                    shutil.copyfile(f, self.game / "SAVE" / f.name.upper())
        (self.cdrive / "HOOK.COM").write_bytes(HOOK_COM)
        for name, data in self.files.items():
            (self.game / name).write_bytes(data)
        conf = {
            "sdl": {"output": "surface", "autolock": "false", "showmenu": "false",
                    "mapperfile": str(self.workdir / "mapper.map")},
            "dosbox": {"machine": MACHINES[self.machine][0], "captures": str(self.captures),
                       "quit warning": "false", "fastbioslogo": "true",
                       "startbanner": "false"},
            "render": {"frameskip": "0"},
            "cpu": {"core": "normal", "cputype": "auto", "cycles": self.cycles,
                    "turbo": "true" if self.turbo else "false"},
            "mixer": {"nosound": "false" if self.wav else "true"},
            "dos": {"ems": "false", "xms": "false", "umb": "false"},
        }
        if self.memsize:
            conf["dosbox"]["memsize"] = str(self.memsize)
        for key, value in self.extra_conf.items():
            section, prop = key.split(".", 1)
            conf.setdefault(section, {})[prop] = value
        text = "".join(f"[{s}]\n" + "".join(f"{k}={v}\n" for k, v in props.items())
                       for s, props in conf.items())
        pad = f" {self.hook_pad:x}" if self.hook_pad else ""
        text += ("[autoexec]\n" + f"mount c \"{self.cdrive}\"\n" + "c:\n" +
                 f"HOOK.COM{pad}\n" + "cd \\COK\n" +
                 "".join(c + "\n" for c in self.pre_commands) +
                 f"START.EXE {self.args}".rstrip() + "\n" +
                 ("exit\n" if self.exit_after else ""))
        (self.workdir / "dosbox.conf").write_text(text)
        if self.mapper is not None:
            (self.workdir / "mapper.map").write_text(self.mapper)

    def start(self):
        """Start Xvfb and DOSBox-X, and wait for the hook (and, with want_psp,
        for the game's PSP); set the seed and start recording as asked."""
        self._start_once()
        if self.want_psp is not None and self.psp != self.want_psp:
            pad = self.hook_pad + self.want_psp - self.psp
            if pad < 0:
                raise BoxError(f"the game loads at {self.psp:04x}, above {self.want_psp:04x}")
            self.log(f"PSP {self.psp:04x}: starting again with HOOK.COM {pad:x}")
            self.close()
            self.hook_pad, self._ds = pad, None
            self._start_once()
            if self.psp != self.want_psp:
                raise BoxError(f"the game loads at {self.psp:04x}, not {self.want_psp:04x}")
        if self.seed is not None:
            # At the game's first wait for a key (the title's, or the setup's):
            # after startup's Randomize (3e99:0080), with the game idle.
            self.wait_input()
            self.set_seed(self.seed)
        if self.wav:
            self.record_sound()
        return self

    def _start_once(self):
        self.prepare()
        self._deadline = time.monotonic() + self.timeout
        self.display_name = self._start_xvfb()
        env = dict(os.environ, DISPLAY=self.display_name, SDL_AUDIODRIVER="dummy")
        env.pop("WAYLAND_DISPLAY", None)
        self._log_file = open(self.workdir / "dosbox.log", "wb")
        self.dosbox = subprocess.Popen(
            ["dosbox-x", *([] if self.mapper is not None else ["-defaultmapper"]),
             "-nopromptfolder", "-fastlaunch", "-nomenu",
             "-conf", str(self.workdir / "dosbox.conf"), "-time-limit", str(int(self.timeout))],
            cwd=self.workdir, env=env, stdin=subprocess.DEVNULL, stdout=self._log_file,
            stderr=subprocess.STDOUT, start_new_session=True)
        _RUNNING.append(self)
        self.window = self._until(self._find_window, 90, "DOSBox-X's window")
        self._xdo("windowfocus", "--sync", self.window)
        self.base = self._until(self._find_ram, 90, "the guest's memory")
        self.hook_addr = self._until(self._find_hook, 90, "HOOK.COM")
        self.log(f"display {self.display_name}, ram at {self.base:#x}, hook at {self.hook_addr:#x}")
        if self.want_psp is not None:
            self.ds  # noqa: B018 (finds it)

    def _start_xvfb(self):
        """Start Xvfb; return its display name. COKBOX_DISPLAYS (e.g. "110-119")
        limits it to those display numbers, the first free one taken (for runs
        side by side with others'); otherwise Xvfb picks one (-displayfd)."""
        args = ["-screen", "0", "1024x768x24", "-nolisten", "tcp"]
        wanted = os.environ.get("COKBOX_DISPLAYS")
        if wanted:
            lo, _, hi = wanted.partition("-")
            for n in range(int(lo), int(hi or lo) + 1):
                if _display_taken(n):
                    continue
                self.xvfb = subprocess.Popen(
                    ["Xvfb", f":{n}", *args], stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL, start_new_session=True)
                end = time.monotonic() + 20
                while self.xvfb.poll() is None and time.monotonic() < end:
                    # Ours once its lock file names it (another run may have
                    # taken the number at the same moment).
                    try:
                        owner = int(pathlib.Path(f"/tmp/.X{n}-lock").read_text().strip())
                    except (OSError, ValueError):
                        owner = None
                    if owner == self.xvfb.pid and os.path.exists(f"/tmp/.X11-unix/X{n}"):
                        return f":{n}"
                    time.sleep(0.05)
                if self.xvfb.poll() is None:
                    os.killpg(self.xvfb.pid, signal.SIGKILL)
                    self.xvfb.wait()
            raise BoxError(f"no free X display in {wanted}")
        rfd, wfd = os.pipe()
        self.xvfb = subprocess.Popen(
            ["Xvfb", "-displayfd", str(wfd), *args],
            pass_fds=(wfd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True)
        os.close(wfd)
        number = b""
        with os.fdopen(rfd, "rb") as r:
            while not number.endswith(b"\n"):
                chunk = r.read(1)
                if not chunk:
                    raise BoxError("Xvfb did not start")
                number += chunk
        return ":" + number.decode().strip()

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.close()

    def close(self):
        """Stop DOSBox-X and Xvfb (the WAV recording is ended first)."""
        if self.wav and self.dosbox and self.dosbox.poll() is None:
            try:
                self.record_sound()
                time.sleep(0.5)
            except BoxError:
                pass
        for proc in (self.dosbox, self.xvfb):
            if proc and proc.poll() is None:
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                    proc.wait(5)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    proc.wait()
        if self._mem:
            self._mem.close()
            self._mem = None
        if getattr(self, "_log_file", None):
            self._log_file.close()
            self._log_file = None
        if self.wav:
            waves = sorted(self.captures.glob("*.wav"))
            if waves:
                shutil.copyfile(waves[-1], self.wav)
        if self in _RUNNING:
            _RUNNING.remove(self)
        if self._own_workdir and self.workdir is not None and self.workdir.exists():
            shutil.rmtree(self.workdir, ignore_errors=True)

    @property
    def running(self):
        return self.dosbox is not None and self.dosbox.poll() is None

    def _until(self, probe, seconds, what, interval=0.05):
        end = min(time.monotonic() + seconds, self._deadline or float("inf"))
        while True:
            if self.dosbox is not None and self.dosbox.poll() is not None:
                raise BoxError(f"DOSBox-X ended (status {self.dosbox.returncode}) waiting for {what}")
            value = probe()
            if value is not None and value is not False:
                return value
            if time.monotonic() > end:
                raise BoxError(f"timed out waiting for {what}")
            time.sleep(interval)

    def _xdo(self, *args):
        env = dict(os.environ, DISPLAY=self.display_name)
        return subprocess.run(["xdotool", *map(str, args)], env=env, capture_output=True,
                              text=True, timeout=20).stdout

    def _find_window(self):
        out = self._xdo("search", "--name", "^DOSBox-X").split()
        return out[0] if out else None

    def _find_ram(self):
        pid = self.dosbox.pid
        try:
            maps = pathlib.Path(f"/proc/{pid}/maps").read_text().splitlines()
            mem = open(f"/proc/{pid}/mem", "r+b", buffering=0)
        except OSError:
            return None
        for line in maps:
            fields = line.split()
            lo, hi = (int(v, 16) for v in fields[0].split("-"))
            if not fields[1].startswith("rw") or not 0x100000 <= hi - lo <= 1 << 30 or \
                    len(fields) > 5 and fields[5].startswith("/"):
                continue
            try:
                mem.seek(lo)
                data = mem.read(hi - lo)
            except OSError:
                continue
            # The keyboard buffer's start and end, 0040:0080 and 0082, and the
            # interrupt table's vectors in the BIOS's segment.
            for m in re.finditer(b"\x1e\x00\x3e\x00", data):
                off = m.start() - 0x480
                if off < 0 or (lo + off) % 16:
                    continue
                segs = struct.unpack_from("<32H", data, off)[1::2]
                if sum(1 for s in segs if s == 0xF000) >= 8:
                    self._mem = mem
                    return lo + off
        mem.close()
        return None

    def _find_hook(self):
        data = self.read(0, 0xA0000)
        for m in re.finditer(re.escape(HOOK_SIGNATURE), data):
            if m.start() % 16 == 0:
                return m.start()
        return None

    # -------------------------------------------------- memory

    def read(self, addr, size):
        """SIZE bytes of the guest's memory from linear address ADDR."""
        self._mem.seek(self.base + addr)
        return self._mem.read(size)

    def write(self, addr, data):
        self._mem.seek(self.base + addr)
        self._mem.write(bytes(data))

    def byte(self, addr):
        return self.read(addr, 1)[0]

    def word(self, addr):
        return struct.unpack("<H", self.read(addr, 2))[0]

    def dword(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    def hook(self, field):
        return self.word(self.hook_addr + HOOK_FIELDS[field])

    def hook_state(self):
        return {name: self.hook(name) for name in HOOK_FIELDS}

    def find_ds(self):
        """The game's data segment, by the signature of its first bytes."""
        data = self.read(0, 0xA0000)
        for m in re.finditer(re.escape(self._ds_sig[:16]), data):
            if m.start() % 16 == 0 and data[m.start():m.start() + 64] == self._ds_sig:
                seg = m.start() // 16
                psp = seg - DS_FROM_PSP
                if self.read(psp * 16, 2) == b"\xcd\x20":
                    return seg
        return None

    @property
    def ds(self):
        if self._ds is None:
            self._ds = self._until(self.find_ds, 60, "the game's data segment")
            self.log(f"DS {self._ds:04x}, PSP {self._ds - DS_FROM_PSP:04x}")
        return self._ds

    @property
    def psp(self):
        return self.ds - DS_FROM_PSP

    def ds_read(self, off, size):
        return self.read(self.ds * 16 + off, size)

    def ds_write(self, off, data):
        self.write(self.ds * 16 + off, data)

    def ds_byte(self, off):
        return self.ds_read(off, 1)[0]

    def ds_word(self, off):
        return struct.unpack("<H", self.ds_read(off, 2))[0]

    def ds_dword(self, off):
        return struct.unpack("<I", self.ds_read(off, 4))[0]

    def far(self, ptr_addr):
        """The linear address a far pointer at linear PTR_ADDR holds (None if nil)."""
        off, seg = struct.unpack("<HH", self.read(ptr_addr, 4))
        return None if off == seg == 0 else seg * 16 + off

    def find_text(self, hint, row=0, col=0):
        """The host address of the 80-column text screen, found by HINT (a
        string shown at ROW, COL; the attribute bytes between its characters
        may be anything): DOSBox-X keeps video memory outside the guest's RAM
        block, so B800:0000 is not readable through read(). None if not found.
        A CGA's or Tandy's memory holds character and attribute in turn;
        DOSBox-X keeps an svga_s3's spread out, character and attribute then six
        bytes more (planes), which text_rows then reads (self.text_stride, 2, 4
        or 8)."""
        pid = self.dosbox.pid
        for stride in (2, 4, 8):
            pattern = b"".join(re.escape(bytes([ord(c)])) + b"." * (stride - 1) for c in hint)
            for line in pathlib.Path(f"/proc/{pid}/maps").read_text().splitlines():
                fields = line.split()
                lo, hi = (int(v, 16) for v in fields[0].split("-"))
                if not fields[1].startswith("rw") or hi - lo > 1 << 30 or \
                        len(fields) > 5 and fields[5].startswith("/"):
                    continue
                try:
                    self._mem.seek(lo)
                    data = self._mem.read(hi - lo)
                except OSError:
                    continue
                for m in re.finditer(pattern, data, re.DOTALL):
                    start = lo + m.start() - (row * 80 + col) * stride
                    if start >= lo and (start - self.base) % 2 == 0:
                        self.text_stride = stride
                        return start
        return None

    text_stride = 2

    def text_rows(self, address):
        """The 25 rows of the 80-column text screen at host ADDRESS (from
        find_text): (characters, attributes) each."""
        k = self.text_stride
        self._mem.seek(address)
        raw = self._mem.read(2000 * k)
        return [(raw[r * 80 * k:(r + 1) * 80 * k:k].decode("cp437"),
                 raw[r * 80 * k + 1:(r + 1) * 80 * k:k]) for r in range(25)]

    def set_seed(self, seed):
        """Set Turbo Pascal's RandSeed (DS:43ae)."""
        self.ds_write(RANDSEED, struct.pack("<I", seed & 0xFFFFFFFF))

    def to_ghidra(self, seg, off):
        """A real CS:IP in the game's root image as START_FULL.EXE's (load segment
        0x1000), or None when it lies outside (an overlay's buffer, DOS, the BIOS)."""
        load = self.psp + 0x10
        if load <= seg < load + 0x0F48:  # the root image below the overlays (ovrmerge)
            return seg - load + 0x1000, off
        return None

    def locate(self, seg, off, exe=None):
        """Where real code SEG:OFF is in START_FULL.EXE (`make merged`), as a
        Ghidra (seg, off), found by the bytes from there on (an overlay's
        code moves with its buffer); None if not found or not unique."""
        exe = pathlib.Path(exe or ROOT / "build/START_FULL.EXE")
        if not hasattr(self, "_exe"):
            self._exe = exe.read_bytes()
        data = self._exe
        hdr = struct.unpack_from("<H", data, 8)[0] * 16
        for start, size in ((0, 12), (0, 20), (-8, 24)):
            if off + start < 0:
                continue
            probe = self.read(seg * 16 + off + start, size)
            hits = [m.start() - start for m in re.finditer(re.escape(probe), data)
                    if (m.start() - start - hdr - off) % 16 == 0]
            if len(hits) == 1:
                return 0x1000 + (hits[0] - hdr - off) // 16, off
        return None

    def where(self):
        """Where the game waits for a key, as Ghidra addresses: the return
        addresses of a ReadKey waiting (Crt's, then its callers' frames), or
        the call site of the last KeyPressed that found no key, and its
        callers'; "?SEG:OFF" for a real address not found. The frames are
        read as the game runs: a hint, now and then from another call."""
        if self.hook("waiting"):
            sites = self.caller_chain()[1:]
        else:
            sites = [(self.hook("pollcs"), self.hook("pollip"))] + self.frames(
                self.hook("pollss"), self.hook("pollbp"), 3)
        out = []
        for real in sites:
            at = self.to_ghidra(*real) or self.locate(*real)
            out.append("%04x:%04x" % at if at else "?%04x:%04x" % real)
            if at and at[0] == 0x1000:  # main's loop: the outermost
                break
        return out

    def frames(self, ss, bp, depth=6):
        """Return addresses up the BP chain from SS:BP (far calls assumed)."""
        chain = []
        for _ in range(depth):
            if bp == 0:
                break
            up, ip, cs = struct.unpack("<HHH", self.read(ss * 16 + bp, 6))
            chain.append((cs, ip))
            if up <= bp:  # the outermost frame (main's BP is 0, or garbage)
                break
            bp = up
        return chain

    def caller_chain(self, depth=6):
        """The return addresses of the game's last key read: Crt's ReadKey's,
        its caller's, then the BP frames', as real (seg, off) pairs."""
        st = self.hook_state()
        sp = st["ss"] * 16 + st["sp"]
        return ([(st["retcs"], st["retip"])] +
                [struct.unpack("<HH", self.read(sp + 6, 4))[::-1]] +
                self.frames(st["ss"], st["bp"], depth))

    # -------------------------------------------------- keys

    def buffer(self):
        """The keys waiting in the BIOS's buffer, as words."""
        head, tail = self.word(0x41A), self.word(0x41C)
        start, end = self.word(0x480), self.word(0x482)
        keys = []
        while head != tail:
            keys.append(self.word(0x400 + head))
            head = head + 2 if head + 2 < end else start
        return keys

    def push_key(self, word):
        """Put one key in the BIOS's buffer (False if it is full)."""
        tail = self.word(0x41C)
        start, end = self.word(0x480), self.word(0x482)
        nxt = tail + 2 if tail + 2 < end else start
        if nxt == self.word(0x41A):
            return False
        self.write(0x400 + tail, struct.pack("<H", word))
        self.write(0x41C, struct.pack("<H", nxt))
        return True

    # Polls of KeyPressed that are not a wait: the flush (1614:045c) and the
    # key read's look before its ReadKey (1614:025b); their return addresses.
    NOT_WAITS = ((0x1614, 0x0467), (0x1614, 0x026C))

    def from_ghidra(self, seg, off):
        """START_FULL.EXE's SEG:OFF in the game's root image as a real one."""
        return seg - 0x1000 + self.psp + 0x10, off

    def waiting(self, reads_only=False):
        """True when the game waits for a key with none in the buffer: in
        ReadKey, or (unless READS_ONLY) polling KeyPressed (1614:03c2's
        "key pending", a menu's or the title's wait) in two samples running,
        not a flush."""
        if self.buffer():
            return False
        if self.hook("waiting"):
            return True
        if reads_only:
            return False
        polls = self.hook("polls")
        site = (self.hook("pollcs"), self.hook("pollip"))
        flushes = [self.from_ghidra(*a) for a in self.NOT_WAITS]
        advancing = polls != self._last_polls and site not in flushes
        self._last_polls = polls
        self._streak = self._streak + 1 if advancing else 0
        return self._streak >= 2

    def wait_input(self, reads_only=False, seconds=120, settle=0.0):
        """Wait until the game waits for a key (see waiting); then SETTLE
        seconds more, for the screen to be drawn."""
        self._last_polls = self.hook("polls")
        self._streak = 0
        self._until(lambda: self.waiting(reads_only), seconds, "the game to wait for a key", 0.03)
        if settle:
            time.sleep(settle)

    def key(self, word, reads_only=False, seconds=120):
        """Type one key (a BIOS word or a character) as the game next waits
        for one, and wait for it to be taken from the buffer."""
        if isinstance(word, str):
            word = key_word(word)
        self.wait_input(reads_only, seconds)
        self.push_key(word)
        try:
            self._until(lambda: not self.buffer(), seconds, "the game to take the key", 0.01)
        except (OSError, BoxError):
            # A key that quits to DOS: DOSBox-X may be gone (its memory with
            # it) before the buffer is seen empty.
            if self.dosbox is not None and (self.dosbox.poll() is not None or
                                            self._exiting()):
                return
            raise

    def _exiting(self):
        """DOSBox-X ends within a few seconds (after the game quit to DOS)."""
        try:
            self.dosbox.wait(5)
            return True
        except subprocess.TimeoutExpired:
            return False

    def keys(self, text, seconds=120, shots=None):
        """Type eclplay-style KEYS (see parse_keys) one at a time as the game
        waits for them, in ReadKey or polling KeyPressed (so a key ends the
        title's waits too, as `\\k` does in eclplay). Screenshots (\\sNAME.)
        are saved in SHOTS (a directory)."""
        for item in parse_keys(text):
            kind = item[0]
            if kind == "key":
                self.key(item[1], seconds=seconds)
            elif kind == "poll":
                pass
            elif kind == "ahead":
                for w in item[1]:
                    self.push_key(w)
            elif kind == "pause":
                time.sleep(item[1] / 100)
            elif kind == "timeout":
                raise BoxError("\\t: let a timed menu run out with a pause, \\pN.")
            elif kind == "shot":
                self.wait_input(seconds=seconds, settle=0.5)
                self.screenshot(pathlib.Path(shots or self.workdir) / f"{item[1]}.png")

    def press(self, *keys):
        """Press keys through X (xdotool key names, e.g. 'ctrl+Pause'), for
        what only the BIOS's keyboard handler sees."""
        self._xdo("windowfocus", "--sync", self.window)
        self._xdo("key", "--delay", "80", *keys)

    # -------------------------------------------------- screen and sound

    def _hotkey(self, key):
        self._xdo("windowfocus", "--sync", self.window)
        self._xdo("keydown", "F12", "key", key, "keyup", "F12")

    def screenshot(self, path=None, size=None):
        """Take DOSBox-X's screenshot and return it as a Screen; with PATH, keep
        the PNG there too."""
        before = set(self.captures.glob("*.png"))
        self._hotkey("p")
        new = self._until(lambda: (set(self.captures.glob("*.png")) - before) or None, 10,
                          "the screenshot")
        png = sorted(new)[-1]
        self._until(lambda: _complete_png(png), 5, "the screenshot to be written")
        self._shots += 1
        if path:
            pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(png, path)
        screen = Screen.load(png, size)
        png.unlink()  # DOSBox-X stops at start_999.png: keep the directory empty
        return screen

    def record_sound(self):
        """Start or stop DOSBox-X's WAV recording (F12+W)."""
        self._hotkey("w")

    def wait_exit(self, seconds=60):
        """Wait for DOSBox-X to end (the game quit to DOS and `exit` ran)."""
        end = time.monotonic() + seconds
        while self.running and time.monotonic() < end:
            time.sleep(0.1)
        return not self.running


def _display_taken(n):
    """Whether X display N is in use: its lock file names a live process (Xvfb
    replaces a stale one itself)."""
    try:
        pid = int(pathlib.Path(f"/tmp/.X{n}-lock").read_text().strip())
    except (OSError, ValueError):
        return os.path.exists(f"/tmp/.X{n}-lock")
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        pass
    return True


def _complete_png(path):
    try:
        return path.read_bytes().endswith(b"IEND\xaeB`\x82")
    except OSError:
        return False


_RUNNING = []


def _cleanup(*_):
    for box in list(_RUNNING):
        box.close()


atexit.register(_cleanup)
for _sig in (signal.SIGTERM, signal.SIGHUP):
    signal.signal(_sig, lambda *a: (_cleanup(), sys.exit(1)))


# ---------------------------------------------------------------- command line

def eclplay(keys, out, assets=None, machine="tandy", saves=(), args=(), shots=True,
            start_menu=True):
    """Run eclplay with KEYS, as the Box would run the game: --start-menu
    (unless START_MENU is false: ARGS then say where to start), a KRYNN.CFG
    for MACHINE, SAVES copied into OUT/saves (its --saves). Returns (stdout,
    saves directory, the shots' paths in order)."""
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
    assets = pathlib.Path(assets or ROOT / "Assets")
    cfg = out / "krynn.cfg"
    cfg.write_bytes(krynn_cfg(MACHINES[machine][1]).encode())
    result = subprocess.run(
        [str(ROOT / "build/eclplay"), *(["--start-menu"] if start_menu else []),
         "--config", str(cfg), "--saves",
         str(save_dir), *(["--shots", str(shot_dir)] if shots else []), *map(str, args),
         "--keys", keys, str(assets)],
        check=False, capture_output=True, text=True, timeout=600)
    return result.stdout, save_dir, sorted(shot_dir.glob("*.bmp"))


def eclplay_shots(assets, machine, keys, out):
    """eclplay --start-menu's shots for KEYS in MACHINE's display mode."""
    return eclplay(keys, out, assets=assets, machine=machine)[2]


def selftest(args):
    """Boot the game to the title's menu; compare the SSI picture the title
    shows first, and the menu, with eclplay's frames."""
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="cokbox-selftest-"))
    work.mkdir(parents=True, exist_ok=True)
    assets = pathlib.Path(args.assets or ROOT / "Assets")
    if not (ROOT / "build/eclplay").exists():
        raise BoxError("build/eclplay is missing: run make first")
    # eclplay's shots at its key reads: the title's flush reading the key that
    # ended its first wait (the SSI picture), then the menu.
    ssi, menu = eclplay_shots(assets, args.machine, "\\w2.\\k\\r", work / "eclplay")[:2]
    expected = {"ssi": ssi, "menu": menu}
    shown = {}
    with Box(workdir=work / "box", assets=assets, machine=args.machine,
             verbose=args.verbose, timeout=args.timeout) as box:
        box.wait_input(settle=0.5)
        shown["ssi"] = box.screenshot(work / "dosbox-ssi.png")
        box.key(" ")
        box.wait_input(settle=1.0)
        shown["menu"] = box.screenshot(work / "dosbox-menu.png")
        print(f"DS {box.ds:04x}, PSP {box.psp:04x}; the menu waits at {box.where()}")
    status = 0
    for name, path in expected.items():
        want = Screen.load(path)
        count, area = shown[name].diff(want)
        if count:
            shown[name].save_png(work / f"diff-{name}.png", highlight=want)
            status = 1
        print(f"{name} ({args.machine}): {count} pixels differ from eclplay's "
              f"{path.parent.name}/{path.name}" + (f" in {area}" if area else ""))
    nasm = shutil.which("nasm")
    if nasm:  # HOOK_COM is hook.asm's
        built = work / "HOOK.COM"
        subprocess.run([nasm, "-f", "bin", "-o", str(built), str(HERE / "hook.asm")], check=True)
        if built.read_bytes() != HOOK_COM:
            print("HOOK_COM is not hook.asm assembled: update it")
            status = 1
    if args.workdir is None and status == 0:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print(f"files in {work}")
    return status


def run(args):
    box = Box(workdir=args.workdir, assets=args.assets, machine=args.machine,
              display=args.display, sound=args.sound, saves=args.saves or (), args=args.args,
              cycles=args.cycles, turbo=args.turbo, timeout=args.timeout, verbose=args.verbose,
              wav=args.wav)
    with box:
        if args.seed is not None:
            box.wait_input()
            box.set_seed(args.seed)
        box.keys(args.keys or "", shots=args.shots)
        if args.shots:
            box.wait_input(settle=0.5)
            box.screenshot(pathlib.Path(args.shots) / "final.png")
        for spec in args.dump or ():
            off, size = (int(v, 16) for v in spec.split(","))
            print(f"DS:{off:04x}", box.ds_read(off, size).hex(" "))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("run", "selftest"):
        p = sub.add_parser(name)
        p.add_argument("--assets")
        p.add_argument("--workdir")
        p.add_argument("--machine", default="tandy", choices=sorted(MACHINES))
        p.add_argument("--timeout", type=float, default=600)
        p.add_argument("--verbose", "-v", action="store_true")
        if name == "run":
            p.add_argument("--display", help="KRYNN.CFG's first line (default: the machine's)")
            p.add_argument("--sound", default="S")
            p.add_argument("--saves", action="append", help="saved games to copy into C:\\COK\\SAVE")
            p.add_argument("--args", default="", help="START.EXE's arguments")
            p.add_argument("--cycles", default="fixed 20000")
            p.add_argument("--turbo", action="store_true")
            p.add_argument("--seed", type=int)
            p.add_argument("--keys", help="eclplay's key escapes, plus \\sNAME. for a screenshot")
            p.add_argument("--shots", help="directory for screenshots")
            p.add_argument("--wav")
            p.add_argument("--dump", action="append", help="OFF,SIZE (hex) of DS to print at the end")
    args = parser.parse_args(argv)
    try:
        return {"run": run, "selftest": selftest}[args.command](args)
    except BoxError as e:
        print(f"cokbox: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
