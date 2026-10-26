#!/usr/bin/env python3
"""Checks #113, #114 and #115: the roster (docs/checks.md, Checks against the
original: "Add a roster character with 128 or more jewels: it should not be
listed. Make the first key of Add a space: Add should end. Add a character,
leave, and open Add again with a space: it may stay."; "Add a ranger to a
party with three rangers: `too many rangers in party`."; "Remove a character
and save it over an existing file, answering No to `Overwrite`: the
character should take the new file's name.").

The roster is written by eclplay (Create, seeded: BOB, JEWEL and CARL
fighters, RANGA-RANGD rangers), JEWEL.WHO's +0xf7 (the jewelry's low byte)
then set to 0x80. The original in DOSBox-X and eclplay run the same keys
from the title on copies of it; each screen is compared with the port's at
the same key read. A space in Add: the original leaves Add (it waits in the
start menu's list, 4def:0369, as batch A recorded: the "may stay" did not
happen in DOSBox-X), the port's screen as the original's. The fourth
ranger: the party in memory against the port's, and the message (shown for
DS:4b38 x 100 ms, the speed made 60 for that key in the original) caught
in the original's screens, each the port's at its delays, in order. The
roster files after Remove are compared.
"""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402
from a_create import create_keys  # noqa: E402

# It judges what the original shows as it comes: its boxes at their cycles
# (when it runs, not when another check imports it).
if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

cokbox = A.cokbox
ROSTER = [("BOB", 1), ("JEWEL", 1), ("CARL", 1), ("RANGA", 4), ("RANGB", 4), ("RANGC", 4),
          ("RANGD", 4)]   # human (row 6) fighters (class row 1) and rangers (4)
ADD = r"\v\r"             # from the start menu's first row, with no party
START_MENU = "4def:0369"  # the start menu's list waits here

CASES = {
    # 113: JEWEL is not listed.
    "jewels": r"\kxP" + ADD + r"\slist.\e\sback.",
    # 113: a space as Add's first key.
    "space": r"\kxP" + ADD + r"\slist. \safter-space.",
    # 113: Add BOB, leave, Add again, a space.
    "space-again": r"\kxP" + ADD + r"\r\e\sleft." + r"\v\v\v\r\slist2. \safter-space.",
    # 114: three rangers, then a fourth (the rangers are rows 4-7 without JEWEL).
    "rangers": r"\kxP" + ADD + r"\v\v\r\v\r\v\r\srangers3.\v\r\sfourth.\e\sback.",
    # 115: BOB added, removed (row 6) and saved as DAVE over BOB.WHO.
    "remove": r"\kxP" + ADD + r"\r\e\sadded.\v\v\v\v\r\r\soverwrite.N\snew-name.DAVE\r\sremoved.",
}


def make_roster(work):
    """Create the roster with eclplay; return its directory."""
    keys = r"\kxP" + "".join(create_keys(n, 6, c, None, 0, 0) for n, c in ROSTER)
    _, saves, _ = cokbox.eclplay(checklib.port_keys(keys), work / "make-roster", shots=False,
                                 args=["--seed", "3"])
    roster = work / "roster"
    shutil.rmtree(roster, ignore_errors=True)
    roster.mkdir(parents=True)
    for f in saves.iterdir():
        shutil.copyfile(f, roster / f.name)
    who = bytearray((roster / "JEWEL.WHO").read_bytes())
    who[0xF7] = 0x80
    (roster / "JEWEL.WHO").write_bytes(who)
    print("roster:", " ".join(sorted(p.name for p in roster.iterdir())))
    return roster


def message_frames(pair, keys):
    """The port's screens from the key read where the fourth ranger's Enter
    is typed to the next: there, at each delay and each line logged."""
    out, _, shots = cokbox.eclplay(checklib.port_keys(keys), pair.work / "message-port",
                                   binary=A.probe_eclplay(delays=True), **pair.kw)
    reads = [int(line.split()[3]) for line in out.splitlines() if line.startswith("aprobe: read ")]
    first = reads[-2]
    return [cokbox.Screen.load(p) for p in shots if int(p.stem) >= first]


def judge_burst(v, seen, frames):
    """Each of the original's screens SEEN, taken as fast as they come, is
    one of FRAMES at or after the previous one's, or caught while the game
    drew from one frame to the next (each pixel one of the two's)."""
    at = 0
    for n, shot in enumerate(seen):
        hit = next((i for i in range(at, len(frames)) if frames[i].pixels == shot.pixels), None)
        if hit is None:
            hit = next((i for i in range(at, len(frames) - 1) if all(
                p in (a, b) for p, a, b in zip(shot.pixels, frames[i].pixels,
                                               frames[i + 1].pixels))), None)
            v.check(hit is not None, f"the fourth ranger, screen {n} after Enter",
                    "caught while drawing between port frames "
                    f"{hit} and {hit + 1}" if hit is not None else
                    "none of the port's frames there, nor between two of them in a row")
            at = hit if hit is not None else at
        else:
            v.ok(f"the fourth ranger, screen {n} after Enter", f"port frame {hit} of {len(frames)}")
            at = hit
    return at


def run_case(v, name, keys, work, roster, verbose):
    print(f"{name}: keys {keys}")
    pair = A.Pair(keys, work / name, saves=[roster], verbose=verbose)
    print("  port: " + " | ".join(line for line in pair.out.strip().splitlines()[-30:]
                                  if not line.startswith("item: ")))
    where, seen = {}, []
    with pair:
        rest = keys
        while r"\s" in rest:
            head, _, tail = rest.partition(r"\s")
            mark, _, rest = tail.partition(".")
            if mark == "fourth":
                # The message shows for speed x 100 ms: the original's speed
                # (DS:4b38, 4) made 60 for this one key, its screens taken
                # as fast as they come.
                pair.run(head[:-2])
                speed = pair.box.ds_byte(0x4B38)
                pair.box.ds_write(0x4B38, bytes([60]))
                pair.box.wait_input()
                pair.box.push_key(cokbox.key_word("\r"))
                for _ in range(14):
                    s = pair.box.screenshot()
                    if not seen or s.pixels != seen[-1].pixels:
                        s.save_png(pair.work / f"message-{len(seen)}.png")
                        seen.append(s)
                pair.box.ds_write(0x4B38, bytes([speed]))
                head = ""
            pair.run(head)
            pair.shot(mark)
            where[mark] = pair.box.where()
            if mark == "fourth":
                party = [A.name_of(raw) for raw, _ in A.party_records(pair.box)]
        if rest:
            pair.run(rest)
        orig = pair.box_files()
    for mark in pair.marks():
        pair.judge(v, mark, what=f"{name}, screen {mark}")
    if name.startswith("space"):
        v.check(START_MENU in where["after-space"], f"{name}: the space left Add in the "
                "original (recorded)", f"it waits at {where['after-space']}")
    if name == "rangers":
        probe = pair.probes()["fourth"]
        v.value("the party after the fourth ranger", party,
                [A.name_of(raw) for raw, _ in probe.members])
        frames = message_frames(pair, keys[:keys.index(r"\sfourth.")])
        reads = (frames[0].pixels, frames[-1].pixels)
        at = judge_burst(v, seen, frames)
        v.check(any(s.pixels not in reads and s.pixels in {f.pixels for f in frames}
                    for s in seen), "the message seen in the original, as the port shows it",
                f"{len(seen)} screens after Enter, {len(frames)} port frames; the last matched {at}")
    if name == "remove":
        port = pair.port_files()
        for label, files in (("original", orig), ("port", port)):
            who = files.get("DAVE.WHO")
            print(f"  {label}: files {sorted(files)}; DAVE.WHO's name "
                  f"{A.name_of(who) if who else None}")
        A.judge_files(v, orig, port, A.mask_roster, what="remove: file")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "roster")
    roster = make_roster(work)
    v = checklib.Verdict()
    for name, keys in CASES.items():
        if not args.case or name in args.case:
            run_case(v, name, keys, work, roster, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
