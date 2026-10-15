#!/usr/bin/env python3
"""Check: the sound against DOSBox-X's machine=tandy (README, Checks against
the original: "The sound (see Sound) against a Tandy 1000, or DOSBox with
machine=tandy and KRYNN.CFG's second line T (then P for the PC speaker; and
a file with LF line ends, which should give no sound): record the title's
music and a few sounds ... and compare them with eclplay --wav"; #4, the
sounds a record read cuts).

DOSBox-X's Tandy sound is MAME's SN76496 code, as the port's model is, so
agreement here checks the drivers' writes and their timing (pitches, tempo,
which sounds, how long), not the chip: the noise's character, the loudness
and the clicks are MAME's on both sides and need a real Tandy 1000.

Each part runs the original in DOSBox-X with F12+W recording (48 kHz
stereo) and eclplay --wav (44.1 kHz mono; built from the port's source
with a line giving the time of each sound call, nothing else changed) and
compares them in seconds (f_wav.py). Judged, and only that:

- title-T, title-P: the title's music with no key (to its menu), on the
  Tandy's chip and on the speaker: the pitches (the strongest spectral
  peaks of 45 ms frames every 0.25 s: 90 % of the frames with sound in
  both) and the tempo (the lag of successive 4-second windows within 0.1 s
  over the music: DOSBox-X's WAV keeps its emulated time only to a few
  tenths of a percent when it runs slower than real time, see `clock`).
  The music's length is the title's own (its pictures load in emulated
  disk and CPU time in DOSBox-X, in none in eclplay): printed, not judged.
- lf: KRYNN.CFG with LF line ends: device 5 (DS:577a) in the original and
  no sound in either.
- battle, 3D: sounds in a stretch of play, a pause of 1.5 s at each key in
  both; the sounds (stretches above a threshold) matched in order by their
  lengths (within 15 ms). A sound left unmatched must be explained exactly:
  between the same matched neighbours, each of the port's sounds there
  holds two commands or more landing within 100 ms of each other (by the
  game's clock, which gives the drawing no time, each cuts the one before:
  README Sound > Time), the original has sounds there too, and they last no
  less in all than the port's (the same commands spread over its drawing
  time play longer before the next cuts them). Anything else fails. #4:
  in the battle's first 10 s, a command followed at once by a record read
  (the step that meets the caravan) is silent in the port, and the
  original's step sounds one driver interrupt at most (the timer may come
  between the command and the read); those 10 s are not matched further.
- clock (not run by default): DOSBox-X's WAV against the BIOS's tick count,
  the measure of the WAV's error used above.
"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import f_lib  # noqa: E402
import f_wav  # noqa: E402

cokbox = checklib.cokbox

DS_DEVICE, DS_MUSIC = 0x577A, 0x5780
HOP = 0.005
RATE = 44100
SAME_LENGTH = 0.015    # s: sounds of the same length (three envelope steps)
MERGE = 0.1            # s: commands landing this close cut each other at once
MAX_DRIFT = 0.1        # s: the tempo's drift allowed over the title's music
ONE_TICK = 0.02        # s: one driver interrupt (1/54.6 s) at the envelope's steps

SOUND_LOG = ('        if (call == COK_SOUND_COMMAND)\n'
             '            printf("call: command %u\\n", value);\n'
             '        else\n'
             '            printf("call: %s\\n", names[call]);\n')


def sound_times(name, text):
    """src/eclplay.c: --sound-calls lines with the sample each call lands at
    (`call: NAME VALUE at SAMPLE`)."""
    if SOUND_LOG not in text:
        raise cokbox.BoxError("src/eclplay.c's --sound-calls lines changed: update f_sound")
    return text.replace(SOUND_LOG, '        printf("call: %s %u at %llu\\n", names[call], value, '
                                   '(unsigned long long)p->frames);\n')


def cfg_bytes(sound, eol="\r\n"):
    return cokbox.krynn_cfg("T", sound).replace("\r\n", eol).encode("latin-1")


def eclplay_wav(keys, out, cfg, saves=(), seed=None):
    """eclplay --start-menu --wav on KEYS: (the WAV's path, the calls as
    [(name, value, seconds)])."""
    out.mkdir(parents=True, exist_ok=True)
    (out / "krynn.cfg").write_bytes(cfg)
    savedir = out / "saves"
    if savedir.exists():
        shutil.rmtree(savedir)
    savedir.mkdir()
    for s in saves:
        for f in pathlib.Path(s).iterdir():
            if f.is_file():
                shutil.copyfile(f, savedir / f.name)
    wav = out / "eclplay.wav"
    exe = checklib.build("eclplay", transforms=[("eclplay.c", sound_times)])
    args = [str(exe), "--start-menu", "--config", str(out / "krynn.cfg"), "--saves",
            str(savedir), "--wav", str(wav), "--sound-calls"]
    if seed is not None:
        args += ["--seed", str(seed)]
    res = subprocess.run(args + ["--keys", keys, str(cokbox.ROOT / "Assets")],
                         capture_output=True, text=True, timeout=600, check=True)
    (out / "eclplay.txt").write_text(res.stdout + res.stderr)
    calls = []
    for line in res.stdout.splitlines():
        if line.startswith("call: "):
            words = line[6:].rsplit(" ", 3)
            calls.append((words[0], int(words[1]), int(words[3]) / RATE))
    return wav, calls


def title(v, device, work, verbose):
    """The title's music, no key pressed, to the title's menu."""
    work.mkdir(parents=True, exist_ok=True)
    ewav, _ = eclplay_wav("", work / "eclplay", cfg_bytes(device))
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", sound=device, cfg=cfg_bytes(device), wav=bwav,
                    verbose=verbose, timeout=300) as box:
        box._until(lambda: box.ds_byte(DS_MUSIC) != 0, 120, "the title's music")
        t0 = time.monotonic()
        box._until(lambda: box.ds_byte(DS_MUSIC) == 0, 200, "the title's music to end", 0.02)
        print(f"  title-{device}: DS:5780 set for {time.monotonic() - t0:.1f} s of host time; "
              f"device DS:577a {box.ds_byte(DS_DEVICE)}")
        time.sleep(1.5)
    compare_music(v, f"title-{device}", ewav, bwav)


def compare_music(v, name, ewav, bwav):
    re_, e = f_wav.read(ewav)
    rb, b = f_wav.read(bwav)
    env_e, env_b = f_wav.envelope(re_, e, HOP), f_wav.envelope(rb, b, HOP)
    on_e, on_b = f_wav.onset(env_e, 0.003), f_wav.onset(env_b, 0.003)
    if not v.check(on_e is not None and on_b is not None, f"{name}: music in both",
                   f"onsets eclplay {on_e}, DOSBox-X {on_b}"):
        return
    lag0 = on_b - on_e
    lag, r = f_wav.best_lag(env_e, env_b, lag0 - 40, lag0 + 40, on_e, min(2000, len(env_e) - on_e))
    offset = lag * HOP
    print(f"  {name}: music starts at {on_e * HOP:.3f} s (eclplay) and {on_b * HOP:.3f} s "
          f"(DOSBox-X); best offset {offset:.3f} s (correlation {r:.3f})")
    snd_e = f_wav.sounds(env_e, HOP, 0.003, 0.5)
    snd_b = f_wav.sounds(env_b, HOP, 0.003, 0.5)
    end_e = snd_e[-1][0] + snd_e[-1][1] if snd_e else 0
    end_b = snd_b[-1][0] + snd_b[-1][1] if snd_b else 0
    print(f"  {name}: the music lasts {end_e - on_e * HOP:.2f} s (eclplay), "
          f"{end_b - on_b * HOP:.2f} s (DOSBox-X): the title's own length, not judged (its "
          "pictures load in DOSBox-X's emulated time, in none in eclplay)")
    windows = f_wav.tempo(env_e, env_b, lag, HOP, 4.0, 0.25)
    lags = [w[1] for w in windows if w[2] > 0.5]
    print(f"  {name}: offsets of 4-second windows {[(w[0], w[1]) for w in windows]}")
    drift = (max(lags) - min(lags)) if lags else None
    v.check(len(lags) >= 5 and drift <= MAX_DRIFT,
            f"{name}: the tempo (the offsets of 4-second windows within {MAX_DRIFT} s: "
            "DOSBox-X's WAV error)", f"{drift if drift is None else round(drift, 3)} s over "
            f"{len(lags)} windows")
    times = [on_e * HOP + 0.1 + 0.25 * i for i in range(int((end_e - on_e * HOP) / 0.25) - 1)]
    rd_e, d_e = f_wav.decimate(re_, e, 2)
    rd_b, d_b = f_wav.decimate(rb, b, 2)
    agree, both, differ = f_wav.compare_pitches(rd_e, d_e, rd_b, d_b, offset, times)
    v.check(both >= 100 and agree >= 0.9 * both,
            f"{name}: the pitches (the strongest peak within 3 % in 90 % of the frames)",
            f"{agree} of {both}; some that differ (t, eclplay Hz, DOSBox-X Hz): {differ}")


def lf(v, work, verbose):
    """KRYNN.CFG with LF line ends: one line to Turbo Pascal, device 5."""
    work.mkdir(parents=True, exist_ok=True)
    cfg = cfg_bytes("T", "\n")
    ewav, _ = eclplay_wav("", work / "eclplay", cfg)
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", cfg=cfg, wav=bwav, verbose=verbose, timeout=300) as box:
        box.wait_input()
        device = box.ds_byte(DS_DEVICE)
        time.sleep(20)
        music = box.ds_byte(DS_MUSIC)
    peaks = []
    for path in (ewav, bwav):
        _, s = f_wav.read(path)
        peaks.append(max((abs(x) for x in s), default=0.0))
    print(f"  lf: DS:5780 {music} 20 s into the title")
    v.check(device == 5, "lf: the original's device DS:577a is 5 (none)", f"{device}")
    v.check(peaks[1] == 0.0, "lf: no sound in the original", f"peak sample {peaks[1]:.5f}")
    v.check(peaks[0] == 0.0, "lf: no sound in the port", f"peak sample {peaks[0]:.5f}")


def clock(v, work, verbose, seconds=150):
    """DOSBox-X's WAV against its own emulated time: with KRYNN.CFG's LF line
    ends (device 5) the game never reprograms the timer, so the BIOS's tick
    count (0040:006c, 1193181.8 / 65536 a second) measures the emulated time
    the recording spans; a WAV short of it plays the drivers' tempo faster.
    A measure of the harness: nothing judged."""
    work.mkdir(parents=True, exist_ok=True)
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", cfg=cfg_bytes("T", "\n"), wav=bwav, verbose=verbose,
                    timeout=seconds + 120) as box:
        t0 = box.dword(0x46C)
        time.sleep(seconds)
        t1 = box.dword(0x46C)
    rate, s = f_wav.read(bwav)
    emulated = (t1 - t0) * 65536 / 1193181.8
    print(f"  clock: the WAV lasts {len(s) / rate:.3f} s, the BIOS counted {t1 - t0} ticks "
          f"({emulated:.3f} s) between the recording's start and end: ratio "
          f"{len(s) / rate / emulated:.5f}")


# The effects runs: keys typed at once to a place (HEAD), then each of TAIL
# after a pause of 1.5 s at the wait (eclplay: \p150. before each), so the
# sounds are isolated and no key read cuts one short in eclplay.
SEED = 1
PAUSE = 1.5
EFFECT_RUNS = {
    # Game A to the overland map, Move, the steps to the caravan (CALL [b203]
    # steps: sound 9, the last cut by the battle's record read), its text and
    # battle (4 baaz): Done, Guard each turn (the baaz' steps, hits, a drop).
    # The first step meets the caravan: its record read comes at once; the
    # battle's first sound comes after LEAD seconds of the tail in both (its
    # text and setup).
    "battle": dict(throtl=False, lead=10.0,
                   head=f_lib.load_letter("A") + r"\r\r\r\r\r\r\rLNM",
                   tail=[r"\>", r"\>", r"\^", r"\r"] + ["D", "G"] * 10),
    # The port's save by Throtl: Move, a step away and back (sound 9 on the
    # overland map), Throtl's street, Move, steps and turns in 3D (sound 9).
    "3D": dict(throtl=True, head=f_lib.load_letter("D"),
               tail=["m", "2", "8", "y", "m", r"\^", r"\^", r"\>", r"\<", r"\>", r"\>"]),
}


def stretch(calls, start):
    """The calls from START seconds on, timed from there."""
    return [(n, val, t - start) for n, val, t in calls if t >= start - 1e-9]


def effects(v, name, work, verbose):
    work.mkdir(parents=True, exist_ok=True)
    spec = EFFECT_RUNS[name]
    saves = f_lib.throtl_save(work, "D", beaten=True) if spec["throtl"] else checklib.SAVES
    kw = dict(saves=[saves], args=["--seed", str(SEED)])
    pause = r"\p%d." % round(PAUSE * 100)
    bwav = work / "dosbox.wav"
    with cokbox.Box(workdir=work / "box", sound="T", cfg=cfg_bytes("T"), wav=bwav,
                    saves=[saves], seed=SEED, verbose=verbose, timeout=900) as box:
        box.keys(spec["head"])
        box.wait_input(settle=1.0)
        at_head = box.screenshot(work / "head.png")
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
    # The port at the head's end: the original's screen there (a loaded game
    # by Throtl: the known `resume` difference, the port then declining).
    extra = ""
    if spec["throtl"]:
        extra = f_lib.resumed(v, f"{name}: the game loaded", at_head, spec["head"],
                              work / "resumed", **kw)
    else:
        v.screen(f"{name}: the screen at the tail's start", at_head,
                 checklib.frame_after(spec["head"], work / "head", **kw),
                 fixed=lambda: checklib.frame_after(spec["head"], work / "head-fixed",
                                                    fixed=True, **kw), work=work)
    head = spec["head"] + extra
    # eclplay's WAV of the head alone gives where the tail starts (less the
    # second it adds at the end). After the tail a last Escape after the
    # pause, and a pause after it: eclplay renders no time for the pause
    # before the last key in the keys at the 3D view's menu (the run ends
    # there at once), which would cut the last sound.
    hwav, _ = eclplay_wav(head, work / "eclplay-head", cfg_bytes("T"), [saves], seed=SEED)
    rh, h = f_wav.read(hwav)
    tail_start = len(h) / rh - 1.0
    ekeys = head + "".join(pause + k for k in spec["tail"]) + pause + r"\e" + pause
    ewav, calls = eclplay_wav(ekeys, work / "eclplay", cfg_bytes("T"), [saves], seed=SEED)
    calls = stretch(calls, tail_start)
    print(f"  {name}: the port's commands in the tail (value, s): "
          f"{[(val, round(t, 3)) for n, val, t in calls if n == 'command']}")
    re_, e = f_wav.read(ewav)
    e = e[int(tail_start * re_):]
    rb, b = f_wav.read(bwav)
    found = []
    for who, rate, data in (("eclplay", re_, e), ("DOSBox-X", rb, b)):
        env = f_wav.envelope(rate, data, HOP)
        out = [(round(start, 3), round(length, 3), round(peak, 3))
               for start, length, peak in f_wav.sounds(env, HOP, 0.002, 0.1)]
        found.append(out)
        print(f"  {name}: {who}: {len(out)} sounds (start s, length s, peak RMS): {out}")
    port, orig = found
    if spec.get("lead"):
        cut_by_record(v, name, calls, re_, e, port, orig, spec["lead"])
        port = [s for s in port if s[0] >= spec["lead"]]
        orig = [s for s in orig if s[0] >= spec["lead"]]
        calls = [c for c in calls if c[2] >= spec["lead"]]
    pairs = f_wav.match(port, orig, lambda x, y: abs(x[1] - y[1]) <= SAME_LENGTH)
    print(f"  {name}: {len(pairs)} sounds matched by length (eclplay/DOSBox-X): "
          f"{', '.join(f'{port[i][1]:.3f}/{orig[j][1]:.3f}' for i, j in pairs)}")
    v.check(len(pairs) >= 1, f"{name}: sounds matched by length", f"{len(pairs)}")
    unmatched(v, name, port, orig, pairs, calls)


def commands_in(port, calls):
    """The port's commands of each of its sounds PORT (start, length, peak):
    those after the end of the sound before it, up to its own end (a
    command's sound starts a few driver ticks after it: the first often
    writes 0)."""
    out, after = [], float("-inf")
    for start, length, _ in port:
        out.append([t for n, _, t in calls if n == "command" and after < t <= start + length])
        after = start + length
    return out


def unmatched(v, name, port, orig, pairs, calls):
    """Each stretch of unmatched sounds between the same matched neighbours
    explained exactly (see the docstring), or a difference."""
    commands = commands_in(port, calls)
    anchors = [(-1, -1)] + list(pairs) + [(len(port), len(orig))]
    for (i0, j0), (i1, j1) in zip(anchors, anchors[1:]):
        ps, os_ = port[i0 + 1:i1], orig[j0 + 1:j1]
        if not ps and not os_:
            continue
        what = (f"{name}: unmatched sounds eclplay {[(s[0], s[1]) for s in ps]}, "
                f"DOSBox-X {[(s[0], s[1]) for s in os_]}")
        groups = commands[i0 + 1:i1]
        merged = all(len(g) >= 2 and max(g) - min(g) <= MERGE + 1e-6 for g in groups)
        total_p, total_o = sum(s[1] for s in ps), sum(s[1] for s in os_)
        good = bool(ps) and bool(os_) and merged and total_o >= total_p - SAME_LENGTH
        v.check(good, what, f"the port's {sum(len(g) for g in groups)} commands there land "
                f"within {MERGE * 1000:.0f} ms in each of its sounds ({[len(g) for g in groups]}"
                f" commands), cutting each other by the game's clock (README Sound > Time); "
                f"the original's {len(os_)} sounds last {total_o:.3f} s, the port's "
                f"{total_p:.3f} s" if good else
                f"not explained: the port's commands in its sounds {groups}; lengths in all "
                f"{total_p:.3f} s (port), {total_o:.3f} s (original)")


def cut_by_record(v, name, calls, re_, e, port, orig, lead):
    """#4 in the first LEAD seconds of the tail: the port's commands followed
    at once by a record read are silent in its WAV, and it has no sound
    there; the original's sounds there (one a step at most) last one driver
    interrupt at most: the record read's command 0 (169c:088e) stops the
    step before its sound starts unless the timer's interrupt (54.6 Hz)
    comes between the two, in emulated time as on a machine."""
    cut = [t for (n, _, t), (n2, _, t2) in zip(calls, calls[1:])
           if n == "command" and n2 == "record" and t2 == t and t < lead]
    v.check(bool(cut), f"{name}: the port reads a record at once after a command (the step "
            "that meets the caravan)", f"at {[round(t, 3) for t in cut]} s")
    for t in cut:
        piece = e[int(t * re_):int((t + 0.2) * re_)]
        v.check(max((abs(x) for x in piece), default=0.0) == 0.0,
                f"{name}: the port's command at {t:.3f} s, a record read at once, makes no sound")
    lead_p = [s for s in port if s[0] < lead]
    lead_o = [s for s in orig if s[0] < lead]
    v.check(not lead_p, f"{name}: the port's first {lead} s silent", f"{lead_p}")
    v.check(len(lead_o) <= len(cut) and all(s[1] <= ONE_TICK for s in lead_o),
            f"{name}: the original's step before a record read cut within one driver "
            f"interrupt ({ONE_TICK * 1000:.0f} ms at most)", f"its sounds in the first {lead} s: "
            f"{[(s[0], s[1]) for s in lead_o]}")


PARTS = ["title-T", "title-P", "lf", "battle", "3D"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append", choices=PARTS + ["clock"])
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "sound")
    v = checklib.Verdict()
    for part in args.part or PARTS:
        if part.startswith("title-"):
            title(v, part[-1], work / part, args.verbose)
        elif part == "lf":
            lf(v, work / part, args.verbose)
        elif part == "clock":
            clock(v, work / part, args.verbose)
        else:
            effects(v, part, work / part, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
