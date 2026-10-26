#!/usr/bin/env python3
"""Death Knights of Krynn's Trade (580d:23a0), Escape at the first coin
list, in the original against eclplay --game death-knights --start-menu
(docs/camp.md, "View").

Trade's flag ([bp-0x13c], tested at 580d:26c0) is set only by an amount;
before one it holds what View's menu (6a69:0f1b, called 0x0c below Trade)
left at its [bp-0x130]: 6a69:0fee's high word of the time it began, in
hundredths of a second since midnight by the DOS clock (14e4:09bd, GetTime)
plus 30. Nonzero, Trade is left for View's menu; 0, only in the 655 seconds
after midnight, it asks for another partner.

Each flow loads saved game A, rests (block 16's camp), View for SIR
DRYDEN, Trade, the next character, and reads the byte in the original's
memory while the coin list waits (Trade's frame found by its callee's
return address), then Escape:

day: the BIOS's clock set to noon as camp's menu waits: the byte is the
high word's low byte of the DOS clock's hundredths
(+ 30) when View's menu began, and Trade is left: the coin list and View's
menu after Escape against the port's at the same key reads.

midnight: the BIOS's clock (0040:006c) set to a second past midnight as
camp's menu waits: the byte is 0 and the original asks for another partner
(the port, which has no clock of the day, takes any other time and leaves
Trade; not compared).

Seeded (Box seed, eclplay --seed). In run_all.py's Death Knights set (make
dkk-checks). About a minute.

    python3 tools/dosbox/checks/dkk_trade.py [--workdir DIR] [--seed N]
        [--flows day,midnight]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402

cokbox = checklib.cokbox

TRADE = (0x580D, 0x23A0, 0x26D0)
VIEW_MENU = (0x580D, 0x0E2D)      # View's return from its menu (6a69:0f1b)
# After Begin: block 16's two texts and Yes to rest (the camp), View, Trade,
# the next character (65cd:33c9's Select alone).
CAMP = [r"\r", r"\r", r"\r"]
TO_COINS = ["V", "T", r"\v\r"]
BIOS_CLOCK = 0x46C


def trade_frame(box):
    """Trade's BP while the coin list waits: the frame whose return address
    lies in Trade belongs to the routine it called, and holds Trade's BP."""
    st = box.hook_state()
    ss, bp = (st["ss"], st["bp"]) if st["waiting"] else (st["pollss"], st["pollbp"])
    for _ in range(14):
        up, ip, cs = struct.unpack("<HHH", box.read(ss * 16 + bp, 6))
        at = box.locate(cs, ip)
        if at and at[0] == TRADE[0] and TRADE[1] <= at[1] < TRADE[2]:
            return ss, up
        if up <= bp:
            break
        bp = up
    return None


def in_trade(box):
    """Whether the game waits inside Trade (its pick asked again)."""
    st = box.hook_state()
    ss, bp = (st["ss"], st["bp"]) if st["waiting"] else (st["pollss"], st["pollbp"])
    for _ in range(14):
        up, ip, cs = struct.unpack("<HHH", box.read(ss * 16 + bp, 6))
        at = box.locate(cs, ip)
        if at and at[0] == TRADE[0] and TRADE[1] <= at[1] < TRADE[2]:
            return True
        if at == VIEW_MENU or up <= bp:
            return False
        bp = up
    return False


def hundredths(ticks):
    """The DOS clock's time of day in hundredths of a second for the BIOS's
    tick count, as DOSBox's GetTime makes it (18.2065 ticks a second)."""
    return int(ticks * 100 / 18.206481) % 8640000


def flow(v, work, opts, name):
    seed = opts.seed
    keys = K.LOAD_A_BEGIN + "".join(CAMP + TO_COINS)
    saves = work / f"saves-{name}"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    port_coins = K.port(work, f"{name}-coins", keys, seed)[1]
    port_after = K.port(work, f"{name}-after", keys + r"\e", seed)[1]
    with cokbox.Box(workdir=work / f"box-{name}", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=600) as box:
        v.check(K.title(box), f"{name}: the title's four pictures")
        K.type_steps(box, K.LOAD_A_STEPS, settle=1.0)
        K.type_steps(box, CAMP, settle=1.0)
        # Noon by day (the host's clock could be just past midnight).
        noon = int(12 * 3600 * 18.206481)
        box.write_paused([(BIOS_CLOCK, struct.pack("<I", 18 if name == "midnight" else noon))])
        before = box.dword(BIOS_CLOCK)
        K.type_steps(box, TO_COINS, settle=1.0)
        after = box.dword(BIOS_CLOCK)
        coins = box.screenshot(work / f"dosbox-{name}-coins.png")
        frame = trade_frame(box)
        v.check(frame is not None, f"{name}: Trade's frame found", str(box.where()))
        if frame is None:
            return
        ss, bp = frame
        flag, high = box.byte(ss * 16 + bp - 0x13C), box.word(ss * 16 + bp - 0x13C)
        # The menu began between the two reads of the clock.
        lo, hi = (hundredths(before) + 30) >> 16, (hundredths(after) + 30) >> 16
        print(f"original, {name}: [bp-0x13c] {flag:#04x} (the word {high:#06x}); the DOS "
              f"clock {hundredths(before)}..{hundredths(after)} hundredths, high words "
              f"{lo:#x}..{hi:#x}")
        v.check(lo <= high <= hi, f"{name}: Trade's flag is the high word of the DOS clock's "
                "hundredths + 30 as View's menu began (6a69:0fee)",
                f"{high:#06x}, the clock's {lo:#x}..{hi:#x}")
        box.keys(r"\e")
        box.wait_input(settle=1.0)
        asks = in_trade(box)
        shown = box.screenshot(work / f"dosbox-{name}-after.png")
        print(f"original, {name}: after Escape {'asks again' if asks else 'leaves Trade'} "
              f"(waits at {', '.join(box.where())})")
    v.check(asks == (flag == 0), f"{name}: asks again exactly when the flag is 0 (580d:26c0)",
            f"flag {flag:#04x}, asks again {asks}")
    if name == "day":
        v.check(not asks, "day: Trade is left, as the port does", f"asks again {asks}")
        v.screen("day: the first coin list", coins,
                 cokbox.Screen.load(port_coins) if port_coins else None, work=work)
        v.screen("day: View's menu after Escape", shown,
                 cokbox.Screen.load(port_after) if port_after else None, work=work)
    else:
        v.check(flag == 0 and asks, "midnight: the flag 0, Trade asks again (the original's; "
                "the port, with no clock of the day, leaves Trade: not compared)",
                f"flag {flag:#04x}, asks again {asks}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_trade"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="day,midnight",
                        help="which flows: day, midnight (comma-separated)")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    for name in opts.flows.split(","):
        flow(v, work, opts, name)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
