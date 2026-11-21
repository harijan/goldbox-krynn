#include "queen.h"

#include "arena.h"
#include "combat.h"
#include "data.h"
#include "ecl.h"
#include "field.h"
#include "party.h"
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

/* b5: Meteor Swarm 0x7c (7fd8:3b99) and its try (7fd8:3a73). */

/* The selected member's record as a party member (queen_cast.c's caster_member), NULL with none. */
static cok_character *caster_member(cok_adventure *game)
{
    if (game->vm.character == NULL) return NULL;
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* 7fd8:3a73, the try k (1 to 4) of meteor m (1 to 4) from the caster's cell cx, cy at the aimed cell tx, ty: mx, my
 * is the cell tried. DS:8788 and DS:8790 hold the steps of m - 1, {-1, 1, -1, 1} and {-1, -1, 1, 1}: k 1 is the aimed
 * cell's diagonal neighbour, k 2 the aimed x with my kept (the last try's, pulled back or not), k 3 the neighbour's x
 * with the aimed y, k 4 the aimed x with my kept. A cell off the map is stopped (no sight; range 0xff stays). Else the
 * sight from the caster may pull mx, my back to a blocking cell, and seen is its result. The range is halved after
 * either (a stopped try leaves 0x7f). DS:c6f3 (set only while the pending list 148f:2d9b runs; 148f:2da1 and 2ef2) is 0
 * here: the port never runs that list (a non-empty one stops first), so the aimed cell's own test is left out. False
 * where the sight's terrain is past the table. */
bool queen_meteor_try(cok_adventure *game, int8_t cx, int8_t cy, int8_t tx, int8_t ty, unsigned k, unsigned m,
                      int8_t *mx, int8_t *my, uint16_t *range, bool *seen)
{
    static const int8_t dx[4] = {-1, 1, -1, 1}, dy[4] = {-1, -1, 1, 1};
    assert(k >= 1 && k <= 4 && m >= 1 && m <= 4);
    *range = 0xff;
    switch (k) {
    case 1:
        *mx = (int8_t)(tx + dx[m - 1]);
        *my = (int8_t)(ty + dy[m - 1]);
        break;
    case 3:
        *mx = (int8_t)(tx + dx[m - 1]);
        *my = ty;
        break;
    default: /* k 2 and 4: the aimed x, my kept */
        *mx = tx;
        break;
    }
    bool stop = *mx < 0 || *mx > 0x31 || *my < 0 || *my > 0x18;
    *seen = false;
    if (!stop && !cok_combat_sight(&game->combat, cok_game_rules(game), cx, cy, mx, my, range, seen))
        return undefined(game, "terrain past the table is read (DS:1ee4, 6b30:03f1)");
    *range >>= 1;
    return true;
}

/* 7fd8:3b99, Meteor Swarm (0x7c; the cast skips the flight): underwater the bar's refusal; else one image and the
 * damage's type 9. Four meteors, each from the caster's cell to the cell its try found (tries 1 to 4 until one is
 * seen): the caster's pose toward it, the Fireball family's picture and the missile, then the burst of radius 1 at
 * the meteor. The combatants on its line (queen_cone, width 1, the caster out, length the halved range) take dN(10, 4)
 * with no save; then the radius-1 list (the caster in) takes a save of type 4 first, and dN(10, 4) half on a save.
 * Each target is read again at its place (NULL or inactive: skipped), so one on the line and in the area takes two
 * hits. The caster's visible pose is set again after each meteor. */
static bool meteor_swarm(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    if (underwater(game)) {
        queen_bar_message(game, underwater_text);
        return true;
    }
    r->images = 1;
    r->damage_type = 9;
    cok_character *c = caster_member(game);
    if (c == NULL)
        return undefined(game, "the caster is read through NULL with no character selected (DS:ad67)");
    cok_combat *combat = &game->combat;
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    int8_t cx = cok_combat_x(combat, c->record), cy = cok_combat_y(combat, c->record);
    int8_t tx = game->spell_x, ty = game->spell_y;
    int8_t mx = 0, my = 0;
    uint16_t range = 0xff;
    for (unsigned m = 1; m <= 4; ++m) {
        game->spell_targets[0] = NULL; /* 2093:0015 (DS:c5df) */
        bool hit = false;
        unsigned tries = 1;
        do {
            if (!queen_meteor_try(game, cx, cy, tx, ty, tries, m, &mx, &my, &range, &hit)) return false;
        } while (++tries <= 4 && !hit);
        uint8_t dir = 0;
        bool in = false;
        while (!in) {
            if (!cok_combat_in_arc(cx, cy, mx, my, dir, &in))
                return undefined(game, "the arc of the caster reads past the terrain (5d71:048f)");
            if (!in && ++dir == 8)
                return undefined(game, "no facing puts the meteor's cell in the caster's quarter (5d71:048f)");
        }
        combat->show_actions = true; /* DS:c3f5 */
        if (!cok_arena_pose(game, c, dir, 1, false)) return false; /* 5837:09ab(rec, dir, 1, 0) */
        if (!cok_arena_missile_frames(game, 0x20) || !queen_direction_picture(game, dir, 0x20)) return false;
        if (!cok_arena_missile(game, cx, cy, mx, my, 1, 0x1e)) return false;
        game->spell_x = mx;
        game->spell_y = my;
        if (!queen_burst(game, 1, 0x19, 0x0c)) return false;
        if (!queen_cone(game, cx, cy, mx, my, 1, (uint8_t)range)) return false;
        uint8_t n = game->spell_target_count;
        /* 5d71:07d7 (the area's list, range 1, any facing, size 1), appended to the line's targets: repeats kept. */
        if (!cok_combat_list(combat, rules, mx, my, 1, 0xff, 1))
            return undefined(game, "the list of combatants around one (6b30:08d8) reads past its tables");
        for (uint8_t j = 1; j <= combat->listed_count; ++j) {
            if (game->spell_target_count >= COK_PARTY_RECORDS)
                return undefined(game, "a meteor's targets run past the party's table (DS:c5df)");
            game->spell_targets[++game->spell_target_count] =
                combat->combatant[combat->listed[j].index].character;
        }
        for (uint8_t k = 1; k <= game->spell_target_count; ++k) {
            cok_character *t = game->spell_targets[k];
            if (t == NULL || cok_field_u8(L, t->record, COK_FIELD_ACTIVE) == 0) continue;
            if (k <= n) {
                /* On the line: no save, the damage whole (kind 0). */
                if (!queen_cast_damage(game, t, dice_count(game, 10, 4), 0, false)) return false;
            } else {
                /* In the area: the save first (148f:1886), then the dice, half on a save (kind 2). */
                bool saved = false;
                if (!queen_save(game, t, 4, 0, &saved)) return false;
                if (!queen_cast_damage(game, t, dice_count(game, 10, 4), 2, saved)) return false;
            }
        }
        bool shown = false;
        if (!cok_combat_visible(combat, rules, c->record, false, &shown))
            return undefined(game, "a size past the footprints is read after them (5837:0626)");
        if (shown) {
            uint8_t facing = c->combat->facing;
            if (!cok_arena_pose(game, c, facing, 1, true) || !cok_arena_pose(game, c, facing, 0, false))
                return false;
        }
    }
    return true;
}

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
    case 0x7c: return meteor_swarm;

    default: return NULL;
    }
}
