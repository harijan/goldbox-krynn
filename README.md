# Champions of Krynn Linux port

The native tools are a C17 DAX archive reader, decompressor, image exporter,
picture and text compositor, ECL script interpreter and disassembler, and a
headless script player that shows the scripts' text, menus and pictures on
the adventure screen. The original DOS executable, decompiler output, and
game data are reference inputs, kept in `Assets/`. These tools do not yet run
the game.

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
make test
make sanitize
```

`daxcheck` reads archives without modifying them. It validates every directory
entry and decompresses every record, checking the decoded byte count. It exits
nonzero if any file or record fails. `--list` prints metadata for each validated
record; directory entry numbers are zero-based. Duplicate IDs are retained.
`make test` includes synthetic malformed inputs, all supplied DAX archives,
export checks for all 26 supported graphics archives (2,363 images), and
tests of the picture, text and menu routines, including PIC delta decoding on
`PIC1.DAX` and the game font in `8X8D1.DAX`, and plays the opening scripts
with `eclplay`.
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
row outside the window. Heading items (flag byte 0x29) are not ported; ECL
lists have none.

`src/screen.h` draws the screen frames from the root segment `0x128` with
tile set 4, glyphs 0x100-0x127, which the game loads from `8X8D1.DAX` record
202 at startup (`6e22:0050`). The layouts use tiles 0x14 and up; the tables
of tile values are at `DS:0e3a`, `0e62`, `0e7a`, `0ea1` and `0eae`.
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
is not ported, in brackets. `--keys` types keys (`\r` Enter, `\e` Escape,
`\b` Backspace, `\<` and `\>` the arrows); when they run out the run stops.
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

A one-item menu reading `PRESS BUTTON OR RETURN TO CONTINUE.` is shown as
`PRESS <ENTER>/<RETURN> TO CONTINUE.`, and Enter picks it, as in the
original. Menu items are joined as `~ITEM` separated by spaces, cut to 50
characters. Small pictures are delta collections whose groups after the first
are animation frames, each preceded by its delay in hundredths of a second;
with animation off (`DS:4b4f`) only the first is loaded. The game speed
(`DS:4b38`) defaults to 4. The 3D view (`6945:00ba`), party list
(`6346:07ba`), status line (`6346:2d75`), the picture path taken when
`0x7ee1` is not 0xff (`3775:0538`), the sequence for big picture 0x79
(`4877:0005`) and the wall sets that `LOAD FILES` and `LOAD PIECES` load are
not ported: the view is left blank and the others are reported as unported.

## Disassembly image

`make merged` writes `build/START_FULL.EXE` and `build/START_FULL.map` with
`tools/ovrmerge.py` (Python 3, standard library only). The game logic lives in
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
