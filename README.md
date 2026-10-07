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
the adventure loop and the camp, including PIC delta decoding on `PIC1.DAX` and the game font
in `8X8D1.DAX`, and plays the opening scripts, the view of Throtl and a walk
through it with `eclplay`. It builds `build/START_FULL.EXE` (see
Disassembly image) to check the original's tables that the port uses. With
the original's saved games in `SAVE/` (`SAVGAMA.DAT` and its `CHRDATA*`
files), it also plays them with a party, checks the stats recomputed for
their characters, runs their spell effects, and saves them again to compare
the files; those tests are skipped without them.
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
rest not yet. Comparisons order the first operand against the second,
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
switches blocks. It prints text as it is printed (`print:`), menus with their
`~` marks (`menu:`, `list:` and `item:`), the choices made (`choice:`) and
input read (`input:`), and the name and operand values of each opcode that
is not ported, in brackets. A spell effect the port cannot carry out ends
the run with its reason (see Spell effects). `--play` then runs the adventure loop (see
Adventure loop), which adds the party's square and facing after each step or
turn (`at: X,Y,DIR`) and the commands that are not ported (`unported:`).
`--keys` types keys (`\r` Enter, `\e` Escape, `\b` Backspace, `\<`, `\>`,
`\^` and `\v` the arrows); when they run out the run stops. `\k` presses
the next key while the party rests, which the rest loop sees (`1614:03c2`).
`--saves DIR` is where the camp's Save writes; without it, saving logs an
error. A spell list logs its rows as `item:` and `heading:`, and quitting
to DOS ends the run with `(quit to DOS in block N)`.
`--party SAVE` adds the characters of a saved game to the party (see Party),
and `--load SAVE` loads the whole saved game first; then `BLOCK` may be left
out to resume where it was saved. WHO prints the character picked (`who:`).
`--shots DIR` saves `DIR/NNN.bmp` each time the game waits for a key,
`--screen FILE` the final screen. `--start ADDR` runs from a code address
instead, `--vector N` one vector, `--at X,Y,DIR` places the party, `--set
ADDR=VALUE` sets variables (in hex), `--file N` picks the ECL file (by default
the first that holds the block), `--still` loads only the first frame of each
picture, and `--trace` lists each instruction. For example,
`./build/eclplay --start 899b --keys '\r2\r' Assets 48` answers the guards at
the gates of Gargath with the second item of a list.

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

Not ported, and logged as `unported:`: `Area`, the overhead map
(`69ea:000f`); `Cast` (`4888:0a0d`); `View` (`546c:0d74`); and travel
outside 3D areas (`475c:08d5`), where the loop stops. Sound is not
ported either.

## Party

`src/party.h` holds the party and its characters. The original keeps the
party as a linked list of 409-byte records from `DS:609a` (next at `+0x17f`)
with the selected character at `DS:6096`; the port keeps an array in the same
order, and the selected record in `vm.character`. `party.h` lists the record
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
before, wrapping to the last, down (or 2) the one after, wrapping to the
first, and every other special key the first. `HORIZONTAL MENU` then redraws
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
error 200 would.

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
needs the combat record and is not. The other places the original
recomputes are not ported either: the ECL opcodes `ADD
NPC` (`2fd3:311c`), `DESTROY ITEMS` (`2fd3:35a3`) and `COMBAT`, through
combat setup (`3cb2:10d9`), each combatant's turn (`3995:040b`), the AI's
choice of weapon (`3afb:1608`), attacks (`432f:1579`, `432f:1a45`), spells
with an attack roll (`5b04:1071`) and the end of combat (`351b:1968`); taking
an item from treasure or a shop (`36d0:034c`, `546c:32b0`) and appraising
gems (`58e7:1929`); the character sheet (`546c:07bb`), the Items menu
(`546c:17f9`) and Trade (`546c:2178`); and creating, training, modifying
and changing the order of a character (`4def:06dd`, `4def:4d9e`,
`4def:28fa`, `4def:567f`).

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
the value of the first 0x0e. `60f4:1743` also recomputes dexterity and
constitution (with the maximum hit points and effect 0x3e) for other
callers, none of them ported; only strength and charisma are ported.
Removing an effect not in the list makes the original write to
`0000:0005`; the port fails.

`cok_effects_dispatch` runs the handlers for event 1-0x18 (`60f4:057c`),
each a fixed list of effect ids, in order; for each id the target's first
effect with it, or, for the ids the party shares (0x15, 0x2d, 0x2e and
0x31, the set at `60f4:0332`), the first member's when the target has
none (`60f4:0352`). In combat only members in range count, which needs
the combat map and is not ported. Events 6 and 9 first check magic
resistance (`60f4:04f3`), which needs the caster's level and is not
ported when a spell's damage or effect is pending. Handlers come from the
table at `DS:6b94` that `3f44:38ea` fills, and get the flag (0 from the
dispatch, 1 on removal), the effect (the holder's, for a shared one) and
the character. While `DS:713c` is set, which only readying an item does,
`60f4:01a8` calls `3f44:3888` instead to add or remove the item's effect;
that is not ported.

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
`0344` (8, 0x2d), `0379` (9, 0x2e), `03ae` (0x0a), `03cd` (0x0b, its end
and after it took hold), `04b1` (0x0c, 0x26), `05bc` (0x0e), `0625`
(0x10), `062c` (0x11), `065d` (0x12), `0681` (0x14), `07b5` (0x17, but
for creating the hammer), `09b3` (0x19), `0ab8` (0x1d), `0cf0` (0x21),
`0f3f` (0x24), `0f78` (0x27, after its first time), `144c` (0x2a),
`1469` (0x2b, for a strength of 3 or less), `15a3` (0x2f), `16ef` (0x31), `173a` (0x32), `176b` (0x36),
`179c` (0x37), `17a3` (0x38), `17c6` (0x39), `17ea` (0x3a), `1891`
(0x3b), `1a72` (0x3d), `1b18` (0x3f), `2665` (0x49, but for damage of
type 0x20), `29d5` (0x4d, but for choosing a target in combat), `320f`
(0x59), `3258` (0x5b), `32a8` (0x5e), `3328` (0x5f), `334c` (0x60),
`325f` (0x5d), `3361` (0x61), `336f` (0x62), `3386` (0x63), `33a7`
(0x64), `3406` (0x65), `3449` (0x66), `3450` (0x67), `34f9`
(0x6b, its end), `3619` (0x6c), `363c` (0x6d), `3643` (0x6e), `3768`
(0x71), `37e8` (0x74), `386a` (0x76), `3876` (0x77) and `3881` (5, 0x13,
0x18, 0x5c). Many do nothing but mark the character for other code, and
all but a few act the same when their effect ends. 0x5d, 0x64 and 0x67
look at what the selected character strikes with (`3f44:13a9`): its
readied weapon (slot 0), or for a missile weapon its readied arrows or
quarrels (`6346:3111`, by the weapon type's flags), and 0x65 at the weapon
itself. The parts of 0x17, 0x27 and 0x2b that print are not ported. Not
ported, because they need combat (its records at `+0x183`, the map,
targets or icons): 0x07, 0x1a, 0x1b, 0x1f, 0x25, 0x33-0x35, 0x44, 0x4b, 0x69, 0x6a, 0x6f,
0x72 and 0x73; combat and text: 3, 0x0d, 0x15, 0x1c, 0x1e (the stinking
cloud), 0x20, 0x23, 0x28-0x29, 0x30, 0x3c, 0x40-0x43, 0x45-0x48, 0x4c,
0x4f-0x52, 0x56-0x58, 0x70 and 0x75; dealing damage or killing, with
text: 0x0f, 0x16, 0x22 and 0x2c; healing, with text: 0x3e; spells: 0x4a;
and 0x78, whose handler is the item routine `3f44:3888`, which reads
past the 9-byte record. Ids 0, 4, 6, 0x4e, 0x53-0x55, 0x5a and 0x68 have
no handler: the original calls `0000:0000`. A call that reaches any of
these fails, naming the handler; `DAMAGE` and the clock then end the run
with `COK_ECL_EFFECT_FAILED` and log the reason as an error.

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
the status line, rows 18-22 and row 24. `View` is logged as unported
(`546c:0d74`).

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
until Exit or Escape for the selected character. `Cast` is logged as
unported; the original goes on to `4888:0a0d` and, for a spell picked,
`5b04:1415`. `Rest` is the camp's. A character whose status is 1 or that
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
