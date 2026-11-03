#ifndef COK_QUEEN_ARENA_H
#define COK_QUEEN_ARENA_H

#include "combat.h"
#include "hlib.h"
#include "picture.h"
#include "rules.h"

#include <stdbool.h>
#include <stddef.h>

/* The Dark Queen of Krynn's combat ground (adventure.h's cok_drawing
 * arena hooks, drawn by dark_queen.c): the map's 7 by 7 cells shown, each
 * 24 by 24 pixels, on the VGA screen at 8, 8 (the battle's frame: the
 * screen's ground beside the side panel, as the original's first turn
 * shows it, dqk 5837:06e6 and 172c). A cell's picture is the tile of its
 * terrain value (the terrain table's tile, DS:6197's fourth byte), record
 * tile + 1 of the battlefield's set (record 0 is the set's palette). A value
 * of 0 or past 0x4c is drawn as 0x15 (5837:172c:1784-1790).
 *
 * The combatants' icons are drawn over the cells (cok_queen_arena_icon, from
 * dark_queen.c's arena_icon, and the cursor's box as an icon). Not drawn yet,
 * and a cell that needs them stops (false, with error set, citing the
 * original): the clouds and the bodies (values 0x1b-0x1f and 0x46: the ground
 * beneath them, 5837:137a's lists DS:c2d6 and DS:c526, then COMSPR's overlay
 * over it, 172c:17ac-18d4), and the skull (part 12). */

enum {
    COK_QUEEN_ARENA_X = 8,     /* The view's left column, in pixels. */
    COK_QUEEN_ARENA_Y = 8,     /* Its top row. */
    COK_QUEEN_ARENA_CELL = 24, /* A cell's width and height. */
    COK_QUEEN_ARENA_VIEW = 7,  /* Cells across and down. */
};

/* Draw map cell x, y (map coordinates) at its place in the view, if it is
 * in the view (nothing otherwise). False with error set where the map or
 * the set has no such cell or tile, or the cell needs what is not drawn. */
bool cok_queen_arena_cell(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat, int x, int y, char *error, size_t size);
/* Draw a combatant's icon record (a picture: CPIC's or CBODY's, or COMSPR's cursor
 * box) at the view's cell x, y, facing facing, as dqk 204c:030f does. A NULL record
 * draws nothing at facing <= 3 (269b:0a9c's 0ad7: an empty slot); above 3 it stops
 * (the original draws the scratch slot 0x98's stale picture, see the .c). Its picture
 * is mirrored where facing > 3 (030f:0342, 2796:000c). Drawn at 24 x + 8, 24 y + 8
 * less its hotspot, the column taken down to a multiple of 4 (269b:0a9c), and
 * clipped to the view (5837:1367's rows and columns 8-175). The mode byte 030f
 * passes (0515's 1) is not read (0515 pushes only its first three arguments). False
 * with error set where
 * the record is not a picture, a mirror is refused, or an empty record faces above 3. */
bool cok_queen_arena_icon(cok_picture *screen, const uint8_t *record, size_t size, int x, int y,
                          unsigned facing, char *error, size_t error_size);
/* Draw the view: its 7 by 7 cells from the view's corner (combat->view_x,
 * view_y), each as cok_queen_arena_cell draws it. False with error set
 * where a cell is past the map (the original reads the heap there), lacks
 * its tile, or needs what is not drawn. */
bool cok_queen_arena_view(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat, char *error, size_t size);

#endif
