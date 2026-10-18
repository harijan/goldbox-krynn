#include "round.h"

#include "adventure.h"
#include "ai.h"
#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "cast.h"
#include "cloud.h"
#include "command.h"
#include "step.h"

#include <stdio.h>
#include <string.h>

static int8_t s8(int value)
{
    return (int8_t)(uint8_t)value;
}

static void name_of(const cok_character_layout *L, const uint8_t *r, char out[16])
{
    uint8_t stored = cok_field_u8_at(L, r, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(out, r + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    out[length] = '\0';
}

static void log_text(cok_adventure *game, const char *kind, const char *text)
{
    cok_adventure_log(game, kind, text);
}

/* Run c's effects for event, ending the run where that fails. */
static bool event(cok_adventure *game, cok_character *c, uint8_t ev)
{
    if (cok_effects_dispatch(&game->effects, c, ev)) return true;
    return cok_adventure_effect_failed(game);
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* A record's combat record, which every record has during a battle. */
static cok_combat_record *record_of(cok_adventure *game, cok_character *c)
{
    if (c->combat == NULL)
        undefined(game, "a record with no combat record is read through NULL in combat");
    return c->combat;
}

/* The shared helpers. */

void cok_combat_end_turn(cok_character *c)
{
    if (c->combat == NULL) return;
    c->combat->initiative = 0;
    c->combat->spell = 0;
    c->combat->guarding = 0;
    c->combat->movement = 0;
}

bool cok_combat_damage(cok_combat *combat, cok_character *c, const cok_rules *rules, uint8_t damage,
                       uint8_t mode)
{
    const cok_character_layout *L = rules->character;
    uint8_t *r = c->record;
    uint8_t over = 0, left = 0, hp = cok_field_u8(L, r, COK_FIELD_HIT_POINTS);
    if (hp < damage)
        over = (uint8_t)(damage - hp);
    else
        left = (uint8_t)(hp - damage);
    if (over > 9 || (left == 0 && cok_field_u8(L, r, COK_FIELD_STATUS) == 1)) {
        cok_field_set8(L, r, COK_FIELD_STATUS, 6);
    } else if (over > 0) {
        cok_field_set8(L, r, COK_FIELD_STATUS, 5);
        if (mode == 5) {
            if (c->combat == NULL) return false;
            c->combat->dying = over;
        }
    } else if (left == 0) {
        cok_field_set8(L, r, COK_FIELD_STATUS, 4);
    }
    if (cok_field_u8(L, r, COK_FIELD_STATUS) == 0 || cok_field_u8(L, r, COK_FIELD_STATUS) == 1) {
        cok_field_set8(L, r, COK_FIELD_HIT_POINTS, left);
        return true;
    }
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 0);
    if (mode != 5) return true;
    if (cok_field_u8(L, r, COK_FIELD_HOSTILE) > 1 || c->combat == NULL) return false;
    --combat->sides[cok_field_u8(L, r, COK_FIELD_HOSTILE)];
    c->combat->initiative = 0;
    return true;
}

bool cok_combat_bandage(cok_adventure *game, bool bandage)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    bool found = false;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (cok_character_combat(c)->not_party != 0 || cok_field_u8(L, r, COK_FIELD_HOSTILE) != 0 ||
            cok_field_u8(L, r, COK_FIELD_STATUS) != 5)
            continue;
        found = true;
        if (!bandage) continue;
        cok_field_set8(L, r, COK_FIELD_STATUS, 4);
        if (c->combat != NULL) c->combat->dying = 0;
        (void)cok_arena_say(game, c, "is bandaged", 10, true); /* 6346:326c; row 10 never fails */
        bandage = false;
    }
    return found;
}

bool cok_combat_helpless(const cok_character *c, const cok_rules *rules)
{
    const cok_data_segment *D = rules->data;
    bool found = false;
    for (size_t k = 0; k < cok_data_count(D, COK_DATA_HELPLESS, 0); ++k)
        if (cok_character_find_effect(c, cok_data_u8_at(D, COK_DATA_HELPLESS, k)) != NULL)
            found = true;
    return found;
}

uint8_t cok_combat_opposite(const cok_character *c, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    return cok_field_u8(L, c->record, COK_FIELD_HOSTILE) == 0 ? 1 : 0;
}

bool cok_combat_movement(cok_adventure *game, cok_character *c, uint8_t *movement)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    uint8_t m = cok_field_u8(L, r, COK_FIELD_MOVEMENT);
    if (cok_field_u8(L, r, COK_FIELD_HOSTILE) == 0)
        m = (uint8_t)(m + cok_ecl_var(&game->vm, COK_VAR_MOVE_BONUS)); /* var 0x7f72 */
    if (m < 1 || m > 0x60) m = 1;
    game->effects.rolls.rate = (uint8_t)(m * 2);
    if (!event(game, c, 0x12)) return false;
    *movement = game->effects.rolls.rate;
    return true;
}

uint8_t cok_combat_half_attacks(uint8_t round, uint8_t n)
{
    if ((round & 1) != 0) ++n;
    return (uint8_t)(n >> 1);
}

/* The readied weapon's item type, or false for one past ITEMS. */
static bool weapon_type(const cok_item_types *types, const cok_character *c, const uint8_t **type)
{
    *type = NULL;
    size_t weapon = c->slots[0];
    if (weapon == 0 || weapon > c->item_count) return true;
    uint8_t t = c->items[weapon - 1][0x2e];
    if (t >= COK_ITEM_TYPES) return false;
    *type = types->type[t];
    return true;
}

bool cok_combat_missile_weapon(const cok_item_types *types, const cok_character *c,
                               bool *missile)
{
    const uint8_t *type;
    *missile = false;
    if (!weapon_type(types, c, &type)) return false;
    *missile = type != NULL && type[12] > 1;
    return true;
}

bool cok_combat_ammunition(const cok_item_types *types, const cok_character *c, size_t *item,
                           bool *has)
{
    const uint8_t *type;
    *item = 0;
    *has = false;
    if (!weapon_type(types, c, &type)) return false;
    uint8_t flags = type != NULL ? type[14] : 0;
    if (type != NULL) {
        if ((flags & 0x10) != 0) *item = c->slots[0];
        if ((flags & 8) != 0) {
            if ((flags & 1) != 0) *item = c->slots[11];
            if ((flags & 0x80) != 0) *item = c->slots[12];
        }
    }
    if (*item > c->item_count) *item = 0;
    *has = *item != 0 || flags == 0x0a;
    return true;
}

static bool item_type_past(cok_adventure *game)
{
    return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
}

bool cok_combat_weapon_attacks(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    cok_rolls *rolls = &game->effects.rolls;
    uint8_t old = cok_field_u8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1);
    cok_field_set8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1,
                      cok_field_u8_at(L, r, COK_FIELD_BASE_ATTACKS, 1));
    bool missile, has = false;
    size_t item = 0;
    if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return item_type_past(game);
    if (missile && !cok_combat_ammunition(&game->item_types, c, &item, &has))
        return item_type_past(game);
    if (has) {
        const uint8_t *type;
        weapon_type(&game->item_types, c, &type);
        rolls->rate = type[5] < 2 ? 2 : type[5];
    } else {
        rolls->rate = cok_field_u8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1);
    }
    if (!event(game, c, 0x12)) return false;
    uint8_t n = cok_combat_half_attacks(rolls->round, rolls->rate);
    if (has && item != 0) {
        uint8_t count = c->items[item - 1][0x39], most = count > 1 ? count : 1;
        if (most < n && count > 0) n = most;
    }
    cok_field_set8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1, cr->attacked == 0 || n < old ? n : old);
    return true;
}

bool cok_combat_fastest_enemy(cok_adventure *game, cok_character *c, uint8_t *fastest)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    *fastest = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *other = game->party.members[i];
        if (cok_combat_opposite(c, cok_game_rules(game)) !=
                cok_field_u8(L, other->record, COK_FIELD_HOSTILE) ||
            cok_field_u8(L, other->record, COK_FIELD_ACTIVE) == 0)
            continue;
        uint8_t m;
        if (!cok_combat_movement(game, other, &m)) return false;
        m = (uint8_t)(m >> 1);
        if (m > *fastest) *fastest = m;
    }
    return true;
}

void cok_combat_auto(cok_character *c, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    cok_field_set8(L, c->record, COK_FIELD_COMPUTER, 1);
    if (c->combat != NULL && c->combat->target != NULL &&
        cok_field_u8(L, c->combat->target, COK_FIELD_HOSTILE) ==
            cok_field_u8(L, c->record, COK_FIELD_HOSTILE))
        c->combat->target = NULL;
}

bool cok_combat_battle_only(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    for (size_t k = 0; k < cok_data_count(D, COK_DATA_BATTLE_ONLY, 0); ++k) {
        if (cok_effects_remove(&game->effects, c, NULL, cok_data_u8_at(D, COK_DATA_BATTLE_ONLY, k)))
            continue;
        return cok_adventure_effect_failed(game);
    }
    if (cok_character_find_effect(c, cok_game_effect(game, COK_EFFECT_BERSERK)) != NULL &&
        cok_field_u8(L, c->record, COK_FIELD_NPC) == 0xb3)
        cok_field_set8(L, c->record, COK_FIELD_HOSTILE, 0);
    return true;
}

bool cok_combat_leave(cok_adventure *game, cok_character *c, uint8_t status, const char *text)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) return true;
    uint8_t n = cok_combat_index(combat, r);
    if (!cok_arena_turn(game, c, 3, false) || !cok_arena_say(game, c, text, 10, true))
        return false;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, status);
    if (status != 3) cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 0);
    if (!cok_arena_erase(game, 0, 0, n)) return false;
    if (!cok_arena_show(game)) return false; /* 60f4:13c0-13cf */
    if (n == 0)
        return undefined(game, "a record that is not a combatant leaves the map: its size is "
                               "written over the count (60f4:133c)");
    combat->combatant[n].size = 0;
    if (!cok_combat_occupy(combat, cok_game_rules(game)))
        return undefined(game, "a footprint is written outside the occupants (6beb:0375)");
    cok_combat_end_turn(c);
    return cok_combat_battle_only(game, c);
}

bool cok_combat_gods(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    cok_camp_notice(game, "The Gods intervene!"); /* 6346:1827 */
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (cok_field_u8(L, r, COK_FIELD_HOSTILE) == 1) {
            cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
            cok_field_set8(L, r, COK_FIELD_STATUS, 6);
            /* The occupants are not rebuilt: its cells keep its number. */
            uint8_t n = cok_combat_index(combat, r);
            if (n == 0)
                return undefined(game, "a record that is not a combatant is taken off the map: "
                                       "the count is cleared (432f:41e2)");
            combat->combatant[n].size = 0;
        }
        cok_combat_end_turn(c);
    }
    return cok_arena_centre(game, s8(combat->view_x + 3), s8(combat->view_y + 3), 0xff, 8);
}

bool cok_combat_explode(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    combat->exploding_now = true;
    game->effects.rolls.damage_type = 0;
    /* The count is read again each pass: those the damage makes explode
     * explode in turn. */
    for (unsigned i = 1; i <= combat->exploding_count; ++i) {
        cok_character *x = combat->exploding[i];
        if (x == NULL)
            return undefined(game, "the dead that explode hold NULL, written through at "
                                   "0000:0188 (60f4:24c7)");
        if (!cok_arena_say(game, x, "explodes.", 10, true)) return false;
        const uint8_t *r = x->record;
        if (!cok_combat_list(combat, cok_game_rules(game), cok_combat_x(combat, r),
                             cok_combat_y(combat, r), 1, 0xff,
                             cok_combat_size(combat, r)))
            return undefined(game, "the list of combatants around one (6b30:08d8) reads past "
                                   "its tables");
        /* The others listed, copied first into bytes of the stack below
         * the exploding record's pointer, which a ninth overwrites. */
        bool hurt = false;
        if (combat->listed_count > 1) {
            unsigned m = combat->listed_count - 1u;
            if (m > 8)
                return undefined(game, "more than eight around one that explodes overwrite its "
                                       "pointer on the stack (60f4:2452)");
            uint8_t near[9];
            for (unsigned j = 2; j <= combat->listed_count; ++j)
                near[j - 1] = combat->listed[j].index;
            for (unsigned k = 1; k <= m; ++k) {
                cok_character *e = near[k] <= COK_COMBATANTS ?
                                       combat->combatant[near[k]].character : NULL;
                if (e == NULL)
                    return undefined(game, "a listed combatant has no record (60f4:2375)");
                if (cok_field_u8(L, e->record, COK_FIELD_STATUS) == 10) continue;
                uint8_t d = cok_dice_count(&game->vm.seed, 1, 6, &game->effects.rolls.dice);
                if (!cok_cast_damage(game, e, d, 0, false)) return false;
                hurt = true;
            }
        }
        cok_field_set8(L, x->record, COK_FIELD_ACTIVE, 0);
        cok_field_set8(L, x->record, COK_FIELD_STATUS, 6);
        /* 6beb:0e08's stale byte: 0x24, the high byte of the return
         * address of 60f4:1db7 (24bf), after damage in the pass, else 0
         * (see arena.h). */
        cok_arena_reachable also = {{combat->exploding[1], combat->exploding[8],
                                     combat->exploding[15]},
                                    {NULL}};
        if (!cok_arena_kill(game, x, hurt ? 0x24 : 0, &also)) return false;
    }
    if (combat->exploding_count > 0) {
        combat->exploding_count = 0;
        memset(&combat->exploding[1], 0, 20 * sizeof combat->exploding[1]); /* DS:6b45, 0x50 */
    }
    combat->exploding_now = false;
    return true;
}

/* The lines, sight and ranges of the map (6b30). */

/* 6b30:0005. */
static int16_t sign(int value)
{
    return (int16_t)(value < 0 ? -1 : value > 0 ? 1 : 0);
}

void cok_combat_line_start(cok_combat_line *line)
{
    line->x = line->x0;
    line->y = line->y0;
    line->dx = (int16_t)(line->x1 - line->x0 < 0 ? line->x0 - line->x1 : line->x1 - line->x0);
    line->dy = (int16_t)(line->y1 - line->y0 < 0 ? line->y0 - line->y1 : line->y1 - line->y0);
    line->step_x = sign(line->x1 - line->x0);
    line->step_y = sign(line->y1 - line->y0);
    line->err = 0;
    line->length = 0;
}

bool cok_combat_line_step(cok_combat_line *line, const cok_rules *rules)
{
    bool stepped = false;
    int moved_x = 1, moved_y = 1;
    if (line->dx >= line->dy) {
        if (line->x != line->x1) {
            line->x = (int16_t)(line->x + line->step_x);
            moved_x = line->step_x + 1;
            line->err = (int16_t)(line->err + 2 * line->dy);
            line->length = (uint8_t)(line->length + 2);
            if (line->err >= line->dx) {
                line->err = (int16_t)(line->err - 2 * line->dx);
                line->y = (int16_t)(line->y + line->step_y);
                moved_y = line->step_y + 1;
                ++line->length;
            }
            stepped = true;
        }
    } else if (line->y != line->y1) {
        line->y = (int16_t)(line->y + line->step_y);
        moved_y = line->step_y + 1;
        line->err = (int16_t)(line->err + 2 * line->dx);
        line->length = (uint8_t)(line->length + 2);
        if (line->err >= line->dy) {
            line->err = (int16_t)(line->err - 2 * line->dy);
            line->x = (int16_t)(line->x + line->step_x);
            moved_x = line->step_x + 1;
            ++line->length;
        }
        stepped = true;
    }
    line->direction = cok_data_u8_at2(rules->data, COK_DATA_LINE_DIRECTIONS, moved_y, moved_x);
    return stepped;
}

/* The terrain of cell x, y, which must be on the map and in the table. */
static bool terrain_at(const cok_combat *combat, const cok_rules *rules, int x, int y,
                       cok_terrain *terrain)
{
    if (x < 0 || x >= COK_COMBAT_WIDTH || y < 0 || y >= COK_COMBAT_HEIGHT) return false;
    uint8_t t = combat->cells[y][x];
    if (t >= cok_data_count(rules->data, COK_DATA_TERRAIN, 0)) return false;
    *terrain = cok_combat_terrain(rules, t);
    return true;
}

bool cok_combat_sight(const cok_combat *combat, const cok_rules *rules, int8_t x0, int8_t y0,
                      int8_t *x1, int8_t *y1, uint16_t *range, bool *seen)
{
    uint16_t limit = (uint16_t)(*range * 2u + 1u);
    cok_combat_line line = {.x0 = x0, .y0 = y0, .x1 = *x1, .y1 = *y1};
    cok_combat_line_start(&line);
    /* The original runs a second line, at the eye's height, along the
     * first; it never leaves that height. */
    cok_terrain start;
    if (!terrain_at(combat, rules, x0, y0, &start)) return false;
    *seen = false;
    for (;;) {
        cok_terrain t;
        if (!terrain_at(combat, rules, line.x, line.y, &t)) return false;
        if ((combat->see_all == 0 && t.block > start.eye) || line.length > limit) {
            *x1 = s8(line.x);
            *y1 = s8(line.y);
            *range = line.length;
            return true;
        }
        if (!cok_combat_line_step(&line, rules)) break;
    }
    *range = line.length;
    *seen = true;
    return true;
}

bool cok_combat_in_arc(int8_t x0, int8_t y0, int8_t x1, int8_t y1, uint8_t dir, bool *in)
{
    *in = false;
    if (x0 < 0 || x0 > 0x31 || y0 < 0 || y0 > 0x18 || x1 < 0 || x1 > 0x31 || y1 < 0 || y1 > 0x18)
        return true;
    if (dir == 0xff) dir = 8;
    if (dir > 8) return false;
    int fx = s8(x0 + cok_step_x[dir]), fy = s8(y0 + cok_step_y[dir]);
    int x = x1, y = y1;
    if ((x1 == x0 && y1 == y0) || (x == fx && y == fy)) {
        *in = true;
        return true;
    }
    switch (dir) {
    case 0:
        if (x >= fx && y <= fx - x + fy) *in = true;
        else *in = x <= fx && y <= x - fx + fy;
        break;
    case 1:
        if (x >= fx && y <= fx - x + fy) *in = true;
        else *in = x >= fx + fy - y && y <= fy;
        break;
    case 2:
        if (x >= fx + fy - y && y <= fy) *in = true;
        else *in = x >= fx + y - fy && y >= fy;
        break;
    case 3:
        if (x >= fx + y - fy && y >= fy) *in = true;
        else *in = x >= fx && y >= x - fx + fy;
        break;
    case 4:
        if (x >= fx && y >= x - fx + fy) *in = true;
        else *in = x <= fx && y >= fx - x + fy;
        break;
    case 5:
        if (x <= fx && y >= fx - x + fy) *in = true;
        else *in = x <= fx + fy - y && y >= fy;
        break;
    case 6:
        if (x <= fx + fy - y && y >= fy) *in = true;
        else *in = x <= fx + y - fy && y <= fy;
        break;
    case 7:
        if (x <= fx + y - fy && y <= fy) *in = true;
        else *in = x <= fx && y <= x - fx + fy;
        break;
    default: *in = true; break;
    }
    return true;
}

/* 6b30:0033: an exchange sort of the list, nearest first, then by
 * direction where the later's is lower and not odder. */
static void sort_listed(cok_combat *combat)
{
    uint8_t n = combat->listed_count;
    if (n <= 1) return;
    for (unsigned i = 1; i + 1 <= n; ++i) {
        for (unsigned j = i + 1; j <= combat->listed_count; ++j) {
            uint8_t di = combat->listed[i].direction, dj = combat->listed[j].direction;
            uint8_t ai = combat->listed[i].distance, aj = combat->listed[j].distance;
            if (aj < ai || (aj == ai && dj < di && dj % 2 <= di % 2)) {
                cok_combat_listing t = combat->listed[i];
                combat->listed[i] = combat->listed[j];
                combat->listed[j] = t;
            }
        }
    }
}

bool cok_combat_list(cok_combat *combat, const cok_rules *rules, int8_t x, int8_t y, uint16_t range,
                     uint8_t dir, uint8_t size)
{
    unsigned cells = (unsigned)cok_data_count(rules->data, COK_DATA_FOOTPRINTS, 1);
    assert(cells <= COK_COMBAT_FOOTPRINT_CELLS);
    int8_t vx[COK_COMBAT_FOOTPRINT_CELLS], vy[COK_COMBAT_FOOTPRINT_CELLS];
    if (size >= cok_data_count(rules->data, COK_DATA_FOOTPRINTS, 0)) return false;
    for (unsigned k = 0; k < cells; ++k) {
        int8_t dx, dy;
        if (cok_combat_footprint(rules, size, k, &dx, &dy)) {
            vx[k] = s8(dx + x);
            vy[k] = s8(dy + y);
        } else {
            vx[k] = -1;
            vy[k] = 0;
        }
    }
    combat->listed_count = 0;
    for (unsigned n = 1; n <= combat->count && n <= COK_COMBATANTS; ++n) {
        const cok_combatant *e = &combat->combatant[n];
        if (e->size == 0) continue;
        if (e->size >= cok_data_count(rules->data, COK_DATA_FOOTPRINTS, 0)) return false;
        int8_t tx[COK_COMBAT_FOOTPRINT_CELLS], ty[COK_COMBAT_FOOTPRINT_CELLS];
        for (unsigned k = 0; k < cells; ++k) {
            int8_t dx, dy;
            if (cok_combat_footprint(rules, e->size, k, &dx, &dy)) {
                tx[k] = s8(dx + e->x);
                ty[k] = s8(dy + e->y);
            } else {
                tx[k] = -1;
                ty[k] = 0;
            }
        }
        bool found = false;
        uint16_t best = 0xff;
        unsigned bj = cells, bk = cells;
        for (unsigned k = 0; k < cells; ++k) {
            if (tx[k] < 0) continue;
            for (unsigned j = 0; j < cells; ++j) {
                if (vx[j] < 0) continue;
                bool in, seen;
                if (!cok_combat_in_arc(vx[j], vy[j], tx[k], ty[k], dir, &in)) return false;
                if (!in) continue;
                int8_t sx = tx[k], sy = ty[k];
                uint16_t r = range;
                combat->sight_step = sign(tx[k] - vx[j]);
                if (!cok_combat_sight(combat, rules, vx[j], vy[j], &sx, &sy, &r, &seen))
                    return false;
                if (!seen) continue;
                found = true;
                if (r < best) {
                    best = r;
                    bj = j;
                    bk = k;
                }
            }
        }
        if (!found) continue;
        /* With no length below 0xff the original takes the cells its
         * locals hold from before. */
        if (bj == cells || combat->listed_count >= COK_COMBATANTS) return false;
        uint8_t d = 0;
        if (dir < 8) {
            d = dir;
        } else {
            /* The first direction whose arc holds the pair; 8 always does. */
            bool in = false;
            while (cok_combat_in_arc(vx[bj], vy[bj], tx[bk], ty[bk], d, &in) && !in && d < 8)
                ++d;
            if (!in) return false;
        }
        ++combat->listed_count;
        combat->listed[combat->listed_count].index = (uint8_t)n;
        combat->listed[combat->listed_count].distance = (uint8_t)best;
        combat->listed[combat->listed_count].direction = d;
    }
    sort_listed(combat);
    return true;
}

/* The combatant record of a listed entry. */
static const uint8_t *listed_record(const cok_combat *combat, unsigned i)
{
    uint8_t n = combat->listed[i].index;
    if (n > COK_COMBATANTS || combat->combatant[n].character == NULL) return NULL;
    return combat->combatant[n].character->record;
}

bool cok_combat_enemies(cok_adventure *game, cok_character *c, uint8_t range, uint8_t *count)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    const uint8_t *r = c->record;
    *count = 0;
    if (!cok_combat_list(combat, cok_game_rules(game), cok_combat_x(combat, r),
                         cok_combat_y(combat, r), range, 0xff,
                         cok_combat_size(combat, r)))
        return undefined(game, "the list of combatants around one (6b30:08d8) reads past its "
                               "tables");
    uint8_t n = combat->listed_count;
    if (n > 0) {
        uint8_t kept = 0;
        for (unsigned i = 1; i <= n; ++i) {
            const uint8_t *other = listed_record(combat, i);
            if (other == NULL)
                return undefined(game, "a listed combatant has no record (6346:26e2)");
            if (cok_field_u8(L, other, COK_FIELD_HOSTILE) !=
                cok_combat_opposite(c, cok_game_rules(game)))
                continue;
            ++kept;
            combat->listed[kept] = combat->listed[i];
        }
        n = kept;
        combat->listed_count = kept;
    }
    for (unsigned i = 1; i <= n; ++i) combat->enemies[i] = combat->listed[i].index;
    if (cok_character_find_effect(c, cok_game_effect(game, COK_EFFECT_YELLED_AT)) != NULL) {
        bool swapped = false;
        for (unsigned i = 1; i <= n && !swapped; ++i) {
            uint8_t e = combat->enemies[i];
            const cok_character *other = combat->combatant[e].character;
            if ((other != NULL ? other->record : NULL) != combat->yelled) continue;
            combat->enemies[i] = combat->enemies[1];
            combat->enemies[1] = e;
            swapped = true;
        }
    }
    *count = n;
    return true;
}

bool cok_combat_distance(cok_combat *combat, const cok_rules *rules, const uint8_t *origin,
                         const uint8_t *target, uint8_t *distance)
{
    cok_combat_listing saved[COK_COMBATANTS + 1];
    memcpy(saved, combat->listed, sizeof saved);
    const uint8_t *r = origin;
    combat->see_all = 1;
    bool ok = cok_combat_list(combat, rules, cok_combat_x(combat, r), cok_combat_y(combat, r), 0xff,
                              0xff, cok_combat_size(combat, r));
    combat->see_all = 0;
    if (ok) {
        unsigned i = 1;
        while (listed_record(combat, i) != target && i < combat->listed_count) ++i;
        *distance = (uint8_t)(combat->listed[i].distance >> 1);
    }
    /* The entries after the count are put back, the count not. */
    memcpy(&combat->listed[1], &saved[1], sizeof saved - sizeof saved[0]);
    return ok;
}

bool cok_combat_direction(const cok_combat *combat, const cok_character *a,
                          const cok_character *b, uint8_t *direction)
{
    int ax = cok_combat_x(combat, a->record), ay = cok_combat_y(combat, a->record);
    int bx = cok_combat_x(combat, b->record), by = cok_combat_y(combat, b->record);
    int dx = bx - ax < 0 ? ax - bx : bx - ax, dy = by - ay < 0 ? ay - by : by - ay;
    /* 0x26a / 256 and 0x6a / 256 are tan 67.5 and 22.5 degrees; the
     * product is a word, taken as signed. */
    int steep = (int16_t)(uint16_t)(0x26a * dx) / 256, flat = (int16_t)(uint16_t)(0x6a * dx) / 256;
    bool diagonal = steep >= dy && flat <= dy;
    for (unsigned d = 0; d < 256; ++d) {
        bool found = false;
        switch (d & 0xff) {
        case 0: found = by <= ay && steep <= dy; break;
        case 2: found = bx >= ax && flat >= dy; break;
        case 4: found = by >= ay && steep <= dy; break;
        case 6: found = bx <= ax && flat >= dy; break;
        case 1: found = by < ay && bx > ax && diagonal; break;
        case 3: found = by > ay && bx > ax && diagonal; break;
        case 5: found = by > ay && bx < ax && diagonal; break;
        case 7: found = by < ay && bx < ax && diagonal; break;
        default: break;
        }
        if (found) {
            *direction = (uint8_t)d;
            return true;
        }
    }
    return false;
}

/* The round. */

bool cok_combat_round_start(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    cok_rolls *rolls = &game->effects.rolls;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    cr->spell = 0;
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, cok_game_rules(game), r, 8, &occupant, &terrain, &cloud, &puddle))
        return undefined(game, "a size past the footprints is read after them (6beb:0c9d)");
    cr->may_cast = !cloud;
    cr->may_use = 1;
    cr->attacked = 0;
    cr->attack_slot = 2;
    if (!cok_combat_weapon_attacks(game, c)) return false;
    rolls->rate = cok_field_u8_at(L, r, COK_FIELD_BASE_ATTACKS, 2);
    if (!event(game, c, 0x12)) return false;
    cok_field_set8_at(L, r, COK_FIELD_ATTACKS_LEFT, 2,
                      cok_combat_half_attacks(rolls->round, rolls->rate));
    cr->sweeps = cok_field_u8(L, r, COK_FIELD_FIGHTER_LEVEL);
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) != 0) {
        int8_t bonus = cok_character_dexterity_missile(r, cok_game_rules(game));
        uint8_t initiative = (uint8_t)(cok_dice(&game->vm.seed, 1, 6) + bonus);
        if ((int8_t)initiative < 1) initiative = 1;
        /* (side + 1) & var 0x7ecb: bit 0 the party's side surprised, bit 1
         * the enemy's. */
        if (((uint8_t)(cok_field_u8(L, r, COK_FIELD_HOSTILE) + 1) &
             (uint8_t)cok_ecl_var(&game->vm, COK_VAR_SURPRISE)) != 0)
            initiative = (uint8_t)(initiative - 6);
        if ((int8_t)initiative < 0 || (int8_t)initiative > 20) initiative = 0;
        cr->initiative = (int8_t)initiative;
    } else {
        cr->initiative = 0;
    }
    return cok_combat_movement(game, c, &cr->movement);
}

void cok_combat_turn_order(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    memset(combat->turn, 0, sizeof combat->turn);
    for (size_t m = 0; m < game->party.count; ++m) {
        cok_character *c = game->party.members[m];
        int8_t key = cok_character_combat(c)->initiative;
        unsigned i = 1;
        for (bool go = true; i < 0x41 && go;) {
            const cok_character *at = combat->turn[i];
            int8_t k = at != NULL ? cok_character_combat(at)->initiative : -1;
            if (key > k || (key == k && cok_dice(&game->vm.seed, 1, 2) == 1))
                go = false;
            else
                ++i;
        }
        cok_character *moved = combat->turn[i];
        combat->turn[i] = c;
        for (unsigned j = i; moved != NULL && j < COK_COMBATANTS;) {
            cok_character *next = combat->turn[++j];
            combat->turn[j] = moved;
            moved = next;
        }
    }
}

/* The computer's turn is 3afb:004b (cok_combat_computer), the player's
 * 3995:0573 (cok_combat_commands), which with --combat gods presses Alt-X
 * (scan code 0x2d) first, running the Helm cheat (432f:41e2) in a game
 * started with Helm, and with --combat auto Alt-Q, putting the party on
 * Auto. With COK_COMBAT_UNPORTED (eclplay with neither --combat nor
 * --keys) the player's turns pass, the computer's are played; with --combat melee every turn attacks the
 * nearest enemy it can reach (cok_combat_melee), and with --combat pass
 * every turn passes, the computer's too. */
static bool act(cok_adventure *game, cok_character *c, bool computer)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    char name[16], text[48];
    name_of(L, c->record, name);
    snprintf(text, sizeof text, "%s (initiative %d)", name, c->combat->initiative);
    log_text(game, "turn", text);
    cok_combat_stub stub = game->combat_stub;
    if (stub == COK_COMBAT_MELEE) return cok_combat_melee(game, c);
    if (stub == COK_COMBAT_PASS) {
        cok_combat_end_turn(c);
        return true;
    }
    if (computer) return cok_combat_computer(game, c);
    if (stub == COK_COMBAT_PLAYER) return cok_combat_commands(game, c, 0);
    if (stub == COK_COMBAT_GODS) return cok_combat_commands(game, c, 0x2d);
    if (stub == COK_COMBAT_AUTO) return cok_combat_commands(game, c, 0x10);
    /* The turn passes, after 3995:0573's first step: the menu's first item
     * selected (DS:6e0f), for one that can act and is not casting. */
    if (cok_field_u8(L, c->record, COK_FIELD_ACTIVE) != 0 && c->combat->spell == 0)
        game->selected = 1;
    cok_combat_end_turn(c);
    return true;
}

bool cok_combat_turn(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    cr->hits = 0;
    cr->turning = 0;
    cr->guarding = 0;
    if (!event(game, c, 7)) return false;
    if (cr->initiative <= 0) return true;
    if (cr->initiative == 0x14) cr->initiative = 0x13;
    game->vm.character = r;
    bool visible = cok_field_u8(L, r, COK_FIELD_HOSTILE) == 0;
    if (!visible && !cok_combat_visible(combat, cok_game_rules(game), r, false, &visible))
        return undefined(game, "a size past the footprints is read after them (6beb:06ef)");
    combat->show_actions = visible;
    if (!cok_arena_turn(game, c, 2, true)) return false;
    char error[300];
    if (!cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    combat->panel = true;
    if (!cok_arena_panel(game, c)) return false;
    if (!event(game, c, 0x0f)) return false;
    if (cr->spell == 0 && !event(game, c, 0x15)) return false;
    if (cr->initiative > 0 && !act(game, c, cok_field_u8(L, r, COK_FIELD_COMPUTER) != 0))
        return false;
    return cok_arena_redraw_cell(game, cok_combat_x(combat, r), cok_combat_y(combat, r));
}

bool cok_combat_end_round(cok_adventure *game, bool *done)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    if (!cok_adventure_pass_time(game, 1, 1)) return false; /* 57e4:0549 */
    ++game->effects.rolls.round;
    cok_combat_enemy_health(combat, &game->party, cok_game_rules(game));
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        if (!event(game, c, 0x13) || !cok_cloud_square(game, c, false)) return false;
        if (cok_field_u8(L, c->record, COK_FIELD_STATUS) != 5) continue;
        cok_combat_record *cr = record_of(game, c);
        if (cr == NULL) return false;
        if (++cr->dying > 9) cok_field_set8(L, c->record, COK_FIELD_STATUS, 6);
    }
    if (cok_combat_bandage(game, false)) cok_camp_notice(game, "Your Teammate is Dying");
    if (!cok_combat_count_sides(combat, &game->party, cok_game_rules(game)))
        return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                               "DS:6b2d (6346:268a)");
    if (!cok_arena_centre(game, s8(combat->view_x + 3), s8(combat->view_y + 3), 0xff, 8))
        return false;
    if (combat->sides[0] == 0 || combat->sides[1] == 0 ||
        game->effects.rolls.round >= combat->round_limit)
        *done = true;
    /* The demonstration (DS:4b4b) does not ask (3995:0c78). */
    if (!game->demo && combat->sides[0] > 0 && combat->sides[1] == 0) {
        int answer = cok_camp_yes_no(game, "Continue Battle:", 13); /* 67b5:177f */
        if (answer == 'Y') *done = false;
    }
    return true;
}

/* The round's order, logged. */
static void log_order(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    char text[2048]; /* 72 records of up to 15 letters each */
    size_t used = (size_t)snprintf(text, sizeof text, "%u:", game->effects.rolls.round + 1u);
    const char *separator = " ";
    for (unsigned i = 1; i <= COK_COMBATANTS && game->combat.turn[i] != NULL; ++i) {
        const cok_character *c = game->combat.turn[i];
        char name[16];
        name_of(L, c->record, name);
        int n = snprintf(text + used, sizeof text - used, "%s%s %d", separator, name,
                         cok_character_combat(c)->initiative);
        if (n < 0 || used + (size_t)n >= sizeof text) break;
        used += (size_t)n;
        separator = ", ";
    }
    log_text(game, "round", text);
}

/* eclplay's --combat won, fled and lost, in place of the rounds: every
 * record against the party (+0x18a 1) drops, every party record that can
 * act flees, or the whole party dies. */
static void stub(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    static const char *const outcome[] = {"", "won", "fled", "lost"};
    cok_combat_stub how = game->combat_stub;
    log_text(game, "combat", outcome[how]);
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *r = game->party.members[i]->record;
        bool member =
            cok_character_combat(game->party.members[i])->not_party != 1 &&
            cok_field_u8(L, r, COK_FIELD_HOSTILE) != 1;
        if (how == COK_COMBAT_WON && cok_field_u8(L, r, COK_FIELD_HOSTILE) == 1) {
            cok_field_set8(L, r, COK_FIELD_STATUS, 6);
            cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
        } else if (how == COK_COMBAT_LOST && member) {
            cok_field_set8(L, r, COK_FIELD_STATUS, 6);
            cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
            cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 0);
        } else if (how == COK_COMBAT_FLED && member && cok_field_u8(L, r, COK_FIELD_ACTIVE) != 0) {
            cok_field_set8(L, r, COK_FIELD_STATUS, 3);
            cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
        }
    }
}

static bool rounds(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    cok_ecl *vm = &game->vm;
    if (!cok_combat_setup(game)) return false;
    if (game->hooks.battlefield != NULL) game->hooks.battlefield(game, game->hooks.context);
    bool done = combat->sides[0] == 0 || combat->sides[1] == 0;
    for (size_t i = 0; i < game->party.count; ++i)
        if (!event(game, game->party.members[i], 0x18)) return false;
    if (game->combat_stub == COK_COMBAT_WON || game->combat_stub == COK_COMBAT_FLED ||
        game->combat_stub == COK_COMBAT_LOST) {
        stub(game);
        /* As the end of the round would count them. */
        if (!cok_combat_count_sides(combat, &game->party, cok_game_rules(game)))
            return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                                   "DS:6b2d (6346:268a)");
        done = true;
    }
    while (!done && !vm->abort) {
        if (!cok_combat_count_sides(combat, &game->party, cok_game_rules(game)))
            return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                                   "DS:6b2d (6346:268a)");
        for (size_t i = 0; i < game->party.count; ++i)
            if (!cok_combat_round_start(game, game->party.members[i])) return false;
        cok_ecl_set_var(vm, COK_VAR_SURPRISE, 0); /* var 0x7ecb */
        cok_combat_turn_order(game);
        log_order(game);
        cok_character *c = combat->turn[1];
        combat->turn_index = 1;
        while (c != NULL && !vm->abort) {
            if (!cok_combat_turn(game, c) || !cok_combat_explode(game)) return false;
            ++combat->turn_index;
            c = combat->turn_index > COK_COMBATANTS ? NULL : combat->turn[combat->turn_index];
        }
        if (vm->abort || !cok_combat_end_round(game, &done)) break;
    }
    if (vm->status != COK_ECL_OK) return false;
    if (game->combat_stub == COK_COMBAT_MELEE || game->combat_stub == COK_COMBAT_AUTO ||
        game->combat_stub == COK_COMBAT_PLAYER) {
        char text[32];
        snprintf(text, sizeof text, "seed %lu", (unsigned long)vm->seed);
        log_text(game, "combat", text);
    }
    if (!cok_combat_end(game)) return false;
    game->text_delay = true; /* 3995:0297 */
    return true;
}

bool cok_combat_battle(cok_adventure *game)
{
    /* DS:6e3a; the mode is 5 from setup on. */
    game->combat_targets = true;
    game->effects.in_battle = true;
    bool ok = rounds(game);
    game->effects.in_battle = false;
    game->combat_targets = false;
    return ok;
}
