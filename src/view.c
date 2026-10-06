#include "view.h"

#include <string.h>

/* The buffer the original draws into sits one unit right and one cell down
 * on the screen (127f:12e8). */
enum { BUFFER_X = 1, BUFFER_Y = 1 };

/* Steps for directions 0-7 (DS:1ed6, DS:1edf); north is up. */
static const int8_t step_x[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int8_t step_y[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

/* The ten places a wall type has tiles for: where each starts in its
 * record (DS:0df4), its size in cells (DS:0dfe, DS:0e08), and its column and
 * row in the 11 by 11 view (DS:0e12, DS:0e26). 0 is a front wall two squares
 * ahead, 9 the edge between two of those; 1 and 2 are side walls two ahead;
 * 3 a front wall one ahead, 4 and 5 its sides; 6 the front wall of the
 * party's square, 7 and 8 its sides. */
static const uint8_t place_start[10] = {0x00, 0x02, 0x06, 0x0a, 0x16,
                                        0x26, 0x36, 0x6e, 0x84, 0x9a};
static const uint8_t place_units[10] = {1, 1, 1, 3, 2, 2, 7, 2, 2, 1};
static const uint8_t place_rows[10] = {2, 4, 4, 4, 8, 8, 8, 11, 11, 2};
static const int place_x[10] = {5, 4, 6, 4, 2, 7, 2, 0, 9, 5};
static const int place_y[10] = {4, 3, 3, 3, 1, 1, 1, 0, 0, 4};

/* First tile value of each set (DS:1ec2). */
static const unsigned tile_base[COK_VIEW_TILE_SETS] = {0x01, 0x2e, 0x74, 0xba, 0x100};

/* DS:0dc4, then the recolour tables that follow it (DS:0dd4, DS:0de4). */
static const uint8_t sky_colors[48] = {
    0, 15, 4, 11, 13, 2, 9, 14, 0, 15, 4, 11, 13, 2, 9, 14,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 14, 15,
};

uint8_t cok_view_sky_color(uint16_t value)
{
    /* Larger values read further into the data segment; 0 here. */
    return value < sizeof sky_colors ? sky_colors[value] : 0;
}

void cok_view_free(cok_view *view)
{
    for (size_t i = 0; i < COK_VIEW_TILE_SETS; ++i) cok_picture_free(&view->tiles[i]);
    for (size_t i = 0; i < COK_VIEW_SKY_PICTURES; ++i) cok_picture_free(&view->sky[i]);
}

bool cok_view_set_map(cok_view *view, const uint8_t *record, size_t size)
{
    if (size != COK_VIEW_MAP + 2) return false;
    memcpy(view->map, record + 2, COK_VIEW_MAP);
    return true;
}

size_t cok_view_set_walls(cok_view *view, unsigned slot, const uint8_t *record, size_t size)
{
    if (slot < 1 || slot > COK_VIEW_WALL_SETS || size == 0 || size % COK_VIEW_WALL_SET != 0)
        return 0;
    size_t sets = size / COK_VIEW_WALL_SET;
    if (sets + slot > COK_VIEW_WALL_SETS + 1) return 0;
    for (size_t i = 0; i < sets; ++i) {
        size_t set = slot + i;
        uint8_t *walls = view->walls + (set - 1) * COK_VIEW_WALL_SET;
        memcpy(walls, record + i * COK_VIEW_WALL_SET, COK_VIEW_WALL_SET);
        /* Records name set 1's tiles; values below 0x2d are set 0's. */
        unsigned offset = tile_base[set] - tile_base[1];
        for (size_t j = 0; j < COK_VIEW_WALL_SET; ++j)
            if (walls[j] >= 0x2d) walls[j] = (uint8_t)(walls[j] + offset);
    }
    return sets;
}

static bool on_map(int x, int y)
{
    int8_t cx = (int8_t)x, cy = (int8_t)y;
    return cx >= 0 && cx < COK_VIEW_SIZE && cy >= 0 && cy < COK_VIEW_SIZE;
}

/* The map index of a square, or -1 for none (69ea:0542 and the edge wrap
 * shared by 69ea:06a2 and 69ea:07a5). */
static int square_index(const cok_view *view, int x, int y)
{
    if (!on_map(x, y) && !view->wrap) return -1;
    int8_t cx = (int8_t)x, cy = (int8_t)y;
    if (cx >= COK_VIEW_SIZE) cx = 0;
    if (cx < 0) cx = COK_VIEW_SIZE - 1;
    if (cy >= COK_VIEW_SIZE) cy = 0;
    if (cy < 0) cy = COK_VIEW_SIZE - 1;
    return cy * COK_VIEW_SIZE + cx;
}

uint8_t cok_view_wall(const cok_view *view, unsigned dir, int x, int y)
{
    int i = square_index(view, x, y);
    if (i < 0) return 0;
    switch (dir) {
    case 0: return view->map[i] >> 4;
    case 2: return view->map[i] & 15;
    case 4: return view->map[0x100 + i] >> 4;
    case 6: return view->map[0x100 + i] & 15;
    default: return 0; /* The original returns an unset value. */
    }
}

uint8_t cok_view_square(const cok_view *view, int x, int y)
{
    int i = square_index(view, x, y);
    return i < 0 ? 0 : view->map[0x200 + i];
}

void cok_view_tile(cok_picture *dst, const cok_view *view, unsigned value, int x, int y,
                   bool masked)
{
    if (value == 0) return;
    size_t set = COK_VIEW_TILE_SETS;
    while (set > 0 && value < tile_base[set - 1]) --set;
    if (set == 0 || value > 0x127) return;
    const cok_picture *tiles = &view->tiles[set - 1];
    if (tiles->pixels == NULL) return;
    cok_picture_draw(dst, tiles, value - tile_base[set - 1], x, y,
                     masked ? COK_DRAW_MASKED : 0, NULL);
}

/* Draw wall type type at place, at view column x and row y; tiles outside
 * the 11 by 11 view are skipped (69ea:0434). */
static void piece(cok_picture *dst, const cok_view *view, unsigned place, unsigned type, int x,
                  int y)
{
    if (type == 0) return;
    const uint8_t *tiles = view->walls + (type - 1) * COK_VIEW_WALL_TYPE + place_start[place];
    for (int row = y; row < y + place_rows[place]; ++row)
        for (int col = x; col < x + place_units[place]; ++col) {
            uint8_t value = *tiles++;
            if (row >= 0 && row < 11 && col >= 0 && col < 11 && value != 0)
                cok_view_tile(dst, view, value, col + 2 + BUFFER_X, row + 2 + BUFFER_Y, true);
        }
}

/* Front walls of four squares two ahead, from the middle out to one side,
 * with the edge piece where a wall ends (69ea:0820, first two loops). side
 * is the way to step, sign -1 to the left and 1 to the right. */
static void far_fronts(cok_picture *dst, const cok_view *view, unsigned dir, unsigned side,
                       unsigned other, int sign, int x, int y)
{
    int offset = 0;
    uint8_t last = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t wall = cok_view_wall(view, dir, x, y);
        if (!on_map(x, y) && cok_view_wall(view, other, x, y) == 0) last = 0;
        if (wall == 0) {
            if (last != 0 &&
                cok_view_wall(view, side, x - step_x[side], y - step_y[side]) != 0)
                piece(dst, view, 9, last, place_x[9] + offset - sign, place_y[9]);
            last = 0;
        } else {
            if (last != 0) piece(dst, view, 9, last, place_x[9] + offset - sign, place_y[9]);
            piece(dst, view, 0, wall, place_x[0] + offset, place_y[0]);
            last = wall;
        }
        offset += 2 * sign;
        x += step_x[side];
        y += step_y[side];
    }
}

/* Side walls of three squares two ahead (69ea:0820, third and fourth). */
static void far_sides(cok_picture *dst, const cok_view *view, unsigned side, unsigned place,
                      int sign, int x, int y)
{
    int offset = 0;
    for (int i = 0; i < 3; ++i) {
        uint8_t wall = cok_view_wall(view, side, x, y);
        if (wall != 0)
            piece(dst, view, place, wall, place_x[place] + offset + (i == 0 ? 0 : sign),
                  place_y[place]);
        offset += 2 * sign;
        x += step_x[side];
        y += step_y[side];
    }
}

/* Front and side walls of a row of squares, starting count squares to one
 * side and stepping back to the middle (69ea:0820, nearer rows). */
static void near_row(cok_picture *dst, const cok_view *view, unsigned dir, unsigned side,
                     unsigned front, unsigned place, int count, int width, int x, int y)
{
    unsigned toward = (side + 4) % 8;
    int sign = side == (dir + 6) % 8 ? -1 : 1;
    int offset = sign * width * count;
    x += step_x[side] * count;
    y += step_y[side] * count;
    for (int i = 0; i <= count; ++i) {
        uint8_t wall = cok_view_wall(view, dir, x, y);
        if (wall != 0) piece(dst, view, front, wall, place_x[front] + offset, place_y[front]);
        wall = cok_view_wall(view, side, x, y);
        if (wall != 0) piece(dst, view, place, wall, place_x[place] + offset, place_y[place]);
        offset -= sign * width;
        x += step_x[toward];
        y += step_y[toward];
    }
}

/* The backdrop outside CGA mode (69ea:0184). */
static void backdrop(cok_picture *dst, const cok_view *view, int x, int y, unsigned dir,
                     const cok_view_backdrop *b)
{
    int left = 2 + BUFFER_X, top = BUFFER_Y * 8;
    cok_picture_fill(dst, left, top + 0x10, 11, 0x2c, b->sky);
    cok_picture_fill(dst, left, top + 0x3c, 11, 2, b->horizon);
    cok_picture_fill(dst, left, top + 0x3e, 11, 0x2a, b->ground);
    const cok_picture *strip = &view->sky[0], *sun = &view->sky[1], *horizon = &view->sky[2];
    if (cok_view_square(view, x, y) < 0x80 && b->sky == 11) {
        int hour = b->hour, sun_x = -1, sun_y = 0;
        if (hour >= 1 && hour <= 5) {
            if (dir == 2) sun_x = 9, sun_y = 7 - hour;
            else if (dir == 4 && hour > 2) sun_x = hour - 1, sun_y = 7 - hour;
        } else if (hour >= 13 && hour <= 18) {
            if (dir == 6) sun_x = 2, sun_y = hour - 11;
            else if (dir == 4 && hour < 16) sun_x = hour - 6, sun_y = hour - 11;
        }
        if (sun_x >= 0 && sun->pixels != NULL)
            cok_picture_draw(dst, sun, 0, sun_x + BUFFER_X, sun_y + BUFFER_Y, COK_DRAW_MASKED,
                             NULL);
        if (dir == 0 && strip->pixels != NULL)
            cok_picture_draw(dst, strip, 0, 2 + BUFFER_X, 2 + BUFFER_Y, COK_DRAW_MASKED, NULL);
    }
    if (horizon->pixels != NULL)
        cok_picture_draw(dst, horizon, 0, 2 + BUFFER_X, 7 + BUFFER_Y, COK_DRAW_MASKED, NULL);
}

void cok_view_draw(cok_picture *dst, const cok_view *view, int x, int y, unsigned dir,
                   const cok_view_backdrop *b)
{
    dir &= 6;
    backdrop(dst, view, x, y, dir, b);
    unsigned left = (dir + 6) % 8, back = (dir + 4) % 8, right = (dir + 2) % 8;
    /* From two squares ahead back to the party's square. */
    x += step_x[dir] * 2;
    y += step_y[dir] * 2;
    far_fronts(dst, view, dir, left, right, -1, x, y);
    far_fronts(dst, view, dir, right, left, 1, x, y);
    far_sides(dst, view, left, 1, -1, x, y);
    far_sides(dst, view, right, 2, 1, x, y);
    x += step_x[back];
    y += step_y[back];
    near_row(dst, view, dir, left, 3, 4, 2, 3, x, y);
    near_row(dst, view, dir, right, 3, 5, 2, 3, x, y);
    x += step_x[back];
    y += step_y[back];
    near_row(dst, view, dir, left, 6, 7, 1, 7, x, y);
    near_row(dst, view, dir, right, 6, 8, 1, 7, x, y);
}
