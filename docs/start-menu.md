# The start menu

The start menu, creating characters and training. Part of the
[developer notes](../DEVELOPERS.md).

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
(`1661`, see [Sound](sound.md#sound)).

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
| V | Pick Character, then [View](camp.md#view) (`546c:0d74`, see [View](camp.md#view)) |
| A | Add Character to Party (`4def:37a2`) |
| R | Pick Character; a player character is saved to the roster (`4b6d:0bed` with no base) and leaves the party (`4def:3b0a` 0, 1) whether it was saved or not; an NPC is dropped (`4def:2668`, which picks again) |
| L | Load Saved Game (`4b6d:1b34`) |
| S | Save Current Game (`4b6d:22de`, see [Camp](camp.md#camp)), with no rule-book question |
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
the script goes on (see [The overland map](adventure.md#the-overland-map)). A game loaded in the start
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
joystick` for 2 seconds on failure, see [The mouse and the joystick](front-end.md#the-mouse-and-the-joystick)); still
on (a stick found, calibrated or not), `J` and its thresholds, the mouse
off; else `N` and zeros, the mouse as it was. `disable both` turns both off
and writes `N`. The calibration's text stays until the start menu draws its
frame again. The port writes the file (`cok_config_rewrite`, logged
`config: line 3 M`, `J` or `N`) where the setup would (see [Startup and the
setup](front-end.md#startup-and-the-setup)): read from the assets, it is written to the saves directory, which
later runs read first.

The demonstration (`DS:4b4b`): main sets the ECL file 2 and speed 9 and
skips the start menu; `2fd3:3c28` loads ECL2 block 0x39 and runs only its
load vector, which is the whole demonstration (three NPCs joining, walks,
a fight, the dragon's close-up), with no party list, then removes every
member (`4def:3b0a` with 1, 1) and returns. Meanwhile a key read with
none waiting returns 0 (`1614:025b`), so `PRESS ... TO CONTINUE` and
paging do not wait; `CLEAR BOX` does not redraw the small picture
(`2fd3:3086`); and the end of combat skips the party's part and stops
once the enemies are removed (see [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)), with
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
  `NULL`): `cok` and `eclplay` refuse `--woof` for it (status 2). The
  title starts song 1 (`MUSIC.DAX`, see [Death Knights'
  songs](sound.md#death-knights-songs)), which plays on through the
  title's menu until main's loop stops it (`1000:015b`); main hooks no
  effects driver after the title. The title's waits and its menu's
  timeout go by `GetTime`, which the songs' driver makes four times as
  fast: with a sound device, the seconds below pass in a quarter of the
  time (the credits 42.4 seconds, the menu 7.5), with the sound off in
  full. The title's menu `Death Knights Of Krynn 1.10 ` and
  `Play Demo`, by index (`1000:0118`, `021c`: `6a69:00cb` with 1, no
  keypad or picture; `cok_menu_pick_timed`): the time running out (30
  seconds, after a demonstration 10, `DS:c0fa` from the menu's start, keys
  typed meanwhile not starting it again) gives `DS:c0fe`'s 1, Demo, as `D`
  and Enter on Demo do; any other result plays, F10 (Champions' Demo, its
  scan code 0x44 its `D`) and Escape too. The demonstration would play
  ECL5 block 2 (`3632:2e06`), which the game does not ship, and stops when
  it cannot load it.
- A new game starts in block 0x10. After the start menu main draws the
  adventure screen always (`1000:01d1`, see [Games](games.md#games)).
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
  Wait`; a space is not special. The list is by index (`49cb:34db`:
  `6a69:00cb` with 0, `1b82` with `Add` and Exit, the last 0): Exit and
  Escape leave, a row picked is tried, and the menu starts on the item
  "from where" left selected (`DS:c0f7`), so after `Champions` (its
  second) Enter is Exit. Nothing else comes back from it, so the loop's
  count of players (`[bp-0x1e]`, set once a row is tried) is never read
  unset.
- The start menu's list and Pick Character (`580d:3067`: `Select`
  alone, 6a69:00cb(1), with the keypad, until an index of 1 or less;
  Escape is 1, another special key picks between redraws) go by index
  (6a69:1b82, 0f1b): a space picks no row, and a click goes as Death
  Knights' (see [The mouse and the joystick](front-end.md#the-mouse-and-the-joystick)).
- Initialize Mouse/Joystick (`52cd`, `58ec`) is Champions' routine with
  its own words (the `start` section's `mouse`: `Init mouse/joystick: `,
  `mouse on` ..., `Push joystick #1 ...`, `unable to read joystick` but
  for a read lost at the upper left, `Unable ...`) and its list by index
  (6a69:1b82 with Exit, so `Select Exit`); the file it reads and writes is
  `krynn.cfg` (CS `52bd`), as Champions' is, not `DKK.CFG`, which the game
  reads at startup: so what it sets lasts until the game is left. The port
  reads and writes a `krynn.cfg` beside where it writes `DKK.CFG` (the
  saves), read from the assets' when there is none there; with neither it
  is written empty, as the original's `Reset` fails (logged `config:
  krynn.cfg line 3 N`). The error stays set (nothing calls `IOResult` in
  between), so the original's next pass does no I/O at all and its file
  stays empty; the port, which does not model the error past its pass (as
  Champions'), reads the empty file and writes five empty lines but the
  third and the zeros (`dkk_mouse.py` saw the original's empty).
  Create (`05c7`) and Modify (`25d4`) are Death
  Knights' creation's (see Creating characters); Train (`43b6`) and Knight
  Change Order (`4ea4`) are Death Knights' training's.

`tools/dosbox/checks/dkk_start.py` (`make dkk-checks`, as
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
there are any), are written as for a saved game (see [Party](party.md#party)). The roster
copy of a character (`4b6d:0a80`, by `cok_party_file_name` of its name:
`.WHO`, `.STF`, `.SFX`) is erased when it is dropped, when a game is
loaded from the start menu (every record) and after each character a
game saves (`4b6d:22de`).

Load Saved Game (`4b6d:1b34`) offers the letters A-J whose
`SAVGAM<letter>.DAT` is there (`Load Which Game: `, light magenta, digits
not directions), and does nothing at all with none; Escape cancels;
special keys count as their scan codes' letters (F7-F10, Home, up, PgUp
as A-D, G-I) when that game is there. Then `Loading...Please Wait` on row
24, the game is loaded (`cok_adventure_restore`, see [Party](party.md#party)) and the
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
  0xnnf0, as the emulator does; see [Spells in combat](combat-spells.md#spells-in-combat)) when the row joins
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
show on cyan without the frame (`4def:4276`, `3d46`; see [Display modes](front-end.md#display-modes)).

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

`tools/dosbox/checks/dkk_create.py` (`make dkk-checks`) compares the
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
Exit` (`DS:3361`): Change promotes whatever the petition said; its menu,
by index (`49cb:5261`: `6a69:00cb` with 1, no prompt, keypad or
picture), takes Change (0) or Exit (1); its special keys do nothing
(`DS:c0f8`) but Escape, which leaves. A knight of no order
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
