# Death Knights of Krynn: plan

The goal is for this port to play *Death Knights of Krynn* (1991, SSI) as well
as *Champions of Krynn*. This file records what the two games share, what
differs, how the port will hold both, and the order of the work. How the
port itself works is in [DEVELOPERS.md](DEVELOPERS.md).

Death Knights' files and Ghidra output go in `DKKAssets/` (not in git);
`make death-merged` builds its merged image, `build/death-START_FULL.EXE`
(see Disassembly image in DEVELOPERS.md). Addresses below without a game
named are Champions' (`START_FULL.EXE`); `dkk 46b2:10d7` is Death Knights'.

## The design

One executable plays both games. At startup it chooses the game once, from
`--game` or (later) from the files in the assets directory, and points
`game->rules` at that game's descriptor (`cok_rules`). Everything that
differs between the games lives in the descriptors:

- file names and file sets,
- the character record's layout, read through named field accessors,
- the tables copied from each executable's data segment,
- the ECL opcode table (operand counts, IF skips) and the variable memory,
- the tables keyed by spell and effect id,
- the routines that really differ, as function pointers, one per feature.

Shared code reads those and never asks which game it is. There are no
`#ifdef`s per game and no second copy of the engine: about half of Death
Knights' engine is Champions' code unchanged, and a second copy would need
every fix twice.

Character records stay raw bytes, as the files and the original hold them;
the accessors find a field's offset in the descriptor. The ECL reads
character fields by raw offset (0x7c00-0x7fff), and the saved files must be
byte-exact in each game's own layout, so a native C struct would need a
conversion at every one of those edges.

## What the games share and what differs

Measured on 2026-10-17 by matching every routine of both merged images (the
decompiled C and `ndisasm` of the bytes, normalised, then hashed and
compared), running the port's tools on `DKKAssets/`, and surveying `src/`.

**Engine code.** Of Champions' 1,009 routines, 489 are identical in Death
Knights (about 100 of those differing only in record offsets), 153 change
little, 211 change substantially, 51 are rewritten and 105 have no
counterpart. Death Knights adds 181 routines of its own (about 17% of its
code). Of the 608 routines the port cites, 233 are identical, 118 change
little, 176 change substantially, 35 are rewritten and 46 are gone. Combat,
the battlefield, rounds, casting and the AI are mostly identical; doors,
treasure, the temple, creation and camp's Alter changed most.

**Data segment.** Every `DS:` offset the port cites has moved, by a
constant within each block (for example `DS:609a` → `a2c6`, `6a32` →
`bd7e`). 21 of the 43 tables the port copies are byte-identical; every large
one differs: levels go to 18 (THAC0 rows of 19, longer XP and thief-skill
tables), the spell table is reworked and turning undead changed.

**Files.**

| Files | In Death Knights |
| --- | --- |
| DAX archives | Same container; all 50 decompress (631 records). |
| PIC, SPRIT, BIGPIC, CPIC, COMSPR, CHEAD, CBODY | Same formats; more files (PIC1-7, SPRIT1-4), of which PIC4-7 and SPRIT4 are chosen by the picture's id, not by the ECL file. COMSPR holds icons 0-21. |
| GEO | Same 1026-byte maps. |
| WALLDEF, 8X8D | Seven wall ids in five files (1→1, 2→4, 3-4→2, 5-6→3, 7→5), chosen by wall id, not by ECL file (dkk `6d21:0ec5`), and loaded from slot 0; WALLDEF records hold 3 sets of 0x30c; 8X8D tile sets of 256 tiles (a header's frame count 0xff means 256). Only tile set 0 and the frame's set are drawn from; startup does not load set 0. |
| BACK1-5, SKYGRND | New 3D-view backdrops in place of SKY: BACK (88×88) chosen by the same wall id, behind squares of 0x80 and up; SKYGRND record 2 the ground by day, record 1 the night sky after hour 13. Both, and the overland cursor, are drawn without a mask. |
| CURSOR, TILES, RANDCOM, HEAD/BODY | Gone; the overland cursor is 8X8D1 record 204. |
| ECL | Same opcodes 0x00-0x41; 0x0c, 0x1d, 0x1f, 0x22, 0x23, 0x27, 0x29, 0x2c, 0x31 and 0x3c take other operands or mean other things (0x1d is PRESS KEY and 0x23 CLEAR EXIT, the most used; 0x31 is ASK MENU). SETUP MONSTERS' fourth operand is the directions an ambush comes from (DS:c4cc). CALL's redraw is at 2dcb; variable 0x4cf8 always reads 1, stores at or below 49000 do nothing; most variables are at Champions' addresses, but "off the overland map" is 0x4cf7 == 1 (Champions 0x4c38), the save count is 0x4cf6 (0x4c3c), the two sky colours are swapped and 0x4cff is turning undead's; there is no table of opcode names (no debug overlay). Variables are bytes (dkk `3c1e:0d85`, `0bc1`), where Champions' are words. |
| Characters | 216-byte `.GAM` record, uncompressed, with the spells in a 220-byte `.WIZ`; items (`.JNK`) and effects (`.EFF`) as Champions' `.STF` and `.SFX`. Roster files `NAME.PC`. |
| Saved games | `SAVGAM?.PTY`, 2913 bytes: byte variables, 4 wall sets. A bank vault, `VAULT?.DAT`. |
| Monsters | MON1 and CPIC1 only, 216-byte records, with their spells in `MON1WIZ.DAX`. |
| ITEMS | Same; ITEM0.DAX is a new table of 17-byte entries, not yet decoded. |
| DKK.CFG | A fifth line, the path of Champions' saved games. |
| MUSIC.DAX | One driver for every device and songs in a new format ("GLBR"); music plays during the game through ECL 0x2c. SOUND.DAX is not read. |

**Rules.** The same classes, races, deities and alignments. Levels go to 18.
Nine reversed cleric spells are gone and about twenty spells are new (Harm,
Heal, Restoration, Barkskin, Death Spell, Disintegrate, Power Word Stun, Mind
Blank and others), and spell ids are reused for other spells (1-0x72; the spell lists go to level 8). The moons are drawn with other tiles (0x100 + 0x15, 0x0d, 0x11 + phase; Champions 0x114 + 10, 6, 14). New: a
leadership ability at the start of combat, aging and the Elixir of Youth, the
vault, and importing Champions characters (dkk `46b2:10d7` converts the
409-byte record field by field). No copy protection; no mounted combat.

## The refactor

First the port is reshaped on Champions alone, so that Death Knights can be
added as a second descriptor and its own code. Each part changes no
behaviour: `make test`, `make sanitize`, every `eclplay` transcript and the
DOSBox-X checks (`make dosbox-checks`) give the same results before and
after.

| Part | What | Status |
| --- | --- | --- |
| R1 | The descriptor, `game->rules`; Champions' file names, save extensions, titles and region digits in it; `--game` | Done |
| R2a | Character field accessors: party, items, magic, sheet | Done |
| R2b | The same: attack, round, ai, cast, effect, combat, arena, cloud | Done |
| R2c | The same: create, modify, train, camp, shop, treasure, icon, command, roster | Done |
| R3 | The data-segment tables in the descriptor; named lookups for `ds_byte` arithmetic | Done |
| R2d | The same as R2a: ecl, adventure, monster, eclplay | Done |
| D1 | Read Death Knights headless (step 1 below), to try the descriptor on the real game before the rest; the ECL's opcode table and variable width moved into the descriptor | Done |
| R5 | Spell names; the spell table (dkk DS:6a80, 16 bytes an id); spell and effect handler tables by each game's own ids; the spell count (ids 1-0x72 in Death Knights; the memorized spells, the ECL's SPELL bound, are 73); MON1WIZ as a fourth monster file; levels to 18, moons and deities in the descriptor. Of Death Knights' 235 spell and effect ids, 127 share Champions' handlers; the rest, and its cast entry, wait for step 3 | Done |
| R4 | The variables by name and width (the ~190 raw reads and writes of the variable memory outside `ecl.c`); the ECL's character fields (0x7c00-0x7fff, dkk `3c1e:073a`/`0a9a`) and special addresses per game; the handlers that kept their meaning but changed (HORIZONTAL MENU, DELAY, COMPARE AND, GETTABLE and SAVE TABLE's widths; PROGRAM, SPELL, ADD EP, COMBAT, DAMAGE, LOAD MONSTER stop until their parts); SETUP MONSTERS' fourth operand | Done |

R2c and R3 ran side by side, as did R3 and R2d. After R2d, Death Knights is
read headless first (D1), so that R4 and R5 are shaped by what it needs
rather than by guesses; it takes the place of a planned test descriptor with
the character fields moved (R6), which the real game's layout does better.

## After the refactor

1. Read Death Knights headless (D1, moved before R4 and R5): its descriptor, the ECL operand table,
   images by name, wall sets by wall id, 256-tile sets and backdrops, so
   `eclplay` shows its scripts and 3D view.
2. Characters and saved games, and importing Champions characters; its
   screens' frames and 35 icon slots (done: `frame`); characters, their files,
   monsters and the import (done: `chars`); saved games, the vault and the
   start menu, playing up to the first fight (done: `saves`). Left: creation
   and Modify Character (dkk `49cb:05c7`, `25d4`, rewritten).
3. Combat and the rules, in ten parts: (1) `encounter`, into and out of
   every fight: SETUP MONSTERS with its ambush, LOAD MONSTER, COMBAT,
   leadership (a party knight leading NPC allies), its own battlefield
   builders and placement, the end of combat with may-train, no former
   classes or fighter levels (done); (2) `fight`, the rounds, the
   computer's turns, melee, turning undead (done, with the saving throw,
   caster level and the computer's spell choice); (3) `commands`, the player's
   two-row menu, Move, aiming (done); (3b) `items`, View and the items
   menu (Ready, Use, Trade, Halve, Drop, Deposit, Sell, Id; rewritten; done); (4) `casting`, the cast
   entry, magic resistance, band A (done: 65 spell ids cast); (5)
   `effects`, the changed effect handlers, level drain (done; band C
   effects 02 04 15 22 30 3a left for 10); (6) `treasure`,
   TREASURE ITEMS and ITEM0, the pool, the vault's menu, DAMAGE, ADD EP,
   STOCK, ROB (done); (7) shops and the temple (done; Raise Dead,
   Remove Curse and Restoration wait for their spells); (8) `spells`, the changed spells (done: band B, 19 routines);
   (9) `levels`, levels to 18, Train, Knight Change Order, aging (also after a
   battle), Memorize's 8 levels (done; camp Fix left, a rewrite); (10) the new spells (done) and the rewritten
   clouds (clouds done). Death Knights has no locked doors: nothing to port there.
4. Music: the MUSIC.DAX driver and its songs. Until then Death Knights plays
   silent.
5. Checks against the original: tests against `death-START_FULL.EXE` and a
   DOSBox-X harness for Death Knights.
