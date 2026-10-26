# Combat

The battlefield, the rounds and the combat screen. Part of the
[developer notes](../DEVELOPERS.md).

## The battlefield

`src/combat.h` ports combat setup (`3cb2:1c58`) with the rest of overlay
`3cb2`, the combat tile loader (`6d21:002c`), the map's lookups of overlay
`6beb`, `6346:268a` and `432f:2df3`, and keeps the battle's map and
tables in `cok_adventure.combat` (`cok_combat`) for the rest of combat
(see The rounds and after).

Setup runs with the mode already 5, as `3995:0172` sets it first. It
clears the text delay flag (`DS:4b59`, see [Library](library.md#library); the battle's end
sets it again, `3995:0297`), frees the small picture (`6961:0537`), the
big one (`DS:6e02`) and the portrait's pieces (`6de5`, `6dea`, marking
none held; see [Playing scripts](scripts.md#playing-scripts)), clears row 24, waits speed × 100 ms and prints "A battle begins..." there in
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
traced last (`3cb2:1e13`, see [Debug mode](scripts.md#debug-mode)); the lists of clouds and pools
(`7111`, `7119`;
`7115` is cleared and never used) are emptied (see [Clouds, pools and
breath](combat-spells.md#clouds-pools-and-breath)).

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
hook (see [Playing](front-end.md#playing)).

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
[Monsters and encounters](adventure.md#monsters-and-encounters)) gets its facing and `+0x13` at setup.

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
[Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)); then each record charmed (effect 0x0b) and okay
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
(see [Spells in combat](combat-spells.md#spells-in-combat)). Of 4,000 lookups, the 3,619 the port carries out
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
[Checks against the original](checks.md#checks-against-the-original)).

## The rounds

`src/round.h` ports the battle (`3995:0172`) with the rest of overlay
`3995` but the player's commands, the start of a round for each record
(`432f:0000`, `014d`, `0e80`, `0f9e`), the lines, sight and ranges of
overlay `6b30`, and the helpers of overlays `432f`, `6346` and `60f4` that
the later parts of combat share. Each turn is logged as `turn: NAME
(initiative N)`; the computer's (`3afb:004b`, see [The computer's turns](combat-turns.md#the-computers-turns))
and the player's commands (`3995:0573`, see [The player's commands](combat-turns.md#the-players-commands)) are
ported; with no keys the player's turn ends (`6346:2964`). The screen is
drawn as the battle goes (see The combat screen, and below).

The battle (`cok_combat_battle`), as `COMBAT` starts it: the mode becomes
5, spells pick their targets with the combat routine (`DS:6e3a` holds
`432f:2337`, see [Spells in combat](combat-spells.md#spells-in-combat)), the battlefield
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
(`60f4:0dc3` with no flag: 1d6 through `60f4:1db7`, see [Clouds, pools
and breath](combat-spells.md#clouds-pools-and-breath)); a dying one (status 5) counts a round
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

In a battle the effects (see [Spell effects](party.md#spell-effects)) are run with the combat map:
an effect the party shares (0x15, 0x2d, 0x2e, 0x31) counts for a target
only from a holder that has it within 6 cells (Prayer) or 1 (the others),
as `60f4:0352` lists them (`6b30:08d8`, with the list's entries put back
after, but not its count). The handlers that wait for a battle are
ported with it (see [Spell effects](party.md#spell-effects)):

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
| 0x47 (spell turning) | `23a4` | a d10; on 1 for a spell of targets below 8, or always above 14, the spell is turned back on its caster (see [Spells in combat](combat-spells.md#spells-in-combat)) |
| 0x48 | `24f4` | each enemy within a cell (`6b30:08d8`) without 0x5c that fails a saving throw of type 4 "is terrified" (0x6f for good) and flees |
| 0x4b | `26bf` | against a target with `+0x140` bit 0, damage 3 times a d12 + 4 + strength's damage bonus, as a byte, and the attack roll 2 better |
| 0x4f | `2b5f` | "emits an evil stench"; each enemy within a cell that fails a saving throw of type 4 and lacks 0x76 "is affected" |

Outside a battle those that write a combat record (`+0x183`) or use the
map stop the run, as the original's would write through records it has
freed. Those that only read it, Molly's 0x07 and the slayers' 0x69, 0x72
and 0x73, run as an item that gives one is unreadied (`3f44:3888`), read
their target through a combat record nil before any battle (`0000:000a`,
the interrupt table) or the one the last battle freed, and set only the
THAC0, which the items menu's recompute (`6346:0d20`) sets again, and an
attack's amount and target, which the next attack sets before reading:
the port rolls 0x07's die (a d6 or a d4 by the distance to whatever the
pointer reads, one Random either way) and keeps nothing else
(`e_kender.py`: the Hoopak unreadied, RandSeed one step on in both). 0x6f
(terror), which writes `+0x10` of it, lasts only through the battle
(`60f4:1440` takes it off while the records are there), so its stop
outside one needs a second 0x6f on a record. Monsters carry effects that run every turn, and the timers run at
the end of each round (a minute): every handler they reach is now ported,
none is skipped. Sir Lebaum's 0x4a rolls a d20 at event 0x0e of each of
his turns and on 1 casts Dispel Magic on what the last cast left (see
[Spell effects](party.md#spell-effects)); the timers 0x0f, 0x16, 0x22, 0x2b, 0x2c and 0x3e hurt,
weaken, kill and heal as their effects run out (`eclplay --effect` gives
them for a test). 0x30, 0x3c, 0x42, 0x4c and 0x6a are ported with the
attacks, the clouds', pools' and the monsters' breath and attacks (0x28,
0x46, 0x50; 4, 6, 0x4e, 0x53-0x55, 0x5a, 0x68) with the clouds and breath
(see [Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)). The handlers the attacks reach
are ported with them (see [Attacks, movement and death](combat-turns.md#attacks-movement-and-death)). Each effect that
ends in a battle, by its timer or the battle's end, is logged, `effect:
NAME loses 0x77`. The dead that explode (effect 0x44) explode after
every attack and every turn (`60f4:2375`, see [Attacks, movement and
death](combat-turns.md#attacks-movement-and-death)), which clears the damage type (`DS:6b31`) all the same.

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
reaches the menu (see [The player's commands](combat-turns.md#the-players-commands)), as the original allows with
`Helm` on its command line: `432f:41e2` tests `ParamStr(2)` against `DS:896a` and returns at
once without it, so the cheat needs `cok_adventure.helm`, which `--combat
gods` sets (see [Overhead map](adventure.md#overhead-map)).

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

Attacks, movement and death are ported (see [Attacks, movement and
death](combat-turns.md#attacks-movement-and-death)), and so are the computer's turns, the player's commands, the
spells and the clouds (see [The computer's turns](combat-turns.md#the-computers-turns), [The player's commands](combat-turns.md#the-players-commands),
[Spells in combat](combat-spells.md#spells-in-combat) and [Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)); `eclplay --combat melee`
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
the aging (`3e05:0264`, `cok_train_aging`; see [Death Knights' training](start-menu.md#death-knights-training)).
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
  one list), and its cloud square (`638c:0c65`) is rewritten (see [Death
  Knights' clouds](combat-spells.md#death-knights-clouds)). Its cloud and pool tiles, Stinking Cloud's and Cloud
  Kill's, are 0x1d and 0x1c, marked wherever they are and counted as
  terrain too (`6f03:0e33`).
- The player's turn (`3e05:08af`): see [Death Knights' commands](combat-turns.md#death-knights-commands). A spell
  being cast goes to its cast entry (`5dc5:1298`, see [Death Knights'
  casting](camp.md#death-knights-casting)).
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
not ported (see [Death Knights' effects](party.md#death-knights-effects)).
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
`1e36`). LOAD MONSTER's icons now load so (see [Monsters and encounters](adventure.md#monsters-and-encounters)).
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
colours; see [Creating characters](start-menu.md#creating-characters)).

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
