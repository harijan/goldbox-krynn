#ifndef COK_QUEEN_ARENA_H
#define COK_QUEEN_ARENA_H

#include "combat.h"
#include "hlib.h"
#include "picture.h"
#include "rules.h"

#include <stdbool.h>

/* The Dark Queen of Krynn's combat ground (adventure.h's cok_drawing
 * arena hooks, drawn by dark_queen.c): the map's 7 by 7 cells shown, each
 * 24 by 24 pixels, on the VGA screen at 8, 8 (the battle's frame: the
 * screen's ground beside the side panel, as the original's first turn
 * shows it). A cell's picture is the tile of its terrain value (the
 * terrain table's tile, cok_combat_terrain), record tile + 1 of the
 * battlefield's set (record 0 is the set's palette). The clouds and the
 * bodies' icons over a cell (dkk 6f03:010d) and the combatants' icons are
 * not drawn here: parts 11-12. */

enum {
    COK_QUEEN_ARENA_X = 8,     /* The view's left column, in pixels. */
    COK_QUEEN_ARENA_Y = 8,     /* Its top row. */
    COK_QUEEN_ARENA_CELL = 24, /* A cell's width and height. */
    COK_QUEEN_ARENA_VIEW = 7,  /* Cells across and down. */
};

/* Draw map cell x, y (map coordinates) at its place in the view, if it is
 * in the view (nothing otherwise). False where the map or the set has no
 * such cell or tile. */
bool cok_queen_arena_cell(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat, int x, int y);
/* Draw the view: its 7 by 7 cells from the view's corner (combat->view_x,
 * view_y), each as cok_queen_arena_cell draws it. False where a cell is
 * past the map (the original reads the heap there) or lacks its tile. */
bool cok_queen_arena_view(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat);

#endif
