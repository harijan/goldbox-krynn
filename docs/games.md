# Games

Part of the [developer notes](../DEVELOPERS.md).

The port is to play *Death Knights of Krynn* as well (see
[DEATH-KNIGHTS.md](../DEATH-KNIGHTS.md)). Everything that differs between the
games goes in a descriptor, one a game: `cok_rules` in `src/rules.h`, with
Champions' in `src/rules.c` (`cok_rules_champions`) and Death Knights' in
`src/rules_death_knights.c` (`cok_rules_death_knights`, read headless so
far: see Death Knights below). A front end picks it once, before any file
is read, from `--game` (`champions`, the default, or `death-knights`;
`cok_rules_find`, an unknown name an error listing `cok_rules_names`), and
hands it to `cok_adventure_open`, which keeps it in
`game->rules`, read through `cok_game_rules(game)`; the code that has no game (`cok_character_read` and
`write`, `cok_sound_load`, `cok_config_find`, the setup, `daximages` and
`daxcompose`'s `cok_archive_layout`) takes it as an argument. Shared code
reads what it needs from it and never asks which game it is.
`cok_adventure_open` asserts that it was given one, and `cok_game_rules`,
which every read of `game->rules` goes through, that the game has one:
a path that builds a game some other way without it stops at the first
read of the rules, with the assertion, not reading through NULL. (Tests
that build a game by hand set `game.rules` themselves.)

The descriptor holds names, the character record's layout and the data
segment's tables, a section to a concern:

| Section | Holds | Champions |
| --- | --- | --- |
| `id`, `assets` | `--game`'s name; the assets' directory without `--assets` | `champions`, `COKAssets` |
| `text` | the window's title; the title menu's heading at startup and after a demonstration (`1000:0134`, spelt differently); camp's first line (`4888:2c31`) | `Champions of Krynn`, `Champions of Krynn v1.2`, `Champions Of Krynn v1.2`, `The party makes camp...` |
| `archives` | each DAX archive the game reads, by stem: with the ECL file's number (`GEO` makes `GEO1.DAX`), with `file_digit`'s digit (`MON`, with the kinds `CHA`, `SPC`, `ITM`; `ITEM`, `HEAD`, `BODY`, `CPIC`) or alone (`SKY`); the names `6d21:01d0` loads icons by as they are; records read from a file of their own, as a `cok_rules_record` (stem, file, 0 for none, record): the font and the overland map's cursor; and `file_digit`, the digit a file's number gives the names that hold one (Str into a string[1], `4b6d:161b`, `2fd3:1d21`, `6961:05b9`, `6d21:01d0`) | `ECL`, `GEO`, `WALLDEF`, `8X8D`, `PIC`, `SPRIT`, `BIGPIC`, `HEAD`, `BODY`, `CPIC`, `COMSPR`, `CHEAD`, `CBODY`, `SKY`, `TITLE`, `ITEM`, `MON`, `DUNGCOM`, `WILDCOM`, `RANDCOM`, `MIDI`, `SOUND`; font `8X8D1` record 201, cursor `CURSOR` record 1; the first decimal digit |
| `files` | the devices' file (looked for in upper case, then lower) and the paths its lines hold after the third (`config_paths`); the item types; the directory the setup takes the game to run in and the save path's default under it | `KRYNN.CFG`, 1, `ITEMS`, `C:\COK\`, `SAVE` |
| `setup` | the setup's words and answers when there is no devices' file (`src/setup.c`): the display's screen, whether its Tandy makes the mouse `N` (`tandy_no_mouse`), the sound screen's lines, its highest digit and each answer's letter (an answer past the letters writes no line), the letters the file's second line reads as the devices 0-4 (`sound_devices`, `config.c`), the input's screen, whether a third line of another letter leaves flags never set that the game reads (`input_unset`: the run stops after the note), the other paths' line, prompt and defaults (`other_*`, for `files.config_paths` past 1), the calibration's lines and its failure's, and the note at every start | `GRAPHICS ADAPTER TYPE:  [1] CGA ...` (`11c7:0114`), no; `SOUND TYPE:  [1] C/MS` and five more, 6, `CRATPS`; `CRATP` (`11c7:0a5a`); `ALTERNATE INPUT TYPE:  [1] JOYSTICK ...`; no; none; `Push JOYSTICK #1 ...` (`177c:029d`), `Couldn't read the joystick.  Aborting calibration.`; `TANDY 1000 USERS - Make sure NUMLOCK is on.` |
| `saves` | saved games (`SAVGAM` letter `.DAT`) and their characters' names (`CHRDAT` letter number); a character's extensions: in a saved game, in the roster, its items, its effects, its spells' block (a game that keeps one); the debug mode's trace; the saved game's layout past the variables' width (`ecl.variable_bytes`): its wall table's entries and those the loader loads again, and whether the loader keeps the saved last mode (see [Party](party.md#party)); the byte the roster's list leaves a record out by; the bank vault's files, or none | `SAVGAM`, `DAT`, `CHRDAT`, `SAV`, `WHO`, `STF`, `SFX`, none, `DEBUG.TXT`; 3 and 3 (`DS:6d8a`, `4b6d:1b34`), no (the saved mode becomes the last); `COINS` 6 (`4b6d:0453`); none |
| `images` | the archives `daximages` and `daxcompose` read, by layout: XOR deltas, collections, single images; stems, `NULL`-ended; and the frames a header's count byte 0xff gives (`frames_ff`: 255 where the loader takes the byte, `127f:1c2f`; Death Knights' 256, its 256-tile sets) | `PIC1-3`; `SPRIT1-3`; the 20 others; 255 |
| `character` | the character record's layout (`src/field.h`): its size, its file's size, where a spells' block of its own lies, and where each field lies | 409 bytes, none, the fields below |
| `characters` | the routines that recompute a character where the games' differ (`party.c`, each selected by an enum: the stats from the items, `cok_stats_routine`; the classes and levels, `cok_levels_routine`), the import of another game's characters (`cok_rules_import`, or `NULL`), a paladin's experience row and the classes that count as fighters' (exceptional strength, the hit points by constitution) | `6346:0d20`, `66c2:0433`; none; none, fighters, rangers and knights (`60f4:1743`, `1628`) |
| `data` | the original's data segment (`src/data.h`): the tables the port copied from it, by DS offset, and where each table the code reads lies and its shape | 46 tables (`src/data_champions.c`), 58 named; see below |
| `ecl` | the ECL's opcodes, by opcode byte (`cok_rules_opcode`): what each does (`meaning`, a `cok_ecl_opcode`), the operands its handler loads (or a list over a header of `header` operands, or none ever), the operands a failed `IF` skips with it, and the handler the port does not carry out yet (`unported`, cited); the routines `CALL` runs by address (`calls`); the bytes a variable address holds (`variable_bytes`); the variables code reads and writes by name (`vars`, a `cok_rules_var` each: address, width, elements, the value a flag is off at); what a block's start sets (`start`); the special addresses read and stored (`reads`, `stores`); the selected character's fields at `0x7c00`-`0x7fff` (`character`, a `cok_rules_character_var` each, and `class_bits` for a field of class bits); and three handlers' constants: `COMPARE AND` of strings, `DELAY`'s wait, `HORIZONTAL MENU`'s single item (see [ECL scripts](scripts.md#ecl-scripts)) | its 66 opcodes, each its own meaning (`2fd3:0050-36dc`, `3775:1e74`), none unported; `2e10` redraw, `b203` sound, `c01e` step, `c018` wall ahead, `6803` animate (`2fd3:329b`); 2; 53 names, each a word; the portrait 0xff, the rest's encounters 0, magic allowed (`3775:01e8`); 10 reads, 8 stores (`3775:0fca`, `0e06`); 36 entries (`3775:07d9`, `0c00`); values only, the speed's, yes |
| `view` | the 3D view's tile sets (each one's first and last value, `6e22:01ab`, and the colour each is loaded with as transparent, `tile_transparent`), the wall slot that holds wall types 1-5 and that `LOAD PIECES`' two-set path loads first, whether a record's wall sets share one tile set and whether tile values are moved by the slot's set; the overhead map's tiles; `PICTURE`'s overland map; whether CGA mode has a backdrop of its own (`cga_backdrop`: a line row, no pictures) | 1-0x2d, ..., 0x100-0x127 (`DS:1ec2`), each loaded with 13 transparent (`6e22:0050`); slot 1; a tile set each, moved (`69ea:1025`, `1230`); 0x104 and 0x100; 0x79; yes (`69ea:0184`) |
| `startup` | what startup loads past the font (`3e99:005b`): tile sets from `<tiles>1` and the missiles' icons from `COMSPR`, as (first id, first slot, count); the icon slots (`icon_slots`, at most `COK_ICON_SLOTS_MAX`, 35) and their table's DS offset, for messages; whether the missiles' icons are recoloured as a `CPIC`'s outside CGA (`missiles_recoloured`: 8 to 0) | sets 4 and 0, records 202 and 203; 0-11 into 13-24, 25 into 25; 26 slots at `DS:6172` (`3e99:0466` clears 0xd0 bytes); no (`6d21:01d0`: a `CPIC`'s only) |
| `spells` | what is keyed by spell id (`magic.c`, `cast.c`): the ids' count; their names and the spell lists' headings, with their tables' DS offsets and sizes for the messages; each id's handler (`cok_rules_cast`: the port's, a `cok_cast_handler`, and the original's address, or `unported`); the durations not taken from the spell table (`cok_rules_duration`); the spell ids the code names (`ids`, by `cok_named_spell`); the spell lists' sizes; the granted powers' ids; an item power's spell; Display's search; byte 7's values for the caster, one picked and the party; who may use a class's spells (`usable_paladin`, `usable_ranger`, `usable_mage_class`) | ids 1-0x6b (`DS:2077` + 41 × id, `DS:0bfa` + 41 × level), the handlers `5b04:58ed` stores at `DS:6e3a`, 7 durations (`5b04:0f78`), 23 names; 58 and 49 (`DS:473a`, `4774`); 101-107; above 0x38, 0x17 less (`3afb:04b2`); spells 1-0x38 (`4888:17ae`); 1, 2, 4 (`5b04:127e`); no paladin, a ranger from 7, any magic-user (`5b04:0083`) |
| `effects` | what is keyed by effect id (`effect.c`, `cast.c`, `magic.c`): the ids' count; each id's handler (`cok_rules_effect`, as the spells'); the ids each event runs; the ids the party shares and Prayer's reach; those magic resistance leaves pending; the effect ids the code names (by `cok_named_effect`); Display's ids and texts; the rules' routines a game does not have ported yet (the caster level, the saving throw, magic resistance, Display); the effect whose holder's saving throw rolls 1 and where that throw reads a mage's moon; what one an effect does not take "is" | 0x79 (`DS:6b94`, `5b04:58ed` and `3f44:38ea`), events 1-0x18 (`60f4:057c`), 0x15, 0x2d, 0x2e, 0x31 and 0x31 from 6 squares (`60f4:0332`, `0352`), 0x5b and 0x52 (`60f4:04f3`), 80 names, `4888:17ae`'s; none; none, the wrapped byte (`60f4:113a`); `is Unaffected` |
| `moons` | the three moons (`57e4:001e`, `0143`): the days each stays in a phase, and on the frame each one's column and tile | 8, 1, 6 days; columns 8, 19, 30; tiles 0x114 + phase + 10, 6, 14 |
| `frame` | the screens' frames (`screen.c`), by `cok_screen` (the frame, the open frame, the adventure screen, a big picture's, Memorize's, the spell lists', combat's, the sheet's, the credits'): each a `cok_rules_screen`, the screen drawn under it first, the box cleared (`1128:07e6`) and its lines, rows and columns of tiles (`cok_rules_frame_line`: a tile, or a table by the cell's column or row, and on row 0 the moons), each the frame's `base` + its value; which frame main draws after the start menu (`menu_big`) and Begin Adventuring for a loaded game (`resumed`, a `cok_resumed`) | `1128:0000`-`077c`'s layouts, tiles 0x114 + `DS:0e3a`, `0e62`, `0e7a`, `0ea3`, `0eb0`, `0e8b`, `0ebd`, `0f02`, `0f19` and the code's; the big frame in mode 3 on the map (`1000:024b`); in block 0 or off the map (`4def:01b4`) |
| `start` | the front end (`start.c`, `roster.c`, camp's Save): the title's pictures (record, cell, whether the screen is cleared first, the seconds waited) and credits (lines, their wait, whether they show after a key), a new game's block and the demonstration's file and block; the start menu's items, prompt and word, each item's routine where the port does not carry it out for the game (`unported`, cited: choosing it stops the run); Initialize Mouse/Joystick's file and words (`mouse`, a `cok_rules_mouse_menu`: the file it reads and writes, its list's prompt, word and rows, the calibration's screens and failures); the words of Pick Character, Drop, Exit, Load, Save, the roster's Overwrite, Add, and "from where" for a game that imports another's; the questions' colour; and what its commands do where the games differ (Add's paladin rules, its space, its message and marks; a Drop or Remove that empties the party ending the game; Exit's No saving then quitting; Begin's mode; PROGRAM 0 marking the party on the map; the wall table's entry the end of a game sets) | `2f55:063c`'s pictures, 1, 2, 3 at 5, 1 and 4 at 0, 1 for 5, 10 and 10 seconds, the credits (`2f55:0267`) for 10 unless a key; 0x24, file 2 block 0x39; `DS:0878`, `Choose a FUNCTION `, `Select `, none unported; `krynn.cfg` and `4def:5cc7`'s and `638d`'s words; yellow; paladins, the space, the message and marks; no, no, the entry mode, no; the first |
| `combat` | the encounter and the battle's frame where the games differ (`monster.c`, `combat.c`, `round.c`, `treasure.c`'s end of combat): `COMBAT`'s vault branch and the vault a game's port does not carry out yet; the show at distance's `0xff` for none; leadership before the setup and its words; the dead exploding after a turn and a blow, a side gone ending the round there; the setup's line, its tile counts, whether a set is kept, the open ground's builder (`cok_wilderness_routine`), the dungeon's fill and corners; the placement's clears, ambush, distances, the cell under a body, status 9; the end of combat's lost weapons, shares, effects lost, may-train, the results' words and key, the treasure menu's condition and stop, a destroyed party's words and whether a party destroyed while a block is entered still reads the adventure's menu once (`killed_command`) | the vault none; no; no; yes, no; none; `A battle begins...`, 0x19 and 0x21, no, `COK_WILDERNESS_CHAMPIONS` (`3cb2:0fc8`), no fill, Champions' corners; 255, no ambush, status 9 off the map; weapons back, shares, `DS:0390`'s 11, no may-train, `The party has won.` ..., `press <enter>/<return> to continue`, always (`351b:1968`), no |
| `fight` | the rounds, the computer's turns and the attacks where the games differ (`round.c`, `ai.c`, `attack.c`, `arena.c`, `command.c`, `cloud.c`): their words (`cok_rules_fight_words`: the attack's message, fleeing, turning undead, the computer's, the round's end, the panel, guarding, Alt-X without the Helm cheat); the clouds (`cok_clouds_routine`: Champions' or Death Knights' Stinking Cloud, Cloud Kill, cloud square, the round's clouds and Dispel Magic's part) and their words (one coughing, one choking, a cloud gone, Stinking Cloud's, Cloud Kill's poison); the battlefield's cloud and pool tiles and whether they count as terrain; the death's search for a body, its cell kept for a pool, its spell cleared; the computer's turn: a target first, the items' spells, its spell choice (`cok_computer_cast_routine`), the undead it turns and the paladins who do, its reach, morale's compare and the fleeing's effects, the flight's way; the attack's sides, the free attacks' effect, the creature flag for no dying words, a lost spell's mark, the arrows, the missile's clamp, turning undead (`cok_turning_routine`) | Champions' words; `COK_CLOUDS_CHAMPIONS`, `starts to cough`, `chokes and gags from nausea`, `The air clears a little...`, `Creates a noxious cloud`, none; 0x1e and 0x1d, no; by the stack's byte, the cloud's only, yes; no, yes, `3afb:03b9`, the stack's byte, none, range less 1, unsigned, `DISPELLING` and `GIANT_SLAYER`, 0; no, none, none, cleared, 5, 7, 0x0c, 0x1e, no, `432f:12b7` |
| `menus` | the menu loop (`menu.c`, `cok_menus_routine`: by letters, or by index, `cok_menu_pick`), the Yes/No's items for the indexed loop (`camp.c`), and the adventure's menu where the games differ (`adventure.c`: whether it keeps its own selection, whether Escape leaves Move), whether camp's menu keeps its own selection (`camp.c`) and a special key's pick of a character (`devices.c`: whether a key but Up and Down keeps the one selected) | by letters (`67b5:03e2`, `177f`); none; no, no (`475c:09ec`); no (`4888:2c31`); no, the first is picked (`546c:3334`) |
| `commands` | the player's commands in a battle (`command.c`): the routine (`cok_commands_routine`: Champions' turn `3995:0573` or Death Knights' `3e05:08af`), the words of Move's steps and of attacking an ally and whether Yes turns the NPCs against the party, Death Knights' menus' words (its turn's two-row menu, Move's, aiming's, the cursor's, Speed's), the cursor's icon slot | `COK_COMMANDS_CHAMPIONS`; `Flee:`, `can't go there`, `Not with that weapon`, `Attack Ally: `, yes; none; 0x19 (`6beb:00fc`, `6346:1a26`) |
| `items` | View and the items menu (`sheet.c`, `items.c`; see [View](camp.md#view) and [Death Knights' View](camp.md#death-knights-view)): the routine (`cok_items_routine`: Champions' `546c:0d74` and `17f9` by letters, or Death Knights' `580d:0c66` and `0ffa` by index), Death Knights' menus' words; the items list's readied column; Ready's thief level for another order's scroll and its word; the words of Use of an item not readied, of the questions of parting with a scroll and of Drop, of Trade's pick for money and the coin lists' first item, of Death Knights' Deposit; Sell's and Id's words, Sell's divisor of a count's price, whether their question starts on Yes, whether Id waits twice; Death Knights' Heal and Cure (the class, the effect and its minutes, the words) | `COK_ITEMS_CHAMPIONS`; none; `" Yes  "`, `" No   "` (`6346:0473`); none; `Must be Readied`, `is it Okay to lose it? `, `Drop It? `, `Trade to?`, `" Select"`; none; `546c:2822`'s and `2a59`'s words, 20, no, no, no; none |
| `shops` | the shop, its Buy and Appraise, the temple and its Heal (`shop.c`; see [Shops and the temple](treasure-shops.md#shops-and-the-temple) and [Death Knights' shops](treasure-shops.md#death-knights-shops)): the routine (`cok_shops_routine`: Champions' `36d0:07da`, `0484`, `58e7:1929`, `340d:0ea9`, `0be1` by letters, or Death Knights' `3b89:06f3`, `0453`, `5c72:0dc2`, `3999:07d3`, `04e2` by index); whether mode 1's redraw shows the portrait last shown in 3D areas; whether a price (Buy's, Id's) is paid from the steel and then the pool's steel, rather than from the money in steel of every coin; Death Knights' words (the menus, Buy's, the funds, a purchase's, leaving money, Appraise's, Heal's, the cures' questions and words) and the spells its cures name (`DS:030c`) | `COK_SHOPS_CHAMPIONS`; yes (`6346:2c17`); no (`546c:3424`, `58e7:0155`); none |
| `treasure` | treasure where the games differ (`treasure.c`, `adventure.c`'s `DAMAGE` and `ADD EP`; see [Death Knights' treasure](treasure-shops.md#death-knights-treasure)): the item table `TREASURE ITEMS` and `STOCK` take items from (a `cok_rules_record` and how many entries it fills, or none); the treasure menus' ways (selections from each menu's first item, special keys told apart from letters, leaving's Yes/No from No) and words (leaving with treasure left, Take's, the items' and the coins' prompts, the coin questions'); taking an item's word when overloaded and whether a counted item joins its kind; the vault's menu items, its line and the deposit's question (View's money's too); `DAMAGE`'s saving throw bonus bits, whether a save made halves the damage, its key; `ADD EP`'s words and whether those who cannot act are passed over first | none (`TREASURE` reads `ITEM<file>`); no, no, no; `There is still treasure left.  ` ..., `Take: `, `Items: `, `Select type of coin `, `How Many `/`How much `; `OverLoaded`, no; none; 0x1f, no, `press <enter>/<return> to continue` (`2fd3:2c80`); `Congratulations ` ... `gains experience!`, no (`2fd3:36dc`) |
| `creation` | Create New Character, Modify Character and the icon editor where the games differ (`create.c`, `modify.c`, `icon.c`; their lists and menus by letters or by index as the `menus` section's routine): the lists' headings; by class id the levels and experience it starts with, a paladin's effect, the classes with no spells' block; the money (the classes' dice, or so much steel); a mage's spells and those creation's training gives by mage level and order (`cok_rules_created_spells`); the reroll's, name's and save's words and whether the save names the character and starts on Yes; the items every class starts with (`cok_rules_kit` by kind, or a knight's alone); Modify's experiences of a new character, its prompt, the race and class whose charisma is held at 17, whether Keep sets `SHARES`, its most hit points (`cok_most_routine`); the icon editor's words, frame slot and most head and body | `Pick Race` ... `Pick God`, `Pick Alignment`; `4def:0ae0`'s levels and experience, no paladin effect, none; the dice; 11, 18, 12, 15, 21 and levels 2-5 any order; `Reroll stats? `, `Character name: `, `Save NAME? ` from No; a knight's (`4def:5534`); 1500, 1251, 2000, 2001, 2251, 2500, 5000 and the level changed at 0, `Modify: `, none, yes, `4def:48e9`; `Is this icon ok? ` from No, `Sorry, not in CGA`, slot 25, 13 and 31 |
| `levels` | training, the knights' orders, aging, Memorize and Scribe where the games differ (`train.c`, `magic.c`, `camp.c`; see [Training](start-menu.md#training) and [Camp](camp.md#camp)): Train's words, whether its question starts on Yes, whether free training trains, the highest levels by race and class in place of the kender's limits, the row of a class without one, whether a ranger learns only when it rises; the fixed hit points at a class's top level and a floor of 0; Knight Change's words, whether Change is always offered and special keys do nothing, the level from which a lost level costs a fixed 2; the aging after a battle (by race the ages, and its words); the spell lists' prompt, the memorize table's labels and columns, how the spells marked are kept (a list with Keep, or a question) and Scribe's questions and words; whether the camp unmarks the spells to memorize; Display's headings; the Magic menu's own selection; the camp's Fix (`cok_fix_routine`: Champions' or Death Knights') and the rest's words (the day's healing, an encounter); the rest menu's items, `Stop Resting? ` and whether it starts on Yes, whether the camp's Rest starts the menu from its first item; the camp Save's quit question | `4def:4d9e`'s words, from No, free training, the kender's limits, row 0x74, every time; 2, 0, 3, 0, 2, 1, 2, 3 and wrapping; `Knight Of The `, `Sword`, `Rose`, not always, F9 as C, none; no aging; `Choose Spell: `, `    Cleric Spells:` ... at 20, the questions `Memorize These Spells? `, `Scribe These Spells? ` in yellow from No; yes (`4888:06b8`); 11; the last menu's; none; `Rest Days Hours Mins Add Subtract Exit`, `Stop Resting? ` from No, the first; `Quit TO DOS ` |

A `NULL` stem says the game has no such archive (Death Knights has no
`SKY`, `CURSOR`, `RANDCOM`, `HEAD` or `BODY`): code that reads it must not
be reached for that game. `archives` also holds `ground` and `backdrop`
(Death Knights' `SKYGRND` and `BACK`, `NULL` in Champions; the view draws
what was loaded, see [3D view](adventure.md#3d-view)) and, for a game that picks a record's file by
the record's id rather than by the ECL file, `pic_files`, `sprite_files`
and `wall_files` (256 file numbers each, 0 for the ECL file's; `NULL` in
Champions). Most record ids are still in the code that reads them (`SKY`'s
250-252 and `SKYGRND`'s 1 and 2 at startup, `RANDCOM`'s 1, the music
drivers' 4 and 5); the startup's tile sets and icons are the `startup`
section's; one that a game reads from another file goes in a
`cok_rules_record` (Death Knights' cursor is `8X8D1` record 204).
`archives.monster_spells` names a fourth kind of monster file, the
monsters' spells, where a game keeps them apart from the record (Death
Knights' `MON1WIZ`, as its characters' `.WIZ`; `NULL` in Champions,
whose record holds them): `cok_monster_read` reads a monster's record
of it, where there is one, into the record's spells' block.

To add to it: put the field in the section of its concern (a new
section, a struct of its own, for a new concern: the character record's
layout, the data segment's tables, the ECL's, the spells', the routines
that differ as function pointers), with a comment saying what it is and
where the original holds or reads it; give Champions' value in
`src/rules.c`; read it through `cok_game_rules(game)` (or the descriptor
passed in) where the literal was; and check it in `tests/test_rules.c`
(every file Champions' descriptor names must be in `COKAssets/`). The
message strings the game shows stay where they are. The port's own
messages that name a file build the name from the descriptor; the tests
check them (`tests/test_monster.c`'s damaged files: `MON1CHA`, `SPC`,
`ITM`, `GEO1`, `WALLDEF1`, `SPRIT1`; `tests/test_debug.c`'s `DEBUG.TXT`
without a saves directory), but for a few checked by review only:
`RANDCOM`'s "record 1 is too short" (unreachable with a record that loads
as its six tiles, which are longer than the map it would hold), and the
font's, `PIC`'s and `ECL`'s load errors. To add a game: a descriptor of its own
in a file of its own (`src/rules_death_knights.c`; every program that links
`src/rules.c` links it, as `src/data_champions.c`), listed in `games`
(`src/rules.c`), which `--game` then finds.

### Death Knights

`cok_rules_death_knights` (`src/rules_death_knights.c`, its addresses
`dkk SEG:OFF` in `build/death-START_FULL.EXE`, `make death-merged`, its data
segment at `DS` 0x1bce) is DEATH-KNIGHTS.md's step 1 and step 2 (its
frames, characters, saved games and front end) and the first three parts
of step 3 (the encounter, into and out of each fight; the fight, its
rounds, the computer's turns and melee; the player's commands; View and
the items menu; and of the later parts, among them, the shops and the
temple): the tools
and `eclplay
--game death-knights` read every one of its archives and run its scripts,
their text, menus and choices, its 3D view and its overland map, read,
recompute and write its characters and monsters and import Champions'
characters, and `cok` and `eclplay --start-menu` play it from its title,
through its start menu and saved games, into its fights: the battlefield
set up and the combatants placed as the original's, the fight played by
keys from the player's menu, or to its end with the party on Auto (Alt-Q
at the player's turn, `eclplay --combat auto`) with the original's dice,
or left with `eclplay --combat won`, `fled` or `lost`, and the script
going on. It holds:

- its files: Champions' stems but `SKY`, `CURSOR`, `RANDCOM`, `HEAD`,
  `BODY` and the music's (`MUSIC` for its songs in place of `MIDI` and
  `SOUND`: see [Death Knights' songs](sound.md#death-knights-songs)), and
  `SKYGRND` and `BACK`; the digit of `MON`'s and `CPIC`'s files always 1
  (`46b2:198c`, `7090:016d`); `PIC4-7` and `SPRIT4` by picture id
  (`6ca1:00a4`), `WALLDEF`, `8X8D` and `BACK` by wall set id (1 in file 1,
  2 in 4, 3-4 in 2, 5-6 in 3, 7 in 5: `6d21:0ec5`); `DKK.CFG`, `C:\DKK\`;
  the saved games' names (`SAVGAM?.PTY`, `CHRDAT`; see [Party](party.md#party)), the
  characters' extensions (`.GAM` in a saved game and `.PC` in the roster,
  `46b2:0abc`; `.WIZ`, `.JNK`, `.EFF` beside them, `DS:71c0`); the image
  lists (`PIC1-7`, `SPRIT1-4`, `BACK1-5`,
  `SKYGRND`, `8X8D1-5`, ...: 30 archives) and 0xff frames meaning 256
  (`123c:1c58`);
- its ECL table (`3632`'s handlers, the dispatch `3632:2a54`, the skips
  `3c1e:1b48`): Champions' but for ten opcodes, which ecl.h names past
  Champions' meanings (`COK_ECL_SETUP_MONSTERS` and on), among them
  `TREASURE ITEMS` and `STOCK` (the treasure section's), and `DAMAGE` and
  `ADD EP` by Champions' with the treasure section's rules; `CALL`'s
  redraw at `2dcb`; byte variables,
  its named variables, special addresses and character fields (see [ECL
  scripts](scripts.md#ecl-scripts));
- its view: tile values 1-0xff all from tile set 0 and 0x100-0x12c from
  the frame's (`DS:43fa`, `4404`, `70eb:01f5`), the frame's set loaded
  with colour 0 transparent, so that its colour 13 shows (the moons'
  magenta), the others with 13 (`70eb:002f`), wall slot 0 first, one tile
  set of 256 tiles for a record's three wall sets, no tile moved
  (`6d21:0ec5`), the overhead map's 0x119 and 0x129 (`6d21:0000`), the
  overland map's `PICTURE 0x70`;
- its startup: the frame's tile set alone, `COMSPR` 0-21 into 13-34
  (`4f9a:00e1`), 35 icon slots (`DS:b474`, cleared at `4f9a:0556`);
- its frames (`110f`): other tiles of its frame's set, from 0x100: one
  frame (`110f:0000` with 0; every caller outside `110f` passes 0, so
  Champions' open frame is the same frame), row 0 from `DS:070c` with the
  moons, row 23 from `DS:0734`, the sides tile 7 between; the dividers
  across with their own ends (`DS:075c`: row 16 of the adventure screen
  `0162` and a big picture's `02ab`, row 2 of the spell lists' `0537`),
  column 16 from `DS:0784`, the 3D view's box with corners 0-3, rows of 4
  and columns of 7; Memorize's (`02f8`, `110f:0000` with 2: rows 1-16
  cleared), combat's (`0345`, `110f:0000` with 1: all cleared, the bottom
  row 22, column 22 from `DS:07ac`), the sheet's (`0392`: rows 8, 16 and
  20 inside the sides `DS:07d9` and `07ef` + row, column 19 from `DS:07fe`
  + row); the credits the frame (`35c6:026e`); after the start menu main
  draws the adventure screen always (`1000:01d1`), and Begin Adventuring
  a loaded game's frame by the mode (`49cb:010b`: 0 and 4 the adventure
  screen and the party list, 3 the big picture's frame); its CGA backdrop
  is the other modes' (`6d21:0177`: no line row, the pictures drawn);
- its data segment's tables (`src/data_death_knights.c`): all but the
  treasure templates and the temple's diseases, which it has no
  counterpart of, each with the routine that reads it (see The data
  segment's tables);
- its spells and effects (see The spells and effects): names (`DS:5825`
  + 41 × id, ids 1-0x72) and headings (`DS:3770` + 11 × level), the
  handler tables (127 of its 235 ids run Champions' handler, the others
  are unported), durations (`5dc5:0ef5`), byte 7's party value 3
  (`5dc5:1182`), the event lists (`638c:055b`), its cast (`5dc5:1298`)
  and magic resistance (`638c:04c9`, see [Death Knights' casting](camp.md#death-knights-casting); its
  caster level and saving throw: see [Death Knights' fight](combat.md#death-knights-fight)); `MON1WIZ`;
  its moons'
  tiles, 0x100 + phase + 0x15, 0x0d and 0x11 (`5b7d:0000`);
- its characters (see The character record's layout): the record's 216
  bytes (`.GAM`, `.PC`, `MON1CHA`), every field the code reads at its
  place there and the spells' block of 220 bytes (`.WIZ`, `MON1WIZ`) held
  after it; its recompute, the stats `65cd:0e46` (the movement with the
  effects that double and halve it, `65cd:1241`; no fighter level) and the
  levels `69a8:050b` (spells a day in the block to 8 levels, paladins',
  3 or 4 attacks, its own thief skills, no former classes), as
  `characters.stats` and `levels` select; and the import of Champions'
  characters (`characters.import`, `46b2:10d7`, below);
- its saved games (2913 bytes: byte variables, four wall entries, the
  first alone loaded again, the last mode kept: see [Party](party.md#party)), the bank
  vault's files (`VAULT?.DAT`, `TMPVAULT.DAT`), and the savings variable
  (`0x4c33`, a word over two) a Champions game's load copies;
- its front end (`start`, see [The start menu](start-menu.md#the-start-menu)): the title (`35c6:051c`)
  and credits (`35c6:026e`), its start menu (`49cb:010b`, `DS:0cf8`) and
  its words, Load and Add from Death Knights or Champions (`--import`),
  a Drop or Remove that empties the party ending the game, Exit's No
  saving then quitting, Begin's mode from the last mode; Initialize
  Mouse/Joystick's words and file (`52cd`, `58ec`; see [Death Knights'
  mouse](front-end.md#death-knights-mouse)); and a start menu command it does not carry out yet, which
  stops the run citing its routine (none now);
- its creation (the `creation` section; see [Death Knights' creation](start-menu.md#death-knights-creation)):
  Create New Character (`49cb:05c7`), its icon editor (`3cac`), its
  items (`4bea`) and Modify Character (`25d4`);
- its encounter and battle (the `combat` section; see [Death Knights'
  encounter](adventure.md#death-knights-encounter), battlefield, battle and end of combat): `COMBAT`'s vault
  first, the show at distance, leadership, its own open ground, the
  dungeon's fill and corners, the ambush's placement, a round ended by a
  side gone, no exploding dead, the end of combat's effects, may-train
  and words;
  its combat variables (`0x7ec0`-`0x7f72` at Champions' addresses, as its
  table `DS:836`-`864` has them; the vault's `0x4d00`, the river's
  `0x4bf3`); no former classes (`cok_character_former_class` false, the
  former levels read as 0: `cok_character_former_level`);
- its fight (the `fight` section; see [Death Knights' fight](combat.md#death-knights-fight)): the words of
  its rounds, turns and attacks, most in lower case; the player's turn
  (`3e05:08af`, its menu and commands below); the computer's turns (`3f63:0000`) with its own spell choice (`05d7`), no
  items' spells, a paladin's turning; its attacks (`5340`), turning undead
  (`119b`), the free attacks, the death on the battlefield (`6f03:0fbd`),
  a lost spell marked; its clouds (Stinking Cloud's and Cloud Kill's,
  `5dc5:2934`, see [Death Knights' clouds](combat-spells.md#death-knights-clouds)); its saving throw (`638c:10f8`) and the
  effects its first fights carry (0x07, 0x4f, 0x50, 0x5c, 0x5d, 0x69,
  0x6a, 0x6c, 0x72, 0x73).
- its effects (the effects section; see [Death Knights' effects](party.md#death-knights-effects)): the
  handlers that changed, every effect its monsters carry and its level
  drain (`504d:14c7`), the dragons' breaths, the vampires' gaze, a
  monster's Fireball and the fire minions' flames; six new effects left
  (0x02, 0x04, 0x15, 0x22, 0x30, 0x3a).
- its menus and the player's commands (the `menus` and `commands`
  sections; see [Library](library.md#library)'s menus by index and [Death Knights' commands](combat-turns.md#death-knights-commands)): the
  menu loop by index with two rows (`6a69:0f1b`), its Yes/No (`2048`), the
  adventure's menu (`4307:0257`), a special key's pick (`580d:2bb2`); the
  player's turn's menu (`3e05:0ccc`) and commands (`08af`), Move
  (`10bb`), aiming (`5340:3888`), Speed (`3e05:14aa`), Attack Ally
  (`5340:2b81`), the cursor's box (icon slot 0x1c) and the missiles' icons
  recoloured as `CPIC`'s (`7090:016f`).
- its View and items menu (the `items` section; see [Death Knights' View](camp.md#death-knights-view)):
  View (`580d:0c66`) and the items menu (`0ffa`) by index with their own
  selections, the list `6a69:1b82` (`cok_menu_indexed_rows`), the readied
  column a space shorter, Ready's thief level, Deposit in the vault for
  coins and items, Heal and Cure, Sell and Id with its words and its
  steel, and camp's menu keeping its own selection (`4381:3106`);
- its treasure (the `treasure` section and `COK_DATA_STOCK_*`; see [Death
  Knights' treasure](treasure-shops.md#death-knights-treasure)): the item table `ITEM0`, `TREASURE ITEMS`, `STOCK`,
  the treasure menu, Take, the bank vault's menu and its
  deposit, the vault's mode 7, `DAMAGE` and `ADD EP` with may-train;
- its shops and temple (the `shops` section; see [Death Knights' shops](treasure-shops.md#death-knights-shops)):
  the shop (`3b89:06f3`), Buy (`0453`) paid in steel, Appraise
  (`5c72:0dc2`), the temple (`3999:07d3`) and its free cures (`04e2`),
  mode 1's redraw with the small picture always (`65cd:2db0`);
- its levels (the `levels` section; see [Death Knights' training](start-menu.md#death-knights-training) and [Death
  Knights' camp](camp.md#death-knights-camp)): Train Character (`49cb:43b6`) to level 18 by the
  highest levels of `DS:82d4`, Knight Change Order (`49cb:4ea4`, `4da1`),
  the aging after a battle (`3e05:0264`) that its haste (effect 0x27,
  `504d:0d88`) asks for, Memorize, Scribe and the spells' table to 8
  levels (`4381:0f67`, `115d`, `09c7`), Display (`4381:15aa`), the camp
  unmarking only the scrolls (`4381:0626`), and the names of those who may
  train in light magenta (`65cd:1b3b`); and camp's Fix (`4381:2fd4`).

`cok --game death-knights` and `eclplay --start-menu` play it from the
title into its fights: a saved game loaded (its own or Champions'), or
a party added from the roster or from Champions', saves and resumes, and
the adventure in its scripts, 3D view and overland map, camp (Magic's
Memorize, Scribe and Display, Fix), `PROGRAM`'s start menu (Train and Knight
Change Order too), the encounters and their battlefields, their fights on Auto
(the player's turn by keys, `3e05:08af`: its menu, `3e05:0ccc`, Move,
Aim, Cast up to the cast entry, Turn, Guard, Quick, Delay, Bandage, Yell,
Speed, End, Alt-Q and the hidden keys, View and Use with Death Knights'
sheet and items menu; `eclplay
--combat auto` presses Alt-Q, `melee` plays every turn as the port's
melee, and `won`, `fled` and `lost` decide the battle without its
rounds), their end
of combat and treasure menu, the bank vault with its View and Items, camp's
View and Items, the shops (`COMBAT`'s and `STOCK`'s) and the temple, until
a routine not ported yet,
a spell
(Death Knights' casting lists those that cast) or an effect not ported
yet (Death Knights' effects), a
spell or effect it does not name, or a start menu command above; there the run
stops with the reason (`not yet ported: ...`), `cok` showing it on the
screen and quitting, as it does at any stop: the original never returns
to the start menu from the middle of a script, and the port's state there
is half done. `--load` and `--party` read its saved games.

Its characters' files: a record of 216 bytes, read whole into a zeroed
record and written whole (`46b2:10d7`, `46b2:0abc`); the spells' block,
read from `.WIZ` if there is one (what there is of it; the original says
"problem openning magic file" when it is short) and written if the record
holds one, the file erased first; items (`.JNK`) and effects (`.EFF`) as
Champions' `.STF` and `.SFX`: 63-byte records with the next pointer
at +0x2a, 9-byte ones with it at +5. A roster file `NAME.PC` is a saved
game's `.GAM` by another extension, its other files beside it (the name
made as Champions' roster's, `15fe:0580` and `064f`, identical to
`169c:050b` and `05da`: `cok_party_roster_name`). Its monsters (`46b2:19a2`):
`MON1CHA`'s record (216 bytes), its spells' block from `MON1WIZ`'s record
of the same id where there is one (14 of the 64), its effects (`SPC`) and
items (`ITM`); ADD NPC then joins it (`46b2:1d90`, its levels recomputed,
`69a8:050b`). Every one of `DKKAssets/SAVE`'s characters reads and writes
back byte for byte, but the far pointers the original writes as they are
in memory (`tests/test_party.c`), and its recompute gives each one's
derived fields back from nothing.

The import (`cok_character_import`, the start menu's Add Character to
Party, Champions; `46b2:10d7` with `DS:c468` 0): a Champions roster file
(`*.WHO` in the path of `DKK.CFG`'s fifth line), its 409 bytes read into a
zeroed block, moved by ten runs into the record zeroed (the descriptor's
`cok_rules_import`: Champions' `+0x00-0x1d` to `+0x00`, `+0x58` to `+0x1e`,
`+0xcf` to `+0x29`, `+0xda` to `+0x31`, `+0xf3` to `+0x3f`, `+0x11a` to
`+0x6b`, `+0x130` to `+0x6d`, `+0x135` to `+0x70`, `+0x142` to `+0x7c`,
`+0x188` to `+0xc7`), the pointers cleared; for a class but fighter, thief
and fighter/thief (2, 6, 0x0e) a spells' block with Champions' memorized
spells, known spells 1-107 and four classes' spells a day, 5 levels into
rows of 8; the age capped by race (1200, 1200, 200, 300, 300; a half-elf's
above 199 set to 70; 50); its items but those of types 42, 47, 58, 60, 63,
69 and 72 (the set at `46b2:10b7`) and its effects as they are; then
recomputed as a load is. Spell, item and effect ids are not translated:
Champions' ids stand for other spells in Death Knights, and Champions'
former levels (`+0x101`) land in Death Knights' levels before a drain
(`+0x4d`). The original's load also recomputes strength between the
stats and the levels (`638c:16d8` with 0, the effects engine's): the port
does not yet, which changes nothing a file holds unless an item or effect
gives strength by a rule that differs. Its stats add var `0x7f72` (the
party's movement bonus) to a party member's movement, which no Death
Knights script sets: 0 in the port's stats, which have no ECL variables.
Each of `SAVE/`'s Champions characters imported and written gives, but for
the far pointers, the files the original wrote for it (Add Character to
Party, Champions, then Save Current Game, in DOSBox-X: `tests/test_party.c`
holds their digests).

What it has not, or the port does not reach yet, is left out: a field of
the character record it has not (the fighter level, the level a human
changed class at, the levels shared out, an NPC's shares, the former
levels; the code that reads those asks the descriptor first: no former
classes, `cok_levels_former_classes`, the former levels and the level a
class was changed at read as 0, no sweeps, no shares) stops at its
accessor's assertion ("the game's records have no such field"), as does a table of the data segment it has not ("the game's
data segment has no such table"), a spell or effect it does not name
("the game has no such spell", "effect") and a variable it has not
(`cok_ecl_var`: "the game has no such variable"); the four opcodes not
carried out yet stop the run with `COK_ECL_UNPORTED` (see [ECL scripts](scripts.md#ecl-scripts)).
Champions' rules that read the former levels compute, with them 0, what
Death Knights' routines compute where those are the same (the backstab
`5340:27c1` and its damage `014f`, the scrolls `5dc5:08a5` and Use
`580d:1dd7`); who may use a class's spells (`5dc5:0078`) and the fighters'
classes (`638c:16d8`, `15e0`: paladins too) are the descriptor's; the
others differ beyond that: turning undead (`5340:119b`, paladins, levels
clamped, var `0x4cff`) is the fight section's own routine; the combat
menu's Turn (`3e05:0ccc`: clerics, and paladins above 2) is the commands'
routine's. The party list in its frame
is Champions' (`6346:07ba`; dkk `65cd:07a5` colours AC and hit points by
other ranges of its record, `+0xcc` and `+0xd6`), drawn with what the port
reads of the party.
`tests/test_death_knights.c` checks the descriptor against
`build/death-START_FULL.EXE` (its tables byte for byte, the level tables
following each other without a gap, each table's base in the code that
reads it, the names, the tile sets' bounds, the
wall sets' and `SPRIT4`'s files, startup's names, the version; the
frames' tables (each line's tiles a table `110f` reads, by the cell's
column or row), the moons' offsets in `110f:0000`, the frame's set's
transparent colour (`70eb:002f`), startup's `COMSPR` loop (`4f9a:07a6`)
and the icon slots' table it clears (`4f9a:0556`); the ECL
table, by following the compares of the skips `3c1e:1b48` and the dispatch
`3632:2a54` for each opcode and reading the operands its handler loads
first; its `0x7c00` variables' offsets, `DS:836`-`864`; the special
addresses `3c1e:0d85` compares and `0bc1`'s bound of 49000; the import's
moves in its code, the classes without a spells' block, the set of item
types dropped, the record's and block's sizes, the files' extensions;
and that each field Champions' import moves lies where the move puts it;
the encounter's section against its code: `COMBAT`'s branches in order,
`SETUP MONSTERS`' operand, the placement's ambush, its clears, distances,
facing and cell under a body, the setup's line and tiles, the river's
variable, leadership's tests, chances and words, the battle's routines,
the end of combat's effects, words and treasure menu, may-train's row and
table, who may use spells and the fighters' classes; the fight's section
against its code: its words, the clouds' routines (where the round's
end calls them, their words, Cloud Kill's levels, the rounds' compares,
the cover), the probe's tiles, the computer's turn's first target, its
limit and paladins, its spell's columns and levels, reach, morale and
flight, the attack's sides, the free attacks' effect, the quiet bit,
the lost spell's mark, the arrows, the missile's clamp, turning undead's
levels, clamps, variable, table and dice, and the saving throw's effect
0x39; the treasure section against its code: the item table's loader
and its call, `TREASURE ITEMS`' and `STOCK`'s tables and bounds, the
treasure menu's, Take's and the coins' words, taking an item's join, the
vault's items, line, deposit and the routines it calls, `DAMAGE`'s and
`ADD EP`'s constants and words;
the levels section against its code: Train's free training cleared, its
tables' bases, words and question, the routines it calls, the hit points'
fixed amounts and floor, Knight Change Order's words, menu, experience,
scores and fixed loss, the aging's ages and words, the memorize table's
labels, levels and columns, the lists' prompts, Scribe's question and
words, the camp's forget, Display's bound, texts and heading colour; the
items section against its code: View's and the items menu's words and
list, the readied column, Ready's thief level, the words where their
routines load them, the Yes/No's first, Sell's count and Id's steel, Heal's
class, effect and minutes, Cure's byte; camp's own selection)
and
`DKKAssets/` (each file and record it names, each picture and wall set in
the file its id names and no other, `COMSPR`'s icons), its view's wall
sets and tiles, its frames drawn (every screen's tiles by cell) and its
CGA backdrop; `tests/test_start.c` Begin Adventuring's frame for a game
loaded by mode and main's after the start menu, on a copy of Champions'
descriptor with those fields of Death Knights';
`tests/test_death_knights.py` the tools over `DKKAssets/` and `eclplay`'s
runs of its blocks, and its front end (the title to the game, Load and
Save of `DKKAssets/SAVE`'s games, Champions' party, the stops); its
descriptor's front end against the original too (`test_start` in
`tests/test_death_knights.c`: the start menu's items and words, the
title's pictures, waits and credits, a new game's and the
demonstration's blocks, the end of a game's wall entry, the saved games'
and vault's names and layout, the loader's one wall entry and kept last
mode, the roster's NPC flags and sizes); `tests/test_start.c` its saved
games, vault and start menu (`test_death_knights_saves`, `_menu`);
`tools/dosbox/checks/dkk_views.py`, `dkk_start.py`, `dkk_fight.py`,
`dkk_treasure.py`, `dkk_levels.py`, `dkk_items.py`, `dkk_shops.py` and `dkk_menus.py` the original (see [Checks against the original](checks.md#checks-against-the-original));
`tests/test_death_knights.py` its first fight on Auto, by Alt-Q and by
`--combat auto`, 13 rounds as the original's; `tests/test_fight.c` its
fight on records made by hand (the cloud and pool tiles, a death that
finds its body, turning undead's levels and count, the nightmares' smoke,
the half damage of 0x5d, Alt-A, the computer's spell choice, the clouds:
laid, coughing and choking, Cloud Kill's deaths, aged, dispelled, a body
under one);
`tests/test_effect.c` its caster level and saving throw (and Champions'
moon); `tests/test_combat.c` its open
ground's builder on the first fight's seed against the map the original
builds, and a river's shape.

### The character record's layout

A character stays a record of raw bytes (`cok_character.record`), as the
files and the original hold it: the ECL reads its fields by offset
(0x7c00-0x7fff) and the saved files must be byte-exact in each game's own
layout. Code reaches a field by name through the game's layout,
`rules->character` (a `cok_character_layout`, `src/field.h`), which gives
each field of the enum `cok_field` its offset, width (1, 2 or 4 bytes,
little-endian), element count and first index for an array (as the
original's code counts them, a Pascal array's lower bound: the thief
skills 1-8, the class levels 0-7, the coins 0-6, known spells by spell id
1-108, the attacks left and their dice by attack 1-2, the icon's colours
1-6), the stride between elements (the scores, base and current,
interleave), rows for a table (spells a day: 4 spell classes by levels
1-5), whether the original reads it as signed (a record of the original,
for a later game's conversions: the accessors read any byte either way),
and whether it is a far pointer (meaningless in a file; 0 in the port). Champions' table, with
what each field is and where the original reads it, is
`champions_character` in `src/rules.c`; the layout's `size` is the
record's, at most `COK_CHARACTER_MAX`, the buffer every game's fits in,
and `file_size` the bytes of its file (`.SAV`, `.WHO`), which
`cok_character_read` and `write` read and write.

Death Knights' (`death_knights_character` in `src/rules_death_knights.c`,
each field with its evidence: where the import `46b2:10d7` moves
Champions' field to, and the offsets the routines matching Champions'
have where Champions' have the field's) is the record of its files, 216
bytes (`file_size` 0xd8), and after it, in the same buffer, the spells'
block that the record points to (`SPELLS`, a far pointer at +0xc2) and a
file of its own holds (`.WIZ`, `MON1WIZ`): `spells_offset` 0xd8 and
`spells_size` 0xdc, `size` 0x1b4, `COK_CHARACTER_MAX` (Champions' record
409 bytes). The block holds the memorized spells (74), the known ones (by
id 1-0x72) and the spells a day (4 spell classes, levels 1-8 in rows of
8), so that the accessors reach them as any field
(`cok_field_in_spells` says a field is the block's). A fighter or thief
has no block (the original's pointer nil): the port marks one held with
the pointer's first byte 1 (`cok_field_set_spells`,
`cok_field_has_spells`; 0 in files, the block's bytes 0 without one),
which the loaders set, the writer reads, the levels' recompute and the
ECL's fields test as the original tests the pointer (`69a8:0000`,
`3c1e:073a`). The original's arithmetic past a field (`size` bounds it)
reaches the block, where the original's reaches the heap after its
record. Death Knights has no fighter level, level a human changed class
at, levels shared out, NPC's shares or former levels (width 0); its own
fields are the levels, experience and maximum hit points before a drain
(`UNDRAINED_LEVEL` at +0x4d, `_EXPERIENCE` +0x66, `_MAX_HIT_POINTS` +0x6a:
`504d:14c7` keeps them, Restoration's `3999:045f` compares them), which
Champions has not. No two of its fields share a byte, and one byte, the
import's 0 at +0xc6, is none's.

The accessors are `static inline` over the table, each check folding to
a compare or two (a combat round calls them often: the stat routines
alone, recomputing a character a million times, take a quarter longer
than with literal offsets, a tenth of a microsecond a character; the runs
of `tests/test_eclplay.py` take no measurably longer). Each takes the
layout (named `L` by convention: `const cok_character_layout *L =
cok_game_rules(game)->character;` once at a routine's top), the record,
the field and, for an array, the index (any integer type, as a subscript;
`_at`), or for a table the row and index (`_at2`). They are typed as the
expressions they replace were, which `-Wconversion` needs:

| Was | Is |
| --- | --- |
| `c[0x197]` | `cok_field_u8(L, c, COK_FIELD_HIT_POINTS)` |
| `(int8_t)c[0xf9 + class]`, `(int8_t)c[0x18d]` | `cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class)`, `cok_field_s8(L, c, COK_FIELD_AC)`: every `(int8_t)` read is `s8`, whatever the field |
| `c[0x100] > c[0xce]` | `cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > ...`: every plain read is `u8`, whatever the field |
| `u16(c + 0x17d)`, `c[0x17d] \| c[0x17e] << 8` | `cok_field_u16(L, c, COK_FIELD_WEIGHT)` |
| `word(c + 0xeb + 2 * i)` | `cok_field_u16_at(L, c, COK_FIELD_COINS, i)` |
| the four bytes of +0x116 | `cok_field_u32(L, c, COK_FIELD_EXPERIENCE)` |
| `c[0x11b + k * 5 + level]` | `cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, k, level)` |
| `c[x] = v`, `put16`, ... | `cok_field_set8`, `set16`, `set32` (and `_at`, `set8_at2`) |
| `c[x] = (uint8_t)(c[x] + d)`, `++c[x]` | `cok_field_add8(L, c, f, d)` (and `_at`, `_at2`) |
| `&c[0xd0 + type]`, `c + 1`, `memset(c + 0x11c, 0, 0x14)` | `c + cok_field_offset_at(L, f, i)`, `cok_field_offset(L, f)`, `cok_field_size(L, f)` |
| `i <= 0x39` over the memorized spells | `i < cok_field_count(L, COK_FIELD_MEMORIZED)` |
| `for (i = 1; i <= 6; ++i)` over the coins but silver | from `cok_field_first(L, COK_FIELD_COINS)` (at least 1) to below first + `cok_field_count` |

Each asserts that the game has the field (a game without one gives it
width 0: `cok_field_present`), that the index is one of its elements, that
the access is as wide as the field, and that a single value's form is not
given an array. The assertions are compiled into the builds that play
(no `-DNDEBUG`): one that fails stops `cok` and `eclplay`. Where the
original computes an offset it does not bound (`cok_magic_slots`' class
and level past the table, reaching other fields or the far pointers),
`cok_field_offset_unchecked` does its arithmetic from the layout and
`cok_field_pointer_at` says whether a byte is a far pointer's; the record
is then indexed with the result, checked against the layout's `size`.

Every routine reads every character field through the layout: no file
but `src/rules.c` names a character offset (the item records, 63 bytes, the
effects, 9, and the combat record are not the character record and keep
their offsets). They were converted in four parts: `party.c`, `items.c`,
`magic.c` and `sheet.c`; combat's in `attack.c`, `round.c`, `ai.c`,
`cast.c`, `effect.c`, `combat.c`, `arena.c` and `cloud.c`; those of
creation, Modify, training, the camp, the shops and temple, the treasure,
the icon editor, the combat commands and the roster in `create.c`,
`modify.c`, `train.c`, `camp.c`, `shop.c`, `treasure.c`, `icon.c`,
`command.c` and `roster.c`; and the ECL's, the adventure's, the monsters'
and `eclplay`'s in `ecl.c`, `adventure.c`, `monster.c` and `eclplay.c`,
with the debug mode's names in `debug.c`. The ECL's VM has the rules
(`cok_ecl_init(vm, rules, hooks)`, asserting them, as its first character
field read does again for a VM made otherwise), and its table of the
fields at `0x7c00`-`0x7fff` gives each one's field and element where it gave
an offset (see [ECL scripts](scripts.md#ecl-scripts)).
Those that took only a record take the rules after it
(`cok_character_stats(character, rules, types, ...)`,
`cok_character_former_class(record, rules)`, `cok_character_damage`, `heal`,
`allowance`, `strength_bonus`, `dexterity_missile`, the cleric spells,
saving throws and thief skills, `cok_party_add(party, rules, character)`,
`cok_party_draw`, `cok_magic_sort`, `cok_magic_slots`;
`cok_combat_damage(combat, c, rules, ...)`, `cok_combat_opposite(c, rules)`,
`cok_combat_auto`, `cok_combat_place(combat, mode, character, rules, ...)`,
and after the party `cok_combat_count_sides` and `cok_combat_enemy_health`;
`cok_create_icon(record, rules)`, `cok_create_knight_items`,
`cok_modify_least` and `cok_modify_most(c, rules, selected, ...)`,
`cok_train_row`, `cok_train_constitution` and `cok_train_hit_die`,
`cok_camp_preparation(character, rules)` and `cok_camp_forget`,
`cok_shop_money` and `cok_pool_pay(c, rules, steel)`;
`cok_monster_movement(party, rules, ...)` and
`cok_debug_name(record, rules, out)`); callers pass
`cok_game_rules(game)` (a spell's handler `cok_game_rules(cx->game)`), the
effects engine its own `fx->rules` (`cok_effects_init` takes them),
`cok_combat_records` the rules it is given, and tests
`&cok_rules_champions`. Four indices stay the original's unbounded
arithmetic, through `cok_field_offset_unchecked` and each site's own bound
as before: a helpless target's blow at attack slot 0 (`ATTACKS_LEFT`'s
offset for slot 0 is `AC_BEHIND`'s), checked against the record's size; a
saving throw's signed type (`SAVING_THROW` from `+0xd0`), likewise;
`cok_combat_damage_roll`'s slot, which its guard bounds at 2 only, so that
slot 0 reads `DICE`, `DICE_SIDES` and `DAMAGE_BONUS` one byte early
(`+0x190`, `+0x192`, `+0x194`), as the original's `+0x190 + slot` does; and
the spell scribing makes known (`KNOWN_SPELL` from `+0x62`), a scroll's byte
& 0x7f, which for 109-127 writes `+0xcf`-`+0xe1` past the known spells, as
the original does, the record's 409 bytes bound enough. Modify's name editor
reaches the name as the string it is (`cok_field_offset` of `NAME`), indexed
by the cursor and the length byte as before, which a file may make more than
15; the roster's list takes a `.WHO` of the layout's `file_size` and tests
the jewelry's low byte (`COINS` 6's offset, `+0xf7`). The indices the ECL
gives (the thief skills at `0x7ca5`-`0x7cac`, read and in `CHECKPARTY`, and
the spells memorized written at `0x7c20`-`0x7c59`) go through the checked
accessors: each address range is the field's elements exactly, 1-8 and
0-0x39, so the range's own test bounds them. A monster record is the
layout's `size` long, and its far pointers are cleared by field.

To add a field: name it for what it is in `cok_field` (`src/field.h`),
with a comment; give Champions' place in `champions_character`
(`src/rules.c`) with the macro of its shape (`BYTE`, `SBYTE`, `WORD`,
`DWORD`, `BYTES(offset, count, first)`, `SBYTES`, `WORDS`, `INTERLEAVED`,
`POINTER`, `POINTERS`) and a comment saying what it is and where the
original reads it, and Death Knights' in `death_knights_character`
(`src/rules_death_knights.c`; width 0, left out, where it has none, and a
field only it has left out of Champions'); and add it to `expected` and
`dkk_expected` in `tests/test_layout.c`, which check every field's place
and that no two share a byte (a field that does, as known spell 108 and
the highest fighter level do in Champions', is listed there as
documented); `tests/test_death_knights.c` checks that a field the import
moves lies where the move puts it.

To convert a file: take each raw character offset (`c[0x...]`,
`record[0x...]`, `c + 0x...`, a `u16`/`word` of one, a loop bound that is
an array's count or first index), find or add its field, and replace the
expression by the accessor of the same type: a plain byte read by `u8`,
an `(int8_t)` one by `s8`; a routine that has a game declares `L` at its
top, one that only has a record gains the rules after it and its callers
pass theirs. An index may go through a checked accessor only when it is a
constant, a loop bounded by the field's elements, a choice already checked
against them, or the game's own tables; one that comes from data (a
byte of an item, a monster, a file or the ECL: camp.c's scroll spell,
`item byte & 0x7f`, up to 127 where known spells end at 108; attack.c's
attack slot 0, which reaches AC_BEHIND by the original's `+0x18e + slot`)
goes through `cok_field_offset_unchecked` and the site's existing bound
(the record's size, as there), never through a checked accessor that
would stop where the code before went on.
Count the offsets before and after (none left but those an unbounded
original index needs, as above); then the golden outputs and the DOSBox-X
checks' replay show nothing changed.

### The data segment's tables

The tables the port copied from the original's initialized data (its data
segment, `DS`) are the descriptor's: `rules->data`, a `cok_data_segment`
(`src/data.h`). Champions' bytes are in `src/data_champions.c`, which only
the descriptor (`src/rules.c`, `champions_data`) includes; shared code and
the tests read them through the descriptor, never by name, so that
Death Knights' tables, moved and reshaped, go beside them without touching
it. The descriptor holds two things:

- `tables`, every table the port holds, as a `cok_ds_table`: its DS
  offset, its length, the stride between its bytes (1, or 16 for a column
  of the spell table) and the bytes. Those with a stride of 1 come first,
  in order of their offsets (the lookup's search relies on it), then the
  columns. A table's copy may run on into the next one's (the steps into
  the terrain, the terrain into the footprints, the cone's sides into the
  battle's effects); where two hold the same byte they must agree.
- `table[COK_DATA_...]`, a `cok_data_table` for each table the code reads,
  named for what it is (`COK_DATA_SPELLS`, `COK_DATA_THAC0`,
  `COK_DATA_SAVING_THROWS`, ...): `base`, the DS offset the original's
  arithmetic adds each index times its stride to, as its code has it (the
  thief skills' `DS:3908` for a table that starts at `DS:3911`, its level
  and skill counted from 1); one to three indices, outermost first, each
  with its first, its count and its stride (the innermost's the element's
  width); and `from` and `size`, the table's own bytes, by default its
  elements', given where the port held more than the elements (the ways
  of moving, turning undead, the cone's sides: what a read the code
  bounds by its copy may reach). Width 0: the game has no such table.

A lookup takes the segment (`const cok_data_segment *D =
cok_game_rules(game)->data;`, or `rules->data`) and the table's name, and
asserts that the game has the table and that it is given the table's
number of indices:

| Was | Is |
| --- | --- |
| `cok_ds_byte((uint16_t)(0x31b3u + 16u * spell + column), &b)` | `cok_data_byte(D, (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, column), &b)` |
| `ds_byte(l, 0x4056 + class * 60 + level * 5 + type, ...)` (party.c) | `ds_byte(l, cok_data_offset3(l->D, COK_DATA_SAVING_THROWS, class, level, type), ...)` |
| `ds((uint16_t)(0x3903 + i))` (train.c, modify.c) | `ds(D, (uint16_t)cok_data_offset(D, COK_DATA_TOP_LEVELS, i))` |
| `cok_ds_string((uint16_t)(0x1330 + 13 * status), s)` | `cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_STATUS_NAMES, status), s)` |
| `at = 13 * kind + level`, bounded by the copy, then `turning[...]` | `at = cok_data_offset2(D, COK_DATA_TURNING, kind, level)`, `cok_data_holds(D, COK_DATA_TURNING, at)`, `cok_data_held(D, at)` |
| `formations[f][row][0]`, `quadrant_dir[q]` | `cok_data_s8_at3(D, COK_DATA_FORMATIONS, f, row, 0)`, `cok_data_u8_at(D, COK_DATA_QUADRANT_DIRECTIONS, q)` |
| `template_word(t)`: `word(template_table + 2 * t)` | `cok_data_u16_at(D, COK_DATA_TREASURE_TEMPLATES, t)` |
| `k < sizeof helpless`, `size >= COK_COMBAT_SIZES` | `k < cok_data_count(D, COK_DATA_HELPLESS, 0)`, `size >= cok_data_count(D, COK_DATA_FOOTPRINTS, 0)` |
| `cok_combat_terrain[v].cost` | `cok_combat_terrain(rules, v).cost` |

`cok_data_offset` (and `2`, `3`) is the original's arithmetic, unbounded,
as a `long`: the site converts and bounds it as it did before (a
`uint16_t`, as the original's 16-bit arithmetic wraps; or a check that it
lies in 0-0xffff, failing past the initialized data, as party.c's
`ds_byte`). `cok_data_byte` then reads the byte at that DS offset from
whichever of the game's tables holds it, so that an index past one table
reads the next as the original does (a level 13 cleric's THAC0 is the
next class's level 0; a spell id up to 0xff reads on past the spell
table), and returns false past the data the port holds; `cok_data_holds`
says whether an offset is one of a table's own bytes, for the reads the
port bounded by its copy of a table (the ways of moving, turning undead,
the names); `cok_data_held` reads a byte that must be held (asserted).
`cok_data_u8_at`, `s8`, `u16` (and `_at2`, `_at3`) are for the reads the
code kept within a table, as a C array's bound did: each index asserted to
be one of the table's, the width the element's. An index from data (a
spell id, a record's byte, the ECL's) goes through `cok_data_offset` and
the site's existing bound, never a checked accessor, as with the character
record's fields. `cok_data_string` reads a name, a Pascal string within
the name strings (`COK_DATA_NAMES`, `DS:0f5a`-`1dd3`), false where it would
start or end outside them or hold a NUL; `cok_data_first` and
`cok_data_count` give an index's range for a loop or a bound.

Routines that read a table and had neither a game nor the rules take the
rules: `cok_spell_class`, `level`, `effect(rules, id)`,
`cok_treasure_item(rules, seed, ...)`, `cok_train_experience(rules, row,
...)`; after the record `cok_train_constitution`, `cok_modify_least`,
`cok_modify_most`, `cok_camp_preparation`, `cok_combat_helpless(c,
rules)`; after the combat (whose map the footprints and the terrain lay
out) `cok_combat_occupy`, `probe`, `visible`, `sight`, `list`, `distance`,
`dungeon`, `wilderness`; `cok_combat_line_step(line, rules)` (the line's
directions); first `cok_combat_footprint(rules, ...)`; and
`cok_cast_step_byte(D, at)` the segment. `cok_combat_terrain` is a
function of the rules and a terrain value, which must be one the table
holds; the bounds that were `COK_COMBAT_TERRAINS` and `COK_COMBAT_SIZES`
are the tables' counts.

The steps by direction (`DS:1ed6`, `DS:1edf`), the compass, the same in
every game, are the code's own constants, `cok_step_x` and `cok_step_y`
(`src/step.h`), which test_data checks against `COK_DATA_STEP_X` and
`STEP_Y`. The descriptor lists those bytes (`DS:1ed6`-`1ee7`) only for the
reads that run on past direction 8 into the terrain table (a cone's, a
cloud's, the overland map's), which go through `cok_cast_step_byte`. A
footprint's cells are counted by the table (`COK_DATA_FOOTPRINTS`' second
index); the arrays that hold one footprint's cells are
`COK_COMBAT_FOOTPRINT_CELLS` long, which test_data checks the game's
count against (4 in both games). Not in the descriptor: the tables that
cite the data segment but the port never held as its copy of it: the
screen's frames
and colours, CGA's and the mouse's tables, the keypad's and the view's
keys, the 3D view's places and tile sets, the start menu's items, the
treasure's coin names and rates, the icon sizes' letters, the sound's
effects. A later part moves those a game changes. The spells' names and
the spell lists' headings are the `spells` section's (see The spells and
effects); the temple's diseases (`DS:01c6`) are a table here,
`COK_DATA_DISEASES`.

To add a table: its bytes in `src/data_champions.c` as the original
holds them, with a comment citing its DS range and what it is, declared in
`src/data_champions.h`; listed in `champions_tables` (`src/rules.c`) in
order of its offset; and, if the code reads it, named in `cok_data`
(`src/data.h`) with a comment, and placed in `champions_data` with the
macro of its shape (`LIST(base, width, first, count)`, `TABLE` for two
indices, `TABLE3` for three, `TABLE_IN` with its own bytes given) and a
comment citing the routines that read it. `tests/test_data.c` checks every
listed byte against `build/START_FULL.EXE` (`make test` builds it with
`make merged`), that overlapping tables agree, that every named table's
elements lie in its own bytes and are held; add the expression the code
had to `test_arithmetic`, over the whole range of its indices, the ones
past the table too. To add a lookup: replace the DS arithmetic by
`cok_data_offset` of the table's name (or a checked accessor for an index
the code keeps in range), keeping the site's own conversion and bound and
its messages, which print the DS offset as before.

A game's shapes differ (Death Knights' levels go to 18: THAC0 rows of 19,
saving throws 90 bytes a class, spells a day and experience rows of 204
with a spell level's 8 bytes apart, three spell lists of 8 levels,
turning undead's rows of 11, every base moved); its descriptor gives its
own bases, strides, counts and bytes, and the code that reads through the
names follows. A game's name strings may lie in more than one run: Death
Knights' item words lie apart from its other names, and
`cok_data_string` reads a name within either (`COK_DATA_NAMES`, or
`COK_DATA_ITEM_WORDS` where they are not within it).

### The spells and effects

What is keyed by spell id and effect id is the `spells` and `effects`
sections' (`src/rules.h`), so that a game brings its own spells and its
own handler of each id. Shared code never compares an id with a number:

- Ids the code names are named (`src/spells.h`: `cok_named_spell`,
  `cok_named_effect`, each with Champions' id and the routine it is
  from), and a game's `spells.ids` and `effects.ids` give each its id, 0
  for one it has not. `cok_spell_id(rules, COK_SPELL_KNOCK)` and
  `cok_effect_id(rules, COK_EFFECT_INVISIBLE)` (in the game's code
  `cok_game_spell(game, ...)`, `cok_game_effect(game, ...)`; in `cast.c`
  and `effect.c` the macros `SPELL(KNOCK)` and `EFFECT(INVISIBLE)`) assert
  that the game has it, as a field or a table a game has not; a test of
  an id goes through `cok_spell_is` and `cok_effect_is` (`IS_SPELL`,
  `IS_EFFECT`), false for a name the game has not, so that a handler
  shared by several ids (the poisons' `h_poison`, the breaths' dispatch)
  does not stop at a sibling the game lacks. The ids the messages cite
  stay Champions', with the handlers' addresses. A test that a character
  has a named effect goes through `cok_character_find_named` (`party.h`),
  NULL for a name the game has not. A value of a record's or an item's
  byte that both games test alike is not an id: it stays a constant, or a
  descriptor value where it is the game's (`spells.weapon_unscored`,
  0x53, the weapon score's test of an item's +0x3d, `3afb:14ca`).
- Each id's handler is a table entry: `spells.cast[id]` (`cok_rules_cast`)
  and `effects.handlers[id]` (`cok_rules_effect`) hold the port's handler,
  a `cok_cast_handler` or `cok_effect_handler` (one each of `cast.c`'s and
  `effect.c`'s handlers, by its function's name: `COK_CAST_PROTECTED` is
  `protected`, `COK_FX_HELD` is `h_held`), and the original routine's
  offset; `cast.c` and `effect.c` hold a function by kind and dispatch
  through the game's table, as the original through `DS:6e3a` and
  `DS:6b94`. An entry with `unported` set (the game's routine, "dkk
  5dc5:1daa") stops the run where the port would run it: casting stops
  with `COK_ECL_UNPORTED`, an effect's handler fails.
- What else differs by id is data: the durations not taken from the
  spell table, each a rule (`cok_duration_rule`) and its minutes; byte
  7's values (caster, one picked, the party); the ids each event runs,
  the ids the party shares, those magic resistance leaves pending;
  Display's ids and texts; the spell lists' sizes, the granted powers'
  ids, an item power's spell; the counts that bound the tables.
- A handler that both games share but for a constant takes it from its
  entry (`cok_rules_cast`'s `value`, `count`, `sides`, `type`: Death
  Knights' Enlarge, Lightning Bolt, and Flame Strike and Ice Storm by
  `COK_CAST_DICE`; its band B spells, see [Death Knights' casting](camp.md#death-knights-casting)), and
  its words where they are not Champions' from the entry's `text` (Death
  Knights' "is fire-resistant"; `COK_CAST_SAYING` and `COK_CAST_MISSILES`
  say only that); where the cast's own rules differ (its words, its
  targets, the damage's message, magic resistance, the cone's range,
  Dispel Magic's clouds), the casting section holds them (see [Death
  Knights' casting](camp.md#death-knights-casting)). Loops over spell levels and list entries are bounded
  by the tables' shapes (`COK_DATA_SPELL_LISTS`: Champions' 4 levels,
  Death Knights' 8), and each game's list sizes are at most
  `COK_SPELL_LIST_MOST`, `magic.c`'s arrays.

To add a spell or effect handler: its function in `cast.c` or `effect.c`,
its kind in `src/spells.h` and the function in the file's table by kind,
and the ids that run it in each game's table. To name an id: add it to
the enum with Champions' id and routine in a comment, give it in each
game's `ids` (0 where the game has not it), and read it through the
lookups. `tests/test_magic_rules.c` checks each game's names and headings
byte for byte, its handler tables against the stores of the routines
that fill them at startup (through the thunks), its event lists against
the dispatch's calls, the shared ids against the set the dispatch tests,
byte 7's values against the target routine's compares, the weapon
score's item byte, the learning list's levels and the moons against
their routines, in both executables; that ids of
one routine have one handler; and that every handler kind is one of
Champions'.

Death Knights' tables were made from its executable and Champions': its
handler tables (`DS:c150` by `5dc5:5622`, `DS:be7e` by `5dc5:5622` and
`504d:2948`) and each handler's code compared with Champions' instruction
by instruction (string arguments by their text, calls through the
routines they map to, thunks followed), an id taking Champions' handler
where the routine is the same but for data segment addresses and the
record's offsets (by the import's block moves, `46b2:10d7`), else
unported. Its named ids are Champions' where its handler of the id is
Champions' routine, or where a shared handler uses the id unchanged;
others are 0.
