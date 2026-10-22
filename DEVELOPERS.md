# Champions of Krynn port: developer notes

How the port is built, how each part follows the original, and how it is
checked. To build and play the game, see [README.md](README.md).

The native tools are a C17 DAX archive reader, decompressor, image
exporter, picture and text compositor, ECL script interpreter and
disassembler, and a headless script player that shows the scripts' text,
menus, pictures, 3D view and party on the adventure screen, and plays the
game from its title and start menu; and `cok`, which plays the game in a
window with SDL2, from the title, with the keyboard, the mouse or a joystick
(see The mouse and the joystick) and with its sound: the
Tandy's three voices and noise, or the PC speaker, the effects and the
title's music (see Playing and Sound), in the display mode `KRYNN.CFG`
names: EGA, the Tandy's or CGA's four colours (see Display modes), after
the original's text-mode setup when there is no `KRYNN.CFG` (see Startup
and the setup). The original DOS executable, decompiler output, and game
data are reference inputs, kept in `COKAssets/`; saved games from the
original, if any, go in `SAVE/`. The sequels' files go in `DKKAssets/`
(Death Knights of Krynn, which the tools and `eclplay` read headless: see
Games) and `DQKAssets/` (The Dark Queen of Krynn, for disassembly only: see
Disassembly image); like `COKAssets/`, they are not in git.
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

`make cok` builds `build/cok`, the playable front end (see Playing), which
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
against the original's, recorded in an emulator, and the synthesis),
`KRYNN.CFG` (`tests/test_config.c`), the games' descriptors and `--game`
(`tests/test_rules.c`, see Games), Death Knights of Krynn's descriptor,
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
title screen's demonstration. It builds `build/START_FULL.EXE` (see Disassembly
image) to check the original's tables that the port uses. With the
original's saved games in `SAVE/` (`SAVGAMA.DAT` and its `CHRDATA*`
files), it also plays those with their party, through encounters, fights
resolved by `--combat` and every treasure the scripts give out, checks the
stats recomputed for their characters, runs their spell effects, and saves
them again to compare the files; those tests are skipped without them.
`make sanitize` repeats these checks with AddressSanitizer and UBSan. `make
dosbox-selftest` runs the original game in DOSBox-X instead (see DOSBox-X
harness); it needs `dosbox-x`, `Xvfb` and `xdotool`.
Tests also require Python 3 (standard library only). The native tools have no
third-party dependencies. Sanitizer targets require the compiler's ASan/UBSan
runtime libraries, which were unavailable on the initial development system.

## Games

The port is to play *Death Knights of Krynn* as well (see
[DEATH-KNIGHTS.md](DEATH-KNIGHTS.md)). Everything that differs between the
games goes in a descriptor, one a game: `cok_rules` in `src/rules.h`, with
Champions' in `src/rules.c` (`cok_rules_champions`) and Death Knights' in
`src/rules_death_knights.c` (`cok_rules_death_knights`, read headless so
far: see Death Knights below). A front end picks it once, before any file
is read, from `--game` (`champions`, the default, or `death-knights`;
`cok_rules_find`, an unknown name an error listing `cok_rules_names`), and
hands it to `cok_adventure_open`, which keeps it in
`game->rules`, read through `cok_game_rules(game)`; the code that has no game (`cok_character_read` and
`write`, `cok_sound_load`, `cok_config_find`, the setup, `daximages` and
`daxcompose`'s `cok_archive_layout`) takes it as an argument. Shared code
reads what it needs from it and never asks which game it is.
`cok_adventure_open` asserts that it was given one, and `cok_game_rules`,
which every read of `game->rules` goes through, that the game has one:
a path that builds a game some other way without it stops at the first
read of the rules, with the assertion, not reading through NULL. (Tests
that build a game by hand set `game.rules` themselves.)

The descriptor holds names, the character record's layout and the data
segment's tables, a section to a concern:

| Section | Holds | Champions |
| --- | --- | --- |
| `id`, `assets` | `--game`'s name; the assets' directory without `--assets` | `champions`, `COKAssets` |
| `text` | the window's title; the title menu's heading at startup and after a demonstration (`1000:0134`, spelt differently); camp's first line (`4888:2c31`) | `Champions of Krynn`, `Champions of Krynn v1.2`, `Champions Of Krynn v1.2`, `The party makes camp...` |
| `archives` | each DAX archive the game reads, by stem: with the ECL file's number (`GEO` makes `GEO1.DAX`), with `file_digit`'s digit (`MON`, with the kinds `CHA`, `SPC`, `ITM`; `ITEM`, `HEAD`, `BODY`, `CPIC`) or alone (`SKY`); the names `6d21:01d0` loads icons by as they are; records read from a file of their own, as a `cok_rules_record` (stem, file, 0 for none, record): the font and the overland map's cursor; and `file_digit`, the digit a file's number gives the names that hold one (Str into a string[1], `4b6d:161b`, `2fd3:1d21`, `6961:05b9`, `6d21:01d0`) | `ECL`, `GEO`, `WALLDEF`, `8X8D`, `PIC`, `SPRIT`, `BIGPIC`, `HEAD`, `BODY`, `CPIC`, `COMSPR`, `CHEAD`, `CBODY`, `SKY`, `TITLE`, `ITEM`, `MON`, `DUNGCOM`, `WILDCOM`, `RANDCOM`, `MIDI`, `SOUND`; font `8X8D1` record 201, cursor `CURSOR` record 1; the first decimal digit |
| `files` | the devices' file (looked for in upper case, then lower); the item types; the directory the setup takes the game to run in and the save path's default under it | `KRYNN.CFG`, `ITEMS`, `C:\COK\`, `SAVE` |
| `saves` | saved games (`SAVGAM` letter `.DAT`) and their characters' names (`CHRDAT` letter number); a character's extensions: in a saved game, in the roster, its items, its effects, its spells' block (a game that keeps one); the debug mode's trace; the saved game's layout past the variables' width (`ecl.variable_bytes`): its wall table's entries and those the loader loads again, and whether the loader keeps the saved last mode (see Party); the byte the roster's list leaves a record out by; the bank vault's files, or none | `SAVGAM`, `DAT`, `CHRDAT`, `SAV`, `WHO`, `STF`, `SFX`, none, `DEBUG.TXT`; 3 and 3 (`DS:6d8a`, `4b6d:1b34`), no (the saved mode becomes the last); `COINS` 6 (`4b6d:0453`); none |
| `images` | the archives `daximages` and `daxcompose` read, by layout: XOR deltas, collections, single images; stems, `NULL`-ended; and the frames a header's count byte 0xff gives (`frames_ff`: 255 where the loader takes the byte, `127f:1c2f`; Death Knights' 256, its 256-tile sets) | `PIC1-3`; `SPRIT1-3`; the 20 others; 255 |
| `character` | the character record's layout (`src/field.h`): its size, its file's size, where a spells' block of its own lies, and where each field lies | 409 bytes, none, the fields below |
| `characters` | the routines that recompute a character where the games' differ (`party.c`, each selected by an enum: the stats from the items, `cok_stats_routine`; the classes and levels, `cok_levels_routine`), the import of another game's characters (`cok_rules_import`, or `NULL`), a paladin's experience row and the classes that count as fighters' (exceptional strength, the hit points by constitution) | `6346:0d20`, `66c2:0433`; none; none, fighters, rangers and knights (`60f4:1743`, `1628`) |
| `data` | the original's data segment (`src/data.h`): the tables the port copied from it, by DS offset, and where each table the code reads lies and its shape | 46 tables (`src/data_champions.c`), 58 named; see below |
| `ecl` | the ECL's opcodes, by opcode byte (`cok_rules_opcode`): what each does (`meaning`, a `cok_ecl_opcode`), the operands its handler loads (or a list over a header of `header` operands, or none ever), the operands a failed `IF` skips with it, and the handler the port does not carry out yet (`unported`, cited); the routines `CALL` runs by address (`calls`); the bytes a variable address holds (`variable_bytes`); the variables code reads and writes by name (`vars`, a `cok_rules_var` each: address, width, elements, the value a flag is off at); what a block's start sets (`start`); the special addresses read and stored (`reads`, `stores`); the selected character's fields at `0x7c00`-`0x7fff` (`character`, a `cok_rules_character_var` each, and `class_bits` for a field of class bits); and three handlers' constants: `COMPARE AND` of strings, `DELAY`'s wait, `HORIZONTAL MENU`'s single item (see ECL scripts) | its 66 opcodes, each its own meaning (`2fd3:0050-36dc`, `3775:1e74`), none unported; `2e10` redraw, `b203` sound, `c01e` step, `c018` wall ahead, `6803` animate (`2fd3:329b`); 2; 53 names, each a word; the portrait 0xff, the rest's encounters 0, magic allowed (`3775:01e8`); 10 reads, 8 stores (`3775:0fca`, `0e06`); 36 entries (`3775:07d9`, `0c00`); values only, the speed's, yes |
| `view` | the 3D view's tile sets (each one's first and last value, `6e22:01ab`, and the colour each is loaded with as transparent, `tile_transparent`), the wall slot that holds wall types 1-5 and that `LOAD PIECES`' two-set path loads first, whether a record's wall sets share one tile set and whether tile values are moved by the slot's set; the overhead map's tiles; `PICTURE`'s overland map; whether CGA mode has a backdrop of its own (`cga_backdrop`: a line row, no pictures) | 1-0x2d, ..., 0x100-0x127 (`DS:1ec2`), each loaded with 13 transparent (`6e22:0050`); slot 1; a tile set each, moved (`69ea:1025`, `1230`); 0x104 and 0x100; 0x79; yes (`69ea:0184`) |
| `startup` | what startup loads past the font (`3e99:005b`): tile sets from `<tiles>1` and the missiles' icons from `COMSPR`, as (first id, first slot, count); the icon slots (`icon_slots`, at most `COK_ICON_SLOTS_MAX`, 35) and their table's DS offset, for messages; whether the missiles' icons are recoloured as a `CPIC`'s outside CGA (`missiles_recoloured`: 8 to 0) | sets 4 and 0, records 202 and 203; 0-11 into 13-24, 25 into 25; 26 slots at `DS:6172` (`3e99:0466` clears 0xd0 bytes); no (`6d21:01d0`: a `CPIC`'s only) |
| `spells` | what is keyed by spell id (`magic.c`, `cast.c`): the ids' count; their names and the spell lists' headings, with their tables' DS offsets and sizes for the messages; each id's handler (`cok_rules_cast`: the port's, a `cok_cast_handler`, and the original's address, or `unported`); the durations not taken from the spell table (`cok_rules_duration`); the spell ids the code names (`ids`, by `cok_named_spell`); the spell lists' sizes; the granted powers' ids; an item power's spell; Display's search; byte 7's values for the caster, one picked and the party; who may use a class's spells (`usable_paladin`, `usable_ranger`, `usable_mage_class`) | ids 1-0x6b (`DS:2077` + 41 × id, `DS:0bfa` + 41 × level), the handlers `5b04:58ed` stores at `DS:6e3a`, 7 durations (`5b04:0f78`), 23 names; 58 and 49 (`DS:473a`, `4774`); 101-107; above 0x38, 0x17 less (`3afb:04b2`); spells 1-0x38 (`4888:17ae`); 1, 2, 4 (`5b04:127e`); no paladin, a ranger from 7, any magic-user (`5b04:0083`) |
| `effects` | what is keyed by effect id (`effect.c`, `cast.c`, `magic.c`): the ids' count; each id's handler (`cok_rules_effect`, as the spells'); the ids each event runs; the ids the party shares and Prayer's reach; those magic resistance leaves pending; the effect ids the code names (by `cok_named_effect`); Display's ids and texts; the rules' routines a game does not have ported yet (the caster level, the saving throw, magic resistance, Display); the effect whose holder's saving throw rolls 1 and where that throw reads a mage's moon; what one an effect does not take "is" | 0x79 (`DS:6b94`, `5b04:58ed` and `3f44:38ea`), events 1-0x18 (`60f4:057c`), 0x15, 0x2d, 0x2e, 0x31 and 0x31 from 6 squares (`60f4:0332`, `0352`), 0x5b and 0x52 (`60f4:04f3`), 80 names, `4888:17ae`'s; none; none, the wrapped byte (`60f4:113a`); `is Unaffected` |
| `moons` | the three moons (`57e4:001e`, `0143`): the days each stays in a phase, and on the frame each one's column and tile | 8, 1, 6 days; columns 8, 19, 30; tiles 0x114 + phase + 10, 6, 14 |
| `frame` | the screens' frames (`screen.c`), by `cok_screen` (the frame, the open frame, the adventure screen, a big picture's, Memorize's, the spell lists', combat's, the sheet's, the credits'): each a `cok_rules_screen`, the screen drawn under it first, the box cleared (`1128:07e6`) and its lines, rows and columns of tiles (`cok_rules_frame_line`: a tile, or a table by the cell's column or row, and on row 0 the moons), each the frame's `base` + its value; which frame main draws after the start menu (`menu_big`) and Begin Adventuring for a loaded game (`resumed`, a `cok_resumed`) | `1128:0000`-`077c`'s layouts, tiles 0x114 + `DS:0e3a`, `0e62`, `0e7a`, `0ea3`, `0eb0`, `0e8b`, `0ebd`, `0f02`, `0f19` and the code's; the big frame in mode 3 on the map (`1000:024b`); in block 0 or off the map (`4def:01b4`) |
| `start` | the front end (`start.c`, `roster.c`, camp's Save): the title's pictures (record, cell, whether the screen is cleared first, the seconds waited) and credits (lines, their wait, whether they show after a key), a new game's block and the demonstration's file and block; the start menu's items, prompt and word, each item's routine where the port does not carry it out for the game (`unported`, cited: choosing it stops the run); the words of Pick Character, Drop, Exit, Load, Save, the roster's Overwrite, Add, and "from where" for a game that imports another's; the questions' colour; and what its commands do where the games differ (Add's paladin rules, its space, its message and marks; a Drop or Remove that empties the party ending the game; Exit's No saving then quitting; Begin's mode; PROGRAM 0 marking the party on the map; the wall table's entry the end of a game sets) | `2f55:063c`'s pictures, 1, 2, 3 at 5, 1 and 4 at 0, 1 for 5, 10 and 10 seconds, the credits (`2f55:0267`) for 10 unless a key; 0x24, file 2 block 0x39; `DS:0878`, `Choose a FUNCTION `, `Select `, none unported; yellow; paladins, the space, the message and marks; no, no, the entry mode, no; the first |
| `combat` | the encounter and the battle's frame where the games differ (`monster.c`, `combat.c`, `round.c`, `treasure.c`'s end of combat): `COMBAT`'s vault branch and the vault a game's port does not carry out yet; the show at distance's `0xff` for none; leadership before the setup and its words; the dead exploding after a turn and a blow, a side gone ending the round there; the setup's line, its tile counts, whether a set is kept, the open ground's builder (`cok_wilderness_routine`), the dungeon's fill and corners; the placement's clears, ambush, distances, the cell under a body, status 9; the end of combat's lost weapons, shares, effects lost, may-train, the results' words and key, the treasure menu's condition and stop, a destroyed party's words and whether a party destroyed while a block is entered still reads the adventure's menu once (`killed_command`) | the vault none; no; no; yes, no; none; `A battle begins...`, 0x19 and 0x21, no, `COK_WILDERNESS_CHAMPIONS` (`3cb2:0fc8`), no fill, Champions' corners; 255, no ambush, status 9 off the map; weapons back, shares, `DS:0390`'s 11, no may-train, `The party has won.` ..., `press <enter>/<return> to continue`, always (`351b:1968`), no |
| `fight` | the rounds, the computer's turns and the attacks where the games differ (`round.c`, `ai.c`, `attack.c`, `arena.c`, `command.c`, `cloud.c`): their words (`cok_rules_fight_words`: the attack's message, fleeing, turning undead, the computer's, the round's end, the panel, guarding, Alt-X without the Helm cheat); the clouds (`cok_clouds_routine`: Champions' or Death Knights' Stinking Cloud, Cloud Kill, cloud square, the round's clouds and Dispel Magic's part) and their words (one coughing, one choking, a cloud gone, Stinking Cloud's, Cloud Kill's poison); the battlefield's cloud and pool tiles and whether they count as terrain; the death's search for a body, its cell kept for a pool, its spell cleared; the computer's turn: a target first, the items' spells, its spell choice (`cok_computer_cast_routine`), the undead it turns and the paladins who do, its reach, morale's compare and the fleeing's effects, the flight's way; the attack's sides, the free attacks' effect, the creature flag for no dying words, a lost spell's mark, the arrows, the missile's clamp, turning undead (`cok_turning_routine`) | Champions' words; `COK_CLOUDS_CHAMPIONS`, `starts to cough`, `chokes and gags from nausea`, `The air clears a little...`, `Creates a noxious cloud`, none; 0x1e and 0x1d, no; by the stack's byte, the cloud's only, yes; no, yes, `3afb:03b9`, the stack's byte, none, range less 1, unsigned, `DISPELLING` and `GIANT_SLAYER`, 0; no, none, none, cleared, 5, 7, 0x0c, 0x1e, no, `432f:12b7` |
| `menus` | the menu loop (`menu.c`, `cok_menus_routine`: by letters, or by index, `cok_menu_pick`), the Yes/No's items for the indexed loop (`camp.c`), and the adventure's menu where the games differ (`adventure.c`: whether it keeps its own selection, whether Escape leaves Move), whether camp's menu keeps its own selection (`camp.c`) and a special key's pick of a character (`devices.c`: whether a key but Up and Down keeps the one selected) | by letters (`67b5:03e2`, `177f`); none; no, no (`475c:09ec`); no (`4888:2c31`); no, the first is picked (`546c:3334`) |
| `commands` | the player's commands in a battle (`command.c`): the routine (`cok_commands_routine`: Champions' turn `3995:0573` or Death Knights' `3e05:08af`), the words of Move's steps and of attacking an ally and whether Yes turns the NPCs against the party, Death Knights' menus' words (its turn's two-row menu, Move's, aiming's, the cursor's, Speed's), the cursor's icon slot | `COK_COMMANDS_CHAMPIONS`; `Flee:`, `can't go there`, `Not with that weapon`, `Attack Ally: `, yes; none; 0x19 (`6beb:00fc`, `6346:1a26`) |
| `items` | View and the items menu (`sheet.c`, `items.c`; see View and Death Knights' View): the routine (`cok_items_routine`: Champions' `546c:0d74` and `17f9` by letters, or Death Knights' `580d:0c66` and `0ffa` by index), Death Knights' menus' words; the items list's readied column; Ready's thief level for another order's scroll and its word; the words of Use of an item not readied, of the questions of parting with a scroll and of Drop, of Trade's pick for money and the coin lists' first item, of Death Knights' Deposit; Sell's and Id's words, Sell's divisor of a count's price, whether their question starts on Yes, whether Id waits twice; Death Knights' Heal and Cure (the class, the effect and its minutes, the words) | `COK_ITEMS_CHAMPIONS`; none; `" Yes  "`, `" No   "` (`6346:0473`); none; `Must be Readied`, `is it Okay to lose it? `, `Drop It? `, `Trade to?`, `" Select"`; none; `546c:2822`'s and `2a59`'s words, 20, no, no, no; none |
| `shops` | the shop, its Buy and Appraise, the temple and its Heal (`shop.c`; see Shops and the temple and Death Knights' shops): the routine (`cok_shops_routine`: Champions' `36d0:07da`, `0484`, `58e7:1929`, `340d:0ea9`, `0be1` by letters, or Death Knights' `3b89:06f3`, `0453`, `5c72:0dc2`, `3999:07d3`, `04e2` by index); whether mode 1's redraw shows the portrait last shown in 3D areas; whether a price (Buy's, Id's) is paid from the steel and then the pool's steel, rather than from the money in steel of every coin; Death Knights' words (the menus, Buy's, the funds, a purchase's, leaving money, Appraise's, Heal's, the cures' questions and words) and the spells its cures name (`DS:030c`) | `COK_SHOPS_CHAMPIONS`; yes (`6346:2c17`); no (`546c:3424`, `58e7:0155`); none |
| `treasure` | treasure where the games differ (`treasure.c`, `adventure.c`'s `DAMAGE` and `ADD EP`; see Death Knights' treasure): the item table `TREASURE ITEMS` and `STOCK` take items from (a `cok_rules_record` and how many entries it fills, or none); the treasure menus' ways (selections from each menu's first item, special keys told apart from letters, leaving's Yes/No from No) and words (leaving with treasure left, Take's, the items' and the coins' prompts, the coin questions'); taking an item's word when overloaded and whether a counted item joins its kind; the vault's menu items, its line and the deposit's question (View's money's too); `DAMAGE`'s saving throw bonus bits, whether a save made halves the damage, its key; `ADD EP`'s words and whether those who cannot act are passed over first | none (`TREASURE` reads `ITEM<file>`); no, no, no; `There is still treasure left.  ` ..., `Take: `, `Items: `, `Select type of coin `, `How Many `/`How much `; `OverLoaded`, no; none; 0x1f, no, `press <enter>/<return> to continue` (`2fd3:2c80`); `Congratulations ` ... `gains experience!`, no (`2fd3:36dc`) |
| `creation` | Create New Character, Modify Character and the icon editor where the games differ (`create.c`, `modify.c`, `icon.c`; their lists and menus by letters or by index as the `menus` section's routine): the lists' headings; by class id the levels and experience it starts with, a paladin's effect, the classes with no spells' block; the money (the classes' dice, or so much steel); a mage's spells and those creation's training gives by mage level and order (`cok_rules_created_spells`); the reroll's, name's and save's words and whether the save names the character and starts on Yes; the items every class starts with (`cok_rules_kit` by kind, or a knight's alone); Modify's experiences of a new character, its prompt, the race and class whose charisma is held at 17, whether Keep sets `SHARES`, its most hit points (`cok_most_routine`); the icon editor's words, frame slot and most head and body | `Pick Race` ... `Pick God`, `Pick Alignment`; `4def:0ae0`'s levels and experience, no paladin effect, none; the dice; 11, 18, 12, 15, 21 and levels 2-5 any order; `Reroll stats? `, `Character name: `, `Save NAME? ` from No; a knight's (`4def:5534`); 1500, 1251, 2000, 2001, 2251, 2500, 5000 and the level changed at 0, `Modify: `, none, yes, `4def:48e9`; `Is this icon ok? ` from No, `Sorry, not in CGA`, slot 25, 13 and 31 |
| `levels` | training, the knights' orders, aging, Memorize and Scribe where the games differ (`train.c`, `magic.c`, `camp.c`; see Training and Camp): Train's words, whether its question starts on Yes, whether free training trains, the highest levels by race and class in place of the kender's limits, the row of a class without one, whether a ranger learns only when it rises; the fixed hit points at a class's top level and a floor of 0; Knight Change's words, whether Change is always offered and special keys do nothing, the level from which a lost level costs a fixed 2; the aging after a battle (by race the ages, and its words); the spell lists' prompt, the memorize table's labels and columns, how the spells marked are kept (a list with Keep, or a question) and Scribe's questions and words; whether the camp unmarks the spells to memorize; Display's headings; the Magic menu's own selection; the camp's Fix (`cok_fix_routine`: Champions' or Death Knights') and the rest's words (the day's healing, an encounter); the rest menu's items, `Stop Resting? ` and whether it starts on Yes, whether the camp's Rest starts the menu from its first item; the camp Save's quit question | `4def:4d9e`'s words, from No, free training, the kender's limits, row 0x74, every time; 2, 0, 3, 0, 2, 1, 2, 3 and wrapping; `Knight Of The `, `Sword`, `Rose`, not always, F9 as C, none; no aging; `Choose Spell: `, `    Cleric Spells:` ... at 20, the questions `Memorize These Spells? `, `Scribe These Spells? ` in yellow from No; yes (`4888:06b8`); 11; the last menu's; none; `Rest Days Hours Mins Add Subtract Exit`, `Stop Resting? ` from No, the first; `Quit TO DOS ` |

A `NULL` stem says the game has no such archive (Death Knights has no
`SKY`, `CURSOR`, `RANDCOM`, `HEAD` or `BODY`): code that reads it must not
be reached for that game. `archives` also holds `ground` and `backdrop`
(Death Knights' `SKYGRND` and `BACK`, `NULL` in Champions; the view draws
what was loaded, see 3D view) and, for a game that picks a record's file by
the record's id rather than by the ECL file, `pic_files`, `sprite_files`
and `wall_files` (256 file numbers each, 0 for the ECL file's; `NULL` in
Champions). Most record ids are still in the code that reads them (`SKY`'s
250-252 and `SKYGRND`'s 1 and 2 at startup, `RANDCOM`'s 1, the music
drivers' 4 and 5); the startup's tile sets and icons are the `startup`
section's; one that a game reads from another file goes in a
`cok_rules_record` (Death Knights' cursor is `8X8D1` record 204).
`archives.monster_spells` names a fourth kind of monster file, the
monsters' spells, where a game keeps them apart from the record (Death
Knights' `MON1WIZ`, as its characters' `.WIZ`; `NULL` in Champions,
whose record holds them): `cok_monster_read` reads a monster's record
of it, where there is one, into the record's spells' block.

To add to it: put the field in the section of its concern (a new
section, a struct of its own, for a new concern: the character record's
layout, the data segment's tables, the ECL's, the spells', the routines
that differ as function pointers), with a comment saying what it is and
where the original holds or reads it; give Champions' value in
`src/rules.c`; read it through `cok_game_rules(game)` (or the descriptor
passed in) where the literal was; and check it in `tests/test_rules.c`
(every file Champions' descriptor names must be in `COKAssets/`). The
message strings the game shows stay where they are. The port's own
messages that name a file build the name from the descriptor; the tests
check them (`tests/test_monster.c`'s damaged files: `MON1CHA`, `SPC`,
`ITM`, `GEO1`, `WALLDEF1`, `SPRIT1`; `tests/test_debug.c`'s `DEBUG.TXT`
without a saves directory), but for a few checked by review only:
`RANDCOM`'s "record 1 is too short" (unreachable with a record that loads
as its six tiles, which are longer than the map it would hold), and the
font's, `PIC`'s and `ECL`'s load errors. To add a game: a descriptor of its own
in a file of its own (`src/rules_death_knights.c`; every program that links
`src/rules.c` links it, as `src/data_champions.c`), listed in `games`
(`src/rules.c`), which `--game` then finds.

### Death Knights

`cok_rules_death_knights` (`src/rules_death_knights.c`, its addresses
`dkk SEG:OFF` in `build/death-START_FULL.EXE`, `make death-merged`, its data
segment at `DS` 0x1bce) is DEATH-KNIGHTS.md's step 1 and step 2 (its
frames, characters, saved games and front end) and the first three parts
of step 3 (the encounter, into and out of each fight; the fight, its
rounds, the computer's turns and melee; the player's commands; View and
the items menu; and of the later parts, among them, the shops and the
temple): the tools
and `eclplay
--game death-knights` read every one of its archives and run its scripts,
their text, menus and choices, its 3D view and its overland map, read,
recompute and write its characters and monsters and import Champions'
characters, and `cok` and `eclplay --start-menu` play it from its title,
through its start menu and saved games, into its fights: the battlefield
set up and the combatants placed as the original's, the fight played by
keys from the player's menu, or to its end with the party on Auto (Alt-Q
at the player's turn, `eclplay --combat auto`) with the original's dice,
or left with `eclplay --combat won`, `fled` or `lost`, and the script
going on. It holds:

- its files: Champions' stems but `SKY`, `CURSOR`, `RANDCOM`, `HEAD`,
  `BODY` and the music's (`NULL`: MUSIC.DAX's songs are step 4), and
  `SKYGRND` and `BACK`; the digit of `MON`'s and `CPIC`'s files always 1
  (`46b2:198c`, `7090:016d`); `PIC4-7` and `SPRIT4` by picture id
  (`6ca1:00a4`), `WALLDEF`, `8X8D` and `BACK` by wall set id (1 in file 1,
  2 in 4, 3-4 in 2, 5-6 in 3, 7 in 5: `6d21:0ec5`); `DKK.CFG`, `C:\DKK\`;
  the saved games' names (`SAVGAM?.PTY`, `CHRDAT`; see Party), the
  characters' extensions (`.GAM` in a saved game and `.PC` in the roster,
  `46b2:0abc`; `.WIZ`, `.JNK`, `.EFF` beside them, `DS:71c0`); the image
  lists (`PIC1-7`, `SPRIT1-4`, `BACK1-5`,
  `SKYGRND`, `8X8D1-5`, ...: 30 archives) and 0xff frames meaning 256
  (`123c:1c58`);
- its ECL table (`3632`'s handlers, the dispatch `3632:2a54`, the skips
  `3c1e:1b48`): Champions' but for ten opcodes, which ecl.h names past
  Champions' meanings (`COK_ECL_SETUP_MONSTERS` and on), among them
  `TREASURE ITEMS` and `STOCK` (the treasure section's), and `DAMAGE` and
  `ADD EP` by Champions' with the treasure section's rules; `CALL`'s
  redraw at `2dcb`; byte variables,
  its named variables, special addresses and character fields (see ECL
  scripts);
- its view: tile values 1-0xff all from tile set 0 and 0x100-0x12c from
  the frame's (`DS:43fa`, `4404`, `70eb:01f5`), the frame's set loaded
  with colour 0 transparent, so that its colour 13 shows (the moons'
  magenta), the others with 13 (`70eb:002f`), wall slot 0 first, one tile
  set of 256 tiles for a record's three wall sets, no tile moved
  (`6d21:0ec5`), the overhead map's 0x119 and 0x129 (`6d21:0000`), the
  overland map's `PICTURE 0x70`;
- its startup: the frame's tile set alone, `COMSPR` 0-21 into 13-34
  (`4f9a:00e1`), 35 icon slots (`DS:b474`, cleared at `4f9a:0556`);
- its frames (`110f`): other tiles of its frame's set, from 0x100: one
  frame (`110f:0000` with 0; every caller outside `110f` passes 0, so
  Champions' open frame is the same frame), row 0 from `DS:070c` with the
  moons, row 23 from `DS:0734`, the sides tile 7 between; the dividers
  across with their own ends (`DS:075c`: row 16 of the adventure screen
  `0162` and a big picture's `02ab`, row 2 of the spell lists' `0537`),
  column 16 from `DS:0784`, the 3D view's box with corners 0-3, rows of 4
  and columns of 7; Memorize's (`02f8`, `110f:0000` with 2: rows 1-16
  cleared), combat's (`0345`, `110f:0000` with 1: all cleared, the bottom
  row 22, column 22 from `DS:07ac`), the sheet's (`0392`: rows 8, 16 and
  20 inside the sides `DS:07d9` and `07ef` + row, column 19 from `DS:07fe`
  + row); the credits the frame (`35c6:026e`); after the start menu main
  draws the adventure screen always (`1000:01d1`), and Begin Adventuring
  a loaded game's frame by the mode (`49cb:010b`: 0 and 4 the adventure
  screen and the party list, 3 the big picture's frame); its CGA backdrop
  is the other modes' (`6d21:0177`: no line row, the pictures drawn);
- its data segment's tables (`src/data_death_knights.c`): all but the
  treasure templates and the temple's diseases, which it has no
  counterpart of, each with the routine that reads it (see The data
  segment's tables);
- its spells and effects (see The spells and effects): names (`DS:5825`
  + 41 × id, ids 1-0x72) and headings (`DS:3770` + 11 × level), the
  handler tables (127 of its 235 ids run Champions' handler, the others
  are unported), durations (`5dc5:0ef5`), byte 7's party value 3
  (`5dc5:1182`), the event lists (`638c:055b`), its cast (`5dc5:1298`)
  and magic resistance (`638c:04c9`, see Death Knights' casting; its
  caster level and saving throw: see Death Knights' fight); `MON1WIZ`;
  its moons'
  tiles, 0x100 + phase + 0x15, 0x0d and 0x11 (`5b7d:0000`);
- its characters (see The character record's layout): the record's 216
  bytes (`.GAM`, `.PC`, `MON1CHA`), every field the code reads at its
  place there and the spells' block of 220 bytes (`.WIZ`, `MON1WIZ`) held
  after it; its recompute, the stats `65cd:0e46` (the movement with the
  effects that double and halve it, `65cd:1241`; no fighter level) and the
  levels `69a8:050b` (spells a day in the block to 8 levels, paladins',
  3 or 4 attacks, its own thief skills, no former classes), as
  `characters.stats` and `levels` select; and the import of Champions'
  characters (`characters.import`, `46b2:10d7`, below);
- its saved games (2913 bytes: byte variables, four wall entries, the
  first alone loaded again, the last mode kept: see Party), the bank
  vault's files (`VAULT?.DAT`, `TMPVAULT.DAT`), and the savings variable
  (`0x4c33`, a word over two) a Champions game's load copies;
- its front end (`start`, see The start menu): the title (`35c6:051c`)
  and credits (`35c6:026e`), its start menu (`49cb:010b`, `DS:0cf8`) and
  its words, Load and Add from Death Knights or Champions (`--import`),
  a Drop or Remove that empties the party ending the game, Exit's No
  saving then quitting, Begin's mode from the last mode; and the start
  menu's command it does not carry out yet, which stops the run citing
  its routine: Initialize Mouse/Joystick (`52cd`);
- its creation (the `creation` section; see Death Knights' creation):
  Create New Character (`49cb:05c7`), its icon editor (`3cac`), its
  items (`4bea`) and Modify Character (`25d4`);
- its encounter and battle (the `combat` section; see Death Knights'
  encounter, battlefield, battle and end of combat): `COMBAT`'s vault
  first, the show at distance, leadership, its own open ground, the
  dungeon's fill and corners, the ambush's placement, a round ended by a
  side gone, no exploding dead, the end of combat's effects, may-train
  and words;
  its combat variables (`0x7ec0`-`0x7f72` at Champions' addresses, as its
  table `DS:836`-`864` has them; the vault's `0x4d00`, the river's
  `0x4bf3`); no former classes (`cok_character_former_class` false, the
  former levels read as 0: `cok_character_former_level`);
- its fight (the `fight` section; see Death Knights' fight): the words of
  its rounds, turns and attacks, most in lower case; the player's turn
  (`3e05:08af`, its menu and commands below); the computer's turns (`3f63:0000`) with its own spell choice (`05d7`), no
  items' spells, a paladin's turning; its attacks (`5340`), turning undead
  (`119b`), the free attacks, the death on the battlefield (`6f03:0fbd`),
  a lost spell marked; its clouds (Stinking Cloud's and Cloud Kill's,
  `5dc5:2934`, see Death Knights' clouds); its saving throw (`638c:10f8`) and the
  effects its first fights carry (0x07, 0x4f, 0x50, 0x5c, 0x5d, 0x69,
  0x6a, 0x6c, 0x72, 0x73).
- its effects (the effects section; see Death Knights' effects): the
  handlers that changed, every effect its monsters carry and its level
  drain (`504d:14c7`), the dragons' breaths, the vampires' gaze, a
  monster's Fireball and the fire minions' flames; six new effects left
  (0x02, 0x04, 0x15, 0x22, 0x30, 0x3a).
- its menus and the player's commands (the `menus` and `commands`
  sections; see Library's menus by index and Death Knights' commands): the
  menu loop by index with two rows (`6a69:0f1b`), its Yes/No (`2048`), the
  adventure's menu (`4307:0257`), a special key's pick (`580d:2bb2`); the
  player's turn's menu (`3e05:0ccc`) and commands (`08af`), Move
  (`10bb`), aiming (`5340:3888`), Speed (`3e05:14aa`), Attack Ally
  (`5340:2b81`), the cursor's box (icon slot 0x1c) and the missiles' icons
  recoloured as `CPIC`'s (`7090:016f`).
- its View and items menu (the `items` section; see Death Knights' View):
  View (`580d:0c66`) and the items menu (`0ffa`) by index with their own
  selections, the list `6a69:1b82` (`cok_menu_indexed_rows`), the readied
  column a space shorter, Ready's thief level, Deposit in the vault for
  coins and items, Heal and Cure, Sell and Id with its words and its
  steel, and camp's menu keeping its own selection (`4381:3106`);
- its treasure (the `treasure` section and `COK_DATA_STOCK_*`; see Death
  Knights' treasure): the item table `ITEM0`, `TREASURE ITEMS`, `STOCK`,
  the treasure menu, Take, the bank vault's menu and its
  deposit, the vault's mode 7, `DAMAGE` and `ADD EP` with may-train;
- its shops and temple (the `shops` section; see Death Knights' shops):
  the shop (`3b89:06f3`), Buy (`0453`) paid in steel, Appraise
  (`5c72:0dc2`), the temple (`3999:07d3`) and its free cures (`04e2`),
  mode 1's redraw with the small picture always (`65cd:2db0`);
- its levels (the `levels` section; see Death Knights' training and Death
  Knights' camp): Train Character (`49cb:43b6`) to level 18 by the
  highest levels of `DS:82d4`, Knight Change Order (`49cb:4ea4`, `4da1`),
  the aging after a battle (`3e05:0264`) that its haste (effect 0x27,
  `504d:0d88`) asks for, Memorize, Scribe and the spells' table to 8
  levels (`4381:0f67`, `115d`, `09c7`), Display (`4381:15aa`), the camp
  unmarking only the scrolls (`4381:0626`), and the names of those who may
  train in light magenta (`65cd:1b3b`); and camp's Fix (`4381:2fd4`).

`cok --game death-knights` and `eclplay --start-menu` play it from the
title into its fights: a saved game loaded (its own or Champions'), or
a party added from the roster or from Champions', saves and resumes, and
the adventure in its scripts, 3D view and overland map, camp (Magic's
Memorize, Scribe and Display, Fix), `PROGRAM`'s start menu (Train and Knight
Change Order too), the encounters and their battlefields, their fights on Auto
(the player's turn by keys, `3e05:08af`: its menu, `3e05:0ccc`, Move,
Aim, Cast up to the cast entry, Turn, Guard, Quick, Delay, Bandage, Yell,
Speed, End, Alt-Q and the hidden keys, View and Use with Death Knights'
sheet and items menu; `eclplay
--combat auto` presses Alt-Q, `melee` plays every turn as the port's
melee, and `won`, `fled` and `lost` decide the battle without its
rounds), their end
of combat and treasure menu, the bank vault with its View and Items, camp's
View and Items, the shops (`COMBAT`'s and `STOCK`'s) and the temple, until
a routine not ported yet,
a spell
(Death Knights' casting lists those that cast) or an effect not ported
yet (Death Knights' effects), a
spell or effect it does not name, or a start menu command above; there the run
stops with the reason (`not yet ported: ...`), `cok` showing it on the
screen and quitting, as it does at any stop: the original never returns
to the start menu from the middle of a script, and the port's state there
is half done. `--load` and `--party` read its saved games.

Its characters' files: a record of 216 bytes, read whole into a zeroed
record and written whole (`46b2:10d7`, `46b2:0abc`); the spells' block,
read from `.WIZ` if there is one (what there is of it; the original says
"problem openning magic file" when it is short) and written if the record
holds one, the file erased first; items (`.JNK`) and effects (`.EFF`) as
Champions' `.STF` and `.SFX`: 63-byte records with the next pointer
at +0x2a, 9-byte ones with it at +5. A roster file `NAME.PC` is a saved
game's `.GAM` by another extension, its other files beside it (the name
made as Champions' roster's, `15fe:0580` and `064f`, identical to
`169c:050b` and `05da`: `cok_party_roster_name`). Its monsters (`46b2:19a2`):
`MON1CHA`'s record (216 bytes), its spells' block from `MON1WIZ`'s record
of the same id where there is one (14 of the 64), its effects (`SPC`) and
items (`ITM`); ADD NPC then joins it (`46b2:1d90`, its levels recomputed,
`69a8:050b`). Every one of `DKKAssets/SAVE`'s characters reads and writes
back byte for byte, but the far pointers the original writes as they are
in memory (`tests/test_party.c`), and its recompute gives each one's
derived fields back from nothing.

The import (`cok_character_import`, the start menu's Add Character to
Party, Champions; `46b2:10d7` with `DS:c468` 0): a Champions roster file
(`*.WHO` in the path of `DKK.CFG`'s fifth line), its 409 bytes read into a
zeroed block, moved by ten runs into the record zeroed (the descriptor's
`cok_rules_import`: Champions' `+0x00-0x1d` to `+0x00`, `+0x58` to `+0x1e`,
`+0xcf` to `+0x29`, `+0xda` to `+0x31`, `+0xf3` to `+0x3f`, `+0x11a` to
`+0x6b`, `+0x130` to `+0x6d`, `+0x135` to `+0x70`, `+0x142` to `+0x7c`,
`+0x188` to `+0xc7`), the pointers cleared; for a class but fighter, thief
and fighter/thief (2, 6, 0x0e) a spells' block with Champions' memorized
spells, known spells 1-107 and four classes' spells a day, 5 levels into
rows of 8; the age capped by race (1200, 1200, 200, 300, 300; a half-elf's
above 199 set to 70; 50); its items but those of types 42, 47, 58, 60, 63,
69 and 72 (the set at `46b2:10b7`) and its effects as they are; then
recomputed as a load is. Spell, item and effect ids are not translated:
Champions' ids stand for other spells in Death Knights, and Champions'
former levels (`+0x101`) land in Death Knights' levels before a drain
(`+0x4d`). The original's load also recomputes strength between the
stats and the levels (`638c:16d8` with 0, the effects engine's): the port
does not yet, which changes nothing a file holds unless an item or effect
gives strength by a rule that differs. Its stats add var `0x7f72` (the
party's movement bonus) to a party member's movement, which no Death
Knights script sets: 0 in the port's stats, which have no ECL variables.
Each of `SAVE/`'s Champions characters imported and written gives, but for
the far pointers, the files the original wrote for it (Add Character to
Party, Champions, then Save Current Game, in DOSBox-X: `tests/test_party.c`
holds their digests).

What it has not, or the port does not reach yet, is left out: a field of
the character record it has not (the fighter level, the level a human
changed class at, the levels shared out, an NPC's shares, the former
levels; the code that reads those asks the descriptor first: no former
classes, `cok_levels_former_classes`, the former levels and the level a
class was changed at read as 0, no sweeps, no shares) stops at its
accessor's assertion ("the game's records have no such field"), as does a table of the data segment it has not ("the game's
data segment has no such table"), a spell or effect it does not name
("the game has no such spell", "effect") and a variable it has not
(`cok_ecl_var`: "the game has no such variable"); the four opcodes not
carried out yet stop the run with `COK_ECL_UNPORTED` (see ECL scripts).
Champions' rules that read the former levels compute, with them 0, what
Death Knights' routines compute where those are the same (the backstab
`5340:27c1` and its damage `014f`, the scrolls `5dc5:08a5` and Use
`580d:1dd7`); who may use a class's spells (`5dc5:0078`) and the fighters'
classes (`638c:16d8`, `15e0`: paladins too) are the descriptor's; the
others differ beyond that: turning undead (`5340:119b`, paladins, levels
clamped, var `0x4cff`) is the fight section's own routine; the combat
menu's Turn (`3e05:0ccc`: clerics, and paladins above 2) is the commands'
routine's. The party list in its frame
is Champions' (`6346:07ba`; dkk `65cd:07a5` colours AC and hit points by
other ranges of its record, `+0xcc` and `+0xd6`), drawn with what the port
reads of the party.
`tests/test_death_knights.c` checks the descriptor against
`build/death-START_FULL.EXE` (its tables byte for byte, the level tables
following each other without a gap, each table's base in the code that
reads it, the names, the tile sets' bounds, the
wall sets' and `SPRIT4`'s files, startup's names, the version; the
frames' tables (each line's tiles a table `110f` reads, by the cell's
column or row), the moons' offsets in `110f:0000`, the frame's set's
transparent colour (`70eb:002f`), startup's `COMSPR` loop (`4f9a:07a6`)
and the icon slots' table it clears (`4f9a:0556`); the ECL
table, by following the compares of the skips `3c1e:1b48` and the dispatch
`3632:2a54` for each opcode and reading the operands its handler loads
first; its `0x7c00` variables' offsets, `DS:836`-`864`; the special
addresses `3c1e:0d85` compares and `0bc1`'s bound of 49000; the import's
moves in its code, the classes without a spells' block, the set of item
types dropped, the record's and block's sizes, the files' extensions;
and that each field Champions' import moves lies where the move puts it;
the encounter's section against its code: `COMBAT`'s branches in order,
`SETUP MONSTERS`' operand, the placement's ambush, its clears, distances,
facing and cell under a body, the setup's line and tiles, the river's
variable, leadership's tests, chances and words, the battle's routines,
the end of combat's effects, words and treasure menu, may-train's row and
table, who may use spells and the fighters' classes; the fight's section
against its code: its words, the clouds' routines (where the round's
end calls them, their words, Cloud Kill's levels, the rounds' compares,
the cover), the probe's tiles, the computer's turn's first target, its
limit and paladins, its spell's columns and levels, reach, morale and
flight, the attack's sides, the free attacks' effect, the quiet bit,
the lost spell's mark, the arrows, the missile's clamp, turning undead's
levels, clamps, variable, table and dice, and the saving throw's effect
0x39; the treasure section against its code: the item table's loader
and its call, `TREASURE ITEMS`' and `STOCK`'s tables and bounds, the
treasure menu's, Take's and the coins' words, taking an item's join, the
vault's items, line, deposit and the routines it calls, `DAMAGE`'s and
`ADD EP`'s constants and words;
the levels section against its code: Train's free training cleared, its
tables' bases, words and question, the routines it calls, the hit points'
fixed amounts and floor, Knight Change Order's words, menu, experience,
scores and fixed loss, the aging's ages and words, the memorize table's
labels, levels and columns, the lists' prompts, Scribe's question and
words, the camp's forget, Display's bound, texts and heading colour; the
items section against its code: View's and the items menu's words and
list, the readied column, Ready's thief level, the words where their
routines load them, the Yes/No's first, Sell's count and Id's steel, Heal's
class, effect and minutes, Cure's byte; camp's own selection)
and
`DKKAssets/` (each file and record it names, each picture and wall set in
the file its id names and no other, `COMSPR`'s icons), its view's wall
sets and tiles, its frames drawn (every screen's tiles by cell) and its
CGA backdrop; `tests/test_start.c` Begin Adventuring's frame for a game
loaded by mode and main's after the start menu, on a copy of Champions'
descriptor with those fields of Death Knights';
`tests/test_death_knights.py` the tools over `DKKAssets/` and `eclplay`'s
runs of its blocks, and its front end (the title to the game, Load and
Save of `DKKAssets/SAVE`'s games, Champions' party, the stops); its
descriptor's front end against the original too (`test_start` in
`tests/test_death_knights.c`: the start menu's items and words, the
title's pictures, waits and credits, a new game's and the
demonstration's blocks, the end of a game's wall entry, the saved games'
and vault's names and layout, the loader's one wall entry and kept last
mode, the roster's NPC flags and sizes); `tests/test_start.c` its saved
games, vault and start menu (`test_death_knights_saves`, `_menu`);
`tools/dosbox/checks/dkk_views.py`, `dkk_start.py`, `dkk_fight.py`,
`dkk_treasure.py`, `dkk_levels.py`, `dkk_items.py` and `dkk_shops.py` the original (see Checks against the original);
`tests/test_death_knights.py` its first fight on Auto, by Alt-Q and by
`--combat auto`, 13 rounds as the original's; `tests/test_fight.c` its
fight on records made by hand (the cloud and pool tiles, a death that
finds its body, turning undead's levels and count, the nightmares' smoke,
the half damage of 0x5d, Alt-A, the computer's spell choice, the clouds:
laid, coughing and choking, Cloud Kill's deaths, aged, dispelled, a body
under one);
`tests/test_effect.c` its caster level and saving throw (and Champions'
moon); `tests/test_combat.c` its open
ground's builder on the first fight's seed against the map the original
builds, and a river's shape.

### The character record's layout

A character stays a record of raw bytes (`cok_character.record`), as the
files and the original hold it: the ECL reads its fields by offset
(0x7c00-0x7fff) and the saved files must be byte-exact in each game's own
layout. Code reaches a field by name through the game's layout,
`rules->character` (a `cok_character_layout`, `src/field.h`), which gives
each field of the enum `cok_field` its offset, width (1, 2 or 4 bytes,
little-endian), element count and first index for an array (as the
original's code counts them, a Pascal array's lower bound: the thief
skills 1-8, the class levels 0-7, the coins 0-6, known spells by spell id
1-108, the attacks left and their dice by attack 1-2, the icon's colours
1-6), the stride between elements (the scores, base and current,
interleave), rows for a table (spells a day: 4 spell classes by levels
1-5), whether the original reads it as signed (a record of the original,
for a later game's conversions: the accessors read any byte either way),
and whether it is a far pointer (meaningless in a file; 0 in the port). Champions' table, with
what each field is and where the original reads it, is
`champions_character` in `src/rules.c`; the layout's `size` is the
record's, at most `COK_CHARACTER_MAX`, the buffer every game's fits in,
and `file_size` the bytes of its file (`.SAV`, `.WHO`), which
`cok_character_read` and `write` read and write.

Death Knights' (`death_knights_character` in `src/rules_death_knights.c`,
each field with its evidence: where the import `46b2:10d7` moves
Champions' field to, and the offsets the routines matching Champions'
have where Champions' have the field's) is the record of its files, 216
bytes (`file_size` 0xd8), and after it, in the same buffer, the spells'
block that the record points to (`SPELLS`, a far pointer at +0xc2) and a
file of its own holds (`.WIZ`, `MON1WIZ`): `spells_offset` 0xd8 and
`spells_size` 0xdc, `size` 0x1b4, `COK_CHARACTER_MAX` (Champions' record
409 bytes). The block holds the memorized spells (74), the known ones (by
id 1-0x72) and the spells a day (4 spell classes, levels 1-8 in rows of
8), so that the accessors reach them as any field
(`cok_field_in_spells` says a field is the block's). A fighter or thief
has no block (the original's pointer nil): the port marks one held with
the pointer's first byte 1 (`cok_field_set_spells`,
`cok_field_has_spells`; 0 in files, the block's bytes 0 without one),
which the loaders set, the writer reads, the levels' recompute and the
ECL's fields test as the original tests the pointer (`69a8:0000`,
`3c1e:073a`). The original's arithmetic past a field (`size` bounds it)
reaches the block, where the original's reaches the heap after its
record. Death Knights has no fighter level, level a human changed class
at, levels shared out, NPC's shares or former levels (width 0); its own
fields are the levels, experience and maximum hit points before a drain
(`UNDRAINED_LEVEL` at +0x4d, `_EXPERIENCE` +0x66, `_MAX_HIT_POINTS` +0x6a:
`504d:14c7` keeps them, Restoration's `3999:045f` compares them), which
Champions has not. No two of its fields share a byte, and one byte, the
import's 0 at +0xc6, is none's.

The accessors are `static inline` over the table, each check folding to
a compare or two (a combat round calls them often: the stat routines
alone, recomputing a character a million times, take a quarter longer
than with literal offsets, a tenth of a microsecond a character; the runs
of `tests/test_eclplay.py` take no measurably longer). Each takes the
layout (named `L` by convention: `const cok_character_layout *L =
cok_game_rules(game)->character;` once at a routine's top), the record,
the field and, for an array, the index (any integer type, as a subscript;
`_at`), or for a table the row and index (`_at2`). They are typed as the
expressions they replace were, which `-Wconversion` needs:

| Was | Is |
| --- | --- |
| `c[0x197]` | `cok_field_u8(L, c, COK_FIELD_HIT_POINTS)` |
| `(int8_t)c[0xf9 + class]`, `(int8_t)c[0x18d]` | `cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class)`, `cok_field_s8(L, c, COK_FIELD_AC)`: every `(int8_t)` read is `s8`, whatever the field |
| `c[0x100] > c[0xce]` | `cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > ...`: every plain read is `u8`, whatever the field |
| `u16(c + 0x17d)`, `c[0x17d] \| c[0x17e] << 8` | `cok_field_u16(L, c, COK_FIELD_WEIGHT)` |
| `word(c + 0xeb + 2 * i)` | `cok_field_u16_at(L, c, COK_FIELD_COINS, i)` |
| the four bytes of +0x116 | `cok_field_u32(L, c, COK_FIELD_EXPERIENCE)` |
| `c[0x11b + k * 5 + level]` | `cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, k, level)` |
| `c[x] = v`, `put16`, ... | `cok_field_set8`, `set16`, `set32` (and `_at`, `set8_at2`) |
| `c[x] = (uint8_t)(c[x] + d)`, `++c[x]` | `cok_field_add8(L, c, f, d)` (and `_at`, `_at2`) |
| `&c[0xd0 + type]`, `c + 1`, `memset(c + 0x11c, 0, 0x14)` | `c + cok_field_offset_at(L, f, i)`, `cok_field_offset(L, f)`, `cok_field_size(L, f)` |
| `i <= 0x39` over the memorized spells | `i < cok_field_count(L, COK_FIELD_MEMORIZED)` |
| `for (i = 1; i <= 6; ++i)` over the coins but silver | from `cok_field_first(L, COK_FIELD_COINS)` (at least 1) to below first + `cok_field_count` |

Each asserts that the game has the field (a game without one gives it
width 0: `cok_field_present`), that the index is one of its elements, that
the access is as wide as the field, and that a single value's form is not
given an array. The assertions are compiled into the builds that play
(no `-DNDEBUG`): one that fails stops `cok` and `eclplay`. Where the
original computes an offset it does not bound (`cok_magic_slots`' class
and level past the table, reaching other fields or the far pointers),
`cok_field_offset_unchecked` does its arithmetic from the layout and
`cok_field_pointer_at` says whether a byte is a far pointer's; the record
is then indexed with the result, checked against the layout's `size`.

Every routine reads every character field through the layout: no file
but `src/rules.c` names a character offset (the item records, 63 bytes, the
effects, 9, and the combat record are not the character record and keep
their offsets). They were converted in four parts: `party.c`, `items.c`,
`magic.c` and `sheet.c`; combat's in `attack.c`, `round.c`, `ai.c`,
`cast.c`, `effect.c`, `combat.c`, `arena.c` and `cloud.c`; those of
creation, Modify, training, the camp, the shops and temple, the treasure,
the icon editor, the combat commands and the roster in `create.c`,
`modify.c`, `train.c`, `camp.c`, `shop.c`, `treasure.c`, `icon.c`,
`command.c` and `roster.c`; and the ECL's, the adventure's, the monsters'
and `eclplay`'s in `ecl.c`, `adventure.c`, `monster.c` and `eclplay.c`,
with the debug mode's names in `debug.c`. The ECL's VM has the rules
(`cok_ecl_init(vm, rules, hooks)`, asserting them, as its first character
field read does again for a VM made otherwise), and its table of the
fields at `0x7c00`-`0x7fff` gives each one's field and element where it gave
an offset (see ECL scripts).
Those that took only a record take the rules after it
(`cok_character_stats(character, rules, types, ...)`,
`cok_character_former_class(record, rules)`, `cok_character_damage`, `heal`,
`allowance`, `strength_bonus`, `dexterity_missile`, the cleric spells,
saving throws and thief skills, `cok_party_add(party, rules, character)`,
`cok_party_draw`, `cok_magic_sort`, `cok_magic_slots`;
`cok_combat_damage(combat, c, rules, ...)`, `cok_combat_opposite(c, rules)`,
`cok_combat_auto`, `cok_combat_place(combat, mode, character, rules, ...)`,
and after the party `cok_combat_count_sides` and `cok_combat_enemy_health`;
`cok_create_icon(record, rules)`, `cok_create_knight_items`,
`cok_modify_least` and `cok_modify_most(c, rules, selected, ...)`,
`cok_train_row`, `cok_train_constitution` and `cok_train_hit_die`,
`cok_camp_preparation(character, rules)` and `cok_camp_forget`,
`cok_shop_money` and `cok_pool_pay(c, rules, steel)`;
`cok_monster_movement(party, rules, ...)` and
`cok_debug_name(record, rules, out)`); callers pass
`cok_game_rules(game)` (a spell's handler `cok_game_rules(cx->game)`), the
effects engine its own `fx->rules` (`cok_effects_init` takes them),
`cok_combat_records` the rules it is given, and tests
`&cok_rules_champions`. Four indices stay the original's unbounded
arithmetic, through `cok_field_offset_unchecked` and each site's own bound
as before: a helpless target's blow at attack slot 0 (`ATTACKS_LEFT`'s
offset for slot 0 is `AC_BEHIND`'s), checked against the record's size; a
saving throw's signed type (`SAVING_THROW` from `+0xd0`), likewise;
`cok_combat_damage_roll`'s slot, which its guard bounds at 2 only, so that
slot 0 reads `DICE`, `DICE_SIDES` and `DAMAGE_BONUS` one byte early
(`+0x190`, `+0x192`, `+0x194`), as the original's `+0x190 + slot` does; and
the spell scribing makes known (`KNOWN_SPELL` from `+0x62`), a scroll's byte
& 0x7f, which for 109-127 writes `+0xcf`-`+0xe1` past the known spells, as
the original does, the record's 409 bytes bound enough. Modify's name editor
reaches the name as the string it is (`cok_field_offset` of `NAME`), indexed
by the cursor and the length byte as before, which a file may make more than
15; the roster's list takes a `.WHO` of the layout's `file_size` and tests
the jewelry's low byte (`COINS` 6's offset, `+0xf7`). The indices the ECL
gives (the thief skills at `0x7ca5`-`0x7cac`, read and in `CHECKPARTY`, and
the spells memorized written at `0x7c20`-`0x7c59`) go through the checked
accessors: each address range is the field's elements exactly, 1-8 and
0-0x39, so the range's own test bounds them. A monster record is the
layout's `size` long, and its far pointers are cleared by field.

To add a field: name it for what it is in `cok_field` (`src/field.h`),
with a comment; give Champions' place in `champions_character`
(`src/rules.c`) with the macro of its shape (`BYTE`, `SBYTE`, `WORD`,
`DWORD`, `BYTES(offset, count, first)`, `SBYTES`, `WORDS`, `INTERLEAVED`,
`POINTER`, `POINTERS`) and a comment saying what it is and where the
original reads it, and Death Knights' in `death_knights_character`
(`src/rules_death_knights.c`; width 0, left out, where it has none, and a
field only it has left out of Champions'); and add it to `expected` and
`dkk_expected` in `tests/test_layout.c`, which check every field's place
and that no two share a byte (a field that does, as known spell 108 and
the highest fighter level do in Champions', is listed there as
documented); `tests/test_death_knights.c` checks that a field the import
moves lies where the move puts it.

To convert a file: take each raw character offset (`c[0x...]`,
`record[0x...]`, `c + 0x...`, a `u16`/`word` of one, a loop bound that is
an array's count or first index), find or add its field, and replace the
expression by the accessor of the same type: a plain byte read by `u8`,
an `(int8_t)` one by `s8`; a routine that has a game declares `L` at its
top, one that only has a record gains the rules after it and its callers
pass theirs. An index may go through a checked accessor only when it is a
constant, a loop bounded by the field's elements, a choice already checked
against them, or the game's own tables; one that comes from data (a
byte of an item, a monster, a file or the ECL: camp.c's scroll spell,
`item byte & 0x7f`, up to 127 where known spells end at 108; attack.c's
attack slot 0, which reaches AC_BEHIND by the original's `+0x18e + slot`)
goes through `cok_field_offset_unchecked` and the site's existing bound
(the record's size, as there), never through a checked accessor that
would stop where the code before went on.
Count the offsets before and after (none left but those an unbounded
original index needs, as above); then the golden outputs and the DOSBox-X
checks' replay show nothing changed.

### The data segment's tables

The tables the port copied from the original's initialized data (its data
segment, `DS`) are the descriptor's: `rules->data`, a `cok_data_segment`
(`src/data.h`). Champions' bytes are in `src/data_champions.c`, which only
the descriptor (`src/rules.c`, `champions_data`) includes; shared code and
the tests read them through the descriptor, never by name, so that
Death Knights' tables, moved and reshaped, go beside them without touching
it. The descriptor holds two things:

- `tables`, every table the port holds, as a `cok_ds_table`: its DS
  offset, its length, the stride between its bytes (1, or 16 for a column
  of the spell table) and the bytes. Those with a stride of 1 come first,
  in order of their offsets (the lookup's search relies on it), then the
  columns. A table's copy may run on into the next one's (the steps into
  the terrain, the terrain into the footprints, the cone's sides into the
  battle's effects); where two hold the same byte they must agree.
- `table[COK_DATA_...]`, a `cok_data_table` for each table the code reads,
  named for what it is (`COK_DATA_SPELLS`, `COK_DATA_THAC0`,
  `COK_DATA_SAVING_THROWS`, ...): `base`, the DS offset the original's
  arithmetic adds each index times its stride to, as its code has it (the
  thief skills' `DS:3908` for a table that starts at `DS:3911`, its level
  and skill counted from 1); one to three indices, outermost first, each
  with its first, its count and its stride (the innermost's the element's
  width); and `from` and `size`, the table's own bytes, by default its
  elements', given where the port held more than the elements (the ways
  of moving, turning undead, the cone's sides: what a read the code
  bounds by its copy may reach). Width 0: the game has no such table.

A lookup takes the segment (`const cok_data_segment *D =
cok_game_rules(game)->data;`, or `rules->data`) and the table's name, and
asserts that the game has the table and that it is given the table's
number of indices:

| Was | Is |
| --- | --- |
| `cok_ds_byte((uint16_t)(0x31b3u + 16u * spell + column), &b)` | `cok_data_byte(D, (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, column), &b)` |
| `ds_byte(l, 0x4056 + class * 60 + level * 5 + type, ...)` (party.c) | `ds_byte(l, cok_data_offset3(l->D, COK_DATA_SAVING_THROWS, class, level, type), ...)` |
| `ds((uint16_t)(0x3903 + i))` (train.c, modify.c) | `ds(D, (uint16_t)cok_data_offset(D, COK_DATA_TOP_LEVELS, i))` |
| `cok_ds_string((uint16_t)(0x1330 + 13 * status), s)` | `cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_STATUS_NAMES, status), s)` |
| `at = 13 * kind + level`, bounded by the copy, then `turning[...]` | `at = cok_data_offset2(D, COK_DATA_TURNING, kind, level)`, `cok_data_holds(D, COK_DATA_TURNING, at)`, `cok_data_held(D, at)` |
| `formations[f][row][0]`, `quadrant_dir[q]` | `cok_data_s8_at3(D, COK_DATA_FORMATIONS, f, row, 0)`, `cok_data_u8_at(D, COK_DATA_QUADRANT_DIRECTIONS, q)` |
| `template_word(t)`: `word(template_table + 2 * t)` | `cok_data_u16_at(D, COK_DATA_TREASURE_TEMPLATES, t)` |
| `k < sizeof helpless`, `size >= COK_COMBAT_SIZES` | `k < cok_data_count(D, COK_DATA_HELPLESS, 0)`, `size >= cok_data_count(D, COK_DATA_FOOTPRINTS, 0)` |
| `cok_combat_terrain[v].cost` | `cok_combat_terrain(rules, v).cost` |

`cok_data_offset` (and `2`, `3`) is the original's arithmetic, unbounded,
as a `long`: the site converts and bounds it as it did before (a
`uint16_t`, as the original's 16-bit arithmetic wraps; or a check that it
lies in 0-0xffff, failing past the initialized data, as party.c's
`ds_byte`). `cok_data_byte` then reads the byte at that DS offset from
whichever of the game's tables holds it, so that an index past one table
reads the next as the original does (a level 13 cleric's THAC0 is the
next class's level 0; a spell id up to 0xff reads on past the spell
table), and returns false past the data the port holds; `cok_data_holds`
says whether an offset is one of a table's own bytes, for the reads the
port bounded by its copy of a table (the ways of moving, turning undead,
the names); `cok_data_held` reads a byte that must be held (asserted).
`cok_data_u8_at`, `s8`, `u16` (and `_at2`, `_at3`) are for the reads the
code kept within a table, as a C array's bound did: each index asserted to
be one of the table's, the width the element's. An index from data (a
spell id, a record's byte, the ECL's) goes through `cok_data_offset` and
the site's existing bound, never a checked accessor, as with the character
record's fields. `cok_data_string` reads a name, a Pascal string within
the name strings (`COK_DATA_NAMES`, `DS:0f5a`-`1dd3`), false where it would
start or end outside them or hold a NUL; `cok_data_first` and
`cok_data_count` give an index's range for a loop or a bound.

Routines that read a table and had neither a game nor the rules take the
rules: `cok_spell_class`, `level`, `effect(rules, id)`,
`cok_treasure_item(rules, seed, ...)`, `cok_train_experience(rules, row,
...)`; after the record `cok_train_constitution`, `cok_modify_least`,
`cok_modify_most`, `cok_camp_preparation`, `cok_combat_helpless(c,
rules)`; after the combat (whose map the footprints and the terrain lay
out) `cok_combat_occupy`, `probe`, `visible`, `sight`, `list`, `distance`,
`dungeon`, `wilderness`; `cok_combat_line_step(line, rules)` (the line's
directions); first `cok_combat_footprint(rules, ...)`; and
`cok_cast_step_byte(D, at)` the segment. `cok_combat_terrain` is a
function of the rules and a terrain value, which must be one the table
holds; the bounds that were `COK_COMBAT_TERRAINS` and `COK_COMBAT_SIZES`
are the tables' counts.

The steps by direction (`DS:1ed6`, `DS:1edf`), the compass, the same in
every game, are the code's own constants, `cok_step_x` and `cok_step_y`
(`src/step.h`), which test_data checks against `COK_DATA_STEP_X` and
`STEP_Y`. The descriptor lists those bytes (`DS:1ed6`-`1ee7`) only for the
reads that run on past direction 8 into the terrain table (a cone's, a
cloud's, the overland map's), which go through `cok_cast_step_byte`. A
footprint's cells are counted by the table (`COK_DATA_FOOTPRINTS`' second
index); the arrays that hold one footprint's cells are
`COK_COMBAT_FOOTPRINT_CELLS` long, which test_data checks the game's
count against (4 in both games). Not in the descriptor: the tables that
cite the data segment but the port never held as its copy of it: the
screen's frames
and colours, CGA's and the mouse's tables, the keypad's and the view's
keys, the 3D view's places and tile sets, the start menu's items, the
treasure's coin names and rates, the icon sizes' letters, the sound's
effects. A later part moves those a game changes. The spells' names and
the spell lists' headings are the `spells` section's (see The spells and
effects); the temple's diseases (`DS:01c6`) are a table here,
`COK_DATA_DISEASES`.

To add a table: its bytes in `src/data_champions.c` as the original
holds them, with a comment citing its DS range and what it is, declared in
`src/data_champions.h`; listed in `champions_tables` (`src/rules.c`) in
order of its offset; and, if the code reads it, named in `cok_data`
(`src/data.h`) with a comment, and placed in `champions_data` with the
macro of its shape (`LIST(base, width, first, count)`, `TABLE` for two
indices, `TABLE3` for three, `TABLE_IN` with its own bytes given) and a
comment citing the routines that read it. `tests/test_data.c` checks every
listed byte against `build/START_FULL.EXE` (`make test` builds it with
`make merged`), that overlapping tables agree, that every named table's
elements lie in its own bytes and are held; add the expression the code
had to `test_arithmetic`, over the whole range of its indices, the ones
past the table too. To add a lookup: replace the DS arithmetic by
`cok_data_offset` of the table's name (or a checked accessor for an index
the code keeps in range), keeping the site's own conversion and bound and
its messages, which print the DS offset as before.

A game's shapes differ (Death Knights' levels go to 18: THAC0 rows of 19,
saving throws 90 bytes a class, spells a day and experience rows of 204
with a spell level's 8 bytes apart, three spell lists of 8 levels,
turning undead's rows of 11, every base moved); its descriptor gives its
own bases, strides, counts and bytes, and the code that reads through the
names follows. A game's name strings may lie in more than one run: Death
Knights' item words lie apart from its other names, and
`cok_data_string` reads a name within either (`COK_DATA_NAMES`, or
`COK_DATA_ITEM_WORDS` where they are not within it).

### The spells and effects

What is keyed by spell id and effect id is the `spells` and `effects`
sections' (`src/rules.h`), so that a game brings its own spells and its
own handler of each id. Shared code never compares an id with a number:

- Ids the code names are named (`src/spells.h`: `cok_named_spell`,
  `cok_named_effect`, each with Champions' id and the routine it is
  from), and a game's `spells.ids` and `effects.ids` give each its id, 0
  for one it has not. `cok_spell_id(rules, COK_SPELL_KNOCK)` and
  `cok_effect_id(rules, COK_EFFECT_INVISIBLE)` (in the game's code
  `cok_game_spell(game, ...)`, `cok_game_effect(game, ...)`; in `cast.c`
  and `effect.c` the macros `SPELL(KNOCK)` and `EFFECT(INVISIBLE)`) assert
  that the game has it, as a field or a table a game has not; a test of
  an id goes through `cok_spell_is` and `cok_effect_is` (`IS_SPELL`,
  `IS_EFFECT`), false for a name the game has not, so that a handler
  shared by several ids (the poisons' `h_poison`, the breaths' dispatch)
  does not stop at a sibling the game lacks. The ids the messages cite
  stay Champions', with the handlers' addresses. A test that a character
  has a named effect goes through `cok_character_find_named` (`party.h`),
  NULL for a name the game has not. A value of a record's or an item's
  byte that both games test alike is not an id: it stays a constant, or a
  descriptor value where it is the game's (`spells.weapon_unscored`,
  0x53, the weapon score's test of an item's +0x3d, `3afb:14ca`).
- Each id's handler is a table entry: `spells.cast[id]` (`cok_rules_cast`)
  and `effects.handlers[id]` (`cok_rules_effect`) hold the port's handler,
  a `cok_cast_handler` or `cok_effect_handler` (one each of `cast.c`'s and
  `effect.c`'s handlers, by its function's name: `COK_CAST_PROTECTED` is
  `protected`, `COK_FX_HELD` is `h_held`), and the original routine's
  offset; `cast.c` and `effect.c` hold a function by kind and dispatch
  through the game's table, as the original through `DS:6e3a` and
  `DS:6b94`. An entry with `unported` set (the game's routine, "dkk
  5dc5:1daa") stops the run where the port would run it: casting stops
  with `COK_ECL_UNPORTED`, an effect's handler fails.
- What else differs by id is data: the durations not taken from the
  spell table, each a rule (`cok_duration_rule`) and its minutes; byte
  7's values (caster, one picked, the party); the ids each event runs,
  the ids the party shares, those magic resistance leaves pending;
  Display's ids and texts; the spell lists' sizes, the granted powers'
  ids, an item power's spell; the counts that bound the tables.
- A handler that both games share but for a constant takes it from its
  entry (`cok_rules_cast`'s `value`, `count`, `sides`, `type`: Death
  Knights' Enlarge, Lightning Bolt, and Flame Strike and Ice Storm by
  `COK_CAST_DICE`; its band B spells, see Death Knights' casting), and
  its words where they are not Champions' from the entry's `text` (Death
  Knights' "is fire-resistant"; `COK_CAST_SAYING` and `COK_CAST_MISSILES`
  say only that); where the cast's own rules differ (its words, its
  targets, the damage's message, magic resistance, the cone's range,
  Dispel Magic's clouds), the casting section holds them (see Death
  Knights' casting). Loops over spell levels and list entries are bounded
  by the tables' shapes (`COK_DATA_SPELL_LISTS`: Champions' 4 levels,
  Death Knights' 8), and each game's list sizes are at most
  `COK_SPELL_LIST_MOST`, `magic.c`'s arrays.

To add a spell or effect handler: its function in `cast.c` or `effect.c`,
its kind in `src/spells.h` and the function in the file's table by kind,
and the ids that run it in each game's table. To name an id: add it to
the enum with Champions' id and routine in a comment, give it in each
game's `ids` (0 where the game has not it), and read it through the
lookups. `tests/test_magic_rules.c` checks each game's names and headings
byte for byte, its handler tables against the stores of the routines
that fill them at startup (through the thunks), its event lists against
the dispatch's calls, the shared ids against the set the dispatch tests,
byte 7's values against the target routine's compares, the weapon
score's item byte, the learning list's levels and the moons against
their routines, in both executables; that ids of
one routine have one handler; and that every handler kind is one of
Champions'.

Death Knights' tables were made from its executable and Champions': its
handler tables (`DS:c150` by `5dc5:5622`, `DS:be7e` by `5dc5:5622` and
`504d:2948`) and each handler's code compared with Champions' instruction
by instruction (string arguments by their text, calls through the
routines they map to, thunks followed), an id taking Champions' handler
where the routine is the same but for data segment addresses and the
record's offsets (by the import's block moves, `46b2:10d7`), else
unported. Its named ids are Champions' where its handler of the id is
Champions' routine, or where a shared handler uses the id unchanged;
others are 0.

## Playing

`build/cok [options]` plays the game in a window, as `eclplay
--start-menu` plays it without one: main's loop (`1000:0134`, see The
start menu), the title, its menu and the demonstration, the start menu,
then the adventure, camp, shops and combat, game after game, the
player's turns in a battle reading the keyboard (`3995:0573`,
`COK_COMBAT_PLAYER`). The game's code calls the keyboard and the delay
hook and waits in them; they handle the window's events, show the screen
when it has changed and sleep until a key comes, the delay is over, a
timed menu runs out or the small picture's next frame is due.

| Option | |
| --- | --- |
| `--game NAME` | the game to play (see Games): `champions`, the default, or `death-knights`, which plays from its title through the start menu (its saved games, the vault's files, Champions' parties) into the game, up to what is not ported yet (its first fight, creating a character: see Games' Death Knights), where the run stops with the reason on the screen; any other name is an error that lists the games (status 2) |
| `--assets DIR` | the game's DAX files (default `COKAssets`, the game's: see Games) |
| `--saves DIR` | the roster and saved games (default `saves`, created if missing; ignored by git); never `SAVE/` itself: to play the original's games, copy them into it (`mkdir -p saves && cp SAVE/* saves/`) and pick Load Saved Game; and the port's `KRYNN.CFG`, read before the assets' and written by the setup (see Startup and the setup) |
| `--import DIR` | where a game that imports another's characters finds them: Death Knights' Champions roster (`*.WHO`) and saved games for Add Character to Party and Load Saved Game, Champions (the original's `DKK.CFG` fifth line, a DOS path, `DS:99a4`); default `--saves`, so that one saves directory serves both games |
| `--config FILE` | the `KRYNN.CFG` to read, or, if it is missing, for the setup to write |
| `--scale N` | the window's size, N (1-16) times the screen (default 3: 960×720) |
| `--aspect 4:3`, `--aspect 1:1` | 4:3, the default, shows the 200 lines as 240, as a 4:3 monitor showed the 320×200 mode: the game was drawn for it (the moons and the portraits are round so); 1:1 keeps the pixels square |
| `--fullscreen` | start full screen |
| `--screenshots DIR` | where F12 saves the screen (default `screenshots`, created when needed, ignored by git) |
| `--seed N` | start Turbo Pascal's `Random` from N; by default the clock seeds it, as startup's `Randomize` (`1a46:1200` from `3e99:005b`) does |
| `--woof`, `--helm` | start as `START Woof` (no title, and the debug keys Ctrl-D, Ctrl-Z and Ctrl-C: see Debug mode) or with `Helm` (`Area` anywhere, the Gods' cheat Alt-X, free training) |
| `--log` | print the game's text, menus, choices, sounds and what is not ported to standard error, as `eclplay` prints them; the window's size and where the screen is shown in it; keys typed ahead that a flush drops (`flush:`); startup's text screen, a row at a time (`crt:`); the display mode (`display:`); the alternate input found (`input:`); the audio driver and the sound device (`sound:`); and a check that the window keeps up: `stale:` if, through the game's delays, the window kept an older screen than the game's for more than 160 ms |
| `--log-times` | with `--log`, start each line with SDL's clock (`SDL_GetTicks64`, the milliseconds the delays and waits go by), and log each frame the window presents (`present: N`, numbered as SDL's dummy driver numbers the frames it saves with `SDL_VIDEO_DUMMY_SAVE_FRAMES`) and each frame of the small picture's animation as it is drawn (`frame: N`, 0-based): what `tests/test_cok.py` measures the pacing by in real time |
| `--keys KEYS` | type KEYS first, in `eclplay`'s escapes (see Playing scripts), each turned into the key that gives it and sent through SDL's key events as the game asks for a key (`\k` as it polls, `\t` the timed menu running out, `\[KEYS\]` typed ahead and `\wN.` as in Playing scripts; besides, `\pN.` pauses N hundredths of a second before the next key is typed as the game reads it, and `\#85` and `\#86`, codes the game never gets, press F11 and F12; `\mX,Y,B.` and `\jX,Y,B.` click the mouse and move the stick through SDL's events, see The mouse and the joystick; a key repeated in a group typed ahead is held, its repeats sent as a held key's are; each a real keystroke, so that a flush after its 0 is read loses its scan code, which `eclplay` keeps for keys not typed ahead); the run then ends as `eclplay`'s does, printing `(out of keys ...)`, unless `--continue` hands over to the keyboard |
| `--no-delay` | do not wait where the game waits (for tests); a rest keeps the front end's pace (see below); the sound then goes by the game's clock (see Sound) |
| `--display ega`, `--display tandy`, `--display cga` | the display mode in place of the one `KRYNN.CFG` names (its first line: `E`, `T`, anything else CGA, see Display modes): EGA and the Tandy's look the same; CGA shows four colours |
| `--sound tandy`, `--sound speaker`, `--sound off` | the sound device in place of the one `KRYNN.CFG` names (its second line: `T`, `P`, `S`; `C`, `R` and `A` play the effects on the speaker and no music, see Sound): the Tandy's three voices and noise, the PC speaker, or none |
| `--no-sound` | open no audio device |
| `--volume N` | 0-100 (default 100) |
| `--sound-clock real`, `--sound-clock game` | what times the sound drivers: the audio device as it plays (the default; with `--no-delay`, `game`), or the game's delays and `\pN.` pauses, as `eclplay --wav` renders them |
| `--input mouse`, `--input joystick`, `--input none` | the alternate input in place of the one `KRYNN.CFG` names (its third line: `M`, `J`, `N`; see The mouse and the joystick): `cok` always has SDL's mouse, and has a joystick when SDL finds one; `--input joystick` over a file whose third line is not `J` (which gives no thresholds) calibrates the stick at startup, as the setup's `[1]` does (`177c:029d`, on the text screen before the note to Tandy users), for the run only: the file is not written |
| `--shots DIR` | save `DIR/NNN.bmp` each time the game waits for a key and when a battle is set up, as `eclplay --shots` does, but read back from the window: each pixel of the screen where its area's middle is shown must be one of the screen's colours (16, or CGA's 4), and at a whole multiple of 320×200 its whole block; in 4:3 it needs `--scale 2` or more, as at 320×240 the smoothing blends every row |

The window can be resized; the screen keeps its aspect, letterboxed. It is
scaled up by whole numbers with square pixels to at least the size shown,
then smoothly to that size, so the pixels stay even at any size and are
sharp at whole multiples. Colours are the 16 of the Tandy and EGA modes,
or in CGA mode the four of its palette, as the BMP export gives them
(`cok_image_rgb`).

Keys, as Turbo Pascal's `Crt.ReadKey` reads them through INT 16h function
0 (`src/keymap.h`; `tests/test_keymap.c` checks each, that every code
the port compares a key against can be typed, and NumLock and Alt's
keypad numbers as the BIOS keeps them, `cok_keymap_event`):

| Key | The game reads | For |
| --- | --- | --- |
| letters | `a`-`z`, with Shift or CapsLock `A`-`Z` | menu hotkeys (either case), names and other text |
| digits, punctuation | ASCII; with Shift as on a US keyboard | text input (0x20-0x7a), amounts, digit hotkeys; 1-9 as the keypad's in menus that take directions |
| Space | 32 | menus that take it; in combat on Auto, the player back in charge |
| Enter, keypad Enter | 13 (with Ctrl 10) | the selected item; the end of a line |
| Escape | 27 | Exit, cancel |
| Backspace | 8 (with Ctrl 127) | erase in text input |
| Tab, Shift-Tab | 9; 0, 0x0f | nothing |
| arrows | 0, then 0x48 up, 0x50 down, 0x4b left, 0x4d right | menu items, picking a character, lists, Move in 3D (step, turn), facing on the overland map, combat directions, Modify's fields, Rest and Speed |
| Home, PgUp, End, PgDn | 0, then 0x47, 0x49, 0x4f, 0x51 | the diagonals in combat and on the overland map; PgUp and PgDn page lists |
| Ins, Del | 0, then 0x52, 0x53 | Del deletes in Modify's name |
| keypad digits and `.` | `0`-`9`, `.`: NumLock is on, as startup sets it (`3e99:005b`, `0040:0017` bit 5), whatever the host's; with it off the cursor key's code (none for 5) | directions (`DS:1fdf`): 8 up, 2 down, 4 left, 6 right, 7 9 1 3 the diagonals, 5 the centre in combat's aim |
| Shift and a keypad key | the cursor key's code (none for 5); with NumLock off the digit | as the arrows |
| NumLock | nothing: NumLock off and on again, as the BIOS's keyboard handler (INT 9) turns it, the host's left as it is; with Ctrl it is Pause, not ported, and turns nothing | the keypad's digits or cursor keys |
| Alt and keypad digits | on Alt's release, the character code they make, as a byte (DOS's `Alt`+number; none for 0), Ctrl held or not; Shift, Ctrl, the lock keys and space (which types a space) keep the number, another key with Alt starts it again | text input, as any key typing it |
| keypad `/ * - +` | ASCII | nothing |
| Ctrl and Left, Right, Home, End, PgUp, PgDn | 0, then 0x73, 0x74, 0x77, 0x75, 0x84, 0x76 | nothing |
| F1-F10 | 0, then 0x3b-0x44; with Shift 0x54-0x5d, Ctrl 0x5e-0x67, Alt 0x68-0x71 | F10 picks Demo on the title's menu; Ctrl-F8 camps on the overland map; menus that do not tell scan codes from letters take F7-F10 (0x41-0x44) as A-D, Home, Up and PgUp as G, H and I, and so on (saved games A-J, Yes and No) |
| Alt and a letter | 0, then the letter's scan code | in combat Alt-Q and Alt-A Auto, Alt-M Magic on and off, Alt-X the Gods (with `--helm`), Alt-R nothing |
| Alt and a digit, `-` or `=` | 0, then 0x78-0x83 | nothing |
| Ctrl and a letter | 1-26; Ctrl `[ \ ] 6 -` 27-31; Ctrl-2 0, 0x03 | Ctrl-S the sound off and on again (`1614:025b`, see Sound); with `--woof`, Ctrl-D the debug mode on and off, Ctrl-Z the party's effects to `DEBUG.TXT`, Ctrl-C (and Ctrl-2, whose scan code is 3) quit to DOS (see Debug mode); in the setup's path line, Crt's editor's: Ctrl-S erases a character, Ctrl-A the line, Ctrl-D and Ctrl-F recall (`1962:0394`, see Startup and the setup), and the debug keys and Ctrl-S's sound do nothing there |
| F11 or Alt-Enter | nothing (function 0 drops them) | full screen on and off |
| F12 | nothing | save the screen as shown (the mouse's cursor too), 320×200 (startup's text screen 640×200), as `cokNNN.bmp` in `screenshots/` or `--screenshots DIR` |

With the mouse on (`KRYNN.CFG`'s `M`, or `--input mouse`), its left button
is Enter and its right Escape where the game reads a key, and a click goes
where it lands in the menus (see The mouse and the joystick); the game draws
its arrow in place of the window's pointer. With a joystick, a push is a
keypad digit, its first button Enter, its second Escape.

Keys the game cannot read give nothing: those only an enhanced keyboard's
function 0x10 returns (Ctrl or Alt with the arrows, Alt-Enter, F11, F12),
Ctrl with Alt (but for the keypad's digits), keys with the GUI (Windows)
key or AltGr, and non-ASCII keys. Letters and unshifted punctuation follow
the host's layout; shifted punctuation is the US layout's. Keys typed
ahead wait, up to 15 as in the BIOS's buffer
(more are lost), and are thrown away where the original flushes the
keyboard (`1614:045c`: `ReadKey` while `KeyPressed`, the scan code owed
after a 0 too), through the adventure's `flush` hook. Its 21 calls in
`START_FULL.EXE`, each ported but the last:

| Call | In | When |
| --- | --- | --- |
| `67b5:0c28` | `67b5:03e2` | every menu, once it has its key or has timed out |
| `1521:06ea` | `1521:04ac` | paging running text, after its key |
| `2f55:0006`, `005f` | `2f55:0000` | before and after each of the title's waits |
| `1000:0313` | main's loop | after the title that follows a demonstration, before its menu |
| `127f:029a` | `127f:0111` | an image loaded from a DAX file (title pictures, portraits' pieces, combat icons and tiles, big pictures), not when the record is not there |
| `6961:0514` | `6961:00e4` | the small picture or a sprite loaded, not one already held |
| `6961:06b2` | `6961:05b9` | a portrait shown, its pieces held or not |
| `6d21:04a5` | `6d21:01d0` | a combatant's icons loaded |
| `6d21:00ec` | `6d21:002c` | combat's terrain tiles loaded |
| `4b6d:0a66` | `4b6d:0817` | a character's icon composed |
| `4b6d:15ce` | `4b6d:11e5` | a character's files read (a saved game's party, the roster's Add) |
| `4b6d:18ec` | `4b6d:161b` | a monster's records read (`LOAD MONSTER`, `ADD NPC`) |
| `4b6d:20ef` | `4b6d:1b34` | a saved game loaded |
| `58e7:0229`, `0ab6` | `58e7:01f2`, `0a81` | "Overloaded" taking money from the pool, after its pause |
| `58e7:0506` | `58e7:044b` | "Overloaded" trading money, after its pause |
| `3995:145f` | `3995:121c` | combat's Auto, before its 200 ms |
| `432f:34ae` | `432f:33ce` | each step of combat's aiming cursor |
| `3afb:12eb` | `3afb:1200` | the computer's turn's poll of the keys, as it returns, with no key waiting too |
| `6e22:017d` | `6e22:0050` | after "Bad symbol number..." and quitting to DOS (`1614:0000`): never reached, as the run has ended |

The delays are the game's: each character of running text waits speed × 3
ms and `DELAY` and the notices speed × 100 ms, the speed being camp's Alter
Speed (`DS:4b38`, 4 at startup, 9 in the demonstration); the title waits
5, 10, 10 and 10 seconds, polling for a key each hundredth; the title's
menu runs out after 30 seconds (10 after a demonstration) and plays the
demonstration, from when it opens, arrows not restarting it (`DS:6e11`; the
original never runs out past midnight, when `GetTime` wraps; the front end
does). While a menu that the original asks to (`67b5:03e2`'s flag)
waits, the small picture animates as there: each frame stays its delay ×
10 hundredths of a second (the pictures' delays count tenths there), then
the next is drawn at cell 3, 3, the first after the last, for pictures of
two frames or more loaded since the slot was freed. The menus that ask are
the camp's (Save, View, Magic, Rest, Alter and Alter's, Fix), treasure's,
Pick Character in camp and treasure (modes 2 and 6), Save Which Game in
camp, and the scripts' `HORIZONTAL MENU` and `ENCOUNTER MENU` over a
picture that replaced the view (`DS:884a`, `884b`; for `ENCOUNTER MENU` in
a 3D area and not PIC 9). The frame advances in the game's state
(`DS:6da3`), as in the original, so a script's `CALL [6803]` goes on from
it; the frame's time starts again each time a menu reads a key (the
original's at `67b5:03e2`'s start and after each frame), so arrow keys
delay the next frame a little. Nothing else animates while the game waits.
The screen is shown when it has changed, at most 60 times a second while
the game runs and at once when it waits for a key, which it does asleep in
`SDL_WaitEventTimeout`: `tests/test_cok.py` measures the CPU time over two
seconds at the title's menu and in the title's waits, which must be under
a tenth of a core (it is under 1% here). A present the rate holds back is
owed, and every wait wakes for it, so the window never keeps an old screen
through a delay or while the game waits for a key; nor after it is
exposed or resized. Frames whose delay is 0 (every sprite's groups have
it), which the original's loop redraws as fast as it runs, stay a
hundredth.

Resting (`57e4`'s loop, from camp's Rest) polls the keyboard once each
five minutes rested and waits for nothing, so the original rested at its
machine's pace, showing the time left every five steps (`57e4:0764`); a
key stops it with `Stop Resting?`. The front end gives each step a
hundredth, so that eight hours take about a second, the count shows and a
key can stop it.

Closing the window (or `SIGINT`, `SIGTERM`, even where a shell started it
in the background with `SIGINT` ignored) ends the run at once: the key
read returns the end of input, or the delay sets the abort flag the end of
input sets, and the game returns as `eclplay`'s does when its keys run out,
writing nothing more; a save under way finishes first, as saving reads no
key. `cok` then exits with status 0. Quitting to DOS (the start menu's
Exit to DOS, Yes to `Quit TO DOS` after camp's Save, or Ctrl-C with
`--woof`) ends it too, printing `(quit to DOS)`.

Where the port stops (`COK_ECL_UNDEFINED`, `COK_ECL_EFFECT_FAILED`, an
unported routine, a file missing or unreadable), `cok` prints `stopped in
block N at ADDR: ` and the reason to standard error, writes the reason in
light red on the screen's last rows over black, with `Press a key to quit.`
in white (in CGA mode magenta and white), waits for a key or the window's
close, and exits with status 1 (2 for bad options or a `--keys` code no key
gives). Without the font, a message box says it.

Not done: the music of CMS, the MT-32 and AdLib (see Sound).
`tests/test_cok.py` plays, with `--input none` (the keyboard alone, as
`eclplay` without `--mouse` has it), the title and the demonstration, and a
new game from the title (creating a character, the overland map, two fights
with the player's keys, camps in an inn and in 3D, walking in Throtl,
saving the game) with the same keys as `eclplay --start-menu --shots`,
typed through SDL's events, and checks that every frame the window shows,
read back at 640×400, at 960×720 and full screen letterboxed at 1024×640,
is `eclplay`'s byte for byte, and the saved game written the same; keys
typed ahead dropped by a menu's flush and kept by the name's line, and
pressed in the middle of the title's waits and of a rest, as `eclplay` has
them; from the frames SDL's dummy driver saves as they are presented
(`SDL_VIDEO_DUMMY_SAVE_FRAMES`), that the window shows the title while it
waits and the title's menu, once, as `eclplay` draws it, and the
hobgoblin's picture animating in Throtl, each frame for its delay; the
title's menu timing out 10 seconds after it opens following a
demonstration, an arrow half way not starting it again; a rest taking its
pace with the time left shown; closing in the middle of the demonstration's
delays ending at once; F11's full screen shown at once and F12's
screenshot; NumLock, the keypad and Alt's keypad numbers pressed as keys
(`\n`, `\dC`, `\aN.`) giving `eclplay`'s frames; SIGINT with SIGINT
ignored; and, with real delays through the
first fight, that the window never kept an old screen (`--log`'s check) nor
showed more than about 60 frames a second; and the sound through SDL's disk
writer: by the game's clock (`--no-delay`) the same samples as `eclplay
--wav` for the same keys and seed, after the silence the device starts
with; in real time the title's music playing on through the title's wait,
and two steps' sounds each playing out whole while the game waits for a
key, and without a device, with `--no-sound` or without `SOUND.DAX` the
game playing on,
saying why; a copy of the assets whose `KRYNN.CFG` says `P` playing the
speaker; and, with a copy of the assets without `KRYNN.CFG`, the setup's
text screens at 640×200 frame for frame as `eclplay` draws them, the file
it writes and the game in the mode it chose, the next start reading it, and
keys that end in the setup (the game then with the mouse the setup chose,
as `eclplay --mouse` plays it); that the window shows startup's note to
Tandy users before the title, and the music in real time after it; and the
mouse's clicks and the joystick (see The mouse and the joystick).

The checks in real time hold on a busy machine (the tests run while
other builds and tests load every core) and still fail on a change to the
pacing they check. They time the game by SDL's clock as `cok --log
--log-times` logs it (its lines, the frames it presents and the picture's
frames as it draws them), not by the test's clock, which is late by however
long the test takes to read the lines, nor by when the frames' files were
written. A pace must never be shorter than the original's, since every
wait is measured on that clock; it may be longer by the machine's lateness
in waking, which is bounded only where the port itself could be too slow,
and then in the least late of the frames or steps that repeat: the
picture's frames each under 20 ms over their delays in one of three rounds
and presented within 16 ms of being drawn, the rest's count shown under
56 ms apart (five steps of 10 ms) at least once, the note to Tandy users
under 2.5 seconds, the menu's timeout under 10.5 seconds after it opens and
the arrow's redraw under 5.5; and in the demonstration's busiest second
from 55 to 66 frames presented (63 here, idle or busy). Waits for something
to happen are bounded by the wall clock only to end a failing run. SDL's
dummy driver writes each frame inside the present, so the tests have it
write them in `/dev/shm` where there is one with 32 MB free (else the temp
folder): on a disk slow to write (here a FUSE overlay, starved of CPU on a
busy machine) a frame took seconds, and the game was late by that. The
driver leaves a frame short without a word when its disk is full, which
the tests report as such (its size against its header's). Their temp
folders are named `test_cok-*`. The sound's checks count the device's
samples, which SDL's disk writer produces at its own thread's pace, slower
on a busy machine (0.8 seconds of them in 3 once): the music is checked
over 2.5 seconds of them, and the steps' sounds, each after a pause, once
0.4 seconds of them follow the second step's `overland:` line, however
long they take to come; the pause between the two sounds only for being
several times a sound's length. Halving or doubling the picture's delays
or the rest's step, each of its frames 40 ms longer, steps of 12 ms,
presenting a frame 100 ms late, every delay twice its time, timing the
menu out at 5, 15 (again from the arrow) or 20 seconds, presenting up to
120 or only 30 frames a second, not presenting in a delay's loop,
presenting again at each wake, the note to Tandy users for 1 second, the
device silent through a delay or a key's wait, or a delay not seeing the
window close: each fails its test.

`tests/test_hooks.c` checks each flush, which menus animate, the sound
driver's calls, and `Loading...Please Wait` and `PIC not found` against
what the original's loaders did in an 8086 emulator
(`tests/test_loading_data.h`, see Playing scripts).

## Startup and the setup

Startup (`11c7:031a`) works in the text mode DOS left, before it sets the
graphics mode, through Turbo Pascal 6's `Crt` unit (`1962`) and `System`'s
text files: Ctrl-Break is turned off (`Crt.CheckBreak`, `DS:8a44`), it
waits for `GAME.OVR` (`169c:01c8`: `Please insert overlay disk.` and a key
while it is missing) and looks for `krynn.cfg` in the current directory
(`169c:04c1`, `FindFirst` with no attributes: a hidden one, or a directory
of that name, counts as missing under MS-DOS; DOSBox-X's own DOS returns
hidden files to such a search, so there a hidden one is found). Without
it the setup runs: `Rewrite`
makes the file first, and if that fails it says `Sorry, disk A can't be
write-protected.`, a blank line and `If these are your masters, use
DISKCOPY to make duplicates.` and halts with status 1. Then four screens,
each after `ClrScr`, in `TextAttr` (the colour under DOS's cursor, normally
light grey on black; nothing changes it):

| Screen | Asks | Writes |
| --- | --- | --- |
| `GRAPHICS ADAPTER TYPE:  [1] CGA  [2] EGA  [3] Tandy  ` | a digit 1-3 | `C`, `E`, `T` |
| `SOUND TYPE:  [1] C/MS` and five more lines to `[6] Silent` | 1-6 | `C`, `R`, `A`, `T`, `P`, `S` |
| `ALTERNATE INPUT TYPE:  [1] JOYSTICK  [2] MOUSE  [3] NEITHER  ` | 1-3 | `J`, `M`, `N` |
| `PATH TO SAVE DATA (DEFAULT - C:\COK\SAVE):  ` | a line | the path, then `F` and 14 numbers |

A digit (`11c7:0020`) is read with `ReadKey` until it is one in range,
then echoed; other keys do nothing, but an extended key's scan code is read
as a character too, so Alt-N (0x31) answers 1 and Alt-M (0x32) 2. The path
is read with `ReadLn` through `Crt`'s line editor (`1962:0394`): up to 126
characters, echoed, wrapping at the 80th column; Backspace or Ctrl-S erases
one, Escape or Ctrl-A all; Ctrl-D brings back one character the buffer
still holds past the cursor (one typed and erased), Ctrl-F all of them;
Enter ends it; other control keys and extended keys do nothing (Ctrl-Z
would end it with `CheckEOF`, which is off). The string keeps the first 80.
Its first character upper-cased, it is taken if it has at least 4
characters, the second a colon and the first a drive from A to P, and not
the floppy (A or B) the game runs on; else `USING DEFAULT PATH: ` and the
default, for 2 seconds. Then every letter is upper-cased and a `\` added
(unless 80 characters already fill the string). The default is the current
directory (`FExpand('*.exe')`, cut to FSplit's 67 characters) and `SAVE`,
or on drive A or B the other drive's root (`B:\SAVE`, `A:\SAVE`). Joystick
calibration follows (`177c:029d`, see The mouse and the joystick): with a
stick on port 0x201 and `[1]`, two screens (`Push JOYSTICK #1 to the
extreme upper left position and press BUTTON #1.`, then lower right;
`Couldn't read the joystick.  Aborting calibration.` on failure, the
numbers as far as it got); without one it asks nothing and the numbers are
0. The file, each line ended by CR LF:

| Line | Holds | Read into | In the port |
| --- | --- | --- | --- |
| 1 | the display's letter | `DS:4b76` (see Display modes) | the display |
| 2 | the sound device's | `DS:577a` (see Sound) | the device |
| 3 | `J`, `M` or `N` | the joystick `DS:89ce`, the mouse `DS:8986`; another letter leaves both; then `177c:0000` (`11c7:0b16`) keeps the joystick only if stick 1 can be read, and the game's start resets the mouse (`1743:0000` at `3e99:0085`), on only with a driver | `cok_config.joystick` (with a stick the front end has, `cok_config_parse_stick`), `.mouse` (as the file says; the game's start finds the driver or not, see The mouse and the joystick) |
| 4 | the save path | `DS:5784`, string[255]: the roster and saved games, the save disk's checks (`4b6d:0555`), free space | `cok_config.save_path`; the port saves in `--saves` |
| 5 | `F` | `DS:4b26`, 1 for `F`: nothing reads it | `cok_config.f_line` |
| 6-19 | the stick's 7 X and 7 Y thresholds in pairs, read only with the joystick on after `177c:0000` (`Read` of an integer: blanks skipped, a sign, decimal or `$` hex; error 106 makes every later read do nothing) | `DS:8996`, `89a4` | `cok_config.joystick_x`, `_y`: 0 without a stick |

Then, file or not, `ClrScr`, `TANDY 1000 USERS - Make sure NUMLOCK is on.`
and `Delay(2000)` (a busy loop: keys typed wait in the BIOS's buffer), and
the file is read (`Read` and `ReadLn`, see Sound's Devices) as the mode is
set. No argument or key reaches the setup again; the start menu's
Initialize Mouse/Joystick (`4def:5cc7`) rewrites the file (see The start
menu).

The port does the same (`src/setup.c`, on the text screen of `src/crt.c`),
in `cok` and in `eclplay --start-menu`, before the game opens; `--display`
and `--sound` then override what the file says. What the original showed
depends on the machine's adapter, not on `KRYNN.CFG`, as no mode is set
yet: on a CGA or a Tandy 1000, 80×25 at 640×200 in the ROM's 8×8
characters, the cursor on scan lines 6 and 7; on an EGA 640×350 in 8×14
cells; on a VGA 720×400 in 9×16. The port shows the CGA's and the Tandy's:
640×200 in 8×8 cells, light grey on black, with the cursor's two bottom
lines, as the setup runs before the port knows the display (it is what the
setup asks) and 8×8 is the mode of the two adapters the game draws its
four colours on. Its characters are a PC-style 8×8 font of all 256 of code
page 437, the ROM font's place (the port does not copy IBM's ROM, nor
font8x8, whose ASCII is IBM's CGA font): unscii 2.1's `unscii-8` by
viznut (<http://viznut.fi/unscii/>), in the Public Domain ("Everything
made by me (viznut) is in the Public Domain", its `unscii.txt`), its own
2-pixel-line design after several 8-bit system fonts; each byte as its
Unicode meaning, 0x01-0x1f and 0x7f as the PC's pictures (smileys, suits,
arrows, ⌂). Four that unscii lacks are drawn by the port in its style:
☼ (0x0f), ⌂ (0x7f, from Ctrl-Backspace), ⌐ (0xa9) and ∙ (0xf9); 0x00,
0x20 and 0xff are blank. Lower case shows as typed, as do the symbols,
accented letters, box drawing and blocks. The table is in `src/crt.c`
(`cok_crt_glyph`); `tools/pcfont.py unscii-8.hex` prints it. The game's
own font (`8X8D1.DAX`) draws everything after the setup. `cok` shows it
in the window (F12 saves it, 640×200), `cok --log` and `eclplay` log each
row as the cursor leaves it (`crt: `), and `eclplay --shots` saves it at
each key read. The game's directory is taken as `C:\COK\`, where the
assets' own `KRYNN.CFG` says the game was installed (its save path
`C:\COK\SAVE\`), so the default path is `C:\COK\SAVE`. The sound's time
starts after the note: `eclplay --wav` renders nothing for its 2 seconds.
The note's `Delay(2000)` is the game's first wait, so a key script's group
typed ahead at the start (`\[...\]`) is typed there and waits in the buffer
until the title's flush drops it, as keys typed during the note did, and
`\wN.` counts it as a wait.

Where the port keeps the file: the original reads and writes it in its own
directory, which the port splits into the assets (read only, the original's
files) and `--saves` (the port's own). It reads `--config FILE` if given;
else `KRYNN.CFG` (or `krynn.cfg`) in the saves directory; else the one in
the assets directory, as a copy of the original's directory has it
(`COKAssets/KRYNN.CFG` says `E`, `T`, `M`, `C:\COK\SAVE\`, `F` and 14 zeros).
A file found is a regular file, as `FindFirst` finds one: a directory of
that name counts as missing (the setup then fails to make it, as the
original's `Rewrite` does), and a file the port cannot read stops the run,
never made again. Without any, the setup writes `--config FILE` or
`SAVES/KRYNN.CFG`, which later runs then read; the port never writes into
the assets directory, which may be the repository's `COKAssets/` or a
read-only copy of the game, not even with `--saves` naming it (the setup
then writes nowhere). So the saves directory's copy, once written (by the
setup, or by Initialize Mouse/Joystick, which copies the assets' with its
third line changed), shadows the assets': later edits of the assets' file
are not read, and one saves directory used with several copies of the
assets gives them all its file. `eclplay` without `--saves` or `--config`
writes it nowhere, saying so, and plays on with the answers. Where the
original makes the file as the setup starts and fills it as it ends, so
that a setup cut short leaves an empty `krynn.cfg` (read next time as CGA
with no sound), the port makes it at the start too, to say at once that it
cannot, but removes it again if the run ends in the setup (only a file it
made: what was there before is left). A file it cannot make shows the
original's message on the text screen; then, there as for a file it cannot
read or write, a blank row and `The port stops here: ` with the reason,
also on standard error; the run ends with status 1 (`cok` waits for a key
or the window's close first). A run whose keys end in the setup ends `(out
of keys in the setup)`.

`tests/test_setup.c` replays 96 setups (the digits with noise around them,
extended keys, paths typed with every editing key, long lines, floppies,
long directories, each kind of path refused or taken on each kind of drive,
erasing back over a wrapped line, more keys than the line holds, input
ending early, the file that cannot be made, the file found) against what
the original's `11c7:031a`, `Crt` and `System` left in an 8086 emulator,
with BIOS text output, the keyboard and DOS's files modelled: the text
screen and cursor at each key read and each delay, how it ended and the
file written. 456 such cases agreed; every instruction of `11c7:0020` and
of `11c7:031a` up to the file's reading ran, all of the line editor but
Ctrl-Z with `CheckEOF`, and of `Crt`'s output all but the BIOS path
(`DirectVideo` is on), scrolling (the setup never reaches the last row:
`tests/test_setup.c` checks the port's), the bell and CheckSnow's waits. It
also checks the file's lines read, where the file is found and written, the
rewrite, the timeout key (`\t`) waiting on, the text screen's cells as it
scrolls, its rows logged as the setup writes them, its drawing, and its
font (the table's digest, glyphs of its own for every character but the
blanks, lower case apart from capitals, box drawing to the cell's edges);
`tests/test_eclplay.py` and `tests/test_cok.py` play the setup from the
title's start.

## The mouse and the joystick

`KRYNN.CFG`'s third line picks the alternate input (see Startup and the
setup): `M` the mouse (`DS:8986`), `J` the joystick (`DS:89ce`), `N`
neither. Startup then looks for them: the joystick's unit (`177c`) reads
the stick and turns it off if it does not answer (`177c:0000` at
`11c7:0b16`), taking its fourteen thresholds from the file only then; main's
startup (`3e99:0085`) resets the mouse's driver (INT 33h through the unit
`1743`), off if none answers, and with one sets its cursor (`1743:0107`: the
arrow at `DS:42b4`, one bit a pixel, or in CGA mode `DS:42f4`, two bits a
pixel; the hot spot at its tip), moves the pointer to x 639 and reads it
back to learn whether the driver counts x in pixels (`DS:8984`: a driver of
the 320×200 modes counts 0-639, two to a pixel, which the unit halves and
doubles), shows the cursor, and puts the pointer at the screen's middle.
`src/input.c` ports both units and models the machine under them without
SDL: the mouse driver's pointer (virtual x 0-639, y 0-199), buttons, show
counter and cursor, and the game port's reads of the stick; `src/devices.c`
is the game's side.

| | The original | The port |
| --- | --- | --- |
| a key (`1614:025b`) | with either on and no key waiting, polls them until a key or: the mouse's left button Enter, the right Escape, the last key a click (`DS:8987`) at the pointer (`DS:8989`, `898b`, pixels; `1614:01ad`); the stick pushed its keypad digit (`DS:4358`), else button 1 Enter, button 2 Escape (`1614:00fa`); a key typed meanwhile is read in its place, the click kept; in the demonstration it waits for them rather than give 0 | the front end's read gives `COK_KEY_DEVICE` while the game waits on them (`game->device_wait`) and `cok_adventure_device_ready` says they give a key; `cok_adventure_device_key` makes it. Both buttons (or the middle) leave the key unset: from a menu, or wherever `03c2` polled just before (`1614:0479`, the computer's turn), it holds `BH` of the mouse's state from `03c2`'s frame, 0 (`67b5:05ac`/`05b8`, `3afb:120a`/`1216`, `4def:2a69`/`2a72`): a menu takes it as the extended key's prefix, reads a second key as its scan code (`05c6`, 0 again while both are held) and gives scan code 0 after the release wait; paging (`1521:09ac`), the disk prompts (`169c:0136`, `0201`), `69ea:1109`, `1394` and `6e22:016b`, `022f` throw the key away; only the digit read at `58e7:02f7` gets a byte unknown (SS's low byte, from `0a46:131f`). The port gives 0 everywhere |
| KeyPressed (`1614:03c2`) | a key, a mouse button down (the position kept), the stick's button or push | `cok_adventure_key_pending` |
| a menu (`67b5:03e2`) | Enter from a click goes to its place's cell (x / 8, y / 8): the party list (`065c`, with `DS:8985`, outside the overland map without the list, Move mode and combat: the member on the row is selected, `DS:6096`, or picked, `DS:6e09` with `DS:6e08`, as the special key Up; a row past the party takes the click and gives nothing); in combat during Move (`DS:71a6`) or Manual's cursor (`DS:71a5`), cells 1-21: a step from the mover or the cursor toward the 24-pixel cell clicked (`1743:0167`, below, from `col / 3 + 1`); in Move mode (`DS:8858`): the 3D view's cells 3-13 forward, turn left, right or about (`DS:1ff5`), or the overland map's cells 1-38 by 1-15 a step from the party toward the cell; row 24: the item clicked, as Enter on it, selected; Modify's sheet (`DS:7141`): Home, the field clicked kept (`DS:6e35`); a list's columns (`DS:6e2f`, `6e30`): Enter if the row is at or below one of the list's rows (`DS:6e16`); else nothing, and it reads on. Then it flushes the keyboard and waits for the mouse's buttons to come up after a click, or the joystick's after any key (`67b5:0c2d`) | `menu.c` (row 24, the list's columns), the game's regions in `devices.c` (`cok_pointer.click`), the wait `cok_pointer.release` |
| a list (`67b5:1368`) | a click on a row shown picks it out (a heading too); Select on row 24 takes it | `cok_menu_rows` |
| the special keys (`546c:3334`) | do nothing after a click, which picked already | `cok_adventure_special` |
| Pick Character (`546c:36cc`), the picker (`6346:32c7`) | the party list takes clicks while they ask (`DS:8985` set; the picker's only in the start menu, mode 0); Up from a click takes the member clicked | `cok_start_pick`, `pick_character` |
| `DS:8985` | set at startup with `M`, at a new game (`3e99:0933`), turning the mouse on, leaving the start menu by Begin; cleared entering the start menu | `game->mouse.party_clicks` |
| the cursor (`1743:0030`, `0051`) | the driver draws it in video memory while its show counter is 0: hidden through the title's waits (`2f55:0651` to `006d`) and through running text and its paging (`1521:04ac`); every other routine hides it around its drawing and shows it again before any wait | the counter kept where it matters: startup, the title, running text (`cok_adventure_wrap`), the drift below, Initialize Mouse/Joystick; the cursor composed over the screen as the window or `--shots` shows it (`cok_mouse_draw`): each pixel ANDed with the screen mask's bit and XORed with the cursor mask's, 15 for a bit in the 16-colour modes, CGA's two-bit pairs in CGA mode |
| Initialize Mouse/Joystick (`4def:5cc7`) | see The start menu | `cok_start_mouse` |
| the joystick's calibration (`177c:029d` in the setup, `4def:638d` in Initialize Mouse/Joystick) | the centre the mean of 20 reads; then each corner's prompt, up to 20000 polls for button 1 (`Couldn't read the joystick.  Aborting calibration.` or `Unable to read joystick` and 2 seconds), the least (upper left) or most counts of 20 reads with it held, the thresholds a third and two thirds of the way, all in 16 bits; a wait for the button to come up between | `cok_stick_calibrate` |

The direction of a click (`1743:0167`): from cell (fx, fy) to (tx, ty),
with dx and dy the distances, the first of up (ty ≤ fy and 0x26a × dx / 256 ≤
dy, tan 67.5°), up-right (ty < fy, tx > fx, between), right (tx ≥ fx and
0x6a × dx / 256 ≥ dy, tan 22.5°), down-right, down, down-left, left and
up-left, so ties go to the axes and the same cell is up; the products in 16
bits, so past about 300 cells across none holds and the original loops for
ever (the port gives 0; only a corrupt var 0x4bc3 gets there). The stick's
direction (`177c:003b`): its counts against the thresholds (the last of the
1st to 5th below the count, unsigned), and only the outer bands, past two
thirds of the way to an edge, push (`DS:4334`).

The quirks kept: a click in the title menu's column 0 gives Enter (the
list's columns `DS:6e2f`, `6e30` and rows are 0 before any list: they are
uninitialised memory, taken as the 0s DOSBox gives; and the party list's
rectangle `DS:6e31`-`6e34`, never cleared, likewise); the party list keeps
taking clicks over a list drawn on it (a shop's), as Up; Modify's sheet
gives Home for a click on a field's row beside the field, which goes back to
the field clicked last; a click on a row of the view or map outside Move
mode does nothing; the list takes a click below its rows from the menu as
Enter and then does nothing; text input drawing or erasing at column 40,
which ECL's INPUT STRING reaches with its 40 characters (`2fd3:0a57`),
hides the cursor for good (`1521:0353` and `030a` hide it and leave
without the show past column 39), until the mouse is turned off and on.
Not reproduced: where the original draws under a shown cursor, the
overland map's mark after a picture, a battle or a camp (`4877:0005` from
`2fd3:096c`, `1c5a`, `346a`, unlike the step's `475c:08d5`), two
artifacts: it saves the cell under it with the cursor's image in it
(`4877:0026`, `127f:0edb`), which the next step paints back; and the cell
it puts (`4877:008b`, `127f:10e7`) covers the arrow, so when the pointer
next moves the driver restores the background it saved before the mark,
and the part of the mark under the arrow's 16×16 box vanishes until the
next redraw. The port's cursor is never in the screen. In DOSBox-X with
its own driver (`f_mouse.py`) the second was seen after a camp (25 pixels
of the mark gone where the arrow had been); the first, the arrow's image
painted back after a step, was not. A read of the stick
that fails as the calibration's first of the centre's twenty adds the
words at `[bp-7]`, `[bp-9]`, which the probe did not write (it reads into
`177c:0000`'s own locals): what the caller's previous call left there,
fixed by the code's layout (`0a46:084a`, the WriteLn at `11c7:0840`, for
`029d`; `0a46:056c`, the Rewrite at `4def:60aa`, for `638d`), not computed
here; the port adds 0.

The machine: `cok` always has a mouse driver (SDL's pointer) and has the
first joystick SDL finds; `eclplay` has neither unless `--mouse` and
`--joystick` give them, so its transcripts with the assets' `M` are those
of a machine without a mouse. Both take `--input mouse`, `joystick` or
`none` in place of the file's third line (`none` is `N`; `M` without a
driver ends the same); `--input joystick` over a file without `J` calibrates
the stick at startup (see the options). In `cok` the pointer follows the
window's: its place over the screen as shown, letterboxed and at any scale,
each window pixel taken at its centre, as the driver's x (0-639) and y; the game's moves of the pointer (startup's) move the
window's where it is over it; the window's own cursor is hidden while the
game's mouse is on. The stick's axes give counts 1-256 (the original's
depend on the machine's speed, which is why it calibrates), its first two
buttons buttons 1 and 2; a read of it is instant, so `cok` gives the
calibration's polls 20 ms each (20000 of them: about 400 seconds) and a
pushed stick a key every 60 ms, about the original's pace on a 286. Keys
for scripts (`--keys`, both): `\mX,Y,B.` puts the pointer on pixel X, Y and
presses buttons B (1 left, 2 right, 3 both; `,B` left out for a move
alone), as the game reads its next key, the buttons coming up once the game
has read the click and waits or reads again; `\jX,Y,B.` puts the stick at
counts X, Y with buttons B (1 button 1, 2 button 2) until the next `\j`;
where the game polls them in a loop (the menus' wait for the buttons, the
calibration) the next `\j` or move comes, else the stick's buttons come up,
and with nothing to change the keys' end ends the run. `cok` presses them
through SDL's mouse events and a virtual joystick (attached at the start
when the keys have `\j`), so the same keys give the same frames.

`tests/test_input.c` replays 150 random clicks of 67b5:03e2's and 740 on
the edges of its regions (the key, the extended flag, the selection,
Modify's field, the selected character and the pick), 1743:0167
from (0, 0) to every cell within 30, and 150 of the joystick unit's reads
and both calibrations (prompts, thresholds, reads, failures) against what
the original gave in an 8086 emulator, the menu's routine run whole with
the keyboard and drawing stubbed and the game's state laid out in memory,
`177c:060d` stubbed to give the stick's reads. Of 3,000 random clicks and
880 on the regions' edges all agreed, every instruction of the click's code (`67b5:065c-0a88`) run but
row 24's after a timeout in the same pass (the port's timeout is its own
read); of 50,625 directions, all; of 2,000 joystick cases, all, every
instruction of `177c:0000`, `003b` and the calibrations run but their outer
loops' repeats (never taken) and stick 2's buttons (no caller). It also
checks the cursor's drawing, the units, the keys they give, the cursor's
hiding around text and INPUT STRING's drift, a list's clicks, the file
Initialize Mouse/Joystick writes and the setup's calibration;
`tests/test_eclplay.py` plays the menus, Modify, a walk in Throtl, the
overland map and a battle's Move and aiming by clicks against the keys that
do the same, frame for frame, the party list, the title's hidden cursor, the
mouse off and on, the joystick's keys and both calibrations; and
`tests/test_cok.py` the clicks and the joystick through SDL's events frame
for frame as `eclplay` draws them, at two scales.

## Sound

`src/sound.c` is the sound, without SDL: the original's effects driver
(`17e8`, assembler linked into `START.EXE`), its title music (`1661`,
which loads its driver from `MIDI.DAX` and the score from `SOUND.DAX`),
the timer interrupt that runs them, and the hardware they write to. The
game calls it through `hooks.sound` (`cok_sound_call`) as the original
calls the drivers; `cok` plays it through SDL's audio device and
`eclplay --wav FILE` renders it to a WAV file (16-bit mono, 44,100 Hz).
The drivers (what the original computes) are kept apart from the
synthesis (what the hardware did): each port write they make goes to
`cok_sound.trace` as well as to the hardware, and those writes were
compared with the original's, run in an 8086 emulator (see below).

### Devices

Startup (`11c7:031a`) reads `krynn.cfg` in the game's directory, as Turbo
Pascal's text reads go: `Read` of the first line's first character (the
display: `E` EGA, `T` Tandy, else CGA), `ReadLn`, then `Read` of the second
line's first character, the sound device, into `DS:577a`: `C` 0 (CMS, the
Game Blaster, ports 0x220-0x223), `R` 1 (Roland LAPC, an MT-32 through an
MPU-401, 0x330), `A` 2 (AdLib, 0x388), `T` 3 (the Tandy's sound chip,
0xC0), `P` 4 (the PC speaker), anything else 5, none (the setup's `[6]
Silent` writes `S`). Only those upper-case letters count; nothing is
skipped before them, so a space, a lower-case letter or an empty line is 5.
`ReadLn` ends a line at a CR (and a LF after it) and stops at a Ctrl-Z or
the end, where `Read` gives Ctrl-Z: a file of one line, or with LF line
ends only (one line to Turbo Pascal), is 5 too. Without the file the
original shows its setup (see Startup and the setup), writes it and reads
it. There is no other choice of the device, in the game or on its command
line. The effects play on the Tandy's chip for `T` and on the PC speaker
for `C`, `R`, `A` and `P`; the music's driver is `MIDI.DAX` record device +
1.

The port reads `KRYNN.CFG` the same way (`cok_config_parse`,
`src/config.c`, which reads the other lines too; `COKAssets/KRYNN.CFG` says
`E` and `T`), from `--config`, the saves directory or the assets (see
Startup and the setup); without one, the setup asks and writes it.
`eclplay` without `--start-menu` runs no setup: without a file it says so
on standard error and takes the Tandy, for the display as well. `--sound
tandy`, `--sound speaker` or `--sound off`, in `cok` and `eclplay`,
override it. With `C`, `R` or `A` the effects play on the speaker and the
title is silent: their music drivers (`MIDI.DAX` records 1-3) are not
ported, which is logged as `unported: the title's music on C/MS (MIDI.DAX
record 1)` (or `Roland LAPC`, 2; `AdLib`, 3) when the title starts, and
said on standard error at startup.

### The effects driver

`17e8:0020` takes a command (a word; the set at `17e8:0000` lets 0-15
and 0xff through), unless the title's music plays (`DS:5780`); with the
device off (5) only 15 does anything:

| Command | Does |
| --- | --- |
| 0 | stop: both chips' channels start sound 0, which silences them |
| 1 | let the channels sound (`17e8:0156`, which both chips' output checks; 1 from the start, and nothing clears it) |
| 2-12 | sound command - 1, on the Tandy (device 3) or the speaker |
| 13, 14 | nothing |
| 15 | arm a copy-protection counter (`DS:4b22` points at `17e8:0124`): 5,000 ticks later the interrupt prints `Run-time  Error 209 at 0BAD:0038` and quits to DOS; nothing sends it |
| 0xff | stop, unhook the timer (`12fa`: `INT 8` back, PIT channel 0 to 0xffff, the speaker and the four voices silenced), `DS:577c` 0 |

`17e8:00da` hooks it: `DS:577b` = `DS:577a`, `DS:577c` 1, and unless the
device is off `INT 8` to `17e8:1333` and PIT channel 0 to 0x13b1 (written
after channel 2's control word 0xb6, so channel 0 keeps its mode), 236.7
interrupts a second, every 13th passed on to the BIOS's (13 × 0x13b1 is
65,533), then sound 0. Main hooks it after the title (`1000:0160`).
Around every overlay the game loads, main's (`1000:01d8`, `0282`, `02b8`,
`02de`) and `COMBAT`'s (`2fd3:191c`: before the battle, around the end
of combat, which so runs with the driver off, around a shop's, the
temple's or treasure's overlay, and after them), it unhooks the driver
with 0xff if `DS:577c` is set and hooks it again after unless it is.
Before the title that follows a
demonstration it sends 0xff and hooks it after (`1000:0304`, `030e`),
and stops the sound after its menu (`1000:036d`). Every DAX record read
(`169c:088e`) sends 0 before reading and 1 after, so a picture, an icon,
a monster or a script loaded cuts the sound playing; the port sends them
from its record reads (`find_record`).

Each chip has four channels of 0x30 bytes (the speaker's at `17e8:0176`,
the Tandy's at `0310`); a sound's table entry (`04a8` + 8n, `0550` + 8n)
names a script for each, and every sound names all four (an empty script
for those it does not use), so a sound replaces the one playing whole:
sounds cut each other, nothing queues, and the game never waits for one.
Each interrupt, unless a sound is being started, runs the speaker's
channels, then the Tandy's, whatever the device: per channel a volume
slide, a pitch slide, vibrato from a table, a note-off countdown, the
script when its count runs out, and an envelope (pairs of a slide and a
count, 0xffff setting the level). Scripts set any field (0xff), loop on a
counter (0xfe), switch to another channel (0xfd), call and return (0xfc,
0xfb), clear the channel (0xfa) and play groups of three-byte notes (the
octaves from a table at `02f6` or `0490`, shifted right); the shipped
sounds use the field writes alone. Then the speaker plays the first
channel sounding (PIT channel 2's count, port 0x61 bits 0-1 its volume &
3), and the Tandy's four voices are all written, pitch and volume, every
interrupt (the noise voice only its control byte); after a stop
(`17e8:0148` 0) the speaker is turned off and the voices silenced each
interrupt. The sounds, where they play, and how long they sound (from
the command to the driver's last tick with a voice louder than
attenuation 15 on the Tandy, or with port 0x61's bits 0 and 1 both set
on the speaker):

| Command | Sound | Where | Tandy, speaker |
| --- | --- | --- | --- |
| 2 | 1 | a spell cast in combat (`5b04:166e`), but Fireball and Lightning Bolt; each missile of a spell that sends one to each target but the last picked (`5b04:1d26`) | 0.81 s, 1.01 s |
| 3 | 2 | text flashed over a combatant, kind 0 (`6346:2316`) | 0.21 s, 0.15 s |
| 4 | 3 | text flashed over a combatant, other kinds (`6346:230b`) | 1.01 s, 0.89 s |
| 5 | 4 | a combatant drops (`6beb:0e19`, `0eb8`) | 1.59 s, 0.74 s |
| 6 | 5 | item types 0x36 and 0x37 and sling stones flying (`432f:2d15`, `2d66`) | 1.01 s, 0.63 s |
| 7 | 6 | a hit (`432f:15d0`, `1873`) | 0.08 s, 0.27 s |
| 8 | 7 | Lightning Bolt (`5b04:1663`) | 2.38 s, 2.53 s |
| 9 | 8 | a miss (`432f:1926`); hand axes, clubs, hammers and other thrown things flying (`2ceb`, `2da7`) | 0.25 s, 0.04 s |
| 10 | 9 | a step or turn in 3D (`475c:081a`, `0c58`, `0ca6`), the party on a new square (`2fd3:3eb9`), `CALL [b203]` (the scripts' steps on the overland map), a combatant's step (`432f:094a`) | 0.07 s, 0.03 s |
| 11 | 10 | Fireball (`5b04:1654`); `CALL [b203]` with `0x3de` 10 | 1.81 s, 1.44 s |
| 12 | 11 | a missile set off (`432f:2bf5`), then darts, javelins, quarrels and arrows flying (`2cbd`) | 0.13 s, 0.13 s |

The port logs the commands it logged before as `sound:` (combat's, `CALL
[b203]`'s); the steps and turns, the record reads, the hooks and
unhooks are not logged, but `eclplay --sound-calls` prints every call as
`call:`.

Ctrl-S (0x13) read by `1614:025b`, through which every key is read but
text input's (`1521:0739` reads `Crt.ReadKey` itself), turns the sound
off (`DS:577b` = the device, command 0, the device 5) or back on (the
device = `DS:577b` and command 1), the latter only for the Tandy or the
speaker (the set at `1614:022c`), and not while the title's music plays.
The key is still returned, and Alt-R's scan code (0x13 after a 0) does
the same. Since `17e8:00da` copies the device to `DS:577b`, a title after
a demonstration played with the sound off leaves it off for good.
Quitting to DOS (`1614:0000`) puts the device back from `DS:577b`, then
stops the title's music or sends 0xff.

### The title's music

The title (`2f55:063c`) sets `DS:5780` and, unless the device is off,
loads the driver (`1661:000a`: `MIDI.DAX` record device + 1, afresh
each time), hooks `INT 8` (`1661:02e6`, PIT channel 0 at 0x5555: 54.6
interrupts a second, every third passed on), loads the score (`014d`:
`SOUND.DAX` record 1, 14,064 bytes, one for every device) and plays it
(`02b3`: the driver called with AH 1). Each interrupt calls the driver
with AH 0; at the title's end (`2f55:006d`, after the credits' wait or
a key) AH 2 stops it, `00da` unhooks it and `DS:5780` is cleared. Only
the title has music. The score is MIDI-like: delta times in ticks, notes
on and off, programs, pitch bends, running status, `FC` ending it (`FC
80` would loop; the title's does not). It lasts 7,860 ticks (2 min 24 s),
longer than the title, which always cuts it.

The Tandy's driver (record 4) plays four voices, the MIDI channels at
score bytes 0x43-0x46 (1, 3, 2 and 9) transposed by 0x47-0x4a, notes
folded into its table's 51 by octaves; each voice takes its channel's
instrument (an attenuation envelope, a vibrato table and its depth); a
note finding its channel's voices busy takes the one sounding longest;
channel 9's notes 0x24 and 0x26 are a bass drum and a snare on the noise
voice. The speaker's (record 5) plays one voice, the channel at byte
0x4b transposed by 0x4c, and channel 15's notes 0 and 1 as short pitch
sweeps (one pseudo-random). A pitch bend or vibrato that would overflow
the driver's divides ends the original with runtime error 200; the port
stops the music there and says so (`sound.divide_error`); the shipped
score never does. Without `MIDI.DAX` or `SOUND.DAX` the title is silent
and `cok` and `eclplay --wav` say why on standard error.

### Time

The original's drivers run from the timer interrupt between any two of
the game's instructions; the game never waits for them. `cok_sound`
counts time in clocks of the PC's 14,318,181.8 Hz crystal (157.5 MHz /
11; the PIT counts every 12th, the Tandy's chip steps every 64th), runs each interrupt when
its time comes, and renders samples at 44,100 Hz; a call lands at the
next sample. Rendering in one piece or in any number gives the same
samples. A count written to PIT channel 0 after a control word restarts
its interrupts; one written alone (`17e8:12c5`, `12fa`) takes over at
the next interrupt.

`cok` by default lets the audio device time the drivers: its callback
renders as the device plays, so the music and the sounds go on through
the game's delays and while it waits for a key, as the interrupt-driven
original's did, and the game's calls land as they come, a buffer (23 ms)
ahead of the speaker. With `--no-delay` (or `--sound-clock game`) the
game's own clock times them instead: its delays (`Crt.Delay`: the text's
characters, `DELAY`, the title's waits, the missiles' steps) and the
`\pN.` pauses of `--keys`; a key read takes no time. That is how `eclplay
--wav` renders, with a second more at the end, and `cok` then gives SDL's
disk writer (`SDL_AUDIODRIVER=disk`) the same samples, played out to the
end of that second before it closes. The game runs on as it renders; at most 12
seconds of samples wait for a device that plays slower (a real one with
`--no-delay`), the oldest dropped beyond that (`--log` counts them),
while the disk writer is waited for instead. By the game's clock a sound cut
short by the next command or by a key read that took no time is cut
there; in real time it plays as long as the player takes.

### The hardware

The Tandy's chip is modelled as MAME has the NCR 8496 of the Tandy 1000:
three square waves of 3,579,545 / (32 N) Hz, N 0 counting as 1024 (MAME's
`sn76496_base_device::write` gives every chip but Sega's PSGs a period
of 0x400 for 0: `if ((m_register[r] != 0) || m_sega_style_psg)
m_period[c] = m_register[r]; else m_period[c] = 0x400;`; the effects
driver writes 0 on the first tick of most sounds), and a noise
voice shifting a 16-bit register every 32, 64 or 128 steps or at tone 2's
rate, fed back at bit 15 from bit 1 (periodic noise) or from bits 1 and 5
XNOR'd (white), the register reset to 0x8000 only when the
white/periodic bit changes (the effects driver writes the noise's control
every interrupt); a data byte goes to the register latched last;
attenuation in 2 dB steps from 8,191, 15 silent. Each voice adds its
level while its output is high; the sum is averaged over the chip's
steps in each sample. The PC speaker is PIT channel 2's square wave
(mode 3, a new count taking over at a half-wave's end, the gate in port
0x61 bit 0 holding it high and restarting it), heard while bit 1 is set,
at 8,191. A machine with a speaker only hears no `0xC0` writes. The sum
goes through the coupling capacitor (a high-pass at about 14 Hz), is
scaled by `--volume` and clipped to 16 bits. Averaging over each sample
is the only filter, so the square waves' harmonics above 22 kHz alias
somewhat; the game's tones are low enough that this is faint.

### Checks

The drivers were run against the original's code in an 8086 emulator
booting nothing: `START_FULL.EXE` loaded and relocated, `17e8:0105`,
`0020`, `00da` and the interrupt `1333`, `1661:02e6`, `02b3`, `0353`,
`0321` and the interrupt `035a` with `MIDI.DAX` records 4 and 5 loaded,
`1614:025b` reading Ctrl-S and `1614:0010`-`0039` for quitting; each
operation's and each interrupt's port writes (0x40-0x43, 0x61, 0xC0)
compared, write for write. 2,704 scenarios on devices 0-5 agreed, 0
differed (3.2 million lines): 800 random runs of commands, installs,
overlay pairs, record reads, Ctrl-S and quitting with interrupts between
(every command, overlapping sounds, the toggle on every device), some
with the title's music first; the whole score 16 times on both drivers;
800 scores made up to take every path (bends, the divide overflows,
unknown statuses, running status, loops, every instrument); 200 runs of
the speaker's drums with every opcode of their scripts; 800 sets of
effect scripts made up with every opcode, notes, envelopes and vibrato;
the sounds no command plays (12-20); and the copy protection's
counter. Every instruction of the effects driver and of both music
drivers ran but their padding and one unused routine. `tests/test_sound.c`
checks the port's writes against digests of the original's for 112 of
these scenarios and the first ticks byte for byte, the driver's data
against `START_FULL.EXE`, Ctrl-S, the timer's rates, the chip's tones,
noise and volumes, the speaker's tone and gate, and rendering in pieces;
`tests/test_hooks.c` and `tests/test_adventure.c` where the game calls
the driver, and `tests/test_eclplay.py` and `tests/test_cok.py` the
calls in whole games, `--wav` and the audio device (see Playing).
`make sanitize` also builds `cok` with ThreadSanitizer (`build/cok_tsan`)
and plays sound through it in real time, the game's calls crossing the
audio thread's rendering, so that a call made without the lock fails.

What the comparison does not show: the time between interrupts (it
compares one operation or interrupt at a time; the PIT's rates and the
sample clock are `tests/test_sound.c`'s), the BIOS's tick every 13th or
3rd interrupt (only its count, `17e8:0120`, as data), the music's
loading (`1661:000a` and `014d`: the emulator gets the records' bytes put
in place; their record reads come after `DS:5780` is set, so they do
nothing), and what happens after a divide overflow or the copy
protection's counter ends the original (it compares up to there; that
the port stops the music or the driver then is `tests/test_sound.c`'s).
The emulator starts from the executable's own data segment and
`17e8:0105`, as the game does.

Not modelled: the time the original's disk reads and overlay loads took
(the sound stays stopped meanwhile there; here they take none); the
port's record reads stand for `169c:088e`'s, and where the port reads a
record twice or keeps one the original read again, the sound stops at
the same moment or one read sooner or later; port 0x61's other bits
(read as 0); and, in the hardware, anything MAME's model of the NCR 8496
leaves out (see Checks against the original).

## Display modes

Startup (`11c7:031a`) takes the display from `KRYNN.CFG`'s first line
(see Sound's Devices): `E` sets `DS:4b76` to 1, EGA, video mode 0xd
(`127f:00cf`), with the shown and drawn pages 0 (`DS:4b72`, `4b73`,
`1521:0071`, `00a0`); `T` 2, the Tandy's mode 9, after lowering the
heap's end by 16 KB for its video memory (`DS:438e`, the old end kept in
`DS:483e`, never read); anything else 0, CGA, mode 4, 320×200 in four
colours. The setup the original shows without the file offers `[1] CGA
[2] EGA [3] Tandy` (see Startup and the setup). The port reads it with the
sound device (`cok_config_parse`): the assets' `E` plays EGA; `--display
ega`, `tandy` or `cga` in `cok` and `eclplay` override it, and `cok --log`
prints `display: EGA`.
`cok_adventure_open_display` opens the game in a mode (`game->display`,
`DS:4b76`; `cok_adventure_open` in the Tandy's).

The original reads the mode in these places (a scan of `START_FULL.EXE`
for `DS:4b76`, and the tables and variables that go with it):

| Where | Tandy | EGA | CGA |
| --- | --- | --- | --- |
| `127f:1c2f` pictures | 4 bytes a unit and row, packed nibbles | 4 plane bytes a unit | 2 bytes, 2 bits a pixel |
| `127f:02a5` load | the record's nibbles; transparent ones masked and 0 | planes, the same pixels | each colour's CGA colour from the record's map (header bytes 9-16, nibble n for colour n, `127f:007b`, `DS:1e0e`); the mask from the colour |
| `127f:08d7`, `09c3` draws (`2678`/`270d`, `27af`; `23d1`/`2460`, `24fd`; `2151`/`21d8`, `226c`) | | the same arithmetic scaled: the same pixels | the same, on 2-bit pixels |
| `127f:0b94` mirror (`0b63`, `0a51`, `0ad6`) | nibbles swapped | plane bits reversed | bit pairs reversed |
| `127f:0edb`, `10e7` save and put | | through the planes | |
| `127f:12e8` the view to the screen | | onto page A200, then latched to A000 | |
| `127f:15fd` recolour | nibbles | planes | CGA colours through the picture's map |
| `127f:1d71` fill | colour × 0x11 | planes | the colour's CGA colour in the map passed, always `DS:1e1e`: 0-15 to 0 0 1 1 2 2 1 2 0 1 2 1 3 0 3 3 |
| `1521:01df` text (`0e1d`, `0dbd`, `0d1e`) | | write mode 2 | colours through `DS:1de9`: 0 0 0 1 2 2 2 3 0 1 1 1 2 2 3 3 |
| `1521:0a34`, `0b60`, `1128:07e6` clears | four banks | pages 0 and 1 | two banks |
| `3e99:0731` startup | `SKY.DAX` 250-252 loaded | the same | none |
| `69ea:0184` backdrop | 44 rows of sky, 2 of `DS:6d82` (0), 42 of `6d83` (8); the sun, the strip, the horizon | the same | 43 rows of sky, 1 of `DS:6d81`, 2 of `6d82`, 42 of `6d83`, through `DS:1e1e`; no pictures (Death Knights' `6d21:0177`: as in the other modes, the view's `cga_backdrop`) |
| `6945:012c` after a step | `DS:6d83` 8 | the same | where `DS:884f` is set (a block entered, 0xc059 or 0xc05f written): `6d81` 0 for 0xc05f 8 or 0x41, else 15; `6d82` 8 for 8, else 7; `6d83` 8 for 0xc059 0x0b, 1 for 6, else 6 (`6945:0014`, `0048`, `007c`) |
| `6d21:0408`, `0478` icons; `4b6d:08e0`, `0938` characters' | colour 8 drawn black (`DS:1e26` to `1e36`) | the same | not |
| `6961:0497` sprites | colour 13 drawn black (`DS:0dd4` to `0de4`) | the same | not |
| `4def:3d46` the icon editor's icons | on colour 0, COMSPR 25's frame behind | the same | on colour 3 (cyan), no frame |
| `4def:4276`, `42d0` its 1st-color, 2nd-color | their menu | the same | `Sorry, not in CGA` in yellow (white) on row 24 for speed × 100 ms (`1521:0af1`); the menu stays |
| `3e99:00ac`, `4def:5edb` the mouse's cursor | `DS:42b4` | the same | `DS:42f4`, two bits a pixel (see The mouse and the joystick) |
| `169c:0000` BIOS text | | | the colour through `DS:1de9`; no caller |

No file or record is the CGA's alone: every mode loads the same (`CPIC`,
`CBODY` and `CHEAD` are the combat icons of all), and every record's map
holds CGA colours 0-3 (`SKY.DAX`'s are all 0). No code sets a palette (no
INT 10h AH=0Bh or 10h, no write to port 0x3D9 or the attribute
controller), so CGA shows the BIOS's mode 4: palette 1 with intensity,
black, light cyan, light magenta and white (`cok_cga_palette`); EGA the
16 colours of mode 0xd, the Tandy's.

EGA looks as the Tandy: its routines give the Tandy's 16-colour pixels for
everything the game passes. They differ only past that: colours above 15
(the EGA takes their low four bits, as the port does in every mode),
recolouring's random flag (two `Random(256)` a unit; no caller passes it),
fills 128 units wide or more (a signed byte counts them), and a row of the
view marked with nothing drawn (a draw of no width on a clean row), where
`12e8` copies 16 KB of the page A200 to the screen. The game passes none of
the first three; of the last, a probe of 68 `eclplay` runs (the Makefile's,
these notes', the start menu's) met no draw of no width. The port draws EGA
as the Tandy; its game-level code reads only CGA against the others. What
differs is two stack leftovers (below).

In CGA mode the port keeps its screen of nibbles, each a CGA colour 0-3:
a picture made or loaded for it (`cok_picture.cga`) holds them, so draws,
mirrors, saves and the icons' composition (`4b6d:0784`) are the Tandy's
routines on 2-bit pixels, while loading (`cok_picture_load_cga`), fills,
text and recolouring map colours as the table says. Recolouring is lossy:
it matches the CGA colour, so it turns every colour that shares it, and
transparent pixels where the map gives 0 (a masked draw then ORs the new
colour into what lies under them, as the original does). A map value
above 3 ORs bytes of the tables after `DS:1e0e` into its byte's four
pixels, as `127f:02a5` and `15fd` do; no record has one. A CGA picture
shows in `cok_cga_palette` (`cok_image.palette`), in `eclplay`'s
`--shots` and `--screen` and in `cok`'s window and screenshots. A value
above 3 on CGA mode's screen can only come from a picture drawn on it
without CGA's colours, a bug the palette would hide: `eclplay` stops there
when it saves the screen, and `cok --shots` fails
(`cok_picture_cga_valid`).

Two stack leftovers depend on the blit routine (see Spells in combat):
Fear aimed at its caster's own cell reads the locals the clipped blit of
its icon left (`drawing_locals`); EGA's (`24fd`) are the Tandy's but for
the unit's x, which no cone reads; CGA's (`226c`) halve every byte count
(a unit is 2 bytes): 6 (6 - x), 6, 144, 1008 (y + 1), 6 x. A breath at a
target drawn mirrored reads what `127f:0b94` left (`drawn_bytes`): in
EGA mode the bit-reversed mask byte of the 8 leftmost pixels of the
icon's bottom row, twice (0, sight to one cell, when all 8 are opaque,
not the Tandy's 2); in CGA mode the count of its last `Move`, 2 (sight to
3 cells), and as the side the low byte of the offset of the mirror's mask
in the heap, not known: the run stops if its 256 values give different
targets. Both were compared with the three routines run in the emulator.
Memory is not modelled in any mode: CGA's pictures take half the memory
and the Tandy's heap is 16 KB smaller, so where `6961:00e4` stops loading
a small picture's frames (`MaxAvail` under 0xf3c) differs by mode; the
port loads them all. In DOSBox-X (`f_frames.py`, the guards' close-up at
Throtl's gate, PIC1 record 12's four frames, free memory cut by
`HOOK.COM`'s pad): with `MaxAvail` 40,000 or more before, all four in
Tandy mode (the run at 32,000 failed), 32,000 or more in EGA and CGA mode; about 25,300, two in Tandy and EGA mode, four in CGA's;
about 18,300, three in CGA's, and in Tandy and EGA mode no close-up but
`WARNING: Insufficient Memory` on row 24 in yellow and a wait for a key:
Turbo Pascal's `HeapError` (`DS:439a`, set by `2f48:00b2`) is
`2f48:0042`, which shows that (`1521:096c`) and makes `GetMem` return nil
when `DS:4b27` is set, else prints `Insufficient Memory. Exiting
Program`. About 10,000 reached no wait. The port has neither message.

`tests/test_display.c` replays each screen type's drawing (the title, big
and small pictures, sprites, portraits, the 3D view and its backdrop, the
overhead map, combat with mirrored icons, menus in every colour pair, the
icon editor, recolouring, edges) on the port's routines in each mode and
compares the screen, the view buffer, pictures and masks, 714 in all, with
what the original's `127f` and `1521` left in an 8086 emulator (its EGA's
planes, map mask and graphics controller modelled; the scripts and digests
are in `tests/test_display_data.h`). There every instruction of the
unclipped blits (`2151` to `270d`), the glyphs' (`0d1e`, `0dbd`, `0e1d`)
and the mirrors' helpers ran, nine tenths of the clipped blits' (`226c`,
`24fd`, `27af`: not their rejections of a picture wholly off the buffer),
and each mode's branch of `02a5`, `0b94`, `0edb`, `10e7`, `12e8`, `15fd`
and `1d71`. `tests/test_eclplay.py` plays the new game in each mode: EGA's
and the assets' are the Tandy's frame for frame, CGA's the same lines in
four colours, its menus' text through `DS:1de9`, its 3D view, its icon
editor; `tests/test_cok.py` that `cok --display cga` shows `eclplay`'s
frames. `tests/test_picture.c`, `test_text.c` and `test_view.c` check the
CGA mappings and backdrop; `test_adventure.c` and `test_arena.c` the game's
CGA paths (no sky, sprites' and icons' colours, the backdrop's colours by
0xc05f and 0xc059); `test_cloud.c` and `test_spell.c` the leftovers in each
mode; `test_config.c` `KRYNN.CFG`.

## Image export

`daximages` writes native-size 24-bit BMP images and a labeled HTML contact
sheet (`index.html`) into the specified output directory. The directory is
created if absent; its parent must already exist. Open the HTML file in a
browser to browse the images and click through to individual BMPs. Keep the
HTML and images together. Running the same export again replaces its files.

By default every image in the archive is exported. `--entry N` selects a
zero-based directory entry, preserving duplicate IDs. Optional `--frame N`
selects a zero-based image within that entry, flattened across groups and
their pixel frames. Filenames include entry, ID, group, and frame; the sheet
also displays the flattened image index and dimensions. Use separate output
directories for different archives or selections.

Supported archive names (case-insensitive) are `TITLE`, `BIGPIC1–3`, `PIC1–3`,
`SPRIT1–3`, `CPIC1–3`, `CBODY`, `CHEAD`, `BODY2–3`, `HEAD2–3`, `COMSPR`,
`CURSOR`, `SKY`, `TILES`, and `8X8D1–3`, with the `.DAX` extension: the
game's `images` lists (see Games); with `--game death-knights` Death
Knights', `PIC1-7`, `SPRIT1-4`, `BACK1-5`, `SKYGRND` and `8X8D1-5` among
them (2,374 images; its 8X8D sets of 256 tiles count 0xff in their
headers). `--game NAME` (for `daxcompose`, before
the output) names the game, `champions` by default. The
`8X8D1.DAX` entry with ID 201 is the 8×8 font, not an image: it is reported
as unsupported while the other entries continue exporting. Draw it with
`daxcompose`'s `glyph` and `text` commands instead. Unsupported archive
names are rejected; malformed images and failed writes give a nonzero exit
status. Bulk exports can produce a partial sheet when some entries fail.

The image reader verifies the complete record layout before exporting. It
supports both single 17-byte headers (including multiple pixel frames) and
the `PIC`/`SPRIT` collections: a group-count byte, then four metadata bytes
and an image header/payload per group. Each image header begins with 16-bit
little-endian height, width in eight-pixel units, x and y placement fields,
then a pixel-frame count at byte 8. Bytes 9–16 hold a 16-entry, two-bit CGA
colour map, unused by these tools (the game's CGA mode uses it; see
Display modes). Pixels are packed high nibble first. The
four collection metadata bytes are not yet interpreted.

In `PIC1–3`, each group after the first stores its pixels XORed with the
first group's, as decoded by the game's picture loader in `GAME.OVR`; the
groups are animation frames. Both tools decode this before exporting or
drawing. `SPRIT` groups are not delta-coded.

Previews use the standard EGA palette and render every pixel as opaque.
They retain the confirmed purple splash background. In-game sprite masking,
placement, animation timing, CGA mappings, and DOS display aspect-ratio
correction are not applied. These are asset inspection exports, rather than
composited game screenshots.

## Picture composition

`daxcompose OUTPUT.bmp COMMAND...` starts from a black 320×200 screen, runs
the commands in order, and writes a BMP:

- `fill X Y UNITS ROWS COLOR`: fill a rectangle with EGA colour 0–15.
- `draw ARCHIVE ENTRY IMAGE X Y`: draw an image opaquely.
- `sprite ARCHIVE ENTRY IMAGE X Y`: draw with colour 0 transparent and
  colour 13 drawn as black, as the game loads masked pictures.
- `mirror ARCHIVE ENTRY IMAGE X Y`: `sprite`, flipped left to right.
- `text FONT ENTRY X Y FG BG STRING`: draw a string in 8×8 cells, mapped as
  the game does (see below), with colours 0–15.
- `glyph FONT ENTRY GLYPH COUNT X Y FG BG`: draw glyph 0–255 `COUNT` times
  (0–255) along a row of cells.

`ENTRY` and `IMAGE` number entries and flattened images as `daximages` does.
`FONT ENTRY` names a record of raw glyphs: the game font is `8X8D1.DAX` entry 0.
Coordinates follow the original routines: `X` and `UNITS` are in eight-pixel
units, `fill` takes `Y` and `ROWS` in pixel rows, and the drawing commands
take `Y` in eight-row cells. Coordinates may be negative; drawing is clipped.
The game's actual screen layouts live in `GAME.OVR` and are not yet known.

## Library

`src/dax.h` exposes archive loading, record lookup by directory index, and
decompression. Initialize `dax_archive` to `{0}`, open it with `dax_open` (or
copy bytes into it with `dax_parse`), and release it with `dax_close`. Close an
archive before reusing it. Archive pointers and record output pointers must be
non-NULL. Record payload pointers are borrowed from the archive. Allocate at
least `record.decoded_size` bytes before calling `dax_decode`. On a decoding
error, the output may contain partial data and must be discarded.

The on-disk layout is a two-byte little-endian directory length followed by
nine-byte entries: one-byte ID, four-byte payload-relative offset, two-byte
decoded size, and two-byte packed size. Directory length counts bytes, not
records. Fields are read explicitly; C structure packing and host endianness
are irrelevant.

Compression follows `FUN_1000_6e33` in `START.EXE.c`: controls 0–126 copy the
next `control + 1` bytes, controls 129–255 repeat the next byte `256 - control`
times, and controls 127 and 128 are skipped. The decoder rejects truncated
runs, output overflow, and decoded size mismatches. This differs from standard
PackBits; substituting a generic PackBits decoder changes the results.

`src/picture.h` ports the runtime picture routines from `START.EXE`'s
graphics unit (code segment `0x27f` in the unpacked image) for the linear
Tandy pixel layout. Pixels match the DAX format, so 4-bit EGA colour indexes
are kept unchanged; the EGA's planes hold the same pixels, and a CGA
picture holds its CGA colours in the nibbles (see Display modes). The
per-scanline dirty tables that limited copies to video memory are not
ported. The clipped
draws of the three modes (`127f:226c` CGA, `24fd` EGA, `27af` Tandy) share
their arithmetic and copy loops (`127f:2927`, `2986`), so what they do at
the edges, such as the counts of 65536 of The combat screen, is the same
in every mode.

| Function | Original | Ghidra name |
| --- | --- | --- |
| `cok_picture_create`, `cok_picture_free` | `27f:1c2f`, `27f:1cf6` | `FUN_1000_41e6`, `FUN_1000_42ad` |
| `cok_picture_load` | `27f:02a5` | `FUN_1000_285c` |
| `cok_picture_load_cga` | `27f:02a5`, its CGA path | `FUN_127f_02a5` in `START_FULL.EXE.c` |
| `cok_picture_draw` | `27f:09c3` (clipped), `27f:08d7` (unclipped) | `FUN_1000_2f7a`, `FUN_1000_2e8e` |
| `cok_picture_fill` | `27f:1d71` | `FUN_1000_4328` |
| `cok_picture_recolor` | `27f:15fd` | `FUN_1000_3bb4` |
| `cok_picture_mirror` | `27f:0b94` | `FUN_1000_314b` |

A mask byte has set bits where the destination is kept; a masked draw
computes `(destination & mask) | source`. Pictures with a mask have their
transparent pixels cleared to 0. `cok_picture_load_sprite` follows the
`GAME.OVR` loader for masked pictures: colour 0 is transparent, then colour 13
is recoloured to opaque black.

Two clipping paths differ deliberately from the original. Its clipped opaque
draw does not skip clipped source columns, so it shears horizontally clipped
images. Its fill draws an extra row at negative `y`, and at or below the
bottom edge its row count underflows and writes far past the picture. These
ports clip both correctly and skip empty draws. The original's saved
background used a different offset when clipped on the left; the port saves
with the source frame's layout, so drawing the save picture opaquely at the
same position restores the screen.

`src/text.h` ports the 8×8 text routines from `START.EXE`'s text unit (code
segment `0x521`), again for the Tandy layout (CGA pictures mapping the
colours). The original draws straight
to video memory; the port draws into frame 0 of a `cok_picture`, such as a
320×200 screen (40 units by 200 rows). Coordinates are 8×8 cells, 40×25 on
that screen.

| Function | Original | Ghidra name |
| --- | --- | --- |
| `cok_text_glyph` | `521:01df` | `FUN_1000_51b6`, Tandy blitter `FUN_1000_5df4` |
| `cok_text_clear` | `521:030a` | `FUN_1000_52e1` |
| `cok_text_string` | `521:0353` | `FUN_1000_532a` |
| `cok_text_wrap` | `521:04ac` | `FUN_1521_04ac` in `START_FULL.EXE.c` |

A font is raw 1-bit glyphs, 8 bytes each, top row first, most significant bit
leftmost. Set bits take the foreground colour and clear bits the background,
so every cell is opaque. `8X8D1.DAX` entry 0 (ID 201) decodes to 1,416 bytes,
177 glyphs. It was identified by its glyph shapes, because the font pointer
(`DS:0x6128`) is set outside `START.EXE`. Strings map each
character by upper-casing ASCII letters and taking the result modulo 64, so
glyphs 1–26 are A–Z, 32 is a space, and 48–57 are digits; `cok_text_index`
does this. Glyphs 64–176 are reached only by index, through
`cok_text_glyph`: they hold another alphabet, and frame and border pieces.
`cok_text_clear` draws spaces with both colours equal, so it needs a font.

The original checks only the starting cell of a glyph run. A run past
column 39 writes outside the cell row, and glyph indexes past the font read
beyond it. The ports skip cells outside the picture and glyphs past the font.
The original takes unsigned byte coordinates; the ports accept negative ones
and clip. Its Tandy blitter ORs whole colour bytes into the pixel pair; the
ports use their low four bits.

`cok_text_wrap` is the printer `GAME.OVR` uses for running text. It keeps its
cursor at `DS:6134`/`DS:6135` between calls. A word is a run up to a space or
to a run of `! , - . : ; ?` (the set at `DS:0f38`); the space or punctuation
stays with the word. When a word passes the bottom of the window, the
original clears row 24, prints "Press any key to continue" in colour 13, waits
for a key (`1614:025b`), flushes the keyboard (`1614:045c`), clears the window
to colour 0 (`521:0b60`) and continues. The port does the same, but the key
wait and flush go through the `page` hook, and the original's per-character
`Delay(DS:4b38 * 3)`, made when `DS:4b59` is set (`1521:042e`), is the `delay`
hook. The game's printer, `cok_adventure_wrap`, waits the speed times 3 ms
after each character while `game->text_delay` (`DS:4b59`) is set, and hides
the mouse's cursor while it prints and shows it after (`1743:0051`,
`1743:0030`; see The mouse and the joystick). The original's window check
allows a bottom row up to 39, which the port keeps, but it does not reject
a right edge left of the left one or a bottom above the top, which would
make its window clear write far outside the window; the port prints
nothing for those windows. A final one-character word that wraps past the
bottom row is printed below the window rather than paged, as in the
original; a dead branch meant to drop a trailing space at the right edge is
omitted.

The text delay is one flag for every caller of `1521:04ac`, not a choice
of each: running text is typed a character at a time wherever it is
printed while the flag is set. Startup and a new game set it (`3e99:0560`,
`099d`); `PRINT` sets it while it prints and clears it after (`2fd3:0ae3`,
`0b62`); `ENCOUNTER MENU` sets it at its start and clears it as it returns
(`2fd3:23f6`, `2aae`); combat setup clears it (`3cb2:1c5e`), and the end
of the battle sets it (`3995:0297`, after `3995:004b`) as does the end of
combat (`351b:1b00`, not in the demonstration, where `3995:0297` has set it
already). Nothing else writes it, and it is not saved. So
`PRINT` and the encounter's texts are always typed; and after a battle,
or from the start of a game, until the next `PRINT` or encounter, so is
every other running text: "The monsters rejoice for the party has been
destroyed", the NPCs' shares and the treasure left, the temple's and
shops' texts, `ADD EP`'s and `DAMAGE`'s, a camp's say (`6346:1883`), the
items' texts (`546c`) and `VERTICAL MENU`'s prompt, which is otherwise
printed at once (after a `PRINT`, as it usually is). In combat it is
off.

`cok_text_input` (`521:0739`) edits a line on row 24 for the ECL input
opcodes, and `cok_text_number` (`521:08d9`) repeats it, six characters at a
time, until Turbo Pascal's `Val` (`1a46:136a`, ported as `cok_tp_val`) reads
a number from 0 to 65536; the low word is returned, so 65536 reads as 0.
Keys come through a `cok_keyboard`, which returns ASCII codes or 0 followed by
a scan code, as `Crt.ReadKey` does, and a negative value when input runs out.
Characters 0x20-0x7a are taken, so `~` and lower-case `{|}` are not, and each
is drawn one column further right before the cell is filled, which leaves a
blank column after the prompt. The line is upper-cased when Enter or Escape
ends it; Escape does not discard it.

`src/menu.h` ports the row-24 menus from `GAME.OVR`, keyboard only. Menu
text marks each hotkey with `~` (`3775:176e`): the marks are removed, the
marked character upper-cased, and every other letter and digit moved up by
0x20. Letters therefore show in lower case, which the font draws as capitals
in the normal colour, while hotkeys show in the highlight colour. Digits
become P-Y, which the layout (`67b5:00da`) takes as hotkeys: each upper-case
character starts an item, which ends two columns before the next. A digit in
menu text thus splits its item and, if typed as P-Y, returns 0xff, the
original's "not a hotkey" result. Item text is cut to 40 characters.

| Function | Original |
| --- | --- |
| `cok_menu_parse` | `3775:176e`, then `cok_menu_layout` (`67b5:00da`) |
| `cok_menu_draw` | `67b5:01e9` |
| `cok_menu_horizontal` | `3775:1885` with the key loop `67b5:03e2` |
| `cok_menu_read` | `67b5:03e2` as overlay `475c` calls it |
| `cok_menu_list` | `67b5:1368` as `3775:1990` calls it |

The selected item is drawn in colour 0 on the highlight colour. Left and
right arrows, and 4 and 6, move the selection; Enter returns the selected
item's hotkey; Escape and space are ignored. Other extended keys, and the
keypad digits 1-9 and `\` (mapped to scan codes by the table at `DS:1fdf`),
go to a `special` hook: `3775:1885` passes them to `546c:3334` and redraws
the party (see Adventure loop). Because that check follows the hotkey match,
a hotkey 1-9 can only be chosen with the arrows and Enter. With one item, the
arrows and 4 and 6 are special too. A click (Enter with `DS:8987`) goes to
the regions of The mouse and the joystick; it also animates the small picture while it
waits when its caller asks (its flag at `[bp+0xa]`, `game->menu_animates`,
which a front end that waits does: see Playing); flushes the keyboard
(`1614:045c`, `cok_keyboard.flush`) once it has its key, so that keys typed
ahead are lost; and times out when `DS:6e11` is set, as only the title's menu does
(see The start menu): `cok_menu_timed` returns `DS:6e15`'s result when the
keyboard gives `COK_KEY_TIMEOUT`, the time passed with no key.

`cok_menu_list` shows a list in a window with a `Select` menu on row 24,
adding `Next` and `Prev` when there are more rows to page to, and `Exit` if
asked. Up and down (8 and 2) move the selected row, wrapping within the rows
shown; PgUp and PgDn (9 and 3), and `Prev` and `Next`, page. Escape or `Exit`
cancels but leaves the index where it was. The first row shown persists
between lists (`DS:6e0d`) as in the original, which can leave the selected
row outside the window. ECL lists have no headings; the spell lists do (see
Camp).

`cok_menu_ask` is `cok_menu_read` with the digits flag of `67b5:03e2`
(`[bp+0xc]`) clear, as the yes/no prompt (`67b5:177f`), Pics and the saved
game's letter call it: then 1-9 are not directions, and with one item 4
and 6 return themselves. `cok_menu_rows` is `67b5:1368` in full: a prompt
on row 24 in the heading colour, a first item other than `Select`, rows
flagged as headings (node byte 0x29), drawn in the heading colour and
skipped after each move or page in the same direction, at most a page's
rows (`67b5:10d1`), so that a list that starts with a heading starts on
its last row shown; the list drawn only when asked or when the first row
shown moves to the pick; and `Prev` offered only while rows other than the
leading headings are above the window. `cok_menu_list` is `cok_menu_rows`
with no headings.

Death Knights' menus go by index (`dkk 6a69:0f1b`, the descriptor's
`menus.routine`, `COK_MENUS_INDEXED`): `cok_menu_pick` takes the text of
every item the menu may have and the list of those it shows
(`cok_menu_items`, `DS:c120`'s 20 entries, filled by `6a69:00cb`,
`cok_menu_items_upto`, and `00ab`, `cok_menu_items_add`), and returns the
index of the item chosen rather than its letter. `cok_menu_indexed` lays it
out (`6a69:0d1e`, `0135`): each item of the text starts at a letter or digit
and runs to the next, its spaces with it; those listed are copied in order
and one trailing space deleted, their first characters the hotkeys
(`DS:c121`); an `@` in an item puts the items after it on a second row, the
first on row 23 and the second on row 24 from the same column, after the
prompt, which is always on row 24 (`6a69:025d`). Colours are Champions'
(`DS:c148` 10, `c14a` 15, `c149` 13 for the prompt, set once). Keys
(`6a69:0f1b`): a hotkey returns its item's index and Enter the selected
one's; Escape returns the special key 0x1b; an extended key returns its scan
code, but Left and Right, which move the selection, wrapping, and Up and
Down, which change rows by as many items as the first row holds (`0c8c`),
where there are items to move among: those read on (`0c26`, `0c59` and
`0c8c` clear the loop's flag, `[bp-0x13e]`); 4 and 6 move the selection and 2, 8, H and P
change rows where no item has them as hotkeys; with the keypad flag the
digits and `\` are their scan codes (`DS:5791`, Champions' `DS:1fdf`: the 5
is 0x20); Space and other keys are read past; the keyboard is flushed once
it has its key. The mouse's regions (`6a69:0490`-`0ba7`, test and act
routines in pairs) are not taken yet: a click is a key. Death Knights' Yes/No (`6a69:2048`) is `cok_camp_yes_no` through this
loop: `Yes No` by index from No, special keys read past, the prompt always
light magenta. Its lists (`6a69:1b82`) are `cok_menu_indexed_rows`:
the rows as `cok_menu_rows` shows, marks and moves through them
(`6a69:1506`, `170d`, `1a73`, `19db`: 67b5's, but a heading unmarked in the
normal colour, `17ab`), the menu on row 24 by index with the keypad's
directions, the caller's text and items with ` Next`, ` Prev` and ` Exit`
after them as `cok_menu_rows` offers them, each at the index after the one
before from the caller's last + 1, which the caller's items must reach
without a gap for them to show (`6a69:0d1e` stops at the first unused
entry, `DS:c120` being 0xff past them): the items menu, whose last is 8
with fewer items before its own Exit (9), never shows them. An index past
the last is the list's own (Next and Prev page while offered, Exit and
Escape return 0x1b as a special key with no row picked: so the items
menu's Exit pages down while there are rows below, which 16 items in 18
rows never leave); otherwise the index is returned with the row picked.
Up and Down (8 and 2) move, PgDn and PgUp (3 and 9) page, the scan code
0x4e pages down, 0x45 and 0 return as Escape; the rows' clicks
(`DS:c118`, `c119`, `c0fe`) are not taken.

| Function | Original |
| --- | --- |
| `cok_menu_items_upto`, `cok_menu_items_add` | `dkk 6a69:00cb`, `00ab` |
| `cok_menu_indexed` | `dkk 6a69:0d1e`, `0135` |
| `cok_menu_pick` | `dkk 6a69:0f1b` with `025d`, `0442`, `0c26`, `0c59`, `0c8c`, `0e74` |
| `cok_menu_indexed_rows` | `dkk 6a69:1b82` with `1506`, `170d`, `17ab`, `1a73`, `19db`, `1b3b` |

`src/screen.h` draws the screen frames from the root segment `0x128` with
tile set 4, glyphs 0x100-0x127, which the game loads from `8X8D1.DAX` record
202 at startup (`6e22:0050`). Like every tile set it is loaded with colour 13
transparent (`127f:0111` with 13 and 1), which clears those pixels; the
frame is drawn opaque, so they show black. `cok_screen_draw` draws a
screen (`cok_screen`) as the game's descriptor lays it out (its `frame`
section, see Games): the screen under it, the box it clears, then its rows
and columns of tiles. Champions' use tiles 0x114 and up; the tables of tile
values are at `DS:0e3a`, `0e62`, `0e7a`, `0ea1` and `0eae`.
The frame (`1128:0000`) clears the inside and draws the border, with
the three moons of Krynn on the top row at columns 8, 19 and 30, their tiles
offset by the phases in game words `0x4cf9`-`0x4cfb` (the `moons`
section's). The adventure screen
(`1128:0242`) adds the divider on row 16, the column 16 divider above it, and
the box of the 3D view from cell 2 to 14; a big picture's (`1128:0344`)
adds only the row 16 divider, for full-width pictures. `cok_adventure_screen`
draws one with the game's moons.

`START.EXE` is compressed with EXEPACK. `START.EXE.c` was decompiled from the
packed file, so its function addresses match the packed bytes, but far calls
and data references use the unpacked layout. Unpacked offsets are the Ghidra
address plus `0x239`; the data segment is `0x0bc6`. `GAME.OVR` calls the
graphics routines at `27f:offset` and the text routines at `521:offset`.

The SSI splash image in `TITLE.DAX` entry 0 (ID 1) decodes to 32,017 bytes:
a 17-byte header and 32,000 bytes of packed 4-bit pixels for a 320×200 image.
Its purple background was verified against the original in DOSBox.

## ECL scripts

`src/ecl.h` ports the interpreter for the ECL scripts in `ECL1.DAX`-`ECL3.DAX`
from `GAME.OVR`: operand decoding and variables from overlay `3775`, opcode
handlers from overlay `2fd3` (dispatch `2fd3:386d`). Addresses below are from
`START_FULL.EXE.c`; `DS:` offsets are in the data segment.

An ECL record's first two bytes are skipped and the rest loads at address
`0x8000` in a `0x1e00`-byte buffer (`3775:0361`). Row 24 is cleared and
shows `Loading...Please Wait` in light green while the record is read,
then cleared again (`67b5:0c7b`); a record shorter than two bytes the
original reads again for ever, where the port stops. It starts with five
one-operand instructions whose operands are the entry vectors (`3775:01e8`):
`DS:4b39` and `4b3b`, run by the adventure loop `2fd3:3c28` after moves and
for location events; `4b3d` and `4b3f`, run when camping (`2fd3:3403`); and
`4b41`, run when the block loads, always `0x8014`.

Each operand is a type byte and a low byte. Types 1 and 3 read the variable
at high:low and add a high byte, as do type 2, an immediate word, and type
`0x81`, a string variable. Type 0 is an immediate byte, and type `0x80` packs
low bytes of text six bits a character, four characters to three bytes, with
codes below `0x20` meaning `@`-`_`. Strings fill numbered slots from
`DS:7430`. The operand arrays persist between instructions, so a value can
read a stale high byte, as the original's can.

Variables are words (in Champions; Death Knights' are bytes, below):
`0x4b00`-`0x4eff`, `0x7a00`-`0x7bff` and `0x7c00`-`0x7fff` live in three
game data blocks, `0x8000`-`0x9dff` are bytes of the script
itself, and other addresses are special game state such as the map position
(`0xc04b`, `0xc04c`) and facing (`0xc04d`).

The special addresses are the descriptor's (`ecl.reads` and `ecl.stores`,
an address and a `cok_ecl_special` each; `cok_ecl_special_read`,
`_store`): Champions' read `0x4bea` and `0x33d` (the facing, 0-6),
`0xc04b`-`0xc04f` (the square, the facing in steps of 2 outside 3D areas
or halved in them, the wall ahead, the square's value) and `0xb1`, `0xfb`,
`0xfc` (values the host gives, `vm->value_b1` ...); stores to `0x3de`,
`0xb8`, `0xb9` (values the host takes), `0xc04b`-`0xc04d`, `0xc059` and
`0xc05f` (the backdrop's colours) set them (`3775:0fca`, `0e06`). Another
address outside the blocks reads 0 and takes nothing, and a special read
is never widened. A store to `0x4bfd` or `0x4bfe` marks the area changed
and one to `0x4be6` switches the mode, in both games (`3775:0e06`, `dkk
3c1e:0bc1`).

Code outside the VM's operands reads and writes the variables by name
(`cok_var` in `src/ecl.h`, 53 of them: `cok_ecl_var(vm, COK_VAR_DIFFICULTY)`,
`cok_ecl_var_at` for an element of a run the code indexes,
`cok_ecl_set_var(_at)`, `cok_ecl_add_var`), never by `vm->mem4b00[...]`:
where each lies, how wide and how many elements, is the descriptor's
(`ecl.vars`), Champions' a word at the address its name's comment gives.
They read and write the variable's cell itself, without a store's side
effects (the mode, the hooks, a character's field), as the original's
code does outside 3775's routines; a game's byte keeps the value's low
byte, and a variable of width 2 where each address holds a byte is a
little-endian word over two addresses. A name the game has not (address
0) stops at an assertion ("the game has no such variable"), but for the
three Death Knights has no such thing at all, which the code asks
`cok_ecl_has_var` about: the status line's turn (`0x4cff`), the portrait
(`0x7ee1`) and the view's own test (`COK_VAR_NO_VIEW`). A flag
(`cok_ecl_flag`) is set when it is not the game's off value: Champions'
"off the overland map" is `0x4c38` not 0, Death Knights' `0x4cf7` not 1,
where Champions' code tests `0x4c38` with "outside 3D areas" or mode 3;
where it tests `0x4c38` alone (after camp, `2fd3:3403`; in a mode-4
redraw, `6346:2c17`) the flag is `COK_VAR_NO_VIEW`, also `0x4c38`, which
Death Knights has not: its code draws the view there always (`3632:237e`,
`65cd:2db0`). What a block's start sets is the
descriptor's `start` (Champions: `0x7ee1` 0xff, `0x7ed2`, `0x7ed3`, `0x4be5`
0); the block's variables it clears unless kept (`0x4c00`-`0x4c1f`,
`0x7f79`-`0x7f82`) and `DIVIDE`'s remainder and `NEWECL`'s `0x4bf2` are
named too. Only whole blocks are still copied raw: the camp's save and
restore, startup's clearing, the saved game's file (Champions' format).

Many `0x7c00`-range addresses read and write fields of the selected
character's record, by the descriptor's table (`ecl.character`: offsets
first to last, how each reads and is written, a `cok_ecl_character_kind`,
and the field and element of the game's layout; the VM's `rules`, which
`cok_ecl_init` takes; see Games' "The character record's layout"), read as
0 with none selected; `0x7f12` picks the ECL file for the next block.
Champions' (`3775:07d9`, `3775:0c00`), by address (its offsets):

| Address | Field | Read and written as |
| --- | --- | --- |
| `0x7c00` | `NAME` (`+0x00`) | a string, cut to 15 characters; a word store of 0 marks it cleared |
| `0x7c15`, `0x7c18`, `0x7c19` | `SCORE` intelligence, constitution, charisma (`+0x13`, `+0x19`, `+0x1b`) | bytes, read |
| `0x7c20`-`0x7c59` | `MEMORIZED` 0-0x39 (`+0x1e`-`+0x57`) | bytes, written |
| `0x7c61` | `KNIGHT_ORDER` (`+0x5c`) | a byte, read |
| `0x7c72`, `0x7c73` | `RACE`, `CLASS` (`+0x5a`, `+0x5b`) | signed bytes, read |
| `0x7c76` | `MAX_HIT_POINTS` (`+0x62`) | a byte, read |
| `0x7c9b` | `SAVING_THROW` 1 (`+0xd1`) | a byte, read |
| `0x7ca0` | `HIGHEST_LEVEL` (`+0xd6`) | a signed byte, read |
| `0x7ca5`-`0x7cac` | `THIEF_SKILL` 1-8 (`+0xdb`-`+0xe2`) | bytes, read |
| `0x7cb8` | `NPC` (`+0xe7`) | a byte; a value above 0xb2 is written less 0x32 |
| `0x7cbb`, `0x7cbd`, `0x7cbf`, `0x7cc1`, `0x7cc3` | `COINS` 0-4 (`+0xeb`-`+0xf4`) | words |
| `0x7ccc`, `0x7ccd`, `0x7cd0` | `CLASS_LEVEL` 2, 7, 4 (`+0xfb`, `+0x100`, `+0xfd`) | signed bytes, read |
| `0x7cd6`, `0x7cd8` | `GENDER`, `ALIGNMENT` (`+0x109`, `+0x10a`) | signed bytes, read |
| `0x7cf7`, `0x7cf9` | `MONSTER_EXPERIENCE` (`+0x130`), `MONSTER_HIT_POINT_EXPERIENCE` (`+0x132`) | a word, a byte |
| `0x7d00` | `STATUS` (`+0x188`) | read as 1 for 0 (okay), 6 for 3, 5 for 4, 4 for 5, 3 for 6, 7 for 7, 2 for 8, else 0; 0 when the selection is missing; writing 1 sets it 0 and `ACTIVE` (`+0x189`) 1 |
| `0x7d0c` | `HOSTILE`, `COMPUTER` (`+0x18a`, `+0x18b`) | read as 0x81 against the party, 0x80 for one the computer controls on its side, else 0; writing 0, 0x80 or 0x81 sets both so |
| `0x7d19` | `HIT_POINTS` (`+0x197`) | a byte |
| `0x7d1b` | `MOVEMENT` (`+0x198`) | a byte, read |
| `0x7ecf` | `SCORE` charisma | read through the reaction table, 0 from 26 |

`0x7cc9`, `0x7eb1` and `0x7eb4` are the host's (see Playing scripts),
`0x7f3e` reads its own word as a field, and the other addresses of the
range hold their own words.

`cok_ecl_run` runs control flow, arithmetic, comparisons, `RANDOM` (Turbo
Pascal's generator), `SAVE`, tables and `NEWECL` itself, and decodes every
other opcode's operands before handing it to the `opcode` hook. Those opcodes
drive pictures, text, menus, monsters, combat, items and characters; the
screen-side ones and the party's are ported in `src/adventure.h` (see
Playing scripts), the monsters' and encounters' in `src/monster.h` (see
Monsters and encounters); of their handlers only `PROGRAM 0`, the start
menu's training, is not. Comparisons order the first operand against the second,
but `AND` and `OR` set the flags for 0 against their result; `SUBTRAT` stores
the second minus the first; `DIVIDE` leaves its remainder in
`0x7f3f`. A failed `IF` skips the next instruction using its own operand
table (`3775:1e74`), which disagrees with the handlers for the menu and
`ON GOTO` forms, `ECL CLOCK`, `ADD NPC` and `PROTECTION`; the port keeps that
table, and no shipped script puts those opcodes after an `IF`.

`ecldump` lists every instruction reachable from a block's vectors through
jumps, calls, `ON GOTO` tables and both arms of each `IF`. All 22 blocks
decode, 13,547 instructions. The opcode names come from the game's own trace
table (`6d7e:0055`), including its spelling `SUBTRAT`.

What each opcode does, the operands it loads and those a failed `IF` skips
with it are the game's (the descriptor's `ecl`, see Games): the VM and the
hooks dispatch on an opcode's meaning (`cok_ecl_meaning`), a
`cok_ecl_opcode`, not on its byte, which stays `vm->opcode` (the traces
print it); Champions' every opcode means itself, so its table is the one
the VM held before (`tests/test_ecl.c` checks it opcode by opcode against
the old arrays). A list opcode loads a header of `header` operands whose
last counts the list (up to 4: Death Knights' `0x27`). `CALL`'s addresses
are the descriptor's too (`cok_ecl_call_of`).

Death Knights of Krynn (`--game death-knights`; its handlers `dkk
3632:0000`-`28d2`, dispatch `3632:2a54`, skips `3c1e:1b48`) keeps every
opcode's meaning but ten:

| Opcode | Champions' | Death Knights' (the port's name) | Port |
| --- | --- | --- | --- |
| `0x0c` | SETUP MONSTER, 3 operands | SETUP MONSTERS, 4 (`3632:038f`; a failed `IF` still skips 3): the fourth the ambush's directions | ported (see Death Knights' encounter) |
| `0x1d` | PARTYSTRENGTH | PRESS KEY: a menu of "Press button or enter to continue" (`DS:088a`, `3632:1b5f`) | ported |
| `0x1f` | none (hangs) | JOURNAL N: "This is recorded as journal entry N." (`DS:2a79`), then PRESS KEY (`3632:279f`) | ported |
| `0x22` | PARTY SURPRISE | YES NO: a menu of Yes and No (`DS:31cb`), `=` for Yes (`3632:1b9e`) | ported |
| `0x23` | SURPRISE | CLEAR EXIT: the text window cleared, what changed shown again (`3c1e:1dd9`), `EXIT` (`3632:2859`) | ported |
| `0x27` | TREASURE, 8 | TREASURE ITEMS: coins, then a list (a header of 4) of `ITEM0`'s items (`3632:1933`) | ported (see Death Knights' treasure) |
| `0x29` | ENCOUNTER MENU, 14 | NOTHING (`3632:28c9`) | ported |
| `0x2c` | PARLAY, 6 | MUSIC N (`3632:2878`) | logged (`music:`), not played |
| `0x31` | SPRITE OFF | ASK MENU: "what do you do" (`DS:2bed`) where the text stands, then HORIZONTAL MENU (`3632:11fd` with 1) | ported |
| `0x3c` | PROTECTION (hangs) | STOCK: a shop's items from `ITEM0`'s lists (`3632:28d2`), then the shop | ported (see Death Knights' shops) |

An opcode that stops ends the run with `COK_ECL_UNPORTED` and "not yet
ported: Death Knights of Krynn's opcode 0xNN, NAME (dkk SEG:OFF)".
`CALL`'s redraw is at `2dcb` (`3632:2294`), Champions' `2e10`. Its
variables are bytes (`3c1e:0d85`, `3c1e:0bc1`; the descriptor's
`variable_bytes` 1): an operand of type 0 or 1 reads and writes one byte,
one of type 2 and up (3, `0x80`) a little-endian word over the address and
the next (`cok_ecl_value`, `cok_ecl_store_to` and `cok_ecl_store_as` for an
operand's address; `cok_ecl_store` stores a byte); a special address takes
the value whatever the width, and a word at a store's last address has no
byte past it (the original reads on into what follows). All 25 of its
blocks decode, 14,781 instructions, 0 `IF`s skipping by another length:
`ecldump --game death-knights` agrees instruction for instruction with the
planning's patched listing but for `0x27`'s fourth header operand (which
that listing printed from a three-operand copy, stale) and 12 instructions
after a CLEAR EXIT, which ends the block's walk.

Its named variables are bytes at Champions' addresses (the counterparts
of the routines that use them read and write them there, the `0x7c00`
ones through its table of offsets at `DS:836`-`864`), but for: the clock,
seven bytes (`5b7d:0523`, not words over two addresses); the sky's colours,
`0x4bfe` below `0x80` and `0x4bfd` from it (`6c84:00a6`); games saved,
`0x4cf6` (`4381:3106`); "off the overland map", `0x4cf7` not 1 (where
Champions' routines test `0x4c38` with "outside 3D areas" or mode 3; where
they test it alone, its code has no test); no status line turn (its `0x4cff`
is turning undead's, `5340:119b`) and no portrait; and the names only
combat, treasure, monsters, shops and spells use, left out until they are
ported. Its block start clears only the encounters while resting
(`3c1e:01c1`). Its special addresses (`3c1e:0d85`, `0bc1`): `0x4bea`,
`0x33d` and `0xc04b`-`0xc04f` as Champions', `0x4cf8` always 1 (so ECL1
block 35, the vault, jumps to its fight at `8355` as the original does),
no host values at `0xb1`, `0xfb`, `0xfc`, and stores only above 49000
(`0xc04b`-`0xc04d`, `0xc059`, `0xc05f`). Its character fields (`3c1e:073a`
reads, `0a9a` writes), in its 216-byte record:

| Address | Field (offset) | Read and written as |
| --- | --- | --- |
| `0x7c00`-`0x7c0f` | `NAME` text from element 1 (`+0x01`) | read as many bytes as `SIZE` (`+0x29`) says, a word from 2; any store marks the name cleared |
| `0x7c14`-`0x7c19`, `0x7c1a` | `SCORE` 0-5 (`+0x11`, by 2), `STRENGTH_EXCEPTIONAL_BASE` (`+0x1d`) | bytes, read |
| `0x7c1b`-`0x7c64` | `MEMORIZED`, 74, in the WIZ record (`+0xc2` points to it) | bytes; not ported (the field is left out: a character stops at its assertion) |
| `0x7c65`-`0x7c6a`, `0x7c6b` | `SCORE_BASE` 0-5 (`+0x10`), `STRENGTH_EXCEPTIONAL` (`+0x1c`) | bytes, read |
| `0x7c71`, `0x7c72`, `0x7c74`, `0x7c76` | `BASE_THAC0` (`+0x1f`), `RACE` (`+0x20`, signed), `AGE` (`+0x26`, a word), `MAX_HIT_POINTS` (`+0x28`) | read |
| `0x7c9a`-`0x7c9e`, `0x7c9f` | `SAVING_THROW` 0-4 (`+0x2a`), `MOVEMENT` (`+0xd7`) | bytes, read |
| `0x7ca5`-`0x7cac` | `THIEF_SKILL` 1-8 (`+0x32`) | bytes, read |
| `0x7cb8` | `NPC` (`+0x3e`) | a byte, written as it is |
| `0x7cbb`, `0x7cbc`, `0x7cbf` | `MAGIC_ORDER` (`+0x24`), `DEITY` (`+0x23`, signed), `KNIGHT_ORDER` (`+0x22`) | read |
| `0x7cc3`-`0x7cc8` | `COINS` 4-6 (`+0x3f`), each at two addresses | words |
| `0x7cd3`, `0x7cd6`, `0x7cd8` | `MAGIC_RESISTANCE` (`+0xc1`), `GENDER`, `ALIGNMENT` (`+0x55`, `+0x56`, signed) | read |
| `0x7ceb` | `CLASS_LEVEL` above 0 as bits: classes 5, 0, 6, 2, 7, -, 3, 4 (`class_bits`) | read |
| `0x7d00` | `ACTIVE` (`+0xc8`) | read as 1, or 0x80 if it cannot act; writing 1 sets `STATUS` (`+0xc7`) 0 and `ACTIVE` 1 |
| `0x7d0c` | `HOSTILE`, `COMPUTER` (`+0xc9`, `+0xca`) | written as Champions' (not read) |
| `0x7d0e`-`0x7d10` | `THAC0`, `AC`, `AC_BEHIND` (`+0xcb`-`+0xcd`) | bytes, read |
| `0x7d19`, `0x7d1a` | `HIT_POINTS` (`+0xd6`), `MOVEMENT` (`+0xd7`) | a byte; read |
| `0x7eb1`, `0x7eb4` | the host's | |
| `0x7f12` | the ECL file | written (picks the next block's file); read as the variable |

Each handler that keeps its meaning, against Champions' (`dkk 3632`'s
against `2fd3`'s; the VM's own as `ecl.c` has Champions'):

| Opcode | dkk | Against Champions' | Port |
| --- | --- | --- | --- |
| EXIT, GOTO, GOSUB, COMPARE, RANDOM, SAVE, RETURN, the IFs, ON GOTO, ON GOSUB, PRINT, PRINTCLEAR, PRINT RETURN, ECL CLOCK, CLEAR BOX, DUMP | `0000`, `009b`, `00b8`, `00cd`, `01fb`, `025d`, `0b99`, `0cdc`, `189e`, `0afd`, `202d`, `1ff8`, `2056`, `2487` | the same (EXIT's frame redrawn is the game's: step 2) | as Champions' |
| ADD, SUBTRAT, DIVIDE, MULTIPLY, AND, OR, INPUT NUMBER, INPUT STRING | `014a`, `0f3f`, `0a3b`, `0a85` | stored as wide as the destination's type; `DIVIDE`'s remainder a byte at `0x7f3f` | named variables, byte variables |
| GETTABLE | `0fb2` | read as wide as the third operand's type | ported |
| SAVE TABLE | `1018` | stored as wide as the value's (first) operand's type | ported (D1 took the second's) |
| COMPARE AND | `0bed` | a pair compared as strings where its first is one | `compare_and_strings` |
| VERTICAL MENU | `1068` | the same, but the store's width | as Champions' |
| HORIZONTAL MENU | `11fd` (0) | no single item's case (white, Enter, respelled), colour 10, the picture animated whenever shown | `menu_single_item` |
| PICTURE | `096c` | no portrait (`0x7ee1`), no `DS:884b`; overland map 0x70 | has the variable not; `view.overland_picture` |
| DELAY | `1c90` | 600 milliseconds, not the speed's | `delay_ms` |
| NEWECL, APPROACH, CLEARMONSTERS, LOAD FILES, LOAD PIECES, CALL | `0d52`, `0916`, `1376`, `0daa`, `2294` | variables bytes; CLEARMONSTERS clears 12 bytes of its table (`DS:be58`), not 28; the wall slots, `CALL`'s redraw (D1); `CALL`'s sound reads `DS:db5a`, which no ECL address stores | as Champions' |
| LOAD CHARACTER, CHECKPARTY, ROB, FIND ITEM, WHO, ADD NPC, FIND SPECIAL, DESTROY ITEMS | `02af`, `1452`, `1a76`, `1bdd`, `20a9`, `210f`, `24b6`, `250a` | the record's offsets only (the layout's) | as Champions' (no party: nothing) |
| SPELL | `2177` | the memorized spells in the WIZ record, 74 (Champions' 58), of those who can act; no trace | as Champions' without a party; a character stops at `MEMORIZED` |
| LOAD MONSTER | `0472` | the record's, the difficulty a byte, other monster files | ported (see Death Knights' encounter) |
| COMBAT | `16ec` | a branch on `0x4d00` first (`3948:000e`, the bank vault's menu: see Party), then the shop, the temple; other routines | ported (the shop and the temple: see Death Knights' shops) |
| DAMAGE | `1ca2` | a save's bonus bits 0-3 (Champions' 0-4: bit 4 counted), half the damage for a save made with bit 4 (Champions' all of it), its key `DS:0868`; its saving throw (`638c:10f8`) | the treasure section's; the saving throw ported (see Death Knights' fight) |
| PROGRAM | `23dd` | its start menu (`49cb:010b`, see The start menu); after it the screen redrawn on the map unless the menu ended the game (`DS:8c5b`), and the party marked on the map (`6997:0000`); its camp (`237e`) | `start.program_marks`; the game ended, `new_game` |
| ADD EP | `25f1` | only those who can act, before the division; experience at `+0x62`; a level flag (`65cd:38c7`); its words (`DS:1485`, `2a64`, `2a3b`) | the treasure section's |

Where the original would misbehave, the port stops with a status instead:
opcode `0x1f` and opcodes past `ADD EP` have no handler in the dispatch
(`2fd3:386d`), and `PROTECTION`'s (`2fd3:3516`) returns at once, so none of
them advances and the original runs it again for ever;
division by zero is Turbo Pascal runtime error 200, and a failed `NEWECL` load
retries forever. Code addresses outside the buffer, more than 64 operands or
32 string operands, and `GOSUB` nesting past 256 are also errors. Variable
strings stop at 255 characters or the end of their store, and reads the
original leaves unset return 0.

## Playing scripts

`eclplay [options] ASSETS BLOCK` runs an ECL block on a 320×200 adventure
screen without a display, as `2fd3:3b47` enters a block: the load vector,
then the after-move and location vectors, starting over when `NEWECL`
switches blocks; `--game NAME` names the game whose files ASSETS holds
(see Games): with `death-knights` it runs Death Knights' blocks (their
songs logged as `music: N`, unplayed; an opcode or a read not ported yet
ends the run, saying which), not its title, start menu or saved games. It prints text as it is printed (`print:`), menus with
their `~` marks (`menu:`, `list:` and `item:`), the choices made
(`choice:`) and input read (`input:`), and the name and operand values of
each opcode that is not ported, in brackets. A spell effect the port cannot
carry out ends the run with its reason (see Spell effects). `--play` then
runs the adventure loop (see Adventure loop), which adds the party's square
and facing after each step or turn (`at: X,Y,DIR`), its place on the
overland map after each step there (`overland: X,Y,DIR`), the overhead map
going on and off (`area: on`, `area: off`; see Overhead map) and the
commands that are not ported (`unported:`); `--helm` plays as a game
started with `Helm`, which lifts `Area`'s `Not Here` and enables the Gods
cheat; `--combat gods` implies it. `--keys` types keys (`\r` Enter, `\e`
Escape, `\b` Backspace, `\<`, `\>`, `\^` and `\v` the arrows, `\@X` Alt and
letter `X`, `\cX` Ctrl and letter `X` (`\cD`, `\cZ`, `\cC` the debug keys,
see Debug mode), `\#HH` the extended key of scan code `HH`, in hex: `\@q`
and `\#10` are Alt-Q; and the keypad's keys as the BIOS takes them,
`cok_keymap_event`: `\n` the NumLock key, turning NumLock off and on again
and typing nothing, `\dC` the keypad's key `C` (`0`-`9`, `.`), its digit
with NumLock on and else the cursor key's code (nothing for 5), `\aN.` Alt
held over the keypad's digits `N`, typing their character code as a byte on
Alt's release, nothing for 0; `cok` presses those keys through SDL's
events, outside a group typed ahead, where they are typed as their codes);
when they run out the run stops. `\k` presses the next key while the party
rests, which the rest loop sees (`1614:03c2`), or as a battle is set up,
which drops it (`1614:0479`). Keys are otherwise typed one at a time as the
game reads them, but `\[KEYS\]` types KEYS ahead: all at once into the
keyboard's buffer (15 keystrokes; more are lost), at the game's next wait
(a delay or a poll of `KeyPressed`) or key read, whichever comes first,
where they wait to be read or are thrown away by the game's flushes
(`1614:045c`, see Playing), each flush that drops some logged as `flush: N
keys typed ahead dropped`; a group typed ahead makes `KeyPressed` true
while it lasts. `\wN.` holds back what follows until N more of the game's
waits have passed (a key read ends it at once), so that a key arrives in
the middle of a wait: the title waits polling and sleeping each hundredth,
two waits a hundredth (`\w600.\[x\]` ends it a second into its second
picture), and a rest polls once a step. From startup the first wait is the
note to Tandy users' `Delay(2000)` (see Startup and the setup), where a
group typed ahead at the start arrives, for the title's flush to drop.
`eclplay` has no delays; it counts the calls to the delay hook as the
window's front end does, and passes over `cok`'s `\pN.` (a pause in real
time). `--saves DIR` is where Save writes and the roster is kept (see The
start menu); without it, saving logs an error. `--import DIR` is where a
game that imports another's characters finds them (Death Knights' Add and
Load from Champions), `--saves` without it. `--effect
M,ID,MINUTES,VALUE` (hex, repeatable) gives party member M (1 the first) an
effect, its handler run when it is removed, as a saved game's `.SFX` would
hold it, for tests of the timers and of effects in a battle. A spell list
logs its rows as `item:` and `heading:`, and quitting to DOS ends the run
with `(quit to DOS in block N)`. Cast and View run from the commands and
the camp, their lists logged as `list:` and `item:`. Monsters loaded, the
encounter's sprite and close-up and the money robbed log as `monster:`, a
battle's setup, where each combatant stands, and the end of a fight as
`combat:` (see Monsters and encounters and The battlefield), each round's
order of turns and each turn as `round:` and `turn:`, and effects ending in
a battle as `effect:` (see The rounds), the lines of an attack not said in
the side panel as `attack:` (see Attacks, movement and death), each cast in
a battle as `cast:` (see Spells in combat), the coins and items `TREASURE`
adds as `treasure:` (see Treasure and the end of combat), and what is paid
and appraised in shops and the temple as `shop:` (see Shops and the
temple), the characters `ADD NPC` adds and `DUMP` removes as `party:` (see
Party), `CALL [b203]`'s sounds as `sound:` and `CALL [c01e]`'s step as
`at:`, and the debug mode's switching, traces and View as `debug:` and the
lines it writes to its file as `debug.txt:` (see Debug mode). `COMBAT`'s
battle runs its rounds, the computer playing the monsters and the party's
NPCs (see The computer's turns) and the player's turns reading the keys
(see The player's commands), or, without `--keys`, passing, unless
`--combat won`, `fled` or `lost` decides it instead, `gods` has the
player's turns start with Alt-X, the original's cheat (see The rounds),
`melee` has every turn attack the nearest enemy it can (see Attacks,
movement and death), `auto` has the player press Alt-Q, the computer then
playing every turn, or `pass` has every turn pass, the computer's too. Keys
that run out in a battle end the run there, `(out of keys in block N at
ADDR)`, as anywhere else: the battle's end and its treasure are not
reached. `--combat-map FILE` writes each battle's map and combatants to
`FILE` (see The battlefield). A fight ends with the results and the
treasure menu, which read keys; with no party at all the monsters rejoice
and the run ends. `--test-party N` adds N (1-8) characters made up for
testing, as a saved game's are added: a fighter, a cleric of Mishakal, a
White mage and a thief in turn, human, of level 1, their weapons and armour
readied, 20 steel each, the cleric's Cure Light Wounds and the mage's
Detect Magic memorized, one cell in combat (`SIZE`, `+0xcf`, 1), their stats
computed as a loaded character's are; the seventh, GWEN, has Charm Monsters
memorized too and a Wand of Magic Missiles readied with 5 charges; the
eighth, HOLT, is a mage too, with Stinking Cloud memorized instead.
`--party SAVE` adds the characters of a saved game to the party (see
Party); `--character FILE`, up to 8 times, adds the character whose record
is in `FILE` (a saved game's `CHRDATA1.SAV`, the roster's `BOB.WHO`,
Death Knights' `CHRDATA1.GAM` or `BOB.PC`), with its other files beside
it, after `--party`'s, as they are added (`cok_adventure_add_characters`:
read, joined, counted, their icons loaded, the first selected); unlike
`--party` and `--load` it works for a game not yet playable. `--load SAVE`
loads the whole saved game first; then `BLOCK`
may be left out to resume where it was saved, as `2fd3:3c28` starts the
game, running only that block's load vector before the first command (see
Adventure loop). A block named is entered at once, a test entry and not the
original's resume: its load, after-move and location vectors run, the first
at the stack depth of `2fd3:3c28`'s own, as `2fd3:3b47` runs them, which
also makes the mode the last mode; with `--load` too, the saved block's
after-move and location vectors run before any command, as the original's
resume never does. `--at` with `--load` and no block places the party, the
square's byte (`DS:6d89`) read from the map there, before the load vector
runs. WHO prints the character
picked (`who:`). `--shots DIR` saves `DIR/NNN.bmp` each time the game waits
for a key and once each battle is set up, `--screen FILE` the final screen,
in the display mode `KRYNN.CFG`'s first line names or `--display ega`,
`tandy` or `cga` (CGA in its four colours; see Display modes). `--start
ADDR` runs from a code address instead, `--vector N` one vector, `--at
X,Y,DIR` places the party, `--set ADDR=VALUE` sets variables (in hex),
`--file N` picks the ECL file (by default the first that holds the block),
`--still` loads only the first frame of each picture, `--seed N` starts
Turbo Pascal's `Random` from N (in decimal; by default 0), and `--trace`
lists each instruction. `--start-menu ASSETS`, with no block, plays the
game as `START.EXE` does, from startup: the setup if there is no
`KRYNN.CFG` (in `--config FILE`, `--saves DIR` or ASSETS; it writes it in
the first of the two given, see Startup and the setup) and the note to
Tandy users, logged as `crt:` rows and saved by `--shots` at 640×200, then
the title (a key pressed with `\k` while it waits ends it), its menu, where
`\t` lets the time run out and plays the demonstration, then the start menu
and the adventure, game after game (see The start menu); `--woof` starts it
as `START Woof` does, without the title, and in any run lets the debug keys
work (see Debug mode). Characters, the roster and saved games go in
`--saves DIR`. The run ends `(out of keys in the setup)`, `(out of keys at
the title)`, `(out of keys in the start menu)` or as other runs do. `--wav
FILE` renders the sound to FILE as the game's time passes (see Sound), on
the device `KRYNN.CFG` names, or `--sound tandy`, `speaker` or `off` (which
need `--wav`); a run that does not start from the title installs the
effects driver first, as main has by then (`1000:0160`); `--sound-calls`
prints the game's calls to the sound driver as `call:` lines among the
rest. `--mouse` and `--joystick` give the machine a mouse driver and a
joystick, which `\m` and `\j` in the keys click and move, and `--input
mouse`, `joystick` or `none` picks in place of `KRYNN.CFG`'s third line
(see The mouse and the joystick). For example, `./build/eclplay --start 899b --keys '\r2\r' COKAssets 48`
answers the guards at the gates of Gargath with the second item of a list.

`src/adventure.h` holds the screen side of the opcodes, from overlay `2fd3`:

| Opcode | Handler | Port |
| --- | --- | --- |
| `PRINT`, `PRINTCLEAR` | `2fd3:0acf` | `cok_text_wrap` in cells 1-38 by 17-22, colour 10, typed, leaving the text delay off; a number prints in decimal |
| `HORIZONTAL MENU` | `2fd3:116d` | `cok_menu_horizontal`, colours 15 and 10, or all 15 for one item |
| `VERTICAL MENU` | `2fd3:0f9d` | prompt as `PRINTCLEAR` (typed only while the text delay is on), then `cok_menu_list` below it |
| `INPUT NUMBER`, `INPUT STRING` | `2fd3:0a11`, `2fd3:0a57` | `cok_text_number`; `cok_text_input`, an empty line stored as a space |
| `PICTURE` | `2fd3:0914` | below 0x70, `PIC<file>.DAX` at cell 3, 3; else `BIGPIC<file>.DAX` at 1, 1 in its frame; 0xff restores the view |
| `CLEAR BOX` | `2fd3:3063` | the adventure frame (`1128:0242`) and the first picture frame |
| `DELAY` | `2fd3:2c33` | the `delay` hook, speed × 100 ms (`521:0b4b`) |
| `LOAD FILES`, `LOAD PIECES` | `2fd3:0cf4` | the map and wall sets of the 3D view (see below) |
| `LOAD CHARACTER` | `2fd3:02e9` | select a character by position until the script exits (see Party) |
| `ADD EP` | `2fd3:36dc` | experience for the selected character or the party |
| `WHO` | `2fd3:30b6` | pick the selected character from the party list |
| `DAMAGE` | `2fd3:2c80` | damage by attacks or saving throws, which can kill the party |
| `CALL` | `2fd3:329b` | a routine by its address: the view after an encounter, a sound, a step forward, the wall ahead, a frame of the small picture (see below) |
| `ADD NPC`, `DUMP` | `2fd3:311c`, `2fd3:351b` | a monster joins the party as an NPC; the selected character leaves it (see Party) |
| `FIND ITEM`, `FIND SPECIAL` | `2fd3:2b7e`, `2fd3:354f` | whether any record has an item of a type, or the selected character an effect |
| `SPELL` | `2fd3:318b` | which character has a spell memorized, and where |
| `ECL CLOCK` | `2fd3:3005` | passes the time (`57e4:0549`), count first, then the unit |

A one-item menu reading `PRESS BUTTON OR RETURN TO CONTINUE.` is shown as
`PRESS <ENTER>/<RETURN> TO CONTINUE`, with no full stop (the 34 characters
of `2fd3:1146`), and Enter picks it, as in the original. Menu items are
joined as `~ITEM` separated by spaces, cut to 50
characters. Small pictures are delta collections whose groups after the first
are animation frames, each preceded by its delay, which the menus that
animate the picture (`67b5:03e2`) count in tenths of a second (see
Playing);
with animation off (`DS:4b4f`) only the first is loaded. A picture
loaded stays loaded for the same name (`PIC` or `SPRIT`) and record
(`DS:6dee`, `6e00`), whatever the ECL file, until the slot is freed.
With animation on, loading one (`6961:00e4`, pictures and sprites alike)
clears row 24 and shows `Loading...Please Wait` in light green while it
reads, and clears the row after the keyboard's flush, so that whatever
row 24 held is gone; with it off row 24 is left alone. A record that is
not there says `PIC not found` on row 24 in yellow and waits for a key
(`1521:096c`, as `head not found`), with no flush or clearing after; the
slot is left empty but holds that name and record, so loading it again
does nothing. A file that is not there stops the port, where the
original asks for it (`Couldn't find PIC1.dax. Check install.` on row 24,
`169c:02a7`, with `DS:5884` 0 outside the monster loader) and waits for
a key and looks again until it is there. The
game speed
(`DS:4b38`) defaults to 4. Big picture 0x79, the overland map, marks the
party on it (`4877:0005`) and does not count as a big picture shown (see
The overland map).

With var `0x7ee1` not 0xff, `PICTURE` shows a portrait instead
(`3775:0538`), as the close-up of `SETUP MONSTER` does: the head of
`0x7ee1` over a body of the picture's number. The shops of ECL2 block 50
and ECL3 block 80 show their shopkeepers so (heads 68 and 69 over body
68), and in 3D areas a shop's or the temple's redraw shows the portrait
last shown again (`DS:4b55`, `4b56`; see Shops and the temple). The view
is not marked as replaced (`DS:884b`). `6961:05b9` loads the head from
`HEAD<file>.DAX` and the body from `BODY<file>.DAX` (`DS:6de5`, `6dea`),
opaque, the file's number cut to one digit, unless the record wanted is
the one held (`DS:6de4`, `6de9`) or 0xff; `6961:06bd` then draws the head
at cell 3, 3 and the body at 3, 8 (`6961:000a` with 0, `127f:10e7`:
straight to the screen, unclipped), nothing for a piece not loaded. A
record that is not there leaves no piece and says `head not found`, for
the body too, on row 24 in yellow, and waits for a key; it counts as held.
None is held at startup (0xff, `3e99:0477`), and each step (`475c:0e77`)
and combat setup (`3cb2:1c58`) free the pieces and hold none again. A file
that is not there makes the original ask for its disk, and the run fails:
the game has no `HEAD1.DAX` or `BODY1.DAX`, but no script of ECL1 sets
`0x7ee1`, and its shops and temples all open outside 3D areas, so only a
script made up or a run such as `eclplay --set 4be6=1` at one of them
reaches it. The port keeps these quirks:

- The portrait last shown is head 0 over body 0 until one is shown: a shop
  or temple redrawn in a 3D area before any portrait says `head not found`
  twice (the shipped scripts show their shopkeeper first).
- A piece held stays held whatever the ECL file: after a `NEWECL` to
  another file the same head shows until a step.

The screen's redraw for the mode (`6346:2c17`), whose other modes are
described with them (the adventure, camp, shops, treasure), draws in the
party menu (mode 0) the cleared frame of `1128:0000` alone, and nothing in
combat (mode 5).

`CALL` takes its operand's address whatever its type (`3775:06cc`), and
less 0x7fff picks:

- 0x2e10, which scripts call after an encounter (see Monsters and
  encounters);
- 0xb203, a sound: 11 (`DS:1e5e`) if `0x3de` (`DS:8834`, which only
  scripts write) holds 10, else 10 (`DS:1e5c`); scripts set it to 5;
- 0xc01e, a step forward (`3775:1bed`): the party's square moves a square
  the way it faces, wrapping within 0-15 (a step down from 0 or below
  gives 15, a step up from 15 or above gives 0, as signed bytes), not at
  all for a facing other than 0, 2, 4 and 6; then the square byte and the
  wall ahead are worked out again and the view marked as changed
  (`DS:8850`), so that the next `CALL [2e10]` draws it. For another facing, which only the overland map or a
  saved game gives, the wall ahead (`69ea:06a2`) is its unset local
  `[bp-1]`: the byte that `1bed`'s call to `69ea:07a5` pushed, AX's high
  byte, which still holds the address less 0x7fff, so 0x40; the emulator
  confirms it with any stack. Off the map of a block with no squares there
  (`DS:8846` 0 or 0x50) it is 0 first. ECL2 block 48 and ECL3 blocks 97
  and 99 use it.
- 0xc018, the wall ahead, outside 3D areas only; there, for a facing
  other than 0, 2, 4 and 6, the unset byte is one `3775:0032` left on the
  stack, which depends on the opcodes before, and the port stops with
  `COK_ECL_UNDEFINED`. No script uses it.
- 0x6803 draws the small picture's current frame (`DS:6da3`) at cell 3,
  3 (`6961:000a`), moves to the next, back to the first after the last,
  and waits speed × 100 ms (`1521:0b4b`). ECL2 block 57 animates a
  close-up and a picture so. Loading a picture starts from its first
  frame, but `PICTURE` of the one loaded keeps the current frame
  (`6961:00e4` returns at once, as it goes by the name and record, not
  the ECL file), though it draws the first. Where the slot was freed
  (`6961:0537`, as at startup, each step and combat setup) and nothing
  loaded since, or a record was not there, the frame is 0, and entry 0
  is the far pointer `DS:6da2`:`6da0`: segment 0 (the count and frame)
  and as offset the segment of the sky picture loaded at startup outside
  CGA mode (`DS:6d9e`), so the original draws from low memory; the port
  stops with `COK_ECL_UNDEFINED`. The menus that animate the small
  picture also move the frame while they wait (see Playing).
- any other address, nothing.

`FIND ITEM type` (`2fd3:2b7e`) sets the comparison flags to `=` if any
record in the list has an item of that type (`+0x2e`), else `<>`, the
others false; ECL2 block 67 asks for its tomb's sword (type 63) so, and
ECL3 block 98 for another. `FIND SPECIAL id` (`2fd3:354f`) does the same
for an effect of the selected character (`6346:2447`); with none selected
the original reads through NULL and the port stops; no script uses it.
`SPELL spell where who` (`2fd3:318b`) stores, for the first record in the
list with the spell among its 58 spell bytes (`MEMORIZED`, `+0x1e`-`+0x57`),
the byte's index in `where` and the record's position in `who`; with none,
0xff and the position of the last record (0 for none). ECL1 block 32's trap
asks for Find Traps (22) and forgets it. The trace it then prints
(`6d7e:093e`, `spell`, the index, the position and the spell) shows only
with the debug flag (see Debug mode). `ECL CLOCK count unit` (`2fd3:3005`) passes the time
(`57e4:0549`); no script uses it.

A differential test ran the original's handlers in an 8086 emulator
against the port: `FIND ITEM`, `ECL CLOCK`, `ADD NPC` (with `4b6d:18fc`,
`161b`, `1989`, `66c2:0433` and `6346:0d20` as they are), `SPELL`, `DUMP`
(with `4def:3b0a`), `FIND SPECIAL` and `CALL` at 0xb203, 0xc01e, 0xc018,
0x6803 and other addresses, on random parties of up to twelve monster
records with their items and effects, random selections, party sizes,
squares, facings, maps and flags, comparing every record, item and effect
in the list, the selection, `DS:43bf`, `0x7f3e`, the flags, the square,
the wall ahead, the square byte, the view's flag, the variables stored,
the clock, the sounds, delays and icon slots loaded and freed; `CALL
[6803]` one to six times after a `PIC` or `SPRIT` record of any file was
loaded (`6961:00e4`, animation on or off) from a random frame, in a
third of the pictures' cases after `PICTURE` of the same, half of those
with another ECL file picked, comparing
the frame, the screen (noise before) and the delays; and the portrait
(`3775:0538`) after up to three portraits or steps' frees, the file
changing between, or the shop's redraw of it (`6346:2c17` in mode 1 in a
3D area), comparing the records held, the portrait last shown, the screen
above row 24 and the prompts, with the picture routines of `127f` run as
they are in Tandy mode. Of 12,000 cases, the 10,607 the port carries out
agreed: 6,332 opcodes (1,396 `DUMP`, 1,342 `CALL`, 818 `ADD NPC`, 755
`ECL CLOCK`, 742 `FIND ITEM`, 689 `SPELL`, 590 `FIND SPECIAL`), 2,405
frames of pictures and sprites and 1,870 portraits and redraws. Both
refused 1,218: monster ids not in the file, which quit to DOS (610),
portraits of ECL1, which has no `HEAD1.DAX` or `BODY1.DAX` (500), and
`CALL [6803]` with no picture loaded, which draws from low memory (108).
The port alone refused 175: `FIND SPECIAL` with none selected (121) and
`ADD NPC` with the party full and none selected (17), which read and
write through NULL, and `CALL [c018]` facing other than a side (37),
whose byte of the stack the emulator, which clears it, cannot know. Running `CALL [c018]` and
`[c01e]` with `3775:0032` decoding the operand and the stack filled with
any byte showed that byte for `c018` and 0x40 for `c01e`. Four
mutations of the port (the wall after a step, a frame's wrap, the body's
row and `SPELL`'s position) each made from 20 to 300 of 3,000 cases
differ; `PICTURE` starting the frames over, as the port did before, 15
of 2,000, and reloading a picture for another ECL file 28 of 3,000. It is not part of the repository.

## Debug mode

The original has a debug mode, `src/debug.h` in the port. Its flag,
`DS:4b51`, clear at startup (`3e99:055b`), is toggled by Ctrl-D, which the
key reader (`1614:025b`, which every key the game reads goes through but
text input's and the flushes', see Playing) takes, with Ctrl-Z and
Ctrl-C, only when the first argument is `Woof` (`ParamStr(1)` against
`1614:024c`, exact case): `START Woof`, or `START Woof Helm` with the
Gods' cheat too; `cok --woof` and `eclplay --woof`. No other argument or
build is involved; the readers of the flag are in every run, but nothing
else sets it. The key is passed on as read in every case, so a menu reads
it too; a scan code of 3 (Ctrl-2's) is Ctrl-C as well.

| Key | What it does |
| --- | --- |
| Ctrl-D (4) | The flag on: `DEBUG.TXT` assigned and made anew (`1a46:04f7`, `056c`); off: closed (`05c1`) |
| Ctrl-Z (0x1a) | With the flag set, for each record of the list (`DS:609a`) and each of its effects (`+0xe3`, linked at `+5`), the line `who: NAME  sp#: ID` (`1614:007b`) |
| Ctrl-C (3) | Quit to DOS (`1614:0000`): the sound stopped, `DEBUG.TXT` closed if the flag is set, `Halt`; not DOS's break, which Crt's `ReadKey` does not see |

`DEBUG.TXT` (the Text file at `DS:8864`, which the game names
`debug.txt` and DOS upper-cases, as the port names the saved games) is
written as Turbo Pascal
writes a text file on a disk, checked in the emulator: lines end CR LF and
go into its 128-byte buffer, written out when it fills and when the file
is closed; the game reads no I/O result (`{$I-}`): where the file cannot
be made (`C:\COK` read-only in DOSBox-X, `e_debug_readonly.py`) Turbo
Pascal's `InOutRes` holds 2, then 103 once Ctrl-D closes it, and nothing
is skipped for it: pictures load and a Save writes every file, with no
runtime error, as in the port. The end of a game
(`3e99:0977`, `3e99:0843`) clears the flag and leaves the file open, so
that what its buffer holds is never written and the next Ctrl-D makes it
anew. The original writes it in the current directory; the port in the
saves directory (`--saves`), or nowhere without one (`eclplay` without
`--saves`; the file counts as open all the same), and logs each line as
`debug.txt:`; Ctrl-D logs `debug: on` and `debug: off`, Ctrl-C `quit: to
DOS`. In `--keys`, `\cD`, `\cZ` and `\cC` type Ctrl-D, Ctrl-Z and
Ctrl-C (`\cX` is Ctrl and any letter, 1-26; the raw bytes 4, 0x1a and 3
do as well), which `cok` sends as Ctrl and the letter. Besides Ctrl-Z's lines, loading a tile set
(`6e22:0050`; the wall sets' from `LOAD FILES` and `LOAD PIECES`, slots
1-3) writes `slotnum = N`. At the end of a run the port closes the file
if the flag is set, as quitting would.

While the flag is set:

- The trace (`6d7e:093e`, `cok_debug_trace`): row 0 cleared (the frame's
  top and its moons), three numbers drawn there as `A , B , C` in yellow,
  a text (cut to 40) on row 24 in yellow (`1521:096c`), a key read
  (through `1614:025b`, so Ctrl-D there turns the mode off), and row 0
  cleared again; logged as `debug: A , B , C | TEXT`. The ECL loop
  (`2fd3:3add`) traces each opcode once its handler returns, with zeros,
  by the name `6d7e:024f` gives (the opcode last fetched, `DS:72e7`: a
  nested run's, or the one a failed `IF` skipped; the trace table's names,
  `IF = ` with a space after it, and `BULLSHIT N` for opcodes it does not
  name, which hang before they are traced); `SPELL` traces `spell` with
  the index, the position and the spell (`2fd3:3292`); a battle's setup
  traces each record of the list by name with its status (`+0x188`
  `STATUS`) and whether it can act (`+0x189` `ACTIVE`) (`3cb2:1e13`); effect
  0x07's handler traces `bad news` first (`3f44:0226`). In the demonstration
  the trace's key read returns at once unless a key is waiting, as every key
  read there does.
- The status line has `*` after the time (`6346:2f4c`), and the combat
  menu `*` after `Done` (`3995:0aa5`).
- Items offers `View` first (`View Ready`, `546c:187b`): with an item
  picked, `546c:12be` clears the frame (`1128:0000`) and lists in light
  green, from row 1, `itemptr` (its type, `+0x2e`), `namenum(1)`-`(3)`
  (`+0x2f`-`+0x31`), `plus`, `plussave`, `ready`, `identified`,
  `cursed` (`+0x32`-`+0x36`), `value` (the word at `+0x3a`),
  `special(1)`-`(3)` (`+0x3c`-`+0x3e`), and from its type's row of
  `DS:5886` `missile type` (byte 14), `max range` (12) and `item type`
  (0), their values at column 20, then `press a key` on row 24; the list
  and its header are drawn again after. A type above 128 is read past
  the table (from `DS:6096`); the port stops (`COK_ECL_UNDEFINED`).
  `View` makes the menu longer: in a shop it can fill the 40 characters,
  leaving no room for `Exit` (Escape still leaves).

A differential test ran the original routines in the 8086 emulator
against the port (the draws compared as the port's primitives draw them,
the files byte for byte): `1614:025b`'s keys with the real Turbo Pascal
file routines and `ParamStr` on a PSP (with `Woof`, `Woof Helm`, `woof`,
`WOOF`, `Woofy`, none and others; Ctrl-D, Ctrl-Z, Ctrl-C, other keys and
the end of a game's reset, random parties and effects, files past one
buffer), the trace with the flag on and off, `546c:12be` on random items,
the status line `6346:2d75` and the combat menu's text (`3995:08d5`) with
the flag on and off, the Items menu `546c:17f9` with View, `SPELL`
(`2fd3:318b`) with its trace and the ECL loop's after it, and
`6d7e:024f`'s name for every opcode: 3,000 random cases of each and the
256 names, all agreeing (among them 1,467 Ctrl-Ds turning the flag on
and 719 off, 683 Ctrl-Zs writing, 318 Ctrl-Cs quitting, 1,126 resets, 181
files past one buffer; 2,254 traces with the flag set; 1,514 Views).

## 3D view

`src/view.h` ports the 3D view from overlay `69ea`, drawn with the 8×8 tile
routine of overlay `6e22`. `cok_adventure_view` shows it as `6945:00ba`
does: when `PICTURE 0xff` restores the view, when a block is entered by
`NEWECL`, after its load vector, if it loaded files or the area stays in 3D
(`2fd3:3b47`), when a loaded game begins (see Adventure loop), and after
each step or turn of the adventure loop;
`eclplay --at X,Y,DIR` places the party before a block runs. Where the area
has no 3D view (`0x4be6` and `0x4c38` both 0), `6945:00ba` shows the big
picture in its frame instead.

The tile sets, the wall slots and the overhead map's tiles are the
descriptor's `view` (see Games); the values below are Champions', and
Death Knights' differ after them.

`LOAD FILES` (opcode 0x21) loads a map: if its first operand is not 0xff or
0x7f and the area is 3D (`0x4be6`), record N of `GEO<file>.DAX` (`69ea:130d`),
which also sets `0x4bc5`. `LOAD PIECES` loads wall sets from
`WALLDEF<file>.DAX` (`69ea:1025`) into slots 1-3: all three operands, 0xff
leaving a slot empty, when `0x4be7` or `0x4be8` is 0; otherwise only the
first and third, so that a record of two sets can fill slots 1 and 2.
Scripts set those words before loading such a record; a block entered
without them (66 or 97, alone) names a record that does not exist. The
original halts when a file is missing or does not fit; the run then stops
with `COK_ECL_LOAD_FAILED` and the error. With an operand of 0x7f, it loads
record 0 into slot 1, which no shipped script does.

A GEO record is two bytes, then four tables of 256 bytes, one per square of
the 16×16 map, row by row, north up. The first holds the wall type 0-15 on
each square's north side in its high nibble and east in its low nibble; the
second south and west. The third is a byte per square, which picks the sky
colour: below 0x80 the area word `0x4bfd`, otherwise `0x4bfe`, through the
table at `DS:0dc4`. The fourth holds two bits per side (west highest), not
used by the view: they say how the party may pass a side that has a wall
(see Adventure loop). Off the map, squares read from the opposite edge unless
the block is 0 or 0x50 (`DS:8846`); then they have no walls.

A WALLDEF record holds one or two sets of five wall types, 156 bytes each:
wall types 1-5 use slot 1, 6-10 slot 2 and 11-15 slot 3. Each type lists, row
by row, the tile values for the ten places a wall can be seen at
(`DS:0df4`-`0e38`): front walls one, two and three squares ahead, the side
walls of those squares, and the edge between two front walls three squares
ahead. Records name their tiles as tile set 1; values from 0x2d are moved up
by the first slot's offset from tile set 1 (`DS:1ec2`, worked out once at
`69ea:1124`), the same for every set of the record (`69ea:1230`), adding
bytes that wrap: a record of two sets already names its second set's tiles
past set 1's, so Throtl's (`WALLDEF1.DAX` record 1, loaded into slot 1) is
not moved at all. Each slot's tile set is record N of `8X8D<file>.DAX`, or
N × 10 + 1 and N × 10 + 2 for a record of two sets. Tile sets 0 (`8X8D1.DAX`
record 203) and 4 (the frame's) are loaded at startup.

Death Knights (`--game death-knights`) chooses a wall set's files by its
id, not by the ECL file (`dkk 6d21:0ec5`; the descriptor's `wall_files`):
`WALLDEF<file>` record id, its record of three sets of 0x30c from slot 0
(`LOAD PIECES`' two-set path loads slots 0 and 3, `3632:0daa`; slot n is
wall set n, types 5n + 1 to 5n + 5), no tile value moved; `8X8D<file>`
record id, 256 tiles (its header counts 0xff: `123c:1c58`), into the slot's
tile set (`70eb:002f`, tile set 0's file by the id), from which the view
draws every value 1-0xff (its sets' bounds `DS:4404`; 0x100-0x12c are the
frame's); and `BACK<file>` record id, the backdrop it shows on squares of
0x80 and up (`6d21:0e4e`). Its backdrop (`6d21:0177`) has Champions' fills
(no CGA split) and no sky pictures: on squares below 0x80 the ground by
day, `SKYGRND` record 2, and past hour 13 the night sky, record 1, over it;
else the wall set's `BACK`; all three drawn opaque at the view's top-left
(loaded without a mask, `DS:42d0`'s 0). Its overhead map's squares are
0x119 + walls and its arrow 0x129 + facing / 2 (`6d21:0000`), and its view
is drawn as Champions' (`6d21:0691` is `69ea:0820`, its places `DS:06c6`
Champions' `DS:0df4`).

`cok_view_draw` follows `69ea:0820`: it fills the backdrop, then draws
front and side walls from two squares ahead back to the party's square,
nearer walls over farther ones, each tile masked with colour 13
transparent. The backdrop (`69ea:0184`, outside CGA mode; see Display modes
for CGA's) is 44 rows of sky, a two-row line in `DS:6d82`, which only CGA
mode sets (so it is black), and 42 rows of colour 8, then the horizon
picture from `SKY.DAX` record 252. Under a sky of colour 11 on squares
below 0x80, record 251, the sun, shows facing east at hours 1-5 (`0x4bc9`),
south at 3-5 and 13-15, and west at 13-18, and record 250 shows facing
north. The sky colour, and so the sun, is the one `6945:00ba` last picked
for the party's square (`DS:6d80`), with the map on or not; turns and
`Area` redraw with `69ea:0820` alone and keep it after a script has changed
`0x4bfd` or `0x4bfe`, as ECL2 block 48's location vector does at 15:00
without showing the view again. The original draws into a 21-unit by
168-row buffer (`DS:4b78`) that `127f:12e8` copies to the screen one unit
right and one cell down; the port draws on the screen with that offset, so
the view fills cells 3-13 across and down, as small pictures do. While
`DS:6d84` is set, the view shows the overhead map instead (see Overhead
map).

## Overhead map

`Area` in the adventure commands (`475c:0a75`) turns the overhead map on
or off (`DS:6d84`) and redraws the view (`69ea:0820`), leaving the status
line as it is. Where the area hides the party's square, var `0x4bfb` not
0, it shows `Not Here` on row 24 in yellow for speed × 100 ms and clears
the row (`1521:0af1`) instead, unless the game was started with `Helm` as
its second argument (`ParamStr(2)` against `DS:896a`, which `1960:0000`
sets at startup; `cok_adventure.helm`, `eclplay --helm`). The catacombs of Throtl (ECL1 block 34), Gargath Keep
(ECL2 block 49) and the flying citadel (ECL3 block 96) set `0x4bfb` and
clear it as they are left. It is a game variable, saved with the game;
the status line leaves out the square while it is set (`6346:2d75`, see
Party).

While `DS:6d84` is set, `69ea:0820` draws the map (`69ea:000f`) in place
of the view and its backdrop: 11 by 11 squares, one 8×8 tile each,
drawn opaque into the view's buffer with the tile routine (`6e22:01ab`),
so that they fill cells 3-13 across and down. The window starts at the
party's column and row less 5, kept to 0-5 so that it stays on the 16 by
16 map. Each square is tile 0x104 of the frame's set (`8X8D1.DAX` record
202) plus 1, 2, 4 and 8 for a wall of any type on its north, east, south
and west sides (`69ea:06a2`): doors and walls the party can pass show as
walls, and a wall shows only on the side of the square that has it. Then
the party's arrow, tile 0x100 + facing / 2, pointing north, east, south or
west, covers the party's square and its walls. A square is dark grey (8)
with a line of light grey (7) along each side with a wall; the arrow is
white (15) outlined in black. Nothing in the routine depends on the
display mode (`DS:4b76`), so CGA mode draws the same tiles.

The map stays on through steps and turns, which redraw it.
`6945:00ba`, which shows the view after a step, when a block is entered
and when a picture or sprite is taken away, turns it off first where
`0x4bfb` is set, `Helm` or not. A turn (`475c:0c2d`, `0c7d`, `0ccb`) and
`Area` redraw with `69ea:0820` alone: a map turned on before a script set
`0x4bfb` stays on until the next step, and `Area` then says `Not Here`
and leaves it on. The first sprite of an encounter (`3775:0575`, with no
sprite loaded) turns it off and shows the view, or outside 3D areas the
big picture (`DS:713a`); a sprite loaded before is erased by a redraw
that keeps the map, but scripts forget their sprite when they exit. A
picture in the view's place covers the map until the view is restored.
`DS:6d84` is cleared at startup and at the end (`3e99:005b`, `0843`) and
is not saved.

For a square off the map, which only a script or `--at` can give, the
original draws the arrow outside the window, unclipped (`127f:08d7`), and
copies whatever its buffer holds around it to the screen; an arrow past
the frame's tiles (a facing of 0x50 or more, which the port never has)
halts it (`6e22:01ab`). The port stops with `COK_ECL_UNDEFINED` for both.

A differential test ran `69ea:0820` with `DS:6d84` set in an 8086
emulator on the 15 GEO maps of the three files, at every square and
facing, with `DS:8846` 0, 0x50 or the block, and on random squares and
facings, the tiles it drew replayed onto a screen of noise and compared
with the port's. Of 20,360 cases, the 16,437 the port draws, the 15,360
of every map, square and facing among them, agreed in every pixel. Both
refused 1,979, arrows past the frame's tiles; the port alone refused
1,944, squares off the map, whose arrow the original drew outside the
window but inside its buffer (379) or outside the buffer (1,565). It is
not part of the repository.

## Adventure loop

`cok_adventure_enter` starts the game's first block as `2fd3:3c28` does, a
new game's or a loaded one's: it runs only the block's load vector
(`2fd3:3cc7`), entering the next block as `2fd3:3b47` does if that runs
`NEWECL`, as Solace's (block 0x24) does. A loaded game then redraws the
screen and shows the view (`DS:4b52`), outside the overland map (mode 3)
only, where nothing is redrawn; the after-move and location
vectors wait for the first command that returns to the loop: so a game
saved on a square with an event, or on Throtl's square of the overland
map, resumes quietly at the command menu, and no step is counted and no
random number drawn before it. `cok_adventure_play` runs the loop of
`2fd3:3c28` in a 3D area once a block has been entered; `eclplay --play`
enters the block, then takes commands until the keys run out. Each turn
takes a command from the menu on row 24
(`475c:09ec`), runs the after-move vector, takes the step chosen
(`475c:0e77`), shows the view and runs the location vector. In a 3D area
the menu returns only for a step forward (taken or not), `Encamp` and
`Look`; `Area`, `Cast`, `View`, `Search` and turns stay in it and run no
vector. `Encamp` camps and `Look` runs the location vector alone, each
then taking the next command; on the overland map the menu returns for a
step and `Encamp`. `NEWECL` in any
vector enters the new block as `2fd3:3b47` does, which also frees the small
picture, clears `0x7ed5` and keeps the block in `0x4bf2`. The party's square
before each step is kept in `0x4bf0` and `0x4bf1`. When the loop ends it
clears `DS:4b57`, the flag that ends a run. Not ported: when the first
block's load vector (or a block it enters by `NEWECL`) sets `DS:4b57`, the
party killed, `2fd3:3c28` does not test it until it has redrawn a loaded
game's screen and read one command (`475c:09ec` at `3d64`, then `3b47` if a
`NEWECL` was pending); the port ends the run at once. No shipped load
vector kills the party.

The menu reads `Move Area Cast View Encamp Search Look`, laid out as it is,
so each capital is an item and its key (`cok_menu_read`). `Move` changes the
menu to `Exit`: then the up arrow (or 8) steps ahead, left and right (4 and
6) turn a quarter, down (2) turns around, and `Exit` returns to the commands.
`Search` toggles bit 0 of `0x7eca`. `Look` sets bit 1 and passes ten
minutes; the location vector then runs once with `0x7eca` at 1, after which
the search bit is restored. `Encamp` camps (`2fd3:3403`, see Camp).
After a command, text that `PRINT` or `VERTICAL MENU` left in rows 17-22 is
cleared (`DS:884e`). Other special keys pick a character (`546c:3334`, see
Party) and redraw the party list and status line, which are also redrawn
after each step, turn and `Search` or `Look`.

Before the after-move vector runs, a step that would leave the map sets
`0x7ed5` (`475c:0765`); a vector that sets `0x7ec9` to 0xff cancels the
step. Whether the party can pass the side it faces comes from the map
(`69ea:0573`, `cok_view_passage`): a side with no wall is open, and
otherwise its two bits in the fourth table give 0 for a solid wall, 1 for a
way through, 2 for a locked door and 3 for one that cannot be picked. A step
(`475c:0813`) waits 50 ms, moves one square, wrapping at the map's edges,
and passes one minute, or ten while searching.

At a locked door the menu shows `Locked.` and whichever of `Bash`, `Pick`
and `Knock` may still be tried (`DS:7146`-`7148`), then `Exit`. All three
become available again after each step; before the first step none are, and
no menu shows. `Bash` (`475c:02f3`) rolls for each character in turn against
its strength (`+0x11`, `SCORE`, with exceptional strength `+0x1c`,
`STRENGTH_EXCEPTIONAL`), with a harder table for a door that cannot be
picked; a character too weak to try stops it being offered. `Pick`
(`475c:05b6`) is offered when a character is a thief (`475c:0275`: a level
in class 6, or a former one a human may still use, `66c2:0efb`), and each
character who is okay rolls 1-100 against `+0xdc` (`THIEF_SKILL` 2); at a
door that cannot be picked, choosing it only stops it being offered. A
bashed or picked door opens on both sides for good (`475c:0148`). `Knock`
(`475c:0720`) is offered when a character has memorized spell 0x1f among the
bytes from `+0x1e` (`MEMORIZED`); the first such character forgets it, and
the party passes once, leaving the door locked. As in the original, the door
menu takes special keys by the letter of their scan code, so the down arrow
(0x50, `P`) picks and the left arrow (0x4b, `K`) knocks.

The clock is seven words from `0x4bc6`, which carry into the next at 10,
10, 6, 24, 30, 12 and 256 (`DS:3874`): `0x4bc7` counts minutes, `0x4bc8`
tens of minutes and `0x4bc9` hours, then days, months and years.
`cok_adventure_pass_time` follows `57e4:0549`: it adds one unit at a time and
carries each full unit once. A new day counts a day in each moon's phase
(`0x4cfc`-`0x4cfe`); after 8, 1 and 6 days a moon moves to its next phase
(`0x4cf9`-`0x4cfb`, 0-3) and is redrawn on the frame. Months carry into
years without aging anyone; only once the years reach 256, where they
stay, does each unit that passes age each character a year (the word at
`+0x60`, `AGE`, `57e4:0459`). The clock then counts down the
party's spell effects (`57e4:0171`, see Spell effects).

`Cast` (`475c:0ade`) casts for the selected character if its status is
0 (see Casting); with none selected the original reads through NULL and
the port stops. `View` shows the selected character (see View). `Area`
shows the overhead map (see Overhead map). Outside 3D areas the loop
travels the overland map (see The overland map). A step forward plays
sound 10 as it starts (`475c:081a`) and again once the view shows the new
square (`2fd3:3eb9`, when the party's square changed or is below 0); a
turn plays it (`475c:0c58`, `0ca6`); see Sound.

## The overland map

Outside 3D areas (`0x4be6` 0, mode 3) the adventure loop travels the
overland map, the lands around Throtl that ECL1 block 16 runs. It is
`BIGPIC1.DAX` record 0x79, 38 units by 120 rows, drawn at cell 1, 1 in
the frame of `1128:0344` (`6961:085b`); each 8×8 cell is a square, 38
across and 15 down. The party's place there is var `0x4bc3` across and
`0x4bc4` down, words saved with the game, which travel and the scripts
set: block 16 puts the party at 1, 3, the first outpost, when the game
starts out (`0x4bf2` 0x24, at `803e`) and when it helps the caravan's
survivors there (`879e`), and at 17, 8 when Maya leads it to Neraka
(`80a2`); ECL3 block 98 at 12, 4 before it returns there. The scripts
set it before showing the map or leaving it. `cok_adventure_travel`
and `cok_adventure_mark` are the step and the mark.

The commands (`475c:09ec`) need var `0x4cf7`, which block 16's load
vector sets and clears again before it leaves for a location. The menu on
row 24 reads `Move Encamp`, as the 3D menu is laid out and coloured.
`Move` clears row 24 (`67b5:0c7b`) and shows `Exit`: then any special
key ends the command, an arrow or keypad key (`DS:1fdf`) first facing the
party (`DS:6d87`) by its scan code, `H I M Q P O K G` north to north-west
(keypad 8 9 6 3 2 1 4 7); any other special key, keypad 5 among them,
leaves the facing as it is. `Exit` goes back to the commands; Move mode
otherwise lasts through steps and blocks. `Encamp` camps (see Camp).
Special keys from the commands pick no character, and the party list and
status line are not drawn in mode 3 (`6346:07ba`, `2d75`). Without
`0x4cf7`, or in a mode other than 3 and 4, the command is a byte of the
stack the routine never sets (`[bp-3]`), which the calls before it left
there: 9, the counter of `3775:01e8`'s loop, after a block is entered,
but a byte of a segment that `2fd3:3b47` pushed after a `NEWECL` in the
load vector, or 0 when overlay `475c` had to be loaded. The loop then runs
the after-move vector and, in mode 3, the step, which does not test
`0x4cf7`: the party travels on with no key read, step after step. No
shipped script reaches the loop so (block 18 clears `0x4be6` but always
leaves); the port stops with `COK_ECL_UNDEFINED`.

The loop then runs the after-move vector, keeps the party's 3D square in
`0x4bf0` and `0x4bf1` as it does in 3D areas, and takes the step
(`475c:0e77`): unless the vector set `0x7ec9` to 0xff, `475c:08d5` puts
back the cell under the mark (`4877:00d6`), keeps the place in `0x4bf0`
and `0x4bf1`, adds the facing's step (`DS:1ed6` across, `DS:1edf` down)
to the low byte of each word, kept as a signed byte within 0-37 and 0-14,
marks the party there and passes twelve hours (`57e4:0549`, unit 3, count
12), so that a day passes every second step. Then the view (`6945:00ba`,
which here draws the map only while `DS:713a` is set) and the location
vector run. A stopped step leaves `0x7ec9` at 0xff through the location
vector, where the 3D loop clears it. `eclplay` logs each step as
`overland: X,Y,DIR`: with `--set 4bf2=11 --set 4bc3=1 --set 4bc4=3 --set
4c2d=1` (hex; as if from the first outpost, the caravan met), the keys
`nm98y` decline the outpost, travel north-east and north, and enter
Throtl.

The mark (`4877:0005`) is `CURSOR.DAX` record 1, loaded at startup with
colour 13 transparent (`3e99:07b8`, `DS:8965`): a cell white around a
black ring around yellow, which has no colour 13 and covers the square.
It saves the screen cell at `0x4bc3` + 1, `0x4bc4` + 1 (`127f:0edb`) in a
picture of a cell (`DS:6162`, zeroed at startup), draws that and the
cursor masked into another (`DS:6166`, `127f:08d7`) and puts it on the
screen (`127f:10e7`); then it clears the view buffer's dirty tables
(`DS:4b90`, `4c38`, `4d88`, `4ed8`), which are not ported. Both copies
take the words times 4 bytes across and 8 rows down, unclipped. `PICTURE`
0x79 marks the party after drawing the map, and the
camp (`2fd3:3403`) and `COMBAT` (`2fd3:1c5a`) after showing the map again
outside 3D areas, unless `0x4c38` is set (for `COMBAT`, also unless the
run ended). Loading a saved game outside 3D areas loads big picture 0x79
(`4b6d:1b34`) without drawing it.

Where to go and what is met is block 16's script. Its location vector
looks the place up in a table of eleven (at `8e72` and `8e7e`) and asks
`YOU ARE NEAR NAME. DO YOU ENTER?`; Yes sets `0x4be6` 1 and `0x4cf7` 0,
picks the ECL file and runs `NEWECL`, and each place left for the map
sets `0x7f12` 1 and runs `NEWECL 16`. In the wild it counts the steps in
`0x4c06`, cleared with the block's variables, and from the tenth rolls
`RANDOM 99` each step, until one meets something, against a third of
each of `0x4bc3` and `0x4bc4`, plus 10: monsters by region below 50
(`COMBAT` outside 3D areas: open ground, the monsters two squares off),
then a merchant caravan's shop, pilgrims'
temple, aged adventurers' training (`PROGRAM 0`) or knights' camp
(`PROGRAM 9`). The after-move vector stops steps into the sea (`YOU
QUICKLY TIRE OF SWIMMING AND RETURN TO SHORE.`) by place and facing (var
`0x4bea`), and the camp vector sets the rate of encounters while resting
(`0x7ed2` 96, `0x7ed3` 10; none after the knights' hospitality, `0x4c05`).

`DESTROY ITEMS type` (`2fd3:35a3`), which block 16's location vector
runs on entering the block and after each step (`DESTROY ITEMS 63`),
takes every item of that type (`+0x2e`) from every record in the list
(`6346:1697`), unreadied first if readied (`546c:1ea7`) with its record
selected, so that the effect of its power goes from it; a cursed one says
"It's Cursed" and goes readied. Each record's stats are then recomputed
(`6346:0d20`), and the selection restored. Type 63 is the Long Sword +5
of the tomb of ECL2 block 67 (`ITEM2` record 66, given at `92a6`: "THESE
SWORDS ARE ONLY TO BE USED WITHIN THE TEST OF HONOR"), which that block
takes back itself in places and ECL3 block 98 too (`8c10`, with six other
types): it is gone once the party is back on the map. The original
reads the next item before it unreadies one, so where an item's power
takes items away or adds them (Spiritual Hammer's), the item or the next
may be freed; the port stops with `COK_ECL_UNDEFINED` when the items
change.

`PROGRAM` (`2fd3:3473`) turns Move mode off while it runs and first
restores the selection `LOAD CHARACTER` changed (`DS:43ba`). 9 camps
(`2fd3:3403`, see Camp) in the middle of the script, which goes on after
it (`DS:4b43` kept), and then ends the script as `EXIT` does
(`2fd3:0050`) unless `0x4c38` is set: the knights' camp, the inn of
ECL1 block 17, and ECL3 blocks 96 and 97. 0 opens the start menu
(`4def:01b4`, see The start menu), which the scripts' training halls
open after setting var `0x7ea8`, and the script goes on once Begin
Adventuring leaves it; then, outside 3D areas, unless `0x4c38` is set or
the small picture is PIC 9, the screen is redrawn for the mode
(`6346:2c17`). Other values (ECL2 block 57's 3) do nothing.

The port keeps these quirks:

- The cell put back is the one saved at the last mark, put back at the
  party's place now: a script that moves the party while the map is
  shown leaves the old mark behind and pastes the cell it covered at the
  new place, and before any mark the cell is black. The shipped scripts
  set the place only before `PICTURE` 0x79 or a `NEWECL`.
- The step works on the low byte of each word, so 0x4002 travels as 2,
  and 0xffff, -1, stops at 0; the mark takes the words times 4 and 8, so
  0x4002 is marked in column 3 and 0xffff in column 0.
- Ctrl-F8 in Move mode, whose scan code 0x65 is `e`, camps: the loop
  camps on `UpCase` of the key (`2fd3:3db9`), with the facing unchanged
  and no step. (NumLock's scan code is `E`, but the BIOS never queues
  it.)
- A stopped step keeps `0x7ec9` at 0xff through the location vector, and
  `0x4bf0` and `0x4bf1` hold the 3D square the loop stored.
- A facing past 8, which only a saved game could hold, takes its step
  from the bytes after the tables, the combat terrain table from
  `DS:1ee4` (`cok_combat_terrain`); 8 does not move.

A mark on a cell off the screen, a column past 39 or a row past 24 once
the words are multiplied, which the original reads and writes outside its
rows or past its table of rows (`DS:55e8`), stops the run with
`COK_ECL_UNDEFINED`; the step itself keeps the party on the map, so only
a script or a saved game can put it there. The mouse calls around the
step (`1743:0051`, `0030`) hide the cursor around the redraw only, so the
port, which composes the cursor over the screen, has no need of them; the
scripts sound each step
(`CALL [b203]`).

A differential test ran the original's step (`475c:08d5`, after
`4877:0005` in half the cases), `PICTURE` 0x79 (`6961:07ed`, `085b` and
`4877:0005`) and the command (`475c:09ec` in mode 3, its menu fed
scripted results) in an 8086 emulator against the port, in Tandy mode on
a linear screen of noise: random places, mostly on the map, words that
wrap or lie off the screen, facings 0-8 and past the tables, and random
cells in `DS:6162`, comparing the screen (for the map, cells 1-38 by
1-15), the cell saved, the place, `0x4bf0`-`0x4bf1` and the time passed,
and for the command the menus shown, the facing, Move mode, camping,
`0x7ec9` and the item selected. Of 20,000 cases, the 17,753 the port
carries out agreed: 8,945 steps, 3,769 maps and 5,039 commands. The port
alone refused 2,246 marks off the screen, which the original writes
outside it; both refused one, in which the emulator then met an opcode it
does not implement. It is not part of the repository.

## Party

`src/party.h` holds the party and its characters. The original keeps the
party as a linked list of 409-byte records from `DS:609a` (next at `+0x17f`)
with the selected character at `DS:6096`; the port keeps an array in the same
order, up to 72 records with the monsters `LOAD MONSTER` appends after the
party (see Monsters and encounters), and the selected record in
`vm.character`. The record's fields are named in the game's layout
(Champions' in `src/rules.c`, see Games' "The character record's
layout"), through which code reads them. Fields such as the armour class
(`+0x18d`, as 60 - AC) are derived from the others and the items when a
character loads (see Derived stats); its spell effects are a list (see
Spell effects).

A saved game, `SAVGAM<letter>.DAT` (`4b6d:1b34`; the camp's Save writes
one, see Camp), is 5,469 bytes: the ECL file, the variables
`0x4b00`-`0x4eff`, `0x7c00`-`0x7fff` and `0x7a00`-`0x7bff` as words, the
party's square, facing, wall ahead and square byte
(`DS:6d85`-`6d89`), the last and current modes (`0x1406` and `0x1407`:
`DS:4b4a` and `4b49`), three wall sets (record and slot), the party's size
and eight 40-character names. Each name, stripped of
` .*,?/\:;|`, cut to eight characters and upper-cased (`169c:05da`), names
the character's files beside the saved game: `.SAV`, the record; `.STF`, its
items, 63 bytes each; and `.SFX`, its spell effects, 9 bytes each
(`4b6d:11e5`). Missing characters are skipped. Each character's derived
fields are then recomputed (see Derived stats). Each character added gets
the lowest combat icon slot free (`+0x137`, `ICON`) and is counted in
`0x7f3e` (`4b6d:1989`), and an NPC's levels are recomputed again. Then every
record gets its combat icons (`4b6d:1b34`, see The combat screen). Loaded
from the start menu, the roster's copies of the characters (`.WHO`, `.STF`,
`.SFX` named after them, `4b6d:0a80`) are then erased (see The start
menu); `eclplay --load` keeps no roster. The names are made as the
original makes them, by `169c:05da` after `169c:050b` has taken out the
dots: each removes one character at a time and does not test again the
place a removal slides the next into, so of two alike in a row one stays
(`A  B` names `A B`).
`cok_adventure_restore` then reloads the map and wall sets in a 3D area,
or else loads the overland map, big picture 0x79, without drawing it,
sets the speed and animation from `0x4bfc` and `0x4bff`, and
sets `DS:4b52`, so that the first block keeps its variables and the
adventure loop redraws the screen (`6346:2c17`). The loader makes the
saved mode the last mode (`DS:4b4a`) and the mode 0. Play resumes in block
`0x4bf2`, or 0x24 if that is 0 (`2fd3:3c28`), which sets the mode to 4 (3
outside 3D areas) and runs only that block's load vector before the first
command (see Adventure loop); the last mode stays the loaded one until a
`NEWECL` (`2fd3:3b47` copies the mode there at its end), so a game saved
in camp, loaded and saved in camp again holds 2 at `0x1406`.

The layout is the game's (the descriptor's `saves`, with
`ecl.variable_bytes`; `cok_saved_game_read` and `write` in `src/party.c`):

| Bytes | Champions (5,469) | Death Knights (2,913, `SAVGAM?.PTY`) |
| --- | --- | --- |
| the ECL file | 0 | 0 (`DS:98a2`) |
| `0x4b00`-`0x4eff` | 1-0x800, a word an address | 1-0x400, a byte an address |
| `0x7c00`-`0x7fff` | 0x801-0x1000 | 0x401-0x800 (`0x7f12` at 0x713, `0x7f3e` at 0x73f) |
| `0x7a00`-`0x7bff` | 0x1001-0x1400 | 0x801-0xa00 |
| square, facing, ahead, square byte | 0x1401-0x1405 (`DS:6d85`) | 0xa01-0xa05 (`DS:c073`) |
| last mode, mode | 0x1406, 0x1407 (`DS:4b4a`, `4b49`) | 0xa06, 0xa07 (`DS:8c4f`, `8c4e`) |
| wall table: record, slot (words) | 0x1408-0x1413, slots 1-3 (`DS:6d8a`) | 0xa08-0xa17, slots 0-3 (`DS:c078`) |
| party's size, eight string[40]s | 0x1414, 0x1415- | 0xa18, 0xa19- |

Death Knights' loader (`46b2:1ef0`) and save (`46b2:2808`) follow
Champions' with these differences: the loader keeps the saved last mode
(the mode 0), and loads again the wall table's first entry alone (its map
and that record's three sets, `6d21:0ec5`; a second record that `LOAD
PIECES` put in slot 3 is not reloaded), outside 3D areas picture 0x70;
its characters are `.GAM` and their files (see Games' Death Knights); the
roster's copies are erased by the loader itself (`46b2:09dc`: `.PC`,
`.JNK`, `.EFF` and `.WIZ`); and the vault's letter becomes the saved
game's. Bytes the original leaves as its stack held them: after each name,
and the slots past the party (as Champions'); the port writes 0 there.
Both saved games of `DKKAssets/SAVE`, loaded and saved again, give their
files back but for those bytes and the characters' far pointers
(`tests/test_start.c`, `tests/test_death_knights.py`), as do those the
original wrote in DOSBox-X (`saves`' checks).

Load Saved Game in Death Knights asks first `load from where? ` over
`Death Champions Exit` (`DS:306c`, `3337`): Death its own saved games,
Champions those of *Champions of Krynn* in the directory `DKK.CFG`'s
fifth line names (`DS:99a4`; the port's `--import`, by default
`--saves`), loaded as `46b2:1ef0` does with `DS:c468` 0
(`cok_adventure_import_game`): of the file only the party's size and
names (Champions' layout: `Seek(0x1414)`) and Champions' bank, the word of
its variable `0x4c33` (`Seek(0x267)`), which goes to the bytes `0x4c33`
and `0x4c34` (`COK_VAR_SAVINGS`); each `.SAV` imported
(`cok_character_import`) and joined; then icons and the mode 0. The
loader would leave out Champions' NPCs (the record's `+0x3e` 0x80 and up
after the import), but the import does not move Champions' NPC flag
(`+0xe7`), so an NPC of a Champions game joins as a player character. The other variables stay a new game's (the first block clears
its own), and so the speed and pictures read from `0x4bfc` and `0x4bff`
are 0 (a quirk: the original's next save holds 0 there too).

The bank vault (`VAULT<letter>.DAT` beside the saved game): 12 bytes, the
three 32-bit counts of coins of the treasure pool (`DS:be58`), then items
of 63 bytes (`cok_vault_read`, `write`; their next pointers, `+0x2a`,
written as in memory by the original, 0 by the port). The game keeps the
letter of the vault it last loaded or saved (`DS:fb79`, `Q` for none at
startup and after a game) and whether the vault's menu left it in
`TMPVAULT.DAT` (`DS:fb78`). Saving as a letter (`46b2:2808`, after the
characters) moves the temporary file to the letter's name (erasing the
old), or else, for another letter than the vault's, reads the vault's
file (`46b2:2e1d`; without one, no coins, or a hundred times the savings
`0x4c33` in steel: a Champions party's bank) and writes it as the
letter's (`46b2:302d`). The vault's menu (`3948:000e`, reached from
`COMBAT` with `0x4d00` set: block 35) is the treasure's (View, Take,
Pool, Money, Items) over the vault loaded into the pool, and writes
`TMPVAULT.DAT` when left: see Death Knights' treasure.

The party list (`6346:07ba`) shows `Name` and `AC  HP` on row 2 from column
17, then a character a row from row 4: the selected one's name in white,
others light cyan, or light red when they cannot act (`+0x189` clear;
`6346:199d`). AC is light green, right-aligned to column 34 with a minus sign
for negative values (`6346:0984`); hit points are right-aligned to column 38,
yellow when below the maximum (`6346:0a0d`). Each row is cleared first, and one
more after the list, so a party that shrinks by two leaves a stale row, as in
the original. It is not drawn outside 3D areas unless `0x4c38` is set, nor
over a big picture (`DS:4b4e`). The status line (`6346:2d75`) on row 15, in
light green, reads like `7,15 N 00:00 search`: the square unless the area
hides it (`0x4bfb`, see Overhead map), the facing (turned by `0x4cff`), the hour and minutes,
and `search` while searching, or `camping` in camp, with `*` after the
time while the debug flag is set (`6346:2f4c`, see Debug mode). The list
is redrawn when a block's vectors have run, by `CLEAR BOX`, `LOAD FILES` and
`WHO`, and after damage; the status line after steps, turns, `Search`, `Look`
and special keys in the adventure loop.

Special keys from a menu pick a character (`546c:3334`): up (or 8) the one
before, wrapping to the last (or the last, with none selected), down (or 2)
the one after, wrapping to the first, and every other special key the
first. `HORIZONTAL MENU` then redraws
the party list (`3775:1885`), and the adventure loop the list and status
line. The original walks off the list when the selected character is not in
the party; the port selects none.

`LOAD CHARACTER` selects the character at the position given by its operand,
`& 0x7f`, from 0; `EXIT` restores the selection made before the vector ran
(`DS:43bf`). Past the end of the party the selection stays and `0x7d00`
reads 0 (`DS:8855`). With bit 7 set, the original would also remove the
character (`4def:3b0a`) when `DS:883a` is set and its name was cleared, but
nothing sets `DS:883a`. `0x7eb1` and `0x7eb4` read the selected character's
position (`3775:0773`), the party's size if none is selected, and `0x7cc9`
whether it may use a former class (`66c2:0efb`).

`ADD NPC id morale` (`2fd3:311c`) adds monster `id` of the ECL file as an
NPC (`4b6d:18fc`) unless the party (`0x7f3e`) has eight or more: the
record, its effects and items read as `LOAD MONSTER` reads them
(`4b6d:161b`, see Monsters and encounters), `id` kept in `+0x115`
(`MONSTER_ID`), then joined as a saved game's characters join (`4b6d:1989`):
after every record in the list, in the lowest icon slot 0-7 free (8 if none
is), selected, counted in `0x7f3e`, and its levels recomputed (`66c2:0433`)
if it is an NPC (`+0xe7` `NPC` 0x80 and up, as every one is); then its
combat icons, `CPIC` records `id` and `id + 0x80` of the file, load in its
slot (`6d21:01d0`). Then, whether it joined or not, the selected character
gets `+0xe7` 0x80 + `morale` / 2, its stats are recomputed (`6346:0d20`) and
the party list drawn. `eclplay` logs `party: NAME joins, icon ID in slot N`.
The NPCs of the scripts are the kender Kildirf, a soldier and Strangbourn in
ECL1 block 32, and Skyla and Mysellia in ECL2 block 50, all with morale 100,
so 0xb2. ECL2 block 57 is the demonstration the title screen plays when no
key is pressed: its menu times out to `D` (`DS:6e11`, `6e15`), which sets
`DS:4b4b`, and `2fd3:3c28` then starts block 0x39 of ECL2 at speed 9 with
no party; the block adds three NPCs to make one. Its own paths, where
`DS:4b4b` is read, are described in The start menu; `eclplay
--start-menu` plays it from the title. Skyla's icons are `CPIC2` record 44, which
is not there: she has none in combat, as in the original.

`DUMP` (`2fd3:351b`) removes the selected character from the list
(`4def:3b0a` with 0, 1, as Alter's Drop does: its icons freed, counted out
of `0x7f3e`) and selects the record before it, or the first; with none
selected it selects the first and removes nothing. The selection then
becomes the one `EXIT` restores (`DS:43bf`), and the party list is drawn.
`eclplay` logs `party: NAME leaves`.

The port keeps these quirks:

- With eight in the party, `ADD NPC` adds no one but still makes the
  selected character an NPC of that morale; with none selected the
  original writes through NULL, and the port stops.
- The scripts select an NPC to dump by name: ECL2 block 50's search for
  Skyla (`9add`) loads each position with `LOAD CHARACTER` and skips any
  that does not read as able (`0x7d00` 0, status 1, 2 or 9 and up), so
  that without her, the selection stays on the last position inside the
  party and `DUMP` takes that character.

`ADD EP` prints `Congratulations NAME gains experience!` (or `the party`)
and waits speed × 100 ms, then adds the points, divided by the number of
classes with a level (`+0xf9`-`+0x100`, `CLASS_LEVEL`), to the 32-bit
experience at `+0x116` (`EXPERIENCE`) of the selected character (first
operand 0) or each character, if it can act. A character with no level
divides by zero, which stops the run as runtime error 200 would, even one
that cannot act, as the division comes first.

`WHO` clears the text window and shows its prompt and `Select` on row 24
(`6346:32c7`), redrawing the party list with the character picked so far:
up and down (8 and 2) move through the party, wrapping, and `S` or Enter
picks. Escape does nothing. The original also leaves on the special keys
whose scan codes are `E` and `S`: Del, and NumLock, which the BIOS never
puts in the keyboard buffer that the game reads (`Crt.ReadKey`, INT 16h),
so that only Del does.

`DAMAGE` takes five byte operands: an attack count or flags, dice count,
dice sides, damage bonus, and a to-hit bonus or saving throw. Damage is the
bonus plus the dice, rolled with Turbo Pascal's `Random` (`60f4:1216`). Without
bit 7 of the first operand, it is that many attacks on characters chosen at
random from the size in `0x7f3e`, each hitting if a d20 (20 counting as 100)
plus the fifth operand beats `+0x18d` (`AC`), a 1 always missing
(`60f4:0ffb`), and damage is rolled again after each. With bit 7, its low
five bits are a saving-throw bonus and the fifth operand's low three bits
the throw: with bit 6 each character is hit unless it saves (bit 5: no
save), with fifth operand bit 7 the selected character (throw type - 1, and
none for type 0), and otherwise a random one; bit 4 deals the damage even on
a save. A save (`60f4:113a`) needs a d20 plus the bonuses at `+0x17c`
(`SAVE_BONUS`) and in the operand, as a byte, to reach the throw at `+0xd0`
(`SAVING_THROW`); 1 always fails and 20 succeeds, and a character with a
level at `+0xfe` (`CLASS_LEVEL` 5) and `+0x5e` (`MAGIC_ORDER`) set gets -1
or +1 from the word at `0x4bf8 + +0x5e`, a byte sum that wraps (perhaps
meant for the moons). Each hit (`3775:20a6`) prints `NAME is hit FOR N
points of Damage.`, or `NAME dies.` past its hit points plus 10, paging with
`press <enter>/<return> to continue` when the window is full, and redraws
the list; the dead take none. Damage equal to the hit points leaves a
character unconscious (status 4), up to 9 more dying (5), and more dead (6),
and it can no longer act (`6346:24d7`). The message tests more than 10 past
the hit points but the status 10 or more, and only the low byte of the
damage is dealt, as in the original. Afterwards, if no character can act,
the frame is cleared, `The entire party is killed!` printed and the run
ended (`DS:4b57`); either way the prompt then waits for a key. The
characters' spell effects change the attack and saving rolls (see Spell
effects). In a battle `6346:24d7` also updates the combatant's combat record
(see Attacks, movement and death).

## Derived stats

`cok_character_stats` ports `6346:0d20` and `cok_character_levels` ports
`66c2:0433`, with the routines they call. The original runs both, in that
order, at the end of loading a character (`4b6d:11e5`), and `66c2:0433`
again when an NPC (`+0xe7` 0x80 and up) joins the party (`4b6d:1989`); the
port does the same in `cok_character_read` and when it adds a saved game's
characters or `ADD NPC` adds one, after which `ADD NPC` (`2fd3:311c`) runs
`6346:0d20` on the character selected (see Party). Otherwise `6346:0d20`
does not run again after `66c2:0433`, so an item
that `66c2:0433` unreadies still counts until the next recompute, as in the
original. Item types come from `ITEMS`, which the game reads at startup
(`3e99:005b`): 128 records of 16 bytes from offset 2, at `DS:5886`.
The character record's fields these routines use are in Champions'
layout (`champions_character` in `src/rules.c`, see Games), the items'
and item types' in `party.h`.

`6346:0d20` counts the items (`+0x142`), weighs them, each weight (`+0x37`)
times its count (`+0x39`) if that is not 0, with the six coin words from
`+0xed`, into `+0x17d`, and adds up the hands of the readied ones (`+0x17b`).
Each readied item fills the slot its type names (the far pointers at `+0x147`,
which it clears first; the port sets those bytes to 0 and keeps 1 + the item's
index in `cok_character.slots`, 0 for none): the last one of each slot
0-8, the first two rings, and the last readied items of types 0x1e and 0x0c as
arrows and quarrels. The base attacks, dice and damage bonus
(`+0x10d`-`+0x112`) become `+0x191`-`+0x196`, THAC0 (`+0x18c`, as 60 - THAC0)
starts from `+0x59`, armour class from `+0x113` and movement (`+0x198`) from
`+0xd5`. Without a weapon, strength adds to hit and damage (`6346:1412`,
`6346:14b5`, by the row `6346:137a` gives, when `+0x114` is set). With one
(`6346:0023`), its type sets the attacks, dice and damage bonus; dexterity
adds to hit for a missile weapon (`6346:12f8`) and strength when the type's
flags say; the weapon's bonus and its readied arrows' or quarrels' add to
both; and races 0 and 1 with types 0x12, 0x13 and 0x16-0x1a add 1 to hit, race
5 with type 0x43 2. The armour class is the sum of five parts (`6346:02c8`):
dexterity (`6346:1276`), a shield and its bonus, the bonuses of items whose
type's armour class is 0 (byte 6 0x80), the best bonus of such a ring, and the
best armour plus bonus, or the base armour class if that is better. Magic
armour (slot 2 with a positive bonus) drops the ring part. `+0x18e`, the
armour class from behind, is the armour, item and ring parts less 2. Those
items and rings also add their `+0x33` to the saving throw bonus `+0x17c`.
Armour sets the movement by its weight, 3 more with any bonus but 0, so
cursed armour speeds its wearer too (`6346:0240`), and
weight past the strength allowance (`6346:153b`) limits it to 9, 6 or 3
(`6346:03e6`). `+0xce` is the highest fighter, ranger or knight level, or 1.

`66c2:0433` sets the base THAC0 `+0x59` to the best for each class's level
(`DS:3882`), one better for `+0x5d` 3, or 6 with race 3 or 4; raises the
highest level `+0xd6`, which it never lowers; and sets `+0x10b` to 3 above
fighter or knight level 6 or ranger level 7. `66c2:000f` and `66c2:0722` set
the spells a day from `+0x11c` (`DS:3c99`) and the spells known: a cleric's,
with wisdom bonuses, and a knight's from level 6 unless of order 1 (`+0x5c`),
which know the cleric spells of each level they can cast (`DS:423b`); a
ranger's from level 8, which knows all druid spells (`DS:31c3`); and a mage's,
doubled for spell levels 1-3 by each readied item of power 1. `66c2:08a6` sets
the saving throws (`DS:405b`) and `66c2:0b9f` the thief skills (`DS:3911`,
`DS:3971` and `DS:39a9`, by level, race and dexterity). `+0x11a` then gets the
bits (`DS:38ea`) of the classes with a level, or with a former level below
`+0xd6`, and items in the slots whose type's classes (byte 13) share none of
them are unreadied, unless `+0x36` is set. A human that may use its former
class (`66c2:0efb`) also gets its former levels' THAC0, attacks and thief
skills.

The tables are the original's, and the port indexes them with the
original's arithmetic over the original's initialized data, from `DS:3509`,
the lowest an index can reach, to its end at `DS:43bf`. An index past a
table reads what follows it, as the original does: the thief tables by
level, race and dexterity lie together, dexterity 20 reads the bytes after
them (3, 3, 18, 16 and 75), a level 13 cleric's THAC0 is the next class's at
level 0, and a level of -1 reads the byte before the THAC0 table. Item type
128 is the zeroed record after the 128 of `ITEMS` (the game reads 0x810
bytes into `DS:5886`). Where the original would read data the game sets as
it runs, an item type past 128 or a saving throw for a level of 90 or more,
or leaves the result uninitialized, as `6346:137a` does for a strength past
25 or an exceptional strength past 100, the port fails with an error and
the character does not load. `tests/test_data.c` checks the data segment's
tables (`src/data_champions.c`, read through the descriptor's named
tables: see "The data segment's tables") against `build/START_FULL.EXE`.

The port keeps these quirks of the original:

- `66c2:08a6` checks a former class after its class loop rather than in
  it, so only for class 7, the loop's last: a knight above its former
  knight level `+0x108` also gets the throws of that level, and level 0
  reads the entry before level 1, the thief's at level 12. A level 1 knight
  therefore saves as a level 12 thief.
- `66c2:0b9f` adds an uninitialized local to each thief skill. The stack
  holds the class counter `66c2:08a6` left there, 7, so every skill is 7
  higher, and a level 1 human thief reads languages at 7. With an item of
  power 11, skills 1 and 2 set it to 0 or 5 and the later skills keep that,
  as does the second call for a former thief.
- Saving throw 0 is raised, made harder, by a constitution of 4-18 for
  races 3, 4 and 5 or with a readied item of power 6.
- `66c2:0b9f` stops at the first readied item of power 2 or 11, so a
  character with both gets only the first one's effect.
- `6346:03e6` takes the weight past the allowance as a signed word, so
  32,768 or more past it counts as none.
- The dexterity table gives -19 for picking pockets at dexterity 10.

The twelve characters in `SAVE/` were saved by the original, and every field
recomputed from them matches the saved value but one: the two clerics of
`SAVGAMA.DAT` lack spell 8, which this executable's table makes them know.
Nothing in it clears a known spell, and the game in DOSBox-X, loading and
saving game A, sets it (see Checks against the original), so those saves
come from another version of the game. A differential test
against the original routines, run in an 8086 emulator on random
characters, also agreed; it is not part of the repository.

Neither routine reads spell effects (`.SFX`). Effects change stats through
their own handlers (see Spell effects): Spiritual Hammer's (`3f44:07b5`)
removes its hammer and recomputes when it ends, which is ported, and adds
one when cast, which is not; the stinking cloud's cough (`3f44:0ae0`),
which recomputes and then makes the armour class that from behind, 2
worse, needs the combat record, and is ported with the rounds. The
character sheet (`546c:07bb`),
the Items menu after every key (`546c:17f9`) and Trade's receiver
(`546c:2178`, `546c:32b0`) recompute, as ported (see View), and so do
spells by touch, before the attack roll (`5b04:1071`, which no spell cast
outside combat reaches; see Spells in combat).
Creating, training, modifying and changing the order of a
character (`4def:06dd`, `4def:4d9e`, `4def:28fa`, `4def:567f`) are ported
(see Creating characters and Training), and so is the computer's choice of
weapon in combat (`3afb:1608`, see The computer's turns). Combat
setup (`3cb2:10d9`) recomputes every record (see The battlefield), each
combatant's turn (`3995:040b`) the combatant (see The rounds), an attack
(`432f:1a45`) the attacker and a strike (`432f:1579`) the target (see
Attacks, movement and death), the end
of combat (`351b:1968`) every record left, taking an item (`36d0:034c`,
which buying does too) the taker, appraising gems (`58e7:1929`) the
appraiser after each key, `DESTROY ITEMS` (`2fd3:35a3`) every
record, and `ADD NPC` the character selected, as ported (see Treasure
and the end of combat, Shops and the temple, The overland map and
Party).

## Spell effects

`src/effect.h` ports the characters' spell effects: the rolls that consult
them and their dispatch (overlay `60f4`), their handlers (overlay `3f44`)
and their timers (`57e4:0171`). A character's effects are a list of 9-byte
records linked from the far pointer at `+0xe3`, read from `.SFX` in file
order (`4b6d:11e5`): `+0` the effect id, `+1` the minutes left as a word,
0 for an effect that does not end, `+3` a value its handler uses, `+4` set
to run its handler when the effect is removed, and `+5` the far pointer to
the next. The port keeps the list linked (`cok_effect`), without the
file's stale pointers. The characters in `SAVE/` hold only permanent
effects, each of value 0xff: 0x07, 0x12, 0x1a, 0x2f, 0x5c, 0x5e, 0x5f and
0x69, given by race or class; only Molly's 0x07 asks for its handler on removal.

| Function | Original |
| --- | --- |
| `cok_character_add_effect` | `60f4:1285` |
| `cok_character_find_effect` | `6346:2447` |
| `cok_effects_remove` | `60f4:01e9`, then `60f4:1743` for strength or charisma |
| `cok_effects_dispatch` | `60f4:057c`, `60f4:0352`, `60f4:01a8` |
| `cok_effects_attack` | `60f4:0ffb` |
| `cok_effects_save` | `60f4:113a` |
| `cok_effects_pass_time` | `57e4:0171` |

An effect is added at the end of the list; the original does not check
its allocation. Removing one takes it or, given none, the first with an
id; if its `+4` is set, the handler of the id given (not the effect's own)
runs with flag 1 first. It is then unlinked and freed. Removing id 0x0e
recomputes charisma, and 0x0c or 0x26 strength (`60f4:1743`), from the
base scores (`+0x10`, `+0x12` ... `+0x1a`, and `+0x1d` for exceptional
strength) into the current ones (`+0x11` ... `+0x1b`, `+0x1c`). Readied
items with a power (`+0x3e` 0x80 + power, `+0x3d` its kind) change them:
for strength, power 3, or 5 of kind 0, gives 18/100, 5 of kinds 1-6 19-24,
8 of kind 0 the base + 1 below 18, and 0x0d sets 3; for charisma, power 6
takes 1 and 8 of kind 5 adds 1 below 18. Then effects 0x26, 0x71 and 0x0c
give strengths by their value: up to 101, 18 with the value - 1 as
exceptional strength (so 0 gives 18/255), above that the value - 100.
0x26's strength is added to one below 19; a fighter, ranger or knight, now
or before, gets 10 times the excess over 18 added to its current
exceptional strength (`+0x1c`, not the base), up to 100. The better
strength wins at each step, where an 18 with a higher exceptional strength
beats 19 and up, so the result depends on the item order; charisma adds
the value of the first 0x0e. `cok_effects_ability` is `60f4:1743` for
any ability, as Remove Curse calls it: intelligence and wisdom are worked
out and not stored; dexterity (`+0x17`) adds 4, 2 or 1 for an item of
power 2 by the base below 7, up to 13 or above, 1 for power 8 of kind 3
below 18, and takes 2 for power 10; constitution (`+0x19`) adds 1 for
power 6 or 8 of kind 4 below 18, then sets the maximum hit points
(`+0x62`) to `+0x11b` plus, for each class, the hit points of its former
level and of its level above `+0xd7` (capped at its top, `DS:3903`, one
less from the top, a ranger's one more unless it has another former
level), 1-7 a level by constitution 15-25 for fighters, rangers and
knights, 1 at 15 and 2 above for the others, added as a byte and divided
by the number of classes with a level (none divides by zero: the port
stops); the hit points change as much, down to 0. At 20 and up the
character gets effect 0x3e (60 minutes, its handler on removal) if it
lacks it; below, its 0x3e goes. Removing an effect not in the list makes the original write to
`0000:0005`; the port fails.

`cok_effects_dispatch` runs the handlers for event 1-0x18 (`60f4:057c`),
each a fixed list of effect ids, in order; for each id the target's first
effect with it, or, for the ids the party shares (0x15, 0x2d, 0x2e and
0x31, the set at `60f4:0332`; the game's `effects.shared`), the first
record's in the list, a monster's too, when the target has none
(`60f4:0352`); in combat only a holder that has the target within 6
cells (Prayer, 0x31) or 1 (the others) on the map counts (see The
rounds). Events 6 and 9 first check magic
resistance (`60f4:04f3`) when the target has some (`+0x187`), an effect
is pending (`6b2f`) and no damage is, or magic damage (`6b31` bit 8): a
d100 up to the resistance plus 5 for each level the caster (the selected
character, `cok_effects_caster_level`, see Casting) has of the spell
(`6b33`) below 11, as a byte, clears the damage and the effect (but for
0x5b and 0x52). Handlers come from the table at `DS:6b94` (the game's
`effects.handlers`, see The spells and effects) that
`5b04:58ed` fills at startup (`3e99:0835`), and `3f44:38ea` after it
(`3e99:083a`), and get the flag (0 from the
dispatch, 1 on removal), the effect (the holder's, for a shared one) and
the character. While `DS:713c` is set, which readying an item and Remove
Curse do, `60f4:01a8` calls `3f44:3888` instead to add or remove the
item's effect (see View).

Of the ported code, only `DAMAGE` raises events: 0x10 for each attack and
0x0c for each saving throw. An attack (`60f4:0ffb`) now also misses when
its roll is negative after the effects. The timers reach every handler,
with flag 1. Handlers work through bytes of the original's data segment,
which keep their values between calls (`cok_rolls`): the saving throw
(`6b2e`), its type and result (`6b43`, `6b44`), the attack roll (`6b3b`),
the damage and its type (`6b30`, `6b31`: 1 fire, 2 cold, 4 electricity, 8
magic, 0x10 acid), the effect a spell is adding (`6b2f`), the spell
(`6b33`), its dice (`6b34`), the rate (`6b32`), morale (`6b3e`), whether
the target can be attacked (`6b37`), whether effects are being cured
(`6b38`), whether the game was saved in the current camp (`5885`: set by
Save, `4b6d:22de`, and cleared when the camp menu returns at `2fd3:344d`,
at startup by `3e99:005b` and `0843`, and by each command of the start
menu but Save and Exit, `4def:01b4`) and the combat round (`714b`).

Handlers ported, by address and effect id: `3f44:0124` (1), `0134` (2),
`0344` (8, 0x2d), `0379` (9, 0x2e), `03ae` (0x0a), `03cd` (0x0b, outside
combat its end and after it took hold), `04b1` (0x0c, 0x26), `05bc` (0x0e), `0625` (0x10),
`062c` (0x11), `065d` (0x12), `0681` (0x14), `07b5` (0x17), `09b3` (0x19),
`0ab8` (0x1d), `0cf0` (0x21), `0f3f` (0x24), `0f78` (0x27), `144c` (0x2a),
`15a3` (0x2f), `16ef` (0x31),
`173a` (0x32), `176b` (0x36), `179c` (0x37), `17a3` (0x38), `17c6` (0x39),
`17ea` (0x3a), `1891` (0x3b), `1a72` (0x3d), `1b18` (0x3f), `2665` (0x49,
its text with the breath of monsters), `29d5` (0x4d, outside combat),
`320f` (0x59), `3258` (0x5b), `32a8` (0x5e), `3328` (0x5f), `334c`
(0x60), `325f` (0x5d), `3361` (0x61), `336f` (0x62), `3386` (0x63), `33a7`
(0x64), `3406` (0x65), `3449` (0x66), `3450` (0x67), `34f9` (0x6b),
`3619` (0x6c), `363c` (0x6d), `3643` (0x6e), `3692` (0x6f, see The
battlefield), `303c` (0x52, see The battlefield), `3768` (0x71), `37e8`
(0x74), `386a` (0x76), `3876` (0x77) and `3881` (5, 0x13, 0x18, 0x5c). Many
do nothing but mark the character for other code, and all but a few act the
same when their effect ends. 0x5d, 0x64 and 0x67 look at what the selected
character strikes with (`3f44:13a9`): its readied weapon (slot 0), or for a
missile weapon its readied arrows or quarrels (`6346:3111`, by the weapon
type's flags), and 0x65 at the weapon itself. Handlers that print speak
through the `say` hook of `cok_effects` (`6346:1883`), and those that add an
effect with text through its `flash` hook (`6346:228c`, see The combat
screen):
0x27, haste, the first time it runs (its value's bit 4 clear) sets the bit
and the character "ages" a year (`+0x60`); 0x17, Spiritual Hammer, gives a
character with no hammer and fewer than 16 items counted (`+0x142`) one at
the end of its items (type 6, name parts 6 and 0x79, bonus 1, `+0x3d` 0x17,
`+0x3e` 0x80) and says it "Gains an item"; its search for the new hammer, to
ready it in an empty weapon slot, steps past it, so it is never readied.
The timers, each run when its effect ends, and 0x3e at a constitution of
20 and up: `05c3` (0x0f, Slow Poison's poison) and `1504` (0x2c, wasting)
each add themselves again for ten minutes with their value (`3f44:00ba`,
unless effects are being cured, `DS:6b38`), then one with more than a hit
point takes 1 of type 0 through `60f4:1db7` ("takes 1 point of damage from
Magic") and outside combat the party list is drawn again (`6346:07ba`); at
a hit point or none, 0x2c brings 0x1f for good if the character lacks it.
`1469` (0x2b, strength drain) adds itself again for an hour, then a
strength (`+0x11`) above 3 "is weakened" and loses a point, and one of 3
or less brings 0x1f. `0d19` (0x22, Cause Disease's) runs 0x2b's and 0x2c's
handlers with its own effect, so that the disease becomes both. `0725`
(0x16, Slow Poison's) makes a poisoned character (0x37) "die from poison"
(`60f4:00eb`, status 6) and lose its 0x37, then removes 0x0f while curing
and clears `DS:6b38` after, even if it was set before (as when Neutralize
Poison removes 0x16). `1acd` (0x3e) adds itself again for an hour, then
heals a hit point (`60f4:21ea`, of status 0, 1, 4 or 5 below its most:
"is fully healed" or "is partially healed", `6346:25f9`, and the party list
outside combat); the effect comes back even when a constitution recomputed
below 20 removes it, so it stays. Each step of time up to an hour outside
camp so takes a point of strength, a hit point or heals one; in a camp's
rest the chain stops after its first end, since the effect a handler adds
has no flag set (see below), in the original (DOSBox-X, `b_timers.py`) as
in the port. They all speak through the `say` hook,
deal damage and kill through the `damage` and `kill` hooks
(`cok_cast_damage`, `cok_combat_kill`) and draw the party list through
`party_list`, in a battle or out of one. `26a5` (0x4a, Sir Lebaum's), at
event 0x0e of the computer's turn, rolls a d20 and on 1 runs Dispel
Magic's spell handler (`5b04:320f`, through `DS:6ede`, `cok_cast_dispel`)
with no cast around it: the selected character casts, at the caster
level of whatever spell `DS:6b33` last held, on the targets, count and
cell the last cast or breath left (`DS:6fea` on, `710f`), which may be an
earlier battle's; one freed since stops the run, as one taken off (NULL)
does. Applying 0x6b (`34f9`), which no event, spell or item does (only an
effect a file gave), turns the character on the second combatant listed
around it, as berserk does in combat but in any mode and without its text:
the computer controls it (`+0x18b`, `+0xe7` 0xb3, or 0xb2 for another NPC)
and it takes the side against that one's; outside combat it writes through
the combat record combat freed, and the port stops. Ported with the
rounds, for a
battle (see The rounds): `00f7` (0x1b, 0x1f, 0x33-0x35), `016a` (3),
`06b2` (0x15), `09e8` (0x1a), `0a2f` (0x1c), `0ae0` (0x1e, the stinking
cloud), `0d7c` (0x23), `1412` (0x29), `23a4` (0x47; turning a spell
back with the spells in combat), `24f4` (0x48), `26bf` (0x4b) and `2b5f`
(0x4f); with the attacks (see Attacks, movement and death): `0208` (0x07),
`04c2` (0x0d), `0bc9` (0x20), `0f50` (0x25), `1b55` with `1c21`, `1c34`,
`31bc` and `31cf` (0x40, 0x41, 0x56, 0x57), `1dc5` (0x43), `1f97` (0x44),
`000d` with `200b`, `3009` and `31e2` (0x45, 0x51, 0x58), `349c` (0x69),
`36e1` (0x70), `376f` (0x72), `37b0` (0x73), `37fc` (0x75), `15cd` (0x30),
`18b9` (0x3c), `1c54` (0x42), `272a` (0x4c), `34db` (0x6a), and the
combat parts of `03cd` (0x0b taking hold) and `29d5` (0x4d); with the
clouds and pools (see Clouds, pools and breath): `0ff7` (0x28), `2037`
(0x46) and `2cad` (0x50), and a monster's breath or attack, which
`5b04:58ed` installs at startup and event 0x0e of the combat AI runs: 4
(`5b04:4ab8`), 6, 0x53 and 0x68 (`546e`), 0x4e (`4dc2`), 0x54 (`4eea`), 0x55
(`50cf`) and 0x5a (`5227`); outside a battle those that write a combat
record or read the map stop the run, as the original writes through
records it has freed (the ends of 0x28 and 0x50 find no cloud there and do
nothing), and so does 0x47 turning a spell back (`3f44:23a4`): it reads
the cells (`6beb:0bcb`) from the last battle's tables, copies its
missile's pictures into the buffer that battle freed (`DS:719e`,
`6346:1b5b`; NULL before any battle) and centres the view over the freed
combat map (`6346:1ba6`, `6beb:096b`): outside a battle it reads what
that battle freed, so the port stops there too. Every handler
of the table is now ported. 0x78's is the item routine `3f44:3888` (see
View), which takes the effect it is given for an item and adds or removes
the effect of id `+0x3d`; no item, monster or spell gives effect 0x78
(only a file could), and run for its own effect it reads that id 61 bytes
into a 9-byte record, in whatever the heap holds there: the port stops.
While `DS:713c` is set `60f4:01a8` calls that routine, entry 0x78, for any
id; only readying or unreadying an item with a power (`546c:1d96`) and
Remove Curse (`5b04:35f5`) set it, just before their call, and the routine
clears it first, so the port does their part there (`items.c`, `cast.c`)
and never has it set. Id 0 has no handler: the original calls
`0000:0000`. A call that reaches a stop fails, naming the handler; `DAMAGE`,
the clock and the battle then end the run with `COK_ECL_EFFECT_FAILED` and
log the reason as an error.

`57e4:0549` calls `57e4:0171` with the unit and count once the clock has
moved. It turns them into minutes as a word, which wraps past 45 days,
with unit 0 counting as minutes, and counts them down ten at a time. Each
pass walks the members whose flag at `DS:46b4` (by position) is set,
clearing it: an effect with no time skips, one with more time left than
the pass loses it and sets the flag again, and one with no more is
removed as `cok_effects_remove` does. The walk stops after the effect that
was last when it began, so effects that removal handlers add are not
counted down in that pass. Outside camp (mode 2) every flag is set first;
in camp nothing is counted while none is set. When the effect that ends
is second in the list and was not the last when the walk began, the walk
starts again from the first, which loses the time twice; the port keeps
this. (Ending as the last, it has already stopped the walk.) When a
handler removes the effect the walk goes to next, the original reads it
after freeing it: FreeMem (`1a46:03d3`) writes the block's size over its
next pointer (`0000:0100` for one that was last, the interrupt vectors).
At the heap's top the effect's first bytes survive: the walk takes it for
the last, ends it again, its unlink finds it in no list, and freeing it
again stops the game with runtime error 204. Elsewhere those bytes are
the free list's link, which the walk takes for the time left and may
count down, corrupting the heap, before it walks the interrupt vectors.
The port fails. In ordinary play that happens only when Slow Poison
lapses, 63 hours after it is cast (also in a battle), on a character
still poisoned: 0x0f puts itself back at the end each time, so 0x16's
handler removes the 0x0f the walk goes to next, unless an effect added
between the poison (0x37) and 0x16 makes the walk start again from the
first. The walk also compares the effect that was last when it began,
and the one before the effect that ends, with what it meets: when a
handler has removed one of them and an effect made since comes up, the
original's comparison depends on where TP's GetMem (first fit) put that
one, perhaps in the freed block, which the port cannot know: it fails
there too. The original in DOSBox-X (`slow_poison.py`, 0x16 cut to 30
minutes, and `b_slow_poison_63h.py`, the whole 63 hours rested), with the
freed 0x0f not at the heap's top: the character "dies from poison"
(status 6, no hit points, no effects left), the rest goes on to its end,
the camp takes keys and a Save writes every file, no runtime error, the
interrupt table untouched; but the walk counted the minutes down from the
free list's link (`53dd:0000` became `53dc:fb00`), so the free list runs
into garbage, for a later `GetMem` past its first block to hand out.

The port keeps these quirks of the handlers:

- 0x5e (`3f44:32a8`), a racial bonus, adds 1-5 by constitution (4-6 ... 18-20)
  to saving throws of types 0, 2 and 4, on top of the change `66c2:08a6`
  makes to throw 0. For a constitution outside 4-20 the original adds an
  uninitialized local, whatever earlier calls left on the stack, which
  depends on the opcodes and members before; the port fails.
- 0x36 (`3f44:176b`) doubles cold damage unless the game was saved in the
  current camp (`DS:5885`), where 0x32 (`3f44:173a`) tests the failed save
  for fire.
- 0x11, 0x21 and 0x39 change the armour class in place each time they run,
  until the stats are next recomputed.
- 0x08 and 0x09 test the alignment (`+0x10a`) of the selected character,
  the attacker in combat, whoever is selected otherwise; with none selected
  the original reads through NULL and the port fails, as for 0x19 and 0x2f.
- 0x3d makes 1 damage of 1 die 255 when the fire is magical.
- 0x59's miss is used up by the first attack roll and comes back only at
  the start of combat (a roll of 0 in round 0).
- 0x6e divides by 5 the knight levels less 1; below 1 that overflows, a
  runtime error 200, and the port fails.
- 0x12 and 0x5f roll a d100 even when nothing is being resisted.
- 0x3e, a constitution of 20 or more's, puts itself back when it is
  removed, also when a constitution recomputed below 20 removes it
  (`60f4:1743`), so it keeps healing a point an hour.
- 0x16 clears `DS:6b38` after removing 0x0f, even while a cure has it set.
- 0x22 runs 0x2b's and 0x2c's handlers with its own effect: they add
  themselves with 0x22's value, and the disease becomes both.
- 0x0f and 0x2c hurt only above a hit point; neither kills.

A differential test ran the original routines in an 8086 emulator against
the port on random characters, items and effects: every ported handler
with both flags, the dispatch for every event, attacks, saving throws,
removal (with the strength and charisma recomputed) and the timers, with
the stats recomputed first in half the cases so that weapons are readied.
Of 18,000 cases, the 17,120 the port carries out agreed in the records,
item and effect lists, the working bytes, the timer flags and the random
numbers drawn; the rest reach what is not ported or what the original
mishandles. It is not part of the repository.

The timers' handlers, 0x4a and 0x6b were tested the same way, on
battlefields both set up as for the attacks: effects of those ids added
and removed, the timers run, handlers run with flag 0, event 0x0e, the
spell's targets set as a cast leaves them, curing set, the mode switched
to 4 and 2 between, records poked, damage and deaths. Of 4,000 cases
(2,000 mixed, 1,000 with Dispel Magic cast from 0x4a, 1,000 with Slow
Poison's timers) 2,887 agreed and none differed; the rest stopped where
the original misbehaves: a handler run with no effect, whose value it
reads at `0000:0003` (made on purpose, 347), the walk reading an effect
freed (mostly Slow Poison's end, 696) or meeting one the heap may have put
in a freed one's block (14, where the emulator's own heap ran out on 8),
0x6b with no one listed, 0x4a's Dispel removing itself again. In the
agreeing cases 0x0f's handler ran 4,662
times (2,017 hurting), 0x2c's 8,503 (3,152), 0x2b's 2,451 (1,860
weakening), 0x22's 609, 0x16's 643 (57 deaths), 0x3e's 1,418 (282
heals), 0x4a's 20,727 (1,043 d20s of 1, 1,043 Dispels, 582 effects
taken), 0x6b applied 332 times. Outside combat the camp's difftest (see
Camp) ran the rest (`57e4:0e3e`) and the clock (`57e4:0549`) with those
effects on the party: of 5,000 cases none differed (4,470 carried out),
with 319 deaths from poison, 1,662 heals, 2,770 weakenings and 8,381
points of poison or wasting; again the rest stopped at the walk's freed
effects.

Whole battles (`3995:0172`, as for the clouds and breath, see Clouds,
pools and breath) were run with 0x4a given to some monsters (and to Sir
Lebaum, who has it), the party given effects of other values for Dispel
Magic to take, and the timers' effects, short enough to end in the
battle, given to the party and some monsters, the computer's turns or the
player's keys: of 2,000 battles 1,667 agreed and none differed (an earlier
run's one difference was the walk meeting a new effect where the original's
heap put it in the freed last one's block, now a stop). The rest stopped
at the walk's freed effects (202), Ready given no item (45, the harness's
doubly readied weapons), 0x4a's Dispel reading a first target taken off
(9, the interrupt table at `0000:00e3`), the walk meeting a new effect
(4), the earlier parts' stops, or both did. In the agreeing battles 0x4a's
handler ran 9,297 times, 447 with a d20 of 1, its Dispel taking 525
effects (274 times "is affected"); 0x0f's handler ran 1,249 times (892
hurting), 0x2c's 1,579 (1,185), 0x2b's 857 (857 weakening), 0x22's 408,
0x16's 198 (158 deaths), 0x3e's 806 (671 heals); the timers 11,192
times. Every block's `eclplay` transcripts (17,843 runs with no
`--combat`, pass, won, melee and auto) are the same as before, but for
the eight of Sir Lebaum's battle on auto, which roll 0x4a's d20 (none of
them a 1) where they skipped it.

### Death Knights' effects

Death Knights' handlers (`DS:be7e`, overlay `504d` and a few in `5dc5`)
were compared with Champions' instruction by instruction (calls followed
through their thunks, strings by their text). Where one is Champions' but
for a constant, its entry (`cok_rules_effect`) holds the constant
(`value`), its words (`text`) or that it does nothing outside a battle
(`battle`, a first `cmp byte [8c4e], 5`; `call_handler` returns before
the handler when the mode is not 5); where it differs, it has a kind of
its own (spells.h, after `COK_FX_KENDER_BATTLE`). Its working bytes are
Champions' moved by 0x533c (`DS:be6a` the saving throw, `be6c` the
damage, `be6d` its type, `be78` the attack roll, `be7b` morale), with
the movement byte `be6f` (0x27, 0x2a) between the rate and the spell.

| Ids | Routine | In the port |
| --- | --- | --- |
| 0x09 | `0394` | nothing |
| 0x0d, 0x39, 0x3c | `04a5`, `1044`, `10b4` | `COK_FX_ARMOUR`: the armour class, front and back, by the value (-2, -4, +4) |
| 0x0f | `04c4` | Champions' poison; its party row (`65cd:0959`, one member's) is drawn as the whole list, the same pixels |
| 0x16 | `067c` | `COK_FX_POISON_COLLAPSE`: one poisoned (0x37) "collapses from poison" (status 4) and keeps it; 0x0f goes |
| 0x17 | `06dc` | the hammer, "gains an item" |
| 0x1a, 0x23, 0x25, 0x69, 0x72, 0x73 | `0900`, `0bab`, `0d5e`, `2404`, `27e6`, `282d` | Champions', in a battle only |
| 0x1e | `09d5` | `COK_FX_NAUSEA`: in a battle, "is coughing", no items or spells, the stats recomputed, the armour class front and back 2 worse |
| 0x20 | `0a7e` | `COK_FX_UNSEEN`: a holder with creature flag 0x40 and without 0x18 cannot be targeted, the attack roll 4 lower |
| 0x2a | `0f0d` | `COK_FX_SLOW_MOVEMENT`: applied, the rate and the movement halved |
| 0x3b, 0x3f | `108d`, `111e` | the minor globe: spells of a level below the value (5, 4) |
| 0x41, 0x4b, 0x53 | `1228`, `1c64`, `1f79` | `COK_FX_TYPE_NONE`, `_DOUBLE`, `_HALF`: damage of the value's types none (electricity), doubled (cold), halved (electricity, cold) |
| 0x42 | `123d` | `COK_FX_PLUS_WEAPONS`: 0x67's, an item helping only with a bonus of 3 or more |
| 0x43, 0x44 | `1815`, `1836` | `COK_FX_DRAIN_LEVELS`: the target of the holder's attack loses 1 or 2 levels (the drain hook, below) |
| 0x46 | `186e` | `COK_FX_FLAMES`: a burst on the holder (`65cd:39bd`, slot 0x18), 1d6 of fire to each listed within a cell, either side |
| 0x48, 0x4d | `1a79`, `1cf2` | Champions' terrible presence (its fearless 0x5c) and berserk (`DS:4558` is a constant 0xff) |
| 0x4a | `1bff` | `COK_FX_FIRE_TOUCH`: in a battle, the target takes 2d6, half on a save of type 4 |
| 0x4c | `1c7e` | `COK_FX_FIRE_HEALS`: magical fire heals as much as it hurts; no fire takes hold |
| 0x4e | `5dc5:4f97` | `COK_FX_CAST`: the holder casts the value's spell, Fireball, as the computer does (`5dc5:1298`), and loses 0x4e if it did |
| 0x51 | `5dc5:4fd3` | `COK_FX_GAZE`: targets as the computer's spell 0x0a picks them, "gazes!", the missile, then that spell's handler |
| 0x54 | `1f9c` | `COK_FX_HEAL_ROUND`: 3 hit points up to the most |
| 0x55 | `5dc5:508c` | Champions' lightning breath, "breathes " |
| 0x57, 0x65 | `2005`, `22bc` | `COK_FX_IRON_GOLEM`, `COK_FX_FLESH_GOLEM`: healed by magical fire (by the damage) or magical electricity (by its dice), slowed (0x2a) by electricity for 3 minutes or by magical fire or cold for 2d6; no spell takes hold |
| 0x5a, 0x64, 0x68 | `5dc5:51cc` | `COK_FX_DRAGON_BREATH` and the effects section's `breaths` (below) |
| 0x6b | `25bb` | `COK_FX_TURN_BATTLE`: Champions', but applied outside a battle it does what its removal does |
| 0x6e | `26f8` | `COK_FX_TIER`: when it ends, the tier is the value's class's level (3) / 5 + 1 |
| 0x75 | `286b` | `COK_FX_DISRUPT_UNDEAD`: in a battle, applied: a target with creature flag 2 takes double, else one of a kind of undead "is disrupted" |
| 0x02 | `0124` | `COK_FX_STUN_HIT` (olin's quarterstaff): in a battle, applied, on a hit (event 2): unless the target makes a saving throw of type 1, the entry's effect (6, held) for 3 minutes, "is stunned" |
| 0x04 | `0219` | `COK_FX_EVIL_AVOID` (Dispel Evil's, on its caster), applied, being attacked (event 0x0b): an attacker of creature flag 1 (`+0x7a`) 7 worse to hit |
| 0x15 | `05bf` | `COK_FX_SILENCE_SPELL` (Silence's, shared within a square): in a battle, Champions' silence, then one casting "lost a spell" (row 12), forgets it and its turn ends (the `lose_spell` hook, `cok_cast_lose_spell`) |
| 0x22 | `0aee` | `COK_FX_DISPEL_EVIL`: in a battle, applied, at the damage (event 4): the target of creature flag 1 failing a save of type 4 "is dispelled" (status 8) and the holder loses its 0x04 and 0x22; any other "resists dispel evil" and a pause (the `pause` hook) |
| 0x30 | `0f4f` | `COK_FX_BARKSKIN`: being attacked and at a save (events 0x0b, 0x0c), the armour class, front and back, 1 better in place, and the save 1 better but against a spell being cast (`DS:be70`) whose save is of type 4 |
| 0x3a | `1063` | `COK_FX_MIND_BLANK` (Mind Blank's): a pending 0x0b, 0x23, 0x34, 0x3d or 0x6f cancelled; no event runs it |

The dragons' breath (`5dc5:51cc`, `cok_rules_breath`): its uses become 3
at the first round; acid (type 0x30, a cone 1 wide and 6 long), death
(0x60, 1 by 8) and fire (0x21, 3 by 9) aimed as the computer's spell
0x24 picks; the cone (`5dc5:175d`, Champions' `5b04:1903`) has its
sight's range set to twice its length (the length doubled in place at
`17be`, then copied at `1886`), but its side lines, where no one stands
on the centre line, still come from a byte of the stack: the run stops if
that changes the targets; then "breathes acid" (`DS:22d8` and the word),
and each target saves (type 3) for half of the dragon's most hit points.
The gaze, the cast and the flames go through the breathe hook as the
breaths do (`cok_cast_breath` picks the routine by the entry's kind).

The level drain (`504d:14c7`, `cok_train_drain`): the record "gets
drained" (row 10, no wait, then a pause of the speed), and for each level
the class of the highest level (of equal ones, the one whose level asks
more experience, `65cd:382c`'s rows) loses one; the levels, experience and
most hit points not drained (`+0x4d`, `+0x66`, `+0x6a`) keep the higher;
the experience falls halfway (a LongInt division by 2) between the
experience of the level it now has and the one above; the level's hit
points go (`580d:3193` losing: the dice rolled as for gaining, the base
hit points `+0x6c` and the most `+0x28` less them, the most no higher
than `580d:3454`'s for the levels left, the hit points keeping their
distance below it); the levels are recomputed (`69a8:050b`) and the
spells the slots no longer hold forgotten (`504d:1288`: those memorized
counted by list and level, a mage keeping one or two more of its list by
its order's moon, then those to memorize). With no level left or no most
hit points it "dies from drainning" (status 8), else with no hit points
"collapses from drainning" (4). A class with no experience row (a knight
of no order, a druid) reads `65cd:382c`'s uninitialized local: the port
stops.

`remove-an-effect` (`638c:01c6`) and the kill (`638c:00c8`) are
Champions' `60f4:01e9` and `00eb`. The named ids now given: `BERSERK`
0x4d, `FEARLESS` 0x5c, `LIGHTNING_BREATH` 0x55, and `MAD` 0x20 and
`NAUSEA` 0x1e, which the computer's cloud test (`3f63:06ca`) reads by the
same ids. Every effect id is ported. Checked: `test_death_knights.c`'s
`test_effects` (each entry's constant, the breaths, the battle-only
entries and the words against the routines), `test_effect.c`'s
`test_dkk_handlers`, and in DOSBox-X the first fight with its monsters
changed (`dkk_fight.py`'s drain, mage and dragon flows: wights and
spectres draining, kuo-toas, black wizards casting, a blue dragon); band C's
in `tests/test_band.c` and `test_death_knights.c`'s `test_band_c`, and in
DOSBox-X by the priests flow (Silence's 0x15 shared) and the band flow
(see Death Knights' casting).

## Camp

`src/camp.h` ports the camp from overlay `4888` and the rest loop of
overlay `57e4`; `src/magic.h` the camp's Magic menu, with the spell lists of
overlays `546c` and `5b04`. `Encamp` (`2fd3:3403`) runs the block's camp
vector (`4b3d`), then the camp menu; if an encounter interrupted a rest, it
redraws the screen and runs the rest vector (`4b3f`), which scripts use to
start the encounter. Then it shows the view, clears `DS:5885`
(`2fd3:344d`), so that the game counts as saved only in the camp it was
saved in, and outside 3D areas marks the party on the overland map
(`4877:0005`, see The overland map). As in the original, a `NEWECL` in either vector
enters the new block only after the next command. When the camp vector
ends the run (`DS:4b57`, as when the party is killed), the original opens
the camp menu all the same; the port does not.

The camp menu (`4888:2c31`) sets the mode to 2, which shows `camping` on
the status line and keeps the effect timers from counting down members
with no timed effect, clears the rest time and the day's healing count,
and redraws the screen for camp (`6346:2c17`): the frame, the party list,
the status line and the camp's picture, `PIC` record 0x3b, which the
menus draw at cell 3, 3 and animate while they wait (see Playing). It
prints `The party makes camp...` on row 18 in light green and unmarks
every spell marked to memorize or scribe (`4888:06b8`). The menu reads
`Save View Magic Rest Alter Fix Exit`, prompt in light magenta and items
in white and light green as the adventure's, and takes keys until Exit or
Escape, or a rest is interrupted. It keeps the item selected by the last
menu of its kind; right after `Encamp` (by its letter or with the arrows)
that is `Save`, highlighted, which Enter opens, in the original (checked
in DOSBox-X) as in the port. Special
keys pick a character (`546c:3334`) and redraw the party list; one whose
scan code is `E` would also leave, but that is NumLock, which never
reaches the game (see Party). On leaving it reloads the small
picture shown before if it was a `PIC` (`4888:2de7`: not a monster's
sprite, which an encounter that ended before its close-up leaves, nor an
empty slot; the camp's picture then stays), unmarks the spells again,
and restores the mode,
the status line, rows 18-22 and row 24, and clears the spell target
(`DS:710b`, `4888:2e28`). `View` shows the selected character (see
View).

`Rest` (`4888:0f05`) sets the rest time to what the member who needs
longest needs to learn its marked spells and scribe its marked scrolls
(`4888:0032`): 4 hours if any, 6 if any is above level 2 (into `+0x58`,
`STUDY_HOURS`), and 15 minutes a level, the levels added as bytes; a level-0
power counts as 1 to memorize but 0 on a scroll. The hours may pass 23. Then
it rests (`57e4:0e3e`): row 17 shows `Rest Time:` and days, hours and
minutes (`57e4:0764`) in light green, the unit chosen in white; the menu
`Rest Days Hours Mins Add Subtract Exit` (`57e4:08a0`) picks a unit, adds 1
(5 for minutes) or subtracts as much, and rests; up and down (8 and 2) add
and subtract. Subtracting borrows from larger units, and with none to
borrow from up to the days, clears the whole time (`57e4:05ee`); each
change then carries full units once into the next with the clock's own
routine (`57e4:0459`), so hours that carry into a day move the moons a
day, and months fold into days, at most 99 (`57e4:0517`). The rest runs
in ticks of five minutes while minutes, tens, hours or days are left, so
it passes the time rounded up to five minutes. Each tick, if a key is
waiting, it asks `Stop Resting? `, and the waiting key is the answer's
first; then it takes five minutes off the rest time, passes
them (`cok_adventure_pass_time`, unit 1, count 5, which counts the effect
timers down: at the start every member's timer flag is set), and:

- heals (`57e4:09f0`): at the 288th tick, counted across rests and Fix in
  one camp (`DS:7136`), each member with status 0, 1, 4 or 5 gains a hit
  point, added as a byte, so 255 wraps to 0 (`60f4:21ea`); an unconscious
  or dying character recovers and can act. `The Whole Party Is Healed`
  shows on row 19, whoever was healed.
- learns (`57e4:0c9c`): each member whose timer (`DS:712e`, by position,
  cleared when a rest starts) has run out and whose hours of preparation
  are over scribes its first marked scroll spell above 0x80, else learns
  its first marked spell, saying so on rows 19 and 20 (`5b04:57eb`), and
  times the next at three ticks a level. Scribing sets the spell known
  (`KNOWN_SPELL`, `+0x62` + spell) and takes it off the scroll, which is
  gone once its count (`+0x2f`) drops below 100 (`5b04:575d`, without the
  item count or the stats being recomputed).
- prepares (`57e4:0d52`): every twelfth tick counts down each member's
  hours of preparation, after that tick's learning; at 0 its first spell
  is timed.
- rolls for encounters: every `0x7ed2` ticks, counted in `DS:4b53` across
  rests, a roll of 1-100 up to `0x7ed3` shows `Your repose is suddenly
  interrupted!` in white on row 19 and ends the rest and the camp menu.

`Fix` (`4888:2b44`), if anyone lacks hit points, adds up the cure spells
memorized by members who are okay (1d8 for Cure Light Wounds, 2d8+1
Serious, 3d8+3 Critical), without using them, and rests, with no menu and
no keys, as long as it takes the clerics to memorize their spells a day of
levels 1, 4 and 5 (`4888:28f9`), shortened by the ratio of an estimate of
the points healed to the points lacking; then it adds those spells' cures
and heals the members in order from the total. An encounter heals no one
and leaves the rest time it set. `Alter` (`4888:2539`) offers `Order Drop
Speed Icon Pics Level Exit` until Exit or Escape: `Order` (`4888:1fc1`)
selects a character and moves it up or down with the arrows (8 and 2), the
first wrapping to the end and the last to the front; `Drop` (`4888:2126`)
asks `Drop from party? ` after saying the character `will be gone`, then
removes it (`4def:3b0a`), counting it out of `0x7f3e` and selecting the
one before, or with the party's last asks `quit TO DOS: `; `Speed`
(`4888:2384`) shows `Game Speed = N (0=fastest 9=slowest)` on row 18 and
offers `Faster` and `Slower` (down and up) within 0-9; `Pics` toggles
`DS:4b4d`, which nothing else reads, and the animation (`DS:4b4f`); and
`Level` (`4888:2272`) sets the difficulty, `0x4cf4`, 1-5, `Novice` to
`Champion`. `Icon` is the combat icon editor (`4def:408b`, see Creating
characters), then the screen redrawn for camp (`6346:2c17`).

The yes/no prompts (`67b5:177f`: `Stop Resting? `, `Quit TO DOS `, `quit
TO DOS: `, `Drop from party? ` and Magic's) select No once, before they
read keys, and ignore Escape, so Yes chosen with the arrows stays chosen
through Escape or other keys until Enter.

`Save` counts saves in `0x4c3c`; every tenth, the original asks a word
from the rule book (`4888:0376`) and quits to DOS on a wrong answer, which
the port logs as unported and passes. Then it asks `Save Which Game: `
over `A B C D E F G H I J` (`4b6d:22de`), keypad digits not directions;
Escape cancels, but the camp then asks `Quit TO DOS ` all the same, and Y
quits. `cok_camp_save_game` writes the game as `cok_adventure_restore`
reads it (see Party), after setting the speed (`0x4bfc`), pictures and
animation (`0x4bff`, 2 × `DS:4b4d` + `DS:4b4f`) and the ECL file
(`0x7f12`): the modes as they are, so a camp's save holds 2 and the mode
before it; the wall sets as `DS:6d8a` holds them, from record 0 at slot 1
at startup; and the characters as `CHRDAT<letter><n>` beside it
(`4b6d:0bed`): the 409-byte record, the items (`.STF`) and the effects
(`.SFX`), each file erased first and written only if there are any. It
shows `Saving...Please Wait` on row 24 in light green; with no party, it
shows `WARNING: Problem Saving Characters` in yellow and waits for a key
before writing the count and names (`4b6d:267d`), then writes a party of
none. The original's disk, volume label and free space checks are left
out. It checks no write: a file that cannot be written does not stop the
others, and `DS:5885` is set after all (`4b6d:2809`); the port does the
same, logging each failure as `error:`. Saving SAVE/'s
games again gives the same bytes but for those the original writes from
memory: the far pointers in the records (`+0xe3` `EFFECTS`, `+0x143`
`ITEMS`, `+0x147`-`+0x17a` `ITEM_SLOTS`, `+0x17f` `NEXT`, `+0x183`
`COMBAT`), items (`+0x2a`) and effects (`+5`), 0 in the port's files, and in
the `.DAT` the bytes after each name and the slots past the party, which
hold what was on the original's stack; and spell 8 of game A's clerics (see
Derived stats). After each character's files, its roster copy is erased
(`4b6d:0a80`, see The start menu).

`Magic` (`4888:1c32`) offers `Cast Memorize Scribe Display Rest Exit`
until Exit or Escape for the selected character. `Cast` casts (see
Casting). `Rest` is the camp's. A character whose status is 1 or that
cannot act is `in no condition to` memorize or scribe (`4888:08d8`, in
the text window with its name, `6346:1883`).

`Memorize` (`4888:1098`) first lists the spells marked (`Spells to
Memorize`) and asks `Memorize These Spells? `: No unmarks them all and
opens the grimoire, anything else keeps them and leaves. In the grimoire,
rows 18-22 show the spells left today (`4888:0b24`): cleric, druid,
granted and magic-user spells of levels 1-5 (`4888:0700`: a day's less
those memorized and marked), blank for none a day, and the bonus spells a
magic-user of an order gets from its moon (`0x4cf8` + `+0x5e` `MAGIC_ORDER`:
1 or 2, less the spells it has marked beyond a day's); with no spells a day
of any kind the character `cannot memorize any spells`. The list (`Spells in
Grimoire`, rows 5-15) holds the known spells (`KNOWN_SPELL`, `+0x62` +
spell) of classes it can use (`5b04:0083`) by level, a heading before each,
without the powers it has memorized. A spell picked with one of its level
and class left, or a magic-user's while bonus spells are left, is marked in
the first free byte (`MEMORIZED`, `+0x1e` on) and the bytes sorted by spell
(`4888:0fb2`), which puts the empty ones first. On leaving, the marked
spells are shown again with `Memorize these spells? `.

`Scribe` (`4888:132b`) does the same for the spells of scrolls (`Spells
on Scrolls`), listed when the scroll's order bit (`+0x35` 0x20 for order
1, 0x10 for 2) matches the character's (`+0x5e`, `MAGIC_ORDER`) and its low
three bits are clear, which effect 0x10 clears for good (`5b04:0981`). A
spell known already says `You already know that spell`, one marked `You are
already scibing that spell` (sic), and one of a level and class with no
spells a day `You can not scribe that spell.`, on row 24 for a moment
(`6346:1827`). Otherwise the first byte of any item equal to the spell is
marked, scroll or not; a sword holding the spell's id is marked instead
of the scroll, and stays marked. With no scroll to copy, the character
`has no copyable scrolls`. `Display` (`4888:17ae`) lists each member's
name and its spell effects, named after the first spell of 1-0x38 that
adds them (byte 10 of the spell table), or with their own text for
eighteen others, or `<No Spell Effects>`, in the open frame.

The lists (`546c:34ec`, `5b04:027a`, `5b04:0b21`) show `NAME's Spells
...` on row 1, in the frame of `1128:0384` (Memorize, keeping rows 17-22)
or `1128:077c`, with the menu `Choose Spell: ` and `Memorize` or `Scribe`
(see `cok_menu_rows`). Marked spells show ` *` before their names, and a
spell memorized more than once its count. The spell names (`DS:20a0`, 41
bytes each) and the spell table (`DS:31b3`, 16 bytes each: class, level,
and the effect at byte 10) are the original's, checked against
`build/START_FULL.EXE` by `make test`.

The port keeps these quirks of the original:

- `4888:28f9` starts its estimate of the points healed from an
  uninitialized local, which holds 14, the count of the `Move` before it,
  and keeps the times of the last member who is okay for one who is not,
  counting them again. An estimate 256 or 512 times the points lacking
  makes a ratio of 0 as a byte, a division by zero: Fix then stops with
  `COK_ECL_DIVIDE_BY_ZERO`.
- A rest started again (Rest, or Fix) restarts the learning timers, so a
  spell whose wait was interrupted is learned on the next rest's first
  tick.
- A used-up scroll ends the scribing walk, which reads its freed next
  pointer, cleared by `6346:1697`, but first the rest of its own spells.
- Pics takes special keys by their scan codes' letters: the down arrow
  (0x50) toggles the pictures and F7 (0x41) the animation; the save's
  letter takes F7-F10 as A-D, Home, up and PgUp as G-I.
- `4888:0700` counts a power granted to one without a cleric level (a
  knight above level 5, or a former cleric, as `5b04:0083` allows) against
  an uninitialized local. In Memorize's call it holds the low byte of the
  return offset of the `546c:34ec` call before it (`1098:116c`), 0x71, so
  such a power can be memorized many times; the emulator confirms it. A
  Turbo Pascal overlay manager that unloaded overlay `4888` during that
  call could have rewritten the return address; the port takes 0x71. In
  the table's own call (`4888:0b24`) the byte is never shown.
- `4888:1098` writes past the 58 bytes when all are full; the port fails
  with `COK_ECL_UNDEFINED`, as it does where `4888:0700`, for a level or
  class past the spell table's, reads a far pointer, where a list would
  show a spell with no name (ids 0 and 0x6c-0x7f) or a level past 9, or
  hold more than 58 spells or 49 scroll spells (the game's
  `spells.list_spells` and `list_scrolls`), and where scribing frees a
  readied scroll.

A differential test ran the original routines in an 8086 emulator
against the port on random parties, items and effects: preparation times,
unmarking, the rest time arithmetic, healing, sorting, spell slots, whole
rests and Fix without the menu (with the clock, moons, effect timers,
learning, scribing, healing, encounters and the random numbers drawn, and
in half the rests the stats recomputed first, so that used-up scrolls
renumber readied items), the spell lists of every kind and the
spells-left table. Of 8,000 cases, the 7,407 the port carries out agreed
in the records, items, readied slots, effects, rest time, timers, clock,
moons, messages, lists and random numbers drawn. The 593 it refused reach
spells with no name (430) or levels past 9 (20), a far pointer (32), or
`4888:0700`'s uninitialized byte called outside Memorize (111), which
random data makes common. It is not part of the repository. The
interactive rest and the menus were tested in the port only.

### Death Knights' camp

Death Knights' spells lie in a block of their own (`.WIZ`); one without
it (a fighter, a thief) `cannot memorize any spells` (`4381:0f67`) and
has no table (`4381:09c7`) and no spells left (`4381:0664`). Its Magic
menu (`4381:1a34`) keeps a selection of its own, from Cast each time it
opens (`levels.magic_selection`). Memorize lists the spells marked with
the prompt `spells OK? ` and `Keep` (`5dc5:016e`'s mode 5): picking keeps
them, Escape unmarks them and opens the grimoire; no question is asked.
The table shows levels 1-8 (as many as the record's spells a day) from
column 14, labelled `    Cleric :` ... `Magic-User :` and `Bonus      :`
(`DS:2e9f`-`2ef3`), after clearing rows 18-21 (`110f:0584`) with no name
said (`levels.table_name`). A power granted to one without a cleric level
counts its spells left from a local of `4381:0664`'s own, which the port
does not know (`levels.granted_stale` 0): memorizing one stops the run. The lists' prompt is `Choose Spell:` (`DS:2fd9`).
Scribe (`4381:115d`) asks `scribe these spells? ` (`DS:30c0`) before and
after, from Yes in light magenta, with its words in lower case (`you are
already scribing that spell`, `DS:2f65`). The camp's start and end unmark
only the scrolls (`4381:0626`, `05b0`): spells cast, marked to be learned
again, stay marked. Display (`4381:15aa`) names the effects of spells
1-0x3f by the spell (`spells.display_last`) and twenty others by texts of
its own (`4381:148c` on), its blank rows headings too, in light magenta
(`DS:c149`), so that the list starts on its last row. The rest says `The
whole party is healed` (`DS:2560`) and `your repose is suddenly
interrupted!` (`DS:25a9`). Rest (`4381:0dee`, as `4888:0f05`) rests as
`57e4:0e3e` does (`5b7d:0d51`), but for its menu and question. The menu
(`5b7d:081d`) is read by index (`6a69:0f1b`, all seven items, the keypad,
no picture), from whatever item the menu before left selected: the camp's
menu (`4381:3106`) does not set the first, as Champions' does, so it
starts on Rest's place, `Mins`, and from Magic on `Add`; Enter takes the
item selected, which is not always Rest. Up and Down (8 and 2) add and
subtract; Left and Right (4 and 6) move the selection only, never the unit
(Champions' cycle it); any other special key, Escape among them, exits.
With a key waiting it asks `Stop resting? ` (`DS:10a1`, `6a69:2048` from
Yes, `DS:57c0`): the waiting key is the answer's first, and Enter stops.
After Save the camp asks its start menu's `Quit to DOS ` (`DS:124c`), from
No; every tenth save (counted in `0x4cf6`) the original asks a word from
the rule book (`4381:0337`, three tries), which the port logs as unported
and passes, as Champions'.

The camp's menu (`4381:3106`), Magic's (`4381:1a34`) and Alter's
(`4381:21df`) are read by index (`6a69:0f1b`, `cok_camp_pick_menu`), the
small picture shown and animated: an item's first letter picks it, as in
Champions, but a special key's scan code is no letter (NumLock's `E` left
Champions' camp). The camp keeps its own selection, from `Save`, and
Escape is its Exit; other special keys pick a character (`580d:2bb2`,
which keeps the one selected for any key but Up and Down, between
`65cd:0959`'s redraws of the two rows). Magic starts from `Cast` each time
it opens; a special key picks, Escape too, and Escape then leaves.
Alter, `Order Drop Speed Icon Level Exit` (`DS:371e`: no `Pics`), starts
from `Order` each time, without the keypad, Escape its Exit. `Order`
(`1d98`) asks `party order ` (`DS:30df`) over `Select` or `Place`, and
`Exit` (`DS:33f5`), with a selection of its own: while moving, any special
key redraws the party list after Up or Down has moved the character, and
Escape leaves then too. `Drop` (`1f14`) says `will be gone forever`
(`DS:110a`), and with the party's last asks `Quit to DOS ` (`DS:124c`).
`Speed` (`20d3`) shows `Game Speed (0=fastest 9=slowest) = N` (`DS:168e`)
and asks `game speed: ` (`DS:30f4`) over those of `Faster Slower Exit`
(`DS:340a`) it offers, from its own selection; special keys but Up, Down
and Escape do nothing. `Level` (`2045`) starts from the difficulty, without
the keypad; a special key, Escape too, picks a character and keeps it.
`Icon` redraws the screen after the editor (`65cd:2db0`). The words are
`levels.alter_menu`, `order_prompt`, `order_menu`, `drop_gone`,
`drop_quit` and `speed_before`, `speed_after`, `speed_prompt`,
`speed_menu`.

Fix (`4381:2fd4`, `levels.fix` `COK_FIX_DEATH_KNIGHTS`) does nothing where
the area forbids magic (var `0x4be5`) or no one lacks hit points. It adds
up the spells memorized by members who are okay and have a spells' block
(`22a9`), without using them (`22db`): Cure Light and Cure Serious Wounds
2d8+1 each (Light as Serious), Critical 3d8+3, Heal 4d8+5, and counts Heal,
Neutralize Poison, Raise Dead, Remove Curse (`0x2b`, `0x59`), Stone to
Flesh and Cure Blindness. It rests (`2537`, as `4888:28f9`, with the
estimate from the stack's 14) as long as those of wisdom above 3 take to
memorize their spells a day of cleric level 1 and druid level 2 (15 and
30 minutes, 4 hours before; 27 to the estimate each), cleric levels 4 and
5 (60 and 75, 6 hours; 34, or 78 with level 5) and 6 (90, 8 hours; 90),
the rests setting `DS:c46f`, which keeps them from learning or scribing
anything (`5b7d:09b9`, `0aa3`), the hours of study counting down. Then
those spells add 1d8, 2d8+1, 3d8+3 and 4d8+3 each (`23f3`), and each
member in turn is cured while the spells last (`2a73`), each cure said on
row 10 with a pause in its code's words: Heal for one of effect `0x3d`,
which goes with its blindness (`is healed`); Stone to Flesh for the stoned
(status 7: okay, 1 hit point, `is changed back to flesh`); Neutralize
Poison for the poisoned (`0x37`: as the temple's, `is detoxified`), which
comes first, so that a dead member poisoned is made okay, not raised;
Raise Dead for the dead (status 6) of race 3 and above (`has been raised
from the dead`; one of race 2 counts as needing it for the second rest's
time all the same, `2ebc`; as Champions' temple's
`340d:0688` on Death Knights' fields: a point of constitution, and from 14
the hit points above the base divided by its classes 0-6, off the most
unless at 17 or more with no level of class 2, 3 or 7); Remove Curse for
one of Bestow Curse's `0x24` (the spell's `5dc5:311b`, which cures `0x2b`:
`0x24` stays; `is relieved of a curse`); and Cure Blindness for one blind
whom Heal did not cure, which cures the selected character (`DS:a2c2`),
`regains sight`, and uses no spell. The party is healed from the total in
order (`2947`) and redrawn, the rest time put back; then it rests again
(`2ebc`, `2779`) for those still in need, as many as the spells left and
the spells a day of each cure can treat (Heal cleric 6 and Stone to Flesh
magic-user 6, 90 minutes each; Raise Dead cleric 5, 75; Neutralize Poison
cleric 4, 60; Cure Blindness cleric 3 and Remove Curse cleric 3 and
magic-user 4, 45), and cures again, healing no one. An encounter in either
rest ends it, leaving the rest time it set.

`tools/dosbox/checks/dkk_levels.py`'s flow `fix` plays Fix on saved game
A with a member dead, one stoned, poisoned, cursed, blind and of `0x3d`,
the cures memorized and spells of levels 5 and 6 a day: the screens 0
pixels apart, saved game C (its clock) and every character file the same,
and RandSeed. Flow `memorize` checks camp, Magic,
Memorize's list of the spells marked, the table and grimoire, and Display
against the original, 0 pixels apart. Display's list is Death Knights'
(`6a69:1b82`, from `4381:19df`) with no text, item 0 and last `0xff`,
which `1b82` takes as a signed byte (`1d29`: `cbw`; `1e5d`: `cbw`,
`jng`): its own Next, Prev and Exit from index 0, every pick leaving;
its Exit is drawn from column 0 (Champions' list draws ` Exit` from 1),
highlighted, the selection past its items starting on the first
(`0fab`). Flows
`rest`, `stop` and `interrupted` play Rest in block 16: the menu from
`Mins`, Enter, Add, Left, Up, the keypad's 8 and 4, Down, a rest of 15
minutes and Save (saved game C, its clock); a key waiting at the rest's first
poll (the box traps `5b7d:0e20` to put it there; eclplay's `\[...\]`), `Stop resting? `, Enter,
and again with No, resting out; a day's rest that the block's camp vector
(`0x7ed2` 255, `0x7ed3` 100) interrupts at its 255th tick, and the rest
vector's text; each screen, RandSeed and the clock (`0x4bc6`-`0x4bcc`):
0 pixels apart but the eulogy's portrait's eyes, whose blink a screenshot
catches by the host's timing (36 pixels once). Flow `menus` walks the
camp, Magic, Escape and Enter (Magic again), Alter's Order (Select, Down,
Place, Escape), Drop (No), Speed (Faster, Up, Escape), Icon, Level (Left,
Enter) and Escape, then Save: each screen, RandSeed, the clock, saved game
C and the character files. Flow `saves` plays Save's menu by index
(`46b2:2808`): Escape (Quit to DOS, No), F7 read past, Right twice, C;
flow `load` the start menu's Load (`46b2:1ef0`: Death, F7 read past,
Right, B) and View Character's Pick Character (`580d:3067`: `E`, then
Escape).

## Casting

`src/cast.h` ports casting outside combat from overlay `5b04`: the cast
(`5b04:1415`), its targets (`5b04:127e`) and the handler of each spell
from the table at `DS:6e3a`, which `5b04:58ed` fills at startup (the
game's `spells.cast`, see The spells and effects).
`cok_magic_cast` (`src/magic.h`) is Cast (`4888:0a0d`), from the camp's
Magic menu and from the adventure's commands. It clears the spell target
(`DS:710b`) and, unless the character is in no condition to
(`4888:08d8`, see Camp, or "cannot cast spells in this area" while
`0x4be5` is set), lists the spells in memory (`546c:34ec`, kind 0, in
the open frame of `1128:077c`, with `Choose Spell: ` and `Cast`) until
none is picked. Each spell picked clears rows 17-22 and is cast; an
empty list says "has no spells memorized" (`6346:1883`). The screen is
redrawn for the mode after a list (`6346:2c17`).

A cast (`5b04:1415`) is made by the selected character:

- Outside combat, a spell whose byte 7 in the spell table is 0 shows its
  name on row 19 and "can't be cast here..." on row 20, both light
  green, and asks "Lose it? " (light magenta, `67b5:177f`); Yes forgets
  it. While an item is used (`DS:711d`, see View) it shows "That Item"
  and "is a combat-only item..." and asks "Use it? ", and Yes counts the
  item as used. Those spells are 2, 4, 9, 0x0a, 0x0f, 0x14, 0x15, 0x17,
  0x19, 0x1b, 0x1f (Knock, which the door menu uses), 0x21, 0x22, 0x26,
  0x28, 0x2c, 0x2d, 0x2f, 0x31, 0x33, 0x37, 0x3c, 0x3d, 0x40-0x42, 0x44,
  0x46, 0x48, 0x49, 0x4a, 0x4c, 0x4e, 0x4f, 0x51-0x54, 0x56, 0x57,
  0x5b-0x5e, 0x62, 0x64, 0x66 and 0x6a.
- A caster with effect 0x4a then miscasts on a d2 of 1 (`5b04:57eb`:
  name and "miscasts" on row 19, the spell's name on row 20), keeping the
  spell.
- Otherwise, from memory, it "casts" the spell, said the same way.
- The targets (`5b04:127e`), by byte 7 (the values the game's
  `spells.target_*` give): 1 the caster; 2 a member picked
  with "Cast Spell on whom" and `Select Exit` (`6346:32c7`, from the last
  target or the caster, after redrawing the screen); 4 the whole party.
  With a target, the caster's invisibility (0x19) ends (`60f4:1408`), a
  spell from memory is forgotten (`6346:161b`: the first byte equal to
  it, so a spell marked to be learned does not count), and the handler
  runs with the spell in `DS:6b33`. Exit picks no one, and nothing is
  used.
- Rows 18-22 are cleared (`6346:196a`).

The caster level (`6346:29fe`, `cok_effects_caster_level`) is 6 for a
character with no cleric or mage level, a knight level below 9 and a
ranger level below 8; otherwise, by the spell's class (byte 0), the
better of the cleric level and the knight level less 8 (0 and 2), the
ranger level less 7, at least 0 (1), the better of the mage level and
the ranger level less 8 (3), or 12 (4, the powers of items). A human
that may use its former class (`66c2:0efb`) adds its former levels; a
mage of an order (`+0x5e`) loses a level while its moon (`0x4cf8` +
order) is in phase 0 and gains one in phase 2 above level 5. While an
item is used the level is 6, but for class 4. Levels are signed bytes.
A spell lasts (`5b04:0f78`) the caster level times byte 5 plus byte 4
minutes, but 0x1a 3780, 0x28 1d6 × 10, 0x39 and 0x3d 5d4, 0x3b 1d4 × 10
+ 40, 0x3f (1d10 + 10) × 10 (2d10 × 10 in combat) and 0x43 1440 (the
game's `spells.durations`).

Most handlers apply the spell (`5b04:1071`) to each target left in the
list: a saving throw if byte 8 is set (of type byte 9), damage if any
(`60f4:1db7`), and the effect of byte 10 for the spell's duration, with
the caster level or a given value (`60f4:20f7`). The effect is first
pending (`DS:6b2f`) through event 9 (magic resistance, then the
target's effects); if cancelled, or saved against with a save of kind
1, the target "is Unaffected". An effect of the same id with time left
is removed first; a permanent one stays and the new one is added after
it. Then the target says the spell's text (`6346:228c`, which outside
combat is `6346:1883`: name and text in rows 18-22, then a pause; in
combat the flash, `cok_arena_flash`).
Damage (`60f4:1db7`) runs event 6, halves or cancels for a save of kind
2 or 1, else runs event 0x14; if any is left and the target can act, it
"takes N points of damage" (or "takes 1 point of damage") "from Fire",
"from Cold", "from Electricity" or "from Acid" by the type (`DS:6b31`
without bit 8), or "from Magic" when the type has no other bit; then it
"Goes Down", ", and is Dying", or "is killed". The party list is not
redrawn.

| Spell | Handler | Outside combat |
| --- | --- | --- |
| 1, 0x69 Bless | `1efb` | the party on the caster's side (`+0x18a`, `5b04:1e34`): effect 1 for 6 minutes, "is Blessed" |
| 3 Cure Light Wounds, 0x3a Cure Serious Wounds | `1f57`, `3ddc` | 1d8, or 2d8 + 1, and 1d8 more first for a caster of deity 4; "is fully healed" or "is partially healed" (`6346:25f9`), and the party list redrawn |
| 5, 0x0b, 0x12, 0x16, 0x1d, 0x4d, 0x67 | `2004` | effects 5, 0x10, 0x13 or 0x18: "is affected" |
| 6, 7, 0x10, 0x11, 0x34-0x36, 0x45, 0x65 | `203e` | effects 8, 9, 0x2d, 0x2e or 0x29: "is protected" |
| 8 Resist Cold, 0x18 Resist Fire, 0x13 Shield | `207d`, `2679`, `2456` | 0x0a "is cold-resistant", 0x14 "is fire resistant", 0x11 "is shielded" |
| 0x6b Burning Hands | `20ab` | fire and magic damage of the caster level, no text |
| 0x0c Enlarge | `21d7` | strength by caster level (18/00, 18/01, 18/51, 18/76, 18/91, 18/100, 19-22) as effect 0x0c if better than the base (`60f4:1592`): "is stronger"; else "is unaffected" |
| 0x0d Reduce | `230c` | unless a save of type 4 is made, the first 0x0c goes: "has been reduced" |
| 0x0e Friends | `23ac` | effect 0x0e of 2d4, rolled first: "is friendly"; charisma recomputed |
| 0x1a Slow Poison | `26eb` | see below |
| 0x1c Spiritual Hammer | `2877` | effect 0x17, then its handler, which gives the hammer |
| 0x1e, 0x32 Invisibility, 0x3f, 0x61 | `28c7`, `3fbd`, `49ff` | effects 0x19 or 0x47: "is invisible" (0x61 no text) |
| 0x20 Mirror Image | `293b` | effect 0x1c with 1d4 images in the value's high nibble, the level below: "is duplicated" |
| 0x23 Strength | `2e4e` | see below |
| 0x24, 0x5a Animate Dead, 0x38 Restoration, 0x47 Cure Critical Wounds, 0x4b Raise Dead, 0x50 Invisibility to Animals | `3000`, `3d8c`, `42a7`, `42c0`, `42e3` | nothing, though the spell is used up |
| 0x25 Cure Blindness | `300d` | 0x21 goes, "is Cured" (`60f4:14b7`), and "can see" |
| 0x27 Cure Disease | `3107` | 0x22 goes; if 0x2b does, 0x2c and 0x1f too, and strength is recomputed (`DS:6b38` set) |
| 0x29, 0x2e Dispel Magic | `320f` | see below |
| 0x2a Prayer | `358a` | the party: effect 0x31, the caster's side in the value's high nibble: "is praying" |
| 0x2b, 0x59, 0x68 Remove Curse | `35f5` | see below |
| 0x30 Haste | `3991` | the first members on the caster's side, as many as its level, curing Slow (0x2a) or else hasted (0x27): "is Hasted", then event 0x12, so each ages a year the first time |
| 0x39 | `3d9b` | cures Slow, else effect 0x27: "is Speedy" |
| 0x3b | `3e53` | effect 0x71, strength 21 if better than the base ("is stronger"), see below |
| 0x3e, 0x63 | `3f63`, `4a40` | heal 2d4 + 2: "is Healed", the party list not redrawn |
| 0x43 Neutralize Poison | `4098` | see below |
| 0x55 Fire Shield | `464d` | see below |
| 0x58 Minor Globe, 0x5f, 0x60 | `4957`, `49a3`, `49d1` | effects 0x3f "is protected", 0x49 and 0x61 with no text |

Slow Poison drops a target of status 1 from the list; a poisoned one
(0x37), whatever its status, gets at least 1 hit point, status 0, can
act, and gets 0x16 for 3780 minutes and 0x0f for 10, each run when
removed; 0x37 stays. Neutralize Poison likewise drops status 1; a
poisoned target gets at least a hit point, loses 0x37, 0x16 and 0x0f
(while curing), says "is unpoisoned" and is okay; another "is
unaffected". Remove Curse removes Bestow Curse's 0x24 ("is Cured", "is
un-cursed"), or else unreadies the first cursed item (`+0x36`), which
stays cursed and counts in the stats until they are next recomputed; for
an item with a power (`+0x3e` above 0x7f) the effect it gave goes
(`3f44:3888`: the first of id `+0x3d`) and every ability is recomputed
(`60f4:1743`); "has an item un-cursed". Dispel Magic removes each effect
of its first target whose value is not 0xff, taking the low nibble as
the level that cast it, on a d100 up to 50, plus 5 a level the caster is
above it or less 2 a level below, as a byte (`DS:4839`, `483a`), and
says "is affected" if any went. Fire Shield asks "flame type: " with
`Hot Cold` (`67b5:03e2`, or a d10 above 5 for Hot for a character the
computer controls); Hot adds 0x32 and says "is protected", Cold adds
0x36 silently, each with 0x70, for the level + 2 minutes; any other key
asks "Abort spell? " with `Yes No`, and Yes ends it.

The port keeps these quirks:

- Magic resistance at caster level 12 and up: the chance wraps, so a
  resistance of 1-4 resists every time. A damage byte left over in
  `DS:6b30` (from an earlier attack or damage) skips the check for
  spells that deal none, as nothing clears it.
- The d2 for 0x4a is rolled for a spell refused outside combat too, so
  "miscasts" can follow "Lose it?".
- Effects of no duration, as invisibility's, stack: each cast adds one.
- Strength rolls 1d4 for a mage, 1d6 for a cleric or thief, 1d8 for a
  fighter (each that applies, the last kept, former classes counting
  above `+0xd7`); for none of those (a knight or ranger alone) the roll
  is an uninitialized local, `[bp-6]`, which is `[bp-0x36]` of
  `5b04:1415`. Cast from memory, that holds the frame pointer of `1415`
  that `6346:161b` (called at `5b04:16fc`) pushed there, and its low byte
  is fixed by the calls above it: the stack starts at 0x4000 (the MZ
  header's SP), main (`1000:0134`) reserves 0x100, and through
  `2fd3:3c28`, `475c:09ec` and `4888:0a0d` (the commands' `Cast`) BP is
  0x3d74, adding 0x74 (116); through `2fd3:3c28`, `2fd3:3403`,
  `4888:2c31`, `4888:1c32` and `4888:0a0d` (camp's `Magic`) it is 0x3d9a,
  adding 0x9a (154). Strength as an item's power does not call `161b`,
  and the byte is the segment of the effect pointer `60f4:1408` left
  there, NULL after its search, so 0. An emulator run of each path agreed,
  and so did the original in DOSBox-X (`e_strength.py`): a knight alone
  of strength 3 gets 0x26 valued 0xd8 (0x74 + 100) and 18/30 from the
  commands' `Cast` (0xd8 & 0x7f gives 18/87, then `60f4:1743` adds the
  score: 21 makes 18 and (21 - 18) × 10 = 30), 0xfe (0x9a + 100) and
  18/100 from camp's `Magic`, as in the port.
- 0x3b adds effect 0x71 whatever the base strength (`3e73` jumps past
  only "is stronger"); on a base of 21 or more its value is the byte
  `[bp-1]` it never set, `[bp-0x31]` of `1415`: from an item, the high
  byte of the return address `16eb`, so 0x16, and strength is recomputed
  with that; from memory, the high byte of the overlay's segment, which
  depends on where the overlay manager loaded it: the port stops with
  `COK_ECL_UNDEFINED`.
- Strength and 0x3b compare against the base strength (`+0x10`,
  `+0x1d`), not the current one, so repeated casts stack.
- Slow Poison and Neutralize Poison raise the dead who are poisoned.
- Burning Hands, from an item outside combat, burns its user for 6.
- Spiritual Hammer's hammer is never readied; and when it is readied and
  then unreadied, its power 0 removes effect 0x17, whose handler then
  takes the hammer away.
- Dispel Magic works on the list's first target each pass (one target
  outside combat). Its second part, the clouds on the combat map (see
  Clouds, pools and breath), runs outside combat too, reading the cells
  around the spell's through the map pointer (`DS:6a2e`), NULL there: the
  interrupt table. The port takes none of those bytes to be a green cloud,
  in which case nothing happens.

In a battle the cast picks its targets with the combat routine
(`432f:2337`) that the battle puts in `DS:6e3a`, flies its missile and
runs the handlers that act on the map: see Spells in combat. The effects
spells add run their handlers when their timers end, outside combat too:
Slow Poison's 0x0f and 0x16, Cause Disease's 0x22 (see Spell effects).
Outside combat 0x47 (from 0x3f) on event 9, when it would turn a spell
back, stops the run with `COK_ECL_EFFECT_FAILED`, for the original reads
the last battle's tables and freed map; and spell ids 0x6c-0x7f, which
only items name, are past the handler table. The
spell target and the trade partner (`DS:710b`, `46b0`), which the
original keeps as pointers into freed records when Alter's Drop removes
a character, are forgotten in the port.

### Death Knights' casting

`cok_rules_death_knights.casting` (`cok_rules_casting`, `rules.h`) holds
what Death Knights' cast (`5dc5:1298`, Champions' `5b04:1415`) and the
rules under it do otherwise; `cast.c`, `effect.c`, `magic.c`, `party.c`
and `arena.c` read it:

- The cast's words are in the data segment (`DS:0f1a`-`0f99`: "Can't be
  cast here...", "That item", "Is a combat-only item...", "Abort Spell"),
  its questions start on Yes (`6a69:2048` with `DS:57c0`), and one holding
  `COK_EFFECT_DISPELLING` never miscasts (`miscast` false). In combat it
  "casts a spell" (`5dc5:552a`, `DS:2304`); outside, the pick is "cast
  spell on whom" (`5dc5:1182`, `DS:3192`) by the pick by index
  (`65cd:33c9`: "Select" alone, see Death Knights' View).
- After the flight, Stinking Cloud, Cloud Kill, the Fireballs and Ice
  Storm burst on the spell's cell (`65cd:39bd`, `bursts`): the screen
  centred there, Fireball's sound, the pictures of an icon slot (0x18-
  0x1b), then 3n + 1 rings of 2k - 1 sparks each at an angle of Turbo
  Pascal's real `Random` times 2π, `Trunc` of its cosine and sine times k
  units from the cell (`1a07:1265`, `1278`: the reals are C doubles here,
  which would put a spark elsewhere only within about 1e-10 of a unit's
  edge), each within 9 units drawn masked with the frame `Random(4)`.
  The sparks' `Random`s are the dice's: the damage after rolls on.
- The targets in combat (`5340:1ff7`) clear the list first
  (`targets_cleared`); Silence's area has byte 6's high nibble for its
  radius (`silence_radius`). The computer picks (`5340:1eeb`,
  `COK_CAST_PICK_DEATH_KNIGHTS`) one target within range among the side
  the spell is for (`5340:1da5`: `5340:3cac` with its allies flag,
  `cok_combat_pick_target_side`, then `5340:3fd5`'s worth, `cok_ai_worth`),
  or for an area spell (byte 6 of 9 and up) the cell whose area does the
  most good (`5340:44f8`: the worths summed over a 19 by 19 grid around it
  by the rows of `DS:0648`, `COK_DATA_AREA_ROWS`, a tie with some good
  taken on a `Random(100)` below 25, seen from the cell within range).
- Every spell, not Bless's alone, keeps out an ally with an enemy beside
  it (`5dc5:1cc6`, `sided_all`); Charm Monsters' effect holds the caster's
  side in bit 7 (`5dc5:1b20`, `missiles_charm_side`); a bolt's first
  bounce doubles the length walked and each line is charged at least 2
  (`5dc5:354a`, `bolt_doubles`, `bolt_least`).
- Magic resistance (`638c:04c9`) resists magic damage with no effect
  pending too (`resist_damage`).
- The damage's message (`638c:1c52`): "takes N points of damage" with no
  space before "from fire" (lower case, `DS:1c32`-`1d04`), "goes down",
  ", and is dying"; one whose creature flags hold the fight section's
  quiet bit only goes down. A cure says "is cured." (`638c:1479`).
- Cast's refusals (`4381:07ff`) are cut to their strings' lengths:
  "cannot cast spells in this are", "is in no condition tcast any
  spells".
- A character's load (`46b2:10d7`) recomputes its strength between its
  stats and levels (`638c:16d8` with 0, `load_strength`,
  `cok_character_strength`).
- A body's cell in combat shows the cell under it and the icon of slot
  0x22 over it (`6f03:010d`, `body_icon`); a cloud's cell alike, the
  terrain the cloud kept with its icon over it (see Death Knights'
  clouds).

Its spells that cast: the 60 ids that share Champions' handlers, and
Enlarge, Lightning Bolt, Flame Strike, Confusion and Ice Storm, Stinking
Cloud and Cloud Kill (`5dc5:2934`, see Death Knights' clouds), and the
changed spells (band B, 5dc5's handlers against Champions' 5b04's, by
their entries' constants and words, `cok_rules_cast`):

| Spell | dkk | As |
| --- | --- | --- |
| 0x01, 0x69 Bless | `1d77` | Champions' (`1efb`): "is " and "Blessed" run together |
| 0x02 Barkskin, 0x62 Fire Touch | `1daa`, `4b7d` | `COK_CAST_SAYING`: the spell applied, "is toughened from barkskin", "is covered in flame" |
| 0x0a, 0x6a Charm Person | `2044` | `20f8` with `value` 1: an elf (`+0x20` 7) without the creature flag 2 is unaffected, not a monster of some kind |
| 0x18 Resist Fire, 0x2c, 0x64 Bestow Curse | `2763`, `3204` | `2679`, `3705`: "is fire-resistant", "is cursed" |
| 0x23 Strength | `2bc9` | `2e4e` with `value` 1: 1d8 for each of the fighters' classes (`characters.fighter_classes`: paladins, rangers and knights too); one of none of its classes stops the run (the roll is a local never set) |
| 0x29, 0x2e Dispel Magic | `2e5c` | `320f`, its part for the clouds Death Knights' (see Death Knights' clouds: `2f93`, `2dd7`, `638c:21ec`) |
| 0x2b, 0x59, 0x68 Remove Curse | `311b` | `35f5` with `value` 0x2b: it cures effect 0x2b (Bestow Curse adds 0x24, as the original), "is uncursed" |
| 0x2f, 0x40, 0x6c Fireball | `3241` | `376c`; 0x6c (Delay Blast) with `value` 1: the caster level added to the damage |
| 0x30 Haste, 0x37 Slow, 0x39 | `347b`, `396e`, `3aa1` | `3991`, `3d5e`, `3d9b` (`3359` is `3865`): "is hasted", "is slowed", "is hasted" |
| 0x3e, 0x63 | `3c39`, `4b9b` | `3f63`, `4a40`: "is healed" |
| 0x47 Cure Critical Wounds | `4037` | `COK_CAST_CURE_CRITICAL`: Cure Serious' heal of 3d8 + 3, the deity's d8 rolled before the targets are counted |
| 0x4f Faerie Fire | `4391` | `COK_CAST_MISSILES`: `5dc5:1b20` with no bonus, "is highlighted" |
| 0x54 Fear | `4566` | `44ed` with `value` 1: "runs away", and one flees only while it holds 0x6f after (`65cd:2687`); its cone (`175d`) sets the sight's range to the doubled length (`casting.cone_range_length`), while the side with no one on the line and the directions aimed at the caster's own cell are the stack's, not known (`cone_stack_unknown`: the side tried at all its values, the own cell stops the run) |
| 0x55 Fire Shield | `468f` | `464d` with `value` 1: the flame by index from "Flame type: " (`casting.flame_type`) over "Hot Cold" (`6a69:0f1b`), for the computer 1d2; Hot and Cold both "is Protected"; another key asks the cast's "Abort Spell" from Yes |

The band C spells (5dc5's routines with no Champions counterpart), each a
kind of its own (`spells.h`, after `COK_CAST_CURE_CRITICAL`), their
constants in the entry where the routine loads them:

| Spell | dkk | In the port |
| --- | --- | --- |
| 0x04 Charm Person/Mammal | `1e4d` | `COK_CAST_CHARM_MAMMAL`: a race below 7 or creature flag 0x10, any size, "is Charmed" (`DS:4570` + `4731`); else "is not Charmed" |
| 0x07 Ressurection | `1f70` | `COK_CAST_RESURRECTION`: the dead (status 6) of a base constitution and a race above 1 lose their poison (0x37), the most hit points, "is ressurected" |
| 0x11 Restoration | `2326` | `COK_CAST_RESTORATION`: `cok_train_restore` (`train.c`): the drained class whose next level asks the least experience (below `DS:7a6c`'s 3,500,000; the later of equals) rises, the hit points by the lack over the levels lost, the base less the hit die's bonus over the classes drained; the experience that level's, or the experience before the drain for the last level lost; may-train |
| 0x28 Harm | `2d8e` | `COK_CAST_HARM`: the hit points less 1d4 (a byte: 255 and so on below it), magic, no save |
| 0x35 Heal | `3848` | `COK_CAST_HEAL`: blindness and the entry's 0x3d cured ("can see", "regains intellect", intelligence and wisdom recomputed); healed to the most less 1d4 (none within 1d4 of it), "is healed"; `5dc5:2d6c`, a cure that always fails, says nothing |
| 0x38 Death Spell | `398d` | `COK_CAST_DEATH_SPELL`: 2 x 4d20 of hit dice, by `+0x30`'s weights (0 1, 1 2, 2-3 8, 4-5 16, 6 16 or 20 above 51 most hit points, 7-8 20, else 162), the targets in turn "drop dead" (status 6) but those of creature flag `+0x7b` 0x08; the rest off the list |
| 0x3c Disintegrate, 0x48 Flesh to Stone | `3bb9`, `40a6` | `COK_CAST_DESTROY`: unless event 9 cancels it or a save of type 4 is made, killed with the entry's status (6, 7) and words |
| 0x42 Stone to Flesh | `3cd1` | `COK_CAST_STONE_TO_FLESH`: one stoned is okay with 1 hit point, "is no longer a status"; in combat its body taken back (the first entry holding it from 1, the count not looked at: the cell it covered back, the entries after moved down), placed again (`6f03:12a9`), the screen centred on the spell's cell; then the spell applied |
| 0x49 Dispel Evil | `4109` | `COK_CAST_DISPEL_EVIL`: the caster gets the entry's 0x04, then the spell's 0x22, "is affected" |
| 0x4b Raise Dead | `4196` | `COK_CAST_RAISE_DEAD`: dead or animated (6, 1), a base constitution, a race but 0 and 1: poison cured, a point of base constitution less (recomputed), 1 hit point, "is raised" |
| 0x4c Slay Living | `4248` | `COK_CAST_SLAY_LIVING`: type 0x40, `DS:be6c` 0x43 through event 9 ("is unaffected" if cancelled); a failed save of type 4 "is slain" (status 6) but for creature flag `+0x7b` 0x08; else 2d8 + 1 of magic |
| 0x4e Entangle | `42f1` | `COK_CAST_ENTANGLE`: nothing in a 3D area (var 0x4be6); each target's save of type 4 as kind 1 allows, the entry's 6 (held), "is entangled" |
| 0x5c Cone of Cold | `4962` | `COK_CAST_CONE_OF_COLD`: the cone (`175d`) of width 2 and (level + 1) / 2 long, its side where no one is on the line not known (as Fear's); level d4 + level of cold and magic |
| 0x5d Feeblemind | `4a04` | `COK_CAST_FEEBLEMIND`: the save of type 4 a cleric's 1 easier, a mage's 4 and others' 2 harder while the spell applies its 0x44; then the entry's 0x44's handler runs (the drain of 2 levels of the holder's target, `504d:1836`) and the save is put back |
| 0x60 (the Elixir of Youth's) | `4ad7` | `COK_CAST_YOUTH`: the caster's age 1d4 + 1 less, at least 18 ("is now 18 years old"), else "becomes younger" |
| 0x6e Power Word Stun | `4bd9` | `COK_CAST_POWER_WORD_STUN`: the spell's effect (6) for 4d4, 2d4 or 1d4 minutes by hit points 1-30, 31-60, 61-90, else none, "is stunned" |
| 0x6f Mass Charm | `4c8a` | `COK_CAST_MASS_CHARM`: of twice the caster level of hit dice, those of creature flag 2 and no larger than a man ("is unaffected" else) whose hit dice fit, in turn until used up exactly: 0x0b as Charm's, as the save of kind 1 allows, "is charmed" |
| 0x71 Otto's Irresitable Dance | `4dd8` | `COK_CAST_DANCE`: by touch (the stats, event 0x0b, the attack roll): the entry's 6 and 0x39 (`value`, `count`), "begins to dance wildly" |
| 0x72 Power Word Blind | `4e81` | `COK_CAST_POWER_WORD_BLIND`: those whose most hit points fit in 100, in turn, blinded (0x21); a 1d4 rolled between the counts and not used |

Raise Dead and Restoration run at the temple through `cok_cast_on`,
which leaves the spell being cast set after (`DS:be70` 0x4b or 0x11, as
`3999:0293` does), where a cast clears it: effects that read it (Barkskin's
0x30, the minor globes) see it until the next cast. Adding an effect a
target resists says the effects section's `unaffected` ("is unaffected",
`638c:1f86`'s `DS:1d0f`; Champions' "is Unaffected"). Every spell id
casts (the clouds: see Death Knights' clouds). Checked: the section against the
original's code (`test_death_knights.c`'s `test_casting` and
`test_spells`), and in DOSBox-X the first fight on Auto with casters and
Magic On, round by round (`dkk_fight.py`'s cast flow; its spells flow:
Shield, Dispel Magic on the shielded CELESTE, Fireball, Bless, Haste and
Lightning Bolt cast by keys in the first two rounds, then on Auto with
Magic Off), and camp's cure (`dkk_levels.py`'s cast flow); band C's by
`tests/test_band.c` (each kind), `test_death_knights.c`'s `test_band_c`
(the entries' constants and words against the routines), and in DOSBox-X
`dkk_fight.py`'s priests flow (patriarchs and high wizards: Slay Living,
Harm, Silence, Feeblemind, Otto's), its band flow (the party on Auto with
Magic On casting Disintegrate, Power Word Stun, Harm, Flesh to Stone,
Cone of Cold) and `dkk_shops.py`'s raise flow (the temple's Raise Dead and
Restoration on a dead, drained knight). A bolt's bounce
(`bolt_doubles`) is checked against the code only: no fight played by
keys bounced one.

## View

`src/sheet.h` and `src/items.h` port View (`546c:0d74`), from the
adventure's commands and the camp, for the selected character: its sheet,
then the menu on row 24 (prompt empty, items white and light green,
`67b5:03e2` with no keypad directions) of `Items` if it has items,
`Spells` if any of its 58 spell bytes is set, `Trade` if it has money,
outside combat, and is a player character (`+0xe7` below 0x80), cannot
act, or has status 1; `Drop` if it has money; and `Exit`. `Heal` and
`Cure` (`546c:3652`, `3665`) are never offered and do nothing. View takes
no special keys apart: PgUp (scan 0x49) is `I`, Del `S`, shift-F1 `T`
(whether offered or not), F10 `D`; NumLock would exit, but never reaches
the game (see Party). Items, Spells and Trade
redraw the sheet; Escape or Exit leaves, and the screen is redrawn for
the mode. View keeps the character in `DS:46ac` and starts trades from it
(`DS:46b0`).

The sheet (`546c:00a3`) recomputes the stats (`546c:07bb`, `6346:0d20`)
and draws, in the frame of `1128:05fc` (rows 8, 16, 20 and 23 across,
column 19 from row 9 to 19):

| Row | Column | Text | Colour |
| --- | --- | --- | --- |
| 1 | 1 | name | 11, 12 if it cannot act |
| 1 | 20, 27 | `Status:`, the status (`DS:1330`) | 15, 10 |
| 3 | 1, 8 | gender (`DS:12d5`), age and ` years` | 15 |
| 3 | 20, 31 | `Hit Points `, hit points `/` maximum (`6346:0a0d`) | 15, 14 below the maximum else 10 |
| 4 | 1, 20 | alignment (`DS:11c0`), race (`DS:1140`) | 15 |
| 5 | 1 | class (`DS:0f5a`), after the deity (`DS:1259`) for a cleric, the order (`DS:12b9`) and a space before its `Mage`, a knight's ` of the Crown`, ` of the Sword` or ` of the Rose` | 15 |
| 7 | 1, 7 | `Level`, each class's level and former level, `/` between (former ones below the first class's level, `66c2:0eab`) | 15 |
| 7 | 20 | `Experience ` and the experience | 15 |
| 9-14 | 1, 5 | `STR `-`CHA `, each score (from column 6 below 10), `(NN)` at 7 for exceptional strength, `*` at 12 when not the base (`546c:0b50`) | 10 |
| 9-15 | 20 | each coin but silver it has, its amount ending at 37 (`546c:0667`) | 10 |
| 17 | 1, 15-17 | `Armor Class`, the armour class, a sign before it, from column 16, 17 for 9 to 0, 15 for 60 and -10 down (`6346:0984`) | 15, 10 |
| 17 | 20, 33 | `Encumbrance`, the weight, five wide | 15, 10 |
| 18 | 1, 16 | `THAC0   `, 60 less `+0x18c` as a byte, two wide | 15, 10 |
| 18 | 20, 35 | `Movement`, doubled by 0x27, halved by 0x2a, three wide | 15, 10 |
| 19 | 1, 11 | `Damage`, dice, sides and bonus (`1d8+6`), seven wide | 15, 10 |
| 21, 22 | 1 | the readied weapon's and armour's names | 10 |

Names come from the original's strings (`DS:0f5a`-`1dd3`, the data
segment's `COK_DATA_NAMES`, checked against `build/START_FULL.EXE`), by
the named tables `COK_DATA_CLASS_NAMES` and the like, indexed as signed
bytes; an index past them stops the port, and so does one that lands in the middle of the
table on a string that holds the NULs between names, which the original
draws as characters. The class line's search for `M?g` reads past
the class into the bytes the gender, alignment and race left in the
buffer; where it would read bytes nothing set, the port stops.

An item's name (`6346:0488`) is `" Yes  "` or `" No   "` in lists; `* `
for a bonus or curse while anyone has effect 5; its count if not 0; then
the name parts `+0x31`, `+0x30` and `+0x2f` (21 bytes each from
`DS:1390`, 123 of them), each hidden while its bit of `+0x35` (1, 2, 4)
is set, each followed by a space, or for a count above 1, by `s ` on one:
the only part shown, the first of more than two (but type 0x37), the
second when the first is hidden, the third for type 0x37, or any for
types 0x1e, 5 and 0x0c unless `+0x31` is 0x5d (silver); the string is cut
to 40 at each step and kept in the item's first bytes, the rest left as
they were.

Items (`546c:17f9`) lists the items, `Ready Item` on row 3, in rows 5-22
(`67b5:1368`), with `Ready`; `Use` if the character can act, the area
allows magic and it is camp, a plain or a 3D area; `Trade` as View
offers it, outside combat; `Drop`; `Halve` below 16 items; `Join`; and
in shops and the temple (mode 1) `Sell` and `Id` (see Shops and the
temple). The stats are recomputed after every key (`6346:0d20`), and the
list redrawn when the count changes. It ends with Escape or Exit, when
the item count is 0, or when a use ends the turn, which only combat
keeps.

- `Ready` (`546c:1ea7`) unreadies, but not a cursed item ("It's
  Cursed"), or readies; a notice on row 24 (`6346:1827`) says why not,
  the last that applies: "Your hands are full!" (more than 2 with the
  type's), "already using " and the item in its slot (for a ring, when
  the second ring slot is taken, the first ring; for arrows or quarrels,
  those readied), "Wrong Magical Order"
  for a scroll (type 0x27) not of the character's order, "Usable Only By
  Kender" (type 0x43), "Wrong Class" (`+0x11a` against the type's byte
  13). An item with a power (`+0x3e` above 0x7f, `546c:1d96`) of 0 or 2
  adds or removes its effect `+0x3d` (`3f44:3888`: permanent, value
  0xff, its handler run on removal); 3 and 5 recompute strength; 9
  unreadied removes 0x17.
- `Use` needs the item readied ("Must be Readied"), and a scroll, or a
  spell in `+0x3d` with `+0x3e` below 0x80 (`546c:24d7`). A scroll's
  spells are listed (`Spells on Scroll`, `5b04:0981`, whatever its order)
  to pick one; another item "uses an item" (its name on row 22, a pause)
  and casts `+0x3d & 0x7f`. Either is cast as an item (`DS:711d`), at
  level 6 but for item powers. A scroll needs a cleric or magic-user (a
  human also by its first class or first former class, `66c2:0e4f`,
  `0df3`), or a thief above level 9 who rolls 1-75 on a d100, or else
  "oops!". When it took: a scroll loses the spell
  and a use (`+0x2f`, `5b04:575d`) and goes below 100; another item with
  charges (`+0x3c`) loses one of a stack (`+0x39`) or a charge, and goes
  with the last; one with none lasts.
- `Trade` (`546c:2178`), `Drop` and `Sell` first need the item unreadied
  ("Must be unreadied"), and for a scroll marked to scribe, Yes to "is it
  Okay to lose it? " after " was going to scribe from that scroll" in
  yellow on row 21 (`546c:10cb`). Trade picks "Trade with Whom?" from the
  last partner and moves the item to the end of the receiver's, unless it
  has 16 items or the weight would pass its strength allowance + 1500
  ("Overloaded", `546c:32b0`). Drop says "Your NAME will be gone forever"
  in yellow on rows 21-22 and asks "Drop It? ".
- `Halve` (`546c:2234`) splits a count into a copy of half, unreadied,
  after it ("Can't halve that" for 1); `Join` (`546c:22e4`) adds to the
  item every other item of its type, name parts, bonuses, curse and
  weight, with a count and `+0x3c` below 2, up to 255, the rest staying
  with the other.

`Spells` lists the spells in memory (`546c:34ec`, `Spells in Memory`).
`Trade` (`546c:2c75`) picks "Trade to?" from the last partner, shows the
giver's sheet, and lists its coins (`Select type of coin ` with ` Select`
over rows 9-15, the amount after the name padded to column 14); "How
much platinum will you trade? " asks an amount (`58e7:028d`: digits drawn
in white after the prompt on row 24, at most 6, a number past what it
has becoming all of it, Backspace, Enter or Escape for 0), which the
receiver takes unless its weight would pass its strength allowance +
1500 (`58e7:044b`, "Overloaded"), weight and coins changing as words;
again until the giver has none, or Escape asks for another partner.
`Drop` (`546c:2fe2`) lists likewise, the amount right-aligned to column
18, asks "How much ... will you drop? ", and the coins are gone, but in
shops and treasure (modes 1 and 6), where they go to the pool
(`58e7:09fa`, see Treasure and the end of combat).

The port keeps these quirks:

- In Items, Join compares `+0x3c`-`+0x3e` of the other item with itself,
  so they never stop a join, and frees readied items without their
  powers' effects going.
- An item whose `+0x3d` is 0x80 is spell 0: Use does nothing but leave
  `DS:711d` set, so that the next cast from memory counts as an item's:
  no "casts", nothing forgotten, and the "Use it?" prompt for spells for
  combat only.
- A scroll's list counts its spells in `DS:4838` with those listed
  before, which only Scribe's lists reset; past 49 the original writes
  over the data segment, and the port stops.
- Backspace in an amount clears the cell after the last digit, so the
  digit taken stays on the screen.
- In Trade, Escape at the first coin list, before any amount, tests a
  byte the original never set (`[bp-0x13c]`, `546c:2f9b`), which decides
  whether to ask for another partner (0) or leave Trade. Loading the
  camp's picture zero-fills that part of the stack, so in View, the first
  command of a camp whose picture was not already loaded, Trade asks
  again; after anything chosen before in the same View (Items, Spells,
  Trade or Drop) or a View before in the same camp, it leaves. Elsewhere
  (View from the commands, a camp whose picture was kept, or another camp
  command before) the byte depends on the run's history, and the port
  stops with `COK_ECL_UNDEFINED`. An emulator run of the whole camp
  agreed on each case the port carries out, and so did the original in
  DOSBox-X (`e_trade.py`). From the commands' View the byte (at
  `ss:3b29`) varied there from run to run even with the same seed: 0 in 2
  runs of 9 (Trade asks again), 0xac, 0x82 or 0x86 otherwise (it leaves),
  what ran last at that depth (an interrupt's frame, likely) left it. The
  treasure menu's View
  works the same way (see Treasure and the end of combat).
- An item used to cast a spell is followed by its position, as a spell
  such as Dispel Magic can remove an effect whose handler removes another
  item, Spiritual Hammer's; if the item itself goes, the original goes
  on with the freed item, and the port stops.
- A name's bytes past its length keep what longer names left (as the
  original's files show).

With the debug flag set, Items offers `View` first (`546c:187b`; see Debug
mode). The combat side (no Trade, `Use` while the combatant may use items this
round, the item's use ending the turn) is with the player's commands (see
The player's commands).

A differential test ran the original routines in an 8086 emulator
against the port on random parties, items and effects: casting every
spell from 1 to 0x6b with its targets, answers and Fire Shield's keys
(`5b04:1415` and every handler outside combat, with the original's
`6346:0d20`, effects and rolls), Use of wands and scrolls (`546c:24d7`),
Ready, Halve and Join (`546c:1ea7`, `2234`, `22e4`), item names
(`6346:0488`), caster levels (`6346:29fe`), durations (`5b04:0f78`) and
abilities (`60f4:1743`), with the screen's texts, the records, items,
effects, rolls and random numbers compared, Cast from the commands' and
from camp's stack depths. Of 10,000 cases, the 9,670 the port carries out
agreed. The 330 it refused reach a name part past the name table (257,
which random items make common), 0x3b's value from the overlay's segment
(30), effects whose handlers need combat or text (23) and spell ids past
the handler table (20). It is not part of the repository. The screens
and menus were tested in the port only, but for the Trade byte, whose
cases were run in the emulator from the camp menu.

### Death Knights' View

Death Knights' View and items menu (the `items` section's
`COK_ITEMS_DEATH_KNIGHTS`; `cok_sheet` and `cok_items` take its routines)
are Champions' but for their menus, by index (`6a69:0f1b`, see Library),
each keeping its own selection from the first (put back into `DS:c0f7`
before each ask), Escape as Exit; their own items; and these:

- View (`580d:0c66`): `Items Spells Trade Deposit Drop Heal Cure Exit`
  (`DS:3276`, no keypad): Items while it has items, Spells while a byte of
  its spells' block's 0-0x49 has its low 7 bits set, Trade while it may
  trade and has steel, gems or jewelry outside combat, Deposit in the vault
  (mode 7) or else Drop while it has them, Heal and Cure, Exit; the index
  compared as it comes, a special key's scan code too; the sheet drawn
  again after Items, Spells and Trade. Trade money (`580d:23a0`, "trade
  to?", `DS:1061`) and Drop money (`26d0` with 1) list the coins by
  `6a69:1b82` with `Select` (`DS:31d6`: Trade's alone, its Exit the list's;
  Drop's with the item after it, Exit at its index 1), from the selection
  the menu before left; Drop asks "will you deposit? " in the vault
  (`treasure.deposit`) and the coins go to the pool there. Trade's flag
  (`[bp-0x13c]`, tested at `580d:26c0`) holds what the stack held, which
  the port does not know: Escape at Trade's first coin list stops the run.
- Heal (`2e95`, `2f47`): offered outside combat to a paladin (class 3,
  `+0x21`) that is okay and has no effect 0x6d; picks one from the
  party's first ("Heal whom? "), heals it twice the paladin's level
  (`+0x48`, `638c:206f` while below its maximum), "NAME feels better" or
  "NAME is unaffected", and gives the paladin effect 0x6d for 0x5a0
  minutes (`638c:1257`). Cure (`2ee0`, `3018`): the same while `+0x25`
  (the layout's knight tier) is not 0; it picks one ("Cure whom? ") and
  does nothing more.
- The items menu (`580d:0ffa`): `Ready Use Trade Deposit Drop Halve Join
  Sell Id Exit` (`DS:32c7`) by the list `6a69:1b82` with its last index 8,
  so that its own Next, Prev and Exit never show; Deposit in the vault in
  place of Drop; Exit always; no debug View. Ready (`1717`): another
  order's scroll (type 0x27) is readied by a thief of level 10 and up
  (`+0x4b`), "Thief Level Too Low" below it (above 0). Use: "Must be
  readied". The questions of a scroll marked to scribe and of Drop are
  "Lose it? " from No (`DS:0f1a`). Trade (`1a28`) starts from the last
  partner, or with none the selected. Deposit (mode 7): "Your NAME will be
  left in the vault" (`580d:0fda`, `0fe0`) in yellow on rows 21-22, the
  pause, and the item goes to the end of the pool, which the vault writes.
  Sell (`2077`): half the value times any count, no twentieth; Sell and Id
  (`21d3`) ask "is it a deal? " from Yes, in lower case (`DS:1134`-`1743`),
  and Id pays its 100 as the shops' Buy does (the shops section's steel:
  from the steel, `+0x3f`, and what that lacks from the pool's,
  `DS:be58`), "not enough money." otherwise, waiting twice when it shows a
  name (see Death Knights' shops).
- An item's name in the list has the readied column a space shorter
  (`65cd:047f`, `DS:2c02` " Yes ", `2c0d` " No  "), kept in its name bytes
  as the files show.

The pick of a character (`65cd:33c9`; Trade's, Heal's, WHO's, the cast's
target outside combat) goes by index where the menus do
(`menus.routine`): `Select` alone after the prompt and a space, by the
loop with the keypad, the small picture shown in camp and mode 6, until
Select or Escape, which picks none where Exit is allowed (and ends the
pick at once, where Champions' Escape needs Exit after it); Down (2) the
next, wrapping to the party's first, Up (8) the one before, the last before
the first or for none; other keys read again; Down with none picked reads
nil's next in the original, and the port stops. Camp's menu (`4381:3106`) keeps
its own selection (`menus.camp_keeps_selection`). The vault's View and
Items are these (see Death Knights' treasure).

`tools/dosbox/checks/dkk_items.py` checks them against the original: camp
(View, Items, Ready, Halve, unready, Trade, Drop, Escape, Drop money,
camp's Save: 0 pixels apart at every step but the pick's row, saved game C
and every character file byte for byte) and the vault (View's Deposit of
steel, Items' Deposit of the mace, `TMPVAULT.DAT`, `VAULTC.DAT`, saved game
C and its characters); `dkk_fight.py`'s `items` flow View and Use in
combat.

## Monsters and encounters

`src/monster.h` ports the ECL opcodes that load monsters and start
encounters, from overlay `2fd3` and their helpers in `3775`, `4b6d`, `6961`
and `6d21`, and the wrapper of `COMBAT` without the battle.

A monster is a 409-byte character record from `MON<file>CHA.DAX`, with its
effects (`MON<file>SPC`, 9-byte `.SFX` records) and items (`MON<file>ITM`,
63-byte `.STF` items). What a monster keeps in the fields of Champions'
layout (`champions_character` in `src/rules.c`, see Games):

| Offset | Field | Meaning |
| --- | --- | --- |
| `+0x0cf` | `SIZE` | bits 0-2 the size of its combat icon, bit 7 a large creature |
| `+0x0da` | `UNDEAD` | its kind of undead, for turning; 0 for none |
| `+0x0e7` | `NPC` | 0x80 and up; the low bits are its morale |
| `+0x0eb` | `COINS` | coins, seven words, for the treasure |
| `+0x10b`, `+0x10c` | `BASE_ATTACKS` | attacks a round, doubled, of its two attacks |
| `+0x10d`-`+0x112` | `BASE_DICE`, `BASE_DICE_SIDES`, `BASE_DAMAGE_BONUS` | dice, sides and damage bonus of each (the derived `+0x191`-`+0x196`) |
| `+0x11b` | `BASE_HIT_POINTS` | hit points at full |
| `+0x130`, `+0x132` | `MONSTER_EXPERIENCE`, `MONSTER_HIT_POINT_EXPERIENCE` | its experience, `+0x130` (a word) + `+0x132` × `+0x11b` (`351b:0037`) |
| `+0x187` | `MAGIC_RESISTANCE` | magic resistance |
| `+0x18a`, `+0x18b` | `HOSTILE`, `COMPUTER` | 1 against the party, 1 while the computer controls it |

Monsters live in the party list. The original keeps one list from
`DS:609a`, the party first, then the monsters `LOAD MONSTER` appends,
which stay until the end of combat removes them; var `0x7f3e` stays the
party's size. The port's `cok_party` holds up to 72 records (8 + 63, the
size of combat's tables), and every walk of "the party" covers the whole
list, as each of the original's runs to the NULL at the end: the party
list, the special keys and the picker (`546c:3334`, `6346:32c7`, both
wrapping through monsters), `LOAD CHARACTER`, `ADD EP`, `DAMAGE`'s
saving throws and its check that anyone can act, Bash, Pick, Knock, aging,
the effect timers and the effects the party shares (`60f4:0352`, a
monster's counting), the camp, Fix, Order, Save, Display, the spell
targets and Detect Magic. Only `DAMAGE`'s random targets and the camp's
check for timed effects (`57e4:0171`) count to `0x7f3e`. Where the
original's walk would run past a table of eight, the port stops with
`COK_ECL_UNDEFINED`: resting with nine or more records (the learning
timers by position at `DS:712e`: a ninth and tenth record's land on the
healing count, the word at `DS:7136`, which would be deterministic, an
eleventh's on the resting flag) and saving them (`4b6d:22de`'s names on the stack run into
its return address). Character files of every record are saved, monsters
too. A character added (`4b6d:1989`) goes after any monsters. None of this
arises in the shipped scripts: every path from each of their 431 `LOAD
MONSTER`s reaches `COMBAT` with nothing between, so monsters are in the
list only until that `COMBAT`.

Each record has a combat record, `cok_combat_record`, the 22 bytes at the
far pointer `+0x183`. Combat setup (`3cb2:10d9`) gives every record one,
zeroed, and the end of combat (`351b:1493`) frees them; outside combat it
is NULL, read as all zero, as the original's end of combat reads
`0000:0013` for `+0x13` when no battle ran.

| Offset | Meaning |
| --- | --- |
| `+0x00` | a spell being cast, cast at its next turn |
| `+0x01`, `+0x02` | may cast, may use items this round |
| `+0x03` | initiative, signed; 0 once it has acted |
| `+0x04`, `+0x05` | the attack slot in progress, sweeps left |
| `+0x06` | movement left, in half squares |
| `+0x07`, `+0x08` | guarding, has attacked this round |
| `+0x09` | facing, 0-7 from north clockwise |
| `+0x0a` | a far pointer to the record it aims at |
| `+0x0e`, `+0x0f` | rounds dying, times attacked since its turn |
| `+0x10` | made to flee by an effect |
| `+0x11`, `+0x12` | attempts to turn undead, turning from attackers |
| `+0x13` | set past the party's size in the list |
| `+0x14`, `+0x15` | fleeing, the AI's way of moving |

`LOAD MONSTER id count icon` (`2fd3:0465`), unless 63 records were loaded
since `CLEARMONSTERS` (`DS:43be`), reads monster `id` (`4b6d:161b`): the
record (the layout's size, 409 bytes; its far pointers `ITEMS`, `EFFECTS`,
`NEXT` and `COMBAT` cleared, the readied slots `+0x147`, `ITEM_SLOTS`, left
as they are, no stats computed), its effects and its items, in file order. A
monster against the party (`HOSTILE`, `+0x18a`, 1) has its hit points
(`HIT_POINTS` `+0x197`, `MAX_HIT_POINTS` `+0x62`) scaled by the difficulty
`0x4cf4`, 1-5, as `(d + 1) × hp / 4` in a byte; either at 0 makes both 1. An
undead (`UNDEAD`, `+0xda`) counts in `DS:8859`. The icons of the slot
`DS:72eb`, `CPIC<file>` records `icon` and `icon + 0x80`, ready and
attacking, load with colour 0 transparent and colour 8 drawn black outside
CGA mode (`6d21:01d0`, `DS:4b76`; see Display modes and The combat screen).
Then the record and `count - 1` copies (0 counts as 1), until 63 are loaded,
go to the end of the list, each with that slot (`ICON`, `+0x137`). The
copies' items and effects are the first record's in the reverse order, as
each is put first in a copy's lists. `DS:72eb` then moves to the next slot
and `DS:8851` is set, so `COMBAT` fights. The file number is cut to one
digit (`Str` with a width of 1). A missing record says `Unable to load
monster` on row 24, waits for a key and quits to DOS, as in the original.
The port stops with `COK_ECL_UNDEFINED` for a record shorter than 409 bytes
and effects or items that are not whole records (the original reads past
them), an icon slot past the game's (26, `DS:6172`), and a 73rd record. With
no party, where the original writes through `0000:017f`, the monsters start
the list.

`CLEARMONSTERS` (`2fd3:12fe`) clears `DS:8859`, `43be` and `8851`, sets
`DS:72eb` to 8 (as a block starting does, `3775:01e8`) and empties the
treasure pool (`cok_pool`: the coins at `DS:6b0c`, seven longs, and the
items at `DS:6b28`); it leaves the monsters in the list.

`SETUP MONSTER sprite distance picture` (`2fd3:03c9`) keeps the sprite
(`DS:72e9`), the distance wanted (var `0x7ec0`) and the close-up
(`DS:72ea`), sets the distance (var `0x7ec1`) to the least of that and the
open squares ahead (`3775:04a9`: a step at a time while the side faced has
no wall at all, doors and walls the party may pass included, at most 2;
the steps are bytes, the walls wrap as the view's do; 2, stored in
`0x7ec1` too, outside 3D areas), and shows the monster there
(`3775:0575`): until the close-up is shown, the first time in a 3D area
it loads `SPRIT<file>` record `sprite` into the small picture's slot
(`6961:00e4` with mode 1: every group, colour 0 transparent and 13 drawn
black, `cok_picture_load_sprite`), and later redraws the view to erase
it; in 3D mode it draws group `distance + 1` masked at its header's x and
y + 2 (`6961:072e`), which the view's buffer puts at cells x + 3, y + 3.
At distance 0 in 3D mode, outside `ENCOUNTER MENU`, the close-up `PIC`
replaces it at cell 3, 3 (its first frame), or with var `0x7ee1` not 0xff
a portrait (`3775:0538`, see Playing scripts), logged as `monster:
portrait HEAD and BODY`. Outside 3D mode nothing is
drawn. A distance past 2, which only a script setting var `0x7ec1` could
give, says "Illegal range in Show3DSprite." and quits to DOS
(`6961:072e`), as in the original. `APPROACH` (`2fd3:08d6`) moves it a square nearer unless it is at
0; `SPRITE OFF` (`2fd3:2fe1`) redraws the view if a sprite is drawn.
`EXIT` forgets the sprite and the close-up (`DS:8830`, `8831`), so a
script's next `SETUP MONSTER` loads again; `PICTURE` 0xff erases a
sprite as it erases a picture. The first sprite turns the overhead map
off (`DS:6d84`), in any area (see Overhead map). Loading the sprite or
the close-up shows "Loading...Please Wait" while animation is on (see
Playing scripts).

`ENCOUNTER MENU sprite distance picture result c0 c1 c2 c3 c4 text0 text1
text2 flee speed` (`2fd3:23e5`) shows the monster as `SETUP MONSTER` does,
then until a result describes it (the first text not empty from that of
its distance on, typed in light green in rows 17-22, cleared first only in
3D areas) and offers `~COMBAT ~WAIT ~FLEE ~ADVANCE`, or `~PARLAY` in
place of Advance at distance 0 or outside 3D areas (`3775:1885`, the
selection kept from the menu before). The monsters' reaction to the item
chosen, `c0`-`c4`, decides what is stored: 0 the monsters fled ("The
monsters flee."), 1 combat, 2 the party got away, 3 talk:

| Reaction | Combat | Wait | Flee | Advance | Parlay |
| --- | --- | --- | --- | --- | --- |
| 0, attack | 1 | 1 | 2 if the slowest member's movement reaches `flee`, else 1 | 1 | 1 |
| 1, hold | 1 | "Both sides wait." | 2 | nearer | nearer, at 0 3 |
| 2, timid | 0 unless `speed` is below the fastest's, then 1 | 0 | 0 | 0 | 0 |
| 3, advance | 1 | nearer, at 0 "Both sides wait." | 2 | nearer | nearer, at 0 3 |
| 4, talk | 1 | nearer, at 0 3 | 2 | nearer | nearer, at 0 3 |

"Nearer" approaches a square and asks again; 5 and up store nothing. The
movements (`3775:1f8b`) are `MOVEMENT` (`+0x198`) of every record, doubled
as a byte with haste (0x27), else halved when slowed (0x2a), but both start
from the first record's own; with an empty list the original reads
`0000:0198`, and the port stops. No close-up shows in the menu. Row 24 is
cleared at the end. `PARLAY a b c d e result` (`2fd3:2adf`) offers `~HAUGHTY
~SLY ~NICE ~MEEK ~ABUSIVE` after the prompt " " and stores the operand of
the attitude chosen.

`CHECKPARTY field effect min max average found` (`2fd3:1517`) takes a
field given as a variable (operand type 1) by its address, any other by
its value: 0 tests whether a record has the effect, storing 0, 0, 0 and
the result; the variables of charisma (`0x7c19`, `SCORE` 5), the thief
skills (`0x7ca5`-`0x7cac`, `THIEF_SKILL` 1-8) and movement (`0x7c9f`,
`MOVEMENT`) give their least, greatest and average (the sum, a word, divided
by the count as signed); any other stores nothing. So ECL3 block 80's
`CHECKPARTY [7d1b] ...` (the movement as other opcodes read it) leaves
`[7f79]` as the menu left it, and fleeing the minotaurs always fails, as in
the original. With an empty list it divides by zero. `PARTYSTRENGTH result`
(`2fd3:136c`) sums as a byte, for each record, a tenth of its mage levels ×
8, its THAC0 better than 20 and armour class better than 0 × 5 each, its hit
points and its cleric levels × 4 (former levels for a human who may use
them). `PARTY SURPRISE a b` (`2fd3:17b6`) stores whether a record is a
ranger or cleric/ranger (`CLASS`, `+0x5b`, 4 or 10), and 0. `SURPRISE a b c
d` (`2fd3:1856`) rolls two d6 and stores its result to address `0x2cb`,
which keeps nothing, where it meant var `0x7ecb`; only the dice remain.
These walk the whole list too.

`ROB all percent chance` (`2fd3:229e`) robs the selected character, or
with `all` every record in the list: each of its seven coin words
(`3775:1c7c`, `COINS` from `+0xeb`) becomes `Trunc(w × ((100 - percent) /
100))` in Turbo Pascal 6-byte Reals, the low word kept; then each item
(`3775:1da9`) costs a d100 and goes on one up to the chance, which an
item of 25 or more lowers by 50 and one above 255 by 90, to no less than
0, for itself and every item after it. A readied item taken is unreadied
first (`546c:1ea7`): a cursed one says "It's Cursed" and goes readied; the
effect of an item's power is taken from the selected character, whose item
it need not be. The stats are not recomputed. With none selected the
original reads through NULL; the port stops.

The Reals are `cok_real`: byte 0 the exponent biased by 0x81, 0 for zero,
bytes 1-5 the mantissa below its leading 1, the top bit the sign.
`cok_real_from_long` (`1a46:1153`) is exact; the multiply (`1a46:113f`)
and divide (`1a46:1145`) round on bit 7 of a 48-bit result, ties up, after
one normalising shift; the divide takes 42 quotient bits. When either
mantissa's low 24 bits are 0, as for any word converted, the multiply
takes a short way that multiplies the lowest byte by the other's high
byte only, which is not the exact product: so `ROB 0 10 0` leaves 476 of
530 coins, where a C double gives 477. `cok_real_trunc` (`1a46:1157`)
ignores the mantissa's lowest byte. A percent above 100 gives a negative
factor, and the coins wrap: 1 of them at 255 becomes 65,535.

`COMBAT` (`2fd3:191c`) clears the big picture flag (`DS:4b4e`) and the
Move mode (`DS:8858`, restored after), then:

- with monsters loaded (`DS:8851`; a duel, `DS:883e`, is never set in
  CoK), clamps the distance (var `0x7ec1`) to the open squares ahead (2
  outside 3D areas), fights, runs the end of combat, and outside 3D areas
  loads the overland map, `BIGPIC<file>` 0x79 (`6961:07ed`), which only
  `BIGPIC1` holds: for the others, as for any big picture not there, the
  original's `127f:0111` leaves none, silently, and the port does too;
- else with var `0x7f6c` 1 clears it and opens a shop (`36d0:07da`), or
  with var `0x7ee2` 1 clears it and opens the temple (`340d:0ea9`; see
  Shops and the temple);
- else runs the end of combat, for treasure.

Then the mode is 4, or 3 outside 3D areas, var `0x7eca` keeps only its
search bit, the sprite and picture are forgotten (`DS:8830`, `884a`) and,
unless a shop or the temple closed outside 3D areas, the screen is redrawn
for the mode (`6346:2c17`), and outside 3D areas, unless the run ended or
`0x4c38` is set, the party is marked on the overland map (`4877:0005`,
see The overland map). The preloads of overlays (`XXXX:0000`,
`432f:1e96`) are left out; the sound driver's uninstall and install
around them are made (see Sound): before the battle, around the end of
combat (which runs with the driver off), around a shop's, the temple's or
treasure's overlay, and after them.

The battle (`3995:0172`) sets up the battlefield (`3cb2:1c58`, see The
battlefield), which builds the map, gives every record its combat record
and places them, removing monsters with no place, then fights its rounds
(see The rounds), in which the computer plays the monsters, the NPCs and
party members on Auto (see The computer's turns) and the player's turns
take the player's commands from the keys (see The player's commands;
`COK_COMBAT_PLAYER`, which `eclplay --keys` with no `--combat` sets).
Without keys (`COK_COMBAT_UNPORTED`, `eclplay` with neither) the player's
turns pass: the monsters fight a party that does nothing, which usually
ends with the party killed; `--combat pass` has the computer's turns pass
too. `eclplay`'s `--combat`
(`cok_adventure.combat_stub`) decides the outcome instead of the rounds:
`won`, every record against the party (`+0x18a` 1) drops (status 6,
cannot act), and those on its side past the party's size do not; `fled`,
every party record that can act flees (status 3); `lost`, the whole party
dies. Or it plays the rounds otherwise: `gods` has the player's commands
start with Alt-X, which runs the original's cheat when started with
`Helm` (`432f:41e2`); `melee` has every turn attack (see Attacks,
movement and death); `auto` has the player's first turn press Alt-Q,
putting the party on Auto, so that the computer plays every turn; and
`pass` has every turn pass, the computer's too, so that fifteen rounds
without an attack end it. Then the battle's end (`3995:004b`) and the end of combat (`351b:1968`) run, after
every battle and for treasure alone (see Treasure and the end of combat):
the party's part, `351b:0574`, decides from the statuses the battle left
whether the party won, fled or was destroyed. With `--combat pass`
nothing drops, so the party wins, with none of the monsters' experience;
with no party at all, as in `eclplay` without `--party`, it is destroyed, as
`351b:0574` finds no one standing, and the run ends.

`CALL [2e10]` (`2fd3:329b`), which scripts run after each encounter,
recomputes the party's square and, if a picture or sprite is shown or the
party's place, view or area changed (`DS:884a`, `884d`, `884f`, `8850`,
`8852`), forgets the sprite, redraws the view and the status line, clears
those flags and recomputes the wall ahead. Its other addresses, a sound
(0xb203), a step forward (0xc01e), the wall ahead outside 3D areas
(0xc018) and a frame of the small picture's animation (0x6803), are
described in Playing scripts.

The dice (`60f4:1216`), the sum as a byte of `count` rolls of `Random(sides)
+ 1`, are `cok_dice` (`ecl.h`) for every module, and `cok_dice_count` is
`60f4:1261`, which also keeps the count in `DS:6b34`.

The port keeps these quirks:

- `LOAD MONSTER` checks the 63 before reading its operands' values, and
  the icon slot moves once a load, not a copy, and is not checked.
- Scaling is a byte: at Champion a monster of 171 or more hit points wraps,
  and one that gives 0 gets 1, as do both fields when either is 0.
- `CLEARMONSTERS` and a `COMBAT` that does not fight leave monsters
  loaded in the list until a `COMBAT` that does, and every walk of the
  party sees them meanwhile, though the shipped scripts never do this.
- A door or any wall ahead keeps the monster at distance 0: at Throtl's
  gate (7, 15 facing north, a wall the party may pass) the guards' close-up
  shows at once.
- `ENCOUNTER MENU` waits forever while both sides wait; it passes no time.
- `CHECKPARTY [7d1b]` stores nothing; `SURPRISE` stores nowhere.
- `ROB` lowers the chance for the items after a heavy one (above 24 by 50,
  above 255 by 90), rolls the d100
  even at a chance of 0, takes the power's effect from the selected
  character, and rounds with Turbo Pascal's Reals.
- `ADD EP` divides by the number of classes before it tests whether the
  character can act, so a record with no class level, a monster or one
  dead, divides by zero.
- Up from a menu with none selected selects the last record.

A differential test ran the original routines in an 8086 emulator against
the port on random parties, monsters, maps and operands: `LOAD MONSTER`
with the DAX reads fed the files' records and the icon loads recorded,
`CLEARMONSTERS`, `SETUP MONSTER`, `APPROACH` and `SPRITE OFF` with the
picture loads and draws recorded, `3775:04a9`, `3775:1f8b`, `ENCOUNTER
MENU` and `PARLAY` with their menus fed choices and their text recorded,
`CHECKPARTY`, `PARTYSTRENGTH`, `PARTY SURPRISE`, `SURPRISE`, `ROB` and
`351b:1493`, comparing the variables, the list's records, items and
effects, the counters, the icon slots, the transcript and the random
numbers drawn. Of 13,000 cases, the 12,738 the port carries out agreed.
Both refused 246: an empty party for `3775:1f8b` (245) and a monster id
not in the file, which quits to DOS (1). The port alone refused 16: a 73rd
record (7), and `ROB` with none selected (7) or taking an item's power
from none selected (2), where the original reads through NULL. Mutations
that the unit tests cannot see (the first enemy in `351b:1493`, `ROB`'s
chance floored at 0, the power's effect from the
selected character, a field of operand type 3) each failed hundreds of
these cases. The Reals agreed with a reference model, itself checked
against the emulator, on 200,000 operations. It is not part of the
repository.

### Death Knights' encounter

Death Knights (the descriptor's `combat` section, see Games) always sets
an encounter up as `SETUP MONSTERS`, `CLEARMONSTERS`, `LOAD MONSTER`s and
`COMBAT`. `SETUP MONSTERS` (opcode `0x0c`, `dkk 3632:038f`) is `SETUP
MONSTER` with a fourth operand, the directions an ambush comes from
(bits 0-3, a quadrant each: `DS:c4cc`, `game->ambush`), which the
placement reads. Its show at distance (`3c1e:0515`, `combat.show_none`)
takes a sprite or close-up of `0xff` for none, shows the close-up at any
distance where there is no sprite, and has no portrait (the game has no
`0x7ee1`). `LOAD MONSTER` (`3632:0472`) is Champions' on its own records
(see Games' Death Knights: `46b2:19a2`), the icons from `CPIC1`
(`7090:016f`). `COMBAT` (`3632:16ec`) with no monsters loaded tries the
vault's menu first (var `0x4d00` 1, cleared: `3948:000e`), then a shop
(`0x7f6c`: `3b89:06f3`) and the temple (`0x7ee2`: `3999:07d3`; see Death
Knights' shops; the vault's `vault_unported` is none: see Death Knights'
treasure), else the end of combat;
after a battle off the 3D map it shows `PICTURE`'s overland map
(`view.overland_picture`, `0x70`; Champions' `0x79`).

## The battlefield

`src/combat.h` ports combat setup (`3cb2:1c58`) with the rest of overlay
`3cb2`, the combat tile loader (`6d21:002c`), the map's lookups of overlay
`6beb`, `6346:268a` and `432f:2df3`, and keeps the battle's map and
tables in `cok_adventure.combat` (`cok_combat`) for the rest of combat
(see The rounds and after).

Setup runs with the mode already 5, as `3995:0172` sets it first. It
clears the text delay flag (`DS:4b59`, see Library; the battle's end
sets it again, `3995:0297`), frees the small picture (`6961:0537`), the
big one (`DS:6e02`) and the portrait's pieces (`6de5`, `6dea`, marking
none held; see Playing scripts), clears row 24, waits speed × 100 ms and prints "A battle begins..." there in
light green (`1521:0353`); clears the round (`DS:714b`), the attack roll
(`6b3b`), the bodies, the kender who yelled (`71a7`), Magic On (`7198`),
the recovered missile (`60a2`), the weapons lost (`609e`) and var
`0x7f33`, and sets the round limit (`714c`) to 15; builds the map
(`3cb2:1029`), gives every record its stats and combat record
(`3cb2:10d9`) and places them (`3cb2:17f7`); sets the view's origin to the
first record's cell less 3 (`6beb:0bcb`, placed or not) and draws the
screen there (`6346:300f`, see The combat screen), whose scroll
(`6beb:096b`, `07a9`) works out the screen positions (`6beb:0077`); clears
`+0x11` of each record's combat record and runs effect events 8 and 0x16
for it; works out the enemies' health (`432f:2df3`) and clears the Move
mode (`DS:8858`). A key waiting is read and dropped (`1614:0479`) after the
map, the records and placement, and before each record is placed. The
dead that explode (`6b45`, `6b95`, `6b96`) are left to the parts that use
them; the mouse's flags for Move and aiming (`71a6`, `71a5`) and Move mode
(`8858`) are cleared; with the debug flag set, each record of the list is
traced last (`3cb2:1e13`, see Debug mode); the lists of clouds and pools
(`7111`, `7119`;
`7115` is cleared and never used) are emptied (see Clouds, pools and
breath).

The map (`DS:6a2e`, 0x4e9 bytes from GetMem, freed by `3995:004b`):

| Offset | Meaning |
| --- | --- |
| `+0`, `+1` | never used |
| `+2`, `+3` | the view's origin, the top-left of the 7 by 7 cells shown (signed) |
| `+4`, `+5` | the cursor is shown, its footprint (0 and 1 at setup) |
| `+6` | sight is not blocked (`6b30:03f1`; 0 at setup) |
| `+7` | a terrain value a cell, 50 across by 25 down, row by row |

A terrain value's four bytes (`DS:1ee4`, `cok_combat_terrain`) are the
cost of moving onto it (0xff where none can), the eye height of one
standing there, the height that blocks sight past it and its tile:
values 0x01-0x19 are DUNGCOM's frames 0-0x18 (0x17, frame 0x16, plain
floor), 0x1a-0x1f RANDCOM's (table, chair, white cloud, puddle, green
cloud, a fallen character) and 0x20-0x41 WILDCOM's 0-0x21; 0 stands for
off the map. The tile set (`DS:616a`) holds 40 frames of 24 by 24: setup
loads, opaque (`6d21:002c`), DUNGCOM record 1's 25 frames in a 3D area
(var `0x4be6`), else WILDCOM's first 33 of 35, from frame 0, and
RANDCOM's 6 from 0x22. The frames between keep what an earlier battle
loaded; 0x21, the river bank's, is never loaded. `TILES.DAX` is not used.
The keyboard flush after each load (`1614:045c`) goes through the `flush`
hook (see Playing).

Combatants are numbered from 1 in list order, the party first:

| Data | Meaning (`cok_combatant`) |
| --- | --- |
| `DS:623f + 4n` | its top-left cell x and y, then n and its size, 0 while it is not on the map |
| `DS:68d1 + 4n` | its record, a far pointer |
| `DS:6362 + n`, `63aa + n` | its cell less the view's origin, as last worked out |
| `DS:6242` | the count, one more than the combatants (entry 0's size) |
| `DS:63f3` | each cell's combatant, 0 for none (`6beb:0375`) |
| `DS:69ee + 7k` | from k 1, up to 8 (count `DS:6a2d`): a party member fallen where it stands, its x and y, and the terrain its body (0x1f) covers |
| `DS:6b2c`, `6b2d` | the records on each side that can act (`6346:268a`) |
| `DS:7197` | the enemies' hit points as a percentage of their most, in fives (`432f:2df3`) |

A size is a footprint of up to four cells (`DS:1fe4 + 8 × size`, from
`+0xcf` & 7, the layout's `SIZE`): 1 one cell, 2 it and the one below, 3 it
and the one to the right, 4 two by two. The combat record (`+0x183`, see
Monsters and encounters) gets its facing and `+0x13` at setup.

On a 3D map (`3cb2:08cd`) each 3D square from 6 west to 6 east and 2 north
to 2 south of the party's, row by row from the north and west to east along
each, becomes a block of 6 by 5 cells whose top-left is 6 × east + 5 × south
+ 21 across and 5 × south + 10 down: north is up and to the left, and the
party's square is cells 21-26 of rows 10-14. Each side of a square is open,
a wall or a door (`3cb2:0306`, `0388`): open where the 3D map has no wall, a
wall where the party cannot pass (`69ea:0573`), else a door, the two faces
of the edge together; off the 3D map a wall, but open to the east and west
on the party's row. A block is floor in rows 2-4, its west edge drawn down
the slant (`03fc`), its north edge in rows 0-1 (`04a1`) and its corners from
the edges of its own and of the squares north, west and east of it (`051e`,
`06f6`); blocks overlap by a column, later over earlier. A square whose byte
has 0x40 (`69ea:07a5`), with a wall, no door, and walls where two opposite
sides are walls on the other two as well, gets a table on a d10 of 5 or less
on each plain floor cell a + b of row b of its block, for a 2 then 3 and b
2-4, each followed by a chair on 9 or less on each plain floor beside it
(`00d3`); the dice are rolled only where all that holds. Cells 6 and 7 and
those beside them, to 8 across and the row below, belong to blocks not yet
built, and the test for floor reads them as they are: GetMem does not clear
the map. In play it takes the 1,264 bytes the buffer of RANDCOM's record
held, which `6d21:002c` has just freed, so its cells hold that record from
its byte 7 (cell 0, 0 may hold the free list's size, which nothing reads),
and the port fills them so before building. Later blocks overwrite whatever
the dice put there: the furniture stands only in the room's own block, and
the rest shows only in the dice. Where the heap holds a free block of 1,264
to 1,751 bytes below the lowest of 1,752 or more, the map goes there instead
and holds what that held, such as an earlier battle's map or the buffers of
`8X8D1.DAX` records 201 and 203 (1,416 and 1,457 bytes), and the dice may
differ. Only GEO2 record 64, Neraka (ECL2 block 64), has rooms, at x 11-15
of y 8-9 and x 8-10 of y 12-13.

On open ground (`3cb2:0fc8`) every cell is floor, then come a river, trees
and scatter by the flags of terrain type 15 (`DS:0441`, 4): the river's d100
is rolled but needs flag 0x10 or 0x20 (`0a00`); column by column from row 1,
a floor cell below another gets something on a d100 of 8 or less: on a
second d100 of 8 or less a bush (0x2a, 0x2b), else a tree, its top
(0x20-0x25) above its trunk (0x26-0x29) (`0b0b`); then each floor cell's
d100 picks 0x30-0x31 up to 2, 0x2c-0x2f to 7, 0x37-0x39 to 17 and 0x32-0x35
to 22 (`0e4a`, `0ca2`): about 2,700 rolls. The port takes the other types'
flags, which no battle uses.

`3cb2:10d9`, for each record in the list: its stats (`6346:0d20`), a
combat record zeroed (the old one, which the original overwrites, freed),
`+0x13` set past the party's size (var `0x7f3e`), facing the combat
direction of the party's facing (`DS:03da`: north 7, east 2, south 3,
west 6), turned round against the party (`+0x18a` 1); an ally past the
party whose morale (`+0xe7` & 0x7f) is 0 or above 0x66 gets var `0x7ec6`
+ 0x80.

Placement (`3cb2:17f7`) counts each side's records that can act
(`6346:268a`), and gives each side a square (the party's, or the monsters'
var `0x7ec1` squares ahead), the quadrant it faces (the party's facing, or
the opposite), a first rank as wide as half those that can act, rounded up,
and four formations of 11 columns by 6 rows (`DS:43cc`), one for each square
it may stand on, free between the limits of `DS:03ee` for its quadrant, or
for the second square those of formation 4, the widest. Then for each record
in list order (`3cb2:1379`, `122c`): from its side's square, ranks from a
centre set by the square and quadrant (`DS:03ca`, `03de`, `03e6`), moved
back by the rank's number, take the centre and then cells to one side and
the other in turn, further each pair (directions of `DS:03da`). A rank ends
at a cell outside the formation in one of column and row, which is not
tried, or at the cell that makes as many as the first rank's width, later
ranks 12, which is; the first rank is the first of the record's whole
search, so a later square's ranks all take 12. In the party's first rank
facing east or west, the next rank is skipped if the party's square has any
side but the one it faces that is not a wall. A cell outside in both moves
on to the next square (`DS:03ba`: behind, then to either side) that the
side's own is not walled off from, from rank 0; with none left there is no
place. A cell is taken if it is free in its formation and the footprint
there, probed (`6beb:0c9d`), meets no one and can be walked on; the
combatant stands at column + 6 × east + 5 × south + 22, row + 5 × south +
10, of its square. The walls asked are the 3D map's even on open ground: the
test is for mode 3, and `3995:0172` has made it 5. A record placed that
cannot act leaves the map (size 0), a party member leaving a body; status 9
makes it unable to act and off the map. A monster (`+0x13`) with no place is
removed (`4def:3b0a`: selected first, the party's size and its group's icons
kept); a party member with none stays in the list, off the map, its place
the last cell whose formation cell was free (or an earlier battle's, with
none). The occupants are rebuilt after each record placed.

The battle's end (`3995:004b`, `cok_combat_end`) runs after the rounds: it
frees the lists of clouds and pools on the map (`DS:7111`, `7119`, see
Clouds, pools and breath); then each record charmed (effect 0x0b) and okay
runs (status 3) if more than one enemy could act at the last count
(`DS:6b2d`), which the end of the last round made; each loses the first
effect of each id that lasts only through the battle (`60f4:1440`,
`DS:0db4`: 0x03, 0x0b, 0x15, 0x17, 0x1b, 0x1e, 0x1f, 0x33-0x35, 0x5b, 0x6a,
0x6b, 0x6f, 0x76 and 0x77); one berserk (0x4d) and turned (`+0xe7` 0xb3)
goes back to the party's side; the map is freed, and spells pick their
targets outside combat again (`DS:6e3a` back to `5b04:127e`). The original
also frees the pictures of missiles and flashes (`DS:719e`), which setup
makes again; the port keeps them.

Effect 0x52, a dragon's fear (`3f44:303c`), which event 8 runs at setup
for the red dragons of MON3 record 22 (ECL3 blocks 97 and 98), acts on
every record on the other side without effect 0x5c, 0x6f or 0x77: one of
level (`+0xd6`) 0-3 is terrified, effect 0x6f for good, through
`60f4:20f7` (event 9 first, so that magic resistance may leave it
"Unaffected"), and if it took hold the computer controls it (`+0x18b`),
a player character is turned (`+0xe7` 0xb3), its target is cleared and
it flees (`+0x10`); others are afraid (0x77) unless they make a saving
throw of type 4. The handler does the same when the effect goes. 0x6f's
handler (`3f44:3692`), as the battle's end removes it, gives a turned
character back (`+0xe7` 0, `+0x18b` 0) and clears `+0x10`. In combat the
character flashes and "is terrified" or "is afraid" shows in the side
panel (`6346:228c`, see The combat screen).

| Function | Original |
| --- | --- |
| `cok_combat_setup` | `3cb2:1c58`, `3cb2:1029`, `6d21:002c` |
| `cok_combat_dungeon`, `cok_combat_wilderness` | `3cb2:08cd`, `3cb2:0fc8` |
| `cok_combat_records` | `3cb2:10d9` |
| `cok_combat_end` | `3995:004b`, `60f4:1440` |
| `cok_combat_place_all` | `3cb2:17f7`, `1379`, `122c`, `11fb` |
| `cok_combat_footprint` | `6beb:000f` |
| `cok_combat_occupy` | `6beb:0375` |
| `cok_combat_cell` | `6beb:0493` |
| `cok_combat_on_view`, `cok_combat_visible` | `6beb:06be`, `06ef` |
| `cok_combat_scroll` | `6beb:07a9` and `0077`, without drawing (see `cok_arena_centre`) |
| `cok_combat_x`, `_y`, `_size`, `_index` | `6beb:0bcb`, `0bf3`, `0c1b`, `0c43` |
| `cok_combat_probe` | `6beb:0c9d` |
| `cok_combat_place` | `6beb:10f3` |
| `cok_combat_count_sides`, `cok_combat_enemy_health` | `6346:268a`, `432f:2df3` |

The port keeps these quirks:

- The river's d100 is rolled for a type with no river; the tables' dice
  only where a room's conditions hold, and they read cells not yet
  built, RANDCOM's record.
- The walls of the 3D map hold back the formations on open ground too.
- The first rank's width counts the records that can act before any is
  removed; a party of two has a first rank of one, so its second member
  stands a rank back.
- The view's origin comes from the first record, placed or not. With the
  list's one record removed, the lookup of the first, NULL, finds entry
  1, which the removal cleared, at the last cell tried for it. With the
  list empty from the start it finds none, entry 1 holding a record of
  an earlier battle, and the origin is -3, -3.
- A record of status 9, as the ghasts of MON3 record 58 (ECL3 block 82),
  is made unable to act and taken off the map, so that the end of combat
  counts it as dropped (var `0x7ec8`).
- `6beb:0375` runs a footprint at x 49 into the next row.
- `6beb:0c9d` starts from plain floor, which a footprint all cloud and
  puddle keeps; a cell off the map makes it 0 whatever follows; ties go to
  the later cell.
- `6beb:0c43` compares the entry after the last too, which during
  placement holds the record being placed; afterwards the original's holds
  a record of an earlier battle or NULL, the port's NULL. A record that is
  not a combatant reads as at 0, 0, with the count as its size.
- `6beb:10f3`, raising a party member that has no body, writes the
  costliest terrain under its footprint into its cell; the count of
  bodies is never lowered.
- `6beb:07a9` moves the centre only toward its target and within 3-0x2e
  across and 3-0x15 down, so a centre outside stays outside.
- `6beb:06ef` reads the screen positions as last worked out.
- `432f:2df3` multiplies the hit points by 20 as a word, which wraps above
  3,276, and leaves the last value with no enemies.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`: a
record on a side other than 0 or 1 (`6346:268a` counts one that can act,
as a signed byte, into the bytes around `DS:6b2c`, and placement indexes
the sides' formations by any); with no party, the list's first two
records both removed (the walk goes on from the last record kept, or
with none from the first, freed, whose next still names the second, so
it goes back to the second for good); a 72nd
combatant (the entry after it lies in the screen positions), a ninth body
(the table has eight), the body of a record of size 0 placed off the map,
a footprint outside the occupants, a size past 7 (`6beb:10f3` takes
`+0xcf` & 0x7f) and a lookup of a record that is not a combatant with a
count past 7, which read past the footprints, placing a record that is
not a combatant (`6beb:10f3` writes its size over the count), a facing
past 7, and stats that cannot be worked out.

`eclplay` logs the battlefield, `combat: the 3D map around X,Y facing D,
the enemy N squares ahead` or `open ground facing D, ...`, then each
combatant as it is placed, `N NAME at X,Y` (`, fallen` with a body, `,
off the map` with size 0), `N NAME has no place` or `NAME has no place
and is removed`, then `the view from X,Y`.
`--combat-map FILE` writes, for each battle, a line naming the block, the
view and the count; the map, a character a cell: a combatant's mark (1-9,
a-z, A-Z), or for the terrain `.` plain floor, `#` where none can walk,
`T` a table, `h` a chair, `_` a body, `:` cost 2, `~` cost 4 and `,`
anything else; the map again, each cell's value in hex; and a line for
each combatant (mark, number, name, cell, size, side and facing) and each
body.

A differential test ran the original routines in an 8086 emulator against
the port: open ground (`3cb2:0fc8`) on random seeds and flags, the 3D
builder (`3cb2:08cd`) on random squares, GEO records and random maps, the
map's cells first RANDCOM's record or other fills alike, the whole of
setup (`3cb2:1c58`, its drawing, text and tile loads hooked, the map
taking RANDCOM's buffer) followed by the battle's end (`3995:004b`) on
random parties, monsters with their effects (0x52 among them), maps,
squares, facings and distances, and each lookup on random tables,
comparing the maps, occupants, combatant and body tables, screen
positions, the list's records, items, effects and combat records before
and after the end, the texts said, the selection, the counts and the
random seed. All 1,000 maps of open ground and all 4,000 of the 3D
builder agreed (281 of those reading unbuilt cells). Of 6,000 setups, the
5,537 the port carries out agreed; both refused 15, where the original
never ends (with no party, its first two records removed; in 7 the port
stopped first at the handler of effect 0x47, then unported); the port alone
refused 448: a side other than 0 or 1 (241), a magic resistance roll for
a spell with no character selected, which the original reads through
NULL (154), the handler of effect 0x47 on event 9, then unported (50),
and a body off the map (3). With the rounds the port runs setup in a
battle, where 0x47's handler is ported, turning a spell back included
(see Spells in combat). Of 4,000 lookups, the 3,619 the port carries out
agreed; it alone refused 381: placing a record that is not a combatant
(243), a side other than 0 or 1 (58), a footprint outside the occupants
(30), sizes past the footprints (45) and placing one whose footprint
lies outside the map (5). It is not part of the repository.

### Death Knights' battlefield

Its setup (`dkk 410b:1e41`) says `a battle begins...` (`DS:2a07`,
`combat.begins`) and builds the map (`410b:10d7`): `WILDCOM`'s 0x26 tiles
or `DUNGCOM`'s 0x19, the latter only when the wilderness's were loaded
since (`DS:b58c`, `combat.tiles_kept`), and no random tiles (it has no
`RANDCOM`). The dungeon (`410b:0892`) fills the map with floor first
(`combat.dungeon_fill`: every cell built) and draws two corners otherwise
(`combat.door_corners`, `410b:04d8` and `06ae`: a door on the
north-west's west edge as a wall, and beyond the north-east's north
tile 7 where the east is open). Open ground is a builder of its own
(`combat.wilderness`, `COK_WILDERNESS_DEATH_KNIGHTS`, `410b:107e`): floor
`0x45` (its tile `0x25`); a river where var `0x4bf3` is set
(`COK_VAR_RIVER`; no d100), from a column 34 less 5d4 moved left to a
multiple of 7 less 2, each row a bank `0x34`, water `0x35-0x38` (`0x34`
+ d2, `0x37`, `0x37`, `0x37` + d2) or, where a d20 is at most the rows
since the last, a ford (`410b:0987`: `0x3a`, `0x3a` + d2 twice, `0x3d`),
and the far bank five on, one column right a row; trees (`0beb`: the
chance 5, 2 with flag 2, 3 less for 4, 2 more for `0x40`, 5 for 8; a
bush `0x29` + d2, a tree `0x1f` + d5 over `0x24` + d5); and Champions'
scatter (`0f00`) with its own cells (`0d82`: `0x31` + d2, `0x2c`, `0x2c`
+ d5 up to the first two chances and 1, the third's not added, `0x44`,
`0x3d` + d6). The placement (`410b:1aa4`, `18c2`) clears the sizes of
entries 1-72 alone (`combat.placement_clears`), and with an ambush's
directions has each enemy come from the next of them (`combat.ambush`):
the enemy's first rank a share's half, the formations laid out again for
each new facing (from the enemy's distance, 1 where it is 0 and the
facing is not the party's, 2 in the demonstration), each record's combat
facing set from it; a record whose facing lays the formations out again
is placed by the enemy's side's formation whatever its own (`18c2`'s loop
over the sides leaves `DS:86e8`, the side `410b:1444` places by, 1): a
knight on the party's side after enemies from another way stands at the
enemy's anchor; a body keeps the cell under one already there
(`6f03:152e`, `combat.body_under`); a record of status 9 stays on the
map (`combat.status_9` false). Placement, leadership and the setup agree
with the original's in DOSBox-X, cell for cell (`dkk_fight.py`, see
Checks against the original).

## The rounds

`src/round.h` ports the battle (`3995:0172`) with the rest of overlay
`3995` but the player's commands, the start of a round for each record
(`432f:0000`, `014d`, `0e80`, `0f9e`), the lines, sight and ranges of
overlay `6b30`, and the helpers of overlays `432f`, `6346` and `60f4` that
the later parts of combat share. Each turn is logged as `turn: NAME
(initiative N)`; the computer's (`3afb:004b`, see The computer's turns)
and the player's commands (`3995:0573`, see The player's commands) are
ported; with no keys the player's turn ends (`6346:2964`). The screen is
drawn as the battle goes (see The combat screen, and below).

The battle (`cok_combat_battle`), as `COMBAT` starts it: the mode becomes
5, spells pick their targets with the combat routine (`DS:6e3a` holds
`432f:2337`, see Spells in combat), the battlefield
is set up (see The battlefield), and the battle is over before it starts
if either side has no record that can act at the count placement made,
before it removed the monsters with no place. Every record runs effect
event 0x18 (a dragon's fear, a second time), then until the battle is
over, round by round:

1. The sides are counted (`6346:268a`).
2. Each record in list order starts the round (`432f:0000`): it casts no
   spell (combat record `+0x00`), may cast unless a stinking cloud (terrain
   0x1e) lies under it, may use items, has not attacked, uses attack slot
   2, has its attacks with its weapon (`+0x18f`, `ATTACKS_LEFT` 1,
   `432f:0e80`) and its others (`+0x190`, `ATTACKS_LEFT` 2: `+0x10c`,
   `BASE_ATTACKS` 2, after event 0x12, haste and slow, halved by
   `432f:0f9e`), its sweeps (`+0xce`, `FIGHTER_LEVEL`), its initiative and
   its movement (`432f:014d`). One that can act rolls 1d6 plus its
   dexterity's missile bonus (`6346:12f8`), as a byte, at least 1, less 6
   when its side is surprised (var `0x7ecb`, side + 1: bit 0 the party's,
   bit 1 the enemy's), and 0 outside 0-20; one that cannot gets 0 and rolls
   nothing.
3. Var `0x7ecb` is cleared: surprise lasts one round.
4. The turn order (`3995:02a0`, `DS:71ae`): every record by initiative,
   highest first, the dead too. Each goes before the first entry from 1
   with a lower initiative, an empty entry counting as -1, or before an
   equal one for which a d2 rolls 1; the search stops at entry 65 and
   later entries move down, the 72nd falling off.
5. From entry 1 (`DS:72ce`), each entry's turn (`3995:040b`), then the
   dead that explode (`60f4:2375`), until an empty entry or past the 72nd.
   The entry is read again after each turn, so that turns that move
   entries (a delayed spell, Delay, Quick) count. No side's end stops the
   round.
6. The end of the round (`3995:0b6d`).

The battle's end (`3995:004b`) follows (see The battlefield).

A turn (`3995:040b`) clears the combatant's hits and turning (`+0x0f`,
`+0x12`) and guard (`+0x07`) and runs event 7 (held, asleep, paralysed:
0x33-0x35, 0x1f, 0x03, 0x1b, which end the turn). If its initiative is
then above 0, 0x14 (a turn given to the computer by Quick) becoming 0x13,
it is selected (`DS:6096`), its actions are shown (`DS:71ad`) if it is on
the party's side or any of its cells is on the view (`6beb:06ef`), the
map shows it (`6beb:12ef`), its stats are recomputed (`6346:0d20`), the
side panel drawn (`DS:71ac`, `6346:0af6`), events 0x0f and, unless it is
casting, 0x15 run, and if its initiative is still above 0, the computer
acts for it (`+0x18b`) or the player does; its cell is redrawn
(`6beb:02aa`). A turn that passes for want of keys selects the menu's
first item (`DS:6e0f`) for one that can act and casts no spell, as the
player's commands start.

The end of a round (`3995:0b6d`): a minute passes (`57e4:0549`, the clock
and every record's effects, the monsters' too, counted down: in mode 5
every timer flag is set); the round is counted (`DS:714b`); the enemies'
health is worked out (`432f:2df3`); each record runs event 0x13 and is
hurt by a puddle (terrain 0x1d) it stands in when it can act
(`60f4:0dc3` with no flag: 1d6 through `60f4:1db7`, see Clouds, pools
and breath); a dying one (status 5) counts a round
(`+0x0e`, a byte) and past 9 is dead (status 6), its hit points and icon
unchanged. Then "Your Teammate is Dying" shows on row 24 (`6346:1827`) if
a party member on the party's side is dying (`6346:31e9`), the sides are
counted and the map redrawn around the view. The battle is over when a
side has none that can act or the rounds reach the limit (`DS:714c`, 15,
which an attack moves to the round + 15); when the enemy has none and
the party some, "Continue Battle:" (`67b5:177f`) asks (not in the
demonstration), and Yes fights on, asking again each round.

Data the rounds keep in `cok_combat`, beside the battlefield's:

| Data | Meaning |
| --- | --- |
| `DS:71ae + 4(i - 1)` | the turn order, records from entry 1 to 72 (`turn`); `DS:72ce` the entry whose turn it is |
| `DS:71ac`, `71ad` | the side panel is to be drawn; the actor's moves are shown |
| `DS:6a30 + 3k`, k from 1 | the combatants around a cell (`6b30:08d8`, `listed`): combatant, length in half cells, direction; the count at `DS:6a32` |
| `DS:714c + i`, i from 1 | the enemies `6346:26e2` found (`enemies`), combatant numbers |
| `DS:6b41 + 4i`, i 1-20 | the dead that explode (`exploding`), count `DS:6b95`, `DS:6b96` while they do |

`6b30:08d8` (`cok_combat_list`) lists each combatant on the map (size above
0) that a footprint at x, y facing a direction (0xff any) sees within a
range: for each cell of the combatant and each of the footprint, the cell
in the footprint's quarter (`6b30:054a`) and seen from it (`6b30:03f1`),
the least length kept (below 0xff), with the direction given, or for any,
the first 0-8 whose quarter holds the pair that gave it; the one at x, y
is listed too, at 0. The list is sorted (`6b30:0033`, an exchange sort):
nearest first, then the lower direction first unless it is odd and the
other even. A line (`6b30:01a5`, `024c`, `cok_combat_line`) steps a cell
along the longer axis, adding 2 to its length, and one along the shorter
when its error, increased by twice the shorter, reaches the longer, adding
1 more, so that a straight step is 2 and a diagonal 3; it stops once at
its end. Sight (`6b30:03f1`) walks the line from the start cell, which
counts, until a cell's blocking height is above the eye height of the
start's terrain (`DS:1ee4`: walls block 2, a table lifts the eye to 2),
unless sight is not blocked (map `+6`), or until its length passes twice
the range + 1. The quarter (`6b30:054a`) holds the cell itself and the
one ahead, and is a wedge of 90 degrees from the cell ahead, by eight
pairs of comparisons; neither cell may be off the map.

The helpers:

| Function | Original |
| --- | --- |
| `cok_combat_battle` | `3995:0172` |
| `cok_combat_round_start` | `432f:0000` |
| `cok_combat_turn_order` | `3995:02a0` |
| `cok_combat_turn` | `3995:040b` |
| `cok_combat_end_round` | `3995:0b6d` |
| `cok_combat_end_turn` | `6346:2964` |
| `cok_combat_damage` | `6346:24d7`, in combat |
| `cok_combat_bandage` | `6346:31e9` |
| `cok_combat_helpless`, `cok_combat_opposite` | `6346:0cdb`, `6346:2666` |
| `cok_combat_movement`, `cok_combat_half_attacks` | `432f:014d`, `432f:0f9e` |
| `cok_combat_weapon_attacks` | `432f:0e80` |
| `cok_combat_missile_weapon`, `cok_combat_ammunition` | `6346:3043`, `6346:3111` |
| `cok_combat_fastest_enemy` | `432f:2e82` |
| `cok_combat_auto` | `3995:1604` |
| `cok_combat_battle_only` | `60f4:1440` |
| `cok_combat_leave` | `60f4:133c` |
| `cok_combat_gods` | `432f:41e2` |
| `cok_combat_explode` | `60f4:2375` |
| `cok_combat_line_start`, `cok_combat_line_step` | `6b30:01a5`, `6b30:024c` |
| `cok_combat_sight`, `cok_combat_in_arc` | `6b30:03f1`, `6b30:054a` |
| `cok_combat_list` | `6b30:08d8`, `0033` |
| `cok_combat_enemies`, `cok_combat_distance`, `cok_combat_direction` | `6346:26e2`, `2888`, `34e9` |

`6346:24d7` in combat keeps a dying record's damage past its hit points
as its rounds dying, and a record that drops loses its initiative and is
counted out of its side at once. `6346:31e9` finds, and bandages if asked,
the first dying party member on the party's side wherever it is:
unconscious, no rounds dying, "is bandaged" (in the panel, row 10, with a
pause: `6346:326c`). `60f4:133c` takes one that can
act out of the battle with a text, a status, its hit points 0 unless it
runs (3), off the map, its turn ended and its effects that last through the
battle gone. `432f:41e2` (Alt-X, or `-` in the computer's turns, when the
game was started with `Helm`) says "The Gods intervene!", kills every record
against the party and takes it off the map, and ends every record's turn.
`6346:26e2` keeps, of the combatants listed around one, the enemies (on the
side `6346:2666` gives: 1 for the party's side, else 0), into the list and
`DS:714d`, the one who yelled (`DS:71a7`) first for one with effect 0x5b.
`6346:2888` is the distance in squares (the length / 2) with sight not
blocked, and `6346:34e9` the direction 0-7 from one record to another, the
first whose wedge (by `0x26a` and `0x6a` / 256, tan 67.5° and 22.5°) holds
it. `432f:2e82` is the most that the enemies who can act move this round,
in squares (`432f:014d` / 2, which runs each one's event 0x12).

In a battle the effects (see Spell effects) are run with the combat map:
an effect the party shares (0x15, 0x2d, 0x2e, 0x31) counts for a target
only from a holder that has it within 6 cells (Prayer) or 1 (the others),
as `60f4:0352` lists them (`6b30:08d8`, with the list's entries put back
after, but not its count). The handlers that wait for a battle are
ported with it (see Spell effects):

| Id | Handler | In a battle |
| --- | --- | --- |
| 0x1b, 0x1f, 0x33-0x35 | `3f44:00f7` | the turn ends (`6346:2964`), at event 7 |
| 3 (snakes) | `016a` | the value counts down by the round's attacks (`+0x18f` + `+0x190`, a byte), the first 3 going when they reach it; "is fighting with snakes", and the turn ends |
| 0x15 (silence) | `06b2` | one that may use items "is silenced"; it may neither use them nor cast |
| 0x1e (nausea) | `0ae0` | the same, "is coughing", its stats recomputed and its armour class 2 worse, or 0x32 for 0x34 or less; then the side panel for the selected (`6346:0af6`, drawn only while `DS:71ac` is set) |
| 0x1c (Mirror Image) | `0a2f` | on damage, a d(images + 1) above 1, unless a spell marked `DS:6b39` is cast, takes it and the effect pending: "lost an image" |
| 0x23 (confusion) | `0d7c` | at event 0x15 a d100: 1-10 it runs away (0x6f for ten minutes, the computer in control), 11-60 "is confused" and the turn ends, 61-80 "goes berserk" (0x4d, whose handler runs), 81-100 "is enraged"; then a saving throw of type 4 at -2 ends it |
| 0x1a | `09e8` | against a target (`DS:6b3f`) with `+0x13f` bit 7, the attack roll 1 better |
| 0x29 | `1412` | an attacker striking with an item of no bonus from more than a square away misses on a d100 of 100 or less: "Avoids it", no damage, the attack roll 0xff, one hit fewer (`DS:6b3c`) |
| 0x47 (spell turning) | `23a4` | a d10; on 1 for a spell of targets below 8, or always above 14, the spell is turned back on its caster (see Spells in combat) |
| 0x48 | `24f4` | each enemy within a cell (`6b30:08d8`) without 0x5c that fails a saving throw of type 4 "is terrified" (0x6f for good) and flees |
| 0x4b | `26bf` | against a target with `+0x140` bit 0, damage 3 times a d12 + 4 + strength's damage bonus, as a byte, and the attack roll 2 better |
| 0x4f | `2b5f` | "emits an evil stench"; each enemy within a cell that fails a saving throw of type 4 and lacks 0x76 "is affected" |

Outside a battle those that write a combat record (`+0x183`) or use the
map stop the run, as the original's would write through records it has
freed. Monsters carry effects that run every turn, and the timers run at
the end of each round (a minute): every handler they reach is now ported,
none is skipped. Sir Lebaum's 0x4a rolls a d20 at event 0x0e of each of
his turns and on 1 casts Dispel Magic on what the last cast left (see
Spell effects); the timers 0x0f, 0x16, 0x22, 0x2b, 0x2c and 0x3e hurt,
weaken, kill and heal as their effects run out (`eclplay --effect` gives
them for a test). 0x30, 0x3c, 0x42, 0x4c and 0x6a are ported with the
attacks, the clouds', pools' and the monsters' breath and attacks (0x28,
0x46, 0x50; 4, 6, 0x4e, 0x53-0x55, 0x5a, 0x68) with the clouds and breath
(see Clouds, pools and breath). The handlers the attacks reach
are ported with them (see Attacks, movement and death). Each effect that
ends in a battle, by its timer or the battle's end, is logged, `effect:
NAME loses 0x77`. The dead that explode (effect 0x44) explode after
every attack and every turn (`60f4:2375`, see Attacks, movement and
death), which clears the damage type (`DS:6b31`) all the same.

The battle draws the combat screen (see The combat screen) where the
original does: a turn shows its combatant (`6beb:12ef`, margin 2, with
the cursor), draws the side panel (`6346:0af6`) and at its end redraws
its cell (`6beb:02aa`); the end of each round and the Gods centre on the
view's centre (`6beb:096b`, margin 0xff); one that leaves the battle
(`60f4:133c`) is shown (`6beb:12ef`, margin 3), says its text in the
panel (`6346:1883`, row 10, with a pause), and is erased
(`6beb:0500`) and the map shown (`60f4:13c0`). The effects' texts that
the original flashes (`6346:228c`, kind 1: "is fighting with snakes", "is
confused", "is enraged", "runs away", "goes berserk", "is terrified" and
the stench's "is affected", which alone is not cleared after) flash on
the combatant, which centres the view on it unless it is shown whole.
Drawing rolls no dice: the battle's random numbers are as before.

`eclplay` logs each round's order as `round: N: NAME I, ...` (every
record, with its initiative), and each turn as `turn:`.
`--combat won`, `fled` and `lost` decide the battle in place of its
rounds, after event 0x18, and the sides are counted again before the
battle's end; `--combat gods` presses Alt-X at each player's turn that
reaches the menu (see The player's commands), as the original allows with
`Helm` on its command line: `432f:41e2` tests `ParamStr(2)` against `DS:896a` and returns at
once without it, so the cheat needs `cok_adventure.helm`, which `--combat
gods` sets (see Overhead map).

The port keeps these quirks:

- The battle's sides are those counted before setup removed monsters with
  no place: a battle whose monsters were all removed still starts, and its
  first round's end finds the enemy gone.
- Event 0x18 runs after setup's event 8, so a dragon's fear runs twice:
  those who made their saving throw the first time throw again.
- The turn order rolls a d2 for every tie, the dead's 0s too; an
  initiative of -1 rolls against the empty entries, and one below that
  passes them all to entry 65.
- Initiative is at least 1 before surprise takes 6, so a surprised
  combatant keeps a turn only when 1d6 and its dexterity's bonus make 7 or
  more.
- Half attacks round up on odd rounds, as a byte: 255 half attacks give
  none then.
- With ammunition, `432f:0e80` caps the attacks at its count only when the
  count is not 0, and one arrow allows one; having attacked, a combatant
  never gets more attacks by readying another weapon.
- Movement takes var `0x7f72` as a byte for the party's side, and 0 or
  above 0x60 counts as 1; haste doubles it as a byte.
- A dying combatant's count starts at the damage past its hit points and
  gains one at every round's end; past 9 it is dead, its hit points and
  icon unchanged. Bandaging reaches the first dying party member anywhere.
- Damage in combat counts a record out of its side each time it drops,
  even when it had dropped before, and the count wraps; one running (3)
  that is hurt drops.
- No side's end stops a round: every remaining entry has its turn.
- `432f:41e2` takes the dead off the map (size 0) without rebuilding the
  occupants (`DS:63f3`), which keep their numbers.
- The list `6346:2888` and `60f4:0352` put back keeps its new count; a
  target `6346:2888` does not find reads the last entry, or with none
  listed the first, stale.
- `6b30:03f1` tests the start cell: one standing on a wall sees nothing.
- "Continue Battle:" asks every round while the enemy is gone and the
  answer is Yes; a battle so continued ends at the round limit or when the
  party can no longer act.
- An evil stench (0x4f) adds 0x76 not to the enemy affected but to its
  holder, once for each enemy that fails, as many times as the stench
  runs.
- Mirror Image (0x1c) counts its value down by 1 for each hit it takes,
  the caster's level in its low nibble first, and rolls a d(images + 1)
  with the images in the high nibble: of a value 16N + L it takes 16N + L
  - 15 hits, and then, its die a d1, takes none but stays until its timer
  ends (its removal at 0, `3f44:0aad`, is never reached).
- Confusion's berserk (0x23, 61-80) runs 0x4d's handler with no effect,
  which it does not read.
- 0x29 takes the hit from the first slot (`DS:6b3c`) whichever slot
  struck.
- A handler that runs on its effect's removal and removes it again
  recurses. 3, removed (at its timer or the battle's end) with a value no
  more than the round's attacks, does so without end: the original has
  no stack check, and its stack wraps after some 150-190 levels of about
  84 bytes and writes over memory; the port stops at once. 0x23 does by
  its dice and ends; the port stops past 64 nested removals, and where an
  inner removal has already unlinked the effect, which the original
  unlinks again through NULL.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`: a
record with no combat record in a battle (read through NULL), a record on
a side other than 0 or 1 (counted into the bytes after `DS:6b2d`), an item
type past `ITEMS` for a readied weapon, a footprint size past the table, a
direction of 9-0xfe to `6b30:054a` (an uninitialized result), a map cell's
terrain past the table, a record that is not a combatant taken off the map
(its size would be written over the count), and a direction `6346:34e9`
finds for no wedge, where the original loops for ever.
The handlers ported for a battle stop the run with `COK_ECL_EFFECT_FAILED`
where theirs misbehave: 0x4b against no target (read at `0000:0140`) and
0x1a likewise (`0000:013f`), 0x29 reading the selected character when none
is, a record with no combat record, and the nested removals above.

Attacks, movement and death are ported (see Attacks, movement and
death), and so are the computer's turns, the player's commands, the
spells and the clouds (see The computer's turns, The player's commands,
Spells in combat and Clouds, pools and breath); `eclplay --combat melee`
attacks at each turn.

A differential test ran the original routines in an 8086 emulator against
the port. Whole battles: setup (`3cb2:1c58`), event 0x18, rounds with
every turn passing (`3afb:004b` and `3995:0573` replaced by the same end
of turn, `3995:08d5` answering Alt-X for `gods`, "Continue Battle:" fed
answers), and the battle's end, on random parties, monsters with their
effects and items, effects with timers, maps, squares, surprise, var
`0x7f72` and clocks, comparing the records, items, effects and combat
records, the map and its tables, the turn order, the lists, the sides,
round, limit, health, selection, menu item, the rolls' bytes, the clock,
the timer flags, each round's order, the turns, the texts printed (a
name and its text for `6346:1883` and `228c`) and the random seed. Of
2,000 battles, the 1,825 the port carries out without skipping a handler
agreed (1,028 to the round limit, 602 ending earlier, 195 by the Gods);
no case differed. In 108 the port skipped a handler and was compared all
the same: 44 still agreed (43 skipping 0x4c, which did nothing there) and
64 parted from the original, as the sentence on skipped handlers above
warns (0x4d 30, 0x30 13, 0x42 11, 0x0b 5, 0x4c 2, others 3). Both
refused 5, in which the original never ended (4 within effect 3's
removal); the port alone refused 62: magic resistance to an effect being
added, reading the caster's level from the selected character, which
none is (28), a side other than 0 or 1 (10), 0x23 removed by an inner
removal and unlinked again (10), other handlers reading the selected
character (10), 0x1a and 0x4b with no target (3) and a body off the map
(1). Each handler (`60f4:057c` on one record, with random rolls' bytes
and combat records): of 1,000 cases, the 935 the port carries out
agreed, the rest refused, mostly for a selected character or a target
that is not there and item types past `ITEMS`; and of 4,000 cases aimed
at the handlers ported for the battle (holders at the map's edges, up to
seven combatants around, mixed sides), the 3,585 it carries out agreed,
both refused 8 (3's endless removal) and the port alone 407, for the same
reasons and the handlers it skips. Each routine alone, on random battles
laid out with random combat records: `432f:0000`, `014d`, `0e80`, `0f9e`,
`2e82`, `3995:02a0`, `040b`, `0b6d`, `1604`, `6346:2964`,
`24d7` in mode 5, `0cdb`, `2666`, `31e9`, `60f4:1440`, `133c`, `2375`,
`432f:41e2`, `6346:26e2`, `2888` and `34e9`: of 4,000 cases, the 3,830
the port carries out agreed; the port alone refused 157 (a damaging
cloud or a handler skipped, a record with no combat record, a side other
than 0 or 1 and item types past `ITEMS`), the original alone a delayed
spell's cast (12), and both 1 that never ends. The map's routines
(`6b30:08d8`, `03f1`, `054a`, with `26e2`, `2888`, `34e9`) on random maps
and tables: 2,964 of 3,000 agreed, the rest refused for item types past
`ITEMS`; and 1,000 lines (`6b30:01a5`, `024c`), all agreeing. It is not
part of the repository. The screen was not compared.

### Death Knights' battle

Its battle (`dkk 3e05:00ec`) runs leadership first (`5340:4acf`,
`combat.leadership`, `cok_combat_leadership`): every record on the
party's side but an NPC (`+0x3e` 0x80 and up) is the player's, the others
the computer's (`+0xca`); each party member who is no NPC, has a knight
level (`+0x4c`) and is okay may lead each NPC on its side the computer
plays that is okay and of intelligence above 3, on a d100 below 20, 40 or
80 by its knights' order and 5 for each point of charisma above 13
(`5340:4a25`): that NPC is the player's for the battle. The first one led
clears the frame for `  Using leadership ability ` (light magenta) and
`<leader> leads <npc>.` (white), and any led waits for a key (`DS:0868`).
Its rounds have no dead that explode after a turn (`combat.explode`) and
end the round after any turn that leaves a side gone or reaches the round
limit, once a battle (`combat.ends_in_round`). Its round start
(`5340:0000`) stores no sweeps (the record has no fighter level). Its
turns are the next section's. After the rounds, where an effect 0x27
(its haste, `504d:0d88`) aged someone (`DS:c463`, `game->effects.aging`),
the aging (`3e05:0264`, `cok_train_aging`; see Death Knights' training).
Its end of battle (`3e05:0000`) is Champions'.

### Death Knights' fight

Its rounds, turns and attacks are Champions' routines but where the
`fight` section (`cok_rules_fight`) says otherwise, each difference found
by comparing every routine of the area with its Champions counterpart
instruction by instruction (the calls followed through their thunks):

- The words (`fight.words`): the attack's message in lower case,
  `(from behind)` without its space and ` of damage` with one, `, and is
  dying`; ` flees in panic` and ` is bandaged` with a space before; the
  panel's `hitpoints`, `(helpless)`, `(casting)`; `continue battle? `;
  and Alt-X without the Helm cheat says nothing (`3e05:08af`).
- The end of a round (`3e05:0ed0`) ages its clouds first (`638c:21ec`, its
  one list), and its cloud square (`638c:0c65`) is rewritten (see Death
  Knights' clouds). Its cloud and pool tiles, Stinking Cloud's and Cloud
  Kill's, are 0x1d and 0x1c, marked wherever they are and counted as
  terrain too (`6f03:0e33`).
- The player's turn (`3e05:08af`): see Death Knights' commands. A spell
  being cast goes to its cast entry (`5dc5:1298`, see Death Knights'
  casting).
- The computer's turn (`3f63:0000`): one with no target picks one first
  (`5340:3cac` with 0, 1, 0xff; and before a spell being cast, afresh),
  `computer_target_first`; it rolls its items' 1d7 but uses none
  (`3f63:02b8`, `computer_items`); it turns undead of kinds up to 0x0d
  (`3f63:0251`, not the stack's byte), a paladin above level 2 too
  (`paladin_turns`); its spell (`3f63:05d7` with `056c`, `03ee`, `04f2`,
  `COK_COMPUTER_CAST_DEATH_KNIGHTS`): with magic allowed, a spells' block,
  casting allowed this round and an NPC or Magic On, the memorized spells
  whose column 11 is set, in order of priority (column 13), each tried
  until one is of use (`5340:43ea`: on itself where column 6 is 0, else on
  one within the spell's range of the side column 14 says, by `5340:3fd5`'s
  test for the spell: Hold Person not on one holding 0x61 or 0x34, Slay
  Living not on creature flags' bit 3 of byte 1, a cure on one hurt, an
  effect not already held, nothing against a globe or magic resistance
  above 50, ...); none, no die rolled; else cast unless a d100 is at most
  four times the sum of its mage, paladin, ranger and knight levels (a
  byte) or a d5 is above that priority; the cast then stops at the cast
  entry. Its caster level (`65cd:2c33`, for the range) is Champions' but
  that a knight's and a ranger's levels count whole and Fireball by one
  holding 0x4e is cast at 15 (`effects.caster_*`). Its reach is the
  weapon's range, not one less (`3f63:0ccc`, `reach_less`); morale
  compares 100 less the byte var `0x7ec6` signed and takes no effects off
  one made to flee (`3f63:1260`); one fleeing turns one more
  (`3f63:08c6`, `flee_turn`), which reads its way of moving only for a try
  below 6 and a way below 7 (`ways_guarded`).
- The movement a round (the round's start `5340:0000`, the flight
  `5340:0cc0`, the fastest enemy `5340:2afe`, morale `3f63:1260`) is
  `65cd:1241`'s, where the stats count the movement's effects
  (`cok_stats_count_movement_effects`): the record's movement recomputed
  from its base, armour and load, doubled by haste (0x27) and 0x09 and
  halved by slow (0x2a), var `0x7f72` added on the party's side, doubled,
  as bytes (`cok_character_round_movement`); no event 0x12 doubles or
  halves it again, and no bounds (Champions' `432f:014d`). The event
  still runs for the attacks at the round's start. Found by DOSBox-X: a
  hasted Solamnic knight walked twice as far in the port.
- The attacks (`5340`): whether one may attack a target (`10ab`) runs the
  target's event 1 with the attacker selected and refuses one of its own
  side (`attack_sides`; its new "allies" argument, 0 in every fight's
  call, is casting's); a combatant holding effect 6 makes no free attack
  on one stepping away (`0860`, `free_attack_effect`); no dead explode
  after a blow (`148f`, `combat.explode`); one whose creature flags' byte
  1 holds bit 3 drops with no dying or killed line (`0230`, `quiet_flag`;
  the damage's message, `638c:1c52`, does the same with words of its own,
  which `cok_cast_damage` does not have yet: no fight reaches it); type 0x42 flies as an arrow (`28a0`); the missile's ends
  are kept on the battlefield (`65cd:1d56`); a lost spell is marked
  (`| 0x80`) to memorize again, not cleared (`65cd:17c4`,
  `cok_combat_forget`, cast.c's too).
- Turning undead (`5340:119b`, `COK_TURNING_DEATH_KNIGHTS`): the d20
  first; the level the better of the cleric's and the paladin's less 2,
  2 more for deity 2, above 8 made 9 (below 14) or 10, less var `0x4cff`
  (`COK_VAR_TURNING`; 0 at least); the count of undead rolled at the first
  one found, 1d6 + 6 where its row's value is negative (to destroy), else
  1d12; the table as Champions' (rows of 11 at `DS:05a5`).
- The death on the battlefield (`6f03:0fbd`): a record already among the
  bodies 1-8 does nothing, found by a search, not a byte of the stack
  (`kill_finds_body`); a body's cell keeps the one under it
  (`6f03:152e`: under a cloud the terrain it kept); a cloud's tile is kept
  (`body_spares_pool`);
  the spell being cast stays (`kill_clears_spell`).
- Its saving throw (`638c:10f8`) is Champions' but that a holder of
  effect 0x39 rolls 1 (`effects.save_fail_id`) and a mage's moon is read at
  var `0x4cf8` + its order, the moon's phase (Champions' sum wraps to
  `0x4bf8` + order: `save_moon_wraps`). The effects its first fights
  carry: 0x07 (`504d:0239`, Champions' 0x07 without its trace, only in a
  battle), 0x4f (`1e47`, Champions' with `Affected`), 0x50 and 0x5c
  (`1f6b`, `2162`: cancel 0x23 and 0x6f, `COK_FX_CANCEL` by the entry's
  value), 0x5d (`2170`: half damage from items of kind 0 or 1), 0x69,
  0x72 and 0x73 (`2404`, `27e6`, `282d`: Champions' only in a battle),
  0x6a (`2448`: the nightmare's smoke on the adjacent enemy of the most
  THAC0 plus attacks times six times dice plus bonus, without 0x6c, which
  it gets unless it saves, type 3: `is blinded by smoke`) and 0x6c
  (`26e0`: damage and attack roll 2 worse).

What it does not carry out yet stops the run, naming the routine: a
spell cast (any, the computer's too, at the cast entry), an effect
not ported (see Death Knights' effects).
`eclplay --combat auto` plays each of ECL1's fights from its `SETUP
MONSTERS` to its end or such a stop (32 won), `--combat melee` with the
port's melee (48 won).

## The combat screen

`src/arena.h` ports the combat screen: the map's drawing of overlay `6beb`,
the icons of overlays `6d21` and `4b6d`, and the side panel, messages,
missiles and flashes of overlay `6346`. Setup draws the screen
(`6346:300f`); the rounds and the later parts of combat call the rest at
the original's points.

The frame (`1128:04c1`, `cok_screen_draw`'s `COK_SCREEN_COMBAT`) clears cells 0-39 of rows
0-23, draws the moons' row as the other frames do, columns 0, 22 and 39 to
row 22 (`DS:0ebd`, `0ed4` and `0eeb`, which hold the same: tile 3 on row
1, 4 down to row 20, 5 on row 21), then row 22 across over their ends. Row
24 is kept, so "A battle begins..." stays below the map once setup has drawn
it. The map shows 7 by 7 cells of 24 by 24 pixels from the view's origin
(map `+2`, `+3`) in cells 1-21; the side panel and messages take cells
23-38 by 1-21.

The original draws the map into the 3D view's buffer (`DS:4b78`, 21 units
by 168 rows) and shows it one unit right and one cell down (`127f:12e8`,
which copies the bytes of each row written since it last ran); the port
keeps the buffer (`cok_combat.buffer`) and copies it whole, so that what is
drawn is clipped at its edges, as the original's is. A cell's terrain is
its value's tile (`DS:1ee7`), opaque (`6d21:00f7`). The cells are read
without a check: past a side of the map a cell is the row before's or
after's, and cells -3 to -1 of row 0 are the map's bytes `+4` to `+6` (the
cursor, its footprint and the sight flag); the rows above and below, which
the original reads from the heap around the map, stop the port. An icon
(`6d21:04b0`) is slot `+0x137`'s picture, ready or attacking, drawn masked
from its combatant's top-left cell at its screen position (`DS:6362`,
`63aa`, as last worked out), mirrored for a facing above 3; a large icon is
drawn whole and clipped.

| Function | Original |
| --- | --- |
| `cok_screen_draw` (`COK_SCREEN_COMBAT`) | `1128:04c1` |
| `cok_arena_redraw` | `6346:300f` |
| `cok_arena_centre` | `6beb:096b`, `07a9`, `00fc`, `6d21:00f7` |
| `cok_arena_redraw_cell`, `cok_arena_erase` | `6beb:02aa`, `0500` |
| `cok_arena_pose`, `cok_arena_turn` | `6beb:0ad8`, `12ef` |
| `cok_arena_kill` | `6beb:0e08` |
| `cok_arena_show` | `127f:12e8` |
| `cok_arena_icon`, `cok_arena_load_icon`, `cok_arena_free_icon` | `6d21:04b0`, `01d0`, `0156` |
| `cok_arena_compose`, `cok_arena_party_icon`, `cok_arena_join` | `4b6d:0784`, `0817`, `1b34` |
| `cok_arena_panel` | `6346:0af6`, `0984`, `0a0d`, `0cdb` |
| `cok_arena_say`, `cok_arena_clear_text` | `6346:1883`, `196a` |
| `cok_arena_flash` | `6346:228c` |
| `cok_arena_missile_frames`, `cok_arena_missile` | `6346:1b5b`, `1a26`, `1ba6`, `6b30:01a5`, `024c` |

Centring (`6beb:096b`) takes a cell moved a step in a direction (8 for
none). Unless it is within the margin of the view's centre (0xff: always),
the view scrolls toward it (`6beb:07a9`, `cok_combat_scroll`) and every
tile shown is drawn, then every combatant on the map, partly shown, that
can act or whose status is 10. Then the cell it started from is redrawn
(`6beb:02aa`: its terrain, and its combatant's icon whether or not it can
act), and at the target, clamped onto the map when it is not shown, the
terrain of each cell of the cursor's footprint (map `+5`) that is shown,
the cursor's box (icon slot 25) over each while the cursor is on (map
`+4`), and the target cell's combatant (`6beb:00fc`); then the map is
shown. `6346:300f` draws the frame and centres, forced, on the view's
centre. A turn's start (`6beb:12ef`) puts the cursor on in the combatant's
footprint and centres on it while its moves are shown (`DS:71ad`,
`cok_combat.show_actions`), then turns the cursor off with a footprint of 1.
A pose (`6beb:0ad8`) centres on the combatant unless it is shown whole,
erases it (`6beb:0500`: the terrain over its footprint) when it turns from
one side to the other (facing / 4 changes), for the attacking image or to
hide it, keeps the facing (combat record `+9`), and draws the image;
the drawing only while the moves are shown, the facing whatever.

A combatant that drops (`6beb:0e08`) outside combat only sounds 5 and
pauses. In combat the screen centres on it unless it is shown whole, it is
erased, sound 5 plays, and the skull (icon slot 24) flashes over each cell
of its footprint that is shown, ready and attacking in turn, nine times 10
ms apart; a party member (combat record `+0x13` clear) then leaves a body
(`DS:69ee`) at its top-left cell, which becomes 0x1f unless it holds a
green cloud (0x1e), the cell's value kept in the body; after a pause of
speed × 100 ms it is erased again and taken off the map (size 0), the
occupants are rebuilt, the view's centre cell is redrawn and its
initiative, movement, spell and guard are cleared.

The side panel (`6346:0af6`), when it is due (`DS:71ac`,
`cok_combat.panel`, which it clears), clears cells 23-38 of rows 1-21 and
shows:

| Row | Column | Text | Colour |
| --- | --- | --- | --- |
| 1 | 23 | the name (`6346:1883` with " ") | 11, 14 against the party, 12 if it cannot act |
| 3 | 23, 33 | `Hitpoints`, the hit points (`6346:0a0d`) | 10, 14 below the maximum else 10 |
| 5 | 23, 26 | `AC`, `-` and the armour class as the sheet's (`6346:0984`) | 10 |
| 7-9 | 23 | the readied weapon's name (`6346:0488`), wrapped to column 38 | 10 |
| 2 below the last text | 23 | the status (`DS:1330`) if it cannot act, else `(Helpless)` with effect 0x1f or 0x33-0x35 (`6346:0cdb`), else `(Casting)` while it casts a spell (combat record `+0`) | 15 |

Messages (`6346:1883`) in combat clear cells 23-38 from their row to row
21, show the name (`6346:199d`) at column 23 and wrap the text below it in
light green; outside combat they take rows 18-22 as before, whatever the
row. With a wait they pause speed × 100 ms and clear the text
(`6346:196a`: in combat rows 10-21 of the panel, outside rows 18-22).
`6346:1827`, the notice on row 24, has no combat branch. A flash
(`6346:228c`) in combat builds its four pictures from icon slot 0x16 (kind
not 0, sparkles) or 0x17 (a burst), centres on the combatant unless it is
shown whole, sounds 4 or 3, says the text on row 10 without a wait, then
draws the pictures masked at its top-left cell, each shown 70 ms and
erased, once for kind 0 and speed + 1 times otherwise; once, it then
pauses speed × 100 ms. Outside combat the text is said on row 10 with a
wait. The pictures of a missile or flash (`DS:719e`, `6346:1b5b`,
`1a26`) are a slot's ready, ready mirrored, attacking mirrored and
attacking.

A missile (`6346:1ba6`) flies along the line from cell to cell in steps of
8 pixels (`6b30:01a5`, `024c`: a step along the longer axis, and along the
other as an error of twice its distance a step reaches the longer's). A
missile to its own cell is not drawn. The screen is centred, forced, on the
view's centre if both ends are shown, else on their midpoint (halves
truncated) if they are at most 6 cells apart, else on the view's centre.
Each step draws the next of `frames` pictures masked, shows the map, waits
`delay` ms and erases it, until the step before the target; with a delay
of 0 it is drawn only where it lies on a cell's edge across or down, which
along a row is every step. A step that leaves the view (units 0-18 across
and down) is drawn, then, unless the screen was centred on the midpoint or
both ends, the screen centres near the target (on it, moved in by the
distance it lies within 3 of an edge, by the original's arithmetic) and the
walk goes back from the target to where it comes into this view, from which
it flies on. At the target it is drawn; with a delay it is shown, waited
for and erased, with none it stays.

The original's masked draw with a save (`127f:09c3`, flags 5) keeps what
it covers in `DS:4b7c` byte by byte, its rows running on by the units
clipped on the right but not on the left, and the opaque draw of that back
reads its rows as long as the bytes drawn in a row, so that an erase
clipped on the right shears, drawing bytes of earlier saves. A missile is
clipped only at the step that leaves the view; after it `6346:1ba6`
centres with a margin of 0xff, redrawing every cell, unless that step was
the last (`1ba6:1f6a` jumps to `21a2`): then the target is drawn and shown
over the sheared erase, which shows through its transparent pixels, as a
missile to a target on the map's right edge (x 49) can. The port keeps the
same bytes (`cok_combat.under`, `under_set` marking those a save wrote)
and stops only where a byte no save wrote, which the original takes from
the heap, would be shown.

Icons are kept by slot (`DS:6172`, `game->icons`, the game's
`startup.icon_slots` of them, `cok_icon_slots`: Champions' 26, Death
Knights' 35): 0-7 the party's, 8 on
the monsters' groups, 11 the head while a player's icon is built, and
COMSPR's ids 0-11 in 13-24 and 25 in 25, which `cok_adventure_open` loads
as the game does at startup (`3e99:06e0`, `072a`; Death Knights' 0-21 in
13-34, `4f9a:00e1`). A slot loads
(`6d21:01d0`) records id and id + 0x80 of its file, masked with colour 0
transparent: for `CHEAD` and `CBODY` followed by a letter, the letter is
dropped and a T adds 0x40 to the id; `COMSPR` as it is; any other name
gets the first digit of the ECL file (`DS:5782`) and, outside CGA mode
(`DS:4b76` not 0), colour 8 drawn black (`127f:15fd` from `DS:1e26` to
`1e36`). LOAD MONSTER's icons now load so (see Monsters and encounters).
A player character's icon (`4b6d:0817`) is its body (`+0x136`, `ICON_BODY`)
from CBODY with its head (`+0x135`, `ICON_HEAD`) from CHEAD laid over it
(`4b6d:0784`: each byte of the head's frame ORed into the body's pixels and
ANDed into its mask, from the first), small or large by `+0x138`
(`ICON_SIZE`: 1 S, 2 T), colour 8 drawn black, then each of the six bytes
from `+0x139` (`ICON_COLOURS` 1-6) giving the template's colours 1, 2, 3, 4,
6 and 7 (`DS:390b`) its low nibble and those + 8 its high one. As a saved
game's characters join (`4b6d:1b34`), every record in the list gets its
icons: a player character's built, an NPC's (`+0xe7` 0x80 and up) `CPIC`
record `+0x115` (`MONSTER_ID`) of the saved game's file. `eclplay
--test-party` gives its characters the icons a new human character gets
(`4def:3c61`: large, head 9 or 5 by gender, body by class, the template's
colours; see Creating characters).

The effects' handlers that speak say their text in the panel in combat
(row 10 with a pause, `6346:1883`; the stench's "emits an evil stench",
`3f44:2b84`, without one), and those that add an effect with text
(`60f4:20f7`) flash it, kind 1, then clear the text (`6346:196a`), so that
a red dragon's fear at setup flashes each character it terrifies or
frightens. `eclplay` logs the sound driver's commands as `sound:` and, with
`--shots`, also saves the screen once each battle is set up.

The port keeps these quirks:

- `6beb:0e08` looks for a body of the combatant through a local it never
  set (`[bp-3]`), the byte its caller's calls before it left on the stack,
  as an index b of the body table (the record pointer at `DS:69ee` + 7b);
  the callers' layout fixes it:

  | Caller | Byte |
  | --- | --- |
  | `432f:0678`, `60f4:017f`, `60f4:20d2` | `60f4:1440`'s `[bp-3]` (through `60f4:057c`, which writes no local): 0, or the low byte of the segment of the combatant's effect 0x4d node if it had one |
  | `432f:1425` | after `6346:1883`, the high byte of the far return's segment: overlay `432f`'s code segment, its stub segment, or 0 through `INT 3Fh` (`19f0:02e6` leaves the stub's IP 0x0075): unknown |
  | `60f4:24df` | 0x24, the high byte of the return address 24bf, after a `60f4:1db7` in the pass, else 0 |

  An interrupt between could leave another byte on any path. Entries 1-8
  are the bodies, which setup does not clear (only their count, `DS:6a2d`,
  in `3cb2:1cdd`), so an earlier battle's can be found. Entry 0 reads the
  record pointers of combatants 71 and 72 and entry 0x24 targets 62 and 63
  listed (`DS:6a30`), which never hold a heap pointer, so neither finds a
  body: a combatant that drops again gets a second body, the first's 0x1f
  under it. Bytes 0x31, 0x35 and 0x39 read the exploding list's entries 1,
  8 and 15 (`DS:6b45`, `6b61`, `6b7d`), and 0xdb + 4j the spell targets'
  1, 8, ..., 64 (`DS:6feb` + 28j), neither cleared with the bodies. Callers
  pass the byte and what they hold of those lists (`cok_arena_kill`);
  unknown, the port stops if the combatant is in any of them, where the
  byte could find it, and else takes it as not found. An entry whose
  record was freed since (a party member dropped by Alter or removed by
  treasure: `cok_cast_forget` marks it) matches whatever record the heap
  puts at its address: a byte reaching one, or an unknown byte with one
  among the entries, stops the port.
- Centring redraws the cell it starts from with its combatant's icon even
  if that cannot act; the scroll draws only those that can act or have
  status 10.
- Cells past a side of the map are the next or last row's.
- The status shown is the 13-byte entry of `DS:1330` by the status as a
  signed byte: status 9 shows `Battle Axe`, the item name after the
  statuses.
- A missile with no delay is drawn where either coordinate is on a cell's
  edge; one centred near its target walks back to where it comes in.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`: a
cell drawn from the heap around the map (with no party the first record
can be a monster two squares north, its view's top row above the map, so
that a party-less `eclplay` run stops at such a battle's setup); a terrain value past the table;
a tile no battle loads (0x21, terrain 0x41); a sheared erase's byte that
no save wrote, when it would be shown; a message said in combat
from a row past 20, whose text the original prints over the frame's row
22 and below; a record that is not a
combatant (`6beb:0c43` gives 0, whose entry the original reads or writes
as the count); an icon slot past the table; a footprint past the
footprints; a direction past `DS:1ed6`; a missile's line of more than 147
steps (its list of directions is 148 bytes), a fifth picture, a walk back
past the line's start, or a missile that keeps leaving the view, as one
to a target on an edge of the map from more than 6 cells away can (the
original never ends); a status whose
name is not one the original can show; a ninth body; the missile's
picture slot not 24 by 24; an icon size past 2 (the letter is read from the
strings after `DS:0877`); a head or body record that is not there to lay
together; and any masked picture drawn with none of its rows, or of its
bytes in a row, in the buffer (just above or below it, or left or right of
it by its width), whose count `127f:27af` takes as 65536, copying over
memory. The keyboard's flush after each icon load (`1614:045c`) goes
through the `flush` hook; the mouse's hiding around its drawing is not
needed (the port's cursor is never in the screen).

A differential test ran the original's routines in an 8086 emulator: its
combat setup (`3cb2:1c58`, drawing the screen and flashing for effects) on
P4's random battlefields, parties and monsters, the icons loaded and
composed by `6d21:01d0` and `4b6d:0817` and the screen's pictures made by
`127f:1c2f`, then random calls of `6beb:096b`, `0500`, `02aa`, `0ad8`,
`12ef`, `0e08` (its stack byte set), `6346:0af6`, `1883`, `196a`, `228c`,
`1b5b` with `1ba6`, and `300f`, with the picture routines of overlay `127f`
run as they are in Tandy mode and the text, the frame's tiles, the clears
and `127f:12e8` hooked; what those drew was replayed on a screen by the
port's text and picture routines and compared with the port's screen pixel
for pixel at every pause and at the end, with the pauses' lengths, the
sounds and the map, combatants, bodies, cursor and combat records after.
Of 1,000 random cases, their missiles weighted toward targets on the
map's edges, the two agreed on all 444 that both carried out (71 setups
alone, 373 with calls after them), `DS:4b7c` included, and differed on
none. On 185 both stopped: the original overwrote the buffer's pointer
(`DS:4b78`) with a count of 65536 (119), never ended (59) or stopped
otherwise (7). On 369 only the port stopped, where the original goes on
with what is not there: a head or body not in its file (100), an icon size
past 2 (92), an icon slot past the table (33), cells from the heap (30), a
missile's fifth picture (27), a record removed at setup (26), a side past
1 (24), a body's entry past the lists it can read (14), a direction past
the steps (11), a caster level with no character selected (10, from the
effects at setup), an icon slot not 24 by 24 (1) and text below the panel
(1); two cases the emulator could not run (a
page break). The party's icons (`4b6d:0817` over every head, body and
size, and random records) agreed on all 1,482 built, and `4b6d:0784` on
all 1,084 random pairs it can compose, the port refusing 198 sizes past
2, 164 heads or bodies missing and 916 heads larger than their bodies.
Missiles whose sheared erase shows under the target, from 44, 13 to 49, 2,
45, 6 to 49, 21 and 39, 1 to 49, 21 after centring on 6, 1, agreed too. It
is not part of the repository.

## Attacks, movement and death

`src/attack.h` ports the attacks of overlay `432f` (`432f:1a45` and what
it calls), sweeps, guards, attacks of opportunity, a step on the map,
fleeing and the choice of a target, the attack roll and the deaths of
overlay `60f4`, and the weapon tests of `6346`. `60f4:1db7`'s combat part
is `cok_cast_damage` (`src/cast.h`). The computer's turns (`3afb:004b`)
call them (see The computer's turns), and so do the player's commands
(`3995:0573`, see The player's commands); `eclplay --combat melee` attacks
in place of either (below).

An attack (`432f:1a45`) sets the actor's moves shown and the panel due
(`DS:71ad`, `71ac`) and the round limit to the round + 15. A target hit
fewer than twice, unless the attack is from behind (`432f:0986`'s), turns
away from the attacker (its facing, combat record `+9`, the direction to
the attacker + 4); then, if any cell of it is on the view (`6beb:06ef`),
it is drawn facing the attacker (`6beb:0ad8`), which sets its facing to
that. One hit twice or more that is shown flips (+ 4, not from behind)
and is drawn back as it was. The attacker shows its panel, turns to the
target in its attacking image, aims at it (`+0x0a`), and after 100 ms the
item it was given flies (`432f:2beb`, below), then a sling's stone
(weapon type 0x1c or 0x1d). With attacks left (`+0x18f` or `+0x190`,
`ATTACKS_LEFT`) it is selected (`DS:6096`) while it strikes (`432f:1579`),
and the item is used up: but for a hoopak's (`6346:3086`), its count
(`+0x39`), if not 0, less the attacks made with slot 1 (`DS:7195`), a byte;
at 0 it goes (`6346:1697`), and if the weapon is thrown (`6346:30bd`: a
missile weapon whose type's flags have 0x14, or a hoopak) and the item is
not a spiritual hammer (`+0x3d` 0x17), a copy, unreadied, goes into the pool
after the missile recovered (`DS:60a2`) or at its end, and becomes that
missile. Its stats are then recomputed (`6346:0d20`), undoing the dice for
large targets. With no attacks left, its turn ends (`6346:2964`); if it is
shown it is drawn back in its ready image.

A strike (`432f:1579`) clears the hits (`DS:6b3c`, `6b3d`), the attacks
made (`DS:7195`, `7196`) and the damage, and marks the attacker as having
attacked (`+0x08`). A helpless target (`6346:0cdb`) is slain from the slot
in progress (`+0x04`), or the first below it with attacks left, counted in
`DS:7194` + slot: "slays helpless", "with one cruel blow", its hit points +
5 dealt, as a byte (dying unless that wraps); the attacker's invisibility
ends and it has no attacks left. Otherwise:

1. Against a large target (`+0xcf`, `SIZE`, above 0x80 or `& 7` above 1)
   with a weapon readied, slot 1's dice are the type's for large ones (bytes
   2 and 3), the bonus less byte 11 plus byte 4.
2. The target's stats are recomputed and its event 0x0b runs.
3. The armour class is the target's from behind (`+0x18e`) less 4 for a
   backstab (`432f:2af0`); else from behind when asked, or when the target
   has been hit more than once, faces as the attacker's direction to it
   and has turned more than 4 (`+0x12`); else `+0x18d`. Range then worsens
   it (`432f:1ddc`): for a missile weapon, by 2 and then 3 more for each
   third of its range less 1 (byte 12) the distance passes.
4. From the slot in progress down to 1, each attack left (`+0x18e` + slot,
   `ATTACKS_LEFT` slot) while the target has not dropped: counted, the slot
   recorded, the roll (`60f4:1062`); a hit, or a helpless target, counts in
   `DS:6b3b` + slot, sounds 7, rolls the damage (`432f:01ba`), says it
   (`432f:033e`) to a target that can act, and runs event slot + 1 (2 or 3:
   poison, paralysis) on the attacker while the target can act and some was
   dealt; then a target that dropped ends the attacks, and an attacker that
   dropped loses that slot's (not the next one's). Only a hit tests them: a
   target that dropped otherwise is swung at until one lands. The dead that
   explode go off after each attack (`60f4:2375`).
5. With no hit, sound 9 and "and Misses" once.

The turn is over when no attacks are left, or the attacker cannot act;
then it ends. The sweeps left (`+0x05`) are 0.

The roll (`60f4:1062`) ends the attacker's invisibility (`60f4:1408`) and
rolls a d20 (`DS:6b3b`): 1 or less misses, 20 counts as 100. Then the
attacker's effects for event 0x0a and the target's for 0x10 change it; it
hits if not negative and, with the attacker's THAC0 (`+0x18c`) and var
`0x7f71` for the party's side or `0x7f70` for the other as a signed byte,
it reaches the armour class. A case on the constant 500 that would adjust
the party's (`60f4:10c8`, perhaps meant for the difficulty) never matches.
The damage (`432f:01ba`) is slot's dice (`+0x190` + slot of `+0x192` +
slot, `DICE` and `DICE_SIDES` slot, `60f4:1261`) plus its bonus (`+0x194` +
slot, `DAMAGE_BONUS`, signed), a byte; the attacker's effects for event 4
and the target's for 5 change it; a backstab multiplies it by 2 + (thief
levels - 1) / 4 (former ones counting as `66c2:0efb` allows), a byte; the
damage type is 0. A backstab (`432f:2af0`) is a thief's (`+0xff`,
`CLASS_LEVEL` 6, or `+0x107`, `FORMER_LEVEL` 6, usable) with no weapon or a
hoopak, club, dagger or broad, long or short sword (types 0x43, 3, 4,
0x11-0x13) against a target hit more than once, a square away (`6346:2888`),
of one cell (`+0xcf`, `SIZE`, & 0x7f at most 1), facing as the attacker's
direction to it. Before an attack the callers book it (`432f:19b6`): the
defender's hits count one more and its turning (`+0x12`) the turn from its
facing to the attacker, 0-4, modulo 8.

The message (`432f:033e`): the attacker's name and "Attacks",
"-Backstabs-" or "slays helpless" from row 10 (`6346:1883`, no wait), the
target's name on row 12 (`6346:199d`), and in cells 23-38 of rows 13-16
"(from behind) " and "Hitting for N points of damage" ("1 point"), "with
one cruel blow" or "and Misses" (`1521:04ac`), the damage dealt
(`6346:24d7` in combat) before it is printed, unless the target is gone
(status 8). Damage takes the target's may-cast (`+0x01`); one casting
(`+0x00`) "lost a spell" (row 12, with a pause), forgets it (`6346:161b`)
and its turn ends. After a pause, a target that dropped "goes down" from
the row after the text, then "and is Dying" (status 5, `1521:0353`) or "is
killed" (6-8) two rows below, loses the effects that last only through the
battle (`60f4:1440`), runs event 0x0d and, unless exploding (status 10),
dies on the screen (`6beb:0e08`), else pauses. The text is cleared
(`6346:196a`).

A missile (`432f:2beb`) sounds 0x0c and flies (`6346:1ba6`) from the
attacker's cell to the target's: darts, javelins, quarrels and arrows
(types 5, 7, 0x0c, 0x1e) as one picture of COMSPR's arrows by the
direction (slots 13-15, ready or attacking, mirrored for 5 and 7,
`6346:1a26`), sound 0x0c again, 10 ms a step; hand axes, clubs and hammers
(2, 3, 6) spinning (slot 0x10's four), sound 9, and 0x36 and 0x37 (slot
0x11's), sound 6, 50 ms a step; sling stones (0x1c, 0x1d, 0x43; slot 0x15)
10 ms, sound 6; anything else (slot 0x14) 20 ms, sound 9.

A sweep (`432f:0fce`) by an attacker with fewer attacks left (`+0x18f`)
than sweeps (`+0x05`, the fighter levels at the round's start), against a
target below 1 hit die (`+0xd6` 0) a square away: if more enemies a square
away (`6346:26e2`) are below 1 hit die than it has attacks, it "sweeps"
(row 10, with a pause), the target moved to the head of the list, and
attacks each of them in order, one attack each, at most its sweeps.
`432f:11d4` says whether one can attack another: no one for none, itself
yes, else unless the target's effects for event 1 make it so it cannot
(`DS:6b37`: invisibility, blink), then the attacker's for event 0, which
runs no handler, its aim the target meanwhile. `432f:3f9f` picks a target
for the computer (P8) and some effects: it keeps one on the other side
that can act and can be attacked, unless forced; else up to 20 times a
d(the enemies within range), but the one who yelled (`DS:71a7`) listed
first at the first try for one with effect 0x5b, takes one that can be
attacked, or takes it off the list; a second pass sees through walls (map
`+6`) unless forced, where flag takes the pick untested.

A step (`432f:077a`) costs the cell's terrain cost twice, or three times
on a diagonal, as a byte; with less movement left, none is left. The
computer's actor is centred on first when the cell is off the view; the
actor is erased, moved, the occupants rebuilt, and drawn there; its hits
and turning are cleared, sound 0x0a, and each enemy beside it that guards
(`+0x07`) and is not helpless stops guarding and attacks it (`432f:068f`).
One that cannot act or is helpless after has no movement left. Before a
step, each enemy beside the mover, not helpless or made to flee (`+0x10`),
that it would leave (`432f:0986`, its place moved for the while to list
those beside it after) attacks it once from behind if it can attack it and
has no missile weapon, or a thrown one: at once if it has not had its turn
or not been hit, else if the mover lies in its arc (`6b30:054a`) from one
of five directions from its facing + 6; the last of its two slots with
attacks left, or 1 (2 with no attacks in the first), given one attack if
it has none; its aim put back. `432f:0dc7`, fleeing: with no enemy on the
map, or moving farther than the fastest enemy (`432f:2e82`), or as far and
a d2 of 1, it "Got Away" (`60f4:133c`, running); else "Escape is blocked"
on row 24; its turn ends.

`60f4:00eb` kills with a text (row 10, no wait): unless dead, stoned or
gone already, which only says it, the record takes the status, cannot act,
has no hit points, loses its battle's effects, runs event 0x0d and, if it
stays down and is not exploding, dies on the screen; then a pause, the text
cleared, and outside combat the party list redrawn. `60f4:22b7` puts a
record back on the map where it was (`6beb:10f3`, its bodies taken back);
if it fits it is okay with the hit points given, shown (`6beb:12ef`) in
combat, flashes with a text (`6346:228c`, kind 1), and the sides are
counted. `60f4:1db7` in combat (`cok_cast_damage`) flashes its text
("takes N points of damage", kind 0, a burst), deals the damage through
`6346:24d7`, takes the may-cast, makes one casting lose its spell, and for
one that drops says "Goes Down" (", and is Dying", or "is killed") from the
row after the text, then as the message above. The dead that explode
(`60f4:2375`, after each attack and turn) each say "explodes." (row 10,
with a pause), deal 1d6 (`60f4:1261`) through `60f4:1db7` to each other
combatant `6b30:08d8` lists within a cell, but those exploding, in the
order listed before any damage; then each is dead and dies on the screen.
Those that explode meanwhile are added and go off in the same pass. The
list is emptied.

| Function | Original |
| --- | --- |
| `cok_combat_attack` | `432f:1a45`, `2beb` |
| `cok_combat_strike` | `432f:1579`, `033e` |
| `cok_combat_hit`, `cok_combat_reveal` | `60f4:1062`, `1408` |
| `cok_combat_damage_roll` | `432f:01ba` |
| `cok_combat_book`, `cok_combat_backstab`, `cok_combat_range` | `432f:19b6`, `2af0`, `1ddc` |
| `cok_combat_sweep` | `432f:0fce` |
| `cok_combat_can_attack`, `cok_combat_pick_target` | `432f:11d4`, `3f9f` |
| `cok_combat_step` | `432f:077a`, `068f` |
| `cok_combat_opportunity` | `432f:0986` |
| `cok_combat_flee` | `432f:0dc7` |
| `cok_combat_kill`, `cok_combat_revive` | `60f4:00eb`, `22b7` |
| `cok_cast_damage` | `60f4:1db7` |
| `cok_combat_explode` | `60f4:2375` |
| `cok_combat_thrown`, `cok_combat_hoopak` | `6346:30bd`, `3086` |
| `cok_arena_missile_frame` | `6346:1a26` |
| `cok_combat_melee` | eclplay's `--combat melee` |

`eclplay --combat melee` stands for the computer's and the player's turns
alike: each turn attacks the nearest enemy it can attack
(`6346:26e2`, `432f:11d4`) a square away, or with a missile weapon and its
ammunition and none a square away, within the weapon's range (byte 12 less
1), as the player's `Aim` does (`432f:3219`: the cursor off, the view's
centre redrawn, a sweep, or the target booked and attacked with the
ammunition, but none for a thrown weapon a square away); again while the
turn goes on (a target felled with attacks left); with none, the turn ends.
An attack's text that does not go through `6346:1883` is logged as
`attack:` (the target's name and the line of `1521:04ac`, or "and is
Dying"), and the seed at the battle's end as `combat: seed N`.

Effects whose handlers the attacks reach are ported with them (see Spell
effects): poison on a hit (0x40, 0x41, 0x56, 0x57, `3f44:1b55`: a saving
throw of type 0 by the attacker's target with the bonus of each and the
value of its 0x74, or "is Poisoned", 0x37 and "is killed"), paralysis
(0x45, 0x51 with no damage, 0x58 against races above 1, `3f44:000d`: a
saving throw, or a flash, "is Paralyzed", 0x34 for 100 minutes), Molly's
weapon (0x07, `3f44:0208`), blink (0x25, `0f50`), the damages of 0x69,
0x72 and 0x73 (`349c`, `376f`, `37b0`), disruption (0x75, `37fc`), fire
shield (0x70, `36e1`), and at death the weapon lost (0x43, `1dc5`), the
dead that explode (0x44, `1f97`) and going mad and arising in a new form
(0x0d, 0x20, `04c2`, `0bc9`); and the combat parts of charm taking hold
(0x0b, `03cd`) and of berserk (0x4d, `29d5`). A weapon lost (0x43) goes to
`DS:609e`, after those lost before, and back at the end of combat
(`351b:185f`, see Treasure and the end of combat).

The monsters' handlers that the start of a turn (event 0x0f) and the
end of an effect reach need only these routines and are ported too:
immolation (0x30, `3f44:15cd`: "immolates" with a pause, then 1d6 through
`60f4:1db7` to each other combatant within a cell that can act, listed
before), zapping (0x42, `1c54`: the first listed within a cell on another
side "gets zapped!" and takes 2d6, and the holder's turn ends), bursting
(0x3c, `18b9`, only when it ends, as death throes' 0x3a leave it: with
those within a cell listed, it "explodes!" and is gone (`60f4:00eb`,
status 8); each other listed then that can act takes 3d6 (the list is
read after the death, whose events can work out a distance, `6346:2888`,
as 0x43 does, which puts the entries back but not the count, so others
listed before may be reached, and past them entries of none, read at
`0000:0189`, where the port stops) and, unless it makes a
saving throw of type 4 (`60f4:113a`), "is stunned" and gets 0x6a for
good; then the dead that explode go off), stunned (0x6a, `34db`: the
turn ends) and gating (0x4c, `272a`, Sir Lebaum's: below half its hit
points (`+0x197` < `+0x62` / 2) the records waiting (status 9) in the
party list, from the first, are put back (`60f4:22b7`, "gates in", with
their full hit points) on the first free cell of floor (0x17) found
going round the first record on another side, north first and clockwise,
the next direction for the next one, then around the next such record
and lastly the holder; it stops at the first not waiting, and the
holder's 0x4c goes). The dice are `60f4:1261`'s.

The port keeps these quirks:

- A target turns its facing away from its attacker unless it is shown:
  the view decides whether a later attack is from behind.
- A helpless target's blow searches the slots down past 1 to 0, whose
  "attacks" are the armour class from behind (`+0x18e`), counted in
  `DS:7194`, which the list of enemies holds its 72nd entry in. The port
  finds the byte as the original does, `ATTACKS_LEFT`'s offset for slot 0
  through `cok_field_offset_unchecked`, bounded by the record's size.
- Damage of a negative total wraps (`432f:01ba`'s test for it compares
  unsigned), and a backstab's product is a byte.
- A large target's dice stay the attacker's until its stats are
  recomputed after the strike; the target's are recomputed before.
- An attacker that drops in its own strike (an explosion, a fire shield)
  loses that slot's attacks but strikes on with the next; a target that
  drops other than by a hit (an explosion) is swung at until a hit
  lands.
- The ammunition's count drops by the attacks made with slot 1, a byte,
  and wraps when fewer are left; one item uncounted (0) goes at once.
  Darts and javelins (flags 0x1a) are not thrown weapons by `6346:30bd`:
  they are lost, where hand axes, clubs, hammers and hoopaks go into the
  pool.
- `6346:3111` takes a sling's flags (0x0a) as having ammunition: slings
  never run out.
- An attack's item that an effect takes during the strike (0x43 takes the
  striking item, the arrows of a bow, from an archer that kills a bearer a
  square away; or darts and javelins) is unlinked and freed, and
  `432f:1c1f` goes on with the freed node, whose count is intact: less the
  attacks made with slot 1, and at 0 `6346:1697`, not finding it among
  the attacker's items, says "Tried to Lose item & couldn't find it!" on
  row 24 in yellow and waits for a key (`6346:1670`, `1521:096c`). The
  weapons lost keep the count it had. Its tests of a hoopak and a thrown
  weapon read the readied weapon, which 0x43 has just cleared
  (`3f44:1f7a`): the freed node is never copied into the pool.
- The pool's missile is the item a thrown weapon's copy lands after;
  experience for magic items stops there (see Treasure and the end of
  combat).
- An attack of opportunity's attacker that has had its turn and been hit
  tests five arcs from its facing + 6; one that has not attacks whatever
  its facing.
- A step's cost is a byte: a wall's 0xff twice is 0xfe.
- With no weapon readied, `432f:1a45` reads the weapon's type through NULL
  (`0000:002e`, a byte of the interrupt vector of IRQ 3) for a sling; the
  port takes it as no sling.
- Molly's weapon (0x07) rolls its die at the attack roll and again at the
  damage, and sets the THAC0 to the base before adding to it.
- A charm that takes hold (0x0b) puts the record on side 0 unless its
  value passes 0x7f (`3f44:045d`).
- Poison kills on a failed save; disruption of an undead of a kind makes
  it gone (status 8).
- A record that goes mad (0x0d) or arises (0x20) is okay and can act even
  when it cannot be put back on the map.
- Fire shield's zap ends with the damage and its type put back.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`: more
than eight around one that immolates or bursts
(`3f44:15cd` and `18b9` copy the ninth over their loop's count), or none
listed around one that immolates, off the map (`3f44:163b`'s count less
1 wraps to 255 and it hurts what the stack holds); gating
that tries a cell off the map, which it then tries forever (`3f44:2895`
moves to the next direction only after a cell on the map), reads past the
party list's last record, places a record that is not a combatant
through entry 0, over the count (`3f44:2901`), or, the last waiting not
put back, places the next, through NULL (`3f44:28e2`); a helpless
target's slot search past the record; an attack slot past 2; a
target a sweep's list lacks (its place a byte never set, `432f:110b`); a
step off the map (the heap around it); more than 12 enemies beside one
(`432f:0986`'s list) or 8 around one that explodes (`60f4:2452` copies
them over its own pointer); a NULL in the list of the dead that explode; a
target of an effect that is none (Molly's weapon with none, poison,
paralysis, the damages and disruption, read at `0000:...`); Molly's weapon
with no weapon readied, whose bonus is a local never set (`3f44:024c`); and
records that are not combatants, have no combat record or lie on a side
other than 0 or 1.

Molly's local (`[bp-6]` of `3f44:0208`) is what the stack held. At the
attack roll (event 0x0a) it is whatever earlier code left there: the
emulator's harness zeroes the stack (filled with 0xa5 it reads 0xa5), and
with 0x2d or 0x2e in the party it is a byte of a stack address. At the
damage (event 4) the frame lies 2 bytes higher, where the same handler at
the attack roll kept the offset of its target's pointer (`[bp-4]`,
`3f44:0262`): its low byte, 0 or 8 by where the heap put the target's
record, unless prayer (0x31) in the party had `6b30:08d8` write a row of
a footprint there (11 to 14) in between. It depends on the heap and the
stack, so the port still stops; the player's turns reach it when Molly
unreadies her hoopak (View, Items, Ready) and then attacks.

The monsters' breath (event 0x0e), which picks its targets through the
spell target routine (`*DS:6e3a`, `432f:2337`) and uses the cones and
bolts of overlay `5b04`, is ported with the clouds (see Clouds, pools and
breath); casting in combat and turning a spell back (0x47) with the
spells (see Spells in combat).

A differential test ran the original routines in an 8086 emulator against
the port: P6's random battlefields set up by both (`3cb2:1c58`), then
random calls of `60f4:1062`, `432f:01ba`, `1579`, `1a45` (with its
missiles and ammunition), `0fce`, `11d4`, `3f9f`, `077a`, `0986`, `0dc7`,
`60f4:00eb`, `22b7`, `19b6`, `2af0`, `1ddc`, `60f4:2375`, `1db7` in combat,
`01e9` (an effect's end), the events the attacks and turns raise and the
`--combat melee` rule (`432f:3219` at the nearest enemy), with records,
combat records and the battle's tables poked between: every record's
attack slot in progress (combat record `+4`, which only a round's start
sets) at the start and, mostly, before each attack, with its attacks left
and THAC0 at times, so that strikes swing and hit. The records, items,
effects, combat records, map, tables, rolls, seed, pool and weapons lost
were compared after, and the lines logged as `print`, `attack`, `sound`
and `unported`. Besides the general cases, generators aimed at thrown
weapons and missiles with and without ammunition, the pool, backstabs,
sweeps, guards and attacks of opportunity, the ported effects, the
turn's handlers, gating, and archers killing bearers of 0x43. Of 49,000
cases, 22,820 ran through on both and agreed, and none differed: 10,096
of 20,000 general ones, 863 of 3,000 battles fought out (40 to 120 calls
each, most of them turns of the melee rule), and the rest of the aimed
ones. In samples of those carried out, strikes hit 263 times (158 of
1,045 general cases) and 319 times in 113 of 136 long battles; backstabs
163, sweeps 28, attacks of opportunity and guards 163 (105 from behind);
immolation 305, zapping 22, bursting 151 (154 stunned), gating 307 put
back; weapons lost 117, and the freed arrows' "Tried to Lose item" 15.
On the cases where a handler not ported ran (0x46 at a death, 0x47 at
setup) the port parts from the original as the rounds' section warns;
otherwise only the port stopped, where the original reads or writes what
is not there (as listed above, chiefly a target or selected character
that is none, an icon slot past the table, a revived record that is not
a combatant and a step off the map), or both did; and the setups stopped
as P6's do. A ninth body (`DS:69ee`, which the original writes over its
count and the map's pointer) takes a party member killed twice in one
battle, which play cannot do: the random setups' records killed again
make the few left in the long battles. Two divergences the runs found are
fixed in the harness (a slot left pointing at an item freed during a
strike run alone compares as none, as `1a45` recomputes the slots after)
and in the port (immolation with none listed is refused: `3f44:163b`'s
count less 1 wraps to 255). A last sample of 13,000 fresh
cases across every generator, with bursters that bear 0x43, agreed on
all 5,907 both carried out. The original's `432f:0dc7` returns
`6346:2964`'s 1 whether or not one gets away. It is not part of the
repository.

## The computer's turns

`src/ai.h` ports overlay `3afb`, the turns of the monsters, of NPCs and of
party members on Auto (`+0x18b`), with turning undead (`432f:12b7`,
`14a2`, in `src/attack.h`). The battle's turn (`3995:040b`) hands such a
turn to `3afb:004b` (`cok_combat_computer`), and the player's to the
player's commands (`3995:0573`, see The player's commands).

A turn (`3afb:004b`):

1. The keys (`3afb:1200`): a key waiting is read, a 0 followed by a
   second read, as scan codes come. Alt-M (or `2`, scan code 0x32) turns
   Magic On or Off (`DS:7198`, "Magic On" or "Magic Off" on row 24); Space
   gives every party member (`+0xe7` below 0x80) whose status is not 1
   back to the player, and if the one whose turn it is is among them, its
   initiative becomes 0x14 and it does nothing more; Alt-X (or `-`) runs
   the Helm cheat (`432f:41e2`) in a game started with `Helm`. The
   keyboard's flush after (`1614:045c`, `3afb:12eb`, with no key too) goes
   through the `flush` hook. Row 24 and the
   panel's text are cleared.
2. One that cannot act ends its turn (`6346:2964`).
3. Its way of moving (combat record `+0x15`): a row 1-4 stays unless a d4
   rolls 1; then, or for any other row, a d8 of 8 gives 4 + d2, else a
   d4. The dice are rolled whatever the turn is to be.
4. Unless the turn is over, morale (`3afb:1316`): it is not fleeing
   (`+0x14`); one made to flee (`+0x10`: fear, confusion) flees, "is forced
   to flee" (row 10, with a pause). An NPC or monster (`+0xe7` from 0x80)
   holds while its morale, (`+0xe7 & 0x7f`) × 2 as a byte, 0 above 0x66,
   in `DS:6b3e` after its effects for event 0x11, is not 0 and reaches 100
   less its hit points as a percentage of its most (a signed division:
   above its most, the share passes 100); else, on the side against the
   party, while the enemies' health (`DS:7197`, again through event 0x11)
   is not 0 and reaches 100 less var `0x7ec6`, as words. Failing both: if
   the fastest of its enemies (`432f:2e82`) moves farther than its own
   movement (`432f:014d`) halved, one of intelligence (`+0x13`) above 5
   "Surrenders" (`60f4:133c`, status 4) and its turn ends, and one of 5 or
   less fights on; else it flees, losing effects 0x4a and 0x4b. Party
   members never test it.
5. One fleeing that was not made to "flees in panic", at each turn.
6. Unless the turn is over, the first that applies: an item's spell
   (`3afb:04b2`), a spell begun at an earlier turn (`5b04:1415`), turning
   undead (`3afb:024b`) or a spell (`3afb:0613`), each ending the turn;
   else it readies its weapon (`3afb:1608`), reads the keys again and,
   while its turn goes on, picks a target (`432f:3f9f` with flag, range
   0xff) and, with one, its initiative above 0 and able to act, moves and
   attacks (`3afb:0d49`), else guards or ends its turn (`3afb:118e`).

An item's spell (`3afb:04b2`): a d7, then, if it may use items this round
(combat record `+0x02`), some enemy can act and the area allows magic
(var `0x4be5`), for passes 1 to the d7 at levels 7, 6 and so on, the first
readied item that is no scroll, casts a spell (`+0x3e` below 0x80) and has
one (`+0x3d`; one above 0x38 is tested as 0x17 less) that suits it at the
level. A spell (`3afb:0613`): the spells it has memorized, from `+0x1f`
(the byte at `+0x1e` is not one; those marked to be learned, + 0x80, are
taken too) if it may cast this round (`+0x01`), and a d7; if it has any,
is an NPC or monster or Magic is On, and some enemy can act: for passes 1
to the d7 at levels 7, 6 and so on, three draws of a d(spells) each, the
first that suits it. A spell suits at a level (`3afb:03b9`) if its
priority (spell table byte 13) reaches the level and either it is not
aimed at enemies (byte 14) or some enemy is within its range (`5b04:0ecb`:
byte 2 plus byte 3 times the caster level, `6346:29fe`, 6 while an item is
used; 0 is 1 when byte 6 is set, and 0xff is 1) and, for an area (byte 15,
its radius), no enemy stands where the area (`6b30:08d8`) would take in one
of the selected character's own side that, unless the spell's save (byte
8 = 1) negates it, fails a saving throw of type byte 9 (`60f4:113a`, at
-2 on the party's side, +8 on the other); every such one throws, failing
or not (`3afb:02b7`). Cure Light Wounds (3), whose test for a target
(`432f:1eed`, see Spells in combat) comes after the priority, has
priority 0, so the computer never chooses it, nor any spell of priority
0; it casts one begun while the player had the character. The spell
bytes of marked spells lie past the table's end (up to `DS:41a3`), which the
original reads; the port reads them from the executable's data.

What the computer has chosen, by all of the tests above with their dice
and saving throws, it casts (`432f:28bd` with the computer's flag:
`cok_combat_cast`, no redraw, "Begins Casting" for a spell that takes
time) or uses (`546c:24d7`, `cok_item_use`), the turn then over; a spell
begun at an earlier turn is cast (`5b04:1415`, the computer picking its
targets) and the turn ends. Unlike the player's turn (`3995:0573`), the
computer's leaves the spell begun in its combat record while it casts
it, so one hurt by its own spell (a Fireball around itself) "lost a
spell" and forgets another of it. See Spells in combat.

Turning undead (`3afb:024b`): a cleric (`+0xf9`, `CLASS_LEVEL` 0, above 0,
or `+0x101`, `FORMER_LEVEL` 0, above `+0xd7`, `CHANGE_LEVEL`, signed) that
has tried fewer times this battle (combat record `+0x11`) than undead were
loaded (`DS:8859`) turns if an enemy undead not made to flee is of a kind
(`+0xda`, `UNDEAD`, signed) no higher than a limit `3afb:024b` never sets
(`[bp-6]`): the byte its callers left there, which the calls before it at
the same depth fix. The way of moving's dice (`60f4:1216` pushes the sides
for `Random` at that place) leave 2 or 4, the sides of the last; "is forced
to flee" (`3afb:1316` builds the string over it) leaves its fifteenth
letter, `l` (0x6c), and "flees in panic" the low byte of the return address
`3afb:0142` that the call of `6346:1883` pushes there, 0x42. So the computer
turns only kinds up to 2 or 4 unless it flees. An interrupt between could
leave another byte. The turn (`432f:12b7`, also the player's `Turn`): "turns
undead..." (row 10, no wait), row 24 cleared and a pause; its tries count
one more; 1d12 of the undead may be turned, against one d20, at its level:
the cleric's and the former cleric's that a human may use, + 2 for deity 2
(`+0x5d`, `DEITY`), a byte. While `432f:14a2` finds an enemy undead not made
to flee of the highest kind no higher than the kinds left (from 13; the
first of that kind in the list of enemies, range 0xff) and some are left to
turn, the table `DS:0444` + 13 × kind + level (signed) gives what the d20
must reach in size: below it, the kinds left are those below; else the
undead is shown (`6beb:12ef`) with its panel and, above 0, "is turned"
(`60f4:133c`, running) or else "Is destroyed" (no wait), dies on the screen
(`6beb:0e08`) and is gone (status 8). Each turned leaves one fewer to turn,
but a destroyed one of a value below 0 at the last keeps one while six bonus
turns last. With none turned, "Nothing Happens..." on row
24. The sides are counted, the turn ends and the text is cleared. The
level is not clamped: past 12 it reads the next kind's row, and past the
last kind's the camp menu's strings, where the port stops beyond them.

The weapon (`3afb:1608`): the hands of the readied weapon and shield are
taken off those its items hold (`+0x17b`) for the while; of the items it
may use (type byte 13 and `+0x11a`) in slot 0, the best missile weapon
(type flags, byte 14, & 8 or & 0x10: a thrown weapon counts as both) of
worth above 1 and the best for melee (& 8 clear) of worth above its bare
hands (`+0x10d` × `+0x10f`, plus twice `+0x111` if positive), and the best
shield (slot 1) by its bonus + 1 (0 below 0). A weapon's worth
(`3afb:14ca`), a byte: its dice (bytes 9 × 10), plus 8 times its bonus
and twice the type's bonus against small targets (byte 11) if positive;
holy water (type 0x36) 8 against a target that is undead; a missile weapon
(& 8) twice its rate less 2; 3 more for one hand (byte 1); 0 if its hands
and the others held pass 3, for an alignment item (`+0x3e` 0x84) of
another alignment (`+0x3d` & 0x0f against `+0x10a`), for `+0x3d` 0x53, or
cursed (`+0x36`). The missile weapon is taken if worth more than half the
other, with its ammunition (itself if thrown, flags & 0x10; for & 8 the
arrows readied (& 1) or the quarrels (& 0x80); flags exactly 0x0a, slings,
need none) and either a thrown missile weapon of range above 1 or no enemy
a square away (`6346:26e2`). Unless the weapon readied is cursed or the
one chosen, it is put away (`546c:1ea7`, Ready's toggle, on the selected
character, which the port requires to be the one whose turn it is), the
stats recomputed (`6346:0d20`), the shield's hands taken off again unless
it is cursed, and the chosen one, if any, readied. With its stats and
attacks (`432f:0e80`) recomputed: with more than two hands held, the
shield is put away, or with none or a cursed one the chosen weapon is
toggled; with fewer than two, unless the shield is the best or is cursed,
it is put away and the best readied. Ready's notices are said ("It's
Cursed", "already using ...", but not "Your hands are full!" for the
computer), and the panel redrawn if anything was readied.

Moving and attacking (`3afb:0d49`): no last step (`DS:43c8` 8), not stuck
(`DS:43c9`, `43ca`); event 0x0e, where the monsters' breath fires (see
Clouds, pools and breath); one on the
party's side bandages the first dying party member (`6346:31e9`), which
ends its turn. Then at most 20 passes while its turn goes on (the 21st
guards, and that pass goes on all the same): one fleeing steps while it
has movement and initiative other than 0x14; with its reach, its weapon's
range (byte 12) less 1, 1 for 0, 0xff or no weapon, it keeps its target
only if that can act and is on the side against the party (a monster's
target is always dropped), can be attacked (`432f:11d4`) and is seen
within reach with sight blocked (`6b30:03f1`, map `+6` cleared); else a
d(enemies within reach) picks one, or with none `432f:3f9f` picks one to
step toward (or it guards, and the call goes on); with a missile weapon
not thrown (`6346:30bd`) and an enemy a square away it readies another
(`3afb:1608`) and the call ends; the one picked is attacked if a square
away or it can be attacked. The attack (`DS:71ab` set): the screen
centred toward it (`6beb:096b`), a sweep (`432f:0fce`) that ends the
turn, or the defender booked (`432f:19b6`) and attacked (`432f:1a45`)
with the ammunition, none for a thrown weapon a square away, `DS:71ab`
then whether it has any. An attack that leaves attacks ends nothing;
a target that drops ends the call with the turn going on, and the turn
picks a target again. A turn that picks without end (a cursed missile
weapon with an enemy a square away readies nothing and leaves again),
where the original loops for ever, stops the port.

A step (`3afb:095a`): "Move/Attack, Move Left = N" on row 24, the keys; then
it holds (`3afb:118e`) with no move (half its movement 0) or initiative
left; every NPC and monster rolls a d100, and one on the party's side holds
if the enemies' health passes it plus the morale (`DS:6b3e`, left by the
last morale tested); one not fleeing that wears no armour (slot 2) with
`+0x5b` 5 holds. Its way: toward its target (`6346:34e9`), or, fleeing, a d2
for its row and the party's facing (`DS:6d87`) turned to 7, 2, 3 or 6 for 0,
2, 4 and 6, the other way for the party's side. Of the row's five turns
(`DS:0396`: row 1 0, -1, -2, +1, +2; 2 0, +1, +2, -1, -2; 3 -1, +1, 0, -2,
+2; 4 +1, -1, 0, +2, -2; 5 0, -1, -2, -3, 4; 6 0, +1, +2, +3, 4) it takes
the first it can step to (`3afb:0743`: on the map, not held, a cell that can
be walked on costing, twice or three times on a diagonal as a byte, less
than its movement left; a green cloud costs all unless it has 0x20, 0x1e,
0x63 or 0x3f, is made to flee or makes a saving throw of type 0; a puddle
all for one below level 7 unless 0x63 or made to flee); one fleeing that
would step off the map tries to get away (`432f:0dc7`) and stops fleeing.
None, or the way back (opposite the last step): stuck once more, the next
row; at twice the target is forgotten and picked again (`432f:3f9f` with
flag) or it guards; at three times no movement is left. Then, shown if its
target was in reach, it is on the view or on the party's side (`DS:71ad`),
it turns there (`6beb:0ad8`), the enemies it leaves attack it (`432f:0986`),
and it steps (`432f:077a`); one that drops or is helpless ends its turn;
then the cloud or puddle under it acts (`60f4:0dc3` with its flag, see
Clouds, pools and breath). Guarding (`3afb:118e`): one not helpless, without
a missile weapon and with initiative guards (`6346:29b3`: its turn ends and,
unless made to flee, it guards, "Guarding" on row 24); others end their
turn.

| Function | Original |
| --- | --- |
| `cok_combat_computer` | `3afb:004b` |
| `cok_ai_keys`, `cok_ai_pattern`, `cok_ai_morale` | `3afb:1200`, `004b`, `1316` |
| `cok_ai_use_item`, `cok_ai_cast`, `cok_ai_spell_fits` | `3afb:04b2`, `0613`, `03b9` with `02b7`, `5b04:0ecb` |
| `cok_ai_turn_undead`, `cok_combat_turn_undead`, `cok_combat_undead` | `3afb:024b`, `432f:12b7`, `14a2` |
| `cok_ai_weapon_score`, `cok_ai_choose_weapon` | `3afb:14ca`, `1608` |
| `cok_ai_fight`, `cok_ai_step`, `cok_ai_can_step`, `cok_ai_guard` | `3afb:0d49`, `095a`, `0743`, `118e` with `6346:29b3` |
| `cok_item_ready` | `546c:1ea7` |

`eclplay --combat auto` has the player's first turn press Alt-Q
(`3995:078d`): the turn is given back (`DS:72ce` less 1, initiative 0x14),
every record in the list goes to the computer (`3995:1604`), row 24 is
cleared and after 200 ms the battle runs the turn again, the computer's
(0x14 becoming 0x13): whole battles decided by the computer. The seed at
its end is logged (`combat: seed N`). `--combat pass` has every turn pass,
the computer's too, as the rounds did before the computer's turns were
ported. With no `--combat` the computer plays and the player's turns read
the keys, or with no `--keys` pass.

The port keeps these quirks:

- A monster drops its target at each call of `3afb:0d49`, which keeps
  only targets on the side against the party: only a party member on Auto
  keeps its target.
- The 21st pass of `3afb:0d49` guards and goes on to attack all the same;
  a call that finds no one to attack or step toward guards but returns
  with its turn going on, and the turn picks a target again.
- Every NPC and monster rolls a d100 at each step, which only an NPC on
  the party's side uses.
- The limit of turning undead, the stack's byte above.
- Marked spells are tested at the bytes past the spell table.
- A spell id above 0x38 on an item is tested as 0x17 less, and logged as
  it is.
- Every ally in a spell's area throws its save, though one failure
  rejects the spell.
- Morale: monsters on the party's side never pass the second test; the
  percentage is a signed division.
- `DS:71ab`, whether the target was in reach (or the ammunition), is read
  by the next step, from any earlier call, battle or turn.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`:
the weapon chosen readied with Ready on a selected character other than
the one whose turn it is (the original's Ready works on the selected);
toggling the chosen weapon when none was chosen, with more than two hands
held and no shield, or a cursed one, to put away (`3afb:1ac1` passes NULL
to Ready, which reads the interrupt table); a step
toward no target (`6346:34e9` reads where none stands, the one after the
last combatant); a turn that never ends; morale with no most hit points
(runtime error 200); turning past the table; an area spell's side read
with no selected character; a way of moving past its table, and item
types past `ITEMS`.

A differential test ran the original's whole battle, `3995:0172` with its
setup and end and the computer's turns (`3afb:004b`) as they are, in an
8086 emulator against the port's, with the same seed: the drawing run as
in P6's harness, the texts, sounds and delays recorded, no key waiting,
the player's commands (`3995:0573`) run with their key routine
(`3995:08d5`) answering Alt-Q, "Continue Battle:" No, `GetMem` a bump
allocator, the stack and heap filled with 0xa5 (and again with 0x5a and
0), `DS:6de4` and `6de9` 0xff as at startup, and `432f:28bd`,
`546c:24d7` and `5b04:1415` called from `3afb` logging what the port then
logged and ending the turn (the casts are now carried out: see Spells in
combat for the test that runs them). Parties of one to six of `SAVE/`'s
characters and `--test-party`'s fought the monster groups the scripts load
before each `COMBAT` (`LOAD MONSTER`'s constant operands in the three ECL
files, built as `LOAD MONSTER` builds them, at most eight monsters), on 3D
maps from `GEO` and on open ground, some party members given a bow or a wand.
Every record, item, effect and combat record, the tables, sides, rounds,
rolls, pool, seed and the transcript (texts, attacks, sounds, the rounds'
orders and turns) were compared at the end. Of 10,000 battles, 9,256
agreed and none differed; in 509 the port skipped an effect handler not
ported (breath, 4, 6, 0x53-0x55, 0x5a, 0x68, and 0x46, 0x4a, 0x4e) and
was not compared; in 233 it stopped at Ready given no item (`3afb:1ac1`),
each time on a character of `SAVE/` to whom the harness had given a second
weapon readied on top of the first, two items in slot 0 that Ready never
makes (given unreadied, with arrows and quarrels readied, 1,898 of 2,000
battles agreed and none stopped); in 2 the original ran past the
emulator's limit of 400 million instructions (in an earlier run such a
battle, of 242 rounds, agreed with a higher limit). In the
agreeing battles the computer had 383,769 turns, took 1,064,754 steps, was
stuck 443,307 times and held 19,371, attacked 158,425 times and swept 65,
readied weapons 199,824 times (switched to one for melee with an enemy a
square away once), guarded 179,918 times, bandaged 5,621, fled in panic
7,692 times and surrendered 5,309, tried to leave the map 1,754 times,
tested items for a spell 39,468 times and used 1,359, drew 58,184 spells
and chose 3,629, tried to turn undead 2,331 times and turned 220. Made to
flee (fear: the dragons' breath) only in battles with skipped handlers;
a spell begun earlier never. Filled with 0x5a instead, 1,852 of 2,000
agreed and none differed. A fresh batch of 5,000 agreed on 4,628, none
differing. With Magic On set at each round on both sides, so that party
members on Auto cast too, 1,834 of 2,000 agreed (86,994 spells drawn); and
with a key waiting at a chosen poll of the rounds (`1614:03c2`) on both
sides, Alt-M (`2`), Space and `-` alone and together, 1,848 of 2,000
agreed, none differing. A port whose limit for turning undead was 13
instead of the stack's byte differed from the original in 86 battles of
3,000 with casters (the review's control run), where this one agreed on
all it carried out. Each piece alone (`3afb:14ca`, `1608`,
`1316`, `004b`'s dice, `0743`, `095a`, `004b`, `03b9`, `118e`, with random
pokes of records and combat records between) on battlefields set up by
both: of 4,000 cases 3,947 agreed and none differed, the rest the port's
stops above (a step toward no target, which only a step run alone
reaches, and Ready given no item) and skipped handlers; with the stack
and heap filled with 0, 2,964 of 3,000. It is not part of the repository.

## The player's commands

`src/command.h` ports the player's turn in a battle (`3995:0573`, overlay
`3995`) and the commands it leads to: Move (`3995:0d9c`, `0cad`, `1117`),
aiming (`432f:3b80`, `38d2`, `39ad`, `3026`, `33ce`, `3219`, `2f12`),
Done (`3995:121c`, `14cf`, with Guard `6346:29b3`), Cast (`432f:28bd`),
Yell (`60f4:0023`) and the hidden keys, with Turn (`432f:12b7`, see The
computer's turns) and the combat side of View and Use (`546c:0d74`,
`17f9`, `24d7`, see View). The battle's turn (`3995:040b`) hands a
combatant the player controls (`+0x18b` `COMPUTER` 0) to it
(`cok_combat_commands`). Keys come through the menus on row 24 (`67b5:03e2`,
see Library); a click on the map in Move (`DS:71a6`) or Manual's cursor
(`DS:71a5`, at `DS:71a3`, `71a4`) is a step toward the cell (see The mouse
and the joystick).

A turn (`3995:0573`): one that cannot act ends it (`6346:2964`); one with
a spell begun at an earlier turn (combat record `+0x00`) casts it
(`5b04:1415`) and ends it. Otherwise, until the turn is over: the menu's
first item is selected (`DS:6e0f`) and the menu read (`3995:08d5`), its
items `Move ` while it has movement left (`+0x06`), `View Aim `, `Use `
while it has items (`+0x142` `ITEM_COUNT`), `Cast ` with a spell memorized
(`+0x1e`-`+0x57` `MEMORIZED`) when it may cast this round (`+0x01`) and the
area allows magic (var `0x4be5`), `Turn ` for a cleric (`+0xf9`
`CLASS_LEVEL` 0 above 0, or a former one, `+0x101` `FORMER_LEVEL` 0, that a
human may use, `66c2:0efb`) that has tried fewer times this battle (`+0x11`)
than undead were loaded (`DS:8859`), `Yell ` for a kender (`+0x5a` `RACE` 5)
while no one has yelled this battle (`DS:71a7`), and `Done`, then `*` while
the debug flag is set (`DS:4b51`, see Debug mode), cut to 40 characters. It
is read until it gives a command letter or Space, or one of the scan codes
of Alt-Q, Alt-R, Alt-A, Alt-X and Alt-M; other scan codes (the keypad's
digits among them) count as nothing. Row 24 is cleared. Then:

| Key | What it does |
| --- | --- |
| `M` | Move (below) |
| `V` | View (`546c:0d74`): Trade is not offered in combat |
| `U` | the Items menu (`546c:17f9`) with its second item selected (`DS:6e0f` 2, which the list's menu, `67b5:1368`, sets back to the first): `Use` is offered while it can act, the area allows magic and it may use items this round (`+0x02`) |
| `A` | Aim with its weapon's range (below) |
| `C` | Cast (below) |
| `T` | Turn (`432f:12b7`), and the turn ends |
| `Y` | Yell (below), and the turn ends |
| `D` | Done (below) |
| Space | every party member (`+0xe7` `NPC` below 0x80) back to the player (`+0x18b` `COMPUTER` 0), whatever its status |
| Alt-M | Magic On or Off (`DS:7198`, "Magic On" or "Magic Off" on row 24) |
| Alt-Q, Alt-A | the turn given back (`DS:72ce` less 1, initiative 0x14), every record in the list to the computer (`3995:1604`, forgetting a target on its own side), row 24 cleared, 200 ms: the battle runs the turn again, the computer's (0x14 becoming 0x13) |
| Alt-X | in a game started with `Helm`, the Gods' cheat (`432f:41e2`) and the combatant shown (`6beb:12ef`), ending the turn; else "That doesn't work" |
| `Q`, Alt-R | nothing (`Q` is in the keys taken but no item has it) |

After View and Use its attacks this round are worked out again
(`432f:0e80`) and, unless an item used ended the turn, the combat screen
redrawn (`6346:300f`). While the turn goes on the combatant is shown
(`6beb:12ef`) and its panel drawn (`6346:0af6`).

Move (`3995:0d9c`): until Enter or `D` (the single item, `Done`), Escape,
or the turn is over, "Move/Attack, Move Left = N" (half its movement) on
row 24 with ` Done`, the keypad's eight directions (the arrows, Home, End,
PgUp and PgDn, or 1-9 but 5): it turns that way (`6beb:0ad8`); what its
footprint would stand on there (`6beb:0c9d`): another combatant is
attacked (below); off the map, "Flee:" Yes tries to get away
(`432f:0dc7`), which ends the turn; else the cost, the costliest terrain
under the footprint (`DS:1ee4`) twice, three times on a diagonal, as a
byte, 0xff for a cell none can enter, more than its movement left says
"can't go there"; else the enemies it leaves attack it (`432f:0986`), and
unless it dropped it steps (`432f:077a`) while it has movement, the cloud
or puddle under it acts (`60f4:0dc3` with its flag, see Clouds, pools and
breath), and one that dropped or is helpless ends its turn. Escape first
undoes the moves (`3995:0cad`): the movement and facing come back, it is
erased and put back where it started (`6beb:10f3`), which, where another
now stands, leaves it off the map (size 0) and ends the turn. Leaving Move
does not end the turn; half a square left is lost. Moving into another
(`3995:1117`): with a missile weapon not thrown (`6346:3043`, `30bd`),
"Not with that weapon"; else unless it is an ally the player will not
attack (`432f:2f12`), it aims at it (`+0x0a`) and sweeps (`432f:0fce`),
which ends Move, or books and attacks it (`432f:19b6`, `432f:1a45`).

`432f:2f12`, attacking an ally: no question where the attacker is on the
side opposite the target's (`6346:2666` of the target) or the computer
controls the attacker; else "Attack Ally: ", and Yes sets var `0x7f33` and
`DS:714a` (which nothing reads) and turns every NPC that is okay (`+0xe7`
`NPC` from 0x80, but 0xb3, status 0) against the party (`+0x18a` `HOSTILE`
1), forgetting its target, and the sides are counted.

Aim (`432f:3b80`, `cok_combat_aim`): the range, the weapon's (type byte
12) less 1, 0 and 0xff becoming 1 (1 with no weapon). Its targets
(`432f:38d2`) are the combatants around it within 0x7f (`6b30:08d8`, any
direction) that it can attack (`432f:11d4`), nearest first: itself first.
A target is shown (`432f:3026`): attacking, "Range = N" on row 23 (the
distance with sight not blocked, halved); its panel; and "Next Prev Manual Center
Exit" with `Target` before Center when it is within range, the cell
valid, and, attacking, it is not the attacker itself and the attacker has
no missile weapon, or has ammunition (`6346:3111`) and either no enemy
beside it or a thrown weapon. Next and Prev step through the targets,
wrapping, the cursor flying from one to the next (`432f:39ad`,
`6346:1ba6` with the cursor's picture, icon slot 0x19); Center centres
the map there with the cursor; Target sets what is picked, ends the
aiming and attacks (`432f:3219`): unless an ally the player will not
attack (which clears what was picked), the cursor off, the screen
centred, a sweep, which ends the turn, or the target booked and attacked
with the ammunition, none for a thrown weapon a square away; Exit and
Escape leave, the turn going on. Manual (`432f:33ce`) moves a cursor from
the target's cell with the keypad, kept on the map: the cell is valid
when seen from the attacker within range (sight blocked, `6b30:03f1`),
not a wall, and its creature (a combatant, or a body's record) can be
attacked, or empty where empty cells may be (never for Aim); attacking,
not the attacker nor a body, and the missile rules above. `Target` (or
`Exit`) with "(Use Cursor keys)"; Space centres the map on the cursor.
(Its exits also clear `DS:742e`, the spell targets' "done" flag (5b04's
breath and attack handlers, `3f44:1c54`), not the mouse's, as this said
before.)
Target picks it (the menu's fourth item then selected), as Target does;
Exit or Escape go back to the targets with nothing picked but the cell
where the cursor started (Manual, the third item, selected).

Done (`3995:121c`): "Guard " unless a missile weapon not thrown, "Delay
Quit ", "Bandage " while a party member on the party's side is dying
(`6346:31e9`), and "Auto Speed Exit", until Exit or Escape or the turn is
over. Guard (`6346:29b3`): the turn ends and, unless made to flee
(`+0x10`), it guards, "Guarding". Delay: initiative 1, and it moves to the
end of the round's order, those after it moving up, as far as entry 72;
the battle goes on with the next (`DS:72ce` less 1). Quit: the turn ends.
Bandage: the first dying party member is bandaged (unconscious, no
rounds dying, "is bandaged"), and the turn ends. Speed (`3995:14cf`):
"GameSpeed (N) :" with " Slower " below 9 and "Faster " above 0, and
Exit, until Exit or Escape: Slower and Faster change the game speed
(`DS:4b38`, `game->speed`). Auto: the computer takes it (`3995:1604`),
row 24 is cleared, 200 ms pass and the computer plays the rest of its
turn (`3afb:004b`). The keyboard's flush there (`1614:045c`) goes
through the `flush` hook.

Cast (`432f:28bd`, `cok_combat_cast`), for the selected character: a
spell picked from its spells in memory (`546c:34ec`), or none; one whose
spell table byte 11 is 0 is refused, "Camp Only Spell". Unless the
caller already did (the computer's casts), the combat screen is redrawn
and the caster shown with its panel. A spell whose casting time (byte 12)
in thirds is 0 is cast (`5b04:1415`) and the turn ends. A longer one
"Begins Casting" (row 10, with a pause): the spell is kept (combat record
`+0x00`) for the caster's next turn, its initiative falls by the time, at
least to 1, and it moves down the round's order past those of more
initiative and, a d2 of 1 each, those of as much, the turn over; the
battle goes on with the next.

Yell (`60f4:0023`): the kender is the one who yelled this battle
(`DS:71a7`); every record that is okay, of intelligence (`+0x13`, `SCORE` 1)
above 3, on another side and not helpless, that fails a saving throw of type
4 (`60f4:113a`), gets effect 0x5b (no time, value 0xff), "wants to kill
NAME" flashed on it (`60f4:20f7`), which steers the computer's targets
(see The computer's turns).

Use in combat (`546c:24d7`): after the item's spell is known (a scroll's
picked from its list) the combat screen is redrawn unless the computer
has the character; an item other than a scroll "uses an item" (row 10)
with "Item:" and its name on row 23, a pause and the text cleared; a
scroll a thief may fail to read ("oops!", said from the row after the
text's last, `DS:6135`). The spell is cast (`5b04:1415`), and in combat a
spell its table's byte 11 allows ends the turn (`6346:2964`); the turn's
end uses up the scroll's spell or a charge, as outside combat.

The cast itself, from Cast, a spell begun earlier or an item, is in
Spells in combat. A camp-only spell from an item (byte 11 0, which Use
does not test) is cast, sets what Use and the turn's menus share when it
finds a target, so the menus end, and uses its charge, though the turn
goes on.

| Function | Original |
| --- | --- |
| `cok_combat_commands`, `cok_combat_command_text` | `3995:0573`, `08d5` |
| `cok_combat_move`, `cok_combat_charge` | `3995:0d9c` with `0cad`, `1117` |
| `cok_combat_ally` | `432f:2f12` |
| `cok_combat_aim` | `432f:3b80` with `38d2`, `39ad`, `3026`, `33ce`, `3219` |
| `cok_combat_done`, `cok_combat_speed`, `cok_combat_guard` | `3995:121c`, `14cf`, `6346:29b3` |
| `cok_combat_cast` | `432f:28bd` |
| `cok_combat_yell` | `60f4:0023` |

`eclplay --keys` with no `--combat` has the player's turns read the keys;
`\@X` types Alt and a letter and `\#HH` the extended key of scan code
`HH` (Alt-Q is `\@q` or `\#10`). Each menu read logs its items (`menu:`).
When the keys run out during a battle the run ends there, as anywhere
else: "(out of keys in block N at ADDR)", the end of combat and its
treasure not reached (the battle's own end, `3995:004b`, runs). Without
`--keys` the player's turns pass. `--combat gods` and `auto` press Alt-X
and Alt-Q for the player at the first read of each player's turn; the
menu is shown and logged first.

The port keeps these quirks:

- Escape in Move undoes the steps but not what they caused: attacks of
  opportunity, damage, the squares' effects and the sounds stay. Put back
  where another now stands, the combatant is off the map and its turn
  ends.
- Move's cost is the costliest terrain under the footprint, the step's
  (`432f:077a`) that of its first cell; half a square left after Move is
  lost, and with it the `Move` item.
- `432f:2f12` tests the attacker's computer control, not the target's:
  a player's character attacking a party member the computer controls
  is still asked; one on Auto never is.
- Delay when last in the order gives the turn straight back, running the
  turn's start (events 7, 0x0f and 0x15) again; so does Alt-Q. In
  DOSBox-X (`d_altq.py`) an immolating character's 0x0f burns twice so,
  as in the port; event 7's effects end the turn before its menu, so
  Alt-Q never runs them twice.
- Bandage reaches the first dying party member anywhere on the map, and
  its result is overwritten by the turn's end.
- The Done menu's and Speed's special keys count as their scan codes'
  letters: F8 (0x42) is `B`, which ends the turn even with nothing to
  bandage; Home (0x47) `G`; PgDn (0x51) `Q`; F7 (0x41) `A`, Auto; F10
  (0x44) `D`, Delay; Del (0x53) `S`; in Speed, Del slows and Scroll Lock
  (0x46) speeds up past the bounds, the speed a byte. In Move, F10 leaves
  as `D`; in Aim, a special key is not acted on, but NumLock (0x45) and
  shift-F1 (0x54) leave it, and keypad + (0x4e), Down (0x50, `P`) and
  shift-F1 take the cell of the target shown again.
- After Manual's Exit there is no target, and the cell may still be valid:
  `Target` is offered (and attacking reads through NULL, below); Down
  then takes the cell of no target: `6beb:0c43` finds NULL in the entry
  after the last combatant, whose cell is where placement last tried a
  monster it removed, or what an earlier battle left there (0, 0 at
  first). The port keeps those cells, and which entries of `DS:68d1`
  hold a record, as the original does: placement writes an entry at each
  try (`3cb2:1a33`) and clears it for a monster it removes (`1bdb`), and
  nothing clears them between battles. Where an earlier battle's record
  is still in that entry the original finds none and reads entry 0's
  cell (`DS:623f`, `6240`), other data: the port stops. A cell off the
  map there makes the sight read past the map: the port stops too. In
  DOSBox-X (`e_manual.py`, the earlier battle's record written into the
  entry) entry 0's bytes were 0x00, 0xe7, a y of -25: Center centred the
  map as for 0, 0 with the cursor there, `RANGE = 52` (the distance to 0,
  -25) on row 23, the sight reading terrain 1,243 bytes before the map,
  and the battle went on; those bytes change from run to run (a y of 0-24
  would centre elsewhere and could offer `Target`).
- Aiming lists the attacker itself first; it is never a target while
  attacking.
- Turn's level is not clamped (see The computer's turns); a "Turn" that
  turns nothing still counts a try.
- Space gives back to the player party members of any status; the
  computer's own key poll (`3afb:1200`) skips status 1.
- `Q` and Alt-R are taken by the menu but do nothing.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`:
attacking with no target after Manual's Exit (`432f:2f12` reads
`0000:018a`); a Cast that would move the caster past entry 72 of the
order (no bound: `DS:72ce` read as a record); aiming with no target
listed (a combatant off the map: `432f:39ad` reads its frame's entry
never written); the cell of no target where an earlier battle's record
is left in the entry after the last combatant (entry 0's cell, other
data), or off the map (sight reads past it); a cell past the terrain
table; "oops!" said from a row past 20 (the row after the
text's last, `DS:6135`), which the original prints over the frame (see
The combat screen); an item type past `ITEMS`; a record with no combat
record. Molly's hoopak power (effect
0x07) with no weapon readied still stops (see Attacks, movement and
death): the player's turns reach it when she unreadies the hoopak and
attacks.

What the original does with no target after Manual's Exit, in DOSBox-X
(`e_manual.py`): `0000:018a` and the NULL record's `+0x18b` are both 0
there (INT 62h's vector unused), so the target counts as an ally not
under the computer's control and `Attack Ally:` is asked; No gives the
turn back, nothing written; Yes attacks the record at `0000:0000`, writing
over the interrupt table where its fields lie (INT 2-4, INT 33h's segment,
the mouse driver's, INT 50h-5Fh, 63h-66h), ends the turn, leaving two
red `@` at column 39 of rows 12 and 14 of the next menu's screen, and the
battle goes on (with KRYNN.CFG's mouse the next INT 33h call would jump
astray).

A differential test ran the original's whole battle, `3995:0172` with
the player's commands and everything they call as they are (`08d5`,
`67b5:03e2`, `177f`, `1368`, View, Items, the spell lists), in an 8086
emulator against the port's, with the same seed and the same keys: the
battles of the computer's turns' test (random parties of `SAVE/`'s and
`--test-party`'s characters, the scripts' monster groups, 3D maps and open
ground), the player's turns reading a random key script through Crt's
`KeyPressed` and `ReadKey` (`1614:03c2`, `025b`) wherever they are called
but the computer's poll, the script ending with Escapes, No and Alt-Q so
that the computer finishes the battle; `1521:09ba` (the time, for the
menus' timeout and animation, neither used) 0; `5b04:1415` from the
player's side logging what the port logs. Every record, item, effect and
combat record, the tables, sides, rounds, rolls, pool, seed, the menu item
selected, Magic On, the speed, var `0x7f33`, the one who yelled, the
round's order and its index, the keys read and the transcript (attacks,
sounds, turns, rounds, unported lines) were compared at the end, and,
for a sample, the screen at every key read and every pause, the
original's drawing replayed as P6's harness does. Of 10,000 battles,
9,324 agreed and none differed; in 555 the port skipped an effect handler
not ported (breath and the like, as in The computer's turns), 92 stopped
at Target with no target after Manual's Exit, 25 at Ready given no item
(the harness gave every member a readied wand while Molly kept her
readied hoopak, two readied weapons that Ready never makes: with the
wand unreadied, all 25 agree) and 4 at
sight read off the map from the cell of no target. In the agreeing
battles the player read 214,367 menus over 67,090 turns: 65,826 Moves
with 99,009 steps, 102,178 refused for their cost, 99,953 checks of
attacks of opportunity, 20,767 undone with Escape, 17,420 moves into
another (7,245 attacks, 1,839 "Not with that weapon", 20 sweeps), 208
"Flee:" asked and 91 flights tried; 38,183 Aims with 34,407 Next and 8,247
Prev, 24,980 cursors (874 picks), 18,249 Centers, 3,450 Targets, 4,324
strikes (1,687 attacks, 258 with ammunition, 3 sweeps), 17,019 "Attack
Ally:" asked and 6,069 answered Yes; 39,195 Done menus with 6,885
guards, 10,958 delays (42,297 entries moved up), 7,673 quits, 314
bandages, 4,018 Speed menus and 6,385 Autos; 10,393 Casts (1,737 camp
only, 2,482 at once, 2,846 begun with 1,806 d2s in the order, 2,645 cast
at the next turn); 12,878 Views, 23,127 Item menus with 7,571 items used
(827 scrolls misread, "oops!"); 2,060 Turns, turning 1,336 and destroying
854; 931 Yells; 6,876 Spaces, 4,930 Alt-M, 6,802 Alt-Q and 1,586 Alt-X.
With the screens compared at every key read and pause, 932 of 1,000
agreed (two stopped at "oops!" said below the side panel); filled with
0x5a, 1,858 of 2,000, and with 0, 941 of 1,000, none differing. A first
version that took the cell of no target as 0, 0 showed in the screens
(two of 1,000: "Range = 18" where the port said 34). After the review
(the entries of `DS:68d1` kept, a spell past the handler table refused
before the in-battle log), 3,000 fresh battles: 2,687 agreed and none
differed, 124 stopped at a harness wand's spell 0x72 (past the handler
table), 37 at Target with no target, 1 at sight off the map, the rest
skipped handlers; with the screens, 270 of 300 agreed, none differing.
It is not part of the repository.

### Death Knights' commands

The descriptor's `commands.routine` picks the player's turn
(`COK_COMMANDS_DEATH_KNIGHTS`, `dkk 3e05:08af`, `dk_commands` in
`command.c`); `cok_combat_move` and `cok_combat_aim` go to Death Knights'
Move and aiming the same way, so that casting's aim (part of step 3's
casting) will reach them too. Each was compared with Champions' instruction
by instruction; what is the same is shared (`move_step`, the charge, the
undo, `strike`, Delay, Bandage, Yell, Guard, Cast, Champions' Auto).

The turn (`3e05:08af`): one that cannot act ends it, a spell begun earlier
goes to the cast entry, as before; otherwise, until the turn is over, the
selection set to the first (`DS:c0f7`) and the menu read (`3e05:0ccc`) with
the menu loop by index, the text `Move Aim Use Cast Turn Guard Quick@Delay
Bandage Yell View Speed End` (`DS:3453`; Quick ends the first row): Move
while it has movement left (combat record `+0x06`), Aim, Use while it has
items (`+0x7c`), Cast with a spells' block holding a memorized spell (a
byte of 0-0x49 whose low seven bits are not 0) when it may cast this round
(`+0x01`) and the area allows magic (var `0x4be5`), Turn for a cleric
(`+0x45` above 0) or a paladin above level 2 (`+0x48`, the fight section's
`paladin_turns`) that has tried fewer times than undead were loaded
(`DS:db7d`), Guard unless it has a missile weapon not thrown, Quick and
Delay, Bandage while a party member is dying (`65cd:3326`), Yell for a
kender (`+0x20` 5) while no one has yelled (`DS:c4ce`), and View, Speed and
End. It is read until it gives an index, or one of the special keys of the
set at `3e05:0cac` (Alt-Q, Alt-R, Alt-A, 0x20: the keypad's 5 or Alt-D,
Alt-X, Alt-M); rows 23 and 24 are cleared. Then: Move (below); Aim
(`5340:3888` with the weapon's range); Use (the items menu with its second
item selected) and View, each followed by the attacks recomputed
(`5340:0d69`) and, unless an item ended the turn, the screen redrawn
(`65cd:3158`); Cast (`5340:258e` = Champions' `432f:28bd`); Turn (its
condition tested again, `5340:119b`), Guard, Bandage, Yell and End end the
turn; Quick is Champions' Auto (the computer takes it and plays the rest of
the turn); Delay is Champions'; Speed (`3e05:14aa`: `game speed: (N) ` with
`Faster` above 0, `Slower` below 9 and `Exit`, by index, until Exit:
Escape does not leave it). The special keys: 0x20 gives every party member
who is no NPC (`+0x3e` below 0x80) back to the player; Alt-M Magic On or
Off; Alt-Q and Alt-A the turn back and the party on Auto, as Champions';
Alt-X the Helm's cheat, with no word without it; Alt-R nothing. View and
Use are Death Knights' (`580d:0c66`, `0ffa`; see Death Knights' View).

Move (`3e05:10bb`): `move/attack, move left = N` (half its movement) with
`Done` by index, read again after each step; the keypad's directions are
special keys, a step each, as Champions'; Escape (special) undoes the
moves, Done (or Enter) leaves. The step's words: `flee ? ` (the Yes/No by
index), `cant't go there` (sic, `DS:1718`), `not with that weapon`.

Aiming (`5340:3888`): the cursor's flight's icon slot 0x1c (`65cd:1bd5`);
the range the weapon's type byte 12 whole (Champions' less 1); the
attacker's cell redrawn first (`6f03:04e8`); the targets (`36f2`) all those
within 0xff it can attack, nearest first, the one it last aimed at
(combat record `+0x0a`) shown first, and when that is not itself `Target`
selected (`DS:c0f7` 4); the flight to the next or previous (`34b3`) kept
to a listed entry with a record on the map (else the one shown stays), and
with none listed the original quits to DOS (`16ab:0000`): the port stops.
Each target is shown (`2bee`): `range = N  ` on row 23 when attacking, its
panel, and `aim: ` with `Next Prev Manual Target Center Exit` by index,
`Target` by Champions' rule. Escape (special) leaves, the target's cell
redrawn and the screen centred on the attacker; after Next, Prev and
Target the cell is the target's only for one (the set at `5340:3868`, by
whatever key gave 0, 1 or 3). The cursor (`2f3d`): `(use cursor keys) `
with `Target` where the cell may be picked, else `Exit`, by index; a
creature under it may be picked where the attacker can attack it counting
allies (`5340:10ab` with 1, `cok_combat_can_attack_allies`); Escape is
Exit; the keypad's 5 (0x20) centres. Attacking an ally (`5340:2b81`):
`attack ally? ` (the Yes/No by index), Yes setting var `0x7f33` and
`DS:c470` only, no NPC turned against the party
(`commands.ally_turns_npcs`).

The battlefield's cursor box is `COMSPR` 15 in slot 0x1c
(`commands.cursor_slot`; `6f03:036c`), and Death Knights recolours its
missiles' icons as a `CPIC`'s (`startup.missiles_recoloured`, `7090:016f`):
the box's dark gray edge is black.

The adventure's menu (`4307:0257`) reads with the loop by index too
(`command_read` in `adventure.c`): it keeps its own selection
(`menus.adventure_keeps_selection`: put back before each ask of its
main menus, whatever Cast, View and Encamp left, their menus starting from
the first as Champions' are set to; Move's `Exit` neither puts it back nor
takes it), Escape leaves Move (`menus.escape_leaves_move`; on the overland
map it also ends the command, a step as any special key),
and Left and
Right, which the loop gives back, are special keys that keep the character
selected (`580d:2bb2`, `menus.special_keeps`; Champions' `546c:3334` picks
the first for any key but Up and Down).

`tests/test_menu.c` checks the loop (`test_indexed`), `tests/test_death_knights.c`
the sections against the original (`test_commands`: the loop's sets and
colours, the words where their routines load them, the hidden keys' set,
the cursor's slot, aiming's range and list), `tests/test_fight.c` aiming's
range and first target, the keypad's 5 and attacking an ally on records
made by hand, `tests/test_death_knights.py` the first fight by keys (with
the cursor's box) and the adventure's menu, and
`tools/dosbox/checks/dkk_fight.py`'s `keys` and `keys2` flows the original:
the first fight from its first turn by keys (the menu's rows, Move with a
step and its undo, Aim with Next, Prev, the cursor and Center, Speed,
Delay, Guard, End, Quick; Yell, Cast's spells left, a round's turns ended,
an attack by Aim and Target, View), its screens at each wait and its state
after.

## Spells in combat

`src/cast.h` ports casting in a battle: the combat part of the cast
(`5b04:1415`, with `5b04:57eb`), the targets in combat (`432f:2337`, which
the battle puts in `DS:6e3a`, with `432f:210b`, `432f:1eed` and
`5b04:0ecb`), the apply routine's touch (`5b04:1071` with `60f4:1062`),
the handlers of the spells cast only in combat (table `DS:6e3a`, filled
by `5b04:58ed`) with their helpers `5b04:1cbd` (missiles), `1e34` (sides),
`39bb` and `3a6f` (bolts) and `1903` with `17aa`, `17eb` and `1836`
(cones), and effect 0x47 turning a spell back (`3f44:23a4`). Cast
(`432f:28bd`, see The player's commands), a spell begun at an earlier
turn (`3995:0573`, `3afb:004b`) and Use (`546c:24d7`) all cast through
`cok_cast_spell`; the computer's casts (`3afb:0613`) go through Cast with
its flag, and its items (`3afb:04b2`) through Use (`cok_item_use`). Each
cast in a battle is logged, `cast: NAME: SPELL`.

The cast in combat (`5b04:1415`, the mode 5): the refusal of spells of
byte 7 0 is skipped; a caster with 0x4a miscasts on a d2 of 1; then, or
when announced and from memory, `5b04:57eb` says in the side panel that
the caster "Casts a Spell" (row 10, with a pause), whatever the verb, so
"miscasts" never shows in combat, and puts "Spell:" and the spell's name
on row 23 (light green). Until it is over: the targets (`432f:2337`,
below, with the computer's flag of the caller: Cast's, `3afb:004b`'s 1,
`3995:0573`'s 0, Use's the user's `+0x18b`); with any, the flight: the
spell's missile (`6346:1b5b`, icon slot 0x12), the caster turned toward
the spell's cell (`DS:710f`, `7110`; the first direction from north whose
quarter, `6b30:054a`, holds it) in its casting pose (`6beb:0ad8`,
`DS:71ad` set), sound 0x0b for Fireball, 8 for Lightning Bolt, else 2,
the missile flown (`6346:1ba6`, four pictures, 30 ms a step), and the
caster, if shown (`6beb:06ef`), drawn back in its ready pose; then as
outside combat its invisibility ends, the spell is forgotten unless an
item is used, and the handler runs with the spell in `DS:6b33`. With no
target the player is asked "Abort Spell? " (yellow): No aims again; Yes,
and the computer at once, "Spell Aborted" (row 24), the spell forgotten
unless an item is used. Then the panel's text and row 23 are cleared.

The targets in combat (`432f:2337`) clear the list (`DS:6fea`), the area
flag (`DS:6b39`) and set the spell's cell to the caster's, then by the
low nibble k of the spell table's byte 6:

| k | Spells | Targets |
| --- | --- | --- |
| 0 | Detect Magic, Shield, Mirror Image, Prayer, Snake Charm, Animate Dead, Fire Shield and the like | the caster |
| 5 | Faerie Fire (0x4f), Charm Monsters (0x51) | picks while the sum of their weights is within an allowance (the caster level for Faerie Fire, else 2d4): by hit dice (`+0xd6`: 0 or 1 count 1, 2 2, 3 4, others 8) or for Faerie Fire by size (`+0xcf`: 1, 2 or 4); a pick already taken costs the computer a point of its allowance and gives the player "Already been targeted"; picks stop past the allowance with two or more, on none, or at an allowance of 0 |
| 15 | Silence (0x19, 0x66) | one pick: the caster's target (`+0x0a`), or with none (the player's Target after Manual's Exit) those at the cell, radius (k >> 4), 0 |
| 8-14 | Bless, Sleep, Fireball, Lightning Bolt, Haste, Slow, Dispel Magic, Confusion, Fear, Dimension Door, Ice Storm, Cone of Cold and others | a cell (an empty one allowed), then everyone around it within byte 6 & 7 (`6b30:08d8`, any direction, a footprint of 1), either side, the caster too, nearest first; the area flag set |
| others | single targets: 1 pick for byte 6 & 3 = 0, up to 4 for Hold Person (0x31), Hold Monsters and 0x3f | (byte 6 & 3) + 1 picks: one already taken is refused ("Already been targeted", the computer losing the pick), the caster too for Hold Person (0x17, 0x31: "Cannot target yourself"); the spell's cell becomes the last pick's |

Each pick (`432f:210b`): the player aims (`432f:3b80`, see The player's
commands: not attacking, an empty cell allowed for an area, a creature
unless Dimension Door) within the spell's range (`5b04:0ecb`: byte 2 plus
byte 3 times the caster level, 6 while an item is used, as a byte; 0 with
byte 6 set, and 0xff, a touch, become 1), and the pick, or none, becomes
the caster's target (`+0x0a`); the computer takes itself for a spell not
aimed at enemies (byte 14 0), or for Cure Light Wounds the ally
`432f:1eed` finds; else `432f:3f9f` picks an enemy within the range
afresh (a d(enemies) a try), which is refused if it is helpless
(`6346:0cdb`) and the spell's effect (byte 10) is one that makes it so
(`DS:200c`: 0x33-0x35, 0x1f). A pick sets the spell's cell. With no
target at all *done is cleared, but never for k 5 (its handler then runs
with none), nor for an area whose cell holds no one. `432f:1eed`: of the
eight cells around the caster and its own (`DS:1ed6`, `1edf`), the
combatant on its side that is hurt with the fewest hit points, or the
caster itself below half its most; but where none has fewer than 8, the
last body found on a body's cell (0x1f, `DS:69ee`) whose record is not
dead, stoned or gone (statuses 6-8). The computer's test of Cure Light
Wounds (`3afb:03b9`) uses it too.

The apply routine (`5b04:1071`) in combat: for a spell cast by touch
(byte 2 of `DS:6b33` 0xff), after its saving throw, the target's stats
(`6346:0d20`), its event 0x0b, and the caster's attack roll against its
armour class (`60f4:1062`, see Attacks, movement and death); a miss
counts as saved and leaves no damage, for that target and those after it.
Each step reads the target from the list again. Damage in combat is
`60f4:1db7`'s (see Attacks, movement and death): the flash, a hurt
caster's spell lost.

| Spell | Handler | In combat |
| --- | --- | --- |
| 2 Curse | `1f2c` | those against the caster (`1e34`): effect 2, "is Cursed" |
| 1, 0x69 Bless | `1efb` | those on the caster's side, in combat but one with an enemy beside it (`6346:26e2` within 1) |
| 4 Cause Light Wounds | `1fc2` | by touch, 1d8 of magic (`60f4:1261`) |
| 0x0a, 0x6a Charm Person | `20f8` | the first target, unless a monster of some kind (`+0x13f` above 1) or larger than a man (`+0xcf`): "is unaffected"; else 0x0b valued at the caster level and the caster's side in bit 7, run when removed, "is charmed", and 0x0b's handler takes hold |
| 0x0f Magic Missile | `23f3` | (level + 1) / 2 missiles of 1d4 + 1, magic |
| 0x14 Shocking Grasp | `2484` | by touch, 1d8 + the level, electricity and magic |
| 0x15 Sleep | `24d8` | 4d4 hit dice, each target in turn by its own (`+0xd6`, signed: up to 1 counts 1, 2 2, 3 4, 4 6, 5 10 for an elf, 20 for others and above): those unconscious, asleep (0x35) or past what is left are taken off; the rest "falls asleep", no save |
| 0x17, 0x31, 0x5e Hold | `2616` | through `1cbd` with a save 2 easier for one (3 for 0x31, 0x5e), 1 for two, none for three or four: "is held" |
| 0x19, 0x66 Silence | `26b2` | 0x15, "is silenced" |
| 0x1b Snake Charm | `279a` | every snake in the list (`+0x13f` & 0x10), either side, that can act and is not charmed, while their hit points are within the caster's: 0x33, "is charmed" |
| 0x1f Knock | `2900` | nothing (no effect to add) |
| 0x21, 0x26, 0x28, 0x2c, 0x2d, 0x3d, 0x64 | `2999`, `3053`, `3124`, `3705`, `373e`, `3f2c` | "is weakened", "is blind", "is diseased" (run when removed), "has been cursed!" (0x64 adds none), "is blinking", "is paralyzed" |
| 0x2f Fireball, 0x40 | `376c` | the caster level's d6 (0x40: 2 × 1d3 + 1), fire and magic; outside a 3D area (var 0x4be6 0) those within 2 of the cell, not 3; the screen centred there |
| 0x33 Lightning Bolt | `3d04` | the level's d6 (`60f4:1216`), electricity and magic, on the cell, then a bolt of 7 |
| 0x3c | `3ed8` | 1d6 + 20 of electricity on the cell, then a bolt of 3 doing 20 |
| 0x37 Slow | `3d5e` | those against the caster, as many as its level: Haste (0x27) cured, else "is Slowed" (0x2a) |
| 0x41 | `3feb` | 2d4 + 2 of magic |
| 0x42 Cause Serious Wounds | `4026` | by touch, 2d8 + 1, + 3 for a caster of deity 4 |
| 0x44 Poison | `418e` | the spell applied (a save that changes nothing); then the caster's target runs event 9 for 0x40, and unless that cancels it, 0x40's handler poisons it (`3f44:1b55`) |
| 0x46 Sticks to Snakes | `4214` | the first target, of fewer than 6 hit dice: effect 3 at the caster level and its handler, "is fighting with snakes"; else "smashes them flat" |
| 0x51 Charm Monsters | `42f3` | through `1cbd`, "is charmed", then 0x0b's handler for each |
| 0x52 Confusion | `4382` | at most 2d8 of the targets, each throwing type 4: 0x23 for the spell's duration, a save of kind 1 |
| 0x53 Dimension Door | `4446` | the caster erased and put at the cell (`6beb:10f3`), off the map where another stands, the screen centred on it, "teleports" |
| 0x54 Fear | `44ed` | the cone (3, 6) toward the cell, either side: each throws type 4; failing, 0x6f (run when removed), "runs in terror", and whatever became of the effect it flees (`+0x10`), the computer controls it, a player's character is turned (`+0xe7` 0xb3) and it forgets its target; else "is unaffected" |
| 0x56 Fumble | `4801` | the first target throws type 4 (the damage type 0x40): failing, 0x1b "is clumsy" and its handler, the turn lost; else 0x2a "is slowed"; then the spell applied again: a second save and 0x1b once more |
| 0x57 Ice Storm | `4914` | 3d10 of cold |
| 0x48, 0x49, 0x4a, 0x4c, 0x4e, 0x4f, 0x5b, 0x5c, 0x5d, 0x62 | `42b1`-`42de`, `4984`-`4998`, `4a2c` | nothing: Cause Critical Wounds, Dispel Evil, Flame Strike, Slay Living, Entangle, Faerie Fire, Cloud Kill, Cone of Cold, Feeblemind and 0x62 are used up |
| 0x22 Stinking Cloud | `29de` | see Clouds, pools and breath |

The handlers ported outside combat run in combat too, their texts in the
side panel (`6346:1883`: "is Cured", "is Unaffected", healing, "is
stronger") or flashed (`6346:228c`: "can see", Dispel Magic's "is
affected", "is un-cursed", "is Healed"). `5b04:1cbd`: from the last
target to the first, a missile to each but the last picked (sound 2), a
saving throw of the spell's type (byte 9) with the bonus given, an elf
(`+0x5a` 7) always saving against Hold Person, Faerie Fire and Charm
Monsters counting the last picked as saved against a save of kind 1;
then the spell's effect for its duration, valued at the caster level.

A bolt (`5b04:3a6f`, after `39bb` on the cell): from the spell's cell on,
away from the caster, its length in cells (2 a step, 3 a diagonal one),
in segments that end at another combatant, off the map, at a cell costing
more than 1, at the line's end or the length; each flies the bolt (icon
slot 0x13, 50 ms a step) and hits its last cell (`39bb`): a combatant
other than the one the last segment hit throws (type 4, 3 for breath)
and takes the damage, half if saved. A wall in a 3D area (var 0x4be6 1)
reached with its flag clear bounces: the wall becomes the spell's cell,
the bolt turns back along the line from it to the caster (alternately
toward and away from it), anyone may be hit again, the caster too (in
DOSBox-X a Lightning Bolt at the warrior in Throtl's gateway hit it, came
back off the wall to hit it again and two hobgoblins, and ran on to its
caster, as in the port: `d_bolt.py`), and for Lightning
Bolt's first bounce the 8 the line back costs where it is 8 or shorter is
charged. A cone (`5b04:1903`): the aim pushed on, repeating the line's own
steps, until the length walked reaches the cone's length or the aim
reaches the map's edge, kept on the map and pulled back to the first cell
that blocks sight (`6b30:03f1`); the combatants on the line to it,
nearest first, each once, then for a width of 2 or more those on a line
to the end's neighbour on the right, by the direction of the last step
that met one, and for 3 the left (`DS:0d94`-`0dac`); for a width of 0xff
those within 3 of the end instead; the caster left out.

Effect 0x47 (`3f44:23a4`, on events 6 and 9) turns a spell back on a d10
of 1 for one whose targets (byte 6 of `DS:6b33`, spell 0's, all 0, when
none is cast) are below 8, or always above 14: the holder "reflects the
spell" (no pause), the selected character's cell becomes the spell's, it
takes the holder's place in the targets, the spell's missile (one
picture, 30 ms a step) flies from the holder to the first target, and the
damage or effect goes to it: `60f4:057c`'s record is the caller's
(`cok_effects_dispatch_at`), so the event's later handlers, and the
damage (`60f4:1db7`) or the effect (`60f4:20f7`), take the new one.

The port keeps these quirks:

- "miscasts" never shows in combat: 57eb says "Casts a Spell".
- Silence's area radius is always 0 ((byte 6 & 15) >> 4).
- Charm Monsters' and Faerie Fire's last pick counts as saved against a
  save of kind 1 (`1cbd`), so a lone target is always "Unaffected"; their
  picks never clear *done, and their handler runs with none.
- An area keeps *done with no one in it: the spell is used up on no one.
- The computer's second pick of one already taken costs it a pick; the
  player is told and picks again.
- Ten combat spells do nothing; Bestow Curse 0x64 adds no effect; Knock
  adds none.
- Poison ignores its own saving throw; Fumble throws twice and can add
  0x1b twice; 0x3c's bolt does a flat 20 past the cell.
- Fireball's area is recomputed with radius 2 outside 3D areas.
- Sleep weighs the caster too when aimed at its own cell, and can put the
  caster's allies to sleep; Fear frightens allies in the cone.
- The computer's Dimension Door aims at an enemy's cell, where the caster
  cannot stand: it leaves the map.
- A bolt's length on a line after a bounce counts the length of the line
  before it, as a byte: a first segment shorter than that ends the bolt.
- A cone's "already listed" flag is never cleared on its main line: after
  one met a second time (a large monster's second cell) no one further on
  is added.
- Bless in combat leaves out an ally with an enemy beside it.
- The computer leaves its spell begun in its combat record while it casts
  it (`3afb:004b`, unlike `3995:0573`): hurt by its own Fireball it "lost a
  spell", forgets another of it and ends its turn.
- A camp-only spell from an item is cast in combat (Use does not test
  byte 11) and does not end the turn.
- Effect 0x47 turns damage dealt outside a spell too (event 6 of
  `60f4:1db7` from an explosion or an effect, with no spell: spell 0's
  byte 6 is 0), flying the missile toward the last spell's first target.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`:
a handler reading its first target (`DS:6feb`) through NULL, as Target
after Manual's Exit makes it (the original reads the interrupt table at
`0000:0000`), or weighing such a pick (Charm Monsters); Hold's save bonus
for more than four targets (a local never set; the targeting never gives
more; with none it is not read); a cone aimed at the cell of a caster
larger than one cell, or cast by a caster not shown after its flight (its
range and side then come from the missile's stack); a cone's list past 30
(into its directions); a bolt that ends its line short of its length on a
cell it does not bounce from (the original passes there for ever), or
that starts a line after a bounce from a state it started one from
before, its own and the seed, hit points and conditions all the same
(between two walls as far apart as its line before was long it is
charged nothing and bounces for ever: in DOSBox-X, on a combat map
rewritten in memory as no battle searched with `eclplay` met one, the
bolt flies between the two walls for good, no key polled and no random
number drawn, `e_bolt_bounce.py`), or after 4,096 bounces; 0x47
turning a spell toward a first target whose record was freed with the
party member it was (removed in camp or by treasure, marked when it is);
terrain past the table.

Bytes `1903` reads from the stack, never set there, are those of the
calls before it. The sight's range is the return address the caster's
terrain blit (`127f:2678` in Tandy mode, 0x2705; 0x21d0 CGA, 0x2458 EGA)
left: no limit in any mode. The side line's direction where no one is on
the main line is the low byte of the data segment that blit pushed, which
depends on where DOS loaded the game (PSP + 0x10 + 0x0bc6): 0xc6 for a
PSP of 0xnnf0, as in the emulator (load segment 0x1000), the port's
assumption. Its entries of the neighbour tables are then the screen
frame's bytes: the right line ends 4 cells east of the end, the left at
the end. A probe computing the cone for all 256 values of the byte found
the same targets in each of 88 Fear casts with no one on the line. Aimed
at the caster's own cell (Fear with `T` at once: the aim starts there),
the step directions are the locals that drawing the caster's icon at the
cast's end left (`6beb:0ad8`, `6d21:04b0`, Tandy's), the screen offsets
of its cell on the view: with sx, sy its place on the view, the bytes 0,
12 (6 - sx), 0, 12, 0, 32, 1, then the word 2016 (sy + 1), 24, 0, 24 sy,
0, 12 sx, 0, 0, then zeros; a probe of 78 casts, every view cell it
reached, agrees. In DOSBox-X (`d_fear.py`, Throtl's gate, the caster at
26, 13) Fear with `T` at once struck what the port's does, in Tandy, EGA
and CGA mode alike, with the game loaded at a PSP of 0x08f0 (DS 0x14c6);
loaded where DOSBox-X puts it with no pad, PSP 0x087b (DS 0x1451, low
byte 0x51), the cone went elsewhere in all three modes (a hobgoblin ran,
no party member was touched): this cone too depends on where DOS loads
the game. The spell's cell after no pick is entry 0's
(`DS:6fe7` is the handler table's last bytes and the count), which
nothing reads before the next cast sets it.

Stinking Cloud's clouds, Dispel Magic's part for them (`5b04:3352`) and
the breath of monsters, which picks its targets through `432f:2337` and
uses the cones and bolts, are ported with the clouds (see Clouds, pools
and breath). The effects spells add, 0x22 of Cause Disease and the
timers' 0x0f and 0x16 of Slow Poison from an item among them, run their
handlers when they end (see Spell effects), and Sir Lebaum's 0x4a casts
Dispel Magic with no cast around it, on what the last cast left.

| Function | Original |
| --- | --- |
| `cok_cast_spell` | `5b04:1415`, `57eb`, `1071`, `432f:2337`, `210b` |
| `cok_cast_range` | `5b04:0ecb` |
| `cok_cast_wounded_ally` | `432f:1eed` |
| `cok_cast_reflect` | `3f44:23a4`'s turning |
| `cok_effects_dispatch_at` | `60f4:057c` with the caller's record |
| `cok_item_use` | `546c:24d7` |

A differential test ran the original's whole battle (`3995:0172`) in an
8086 emulator against the port's, as for the player's commands, but with
`432f:28bd`, `546c:24d7` and `5b04:1415` run as they are on both sides:
the battles of the computer's turns' test, the party's and some monsters'
records given random spells of the table's combat ones (Stinking Cloud
left out) and some effect 0x47, the player's keys casting at random (Cast,
the spell list's arrows, the aiming keys, Exit and the "Abort Spell?"
answers, Alt-M and Alt-Q), or every turn the computer's with Magic On.
Records, items, effects, combat records, the tables, rounds, rolls, seed,
the order and the transcript (casts, prints from a cast to the next turn,
attacks, sounds, turns, rounds, keys, unported lines) were compared at the
end, and for a sample the screen at every key read and pause. Of 10,000
such battles 8,542 agreed and none differed; of 3,000 with every turn the
computer's, 2,789 (one more after a run past the emulator's instruction
limit); of 3,000 of the player's commands' kinds, 2,531; filled with 0x5a,
1,708 of 2,000, and with 0, 844 of 1,000; with the screens, 858 of 1,000;
none differing. The rest skipped a handler not ported (the breath of
monsters at event 0x0e, 0x46 at a death, Stinking Cloud's clouds, 0x22 of
Cause Disease twice), stopped where the original misbehaves (a cone
aimed at its own cell 71 times in 10,000, then a stop; a first target of
none, a pick of none weighed) or at the earlier parts' stops (a harness wand's
spell past the handler table, Target after Manual's Exit, sight off the
map), or both did. In the 10,000 agreeing battles 25,837 spells were
cast, 103 of them, each at least once (Friends and Knock once, Stinking
Cloud's twice through items); targets: the caster 4,714, areas 9,884,
separate picks 17,384, weighed picks 353, Silence 561; 22,776 flights;
no target 10,120 times, "Abort Spell?" 9,659 and 3,061 aborted; the
computer's casts 9,736 with 6,128 spells begun cast at their next turn
(the player's 7,461), its items 1,079; saving throws 25,285 (in the
computer's 3,000: 7,167 made, 12,646 failed); magic resistance rolled 493
times and resisting 255; touch rolls 784 with 462 misses; 718 missiles to
extra targets (`1cbd`); bolts 2,881 cells and 233 bounces; 209 cones;
216 spells turned back (0x47); 189 spells lost; Cure Light Wounds' ally
found 16 times. A later 1,500, after 6beb:0e08's reachable entries took
the spell targets, agreed on 1,282, none differing (one more stop: a
destroyed undead among them, where the stale byte is not known). After the
review (the cone at its own cell modelled, freed entries marked), 4,500
fresh battles: of 2,000 of the mixed kinds 1,706 agreed; of 1,000 that
start with both sides' target lists filled as an earlier battle leaves
them, 905; of 500 casting only Fear, 456, with 2,349 cones; of 1,000
filled with 0x33, 846. None differed, and none stopped at a cone's own
cell (before, 71 of 10,000 above and 187 of 400 battles of Fear in the
review's run stopped there). It is not part of the repository.

## Clouds, pools and breath

`src/cloud.h` ports the clouds and pools on the combat map: Stinking
Cloud's clouds (`5b04:29de`), what a cloud or pool does to one who stands
in it (`60f4:0dc3`, with `60f4:0c95`), the acid pools of those that
dissolve as they die (effect 0x46, `3f44:2037`), the ends of both (effects
0x28 and 0x50, `3f44:0ff7` and `2cad`) and Dispel Magic's part for the
clouds (`5b04:3352`-`357e`). `src/cast.h` ports the breath and attacks of
monsters, the effect handlers of event 0x0e (`5b04:4ab8`, `4dc2`, `4eea`,
`50cf`, `5227`, `546e`), with the cones and bolts of the spells in combat.
With them the effect handlers of groups D (0x28, 0x46, 0x50) and F (4, 6,
0x4e, 0x53-0x55, 0x5a, 0x68) are all ported, and effect 0x49's text.
Dragons, draconians, the aurak and the kapaks need them.

A cloud or a pool is a 30-byte node from the heap: its caster's record
(`+0`), the next (`+4`), the terrain kept under each of its cells k 1-4
(`+7` + k), whether cell k is covered (`+0x10` + k), its cell x, y
(`+0x1a`, `+0x1b`), its index among its caster's (`+0x1c`) and whether a
Dispel Magic failed against it (`+0x1d`). The clouds are listed from
`DS:7111`, the pools from `DS:7119` (`cok_combat.clouds`, `pools`), in the
order laid; `DS:7115`, a third such pointer, is only ever cleared. Setup
(`3cb2:1c58`) clears them and the battle's end (`3995:004b`) frees them, so
outside a battle they are empty. A cloud's cells are the cell, east,
south-east and south of it (`DS:2092`-`2095`: 8, 2, 3, 4); a pool covers
its cell. A covered cell's terrain becomes green cloud (0x1e) or puddle
(0x1d), whose tiles (RANDCOM's frames 4 and 3, `cok_combat_terrain`) are
what drawing the map shows; both are as high as floor, so sight passes.
The rest of combat sees them through the terrain only: a step's cost
leaves them out and notes them (`6beb:0c9d`), one in a green cloud may not
cast that round (`432f:0000`), the computer steps into a green cloud only
if it throws (type 0) or is immune and into a puddle only from level 7
(`3afb:0743`), the computer's Cure Light Wounds does not see a body under
a cloud (`432f:1eed`), and one who drops in a cloud leaves its body under
it (`6beb:0e08`).

Stinking Cloud (`5b04:29de`) for the selected character (the caster) at
the spell's cell (`DS:710f`, `7110`): the spell takes no mirror image
(`DS:6b39`); a node goes at the list's end, its index the caster's clouds
counted on the way there, which never looks at the list's last node; the
caster gets effect 0x28 for the caster level (`6346:29fe`) in minutes,
valued the index × 16 + the level, run when removed (`60f4:1285`). Each
of the four cells on the map and not a wall (cost 0xff) is covered; the
terrain kept is the cell's, or for one already 0x1e the last other cloud's
with a covered cell there that kept something else, or for a body's tile
(0x1f) the cell any body there covered, one taken back too (its entry
cleared to cell 0, 0). Then "Creates a noxious cloud" (row 10, no wait),
the screen centred on the cell (forced, `6beb:096b`), a pause, the text
cleared, and each combatant that stood on the cells, once each, is in the
cloud (`60f4:0dc3` with its flag).

`60f4:0dc3` for one who can act: with the flag (after a step, the player's
`3995:108b` and the computer's `3afb:0d2f`, and as a cloud is laid),
standing in a green cloud (some cell of its footprint, `6beb:0c9d`), not
undead (`+0xda`, `UNDEAD`, 0) and without 0x1f or 0x63, it throws (type 0,
no bonus); meanwhile the selected character is the cloud's caster
(`60f4:0c95`): saved, it "starts to cough", effect 0x1e, else "chokes and
gags from nausea", 0x1f, either for 1d4 + 1 minutes, valued 0xff
(`60f4:20f7`), and if it then has the effect its handler runs (`60f4:01a8`:
0x1e's loses the round's casting and items and worsens its armour class,
0x1f's ends its turn). Standing in a puddle, with the flag or without (the
end of a round, `3995:0b6d`), it takes 1d6 (`60f4:1261`) of the damage type
`DS:6b31` holds, with no save (`60f4:1db7`). `60f4:0c95` finds the cloud by
its top-left cell alone, the first whose cell k + 1's direction (`DS:2092` +
k) with cell k's flag matches; none found gives the list's first record
(`DS:609a`).

Effect 0x28's end (`3f44:0ff7`): the caster's first cloud whose index is
the value / 16 goes: "The air clears a little..." (`6346:1827`), each
covered cell gets back its terrain, or the body's tile where a body not
taken back lies, the node is freed, and every other cloud's cells and
every pool's cell are covered again. The screen is not redrawn. 0x28
lasts beyond the battle (it is not among the effects that end with it,
though the end of combat's list `DS:0390` takes it from the party); its
end then finds no cloud. Effect 0x46 (`3f44:2037`, event 0x0d) on one
without hit points or unable to act: "dissolves into acid!" (no wait) after
a pool is laid at its cell, as a cloud is (its index the holder's pools
before the list's last), with effect 0x50 for 1d6 minutes valued the
index, run when removed, the terrain kept as Stinking Cloud keeps it, and
the screen centred on it (forced). Effect 0x50's end (`3f44:2cad`): the
holder's first pool of the value's index goes: "The acid drains away.."
(`6346:1827`), its cell gets back its terrain or the body's tile, the
clouds' cells and the pools' are covered again (the clouds twice, around
the node's freeing).

Dispel Magic's part for the clouds (`5b04:3352`), after its targets, in
combat: for the spell's cell and then north, north-east, east, south-east,
south, south-west, west and north-west of it (each case moving x or y
from the last), a cell that is a green cloud is tried against each cloud
that has a cell there and has never resisted: a d100 up to 50, plus 5 a
level the caster level is above the cloud's, or less 2 a level below, as
a byte (`DS:4839`, `483a`, `5b04:3151`); the cloud's level is its caster's
magic-user level (`+0xfe`) plus its former one (`+0x106`) times whether
the selected character, the dispeller, may use its former class
(`66c2:0efb`), as a byte. Taken, the caster's first 0x28 valued the
cloud's index × 16 + a level is removed (its end above, which finds the
first cloud of that index, maybe another); else the cloud resists, for
good. After each such cell the screen is centred on the spell's cell
(margin 3).

The breath and attacks of monsters run on event 0x0e, which the
computer's attack (`3afb:0d49`) raises at its start, each again when that
returns with the turn going on. Each counts its uses in the effect's
value, set to 3 in the first round (`DS:714b` 0), and ends the breather's
turn (`6346:2964`) when used. A target is picked as the computer's cast of
a spell picks it (`432f:2337` with its flag, through `DS:6e3a`, for the
selected character): the area flag is left as that leaves it.

| Id | Handler | What it does |
| --- | --- | --- |
| 4 | `4ab8` | a d100 up to 75 to act; with a use and a d100 up to 25, the last of those within a cell (`6b30:08d8`, after the first listed) on another side: "Breathes!", it throws (type 3): saved, 10; failed, 20, blind for good (0x21, "is blinded") and the turn over; a use goes. Otherwise (no one, no use, or above 25) up to two energy bolts: a target as for spell 0x3d, "Fires An Energy Bolt!", the spell's missile (one picture, 30 ms), 1d8 + 2, and while it can act, a second 1d8 + 2; one that drops makes a new pick for the second; then the turn ends. The damage type is what `DS:6b31` holds |
| 0x4e | `4dc2` | a target as for Fireball (everyone within 3 of its cell, the thrower too): the spell becomes Fireball (`DS:6b33`, left so), the screen centred on the thrower (margin 0), "throws a fireball", the missile, then Fireball applied (`5b04:1071`) with 15d6 of fire and magic, half if saved; 0x4e goes. The turn goes on |
| 0x54 | `4eea` | poison: a target as for spell 0x24, a cone from the breather of width 0xff (everyone within 3 of its end) and length 3, either side; with anyone in it, the damage type 0x20, "breathes poison", the breather turned and drawn attacking, its panel, the missile, and each throws (type 3) and takes the breather's most hit points (`+0x62`), half if saved |
| 0x55 | `50cf` | lightning: the damage type 0x24; a target as for Lightning Bolt; "Breathes!", the bolt's missile from the breather to the cell (four pictures, 50 ms), then a bolt of the breather's most hit points (type 3 saves): the cell (`39bb`) and length 10 on (`3a6f`, no surcharge) |
| 0x5a | `5227` | acid: the damage type 0x30; a target as for spell 0x3d, a cone of width 1 and length 6; unless one in it is on the breather's side (`6346:2666`), "breathes acid" and the damage as 0x54's |
| 6, 0x53, 0x68 | `546e` | a dragon's: its uses 1 for 6, else 3, also above 3; the damage type 0x22, a cone of width 3 and length 8 (6) or 7 (0x53), "breathes cold", or 0x21, 9, "breathes Fire" (0x68); a target as for spell 0x3d; unless one in the cone is on its side, the damage as 0x54's |

The breath's damage types name no element ("takes 33 points of
damage"), but bit 0x20 makes effect 0x49 (`3f44:2665`, the dragons'
own) cancel the damage and the effect and say "is unaffected" (row 10, a
pause), also for a type left in `DS:6b31` by an earlier breath.

The cone (`5b04:1903`) reads its sight's range and, where no one is on its
line, its side line's direction from the stack, never set there: under
breath they are what the target routine (`432f:2337`) left. For 0x54 the
range is the line record of the last sight the routine's listing at the
picked cell looked along (`6b30:01a5`, called by `03f1` from `08d8`): the
high byte of its y and the low byte of its x step, which is 0 for a line
up or down a column, cutting sight to one cell (the burst then lies by the
breather), and else no limit. For 0x5a and 6/0x53/0x68 it is, where the
pick's redraw (`6beb:02aa`) drew the target's icon mirrored (facing 4 or
more), the byte the mirror (`127f:0b94`, Tandy) swapped last, the first of
the mask's last row, nibbles swapped, twice: 0 cuts sight to one cell;
else the segment a far call of `432f:11d4` pushed, in DOS where the
overlay manager loaded overlay 432f from `GAME.OVR` (0x432f in the
emulator), or 0x005b: no limit. The side, read only by the dragons'
(width 3): 0 where the redraw drew the target's cell's terrain and no
mirror after (`127f:08d7`'s flag of a masked draw); else the low byte of a
stack offset: `127f:0b94`'s frame pointer where it mirrored, else the
address of an argument of `432f:11d4` (pushed by `124b`). Those do not
depend on where DOS loads the game, and inside a battle every breath runs
at the same depth (`546e` 0x16e below `3995:0172`'s SP, in 6,786 calls of
an emulator's): they are 0x16 and 0x0c above the low byte of the stack
pointer the battle starts at, which the path the script was run by gives
(`cok_adventure.battle_sp`): 0x3dd4 for a vector `2fd3:3c28` runs itself,
measured by booting the game in an emulator and letting the demonstration
reach its battle (0xea and 0xe0); 6 less through `3b47`, a block entered
after NEWECL, and 8 less through `3403`, Encamp's camp and rest vectors,
both from the disassembly. PROGRAM 9's camp, run in the middle of a
script, runs its camp and rest vectors 0x128 bytes below the script
running PROGRAM (`COK_PROGRAM_CAMP_DEPTH`): `2fd3:3add`'s SP when `3403`
calls it from `3473` (through the dispatch `386d`) against its SP for the
script, measured by running ECL1 block 16's `PROGRAM 9` (at 8918) in an
emulator; so 0xc2 and 0xb8 below a script `2fd3:3c28` runs itself.

The port keeps these quirks:

- A caster's clouds are counted before the list's last node only: its
  second cloud gets index 0 again, and its 0x28s, alike, take the clouds in
  list order, not their own. Pools alike.
- `60f4:0c95` matches cell k's flag with the direction of cell k + 1, so
  a cloud is found from a cell it does not cover (a wall's) and not from
  one it does; one not found makes the list's first record the caster for
  the effect's event 9 (its caster level against magic resistance).
- The saved one's 0x1e handler runs with no effect (the pointer the search
  for 0x63 left).
- A puddle hurts with whatever damage type was left, and naming it.
- A body taken back but still on the map's tile (`6beb:10f3` clears its
  entry, cell 0, 0) still counts for a cloud laid over cell 0, 0.
- A cloud that once resists Dispel Magic is never tried again; Dispel's
  removal takes the caster's first cloud of the index.
- The aurak's breath on a saved target does not end its turn; its bolts
  use the damage type left from before.
- 0x4e leaves the spell Fireball (`DS:6b33`) after it, which later damage
  and magic resistance read as the spell cast; the area flag stays as the
  target routine left it after every breath.
- Poison breath and lightning hit friends; acid and the dragons' refuse
  with a friend in the cone, and with no one in it; 0x54's range cuts its
  burst to the breather's side when the target routine last looked along
  a column.
- A dragon's uses go back to 3 (1 for effect 6) whenever they are above 3.
- The event's later handlers still run after a breath ended the turn: a
  monster with two breaths can use both in one turn.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`:
Dispel Magic going on to the next cloud from one it freed (`5b04:354a`:
the node's next is read where FreeMem wrote the block's size, rounded to
0x20, as the pointer 0002:0000 (`1a46:03fc`): the walk goes on into the
interrupt vectors and through them into the BIOS's bytes, which differ
from machine to machine); a pool laid over a body's tile (`3f44:22a0`),
which compares the bodies with the spell's cell moved by a direction from
a byte never set (`[bp-0xe]`, a local of `60f4:0352`'s buffer, which
event 0x0d's calls leave unwritten for 0x46; an emulator found it 0 in 104
calls at 8 depths): where the 256 values give different terrains, the
pool's kept terrain is unknown and the run stops when it is put back with
no body there; a breath's bolt bouncing for ever between two walls (see
Spells in combat); 0x46 outside combat (the map is NULL); a breath aimed
at its own cell; terrain past the table (Stinking Cloud's).
Outside combat Dispel Magic's part reads the cells around the spell's
from the interrupt table (the map pointer `DS:6a2e` is NULL): the port
takes none of them to be a green cloud, as with none the walk does
nothing and the screen is not centred.
In DOSBox-X (`e_cloud_dispel.py`, the cloud at the heap's top) the battle
went on: no random number drawn but the dispel's own d100, nothing written
in low memory or the heap's free list, the cloud's tiles left drawn. From
FreeMem's 0002:0000 the walk reads INT 8's and INT 9's vectors there
(DOSBox-X's BIOS keyboard handler, F000:E987, whose bytes at +4 make
9090:9090, then NULL), none a cloud at the spell's cell (the freed block is
soon used again, so memory read after the battle shows other bytes in it).
Another machine's vectors and BIOS lead elsewhere: a 1 written into a
garbage node, an effect removed from garbage, or a loop. In camp (the
spell's cell 0, 0 in that run) no byte read was 0x1e and nothing happened,
as in the port.

| Function | Original |
| --- | --- |
| `cok_cloud_stinking` | `5b04:29de` |
| `cok_cloud_square` | `60f4:0dc3` |
| `cok_cloud_caster` | `60f4:0c95` |
| `cok_cloud_clear` | `3f44:0ff7` |
| `cok_cloud_acid` | `3f44:2037` |
| `cok_cloud_drain` | `3f44:2cad` |
| `cok_cloud_dispel` | `5b04:3352`-`357e`, `3151` |
| `cok_cloud_free` | `3995:004b`'s, `3cb2:1c58`'s clearing |
| `cok_cast_breath` | `5b04:4ab8`, `4dc2`, `4eea`, `50cf`, `5227`, `546e` |

A differential test ran the original's whole battle (`3995:0172`) in an
8086 emulator against the port's, as for the spells in combat: the
battles' monsters given breath (effects 4, 6, 0x4e, 0x53-0x55, 0x5a, 0x68)
or 0x46 as the game's monsters carry them, the party's and some monsters'
records Stinking Cloud and Dispel Magic among their spells, the player's
keys casting at random or every turn the computer's; the lists of clouds
and pools and the map were compared too, at each turn. Of 3,000 battles of
these kinds 2,816 agreed and none differed; filled with 0x5a, 952 of
1,000; of 1,000 with breath and every turn the computer's, 937; of 500
with breath and the player's keys, 461; of 1,000 with clouds, 936; with
the screens, 369 of 400; none differing. The rest skipped 0x4a (94 of the
6,900), stopped at Dispel Magic's freed cloud (78, and 11 where the
emulator also ran past its instruction limit, its FreeMem then filling
the node with 0xdd), at a dragon's cone whose
side byte changes its targets (53) or at the earlier parts' stops (a
harness wand's spell past the handler table, an attack aimed at no one, a
body found by a stale byte, sight off the map), or both did; two that the
original did not finish within the emulator's limit agreed with ten times
the limit. In the agreeing battles 2,447 clouds were laid (873 over
another's cell, 209 over a body) and 1,156 cleared; 6,049 saving throws in
a cloud, 2,898 coughing and 3,151 nauseated; the cloud's caster found by
its cells 5,598 times and the list's first record taken 451 times; 345
pools (9 over a cloud, 8 over a body), 166 drained, 10 puddles hurting;
Dispel Magic found a cloud's cell 158 times and tried 57 clouds, every one
resisting; the aurak's handler ran 3,114 times (115 breaths, 82 blinded,
33 saved; 590 bolts, 376 of them a second); the dragons' 12,959 (1,814
breaths, 1,285 refused for a friend in the cone, 708 with no uses left);
0x54's 1,771 (430 breaths), 0x55's 1,447 (435), 0x5a's 871 (184, 43
refused); 3,474 cones and 5,146 bolt cells with 452 bounces. 0x4e, which
none of those monsters carries, ran in a unit test, which ran the
original's routines (`5b04:29de`, `60f4:0dc3`, `0c95`, `5b04:320f` with no
targets, events 0x0d and 0x0e, the removals of 0x28 and 0x50) on
battlefields both set up, with effects added, cells poked, records
changed and kills between: of 2,000 such cases (the port built with
AddressSanitizer) 1,799 agreed, and of 1,500 that breathe 1,321, none
differing (0x4e 501 times, 151 fireballs; 4,410 clouds, 4,167 saving
throws in one, 44 pools); the rest stopped at Dispel Magic's freed cloud
(294, the emulator looping in 43 more), a step off the map, or an effect
whose removal runs it again (0x4e given the removal flag, which no
monster's has). After the review (the side byte worked out from the
battle's stack pointer, the harness's stack set so that `3995:0172` runs at
0x3dd4 as in the game, and its FreeMem writing the size at +4 as TP's
does), 2,500 fresh battles: of 1,000 of the mixed kinds 953 agreed, of 600
breathing 579, of 300 breathing by the player's keys 282, of 600 with
clouds, filled with 0x33, 565; none differed and none stopped at the side
byte (in the review's 2,500 at the harness's stack, 23 had); of 1,500 unit
cases 1,361 agreed, none differing. It is not part of the repository.

`eclplay`'s transcripts over every block (none, pass, won, melee, auto as
in Spells in combat, 17,843 runs) are the same as before these handlers
were ported but for 373, all from where the earlier build skipped one of
them (0x46 at a kapak's death 114, breath 208, Stinking Cloud 51); 22 of
those then stop: 13 at a dragon's cone's side byte, 6 at a body found by a
stale byte (`6beb:0e08`) and 3 at Molly's 0x07 outside combat, earlier
parts' stops reached on another path. With the side byte worked out, the
13 run to their end.

### Death Knights' clouds

Death Knights rewrote the clouds (`fight.clouds`,
`COK_CLOUDS_DEATH_KNIGHTS`; `cloud.c`'s `dk_` routines, which the public
ones call for it). It has no acid pools (its effect 0x46 is the fire
minions' flames) and no cloud effect on the caster: a cloud keeps its own
rounds. Its clouds are in one list (`DS:c443`, `cok_combat.clouds`), the
latest laid first, each a 31-byte node: the caster (`+0`), the next
(`+4`), the cell under each cell k 1-9 (`+7` + k), whether cell k is
covered (`+0x10` + k), the cell (`+0x1a`, `+0x1b`), its rounds left
(`+0x1c`), its level (`+0x1d`) and its tile (`+0x1e`). A cloud's cells
are the directions `DS:5843` + k (8, 2, 3, 4, 5, 6, 7, 0, 1): Stinking
Cloud's the first four (the cell, east, south-east and south, as
Champions'), Cloud Kill's all nine around the cell. Stinking Cloud's tile
is 0x1d and Cloud Kill's 0x1c (`fight.cloud_tile`, `pool_tile`); the
battlefield draws either (and 0x1e, which nothing lays, and a body's
0x1f, `6f03:010d`) as the cell under it with the icon of slot 0x0d past
its tile over it, masked (0x1a, 0x1b, 0x21, 0x22): the cell under
(`6f03:152e`, `cok_cloud_under`) is, for a cloud's tile, the terrain the
last cloud in the list covering the cell kept, for a body's the last
body's cell there, and the drawing takes the cell under that again
where it is one of those.

Stinking Cloud and Cloud Kill (`5dc5:2934`, `COK_CAST_STINKING_CLOUD`,
Cloud Kill's entry of value 1 where the original tests the spell for
0x22): after the burst of the cast entry, a node for the caster at the
spell's cell (`DS:c441`, `c442`), its rounds and level the caster level
(`65cd:2c33`); each cell on the battlefield keeps the cell under it and
is covered unless that is 0 or a wall (its cost, `DS:4420`), its tile
laid where covered; its combatant is noted unless an earlier cell's. The
node goes first in the list, the screen is centred on the cell (forced)
and each combatant noted is in the cloud (`638c:0c65` with the step). No
word is said, and the caster gets no effect.

One in a cloud (`638c:0c65`, `cok_cloud_square`), able to act, is called
by the same steps as Champions' (the player's Move `3e05:137c`, the
computer's step `3f63:0cb2`, a cloud laid, all with the step; the round's
end without, `3e05:0f21`). In Stinking Cloud's, with the step, the screen
is centred on it (forced) unless all of it is shown and the actions are
(`DS:c4d4`); then, unless it is undead (`+0x31`) or holds 0x1f or 0x63,
it throws (type 0): saved, it "starts to cough" (`DS:1c06`), 0x1e, whose
handler runs (with no effect, as Champions'), and one casting then "lost
a spell" (row 12, a pause), the spell marked to memorize again
(`65cd:17c4`) and its turn over (`65cd:2bac`); failed, it "chokes and
gags from nausea" (`1c1b`), 0x1f, whose handler runs; either for 1d4 + 1
minutes, valued 0xff (`638c:1f86`). The selected character is kept (not
the cloud's caster). In Cloud Kill's, with the step or not: one of
level (`+0x30`, signed) 0-4, of 5 who fails a throw at -4, or of 6 who
fails one "is poisoned" (`DS:1ccf`, row 10), a pause, effect 0x37 for no
time valued 0xff, and is killed (`638c:00c8`, status 6, "is killed").

The end of a round (`3e05:0ed0`, after the time passed; `638c:21ec` with
0, `cok_cloud_round`): each cloud whose rounds are 1 or fewer (signed)
goes, the others' one fewer; a cloud of level n lasts to the end of the
n-th round. One going puts each covered cell back to the terrain it
kept, or a body's tile where a body (not taken back) lies, and is freed;
after the list, if any went, every cloud's covered cells get its tile
again, in the list's order (`638c:213c`: where two overlap, the earlier
one's tile is left), and "the air clears a little..." (`65cd:19c5`, row
24). The screen is not redrawn. One who dies in a cloud
(`6f03:0fbd`) leaves the cloud's tile on the map, its body keeping the
terrain under the cloud.

Dispel Magic (`5dc5:2e5c`, after its targets, `2f93`): for the cells
around the spell's cell by the steps (`DS:4412`, `441b`, its own last),
on Stinking Cloud's tile each cloud with one of its first four cells
there, on Cloud Kill's each with one of its nine, covered or not, is
tried (`2dd7`), and taken is marked (`+0x1c` 0xff); after each such cell
the screen is centred on the spell's cell (margin 3); then the clouds
marked go (`638c:21ec` with 1). The try is a d100 up to 50, 5 more a
level the dispeller's caster level is above the cloud's; below it, 2
less a level that the byte `DS:8954` is above the dispeller's (the
cloud's level is never stored there, as Champions' `3151` stores it in
`DS:483a`): what the dispel's first part left there, the level of the
last effect it tried (`cast.c` passes it), or what an earlier Enlarge,
Sleep, Death Spell or Dispel Magic left. Where the first part tried no
effect the port does not know that byte and the run stops
(`COK_ECL_UNDEFINED`).

The port keeps these quirks: a Cloud Kill or Stinking Cloud over another
cloud keeps the terrain the other kept, and where they overlap the older
cloud's tile is the one laid again whenever any cloud goes; Dispel
Magic counts a cloud's cells by the tile it finds (four on Stinking
Cloud's, nine on Cloud Kill's), so that a Cloud Kill's cells past its
fourth are not tried from Stinking Cloud's tile, tries a cloud once at
each cell of the walk it has a cell at, covered or not, and one already
taken again (each a d100); the computer's step into a cloud
(`3f63:06ca`) is Champions' test, by the same tiles.

| Function | Original |
| --- | --- |
| `cok_cloud_stinking` | `5dc5:2934` |
| `cok_cloud_square` | `638c:0c65` |
| `cok_cloud_round` | `638c:21ec` with 0, `213c` |
| `cok_cloud_under` | `6f03:152e` |
| `cok_cloud_dispel` | `5dc5:2e5c`'s second part, `2dd7`, `638c:21ec` with 1 |

Checked: the routines' constants against the code (`test_death_knights.c`:
the spell compare and tiles of `2934`, its cells' table, bounds and calls;
`0c65`'s words, levels, throw, effect and status; `21ec`'s compares, body
tile and cover; Dispel's counts, mark, stale byte and call), unit tests
on records made by hand (`test_fight.c`), `eclplay --combat auto` over
ECL1-3's fights (the Vampire Mage's and Igorf's Stinking Clouds cast in
10 of 600 runs, none stopping at a cloud), and DOSBox-X: `dkk_fight.py`'s
clouds flow (two Cloud Kills and a Stinking Cloud cast by keys, TANAR
killed in one, a Dispel Magic taking the Stinking Cloud, then Auto, round
by round to the 9th with the battlefield's cells and bodies, the Cloud
Kills gone at the 7th's and 8th's ends).

## Treasure and the end of combat

`src/treasure.h` ports `TREASURE` (`2fd3:1d21`) with its random items
(overlay `58e7`), the end of combat (overlay `351b`), which every `COMBAT`
runs, with or without a battle, and the money pool's routines in `58e7`,
which the shops share.

The pool (`cok_pool`) is the coins at `DS:6b0c`, seven signed LongInts in
the order of a character's coins (silver, copper, bronze, platinum, steel,
gems, jewelry; names at `DS:12e3`), and the items at `DS:6b28`, a list of
63-byte items linked at `+0x2a`, kept in list order, the head first. Two
lists only combat fills, both empty until it is ported, are kept with it:
the last missile combat put in the pool (`DS:60a2`, `432f:1a45`), as the
item it is, and the weapons lost in combat (`DS:609e`, effect 0x43,
`3f44:1dc5`): 71-byte nodes of an item and the record it goes back to
(`+0x3f`). The original keeps the missile as a pointer, which only combat
setup clears (`3cb2:1c58`, as the port does), so that a later item
allocated where a freed one was can match it; the port forgets it when its
item goes. The flag
`DS:711e` ("money in the pool"), which Pool, Share and Take write and
nothing reads, is left out.

`TREASURE silver copper bronze platinum steel gems jewelry items` sets the
pool's coins to the seven words, replacing them, and by the items
operand's low byte adds below 0x80 the items of `ITEM<file>.DAX` record
`items` (the file a digit, as `LOAD MONSTER`'s), each put first in the
pool, so in the reverse order, with the names they were stored with; 0x80
to 0xfe, that less 0x80 random items, each put first, after which every
item in the pool is named (`6346:0488`), old ones too; 0xff none. A
record that is not there says `Unable to find item file` on row 24, waits
for a key and quits to DOS; no shipped `TREASURE` names one. One that is
not whole items, which the original reads past, stops the port with
`COK_ECL_UNDEFINED`. `eclplay` logs the coins and the items added as
`treasure:`.

Each random item's type comes from `TREASURE`'s own dice, then
`58e7:1039` makes it:

| d100 | Type |
| --- | --- |
| 1-20 | a second d100: 1-10, a d10 (hammer 1-2, mace 3-6, flail 7-8, quarter staff 9, staff sling 10); 11-25, a d20 (darts 1-3, javelin 4-5, long bow 6-8, composite long bow 9-10, short bow 11-12, composite short bow 13-14, sling 15-16, staff sling 17-18, arrows 19-20); else a d4, 1-3 a d20 (long sword 1-8, broad sword 9-13, scimitar 14, bastard sword 15, short sword 16-18, two-handed sword 19-20), 4 a d8 (battle axe, dagger 2-3, mace 4-5, military pick, spear, halberd) |
| 21-65, 66-70 | a magic-user's scroll (0x27), a cleric's (0x28) |
| 71-80, 81-85 | a shield (0x25), a ring of protection (0x3b) |
| 86-95 | a d20: leather 1-3, banded 4-11, plate 12-16, ring 17, scale 18, chain mail 19-20 |
| 96-100 | a d6: bracers 1-2, a potion of healing 3-4 (0x30), a wand of magic missiles 5 (0x34), a potion of giant strength 6 (0x35) |

The item starts zeroed, unreadied, its type set and its parts `+0x2f` and
`+0x30` hidden (`+0x35` 6). A weapon, armour, shield, ring or bracers has
a bonus of +1, or +2 on a d10 of 10 (`58e7:1005`), and is named by its
type and `+1` or `+2` (name parts 0x6f, 0x70); armour (0x1f-0x24) is its
type, `Armor` or `Mail` and the plus, only the plus hidden; the ring
`Ring Of Prot` and the plus; bracers `Bracers of AC 6`, bonus 4 whatever
the roll. Weights are the original's by type; darts come twenty to the
item at 5, and arrows, quarrels and every type not listed twenty at 4. The
value is the bonus times 1000, or 1250 for a shield, 75 for arrows, 1500
for ring and scale mail and bracers, 1750 for chain, 2000 banded and 2500
plate. A scroll has a d3 of spells (`1 Spell`, `2 Spells`, `3 Spell`, sic),
bonus 1 and weight 25; a magic-user's order is a d2, 1 Red (`+0x35` bit
0x10) and 2 White (0x20), named `Red MU Scroll` or `White MU Scroll`, a
cleric's `Cler Scroll` with nothing hidden; each spell is a d4 level, then
a die of the level's table (`DS:423a`, `4262`, `428a`, ten bytes a level,
from 1), worth 150 a level, in `+0x3c`-`+0x3e`. A wand, the potions and a
javelin that rolls 5 on a d5 after its bonus (of lightning) take a
template of eight words from `DS:0b88`: three name parts, weight, value
and `+0x3c`-`+0x3e`, with bonus and saving throw bonus 1, so that Detect
Magic marks them; it keeps the type and the hidden parts, so they show as
`Potion`, `Wand` or `Javelin`. A potion of healing is extra healing on a
d8 of 6-8. The generated potions are of types 0x30 and 0x35, where the
shipped ones are 0x2f, and the healing potion's spell is 3 where the
shipped one's is 0x3e. They can be used: in DOSBox-X (`c_ecl3.py`) a
generated potion of type 0x30 from ECL3 block 81's `TREASURE ... 130`,
readied and used, healed its drinker fully and was gone, as in the port.

The end of combat (`351b:1968`), as `cok_treasure_end_of_combat`:

1. Each weapon lost in combat goes back to its owner, who is selected
   (`351b:185f`); taking an item (`36d0:034c`) puts a copy at the end of
   the selected character's items and recomputes its stats unless it
   would be overloaded (`546c:32b0`), which shows `OverLoaded` on row 24:
   the weapon is lost.
2. Var `0x7ec7` is 0, and the party's part runs (`351b:0574`), over the
   party records: those before the first past the party's size (a combat
   record's `+0x13`), or with no battle, every record, as the original
   reads `0000:0013` for none. Someone running (status 3) means the party
   fled. Each party record loses the first effect of each id that lasts
   only through combat (`DS:0390`: 0x03, 0x0b, 0x15, 0x17, 0x1b, 0x23,
   0x28, 0x33, 0x34, 0x35, 0x1f, removed as `cok_effects_remove` does).
   The party is destroyed unless a party record has status 0, 1 or 3, is
   on the party's side (`+0x18a` `HOSTILE` 0) and is not an NPC (`+0xe7`
   `NPC` below 0x80), or in a fight that cannot kill (var `0x7ee6`, which no
   script sets) anyone in the list can act or is running, unconscious or
   dying. One that stands (status 0 or 1) means the party did not flee, and
   the experience is worked out and given (below). If the party lives,
   either it comes round: running and unconscious with hit points become
   okay, dying unconscious, and in a fight that cannot kill dying and
   unconscious are okay with a hit point; or when it fled, var `0x7ec7` is
   0x81, those who ran are okay and the rest are left behind, removed
   (`4def:3b0a`, the record before then selected) and counted out of
   `0x7f3e`. A destroyed party is removed and `0x7f3e` is 0.
3. The mode is 6, and the enemies are removed (`351b:1493`): every record
   whose combat record's `+0x13` or whose `+0x18a` (`HOSTILE`) is 1 counts
   as a monster (`DS:43c4`) and is removed (`4def:3b0a`), freeing its icon
   slot, one not past the party's size counted out of `0x7f3e`; var
   `0x7ec8` counts those that dropped (`+0x189` `ACTIVE` not 1), var
   `0x4cf8` is set if the first one did, and var `0x7ec7` becomes 1 for one
   that fled while it is 0. The others' combat records are freed, the first
   record is selected, and every record's stats are recomputed
   (`6346:0d20`). Where the selection the original keeps in `DS:43bf`, which
   `EXIT` restores after `LOAD CHARACTER` and a block's vectors restore when
   they end (`2fd3:3b47`), is a record removed here or in step 2, the
   original would go on with it freed, and the port stops with
   `COK_ECL_UNDEFINED`, unless the party was destroyed and the run ends
   first.
4. Unless the party was destroyed: if it fled, the pool's items go; the
   NPCs take their shares, the results show and the treasure menu runs
   (below); then the pool's items go. Its coins stay until the next
   `CLEARMONSTERS` or `TREASURE` replaces them. If it was destroyed, var
   `0x7ec7` is 0x80, the frame is cleared, "The monsters rejoice for the
   party has been destroyed" typed (the text delay is on since the
   battle's end, see Library) in cells 2-37 from row 5, cleared first
   (`351b:1aab`), and "Press any key to continue" shown in light
   magenta on row 24 until a key, and the run ends (typed in the
   original too: DOSBox-X, `d_bozak.py`).
5. The text delay is set (`351b:1b00`), and vars `0x7f70`-`0x7f72`,
   `0x7ee3`, `0x7ee6` and `0x4cf5` are cleared.

The original's path for a duel (`DS:883e`), which is never set in this
game, is left out. In the demonstration (`DS:4b4b`, see The start menu)
the party's part is skipped and the end of combat returns once the
enemies are removed (`351b:1982`, `1996`): no recompute, results,
treasure or cleanup.

The experience (`351b:0037`) is a LongInt, `DS:8840`: each defeated enemy
in the list (`+0x18a` `HOSTILE` 1, status neither 0 nor 3) counts as a
monster (`DS:43c4`, which `351b:1493` sets too for each enemy it removes)
and gives `+0x130` + `+0x132` × `+0x11b` (`MONSTER_EXPERIENCE` +
`MONSTER_HIT_POINT_EXPERIENCE` × `BASE_HIT_POINTS`), the product a signed
word; a monster past the party's size puts its coins in the pool, and unless
monsters keep their items (var `0x7ee3` 1) each of its items is named and
put first in the pool, unreadied, in its order, so the last first. Then
the pool's coins count, as signed LongInts: a hundredth of the silver, a
twentieth of the copper, a tenth of the bronze, the platinum, 2 a steel,
250 a gem and 2200 a jewel; then 400 times the bonus of each magic item,
as a signed word, from the pool's head to the last missile recovered. The
sum, divided by `0x7f3e` less the party records that cannot act or are
animated (`DS:883f`), as a word, is what each gets. It is given
(`351b:0379`), unless the bank is paying out (var `0x4cf5`), to every
record in the list that can act and is not animated, monsters still in it
too, added to `+0x116` (`EXPERIENCE`): a tenth more for a cleric (`+0x5b`
`CLASS` 0) of wisdom above 14, a fighter, paladin, ranger, mage or thief
whose prime requisites (strength; strength and wisdom; strength,
intelligence and wisdom; intelligence; dexterity) are above 15; half for two
classes (8, 10-14, 16) and a third for three (9, 15); all for the rest.

The screens, each on a cleared frame (`1128:0000`), are:

- The NPCs' shares (`351b:1618`). Each NPC that is okay counts `+0xe8`
  `SHARES` & 7 shares, every other record one, as bytes; with any NPC
  shares, of each coin in the pool the NPCs take their shares of the pool
  divided by all the shares, cut to a byte, and the coins are gone. Then
  each okay NPC with `SHARES` set says `NAME takes and hides his share.`
  (`her` for `+0x109` `GENDER` not 0) in light green in cells 5-34 from row
  5, two rows apart, each cleared first, and `press <enter>/<return> to
  continue` waits on row 24, all one item, black on white (`67b5:03e2`):
  Enter, space, Escape or a special key ends it.
- The results (`351b:0b23`), in light green from column 1: after a fight
  (`DS:43c4`), `The party has fled.` on row 3, with no experience and the
  pool's coins gone; `You have lost the fight.` in a fight that cannot
  kill with none standing, with none, and var `0x7ec7` 0x80; else `The
  party has won.`; without one, `The party has found Treasure!` on row 3,
  or for the bank `The party makes a withdrawl.  A small` and `fee has
  been assessed by the bank.` on rows 5 and 7. Then, but for the bank,
  `Each character receives N` (signed) and `experience points.` on rows 5
  and 7, and the wait for Enter.
- The treasure menu (`351b:118c`), after the screen is redrawn for mode 6
  (`6346:2c17`): the adventure frame, the treasure's picture (`PIC`
  record 0x3c) in the view's place, which the menu draws as it waits, and
  the party list, with no status line. Row 24 offers `View Take Pool
  Share` with money in the pool, `View Take Pool` with only items, else
  `View Pool`, then `Detect` if there are items and the selected character
  can act and has memorized a Detect Magic (5, 0x0b, 0x4d or 0x67, the
  first in `+0x1e`-`+0x57` `MEMORIZED`), and `Exit`; the prompt is empty,
  the items white and light green, as the camp's. Special keys pick a
  character. `View` is View (see View), in which Drop puts the money in the
  pool (`58e7:09fa`); `Detect` casts the spell (`5b04:1415`) without saying
  "casts". `Exit` or Escape leaves when the pool is empty; otherwise
  `There is still treasure left.  ` in light green and `Do you want to go
  back and claim your treasure?` in white in rows 17-22, and `~Yes ~No`
  (`3775:1885`, the selection kept from the menu before): No leaves, Yes
  clears rows 17-22 and goes back.

`Take` (`351b:0ff7`) takes the items if the pool has no money, the money
if it has no items, else offers `Take: ` and `Money Items Exit` until one
runs out, Exit or Escape. It takes special keys by their scan codes'
letters, as Pics does: up and down (`H`, `P`) pick a character, PgUp
(`I`) takes items, and `E`, NumLock, would leave but never reaches the
game (see Party). Items (`351b:0ec5`, `0df1`)
lists the pool's items, each named first, in cells 1-38 by 1-22 of the
open frame, with `Items: ` and `Take` (`cok_menu_rows`, the first row
shown kept as lists keep it, the pick from the first each time); the one
picked goes to the selected character (`36d0:034c`) and leaves the pool,
unless it would be overloaded, and again until the pool is empty or none
is picked. Money (`58e7:0d01`) lists the pool's coins, jewelry first, each
`Name amount`, in cells 2-15 by 2-8 of the frame with its row 16 divider,
with `Select type of coin ` and `Select` (no space before it, unlike
Drop's); the coin picked asks `How much steel  will you take? ` (two
spaces, as for each coin but gems, `How Many Gems  will you take? `) for at
most the pool's low word (`58e7:028d`), and the selected character takes
it (`58e7:0a81`): if that much would overload it, `Overloaded` and nothing;
else at most what the pool has. Again until the pool has no coins or none
is picked. Each time the screen is redrawn after.

`Pool` (`58e7:0511`) puts the money of every player character, turned
ones too (`+0xe7` `NPC` 0 or 0xb3), in the pool, lightening its weight as
words; NPCs keep theirs. `Share` (`58e7:063a`) divides each coin by the
records Pool counts, a byte: each record below 0x80 then takes its share of
each coin, jewelry first, and a coin of the remainder while one is left, as
far as it can carry (`58e7:006d`: its weight and the amount, a word that
wraps, against its allowance + 1500, `6346:153b`); what it cannot carry
goes back to the remainder. Then each record in the list in turn, NPCs and
monsters too, takes as much of each remainder as it has room for, and
what is left stays in the pool. With money and no record to count, Share
divides by zero, runtime error 200, and the port stops. The routines the
shops use are ported with them: the value of coins in steel (`58e7:00d3`,
1, 5, 10, 25 and 50 for coins 0-4, over 50), paying in steel from the pool
or from a character (`58e7:018f`, `0155`) and giving steel, the rest to the
pool with `Overloaded.  Money will be put in Pool.` (`58e7:01f2`).

The port keeps these quirks:

- `TREASURE` replaces the pool's coins, so of two in a row only the
  second's count, and both's items; its random items rename the items
  already there.
- The monster's experience product is a signed word, as is 400 times a
  bonus of 82 or more; coins count by signed LongInts.
- The divisor is a word: with everyone out it is 0, runtime error 200, and
  the port stops with `COK_ECL_DIVIDE_BY_ZERO`; with more out than the
  party's size (an NPC or an ally among the party records) it wraps, and
  each gets next to nothing.
- The experience goes to every record that can act, monsters still in the
  list included, before they are removed; a cleric needs only wisdom 15.
- A party member against the party that drops gives its items, not its
  coins, and goes with the enemies.
- A party of NPCs, or of characters on the other side, is destroyed,
  whoever stands.
- Only the first effect of each combat-only id goes.
- When the experience was not worked out (no one standing) the results
  show `DS:8840` from the last combat that did; with none before, the port
  stops with `COK_ECL_UNDEFINED`. Only a fight that cannot kill reaches
  this.
- An NPC's share is cut to a byte, so at most 255 a share of each coin;
  the coins vanish; an NPC whose `+0xe8` (`SHARES`) is 8 takes no share but
  says it hides one; shares that wrap to 0 divide by zero, and the port
  stops.
- Share counts turned characters but gives them nothing, so their shares
  are lost, and gives records whose `+0xe7` (`NPC`) is 1-0x7f a share they
  were not counted in; a record already carrying more than it may takes a
  whole remainder, its room wrapping. A hoard too heavy for the party,
  such as the 95,000 coins of ECL3 block 98, stays in the pool after
  Share.
- Take money checks the amount asked against the load before it cuts it to
  the pool's, and asks at most the pool's low word.
- A fled party's pool loses its items before the menu and its coins at
  the results; the menu still opens.

Combat fills the weapons lost (effect 0x43) and the missile recovered (a
thrown weapon used up, `432f:1a45`); see Attacks, movement and death.

In Trade from the treasure menu's View, Escape at the first coin list
tests a byte of the stack (see View). Loading the treasure's picture
(`6961:01fd`, through `6346:2c17`) zero-fills it, so Trade asks for
another partner when the menu's redraw loaded the picture: always after a
battle, whose setup frees the picture (`3cb2:1c58`, `6961:0537`), and for
treasure alone unless the picture was already there. After a View in the
same menu, or Yes to go back for the treasure, the byte is set, and Trade
is left; after Take, through the coins or the items, it holds the low
byte of a segment, and with it unknown, as when the picture was already
there, the port stops with `COK_ECL_UNDEFINED`. Pool, Share, Detect and
picking a character leave it as it was. An emulator run of the paths
agreed.

A differential test ran the original routines in an 8086 emulator against
the port. The item generator (`58e7:1039`), on 40,000 random types and
seeds, agreed in every byte of the item and the seed. Of 30,000 cases of
`TREASURE` with random items, `351b:0037`, `0379`, `0574` (on random
parties, monsters and pools, half after a won, fled or lost battle),
`1493`, `1618`, `36d0:034c` and the `58e7` routines (Pool, Share, take,
drop, the value and the three steel routines), with the records, items,
effects, pool, variables, flags, selection, messages and random numbers
compared, the 28,011 the port carries out agreed. Both refused 1,719:
Share with no one to count (698) and the experience divided by none
(351), runtime error 200 in the original; a routine given no selected
character, which the original reads through NULL (590); and an effect
removed after combat whose handler needs combat, which in the original
does not return either (80). The port alone refused 270, effects removed
after combat whose handlers need combat (0x03, 0x15, 0x1b, 0x1f and
0x33-0x35), which the original's handlers carried out on random data. It
is not part of the repository. The screens were tested in the port only.

### Death Knights' end of combat

Its end of combat (`dkk 3a37:1318`) gives no lost weapons back and the
NPCs take no shares (`combat.weapons_back`, `shares`); the party loses its
own effects after combat (`3a37:0506`, `DS:455d`: 19, `combat.combat_effects`);
the experience (`3a37:0000`) counts its coins 4-6 (the record's `COINS`,
`cok_field_first`); the award (`3a37:02fa`) then sets whether each may
train (`65cd:38c7`, `combat.may_train`, `COK_FIELD_MAY_TRAIN` +0xc6): a
class of level 1-17, the first that may deciding, whose next level's
experience its row gives (`65cd:382c`, a paladin's row 10,
`characters.paladin_row`) it has, above 0, below its race's highest level
in the class (`DS:82d4`, `COK_DATA_LEVEL_LIMITS`). An ally's or a
monster's that reads past those (a knight of no order, race 7) is removed
after combat unread, and the port leaves it. The results (`3a37:09b7`)
speak in its own words (`combat.fled` and on: `the party has won.`, ...),
without the bank's lines, then whether the pool holds anything (`the
party has found treasure`, `... no treasure`) on row 10 and its key
(`Press button or enter to continue`); the treasure menu (`3a37:0eed`)
comes only for a pool that holds anything (`combat.treasure_if_any`; see
Death Knights' treasure); a destroyed party's words are its own.

A destroyed party (`3a37:0506` sets `DS:8c5a`, Champions' `4b57`) ends
the ECL at once (`3632:2cd5` tests it after each opcode: nothing after
the lost `COMBAT` runs, and no script reads the outcome's 0x80), and the
view draws nothing more (`6c84:00a6`, as Champions' `6945:00ba`:
`cok_adventure_view` returns, the screen keeping "the monsters rejoice").
The adventure's loop (`3632:2e06`) runs a block's load vector (`2cd5`
with `DS:8c46`, and the blocks a `NEWECL` there enters, `2d28`) and then
reads its menu (`4307:0257`) before it tests the flag: a party destroyed
in the load vector (Begin Adventuring's first fight, block 16) still gets
the menu once, "Move Encamp" in the open (`combat.killed_command`), from
its first item (`DS:c0f7` 1), its choice not acted on; then the loop ends, the records go (`49cb:36ee`)
and main shows the start menu again (`49cb:010b`). A destroyed party in
the after-move or location vectors ends the loop with no menu. In mode 3
without the overland menu (0x4cf7 0, as after ECL1 block 2's fight, whose
side is NPCs alone, so that `3a37:0506` finds the party destroyed though
every enemy dropped) `4307:0257` reads no key: it clears the text window
if text is shown and returns a byte of its stack, unused here.
Champions' `2fd3:3c28` has the same shape (`475c:09ec` before `4b57`),
not checked against the original: its `killed_command` is off, its game
ending at once as before. Where a loaded game's entry would redraw first
(mode not 3 and `DS:8c57`: `65cd:2db0`) the port stops (`not yet
ported`). Checked in DOSBox-X: `dkk_fight.py`'s lost flow (the first
fight's party written dead at its first turn: the rejoicing, the menu,
Encamp, the start menu, 0 pixels each).

### Death Knights' treasure

Death Knights of Krynn has no random items and no `TREASURE`: its
treasure is coins and the entries of an item table (the descriptor's
`treasure` section, `cok_rules_treasure`). Its pool holds three kinds of
coin, steel, gems and jewelry (`DS:be58`, `5c`, `60`: the pool's coins
4-6, as the record's `COINS` are), its items at `DS:be64`; Pool, Share,
Take and drop go over the kinds the game's records hold (the layout's
`COINS`, `cok_field_first`: Champions' 0-6), `5c72`'s routines being
`58e7`'s otherwise, and so does `ROB` (`3632:1a76`, `3c1e:1a00` and
`1a7f`: Champions' over its three coins). The shops' payment
(`cok_shop_money`, `cok_pool_pay`, `set_steel`: `58e7:00d3`, `0155`,
`018f`, coins 0-4) has no counterpart in Death Knights' code, whose shops
pay from the steel (see Death Knights' shops).

The item table (`4f9a:0006`, the end of startup `4f9a:00e1`; its name
`ITEM0` at `4f9a:0000`) is `ITEM0.DAX` record 1 read as 17-byte entries,
an item's `+0x2e`-`+0x3e` (type, the three name parts, bonus, saving
throw bonus, readied, hidden parts, curse, weight, count, value,
`+0x3c`-`+0x3e`), into ids 1 to at most `0x7f` of a table of 254
(`DS:a2ca`, an id's entry at `DS:a2b9` + 17 × id) filled with `0xff`
first; an entry starting `0xff` is none. The shipped record holds 96
(`game->item_table`). A record that is not there says "unable to find
item file" and halts (`DS:1abb`); the port's open fails, as it does for a
last entry cut short. Ghidra's `DS:a2b9` is the base for id 0, which would
read the 17 bytes before the table (the party list's pointer at `DS:a2c6`
among them) and 255 those after it: no script names either, and the port
stops (`COK_ECL_UNDEFINED`).

`TREASURE ITEMS steel gems jewelry n : id...` (`3632:1933`, a list over a
header of 4: `cok_ecl_header_value`) adds the three words to the pool's
coins (not replacing them, as `TREASURE` does) and puts each id's item
first in the pool, so in the reverse order: the entry at `+0x2e`, `+0x29`
and the link cleared, the name before them whatever the stack held (the
port's 0; the name is built before anything shows or saves it, from
`+0x00` as long as it is, so a saved item holds the stack's bytes after
its name where the port's holds 0s). `STOCK n` (`3632:28d2`) frees the
pool's items, then for shop n (1-8, any other 1) puts the ids from index
`DS:0007` + n for `DS:000f` + n of them (`DS:0017` + index:
`COK_DATA_STOCK_FIRST`, `COUNTS`, `ITEMS`) first in the pool, each shown
(`+0x35` & 0xf8), and opens the shop (`3b89:06f3`, see Death Knights'
shops), which puts back the mode it found. `eclplay`
logs the coins and items added as `treasure:`, the items named without
naming them.

The treasure menu (`3a37:0eed`), Take (`0dfc`) and its items (`0ce8`,
`0c26`) and money (`5c72:0ac4`) are Champions' but for their menus, by
index (`6a69:0f1b` over a table of words, `DS:3560` "View Take Pool Share
Detect Exit", `DS:354b` "Money Items Exit", and the indices shown,
`6a69:00ab`, `00cb`; the same words as Champions' build, so the port
builds them so): each menu's selection starts at its first item
(`treasure.menus_first`: `DS:c0f7` set to 1 by Take and its items, the
treasure menu keeping its own from 1 apart from theirs, and it and the
vault's leaving the selection as the menu before had it); special keys are
told apart from letters (`specials_apart`, `DS:c0f8`): Escape is Exit,
up and down pick a character, the others do nothing (Champions' Take
takes PgUp as its `I`); leaving with treasure left says "there is still
treasure left. " and "do you want to go back and claim your treasure?"
(`DS:194f`, `1978`) and the Yes/No starts on No (`6a69:2048` with
`DS:57c1`'s 1, `claim_no`); Take says "take: " (`DS:3168`), its items
"items : " (`DS:315d`), its coins "select type of coin " (`DS:104c`), and
the coin's question "How much Steel  will you take? ", "How many Gems
will you take? " (`DS:0fae`, `0fce`, `0fb9`; `5c72:09e4`'s words
`DS:0c62` + a space). Detect casts through its cast entry (see Death
Knights' casting); View is the port's View, as camp's.

Taking an item (`3b89:033b`) is `36d0:034c` but that an item with a count
(`+0x39`) joins its kind once taken (`580d:1be6`, View's Join,
`cok_item_join`), and so with 16 items one is taken that matches one of
the character's in type, name parts, bonuses, curse, weight and
`+0x3c` (below 2) to `+0x3e` (`580d:2a0a` with 1): its weight alone
counts; the word when it cannot is "Overloaded" (`DS:2acc`,
`treasure.overloaded`; Champions' "OverLoaded"). The join takes its item
from a pointer the routine sets only when the character had items: into a
character with none, a counted item is joined with the stack's pointer,
and the port stops (`COK_ECL_UNDEFINED`).

The bank vault's menu (`3948:000e`, `cok_treasure_vault`), which `COMBAT`
opens with var `0x4d00` 1 (ECL1 block 35, the vault of Gargath, always:
var `0x4cf8` reads 1, so its bank script is never reached): mode 7, the
screen redrawn (`65cd:2db0`, `DS:db62` set: the adventure frame, the small
picture's first frame at 3, 3, the party list, the status line and "bank
of gargath", `DS:2ccd`, at 1, 15 in light green), the pool's items freed
and the vault read into it (`46b2:2e1d`: `TMPVAULT.DAT` where the menu
left it, else the vault of the game loaded last, `DS:fb79`; its coins as
the pool's three, its items after the pool's in the file's order; none:
no coins, or steel a hundred times the savings `0x4c33`). Its menu
(`DS:35bd`, `treasure.vault_items`; no picture animated, `6a69:0f1b` with
0) offers View, Take with money or items in the pool, Pool, Money when the
selected character has coins, Items when it has items, and Exit, its
selection kept from the first, Escape for Exit, up and down picking a
character. Take is the treasure menu's Take; Pool pools the party's
money; Money deposits the selected character's coins (`580d:26d0` with 0:
in the text window, steel first, each row its name, spaces to 18 less the
name's and the amount's lengths, the amount; "select type of coin ", and
"How much Steel will you deposit? " for at most what it has, `DS:1237`;
the coins go into the pool, as a drop does in modes 1, 6 and 7,
`5c72:0891`), the text window cleared after. Exit writes the pool to
`TMPVAULT.DAT` (`46b2:302d`: coins, then the items in order; the disk's
space check left out) for the next save to make the saved game's vault
(see Party). View and Items are Death Knights' sheet (`580d:0c66`) and
items menu (`0ffa`, then the screen redrawn, `65cd:2db0`), which in mode 7
deposit what they would drop: coins and items into the pool, which Exit
writes (see Death Knights' View).

`DAMAGE` (`3632:1ca2`) is Champions' but that a save's bonus is bits 0-3
of the first operand (`treasure.damage_bonus` 0x0f; Champions' 0x1f, so
its bit 4, "deal it even when saved", adds 16 to the bonus there), a save
made with bit 4 halves the damage (`damage_halves`, `shr ax, 1`), and its
key is "Press button or enter to continue" (`DS:0868`). Its saving throw
(`638c:10f8`) is not ported yet: a `DAMAGE` with a save stops there.
`ADD EP` (`3632:25f1`) passes over a character that cannot act before its
classes divide the points (`gains_active_first`), so no record divides by
zero but one that can act; its words are "congratulations..." and the
name and " gains experience!" (`DS:1485`, `2a64`) and, for the party,
"Congratulations the party gains experien" (`DS:2a3b`, a string[40] that
cuts it); and after the experience whether one may train is set
(`65cd:38c7`, `COK_FIELD_MAY_TRAIN`, as the end of combat does:
`cok_treasure_may_train`). For the selected character (first operand 0)
the original tests the `+0xc8` (can act) of a pointer it reads from the
stack before it stores the selection there (`[bp-7]`, left by the opcode
before: `LOAD CHARACTER`'s frame, which comes before 16 of the scripts'
18 such `ADD EP`s); the port takes the selected character's.

`tests/test_treasure.c` checks these on a Death Knights game (and
`DAMAGE`'s rules on a copy of Champions' descriptor, its saving throw
being Champions'), `tests/test_death_knights.c` the section against the
original's code, `tests/test_death_knights.py` the scripts' sites
(block 2's treasure, ECL3 block 80's `TREASURE ITEMS`, ECL1 block 36's
`STOCK` and `ADD EP`, block 35's vault), and
`tools/dosbox/checks/dkk_treasure.py` the original (see Checks against
the original).

## Shops and the temple

`src/shop.h` ports the shop (overlay `36d0`), the temple (overlay `340d`)
and Appraise (`58e7:1929`), which `COMBAT` opens instead of a battle: a
shop when a script has set var `0x7f6c` (`SAVE 1 [7f6c]; ... TREASURE ...;
COMBAT`), the temple when it has set var `0x7ee2`. A shop's stock is the
pool's items, which `TREASURE` put there; the shops are Throtl's armoury
(ECL1 block 17 at `8782`), the caravan of ECL1 block 16 (`89be`), the magic
shop and the weapon smith of ECL2 block 50 (`8b85`, `8ceb`) and two of ECL3
block 80 (`8f03`, `9458`); the temples are Throtl's (ECL1 block 17, `87ac`)
and the pilgrims' of ECL1 block 16 (`8a94`). `eclplay` logs what is paid
and appraised as `shop:`.

Both set the mode to 1, which `COMBAT` replaces with the area's when they
end, and redraw the screen for it (`6346:2c17`): the adventure frame
(`1128:0242`) but for this first redraw (`DS:883c`, set after it), the
small picture's first frame at cell 3, 3 (`6961:000a` of `DS:6da8`), or in
3D areas the head and body of the portrait last shown (`DS:4b55`, `4b56`;
`6961:05b9`, `06bd`, see Playing scripts), then the party
list and the status line. Both empty the pool's coins (`DS:6b0c`), which
are not emptied on leaving, and the shop names the pool's items
(`6346:0488`), which stay until the next `CLEARMONSTERS`. The menu on row
24 is `Buy View Take Pool Share Appraise Exit` with money in the pool,
else `Buy View Pool Appraise Exit`, or with `Heal` for `Buy` in the
temple; the prompt is empty, the items white and light green, keypad
digits are directions, and the selection is kept from the menu before
(`DS:6e0f`). Special keys count as the letters of their scan codes, as in
the treasure menu: F8 is `B`, shift-F3 `V`, shift-F1 `T`, Del `S` and F7
`A`; only the down arrow, `P`, is told apart from Pool and picks a
character, as the up arrow, `H`, does (`546c:3334`); in the temple `H` is
told apart from Heal too. Escape does nothing.

- `View` is View (see View), whose Items offer `Sell` and `Id` here.
- `Take` takes coins from the pool (`58e7:0d01`), `Pool` and `Share` are
  the treasure menu's (`58e7:0511`, `063a`; see Treasure and the end of
  combat).
- `Appraise` (below).
- `Exit` leaves when the pool has no money. Otherwise rows 17-22 say `As
  you Leave the Shopkeeper says, "Excuse me but you have Left Some Money
  here."  ` in light green and `Do you want to go back and get your
  Money?` in white after it, or in the temple `As you leave a priest
  says, "Excuse me but you have left some money here" ` and `Do you want
  to go back and retrieve your money?`, both light green and each
  clearing the window, so that the first goes at once; then `~Yes ~No`
  (`3775:1885`, the selection kept): No leaves, the money staying in the
  pool, Yes clears rows 17-22 and goes back.
- After `Buy` and `Take`, and `Appraise` when it showed anything, the
  screen is redrawn; after every key the party list.

`Buy` (`36d0:0484`) clears the frame (`1128:0000`) and lists the pool's
items, `Items: ` and `Buy` in cells 1-38 by 1-22 (`36d0:004e`, `67b5:1368`,
the menu selection cleared first and the first item picked on entry),
until Escape or `Exit`. Each line is 30 wide: the price right-aligned,
and over it the item's name; it is written into the item's name, which
the pool's items are named again after the list, so the line's bytes
past the new name stay, in the pool and in the copies bought. An item
worth 0 (`+0x3a`) is first made worth 1, for good. The price is the value
scaled by the price factor, var `0x7f6d`: 1, 2, 4 and 8 divide it by 16,
8, 4 and 2, 0x10 keeps it, 0x20, 0x40 and 0x80 multiply it by 2, 4 and 8,
as words. The item picked is bought by the selected character if its
money in steel covers the price (`546c:3424`: silver, copper, bronze,
platinum and steel worth 1, 5, 10, 25 and 50 fiftieths of a steel piece,
the low word of the sum over 50); its coins then become all steel, that
less the price (`58e7:0155`), the weight not changed. Otherwise the pool
pays if its worth in steel does (`58e7:00d3`, `018f`), else `Not enough
Money.` (`6346:1827`). Bought, a copy goes after the character's items
(`36d0:034c`) unless it would be overloaded (`OverLoaded`, nothing paid),
the shop keeping its own, and `NAME buys a ITEM` shows on row 24, or
`NAME buys ITEM` for an item with a count (`+0x39`).

`Appraise` (`58e7:1929`) values the selected character's gems and
jewelry (`+0xf5`, `+0xf7`: `COINS` 5 and 6), or says `No Gems or Jewelry`.
It clears cells 1-38 by 1-22 and shows the character's name on row 1
(`6346:199d`), `You have a fine collection of:` on row 7, and `N Gem`, `N
Gems`, `1 piece of Jewelry` or `N pieces of Jewelry` on rows 9 and 10, all
white, with `Appraise : ` and ` Gems Jewelry Exit` (the leading spaces as
given), as it has them. Picked, a gem goes from its coins and is valued on a
d100: 1-25 5 steel, 26-50 25, 51-70 50, 71-90 250, 91-99 500 and 100 2500; a
jewel on a d100 and then Turbo Pascal's `Random` (through Reals, which
change nothing): 1-10 50-499, 11-20 100-599, 21-40 150-899, 41-50
250-1499, 51-70 500-2999, 71-90 1000-3999 and 91-100 1000-5999. `The Gem
is Valued at N steel.` (or `Jewel`) shows on row 12, and `You can : `
offers `Sell Keep`, or `Sell` alone if the character has 16 items
(`+0x142`, `ITEM_COUNT`) or one more would overload it (`58e7:006d`). `Keep`
makes it an item, type 0x2f, named `Gem` or `Jewelry` (part `+0x31` 0x7a or
0x7b), worth the value and weighing 1, after the character's items; any
other key sells it for the value in steel (`58e7:01f2`, what would overload
the character going to the pool with `Overloaded.  Money will be put in
Pool.`). The stats are recomputed (`6346:0d20`) after each key, and it
ends when none are left, or on `Exit` or Escape.

The temple's `Heal` (`340d:0be1`) clears row 24 and shows, in the frame
of a large picture (`1128:0344`), `NAME, how can we help you?` on row 1
in white and the cures (`DS:01cc`, 41 bytes each) in cells 2-38 by 4-15,
with `Heal Exit` (`67b5:1368`), for the selected character, until Exit or
Escape; then the screen is redrawn. The frame is drawn once (`340d:0c81`)
and the list only the first time; the cures' texts and questions go below
it (rows 17-22 and 24), so after a cure, paid for or not, all ten rows
show again, the one picked highlighted (as in DOSBox-X after Raise Dead
bought, `c_outpost.py`). A cure (`340d:027f`-`0b32`) that
the character does not need says so (the name on row 18 and the text
below it, `6346:1883`, no pause) and
asks `cast cure anyway: ` (`67b5:177f`, No first). Then `CURE will only
cost N steel pieces.` in rows 17-22 and `pay for cure `: Yes pays as Buy
does, from the character's money, else the pool's, else `Not enough
money.`; paid, the character `is cured.` (with a pause), whatever the
cure then does, and only then are the dice rolled:

| Cure | Steel | Not needed | Once paid |
| --- | --- | --- | --- |
| Cure Blindness | 500 | `is not blind.` (no 0x21) | 0x21 goes |
| Cure Disease | 500 | `is not Diseased.` (none of 0x1f, 0x22, 0x2b, 0x2c, 0x32, 0x39) | while curing (`DS:6b38`) those go, the first of each, and strength is recomputed (`60f4:1743`) |
| Cure Light, Serious, Critical Wounds | 50, 175, 300 | | 1d8, 2d8 + 1, 3d8 + 3 hit points (`60f4:21ea`) |
| Heal | 2500 | | up to the maximum less 1d4, none if within it; 0x21, the diseases (not while curing) and 0x44 go; intelligence and wisdom are worked out, which keeps nothing |
| Neutralize Poison | 500 | `is not poisoned.` (no 0x37) | at least a hit point; while curing 0x37, 0x16 and 0x0f go; okay and able to act |
| Raise Dead | 2750 | `cannot be raised` (race 0 or 1, the elves), else `is not dead.` (status 6 or 1) | while curing 0x20 and 0x37 go; 1 hit point, okay, able to act, a point of constitution less; see below |
| Remove Curse | 1750 | `is not cursed.` (no cursed item, `+0x36`, nor 0x24) | the spell's handler (`5b04:35f5`) |
| Stone to Flesh | 1000 | `is not stoned.` (status 7) | okay, able to act, 1 hit point |

Raised with a constitution left of 14 or more, a character's maximum hit
points (`+0x62` `MAX_HIT_POINTS`) above those at full (`+0x11b`
`BASE_HIT_POINTS`) are divided by a byte sum over its class levels
(`+0xf9`-`+0x100` `CLASS_LEVEL`): each level, two for one above 15, a
fighter's times the constitution less 14; they come off the maximum unless
the constitution is 17 or more, it has no fighter level and `+0xfb`
(`CLASS_LEVEL` 2) is not above `+0xd7` (`CHANGE_LEVEL`).

View's Items in shops and the temple (mode 1) add `Sell`, as Trade allows
it, and `Id`. `Sell` (`546c:2822`) needs the item unreadied, as Drop does,
and offers half its value, or for a count above 1 that times the count,
a word, over 20, in yellow on rows 21-22 (`I'll give you N steel pieces
for your ITEM`), and asks `Is It a Deal? `; Yes says `Sold!`, the item
goes (`6346:1697`), and the price goes to the character's steel as a
word, or what would overload it (`58e7:006d`, the item still weighed) to
the pool's steel with `Overloaded.  Money will be put in pool.`, without
a pause. `Id` (`546c:2a59`) offers `For 100 steel pieces I'll identify
your ITEM`, readied or not; Yes pays 100 as a cure is paid, or says `Not
Enough Money`; paid, an item with hidden parts (`+0x35` bits 0-2) shows
them, `It looks like some sort of ITEM`, else `I can't tell anything new
about your ITEM`, and either pauses.

The port keeps these quirks:

- A price factor not in the table shows the value in the list, but Buy
  charges the word its stack holds there, the colour 15 that the shop's
  menu pushed (`36d0:08b2`): 15 steel. The weapon smith of ECL2 block 50
  (at `8ceb`) sets no factor, so before any other shop has, var `0x7f6d`
  is 0 and everything costs 15. An emulator run agreed.
- Money in steel is a word in Buy and the temple, so a character worth
  65,536 steel or more is judged by the rest; paying turns all of coins
  0-4 into steel, losing what is less than a steel piece, and the pool's
  remainder is a word.
- Buy and the shop's names leave a line's characters in the bytes past a
  name, and make items worth 0 worth 1.
- Appraise takes the coin before the overload check, which still counts
  its weight; Escape, special keys or any other key sells; and `Keep`
  with no items links the new item to nothing (`58e7:1df5`, `213c`), so it
  is lost with its coin.
- The temple charges a cure not needed when asked to cast it anyway;
  Neutralize Poison revives the dead who are poisoned; Heal removes a
  drain (0x2b) without curing, so its handler adds it again; Remove Curse
  leaves the item cursed, to be paid for again; Raise Dead's sum wraps,
  and at 0 takes the whole bonus; Stone to Flesh leaves 1 hit point.
- `Is cured.` is said before the cure, whatever it does.
- Heal's first pick is the word Heal's stack holds: the flag 0 that the
  temple's menu pushed (`340d:0f47`), so the first cure. An emulator run
  agreed.
- A stack of 2 sells for about a tenth of one; Id charges for telling
  nothing new.
- In Trade from View, Escape at the first coin list tests a byte of the
  stack (see View): on entering a shop or the temple it holds what the
  run left, and the port stops with `COK_ECL_UNDEFINED`; a View before,
  Yes to going back, Appraise showing the gems and, in the temple, Heal
  set it, so Trade is left; Buy, Take, Pool, Share and picking a
  character keep it. The temple's locals are 14 bytes fewer than the
  shop's, so every call from its menu lands 14 bytes higher and the same.
  An emulator run of each, in the shop and in the temple, agreed. In
  DOSBox-X (`e_trade.py`) the outpost's armoury entered from the
  outpost's menu with nothing before held 0 in 7 runs of 7, so Trade asked
  for a partner again; other histories were not tried.

A differential test ran the original routines in an 8086 emulator against
the port on random parties, items and pools: Buy's lines and a purchase
(`36d0:004e`, `0484`, with every factor, the stack word set as the menu
leaves it), Appraise (`58e7:1929`, with its keys), each cure with its
payment (`340d:027f`-`0b32`, `00f2`, `0045`), the money in steel
(`546c:3424`), Sell and Id (`546c:2822`, `2a59`), comparing the records,
items, effects, pool, texts and random numbers drawn. Of 12,000 cases, the
11,686 the port carries out agreed. Both refused 201: no character
selected, which the original reads through NULL (163), Buy given an index
past the list, which it cannot pick (36), and an effect whose handler
needs combat, and keys that ran out (1 each). The port alone refused 15,
removals whose handlers need combat or text (0x1f, 0x22, 0x2b and 0x2c),
which the original's carried out on random data; and 98 were left out,
offers of Sell and Id too long for their two rows, which page and wait for
a key in both, but which the emulator's text routine does not wrap. The
test found the item that Appraise's Keep loses. It is not part of the
repository. The screens were tested in the port only.

### Death Knights' shops

Death Knights' shop (`3b89:06f3`), Buy (`0453`), Appraise (`5c72:0dc2`)
and temple (`3999:07d3`) are the descriptor's `shops` section's routine
(`COK_SHOPS_DEATH_KNIGHTS`), in its own words (`DS:34f8` on); Champions'
above are `COK_SHOPS_CHAMPIONS`. `COMBAT` opens them as Champions' does
(var `0x7f6c`, `0x7ee2`), and `STOCK` opens the shop (see Death Knights'
treasure); the prices' factor is var `0x7f6d` (`DS:85c`). ECL1 block 36,
the outpost of Gargath, has the weapons store (its square 6, `90da`:
`STOCK 1` at 16) and the temple (square 3, `865c`, reached facing a wall,
var `0x4c06`).

- The shop: as Champions', but its mode put back at the end (`DS:8c4f`;
  `COMBAT` sets its own after, `STOCK` does not); its menu by index
  (`6a69:0f1b`, keypad on) with its own selection from the first, Escape
  as Exit, up and down (`H`, `P`) picking a character (`580d:2bb2`), the
  index dispatched as it comes (a special key's scan code too); Buy, Take
  and Appraise redraw the screen, and Pool once an Appraise has set the
  flag it shares (`[bp-8]`; before one, what the stack held: the redraw
  then draws the screen as it stands); leaving money asks "as you leave a
  man says, ..." (light green, clearing) and "do you want to go back and
  get your money?" (white) from Yes (`DS:1758`, `17a9`, `57c0`).
- Buy: on the frame, the funds under the list ("Personal funds:" and
  "Pooled Funds:  ", the selected character's steel and the pool's,
  right-aligned to 10, white on rows 20 and 21, `3b89:0000`), the lines as
  Champions' (`00b1`) but a price times a count above 1 and at least 1, by
  the list `6a69:1b82` in cells 1-38 by 1-18 with "items : " and "Buy",
  the selection cleared first, until Escape or Exit. The price is the
  factor's (the same table, times the count, at least 1); a factor not in
  the table keeps the price set last, the stack's word at first: the port
  stops there (`COK_ECL_UNDEFINED`). It is paid from the selected
  character's steel (`+0x3f`) and what that lacks from the pool's
  (`DS:be58`, a LongInt; `shops.steel`, `cok_pool_pay_steel`), or "not
  enough money." (`DS:1743`); the copy is taken as treasure's are
  (`3b89:033b`, which joins a counted item to its kind), "NAME buys a
  ITEM" or "NAME buys ITEM" (`DS:1738`, `172d`). Id pays the same way.
- Appraise: as Champions' with its words ("no gems or jewelry", "gems :
  N" and "jewelry : N" on rows 9 and 10 whatever they are, "The gem is
  valued at N steel."), its menu "Gems Jewelry Exit" by index with the
  keypad and Escape as Exit, "Sell Keep" by index without a prompt or the
  keypad; what a sale would overload goes to the pool with the items
  menu's word (`5c72:00ba`, `DS:1188`).
- The temple: as the shop, but with `Heal` for `Buy` (`DS:3594`), the
  selection as the menu before left it, the mode not put back, the pool's
  items not named, Take redrawing and Appraise when it showed any, leaving
  money's words the shop's, both light green and clearing, from No. Its
  View is given `DS:c0f8` for its flag (`580d:0c66`'s parameter), which
  every menu by index sets (`game->menu_special`): View, and the Items it
  passes the flag to, end at the first menu read that took a special key
  (in View's menu Escape or another that is not a move, as Up; Items'
  Exit or Escape, a pick's Escape, a coin list's Exit, Spells' list,
  `5dc5:016e`, which ends only so), so that Escape from Items leaves View
  too; Left and Right move and read on (see Library's menus by index).
- Heal (`3999:04e2`): row 24 cleared, a big picture's frame, then for the
  selected character its name, hit points (white at the maximum, else
  light magenta) and "/" and the maximum on row 19, the name and ", how
  can we help yo" (`DS:1aa6`, a string[20] that cuts it) on row 1, and the
  cures, the names of the spells `DS:030c` lists (`shops.cures`: Cure
  Blindness, Cure Disease, the three Cure Wounds, Heal, Neutralize Poison,
  Raise Dead, Remove Curse, Stone to Flesh, Restoration), by the list
  `6a69:1b82` in cells 2-38 by 3-15 with "Heal", until Exit or Escape;
  then the screen redrawn. The cures are free, without "is cured.": Cure
  Blindness takes 0x21 (`0053`); Cure Disease does nothing (`00ab`); the
  wounds and Heal heal as Champions' do (`00b0`), Heal then taking 0x21 and
  working out intelligence and wisdom (no diseases, no 0x44); Neutralize
  Poison and Stone to Flesh are Champions' (`02a0`, `0404`); Raise Dead
  (status 6 or 1, not an elf) and Restoration (a class level below its
  level before a drain, `+0x45` against `+0x4d`) run their spells'
  handlers (`01fc`, `045f`: the spell set in `DS:be70` and its handler
  from `DS:c150` + 4 × id), Remove Curse (a cursed item or 0x24) its
  spell's routine (`0368`: `5dc5:311b`), each through `cok_cast_on`, which
  stops the run citing a handler not ported yet. One that does not need a
  cure is asked "cast cure anyway? " from No (`3999:0000`), in the words
  `DS:19fe`-`1a91`.
- Mode 1's redraw (`65cd:2db0`) is mode 7's: the frame unless `DS:db62`
  is clear (the shops' first redraw), the small picture always, no
  portrait (`shops.portrait`), the party list and the status line.

The Champions temple's diseases, its prices and its Raise Dead's hit
points (`shop.c`'s `CHANGE_LEVEL` read), and the effects it names that
Death Knights has not (0x20, 0x44), are Champions' routine's alone.

`tests/test_shop.c` checks these on a Death Knights game,
`tests/test_items.c` its Sell when overloaded and Id's two waits,
`tests/test_death_knights.c` the section against the original's code
(the words where their routines load them, the Yes/Nos' first, the mode
kept, the steel, the cures' ids and names, the handlers the cures run,
mode 1's redraw), `tests/test_death_knights.py` block 36's store and
temple, and `tools/dosbox/checks/dkk_shops.py` the original (see Checks
against the original).

## The start menu

`src/start.h` ports the game's front end: main's loop (`1000:0134`), the
title (overlay `2f55`), the state the game starts and ends with
(`3e99:005b`, `3e99:0843`) and the start menu (`4def:01b4`), with the
roster in `src/roster.h`. `eclplay --start-menu` plays from startup
(`cok_start_game`).

Startup shows `Loading...Please Wait` in light green on row 24, cleared
first, once the font is loaded (`3e99:06a4`), while the tile sets, the
sky, the combat sprites and the item types load; nothing erases it but
the title's pictures, or after `Woof` the title's menu over it.

Main runs startup, then the title unless the first argument is `Woof`
(`ParamStr(1)`, exact case), then the title's menu: `Champions of Krynn
v1.2` in light magenta at column 0 of row 24 and `Play Demo` straight
after it (`67b5:03e2`, white and light green, digits not directions), with
`DS:6e11` 3000 hundredths: if no key comes in 30 seconds the menu returns
`DS:6e15`, `D`, and the demonstration plays (`DS:4b4b`). Any other key
that leaves the menu plays the game; F10, whose scan code is 0x44, `D`,
plays the demonstration too, as the special flag is not tested. Arrow
keys do not restart the time; past midnight `GetTime` is lower than the
start and the menu never times out. Then, game after game: the ECL file
is 1 (`DS:5782`), or 2 with speed 9 for the demonstration; the start menu
runs until Begin Adventuring, then main draws the frame (`1128:0242`, or
`1128:0344` in mode 3 without `0x4c38`; Death Knights `110f:0162` always,
the frame section's `menu_big`) and the party list again, and the
adventure (`2fd3:3c28`) runs from var `0x4bf2`'s block, or 0x24 when it
is 0 (a new game: Solace). Quitting to DOS in it (camp's `Quit TO DOS`,
Ctrl-C) halts the program there (`1614:0000`). When it ends (`DS:4b57`:
the party killed or destroyed), every member is removed (`4def:3b0a`), the end of the game
(`3e99:0843`) resets the state, and the start menu opens again; after a
demonstration the title shows again, then the menu with `Champions Of
Krynn v1.2` (capital O) for 10 seconds. A `ParamStr(2)` compare against
`Helm` after the start menu is discarded, and the code wheel question
after it (`33c7:0131`, `1000:0218`) is never reached: nothing jumps there.

| Function | Original |
| --- | --- |
| `cok_start_game` | `1000:0134` |
| `cok_start_title` | `2f55:063c`, `0000`, `006d`, `0267`; `1128:0130` (`COK_SCREEN_CREDITS`) |
| `cok_start_startup`, `cok_start_reset` | `3e99:005b`, `3e99:0843` |
| `cok_start_menu`, `cok_start_command` | `4def:01b4` |
| `cok_start_pick` | `546c:36cc` |
| `cok_start_mouse` | `4def:5cc7`, `638d` |
| `cok_roster_list`, `cok_roster_add`, `cok_roster_drop`, `cok_roster_remove` | `4b6d:0453`, `008b`; `4def:37a2`, `2668`; `4def:01b4` R |
| `cok_roster_save`, `cok_roster_erase`, `cok_roster_load` | `4b6d:0bed` with no base, `0a80`, `1b34` |

The title (`2f55:063c`) frees the small picture, then draws `TITLE.DAX`
records 1 (the SSI splash) at 0, 0, then 2 at 0, 0 with 3 (240 by 88) at
unit 5, cell 1, then 4 (320 by 88) at 0, cell 1, opaque (`127f:0111`,
`10e7`), waiting 5, 10 and 10 seconds after them (`2f55:0000`: the
keyboard flushed, then until a key is pressed or the time passes, then
flushed again, so the key is thrown away; the port polls `key_pending`
each hundredth, as often as `GetTime` changes, and waits 10 ms between
through the `delay` hook). A key ends the whole title.
Otherwise the screen is cleared, the credits show for 10 seconds, and it
is cleared again (`2f55:006d`). The credits (`2f55:0267`) are thirty
strings in purple (5), yellow (14) and light red (12) inside three boxes
of the frame tiles, rows 0-4, 4-20 and 20-23, without moons
(`1128:0130`). The title's music plays from its start to its end
(`1661`, see Sound).

Startup and the end of a game leave the variables cleared but for the
difficulty (`0x4cf4` 3), a 3D area (`0x4be6` 1) and the moons in phase 2
(`0x4cf9`-`0x4cfb`); the party at 7, 13, facing north at startup and
south after a game (`DS:6d85`-`6d87`); the speed 4, the mode 4, no party
or selection, the ECL file 1, the first row a list shows 1 (`DS:6e0d`),
the menu's selection 1, the game unsaved (`DS:5885`) and the overhead map
off. `eclplay` without `--start-menu` keeps its own state (row 0 first).

The start menu (`4def:01b4`) sets the mode to 0 (the party list from
column 1, `6346:07ba`), keeps the mode it was entered with, turns free
training off (`DS:7140`) and starts on its first row. Each pass draws the
open frame (`1128:0000`), the party list if a character is selected, and
lists in cells 9-38 of rows 12-22 the items of `DS:0878` (13 string[40]s
with a flag, 0x2a bytes apart) that the flags allow: `Create New
Character`, `Add Character to Party` and `Exit to DOS` always; with a
character selected `Drop`, `Modify`, `View`, `Remove Character from
Party`, `Save Current Game` and `Begin Adventuring`, and `Train
Character` and `Knight Change Classes` while var `0x7ea8` is set (a
training hall) or free training is on; with none, `Load Saved Game` and
`Initialize Mouse/Joystick`. The list (`67b5:1368`) has the prompt
`Choose a FUNCTION ` and `Select ` (with its space), no Exit, rows in
light green and the pick black on white, prompt in light magenta; with 11
rows at most, there is no paging. The command is the first letter of the
row picked, not the key: Select, Enter or space take it, and Escape,
which leaves no row, reads the byte at `0000:0001`, the high byte of
Turbo Pascal's INT 0 vector (0x00df), so 0: nothing. Every command but
Save and Exit, and Escape, marks the game unsaved (`DS:5885` 0).

| Letter | Command |
| --- | --- |
| C | Create New Character (`4def:06dd`, see Creating characters) |
| D | Drop Character (`4def:2668`) |
| M | Modify Character (`4def:28fa`, see Creating characters) |
| T, K | Train Character (`4def:4d9e`), Knight Change Classes (`4def:5812`), see Training |
| V | Pick Character, then View (`546c:0d74`, see View) |
| A | Add Character to Party (`4def:37a2`) |
| R | Pick Character; a player character is saved to the roster (`4b6d:0bed` with no base) and leaves the party (`4def:3b0a` 0, 1) whether it was saved or not; an NPC is dropped (`4def:2668`, which picks again) |
| L | Load Saved Game (`4b6d:1b34`) |
| S | Save Current Game (`4b6d:22de`, see Camp), with no rule-book question |
| B | Begin Adventuring |
| I | Initialize Mouse/Joystick (`4def:5cc7`) |
| E | `Quit to DOS ` (yellow); Yes, with a party and the game unsaved, asks `Game NOT saved.  Quit anyway? `: No saves (`4b6d:22de`) and stays in the menu, Yes quits |
| J | free training, if the game was started with `Helm` (`ParamStr(2)`): toggled, `Free training on` or `off` on row 24; no row begins with J, so it is never reached |

Pick Character (`546c:36cc`) shows `Pick Character ` (light magenta) and
`Select Exit` on row 24; special keys pick a character (`546c:3334`) and
redraw the party list, and it ends on Select (true), Exit or Escape;
space and other keys are ignored.

Begin Adventuring needs a party (or the demonstration, which never comes
here): the mode comes back as it was; the frame (`1128:0344` in mode 3
without `0x4c38`, else `1128:0242`) and the party list are drawn, unless
a game was loaded here (`DS:4b52`) and the small picture is not PIC 9 and
it will draw them (var `0x4bf2` set and `0x4c38` clear; Death Knights
draws by the mode instead, `49cb:010b`: 0 and 4 the adventure frame and
the party list, 3 the big picture's frame, the frame section's
`resumed`); row 24 is
cleared and var `0x7ea8` cleared, which closes the hall. From `PROGRAM 0`
the script goes on (see The overland map). A game loaded in the start
menu opened by a script replaces the variables and the party, but the
script's block stays loaded and goes on after `PROGRAM`, as in the
original.

Initialize Mouse/Joystick (`4def:5cc7`) lists `mouse ON` or `OFF`,
`joystick ON` or `OFF`, as they are, and `disable both` (`Init
Mouse/Joystick: `, `Select Exit`, in the same window) until Exit, Escape or
space. Each pass reads `KRYNN.CFG`'s first five lines (string[255]s, empty
without the file) and each Select writes the file again with the third line
and the joystick's 14 numbers (without a file to read, `Reset`'s error,
never checked, stops every `Write` after the `Rewrite`: the file is left
empty, and the error stays for the game's next unchecked I/O, which the port
does not model). Select on the mouse turns it over: on, its
driver is reset and set up as at startup (`DS:8985` set, the joystick off,
`M` and zeros; the pointer is not moved, staying at 639, 100, while the
click's place is set to the middle); off, or no driver, `N` and zeros, the
cursor hidden for good (the flag set for the hide, then cleared). Select on
the joystick turns it over: on, the file is emptied first and the stick
calibrated in the window (`4def:638d`: the window cleared, `Push JOYSTICK
#1 to the extreme upper` and `left position and press button #1.` in light
magenta at rows 3 and 5, then `lower` and `right`; `Unable to read
joystick` for 2 seconds on failure, see The mouse and the joystick); still
on (a stick found, calibrated or not), `J` and its thresholds, the mouse
off; else `N` and zeros, the mouse as it was. `disable both` turns both off
and writes `N`. The calibration's text stays until the start menu draws its
frame again. The port writes the file (`cok_config_rewrite`, logged
`config: line 3 M`, `J` or `N`) where the setup would (see Startup and the
setup): read from the assets, it is written to the saves directory, which
later runs read first.

The demonstration (`DS:4b4b`): main sets the ECL file 2 and speed 9 and
skips the start menu; `2fd3:3c28` loads ECL2 block 0x39 and runs only its
load vector, which is the whole demonstration (three NPCs joining, walks,
a fight, the dragon's close-up), with no party list, then removes every
member (`4def:3b0a` with 1, 1) and returns. Meanwhile a key read with
none waiting returns 0 (`1614:025b`), so `PRESS ... TO CONTINUE` and
paging do not wait; `CLEAR BOX` does not redraw the small picture
(`2fd3:3086`); and the end of combat skips the party's part and stops
once the enemies are removed (see Treasure and the end of combat), with
no `Continue Battle:` (`3995:0c78`). Reads straight from `1614:025b`
(`PROMPT KEY`, paging in the combat panel too) do not wait either; the
menus, which read only once a key is pressed (`67b5:03e2`), do. Not
ported: the
line input ending after one key (`1521:0739`), which block 57 does not
use. No key ends the demonstration.

The port keeps these quirks:

- The command is the row's first letter: Escape is 0, and free training
  (J) is unreachable.
- Escape, like any command but Save and Exit, marks the game unsaved.
- No at `Game NOT saved.  Quit anyway? ` saves and returns to the menu.
- Pick Character cannot be left with space.
- The title's menu takes F10 as `D`.

### Death Knights' front end

Death Knights' (the descriptor's `start` section; main is `entry`, the
title `35c6:051c`, the start menu `49cb:010b`) differs from Champions'
here:

- The title: `TITLE` 1 on a cleared screen for 5 seconds, 2 on a cleared
  screen for 5, 3 at row 11 for 5, 4 there for 20; then the credits
  whether a key ended the pictures or not (`35c6:026e`: the screen
  cleared, the frame, twenty lines in yellow, white and light green), for
  10000 seconds' hundredths multiplied into a word (`35c6:0000`'s `mul`
  then `xor dx, dx`: 16960, 169.6 seconds) unless a key. No `Woof`: main
  reads no argument, and the game has no debug mode (`saves.debug`
  `NULL`): `cok` and `eclplay` refuse `--woof` for it (status 2). Its music
  (`MUSIC.DAX`) is not read yet: `sound: no music: Death Knights of
  Krynn's music is not ported`, the effects as they are. The title's menu `Death Knights Of Krynn 1.10 ` and
  `Play Demo`; the demonstration would play ECL5 block 2 (`3632:2e06`),
  which the game does not ship, and stops when it cannot load it.
- A new game starts in block 0x10. After the start menu main draws the
  adventure screen always (`1000:01d1`, see Games).
- The start menu's items (`DS:0cf8`) are Champions' but `Knight Change
  Order`, under no prompt and `Select`; the command is the row's place
  (a compare of its text against the items'), which is the first letter's
  item, all being different. The questions (Drop's, Exit's, Overwrite)
  are in light magenta (`6a69:2048`), and their words are its own (`drop
  NAME forever `, `NAME is dumped out back.`, `NAME breathe a sigh of
  relief.`, `game not saved. quit anyway? `, `overwrite NAME? `, `new file
  name : `); Pick Character has `Select` alone (`580d:3067`).
- Its menus are read by index (`6a69:0f1b`, `cok_camp_pick_menu`), so a
  special key's scan code is no letter: Pick Character (`580d:3067`:
  items 0 and 1 of `Select`, the keypad, no picture) takes `S` or Enter,
  Escape is its Exit (index 1, not shown), `E` does nothing and other
  special keys pick a character. `load from where? ` and `add from where? `
  (`46b2:1ef0`, `49cb:343c`: the three of `DS:3337`, no keypad) take an
  index or Escape. Any other special key: Load goes on with a path it
  never set (`[bp-0x326]`), which stops the run here, citing it; Add
  (`49cb:34a9`) goes on with whose files `DS:c468` names
  (`game->own_files`), as the last Save (`46b2:2864`), Load (`1f31`,
  `1f78`), Add (`3490`, `349b`) or roster write (`0ad6`) left it. Before
  any of these it was never set: it lies past the image EXEPACK unpacks
  (`0x141a` paragraphs, DS's data ending at `DS:84c0`) and System's
  startup (`1a07:0000`) clears nothing, so that stops the run too. `load
  which game: ` (`46b2:207a`) offers by index those of `A`-`J` that are
  there (`DS:c120`, from the menu before's selection); Escape leaves, an
  index is its letter. Save's `save which game: ` (`46b2:2808`, from the
  start menu or camp: the ten of `DS:33e0`, no keypad, the picture in
  camp) reads special keys past but Escape, which saves none (F7 is no
  longer A).
- The mode the menu was entered with is kept in the last mode (`DS:8c4f`),
  which Begin takes back; a load sets it to the saved game's last mode,
  so a loaded game begins in the mode it was saved from.
- A Drop or a Remove that leaves no party ends the game there
  (`4f9a:08ad`, `DS:8c5b`): the end of a game's state (whose wall table
  loop, Champions' for slots 1-3 on a table of slots 0-3, sets the second
  entry and keeps the first), the vault's letter forgotten, the list on its
  first row; the adventure, once the menu returns (from main, or from
  `PROGRAM 0` in a script, which then does not redraw), starts over from
  a new game's block (`3632:2e06`'s outer loop). Meanwhile no script runs
  on: the ECL's run loop (`3632:2cd5`) stops on `DS:8c5b` as on its other
  two flags, so the script that opened the menu ends at `PROGRAM` and every
  vector after it returns at once, until the game starts over (the VM's
  `halt`, which `cok_ecl_run` tests; Champions' loop, `2fd3:3add`, tests
  only `DS:43b8` and `4b57`, and nothing sets `halt` there).
- Exit to DOS: No to `game not saved. quit anyway? ` saves and then quits.
- Add Character to Party asks `add from where? ` (`DS:1393`): Death its
  roster (`*.PC` in the saves), Champions Champions' (`*.WHO` in
  `--import`), each by its own NPC flag (`DS:57a`: Champions' `+0xe7`,
  not its jewelry) and record size (`DS:57e`), imported
  (`cok_character_import`); `add a character: ` and `Add`; the first joins
  with no checks, then a duplicate (name and `+0x61`), a seventh player
  or ninth record, a fourth ranger (`too many rangers in your party`) is
  turned away and keeps its `* `; no paladin rule; no `Loading...Please
  Wait`; a space is not special.
- Initialize Mouse/Joystick (`52cd`: its own words, and it rewrites
  `krynn.cfg`, not `DKK.CFG`) is not ported: choosing it stops the run
  (`not yet ported: Death Knights of Krynn's Initialize Mouse/Joystick
  (dkk 49cb:52cd)`). Create (`05c7`) and Modify (`25d4`) are Death
  Knights' creation's (see Creating characters); Train (`43b6`) and Knight
  Change Order (`4ea4`) are Death Knights' training's.

`tools/dosbox/checks/dkk_start.py` (not in `run_all.py`, as
`dkk_views.py`) compares the original in DOSBox-X: the four title
pictures, the credits, the title's menu, the start menu with and without a
party, Load from Death Knights and from Champions (their bank set), Save,
Begin, the first texts and camp, Remove, Add, Drop and the game ended by
the last Drop, each screen whole; and the games saved (the saved game but
its stack's bytes, the vault, the characters but their pointers).

### The roster

Characters wait between games in the save directory (`DS:5784`, `--saves`)
as `NAME.WHO` (the 409-byte record), `NAME.STF` (items) and `NAME.SFX`
(effects). The list (`4b6d:0453`, `008b`) holds each `*.WHO` file of
exactly 409 bytes whose byte `+0xf7`, the jewelry's low byte (`COINS` 6), is
below 0x80 (surely meant to be the NPC flag `+0xe7`, `NPC`), named as its
record is, padded to 15 columns, unless a record in the list has that name;
DOS lists them in directory order, the port in the order of their file
names.

Add Character to Party (`4def:37a2`, nothing with no roster) shows the
list in cells 1-38 of rows 2-22 with `Add a character: ` (light magenta)
and `Add ` and `Exit`. Exit is offered by a byte the routine never sets
(`[bp-0x1c]`): from the start menu it holds 0x2e, the low byte of a
return address that `FreeMem` (`1a46:042b`) leaves there as the start
menu frees its list, so Exit shows; the game booted in an emulator with
Turbo Pascal's heap confirmed it in every history tried (see below). Add
(or Enter) on a row not marked `* ` loads the file (`4b6d:11e5`,
`Loading...Please Wait` on row 24 in light green) and marks the row. The
first character joins an empty party with no checks; after it, one joins
if no record has its name and `+0x115` (`MONSTER_ID`; a duplicate), it would
not be a seventh player character (or for an NPC, the party holds fewer than
8, `0x7f3e`), it is not a paladin while an evil character is in the party,
not a fourth ranger, and not evil while a paladin is. Joining counts it
(`4b6d:1989`) and builds its icon (`4b6d:0817`). A character turned away
loses its mark, and at most one message shows on row 24 (`6346:1827`),
whichever applies first of `paladins do not join with evil scum` (with a
second pause), `too many rangers in party` and `NAME will tolerate no
evil!` (the last paladin seen), whatever the reason was. Only the
rangers' rule can be met in play: no race may be a paladin, and no
alignment Create offers is evil (codes 2, 5 and 8). It ends on Exit,
Escape, or when 6 player characters or 8 records are counted. Add does
not erase the roster's files.

Drop Character (`4def:2668`) picks a character, asks `Drop NAME
forever? ` and `Are you sure? ` (yellow), and on Yes to both says `You
dump NAME out back.` (one who cannot act) or `NAME bids you farewell.`,
erases its roster copy and removes it; No says `NAME breathes a sigh of
relief.`. The party list is redrawn.

Saving to the roster (`4b6d:0bed` with no base) names the file
`cok_party_roster_name`'s base of the character's name with `.WHO`; if
that file is there, `Overwrite NAME? ` (yellow) asks, and No asks `New
file name: ` (light green, 8 characters, upper-cased) until one is typed,
which becomes the character's name too, and checks again. The record,
then the items and effects (each file erased first, written only if
there are any), are written as for a saved game (see Party). The roster
copy of a character (`4b6d:0a80`, by `cok_party_file_name` of its name:
`.WHO`, `.STF`, `.SFX`) is erased when it is dropped, when a game is
loaded from the start menu (every record) and after each character a
game saves (`4b6d:22de`).

Load Saved Game (`4b6d:1b34`) offers the letters A-J whose
`SAVGAM<letter>.DAT` is there (`Load Which Game: `, light magenta, digits
not directions), and does nothing at all with none; Escape cancels;
special keys count as their scan codes' letters (F7-F10, Home, up, PgUp
as A-D, G-I) when that game is there. Then `Loading...Please Wait` on row
24, the game is loaded (`cok_adventure_restore`, see Party) and the
roster's copies erased; the mode is 0 and the last mode the saved one.

The original's checks of the save disk (`4b6d:0555`: the volume label
`CHAMP` on a floppy, `Put save disk in X:`, creating the save directory
with `MkDir`), of free space (`Can't save.  No room on this disk.`,
`Lose character? `) and an error check after `Reset` that reads
`Dos.DosError`, which `Reset` does not set, are left out.

The port keeps these quirks:

- The list leaves out characters whose jewelry's low byte is 0x80 or
  more.
- The duplicate test needs the same name and `+0x115` (`MONSTER_ID`); a
  player character is not counted against 8 records, only 6 players.
- The message names the first rule that applies, not why the character
  was turned away; a duplicate or a full party says nothing.
- With a space as the first key of Add, before any choice, the original
  tests a count it never set (`[bp-0x1e]`), and Add ends when it is above
  5. It holds what `FreeMem` (`1a46:03d3`) left when the start menu freed
  its last row (`4def:0381`): the offset of the free block below the row,
  or the low byte of DS (0xc6 where DOS loads the game at a PSP of
  0xnnf0, as the emulator does; see Spells in combat) when the row joins
  that block. The game
  booted in an 8086 emulator with Turbo Pascal's own heap left 8 or 0xc6
  there in every history tried, so the space leaves Add, as in the port:
  the title seen or skipped at each wait, or `Woof`; the demonstration
  first; a character created and saved, a creation cancelled, the mouse
  screen or an Escape first; saved game A or B loaded; game A begun and
  the outpost's `HALL` (`PROGRAM 0`) opening the start menu; and the EGA,
  Tandy and CGA settings of `KRYNN.CFG`. Other heaps leave 0 and the
  space does nothing: an empty heap or two font blocks, and a later Add
  in the same visit to the start menu after an earlier one, since each
  Add leaks its list of file names (`4b6d:0453`, 0x30 bytes a file),
  which moves the start menu's rows. The port leaves Add every time. In
  DOSBox-X (`a_roster.py`: the title skipped with a key, Play, a character
  added, Escape, Add again and a space) the space left Add the second
  time too, the count above 5 there as well.
- Refusing to overwrite renames the character to the new file's name.
- The file names keep one of two separators in a row (`169c:050b`).

## Creating characters

`src/create.h` ports Create New Character (`4def:06dd`) with the knight's
items (`4def:5534`) and the default icon (`4def:3c61`), and `src/icon.h`
the combat icon editor (`4def:408b`, `3d41`, `3e20`), which camp's Alter
`Icon` also opens.

The new record starts zeroed with the icon's colours as the template's
(`+0x139`-`+0x13e` `ICON_COLOURS` 91 a2 b3 c4 e6 f7), armour class 10
(`+0x113` `BASE_AC`), THAC0 20 (`+0x59` `BASE_THAC0` 0x28), able to act, one
cell in combat, icon slot 10, and a `Random(256)` in `+0x115`
(`MONSTER_ID`), the first number drawn. Five lists follow, each in cells
1-38 of rows 2-22 under its heading in light magenta, the rows indented two
spaces in light green, `Select` and `Exit` on row 24 (`67b5:1368`), starting
on the first row; Escape or Exit discards the character. Pick Race
(`DS:1140`): a kender gets effects 0x5c and 0x5e, a dwarf 0x5e, 0x1a and
0x2f, an elf 0x5f, a half-elf 0x12, each permanent with value 0xff; dwarves
and kender are small (`+0x138` `ICON_SIZE` 1). Pick Gender. Pick Class, the
race's classes from `DS:3a50` (15 bytes a race: the count and the classes),
sets the levels and experience:

| Class | Levels | Experience |
| --- | --- | --- |
| Cleric | 1 | 0, then 2000 good or 1500 neutral |
| Fighter, Ranger, Mage, Thief | 2 | 2001, 2251, 5000, 1251 |
| Knight | 1, order of the Crown | 2500 |
| Cleric/Fighter, Cleric/Ranger | 1/1 | 2001, 2251 |
| Cleric/Thief | 1/1 | 0, then as a cleric |
| others | 1 each | 5000, Fighter/Thief 2001 |

A ranger gets effect 0x69. Then the thief skills (`66c2:0b9f`), the THAC0
and class bits of the levels and the saving throws (`66c2:08a6`) are set
with every score still 0. Pick God, for clerics (dwarves have only
Reorx): spells known by deity (Paladine 101, Majere 102, Kiri-Jolith 103
and THAC0 one better, Mishakal 104-106, Sirrion 107, Reorx THAC0 one
better for a dwarf, Shinare 106), and the alignments allowed, good for
deities 1-4, neutral for 5-7. Pick Alignment offers the class's
alignments (`DS:3c45`) the deity allows, lawful, true or chaotic good or
neutral (`DS:0a9a`); never evil. A mage gets order 1 (White) when good,
2 (Red) when neutral.

The age (`+0x60`, `AGE`) is a single class's base and dice (`DS:3ab9`, race
× 32 + class × 4), or for more classes the most one class's dice can give,
with no roll. The character is then selected and its sheet drawn
(`546c:00a3`). Then, and again on each reroll: the levels become 1;
each score is the best of six rolls of 3d6 + 1, in the order strength,
intelligence, wisdom, dexterity, constitution, charisma, changed by the
age's thresholds (`DS:3b99`: strength +1 past the first, -1, -2, -1 past
the next three; intelligence +1 past the second and fourth; wisdom -1
at or below the first and +1 past the second, third and fourth;
dexterity -2 and -1; constitution -1 three times), kept within the
race's limits (`DS:39e0`, strength's by gender) and raised to the
class's minimum (`DS:3bdf`), wisdom to 13 for a cleric of more than one
class, and at 18 a fighter, ranger or knight rolls exceptional strength
d100, at most the race's; each is drawn as it is rolled (`546c:0b50`).
Movement 12, one attack of 1d2, strength's bonus; the money, in steel
(`+0xf3`, `COINS` 4), is the classes' dice (cleric 3d6, fighter and ranger
5d4, mage 2d4, thief 2d6; a knight's 1d4 replaces it) and a fifth more; a
cleric's spells a day (`66c2:0722`) and spells 1, 3, 5, 6 and 8 known, a
mage's spell a day and spells 11, 12, 15, 18 and 21. Hit points come from
`4def:4b3a` for every class (see Training), then the character trains
silently (`4def:4d9e` with `DS:4b58`) until no class can rise, with var
`0x7ea8` 0xff meanwhile, restored as its low byte; the level line is
written over the sheet's at 7, 7 in white, and the combat figures and
money are drawn. `Reroll stats? ` (light magenta, No first) rolls again.
Then the sheet is redrawn, `Character name: ` (light magenta, 15
characters, upper-cased) asks until a name is typed, the default icon is
set and the icon editor opens, the scores become the base, and `Save
NAME? ` saves it to the roster (see The start menu), a knight with its
Plate Mail, Shield and Long Sword (not readied, linked Plate Mail,
Shield, Long Sword after the last recompute, so the weight saved leaves
them out). The record never joins the party; it is freed, saved or not.

The random numbers, in order: `Random(256)`; the age's dice; per score
18 d6 (and d100 after strength's for exceptional strength); the money's
dice in class order; then `4def:4b3a`'s and the silent training's; and a
reroll repeats all but the first two.

The port keeps these quirks:

- No score is adjusted by race: the code tests the race list's loop
  counter, always 6, the human, instead of the race.
- A multi-class's money is not divided: the test reads the class loop's
  counter, always 7 after it.
- A multi-class's age is its most, with no roll.
- Exceptional strength is rolled even where the race allows none, then
  cut to 0.
- The young (at or below the first threshold) lose a point of wisdom.
- `66c2:0b9f` at the class step reads 0x0a as its uninitialized byte,
  the normal colour the class list was given (`4def:0ee7`); every class
  with a thief level trains, and training's `66c2:0433` reads the usual
  7, so it never reaches the file.
- A single cleric or knight does not train: it keeps the saving throws
  computed with every score 0, and `+0xd6` (`HIGHEST_LEVEL`) 0.
- Var `0x7ea8` comes back as its low byte.
- The knight's items are not in the weight saved.
- A name is asked until one is typed; Escape does not cancel it. No name
  is checked against others.

Where creation would never end, free training (no row offers it) or a
character not okay (it cannot be), the port stops with
`COK_ECL_UNDEFINED`.

### The icon

The default icon (`4def:3c61`): head 3 for a kender, else 9 (large) or 7
for a woman and 5 or 0 for a man; body 0x17 for a cleric, else 1 for a
ranger, 0x18 for a knight or fighter, 0x1d for a mage, 5 for a thief.

The editor (`4def:408b`) clears the open frame and shows the icon as it
is, `old`, at cells 4-6 (ready) and 13-15 (attacking) of rows 7-9, and
as it is being changed, `new`, on rows 13-15, each over COMSPR's frame
(slot 25), with `old`, `new` and `ready   action` in white (cells 8, 6;
3, 10; 8, 12; 3, 16). The working icon is built in slot 12 from the
record's (`4b6d:0817` without colours) and recoloured (`4def:3e20`). Its
menus, on row 24 with no prompt (`67b5:03e2`, white and light green,
digits not directions, the selection kept from the menu before):

| Menu | Items | What they do |
| --- | --- | --- |
| 1 | `Parts 1st-color 2nd-color Size Exit` | the menus below; Exit or Escape leaves |
| 2 | `Head Weapon Exit` | `Weapon` edits the body (`+0x136` `ICON_BODY`), whose pictures hold the weapon |
| 3 | `Weapon Body Hair Shield Arm Leg Exit` (`Face` for 2nd-color) | the part's colour: body 1, arm 2, leg 3, hair or face 4, shield 5, weapon 6 (`ICON_COLOURS` 1-6, `+0x139` on), the low nibble for 1st-color, the high for 2nd |
| 4 | `Small Keep Exit` or `Large Keep Exit` | the size to change to (`+0x138` `ICON_SIZE`) |
| 5 | `Next Prev Keep Exit` | heads 0-13, bodies 0-31, nibbles 0-15, wrapping; Keep keeps the field, Exit or Escape puts it back |

Leaving puts back what was not kept, copies the icon to the record's slot
with its colours, frees slot 12 and asks `Is this icon ok? ` (light
magenta); No starts again from the icon as it is, without the frame. The
camp's Alter then redraws the camp's screen. In CGA mode 1st-color and
2nd-color give `Sorry, not in CGA` and stay at the menu, and the icons
show on cyan without the frame (`4def:4276`, `3d46`; see Display modes).

The port keeps these quirks: a space in `Parts` opens menu 5 with nothing
to edit, and no key leaves it; `Weapon` edits the body; menu 3's text is
patched in the data segment (`Hair` or `Face`); the selection carries
over from the menu before (from creation `1st-color`, from the camp
`Size`). The level whose key was last handled is uninitialized before the
first; a special key first whose scan code is 0 or 0x45 would test it,
and the port stops with `COK_ECL_UNDEFINED`, though the keyboard never
gives either.

### Modify Character

Modify (`4def:28fa`) picks a character and changes it only if its
experience is that of a new character (1500, 1251, 2000, 2001, 2251,
2500 or 5000) and `+0xd7` (`CHANGE_LEVEL`) is 0; otherwise `NAME can't be
modified.` in yellow. Over the sheet, the field being changed is drawn in
light magenta (`4def:278c`): the scores 0-5, the name (6, the character at
the cursor in white, `%` for a space) and the maximum hit points (7).
`Modify: ` and `Add Subtract Keep Exit` (digits as directions) take keys
for the scores and hit points; the name reads keys raw (`1614:025b`).
Up and down (8 and 2) move between fields, wrapping, as Enter does in
the name. Subtract and Add change a score within its race's limits
(`DS:39e0`), Subtract keeping the class's minimum (`DS:3bdf`); strength
above 18 raises a fighter's, ranger's or knight's exceptional strength
to the race's most, and Subtract takes it down first; wisdom sets a
cleric's spells a day `+0x11c` (`SPELLS_A_DAY` 0, level 1) to 1;
constitution and the hit points keep the maximum between the least and the
most the levels allow (`4def:4808`, `48e9`), the hit points then the
maximum. In the name Subtract and Add move the cursor, wrapping; Del deletes
at the cursor, Backspace before it; letters, digit 0 and others from space
to `z` are typed. After each key the stats are recomputed (`6346:0d20`,
`546c:07bb`). Keep recomputes the spells a day (`66c2:0722`), sets `+0xe8`
(`SHARES`), sets the hit points at full (`+0x11b`, `BASE_HIT_POINTS`) to the
maximum less the constitution's average bonus, and makes the scores the
base; Exit or Escape puts back the scores, exceptional strength, maximum and
name, and the hit points become the maximum.

The port keeps these quirks: in the name, upper-case A and S move the
cursor, and K is typed and then keeps; digits 1-9 and `\` are directions;
Backspace does not move the cursor back; typing at the end adds a space,
which is kept; Exit leaves `+0x11c` (`SPELLS_A_DAY`) as a wisdom change left
it; Keep's bonus sum is a byte divided unsigned (a cleric/fighter of
constitution 5 and a maximum of 8 gets 137 at full); `4def:48e9` past a
class's top level sets its sum rather than adding to it; the least counts a
ranger's and a knight's extra level, the most also a cleric's of a deity up
to 4; the name is redrawn in light green after a constitution change; the
`*` beside a changed score is never erased. Modify does not recompute THAC0,
saving throws or thief skills. Where `4def:257c` returns its
uninitialized local (a constitution outside 3-19), the port stops.

### Death Knights' creation

Death Knights' (dkk `49cb:05c7`, `3cac`, `4bea`, `25d4`; the `creation`
section, and the `menus` section's lists and menus by index) differs from
Champions' here, the rest being as above with its own tables (`DS:7423`,
`748c`, `756c`, `73b3`, `75b2`, `7618`, `0584`):

- The lists are 6a69:1b82's (`cok_menu_indexed_rows`), the heading a row
  of the list, `Select` and the list's own Exit; any key it returns but
  Select leaves. `Pick Diety`, `reroll stats? ` (from No), `character
  name: `, `save character? ` (from Yes, without the name).
- Pick Class gives a spells' block (220 bytes, `.WIZ`) to every class but
  fighter, thief and fighter/thief (2, 6, 0x0e), the levels 1 (a ranger
  2) and the experience 210003 for one class, 105002 for two, 70001 for
  three; a paladin (class 3, which dwarves may be) gets effect 0x2d
  (protection from evil). The cleric's 2000 and 1500 never apply (no
  class starts at 0).
- No money is rolled: 500 steel. A mage knows 10, 11, 12, 15, 16, 18 and
  19, and creation's training (`49cb:43b6` with `DS:8c5c`) gives it at
  levels 3, 5 and 7 those of its order (White: 29, 33, 34; 46, 47, 51;
  81, 82, 85, 87; else 30, 31, 32, 34; 45, 47, 48, 50; 83, 84, 85, 87);
  every class trains to its levels by the experience (the hit points
  each time). Exceptional strength and the default icon's body 0x18 count
  paladins as fighters.
- Saved, each class gets its items (`4bea`), from the item table (ITEM0):
  by kind (a knight; a fighter, ranger or paladin; a thief; a cleric; a
  mage) up to four of `DS:058f + 4 * kind`, a thief with a cleric level
  `DS:059c`'s first, each put at the list's head, readied but 0x5b; then
  the stats are recomputed and the item count set to 3, whatever it is.
- The icon editor (`3cac`) is Champions' with menus by index (`DS:35b8 +
  0x2e * level`: Size's `Small Large Keep Exit` without the size it has,
  the colours' parts without Face or Hair), every special key, Escape
  among them, doing nothing; `sorry not in CGA`, `is this icon ok? ` from
  Yes; the frame behind each icon from slot 0x1c. A colour's part read
  before one was picked (an index no item gives) stops the run, as the
  original reads its local unset.
- Modify (`25d4`) takes a character of 210003, 105002 or 70001
  experience; `Modify : ` by index (keypad on): Add, Subtract, Keep, Exit,
  Escape as Exit. What a key in the name does is the menu's last index,
  which only a special key set: in the name every letter is typed (A, S,
  K, E too), Enter and Escape do nothing. A hill dwarf paladin's charisma
  stays at 17; the most hit points are the level drain's (`580d:3454`);
  Keep sets no `SHARES`.

`tools/dosbox/checks/dkk_create.py` (not in `run_all.py`) compares the
original in DOSBox-X, seeded: seven characters of every family (cleric,
paladin, fighter/mage/thief, White mage, kender cleric/thief, knight,
cleric/ranger), every list, roll and reroll, name, the icon editor's
menus and edits, the questions, whole screens; their roster files byte
for byte but the far pointers; and for two of them Add, Modify (scores,
the name, the hit points, Keep or Exit) and Remove, and the files again.
`tests/test_start.c` (`test_death_knights_create`) creates and modifies on
its records, `tests/test_death_knights.c` (`test_creation`) checks the
section against the original's code.

## Training

`src/train.h` ports Train Character (`4def:4d9e`) with its hit points
(`4def:4b3a`, `4789`) and the experience table's row (`6346:371e`), and
Knight Change Classes (`4def:5812`, `567f`, `257c`). Training is free.

The training halls' scripts set var `0x7ea8` to the classes they train,
bits of `DS:38f2` (127, every class, in all three: ECL1 block 16's aged
adventurers, the outpost's HALL in block 17, and block 18 once), then run
`PROGRAM 0`; Begin Adventuring clears it. Train picks a character; one
not okay is refused (`we only train conscious people`, yellow) unless
free training is on. For each class with a level, the experience its
next level needs is the LongInt of `DS:3c65` (ten rows of 99 bytes: 0 and
1 clerics by deity, 2 fighters, 3-5 knights by order, 6 rangers, 7 and 8
mages by order of magic, 9 thieves) at 4 × (level + 1); 0 or less is the
top (level 8, rangers 7, thieves 9). A kender fighter or ranger stops at
5 below strength 17, at 6 at 17 and at 7 above. Experience past the
second next level is cut to one less than it, before anything is asked.
`We don't train that class here` or `Not Enough Experience` (yellow) end
it, else the frame's inside is cleared and `NAME will become:` (the
name light cyan at 4, 4) lists `    a level N Class`, then `and a level
N Class` from column 6, and `Do you wish to train? ` (light magenta, No
first) asks; Yes says `Congratulations...` on row 24. Each class the hall
trains rises a level, and while `+0xd8` (`SPREAD_LEVELS`) is not 0 it takes
`+0xd9` (`SPREAD`) down by `+0xd9` / `+0xd8` and counts `+0xd8` down, two
bytes nothing else in Champions reads or writes (0 in every monster and
in SAVE/'s characters); the levels are recomputed (`66c2:0433`), a mage
rising learns a spell, and the hit points rise (`4def:4b3a`) if the
highest level (`+0xd6`, `HIGHEST_LEVEL`) passed `+0xd7` (`CHANGE_LEVEL`).

A spell is learned from `NAME's Spells to Choose` (`546c:34ec` kind 4,
`5b04:0b21`): for each mage spell level 1-4 with a spell a day, the
spells of the order's table (`DS:4208` + 40 × (order 1 ? 2 : 3) + 10 ×
level, to the first 0) the character does not know, under headings, with
`Choose Spell: ` and `Learn` and no Exit; Escape shows it again.

The hit points (`4def:4b3a`): each class trained below its top level
(`DS:3903`) rolls its dice twice and keeps the better (at level 1
`DS:0b6e` of `DS:0b76`'s sides: cleric 2d8, or 1d8 for a deity above 4,
fighter and paladin 1d10, ranger 2d8, mage 1d4, thief 1d6, knight 2d10;
above it one die), and adds its constitution bonus (`4def:4789`: `DS:3c53`
by constitution, a fighter, ranger or knight one more at 17 up to five at
24-25) times the dice; at or above its top a class gives a fixed 3
(fighter, knight), 2 (cleric, ranger, thief) or 1 (mage) in place of the
sum so far. Sum and bonus are each divided by the classes with a level,
the sum at least 1: the maximum (`+0x62`, `MAX_HIT_POINTS`) gains both,
`+0x11b` (`BASE_HIT_POINTS`) the sum, and the hit points stay as far below
the maximum as they were.

In creation (`DS:4b58`) nothing is shown or asked, no experience is cut,
each class with the experience rises one level each call, the hall being
every class, and a mage of level 2, 3, 4 or 5 after it knows spell 15,
34 and 10, 31, or 47.

Knight Change Classes (`4def:5812`) picks a character and clears the
frame's inside; a knight of the Crown or the Sword shows, in light
green, its name centred on row 3, `is petitioning to become a` and
`Knight Of The sword` (or `rose`, in lower case) and, on row 9, `NAME is
too inexperienced.` (level 3 and 12,000 experience for the Sword; level
4 and 27,000, without the full stop, for the Rose), `NAME does not
qualify.` (base scores 12, 9, 13, 9, 10, 3 or 15, 10, 13, 12, 15, 3,
`DS:0b7e`) or `NAME may become a` and `Knight Of The Sword` (or `Rose`)
with `Change Exit`, else `Exit`. Change (`4def:567f`) moves it to the
next order; if its experience is below the new order's for its level, it
loses a level and hit points (`+0x11b`, `BASE_HIT_POINTS`, divided by the
level + 1, and the constitution bonus of `4def:257c`). The levels are
recomputed; experience is kept.

The port keeps these quirks:

- Training adds class bits, so a cleric and druid, or paladin and
  knight, make another class's bit.
- Experience past the second next level is lost even when No is
  answered or the hall refuses.
- The second pass for the experience cut reads the last class's row and
  level for every class (only visible with the kender's limits).
- A kender at level 6 with strength below 17 is not stopped.
- The constitution bonus divides unsigned: two or three classes with a
  negative bonus gain about 127 hit points.
- At the top level a fixed amount replaces earlier classes' rolls.
- Free training trains the unconscious and the dead, raises the
  experience to what each level needs, and at the top level still gives
  a hit point; nothing offers it.
- F9, whose scan code 0x43 is `C`, changes a knight's order even when
  only `Exit` is offered, as the special flag is not tested.
- The Rose's `is too inexperienced` has no full stop; `sword` and `rose`
  are in lower case on row 7.

Where the original reads an uninitialized local, the port follows what
the calls before leave there or stops:

- The experience row of a druid, a paladin or a knight of no order
  (`6346:371e`) is the row of the class before it in the same call; for
  the first class it is 0x74, the `t` of `Pick Character ` that Pick
  Character (`546c:36cc`) copies to its frame on every key at the very
  slot, whether the start menu came from the title or `PROGRAM 0`. Row
  0x74 reads the combatants' far pointers (`DS:6945` on), NULL or a
  segment above 0x2000, so such a class never trains and cuts no
  experience. With free training, which would take the pointer as
  experience, or with experience of 0x20000000 or more, the port stops
  with `COK_ECL_UNDEFINED`. No race's class list (`DS:3a50`) has a druid
  or paladin, and Create makes knights of the Crown, so only an edited
  file reaches this.
- A knight of no order petitions to become a `Knight Of The `: the
  order's name is a local never set (`[bp-6]` of `4def:5812`), which the
  start menu's freeing of its rows (`67b5:18a1` from `4def:0381`) leaves
  an empty string. The experience and scores are not checked and only
  `Exit` is offered; F9 changes the order all the same.
- `4def:257c` for a constitution outside 3-19 stops the port.

A differential test ran the original's routines in an 8086 emulator
against the port. Creation (`4def:06dd`, its lists fed picks and its
icon editor passed, the rest as it is, `4b3a`, `4d9e` and `66c2`
included) on random races, genders, classes, deities, alignments,
rerolls, names, seeds and var `0x7ea8`: the record saved, its items and
effects and the random seed after agreed in all 20,000 cases. Training
(`4def:4d9e` in both modes, with Pick Character and `546c:34ec`'s list
built and picked) and Knight Change Classes (`567f`) on random
characters, halls, answers and seeds: of 20,000 cases, the 19,685 the
port carries out agreed in the record, the seed, the texts drawn, the
notices and the spells listed, 421 of them with a first class that has
no row (0x74); the port alone refused 315 with such a class, 205 in
creation and 110 with free training. Modify (`4def:28fa` with the sheet,
menus and keyboard as they are) on random characters, constitutions 0 to
255 included, and keystrokes, and `4808` and `48e9` alone: the 13,627
Modify runs and 5,254 hit point pairs the port carries out agreed; the
port alone refused 385 runs and 704 pairs, a constitution outside 3-19
(`4def:257c`). The icon editor (`4def:408b` with `0817`, `3e20`, `3d41`
and the picture routines in Tandy mode on a linear frame buffer) on
5,000 random icons and keystrokes: all agreed in the record and every
pixel of both icons. The start menu (`4def:01b4` with Pick Character,
the lists, menus and party list, and each command as far as an Escape or
No takes it) on random parties, halls and keystrokes, its text and tiles
replayed by the port's routines and compared at every key read: of 5,000
cases, 3,440 agreed in every screen and the state after; 1,558 reached
commands the test does not drive (View, Add, Save and the mouse) and 2
the emulator's `Crt.ReadKey`. Add Character to Party, from the start
menu, with its listing, `4b6d:0555`, `008b` and the file reads running
over a small DOS layer on random rosters, and Turbo Pascal's own heap
laid out as the game booted in the emulator left it after the start menu
freed its rows: of 3,550 cases, 3,541 agreed in every screen and the
party after, 698 of them with a space first; in 9 a later Add ignored a
first space where the port leaves (see The roster). `169c:05da` with
`050b` on 1,450 random strings agreed. The title (`2f55:063c`): its five
screens agreed pixel for pixel. It is not part of the repository; `make
test` checks cases of each that the emulator ran (`tests/test_start.c`).

### Death Knights' training

Death Knights' Train Character (`dkk 49cb:43b6`) is Champions' as the
`levels` section words and bounds it. It clears free training first
(`DS:c46a`: Death Knights has none). The experience row of a paladin is 10
(`65cd:382c`, `characters.paladin_row`); a druid's or a knight's of no
order is an uninitialized local there too, which the port does not know
(`levels.stale_row` 0): such a class stops the run. A class at its race's
highest level (`DS:82d4` + race × 8 + class, `COK_DATA_LEVEL_LIMITS`: 18
at most) does not rise, where Champions stops kender fighters and rangers
by strength. Its words are in lower case (`DS:1542`-`1485`; `not enough
experienc`, a string[20]), and `do you wish to train? ` starts on Yes
(`6a69:2048` with `DS:57c0`'s 0). A ranger above level 8 learns a spell
only when the ranger level rises. The hit points (`580d:3193`) count
paladins with the fighters for the constitution (`580d:310e`,
`characters.fighter_classes`), give a paladin at its top level a fixed 3,
and keep the hit points as far below the maximum as before, but not below
0. After it, and after Knight Change Order, whether the character may
train (`+0xc6`) is worked out again (`65cd:38c7`), which colours its name
light magenta (`65cd:1b3b`, also in the party list's and the camp's
names).

Knight Change Order (`49cb:4ea4`) draws Champions' petition in lower case
(`knight of the sword`, `DS:14cf`, `14b9`, `14c4`; ` is too
inexperience`, a string[20], for both orders) and always offers `Change
Exit` (`DS:3361`): Change promotes whatever the petition said; its menu's
special keys do nothing (`DS:c0f8`), Escape leaves. A knight of no order
petitions for a local never set (`levels.no_order` `NULL`): the port
stops. A level lost (`49cb:4da1`) from knight level 10 up costs 2 base and
maximum hit points and no constitution (`levels.promote_fixed`).

The aging after a battle (`3e05:0264`, `cok_train_aging`) runs when an
effect 0x27 aged someone in it (its haste, `504d:0d88`: Champions'
`3f44:0f78` but only when not removed, the movement `DS:be6f` doubled too,
and `DS:c463` set, `game->effects.aging`; the years' carry clears it,
`5b7d:042e`). Each party record (not past the party's size) not dead,
stoned or gone, of race 0-6, past its race's first age (1590, 1590, 315,
515, 515, 189, 110) is `who is looking very old`; past the second (1600,
1600, 325, 525, 525, 199, 120) the panel says `the strain of the last
battle has worn heavily on NAME.  `, then `hebreathes deeply one last
time and crumples to the ground.` (`DS:23da` and `2389` run together), and
it `dies from age` (status 8, `638c:00c8`). The original then counts the
party members who saw it from a local it never sets (`[bp-0x10]`): the
port stops there (`COK_ECL_UNDEFINED`).

`tests/test_start.c` (`test_death_knights_levels`) trains and promotes
`DKKAssets/SAVE`'s characters and ages them; `tests/test_effect.c` effect
0x27; `tests/test_death_knights.py` the start menu's Train and Knight
Change Order, and a hasted knight of 115 aging in the first fight on Auto
(`--effect 1,27,0,0`) and looking very old after it; `tools/dosbox/checks/dkk_levels.py` (flow `train`) the
original: Knight Change Order, Train with the spell learned, and the saved
game and characters written after, 0 pixels and 0 bytes apart.

## DOSBox-X harness

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
  unless they say not (`GAMES`' `turbo`; Death Knights' not yet tried):
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
whole screen; `load_bmp` decodes a frame with `bytes.translate`). The
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

## Checks against the original

These follow the disassembly. Each that can run unattended was then run in
the original under DOSBox-X (see DOSBox-X harness) by a script in
`tools/dosbox/checks/`, which compares its screens (each with the port's
at the same key read), files and memory values with the port's for the
same keys and seed, or, where the port stops or does not model what the
original does, asserts what the original was seen to do; `make
dosbox-checks` runs them all.
The checks keep the numbers they had in this list (#1-#129, in its order),
which the scripts cite.

Checked in DOSBox-X, the original as the port unless said (where the
original's behaviour is recorded, its section says what it does):

- #1 Startup's text screens, in `machine=tandy` and `svga_s3`: at each of
  46 key reads the 80×25 text, its attributes (all 0x07) and the cursor,
  the note to Tandy users, Backspace back over a wrapped row's start,
  Ctrl-Break and Ctrl-C doing nothing, and `KRYNN.CFG` byte for byte; the
  setup closed at the path leaves an empty `KRYNN.CFG`, which the next
  start reads as CGA with no sound: `a_setup.py`.
- #3 The title's music with `KRYNN.CFG`'s `T` and `P`: the pitches, the
  tempo (within DOSBox-X's WAV error); LF line ends silent; the overland
  steps, and the 3D steps and turns, all matched by length (given a pause
  after the last key, without which `eclplay`'s run ends before the last
  turn's sound does); in a battle the drop's sound, the baaz turns' steps
  two or more commands within 100 ms in `eclplay`'s clock, which has no
  drawing time, sounding as one there where the original's spread (the
  rest below): `f_sound.py`.
- #4 The step before a record read is cut there, as in the port: silent in
  one run, 10 ms in another (a timer interrupt between the command and the
  read), never more than one interrupt's (how long the read keeps it:
  below): `f_sound.py`.
- #5 Ctrl-S after the demonstration leaves the sound off for good; Alt-R
  toggles it as Ctrl-S does, tried in the start menu (in combat: below):
  `a_ctrl_s.py`.
- #6 Stinking Cloud with a wall among its cells and over half another
  cloud: the tiles, who coughs and chokes, both clearings and no redraw,
  over four rounds (the rest below): `d_cloud.py`.
- #7 Dispel Magic at a cloud: in combat the original goes on, nothing
  written (see Clouds, pools and breath for what its walk read); in camp
  nothing happens, as in the port: `e_cloud_dispel.py`.
- #8 Slow Poison's 63 hours rested: the character "dies from poison", the
  game goes on, its free list corrupted (see Spell effects); Cause Disease
  and a constitution of 20 as in the port, the chains stopping in a camp's
  rest: `b_slow_poison_63h.py`, `slow_poison.py`, `b_timers.py`.
- #9 Sir Lebaum's d20 over ten rounds: his Dispel Magic took SIRRION's
  0x6f, as in the port: `d_lebaum.py`.
- #11 A bolt between two walls bounces for ever in the original, the game
  hung (on a combat map rewritten in memory; see Spells in combat):
  `e_bolt_bounce.py`.
- #12 The aurak's breath at one beside it that saves: 10 points, and its
  turn goes on to an attack: `d_aurak.py`.
- #13 The debug mode: the trace after each opcode, the status line's `*`,
  `DEBUG.TXT` (128 bytes held, CR LF, the rest written at Ctrl-D's close,
  identical files), Ctrl-2 quitting, Items' View and a shop's menu without
  `Exit`; in the caravan's battle the setup's trace of each combatant and
  the combat menu's `Done*`, 122 screens (the end of a game: below):
  `a_debug.py`, `a_debug_battle.py`.
- #14 `DEBUG.TXT` where it cannot be made: nothing is skipped and no
  runtime error comes, as in the port (see Debug mode):
  `e_debug_readonly.py`.
- #15 Aim, Manual, Escape, Down and Center in a first battle; with an
  earlier battle's record left in the entry the original centres on entry
  0's cell (see The player's commands): `e_manual.py`.
- #16 Done's F8 ends the turn, Home guards, Del slows past 9: `d_done.py`.
- #17 Escape in Move after an attack of opportunity: back, the wounds
  kept: `d_move.py`.
- #18 `Target` after Manual's Exit: the original asks `Attack Ally:` (see
  The player's commands): `e_manual.py`.
- #20 (part) The mouse with DOSBox-X's own driver: the arrow's look in
  Tandy and EGA mode, hidden through the title's waits and running text,
  clicks landing on the cell under its tip, a click in column 0 of the
  title's menu with `DS:6e2f`, `6e30`, `6e16` unset, both buttons at the
  title's menu; in CGA mode DOSBox-X's driver draws the arrow a bit a
  pixel, as these notes say, so its box is not compared there; after a
  camp the part of the overland mark under the arrow vanishes in the
  original, as recorded under The mouse and the joystick, where the port
  does not reproduce it (the rest below): `f_mouse.py`.
- #22 A cleric's long spell moves it down the order, the d2s as the
  port's: `d_cast.py`.
- #24 Space during the computer's turns: the turn given up, the next party
  member's the player's: `d_space.py`.
- #25 Alt-Q runs the first turn's 0x0f twice; event 7's effects end the
  turn before its menu, so that half cannot arise: `d_altq.py`.
- #26 The frame's tile set: border and moons the port's in Tandy, EGA and
  CGA mode: `load_game_screen.py`.
- #27 No light magenta and no holes in Throtl's 3D view, and its far walls
  the original's since the wall sets' fix (see Wall tiles and sky pictures
  below): `c_throtl.py`.
- #28 `Loading...Please Wait` shows where the port draws it, count for
  count in 33 phases, animation on and off, row 24 after a picture as the
  port leaves it: `f_loading.py`.
- #29 Game A's party list and status line (`7,13 N 00:00`) beside the
  outpost's view, and Solace's first page, row 24 included since the full
  stop's fix (see Load saved game A below): `c_outpost.py`,
  `load_game_screen.py`.
- #30 The pit in Throtl: three hits on one page (the view's walls: #27):
  `c_pit.py`.
- #31 Game A loaded and saved again: KAL and SIRRION know spell 8, every
  file the port's: `save_again.py`.
- #32 A human thief's skills with the 7: `b_thief_skills.py`.
- #33 Game B rested a day: its `.SFX` files unchanged:
  `b_rest_day_effects.py`.
- #34 A long timed effect loses twenty in the ten minutes where a short
  one ends: `b_effect_order.py`.
- #35 0x5e's bonus to saving throws: a kender of constitution 18 saves
  where a build without it fails (a dwarf saved by it alone was not met):
  `c_pit.py`.
- #36 Throtl's camp screen and its rest menu: `b_camp_screen.py`.
- #37 Enter right after `Encamp` opens `Save`, not `Alter`, in both (see
  Camp): `b_camp_screen.py`.
- #38 Games saved in Throtl's camp, the outpost inn's camp and `SAVE/`'s A
  resume as in the original, and saved again hold the same last mode,
  since the fix of the first block's vectors and last mode (see Save in
  camp below): `b_resume.py`.
- #39 `Fix`'s time with the stack's 14: `b_fix_time.py`.
- #40 A day's rest heals once only counting one camp's ticks (`DS:7136`):
  `b_heal_day.py`.
- #41 Scribing marks the other item and keeps the scroll: `b_scribe.py`.
- #42 `Pics`' down arrow turns the pictures off (and F7 the animation):
  `b_pics.py`.
- #43 The grimoire highlights its last row shown: `b_grimoire.py`.
- #44 `Display` shows 0x13 as `Find Traps`: `b_display_effects.py`.
- #45 A knight above level 6 with no cleric level marks a granted power
  again and again in the original too: `b_granted.py`.
- #46 Strength on a knight alone of strength 3: 18/30 from the commands'
  `Cast`, 18/100 from camp's `Magic`, in both (see Casting):
  `e_strength.py`.
- #47 Trade's byte in the four camp cases; from the commands' `View` the
  original asks again or leaves by the run (see View): `e_trade.py`.
- #48 After an item with `+0x3d` 0x80, a cast neither says "casts" nor
  forgets the spell: `b_item_spell0.py`.
- #49 Haste ages each member a year, once: `b_haste.py`.
- #50 A character's sheet, `Fighter/Red Mage` and `18(00)`: `b_sheet.py`.
- #51 The conjured Spiritual Hammer vanishes when unreadied; the item's
  permanent 0x17 stays, in both: `b_hammer.py`.
- #52 Backspace in an amount leaves the last digit shown: `b_amount.py`.
- #53 Throtl from the south: the guards' close-up at once: `c_throtl.py`.
- #54 A wandering monster's sprite at 2 and 1 over the view, and the
  close-up: `c_wander.py`, `c_temple33.py`.
- #55 The cleric round the corner: `Wait`, the menus' selection, `Sly`:
  `c_temple33.py`.
- #56 `ROB 0 10 0` leaves 476 of 530: `c_rob.py`.
- #57 The minotaurs of ECL3 block 80: fleeing fails (three seeds):
  `d_minotaurs.py`.
- #58-62 ECL1 block 32's chest: 1391 experience points, the results and
  treasure menu, `Take`'s jewelry first, `Share`'s gems, the pool's
  question, `Detect`: `c_chest.py`.
- #63 Fleeing: `The party has fled.` and 0 experience (every enemy gone
  first; a flight past living ones not tried): `d_flee.py`.
- #64 ECL3 block 98's hoard shared: what is left stays, `Share` still
  offered: `c_ecl3.py`.
- #65 A generated potion (type 0x30) can be used, as in the port (see
  Treasure and the end of combat): `c_ecl3.py`.
- #66 The outpost's armoury: its first screen, `Buy`'s list, the purchase
  (the `buys` line on row 24 comes between key reads and was not caught):
  `c_outpost.py`.
- #67 ECL2 block 50's weapon smith charges 15: `c_block50.py`.
- #68 Appraise's `Keep` with no items loses the gem; Escape sells it:
  `c_outpost.py`.
- #69 Leaving a temple with money in the pool: `c_outpost.py`.
- #70 `Heal`'s list after a cure bought: all ten cures again, the row
  picked highlighted, since the fix of Heal's frame (see In a temple's
  `Heal` below); the first time, the first cure highlighted:
  `c_outpost.py`.
- #71 Raise Dead loses the whole difference (constitution 15):
  `c_outpost.py`.
- #72 `10 Arrows +1` halved and sold: 6 for the two, 60 for one:
  `c_block50.py`.
- #73 A shop's `View` first, `Trade`, Escape: entered from the outpost's
  menu the original asks for a partner again (7 runs); after a `View` it
  leaves, as the port (see Shops and the temple): `e_trade.py`.
- #74 Throtl's gate's battlefield, every combatant's cell and facing:
  `d_gate.py`.
- #75 The second of four unconscious: its body right of the first, at the
  gate and on open ground: `d_ranks.py`.
- #76 The huge patrol, the party a step south-east from 30, 2 onto KERNEN
  at 31, 3: 5 bozaks and 9 sivaks find no place in both and each receives
  5033 (the bullet's 7, 9 and 4937 were `eclplay`'s with no 3D map at 0,
  0; from 30, 3 all 27 find a place, 7698 each): `d_patrol.py`.
- #77 Neraka's room at 12, 8: tables and chairs only in the rooms' blocks
  (the other rooms not fought in): `d_neraka.py`.
- #78 ECL3 block 97's red dragon: levels 1-3 "terrified" (no higher level
  in the party): `d_dragonfear.py`.
- #79 The outpost's patrol's surprise: the first round's order, all 33
  entries (no one of dexterity under 16 in the party): `d_patrol.py`.
- #80 Dying: the count (combat record `+0x0e`) and status in the
  original's memory at each round's end, status 6 at 10 with the hit
  points unchanged, as in the port, whose log says "Your Teammate is
  Dying" to the ninth round (the text itself, overwritten by the Continue
  prompt, was not caught on the original's screen); Bandage: `d_dying.py`.
- #81 "Continue Battle:" at each round's end of a won battle: `d_helm.py`.
- #82 `START x Helm` and Alt-X: "The Gods intervene!": `d_helm.py`.
- #83 The gate's combat screen as the battle begins: `d_gate.py`.
- #84 Game A's combat icons: `d_gate.py`.
- #88 Attacking a hobgoblin: the panel's rows, text and colours:
  `d_gate.py`.
- #89 Arrows counted down by the shots (a short bow: no long bow in the
  saved games; the rest below): `d_arrows.py`.
- #90 A bozak explodes beside two characters, 1-6 each; that route ends
  with the party destroyed, whose text is typed a letter at a time since
  the fix of the text delay (see Kill a bozak below): `d_bozak.py`.
- #91 A baaz's killer loses his sword and has it back after:
  `d_weapon.py`.
- #92 A step away draws one attack `(from behind)`: `d_move.py`.
- #93 `Area` in Throtl, the map following a step and a turn:
  `c_throtl.py`.
- #94 The map's doors and passable walls, Throtl's and Sanction's
  one-sided ones: `c_throtl.py`, `c_map80.py`.
- #95 A wandering monster with the map on: `c_wander.py`.
- #96 The catacombs' `Not Here`; with `Helm` the map, off at the next
  step: `c_catacombs.py`.
- #97 The catacombs entered with the map on show the 3D view:
  `c_catacombs.py`.
- #98 Leaving the first outpost: the overland map, the mark, a step east:
  `c_overland.py`.
- #99 Ctrl-F8 in `Move` camps without a step: `c_overland.py`.
- #100 Swimming off 35, 4: no step, the same again on 5 or F1:
  `c_overland.py`.
- #101 A won fight on the overland map leaves the mark where it was:
  `c_overland.py`.
- #102 The knights' camp left: the map again, the script ended:
  `c_overland.py`.
- #103 The magic shop's and the weapon smith's portraits: `c_block50.py`.
- #104 Skyla drawn as nothing, the kender's icon `CPIC1` record 15:
  `c_npc.py`.
- #105 Find Traps cast from the second character's memory at the trap:
  `c_pit.py`.
- #106 Leaving Throtl: the kender and Strangbourn leave: `c_npc.py`.
- #107 The demonstration: its party, the green dragon's close-up's 12
  frames, the title after its battle; each screen, read from memory every
  50 ms, the port's frame in order or one caught while the game drew (it
  clears the whole screen before the battle's frame, and a combatant's
  whole panel before writing it again): `a_demo.py`.
- #108 30 seconds at the title's menu, or F10, start the demonstration
  (its `Of` cannot be told from `of`: the font has capitals only):
  `a_demo.py`.
- #109 Escape then Save in the start menu: unsaved, `Quit anyway?`, No
  opens Save: `a_unsaved.py`.
- #110 Creation: no racial adjustment shown, a multi-class's money
  undivided, a human cleric of 19 losing a point of wisdom and one of 21
  not (20 not tried), a thief's skills: `a_create.py`.
- #111 A knight's Plate Mail, Shield and Long Sword carried, not readied;
  its weight until loaded again: `a_create.py`.
- #112 No key leaves the icon editor's `Parts` menu: `a_icon.py`.
- #113 A roster character with 128 jewels is not listed; a space first
  ends Add; and a space at a second Add left Add too (see The roster):
  `a_roster.py`.
- #114 A fourth ranger: `too many rangers in party`: `a_roster.py`.
- #115 Removed and saved over a file with No: the new name taken:
  `a_roster.py`.
- #116 Training cut the experience before No: `a_hall.py`.
- #117 F9 at Knight Change Classes changes the order: `a_hall.py`.
- #118 Modify's `K` keeps and ends; the wisdom's spell kept until loaded
  again: `a_modify.py`.
- #119 Magic Missile with Cast: the pose, the flight, `Casts a Spell`,
  `Spell:Magic Missile`; under 0x4a too: `d_cast.py`.
- #120 Hold Person at three targets, `Cannot target yourself`:
  `d_hold.py`.
- #122 Fear with `T` at once, the caster at one place (26, 13; the bullet
  asks for a few): the port's cone in Tandy, EGA and CGA mode with the
  game at a PSP of 0x08f0; another at 0x087b (see Spells in combat):
  `d_fear.py`.
- #123, #124 Every screen compared in CGA and EGA mode (the title, the
  start menu, character creation, Solace, the outpost, the camp, a sheet,
  the overland map, Throtl's 3D view, a battle's first turn and Aim, CGA's
  four colours, `Sorry, not in CGA`) is the port's (see In CGA mode
  below): `f_display_modes.py`.
- #126 A small picture's frames with little memory: fewer by mode, or
  `WARNING: Insufficient Memory` (see Display modes): `f_frames.py`.
- #127 Lightning Bolt beside a wall comes back and hits the enemy again,
  and the caster too, in both (see Spells in combat): `d_bolt.py`.
- #128 A cleric hit while casting "lost a spell", and the spell is gone:
  `d_cast.py`.
- #129 Magic Missile reflected: the caster takes the damage:
  `d_turning.py`.

Death Knights of Krynn, read headless (see Games), is checked by
`tools/dosbox/checks/dkk_views.py`, which `run_all.py` (Champions' set)
does not run: `python3 tools/dosbox/checks/dkk_views.py` (about four
minutes; DOSBox-X in EGA mode, and two views in CGA mode). It compares,
with `daxcompose` and `eclplay --game death-knights`, the title's first two
pictures (`TITLE` records 1 and 2, whole screens); the first text of the
game where `DKKAssets/SAVE/SAVGAMA.PTY` resumes (ECL1 block 16 over the
overland map: the whole screen, the big picture's frame with the moons,
the map with the cursor, the text window and the menu's row); and the 3D
view with the adventure screen's frame around it (all but the party list
and the status line, cells 17-38 of rows 1-15, which need a party, the
text window and the menu's row) in four places of ECL1, the outpost's gate and a hall
(block 36, wall set 1 from `WALLDEF1`, `8X8D1` and `BACK1`) and the city's
street and a building (block 32, wall set 2 from the files of 4), out of
doors (`SKYGRND`'s ground) and in (`BACK`), facing each way the party turns
there (Move mode): nine views, and the gate's and the hall's first again
in CGA mode. The port could not load Death Knights' saved games when it
was written, so each place is a copy of `SAVGAMA.PTY` with the place written in
(the script says where), which the original resumes; the port runs the
block's load vector from `LOAD FILES` with its code patched (`--start`,
`--set` on the code) to stand on the same square, facing the same way, and
stop once the view is shown. All agree, 0 pixels in each region, after two
fixes the check found: `SKYGRND` and `BACK` are drawn opaque (loaded
without a mask, `DS:42d0`'s 0, not with colour 0 or the sky's
transparent), and the overland cursor too (its colour 13 drawn). With the
frames (see Death Knights), after two more: the frame's tile set keeps its
colour 13 (loaded with 0 transparent, `70eb:002f`: the middle moon's
magenta), and the CGA backdrop has no line row and draws the pictures
(`6d21:0177`). The saved game copies carry the moons' phases the port is
given (`--set` 0x4cf9-0x4cfb); in block 32, whose script does not draw the
frame again after eclplay's first, they are the port's own at the start,
0.

Its front end, `tools/dosbox/checks/dkk_start.py` (about four minutes;
see Death Knights' front end), plays the same keys in the original and in
`eclplay --start-menu` through three flows: the title and Load, Save and
Begin of `DKKAssets/SAVE`'s game A, its first texts and camp (with the
party list); Load from Champions (the `SAVE/` games with their bank set);
and Remove, Add, Drop until no one is left. Each screen agrees, 0 pixels,
after four fixes it found: the camp's first line is Death Knights' own
(`the party makes camp`, `text.camp`), the questions' prompts are light
magenta (`6a69:2048`), the list goes back to its first row when the last
Drop ends the game, and the roster's erase takes the `.WIZ` too
(`46b2:09dc`). The saved games, vaults and characters agree but for the
original's stack bytes and pointers.

Its fights, `tools/dosbox/checks/dkk_fight.py` (about eighteen minutes, seeded
with `--seed`, 1 by default: `cokbox.Box(seed=N)` sets its RandSeed,
`DS:84ac`, as `eclplay --seed N`), with the Death Knights helpers of
`tools/dosbox/checks/dkk_lib.py` (its data segment's combat offsets, each
from the code that reads it, its records, combatants, turn order and
battlefield read from the original's memory; the port's state through
`d_lib`'s dump, with the variables `0x7c00`-`0x7fff` and the state where
the run ends, beside `--screen`): saved game A loaded and begun, block 16's
texts to the first fight, "THE BATTLE IS JOINED.", the leadership's
screen, and the battlefield at the first turn, where the original waits in
the player's menu and the port waits for a key, its menu not drawn: 0
pixels but the menu's rows and the actor's cursor, which the player's turn
draws; the
combatants, their cells, sizes, hit points, statuses, sides, the
computer's flags and effects, their combat records, the turn order, the
round, the battlefield's cells, the records and RandSeed equal (the dice
rolled alike from the title on). Then every enemy is written dead in the
original, as `--combat won` makes them, its turn ended (`E`) and the
battle with it (`continue battle?` No): the results' screen agrees with
`--combat won`'s, 0 pixels, and so do the party's experience and
may-train flags and the variables but the clock's minute (`0x4bc7`), the
round the original fought. The same battlefield with a river (var
`0x4bf3` set in a copy of the saved game) and with two ambushes (`DS:c4cc`
poked with `0x0e` and `0x0b` once `SETUP MONSTERS` has run, the port's
build taking `COK_DKK_AMBUSH`), whose last enemies face other than the
party, so that the first knight after them is laid out again and placed by
the enemy's side: the battlefields, the combatants' cells, their combat
records and RandSeed agree. And the first fight with the party put on
Auto (Alt-Q at the first turn, `eclplay --combat auto`) to its end: the
original is stopped at each round's end (`3e05:0ed0` made to spin,
`dkk_lib.trap`, its code found in the overlay buffer, `DS:8478`-`847c`,
by windows the loader's fixups leave alone; the round over when the turn
entry holds none or a side is gone, the game then standing still) and
read, against the port's state at the same place (`dkk_lib.round_mark`, a
mark `round.c` logs that `eclplay`'s log writes the state at): the
combatants, their records and combat records, the turn order, the round,
RandSeed and the screen, each of the 13 rounds equal and 0 pixels; then
"continue battle?" (No) and the results, 0 pixels, the records and the
party's experience equal, and combatant entry 0's bytes (`DS:b58c`,
`b58d`, the setup's tile-set flags) kept through the fight. The first runs
found the saving throw's moon (var `0x4cf8` + order, not Champions'
wrapped `0x4bf8`): the nightmares' smoke took hold of a mage the original
saved. `--flows fight,river,ambush,auto` picks the flows (each about
three and a half minutes, the auto flow four). The drain, mage and dragon flows play
the Auto fight with other monsters: block 16's two enemy `LOAD MONSTER`s
(`8621`, `8628`) poked once `SETUP MONSTERS` has run, in the original's
ECL code (`DS:8c39`, a far pointer to address `0x8000`; `dkk_lib.code_poke`)
and the port's (`COK_DKK_POKE`, a transform of `monster.c`): wights and
spectres (the level drain, 0x43 and 0x44), kuo-toas (0x53) and black
wizards (the computer's spells: Magic Missile, Hold Monsters, Hold
Person, Mass Invisibility), a blue dragon (its breath, 0x55) and ghouls,
fought by a copy of saved game A where no one turns undead (its cleric
levels 0, the paladin's 2); each round as the auto flow, and the party's
levels, experience and hit points after (`+0x28`, `+0x45`-`+0x6c`). A
spell's overlay loaded in a round can move the code the round's trap made
spin; the untrap gives back the moved copy too (`dkk_lib.untrap`). The spells flow (band B's
handlers): a copy of the saved game whose casters memorize Fireball and
Lightning Bolt (FLAVIUS), Shield and Haste (CELESTE), Dispel Magic and
Bless (NEACAIL, TANAR); keys from the first turn, each typed once the
original waits and stands still, cast them over the first two rounds (the
others guard), and the screen, the records and RandSeed are compared
where those keys end; then Alt-Q (Magic Off: the computer's own spell
choice is the cast flow's) and the rounds after, as the auto flow's
(about fifteen minutes).

Its treasure, `tools/dosbox/checks/dkk_treasure.py` (see Death Knights'
treasure; seeded as `dkk_fight.py`; `--flows treasure,vault`, about two
and a half and one and a half minutes): the first fight left as
`dkk_fight.py` leaves it, then block 36's own way to the commander's
`TREASURE ITEMS` (the walk to the temple, Yes to the healing and to each
cure: two healed set var `0x4c04` to 2) and its `COMBAT`: the results
(the ring's experience), the treasure menu, Take's list, the ring taken,
Exit, `ADD EP` for the party, camp and Save Current Game C; and saved game
A with its block (var `0x4bf2`) 35, which Begin resumes in the vault of
Gargath: its screen, Pool, Take's coins, Money's deposit, Exit, camp and
Save C. Every screen agrees, 0 pixels, the treasure's and the vault's all
whole; the vault's `TMPVAULT.DAT` and `VAULTC.DAT` byte for byte, saved
game C and the characters' files but the stack's bytes and pointers, and
the ring in its taker's `.JNK` but the stack's bytes after its name. What
the first fight's end leaves different, where the original fought its
first turn and round and `--combat won` none, is masked and said: the
clock's minute on the status line (and var `0x4bc7` in the saved game),
`RANDOM`'s pick after it (the round's dice), the selection the block's
`EXIT` restores (the original's first turn's actor) and each record's
attacks left (`+0xce`). And camp's forget, which it found: the original's
(`4381:05b0`) clears only scrolls' marks, the port's (Champions' 4888)
also the spells cast, so the two cures cast before the save stay marked
(bit 7) in the original's `.WIZ` only (step 3's levels and training, with
camp). It found too that a menu's selection must not leak past the vault
into the adventure's menu (each Death Knights menu keeps its own, around
`DS:c0f7`), and the colour of camp's question after Save (`start.ask_colour`).

Its levels, `tools/dosbox/checks/dkk_levels.py` (see Death Knights'
training and Death Knights' camp; seeded; `--flows train,memorize`, about
a minute and a half each): saved game A with the hall's variable `0x7ea8`
at 127 and FLAVIUS's experience at 100,000, loaded: Knight Change Order
for SIR DRYDEN and Change, Train Character for FLAVIUS (the levels he
reaches, Yes, the spells to choose, the one learned), Save C: every screen
0 pixels apart, saved game C and the characters' files but the stack's
bytes and pointers; and saved game A with a spell of FLAVIUS's marked,
loaded and begun: block 16's rest, camp, Magic, Memorize's list of the
marked spells (Keep), Escape, the table of 8 levels and the grimoire,
Display, 0 pixels apart (its Exit by `6a69:1b82`, from column 0). It
found that Death Knights' Magic menu keeps its own selection and that
Display's blank rows are headings. Its flow `table` (saved game B begun,
camp, Magic, BALTHIR NOMOON's Memorize) shows the table with no name said
over it, 0 pixels apart.

Its shops, `tools/dosbox/checks/dkk_shops.py` (see Death Knights' shops;
seeded; `--flows shop,temple`, about six and four minutes): saved game A
made to resume in ECL1 block 36 (the block, map 36 and wall set 1 in slot
0 written in, as `dkk_views.py` does; SIR DRYDEN given 2 gems, a jewel and
40 hit points), from the square south of the weapons store a step north,
Yes, and its shop: Buy of the Banded Mail and its funds, View's Items in
mode 1 with Sell of the Mace +1 and Id of the Banded Mail, Appraise (a gem
sold, the jewel kept), Pool, Buy of the Chain Mail from the pooled steel,
Exit with money left (Yes, from Yes), Share, Exit, camp and Save Current
Game C; and from the temple's square, a turn west into its wall (var
`0x4c06`, the wall ahead), Yes, and the temple: Heal's Cure Light Wounds,
Cure Blindness for one not blind (No), Heal, View's Items with Escape, back
at the temple's menu, and View with Right, which stays in View, and Up,
which leaves it (View's flag `DS:c0f8`), Exit, camp and Save C. Every screen agrees, 0 pixels, saved game C and
the characters' files byte for byte but the stack's bytes and pointers:
the Banded Mail bought holds its price line past its name in both, and
after the line what `STOCK`'s stack left in the original's.

Still to check, with why:

- (#1) Startup and the setup, the parts DOSBox-X cannot show: a hidden
  `krynn.cfg` should bring the setup back (`169c:04c1`'s `FindFirst`
  passes no attributes), but DOSBox-X's DOS returns hidden files to such a
  search, so there the game finds it: needs MS-DOS (booted in DOSBox-X, or
  a PC). With a joystick (DOSBox's `joysticktype`), the calibration's two
  screens and the thresholds written, against `eclplay --joystick` with
  `\j` keys: DOSBox-X's emulated stick did not move for mapper bindings
  pressed through X (needs a host joystick). And the 80-column mode's look
  (the ROM's font) against the port's 640×200 in unscii's 8×8 font, a
  judgement.
- (#2) The Tandy's chip given a tone of divisor 0 (the effects driver
  writes one, loud, on the first tick of commands 2, 4, 5, 6, 8 and 12):
  the port follows MAME in taking it as 1024, a 109 Hz tone; a chip that
  took it as a period of 0 would toggle every step, inaudibly. Record the
  first 5 ms of those sounds on a Tandy 1000. DOSBox-X's Tandy chip is
  MAME's code, as the port's, so only the real one tells.
- (#3) The sound (see Sound) on a Tandy 1000: the noise's character in the
  steps and the title's drums (the NCR 8496 feeds back bits 1 and 5 XNOR'd
  and keeps its register through the effects driver's rewrites of the
  noise control each tick; a chip that reset it on every write would sound
  different), the voices' loudness against each other and against the
  speaker's (the port's levels are MAME's, the speaker's a guess), and the
  clicks the coupling smooths: DOSBox-X's chip is MAME's code, as the
  port's. In DOSBox-X still: an arrow, Fireball and Lightning Bolt,
  against `eclplay --wav`.
- (#4) The sounds a record read cuts (see Sound): for how long the disk
  read keeps the sound stopped (DOSBox-X's reads take its own time: a real
  disk), and a sound followed by a portrait (not run).
- (#5) Alt-R in combat, where nothing else takes it, should turn the sound
  off as Ctrl-S does: tried only in the start menu.
- (#6) Clouds and pools (see Clouds, pools and breath): Stinking Cloud
  over a fallen party member's body (none fell where a cloud could go in
  the runs), and a kapak dying on a body that is raised before its pool
  drains: the port stops there (`3f44:2037`'s byte never set).
- (#10) Breath: a red dragon two cells across, a wall diagonally below its
  left cell, breathing at someone seen past its right cell (the cone's
  line stops at the wall, meeting no one): the side byte is a stack
  address's low byte, 0xe0 (0xea after a mirror) in a battle a vector of
  the adventure loop starts, 0xda (0xe4) after NEWECL, 0xd8 (0xe2) in a
  rest encounter, 0xb8 (0xc2) in one of PROGRAM 9's camp (ECL1 block 16's
  knights); compare who is hit in each (ECL3 block 98 at 8f44 with
  `eclplay --test-party 6 --combat auto --seed 8` is one). A green dragon
  breathing at a target when the last combatant listed lies in the
  target's column: the burst should fall beside the dragon (the range the
  target routine's last line left is 0). Acid or a dragon's breath at a
  target facing west (its icon drawn mirrored) whose icon's mask is opaque
  at the bottom-left: sight one cell in Tandy mode (its 2 leftmost pixels
  opaque); in EGA mode only where the 8 leftmost are; in CGA mode sight to
  3 cells whatever the mask (see Display modes). Not run in DOSBox-X: each
  context needs its own route to a red dragon in that geometry (ECL3 block
  98's fight at 8f44 can be reached with a patched saved game); breath
  otherwise agrees (#12, #90).
- (#13) The debug mode at the end of a game: let the party die with the
  flag set, then press Ctrl-D: `DEBUG.TXT` should be empty again. Not run:
  the party must die (with `Woof` the title, and so the demonstration, is
  skipped).
- (#19) Aim's cursor's flight between targets (`6346:1ba6` with delay 0):
  compare with the port's. A person's judgement: DOSBox-X's screenshots
  come about three a second (an AVI, F12+I, could be looked at).
- (#20) The mouse: with both buttons at once, the digit read at
  `58e7:02f7` (the port gives it 0); and the pointer's image left in the
  overland map's old cell after a battle (after a camp it was not seen;
  see The mouse and the joystick).
- (#21) The joystick: the keys a push gives and their pace on a real
  machine (the port repeats a held push every 60 ms) and the calibration's
  timeout (20000 polls; about 400 seconds in the port). Needs a real
  machine and stick.
- (#23) A cleric on Auto against undead (see The computer's turns):
  `3afb:024b` takes the highest kind to turn from a stack byte its calls
  leave, the way of moving's last die's sides, 2 or 4. Put a cleric of
  level 5 or more on Auto (Alt-Q) against undead only of kinds (`+0xda`)
  above 4: the port's never tries to turn them, where the player's `Turn`
  would. Then against undead of kind 1 or 2, which it should try to turn.
  Not run: no fight reached had undead of those kinds on the map (in Sir
  Lebaum's, `d_lebaum.py`, his kind is 0 and the ghasts found no place;
  KAL, cleric 7 on Auto, never tried to turn in either).
- (#85) In the red dragons' fight (above), each terrified or frightened
  character should flash with sparkles, at speed 4 five times, 70 ms a
  picture, its name and "is terrified" or "is afraid" in the panel from
  row 10, and the text then cleared. A person's judgement (the flashing's
  timing).
- (#86) Have a character drop in combat: a skull should flash over it nine
  times, red and pink, then its body stay on its cell. A person's
  judgement (the timing).
- (#87) Shoot at a monster more than 6 cells away and out of the view: the
  arrow should fly to the view's edge, then the screen move to the target
  and the arrow come in from the edge near it. A person's judgement (the
  flight and the screen moving).
- (#89) Throw a hand axe until none is left and win: it should be in the
  treasure pool; darts and javelins thrown to the last should not (no
  character of `SAVE/`'s games carries one; a type patched into a `.STF`
  would do). That one picture of the arrow serves its whole flight is for
  a person to see.
- (#121) Cast Fear at an empty cell with no one on the line to it (and on
  past it, 6 cells in all), a creature on the line from the caster to 4
  cells east of the line's end and none on the line to the end itself: the
  creature should run in terror (the stack byte 0xc6, which needs DOS to
  load the game at a PSP of 0xnnf0: note DOSBox's, the segment `mem` shows
  for the game, or `DS - 0x0bd6`). Repeat in EGA and CGA mode. Not run: no
  fight reached gave such a cell without a search; `d_fear.py --psp` loads
  the game at a chosen PSP, and #122 shows the side line depending on it.
- (#125) In EGA mode, a draw of no width on a row of the 3D view or the
  combat map that nothing else marks (a picture placed just past the right
  edge, `127f:09c3` copying no byte): `127f:12e8` should copy 16 KB of the
  off-screen page A200 onto the screen from that row, where the port shows
  nothing new (it does not model the dirty rows; no run met one). Not run:
  no run is known to reach such a draw.

The port differences DOSBox-X found and the port has since fixed (the
checked list's #27, #29, #38, #70, #90, #123 and #124):

- Wall tiles and sky pictures are masked with colour 13 in the 3D view.
  Compare a view, such as Throtl's street from 7, 15 facing north, for light
  magenta or holes. Checked in DOSBox-X (`tools/dosbox/checks/c_throtl.py`
  on the `dosbox` branch): no light magenta and no holes, but the far
  walls differed, a two-set WALLDEF record's second set offset again (see
  3D view); the port now offsets every set by the first slot's and agrees,
  0 pixels in every screen at a key read of `c_throtl.py`, `c_pit.py`,
  `c_chest.py`, `c_npc.py` and `c_temple33.py`.
- Load saved game A and compare the party list and status line beside the
  view: the columns of AC and hit points, the colours, and `7,13 N 00:00`.
  Partly done (`tools/dosbox/checks/load_game_screen.py`, in Tandy, EGA and
  CGA mode): the start menu with game A's party is the port's pixel for
  pixel, and so is Solace's first page after Begin Adventuring but for row
  24: the single-item menu's text there (`2fd3:116d`, the string at
  `2fd3:1146`) is `PRESS <ENTER>/<RETURN> TO CONTINUE`, without the full
  stop the port adds (`src/adventure.c`), which it draws black on white at
  column 34. Fixed: the port now writes it without (2fd3:1146), and
  Solace's first page is the original's pixel for pixel; the status line
  beside the view, `7,13 N 00:00`, agrees too (the checked list, #29).
- Save in camp, then load that game: it holds mode 2 (`0x1407`). Compare
  how the original resumes it with how it resumes a game saved from the
  party menu. Checked in DOSBox-X (`tools/dosbox/checks/b_resume.py`):
  besides Throtl's walls (above), the port set the last mode as the first
  block began, so the game saved in camp again held 4 at `0x1406` where the
  original's holds 2; and DOSBox-X checks of saves on the overland map and
  on squares with events (`d_firstblock.py`, `c_overland.py`, `c_npc.py`,
  `f_display_modes.py`) found the port running the first block's
  after-move and location vectors at Begin Adventuring (asking `YOU ARE
  NEAR THROTL. DO YOU ENTER?` again, a roll ahead), which the original
  runs only after the first command (see Adventure loop). The port now
  does as the original: the three games resume with 0 pixels differing,
  their saves again the same (modes 2, 2 and 4, 2), and RandSeed and the
  step counter untouched until the first command.
- In CGA mode (`KRYNN.CFG`'s first line `C`): compare the title, the
  start menu, character creation, the overland map, combat, a 3D view and
  the camp with `eclplay --display cga --shots`, colour for colour; the
  palette should be black, light cyan, light magenta and white (the
  BIOS's palette 1 with intensity: on a CGA without it, or a monitor
  without intensity, the dark ones), the 3D view's ground cyan under a
  black sky with a white line and a magenta horizon (`DS:6d81`-`6d83`
  from 0xc05f 9 and 0xc059 0x41 on entering a block). In the icon
  editor, 1st-color should say `Sorry, not in CGA`. Checked in DOSBox-X
  (`tools/dosbox/checks/f_display_modes.py`): all agreed but Throtl's 3D
  view, whose far walls differed in every mode alike (the wall sets'
  offset, see the first of these checks); the port now agrees there too,
  0 pixels in Tandy, EGA and CGA mode.
- In EGA mode, the same screens should match `eclplay`'s (the Tandy's).
  Checked in DOSBox-X with the CGA check above: the same, Throtl's view
  included now.
- In a temple's `Heal`, buy a cure: on the list's return, which rows show
  and which is highlighted. Checked in DOSBox-X
  (`tools/dosbox/checks/c_outpost.py`, Raise Dead bought): the original
  shows all ten cures again, the row picked highlighted, where the port
  showed only that row (3,227 pixels): it redrew the frame each pass. The
  port now draws the frame once, as `340d:0be1` does (`0c81`, before its
  loop), and agrees, 0 pixels in every temple screen (#70).
- Kill a bozak beside two characters: "explodes." and each beside it
  "takes N points of damage" (1-6). Checked in DOSBox-X
  (`tools/dosbox/checks/d_bozak.py`): as the port; but the route ends with
  the party destroyed, whose text ("The monsters rejoice ...") the
  original types a letter at a time, the speed's delay between letters,
  where the port printed it at once. The port now keeps the original's
  text delay flag (`DS:4b59`, see Library) for every running text and
  types it, its frames showing the text grow as the original's
  screenshots do (#90).

## Disassembly image

`make merged` writes `build/START_FULL.EXE` and `build/START_FULL.map` with
`tools/ovrmerge.py` (Python 3, standard library only); `make test` and
`make sanitize` build them too. The game logic lives in
`GAME.OVR`, a Turbo Pascal overlay file (`FBOV`) that `START.EXE` loads through
35 `INT 3Fh` stub segments, with 506 entry points in all. The tool unpacks
EXEPACK, appends each overlay segment from paragraph `0x1f48` (above the stack
and heap the program reserves), turns the overlay fixups into MZ relocations,
and rewrites every stub entry as a `JMP FAR` to its routine. The stubs cover
`GAME.OVR` exactly; the tool stops if any byte is left over.

Load `START_FULL.EXE` in Ghidra as an MZ executable. Its addresses are the
unpacked ones, so `521:04ac` is `1521:04ac` at Ghidra's default load segment
`0x1000`, with no `0x239` correction. The map lists each overlay's stub, load
segment and `GAME.OVR` offset, and where each stub entry jumps. The output is
for disassembly only and has not been run under DOS.

The sequels get the same treatment, each with its own target:

| Target | Input | Output |
| --- | --- | --- |
| `make merged` | `COKAssets/START.EXE`, `COKAssets/GAME.OVR` | `build/START_FULL.EXE`, `.map` |
| `make death-merged` | `DKKAssets/START.EXE`, `DKKAssets/GAME.OVR` | `build/death-START_FULL.EXE`, `.map` |
| `make dqk-merged` | `DQKAssets/DQK.EXE` | `build/DQK_FULL.EXE`, `.map` |

`DEATH=dir` and `DQK=dir` read the files from elsewhere. Each game's Ghidra
output (`*.EXE.c`, `*.EXE.h`) is kept beside its files.

Death Knights of Krynn is built the same way as this game: an EXEPACK
`START.EXE` and a `FBOV` `GAME.OVR`, 36 overlay segments with 475 entry
points, appended from paragraph `0x25b9`. `tools/ovrmerge.py` also reads a
`START.EXE` that is not packed.

The Dark Queen of Krynn has no `GAME.OVR`: its overlays sit in `DQK.EXE`
itself, after the part DOS loads (file offset `0x218e0` to the end), and a
different overlay manager loads them, in the entry point's segment `1d99`.
`tools/dqkmerge.py` finds its table (`1d99:0c1f`, 13 entries of 18 bytes,
count at `1d99:0c19`): each entry gives the load segment, the file
paragraph (relocations as offset/segment pairs, then the code from the next
paragraph), the paragraphs in memory and read from the file, the relocation
count, the parent overlay and a flag for overlays loaded at start. Overlays
share load areas as a tree (children above their parent, siblings at one
segment), so a segment number in an overlay means that overlay or one of
its parents. Its 182 routines are called through 10-byte stubs in `1d99`,
`CALL 058d` / `JMP FAR seg:off` / `DW overlay`.

The tool leaves the root image and overlay 12 where they run: overlay 12 is
loaded at start and is the data segment and stack (`322e`, so `422e` in
Ghidra, as in `DQK.EXE.c`). Every other overlay gets a segment of its own
from `0x4045`; their relocations are rewritten to the copy each segment
number means, the table's load segments to the copies, and each stub
becomes a `JMP FAR` to its routine. Every stub lands just after a `RETF`,
154 of them on `PUSH BP / MOV BP,SP`. The map lists where each overlay runs
and where it was put, every stub's target, and any segment number in an
overlay area that no overlay owns (only `1d99:0c0f`, the top of memory,
`4024`).
