#!/usr/bin/env python3
"""Death Knights of Krynn's treasure and bank vault in the original against
eclplay --game death-knights --start-menu (DEVELOPERS.md, "Death Knights'
treasure"). Two flows, each from saved game A loaded and begun, the same
keys typed to both, the original's screen at each wait against the port's
at the key read the same keys leave it in:

treasure: block 16's first fight, left as dkk_fight.py leaves it (the
enemies' statuses written at the first turn, End, No; the port's --combat
won); then block 36: the walk to the temple, Yes to the healing and to each
cure, the commander's TREASURE ITEMS (ITEM0's ring, 3632:1933) and its
COMBAT: the results (the ring's experience), the treasure menu (3a37:0eed),
Take's list (3a37:0ce8), the ring taken by the first character
(3b89:033b), Exit, ADD EP for the party (3632:25f1); camp, Save Current
Game C. Saved game C against the port's but the bytes the original's
stack leaves and the minute of the round the original fought (var
0x4bc7); its characters' files but their far pointers and, in the items
the treasure gave, the bytes after the name that TREASURE ITEMS' stack
left there.

vault: saved game A with its block (var 0x4bf2) 35, the bank of Gargath,
which Begin resumes in: its COMBAT opens the vault's menu (3948:000e, mode
7's screen 65cd:2db0); Pool, Take's coins (5 steel), Money's deposit
(2 steel, 580d:26d0), Exit: TMPVAULT.DAT (46b2:302d) byte for byte; then
camp, Save C: VAULTC.DAT, saved game C and its characters.

What the first fight's end leaves different (the original fought its
first turn and round, --combat won none) is masked and said (MASKS), and
so is camp's forget of the spells cast (4381:05b0 keeps them).

Seeded (Box seed, eclplay --seed). In run_all.py's Death Knights set (make dkk-checks). About a
minute, the two flows side by side there.

    python3 tools/dosbox/checks/dkk_treasure.py [--workdir DIR] [--seed N]
        [--flows treasure,vault]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402
import dkk_start  # noqa: E402

cokbox = checklib.cokbox

# After Begin: two texts, No rest, the festival's eight texts to "THE
# BATTLE IS JOINED.", and past it (the leadership's screen, if any, after).
TO_COMBAT = K.LOAD_A_BEGIN + r"\r\rN" + r"\r" * 9
# After the fight's results: the steps to the treasure and the save, each
# (the screen reached, the keys typed at the wait before it).
TREASURE = [
    ("defeated", r"\r"),     # WITH THE ENEMY DEFEATED ...
    ("agony", r"\r"),        # block 36: MANY ARE IN AGONY. HELP WITH HEALING?
    ("cure-1", r"\r"),       # Yes: WILL TANAR CAST CURE LIGHT WOUNDS?
    ("cast-1", r"\r"),       # YOU CAST THE SPELL.
    ("healed-1", r"\r"),     # A YOUNG WOMAN IS HEALED!
    ("cure-2", r"\r"),       # WILL FLAVIUS CAST CURE BLINDNESS?
    ("cast-2", r"\r"),
    ("healed-2", r"\r"),
    ("commander", r"\r"),    # THE OUTPOST COMMANDER WALKS UP.
    ("willed", r"\r"),       # 'WE THANK YOU. AS HE DIED, A KNIGHT WILLED THIS TO YOU.'
    ("found", r"\r"),        # TREASURE ITEMS, COMBAT: the results
    ("menu", r"\r"),         # the treasure menu: View Take Pool Exit
    ("take", r"\>\r"),       # Take: the pool's items, Ring
    ("taken", r"\r"),        # taken by the first: View Pool Exit
    ("left", r"\>\r"),       # Exit; ADD EP; AFTER YOU REST ...: the adventure's menu
    ("camp", "E"),
    ("which", "S"),
    ("saved", r"\>\>\r"),    # Save Current Game C
]
# What the first fight's end leaves different, the original having fought
# its first turn and round where --combat won fights none (dkk_fight.py:
# the variables but the clock's minute agree at its results): the clock's
# minute on the status line; RANDOM after it (the round's dice); the
# selection, the actor of the original's first turn (NEACAIL), which the
# block's EXIT restores; each record's attacks left (+0xce, the round's
# start). And camp's forget: the original's (4381:05b0) clears only
# scrolls' marks, the port's (Champions' 4888) also the spells cast
# (memorized with bit 7): the healers' cures, cast before the save.
CLOCK = (216, 120, 8, 8, "the clock's minute: the original fought the first fight's round")
HEALED = (72, 136, 40, 8, "RANDOM 4's man, woman or child: the original's round rolled more")
SELECTED = (136, 64, 56, 8, "the selection, the original's first turn's actor (NEACAIL)")
MASKS = {"healed-1": [CLOCK, HEALED], "left": [CLOCK, SELECTED], "camp": [CLOCK, SELECTED],
         "which": [CLOCK, SELECTED], "saved": [CLOCK, SELECTED]}
for _name in ("agony", "cure-1", "cast-1", "cure-2", "cast-2", "healed-2", "commander", "willed"):
    MASKS[_name] = [CLOCK]

VAULT = [
    ("pooled", r"\>\r"),     # Pool: View Take Pool Items Exit
    ("coins", r"\r"),        # Take: the coins
    ("how-much", r"\r"),     # How much Steel  will you take?
    ("took", r"5\r"),
    ("back", r"\e"),
    ("deposit", r"\>\>\r"),  # Money: the character's coins
    ("deposit-how", r"\r"),  # How much Steel will you deposit?
    ("deposited", r"2\r"),
    ("back-2", r"\e"),
    ("left", r"\>\>\r"),     # Exit: TMPVAULT.DAT; block 36's start; the adventure's menu
    ("camp", "E"),
    ("which", "S"),
    ("saved", r"\>\>\r"),
]


def load(path):
    return cokbox.Screen.load(path) if path is not None else None


def item_names(data):
    """The bytes of each 63-byte item after its name (+0, a string[40]),
    which the original leaves as they were."""
    out = []
    for k in range(0, len(data) - 62, 63):
        n = min(data[k], 40)
        out.append((k + 1 + n, k + 0x29))
    return out


def compare_files(v, what, original, port, explained_items=False, fought=False):
    """Saved game C and its characters: the saved game but the stack's bytes
    and the clock's minute (var 0x4bc7, byte 1 + 0xc7), the characters but
    their far pointers (dkk_start.masked_character) and, with
    EXPLAINED_ITEMS, an item's bytes after its name; with FOUGHT, a
    record's attacks left (+0xce) and a spell memorized cast (bit 7) in the
    original where the port's is none (see MASKS)."""
    a, b = bytearray((original / "SAVGAMC.PTY").read_bytes()), \
        bytearray((port / "SAVGAMC.PTY").read_bytes())
    minute = (a[1 + 0xC7], b[1 + 0xC7])
    a[1 + 0xC7] = b[1 + 0xC7]
    v.check(dkk_start.same_saved(bytes(a), bytes(b)),
            f"{what}: saved game C (but the stack's bytes; the minute 0x4bc7 original "
            f"{minute[0]}, port {minute[1]})")
    names = sorted(p.name for p in original.iterdir() if p.name.startswith("CHRDATC"))
    v.value(f"{what}: the character files", names,
            sorted(p.name for p in port.iterdir() if p.name.startswith("CHRDATC")))
    for n in names:
        x = bytearray(dkk_start.masked_character(n, (original / n).read_bytes()))
        y = bytearray(dkk_start.masked_character(n, (port / n).read_bytes()))
        tails = []
        if fought and n.upper().endswith(".GAM") and len(x) == len(y) and x[0xCE] != y[0xCE]:
            tails.append(f"attacks left {x[0xCE]}, {y[0xCE]}")
            x[0xCE] = y[0xCE]
        if fought and n.upper().endswith(".WIZ") and len(x) == len(y):
            for k in range(0x4A):
                if x[k] != y[k] and x[k] >= 0x80 and y[k] == 0:
                    tails.append(f"spell {x[k]:#04x} cast at {k}")
                    x[k] = y[k]
        if explained_items and n.upper().endswith(".JNK") and len(x) == len(y):
            for start, end in item_names(x):
                if x[start:end] != y[start:end]:
                    tails.append(f"the stack's bytes after item {start // 63 + 1}'s name")
                    x[start:end] = y[start:end]
        same = x == y
        diff = [k for k in range(min(len(x), len(y))) if x[k] != y[k]][:8]
        v.check(same, f"{what}: {n}" + (f" (explained: {', '.join(tails)})" if tails else "")
                + ("" if same else f" differs at {[hex(k) for k in diff]} "
                                   f"(sizes {len(x)}, {len(y)})"))


def still_shot(box, path, tries=5):
    """The screen once the original waits with it drawn: the shot again
    after another wait until two agree. A text's delay polls the keys as a
    menu does (Box.waiting), and with turbo the settle's game time can pass
    in the block's load that follows ("LOADING...PLEASE WAIT")."""
    shown = box.screenshot()
    for _ in range(tries):
        box.wait_input(settle=1.0)
        again = box.screenshot()
        if again.diff(shown)[0] == 0:
            break
        shown = again
    return box.screenshot(path)


def run_steps(v, work, box, flow, steps, prefix, seed, saves, args=()):
    """Each step's keys to the box, then its screen against the port's on the
    keys so far (PREFIX the port's before them). Returns the last step's
    port saves folder."""
    keys = prefix
    port_saves = None
    for name, typed in steps:
        keys += typed
        K.type_steps(box, [typed], settle=2.0)
        shown = still_shot(box, work / f"dosbox-{flow}-{name}.png")
        log, shot, _, _ = K.port(work, f"{flow}-{name}", keys, seed, saves=saves, args=args)
        port_saves = work / f"port-{flow}-{name}" / "saves"
        v.screen(f"{flow}: {name}", shown, load(shot), masks=MASKS.get(name, ()) if flow ==
                 "treasure" else (), work=work)
    return port_saves


def treasure(v, work, opts):
    seed = opts.seed
    log, _, _, _ = K.port(work, "treasure-lead", TO_COMBAT, seed)
    led = "Using leadership ability" in log
    to_turn = TO_COMBAT + (r"\r" if led else "")
    log, results, _, _ = K.port(work, "treasure-results", to_turn, seed, args=["--combat", "won"])
    print(f"port: leadership {'shown' if led else 'not shown'}; results "
          f"{'reached' if results else 'not reached'}", flush=True)
    with cokbox.Box(workdir=work / "box-treasure", game="death-knights", machine="ega",
                    seed=seed, saves=[K.ASSETS / "SAVE"], verbose=opts.verbose, timeout=1500,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures")
        K.type_steps(box, K.LOAD_A_STEPS + [r"\r", r"\r", "N"] + [r"\r"] * 8)
        K.type_steps(box, [r"\r"] * (2 if led else 1), settle=2.0)
        v.check(box.ds_byte(K.DS_MODE) == 5, "the original waits in the first fight's turn")
        for r in K.party(box):
            if r["raw"][K.SIDE] == 1:
                box.write(r["rec"] + K.STATUS, bytes([6, 0]))
        K.type_steps(box, ["E"], settle=2.0)
        K.type_steps(box, ["N"], settle=2.0)
        shown = box.screenshot(work / "dosbox-treasure-results.png")
        v.screen("treasure: the fight's results", shown, load(results), work=work)
        port_saves = run_steps(v, work, box, "treasure", TREASURE, to_turn, seed, None,
                               args=["--combat", "won"])
        original = work / "box-treasure" / "c" / "DKK" / "SAVE"
    compare_files(v, "treasure", original, port_saves, explained_items=True, fought=True)


def vault(v, work, opts):
    seed = opts.seed
    saves = work / "saves-vault"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    game = bytearray((saves / "SAVGAMA.PTY").read_bytes())
    game[1 + 0xF2] = 35  # var 0x4bf2, the block: byte 0 the ECL file, then 0x4b00-0x4eff
    (saves / "SAVGAMA.PTY").write_bytes(bytes(game))
    _, begun, _, _ = K.port(work, "vault-begun", K.LOAD_A_BEGIN, seed, saves=saves)
    with cokbox.Box(workdir=work / "box-vault", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures (vault)")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        shown = box.screenshot(work / "dosbox-vault-begun.png")
        v.check(box.ds_byte(K.DS_MODE) == 7, "the original waits in the vault's menu (mode 7)")
        v.screen("vault: its menu", shown, load(begun), work=work)
        keys = K.LOAD_A_BEGIN
        port_saves = None
        original = work / "box-vault" / "c" / "DKK" / "SAVE"
        for name, typed in VAULT:
            keys += typed
            K.type_steps(box, [typed], settle=2.0)
            shown = still_shot(box, work / f"dosbox-vault-{name}.png")
            _, shot, _, _ = K.port(work, f"vault-{name}", keys, seed, saves=saves)
            port_saves = work / f"port-vault-{name}" / "saves"
            v.screen(f"vault: {name}", shown, load(shot), work=work)
            if name == "left":
                a = (original / "TMPVAULT.DAT").read_bytes() \
                    if (original / "TMPVAULT.DAT").exists() else None
                b = (port_saves / "TMPVAULT.DAT").read_bytes() \
                    if (port_saves / "TMPVAULT.DAT").exists() else None
                v.value("vault: TMPVAULT.DAT", a.hex() if a else None, b.hex() if b else None)
    a, b = (original / "VAULTC.DAT").read_bytes(), (port_saves / "VAULTC.DAT").read_bytes()
    v.value("vault: VAULTC.DAT", a.hex(), b.hex())
    compare_files(v, "vault", original, port_saves)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_treasure"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="treasure,vault",
                        help="which flows: treasure, vault (comma-separated)")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "treasure" in flows:
        treasure(v, work, opts)
    if "vault" in flows:
        vault(v, work, opts)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())
