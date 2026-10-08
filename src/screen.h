#ifndef COK_SCREEN_H
#define COK_SCREEN_H

#include "picture.h"

#include <stdbool.h>
#include <stdint.h>

/* Screen frames from the root segment 0x128, drawn with tile set 4: glyphs
 * 0x100-0x127, loaded from 8X8D1.DAX record 202 at startup (6e22:0050). The
 * frame draws tiles 0x14 and up of the set (glyph 0x114). Tiles are one
 * cell; a missing tile draws nothing. Coordinates are in cells. */

/* Frame tile values the layouts use, added to 0x14. */
enum {
    COK_FRAME_HORIZONTAL = 0,
    COK_FRAME_VERTICAL = 4,
};

/* Clear cells 1-38 by 1-22 to colour 0 and draw the outer frame: row 0 with
 * the three moons at columns 8, 19 and 30 (tiles 10, 6 and 14 past their
 * phase, game words 0x4cf9-0x4cfb), columns 0 and 39, and row 23 (1128:0000).
 * Without open, the side columns join a divider on row 16. */
void cok_screen_frame(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3],
                      bool open);

/* The adventure screen (1128:0242): the frame, the divider on row 16, the
 * column 16 divider above it, and the 3D view's box from cells 2 to 14,
 * whose inside starts at cell 3. Text prints in rows 17-22 and menus on
 * row 24. */
void cok_screen_adventure(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3]);

/* The frame for a large picture (1128:0344): the frame and the row 16
 * divider. The picture goes at cell 1. */
void cok_screen_big(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3]);

/* The frame of the Memorize lists (1128:0384): as the big picture's, but
 * clearing only rows 1-16, so that rows 17-22 keep the table drawn there.
 * Its right side comes from DS:0e8b, which holds the same values. */
void cok_screen_spells(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3]);

/* The frame of the other spell lists (1128:077c): the open frame
 * (1128:0000 with 1), a row of tiles across row 2, and tile 3 on each side
 * of row 3. */
void cok_screen_list(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3]);

/* The combat screen's frame (1128:04c1): the whole screen but row 24
 * cleared, the moons' row, columns 0, 22 and 39 to row 22 (DS:0ebd,
 * 0ed4 and 0eeb, which hold the same), then row 22 across. The map shows
 * inside the box of cells 1-21, the side panel in cells 23-38. */
void cok_screen_combat(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3]);

/* The character sheet's frame (1128:05fc): the whole screen but row 24
 * cleared, the moons' row, rows 8, 16, 20 and 23 across, the sides (from
 * DS:0f02) to row 22, and column 19 from row 9 to 19 (DS:0f19). */
void cok_screen_sheet(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3]);

/* The credits' frame (1128:0130): cells 1-38 by 1-22 cleared, rows 0, 4,
 * 20 and 23 across, without moons, and columns 0 and 39 between, making
 * three boxes. */
void cok_screen_credits(cok_picture *dst, const cok_picture *tiles);

#endif
