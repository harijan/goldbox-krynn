#include "cloud.h"

#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "cast.h"
#include "round.h"
#include "step.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The direction of cell k (1-4) of a cloud (DS:2091 + k). */
static uint8_t cell_dir(const cok_adventure *game, unsigned k)
{
    return cok_data_u8_at(cok_game_rules(game)->data, COK_DATA_CLOUD_CELLS, k);
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

static bool effect_failed(cok_adventure *game)
{
    return cok_adventure_effect_failed(game);
}

static cok_character *member_of(cok_adventure *game, const uint8_t *record)
{
    size_t i = cok_party_index(&game->party, record);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* Whether the game's clouds are Death Knights' (cok_clouds_routine). */
static bool dkk(const cok_adventure *game)
{
    return cok_game_rules(game)->fight.clouds == COK_CLOUDS_DEATH_KNIGHTS;
}

/* Death Knights' cells of a cloud, k 1-9 (DS:5843 + k): Stinking Cloud's
 * first four are covered, Cloud Kill's all nine; the rest are never
 * covered. */
#define DKK_CELLS 9

/* The terrain of cell x, y (6beb:0493), within the table (DS:1ee4) or
 * false. */
static bool terrain_of(cok_adventure *game, int x, int y, uint8_t *occupant, uint8_t *terrain,
                       const char *where)
{
    cok_combat_cell(&game->combat, (int8_t)x, (int8_t)y, occupant, terrain);
    if (*terrain < cok_data_count(cok_game_rules(game)->data, COK_DATA_TERRAIN, 0)) return true;
    char text[96];
    snprintf(text, sizeof text, "terrain past the table is read (DS:1ee4, %s)", where);
    return undefined(game, text);
}

/* The map's cell x, y, on the map, written as the original writes it at
 * map + 7 + y * 50 + x. */
static bool set_cell(cok_adventure *game, int x, int y, uint8_t terrain)
{
    int at = y * COK_COMBAT_WIDTH + x;
    if (at < 0 || at >= COK_COMBAT_WIDTH * COK_COMBAT_HEIGHT)
        return undefined(game, "a cloud's cell is written outside the map (3f44:0ff7)");
    game->combat.cells[at / COK_COMBAT_WIDTH][at % COK_COMBAT_WIDTH] = terrain;
    return true;
}

/* A cloud's cell k (1-4). */
static int cloud_x(const cok_adventure *game, const cok_combat_cloud *p, unsigned k)
{
    return p->x + cok_step_x[cell_dir(game, k)];
}

static int cloud_y(const cok_adventure *game, const cok_combat_cloud *p, unsigned k)
{
    return p->y + cok_step_y[cell_dir(game, k)];
}

/* The terrain a cloud kept for cell x, y, already a green cloud: of each
 * cloud but p with a covered cell there, what it kept, unless that is a
 * green cloud too; the last such. */
static uint8_t under_cloud(cok_adventure *game, const cok_combat_cloud *p, int x, int y,
                           uint8_t terrain)
{
    for (const cok_combat_cloud *q = game->combat.clouds; q != NULL; q = q->next) {
        if (q == p) continue;
        for (unsigned j = 1; j <= 4; ++j)
            if (q->covered[j] && x == cloud_x(game, q, j) && y == cloud_y(game, q, j) &&
                q->saved[j] != 0x1e)
                terrain = q->saved[j];
    }
    return terrain;
}

/* Cover every cloud's cells again, then every pool's cell. */
static bool cover(cok_adventure *game, bool pools)
{
    for (const cok_combat_cloud *q = game->combat.clouds; q != NULL; q = q->next)
        for (unsigned k = 1; k <= 4; ++k)
            if (q->covered[k] && !set_cell(game, cloud_x(game, q, k), cloud_y(game, q, k), 0x1e))
                return false;
    if (!pools) return true;
    for (const cok_combat_cloud *q = game->combat.pools; q != NULL; q = q->next)
        if (!set_cell(game, q->x, q->y, 0x1d)) return false;
    return true;
}

/* Whether a body not taken back (its record not NULL) lies at x, y; the
 * coordinates bytes. */
static bool body_at(const cok_combat *combat, uint8_t x, uint8_t y)
{
    bool found = false;
    for (unsigned j = 1; j <= combat->bodies && j <= COK_COMBAT_BODIES; ++j) {
        const cok_combat_body *b = &combat->body[j - 1];
        if (b->character != NULL && (uint8_t)b->x == x && (uint8_t)b->y == y) found = true;
    }
    return found;
}

/* Unlink p from list (head). */
static void unlink(cok_combat_cloud **head, cok_combat_cloud *p)
{
    if (*head == p) {
        *head = p->next;
        return;
    }
    cok_combat_cloud *q = *head;
    while (q->next != p) q = q->next;
    q->next = p->next;
}

/* A new node at the end of list, the caster's nodes before the last one
 * counted in *count (the last is never looked at). */
static cok_combat_cloud *append(cok_adventure *game, cok_combat_cloud **head,
                                const uint8_t *caster, uint8_t *count)
{
    cok_combat_cloud *p = calloc(1, sizeof *p);
    *count = 0;
    if (p == NULL) {
        undefined(game, "out of memory laying a cloud");
        return NULL;
    }
    if (*head == NULL) {
        *head = p;
        return p;
    }
    cok_combat_cloud *q = *head;
    for (; q->next != NULL; q = q->next)
        if (q->caster == caster) ++*count;
    q->next = p;
    return p;
}

static bool dk_lay(cok_adventure *game, bool kill);

bool cok_cloud_stinking(cok_adventure *game, bool kill)
{
    if (dkk(game)) return dk_lay(game, kill);
    cok_combat *combat = &game->combat;
    cok_rolls *r = &game->effects.rolls;
    r->images = 1;
    uint8_t level;
    if (!cok_effects_caster_level(&game->effects, r->spell, &level)) return effect_failed(game);
    uint8_t *caster = game->vm.character;
    cok_character *who = member_of(game, caster);
    if (who == NULL)
        return undefined(game, "Stinking Cloud's caster is not in the list (5b04:2ac4)");
    uint8_t count;
    cok_combat_cloud *p = append(game, &combat->clouds, caster, &count);
    if (p == NULL) return false;
    if (cok_character_add_effect(who, cok_game_effect(game, COK_EFFECT_CLOUD), level,
                                 (uint8_t)((count << 4) + level), true) == NULL)
        return undefined(game, "out of memory adding a spell's effect");
    p->caster = caster;
    p->index = count;
    p->x = game->spell_x;
    p->y = game->spell_y;
    uint8_t occupant[5] = {0};
    for (unsigned k = 1; k <= 4; ++k) {
        int x = cloud_x(game, p, k), y = cloud_y(game, p, k);
        uint8_t terrain;
        if (!terrain_of(game, x, y, &occupant[k], &terrain, "5b04:2b60")) return false;
        p->covered[k] = terrain > 0 &&
                        cok_combat_terrain(cok_game_rules(game), terrain).cost < 0xff;
        if (terrain == 0x1e) {
            terrain = under_cloud(game, p, x, y, terrain);
        } else if (terrain == COK_COMBAT_BODY) {
            /* Every body counted, one taken back too (its cell 0, 0). */
            for (unsigned j = 1; j <= combat->bodies; ++j) {
                if (j > COK_COMBAT_BODIES)
                    return undefined(game, "a body past the table's eighth is read (5b04:2cce)");
                const cok_combat_body *b = &combat->body[j - 1];
                if (x == b->x && y == b->y) terrain = b->cell;
            }
        }
        p->saved[k] = terrain;
        if (p->covered[k] && !set_cell(game, x, y, 0x1e)) return false;
    }
    if (!cok_arena_say(game, who, cok_game_rules(game)->fight.noxious, 10, false) ||
        !cok_arena_centre(game, game->spell_x, game->spell_y, 0xff, 8))
        return false;
    cok_adventure_wait(game, game->speed * 100u); /* 1521:0b4b */
    cok_arena_clear_text(game);
    /* Each combatant once: a cell's occupant also on another cell is
     * dropped from the earlier (the first cell's is kept). */
    for (unsigned i = 2; i <= 4; ++i)
        for (unsigned j = 1; j <= 4; ++j)
            if (occupant[j] == occupant[i] && i != j) occupant[i] = 0;
    for (unsigned k = 1; k <= 4; ++k) {
        if (occupant[k] == 0) continue;
        cok_character *o = combat->combatant[occupant[k]].character;
        if (o == NULL) return undefined(game, "a cell's combatant has no record (5b04:2e34)");
        if (!cok_cloud_square(game, o, true)) return false;
    }
    return true;
}

uint8_t *cok_cloud_caster(const cok_adventure *game, int8_t x, int8_t y)
{
    for (const cok_combat_cloud *p = game->combat.clouds; p != NULL; p = p->next)
        for (unsigned k = 1; k <= 4; ++k) {
            uint8_t d = cok_data_u8_at(cok_game_rules(game)->data, COK_DATA_CLOUD_CELLS,
                                       k + 1); /* DS:2092 + k */
            if (p->covered[k] && x == p->x + cok_step_x[d] && y == p->y + cok_step_y[d])
                return p->caster;
        }
    return game->party.count > 0 ? game->party.members[0]->record : NULL;
}

static bool dk_square(cok_adventure *game, cok_character *c, bool step);

bool cok_cloud_square(cok_adventure *game, cok_character *c, bool step)
{
    if (dkk(game)) return dk_square(game, c, step);
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) return true;
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, cok_game_rules(game), r, 8, &occupant, &terrain, &cloud, &puddle))
        return undefined(game, "a size past the footprints is read after them (6beb:0c9d)");
    if (cloud && step && cok_field_u8(L, r, COK_FIELD_UNDEAD) == 0 &&
        cok_character_find_named(c, cok_game_rules(game), COK_EFFECT_HELPLESS) == NULL &&
        cok_character_find_named(c, cok_game_rules(game), COK_EFFECT_POISON_IMMUNE) == NULL) {
        bool made;
        if (!cok_effects_save(&game->effects, c, 0, 0, &made)) return effect_failed(game);
        uint8_t *selected = game->vm.character;
        int8_t x = cok_combat_x(combat, r), y = cok_combat_y(combat, r);
        game->vm.character = cok_cloud_caster(game, x, y);
        uint8_t id = made ? cok_game_effect(game, COK_EFFECT_NAUSEA)
                          : cok_game_effect(game, COK_EFFECT_HELPLESS);
        uint16_t minutes = (uint16_t)(cok_dice(&game->vm.seed, 1, 4) + 1);
        const cok_rules_fight *F = &cok_game_rules(game)->fight;
        bool ok = cok_cast_add_effect(game, c, id, minutes, 0xff, false, 0, made,
                                      made ? F->cough : F->choke);
        cok_effect *effect = cok_character_find_effect(c, id); /* 6346:2447 */
        if (ok && effect != NULL &&
            !cok_effects_run(&game->effects, c, id, made ? NULL : effect))
            ok = effect_failed(game);
        game->vm.character = selected;
        if (!ok) return false;
    }
    if (!puddle) return true;
    uint8_t amount = cok_dice_count(&game->vm.seed, 1, 6, &game->effects.rolls.dice);
    return cok_cast_damage(game, c, amount, 0, false);
}

bool cok_cloud_clear(cok_adventure *game, cok_character *c, const cok_effect *effect)
{
    cok_combat *combat = &game->combat;
    uint8_t index = (uint8_t)(effect->value >> 4);
    cok_combat_cloud *p = combat->clouds;
    while (p != NULL && !(p->caster == c->record && p->index == index)) p = p->next;
    if (p == NULL) return true;
    cok_camp_notice(game, cok_game_rules(game)->fight.clears); /* 6346:1827 */
    for (unsigned k = 1; k <= 4; ++k) {
        if (!p->covered[k]) continue;
        uint8_t x = (uint8_t)cloud_x(game, p, k), y = (uint8_t)cloud_y(game, p, k);
        if (!set_cell(game, x, y, body_at(combat, x, y) ? COK_COMBAT_BODY : p->saved[k]))
            return false;
    }
    unlink(&combat->clouds, p);
    free(p);
    return cover(game, true);
}

/* For 3f44:2037's body branch: the terrain the bodies give for each byte
 * d of the stack it never set, the spell's cell moved by DS:1ed6 + d and
 * DS:1edf + d; whether all agree. */
static bool stale_body(cok_adventure *game, uint8_t *terrain)
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    const cok_combat *combat = &game->combat;
    uint8_t first = 0;
    for (unsigned d = 0; d <= 0xff; ++d) {
        uint8_t t = COK_COMBAT_BODY;
        int x = game->spell_x +
                cok_cast_step_byte(D, (uint16_t)cok_data_offset(D, COK_DATA_STEP_X, d));
        int y = game->spell_y +
                cok_cast_step_byte(D, (uint16_t)cok_data_offset(D, COK_DATA_STEP_Y, d));
        for (unsigned j = 1; j <= combat->bodies && j <= COK_COMBAT_BODIES; ++j) {
            const cok_combat_body *b = &combat->body[j - 1];
            if (x == b->x && y == b->y) t = b->cell;
        }
        if (d == 0) first = t;
        if (t != first) return false;
    }
    *terrain = first;
    return true;
}

bool cok_cloud_acid(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (cok_field_u8(L, r, COK_FIELD_HIT_POINTS) > 0 && cok_field_u8(L, r, COK_FIELD_ACTIVE) != 0)
        return true;
    if (!combat->active)
        return undefined(game, "an acid pool is laid on the combat map outside combat, through "
                               "the NULL map (3f44:2037)");
    if (combat->bodies > COK_COMBAT_BODIES)
        return undefined(game, "a body past the table's eighth is read (3f44:22bf)");
    uint8_t count;
    cok_combat_cloud *p = append(game, &combat->pools, r, &count);
    if (p == NULL) return false;
    uint8_t minutes = cok_dice(&game->vm.seed, 1, 6);
    if (cok_character_add_effect(c, cok_game_effect(game, COK_EFFECT_ACID_POOL), minutes, count,
                                 true) == NULL)
        return undefined(game, "out of memory adding a spell's effect");
    p->caster = r;
    p->index = count;
    p->x = cok_combat_x(combat, r);
    p->y = cok_combat_y(combat, r);
    /* 2037 compares the terrain with 0x1e and 0x1f only, never reading the
     * terrain table. */
    uint8_t occupant, terrain;
    cok_combat_cell(combat, p->x, p->y, &occupant, &terrain);
    if (terrain == 0x1e) {
        terrain = under_cloud(game, p, p->x, p->y, terrain);
    } else if (terrain == COK_COMBAT_BODY && !stale_body(game, &terrain)) {
        p->unknown = true;
    }
    p->saved[1] = terrain;
    if (!set_cell(game, p->x, p->y, 0x1d)) return false;
    int8_t x = cok_combat_x(combat, r), y = cok_combat_y(combat, r);
    return cok_arena_centre(game, x, y, 0xff, 8) &&
           cok_arena_say(game, c, "dissolves into acid!", 10, false);
}

bool cok_cloud_drain(cok_adventure *game, cok_character *c, const cok_effect *effect)
{
    cok_combat *combat = &game->combat;
    cok_combat_cloud *p = combat->pools;
    while (p != NULL && !(p->caster == c->record && p->index == effect->value)) p = p->next;
    if (p == NULL) return true;
    cok_camp_notice(game, "The acid drains away.."); /* 6346:1827 */
    bool body = body_at(combat, (uint8_t)p->x, (uint8_t)p->y);
    if (!body && p->unknown)
        return undefined(game, "an acid pool puts back the terrain it kept from a byte of the "
                               "stack 3f44:2037 never set (3f44:22a0)");
    if (!set_cell(game, p->x, p->y, body ? COK_COMBAT_BODY : p->saved[1])) return false;
    unlink(&combat->pools, p);
    if (!cover(game, false)) return false;
    free(p);
    return cover(game, true);
}

/* 5b04:3151: whether Dispel Magic of the caster level level takes away a
 * cloud of caster's. */
static bool dispelled(cok_adventure *game, const uint8_t *caster, uint8_t level)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    int former = cok_character_former_class(game->vm.character, cok_game_rules(game)) ? 1 : 0;
    uint8_t of = (uint8_t)(former * cok_character_former_level(cok_game_rules(game), caster, 5) +
                           cok_field_s8_at(L, caster, COK_FIELD_CLASS_LEVEL, 5));
    uint8_t chance = 50;
    if (level > of)
        chance = (uint8_t)((level - of) * 5 + 50);
    else if (level < of)
        chance = (uint8_t)(50 - 2 * (of - level));
    return cok_dice(&game->vm.seed, 1, 100) <= chance;
}

/* Whether p is still in the list. */
static bool listed(const cok_combat *combat, const cok_combat_cloud *p)
{
    for (const cok_combat_cloud *q = combat->clouds; q != NULL; q = q->next)
        if (q == p) return true;
    return false;
}

static bool dk_dispel(cok_adventure *game, uint8_t level, int stale);

bool cok_cloud_dispel(cok_adventure *game, uint8_t level, int stale)
{
    cok_combat *combat = &game->combat;
    if (!combat->active) return true;
    if (dkk(game)) return dk_dispel(game, level, stale);
    if (game->vm.character == NULL)
        return undefined(game, "Dispel Magic tries a cloud with no character selected, reading "
                               "through NULL (5b04:315f)");
    /* The cells, each case moving x or y from the last (5b04:3360). */
    static const int8_t walk[9][2] = {{0, 0},  {0, -1}, {1, -1}, {1, 0}, {1, 1},
                                      {0, 1},  {-1, 1}, {-1, 0}, {-1, -1}};
    for (unsigned k = 0; k <= 8; ++k) {
        int8_t x = (int8_t)(game->spell_x + walk[k][0]), y = (int8_t)(game->spell_y + walk[k][1]);
        uint8_t occupant, terrain;
        cok_combat_cell(combat, x, y, &occupant, &terrain);
        if (terrain != 0x1e) continue;
        for (cok_combat_cloud *p = combat->clouds; p != NULL;) {
            bool freed = false;
            for (unsigned i = 1; i <= 4 && !freed; ++i) {
                if (!(x == cloud_x(game, p, i) && y == cloud_y(game, p, i) && p->resisted == 0))
                    continue;
                if (!dispelled(game, p->caster, level)) {
                    p->resisted = 1;
                    continue;
                }
                cok_character *caster = member_of(game, p->caster);
                if (caster == NULL)
                    return undefined(game, "a cloud's caster is not in the list (5b04:34ab)");
                cok_effect *e = caster->effects;
                uint8_t cloud = cok_game_effect(game, COK_EFFECT_CLOUD);
                while (e != NULL && !(e->id == cloud && (e->value >> 4) == p->index)) e = e->next;
                if (e == NULL) continue;
                if (!cok_effects_remove(&game->effects, caster, e, cloud))
                    return effect_failed(game);
                /* The cell is no other of p's: the rest of its cells are
                 * read from the freed node to no effect. */
                freed = !listed(combat, p);
            }
            if (freed)
                return undefined(game, "Dispel Magic reads the next cloud from the one it "
                                       "freed, where FreeMem wrote the block's size "
                                       "(1a46:03fc): 0002:0000, the interrupt vectors, then "
                                       "what they point at (5b04:354a)");
            p = p->next;
        }
        if (!cok_arena_centre(game, game->spell_x, game->spell_y, 3, 8)) return false;
    }
    return true;
}

/* Death Knights' clouds (dkk 5dc5:2934, 638c:0c65, 21ec, 213c, 6f03:152e,
 * 5dc5:2e5c's second part and 2dd7). */

uint8_t cok_cloud_under(const cok_adventure *game, int8_t x, int8_t y, uint8_t value)
{
    const cok_rules_fight *F = &cok_game_rules(game)->fight;
    const cok_combat *combat = &game->combat;
    if (value == F->cloud_tile || value == F->pool_tile) {
        for (const cok_combat_cloud *p = combat->clouds; p != NULL; p = p->next)
            for (unsigned k = 1; k <= DKK_CELLS; ++k)
                if (p->covered[k] && x == cloud_x(game, p, k) && y == cloud_y(game, p, k))
                    value = p->saved[k];
    } else if (value == COK_COMBAT_BODY) {
        for (unsigned j = 1; j <= combat->bodies && j <= COK_COMBAT_BODIES; ++j) {
            const cok_combat_body *b = &combat->body[j - 1];
            if (b->x == x && b->y == y) value = b->cell;
        }
    }
    return value;
}

/* dkk 638c:213c: every cloud's covered cells get its tile again, in the
 * list's order (the latest laid first, so that an earlier one's tile is
 * the one left where they overlap). */
static bool dk_cover(cok_adventure *game)
{
    for (const cok_combat_cloud *p = game->combat.clouds; p != NULL; p = p->next)
        for (unsigned k = 1; k <= DKK_CELLS; ++k)
            if (p->covered[k] && !set_cell(game, cloud_x(game, p, k), cloud_y(game, p, k), p->tile))
                return false;
    return true;
}

/* dkk 5dc5:2934, Stinking Cloud's (four cells, tile fight.cloud_tile) or,
 * with kill, Cloud Kill's (nine, fight.pool_tile; the original tells them
 * apart by the spell, 0x22 or not): a node for the selected character
 * (the caster) at the spell's cell (DS:c441, c442), its rounds and level
 * the caster level (65cd:2c33). Each cell on the map (x 0-0x31, y
 * 0-0x18) keeps the cell under it (6f03:152e: a cloud's kept terrain, a
 * body's cell), covered unless that is 0 or a wall (cost 0xff), and its
 * tile laid if covered; its combatant is noted, unless an earlier cell's.
 * The node goes first in the list (DS:c443), the screen is centred on
 * the cell (forced), and each combatant noted is in the cloud (638c:0c65
 * with step). No word is said. */
static bool dk_lay(cok_adventure *game, bool kill)
{
    const cok_rules *rules = cok_game_rules(game);
    cok_combat *combat = &game->combat;
    uint8_t level;
    if (!cok_effects_caster_level(&game->effects, game->effects.rolls.spell, &level))
        return effect_failed(game);
    cok_combat_cloud *p = calloc(1, sizeof *p);
    if (p == NULL) return undefined(game, "out of memory laying a cloud");
    p->x = game->spell_x;
    p->y = game->spell_y;
    p->caster = game->vm.character;
    p->level = p->age = level;
    p->tile = kill ? rules->fight.pool_tile : rules->fight.cloud_tile;
    unsigned cells = kill ? 9 : 4;
    uint8_t occupant[DKK_CELLS + 1] = {0};
    for (unsigned k = 1; k <= cells; ++k) {
        int x = cloud_x(game, p, k), y = cloud_y(game, p, k);
        if (x < 0 || x > 0x31 || y < 0 || y > 0x18) {
            p->covered[k] = 0;
            continue;
        }
        uint8_t who, terrain;
        cok_combat_cell(combat, (int8_t)x, (int8_t)y, &who, &terrain); /* 6f03:06d0 */
        for (unsigned j = 1; j + 1 <= k; ++j)
            if (occupant[j] == who) who = 0;
        occupant[k] = who;
        uint8_t under = cok_cloud_under(game, (int8_t)x, (int8_t)y, terrain);
        if (under != 0 && under >= cok_data_count(rules->data, COK_DATA_TERRAIN, 0)) {
            free(p);
            return undefined(game, "terrain past the table is read (DS:4420, dkk 5dc5:2ac5)");
        }
        p->covered[k] = under != 0 && cok_combat_terrain(rules, under).cost < 0xff;
        p->saved[k] = under;
        if (p->covered[k] && !set_cell(game, x, y, p->tile)) {
            free(p);
            return false;
        }
    }
    p->next = combat->clouds;
    combat->clouds = p;
    if (!cok_arena_centre(game, game->spell_x, game->spell_y, 0xff, 8)) return false;
    for (unsigned k = 1; k <= cells; ++k) {
        if (occupant[k] == 0) continue;
        cok_character *o = combat->combatant[occupant[k]].character;
        if (o == NULL) return undefined(game, "a cell's combatant has no record (dkk 5dc5:2bad)");
        if (!cok_cloud_square(game, o, true)) return false;
    }
    return true;
}

/* dkk 638c:0c65 for c, who can act. In a cloud of Stinking Cloud's, with
 * step: the screen centred on c (forced) unless all of it is shown and
 * actions are (DS:c4d4); then unless it is undead (+0x31) or holds
 * effect 0x1f or 0x63, it throws (type 0): saved it "starts to cough",
 * effect 0x1e, else it "chokes and gags from nausea", 0x1f, either for
 * 1d4 + 1 minutes, valued 0xff (638c:1f86), and if it then has the effect
 * its handler runs (638c:0185), 0x1e's with no effect (the search for
 * 0x63's); one coughing that is casting "lost a spell" (row 12, a pause),
 * the spell to memorize again (65cd:17c4) and its turn over (65cd:2bac).
 * The selected character is kept. In a cloud of Cloud Kill's, with step
 * or not, while it can act: one of levels 0-4 (+0x30, signed), of 5 that
 * fails a throw at -4, or of 6 that fails one "is poisoned" (row 10), a
 * pause, effect 0x37 (no time, valued 0xff) and dies, "is killed"
 * (638c:00c8, status 6). */
static bool dk_square(cok_adventure *game, cok_character *c, bool step)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_rules_fight *F = &rules->fight;
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) return true;
    uint8_t occupant, terrain;
    bool cloud, kill;
    if (!cok_combat_probe(combat, rules, r, 8, &occupant, &terrain, &cloud, &kill))
        return undefined(game, "a size past the footprints is read after them (6f03:0e33)");
    if (cloud && step) {
        bool whole;
        if (!cok_combat_visible(combat, rules, r, true, &whole))
            return undefined(game, "a size past the footprints is read after them (6f03:08c6)");
        if ((!whole || !combat->show_actions) &&
            !cok_arena_centre(game, cok_combat_x(combat, r), cok_combat_y(combat, r), 0xff, 8))
            return false;
        if (cok_field_u8(L, r, COK_FIELD_UNDEAD) == 0 &&
            cok_character_find_named(c, rules, COK_EFFECT_HELPLESS) == NULL &&
            cok_character_find_named(c, rules, COK_EFFECT_POISON_IMMUNE) == NULL) {
            bool made;
            if (!cok_effects_save(&game->effects, c, 0, 0, &made)) return effect_failed(game);
            uint8_t *selected = game->vm.character;
            uint8_t id = made ? cok_game_effect(game, COK_EFFECT_NAUSEA)
                              : cok_game_effect(game, COK_EFFECT_HELPLESS);
            uint16_t minutes = (uint16_t)(cok_dice(&game->vm.seed, 1, 4) + 1);
            bool ok = cok_cast_add_effect(game, c, id, minutes, 0xff, false, 0, made,
                                          made ? F->cough : F->choke);
            cok_effect *effect = ok ? cok_character_find_effect(c, id) : NULL; /* 65cd:2687 */
            if (effect != NULL) {
                ok = cok_effects_run(&game->effects, c, id, made ? NULL : effect) ||
                     effect_failed(game);
                if (ok && made && c->combat != NULL && c->combat->spell > 0) {
                    ok = cok_arena_say(game, c, F->words.lost_spell, 12, true);
                    cok_combat_forget(rules, r, c->combat->spell);
                    cok_combat_end_turn(c);
                }
            }
            game->vm.character = selected;
            if (!ok) return false;
        }
    }
    if (!kill || cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) return true;
    int8_t level = cok_field_s8(L, r, COK_FIELD_HIGHEST_LEVEL);
    bool dies = level >= 0 && level <= 4;
    if (level == 5 || level == 6) {
        bool made;
        if (!cok_effects_save(&game->effects, c, 0, level == 5 ? 0xfc : 0, &made))
            return effect_failed(game);
        dies = !made;
    }
    if (!dies) return true;
    if (!cok_arena_say(game, c, F->poisoned, 10, false)) return false;
    cok_adventure_wait(game, game->speed * 100u); /* 14e4:0b56 */
    if (cok_character_add_effect(c, cok_game_effect(game, COK_EFFECT_POISONED), 0, 0xff, false) ==
        NULL)
        return undefined(game, "out of memory adding a spell's effect");
    return cok_combat_kill(game, c, 6, F->words.killed); /* 638c:00c8 */
}

/* dkk 638c:21ec: at the end of a round (not dispelled) each cloud whose
 * rounds are 1 or fewer (signed) goes and the others' are one fewer; or
 * (dispelled, after Dispel Magic) each cloud marked 0xff goes. One going
 * puts back each covered cell's kept terrain, or the body's tile where a
 * body (not taken back) lies, and is freed; after the list, if any went,
 * every cloud's cells are covered again (213c) and "the air clears a
 * little..." (65cd:19c5). The screen is not redrawn. */
static bool dk_age(cok_adventure *game, bool dispelled)
{
    cok_combat *combat = &game->combat;
    bool gone = false;
    cok_combat_cloud *last = NULL;
    for (cok_combat_cloud *p = combat->clouds, *next; p != NULL; p = next) {
        next = p->next;
        if (dispelled ? p->age != 0xff : (int8_t)p->age > 1) {
            last = p;
            if (!dispelled) --p->age;
            continue;
        }
        for (unsigned k = 1; k <= DKK_CELLS; ++k) {
            if (!p->covered[k]) continue;
            int x = cloud_x(game, p, k), y = cloud_y(game, p, k);
            if (!set_cell(game, x, y,
                          body_at(combat, (uint8_t)x, (uint8_t)y) ? COK_COMBAT_BODY : p->saved[k]))
                return false;
        }
        gone = true;
        if (last == NULL)
            combat->clouds = next;
        else
            last->next = next;
        free(p);
    }
    if (!gone) return true;
    if (!dk_cover(game)) return false;
    cok_camp_notice(game, cok_game_rules(game)->fight.clears); /* 65cd:19c5 */
    return true;
}

bool cok_cloud_round(cok_adventure *game)
{
    return !dkk(game) || dk_age(game, false);
}

/* dkk 5dc5:2dd7: whether Dispel Magic of the caster level level takes
 * away a cloud of the level of: a d100 up to 50, plus 5 a level above it,
 * or, below it, less 2 a level that DS:8954 is above the caster level (as
 * a byte; the cloud's own level is never stored there), stale the byte
 * DS:8954 holds (the last effect's level the dispel tried), -1 where the
 * port does not know it. */
static bool dk_dispelled(cok_adventure *game, uint8_t level, uint8_t of, int stale,
                         bool *taken)
{
    uint8_t chance = 50;
    if (of < level) {
        chance = (uint8_t)((level - of) * 5 + 50);
    } else if (level < of) {
        if (stale < 0)
            return undefined(game, "Dispel Magic tries a cloud of a higher level against the "
                                   "byte DS:8954, which an earlier spell left (dkk 5dc5:2e22)");
        chance = (uint8_t)(50 - 2 * (stale - level));
    }
    *taken = cok_dice(&game->vm.seed, 1, 100) <= chance;
    return true;
}

/* dkk 5dc5:2e5c after its targets: for the cells around the spell's cell
 * by the steps (DS:4412, 441b, 0-8, its own last), on a tile of Stinking
 * Cloud's (four cells) or Cloud Kill's (nine), each cloud with one of that
 * many cells there (covered or not) is tried (2dd7): taken, it is marked
 * 0xff; then the screen is centred on the spell's cell (margin 3). Then
 * the clouds marked go (638c:21ec with 1). */
static bool dk_dispel(cok_adventure *game, uint8_t level, int stale)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_data_segment *D = rules->data;
    cok_combat *combat = &game->combat;
    for (unsigned k = 0; k <= 8; ++k) {
        int8_t x = (int8_t)(game->spell_x + (int8_t)cok_data_u8_at(D, COK_DATA_STEP_X, k)),
               y = (int8_t)(game->spell_y + (int8_t)cok_data_u8_at(D, COK_DATA_STEP_Y, k));
        uint8_t occupant, terrain;
        cok_combat_cell(combat, x, y, &occupant, &terrain); /* 6f03:06d0 */
        if (terrain != rules->fight.cloud_tile && terrain != rules->fight.pool_tile) continue;
        unsigned cells = terrain == rules->fight.cloud_tile ? 4 : 9;
        for (cok_combat_cloud *p = combat->clouds; p != NULL; p = p->next)
            for (unsigned i = 1; i <= cells; ++i) {
                if (x != cloud_x(game, p, i) || y != cloud_y(game, p, i)) continue;
                bool taken = false;
                if (!dk_dispelled(game, level, p->level, stale, &taken)) return false;
                if (taken) p->age = 0xff;
            }
        if (!cok_arena_centre(game, game->spell_x, game->spell_y, 3, 8)) return false;
    }
    return dk_age(game, true);
}

void cok_cloud_free(cok_combat *combat)
{
    for (int k = 0; k < 2; ++k) {
        cok_combat_cloud **head = k == 0 ? &combat->clouds : &combat->pools;
        while (*head != NULL) {
            cok_combat_cloud *next = (*head)->next;
            free(*head);
            *head = next;
        }
    }
}
