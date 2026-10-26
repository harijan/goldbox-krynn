#!/usr/bin/env python3
"""Checks #47 and #73: Escape at Trade's first coin list (docs/checks.md, Checks
against the original: "`Encamp`, choose `View` first, `Trade`, pick a
partner, and press Escape at the first coin list" and "In a shop, `View`
first, `Trade`, pick a partner and press Escape at the first coin list").

Trade (546c:2c75) tests a byte it never set, [bp-0x13c], at 546c:2f9b: 0
asks for a partner again, else Trade is left. The port follows what each
path leaves on the stack there (src/sheet.h, docs/camp.md): a camp's View
asks again as View's first command, after Magic too, and leaves after
Items, Spells, Trade or Drop, a View before or Alter; a shop's View first
asks again, after a View leaves; a temple's View first leaves, and so does
the start menu's View. From the adventure's commands the byte is what an
interrupt's frame left there, which varies from run to run (0 in 4 of 13
runs at fixed cycles, the game's speed, in 11 of 12 with turbo): the port
leaves, and the original is not compared there. Each case runs in the original
from a fresh start and in eclplay --start-menu: the coin list's screen
against the port's; the byte (and [bp-0x13d], tested just before) read
from Trade's frame while the coin list waits; then Escape and what the
game does next (the partner's menu again, inside Trade, or View's menu)
against the port's screen. Always: asks again exactly when the byte is 0
(546c:2f9b's test).

Places: game A's start leads to the outpost; its inn camps there (camp
cases), its armoury is a shop (#73), its temple (NEXT, TEMPLE) one too;
ECL1 block 32's chest (game G, c_chest.py's) is the treasure menu: View
first asks again (the picture's load), after Take it leaves. The
adventure commands' View (#47's last case) is Throtl's street after the
gate fight, saved there by the port as game C. The start menu's View
loads game A only.
"""
import argparse
import pathlib
import shutil
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import e_common as E  # noqa: E402
cokbox = E.cokbox

OUTPOST = r"\r\r\r\r\r\r\r"
INN_CAMP = OUTPOST + r"I\r"
ARMOURY = OUTPOST + "A"
PICK = r"T\vS"
# ECL1 block 32's chest (c_chest.py's game G, north of it): the room, the
# results, the treasure menu (View Take Pool Share Exit).
TO_CHEST = r"m\^\r\r"           # Trade, the next character as partner, Select
# name: (check, saved game, keys to the place, keys to the coin list, the
# port's outcome: asks again, leaves or stops)
# name: (check, saved game (None: loaded, not begun), keys to the place,
# keys to the coin list, the port's outcome: asks again or leaves)
CASES = {
    "camp-view-first": (47, "A", INN_CAMP, "V" + PICK, "asks again"),
    "camp-drop-first": (47, "A", INN_CAMP, r"VD\e" + PICK, "leaves"),
    "camp-spells-first": (47, "A", INN_CAMP, r"\v\v\v\vVS\e" + PICK, "leaves"),
    "camp-view-twice": (47, "A", INN_CAMP, "VEV" + PICK, "leaves"),
    "camp-magic-first": (47, "A", INN_CAMP, r"M\eV" + PICK, "asks again"),
    "camp-alter-first": (47, "A", INN_CAMP, r"A\eV" + PICK, "leaves"),
    "commands-view": (47, "C", "", "V" + PICK, "leaves"),
    "start-view": (47, None, "", r"\v\r\r" + PICK, "leaves"),
    "shop-view-first": (73, "A", ARMOURY, "V" + PICK, "asks again"),
    "shop-view-twice": (73, "A", ARMOURY, "VEV" + PICK, "leaves"),
    "temple-view-first": (73, "A", OUTPOST + "NT", "V" + PICK, "leaves"),
    "treasure-view-first": (47, "G", TO_CHEST, "V" + PICK, "asks again"),
    "treasure-take-first": (47, "G", TO_CHEST, r"T\r1\r\eV" + PICK, "leaves"),
}
# Where the original's byte varies from run to run: not compared.
VARIES = {"commands-view"}
TRADE = (0x546C, 0x2C75, 0x2FA9)


def trade_frame(box):
    """SS and Trade's BP while the coin list waits: the frame whose return
    address lies in Trade (546c:2c75-2fa8) belongs to the routine Trade
    called, and holds Trade's BP."""
    st = box.hook_state()
    ss, bp = (st["ss"], st["bp"]) if st["waiting"] else (st["pollss"], st["pollbp"])
    for _ in range(12):
        up, ip, cs = struct.unpack("<HHH", box.read(ss * 16 + bp, 6))
        at = box.to_ghidra(cs, ip) or box.locate(cs, ip)
        if at and at[0] == TRADE[0] and TRADE[1] <= at[1] < TRADE[2]:
            return ss, up
        if up <= bp:
            break
        bp = up
    return None


def in_trade(where):
    for site in where:
        if not site.startswith("?"):
            seg, off = (int(v, 16) for v in site.split(":"))
            if seg == TRADE[0] and TRADE[1] <= off < TRADE[2]:
                return True
    return False


def outcome(shot):
    """What a screen after Escape shows: Trade's partner menu again, or not."""
    return "asks again" if E.text_row(shot, 24).startswith("TRADE TO?") else "leaves"


def run_case(v, name, saves, work, args):
    check, game, place, to_coins, port_says = CASES[name]
    load = E.LOAD.format(game) if game else r"\kxP\v\v\rA"
    keys = load + place + to_coins
    pair = E.Pair(keys + r"\scoins." + r"\e\safter.", work / name, saves=[saves])
    port_does = outcome(pair.frame("after"))
    v.check(port_does == port_says and E.port_error(pair.text("after")) is None,
            f"{name}: the port {port_says} (its rule for this case)",
            f"{port_does}, {E.port_error(pair.text('after'))}")
    for run in range(args.repeat):
        tag = f"{name}" + (f" (run {run + 1})" if args.repeat > 1 else "")
        with cokbox.Box(workdir=work / f"box-{name}", saves=[saves], verbose=args.verbose,
                        timeout=600, seed=args.seed) as box:
            box.keys(keys)
            box.wait_input(settle=0.5)
            pair.judge(v, "coins", box.screenshot(work / f"{name}-coins.png"),
                       f"{tag}: the first coin list")
            frame = trade_frame(box)
            v.check(frame is not None, f"{tag}: Trade's frame found", str(box.where()))
            if frame is None:
                continue
            ss, bp = frame
            b13c, b13d = box.byte(ss * 16 + bp - 0x13C), box.byte(ss * 16 + bp - 0x13D)
            print(f"  original: Trade's BP {ss:04x}:{bp:04x}, [bp-0x13c] = {b13c:#04x}, "
                  f"[bp-0x13d] = {b13d:#04x}")
            box.keys(r"\e")
            box.wait_input(settle=0.5)
            shown = box.screenshot(work / f"{name}-after.png")
            where = box.where()
        does = outcome(shown)
        print(f"  original after Escape: {does} (waits at {', '.join(where)})")
        v.check(does == ("asks again" if in_trade(where) else "leaves"),
                f"{tag}: the screen and where the game waits agree", f"{does}, {where}")
        v.check(does == ("asks again" if b13c == 0 else "leaves"),
                f"{tag}: asks again exactly when [bp-0x13c] is 0 (546c:2f9b)",
                f"{does} with {b13c:#04x}")
        if name in VARIES:
            print(f"  {tag}: the original's byte varies from run to run; the port "
                  f"{port_says}, not compared")
        else:
            v.value(f"{tag}: what Escape does", does, port_says)
            pair.judge(v, "after", shown, f"{tag}: after Escape")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("--repeat", type=int, default=1,
                        help="runs of each case in the original (the byte may differ)")
    parser.add_argument("--seed", type=int, help="RandSeed at the title (default: the clock's)")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-trade")
    checklib.need_saves()
    v = checklib.Verdict()
    saves = E.make_c(work)     # SAVE/'s games and C (Throtl, after the gate fight)
    if any(name.startswith("treasure") for name in (args.case or CASES)):
        import c_places as cp
        g, _ = cp.throtl_save(work / "save-g", "G", at=(12, 4, 4), var={0x4C4A: 4})
        shutil.copyfile(g / "SAVGAMG.DAT", saves / "SAVGAMG.DAT")
        for f in g.glob("CHRDATG*"):
            shutil.copyfile(f, saves / f.name)
    for name in args.case or CASES:
        run_case(v, name, saves, work, args)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
