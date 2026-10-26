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
