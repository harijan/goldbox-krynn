#!/usr/bin/env python3
"""Check: the sound against DOSBox-X's machine=tandy (README, Checks against
the original: "The sound (see Sound) against a Tandy 1000, or DOSBox with
machine=tandy and KRYNN.CFG's second line T (then P for the PC speaker; and
a file with LF line ends, which should give no sound): record the title's
music and a few sounds ... and compare them with eclplay --wav").

DOSBox-X's Tandy sound is MAME's SN76496 code, as the port's model is, so
agreement here checks the drivers' writes and their timing (pitches, tempo,
which sounds, how long), not the chip: the noise's character, the loudness
and the clicks are MAME's on both sides and need a real Tandy 1000.

Parts (each runs the original in DOSBox-X with F12+W recording, 48 kHz
stereo, and eclplay --wav, 44.1 kHz mono, and compares them in seconds; see
f_wav.py):

- title-T, title-P: the title's music with no key (to its menu), on the
  Tandy's chip and on the speaker: the music's start found by its onset,
  the tempo by the lag of successive 4-second windows (constant: the same
  tempo), the pitches by the strongest spectral peaks of 45 ms frames every
  0.25 s, and the length (the title cuts the music: DS:5780 cleared).
- lf: KRYNN.CFG with LF line ends: the device byte DS:577a and silence.
- effects: game A's party on the overland map, three steps (CALL [b203],
  sound 9) to the caravan, its battle's first rounds with every player
  turn Done, Guard (the baaz' steps, hits, misses), with a pause of 1.5 s
  at each key in both, so no key read cuts a sound short: the sounds'
  onsets, lengths and pitches in order.
"""
import argparse
import os
import pathlib
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402
import f_wav  # noqa: E402

os.environ.setdefault("COKBOX_DISPLAYS", "160-169")

DS_DEVICE, DS_MUSIC = 0x577A, 0x5780
HOP = 0.005


def cfg_bytes(sound, eol="\r\n"):
    return cokbox.krynn_cfg("T", sound).replace("\r\n", eol).encode("latin-1")


def eclplay_wav(keys, out, cfg, saves=(), seed=None):
    out.mkdir(parents=True, exist_ok=True)
    (out / "krynn.cfg").write_bytes(cfg)
    wav = out / "eclplay.wav"
    args = [str(cokbox.ROOT / "build/eclplay"), "--start-menu", "--config", str(out / "krynn.cfg"),
            "--saves", str(out / "saves"), "--wav", str(wav), "--sound-calls"]
    if seed is not None:
        args += ["--seed", str(seed)]
    savedir = out / "saves"
    savedir.mkdir(exist_ok=True)
    for s in saves:
        for f in pathlib.Path(s).iterdir():
            if f.is_file():
                (savedir / f.name).write_bytes(f.read_bytes())
    import subprocess
    res = subprocess.run(args + ["--keys", keys, str(cokbox.ROOT / "Assets")],
                         capture_output=True, text=True, timeout=600)
    (out / "eclplay.txt").write_text(res.stdout + res.stderr)
    return wav, res.stdout


def title(device, work, verbose):
    """The title's music, no key pressed, to the title's menu."""
    work.mkdir(parents=True, exist_ok=True)
    ewav, etext = eclplay_wav("", work / "eclplay", cfg_bytes(device))
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", sound=device, cfg=cfg_bytes(device), wav=bwav,
                    verbose=verbose, timeout=300) as box:
        box._until(lambda: box.ds_byte(DS_MUSIC) != 0, 120, "the title's music")
        t0 = time.monotonic()
        box._until(lambda: box.ds_byte(DS_MUSIC) == 0, 200, "the title's music to end", 0.02)
        print(f"title-{device}: DS:5780 set for {time.monotonic() - t0:.1f} s of host time; "
              f"device DS:577a {box.ds_byte(DS_DEVICE)}")
        time.sleep(1.5)
    return compare_music(f"title-{device}", ewav, bwav)


def compare_music(name, ewav, bwav):
    re_, e = f_wav.read(ewav)
    rb, b = f_wav.read(bwav)
    env_e, env_b = f_wav.envelope(re_, e, HOP), f_wav.envelope(rb, b, HOP)
    on_e, on_b = f_wav.onset(env_e, 0.003), f_wav.onset(env_b, 0.003)
    if on_e is None or on_b is None:
        print(f"{name}: no sound (eclplay onset {on_e}, DOSBox-X onset {on_b})")
        return False
    lag0 = on_b - on_e
    lag, r = f_wav.best_lag(env_e, env_b, lag0 - 40, lag0 + 40, on_e, min(2000, len(env_e) - on_e))
    offset = lag * HOP
    print(f"{name}: music starts at {on_e * HOP:.3f} s (eclplay) and {on_b * HOP:.3f} s "
          f"(DOSBox-X); best offset {offset:.3f} s (correlation {r:.3f})")
    snd_e = [s for s in f_wav.sounds(env_e, HOP, 0.003, 0.5)]
    snd_b = [s for s in f_wav.sounds(env_b, HOP, 0.003, 0.5)]
    end_e = snd_e[-1][0] + snd_e[-1][1] if snd_e else 0
    end_b = snd_b[-1][0] + snd_b[-1][1] if snd_b else 0
    print(f"{name}: the music lasts {end_e - on_e * HOP:.2f} s (eclplay), "
          f"{end_b - on_b * HOP:.2f} s (DOSBox-X)")
    windows = f_wav.tempo(env_e, env_b, lag, HOP, 4.0, 0.25)
    lags = [w[1] for w in windows if w[2] > 0.5]
    drift = (max(lags) - min(lags)) if lags else None
    print(f"{name}: offsets of 4-second windows {[(w[0], w[1]) for w in windows]}")
    print(f"{name}: tempo: offsets within {drift:.3f} s over {len(lags)} windows" if lags
          else f"{name}: tempo: no window correlates")
    times = [on_e * HOP + 0.1 + 0.25 * i for i in range(int((end_e - on_e * HOP) / 0.25) - 1)]
    rd_e, d_e = f_wav.decimate(re_, e, 2)
    rd_b, d_b = f_wav.decimate(rb, b, 2)
    agree, both, differ = f_wav.compare_pitches(rd_e, d_e, rd_b, d_b, offset, times)
    print(f"{name}: pitches: {agree} of {both} frames agree (strongest peak within 3%); "
          f"some that differ (t, eclplay Hz, DOSBox-X Hz): {differ}")
    # The music's length is the title's (its pictures load in emulated disk
    # and CPU time in DOSBox-X, in none in eclplay): reported, not compared.
    # DOSBox-X's WAV keeps its emulated time only to a few tenths of a percent
    # when it runs slower than real time (see clock): a drift up to 0.1 s
    # over the 35 s passes.
    ok = bool(lags) and drift <= 0.1 and both and agree >= 0.9 * both
    print(f"{name}: {'AGREES' if ok else 'DIFFERS'}")
    return ok


def lf(work, verbose):
    """KRYNN.CFG with LF line ends: one line to Turbo Pascal, device 5."""
    work.mkdir(parents=True, exist_ok=True)
    cfg = cfg_bytes("T", "\n")
    ewav, etext = eclplay_wav("", work / "eclplay", cfg)
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", cfg=cfg, wav=bwav, verbose=verbose, timeout=300) as box:
        box.wait_input()
        device = box.ds_byte(DS_DEVICE)
        time.sleep(20)
        music = box.ds_byte(DS_MUSIC)
    peaks = []
    for path in (ewav, bwav):
        rate, s = f_wav.read(path)
        peaks.append(max(abs(v) for v in s) if s else 0.0)
    print(f"lf: DS:577a {device} (5: none), DS:5780 {music} 20 s into the title; "
          f"peak sample eclplay {peaks[0]:.5f}, DOSBox-X {peaks[1]:.5f}")
    ok = device == 5 and peaks[0] < 1e-3 and peaks[1] < 1e-3
    print(f"lf: {'AGREES' if ok else 'DIFFERS'}")
    return ok


def clock(work, verbose, seconds=150):
    """DOSBox-X's WAV against its own emulated time: with KRYNN.CFG's LF line
    ends (device 5) the game never reprograms the timer, so the BIOS's tick
    count (0040:006c, 1193181.8 / 65536 a second) measures the emulated time
    the recording spans; a WAV short of it plays the drivers' tempo faster."""
    work.mkdir(parents=True, exist_ok=True)
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", cfg=cfg_bytes("T", "\n"), wav=bwav, verbose=verbose,
                    timeout=seconds + 120) as box:
        t0 = box.dword(0x46C)
        time.sleep(seconds)
        t1 = box.dword(0x46C)
    rate, s = f_wav.read(bwav)
    emulated = (t1 - t0) * 65536 / 1193181.8
    ratio = len(s) / rate / emulated
    print(f"clock: the WAV lasts {len(s) / rate:.3f} s, the BIOS counted {t1 - t0} ticks "
          f"({emulated:.3f} s) between the recording's start and end: ratio {ratio:.5f}")
    return True


# The effects runs: keys typed at once to a place (HEAD), then each of TAIL
# after a pause of 1.5 s at the wait (eclplay: \p150. before each), so the
# sounds are isolated and no key read cuts one short in eclplay.
SEED = 1
PAUSE = 1.5
LOAD_A = r"\kxP\v\v\rA\v\v\v\v\v\r"
EFFECT_RUNS = {
    # Game A to the overland map, Move, the steps to the caravan (CALL [b203]
    # steps: sound 9), its text and battle (4 baaz): Done, Guard each turn
    # (the baaz' steps, hits, misses, a drop).
    "battle": dict(saves=None, head=LOAD_A + r"\r\r\r\r\r\r\rLNM", ecl_head="",
                   tail=[r"\>", r"\>", r"\^", r"\r"] + ["D", "G"] * 10),
    # The port's save by Throtl (f_display_modes.py's): Move, a step away and
    # back (sound 9 on the overland map), Throtl's street, Move, steps and
    # turns in 3D (sound 9).
    "3D": dict(saves="throtl", head=r"\kxP\v\v\rD\v\v\v\v\v\r", ecl_head="n",
               tail=["m", "2", "8", "y", "m", r"\^", r"\^", r"\>", r"\<", r"\>", r"\>"]),
}


def effects(name, work, verbose):
    work.mkdir(parents=True, exist_ok=True)
    spec = EFFECT_RUNS[name]
    if spec["saves"] == "throtl":
        import f_display_modes
        saves = f_display_modes.port_save(work)
    else:
        saves = cokbox.ROOT / "SAVE"
    pause = r"\p%d." % round(PAUSE * 100)
    head = spec["head"] + spec["ecl_head"]
    # eclplay's WAV of the head alone gives where the tail starts (less the
    # second it adds at the end).
    hwav, _ = eclplay_wav(head, work / "eclplay-head", cfg_bytes("T"), [saves], seed=SEED)
    rh, h = f_wav.read(hwav)
    tail_start = len(h) / rh - 1.0
    # A last Escape after the pause, which eclplay takes only after the pause
    # has passed (its keys running out would end the run at once, cutting the
    # last sound with the overlays' unhook).
    ekeys = head + "".join(pause + k for k in spec["tail"]) + pause + r"\e"
    ewav, etext = eclplay_wav(ekeys, work / "eclplay", cfg_bytes("T"), [saves], seed=SEED)
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", sound="T", cfg=cfg_bytes("T"), wav=bwav,
                    saves=[saves], seed=SEED, verbose=verbose, timeout=900) as box:
        box.keys(spec["head"])
        box.wait_input()
        box.record_sound()  # a new file from here: the tail
        time.sleep(0.3)
        box.record_sound()
        for k in spec["tail"]:
            box.wait_input()
            time.sleep(PAUSE)
            box.keys(k)
        box.wait_input()
        time.sleep(PAUSE)
        box.screenshot(work / "end.png")
    lines = etext.splitlines()
    calls = [line.split()[-1] for line in lines if line.startswith("call: command")]
    print(f"effects {name}: eclplay's commands (all): {' '.join(calls)}")
    re_, e = f_wav.read(ewav)
    e = e[int(tail_start * re_):]
    rb, b = f_wav.read(bwav)
    found = []
    for who, rate, data in (("eclplay", re_, e), ("DOSBox-X", rb, b)):
        env = f_wav.envelope(rate, data, HOP)
        out = [(round(start, 3), round(length, 3), round(peak, 3))
               for start, length, peak in f_wav.sounds(env, HOP, 0.002, 0.1)]
        found.append(out)
        print(f"effects {name}: {who}: {len(out)} sounds (start s, length s, peak RMS): {out}")
    # The sounds in order, matched by their lengths (within 15 ms: three
    # steps of the envelope); the steps' noise has no pitch to compare, and
    # the time between them is the original's drawing and disk time, which
    # eclplay's clock does not have.
    # #4: the first sound's onset (a step followed by a record read, as the
    # caravan's, makes none in either if the read cuts it at once).
    print(f"effects {name}: first sound at {found[0][0][0] if found[0] else None} s (eclplay), "
          f"{found[1][0][0] if found[1] else None} s (DOSBox-X) after the tail's start")
    pairs = f_wav.match(found[0], found[1], lambda x, y: abs(x[1] - y[1]) <= 0.015)
    lone_e = [x for i, x in enumerate(found[0]) if i not in {p[0] for p in pairs}]
    lone_b = [x for j, x in enumerate(found[1]) if j not in {p[1] for p in pairs}]
    print(f"effects {name}: {len(pairs)} sounds matched by length "
          f"({', '.join(f'{found[0][i][1]:.3f}/{found[1][j][1]:.3f}' for i, j in pairs)}); "
          f"eclplay's alone {lone_e}; DOSBox-X's alone {lone_b}")
    ok = not lone_e and not lone_b
    print(f"effects {name}: " + ("AGREES" if ok else
          "some sounds unmatched: commands close together merge differently, eclplay's "
          "clock having no drawing or disk time (README Sound > Time); see the results"))
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append",
                        choices=["title-T", "title-P", "lf", "clock", "battle", "3D"])
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-sound-")).resolve()
    ok = True
    for part in args.part or ["title-T", "title-P", "lf", "clock", "battle", "3D"]:
        if part.startswith("title-"):
            ok &= title(part[-1], work / part, args.verbose)
        elif part == "lf":
            ok &= lf(work / part, args.verbose)
        elif part == "clock":
            clock(work / part, args.verbose)
        else:
            ok &= effects(part, work / part, args.verbose)
    print(f"files in {work}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
