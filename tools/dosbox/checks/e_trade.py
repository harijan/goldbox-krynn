#!/usr/bin/env python3
"""Checks #47 and #73: Escape at Trade's first coin list (README, Checks
against the original: "`Encamp`, choose `View` first, `Trade`, pick a
partner, and press Escape at the first coin list" and "In a shop, `View`
first, `Trade`, pick a partner and press Escape at the first coin list").

Trade (546c:2c75) tests a byte it never set, [bp-0x13c], at 546c:2f9b: 0
asks for a partner again, else Trade is left. The port follows it where
the README says what the stack holds (a camp's View: asks again as View's
first command, leaves after Items, Spells, Trade or Drop, or a View before)
and stops with COK_ECL_UNDEFINED elsewhere (View from the adventure
commands, a shop's View). Each case runs in the original from a fresh
start: the byte (and [bp-0x13d], tested just before) is read from Trade's
frame while the coin list waits, then Escape is typed and what the game
does next is recorded (the partner's menu again, inside Trade, or View's
menu), with the screen compared with eclplay's where the port goes on.

Places: game A's start leads to the outpost; its inn camps there (camp
cases), its armoury is a shop (#73). The adventure commands' View (#47's
last case) is Throtl's street after the gate fight, saved there by the
port as game C.
"""
import argparse
import pathlib
import struct
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import e_common as E  # noqa: E402
cokbox = E.cokbox

OUTPOST = r"\r\r\r\r\r\r\r"
INN_CAMP = OUTPOST + r"I\r"
ARMOURY = OUTPOST + "A"
PICK = r"T\vS"           # Trade, the next character as partner, Select
# name: (check, saved game, keys to the place, keys to the coin list, port's verdict)
CASES = {
    "camp-view-first": (47, "A", INN_CAMP, "V" + PICK, "asks again"),
    "camp-drop-first": (47, "A", INN_CAMP, r"VD\e" + PICK, "leaves"),
    "camp-spells-first": (47, "A", INN_CAMP, r"\v\v\v\vVS\e" + PICK, "leaves"),
    "camp-view-twice": (47, "A", INN_CAMP, "VEV" + PICK, "leaves"),
    "commands-view": (47, "C", "", "V" + PICK, "stops"),
    "shop-view-first": (73, "A", ARMOURY, "V" + PICK, "stops"),
    "shop-view-twice": (73, "A", ARMOURY, "VEV" + PICK, "leaves"),
}
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("--repeat", type=int, default=1,
                        help="runs of each case in the original (the byte may differ)")
    parser.add_argument("--seed", type=int, help="RandSeed at the title (default: the clock's)")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-trade-")).resolve()
    if not (E.SAVE / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    # Game C: game A's party after Throtl's gate fight, camped and saved there.
    out, saves_c, _ = E.eclplay_block(
        r"\r\rEESC", work / "port-save",
        ["--combat", "pass", "--party", E.SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1"], 32)
    assert (saves_c / "SAVGAMC.DAT").exists(), out[-2000:]
    saves = work / "saves"
    saves.mkdir(exist_ok=True)
    for d in (E.SAVE, saves_c):
        for f in d.iterdir():
            if f.is_file():
                (saves / f.name).write_bytes(f.read_bytes())
    status = 0
    for name in args.case or CASES:
        check, game, place, to_coins, port_says = CASES[name]
        keys = place + to_coins
        pout, _, pshots = E.eclplay_block(keys + r"\e", work / f"port-{name}",
                                          ["--load", saves / f"SAVGAM{game}.DAT", "--play"],
                                          None, shots=True)
        last = [line for line in pout.splitlines() if line.startswith(("menu:", "error:"))][-1]
        print(f"#{check} {name}: port {port_says} ({last})")
        for run in range(args.repeat):
            with E.Box(workdir=work / f"box-{name}", saves=[saves], verbose=args.verbose,
                            timeout=600, seed=args.seed) as box:
                box.keys(E.LOAD.format(game) + keys)
                box.wait_input(settle=0.5)
                box.screenshot(work / f"{name}-coins.png")
                frame = trade_frame(box)
                if frame is None:
                    print("  original: Trade's frame not found; where", box.where())
                    status = 1
                    continue
                ss, bp = frame
                b13c, b13d = box.byte(ss * 16 + bp - 0x13C), box.byte(ss * 16 + bp - 0x13D)
                print(f"  original: Trade's BP {ss:04x}:{bp:04x}, [bp-0x13c] = {b13c:#04x}, "
                      f"[bp-0x13d] = {b13d:#04x}")
                box.keys(r"\e")
                box.wait_input(settle=0.5)
                shown = box.screenshot(work / f"{name}-after.png")
                where = box.where()
                does = "asks again" if in_trade(where) else "leaves"
                print(f"  original after Escape: {does} (waits at {', '.join(where)})")
                if port_says != "stops":
                    best = min(pshots, key=lambda p: shown.diff(cokbox.Screen.load(p))[0])
                    count, area = shown.diff(cokbox.Screen.load(best))
                    print(f"  screen: {count} pixels differ from eclplay's {best.name} "
                          f"(its last is {pshots[-1].name})")
                    agrees = does == port_says
                    print(f"  {'AGREES' if agrees else 'DIFFERS'} with the port")
                    if not agrees:
                        status = 1
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())
