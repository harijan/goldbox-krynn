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
| PIC, SPRIT, BIGPIC, CPIC, COMSPR, CHEAD, CBODY | Same formats; more files (PIC1-7, SPRIT1-4). |
| GEO | Same 1026-byte maps. |
| WALLDEF, 8X8D | Five sets, chosen by wall id, not by ECL file (dkk `6d21:0ec5`); WALLDEF records hold 3 sets of 0x30c; 8X8D tile sets of 256 tiles. |
| BACK1-5, SKYGRND | New 3D-view backdrops (88×88, and a night sky) in place of SKY. |
| CURSOR, TILES, RANDCOM, HEAD/BODY | Gone; the overland cursor is 8X8D1 record 204. |
| ECL | Same opcodes 0x00-0x41; 0x0c, 0x1d, 0x1f, 0x22, 0x23, 0x27, 0x29, 0x2c, 0x31 and 0x3c take other operands or mean other things. Variables are bytes (dkk `3c1e:0d85`), where Champions' are words. |
| Characters | 216-byte `.GAM` record, uncompressed, with the spells in a 220-byte `.WIZ`; items (`.JNK`) and effects (`.EFF`) as Champions' `.STF` and `.SFX`. Roster files `NAME.PC`. |
| Saved games | `SAVGAM?.PTY`, 2913 bytes: byte variables, 4 wall sets. A bank vault, `VAULT?.DAT`. |
| Monsters | MON1 only, 216-byte records, with their spells in `MON1WIZ.DAX`. |
| ITEMS | Same; ITEM0.DAX is a new table of 17-byte entries, not yet decoded. |
| DKK.CFG | A fifth line, the path of Champions' saved games. |
| MUSIC.DAX | One driver for every device and songs in a new format ("GLBR"); music plays during the game through ECL 0x2c. SOUND.DAX is not read. |

**Rules.** The same classes, races, deities and alignments. Levels go to 18.
Nine reversed cleric spells are gone and about twenty spells are new (Harm,
Heal, Restoration, Barkskin, Death Spell, Disintegrate, Power Word Stun, Mind
Blank and others), and spell ids are reused for other spells. New: a
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
| R1 | The descriptor, `game->rules`; Champions' file names, save extensions, titles and region digits in it; `--game` | |
| R2a | Character field accessors: party, items, magic, sheet | |
| R2b | The same: attack, round, ai, cast, effect, combat, arena, cloud | |
| R2c | The same: create, modify, train, camp, shop, treasure, icon, command, roster | |
| R3 | The data-segment tables in the descriptor; named lookups for `ds_byte` arithmetic | |
| R2d | The same as R2a: ecl, adventure, monster, eclplay | |
| R5 | Spell names, spell and effect handler tables, spell count, levels, moons and deities in the descriptor | |
| R4 | ECL opcode tables in the descriptor; the variable memory behind get and set | |
| R6 | A test descriptor with the character fields moved, to find any raw offset left | |

R2c and R3 can run side by side, as can R2d and R5. R4 follows R2d, since both
change `ecl.c` and `adventure.c` throughout.

## After the refactor

1. Read Death Knights headless: its descriptor, the ECL operand table,
   images by name, wall sets by wall id, 256-tile sets and backdrops, so
   `eclplay` shows its scripts and 3D view.
2. Characters and saved games, and importing Champions characters.
3. The rules: level 18, the spells and effects, leadership, aging, the
   temple, doors, treasure and ITEM0, the vault.
4. Music: the MUSIC.DAX driver and its songs. Until then Death Knights plays
   silent.
5. Checks against the original: tests against `death-START_FULL.EXE` and a
   DOSBox-X harness for Death Knights.
