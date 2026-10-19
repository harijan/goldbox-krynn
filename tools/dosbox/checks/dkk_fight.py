#!/usr/bin/env python3
"""Death Knights of Krynn's first fight in the original against eclplay
--game death-knights (DEVELOPERS.md, "Death Knights"): saved game A loaded
from the start menu, Begin Adventuring, block 16's texts (No rest) to
COMBAT; the leadership's screen where a party knight leads the Solamnic
knights (5340:4acf); the battlefield (410b:1e41: open ground, 410b:107e)
and its combatants at the first turn, where the original waits in the
player's menu (3e05:08af) and the port stops (its turn is not ported):
the screen, the records (hit points, status, side, the computer's,
effects, combat records), the combatants' cells and sizes, the turn
order, the battlefield's cells and RandSeed, which says every die was
rolled alike. Then out of the fight with the statuses written as
eclplay's --combat won writes them (every enemy dead): the original's
turn ended (End) and the battle with it, against the port's --combat won,
the results' screen and the records and variables after. And the fight
again on ground with a river (var 0x4bf3 set in a copy of the saved game,
410b:0a4c): its battlefield at the first turn. Seeded (Box seed, eclplay
--seed): the same dice. Not part of run_all.py's set. About seven
minutes.

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
# The rows the port's stop leaves as they were where the original's turn
# draws its menu (rows 23-24).
MENU_ROWS = (0, 184, 320, 16, "the player's menu (3e05:08af), not ported: the port stops first")


def load(path):
    return cokbox.Screen.load(path) if path is not None else None


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
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir)
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()

    # The port: its screen where the keys run out ("THE BATTLE IS JOINED.",
    # the leadership's wait) and its stop at the first turn, with its
    # state; and --combat won's results.
    _, joined, _, _ = K.port(work, "joined", TO_JOINED, opts.seed)
    log, lead, _, _ = K.port(work, "lead", TO_COMBAT, opts.seed)
    led = "Using leadership ability" in log
    to_turn = TO_COMBAT + (r"\r" if led else "")
    log, _, final, state = K.port(work, "turn", to_turn, opts.seed)
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
            return v.finish()
        # The actor's cell: the player's turn draws its cursor there.
        view = box.read(box.far(box.ds * 16 + K.DS_MAP) + 2, 2)
        actor = next(c for c in K.port_combatants(state)
                     if c["name"] == state["records"][state["turn"][state["turn_index"] - 1]]["name"])
        cursor = (8 + 24 * (actor["x"] - view[0]), 8 + 24 * (actor["y"] - view[1]), 24, 24,
                  "the actor's cursor, which the player's turn (3e05:08af) draws")
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
    river(v, work, opts)
    return v.finish()


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
    log, _, final, state = K.port(work, "river-turn", TO_COMBAT + (r"\r" if led else ""),
                                  opts.seed, saves=saves)
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
                  "the actor's cursor, which the player's turn (3e05:08af) draws")
        v.screen("the battlefield with a river at the first turn", shown,
                 cokbox.Screen.load(final), masks=[MENU_ROWS, cursor], work=work)
        cells = K.cells(box) or []
        v.check(any(0x34 in row for row in cells), "a river on the original's battlefield")
        v.value("the river's battlefield's cells", [c.hex() for c in cells],
                [c.hex() for c in state["cells"]])
        v.value("RandSeed (river)", box.ds_dword(K.DS_RANDSEED), state["seed"])
        v.value("the combatants (river)", [f[:5] for f in K.fighters(K.combatants(box))],
                [f[:5] for f in K.fighters(K.port_combatants(state))])


if __name__ == "__main__":
    sys.exit(main())
