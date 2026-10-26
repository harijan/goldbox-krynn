# Adventure

The 3D view, the overhead map, the adventure loop, the overland map, monsters and encounters. Part of the
[developer notes](../DEVELOPERS.md).

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
descriptor's `view` (see [Games](games.md#games)); the values below are Champions', and
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
transparent. The backdrop (`69ea:0184`, outside CGA mode; see [Display modes](front-end.md#display-modes)
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
[Party](party.md#party)).

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
the search bit is restored. `Encamp` camps (`2fd3:3403`, see [Camp](camp.md#camp)).
After a command, text that `PRINT` or `VERTICAL MENU` left in rows 17-22 is
cleared (`DS:884e`). Other special keys pick a character (`546c:3334`, see
[Party](party.md#party)) and redraw the party list and status line, which are also redrawn
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
party's spell effects (`57e4:0171`, see [Spell effects](party.md#spell-effects)).

`Cast` (`475c:0ade`) casts for the selected character if its status is
0 (see [Casting](camp.md#casting)); with none selected the original reads through NULL and
the port stops. `View` shows the selected character (see [View](camp.md#view)). `Area`
shows the overhead map (see Overhead map). Outside 3D areas the loop
travels the overland map (see The overland map). A step forward plays
sound 10 as it starts (`475c:081a`) and again once the view shows the new
square (`2fd3:3eb9`, when the party's square changed or is below 0); a
turn plays it (`475c:0c58`, `0ca6`); see [Sound](sound.md#sound).

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
otherwise lasts through steps and blocks. `Encamp` camps (see [Camp](camp.md#camp)).
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
(`2fd3:3403`, see [Camp](camp.md#camp)) in the middle of the script, which goes on after
it (`DS:4b43` kept), and then ends the script as `EXIT` does
(`2fd3:0050`) unless `0x4c38` is set: the knights' camp, the inn of
ECL1 block 17, and ECL3 blocks 96 and 97. 0 opens the start menu
(`4def:01b4`, see [The start menu](start-menu.md#the-start-menu)), which the scripts' training halls
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

## Monsters and encounters

`src/monster.h` ports the ECL opcodes that load monsters and start
encounters, from overlay `2fd3` and their helpers in `3775`, `4b6d`, `6961`
and `6d21`, and the wrapper of `COMBAT` without the battle.

A monster is a 409-byte character record from `MON<file>CHA.DAX`, with its
effects (`MON<file>SPC`, 9-byte `.SFX` records) and items (`MON<file>ITM`,
63-byte `.STF` items). What a monster keeps in the fields of Champions'
layout (`champions_character` in `src/rules.c`, see [Games](games.md#games)):

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
CGA mode (`6d21:01d0`, `DS:4b76`; see [Display modes](front-end.md#display-modes) and [The combat screen](combat.md#the-combat-screen)).
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
a portrait (`3775:0538`, see [Playing scripts](scripts.md#playing-scripts)), logged as `monster:
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
[Playing scripts](scripts.md#playing-scripts)).

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
  [Shops and the temple](treasure-shops.md#shops-and-the-temple));
- else runs the end of combat, for treasure.

Then the mode is 4, or 3 outside 3D areas, var `0x7eca` keeps only its
search bit, the sprite and picture are forgotten (`DS:8830`, `884a`) and,
unless a shop or the temple closed outside 3D areas, the screen is redrawn
for the mode (`6346:2c17`), and outside 3D areas, unless the run ended or
`0x4c38` is set, the party is marked on the overland map (`4877:0005`,
see The overland map). The preloads of overlays (`XXXX:0000`,
`432f:1e96`) are left out; the sound driver's uninstall and install
around them are made (see [Sound](sound.md#sound)): before the battle, around the end of
combat (which runs with the driver off), around a shop's, the temple's or
treasure's overlay, and after them.

The battle (`3995:0172`) sets up the battlefield (`3cb2:1c58`, see [The
battlefield](combat.md#the-battlefield)), which builds the map, gives every record its combat record
and places them, removing monsters with no place, then fights its rounds
(see [The rounds](combat.md#the-rounds)), in which the computer plays the monsters, the NPCs and
party members on Auto (see [The computer's turns](combat-turns.md#the-computers-turns)) and the player's turns
take the player's commands from the keys (see [The player's commands](combat-turns.md#the-players-commands);
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
`Helm` (`432f:41e2`); `melee` has every turn attack (see [Attacks,
movement and death](combat-turns.md#attacks-movement-and-death)); `auto` has the player's first turn press Alt-Q,
putting the party on Auto, so that the computer plays every turn; and
`pass` has every turn pass, the computer's too, so that fifteen rounds
without an attack end it. Then the battle's end (`3995:004b`) and the end of combat (`351b:1968`) run, after
every battle and for treasure alone (see [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)):
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

Death Knights (the descriptor's `combat` section, see [Games](games.md#games)) always sets
an encounter up as `SETUP MONSTERS`, `CLEARMONSTERS`, `LOAD MONSTER`s and
`COMBAT`. `SETUP MONSTERS` (opcode `0x0c`, `dkk 3632:038f`) is `SETUP
MONSTER` with a fourth operand, the directions an ambush comes from
(bits 0-3, a quadrant each: `DS:c4cc`, `game->ambush`), which the
placement reads. Its show at distance (`3c1e:0515`, `combat.show_none`)
takes a sprite or close-up of `0xff` for none, shows the close-up at any
distance where there is no sprite, and has no portrait (the game has no
`0x7ee1`). `LOAD MONSTER` (`3632:0472`) is Champions' on its own records
(see Games' [Death Knights](games.md#death-knights): `46b2:19a2`), the icons from `CPIC1`
(`7090:016f`). `COMBAT` (`3632:16ec`) with no monsters loaded tries the
vault's menu first (var `0x4d00` 1, cleared: `3948:000e`), then a shop
(`0x7f6c`: `3b89:06f3`) and the temple (`0x7ee2`: `3999:07d3`; see [Death
Knights' shops](treasure-shops.md#death-knights-shops); the vault's `vault_unported` is none: see [Death Knights'
treasure](treasure-shops.md#death-knights-treasure)), else the end of combat;
after a battle off the 3D map it shows `PICTURE`'s overland map
(`view.overland_picture`, `0x70`; Champions' `0x79`).
