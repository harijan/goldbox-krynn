#include "queen_arena.h"

#include "cloud.h"
#include "data.h"
#include "queen.h"
#include "vga.h"

#include <stdio.h>

/* 5837:172c:1784-1790: a value of 0 or past 0x4c is drawn as 0x15. */
static uint8_t shown(uint8_t value)
{
    return value == 0 || value > 0x4c ? 0x15 : value;
}

/* 5837:172c:17ac-17ca: the values whose ground is under a cloud or a body
 * (their overlay, COMSPR's record tile, is drawn in mode 1 over it). */
static bool under_overlay(uint8_t value)
{
    return (value >= 0x1b && value <= 0x1f) || value == 0x46;
}

/* The picture of a cell's value drawn at view row, column: the value's
 * tile, record tile + 1 of the set (vga.h's cok_vga_draw_record puts a 24
 * by 24 record at its place, its hotspot 0, 0). */
static bool draw(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                 uint8_t value, unsigned row, unsigned column, char *error, size_t size)
{
    value = shown(value);
    if (value >= cok_data_count(rules->data, COK_DATA_TERRAIN, 0)) {
        snprintf(error, size, "terrain value 0x%02x is past the table (DS:6197)", value);
        return false;
    }
    unsigned tile = cok_combat_terrain(rules, value).tile;
    if (!cok_vga_draw_record(screen, tiles, tile + 1,
                             COK_QUEEN_ARENA_Y + COK_QUEEN_ARENA_CELL * (int)row,
                             COK_QUEEN_ARENA_X + COK_QUEEN_ARENA_CELL * (int)column, NULL, NULL)) {
        snprintf(error, size, "tile %u of the battle's set is not there (dqk 5837:172c, DS:6197)",
                 tile + 1);
        return false;
    }
    return true;
}

/* 5837:172c:17ac-18d4 for a cell under a cloud or a body (under_overlay): the ground under it, the
 * value 137a gives (cok_cloud_under; for 0x1e and 0x1f, the last body's cell at x, y, looked
 * through again), then over it COMSPR's icon in the slot of the cell's own tile + 0x0f (image 0,
 * masked). */
static bool overlay(cok_adventure *game, const hlib_library *tiles, int x, int y, unsigned row,
                    unsigned column, char *error, size_t size)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_combat *combat = &game->combat;
    uint8_t value = combat->cells[y][x];
    uint8_t under = cok_cloud_under(game, (int8_t)x, (int8_t)y, value);
    if (under == 0x1e || under == 0x1f) {
        for (unsigned k = 1; k <= combat->bodies && k <= COK_COMBAT_BODIES; ++k) {
            const cok_combat_body *b = &combat->body[k - 1];
            if (b->x == x && b->y == y) under = b->cell;
        }
    }
    if (!draw(&game->screen, tiles, rules, under, row, column, error, size)) return false;
    if (value >= cok_data_count(rules->data, COK_DATA_TERRAIN, 0)) {
        snprintf(error, size, "terrain value 0x%02x is past the table (DS:6197)", value);
        return false;
    }
    unsigned slot = cok_combat_terrain(rules, value).tile + 0x0f;
    const uint8_t *record = NULL;
    size_t record_size = 0;
    if (!queen_icon(game, slot, 0, &record, &record_size)) {
        snprintf(error, size, "icon slot %u is past the table (DS:b17d, dqk 5837:172c)", slot);
        return false;
    }
    return cok_queen_arena_icon(&game->screen, record, record_size, (int)column, (int)row, 0,
                                error, size);
}

/* One map cell (x, y) in the view at row, column: an overlay's cell or a plain one. */
static bool cell(cok_adventure *game, const hlib_library *tiles, int x, int y, unsigned row,
                 unsigned column, char *error, size_t size)
{
    if (under_overlay(game->combat.cells[y][x]))
        return overlay(game, tiles, x, y, row, column, error, size);
    return draw(&game->screen, tiles, cok_game_rules(game), game->combat.cells[y][x], row, column,
                error, size);
}

bool cok_queen_arena_cell(cok_adventure *game, const hlib_library *tiles, int x, int y, char *error,
                          size_t size)
{
    const cok_combat *combat = &game->combat;
    int column = x - combat->view_x, row = y - combat->view_y;
    if (column < 0 || row < 0 || column >= COK_QUEEN_ARENA_VIEW || row >= COK_QUEEN_ARENA_VIEW)
        return true;
    if (x < 0 || y < 0 || x >= COK_COMBAT_WIDTH || y >= COK_COMBAT_HEIGHT) {
        snprintf(error, size, "the battle's cell %d,%d is past the map (DS:bedb)", x, y);
        return false;
    }
    return cell(game, tiles, x, y, (unsigned)row, (unsigned)column, error, size);
}

/* A multiple of 4 at or below x (the unchained VGA's byte column, as 269b:0a9c's). */
static int down4(int x)
{
    return x >= 0 ? x & ~3 : -((-x + 3) & ~3);
}

bool cok_queen_arena_icon(cok_picture *screen, const uint8_t *record, size_t size, int x, int y,
                          unsigned facing, char *error, size_t error_size)
{
    /* 269b:0a9c: NULL draws nothing (at facing <= 3). Above 3, 030f:034b copies the empty
     * entry into the scratch slot 0x98 (19a4:0881:08f5-0921 does not reallocate for a size 0
     * source, so 0977 copies nothing and 0x98 keeps the last mirrored picture), and that stale
     * picture is drawn: not ported, so a stop. */
    if (record == NULL) {
        if (facing <= 3) return true;
        snprintf(error, error_size,
                 "an empty icon faced above 3 draws the scratch slot 0x98's last picture (dqk "
                 "204c:030f:034b, 19a4:0881:08f5)");
        return false;
    }
    cok_vga_image image = {0};
    if (cok_vga_decode(record, size, NULL, &image) != COK_VGA_OK) {
        snprintf(error, error_size, "a combat icon is not a picture (dqk 269b:0a9c)");
        return false;
    }
    /* 030f:0342: a facing above 3 mirrors the picture (0881 to the scratch slot, 2796:000c). */
    if (facing > 3 && !cok_vga_mirror(&image)) {
        snprintf(error, error_size, "Invalid type (%x) to Mirror (dqk DS:9db6, 2796:000c)",
                 image.format);
        cok_vga_free(&image);
        return false;
    }
    /* 030f:0314-0340: the cell's pixel, less the hotspot (decode keeps the header's i16 at 2
     * as image.x, the row's, and at 4 as image.y, the column's). The y < -900 quirk
     * (030f:0329) is not reached: a cell is 0..6. */
    const cok_vga_clip view = {COK_QUEEN_ARENA_Y, COK_QUEEN_ARENA_X,
                               COK_QUEEN_ARENA_Y + COK_QUEEN_ARENA_CELL * COK_QUEEN_ARENA_VIEW,
                               COK_QUEEN_ARENA_X + COK_QUEEN_ARENA_CELL * COK_QUEEN_ARENA_VIEW};
    int row = COK_QUEEN_ARENA_Y + COK_QUEEN_ARENA_CELL * y - image.x;
    int col = down4(COK_QUEEN_ARENA_X + COK_QUEEN_ARENA_CELL * x - image.y);
    cok_vga_draw(screen, &image, col, row, &view, NULL);
    cok_vga_free(&image);
    return true;
}

int cok_queen_arena_place(int unit)
{
    return COK_QUEEN_ARENA_X + 8 * unit;
}

enum { SAVE_SIZE = 12 };

/* The pixel at x, y of the screen as an index, or -1 off it. */
static long pixel_index(const cok_picture *screen, int x, int y)
{
    int width = (int)screen->units * 8;
    if (x < 0 || y < 0 || x >= width || y >= (int)screen->height) return -1;
    return (long)y * width + x;
}

void cok_queen_arena_save(const cok_picture *screen, int x, int y, uint8_t under[144])
{
    for (int row = 0; row < SAVE_SIZE; ++row)
        for (int column = 0; column < SAVE_SIZE; ++column) {
            long at = pixel_index(screen, x + column, y + row);
            under[row * SAVE_SIZE + column] = at >= 0 ? screen->pixels[at] : 0;
        }
}

void cok_queen_arena_restore(cok_picture *screen, int x, int y, const uint8_t under[144])
{
    for (int row = 0; row < SAVE_SIZE; ++row)
        for (int column = 0; column < SAVE_SIZE; ++column) {
            long at = pixel_index(screen, x + column, y + row);
            if (at >= 0) screen->pixels[at] = under[row * SAVE_SIZE + column];
        }
}

void cok_queen_arena_missile(cok_picture *screen, const cok_vga_image *image, int x, int y)
{
    /* As cok_queen_arena_icon's: the hotspot less, the column down to a multiple of 4. The clip is
     * the screen's, as the clip is at the battle's first turn (DS:cdae and cdb3 0, cdb5 320, cdb9
     * 200, read in the box); the view's clip (5837:1367) is not checked during a flight. */
    int row = y - image->x, col = down4(x - image->y);
    cok_vga_draw(screen, image, col, row, NULL, NULL);
}

bool cok_queen_arena_view(cok_adventure *game, const hlib_library *tiles, char *error, size_t size)
{
    const cok_combat *combat = &game->combat;
    for (int row = 0; row < COK_QUEEN_ARENA_VIEW; ++row)
        for (int column = 0; column < COK_QUEEN_ARENA_VIEW; ++column) {
            int x = combat->view_x + column, y = combat->view_y + row;
            if (x < 0 || y < 0 || x >= COK_COMBAT_WIDTH || y >= COK_COMBAT_HEIGHT) {
                snprintf(error, size, "the battle's view has a cell %d,%d past the map (DS:bedb)",
                         x, y);
                return false;
            }
            if (!cell(game, tiles, x, y, (unsigned)row, (unsigned)column, error, size))
                return false;
        }
    return true;
}
