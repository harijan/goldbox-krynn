#!/usr/bin/env python3
"""Death Knights of Krynn's title song (MUSIC.DAX record 1, docs/sound.md
"Death Knights' songs") in the original against eclplay --game
death-knights --wav, as f_sound.py's title-T does for Champions (the
title's waits four times as fast under the song, in both: the song stops
at the title's menu, 51 s in), on the
Tandy's chip (machine=tandy, DKK.CFG's T, no Sound Blaster: the driver
finds no AdLib, then the Tandy's BIOS) and on the AdLib (machine=ega, a
Sound Blaster with an OPL2, DKK.CFG's A). Judged: the music in both, the
tempo and the pitches (f_sound.compare_music). The driver's device (its
DS:022e) is read from the original's memory and printed. In real time:
about four minutes.

    python3 tools/dosbox/checks/dkk_sound.py [--workdir DIR] [--devices T,A]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import f_sound  # noqa: E402

if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

cokbox = checklib.cokbox
ROOT = cokbox.ROOT

# dkk DS:9896: 2 while a song is started (15dc:003d, 16fc:001f); DS:9889,
# 988b the driver's paragraph-aligned far pointer (15dc:00d3).
DS_STATE, DS_DRIVER = 0x9896, 0x9889
SONG_SECONDS = 80  # MUSIC.DAX's song 1: 5,376 interrupts at 72.8 a second, 74 s
# The tempo's drift allowed over the 51 s compared (f_wav's envelopes at
# each WAV's own rate): four runs gave 0.0-0.035 s; a divisor off by one
# (225 for 224, 0.44 %) would give 0.23 s.
MAX_DRIFT = 0.05

DEVICES = {
    "T": dict(machine="tandy", conf={"sblaster.sbtype": "none", "sblaster.oplmode": "none"}),
    "A": dict(machine="ega", conf={"sblaster.sbtype": "sb16", "sblaster.oplmode": "opl2"}),
}


def eclplay_wav(letter, display, out):
    """eclplay --start-menu with no keys: the title, its song to the end."""
    out.mkdir(parents=True, exist_ok=True)
    cfg = out / "dkk.cfg"
    cfg.write_bytes(cokbox.dkk_cfg(display, letter).encode("latin-1"))
    saves = out / "saves"
    saves.mkdir(exist_ok=True)
    wav = out / "eclplay.wav"
    res = subprocess.run([str(ROOT / "build" / "eclplay"), "--game", "death-knights",
                          "--start-menu", "--config", str(cfg), "--saves", str(saves),
                          "--wav", str(wav), "--keys", "", str(ROOT / "DKKAssets")],
                         capture_output=True, text=True, timeout=600)
    (out / "eclplay.txt").write_text(res.stdout + res.stderr)
    return wav


def title(v, letter, work, verbose):
    spec = DEVICES[letter]
    work.mkdir(parents=True, exist_ok=True)
    display = cokbox.MACHINES[spec["machine"]][1]
    ewav = eclplay_wav(letter, display, work / "eclplay")
    bwav = work / "dosbox.wav"
    with cokbox.Box(game="death-knights", workdir=work / "box", machine=spec["machine"],
                    sound=letter, wav=bwav, extra_conf=spec["conf"], verbose=verbose,
                    timeout=SONG_SECONDS + 200) as box:
        box._until(lambda: box.ds_byte(DS_STATE) == 2, 120, "the title's song")
        # DS:9896 is set before the driver's function 0 runs (16fc:001f).
        time.sleep(1.0)
        seg = box.ds_word(DS_DRIVER + 2) + (box.ds_word(DS_DRIVER) >> 4)
        device = box.word((seg - 0x10 + 0x2d1) * 16 + 0x22e)
        print(f"  title-{letter}: the driver's device (its DS:022e) {device}")
        time.sleep(SONG_SECONDS)
    v.check(device == {"T": 1, "A": 2}[letter], f"title-{letter}: the driver's device",
            f"{device}")
    f_sound.compare_music(v, f"title-{letter}", ewav, bwav, MAX_DRIFT)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--workdir", type=pathlib.Path, default=ROOT / "build" / "dkk_sound")
    ap.add_argument("--devices", default="T,A")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    v = checklib.Verdict()
    for letter in args.devices.split(","):
        title(v, letter, args.workdir / f"title-{letter}", args.verbose)
    return v.finish()


if __name__ == "__main__":
    checklib.run(main)
