"""Helpers shared by batch E's checks (freed and garbage memory where the
port stops): saved games patched and written by the port, the party's
records and effects, Turbo Pascal's heap and the interrupt table read in
the original, and whether the game still takes keys.
"""
import pathlib
import shutil
import struct
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

SAVE = cokbox.ROOT / "SAVE"
# Turbo Pascal 6's System variables (DS): HeapOrg, HeapPtr, FreeList,
# PrefixSeg (checks/slow_poison.py), and RandSeed.
HEAP_ORG, HEAP_PTR, FREE_LIST, PREFIX_SEG = 0x4386, 0x438A, 0x4392, 0x43A8
PARTY = 0x609A          # the list of records (far pointer), next at +0x17f
# Title, Play, Load Saved Game LETTER, Begin Adventuring.
LOAD = r"\kxP\v\v\r{}\v\v\v\v\v\r"


class Box(cokbox.Box):
    """cokbox.Box started again (up to three times) when DOSBox-X shows no
    window in time, as happens under a heavy load."""

    def start(self):
        for attempt in range(3):
            try:
                return super().start()
            except cokbox.BoxError as e:
                if "window" not in str(e) or attempt == 2:
                    raise
                self.log(f"{e}: starting again")
                self.close()


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


def eclplay_block(keys, out, args, block, shots=False):
    """eclplay ARGS --keys KEYS Assets [BLOCK] (cokbox.eclplay puts the
    assets last, so the block is run here; None for a saved game's own)."""
    import subprocess
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    shot_dir, save_dir = out / "shots", out / "saves"
    for d in (shot_dir, save_dir):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir()
    cfg = out / "krynn.cfg"
    cfg.write_bytes(cokbox.krynn_cfg("T").encode())
    result = subprocess.run(
        [str(cokbox.ROOT / "build/eclplay"), "--config", str(cfg), "--saves", str(save_dir),
         *(["--shots", str(shot_dir)] if shots else []), *map(str, args), "--keys", keys,
         str(cokbox.ROOT / "Assets"), *([] if block is None else [str(block)])],
        check=False, capture_output=True, text=True, timeout=600)
    return result.stdout + result.stderr, save_dir, sorted(shot_dir.glob("*.bmp"))


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
    """HeapOrg, HeapPtr and the free list (seg:off, size), as text."""
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
    org = ptr(HEAP_ORG)
    return "HeapOrg %04x:%04x, HeapPtr %04x:%04x, free list [%s]" % (
        org + top + (", ".join(blocks) or "empty",))


def low_memory(box):
    return box.read(0, 0x600)


def low_changes(before, after):
    """Words of the interrupt table and BIOS data that changed, leaving out
    the keyboard buffer and flags, the clock and the video BIOS's cursor."""
    skip = [(0x417, 0x419), (0x41A, 0x440), (0x46C, 0x472), (0x450, 0x452), (0x4F0, 0x500)]
    out = []
    for i in range(0, 0x600, 2):
        if before[i:i + 2] != after[i:i + 2] and not any(a <= i < b for a, b in skip):
            out.append(f"{i:04x}: {before[i:i + 2].hex()} -> {after[i:i + 2].hex()}")
    return out


def takes_keys(box, seconds=20):
    """Whether the game waits for a key within SECONDS (and where)."""
    try:
        box.wait_input(seconds=seconds)
        return True, box.where()
    except cokbox.BoxError as e:
        return False, str(e)


def watch(box, seconds, work, prefix):
    """Screenshots for SECONDS, the different ones saved; returns them."""
    seen = []
    end = time.monotonic() + seconds
    while time.monotonic() < end and box.running:
        shot = box.screenshot()
        if not seen or shot.pixels != seen[-1].pixels:
            shot.save_png(pathlib.Path(work) / f"{prefix}-{len(seen):02d}.png")
            seen.append(shot)
    return seen


def cs_ip_sample(box, samples=8, interval=0.25):
    """Where the CPU is, sampled from DOSBox-X's own state is not readable;
    instead sample the hook's poll/read counters to tell a loop with no key
    look from a wait."""
    out = []
    for _ in range(samples):
        out.append((box.hook("reads"), box.hook("polls")))
        time.sleep(interval)
    return out
