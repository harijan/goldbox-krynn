# Games

Part of the [developer notes](../DEVELOPERS.md).

The port is to play *Death Knights of Krynn* as well (see
[DEATH-KNIGHTS.md](../DEATH-KNIGHTS.md)), and *The Dark Queen of Krynn* (see
[DARK-QUEEN.md](../DARK-QUEEN.md)). Everything that differs between the
games goes in a descriptor, one a game: `cok_rules` in `src/rules.h`, with
Champions' in `src/rules.c` (`cok_rules_champions`), Death Knights' in
`src/rules_death_knights.c` (`cok_rules_death_knights`: see Death Knights
below) and Dark Queen's in `src/rules_dark_queen.c` (`cok_rules_dark_queen`,
read headless so far: see The Dark Queen below). A front end picks it once,
before any file is read, from `--game` (`champions`, the default,
`death-knights` or `dark-queen`;
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
characters, and `krynn` and `eclplay --start-menu` play it from its title,
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

`krynn --game death-knights` and `eclplay --start-menu` play it from the
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
stops with the reason (`not yet ported: ...`), `krynn` showing it on the
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
`dkk_treasure.py`, `dkk_levels.py`, `dkk_items.py`, `dkk_shops.py`, `dkk_menus.py` and `dkk_trade.py` the original (see [Checks against the original](checks.md#checks-against-the-original));
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

### The Dark Queen

`cok_rules_dark_queen` (`src/rules_dark_queen.c`, its addresses `dqk
SEG:OFF` in `build/DQK_FULL.EXE`, `make dqk-merged`, its data segment at
`DS` 0x422e) is DARK-QUEEN.md's step 1 and its step 2's screens and
saved games: `eclplay --game dark-queen` and `ecldump --game dark-queen`
read its libraries, run its scripts with their text, menus, input and
pictures, its 3D view, its title, main menu, camp and demonstration, load
its saved games and play them in 3D up to the first fight, as the
original draws them, and `krynn --game dark-queen` plays the same in a
window (`make dqk-checks`' `dqk_views.py`, `dqk_boot.py`, `dqk_menus.py`
and `dqk_party.py`: see [Checks against the original](checks.md)). Its
`--load` and `--play` take a saved game as the start menu's Load and Begin
Adventuring do (`cok_drawing`'s `load` and `play`, `dark_queen.c`'s
`load_saved` and `begin_adventuring`), and its `unplayable` says why parties
(`--party`, `--test-party`) stay refused. What is not ported is left
out of the descriptor (the data segment, spells, combat), so that code
reaching it stops at an assertion, and its opcodes that need them stop the
run, citing their handlers (`unported`). It holds:

- its files: HLIB libraries, not DAX archives (`archives.format`
  `COK_ARCHIVE_HLIB`, `src/hlib.c`: `"HLIB"`, u32 size, u16 count, u16
  flags, the type, count + 1 offsets; flag 0x100 an id map in record 0;
  a record starting `"HLIB"` a library nested in it; nothing compressed),
  each one file for every ECL file and named whole (`ECL.GLB`, `GEO.GLB`,
  `TITLE.TLB`; `cok_adventure_record` reads its record by id through the
  map), looked for in the assets and then in `DISK1`-`DISK3`
  (`archives.folders`, as the installed game keeps them: it reads
  `disk1\dqk.cfg`, `DS:608e`); `ecldump` reads a game's `ECL.GLB` by its
  id map;
- its drawing (`drawing`, `COK_DRAWING_DARK_QUEEN`): routines of its own
  (`src/dark_queen.c`, `cok_drawing` in `src/adventure.h`), which the
  adventure hands the screen to: startup, the frames, the view, the
  opcodes that draw or read keys, a special address of the host's
  (`COK_ECL_SPECIAL_HOST`), its stores, and the title. Its screen is a VGA
  picture (`picture.h`'s `vga`: a byte a pixel, shown in 256 colours,
  `cok_picture_create_vga`), on which the 16-colour routines draw nothing,
  and BMPs of it are its colours as DOSBox-X shows them (`image.h`'s
  `vga`). See The Dark Queen's screen below;
- its devices' file, `DQK.CFG` (`files.config_bytes`, `config_folder`:
  `disk1\dqk.cfg`, `DS:608e`, read by `6478:0000`, its 0x90 bytes to
  `DS:acd6`; `cok_config_parse_bytes`): u16 at 0 the display (0 VGA, 1
  EGA, 2 Tandy: the game ends with "Installed for EGA only!" or "...TANDY
  16 color only!" for another than the adapter, `6478:00e2`, and the port
  draws VGA alone), u16 at 2 the sound in INSTALL's order (the speaker,
  the Tandy, the AdLib, the Sound Blaster, the MT-32, the MT-32 and the
  Sound Blaster's effects, none: the port's speaker, Tandy, AdLib, AdLib,
  Roland, Roland, none), at 10 the save floppy's letter, at 12 Death
  Knights' save path (`DS:ace2`, `other_path`), at 78 the game's (unread).
  The game never writes it (`INSTALL.EXE` does) and has no setup: without
  it, or short, it prints "Error reading configuration file!" and ends,
  as `eclplay` and `krynn` do (looking for it in `--config`, the saves,
  then the assets' `DISK1`). `--display` is refused (VGA alone);
  `--sound` as the others'. The arguments it reads (`SB220 SI7 RB330`,
  `6478:0170`, the sound's ports; a monitor's name, `65fa:08d1`; Woof and
  Gem) have no port counterpart;
- its ECL: the opcodes of its handlers' table (`dqk 5f07:0d34`, loaded by
  `5f07:0013`), Champions' meanings but `CHECKPARTY` (now loading six
  operands and doing nothing, `5f07:0812`), `LOAD FILES` with a map and a
  wall set (`COK_ECL_LOAD_MAP`, `6c70:1dec`), `0x37` finding a member by
  name (`COK_ECL_FIND_NPC`, `6c70:31e8`) where Champions has `LOAD PIECES`,
  `0x3c` treasure from the item lists (`COK_ECL_STOCK_TREASURE`,
  `6c70:335e`), and Death Knights' `PRESS KEY`, `JOURNAL`, `YES NO`,
  `CLEAR EXIT`, `TREASURE ITEMS`, `NOTHING`, `MUSIC` and `ASK MENU` at
  their numbers; a failed `IF` skipping as many operands as the handler
  loads, and a list opcode's whole list (`6c70:1385`: `skip_list`); 44,780
  instructions in its 47 blocks (`ecldump --game dark-queen --summary
  DQKAssets/DISK3/ECL.GLB`), every IF agreeing. Its variables are the
  bytes of the 1 KB at far `DS:b20f`, `0x0001`-`0x0400`, the selected
  character's fields `0x0401`-`0x0800` (see Dark Queen's characters
  below), the block from `0x8000` in a buffer of 0x2ee3 bytes
  (`64b2:0131`), its record with no header (`cok_rules_ecl`'s layout:
  `area_first`, `game_*`, `scratch_*`, `code_size`, `block_header`,
  `name_address`); its special addresses inside the variables, each store
  kept in the variable too (`special_stores_kept`, `6c70:0728`, `0817`):
  0x11 the facing in steps, 0x23 the facing, 0x2d the square's byte, 0x32
  the wall ahead, 0x34 and 0x35 the square, 0x3d the square's backdrop
  (`8a8b:0807`), 0x10 0 (no member to have effect 5); `COMPARE AND`
  finding a pair of strings never equal (`5f07:05be` compares the slots'
  addresses: `compare_and_unequal_strings`); a block's start clearing
  0x2b and 0x2c (`start_stores`) and its locals 0x97-0xc8; `DELAY` 600 ms;
  `CALL` 1 the view again, 2 a step, 3 the picture's next frame
  (`5f07:0aba`); a new game's block 4 (`5f07:10cd`); ECL CLOCK (`5f07:0a35`:
  the count is the first operand, a byte, the unit the second, a word; `1396:044d` and its carry
  `1396:0368`: 100 years come off a full year, every member ages a year at +0x52
  and the aging is asked for (DS:bce7); `1396:00f8` turns the moons on a day, and
  their lamp is drawn by the port's `moon()` as `1396:000e` draws it outside mode
  3; the minutes of `1396:019f` run the effects, and an effect ending stops the run
  (`148f:017d`). A count of 255 loops for ever in the original, and a unit past
  6 writes its own locals, so both stop. Not modelled: 019f's camp case (mode 2,
  Encamp, `encamp`, whose Rest and Fix stop; PROGRAM 9 runs it too), which runs only
  the members whose flags DS:a260 are set. Checked on test blocks with saved game A's party (eclplay's
  `--character` for its six `.QCH` files): a year with the age, a day and a second
  day (`dqk_ecl_misc.py`, the clock block, 2026-11-02). DUMP (`5f07:0c9b`:
  `177e:1e31` unlinks the selection and frees its icon, the one before it or the
  second is selected, variable 0x20 counts out; a selection not in the party is
  kept, and none selects the first) and FIND SPECIAL (`5f07:0cc5`, the handler
  itself: `177e:1734` walks the selection's effects; with none selected the
  original reads through 0000:0004, and the port stops) are checked by
  `dqk_ecl_misc.py`'s party block (14 ids for each member; DUMP of the middle
  member and then the first; 2026-11-02). PROGRAM (`5f07:0baf`) does 0, the camp from
  the adventure (`5f07:0c15`: `7368:046f` with 0, the camp's menu as main's, its
  Remove of the last member setting DS:b7a7 where main makes a new game; then, on Begin
  Adventuring, the screen as it was (`177e:1ad1`, by the mode: 0 the full window, 3 the
  view again, 4 the adventure's screen, the view, the party's list and the status; 1, 2, 6
  and 10 stop, their small pictures not ported) and the party's place on the overland
  (`7065:0011`) where picture 9 is not shown and variables 0x22 0 and 0x24 1, and the view
  again at the next CALL 1; DS:c2dc, cleared by a new game (`5f07:10d3`), is taken 0:
  nothing in the game sets it (only 5f07:10d3 clears it); with DS:b7a7 set the ECL run
  halts (`5f07:0fc4`), and the adventure's loop ends and frees the party (`5f07:1338-1370`,
  `177e:1e31`); a load asked (its Load, `7368:0758`: DS:acc4 by `5f07:0004`, DS:b7a7 by
  `7368:0761`) loads there (`604b:2e87`, the Load Saved Game below), then main takes the camp
  from main (`5ebd:0312-033a`); without one, main makes a new game and shows its menu again
  (`5ebd:039b`, `64b2:04ec`, not ported: the run stops there, "not yet"); 1, the ending;
  2, the quit
  (`2093:002d`, as Exit ends a game); and 4-255 (`5f07:0c8c`: DS:b7f8, the view
  and status again at the next CALL 1, the port's view_changed), with the
  selection LOAD CHARACTER changed restored and Move mode kept, as the original's
  prelude does. It stops at 3 (a level drain, `78e4:17dd`), with its cite; 9 is
  Encamp (below). The
  arithmetic (ADD, SUBTRACT,
  MULTIPLY, DIVIDE, AND, OR, SAVE, RANDOM), COMPARE with every IF (9 against 5, 0xffff
  against 1), GOTO, GOSUB (nested), RETURN (at depth 0 too), ON GOTO and ON GOSUB in
  range, at the count and out of range, the flags AND and OR leave, GETTABLE and SAVE
  TABLE agree with the original on test blocks, narrow and wide (`dqk_ecl.py`,
  2026-11-01); DIVIDE by zero does not run in the original at all: its INT 0 is
  unhandled and the game hangs (DOSBox-X's "Illegal Unhandled Interrupt Called 0"),
  where eclplay stops with "division by zero" at the same address, 832a; the input and menu opcodes
  agree on test blocks (`dqk_ecl_input.py`, 2026-11-02): INPUT NUMBER, INPUT
  STRING, COMPARE AND, VERTICAL MENU, HORIZONTAL MENU, ASK MENU and YES NO (their
  keys typed; LOAD CHARACTER not run, it needs a party on both sides). VERTICAL
  MENU's store takes the first item's type (`6c70:1e91` reads `DS:b216` in its
  item loop; the port took the last item's, fixed in 2026-11-02's part). The mouse
  branches of YES NO (`1bdb:0424`, 0452) and HORIZONTAL and ASK (`6c70:0e3f`) read
  `DS:c2f5`, which `1bdb:1206` sets when a menu's result is a code and not an item (a
  key below 0x20, an extended key, a view area's click; not a mouse hit on a control,
  nor a driver's flag as `queen.h` had it); the port tells codes apart by value; not run;
  its text, picture and wait opcodes (PRINT, PRINTCLEAR, PRINT RETURN, PRESS KEY,
  JOURNAL, DELAY, CLEAR BOX, CLEAR EXIT, PICTURE, and the view again after a small and
  after a big picture; `dqk_ecl_text.py`) agree with the original at every wait (the
  screen, but the party's list and the status line, and the variables) and after
  CLEAR EXIT (the status line too); the lamps under the view agree, drawn from the
  saved game's variables (2026-11-02). Fixed: JOURNAL's and ASK MENU's text are in
  colour 7 whatever variable 0x39 says (`6c70:32b2` and `6c70:200e` push 7; PRINT's
  3 is `6c70:1cd3`); the backdrop is chosen by the saved square's byte, DS:c9f4
  (`8a8b:0290` and `029d`), which a block's entry, a step and a saved game's load set
  and the view does not (`5e22:0000` recomputes nothing); the view is drawn in mode 4,
  or in mode 3 with variable 0x24 not 1 (`5e22:0032-0049`), and in no other mode.
  CALL 1 and CLEAR EXIT redraw through the shared `redraw_changed`, which recomputes
  the saved square as the original does (6c70:1685 writes DS:c9f4 from DS:c9f0-1 at
  1694, and CLEAR EXIT calls 1685; `dqk_ecl_flow.py`'s redraw block, 2026-11-02); the
  view itself (`5e22:0000`) recomputes nothing.
  FIND ITEM (`6c70:2f3b`), DESTROY ITEMS (`6c70:30ec`) and ROB (`6c70:28f8`) are carried out
  on Dark Queen's item record (the type at +0x28, readied at +0x32, the bonus at +0x30,
  the weight at +0x2c): DESTROY ITEMS' 0x80 takes the items with a bonus only, the stats
  are recomputed for the members that lost one (`31af`), and a readied item goes off by
  the readied branch of `86b8:1965` (`queen_item_unready`; a power's `86b8:16f7` stops the
  run); ROB's money is integers, each coin times the percent kept over 100 (`124f`),
  not Reals. TREASURE ITEMS' items (`6c70:218f`) are ported (part 21a, below); STOCK TREASURE
  (`6c70:335e`) still stops (part 40; `1d99:1189` for STOCK).
  Checked by `dqk_ecl_items.py` (FIND and DESTROY, with ROLANDOF's base AC before and after;
  ROB's money, 200 with 39 taken coming to 122, not Reals' 121, and its three d100 draws
  and the RANDOM after them; on test blocks with saved game A's party; 2026-11-02). The cursed
  path (an item with +0x34 set: "It's Cursed!", still removed) has no DOSBox coverage, only
  the repo test.
  Not done: the overland's big picture is drawn without the picture flag (DS:ae27) that
  `5e22:0000` tests. PICTURE 0xff's sprite condition (DS:b419) and its clears (DS:c6f6,
  c6f7) are ported with the encounter (`5f07:0511-054b`; `test_monster.c`'s
  `test_dark_queen_picture_flags`); its drawing is the encounter flow's.
  The block, map and call opcodes agree with the original on test blocks
  (`dqk_ecl_flow.py`, 2026-11-02): CLEARMONSTERS, flow only (no monster is loaded: 0x0c is
  unported, so `75cc:151b`'s monster variables are not compared); NOTHING
  with six operands (CHECKPARTY's, `5f07:0812`) and with none (`5f07:1449`); CALL 1, the
  view again, which recomputes the saved square byte (`6c70:1694`) where a store to 0x34
  or 0x35 does not; CALL 2, two steps (the square, the facing, the wall ahead); CALL 0,
  4 and a word 0x0101 do nothing. A CALL's value is its first operand's: a word is its
  own, but an immediate takes the high byte last set by an operand of type 1-3 at that
  index (`5f07:0ac8`), which at a block's start is 0x80 (the vectors' words), so the
  first `CALL imm(n)` does nothing, and after a word with high byte 3 `CALL imm(1)` is
  0x0301. CALL 3, the picture's next frame: PICTURE 14 (PICA's record 14, the coral base
  and its creature overlays, `63ae:000c`) and eight CALL 3s agree on the screen at a
  text's wait (a PRESS KEY would animate the frame in the original, and the block's EXIT
  redraws the view there); eclplay reports no frame, so the frame byte (DS:b20e) is
  compared through the picture, not as a value. LOAD FILES with wall sets 0 and 0xff
  agree on the screen; the wall set (DS:ca03, `6c70:1e57`, saved as the header's +9) is
  kept by LOAD FILES and checked by `test_adventure`'s `test_dark_queen_wall_set`; a
  different set frees the big picture and sets DS:ae43 to 0 (`6c70:1e41-1e4d`), which
  the port's `big_id` takes as 0. MUSIC (the variables; no sound compared). NEWECL 2
  from block 1 into a test block: two waits, the variables agree, and the run rests at
  0x8015 with opcode 0 once block 2's EXIT has run (vectors 1 and 2 run it; the variable
  0x16 reads 1 in block 2's body and 2 after). Not run or not ported: the redraw at the
  end of LOAD FILES (`6c70:1e5a-1e88`: the adventure screen, the party, and `5e22:03f8`
  when the last mode is 3, the mode is not 3 and DS:beea is set); and NEWECL's entry in
  mode 3 with no LOAD FILES since the adventure was entered, where the port shows the
  view only with files loaded or from mode 4 to mode 4, and the original
  (`5f07:105e-107a`) shows it unless the last mode is 4 and the mode is not 4 (the
  mode-3 loop is not ported).
  The party opcodes (`dqk_ecl_party.py`, with the saved game A's six members on both
  sides): LOAD CHARACTER (0x0a past the end keeps the selection), WHO (`6c70:2fe0`: its
  prompt "who will attempt to disarm the trap?" is shown as "Who will try to disarm?",
  case-blind, `cok_rules_who_prompt`; its clear and its bottom bar (`cok_drawing`'s
  `pick_bar`: the prompt boxed, Select) are the Dark Queen's own; its keys are the
  shared pick's, with End and Home too (`menus.pick_end_home`, `76b8:3c12`)), ADD EP (`6c70:2719`, `gain_dark_queen`: the points times 100 divided among the
  class levels that are not 0, seven, unsigned; with none the software division leaves
  0xffffffff, a drop of one; then may-train by `cok_queen_may_train` (`177e:1fe5`); the
  words in colour 7 through the window routine; the party's list drawn again after a
  party's ADD EP, `6c70:2828`, and the cursor set to (1, 0x11) before a selected member
  that cannot act is passed over, `6c70:2755`; its pause is variable 0x12 as it is now
  (100 ms times, `1a62:0949` -> `1df9:052a`): as the game's wait it draws one random(1000)
  first (`cok_dark_queen_wait`, `pass`), then steps the colour cycles and waits by the delay hook)
  and FIND NPC (`6c70:31e8`: the first member whose name matches, case-blind, with bit 7
  of +0x9b, its position and 0x80 if it cannot act, else 0xff). ADD NPC (`6c70:307b`)
  stops: `604b:2886` allocates a 0x19e record, fills it from DS:b2a4 by `604b:257c`,
  links it (`604b:3491`) and writes `204c:00df`, which needs step 2's NPC records and is
  not ported. Found and fixed on the way: ADD EP's words went through the 16-colour wrap,
  which draws nothing on the VGA screen (`cok_drawing`'s `window_text` and `window_clear`
  now carry them).
- the combat ground (`queen_arena.c`, the arena hooks of `cok_drawing`, `dqk_fight.py`'s flow
  `ground`): the battlefield's 7 by 7 view of 24 by 24 tiles at 8, 8 (dqk 5837:06e6, the
  redraw), each cell the terrain table's tile (DS:6197, its fourth byte) as record tile + 1 of
  the set (5837:172c; a value of 0 or past 0x4c is drawn as 0x15, 172c:1784). The set is
  59c7:0e70's: DS:ca03 (the wall set, LOAD FILES' second operand) of DungCom1 where var 0x22 is
  not 0, else of WildCom1. combat.c's build calls `arena_tiles` in place of its 4-bit tiles,
  after 59c7:1dd7 -> 0e70, before the builders: the set's record 0 is its palette
  (204c:0007 -> 19a4:0b8c), whose colour cycles draw random(8) from the seed (261b:00b1), one
  for DUNGCOM 3 and 6 (`tests/test_queen_arena.c`). The clouds and the bodies (values 0x1b-0x1f
  and 0x46: 172c:17ac, 137a's lists DS:c2d6 and DS:c526, COMSPR's overlay) stop the drawing
  with their cite. The combatants' icons are drawn (part 11b: `cok_arena_icon` through the game's
  `arena_icon`, the blit of 204c:030f as `queen_arena.c`'s `cok_queen_arena_icon`: the table's
  entry, a facing above 3 mirrored (2796:000c, `cok_vga_mirror`), at 24 x + 8 and 24 y + 8 less the
  hotspot, clipped to the view), and so are the cursor's box (slot 0x1e, COMSPR 15, 5837:00f4) and a
  pose with effect 0xd5, which faces 2 (5837:09ab). Not yet: a body's skull (`cok_arena_kill`,
  025d, part 12), and the missiles and the text (parts 11-12), each a stop (`cok_arena_kill`,
  `say`), never a picture left out. The redraw's frame (20b2:02fa: the panel cleared to 0x17 at
  cells 23-38 by 1-21, FRAME 1, 2, 3 and 5, FRAME 4 within rows 186-199, FRAME 6) is `combat_screen`
  in `dark_queen.c`, the screen's hook for COMBAT. The side panel (177e:085f, `combat_panel`, the
  `arena_panel` hook behind `cok_arena_panel`) is drawn at each turn's start: the name (177e:1697's
  colours), "Hitpoints" and the hit points (light cyan below the most), "AC" and the armour class,
  the readied weapon's name (177e:03fc, `queen_item_name`; 177e:0311 wraps it over rows 7-9, at
  once: no delay), and the status name, "(Helpless)" or "(Casting)" in light cyan, two rows below
  the weapon's text's last row (row 7 without one; 177e:0981 reads the cursor row 0311 left). Its status names are the
  pointers at DS:3bde (Okay ... Gone), not the other games' list at DS:1330.
  Arena.c's one cell draw (`terrain`) goes to the game's `arena_cell` (5837:04d4, 00f4, 172c)
  and its show does not blit the 4-bit buffer. `eclplay --ground FILE --ground-set dungcom4`
  draws a map file's ground and saves `--screen`.
- its fights' loads (`dqk_fight.py`, flows `setup`, `load`, `clear` and `six`): LOAD MONSTER
  only registers the monster (`6c70:29a3`: the flag DS:bd4e, the slot's id, count and icon,
  the next icon slot, below 0x0e), and COMBAT's monsters branch appends the records
  (`5f07:081b`, then `6c70:2d82` through `29fc` per slot, from slot 8 to DS:ad6b, the
  difficulty's hit points, the undead count DS:c2ed (`2ad6`, once per undead slot) and the
  icons), then `6c70:170e` appends ten GORGONs (MONCHA 0x14 by var 0x3a, status 9, the NPC
  bits 0x32, the icon in the next slot, which then is taken) that placement drops
  (`59c7:1d34`); the list's caps are 0x28 and 0x32. Port: `monster.c`'s `register_monster`,
  `append_slot` and `append_extra` (`rules_dark_queen.c`'s `combat.monsters_at_combat`).
  CLEARMONSTERS (`5f07:07fe`, which calls `75cc:151b` and `63ee:0002`) zeroes DS:c2ed, DS:c6f1
  and DS:bd4e, sets the next icon slot DS:ad6b to 8 and frees the treasure pool (DS:b1db's
  coins, the list at DS:b1e7): the shared clear (`monster.c`'s `clear_monsters`, as
  Champions'). The slot tables (DS:c7c4, c7ca, c7be) are not cleared, and nothing reads them
  above the next slot. The icon loads are ported (`204c:00df` loads one CPIC record into a slot
  and into slot + 0x26: `29fc:2b0b`, `170e:17d3`; dark_queen.c's `icon_load`, the table
  `queen_icon`), so each slot holds its monster's icons. The party's icons (`604b:05d7`: CBODY
  +0xc4 into +0xc5; an NPC's CPIC +0xbd) load in `loaded()` and the import, and Add Character loads
  CBODY for any joiner (`queen_added_icons`, 7368:0af4, 0ba7, no NPC test). The slot is the join's
  (`cok_party_add`, 604b:3491). Drop and the other removals free the member's slot (`177e:1e9d`:
  `queen_remove_icons`, 204c:004d). Creation's editor (66e5) and Alt Icon (7070:1ce2) call 05d7
  too: not ported. A record missing stops, storing neither image (`19a4:004b`, untraced: Q1).
  `icon_slots` is 15: LOAD MONSTER takes 8 to 13 (it stops at 0x0e, `29ac`), and 170e's
  extras take 14. COMBAT starts the battle (`5045:059d`, below) and with no monsters, the
  branches of `5f07:081b` in their order: the vault (var 0x31, `771f:09a0`) stops, cited; the
  temple (var 0x14, `7825:0af6`) runs its menu, "Heal View Repair Exit" (`dark_queen.c`'s
  `cok_dark_queen_temple`): View (`86b8:0b0c`, the sheet, then the screen as it was) and Exit
  leave (Exit draws the party's list, `5e22:023b`), Up and Down pick a member; Heal (`7825:0615`,
  its list of eleven cures, `085f`'s table: Cure Blindness `0045`, Cure Disease `00b8`, Cure Light,
  Serious and Critical Wounds and Heal `0193`, Neutralize Poison `0394`, Raise Dead `02c0`, Remove
  Curse `0479`, Stone to Flesh `0524`, Restoration `059e`) runs, Stone to Flesh is ported (the
  question "is not stoned." and "cast cure anyway? " for a member not stoned, which changes nothing;
  a stoned one with a readied item of type 0x8c, `+0x37`, is told "spell dosn't work!"; else status
  0, active, 1 hit point; no money); every other cure stops, cited (their effects run `148f`, the
  spells `849a` and `7fd8`, not ported); Repair (`7825:08fa`, which ends the party's effects through
  `148f:017d` and casts `849a:21b5`) stops, cited; the mode after it is 3
  only on the overland (var 0x22 0 and 0x24 1, `5f07:0917`), else 4, the variable 0x14 cleared
  after the call (`5f07:0861-0871`); the treasure with no monsters (`75cc:135c`): the lost weapons'
  walk `1297` (empty with no battle; a list that is not empty stops, cited), var 0x2e cleared, the
  flags `= <> < >` cleared (`2093:0015` with DS:b2f5 and 4; `<=` and `>=` kept), DS:c7df cleared
  (`1380`), then 0593, the party's after combat (unless the demonstration runs: `1390` on, part 19d,
  cited): b7a7, c7de, af3f and b1da set from the records, and the 18 effects that end with the battle
  (DS:631e) removed from each (a record holding one stops, cited `148f:017d`, part 27); when a member
  stands, 01fb (the experience: the pool's steel x2, gems x250 and jewelry x2200, and the magic items'
  bonuses x400, a magic item's bonus a signed word, over var 0x20 less those out, DS:b1da, which 01fb
  decrements where var 0x20 is no more; the division is Borland's LDIV@ (`10d7:2a72`, signed, with no
  test for 0: a divisor of 0 gives 0xffffffff, or 1 for a negative dividend, and no error); the
  monsters past the party add their coins and experience, and their items go to the pool first, in
  their list order, unreadied, at `02cb` (part 21c; `000b`, a merge of counted items, stops the run
  cited: no shipped monster has two of a kind; part 19b) and 0427 (the award by class, as Champions'; half and a third
  divide by LDIV@ too) run; then its rest from `75cc:0736`: with no load asked (b7a7 clear) each record
  until one past the party walks its status (3 revived, 4 revived with hit points or var 0x1d, else
  kept; 5 revived with var 0x1d, else 4), a fled party's members not status 3 are removed (`177e:1e31`:
  icons, the count one less); with the load asked (b7a7 set) every record until one past the party is
  removed and var 0x20 is 0 (`818-86e`). Then 135c sets the mode 6 (`1390`) and calls 11b0: each enemy (a
  record past the party, or hostile word 1) sets DS:c7df, counts in var 0x15 when inactive, and is removed
  (`177e:1e31`, the count one less only for a record in the party); the other records lose their combat
  blocks; the selection ends on the first record. Then 135c (part 19d) sets flag 1 and runs `177e:0be2`
  over the records; a lost party (b7a7) prints its three words (slow while DS:ae65 is set: each character's
  delay draws `random(1000)`, `1a62:0311`; DS:ae65 is the text's state: set at the start and a new game,
  set around each PRINT, PRINTCLEAR and JOURNAL and cleared after it (`6c70:1d02-1d84`, `32fc-3323`),
  cleared by the battle's setup (`59c7:1de2`), set by 135c (`14eb`), and every text window reads it, so
  the lines after a PRINT draw none) and waits at the continue prompt (`1460`, `1bdb:0511`, answered by
  Return or the button); then COMBAT's return (`5f07:0987`) and the adventure's menu, which waits with the
  party empty and DS:b7a7 kept (`5f07:11f3`, cited: `adventure()` stops there, not ported); else fled frees the pool's items, otherwise flag 0 is set
  and flag 1 cleared (`1427`); the results (`091f`: "the party has fled." / "you have lost the fight." /
  "The party has won." with monsters, the share's "each character receives N experience points.", the
  treasure line and the bar's "Press button or enter to continue", whose wait ends on Return) run; with
  money or items the treasure menu runs (`75cc:0e93`: View, the sheet and the screen again; Exit's claim
  question, Yes or No; Escape is Exit; Up and Down pick a member (`5e22:067c`), End and Home do nothing (`75cc:103e`);
  Take (`75cc:0d01`: the coins' list, the amount, the "Take:" bar with items), Pool (`74b9:00f6`) and Share
  (`74b9:01ee`, the 32-bit overload test `74b9:0000`, n = 0 gives 0xffff each) are ported; Take's items
  (`75cc:0bbe`, part 21b: the pool's items as a list, Take and Exit; Take gives the row to the selected
  member (`771f:0295`: "Too many Bundles!" for a scroll bundle with the party's bundles, "OverLoaded" by
  the weight or the sixteenth item that does not join (`86b8:2fbf`, which joins with the stack flag),
  else a copy last in the member's items, joined with its kind (`86b8:1fda`, the count and +0x33) and
  the stats recomputed; a member with no items taking a counted item stops, `771f:03b4`; the vault's
  Take's items stops, `63ee:00db`, part 43) and Detect's cast (`849a:145b`, part 28) are cited stops); the pool is emptied (`63ee:0002`),
  vars 0x01, 0x2f, 0x1c, 0x1e, 0x1d and 0x1f cleared, the monsters cleared (`75cc:151b`, monster.c's
  clear), and COMBAT's ending runs. TREASURE ITEMS (`6c70:2127`) adds its three header words to the pool's coins (steel, gems,
  jewelry), and then (`6c70:218f`, part 21a) each list id's item goes first in the pool: an id whose
  table entry starts 0xff (id 0 too) adds none, and 0xff is the random scroll (`6c70:1a83`,
  `cok_dark_queen_random_scroll`: three mage spells, the scroll's White or Red Mage word from r = 1
  or 2, drawn from RandSeed with 1a4e's test on DS:253e). A pool item is put by `pool_put`, the game's
  insert (`cok_adventure_pool_insert`), which keeps +0x2a-+0x2d (Dark Queen's words 2-3 and its weight: its item has no link, `link` 0; Champions' and Death Knights' clear it). The use-up of a thrown weapon in combat goes the same way (`52db:1b56`).
  Take's items (`75cc:0bbe`) are ported (part 21b). `dqk_treasure.py` checks the state at
  the treasure's stops: flows `won`, `fled` and `lost` (saved game A's pool is empty, so `won` awards nothing),
  and `pool`, `bonus`, `zero` and `negative` (the pool's coins, a divisor of 0 and a negative share),
  and `rest`, `gentle`, `fledmix` and `lost9` (the rest's statuses with and without var 0x1d, a fled
  party's removals, the lost party's var 0x20 at 0), and `enemy` (11b0's removal of a hostile member);
  the repo tests cover the magic items, the records past the party and the missile stop. The
  temple's View,
  Heal's list and its question agree with the original in `dqk_shops.py` (the wounded member's
  hit points in the colour the original draws: byte 0x00 on the window's grey; Stone to Flesh on a
  stoned member, by keys); the other cures are checked to stop at their routines.
  Its open squares (`6c70:01c3`, lowering var 0x37 to its result in
  `5f07:088f`-`08ba`, between the overlay's loads and the text) run now, as the encounter's
  (below). Both need LOAD FILES
  first, as the shipped block 4 runs it before its dragons (8066). Without it, in the
  original, APPROACH waits in its disk prompt and COMBAT in "LBIBase: Invalid Library File";
  with it the battle's first turn is reached (`5045:078b`), with the seed's 11 draws, and
  the fight checks run with the sound off (DQK.CFG's sound word 6, read by `1fd3:000b`).
  The load flow's party list at the first turn (`5045:078b`, after the placement): 19 records, the monsters'
  bytes equal (pointers masked, whole), and the party's too from the save (`--load`; with `--character`
  four members' import differed: known spells, saving throws, thief skills, not traced). The icons
  load unrecoloured into the table `queen_icon` (part 11a; `dqk_fight.py`'s flow `slots`); the
  combatants are drawn over the view (part 11b; `dqk_fight.py`'s flow `icons`).
  The sound's own draws are not traced: the fight's are the same with the sound off in every
  run, and the sound's effect on them is step 5's question (see [checks](checks.md)).
- its adventure's camp (step 4, part 44; `dark_queen.c`'s `encamp`, `encamp_menu`,
  `program_encamp`; `dqk_camp.py`'s flow `encamp`): Encamp (the commands' key 4, `5f07:122c`,
  which calls `5f07:0b3b`) and PROGRAM 9 (`5f07:0c6d`, the ECL position kept across it). The camp
  vector (vectors[2], DS:b298) runs first, then the camp's menu (`7070:2d3c`): "View Magic Rest
  Alt Fix Load Save Exit" in mode 2, its screen the adventure's, the camping picture (0x80) at
  cell 3, 3, the party's list and the status (`177e:1ad1` in mode 2). The camp's start and end
  forget the scrolls' marks (`7070:0218`, `02f2`: an item of type 0x27 or 0x28 loses bit 7 of
  its spells at 0x36-0x38; a scroll's contents are none in the port). The menu takes the arrow
  and Alt keys as a filtered pick (`1bdb:1a1b`'s filter): Escape is Exit, any other filtered key
  stops at `5e22:067c` (a member picked by a key). Each item by its index: View, the sheet
  (`86b8:0b0c`, then the screen as it was: `86b8:0d97`); Save (`604b:3395`, then "Exit to DOS? "
  (`4776`), Yes quitting); Load asks "Game not saved. Load anyway? " where a member is selected
  and the game is not saved (`7070:2f07`): DS:acc4 (`5f07:0004`) and DS:b7a7 (`7070:2f2b`)
  are set and the run ends, the adventure's loop loads the game (`5f07:1338-137a`, the Load
  Saved Game below), then main takes the camp from main (`5ebd:0312-033a`). Exit puts the mode back, redraws the status and rows 18-22 and the
  bar, and the view is drawn (`5e22:0000`); then the party's mark is drawn where variable 0x22 is
  0 or 0x24 is 1 (`5f07:0b8f`, `7065:0011`), and the game is no longer saved (`5f07:0ba6`,
  DS:ae5b, read by Load's question and the start menu's camp). Rest and Fix are off where
  variable 0x2b is 1 and 0x2c 0x64 or more (`2dfd-2e09`). Magic (`7070:1671`), Rest
  (`7070:0aac`) and Fix (`7070:2ad0`) stop with their cites; PROGRAM 9 on the overland stops at
  `5f07:01ae`. Alt (`7070:1ce2`, "Order Drop Speed Icon Level Exit" in the bar, by its letters or
  index; Escape is Exit; Drop only with more than one member, variable 0x20): Order (`19a3`)
  Select or Place toggles the moving state ("has been selected" over the member, `177e:156e`),
  Down or End (`179b`, `18b6`: `cok_party_move`, the order wrapping) and Up or Home move it while
  moving, the list drawn after each, and Escape leaves; Down, End, Home and Up not moving stop at
  `5e22:067c` (the list's selection). Speed (`1bab`): "Game Speed (0=fastest 9=slowest) = N" on
  row 18, the bar's Faster (Down, above 0), Slower (Up, below 9) and Exit, the speed kept in
  variable 0x12 (the menu and the text's delay read it) and the game's speed. Level (`1b15`): "Novice Squire Veteran
  Adept Champion" sets variable 0x27 to 1-5, Escape keeps it. Drop (`7368:0ee9`): "Drop <name>
  forever? " and "Are you sure? " by Yes or No, No is "<name> breathes a sigh of relief.", else
  "<name> bids you farewell." (active, +0x18b) or "You dump <name> out back.", then the member
  removed (`177e:1e31`, as Remove Character does). Its removal also calls `177e:1c87`, which only
  gives the member's blocks back to their pools (memory; the port's `cok_party_remove` frees them):
  the saved game after a removal holds the original's heap past the item table (the save pads
  from `DS:c6de`, `604b:3352-3366`; the port writes 0 there), which `dqk_camp.py` masks.
  Icon (`2d99:13ff`, the combat icon editor) stops for a member; an NPC has none. Escape and
  the rest of the bar's filtered keys do what the original's do: the others nothing, Down, End, Home and Up stop as above. The camp's entry zeroes the rest time (DS:b1cc, 14 bytes) and
  DS:bce1 (`2d4e-2d5c`); the port does not model them: its rest time (`game->rest`) is cited at
  DS:7120, not b1cc, so that cite waits for Rest's part.
  Checked by `dqk_camp.py` (2026-11-02): the menu, View's sheet and the menu again, the adventure
  after Exit, with the variables 0x0001-0x0400 at each; Save C with No: the saved game byte for
  byte with the far pointers masked and the slots and effects' pointer by nil-ness (the original
  writes its heap's addresses there, as `dqk_create.py` masks them); Load's question after Save
  and Exit. The camping picture animates while the camp's menu waits (DS:c2ef above 0), so it is
  masked on the screens that show it. Alt (`dqk_camp.py`'s flow `alt`, 2026-11-03): after Load's
  question's No, Speed (Slower, Exit), Level (Champion), Order (Select, then Down, Place,
  Exit), Drop (Yes, Yes) and Exit: each step's screen and the variables agree, and so does the
  saved game D after the Drop (the pad past the item table masked, as above).
- its encounter (step 4, part 6; `dqk_fight.py`'s flow `encounter`; `monster.c` and
  `dark_queen.c`'s `encounter`, the drawing hook `cok_drawing.encounter`). SETUP MONSTERS
  (`6c70:2db1`) takes four operands: the sprite (op 1, a SPRIT.TLB record, 82 in block 7:
  stored at DS:bee8, the low byte of effect 0's far pointer, `6c70:2dc3`; the port keeps it
  in `game->sprite_id` and leaves effect 0 as COK_FX_NOTHING: which handler id 0 calls once
  the stores have run is the effects part's question, not this one's), the distance wanted
  (op 2, var 0x38), the close-up's id (op 3, DS:b1ca) and the ambush (op 4, DS:c2e7). Then
  `6c70:01c3`, the open squares ahead (at most 2, the wall test `1d99:10f3` per step): var 0x37
  becomes the lower of the squares and var 0x38; with var 0x22 0 the squares are 2 and var
  0x38 is set to 2 (`01d9`-`01e4`); a wanted 16 (`2e1c`) makes both 2. Then `6c70:0255`, the
  sprite: with no close-up shown (DS:c6f6 byte 1) and sprite not 0xff, the overhead map off
  and, in a 3D area, the sprite's record loaded (`sprite_loaded`, DS:c6f6 byte 0; `sprite_shown`,
  DS:b419) or else the view redrawn (`5e22:0000`, DS:ae27); in mode 4 the sprite's group
  distance + 1 drawn over the view (`53ae:02e5`). The close-up (`picture_shown`, DS:ae64;
  `closeup_shown`) comes at distance 0, or where the sprite is 0xff, in mode 4 (`53ae:0078`,
  mode 0, and `53ae:000c`). The sprite is SPRIT.TLB's HLIB record of the id: record 0 its
  palette (0xb0-0xff), records 1-3 its frames (`dark_queen.c`'s `encounter`). APPROACH
  (`6c70:2e4f`) steps var 0x37 one nearer unless it is 0 and draws the sprite there; its
  `incw DS:b7f9` is the ECL's read position (the VM moves past the opcode itself). 19a4:061a
  stores its argument at DS:1cb6, the pointer's hide and show: the front end's cursor, not
  modelled. Checked by `dqk_fight.py`'s flow `encounter` (2026-11-02): at each PRESS KEY (SETUP's
  `setup`, the APPROACHes' `near` and `nearer`, then `wall` and `raised`) the 1 KB of variables
  and the screen in RGB agree. var 0x37 is 2, 1, 0 at the approaches; at `wall`, a second SETUP
  with the wanted 3, it is the open squares (0: saved game A's square, (7, 13) facing north, is a
  wall at once); ADD makes it 9 at `raised`, and COMBAT
  lowers it to the open squares before the battle (`5f07:088f-08ba`, checked at the battle's
  call). The close-up at distance 0 is PICA 21, one frame (`dqk_fight.py`'s ENC_CLOSEUP), and
  agrees with the original's screen. PICTURE 14 is not used: it is animated (`63ae:0269`-`0287`,
  the loop from frame 5, stepped during PRESS KEY), so a settled screen shows a later frame than
  eclplay's first; that difference is the animation, not the close-up's drawing. PICTURE 0xff
  and the second SETUP draw the second sprite as the original does. The battle's start is the
  load flow's. Not checked against the original: 1 or 2 open squares (the count is the same
  routine; the repo test has a synthetic flat map for them).
- its battlefield (step 4, part 7; `dqk_fight.py`'s flow `field`; `combat.c`'s `build`,
  `cok_combat_dungeon` and `cok_combat_setup`, with the descriptor's `dungeon_*`, `door_corner_*`,
  `wilderness_unported`, `placement`, `menu_unported`, `computer_unported`, `spell_unported`, `pending_unported`, `demo_key_unported`, `round_clouds_unported`, `square_unported`, `battle_end_unported`, `aging_unported`, `ends_in_round` and `sides_ambush_0x20` fields). The battle (`5045:059d`) starts with its
  setup (`59c7:1dd7`, mode 5, the setup's clears), then the tiles and 3D map (`59c7:0e70`, part 4's
  set), then the dungeon builder (`59c7:06a7`) for a battle in 3D (var 0x22 set). The builder fills
  the 50 by 25 map with floor (0x16, every cell built), then for each of its 13 by 5 squares the
  four edges (`59c7:00ef`, `side_kind` on the map's own size, `59c7:0072`), the west edge
  (`014e`), the north edge (`01fb`), the north-west corner (`0285`: no door as a corner) and the
  north-east (`049e`: a door's corner, tile 7 where the east is open). Dark Queen's loop draws no
  tables and chairs (`dungeon_no_tables`: no dice, so RandSeed is the seed the battle began
  with). Checked by `dqk_fight.py`'s flow `field`: at the first turn (`5045:078b`, trapped, after the arena's redraw `59c7:1e99` and the placement `59c7:1864`), the
  original's 50 by 25 cells, the header (+6, +7, +8) and RandSeed agree with eclplay's (`--field`)
  for the fixture's square and for the west edge (0, 3, 2), (12, 12) and (4, 14). The original's
  RandSeed also advances by its memory cache's purges: 27fa:05f8 calls 27fa:0f14 while the free
  memory is short, and 0f14 adds one to DS:d9ba (argument 0) or DS:da8c and draws random(3) from
  the seed (`2102:0234` at `0f2c`) to pick the block to evict. The count varies from run to run
  with the heap's state (0 to 11 at the placement here), and the port does not model the heap. The
  checks freeze that draw in the box only (`dqk_fight_lib.freeze_purges`: AX = 0, so the victim is
  always DS:d6ba's order), so the original's RandSeed at the placement is the seed the battle began
  with, as eclplay's is; the purge counters are still read and recorded (`PURGE_COUNTERS`). The
  memory settings do not stop the purges (docs/checks.md).
  The battle's leadership (`52db:4dd2`, before `1dd7`, no announcement: the descriptor's
  `leadership_*` fields) is ported; Dark Queen's var 0x36 is FLED (`1dd7` clears it, `52db:2aef`
  sets it); and its setup has no prelude (`no_setup_prelude`: `5045:059d` and `1dd7` free no
  picture, clear no row, wait and print nothing, so "A battle begins..." is printed once, by COMBAT).
  The combat records (`59c7:0f7b`) are ported (`cok_combat_records`, the descriptor's `combat_block`):
  each record's 0x18-byte combat block at +0x40 is made with spell (+0x00, 0 here: 177e:09ef shows
  "(Casting)" above 0), facing (+0x0a: the quadrant's direction,
  +4 for a hostile one, 2 with effect 0xd5; read by 5045:10b4 and 52db:0b8c), not_party (+0x14: past var 0x20), in_battle (+0x17, 1 for
  every record; its readers not found yet), the morale's NPC flag (var 0x20 + 0x80), and effect 0x95
  for a record that is not hostile while var 0x3c is set (`148f:19c2`; the fixture's var 0x3c is 0, so
  no record gets it there). The other block bytes are unnamed and 0. The view's origin (DS:bedb +2, +4)
  is written after the placement and compared at the stop. The open ground (`59c7:0e3b`: the floor 0x45,
  the river (`0860`, var 0x33), trees (`09e7`) and scatter (`0b79`, its cell `0ce3` with a 0x2c band one
  roll wider and no 0x2c + d5) as Death Knights' builder does; the flags are `DS:5d7b` at var 0x1a2, the
  Abyss's encounter state (only blocks 64 and 66 store it: 0, 1, 2, 4), 32 entries) is ported
  (`COK_WILDERNESS_DARK_QUEEN`; `dqk_fight`'s flow `wild`). The placement's helpers (`1117` the
  both-out test, `113a` the cell try, `128e` the find, `16b3` the layout) are combat.c's shared
  ones, checked against the original: the layout's distance is the descriptor's `ambush_distance`
  (1, as Death Knights'); a record laid out again is placed on its own side (`layout_sets_side`
  off: 16b3's loop is a local), where Death Knights' 18c2 leaves it the enemy's (on). The
  The placement (`59c7:1864`, `cok_combat_place_dark_queen`: the jump table at `1abf` for the ambush's 0x20 with hostile records, a layout per record for the ambush directions, the search `128e` for the rest) is ported; a placed body (`1c0d`, `5837:137a`) and an unlinked not-party record (`1d57`, `177e:1e31`) stop, cited. The 0x20 count quirk (`51c2:0920`'s `096a-0978`: side 1 one less while DS:c2e7 is 0x20, on every side count, `sides_ambush_0x20`) is in `cok_combat_count_sides`. The arena's redraw (`51c2:0c94`) runs: the view's origin and the screen positions are the shared scroll's, the combatants' icons are not drawn yet (part 11: the descriptor's `arena_icon` hook draws nothing), and then 1dd7's tail (`59c7:1e99`, below). The round's start and the first turn's stop follow it (below).

The setup's tail (`59c7:1e99`, 1dd7's tail, `setup_*` in rules.h): each record gets effect 8 only (`1ea5-1ed3`, no 0x16) and its block's turning byte (+0x12) is cleared, not turns (+0x11); Move mode is off (DS:af3d) and the cursors (DS:71a5, 71a6) are kept; the enemies' health (`52db:29f1`) leaves out a record of status 9. The tail also clears combatant 0's x (DS:c3f1) and DS:b2ea. The port does not model either yet: nothing before the first turn reads them, and a part that writes combatant 0 or casts (10b on) must clear them here.

The round's start (`5045:059d`, after 1dd7 returns; `cok_combat_round_start` and `rounds()` in round.c): an event 0x18 for each member, then the sides (`51c2:0920`, the 0x20 quirk), then `52db:0004` for each combatant in list order, then the turn list (`5045:0447`, `cok_combat_turn_order`), and the first turn (`5045:078b`) runs its arena turn (`5837:12f5`), which draws the cursor's box (`5837:00f4`); the side panel (`177e:085f`, `combat_panel`) is drawn after it (`5045:0846`), then the dispatch: the first combatant (a member: CINDER in the repo test's walk, TIMASALIN or BALTHIR NOMOON in
`dqk_fight`) stops at the command menu (`5045:08cf`, cited: part 22), before the dispatch's ported part. The dispatch after it is ported (`cok_combat_act`, checked before `--combat`'s stubs): a combatant that can act (+0x18b) reads the command menu (`queen_commands.c`, `5045:08cf`, `menu_first`: the items of `0cd7` as the record allows (Aim, Use, Cast, Turn, Guard, Quick, Delay, Bandage, Yell, View, Speed, End; the built text is logged as "menu") shown by `queen_combat_menu` in two rows (`1bdb:1595`, Quick's '@' the break); End (`51c2:0c11`), Guard (`51c2:0c52`, "Guarding" in the bar unless forced) and Delay (`5045:0a60`, `cok_combat_delay`) are ported. Quick (`5045:0a2c`: the party's Auto (`5045:1791`), the bottom bar, the keys flushed (`cok_adventure_flush`; `2093:0136`'s mouse read `22b7:235f` not checked), 200 ms with one draw, then the computer's turn for c, `cok_combat_computer` (`5bb8:0000`, part 18a: run at once)) and Alt-A and Alt-Q (`5045:0c0f`: the turn index less one, initiative 0x14, every record of the combat list (`DS:c6e5`, `5045:0c20-0c4a`: the monsters too) on Auto, the bar, 200 ms, the turn ends) are ported; Alt-M (`5045:0bd7`) toggles the magic and says "magic on" or "magic off" in the bar, then the arena and the menu again (`5837:12f5` is the arena's turn); Speed (`5045:166c`, `queen_speed`) is "game speed: (N) " over Faster, Slower and Exit, Faster and Slower moving var 0x12 by one, Exit or Escape leaving it, then the arena and the menu again. Aim (`08cf`'s 0x97c: `52db:37dd`, `queen_aim.c`, part 24a) lists the targets (`52db:36c6`, `cok_combat_list` from her cell: herself first, then the enemies within sight that she can attack, the one she kept (+0x0b) shown first); Next and Prev step through them (`52db:3465`: on a step the party's side is passed while an enemy is listed, the cursor flies to the new one at `51c2:00df`), Center centres (`5837:0846`), Exit and Escape leave (the arena and the menu again). The menu (`52db:2b60`) is "Next Prev Manual Target Center Exit" with Target only where the weapon's range reaches the one shown and it is not herself (a missile weapon needs ammunition, a thrown one or none beside her); the distance is written on row 23 by `1a62:0046`, whose battle branch (`1a62:01d0`) draws the text one cell right of x at pixel row 178 over a grey line on row 177, and `1bdb:03a4` clears the line at the end. Target when attacking is the strike (`52db:2d46`, `queen_aim.c`'s `strike`, part 24b): the ally question (`queen_attack_ally`: Aim lists no one of her side, so it asks nothing here; Manual's pick can reach it), then the view centred on its corner plus 3 (`5837:0846`, margin 3), the book and the attack (a missile weapon's ammunition as the item, none when it is thrown at one square), the turn ended unless the attack leaves done clear (the target down with attacks left, `52db:18d6`); not done (that, or the question refused) the cursor off, the cell centred (margin 0) and the command menu again. Manual (`52db:2e9c`, `queen_aim.c`'s `manual`, part 25; keys only) walks the cursor from her target's cell: each pass centres the view on it at margin 3 and steps it by the direction key given (the first pass has none; Aim's direction key is Manual's first key, so it steps on the second pass), Center centres at margin 0, Exit and Escape redraw the cell and leave (Aim again), Target picks it (`res` is the occupant's record, or a body's on terrain 0x1e or 0x1f, and the cell). The menu is "Target Center Exit", or "Center Exit" where the cell may not be picked: a foe beyond the weapon's range, herself, a body on 0x1f with no occupant, an ally unless she may attack it (`1030` with allies), and a missile needs its ammunition, or, at one square, a thrown one. Target from Manual is the strike as Aim's, so a party member asks "attack ally? " (No: not done, the cell centred at margin 0, the command menu again; Yes: the attack, the turn ended). With the mouse on, Manual stops before its first menu: its map areas (`1bdb:1862`'s five, handled by `40a7`/`40d6`, the click moving the cell) are the cited stop "dqk 52db:40d6" (a later part). A spell's Target (attack 0, part 28) gives back the record and cell. Bandage (`5045:0adb`: `51c2:0d63(1)`, then End) bandages a dying member who is not hostile (status word 4, dying 0) and says "is bandaged" in the battle's window: the say is `177e:156e` in mode 5 (the drawing's `arena_say`: the window cleared from row 10, the name on it, the text under it in grey, then with wait the speed's time and the window cleared again, one draw; logged as the say's are). With `--combat auto` (part 18a) the first party turn's pressed key is Alt-Q (scan code 0x10, `round.c`'s menu_first): the menu is drawn, then the key is taken (`1bdb:0d26` draws the menu before `0e37` reads), the Alt-Q result at once, every record on Auto and the turn ends; `--combat gods` presses Alt-X (0x2d) the same way. Alt-X (`5045:0c5d`, `52db:3feb`) answers only to the Helm: without it "That doesn't work" on the bar (`177e:153c`) and the menu again; with eclplay's and krynn's `--helm` (the original's third argument "Gem", the Helm's password) it is the gods (`cok_combat_gods`: "The Gods intervene!" on the bar, every hostile record (word +0x66 1) off the map with status word 6, every record's turn ended), then the arena's turn. Yell (`5045:0ae9`, `148f:0005`, `cok_combat_yell`) is a kender's (+0x58 5, +0x59 0) while no one has yelled this battle: the damage type is cleared (`148f:0020`, DS:bebd, before the saves), then each hostile record (word +0x66 1, not the yeller's side) with status word 0, intelligence above 3 and not helpless makes a save of kind 4 (`1886`, one roll); a save that fails takes effect 0x8c (YELLED_AT, 0 minutes, value 0xff, `252c` = cok_cast_add_effect) and its flash (`51c2:0731`: the drawing's `arena_flash`, part 28b). A save made says nothing. Then End (`0b29`). Effect 0x8c's handler does nothing (`78e4:24c3`); `attack.c` and `round.c` read YELLED_AT for the computer's targets and the casting's picks (parts 15 and 28 check them, not here); View's Lay tests 0x8c too (`86b8:314e`, a stop). The effect text said with no flash in the battle is "is Unaffected" (`effects.unaffected`, DS:01b5), through the battle's say. View (`5045:0af8`, `queen_view_combat`: `86b8:0b0c` with its done, then the mode 5 tail) is the sheet and its menu as the camp's: Items, Drop and Exit (no Trade in combat; Drop with coins), Spells a stop (`849a:2080`), Use a stop (`86b8:0f41`), Escape or Exit leaves it. Items sets done where a readied item of 0x8c is held (`86b8:16bc`, after any Items choice, Exit too): the turn is over, the sheet is not drawn again and the menu is not built again. Then the weapon attacks (`52db:0edf`, `cok_combat_weapon_attacks`, which also stores DS:b7f5 = 0, read only by effect 0xb2's handler `7e76:0dc6`, COK_FX_MISSING here, so nothing is lost), then, unless done, the arena again (`51c2:0c94`, nothing drawn) and the menu again, the turn kept. A former paladin's rule (`177e:21bd`) does not stop View in a battle (`86b8:314e`, `31af` give Lay and Cure 0 there). The tail's `5837:00e4` (the combat icons' palette made current again, DS:c5d4) is not ported: the check's screen after Escape agrees without it. The keys running out in the sheet end the run with the turn kept. Move (`5045:1099`, `queen_commands.c`'s `move`, part 23) is ported: a direction key or one of the mouse areas steps the combatant once at once, then the Move menu, "Move/Attack, Move Left = N" (`%2d` of the movement over 2) with Undo and Done (`queen_combat_menu` takes the prompt), is drawn; each further direction steps, Undo or Escape puts her back (`10bd`: the centre at margin 0, facing 2 while she holds effect 0xd5, else the turn's), and Done leaves, the movement under 2 then 0, and the menu comes again (`c8c`). A step: the pose and the probe. A combatant in the square is melee by the step (`15a3`: a missile weapon not thrown says "Not with that weapon", after "Can't use that underwater" when var 0x3c is set; else `52db:2aef`, `queen_attack_ally`: go where the target is on the other side (`51c2:08fd`: 1 where its word +0x66 is 0, else 0, equal to her word +0x66) or she is the computer's, else "attack ally? " asks, and Yes sets var 0x36 and counts the sides; DS:af3e, which nothing reads, is not kept; then the book and the attack, no sweep, Dark Queen's having none). A wall (terrain 0) asks "flee ? " (Yes flees and ends the turn; No or Escape keeps the turn); a cost above the movement says "can't go there" (one draw); else the opportunity, the step, the round's square (`148f:12ab`), and the end of the turn where she is inactive or helpless. The keys running out at a question or at the Move menu end the run with the turn kept. Use (`86b8:0f41`) is a cited stop; Cast (`52db:2543`, part 28c) is ported; Turn (`52db:1117`) is ported (part 26, below); with the mouse on, the menu's eight mouse areas round the combatant's cell (`1bdb:1595`'s `15de-1828`, `queen_combat_menu`'s p and q) give a click's code 0x81-0x88 (the arrow keys' codes: Move's), the cursor over each (`control_add_cursor`: 9, 11, 5, 7, 10, 4, 6, 8). The original adds them, and the type-7 control at `1810` (message 0x20, Space: `1570`), to a fresh control list only (`15de` and `1810` test DS:a6b5, the rebuilt flag `0d26` leaves), with or without a mouse; a list not rebuilt keeps them. So Space is taken either way: the port's filter takes it (`space`), and the control's only effect, the same 0x20, is not carried out (its `188f` draw is not traced). The port adds the areas on every call and gates them on the mouse, which the original does not, but no click comes without one; Escape and other keys show the menu again, and when the keys run out the run ends there with the turn kept. Its message waits (`pass`, `1df9:052a`) draw one random(1000) first, as the original's wait does; the waits that are not 1df9:052a (print_window's 1df9:009e, timed_wait) do not. A combatant with a spell pending is cast at `849a:145b` (cited), and a computer's turn stops at `5bb8:0000` (cited: part 15). A member that cannot act is ended at once (`08cf`'s `51c2:0c11`: initiative, spell, guard and movement zeroed, `cok_combat_end_turn`). After each turn, the tail (`5045:059d`'s `148f:2ab8`, `2c4f` and `2d9b`: DS:bebd 0 with the lists empty, `pending_unported` stops when one is not (its handlers 0x9a, 0xbc and 0xbd are part 39); the demonstration's key test `5045:06cf-070e`, `demo_key_unported`, stops in the demonstration; the colour cycles `261b:05be` step, the drawing's `turn_tail_cycles`), then the sides and the early end (`ends_in_round`, Dark Queen's: a side gone or the round limit, the first time in a battle). The round's end (`5045:0f6c`) runs Dark Queen's own steps through the drawing's hooks, not the shared `cok_combat_end_round` alone: the clock `1396:044d(1, 1)` (`round_clock`: dark_queen.c's pass_time, its timers included, whose effect-end stop is `148f:017d`, part 27b); the clouds `148f:2817` (`round_clouds_unported`, when one exists); the square test `148f:12ab` (`square_unported`: a cloud or terrain 0x46 stops, cited, part 39); "Your Teammate is Dying" (`177e:153c`, `teammate_dying`); and the Continue question `1bdb:0424` (`continue_battle`: Yes keeps the battle, not asked in the demonstration). The battle's end (`5045:0001`, the shared `cok_combat_end` with `battle_end_unported`): each record's effects 0xb0 and 0xd0 (`148f:017d`, a cited stop before its charm test); the charm test with CHARMED (0x0b, status 3 with the enemy's side standing, else 0); `1b66`'s battle-only effects (`DS:00a2`); the icons' slots 0x0f-0x16 emptied (`204c:004d`, the drawing's `battle_icons`; the slots 0x98-0x9c are not traced, and no slot past 0x25 is kept); then 75cc:135c (monster.c's `battle()` runs `cok_dark_queen_no_monsters_treasure`, the same branch as with no monsters). The aging report (`5045:012f`, `aging_unported`) is a cited stop in `rounds()` when `effects.aging` is set. The effect names (`effects.ids`): CHARMED 0x0b, BERSERK 0x4d, INVISIBLE 0x19, ENLARGED 0x0c, FRIENDLY 0x0e, STRENGTH 0x26 and YELLED_AT 0x8c, every other name 0 (none). `52db:0004` makes the combat record: may cast (a memorised spell, the scan of +0xce to +0x156, and not in a cloud), may use (not in a cloud: `may_use_needs_no_cloud`), the attack slot, the attacks (`52db:0edf`, Dark Queen's ammunition: none while var 0x3c is set, flags 0x0c as well as 0x0a for ammunition), the initiative (1d6 and dexterity, -6 on the side var 0x2e marks, 0 outside 0..20; `148f:1968`, `177e:104d`) and the movement (`177e:0a5f`'s, with the load cap `177e:038c`: party.c's `cok_character_queen_round_movement`, which the stats share). The turn list takes the initiative with the dice of the tie. A battle over before any round (a side gone at the start) goes to its end (`5045:0001`) as above.
- its maps: `GEO.GLB` records of their own size (`cok_view_set_geo`,
  `8a8b:12e3`): u8 height, u8 width, the wall sets of slots 0-2 and three
  backdrops, then four planes of a byte a square, row by row: the north
  and east walls, the south and west, the square's byte, the sides' ways
  through and the backdrop's choice; read through the view's
  `cok_view_wall`, `square`, `passage` (`8a8b:0628`, `075b`, `04a4`: off
  the map the far edge's square, its byte 0 unless the view wraps); a
  step (`CALL` 2) wraps at the map's own edges (`6c70:1186`); eclplay's
  `--at` stores the square at the game's own special addresses and its
  `--start` takes an address in the game's buffer.

The turn after a computer's dispatch (`5045:0891`, `COMPUTER`) and the round's end are ported (step 4, part 27a, `dqk-round-end`). The tail (`5045:059d`'s, `cok_combat_turn_tail`): the pending lists (`148f:2ab8`, `2c4f`, `2d9b`, with DS:bd4c, ad6e, ad6f) stop, cited, when one holds; `rolls.damage_type` is 0 (DS:bebd) with the lists empty; the demonstration's key test (`5045:06cf-070e`) stops; the colour cycles step (`261b:05be`). The round's end (`5045:0f6c`): the clock is `1396:044d(1, 1)` (var 6 a minute; the timers are part 27b's), the clouds stop (`148f:2817`) when one exists, each record's square is read by `148f:12ab` (its footprint's marks: cloud kinds 0x1b and 0x1c, and acid 0x46, stop, cited; 0x1d is skipped at the round's end), the dying count and bandage (`51c2:0d63`: status 5 and hostile 0 as words), "Your Teammate is Dying" and "continue battle?" (Yes keeps the battle). The battle's end (`5045:0001`): effects 0xb0 and 0xd0 stop, cited (`148f:017d`), the charm test (CHARMED 0x0b, status 0 as a word), `1b66` (BERSERK 0x4d, hostile 0; INVISIBLE 0x19 is removed by `148f:1b2a`), and the icon slots 0x0f-0x25 are emptied (`204c:004d`, k = 0..0x16 at `00e0-00f9`). The aging report (`5045:012f`) stops in `rounds()` when the battle aged someone, cited. Dark Queen's own end of combat is `75cc:135c`, called with no sound call around it (`monster.c`'s `battle()`). Its checks: `dqk_fight.py`'s flows `keys`, `turns`, `round` and `won` (`docs/checks.md`), and the repo tests in `test_dark_queen.c` (`test_round_end`, `test_battle_icons`).
Part 13a (`dqk-ai-text`): the computer's texts and waits go through the drawing's hooks. The row 24 line (`5bb8:06b0`: "Move/Attack, Move Left = N", with no trailing space from ten up) is boxed on the bar in white on grey (`1a62:0046`, `arena_text`); the bar is blanked at the turn's start (`1bdb:036c`, `clear_bar`); the battle's text is cleared (`177e:1665`, `arena_clear_text`); the Alt-M, Guard and blocked-flee notices are the bar message (`177e:153c`, `notice`); the attack's 100 ms (`1df9:052a`, `wait`) and the report's pauses (`1a62:0949`: variable 0x12 times 100 ms, `pause`) draw one random each where the original's wait does. Repo tests: `test_dark_queen.c` (`test_moves_left_text`, `test_battle_clear`, `test_battle_text`, `test_battle_bar`, `test_battle_waits`).
Part 13b (`dqk-ai-keys`): the computer's keys (`5bb8:1050`: `cok_ai_keys` with the descriptor's `keys` routine, `COK_KEYS_DARK_QUEEN`): the keys waiting through the Dark Queen's reader (`queen_read_code`, the menus' codes: Alt-M 0xcc, Alt-X 0xd7; every key waiting is read and the last kept, `2093:0078`'s loop at `0x114`, the keyboard alone (`queen_key_waiting`)), Space gives the turn back as Champions' does, Alt-M toggles Magic On with its notice, Alt-X gives the gods with the Helm (`cok_combat_gods`; 52db:3feb's own test is the Helm), and nothing is flushed (`keys_unflushed`). The target pick (`52db:3d9e`, `pick_tries_listed`): one draw unused, then tries the number listed. The guard (`5bb8:0fec`) is the shared one, unchanged. The spell choice's prefix (`5bb8:0306`, `queen_spell_choice` in `queen_ai.c`): no cast when fleeing, var 0x02, may_cast 0, or a monster with Magic off; else the choice (part 29a). The geometry (`5d71`) is the shared code, pinned by `test_queen_fight.c`; the footprints have six cells, so `COK_COMBAT_FOOTPRINT_CELLS` is 6 (4 in Champions and Death Knights). Repo tests: `test_dark_queen.c` (`test_computer_keys`), `test_queen_fight.c` (`test_pick_listed`, `test_geometry`, `test_spell_choice`).
Part 13c (`dqk-ai-step`): the computer's step (`5bb8:06b0`, the shared `cok_ai_step` with the descriptor's values): its row 24 text is 13a's (colour 7, a blank below ten), no slot-2 test for a class 5 (`step_no_slot_test`), and its no-move tail (`5bb8:0af0`) runs the spell prefix first when movement is 0 and it is not fleeing (`no_move`, `queen_spell_choice`). The may-step test (`5bb8:0438`, `can_step`, `queen_can_step` in `queen_ai.c`) reads the way's turn from the ways table (`DS:5db4`, `ways`), probes and marks the square: a cloud (kinds 0x1b, 0x1c, 0x1d) or acid (0x46) costs all the movement unless the record is forced or holds what the original tests (0x39 and 0x8f for 0x1c; 0x1e, 0xa0, 0x8f, 0x3f and 0x39 for 0x1d; the record's +0xc8 bits 0x10 and 0x18; acid while the highest level is below 7, unsigned). No saving throw. The step's sound is 0x0b (`52db:06c0`, `step_sound`). The square test (`148f:12ab(c, 0, 0)`, `cok_round_square` at a step) stops, cited, at a cloud or acid, kind 0x1d too. The flight (`52db:0e26`): the ambush byte 0x20 blocks it ("Escape is blocked", `flee_blocked_by_ambush`), else "Got Away" as before. The free attacks (`52db:08e6`): free_attack_effect 0x1b as before, and the blow is barred (not made, the turn's attack used up, nothing drawn) by 0xd2, 0x6a, 0x1f, 0x33, 0x34, 0x35, 0x37, 0x76, 0x82 and 0xd5 (`free_attack_barred`); the over flag goes to `combat->attack_over` (DS:bd2e, which turning, part 26, sets to 1 at its end). The unbarred blow is the strike (part 14a: `52db:1417`, below). Repo tests: `test_queen_ai.c` (the ways rows, the may-step cases, the sound, the no-move tail, the flight, the barred blow, the unbarred blow, the stuck counter, the square test), `test_dark_queen.c` (the ways table's bytes against the executable).
Part 15a (`dqk-ai-fight-a`): the computer's turn's frame, `5bb8:0000` (`queen_computer` in `queen_ai.c`, reached from `cok_combat_computer` by the descriptor's `computer_routine`). A record with no target picks one first (`52db:3d9e` with 0xff, 1, 0, 0; the result unused); the facing effect 0xd5 ends its turn with no draw; the keys (`cok_ai_keys`), the bar and the text; a record that cannot act ends, and its way of moving is still drawn (`0074` falls through); morale (`cok_ai_morale`, with `morale_uncapped`: no 0x66 cap; `morale_signed` and var 0x1b, a byte, `COK_VAR_MORALE`; no effects removed on a flight); one fleeing and not forced says "flees in panic"; a pending spell's cast is part 29a's (`5bb8:0170`); turn undead (`5bb8:0268`: not fleeing, turns below the undead count, a cleric or a paladin above level 2; the search `52db:135e` (`queen_undead`, limit 13) finds one, and `queen_turn_undead` (`52db:1117`, part 26) turns it, the turn ending (`5bb8:01b8`)); the spell choice (`queen_spell_choice`, part 29a); the weapon's choice is ported (`queen_choose_weapon`, part 15b), then the keys again (`1050`), and the fight and its loop (`5bb8:0209`) are part 15c. `02f5` is not called (no draw). A surrender's leave (`148f:1a69`, part 16) stops through `surrender_unported`, on the surrender branch: `cok_combat_leave` also serves the flight (`52db:0e26`), which is ported. `computer_unported` is NULL. Quick (`5045:0a2c`) still stops at its hand-on, citing `5bb8:0000`: dqk_fight's quick_check compares its state there. Repo tests: `test_queen_ai.c` (`test_computer_*`, `test_morale_*`, `test_spell_choice`).
Part 26 (`dqk-turn`): Turn Undead, the command's and the computer's. The command (`queen_commands.c`'s `ITEM_TURN`, from `08cf`'s 0x9c7) checks again (a cleric, class level 0 above 0, or a paladin, class 3 above 2, with fewer turns (+0x12) than DS:c2ed, `game->undead`; else the menu again), runs `queen_turn_undead` (`52db:1117`) and ends the turn (`0b29`, `51c2:0c11`). `queen_turn_undead` (`queen_ai.c`): "turns undead..." on row 10, the bottom bar blanked, a pause (`1a62:0949`: one draw with var 0x12 nonzero), the turns up; the d20 is drawn first, then the level, which draws nothing: the cleric's level (signed), or the paladin's less 2 where higher; deity word 0x19b 2 adds 2; above 8 it is 9 below 14, else 10; var 0x0f (`COK_VAR_TURNING`, unsigned) is taken off it, or the level is 0 where the var is above it. The undead search (`queen_undead`, `52db:135e`, limit 13: the enemies in sight (`51c2:0980`), not forced (+0x11), the highest signed word +0x5c kind above 0 and no higher than the limit, the first of equals) gives each undead its table value v (DS:5a6a, the kind's row of 11, the level's column): |v| above the d20 lowers the limit, so the search goes on below that kind; else v above 0 is "is turned" (`cok_combat_leave`, status 3), and v 0 or below is "Is destroyed" (the arena's kill, status word 8, inactive). The count is 0xff until the first undead is hit, then 1d6 + 6 where v is negative (the bonus of six turns, `extra`) or else 1d12, and it drops by one per undead hit (the bonus first). None turned: "Nothing Happens..." (one draw). The sides are counted (`51c2:0920`), End (`51c2:0c11`), and DS:bd2e (`combat->attack_over`) is set to 1 (End's result). The computer's turn (`queen_computer`'s `turn_undead`, `5bb8:0268`) runs the search with limit 13 and, when it finds one, `queen_turn_undead`; the turn then ends (`cok_combat_end_turn`, `5bb8:01b8`; DS:ae48 is not modelled, see `queen_computer`). The tables and words: `data_dark_queen.c`'s turning table, `rules_dark_queen.c`'s `words.destroyed` and `words.nothing`, and var 0x0f. Repo tests: `test_queen_ai.c` (`test_computer_undead`, `test_computer_undead_count`, `test_undead_search`, `test_turn_undead_levels`, `test_turn_undead_d20`, `test_turn_undead_limit`), `test_dark_queen.c` (`test_command_turn`); the `turn` flow of `dqk_fight` is in docs/checks.md.

Part 14a (`dqk-strike-a`): the attack's roll, damage and strike (`attack.c`, `round.c`), with the descriptor's values for Dark Queen. The to-hit roll (`148f:1771`): the party's var 0x2f with the difficulty's adjustment (var 0x27: 1 +2, 2 +1, 3 0, 4 -1, 5 -2), a monster's var 0x01 (`COK_HIT_BONUS_DARK_QUEEN`); a natural 17-20 that hits sets the critical flag (DS:c6b9, `rolls.critical`, read by 148f:16e0, part 27). The damage (`52db:01dd`) is a signed word (`rolls.amount16`, DS:ae55), not below 0, and a backstab multiplies it by 5 for a thief of level 0 (by effect 0xbe), else by (level - 1) / 4 + 2 at most 5; the damage is dealt and reported as a word (`cok_combat_damage`, a helpless target's hp + 5 too). The backstab's weapon types add 0x3f and are read at the item type's 0x28 (`record_type_at`; Champions' and Death Knights' 0x2e). Forgetting (`177e:1346`) walks 0x89 memorized bytes and marks 0x80. The strike (`52db:1417`): sounds 8 on a hit and 0x0a on a miss (only with no item), the event slot + 1 also for a hit with no damage when the attacker holds 0xb8, the pending lists (`148f:2ab8`, `2c4f`, `2d9b`) after each slot, and the sweeps kept. The report's words are set (its layout is part 14b's). The range (`52db:1c9d`) and book (`52db:1853`) are the port's, unchanged. Not done: the missile and the readied weapon's flight (17), the kill (`5837:0d55`, 16a), the effect 0xb8's handler (`78e4:3d76`, a cited stop when it runs), and the other readers of DS:ae55 (the effect parts 27 and 34-38). Repo tests: `test_queen_fight.c` (`test_hit_bonus`, `test_damage`, `test_forget`, `test_strike`, `test_hit_event`, `test_helpless_blow`), `test_queen_ai.c` (`test_free_attack_blow`).
Part 14b (`dqk-strike-b`): the report (`52db:02c0`), the attack (`52db:18d6`) and the sweep (`52db:52e4`), with the descriptor's values for Dark Queen. The report's name is at 0x17, row 12 (`arena_name`, `queen_name_line`), its text is `print_window` in cells 0x17-0x26, rows 13-16, in grey (`arena_report`); "and is Dying" is `1a62:0046` in colour 7 (`dying_colour`); the pause is 13a's (`pause`, once after the text), and "lost a spell" is said with its wait, then the spell is forgotten (0x80) and the target's turn ends. The status tests read words (`cok_combat_field_word`). The attack turns the target (`turned`: 2 for effect 0xd5 (`facing_effect`), else (d + 4) % 8; a target shown is drawn at its pose, which sets the facing) and waits 100 ms (one draw). An item, or a readied weapon of type 0x1c or 0x1d, flies (`missile`, part 17b), and a target brought down goes to the death (`cok_arena_kill`, part 16a). The sweep lists the enemies without the yell swap (`sweep_no_yell`, 52db:54b5). Repo tests: `test_queen_fight.c` (`test_report_text`, `test_report_cells`, `test_report_pause`, `test_report_spell`, `test_report_kill`, `test_attack_facing`, `test_attack_wait`, `test_attack_missile`, `test_sweep`). Not done: the name's colour (no test), and a readied weapon's read when there is none (0000:0028, taken as none: `cok_combat_attack` says so).
Part 15b (`dqk-ai-fight-b`): the computer's weapon choice, `5bb8:1396` (`queen_choose_weapon` in `queen_ai.c`). Its needs (aca8-acab) come from the target's effects (`queen_weapon_needs_of`: 0x7d 2, 0x3c 3, 0x50, 0x51 or 0x67 1, 0x29 for flagged weapons alone, 0x42 and 0x43 halve); the score `5bb8:1271` (`queen_weapon_score`) reads the item's bonus, cursed (`rules.item_record.cursed`, Dark Queen's +0x34 only) and the type's bytes, with the hands taken off slots 0 and 1 for the while; the missile and melee choice, the side list (`cok_combat_side_list`, range 1), the slot 0 swap, the stats and attacks (`52db:0edf`, `cok_combat_weapon_attacks`, checked equal), slot 1's hands, the readying (`queen_item_ready`, View's `86b8:1965`, which needs the View's record shown: `vm.character`), and the panel when changed. No draw. Its checks: `test_queen_ai.c` (`test_weapon_*`), and `dqk_fight.py`'s `turns`/`AltQ` flows, which now run on through the turn (part 15c). Their weapon checks (`dqk_fight.py`'s `weapon_original`, `weapon_entry`) run a second original to the panel's call (`177e:085f`, from `5bb8:19a5`, return `19aa`): the draws from the entry to it are exact (the pattern's and the pick's), and no event-1 effect is held. eclplay's state is no longer compared there (its stop is later).

Part 15c (`dqk-ai-fight-c`): the computer's fight, `5bb8:0b3d` (`queen_fight` in `queen_ai.c`), and the turn's loop, step 14 of `queen_computer` (`5bb8:0209`): while the pick (`52db:3d9e` with 0xff, 1, 0, 0) finds a record with initiative above 0 and +0x18b set, a fight pass runs; the loop is bounded as `ai.c`'s, past 2 * COK_COMBATANTS + 8 passes it is undefined. A pass is the fight's prologue, the fleeing steps, the initiative and count tests, the reach (the weapon's type 12), the kept target, sight (`5d71:0328`, see_all 0), the enemies in reach (`cok_combat_side_list`): none means the pick (`52db:3d9e` with 0xff, 0, 0, 0: found, 13's step, where a spell begun ends the fight; not found, the guard and done); some means the pick by `d(1, n)` (`148f:1968`), and a missile weapon is chosen (`queen_choose_weapon`, the block's target, var 0x3c); in reach it is the centre, the sweep (which ends the turn), the book, the ammunition (`51c2:10c3`) and the attack (`cok_combat_attack`, part 14). A missile's flight (`52db:2888`, part 17b) is drawn by the game's hooks: the flow `altq_missile` checks its strike (`148f:077f`, see `docs/checks.md`). The listing restore of the distance (`51c2:0b26`, `cok_combat_distance`) keeps the listing's entry 0 and entry 1's index under `distance_kept_bytes` 4 (Dark Queen; 0, the default, keeps the whole restore of Champions' and Death Knights'; 1df9:0432 -> 10d7:2776). Repo tests: `test_queen_ai.c` (`test_fight_*`, `test_loop_initiative`, `test_distance_kept`, which pins the 4 bytes), `test_dark_queen.c`'s computer act. Its check is `dqk_fight.py`'s `melee` flow (`docs/checks.md`).

Part 15d (`dqk-ai-fight-d`): no code change; the computer's fight is checked to round 6's first menu (`melee-long`, `dqk_fight.py`). Over rounds 2 to 6 the original's RandSeed, places, records, combat blocks, turn lists, effect chains and screens at each round's first menu agree with eclplay's, with the first strike (round 5's turn 8, SHARKMAN 2 on ROLAND OF DUST, 8 points) in them: the attack (14a, 14b), the computer's step and keys (13a-c) and the fight loop (15a-c) run as the original's there. Part 16b (`dqk-death-b`): the box run `melee-dying` kills ROLAND OF DUST at round 5's turn 8 (the fixture's first strike, with his hit points lowered to 1 before COMBAT): status 5, 0 hit points, the body on the map (0x1f) and eclplay agrees at each round's first menu, the bodies included.

Part 16a (`dqk-death-a`): the death on the battlefield, `5837:0d55` (`cok_arena_kill`), with the descriptor's values for Dark Queen: the bodies 1-8 searched (`kill_finds_body`, no stale byte), the sound 6 outside combat too (`kill_sound`), no body at status 8 (`kill_no_body_status`), a stone (0x1e) at status 7 (`kill_stone_status`, `kill_stone_tile`), the cells 0x1d, 0x1c and 0x46 kept under a body (`body_spared`), the spell kept (`kill_clears_spell` false). The skull's nine flashes go through the drawing's icon hook (slot 0x18, facing 3; `204c:030f`, `arena_icon`), the waits through the drawing's wait and pause (one draw each: ten per death in a fight, one outside). 137a (`cok_cloud_under`) looks through `under_tiles` (0x1d, 0x1c, 0x1b, 0x46) and a body's cell. A cell under a cloud or body (0x1b-0x1f, 0x46) is drawn as the ground under it, then COMSPR's overlay of its own tile masked over it (`queen_arena.c`'s `overlay`, 172c:17ac-18d4). Verified, pinned by tests: `148f:1b66` (`cok_combat_battle_only`; its list (DS:00a2, 0x4d its 11th id; 148f:017d removes one node per id) goes before the berserk test, so a turned record needs a second berserk to go back to the party), `148f:1a69` (`cok_combat_leave`), `5837:10bd` (`cok_combat_place`). Champions and Death Knights keep their values (sound 5, the old spared cells, no status test). Repo tests: `test_queen_fight.c` (`test_kill_dqk`, `test_under_cloud`, `test_battle_only_leave_place`, `test_report_kill`, `test_report_quiet`), `test_queen_arena.c` (`test_overlays`). Not done: the box run (16b).
Part 17a (`dqk-missiles-a`): the missile's pictures and the use-up, with the descriptor's values for Dark Queen. `52db:2888` is `cok_combat_missile_pictures` (the choice, nothing drawn) then `missile`: an arrow (`fight.arrows`: 4, 5, 7, 0x0c, 0x1e, 0x42, 0x4a) is slot 0x0f + d % 4 (image d / 4) for an even direction, slot 0x10 for an odd one (image 1 for 3 and 5, mirrored for 5 and 7); spinning 2, 3, 6 are slot 0x12's four (sound 0x0a, 50 ms); a sling stone (0x1c, 0x1d, 0x43) is slot 0x16's two (sound 7, 10 ms); any other, 0x36 and 0x37 too (no thrown kind), slot 0x15's two (sound 0x0a, 20 ms); no leading sound. The item type is read at the record's type (`record_type_at`), as the hoopak (`177e:218e`) and the sling test are. The use-up (`cok_combat_use_up`, `52db:18d6`'s 1ad2-1c0d) reads the count (+0x35), readied (+0x32) and effect (`item_record.effect`, +0x37; 0x17 is the spiritual hammer, `COK_EFFECT_HAMMER`, which is not put in the pool). The flight is part 17b's (below). `use_up_freed` stops, cited, for Dark Queen's removal (`177e:13d2` writes through NULL for an item not in the list). A dart (type 5, flags 0x1a) is not thrown in the data (its flags have no 0x14): the pool tests use an axe (type 3, flags 0x14). Repo tests: `test_queen_fight.c` (`test_missile_pictures`, `test_item_type_at`, `test_use_up`, `test_attack_missile`), `test_attack.c` (`test_missile_pictures`, Champions' values), `test_dark_queen.c` (the effect names, HAMMER 0x17). Its check is the box run of part 17b.

Part 17b (`dqk-missiles-b`): the missile's flight, `51c2:00df` (`cok_arena_missile`, `arena.c`), with the descriptor's values for Dark Queen. The geometry and the steps are the DKK path's, checked line by line against the disassembly (the clamp of the ends to x 0-0x31, y 0-0x18: `missile_clamped`; the frame when a step is drawn; the walk back and the centring). The waits differ: 2 * delay per frame and for the target, one RandSeed draw each (`missile_wait_twice`, 1df9:052a(delay * 2)). Each frame goes through the drawing's hooks in `dark_queen.c` (`arena_missile_frame`: the icon table's picture decoded and mirrored, 51c2:0005; `arena_missile_draw`: the 24 by 24 under it saved, 19a4:09eb, and the picture drawn masked, 19a4:04e1; `arena_missile_restore`: 19a4:0aba). Its place is 8 + 8 per sub-cell (51c2:00a0's 4 * unit + 0x1f44 through 251e:0595; DS:cda0 and DS:cb7a are 4 at the first turn, read in the box). The save is 24 by 24: 19a4:09eb is called with 12, 12, which are relative (251e:0595 makes them 12 * (DS:cda0 >> 1) and 12 * (DS:cb7a >> 1), 24 each with both 4). The clip is the screen's (DS:cdae and cdb3 0, cdb5 320, cdb9 200 at the first turn; verified: 5837:0846 resets the view's clip (1367) to the screen (251e:075c) at its end, so during a flight it is the screen's). The stop (`missile_unported`) is removed. Dark Queen's spells that fly (`cast.c`'s `cok_arena_missile` callers) are unreachable today: every spell handler is a stop and Dark Queen has no gaze or breath effect. In the original those flights also go through `51c2:00df` (52db:3465's and 4edb's calls), so each part that reaches them checks its own slots and delays. `use_up_freed`'s cite is 52db:1b30-1b56 then 177e:13d2. Repo tests: `test_queen_fight.c` (`test_flight`: the waits, the RandSeed draws, delay 0, the clamp with the screen; `test_flight_place`: the pixel place and the restore; `test_attack_missile`: a sling stone flies, then the strike). Not done: the archers' box check (a `melee` variant with MINOTAURs, not built; optional). `altq_missile` compares at effect 0x69 (148f:077f, the strike after the flight; `docs/checks.md`).

Part 18b3 (`dqk-auto-b3`): the first fight plays to its end under Auto (`--combat auto`, part 18a's Alt-Q). The 30th round is the round limit (DS:ae4c + 1 >= DS:b45a: DS:b45a is 30 from round 16) with the sides [6, 1] (SHARKMAN 1 got away in round 16), so no continue question is asked (DS:b7f3 stays 1). The first key wait is the results' ("THE PARTY HAS WON", 0 experience points, no treasure: mode 6, flags 1 0 0 0 0 0, standing 1, fled 0, monsters 1, share 0, pool 0 0 0, the six records and the variables as the original's), and Return gives the adventure menu (mode 4, RandSeed unchanged). eclplay stops at the results' wait (`(out of keys in block 1 at 8030)`, with `--state`, `--records` and `--screen`) and at the menu after one more Return (8031). `dqk_fight`'s `auto` flow checks all of it (see Checks against the original).

Part 27b (`dqk-effects-dispatch`): the effects' event lists (`148f:0512`, the 24 cases) are data in `rules_dark_queen_magic.c` (`cok_dark_queen_event_lists`), with the shared ids 0x15, 0x2d, 0x2e and 0x31 (Prayer in 6 squares). Event 6 runs 02ed (magic resistance: a holder with MR at +0xcc rolls whenever an effect is pending or the damage is magic, and the roll stops cited at the caster level, `177e:1900`, not ported) and 2a6d after its list (the record's +0xc8 clears the damage and the effect). 0369's 0xd4 handler gets the selected record. `148f:017d` is `cok_effects_remove`: the ids FRIENDLY 0x0e, ENLARGED 0x0c and STRENGTH 0x26 are named, and the stat recompute for 0x0e, 0x0c, 0x26 and 0x44 runs `148f:1dfc` (see below). `19c2` adds nothing for id 0 and stops cited past the 400-node pool (DS:bd51). The timers (`1396:019f`, dark_queen.c's `pass_time`) walk the whole list with the flags DS:a260 (a camp scan from index 2 to the party size + 1), remove each node the step runs out through `017d` and go on from the last node kept (its link as the removal left it: a removal that frees that kept node stops, cited, at `1396:019f`; the original reads the freed next node there, which the port does not follow), and stop cited at `5e22:0065` for 0xc4 outside modes 5, 3, 0 and 10. The battle's end removes the eighteen 75cc:0593 effects from each record (`017d`). The set-up's event 8 (`59c7:1eb9`) and the battle's start (event 0x18) run two handlers, ported (`effect.c`): 0x64 (`78e4:14d1`, Caergoth's dragons: every record on the other side of status 0 below level 7 without 0x78 or 0x5c "is terrified", 0x78 with flag 1, no saving throw) and 0xc3 (`78e4:4949`, 170e's GORGONs: the holder gets one 0xcb, 255 minutes, value 4), and 0x69 (`78e4:1aa2`, a ranger's damage: a target within a square of creature flag 0 bit 3 takes the holder's ranger level more on the word, the distance called first; `h_queen_ranger`, repo test `test_ranger`; the auto flow agrees to round 15, where it runs and the add does not apply). The chains are compared with the original's by `dqk_fight.py`'s flows (`--effects`, see DEVELOPERS.md) and the `timer` flow (ECL_CLOCK 1 2 over rewritten nodes and record bytes, `dqk_modify.modified`: the records of members 4 and 5 after ten minutes agree too). Repo tests: `test_effect.c` (`test_dark_queen`), `test_dark_queen.c` (`test_timer_walk`: a removal that removes the next node, and one that removes the kept node).

The racial effects (`dqk-spells-a0`): 0x5c (`78e4:1334`: the cancel of 0x6f, `COK_FX_CANCEL`, value 0x6f), 0x5e (`78e4:1355`: a saving throw of type 0, 2 or 4 gains the constitution's bonus, 1 at 4-6 up to 5 at 18-20, `COK_FX_CONSTITUTION_SAVE`; any other constitution stops, the original adding an unset local) and 0x5f (`78e4:13df`: a d100 of 90 or less cancels 0x0b and 0x35, `COK_FX_RESIST_CHARM_90`). The routines are Death Knights' (`504d:2162`, `21bd`, `223d`), but Dark Queen's tests the removing flag first and does nothing then; the shared handlers do not, and the racial nodes are made with the flag 0 (`queen_create.c`, `148f:19c2`), so they never run removing. ASLEEP is 0x35 (`cok_dark_queen_effect_ids`, as in Champions). Repo test `test_dark_queen.c` (`test_racial_effects`, the effect table and names checks).

Part 28a (`dqk-cast-a`): the spell picker Cast uses, `queen_spells.c`'s `queen_spell_pick` (`849a:2080` for kind 0 in mode 1; the other modes and kinds stay stops). The list is the memorised spells (`+0xce`, the 0x89 bytes, each below 0x80) the character may cast (`queen_spell_usable`, `849a:0031`: the spell's kind word and level from `DS:27e9`, the class levels and wisdom or intelligence; kind 2's human-with-armour exception in the battle, the scroll's class 6 test unused by Cast), sorted by level (`849a:05cc`: a spell goes after its own level's spells, the same spell adds " (n)" for n above one). Each level's heading (`cok_dark_queen_spell_levels`, light red) comes before its first spell (`849a:0b39`'s tail): a heading is no one's choice, so the keys (Up, Down, Home, End, PgUp, PgDn) and a click skip it, and the list starts on the first spell. The title is `NAME's` and "Spells in Memory" at the name's length plus 4 on row 1 (the full window in the battle, the title window otherwise: `849a:2080`); the bar "Cast Exit" under the prompt "Choose Spell:" (`849a:019c`, rows 5 to 0x15). Cast (or Return) gives the current spell's id, Exit or Escape 0, the keys running out -1, and nothing is drawn with no memorised spell (0). The Cast command is part 28c (`queen_cast_command`, below). Within a level the memorised order is kept. The armour is the readied armour slot (`slots[2]`, `+0x14`).

Part 28b (`dqk-cast-b`): the spell engine in `queen_cast.c`, with the flash (`dark_queen.c`'s `arena_flash`, `51c2:0731`, its stop gone). The caster level (`177e:1900`, `queen_caster_level`, on the selected record): the non-caster's 6 (class 0 and 5 at 0, class 4 below 8, class 1 below 9), kind 0 and 3 the larger of classes 0 and 1 (signed), kind 1 class 4 less 7 (below 0 is 0), kind 2 class 5 with the moon of the magic order (`COK_VAR_MOONS`, element order - 1: phase 0 takes one off, 2 adds one above class 5 of 5, an order past 3 stops, the moons' days being read there), never below class 4 less 8, kind 4 12; a kind above 4 (Curse's word 0xff) is undefined unless an item gives 6; an item gives 6 unless the kind is 4; the cap (`capped`) is 0x1f, compared signed. The range (`849a:0f34`, `queen_spell_range`): the base at +3 plus the level times +4 (an item: 4 times 6 plus the base), the spell 0x7d a quarter, 0 with a targeting byte is 1, 0xff (a touch) is 1. The duration (`849a:101b`, `queen_spell_duration`): 0x1a 0x0ec4, 0x79 1d4 + 1, 0x7a 0x5a0, 0x7f and 0x82 5d4, 0x80 1d4 * 10 + 40, else the level times +6 plus +5. The apply (`849a:10f9`, `queen_spell_apply`, over `spell_targets` and the count): the damage type is the amount's type (0 for none, kept for a count of 0); the level (value, or the capped caster level); a save word at +0xa stops at `148f:1886` (saved already by status 10 or effect 0x96 with spell 0x37, as the original), a touch (+3 of the cast spell 0xff) stops at `849a:10f9 touch`, cited, before the damage; the damage (`148f:2214`, `queen_cast_damage`: event 6, then the save (1 none, 2 half, signed) or event 0x14, the "takes N points of damage " text with the type's source ("from Fire", "from Cold", "from Electricity", "from Acid" (0x100: a word's value, which the byte type cannot hold, so never matched), else "from Magic" where the type has no bits but 8), the panel, the flash (kind 0), `177e:17a0` (`cok_combat_damage`), the caster's may-cast cleared and "lost a spell" with its wait, forgotten and the turn ended, then a dead target says "Goes Down" (" and is Dying" for status 5, "is killed" for 6-8, neither when creature flags 1 have bit 8) on row cursor + 1 without a wait, its battle effects go (`cok_combat_battle_only`), event 0x0d, the death (`cok_arena_kill`) unless status 10 (else the speed's wait, `1a62:0960`); outside combat the damage stops, cited (`148f:2214 outside combat`). The heal (`148f:25f7` through `cok_effects_heal`, the guard 0xd7 honoured) and its text (`177e:187a`: "is fully healed" at hit points = maximum, else "is partially healed", row 10 with its wait; outside combat stops, `5e22:0065`). Cure Light Wounds (`7fd8:00f8`, `queen_cure_light`: a d8 bonus first for a caster of deity 4, then the heal's d8) and Magic Missile (`7fd8:050a`, `queen_magic_missile`: the level plus one, halved, that many d4 plus the count, the spell's apply with type 8 and no text). `queen_spell_handler` is the table of the ported ids (CLW 0x03, MM 0x0f) that the cast takes before `rules_dark_queen_magic.c`'s unported entries (the cast itself, the targets and the flight are part 28c). Not done: the saving throw (`148f:1886`), the touch's hit roll (`148f:1771`), magic resistance (effect.c's stop, shared code, left), and the other spells' handlers (`7fd8:xxxx`). The words are DS strings checked in `test_dark_queen.c` against `build/DQK_FULL.EXE`. Repo tests: `test_queen_fight.c` (`test_caster_level`, `test_spell_range`, `test_spell_duration`, `test_magic_missile`, `test_cure_light`, `test_spell_apply`, `test_spell_damage`, `test_flash_draws`), `test_dark_queen.c` (`test_yell`, now through the flash; the words).

Part 28c (`dqk-cast-c`): the Cast command (`52db:2543`, `queen_cast.c`'s `queen_cast_command`): the picker (`queen_spell_pick` with the selected record; -1 for keys out or another mode's stop), the camp-only (the word at +0x0f 0: "Camp Only Spell") and underwater (var 0x3c with 0x4a, 0x09, 0x2f, 0x76, 0x73 or 0x7c: "That has no effect underwater!") refusals on the bar, the arena again unless redrawn, then the time (+0x11 / 3): 0 casts at once (`queen_cast`), and the turn ends whatever it gave; else "Begins Casting" (row 10, with its wait), the spell kept, the initiative less the time (floor 1), the caster moved up the turn list past a higher initiative, a tie decided by the 1d2, and `turn_index` less one. The cast (`849a:145b`, `queen_cast`): the announce unless an item ("Casts a Spell" with its wait, the range line cleared, "Spell:NAME" on row 23); the targets (`52db:1f78`, `queen_targets`, by the spell's +7 kind: 0 the caster alone; 5 (Faerie Fire 0x4f, Charm 0x51) `several`, the allowance the caster's level for 0x4f or 2d4, the weights by the highest level (+0x91) or for 0x4f the size (+0x8a) less one, the computer's repeats counting the allowance down; 8 to 0x0e the area, picked as empty too, its radius by `area_radius`; 0x0f one target or the area by +7 >> 4; 1 to 4, 6 and 7 one-target picks (1 + (+7 & 3)): "Cannot target yourself" (0x17, 0x31) and "Already been targeted" are refused picks, and an Exit with no pick is done clear with the cell the caster's). Entry 0 alone is cleared; a stale entry past the count is kept. Exit at the Aim asks "Abort Spell": Yes gives "Spell Aborted" and the spell marked (not done); No aims again (the computer's `ai` aborts without asking; its aim is part 29b's). A done cast flies (`flight`: the missile's frames (slot 0x13, 0x20 for the Fireball family), the arc to the spell's cell (`cok_combat_in_arc`: the first facing whose quarter holds it), the Fireball family's picture by direction (`51c2:0fc8`, `queen_direction_picture`, part 31a) with one frame, the pose and sound (3; 9 for 0x33; 0xc for 0x2f), `cok_arena_missile` with 4 frames (1 for the Fireball family) and delay 0x1e (none to its own cell), the spell's burst (`51c2:0def`, `queen_burst`, part 31a, by the spell), the caster's visible pose again), then the reveal, the forget unless an item, and the dispatch (`handle_spell`: `queen_spell_handler`, CLW 0x03 and MM 0x0f, then 849a:000c; any other id stops "not yet ported: ... (dqk 7fd8:xxxx)" after the forget, as the original stands at 21b5). The spell's handler is cited where not ported (the flight's pictures and bursts are ported, part 31a), a cast outside a battle (`849a:145b`, camp, part 46). The pending cast at the party's turn (`5045:08e3`, the first block of `queen_commands`) is reached from a battle: `command.c`'s `cok_combat_commands` routes Dark Queen to `queen_commands` before its own pending cast (the COK branch and Death Knights' routing are where they were), and the descriptor's `spell_unported` is NULL, so `round.c` does not stop. The computer's pending cast is part 29a's (`queen_computer`, `5bb8:0170`). The `cast` flow of `dqk_fight.py` (checks.md) checks the list, Magic Missile, Cure Light Wounds and the abort against the original. Aim's Manual on an empty cell (C1, `queen_aim.c`): the tail's redraw and cell are `queen_cell_of`'s (the first combatant 1 to the count with no record, else combatant 0: `5837:0b79`), not the picked record's, so `next_target` gives a listed record again, as the original's `3465(0)` does.

Part 29a (`dqk-ai-cast-a`): the computer's spell choice (`5bb8:0306`, `queen_spell_choice`, `queen_ai.c`): the prefix (fleeing, var 0x02, may_cast, a monster with Magic off) as above, then `queen_best_spell` (`5bb8:1b1e`): the memorised bytes at +0xce (0x89 bytes, nonzero and below 0x80; a camp-only spell, +0x0f 0, is not listed) sorted by priority (+0x12), the later slot first among equals (`1a07`, `sort_spells`), walked (`19b0`, `next_candidate`) to the first spell that `queen_spell_worth` (`52db:4789`) gives above 0. The side is the caster's word +0x66, or the other side where the spell's allies word (+0x13) is set; kind 0 (+7 0) is the caster alone, else the combatants listed around the caster within the range (`cok_combat_list`, nearest first) until one is worth something. `queen_target_worth` (`52db:42cc`, exported for part 29b): -40 for the other side; else the spell's test by id (the table of 42cc's compare chain, the default being the spell's effect not held), 4 where it holds, then cut to 0 by the globe (0x39 below level 5, 0x3f below 4) and graded by magic resistance above 0x28 (3 at 0x29, 2 at 0x47, 1 at 0x65, 0 above 0x82; the grade overwrites an earlier 0). The dice: p is the class levels 2, 3, 4 and 1 summed as bytes and shifted two (70 levels give 24); a d100 at or below p casts nothing (one draw); a d5 above the spell's priority (+0x12) casts nothing (two draws); else `queen_cast_command` (`52db:2543`, redrawn), whose done ends the turn. The pending cast (`5bb8:0170`): `queen_cast` with the computer's ai, the spell still kept in `cr->spell` while it casts (the original's 0170 does not clear it first), then the turn ends (`cok_combat_end_turn` clears it). The caster is the selected record (`vm.character`): the choice and the pending cast set it to the computer's record and put it back (the turn sets it too, `round.c:941`). The computer's aim (part 29b, `queen_cast.c`: `pick`'s computer branch, `computer_pick` (`52db:1d42`: the pick of a target on the spell's side by `cok_combat_pick_target_side`, its allies test from the spell's word +0x13, then the target's worth above 0 by `queen_target_worth` on `queen_spell_side`, the shared side rule) and `area_aim` (`52db:48aa`: the sums of the worth, words, over the listed boxes round the caster or her target, reach clamped to 0x12, the half widths of DS:5a50, the cell with the most, a tie of positive sums drawn at 25 in 100 before the bounds test, the caster's cell seen from the cell (`5d71:0328` from the cell: its terrain gives the eye) within the range); not the player's Aim. Repo tests: `test_queen_ai.c` (`test_best_spell_order`, `test_spell_choice_dice`, `test_target_worth`, `test_spell_worth`, `test_pending_cast`, and part 29b's `test_computer_aim_allies`, `test_computer_aim_area`, `test_computer_aim_negative`, `test_computer_aim_edges`, `test_computer_aim_reach`, `test_computer_aim_radius`, `test_computer_aim_centre`, `test_computer_aim_sight`, `test_computer_aim_asymmetric`, `test_computer_aim_inactive`, `test_computer_aim_word_sums`, `test_computer_aim_far_edges`, `test_computer_aim_sight_range`, `test_computer_aim_forced`, `test_computer_aim_line`, `test_computer_aim_silence`); `dqk_fight`'s `cast_auto` and `cast_auto_area` (checks.md; part 31f: the computer's area cast of Delayed Blast Fireball, its burst against the original's tail). The monsters of the flows checked (SHARKMAN, GORGON, ZOMBIE, MINOTAUR) have no memorised bytes in any recorded state, so the choice draws nothing for them.

Part 30a (`dqk-spells-a1a`): the apply's saving throw and touch (`849a:10f9`, `queen_spell_apply`, `queen_cast.c`). `queen_save` (`148f:1886`, exported in `queen.h` for the effects bands) rolls d20 (one draw): a holder of 0x76 rolls 1; a 1 fails and a 20 saves, both with no event 0x0c; else a mage (class 5 above 0 and an order above 0, both unsigned) gets -1 when the byte of var 0x1f9 + order is 0 and +1 when it is 2 (orders 1-3 read `COK_VAR_MOONS`, 4-6 `COK_VAR_MOON_DAYS`; past 6 stops, cited); the roll is d20 plus the save bonus (+0xcb, signed) plus the bonus; the type (the spell's +0xc word) is set for the event 0x0c on a copy of the target (a swap there is read back); and the save is made when the record's saving throw of that type (+0x8b + type, unbounded within the record) is at most the roll. The apply's save: status word 10 saves with no roll, effect 0x96 with spell 0x37 saves, and a spell with a save word at +0xa calls `queen_save` otherwise. The touch (+3 0xff): each target's stats are recomputed (`177e:0be2`, `cok_character_stats`, an error undefined), event 0x0b runs on its slot (`cok_effects_dispatch_at`), then the hit roll (`148f:1771`, `cok_combat_hit`) of the caster (the combatant of `vm.character`, none undefined) against the target's armour class read after the event; a miss zeroes the amount for this and every later target and saves this one. Each use reads the target again and each pass the count. Not done: the other handlers (bands b-d); the plain applies are part 30a1b. Repo tests: `test_queen_fight.c` (`test_queen_save`, `test_spell_apply`, `test_spell_touch`, and `test_spell_touch_slot`: no ported Dark Queen handler swaps a slot (0x47 is a cited stop), so a copy of the rules puts a test hook on event 0x0b that swaps the slot or cuts the count).

Part 30a1b (`dqk-spells-a1b`): the plain applies, 7fd8's 21 routines and 38 ids, each `849a:10f9(spell, 0, 0, 0, 0, DS:text)` (plain.txt): `queen_cast.c`'s `plain_table` gives each id its text and the DS offset its routine pushes (`queen_plain_text`), and `queen_spell_handler` returns the one plain handler for them, which applies the spell being cast (`rolls.spell`) with that text (the apply of part 30a: saves, touch, the effect for its duration). The texts: "is affected" (879e: 0x05, 0x0b, 0x12, 0x16, 0x1d, 0x3c, 0x4d), "is protected" (87aa: 0x06, 0x07, 0x10, 0x11, 0x34, 0x35, 0x36, 0x39, 0x45, 0x58, 0x70, 0x77, 0x7a), "is cold-resistant" (87b7: 0x08), "is shielded" (882a: 0x13 Shield), "is fire resistant" (884b: 0x18), "is silenced" (885d: 0x19, 0x3b), "is invisible" (8869: 0x1e, 0x32, 0x50), "Knock-Knock" (8876: 0x1f), "is weakened" (8890: 0x21), "is blind" (88f2: 0x26), "has been cursed!" (8929: 0x2c), "is blinking" (893a: 0x2d), "is paralyzed" (8964: 0x82), "is toughened" (8a2b: 0x5a), and "" (879d: 0x64, 0x87, 0x88, which add the effect silently). The camp-only 0x12, 0x16 and 0x1f and the item ids 0x82, 0x87 and 0x88 are carried too (the same routines, reached with parts 46 and 50). 0x50 (Invisibility to Animals) is a touch castable in combat and camp: outside combat its touch's combatant is undefined here (148f:1771 is well defined in the original), not reachable before part 46's camp Cast. Spiritual Hammer (0x1c, 7fd8:08e3) stays unported (band c). Repo tests: `test_dark_queen.c`'s magic ids (the words at each DS offset, each plain routine's bytes against its id's table entry: 38 ids, and the routines that are not plain have no text) and `test_queen_fight.c`'s `test_plain_apply` (the handler of each id, and Shield on the caster: its effect added, "is shielded"). The flow `band_a1` (checks.md) runs Silence at SHARKMAN 2 on the original.

Part 30b (`dqk-spells-a2`): the one-target damage and the heals (`queen_cast.c`, `queen_spell_handler`'s ids 0x04, 0x09, 0x40, 0x14, 0x42, 0x48, 0x3a, 0x47, 0x62 and 0x25), each in its routine's order. Cause Light (`7fd8:0162`, a touch): 1d8, damage from magic (type 8). Burning Hands (`01e6`, 0x09 and 0x40): underwater the bar says "That has no effect underwater!" (`queen_bar_message`, whose speed wait draws one at speed 0) and nothing else; else the uncapped caster level, from fire (type 9). Shocking Grasp (`0588`, a touch): 1d8 first, then the uncapped level added as a byte, from electricity (type 0x0c). Cause Serious (`1e31`): 2d8 + 1 from magic. Cause Critical (`20df`, a touch, save kind 1): 3d8 + 3 from magic. The damage goes through `queen_spell_apply` (the save and touch of part 30a). Cure Serious (`1c2e`): with no target nothing is rolled; a deity 4 caster's d8 first, then 2d8, the bonus and 1. Cure Critical (`2073`): the deity's bonus (d8 + 3, or 3) is read before the target test, then 3d8 and the bonus. The druid's Cure Light Wounds (`2c32`, 0x62): 1d8, the deity ignored. Each heal goes through `heal_first` (`148f:25f7` with flag 0, then "is fully healed" or "is partially healed"). Cure Blindness (`0ff6`, no target test): `queen_cure` (`148f:1bc6`) says "is Cured" (row 10, waited) and removes the first 0x21 on the target as `cok_effects_remove` does (its handler only if the effect asks for one), then "can see" is flashed (kind 1). The apply reads the caster level for every spell, so the kind-255 damage spells (Cause Light, Serious and Critical: the computer's) stop with a caster who has class levels, as the scope says; the repo tests use a non-caster. The words "is Cured" (DS:0117), "can see" (DS:88ea) and the underwater text (DS:87c9) are in `test_dark_queen.c`'s list. Repo tests: `test_queen_fight.c`'s `test_band_a2_damage`, `test_band_a2_heals` and `test_band_a2_cure`, and the id check of `test_plain_apply`. The flow `band_a2` (checks.md) runs Shocking Grasp on herself (the touch misses) and the druid's Cure Light Wounds at 0x32 on the original.

Part 30c (`dqk-spells-a3`): Bless, Curse, Prayer, Mirror Image, Dispel Evil and Fire Touch, in `queen_cast.c`'s `queen_spell_handler` (ids 0x01, 0x3e, 0x02, 0x2a, 0x20, 0x49, 0x76). `7fd8:0001` (`bless`) is Bless's and Curse's shared routine: each target whose word +0x66 is not the side is cleared (the whole word, compared as a word: the caster's side for Bless, the other for Curse, `1` where the caster's word is 0); Bless of spell 1 (not 0x3e) in a battle also clears a target of the side with an enemy within 1 (`cok_combat_enemies`, 51c2:0980); a NULL target is read through (undefined). The images flag is set first, before the clears, and then the apply gives the effect (1, 2) with "is Blessed" or "is Cursed". Prayer (`13b7`): the caster level (capped) plus 0x80 where bit 0 of the caster's +0x66 is set, effect 0x31 with "is praying". Mirror Image (`0957`): a d4 drawn first, then d << 5 | the capped level, effect 0x1c with "is duplicated". Dispel Evil (`210b`): effect 4 on the caster for the spell's duration with value 0 and no text (`cok_cast_add_effect`), then the apply: effect 0x20 with "is affected". Fire Touch (`36d6`): underwater (var 0x3c) the bar's "That has no effect underwater!" only; else the apply, effect 0x79 with "is covered in flame". The effects' handlers (0x01, 0x02, 0x1c, 0x20, 0x31, 0x79) are cited stops in their bands. Repo tests: `test_queen_fight.c`'s `test_bless_curse`, `test_prayer`, `test_mirror_image`, `test_dispel_evil`, `test_fire_touch` (the mutants of the melee test, the side's word, the mode, the images flag, the d4 order and cap, the bits, the order of Dispel Evil's effects, Fire Touch's bar), and the DS words in `test_dark_queen.c`. The flow `band_a3` (checks.md) runs Detect Magic and Mirror Image on the original.

Part 31a (`dqk-spells-b1`): the flight's picture and bursts (`queen_cast.c`'s `flight`, `queen_direction_picture` (`51c2:0fc8`), `queen_burst` (`51c2:0def`); `dark_queen.c`'s `cok_dark_queen_burst_spark` (`51c2:00be`, the spark masked with nothing saved under it); the sine table DS:5976 in `data_dark_queen.c`'s `tables[]`), and the skeleton of `src/queen_area.c` (`queen_area_handler`, with the band b markers; parts 31b to 31e fill it). The Fireball family's flight (0x2f, 0x73, 0x4a, 0x85) takes its picture by the facing (odd: slot + 1, image 1 for 3 and 5, mirrored for 5 and 7; even: slot + dir % 4, image dir / 4; slot 0x14 kept) and one frame; the others 4. The bursts after the missile: 0x22 and 0x5b radius 1 (slots 0x1b and 0x1c); 0x2f, 0x73 and 0x85 radius 2 with var 0x22 0 or for the Necklace (0x85), else 3 (slot 0x19); 0x33 underwater radius 2 (slot 0x14, sound 9); 0x57 radius 2, 3 underwater (slot 0x1a). A burst is 3 * radius + 1 rings, ring i of 2i - 1 sparks, each at a random angle a of 180: x from the sine of a + 45, y from the sine of a, times i, plus 50, over 100 (C's truncation), plus the view's offset (3 cells off an edge of the view: byte adds); a spark within 9 cells of the centre draws a random frame of the four. The four frames are the ready, the ready mirrored, the attacking and the attacking mirrored (0005's calls, not 0053's). Where a spell is not yet ported its handler still stops at its citation after the flight and burst, the byte forgotten. Part 31b (`dqk-spells-b2`): `queen_area.c`'s `queen_fireball` (`7fd8:1519`: Fireball 0x2f, Delayed Blast Fireball 0x73 with its level added to the dice before a save's half, the Necklace 0x85 with a d3 first and (2d + 1)d6; underwater the bar refuses, except Lightning's 0x33 whose area is band b3's), `flame_strike` (`2157`: 6d8 of fire, half on a save; underwater the bar) and `ice_storm` (`28fb`: 3d10 of magic once for every target, 1d10 underwater, no save). `band_b1` (dqk_fight) checks Delayed Blast Fireball at SHARKMAN 2 on the open ground to the turn's tail (part 31b): 119 draws past the menu (RandSeed ad10be81), and eclplay's turn mark 01 agrees on RandSeed, records, blocks, turn list and effect chains. Part 31d (`dqk-spells-b3`): `queen_area.c`'s `queen_bolt` (`7fd8:1772`, exported in `queen.h`: the bolt from the spell's cell away from the caster, 2 * len half-cells of walk, a straight step 2 and a diagonal 3, each segment charged at least 2; it hits its last cell's combatant once (`hit`, `7fd8:1a54`: type 4, halved on a save, the missile's slot 0x14), ends at another combatant, a cell costing more than 1 or the line's end, and steps back off the map; a wall in var 0x22 1 bounces it back toward the caster (doubled once with flag where the walk back is 8 or fewer), the first charge after a bounce wraps as a byte; on the open ground a wall is walked through; a line of zero length (an aim at the caster's own cell) walks nothing and keeps the occupant last read, which an own-cell aim reads unwritten: 0, measured in DOSBox-X (849a:145b's poses of the caster leave it), so the caster is hit a second time), Lightning Bolt 0x33 (`lightning_bolt`, `7fd8:1ac3`: underwater the Fireball family's area (`queen_fireball`), else damage type 0x0c, the caster level's d6 first, then `queen_bolt` of 7 with the flag) and Javelin of Lightning 0x81 (`javelin_of_lightning`, `7fd8:1d20`: 20 points of electricity, no dice; its Use is part 50, so the repo tests only). The unit tests (`tests/test_queen_fight.c`, `test_band_b3_*`): the four hits on a straight line each once, the lengths, the save halved by type 4 alone, the bounce with and without flag (the wrapped charge ends the walk), a second bounce with flag spent, a bounce off the caster's cell (cost 2: a zero-length line), the wrap, the open ground, the off-map ends (straight and diagonal), the own-cell aim (the caster hit twice), and the spells' dice, reach, flag and hits, each search's seed with no save made; `tests/test_dark_queen.c`'s test_flight_bursts runs Lightning dry at a foe and at the caster's own cell (no burst: the handler's draws from the flight's), and underwater, to the end. The unit tests (`tests/test_dark_queen.c`): test_direction_picture (the pictures by facing, pixels), test_burst (98 draws for radius 2 at 32, 18; radius 1 at 0, 0 from a pinned seed, 9 sparks in view; the frames' mirrors) test_flight_burst (radius 2 with var 0x22 0, 3 with 1: the handler's draws from the burst's seed, the own-cell flight's seed read at Lightning's dry stop), test_flight_frames (at each of the flight's delays the missile shown is frame 0 for Flame Strike, the facing's picture; Magic Missile's 4 frames) and test_flight_bursts (each spell's burst at its own cell: the rings' draws, the sounds and the burst's slot, dry and underwater, the Necklace's 2 with var 0x22 1); test_burst also draws radius 3 near each edge and centres once more after a burst.

Part 31c (`dqk-spells-b4`): the cone (`849a:19ff`, `queen_area.c`'s `queen_cone`, exported for Meteor (band b5) and Fear (band c)): the aim walked on along its own steps (2 a straight step, 3 a diagonal one) until the walk is 2 * len, clamped to the map, pulled back by the sight (range 2 * len), then the centre line's combatants (each once, none after one met twice), the right side (width 2, the end's neighbour by the last step's direction, DS:8cc8), the left (width 3), or for width 0xff the combatants within 3 of the end; the caster left out. Cone of Cold (`7fd8:299c`): one image, a cone of width 2 and length (lv + 1) / 2 (at least 1), then dN(lv, 4) + lv of cold, "from Cold", half on a save. A cone whose centre line met no one reads a side the line never wrote (`849a:1c4a`): the high byte of the caller's BP, 0xff in every measured cast, so its sides are DS:8dc7's (`tables[]`): the right end x + 0x53, y + 0x43 as bytes, clamped (49, 24, or 0, 24 from x 45 on), the left x + 0x63, y. One aimed at its caster's own cell stops (`849a:1a9d`). `band_b4` (dqk_fight) checks the cone at SHARKMAN 2 with BALTHIR at mage level 9: the tail after T, 30 draws past the menu, RandSeed 6eacbbd8, SHARKMAN 2 at 0x3d, and eclplay's turn mark 03 agrees.

Part 31e (`dqk-spells-b5`): Meteor Swarm (`7fd8:3b99`, `queen_area.c`'s `meteor_swarm`, after its try `3a73`, exported as `queen_meteor_try`): four meteors from the caster, each at the cell its try found: tries 1 to 4 by DS:8788 and DS:8790 steps (`{-1, 1, -1, 1}` and `{-1, -1, 1, 1}`) round the aimed cell, the sight from the caster may pull the cell back, a cell off the map is stopped (range 0xff, halved to 0x7f). Each meteor has its own picture and missile (the flight is not run for 0x7c), a burst of radius 1, the line's combatants (queen_cone, width 1, the caster out) hit whole (dN(10, 4)), then the radius-1 list (the caster in) hit after a save of type 4 (the save first, then the dice, half on a save); a target listed twice takes both. DS:c6f3 (the pending list's flag, 148f:2da1) is 0: the port never runs that list. Underwater, the bar only, one draw (its wait). `band_b5` (dqk_fight) checks the four meteors at BALTHIR's own cell: the tail 358 draws past his menu (RandSeed a1ad7490), TIMASALIN downed, and eclplay's turn mark 06 agrees.

Part 34a (`dqk-effects-1`): the own-effect hook (`effect.h`'s `own`, set by `adventure.c` to `cok_dark_queen_effect`, `queen_effect.c`): a kind with no `handlers[]` entry (spells.h's `COK_FX_QUEEN_*`, all of the band a and later kinds now) runs Dark Queen's own routine, cited in its row; with no hook it fails with the routine's address. `applied_only` (rules.h): the routine does nothing when its effect is removed (flag 1), as DQK's routines test `[bp+0xe]` first (the rows 0x01, 0x02, 0x04, 0x06, 0x08, 0x09, 0x12, 0x1a, 0x21, 0x24, 0x2d, 0x2e, 0x2f, 0x5c, 0x5e, 0x5f, marked in `rules_dark_queen_magic.c`); the shared handlers run either way. `cancel()` and the 2a6d immunity zero the damage word (`rolls.amount16`) as well as its byte: Dark Queen's 78e4:00a1 zeroes the word, and Champions and Death Knights read it nowhere. 0x06 (`78e4:0184`, the cancel of a damage type, value 4) is `queen_effect.c`'s first routine. The rows of effects band a that the shared handlers carry out: 0x01, 0x02, 0x04, 0x08 and 0x2d (`78e4:0198`, `01c6`, `0168`), 0x09 and 0x2e, 0x12, 0x19 (`054a`: the selected character without 0x18 is untargetable, and 4 worse, at events 1 and 0x10; `SEES_INVISIBLE` is 0x18), 0x1a (ROARK's racial, `7e76:01c7`), 0x1b, 0x1f, 0x33-0x35 (HELD, `010d`), 0x21, 0x24, 0x2f (ROARK's `09b2`). A 0x04 on a touch (Dispel Evil's) runs with no stop, but its -7 is overwritten by the touch's d20 (`148f:1771` rolls after event 0x0b), so a touch hits as without it. The melee does the same in the original: `52db:1588` runs event 0x0b on the target, then `52db:16a7` calls `148f:1771`, whose d20 is written to DS:bec5 (the attack roll, `148f:1799`) after it, so 0x04's -7 never reaches a hit roll there either (the port's order, attack.c). Tests: `test_queen_fight.c` (`test_applied_only`, `test_invisible_sees`, `test_held_ends_turn`, `test_cancel_word`, `test_own_hook_missing`, and the rewritten 0x04 and blindness cure); `test_dark_queen.c` lists the ported ids with their segments. Stopped: 0x16, 0x17 and 0x4d (their parts), magic resistance (177e:1900).

Part 34b (`dqk-effects-2`): the damage word and the rolls, in `queen_effect.c`'s switch (the hook's kinds `COK_FX_QUEEN_RESIST`, `BARKSKIN`, `SHIELD`, `QUARTER_LESS`, `GLOBE`, `GLOBE_MAGIC`, `RESISTANT`; their rows in `rules_dark_queen_magic.c`, all `applied_only`). 0x0a (`78e4:01f4`, cold, value 2) and 0x14 (`0313`, fire, value 1): the type's word halved (`idiv`, a signed word: 300 of fire is 150), the save 3 better. 0x0d (`021d`): the save 1 better unless the damage is magic (type 8), the attack roll 1 worse. 0x11 (`02b9`): AC raised to at least 0x39 (`+0x18e`, unsigned), the save 1 better, and Magic Missile's (spell 0x0f) damage word zeroed, its pending effect kept. 0x1d (`0654`): the damage a quarter less. 0x28 (`073b`): a spell of level below 8 cancelled (`cancel(0)`, the word and the pending effect); 0x39 (`0b11`): a spell above 0 of level 4 or below with the magic type (8) cancelled. The level is the table's byte 2, compared as the listing does (`mov %al,%ah; cmp $4`: zero-extended, unsigned). 0x32 (`0a22`) and 0x36 (`0a7e`): the type's damage (2 for 0x32, 1 for 0x36) adds 2 to the save roll, and is zeroed when the save is made or halved when not; the other type, unsaved, is doubled (`shl`). Each row is `applied_only`: the routines return at once when their effect is removed. Tests: `test_queen_fight.c` (`test_effects2_resist`, `_shield`, `_barkskin_ray`, `_globes`, `_resistant`, `_applied_only`, each on its holder at its event, with a no-op case; the three mutants the tests catch: the byte halved, 0x39 without its type test, 0x32 without `!save_made`); `test_dark_queen.c` lists the nine ids. Stopped: 0x16, 0x17 and 0x4d (their parts), magic resistance (177e:1900). Check: flow `effects_2` (below).

#### The Dark Queen's screen

VGA's mode 13h as the game sets it up (unchained, `65fa:06c3`), its
pictures and palettes from its libraries (`src/vga.c`): a picture record
(u16 height, i16 row and column of its hotspot, u8 width / 4, u8 format)
holds its pixels plane by plane, raw (0x10), raw with 0xff transparent
(0x15), ANDed and XORed on (0x11), PackBits of each row's four plane-rows
(0x12, 0x80 doing nothing: `10d7:0a1c`), runs of the planes' rows with
transparent ones (0x17), or entries of the library's records at their
places (0x19: `269b:0aec`); drawn at the place given less its hotspot, the
column down to a multiple of 4 (`269b:0a9c`), within a clip (`251e:06cb`).
A palette record (format 0x18: type, first, count, its cycles' count,
then 8-bit RGB, then its cycles, 4 bytes each: flags, rate, first, count)
sets its colours `>> 2` (`2796:0536`, `261b:00b1`), each shown as `(v <<
2) | (v >> 4)`, as DOSBox-X shows it, and its colour cycles
(`cok_vga_apply_palette`): the colours set are kept apart from the live
ones, which the cycles rotate (`DS:d145`, `cdc5`); a record changes
things where a colour differs from the one set, or where the cycles
running over its range are not as many as its own or one of them differs
in all four bytes from its own in order (the original's test); then the
cycles over the range stop, their colours shown unrotated again, and its
own start in the next free of 12 slots (`DS:d445`), each to step first at
the timer's tick (72.8 Hz, `251e:004d`) + random(8) + rate − 3; re-set
unchanged, the cycles run on. The changed colours are shown at once (the
original flushes them, `261b:0455`, at its next page flip, `19a4:06d3`,
before any wait: drawing goes to the back page between a begin, `05ee`,
and that flip, so that at every wait and delay the screen shows all drawn
so far, as the port's one screen does; the only moments it does not, a
picture's colours black for the time its decoding takes, no wait shows).
`261b:05be` steps the cycles that are due, each a colour along (up: each
takes the one before, the first the last) for each rate passed, as the
game waits: its delays (`1df9:052a`: the text's, `DELAY`'s, the
animation's, each let pass in steps to the cycles' times), the title's
wait, and, where a front end shows a key's wait as it passes, its menus'
waits (`1bdb:1008`: `cok_adventure_menu_animates`, `cok_drawing`'s
`animates`); eclplay's waits for keys pass no time. The cycles' random
numbers are the port's generator (`2102:0234` not ported); a wall set's
cycles move with its colours. The text is `GAME.FON`'s 96 glyphs of 12 rows, of
which VGA draws rows 0, 1, 3, 4, 6, 8, 9 and 10 in an opaque cell of 8 by
8 (`22b7:0c96`), a colour nibble mapped to itself but 8 to 0x17, the
window's grey (`DS:9d86`).

`src/dark_queen.c` draws, from `ALWAYS.TLB` (colours 0x00-0x0f) and
`FRAME.TLB` (0x10-0x1f, its record 41-44 by variable 0x3a, `20b2:047c`):
the full window (`20b2:0030`) and the 3D adventure screen (`20b2:00e4`:
the dividers, the view's frame, its lamps by variables 0x1fa-0x1fc, the
compass by the facing; the party's list and status: see Dark Queen's saved
games and party below); `PRINT` and `PRINTCLEAR` in the text window of cells 1-38 by 17-22
word by word (`1a62:0311`, colour 7, or 3 by variable 0x39); the bottom
bar's menus of framed buttons, each read by its first letter, Return
picking none (`1bdb:08ef`, `0801`, `008e`), `PRESS KEY`'s and a full
window's "Press [Return] to continue." (`1bdb:0495`), `YES NO`, `ASK
MENU`'s "what do you do?", `VERTICAL MENU`'s list; `PICTURE`'s small
pictures (`PICA`, `B` or `C` by id, `63ae:0078`, at cell 3, 3) and their
frames; and the 3D view (`8a8b:085a`): the map's wall sets loaded into
three slots from `8X8DB` and `8X8DC` (47 pieces each, a slot's colours
moved to 0x20 + 37 × slot, `8a8b:119d`), a backdrop from `BACK.TLB` by the
square (a night's one from hour 20 and before 6, variable 8), then the
pieces of the three rows ahead from the far one in, each place's piece at
its column and row (`8a8b:0435`, `DS:9134`), a door over its wall's,
clipped to cells 3-13. The title (`6523:0345`: `TITLE.TLB`'s eight
scenes, each on its backdrop, passed by Space, Escape or a key) and
main's menu (`5ebd:00ef`: Play, Demo, Quit) are `eclplay --start-menu`'s.

#### The Dark Queen's window library and start

Its menus are a list of controls (`2aa6`: buttons, areas, key filters),
each a rectangle the mouse hits and hotkeys, polled for a key or a click
(`2aa6:15e3`); `src/dark_queen.c` keeps each menu's own:

- the keys as `251e:00e7` codes them (an extended key from 0x100: Up
  0x108, Down 0x104, PgUp 0x101, PgDn 0x103, Home 0x107, End 0x105, Left
  0x106, Right 0x102, F1 0x110, Alt and a letter 0xff + the letter), a
  hotkey taking its letter in either case, `@` Return (0x0d or 0x0a), `#`
  Escape (or `` ` ``) (`2aa6:000e`); a key or click nothing takes beeps
  (`2102:0193`, not sounded yet);
- the bottom bar's menu (`1bdb:1e29`, `0d26`, `08ef`, `0801`, `1206`): a
  prompt boxed from cell 1 (`1a62:0046`; a blank added unless it ends in
  `:`), a framed button an item after it, read by its first letter or a
  click on its box (rows 2 above to 10 below its top, 4 pixels past each
  end, `1bdb:008e`'s), the button then drawn pressed (FRAME 16-18, text
  0x83); disabled ones sunken in 0x80 with no hotkey; no current item,
  Return picking none; a filter before them giving Escape (and with the
  ECL's, the extended keys and Alt's) its code, which `HORIZONTAL MENU`
  reads past (`6c70:0e3f` with no escape: Up and Down would change the
  member selected), `YES NO` takes as No (`1bdb:0424`), main's menu as
  Play (`5ebd:00ef`: anything but Demo and Quit); Alt-S and Alt-T (the
  sound's toggles, `2023:0247`, `0272`, not sounded yet) are taken and the
  menu waits on, here, in the lists (`1b05:0664`) and the title's waits; a menu that times out gives its item (main's: Demo after 70
  seconds, 12 after a demonstration: `DS:c5ce`, `c5d2`); the ECL's items
  as `6c70:0d51` has them (lower case, the `~`'s letter kept, a digit
  0x20 on: a letter P-Y), which `GAME.FON` draws as the capitals but for
  `f`; the small picture animating while it waits where one is shown
  (`1bdb:1008`: its frame drawn at once, the next each half second);
- "Press [Return] to continue." (`1bdb:0495`): Return, Ctrl-Enter or a
  click on its button; paging (`1a62:0311`) leaves its bar so, `PRESS
  KEY` draws the bar again;
- `VERTICAL MENU` (`6c70:1e91`, `1bdb:1f25`, the list `1b05:08b3`,
  `0055`, keys `0664`): the rows below the prompt to row 21, the current
  in light cyan; Up or Home, Down or End move it, wrapping; PgUp and PgDn
  go to the top or bottom row shown, then a page; the rows scroll; Select
  (S or Return), Escape or a click on the current row end it, the index
  stored either way; a click on another row makes it current. The scroll
  bar of a longer list (`21e9:082d`) is not decoded nor drawn;
- `INPUT NUMBER` and `INPUT STRING` (`6c70:2e7c`, `2ec8`, `1a62:0781`,
  `05c9`, the editor `6431:0001`): the bar's field, 38 cells, the text
  from cell 2 with its caret, Backspace or Left, Return; a number of five
  characters at most read as `2d84:000b` reads it (a last B, D, H or O
  its base; then digits below it, `-` making it negative, characters to
  0x20 passed over, any other ending it, in 16 bits: "12x" 12, "70000"
  4464, none 0), 0-32767 taken, asked again otherwise or on Escape; a string of 35 in upper case, stored on Escape
  too, " " for none;
- the mouse (`22b7:1be3`): on wherever a driver answers (the front ends'
  `--mouse` in eclplay; always in krynn), the pointer from pixel 8, 8,
  either button alike; its cursor drawn by the game, not the driver
  (`cok_drawing`'s `cursor`, `cok_adventure_cursor`): `ALWAYS.TLB`'s 1, a
  sword, over a control, its 2, a shield, elsewhere, at the pointer less
  its hotspot, to the pixel; hidden on the title, in the ending and while
  a line is edited.

`PICTURE` from 0xf0 shows a big picture (`63ae:0321`, `03b0`): `BIGPIC.TLB`'s
library n (240 and 241 the overland's halves, 242 the demonstration's),
loaded unless it is (colours 0x20-0xff black first), in the overland's
frame (`20b2:02d3`: the full window, FRAME 7 under row 16), its 304 by 120
at cell 1, 1, its palette with the water's cycles; on the overland (mode
3) the party's place over it, `ALWAYS.TLB` 29 at the cell past variables
0x25 and 0x26 (`7065:0011`). The graphics' state (`DS:5eac`, `5e98:0200`)
frees, as it changes, the small picture and the wall sets (reloaded by
the next view) or the big picture. The view again (`5e22:0000`, `PICTURE
0xff`) on the overland (variables 0x22 0, 0x24 1) is its map, variable
0x3e's, again; `CALL 3` waits its delay with no small picture too.
`PROGRAM 1` is the ending (`6523:03cb`): `FINAL.TLB`'s three scenes in the
overland's frame, each record over its first at cell 1, 1, with waits of
36 hundredths and more, as its code steps them; the other `PROGRAM`s but 0
(the camp, above) stop the run.

The start (`5ebd:00ef`): the title (`6523:0345`: each scene's wait,
`6523:000c`, gives its key's low byte, Space when the time runs out:
Space the next scene; Escape ends it, ALWAYS's colours not set again;
another key, or the last scene's end, ends it through `6523:0319`); main's menu (Play, Demo, Quit, its
name and version); Quit to DOS. Play is the camp's menu (`7368:046f`,
with no party): `GEN.TLB`'s picture under the window (`20b2:03ce`), twelve
buttons of 18 characters in two columns (`7368:01a7`), those a party needs
disabled (`DS:3c3e`); Exit From Game asks "Exit to DOS?" and quits
(`7368:081a`), Load Saved Game asks "load from where?" (`604b:2e87`: Tdqk,
Dkk, Exit; Exit back) and loads (see Dark Queen's saved games and party
below), Add and Delete their rosters, and Create makes a character (see Create Character below). Demo, or the menu's time, runs the
demonstration (`5f07:10cd` with `DS:ae59`): block 7's start with the speed
5, its keys read only where typed, Escape or `` ` `` ending it
(`5f07:0f46`, `DS:a890`), then the title again (unless a key ended it)
and the menu; block 7 stops at its `ADD NPC`. `krynn --game dark-queen`
shows it all in 256 colours (its frames eclplay's, its window's colours
compared with them in its `--shots`).

#### Dark Queen's characters

Its record is `dark_queen_character` (`src/rules_dark_queen.c`, each field
with its evidence: where Death Knights' import `604b:0d65` moves Death
Knights' field to, where the ECL's fields `6c70:040d` and the stats
`177e:0be2` have it): 0x19e bytes in memory and in its files alike, so
that the record is not of a variable length and fits the buffer
(`COK_CHARACTER_MAX`); what varies, its items and effects, the port holds
in the character's lists, as it holds the earlier games'. Its C rewrite
keeps an enum as an int: race, class, undead, gender, alignment, status,
hostile and deity are words, named by their low byte (the high one 0 in
every file and import), which the ECL reads as words. Its name is a C
string at +0x68 (the layout's `c_name`: `cok_field_name` and
`cok_field_set_name` read and write a name in either form), it knows its
spells as bits (`KNOWN_SPELL_BITS`, 16 bytes at +0x157, by id - 1) and
has nine levels of spells a day in each of its four rows; it has the
former levels and the level a human changed class at (+0xac, +0x92), but
no fighter level, shares, levels shared out, icon colours or experience
for a monster's hit points. The derived fields are recomputed as the
original recomputes them: the stats by `177e:0be2` (`COK_STATS_DARK_QUEEN`)
and the levels by `8bcc:07f1` (`COK_LEVELS_DARK_QUEEN`, below), the first
where the loaders run it, the second only where the original does (a
character joining the party from the roster, `604b:2021`, training and a
knight's new order: not a `.QCH`'s read, a saved game's load nor Death
Knights' `Dkk` load, so that a loaded record keeps those fields its file
holds): `cok_character`'s readied slots are the stats'.

A character's file is one (`saves.character_files`
`COK_CHARACTER_ONE_FILE`, `.QCH`; `cok_character_from_bytes` and
`to_bytes` for the copies a saved game holds): the record, then, while its
items' far pointer (+8) is not nil, items of 17 bytes, as many as the
pointer's low word (the writer `604b:2200` puts the count there, as a long),
then, while the effects' pointer (+4) is not nil, effects of 9 bytes, each
with its next pointer (`604b:05fd`). An item is 17 bytes in its files
(`rules.item_record`): the port holds it as the original's 0x3d-byte item
in memory, those bytes at +0x28 (+0x28 its type, +0x2c its weight, +0x32
readied, +0x35 how many), with a name at +5 that no file holds; a scroll
(type 0x49) is followed in a file by as many items as it holds, which
`604b:05fd` drops with it ("SCROLLS DROPPED!") whenever there are any, so
that the port holds no scroll's contents (its writer refuses one with
some). The pointers' bytes the original writes as its heap has them; its
loader tests them against nil only: the port writes the items' pointer as
the original does (their count), the effects' and each effect's next as 1
or 0, and the others 0. Every shipped `.QCH` reads and writes again byte
for byte but those, and the six characters of the shipped saved game
(`SAVE\SAVGAMA.QSV`, from byte 0x40f) read so are, but for the pointers,
the original's party in memory once it has loaded them, its recompute
`177e:0be2` run (DOSBox-X, 2026-10-27; `tests/test_party.c`). `ITEMS.DAT` is the item types, 128 of 16 bytes read
from its start (`64b2:0006`; `files.item_types_at` 0 where Champions'
skips 2); `ITEM.DAT` 254 items of 17 bytes, entries 1-254 of the
treasure's table (`64b2:066c`, entry 0 all 0xff).

Its monsters are `MONCHA.GLB`'s records by id (`archives.monster_library`:
`cok_monster_read`, `604b:257c`): a big-endian size, then PackBits of the
record, a count of effects and of items, the effects of 5 bytes and the
items of 17, its pointers cleared, its item count and the classes whose
items it can use (DS:63f3's bits by its classes with levels,
`characters.class_usable`) set again; all 98 read. Their icons are `CPIC.TLB`'s records
(`archives.icons`, the loader's names without digits: `file_digit` is 0).

Death Knights' characters import (`characters.import`'s
`COK_IMPORT_DARK_QUEEN`, `604b:0b82`, the `.pc` files of the Death Knights
save path `DQK.CFG` names, and `604b:2bcf` a Death Knights saved game's,
`SAVGAM<letter>.PTY`'s `CHRDAT<letter><n>.GAM`, its NPCs left out): the
record whole or not at all, 64 moves into a zeroed record, the name made a
C string, class 7 (the eighth's levels) made class 1; the effects (`.EFF`,
at most 0x8c bytes), each id by `604b:1cc7`'s map; the items (`.JNK`, at
most 0x3f0 bytes) each made Dark Queen's 61 bytes (`604b:181b`), its
name's words by `604b:1bf1`'s map and its spells by `604b:1ea3`'s (a
scroll's three, another's power an effect where +0x38 is 0x80), type 0x34
made 0x33, type 0x36 dropped; the spells (`.WIZ`, under 0xdd bytes): the
memorized and known by the spells' map, the known as bits, the spells a
day's rows 0-3 to rows 0, 1, 3, 2. The hit points are capped by level
(`86b8:354b`, `cok_queen_most_hit_points`) and the recompute follows when
the character joins; not ported: the item of type 0x3a readied again
(`86b8:1965`).

The levels (`src/queen_levels.c`, tables in `src/data_dark_queen.c` from
the data segment, `cok_dark_queen_data`, which `tests/test_dark_queen.c`
compares with `build/DQK_FULL.EXE` byte for byte; the descriptor's `data`):
the experience a level needs (`8bcc:0e9c`, 19 longs a row of `DS:0852`, the
row by class and deity, order or robes, `177e:1f39`; past level 17 so much
more a level from `DS:028c`, level 0x29 `0x7fffffff`), whether a class may
train (`177e:1fe5`, by race `DS:17dc`'s top levels, a knight of the Crown
the 18th, of the Sword the 18th, of the Rose the 40th), the spells a day
(`8bcc:000f`, 29 levels of 9 in the tables of `DS:0b96` chosen by `DS:0847`;
a wisdom's extra cleric spells), the saving throws (`8bcc:0c72`, `DS:13cc`,
7 classes of 22 levels of 5: the best of the classes with levels, a dwarf's,
kender's or ring's bonus), the thief's skills (`8bcc:0af3`), THAC0
(`DS:6359`), the highest level (+0x91), the attacks (+0xb3) and the classes'
items usable (+0xbf, `DS:63f3`; a readied item of a class no longer usable
is put by), and the hit points a level gains (`86b8:380e`: the better of two
rolls of the class's dice, a first level's several, plus the constitution's
(`86b8:3a5b`, `177e:20ce`), the sum over the classes counted; at the top level
(`DS:640a`) a flat 1, 2 or 3 by the row). The dice are the original's
generator (`cok_random_select`), so a seed set at the title (the port's
`--seed`, the box's `seed=`) rolls the same hit points in both.

The combat tables (`cok_dark_queen_data`'s `table[]`; `tests/test_dark_queen.c`
checks each element against the executable): the placement's moves, quadrants,
quadrant directions, the first rank's centres and formations (`59c7:128e`,
`59c7:16b3`), the line directions (`5bb8:1e9b`), the steps (`DS:6189`, `6192`),
the terrain (`DS:6197`: 77 values, to the footprints at `DS:62cb`; 4 bytes a
value; `59c7:0a69`, unbounded, so values 66-76 are read too), the terrain flags
(`DS:5d7b`: 32 bytes, all copied by `59c7:0783`, where Champions has 16),
turning undead (`DS:5a6a`: 13 kinds of 11, `52db:1117`), a spell's area half
widths (`DS:5a40`: words, 7 rows of 3, `52db:48aa`), the footprints (`DS:62cb`:
12 bytes a size, `5837:000a`; six cells, `5837:01b9`; eight sizes here as the
count is not bounded by any code found), a cloud's cells (`DS:6342`-`634a`, k
1-9, `5837:13c4`, nine where Champions has four), the helpless effects (`DS:6313`,
k 0-3, `177e:0a19`) and the battle-only effects (`DS:00a2`-`00b1`, k 1-16,
`148f:1b66`; the port's index is k - 1, which the shared loop in `round.c` counts
from 0 as well, so the table starts at `DS:00a2`, index 0). `75cc:06cb`
reads a second, 18-entry list at `DS:631e` (`631e` + k, k 0-17) whose role is not
named; it is not filled. Not filled: the beside tables (`DS:62f0` holds footprint
bytes; no reader found), the cone sides (`DS:8cc8`, casting), the area templates
(`DS:03b5`, treasure).
The spells and effects (`src/rules_dark_queen_magic.c`, `cok_rules_dark_queen`'s
`spells` and `effects`; `tests/test_dark_queen.c` checks them against the
executable): the names are far pointers at `DS:25c7` + 4 * id (pushed by
`8438:0009` and kin): ids 1-0x89 (0x41, 0x61 and 0x63 point at an empty string),
none for 0x8a-0x8c;
the spell list headings are `DS:8b2e` + 41 * level ('Special', '1st Level' to
'9th Level'); the spell records are `DS:27e9` + 22 * id (ids 0-0x8c, in
`spell_table`), the level at +2 and the effect at +14, not the 16-byte record the
shared readers take (so no `COK_DATA_SPELLS` shape yet); the handlers by spell id
are `DS:af45` + 4 * id, stored by `7fd8:46bb` and `4892` (136 ids; 0x41, 0x5f, 0x61
and 0x63 are never stored: `DS:af45` + 4 * id is 0:0 there, and their stop cites
that); and by effect id `DS:bee8` + 4 * id, stored by `78e4:4fb2` and `549e` (227
ids; 0x80, 0x90 and 0x9d are never stored, and their `{0}` stops "has no
handler"). Each handler is its routine, cited (`dqk seg:off`, past an overlay
stub: 0x75's `2d99:10a3` is `52db:4fd1`) and unported, except a bare retf
(`push bp; mov bp,sp; pop bp; retf`, by its bytes): COK_FX_NOTHING for 40 effects
(`78e4:4f2f`, `0218`, `0238`, `02b4`, `09ce`, `0aca`, `0d30`, `10c0`, `24c3` and
`7e76:0075`). Effect 0 is COK_FX_NOTHING only until start-up's stores are run over:
`DS:bee8`-`beeb` is its far pointer, and SETUP MONSTERS (`6c70:2dc3`, `mov %al,
DS:bee8`) and other stores overwrite it, so id 0 = NOTHING holds only after
start-up (SETUP MONSTERS' sprite is kept apart from it, see the encounter above). `tools/dqk_magic_ids.py` prints the
handler lines from those stores. The named spell ids are the Dark Queen spells
of the same name (Entangle is 0x4e here, not Champions' 0x4f), and two roles:
an item's Fireball is the Necklace of Missiles (0x85, the dice at `7fd8:1545`),
a deity's granted Bless is 0x3e (kind 3, level 0, one of the granted powers
0x39, 0x3b-0x40; Bless 0x01 is the spell). Casting's `cast.c:2936` hard-codes
`level_of(game, 0x4f)` for Entangle, which is Faerie Fire here: left for the
casting part. The named effects have no ids yet (`effects.ids` is NULL, so a
named effect's lookup asserts), as the effects' parts set each one with its
routine.

Train Character (`6be8:0022`, `src/queen_train.c`'s `queen_train`) and Knight
new Order (`7368:10c2`, `queen_knight_order`) are the camp's: the classes
the experience takes up a level in, in the hall variable `0x30`'s bits (or
anywhere with `DS:bd92` set, which no routine found sets), each a level, the
hit points, the screen "will become: a level n class" and Yes, "Congratulations...",
and the experience held below the level after's (6be8:0385); the order
changed with its hit points and levels lowered while the experience falls
short (`7368:0fe0`). The creation's silent training (`66e5:1a90`) is
`queen_train_silently`: the variable 0xff, `DS:bd92` 0, silent (no hall, no
experience cap, a mage's spells by order and level 3, 5 and 7), trained again
until nothing trains, the variable and `DS:bd92` put back. Checked against
the original by `dqk_train.py` (Train, Knight new Order, the saved game
written) and `dqk_add.py` (Add, the levels as the original computes them).
What remains for step 4 (combat and effects): the item power put by when a
readied item's class is lost (`86b8:1965`, `16f7`), the new-spell picker of
a mage's or ranger's training (`849a:2080`, which stops the run), the NPCs'
levels at joining (`3491`), and the callers of `8bcc:07f1` in `78e4:17dd`
and `7fd8:2a48`.

The ECL's `0x0401`-`0x0800` are the selected character's fields alone
(`ecl.game_fields_only`; `game_first` 0x401): `0x041f`-`0x04ab` the
record's bytes from +0xce (the memorized spells and on, `COK_ECL_FIELD_RAW`;
a store of 0 forgets a spell there, 0x80, and 0xff is 0:
`COK_ECL_FIELD_FORGET`), the others by its table (`6c70:045a`, 58
addresses), the ints as words (`COK_ECL_FIELD_INT`), a store of the status
making it able to act or not (`COK_ECL_FIELD_STATUS_INT`); an address no
field is reads 0 and a store to it does nothing; a store goes in whole,
whatever its operand's width (`6c70:06d2`). `eclplay --game dark-queen
--character FILE.QCH` adds characters to run blocks that read them (the
party's other options are a playable game's).

#### Dark Queen's saved games and party

A saved game is `SAVGAM<letter>.QSV` (`saves.game_size` 0x1f1d: `604b:28f8`
reads it, `604b:31a7` writes it; `cok_saved_game_read` and `write`, which
hold it where they hold the earlier games'): the variables
`0x0001`-`0x0400` (the 1 KB at far `DS:b20f`, from its second byte); then
`DS:c9f0`-`c9f4`, the party's square, facing, wall ahead and square's
byte; the last mode and the mode (`DS:ae67`, `ae57`), words; the wall set
`LOAD FILES` last loaded (`DS:ca03`, a word: `wall_ids[0]`) and `DS:ca05`
(a word no routine found sets: `saved_extra`); the party's size, a word,
at 0x40d; each character as its `.QCH` holds it (`604b:05fd` for each,
the scrolls' contents the party read before it holds counted,
`86b8:1f75`, so that a scroll's are always dropped, "SCROLLS DROPPED!";
the writer `604b:2200` for at most eight); then, up to 0x1f1d bytes,
whatever `DS:c6de`'s buffer holds, which the loader reads none of: the
item table (`ITEM.DAT`'s 254 entries after entry 0's 0xff, read at startup
by `64b2:066c`: `files.item_table`, the adventure's `item_table`), its
first bytes. Both shipped saved games (`SAVE\SAVGAMA.QSV`, the one the game
loads, and the one beside `DQK.EXE`) read and write again byte for byte
but the characters' far pointers (the original's heap; `tests/test_party.c`).
The loader stops at a character it cannot read whole and fails, keeping
those it read before (the port fails the whole).

The vault is `VAULT<letter>.DAT` (`saves.vault_items` 50:
`cok_vault_read_game`, `write_game`; `63ee:0239` reads it, `63ee:00df`
writes it), 866 bytes as shipped: the coins, 12 bytes into `DS:b1db`; a
word the writer makes 0xffff and the reader passes over; the count of
items, a word; each item's 17 bytes (a scroll followed by its contents,
which the port refuses, holding none); then, for each item short of 50,
17 bytes of `DS:c6de` again. The shipped `SAVE\VAULTA.DAT` is empty (its
items the item table's first) and writes again byte for byte. The game
touches it as a saved game is written under another letter than its own
(`DS:c3ea`, 'Z' for none): the old letter's read (`604b:35bc`, an empty one
for 'Z') and written as the new letter's (`3600`); a load makes the
saved game's letter the vault's (`604b:2ab4`).

The save path is the game's `SAVE` folder (`DQK.CFG`'s floppy letter 0,
`217e:0132` with 'S'): `--saves DIR` where given, else the assets' `SAVE`,
which the port reads but never writes (Save and Remove then log that
nothing was written).

The camp (`7368:046f`) with a party: the mode 0, the last mode the one it
was; its screen; the party's list where a member is selected; its buttons
on as `DS:3c3e`-`3c49` say (with a member selected all but Train and
Knight new Order where variable 0x30 is 0 and `DS:bd92` too, which no
routine found sets, and Add where six non-NPCs are
in; without, Create, Delete, Add, Load and Exit); Up and Down select the
member before or after. Load Saved Game (with a party not saved, "Game not
saved. Load anyway? ", `DS:7e93`; the party emptied): "load from where?
" Tdqk, the letters A-J of `SAVGAM?.QSV` there are, "load which game: "
with those alone (`DS:af15`'s list), "Loading...Please Wait", the saved
game read (`604b:2ab4`, `28f8`: `cok_adventure_restore`; `DS:b1ed` set,
so that the first block entered keeps its locals 0x97-0xc8, `6c70:0001`:
`keep_vars`), the game's speed the variable 0x12 (the shared waits' `game->speed`:
the original's waits read the variable, so the mirror is taken from it after the read;
`load_saved`), the frame's
colours where variable 0x3a changed, the map or the overland's big picture
again; none loaded, a new game's state (`64b2:04ec`). Dkk's (`604b:2bcf`)
lists the letters of `SAVGAM?.PTY` in Death Knights' save path (`DQK.CFG`'s,
the port's `--import`) and imports one: a new game's state, its party's
size (the byte at Death Knights' 0xa18, at most 8; `b1ed` clear again,
`64b2:04ec`), then `CHRDAT<letter><n>.GAM`
from 1 while there is one, each imported (`604b:0b82`) and joined unless an
NPC. Save Current Game
(`604b:3395`): "save which game: " A-J, "Saving...Please Wait", the file
and the vault as above, the game saved (`DS:ae5b`). Remove Character
(`7368:066f`): the member written to the save path (`604b:23a7`:
`<file name>.QCH`, `5e98:0044` taking out ` =+<>"[].*,?/\:;|`; where the file
is there, "Update <name>?", No asking "New file name:") and removed
(`177e:1e31`: the one before it selected, or after it where it was first),
the game not saved (`7368:061b`);
an NPC's removal (`7368:0ee9`) is not ported yet. Add Character
(`7368:08dc`): "add from where? " Tdqk (the save path's `.QCH`) or Dkk
(Death Knights' `.PC`, imported); the roster (`604b:054a`, `028a`: each
named by its record, by name; none, "No characters to load." in the bar for
the speed's time, `177e:153c`), "add a character: " over its list ("Add
Exit"; the list as `VERTICAL MENU`'s, `1bdb:1f25`), each member's name
after "* ": Add (or Return) joins the current one but a member's where
fewer than six others (eight with an NPC) are in and fewer than three
rangers ("too many rangers in party"), until Exit or six. Delete Character
(`7368:0c23`): the save path's roster ("No characters to delete."),
"delete a character: " ("Delete Exit"), "Delete <name> forever? " and
"Are you sure? " erasing its file. View Character
(`86b8:0b0c`): the sheet (`86b8:0000`: its window, `20b2:00a4`; name and
status, gender and age, alignment, race, class with a cleric's deity
before it, a mage's order before "Mage" and a knight's order after, the
levels, the experience centred, the scores with a strength's exceptional
one and a '*' where a score is not its base, the coins, hit points and
their most, AC, encumbrance, THAC0, movement and damage), then its menu
(Items, Spells, Trade, Deposit, Drop, Lay, Cure, Exit by `86b8:0b0c`'s
tests), Exit or Escape back; its readied weapon and armour lines
(`177e:03fc`, rows 21 and 22: the item's name, `src/queen_items.c`: an
item's words +0x29-+0x2b last to first, each a C string by id from
`DS:3f55`'s table, those not hidden by +0x33's bits, a count first and the
last word's plural where there is more than one, "* " where an effect 5 is
on a member and the item has a bonus, and a ready column ` Yes\t` or ` No\t `
in Items' list), and, as the menu is built again each time, Items
(`86b8:0f41`: the member's items listed under "Ready Item", a bar of Rdy,
Use, Trade, Deposit or Drop, Halve, Join, Sell and Id and Exit by the
mode and the character, Return taking the first; Rdy `86b8:1965` readies
an item or unreadies it, with its messages "It's Cursed!", "Wrong Class",
"already using <item>", "Your hands are full!", "Wrong Magical Order",
"Usable Only By Kender" and "Thief Level Too Low"; Trade `86b8:1c58`
through the picker `86b8:3ae5` ("Trade with whom?": the party's list, Down
or End, Up or Home, Select, Exit; each row a mouse area, a click making that
member the current one; it starts on the member last given to, which View
sets to the member viewed, `86b8:0b1d`) and "Overloaded" `86b8:2fbf`; Drop asks
"Lose it?" under "Your <item>will be gone forever"; Halve `86b8:1d13`;
Join `86b8:1fda`; all but a scroll's contents, which the port holds none
of, and the power an item puts on or takes off when it is readied
(`86b8:16f7`, step 4), Use (`86b8:2175`) and, in the vault or a shop,
Deposit, Sell and Id, which stop cited), the coins' Trade (`86b8:286a`, the
picker's "trade to?", "select type of coin " over the coins' box, a number
typed on the bar `86b8:2586`, "Overloaded" `86b8:27ac`) and Drop
(`86b8:2c32`), and the stats' recompute it starts with (`177e:0be2`). Spells
(`7a87:10cb`), Deposit (the vault's), Lay and Cure (`86b8:3200`, `3319`:
they heal and cure by effects, step 4) are not ported yet. Begin Adventuring
returns to main, the mode the last (4 for none), its screen
(`7368:07af`-`0804`): a game not loaded with small picture 9 not shown
(`DS:ae42`) the overland's frame on the overland in mode 3, else the
adventure's screen, the party's list after either; a game loaded, in mode
0 or 4 the adventure's screen and the list, in mode 3 the overland's frame. Exit From Game with a
party not saved asks "Game not saved. Exit anyway? " (No saves it).

Create Character (`66e5:2f95`, `src/queen_create.c`; its tables, `DS:0328`
(the race's classes), `0563` (the class's alignments), `03f3`, `04b7`, `02b8`,
`04fd` (the ages and limits), `180d` (the kits), `6359`, `16cd` and `27e9`,
are `src/queen_create_data.c`, which `tests/test_dark_queen.py` compares with
the data segment): the record starts with armour class 0x32, THAC0 0x28, size 1,
active, icon slot 0x0d; then the form (`66e5:2a6c`), the cleric's deity, the
rolls, the name, the icon and "Save NAME?". The form is the dialog toolkit
(`2aa6`: 32-byte items, a group of radio buttons the pointer or, with no mouse,
`66e5:2967`'s keys select: Left and Right the column, Up and Down the row but
through the gaps, Return the button; Done and Exit on the bar, D and E) of
race, class, alignment (lawful, neutral, chaotic by good, neutral, evil) and
gender, whose buttons the callbacks (`2581`, `26d4`, `26a1`, `26c2`, `26c7`,
`2323`, `2459`, `2532`, `230c`) disable and enable; ALWAYS.TLB's records 16, 17
and 18 are a button off, on and disabled and the text is drawn only where
it changed (the original redraws the items that changed). Done (`220a`) gives
the race's effects and size (`1839`), the experience 1,000,001 (500,000 for two
classes, 333,333 for three), the class's first levels (`0dbf`), the thief's
skills, THAC0 and the classes usable (`DS:6359`, `63f3`) and the saving
throws. A cleric (not a dwarf) picks a deity from the good or the neutral
gods ("Pick Diety", `36b0`; dwarves are Reorx's) and gets its gifts. The roll
(`1a90`) is the age (the race's dice, `DS:03f3`; a multi-class has the most of
its first class's), each score the best of six rolls of 3d6 + 1, the race's
adjustments, then `0f47`'s age change and limits, the class's least and an
exceptional strength (d100 + 1); the money (200 steel), the spells a day and the
known spells, the hit points (`86b8:380e` over the classes, divided by their
count) and the silent training (`queen_train_silently`). The generator is
`2102:0234` over `DS:ca0e`, as in the camp's Train, so a seed set in both
rolls the same (`dqk_create.py` writes the port's, logged as `seed:`, to the
original). The sheet (86b8:0000) and its bar (Done, Reroll Stats, Modify, Exit;
Modify is `queen_modify` with `creating` set) follow; then the name
(`193f`: "Character name: ", up to 15 characters, not empty or a digit first,
asked again; Escape ends the creation), the body's default (`66e5:0001`) and
the icons' picker (`0cf1`: 49 bodies of CBODY.TLB in a 7 by 7 grid of 24-pixel
cells, ready and action shown at the right; arrows wrap, D is Done, E or Escape
Exit, a click picks a cell), the question "Save NAME?" and, on Yes, the known
spells (`30d3`: a mage's first level and those its order gives), the kit
(`34e4`: the race's and class's items, the first weapon readied) and the file
(`604b:23a7`, `NAME.QCH` in the save path). The mouse cursor over the form is the
sword over a button and the shield elsewhere; back at the camp the original
keeps the shape the form last gave it until the pointer moves, where the port
chooses it at each draw (a difference of the cursor's shape only, which the
check masks at the pointer and which the next move ends).
Checked against the original by `dqk_create.py` ([checks](checks.md)).

Modify Character (`6a87:1293`, `src/queen_modify.c`; the camp's and Create's
Modify button's, `queen_modify(game, character, creating)`): only a character
of the experiences creation gives (1,000,001, 500,000 or 333,333; the original
also takes "HALIBUT" as a fourth word on its command line, which the port has
no way to give) is modified, else its name and " can't be modified." on the
bar; each readied item is unreadied first (`86b8:1965`: a cursed one says "It's
Cursed!" and stays; one with a power, `+0x38` 0x80, stops: `86b8:16f7`'s effects
are not ported); the sheet is drawn, the scores are set to their base (the
sheet's `*` gone), and the bar "Modify: Next Previous Add Sub Keep Exit" runs
over six fields in turn, the scores (STR to CHA), the name and the hit points
(DS:748a): Add (Right) and Sub (Left) change the lit one within its race's
limits (`DS:02b8`, 16 bytes a race: strength's by gender, its exceptional one
past 18 for a knight, fighter, paladin or ranger, the others' least and most)
and its class's least (`DS:04fd`, 6 a class), Next (Down, End) and Previous (Home,
Up) go to the next or the one before (strength's Previous is the hit points,
charisma's Next the name, or, in Create, the hit points; the hit points' Previous the
name or, in Create, charisma); wisdom keeps a cleric's first spell a day at 1;
a mountain dwarf paladin's charisma stays 17; constitution adds to or takes from
the hit points' most (the levels' least, `6a87:007e`, or most, `86b8:354b`),
and the hit points' Add and Sub within the same; a click on a score's row,
the hit points' (the right of row 3) or the name's takes it. The name is
edited in place (up to 15 characters, `x`-`z`, Left and Right wrapping,
Backspace, Delete; Return, Down or End on to the hit points, Home or Up to
charisma, unless another member has the name: "Already a NAME in party!");
the derived block (hit points, armour class, encumbrance, THAC0, movement,
damage) is recomputed (`86b8:065e`, `177e:0be2`) after each key. Exit
(`6a87:01e5`) gives the scores, the exceptional strength, the hit points' most
and the name back as they were and recomputes; Keep (`6a87:025e`) recomputes
the spells (`8bcc:01e7`, `cok_queen_spells`), makes the mage's spell book again
(`6a87:1599` forgets each mage's spell by `id / 8` and `id % 8`, a bit off from
the others' `(id - 1) / 8`, so the bit of the spell after it is cleared, then
`66e5:30d3`), the hit points' base from their most less the
levels' average constitution bonus, and keeps the scores as the base's.

The party's list (`5e22:023b`) is drawn from column 1 in the camp, else
0x11: "Name" and "AC" "HP" in light red on row 2, a row a member from 4,
the selected member's name in light cyan, others grey (black where it
cannot act, light magenta where it may train), its AC (0x3c less +0x18e)
ending at column 0x22 and hit points ending at 0x26, light cyan below their
most; not on the overland nor under a big picture shown outside it
(`DS:b256`). The status (`5e22:03f8`, `cok_drawing`'s `status`): the time
on row 15 ("h:mm AM", "12:mm PM", from variables 8, 7 and 6), the square
on row 13 unless variable 0x1a is set, the compass's needle, "camping" or
"searching" on row 14; `CALL 1` draws it too (`6c70:1685`).

Begin Adventuring (`5ebd:00ef`): the copy protection (`65cc:008b`, a word
from the journal; DARK-QUEEN.md's decision) logged and passed over, its two
random draws (`65cc:00cb` random(20), `00d8` random(3)) made, the adventure's
screen with the party's list and status, then the game
(`5f07:10cd`): the block variable 0x16's, or a new game's 4, entered
(`cok_adventure_enter`); then its loop: the adventure's commands
(`6fea:0365`, in 3D, mode 4: "Area Cast View Encamp Search Look Inv" on
the bar; Right, Down and Left turn the party and draw the view
(`8a8b:085a`) and status again; Up checks the step (`6fea:000a`: an edge
sets variable 0x21) and ends the command; Search turns variable 0x19's
bit 0; Look sets its bit 1, passes ten minutes and runs the location
vector again (searching or not as before); View the sheet; Area, Cast,
Inv and a member picked by a key are not ported yet; Encamp is below); the
after-move vector; the last square in variables 0x17 and 0x18; the step
(`6fea:0753`, `00c6`: where the way is open, the square ahead, wrapping at
the map's edges, a minute passed, ten searching; on the overland, with
variable 3 below 0xff, its travel, `6fea:01a7`: the mark's cell put back,
a square the way it faces within 0-37 by 0-14 into 0x25 and 0x26, the mark
again, twelve hours passed; then the small picture freed, `63ae:02b0`);
the view; the location
vector; each `NEWECL` entering its block (`5f07:0ff4`:
`cok_adventure_reenter`); a loaded game's locals are cleared again from
the block after its first (`DS:b1ed` cleared). The clock (`1396:044d`): variables 5-11 carried
by `DS:634b`'s 10, 10, 6, 24, 30, 12 and 100 (a hundred years age the
party), a day turning the moons (`1396:000e`: phases in 0x1fa-0x1fc, days
in 0x1fd-0x1ff, every 8, 1 and 6 days, the lamp drawn), then the party's
effects' minutes (`1396:019f`: one that runs out, `148f:017d`, stops the
run). The shipped saved game's party is at the start: block 4, Palanthas;
its palace and Leave take it to Caergoth, whose ruins (journal entry 65)
start variable 0xa0's count of steps: three on, huge shadows; six on,
dragons attack, whose fight (`SETUP MONSTERS`, `6c70:2db1`, and the battle, `5045:059d`) is
where the port stops (the encounter is ported; the battle is step 4's). The mouse's four areas over the view in the commands
(`1bdb:1aef`: turn left, step ahead, turn right and turn around, each a
click as its arrow key, with its cursor) are ported and checked
(`dqk_view_mouse.py`); not ported: the areas of the outside-3D commands
(`1bdb:1beb`) and the party list's control (`1bdb:0a33`).

The stats (`148f:1dfc`) are Dark Queen's own recompute (`cok_effects_ability`, with the
descriptor's `ability_routine`): strength from the base at +0x78 and the readied items' 18/xx
(power 3 and 5) with the strength effects 0x26, 0x71 and 0x0c merged as their helpers give
(a fighter, a class 1-4 at a level, gets the excess as exceptional strength, added to the
stored exceptional strength at each recompute); intelligence and wisdom stored at +0x7b and
+0x7d, 3 while the holder has 0x44; charisma from 0x0e; constitution's hit points by class
(`148f:1d0e`: classes 1 and 4 a level more, the top's level less one) and the effect 0x3e
(60 minutes, value 0xff, flag 1) at 20 and up, added again at each recompute as the original
does not check for one held. The removal (`148f:017d`) recomputes for 0x0e, 0x0c, 0x26 and 0x44
(0x71's leaves the stats as they are). The heal (`148f:25f7`) is `cok_effects_heal`: without
its flag a holder of 0xd7 is not healed. The temple's cures that call them (`7825:0045`,
`00b8`, `0193`, `0394`) still stop, cited, as does a removal of 0x3e, whose handler
(`78e4:0df6`) is not ported.

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
import's 0 at +0xc6, is none's. Dark Queen's (`dark_queen_character`:
see Dark Queen's characters above) is 0x19e bytes, its file's too, its
name a C string (`c_name`) and its known spells bits
(`KNOWN_SPELL_BITS`, a field of its own); no two of its fields share a
byte, and the bytes none holds are its ints' high bytes and those no
routine was found to read.

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
(no `-DNDEBUG`): one that fails stops `krynn` and `eclplay`. Where the
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
field only it has left out of Champions'), and Dark Queen's in
`dark_queen_character` (`src/rules_dark_queen.c`); and add it to
`expected`, `dkk_expected` and `dqk_expected` in `tests/test_layout.c`, which check every field's place
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
