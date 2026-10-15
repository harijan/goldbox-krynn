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
start and in eclplay --start-menu: the coin list's screen against the
port's; the byte (and [bp-0x13d], tested just before) read from Trade's
frame while the coin list waits; then Escape and what the game does next
(the partner's menu again, inside Trade, or View's menu). Where the port
goes on, that outcome and the screen against the port's; where it stops
(it must, with its reason), the original's recorded behaviour instead: a
shop's View first 0x00, asking again ("TRADE TO?"); the commands' View
either, the byte varying from run to run. Always: asks again exactly when
the byte is 0 (546c:2f9b's test).

Places: game A's start leads to the outpost; its inn camps there (camp
cases), its armoury is a shop (#73). The adventure commands' View (#47's
last case) is Throtl's street after the gate fight, saved there by the
port as game C.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import e_common as E  # noqa: E402
cokbox = E.cokbox

OUTPOST = r"\r\r\r\r\r\r\r"
INN_CAMP = OUTPOST + r"I\r"
ARMOURY = OUTPOST + "A"
PICK = r"T\vS"           # Trade, the next character as partner, Select
# name: (check, saved game, keys to the place, keys to the coin list, the
# port's outcome: asks again, leaves or stops)
CASES = {
    "camp-view-first": (47, "A", INN_CAMP, "V" + PICK, "asks again"),
    "camp-drop-first": (47, "A", INN_CAMP, r"VD\e" + PICK, "leaves"),
    "camp-spells-first": (47, "A", INN_CAMP, r"\v\v\v\vVS\e" + PICK, "leaves"),
    "camp-view-twice": (47, "A", INN_CAMP, "VEV" + PICK, "leaves"),
    "commands-view": (47, "C", "", "V" + PICK, "stops"),
    "shop-view-first": (73, "A", ARMOURY, "V" + PICK, "stops"),
    "shop-view-twice": (73, "A", ARMOURY, "VEV" + PICK, "leaves"),
}
# Where the port stops, the original's recorded byte (None: it varies).
RECORDED = {"shop-view-first": 0x00, "commands-view": None}
STOP = "Escape at the first coin list tests an uninitialized byte (546c:2f9b)"
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
    keys = E.LOAD.format(game) + place + to_coins
    pair = E.Pair(keys + r"\scoins." + r"\e\safter.", work / name, saves=[saves])
    text = pair.text("after")
    if port_says == "stops":
        v.check(E.port_error(text) == STOP, f"{name}: the port stops at Escape",
                E.port_error(text) or "no stop")
        print(f"#{check} {name}: the port stops at Escape, as the README says: the original's "
              "recorded behaviour is asserted, not compared")
    else:
        port_does = outcome(pair.frame("after"))
        v.check(port_does == port_says and E.port_error(text) is None,
                f"{name}: the port {port_says} (its rule for this case)",
                f"{port_does}, {E.port_error(text)}")
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
        if port_says == "stops":
            want = RECORDED[name]
            if want is not None:
                v.check(b13c == want, f"{tag}: [bp-0x13c] {want:#04x} (recorded)", f"{b13c:#04x}")
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
    for name in args.case or CASES:
        run_case(v, name, saves, work, args)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
