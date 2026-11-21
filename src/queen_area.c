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

/* The bolt (7fd8:1772) and its hit (7fd8:1a54), the line spells' engine: each segment of the line hits its last cell's
 * combatant once, the bolt bounces off a wall in var 0x22 1 and turns back toward the caster, and the length is charged per
 * segment (2 a straight step, 3 a diagonal one, at least 2). */

/* An undefined stop (the original reads unset memory, a NULL or a freed record): cok_adventure_fail. */
static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* The cost of a terrain value (DS:6197 + 4 * terr, the table's first byte); a value past the table is read
 * (7fd8:1772's 18c8), so undefined here. */
static bool terrain_cost(cok_adventure *game, uint8_t terr, uint8_t *cost)
{
    const cok_rules *rules = cok_game_rules(game);
    if (terr >= cok_data_count(rules->data, COK_DATA_TERRAIN, 0))
        return undefined(game, "a bolt reads a terrain past the table (DS:6197, 7fd8:1772)");
    *cost = cok_combat_terrain(rules, terr).cost;
    return true;
}

/* 7fd8:1a54 hit(x, y, amount): the combatant on cell x, y (none for 0) saves by type 4 (not bonused), takes amount (halved
 * by a save) and the missile's picture is built again (icon slot 0x14). A NULL or a freed record is read through NULL
 * (or the heap) there: undefined. */
static bool hit(cok_adventure *game, int8_t x, int8_t y, uint8_t amount)
{
    cok_combat *combat = &game->combat;
    uint8_t occ, terr;
    cok_combat_cell(combat, x, y, &occ, &terr);
    if (occ == 0) return true;
    cok_character *t = combat->combatant[occ].character;
    if (t == NULL) return undefined(game, "a bolt's cell has a combatant with no record (7fd8:1a54)");
    if (t == &cok_arena_freed)
        return undefined(game, "a bolt's cell has a record freed since (7fd8:1a54, read where the heap put another)");
    bool saved;
    if (!queen_save(game, t, 4, 0, &saved)) return false;
    return queen_cast_damage(game, t, amount, 2, saved) && cok_arena_missile_frames(game, 0x14);
}

/* 7fd8:1772 bolt(len, amount, flag): the bolt from the spell's cell away from the caster (vm.character), 2 * len half-cells
 * of walk, in segments that end at another combatant, a cell costing more than 1, the line's end, or off the map (the
 * segment then ends at its last cell in the map). Each segment flies a missile (slot 0x14, 4 frames, delay 0x32) and hits
 * its last cell's combatant once; where the bolt bounces (var 0x22 1 and a cell costing more than 1, not off the map) it
 * turns back along the line to the caster, doubled once (flag, the line of 8 or fewer) and charged again from the
 * wall. The first charge after a bounce wraps as a byte (charged is not reset). On the open ground (var 0x22 0) a wall
 * never bounces: the bolt walks on through it. A line of zero length (the caster's own cell aimed at, or a bounce off the
 * caster's cell) walks nothing: its segments keep the occupant last read (bp-0x3e, kept across segments and lines). */
bool queen_bolt(cok_adventure *game, uint8_t len, uint8_t amount, bool flag)
{
    cok_combat *combat = &game->combat;
    const cok_rules *rules = cok_game_rules(game);
    if (!cok_arena_missile_frames(game, 0x14)) return false;
    if (game->vm.character == NULL)
        return undefined(game, "the caster is read through NULL with no character selected (DS:ad67, 7fd8:1772)");
    int8_t sx = cok_combat_x(combat, game->vm.character), sy = cok_combat_y(combat, game->vm.character);
    int16_t x0 = game->spell_x, y0 = game->spell_y;
    if (!hit(game, (int8_t)x0, (int8_t)y0, amount)) return false;
    uint8_t last, terr;
    cok_combat_cell(combat, (int8_t)x0, (int8_t)y0, &last, &terr);
    uint16_t rem = (uint16_t)(len * 2);
    game->effects.rolls.images = 1;
    uint8_t charged = 0;
    int8_t sign = 1;
    /* The occupant at the segment's end (bp-0x3e): only the walk writes it, and it is kept across segments and lines. An
     * aim at the caster's own cell reads it unwritten at 192f: 0, measured in DOSBox-X (traps at 7fd8:192f: BALTHIR at
     * levels 2 and 9 and TIMASALIN, 0 each time). 849a:145b's poses of the caster just before the handler (5837:09ab at
     * 849a:17ee and 180e, drawn while the caster is on the view, as an own-cell aim's cursor keeps it) leave that byte: 8
     * before them, 0x74 between, 0 after. So the own-cell bolt hits the caster a second time, then walks nothing. */
    uint8_t occ = 0;
    while (rem > 0) {
        cok_combat_line line = {0};
        line.x0 = x0;
        line.y0 = y0;
        /* Words: the low words of 16-bit products (uint32 keeps every bit the low word needs). */
        uint32_t dx = (uint32_t)(uint16_t)(x0 - sx), dy = (uint32_t)(uint16_t)(y0 - sy);
        uint32_t way = (uint32_t)(uint16_t)sign;
        line.x1 = (int16_t)(uint16_t)((uint32_t)(uint16_t)x0 + dx * way * rem);
        line.y1 = (int16_t)(uint16_t)((uint32_t)(uint16_t)y0 + dy * way * rem);
        cok_combat_line_start(&line);
        for (;;) {
            bool out = false, zero = line.x0 == line.x1 && line.y0 == line.y1;
            int8_t bx = (int8_t)line.x, by = (int8_t)line.y;
            int16_t px = line.x, py = line.y;
            uint8_t cost = 0;
            if (!zero) {
                do {
                    px = line.x;
                    py = line.y;
                    bool stepped = cok_combat_line_step(&line, rules);
                    cok_combat_cell(combat, (int8_t)line.x, (int8_t)line.y, &occ, &terr);
                    if (!stepped || (occ != 0 && occ != last) || terr == 0) break;
                    if (!terrain_cost(game, terr, &cost)) return false;
                    if (cost > 1) break;
                } while (line.length < rem);
            }
            if (terr == 0) rem = 0;
            if (line.x < 0 || line.x > 0x31 || line.y < 0 || line.y > 0x18) {
                out = true;
                line.x = px;
                line.y = py;
            }
            if (!cok_arena_missile(game, bx, by, line.x, line.y, 4, 0x32)) return false;
            if (last != occ && !hit(game, (int8_t)line.x, (int8_t)line.y, amount)) return false;
            last = occ;
            if (!terrain_cost(game, terr, &cost)) return false;
            if (cost > 1 && cok_ecl_load_var(&game->vm, 0x22) == 1 && !out) {
                x0 = line.x;
                y0 = line.y;
                sign = -1;
                cok_combat_line back = {0};
                back.x0 = x0;
                back.y0 = y0;
                back.x1 = sx;
                back.y1 = sy;
                cok_combat_line_start(&back);
                while (cok_combat_line_step(&back, rules)) {}
                if (flag && back.length <= 8) line.length = (uint8_t)(line.length << 1);
                flag = false;
            }
            uint8_t c = (uint8_t)(line.length - charged);
            if (c < 2) c = 2;
            rem = c < rem ? (uint16_t)(rem - c) : 0;
            charged = line.length;
            if (rem == 0) break;
            if (cost <= 1 || cok_ecl_load_var(&game->vm, 0x22) != 1) continue;
            break;
        }
    }
    game->effects.rolls.images = 0;
    return true;
}

/* 7fd8:1ac3 Lightning Bolt 0x33: underwater the Fireball family's area (queen_fireball, the spell kept: its radius 2 and the
 * damage of type 9); else the damage type 0x0c, the caster level's d6 (the level first, then the dice) and a bolt of 7 with
 * the surcharge. */
static bool lightning_bolt(cok_adventure *game)
{
    if (underwater(game)) return queen_fireball(game);
    game->effects.rolls.damage_type = 0x0c;
    uint8_t lv = 0;
    if (!queen_spell_level(game, 0x33, false, &lv)) return false;
    return queen_bolt(game, 7, dice_count(game, lv, 6), true);
}

/* 7fd8:1d20 Javelin of Lightning 0x81 (an item: its Use is part 50, so the repo tests only): the damage type 0x0c and a
 * bolt of 3 doing 20, no surcharge, no dice. */
static bool javelin_of_lightning(cok_adventure *game)
{
    game->effects.rolls.damage_type = 0x0c;
    return queen_bolt(game, 3, 0x14, false);
}

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
    case 0x33: return lightning_bolt;
    case 0x81: return javelin_of_lightning;

    /* b4: Cone of Cold 0x5c (7fd8:299c) */
    case 0x5c: return cone_of_cold;

    /* b5: Meteor Swarm 0x7c (7fd8:3b99) */

    default: return NULL;
    }
}
