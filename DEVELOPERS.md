# Champions of Krynn port: developer notes

How the port is built, how each part follows the original, and how it is
checked. To build and play the game, see [README.md](README.md).

The native tools are a C17 DAX archive reader, decompressor, image
exporter, picture and text compositor, ECL script interpreter and
disassembler, and a headless script player that shows the scripts' text,
menus, pictures, 3D view and party on the adventure screen, and plays the
game from its title and start menu; and `cok`, which plays the game in a
window with SDL2, from the title, with the keyboard, the mouse or a joystick
(see [The mouse and the joystick](docs/front-end.md#the-mouse-and-the-joystick)) and with its sound: the
Tandy's three voices and noise, the AdLib's YM3812 or the PC speaker, the
effects and the title's music (see [Playing](docs/front-end.md#playing) and [Sound](docs/sound.md#sound)), in the display mode `KRYNN.CFG`
names: EGA, the Tandy's or CGA's four colours (see [Display modes](docs/front-end.md#display-modes)), after
the original's text-mode setup when there is no `KRYNN.CFG` (see [Startup
and the setup](docs/front-end.md#startup-and-the-setup)). The original DOS executable, decompiler output, and game
data are reference inputs, kept in `COKAssets/`; saved games from the
original, if any, go in `SAVE/`. The sequels' files go in `DKKAssets/`
(Death Knights of Krynn, which the tools and `eclplay` read headless: see
[Games](docs/games.md#games)) and `DQKAssets/` (The Dark Queen of Krynn, for disassembly only: see
[Disassembly image](docs/disassembly.md#disassembly-image)); like `COKAssets/`, they are not in git.
The plan for playing Death Knights of Krynn too, and what it shares with
this game, is in [DEATH-KNIGHTS.md](DEATH-KNIGHTS.md).

## Build and verify

Run from the project directory, with a C compiler and Make installed:

```sh
make
./build/daxcheck COKAssets/TITLE.DAX
./build/daxcheck --list COKAssets/TITLE.DAX
./build/daximages COKAssets/TITLE.DAX build/title
./build/daximages --entry 0 --frame 0 COKAssets/PIC1.DAX build/portrait
./build/daxcompose build/demo.bmp fill 0 0 40 200 1 \
    draw COKAssets/PIC1.DAX 0 4 1 1 sprite COKAssets/SPRIT1.DAX 0 0 4 12 \
    text COKAssets/8X8D1.DAX 0 1 23 14 0 "Champions of Krynn"
./build/ecldump --block 16 COKAssets/ECL1.DAX
./build/ecldump --summary COKAssets/ECL*.DAX
./build/eclplay --test-party 4 --keys '\r\r\r\r\r' --shots build/shots COKAssets 16
./build/eclplay --play --set 4be6=1 --keys '\r\rm\^\r\^\^' COKAssets 32
./build/eclplay --load SAVE/SAVGAMA.DAT --play --keys '\r\r\r' COKAssets
./build/eclplay --play --set 4be6=1 --saves build --keys '\rerharsan\e' COKAssets 32
./build/eclplay --test-party 4 --play --set 4bf2=11 --set 4bc3=1 --set 4bc4=3 \
    --set 4c2d=1 --keys 'nm98y' COKAssets 16
mkdir -p build/roster
./build/eclplay --start-menu --saves build/roster \
    --keys '\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\v\vS' COKAssets
./build/eclplay --start-menu --saves build/roster --wav build/title.wav --keys '\t' COKAssets
./build/eclplay --start-menu --display cga --saves build/roster --shots build/cga \
    --keys '\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\r\e' COKAssets
rm -f build/krynn.cfg
./build/eclplay --start-menu --config build/krynn.cfg --keys '243d:\\krynn\r\t' COKAssets
./build/eclplay --start-menu --mouse --shots build/mouse --keys '\kx\m190,195,1.' COKAssets
./build/ecldump --game death-knights --block 48 DKKAssets/ECL2.DAX
./build/eclplay --game death-knights --set 4be6=1 --screen build/dkk48.bmp --keys '\r' DKKAssets 48
./build/eclplay --game death-knights --character DKKAssets/SAVE/CHRDATA1.GAM \
    --character DKKAssets/SAVE/CHRDATA2.GAM --play --keys 'ev\r\e' DKKAssets 17
make test
make sanitize
make cok
./build/cok
make dosbox-selftest
```

Run `make -j16 test` (`-j` the machine's cores, `nproc`), and `make -j16
sanitize`. Make compiles each source once for each set of flags, into
`build/obj/` (`CFLAGS`), `build/obj-asan/` (`ASAN_FLAGS`: `make
sanitize`'s AddressSanitizer and UBSan) and `build/obj-tsan/`
(`TSAN_FLAGS`: `build/cok_tsan`), each with its headers in a `.d` file
(`-MMD -MP`), and links every program from those objects, so that a
change recompiles only the objects that include it; SDL2's flags go to the
three files that include `SDL.h` (`src/cok.c`, `src/keymap.c`,
`tests/test_keymap.c`). Each test program's run, and each test script's,
is a target of its own (`run-NAME`; `tests/test_eclplay.py` in four parts,
`COK_TEST_SHARD=K/N` running every Nth test from the Kth), so that `make
-j` runs them side by side; plain `make test` runs them one after another.
`run-test_eclplay` then checks, from the ids each part lists, that the
parts ran each of its tests once. `tests/test_cok.py` plays in real time
and runs last, alone, once the others are done, its own tests side by
side, each in a process of its own (`COK_TEST_JOBS` at once, by default
half the cores up to 8; `COK_TEST_JOBS=1` one after another, and a test
named runs alone): they are mostly asleep, a hundred seconds one after
another, 22 s at 8. A clean `make -j16 test` takes about 30 s on a
16-core host (`make -j16 sanitize` 20 s). Beside others a failing run's lines are mixed with
theirs, and its `Error` line is not the last: `make -j16 -Otarget test`
shows each run's output whole once it ends (and `tests/test_cok.py`'s
progress only at its end, so it is not the default). The tests'
temporary files go to `build/tmp` (`TEST_TMPDIR`, as `TMPDIR`; the C
tests' folders through `tests/test_temp.h`): the dev container's `/tmp`
is a FUSE overlay, several times slower to write their many small files.

`make cok` builds `build/cok`, the playable front end (see [Playing](docs/front-end.md#playing)), which
needs SDL2 2.0.18 or later and finds it with `pkg-config sdl2` (Debian and
Ubuntu: `libsdl2-dev`; Fedora: `SDL2-devel`; macOS: Homebrew's `sdl2` and
`pkg-config`). Nothing else needs SDL2: `make`, `make test` and `make
sanitize` work without it, and then skip `build/cok` and its tests, saying
so. With it, `make test` also builds the front end and checks it under
SDL's dummy video driver (`tests/test_keymap.c`, `tests/test_cok.py`).
The dev container installs `libsdl2-dev` but has no display: build and run
`cok` on the host (`make cok && ./build/cok` from the project directory,
with the game's files in `COKAssets/`), or give the container the host's X
server (on a Linux host, run it with `-v /tmp/.X11-unix:/tmp/.X11-unix -e
DISPLAY=$DISPLAY` and allow it with `xhost +local:`; or `ssh -X` into a
machine with the build). SDL picks Wayland, X11 or the console's driver
itself; `SDL_VIDEODRIVER` overrides it; likewise PulseAudio, PipeWire or
ALSA for the sound, `SDL_AUDIODRIVER` overriding it (the container has no
sound device: `cok` says so and plays silently; `SDL_AUDIODRIVER=disk`
writes the samples to `sdlaudio.raw` or `SDL_DISKAUDIOFILE`, 16-bit mono
at 44,100 Hz).

`daxcheck` reads archives without modifying them. It validates every directory
entry and decompresses every record, checking the decoded byte count. It exits
nonzero if any file or record fails. `--list` prints metadata for each validated
record; directory entry numbers are zero-based. Duplicate IDs are retained.
`make test` includes synthetic malformed inputs, all supplied DAX archives,
export checks for all 26 supported graphics archives (2,363 images), and
tests of the picture, text, menu, 3D view, overhead map, party and spell
effect routines, the sound (`tests/test_sound.c`: the drivers' port writes
against the original's, recorded in an emulator, and the synthesis; the
AdLib's chip in `tests/test_opl2.c`),
`KRYNN.CFG` (`tests/test_config.c`), the games' descriptors and `--game`
(`tests/test_rules.c`, see [Games](docs/games.md#games)), Death Knights of Krynn's descriptor,
tools and scripts where `DKKAssets/` holds its files
(`tests/test_death_knights.c` against `build/death-START_FULL.EXE`, which
`make test` then builds, and `tests/test_death_knights.py`; each says so
in a line and skips without them), the character record's layout
(`tests/test_layout.c`), startup's setup and text screen
(`tests/test_setup.c`: against the original's, run in an emulator), the
mouse's clicks and the joystick (`tests/test_input.c`: against the
original's, run in an emulator), the display modes
(`tests/test_display.c`: each screen type's drawing in CGA, EGA and Tandy
mode against what the original's graphics and text units left, run in an
emulator),
the adventure loop, the camp, casting spells, the character sheet with its
items, monsters and encounters, the battlefield, the rounds of a battle,
treasure and the end of combat, the shops and the temple, the combat
screen, the portraits, NPCs and the other remaining opcodes, the
attacks, and the start menu with creating, training and modifying
characters, their icons and the roster, the debug mode
(`tests/test_debug.c`),
including PIC delta decoding on `PIC1.DAX` and the game font in
`8X8D1.DAX`, and plays the opening scripts, the view of Throtl and, with
a party made up for testing (`eclplay --test-party`), its fights, round
by round, a walk through it and a camp, travel on the overland map
with its encounters, NPCs joining and leaving, a shop's portrait and the
title screen's demonstration. It builds `build/START_FULL.EXE` (see [Disassembly
image](docs/disassembly.md#disassembly-image)) to check the original's tables that the port uses. With the
original's saved games in `SAVE/` (`SAVGAMA.DAT` and its `CHRDATA*`
files), it also plays those with their party, through encounters, fights
resolved by `--combat` and every treasure the scripts give out, checks the
stats recomputed for their characters, runs their spell effects, and saves
them again to compare the files; those tests are skipped without them.
`make sanitize` repeats these checks with AddressSanitizer and UBSan. `make
dosbox-selftest` runs the original game in DOSBox-X instead (see [DOSBox-X
harness](docs/dosbox.md#dosbox-x-harness)); it needs `dosbox-x`, `Xvfb` and `xdotool`.
Tests also require Python 3 (standard library only). The native tools have no
third-party dependencies. Sanitizer targets require the compiler's ASan/UBSan
runtime libraries, which were unavailable on the initial development system.

## The notes

The rest of the notes are in `docs/`, one file per part of the port:

- [Games](docs/games.md): The games' descriptors, Death Knights of Krynn's differences, the character record's layout, the data segment's tables and the spells.
- [The front end](docs/front-end.md): `cok`: playing in a window, startup and the setup, the mouse and the joystick, the display modes.
- [Sound](docs/sound.md): The devices, the effects driver, the title's music, timing and the hardware's emulation.
- [Images and the library](docs/library.md): Image export, picture composition and the shared library.
- [Scripts](docs/scripts.md): ECL scripts, how `eclplay` plays them, and the debug mode.
- [Adventure](docs/adventure.md): The 3D view, the overhead map, the adventure loop, the overland map, monsters and encounters.
- [The party](docs/party.md): The party, derived stats and spell effects.
- [Camp, casting and View](docs/camp.md): The camp menu, casting spells outside combat and the character sheet.
- [Combat](docs/combat.md): The battlefield, the rounds and the combat screen.
- [Combat turns](docs/combat-turns.md): Attacks, movement and death; the computer's turns; the player's commands.
- [Spells in combat](docs/combat-spells.md): Spells in combat; clouds, pools and breath.
- [Treasure and shops](docs/treasure-shops.md): Treasure and the end of combat; shops and the temple.
- [The start menu](docs/start-menu.md): The start menu, creating characters and training.
- [DOSBox-X harness](docs/dosbox.md): Running the original in DOSBox-X for checks.
- [Checks against the original](docs/checks.md): The numbered checks of the port against the original game.
- [Disassembly image](docs/disassembly.md): The unpacked executables used for disassembly and table checks.
