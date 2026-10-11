#include "cloud.h"

#include "arena.h"
#include "camp.h"
#include "cast.h"
#include "round.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DS:1ed6, 1edf: a step in direction 0-7, north first, clockwise; 8 for
 * none. DS:2092-2096: a cloud's cells, k 1-4 at DS:2091 + k, and the byte
 * after them, which 60f4:0c95 reads for cell 4. */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};
static const uint8_t cells[5] = {8, 2, 3, 4, 8};

const cok_ds_table cok_cloud_tables[] = {
    {0x1ed6, sizeof step_x, 1, (const uint8_t *)step_x},
    {0x1edf, sizeof step_y, 1, (const uint8_t *)step_y},
    {0x2092, sizeof cells, 1, cells},
};
const size_t cok_cloud_table_count = sizeof cok_cloud_tables / sizeof *cok_cloud_tables;

/* The direction of cell k (1-4) of a cloud (DS:2091 + k). */
static uint8_t cell_dir(unsigned k)
{
    return cells[k - 1];
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

/* The terrain of cell x, y (6beb:0493), within the table (DS:1ee4) or
 * false. */
static bool terrain_of(cok_adventure *game, int x, int y, uint8_t *occupant, uint8_t *terrain,
                       const char *where)
{
    cok_combat_cell(&game->combat, (int8_t)x, (int8_t)y, occupant, terrain);
    if (*terrain < COK_COMBAT_TERRAINS) return true;
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
static int cloud_x(const cok_combat_cloud *p, unsigned k)
{
    return p->x + step_x[cell_dir(k)];
}

static int cloud_y(const cok_combat_cloud *p, unsigned k)
{
    return p->y + step_y[cell_dir(k)];
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
            if (q->covered[j] && x == cloud_x(q, j) && y == cloud_y(q, j) && q->saved[j] != 0x1e)
                terrain = q->saved[j];
    }
    return terrain;
}

/* Cover every cloud's cells again, then every pool's cell. */
static bool cover(cok_adventure *game, bool pools)
{
    for (const cok_combat_cloud *q = game->combat.clouds; q != NULL; q = q->next)
        for (unsigned k = 1; k <= 4; ++k)
            if (q->covered[k] && !set_cell(game, cloud_x(q, k), cloud_y(q, k), 0x1e)) return false;
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

bool cok_cloud_stinking(cok_adventure *game)
{
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
    if (cok_character_add_effect(who, 0x28, level, (uint8_t)((count << 4) + level), true) == NULL)
        return undefined(game, "out of memory adding a spell's effect");
    p->caster = caster;
    p->index = count;
    p->x = game->spell_x;
    p->y = game->spell_y;
    uint8_t occupant[5] = {0};
    for (unsigned k = 1; k <= 4; ++k) {
        int x = cloud_x(p, k), y = cloud_y(p, k);
        uint8_t terrain;
        if (!terrain_of(game, x, y, &occupant[k], &terrain, "5b04:2b60")) return false;
        p->covered[k] = terrain > 0 && cok_combat_terrain[terrain].cost < 0xff;
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
    if (!cok_arena_say(game, who, "Creates a noxious cloud", 10, false) ||
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
            uint8_t d = cells[k]; /* DS:2092 + k */
            if (p->covered[k] && x == p->x + step_x[d] && y == p->y + step_y[d]) return p->caster;
        }
    return game->party.count > 0 ? game->party.members[0]->record : NULL;
}

bool cok_cloud_square(cok_adventure *game, cok_character *c, bool step)
{
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (r[0x189] == 0) return true;
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, r, 8, &occupant, &terrain, &cloud, &puddle))
        return undefined(game, "a size past the footprints is read after them (6beb:0c9d)");
    if (cloud && step && r[0xda] == 0 && cok_character_find_effect(c, 0x1f) == NULL &&
        cok_character_find_effect(c, 0x63) == NULL) {
        bool made;
        if (!cok_effects_save(&game->effects, c, 0, 0, &made)) return effect_failed(game);
        uint8_t *selected = game->vm.character;
        int8_t x = cok_combat_x(combat, r), y = cok_combat_y(combat, r);
        game->vm.character = cok_cloud_caster(game, x, y);
        uint8_t id = made ? 0x1e : 0x1f;
        uint16_t minutes = (uint16_t)(cok_dice(&game->vm.seed, 1, 4) + 1);
        bool ok = cok_cast_add_effect(game, c, id, minutes, 0xff, false, 0, made,
                                      made ? "starts to cough" : "chokes and gags from nausea");
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
    cok_camp_notice(game, "The air clears a little..."); /* 6346:1827 */
    for (unsigned k = 1; k <= 4; ++k) {
        if (!p->covered[k]) continue;
        uint8_t x = (uint8_t)cloud_x(p, k), y = (uint8_t)cloud_y(p, k);
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
    const cok_combat *combat = &game->combat;
    uint8_t first = 0;
    for (unsigned d = 0; d <= 0xff; ++d) {
        uint8_t t = COK_COMBAT_BODY;
        int x = game->spell_x + cok_cast_step_byte((uint16_t)(0x1ed6 + d));
        int y = game->spell_y + cok_cast_step_byte((uint16_t)(0x1edf + d));
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
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (r[0x197] > 0 && r[0x189] != 0) return true;
    if (!combat->active)
        return undefined(game, "an acid pool is laid on the combat map outside combat, through "
                               "the NULL map (3f44:2037)");
    if (combat->bodies > COK_COMBAT_BODIES)
        return undefined(game, "a body past the table's eighth is read (3f44:22bf)");
    uint8_t count;
    cok_combat_cloud *p = append(game, &combat->pools, r, &count);
    if (p == NULL) return false;
    uint8_t minutes = cok_dice(&game->vm.seed, 1, 6);
    if (cok_character_add_effect(c, 0x50, minutes, count, true) == NULL)
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
    int former = cok_character_former_class(game->vm.character) ? 1 : 0;
    uint8_t of = (uint8_t)(former * (int8_t)caster[0x106] + (int8_t)caster[0xfe]);
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

bool cok_cloud_dispel(cok_adventure *game, uint8_t level)
{
    cok_combat *combat = &game->combat;
    if (!combat->active) return true;
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
                if (!(x == cloud_x(p, i) && y == cloud_y(p, i) && p->resisted == 0)) continue;
                if (!dispelled(game, p->caster, level)) {
                    p->resisted = 1;
                    continue;
                }
                cok_character *caster = member_of(game, p->caster);
                if (caster == NULL)
                    return undefined(game, "a cloud's caster is not in the list (5b04:34ab)");
                cok_effect *e = caster->effects;
                while (e != NULL && !(e->id == 0x28 && (e->value >> 4) == p->index)) e = e->next;
                if (e == NULL) continue;
                if (!cok_effects_remove(&game->effects, caster, e, 0x28))
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
