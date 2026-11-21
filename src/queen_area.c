#include "queen.h"

#include "arena.h"
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

/* The bar's words of the underwater refusal (DS:87c9, queen_cast.c's words). */
static const char underwater_text[] = "That has no effect underwater!";

/* Var 0x3c (COK_VAR_RECORD_EFFECT): underwater. */
static bool underwater(cok_adventure *game)
{
    return cok_ecl_var(&game->vm, COK_VAR_RECORD_EFFECT) != 0;
}

/* 148f:199e (count, sides): the sum of the dice, the count stored in rolls.dice (DS:b7f4). */
static uint8_t dice_count(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice_count(&game->vm.seed, count, sides, &game->effects.rolls.dice);
}

/* 7fd8:1519 (Fireball 0x2f, Delayed Blast Fireball 0x73, Necklace of Missiles 0x85; Lightning 0x33 underwater calls it
 * too, band b3): underwater the bar says it has no effect (not for 0x33), else the image flag is set and the count is the
 * caster level (the Necklace's d3 first: (2d + 1) of the dice). Underwater (0x33 only) the area is listed again at
 * radius 2, the centre is redrawn, and the damage is nd6 from fire (type 9); the Delayed Blast adds the count to it.
 * Exported for queen_area.c's table and for its tests (the underwater Lightning's path is band b3's). */
bool queen_fireball(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    uint8_t spell = r->spell;
    if (underwater(game) && spell != 0x33) {
        queen_bar_message(game, underwater_text);
        return true;
    }
    r->images = 1;
    uint8_t lv = 0;
    if (!queen_spell_level(game, spell, false, &lv)) return false;
    uint8_t n = lv;
    if (spell == 0x85) n = (uint8_t)(cok_dice(&game->vm.seed, 1, 3) * 2 + 1);
    if (underwater(game) && !queen_area_targets(game, 2)) return false;
    if (!cok_arena_centre(game, game->spell_x, game->spell_y, 0, 8)) return false;
    uint8_t bonus = spell == 0x73 ? n : 0;
    uint8_t amount = (uint8_t)(dice_count(game, n, 6) + bonus);
    return queen_spell_apply(game, spell, 0, false, amount, 9, "");
}

/* 7fd8:2157 (Flame Strike 0x4a): underwater the bar's refusal; else the image flag, and 6d8 of damage from fire (type 9),
 * halved by a save. */
static bool flame_strike(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    if (underwater(game)) {
        queen_bar_message(game, underwater_text);
        return true;
    }
    r->images = 1;
    return queen_spell_apply(game, r->spell, 0, false, dice_count(game, 6, 8), 9, "");
}

/* 7fd8:28fb (Ice Storm 0x57): the image flag first; 3d10 of damage from magic (type 8), one roll for every target, 1d10
 * underwater. No save roll (save kind 0). */
static bool ice_storm(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    r->images = 1;
    uint8_t count = underwater(game) ? 1 : 3;
    return queen_spell_apply(game, r->spell, 0, false, dice_count(game, count, 10), 8, "");
}

/* b3: Lightning Bolt 0x33 (7fd8:1ac3), Javelin of Lightning 0x81 (1d20). */

/* b4: Cone of Cold 0x5c (7fd8:299c). */

/* The cone's steps for part (0 left x, 1 left y, 2 right x, 3 right y) and side: DS:8cc8 + 8 * part + side,
 * the bytes the port holds in data_dark_queen.c (signed; side is a step's direction, 0-7, or 0xff where the centre
 * line met no one: DS:8dc7 + 8 * part). */
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
 * 0xff those within 3 of the end instead. The targets are spell_targets 1 to the count, the caster left out.
 * Where the centre line meets no one the side (19ff's bp-5) is never written: it holds the high byte of the caller's
 * BP, which 5837:0aea's push bp leaves there (the callers of width 2 and 3, 7fd8:299c and 257a, push 19ff's arguments
 * alike, 0aea the last call before it). Measured in DOSBox-X at 849a:1c4a: bp-6 0xff3c, 299c's BP; the handler's BP
 * is 0xff3c for a pending cast, 0xff1e at once, 0xff14 for the computer's (the dispatch 849a:21b5's 0xff42, 0xff24,
 * 0xff1a): 0xff, the sides at DS:8dc7, 8dcf, 8dd7, 8ddf (left x + 0x63, left y + 0, right x + 0x53, right y + 0x43:
 * the right end clamped to 0x31, 0x18, or 0, 0x18 from x 45 on). */
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
    uint8_t list[32] = {0}, n = 1, side = 0xff;
    bool again = false;
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
    }
    static const char overrun[] = "dqk 849a:194a: a cone's list of more than 30 runs past its directions";
    if (width > 1 && width != 0xff) {
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

/* 7fd8:299c, Cone of Cold (0x5c): one image; a cone of width 2 and length (lv + 1) / 2 (at least 1) from the
 * caster's cell to the spell's cell, lv the uncapped caster level; then dN(lv, 4) + lv of cold damage (type
 * 0x0a), "from Cold", half on a save (save kind 2). */
static bool cone_of_cold(cok_adventure *game)
{
    uint8_t spell = game->effects.rolls.spell, lv;
    if (!queen_spell_level(game, spell, false, &lv)) return false;
    game->effects.rolls.images = 1;
    uint8_t w = (uint8_t)((lv + 1) / 2);
    if (w == 0) w = 1;
    const uint8_t *caster = game->vm.character;
    if (!queen_cone(game, cok_combat_x(&game->combat, caster), cok_combat_y(&game->combat, caster),
                    game->spell_x, game->spell_y, 2, w))
        return false;
    uint8_t amount = (uint8_t)(cok_dice_count(&game->vm.seed, lv, 4, &game->effects.rolls.dice) + lv);
    return queen_spell_apply(game, spell, 0, false, amount, 0x0a, "from Cold");
}

/* b5: Meteor Swarm 0x7c (7fd8:3b99). */

queen_spell_fn *queen_area_handler(uint8_t id)
{
    switch (id) {
    /* b2: the Fireball family 0x2f 0x73 0x85 (7fd8:1519), Flame Strike 0x4a (2157), Ice Storm 0x57 (28fb) */
    case 0x2f:
    case 0x73:
    case 0x85: return queen_fireball;
    case 0x4a: return flame_strike;
    case 0x57: return ice_storm;

    /* b3: Lightning Bolt 0x33 (7fd8:1ac3), Javelin of Lightning 0x81 (1d20) */

    /* b4: Cone of Cold 0x5c (7fd8:299c) */
    case 0x5c: return cone_of_cold;

    /* b5: Meteor Swarm 0x7c (7fd8:3b99) */

    default: return NULL;
    }
}
