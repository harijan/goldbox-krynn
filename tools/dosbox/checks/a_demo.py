#!/usr/bin/env python3
"""Check: the demonstration (README, Checks against the original: "At the
title screen, press no key: the demonstration (ECL2 block 57) should start
with Sir Ringwald, Varianna and Lyn Swiftfoot. Compare the close-up of the
green dragon ...; no `Continue Battle:`, no treasure, and the title again."
and "Wait 30 seconds at the title's menu: the demonstration should play;
press F10 there instead: it should play too ... After it the title should
show again and its menu read `Champions Of Krynn v1.2`.").

Run 1 (real time, seed 0, no key at all): the title's pictures and credits
run out, the menu waits (timed), the demonstration plays; the party is read
in memory as it joins; DOSBox-X's screen is taken as fast as it comes (two
or three a second) until the title's menu shows again after it. Each
different screen is matched against the port's for the same run, from a
copy of eclplay (a_common.delay_eclplay) that keeps every different screen
at the game's delays and the key reads' shots: equal to one, or each pixel
one of two near ones' (caught while drawing, or an animated picture's
other frame); the rest are listed. The close-up of the green dragon, the
frames CALL [6803] shows for 900 ms each after `monster: picture 10`, must
all be seen. Run 2 (turbo): the title skipped with a key, F10 at its menu:
the demonstration should start, and the menu after it is compared again.
"""
import argparse
import pickle
import re
import subprocess
import sys
import time

import a_common as A

cokbox = A.cokbox


def party(box):
    names = []
    rec = box.far(box.ds * 16 + 0x609A)
    while rec is not None and len(names) < 8:
        raw = box.read(rec, 16)
        names.append(raw[1:1 + min(raw[0], 15)].decode("latin-1"))
        rec = box.far(rec + 0x17F)
    return names


_MENU_ROWS = []


def menu_rows(work):
    """Row 24 of the title's menu as the port draws it, before and after a
    demonstration (`Champions of Krynn v1.2`, then `Of`)."""
    if not _MENU_ROWS:
        _, _, shots = cokbox.eclplay(r"\t", work / "menu-rows")
        for p in (shots[0], shots[-1]):
            _MENU_ROWS.append(cokbox.Screen.load(p).region(0, 192, 320, 8))
    return _MENU_ROWS


def at_title_menu(box, work=None):
    """The game waits at the title's menu: it polls for a key, and row 24 is
    the menu's (the call chain where() reads is only a hint)."""
    if not box.waiting():
        return False
    rows = menu_rows(work or box.workdir)
    try:
        return box.screenshot().region(0, 192, 320, 8) in rows
    except cokbox.BoxError:  # a screenshot missed (DOSBox-X busy): try again
        return False


def port_frames(work):
    """The port's frames for no key at all, in order: [(name, Screen)]: every
    different screen at the game's delays (a_common.delay_eclplay's
    ECLPLAY_FRAMES; the title's waits too, which poll in 10 ms delays), and
    eclplay's shots at key reads (the title's menu, the battle set up, the
    menu after the demonstration)."""
    exe = A.delay_eclplay(work / "tool")
    out = work / "port"
    for d in ("keys", "delays", "saves"):
        (out / d).mkdir(parents=True, exist_ok=True)
        for f in (out / d).iterdir():
            f.unlink()
    cfg = out / "krynn.cfg"
    cfg.write_bytes(cokbox.krynn_cfg("T").encode())
    raw = out / "frames.raw"
    env = dict(A.os.environ, ECLPLAY_FRAMES=str(raw), ECLPLAY_DELAY_SHOTS=str(out / "delays"))
    run = subprocess.run([str(exe), "--start-menu", "--config", str(cfg), "--saves",
                          str(out / "saves"), "--shots", str(out / "keys"), "--keys", r"\t",
                          str(cokbox.ROOT / "Assets")], env=env, capture_output=True, text=True,
                         timeout=600)
    (out / "log.txt").write_text(run.stdout + run.stderr)
    data = raw.read_bytes()
    frames = []
    for i in range(len(data) // 32000):
        chunk = data[i * 32000:(i + 1) * 32000]
        pixels = bytes(v for b in chunk for v in (b >> 4, b & 15))
        frames.append((f"frame {i}", cokbox.Screen(320, 200, pixels)))
    keys = sorted((out / "keys").glob("*.bmp"))
    frames += [("keys/" + k.name, cokbox.Screen.load(k)) for k in keys]
    print(f"port: {len(data) // 32000} different screens at delays, {len(keys)} at key reads; "
          + " | ".join(l for l in run.stdout.splitlines() if l.startswith(("party:", "combat: removed",
                                                                           "menu:", "error"))))
    return frames, run.stdout


def raw_shot(box):
    """DOSBox-X's screenshot as the PNG's bytes, not decoded (decoding takes
    longer than taking it): equal screens give equal files."""
    before = set(box.captures.glob("*.png"))
    box._hotkey("p")
    new = box._until(lambda: (set(box.captures.glob("*.png")) - before) or None, 10,
                     "the screenshot")
    png = sorted(new)[-1]
    box._until(lambda: cokbox._complete_png(png), 5, "the screenshot to be written")
    data = png.read_bytes()
    png.unlink()
    return data


def watch(box, work, seconds):
    """Different screens, as fast as DOSBox-X saves them, until the title's
    menu shows again after the party has gone: [(Screen, party names)]."""
    raw = []
    end = time.monotonic() + seconds
    joined = False
    while time.monotonic() < end and box.running:
        try:
            data = raw_shot(box)
        except cokbox.BoxError:
            continue
        names = party(box)
        joined = joined or "SIR RINGWALD" in names
        if not raw or data != raw[-1][0]:
            raw.append((data, names))
        if joined and not names and box.waiting():
            png = work / "menu2.png"
            png.write_bytes(data)
            if cokbox.Screen.load(png).region(0, 192, 320, 8) in menu_rows(work):
                break
    seen = []
    for k, (data, names) in enumerate(raw):
        png = work / "shot.png"
        png.write_bytes(data)
        shot = cokbox.Screen.load(png)
        if not seen or shot.pixels != seen[-1][0].pixels:
            seen.append((shot, names))
    return seen


def differing(x, y):
    """The offsets where two screens' pixels differ (in C: XOR as integers)."""
    n = len(x.pixels)
    xor = (int.from_bytes(x.pixels, "big") ^ int.from_bytes(y.pixels, "big")).to_bytes(n, "big")
    return [m.start() for m in re.finditer(rb"[^\x00]", xor)]


def composite(shot, candidates):
    """A pair (a, b) of CANDIDATES (index, Screen) such that each pixel of SHOT
    is a's or b's: a screen caught as the game draws (text running, a picture
    coming in) or as an animated picture shows another of its frames than
    when the port's frame was taken (the original animates in real time)."""
    for i, a in candidates:
        diff = differing(shot, a)
        for j, b in candidates:
            if j != i and all(shot.pixels[p] == b.pixels[p] for p in diff):
                return i, j
    return None


def match(seen, frames, work):
    """Match each screen with the port's frames: equal to one, or a composite
    of two near the last matched (see composite)."""
    exact = {}
    for i, (_, s) in enumerate(frames):
        exact.setdefault(s.pixels, i)
    last, matched, mixed, unmatched = None, 0, 0, []
    for n, (shot, names) in enumerate(seen):
        i = exact.get(shot.pixels)
        if i is not None:
            matched += 1
            if not frames[i][0].startswith("keys/"):
                last = i
            print(f"  screen {n:03d}: = port {frames[i][0]}; party {names[:3]}")
            continue
        window = range(len(frames)) if last is None else \
            range(max(0, last - 15), min(len(frames), last + 16))
        candidates = [(j, frames[j][1]) for j in window]
        if n:
            candidates.append((-1, seen[n - 1][0]))
        pair = composite(shot, candidates) if last is not None else None
        if pair:
            mixed += 1
            names_ = ["the screen before" if k < 0 else f"port {frames[k][0]}" for k in pair]
            print(f"  screen {n:03d}: each pixel {names_[0]}'s or {names_[1]}'s; "
                  f"party {names[:3]}")
            continue
        best = min(window, key=lambda j: len(differing(shot, frames[j][1])))
        count, area = shot.diff(frames[best][1])
        unmatched.append(n)
        shot.save_png(work / f"box-{n:03d}.png", highlight=frames[best][1])
        print(f"  screen {n:03d}: NO port frame; nearest {frames[best][0]} {count} pixels "
              f"in {area}; party {names[:3]}")
    return matched, mixed, unmatched


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--run", type=int, action="append", choices=(1, 2))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "demo")
    frames, log = port_frames(work)
    bad = 0
    if not args.run or 1 in args.run:
        print("run 1: no key, real time, seed 0")
        with cokbox.Box(workdir=work / "box1", seed=0, verbose=args.verbose, timeout=1500) as box:
            start = time.monotonic()
            box._until(lambda: at_title_menu(box, work), 300, "the title's menu", 0.3)
            t_menu = time.monotonic()
            ticks_menu = box.dword(0x46C)
            menu = box.screenshot(work / "menu1.png")
            menu1 = next(f for n, f in frames if n == "keys/000.bmp")
            print(f"  the title's menu after {t_menu - start:.1f} s: {menu.diff(menu1)[0]} pixels "
                  "differ from port keys/000.bmp")
            box._until(lambda: "SIR RINGWALD" in party(box), 120, "the demonstration's party",
                       0.05)
            print(f"  the party's first member joins {time.monotonic() - t_menu:.1f} s after "
                  f"the menu was seen ({(box.dword(0x46C) - ticks_menu) / 18.2065:.1f} s of the "
                  "BIOS's clock; no key)")
            seen = watch(box, work, 900)
        last = cokbox.Screen.load(work / "menu2.png")
        joined = max((names for _, names in seen), key=len)
        print(f"  party seen: {joined}")
        matched, drawing, unmatched = match(seen, frames, work)
        pickle.dump([(sh.pixels, names) for sh, names in seen], open(work / "seen.pickle", "wb"))
        menu2 = next(f for n, f in frames if n == "keys/002.bmp")
        count = last.diff(menu2)[0]
        print(f"  {len(seen)} different screens: {matched} equal to a port frame, {drawing} "
              f"composites of two, {len(unmatched)} neither ({unmatched}); the title's menu again: "
              f"{count} pixels differ from port keys/002.bmp")
        bad += count != 0 or joined[:3] != ["SIR RINGWALD", "VARIANNA", "LYN SWIFTFOOT"]
        # The close-up: the screens CALL [6803] shows for 900 ms each after
        # `monster: picture 10` (the port's delay shots), each seen in the original.
        lines = log.splitlines()
        start = lines.index("monster: picture 10")
        end = lines.index("monster: cleared", start)
        shots = [int(l.split()[1]) for l in lines[start:end] if l.startswith("delayshot: ")]
        shown = {sh.pixels for sh, _ in seen}
        missed = [k for k in shots if cokbox.Screen.load(
            work / "port" / "delays" / f"d{k:05d}.bmp").pixels not in shown]
        print(f"  the green dragon's close-up: {len(shots)} frames of 900 ms (port delay shots "
              f"{shots[0]}-{shots[-1]}), {len(shots) - len(missed)} seen in the original"
              + (f"; not seen: {missed}" if missed else ""))
        bad += len(missed) != 0
    if not args.run or 2 in args.run:
        print("run 2: a key at the title, F10 at its menu (turbo)")
        with cokbox.Box(workdir=work / "box2", seed=0, turbo=True, verbose=args.verbose,
                        timeout=900) as box:
            box.keys("x")
            box._until(lambda: at_title_menu(box), 60, "the title's menu", 0.05)
            box.key(0x4400)
            t = time.monotonic()
            names = box._until(lambda: len(party(box)) >= 3 and party(box), 60,
                               "the demonstration's party", 0.05)
            print(f"  F10 taken at {box.where()}... the party {names} has joined "
                  f"{time.monotonic() - t:.1f} s later")
            bad += names[:3] != ["SIR RINGWALD", "VARIANNA", "LYN SWIFTFOOT"]
            box._until(lambda: not party(box), 600, "the demonstration's end", 0.05)
            box._until(lambda: at_title_menu(box), 300, "the title's menu again", 0.02)
            last = box.screenshot(work / "menu2-f10.png")
            count = last.diff(frames[-1][1])[0]
            print(f"  the title's menu after the demonstration: {count} pixels differ from "
                  f"port {frames[-1][0]}")
            bad += count != 0
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
