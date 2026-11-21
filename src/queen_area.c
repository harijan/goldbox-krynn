#include "queen.h"

#include "combat.h"
#include "data.h"
#include "ecl.h"
#include "round.h"

#include <assert.h>

/* The Dark Queen of Krynn's area and line spells (band b, part 31): the area damage of 7fd8's band b
 * (dqk 7fd8:1519 the Fireball family, 1772 and its hit 1a54 with the Lightning Bolt 1ac3, the Javelin 1d20,
 * Flame Strike 2157, Ice Storm 28fb, Cone of Cold 299c and its cone 19ff, Meteor Swarm 3b99 and its try 3a73).
 * The flight and the bursts of a spell are queen_cast.c's (51c2:0fc8 and 51c2:0def: queen_direction_picture and
 * queen_burst), run before each handler here. queen_area_handler is the table: a spell with no handler yet is
 * NULL, and queen_spell_handler then takes the plain table or stops at the descriptor's citation. */

/* b2: the Fireball family 0x2f 0x73 0x85 (7fd8:1519), Flame Strike 0x4a (2157), Ice Storm 0x57 (28fb). */

/* b3: Lightning Bolt 0x33 (7fd8:1ac3), Javelin of Lightning 0x81 (1d20). */

/* b4: Cone of Cold 0x5c (7fd8:299c). */

/* The cone's steps for part (0 left x, 1 left y, 2 right x, 3 right y) and side: DS:8cc8 + 8 * part + side,
 * the bytes the port holds in data_dark_queen.c (signed; side is a step's direction, 0-7). */
static int8_t cone_side(const cok_adventure *game, unsigned part, uint8_t side)
{
    return (int8_t)cok_data_held(cok_game_rules(game)->data, 0x8cc8 + 8 * (long)part + side);
}

/* 190d: x and y kept on the map (0 to 0x31 and 0 to 0x18). */
static void cone_clamp(int8_t *x, int8_t *y)
{
    if (*x > 0x31) *x = 0x31;
    else if (*x < 0) *x = 0;
    if (*y > 0x18) *y = 0x18;
    else if (*y < 0) *y = 0;
}

/* 194a: the combatants on a line's cells after its start, each added to list where it is not yet; false
 * where the list would run past 30. */
static bool cone_add(const cok_combat *combat, const cok_rules *rules, cok_combat_line *line, uint8_t list[32])
{
    while (cok_combat_line_step(line, rules)) {
        uint8_t occupant, terrain;
        cok_combat_cell(combat, (int8_t)line->x, (int8_t)line->y, &occupant, &terrain);
        if (occupant == 0) continue;
        unsigned k = 1;
        while (list[k] != occupant && list[k] != 0)
            if (++k > 30) return false;
        list[k] = occupant;
    }
    return true;
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* 849a:19ff: the cone from x0, y0 aimed at x1, y1 (the caster's cell and the spell's). The aim goes on along
 * its own steps while the walk (2 a straight step, 3 a diagonal one, from the line's length) is under 2 * len
 * and the end is on the map; the end is clamped and pulled back by the sight (range 2 * len), and the line to it
 * lists the combatants on its cells, each once, though none after one met a second time. For width 2 and up the
 * right side (the end's neighbour by the direction of the last step that met one), for 3 the left, add theirs; width
 * 0xff those within 3 of the end instead. The targets are spell_targets 1 to the count, the caster left out. */
bool queen_cone(cok_adventure *game, int8_t x0, int8_t y0, int8_t x1, int8_t y1, uint8_t width, uint8_t len)
{
    cok_combat *combat = &game->combat;
    const cok_rules *rules = cok_game_rules(game);
    const cok_data_segment *D = rules->data;
    cok_combat_line line = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
    cok_combat_line_start(&line);
    /* The directions of the first line's steps (a line on the 50 by 25 map has at most 49). */
    uint8_t dirs[50], k = 0;
    while (cok_combat_line_step(&line, rules)) {
        assert(k < 50);
        dirs[k++] = line.direction;
    }
    uint8_t last = (uint8_t)(k - 1), acc = line.length, len2 = (uint8_t)(len * 2), i = 0;
    while (acc < len2 && x1 < 0x31 && x1 > 0 && y1 < 0x18 && y1 > 0) {
        if (k == 0)
            return undefined(game, "dqk 849a:1a9d reads a direction its line never wrote (a cone aimed at "
                                   "its caster's own cell)");
        uint8_t d = dirs[i];
        if (d <= 7) acc = (uint8_t)(acc + ((d & 1) != 0 ? 3 : 2));
        x1 = (int8_t)(x1 + (int8_t)cok_data_held(D, cok_data_offset(D, COK_DATA_STEP_X, d)));
        y1 = (int8_t)(y1 + (int8_t)cok_data_held(D, cok_data_offset(D, COK_DATA_STEP_Y, d)));
        i = i == last ? 0 : (uint8_t)(i + 1);
    }
    cone_clamp(&x1, &y1);
    uint16_t range = len2;
    bool seen;
    if (!cok_combat_sight(combat, rules, x0, y0, &x1, &y1, &range, &seen))
        return undefined(game, "terrain past the table is read (DS:1ee4, 6b30:03f1)");
    /* The centre line: once an occupant has been met twice, no later one is added (again is never cleared). */
    uint8_t list[32] = {0}, n = 1, side = 0;
    bool again = false, side_set = false;
    line = (cok_combat_line){.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
    cok_combat_line_start(&line);
    while (cok_combat_line_step(&line, rules)) {
        uint8_t occupant, terrain;
        cok_combat_cell(combat, (int8_t)line.x, (int8_t)line.y, &occupant, &terrain);
        if (occupant == 0) continue;
        if (n > 1) {
            for (uint8_t j = 1; j < n; ++j)
                if (list[j] == occupant) again = true;
        } else {
            again = false;
        }
        if (!again) {
            if (n > 30)
                return undefined(game, "dqk 849a:1bd8: a cone's list of more than 30 runs past its directions");
            list[n++] = occupant;
        }
        side = line.direction;
        side_set = true;
    }
    static const char overrun[] = "dqk 849a:194a: a cone's list of more than 30 runs past its directions";
    if (width > 1 && width != 0xff) {
        if (!side_set)
            return undefined(game, "dqk 849a:1c4a reads the side of a cone whose centre line met no one (a "
                                   "local never written)");
        int8_t rx = (int8_t)(x1 + cone_side(game, 2, side)), ry = (int8_t)(y1 + cone_side(game, 3, side));
        cone_clamp(&rx, &ry);
        line = (cok_combat_line){.x0 = x0, .y0 = y0, .x1 = rx, .y1 = ry};
        cok_combat_line_start(&line);
        if (!cone_add(combat, rules, &line, list)) return undefined(game, overrun);
        if (width > 2) {
            int8_t lx = (int8_t)(x1 + cone_side(game, 0, side)), ly = (int8_t)(y1 + cone_side(game, 1, side));
            cone_clamp(&lx, &ly);
            line = (cok_combat_line){.x0 = x0, .y0 = y0, .x1 = lx, .y1 = ly};
            cok_combat_line_start(&line);
            if (!cone_add(combat, rules, &line, list)) return undefined(game, overrun);
        }
    }
    if (width == 0xff) {
        if (!cok_combat_list(combat, rules, x1, y1, 3, 0xff, 1))
            return undefined(game, "the list of combatants around one (6b30:08d8) reads past its tables");
        if (combat->listed_count > 30)
            return undefined(game, "dqk 849a:1d4b: a cone's list of more than 30 runs past its directions");
        for (uint8_t j = 1; j <= combat->listed_count; ++j) list[j] = combat->listed[j].index;
    }
    /* The targets: each listed combatant but the caster (the selected member), 1 on. */
    game->spell_target_count = 0;
    unsigned m = 1;
    for (unsigned j = 1;; ++j) {
        if (j > 30) return undefined(game, "dqk 849a:1e03: a cone's list of 30 is read on into its directions");
        if (list[j] == 0) break;
        cok_character *t = combat->combatant[list[j]].character;
        if (t->record == game->vm.character) continue;
        game->spell_targets[m++] = t;
        game->spell_target_count = (uint8_t)(m - 1);
    }
    return true;
}


/* b5: Meteor Swarm 0x7c (7fd8:3b99). */

queen_spell_fn *queen_area_handler(uint8_t id)
{
    switch (id) {
    /* b2: the Fireball family 0x2f 0x73 0x85 (7fd8:1519), Flame Strike 0x4a (2157), Ice Storm 0x57 (28fb) */

    /* b3: Lightning Bolt 0x33 (7fd8:1ac3), Javelin of Lightning 0x81 (1d20) */

    /* b4: Cone of Cold 0x5c (7fd8:299c) */

    /* b5: Meteor Swarm 0x7c (7fd8:3b99) */

    default: return NULL;
    }
}
