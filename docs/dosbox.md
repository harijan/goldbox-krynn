# DOSBox-X harness

Part of the [developer notes](../DEVELOPERS.md).

`tools/dosbox/cokbox.py` runs the original game in DOSBox-X, unattended,
for the checks below: Python 3's standard library, `dosbox-x` (Debian's
2025.02.01), `Xvfb` and `xdotool`, which the dev container installs. Debian
ships `dosbox-x` with the `cap_net_raw` file capability (for its NE2000's
pcap), which a rootless container cannot grant, so running it fails with
`operation not permitted` until `sudo setcap -r /usr/bin/dosbox-x` drops it
(the dev container does). It is not part of `make test`; `make
dosbox-selftest` boots the game in Tandy, EGA and CGA mode to the title's
menu and compares the SSI picture and the menu with `eclplay`'s frames
(about 15 seconds; 0 pixels differ in each).

Each run (`Box`) copies `COKAssets/` into a scratch drive as `C:\COK`, never
writing into `COKAssets/`, with a `KRYNN.CFG` written for it (the display's
letter for the machine: `machine=tandy` with `T`, `ega` (or `vga`,
`svga_s3`) with `E`, `cga` with `C`; a Tandy told `E` hangs after the note
to Tandy users; sound `S` by default) or none (the setup), and saved games
copied into `C:\COK\SAVE` (the file's save path), such as `SAVE/`'s or
the port's: `eclplay --saves DIR` writes games the original loads, so a
check can start at a place and party the port's scripts reach (a camp's
Save, then the start menu's Load Saved Game and Begin Adventuring, which
goes on where it was saved). It starts its own `Xvfb` and `dosbox-x`
(core normal, `cycles=fixed 20000`, no EMS, XMS or UMBs, `nosound` unless
recording), runs `HOOK.COM`, then `START.EXE` with the arguments given
(`Woof`, `x Helm`), and kills both when it ends, after `timeout` seconds
at most whatever the game does (`-time-limit` too). `pre_commands` are DOS
commands the autoexec runs in `C:\COK` before `START.EXE` (`ATTRIB +H
KRYNN.CFG`), `files` more files written there, `mapper` a mapper file's
text in place of DOSBox-X's default bindings. Under a heavy load DOSBox-X's
SDL now and then falls back to its offscreen driver and shows no window:
the run then starts again, up to three times. Runs side by side keep
apart: `COKBOX_DISPLAYS=LO-HI` (or `display_number`) gives `Xvfb` the
first free display number of that range, skipping one while its lock file
is there, removing a lock whose server is gone, and keeping a number only
once its lock names our own `Xvfb` (two runs may start on one together);
without it `Xvfb` (`-displayfd`) takes the first number from `:0` up
whose socket is free, so never one that another run holds (nor, unless
that many run at once, a range such as 110-189 kept for
`COKBOX_DISPLAYS`). Then:

- Memory: the guest's RAM is one block of `dosbox-x`'s heap, found by
  the keyboard buffer's bounds (`0040:0080`) and the interrupt table, and
  read and written through `/proc/PID/mem` (the Debian build has no
  debugger): `read`, `write`, and `ds_read`, `ds_write` at the game's data
  segment, which is found by the first 64 bytes of `START.EXE`'s (unpacked
  as `tools/ovrmerge.py` does), the PSP being `DS - 0x0bd6`. Video memory
  lies outside that block: `find_text` finds the 80-column text screen by
  a string on it, `text_rows` reads it (a CGA's or Tandy's character and
  attribute in turn; DOSBox-X keeps an `svga_s3`'s spread over 8 bytes a
  cell, which it finds too). `write_paused` writes with the emulation
  paused (F12+Pause until the title says `PAUSED`, so between two
  instructions): code the game is running changes safely only so.
  `d_lib.trap` makes a routine spin (`EB FE` over its first two bytes) and
  `untrap` gives them back that way: written while the game spun there,
  the CPU could fetch the spin's `EB` with the routine's second byte, a
  jump into another routine, which now and then ended `d_patrol`'s and
  `d_gate`'s battles at once.
- Waiting for a key: `HOOK.COM` (`tools/dosbox/hook.asm`, assembled into
  `cokbox.py`), a TSR, hooks INT 16h and counts in a block the host finds
  by its signature the reads (functions 0 and 0x10, with a flag while one
  waits in the BIOS) and the polls (1 and 0x11) that found no key, with
  their call sites and frames. The game waits when `Crt.ReadKey` waits, or
  when `KeyPressed` keeps finding nothing (two samples running) at a call
  site other than the flush's (`1614:0467`) and the key read's look before
  its `ReadKey` (`1614:026c`): a menu's, the title's or a rest's poll
  (`1614:03c2`); but not a caller that polls once and goes on
  (`NOT_WAIT_CALLERS`: the computer's turn's look for Space, `3afb:1200`,
  known by its bytes wherever its overlay lies), whose polls would
  otherwise take keys typed for the next menu. `where()` names the call
  site and its callers as `START_FULL.EXE` addresses (an overlay's found
  by its bytes), e.g. `1614:03cd`, `67b5:058e`, `1000:0199` at the title's
  menu.
- Keys: `keys(TEXT)` types `eclplay`'s escapes (`\r \e \b \^ \v \< \>
  \@X \cX \#HH \dC`, `\[...\]` typed ahead, `\pN.` a pause, and `\sNAME.`
  a screenshot) one at a time as the game waits, by putting each key's
  word in the BIOS's buffer (`0040:001E`) and waiting for the game to take
  it, so that no flush loses it; a key also ends the title's waits, as
  `\k` does in `eclplay`, and a timed menu runs out with a pause
  (`\p3100.`). `press` presses keys through X with `xdotool` instead, for
  what only the BIOS's keyboard handler sees (Ctrl-Break, the shift
  flags); DOSBox-X's own keys are F12 and another (F12+P its screenshot,
  F12+W its recording).
- Screens: `screenshot()` takes DOSBox-X's (F12+P), a PNG of the mode's
  pixels (320×200 in the Tandy's and CGA's modes, 640×400 in EGA's, the
  text mode at the adapter's own), read back as a `Screen` of colour
  indices at 320×200 (640 wide for text), the nearest of the 16 colours, as
  `eclplay --shots` BMPs load; `diff` counts the pixels that differ and
  their box, `save_png` marks them. About 20 a second on an idle host,
  ten times fewer on a loaded one. Each PNG is deleted once read (DOSBox-X
  numbers them up to 999 only). In the Tandy's 320×200 mode `vram()` reads
  the screen from the guest's RAM instead, in microseconds whatever the
  load (DOSBox-X keeps a Tandy's video memory there, the game's page at
  `0x98000`; `tandy_screen` decodes it), the same pixels as the
  screenshot's (`make dosbox-selftest` compares them); the other machines'
  video memory lies outside the guest's RAM. `checklib.watch` and
  `d_lib.burst` read it so every 50 ms (`checklib.READ_SECONDS`): a read
  is the screen at one moment, where a screenshot is DOSBox-X's frame,
  whose rows it draws as the emulated beam passes them, each at its own
  moment (one taken while the game cleared a combatant's panel and wrote
  it again showed the panel's rows above a line as before, then cleared,
  and those below written again).
- Sound: `wav=FILE` records from the start to the end (F12+W, 48 kHz
  stereo), with `KRYNN.CFG`'s second line `T` or `P`.
- Randomness: `seed=N` sets `RandSeed` (`DS:43ae`) as the game first
  waits for a key (the title's first picture), after startup's
  `Randomize` (`3e99:0080`, the only one): runs repeat, and agree with
  `eclplay --seed N` for the same keys (character creation's rolls, frame
  for frame).
- Memory layout: `want_psp=SEG` loads the game at that PSP (as some checks
  need, such as Fear's 0xnnf0), `HOOK.COM` keeping as many more paragraphs
  as it takes (it starts again once to learn it; with no pad the game's
  PSP is `087b`).
- Speed: `cycles` (`fixed N` or `max`) and `turbo`, which runs the
  emulated time as fast as the host can, delays included: the title's
  waits to its menu take 40 seconds normally, 7 with `turbo`, 1 with
  `turbo` and `max`. A timed menu (the title's) may run out before a key
  is typed then. DOSBox-X ends turbo at a key by default (`stop turbo on
  key`), and the F12 hotkeys (screenshots, the pause, the recording) are
  keys: until 2026-10-21 a box's turbo lasted only to its first
  screenshot. cokbox now turns that off; `b_slow_poison_63h`, whose
  screenshots then came too seldom to see the death at hour 15, rests
  without turbo, as it in effect always had. Champions' boxes are turbo
  unless they say not (`GAMES`' `turbo`; Death Knights' too since 2026-10-23):
  the screens and memory at the key reads are the same, and a full run
  with every box turbo agreed but in `d_space` (its Space typed into the
  computer's turn, over before it came) and `c_wander` (its watched
  screens). A check that judges what the original shows as it comes (a
  burst or a watch of its screens, a recording, a key into a running
  turn) sets `cokbox.REAL_TIME` when it runs, and its boxes keep their
  cycles: with turbo a burst caught a third to a fifth of its screens
  (`d_bozak` 277 of 656, agreeing still, but on less). `COKBOX_TURBO=1` or
  `0` makes every box that does not say turbo or not, for comparing.
- Settling: `wait_input(settle=S)` waits `S` seconds of the game's time
  (the BIOS's clock, 0040:006c), twice that by the host's at most: at a
  box's cycles about the sleep of `S` it was until 2026-10-21, with turbo
  a fraction. Waiting instead until the screen was still for half a
  second of the game's time was tried: in a battle the game shows a text
  for its delay, polling the keyboard and drawing nothing for longer, and
  `d_altq` read its records before the hit in two full runs out of two.
  `COKBOX_SETTLE=fixed` sleeps `S` by the host's clock, for comparing.

`Box(game="death-knights")` (`cokbox.GAMES`) runs Death Knights of Krynn
instead: `DKKAssets/` copied into `C:\DKK`, a `DKK.CFG` written for it
(`dkk_cfg`: `KRYNN.CFG`'s lines with `C:\DKK\SAVE\` and a fifth,
`C:\COK\SAVE\`), its data segment found by its own first bytes (`DS`
0x1bce in `build/death-START_FULL.EXE`, the PSP being `DS - 0x0bde`) and
`locate` reading its merged image; Champions' runs are as before.
`NOT_WAIT_CALLERS` and `to_ghidra` are Champions' addresses (a Death
Knights run finds no such caller).

`cokbox.py run` runs it from the command line (`--game`, `--machine`, `--display`,
`--sound`, `--saves`, `--args`, `--cycles`, `--turbo`, `--seed`, `--keys`,
`--shots DIR`, `--wav`, `--dump OFF,SIZE` of DS at the end), for example
`python3 tools/dosbox/cokbox.py run --saves SAVE --keys '\kxP\v\v\rA\sloaded.' --shots shots`
loads game A and saves the start menu's screen. `cokbox.eclplay(KEYS, DIR,
...)` runs `eclplay --start-menu` with the same `KRYNN.CFG` and saved
games, for comparison (`block=N` after the assets where it does not start
from the start menu, `binary=` another build of `eclplay`).

`tools/dosbox/checks/` holds the checks of Checks against the original,
one script per check or close group, named by the batch it was run in: `a_`
startup, the title, the setup and the start menu; `b_` camp, magic, items,
effects and saving; `c_` places (3D, the area map, the overland map,
NPCs, shops, temples, treasure); `d_` combat; `e_` where the original
reads freed or garbage memory; `f_` display modes, sound, the mouse and
frame counts; and phase 1's `save_again.py`, `slow_poison.py` and
`load_game_screen.py`. `checklib.py` is what they share; `a_common.py`,
`b_lib.py`, `b_magic_lib.py`, `b_screens_lib.py`, `c_places.py`,
`c_geo.py`, `d_lib.py`, `e_common.py`, `e_boltmodel.py`, `f_lib.py` and
`f_wav.py` are the batches' helpers (saved games the port writes at a
place and patches, the port's state printed at each key read by an
instrumented `eclplay`, bursts of screenshots matched in order against
every frame the port shows at its delays, code traps in the overlay
buffer, the party's records, effects and Turbo Pascal's heap in memory,
GEO maps, a model of the bolt, WAV analysis).

Each check compares each screen of the original with the port's at the
same key read (`checklib.frames_at`: `eclplay` run on the keys up to that
point, built to stop where they run out, and its shot at that read; a
screen between key reads with the port's frames in order), and compares
the memory and file values it reads with the port's (its saved games, or
its state printed at that read). Where the port stops or does not model
what the original does, the check asserts what the original was seen to
do instead, and says the port is not compared there. A harness artifact
(DOSBox-X's own CGA cursor) is masked, and said. A check ends with
`VERDICT: AGREES` (exit 0) or `VERDICT: DIFFERS new (...)` (exit 1), each
difference on a `DIFFERENCE:` line before it, and anything else (an
exception, a signal, a harness failure) is an error, exit 3. `COKBOX_ECLPLAY`,
`COKBOX_COK` and `COKBOX_SRC` point the checks at another build of the
port, e.g. a mutant's; `COKBOX_RECORD` names a program that each of
checklib's patched builds runs through (given the executable and its
arguments), such as a recorder of the port's runs that, keeping each
run's arguments and input files, can make them again without DOSBox-X.
The port's runs and builds that a check makes (and waits for) run niced
below the boxes (`cokbox.BELOW_BOXES`, `nice -n 19`): with many checks
side by side their bursts (`frames_at`'s four `eclplay`s, a patched
build's compiles) otherwise made a box late, and a check then differed, a
different one each run. checklib compiles a patched build's files side by
side, on half the cores.

Each runs alone (`python3 tools/dosbox/checks/NAME.py [--workdir DIR]`).
`make dosbox-checks` (`run_all.py`; `ARGS="--jobs N --only NAME ..."`)
runs them all side by side, as many at a time as `COKBOX_DISPLAYS` has
displays less two, else five eighths of the cores (10 of 16: 12 and 14
are no faster, as the part side by side then lasts about as long as
`a_demo`, its longest), and prints one line per script: the checks it covers, its
verdict, and whether that is the verdict recorded (agrees, or new for a
port difference found and not yet fixed, of which none is left), an ERROR
for anything else. A script not as recorded runs once more at the end,
alone, and that verdict counts, the first shown beside it and the script
named in the summary (`--no-again` keeps the first): at 10 at a time
about one run in three had one script differ, a different one each time
(`a_demo`, `c_pit`, `d_altq`, which judge the original's screens or
waits as they come), and alone it agreed; a difference of the port's
differs again. A script that keeps needing the second run deserves a look
all the same: the untrap race (see Memory above) was such a one. On a
script's timeout, or when run_all itself gets SIGTERM, SIGINT or SIGHUP,
it kills the running scripts' process groups and the Xvfb and DOSBox-X
they started (their pids recorded in `COKBOX_PIDFILE`). `run_all.py
--self-test` (in `make dosbox-selftest`) checks its reading of verdicts
and statuses, and the run again (with made-up scripts), without
DOSBox-X. Each run records where the time went, for speeding the checks
up: with `COKBOX_TRACE` (run_all sets `OUT/NAME.trace.jsonl` for each
script) `tools/dosbox/telemetry.py` times cokbox's and checklib's steps
(a box's boot, the waits for the game to want a key or take one,
screenshots, settles and pauses, the port's runs and builds), samples the
check's line ten times a second, and records each box's DOSBox-X CPU
against its lifetime; run_all adds the machine's load every two seconds
(`load.jsonl`) and when each script ran (`schedule.json`), and
`tools/dosbox/checks/telemetry_report.py` reads them into
`OUT/telemetry.txt` (the run's bound at its job count, each script's time
by kind, the spans and lines across all scripts, the longest scripts'
timelines), its first lines printed at the end; `run_all.py --report`
writes it again. A full run of the 82 scripts takes about 6 minutes on a
16-core host with little else running, 10 at a time: the scripts' time
summed (48 minutes) over the ten, about as long as the longest, `a_demo`
(under 5: its demonstration watched at the game's speed), plus the run
again of any script not as recorded. It was 9 before the telemetry: the
turbo lost at the first screenshot (above; `a_demo` 7 minutes then,
`a_ctrl_s` 3), `c_places`' `judge_watch` playing cok twice for the same
frames (`c_wander` 4), `d_lib.read_cell` matching each of the font's
glyphs pixel by pixel (`d_bozak` 3), and the boxes at their cycles where
turbo shows the same (see Speed and Settling above; 80 minutes summed
then). It took 31 before the burst checks read
the Tandy's screen from memory (`d_lib.burst`, `READ_SECONDS`) and so
could run beside the rest (with screenshots, DOSBox-X wrote ten times
fewer on a loaded host and they missed their text), `d_bozak` and
`d_aurak` played their battles at the game's speed 4 instead of 15
(`d_bozak` 12 minutes then, under 3 now), and judging a burst stopped
taking minutes (`d_lib.candidates` compares every 61st pixel before the
whole screen; `load_bmp` decodes a frame with `bytes.translate`).
`make dkk-checks` (`run_all.py --set dkk`, its out `build/dkk-checks`;
`--set all` runs both games' scripts) runs Death Knights' `dkk_*`
scripts the same way, `dkk_fight.py`, `dkk_levels.py` and
`dkk_treasure.py` split by their `--flows` into one script per flow
(`dkk_fight-spells`: its own box, workdir and log): 35 scripts in about 3
minutes, 10 at a time, all as recorded twice running (2026-10-23). They
took 80 minutes one after another at their cycles (`dkk_fight.py` 40),
10 side by side, and 3 with Death Knights' boxes turbo too (`GAMES`), its
`dkk_views.py` typing the start menu's keys as the game waits
(`K.title`, `K.LOAD_A_STEPS`) where `\p` pauses had taken four of its
five minutes, `dkk_lib.settled` counting the game's time as `Box.settle`
does, and `dkk_treasure.py` taking each screen once two shots agree
(`still_shot`: with turbo a text's delay, which polls the keys, could
let the settle pass in the block's load after it). The
checks need the host otherwise idle, its disk as well as its CPU: heavy
I/O on the same filesystem (seen: a large delete) once made `d_turning`
miss its text and `d_bolt` take eight times as long. In the run before
last (15 minutes, 6 of them `a_demo` run again) `a_demo` differed, alone
as well, each time at a screen caught while the game drew (screens 192,
then 137 and 231). The game clears an area wider than what it changes,
then draws it again: the whole screen, top down, before the battle's
frame (its top border the same after), and a combatant's whole panel
before writing its hit points again (its name the same). A screen caught
between has black pixels both where the port's frames around it change
and where they do not, which `judge_watch` allowed only all of one kind,
not each pixel by itself as it said. The video memory read every 10 ms
beside two runs held five such screens (the screen black down to row 95
over the old one), each a difference by the old rule; each pixel by
itself, they agree, and a pixel of another colour put in each still
differs. `checklib.watch` now reads the Tandy's screen from memory
(above): `a_demo` sees 420 to 424 screens, alone or ten at a time (340
to 354 screenshots before). Its first read, right after the keys, can
catch what the game draws at once: `c_catacombs` caught the menu's
highlight moving to Area before `Not Here` (the port's `menu_wait` draws
it too, with no frame), a screen read once, which `c_places` now leaves
out at the start as it did between others. A difference that lasts from
some frame on can still pass `a_demo` where a screen matches no frame,
as that screen's frames before and after then take in the change: the
port's combatant's name made another colour from its frame 600 on passes
with the video memory's screens, the exact frames 83%, above the 80%
asked; that needs a look. The last full run (10 minutes) gave 81 recorded
verdicts, and `c_catacombs` differing as above (fixed since; it, `c_pit`
and `c_wander` agree).
