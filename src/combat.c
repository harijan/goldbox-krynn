#include "combat.h"

#include "adventure.h"
#include "round.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The constant tables of the data segment. */

/* DS:1ee4, four bytes a terrain value: cost, eye and blocking heights,
 * tile. */
const cok_terrain cok_combat_terrain[COK_COMBAT_TERRAINS] = {
    {0x01, 0x00, 0xff, 0x00}, {0xff, 0x01, 0x02, 0x00}, {0xff, 0x01, 0x02, 0x01},
    {0xff, 0x01, 0x02, 0x02}, {0xff, 0x01, 0x02, 0x03}, {0x01, 0x01, 0x00, 0x04},
    {0xff, 0x01, 0x02, 0x05}, {0xff, 0x01, 0x02, 0x06}, {0xff, 0x01, 0x02, 0x07},
    {0x01, 0x01, 0x00, 0x08}, {0xff, 0x01, 0x02, 0x09}, {0x01, 0x01, 0x00, 0x0a},
    {0xff, 0x01, 0x02, 0x0b}, {0x01, 0x01, 0x00, 0x0c}, {0xff, 0x01, 0x02, 0x0d},
    {0x01, 0x01, 0x00, 0x0e}, {0xff, 0x01, 0x02, 0x0f}, {0x01, 0x01, 0x00, 0x10},
    {0xff, 0x01, 0x02, 0x11}, {0xff, 0x01, 0x02, 0x12}, {0xff, 0x01, 0x02, 0x13},
    {0xff, 0x01, 0x02, 0x14}, {0xff, 0x01, 0x02, 0x15}, {0x01, 0x01, 0x00, 0x16},
    {0x01, 0x01, 0x00, 0x17}, {0xff, 0x01, 0x02, 0x18}, {0x02, 0x02, 0x00, 0x22},
    {0x01, 0x01, 0x00, 0x23}, {0x01, 0x01, 0x00, 0x24}, {0x01, 0x01, 0x00, 0x25},
    {0x01, 0x01, 0x00, 0x26}, {0x01, 0x01, 0x00, 0x27}, {0xff, 0x01, 0x02, 0x00},
    {0xff, 0x01, 0x02, 0x01}, {0xff, 0x01, 0x02, 0x02}, {0xff, 0x01, 0x02, 0x03},
    {0xff, 0x01, 0x02, 0x04}, {0xff, 0x01, 0x02, 0x05}, {0x01, 0x01, 0x00, 0x06},
    {0x01, 0x01, 0x00, 0x07}, {0x01, 0x01, 0x00, 0x08}, {0x01, 0x01, 0x00, 0x09},
    {0xff, 0x01, 0x02, 0x0a}, {0xff, 0x01, 0x02, 0x0b}, {0x01, 0x01, 0x00, 0x0c},
    {0x01, 0x01, 0x00, 0x0d}, {0x01, 0x01, 0x00, 0x0e}, {0x01, 0x01, 0x00, 0x0f},
    {0x02, 0x01, 0x00, 0x10}, {0x02, 0x01, 0x00, 0x11}, {0x02, 0x01, 0x00, 0x12},
    {0x02, 0x01, 0x00, 0x13}, {0x02, 0x01, 0x00, 0x14}, {0x02, 0x01, 0x00, 0x15},
    {0x01, 0x01, 0x00, 0x16}, {0x01, 0x01, 0x00, 0x17}, {0x01, 0x01, 0x00, 0x18},
    {0x01, 0x01, 0x00, 0x19}, {0x01, 0x01, 0x00, 0x1a}, {0x01, 0x01, 0x00, 0x1b},
    {0x01, 0x01, 0x00, 0x1c}, {0x04, 0x00, 0x00, 0x1d}, {0x04, 0x00, 0x00, 0x1e},
    {0x04, 0x00, 0x00, 0x1f}, {0x01, 0x01, 0x00, 0x20}, {0x01, 0x01, 0x00, 0x21},
};

/* DS:1fe4 + 8 * size: up to four (dx, dy) cells, dx < 0 for none. Size 0
 * is the end of the terrain table, which 6beb:000f never reads; sizes 5-7
 * are the bytes after it, effect ids and the keypad's scan codes. */
static const int8_t footprints[COK_COMBAT_SIZES][8] = {
    {0x01, 0x01, 0x00, 0x20, 0x01, 0x01, 0x00, 0x21},
    {0, 0, -1, -1, -1, -1, -1, -1},
    {0, 0, 0, 1, -1, -1, -1, -1},
    {0, 0, 1, 0, -1, -1, -1, -1},
    {0, 0, 1, 0, 0, 1, 1, 1},
    {0x33, 0x34, 0x35, 0x1f, 0x4f, 0x50, 0x51, 0x4b},
    {0x20, 0x4d, 0x47, 0x48, 0x49, 0x48, 0x48, 0x48},
    {0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48},
};

/* DS:1ed6 and DS:1edf: a step in direction 0-7, north first, clockwise; 8
 * for none. */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};

/* DS:03da: the combat direction of a quadrant of the 3D map (north, east,
 * south, west), which the skewed map turns: north is up and left. */
static const uint8_t quadrant_dir[4] = {7, 2, 3, 6};

/* DS:0432: flags of the 16 terrain types of open ground; 3cb2:0fc8 always
 * takes type 15 (DS:45ec), whose flags are 4. */
static const uint8_t terrain_flags[16] = {0x00, 0x18, 0x11, 0x15, 0x01, 0x01, 0x60, 0x14,
                                          0x08, 0x01, 0x00, 0x21, 0x71, 0x09, 0x06, 0x04};

static int8_t s8(int value)
{
    return (int8_t)(uint8_t)value;
}

/* The map's lookups (6beb). */

bool cok_combat_footprint(unsigned size, unsigned k, int8_t *dx, int8_t *dy)
{
    if (size == 0 || size >= COK_COMBAT_SIZES || k > 3) return false;
    *dx = footprints[size][2 * k];
    *dy = footprints[size][2 * k + 1];
    return *dx >= 0;
}

uint8_t cok_combat_index(const cok_combat *combat, const uint8_t *record)
{
    for (unsigned n = 1; n <= combat->count && n <= COK_COMBATANTS; ++n) {
        const cok_character *c = combat->combatant[n].character;
        if (c != NULL && c->record == record) return (uint8_t)n;
    }
    return 0;
}

int8_t cok_combat_x(const cok_combat *combat, const uint8_t *record)
{
    uint8_t n = cok_combat_index(combat, record);
    return n == 0 ? 0 : combat->combatant[n].x;
}

int8_t cok_combat_y(const cok_combat *combat, const uint8_t *record)
{
    uint8_t n = cok_combat_index(combat, record);
    return n == 0 ? 0 : combat->combatant[n].y;
}

uint8_t cok_combat_size(const cok_combat *combat, const uint8_t *record)
{
    uint8_t n = cok_combat_index(combat, record);
    return n == 0 ? combat->count : combat->combatant[n].size;
}

void cok_combat_cell(const cok_combat *combat, int8_t x, int8_t y, uint8_t *occupant,
                     uint8_t *terrain)
{
    if (x < 0 || x >= COK_COMBAT_WIDTH || y < 0 || y >= COK_COMBAT_HEIGHT) {
        *occupant = *terrain = 0;
        return;
    }
    *terrain = combat->cells[y][x];
    *occupant = combat->occupant[y][x];
}

bool cok_combat_on_view(int8_t x, int8_t y)
{
    return x >= 0 && x <= 6 && y >= 0 && y <= 6;
}

/* 6beb:0077, and the screen positions 6beb:0375 works out: each
 * combatant's cell less the view's origin. */
static void screen_position(cok_combat *combat, uint8_t n)
{
    cok_combatant *e = &combat->combatant[n];
    e->screen_x = s8(e->x - combat->view_x);
    e->screen_y = s8(e->y - combat->view_y);
}

bool cok_combat_occupy(cok_combat *combat)
{
    memset(combat->occupant, 0, sizeof combat->occupant);
    for (unsigned n = 1; n <= combat->count && n <= COK_COMBATANTS; ++n) {
        const cok_combatant *e = &combat->combatant[n];
        if (e->size == 0) continue;
        if (e->size >= COK_COMBAT_SIZES) return false;
        for (unsigned k = 0; k < 4; ++k) {
            int8_t dx, dy;
            if (!cok_combat_footprint(e->size, k, &dx, &dy)) continue;
            /* The original writes y * 50 + x into the 50 by 25 table
             * without checking either, so x past 49 is the next row. */
            int at = (e->y + dy) * COK_COMBAT_WIDTH + e->x + dx;
            if (at < 0 || at >= COK_COMBAT_WIDTH * COK_COMBAT_HEIGHT) return false;
            combat->occupant[at / COK_COMBAT_WIDTH][at % COK_COMBAT_WIDTH] = (uint8_t)n;
        }
        screen_position(combat, (uint8_t)n);
    }
    return true;
}

/* Entry n, for 6beb:0c43's 0 the stand-in the original reads: x and y 0
 * and the count as the size. */
static cok_combatant entry(const cok_combat *combat, uint8_t n)
{
    if (n != 0) return combat->combatant[n];
    cok_combatant none = {0};
    none.size = combat->count;
    return none;
}

bool cok_combat_probe(const cok_combat *combat, const uint8_t *record, uint8_t dir,
                      uint8_t *occupant, uint8_t *terrain, bool *cloud, bool *puddle)
{
    *occupant = 0;
    *terrain = COK_COMBAT_FLOOR;
    *cloud = *puddle = false;
    uint8_t highest = 1;
    uint8_t n = cok_combat_index(combat, record);
    cok_combatant e = entry(combat, n);
    if (e.size >= COK_COMBAT_SIZES || dir > 8) return false;
    for (unsigned k = 0; k < 4; ++k) {
        int8_t dx, dy;
        if (!cok_combat_footprint(e.size, k, &dx, &dy)) continue;
        uint8_t o, t;
        cok_combat_cell(combat, s8(e.x + dx + step_x[dir]), s8(e.y + dy + step_y[dir]), &o, &t);
        if (o == n) o = 0;
        if (o > 0) *occupant = o;
        if (t >= COK_COMBAT_TERRAINS) return false;
        if (t == 0 || *terrain == 0) {
            *terrain = 0;
        } else if (t == 0x1e) {
            *cloud = true;
        } else if (t == 0x1d) {
            *puddle = true;
        } else if (cok_combat_terrain[t].cost >= highest) {
            highest = cok_combat_terrain[t].cost;
            *terrain = t;
        }
    }
    return true;
}

bool cok_combat_visible(const cok_combat *combat, const uint8_t *record, bool all,
                        bool *visible)
{
    uint8_t n = cok_combat_index(combat, record);
    cok_combatant e = entry(combat, n);
    *visible = true;
    if (e.size == 0) {
        *visible = false;
        return true;
    }
    if (e.size >= COK_COMBAT_SIZES) return false;
    for (unsigned k = 0; k < 4; ++k) {
        int8_t dx, dy;
        if (!cok_combat_footprint(e.size, k, &dx, &dy)) continue;
        *visible = cok_combat_on_view(s8(e.screen_x + dx), s8(e.screen_y + dy));
        if (*visible != all) break;
    }
    return true;
}

bool cok_combat_place(cok_combat *combat, uint8_t mode, cok_character *character, uint8_t x,
                      uint8_t y, bool restore, bool *placed)
{
    *placed = true;
    if (mode != 5) return true;
    *placed = false;
    uint8_t n = cok_combat_index(combat, character->record);
    if (n == 0) return false;
    cok_combatant *e = &combat->combatant[n];
    e->size = character->record[0xcf] & 0x7f;
    e->x = (int8_t)x;
    e->y = (int8_t)y;
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, character->record, 8, &occupant, &terrain, &cloud, &puddle))
        return false;
    if (occupant != 0 || terrain == 0 || cok_combat_terrain[terrain].cost == 0xff) {
        e->size = 0;
        return true;
    }
    *placed = true;
    if (restore && cok_character_combat(character)->not_party == 0) {
        for (unsigned k = 0; k < combat->bodies && k < COK_COMBAT_BODIES; ++k) {
            cok_combat_body *b = &combat->body[k];
            if (b->character != character) continue;
            if (b->cell != COK_COMBAT_BODY) terrain = b->cell;
            memset(b, 0, sizeof *b);
        }
        bool covered = false;
        for (unsigned k = 0; k < combat->bodies && k < COK_COMBAT_BODIES; ++k) {
            const cok_combat_body *b = &combat->body[k];
            if (b->character != NULL && b->x == (int8_t)x && b->y == (int8_t)y) covered = true;
        }
        if (!covered) {
            unsigned at = y * (unsigned)COK_COMBAT_WIDTH + x;
            if (at >= COK_COMBAT_WIDTH * COK_COMBAT_HEIGHT) return false;
            combat->cells[at / COK_COMBAT_WIDTH][at % COK_COMBAT_WIDTH] = terrain;
        }
    }
    return cok_combat_occupy(combat);
}

bool cok_combat_scroll(cok_combat *combat, int8_t x, int8_t y, uint8_t margin)
{
    int8_t cx = s8(combat->view_x + 3), cy = s8(combat->view_y + 3);
    uint8_t m = margin == 0xff ? 0 : margin;
    int8_t left = s8(cx - m), right = s8(cx + m), top = s8(cy - m), bottom = s8(cy + m);
    if (margin != 0xff && x >= left && x <= right && y >= top && y <= bottom) return false;
    if (x < left) {
        while (x < cx && cx > 3) --cx;
    } else if (x > right) {
        while (x > cx && cx < 0x2e) ++cx;
    }
    if (y < top) {
        while (y < cy && cy > 3) --cy;
    } else if (y > bottom) {
        while (y > cy && cy < 0x15) ++cy;
    }
    combat->view_x = s8(cx - 3);
    combat->view_y = s8(cy - 3);
    /* 6beb:0077: every entry to the count, on the map or not. */
    for (unsigned n = 1; n <= combat->count && n <= COK_COMBATANTS; ++n)
        screen_position(combat, (uint8_t)n);
    return true;
}

/* The sides (6346:268a) and the enemies' health (432f:2df3). */

bool cok_combat_count_sides(cok_combat *combat, const cok_party *party)
{
    combat->sides[0] = combat->sides[1] = 0;
    for (size_t i = 0; i < party->count; ++i) {
        const uint8_t *r = party->members[i]->record;
        if (r[0x189] == 0) continue;
        if (r[0x18a] > 1) return false;
        ++combat->sides[r[0x18a]];
    }
    return true;
}

void cok_combat_enemy_health(cok_combat *combat, const cok_party *party)
{
    uint16_t hp = 0, most = 0;
    for (size_t i = 0; i < party->count; ++i) {
        const uint8_t *r = party->members[i]->record;
        if (r[0x18a] != 1) continue;
        if (r[0x189] != 0) hp = (uint16_t)(hp + r[0x197]);
        most = (uint16_t)(most + r[0x62]);
    }
    if (most > 0) combat->enemy_health = (uint8_t)((uint16_t)(20u * hp) / most * 5u);
}

/* The dungeon (3cb2:08cd). */

/* The builder's state, in the data segment and 08cd's locals: the square
 * (DS:45e4, 45e5) relative to the party's, and the kinds of its sides
 * (DS:45e6-45e9): 0 open, 1 a wall, 3 a door or a wall the party passes. */
typedef struct {
    cok_combat *combat;
    const cok_view *view;
    int8_t party_y;     /* DS:6d86. */
    int8_t sx, sy;
    int8_t x, y;        /* The 3D square itself. */
    uint8_t north, west, east, south;
    bool room;          /* DS:45ed: its square byte's 0x40. */
    /* The cells written so far; the original's map is not cleared. */
    bool built[COK_COMBAT_HEIGHT][COK_COMBAT_WIDTH];
    bool unbuilt;       /* A cell not yet built was read where it mattered. */
} dungeon;

/* 3cb2:0046: cell dx, dy of the square's block gets tile + 1, if it is on
 * the map. */
static void set_cell(dungeon *d, uint8_t dx, uint8_t dy, uint8_t tile)
{
    int8_t x = s8(d->sy * 5 + d->sx * 6 + 0x15 + dx), y = s8(d->sy * 5 + 0x0a + dy);
    if (x < 0 || x > 0x31 || y < 0 || y > 0x18) return;
    d->combat->cells[y][x] = (uint8_t)(tile + 1);
    d->built[y][x] = true;
}

/* 3cb2:0306: side dir of square x, y: off the 3D map, open on the party's
 * row to the east or west, else a wall; on it, a wall where the party
 * cannot pass (69ea:0573), open where there is no wall (69ea:06a2), else a
 * door. */
static uint8_t side_kind(const dungeon *d, int8_t x, int8_t y, uint8_t dir)
{
    if (x < 0 || x > 15 || y < 0 || y > 15)
        return y == d->party_y && (dir == 2 || dir == 6) ? 0 : 1;
    if (cok_view_passage(d->view, dir, x, y) == 0) return 1;
    return cok_view_wall(d->view, dir, x, y) == 0 ? 0 : 3;
}

/* 3cb2:0388: the edge on side dir of square x, y: the kinds of both its
 * faces together. */
static uint8_t edge(const dungeon *d, int8_t x, int8_t y, uint8_t dir)
{
    uint8_t across = (uint8_t)((dir + 4) % 8);
    return side_kind(d, x, y, dir) |
           side_kind(d, s8(x + step_x[dir]), s8(y + step_y[dir]), across);
}

/* 3cb2:03fc: the floor, rows 2-4, and the west edge drawn down its
 * slant. */
static void west_edge(dungeon *d)
{
    for (uint8_t dy = 2; dy <= 4; ++dy)
        for (uint8_t dx = 0; dx <= 5; ++dx) set_cell(d, dx, dy, 0x16);
    if (d->west == 1) {
        for (uint8_t b = 2; b <= 4; ++b) {
            set_cell(d, (uint8_t)(b - 1), b, 4);
            set_cell(d, b, b, 3);
            set_cell(d, (uint8_t)(b + 1), b, 0x0d);
        }
    } else if (d->west == 3) {
        set_cell(d, 1, 2, 8);
        set_cell(d, 5, 4, 0);
    }
}

/* 3cb2:04a1: the north edge, cells 3-4 of rows 0-1. */
static void north_edge(dungeon *d)
{
    bool wall = d->north == 1;
    set_cell(d, 3, 0, wall ? 5 : 0x16);
    set_cell(d, 4, 0, wall ? 5 : 0x16);
    set_cell(d, 3, 1, wall ? 0x0a : 0x16);
    set_cell(d, 4, 1, wall ? 0x0a : 0x16);
}

/* 3cb2:051e: the north-west corner, cells 1-2 of rows 0-1, from the north
 * and west edges and whether the edges that meet them from the squares
 * to the north (its west) and west (its north) are open. */
static void north_west(dungeon *d)
{
    bool open = edge(d, d->x, s8(d->y - 1), 6) == 0 && edge(d, s8(d->x - 1), d->y, 0) == 0;
    uint8_t top_left = 0, top_right = 0, bottom_left = 0, bottom_right = 0;
    if (d->north == 0) {
        if (d->west == 0) top_left = 0x16;
        else if (d->west == 3) top_left = 0x0d;
        else if (d->west == 1) top_left = open ? 0 : 0x0d;
    } else if (d->north == 3 || d->north == 1) {
        if (d->west == 0) top_left = open ? 0x0f : 5;
        else top_left = open ? 0x12 : 2;
    }
    if (d->north == 0) top_right = 0x16;
    else if (d->north == 3) top_right = 0x11;
    else if (d->north == 1) top_right = 5;
    if (d->west == 0) {
        bottom_left = d->north == 0 ? 0x16 : open ? 0x10 : 0x0a;
    } else if (d->west == 3) {
        bottom_left = open ? 0x14 : 7;
    } else if (d->west == 1) {
        bottom_left = open ? 1 : 3;
    }
    if (d->west == 0 || d->west == 3) {
        if (d->north == 0) bottom_right = 0x16;
        else if (d->north == 3) bottom_right = 0x17;
        else if (d->north == 1) bottom_right = 0x0a;
    } else if (d->west == 1) {
        if (d->north == 0) bottom_right = 0x0d;
        else if (d->north == 3) bottom_right = 0x15;
        else if (d->north == 1) bottom_right = 6;
    }
    set_cell(d, 1, 0, top_left);
    set_cell(d, 2, 0, top_right);
    set_cell(d, 1, 1, bottom_left);
    set_cell(d, 2, 1, bottom_right);
}

/* 3cb2:06f6: the north-east corner, cells 5-6 of rows 0-1, from the north
 * and east edges and the edges that meet them from the squares to the
 * north (its east) and east (its north). */
static void north_east(dungeon *d)
{
    uint8_t beyond = edge(d, d->x, s8(d->y - 1), 2), next = edge(d, s8(d->x + 1), d->y, 0);
    bool open = beyond == 0 && next == 0;
    uint8_t top_left = 0, top_right = 0, bottom_left = 0, bottom_right = 0;
    if (d->north == 0) top_left = beyond == 1 ? 4 : 0x16;
    else if (d->north == 3) top_left = 0x0f;
    else if (d->north == 1) top_left = 5;
    if (d->north == 0) {
        if (beyond == 0) top_right = 0x16;
        else if (beyond == 3) top_right = d->east == 0 && next != 0 ? 0x18 : 1;
        else if (beyond == 1) top_right = d->east != 0 ? 3 : next != 0 ? 0x0b : 7;
    } else if (d->east != 0) {
        top_right = 9;
    } else if (next != 0) {
        top_right = 5;
    } else {
        top_right = open ? 0x11 : 0x13;
    }
    if (d->north == 0) bottom_left = 0x16;
    else if (d->north == 3) bottom_left = 0x10;
    else if (d->north == 1) bottom_left = 0x0a;
    if (d->north == 0) {
        if (beyond == 0) bottom_right = 0x16;
        else if (d->east != 0) bottom_right = 4;
        else bottom_right = next == 0 ? 8 : 0x0c;
    } else if (d->east != 0) {
        bottom_right = 0x0e;
    } else {
        bottom_right = next != 0 ? 0x0a : 0x17;
    }
    set_cell(d, 5, 0, top_left);
    set_cell(d, 6, 0, top_right);
    set_cell(d, 5, 1, bottom_left);
    set_cell(d, 6, 1, bottom_right);
}

/* 3cb2:00d3: tables and chairs. A room (square byte 0x40) with a wall, no
 * door and not a passage between two opposite walls only gets them: for
 * cells a + b, b of the block (a 2-3, b 2-4) that are plain floor, a d10
 * of 5 or less puts a table there, then each plain floor beside it (north,
 * east, south and west, DS:0429) a chair on a d10 of 9 or less. The dice
 * are rolled only where those hold. Cells 6 and 7 across and the row
 * below belong to blocks not yet built, which in the original hold what
 * the heap held before (GetMem); whether they count as floor decides the
 * dice, and reading one where it does sets d->unbuilt. */
static bool floor_at(const dungeon *d, int8_t x, int8_t y)
{
    uint8_t v = d->combat->cells[y][x];
    return v < COK_COMBAT_TERRAINS && cok_combat_terrain[v].tile == 0x16;
}

/* DS:042a, 042e: north, east, south and west. */
static const int8_t beside_x[4] = {0, 1, 0, -1}, beside_y[4] = {-1, 0, 1, 0};

static void furniture(dungeon *d, uint32_t *seed)
{
    uint8_t n = d->north, e = d->east, s = d->south, w = d->west;
    bool fits = true;
    if (n != 1 && e != 1 && s != 1 && w != 1) fits = false;
    else if (n == 1 && s == 1 && (e != 1 || w != 1)) fits = false;
    else if (e == 1 && w == 1 && (n != 1 || s != 1)) fits = false;
    else if (n == 3 || e == 3 || s == 3 || w == 3) fits = false;
    for (uint8_t a = 2; a <= 3; ++a) {
        for (uint8_t b = 2; b <= 4; ++b) {
            int8_t x = s8(d->sy * 5 + d->sx * 6 + 0x15 + a + b), y = s8(d->sy * 5 + 0x0a + b);
            if (x < 0 || x > 0x31 || y < 0 || y > 0x18) continue;
            if (!d->built[y][x] && d->room && fits) d->unbuilt = true;
            if (!floor_at(d, x, y)) continue;
            if (!d->room || !fits || cok_dice(seed, 1, 10) > 5) continue;
            d->combat->cells[y][x] = 0x1a;
            for (unsigned k = 0; k < 4; ++k) {
                int8_t cx = s8(x + beside_x[k]), cy = s8(y + beside_y[k]);
                if (cx < 0 || cx > 0x31 || cy < 0 || cy > 0x18) continue;
                if (!d->built[cy][cx]) d->unbuilt = true;
                if (!floor_at(d, cx, cy)) continue;
                if (cok_dice(seed, 1, 10) <= 9) d->combat->cells[cy][cx] = 0x1b;
            }
        }
    }
}

bool cok_combat_dungeon(cok_combat *combat, const cok_view *view, int8_t x, int8_t y,
                        uint32_t *seed)
{
    dungeon d = {.combat = combat, .view = view, .party_y = y};
    for (d.sy = -2; d.sy <= 2; ++d.sy) {
        for (d.sx = -6; d.sx <= 6; ++d.sx) {
            d.x = s8(x + d.sx);
            d.y = s8(y + d.sy);
            d.west = edge(&d, d.x, d.y, 6);
            d.north = edge(&d, d.x, d.y, 0);
            d.east = edge(&d, d.x, d.y, 2);
            d.south = edge(&d, d.x, d.y, 4);
            west_edge(&d);
            north_edge(&d);
            north_west(&d);
            north_east(&d);
            d.room = (cok_view_square(view, d.x, d.y) & 0x40) != 0;
            furniture(&d, seed);
        }
    }
    return !d.unbuilt;
}

/* The wilderness (3cb2:0fc8). */

static uint8_t tile_at(const cok_combat *combat, int x, int y)
{
    return cok_combat_terrain[combat->cells[y][x]].tile;
}

/* 3cb2:0a00: a river, on a d100 at most 0x23 with flag 0x20 or 0x4b with
 * 0x10 (none otherwise, but the d100 is rolled): from a column 34 less
 * 5d4, moved left until two past it is a multiple of 7, one cell of
 * water and one of rapids a row, the column one right a row, and a d20
 * of 1 puts banks to the right (3cb2:09ac). */
static void river(cok_combat *combat, uint8_t flags, uint32_t *seed)
{
    uint8_t chance = 0;
    if (flags & 0x20) chance = 0x23;
    if (flags & 0x10) chance = 0x4b;
    if (cok_dice(seed, 1, 100) > chance) return;
    int8_t x = s8(0x22 - cok_dice(seed, 5, 4));
    while ((x + 2) % 7 > 0) --x;
    for (int y = 0; y <= 0x18; ++y) {
        if (x > 0x31) continue;
        combat->cells[y][x] = (uint8_t)(0x3b + cok_dice(seed, 1, 2));
        if (x < 0x31) combat->cells[y][x + 1] = (uint8_t)(0x3d + cok_dice(seed, 1, 2));
        if (cok_dice(seed, 1, 20) == 1 && x < 0x31) {
            combat->cells[y][x + 1] = 0x40;
            if (y < 0x18) combat->cells[y + 1][x + 1] = 0x41;
        }
        ++x;
    }
}

/* 3cb2:0b0b: trees, unless flag 0x80. The chance, 10, less 5 for flag 2
 * and 2 for 4, plus 5 for 0x40 and 10 for 8, at least 1 when negative:
 * column by column, from row 1, a plain floor cell below another, on a
 * d100 up to the chance, gets on a second d100 up to it a bush, else a
 * tree in both. */
static void trees(cok_combat *combat, uint8_t flags, uint32_t *seed)
{
    if (flags & 0x80) return;
    int8_t chance = 10;
    if (flags & 0x02) chance = s8(chance - 5);
    if (flags & 0x04) chance = s8(chance - 2);
    if (flags & 0x40) chance = s8(chance + 5);
    if (flags & 0x08) chance = s8(chance + 10);
    if (chance < 0) chance = 1;
    for (int x = 0; x <= 0x31; ++x) {
        for (int y = 1; y <= 0x18; ++y) {
            if (tile_at(combat, x, y) != 0x16 || tile_at(combat, x, y - 1) != 0x16) continue;
            if (chance < cok_dice(seed, 1, 100)) continue;
            if (chance >= cok_dice(seed, 1, 100)) {
                combat->cells[y][x] = (uint8_t)(0x29 + cok_dice(seed, 1, 2));
            } else {
                combat->cells[y - 1][x] = (uint8_t)(0x1f + cok_dice(seed, 1, 6));
                combat->cells[y][x] = (uint8_t)(0x25 + cok_dice(seed, 1, 4));
            }
        }
    }
}

/* 3cb2:0ca2: a d100 picks scatter for a cell by five chances in turn. */
static void scatter_cell(cok_combat *combat, int x, int y, const uint8_t chance[5],
                         uint32_t *seed)
{
    static const uint8_t base[5] = {0x39, 0x2f, 0x2b, 0x36, 0x31}, sides[5] = {2, 2, 4, 3, 4};
    uint8_t roll = cok_dice(seed, 1, 100);
    unsigned sum = 0;
    for (unsigned k = 0; k < 5; ++k) {
        sum += chance[k];
        if (roll > sum) continue;
        combat->cells[y][x] = (uint8_t)(base[k] + cok_dice(seed, 1, sides[k]));
        return;
    }
}

/* 3cb2:0e4a: scatter on the plain floor, column by column, by a density
 * of 50, 10 more for flag 0x10, 30 for 0x20 and 20 for 0x40, 10 less for
 * 4, 20 for 2 and 50 for 0x80, which picks the chances. */
static void scatter(cok_combat *combat, uint8_t flags, uint32_t *seed)
{
    int density = 50;
    if (flags & 0x10) density += 10;
    if (flags & 0x20) density += 30;
    if (flags & 0x40) density += 20;
    if (flags & 0x04) density -= 10;
    if (flags & 0x02) density -= 20;
    if (flags & 0x80) density -= 50;
    static const struct { int low, high; uint8_t chance[5]; } bands[5] = {
        {-30, 9, {0, 0, 0, 30, 15}},  {10, 29, {0, 1, 5, 20, 10}},  {30, 69, {0, 2, 5, 10, 5}},
        {70, 89, {10, 2, 10, 10, 1}}, {90, 110, {15, 5, 15, 10, 1}},
    };
    for (int x = 0; x <= 0x31; ++x) {
        for (int y = 0; y <= 0x18; ++y) {
            if (tile_at(combat, x, y) != 0x16) continue;
            for (unsigned b = 0; b < 5; ++b)
                if (density >= bands[b].low && density <= bands[b].high)
                    scatter_cell(combat, x, y, bands[b].chance, seed);
        }
    }
}

void cok_combat_wilderness(cok_combat *combat, uint8_t flags, uint32_t *seed)
{
    memset(combat->cells, COK_COMBAT_FLOOR, sizeof combat->cells);
    river(combat, flags, seed);
    trees(combat, flags, seed);
    scatter(combat, flags, seed);
}

/* The combat records (3cb2:10d9). */

bool cok_combat_records(cok_party *party, const cok_item_types *types, uint16_t party_size,
                        uint8_t direction, uint16_t morale, char *error, size_t error_size)
{
    uint8_t n = 0;
    for (size_t i = 0; i < party->count; ++i) {
        cok_character *c = party->members[i];
        if (!cok_character_stats(c, types, error, error_size)) return false;
        ++n;
        free(c->combat);
        c->combat = calloc(1, sizeof *c->combat);
        if (c->combat == NULL) {
            snprintf(error, error_size, "out of memory");
            return false;
        }
        uint8_t *r = c->record;
        if (n > party_size) c->combat->not_party = 1;
        c->combat->facing = quadrant_dir[direction >> 1];
        if (r[0x18a] == 1) c->combat->facing = (uint8_t)((c->combat->facing + 4) % 8);
        uint8_t own = r[0xe7] & 0x7f;
        if (r[0x18a] == 0 && c->combat->not_party == 1 && (own == 0 || own > 0x66))
            r[0xe7] = (uint8_t)(morale + 0x80);
    }
    return true;
}

/* Placement (3cb2:17f7). */

/* Its state in the data segment: for each side, the 3D square it stands
 * on relative to the party's (DS:45dc, 45de), how wide its first rank is
 * (45e0), the quadrant it faces (45e2), and the cells of its formations
 * still free (DS:43cc + side * 0x108 + k * 0x42 + row * 11 + column), one
 * for each square it may stand on, k: 11 by 6, filled from DS:03ee by the
 * quadrant, or for k 1 its own; and the side of the record being placed
 * (45ea). */
typedef struct {
    cok_adventure *game;
    int8_t anchor_x[2], anchor_y[2];
    uint8_t width[2], quadrant[2];
    uint8_t free[2][4][6][11];
    uint8_t side;
} placement;

/* DS:03ca: by the side's quadrant and the square k it stands on, twice
 * the quadrant its ranks are laid out for. */
static const uint8_t quadrants[4][4] = {{0, 0, 2, 6}, {2, 2, 0, 4}, {4, 4, 2, 6}, {6, 6, 4, 0}};
/* DS:03de, 03e6: the first rank's centre, for k 0 and other squares, by
 * that quadrant. */
static const int8_t start_column[2][4] = {{5, 4, 5, 6}, {3, 8, 7, 2}};
static const int8_t start_row[2][4] = {{3, 2, 2, 3}, {0, 2, 5, 3}};
/* DS:03ba: by the side's quadrant, the 3D directions of squares 1-3
 * (behind, then to either side); 8 for square 0. */
static const uint8_t moves[4][4] = {{8, 4, 6, 2}, {8, 6, 4, 0}, {8, 0, 6, 2}, {8, 2, 0, 4}};

/* DS:03ee: for formation 0-4, the first and last free column of rows 0-5
 * (none where the first is past the last). */
static const int8_t formations[5][6][2] = {
    {{1, 0}, {1, 0}, {1, 0}, {2, 9}, {3, 10}, {4, 10}},
    {{0, 2}, {0, 3}, {1, 4}, {2, 5}, {3, 6}, {4, 7}},
    {{0, 6}, {0, 7}, {1, 8}, {1, 0}, {1, 0}, {1, 0}},
    {{3, 6}, {4, 7}, {5, 8}, {6, 9}, {7, 10}, {8, 10}},
    {{0, 6}, {0, 7}, {1, 8}, {2, 9}, {3, 10}, {4, 10}},
};

/* 3cb2:122c: try column, row of formation k, from square ax, ay: if the
 * formation's cell is free, the combatant stands at the cell it gives,
 * and if nothing is there and it can be walked on (6beb:0c9d), the cell is
 * taken. */
static bool try_cell(placement *p, uint8_t d, int8_t column, int8_t row, int8_t ax, int8_t ay,
                     uint8_t k, bool *placed)
{
    *placed = false;
    if (column < 0 || row < 0 || column > 10 || row > 5) return true;
    uint8_t *cell = &p->free[p->side][k][row][column];
    if (*cell == 0) return true;
    cok_combat *combat = &p->game->combat;
    cok_combatant *e = &combat->combatant[d];
    e->x = s8(column + ax * 6 + ay * 5 + 0x16);
    e->y = s8(row + ay * 5 + 0x0a);
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, e->character->record, 8, &occupant, &terrain, &cloud, &puddle))
        return false;
    if (occupant != 0 || terrain == 0 || cok_combat_terrain[terrain].cost == 0xff) return true;
    *placed = true;
    *cell = 0;
    return true;
}

/* Rounds of 3cb2:1379 past which its state repeats: the ranks are
 * counted in a byte, each takes at most a few dozen tries, and at most
 * four squares are tried. */
enum { PLACE_STEPS = 1 << 20 };

/* 3cb2:1379: find combatant d a place. From the side's square (k 0), its
 * formation is filled rank by rank: a rank starts at a cell set by k and
 * the quadrant (DS:03de, 03e6) moved back by the rank's number, then
 * takes cells to one side and the other, one further each pair (combat
 * directions DS:03da by the quadrant). A rank ends at a cell outside the
 * formation in one of column and row, or once it has tried as many cells
 * as the first rank's width, later ranks 12, the first being the first of
 * the whole search. The party's first rank,
 * facing east or west, skips the next one when its square has any side
 * but the way it faces that is not a wall. A cell outside in both moves
 * on to the next square that the side's square does not have a wall
 * toward (DS:03ba: behind, then to either side), starting again from rank 0; with
 * none left there is no place. A cell where a rank ends at its width is
 * still tried; one outside is not.
 * Every byte wraps, as in the original. *found is whether it found one;
 * returns false where it cannot be carried out. */
static bool find_place(placement *p, uint8_t d, bool *found)
{
    cok_adventure *game = p->game;
    const cok_ecl *vm = &game->vm;
    uint8_t side = p->side, quadrant = p->quadrant[side];
    bool first = true, failed = false, placed = false;
    uint8_t state = 1, rank = 0, k = 0, tried = 0, along = 0;
    int8_t ax = p->anchor_x[side], ay = p->anchor_y[side];
    int8_t centre_column = 0, centre_row = 0, column = 0, row = 0;
    dungeon map = {.view = &game->view, .party_y = vm->map_y};
    for (unsigned long steps = 0;; ++steps) {
        if (steps > PLACE_STEPS) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "placing a combatant never ends (3cb2:1379)");
            return false;
        }
        uint8_t q = quadrants[quadrant][k] >> 1;
        if (state == 1) {
            uint8_t dir = quadrant_dir[(q + 2) % 4];
            centre_column = s8(rank * step_x[dir] + start_column[k > 0][q]);
            centre_row = s8(rank * step_y[dir] + start_row[k > 0][q]);
            column = centre_column;
            row = centre_row;
            along = 1;
            state = 2;
            tried = 1;
        } else if (state == 2) {
            uint8_t dir = quadrant_dir[(q + 1) % 4];
            column = s8(along * step_x[dir] + centre_column);
            row = s8(along * step_y[dir] + centre_row);
            state = 3;
            ++tried;
        } else if (state == 3) {
            uint8_t dir = quadrant_dir[(q + 3) % 4];
            column = s8(along * step_x[dir] + centre_column);
            row = s8(along * step_y[dir] + centre_row);
            state = 2;
            ++along;
            ++tried;
        }
        bool out = column < 0 || row < 0 || column > 10 || row > 5;
        bool both = (column < 0 || column > 10) && (row < 0 || row > 5); /* 3cb2:11fb */
        if ((out && !both) || (first && tried >= p->width[side]) || (!first && tried > 11)) {
            ++rank;
            if (side == 0 && (quadrant & 1) && k == 0 && rank == 1) {
                int8_t x = s8(vm->map_x + p->anchor_x[side]), y = s8(vm->map_y + p->anchor_y[side]);
                bool open = false;
                for (unsigned j = 1; j <= 3; ++j)
                    if (vm->mode == 3 || edge(&map, x, y, moves[quadrant][j]) != 1) open = true;
                if (open) ++rank;
            }
            state = 1;
            first = false;
        }
        if (out && both) {
            placed = false;
            state = 0;
            while (k < 3 && state != 1) {
                ++k;
                int8_t x = s8(vm->map_x + p->anchor_x[side]), y = s8(vm->map_y + p->anchor_y[side]);
                uint8_t dir = moves[quadrant][k];
                if (vm->mode != 3 && edge(&map, x, y, dir) == 1) continue;
                ax = s8(p->anchor_x[side] + step_x[dir]);
                ay = s8(p->anchor_y[side] + step_y[dir]);
                rank = 0;
                state = 1;
            }
            if (state != 1) failed = true;
        }
        if (!out && !try_cell(p, d, column, row, ax, ay, k, &placed)) return false;
        if (placed || failed) break;
    }
    *found = !failed;
    return true;
}

static void name_of(const uint8_t *c, char out[16])
{
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(out, c + 1, length);
    out[length] = '\0';
}

static void log_text(cok_adventure *game, const char *kind, const char *text)
{
    cok_adventure_log(game, kind, text);
}

/* 1614:0479: read and drop a key if one is waiting. */
static void drop_key(cok_adventure *game)
{
    if (!cok_adventure_key_pending(game)) return;
    cok_keyboard keys = cok_adventure_keyboard(game);
    keys.read(keys.context);
}

bool cok_combat_place_all(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    cok_ecl *vm = &game->vm;
    if (!cok_combat_count_sides(combat, &game->party)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a record on a side other than 0 or 1 (+0x18a) counts past DS:6b2d "
                           "(6346:268a)");
        return false;
    }
    /* The original clears the size of entries 1-255, past the 72 into the
     * screen positions (every fourth from DS:6362 and 63aa) and the
     * occupants, which it rebuilds. */
    for (unsigned n = 1; n <= COK_COMBATANTS; ++n) combat->combatant[n].size = 0;
    for (unsigned n = 4; n <= 0x44; n += 4)
        combat->combatant[n].screen_x = combat->combatant[n].screen_y = 0;
    if (!cok_combat_occupy(combat)) return false;
    placement p = {.game = game};
    uint8_t facing = vm->direction;
    p.quadrant[0] = facing >> 1;
    p.quadrant[1] = (uint8_t)((facing + 4) % 8) >> 1;
    uint16_t distance = vm->mem7c00[0x2c1];
    p.anchor_x[1] = s8((int)distance * step_x[facing]);
    p.anchor_y[1] = s8((int)distance * step_y[facing]);
    for (unsigned side = 0; side < 2; ++side) {
        p.width[side] = (uint8_t)((combat->sides[side] + 1) >> 1);
        for (unsigned k = 0; k < 4; ++k) {
            unsigned f = k == 1 ? 4 : p.quadrant[side];
            for (unsigned row = 0; row < 6; ++row)
                for (unsigned column = 0; column < 11; ++column)
                    p.free[side][k][row][column] = formations[f][row][0] <= (int)column &&
                                                   formations[f][row][1] >= (int)column;
        }
    }
    combat->count = 1;
    uint8_t d = 1;
    /* The original walks on from the last record kept, or with none yet
     * from the list's first, freed when removed, whose next still names
     * the record after it: removing that one too sends the walk back to it
     * again and again. */
    bool kept = false;
    unsigned leading = 0;
    for (size_t i = 0; i < game->party.count;) {
        drop_key(game);
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (d >= COK_COMBATANTS) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "a 72nd combatant's next entry lies in the screen positions "
                               "(3cb2:17f7)");
            return false;
        }
        cok_combatant *e = &combat->combatant[d];
        e->character = c;
        if (r[0x18a] > 1) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "a record on side %u indexes past the sides' formations "
                               "(3cb2:1379)", r[0x18a]);
            return false;
        }
        p.side = r[0x18a];
        e->size = r[0xcf] & 7;
        bool found;
        if (!find_place(&p, d, &found)) return false;
        char name[16], text[80];
        name_of(r, name);
        if (!found) {
            e->size = 0;
            if (cok_character_combat(c)->not_party == 1) {
                if (!kept && ++leading == 2) {
                    cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                       "with the list's first two records removed, placement "
                                       "walks back to the second for good (3cb2:17f7)");
                    return false;
                }
                e->character = NULL;
                snprintf(text, sizeof text, "%s has no place and is removed", name);
                log_text(game, "combat", text);
                /* 4def:3b0a removes the selected record: the original
                 * selects it first, then the one before it. */
                if (!cok_treasure_remove_record(game, i, true, false, false)) return false;
                continue;
            }
            snprintf(text, sizeof text, "%u %s has no place", d, name);
            log_text(game, "combat", text);
        } else {
            snprintf(text, sizeof text, "%u %s at %d,%d", d, name, e->x, e->y);
            if (r[0x189] == 0) {
                e->size = 0;
                /* A duel (DS:883e) leaves no bodies; CoK has none. */
                if (cok_character_combat(c)->not_party == 0) {
                    if (combat->bodies >= COK_COMBAT_BODIES) {
                        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                           "a ninth body overruns the table (DS:69ee)");
                        return false;
                    }
                    if (e->x < 0 || e->x >= COK_COMBAT_WIDTH || e->y < 0 ||
                        e->y >= COK_COMBAT_HEIGHT) {
                        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                           "a body off the map is written outside it (3cb2:17f7)");
                        return false;
                    }
                    cok_combat_body *b = &combat->body[combat->bodies++];
                    b->character = c;
                    b->x = e->x;
                    b->y = e->y;
                    b->cell = combat->cells[e->y][e->x];
                    combat->cells[e->y][e->x] = COK_COMBAT_BODY;
                    strncat(text, ", fallen", sizeof text - strlen(text) - 1);
                }
            }
            if (r[0x188] == 9) {
                r[0x189] = 0;
                e->size = 0;
            }
            if (e->size == 0) strncat(text, ", off the map", sizeof text - strlen(text) - 1);
            log_text(game, "combat", text);
            if (!cok_combat_occupy(combat)) {
                cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                   "a footprint is written outside the occupants (6beb:0375)");
                return false;
            }
        }
        kept = true;
        ++d;
        ++combat->count;
        ++i;
    }
    combat->combatant[combat->count].character = NULL;
    return true;
}

/* Setup (3cb2:1c58). */

/* 6d21:002c: copy count frames of name's record 1 into the tile set from
 * frame first. */
static bool load_tiles(cok_adventure *game, const char *name, uint8_t first, uint8_t count)
{
    cok_combat *combat = &game->combat;
    if (combat->tiles.pixels == NULL &&
        cok_picture_create(&combat->tiles, 3, 24, COK_COMBAT_TILES, 0) != COK_PICTURE_OK) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
        return false;
    }
    cok_picture set = {0};
    if (!cok_adventure_load_image(game, name, 1, -1, &set)) {
        cok_picture_free(&set);
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    bool ok = set.frame_size == combat->tiles.frame_size && set.frames >= count;
    if (ok)
        memcpy(combat->tiles.pixels + first * combat->tiles.frame_size, set.pixels,
               count * set.frame_size);
    cok_picture_free(&set);
    if (!ok) cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s.DAX holds no %u tiles", name, count);
    return ok;
}

/* 3cb2:1029: the tiles and the map, from the 3D map in a 3D area (var
 * 0x4be6), else the wilderness. */
static bool build(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    cok_ecl *vm = &game->vm;
    bool area = vm->mem4b00[0xe6] != 0;
    if (!load_tiles(game, area ? "DUNGCOM" : "WILDCOM", 0, area ? 0x19 : 0x21) ||
        !load_tiles(game, "RANDCOM", 0x22, 6))
        return false;
    combat->active = true;
    combat->cursor = 0;
    combat->cursor_size = 1;
    combat->see_all = 0;
    static const char *const facing[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    char text[96];
    if (area) {
        /* GetMem does not clear the map. In play it takes the block the
         * buffer of RANDCOM's record held, freed by 6d21:002c just before,
         * so its cells hold that record from its byte 7 (0, 0 may hold the
         * free list's size instead, which nothing reads); a room's tables
         * read the cells not yet built. */
        size_t size;
        bool no_file;
        uint8_t *rand = cok_adventure_find_record(game, "RANDCOM", 1, &size, &no_file);
        if (rand == NULL || size < 7 + sizeof combat->cells) {
            free(rand);
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "RANDCOM.DAX record 1 is too short");
            return false;
        }
        memcpy(combat->cells, rand + 7, sizeof combat->cells);
        free(rand);
        cok_combat_dungeon(combat, &game->view, vm->map_x, vm->map_y, &vm->seed);
        snprintf(text, sizeof text, "the 3D map around %d,%d facing %s, the enemy %u squares ahead",
                 vm->map_x, vm->map_y, facing[vm->direction & 7], vm->mem7c00[0x2c1]);
    } else {
        cok_combat_wilderness(combat, terrain_flags[15], &vm->seed);
        snprintf(text, sizeof text, "open ground facing %s, the enemy %u squares ahead",
                 facing[vm->direction & 7], vm->mem7c00[0x2c1]);
    }
    log_text(game, "combat", text);
    return true;
}

bool cok_combat_setup(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_combat *combat = &game->combat;
    vm->mode = 5; /* 3995:0172, before setup. */
    if (vm->direction > 7) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "facing %u is past the tables of directions (3cb2:10d9)", vm->direction);
        return false;
    }
    /* 6961:0537 and the big picture; the portraits are not ported. */
    cok_adventure_free_picture(game);
    cok_picture_free(&game->big);
    game->big_id = COK_ADVENTURE_NO_PICTURE;
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
    cok_adventure_wait(game, game->speed * 100u);              /* 1521:0b4b */
    static const char begins[] = "A battle begins...";
    log_text(game, "print", begins);
    cok_text_string(&game->screen, &game->font, begins, 0, 24, 10, 0); /* 1521:0353 */
    combat->magic_on = false;
    game->effects.rolls.round = 0;
    combat->round_limit = 15;
    game->effects.rolls.attack_roll = 0;
    combat->bodies = 0;
    memset(combat->body, 0, sizeof combat->body);
    combat->yelled = NULL;
    memset(combat->exploding, 0, sizeof combat->exploding);
    combat->exploding_count = 0;
    combat->exploding_now = false;
    game->pool.missile = 0;
    free(game->lost_weapons);
    game->lost_weapons = NULL;
    game->lost_weapon_count = 0;
    vm->mem7c00[0x333] = 0;
    bool listed = game->party.count > 0;
    if (!build(game)) return false;
    drop_key(game);
    char error[300];
    if (!cok_combat_records(&game->party, &game->item_types, vm->mem7c00[0x33e], vm->direction,
                            vm->mem7c00[0x2c6], error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    drop_key(game);
    if (!cok_combat_place_all(game)) return false;
    drop_key(game);
    /* The view's origin, from the first record, whether or not it is on the
     * map; then the screen (6346:300f), which draws the map centred there
     * (6beb:096b), scrolling only to work out the screen positions. */
    uint8_t *first = cok_party_record(&game->party, 0);
    combat->view_x = s8(cok_combat_x(combat, first) - 3);
    combat->view_y = s8(cok_combat_y(combat, first) - 3);
    if (first == NULL && listed) {
        /* The list emptied by removing its one record: the original's
         * lookup of NULL finds entry 1, which the removal cleared, at the
         * last cell tried for it. Empty from the start, entry 1 keeps a
         * record of an earlier battle, which does not match: the port's
         * lookup gives 0, 0 as the original's does then. */
        combat->view_x = s8(combat->combatant[1].x - 3);
        combat->view_y = s8(combat->combatant[1].y - 3);
    }
    cok_combat_scroll(combat, s8(combat->view_x + 3), s8(combat->view_y + 3), 0xff);
    char text[80];
    snprintf(text, sizeof text, "the view from %d,%d", combat->view_x, combat->view_y);
    log_text(game, "combat", text);
    log_text(game, "unported", "the combat screen (6346:300f)");
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        c->combat->turns = 0;
        if (!cok_effects_dispatch(&game->effects, c, 8) ||
            !cok_effects_dispatch(&game->effects, c, 0x16)) {
            cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", game->effects.error);
            return false;
        }
    }
    cok_combat_enemy_health(combat, &game->party);
    vm->mode = 5;
    game->moving = false;
    return true;
}

_Static_assert(sizeof(cok_terrain) == 4, "four bytes a terrain value");

const cok_ds_table cok_combat_tables[] = {
    {0x1ee4, sizeof cok_combat_terrain, 1, (const uint8_t *)cok_combat_terrain},
    {0x1fe4, sizeof footprints, 1, (const uint8_t *)footprints},
    {0x1ed6, sizeof step_x, 1, (const uint8_t *)step_x},
    {0x1edf, sizeof step_y, 1, (const uint8_t *)step_y},
    {0x03da, sizeof quadrant_dir, 1, quadrant_dir},
    {0x03ba, sizeof moves, 1, &moves[0][0]},
    {0x03ca, sizeof quadrants, 1, &quadrants[0][0]},
    {0x03de, sizeof start_column, 1, (const uint8_t *)start_column},
    {0x03e6, sizeof start_row, 1, (const uint8_t *)start_row},
    {0x03ee, sizeof formations, 1, (const uint8_t *)formations},
    {0x042a, sizeof beside_x, 1, (const uint8_t *)beside_x},
    {0x042e, sizeof beside_y, 1, (const uint8_t *)beside_y},
    {0x0432, sizeof terrain_flags, 1, terrain_flags},
};
const size_t cok_combat_table_count = sizeof cok_combat_tables / sizeof *cok_combat_tables;

/* The end of the battle (3995:004b): it frees the lists of clouds on the
 * map (DS:7111, 7119), which the port does not have yet; then each record
 * charmed (effect 0x0b) and okay runs (status 3) if more than one enemy
 * could act at the last count, and loses the first effect of each id
 * that lasts only through the battle (60f4:1440, see round.h); the map
 * and the flash picture (DS:719e, not ported) are freed, and spells pick
 * their targets outside combat again (DS:6e3a). */
bool cok_combat_end(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (cok_character_find_effect(c, 0x0b) != NULL && r[0x188] == 0)
            r[0x188] = game->combat.sides[1] > 1 ? 3 : 0;
        if (!cok_combat_battle_only(game, c)) return false;
    }
    game->combat.active = false;
    game->combat_targets = false;
    return true;
}
