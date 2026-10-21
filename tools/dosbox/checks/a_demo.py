#!/usr/bin/env python3
"""Checks #107 and #108: the demonstration (DEVELOPERS.md, Checks against the
original: "At the title screen, press no key: the demonstration (ECL2 block
57) should start with Sir Ringwald, Varianna and Lyn Swiftfoot. Compare the
close-up of the green dragon ...; no `Continue Battle:`, no treasure, and
the title again." and "Wait 30 seconds at the title's menu: the
demonstration should play; press F10 there instead: it should play too ...
After it the title should show again and its menu read `Champions Of Krynn
v1.2`.").

The port's run with no key (eclplay, `\\t` letting the menus run out) gives
its screens in order: a shot at each key read, at each of the game's delays
and at each line it logs (a_common.probe_eclplay(delays=True)).

Run 1 (real time, seed 0, no key at all): the title's menu against the
port's at its first key read, the menu's timeout (DS:6e11) against the
port's, and the time from the menu to the first NPC joining, 30 to 35 s of
the BIOS's clock (the port's \\t does not time it). Then the Tandy's
screen read from memory every 50 ms (checklib.watch) until the title's menu
shows again: each must be the port's in order (an exact frame, at or after
the previous screen's), or caught while drawing (each pixel one of the
port's frames between the frames of the screens before and after it, or
in the area where they change, or black where they agree: judge_watch);
the party read in memory against the port's `party:` lines; the green dragon's
close-up, the 12 frames CALL [6803] shows for 900 ms each after `monster:
picture 10`, each seen in order; the menu after it and its timeout against
the port's second key read. Run 2 (turbo): the title skipped with a key, F10
at its menu: the party against the port's for the same keys, and the menu
after the demonstration against the port's at the same point.
"""
import argparse
import bisect
import hashlib
import pathlib
import re
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

# It judges what the original shows as it comes: its boxes at their cycles
# (when it runs, not when another check imports it).
if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

cokbox = A.cokbox
CONFIG = cokbox.krynn_cfg("T").encode()
PARTY = ["SIR RINGWALD", "VARIANNA", "LYN SWIFTFOOT"]


def party(box):
    return [A.name_of(raw) for raw, _ in A.party_records(box, 16)]


_MENU_ROWS = []


def menu_rows(work):
    """Row 24 of the title's menu as the port draws it, before and after a
    demonstration (`Champions of Krynn v1.2`, then `Of`)."""
    if not _MENU_ROWS:
        _, _, shots = cokbox.eclplay(r"\t", pathlib.Path(work) / "menu-rows")
        for p in (shots[0], shots[-1]):
            _MENU_ROWS.append(cokbox.Screen.load(p).region(0, 192, 320, 8))
    return _MENU_ROWS


def at_title_menu(box, work=None):
    """The game waits at the title's menu: it polls for a key, and row 24 is
    the menu's (the call chain where() reads is only a hint)."""
    if not box.waiting():
        return False
    try:
        return box.screenshot().region(0, 192, 320, 8) in menu_rows(work or box.workdir)
    except cokbox.BoxError:  # a screenshot missed (DOSBox-X busy): try again
        return False


def port_sequence(work):
    """The port's screens for no key at all, in order, consecutive equal
    ones once: [(label, Screen)], label `read N` (a key read or a battle's
    map), `delay N MS` or `log N`; its screens at the key reads; and its
    output."""
    out, shots = A.run_probe(A.probe_eclplay(delays=True), r"\t", work / "port", CONFIG,
                             shots=True)
    kinds = {}
    for line in out.splitlines():
        if line.startswith("adelay: "):
            n, ms = map(int, line.split()[1:3])
            kinds[n] = f"delay {n} {ms}"
        elif line.startswith("alog: "):
            n = int(line.split()[1])
            kinds[n] = f"log {n}"
    seq, reads, cache, last = [], [], {}, None
    for path in shots:
        n = int(path.stem)
        data = path.read_bytes()
        if n not in kinds:
            reads.append(cokbox.Screen.load(path))
        if data != last:
            digest = hashlib.sha1(data).digest()
            if digest not in cache:
                cache[digest] = cokbox.Screen.load(path)
            seq.append((kinds.get(n, f"read {n}"), cache[digest]))
            last = data
        path.unlink()   # 8,000 shots: keep only the distinct screens, in memory
    print(f"port: {len(shots)} shots, {len(seq)} screens in order ({len(cache)} different)")
    return seq, reads, out


def in_order(seen, seq):
    """The longest run of SEEN's screens equal to SEQ's frames in order (a
    frame at or after the previous one's): [frame index or None]."""
    where = {}
    for i, (_, s) in enumerate(seq):
        where.setdefault(s.pixels, []).append(i)
    tails, nodes, back = [], [], {}
    for k, shot in enumerate(seen):
        for i in sorted(where.get(shot.pixels, ()), reverse=True):
            pos = bisect.bisect_left(tails, i)
            back[(k, i)] = nodes[pos - 1] if pos else None
            if pos == len(tails):
                tails.append(i)
                nodes.append((k, i))
            else:
                tails[pos], nodes[pos] = i, (k, i)
    match = [None] * len(seen)
    node = nodes[-1] if nodes else None
    while node:
        match[node[0]] = node[1]
        node = back[node]
    return match


def differing(x, y):
    """The offsets where two screens' pixels differ."""
    n = len(x.pixels)
    xor = (int.from_bytes(x.pixels, "big") ^ int.from_bytes(y.pixels, "big")).to_bytes(n, "big")
    return [m.start() for m in re.finditer(rb"[^\x00]", xor)]


# The share of the original's screens that must be port frames in order:
# the recorded runs had 89-91 % (120 of 132, 247 of 276, 375 of 423, 353
# of 387), the rest caught while the game drew.
EXACT_SHARE = 0.8


def interval(match, k, seq):
    lo = max((m for m in match[:k] if m is not None), default=0)
    hi = min((m for m in match[k + 1:] if m is not None), default=len(seq) - 1)
    return lo, hi


def unexplained(shot, seq, lo, hi):
    """The pixels of SHOT that none of the port's frames LO-HI has."""
    frames = [s.pixels for _, s in seq[lo:hi + 1]]
    return [p for p in differing(shot, seq[lo][1]) if all(f[p] != shot.pixels[p] for f in frames)]


def redrawn(seq, lo, hi, pixels):
    """Those of PIXELS in the area the port redraws between its frames LO
    and HI (the box around the pixels that change among them): a picture
    drawn over another may keep a pixel's colour, which the original
    passes through another colour to reach."""
    frames = seq[lo:hi + 1]
    changed = set()
    for a, b in zip(frames, frames[1:]):
        changed.update(differing(a[1], b[1]))
    if not changed:
        return []
    xs, ys = [p % 320 for p in changed], [p // 320 for p in changed]
    return [p for p in pixels
            if min(xs) <= p % 320 <= max(xs) and min(ys) <= p // 320 <= max(ys)]


def cleared(shot, seq, lo, hi, pixels):
    """Those of PIXELS black (0) in SHOT and the same in each of the port's
    frames LO-HI: the original's screens before and after it, which are
    frames LO and HI, show them as the port does, and between the two the
    game clears the area and draws it again alike (cok_text_clear or a
    fill before a panel's text, a combatant's turn redrawing its panel),
    with no frame of the port's in between. The "or black" kept to that:
    black, and unchanged by the port around it. The area cleared may
    reach into the area the port redraws (see redrawn) and past it: the
    whole screen before the battle's frame is drawn, its top border
    unchanged; a combatant's whole panel before its hit points are written
    again, its name unchanged. judge_watch takes each pixel by itself."""
    frames = [s.pixels for _, s in seq[lo:hi + 1]]
    return [p for p in pixels
            if shot.pixels[p] == 0 and all(f[p] == frames[0][p] for f in frames)]


def realign(match, k, seen, seq):
    """Screen K unmatched equals a port frame J a little past its interval
    (the port presents J before the frames its neighbours matched, the
    original after): take J for K when the neighbours J pushes out of order
    are each then a composite of the frames around them. Returns whether it
    did."""
    lo, hi = interval(match, k, seq)
    for j in range(lo + 1, min(len(seq), hi + 12)):
        if seq[j][1].pixels != seen[k].pixels:
            continue
        trial = list(match)
        trial[k] = j
        out = [i for i in range(k + 1, len(seen)) if trial[i] is not None and trial[i] <= j]
        for i in out:
            trial[i] = None
        if all(not unexplained(seen[i], seq, *interval(trial, i, seq)) for i in out):
            match[:] = trial
            return True
    return False


def judge_watch(v, seen, seq, work):
    """Each screen the original showed against the port's sequence: equal to
    a port frame in order (in_order); or caught while drawing, as it went
    from the port's frame LO to HI (those of the screens before and after
    it): each pixel one of the frames LO-HI's (a composite), or, where it is
    none of them, a pixel in the area the port redraws between LO and HI (the
    port presents no frame in the middle of a drawing: a picture coming in,
    the battle's map scrolling, an icon moving cell by cell, row 24 cleared
    and written; a read of the screen comes at any moment of it), or a
    black pixel where frames LO-HI all agree (cleared, see cleared). Any
    other pixel none of the frames has differs."""
    match = in_order(seen, seq)
    for k in range(len(seen)):
        if match[k] is None and unexplained(seen[k], seq, *interval(match, k, seq)):
            if realign(match, k, seen, seq):
                print(f"  screen {k:03d}: port frame {match[k]}, which the port presents before "
                      "the frames of the screens after it")
    exact = composite = drawing = 0
    for k, shot in enumerate(seen):
        if match[k] is not None:
            exact += 1
            print(f"  screen {k:03d}: = port {seq[match[k]][0]}")
            continue
        lo, hi = interval(match, k, seq)
        left = unexplained(shot, seq, lo, hi)
        span = f"port frames {lo}-{hi} ({seq[lo][0]} to {seq[hi][0]})"
        if not left:
            composite += 1
            print(f"  screen {k:03d}: caught while drawing, each pixel one of {span}")
            continue
        xs, ys = [p % 320 for p in left], [p // 320 for p in left]
        area = (min(xs), min(ys), max(xs), max(ys))
        shot.save_png(work / f"box-{k:03d}.png", highlight=seq[lo][1])
        inside = redrawn(seq, lo, hi, left)
        if len(inside) == len(left):
            drawing += 1
            print(f"  screen {k:03d}: caught while drawing, {len(left)} pixels in {area} none of "
                  f"{span}, all in the area the port redraws between them (box-{k:03d}.png)")
            continue
        outside = set(left) - set(inside)
        if len(cleared(shot, seq, lo, hi, outside)) == len(outside):
            drawing += 1
            print(f"  screen {k:03d}: caught between a clear and its redrawing, {len(left)} "
                  f"pixels in {area} none of {span}: {len(outside)} black, which they all show "
                  f"alike, {len(inside)} in the area the port redraws between them "
                  f"(box-{k:03d}.png)")
            continue
        v.differ(f"screen {k:03d} of the demonstration",
                 f"{len(left)} pixels in {area} are none of {span}, and the port draws nothing "
                 f"there between them: box-{k:03d}.png")
    v.check(exact >= EXACT_SHARE * len(seen), "the demonstration's screens are the port's frames "
            f"in order ({EXACT_SHARE:.0%} at least)", f"{len(seen)} screens: {exact} port frames "
            f"in order, {composite} composites of the frames around them, {drawing} caught "
            "drawing elsewhere")
    return match


def close_up(v, seen, match, seq, log):
    """The green dragon's close-up: the 900 ms frames after `monster:
    picture 10`, each seen in the original in order."""
    lines = log.splitlines()
    start = lines.index("monster: picture 10")
    end = lines.index("monster: cleared", start)
    shots = [int(line.split()[1]) for line in lines[start:end]
             if line.startswith("adelay: ") and line.split()[2] == "900"]
    labels = [f"delay {n} 900" for n in shots]
    index = {label: i for i, (label, _) in enumerate(seq)}
    # A frame equal to the one before is not in the sequence (shown once).
    frames = sorted({index[lab] for lab in labels if lab in index})
    shown = {m for m in match if m is not None}
    missed = [i for i in frames if i not in shown]
    v.check(len(shots) == 12 and not missed, "the green dragon's close-up: every 900 ms frame seen "
            "in order", f"{len(shots)} frames of 900 ms (port shots {shots[0]}-{shots[-1]}), "
            f"{len(frames) - len(missed)} of the {len(frames)} different seen"
            + (f"; not seen: {[seq[i][0] for i in missed]}" if missed else ""))


def joined(log):
    """The names the port's list takes, in order, each once: the party's
    NPCs (`party: NAME joins`), then the battle's monsters (`monster: N
    NAME, icon ...`)."""
    names = []
    for m in re.finditer(r"^(?:party: (.*) joins|monster: \d+ (.*), icon)", log, re.M):
        name = m.group(1) or m.group(2)
        if name not in names:
            names.append(name)
    return names


def run1(v, work, seq, reads, log, verbose):
    probes = [A.Probe(block) for block in re.split(r"(?=^aprobe: read )", log, flags=re.M)
              if block.startswith("aprobe: read ")]
    print("run 1: no key, real time, seed 0")
    names = []

    def over():
        now = party(box)
        for n in now:
            if n not in names:
                names.append(n)
        return bool(names) and not now and at_title_menu(box, work)

    with cokbox.Box(workdir=work / "box1", seed=0, verbose=verbose, timeout=3000) as box:
        box._until(lambda: at_title_menu(box, work), 300, "the title's menu", 0.3)
        ticks = box.dword(0x46C)
        menu = box.screenshot(work / "menu1.png")
        v.screen("the title's menu", menu, reads[0], work=work)
        v.value("the menu's timeout (DS:6e11, hundredths)", box.ds_word(0x6E11),
                probes[0].get("timeout"))
        box._until(lambda: "SIR RINGWALD" in party(box), 120, "the demonstration's party", 0.05)
        secs = (box.dword(0x46C) - ticks) / 18.2065
        v.check(30 <= secs <= 35, "the demonstration starts 30-35 s after the menu (#108)",
                f"{secs:.1f} s of the BIOS's clock from the menu seen to SIR RINGWALD joining "
                "(recorded 32.6: the 30 s timeout and the block's load; the port's time is "
                "not compared: eclplay's \\t runs the menu out at once)")
        seen = checklib.watch(box, 2400, out=work / "watch", prefix="screen", until=over)
        box._until(lambda: at_title_menu(box, work), 60, "the title's menu again", 0.05)
        last = box.screenshot(work / "menu2.png")
        v.value("the menu's timeout again (DS:6e11)", box.ds_word(0x6E11),
                probes[-1].get("timeout"))
    v.value("the list as it fills: the party, then the battle's monsters", names,
            joined(log))
    v.check(names[:3] == PARTY, "the party is " + ", ".join(PARTY), f"{names}")
    match = judge_watch(v, seen, seq, work)
    close_up(v, seen, match, seq, log)
    v.screen("the title's menu after the demonstration", last, reads[-1], work=work)


def run2(v, work, verbose):
    print("run 2: a key at the title, F10 at its menu (turbo)")
    keys = r"\kx\smenu.\#44\safter."
    port = checklib.frames_at(keys, work / "port2")
    log, _, _ = cokbox.eclplay(checklib.port_keys(keys), work / "port2-all", shots=False)
    with cokbox.Box(workdir=work / "box2", seed=0, turbo=True, verbose=verbose,
                    timeout=900) as box:
        box.keys("x")
        box._until(lambda: at_title_menu(box, work), 60, "the title's menu", 0.05)
        v.screen("F10: the title's menu", box.screenshot(work / "menu-f10.png"), port["menu"],
                 work=work)
        box.key(0x4400)
        names = box._until(lambda: len(party(box)) >= 3 and party(box), 60,
                           "the demonstration's party", 0.05)
        v.value("F10: the demonstration's party", names[:3], joined(log)[:3])
        box._until(lambda: not party(box), 600, "the demonstration's end", 0.05)
        box._until(lambda: at_title_menu(box, work), 300, "the title's menu again", 0.02)
        v.screen("F10: the title's menu after the demonstration",
                 box.screenshot(work / "menu2-f10.png"), port["after"], work=work)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--run", type=int, action="append", choices=(1, 2))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "demo")
    for old in [*work.glob("box-*.png"), *work.glob("watch/*.png")]:
        old.unlink()
    (work / "watch").mkdir(parents=True, exist_ok=True)
    shutil.rmtree(work / "port", ignore_errors=True)
    v = checklib.Verdict()
    if not args.run or 1 in args.run:
        seq, reads, log = port_sequence(work)
        run1(v, work, seq, reads, log, args.verbose)
    if not args.run or 2 in args.run:
        run2(v, work, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
