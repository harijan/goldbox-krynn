# Camp, casting and View

The camp menu, casting spells outside combat and the character sheet. Part of the
[developer notes](../DEVELOPERS.md).

## Camp

`src/camp.h` ports the camp from overlay `4888` and the rest loop of
overlay `57e4`; `src/magic.h` the camp's Magic menu, with the spell lists of
overlays `546c` and `5b04`. `Encamp` (`2fd3:3403`) runs the block's camp
vector (`4b3d`), then the camp menu; if an encounter interrupted a rest, it
redraws the screen and runs the rest vector (`4b3f`), which scripts use to
start the encounter. Then it shows the view, clears `DS:5885`
(`2fd3:344d`), so that the game counts as saved only in the camp it was
saved in, and outside 3D areas marks the party on the overland map
(`4877:0005`, see [The overland map](adventure.md#the-overland-map)). As in the original, a `NEWECL` in either vector
enters the new block only after the next command. When the camp vector
ends the run (`DS:4b57`, as when the party is killed), the original opens
the camp menu all the same; the port does not.

The camp menu (`4888:2c31`) sets the mode to 2, which shows `camping` on
the status line and keeps the effect timers from counting down members
with no timed effect, clears the rest time and the day's healing count,
and redraws the screen for camp (`6346:2c17`): the frame, the party list,
the status line and the camp's picture, `PIC` record 0x3b, which the
menus draw at cell 3, 3 and animate while they wait (see [Playing](front-end.md#playing)). It
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
reaches the game (see [Party](party.md#party)). On leaving it reloads the small
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
`Champion`. `Icon` is the combat icon editor (`4def:408b`, see [Creating
characters](start-menu.md#creating-characters)), then the screen redrawn for camp (`6346:2c17`).

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
reads it (see [Party](party.md#party)), after setting the speed (`0x4bfc`), pictures and
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
[Derived stats](party.md#derived-stats)). After each character's files, its roster copy is erased
(`4b6d:0a80`, see [The start menu](start-menu.md#the-start-menu)).

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

The spell lists (`5dc5:016e`: Cast, Memorize, Scribe, Learn, Keep, and
View's to look) are `6a69:1b82`'s (`cok_menu_indexed_rows`), from the
first item each time (`DS:c0f7` 1): `Choose Spell:` over `Cast`,
`Memorize`, `Scribe` or `Learn` (`DS:31e1`-`320d`), `spells OK? ` over
`Keep`, or nothing to look (last 0xff: its Exit at index 0, from column
0), each the item 0 (last 0), with Next, Prev and, but for Learn, Exit by
index after it; the list leaves on a pick of the first item or on Escape's
0x1b (Exit, Escape, the scan codes 0x45 and 0). Up and Down (8 and 2)
move, PgUp and PgDn (9 and 3), Next and Prev and 0x4e page; a hotkey
selects its item, so Enter after N is Next again; Space is read past.
Clicks are 1b82's: a click on a row picks it out, a click on the row
picked picks it; a click off Learn's list with `Learn` alone stops the
run, as VERTICAL MENU's (`6a69:1ea6`).

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
game's `spells.cast`, see [The spells and effects](games.md#the-spells-and-effects)).
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
  [Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)), runs outside combat too, reading the cells
  around the spell's through the map pointer (`DS:6a2e`), NULL there: the
  interrupt table. The port takes none of those bytes to be a green cloud,
  in which case nothing happens.

In a battle the cast picks its targets with the combat routine
(`432f:2337`) that the battle puts in `DS:6e3a`, flies its missile and
runs the handlers that act on the map: see [Spells in combat](combat-spells.md#spells-in-combat). The effects
spells add run their handlers when their timers end, outside combat too:
Slow Poison's 0x0f and 0x16, Cause Disease's 0x22 (see [Spell effects](party.md#spell-effects)).
Outside combat 0x47 (from 0x3f) on event 9, when it would turn a spell
back, stops the run with `COK_ECL_EFFECT_FAILED`, for the original reads
the last battle's tables and freed map; and spell ids 0x6c-0x7f, which
only items name, are past the handler table. The
spell target and the trade partner (`DS:710b`, `46b0`), which the
original keeps as pointers into freed records when Alter's Drop removes
a character, are forgotten in the port; the selection kept in `DS:43bf`
names the freed record, as the other removals' (`4def:3b0a`, see
[Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)).

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
  terrain the cloud kept with its icon over it (see [Death Knights'
  clouds](combat-spells.md#death-knights-clouds)).

Its spells that cast: the 60 ids that share Champions' handlers, and
Enlarge, Lightning Bolt, Flame Strike, Confusion and Ice Storm, Stinking
Cloud and Cloud Kill (`5dc5:2934`, see [Death Knights' clouds](combat-spells.md#death-knights-clouds)), and the
changed spells (band B, 5dc5's handlers against Champions' 5b04's, by
their entries' constants and words, `cok_rules_cast`):

| Spell | dkk | As |
| --- | --- | --- |
| 0x01, 0x69 Bless | `1d77` | Champions' (`1efb`): "is " and "Blessed" run together |
| 0x02 Barkskin, 0x62 Fire Touch | `1daa`, `4b7d` | `COK_CAST_SAYING`: the spell applied, "is toughened from barkskin", "is covered in flame" |
| 0x0a, 0x6a Charm Person | `2044` | `20f8` with `value` 1: an elf (`+0x20` 7) without the creature flag 2 is unaffected, not a monster of some kind |
| 0x18 Resist Fire, 0x2c, 0x64 Bestow Curse | `2763`, `3204` | `2679`, `3705`: "is fire-resistant", "is cursed" |
| 0x23 Strength | `2bc9` | `2e4e` with `value` 1: 1d8 for each of the fighters' classes (`characters.fighter_classes`: paladins, rangers and knights too); one of none of its classes stops the run (the roll is a local never set) |
| 0x29, 0x2e Dispel Magic | `2e5c` | `320f`, its part for the clouds Death Knights' (see [Death Knights' clouds](combat-spells.md#death-knights-clouds): `2f93`, `2dd7`, `638c:21ec`) |
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
casts (the clouds: see [Death Knights' clouds](combat-spells.md#death-knights-clouds)). Checked: the section against the
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
the game (see [Party](party.md#party)). Items, Spells and Trade
redraw the sheet; Escape or Exit leaves, and the screen is redrawn for
the mode. View keeps the character in `DS:46ac` and starts trades from it
(`DS:46b0`). With nobody selected (nil, or a record since freed: see
[Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat))
the original draws a sheet of whatever the pointer names, nil's being
the interrupt table (in DOSBox-X: no name, level 208/123/2/192, 0 hit
points, an items list "NO"), and its menu writes there; the port's View
does nothing then, a deliberate difference, in both games.

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
in shops and the temple (mode 1) `Sell` and `Id` (see [Shops and the
temple](treasure-shops.md#shops-and-the-temple)). The stats are recomputed after every key (`6346:0d20`), and the
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
(`58e7:09fa`, see [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)).

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
  whether to ask for another partner (0) or leave Trade. It lies 0x142
  below View's frame, below anything View's menu reaches, so it holds
  what ran there before View, which each caller tells the port (as
  DOSBox-X showed, `e_trade.py`; `src/sheet.h`). Loading the camp's
  picture zero-fills it, so in View, the first command of a camp, Trade
  asks again, and so it did in a camp whose picture was kept (Encamp
  twice); Save, Magic, Rest, Fix and picking a character leave it as it
  was; after anything chosen before in the same View (Items, Spells,
  Trade or Drop), a View before in the same camp or Alter (the low byte
  of the stack's segment its calls pushed, 0x1c), it leaves. From the
  start menu's View it holds the low byte of a stack offset (0xec): it
  leaves. From the commands' View it is what an interrupt's frame left
  at that depth (the BIOS's INT 8 calling INT 1Ch, `F000:FEAF` beside
  it), which varies from run to run even with the same seed: 0 (Trade
  asks again) in 4 of 13 runs at the boxes' fixed cycles, the game's
  speed, else 0x86, 0xac or another (it leaves); with turbo, a harness
  speed, 0 in 11 of 12. The port takes the game's speed's outcome: it
  leaves.
  An emulator run of the whole camp agreed on each case before. The
  treasure menu's View works the same way, and so do the shops' and the
  temple's (see [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat) and [Shops and the temple](treasure-shops.md#shops-and-the-temple)).
- An item used to cast a spell is followed by its position, as a spell
  such as Dispel Magic can remove an effect whose handler removes another
  item, Spiritual Hammer's; if the item itself goes, the original goes
  on with the freed item, and the port stops.
- A name's bytes past its length keep what longer names left (as the
  original's files show).

With the debug flag set, Items offers `View` first (`546c:187b`; see [Debug
mode](scripts.md#debug-mode)). The combat side (no Trade, `Use` while the combatant may use items this
round, the item's use ending the turn) is with the player's commands (see
[The player's commands](combat-turns.md#the-players-commands)).

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
are Champions' but for their menus, by index (`6a69:0f1b`, see [Library](library.md#library)),
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
  (`[bp-0x13c]`, tested at `580d:26c0`) holds what the stack held: what
  View's menu (`6a69:0f1b`, called 0x0c below Trade) left at its
  `[bp-0x130]`, whatever View was called from: `6a69:0fee`'s high word of
  the time the menu began, in hundredths of a second since midnight by
  the DOS clock (`14e4:09bd`, GetTime) plus 30, for its timeout. Its low
  byte is 0 only in the 655 seconds after midnight (to 00:10:55), when
  Escape at the first coin list asks for another partner; at any other
  time Trade is left, and the port, which has no clock of the day, always
  leaves (`dkk_trade.py`: the byte against the clock, and with the BIOS's
  clock set past midnight, 0 and Trade asking again).
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
  name (see [Death Knights' shops](treasure-shops.md#death-knights-shops)).
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
Items are these (see [Death Knights' treasure](treasure-shops.md#death-knights-treasure)).

`tools/dosbox/checks/dkk_items.py` checks them against the original: camp
(View, Items, Ready, Halve, unready, Trade, Drop, Escape, Drop money,
camp's Save: 0 pixels apart at every step but the pick's row, saved game C
and every character file byte for byte) and the vault (View's Deposit of
steel, Items' Deposit of the mace, `TMPVAULT.DAT`, `VAULTC.DAT`, saved game
C and its characters); `dkk_fight.py`'s `items` flow View and Use in
combat.
