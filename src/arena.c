#include "arena.h"

#include "camp.h"
#include "items.h"
#include "round.h"
#include "screen.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BUFFER_UNITS = 21, /* DS:4b78: 7 cells of 3 units, 168 rows. */
    BUFFER_ROWS = 168,
    LAST_SHOWN = 0x12, /* The last unit or cell row a 24 by 24 picture starts at whole. */
    SLOT_HEAD = 11,    /* 4b6d:0817 builds a head here. */
    SLOT_SKULL = 24,   /* COMSPR id 11: the death's skull. */
    SLOT_CURSOR = 25,  /* COMSPR id 25: the cursor's box. */
};

/* DS:1ed6 and DS:1edf: a step in direction 0-7, north first, clockwise; 8
 * for none. */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};
/* DS:390b: the colours of a player's icon that its six colour bytes
 * (+0x139) give, and those + 8. */
static const uint8_t icon_colours[6] = {1, 2, 3, 4, 6, 7};
/* DS:1e26 and DS:1e36: colour 8 drawn black, outside CGA mode. */
static const uint8_t same_colours[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const uint8_t black_colours[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 9, 10, 11, 12, 13, 14, 15};

const cok_ds_table cok_arena_tables[] = {
    {0x1ed6, sizeof step_x, 1, (const uint8_t *)step_x},
    {0x1edf, sizeof step_y, 1, (const uint8_t *)step_y},
    {0x390b, sizeof icon_colours, 1, icon_colours},
    {0x1e26, sizeof same_colours, 1, same_colours},
    {0x1e36, sizeof black_colours, 1, black_colours},
};
const size_t cok_arena_table_count = sizeof cok_arena_tables / sizeof *cok_arena_tables;

static int8_t s8(int value)
{
    return (int8_t)(uint8_t)value;
}

/* 6b30:0005. */
static int sign(int value)
{
    return value < 0 ? -1 : value > 0;
}

static bool fail(cok_adventure *game, const char *format, ...)
{
    char text[300];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", text);
    return false;
}

void cok_arena_sound(cok_adventure *game, unsigned n)
{
    char text[16];
    snprintf(text, sizeof text, "%u", n);
    cok_adventure_log(game, "sound", text);
}

static void sound(cok_adventure *game, unsigned n)
{
    cok_arena_sound(game, n);
}

/* Clear cells x1-x2 of rows y1-y2 (1128:07e6). */
static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    if (x2 < x1 || y2 < y1) return;
    cok_picture_fill(&game->screen, x1, y1 * 8, (size_t)(x2 - x1 + 1), (size_t)(y2 - y1 + 1) * 8,
                     0);
}

/* The screen's pictures, made as the original makes them: the buffer and
 * the save at startup (3e99:005b), the missile's at setup (3cb2:1c58). */
static bool pictures(cok_adventure *game)
{
    cok_combat *c = &game->combat;
    if ((c->buffer.pixels == NULL &&
         cok_picture_create(&c->buffer, BUFFER_UNITS, BUFFER_ROWS, 1, 0) != COK_PICTURE_OK) ||
        (c->under.pixels == NULL && cok_picture_create(&c->under, 3, 24, 1, 0) != COK_PICTURE_OK) ||
        (c->flash.pixels == NULL && cok_picture_create(&c->flash, 3, 24, 4, 1) != COK_PICTURE_OK)) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
        return false;
    }
    return true;
}

/* The map is there (DS:6a2e), which the drawing reads without a check. */
static bool map_there(cok_adventure *game, const char *where)
{
    if (game->combat.active) return pictures(game);
    return fail(game, "%s reads the combat map, which is not there (DS:6a2e)", where);
}

/* The byte the drawing takes for cell x, y: the map's +7 + y * 50 + x,
 * read without a check, so that past a side it is the row before or
 * after, and before the cells the map's own bytes (+2 to +6). Further off
 * the original reads the heap around the map. */
static bool map_byte(cok_adventure *game, int x, int y, uint8_t *value, const char *where)
{
    const cok_combat *c = &game->combat;
    long at = 7L + (long)y * COK_COMBAT_WIDTH + x;
    if (at >= 7 && at < 7L + COK_COMBAT_WIDTH * COK_COMBAT_HEIGHT) {
        *value = c->cells[(at - 7) / COK_COMBAT_WIDTH][(at - 7) % COK_COMBAT_WIDTH];
        return true;
    }
    const uint8_t header[5] = {(uint8_t)c->view_x, (uint8_t)c->view_y, c->cursor, c->cursor_size,
                               c->see_all};
    if (at >= 2 && at <= 6) {
        *value = header[at - 2];
        return true;
    }
    return fail(game, "cell %d,%d is drawn from the heap around the combat map (%s)", x, y, where);
}

/* Draw the terrain of map cell x, y at unit xu and cell row yc of the
 * buffer (6d21:00f7): the tile of its value (DS:1ee7), opaque. */
static bool terrain(cok_adventure *game, int xu, int yc, int x, int y, const char *where)
{
    cok_combat *c = &game->combat;
    uint8_t value;
    if (!map_byte(game, x, y, &value, where)) return false;
    if (value >= COK_COMBAT_TERRAINS)
        return fail(game, "terrain value 0x%02x is past the table (DS:1ee4, %s)", value, where);
    uint8_t tile = cok_combat_terrain[value].tile;
    if (tile >= COK_COMBAT_TILES || !(c->tiles_loaded >> tile & 1))
        return fail(game, "tile 0x%02x of the combat tile set was never loaded (DS:616a, %s)", tile,
                    where);
    cok_picture_draw(&c->buffer, &c->tiles, tile, s8(xu), s8(yc), 0, NULL); /* 127f:08d7 */
    /* The bytes it covers are known again. */
    for (int row = yc * 8; row < yc * 8 + 24; ++row)
        for (int b = xu * 4; b < xu * 4 + 12; ++b)
            if (row >= 0 && row < BUFFER_ROWS && b >= 0 && b < BUFFER_UNITS * 4)
                c->unknown[row * BUFFER_UNITS * 4 + b] = 0;
    return true;
}

/* Where 127f:09c3 (127f:226c, 24fd or 27af by the mode, which share this
 * arithmetic and their copy loops, 2927 and 2986) draws src on the map's
 * buffer at unit x, cell row y, in its own arithmetic: the units clipped on
 * the left and right, the rows to draw, and the offsets it starts from, in
 * units (src_off, from the picture's first row drawn) and bytes (dst_off,
 * the buffer's first row). */
typedef struct {
    uint16_t left, right, skip, x_off, rows;
    long src_off, dst_off;
} span;

/* 0: nothing is drawn; 1: the span; -1: the original's count of rows, or
 * for a masked draw of bytes in a row, is 0, which its loops take as
 * 65536, copying over memory. */
static int span_of(const cok_picture *src, int x, int y, bool masked, span *p)
{
    uint16_t units = src->units, height = src->height;
    p->left = x < 0 ? (uint16_t)-x : 0;
    p->x_off = x < 0 ? 0 : (uint16_t)x;
    uint16_t end = (uint16_t)(x + units);
    if (BUFFER_UNITS >= end) {
        p->right = 0;
        p->skip = (uint16_t)(BUFFER_UNITS - x - units);
    } else {
        p->right = (uint16_t)(end - BUFFER_UNITS);
        p->skip = 0;
    }
    uint16_t dst_row = 0;
    if (y < 0) {
        uint16_t top = (uint16_t)(-y * 8);
        p->rows = (uint16_t)(height - top);
        p->src_off = (long)top * units;
    } else {
        dst_row = (uint16_t)(y * 8);
        p->src_off = 0;
        p->rows = (uint16_t)(dst_row + height) > BUFFER_ROWS ? (uint16_t)(BUFFER_ROWS - dst_row)
                                                             : height;
    }
    p->dst_off = (long)dst_row * BUFFER_UNITS * 4;
    if (units < p->left || units < p->right || p->src_off > (long)src->frame_size ||
        p->dst_off / 4 > BUFFER_UNITS * 4L * BUFFER_ROWS || p->rows > height)
        return 0;
    if (p->rows == 0 || (masked && units == p->left + p->right)) return -1;
    return 1;
}

static bool disaster(cok_adventure *game, int x, int y, const char *where)
{
    return fail(game, "a picture drawn at %d,%d, just outside the map's buffer, copies 65536 "
                      "rows or bytes (127f:27af, %s)", x, y, where);
}

/* The buffer's bytes a masked draw spanning p of frame of src writes
 * whole (its mask 0 there) are known again; under the rest they stay as
 * they were. */
static void known_under(cok_combat *c, const span *p, const cok_picture *src, size_t frame)
{
    size_t width = (size_t)(src->units - p->left - p->right) * 4;
    size_t from = (size_t)p->src_off * 4 + frame * src->frame_size, to = (size_t)p->dst_off;
    for (uint16_t row = 0; row < p->rows; ++row) {
        from += (size_t)p->left * 4;
        to += (size_t)p->x_off * 4;
        for (size_t b = 0; b < width; ++b, ++from, ++to)
            if (src->mask[from] == 0) c->unknown[to] = 0;
        to += (size_t)p->skip * 4;
        from += (size_t)p->right * 4;
    }
}

/* Draw frame of src masked at unit x, cell row y of the map's buffer
 * (127f:09c3), clipped. */
static bool buffer_draw(cok_adventure *game, const cok_picture *src, size_t frame, int x, int y,
                        const char *where)
{
    span p;
    int r = span_of(src, x, y, true, &p);
    if (r < 0) return disaster(game, x, y, where);
    if (r == 0) return true;
    cok_picture_draw(&game->combat.buffer, src, frame, x, y, COK_DRAW_MASKED, NULL);
    known_under(&game->combat, &p, src, frame);
    return true;
}

/* Draw frame of src masked at unit x, cell row y, saving what it covers
 * in DS:4b7c (127f:09c3 with flags 5), byte by byte as the original: its
 * save runs on in a row by the units clipped on the right but not on the
 * left. */
static bool save_draw(cok_adventure *game, const cok_picture *src, size_t frame, int x, int y,
                      const char *where)
{
    cok_combat *c = &game->combat;
    span p;
    int r = span_of(src, x, y, true, &p);
    if (r < 0) return disaster(game, x, y, where);
    if (r == 0) return true;
    if (src->mask == NULL || frame >= src->frames || src->frame_size != c->under.frame_size)
        return fail(game, "a picture saved over is not 24 by 24 (127f:27af, %s)", where);
    size_t width = (size_t)(src->units - p.left - p.right) * 4;
    size_t from = (size_t)p.src_off * 4 + frame * src->frame_size, save = (size_t)p.src_off * 4;
    size_t to = (size_t)p.dst_off;
    for (uint16_t row = 0; row < p.rows; ++row) {
        from += (size_t)p.left * 4;
        to += (size_t)p.x_off * 4;
        for (size_t b = 0; b < width; ++b, ++from, ++save, ++to) {
            if (save >= c->under.frame_size)
                return fail(game, "a save runs past its picture (127f:27af, %s)", where);
            c->under.pixels[save] = c->buffer.pixels[to];
            c->under_set[save] = c->unknown[to] == 0;
            c->buffer.pixels[to] =
                (uint8_t)((c->buffer.pixels[to] & src->mask[from]) | src->pixels[from]);
            if (src->mask[from] == 0) c->unknown[to] = 0;
        }
        to += (size_t)p.skip * 4;
        from += (size_t)p.right * 4;
        save += (size_t)p.right * 4;
    }
    return true;
}

/* Draw what the last save covered back, opaque, at unit x, cell row y
 * (127f:09c3 of DS:4b7c): its rows read as long as the bytes drawn in a
 * row, so that clipped on the right it shears, from bytes of earlier
 * saves. Missiles are clipped only at the step that leaves the view; the
 * map is redrawn whole before it is shown unless that step was the last,
 * when the target's drawing shows the shear (6346:1ba6 jumps from 1f6a to
 * 21a2). Bytes no save has written, which would be the heap's, are marked
 * unknown in the buffer until something covers them. */
static bool restore(cok_adventure *game, int x, int y, const char *where)
{
    cok_combat *c = &game->combat;
    span p;
    int r = span_of(&c->under, x, y, false, &p);
    if (r < 0) return disaster(game, x, y, where);
    if (r == 0) return true;
    size_t width = (size_t)(c->under.units - p.left - p.right) * 4;
    size_t from = (size_t)p.src_off * 4, to = (size_t)p.dst_off;
    for (uint16_t row = 0; row < p.rows; ++row) {
        to += (size_t)p.x_off * 4;
        for (size_t b = 0; b < width; ++b, ++from, ++to) {
            if (from >= c->under.frame_size)
                return fail(game, "a restore runs past its save (127f:27af, %s)", where);
            c->buffer.pixels[to] = c->under.pixels[from];
            c->unknown[to] = c->under_set[from] ? 0 : 1;
        }
        to += (size_t)p.skip * 4;
    }
    return true;
}

bool cok_arena_show(cok_adventure *game)
{
    const cok_combat *c = &game->combat;
    for (size_t i = 0; i < sizeof c->unknown; ++i)
        if (c->unknown[i] != 0)
            return fail(game, "a byte of DS:4b7c no save has written, drawn back sheared, is "
                              "shown (127f:27af, 127f:12e8)");
    cok_picture_draw(&game->screen, &game->combat.buffer, 0, 1, 1, 0, NULL);
    return true;
}

bool cok_arena_icon(cok_adventure *game, int8_t x, int8_t y, uint8_t facing, uint8_t image,
                    uint8_t slot)
{
    if (slot >= COK_ICON_SLOTS || image > 1)
        return fail(game, "icon %u of slot %u is past the icons (DS:6172, 6d21:04b0)", image, slot);
    const cok_picture *icon = &game->icons[slot][image];
    if (icon->pixels == NULL) return true;
    if (icon->mask == NULL) return fail(game, "icon slot %u has no mask (6d21:04b0)", slot);
    if (!pictures(game)) return false;
    if (facing <= 3) return buffer_draw(game, icon, 0, x * 3, y * 3, "6d21:04b0");
    cok_picture mirrored = {0};
    if (cok_picture_create(&mirrored, icon->units, icon->height, 1, 1) != COK_PICTURE_OK ||
        cok_picture_mirror(&mirrored, icon) != COK_PICTURE_OK) { /* 127f:0b94 */
        cok_picture_free(&mirrored);
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
        return false;
    }
    bool ok = buffer_draw(game, &mirrored, 0, x * 3, y * 3, "6d21:04b0");
    cok_picture_free(&mirrored);
    return ok;
}

/* Combatant n's facing (combat record +9). */
static uint8_t facing_of(const cok_character *c)
{
    return cok_character_combat(c)->facing;
}

/* Draw combatant n's icon, ready, at its screen position if it is partly
 * shown (6beb:00fc, 02aa). */
static bool draw_combatant(cok_adventure *game, uint8_t n, const char *where)
{
    const cok_combatant *e = &game->combat.combatant[n];
    if (n > COK_COMBATANTS || e->character == NULL)
        return fail(game, "combatant %u has no record (%s)", n, where);
    bool shown;
    if (!cok_combat_visible(&game->combat, e->character->record, false, &shown))
        return fail(game, "combatant %u's footprint is past the table (6beb:06ef, %s)", n, where);
    if (!shown) return true;
    return cok_arena_icon(game, e->screen_x, e->screen_y, facing_of(e->character), 0,
                          e->character->record[0x137]);
}

/* 6beb:07a9: scroll toward x, y (cok_combat_scroll), and if it does, draw
 * the 49 tiles shown. */
static bool scroll(cok_adventure *game, int8_t x, int8_t y, uint8_t margin, bool *redrew)
{
    cok_combat *c = &game->combat;
    *redrew = cok_combat_scroll(c, x, y, margin);
    if (!*redrew) return true;
    for (int row = 0; row < 7; ++row)
        for (int column = 0; column < 7; ++column)
            if (!terrain(game, column * 3, row * 3, s8(c->view_x + column), s8(c->view_y + row),
                         "6beb:07a9"))
                return false;
    return true;
}

/* 6beb:00fc: the terrain of each cell of the cursor's footprint (map +5)
 * at x, y that is shown, with the cursor's box over it while it is on
 * (map +4); then the icon of the cell's combatant if it is partly shown. */
static bool cursor(cok_adventure *game, int8_t x, int8_t y)
{
    cok_combat *c = &game->combat;
    int8_t sx = s8(x - c->view_x), sy = s8(y - c->view_y);
    if (c->cursor_size >= COK_COMBAT_SIZES)
        return fail(game, "the cursor's footprint %u is past the table (6beb:00fc)",
                    c->cursor_size);
    for (unsigned k = 0; k < 4; ++k) {
        int8_t dx, dy;
        if (!cok_combat_footprint(c->cursor_size, k, &dx, &dy)) continue;
        if (!cok_combat_on_view(s8(sx + dx), s8(sy + dy))) continue;
        if (!terrain(game, (sx + dx) * 3, (sy + dy) * 3, x + dx, y + dy, "6beb:00fc")) return false;
        if (c->cursor != 0 && !cok_arena_icon(game, s8(sx + dx), s8(sy + dy), 0, 0, SLOT_CURSOR))
            return false;
    }
    uint8_t occupant, value;
    cok_combat_cell(c, x, y, &occupant, &value);
    return occupant == 0 || draw_combatant(game, occupant, "6beb:00fc");
}

bool cok_arena_erase(cok_adventure *game, int8_t xs, int8_t ys, uint8_t n)
{
    if (!map_there(game, "6beb:0500")) return false;
    cok_combat *c = &game->combat;
    int8_t mx = s8(c->view_x + xs), my = s8(c->view_y + ys);
    if (n == 0) {
        uint8_t value;
        cok_combat_cell(c, mx, my, &n, &value);
    }
    if (n == 0)
        return !cok_combat_on_view(xs, ys) || terrain(game, xs * 3, ys * 3, mx, my, "6beb:0500");
    if (n > COK_COMBATANTS) return fail(game, "combatant %u is past the table (6beb:0500)", n);
    const cok_combatant *e = &c->combatant[n];
    xs = e->screen_x;
    ys = e->screen_y;
    mx = s8(c->view_x + xs);
    my = s8(c->view_y + ys);
    if (e->size >= COK_COMBAT_SIZES)
        return fail(game, "combatant %u's footprint %u is past the table (6beb:0500)", n, e->size);
    for (unsigned k = 0; k < 4; ++k) {
        int8_t dx, dy;
        if (!cok_combat_footprint(e->size, k, &dx, &dy)) continue;
        if (!cok_combat_on_view(s8(xs + dx), s8(ys + dy))) continue;
        if (!terrain(game, (xs + dx) * 3, (ys + dy) * 3, mx + dx, my + dy, "6beb:0500"))
            return false;
    }
    return true;
}

bool cok_arena_redraw_cell(cok_adventure *game, int8_t x, int8_t y)
{
    if (!map_there(game, "6beb:02aa")) return false;
    cok_combat *c = &game->combat;
    if (!cok_arena_erase(game, s8(x - c->view_x), s8(y - c->view_y), 0)) return false;
    uint8_t occupant, value;
    cok_combat_cell(c, x, y, &occupant, &value);
    return occupant == 0 || draw_combatant(game, occupant, "6beb:02aa");
}

bool cok_arena_centre(cok_adventure *game, int8_t x, int8_t y, uint8_t margin, uint8_t dir)
{
    if (!map_there(game, "6beb:096b")) return false;
    if (dir > 8) return fail(game, "direction %u is past the steps (DS:1ed6, 6beb:096b)", dir);
    cok_combat *c = &game->combat;
    int8_t tx = s8(x + step_x[dir]), ty = s8(y + step_y[dir]);
    bool redrew;
    if (!scroll(game, tx, ty, margin, &redrew)) return false;
    /* Every combatant that can act or whose status is 10, from 1 to the
     * count; the original reads the empty entry's record before its size,
     * which is 0. */
    for (unsigned n = 1; redrew && n <= c->count && n <= COK_COMBATANTS; ++n) {
        const cok_combatant *e = &c->combatant[n];
        if (e->size == 0 || e->character == NULL) continue;
        const uint8_t *r = e->character->record;
        if (r[0x188] != 10 && r[0x189] == 0) continue;
        if (!draw_combatant(game, (uint8_t)n, "6beb:096b")) return false;
    }
    if (!cok_arena_redraw_cell(game, x, y)) return false;
    if (!cok_combat_on_view(s8(tx - c->view_x), s8(ty - c->view_y))) {
        if (tx > COK_COMBAT_WIDTH - 1) tx = COK_COMBAT_WIDTH - 1;
        if (tx < 0) tx = 0;
        if (ty > COK_COMBAT_HEIGHT - 1) ty = COK_COMBAT_HEIGHT - 1;
        if (ty < 0) ty = 0;
    }
    if (!cursor(game, tx, ty)) return false;
    return cok_arena_show(game);
}

bool cok_arena_redraw(cok_adventure *game)
{
    if (!map_there(game, "6346:300f")) return false;
    uint16_t moons[3];
    for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
    cok_screen_combat(&game->screen, &game->view.tiles[4], moons); /* 1128:04c1 */
    cok_combat *c = &game->combat;
    return cok_arena_centre(game, s8(c->view_x + 3), s8(c->view_y + 3), 0xff, 8);
}

/* c's combatant, or false (6beb:0c43 giving 0, whose entry the original
 * reads or writes as the count). */
static bool combatant_of(cok_adventure *game, const cok_character *c, uint8_t *n,
                         const char *where)
{
    *n = cok_combat_index(&game->combat, c->record);
    if (*n != 0 && c->combat != NULL) return true;
    return fail(game, "%.*s is not a combatant (6beb:0c43, %s)",
                c->record[0] > 15 ? 15 : c->record[0], (const char *)c->record + 1, where);
}

static bool shown_whole(cok_adventure *game, const cok_character *c, bool *whole,
                        const char *where)
{
    if (cok_combat_visible(&game->combat, c->record, true, whole)) return true;
    return fail(game, "a footprint past the table (6beb:06ef, %s)", where);
}

bool cok_arena_pose(cok_adventure *game, cok_character *c, uint8_t pose, uint8_t image,
                    bool hide)
{
    if (!map_there(game, "6beb:0ad8")) return false;
    cok_combat *cb = &game->combat;
    uint8_t n;
    bool whole, shown;
    if (!combatant_of(game, c, &n, "6beb:0ad8") || !shown_whole(game, c, &whole, "6beb:0ad8"))
        return false;
    if (!whole && cb->show_actions &&
        !cok_arena_centre(game, cok_combat_x(cb, c->record), cok_combat_y(cb, c->record), 3, 8))
        return false;
    if (((pose >> 2) != (c->combat->facing >> 2) || image != 0 || hide) && cb->show_actions &&
        !cok_arena_erase(game, 0, 0, n))
        return false;
    c->combat->facing = pose;
    if (hide) return true;
    if (!cok_combat_visible(cb, c->record, false, &shown))
        return fail(game, "a footprint past the table (6beb:06ef, 6beb:0ad8)");
    if (!shown || !cb->show_actions) return true;
    const cok_combatant *e = &cb->combatant[n];
    if (!cok_arena_icon(game, e->screen_x, e->screen_y, pose, image, c->record[0x137]))
        return false;
    return cok_arena_show(game);
}

bool cok_arena_turn(cok_adventure *game, cok_character *c, uint8_t margin, bool cursor_on)
{
    if (!map_there(game, "6beb:12ef")) return false;
    cok_combat *cb = &game->combat;
    /* Only the centring needs c to be a combatant: the footprint of one
     * that is not is entry 0's size, which the count overlays, and is
     * put back at once. */
    uint8_t n = cok_combat_index(cb, c->record);
    if (cb->show_actions && !combatant_of(game, c, &n, "6beb:12ef")) return false;
    cb->cursor = cursor_on;
    cb->cursor_size = cok_combat_size(cb, c->record);
    bool ok = !cb->show_actions || cok_arena_centre(game, cok_combat_x(cb, c->record),
                                                    cok_combat_y(cb, c->record), margin, 8);
    cb->cursor = 0;
    cb->cursor_size = 1;
    return ok;
}

/* Whether entry b of the body table (DS:69ee + 7b) is c, for the bytes
 * that read a record pointer the port holds: false with *known clear for
 * any other. */
static bool entry_is(const cok_combat *cb, const cok_arena_reachable *also, unsigned b,
                     const cok_character *c, bool *known)
{
    *known = true;
    if (b == 0 || b == 0x24) return false;
    if (b >= 1 && b <= COK_COMBAT_BODIES) return cb->body[b - 1].character == c;
    if (b == 0x31 || b == 0x35 || b == 0x39)
        return also != NULL && also->exploding[(b - 0x31) / 4] == c;
    if (b >= 0xdb && b <= 0xff && (b - 0xdb) % 4 == 0)
        return also != NULL && also->targets[(b - 0xdb) / 4] == c;
    *known = false;
    return false;
}

bool cok_arena_kill(cok_adventure *game, cok_character *c, int stale,
                    const cok_arena_reachable *also)
{
    if (game->vm.mode != 5) {
        sound(game, 5); /* DS:1e52 */
        cok_adventure_wait(game, game->speed * 100u);
        return true;
    }
    if (!map_there(game, "6beb:0e08")) return false;
    cok_combat *cb = &game->combat;
    /* The search for a body of c by the stale byte (see arena.h). */
    bool known;
    if (stale == COK_ARENA_STALE_UNKNOWN) {
        for (unsigned b = 0; b < 256; ++b)
            if (entry_is(cb, also, b, c, &known))
                return fail(game, "whether a body is found depends on a byte the stack holds "
                                  "(6beb:0e08)");
    } else {
        if (entry_is(cb, also, (unsigned)stale & 0xffu, c, &known)) return true;
        if (!known || stale < 0 || stale > 0xff)
            return fail(game, "the body table's entry %d is past it (6beb:0e08)", stale);
    }
    uint8_t n;
    bool whole;
    if (!combatant_of(game, c, &n, "6beb:0e08") || !shown_whole(game, c, &whole, "6beb:0e08"))
        return false;
    int8_t x = cok_combat_x(cb, c->record), y = cok_combat_y(cb, c->record);
    if (!whole && !cok_arena_centre(game, x, y, 3, 8)) return false;
    if (!cok_arena_erase(game, 0, 0, n)) return false;
    sound(game, 5);
    const cok_combatant *e = &cb->combatant[n];
    if (e->size >= COK_COMBAT_SIZES)
        return fail(game, "combatant %u's footprint %u is past the table (6beb:0e08)", n, e->size);
    for (unsigned k = 0; k < 9; ++k) {
        const cok_picture *skull = &game->icons[SLOT_SKULL][k % 2];
        if (skull->pixels == NULL) return fail(game, "the skull's icon is not loaded (6beb:0e08)");
        for (unsigned j = 0; j < 4; ++j) {
            int8_t dx, dy;
            if (!cok_combat_footprint(e->size, j, &dx, &dy)) continue;
            if (!cok_combat_on_view(s8(e->screen_x + dx), s8(e->screen_y + dy))) continue;
            if (!save_draw(game, skull, 0, (e->screen_x + dx) * 3, (e->screen_y + dy) * 3,
                           "6beb:0e08"))
                return false;
        }
        if (!cok_arena_show(game)) return false;
        cok_adventure_wait(game, 10);
    }
    if (c->combat->not_party == 0) {
        if (cb->bodies >= COK_COMBAT_BODIES)
            return fail(game, "a ninth body overruns the table (DS:69ee, 6beb:0e08)");
        if (x < 0 || x >= COK_COMBAT_WIDTH || y < 0 || y >= COK_COMBAT_HEIGHT)
            return fail(game, "a body off the map is written outside it (6beb:0e08)");
        cok_combat_body *b = &cb->body[cb->bodies++];
        b->character = c;
        b->x = x;
        b->y = y;
        b->cell = cb->cells[y][x];
        if (b->cell != 0x1e) cb->cells[y][x] = COK_COMBAT_BODY;
    }
    cok_adventure_wait(game, game->speed * 100u); /* 1521:0b4b */
    if (!cok_arena_erase(game, 0, 0, n)) return false;
    cb->combatant[n].size = 0;
    if (!cok_combat_occupy(cb))
        return fail(game, "a footprint is written outside the occupants (6beb:0375)");
    if (!cok_arena_centre(game, s8(cb->view_x + 3), s8(cb->view_y + 3), 3, 8)) return false;
    c->combat->initiative = 0;
    c->combat->movement = 0;
    c->combat->spell = 0;
    c->combat->guarding = 0;
    return true;
}

/* Icons. */

/* 6d21:01d0 on the length characters of name, which may hold a NUL, with
 * file the digit's ECL file (DS:5782). */
static bool load_icon(cok_adventure *game, const char *name, size_t length, uint8_t id,
                      uint8_t slot, uint8_t file)
{
    if (slot >= COK_ICON_SLOTS)
        return fail(game, "icon slot %u is past the icon table (DS:6172 + 8 * slot)", slot);
    char path[32];
    bool cpic = false;
    if (length >= 5 && (memcmp(name, "CHEAD", 5) == 0 || memcmp(name, "CBODY", 5) == 0)) {
        if (toupper((unsigned char)name[length - 1]) == 'T') id = (uint8_t)(id + 0x40);
        /* The last character dropped: the letter, or "D" of a bare name. */
        snprintf(path, sizeof path, "%.*s", (int)(length - 1 < 16 ? length - 1 : 16), name);
    } else if ((length == 6 && memcmp(name, "COMSPR", 6) == 0) ||
               (length == 4 && memcmp(name, "ICON", 4) == 0)) {
        snprintf(path, sizeof path, "%.*s", (int)length, name);
    } else {
        /* Str into a string of 1: the first digit of the file's number. */
        char digit[8];
        snprintf(digit, sizeof digit, "%u", file);
        snprintf(path, sizeof path, "%.*s%c", (int)(length < 16 ? length : 16), name, digit[0]);
        cpic = true;
    }
    for (unsigned image = 0; image < 2; ++image) {
        uint8_t record = (uint8_t)(id + 0x80 * image);
        cok_picture *icon = &game->icons[slot][image];
        if (cok_adventure_load_image(game, path, record, 0, icon)) { /* 127f:0111 */
            if (cpic && cok_picture_recolor(icon, same_colours, black_colours, 0, NULL) !=
                            COK_PICTURE_OK) {
                cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
                return false;
            }
            continue;
        }
        bool no_file;
        size_t size;
        uint8_t *data = cok_adventure_find_record(game, path, record, &size, &no_file);
        free(data);
        if (no_file) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return false;
        }
        cok_adventure_log(game, "error", game->error);
    }
    return true; /* 1614:045c, the keyboard's flush, is not ported. */
}

bool cok_arena_load_icon(cok_adventure *game, const char *name, uint8_t id, uint8_t slot)
{
    return load_icon(game, name, strlen(name), id, slot, game->vm.file);
}

void cok_arena_free_icon(cok_adventure *game, uint8_t slot)
{
    if (slot >= COK_ICON_SLOTS) return;
    for (size_t image = 0; image < 2; ++image) cok_picture_free(&game->icons[slot][image]);
}

bool cok_arena_compose(const cok_picture *head, cok_picture *body)
{
    if (head->pixels == NULL || head->mask == NULL || body->pixels == NULL || body->mask == NULL ||
        head->frame_size > body->frame_size)
        return false;
    for (size_t i = 0; i < head->frame_size; ++i) {
        body->pixels[i] |= head->pixels[i];
        body->mask[i] &= head->mask[i];
    }
    return true;
}

bool cok_arena_party_icon(cok_adventure *game, cok_character *c, bool colours)
{
    const uint8_t *r = c->record;
    uint8_t size = r[0x138], slot = r[0x137];
    if (size > 2)
        return fail(game, "icon size %u reads its letter from the strings after DS:0877 "
                          "(4b6d:0817)", size);
    static const char letters[3] = {'\0', 'S', 'T'}; /* DS:0875 */
    char head[6] = {'C', 'H', 'E', 'A', 'D', letters[size]};
    char body[6] = {'C', 'B', 'O', 'D', 'Y', letters[size]};
    if (!load_icon(game, head, sizeof head, r[0x135], SLOT_HEAD, 1) ||
        !load_icon(game, body, sizeof body, r[0x136], slot, 1))
        return false;
    bool ok = true;
    for (unsigned image = 0; image < 2 && ok; ++image) {
        cok_picture *icon = &game->icons[slot][image];
        if (!cok_arena_compose(&game->icons[SLOT_HEAD][image], icon)) {
            ok = fail(game, "a character's head or body (+0x135, +0x136: %u, %u) is not there to "
                            "compose (4b6d:0784)", r[0x135], r[0x136]);
            break;
        }
        ok = cok_picture_recolor(icon, same_colours, black_colours, 0, NULL) == COK_PICTURE_OK;
    }
    if (ok && colours) {
        uint8_t to[16];
        memcpy(to, same_colours, sizeof to);
        for (unsigned k = 0; k < 6; ++k) {
            to[icon_colours[k]] = r[0x139 + k] & 15;
            to[icon_colours[k] + 8] = r[0x139 + k] >> 4;
        }
        for (unsigned image = 0; image < 2 && ok; ++image)
            ok = cok_picture_recolor(&game->icons[slot][image], same_colours, to, 0, NULL) ==
                 COK_PICTURE_OK;
    }
    cok_arena_free_icon(game, SLOT_HEAD); /* 6d21:0156 */
    if (!ok && game->vm.status == COK_ECL_OK)
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
    return ok;
}

bool cok_arena_join(cok_adventure *game, uint8_t file)
{
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        const uint8_t *r = c->record;
        bool ok = r[0xe7] < 0x80 ? cok_arena_party_icon(game, c, true)
                                 : load_icon(game, "CPIC", 4, r[0x115], r[0x137], file);
        if (!ok) return false;
    }
    return true;
}

/* The panel and messages. */

/* The window is full: wait for a key (1614:025b), as PRINT pages. */
static void page(void *context)
{
    cok_keyboard keys = cok_adventure_keyboard(context);
    keys.read(keys.context);
}

/* Print text wrapped in window from the cursor, clearing it first, without
 * logging it (1521:04ac). */
static void wrap(cok_adventure *game, const char *text, cok_text_window window)
{
    cok_text_hooks hooks = {page, NULL, game};
    cok_text_wrap(&game->screen, &game->font, &game->vm.cursor, text, window, 10, 0, true, &hooks);
}

static void text_at(cok_adventure *game, const char *text, int x, int y, uint8_t fg)
{
    cok_text_string(&game->screen, &game->font, text, x, y, fg, 0); /* 1521:0353 */
}

/* 6346:1883, logging the name and text if log. */
static void say(cok_adventure *game, cok_character *c, const char *text, uint8_t row, bool wait,
                bool log)
{
    if (game->vm.mode != 5) {
        cok_camp_say(game, c->record, text, wait);
        return;
    }
    clear_cells(game, 0x17, row, 0x26, 0x15);
    cok_item_draw_name(game, c->record, 0x17, row, false); /* 6346:199d */
    if (log) {
        char name[16];
        snprintf(name, sizeof name, "%.*s", c->record[0] > 15 ? 15 : c->record[0],
                 (const char *)c->record + 1);
        cok_adventure_log(game, "print", name);
        cok_adventure_print(game, text, (cok_text_window){0x17, row + 1, 0x26, 0x15}, 10, true);
    } else {
        wrap(game, text, (cok_text_window){0x17, row + 1, 0x26, 0x15});
    }
    if (!wait || game->vm.abort) return;
    cok_adventure_wait(game, game->speed * 100u);
    cok_arena_clear_text(game);
}

bool cok_arena_say(cok_adventure *game, cok_character *c, const char *text, uint8_t row,
                   bool wait)
{
    /* Below row 20 the text's window ends above where it starts: the
     * original prints it over the frame's row 22 and below, which the
     * port's text routines do not. */
    if (game->vm.mode == 5 && row > 20)
        return fail(game, "text said from row %u runs below the side panel over the frame "
                          "(6346:1883)", row);
    say(game, c, text, row, wait, true);
    return true;
}

void cok_arena_clear_text(cok_adventure *game)
{
    if (game->vm.mode == 5)
        clear_cells(game, 0x17, 0x0a, 0x26, 0x15);
    else
        cok_camp_clear_text(game);
}

bool cok_arena_panel(cok_adventure *game, cok_character *c)
{
    cok_combat *cb = &game->combat;
    if (!cb->panel) return true;
    cb->panel = false;
    const uint8_t *r = c->record;
    clear_cells(game, 0x17, 1, 0x26, 0x15);
    say(game, c, " ", 1, false, false);
    char text[48];
    text_at(game, "Hitpoints", 0x17, 3, 10);
    snprintf(text, sizeof text, "%u", r[0x197]); /* 6346:0a0d */
    text_at(game, text, 0x21, 3, r[0x197] < r[0x62] ? 14 : 10);
    text_at(game, "AC", 0x17, 5, 10);
    uint8_t ac = r[0x18d]; /* 6346:0984 */
    snprintf(text, sizeof text, "%s%u", ac > 60 ? "-" : "", (unsigned)abs(ac - 60) & 0xffu);
    text_at(game, text, 0x1a, 5, 10);
    game->vm.cursor.y = 5;
    if (c->slots[0] != 0) {
        char name[41];
        if (!cok_item_name(game, c->items[c->slots[0] - 1], false, name)) return false;
        wrap(game, name, (cok_text_window){0x17, 7, 0x26, 9});
    }
    int row = game->vm.cursor.y + 2;
    if (r[0x189] == 0) {
        char status[256];
        if (!cok_ds_string((uint16_t)(0x1330 + 13 * (int8_t)r[0x188]), status))
            return fail(game, "status %u has no name the original can show (DS:1330, 6346:0af6)",
                        r[0x188]);
        text_at(game, status, 0x17, row, 15);
    } else if (cok_combat_helpless(c)) { /* 6346:0cdb */
        text_at(game, "(Helpless)", 0x17, row, 15);
    } else if (cok_character_combat(c)->spell > 0) {
        text_at(game, "(Casting)", 0x17, row, 15);
    }
    return true;
}

/* Missiles and flashes. */

bool cok_arena_missile_frame(cok_adventure *game, uint8_t slot, uint8_t image, uint8_t frame,
                             bool mirror)
{
    if (!pictures(game)) return false;
    if (slot >= COK_ICON_SLOTS || image > 1)
        return fail(game, "icon slot %u's picture %u is past the icon table (6346:1a26)", slot,
                    image);
    cok_picture *flash = &game->combat.flash;
    if (frame >= flash->frames)
        return fail(game, "the missile's picture %u is past its four (DS:719e, 6346:1a26)", frame);
    const cok_picture *icon = &game->icons[slot][image];
    if (icon->pixels == NULL || icon->mask == NULL || icon->units != flash->units ||
        icon->height != flash->height)
        return fail(game, "icon slot %u is not a picture of 24 by 24 (6346:1a26)", slot);
    cok_picture copy = {0};
    const cok_picture *from = icon;
    if (mirror) {
        if (cok_picture_create(&copy, 3, 24, 1, 1) != COK_PICTURE_OK ||
            cok_picture_mirror(&copy, icon) != COK_PICTURE_OK) {
            cok_picture_free(&copy);
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
            return false;
        }
        from = &copy;
    }
    memcpy(flash->pixels + frame * flash->frame_size, from->pixels, flash->frame_size);
    memcpy(flash->mask + frame * flash->frame_size, from->mask, flash->frame_size);
    cok_picture_free(&copy);
    return true;
}

bool cok_arena_missile_frames(cok_adventure *game, uint8_t slot)
{
    /* 6346:1b5b: ready, ready mirrored, attacking mirrored, attacking. */
    static const uint8_t images[4] = {0, 0, 1, 1}, mirrors[4] = {0, 1, 1, 0};
    for (uint8_t frame = 0; frame < 4; ++frame)
        if (!cok_arena_missile_frame(game, slot, images[frame], frame, mirrors[frame] != 0))
            return false;
    return true;
}

/* Draw the missile's picture frame at unit x, cell row y of the buffer,
 * masked, saving what it covers (127f:09c3 with 5). */
static bool draw_missile(cok_adventure *game, uint8_t frame, int x, int y)
{
    cok_combat *c = &game->combat;
    if (frame >= c->flash.frames)
        return fail(game, "the missile's picture %u is past its four (DS:719e, 6346:1ba6)", frame);
    return save_draw(game, &c->flash, frame, x, y, "6346:1ba6");
}

bool cok_arena_flash(cok_adventure *game, cok_character *c, uint8_t kind, const char *text)
{
    if (game->vm.mode != 5) {
        say(game, c, text, 10, true, true);
        return true;
    }
    if (!map_there(game, "6346:228c") || !cok_arena_missile_frames(game, kind != 0 ? 0x16 : 0x17))
        return false;
    cok_combat *cb = &game->combat;
    bool whole;
    uint8_t n;
    if (!combatant_of(game, c, &n, "6346:228c") || !shown_whole(game, c, &whole, "6346:228c"))
        return false;
    if (!whole &&
        !cok_arena_centre(game, cok_combat_x(cb, c->record), cok_combat_y(cb, c->record), 3, 8))
        return false;
    sound(game, kind != 0 ? 4 : 3); /* DS:1e50, 1e4e */
    say(game, c, text, 10, false, true);
    uint8_t passes = kind != 0 ? game->speed : 0;
    int8_t x = s8(cb->combatant[n].screen_x * 3), y = s8(cb->combatant[n].screen_y * 3);
    for (unsigned pass = 0; pass <= passes; ++pass) {
        for (uint8_t frame = 0; frame < 4; ++frame) {
            if (!draw_missile(game, frame, x, y)) return false;
            if (!cok_arena_show(game)) return false;
            cok_adventure_wait(game, 70);
            if (!restore(game, x, y, "6346:228c")) return false;
        }
    }
    if (!cok_arena_show(game)) return false;
    if (passes == 0) cok_adventure_wait(game, game->speed * 100u);
    return true;
}

/* Rounds of a missile leaving the view and coming back in: the second
 * comes in near the target, from which it cannot leave. */
enum { MISSILE_ROUNDS = 8 };

bool cok_arena_missile(cok_adventure *game, int x0, int y0, int x1, int y1, uint8_t frames,
                       uint8_t delay)
{
    if (!map_there(game, "6346:1ba6")) return false;
    cok_combat *c = &game->combat;
    /* The step's directions along the line, 148 bytes of 8 (none). */
    uint8_t dirs[148];
    memset(dirs, 8, sizeof dirs);
    cok_combat_line l = {.x0 = (int16_t)(x0 * 3), .y0 = (int16_t)(y0 * 3),
                         .x1 = (int16_t)(x1 * 3), .y1 = (int16_t)(y1 * 3)};
    cok_combat_line_start(&l); /* 6b30:01a5 */
    uint8_t n = 0;
    bool done;
    do {
        done = !cok_combat_line_step(&l); /* 6b30:024c */
        if (n >= sizeof dirs)
            return fail(game, "a missile's line of more than 147 steps runs past its list "
                              "(6346:1ba6)");
        dirs[n++] = l.direction;
    } while (!done);
    uint8_t length = (uint8_t)(n - 2);
    if (length < 2 || n < 2) return true;
    int dx = x1 - x0, dy = y1 - y0;
    bool settled;
    int mid_x = c->view_x + 3, mid_y = c->view_y + 3;
    if (cok_combat_on_view(s8(x0 - c->view_x), s8(y0 - c->view_y)) &&
        cok_combat_on_view(s8(x1 - c->view_x), s8(y1 - c->view_y))) {
        settled = true;
    } else if (abs(dx) <= 6 && abs(dy) <= 6) {
        settled = true;
        mid_x = dx / 2 + x0;
        mid_y = dy / 2 + y0;
    } else {
        settled = false;
    }
    if (!cok_arena_centre(game, s8(mid_x), s8(mid_y), 0xff, 8)) return false;
    uint8_t shown = 0, i = 0;
    int ox = 0, oy = 0;
    for (unsigned round = 0;; ++round) {
        if (round == MISSILE_ROUNDS)
            return fail(game, "a missile that keeps leaving the view never lands (6346:1ba6)");
        /* Toward the target from the cell x0, y0 holds and the part of a
         * cell it has gone (the original moves its midpoint too, which it
         * does not read again). */
        int px = (x0 - c->view_x) * 3 + ox, py = (y0 - c->view_y) * 3 + oy;
        bool out;
        do {
            out = false;
            int sx = step_x[dirs[i]], sy = step_y[dirs[i]];
            px += sx;
            py += sy;
            if (delay > 0 || px % 3 == 0 || py % 3 == 0) {
                if (!draw_missile(game, shown, px, py)) return false;
                if (!cok_arena_show(game)) return false;
                cok_adventure_wait(game, delay); /* 1962:029c */
                if (!restore(game, px, py, "6346:1ba6")) return false;
                if (++shown >= frames) shown = 0;
            }
            ++i;
            if (px < 0 || px > LAST_SHOWN || py < 0 || py > LAST_SHOWN) out = true;
            if (!out && i < length) {
                ox += sx;
                oy += sy;
                if (abs(ox) == 3) {
                    x0 += sign(ox);
                    ox = 0;
                }
                if (abs(oy) == 3) {
                    y0 += sign(oy);
                    oy = 0;
                }
            }
        } while (i < length && !out);
        if (i >= length) break;
        /* It left the view: centre near the target, then walk back from it
         * to where it comes in. */
        ox = oy = 0;
        x0 = x1;
        y0 = y1;
        int ax = 0, ay = 0;
        if (x1 + 3 > COK_COMBAT_WIDTH - 1) ax = x1 - (COK_COMBAT_WIDTH - 1);
        else if (x1 < 3) ax = 3 - x1;
        if (y1 + 3 > COK_COMBAT_HEIGHT - 1) ay = y1 - (COK_COMBAT_HEIGHT - 1);
        else if (y1 < 3) ay = 3 - y1;
        if (!cok_arena_centre(game, s8(x1 + ax), s8(y1 + ay), 0xff, 8)) return false;
        px = (x1 - c->view_x) * 3;
        py = (y1 - c->view_y) * 3;
        i = length;
        do {
            out = false;
            int sx = -step_x[dirs[i]], sy = -step_y[dirs[i]];
            px += sx;
            py += sy;
            if (px > LAST_SHOWN) x0 = c->view_x + 6;
            else if (px < 0) x0 = c->view_x;
            if (py > LAST_SHOWN) y0 = c->view_y + 6;
            else if (py < 0) y0 = c->view_y;
            if (px < 0 || px > LAST_SHOWN || py < 0 || py > LAST_SHOWN) out = true;
            if (!out) {
                ox += sx;
                oy += sy;
                if (abs(ox) == 3) {
                    x0 += sign(ox);
                    ox = 0;
                }
                if (abs(oy) == 3) {
                    y0 += sign(oy);
                    oy = 0;
                }
                if (i == 0)
                    return fail(game, "a missile's walk back passes its line's start "
                                      "(6346:1ba6)");
                --i;
            }
        } while (!out);
        if (settled) return cok_arena_show(game);
    }
    /* At the target. */
    if (!cok_combat_on_view(s8(x1 - c->view_x), s8(y1 - c->view_y)) &&
        !cok_arena_centre(game, s8(x1), s8(y1), 3, 8))
        return false;
    int px = (x1 - c->view_x) * 3, py = (y1 - c->view_y) * 3;
    if (!draw_missile(game, shown, px, py)) return false;
    if (delay > 0) {
        if (!cok_arena_show(game)) return false;
        cok_adventure_wait(game, delay);
        if (!restore(game, px, py, "6346:1ba6")) return false;
    }
    return cok_arena_show(game);
}
