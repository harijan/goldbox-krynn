#!/usr/bin/env python3
"""Death Knights of Krynn's training, Knight Change Order, Memorize, Fix and Rest in
the original against eclplay --game death-knights --start-menu (DEVELOPERS.md,
"Training" and "Camp"). Two flows, the same keys typed to both, the
original's screen at each wait against the port's at the key read the same
keys leave it in:

train: saved game A with its hall variable 0x7ea8 at 127 (every class; as
a hall's script sets it before PROGRAM 0) and FLAVIUS (cleric 6, fighter 7,
mage 6) with 100,000 experience, loaded: Knight Change Order (49cb:4ea4)
for SIR DRYDEN, a knight 7 of the Crown, Change; Train Character
(49cb:43b6) for FLAVIUS: the levels he will reach, Yes, the spells to
choose (580d:2d2a), the one it starts on learned; Save Current Game C: saved game C
and its characters' files (but the stack's bytes and far pointers,
dkk_start's masks).

memorize: saved game A with FLAVIUS's first free memorized byte Bless
marked (0x81), loaded and begun: block 16's rest, Yes; camp, Magic,
FLAVIUS picked, Memorize (4381:0f67): the marked spells asked with Keep
(5dc5:016e's mode 5), Escape unmarks them; the memorize table of 8 levels
(4381:09c7) and the grimoire; Escape; Display (4381:15aa); Escape.

table: saved game B begun, its rest, camp, Magic, BALTHIR NOMOON picked,
Memorize: the table (4381:09c7) clears its rows and says no name.

cast: saved game A with NEACAIL's spells memorized Cure Light Wounds alone
and CELESTE 6 hit points down, begun: camp, Magic, NEACAIL picked, Cast
(4381:091c): the spells in memory, the cure (5dc5:1298) on whom (5dc5:1182),
CELESTE, healed; the records after (hit points, NEACAIL's spells: 65cd:17c4
marks the one cast).

fix: saved game A with SIR DRYDEN dead, LILLITH stoned, TANAR poisoned (0x37)
and 0x3d, FLAVIUS cursed (0x24) and poisoned, CELESTE blind (0x21) and 20
hit points down, NEACAIL's spells memorized Cure Light Wounds twice, Raise
Dead, Stone to Flesh, Remove Curse, Heal and Neutralize Poison, and a
cleric spell of level 5 a day, CELESTE one of level 6; begun: camp, Fix
(4381:2fd4): its two rests, the cures said (2a73: each, and the selected
SIR DRYDEN "regains sight" for the blind CELESTE, both times), the healing;
the camp's menu after; Save Current Game C: saved game C (the clock, the
rest's counts) and the characters' files, and RandSeed.

rest: saved game A begun, camp, Rest (4381:0dee, 5b7d:0d51): the menu by
index (081d) from the camp menu's selection (Mins), Enter, Add, Enter, Left,
Up, Hours, the keypad's 8, Down, the keypad's 4, Rest: 15 minutes; Save C
(Quit to DOS, DS:124c): saved game C (the clock) and the characters' files.

stop: camp, Rest, an hour, Rest with a key waiting at its first poll (a trap):
"Stop resting? " from Yes, Enter; again with No, resting out; Save C.

interrupted: camp, Rest, a day: block 16's camp vector's 0x7ed2 255 and
0x7ed3 100 interrupt it at the 255th tick; the rest vector's text.

The rest flows compare RandSeed and the clock (0x4bc6-0x4bcc) too.

Seeded (Box seed, eclplay --seed). Not part of run_all.py's set. About
four minutes, and as much for the rest flows.

    python3 tools/dosbox/checks/dkk_levels.py [--workdir DIR] [--seed N]
        [--flows train,memorize,table,cast,fix,rest,stop,interrupted]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import struct
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402
import dkk_start  # noqa: E402

cokbox = checklib.cokbox

# The title's Play, the start menu's Load Saved Game from Death Knights' A.
LOAD_A = r"\r\v\v\r\r\r"
LOAD_STEPS = ["x", "x", r"\r", r"\v\v", r"\r", r"\r", r"\r"]
# After the load, with a hall: the steps, each (the screen reached, the keys
# typed at the wait before it).
TRAIN = [
    ("knight", r"\v\v\r"),     # Knight Change Order: Pick Character
    ("petition", r"\r"),       # SIR DRYDEN is petitioning ...: Change Exit
    ("changed", "C"),          # Change: the start menu
    ("train", r"\^\r"),        # Train Character: Pick Character
    ("will-become", r"\v\v\v\r"),  # FLAVIUS will become: do you wish to train?
    ("choose", r"\r"),         # Yes: congratulations..., the spells to choose
    ("trained", r"\r"),        # the one the list starts on learned: the start menu
    ("which", r"\v\v\v\v\v\r"),  # Save Current Game: save which game
    ("saved", "C"),
]
# After Begin: block 16's two texts, Yes to rest: the camp; Magic, FLAVIUS
# (the fourth: down three), Memorize.
MEMORIZE = [
    ("camp", r"\r\r\r"),
    ("magic", "M"),
    ("flavius", r"\v\v\v"),
    ("keep", "M"),             # the marked spells: spells OK? Keep
    ("table", r"\e"),          # unmarked: the table and the grimoire
    ("magic-again", r"\e"),
    ("display", "D"),
    ("displayed", r"\e"),
]
# Display's Exit: the original's menu (6a69:0f1b) keeps the selection the
# Magic menu left (Display, its fourth item), past the one item it offers,
# and highlights none; the port's (67b5:03e2's) takes the first instead.
# Death Knights' menus as a whole are part 3's (commands).
MASKS = {"display": [(0, 192, 40, 8, "Exit highlighted: 6a69:0f1b keeps a selection past "
                                   "its items (part 3, the menus)")]}


def load(path):
    return cokbox.Screen.load(path) if path is not None else None


def hall_saves(work):
    saves = work / "saves-train"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    game = bytearray((saves / "SAVGAMA.PTY").read_bytes())
    game[1 + 0x400 + 0x2A8] = 127  # var 0x7ea8, in the 0x7c00 block's bytes
    (saves / "SAVGAMA.PTY").write_bytes(bytes(game))
    flavius = bytearray((saves / "CHRDATA4.GAM").read_bytes())
    struct.pack_into("<I", flavius, 0x62, 100000)
    (saves / "CHRDATA4.GAM").write_bytes(bytes(flavius))
    return saves


def marked_saves(work):
    saves = work / "saves-memorize"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    wiz = bytearray((saves / "CHRDATA4.WIZ").read_bytes())
    wiz[wiz.index(0)] = 0x81
    (saves / "CHRDATA4.WIZ").write_bytes(bytes(wiz))
    return saves


def run_steps(v, work, box, flow, steps, prefix, seed, saves, masks=None):
    keys = prefix
    port_saves = None
    for name, typed in steps:
        keys += typed
        K.type_steps(box, [typed], settle=2.0)
        shown = box.screenshot(work / f"dosbox-{flow}-{name}.png")
        _, shot, _, _ = K.port(work, f"{flow}-{name}", keys, seed, saves=saves)
        port_saves = work / f"port-{flow}-{name}" / "saves"
        v.screen(f"{flow}: {name}", shown, load(shot), masks=(masks or {}).get(name, ()),
                 work=work)
    return port_saves


def compare_files(v, original, port, flow="train"):
    a, b = (original / "SAVGAMC.PTY").read_bytes(), (port / "SAVGAMC.PTY").read_bytes()
    v.check(dkk_start.same_saved(a, b), f"{flow}: saved game C (but the stack's bytes)")
    names = sorted(p.name for p in original.iterdir() if p.name.startswith("CHRDATC"))
    v.value(f"{flow}: the character files", names,
            sorted(p.name for p in port.iterdir() if p.name.startswith("CHRDATC")))
    for n in names:
        x = dkk_start.masked_character(n, (original / n).read_bytes())
        y = dkk_start.masked_character(n, (port / n).read_bytes())
        diff = [k for k in range(min(len(x), len(y))) if x[k] != y[k]][:8]
        v.check(x == y, f"{flow}: {n}" + ("" if x == y else f" differs at {[hex(k) for k in diff]}"
                                          f" (sizes {len(x)}, {len(y)})"))


def train(v, work, opts):
    seed = opts.seed
    saves = hall_saves(work)
    with cokbox.Box(workdir=work / "box-train", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures")
        K.type_steps(box, LOAD_STEPS, settle=2.0)
        shown = box.screenshot(work / "dosbox-train-loaded.png")
        _, shot, _, _ = K.port(work, "train-loaded", LOAD_A, seed, saves=saves)
        v.screen("train: the start menu with a hall", shown, load(shot), work=work)
        port_saves = run_steps(v, work, box, "train", TRAIN, LOAD_A, seed, saves)
        original = work / "box-train" / "c" / "DKK" / "SAVE"
    compare_files(v, original, port_saves)


def memorize(v, work, opts):
    seed = opts.seed
    saves = marked_saves(work)
    with cokbox.Box(workdir=work / "box-memorize", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures (memorize)")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        run_steps(v, work, box, "memorize", MEMORIZE, K.LOAD_A_BEGIN, seed, saves, MASKS)


# Saved game B, begun: the rest, camp, Magic, BALTHIR NOMOON (the fourth),
# Memorize: the table clears its rows and says no name (4381:09c7), where a
# name of 13 letters or more would show past the labels.
LOAD_B_BEGIN = r"\r\v\v\r\rB\v\v\v\v\v\r"
LOAD_B_STEPS = ["x", "x", r"\r", r"\v\v", r"\r", r"\r", "B", r"\v\v\v\v\v\r"]
TABLE = MEMORIZE[:2] + [("balthir", r"\v\v\v"), ("table", "M")]


def table(v, work, opts):
    seed = opts.seed
    saves = work / "saves-table"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    with cokbox.Box(workdir=work / "box-table", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures (table)")
        K.type_steps(box, LOAD_B_STEPS, settle=2.0)
        run_steps(v, work, box, "table", TABLE, LOAD_B_BEGIN, seed, saves)


# After Begin: the camp; Magic, NEACAIL (the fifth: down four), Cast, the
# cure, on whom: down to CELESTE, Select.
CAST = [
    ("camp", r"\r\r\r"),
    ("magic", "M"),
    ("neacail", r"\v\v\v\v"),
    ("list", "C"),
    ("on-whom", r"\r"),
    ("celeste", r"\v"),
    ("healed", r"\r"),
]


def cure_saves(work):
    saves = work / "saves-cast"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    wiz = bytearray((saves / "CHRDATA5.WIZ").read_bytes())
    wiz[:0x4A] = bytes([3]) + bytes(0x49)
    (saves / "CHRDATA5.WIZ").write_bytes(bytes(wiz))
    celeste = bytearray((saves / "CHRDATA6.GAM").read_bytes())
    celeste[0xD6] -= 6
    (saves / "CHRDATA6.GAM").write_bytes(bytes(celeste))
    return saves


def cast(v, work, opts):
    seed = opts.seed
    saves = cure_saves(work)
    with cokbox.Box(workdir=work / "box-cast", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures (cast)")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        run_steps(v, work, box, "cast", CAST, K.LOAD_A_BEGIN, seed, saves)
        keys = K.LOAD_A_BEGIN + "".join(typed for _, typed in CAST)
        _, _, _, state = K.port(work, "cast-end", keys, seed, saves=saves)
        if state is None:
            v.differ("cast: the port's state at the end", "none written")
            return
        mine = [(r["name"], r["hp"], r["status"]) for r in state["records"]]
        theirs = [(r["name"], r["hp"], r["status"]) for r in K.party(box)]
        v.value("cast: the records' names, hit points and status", theirs, mine)
        raw = json_records(work / "port-cast-end" / "final.json")
        spells = box.read(box.far(K.party(box)[4]["rec"] + 0xC2), 0x4A)
        v.value("cast: NEACAIL's spells memorized", spells.hex(),
                raw[4][0xD8:0xD8 + 0x4A].hex() if raw else None)
        v.value("cast: RandSeed", box.ds_dword(K.DS_RANDSEED), state["seed"])


# After Begin: the camp; Fix, its rests and cures, the camp's menu; Save
# Current Game C (by index: the third).
FIX = [
    ("camp", r"\r\r\r"),
    ("fixed", "F"),
    ("which", "S"),
    ("saved", r"\>\>\r"),
]


def effects_file(saves, name, ids):
    """Effects IDS (id, value: permanent, none run when removed) put before
    those of character file NAME's .EFF (9 bytes each, the next pointer 1
    while one follows, as 46b2's read takes it)."""
    path = saves / f"{name}.EFF"
    old = path.read_bytes() if path.exists() else b""
    data = b""
    for n, (id_, value) in enumerate(ids):
        last = n + 1 == len(ids) and not old
        data += bytes([id_, 0, 0, value, 0]) + (bytes(4) if last else b"\1\0\1\0")
    path.write_bytes(data + old)


def fix_saves(work):
    saves = work / "saves-fix"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)

    def edit(name, change):
        data = bytearray((saves / name).read_bytes())
        change(data)
        (saves / name).write_bytes(bytes(data))

    def dead(r):
        r[K.STATUS], r[K.HP], r[K.ACTIVE] = 6, 0, 0

    def stoned(r):
        r[K.STATUS], r[K.ACTIVE] = 7, 0

    def hurt(r):
        r[K.HP] -= 20

    def spells(w):
        ids = [0x03, 0x03, 0x4B, 0x42, 0x2B, 0x35, 0x43]
        w[:0x4A] = bytes(ids) + bytes(0x4A - len(ids))
        w[0xBC + 4] = 1        # a cleric spell of level 5 a day

    def sixth(w):
        w[0xBC + 5] = 1        # and of level 6

    edit("CHRDATA1.GAM", dead)
    edit("CHRDATA2.GAM", stoned)
    effects_file(saves, "CHRDATA3", [(0x37, 0), (0x3D, 0)])
    effects_file(saves, "CHRDATA4", [(0x24, 0), (0x37, 0)])
    effects_file(saves, "CHRDATA6", [(0x21, 0)])
    edit("CHRDATA6.GAM", hurt)
    edit("CHRDATA5.WIZ", spells)
    edit("CHRDATA6.WIZ", sixth)
    return saves


def fix(v, work, opts):
    seed = opts.seed
    saves = fix_saves(work)
    with cokbox.Box(workdir=work / "box-fix", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures (fix)")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        port_saves = run_steps(v, work, box, "fix", FIX, K.LOAD_A_BEGIN, seed, saves)
        keys = K.LOAD_A_BEGIN + "".join(typed for _, typed in FIX)
        _, _, _, state = K.port(work, "fix-end", keys, seed, saves=saves)
        v.value("fix: RandSeed", box.ds_dword(K.DS_RANDSEED),
                state["seed"] if state is not None else None)
        original = work / "box-fix" / "c" / "DKK" / "SAVE"
    compare_files(v, original, port_saves, "fix")


# Camp's Rest (4381:0dee, the rest 5b7d:0d51, its menu 081d by index) in
# block 16, whose camp vector sets 0x7ed2 255 and 0x7ed3 100: each step
# (the screen reached, the keys, how): typed as the original waits; "rest"
# typed and waited past the rest (DS:c462 clear) before the next wait, the
# rest's own polls of KeyPressed not being one; "ahead" its first key with
# the rest's first poll of KeyPressed (5b7d:0e20) trapped, its second once
# the game spins there, then let go: the poll sees it, as eclplay's \[...\]
# after the first key puts it in the buffer at the next wait.
# (K.push_after_flush misses the menu's flush: the rest polls at once.)
DS_RESTING = 0xC462
REST_POLL = (0x5B7D, 0x0E20)
REST = [
    ("camp", r"\r\r\r", None),
    ("rest", "R", None),          # the menu from the camp's selection: Mins
    ("mins", r"\r", None),        # Enter takes Mins
    ("add", "a", None),           # 0:05, Add selected
    ("add-enter", r"\r", None),   # Enter on Add: 0:10
    ("left", r"\<", None),        # the selection moved, nothing more
    ("up", r"\^", None),          # Up adds: 0:15
    ("hours", "h", None),
    ("eight", "8", None),         # the keypad's 8 adds: 1:15
    ("down", r"\v", None),        # Down subtracts: 0:15
    ("four", "4", None),          # the selection moved, nothing more
    ("rested", "r", "rest"),      # 15 minutes: the camp
    ("which", "S", None),
    ("saved", r"\>\>\r", None),   # Quit to DOS (DS:124c)
]
# A key waiting at the first tick: "Stop resting? " from Yes, Enter stops
# (the time left kept, then cleared by 0dee); again, No rests on.
STOP = [
    ("camp", r"\r\r\r", None),
    ("rest", "R", None),
    ("hours", "h", None),
    ("add", "a", None),
    ("stop", "rx", "ahead"),
    ("stopped", r"\r", None),
    ("again", "R", None),
    ("hours-again", "h", None),
    ("add-again", "a", None),
    ("stop-again", "rx", "ahead"),
    ("rested", "n", "rest"),
    ("which", "S", None),
    ("saved", r"\>\>\r", None),
]
# A day's rest: the encounter at the 255th tick, the rest vector's text.
INTERRUPTED = [
    ("camp", r"\r\r\r", None),
    ("rest", "R", None),
    ("days", "d", None),
    ("day", "a", None),
    ("interrupted", "r", "rest"),
    ("eulogy", r"\r", None),
]
# The eulogy's portrait (PIC 96) blinks: which frame a screenshot catches
# depends on the host's timing, not on the rest (36 pixels, its eyes, on
# the first run).
REST_MASKS = {"eulogy": [(56, 60, 36, 14, "the portrait's eyes: its animation's frame")]}


def spinning(box, seconds=0.3):
    """No poll of KeyPressed for SECONDS: the game spins in a trap."""
    polls = box.hook("polls")
    time.sleep(seconds)
    return box.hook("polls") == polls


def rest_steps(v, work, box, flow, steps, seed, saves):
    keys = K.LOAD_A_BEGIN
    port_saves, shot = None, None
    for name, typed, how in steps:
        if how == "ahead":
            handle = K.trap(box, *REST_POLL, lambda: True)
            box.push_key(cokbox.key_word(typed[0]))
            box._until(lambda: not box.buffer(), 60, "the menu to take the key", 0.01)
            box._until(lambda: spinning(box), 60, "the rest to reach its poll", 0.05)
            box.push_key(cokbox.key_word(typed[1]))
            K.untrap(box, handle)
            box.wait_input(settle=2.0)
            keys += typed[0] + r"\[" + typed[1] + r"\]"
        else:
            box.keys(typed)
            if how == "rest":
                box._until(lambda: box.ds_byte(DS_RESTING) == 0, 300, "the rest to end", 0.05)
            box.wait_input(settle=2.0)
            keys += typed
        shown = box.screenshot(work / f"dosbox-{flow}-{name}.png")
        _, shot, _, _ = K.port(work, f"{flow}-{name}", keys, seed, saves=saves)
        port_saves = work / f"port-{flow}-{name}" / "saves"
        v.screen(f"{flow}: {name}", shown, load(shot), masks=REST_MASKS.get(name, ()), work=work)
    return keys, port_saves, shot


def rest(v, work, opts, flow, steps):
    seed = opts.seed
    with cokbox.Box(workdir=work / f"box-{flow}", game="death-knights", machine="ega",
                    seed=seed, saves=[K.ASSETS / "SAVE"], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), f"the title's four pictures ({flow})")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        keys, port_saves, shot = rest_steps(v, work, box, flow, steps, seed, None)
        _, _, _, state = K.port(work, f"{flow}-end", keys, seed)
        v.value(f"{flow}: RandSeed", box.ds_dword(K.DS_RANDSEED),
                state["seed"] if state is not None else None)
        low, _ = K.box_vars(box)
        ported = K.port_vars(shot) if shot is not None else None
        v.value(f"{flow}: the clock (0x4bc6-0x4bcc)", list(low[0xC6:0xCD]),
                list(ported[0][0xC6:0xCD]) if ported is not None else None)
        original = work / f"box-{flow}" / "c" / "DKK" / "SAVE"
    if steps[-1][0] == "saved":
        compare_files(v, original, port_saves, flow)


def json_records(path):
    """The port's records whole (the spells' block after +0xd8) beside its
    final screen."""
    import json
    if not path.exists():
        return None
    return [bytes.fromhex(r["record"]) for r in json.loads(path.read_text())["records"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_levels"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="train,memorize,table,cast,fix,rest,stop,interrupted",
                        help="which flows: train, memorize, table, cast, fix, rest, stop, "
                        "interrupted (comma-separated)")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "train" in flows:
        train(v, work, opts)
    if "memorize" in flows:
        memorize(v, work, opts)
    if "table" in flows:
        table(v, work, opts)
    if "cast" in flows:
        cast(v, work, opts)
    if "fix" in flows:
        fix(v, work, opts)
    for flow, steps in (("rest", REST), ("stop", STOP), ("interrupted", INTERRUPTED)):
        if flow in flows:
            rest(v, work, opts, flow, steps)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())
