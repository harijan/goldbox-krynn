# The party

The party, derived stats and spell effects. Part of the
[developer notes](../DEVELOPERS.md).

## Party

`src/party.h` holds the party and its characters. The original keeps the
party as a linked list of 409-byte records from `DS:609a` (next at `+0x17f`)
with the selected character at `DS:6096`; the port keeps an array in the same
order, up to 72 records with the monsters `LOAD MONSTER` appends after the
party (see [Monsters and encounters](adventure.md#monsters-and-encounters)), and the selected record in
`vm.character`. The record's fields are named in the game's layout
(Champions' in `src/rules.c`, see Games' "[The character record's
layout](games.md#the-character-records-layout)"), through which code reads them. Fields such as the armour class
(`+0x18d`, as 60 - AC) are derived from the others and the items when a
character loads (see Derived stats); its spell effects are a list (see
Spell effects).

A saved game, `SAVGAM<letter>.DAT` (`4b6d:1b34`; the camp's Save writes
one, see [Camp](camp.md#camp)), is 5,469 bytes: the ECL file, the variables
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
record gets its combat icons (`4b6d:1b34`, see [The combat screen](combat.md#the-combat-screen)). Loaded
from the start menu, the roster's copies of the characters (`.WHO`, `.STF`,
`.SFX` named after them, `4b6d:0a80`) are then erased (see [The start
menu](start-menu.md#the-start-menu)); `eclplay --load` keeps no roster. The names are made as the
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
command (see [Adventure loop](adventure.md#adventure-loop)); the last mode stays the loaded one until a
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
its characters are `.GAM` and their files (see Games' [Death Knights](games.md#death-knights)); the
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
`TMPVAULT.DAT` when left: see [Death Knights' treasure](treasure-shops.md#death-knights-treasure).

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
hides it (`0x4bfb`, see [Overhead map](adventure.md#overhead-map)), the facing (turned by `0x4cff`), the hour and minutes,
and `search` while searching, or `camping` in camp, with `*` after the
time while the debug flag is set (`6346:2f4c`, see [Debug mode](scripts.md#debug-mode)). The list
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
(`4b6d:161b`, see [Monsters and encounters](adventure.md#monsters-and-encounters)), `id` kept in `+0x115`
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
(see [Attacks, movement and death](combat-turns.md#attacks-movement-and-death)).

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
layout (`champions_character` in `src/rules.c`, see [Games](games.md#games)), the items'
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
tables: see "[The data segment's tables](games.md#the-data-segments-tables)") against `build/START_FULL.EXE`.

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
saving game A, sets it (see [Checks against the original](checks.md#checks-against-the-original)), so those saves
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
(`546c:2178`, `546c:32b0`) recompute, as ported (see [View](camp.md#view)), and so do
spells by touch, before the attack roll (`5b04:1071`, which no spell cast
outside combat reaches; see [Spells in combat](combat-spells.md#spells-in-combat)).
Creating, training, modifying and changing the order of a
character (`4def:06dd`, `4def:4d9e`, `4def:28fa`, `4def:567f`) are ported
(see [Creating characters](start-menu.md#creating-characters) and [Training](start-menu.md#training)), and so is the computer's choice of
weapon in combat (`3afb:1608`, see [The computer's turns](combat-turns.md#the-computers-turns)). Combat
setup (`3cb2:10d9`) recomputes every record (see [The battlefield](combat.md#the-battlefield)), each
combatant's turn (`3995:040b`) the combatant (see [The rounds](combat.md#the-rounds)), an attack
(`432f:1a45`) the attacker and a strike (`432f:1579`) the target (see
[Attacks, movement and death](combat-turns.md#attacks-movement-and-death)), the end
of combat (`351b:1968`) every record left, taking an item (`36d0:034c`,
which buying does too) the taker, appraising gems (`58e7:1929`) the
appraiser after each key, `DESTROY ITEMS` (`2fd3:35a3`) every
record, and `ADD NPC` the character selected, as ported (see [Treasure
and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat), [Shops and the temple](treasure-shops.md#shops-and-the-temple), [The overland map](adventure.md#the-overland-map) and
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
cells (Prayer, 0x31) or 1 (the others) on the map counts (see [The
rounds](combat.md#the-rounds)). Events 6 and 9 first check magic
resistance (`60f4:04f3`) when the target has some (`+0x187`), an effect
is pending (`6b2f`) and no damage is, or magic damage (`6b31` bit 8): a
d100 up to the resistance plus 5 for each level the caster (the selected
character, `cok_effects_caster_level`, see [Casting](camp.md#casting)) has of the spell
(`6b33`) below 11, as a byte, clears the damage and the effect (but for
0x5b and 0x52). Handlers come from the table at `DS:6b94` (the game's
`effects.handlers`, see [The spells and effects](games.md#the-spells-and-effects)) that
`5b04:58ed` fills at startup (`3e99:0835`), and `3f44:38ea` after it
(`3e99:083a`), and get the flag (0 from the
dispatch, 1 on removal), the effect (the holder's, for a shared one) and
the character. While `DS:713c` is set, which readying an item and Remove
Curse do, `60f4:01a8` calls `3f44:3888` instead to add or remove the
item's effect (see [View](camp.md#view)).

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
`3619` (0x6c), `363c` (0x6d), `3643` (0x6e), `3692` (0x6f, see [The
battlefield](combat.md#the-battlefield)), `303c` (0x52, see [The battlefield](combat.md#the-battlefield)), `3768` (0x71), `37e8`
(0x74), `386a` (0x76), `3876` (0x77) and `3881` (5, 0x13, 0x18, 0x5c). Many
do nothing but mark the character for other code, and all but a few act the
same when their effect ends. 0x5d, 0x64 and 0x67 look at what the selected
character strikes with (`3f44:13a9`): its readied weapon (slot 0), or for a
missile weapon its readied arrows or quarrels (`6346:3111`, by the weapon
type's flags), and 0x65 at the weapon itself. Handlers that print speak
through the `say` hook of `cok_effects` (`6346:1883`), and those that add an
effect with text through its `flash` hook (`6346:228c`, see [The combat
screen](combat.md#the-combat-screen)):
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
battle (see [The rounds](combat.md#the-rounds)): `00f7` (0x1b, 0x1f, 0x33-0x35), `016a` (3),
`06b2` (0x15), `09e8` (0x1a), `0a2f` (0x1c), `0ae0` (0x1e, the stinking
cloud), `0d7c` (0x23), `1412` (0x29), `23a4` (0x47; turning a spell
back with the spells in combat), `24f4` (0x48), `26bf` (0x4b) and `2b5f`
(0x4f); with the attacks (see [Attacks, movement and death](combat-turns.md#attacks-movement-and-death)): `0208` (0x07),
`04c2` (0x0d), `0bc9` (0x20), `0f50` (0x25), `1b55` with `1c21`, `1c34`,
`31bc` and `31cf` (0x40, 0x41, 0x56, 0x57), `1dc5` (0x43), `1f97` (0x44),
`000d` with `200b`, `3009` and `31e2` (0x45, 0x51, 0x58), `349c` (0x69),
`36e1` (0x70), `376f` (0x72), `37b0` (0x73), `37fc` (0x75), `15cd` (0x30),
`18b9` (0x3c), `1c54` (0x42), `272a` (0x4c), `34db` (0x6a), and the
combat parts of `03cd` (0x0b taking hold) and `29d5` (0x4d); with the
clouds and pools (see [Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)): `0ff7` (0x28), `2037`
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
[View](camp.md#view)), which takes the effect it is given for an item and adds or removes
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
[Camp](camp.md#camp)) ran the rest (`57e4:0e3e`) and the clock (`57e4:0549`) with those
effects on the party: of 5,000 cases none differed (4,470 carried out),
with 319 deaths from poison, 1,662 heals, 2,770 weakenings and 8,381
points of poison or wasting; again the rest stopped at the walk's freed
effects.

Whole battles (`3995:0172`, as for the clouds and breath, see [Clouds,
pools and breath](combat-spells.md#clouds-pools-and-breath)) were run with 0x4a given to some monsters (and to Sir
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
(see [Death Knights' casting](camp.md#death-knights-casting)).
