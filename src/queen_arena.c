#include "queen_arena.h"

#include "data.h"
#include "vga.h"

/* The picture of map cell (x, y) drawn at view row, column: the terrain
 * value's tile, record tile + 1 of the set (vga.h's cok_vga_draw_record
 * puts a 24 by 24 record at its place, its hotspot 0, 0). */
static bool draw(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                 uint8_t value, unsigned row, unsigned column)
{
    if (value >= cok_data_count(rules->data, COK_DATA_TERRAIN, 0)) return false;
    unsigned tile = cok_combat_terrain(rules, value).tile;
    return cok_vga_draw_record(screen, tiles, tile + 1,
                               COK_QUEEN_ARENA_Y + COK_QUEEN_ARENA_CELL * (int)row,
                               COK_QUEEN_ARENA_X + COK_QUEEN_ARENA_CELL * (int)column, NULL,
                               NULL);
}

bool cok_queen_arena_cell(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat, int x, int y)
{
    int column = x - combat->view_x, row = y - combat->view_y;
    if (column < 0 || row < 0 || column >= COK_QUEEN_ARENA_VIEW || row >= COK_QUEEN_ARENA_VIEW)
        return true;
    if (x < 0 || y < 0 || x >= COK_COMBAT_WIDTH || y >= COK_COMBAT_HEIGHT) return false;
    return draw(screen, tiles, rules, combat->cells[y][x], (unsigned)row, (unsigned)column);
}

bool cok_queen_arena_view(cok_picture *screen, const hlib_library *tiles, const cok_rules *rules,
                          const cok_combat *combat)
{
    for (int row = 0; row < COK_QUEEN_ARENA_VIEW; ++row)
        for (int column = 0; column < COK_QUEEN_ARENA_VIEW; ++column) {
            int x = combat->view_x + column, y = combat->view_y + row;
            if (x < 0 || y < 0 || x >= COK_COMBAT_WIDTH || y >= COK_COMBAT_HEIGHT) return false;
            if (!draw(screen, tiles, rules, combat->cells[y][x], (unsigned)row, (unsigned)column))
                return false;
        }
    return true;
}
