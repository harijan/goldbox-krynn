#include "queen_arena.h"

#include "data.h"
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
    if (under_overlay(value)) {
        snprintf(error, size, "the battle's cloud or body (value 0x%02x) is not drawn yet "
                 "(dqk 5837:172c, 137a)", value);
        return false;
    }
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

bool cok_queen_arena_cell(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat, int x, int y, char *error, size_t size)
{
    int column = x - combat->view_x, row = y - combat->view_y;
    if (column < 0 || row < 0 || column >= COK_QUEEN_ARENA_VIEW || row >= COK_QUEEN_ARENA_VIEW)
        return true;
    if (x < 0 || y < 0 || x >= COK_COMBAT_WIDTH || y >= COK_COMBAT_HEIGHT) {
        snprintf(error, size, "the battle's cell %d,%d is past the map (DS:bedb)", x, y);
        return false;
    }
    return draw(screen, tiles, rules, combat->cells[y][x], (unsigned)row, (unsigned)column, error,
                size);
}

bool cok_queen_arena_view(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat, char *error, size_t size)
{
    for (int row = 0; row < COK_QUEEN_ARENA_VIEW; ++row)
        for (int column = 0; column < COK_QUEEN_ARENA_VIEW; ++column) {
            int x = combat->view_x + column, y = combat->view_y + row;
            if (x < 0 || y < 0 || x >= COK_COMBAT_WIDTH || y >= COK_COMBAT_HEIGHT) {
                snprintf(error, size, "the battle's view has a cell %d,%d past the map (DS:bedb)",
                         x, y);
                return false;
            }
            if (!draw(screen, tiles, rules, combat->cells[y][x], (unsigned)row, (unsigned)column,
                      error, size))
                return false;
        }
    return true;
}
