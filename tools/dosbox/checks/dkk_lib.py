"""Death Knights of Krynn's helpers for the combat checks (dkk_fight.py and
the later parts' checks).

The original runs under DOSBox-X (cokbox.Box(game="death-knights",
seed=N)); the port is eclplay --game death-knights --start-menu --seed N
on the same keys, built with d_lib's state dump (the records, their combat
records and effects, the combatants, the turn order, the battlefield, the
seed, at each key read's shot and, here, beside the --screen written where
the run ends). The keys carry marks \\sNAME. as d_lib's do.

The original's memory is read through its data segment (DS 0x1bce in
build/death-START_FULL.EXE), each offset as the code that reads it has it:

  DS:a2c6  the list of records, a far pointer (3a37:1318, 410b:1aa4)
  DS:b58b  the combatants, 4 bytes each from 1: x, y, index, size; DS:b58e
           (entry 0's size) their count + 1 (410b:1aa4)
  DS:bc1d  the combatants' records, far pointers, from 1 (410b:1aa4's
           [4n - 0x43e3])
  DS:bd7a  the battlefield, a far pointer: +2, +3 the view, +7 the cells,
           50 by 25 (410b:10d7, 1e41)
  DS:bd79  the bodies' count, and the bodies 7 bytes each from 1 at DS:bd3a:
           record, x, y, the cell under (410b:1aa4)
  DS:c471  the round, DS:c472 the round limit (410b:1e41)
  DS:c4d1  the turn order, far pointers from 1 (3e05:0601, 00ec's 4n -
           0x3b2f), and DS:c5f5 the entry whose turn it is (3e05:00ec)
  DS:be68  the sides that can act, the party's and the enemy's (65cd:28bb)
  DS:84ac  RandSeed (1a07:163b, cokbox's GAMES)
  DS:8c4e  the mode (3e05:00ec sets 5)

A record (216 bytes, src/rules_death_knights.c): the name at +0, the
effects' list at +0x3a (9-byte nodes, the next at +5), the items at +0x7d,
the next record at +0xb9, the combat record at +0xbd (22 bytes), the
status at +0xc7, able to act +0xc8, the side +0xc9, the computer's +0xca,
the hit points +0xd6.
"""
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import d_lib  # noqa: E402

cokbox = checklib.cokbox

ROOT = cokbox.ROOT
ASSETS = ROOT / "DKKAssets"

DS_PARTY = 0xA2C6
DS_COMBATANTS, DS_COUNT, DS_RECORDS = 0xB58B, 0xB58E, 0xBC1D
DS_MAP, DS_BODIES, DS_BODY = 0xBD7A, 0xBD79, 0xBD3A
DS_ROUND, DS_LIMIT = 0xC471, 0xC472
DS_ORDER, DS_ENTRY = 0xC4D1, 0xC5F5
DS_SIDES = 0xBE68
DS_RANDSEED = 0x84AC
DS_MODE = 0x8C4E
RECORD = 0xD8
HP, STATUS, ACTIVE, SIDE, COMPUTER = 0xD6, 0xC7, 0xC8, 0xC9, 0xCA

# After the title's two keys (the pictures', the credits': BOX_TITLE in the
# original once its fourth picture shows, PORT_TITLE in eclplay): Play, the
# start menu's Load Saved Game from Death Knights' A, then Begin
# Adventuring: block 16's first texts.
LOAD_A_BEGIN = r"\r\v\v\r\r\r\v\v\v\v\v\r"
BOX_TITLE, PORT_TITLE = "xx", r"\kx\kx"
# The original's steps of those keys, each typed once it waits after the
# one before (keys typed ahead while a game loads are flushed).
LOAD_A_STEPS = ["x", "x", r"\r", r"\v\v", r"\r", r"\r", r"\r", r"\v\v\v\v\v\r"]


def type_steps(box, steps, settle=1.5):
    """Type each of STEPS once the original waits after the one before."""
    for keys in steps:
        box.keys(keys)
        box.wait_input(settle=settle)


def pstr(raw):
    return raw[1:1 + min(raw[0], len(raw) - 1)].decode("latin-1").rstrip("\0")


def title(box):
    """Wait in the original until the title's fourth picture shows (a key
    typed before the first is lost to startup's wait), as dkk_start does."""
    shown, seen = None, 0
    for _ in range(400):
        box.keys("\\p25.")
        shot = box.screenshot()
        # DOS's text screen (640x350) can still be up under load: the
        # title has not begun.
        if (shot.width, shot.height) != (320, 200):
            continue
        if (shown is None and sum(1 for p in shot.pixels if p) > 10000) or \
                (shown is not None and shot.diff(shown)[0] > 1000):
            seen += 1
            if seen == 4:
                return True
        shown = shot if shown is not None or seen else None
    return False


# ------------------------------------------------- the original's memory

def effects(b, rec):
    """The record's spell effects: (id, minutes, value) along +0x3a."""
    out = []
    eff = b.far(rec + 0x3A)
    while eff is not None and len(out) < 30:
        e = b.read(eff, 9)
        out.append((e[0], struct.unpack_from("<H", e, 1)[0], e[3]))
        eff = b.far(eff + 5)
    return out


def record(b, rec):
    raw = b.read(rec, RECORD)
    p = b.far(rec + 0xBD)
    return dict(rec=rec, raw=raw, name=pstr(raw[:16]), hp=raw[HP], status=raw[STATUS],
                combat=b.read(p, 22) if p is not None else None, effects=effects(b, rec))


def party(b):
    """The original's list of records (DS:a2c6, along +0xb9)."""
    out, rec = [], b.far(b.ds * 16 + DS_PARTY)
    while rec is not None and len(out) < 80:
        out.append(record(b, rec))
        rec = b.far(rec + 0xB9)
    return out


def combatants(b):
    """The original's combatants, as d_lib.combatants gives Champions'."""
    count = b.ds_byte(DS_COUNT)
    out = []
    for n in range(1, min(count, 73)):
        x, y, _, size = b.ds_read(DS_COMBATANTS + 4 * n, 4)
        rec = b.far(b.ds * 16 + DS_RECORDS + 4 * n)
        if rec is None:
            continue
        out.append(dict(record(b, rec), n=n, x=x, y=y, size=size))
    return out


def order(b):
    """The turn order's names, and the entry whose turn it is."""
    names = []
    for i in range(1, 73):
        rec = b.far(b.ds * 16 + DS_ORDER + 4 * i)
        if rec is None:
            break
        names.append(pstr(b.read(rec, 16)))
    return names, b.ds_byte(DS_ENTRY)


def cells(b):
    """The battlefield's 25 rows of 50 cells, or None outside a battle."""
    p = b.far(b.ds * 16 + DS_MAP)
    if p is None:
        return None
    raw = b.read(p + 7, 50 * 25)
    return [raw[50 * y:50 * y + 50] for y in range(25)]


# ------------------------------------------------------- the port's state

FINAL_AT = "        cok_ecl_status status = cok_start_game(&game, woof);\n" \
           "        if (screen != NULL) save(&p, screen);\n"


def final_state(name, text):
    """src/eclplay.c: the state beside --screen too, where the start menu's
    run ends (a stop not yet ported, say)."""
    if FINAL_AT not in text:
        raise cokbox.BoxError("src/eclplay.c's --screen changed: update dkk_lib.final_state")
    return text.replace(FINAL_AT, FINAL_AT.replace(
        "save(&p, screen);\n", "{\n            save(&p, screen);\n"
        "            d_state(&p, screen);\n        }\n"))


VARS_AT = "            d_state(p, path);\n"
VARS_CODE = r"""/* tools/dosbox/checks/dkk_lib.py: the variables 0x7c00-0x7fff beside
 * each state (NNN.7c00), a byte each, as Death Knights holds them. */
static void dkk_vars(const player *p, const char *bmp)
{
    char path[4200];
    size_t n = strlen(bmp);
    snprintf(path, sizeof path, "%.*s.7c00", (int)(n > 4 ? n - 4 : n), bmp);
    FILE *f = fopen(path, "wb");
    if (f == NULL) return;
    for (size_t i = 0; i < 0x400; ++i) fputc(p->game->vm.mem7c00[i] & 0xff, f);
    fclose(f);
}

"""


def vars_dump(name, text):
    """src/eclplay.c: the variables 0x7c00-0x7fff beside each state."""
    if VARS_AT not in text:
        raise cokbox.BoxError("d_lib.state_dump changed: update dkk_lib.vars_dump")
    text = text.replace(VARS_AT, VARS_AT + "            dkk_vars(p, path);\n")
    return text.replace(d_lib.STATE_AT, VARS_CODE + d_lib.STATE_AT, 1)


LOG_AT = ("static void log_line(cok_adventure *game, const char *kind, const char *text, "
          "void *context)\n{\n    (void)game;\n    (void)context;\n")
LOG_CODE = r"""    /* tools/dosbox/checks/dkk_lib.py: at each round's end (round.c's
     * mark), the screen and the state in rounds/NN beside the shots. */
    if (strcmp(kind, "dkk") == 0) {
        player *pp = context;
        static unsigned rounds;
        if (pp->shots == NULL || pp->game == NULL) return;
        char path[4200];
        snprintf(path, sizeof path, "%s/../rounds/%02u.bmp", pp->shots, ++rounds);
        save(pp, path);
        d_state(pp, path);
        dkk_vars(pp, path);
        return;
    }
"""


def round_log(name, text):
    """src/eclplay.c: the state at round.c's mark (round_mark)."""
    if LOG_AT not in text:
        raise cokbox.BoxError("src/eclplay.c's log_line changed: update dkk_lib.round_log")
    return text.replace(LOG_AT, LOG_AT + LOG_CODE)


def dump(name, text):
    """src/eclplay.c: d_lib's state at each key read, with the variables
    0x7c00-0x7fff, and at the run's end, and at each round's end (one
    transform: checklib.build hashes each on the source as it is)."""
    return round_log(name, final_state(name, vars_dump(name, d_lib.state_dump(name, text))))


ROUND_AT = "bool cok_combat_end_round(cok_adventure *game, bool *done)\n{\n"


def round_mark(name, text):
    """src/round.c: a mark logged as each round ends (dkk 3e05:0ed0's
    start), where the original is trapped (trap)."""
    if ROUND_AT not in text:
        raise cokbox.BoxError("src/round.c's cok_combat_end_round changed: update dkk_lib")
    return text.replace(ROUND_AT, ROUND_AT + '    cok_adventure_log(game, "dkk", "round");\n')


AMBUSH_AT = ("    if (cok_ecl_meaning(vm) == COK_ECL_SETUP_MONSTERS) game->ambush = "
             "(uint8_t)cok_ecl_value(vm, 3);\n")


def ambush_poke(name, text):
    """src/monster.c: COK_DKK_AMBUSH (hex) as SETUP MONSTERS' fourth operand,
    as a check pokes the original's DS:c4cc once SETUP MONSTERS has run."""
    if AMBUSH_AT not in text:
        raise cokbox.BoxError("src/monster.c's setup_monster changed: update dkk_lib.ambush_poke")
    return text.replace(AMBUSH_AT, AMBUSH_AT + (
        "    if (getenv(\"COK_DKK_AMBUSH\") != NULL)\n"
        "        game->ambush = (uint8_t)strtoul(getenv(\"COK_DKK_AMBUSH\"), NULL, 16);\n"
        # COK_DKK_POKE, "ADDR=BYTE,...": the ECL's code changed once SETUP
        # MONSTERS has run, as a check pokes the original's (DS:8c39 + ADDR -
        # 0x8000): the next LOAD MONSTERs' operands, say.
        "    if (cok_ecl_meaning(vm) == COK_ECL_SETUP_MONSTERS && getenv(\"COK_DKK_POKE\") != NULL)\n"
        "        for (const char *p = getenv(\"COK_DKK_POKE\"); *p != '\\0';) {\n"
        "            char *end;\n"
        "            unsigned long at = strtoul(p, &end, 16), byte = strtoul(end + 1, &end, 16);\n"
        "            if (at >= 0x8000 && at < 0x8000 + COK_ECL_CODE_SIZE)\n"
        "                vm->code[at - 0x8000] = (uint8_t)byte;\n"
        "            p = *end == ',' ? end + 1 : end;\n"
        "        }\n"))


def player():
    """eclplay writing the state at each key read and at the run's end (and
    taking COK_DKK_AMBUSH and COK_DKK_POKE)."""
    return checklib.build("eclplay", transforms=[("eclplay.c", dump),
                                                  ("monster.c", ambush_poke),
                                                  ("round.c", round_mark)])


def port_vars(bmp):
    """The port's variables 0x4b00-0x4eff and 0x7c00-0x7fff beside BMP, a
    byte each, or None."""
    state = pathlib.Path(bmp).with_suffix(".json")
    high = pathlib.Path(bmp).with_suffix(".7c00")
    if not state.exists() or not high.exists():
        return None
    low = bytes.fromhex(json.loads(state.read_text())["vars"])[::2]
    return low, high.read_bytes()


def code_poke(box, poke):
    """POKE (ECL address: byte) into the original's ECL code (DS:8c39, a
    far pointer to address 0x8000; 3c1e's fetch reads DS:8c39 + ip -
    0x8000)."""
    base = box.far(box.ds * 16 + 0x8C39)
    for at, byte in sorted(poke.items()):
        box.write(base + at - 0x8000, bytes([byte]))


def box_vars(b):
    """The original's variables: its blocks 0x4b00 and 0x7c00 (DS:8c2d, 8c31,
    far pointers; 3632's handlers)."""
    return b.read(b.far(b.ds * 16 + 0x8C2D), 0x400), b.read(b.far(b.ds * 16 + 0x8C31), 0x400)


def port(work, name, keys, seed, saves=None, args=(), ambush=None, poke=None):
    """eclplay on KEYS (marks dropped) from the start menu, seeded, with
    SAVES (default DKKAssets/SAVE) copied: (stdout + stderr, the shot of
    the key read where the keys ran out (d_lib.mark_read) or None, the
    final screen's path, its state or None)."""
    out = pathlib.Path(work) / f"port-{name}"
    if out.exists():
        shutil.rmtree(out)
    shots, save_dir = out / "shots", out / "saves"
    shots.mkdir(parents=True)
    (out / "rounds").mkdir()
    shutil.copytree(saves or ASSETS / "SAVE", save_dir)
    cfg = out / "DKK.CFG"
    cfg.write_bytes((ASSETS / "DKK.CFG").read_bytes())
    final = out / "final.bmp"
    env = dict(os.environ)
    if ambush is not None:
        env["COK_DKK_AMBUSH"] = f"{ambush:x}"
    if poke:
        env["COK_DKK_POKE"] = ",".join(f"{a:x}={b:x}" for a, b in sorted(poke.items()))
    result = subprocess.run(
        [str(player()), "--game", "death-knights", "--start-menu", "--display", "ega",
         "--config", str(cfg), "--saves", str(save_dir), "--shots", str(shots), "--screen",
         str(final), "--seed", str(seed), *map(str, args), "--keys",
         PORT_TITLE + checklib.port_keys(keys), str(ASSETS)],
        capture_output=True, text=True, cwd=ROOT, timeout=600, env=env)
    log = result.stdout + result.stderr
    (out / "log.txt").write_text(log)
    found = sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))
    at = d_lib.mark_read(found)
    return log, found[at] if at is not None else None, final, read_state(final)


def read_state(bmp):
    """The port's state beside BMP (d_lib.state_dump's JSON), with each
    record's 216 bytes and name, or None."""
    path = pathlib.Path(bmp).with_suffix(".json")
    if not path.exists():
        return None
    s = json.loads(path.read_text())
    for r in s["records"]:
        r["raw"] = bytes.fromhex(r.pop("record"))[:RECORD]
        r["combat"] = bytes.fromhex(r["combat"]) if r["combat"] else None
        r["effects"] = [tuple(e) for e in r["effects"]]
        r["name"] = pstr(r["raw"][:16])
        r["hp"], r["status"] = r["raw"][HP], r["raw"][STATUS]
    s["cells"] = [bytes.fromhex(row) for row in s["cells"]]
    return s


def port_combatants(s):
    out = []
    for n, x, y, size, i in s["combatants"]:
        if i < 0:
            continue
        out.append(dict(s["records"][i], n=n, x=x & 0xFF, y=y & 0xFF, size=size))
    return out


def fighters(cs):
    """What the checks compare of combatants: n, name, cell, size, hit
    points, status, side, the computer's, effects, the combat record."""
    return [(c["n"], c["name"], c["x"], c["y"], c["size"], c["hp"], c["status"],
             c["raw"][SIDE], c["raw"][COMPUTER], c["effects"],
             c["combat"].hex() if c["combat"] else None) for c in cs]


def port_rounds(work, name):
    """The port's states at each round's end (round_mark) of run NAME, in
    order: (the screen's path, the state)."""
    out = sorted((pathlib.Path(work) / f"port-{name}" / "rounds").glob("*.bmp"))
    return [(b, read_state(b)) for b in out]


# ------------------------------------------------------------ the traps

# Turbo Pascal's OvrHeapOrg and OvrHeapEnd (dkk DS:8478, 847c; the
# overlay manager's, as Champions' DS:437a, 437e).
DS_OVR_ORG, DS_OVR_END = 0x8478, 0x847C


def trap(b, seg, off, until, seconds=60):
    """d_lib.trap for Death Knights: the routine at Ghidra SEG:OFF of
    death-START_FULL.EXE made to spin where it starts (EB FE), in every
    copy in the overlay buffer, until UNTIL() holds."""
    patched = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        for addr in code_at(b, seg, off):
            if b.read(addr, 2) == b"\xeb\xfe":
                continue
            patched.append((addr, b.read(addr, 2)))
            b.write(addr, b"\xeb\xfe")
        if patched and until():
            return Trap(patched, seg, off)
        time.sleep(0.002)
    raise cokbox.BoxError(f"the trap at {seg:04x}:{off:04x} did not hold")


class Trap(list):
    """A trap's patched (address, bytes), and the routine it is at."""

    def __init__(self, patched, seg, off):
        super().__init__(patched)
        self.seg, self.off = seg, off


def code_at(b, seg, off, span=0x60, size=12):
    """Where the overlay buffer holds death-START_FULL.EXE's code at Ghidra
    SEG:OFF now: found by windows of SIZE bytes from OFF on (the loader's
    fixups change some, a far call's segment), each where it lies at OFF's
    paragraph, the place most windows agree on."""
    lo, hi = b.ds_word(DS_OVR_ORG) * 16, b.ds_word(DS_OVR_END) * 16
    if not 0 < lo < hi <= 0xA0000:
        return []
    data = b.read(lo, hi - lo)
    votes = {}
    for k in range(0, span, size):
        probe = b._exe_bytes(seg, off + k, size)
        for m in re.finditer(re.escape(probe), data):
            addr = lo + m.start() - k
            if (addr - off) % 16 == 0:
                votes[addr] = votes.get(addr, 0) + 1
    if not votes:
        return []
    most = max(votes.values())
    return [a for a, n in votes.items() if n == most and n >= 2]


def untrap(b, handle):
    """The code trap patched given back, the emulation paused (d_lib.untrap);
    and a spin the overlay manager copied elsewhere with the code while the
    trap held (a spell's overlay loaded in the round: the mage flow's black
    wizards), which the game would run into next, given back with the
    executable's bytes."""
    d_lib.untrap(b, handle)
    if isinstance(handle, Trap):
        moved = [(addr, b._exe_bytes(handle.seg, handle.off, 2))
                 for addr in code_at(b, handle.seg, handle.off) if b.read(addr, 2) == b"\xeb\xfe"]
        if moved:
            b.write_paused(moved)


def settled(b, seconds=1.5, limit=120):
    """Whether the game stands still: RandSeed, the round, the entry and
    the records' bytes unchanged for SECONDS (a trap's spin)."""
    def probe():
        return (b.ds_dword(DS_RANDSEED), b.ds_byte(DS_ROUND), b.ds_byte(DS_ENTRY),
                tuple(r["raw"] for r in party(b)))
    end = time.monotonic() + limit
    last, since = probe(), time.monotonic()
    while time.monotonic() < end:
        time.sleep(0.1)
        now = probe()
        if now != last:
            last, since = now, time.monotonic()
        elif time.monotonic() - since >= seconds:
            return True
    return False
