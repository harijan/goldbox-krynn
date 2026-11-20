# Checks against the original

Part of the [developer notes](../DEVELOPERS.md).

These follow the disassembly. Each that can run unattended was then run in
the original under DOSBox-X (see [DOSBox-X harness](dosbox.md#dosbox-x-harness)) by a script in
`tools/dosbox/checks/`, which compares its screens (each with the port's
at the same key read), files and memory values with the port's for the
same keys and seed, or, where the port stops or does not model what the
original does, asserts what the original was seen to do; `make
dosbox-checks` runs them all.
The checks keep the numbers they had in this list (#1-#131, in its order),
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
  written (see [Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath) for what its walk read); in camp
  nothing happens, as in the port: `e_cloud_dispel.py`.
- #8 Slow Poison's 63 hours rested: the character "dies from poison", the
  game goes on, its free list corrupted (see [Spell effects](party.md#spell-effects)); Cause Disease
  and a constitution of 20 as in the port, the chains stopping in a camp's
  rest: `b_slow_poison_63h.py`, `slow_poison.py`, `b_timers.py`.
- #9 Sir Lebaum's d20 over ten rounds: his Dispel Magic took SIRRION's
  0x6f, as in the port: `d_lebaum.py`.
- #11 A bolt between two walls bounces for ever in the original, the game
  hung (on a combat map rewritten in memory; see [Spells in combat](combat-spells.md#spells-in-combat)):
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
  runtime error comes, as in the port (see [Debug mode](scripts.md#debug-mode)):
  `e_debug_readonly.py`.
- #15 Aim, Manual, Escape, Down and Center in a first battle; with an
  earlier battle's record left in the entry the original centres on entry
  0's cell (see [The player's commands](combat-turns.md#the-players-commands)): `e_manual.py`.
- #16 Done's F8 ends the turn, Home guards, Del slows past 9: `d_done.py`.
- #17 Escape in Move after an attack of opportunity: back, the wounds
  kept: `d_move.py`.
- #18 `Target` after Manual's Exit: the original asks `Attack Ally:` (see
  [The player's commands](combat-turns.md#the-players-commands)): `e_manual.py`.
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
  [Camp](camp.md#camp)): `b_camp_screen.py`.
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
  `Cast`, 18/100 from camp's `Magic`, in both (see [Casting](camp.md#casting)):
  `e_strength.py`.
- #47 Trade's byte in six camp cases (Magic and Alter first among them)
  and from the start menu's `View`; from the commands' `View` the
  original asks again or leaves by the run, the port leaves, not
  compared (see [View](camp.md#view)): `e_trade.py`.
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
  [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)): `c_ecl3.py`.
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
  menu the original asks for a partner again (10 runs); after a `View`
  it leaves; in the outpost's temple it leaves at once; as the port (see
  [Shops and the temple](treasure-shops.md#shops-and-the-temple)): `e_trade.py`.
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
  ends Add; and a space at a second Add left Add too (see [The roster](start-menu.md#the-roster)):
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
  game at a PSP of 0x08f0; another at 0x087b (see [Spells in combat](combat-spells.md#spells-in-combat)):
  `d_fear.py`.
- #123, #124 Every screen compared in CGA and EGA mode (the title, the
  start menu, character creation, Solace, the outpost, the camp, a sheet,
  the overland map, Throtl's 3D view, a battle's first turn and Aim, CGA's
  four colours, `Sorry, not in CGA`) is the port's (see In CGA mode
  below): `f_display_modes.py`.
- #126 A small picture's frames with little memory: fewer by mode, or
  `WARNING: Insufficient Memory` (see [Display modes](front-end.md#display-modes)): `f_frames.py`.
- #127 Lightning Bolt beside a wall comes back and hits the enemy again,
  and the caster too, in both (see [Spells in combat](combat-spells.md#spells-in-combat)): `d_bolt.py`.
- #128 A cleric hit while casting "lost a spell", and the spell is gone:
  `d_cast.py`.
- #129 Magic Missile reflected: the caster takes the damage:
  `d_turning.py`.
- #130 Molly's Hoopak unreadied outside combat (effect 0x07's handler,
  through no combat record): RandSeed one step on, as for no other item,
  and the items list as the port's; ROB of it stops the port (the THAC0
  left unknown) (see [The rounds](combat.md#the-rounds)):
  `e_kender.py`.
- #131 The training hall's Remove of the selection kept in `DS:43bf`:
  the freed record is never restored once LEAVE enters the next block;
  after the vault's Deposit and the INN it is (PROGRAM 9), the camp
  highlights nobody and Up selects nil, as the port (see [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)):
  `e_freed_selection.py`.

Death Knights' title song (see [Death Knights' songs](sound.md#death-knights-songs))
on the Tandy and the AdLib, its pitches and tempo against `eclplay --wav`:
`tools/dosbox/checks/dkk_sound.py`, in `make dkk-checks` (about four
minutes, in real time).

The Dark Queen of Krynn (see [The Dark Queen](games.md#the-dark-queen)) is
checked by `tools/dosbox/checks/dqk_views.py`, `dqk_boot.py`,
`dqk_menus.py`, `dqk_party.py`, `dqk_view_mouse.py`, `dqk_items.py`, `dqk_train.py`, `dqk_add.py`, `dqk_modify.py`, `dqk_stats.py`, `dqk_create.py`, `dqk_ecl.py`, `dqk_ecl_input.py`, `dqk_ecl_text.py`, `dqk_ecl_flow.py`, `dqk_ecl_misc.py`, `dqk_ecl_party.py`, `dqk_ecl_items.py`, `dqk_fight.py`, `dqk_program0.py`, `dqk_camp.py`, `dqk_shops.py`, `dqk_treasure.py` and `dqk_bar.py`, which `make dqk-checks` runs
(`run_all.py --set dqk`; about a minute each), in RGB
(the original in VGA, `RGBScreen`; the title's scenes taken as the
original waits in each, by its loop's scene number, `dqk_lib.title_scene`,
so that a loaded host does not change them). The title is left by `dqk_lib.escape_title`:
Escape, then DS:c5ce (main's menu's timeout, set once the title's loop returns) checked, Escape
again up to three times, else BoxError; a key typed between two scenes is dropped, and the next
key ends the title, so the keys after it would fall on main's menu. `dqk_boot.py` compares the first
screen of the game after Begin Adventuring (block 4's text over Palanthas'
picture) with `eclplay --game dark-queen`'s; `dqk_views.py` the title's
eight scenes (each passed by Space) and its menu, whole; the developers'
jumper (block 1, reached by setting the block variable, 0x16 of the 1 KB
at far `DS:b20f`, to 1 before Begin Adventuring: `5f07:10cd`), its text
and menu, and block 14's arrival in the Dargonesti's sea (D), whole but
the party's list and status (no party yet) and the mouse's pointer; and
the 3D view, cells 3-13, at each place of the adventure loop: block 14
(wall sets 13, 14 and 11) after a step east, its text read and three
turns, then on two squares whose views draw the third set (slot 2, types
11-15: the party's square and facing written, `DS:c9f0`-`c9f2` and
`c9f4`, and turned), and block 18's corridor (Hizden's house at night: wall set 1, the
night's backdrop) facing each way, the port running each block's load
vector from its `LOAD FILES` at the original's square and facing (read
from `DS:c9f0`). Not compared: the colours the title's scenes cycle
(`261b:05be`: the logo's, the twinkle, the letters' fire), which the port
does not run. All agree (2026-10-26).

`dqk_menus.py` compares its window library's screens whole, with the game's
own mouse cursor (`eclplay --mouse`): main's menu; Play's camp menu with no
party, its Load's "load from where?" (the prompt boxed, its buttons after
it) and Escape back, Exit From Game's "Exit to DOS?" and No; past Begin
Adventuring, block 1's developers' menus (JUMPER, NXT, NXT), BPIC's
`INPUT NUMBER` field with 240 typed (its blinking caret not compared) and
the big picture it shows (`BIGPIC` 240 in the overland's frame) with YES
NO; and, in a second box, Hizden's inn a step north of the party's start
(block 18): its greeting, `VERTICAL MENU`, Down, Return and its answer,
the port running the inn's event (`--start 898b`) at the original's square
and facing. The original's colour cycles are stopped in its box
(`261b:05be` made a `retf`), so that pictures with cycles (picture 142,
`BIGPIC` 240) settle and show their colours as set, as the port's shots
do; not compared: the party's list and status, and the inn's lamps: the
original's are its saved game's (variables `0x1fa`-`0x1fc`, 2 each, as they
stay all the way), drawn at Begin Adventuring (`20b2:00e4`), while the
port's inn run draws its frame as eclplay starts, before its `--set`, with
0 (`dqk_party.py`, from the start menu, compares them: they agree).

`dqk_party.py` (about a minute) loads the shipped saved game
(`SAVE\SAVGAMA.QSV`: Load, Tdqk, A) and compares the camp with its party,
Down, View's sheet (with its readied weapon and armour, rows 21-22:
`177e:03fc`) and Escape back; then, past Begin Adventuring (the
copy protection answered in the box), block 4's first text with the party's
list and status, Palanthas, the palace and Leave to Caergoth's 3D view and
commands; the variables 0x0001-0x0400 once loaded against `eclplay
--vars`'s; then 24 steps and turns along Caergoth to the widow's menu, the
clock (variables 5-11), last square (0x17, 0x18) and edge (0x21) there, the
party's square and facing after each (`DS:c9f0`-`c9f2`) against the port's
log, the widow's menu, the shadows and the step before the dragons, whole,
the mouse's cursor included; the original then runs the dragons' `SETUP MONSTERS`
where the port stops, cited (its ECL between them). All agree (2026-11-02).

`dqk_view_mouse.py` (about a minute) moves the mouse over the 3D view's areas at
the adventure's commands (`1bdb:1aef`, the mouse on), from Caergoth's view at
3,2 facing 2 (`dqk_party.py`'s route; the box's pointer, `sdl.mouse_emulation=always`):
fourteen spots, each side of the view's edges, the pointer moved to each (the
whole screen compared, the cursor included: ALWAYS.TLB's 26, 11, 25 or 27, or the
shield elsewhere), then clicked; the party's square and facing (`DS:c9f0`-`c9f2`)
before each spot and after the last click against the port's, one `eclplay
--mouse` run per spot (the pointer's move, then Escape: its last shot, with the
menu waiting). All agree (2026-11-02).

`dqk_items.py` (about two minutes) plays the sheet's menu from the same saved
game, one key a step, every screen compared whole: View's coin Trade
(`86b8:286a`: the picker of `86b8:3ae5`, the type of coin, an amount typed
on the bar by `86b8:2586`, Exit asking whom again) and Drop (`86b8:2c32`:
129 and a 9 more, which the number cannot pass, a Backspace, 19 dropped,
Escape from the amount), then Items (`86b8:0f41`) of the first member
(Trade and Drop of a readied item refused, "Can't halve that", Join, the
helm unreadied, Drop asked and refused, the helm traded to the second
member) and of the second (his arrows halved and joined, his bow refused
twice, "already using ..." and "Your hands are full!", then readied), the
third member's View and coin Trade (the picker starts on the member viewed,
not the one last given to), and Save Current Game: the saved game the original writes against the port's,
byte for byte but the characters' far pointers. All agree (2026-10-29).

`dqk_train.py` (about two minutes) plays the shipped saved game A from a
patched copy in both (variable 0x30, the hall of every class, and three
characters' experience; the same random seed, `DS:ca0e` and `--seed`): Knight
new Order (the sword, then the rose), Train Character for a knight, a dwarf
fighter and thief (his experience held below the level after's), a kender
cleric and thief and a character that cannot ("Not Enough Experience"), then
Save Current Game; every screen in RGB, and the saved game written (but the
heap's far pointers); a second run with the hall of knights alone (variable
0x40) says "we don't train that class here". `dqk_add.py` (about four minutes)
adds characters from the roster (Tdqk, and Dkk's imported from Death Knights'
saved game A as roster files) in two parties, a Death Knights paladin patched
to the twelfth level among them, and saves: the screens and the saved game
written (from its party's size), which hold the stats and levels
(`8bcc:07f1`) as the original computes them. Both agree (2026-10-29). They are
in `make dqk-checks`.

`dqk_modify.py` (about three minutes) runs Modify Character (`6a87:1293`) on
the shipped saved game's first member, in the original and in `eclplay
--start-menu --mouse`, a key at a time in RGB (whole screens but the pointer's cursor, and the name's
cursor, a block the original toggles by the clock): Add, Sub, Next and
Previous over the scores, the name edited in place and the hit points, Exit
(then Remove Character's `.QCH`, compared byte for byte but the original's
far pointers); a saved game of its own with the scores at their race's and
class's limits and the hit points at theirs; a name taken by another of
the party, deleted and typed past its 15 characters; an experience creation
does not give ("can't be modified."); the clicks on a score's row and the hit
points (the box's mouse, `sdl.mouse_emulation=always`); and Keep
(`6a87:025e`: the spells a day and known by `8bcc:01e7`, the mage's spells
forgotten by `6a87:1599` and made again by `66e5:30d3`, the hit points' base)
for a knight, a cleric, fighter and mage and a cleric and ranger, each then
Removed to a `.QCH` compared. All agree (2026-10-29).

`dqk_create.py` (about two minutes a case; `--flows CASE`, `make dqk-checks`
splits them) makes characters in the original and in `eclplay --start-menu
--mouse`, from the camp's Create (`66e5:2f95`): the form's clicks (the box's
pointer, `sdl.mouse_emulation=always`, `Box.click`) of race, class, gender and
alignment, Done, the deity's list for a cleric (Down, Select), the roll and the
sheet (Reroll Stats once for the cleric), the name typed a letter a step, the
icon's picker (arrows, 7 left and 7 up past each edge, a click at a cell's edge),
and "Save NAME?"; every screen of each step in RGB (but the caret the name's
field blinks by the clock, and the camp's cursor where it comes back, which the original draws as
it was last drawn, in the form, until the pointer moves: the steps after it
agree), and the file written, `NAME.QCH`, byte for byte but the far
pointers (the readied slots, the original's pointers, are compared by
which are nil, against the slots the items readied fill). The generator is shared: the port logs its seed where the creation
starts (`seed:`) and the check writes it to the original's `DS:ca0e` as the
form opens, so every roll, hit point and silent training is the same. The cases:
a human fighter, a Silvanesti cleric (lawful good, the good gods), a hill dwarf cleric
(no deity asked), a kender thief, a half-elf fighter and mage, "knights" (a knight,
a paladin, a ranger, and a name refused for its digit, whose stale bytes
the file keeps), "casters" (a mage of the second order, a cleric, fighter
and mage, a cleric of the neutral gods), "modify" (the sheet's Modify) and "exits": Escape
from the sheet's bar, Exit from the form, the sheet and the icons, Escape from
the name and No to the save, each back at the camp and the next creation
going on from the generator as the abandoned one left it. All agree
(2026-10-29).

`dqk_stats.py` (about a minute) loads the shipped saved game A from a copy
whose first member is made by hand and saves it: a cloak (type `0x3a`) that
magic armour keeps, a body armour of +100 as its last item (its armour class
wraps below 0 as a signed byte, which `177e:0f5f` compares unsigned) and effect
`0x4a` (the movement doubled). The saved game written holds the recomputed
record (`177e:0be2`) of a character the original had not already recomputed;
the two agree but for the far pointers (2026-10-29), and `tests/test_dark_queen.py`
holds the digest.

`dqk_ecl.py` (about 50 seconds for its four blocks, and four more for the text's delay, DS:ae65: a PRINT, an ASK MENU and a VERTICAL MENU at speed 4, each ending in a RANDOM) runs test blocks of the
ECL in both (`tools/dqkecl.py` writes them, as `src/ecl.c` reads their
operands; `tools/hlib.py`'s `replace` puts one in a copy of `DISK3\ECL.GLB`
for block 1, the developers' jumper). The original's block variable is set to 1
before Begin Adventuring, as `dqk_views.py` does, and the block's first
instruction, PRESS KEY, is where the box sets the random seed (`DS:ca0e`, the
port's `--seed`, two draws back in a `--play` run: `dqk_lib.seed_before_begin`, as Begin's copy protection draws
two first). Checked, in order: the original waits in that PRESS KEY, and
its ECL stops at the block's last instruction (`DS:b7f9` reads the address past
the opcode it runs: the entry, 0x8015, reads 0x8016); the variables 0x100-0x17f
and 0x29 (`WINDOW`; each block zeroes them first) byte for byte, the original's
1 KB at far `DS:b20f` against `eclplay --game dark-queen --vars`; no other
variable changes on either side, from its start (the original's at its PRESS KEY,
eclplay's at an empty block's end, the blocks starting alike); and both sides
leave what the block should (values written by hand, and the random draws by
`2102:0234`'s formula). The blocks: the arithmetic and RANDOM narrow and wide;
COMPARE with every IF, 9 against 5 and 0xffff against 1 (greater as unsigned);
GOTO, GOSUB (nested) and RETURN; ON GOTO and ON GOSUB in range, at the count
and out of range; the flags AND and OR leave; RANDOM into a wide variable; RETURN
at depth 0, which ends the block; GETTABLE and SAVE TABLE; the wrap of 16-bit
sums and products. All agree (2026-11-01). DIVIDE by zero is not in the
blocks: the original never gets past it (DOSBox-X logs an unhandled INT 0
again and again, the game's screen and ECL stay at the DIVIDE, 832a), where
eclplay stops with "division by zero" there; a difference in a case the game
does not survive, not compared.

`dqk_ecl_input.py` (seven blocks, a few minutes; `--only NAME` runs one) runs the input and menu opcodes the same way, its keys typed after each
block's PRESS KEY (`eclplay --keys`, the box's keys as the game waits); its
machinery is `dqk_ecl.py`'s (`run_block`). `tools/dqkecl.py` writes a string
operand (`text`), the packing `src/ecl.c`'s unpack reads. The blocks: INPUT
NUMBER (an empty line, five digits at most, "-5" and Escape asked again, a suffix
base, out of range asked again, Escape after typed text discarding it, "3" Escape
"7" is 7; 300 into a narrow variable, the byte after it 0x55 still) and INPUT
STRING (lower case upper-cased, an empty line a space, Escape keeping the text, 40
letters keeping 35, Backspace); COMPARE AND (two pairs; a string pair never equal;
a wide variable against a word; a number against a string, whatever the original
gives); VERTICAL MENU (Down and Return, Escape keeping the current item, Up
wrapping; the store is as wide as the first item: a string makes a word, clearing
the byte after a wide header variable, a number first a byte, the byte after a
narrow one (0x151 0x55) kept, a string first and a number last a word (0x155
cleared)); HORIZONTAL and ASK MENU (a hotkey, the first item with its letter,
Escape and Down not ending it, a wide header variable as a word, a narrow one as
a byte with the byte after it, 0x55, kept); and YES NO (a hotkey, Escape as No,
Down not ending it, the flags cleared each time). Down at a bar menu runs the
party's member keys in the original (`1bdb:1e29`, DS:ad67), which the flow does
not compare: it has no party. LOAD CHARACTER is not in the blocks: its result is
which member is selected, which needs the same party on both sides. The flow
fails if eclplay leaves keys unread (its `(keys left unread in block N at ADDR)`
on stderr). Not run: the `DS:c2f5` paths. `1bdb:0424` (YES NO, 0452) and
`6c70:0e3f` (HORIZONTAL, ASK) branch on c2f5, which `1bdb:1206` sets to 1 for a
menu's code result (a key below 0x20 or an extended key, a view area's click), not
for a mouse hit on a control; no block clicks. All agree (2026-11-02); a port
string limit one short fails INPUT STRING's block.

`dqk_ecl_misc.py` (two blocks, in `make dqk-checks`) runs the ECL CLOCK, DUMP and FIND SPECIAL the
same way, with a party on both sides: the original loads saved game A (`plta`), and eclplay gets its
six members with `--character` for each `.QCH` in the order of the saved game (ROLANDOF, TIMASALI,
'CINDER', BALTHIRN, ROARK'CR, WALKERKE). The `clock` block: the years (one more makes 100, which comes
off), the member's age (`wvar` 0x04b1, the word at +0x52) before and after, unit 0, a day and a second
day (Lunitari's first turn), with the clock's variables 5-11, the moons' 0x1fa-0x1ff and the window
0x100-0x17f and 0x20 compared. The `party` block: FIND SPECIAL with 14 ids (the shipped 20, 24, 50,
57, 61, 63, 119 and 168, and the .QCH files' 92, 94, 95, 105, 26 and 47) for each member, the flags
expected from the files' effect lists; then DUMP of the middle member and of the first, with variable
0x20 and the new selection's age after each. All agree (2026-11-02).

`dqk_ecl_items.py` (two blocks, about a minute; in `make dqk-checks`) runs the item opcodes the same
way, with saved game A's party on both sides. The `items` block: FIND ITEM for each type the party
holds and one it does not, then DESTROY ITEMS with 0x80 (bonus items only) and without, the types
found after each step saved (the expected values: the types nobody destroyed stay, the bonus-less
0x44 stays under 0x80, the readied 0x25, 0x12 and 0x44 go, unreadied first), and ROLANDOF's armour
class (ECL 0x571, the base AC) as a word before the DESTROYs and after them, so the stats recompute
is compared. The `rob` block: ROB of ROARK'CR with 39 percent taken (61 kept) of the 200 of gold and
chance 100, the coins read as words (the gold becomes 122, an integer; as Reals it would be 121), and
the types of its items after the d100 draws, which are compared as they fall (one d100 per item, the
expected flags 0, 1, 1); then a RANDOM of range 10 after ROB, whose value is the seed after the three
draws, so the number and order of ROB's draws are checked. WALKERKE's readied power item (0x43) is no
block's: its routine is not ported. The cursed path (an item with +0x34 set: "It's Cursed!") has no
DOSBox coverage: no item in saved game A is cursed, and the repo test (`test_adventure`) covers it.
All agree (2026-11-02).

`dqk_program0.py` (about half a minute; in `make dqk-checks`) runs PROGRAM 0, the camp from the
adventure, the same way, with the party of saved game A on both sides, in two flows; each is a test
block (PRESS KEY, SAVEs, PROGRAM 0, EXIT) in place of block 1. The hall's sets variable 0x30 and,
after PROGRAM 0, the marker 0x130. After the PRESS KEY's Enter the camp opens; View Character (`v`)
and Escape back, then Begin Adventuring (`b`) ends it and the block goes on. The overland's first
sets variable 0x22 to 1 and then 0 (the store makes mode 3), 0x24 to 1, 0x3e to 0xf0 (the big
picture) and 0x25 and 0x26 to 5 (the party's place), then PROGRAM 0 and a PRESS KEY: Begin (`b`),
and the screen at that second PRESS KEY, before the block's EXIT, shows the overland's map again
with the party's place (177e:1ad1). The screens agree at each wait (the PRESS KEY, the camp, the
view's sheet, the camp again, the overland after Begin), with the mouse's pointer, the status line
and the view's foot icons left out (the status line and the icons are the adventure's start, which
eclplay's block entry does not draw). The hall's adventure after Begin is the screen after the
block's EXIT, where the original's bottom bar shows the game's own DISK READ ERROR (left out): the
loop's redraw after the EXIT (the view's, CALL 1's DS:b7f8) is not reached in the original, so it is
not checked. The variables the original changed agree (0x30 and the marker; in the overland flow
0x22, 0x24, 0x3e, 0x25 and 0x26 too). The other variables are not compared: eclplay starts from a
fresh game, not saved game A (its `--load` is not ported). All agree (2026-11-02).

`dqk_shops.py` (flows `temple`, `wounded` and `stoned`, about a minute each; in `make dqk-checks`)
runs the temple: a test block (`tools/dqkecl.py`) runs LOAD FILES 2 1 (the jumper's, as
`dqk_ecl_text.py`'s start), sets var 0x14 and calls COMBAT with no monsters, from a saved game (the
original's entry as `dqk_fight.py`'s). `temple` is saved game A: the menu, View's sheet, the menu
after it, Heal's list (`h`), its Stone to Flesh question (Down nine times and Enter, then No), the
list after No, the menu after Heal (Escape), Down and Up at the menu (the selected member by name:
member 1, then member 0, `5e22:067c`) and Exit. `wounded` is a copy of saved game A with
Roland at 100 of 134 hit points (`+0x198`): Heal's list, the menu after it, Exit. `stoned` is a copy
with Roland's status `+0x64` 7: Heal, Stone to Flesh by keys (Down nine, Enter: no question; cured
to status 0, active, 1 hit point), the list again, the menu, Exit. Each step is compared in RGB (the
mouse's shield masked, the pointer's wider arrow at Exit), the 1 KB of variables (at the menu steps
but var 0x14, which eclplay's run has returned from by then) and the party's records. The other
Heal cures are not compared: eclplay stops at each, citing its routine, and Repair likewise.
The wounded hit points are drawn by the original's colour byte 0x00 (`7825:0737-074b`) on the
window's grey, which `dark_queen.c` writes as `0x80 | fg`: with a black background the Heal screen
differs by 121 pixels.

`dqk_treasure.py` (flows `won`, `fled`, `lost`, `pool`, `bonus`, `zero`, `negative`, `rest`, `gentle`,
`fledmix`, `lost9`, `enemy`, `enemyout`, `enemyhigh`, `take`, `share`, `gather`, `overload`, `takeall` and `items`; in `make dqk-checks`) runs the end of combat with no monsters from the treasure's start: a test block
(`tools/dqkecl.py`) runs LOAD FILES 2 1, COMPARE 1 1 (the flags `= <> < > <= >=` become 01 00 00 00 01
01), SAVE 1 to var 0x2e, the flow's own ops (TREASURE ITEMS for the pool's coins, SAVE 0 to var 0x20),
and COMBAT, from saved game A (`won`, `pool`), or from copies with every member's status 3 (`fled`),
status 2 and inactive (`lost`, `lost9`: var 0x20 set to 9), or changed records (`bonus`: the classes;
`zero` and `negative`: members inactive; `rest` and `gentle`: the statuses 2-5 and hit points, with
none fled, `gentle` with var 0x1d; `fledmix`: fled, the survivors matched to the saved game by name;
`enemy`: member 5 hostile; `enemyout`: member 5 hostile and inactive, var 0x15 set first; `enemyhigh`: its
hostile word 0x0101). The original is trapped in two stages: `2093:0015` (the flags' clear, always
resident) spins at the PRESS KEY before COMBAT; the flow's end is trapped too (`5f07:0987`, COMBAT's
return, which a lost party reaches after its continue prompt), and the
game waits at its first key (the results' bar, or the continue prompt): its screen is compared with
eclplay's last shot there (the keys ran out), the key typed, then the end's trap holds. Each flow's
state where it holds is compared with eclplay's end (`--vars`, `--records`, `--state`, `--screen`): the 1 KB of variables (var 0x2e included), the records (pointers
masked: the experience and may-train with them), the flags, DS:af3f, c7de, c7df, b1da, b7a7, the
share (DS:ae4f) and the pool's coins 4-6, the mode, RandSeed, and the 18 effect ids at DS:631e
(checked against the table). The expected values of each flow (the share, the pool, each member's
experience gained and may-train, as the flow's arithmetic gives) are checked against the original
too, so a mistake in a flow shows. eclplay runs `won`, `fled`, `rest`, `gentle`, `fledmix`, `zero`,
`enemy*` to COMBAT's end; `pool`, `bonus` and `negative` run the treasure menu (`75cc:0e93`) to COMBAT's
end, each wait of the menu compared in turn (the original's screen, variables, records, pool, RandSeed
and the selected record by name). The menu's animation is frozen in the original only: the frame
advance `1bdb:1168` (`incb DS:b20e`) is four nops (`freeze_frame`, checked by its bytes and at each
wait), as eclplay never advances a frame, so the screens are compared exactly, the treasure picture
included, and each wait settles at tolerance 0. `pool`'s steps: the sheet, Escape, Down, Exit's
question, Yes, the question again, No. Its frame check (bug 3): DS:b20e is poked to 2 at the sheet,
Escape redraws the menu, and the menu's entry (`1bdb:1073`) and first pass (`1bdb:113f`) read and draw
frame 2, the first pass's screen settling to the same screen; the frame is then 0 again. eclplay's side
is the repo test (`test_dark_queen_picture_frame_kept`), since it cannot be poked. Detect is not typed
(a cited stop, part 28); `take`, `share` and `gather` type Take's coins, Share and Pool (the probe's
numbers); `takeitems` (part 21b) types Take's items with TIMASALIN selected by Down (id 80, the Red
Mage's scroll, then id 1: her items and weight, 6, 7 and 8 items at 660, 661 and 671), and `takeboth`
the bar's "OverLoaded" for ROLAND's steel 64000 (nothing taken), then TIMASALIN takes id 80; both run
eclplay with `--mouse` and compare their steps' screens whole: over the list (a control from cell 1, 1)
the game's own cursor at 8, 8 is the sword (2aa6:15e3), which reaches past the corner's mask. `items` (part 21a) types TREASURE ITEMS 0 0 0 5 : 1 0 255
255 80 (the random scrolls drawn from RandSeed 12345): the pool's four items at the menu, the share 266,
and eclplay's `--items` (the pool's items, then each record's, as their file bytes in hex) is compared
with the original's lists (`pool_items`: the 0x3d-byte nodes from DS:b1e7; `member_items`: each record's
list at +8), at each wait and at the end, with each item's 17 bytes at +0x28; `lost` and `lost9` run on to COMBAT's return (`5f07:0987`) and stop at the adventure's menu after it (`dqk 5f07:11f3`, cited).
`lost` leaves the party empty (0736 removes it), and `enemy` leaves DS:c7df set (11b0 removes the
hostile member).
The `pool` flow's 4950 over 6 is 825; `zero` divides by 0 (LDIV@: 0xffffffff, member 0 gains -1);
`negative` divides by -2 (-2475). Saved game A's pool is empty, so `won` awards nothing.

`dqk_bar.py` (half a minute; in `make dqk-checks`) takes the original's screen while a bar
message is up: main's menu, Party and Delete Character with no `.QCH` in the box ("No characters to
delete.", `177e:153c`), the spin written at `177e:1560`, after the lcall `1a62:0046`. The box's black
edges must be at pixels 7 and 200 (rows 187-197) and the text from 8: 1a62:0046 boxes from cell x + 1,
and 177e:153c's x is 0. The port's side is `test_dark_queen.c`'s `test_bar_message_place` (the
screen at the message's wait, `hooks.delay`): eclplay's shots come at key waits, after the bar is
drawn again.

`dqk_camp.py` (about a minute; in `make dqk-checks`) runs the adventure's camp, Encamp (`e`) from
the commands after the same walk to Caergoth's 3D view as `dqk_party.py`, with saved game A's
party on both sides: the camp's menu, View's sheet (`v`) and the menu again (Escape), the adventure
after Exit (Escape), each with the screen in RGB (the mouse's pointer left out) and the variables
0x0001-0x0400; then Encamp, Save to slot C (`s`, `c`), "Exit to DOS? " answered No (`n`) and
Escape, the adventure's screen and variables, and the saved game C: byte for byte, with the far
pointers (the records' and items' and effects' next, the item pointer's high word) left out as
`dqk_create.py` leaves them and the effects' pointer and the 13 slots compared by nil-ness (the
original's slots are its readied items', `dqk_create.original_slots` and `model_slots`). Then
Encamp and Load: after Save and Exit the game is not saved (`5f07:0ba6`), so Load asks "Game not
saved. Load anyway? ", whose screen is compared. The camping picture is left out of the menus'
screens and the question: the original animates it while the menu waits (DS:c2ef above 0, checked
at each menu), so its frame at a shot is the animation's. Then the flow `alt` after Load's
question's No: the Alt menu, Speed (Slower, Exit), Level (Champion), Order (Select, then Down, Place, Exit),
Drop (Yes, Yes) with Exit, and Save to slot D (`s`, `d`, `n`, Escape), each with its screen
and the variables; the saved game D is compared as C is, and the bytes the save pads past the
item table (`604b:3352-3366`, from `DS:c6de`'s heap: the port writes 0) are masked. Magic, Rest,
Fix, Icon and Load's yes are not run (they stop eclplay). All agree (2026-11-03).

`dqk_ecl_text.py` (about a minute and a half for its seven blocks; in `make dqk-checks`)
runs the text, picture and wait opcodes the same way, with `dqk_views.py`'s screens:
each block starts with the jumper's LOAD FILES 2 1 and its SAVEs of the party's square
to 3,3 (ECL 1 at 8025 and 802b), then a PRESS KEY. The original is compared with eclplay
entered at the saved game's square (`--at`, read from the original before the block
runs) at every wait: the whole screen but the pointer, the party's list and, at a wait,
the status line (the original shows what the game drew when the saved game started, and
eclplay draws the status only at a redraw, so the status is compared after CLEAR EXIT),
and the variables 0x100-0x17f. The lamps under the view are compared: eclplay's `--set`
variables are set before its frame (as a saved game's are). Strings are the packed 6-bit
operands `src/ecl.c` unpacks (`text()` in the flow). Its blocks: PRINTCLEAR, PRINT with a
long text that waits at the window's end, numbers (an immediate, a wide word, a
variable), PRINT RETURN and DELAY; JOURNAL in colour 3 and in colour 7; PICTURE none, PIC
A, PIC C and a big picture; the view again after PIC A and after the big picture; PRINT
RETURN at the window's last row; CLEAR BOX between PRINTs, then CLEAR EXIT (compared
after it, the bottom bar left out). All agree (2026-11-02). Found and fixed on the way:
JOURNAL's and ASK MENU's colour (`test_adventure`'s `test_dark_queen_journal` and
`test_dark_queen_ask`), the backdrop's square byte and the view's mode test
(`test_dark_queen_view_square` and `test_dark_queen_view_modes`).

`dqk_ecl_flow.py` (a few minutes for its nine blocks; `--only NAME` runs one; in `make
dqk-checks`) runs the block, map and call opcodes the same way, with `dqk_ecl_text.py`'s
start and comparison: CLEARMONSTERS (flow only) and NOTHING (six operands; none), CALL 1,
2, 3 and the other values (the stale high byte of an immediate, `others`), LOAD FILES
with wall sets 0 and 0xff (the screen), MUSIC, and NEWECL into a test block. An original
run whose keys are lost before its block starts (its ECL rests at 0x8000 or 0x8015 with
opcode 0 and never reaches a wait) is retried once; any other failure is not. All nine
agree (2026-11-02).

`dqk_ecl_party.py` (about a minute; in `make dqk-checks`) runs the party opcodes with
the saved game A's six members on both sides: eclplay takes their `.QCH` files in the
saved game's order (`--character`, ROLANDOF, TIMASALI, 'CINDER', BALTHIRN, ROARK'CR,
WALKERKE). The variables block reads each member's race and class (fields 0xad and
0xae, ECL 0x4ae and 0x4af) after LOAD CHARACTER 0, 1, 5 and 0x0a (past the end, the
selection stays), FIND NPC for a member's name and for a name not in the party, and
WHO with End, End, Home and S, which picks the second member (the keys all read: the
pick moves on End and Home too). The screen block runs ADD EP's words for one member and
for the party (the party's ADD EP is 0xffff, so its members may train), and WHO's pick,
at each wait, with the party's list and the pick's bottom bar compared whole (only the
pointer and the status line are left out); its waits take S at the pick (`wait_key`).
Not checked here: ADD EP's experience, which the ECL cannot read (the Dark Queen's
character variables do not reach it), so `test_adventure`'s `test_dark_queen_party`
checks its arithmetic and may-train (+0xcd), the party's list's redraw, the cursor, the
pause's variable and the pick's bar and keys.

To add a block: write it in `dqk_ecl.py`'s `BLOCKS` (a function returning the
`Block` and `expect`, the values it should leave by variable address), and the
flow runs it in both. Its results go in `WINDOW` (0x100-0x17f and 0x29): a block
that changes any other variable is a difference, on either side, so widen
`WINDOW` (for every block) to check more. A block whose last instruction is a
RETURN at depth 0 labels that RETURN `last`; otherwise it ends in its EXIT. A block
may also set `stop` (where its ECL rests, when that is not past its EXIT: NEWECL's
block 2) and `waits` (the count the original must wait); `dqk_ecl_flow.py`'s
`newecl` shows both.

The Dark Queen's first fight (`tools/dosbox/checks/dqk_fight.py`, flows `setup`, `load`, `clear`, `six`, `ground` and the rest below,
in `make dqk-checks`) is a test block of LOAD FILES 4 4, CLEARMONSTERS, a PRESS KEY labelled 'monsters', LOAD
MONSTER 64 x2, a PRESS KEY labelled 'loaded', SETUP MONSTERS, APPROACH and COMBAT, from saved game A, with the
sound off (below). The original's state (`dqk_fight_lib.py`: the records, combat records, combatants,
battlefield, turn, sides, round, mode, the monsters' flag and count, RandSeed) is read at the block's entry,
at 'monsters', at 'loaded' and at the first turn, where the battle's turn routine 5045:078b is made to spin
where it starts (`dqk_fight_lib.trap`). The spin is checked to hold (EB FE in place, the keyboard polls still
over a second), and untrapped the game goes on (the polls move). eclplay starts from a copy of
saved game A (`--load`, its block 1 named in variable 0x16 as the jumper sets it) with `--play`, which
enters the block as Begin Adventuring does (without it the block runs in mode 0), and no `--set`. The
block's variables (1 KB, through the far pointer at DS:b20f, var a at that pointer plus a, as dqk_ecl
reads them) are the save's with 0x16 = 1 at the entry; LOAD FILES 4 4 at 'monsters' sets 0x13 and 0xc6
to 4 in both. eclplay's variables at 'monsters' must equal the original's: `PORT_GAPS` is empty, and the
check asserts the exact set, so a new difference fails. An earlier version read DS:b210 and reported
twelve gaps (LOAD FILES' 0x03, 0x13, 0xc6 and the rest), which were that read's, not the port's. eclplay,
with no key after the entry's, waits at the same 'monsters' PRESS KEY (out of keys there). At the first
turn: turn 1, mode 5, both sides can act, the
monsters appended are exactly the two SHARKMEN, every record has its combat record, the monsters are
combatants on the map, the readings agree (`dqk_fight_lib.consistent`), and the RandSeed is 0x1756582c (11
draws from the seed). The states go to build/dqk-fight/setup.json with the heap pointers masked
(`dqk_fight_lib.portable`: a pointer becomes the index of its record in the list).
Part 27b (dqk-effects-dispatch): every flow that stops at the first turn also compares each record's effect chain (`--effects`, `chains_agree`: id, minutes, value and flag of each node, in order) with the original's (`effect_chains`). The flow `timer` (scope fixture 4) rewrites ROARK's and WALKER's effect nodes and two record bytes in a copy of saved game A (`dqk_modify.modified`: 0x26 and 0x44 added, ROARK's exceptional strength and WALKER's stored intelligence and wisdom) and runs ECL_CLOCK 1 2 between two PRESS KEYs: the chains before and after ten minutes, the records of members 4 and 5 (pointers masked), the stats the removals recompute (ROARK 18/99, WALKER 12 and 16), the clock variables and RandSeed agree with eclplay's.
The round's start (step 4, part 10a): the flows that stop at the first turn (`load`, `encounter` and `field`'s placements, with `--turns`) compare the turn list with the original's (`turns_agree`: each entry's place in the party list and its initiative, in order, after 5045:0447), and every combat block (0x18 bytes: the initiative, the attacks, the movement, may use and may cast) agrees. `setup` checks the first turn is combatant 1 in mode 5 with both sides able to act.
Flow `ground` (step 4, part 4): the same block to the first turn, where the original's screenshot and its battlefield
(`dqk_fight_lib.battlefield`: the view at +2 and +4, the 50 by 25 cells at +9) are read, and the set the battle drew:
DS:ca03 (the wall set, LOAD FILES' second operand) with var 0x22 (`dqk_fight.py`'s `block_vars`), which 59c7:0e70 turns
into DungCom1 (var 0x22 not 0) or WildCom1 (0f14-0f47), asserted to be DUNGCOM 4 here. eclplay's `--ground` draws
that map with that set (`queen_arena.c`, through the game's arena hooks, `--ground-set`), and the 7 by 7 view at 8, 8
must agree pixel for pixel in RGB. Masked: the six cells the combatants stand on (6 x 576 pixels, their icons: parts
11-12) and the 8 by 9 pixels of the mouse pointer at the corner (ALWAYS record 2, hotspot 7, 7, at 8, 8; 5837:0846
hides and shows it around the redraw; not drawn by the port, 72 pixels, 61 of them differ, and nothing else in the
ground does). Compared: 24,696 of the 28,224 ground pixels, 0 differing. DUNGCOM 1-3, 5, 6 and WILDCOM 1, 2, 4 differ on
the same map (on nearly every pixel); the check covers the choice of the set and the palette on set 4 only (the seed's
draws on set 3 and 6: `tests/test_queen_arena.c`).
Flow `load` (`LOADS`: two SHARKMEN and one MINOTAUR, then COMBAT with no SETUP MONSTERS or APPROACH; the
original allows it after LOAD FILES) reads the party list at the first turn (`5045:078b`, after the placement; trapped
once the battle's overlay is in and the block's last key is read; the records and blocks are then made, as
`59c7:0f7b` has run), and eclplay, started as setup's (`--load --play`), stops at the arena's redraw (`--records` and
`--blocks`: the records and the trailer, c6f1, ad6b and the undead count DS:c2ed; each combat block, 0x18 bytes). The original has 19 records (the six and
13 monsters: the SHARKMEN and the MINOTAUR, then ten GORGONs), c6f1 13, ad6b 11 and no undead; the monsters' records agree
whole (pointers masked), and so do the party's (`PARTY_GAPS` is empty: the save's import; with
`--character` four members' records differed), and every combat block agrees (0x18 bytes, the unnamed bytes 0 in both). eclplay's variables at the battle's start agree with the
original's (`PORT_GAPS`). It runs twice, with the entry's difficulty (3) and with difficulty 1 (`--set 27=1`
after the load in eclplay, as the original sets it at the entry), which halves the hostile monsters' hit
points (78 to 39, 107 to 53) and no GORGON's. Flow `clear` (LOAD MONSTER 49 x1, CLEARMONSTERS, LOAD MONSTER
64 x1, COMBAT): the MINOTAUR's slot and record are forgotten, so the original has 17 records, c6f1 11 and ad6b 10
(the SHARKMAN in slot 8, 170e's GORGONs in 9). Flow `six` (six LOAD MONSTERs x1: 64, 49, 41, 37, 36, 64, the ZOMBIE
and the WRAITH undead): 22 records, c6f1 16, ad6b 15 and the undead count 2, with 170e's icon in slot 14 (no icon
slot error; the icons load as they register, CPIC 64 into slot 8). Each run's eclplay transcript has no
error line but the battle's stop. A second fight in one block is not reached: the battle traps the original, and
the port stops at the battle too, so CLEARMONSTERS' undead reset (`75cc:151b`) is checked by `test_monster.c`'s
`test_dark_queen_undead_clear` only.
Flow `encounter` (step 4, parts 6 and 7: LOAD FILES 4 4, CLEARMONSTERS, two SHARKMEN, SETUP MONSTERS with the sprite 82,
distance wanted 16 and the close-up PICA 21, a PRESS KEY, APPROACH, a PRESS KEY, APPROACH, a PRESS KEY (the close-up at
distance 0), PICTURE 0xff, SETUP MONSTERS with the sprite 2 and distance wanted 3, a PRESS KEY, ADD 9 to var 0x37, a PRESS
KEY, COMBAT) waits at each of its five PRESS KEYs ('setup', 'near', 'nearer', 'wall', 'raised'). At each, the original's
1 KB of variables and its screen are read, and eclplay's, started from saved game A with that many keys (`--vars`,
`--screen`), must agree: the variables all (0x37 is the distance, 2, 1 and 0 at the approaches; the open squares at 'wall',
0 here, a wall at once; 9 at 'raised'), and the screen in RGB, all 320 by 200 pixels, except the mouse cursor (the shield at the corner, 16
by 18: the front end draws it, not the screen the port's BMP holds). After the arena's redraw (`59c7:1e99`) var 0x37 is the open
squares in both (COMBAT's lowering of 9), the party and the monsters' records and combat blocks, c6f1, ad6b and the undead count agree as in
`load`, and eclplay stops at the placement. The close-up is one frame (PICA 21), so its screen at distance 0 agrees; PIC 14
is not used: it animates during PRESS KEY (`63ae:0269`), so a settled screen catches a later frame than eclplay's.
Flow `field` (step 4, parts 7 and 9a: the battle's setup, the dungeon builder, and the combat records, `59c7:1dd7` to the placement). For two
party squares, the fixture's (saved game A's, 7, 13, facing 0) and the west edge's (0, 3, 2, written at DS:c9f0 and
eclplay's `--at`): the block as `encounter` gives it to COMBAT, the original trapped at `5045:078b` (the first turn, after the arena's redraw, which follows the placement; the placement's
entry, after the tiles, the dungeon builder and the combat records `59c7:0f7b`), where the 50 by 25 cells, each record's combat block, RandSeed and the mode are read (the original's
spin holds, and the battle is in 3D: var 0x22 set). Four squares: the fixture's, the west edge (0, 3, 2), (12, 12, 0)
(049e's door corner and the 17th column) and (4, 14, 0) (the 17th row, 0285's door again); each catches a mutation the
others do not: 049e's door corner off and its tile 8 -> 9 fail at (12, 12) only, and side_kind on the 16 by 16
bound fails at (12, 12) and (4, 14); the fixture and the west edge agree with each. eclplay's `--field` at its stop (`BATTLE_STOP`: the placement, not yet ported) must agree: the cells all 1250,
the header's +6, +7, +8 (cursor 0, cursor size 1, see-all 0), and RandSeed as the seed the battle began with: the
original draws nothing past SEED, as eclplay does, because the box freezes the memory cache's purge draw (below). The
purge counters (`PURGE_COUNTERS`, DS:d9ba and DS:da8c, read at SEED and at the trap) are still read and recorded: 0, 9,
9 and 0 at the four squares in the run that froze them (`field1.out`; the west edge 9, 9, 9, 11, 11 in five runs of that square,
the same inputs; the port has no heap). The party list's records agree at the placement, all 18 (pointers
masked, the computer flag +0x18c included: 52db:4dd2 sets it before the placement, and the records are compared whole
in the load flows too). The check also reads that eclplay prints "A battle begins..." once.
The memory cache's purge, frozen in the box only (`dqk_fight_lib.freeze_purges`, called by `dqk_fight.py`'s
`enter` after each flow's block entry, so every fight flow has it). 27fa:05f8 calls 27fa:0f14 while the free memory is
short, and 27fa:025b calls it when its table is full (the 0x30 entries of DS:d9bc); 0f14 adds one to DS:d9ba (argument
0) or DS:da8c, then draws random(3) at 0f2c (lcall 2102:0234, `9a 34 02 02 11` in the file, its segment relocated in the
box), and the `or ax,ax` at 0f32 picks the walk (0: the LRU order, DS:d6ba from its end; else the block numbers from the
top, da80-1 down to 0; both take the first block no handle in DS:d98a refers to). The box replaces the lcall with
`xor ax,ax` and three nops (`31 c0 90 90 90`): AX is 0, the same stack (the argument pushed at 0f2b is popped at 0f31's
`pop cx`), the purge still counts and evicts, and nothing is drawn. The bytes are checked first (push bp at 0f14, the
lcall with its relocated segment, push bp at 2102:0234; BoxError otherwise). Why it is safe: AX is the draw's only
result, read only at 0f32 (later writes of AX come first at 0f3e, 0f6e (cbtw) and 0f70; 0f9a's `mov al,0` is the no-victim
return), and AH is 0 in the draw too (random(3) gives 0..2); the callers read AL only (025b's 0329, 05f8's
062e, each `pop cx; or al,al`), or read BX, CX and DX only after writing them (05f8's 0a46 and 0baf, 0652's 03f7 and 087b), and
2102:0234 clobbers exactly BX, CX and DX (it keeps SI and BP). So the change is a legal outcome of the original's own
draw (0 is one of its three values) and only the victim differs: the victim is a block of the file-read cache (27fa:000d
reads a file into it, through 10d7:26ef with 3Fh; no `mov` of AH 40h, a write, appears in 27fa), which the cache reads
from its file again when it is next used (the reload is 27fa:000d's; the lookup that finds a block missing is not traced). Which block is cached when is the heap's state, which the port does not model, and nothing the check compares
(the cells, the records, the variables, RandSeed) depends on it: the field's 1250 cells and the records agree at all four
squares with the freeze, and the purge counts still vary (0 to 11). Every fight flow checks the draw's five bytes at its
trap (`dqk_fight_lib.frozen_at_trap`: `31 c0 90 90 90`), so a flow with no purge still proves the patch is in place.
Memory settings do not stop the purges: with `memsize=64`, `ems` and `xms` true, or `umb` true (the Box's own
settings are all off), the west edge's purges were 5 to 7 (2026-11-02, probes; umb moves DS:da80 at the battle's call
from 20 to 19, not the purges), and DQK_FULL.EXE makes no EMS call (no INT 67h). The view's
origin (DS:bedb +2, +4) is compared at the stop: it is written after the placement (`1dd7`'s `5837:0aea`, `0b19`).
Every combatant is compared, the entries of size 0 too (`placed` in `dqk_fight.py`: the trap's spin holds with the
count at 2 or more and the party's head set, so a stop taken before the placement is waited out, as the six-loads
flow needs).

Flow `wild` (step 4, part 8: the open ground's builders, `59c7:0e3b`, to the placement). The block is the fight
block with the battle outside 3D: after LOAD FILES it stores var 0x22 0, var 0x1a2 and var 0x33 (`dqkecl.fight`'s
`ground`), the variables 59c7:1dd7 and 0e3b read. Var 0x1a2 is the Abyss's encounter state: the only ECL SAVEs of it
are blocks 64 and 66 (values 0, 1, 2, 4), and 0783 reads it as the index into `DS:5d7b` (the flags, 32 of them) with
no other reader; var 0x33 is the river, read at 086a, and its shipped SAVEs are blocks 3, 5, 30 and 42 (0 or 1). So
the block stores values the game stores, not a saved square's (the overland's map and terrain are not in a saved
game). The rest is the game's own COMBAT: outside 3D, `6c70:01c3` only sets var 0x38 to 2. Two grounds from the same
seed: var 0x1a2 1 (flags 0x18, no river) and 2 (flags 0x11, the river). Each is checked as `field` checks the
dungeon: the original trapped at `59c7:0f7b`, the cells (1250), the header and the records agree with eclplay's
`--field` at the stop, and RandSeed is the original's after its draws past SEED (2502 and 2259: the trees, river and
scatter roll one for one), not SEED as the dungeon's is. The unit test (`test_dark_queen`'s `test_wilderness`) pins the builder with
flags 0x71 and 0x06 from seed 12345 under Dark Queen's generator; `test_monster` runs the flags index (var 0 and 1
differ; 32 stops with COK_ECL_UNDEFINED).

Flow `slots` (step 4, part 11a: the combatants' icons loaded, `dark_queen.c`'s icon table). The fixture's dungeon and
`WILD_GROUNDS[0]` (the open ground), each run as `field` runs it to the placement, and the original's screen read at the side
panel's entry (`177e:085f`, after the turn's cursor is drawn: `5045:0846` calls it after `5045:0827`'s `5837:12f5`; the stop
eclplay makes too, the cursor on). The original's head layers DS:a848 are all 0 (no CHEAD in Dark Queen, `204c:00e5`), its six party
members' records (+0xc4, +0xc5), each combatant 1 to count - 1 (its record, +0x9b, +0x18b, status +0x64, combat facing
+0xa, sx and sy at DS:aeac and DS:aee0 + n, size at DS:c3f1 + 6n + 5), the battle's set (DS:ca03, 4) and the GORGON's
CPIC id (DS:75b8 by var 0x3a). eclplay's run (`--load --play`) writes `--icons` (`slot image hex` a line per slot 0-0x25
and image, `-` when empty) and `--screen`. Checked: each party member's slot holds CBODY +0xc4 and +0xc4 + 0x80; the
SHARKMEN's slot 8 holds CPIC 64 and 192 and 170e's slot 9 the GORGON's CPIC id and +0x80; slots 0x0f-0x25 hold COMSPR
k - 15 and +0x80 (each byte for byte against hlib's records). Then the view (168 by 168 at 8, 8): each combatant drawn
(size > 0, +0x18b or status 10, cell 0-6) has its slot's image 0 decoded, mirrored for facing above 3 and laid at
24x + 8, 24y + 8 over eclplay's screen in the set's and ALWAYS's RGB (scope.md's note), and the result equals the
original's screenshot in the view with only the pointer masked (8, 8, 8, 9). The unit tests (`test_queen_arena`,
`test_monster`, `test_dark_queen`) pin the loader; this flow pins the table against the original's.

Flow `icons` (step 4, part 11b: the combatants' icons drawn). The runs of `slots` (the fixture's dungeon and `WILD_GROUNDS[0]`)
and the dungeon with the party on the west edge's square (0, 3, 2, `FIELD_SQUARES[1]`, where a SHARKMAN, slot 8, stands in the
view at (5, 4) in the original: the answer to Q2, read at DS:aeac and DS:aee0 + n; the fixture's SHARKMEN are at 10 and 8, outside),
each to the same stop (`177e:085f`: the turn's cursor is on in both, which `5045:078b` is not). eclplay's `--screen` (its icons drawn by the port, the combat frame of 20b2:02fa too) must
equal the original's screenshot over the whole 320 by 200 screen in RGB, with only the mouse pointer masked (`dqk_views.POINTER`,
ALWAYS 2 at the corner: 1-16 by 1-16, where the DOSBox pointer's arrow lies at 2-15 by 2-16): 0 pixels. Gated: at least 6
combatants drawn in the view in every run, no combatant at cell 0, 0 (under the pointer's mask), and a monster (CPIC) drawn in the
view in the square's run.

Flow `dispatch` (step 4, part 10b: the first turn's dispatch). The original's turn (`5045:078b`, held) is given back,
and its command menu (`5045:08cf`, the party's dispatch of `cok_combat_act`) is spun in the same copies (EB FE over
its entry, 0x144 on): the state is read where the dispatch starts. DS:ad67, the selected record, is the turn list's
first combatant (57fb:019e), and the records, blocks, combatants, battlefield, turn list and RandSeed (0x1756582c)
are those at the turn. eclplay stops at the command menu (`5045:08cf`, cited: part 22), after the side panel
(`177e:085f`, drawn by `combat_panel`), so its stop is compared with that state (`placement`). The computer's
(`5045:0891`, `5bb8:0000`) is not checked: the first turn is the party's. The menu's whole screen is
compared too (`menu_screen_agrees`: the original's at its wait, 08cf given back and settled for a second, against
eclplay's `--screen` at the menu's stop, the mouse pointer (8, 8, 10, 10) masked: 0 pixels). eclplay's menu line
is `Aim Use Guard Quick@Delay View Speed End`, which the flow checks.

Flow `keys` (step 4, part 22a: the command menu's keys). From the same first menu, End, Guard and Delay are each
typed in their own run (`keys_original`: the menu as `dispatch` reads it, then the key). The original's next
combatant's command menu is trapped at `5045:08cf` (ROARK, the turn list's next entry, in the same copies), and
its state read there: records, blocks, turn list, RandSeed, the variables and the selected record (DS:ad67). The
trap is given back and the menu drawn and settled a second before its screenshot. eclplay runs to the menu (its
menu screen and state, as `placement`) and to the next combatant's menu after the key (BATTLE_STOP, its last
turn line ROARK). Checked: the menu's line; the original's RandSeed past the menu (Guard: one draw, the message's
wait; End and Delay none), its DS:bd2d (End and Guard 2, Delay 1) and DS:ad67 as the turn list's entry for
DS:bd2d; the RandSeed, records, blocks, turn list, variables and the whole screen with the menu given back.
Traps: 078b is given back before 08cf is spun; 08cf is spun only after the key (the keyboard polls held over
0.6 s).

Flow `turns` (step 4, part 27a: dqk-round-end, part 15c). Five E keys from the first menu (settled between them), then
SHARKMAN's computer turn, trapped at `5045:0891` (the computer's dispatch, `COMPUTER`: 078b's copies + 0x106; turn
6). Checked against the original alone: RandSeed 0x1756582c with no draw past the menu, DS:bd2d 6, the sides
[6, 2], DS:bebd, the pending counts (bd4c, ad6e, ad6f), DS:c2d6 NULL, DS:1cb5 0, and the weapon choice (`weapon_original`
to the panel's call, 177e:085f: its draws exact, no event-1 effect, the pattern and target 0 at the entry, `weapon_entry`).
eclplay runs the same keys through the whole turn and stops at ROLAND OF DUST's menu (BATTLE_STOP, after "turn: ROLAND
OF DUST (initiative 3)"); no eclplay state is compared at SHARKMAN's entry, which its stop no longer shows (`melee` compares
that stop whole).

Flow `melee` (step 4, part 15c: `dqk_fight.py`'s `melee`, about a minute). One round of the computer's fight from the
first menu: five E keys (settled between), SHARKMAN 1's turn (the walk, the stuck steps and the pattern 6, its target),
ROLAND OF DUST's menu (trapped at `5045:08cf` after `0891` is given back, sel 57fb:0000), E, SHARKMAN 2's turn and
round 2's first menu (TIMASALIN, 57fb:019e). The original is compared with eclplay's stops after five and after six
E ("out of keys" at the menus): RandSeed 0x48f85199 (21 draws past the first menu's) and 0x473b4bc2 (29 past ROLAND's),
combatant 8 walked from 32, 18 to 28, 16, SHARKMAN 2 at 30, 11 by round 2, every record and combat block (the target
as eclplay writes it, the pattern), the turn lists, the combatants' places (`--field`), the effect chains, and the whole
screen at each menu (the mouse pointer masked; the screen at round 2 is taken once the menu is drawn: the original is
held at `08cf` until then).

Flow `melee-long` (step 4, part 15d: `dqk_fight.py`'s `melee-long`, about 100 s, in the default flows). The fight from the
first menu to round 6's first menu: each party menu trapped at `5045:08cf` with E typed there, each computer turn trapped at
`0891` and given back at once (the choreography of scope.md's measurement: both traps armed in 078b's copies, the trap armed
again before each key). At each round's first menu (DS:bd2d 1, rounds 2 to 6) eclplay's run after 6 * (round - 1) E keys is compared:
RandSeed 0x473b4bc2, 0x03f33312, 0xbd18fe62, 0xd1724551 and 0xad8313c7 (scope.md section 0's table), SHARKMAN 2 (7) and
SHARKMAN 1 (8) at their places, ROLAND OF DUST's hit points (record 0: 134, then 126 after the first strike in round 5), the
selected record (the turn list's first entry by name: TIMASALIN in rounds 2, 3 and 6, BALTHIR in round 4, ROARK in round 5), every record and
combat block, the turn list, the effect chains, the combatants' places (`--field`) and the screen (the mouse pointer
masked). The table's "TIMASALIN hp" is ROLAND's: TIMASALIN (record 1) stays at 61.

Flow `melee-dying` (step 4, part 16b: `dqk_fight.py`'s `melee-dying`, about a minute, in the default flows). The melee-long run with ROLAND OF DUST's hit points set to 1 before COMBAT (`fight_block(DYING_MEMBER)`: LOAD_CHARACTER 0, SAVE 1 into ECL 0x5fd, the field 0x1fc, on both sides). The first computer turn (SHARKMAN 1 at `5045:0891`), ROLAND's menu after it (five E) and each round's first menu, 2 to 6, are compared with eclplay's runs: the chains, RandSeed, records, blocks, turns, the battlefield's cells and occupants, the combatants' places, the bodies (eclplay's `--field` now writes them: part 16b) and the screen. SHARKMAN 1's target is record 2 (`57fb:033c`), not in reach in round 1, so ROLAND keeps his 1 hit point to round 5 and the fixture's first strike (round 5's turn 8) kills him: status word 5, 0 hit points, size 0 and his body on 27, 13 (cell 0x1f) at round 6's first menu.
Flow `alt_q` in `keys` (part 15c): the Alt-Q dispatch (`0891`) and the weapon choice's panel call are checked against
the original alone (`weapon_entry`). Its flight is `altq_missile`'s.

Flow `auto` (parts 18a, 18b2 and 18b3, `make dqk-checks ARGS="--only dqk_fight --flows auto"`, about 1.5 min, not in the default flows): Alt-Q typed at the first turn's menu in the original, and each round's end (`5045:0f6c`) made to spin and read, until the fight's last round: 30 rounds, the round limit (DS:ae4c + 1 >= DS:b45a: DS:b45a is 15 to round 14, 29 at round 15, 30 from 16; the sides are [6, 2] to round 15 and [6, 1] from 16, SHARKMAN 1 having got away). No trap is armed after the last round, and no continue question comes (DS:b7f3 stays 1, the sides [6, 1] at the round limit): the first key wait is the results' (18b3), read as `won`'s E (mode 6, the flags, standing, fled, monsters, out, b7a7, share, the pool's coins, the six records, the 1 KB of variables), and Return gives the adventure menu (mode 4, RandSeed unchanged), read the same way. Each reads its screen (the results' with the mouse pointer and the bar masked, the menu's with the pointer masked). eclplay's `--combat auto` run stops at the results' wait (`(out of keys in block 1 at 8030)`, with `--state`, `--records`, `--screen` there), and the same run with one more Return at the menu (`8031`); a fight past 40 rounds fails. Each round's screen is read flipped: `flip_continue` runs the original's page flip (`19a4:06d3`) at the spin, through a trampoline in the hook TSR's PSP command tail, and gives the routine back. The unflipped screen is out of date where the last turn's redraw is on the back page (DS:1cb5 set at 23 of the 30 ends; the screens of rounds 20 and 23 differ by 343 px unflipped, ROLAND's cursor box). eclplay runs the block three times, Alt-Q typed (`\r\r\@Q`), `--combat auto` (`\r\r`) and that with one more Return, and the first two must write the same 30 rounds' files (screens, records, blocks, field, turns). Each round's records (pointers masked), combat blocks (block +0x15, the fleeing byte: 1 for SHARKMAN 2 from round 16), turn list, combatants, battlefield, RandSeed and screen (MENU_POINTER masked) agree with the original's. A hold is taken as the round's end only where `5045:059d`'s loop can have left (the entry at DS:bd2d NULL, a side gone or DS:ae4c >= DS:b45a) and a second hold sees RandSeed and DS:bd2d unmoved: the keyboard polls also stand still for more than 0.6 s inside a turn (round 2's turn 4, SHARKMAN 1's), the spin not yet reached. The strict wait refused 41 holds in the run that measured them (47-48 in the scope's three runs); none of them was a round's end. The Quick check (`keys`) now runs past the hand-on: eclplay plays c's computer turn and waits at the next menu.

Flow `altq_missile` (part 15c, `make dqk-checks ARGS="--only dqk_fight --flows altq_missile"`, about a minute: 0.4 min in the last run): Alt-Q's
first missile flight. The original runs from the first menu with `\@Q` typed there, and its entry's trap is EB FE in the
copies of `52db:2888` in overlay 0 (`arm`; the root image has none); the wait holds when the spin does and RandSeed does
not move over a second, twice (the computer's turns run with no key wait). Then the flight's strike: `148f:077f`
(`mov al, 0x69`, event 4 on the attacker, in the root image, so its trap holds; `78e4:1aa2` is not trapped, overlay 9 is
loaded by that call), where the entry's trap is given back first. eclplay's run with the same keys stops at the strike
(`effect 0x69 (dqk 78e4:1aa2)`: since part 18b1 ported 0x69, a check build cites it unported again, `missile_eclplay`,
so the port stops where it first runs, before its handler), and its state there is compared: RandSeed 1039 draws past
the menu at the entry (`52db:2888`, 0x484d403f; 1034 past the panel call), 1044 at the strike (the flight's three frames, 148f:1771's d20 and
148f:199e's die), every record and combat block, the turn list, the combatants (`--field`), the selected record
(`57fb:04da`), and the whole screen with the menu's pointer masked (the flight's restore is in it). Earlier runs that
trapped `box.from_ghidra(0x52db, 0x2888)` hit no code (that maps only the root image), and their 72 and 202 draws were
moments of the computer's turns, not a flight.

Flow `round` (part 27a). Fixture 1: var 0x2e is 2 at the entry (before set_seed; `--set 2e=2` in eclplay); five E
keys, the state before the sixth, then the sixth key with 0891 and 08cf both armed: round 2's first menu (CINDER,
DS:bd2d 1, `08cf`'s copies). Checked: the records, blocks, turn list of round 2, RandSeed 0x427245c1, the whole
variables (var 6 0 -> 1: one minute; DS:ae4c 1), the sides [6, 2], the pending counts and DS:bebd, and the screen
after the menu is given back.

Flow `won` (part 27a: the battle's end, `dqk-round-end`). Fixture 2: the setup's end is trapped at
`148f:0512` (the 'loaded' PRESS KEY; a spin that the setup's pass reaches, 59c7:1eb9's event 8). Each SHARKMAN
is made to have 6 hit points and no action (+0x64, +0x18b), DS:b7f3 is 0 and var 0x2e 1 (so 135c's clear shows).
Then `2093:0015` (the flags' clear) is trapped, then `75cc:0723` and `0736`, and `75cc:0e93` (the treasure menu,
after the results' key) with `5f07:0987`. Checked at `2093:0015`: var 0x2e is 0, the sides [6, 0], DS:c6ed is
1d99:0fd1 (its segment is the map's load in the box: the pointer is only checked to name offset 0fd1, not the
segment's bytes), DS:c2d6 NULL. The records' effect chains are kept from 2093 to the treasure menu (the party's six;
none 0xb0, 0xd0 or 0x0b). At the treasure menu: mode 6, the flags 1 0 0 0 0 0, standing 1, fled 0, monsters 1, out 0,
b7a7 0, share 1336, pool 8 0 0, RandSeed 0x3039 (the won path draws nothing). eclplay's `--combat won` run to the
results' key wait (its screen, the mouse pointer and the bar masked: RETURN's cell blinks) and to the treasure menu
(`--state`, `--records`, the variables, the screen) agrees with them.

Flow `drop` (part 21c: a defeated monster's items go to the pool, `dqk_fight.py`'s `drop`, `make dqk-checks
ARGS="--only dqk_fight --flows drop"`, about 30 s, in the default flows). Won's run with THENOL WIZARD x2 (MONCHA 41: a
readied dagger and 20 darts each; the copy's list is reversed) in place of SHARKMAN, to the results' key wait, the
treasure menu, then Take's bar (`t`), its item list (`i`), ROLAND's Return on the darts, Escape and Exit's question
(`e`). The original's pool at the menu is 32 6 0 and the four items, head first; RandSeed 0x3039 at each wait; the
darts go to ROLAND (5 items, weight 910 to 1010). eclplay runs `--combat won --mouse --items` with the keys so far, and
`--shots` (the screen where the keys ran out: list_menu clears its controls on that exit, so `--screen` would miss the
sword cursor over the list): each screen agrees whole with the original's (the sword over the list, the pointer's corner
masked at the end only), and so do the variables, the records (pointers masked), the coins, RandSeed, the share and
the pool's and each record's items.

Flow `walk` (`make dqk-checks ARGS="--only dqk_fight --flows walk"`, about a minute): `tests/test_dark_queen.py`'s
`test_first_fight` in the original, the shipped ECL (no test block): the start menu, Load's saved game A (plta),
Begin, block 4's texts to Caergoth and the steps to the dragons (`TO_FIGHT`), THEY ATTACK!'s Return, the first
turn's menu (`5045:08cf`, `menu_from_turn`: to_menu's traps), End, the next turn's menu (`arm`). The original's
RandSeed at the start comes from its title's cycles, not eclplay's, so it is set at the start menu to eclplay's
there (0x3e6298eb: the title's seven random(8) from 0), and eclplay runs as `--load --play --seed 1046649067`
(`test_load_and_play` holds it equal to the `--start-menu` route; the two routes' turn lists are checked equal
too). Load draws nothing in the original. Begin's copy protection draws two in both (`65cc:00cb` random(20), the
entry; `00d8` random(3), the word; the redraw while the entry's word byte at DS:6dd2 + 0x14 entry + 5 is 0xff never
fires in the shipped table, so the port does not port it), so RandSeed is not set again: it is eclplay's at block
4's first text (`81bc`, asserted exact, still while it waits), and from there it agrees draw for draw: RandSeed at
THEY ATTACK!'s wait (`8c0e`, after the texts' delays and the steps: 1850 draws), the turn list (5045:0447: each
entry's record and initiative, all 19) and RandSeed at the first menu (TIMASALIN, initiative 7, DS:bd2d 1, the
selected record the first) and after End ('CINDER', initiative 7, the entry 1 at initiative 0 by then).

The tail's screen is the original's after its own page flip: 078b's tail redraw of the cell (`5837:025d`: the
ground by `172c` through `19a4:0515`, whose begin, `19a4:05ee`, copies the front page to the back, `22b7:0849`, and
draws to the back, `22b7:0c21(0)`, DS:1cb5 set; then the icon by `204c:030f`) is not shown until the next flip
(`19a4:06d3`, `22b7:0a0b`), before the game's next wait. At 066a the shown page still has the menu's cursor box
(356 pixels, the 24 by 24 cell at (104, 80)). So the flow checks DS:1cb5 is 1 there, then runs the flip at the spin
(`dqk_fight_lib.flip`: lcall `19a4:06d3` over the trapped lcall, then EB FE 5 bytes on, written paused; DS:1cb5
0 after it), and takes the screenshot after it. That is what eclplay's one screen holds, as at every wait.

The same flow runs the command menu's four commands (step 4, part 22b; `command_original`, `command_port`,
`commands`), each from the same first menu: Quick (Q) trapped at `5045:0a56` (the lcall to the computer's turn),
Alt-Q (`\@Q`) at the computer's dispatch `5045:0891` and flipped as above, Alt-M (`\@M`) and Speed (S, F, E) trapped at
`5045:0cd7` (the menu built again), the menu's wait screen taken once the trap is given back. Eclplay's stops are
the computer's turn (`5bb8:0000`), Alt-Q's missile flight (its strike, `148f:077f`: see `altq_missile`) and the keys running out at the menu or the speed bar.
Checked against the original: the RandSeed draws past the menu (Quick, Alt-Q and Alt-M one; Speed none), the
records' COMPUTER byte (Quick: record 1 only; Alt-Q: all eight, the monsters too), the combat blocks (record 1's
initiative 9 after Quick, 0x13 after Alt-Q), the turn list, DS:bd2d (1 after Quick and after Alt-Q), DS:b1cb
(0 to 1 with Alt-M, eclplay's "magic on" once and the menu built twice), var 0x12 (4 at the menu and on the speed
bar, 3 after F and after E) and the screens: the speed bar after S and after F with the mouse pointer masked
(`dqk_views.POINTER`), the others with the menu's pointer masked (Alt-Q's is compared at the strike: see `altq_missile`). Bandage and Yell have their runs
below; the mouse areas have theirs in the `mouse` flow.

Two more runs (step 4, part 22b's leftovers; `commands` after the four above). `altx` is `command_original`'s
Alt-X (`\@X`) trapped at `5045:0cd7`, as Alt-M: the box has no "Gem" argument, so the original answers
"That doesn't work" (`177e:153c`, one draw), the menu is built again and its screen is the check. Eclplay's run
says "That doesn't work", builds the menu twice and ends at the menu. The gods (with the Helm) have no run: the
box runs without "Gem", and the repo test (`test_alt_x`) checks them. `bandage` is a run on a patched saved
game: `dqk_modify.modified({}, {0: {0x64: 5}})` makes ROLAND (member 0) dying, so TIMASALIN's first menu has
Bandage; B is typed there (`keys_original` with `data`, the box's `SAVE/SAVGAMA.QSV`), the original is trapped at
the next combatant's menu (`5045:08cf`), and eclplay runs from the same saved game (`saved_entry` with the same
bytes). Checked: the first menu's text and placement, member 0 at status word 4 at the stop, the state there
(records, blocks, turns, RandSeed one draw past the menu: the say's wait), eclplay's "is bandaged" logged, and the
screen at the next menu (the pointer masked). `make dqk-checks ARGS="--only dqk_fight"` runs both as the rest.

`yell` (`yell_check`, last in `commands`) is E at TIMASALIN's and at ROARK's menus (each as `turns`: the menu trapped at
`5045:08cf`, the key, the trap held, given back, settled), then Y at WALKER KEYS' menu (a kender: the built text "Aim Use
Guard Quick@Delay Yell View Speed End" in the original's memory, searched, and eclplay's menu line), the next menu (BALTHIR
NOMOON's) trapped. Checked: RandSeed two draws past WALKER's menu (both SHARKMEN's saves, made), no record with 0x8c
(the chains agree with eclplay's `--effects`), DS:b45b (the yelled record) is WALKER's record, the records, blocks, turns
and variables at the stop, and the screen once given back (the pointer masked). eclplay's run is "EEY" from the first
menu. A failed save (its flash, a stop) and "is Unaffected" have no run: the repo tests (`test_yell`) check them.
The yell run also checks that the two Ends before Y (TIMASALIN's and ROARK's) draw nothing: RandSeed at WALKER's menu
is the first menu's.

`mouse` (`mouse_check`, last in `main`'s flows) is the command menu's mouse areas (`1bdb:1595`'s, with the mouse on,
which the box gives with `sdl.mouse_emulation=always`, as eclplay's `--mouse` does). The box reaches TIMASALIN's
menu (`to_menu`), and the pointer moves (`MouseBox.point`, xdotool) to a spot in each of the eight areas round her cell
(3, 4 on the view), then to the cell and to the gap above it (no area). At each spot the whole screen (nothing masked:
the game draws its cursor, whose shape is the area's) is compared with eclplay's `--mouse` run to the spot: its keys
`\r\r\mX,Y,0.\e` (the pointer's move, then an Escape, which leaves the menu waiting, its last shot taken after the
move). Space typed at the menu enters `1595` again (the menu takes it and goes on: its controls are kept, and the
type-7 control at `1810` answers with the same 0x20). Then the pointer is on area 4 and clicked: the original's
Move routine (5045:1099) is trapped where the turn's copies start (`arm`) and reached; the trap given back, the Move
menu ("MOVE/ATTACK, MOVE LEFT = 10", TIMASALIN at 27, 12) is drawn and waits, and the whole screen is compared with
eclplay's `--mouse` run to the click, whose last shot is that menu (the keys run out there); `code: 0x87` is logged.

`move` (`move`, after `mouse` in `main`'s flows; its originals `move_original`, `move_wild_original` and
`move_ally_original`) is Move with the keys (part 23). Three runs, each against eclplay's run of the same keys. The dungeon's `\>U`, then `\>\>D` (Right, Undo,
Right, Right into the wall, Done): the command menu's build (`5045:0cd7`) is trapped for the Undo's (the first), given
back, and re-armed for Done's; the state there agrees (RandSeed 23057a5d, one draw: the wall's message; TIMASALIN's facing
2 and movement 0x16 in her block; the combatants, the records, blocks and turn list), and the menu's screen after it
agrees. The wild's `\#51` three times, then `\v` (the diagonals, then Down into SHARKMAN 2): the melee's turn tail
(`5045:066a`) is reached (four draws, RandSeed 1ad549da, BALTHIR's block 000001000001000000010408000000000000000000000001),
and the next combatant's menu (`5045:08cf`, record 57fb:0816, armed before the tail's spin is given back: after the
release the overlay's copies can move) agrees with eclplay's stop there. The dungeon's `\>\v` (Right, then Down into
ROARK): the "ATTACK ALLY?" screen is compared with eclplay's last shot (the shield cursor at the corner masked).

`aim` (`aim`, after `move` in `main`'s flows; its originals `aim_screen_original` and `aim_exit_original`) is Aim with the keys (part 24a). The dungeon's `A` (self only): the Aim menu's whole screen after it is compared with eclplay's last shot ("AIM: RANGE = 0" on row 23, NEXT PREV MANUAL CENTER EXIT; the shield cursor at the corner masked). `AE`: the command menu's build (`5045:0cd7`) is trapped after Exit and its state agrees with eclplay's stop, the RandSeed unchanged (no draws), and the menu's screen agrees. The open ground's (`wild_block(1, 0)`) `AN`: the screen with SHARKMAN 2 shown ("AIM: RANGE = 5", the view on her) agrees; `ANPN` holds the RandSeed at the menu's (ce7f005e); `ANPNE` gives the menu after Exit, agreeing as the dungeon's. 17 checks. Part 24b adds Aim's Target on the open ground (`aim_target_check`): Move's three steps, Done, `A`, `N` (the screen with TARGET listed) agree; `T` is the strike, whose turn tail (4 draws past the menu, RandSeed `1ad549da`, BALTHIR's block) and the next combatant's menu (records, blocks, turn list, RandSeed, screen) agree with eclplay's. The carry-over, Move into ROARK and `Y` (`ally_yes_check`), is Move's, not Aim's (Aim's list has no one of her side): the original's var 0x36 is 1 at the tail, and eclplay's is 1 at its stop, where the attack reaches the cited unported effect 0x2f (`78e4:09b2`); the state past it has no comparable stop. 30 checks. Part 25 adds Aim's Manual (`manual_check`, 23 checks): `AM` on the dungeon (the Manual menu on TIMASALIN's own cell, CENTER EXIT) and `AM` then Right (the cell east) agree; `AM`, Right, `ECE` (Manual's Exit to Aim, Aim's Center: her view's origin 0x19, 0x0a with the cursor off, the command menu after Exit with no draws, the records, blocks, turn list, RandSeed and screen) agree with eclplay's stop. On the open ground `ANNPM` (SHARKMAN 2 under the cursor: CENTER EXIT at range 5 against 1) and `ANNPM` then Escape (Aim again) agree. Move's three steps, Done, `A`, `M` and one Down (TARGET listed) agree; `T` is Manual's Target, the strike, whose tail (4 draws, RandSeed `1ad549da`, BALTHIR's block), the next combatant's menu and its screen agree with eclplay's. 53 checks in all.
`turn` (`turn`, after `aim` in `main`'s flows; `turn_original`, `altq_original`, `turn_altq`) is Turn Undead (part 26). ZOMBIES (`wild_block(1, 0)` with monster 37, COUNT 2): BALTHIR's `T` (a cleric of level 10; ZOMBIE 2 destroyed, ZOMBIE 1 out of sight) and the original's next combatant's command menu (`5045:08cf`, reached once End is done) agree with eclplay's stop there: RandSeed 13 draws past the menu, one ZOMBIE at status 8 and inactive, DS:bd2e 1, DS:bd2d 2, every record and combat block, the turn list and the screen. ZOMBIE-DUNGEON (`fight_block(monster=37)`): TIMASALIN's `T`, "Nothing Happens..." (3 draws), the same agreement with nothing destroyed. Alt-Q at BALTHIR's menu: her computer turn, read at its tail (`5045:066a`, after her dispatch `5045:0891` and Alt-Q's wait: one draw): 16 draws past the menu, ZOMBIE 2 destroyed, DS:bd2e 1, against eclplay's second turn mark (`round.c`'s turn mark, `turn_eclplay`, the rounds' writers: the state after each turn's tail). The holds are 4 s of game time (`TURN_HOLD`): a message's wait stops the keyboard polls as a spin does, so the 0.6 s hold of `wait_trap` is too short here. After T the tail trap (`5045:066a`) holds before End (`0b29`), which is why the next menu is read. 28 checks.

`cast` (`cast`, last in `main`'s flows; `cast_original`, `cast_traps`, `cast_key`, `cast_screen`, `cast_hurt`) is Cast (part 28c: `52db:2543`, `849a:145b`, `52db:1f78`, `849a:21b5`, `queen_cast.c`) on the open ground (`wild_block(1, 0)`: two SHARKMEN) with BALTHIR's memorised bytes set by `dqk_modify.modified` (CLW at +0xce, MM at +0xcf; CAST-HURT also his hit points, 0x32 of 106). Three original runs, each key typed alone and waited for: the list (C, Escape: the menu again, no draw; C, Down, C: the Aim, one draw, row 23 "Spell:Magic Missile", N, then T: the tail, 19 draws counted from his menu, SHARKMAN 2 at 0x4b, the next menu WALKER KEYS at 57fb:0816); CLW (C, C: "Begins Casting", his turn ends at WALKER's menu, two draws; E, WALKER's End: his pending cast at its Aim, three draws; T: the tail at six draws, hit points 0x32 to 0x3d, +0xce 0x83); and the abort (C, C, E, E: "Abort Spell" Yes and No; Y: "Spell Aborted" and the tail at four draws, +0xce 0x83). `cast_traps` arms the next trap before the one before it is given back, so the game cannot run past the next stop; a trap in a flight is read once RandSeed has settled (`wait_seed`), as a flight polls no key. eclplay runs the same keys to its stops: the list, the Aim and the question screens agree (the shield at the corner masked), the records, blocks, turn lists and RandSeed agree at each stop, and its turn marks (`turn_port`'s 01 to 03: the first turn's tail, WALKER's, the third's) agree with the original's tails. The message "Spell Aborted" is checked on eclplay's side only (its `message:` line): the original's bar is already clear at the tail's trap, so no original screen shows it.

`cast_auto` (`cast_auto`, after `cast` in `main`'s flows; `cast_auto_original`) is Cast's computer turn (part 29a: `5bb8:0306`, `5bb8:0170`, `queen_ai.c`; part 29b: the computer's aim, `queen_cast.c`) on the same CAST fixture (CLW and MM memorised, the open ground): Alt-M at BALTHIR's menu (the magic on, one draw, the menu again), then Alt-Q (her computer turn: her dispatch at `5045:0891` with two draws past the menu, then the spell choice, which picks MM: its d100 and d5, the cast at once, the announce, the pick of MM's target by the computer's aim and the flight). The original is trapped at the turn's tail (`5045:066a`, once loaded and RandSeed settled: 25 draws past the menu, RandSeed 0x28390bdf, MM's target SHARKMAN 2 (hit points 0x4e to 0x49), the spell cell 32,18, DS:c6f4 1, BALTHIR's +0xcf 0x8f); the trap is then given back and the fight runs to its end (438 draws past the menu, RandSeed 0xadd80f40, both SHARKMEN status 5). eclplay runs the same keys with the turn marks (`turn_port`): its mark 02 (BALTHIR's computer turn's tail; 01 is the turn Alt-Q gives back) agrees with the original's tail (RandSeed, records, combat blocks, turn list; SHARKMAN 2 at 0x49, BALTHIR's +0xcf 0x8f), so the computer's aim and the cast are compared there; eclplay then stops at its cited unported effect 0x2f (`78e4:09b2`, a later part) in SHARKMAN's turn, so the fight's end is checked on the original side only. Run it with `make dqk-checks ARGS="--only dqk_fight --flows cast_auto"`.

`view` (`view_check`, last in `commands`) is V at TIMASALIN's first menu (`view_original`: the menu trapped at `5045:0cd7`
for the rebuilt menu, Escape typed on the sheet). Checked: the sheet's bar is "Items Drop Exit" (TIMASALIN has items and
steel, so no Trade in combat), eclplay out of keys there, and the sheet's screen agrees with the original's (its mouse
pointer, the shield at the corner, is masked with `SHEET_POINTER`, the box the only pixels that differ). After Escape the original draws nothing
past the menu (RandSeed unchanged, 0 draws), DS:ae27 is 1 at the menu before V and after the Escape (`177e:1ad1`; already 1 at the menu, so this does not show the store), and the command menu is built again: eclplay
builds it twice, ends out of keys there, and the records, blocks and turns agree; the screen once the trap is given back
agrees with the pointer masked (`MENU_POINTER`). That screen having no colour difference is what leaves `5837:00e4` (the
mode 5 tail) unported. eclplay's runs are "V" and "V\\e" from the first menu. The keys running out in the sheet,
the weapon attacks' value and Spells have no run: the repo tests (`test_view_combat`) check them.

The six MONCHA
ids the later combat parts check (`MONCHA.GLB` through `cok_monster_read`'s records, unpacked; hit points
are the base 0xc0, AC 0xbb, THAC0 0x87, size 0x8a, movement 0x90, undead 0x5c, magic resistance 0xcc):

- 64 SHARKMAN, a plain melee: size 1, THAC0 54, AC 54, movement 12, hit points 78, 4 attacks 1d6,
  no effects, no items (the flow's monsters, in two).
- 49 MINOTAUR, an archer and size 2: size 2, THAC0 54, AC 54, hit points 71, 1d4; items Two-Handed
  Sword (type 20), Long Bow (24), Arrow (30), Mail Plate (36).
- 41 THENOL WIZARD, a caster: size 1, THAC0 49, AC 50, hit points 40, 2 attacks 1d2; a mage of 22
  (class levels [0,0,0,0,0,22,0]), 22 spells, effects 0x11, 0x1c, 0x29, 0x14, 0x39, 0x7a; items
  Dagger (4), Dart (5).
- 37 ZOMBIE, an undead: size 1, THAC0 44, AC 52, movement 6, hit points 9, undead 2, 1d8,
  effects 0x62, 0x8d, 0x9e, 0x9f, 0x8f.
- 36 WRAITH, a level drainer: size 1, THAC0 45, AC 56, movement 24, hit points 27, undead 7, 1d6,
  effects 0x67, 0x65 (the drain, 7e76:093b), 0x62, 0x5c.
- 45 RED DRAGON, a dragon: size 4, THAC0 54, AC 61, movement 24, hit points 88, 4 + 2 attacks 1d8+4,
  effects 0x49, 0x64, 0x54 (the breath), 0x18.

Saved game A's six members (`DQKAssets/SAVE/*.QCH` in the order of `SAVGAMA.QSV`; race as the sheet's
list, class as its list, the class levels as the record's seven, the spells known and the spells a
day, rows 1-4): ROLAND OF DUST (ROLANDOF: Human, class 1 Knight, levels [0,11,0,0,0,0,0], no spells);
TIMASALIN (Qualinesti Elf, class 9 Cleric/Fighter/Mage, levels [9,0,9,0,0,8,0], 51 spells known,
spells a day [6,6,4,3,1] [5,3,3,2]); 'CINDER' (Qualinesti Elf, class 9, levels [8,0,9,0,0,8,0], 49
known, a day [6,6,4,3] [5,3,3,2]); BALTHIR NOMOON (Silvanesti Elf, class 10 Cleric/Ranger, levels
[10,0,0,0,10,0,0], 45 known, a day [6,6,4,4,2] [2] [1]); ROARK 'CROAKER' (Hill Dwarf, class 14
Fighter/Thief, levels [0,0,9,0,0,0,10], no spells); WALKER KEYS (Kender, class 12 Cleric/Thief,
levels [10,0,0,0,0,0,12], 28 known, a day [6,6,3,3,2]). No member has any spell memorized (the
record's memorized block is 0 in all six).

The original's fight: LOAD MONSTER (6c70:29a3) only registers the monsters (the flag at DS:bd4e, one icon slot more
at DS:ad6b, the slots from 8); COMBAT's monsters branch (6c70:2d82, through 29fc) appends their records, one per
slot, and then 6c70:170e appends ten GORGONs (MONCHA 20: DS:75aa[2 * var 0x3a], which is 0x14 with var 0x3a = 0),
each with an icon slot (6c70:1a3b). Placement (59c7:1d34-1d94) drops the GORGONs (size 0: unlinked and freed),
so at the first turn c6f1 is 12 (the two SHARKMEN and the ten GORGONs appended) and the list holds the party's
six and the SHARKMEN. The loads need LOAD FILES first: the shipped block 4 runs LOAD FILES 4 4 before its dragons
(8066), and block 7 runs LOAD FILES 26 1 (803e; in the box that block ends at once, so the flow uses block 4's).
Without LOAD FILES the fight stalls before placement, so the GORGONs stay on the list in those runs. During
APPROACH (8a8b:120f, then 19a4:0361 loading 8X8DB.TLB from disk 2) the bottom bar shows "INSERT DISK 2 AND PRESS A
KEY" (or "DISK READ ERROR"), the game waiting in 217e:03f2's loop, which prints the prompt (217e:0005) and loops
while its test (217e:01e3) returns 0. Once COMBAT starts, the monsters' loader (5837:00f1, then 19a4:06bf) fails a
read ("Disk read error") and 293f:0236 prints "LBIBase: Invalid Library File", which waits the same way. The disk
prompt is not a missing file. The review's reading (not re-traced here): APPROACH's view draw (8a8b:0874-0959)
loads the wall sets of slots 0-2 from the map buffer's header through 8a8b:119d, which names the library record
by the wall set's id ("8X8D%c": 'b' for ids below 10, then the record is the id); without LOAD FILES the buffer
is empty, so the wall set is 0, and 8X8DB.TLB has no record 0 (its ids run 1-9). After LOAD FILES 4 4 the header
reads 11 11 09 03 11 11 07 04. With LOAD FILES the slot tables read the same at 'monsters' and at the end, and
the first turn's list holds the two SHARKMEN (the flow's checks).
CLEARMONSTERS (5f07:07fe) calls 75cc:151b, which clears DS:c2ed (the undead count), DS:c6f1 and DS:bd4e and sets
the next icon slot DS:ad6b to 8, then 63ee:0002, which frees the treasure pool (the shared clear, as Champions'). It
does not clear the slot tables: DS:c7be + slot (icon), DS:c7c4 + slot (id) and DS:c7ca + slot (count), for slots 8
to 13 (6c70:29ac-29ee; COMBAT reads slots 8 up to DS:ad6b, 2d82). Nothing below slot 8 is read, and nothing in the
tables is stale: a slot is written by LOAD MONSTER before 2d82 reads it. DS:c7c4 + 2 is DS:c7c6, slot 8's icon (0x40,
the id the block passes as the icon).

The sound (round 4). The fight runs with the sound off. DQK.CFG's sound word (its u16 at 2) is 6, none, as
INSTALL writes it (`dqk_fight_lib.files`, `SOUND_NONE`); the sound driver's setup (1fd3:000b) compares DS:acd8
(that word) with 6 and skips the driver for it. With the sound on (the shipped 3, the Sound Blaster), a probe of the
setup block to the first turn with no freeze (dqk_fight's entry, the purge counters read at SEED and at the trap) read
20 draws past SEED in each of four runs here, the same seed (0xbd9c66b5): 9 purges (DS:d9ba + DS:da8c) and 11 other
draws, so the draws less the purges are the sound-off count of 11. The purges explain the extra draws in these runs.
Two things are not settled: an earlier note here had 11 draws in six sound-on runs (not reproduced by this probe, which
counts the purges as draws), and round 3's 18 draws (no run here gave 18; the cause of 18 is not traced). With the sound off the count
is 11 in five of five runs, and the callers are the same in each: every draw reaches 1df9:044d (random(n), which
calls 2102:0234 at 1df9:0453) from 148f:1988, which 8 draws reach from 5045:0624 (through 52db:0004 and 52db:013b)
and 3 from 5045:04d6 (called from 5045:064c). 27fa:05f8, which the scope's name pairing put beside the sound, is not the sound: it is the
battle's movement (27fa:0b20, called by the path walker at 2a3d:04e7; its retry picks an entry of the far-pointer
table at DS:d9bc by DS:da80 and draws random(3) through 27fa:0f14 when the point is off the battlefield), and it
drew in none of the runs here. Until the sound's timing is traced (step 5), fight checks run with the sound off.

Death Knights of Krynn, read headless (see [Games](games.md#games)), is checked by
`tools/dosbox/checks/dkk_views.py`, which `make dkk-checks` runs
(`run_all.py --set dkk`) or `python3 tools/dosbox/checks/dkk_views.py` (about a
minute; DOSBox-X in EGA mode, and two views in CGA mode). It compares,
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
frames (see [Death Knights](games.md#death-knights)), after two more: the frame's tile set keeps its
colour 13 (loaded with 0 transparent, `70eb:002f`: the middle moon's
magenta), and the CGA backdrop has no line row and draws the pictures
(`6d21:0177`). The saved game copies carry the moons' phases the port is
given (`--set` 0x4cf9-0x4cfb); in block 32, whose script does not draw the
frame again after eclplay's first, they are the port's own at the start,
0.

Its front end, `tools/dosbox/checks/dkk_start.py` (about four minutes;
see [Death Knights' front end](start-menu.md#death-knights-front-end)), plays the same keys in the original and in
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

Its treasure, `tools/dosbox/checks/dkk_treasure.py` (see [Death Knights'
treasure](treasure-shops.md#death-knights-treasure); seeded as `dkk_fight.py`; `--flows treasure,vault`, about two
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

Its levels, `tools/dosbox/checks/dkk_levels.py` (see [Death Knights'
training](start-menu.md#death-knights-training) and [Death Knights' camp](camp.md#death-knights-camp); seeded; `--flows train,memorize`, about
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

Its Trade, `tools/dosbox/checks/dkk_trade.py` (see [Death Knights'
View](camp.md#death-knights-view); seeded; `--flows day,midnight`, a quarter of a minute
each): saved game A begun, block 16's rest, camp, View, Trade to TANAR,
Escape at the first coin list. The flag Trade tests, read from its frame,
is the high word of the DOS clock's hundredths (+ 30) as View's menu
began; by day nonzero, and Trade left as in the port (both screens 0
pixels apart); with the BIOS's clock set a second past midnight at camp's
menu, 0, and the original asks for another partner (the port, with no
clock of the day, is not compared there).

Its last menus by index, `tools/dosbox/checks/dkk_menus.py` (see [Death
Knights' camp](camp.md#death-knights-camp) and the ECL's opcodes; seeded; `--flows
lists,ecl,post,clicks,taskclicks`, half a minute each): the spell lists
(Cast's: Space read past, the scan code 0x45 leaving, Exit by Right and
Enter; Memorize's grimoire: Next, PgUp, 0x4e, Down, M, Escape and the
marked spells' Keep; View's to look, `Exit` alone from column 0) and
NEACAIL's memorized spells after; block 16's PRESS KEY by Escape, Space, F1
and the keypad's 5 read past, and its YES NO (6 to No, F1 and Escape read
past, 4 to Yes, No); saved game A made to resume in block 36 at 3,7 (as
`dkk_shops.py` does, with `0x4c1a` 3 and `0x4c2d` 1), a step north to the
soldiers and their HORIZONTAL MENU `CEMETERY PATROL GUARD` (Escape, Down,
5, Space read past, Right, C) and `0x4cec` after; with the mouse, Cast's
list clicked off it and its menu, on the heading's row, the spell's row
twice (cast), and the HORIZONTAL MENU clicked off it and on PATROL, then
PRESS KEY's item. Every screen 0 pixels apart (the eulogy's portrait's
eyes masked: PRESS KEY animates it, as the original's).

Its shops, `tools/dosbox/checks/dkk_shops.py` (see [Death Knights' shops](treasure-shops.md#death-knights-shops);
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
- (#3) The sound (see [Sound](sound.md#sound)) on a Tandy 1000: the noise's character in the
  steps and the title's drums (the NCR 8496 feeds back bits 1 and 5 XNOR'd
  and keeps its register through the effects driver's rewrites of the
  noise control each tick; a chip that reset it on every write would sound
  different), the voices' loudness against each other and against the
  speaker's (the port's levels are MAME's, the speaker's a guess), and the
  clicks the coupling smooths: DOSBox-X's chip is MAME's code, as the
  port's. In DOSBox-X still: an arrow, Fireball and Lightning Bolt,
  against `eclplay --wav`.
- (#4) The sounds a record read cuts (see [Sound](sound.md#sound)): for how long the disk
  read keeps the sound stopped (DOSBox-X's reads take its own time: a real
  disk), and a sound followed by a portrait (not run).
- (#5) Alt-R in combat, where nothing else takes it, should turn the sound
  off as Ctrl-S does: tried only in the start menu.
- (#6) Clouds and pools (see [Clouds, pools and breath](combat-spells.md#clouds-pools-and-breath)): Stinking Cloud
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
  3 cells whatever the mask (see [Display modes](front-end.md#display-modes)). Not run in DOSBox-X: each
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
  see [The mouse and the joystick](front-end.md#the-mouse-and-the-joystick)).
- (#21) The joystick: the keys a push gives and their pace on a real
  machine (the port repeats a held push every 60 ms) and the calibration's
  timeout (20000 polls; about 400 seconds in the port). Needs a real
  machine and stick.
- (#23) A cleric on Auto against undead (see [The computer's turns](combat-turns.md#the-computers-turns)):
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
  [3D view](adventure.md#3d-view)); the port now offsets every set by the first slot's and agrees,
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
  runs only after the first command (see [Adventure loop](adventure.md#adventure-loop)). The port now
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
  text delay flag (`DS:4b59`, see [Library](library.md#library)) for every running text and
  types it, its frames showing the text grow as the original's
  screenshots do (#90).
