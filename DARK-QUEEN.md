# The Dark Queen of Krynn: plan

The goal is for this port to play *The Dark Queen of Krynn* (1992, SSI,
version 1.1 of July 29, 1992) as well as *Champions of Krynn* and *Death
Knights of Krynn*. This file records what the games share, what differs, how
the port will hold the third game, and the order of the work. How the port
holds two games is in [DEATH-KNIGHTS.md](DEATH-KNIGHTS.md).

Dark Queen's files and Ghidra output go in `DQKAssets/` (not in git); `make
dqk-merged` builds its merged image, `build/DQK_FULL.EXE` (`tools/dqkmerge.py`:
its overlays are inside `DQK.EXE`). Addresses written `dqk 5f07:0cc5` are
Ghidra's numbering of that image (the map's segment + 0x1000), as the port's
`dkk ...` citations are.

## The short of it

Dark Queen is not a third version of the same program. SSI rewrote the engine
in C (Borland C++, large model, .RTLink overlays); Champions and Death Knights
are both Turbo Pascal. The game logic is mostly the same, re-coded. The files,
screen, sound and data segment are all new. Death Knights was a second
descriptor plus its own code where it differed. Dark Queen is closer to a
second port of every routine, sharing the logic where the logic is the same.

## What the games share and what differs

Measured on 2026-10-26 by matching every routine of the three merged images on
their decompiled C (the earlier byte-level matcher finds one identical routine
in all of Dark Queen), decoding every file in `DQKAssets/`, and running the
original in DOSBox-X.

**Engine code.** Dark Queen has 1,510 routines (388 KB of code; Death Knights
1,090 and 313 KB). Of Death Knights' 1,085 routines, 74 match a Dark Queen
routine exactly and 32 nearly (most of them trivial), 450 are the same
algorithm re-coded (similarity 0.5 or more), 278 are rewritten and 251 are
gone. 954 of Dark Queen's routines have no counterpart. Of the 662 routines
the port cites, 41 (6%) are identical or nearly so in Dark Queen, against
58% in Death Knights.

| Area | Median similarity of the cited routines |
| --- | --- |
| Effects | 0.65 |
| Combat, rounds, AI, arena, clouds | 0.54 |
| Casting | 0.52 |
| Camp | 0.45 |
| Treasure, items, shops | 0.42 |
| Menus, text, sheet (a new window library) | 0.41 |
| Files, config, start | 0.41 |
| ECL interpreter | 0.40 |
| Creation, training, roster | 0.32 |
| 3D view, pictures, icons | 0.24 |
| Sound | 0.24 |
| Mouse and input | 0.00 |

The match uses only the decompiled C's tokens; random pairs score a median of
0.18 (90th percentile 0.33), and the same matcher on Champions against Death
Knights gives figures close to the published ones above.

**Data segment.** Not a shifted copy of Death Knights' (42 paired addresses
give 38 different shifts): every `DS:` address is found again. Of the 59
tables the port copies, 20 are byte-identical, all small combat and geometry
ones (formations, moves, quadrants, start positions, step and line directions,
thief race and dexterity adjustments, cleric spells, Death Knights' cloud
cells). Every large table differs (names are C strings; spells, classes, THAC0
and levels, saving throws, spells per day, item words, stock, treasure).

**Files.**

| Files | In Dark Queen |
| --- | --- |
| Containers | No DAX. `.TLB` (pictures) and `.GLB` (data) are "HLIB" libraries, uncompressed, with optional id maps and nested libraries. |
| Pictures | 256 colours, a byte a pixel, stored plane by plane (pixel x in plane x%4); raw, raw with 0xff transparent, ANDed and XORed on (0x11), PackBits (0x80 doing nothing), transparent runs, and records drawn from the library's others at their places (0x19, `dqk 269b:0aec`); drawn at a place less the record's hotspot (its header's row and column, `269b:0a9c`). All 1,827 decode. |
| Palettes | Records of 8-bit RGB plus an EGA colour each, partial per library (combat icons 0x90-0xff, frame 0x10-0x1f, pictures 0x20-0xff), some with colour cycles (`261b:05be`); set at once, no fades, and flushed by changed range (`261b:0455`). |
| Title, ending | `TITLE.TLB` 10 scenes, `FINAL.TLB` 3: palettes, 320×200 backdrops, overlays and animation frames. |
| Pictures by id | PICA/B/C (67 pictures, 88×88 plus animation), BIGPIC (3 overland maps 304×120), SPRIT (32, 3 frames each), CPIC (190 combat icons, id and id+0x80), CBODY (98), COMSPR (46); no CHEAD. |
| Combat tiles | DUNGCOM 6 and WILDCOM 4 sets of 24×24 tiles, each with a palette. |
| 3D view | No WALLDEF and no 8×8 tile composition: 17 wall sets (8X8DB/C) of 47 pre-drawn perspective pieces each; BACK 20 backdrops, 88×88. FRAME, GEN, ALWAYS (icons, base palette), TOPVIEW (overhead cells). |
| ECL | `ECL.GLB`, 47 blocks (ids 1-67), without DAX's `88 13` header, loaded at 0x8000 with the same five vectors, operand types and 6-bit strings. Blocks reach 12,000 bytes (its buffer 0x2ee3, `64b2:0131`). Its handlers' table (`dqk 5f07:0d34`) decodes 44,780 instructions (0x00-0x41): Death Knights' operand counts but LOAD FILES with two (a map and a wall set) and 0x37 finding a member by name (Death Knights' LOAD PIECES, 3); CHECKPARTY does nothing, 0x3c gives treasure. Death Knights' table's 52 IF skips that disagreed were its own (no list skipped, SETUP MONSTERS 3): Dark Queen's skip loads what the handler does, lists whole (`6c70:1385`). |
| ECL variables | Bytes, as in Death Knights, but in a new flat range: 0x0001-0x0400 the 1 KB at far `DS:b20f` (the saved game's first), 0x0401-0x0800 the selected character's fields (`6c70:040d`; its name s[0401]); nothing at 0x4b00, 0x7a00, 0x7c00; some operands address the block itself (from 0x8000). |
| Maps | `GEO.GLB`, 20 maps of their own size (10×10 to 39×20 and 22×34): 8 bytes (height, width, the wall sets of three slots, three backdrops), then four planes of a byte a square (`8a8b:12e3`); not 1026 bytes. |
| Monsters | `MONCHA.GLB`, 98, each a big-endian size and PackBits (`10d7:0a1c`, read by `604b:257c`): the record, then counts of effects and items, effects of 5 bytes and items of 17; no MON*WIZ. |
| Characters | `SAVE\*.QCH`, 482-517 bytes: the record, 0x19e bytes, then 17-byte items and 9-byte effects (`dqk 604b:05fd`). Death Knights' `.PC` characters import (`604b:0b82`; listed by `604b:054a`). |
| Saved games | `SAVGAM?.QSV`, 7,965 bytes: 1 KB of variables, the place, modes and wall set, the characters as `.QCH` images, then the rest of the file from the item table's buffer, which no loader reads (`604b:31a7`). `VAULT?.DAT` 866 bytes: coins, a count of items and their 17 bytes each, padded to 50 so (`63ee:00df`). |
| Items | 17-byte entries everywhere; `ITEM.DAT` 254 of them (like Death Knights' ITEM0.DAX); `ITEMS.DAT` Champions' 2050-byte table without its header, contents changed. |
| `DQK.CFG` | Binary, 144 bytes (devices, Death Knights' save path, the game's path), in `DISK1\`. |
| Font | `GAME.FON`, 1,152 bytes: 8×12 for the 96 characters 0x20-0x7f, of which VGA draws rows 0, 1, 3, 4, 6, 8, 9 and 10 in an opaque cell of 8×8 (`22b7:0c96`); the earlier games draw text from 8X8D tiles. |
| Music | XMIDI songs, three a device in its own arrangement (AdLib, MT-32, Tandy, speaker), played by Miles AIL v2 drivers (`*.ADV`); `INSTR.AD` the AdLib timbres. |
| Sound effects | 13 effects: `SFXDQ.VOC` (4-bit Creative ADPCM, Sound Blaster) and `SOUNDS.GLB` (4-bit samples the speaker plays by pulse width). |

**Screen.** VGA's 320×200 in 256 colours, unchained (`65fa:06c3`, `22b7:0a0b`: two pages flipped); EGA and Tandy
(16 colours, each picture's colours quantised per register) are installable;
no CGA. The layout keeps the earlier games' grammar with new frame art, boxed
hotkey buttons and the bottom prompt bar.

**Input.** Every menu item has a hotkey letter; the mouse is read (INT 33h);
the joystick by port 201h.

**Copy protection.** BEGIN ADVENTURING asks a word from the journal
("JOURNAL, PAGE (11) / WHAT IS WORD # 5 / AFTER THE HEADING ..."); the answers
look like obfuscated strings in the EXE.

**The original in DOSBox-X.** It runs without installing: `DQKAssets/` copied
to `C:\DQK`, machine `svga_s3`, a Sound Blaster 16 at 220h IRQ 7, `dqk SB220
SI7 RB330`. Logo at 8 s, title at 26 s, then PLAY / DEMO / QUIT, the camp
menu, loading `SAVGAMA` with its party, up to the copy protection.

## The design

The same as Death Knights': one executable, the game chosen once at startup
into `game->rules`, and shared code that never asks which game it is. What
changes is how much goes in a game's own code.

- **Shared, with a Dark Queen descriptor:** the rules, combat, rounds, the AI,
  casting and effects (similarity above 0.5). Each shared routine is checked
  against its Dark Queen counterpart and cites both; where the logic differs,
  a function pointer, as in Death Knights.
- **Dark Queen's own, behind interfaces the earlier games already have:**
  - its archive reader (HLIB);
  - its pictures and palettes;
  - its 3D view (pre-drawn pieces);
  - its window library, menus and font;
  - its file and save formats;
  - its sound driver.

  Where the interface doesn't fit (a 256-colour palette, maps of any size,
  a variable-length character record), the interface grows for all three
  games, behaviour-neutral for the first two.
- **The ECL:** the same interpreter, with Dark Queen's opcode table, variable
  space and buffer size in the descriptor; changed handlers go in its table.

The port's checks stay the same for Champions and Death Knights at every part:
`make test`, `make sanitize`, the golden transcripts, and `make
dosbox-checks`/`make dkk-checks` at the end of a part.

## The order of the work

The order follows the agreed lessons: get the real game's data on screen early,
keep the gates light, and check against the original in DOSBox-X from the
start.

0. **Tools** (done, 2026-10-26: `dqk-box`): a Dark Queen profile in the DOSBox-X harness
   (`DQK.EXE`, its data segment from `DQK_FULL.EXE`, screenshots compared in
   RGB rather than 16 fixed colours, the copy protection answered or patched in
   the box only); the routine matcher and the HLIB reader moved from the
   scratchpad into `tools/`; `make dqk-checks`.
1. **Read Dark Queen headless** (done, 2026-10-26: `dqk-read`; MONCHA's
   records moved to step 2): `--game dark-queen`; the HLIB
   reader; the pictures, palettes and font; ECL.GLB with the larger buffer,
   its opcode table and variable space; maps of any size; its 3D view from
   pre-drawn pieces. `eclplay` shows its scripts, text and views, agreeing with
   DOSBox-X (`dqk_views`). The display grows a 256-entry palette with fades
   (behaviour-neutral for the 16-colour games). VGA only.
2. **Characters, saves and the start** (done, 2026-10-29: `dqk-chars`,
   `dqk-screens`, `dqk-saves`, `dqk-create`; View's Spells, Deposit, Lay, Cure,
   Use, Sell, Id, item powers, Train's spell picker and Encamp moved to step 4;
   the 3D view's mouse areas to step 3) (split like Death Knights' step 2):
   `.QCH`, `.QSV`, the vault, items, MONCHA; the binary `DQK.CFG`; the title
   and start menu, the window library and its menus; loading the shipped game
   and playing up to the first fight; importing Death Knights' characters;
   creation, Modify, training.
3. **The ECL's changed opcodes** (done, 2026-11-02: `dqk-eclbox`,
   `dqk-mouse3d`, `dqk-ecl-input`, `dqk-ecl-text`, `dqk-ecl-flow`,
   `dqk-ecl-misc`, `dqk-ecl-party`, `dqk-ecl-items`, `dqk-program0`; test
   blocks run in the original and the port, `dqk_ecl*.py`; left for step 4:
   LOAD MONSTER, SETUP MONSTERS, APPROACH, COMBAT, DAMAGE, SPELL, TREASURE
   ITEMS, STOCK TREASURE, ADD NPC, PROGRAM 3 and 9, PROGRAM 0's redraw in
   modes 1, 2, 6 and 10, the party list's mouse control and the mouse
   branches of YES NO and the bar menus): each handler of `dqk 5f07:0d34`
   checked against the port's, the 52 IF-skip differences explained, changed
   handlers ported.
4. **Combat and the rules** (the biggest step, split like Death Knights'
   step 3): the tables from its data segment; every shared routine checked
   against its Dark Queen counterpart; the battlefield, rounds, AI, casting,
   effects, clouds, treasure, shops, the temple, camp, levels.
5. **Sound:** an XMIDI sequencer with an AIL-style voice allocator and
   `INSTR.AD`'s timbres, on the existing OPL2 model (AdLib, and the Sound
   Blaster's music); the Sound Blaster's ADPCM effects; Tandy and the speaker
   on the same sequencer. MT-32 last or not at all (it needs an MT-32 emulator
   and its ROMs).
6. **EGA and Tandy** (optional): one 16-colour path quantised from each
   picture's RGB, as the original does.
7. **Checks against the original:** the full `make dqk-checks` gate, and
   `make dosbox-checks`/`make dkk-checks` unchanged.

Steps 1 and 2 run one after another; inside step 2, characters and the start
menu can run side by side. Step 5 can run beside step 3 or 4, since it
touches only the sound files.

## Size

Rough: each of steps 2 and 4 is about as large as all of Death Knights was,
since 94% of the cited routines have to be checked against new code rather
than confirmed identical, and steps 1 and 5 are new subsystems. Expect two to
three times Death Knights' work.

## Decisions

The user chose on 2026-10-26:

1. Shared logic in one executable, as in [The design](#the-design).
2. VGA only first; EGA and Tandy stay the optional step 6.
3. Sound: AdLib and the Sound Blaster (music and effects) first, then Tandy
   and the speaker; no MT-32.
4. Copy protection: logged and skipped, as for Champions' camp Save; the
   DOSBox-X box answers or patches it.

## Open findings

Things the step 4 reviews found that no part has taken yet, each with the part
that should pick it up. Planned stops (cited in the code) are not repeated
here; search for their `dqk SEG:OFF`.

From part 32 (spell band c, 2026-11-22):

- **Slow Poison's effect 0x16 is unblocked** (part 33 or 46). Its handler
  (`78e4:0337`) waited on the kill `148f:00d4`, which is already ported as
  attack.c's `cok_combat_kill`.
- **Cure Disease 0x27/0x6a** (part 46) needs `cure_disease` (`7fd8:0dc0`),
  static in `queen_band_c.c` today: export it. Heal outside a battle still
  stops at `7fd8:0e9f` (camp Heal).
- **Item powers** (part 50): in `86b8:16f7` powers 2, 4, 6, 7 and 8 do nothing
  and 3 and 5 only recompute strength; power 0 is ported (part 32e).
- **Berserk at a battle's end** (a fixture with confusion's 61-80 roll): 0x4d
  is removed with its handler at the battle's end; the original's order there
  (`5045:0001`, removal before or after the combat blocks are freed) is not
  measured, and it decides which side a berserk monster ends on.
- **Dispel Magic's freed-next guard** (`7fd8:1232`, an undefined stop) can be
  reached in real play: 0x38 appends its 0x19 at the end of the chain, so
  Dispel on a holder whose chain ends [.., 0x38, 0x19] stops. If a play-through
  meets it, measure the original before changing the stop.
- **Dispel Magic's clouds** (part 39): besides the cited `7fd8:12cc`, `10d9`
  reads `DS:c7e5` stale (Enlarge `02e8` writes it); `queen_band_c.c` keeps it
  as an unused local `stale`.
- **Event 9's list** lacks 0x3f 0x4c 0x4f 0x7b 0x9e 0xa5 0x5d 0x8d 0x8f 0x8a
  0x8e (parts 35-38): a Haste, Slow or other event-9 target holding one stops.
- **Saved game A's ZOMBIEs** carry the undead effects 0x8d 0x8e 0x8f 0x9e,
  which sit in the effect-added and saving-throw event lists (parts 37/38):
  until those are ported, a spell or save on a ZOMBIE stops, so the band c
  flows use the SHARKMEN and the party only.
- **Unreachable today, guard if that changes:** ROB with "all" on a readied
  hammer frees it twice in the original (`6c70:293c`); in the port it would
  remove the next item. The AI's weapon swap (`queen_ai.c`, `chosen`) could
  shift if a readied hammer before it were freed. Both need the hammer to
  outlive a battle, which it does not (0x17 is battle-only).
- **Repo tests only, no DOSBox-X flow yet:** Slow (eclplay agrees with the
  scoping probe), Reduce, Entangle, Confusion, a kept sleeper, Fear's failed
  save, Feeblemind's effect 0x44, berserk 0x4d, Cause Disease, Poison, Slay
  Living and Harm (computer-only), Remove Curse, the hammer's readying, Dispel
  Magic. Dispel's flow is cheap: `dqk_modify` can write BALTHIR's chain nodes
  as `effects_1_case` does, then C, C, E, Aim at his own cell, T (~40 lines).
