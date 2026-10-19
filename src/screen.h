#ifndef COK_SCREEN_H
#define COK_SCREEN_H

#include "picture.h"
#include "rules.h"

#include <stdint.h>

/* Screen frames, drawn with tile set 4, the frame's (glyphs 0x100 on,
 * loaded from 8X8D1.DAX record 202 at startup, 6e22:0050): each screen a
 * routine of the original's that clears a box and draws rows and columns
 * of tiles, as the game's descriptor holds them (cok_rules_frame, rules.h).
 * Tiles are one cell, drawn opaque; a missing tile draws nothing.
 * Coordinates are in cells.
 *
 * Champions' (1128): the frame (1128:0000) clears cells 1-38 by 1-22 and
 * draws row 0 with the three moons at columns 8, 19 and 30 (their phases,
 * game words 0x4cf9-0x4cfb, the moons section's tiles), columns 0 and 39
 * joining a divider on row 16, and row 23; the open frame's sides run
 * through rows 15-17. The adventure screen (1128:0242) adds row 16, the
 * column 16 divider above it, and the 3D view's box from cells 2 to 14,
 * whose inside starts at cell 3; text prints in rows 17-22 and menus on
 * row 24. A big picture's (1128:0344) adds row 16 alone, the picture at
 * cell 1. Memorize's lists (1128:0384) clear only rows 1-16, so that rows
 * 17-22 keep the table drawn there. The other spell lists (1128:077c): the
 * open frame, a row across row 2 and tile 3 on each side of row 3. The
 * combat screen (1128:04c1) clears the whole screen but row 24: the moons'
 * row, columns 0, 22 and 39 to row 22, then row 22 across; the map shows
 * inside the box of cells 1-21, the side panel in cells 23-38. The
 * character sheet (1128:05fc): the whole screen but row 24 cleared, the
 * moons' row, rows 8, 16, 20 and 23 across, the sides to row 22, and column
 * 19 from row 9 to 19. The credits (1128:0130): rows 0, 4, 20 and 23
 * across, without moons, and columns 0 and 39 between, making three boxes.
 *
 * Death Knights' (110f) are the same screens in other tiles of its set
 * (from 0x100): one frame, its sides unbroken, row 23 and the dividers
 * with their own end tiles, the 3D view's box with corners. */

/* Draw screen's frame (rules' frame section) into dst with the frame's
 * tiles; moons, the three moons' phases, may be NULL for a screen without
 * them. */
void cok_screen_draw(cok_picture *dst, const cok_picture *tiles, const cok_rules *rules,
                     cok_screen screen, const uint16_t moons[3]);

#endif
