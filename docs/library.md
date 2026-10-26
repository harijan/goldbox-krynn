# Images and the library

Image export, picture composition and the shared library. Part of the
[developer notes](../DEVELOPERS.md).

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
`CURSOR`, `SKY`, `TILES`, and `8X8D1–3`, with the `.DAX` extension: the
game's `images` lists (see [Games](games.md#games)); with `--game death-knights` Death
Knights', `PIC1-7`, `SPRIT1-4`, `BACK1-5`, `SKYGRND` and `8X8D1-5` among
them (2,374 images; its 8X8D sets of 256 tiles count 0xff in their
headers). `--game NAME` (for `daxcompose`, before
the output) names the game, `champions` by default. The
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
colour map, unused by these tools (the game's CGA mode uses it; see
[Display modes](front-end.md#display-modes)). Pixels are packed high nibble first. The
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
are kept unchanged; the EGA's planes hold the same pixels, and a CGA
picture holds its CGA colours in the nibbles (see [Display modes](front-end.md#display-modes)). The
per-scanline dirty tables that limited copies to video memory are not
ported. The clipped
draws of the three modes (`127f:226c` CGA, `24fd` EGA, `27af` Tandy) share
their arithmetic and copy loops (`127f:2927`, `2986`), so what they do at
the edges, such as the counts of 65536 of The combat screen, is the same
in every mode.

| Function | Original | Ghidra name |
| --- | --- | --- |
| `cok_picture_create`, `cok_picture_free` | `27f:1c2f`, `27f:1cf6` | `FUN_1000_41e6`, `FUN_1000_42ad` |
| `cok_picture_load` | `27f:02a5` | `FUN_1000_285c` |
| `cok_picture_load_cga` | `27f:02a5`, its CGA path | `FUN_127f_02a5` in `START_FULL.EXE.c` |
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
segment `0x521`), again for the Tandy layout (CGA pictures mapping the
colours). The original draws straight
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
`Delay(DS:4b38 * 3)`, made when `DS:4b59` is set (`1521:042e`), is the `delay`
hook. The game's printer, `cok_adventure_wrap`, waits the speed times 3 ms
after each character while `game->text_delay` (`DS:4b59`) is set, and hides
the mouse's cursor while it prints and shows it after (`1743:0051`,
`1743:0030`; see [The mouse and the joystick](front-end.md#the-mouse-and-the-joystick)). The original's window check
allows a bottom row up to 39, which the port keeps, but it does not reject
a right edge left of the left one or a bottom above the top, which would
make its window clear write far outside the window; the port prints
nothing for those windows. A final one-character word that wraps past the
bottom row is printed below the window rather than paged, as in the
original; a dead branch meant to drop a trailing space at the right edge is
omitted.

The text delay is one flag for every caller of `1521:04ac`, not a choice
of each: running text is typed a character at a time wherever it is
printed while the flag is set. Startup and a new game set it (`3e99:0560`,
`099d`); `PRINT` sets it while it prints and clears it after (`2fd3:0ae3`,
`0b62`); `ENCOUNTER MENU` sets it at its start and clears it as it returns
(`2fd3:23f6`, `2aae`); combat setup clears it (`3cb2:1c5e`), and the end
of the battle sets it (`3995:0297`, after `3995:004b`) as does the end of
combat (`351b:1b00`, not in the demonstration, where `3995:0297` has set it
already). Nothing else writes it, and it is not saved. So
`PRINT` and the encounter's texts are always typed; and after a battle,
or from the start of a game, until the next `PRINT` or encounter, so is
every other running text: "The monsters rejoice for the party has been
destroyed", the NPCs' shares and the treasure left, the temple's and
shops' texts, `ADD EP`'s and `DAMAGE`'s, a camp's say (`6346:1883`), the
items' texts (`546c`) and `VERTICAL MENU`'s prompt, which is otherwise
printed at once (after a `PRINT`, as it usually is). In combat it is
off.

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
| `cok_menu_parse` | `3775:176e` (`cok_menu_mark`; dkk `3c1e:152d`), then `cok_menu_layout` (`67b5:00da`) |
| `cok_menu_draw` | `67b5:01e9` |
| `cok_menu_horizontal` | `3775:1885` with the key loop `67b5:03e2` |
| `cok_menu_read` | `67b5:03e2` as overlay `475c` calls it |
| `cok_menu_list` | `67b5:1368` as `3775:1990` calls it |

The selected item is drawn in colour 0 on the highlight colour. Left and
right arrows, and 4 and 6, move the selection; Enter returns the selected
item's hotkey; Escape and space are ignored. Other extended keys, and the
keypad digits 1-9 and `\` (mapped to scan codes by the table at `DS:1fdf`),
go to a `special` hook: `3775:1885` passes them to `546c:3334` and redraws
the party (see [Adventure loop](adventure.md#adventure-loop)). Because that check follows the hotkey match,
a hotkey 1-9 can only be chosen with the arrows and Enter. With one item, the
arrows and 4 and 6 are special too. A click (Enter with `DS:8987`) goes to
the regions of The mouse and the joystick; it also animates the small picture while it
waits when its caller asks (its flag at `[bp+0xa]`, `game->menu_animates`,
which a front end that waits does: see [Playing](front-end.md#playing)); flushes the keyboard
(`1614:045c`, `cok_keyboard.flush`) once it has its key, so that keys typed
ahead are lost; and times out when `DS:6e11` is set, as only the title's menu does
(see [The start menu](start-menu.md#the-start-menu)): `cok_menu_timed` returns `DS:6e15`'s result when the
keyboard gives `COK_KEY_TIMEOUT`, the time passed with no key.

`cok_menu_list` shows a list in a window with a `Select` menu on row 24,
adding `Next` and `Prev` when there are more rows to page to, and `Exit` if
asked. Up and down (8 and 2) move the selected row, wrapping within the rows
shown; PgUp and PgDn (9 and 3), and `Prev` and `Next`, page. Escape or `Exit`
cancels but leaves the index where it was. The first row shown persists
between lists (`DS:6e0d`) as in the original, which can leave the selected
row outside the window. ECL lists have no headings; the spell lists do (see
[Camp](camp.md#camp)).

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

Death Knights' menus go by index (`dkk 6a69:0f1b`, the descriptor's
`menus.routine`, `COK_MENUS_INDEXED`): `cok_menu_pick` takes the text of
every item the menu may have and the list of those it shows
(`cok_menu_items`, `DS:c120`'s 20 entries, filled by `6a69:00cb`,
`cok_menu_items_upto`, and `00ab`, `cok_menu_items_add`), and returns the
index of the item chosen rather than its letter. `cok_menu_indexed` lays it
out (`6a69:0d1e`, `0135`): each item of the text starts at a letter or digit
and runs to the next, its spaces with it; those listed are copied in order
and one trailing space deleted, their first characters the hotkeys
(`DS:c121`); an `@` in an item puts the items after it on a second row, the
first on row 23 and the second on row 24 from the same column, after the
prompt, which is always on row 24 (`6a69:025d`). Colours are Champions'
(`DS:c148` 10, `c14a` 15, `c149` 13 for the prompt, set once). Keys
(`6a69:0f1b`): a hotkey returns its item's index and Enter the selected
one's; Escape returns the special key 0x1b; an extended key returns its scan
code, but Left and Right, which move the selection, wrapping, and Up and
Down, which change rows by as many items as the first row holds (`0c8c`),
where there are items to move among: those read on (`0c26`, `0c59` and
`0c8c` clear the loop's flag, `[bp-0x13e]`); 4 and 6 move the selection and 2, 8, H and P
change rows where no item has them as hotkeys; with the keypad flag the
digits and `\` are their scan codes (`DS:5791`, Champions' `DS:1fdf`: the 5
is 0x20); Space and other keys are read past; the keyboard is flushed once
it has its key. The mouse's regions (`6a69:0490`-`0ba7`, test and act
routines in pairs) are not taken yet: a click is a key. Death Knights' Yes/No (`6a69:2048`) is `cok_camp_yes_no` through this
loop: `Yes No` by index from No, special keys read past, the prompt always
light magenta. Its lists (`6a69:1b82`) are `cok_menu_indexed_rows`:
the rows as `cok_menu_rows` shows, marks and moves through them
(`6a69:1506`, `170d`, `1a73`, `19db`: 67b5's, but a heading unmarked in the
normal colour, `17ab`), the menu on row 24 by index with the keypad's
directions, the caller's text and items with ` Next`, ` Prev` and ` Exit`
after them as `cok_menu_rows` offers them, each at the index after the one
before from the caller's last + 1, which the caller's items must reach
without a gap for them to show (`6a69:0d1e` stops at the first unused
entry, `DS:c120` being 0xff past them): the items menu, whose last is 8
with fewer items before its own Exit (9), never shows them. An index past
the last is the list's own (Next and Prev page while offered, Exit and
Escape return 0x1b as a special key with no row picked: so the items
menu's Exit pages down while there are rows below, which 16 items in 18
rows never leave); otherwise the index is returned with the row picked.
Up and Down (8 and 2) move, PgDn and PgUp (3 and 9) page, the scan code
0x4e pages down, 0x45 and 0 return as Escape; the rows' clicks
(`DS:c118`, `c119`, `c0fe`) are not taken.

| Function | Original |
| --- | --- |
| `cok_menu_items_upto`, `cok_menu_items_add` | `dkk 6a69:00cb`, `00ab` |
| `cok_menu_indexed` | `dkk 6a69:0d1e`, `0135` |
| `cok_menu_pick` | `dkk 6a69:0f1b` with `025d`, `0442`, `0c26`, `0c59`, `0c8c`, `0e74` |
| `cok_menu_indexed_rows` | `dkk 6a69:1b82` with `1506`, `170d`, `17ab`, `1a73`, `19db`, `1b3b` |

`src/screen.h` draws the screen frames from the root segment `0x128` with
tile set 4, glyphs 0x100-0x127, which the game loads from `8X8D1.DAX` record
202 at startup (`6e22:0050`). Like every tile set it is loaded with colour 13
transparent (`127f:0111` with 13 and 1), which clears those pixels; the
frame is drawn opaque, so they show black. `cok_screen_draw` draws a
screen (`cok_screen`) as the game's descriptor lays it out (its `frame`
section, see [Games](games.md#games)): the screen under it, the box it clears, then its rows
and columns of tiles. Champions' use tiles 0x114 and up; the tables of tile
values are at `DS:0e3a`, `0e62`, `0e7a`, `0ea1` and `0eae`.
The frame (`1128:0000`) clears the inside and draws the border, with
the three moons of Krynn on the top row at columns 8, 19 and 30, their tiles
offset by the phases in game words `0x4cf9`-`0x4cfb` (the `moons`
section's). The adventure screen
(`1128:0242`) adds the divider on row 16, the column 16 divider above it, and
the box of the 3D view from cell 2 to 14; a big picture's (`1128:0344`)
adds only the row 16 divider, for full-width pictures. `cok_adventure_screen`
draws one with the game's moons.

`START.EXE` is compressed with EXEPACK. `START.EXE.c` was decompiled from the
packed file, so its function addresses match the packed bytes, but far calls
and data references use the unpacked layout. Unpacked offsets are the Ghidra
address plus `0x239`; the data segment is `0x0bc6`. `GAME.OVR` calls the
graphics routines at `27f:offset` and the text routines at `521:offset`.

The SSI splash image in `TITLE.DAX` entry 0 (ID 1) decodes to 32,017 bytes:
a 17-byte header and 32,000 bytes of packed 4-bit pixels for a 320×200 image.
Its purple background was verified against the original in DOSBox.
