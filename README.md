# Champions of Krynn Linux port

The native tools are a C17 DAX archive reader, decompressor, image exporter,
picture and text compositor, ECL script interpreter and disassembler, and a
headless script player that shows the scripts' text, menus, pictures, 3D
view and party on the adventure screen. The original DOS executable,
decompiler output, and game data are reference inputs, kept in `Assets/`;
saved games from the original, if any, go in `SAVE/`. These tools do not yet
run the game.

## Build and verify

Run from the project directory, with a C compiler and Make installed:

```sh
make
./build/daxcheck Assets/TITLE.DAX
./build/daxcheck --list Assets/TITLE.DAX
./build/daximages Assets/TITLE.DAX build/title
./build/daximages --entry 0 --frame 0 Assets/PIC1.DAX build/portrait
./build/daxcompose build/demo.bmp fill 0 0 40 200 1 \
    draw Assets/PIC1.DAX 0 4 1 1 sprite Assets/SPRIT1.DAX 0 0 4 12 \
    text Assets/8X8D1.DAX 0 1 23 14 0 "Champions of Krynn"
./build/ecldump --block 16 Assets/ECL1.DAX
./build/ecldump --summary Assets/ECL*.DAX
./build/eclplay --keys '\r\r\r\r\r' --shots build/shots Assets 16
./build/eclplay --play --set 4be6=1 --keys '\r\rm\^\r\^\^' Assets 32
./build/eclplay --load SAVE/SAVGAMA.DAT --play --keys '\r\r\r' Assets
./build/eclplay --play --set 4be6=1 --saves build --keys '\rerharsan\e' Assets 32
make test
make sanitize
```

`daxcheck` reads archives without modifying them. It validates every directory
entry and decompresses every record, checking the decoded byte count. It exits
nonzero if any file or record fails. `--list` prints metadata for each validated
record; directory entry numbers are zero-based. Duplicate IDs are retained.
`make test` includes synthetic malformed inputs, all supplied DAX archives,
export checks for all 26 supported graphics archives (2,363 images), and
tests of the picture, text, menu, 3D view, party and spell effect routines,
the adventure loop, the camp, casting spells, the character sheet with its
items, monsters and encounters, the battlefield, treasure and the end of
combat, and the shops and the temple,
including PIC delta decoding on `PIC1.DAX` and the game font in
`8X8D1.DAX`, and plays the opening scripts, the view of Throtl and, with
a party made up for testing (`eclplay --test-party`), its fights, a walk
through it and a camp. It builds `build/START_FULL.EXE` (see Disassembly
image) to check the original's tables that the port uses. With the
original's saved games in `SAVE/` (`SAVGAMA.DAT` and its `CHRDATA*`
files), it also plays those with their party, through encounters, fights
resolved by `--combat` and every treasure the scripts give out, checks the
stats recomputed for their characters, runs their spell effects, and saves
them again to compare the files; those tests are skipped without them.
`make sanitize` repeats these checks with AddressSanitizer and UBSan.
Tests also require Python 3 (standard library only). The native tools have no
third-party dependencies. Sanitizer targets require the compiler's ASan/UBSan
runtime libraries, which were unavailable on the initial development system.

## Image export

`daximages` writes native-size 24-bit BMP images and a labeled HTML contact
sheet (`index.html`) into the specified output directory. The directory is
created if absent; its parent must already exist. Open the HTML file in a
browser to browse the images and click through to individual BMPs. Keep the
HTML and images together. Running the same export again replaces its files.

By default every image in the archive is exported. `--entry N` selects a
zero-based directory entry, preserving duplicate IDs. Optional `--frame N`
selects a zero-based image within that entry, flattened across groups and
their pixel frames. Filenames include entry, ID, group, and frame; the sheet
also displays the flattened image index and dimensions. Use separate output
directories for different archives or selections.

Supported archive names (case-insensitive) are `TITLE`, `BIGPIC1–3`, `PIC1–3`,
`SPRIT1–3`, `CPIC1–3`, `CBODY`, `CHEAD`, `BODY2–3`, `HEAD2–3`, `COMSPR`,
`CURSOR`, `SKY`, `TILES`, and `8X8D1–3`, with the `.DAX` extension. The
`8X8D1.DAX` entry with ID 201 is the 8×8 font, not an image: it is reported
as unsupported while the other entries continue exporting. Draw it with
`daxcompose`'s `glyph` and `text` commands instead. Unsupported archive
names are rejected; malformed images and failed writes give a nonzero exit
status. Bulk exports can produce a partial sheet when some entries fail.

The image reader verifies the complete record layout before exporting. It
supports both single 17-byte headers (including multiple pixel frames) and
the `PIC`/`SPRIT` collections: a group-count byte, then four metadata bytes
and an image header/payload per group. Each image header begins with 16-bit
little-endian height, width in eight-pixel units, x and y placement fields,
then a pixel-frame count at byte 8. Bytes 9–16 hold a 16-entry, two-bit CGA
colour map, unused by these tools. Pixels are packed high nibble first. The
four collection metadata bytes are not yet interpreted.

In `PIC1–3`, each group after the first stores its pixels XORed with the
first group's, as decoded by the game's picture loader in `GAME.OVR`; the
groups are animation frames. Both tools decode this before exporting or
drawing. `SPRIT` groups are not delta-coded.

Previews use the standard EGA palette and render every pixel as opaque.
They retain the confirmed purple splash background. In-game sprite masking,
placement, animation timing, CGA mappings, and DOS display aspect-ratio
correction are not applied. These are asset inspection exports, rather than
composited game screenshots.

## Picture composition

`daxcompose OUTPUT.bmp COMMAND...` starts from a black 320×200 screen, runs
the commands in order, and writes a BMP:

- `fill X Y UNITS ROWS COLOR`: fill a rectangle with EGA colour 0–15.
- `draw ARCHIVE ENTRY IMAGE X Y`: draw an image opaquely.
- `sprite ARCHIVE ENTRY IMAGE X Y`: draw with colour 0 transparent and
  colour 13 drawn as black, as the game loads masked pictures.
- `mirror ARCHIVE ENTRY IMAGE X Y`: `sprite`, flipped left to right.
- `text FONT ENTRY X Y FG BG STRING`: draw a string in 8×8 cells, mapped as
  the game does (see below), with colours 0–15.
- `glyph FONT ENTRY GLYPH COUNT X Y FG BG`: draw glyph 0–255 `COUNT` times
  (0–255) along a row of cells.

`ENTRY` and `IMAGE` number entries and flattened images as `daximages` does.
`FONT ENTRY` names a record of raw glyphs: the game font is `8X8D1.DAX` entry 0.
Coordinates follow the original routines: `X` and `UNITS` are in eight-pixel
units, `fill` takes `Y` and `ROWS` in pixel rows, and the drawing commands
take `Y` in eight-row cells. Coordinates may be negative; drawing is clipped.
The game's actual screen layouts live in `GAME.OVR` and are not yet known.

## Library

`src/dax.h` exposes archive loading, record lookup by directory index, and
decompression. Initialize `dax_archive` to `{0}`, open it with `dax_open` (or
copy bytes into it with `dax_parse`), and release it with `dax_close`. Close an
archive before reusing it. Archive pointers and record output pointers must be
non-NULL. Record payload pointers are borrowed from the archive. Allocate at
least `record.decoded_size` bytes before calling `dax_decode`. On a decoding
error, the output may contain partial data and must be discarded.

The on-disk layout is a two-byte little-endian directory length followed by
nine-byte entries: one-byte ID, four-byte payload-relative offset, two-byte
decoded size, and two-byte packed size. Directory length counts bytes, not
records. Fields are read explicitly; C structure packing and host endianness
are irrelevant.

Compression follows `FUN_1000_6e33` in `START.EXE.c`: controls 0–126 copy the
next `control + 1` bytes, controls 129–255 repeat the next byte `256 - control`
times, and controls 127 and 128 are skipped. The decoder rejects truncated
runs, output overflow, and decoded size mismatches. This differs from standard
PackBits; substituting a generic PackBits decoder changes the results.

`src/picture.h` ports the runtime picture routines from `START.EXE`'s
graphics unit (code segment `0x27f` in the unpacked image) for the linear
Tandy pixel layout. Pixels match the DAX format, so 4-bit EGA colour indexes
are kept unchanged. The CGA and EGA layouts, and the per-scanline dirty
tables that limited copies to video memory, are not ported.

| Function | Original | Ghidra name |
| --- | --- | --- |
| `cok_picture_create`, `cok_picture_free` | `27f:1c2f`, `27f:1cf6` | `FUN_1000_41e6`, `FUN_1000_42ad` |
| `cok_picture_load` | `27f:02a5` | `FUN_1000_285c` |
| `cok_picture_draw` | `27f:09c3` (clipped), `27f:08d7` (unclipped) | `FUN_1000_2f7a`, `FUN_1000_2e8e` |
| `cok_picture_fill` | `27f:1d71` | `FUN_1000_4328` |
| `cok_picture_recolor` | `27f:15fd` | `FUN_1000_3bb4` |
| `cok_picture_mirror` | `27f:0b94` | `FUN_1000_314b` |

A mask byte has set bits where the destination is kept; a masked draw
computes `(destination & mask) | source`. Pictures with a mask have their
transparent pixels cleared to 0. `cok_picture_load_sprite` follows the
`GAME.OVR` loader for masked pictures: colour 0 is transparent, then colour 13
is recoloured to opaque black.

Two clipping paths differ deliberately from the original. Its clipped opaque
draw does not skip clipped source columns, so it shears horizontally clipped
images. Its fill draws an extra row at negative `y`, and at or below the
bottom edge its row count underflows and writes far past the picture. These
ports clip both correctly and skip empty draws. The original's saved
background used a different offset when clipped on the left; the port saves
with the source frame's layout, so drawing the save picture opaquely at the
same position restores the screen.

`src/text.h` ports the 8×8 text routines from `START.EXE`'s text unit (code
segment `0x521`), again for the Tandy layout only. The original draws straight
to video memory; the port draws into frame 0 of a `cok_picture`, such as a
320×200 screen (40 units by 200 rows). Coordinates are 8×8 cells, 40×25 on
that screen.

| Function | Original | Ghidra name |
| --- | --- | --- |
| `cok_text_glyph` | `521:01df` | `FUN_1000_51b6`, Tandy blitter `FUN_1000_5df4` |
| `cok_text_clear` | `521:030a` | `FUN_1000_52e1` |
| `cok_text_string` | `521:0353` | `FUN_1000_532a` |
| `cok_text_wrap` | `521:04ac` | `FUN_1521_04ac` in `START_FULL.EXE.c` |

A font is raw 1-bit glyphs, 8 bytes each, top row first, most significant bit
leftmost. Set bits take the foreground colour and clear bits the background,
so every cell is opaque. `8X8D1.DAX` entry 0 (ID 201) decodes to 1,416 bytes,
177 glyphs. It was identified by its glyph shapes, because the font pointer
(`DS:0x6128`) is set outside `START.EXE`. Strings map each
character by upper-casing ASCII letters and taking the result modulo 64, so
glyphs 1–26 are A–Z, 32 is a space, and 48–57 are digits; `cok_text_index`
does this. Glyphs 64–176 are reached only by index, through
`cok_text_glyph`: they hold another alphabet, and frame and border pieces.
`cok_text_clear` draws spaces with both colours equal, so it needs a font.

The original checks only the starting cell of a glyph run. A run past
column 39 writes outside the cell row, and glyph indexes past the font read
beyond it. The ports skip cells outside the picture and glyphs past the font.
The original takes unsigned byte coordinates; the ports accept negative ones
and clip. Its Tandy blitter ORs whole colour bytes into the pixel pair; the
ports use their low four bits.

`cok_text_wrap` is the printer `GAME.OVR` uses for running text. It keeps its
cursor at `DS:6134`/`DS:6135` between calls. A word is a run up to a space or
to a run of `! , - . : ; ?` (the set at `DS:0f38`); the space or punctuation
stays with the word. When a word passes the bottom of the window, the
original clears row 24, prints "Press any key to continue" in colour 13, waits
for a key (`1614:025b`), flushes the keyboard (`1614:045c`), clears the window
to colour 0 (`521:0b60`) and continues. The port does the same, but the key
wait and flush go through the `page` hook, and the original's per-character
`Delay(DS:4b38 * 3)`, made when `DS:4b59` is set, is the `delay` hook. The
port leaves out the mouse hide and show calls around the routine
(`1743:0051`, `1743:0030`). The original's window check allows a bottom row
up to 39, which the port keeps, but it does not reject a right edge left of
the left one or a bottom above the top, which would make its window clear
write far outside the window; the port prints nothing for those windows. A
final one-character word that wraps past the bottom row is printed below the
window rather than paged, as in the original; a dead branch meant to drop a
trailing space at the right edge is omitted.

`cok_text_input` (`521:0739`) edits a line on row 24 for the ECL input
opcodes, and `cok_text_number` (`521:08d9`) repeats it, six characters at a
time, until Turbo Pascal's `Val` (`1a46:136a`, ported as `cok_tp_val`) reads
a number from 0 to 65536; the low word is returned, so 65536 reads as 0.
Keys come through a `cok_keyboard`, which returns ASCII codes or 0 followed by
a scan code, as `Crt.ReadKey` does, and a negative value when input runs out.
Characters 0x20-0x7a are taken, so `~` and lower-case `{|}` are not, and each
is drawn one column further right before the cell is filled, which leaves a
blank column after the prompt. The line is upper-cased when Enter or Escape
ends it; Escape does not discard it.

`src/menu.h` ports the row-24 menus from `GAME.OVR`, keyboard only. Menu
text marks each hotkey with `~` (`3775:176e`): the marks are removed, the
marked character upper-cased, and every other letter and digit moved up by
0x20. Letters therefore show in lower case, which the font draws as capitals
in the normal colour, while hotkeys show in the highlight colour. Digits
become P-Y, which the layout (`67b5:00da`) takes as hotkeys: each upper-case
character starts an item, which ends two columns before the next. A digit in
menu text thus splits its item and, if typed as P-Y, returns 0xff, the
original's "not a hotkey" result. Item text is cut to 40 characters.

| Function | Original |
| --- | --- |
| `cok_menu_parse` | `3775:176e`, then `cok_menu_layout` (`67b5:00da`) |
| `cok_menu_draw` | `67b5:01e9` |
| `cok_menu_horizontal` | `3775:1885` with the key loop `67b5:03e2` |
| `cok_menu_read` | `67b5:03e2` as overlay `475c` calls it |
| `cok_menu_list` | `67b5:1368` as `3775:1990` calls it |

The selected item is drawn in colour 0 on the highlight colour. Left and
right arrows, and 4 and 6, move the selection; Enter returns the selected
item's hotkey; Escape and space are ignored. Other extended keys, and the
keypad digits 1-9 and `\` (mapped to scan codes by the table at `DS:1fdf`),
go to a `special` hook: `3775:1885` passes them to `546c:3334` and redraws
the party, which is not ported. Because that check follows the hotkey match,
a hotkey 1-9 can only be chosen with the arrows and Enter. With one item, the
arrows and 4 and 6 are special too. The original also polls the mouse
(`DS:8987`), times out (`DS:6e11`), and animates the view picture while it
waits; none of these are ported.

`cok_menu_list` shows a list in a window with a `Select` menu on row 24,
adding `Next` and `Prev` when there are more rows to page to, and `Exit` if
asked. Up and down (8 and 2) move the selected row, wrapping within the rows
shown; PgUp and PgDn (9 and 3), and `Prev` and `Next`, page. Escape or `Exit`
cancels but leaves the index where it was. The first row shown persists
between lists (`DS:6e0d`) as in the original, which can leave the selected
row outside the window. ECL lists have no headings; the spell lists do (see
Camp).

`cok_menu_ask` is `cok_menu_read` with the digits flag of `67b5:03e2`
(`[bp+0xc]`) clear, as the yes/no prompt (`67b5:177f`), Pics and the saved
game's letter call it: then 1-9 are not directions, and with one item 4
and 6 return themselves. `cok_menu_rows` is `67b5:1368` in full: a prompt
on row 24 in the heading colour, a first item other than `Select`, rows
flagged as headings (node byte 0x29), drawn in the heading colour and
skipped after each move or page in the same direction, at most a page's
rows (`67b5:10d1`), so that a list that starts with a heading starts on
its last row shown; the list drawn only when asked or when the first row
shown moves to the pick; and `Prev` offered only while rows other than the
leading headings are above the window. `cok_menu_list` is `cok_menu_rows`
with no headings.

`src/screen.h` draws the screen frames from the root segment `0x128` with
tile set 4, glyphs 0x100-0x127, which the game loads from `8X8D1.DAX` record
202 at startup (`6e22:0050`). Like every tile set it is loaded with colour 13
transparent (`127f:0111` with 13 and 1), which clears those pixels; the
frame is drawn opaque, so they show black. The layouts use tiles 0x14 and
up; the tables of tile values are at `DS:0e3a`, `0e62`, `0e7a`, `0ea1` and
`0eae`.
`cok_screen_frame` (`1128:0000`) clears the inside and draws the border, with
the three moons of Krynn on the top row at columns 8, 19 and 30, their tiles
offset by the phases in game words `0x4cf9`-`0x4cfb`. `cok_screen_adventure`
(`1128:0242`) adds the divider on row 16, the column 16 divider above it, and
the box of the 3D view from cell 2 to 14; `cok_screen_big` (`1128:0344`)
adds only the row 16 divider, for full-width pictures.

`START.EXE` is compressed with EXEPACK. `START.EXE.c` was decompiled from the
packed file, so its function addresses match the packed bytes, but far calls
and data references use the unpacked layout. Unpacked offsets are the Ghidra
address plus `0x239`; the data segment is `0x0bc6`. `GAME.OVR` calls the
graphics routines at `27f:offset` and the text routines at `521:offset`.

The SSI splash image in `TITLE.DAX` entry 0 (ID 1) decodes to 32,017 bytes:
a 17-byte header and 32,000 bytes of packed 4-bit pixels for a 320×200 image.
Its purple background was verified against the original in DOSBox.

## ECL scripts

`src/ecl.h` ports the interpreter for the ECL scripts in `ECL1.DAX`-`ECL3.DAX`
from `GAME.OVR`: operand decoding and variables from overlay `3775`, opcode
handlers from overlay `2fd3` (dispatch `2fd3:386d`). Addresses below are from
`START_FULL.EXE.c`; `DS:` offsets are in the data segment.

An ECL record's first two bytes are skipped and the rest loads at address
`0x8000` in a `0x1e00`-byte buffer (`3775:0361`). It starts with five
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

Variables are words: `0x4b00`-`0x4eff`, `0x7a00`-`0x7bff` and `0x7c00`-`0x7fff`
live in three game data blocks, `0x8000`-`0x9dff` are bytes of the script
itself, and other addresses are special game state such as the map position
(`0xc04b`, `0xc04c`) and facing (`0xc04d`). Many `0x7c00`-range addresses read
and write fields of the selected character's 409-byte record (`3775:07d9`,
`3775:0c00`); `0x7f12` picks the ECL file for the next block.

`cok_ecl_run` runs control flow, arithmetic, comparisons, `RANDOM` (Turbo
Pascal's generator), `SAVE`, tables and `NEWECL` itself, and decodes every
other opcode's operands before handing it to the `opcode` hook. Those opcodes
drive pictures, text, menus, monsters, combat, items and characters; the
screen-side ones are ported in `src/adventure.h` (see Playing scripts), the
monsters' and encounters' in `src/monster.h` (see Monsters and
encounters), the rest not yet. Comparisons order the first operand against the second,
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

Where the original would misbehave, the port stops with a status instead:
opcode `0x1f` and `PROTECTION` have no working handler and never advance,
division by zero is Turbo Pascal runtime error 200, and a failed `NEWECL` load
retries forever. Code addresses outside the buffer, more than 64 operands or
32 string operands, and `GOSUB` nesting past 256 are also errors. Variable
strings stop at 255 characters or the end of their store, and reads the
original leaves unset return 0.

## Playing scripts

`eclplay [options] ASSETS BLOCK` runs an ECL block on a 320×200 adventure
screen without a display, as `2fd3:3b47` enters a block: the load vector,
then the after-move and location vectors, starting over when `NEWECL`
switches blocks. It prints text as it is printed (`print:`), menus with
their `~` marks (`menu:`, `list:` and `item:`), the choices made (`choice:`)
and input read (`input:`), and the name and operand values of each opcode
that is not ported, in brackets. A spell effect the port cannot carry out
ends the run with its reason (see Spell effects). `--play` then runs the
adventure loop (see Adventure loop), which adds the party's square and
facing after each step or turn (`at: X,Y,DIR`) and the commands that are not
ported (`unported:`). `--keys` types keys (`\r` Enter, `\e` Escape, `\b`
Backspace, `\<`, `\>`, `\^` and `\v` the arrows); when they run out the run
stops. `\k` presses the next key while the party rests, which the rest loop
sees (`1614:03c2`), or as a battle is set up, which drops it (`1614:0479`).
`--saves DIR` is where the camp's Save writes; without it, saving logs an
error. A spell list logs its rows as `item:` and `heading:`, and quitting to
DOS ends the run with `(quit to DOS in block N)`. Cast and View run from the
commands and the camp, their lists logged as `list:` and `item:`. Monsters
loaded, the encounter's sprite and close-up and the money robbed log as
`monster:`, a battle's setup, where each combatant stands, and the end of a
fight as `combat:` (see Monsters and encounters and The battlefield), the
coins and items `TREASURE` adds as `treasure:` (see Treasure and the end of
combat), and what is paid and appraised in shops and the temple as `shop:`
(see Shops and the temple). `COMBAT`'s battle is ported only as far as its
setup and its end: between them it logs as `[COMBAT]`, unless `--combat
won`, `fled`, `lost` or `gods` resolves it. `--combat-map FILE` writes each
battle's map and combatants to `FILE` (see The battlefield). A fight ends
with the results and the treasure menu, which read keys; with no party at
all the monsters rejoice and the run ends. `--test-party N` adds N (1-8)
characters made up for testing, as a saved game's are added: a fighter, a
cleric of Mishakal, a White mage and a thief in turn, human, of level 1,
their weapons and armour readied, 20 steel each, the cleric's Cure Light
Wounds and the mage's Detect Magic memorized, one cell in combat (`+0xcf`
1), their stats computed as a loaded character's are. `--party SAVE` adds
the characters of a saved game to the party (see Party), and `--load SAVE`
loads the whole saved game first; then `BLOCK` may be left out to resume
where it was saved. WHO prints the character picked (`who:`). `--shots DIR`
saves `DIR/NNN.bmp` each time the game waits for a key, `--screen FILE` the
final screen. `--start ADDR` runs from a code address instead, `--vector N`
one vector, `--at X,Y,DIR` places the party, `--set ADDR=VALUE` sets
variables (in hex), `--file N` picks the ECL file (by default the first that
holds the block), `--still` loads only the first frame of each picture, and
`--trace` lists each instruction. For example, `./build/eclplay --start 899b
--keys '\r2\r' Assets 48` answers the guards at the gates of Gargath with
the second item of a list.

`src/adventure.h` holds the screen side of the opcodes, from overlay `2fd3`:

| Opcode | Handler | Port |
| --- | --- | --- |
| `PRINT`, `PRINTCLEAR` | `2fd3:0acf` | `cok_text_wrap` in cells 1-38 by 17-22, colour 10; a number prints in decimal |
| `HORIZONTAL MENU` | `2fd3:116d` | `cok_menu_horizontal`, colours 15 and 10, or all 15 for one item |
| `VERTICAL MENU` | `2fd3:0f9d` | prompt as `PRINTCLEAR`, then `cok_menu_list` below it |
| `INPUT NUMBER`, `INPUT STRING` | `2fd3:0a11`, `2fd3:0a57` | `cok_text_number`; `cok_text_input`, an empty line stored as a space |
| `PICTURE` | `2fd3:0914` | below 0x70, `PIC<file>.DAX` at cell 3, 3; else `BIGPIC<file>.DAX` at 1, 1 in its frame; 0xff restores the view |
| `CLEAR BOX` | `2fd3:3063` | the adventure frame (`1128:0242`) and the first picture frame |
| `DELAY` | `2fd3:2c33` | the `delay` hook, speed × 100 ms (`521:0b4b`) |
| `LOAD FILES`, `LOAD PIECES` | `2fd3:0cf4` | the map and wall sets of the 3D view (see below) |
| `LOAD CHARACTER` | `2fd3:02e9` | select a character by position until the script exits (see Party) |
| `ADD EP` | `2fd3:36dc` | experience for the selected character or the party |
| `WHO` | `2fd3:30b6` | pick the selected character from the party list |
| `DAMAGE` | `2fd3:2c80` | damage by attacks or saving throws, which can kill the party |
| `CALL` | `2fd3:329b` | address 0x2e10 only, the view after an encounter (see Monsters and encounters) |

A one-item menu reading `PRESS BUTTON OR RETURN TO CONTINUE.` is shown as
`PRESS <ENTER>/<RETURN> TO CONTINUE.`, and Enter picks it, as in the
original. Menu items are joined as `~ITEM` separated by spaces, cut to 50
characters. Small pictures are delta collections whose groups after the first
are animation frames, each preceded by its delay in hundredths of a second;
with animation off (`DS:4b4f`) only the first is loaded. The game speed
(`DS:4b38`) defaults to 4. The picture path taken when `0x7ee1` is not 0xff
(`3775:0538`) and the sequence for big picture 0x79 (`4877:0005`) are not
ported, and are reported as unported.

## 3D view

`src/view.h` ports the 3D view from overlay `69ea`, drawn with the 8×8 tile
routine of overlay `6e22`. `cok_adventure_view` shows it as `6945:00ba`
does: when `PICTURE 0xff` restores the view, and when a block is entered,
after its load vector, if it loaded files or the area stays in 3D
(`2fd3:3b47`), and after each step or turn of the adventure loop;
`eclplay --at X,Y,DIR` places the party before a block runs. Where the area
has no 3D view (`0x4be6` and `0x4c38` both 0), `6945:00ba` shows the big
picture in its frame instead.

`LOAD FILES` (opcode 0x21) loads a map: if its first operand is not 0xff or
0x7f and the area is 3D (`0x4be6`), record N of `GEO<file>.DAX` (`69ea:130d`),
which also sets `0x4bc5`. `LOAD PIECES` loads wall sets from
`WALLDEF<file>.DAX` (`69ea:1025`) into slots 1-3: all three operands, 0xff
leaving a slot empty, when `0x4be7` or `0x4be8` is 0; otherwise only the
first and third, so that a record of two sets can fill slots 1 and 2.
Scripts set those words before loading such a record; a block entered
without them (66 or 97, alone) names a record that does not exist. The
original halts when a file is missing or does not fit; the run then stops
with `COK_ECL_LOAD_FAILED` and the error. With an operand of 0x7f, it loads
record 0 into slot 1, which no shipped script does.

A GEO record is two bytes, then four tables of 256 bytes, one per square of
the 16×16 map, row by row, north up. The first holds the wall type 0-15 on
each square's north side in its high nibble and east in its low nibble; the
second south and west. The third is a byte per square, which picks the sky
colour: below 0x80 the area word `0x4bfd`, otherwise `0x4bfe`, through the
table at `DS:0dc4`. The fourth holds two bits per side (west highest), not
used by the view: they say how the party may pass a side that has a wall
(see Adventure loop). Off the map, squares read from the opposite edge unless
the block is 0 or 0x50 (`DS:8846`); then they have no walls.

A WALLDEF record holds one or two sets of five wall types, 156 bytes each:
wall types 1-5 use slot 1, 6-10 slot 2 and 11-15 slot 3. Each type lists, row
by row, the tile values for the ten places a wall can be seen at
(`DS:0df4`-`0e38`): front walls one, two and three squares ahead, the side
walls of those squares, and the edge between two front walls three squares
ahead. Records name their tiles as tile set 1; values from 0x2d are moved up
to the slot's set. Each slot's tile set is record N of `8X8D<file>.DAX`, or
N × 10 + 1 and N × 10 + 2 for a record of two sets. Tile sets 0 (`8X8D1.DAX`
record 203) and 4 (the frame's) are loaded at startup.

`cok_view_draw` follows `69ea:0820`: it fills the backdrop, then draws front
and side walls from two squares ahead back to the party's square, nearer
walls over farther ones, each tile masked with colour 13 transparent. The
backdrop (`69ea:0184`, outside CGA mode) is 44 rows of sky, a two-row line in
`DS:6d82`, which only CGA mode sets (so it is black), and 42 rows of
colour 8, then the horizon picture from `SKY.DAX` record 252. Under a sky of
colour 11 on squares below 0x80, record 251, the sun, shows facing east at
hours 1-5 (`0x4bc9`), south at 3-5 and 13-15, and west at 13-18, and record
250 shows facing north. The original draws into a 21-unit by 168-row buffer
(`DS:4b78`) that `127f:12e8` copies to the screen one unit right and one cell
down; the port draws on the screen with that offset, so the view fills cells
3-13 across and down, as small pictures do. The overhead map that the view
shows when `DS:6d84` is set (`69ea:000f`) and the CGA colours are not ported.

## Adventure loop

`cok_adventure_play` runs the loop of `2fd3:3c28` in a 3D area once a block
has been entered; `eclplay --play` enters the block, then takes commands
until the keys run out. Each turn takes a command from the menu on row 24
(`475c:09ec`), runs the after-move vector, takes the step chosen
(`475c:0e77`), shows the view and runs the location vector. `NEWECL` in any
vector enters the new block as `2fd3:3b47` does, which also frees the small
picture, clears `0x7ed5` and keeps the block in `0x4bf2`. The party's square
before each step is kept in `0x4bf0` and `0x4bf1`. When the loop ends it
clears `DS:4b57`, the flag that ends a run.

The menu reads `Move Area Cast View Encamp Search Look`, laid out as it is,
so each capital is an item and its key (`cok_menu_read`). `Move` changes the
menu to `Exit`: then the up arrow (or 8) steps ahead, left and right (4 and
6) turn a quarter, down (2) turns around, and `Exit` returns to the commands.
`Search` toggles bit 0 of `0x7eca`. `Look` sets bit 1 and passes ten
minutes; the location vector then runs once with `0x7eca` at 1, after which
the search bit is restored. `Encamp` camps (`2fd3:3403`, see Camp).
After a command, text that `PRINT` or `VERTICAL MENU` left in rows 17-22 is
cleared (`DS:884e`). Other special keys pick a character (`546c:3334`, see
Party) and redraw the party list and status line, which are also redrawn
after each step, turn and `Search` or `Look`.

Before the after-move vector runs, a step that would leave the map sets
`0x7ed5` (`475c:0765`); a vector that sets `0x7ec9` to 0xff cancels the
step. Whether the party can pass the side it faces comes from the map
(`69ea:0573`, `cok_view_passage`): a side with no wall is open, and
otherwise its two bits in the fourth table give 0 for a solid wall, 1 for a
way through, 2 for a locked door and 3 for one that cannot be picked. A step
(`475c:0813`) waits 50 ms, moves one square, wrapping at the map's edges,
and passes one minute, or ten while searching.

At a locked door the menu shows `Locked.` and whichever of `Bash`, `Pick`
and `Knock` may still be tried (`DS:7146`-`7148`), then `Exit`. All three
become available again after each step; before the first step none are, and
no menu shows. `Bash` (`475c:02f3`) rolls for each character in turn against
its strength (`+0x11`, with exceptional strength `+0x1c`), with a harder
table for a door that cannot be picked; a character too weak to try stops it
being offered. `Pick` (`475c:05b6`) is offered when a character is a thief
(`475c:0275`: a level in class 6, or a former one a human may still use,
`66c2:0efb`), and each character who is okay rolls 1-100 against `+0xdc`; at
a door that cannot be picked, choosing it only stops it being offered. A
bashed or picked door opens on both sides for good (`475c:0148`). `Knock`
(`475c:0720`) is offered when a character has memorized spell 0x1f among the
bytes from `+0x1e`; the first such character forgets it, and the party
passes once, leaving the door locked. As in the original, the door menu
takes special keys by the letter of their scan code, so the down arrow
(0x50, `P`) picks and the left arrow (0x4b, `K`) knocks.

The clock is seven words from `0x4bc6`, which carry into the next at 10,
10, 6, 24, 30, 12 and 256 (`DS:3874`): `0x4bc7` counts minutes, `0x4bc8`
tens of minutes and `0x4bc9` hours, then days, months and years.
`cok_adventure_pass_time` follows `57e4:0549`: it adds one unit at a time and
carries each full unit once. A new day counts a day in each moon's phase
(`0x4cfc`-`0x4cfe`); after 8, 1 and 6 days a moon moves to its next phase
(`0x4cf9`-`0x4cfb`, 0-3) and is redrawn on the frame. Months carry into
years without aging anyone; only once the years reach 256, where they
stay, does each unit that passes age each character a year (the word at
`+0x60`, `57e4:0459`). The clock then counts down the
party's spell effects (`57e4:0171`, see Spell effects).

`Cast` (`475c:0ade`) casts for the selected character if its status is
0 (see Casting); with none selected the original reads through NULL and
the port stops. `View` shows the selected character (see View). Not
ported, and logged as `unported:`: `Area`, the overhead map
(`69ea:000f`); and travel outside 3D areas (`475c:08d5`), where the loop
stops. Sound is not ported either.

## Party

`src/party.h` holds the party and its characters. The original keeps the
party as a linked list of 409-byte records from `DS:609a` (next at `+0x17f`)
with the selected character at `DS:6096`; the port keeps an array in the same
order, up to 72 records with the monsters `LOAD MONSTER` appends after the
party (see Monsters and encounters), and the selected record in
`vm.character`. `party.h` lists the record
fields the port uses. Fields such as the armour class (`+0x18d`, as 60 - AC)
are derived from the others and the items when a character loads (see
Derived stats); its spell effects are a list (see Spell effects).

A saved game, `SAVGAM<letter>.DAT` (`4b6d:1b34`; the camp's Save writes
one, see Camp), is 5,469 bytes: the ECL file, the variables
`0x4b00`-`0x4eff`, `0x7c00`-`0x7fff` and `0x7a00`-`0x7bff` as words, the
party's square, facing, wall ahead and square byte
(`DS:6d85`-`6d89`), the last and current modes, three wall sets (record and
slot), the party's size and eight 40-character names. Each name, stripped of
` .*,?/\:;|`, cut to eight characters and upper-cased (`169c:05da`), names
the character's files beside the saved game: `.SAV`, the record; `.STF`, its
items, 63 bytes each; and `.SFX`, its spell effects, 9 bytes each
(`4b6d:11e5`). Missing characters are skipped. Each character's derived
fields are then recomputed (see Derived stats). Each character added gets
the lowest combat icon slot free (`+0x137`) and is counted in `0x7f3e`
(`4b6d:1989`), and an NPC's levels are recomputed again. The original also
loads their combat icons and deletes any roster copies of them (`.WHO`,
`.STF`, `.SFX` named after the character); the port does neither.
`cok_adventure_restore` then reloads the map and wall sets in a 3D area,
sets the speed and animation from `0x4bfc` and `0x4bff`, and
sets `DS:4b52`, so that the first block keeps its variables and the
adventure loop redraws the screen (`6346:2c17`). Play resumes in block
`0x4bf2`, or 0x24 if that is 0 (`2fd3:3c28`).

The party list (`6346:07ba`) shows `Name` and `AC  HP` on row 2 from column
17, then a character a row from row 4: the selected one's name in white,
others light cyan, or light red when they cannot act (`+0x189` clear;
`6346:199d`). AC is light green, right-aligned to column 34 with a minus sign
for negative values (`6346:0984`); hit points are right-aligned to column 38,
yellow when below the maximum (`6346:0a0d`). Each row is cleared first, and one
more after the list, so a party that shrinks by two leaves a stale row, as in
the original. It is not drawn outside 3D areas unless `0x4c38` is set, nor
over a big picture (`DS:4b4e`). The status line (`6346:2d75`) on row 15, in
light green, reads like `7,15 N 00:00 search`: the square unless the overhead
map is on (`0x4bfb`), the facing (turned by `0x4cff`), the hour and minutes,
and `search` while searching, or `camping` in camp. The original adds `*`
while its debug flag (`DS:4b51`, Ctrl-D) is set; the port does not. The list
is redrawn when a block's vectors have run, by `CLEAR BOX`, `LOAD FILES` and
`WHO`, and after damage; the status line after steps, turns, `Search`, `Look`
and special keys in the adventure loop.

Special keys from a menu pick a character (`546c:3334`): up (or 8) the one
before, wrapping to the last (or the last, with none selected), down (or 2)
the one after, wrapping to the first, and every other special key the
first. `HORIZONTAL MENU` then redraws
the party list (`3775:1885`), and the adventure loop the list and status
line. The original walks off the list when the selected character is not in
the party; the port selects none.

`LOAD CHARACTER` selects the character at the position given by its operand,
`& 0x7f`, from 0; `EXIT` restores the selection made before the vector ran
(`DS:43bf`). Past the end of the party the selection stays and `0x7d00`
reads 0 (`DS:8855`). With bit 7 set, the original would also remove the
character (`4def:3b0a`) when `DS:883a` is set and its name was cleared, but
nothing sets `DS:883a`. `0x7eb1` and `0x7eb4` read the selected character's
position (`3775:0773`), the party's size if none is selected, and `0x7cc9`
whether it may use a former class (`66c2:0efb`).

`ADD EP` prints `Congratulations NAME gains experience!` (or `the party`)
and waits speed × 100 ms, then adds the points, divided by the number of
classes with a level (`+0xf9`-`+0x100`), to the 32-bit experience at `+0x116`
of the selected character (first operand 0) or each character, if it can act.
A character with no level divides by zero, which stops the run as runtime
error 200 would, even one that cannot act, as the division comes first.

`WHO` clears the text window and shows its prompt and `Select` on row 24
(`6346:32c7`), redrawing the party list with the character picked so far:
up and down (8 and 2) move through the party, wrapping, and `S` or Enter
picks. Escape does nothing. The original also leaves on the special keys
whose scan codes are `E` and `S` (NumLock and Del).

`DAMAGE` takes five byte operands: an attack count or flags, dice count,
dice sides, damage bonus, and a to-hit bonus or saving throw. Damage is the
bonus plus the dice, rolled with Turbo Pascal's `Random` (`60f4:1216`). Without
bit 7 of the first operand, it is that many attacks on characters chosen at
random from the size in `0x7f3e`, each hitting if a d20 (20 counting as 100)
plus the fifth operand beats `+0x18d`, a 1 always missing (`60f4:0ffb`), and
damage is rolled again after each. With bit 7, its low five bits are a
saving-throw bonus and the fifth operand's low three bits the throw: with bit
6 each character is hit unless it saves (bit 5: no save), with fifth operand
bit 7 the selected character (throw type - 1, and none for type 0), and
otherwise a random one; bit 4 deals the damage even on a save. A save
(`60f4:113a`) needs a d20 plus the bonuses at `+0x17c` and in the operand,
as a byte, to reach the throw at `+0xd0`; 1 always fails and 20 succeeds, and
a character with a level at `+0xfe` and `+0x5e` set gets -1 or +1 from the
word at `0x4bf8 + +0x5e`, a byte sum that wraps (perhaps meant for the
moons). Each hit (`3775:20a6`) prints `NAME is hit FOR N points of Damage.`,
or `NAME dies.` past its hit points plus 10, paging with `press
<enter>/<return> to continue` when the window is full, and redraws the list;
the dead take none. Damage equal to the hit points leaves a character
unconscious (status 4), up to 9 more dying (5), and more dead (6), and it
can no longer act (`6346:24d7`). The message tests more than 10 past the hit
points but the status 10 or more, and only the low byte of the damage is
dealt, as in the original. Afterwards, if no character can act, the frame is
cleared, `The entire party is killed!` printed and the run ended (`DS:4b57`);
either way the prompt then waits for a key. The characters' spell effects
change the attack and saving rolls (see Spell effects); the original also
keeps combat records, which are not ported.

## Derived stats

`cok_character_stats` ports `6346:0d20` and `cok_character_levels` ports
`66c2:0433`, with the routines they call. The original runs both, in that
order, at the end of loading a character (`4b6d:11e5`), and `66c2:0433`
again when an NPC (`+0xe7` 0x80 and up) joins the party (`4b6d:1989`); the
port does the same in `cok_character_read` and when it adds a saved game's
characters. `6346:0d20` does not run again after `66c2:0433`, so an item
that `66c2:0433` unreadies still counts until the next recompute, as in the
original. Item types come from `ITEMS`, which the game reads at startup
(`3e99:005b`): 128 records of 16 bytes from offset 2, at `DS:5886`.
`party.h` lists the fields of records, items and item types that these
routines use.

`6346:0d20` counts the items (`+0x142`), weighs them, each weight (`+0x37`)
times its count (`+0x39`) if that is not 0, with the six coin words from
`+0xed`, into `+0x17d`, and adds up the hands of the readied ones (`+0x17b`).
Each readied item fills the slot its type names (the far pointers at `+0x147`,
which it clears first; the port sets those bytes to 0 and keeps 1 + the item's
index in `cok_character.slots`, 0 for none): the last one of each slot
0-8, the first two rings, and the last readied items of types 0x1e and 0x0c as
arrows and quarrels. The base attacks, dice and damage bonus
(`+0x10d`-`+0x112`) become `+0x191`-`+0x196`, THAC0 (`+0x18c`, as 60 - THAC0)
starts from `+0x59`, armour class from `+0x113` and movement (`+0x198`) from
`+0xd5`. Without a weapon, strength adds to hit and damage (`6346:1412`,
`6346:14b5`, by the row `6346:137a` gives, when `+0x114` is set). With one
(`6346:0023`), its type sets the attacks, dice and damage bonus; dexterity
adds to hit for a missile weapon (`6346:12f8`) and strength when the type's
flags say; the weapon's bonus and its readied arrows' or quarrels' add to
both; and races 0 and 1 with types 0x12, 0x13 and 0x16-0x1a add 1 to hit, race
5 with type 0x43 2. The armour class is the sum of five parts (`6346:02c8`):
dexterity (`6346:1276`), a shield and its bonus, the bonuses of items whose
type's armour class is 0 (byte 6 0x80), the best bonus of such a ring, and the
best armour plus bonus, or the base armour class if that is better. Magic
armour (slot 2 with a positive bonus) drops the ring part. `+0x18e`, the
armour class from behind, is the armour, item and ring parts less 2. Those
items and rings also add their `+0x33` to the saving throw bonus `+0x17c`.
Armour sets the movement by its weight, 3 more with any bonus but 0, so
cursed armour speeds its wearer too (`6346:0240`), and
weight past the strength allowance (`6346:153b`) limits it to 9, 6 or 3
(`6346:03e6`). `+0xce` is the highest fighter, ranger or knight level, or 1.

`66c2:0433` sets the base THAC0 `+0x59` to the best for each class's level
(`DS:3882`), one better for `+0x5d` 3, or 6 with race 3 or 4; raises the
highest level `+0xd6`, which it never lowers; and sets `+0x10b` to 3 above
fighter or knight level 6 or ranger level 7. `66c2:000f` and `66c2:0722` set
the spells a day from `+0x11c` (`DS:3c99`) and the spells known: a cleric's,
with wisdom bonuses, and a knight's from level 6 unless of order 1 (`+0x5c`),
which know the cleric spells of each level they can cast (`DS:423b`); a
ranger's from level 8, which knows all druid spells (`DS:31c3`); and a mage's,
doubled for spell levels 1-3 by each readied item of power 1. `66c2:08a6` sets
the saving throws (`DS:405b`) and `66c2:0b9f` the thief skills (`DS:3911`,
`DS:3971` and `DS:39a9`, by level, race and dexterity). `+0x11a` then gets the
bits (`DS:38ea`) of the classes with a level, or with a former level below
`+0xd6`, and items in the slots whose type's classes (byte 13) share none of
them are unreadied, unless `+0x36` is set. A human that may use its former
class (`66c2:0efb`) also gets its former levels' THAC0, attacks and thief
skills.

The tables are the original's, and the port indexes them with the
original's arithmetic over the original's initialized data, from `DS:3509`,
the lowest an index can reach, to its end at `DS:43bf`. An index past a
table reads what follows it, as the original does: the thief tables by
level, race and dexterity lie together, dexterity 20 reads the bytes after
them (3, 3, 18, 16 and 75), a level 13 cleric's THAC0 is the next class's at
level 0, and a level of -1 reads the byte before the THAC0 table. Item type
128 is the zeroed record after the 128 of `ITEMS` (the game reads 0x810
bytes into `DS:5886`). Where the original would read data the game sets as
it runs, an item type past 128 or a saving throw for a level of 90 or more,
or leaves the result uninitialized, as `6346:137a` does for a strength past
25 or an exceptional strength past 100, the port fails with an error and
the character does not load. `make test` checks the embedded data against
`build/START_FULL.EXE`.

The port keeps these quirks of the original:

- `66c2:08a6` checks a former class after its class loop rather than in
  it, so only for class 7, the loop's last: a knight above its former
  knight level `+0x108` also gets the throws of that level, and level 0
  reads the entry before level 1, the thief's at level 12. A level 1 knight
  therefore saves as a level 12 thief.
- `66c2:0b9f` adds an uninitialized local to each thief skill. The stack
  holds the class counter `66c2:08a6` left there, 7, so every skill is 7
  higher, and a level 1 human thief reads languages at 7. With an item of
  power 11, skills 1 and 2 set it to 0 or 5 and the later skills keep that,
  as does the second call for a former thief.
- Saving throw 0 is raised, made harder, by a constitution of 4-18 for
  races 3, 4 and 5 or with a readied item of power 6.
- `66c2:0b9f` stops at the first readied item of power 2 or 11, so a
  character with both gets only the first one's effect.
- `6346:03e6` takes the weight past the allowance as a signed word, so
  32,768 or more past it counts as none.
- The dexterity table gives -19 for picking pockets at dexterity 10.

The twelve characters in `SAVE/` were saved by the original, and every field
recomputed from them matches the saved value but one: the two clerics of
`SAVGAMA.DAT` lack spell 8, which this executable's table makes them know.
Nothing in it clears a known spell, so those saves may come from another
version of the game (see Checks against the original). A differential test
against the original routines, run in an 8086 emulator on random
characters, also agreed; it is not part of the repository.

Neither routine reads spell effects (`.SFX`). Effects change stats through
their own handlers (see Spell effects): Spiritual Hammer's (`3f44:07b5`)
removes its hammer and recomputes when it ends, which is ported, and adds
one when cast, which is not; the stinking cloud's (`3f44:0ae0`), which
recomputes and then makes the armour class that from behind, 2 worse,
needs the combat record and is not. The character sheet (`546c:07bb`),
the Items menu after every key (`546c:17f9`) and Trade's receiver
(`546c:2178`, `546c:32b0`) recompute, as ported (see View). The other
places the original recomputes are not ported: the ECL opcodes `ADD
NPC` (`2fd3:311c`) and `DESTROY ITEMS` (`2fd3:35a3`), in combat each
combatant's turn (`3995:040b`), the AI's
choice of weapon (`3afb:1608`), attacks (`432f:1579`, `432f:1a45`), spells
with an attack roll (`5b04:1071`, which no spell cast outside combat
reaches); and creating, training, modifying and changing the order of a
character (`4def:06dd`, `4def:4d9e`, `4def:28fa`, `4def:567f`). Combat
setup (`3cb2:10d9`) recomputes every record (see The battlefield), the end
of combat (`351b:1968`) every record left, taking an item (`36d0:034c`,
which buying does too) the taker, and appraising gems (`58e7:1929`) the
appraiser after each key, as ported (see Treasure and the end of combat,
and Shops and the temple).

## Spell effects

`src/effect.h` ports the characters' spell effects: the rolls that consult
them and their dispatch (overlay `60f4`), their handlers (overlay `3f44`)
and their timers (`57e4:0171`). A character's effects are a list of 9-byte
records linked from the far pointer at `+0xe3`, read from `.SFX` in file
order (`4b6d:11e5`): `+0` the effect id, `+1` the minutes left as a word,
0 for an effect that does not end, `+3` a value its handler uses, `+4` set
to run its handler when the effect is removed, and `+5` the far pointer to
the next. The port keeps the list linked (`cok_effect`), without the
file's stale pointers. The characters in `SAVE/` hold only permanent
effects, each of value 0xff: 0x07, 0x12, 0x1a, 0x2f, 0x5c, 0x5e, 0x5f and
0x69, given by race or class; only Molly's 0x07 asks for its handler on removal.

| Function | Original |
| --- | --- |
| `cok_character_add_effect` | `60f4:1285` |
| `cok_character_find_effect` | `6346:2447` |
| `cok_effects_remove` | `60f4:01e9`, then `60f4:1743` for strength or charisma |
| `cok_effects_dispatch` | `60f4:057c`, `60f4:0352`, `60f4:01a8` |
| `cok_effects_attack` | `60f4:0ffb` |
| `cok_effects_save` | `60f4:113a` |
| `cok_effects_pass_time` | `57e4:0171` |

An effect is added at the end of the list; the original does not check
its allocation. Removing one takes it or, given none, the first with an
id; if its `+4` is set, the handler of the id given (not the effect's own)
runs with flag 1 first. It is then unlinked and freed. Removing id 0x0e
recomputes charisma, and 0x0c or 0x26 strength (`60f4:1743`), from the
base scores (`+0x10`, `+0x12` ... `+0x1a`, and `+0x1d` for exceptional
strength) into the current ones (`+0x11` ... `+0x1b`, `+0x1c`). Readied
items with a power (`+0x3e` 0x80 + power, `+0x3d` its kind) change them:
for strength, power 3, or 5 of kind 0, gives 18/100, 5 of kinds 1-6 19-24,
8 of kind 0 the base + 1 below 18, and 0x0d sets 3; for charisma, power 6
takes 1 and 8 of kind 5 adds 1 below 18. Then effects 0x26, 0x71 and 0x0c
give strengths by their value: up to 101, 18 with the value - 1 as
exceptional strength (so 0 gives 18/255), above that the value - 100.
0x26's strength is added to one below 19; a fighter, ranger or knight, now
or before, gets 10 times the excess over 18 added to its current
exceptional strength (`+0x1c`, not the base), up to 100. The better
strength wins at each step, where an 18 with a higher exceptional strength
beats 19 and up, so the result depends on the item order; charisma adds
the value of the first 0x0e. `cok_effects_ability` is `60f4:1743` for
any ability, as Remove Curse calls it: intelligence and wisdom are worked
out and not stored; dexterity (`+0x17`) adds 4, 2 or 1 for an item of
power 2 by the base below 7, up to 13 or above, 1 for power 8 of kind 3
below 18, and takes 2 for power 10; constitution (`+0x19`) adds 1 for
power 6 or 8 of kind 4 below 18, then sets the maximum hit points
(`+0x62`) to `+0x11b` plus, for each class, the hit points of its former
level and of its level above `+0xd7` (capped at its top, `DS:3903`, one
less from the top, a ranger's one more unless it has another former
level), 1-7 a level by constitution 15-25 for fighters, rangers and
knights, 1 at 15 and 2 above for the others, added as a byte and divided
by the number of classes with a level (none divides by zero: the port
stops); the hit points change as much, down to 0. At 20 and up the
character gets effect 0x3e (60 minutes, its handler on removal) if it
lacks it; below, its 0x3e goes. Removing an effect not in the list makes the original write to
`0000:0005`; the port fails.

`cok_effects_dispatch` runs the handlers for event 1-0x18 (`60f4:057c`),
each a fixed list of effect ids, in order; for each id the target's first
effect with it, or, for the ids the party shares (0x15, 0x2d, 0x2e and
0x31, the set at `60f4:0332`), the first record's in the list, a
monster's too, when the target has none (`60f4:0352`). In combat only members in range count, which needs
the combat map and is not ported. Events 6 and 9 first check magic
resistance (`60f4:04f3`) when the target has some (`+0x187`), an effect
is pending (`6b2f`) and no damage is, or magic damage (`6b31` bit 8): a
d100 up to the resistance plus 5 for each level the caster (the selected
character, `cok_effects_caster_level`, see Casting) has of the spell
(`6b33`) below 11, as a byte, clears the damage and the effect (but for
0x5b and 0x52). Handlers come from the table at `DS:6b94` that
`5b04:58ed` fills at startup (`3e99:0835`), and `3f44:38ea` after it
(`3e99:083a`), and get the flag (0 from the
dispatch, 1 on removal), the effect (the holder's, for a shared one) and
the character. While `DS:713c` is set, which readying an item and Remove
Curse do, `60f4:01a8` calls `3f44:3888` instead to add or remove the
item's effect (see View).

Of the ported code, only `DAMAGE` raises events: 0x10 for each attack and
0x0c for each saving throw. An attack (`60f4:0ffb`) now also misses when
its roll is negative after the effects. The timers reach every handler,
with flag 1. Handlers work through bytes of the original's data segment,
which keep their values between calls (`cok_rolls`): the saving throw
(`6b2e`), its type and result (`6b43`, `6b44`), the attack roll (`6b3b`),
the damage and its type (`6b30`, `6b31`: 1 fire, 2 cold, 4 electricity, 8
magic, 0x10 acid), the effect a spell is adding (`6b2f`), the spell
(`6b33`), its dice (`6b34`), the rate (`6b32`), morale (`6b3e`), whether
the target can be attacked (`6b37`), whether effects are being cured
(`6b38`), whether the game was saved in the current camp (`5885`: set by
Save, `4b6d:22de`, and cleared when the camp menu returns at `2fd3:344d`,
at startup by `3e99:005b` and `0843`, and by the start menu, `4def:01b4`,
which is not ported) and the combat round (`714b`).

Handlers ported, by address and effect id: `3f44:0124` (1), `0134` (2),
`0344` (8, 0x2d), `0379` (9, 0x2e), `03ae` (0x0a), `03cd` (0x0b, its end and
after it took hold), `04b1` (0x0c, 0x26), `05bc` (0x0e), `0625` (0x10),
`062c` (0x11), `065d` (0x12), `0681` (0x14), `07b5` (0x17), `09b3` (0x19),
`0ab8` (0x1d), `0cf0` (0x21), `0f3f` (0x24), `0f78` (0x27), `144c` (0x2a),
`1469` (0x2b, for a strength of 3 or less), `15a3` (0x2f), `16ef` (0x31),
`173a` (0x32), `176b` (0x36), `179c` (0x37), `17a3` (0x38), `17c6` (0x39),
`17ea` (0x3a), `1891` (0x3b), `1a72` (0x3d), `1b18` (0x3f), `2665` (0x49,
but for damage of type 0x20), `29d5` (0x4d, but for choosing a target in
combat), `320f` (0x59), `3258` (0x5b), `32a8` (0x5e), `3328` (0x5f), `334c`
(0x60), `325f` (0x5d), `3361` (0x61), `336f` (0x62), `3386` (0x63), `33a7`
(0x64), `3406` (0x65), `3449` (0x66), `3450` (0x67), `34f9` (0x6b, its end),
`3619` (0x6c), `363c` (0x6d), `3643` (0x6e), `3692` (0x6f, see The
battlefield), `303c` (0x52, see The battlefield), `3768` (0x71), `37e8`
(0x74), `386a` (0x76), `3876` (0x77) and `3881` (5, 0x13, 0x18, 0x5c). Many
do nothing but mark the character for other code, and all but a few act the
same when their effect ends. 0x5d, 0x64 and 0x67 look at what the selected
character strikes with (`3f44:13a9`): its readied weapon (slot 0), or for a
missile weapon its readied arrows or quarrels (`6346:3111`, by the weapon
type's flags), and 0x65 at the weapon itself. Handlers that print speak
through the `say` hook of `cok_effects`, as `6346:1883` does outside combat:
0x27, haste, the first time it runs (its value's bit 4 clear) sets the bit
and the character "ages" a year (`+0x60`); 0x17, Spiritual Hammer, gives a
character with no hammer and fewer than 16 items counted (`+0x142`) one at
the end of its items (type 6, name parts 6 and 0x79, bonus 1, `+0x3d` 0x17,
`+0x3e` 0x80) and says it "Gains an item"; its search for the new hammer, to
ready it in an empty weapon slot, steps past it, so it is never readied. The
part of 0x2b that prints is not ported. Not ported, because they need combat
(its records at `+0x183`, the map, targets or icons): 0x07, 0x1a, 0x1b,
0x1f, 0x25, 0x33-0x35, 0x44, 0x4b, 0x69, 0x6a, 0x72 and 0x73; combat and
text: 3, 0x0d, 0x15, 0x1c, 0x1e (the stinking cloud), 0x20, 0x23, 0x28-0x29,
0x30, 0x3c, 0x40-0x43, 0x45-0x48, 0x4c, 0x4f-0x51, 0x56-0x58, 0x70 and 0x75;
dealing damage or killing, with text: 0x0f, 0x16, 0x22 and 0x2c; healing,
with text: 0x3e; spells: 0x4a; a monster's breath or attack, which
`5b04:58ed` installs at startup and event 0x0e of the combat AI runs: 4
(`5b04:4ab8`), 6, 0x53 and 0x68 (`546e`), 0x4e (`4dc2`), 0x54 (`4eea`), 0x55
(`50cf`) and 0x5a (`5227`); and 0x78, whose handler is the item routine
`3f44:3888`, which reads past the 9-byte record. Id 0 has no handler: the
original calls `0000:0000`. A call that reaches any of these fails, naming
the handler; `DAMAGE` and the clock then end the run with
`COK_ECL_EFFECT_FAILED` and log the reason as an error.

`57e4:0549` calls `57e4:0171` with the unit and count once the clock has
moved. It turns them into minutes as a word, which wraps past 45 days,
with unit 0 counting as minutes, and counts them down ten at a time. Each
pass walks the members whose flag at `DS:46b4` (by position) is set,
clearing it: an effect with no time skips, one with more time left than
the pass loses it and sets the flag again, and one with no more is
removed as `cok_effects_remove` does. The walk stops after the effect that
was last when it began, so effects that removal handlers add are not
counted down in that pass. Outside camp (mode 2) every flag is set first;
in camp nothing is counted while none is set. When the effect that ends
is second in the list and was not the last when the walk began, the walk
starts again from the first, which loses the time twice; the port keeps
this. (Ending as the last, it has already stopped the walk.) When a
handler removes the effect the walk goes to next, the original reads it
after freeing it; the port fails.

The port keeps these quirks of the handlers:

- 0x5e (`3f44:32a8`), a racial bonus, adds 1-5 by constitution (4-6 ... 18-20)
  to saving throws of types 0, 2 and 4, on top of the change `66c2:08a6`
  makes to throw 0. For a constitution outside 4-20 the original adds an
  uninitialized local, whatever earlier calls left on the stack, which
  depends on the opcodes and members before; the port fails.
- 0x36 (`3f44:176b`) doubles cold damage unless the game was saved in the
  current camp (`DS:5885`), where 0x32 (`3f44:173a`) tests the failed save
  for fire.
- 0x11, 0x21 and 0x39 change the armour class in place each time they run,
  until the stats are next recomputed.
- 0x08 and 0x09 test the alignment (`+0x10a`) of the selected character,
  the attacker in combat, whoever is selected otherwise; with none selected
  the original reads through NULL and the port fails, as for 0x19 and 0x2f.
- 0x3d makes 1 damage of 1 die 255 when the fire is magical.
- 0x59's miss is used up by the first attack roll and comes back only at
  the start of combat (a roll of 0 in round 0).
- 0x6e divides by 5 the knight levels less 1; below 1 that overflows, a
  runtime error 200, and the port fails.
- 0x12 and 0x5f roll a d100 even when nothing is being resisted.

A differential test ran the original routines in an 8086 emulator against
the port on random characters, items and effects: every ported handler
with both flags, the dispatch for every event, attacks, saving throws,
removal (with the strength and charisma recomputed) and the timers, with
the stats recomputed first in half the cases so that weapons are readied.
Of 18,000 cases, the 17,120 the port carries out agreed in the records,
item and effect lists, the working bytes, the timer flags and the random
numbers drawn; the rest reach what is not ported or what the original
mishandles. It is not part of the repository.

## Camp

`src/camp.h` ports the camp from overlay `4888` and the rest loop of
overlay `57e4`; `src/magic.h` the camp's Magic menu, with the spell lists of
overlays `546c` and `5b04`. `Encamp` (`2fd3:3403`) runs the block's camp
vector (`4b3d`), then the camp menu; if an encounter interrupted a rest, it
redraws the screen and runs the rest vector (`4b3f`), which scripts use to
start the encounter. Then it shows the view, clears `DS:5885`
(`2fd3:344d`), so that the game counts as saved only in the camp it was
saved in, and outside 3D areas would mark the party on the overland map
(`4877:0005`, not ported). As in the original, a `NEWECL` in either vector
enters the new block only after the next command. When the camp vector
ends the run (`DS:4b57`, as when the party is killed), the original opens
the camp menu all the same; the port does not.

The camp menu (`4888:2c31`) sets the mode to 2, which shows `camping` on
the status line and keeps the effect timers from counting down members
with no timed effect, clears the rest time and the day's healing count,
and redraws the screen for camp (`6346:2c17`): the frame, the party list,
the status line and the camp's picture, `PIC` record 0x3b, which the
menus draw at cell 3, 3 while they wait (the original animates it). It
prints `The party makes camp...` on row 18 in light green and unmarks
every spell marked to memorize or scribe (`4888:06b8`). The menu reads
`Save View Magic Rest Alter Fix Exit`, prompt in light magenta and items
in white and light green as the adventure's, and takes keys until Exit or
Escape, or a rest is interrupted. It keeps the item selected by the last
menu, so after `Encamp`, the fifth command, Enter picks `Alter`. Special
keys pick a character (`546c:3334`) and redraw the party list; one whose
scan code is `E`, NumLock, also leaves. On leaving it reloads the small
picture shown before, unmarks the spells again, and restores the mode,
the status line, rows 18-22 and row 24, and clears the spell target
(`DS:710b`, `4888:2e28`). `View` shows the selected character (see
View).

`Rest` (`4888:0f05`) sets the rest time to what the member who needs
longest needs to learn its marked spells and scribe its marked scrolls
(`4888:0032`): 4 hours if any, 6 if any is above level 2 (into `+0x58`),
and 15 minutes a level, the levels added as bytes; a level-0 power counts
as 1 to memorize but 0 on a scroll. The hours may pass 23. Then it rests
(`57e4:0e3e`): row 17 shows `Rest Time:` and days, hours and minutes
(`57e4:0764`) in light green, the unit chosen in white; the menu `Rest
Days Hours Mins Add Subtract Exit` (`57e4:08a0`) picks a unit, adds 1 (5
for minutes) or subtracts as much, and rests; up and down (8 and 2) add
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
  (`+0x62` + spell) and takes it off the scroll, which is gone once its
  count (`+0x2f`) drops below 100 (`5b04:575d`, without the item count or
  the stats being recomputed).
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
`Champion`. `Icon`, the combat icon editor (`4def:408b`), is logged as
unported.

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
reads it (see Party), after setting the speed (`0x4bfc`), pictures and
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
memory: the far pointers in the records (`+0xe3`, `+0x143`, `+0x147`-`+0x17a`,
`+0x17f`, `+0x183`), items (`+0x2a`) and effects (`+5`), 0 in the port's
files, and in the `.DAT` the bytes after each name and the slots past the
party, which hold what was on the original's stack; and spell 8 of game
A's clerics (see Derived stats). The original also erases roster copies
of the characters (`4b6d:0a80`), which the port does not keep.

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
magic-user of an order gets from its moon (`0x4cf8` + `+0x5e`: 1 or 2,
less the spells it has marked beyond a day's); with no spells a day of
any kind the character `cannot memorize any spells`. The list (`Spells in
Grimoire`, rows 5-15) holds the known spells (`+0x62` + spell) of classes
it can use (`5b04:0083`) by level, a heading before each, without the
powers it has memorized. A spell picked with one of its level and class
left, or a magic-user's while bonus spells are left, is marked in the
first free byte (`+0x1e` on) and the bytes sorted by spell (`4888:0fb2`),
which puts the empty ones first. On leaving, the marked spells are shown
again with `Memorize these spells? `.

`Scribe` (`4888:132b`) does the same for the spells of scrolls (`Spells
on Scrolls`), listed when the scroll's order bit (`+0x35` 0x20 for order
1, 0x10 for 2) matches the character's (`+0x5e`) and its low three bits
are clear, which effect 0x10 clears for good (`5b04:0981`). A spell known
already says `You already know that spell`, one marked `You are already
scibing that spell` (sic), and one of a level and class with no spells a
day `You can not scribe that spell.`, on row 24 for a moment
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
  hold more than 58 spells or 49 scroll spells, and where scribing frees a
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

## Casting

`src/cast.h` ports casting outside combat from overlay `5b04`: the cast
(`5b04:1415`), its targets (`5b04:127e`) and the handler of each spell
from the table at `DS:6e3a`, which `5b04:58ed` fills at startup.
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
- The targets (`5b04:127e`), by byte 7: 1 the caster; 2 a member picked
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
+ 40, 0x3f (1d10 + 10) × 10 (2d10 × 10 in combat) and 0x43 1440.

Most handlers apply the spell (`5b04:1071`) to each target left in the
list: a saving throw if byte 8 is set (of type byte 9), damage if any
(`60f4:1db7`), and the effect of byte 10 for the spell's duration, with
the caster level or a given value (`60f4:20f7`). The effect is first
pending (`DS:6b2f`) through event 9 (magic resistance, then the
target's effects); if cancelled, or saved against with a save of kind
1, the target "is Unaffected". An effect of the same id with time left
is removed first; a permanent one stays and the new one is added after
it. Then the target says the spell's text (`6346:228c`, which outside
combat is `6346:1883`: name and text in rows 18-22, then a pause).
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
  there, NULL after its search, so 0. An emulator run of each path agreed.
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
  outside combat). Its second part, the clouds on the combat map, runs
  outside combat too and reads the map pointer (`DS:6a2e`) that combat
  has freed; the port leaves it out.

Not ported: the combat target routine (`432f:2337`) and the combat parts
of `5b04:1415`, `6346:228c` and `60f4:1db7` (icons, missiles, sounds,
"lost a spell"); spells cast by touch (byte 2 0xff, `60f4:1062`), which
none outside combat is; effects whose handlers need combat or text when
the spell's events or timers reach them, such as 0x0f and 0x16 of Slow
Poison when they run out, 0x1c (Mirror Image) on damage and 0x47 (from
0x3f) on event 9, which stop the run with `COK_ECL_EFFECT_FAILED`; and
spell ids 0x6c-0x7f, which only items name, past the handler table. The
spell target and the trade partner (`DS:710b`, `46b0`), which the
original keeps as pointers into freed records when Alter's Drop removes
a character, are forgotten in the port.

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
(whether offered or not), F10 `D`, NumLock exits. Items, Spells and Trade
redraw the sheet; Escape or Exit leaves, and the screen is redrawn for
the mode. View keeps the character in `DS:46ac` and starts trades from it
(`DS:46b0`).

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

Names come from the original's strings (`DS:0f5a`-`1dd3`, embedded and
checked against `build/START_FULL.EXE`), indexed as signed bytes; an index
past them stops the port, and so does one that lands in the middle of the
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
in shops and the temple (mode 1) `Sell` and `Id` (see Shops and the
temple). The stats are recomputed after every key (`6346:0d20`), and the
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
(`58e7:09fa`, see Treasure and the end of combat).

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
  whether to ask for another partner (0) or leave Trade. Loading the
  camp's picture zero-fills that part of the stack, so in View, the first
  command of a camp whose picture was not already loaded, Trade asks
  again; after anything chosen before in the same View (Items, Spells,
  Trade or Drop) or a View before in the same camp, it leaves. Elsewhere
  (View from the commands, a camp whose picture was kept, or another camp
  command before) the byte depends on the run's history, and the port
  stops with `COK_ECL_UNDEFINED`. An emulator run of the whole camp
  agreed on each case the port carries out. The treasure menu's View
  works the same way (see Treasure and the end of combat).
- An item used to cast a spell is followed by its position, as a spell
  such as Dispel Magic can remove an effect whose handler removes another
  item, Spiritual Hammer's; if the item itself goes, the original goes
  on with the freed item, and the port stops.
- A name's bytes past its length keep what longer names left (as the
  original's files show).

Not ported: the debug `View` item of Items (`DS:4b51`, Ctrl-D), and the
combat side: the turn View ends, `Use` in combat (`6346:300f`,
`6346:2964`), the combat panel.

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

## Monsters and encounters

`src/monster.h` ports the ECL opcodes that load monsters and start
encounters, from overlay `2fd3` and their helpers in `3775`, `4b6d`, `6961`
and `6d21`, and the wrapper of `COMBAT` without the battle.

A monster is a 409-byte character record from `MON<file>CHA.DAX`, with its
effects (`MON<file>SPC`, 9-byte `.SFX` records) and items (`MON<file>ITM`,
63-byte `.STF` items). Fields a monster uses beyond those `party.h` lists:

| Offset | Meaning |
| --- | --- |
| `+0x0cf` | bits 0-2 the size of its combat icon, bit 7 a large creature |
| `+0x0da` | its kind of undead, for turning; 0 for none |
| `+0x0e7` | 0x80 and up; the low bits are its morale |
| `+0x0eb` | coins, seven words, for the treasure |
| `+0x10b`, `+0x10c` | attacks a round, doubled, of its two attacks |
| `+0x10d`-`+0x112` | dice, sides and damage bonus of each (the derived `+0x191`-`+0x196`) |
| `+0x11b` | hit points at full; `+0x130` (a word) + `+0x132` × `+0x11b` is its experience |
| `+0x187` | magic resistance |
| `+0x18a`, `+0x18b` | 1 against the party, 1 while the computer controls it |

Monsters live in the party list. The original keeps one list from
`DS:609a`, the party first, then the monsters `LOAD MONSTER` appends,
which stay until the end of combat removes them; var `0x7f3e` stays the
party's size. The port's `cok_party` holds up to 72 records (8 + 63, the
size of combat's tables), and every walk of "the party" covers the whole
list, as each of the original's runs to the NULL at the end: the party
list, the special keys and the picker (`546c:3334`, `6346:32c7`, both
wrapping through monsters), `LOAD CHARACTER`, `ADD EP`, `DAMAGE`'s
saving throws and its check that anyone can act, Bash, Pick, Knock, aging,
the effect timers and the effects the party shares (`60f4:0352`, a
monster's counting), the camp, Fix, Order, Save, Display, the spell
targets and Detect Magic. Only `DAMAGE`'s random targets and the camp's
check for timed effects (`57e4:0171`) count to `0x7f3e`. Where the
original's walk would run past a table of eight, the port stops with
`COK_ECL_UNDEFINED`: resting with nine or more records (the learning
timers by position at `DS:712e`: a ninth and tenth record's land on the
healing count, the word at `DS:7136`, which would be deterministic, an
eleventh's on the resting flag) and saving them (`4b6d:22de`'s names on the stack run into
its return address). Character files of every record are saved, monsters
too. A character added (`4b6d:1989`) goes after any monsters. None of this
arises in the shipped scripts: every path from each of their 431 `LOAD
MONSTER`s reaches `COMBAT` with nothing between, so monsters are in the
list only until that `COMBAT`.

Each record has a combat record, `cok_combat_record`, the 22 bytes at the
far pointer `+0x183`. Combat setup (`3cb2:10d9`) gives every record one,
zeroed, and the end of combat (`351b:1493`) frees them; outside combat it
is NULL, read as all zero, as the original's end of combat reads
`0000:0013` for `+0x13` when no battle ran.

| Offset | Meaning |
| --- | --- |
| `+0x00` | a spell being cast, cast at its next turn |
| `+0x01`, `+0x02` | may cast, may use items this round |
| `+0x03` | initiative, signed; 0 once it has acted |
| `+0x04`, `+0x05` | the attack slot in progress, sweeps left |
| `+0x06` | movement left, in half squares |
| `+0x07`, `+0x08` | guarding, has attacked this round |
| `+0x09` | facing, 0-7 from north clockwise |
| `+0x0a` | a far pointer to the record it aims at |
| `+0x0e`, `+0x0f` | rounds dying, times attacked since its turn |
| `+0x10` | made to flee by an effect |
| `+0x11`, `+0x12` | attempts to turn undead, turning from attackers |
| `+0x13` | set past the party's size in the list |
| `+0x14`, `+0x15` | fleeing, the AI's way of moving |

`LOAD MONSTER id count icon` (`2fd3:0465`), unless 63 records were loaded
since `CLEARMONSTERS` (`DS:43be`), reads monster `id` (`4b6d:161b`): the
record (409 bytes, its pointers cleared, the readied slots `+0x147` left as
they are, no stats computed), its effects and its items, in file order.
A monster against the party (`+0x18a` 1) has its hit points (`+0x197`,
`+0x62`) scaled by the difficulty `0x4cf4`, 1-5, as `(d + 1) × hp / 4` in
a byte; either at 0 makes both 1. An undead (`+0xda`) counts in
`DS:8859`. The icons of the slot `DS:72eb`, `CPIC<file>` records `icon`
and `icon + 0x80`, ready and attacking, load with colour 0 transparent
(`6d21:01d0`; the CGA recolour, `DS:4b76`, is not ported), and are kept;
nothing draws them yet. Then the record and `count - 1` copies (0 counts as
1), until 63 are loaded, go to the end of the list, each with that slot
(`+0x137`). The copies' items and effects are the first record's in the
reverse order, as each is put first in a copy's lists. `DS:72eb` then
moves to the next slot and `DS:8851` is set, so `COMBAT` fights. The file
number is cut to one digit (`Str` with a width of 1). A missing record
says `Unable to load monster` on row 24, waits for a key and quits to DOS,
as in the original. The port stops with `COK_ECL_UNDEFINED` for a record
shorter than 409 bytes and effects or items that are not whole records
(the original reads past them), an icon slot past the table's 26
(`DS:6172`), and a 73rd record. With no party, where the original writes
through `0000:017f`, the monsters start the list.

`CLEARMONSTERS` (`2fd3:12fe`) clears `DS:8859`, `43be` and `8851`, sets
`DS:72eb` to 8 (as a block starting does, `3775:01e8`) and empties the
treasure pool (`cok_pool`: the coins at `DS:6b0c`, seven longs, and the
items at `DS:6b28`); it leaves the monsters in the list.

`SETUP MONSTER sprite distance picture` (`2fd3:03c9`) keeps the sprite
(`DS:72e9`), the distance wanted (var `0x7ec0`) and the close-up
(`DS:72ea`), sets the distance (var `0x7ec1`) to the least of that and the
open squares ahead (`3775:04a9`: a step at a time while the side faced has
no wall at all, doors and walls the party may pass included, at most 2;
the steps are bytes, the walls wrap as the view's do; 2, stored in
`0x7ec1` too, outside 3D areas), and shows the monster there
(`3775:0575`): until the close-up is shown, the first time in a 3D area
it loads `SPRIT<file>` record `sprite` into the small picture's slot
(`6961:00e4` with mode 1: every group, colour 0 transparent and 13 drawn
black, `cok_picture_load_sprite`), and later redraws the view to erase
it; in 3D mode it draws group `distance + 1` masked at its header's x and
y + 2 (`6961:072e`), which the view's buffer puts at cells x + 3, y + 3.
At distance 0 in 3D mode, outside `ENCOUNTER MENU`, the close-up `PIC`
replaces it at cell 3, 3 (its first frame), or with var `0x7ee1` not 0xff
a portrait (`3775:0538`, logged as unported). Outside 3D mode nothing is
drawn. A distance past 2, which only a script setting var `0x7ec1` could
give, says "Illegal range in Show3DSprite." and quits to DOS
(`6961:072e`), as in the original. `APPROACH` (`2fd3:08d6`) moves it a square nearer unless it is at
0; `SPRITE OFF` (`2fd3:2fe1`) redraws the view if a sprite is drawn.
`EXIT` forgets the sprite and the close-up (`DS:8830`, `8831`), so a
script's next `SETUP MONSTER` loads again; `PICTURE` 0xff erases a
sprite as it erases a picture. The original's overhead map, which the
first sprite turns off (`DS:6d84`), and its "Loading...Please Wait" are
not ported.

`ENCOUNTER MENU sprite distance picture result c0 c1 c2 c3 c4 text0 text1
text2 flee speed` (`2fd3:23e5`) shows the monster as `SETUP MONSTER` does,
then until a result describes it (the first text not empty from that of
its distance on, typed in light green in rows 17-22, cleared first only in
3D areas) and offers `~COMBAT ~WAIT ~FLEE ~ADVANCE`, or `~PARLAY` in
place of Advance at distance 0 or outside 3D areas (`3775:1885`, the
selection kept from the menu before). The monsters' reaction to the item
chosen, `c0`-`c4`, decides what is stored: 0 the monsters fled ("The
monsters flee."), 1 combat, 2 the party got away, 3 talk:

| Reaction | Combat | Wait | Flee | Advance | Parlay |
| --- | --- | --- | --- | --- | --- |
| 0, attack | 1 | 1 | 2 if the slowest member's movement reaches `flee`, else 1 | 1 | 1 |
| 1, hold | 1 | "Both sides wait." | 2 | nearer | nearer, at 0 3 |
| 2, timid | 0 unless `speed` is below the fastest's, then 1 | 0 | 0 | 0 | 0 |
| 3, advance | 1 | nearer, at 0 "Both sides wait." | 2 | nearer | nearer, at 0 3 |
| 4, talk | 1 | nearer, at 0 3 | 2 | nearer | nearer, at 0 3 |

"Nearer" approaches a square and asks again; 5 and up store nothing. The
movements (`3775:1f8b`) are `+0x198` of every record, doubled as a byte
with haste (0x27), else halved when slowed (0x2a), but both start from the
first record's own; with an empty list the original reads `0000:0198`,
and the port stops. No close-up shows in the menu. Row 24 is cleared at
the end. `PARLAY a b c d e result` (`2fd3:2adf`) offers `~HAUGHTY ~SLY
~NICE ~MEEK ~ABUSIVE` after the prompt " " and stores the operand of the
attitude chosen.

`CHECKPARTY field effect min max average found` (`2fd3:1517`) takes a
field given as a variable (operand type 1) by its address, any other by
its value: 0 tests whether a record has the effect, storing 0, 0, 0 and
the result; the variables of charisma (`0x7c19`), the thief skills
(`0x7ca5`-`0x7cac`) and movement (`0x7c9f`) give their least, greatest
and average (the sum, a word, divided by the count as signed); any other
stores nothing. So ECL3 block 80's `CHECKPARTY [7d1b] ...` (the movement
as other opcodes read it) leaves `[7f79]` as the menu left it, and
fleeing the minotaurs always fails, as in the original. With an empty list
it divides by zero. `PARTYSTRENGTH result` (`2fd3:136c`) sums as a byte,
for each record, a tenth of its mage levels × 8, its THAC0 better than 20
and armour class better than 0 × 5 each, its hit points and its cleric
levels × 4 (former levels for a human who may use them). `PARTY SURPRISE
a b` (`2fd3:17b6`) stores whether a record is a ranger or cleric/ranger
(`+0x5b` 4 or 10), and 0. `SURPRISE a b c d` (`2fd3:1856`) rolls two d6
and stores its result to address `0x2cb`, which keeps nothing, where it
meant var `0x7ecb`; only the dice remain. These walk the whole list too.

`ROB all percent chance` (`2fd3:229e`) robs the selected character, or
with `all` every record in the list: each of its seven coin words
(`3775:1c7c`, `+0xeb` on) becomes `Trunc(w × ((100 - percent) / 100))`
in Turbo Pascal 6-byte Reals, the low word kept; then each item
(`3775:1da9`) costs a d100 and goes on one up to the chance, which an
item of 25 or more lowers by 50 and one above 255 by 90, to no less than
0, for itself and every item after it. A readied item taken is unreadied
first (`546c:1ea7`): a cursed one says "It's Cursed" and goes readied; the
effect of an item's power is taken from the selected character, whose item
it need not be. The stats are not recomputed. With none selected the
original reads through NULL; the port stops.

The Reals are `cok_real`: byte 0 the exponent biased by 0x81, 0 for zero,
bytes 1-5 the mantissa below its leading 1, the top bit the sign.
`cok_real_from_long` (`1a46:1153`) is exact; the multiply (`1a46:113f`)
and divide (`1a46:1145`) round on bit 7 of a 48-bit result, ties up, after
one normalising shift; the divide takes 42 quotient bits. When either
mantissa's low 24 bits are 0, as for any word converted, the multiply
takes a short way that multiplies the lowest byte by the other's high
byte only, which is not the exact product: so `ROB 0 10 0` leaves 476 of
530 coins, where a C double gives 477. `cok_real_trunc` (`1a46:1157`)
ignores the mantissa's lowest byte. A percent above 100 gives a negative
factor, and the coins wrap: 1 of them at 255 becomes 65,535.

`COMBAT` (`2fd3:191c`) clears the big picture flag (`DS:4b4e`) and the
Move mode (`DS:8858`, restored after), then:

- with monsters loaded (`DS:8851`; a duel, `DS:883e`, is never set in
  CoK), clamps the distance (var `0x7ec1`) to the open squares ahead (2
  outside 3D areas), fights, runs the end of combat, and outside 3D areas
  loads the overland map, `BIGPIC<file>` 0x79 (`6961:07ed`), which only
  `BIGPIC1` holds: for the others, as for any big picture not there, the
  original's `127f:0111` leaves none, silently, and the port does too;
- else with var `0x7f6c` 1 clears it and opens a shop (`36d0:07da`), or
  with var `0x7ee2` 1 clears it and opens the temple (`340d:0ea9`; see
  Shops and the temple);
- else runs the end of combat, for treasure.

Then the mode is 4, or 3 outside 3D areas, var `0x7eca` keeps only its
search bit, the sprite and picture are forgotten (`DS:8830`, `884a`) and,
unless a shop or the temple closed outside 3D areas, the screen is redrawn
for the mode (`6346:2c17`), and outside 3D areas, unless the run ended or
`0x4c38` is set, the party would be marked on the overland map
(`4877:0005`, logged as unported). The preloads of overlays (`XXXX:0000`,
`432f:1e96`) and the sound driver's stops around them are left out.

The battle (`3995:0172`) is ported as far as its setup (`3cb2:1c58`, see
The battlefield), which builds the map, gives every record its combat
record and places them, removing monsters with no place; the rest is not
ported. Unless `eclplay --combat` resolves it, the battle is logged as
unported (`[COMBAT]`, as before) and changes nothing more; the battle's
end (`3995:004b`, see The battlefield) and the end of combat follow. The stub
(`cok_adventure.combat_stub`) decides instead: `won`, every record against
the party (`+0x18a` 1) drops (status 6, cannot act), and those on its side
past the party's size do not; `gods`, the original's cheat when started
with `Helm` (`432f:41e2`), says "The Gods intervene!" on row 24 and drops
them as `won` does; `fled`, every party record that can act flees (status
3); `lost`, the whole party dies. Then the end of combat (`351b:1968`)
runs, after every battle and for treasure alone (see Treasure and the end
of combat): the party's part, `351b:0574`, decides from the statuses the
stub left whether the party won, fled or was destroyed. Without `--combat`
nothing drops, so the party wins, with none of the monsters' experience;
with no party at all, as in `eclplay` without `--party`, it is destroyed,
as `351b:0574` finds no one standing, and the run ends.

`CALL [2e10]` (`2fd3:329b`), which scripts run after each encounter,
recomputes the party's square and, if a picture or sprite is shown or the
party's place, view or area changed (`DS:884a`, `884d`, `884f`, `8850`,
`8852`), forgets the sprite, redraws the view and the status line, clears
those flags and recomputes the wall ahead. Its other addresses, a sound
(0xb203), a step forward (0xc01e), the wall ahead outside 3D areas
(0xc018) and a frame of the small picture's animation (0x6803), are not
ported, and log as `[CALL ...]`.

The dice (`60f4:1216`), the sum as a byte of `count` rolls of `Random(sides)
+ 1`, are `cok_dice` (`ecl.h`) for every module, and `cok_dice_count` is
`60f4:1261`, which also keeps the count in `DS:6b34`.

The port keeps these quirks:

- `LOAD MONSTER` checks the 63 before reading its operands' values, and
  the icon slot moves once a load, not a copy, and is not checked.
- Scaling is a byte: at Champion a monster of 171 or more hit points wraps,
  and one that gives 0 gets 1, as do both fields when either is 0.
- `CLEARMONSTERS` and a `COMBAT` that does not fight leave monsters
  loaded in the list until a `COMBAT` that does, and every walk of the
  party sees them meanwhile, though the shipped scripts never do this.
- A door or any wall ahead keeps the monster at distance 0: at Throtl's
  gate (7, 15 facing north, a wall the party may pass) the guards' close-up
  shows at once.
- `ENCOUNTER MENU` waits forever while both sides wait; it passes no time.
- `CHECKPARTY [7d1b]` stores nothing; `SURPRISE` stores nowhere.
- `ROB` lowers the chance for the items after a heavy one (above 24 by 50,
  above 255 by 90), rolls the d100
  even at a chance of 0, takes the power's effect from the selected
  character, and rounds with Turbo Pascal's Reals.
- `ADD EP` divides by the number of classes before it tests whether the
  character can act, so a record with no class level, a monster or one
  dead, divides by zero.
- Up from a menu with none selected selects the last record.

A differential test ran the original routines in an 8086 emulator against
the port on random parties, monsters, maps and operands: `LOAD MONSTER`
with the DAX reads fed the files' records and the icon loads recorded,
`CLEARMONSTERS`, `SETUP MONSTER`, `APPROACH` and `SPRITE OFF` with the
picture loads and draws recorded, `3775:04a9`, `3775:1f8b`, `ENCOUNTER
MENU` and `PARLAY` with their menus fed choices and their text recorded,
`CHECKPARTY`, `PARTYSTRENGTH`, `PARTY SURPRISE`, `SURPRISE`, `ROB` and
`351b:1493`, comparing the variables, the list's records, items and
effects, the counters, the icon slots, the transcript and the random
numbers drawn. Of 13,000 cases, the 12,738 the port carries out agreed.
Both refused 246: an empty party for `3775:1f8b` (245) and a monster id
not in the file, which quits to DOS (1). The port alone refused 16: a 73rd
record (7), and `ROB` with none selected (7) or taking an item's power
from none selected (2), where the original reads through NULL. Mutations
that the unit tests cannot see (the first enemy in `351b:1493`, `ROB`'s
chance floored at 0, the power's effect from the
selected character, a field of operand type 3) each failed hundreds of
these cases. The Reals agreed with a reference model, itself checked
against the emulator, on 200,000 operations. It is not part of the
repository.

## The battlefield

`src/combat.h` ports combat setup (`3cb2:1c58`) with the rest of overlay
`3cb2`, the combat tile loader (`6d21:002c`), the map's lookups of overlay
`6beb`, `6346:268a` and `432f:2df3`, and keeps the battle's map and
tables in `cok_adventure.combat` (`cok_combat`) for the parts of combat
not yet ported.

Setup runs with the mode already 5, as `3995:0172` sets it first. It
clears the text delay flag (`DS:4b59`, which the port passes with each
print), frees the small picture (`6961:0537`), the big one (`DS:6e02`)
and the portrait's pieces (`6de5`, `6dea`, not ported), clears
row 24, waits speed × 100 ms and prints "A battle begins..." there in
light green (`1521:0353`); clears the round (`DS:714b`), the attack roll
(`6b3b`), the bodies, the kender who yelled (`71a7`), Magic On (`7198`),
the recovered missile (`60a2`), the weapons lost (`609e`) and var
`0x7f33`, and sets the round limit (`714c`) to 15; builds the map
(`3cb2:1029`), gives every record its stats and combat record
(`3cb2:10d9`) and places them (`3cb2:17f7`); sets the view's origin to the
first record's cell less 3 (`6beb:0bcb`, placed or not); clears `+0x11` of
each record's combat record and runs effect events 8 and 0x16 for it;
works out the enemies' health (`432f:2df3`) and clears the Move mode
(`DS:8858`). A key waiting is read and dropped (`1614:0479`) after the
map, the records and placement, and before each record is placed. The
screen (`6346:300f`) is not drawn, and logs as unported; the scroll it
makes (`6beb:096b`, `07a9`), which works out the screen positions
(`6beb:0077`), is ported. The flash picture (`DS:719e`), the lists of
clouds (`7111`-`711b`), the dead that explode (`6b45`, `6b95`, `6b96`),
the mouse's flags (`71a5`, `71a6`) and the debug trace (`6d7e:093e`,
Ctrl-D only) are left to the parts that use them.

The map (`DS:6a2e`, 0x4e9 bytes from GetMem, freed by `3995:004b`):

| Offset | Meaning |
| --- | --- |
| `+0`, `+1` | never used |
| `+2`, `+3` | the view's origin, the top-left of the 7 by 7 cells shown (signed) |
| `+4`, `+5` | the cursor is shown, its footprint (0 and 1 at setup) |
| `+6` | sight is not blocked (`6b30:03f1`; 0 at setup) |
| `+7` | a terrain value a cell, 50 across by 25 down, row by row |

A terrain value's four bytes (`DS:1ee4`, `cok_combat_terrain`) are the
cost of moving onto it (0xff where none can), the eye height of one
standing there, the height that blocks sight past it and its tile:
values 0x01-0x19 are DUNGCOM's frames 0-0x18 (0x17, frame 0x16, plain
floor), 0x1a-0x1f RANDCOM's (table, chair, white cloud, puddle, green
cloud, a fallen character) and 0x20-0x41 WILDCOM's 0-0x21; 0 stands for
off the map. The tile set (`DS:616a`) holds 40 frames of 24 by 24: setup
loads, opaque (`6d21:002c`), DUNGCOM record 1's 25 frames in a 3D area
(var `0x4be6`), else WILDCOM's first 33 of 35, from frame 0, and
RANDCOM's 6 from 0x22. The frames between keep what an earlier battle
loaded; 0x21, the river bank's, is never loaded. `TILES.DAX` is not used.
The keyboard flush after each load (`1614:045c`) is not ported, as
elsewhere.

Combatants are numbered from 1 in list order, the party first:

| Data | Meaning (`cok_combatant`) |
| --- | --- |
| `DS:623f + 4n` | its top-left cell x and y, then n and its size, 0 while it is not on the map |
| `DS:68d1 + 4n` | its record, a far pointer |
| `DS:6362 + n`, `63aa + n` | its cell less the view's origin, as last worked out |
| `DS:6242` | the count, one more than the combatants (entry 0's size) |
| `DS:63f3` | each cell's combatant, 0 for none (`6beb:0375`) |
| `DS:69ee + 7k` | from k 1, up to 8 (count `DS:6a2d`): a party member fallen where it stands, its x and y, and the terrain its body (0x1f) covers |
| `DS:6b2c`, `6b2d` | the records on each side that can act (`6346:268a`) |
| `DS:7197` | the enemies' hit points as a percentage of their most, in fives (`432f:2df3`) |

A size is a footprint of up to four cells (`DS:1fe4 + 8 × size`, from
`+0xcf` & 7): 1 one cell, 2 it and the one below, 3 it and the one to the
right, 4 two by two. The combat record (`+0x183`, see Monsters and
encounters) gets its facing and `+0x13` at setup.

On a 3D map (`3cb2:08cd`) each 3D square from 6 west to 6 east and 2 north
to 2 south of the party's, row by row from the north and west to east along
each, becomes a block of 6 by 5 cells whose top-left is 6 × east + 5 × south
+ 21 across and 5 × south + 10 down: north is up and to the left, and the
party's square is cells 21-26 of rows 10-14. Each side of a square is open,
a wall or a door (`3cb2:0306`, `0388`): open where the 3D map has no wall, a
wall where the party cannot pass (`69ea:0573`), else a door, the two faces
of the edge together; off the 3D map a wall, but open to the east and west
on the party's row. A block is floor in rows 2-4, its west edge drawn down
the slant (`03fc`), its north edge in rows 0-1 (`04a1`) and its corners from
the edges of its own and of the squares north, west and east of it (`051e`,
`06f6`); blocks overlap by a column, later over earlier. A square whose byte
has 0x40 (`69ea:07a5`), with a wall, no door, and walls where two opposite
sides are walls on the other two as well, gets a table on a d10 of 5 or less
on each plain floor cell a + b of row b of its block, for a 2 then 3 and b
2-4, each followed by a chair on 9 or less on each plain floor beside it
(`00d3`); the dice are rolled only where all that holds. Cells 6 and 7 and
those beside them, to 8 across and the row below, belong to blocks not yet
built, and the test for floor reads them as they are: GetMem does not clear
the map. In play it takes the 1,264 bytes the buffer of RANDCOM's record
held, which `6d21:002c` has just freed, so its cells hold that record from
its byte 7 (cell 0, 0 may hold the free list's size, which nothing reads),
and the port fills them so before building. Later blocks overwrite whatever
the dice put there: the furniture stands only in the room's own block, and
the rest shows only in the dice. Where the heap holds a free block of 1,264
to 1,751 bytes below the lowest of 1,752 or more, the map goes there instead
and holds what that held, such as an earlier battle's map or the buffers of
`8X8D1.DAX` records 201 and 203 (1,416 and 1,457 bytes), and the dice may
differ. Only GEO2 record 64, Neraka (ECL2 block 64), has rooms, at x 11-15
of y 8-9 and x 8-10 of y 12-13.

On open ground (`3cb2:0fc8`) every cell is floor, then come a river, trees
and scatter by the flags of terrain type 15 (`DS:0441`, 4): the river's d100
is rolled but needs flag 0x10 or 0x20 (`0a00`); column by column from row 1,
a floor cell below another gets something on a d100 of 8 or less: on a
second d100 of 8 or less a bush (0x2a, 0x2b), else a tree, its top
(0x20-0x25) above its trunk (0x26-0x29) (`0b0b`); then each floor cell's
d100 picks 0x30-0x31 up to 2, 0x2c-0x2f to 7, 0x37-0x39 to 17 and 0x32-0x35
to 22 (`0e4a`, `0ca2`): about 2,700 rolls. The port takes the other types'
flags, which no battle uses.

`3cb2:10d9`, for each record in the list: its stats (`6346:0d20`), a
combat record zeroed (the old one, which the original overwrites, freed),
`+0x13` set past the party's size (var `0x7f3e`), facing the combat
direction of the party's facing (`DS:03da`: north 7, east 2, south 3,
west 6), turned round against the party (`+0x18a` 1); an ally past the
party whose morale (`+0xe7` & 0x7f) is 0 or above 0x66 gets var `0x7ec6`
+ 0x80.

Placement (`3cb2:17f7`) counts each side's records that can act
(`6346:268a`), and gives each side a square (the party's, or the monsters'
var `0x7ec1` squares ahead), the quadrant it faces (the party's facing, or
the opposite), a first rank as wide as half those that can act, rounded up,
and four formations of 11 columns by 6 rows (`DS:43cc`), one for each square
it may stand on, free between the limits of `DS:03ee` for its quadrant, or
for the second square those of formation 4, the widest. Then for each record
in list order (`3cb2:1379`, `122c`): from its side's square, ranks from a
centre set by the square and quadrant (`DS:03ca`, `03de`, `03e6`), moved
back by the rank's number, take the centre and then cells to one side and
the other in turn, further each pair (directions of `DS:03da`). A rank ends
at a cell outside the formation in one of column and row, which is not
tried, or at the cell that makes as many as the first rank's width, later
ranks 12, which is; the first rank is the first of the record's whole
search, so a later square's ranks all take 12. In the party's first rank
facing east or west, the next rank is skipped if the party's square has any
side but the one it faces that is not a wall. A cell outside in both moves
on to the next square (`DS:03ba`: behind, then to either side) that the
side's own is not walled off from, from rank 0; with none left there is no
place. A cell is taken if it is free in its formation and the footprint
there, probed (`6beb:0c9d`), meets no one and can be walked on; the
combatant stands at column + 6 × east + 5 × south + 22, row + 5 × south +
10, of its square. The walls asked are the 3D map's even on open ground: the
test is for mode 3, and `3995:0172` has made it 5. A record placed that
cannot act leaves the map (size 0), a party member leaving a body; status 9
makes it unable to act and off the map. A monster (`+0x13`) with no place is
removed (`4def:3b0a`: selected first, the party's size and its group's icons
kept); a party member with none stays in the list, off the map, its place
the last cell whose formation cell was free (or an earlier battle's, with
none). The occupants are rebuilt after each record placed.

The battle's end (`3995:004b`, `cok_combat_end`) runs after the stub:
each record charmed (effect 0x0b) and okay runs (status 3) if more than
one enemy could act at the last count (`DS:6b2d`); each loses the first
effect of each id that lasts only through the battle (`60f4:1440`,
`DS:0db4`: 0x03, 0x0b, 0x15, 0x17, 0x1b, 0x1e, 0x1f, 0x33-0x35, 0x5b,
0x6a, 0x6b, 0x6f, 0x76 and 0x77); one berserk (0x4d) and turned (`+0xe7`
0xb3) goes back to the party's side; and the map is freed. Freeing the
clouds and the flash picture and restoring the spell target hook are
left to their parts.

Effect 0x52, a dragon's fear (`3f44:303c`), which event 8 runs at setup
for the red dragons of MON3 record 22 (ECL3 blocks 97 and 98), acts on
every record on the other side without effect 0x5c, 0x6f or 0x77: one of
level (`+0xd6`) 0-3 is terrified, effect 0x6f for good, through
`60f4:20f7` (event 9 first, so that magic resistance may leave it
"Unaffected"), and if it took hold the computer controls it (`+0x18b`),
a player character is turned (`+0xe7` 0xb3), its target is cleared and
it flees (`+0x10`); others are afraid (0x77) unless they make a saving
throw of type 4. The handler does the same when the effect goes. 0x6f's
handler (`3f44:3692`), as the battle's end removes it, gives a turned
character back (`+0xe7` 0, `+0x18b` 0) and clears `+0x10`. In combat the
original flashes the character and says "is terrified" or "is afraid" in
the side panel (`6346:228c`); the port says it as outside combat.

| Function | Original |
| --- | --- |
| `cok_combat_setup` | `3cb2:1c58`, `3cb2:1029`, `6d21:002c` |
| `cok_combat_dungeon`, `cok_combat_wilderness` | `3cb2:08cd`, `3cb2:0fc8` |
| `cok_combat_records` | `3cb2:10d9` |
| `cok_combat_end` | `3995:004b`, `60f4:1440` |
| `cok_combat_place_all` | `3cb2:17f7`, `1379`, `122c`, `11fb` |
| `cok_combat_footprint` | `6beb:000f` |
| `cok_combat_occupy` | `6beb:0375` |
| `cok_combat_cell` | `6beb:0493` |
| `cok_combat_on_view`, `cok_combat_visible` | `6beb:06be`, `06ef` |
| `cok_combat_scroll` | `6beb:07a9` and `0077`, without drawing |
| `cok_combat_x`, `_y`, `_size`, `_index` | `6beb:0bcb`, `0bf3`, `0c1b`, `0c43` |
| `cok_combat_probe` | `6beb:0c9d` |
| `cok_combat_place` | `6beb:10f3` |
| `cok_combat_count_sides`, `cok_combat_enemy_health` | `6346:268a`, `432f:2df3` |

The port keeps these quirks:

- The river's d100 is rolled for a type with no river; the tables' dice
  only where a room's conditions hold, and they read cells not yet
  built, RANDCOM's record.
- The walls of the 3D map hold back the formations on open ground too.
- The first rank's width counts the records that can act before any is
  removed; a party of two has a first rank of one, so its second member
  stands a rank back.
- The view's origin comes from the first record, placed or not. With the
  list's one record removed, the lookup of the first, NULL, finds entry
  1, which the removal cleared, at the last cell tried for it. With the
  list empty from the start it finds none, entry 1 holding a record of
  an earlier battle, and the origin is -3, -3.
- A record of status 9, as the ghasts of MON3 record 58 (ECL3 block 82),
  is made unable to act and taken off the map, so that the end of combat
  counts it as dropped (var `0x7ec8`).
- `6beb:0375` runs a footprint at x 49 into the next row.
- `6beb:0c9d` starts from plain floor, which a footprint all cloud and
  puddle keeps; a cell off the map makes it 0 whatever follows; ties go to
  the later cell.
- `6beb:0c43` compares the entry after the last too, which during
  placement holds the record being placed; afterwards the original's holds
  a record of an earlier battle or NULL, the port's NULL. A record that is
  not a combatant reads as at 0, 0, with the count as its size.
- `6beb:10f3`, raising a party member that has no body, writes the
  costliest terrain under its footprint into its cell; the count of
  bodies is never lowered.
- `6beb:07a9` moves the centre only toward its target and within 3-0x2e
  across and 3-0x15 down, so a centre outside stays outside.
- `6beb:06ef` reads the screen positions as last worked out.
- `432f:2df3` multiplies the hit points by 20 as a word, which wraps above
  3,276, and leaves the last value with no enemies.

Where the original misbehaves the port stops with `COK_ECL_UNDEFINED`: a
record on a side other than 0 or 1 (`6346:268a` counts one that can act,
as a signed byte, into the bytes around `DS:6b2c`, and placement indexes
the sides' formations by any); with no party, the list's first two
records both removed (the walk goes on from the last record kept, or
with none from the first, freed, whose next still names the second, so
it goes back to the second for good); a 72nd
combatant (the entry after it lies in the screen positions), a ninth body
(the table has eight), the body of a record of size 0 placed off the map,
a footprint outside the occupants, a size past 7 (`6beb:10f3` takes
`+0xcf` & 0x7f) and a lookup of a record that is not a combatant with a
count past 7, which read past the footprints, placing a record that is
not a combatant (`6beb:10f3` writes its size over the count), a facing
past 7, and stats that cannot be worked out.

`eclplay` logs the battlefield, `combat: the 3D map around X,Y facing D,
the enemy N squares ahead` or `open ground facing D, ...`, then each
combatant as it is placed, `N NAME at X,Y` (`, fallen` with a body, `,
off the map` with size 0), `N NAME has no place` or `NAME has no place
and is removed`, then `the view from X,Y`, and the screen as unported.
`--combat-map FILE` writes, for each battle, a line naming the block, the
view and the count; the map, a character a cell: a combatant's mark (1-9,
a-z, A-Z), or for the terrain `.` plain floor, `#` where none can walk,
`T` a table, `h` a chair, `_` a body, `:` cost 2, `~` cost 4 and `,`
anything else; the map again, each cell's value in hex; and a line for
each combatant (mark, number, name, cell, size, side and facing) and each
body.

A differential test ran the original routines in an 8086 emulator against
the port: open ground (`3cb2:0fc8`) on random seeds and flags, the 3D
builder (`3cb2:08cd`) on random squares, GEO records and random maps, the
map's cells first RANDCOM's record or other fills alike, the whole of
setup (`3cb2:1c58`, its drawing, text and tile loads hooked, the map
taking RANDCOM's buffer) followed by the battle's end (`3995:004b`) on
random parties, monsters with their effects (0x52 among them), maps,
squares, facings and distances, and each lookup on random tables,
comparing the maps, occupants, combatant and body tables, screen
positions, the list's records, items, effects and combat records before
and after the end, the texts said, the selection, the counts and the
random seed. All 1,000 maps of open ground and all 4,000 of the 3D
builder agreed (281 of those reading unbuilt cells). Of 6,000 setups, the
5,537 the port carries out agreed; both refused 15, where the original
never ends (with no party, its first two records removed; in 7 the port
stops first at the unported handler of effect 0x47); the port alone
refused 448: a side other than 0 or 1 (241), a magic resistance roll for
a spell with no character selected, which the original reads through
NULL (154), the unported handler of effect 0x47 on event 9 (50) and a
body off the map (3). Of 4,000 lookups, the 3,619 the port carries out
agreed; it alone refused 381: placing a record that is not a combatant
(243), a side other than 0 or 1 (58), a footprint outside the occupants
(30), sizes past the footprints (45) and placing one whose footprint
lies outside the map (5). It is not part of the repository.

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
the last missile combat put in the pool (`DS:60a2`, `432f:19b6`), as the
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
shipped one's is 0x3e.

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
   on the party's side (`+0x18a` 0) and is not an NPC (`+0xe7` below
   0x80), or in a fight that cannot kill (var `0x7ee6`, which no script
   sets) anyone in the list can act or is running, unconscious or dying.
   One that stands (status 0 or 1) means the party did not flee, and the
   experience is worked out and given (below). If the party lives, either
   it comes round: running and unconscious with hit points become okay,
   dying unconscious, and in a fight that cannot kill dying and
   unconscious are okay with a hit point; or when it fled, var `0x7ec7` is
   0x81, those who ran are okay and the rest are left behind, removed
   (`4def:3b0a`, the record before then selected) and counted out of
   `0x7f3e`. A destroyed party is removed and `0x7f3e` is 0.
3. The mode is 6, and the enemies are removed (`351b:1493`): every record
   whose combat record's `+0x13` or whose `+0x18a` is 1 counts as a
   monster (`DS:43c4`) and is removed (`4def:3b0a`), freeing its icon
   slot, one not past the party's size counted out of `0x7f3e`; var
   `0x7ec8` counts those that dropped (`+0x189` not 1), var `0x4cf8` is set
   if the first one did, and var `0x7ec7` becomes 1 for one that fled while
   it is 0. The others' combat records are freed, the first record is
   selected, and every record's stats are recomputed (`6346:0d20`). Where
   the selection the original keeps in `DS:43bf`, which `EXIT` restores
   after `LOAD CHARACTER` and a block's vectors restore when they end
   (`2fd3:3b47`), is a record removed here or in step 2, the original
   would go on with it freed, and the port stops with `COK_ECL_UNDEFINED`,
   unless the party was destroyed and the run ends first.
4. Unless the party was destroyed: if it fled, the pool's items go; the
   NPCs take their shares, the results show and the treasure menu runs
   (below); then the pool's items go. Its coins stay until the next
   `CLEARMONSTERS` or `TREASURE` replaces them. If it was destroyed, var
   `0x7ec7` is 0x80, the frame is cleared, "The monsters rejoice for the
   party has been destroyed" printed in cells 2-37 from row 5, cleared
   first (`351b:1aab`), and "Press any key to continue" shown in light
   magenta on row 24 until a key, and the run ends.
5. Vars `0x7f70`-`0x7f72`, `0x7ee3`, `0x7ee6` and `0x4cf5` are cleared.

The original's paths for a duel (`DS:883e`) and its demo (`DS:4b4b`),
which are never set in this game, are left out.

The experience (`351b:0037`) is a LongInt, `DS:8840`: each defeated enemy
in the list (`+0x18a` 1, status neither 0 nor 3) counts as a monster
(`DS:43c4`, which `351b:1493` sets too for each enemy it removes) and
gives `+0x130` + `+0x132` × `+0x11b`, the product a signed word; a
monster past the party's size puts its coins in the pool, and unless
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
too, added to `+0x116`: a tenth more for a cleric (`+0x5b` 0) of wisdom
above 14, a fighter, paladin, ranger, mage or thief whose prime
requisites (strength; strength and wisdom; strength, intelligence and
wisdom; intelligence; dexterity) are above 15; half for two classes (8,
10-14, 16) and a third for three (9, 15); all for the rest.

The screens, each on a cleared frame (`1128:0000`), are:

- The NPCs' shares (`351b:1618`). Each NPC that is okay counts `+0xe8` & 7
  shares, every other record one, as bytes; with any NPC shares, of each
  coin in the pool the NPCs take their shares of the pool divided by all
  the shares, cut to a byte, and the coins are gone. Then each okay NPC
  with `+0xe8` set says `NAME takes and hides his share.` (`her` for
  `+0x109` not 0) in light green in cells 5-34 from row 5, two rows apart,
  each cleared first, and `press <enter>/<return> to continue` waits on
  row 24, all one item, black on white (`67b5:03e2`): Enter, space,
  Escape or a special key ends it.
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
  first in `+0x1e`-`+0x57`), and `Exit`; the prompt is empty, the items
  white and light green, as the camp's. Special keys pick a character.
  `View` is View (see View), in which Drop puts the money in the pool
  (`58e7:09fa`); `Detect` casts the spell (`5b04:1415`) without saying
  "casts". `Exit` or Escape leaves when the pool is empty; otherwise
  `There is still treasure left.  ` in light green and `Do you want to go
  back and claim your treasure?` in white in rows 17-22, and `~Yes ~No`
  (`3775:1885`, the selection kept from the menu before): No leaves, Yes
  clears rows 17-22 and goes back.

`Take` (`351b:0ff7`) takes the items if the pool has no money, the money
if it has no items, else offers `Take: ` and `Money Items Exit` until one
runs out, Exit or Escape. It takes special keys by their scan codes'
letters, as Pics does: up and down (`H`, `P`) pick a character, PgUp
(`I`) takes items and NumLock (`E`) leaves. Items (`351b:0ec5`, `0df1`)
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
ones too (`+0xe7` 0 or 0xb3), in the pool, lightening its weight as words;
NPCs keep theirs. `Share` (`58e7:063a`) divides each coin by the records
Pool counts, a byte: each record below 0x80 then takes its share of each
coin, jewelry first, and a coin of the remainder while one is left, as
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
  the coins vanish; an NPC whose `+0xe8` is 8 takes no share but says it
  hides one; shares that wrap to 0 divide by zero, and the port stops.
- Share counts turned characters but gives them nothing, so their shares
  are lost, and gives records whose `+0xe7` is 1-0x7f a share they were
  not counted in; a record already carrying more than it may takes a
  whole remainder, its room wrapping. A hoard too heavy for the party,
  such as the 95,000 coins of ECL3 block 98, stays in the pool after
  Share.
- Take money checks the amount asked against the load before it cuts it to
  the pool's, and asks at most the pool's low word.
- A fled party's pool loses its items before the menu and its coins at
  the results; the menu still opens.

The weapons lost in combat and the missile recovered stay empty until
combat fills them.

In Trade from the treasure menu's View, Escape at the first coin list
tests a byte of the stack (see View). Loading the treasure's picture
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
`6961:05b9`, `06bd`), which is not ported and is logged, then the party
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

- `View` is View (see View), whose Items offer `Sell` and `Id` here.
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
jewelry (`+0xf5`, `+0xf7`), or says `No Gems or Jewelry`. It clears cells
1-38 by 1-22 and shows the character's name on row 1 (`6346:199d`), `You
have a fine collection of:` on row 7, and `N Gem`, `N Gems`, `1 piece of
Jewelry` or `N pieces of Jewelry` on rows 9 and 10, all white, with
`Appraise : ` and `  Gems  Jewelry Exit` (the leading spaces as given), as
it has them. Picked, a gem goes from its coins and is valued on a d100:
1-25 5 steel, 26-50 25, 51-70 50, 71-90 250, 91-99 500 and 100 2500; a
jewel on a d100 and then Turbo Pascal's `Random` (through Reals, which
change nothing): 1-10 50-499, 11-20 100-599, 21-40 150-899, 41-50
250-1499, 51-70 500-2999, 71-90 1000-3999 and 91-100 1000-5999. `The Gem
is Valued at N steel.` (or `Jewel`) shows on row 12, and `You can : `
offers `Sell Keep`, or `Sell` alone if the character has 16 items
(`+0x142`) or one more would overload it (`58e7:006d`). `Keep` makes it an
item, type 0x2f, named `Gem` or `Jewelry` (part `+0x31` 0x7a or 0x7b),
worth the value and weighing 1, after the character's items; any other
key sells it for the value in steel (`58e7:01f2`, what would overload
the character going to the pool with `Overloaded.  Money will be put in
Pool.`). The stats are recomputed (`6346:0d20`) after each key, and it
ends when none are left, or on `Exit` or Escape.

The temple's `Heal` (`340d:0be1`) clears row 24 and shows, in the frame
of a large picture (`1128:0344`), `NAME, how can we help you?` on row 1
in white and the cures (`DS:01cc`, 41 bytes each) in cells 2-38 by 4-15,
with `Heal Exit` (`67b5:1368`), for the selected character, until Exit or
Escape; then the screen is redrawn. Each pass clears the frame but the
list is drawn only the first time, so afterwards only the row picked
shows. A cure (`340d:027f`-`0b32`) that the character does not need says
so (the name on row 18 and the text below it, `6346:1883`, no pause) and
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
points (`+0x62`) above those at full (`+0x11b`) are divided by a byte sum
over its class levels (`+0xf9`-`+0x100`): each level, two for one above
15, a fighter's times the constitution less 14; they come off the
maximum unless the constitution is 17 or more, it has no fighter level
and `+0xfb` is not above `+0xd7`.

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
  stack (see View): on entering a shop or the temple it holds what the
  run left, and the port stops with `COK_ECL_UNDEFINED`; a View before,
  Yes to going back, Appraise showing the gems and, in the temple, Heal
  set it, so Trade is left; Buy, Take, Pool, Share and picking a
  character keep it. The temple's locals are 14 bytes fewer than the
  shop's, so every call from its menu lands 14 bytes higher and the same.
  An emulator run of each, in the shop and in the temple, agreed.

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

## Checks against the original

These follow the disassembly but have not been compared with the game
running in DOSBox:

- The frame's tile set (`8X8D1.DAX` record 202) is loaded with colour 13
  transparent (`6e22:0050`), so the frame shows black where its tiles have
  light magenta. Compare the frame's border and moons with the original.
- Wall tiles and sky pictures are masked with colour 13 in the 3D view.
  Compare a view, such as Throtl's street from 7, 15 facing north, for light
  magenta or holes.
- Load saved game A and compare the party list and status line beside the
  view: the columns of AC and hit points, the colours, and `7,13 N 00:00`.
- Fall into the pit in Throtl and compare the damage messages and how they
  page.
- Load saved game A, save it again, and see whether KAL and SIRRION now
  know spell 8 (`+0x6a` of their `.SAV` set), as this executable's
  `66c2:000f` would make them. If not, the game in DOSBox is not the one in
  `Assets/`, or something not yet found clears it.
- The thief skills include the 7 that `66c2:0b9f` reads from the stack,
  which the saved thieves show; an interrupt between `66c2:08a6` and
  `66c2:0b9f` could leave another value. Load a saved game with a human
  thief and compare its thief skills (`+0xdb`) with the port's; character
  creation calls `66c2:0b9f` from elsewhere (`4def:06dd`), with another
  value on the stack.
- Load saved game B and rest a day: the effects in the `.SFX` files of its
  characters, all permanent, should be unchanged when it is saved again.
- Give a character with no effects (not a dwarf, elf, half-elf, kender or
  ranger, whose racial and class effects load first from `.SFX`) a long
  timed effect, then a short one, then any third, from spells cast in that
  order outside combat, and let time pass until the short one ends: in the
  ten minutes in which it ends, the long one should lose twenty
  (`57e4:0171` restarts its walk). With only the two, it loses ten.
- With a dwarf or kender of constitution 19 in the party, compare the
  saving throws against a trap's `DAMAGE` with those of another race: the
  port adds 5 (effect 0x5e) to throws of types 0, 2 and 4.
- Camp in Throtl and compare the camp screen: the campfire picture in the
  view, `The party makes camp...` on row 18, the status line ending
  `camping`, and the rest time on row 17 (`Rest Time: 00:00:00`, the
  minutes in white) with the rest menu.
- Camp right after `Encamp` and press Enter: the original opens `Alter`,
  the fifth item, as the menu keeps the selection.
- Save in camp, then load that game: it holds mode 2 (`0x1407`). Compare
  how the original resumes it with how it resumes a game saved from the
  party menu.
- Hurt a character a few points with a cleric of two first-level spells a
  day in the party and `Fix`: the clock should pass the time of
  `4888:28f9`, 270 minutes divided by 41 over the points lacking, which
  depends on the 14 it reads from the stack.
- Rest a full day twice in one camp, and once each in two camps: the
  party heals a point a day only counting the ticks of one camp
  (`DS:7136`).
- With a magic-user of an order carrying a scroll and another item whose
  `+0x3c`-`+0x3e` hold the scroll's spell id, scribe it: the other item
  is marked, and the scroll is not used.
- In `Alter`'s `Pics`, press the down arrow: the pictures flag changes.
- Open the grimoire: the first spell highlighted is the last row shown.
- `Display` a character with effect 0x13: it shows `Find Traps`.
- With a knight above level 6 and no cleric level who knows a granted
  power, memorize it: the port lets it be marked again and again, counting
  against 0x71 (`4888:0700`); see whether the original does.
- Cast Strength on a knight or ranger with no other class and a
  strength of 3: the roll is the low byte of a frame pointer (`5b04:2f03`),
  0x74 from the commands' `Cast` and 0x9a from camp's `Magic`, so the
  strength should become 18 and the exceptional strength 100 either way.
  Check both.
- `Encamp`, choose `View` first, `Trade`, pick a partner, and press
  Escape at the first coin list: the original should ask for a partner
  again. Choose `Spells` or `Drop` first, or `View` twice, and it should
  leave Trade. From the commands' `View` the port stops; see what the
  original does.
- Use an item whose `+0x3d` is 0x80, then cast a spell from memory: it
  should not say "casts", and the spell should stay memorized.
- Cast Haste with a mage of level 4: each member hasted should "age" a
  year, once (its value's bit 4).
- Open a character's sheet: compare the layout and colours with the
  table in View, a fighter/mage's class line (`Fighter/Red Mage`) and an
  exceptional strength (`18(00)`).
- Ready the Spiritual Hammer a cleric conjured, then unready it: it
  should vanish (power 0 removes effect 0x17).
- In an amount, type more than the coins there are, then Backspace: the
  last digit stays on the screen.

- Enter Throtl from the south (7, 15 facing north): the guards' close-up
  should show at once, as the wall type on that side, which the party can
  pass, keeps the monsters at distance 0 (`3775:04a9`).
- Meet a wandering monster in an open corridor: compare the sprite at
  distances 2 and 1, masked over the view at its header's place + 3 cells,
  with colour 13 black, and the close-up at 0, with the original.
- In Throtl's temple (ECL1 block 33, the cleric round the corner), `Wait`
  should bring him nearer, and the menu should start on the item chosen in
  the menu before; parlaying `Sly` should lead to the stack of papers.
- Have a character with 530 of a coin robbed of 10% (`ROB 0 10 0`, ECL1
  block 17): it should keep 476.
- Try to flee the minotaurs of ECL3 block 80: it should always fail
  (`CHECKPARTY [7d1b]` stores nothing).

- With a party of six, open the chest of ECL1 block 32 (250 steel, 5 gems
  and 3 jewels): the results should say each character receives 1391
  experience points (`351b:0037`). Compare
  the results screen and the treasure menu: the chest's picture in the
  view's place, the party list, no status line, `View Take Pool Share Exit`.
- In that menu, `Take`: the coins should list jewelry first, and the
  question read `How much Jewelry  will you take? ` with two spaces.
- `Share` right away: of the 5 gems, the first five characters should get
  one each, and the sixth none.
- Leave with coins in the pool: `There is still treasure left.` and the
  question should show, and `~Yes ~No` should start on the item the menu
  before had selected, not on No.
- With a cleric who has Detect Magic memorized, `Detect` in the treasure
  menu: no "casts" message, and the items with a bonus should show `* `.
- Flee a fight: the results should say `The party has fled.` and `Each
  character receives 0`.
- `Share` the hoard of ECL3 block 98 (95,000 coins): what the party
  cannot carry should stay in the pool, with `Share` still offered.
- Take a random potion from a treasure of ECL3 block 81 (`TREASURE ...
  130`) and drink it: generated potions are of type 0x30 or 0x35, where
  the shipped ones are 0x2f; see whether the game can use them.

- Enter Throtl's armoury (ECL1 block 17): the first screen should keep
  the view's frame as it was, with the portrait last shown (if any) in
  the view's place, and only a redraw after `Buy` draws the frame. `Buy`
  should list lines like `Long Sword` and `7` ending at column 30, and
  buying say `NAME buys a Long Sword` on row 24.
- In a new game, before visiting any other shop, the weapon smith of
  ECL2 block 50 should list each weapon at its value and charge 15
  steel for any of them (var `0x7f6d` 0, `36d0:0484`).
- Appraise a gem with a character carrying no items and choose `Keep`:
  the gem should be gone and no item gained. Press Escape at `You can : `
  instead: the gem is sold.
- Leave a temple with money in the pool: the priest's first sentence
  should vanish at once behind `Do you want to go back and retrieve your
  money?`.
- In a temple's `Heal`, buy a cure: on the list's return only the row
  picked should show, the first time the first cure highlighted.
- Raise a dead fighter of constitution 15 whose maximum hit points are
  above those at full: it should lose the whole difference.
- Buy `10 Arrows +1` (worth 120 at ECL2 block 50's magic shop or 50 at
  ECL3 block 80's), halve it down to a stack of 2 and a single one, and
  sell both: the stack should fetch about a tenth of the single one's
  price (2 x 60 / 20 = 6 against 60 for those of ECL2).
- In a shop, `View` first, `Trade`, pick a partner and press Escape at
  the first coin list: the port stops (the byte depends on the run); see
  whether the original asks for a partner again. After a `View` before, it
  should leave Trade.

- Fight the guards at Throtl's gate with a party of six (7, 15 facing
  north, `ATTACK`): "A battle begins..." should show on row 24 after a
  pause; the street should run across the battlefield, walled to the
  north but for the gateway, a guard standing in it; the party in two
  ranks of three, the first character's in the middle of the front
  rank, the second's to its right, the third's to its left, and the guards
  in a rank before them. Compare the map with `eclplay --test-party 6
  --set 4be6=1 --combat-map FILE --keys '\r\rE' Assets 32`.
- With the second of four characters unconscious, fight anything: its
  body should lie where the second would stand, right of the first, and
  the third and fourth stand a rank back.
- Stay to fight the huge patrol that sallies forth from Throtl (ECL1
  block 16 at 8cda: 9 red dragons, 9 bozaks and 9 sivaks) on open
  ground: count how many stand on the battlefield; those that find no
  place are removed before the fight and bring no experience. How many
  find one depends on the trees and on the walls of the 3D map loaded
  and the party's square; in `eclplay`'s run (no 3D map loaded, the party
  at 0, 0) 7 bozaks and 9 sivaks find none, and each of saved game A's
  party receives 4937.
- Fight in the rooms of Neraka (ECL2 block 64, squares at x 11-15 of y
  8-9 and x 8-10 of y 12-13): tables and chairs should stand only in
  the room's own block, never in the block to its east or below it.
- Fight the red dragons of ECL3 block 97 with a party of low levels:
  each character of level 3 or less should flash and be "terrified"
  as the battle begins, and those of higher levels "afraid" unless they
  save.

## Disassembly image

`make merged` writes `build/START_FULL.EXE` and `build/START_FULL.map` with
`tools/ovrmerge.py` (Python 3, standard library only); `make test` and
`make sanitize` build them too. The game logic lives in
`GAME.OVR`, a Turbo Pascal overlay file (`FBOV`) that `START.EXE` loads through
35 `INT 3Fh` stub segments, with 506 entry points in all. The tool unpacks
EXEPACK, appends each overlay segment from paragraph `0x1f48` (above the stack
and heap the program reserves), turns the overlay fixups into MZ relocations,
and rewrites every stub entry as a `JMP FAR` to its routine. The stubs cover
`GAME.OVR` exactly; the tool stops if any byte is left over.

Load `START_FULL.EXE` in Ghidra as an MZ executable. Its addresses are the
unpacked ones, so `521:04ac` is `1521:04ac` at Ghidra's default load segment
`0x1000`, with no `0x239` correction. The map lists each overlay's stub, load
segment and `GAME.OVR` offset, and where each stub entry jumps. The output is
for disassembly only and has not been run under DOS.
