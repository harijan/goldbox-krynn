# Scripts

ECL scripts, how `eclplay` plays them, and the debug mode. Part of the
[developer notes](../DEVELOPERS.md).

## ECL scripts

`src/ecl.h` ports the interpreter for the ECL scripts in `ECL1.DAX`-`ECL3.DAX`
from `GAME.OVR`: operand decoding and variables from overlay `3775`, opcode
handlers from overlay `2fd3` (dispatch `2fd3:386d`). Addresses below are from
`START_FULL.EXE.c`; `DS:` offsets are in the data segment.

An ECL record's first two bytes are skipped and the rest loads at address
`0x8000` in a `0x1e00`-byte buffer (`3775:0361`). Row 24 is cleared and
shows `Loading...Please Wait` in light green while the record is read,
then cleared again (`67b5:0c7b`); a record shorter than two bytes the
original reads again for ever, where the port stops. It starts with five
one-operand instructions whose operands are the entry vectors (`3775:01e8`):
`DS:4b39` and `4b3b`, run by the adventure loop `2fd3:3c28` after moves and
for location events; `4b3d` and `4b3f`, run when camping (`2fd3:3403`); and
`4b41`, run when the block loads, always `0x8014`.

Each operand is a type byte and a low byte. Types 1 and 3 read the variable
at high:low and add a high byte, as do type 2, an immediate word, and type
`0x81`, a string variable. Type 0 is an immediate byte, and type `0x80` packs
low bytes of text six bits a character, four characters to three bytes, with
codes below `0x20` meaning `@`-`_`. Strings fill numbered slots from
`DS:7430`. The operand arrays persist between instructions, so a value can
read a stale high byte, as the original's can.

Variables are words (in Champions; Death Knights' are bytes, below):
`0x4b00`-`0x4eff`, `0x7a00`-`0x7bff` and `0x7c00`-`0x7fff` live in three
game data blocks, `0x8000`-`0x9dff` are bytes of the script
itself, and other addresses are special game state such as the map position
(`0xc04b`, `0xc04c`) and facing (`0xc04d`).

The special addresses are the descriptor's (`ecl.reads` and `ecl.stores`,
an address and a `cok_ecl_special` each; `cok_ecl_special_read`,
`_store`): Champions' read `0x4bea` and `0x33d` (the facing, 0-6),
`0xc04b`-`0xc04f` (the square, the facing in steps of 2 outside 3D areas
or halved in them, the wall ahead, the square's value) and `0xb1`, `0xfb`,
`0xfc` (values the host gives, `vm->value_b1` ...); stores to `0x3de`,
`0xb8`, `0xb9` (values the host takes), `0xc04b`-`0xc04d`, `0xc059` and
`0xc05f` (the backdrop's colours) set them (`3775:0fca`, `0e06`). Another
address outside the blocks reads 0 and takes nothing, and a special read
is never widened. A store to `0x4bfd` or `0x4bfe` marks the area changed
and one to `0x4be6` switches the mode, in both games (`3775:0e06`, `dkk
3c1e:0bc1`).

Code outside the VM's operands reads and writes the variables by name
(`cok_var` in `src/ecl.h`, 53 of them: `cok_ecl_var(vm, COK_VAR_DIFFICULTY)`,
`cok_ecl_var_at` for an element of a run the code indexes,
`cok_ecl_set_var(_at)`, `cok_ecl_add_var`), never by `vm->mem4b00[...]`:
where each lies, how wide and how many elements, is the descriptor's
(`ecl.vars`), Champions' a word at the address its name's comment gives.
They read and write the variable's cell itself, without a store's side
effects (the mode, the hooks, a character's field), as the original's
code does outside 3775's routines; a game's byte keeps the value's low
byte, and a variable of width 2 where each address holds a byte is a
little-endian word over two addresses. A name the game has not (address
0) stops at an assertion ("the game has no such variable"), but for the
three Death Knights has no such thing at all, which the code asks
`cok_ecl_has_var` about: the status line's turn (`0x4cff`), the portrait
(`0x7ee1`) and the view's own test (`COK_VAR_NO_VIEW`). A flag
(`cok_ecl_flag`) is set when it is not the game's off value: Champions'
"off the overland map" is `0x4c38` not 0, Death Knights' `0x4cf7` not 1,
where Champions' code tests `0x4c38` with "outside 3D areas" or mode 3;
where it tests `0x4c38` alone (after camp, `2fd3:3403`; in a mode-4
redraw, `6346:2c17`) the flag is `COK_VAR_NO_VIEW`, also `0x4c38`, which
Death Knights has not: its code draws the view there always (`3632:237e`,
`65cd:2db0`). What a block's start sets is the
descriptor's `start` (Champions: `0x7ee1` 0xff, `0x7ed2`, `0x7ed3`, `0x4be5`
0); the block's variables it clears unless kept (`0x4c00`-`0x4c1f`,
`0x7f79`-`0x7f82`) and `DIVIDE`'s remainder and `NEWECL`'s `0x4bf2` are
named too. Only whole blocks are still copied raw: the camp's save and
restore, startup's clearing, the saved game's file (Champions' format).

Many `0x7c00`-range addresses read and write fields of the selected
character's record, by the descriptor's table (`ecl.character`: offsets
first to last, how each reads and is written, a `cok_ecl_character_kind`,
and the field and element of the game's layout; the VM's `rules`, which
`cok_ecl_init` takes; see Games' "[The character record's layout](games.md#the-character-records-layout)"), read as
0 with none selected; `0x7f12` picks the ECL file for the next block.
Champions' (`3775:07d9`, `3775:0c00`), by address (its offsets):

| Address | Field | Read and written as |
| --- | --- | --- |
| `0x7c00` | `NAME` (`+0x00`) | a string, cut to 15 characters; a word store of 0 marks it cleared |
| `0x7c15`, `0x7c18`, `0x7c19` | `SCORE` intelligence, constitution, charisma (`+0x13`, `+0x19`, `+0x1b`) | bytes, read |
| `0x7c20`-`0x7c59` | `MEMORIZED` 0-0x39 (`+0x1e`-`+0x57`) | bytes, written |
| `0x7c61` | `KNIGHT_ORDER` (`+0x5c`) | a byte, read |
| `0x7c72`, `0x7c73` | `RACE`, `CLASS` (`+0x5a`, `+0x5b`) | signed bytes, read |
| `0x7c76` | `MAX_HIT_POINTS` (`+0x62`) | a byte, read |
| `0x7c9b` | `SAVING_THROW` 1 (`+0xd1`) | a byte, read |
| `0x7ca0` | `HIGHEST_LEVEL` (`+0xd6`) | a signed byte, read |
| `0x7ca5`-`0x7cac` | `THIEF_SKILL` 1-8 (`+0xdb`-`+0xe2`) | bytes, read |
| `0x7cb8` | `NPC` (`+0xe7`) | a byte; a value above 0xb2 is written less 0x32 |
| `0x7cbb`, `0x7cbd`, `0x7cbf`, `0x7cc1`, `0x7cc3` | `COINS` 0-4 (`+0xeb`-`+0xf4`) | words |
| `0x7ccc`, `0x7ccd`, `0x7cd0` | `CLASS_LEVEL` 2, 7, 4 (`+0xfb`, `+0x100`, `+0xfd`) | signed bytes, read |
| `0x7cd6`, `0x7cd8` | `GENDER`, `ALIGNMENT` (`+0x109`, `+0x10a`) | signed bytes, read |
| `0x7cf7`, `0x7cf9` | `MONSTER_EXPERIENCE` (`+0x130`), `MONSTER_HIT_POINT_EXPERIENCE` (`+0x132`) | a word, a byte |
| `0x7d00` | `STATUS` (`+0x188`) | read as 1 for 0 (okay), 6 for 3, 5 for 4, 4 for 5, 3 for 6, 7 for 7, 2 for 8, else 0; 0 when the selection is missing; writing 1 sets it 0 and `ACTIVE` (`+0x189`) 1 |
| `0x7d0c` | `HOSTILE`, `COMPUTER` (`+0x18a`, `+0x18b`) | read as 0x81 against the party, 0x80 for one the computer controls on its side, else 0; writing 0, 0x80 or 0x81 sets both so |
| `0x7d19` | `HIT_POINTS` (`+0x197`) | a byte |
| `0x7d1b` | `MOVEMENT` (`+0x198`) | a byte, read |
| `0x7ecf` | `SCORE` charisma | read through the reaction table, 0 from 26 |

`0x7cc9`, `0x7eb1` and `0x7eb4` are the host's (see Playing scripts),
`0x7f3e` reads its own word as a field, and the other addresses of the
range hold their own words.

`cok_ecl_run` runs control flow, arithmetic, comparisons, `RANDOM` (Turbo
Pascal's generator), `SAVE`, tables and `NEWECL` itself, and decodes every
other opcode's operands before handing it to the `opcode` hook. Those opcodes
drive pictures, text, menus, monsters, combat, items and characters; the
screen-side ones and the party's are ported in `src/adventure.h` (see
Playing scripts), the monsters' and encounters' in `src/monster.h` (see
[Monsters and encounters](adventure.md#monsters-and-encounters)); of their handlers only `PROGRAM 0`, the start
menu's training, is not. Comparisons order the first operand against the second,
but `AND` and `OR` set the flags for 0 against their result; `SUBTRAT` stores
the second minus the first; `DIVIDE` leaves its remainder in
`0x7f3f`. A failed `IF` skips the next instruction using its own operand
table (`3775:1e74`), which disagrees with the handlers for the menu and
`ON GOTO` forms, `ECL CLOCK`, `ADD NPC` and `PROTECTION`; the port keeps that
table, and no shipped script puts those opcodes after an `IF`.

`ecldump` lists every instruction reachable from a block's vectors through
jumps, calls, `ON GOTO` tables and both arms of each `IF`. All 22 blocks
decode, 13,547 instructions. The opcode names come from the game's own trace
table (`6d7e:0055`), including its spelling `SUBTRAT`.

What each opcode does, the operands it loads and those a failed `IF` skips
with it are the game's (the descriptor's `ecl`, see [Games](games.md#games)): the VM and the
hooks dispatch on an opcode's meaning (`cok_ecl_meaning`), a
`cok_ecl_opcode`, not on its byte, which stays `vm->opcode` (the traces
print it); Champions' every opcode means itself, so its table is the one
the VM held before (`tests/test_ecl.c` checks it opcode by opcode against
the old arrays). A list opcode loads a header of `header` operands whose
last counts the list (up to 4: Death Knights' `0x27`). `CALL`'s addresses
are the descriptor's too (`cok_ecl_call_of`).

Death Knights of Krynn (`--game death-knights`; its handlers `dkk
3632:0000`-`28d2`, dispatch `3632:2a54`, skips `3c1e:1b48`) keeps every
opcode's meaning but ten:

| Opcode | Champions' | Death Knights' (the port's name) | Port |
| --- | --- | --- | --- |
| `0x0c` | SETUP MONSTER, 3 operands | SETUP MONSTERS, 4 (`3632:038f`; a failed `IF` still skips 3): the fourth the ambush's directions | ported (see [Death Knights' encounter](adventure.md#death-knights-encounter)) |
| `0x1d` | PARTYSTRENGTH | PRESS KEY: a menu of "Press button or enter to continue" (`DS:088a`, `3632:1b5f`: `3c1e:1631` with Escape) | ported |
| `0x1f` | none (hangs) | JOURNAL N: "This is recorded as journal entry N." (`DS:2a79`), then PRESS KEY (`3632:279f`) | ported |
| `0x22` | PARTY SURPRISE | YES NO: a menu of Yes and No (`DS:31cb`, `6a69:2048` from Yes), `=` for Yes (`3632:1b9e`); row 24 left as it is | ported |
| `0x23` | SURPRISE | CLEAR EXIT: the text window cleared, what changed shown again (`3c1e:1dd9`), `EXIT` (`3632:2859`) | ported |
| `0x27` | TREASURE, 8 | TREASURE ITEMS: coins, then a list (a header of 4) of `ITEM0`'s items (`3632:1933`) | ported (see [Death Knights' treasure](treasure-shops.md#death-knights-treasure)) |
| `0x29` | ENCOUNTER MENU, 14 | NOTHING (`3632:28c9`) | ported |
| `0x2c` | PARLAY, 6 | MUSIC N (`3632:2878`): past 0x7f song N & 0x7f (`15dc:003d`), 1-6 a sound (`15dc:0000` with `DS:N+1`) | ported (logged `music: N`; see [Death Knights' songs](sound.md#death-knights-songs)) |
| `0x31` | SPRITE OFF | ASK MENU: "what do you do" (`DS:2bed`) where the text stands, then HORIZONTAL MENU (`3632:11fd` with 1) | ported |
| `0x3c` | PROTECTION (hangs) | STOCK: a shop's items from `ITEM0`'s lists (`3632:28d2`), then the shop | ported (see [Death Knights' shops](treasure-shops.md#death-knights-shops)) |

An opcode that stops ends the run with `COK_ECL_UNPORTED` and "not yet
ported: Death Knights of Krynn's opcode 0xNN, NAME (dkk SEG:OFF)".

Its menus are read by index (`menus.routine`, `COK_MENUS_INDEXED`).
HORIZONTAL MENU, ASK MENU, PRESS KEY and JOURNAL's go through `3c1e:1631`
(adventure.c's `horizontal_indexed`): the items marked as Champions' are
(`3c1e:152d`, `cok_menu_mark`, 3775:176e's marking), the items 0 to the
number of `~` less one, read by `6a69:0f1b` (`cok_menu_pick`) with the
keypad and the picture argument (`DS:db70`, a picture shown: PRESS KEY now
animates it too) until the index of an item; Escape gives 0 where the
caller says (PRESS KEY, JOURNAL), and reads on in HORIZONTAL MENU and ASK
MENU; a special key picks a character (`65cd:0959`, `580d:2bb2`) only off
mode 3 with `0x4cf7` at 1, and reads on (Champions' always pick). YES NO is
`6a69:2048` itself (`cok_camp_ask` from Yes: no keypad, so 4 and 6 move,
special keys are read past, logged as `menu: ` and `choice: Y` or `N`),
and leaves row 24 as it is (no `14e4:0b6b`). VERTICAL MENU (`3632:1068`,
`3c1e:171a`) lists its items with `6a69:1b82` (`cok_menu_indexed_rows`)
over `Select` with the items 0 and 1 and last 1, no Exit: its Next and
Prev take indexes from 2, so the one shown second has index 1, which is
not past last, and picks as Select does (`Select Next`: Next picks; with
`Select Next Prev`, Prev, at Next's index, pages down; PgDn and PgUp
page); Escape, and the scan codes 0x45 and 0, leave; the row is stored
whatever the key. A click off its list with `Select` alone gives 0xff,
which `6a69:1ea6` compares with an Exit index never set: the run stops
(`COK_ECL_UNDEFINED`). No Death Knights script has a VERTICAL MENU. The end of combat's results wait for
`Press button or enter to continue` (`DS:0868`, `3a37:09b7`'s end,
`treasure.c`'s `press_enter`) is one `6a69:0f1b` with the keypad and no
picture, its result unused, over the items the menu before left in
`DS:c120` (the routine does not call `6a69:00cb`; `cok_pointer.items`
keeps the last `cok_menu_pick`'s, `cok_menu_items_last` reads them, the
image's zeros before any): the line shows only where they start with 0;
its `P`, Enter, Escape and any special key end it, Space and other letters
are read past (Champions' loop ends on any of them).
`CALL`'s redraw is at `2dcb` (`3632:2294`), Champions' `2e10`. Its
variables are bytes (`3c1e:0d85`, `3c1e:0bc1`; the descriptor's
`variable_bytes` 1): an operand of type 0 or 1 reads and writes one byte,
one of type 2 and up (3, `0x80`) a little-endian word over the address and
the next (`cok_ecl_value`, `cok_ecl_store_to` and `cok_ecl_store_as` for an
operand's address; `cok_ecl_store` stores a byte); a special address takes
the value whatever the width, and a word at a store's last address has no
byte past it (the original reads on into what follows). All 25 of its
blocks decode, 14,781 instructions, 0 `IF`s skipping by another length:
`ecldump --game death-knights` agrees instruction for instruction with the
planning's patched listing but for `0x27`'s fourth header operand (which
that listing printed from a three-operand copy, stale) and 12 instructions
after a CLEAR EXIT, which ends the block's walk.

Its named variables are bytes at Champions' addresses (the counterparts
of the routines that use them read and write them there, the `0x7c00`
ones through its table of offsets at `DS:836`-`864`), but for: the clock,
seven bytes (`5b7d:0523`, not words over two addresses); the sky's colours,
`0x4bfe` below `0x80` and `0x4bfd` from it (`6c84:00a6`); games saved,
`0x4cf6` (`4381:3106`); "off the overland map", `0x4cf7` not 1 (where
Champions' routines test `0x4c38` with "outside 3D areas" or mode 3; where
they test it alone, its code has no test); no status line turn (its `0x4cff`
is turning undead's, `5340:119b`) and no portrait; and the names only
combat, treasure, monsters, shops and spells use, left out until they are
ported. Its block start clears only the encounters while resting
(`3c1e:01c1`). Its special addresses (`3c1e:0d85`, `0bc1`): `0x4bea`,
`0x33d` and `0xc04b`-`0xc04f` as Champions', `0x4cf8` always 1 (so ECL1
block 35, the vault, jumps to its fight at `8355` as the original does),
no host values at `0xb1`, `0xfb`, `0xfc`, and stores only above 49000
(`0xc04b`-`0xc04d`, `0xc059`, `0xc05f`). Its character fields (`3c1e:073a`
reads, `0a9a` writes), in its 216-byte record:

| Address | Field (offset) | Read and written as |
| --- | --- | --- |
| `0x7c00`-`0x7c0f` | `NAME` text from element 1 (`+0x01`) | read as many bytes as `SIZE` (`+0x29`) says, a word from 2; any store marks the name cleared |
| `0x7c14`-`0x7c19`, `0x7c1a` | `SCORE` 0-5 (`+0x11`, by 2), `STRENGTH_EXCEPTIONAL_BASE` (`+0x1d`) | bytes, read |
| `0x7c1b`-`0x7c64` | `MEMORIZED`, 74, in the WIZ record (`+0xc2` points to it) | bytes; not ported (the field is left out: a character stops at its assertion) |
| `0x7c65`-`0x7c6a`, `0x7c6b` | `SCORE_BASE` 0-5 (`+0x10`), `STRENGTH_EXCEPTIONAL` (`+0x1c`) | bytes, read |
| `0x7c71`, `0x7c72`, `0x7c74`, `0x7c76` | `BASE_THAC0` (`+0x1f`), `RACE` (`+0x20`, signed), `AGE` (`+0x26`, a word), `MAX_HIT_POINTS` (`+0x28`) | read |
| `0x7c9a`-`0x7c9e`, `0x7c9f` | `SAVING_THROW` 0-4 (`+0x2a`), `MOVEMENT` (`+0xd7`) | bytes, read |
| `0x7ca5`-`0x7cac` | `THIEF_SKILL` 1-8 (`+0x32`) | bytes, read |
| `0x7cb8` | `NPC` (`+0x3e`) | a byte, written as it is |
| `0x7cbb`, `0x7cbc`, `0x7cbf` | `MAGIC_ORDER` (`+0x24`), `DEITY` (`+0x23`, signed), `KNIGHT_ORDER` (`+0x22`) | read |
| `0x7cc3`-`0x7cc8` | `COINS` 4-6 (`+0x3f`), each at two addresses | words |
| `0x7cd3`, `0x7cd6`, `0x7cd8` | `MAGIC_RESISTANCE` (`+0xc1`), `GENDER`, `ALIGNMENT` (`+0x55`, `+0x56`, signed) | read |
| `0x7ceb` | `CLASS_LEVEL` above 0 as bits: classes 5, 0, 6, 2, 7, -, 3, 4 (`class_bits`) | read |
| `0x7d00` | `ACTIVE` (`+0xc8`) | read as 1, or 0x80 if it cannot act; writing 1 sets `STATUS` (`+0xc7`) 0 and `ACTIVE` 1 |
| `0x7d0c` | `HOSTILE`, `COMPUTER` (`+0xc9`, `+0xca`) | written as Champions' (not read) |
| `0x7d0e`-`0x7d10` | `THAC0`, `AC`, `AC_BEHIND` (`+0xcb`-`+0xcd`) | bytes, read |
| `0x7d19`, `0x7d1a` | `HIT_POINTS` (`+0xd6`), `MOVEMENT` (`+0xd7`) | a byte; read |
| `0x7eb1`, `0x7eb4` | the host's | |
| `0x7f12` | the ECL file | written (picks the next block's file); read as the variable |

Each handler that keeps its meaning, against Champions' (`dkk 3632`'s
against `2fd3`'s; the VM's own as `ecl.c` has Champions'):

| Opcode | dkk | Against Champions' | Port |
| --- | --- | --- | --- |
| EXIT, GOTO, GOSUB, COMPARE, RANDOM, SAVE, RETURN, the IFs, ON GOTO, ON GOSUB, PRINT, PRINTCLEAR, PRINT RETURN, ECL CLOCK, CLEAR BOX, DUMP | `0000`, `009b`, `00b8`, `00cd`, `01fb`, `025d`, `0b99`, `0cdc`, `189e`, `0afd`, `202d`, `1ff8`, `2056`, `2487` | the same (EXIT's frame redrawn is the game's: step 2) | as Champions' |
| ADD, SUBTRAT, DIVIDE, MULTIPLY, AND, OR, INPUT NUMBER, INPUT STRING | `014a`, `0f3f`, `0a3b`, `0a85` | stored as wide as the destination's type; `DIVIDE`'s remainder a byte at `0x7f3f` | named variables, byte variables |
| GETTABLE | `0fb2` | read as wide as the third operand's type | ported |
| SAVE TABLE | `1018` | stored as wide as the value's (first) operand's type | ported (D1 took the second's) |
| COMPARE AND | `0bed` | a pair compared as strings where its first is one | `compare_and_strings` |
| VERTICAL MENU | `1068` | the store's width; the list by index (`3c1e:171a`: `6a69:1b82` over `Select`, `DS:31d6`, the items 0 and 1, last 1, no Exit) | the list by index (see below) |
| HORIZONTAL MENU | `11fd` (0) | no single item's case (white, Enter, respelled), colour 10, the picture animated whenever shown; the items by index (`3c1e:1631`) | `menu_single_item`; the items by index (see below) |
| PICTURE | `096c` | no portrait (`0x7ee1`), no `DS:884b`; overland map 0x70 | has the variable not; `view.overland_picture` |
| DELAY | `1c90` | 600 milliseconds, not the speed's | `delay_ms` |
| NEWECL, APPROACH, CLEARMONSTERS, LOAD FILES, LOAD PIECES, CALL | `0d52`, `0916`, `1376`, `0daa`, `2294` | variables bytes; CLEARMONSTERS clears 12 bytes of its table (`DS:be58`), not 28; the wall slots, `CALL`'s redraw (D1); `CALL`'s sound reads `DS:db5a`, which no ECL address stores | as Champions' |
| LOAD CHARACTER, CHECKPARTY, ROB, FIND ITEM, WHO, ADD NPC, FIND SPECIAL, DESTROY ITEMS | `02af`, `1452`, `1a76`, `1bdd`, `20a9`, `210f`, `24b6`, `250a` | the record's offsets only (the layout's) | as Champions' (no party: nothing) |
| SPELL | `2177` | the memorized spells in the WIZ record, 74 (Champions' 58), of those who can act; no trace | as Champions' without a party; a character stops at `MEMORIZED` |
| LOAD MONSTER | `0472` | the record's, the difficulty a byte, other monster files | ported (see [Death Knights' encounter](adventure.md#death-knights-encounter)) |
| COMBAT | `16ec` | a branch on `0x4d00` first (`3948:000e`, the bank vault's menu: see [Party](party.md#party)), then the shop, the temple; other routines | ported (the shop and the temple: see [Death Knights' shops](treasure-shops.md#death-knights-shops)) |
| DAMAGE | `1ca2` | a save's bonus bits 0-3 (Champions' 0-4: bit 4 counted), half the damage for a save made with bit 4 (Champions' all of it), its key `DS:0868`; its saving throw (`638c:10f8`) | the treasure section's; the saving throw ported (see [Death Knights' fight](combat.md#death-knights-fight)) |
| PROGRAM | `23dd` | its start menu (`49cb:010b`, see [The start menu](start-menu.md#the-start-menu)); after it the screen redrawn on the map unless the menu ended the game (`DS:8c5b`), and the party marked on the map (`6997:0000`); its camp (`237e`) | `start.program_marks`; the game ended, `new_game` |
| ADD EP | `25f1` | only those who can act, before the division; experience at `+0x62`; a level flag (`65cd:38c7`); its words (`DS:1485`, `2a64`, `2a3b`) | the treasure section's |

Where the original would misbehave, the port stops with a status instead:
opcode `0x1f` and opcodes past `ADD EP` have no handler in the dispatch
(`2fd3:386d`), and `PROTECTION`'s (`2fd3:3516`) returns at once, so none of
them advances and the original runs it again for ever;
division by zero is Turbo Pascal runtime error 200, and a failed `NEWECL` load
retries forever. Code addresses outside the buffer, more than 64 operands or
32 string operands, and `GOSUB` nesting past 256 are also errors. Variable
strings stop at 255 characters or the end of their store, and reads the
original leaves unset return 0.

## Playing scripts

`eclplay [options] ASSETS BLOCK` runs an ECL block on a 320×200 adventure
screen without a display, as `2fd3:3b47` enters a block: the load vector,
then the after-move and location vectors, starting over when `NEWECL`
switches blocks; `--game NAME` names the game whose files ASSETS holds
(see [Games](games.md#games)): with `death-knights` it runs Death Knights' blocks (their
songs logged as `music: N`, and played with `--wav`; an opcode or a read not ported yet
ends the run, saying which), not its title, start menu or saved games. It prints text as it is printed (`print:`), menus with
their `~` marks (`menu:`, `list:` and `item:`), the choices made
(`choice:`) and input read (`input:`), and the name and operand values of
each opcode that is not ported, in brackets. A spell effect the port cannot
carry out ends the run with its reason (see [Spell effects](party.md#spell-effects)). `--play` then
runs the adventure loop (see [Adventure loop](adventure.md#adventure-loop)), which adds the party's square
and facing after each step or turn (`at: X,Y,DIR`), its place on the
overland map after each step there (`overland: X,Y,DIR`), the overhead map
going on and off (`area: on`, `area: off`; see [Overhead map](adventure.md#overhead-map)) and the
commands that are not ported (`unported:`); `--helm` plays as a game
started with `Helm`, which lifts `Area`'s `Not Here` and enables the Gods
cheat; `--combat gods` implies it. `--keys` types keys (`\r` Enter, `\e`
Escape, `\b` Backspace, `\<`, `\>`, `\^` and `\v` the arrows, `\@X` Alt and
letter `X`, `\cX` Ctrl and letter `X` (`\cD`, `\cZ`, `\cC` the debug keys,
see Debug mode), `\#HH` the extended key of scan code `HH`, in hex: `\@q`
and `\#10` are Alt-Q; and the keypad's keys as the BIOS takes them,
`cok_keymap_event`: `\n` the NumLock key, turning NumLock off and on again
and typing nothing, `\dC` the keypad's key `C` (`0`-`9`, `.`), its digit
with NumLock on and else the cursor key's code (nothing for 5), `\aN.` Alt
held over the keypad's digits `N`, typing their character code as a byte on
Alt's release, nothing for 0; `cok` presses those keys through SDL's
events, outside a group typed ahead, where they are typed as their codes);
when they run out the run stops. `\k` presses the next key while the party
rests, which the rest loop sees (`1614:03c2`), or as a battle is set up,
which drops it (`1614:0479`). Keys are otherwise typed one at a time as the
game reads them, but `\[KEYS\]` types KEYS ahead: all at once into the
keyboard's buffer (15 keystrokes; more are lost), at the game's next wait
(a delay or a poll of `KeyPressed`) or key read, whichever comes first,
where they wait to be read or are thrown away by the game's flushes
(`1614:045c`, see [Playing](front-end.md#playing)), each flush that drops some logged as `flush: N
keys typed ahead dropped`; a group typed ahead makes `KeyPressed` true
while it lasts. `\wN.` holds back what follows until N more of the game's
waits have passed (a key read ends it at once), so that a key arrives in
the middle of a wait: the title waits polling and sleeping each hundredth,
two waits a hundredth (`\w600.\[x\]` ends it a second into its second
picture), and a rest polls once a step. From startup the first wait is the
note to Tandy users' `Delay(2000)` (see [Startup and the setup](front-end.md#startup-and-the-setup)), where a
group typed ahead at the start arrives, for the title's flush to drop.
`eclplay` has no delays; it counts the calls to the delay hook as the
window's front end does, and passes over `cok`'s `\pN.` (a pause in real
time). `--saves DIR` is where Save writes and the roster is kept (see [The
start menu](start-menu.md#the-start-menu)); without it, saving logs an error. `--import DIR` is where a
game that imports another's characters finds them (Death Knights' Add and
Load from Champions), `--saves` without it. `--effect
M,ID,MINUTES,VALUE` (hex, repeatable) gives party member M (1 the first) an
effect, its handler run when it is removed, as a saved game's `.SFX` would
hold it, for tests of the timers and of effects in a battle. A spell list
logs its rows as `item:` and `heading:`, and quitting to DOS ends the run
with `(quit to DOS in block N)`. Cast and View run from the commands and
the camp, their lists logged as `list:` and `item:`. Monsters loaded, the
encounter's sprite and close-up and the money robbed log as `monster:`, a
battle's setup, where each combatant stands, and the end of a fight as
`combat:` (see [Monsters and encounters](adventure.md#monsters-and-encounters) and [The battlefield](combat.md#the-battlefield)), each round's
order of turns and each turn as `round:` and `turn:`, and effects ending in
a battle as `effect:` (see [The rounds](combat.md#the-rounds)), the lines of an attack not said in
the side panel as `attack:` (see [Attacks, movement and death](combat-turns.md#attacks-movement-and-death)), each cast in
a battle as `cast:` (see [Spells in combat](combat-spells.md#spells-in-combat)), the coins and items `TREASURE`
adds as `treasure:` (see [Treasure and the end of combat](treasure-shops.md#treasure-and-the-end-of-combat)), and what is paid
and appraised in shops and the temple as `shop:` (see [Shops and the
temple](treasure-shops.md#shops-and-the-temple)), the characters `ADD NPC` adds and `DUMP` removes as `party:` (see
[Party](party.md#party)), `CALL [b203]`'s sounds as `sound:` and `CALL [c01e]`'s step as
`at:`, and the debug mode's switching, traces and View as `debug:` and the
lines it writes to its file as `debug.txt:` (see Debug mode). `COMBAT`'s
battle runs its rounds, the computer playing the monsters and the party's
NPCs (see [The computer's turns](combat-turns.md#the-computers-turns)) and the player's turns reading the keys
(see [The player's commands](combat-turns.md#the-players-commands)), or, without `--keys`, passing, unless
`--combat won`, `fled` or `lost` decides it instead, `gods` has the
player's turns start with Alt-X, the original's cheat (see [The rounds](combat.md#the-rounds)),
`melee` has every turn attack the nearest enemy it can (see [Attacks,
movement and death](combat-turns.md#attacks-movement-and-death)), `auto` has the player press Alt-Q, the computer then
playing every turn, or `pass` has every turn pass, the computer's too. Keys
that run out in a battle end the run there, `(out of keys in block N at
ADDR)`, as anywhere else: the battle's end and its treasure are not
reached. `--combat-map FILE` writes each battle's map and combatants to
`FILE` (see [The battlefield](combat.md#the-battlefield)). A fight ends with the results and the
treasure menu, which read keys; with no party at all the monsters rejoice
and the run ends. `--test-party N` adds N (1-8) characters made up for
testing, as a saved game's are added: a fighter, a cleric of Mishakal, a
White mage and a thief in turn, human, of level 1, their weapons and armour
readied, 20 steel each, the cleric's Cure Light Wounds and the mage's
Detect Magic memorized, one cell in combat (`SIZE`, `+0xcf`, 1), their stats
computed as a loaded character's are; the seventh, GWEN, has Charm Monsters
memorized too and a Wand of Magic Missiles readied with 5 charges; the
eighth, HOLT, is a mage too, with Stinking Cloud memorized instead.
`--party SAVE` adds the characters of a saved game to the party (see
[Party](party.md#party)); `--character FILE`, up to 8 times, adds the character whose record
is in `FILE` (a saved game's `CHRDATA1.SAV`, the roster's `BOB.WHO`,
Death Knights' `CHRDATA1.GAM` or `BOB.PC`), with its other files beside
it, after `--party`'s, as they are added (`cok_adventure_add_characters`:
read, joined, counted, their icons loaded, the first selected); unlike
`--party` and `--load` it works for a game not yet playable. `--load SAVE`
loads the whole saved game first; then `BLOCK`
may be left out to resume where it was saved, as `2fd3:3c28` starts the
game, running only that block's load vector before the first command (see
[Adventure loop](adventure.md#adventure-loop)). A block named is entered at once, a test entry and not the
original's resume: its load, after-move and location vectors run, the first
at the stack depth of `2fd3:3c28`'s own, as `2fd3:3b47` runs them, which
also makes the mode the last mode; with `--load` too, the saved block's
after-move and location vectors run before any command, as the original's
resume never does. `--at` with `--load` and no block places the party, the
square's byte (`DS:6d89`) read from the map there, before the load vector
runs. WHO prints the character
picked (`who:`). `--shots DIR` saves `DIR/NNN.bmp` each time the game waits
for a key and once each battle is set up, `--screen FILE` the final screen,
in the display mode `KRYNN.CFG`'s first line names or `--display ega`,
`tandy` or `cga` (CGA in its four colours; see [Display modes](front-end.md#display-modes)). `--start
ADDR` runs from a code address instead, `--vector N` one vector, `--at
X,Y,DIR` places the party, `--set ADDR=VALUE` sets variables (in hex),
`--file N` picks the ECL file (by default the first that holds the block),
`--still` loads only the first frame of each picture, `--seed N` starts
Turbo Pascal's `Random` from N (in decimal; by default 0), and `--trace`
lists each instruction. `--start-menu ASSETS`, with no block, plays the
game as `START.EXE` does, from startup: the setup if there is no
`KRYNN.CFG` (in `--config FILE`, `--saves DIR` or ASSETS; it writes it in
the first of the two given, see [Startup and the setup](front-end.md#startup-and-the-setup)) and the note to
Tandy users, logged as `crt:` rows and saved by `--shots` at 640×200, then
the title (a key pressed with `\k` while it waits ends it), its menu, where
`\t` lets the time run out and plays the demonstration, then the start menu
and the adventure, game after game (see [The start menu](start-menu.md#the-start-menu)); `--woof` starts it
as `START Woof` does, without the title, and in any run lets the debug keys
work (see Debug mode). Characters, the roster and saved games go in
`--saves DIR`. The run ends `(out of keys in the setup)`, `(out of keys at
the title)`, `(out of keys in the start menu)` or as other runs do. `--wav
FILE` renders the sound to FILE as the game's time passes (see [Sound](sound.md#sound)), on
the device `KRYNN.CFG` names, or `--sound tandy`, `adlib`, `speaker` or `off` (which
need `--wav`); a run that does not start from the title installs the
effects driver first, as main has by then (`1000:0160`); `--sound-calls`
prints the game's calls to the sound driver as `call:` lines among the
rest. `--mouse` and `--joystick` give the machine a mouse driver and a
joystick, which `\m` and `\j` in the keys click and move, and `--input
mouse`, `joystick` or `none` picks in place of `KRYNN.CFG`'s third line
(see [The mouse and the joystick](front-end.md#the-mouse-and-the-joystick)). For example, `./build/eclplay --start 899b --keys '\r2\r' COKAssets 48`
answers the guards at the gates of Gargath with the second item of a list.

`src/adventure.h` holds the screen side of the opcodes, from overlay `2fd3`:

| Opcode | Handler | Port |
| --- | --- | --- |
| `PRINT`, `PRINTCLEAR` | `2fd3:0acf` | `cok_text_wrap` in cells 1-38 by 17-22, colour 10, typed, leaving the text delay off; a number prints in decimal |
| `HORIZONTAL MENU` | `2fd3:116d` | `cok_menu_horizontal`, colours 15 and 10, or all 15 for one item |
| `VERTICAL MENU` | `2fd3:0f9d` | prompt as `PRINTCLEAR` (typed only while the text delay is on), then `cok_menu_list` below it |
| `INPUT NUMBER`, `INPUT STRING` | `2fd3:0a11`, `2fd3:0a57` | `cok_text_number`; `cok_text_input`, an empty line stored as a space |
| `PICTURE` | `2fd3:0914` | below 0x70, `PIC<file>.DAX` at cell 3, 3; else `BIGPIC<file>.DAX` at 1, 1 in its frame; 0xff restores the view |
| `CLEAR BOX` | `2fd3:3063` | the adventure frame (`1128:0242`) and the first picture frame |
| `DELAY` | `2fd3:2c33` | the `delay` hook, speed × 100 ms (`521:0b4b`) |
| `LOAD FILES`, `LOAD PIECES` | `2fd3:0cf4` | the map and wall sets of the 3D view (see below) |
| `LOAD CHARACTER` | `2fd3:02e9` | select a character by position until the script exits (see [Party](party.md#party)) |
| `ADD EP` | `2fd3:36dc` | experience for the selected character or the party |
| `WHO` | `2fd3:30b6` | pick the selected character from the party list |
| `DAMAGE` | `2fd3:2c80` | damage by attacks or saving throws, which can kill the party |
| `CALL` | `2fd3:329b` | a routine by its address: the view after an encounter, a sound, a step forward, the wall ahead, a frame of the small picture (see below) |
| `ADD NPC`, `DUMP` | `2fd3:311c`, `2fd3:351b` | a monster joins the party as an NPC; the selected character leaves it (see [Party](party.md#party)) |
| `FIND ITEM`, `FIND SPECIAL` | `2fd3:2b7e`, `2fd3:354f` | whether any record has an item of a type, or the selected character an effect |
| `SPELL` | `2fd3:318b` | which character has a spell memorized, and where |
| `ECL CLOCK` | `2fd3:3005` | passes the time (`57e4:0549`), count first, then the unit |

A one-item menu reading `PRESS BUTTON OR RETURN TO CONTINUE.` is shown as
`PRESS <ENTER>/<RETURN> TO CONTINUE`, with no full stop (the 34 characters
of `2fd3:1146`), and Enter picks it, as in the original. Menu items are
joined as `~ITEM` separated by spaces, cut to 50
characters. Small pictures are delta collections whose groups after the first
are animation frames, each preceded by its delay, which the menus that
animate the picture (`67b5:03e2`) count in tenths of a second (see
[Playing](front-end.md#playing));
with animation off (`DS:4b4f`) only the first is loaded. A picture
loaded stays loaded for the same name (`PIC` or `SPRIT`) and record
(`DS:6dee`, `6e00`), whatever the ECL file, until the slot is freed.
With animation on, loading one (`6961:00e4`, pictures and sprites alike)
clears row 24 and shows `Loading...Please Wait` in light green while it
reads, and clears the row after the keyboard's flush, so that whatever
row 24 held is gone; with it off row 24 is left alone. A record that is
not there says `PIC not found` on row 24 in yellow and waits for a key
(`1521:096c`, as `head not found`), with no flush or clearing after; the
slot is left empty but holds that name and record, so loading it again
does nothing. A file that is not there stops the port, where the
original asks for it (`Couldn't find PIC1.dax. Check install.` on row 24,
`169c:02a7`, with `DS:5884` 0 outside the monster loader) and waits for
a key and looks again until it is there. The
game speed
(`DS:4b38`) defaults to 4. Big picture 0x79, the overland map, marks the
party on it (`4877:0005`) and does not count as a big picture shown (see
[The overland map](adventure.md#the-overland-map)).

With var `0x7ee1` not 0xff, `PICTURE` shows a portrait instead
(`3775:0538`), as the close-up of `SETUP MONSTER` does: the head of
`0x7ee1` over a body of the picture's number. The shops of ECL2 block 50
and ECL3 block 80 show their shopkeepers so (heads 68 and 69 over body
68), and in 3D areas a shop's or the temple's redraw shows the portrait
last shown again (`DS:4b55`, `4b56`; see [Shops and the temple](treasure-shops.md#shops-and-the-temple)). The view
is not marked as replaced (`DS:884b`). `6961:05b9` loads the head from
`HEAD<file>.DAX` and the body from `BODY<file>.DAX` (`DS:6de5`, `6dea`),
opaque, the file's number cut to one digit, unless the record wanted is
the one held (`DS:6de4`, `6de9`) or 0xff; `6961:06bd` then draws the head
at cell 3, 3 and the body at 3, 8 (`6961:000a` with 0, `127f:10e7`:
straight to the screen, unclipped), nothing for a piece not loaded. A
record that is not there leaves no piece and says `head not found`, for
the body too, on row 24 in yellow, and waits for a key; it counts as held.
None is held at startup (0xff, `3e99:0477`), and each step (`475c:0e77`)
and combat setup (`3cb2:1c58`) free the pieces and hold none again. A file
that is not there makes the original ask for its disk, and the run fails:
the game has no `HEAD1.DAX` or `BODY1.DAX`, but no script of ECL1 sets
`0x7ee1`, and its shops and temples all open outside 3D areas, so only a
script made up or a run such as `eclplay --set 4be6=1` at one of them
reaches it. The port keeps these quirks:

- The portrait last shown is head 0 over body 0 until one is shown: a shop
  or temple redrawn in a 3D area before any portrait says `head not found`
  twice (the shipped scripts show their shopkeeper first).
- A piece held stays held whatever the ECL file: after a `NEWECL` to
  another file the same head shows until a step.

The screen's redraw for the mode (`6346:2c17`), whose other modes are
described with them (the adventure, camp, shops, treasure), draws in the
party menu (mode 0) the cleared frame of `1128:0000` alone, and nothing in
combat (mode 5).

`CALL` takes its operand's address whatever its type (`3775:06cc`), and
less 0x7fff picks:

- 0x2e10, which scripts call after an encounter (see [Monsters and
  encounters](adventure.md#monsters-and-encounters));
- 0xb203, a sound: 11 (`DS:1e5e`) if `0x3de` (`DS:8834`, which only
  scripts write) holds 10, else 10 (`DS:1e5c`); scripts set it to 5;
- 0xc01e, a step forward (`3775:1bed`): the party's square moves a square
  the way it faces, wrapping within 0-15 (a step down from 0 or below
  gives 15, a step up from 15 or above gives 0, as signed bytes), not at
  all for a facing other than 0, 2, 4 and 6; then the square byte and the
  wall ahead are worked out again and the view marked as changed
  (`DS:8850`), so that the next `CALL [2e10]` draws it. For another facing, which only the overland map or a
  saved game gives, the wall ahead (`69ea:06a2`) is its unset local
  `[bp-1]`: the byte that `1bed`'s call to `69ea:07a5` pushed, AX's high
  byte, which still holds the address less 0x7fff, so 0x40; the emulator
  confirms it with any stack. Off the map of a block with no squares there
  (`DS:8846` 0 or 0x50) it is 0 first. ECL2 block 48 and ECL3 blocks 97
  and 99 use it.
- 0xc018, the wall ahead, outside 3D areas only; there, for a facing
  other than 0, 2, 4 and 6, the unset byte is one `3775:0032` left on the
  stack, which depends on the opcodes before, and the port stops with
  `COK_ECL_UNDEFINED`. No script uses it.
- 0x6803 draws the small picture's current frame (`DS:6da3`) at cell 3,
  3 (`6961:000a`), moves to the next, back to the first after the last,
  and waits speed × 100 ms (`1521:0b4b`). ECL2 block 57 animates a
  close-up and a picture so. Loading a picture starts from its first
  frame, but `PICTURE` of the one loaded keeps the current frame
  (`6961:00e4` returns at once, as it goes by the name and record, not
  the ECL file), though it draws the first. Where the slot was freed
  (`6961:0537`, as at startup, each step and combat setup) and nothing
  loaded since, or a record was not there, the frame is 0, and entry 0
  is the far pointer `DS:6da2`:`6da0`: segment 0 (the count and frame)
  and as offset the segment of the sky picture loaded at startup outside
  CGA mode (`DS:6d9e`), so the original draws from low memory; the port
  stops with `COK_ECL_UNDEFINED`. The menus that animate the small
  picture also move the frame while they wait (see [Playing](front-end.md#playing)).
- any other address, nothing.

`FIND ITEM type` (`2fd3:2b7e`) sets the comparison flags to `=` if any
record in the list has an item of that type (`+0x2e`), else `<>`, the
others false; ECL2 block 67 asks for its tomb's sword (type 63) so, and
ECL3 block 98 for another. `FIND SPECIAL id` (`2fd3:354f`) does the same
for an effect of the selected character (`6346:2447`); with none selected
the original reads through NULL and the port stops; no script uses it.
`SPELL spell where who` (`2fd3:318b`) stores, for the first record in the
list with the spell among its 58 spell bytes (`MEMORIZED`, `+0x1e`-`+0x57`),
the byte's index in `where` and the record's position in `who`; with none,
0xff and the position of the last record (0 for none). ECL1 block 32's trap
asks for Find Traps (22) and forgets it. The trace it then prints
(`6d7e:093e`, `spell`, the index, the position and the spell) shows only
with the debug flag (see Debug mode). `ECL CLOCK count unit` (`2fd3:3005`) passes the time
(`57e4:0549`); no script uses it.

A differential test ran the original's handlers in an 8086 emulator
against the port: `FIND ITEM`, `ECL CLOCK`, `ADD NPC` (with `4b6d:18fc`,
`161b`, `1989`, `66c2:0433` and `6346:0d20` as they are), `SPELL`, `DUMP`
(with `4def:3b0a`), `FIND SPECIAL` and `CALL` at 0xb203, 0xc01e, 0xc018,
0x6803 and other addresses, on random parties of up to twelve monster
records with their items and effects, random selections, party sizes,
squares, facings, maps and flags, comparing every record, item and effect
in the list, the selection, `DS:43bf`, `0x7f3e`, the flags, the square,
the wall ahead, the square byte, the view's flag, the variables stored,
the clock, the sounds, delays and icon slots loaded and freed; `CALL
[6803]` one to six times after a `PIC` or `SPRIT` record of any file was
loaded (`6961:00e4`, animation on or off) from a random frame, in a
third of the pictures' cases after `PICTURE` of the same, half of those
with another ECL file picked, comparing
the frame, the screen (noise before) and the delays; and the portrait
(`3775:0538`) after up to three portraits or steps' frees, the file
changing between, or the shop's redraw of it (`6346:2c17` in mode 1 in a
3D area), comparing the records held, the portrait last shown, the screen
above row 24 and the prompts, with the picture routines of `127f` run as
they are in Tandy mode. Of 12,000 cases, the 10,607 the port carries out
agreed: 6,332 opcodes (1,396 `DUMP`, 1,342 `CALL`, 818 `ADD NPC`, 755
`ECL CLOCK`, 742 `FIND ITEM`, 689 `SPELL`, 590 `FIND SPECIAL`), 2,405
frames of pictures and sprites and 1,870 portraits and redraws. Both
refused 1,218: monster ids not in the file, which quit to DOS (610),
portraits of ECL1, which has no `HEAD1.DAX` or `BODY1.DAX` (500), and
`CALL [6803]` with no picture loaded, which draws from low memory (108).
The port alone refused 175: `FIND SPECIAL` with none selected (121) and
`ADD NPC` with the party full and none selected (17), which read and
write through NULL, and `CALL [c018]` facing other than a side (37),
whose byte of the stack the emulator, which clears it, cannot know. Running `CALL [c018]` and
`[c01e]` with `3775:0032` decoding the operand and the stack filled with
any byte showed that byte for `c018` and 0x40 for `c01e`. Four
mutations of the port (the wall after a step, a frame's wrap, the body's
row and `SPELL`'s position) each made from 20 to 300 of 3,000 cases
differ; `PICTURE` starting the frames over, as the port did before, 15
of 2,000, and reloading a picture for another ECL file 28 of 3,000. It is not part of the repository.

## Debug mode

The original has a debug mode, `src/debug.h` in the port. Its flag,
`DS:4b51`, clear at startup (`3e99:055b`), is toggled by Ctrl-D, which the
key reader (`1614:025b`, which every key the game reads goes through but
text input's and the flushes', see [Playing](front-end.md#playing)) takes, with Ctrl-Z and
Ctrl-C, only when the first argument is `Woof` (`ParamStr(1)` against
`1614:024c`, exact case): `START Woof`, or `START Woof Helm` with the
Gods' cheat too; `cok --woof` and `eclplay --woof`. No other argument or
build is involved; the readers of the flag are in every run, but nothing
else sets it. The key is passed on as read in every case, so a menu reads
it too; a scan code of 3 (Ctrl-2's) is Ctrl-C as well.

| Key | What it does |
| --- | --- |
| Ctrl-D (4) | The flag on: `DEBUG.TXT` assigned and made anew (`1a46:04f7`, `056c`); off: closed (`05c1`) |
| Ctrl-Z (0x1a) | With the flag set, for each record of the list (`DS:609a`) and each of its effects (`+0xe3`, linked at `+5`), the line `who: NAME  sp#: ID` (`1614:007b`) |
| Ctrl-C (3) | Quit to DOS (`1614:0000`): the sound stopped, `DEBUG.TXT` closed if the flag is set, `Halt`; not DOS's break, which Crt's `ReadKey` does not see |

`DEBUG.TXT` (the Text file at `DS:8864`, which the game names
`debug.txt` and DOS upper-cases, as the port names the saved games) is
written as Turbo Pascal
writes a text file on a disk, checked in the emulator: lines end CR LF and
go into its 128-byte buffer, written out when it fills and when the file
is closed; the game reads no I/O result (`{$I-}`): where the file cannot
be made (`C:\COK` read-only in DOSBox-X, `e_debug_readonly.py`) Turbo
Pascal's `InOutRes` holds 2, then 103 once Ctrl-D closes it, and nothing
is skipped for it: pictures load and a Save writes every file, with no
runtime error, as in the port. The end of a game
(`3e99:0977`, `3e99:0843`) clears the flag and leaves the file open, so
that what its buffer holds is never written and the next Ctrl-D makes it
anew. The original writes it in the current directory; the port in the
saves directory (`--saves`), or nowhere without one (`eclplay` without
`--saves`; the file counts as open all the same), and logs each line as
`debug.txt:`; Ctrl-D logs `debug: on` and `debug: off`, Ctrl-C `quit: to
DOS`. In `--keys`, `\cD`, `\cZ` and `\cC` type Ctrl-D, Ctrl-Z and
Ctrl-C (`\cX` is Ctrl and any letter, 1-26; the raw bytes 4, 0x1a and 3
do as well), which `cok` sends as Ctrl and the letter. Besides Ctrl-Z's lines, loading a tile set
(`6e22:0050`; the wall sets' from `LOAD FILES` and `LOAD PIECES`, slots
1-3) writes `slotnum = N`. At the end of a run the port closes the file
if the flag is set, as quitting would.

While the flag is set:

- The trace (`6d7e:093e`, `cok_debug_trace`): row 0 cleared (the frame's
  top and its moons), three numbers drawn there as `A , B , C` in yellow,
  a text (cut to 40) on row 24 in yellow (`1521:096c`), a key read
  (through `1614:025b`, so Ctrl-D there turns the mode off), and row 0
  cleared again; logged as `debug: A , B , C | TEXT`. The ECL loop
  (`2fd3:3add`) traces each opcode once its handler returns, with zeros,
  by the name `6d7e:024f` gives (the opcode last fetched, `DS:72e7`: a
  nested run's, or the one a failed `IF` skipped; the trace table's names,
  `IF = ` with a space after it, and `BULLSHIT N` for opcodes it does not
  name, which hang before they are traced); `SPELL` traces `spell` with
  the index, the position and the spell (`2fd3:3292`); a battle's setup
  traces each record of the list by name with its status (`+0x188`
  `STATUS`) and whether it can act (`+0x189` `ACTIVE`) (`3cb2:1e13`); effect
  0x07's handler traces `bad news` first (`3f44:0226`). In the demonstration
  the trace's key read returns at once unless a key is waiting, as every key
  read there does.
- The status line has `*` after the time (`6346:2f4c`), and the combat
  menu `*` after `Done` (`3995:0aa5`).
- Items offers `View` first (`View Ready`, `546c:187b`): with an item
  picked, `546c:12be` clears the frame (`1128:0000`) and lists in light
  green, from row 1, `itemptr` (its type, `+0x2e`), `namenum(1)`-`(3)`
  (`+0x2f`-`+0x31`), `plus`, `plussave`, `ready`, `identified`,
  `cursed` (`+0x32`-`+0x36`), `value` (the word at `+0x3a`),
  `special(1)`-`(3)` (`+0x3c`-`+0x3e`), and from its type's row of
  `DS:5886` `missile type` (byte 14), `max range` (12) and `item type`
  (0), their values at column 20, then `press a key` on row 24; the list
  and its header are drawn again after. A type above 128 is read past
  the table (from `DS:6096`); the port stops (`COK_ECL_UNDEFINED`).
  `View` makes the menu longer: in a shop it can fill the 40 characters,
  leaving no room for `Exit` (Escape still leaves).

A differential test ran the original routines in the 8086 emulator
against the port (the draws compared as the port's primitives draw them,
the files byte for byte): `1614:025b`'s keys with the real Turbo Pascal
file routines and `ParamStr` on a PSP (with `Woof`, `Woof Helm`, `woof`,
`WOOF`, `Woofy`, none and others; Ctrl-D, Ctrl-Z, Ctrl-C, other keys and
the end of a game's reset, random parties and effects, files past one
buffer), the trace with the flag on and off, `546c:12be` on random items,
the status line `6346:2d75` and the combat menu's text (`3995:08d5`) with
the flag on and off, the Items menu `546c:17f9` with View, `SPELL`
(`2fd3:318b`) with its trace and the ECL loop's after it, and
`6d7e:024f`'s name for every opcode: 3,000 random cases of each and the
256 names, all agreeing (among them 1,467 Ctrl-Ds turning the flag on
and 719 off, 683 Ctrl-Zs writing, 318 Ctrl-Cs quitting, 1,126 resets, 181
files past one buffer; 2,254 traces with the flag set; 1,514 Views).
