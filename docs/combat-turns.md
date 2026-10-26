# Combat turns

Attacks, movement and death; the computer's turns; the player's commands. Part of the
[developer notes](../DEVELOPERS.md).

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

Effects whose handlers the attacks reach are ported with them (see [Spell
effects](party.md#spell-effects)): poison on a hit (0x40, 0x41, 0x56, 0x57, `3f44:1b55`: a saving
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
(`351b:185f`, see [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)).

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
  experience for magic items stops there (see [Treasure and the end of
  combat](treasure-shops.md#treasure-and-the-end-of-combat)).
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
bolts of overlay `5b04`, is ported with the clouds (see [Clouds, pools and
breath](combat-spells.md#clouds-pools-and-breath)); casting in combat and turning a spell back (0x47) with the
spells (see [Spells in combat](combat-spells.md#spells-in-combat)).

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
(`432f:1eed`, see [Spells in combat](combat-spells.md#spells-in-combat)) comes after the priority, has
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
spell" and forgets another of it. See [Spells in combat](combat-spells.md#spells-in-combat).

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
[Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)); one on the
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
[Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)). Guarding (`3afb:118e`): one not helpless, without
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
logged and ending the turn (the casts are now carried out: see [Spells in
combat](combat-spells.md#spells-in-combat) for the test that runs them). Parties of one to six of `SAVE/`'s
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
`17f9`, `24d7`, see [View](camp.md#view)). The battle's turn (`3995:040b`) hands a
combatant the player controls (`+0x18b` `COMPUTER` 0) to it
(`cok_combat_commands`). Keys come through the menus on row 24 (`67b5:03e2`,
see [Library](library.md#library)); a click on the map in Move (`DS:71a6`) or Manual's cursor
(`DS:71a5`, at `DS:71a3`, `71a4`) is a step toward the cell (see [The mouse
and the joystick](front-end.md#the-mouse-and-the-joystick)).

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
the debug flag is set (`DS:4b51`, see [Debug mode](scripts.md#debug-mode)), cut to 40 characters. It
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
or puddle under it acts (`60f4:0dc3` with its flag, see [Clouds, pools and
breath](combat-spells.md#clouds-pools-and-breath)), and one that dropped or is helpless ends its turn. Escape first
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
[The combat screen](combat.md#the-combat-screen)); an item type past `ITEMS`; a record with no combat
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
Use are Death Knights' (`580d:0c66`, `0ffa`; see [Death Knights' View](camp.md#death-knights-view)).

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
