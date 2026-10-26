#!/usr/bin/env python3
"""Death Knights of Krynn's View and items menu (580d:0c66, 0ffa) in the
original against eclplay --game death-knights --start-menu (docs/camp.md,
"View"). Two flows, each from saved game A loaded and begun, the same keys
typed to both, the original's screen at each wait against the port's at
the key read the same keys leave it in:

camp: block 16's rest, camp, View for SIR DRYDEN (Items Trade Drop Exit by
index), Items: the list with its readied column, Ready of the mace with
the sword readied ("already using"), Halve of it, the sword unreadied,
Trade of it (65cd:33c9's pick: Escape, then to the second), Drop of the mace (Lose it?,
Yes), Escape back to View, Drop of 5 steel (580d:26d0); Escape twice,
camp's Save Current Game C: saved game C and its characters' files (their
items' names with Death Knights' readied column).

vault: saved game A with its block (var 0x4bf2) 35, the bank of Gargath
(3948:000e): View in mode 7 (Deposit for the money), 2 steel deposited,
Items in mode 7 (Deposit for the items), the mace deposited ("will be left
in the vault"), Exit: TMPVAULT.DAT (46b2:302d) byte for byte; then camp,
Save C: VAULTC.DAT, saved game C and its characters.

Seeded (Box seed, eclplay --seed). In run_all.py's Death Knights set (make dkk-checks). About a minute.

    python3 tools/dosbox/checks/dkk_items.py [--workdir DIR] [--seed N]
        [--flows camp,vault]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402
import dkk_treasure  # noqa: E402

cokbox = checklib.cokbox

# After Begin: block 16's two texts, Yes to rest: the camp; then each step
# (the screen reached, the keys typed at the wait before it).
CAMP = [
    ("camp", r"\r\r\r"),
    ("view", "V"),             # SIR DRYDEN's sheet: Items Trade Drop Exit
    ("items", "I"),            # the list: Ready Use Trade Drop Halve Join Exit
    ("already", r"\v\v\vR"),   # the mace: already using Long Sword +1
    ("halve", "H"),            # Can't halve that
    ("unready", r"\^\^\^R"),   # the sword unreadied
    ("whom", "T"),             # Trade with Whom? (65cd:33c9: Select alone)
    ("whom-none", r"\e"),      # Escape: none, the list at once
    ("whom-again", "T"),
    ("traded", r"\v\r"),       # to TANAR: the list without it
    ("lose", r"\v\vD"),        # the mace: Your Mace +1 will be gone forever, Lose it?
    ("dropped", "Y"),
    ("view-again", r"\e"),     # Escape is Exit: View's menu, its own selection
    ("coins", "D"),            # Drop: the coins
    ("how-much", r"\r"),       # How much Steel will you drop?
    ("dropped-coins", r"5\r"),
    ("view-back", r"\e"),
    ("camp-back", r"\e"),
    ("which", "S"),
    ("saved", r"\>\>\r"),      # Save Current Game C
]
VAULT = [
    ("view", "V"),             # the sheet in mode 7: Items Trade Deposit Exit
    ("coins", "D"),            # Deposit: the coins
    ("how-much", r"\r"),       # How much Steel will you deposit?
    ("deposited", r"2\r"),
    ("view-back", r"\e"),
    ("vault-back", r"\e"),     # the vault's menu (65cd:2db0)
    ("items", "I"),            # Ready Trade Deposit Halve Join Exit
    ("deposit", r"\v\v\vD"),   # the mace: will be left in the vault
    ("items-back", r"\e"),
    ("left", "E"),             # Exit: TMPVAULT.DAT; block 36: the adventure's menu
    ("camp", "E"),
    ("which", "S"),
    ("saved", r"\>\>\r"),
]
MASKS = {"camp": {}, "vault": {}}


def load(path):
    return cokbox.Screen.load(path) if path is not None else None


def run_steps(v, work, box, flow, steps, prefix, seed, saves, original=None):
    keys = prefix
    port_saves = None
    for name, typed in steps:
        keys += typed
        K.type_steps(box, [typed], settle=2.0)
        shown = box.screenshot(work / f"dosbox-{flow}-{name}.png")
        _, shot, _, _ = K.port(work, f"{flow}-{name}", keys, seed, saves=saves)
        port_saves = work / f"port-{flow}-{name}" / "saves"
        v.screen(f"{flow}: {name}", shown, load(shot), masks=MASKS[flow].get(name, ()),
                 work=work)
        if flow == "vault" and name == "left":
            a = (original / "TMPVAULT.DAT").read_bytes() \
                if (original / "TMPVAULT.DAT").exists() else None
            b = (port_saves / "TMPVAULT.DAT").read_bytes() \
                if (port_saves / "TMPVAULT.DAT").exists() else None
            v.value("vault: TMPVAULT.DAT", a.hex() if a else None, b.hex() if b else None)
    return port_saves


def camp(v, work, opts):
    seed = opts.seed
    saves = work / "saves-camp"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    with cokbox.Box(workdir=work / "box-camp", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        port_saves = run_steps(v, work, box, "camp", CAMP, K.LOAD_A_BEGIN, seed, saves)
        original = work / "box-camp" / "c" / "DKK" / "SAVE"
    dkk_treasure.compare_files(v, "camp", original, port_saves)


def vault(v, work, opts):
    seed = opts.seed
    saves = work / "saves-vault"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    game = bytearray((saves / "SAVGAMA.PTY").read_bytes())
    game[1 + 0xF2] = 35  # var 0x4bf2, the block
    (saves / "SAVGAMA.PTY").write_bytes(bytes(game))
    original = work / "box-vault" / "c" / "DKK" / "SAVE"
    with cokbox.Box(workdir=work / "box-vault", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), "the title's four pictures (vault)")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        v.check(box.ds_byte(K.DS_MODE) == 7, "the original waits in the vault's menu (mode 7)")
        port_saves = run_steps(v, work, box, "vault", VAULT, K.LOAD_A_BEGIN, seed, saves,
                               original)
    a, b = (original / "VAULTC.DAT").read_bytes(), (port_saves / "VAULTC.DAT").read_bytes()
    v.value("vault: VAULTC.DAT", a.hex(), b.hex())
    dkk_treasure.compare_files(v, "vault", original, port_saves)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_items"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="camp,vault",
                        help="which flows: camp, vault (comma-separated)")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "camp" in flows:
        camp(v, work, opts)
    if "vault" in flows:
        vault(v, work, opts)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())
