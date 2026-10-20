#!/usr/bin/env python3
"""Death Knights of Krynn's first fight in the original against eclplay
--game death-knights (DEVELOPERS.md, "Death Knights"): saved game A loaded
from the start menu, Begin Adventuring, block 16's texts (No rest) to
COMBAT; the leadership's screen where a party knight leads the Solamnic
knights (5340:4acf); the battlefield (410b:1e41: open ground, 410b:107e)
and its combatants at the first turn, where the original waits in the
player's menu (3e05:08af) and the port too: the screen (its menu rows and
the actor's cursor left to the keys flows), the records (hit points, status, side, the computer's,
effects, combat records), the combatants' cells and sizes, the turn
order, the battlefield's cells and RandSeed, which says every die was
rolled alike. Then out of the fight with the statuses written as
eclplay's --combat won writes them (every enemy dead): the original's
turn ended (End) and the battle with it, against the port's --combat won,
the results' screen and the records and variables after. And the fight
again on ground with a river (var 0x4bf3 set in a copy of the saved game,
410b:0a4c): its battlefield at the first turn; and with two ambushes
(DS:c4cc poked once SETUP MONSTERS has run, eclplay's build taking
COK_DKK_AMBUSH) whose last enemy faces other than the party, so that the
knights after the enemies are placed by the enemy's side (410b:18c2):
placement and combatants. And the first fight with the party put on Auto
(Alt-Q at the first turn; eclplay --combat auto) to its end: the original
stopped at each round's end (3e05:0ed0 made to spin) against the port's
state there, then "continue battle?" and the results. And the first fight
by keys from its first turn (keys, keys2: the menu's rows, Move, Aim and
its cursor, Speed, Delay, Guard, End, Quick, Yell, an attack by Aim and
Target; items: View and Use): the screen at each wait, the state after. And the Auto fight with
casters (a copy of saved game A whose spells memorized are Magic Missile,
Lightning Bolt and Ice Storm for FLAVIUS, Flame Strike and Cure Light
Wounds for TANAR, Shocking Grasp and Enlarge for CELESTE, Cure Light
Wounds for NEACAIL) and Magic On (Alt-M after Alt-Q): the computer's
choice (3f63:056c, 5340:43ea), its targets (5340:1eeb), the cast
(5dc5:1298, its burst 65cd:39bd), the spells and the damage's words,
round by round. And the spells flow: a copy of saved game A whose
casters memorize band B's spells (5dc5's handlers), cast by keys over the
first two rounds (Shield, Dispel Magic on the shielded CELESTE, Fireball,
Bless, Haste, Lightning Bolt), the screen, records and RandSeed where the
keys end, then Auto with Magic Off, round by round. Seeded (Box seed,
eclplay --seed): the same dice. Not part of run_all.py's set. About
thirty minutes, and the spells flow fifteen more.

    python3 tools/dosbox/checks/dkk_fight.py [--workdir DIR] [--seed N]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402

cokbox = checklib.cokbox

# After Begin: two texts, No rest, the festival's eight texts to "THE
# BATTLE IS JOINED.", and past it.
TO_JOINED = K.LOAD_A_BEGIN + r"\r\rN" + r"\r" * 8
TO_COMBAT = TO_JOINED + r"\r"
# The player's menu's rows (23-24), compared by the keys flows.
MENU_ROWS = (0, 184, 320, 16, "the player's menu (3e05:0ccc), compared by the keys flows")


def load(path):
    return cokbox.Screen.load(path) if path is not None else None


def waiting(run):
    """K.port's result with the screen and state where the keys ran out
    (the key read the port waits at, as the original does), not where the
    run ends: (log, shot, None, its state)."""
    log, shot, _, _ = run
    return log, shot, None, K.read_state(shot) if shot is not None else None


def views(cs):
    return K.fighters(cs)


def masked(combat):
    """A combat record with its target pointer (+0x0a-0x0d) left out."""
    return None if combat is None else combat[:20] + combat[28:]


def compare_records(v, what, original, port):
    o = [(r["name"], r["hp"], r["status"], r["raw"][K.SIDE], r["raw"][K.COMPUTER], r["effects"])
         for r in original]
    p = [(r["name"], r["hp"], r["status"], r["raw"][K.SIDE], r["raw"][K.COMPUTER], r["effects"])
         for r in port]
    v.value(what, o, p)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_fight"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="fight,river,ambush,auto,keys,keys2,cast,items,spells",
                        help="which flows: fight, river, ambush, auto, keys, keys2, cast, items, "
                             "spells (comma-separated)")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir)
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "fight" in flows:
        fight(v, work, opts)
    if "river" in flows:
        river(v, work, opts)
    if "ambush" in flows:
        for bits in AMBUSHES:
            ambush(v, work, opts, bits)
    if "auto" in flows:
        auto(v, work, opts)
    if "keys" in flows:
        by_keys(v, work, opts)
    if "keys2" in flows:
        by_keys(v, work, opts, KEYS2, "keys2")
    if "cast" in flows:
        auto(v, work, opts, cast=True)
    if "items" in flows:
        by_keys(v, work, opts, ITEMS, "items")
    if "spells" in flows:
        auto(v, work, opts, cast=True, tag="spells", spells=SPELLS, first=SPELLS_FIRST,
             magic=False)
    return v.finish()


def fight(v, work, opts):
    """The first fight: to the first turn, then out."""

    # The port: its screen where the keys run out ("THE BATTLE IS JOINED.",
    # the leadership's wait) and its stop at the first turn, with its
    # state; and --combat won's results.
    _, joined, _, _ = K.port(work, "joined", TO_JOINED, opts.seed)
    log, lead, _, _ = K.port(work, "lead", TO_COMBAT, opts.seed)
    led = "Using leadership ability" in log
    to_turn = TO_COMBAT + (r"\r" if led else "")
    log, final, _, state = waiting(K.port(work, "turn", to_turn, opts.seed))
    print(f"port: leadership {'shown' if led else 'not shown'}; "
          f"{log.strip().splitlines()[-1]}", flush=True)
    won_log, won, _, won_state = K.port(work, "won", to_turn, opts.seed, args=["--combat", "won"])
    print(f"port --combat won: {won_log.strip().splitlines()[-1]}", flush=True)
    with cokbox.Box(workdir=work / "box", game="death-knights", machine="ega", seed=opts.seed,
                    saves=[K.ASSETS / "SAVE"], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures")
        K.type_steps(box, K.LOAD_A_STEPS + [r"\r", r"\r", "N"] + [r"\r"] * 8)
        shown = box.screenshot(work / "dosbox-joined.png")
        v.screen("THE BATTLE IS JOINED.", shown, load(joined), work=work)
        K.type_steps(box, [r"\r"], settle=2.0)
        if led:
            shown = box.screenshot(work / "dosbox-leadership.png")
            v.screen("leadership", shown, load(lead), work=work)
            K.type_steps(box, [r"\r"], settle=2.0)
        shown = box.screenshot(work / "dosbox-turn.png")
        v.check(box.ds_byte(K.DS_MODE) == 5, "the original waits in a battle (mode 5)")
        if state is None:
            v.differ("the port's state at its stop", "none written")
            return
        # The actor's cell: the player's turn draws its cursor there.
        view = box.read(box.far(box.ds * 16 + K.DS_MAP) + 2, 2)
        actor = next(c for c in K.port_combatants(state)
                     if c["name"] == state["records"][state["turn"][state["turn_index"] - 1]]["name"])
        cursor = (8 + 24 * (actor["x"] - view[0]), 8 + 24 * (actor["y"] - view[1]), 24, 24,
                  "the actor's cursor (3e05:08af), compared by the keys flows")
        v.screen("the battlefield at the first turn", shown, cokbox.Screen.load(final),
                 masks=[MENU_ROWS, cursor], work=work)
        o = K.combatants(box)
        p = K.port_combatants(state)
        v.value("the combatants (n, name, cell, size, hp, status, side, computer, effects)",
                [f[:10] for f in views(o)], [f[:10] for f in views(p)])
        v.value("their combat records (but +0x0a-0x0d)",
                [masked(c["combat"].hex()) if c["combat"] else None for c in o],
                [masked(c["combat"].hex()) if c["combat"] else None for c in p])
        names, entry = K.order(box)
        v.value("the turn order", names, [state["records"][i]["name"] for i in state["turn"]])
        v.value("the entry whose turn it is", entry, state["turn_index"])
        v.value("the round", box.ds_byte(K.DS_ROUND), state["round"])
        v.value("the battlefield's cells", [c.hex() for c in K.cells(box) or []],
                [c.hex() for c in state["cells"]])
        v.value("RandSeed", box.ds_dword(K.DS_RANDSEED), state["seed"])
        compare_records(v, "the records at the first turn", K.party(box), state["records"])

        # Out: every enemy dead, as --combat won writes them, then End.
        for r in K.party(box):
            if r["raw"][K.SIDE] == 1:
                box.write(r["rec"] + K.STATUS, bytes([6, 0]))
        # End: the round ends after the turn, the enemy gone (3e05:00ec),
        # and its end asks "continue battle?" (3e05:0ed0): No.
        K.type_steps(box, ["E"], settle=2.0)
        box.screenshot(work / "dosbox-continue.png")
        K.type_steps(box, ["N"], settle=2.0)
        shown = box.screenshot(work / "dosbox-results.png")
        v.screen("the results (the original's turn ended) against --combat won's", shown,
                 load(won), work=work)
        mine = K.port_vars(won) if won is not None else None
        if mine is None:
            v.differ("the port's variables at the results", "none written")
        else:
            theirs = K.box_vars(box)
            # The original fought a round (its turn, then the round's end
            # passes a minute: dkk 5b7d:0523, var 0x4bc7), which --combat
            # won does not: the port's clock a minute behind, explained.
            low = bytearray(mine[0])
            low[0xC7] = (low[0xC7] + 1) & 0xFF
            print(f"  explained: the clock's minute 0x4bc7, original {theirs[0][0xC7]}, port "
                  f"{mine[0][0xC7]} (the round the original fought)", flush=True)
            for (base, a, b) in ((0x4B00, theirs[0], bytes(low)), (0x7C00, theirs[1], mine[1])):
                v.value(f"the variables {base:04x}-{base + 0x3ff:04x} at the results",
                        [f"{base + k:04x}={a[k]:02x}" for k in range(0x400) if a[k] != b[k]],
                        [f"{base + k:04x}={b[k]:02x}" for k in range(0x400) if a[k] != b[k]])
        if won_state is not None:
            records = K.party(box)
            v.value("the party's experience after", [(r["name"], r["raw"][0x62:0x66].hex(),
                                                       r["raw"][0xC6]) for r in records],
                    [(r["name"], r["raw"][0x62:0x66].hex(), r["raw"][0xC6])
                     for r in won_state["records"]])



# The ambushes (SETUP MONSTERS' fourth operand, DS:c4cc, poked once it has
# run): ways 1-3 and 0, 1, 3. Each leaves the last enemy facing other than
# the party (S, W), so that the Solamnic knights after the enemies are laid
# out again and placed by the enemy's side (410b:18c2 leaves DS:86e8 1).
AMBUSHES = (0x0E, 0x0B)


def ambush(v, work, opts, bits):
    """The first fight with an ambush from BITS' directions: the battlefield
    at the first turn, the combatants' cells, their combat records and
    RandSeed."""
    tag = f"ambush-{bits:x}"
    log, _, _, _ = K.port(work, f"{tag}-lead", TO_COMBAT, opts.seed, ambush=bits)
    led = "Using leadership ability" in log
    to_turn = TO_COMBAT + (r"\r" if led else "")
    log, final, _, state = waiting(K.port(work, f"{tag}-turn", to_turn, opts.seed, ambush=bits))
    print(f"port ({tag}): {log.strip().splitlines()[-1]}", flush=True)
    with cokbox.Box(workdir=work / f"box-{tag}", game="death-knights", machine="ega",
                    seed=opts.seed, saves=[K.ASSETS / "SAVE"], verbose=opts.verbose,
                    timeout=900, keep=True) as box:
        v.check(K.title(box), f"the title's four pictures ({tag})")
        K.type_steps(box, K.LOAD_A_STEPS + [r"\r", r"\r", "N"] + [r"\r"] * 8)
        box.ds_write(0xC4CC, bytes([bits]))  # SETUP MONSTERS has run (8603)
        K.type_steps(box, [r"\r"] * (2 if led else 1), settle=2.0)
        shown = box.screenshot(work / f"dosbox-{tag}-turn.png")
        if state is None:
            v.differ(f"the port's state at its stop ({tag})", "none written")
            return
        view = box.read(box.far(box.ds * 16 + K.DS_MAP) + 2, 2)
        actor = next(c for c in K.port_combatants(state)
                     if c["name"] == state["records"][state["turn"][state["turn_index"] - 1]]["name"])
        cursor = (8 + 24 * (actor["x"] - view[0]), 8 + 24 * (actor["y"] - view[1]), 24, 24,
                  "the actor's cursor (3e05:08af), compared by the keys flows")
        v.screen(f"the battlefield at the first turn ({tag})", shown, cokbox.Screen.load(final),
                 masks=[MENU_ROWS, cursor], work=work)
        o, p = K.combatants(box), K.port_combatants(state)
        v.value(f"the combatants' cells and sizes ({tag})", [f[:5] for f in K.fighters(o)],
                [f[:5] for f in K.fighters(p)])
        v.value(f"their combat records but +0x0a-0x0d ({tag})",
                [masked(c["combat"].hex()) if c["combat"] else None for c in o],
                [masked(c["combat"].hex()) if c["combat"] else None for c in p])
        v.value(f"RandSeed ({tag})", box.ds_dword(K.DS_RANDSEED), state["seed"])


def river(v, work, opts):
    """The same fight on open ground with a river (var 0x4bf3 set in a
    copy of saved game A: dkk 410b:0a4c): the battlefield at the first turn,
    its cells and RandSeed."""
    saves = work / "saves-river"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    game = bytearray((saves / "SAVGAMA.PTY").read_bytes())
    game[1 + 0xF3] = 1  # byte 0 the ECL file, then 0x4b00-0x4eff
    (saves / "SAVGAMA.PTY").write_bytes(bytes(game))
    log, lead, _, _ = K.port(work, "river-lead", TO_COMBAT, opts.seed, saves=saves)
    led = "Using leadership ability" in log
    to_turn = TO_COMBAT + (r"\r" if led else "")
    log, final, _, state = waiting(K.port(work, "river-turn", to_turn, opts.seed, saves=saves))
    print(f"port (river): {log.strip().splitlines()[-1]}", flush=True)
    with cokbox.Box(workdir=work / "box-river", game="death-knights", machine="ega",
                    seed=opts.seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures (river)")
        K.type_steps(box, K.LOAD_A_STEPS + [r"\r", r"\r", "N"] + [r"\r"] * 9, settle=1.5)
        if led:
            K.type_steps(box, [r"\r"], settle=2.0)
        shown = box.screenshot(work / "dosbox-river-turn.png")
        if state is None:
            v.differ("the port's state at its stop (river)", "none written")
            return
        view = box.read(box.far(box.ds * 16 + K.DS_MAP) + 2, 2)
        actor = next(c for c in K.port_combatants(state)
                     if c["name"] == state["records"][state["turn"][state["turn_index"] - 1]]["name"])
        cursor = (8 + 24 * (actor["x"] - view[0]), 8 + 24 * (actor["y"] - view[1]), 24, 24,
                  "the actor's cursor (3e05:08af), compared by the keys flows")
        v.screen("the battlefield with a river at the first turn", shown,
                 cokbox.Screen.load(final), masks=[MENU_ROWS, cursor], work=work)
        cells = K.cells(box) or []
        v.check(any(0x34 in row for row in cells), "a river on the original's battlefield")
        v.value("the river's battlefield's cells", [c.hex() for c in cells],
                [c.hex() for c in state["cells"]])
        v.value("RandSeed (river)", box.ds_dword(K.DS_RANDSEED), state["seed"])
        v.value("the combatants (river)", [f[:5] for f in K.fighters(K.combatants(box))],
                [f[:5] for f in K.fighters(K.port_combatants(state))])


def round_over(box):
    """Whether the original's round has no turn left: the entry whose turn
    it is (DS:c5f5) holds none, or a side has none that can act
    (DS:be68, be69), as 3e05:00ec ends a round."""
    entry = box.ds_byte(K.DS_ENTRY)
    left = box.far(box.ds * 16 + K.DS_ORDER + 4 * entry) if 0 < entry < 73 else None
    return left is None or box.ds_byte(K.DS_SIDES) == 0 or box.ds_byte(K.DS_SIDES + 1) == 0


def compare_round(v, box, tag, shot, state, work):
    """The original, trapped at a round's end, against the port's state and
    screen at its mark there."""
    o, p = K.combatants(box), K.port_combatants(state)
    v.value(f"the combatants ({tag}: n, name, cell, size, hp, status, side, computer, effects)",
            [f[:10] for f in K.fighters(o)], [f[:10] for f in K.fighters(p)])
    v.value(f"their combat records but +0x0a-0x0d ({tag})",
            [masked(c["combat"].hex()) if c["combat"] else None for c in o],
            [masked(c["combat"].hex()) if c["combat"] else None for c in p])
    compare_records(v, f"the records ({tag})", K.party(box), state["records"])
    names, entry = K.order(box)
    v.value(f"the turn order ({tag})", names, [state["records"][i]["name"] for i in state["turn"]])
    v.value(f"the round ({tag})", box.ds_byte(K.DS_ROUND), state["round"])
    v.value(f"RandSeed ({tag})", box.ds_dword(K.DS_RANDSEED), state["seed"])
    shown = box.screenshot(work / f"dosbox-{tag}.png")
    v.screen(f"the screen ({tag})", shown, cokbox.Screen.load(shot), work=work)


# The casters' spells memorized for the cast flow (their .WIZ's first 0x4a
# bytes): FLAVIUS Magic Missile, Lightning Bolt, Ice Storm; TANAR Flame
# Strike, Cure Light Wounds; CELESTE Shocking Grasp (a touch: none in reach
# at first), Enlarge; NEACAIL Cure Light Wounds twice.
CASTERS = {"CHRDATA3": [0x4A, 0x03], "CHRDATA4": [0x0F, 0x33, 0x57],
           "CHRDATA5": [0x03, 0x03], "CHRDATA6": [0x14, 0x0C]}


# The spells flow's (band B, 5dc5's handlers), each cast by keys: FLAVIUS
# Fireball and Lightning Bolt twice, CELESTE Shield and Haste twice, NEACAIL
# Dispel Magic and Bless, TANAR Bless twice; its keys from the first turn
# (SPELLS_FIRST, each once the original waits and stands still), round 1:
# NEACAIL begins Dispel Magic, the second of her list (C, Down twice,
# Enter); CELESTE casts Shield (C, Down, Enter); SIR DRYDEN guards;
# NEACAIL's Dispel goes off on the first target, the cell where CELESTE
# stands shielded (T); the Solamnic knight guards; TANAR begins Bless;
# LILLITH guards; FLAVIUS begins Fireball, which goes off on the second
# target, two nightmares (N, T); TANAR's Bless on the first (T). Round 2:
# NEACAIL guards; CELESTE begins Haste; LILLITH guards; the Haste on the
# first target (T); three guard; FLAVIUS begins Lightning Bolt, which goes
# off on the first target, a nightmare (T). Then Alt-Q, Magic Off: the
# computer's turns cast nothing (its spell choice, 3f63:056c, is the cast
# flow's).
SPELLS = {"CHRDATA3": [0x01, 0x01], "CHRDATA4": [0x2F, 0x33, 0x33],
          "CHRDATA5": [0x29, 0x01], "CHRDATA6": [0x13, 0x30, 0x30]}
SPELLS_FIRST = (r"C\#50\#50\rC\#50\rGTGC\#50\rGC\#50\rNTT"  # round 1
                r"GC\#50\#50\rGTGGGC\#50\rT")  # round 2


def casters(work, spells=None, tag="cast"):
    """A copy of DKKAssets/SAVE with SPELLS' (by default CASTERS') spells
    memorized."""
    saves = work / f"saves-{tag}"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    for name, spells in (CASTERS if spells is None else spells).items():
        wiz = bytearray((saves / f"{name}.WIZ").read_bytes())
        wiz[:0x4A] = bytes(spells) + bytes(0x4A - len(spells))
        (saves / f"{name}.WIZ").write_bytes(bytes(wiz))
    return saves


def auto(v, work, opts, cast=False, tag=None, spells=None, first="", magic=True):
    """The first fight with the party put on Auto (Alt-Q at the first
    turn, the player's menu, dkk 3e05:08af), to its end: the original is
    stopped at each round's end (3e05:0ed0 made to spin) and read, against
    the port's state at the same place (dkk_lib.round_mark): the
    combatants, their records and combat records, the turn order, the
    round, RandSeed and the screen; then "continue battle?" once the enemy
    is gone (No) and the results, screens and records. With cast, the
    casters' saved game (SPELLS', or CASTERS') and Magic On: Alt-M typed
    ahead after Alt-Q, which the first computer's turn reads (3f63:117c).
    FIRST: keys typed from the first turn before Alt-Q, each once the
    original waits and stands still (menu_keys); without MAGIC, no Alt-M."""
    tag = tag or ("cast" if cast else "auto")
    saves = casters(work, spells, tag) if cast else K.ASSETS / "SAVE"
    log, _, _, _ = K.port(work, f"{tag}-lead", TO_COMBAT, opts.seed, saves=saves,
                          args=["--combat", "auto"])
    led = "Using leadership ability" in log
    keys = TO_COMBAT + (r"\r" if led else "")  # the leadership's wait
    # Cast: Alt-Q pressed at the first turn (after FIRST), Alt-M typed
    # ahead.
    args = [] if cast else ["--combat", "auto"]
    if cast:
        keys += first + (r"\@q\[\@m\]" if magic else r"\@q")
    log, asked, _, _ = K.port(work, tag, keys, opts.seed, saves=saves, args=args)
    rounds = K.port_rounds(work, tag)
    _, final, _, state = waiting(K.port(work, f"{tag}-end", keys + r"\r", opts.seed,
                                        saves=saves, args=args))
    print(f"port ({tag}): {len(rounds)} rounds; {log.strip().splitlines()[-1]}", flush=True)
    if cast:
        print("port (cast): " + "; ".join(line for line in log.splitlines()
                                          if line.startswith("cast: ")), flush=True)
    with cokbox.Box(workdir=work / f"box-{tag}", game="death-knights", machine="ega",
                    seed=opts.seed, saves=[saves], verbose=opts.verbose,
                    timeout=1800, keep=True) as box:
        v.check(K.title(box), f"the title's four pictures ({tag})")
        K.type_steps(box, K.LOAD_A_STEPS + [r"\r", r"\r", "N"] + [r"\r"] * 9, settle=1.5)
        if led:
            K.type_steps(box, [r"\r"], settle=2.0)
        v.check(box.ds_byte(K.DS_MODE) == 5, f"the original waits in the first turn ({tag})")
        # Combatant entry 0's y and index (DS:b58c, b58d), which the
        # battlefield's setup reads as whether its tile sets are loaded
        # (410b:10d7), as the port's combat.dungeon_tiles keeps it.
        tiles = box.ds_read(0xB58C, 2)
        if first:
            menu_keys(box, first)
            # The first keys' end against the port's wait there.
            _, at_first, _, mine = waiting(K.port(work, f"{tag}-first", keys[:len(keys) - len(
                r"\@q")] if not magic else keys[:len(keys) - len(r"\@q\[\@m\]")], opts.seed,
                saves=saves, args=args))
            v.screen(f"the screen after the first keys ({tag})", box.screenshot(
                work / f"dosbox-{tag}-first.png"), load(at_first), work=work)
            if mine is not None:
                compare_records(v, f"the records after the first keys ({tag})", K.party(box),
                                mine["records"])
                v.value(f"RandSeed after the first keys ({tag})", box.ds_dword(K.DS_RANDSEED),
                        mine["seed"])
        box.push_key(0x1000)  # Alt-Q
        if cast and magic:
            box.push_key(0x3200)  # Alt-M
        # The rounds the first keys played end untrapped: the port's from the
        # first the original stops at.
        n = box.ds_byte(K.DS_ROUND) if first else 0
        while True:
            handle = K.trap(box, 0x3E05, 0x0ED0, lambda: round_over(box), seconds=600)
            if not K.settled(box):
                v.differ("the original stopped at a round's end", "it did not settle")
                K.untrap(box, handle)
                return
            n += 1
            last = box.ds_byte(K.DS_SIDES) == 0 or box.ds_byte(K.DS_SIDES + 1) == 0 or \
                box.ds_byte(K.DS_ROUND) + 1 >= box.ds_byte(K.DS_LIMIT)
            if n > len(rounds):
                v.differ(f"round {n}'s end in the port", "the port's battle ended before")
                K.untrap(box, handle)
                break
            compare_round(v, box, f"{tag} round {n}" if cast else f"round {n}", *rounds[n - 1], work)
            K.untrap(box, handle)
            if last:
                break
        v.value("the rounds fought", n, len(rounds))
        v.value(f"DS:b58c, b58d kept through the fight ({tag})", tiles.hex(),
                box.ds_read(0xB58C, 2).hex())
        box.wait_input(seconds=120, settle=2.0)
        shown = box.screenshot(work / f"dosbox-{tag}-continue.png")
        if asked is not None:
            v.screen(f"continue battle? ({tag})", shown, cokbox.Screen.load(asked), work=work)
        K.type_steps(box, [r"\r"], settle=2.0)
        shown = box.screenshot(work / f"dosbox-{tag}-results.png")
        v.screen(f"the results ({tag})", shown, load(final), work=work)
        if state is not None:
            compare_records(v, f"the records at the results ({tag})", K.party(box),
                            state["records"])
            v.value(f"the party's experience after ({tag})",
                    [(r["name"], r["raw"][0x62:0x66].hex()) for r in K.party(box)],
                    [(r["name"], r["raw"][0x62:0x66].hex()) for r in state["records"]])


# The first fight by keys from its first turn (NEACAIL's, a kender with
# spells): each step's keys and the wait the original reaches after them.
KEYS = [
    ("menu", ""),             # the turn's menu (3e05:0ccc): two rows by index
    ("down", r"\#50"),        # Down: the second row (6a69:0c8c)
    ("right", r"\#4d"),       # Right: the next item
    ("up", r"\#48"),          # Up: back to the first row
    ("move", "M"),            # Move (3e05:10bb): "move/attack, move left = N", Done
    ("step", "2"),            # the keypad's 2: a step south
    ("undo", r"\e"),          # Escape: the steps undone (3e05:1001), the menu
    ("aim", "A"),             # Aim (5340:3888): the first target (36f2, 34b3, 2bee)
    ("next", "N"),
    ("prev", "P"),
    ("manual", "M"),          # the cursor (5340:2f3d)
    ("cursor", "8"),          # up a cell
    ("cursor-exit", r"\e"),   # Escape: the targets again, Manual selected
    ("center", "C"),
    ("aim-exit", "E"),        # Exit: the turn's menu
    ("speed", "S"),           # Speed (3e05:14aa)
    ("faster", "F"),
    ("speed-exit", "E"),
    ("delay", "D"),           # Delay: the next one's turn
    ("guard", "G"),           # it guards: the next
    ("end", "E"),             # End: the next
    ("quick", "Q"),           # Quick: the computer plays it; the next
]


# A second turn's worth: NEACAIL yells; the turns to round 2's SIR DRYDEN
# ended, who aims at the nightmare beside him and attacks it (5340:2dca);
# then TANAR's menu. (Cast's list of spells differs on row 24, Champions'
# "Choose Spell: " against Death Knights' DS:2fd9 with no space: the
# casting part's.) A
# step of keys "across" the computer's turns types each once the game
# waits for a key and stands still: the computer's turns poll the keyboard
# (3f63:117c), which would take a key typed early.
KEYS2 = [
    ("yell", "Y"),            # Yell (638c:0000): CELESTE's turn
    ("ended", "E" * 10, True),  # to round 2's SIR DRYDEN
    ("aim-2", "A"),
    ("next-2", "N"),          # the nightmare: Target
    ("target", "T", True),    # the attack, then the turns to TANAR's
]


# NEACAIL's View and Use at the first turn (580d:0c66, 0ffa in combat):
# View's menu (no Trade in combat), its Items, Escape twice back to the
# turn's menu; Use: the items menu from its first; Escape.
ITEMS = [
    ("view", "V"),
    ("view-items", "I"),
    ("view-items-exit", r"\e"),
    ("view-exit", r"\e"),
    ("use", "U"),
    ("use-exit", r"\e"),
]


def menu_keys(box, keys):
    """Type each of KEYS once the original waits for a key and its dice,
    round, turn and records have stood still for three seconds (the
    computer's turns between roll and move)."""
    for item in cokbox.parse_keys(keys):
        if item[0] != "key":
            continue
        for _ in range(60):
            box.wait_input()
            if K.settled(box, seconds=3.0, limit=4):
                break
        box.key(item[1])
    for _ in range(60):
        box.wait_input()
        if K.settled(box, seconds=3.0, limit=4):
            break
    box.wait_input(settle=1.0)


def by_keys(v, work, opts, keys=None, tag="keys"):
    """The first fight by keys from the first turn (KEYS): at each step the
    original's screen at its wait against the port's at the key read the
    same keys leave it in, and after the last the combatants, their records
    and combat records, the turn order, RandSeed, and DS:b58c-b58d (entry
    0's bytes that the battlefield's setup reads as its tiles loaded)."""
    steps = KEYS if keys is None else keys
    log, _, _, _ = K.port(work, f"{tag}-lead", TO_COMBAT, opts.seed)
    led = "Using leadership ability" in log
    to_turn = TO_COMBAT + (r"\r" if led else "")
    ports, typed = [], ""
    for name, keys, *_ in steps:
        typed += keys
        ports.append(waiting(K.port(work, f"{tag}-{name}", to_turn + typed, opts.seed)))
    print(f"port ({tag}): {ports[-1][0].strip().splitlines()[-1]}", flush=True)
    with cokbox.Box(workdir=work / f"box-{tag}", game="death-knights", machine="ega",
                    seed=opts.seed, saves=[K.ASSETS / "SAVE"], verbose=opts.verbose,
                    timeout=900, keep=True) as box:
        v.check(K.title(box), f"the title's four pictures ({tag})")
        K.type_steps(box, K.LOAD_A_STEPS + [r"\r", r"\r", "N"] + [r"\r"] * 9, settle=1.5)
        if led:
            K.type_steps(box, [r"\r"], settle=2.0)
        tiles = box.ds_read(0xB58C, 2)
        for (name, keys, *across), (_, shot, _, _) in zip(steps, ports):
            if across:
                menu_keys(box, keys)
            elif keys:
                K.type_steps(box, [keys], settle=2.0)
            shown = box.screenshot(work / f"dosbox-{tag}-{name}.png")
            v.screen(f"the screen after {name} ({tag})", shown, load(shot), work=work)
        state = ports[-1][3]
        if state is None:
            v.differ(f"the port's state after the keys ({tag})", "none written")
            return
        o, p = K.combatants(box), K.port_combatants(state)
        v.value(f"the combatants after the keys ({tag}: n, name, cell, size, hp, status, "
                "side, computer, effects)", [f[:10] for f in K.fighters(o)],
                [f[:10] for f in K.fighters(p)])
        v.value(f"their combat records but +0x0a-0x0d ({tag})",
                [masked(c["combat"].hex()) if c["combat"] else None for c in o],
                [masked(c["combat"].hex()) if c["combat"] else None for c in p])
        compare_records(v, f"the records after the keys ({tag})", K.party(box), state["records"])
        names, entry = K.order(box)
        v.value(f"the turn order ({tag})", names,
                [state["records"][i]["name"] for i in state["turn"]])
        v.value(f"the entry whose turn it is ({tag})", entry, state["turn_index"])
        v.value(f"RandSeed ({tag})", box.ds_dword(K.DS_RANDSEED), state["seed"])
        v.value(f"DS:b58c, b58d through the keys ({tag})", tiles.hex(), box.ds_read(0xB58C, 2).hex())


if __name__ == "__main__":
    sys.exit(main())
