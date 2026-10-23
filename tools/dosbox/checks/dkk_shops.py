#!/usr/bin/env python3
"""Death Knights of Krynn's shop and temple (3b89:06f3, 3999:07d3) in the
original against eclplay --game death-knights --start-menu (DEVELOPERS.md,
"Death Knights' shops"). Three flows, each from a copy of saved game A that
resumes in ECL1 block 36, the outpost of Gargath (the block, its map 36 and
wall set 1 in slot 0 written in, as dkk_views.py does, and SIR DRYDEN given
2 gems, 1 jewel and 40 hit points), the same keys typed to both, the
original's screen at each wait against the port's at the key read the same
keys leave it in:

shop: from the square south of the weapons store, a step north (its square
6: "A WEAPONS STORE. WILL YOU BUY?"), Yes: STOCK 1 at prices of 16 and the
shop; Buy (3b89:0453) of the Banded Mail from SIR DRYDEN's steel, the funds
under the list (3b89:0000); View's Items in mode 1: Sell of the Mace +1
(580d:2077), Id of the Banded Mail (21d3, its 100 steel); Appraise
(5c72:0dc2): a gem sold, the jewel kept; Pool, Buy of the Chain Mail paid
from the pooled steel, Exit with money left ("as you leave a man says",
Yes from Yes), Share, Exit; camp's Save Current Game C:
saved game C and its characters' files byte for byte (the money, the items
bought and kept, the price line in the bytes after the Banded Mail's name),
but what STOCK's stack left in the original's after that line (see
dkk_treasure.compare_files).

temple: from the temple's square (3), a turn west and a step into its wall
(4c06, the wall ahead), Yes: the temple; Heal (3999:04e2): Cure Light
Wounds (free), Cure Blindness for one not blind ("cast cure anyway?", No),
Heal; Escape; View, Items and Escape, back at the temple's menu (View's
flag is DS:c0f8, which a special key sets); View and Right, which moves and
reads on in View, and Up, a special key, back at the temple's menu; Exit;
camp's Save C: the hit points in its files.

raise: the temple again, SIR DRYDEN dead (status 6) and drained of a
knight level (+0x4c 6 of +0x54 7, 150000 experience of 210003, 90 most
hit points of 100): Raise Dead (3999:01fc, the spell's 5dc5:4196: "is
raised", a point of constitution less, the hit points it gives worked out
again), Restoration (045f, 5dc5:2326: the level, experience and hit points
back); Save C: his record in its files.

Seeded (Box seed, eclplay --seed). In run_all.py's Death Knights set (make dkk-checks). About a minute
and a half (turbo).

    python3 tools/dosbox/checks/dkk_shops.py [--workdir DIR] [--seed N]
        [--flows shop,temple]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402
import dkk_treasure  # noqa: E402

cokbox = checklib.cokbox

# Each step: the screen reached, the keys typed at the wait before it.
SAVE_C = [
    ("after", r"\r"),          # 'GODS SPEED YOU BACK.': Move mode's Exit
    ("adventure", "E"),        # the adventure's menu
    ("camp", "E"),
    ("which", "S"),
    ("saved", r"\>\>\r"),      # Save Current Game C
]
SHOP = [
    ("store", r"M\^"),         # Move, north: A WEAPONS STORE. WILL YOU BUY?
    ("shop", "Y"),             # STOCK 1: Buy View Pool Appraise Exit
    ("buy", "B"),              # the list, the funds under it
    ("bought", r"\r"),         # SIR DRYDEN buys a Banded Mail
    ("buy-left", r"\e"),
    ("view", "V"),
    ("items", "I"),            # Ready Trade Drop Halve Join Sell Id Exit
    ("sell", r"\v\v\vS"),      # I'll give you 750 steel pieces for your Mace +1
    ("sold", r"\r"),
    ("id", "I"),               # for 100 steel pieces i'll identify your Banded Mail
    ("identified", r"\r"),     # i cannot tell anything new about your Banded Mail
    ("items-back", r"\e"),
    ("view-back", r"\e"),
    ("appraise", "A"),         # gems : 2, jewelry : 1, Gems Jewelry Exit
    ("gem", "G"),              # The gem is valued at N steel. Sell Keep
    ("gem-sold", "S"),
    ("jewel", "J"),
    ("jewel-kept", "K"),
    ("appraise-back", r"\e"),  # Escape is Exit: the shop's menu
    ("pooled", "P"),           # Pool: Buy View Take Pool Share Appraise Exit
    ("buy-2", "B"),            # the funds: none and the pool's
    ("bought-2", r"\v\r"),     # the Chain Mail, paid from the pool's steel
    ("buy-2-left", r"\e"),
    ("leaving", "E"),          # as you leave a man says, ...: from Yes
    ("back", r"\r"),           # Yes: back to the shop's menu
    ("shared", "S"),           # Share
    ("left", "E"),
] + SAVE_C
TEMPLE = [
    ("turned", r"M\>"),        # Move, facing west
    ("temple-text", r"\^"),    # into the wall: THE TEMPLE. NEED TO HEAL?
    ("temple", "Y"),           # Heal View Pool Appraise Exit
    ("heal", "H"),             # the cures, SIR DRYDEN's 40/95
    ("cured", r"\v\vH"),       # Cure Light Wounds
    ("anyway", r"\^\^H"),      # Cure Blindness: is not blind. cast cure anyway?
    ("not-cast", r"\r"),       # No
    ("healed", r"\v\v\v\v\vH"),  # Heal
    ("heal-back", r"\e"),
    ("view", "V"),
    ("items", "I"),
    ("items-escaped", r"\e"),  # View's flag is DS:c0f8: the temple's menu
    ("view-2", "V"),
    ("view-right", r"\>"),     # Right in View's menu: a move, View reads on
    ("view-up", r"\^"),        # Up, a special key that moves nothing: the temple's menu
    ("left", "E"),
] + SAVE_C
RAISE = [
    ("turned", r"M\>"),
    ("temple-text", r"\^"),
    ("temple", "Y"),
    ("heal", "H"),             # the cures, SIR DRYDEN's 0/90
    ("raised", r"\v" * 7 + "H"),  # Raise Dead: is raised
    ("restored", r"\v\v\vH"),  # Restoration
    ("heal-back", r"\e"),
    ("left", "E"),
] + SAVE_C
# Where each flow starts in block 36's map (GEO1 record 36): its square,
# facing, and the square's byte (its code in bits 0-5).
STARTS = {"shop": (5, 7, 0, 0x17), "temple": (6, 9, 4, 0x83), "raise": (6, 9, 4, 0x83)}


def load(path):
    return cokbox.Screen.load(path) if path is not None else None


def saved_game(work, flow):
    """SAVGAMA.PTY's copy resuming in block 36 at the flow's start, with
    SIR DRYDEN's gems, jewel and hit points, in WORK/saves-FLOW."""
    saves = work / f"saves-{flow}"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    x, y, facing, square = STARTS[flow]
    path = saves / "SAVGAMA.PTY"
    data = bytearray(path.read_bytes())
    data[1 + 0xF2] = 36                       # var 0x4bf2, the block
    data[1 + 0xC5] = 36                       # var 0x4bc5, the map
    data[1 + 0xE6] = 1                        # var 0x4be6, a 3D area
    data[0xA01:0xA04] = bytes([x, y, facing])
    data[0xA05] = square
    data[0xA08:0xA0C] = bytes([1, 0, 0, 0])  # wall set 1 in slot 0
    path.write_bytes(data)
    first = saves / "CHRDATA1.GAM"
    record = bytearray(first.read_bytes())
    struct.pack_into("<HH", record, 0x41, 2, 1)  # gems, jewelry
    record[0xD6] = 40                            # hit points
    if flow == "raise":
        record[0xC7], record[0xC8], record[0xD6] = 6, 0, 0  # dead
        record[0x45 + 7], record[0x4D + 7] = 6, 7            # a knight level drained
        struct.pack_into("<II", record, 0x62, 150000, 210003)  # experience, undrained
        record[0x28], record[0x6A] = 90, 100                 # most hit points, undrained
    first.write_bytes(bytes(record))
    return saves


def run_flow(v, work, opts, flow, steps):
    seed = opts.seed
    saves = saved_game(work, flow)
    original = work / f"box-{flow}" / "c" / "DKK" / "SAVE"
    with cokbox.Box(workdir=work / f"box-{flow}", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), f"the title's four pictures ({flow})")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        shown = box.screenshot(work / f"dosbox-{flow}-begun.png")
        _, begun, _, _ = K.port(work, f"{flow}-begun", K.LOAD_A_BEGIN, seed, saves=saves)
        v.screen(f"{flow}: begun", shown, load(begun), work=work)
        keys = K.LOAD_A_BEGIN
        port_saves = None
        for name, typed in steps:
            keys += typed
            K.type_steps(box, [typed], settle=2.0)
            shown = box.screenshot(work / f"dosbox-{flow}-{name}.png")
            _, shot, _, _ = K.port(work, f"{flow}-{name}", keys, seed, saves=saves)
            port_saves = work / f"port-{flow}-{name}" / "saves"
            v.screen(f"{flow}: {name}", shown, load(shot), work=work)
    # The items STOCK gave hold the stack's bytes after their names in the
    # original, 0s in the port's (see Death Knights' treasure): the Banded
    # Mail's past its price line.
    dkk_treasure.compare_files(v, flow, original, port_saves, explained_items=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_shops"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="shop,temple,raise",
                        help="which flows: shop, temple, raise (comma-separated)")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "shop" in flows:
        run_flow(v, work, opts, "shop", SHOP)
    if "temple" in flows:
        run_flow(v, work, opts, "temple", TEMPLE)
    if "raise" in flows:
        run_flow(v, work, opts, "raise", RAISE)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())
