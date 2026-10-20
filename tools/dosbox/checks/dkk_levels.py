#!/usr/bin/env python3
"""Death Knights of Krynn's training, Knight Change Order and Memorize in
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

Seeded (Box seed, eclplay --seed). Not part of run_all.py's set. About
three minutes.

    python3 tools/dosbox/checks/dkk_levels.py [--workdir DIR] [--seed N]
        [--flows train,memorize]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import struct
import sys

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


def compare_files(v, original, port):
    a, b = (original / "SAVGAMC.PTY").read_bytes(), (port / "SAVGAMC.PTY").read_bytes()
    v.check(dkk_start.same_saved(a, b), "train: saved game C (but the stack's bytes)")
    names = sorted(p.name for p in original.iterdir() if p.name.startswith("CHRDATC"))
    v.value("train: the character files", names,
            sorted(p.name for p in port.iterdir() if p.name.startswith("CHRDATC")))
    for n in names:
        x = dkk_start.masked_character(n, (original / n).read_bytes())
        y = dkk_start.masked_character(n, (port / n).read_bytes())
        diff = [k for k in range(min(len(x), len(y))) if x[k] != y[k]][:8]
        v.check(x == y, f"train: {n}" + ("" if x == y else f" differs at {[hex(k) for k in diff]}"
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_levels"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="train,memorize",
                        help="which flows: train, memorize (comma-separated)")
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
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())
