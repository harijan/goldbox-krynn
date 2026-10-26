# Treasure and shops

Treasure and the end of combat; shops and the temple. Part of the
[developer notes](../DEVELOPERS.md).

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
   battle's end, see [Library](library.md#library)) in cells 2-37 from row 5, cleared first
   (`351b:1aab`), and "Press any key to continue" shown in light
   magenta on row 24 until a key, and the run ends (typed in the
   original too: DOSBox-X, `d_bozak.py`).
5. The text delay is set (`351b:1b00`), and vars `0x7f70`-`0x7f72`,
   `0x7ee3`, `0x7ee6` and `0x4cf5` are cleared.

The original's path for a duel (`DS:883e`), which is never set in this
game, is left out. In the demonstration (`DS:4b4b`, see [The start menu](start-menu.md#the-start-menu))
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
  character. `View` is View (see [View](camp.md#view)), in which Drop puts the money in the
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
game (see [Party](party.md#party)). Items (`351b:0ec5`, `0df1`)
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
thrown weapon used up, `432f:1a45`); see [Attacks, movement and death](combat-turns.md#attacks-movement-and-death).

In Trade from the treasure menu's View, Escape at the first coin list
tests a byte of the stack (see [View](camp.md#view)). Loading the treasure's picture
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
`DS:0c62` + a space). Detect casts through its cast entry (see [Death
Knights' casting](camp.md#death-knights-casting)); View is the port's View, as camp's. The port reads
these menus by index (`menus.routine`, `cok_camp_pick_menu`; the
keypad and the picture), each index read back as Champions' letter for
the commands, so a space is read past and a click goes as Death Knights'
(see [The mouse and the joystick](front-end.md#the-mouse-and-the-joystick)); the Yes/No is `6a69:2048`'s
(`cok_camp_ask`: `menu: ` with no prompt, special keys read past). Take's
items and money are lists by index (`6a69:1b82`: `3a37:0c26` with `Take`
and Exit, the last 0, only index 0 taking; `5c72:0ac4` with `Select`,
`6a69:00cb` with 1, so Exit is index 1, and any index but 1 takes the
marked row), a space read past (Champions' list takes it as `Select`, or
in its items leaves) and Escape, Exit or no row taking none.

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
selection kept from the first, Escape for Exit (`DS:c0f8` cleared,
`0182`), up and down picking a character, read by index as the treasure
menu's. Take is the treasure menu's Take; Pool pools the party's
money; Money deposits the selected character's coins (`580d:26d0` with 0:
in the text window, steel first, each row its name, spaces to 18 less the
name's and the amount's lengths, the amount; "select type of coin ", and
"How much Steel will you deposit? " for at most what it has, `DS:1237`;
its list by index as Take's money, at `580d:2872`: Escape and Exit
deposit none;
the coins go into the pool, as a drop does in modes 1, 6 and 7,
`5c72:0891`), the text window cleared after. Exit writes the pool to
`TMPVAULT.DAT` (`46b2:302d`: coins, then the items in order; the disk's
space check left out) for the next save to make the saved game's vault
(see [Party](party.md#party)). View and Items are Death Knights' sheet (`580d:0c66`) and
items menu (`0ffa`, then the screen redrawn, `65cd:2db0`), which in mode 7
deposit what they would drop: coins and items into the pool, which Exit
writes (see [Death Knights' View](camp.md#death-knights-view)).

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
`tools/dosbox/checks/dkk_treasure.py` the original (see [Checks against
the original](checks.md#checks-against-the-original)).

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
`6961:05b9`, `06bd`, see [Playing scripts](scripts.md#playing-scripts)), then the party
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

- `View` is View (see [View](camp.md#view)), whose Items offer `Sell` and `Id` here.
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
  stack (see [View](camp.md#view)): on entering a shop or the temple it holds what the
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
  too; Left and Right move and read on (see [Library](library.md#library)'s menus by index).
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
temple, and `tools/dosbox/checks/dkk_shops.py` the original (see [Checks
against the original](checks.md#checks-against-the-original)).
