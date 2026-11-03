#!/usr/bin/env python3
"""The Dark Queen of Krynn's end of combat with no monsters, from the treasure branch's start to
the experience (DARK-QUEEN.md's step 4, part 19a: dqk-end-of-combat; docs/checks.md, "Checks
against the original": dqk_treasure).

A test block (tools/dqkecl.py) replaces block 1 of a copy of DISK3\\ECL.GLB, and the original
runs it from a saved game (dqk_fight's enter: plta, Begin Adventuring, the copy protection
answered), with the seed set after the block's entry PRESS KEY:

    PRESS KEY; LOAD FILES 2 1 (the jumper's, as dqk_shops's temple block); COMPARE 1 1 (the flags
    = <> < > <= >= become 01 00 00 00 01 01); SAVE 1 -> var 0x2e; PRESS KEY; COMBAT; EXIT

COMBAT with no monsters, no vault and no temple wanted runs 75cc:135c: its treasure start walks the
lost weapons (1297, none here), clears var 0x2e, the flags = <> < > (2093:0015), and DS:c7df, then
0593 (the party's after combat) sets the party's flags and removes the effects that end with the
battle. When a member stands, 0593 runs 01fb (the experience: the pool's coins over the party) and
0427 (the award), then its rest from 75cc:0736 (all flows). The stop is at 0736, cited for part 19b.

The original is trapped in two stages (dqk_fight_lib): 2093:0015 (the flags' clear, in the root image,
so it is always resident) spins at the PRESS KEY before COMBAT's key; while it spins, overlay 8's
75cc:0736 is spun too (after 01fb and 0427 in `won`), then 2093:0015 is given back. Its state is read where the spin
holds: the 1 KB of variables, the records (pointers masked), the flags, DS:af3f, c7de, c7df, b1da,
b7a7, the mode (DS:ae57), RandSeed, and the 18 effect ids at DS:631e. Stage 1 also shows that
135c's start ran: var 0x2e is 0 at the 2093:0015 spin.

Flows (each its own saved game as the original starts it; the block's ops before the PRESS KEY
before COMBAT are its `extra`, and each member's record changes are its `changes`):
- `won`: saved game A as it is (its members standing, status 0): 01fb and 0427 run, then 0736.
- `fled`: every member's status word 3 (the others' records as A): 0736, fled set, b7a7 clear.
- `lost`: every member's status word 2 and inactive: 0736, nobody standing or fled, b7a7 set, the
  six members out.
- `pool`: TREASURE ITEMS 1000 3 1 0 first (the pool's coins 1000, 3, 1; no items): the share is
  4950 / 6 = 825, and each member gets its share by class (the bonus, the half and the third).
- `bonus`: `pool` with the classes changed so that each rule shows (+0x5a: 2, 0 at members 0 and 3,
  5 and 6 at 4 and 5): the 10% bonus, the mage with int 15 (none), the thief's dexterity.
- `zero`: SAVE 0 -> var 0x20 and member 5 inactive: 01fb's divisor is 0 (b1da 1 -> 0), so LDIV@ gives
  0xffffffff; member 0 (class 1) gets -1, the others 0 (-1/3 and -1/2 are 0).
- `negative`: `pool`, SAVE 0 -> var 0x20 and members 3-5 inactive: b1da 3 -> 2, the divisor -2, the
  share -2475; members 0-2 get -2475, -825 and -825 (the third of a negative share is negative).

eclplay runs the same block from the same saved game (--load, --play) and stops at its cited stop;
its state is --vars, --records and --state (the mode, RandSeed, the flags, and the end-of-combat
flags). Every flow compares them, and each original value is checked against the expectation the flow
sets up, so that a mistake in the flow shows.

    python3 tools/dosbox/checks/dqk_treasure.py [--workdir DIR] [--flows won,fled,lost] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import shutil
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_fight  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_modify  # noqa: E402
import dqk_shops  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm, var  # noqa: E402

cokbox = checklib.cokbox
JUMP = dqk_ecl.JUMP
SEED = dqk_ecl.SEED
VARIABLES = dqk_ecl.VARIABLES
SURPRISE = 0x2e  # var 0x2e: 75cc:135c's start clears it (COK_VAR_SURPRISE in rules_dark_queen.c)
MEMBERS = 6      # saved game A's party (dqk_modify.record_at)
EFFECTS = bytes([0x03, 0x0b, 0x15, 0x17, 0x1b, 0x23, 0x1f, 0x33, 0x34, 0x35,
                 0x78, 0x76, 0x6f, 0x6a, 0xb2, 0xb1, 0x95, 0x4d])  # DS:631e, 18 bytes
TREASURE = ("TREASURE_ITEMS", (dqkecl.word(1000), imm(3), imm(1), imm(0)))  # steel, gems, jewelry
SAVE_ZERO = ("SAVE", (imm(0), var(0x20)))  # var 0x20 (the shares' divisor) is 0
ZERO = [0] * MEMBERS
NO_POOL = (0, 0, 0)
FLOWS = {
    # flow: the saved game's changes (per member, or None), the block's ops before COMBAT (extra),
    # the stop's cite, the expected state (standing, fled, b7a7, out), DS:ae4f (the share; None
    # where 01fb does not run), the pool's coins 4-6, the experience each member gains, and each
    # member's may-train (+0xcd). The award is 0 where no member stands.
    "won": dict(changes=None, extra=[], cite="dqk 75cc:0736", expect=(True, False, False, 0),
                share=0, pool=NO_POOL, gained=ZERO, may=ZERO),
    "fled": dict(changes=[{0x64: 3, 0x65: 0}] * MEMBERS, extra=[], cite="dqk 75cc:0736",
                 expect=(False, True, False, 0), share=None, pool=NO_POOL, gained=ZERO, may=ZERO),
    "lost": dict(changes=[{0x64: 2, 0x65: 0, 0x18b: 0}] * MEMBERS, extra=[],
                 cite="dqk 75cc:0736", expect=(False, False, True, MEMBERS), share=None,
                 pool=NO_POOL, gained=ZERO, may=ZERO),
    "pool": dict(changes=None, extra=[TREASURE], cite="dqk 75cc:0736",
                 expect=(True, False, False, 0), share=825, pool=(1000, 3, 1),
                 gained=[825, 275, 275, 412, 412, 412], may=[0, 0, 0, 0, 1, 0]),
    "bonus": dict(changes=[{0x5a: 2}, {}, {}, {0x5a: 0}, {0x5a: 5}, {0x5a: 6}],
                  extra=[TREASURE], cite="dqk 75cc:0736", expect=(True, False, False, 0),
                  share=825, pool=(1000, 3, 1), gained=[907, 275, 275, 907, 825, 907],
                  may=[0, 0, 0, 0, 1, 0]),
    "zero": dict(changes=[{}, {}, {}, {}, {}, {0x18b: 0}], extra=[SAVE_ZERO],
                 cite="dqk 75cc:0736", expect=(True, False, False, 0), share=0xffffffff,
                 pool=NO_POOL, gained=[-1, 0, 0, 0, 0, 0], may=ZERO),
    "negative": dict(changes=[{}, {}, {}, {0x18b: 0}, {0x18b: 0}, {0x18b: 0}],
                     extra=[TREASURE, SAVE_ZERO], cite="dqk 75cc:0736",
                     expect=(True, False, False, 2), share=-2475 & 0xFFFFFFFF, pool=(1000, 3, 1),
                     gained=[-2475, -825, -825, 0, 0, 0], may=ZERO),
}
EXP = 0x44   # the experience, a dword (rules_dark_queen.c: DWORD(0x44))
MAY = 0xcd   # the may-train byte (BYTE(0xcd))


def start_block(extra=()):
    """Block 1: the entry PRESS KEY, the 3D view's files, the flags set, var 0x2e, the extra ops
    (name, operands), a PRESS KEY before COMBAT, COMBAT, EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("LOAD_FILES", imm(2), imm(1))  # the jumper's (block 1, ECL 8025), as dqk_shops
    b.op("COMPARE", imm(1), imm(1))     # the flags: = <= >= are set, < > <> are clear
    b.op("SAVE", imm(1), var(SURPRISE))
    for name, operands in extra:
        b.op(name, *operands)
    b.op("PRESS_KEY")
    b.op("COMBAT")
    b.op("EXIT")
    return b


def saved_game(work, name, changes):
    """Saved game A with each member changed (changes: a list of offset: byte dicts, one a member),
    in a folder of its own."""
    data = bytearray((dqk_lib.ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
    if changes is not None:
        for member in range(MEMBERS):
            at = dqk_modify.record_at(data, member)
            for offset, value in changes[member].items():
                data[at + offset] = value
    folder = work / f"saves-{name}"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    (folder / "SAVGAMA.QSV").write_bytes(bytes(data))
    return bytes(data), [folder]


def flags_of(box):
    return list(box.ds_read(0xb2f5, 6))


def read_state(box):
    """The original's state where it spins: the variables, the records, the flags and the
    end-of-combat bytes, the mode, RandSeed and the effect table."""
    return dict(
        vars=dqk_fight.block_vars(box),
        records=[raw for _, raw in L.records(box)],
        flags=flags_of(box),
        standing=box.ds_read(0xaf3f, 1)[0] != 0, fled=box.ds_read(0xc7de, 1)[0] != 0,
        monsters=box.ds_read(0xc7df, 1)[0], out=box.ds_read(0xb1da, 1)[0],
        b7a7=box.ds_read(0xb7a7, 1)[0] != 0, demo=box.ds_read(0xae59, 1)[0],
        mode=box.ds_word(0xae57), seed=f"{int.from_bytes(box.ds_read(0xca0e, 4), 'little'):08x}",
        effects=box.ds_read(0x631e, 18), share=int.from_bytes(box.ds_read(0xae4f, 4), "little"),
        pool=tuple(int.from_bytes(box.ds_read(at, 4), "little") for at in (0xb1db, 0xb1df, 0xb1e3)))


def original(work, name, changes, patched, verbose):
    """The original's state at the spin for one flow: stage 1 at 2093:0015, then the traps at
    75cc:0736 while stage 1 spins, then stage 1 given back. Returns (state, the
    trap's stop address, whether the game waited at the PRESS KEY before COMBAT)."""
    qsv, saves = saved_game(work, name, changes)
    with cokbox.Box(workdir=work / f"box-{name}", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched), saves=saves if changes is not None else []) as box:
        dqk_fight.enter(box)
        box.set_seed(SEED)
        box.keys("\\r")  # the entry PRESS KEY: LOAD FILES, the flags, SAVE, then the PRESS KEY
        box.settled(tolerance=64)
        waiting = box.waiting()
        v = box.far(box.ds * 16 + dqk_ecl.DS_VARS)
        code = L.code(box, 0x2093, 0x0015)
        orig = box.read(code, 2)
        if orig != b"\x55\x8b":
            raise cokbox.BoxError(f"2093:0015 is not where it was expected: {orig.hex()}")
        box.write(code, b"\xeb\xfe")
        h1 = L.Trap([(code, orig)], 0x2093, 0x0015)
        box.keys("\\r")  # COMBAT
        t0 = time.monotonic()
        while not L.held(box, h1)[0]:
            if time.monotonic() - t0 > 60:
                raise cokbox.BoxError("stage 1 did not hold at 2093:0015 (135c's start)")
        stage1_surprise = box.byte(v + SURPRISE)  # 135c's start cleared it before the flags
        h3 = L.trap(box, 0x75CC, 0x0736, seconds=10)
        L.untrap(box, h1)
        hold = L.Trap(list(h3), 0x75CC, 0x0736)
        t0 = time.monotonic()
        while not L.held(box, hold)[0]:
            if time.monotonic() - t0 > 60:
                raise cokbox.BoxError("stage 2 did not hold at 0593's 0736")
        state = read_state(box)
        state["stage1_surprise"] = stage1_surprise
        state["waiting"] = waiting
        return state, qsv


def port(work, name, patched, qsv):
    """eclplay's run of the block from the saved game (qsv), the keys typed: the entry PRESS KEY
    and the one before COMBAT. Returns (variables, the records as raw bytes, the state's lines,
    the transcript)."""
    entry = dqk_shops.entry_file(work, name, qsv)
    records = work / f"port-{name}.records"
    state = work / f"port-{name}.state"
    for path in (records, state):
        if path.exists():
            path.unlink()
    extra = ["--load", str(entry), "--play", "--records", str(records), "--state", str(state)]
    variables, _, _ = dqk_ecl.port(work, name, patched, typed="\\r", extra=extra, block=None)
    data = records.read_bytes() if records.exists() else b""
    size = L.RECORD
    ported = [data[k:k + size] for k in range(0, max(len(data) - 3, 0), size)]
    lines = {}
    if state.exists():
        for line in state.read_text().splitlines():
            key, _, value = line.partition(" ")
            lines[key] = value
    transcript = (work / f"port-{name}.txt").read_text()
    return variables, ported, lines, transcript


def run_flow(v, work, patched, name, verbose):
    spec = FLOWS[name]
    orig, qsv = original(work, name, spec["changes"], patched, verbose)
    v.check(orig["waiting"], f"the original waits at the PRESS KEY before COMBAT ({name})")
    v.check(orig["stage1_surprise"] == 0, f"var 0x2e is 0 at 2093:0015's spin: 135c's start ran "
            f"({name})", f"{orig['stage1_surprise']}")
    v.check(len(orig["records"]) == MEMBERS, f"the original's party is the six members ({name})",
            f"{len(orig['records'])} records")
    got = (orig["standing"], orig["fled"], orig["b7a7"], orig["out"])
    v.check(got == spec["expect"], f"the original's af3f, c7de, b7a7 and b1da are as the flow "
            f"sets them ({name})", f"standing, fled, b7a7, out: {got}, want {spec['expect']}")
    v.check(orig["effects"] == EFFECTS, "the 18 effect ids at DS:631e are as the port's table "
            f"({name})", orig["effects"].hex())
    v.check(orig["demo"] == 0, f"the original is not the demonstration ({name})")
    v.check(orig["mode"] == 4, f"the mode is 4 at the stop, as 135c sets it after 0593 ({name})",
            f"mode {orig['mode']}")
    # The flow's own expectations on the original's side: the share, the pool's coins, each
    # member's experience gained (the saved game's, before the award) and its may-train.
    if spec["share"] is not None:
        v.check(orig["share"] == spec["share"], f"the original's share (DS:ae4f) is as the flow "
                f"sets it ({name})", f"{orig['share']:08x}, want {spec['share']:08x}")
    v.check(orig["pool"] == spec["pool"], f"the original's pool coins (DS:b1db, b1df, b1e3) are "
            f"as the flow sets them ({name})", f"{orig['pool']}, want {spec['pool']}")
    gained, may = [], []
    for m, raw in enumerate(orig["records"]):
        at = dqk_modify.record_at(qsv, m)
        before = int.from_bytes(qsv[at + EXP:at + EXP + 4], "little")
        after = int.from_bytes(raw[EXP:EXP + 4], "little")
        gained.append((after - before + 2 ** 31) % 2 ** 32 - 2 ** 31)
        may.append(raw[MAY])
    v.check(gained == spec["gained"], f"the experience each member gains is as the flow's "
            f"arithmetic gives ({name})", f"{gained}, want {spec['gained']}")
    v.check(may == spec["may"], f"each member's may-train is as the flow's ({name})",
            f"{may}, want {spec['may']}")

    variables, ported, lines, transcript = port(work, name, patched, qsv)
    stop = f"{spec['cite']}"
    v.check(stop in transcript and "error:" in transcript,
            f"eclplay stops at {stop}, cited ({name})", transcript.strip()[-200:])
    v.check(len(variables) == VARIABLES, f"eclplay runs the block to the stop ({name})")
    differing = [a for a in range(1, VARIABLES + 1) if orig["vars"][a - 1] != variables[a - 1]]
    v.check(not differing, f"the 1 KB of variables agree, var 0x2e included ({name})",
            ", ".join(f"{a:x}: original {orig['vars'][a - 1]:02x}, port {variables[a - 1]:02x}"
                      for a in differing[:8]))
    want = [dqk_shops.masked(raw) for raw in orig["records"]]
    have = [dqk_shops.masked(raw) for raw in ported]
    v.check(len(want) == len(have) and want == have,
            f"the party's records agree (pointers masked, {name})",
            f"{len(have)} records, the original {len(want)}")
    port_flags = [int(x) for x in lines.get("flags", "").split()]
    v.check(port_flags == orig["flags"], f"the COMPARE flags agree: = <> < > <= >= ({name})",
            f"original {orig['flags']}, port {port_flags}")
    v.check(int(lines.get("share", -1)) == orig["share"], f"the share (DS:ae4f) agrees ({name})",
            f"original {orig['share']}, port {lines.get('share')}")
    port_pool = tuple(int(x) for x in lines.get("pool", "").split())
    v.check(port_pool == orig["pool"], f"the pool's coins 4-6 agree ({name})",
            f"original {orig['pool']}, port {port_pool}")
    port_seed = lines.get("seed", "")
    v.check(port_seed == orig["seed"], f"RandSeed agrees ({name})",
            f"original {orig['seed']}, port {port_seed}")
    v.check(int(lines.get("mode", -1)) == orig["mode"], f"the mode agrees ({name})",
            f"original {orig['mode']}, port {lines.get('mode')}")
    # DS:af3f, c7de, c7df, b1da, b7a7 and the share DS:ae4f (eclplay --state's names).
    for key, addr, value in (("standing", "af3f", orig["standing"]), ("fled", "c7de", orig["fled"]),
                             ("monsters", "c7df", orig["monsters"]), ("out", "b1da", orig["out"]),
                             ("b7a7", "b7a7", orig["b7a7"])):
        port_value = int(lines.get(key, -1))
        v.check(port_value == int(value), f"DS:{addr} agrees ({name})",
                f"original {int(value)}, port {port_value}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default=",".join(FLOWS),
                        help="which flows: " + ", ".join(FLOWS))
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_treasure")
    v = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    for name in opts.flows.split(","):
        if name not in FLOWS:
            parser.error(f"flow {name}: one of {', '.join(FLOWS)}")
        patched = hlib.replace(glb, JUMP, dqkecl.assemble(start_block(FLOWS[name]["extra"])))
        run_flow(v, work, patched, name, opts.verbose)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
