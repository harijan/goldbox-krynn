# Spells in combat

Spells in combat; clouds, pools and breath. Part of the
[developer notes](../DEVELOPERS.md).

## Spells in combat

`src/cast.h` ports casting in a battle: the combat part of the cast
(`5b04:1415`, with `5b04:57eb`), the targets in combat (`432f:2337`, which
the battle puts in `DS:6e3a`, with `432f:210b`, `432f:1eed` and
`5b04:0ecb`), the apply routine's touch (`5b04:1071` with `60f4:1062`),
the handlers of the spells cast only in combat (table `DS:6e3a`, filled
by `5b04:58ed`) with their helpers `5b04:1cbd` (missiles), `1e34` (sides),
`39bb` and `3a6f` (bolts) and `1903` with `17aa`, `17eb` and `1836`
(cones), and effect 0x47 turning a spell back (`3f44:23a4`). Cast
(`432f:28bd`, see [The player's commands](combat-turns.md#the-players-commands)), a spell begun at an earlier
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

Each pick (`432f:210b`): the player aims (`432f:3b80`, see [The player's
commands](combat-turns.md#the-players-commands): not attacking, an empty cell allowed for an area, a creature
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
armour class (`60f4:1062`, see [Attacks, movement and death](combat-turns.md#attacks-movement-and-death)); a miss
counts as saved and leaves no damage, for that target and those after it.
Each step reads the target from the list again. Damage in combat is
`60f4:1db7`'s (see [Attacks, movement and death](combat-turns.md#attacks-movement-and-death)): the flash, a hurt
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
handlers when they end (see [Spell effects](party.md#spell-effects)), and Sir Lebaum's 0x4a casts
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
