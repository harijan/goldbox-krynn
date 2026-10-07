#ifndef COK_VIEW_H
#define COK_VIEW_H

#include "picture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The 3D view of the adventure screen, from overlay 69ea, drawn with the 8x8
 * tiles of overlay 6e22. A view is built from a GEO map, up to three WALLDEF
 * wall sets and the tile sets those name. The original draws into a buffer
 * (DS:4b78) that 127f:12e8 copies to the screen one unit right and one cell
 * down; these functions draw on the screen itself, with that offset added,
 * so the view fills cells 3-13 across and down. */

enum {
    COK_VIEW_SIZE = 16,          /* Map squares across and down. */
    COK_VIEW_MAP = 0x400,        /* DS:6d7c: four 256-byte tables. */
    COK_VIEW_WALL_TYPE = 0x9c,   /* Tiles of one wall type at the 10 view places. */
    COK_VIEW_WALL_SET = 0x30c,   /* Five wall types, one WALLDEF set. */
    COK_VIEW_WALL_SETS = 3,      /* Wall types 1-5, 6-10, 11-15. */
    COK_VIEW_TILE_SETS = 5,      /* DS:614a. */
    COK_VIEW_SKY_PICTURES = 3,   /* DS:6d96, 6d9a, 6d9e. */
};

typedef struct {
    /* The GEO record after its two-byte header. Per square, row-major:
     * 0x000 north wall type (high nibble) and east (low), 0x100 south and
     * west, 0x200 a square byte, 0x300 two bits per side, west highest. */
    uint8_t map[COK_VIEW_MAP];
    uint8_t walls[COK_VIEW_WALL_SETS * COK_VIEW_WALL_SET]; /* DS:6d78. */
    /* Tile values 1-0x2d, 0x2e-0x73, 0x74-0xb9, 0xba-0xff and 0x100-0x127,
     * from 8X8D records; set 4 is the screen frame's. NULL pixels if absent. */
    cok_picture tiles[COK_VIEW_TILE_SETS];
    /* SKY.DAX 250-252: a strip over the horizon seen facing north, the sun,
     * and the horizon itself. */
    cok_picture sky[COK_VIEW_SKY_PICTURES];
    /* Squares off the map read from the far edge (DS:8846 is neither 0 nor
     * 0x50); otherwise they have no walls. */
    bool wrap;
} cok_view;

/* What the area words choose for the backdrop (6945:00ba). */
typedef struct {
    uint8_t sky;     /* DS:6d80, from DS:0dc4 by 0x4bfd or 0x4bfe. */
    uint8_t horizon; /* DS:6d82, set only in CGA mode; 0 otherwise. */
    uint8_t ground;  /* DS:6d83, 8 outside CGA mode. */
    uint16_t hour;   /* 0x4bc9, places the sun. */
} cok_view_backdrop;

/* Free the pictures; the view may then be reused. */
void cok_view_free(cok_view *view);

/* Load a GEO record (69ea:130d). Returns false unless size is 0x402. */
bool cok_view_set_map(cok_view *view, const uint8_t *record, size_t size);

/* Copy a WALLDEF record into sets slot 1-3 onward and move its tile values
 * from 0x2d up to the slot's tile set (69ea:1025). Returns the sets copied,
 * which name their tiles by 8X8D record id, or id * 10 + 1 and up for more
 * than one set; 0 if slot is out of range, the record is empty or not whole
 * sets, or the sets run past slot 3. The original halts on those. */
size_t cok_view_set_walls(cok_view *view, unsigned slot, const uint8_t *record, size_t size);

/* Wall type 0-15 on side dir (0, 2, 4, 6: north, east, south, west) of
 * square x, y (69ea:06a2). Coordinates are taken as bytes, as the original
 * does; off the map they wrap by one square, or are 0. */
uint8_t cok_view_wall(const cok_view *view, unsigned dir, int x, int y);

/* The square byte (69ea:07a5), off the map as cok_view_wall. */
uint8_t cok_view_square(const cok_view *view, int x, int y);

/* How the party can leave square x, y by side dir (69ea:0573): 1 if that
 * side has no wall, else the side's two bits in the fourth table: 0 a solid
 * wall, 1 a way through, 2 a locked door and 3 one that cannot be picked.
 * Off the map with no wrap, 0. */
uint8_t cok_view_passage(const cok_view *view, unsigned dir, int x, int y);

/* Make side dir of square x, y a way through: its two bits in the fourth
 * table become 1 (475c:0148). Squares off the map are left alone. */
void cok_view_open(cok_view *view, unsigned dir, int x, int y);

/* Move x, y one square in direction dir 0-7 (DS:1ed6, DS:1edf). */
void cok_view_step(unsigned dir, int *x, int *y);

/* Draw tile value 1-0x127 with its top-left at cell x, y of dst, masked or
 * opaque (6e22:01ab, without the buffer offset). Values outside the tile
 * sets, which the original treats as fatal, and missing sets draw nothing. */
void cok_view_tile(cok_picture *dst, const cok_view *view, unsigned value, int x, int y,
                   bool masked);

/* Draw the backdrop and the walls seen from square x, y facing dir (69ea:0184
 * outside CGA mode, then 69ea:0820). */
void cok_view_draw(cok_picture *dst, const cok_view *view, int x, int y, unsigned dir,
                   const cok_view_backdrop *backdrop);

/* Draw the overhead map (69ea:000f) in place of the view, as 69ea:0820 does
 * when DS:6d84 is set: the 11 by 11 squares around square x, y, kept on the
 * map (the first column and row 0-5), each an opaque tile of the frame's
 * set, 0x104 plus 1, 2, 4 and 8 for a wall of any type on its north, east,
 * south and west sides, then over the party's square the party's arrow,
 * 0x100 + dir / 2. Coordinates and dir are taken as bytes. Returns false,
 * drawing nothing, for a square off the map, where the original draws the
 * arrow outside the map's window, unclipped, and the screen then shows
 * what its buffer held there, and for an arrow past the tile sets, where
 * it halts. */
bool cok_view_overhead(cok_picture *dst, const cok_view *view, int x, int y, unsigned dir);

/* Sky colour for an area word (DS:0dc4). */
uint8_t cok_view_sky_color(uint16_t value);

#endif
