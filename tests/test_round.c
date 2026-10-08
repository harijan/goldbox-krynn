#include "round.h"

#include "adventure.h"
#include "cast.h"
#include "monster.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    const char *keys;
    size_t at, length;
    unsigned battles;       /* Times the battlefield hook ran. */
    bool targets_in_battle; /* Spells' targets were the combat routine's then. */
    char log[65536];
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    return (unsigned char)s->keys[s->at++];
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static void unported(cok_adventure *g, void *context)
{
    (void)g;
    (void)context;
}

static script s;
static cok_adventure game;

static void battlefield(cok_adventure *g, void *context)
{
    script *k = context;
    ++k->battles;
    k->targets_in_battle = g->combat_targets && g->effects.in_battle;
}

#define LOGGED(text) (strstr(s.log, text) != NULL)

static void reset(const char *keys)
{
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    game.vm.character = game.vm.saved_character = NULL;
    game.vm.restore_character = false;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    memset(game.view.map, 0, sizeof game.view.map);
    memset(&game.effects.rolls, 0, sizeof game.effects.rolls);
    game.view.wrap = false;
    game.vm.mode = 5;
    game.vm.mem4b00[0xe6] = 1;
    game.vm.mem7c00[0x2e1] = 0xff;
    game.vm.file = 1;
    game.vm.map_x = game.vm.map_y = 7;
    game.vm.direction = 0;
    game.vm.seed = 1;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.input_ended = false;
    game.combat_stub = COK_COMBAT_UNPORTED;
    game.combat_targets = false;
    game.effects.in_battle = false;
    game.selected = 3;
    game.monsters = game.undead = 0;
    game.monsters_loaded = false;
    game.icon_slot = 8;
    cok_combat *c = &game.combat;
    memset(c->cells, COK_COMBAT_FLOOR, sizeof c->cells);
    memset(c->combatant, 0, sizeof c->combatant);
    memset(c->turn, 0, sizeof c->turn);
    memset(c->listed, 0, sizeof c->listed);
    memset(c->enemies, 0, sizeof c->enemies);
    c->listed_count = 0;
    c->count = 1;
    c->bodies = 0;
    c->view_x = c->view_y = 0;
    c->see_all = 0;
    c->cursor = 0;
    c->cursor_size = 1;
    c->round_limit = 15;
    c->sides[0] = c->sides[1] = 0;
    c->exploding_count = 0;
    c->yelled = NULL;
    c->show_actions = false;
    c->active = true;
    s.keys = keys;
    s.at = 0;
    s.length = strlen(keys);
    s.log[0] = '\0';
}

/* A record named by a letter, one cell, dexterity 12, 10 hit points, on
 * side, with a zeroed combat record, a combatant at x, y. */
static cok_character *record(char name, uint8_t side, int8_t x, int8_t y)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = 1;
    r[1] = (uint8_t)name;
    r[0x10] = r[0x11] = r[0x16] = r[0x17] = 12;
    r[0x62] = r[0x197] = 10;
    r[0xcf] = 1;
    r[0x189] = 1;
    r[0x18a] = side;
    r[0x198] = 12;
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL && cok_party_append(&game.party, c));
    cok_combat *k = &game.combat;
    cok_combatant *e = &k->combatant[k->count];
    e->x = x;
    e->y = y;
    e->size = 1;
    e->character = c;
    ++k->count;
    k->combatant[k->count].size = 0;
    k->combatant[k->count].character = NULL;
    CHECK(cok_combat_occupy(k));
    return c;
}

/* Turbo Pascal's Random from seed: the next value of range. */
static uint32_t next(uint32_t *seed, uint16_t range)
{
    return cok_tp_random(seed, range);
}

static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("round: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    for (size_t i = 0; i < cok_round_table_count; ++i) {
        const cok_ds_table *t = &cok_round_tables[i];
        for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    }
}

static void test_turn_order(void)
{
    reset("");
    cok_character *a = record('A', 0, 1, 1), *b = record('B', 0, 2, 1), *c = record('C', 1, 3, 1);
    cok_character *d = record('D', 1, 4, 1);
    /* Highest first, the dead (0) too. */
    a->combat->initiative = 3;
    b->combat->initiative = 0;
    c->combat->initiative = 7;
    d->combat->initiative = 5;
    uint32_t seed = game.vm.seed;
    cok_combat_turn_order(&game);
    CHECK(game.combat.turn[1] == c && game.combat.turn[2] == d && game.combat.turn[3] == a &&
          game.combat.turn[4] == b && game.combat.turn[5] == NULL);
    CHECK(game.vm.seed == seed);
    /* A tie rolls a d2 for each equal entry met: on 1 the record goes
     * before it, else on. */
    reset("");
    a = record('A', 0, 1, 1);
    b = record('B', 0, 2, 1);
    c = record('C', 0, 3, 1);
    a->combat->initiative = b->combat->initiative = c->combat->initiative = 4;
    for (uint32_t start = 1; start < 64; ++start) {
        /* B meets A: one d2; C meets the first, then maybe the second. */
        seed = start;
        bool b_first = next(&seed, 2) == 0;
        bool c_first = next(&seed, 2) == 0;
        bool c_second = !c_first && next(&seed, 2) == 0;
        game.vm.seed = start;
        cok_combat_turn_order(&game);
        CHECK(game.vm.seed == seed);
        cok_character *first = b_first ? b : a, *second = b_first ? a : b;
        if (c_first) {
            CHECK(game.combat.turn[1] == c && game.combat.turn[2] == first &&
                  game.combat.turn[3] == second);
        } else if (c_second) {
            CHECK(game.combat.turn[1] == first && game.combat.turn[2] == c &&
                  game.combat.turn[3] == second);
        } else {
            CHECK(game.combat.turn[1] == first && game.combat.turn[2] == second &&
                  game.combat.turn[3] == c);
        }
    }
    /* An empty entry counts as -1: one of -1 rolls against it. */
    reset("");
    a = record('A', 0, 1, 1);
    a->combat->initiative = -1;
    seed = game.vm.seed = 5;
    cok_combat_turn_order(&game);
    next(&seed, 2);
    CHECK(game.vm.seed != 5 && game.combat.turn[1] == a);
    /* -2 passes every entry to 65, where the search stops. */
    a->combat->initiative = -2;
    seed = game.vm.seed = 5;
    cok_combat_turn_order(&game);
    CHECK(game.vm.seed == 5 && game.combat.turn[65] == a && game.combat.turn[1] == NULL);
}

static void test_round_start(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 1, 6, 5);
    uint8_t *r = a->record;
    r[0x17] = 17; /* +2 */
    r[0x10c] = 3;
    r[0x10b] = 4;
    r[0xce] = 6;
    a->combat->spell = 9;
    a->combat->attacked = 1;
    uint32_t seed = game.vm.seed = 77;
    CHECK(cok_combat_round_start(&game, a));
    uint32_t roll = next(&seed, 6) + 1;
    CHECK(a->combat->initiative == (int8_t)(roll + 2) && game.vm.seed == seed);
    CHECK(a->combat->spell == 0 && a->combat->may_cast == 1 && a->combat->may_use == 1);
    CHECK(a->combat->attacked == 0 && a->combat->attack_slot == 2 && a->combat->sweeps == 6);
    /* Round 0: half attacks round down; movement 12 doubled. */
    CHECK(r[0x18f] == 2 && r[0x190] == 1 && a->combat->movement == 24);
    /* In round 1 they round up. */
    game.effects.rolls.round = 1;
    CHECK(cok_combat_round_start(&game, a) && r[0x190] == 2 && r[0x18f] == 2);
    /* One that cannot act gets 0 and rolls nothing. */
    b->record[0x189] = 0;
    seed = game.vm.seed;
    b->combat->initiative = 5;
    CHECK(cok_combat_round_start(&game, b) && b->combat->initiative == 0 && game.vm.seed == seed);
    /* A low dexterity makes at least 1. */
    r[0x17] = 1; /* -4 */
    CHECK(cok_combat_round_start(&game, a) && a->combat->initiative == 1);
    /* Surprise (var 0x7ecb bit 0 for the party's side, bit 1 the enemy's)
     * takes 6: below 0 it is 0. */
    game.vm.mem7c00[0x2cb] = 1;
    CHECK(cok_combat_round_start(&game, a) && a->combat->initiative == 0);
    game.vm.mem7c00[0x2cb] = 2;
    CHECK(cok_combat_round_start(&game, a) && a->combat->initiative == 1);
    /* With the bonus of dexterity 24-25 (5) a surprised one may still
     * act: 6 + 5 - 6. */
    r[0x17] = 25;
    game.vm.mem7c00[0x2cb] = 1;
    for (uint32_t start = 1;; ++start) {
        seed = start;
        if (next(&seed, 6) == 5) {
            game.vm.seed = start;
            break;
        }
    }
    CHECK(cok_combat_round_start(&game, a) && a->combat->initiative == 5);
    game.vm.mem7c00[0x2cb] = 0;
    /* Above 20, which only effects could make... the cap: dexterity
     * 25 and a 6 make 11, under it. */
    CHECK(cok_combat_round_start(&game, a) && a->combat->initiative <= 11);
    /* Haste doubles the other attacks too. */
    CHECK(cok_character_add_effect(a, 0x27, 0, 0x10, false) != NULL);
    r[0x10c] = 4;
    game.effects.rolls.round = 0;
    CHECK(cok_combat_round_start(&game, a) && r[0x190] == 4);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x27));
    /* A stinking cloud (terrain 0x1e) under it: no casting. */
    game.combat.cells[5][5] = 0x1e;
    CHECK(cok_combat_round_start(&game, a) && a->combat->may_cast == 0);
}

static void test_movement(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 1, 6, 5);
    uint8_t m;
    a->record[0x198] = 0;
    CHECK(cok_combat_movement(&game, a, &m) && m == 2);
    a->record[0x198] = 0x60;
    CHECK(cok_combat_movement(&game, a, &m) && m == 0xc0);
    a->record[0x198] = 0x61;
    CHECK(cok_combat_movement(&game, a, &m) && m == 2);
    /* Var 0x7f72 adds to the party's side only, as a byte. */
    game.vm.mem7c00[0x372] = 0xff;
    a->record[0x198] = 12;
    b->record[0x198] = 12;
    CHECK(cok_combat_movement(&game, a, &m) && m == 22);
    CHECK(cok_combat_movement(&game, b, &m) && m == 24);
    a->record[0x198] = 1;
    CHECK(cok_combat_movement(&game, a, &m) && m == 2); /* 0 counts as 1 */
    game.vm.mem7c00[0x372] = 0;
    /* Haste doubles the rate after, as a byte. */
    CHECK(cok_character_add_effect(b, 0x27, 0, 0x10, false) != NULL);
    b->record[0x198] = 0x50;
    CHECK(cok_combat_movement(&game, b, &m) && m == 0x40);
    uint8_t fastest;
    CHECK(cok_combat_fastest_enemy(&game, a, &fastest) && fastest == 0x20);
    /* A record on another side counts as the party's enemies' side 0. */
    b->record[0x18a] = 5;
    a->record[0x198] = 12;
    CHECK(cok_combat_fastest_enemy(&game, b, &fastest) && fastest == 12);
    b->record[0x189] = 0;
    b->record[0x18a] = 1;
    CHECK(cok_combat_fastest_enemy(&game, a, &fastest) && fastest == 0);
    CHECK(cok_combat_half_attacks(1, 0xff) == 0 && cok_combat_half_attacks(0, 0xff) == 0x7f);
    CHECK(cok_combat_half_attacks(3, 3) == 2 && cok_combat_half_attacks(2, 3) == 1);
}

/* Give c an item of type, readied, with count. */
static size_t give(cok_character *c, uint8_t type, uint8_t count)
{
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = type;
    item[0x34] = 1;
    item[0x39] = count;
    CHECK(cok_character_insert_item(c, c->item_count, item));
    return c->item_count;
}

static void test_weapon(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    uint8_t *r = a->record;
    r[0x10b] = 5;
    const cok_item_types *t = &game.item_types;
    /* A long bow (type 0x10? any missile with arrows): find one whose
     * flags take arrows (8 and 1). */
    uint8_t bow = 0;
    for (unsigned k = 0; k < 128 && bow == 0; ++k)
        if (t->type[k][12] > 1 && (t->type[k][14] & 9) == 9) bow = (uint8_t)k;
    CHECK(bow != 0);
    give(a, bow, 1);
    char error[300];
    CHECK(cok_character_stats(a, t, error, sizeof error));
    bool missile, has;
    size_t ammo;
    CHECK(cok_combat_missile_weapon(t, a, &missile) && missile);
    CHECK(cok_combat_ammunition(t, a, &ammo, &has) && !has && ammo == 0);
    /* Without arrows it attacks at +0x10b, halved. */
    CHECK(cok_combat_weapon_attacks(&game, a) && r[0x18f] == 2);
    /* With 3 arrows its rate (byte 5, at least 2), at most 3. */
    give(a, 0x1e, 3);
    CHECK(cok_character_stats(a, t, error, sizeof error));
    CHECK(cok_combat_ammunition(t, a, &ammo, &has) && has && ammo == 2);
    uint8_t rate = t->type[bow][5] < 2 ? 2 : t->type[bow][5];
    uint8_t n = (uint8_t)(rate >> 1);
    CHECK(cok_combat_weapon_attacks(&game, a) && r[0x18f] == (n < 3 ? n : 3));
    /* One arrow allows one; a count of 0 does not cap it. */
    a->items[1][0x39] = 1;
    CHECK(cok_combat_weapon_attacks(&game, a) && r[0x18f] == (n < 1 ? n : 1));
    a->items[1][0x39] = 0;
    CHECK(cok_combat_weapon_attacks(&game, a) && r[0x18f] == n);
    /* Having attacked, never more than before. */
    r[0x18f] = 0;
    a->combat->attacked = 1;
    CHECK(cok_combat_weapon_attacks(&game, a) && r[0x18f] == 0);
    /* An item type past ITEMS reads past the table: refused. */
    a->items[0][0x2e] = 200;
    CHECK(!cok_combat_missile_weapon(t, a, &missile));
    CHECK(!cok_combat_weapon_attacks(&game, a) && game.vm.status == COK_ECL_UNDEFINED);
    /* A crossbow-like type with flags exactly 0x0a has ammunition with
     * none: its rate, at least 2. */
    reset("");
    a = record('A', 0, 5, 5);
    r = a->record;
    r[0x10b] = 6;
    give(a, 0x1c, 1);
    CHECK(cok_character_stats(a, t, error, sizeof error));
    CHECK(cok_combat_ammunition(t, a, &ammo, &has) && has && ammo == 0);
    CHECK(cok_combat_weapon_attacks(&game, a) && r[0x18f] == 1);
    /* A hoopak (0x43, thrown, rate 0) is its own ammunition, at 2. */
    reset("");
    a = record('A', 0, 5, 5);
    r = a->record;
    r[0x10b] = 6;
    give(a, 0x43, 1);
    CHECK(cok_character_stats(a, t, error, sizeof error));
    CHECK(cok_combat_ammunition(t, a, &ammo, &has) && has && ammo == 1);
    CHECK(cok_combat_weapon_attacks(&game, a) && r[0x18f] == 1);
    /* A range of 1 is no missile weapon (no shipped type has one). */
    cok_item_types saved = game.item_types;
    game.item_types.type[0x43][12] = 1;
    CHECK(cok_combat_missile_weapon(t, a, &missile) && !missile);
    game.item_types = saved;
    /* Type 128 is the zeroed record after ITEMS; 129 is past it. */
    a->items[0][0x2e] = 128;
    CHECK(cok_combat_missile_weapon(t, a, &missile) && !missile);
    a->items[0][0x2e] = 129;
    CHECK(!cok_combat_missile_weapon(t, a, &missile));
    /* No weapon: no ammunition, unless... flags 0x0a need a weapon. */
    reset("");
    cok_character *b = record('B', 0, 5, 5);
    CHECK(cok_combat_ammunition(t, b, &ammo, &has) && !has);
    CHECK(cok_combat_missile_weapon(t, b, &missile) && !missile);
}

static void test_damage(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    cok_combat *c = &game.combat;
    c->sides[0] = 1;
    c->sides[1] = 1;
    a->combat->initiative = 4;
    /* 3 past the hit points: dying, three rounds counted. */
    CHECK(cok_combat_damage(c, a, 13, 5));
    CHECK(a->record[0x188] == 5 && a->combat->dying == 3 && a->record[0x189] == 0);
    CHECK(c->sides[0] == 0 && a->combat->initiative == 0);
    /* Hit again, it is counted out of its side again: the byte wraps. */
    CHECK(cok_combat_damage(c, a, 1, 5) && c->sides[0] == 0xff && a->record[0x188] == 5);
    /* Exactly its hit points: unconscious. Outside combat no count. */
    m->record[0x197] = 4;
    CHECK(cok_combat_damage(c, m, 4, 4) && m->record[0x188] == 4 && c->sides[1] == 1);
    m->record[0x188] = 1;
    m->record[0x197] = 4;
    CHECK(cok_combat_damage(c, m, 6, 5) && m->record[0x188] == 6 && c->sides[1] == 0);
    /* Running (3) and hurt counts as dropping. */
    m->record[0x188] = 3;
    m->record[0x197] = 9;
    m->record[0x189] = 1;
    CHECK(cok_combat_damage(c, m, 1, 5) && m->record[0x197] == 0 && m->record[0x189] == 0);
    /* Other sides count past the bytes: refused. */
    m->record[0x18a] = 2;
    CHECK(!cok_combat_damage(c, m, 50, 5));
    /* Ten past them: dead. */
    m->record[0x18a] = 1;
    m->record[0x188] = 0;
    m->record[0x197] = 5;
    CHECK(cok_combat_damage(c, m, 15, 5) && m->record[0x188] == 6);
    m->record[0x188] = 0;
    m->record[0x197] = 5;
    CHECK(cok_combat_damage(c, m, 14, 5) && m->record[0x188] == 5 && m->combat->dying == 9);
    /* Okay with hit points left. */
    a->record[0x188] = 0;
    a->record[0x197] = 9;
    CHECK(cok_combat_damage(c, a, 3, 5) && a->record[0x197] == 6 && a->record[0x188] == 0);
}

static void test_helpers(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 0, 6, 5), *m = record('M', 1, 9, 9);
    CHECK(cok_combat_opposite(a) == 1 && cok_combat_opposite(m) == 0);
    m->record[0x18a] = 7;
    CHECK(cok_combat_opposite(m) == 0);
    m->record[0x18a] = 1;
    CHECK(!cok_combat_helpless(a));
    CHECK(cok_character_add_effect(a, 0x1f, 0, 0, false) != NULL && cok_combat_helpless(a));
    /* End of turn. */
    b->combat->initiative = 5;
    b->combat->spell = 3;
    b->combat->guarding = 1;
    b->combat->movement = 9;
    b->combat->facing = 4;
    cok_combat_end_turn(b);
    CHECK(b->combat->initiative == 0 && b->combat->spell == 0 && b->combat->guarding == 0 &&
          b->combat->movement == 0 && b->combat->facing == 4);
    /* Auto: the computer controls it, and a target on its own side goes. */
    b->combat->target = a->record;
    cok_combat_auto(b);
    CHECK(b->record[0x18b] == 1 && b->combat->target == NULL);
    b->combat->target = m->record;
    cok_combat_auto(b);
    CHECK(b->combat->target == m->record);
    /* Bandage: the first dying party member, wherever it is. */
    b->record[0x188] = 5;
    b->combat->dying = 4;
    m->record[0x188] = 5;
    CHECK(cok_combat_bandage(&game, false) && b->record[0x188] == 5);
    a->record[0x188] = 5;
    a->combat->not_party = 1;
    CHECK(cok_combat_bandage(&game, true) && b->record[0x188] == 4 && b->combat->dying == 0);
    CHECK(a->record[0x188] == 5 && LOGGED("print: is bandaged;"));
    CHECK(!cok_combat_bandage(&game, false));
    /* Only the first of two. */
    cok_character *d1 = record('D', 0, 7, 7), *d2 = record('E', 0, 8, 8);
    d1->record[0x188] = d2->record[0x188] = 5;
    CHECK(cok_combat_bandage(&game, true) && d1->record[0x188] == 4 && d2->record[0x188] == 5);
    /* The effects that last through the battle go, the first of each id. */
    CHECK(cok_character_add_effect(b, 0x77, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(b, 0x77, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(b, 0x4d, 0, 0, false) != NULL);
    b->record[0xe7] = 0xb3;
    b->record[0x18a] = 1;
    CHECK(cok_combat_battle_only(&game, b));
    CHECK(cok_character_find_effect(b, 0x77) != NULL && b->record[0x18a] == 0);
    CHECK(cok_combat_battle_only(&game, b) && cok_character_find_effect(b, 0x77) == NULL);
}

static void test_leave(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 9, 9);
    CHECK(cok_character_add_effect(a, 0x33, 0, 0, false) != NULL);
    a->combat->initiative = 3;
    game.effects.in_battle = true;
    CHECK(cok_combat_leave(&game, a, 3, "Got Away"));
    CHECK(a->record[0x188] == 3 && a->record[0x197] == 10 && a->record[0x189] == 0);
    CHECK(game.combat.combatant[1].size == 0 && game.combat.occupant[5][5] == 0);
    CHECK(a->combat->initiative == 0 && cok_character_find_effect(a, 0x33) == NULL);
    CHECK(LOGGED("print: Got Away;"));
    /* Not able to act: nothing happens. */
    s.log[0] = '\0';
    CHECK(cok_combat_leave(&game, a, 4, "Surrenders") && a->record[0x188] == 3);
    CHECK(!LOGGED("Surrenders"));
    /* Any other status takes the hit points. */
    CHECK(cok_combat_leave(&game, m, 4, "Surrenders") && m->record[0x197] == 0);
    CHECK(m->record[0x188] == 4);
    /* A record that is not a combatant: its size would be the count. */
    reset("");
    cok_character *b = record('B', 0, 5, 5);
    game.combat.combatant[1].character = NULL;
    CHECK(!cok_combat_leave(&game, b, 3, "Got Away") && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_gods(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 9, 9), *n = record('N', 1, 9, 10);
    a->combat->initiative = 4;
    m->combat->initiative = 6;
    n->record[0x189] = 0;
    CHECK(cok_combat_gods(&game));
    CHECK(LOGGED("print: The Gods intervene!;"));
    CHECK(m->record[0x188] == 6 && m->record[0x189] == 0 && n->record[0x188] == 6);
    CHECK(a->record[0x188] == 0 && a->record[0x189] == 1);
    CHECK(game.combat.combatant[2].size == 0 && game.combat.combatant[1].size == 1);
    /* The occupants are not rebuilt. */
    CHECK(game.combat.occupant[9][9] == 2);
    CHECK(a->combat->initiative == 0 && m->combat->initiative == 0);
}

static void test_explode(void)
{
    reset("");
    game.effects.rolls.damage_type = 9;
    CHECK(cok_combat_explode(&game) && game.effects.rolls.damage_type == 0);
    CHECK(!game.combat.exploding_now);
    /* A NULL entry, which 3f44:1f97 never lists, is written through. The
     * explosions themselves are tested with the attacks (test_attack). */
    record('A', 0, 5, 5);
    game.combat.exploding[1] = NULL;
    game.combat.exploding_count = 1;
    CHECK(!cok_combat_explode(&game) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_lines(void)
{
    /* A straight step adds 2, a diagonal one 3. */
    cok_combat_line line = {.x0 = 0, .y0 = 0, .x1 = 3, .y1 = 0};
    cok_combat_line_start(&line);
    CHECK(cok_combat_line_step(&line) && line.x == 1 && line.length == 2 && line.direction == 2);
    line = (cok_combat_line){.x0 = 0, .y0 = 0, .x1 = 2, .y1 = 2};
    cok_combat_line_start(&line);
    CHECK(cok_combat_line_step(&line) && line.x == 1 && line.y == 1 && line.length == 3);
    CHECK(line.direction == 3);
    CHECK(cok_combat_line_step(&line) && !cok_combat_line_step(&line) && line.length == 6);
    CHECK(line.direction == 8);
    /* 2 across, 1 down: the err test steps down at once. */
    line = (cok_combat_line){.x0 = 0, .y0 = 0, .x1 = -2, .y1 = 1};
    cok_combat_line_start(&line);
    CHECK(cok_combat_line_step(&line) && line.x == -1 && line.y == 1 && line.direction == 5);
    CHECK(cok_combat_line_step(&line) && line.x == -2 && line.y == 1 && line.length == 5);
    /* Mostly down: the shorter axis steps once the err reaches dy. */
    line = (cok_combat_line){.x0 = 0, .y0 = 0, .x1 = 1, .y1 = 2};
    cok_combat_line_start(&line);
    CHECK(cok_combat_line_step(&line) && line.x == 1 && line.y == 1 && line.length == 3);
    CHECK(cok_combat_line_step(&line) && line.x == 1 && line.y == 2 && line.length == 5);
    line = (cok_combat_line){.x0 = 4, .y0 = 4, .x1 = 4, .y1 = 0};
    cok_combat_line_start(&line);
    CHECK(cok_combat_line_step(&line) && line.y == 3 && line.direction == 0);
}

static void test_sight(void)
{
    reset("");
    cok_combat *c = &game.combat;
    int8_t x1 = 10, y1 = 2;
    uint16_t range = 0xff;
    bool seen;
    CHECK(cok_combat_sight(c, 2, 2, &x1, &y1, &range, &seen) && seen && range == 16);
    /* A wall (blocking height 2) at 6, 2 stops it there. */
    c->cells[2][6] = 1;
    x1 = 10, y1 = 2, range = 0xff;
    CHECK(cok_combat_sight(c, 2, 2, &x1, &y1, &range, &seen) && !seen);
    CHECK(x1 == 6 && y1 == 2 && range == 8);
    /* From a table (eye height 2) a wall does not block. */
    c->cells[2][2] = 0x1a;
    x1 = 10, y1 = 2, range = 0xff;
    CHECK(cok_combat_sight(c, 2, 2, &x1, &y1, &range, &seen) && seen);
    c->cells[2][2] = COK_COMBAT_FLOOR;
    /* Nor with sight not blocked (map +6). */
    c->see_all = 1;
    x1 = 10, y1 = 2, range = 0xff;
    CHECK(cok_combat_sight(c, 2, 2, &x1, &y1, &range, &seen) && seen);
    c->see_all = 0;
    /* The range is cells: the length may reach 2 * range + 1. */
    x1 = 5, y1 = 2, range = 1;
    CHECK(cok_combat_sight(c, 2, 2, &x1, &y1, &range, &seen) && !seen && x1 == 4 && range == 4);
    x1 = 3, y1 = 2, range = 1;
    CHECK(cok_combat_sight(c, 2, 2, &x1, &y1, &range, &seen) && seen && range == 2);
    /* The start cell is tested: standing on a wall (eye 1, blocking 2)
     * sees nothing. */
    x1 = 9, y1 = 2, range = 0xff;
    CHECK(cok_combat_sight(c, 6, 2, &x1, &y1, &range, &seen) && !seen);
    CHECK(x1 == 6 && y1 == 2 && range == 0);
    /* Off the map: refused. */
    x1 = 60, y1 = 2, range = 0xff;
    CHECK(!cok_combat_sight(c, 48, 2, &x1, &y1, &range, &seen));
}

static void test_arc(void)
{
    bool in;
    /* Facing north (0) from 10, 10: the cell ahead and a cone above. */
    CHECK(cok_combat_in_arc(10, 10, 10, 9, 0, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 10, 10, 3, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 12, 7, 0, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 13, 8, 0, &in) && !in);
    CHECK(cok_combat_in_arc(10, 10, 10, 11, 0, &in) && !in);
    /* South (4) mirrors it. */
    CHECK(cok_combat_in_arc(10, 10, 12, 13, 4, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 12, 7, 4, &in) && !in);
    /* East (2) and west (6). */
    CHECK(cok_combat_in_arc(10, 10, 14, 11, 2, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 6, 11, 2, &in) && !in);
    CHECK(cok_combat_in_arc(10, 10, 6, 11, 6, &in) && in);
    /* The diagonals: north-east (1) takes the quarter up and right. */
    CHECK(cok_combat_in_arc(10, 10, 14, 6, 1, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 14, 9, 1, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 14, 10, 1, &in) && !in);
    CHECK(cok_combat_in_arc(10, 10, 14, 14, 3, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 6, 14, 5, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 6, 6, 7, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 6, 6, 3, &in) && !in);
    CHECK(cok_combat_in_arc(10, 10, 8, 9, 7, &in) && in);
    CHECK(cok_combat_in_arc(49, 24, 48, 24, 8, &in) && in);
    /* Any direction (8, or 0xff), but neither point off the map. */
    CHECK(cok_combat_in_arc(10, 10, 0, 24, 0xff, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 50, 10, 8, &in) && !in);
    CHECK(cok_combat_in_arc(10, 10, 10, 25, 8, &in) && !in);
    CHECK(cok_combat_in_arc(-1, 10, 10, 10, 8, &in) && !in);
    /* 9-0xfe: an uninitialized byte. */
    CHECK(!cok_combat_in_arc(10, 10, 12, 12, 9, &in));
}

static void test_list(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 12, 10);
    cok_character *m = record('M', 1, 11, 11), *n = record('N', 1, 14, 14);
    (void)b, (void)n;
    /* Around A, any way: itself at 0, M diagonal (3) to the south-east,
     * B two across (4) to the east, N four diagonal (12); each with the
     * first direction whose quarter holds it. */
    CHECK(cok_combat_list(c, 10, 10, 0xff, 0xff, 1));
    CHECK(c->listed_count == 4);
    CHECK(c->listed[1].index == 1 && c->listed[1].distance == 0 && c->listed[1].direction == 0);
    CHECK(c->listed[2].index == 3 && c->listed[2].distance == 3 && c->listed[2].direction == 3);
    CHECK(c->listed[3].index == 2 && c->listed[3].distance == 4 && c->listed[3].direction == 2);
    CHECK(c->listed[4].index == 4 && c->listed[4].distance == 12 && c->listed[4].direction == 3);
    /* Within 1 cell (a length of 3), and facing west (6) nothing but A. */
    CHECK(cok_combat_list(c, 10, 10, 1, 0xff, 1) && c->listed_count == 2);
    CHECK(cok_combat_list(c, 10, 10, 0xff, 6, 1) && c->listed_count == 1);
    CHECK(c->listed[1].direction == 6);
    /* The enemies of A within 1: M, and the list keeps only them. */
    uint8_t count;
    CHECK(cok_combat_enemies(&game, a, 1, &count) && count == 1 && c->enemies[1] == 3);
    CHECK(c->listed_count == 1 && c->listed[1].index == 3);
    CHECK(cok_combat_enemies(&game, a, 0xff, &count) && count == 2 && c->enemies[2] == 4);
    /* With effect 0x5b, the one who yelled goes first. */
    c->yelled = n->record;
    CHECK(cok_character_add_effect(a, 0x5b, 0, 0xff, false) != NULL);
    CHECK(cok_combat_enemies(&game, a, 0xff, &count) && count == 2);
    CHECK(c->enemies[1] == 4 && c->enemies[2] == 3);
    /* The distance in squares, with the list put back but for its count. */
    c->listed[3].index = 0x33;
    uint8_t distance;
    CHECK(cok_combat_distance(c, a->record, n->record, &distance) && distance == 6);
    CHECK(c->listed_count == 4 && c->listed[3].index == 0x33 && c->see_all == 0);
    CHECK(cok_combat_distance(c, a->record, m->record, &distance) && distance == 1);
    /* Directions. */
    uint8_t d;
    CHECK(cok_combat_direction(c, a, m, &d) && d == 3);
    CHECK(cok_combat_direction(c, a, b, &d) && d == 2);
    CHECK(cok_combat_direction(c, m, a, &d) && d == 7);
    CHECK(cok_combat_direction(c, a, a, &d) && d == 0);
    /* 12 across and 29 down: 0x26a * 12 / 256 is 28, below 29: south. */
    c->combatant[2].x = 0;
    c->combatant[2].y = 0;
    c->combatant[4].x = 12;
    c->combatant[4].y = 29;
    CHECK(cok_combat_direction(c, b, n, &d) && d == 4);
    c->combatant[2].x = 12;
    c->combatant[2].y = 10;
    c->combatant[4].x = 14;
    c->combatant[4].y = 14;
    /* Sight is not blocked for the distance: a wall between them. */
    c->cells[12][12] = 1;
    c->cells[13][13] = 1;
    CHECK(cok_combat_distance(c, a->record, n->record, &distance) && distance == 6);
    CHECK(cok_combat_list(c, 10, 10, 0xff, 0xff, 1) && c->listed_count == 3);
    c->cells[12][12] = c->cells[13][13] = COK_COMBAT_FLOOR;
    /* The sort: equal lengths go by direction, but an odd one stays
     * after an even one. */
    reset("");
    record('A', 0, 10, 10);
    record('B', 0, 14, 10); /* 8 to the east */
    record('C', 0, 10, 6);  /* 8 to the north */
    CHECK(cok_combat_list(c, 10, 10, 0xff, 0xff, 1) && c->listed_count == 3);
    CHECK(c->listed[2].index == 3 && c->listed[3].index == 2);
    reset("");
    record('A', 0, 10, 10);
    record('B', 0, 13, 10); /* 6 to the east */
    record('C', 0, 12, 8);  /* 6 north-east */
    CHECK(cok_combat_list(c, 10, 10, 0xff, 0xff, 1) && c->listed_count == 3);
    CHECK(c->listed[2].index == 2 && c->listed[2].direction == 2);
    CHECK(c->listed[3].index == 3 && c->listed[3].direction == 1);
    /* A size past the footprints: refused. */
    CHECK(!cok_combat_list(c, 10, 10, 0xff, 0xff, 9));
}

static void test_turn(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 30, 20);
    game.effects.in_battle = true;
    a->combat->hits = 2;
    a->combat->turning = 3;
    a->combat->guarding = 1;
    a->combat->initiative = 0x14;
    a->record[0x59] = 5;
    /* The player's turn: 0x14 becomes 0x13, it is selected, its actions
     * shown, its stats recomputed, and the turn ends. */
    a->record[0x18f] = 0x77;
    CHECK(cok_combat_turn(&game, a));
    CHECK(a->combat->hits == 0 && a->combat->turning == 0 && a->combat->guarding == 0);
    CHECK(LOGGED("turn: A (initiative 19);") && game.vm.character == a->record);
    CHECK(a->combat->initiative == 0 && game.combat.show_actions && game.selected == 1);
    /* The map's cursor is cleared after; the view scrolled to A, its
     * moves shown. */
    CHECK(game.combat.cursor == 0 && game.combat.cursor_size == 1);
    /* Its stats were recomputed: THAC0 from its base (+0x59). */
    CHECK(a->record[0x18c] == 5);
    /* A party member's moves are shown off the view too, which scrolls to
     * it; an enemy's off the view are not, and the view stays. */
    game.combat.view_x = 30;
    game.combat.view_y = 15;
    CHECK(cok_combat_scroll(&game.combat, 33, 18, 0xff) || true);
    a->combat->initiative = 1;
    CHECK(cok_combat_turn(&game, a) && game.combat.show_actions);
    CHECK(game.combat.view_x == 2 && game.combat.view_y == 2);
    /* A turn with no initiative clears the guard all the same. */
    a->combat->guarding = 1;
    CHECK(cok_combat_turn(&game, a) && a->combat->guarding == 0);
    /* The computer's: an enemy off the view does not show its actions. */
    m->combat->initiative = 2;
    m->record[0x18b] = 1;
    game.combat.view_x = game.combat.view_y = 0;
    CHECK(cok_combat_turn(&game, m) && !game.combat.show_actions);
    CHECK(game.combat.view_x == 0 && game.combat.view_y == 0);
    CHECK(LOGGED("turn: M (initiative 2);"));
    /* No initiative: only event 7. */
    s.log[0] = '\0';
    CHECK(cok_combat_turn(&game, a) && !LOGGED("turn:"));
    /* Held (0x33, 3f44:00f7): event 7 ends the turn, which goes no
     * further. */
    CHECK(cok_character_add_effect(a, 0x33, 0, 0, false) != NULL);
    a->combat->initiative = 3;
    a->combat->movement = 6;
    s.log[0] = '\0';
    CHECK(cok_combat_turn(&game, a) && !LOGGED("turn:"));
    CHECK(a->combat->initiative == 0 && a->combat->movement == 0);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x33));
    /* Event 0x0f runs at the start of the turn: gating (0x4c) at more
     * than half the hit points does nothing. */
    CHECK(cok_character_add_effect(a, 0x4c, 0, 0, false) != NULL);
    a->combat->initiative = 3;
    s.log[0] = '\0';
    CHECK(cok_combat_turn(&game, a));
    CHECK(LOGGED("turn: A (initiative 3);") && cok_character_find_effect(a, 0x4c) != NULL);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x4c));
    /* Event 0x15 runs only for one not casting: 0x23's d100 there. */
    CHECK(cok_character_add_effect(a, 0x23, 0, 0, false) != NULL);
    a->combat->initiative = 3;
    a->combat->spell = 4;
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_turn(&game, a) && game.vm.seed == seed);
    a->combat->initiative = 3;
    a->combat->spell = 0;
    CHECK(cok_combat_turn(&game, a) && game.vm.seed != seed);
    /* Charm taking hold at event 0x0f: the computer controls it (see
     * test_attack). */
    CHECK(cok_character_add_effect(a, 0x0b, 0, 0, false) != NULL);
    a->combat->initiative = 3;
    a->combat->spell = 0;
    s.log[0] = '\0';
    CHECK(cok_combat_turn(&game, a) && !LOGGED("unported") && a->record[0x18b] == 1);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x0b));
    a->record[0x18b] = 0;
    /* Effects that end in a battle are logged. */
    CHECK(cok_character_add_effect(a, 0x77, 0, 0, false) != NULL);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x77) && LOGGED("effect: A loses 0x77;"));
    CHECK(cok_character_add_effect(a, 0x33, 0, 0, false) != NULL);
    /* Outside a battle it fails. */
    game.effects.in_battle = false;
    CHECK(!cok_combat_turn(&game, a) && game.vm.status == COK_ECL_EFFECT_FAILED);
    /* The gods: the player's turn runs the Helm cheat, in a game started
     * with Helm; without, 432f:41e2 returns at once and the turn ends. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->initiative = 2;
    game.combat_stub = COK_COMBAT_GODS;
    game.helm = false;
    CHECK(cok_combat_turn(&game, a) && m->record[0x188] == 0);
    CHECK(strstr(s.log, "Gods") == NULL);
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->initiative = 2;
    game.combat_stub = COK_COMBAT_GODS;
    game.helm = true;
    CHECK(cok_combat_turn(&game, a) && m->record[0x188] == 6);
    CHECK(LOGGED("turn: A (initiative 2);print: The Gods intervene!;"));
    /* But not one that cannot act. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->initiative = 2;
    a->record[0x189] = 0;
    game.combat_stub = COK_COMBAT_GODS;
    CHECK(cok_combat_turn(&game, a) && m->record[0x188] == 0 && game.selected == 3);
    game.helm = false;
}

static void test_end_round(void)
{
    reset("YN");
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 0, 6, 5), *m = record('M', 1, 9, 9);
    b->record[0x188] = 5;
    b->record[0x189] = 0;
    b->combat->dying = 9;
    bool done = false;
    game.effects.rolls.round = 3;
    CHECK(cok_combat_end_round(&game, &done));
    /* A minute passes; B, dying past 9 rounds, is dead. */
    CHECK(game.vm.mem4b00[0xc7] == 1 && game.effects.rolls.round == 4);
    CHECK(b->record[0x188] == 6 && b->combat->dying == 10);
    CHECK(!done && game.combat.sides[0] == 1 && game.combat.sides[1] == 1);
    /* The enemies' health is worked out, and the screen positions. */
    CHECK(game.combat.enemy_health == 100);
    game.combat.combatant[3].screen_x = 77;
    m->record[0x197] = 5;
    CHECK(cok_combat_end_round(&game, &done) && game.combat.enemy_health == 50);
    CHECK(game.combat.combatant[3].screen_x == 9);
    CHECK(!LOGGED("Dying"));
    /* One dying: said. */
    b->record[0x188] = 5;
    b->combat->dying = 0;
    CHECK(cok_combat_end_round(&game, &done) && LOGGED("print: Your Teammate is Dying;"));
    CHECK(b->combat->dying == 1 && b->record[0x188] == 5);
    /* The round limit. */
    game.combat.round_limit = 6;
    CHECK(cok_combat_end_round(&game, &done) && done);
    /* The enemy gone and the party not: Yes fights on, No not. */
    done = false;
    game.combat.round_limit = 100;
    m->record[0x189] = 0;
    CHECK(cok_combat_end_round(&game, &done) && !done);
    CHECK(LOGGED("menu: Continue Battle:;choice: Y;"));
    CHECK(cok_combat_end_round(&game, &done) && done && LOGGED("choice: N;"));
    /* Both gone: no question. */
    s.log[0] = '\0';
    done = false;
    a->record[0x189] = 0;
    CHECK(cok_combat_end_round(&game, &done) && done && !LOGGED("Continue"));
    /* The party gone: no question. */
    s.log[0] = '\0';
    done = false;
    a->record[0x189] = 0;
    m->record[0x189] = 1;
    CHECK(cok_combat_end_round(&game, &done) && done && !LOGGED("Continue"));
    /* A damaging cloud (0x1d) under one that can act would hurt it,
     * which is not ported: logged. */
    a->record[0x189] = 1;
    game.combat.cells[5][5] = 0x1d;
    s.log[0] = '\0';
    CHECK(cok_combat_end_round(&game, &done) && LOGGED("unported: a damaging cloud's damage"));
    a->record[0x189] = 0;
    s.log[0] = '\0';
    CHECK(cok_combat_end_round(&game, &done) && !LOGGED("cloud"));
    /* A side past 1 is refused. */
    m->record[0x18a] = 3;
    CHECK(!cok_combat_end_round(&game, &done) && game.vm.status == COK_ECL_UNDEFINED);
}

/* A seed from which the first Random(range) + 1 is in lo..hi. */
static uint32_t seed_for(uint16_t range, unsigned lo, unsigned hi)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        unsigned d = cok_tp_random(&seed, range) + 1u;
        if (d >= lo && d <= hi) return start;
    }
}

/* A seed whose first two values, of range1 and range2, fall in lo1-hi1 and
 * lo2-hi2, counting from 1. */
static uint32_t seed_for2(uint16_t range1, unsigned lo1, unsigned hi1, uint16_t range2,
                          unsigned lo2, unsigned hi2)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        unsigned d = cok_tp_random(&seed, range1) + 1u, e = cok_tp_random(&seed, range2) + 1u;
        if (d >= lo1 && d <= hi1 && e >= lo2 && e <= hi2) return start;
    }
}

static bool run(cok_character *c, uint8_t ev)
{
    return cok_effects_dispatch(&game.effects, c, ev);
}

/* The handlers that need combat, ported for the battle. */
static void test_handlers(void)
{
    reset("");
    game.effects.in_battle = true;
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 20, 20);
    cok_character *m = record('M', 1, 11, 10), *n = record('N', 1, 30, 20);
    /* Held, asleep, paralysed: the turn ends, with either flag. */
    static const uint8_t held[] = {0x1b, 0x1f, 0x33, 0x34, 0x35};
    for (size_t k = 0; k < sizeof held; ++k) {
        a->combat->initiative = 4;
        a->combat->movement = 5;
        a->combat->guarding = 1;
        a->combat->spell = 2;
        CHECK(cok_character_add_effect(a, held[k], 0, 0, true) != NULL && run(a, 7));
        CHECK(a->combat->initiative == 0 && a->combat->movement == 0);
        CHECK(a->combat->guarding == 0 && a->combat->spell == 0);
        a->combat->initiative = 4;
        CHECK(cok_effects_remove(&game.effects, a, NULL, held[k]) && a->combat->initiative == 0);
    }
    /* Snakes (0x03): the value counts down by the round's attacks, then
     * goes; "is fighting with snakes" and the turn ends. */
    a->record[0x18f] = 1;
    a->record[0x190] = 1;
    cok_effect *snakes = cok_character_add_effect(a, 0x03, 0, 5, false);
    a->combat->initiative = 4;
    CHECK(run(a, 7) && snakes->value == 3 && a->combat->initiative == 0);
    CHECK(LOGGED("print: is fighting with snakes;"));
    CHECK(run(a, 7) && run(a, 7) && cok_character_find_effect(a, 0x03) == NULL);
    /* At the count exactly, it goes. */
    CHECK(cok_character_add_effect(a, 0x03, 0, 2, false) != NULL);
    CHECK(run(a, 7) && cok_character_find_effect(a, 0x03) == NULL);
    /* Asking for its handler on removal, it removes itself again and
     * again: refused. Above the round's attacks it counts down instead. */
    cok_effect *again = cok_character_add_effect(a, 0x03, 0, 1, true);
    CHECK(again != NULL);
    CHECK(!run(a, 7) && strstr(game.effects.error, "without end") != NULL);
    CHECK(a->effects == again && again->next == NULL && game.effects.removing == 0);
    again->value = 3;
    s.log[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x03) && a->effects == NULL);
    CHECK(LOGGED("print: is fighting with snakes;"));
    CHECK(game.effects.removing == 0);
    /* The 64th nested removal runs its handler; the 65th is refused. */
    cok_effect *silence = cok_character_add_effect(a, 0x15, 0, 0, true);
    game.effects.removing = 64;
    CHECK(silence != NULL && !cok_effects_remove(&game.effects, a, NULL, 0x15));
    CHECK(strstr(game.effects.error, "64 removals") != NULL);
    game.effects.removing = 63;
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x15) && a->effects == NULL);
    game.effects.removing = 0;
    /* Silence (0x15), shared: one that may use items is silenced. */
    CHECK(cok_character_add_effect(a, 0x15, 0, 0, false) != NULL);
    a->combat->may_use = a->combat->may_cast = 1;
    s.log[0] = '\0';
    CHECK(run(a, 0x0f) && a->combat->may_use == 0 && a->combat->may_cast == 0);
    CHECK(LOGGED("print: is silenced;"));
    s.log[0] = '\0';
    a->combat->may_cast = 1;
    CHECK(run(a, 0x0f) && a->combat->may_cast == 0 && !LOGGED("silenced"));
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x15));
    /* Coughing (0x1e): the stats recomputed, then the armour class from
     * behind 2 worse, or 0x32 when 0x34 or less, and the armour class. */
    CHECK(cok_character_add_effect(b, 0x1e, 0, 0, false) != NULL);
    b->combat->may_use = 1;
    char why[300];
    bool floor = false, worse = false;
    for (uint8_t base = 0x28; base <= 0x40; ++base) {
        b->record[0x113] = base;
        CHECK(cok_character_stats(b, &game.item_types, why, sizeof why));
        uint8_t behind = b->record[0x18e];
        b->record[0x18e] = 0x77;
        CHECK(run(b, 0x0f));
        uint8_t want = behind > 0x34 ? (uint8_t)(behind - 2) : 0x32;
        CHECK(b->record[0x18e] == want && b->record[0x18d] == want);
        floor = floor || behind == 0x34;
        worse = worse || behind == 0x35;
    }
    CHECK(floor && worse);
    CHECK(LOGGED("print: is coughing;") && b->combat->may_use == 0);
    CHECK(cok_effects_remove(&game.effects, b, NULL, 0x1e));
    /* Mirror Image (0x1c): a d(images + 1) above 1 takes one: no damage,
     * "lost an image", the value 1 less. */
    cok_effect *images = cok_character_add_effect(b, 0x1c, 0, 0x21, false);
    game.vm.seed = seed_for(3, 2, 3);
    game.effects.rolls.amount = 9;
    game.effects.rolls.pending = 4;
    s.log[0] = '\0';
    CHECK(run(b, 6) && images->value == 0x20 && game.effects.rolls.amount == 0);
    CHECK(game.effects.rolls.pending == 0 && LOGGED("print: lost an image;"));
    game.vm.seed = seed_for(3, 1, 1);
    CHECK(run(b, 6) && images->value == 0x20);
    /* Not for a spell that takes no image. */
    game.vm.seed = seed_for(3, 2, 3);
    game.effects.rolls.spell = 5;
    game.effects.rolls.images = 1;
    CHECK(run(b, 6) && images->value == 0x20);
    game.effects.rolls.spell = 0;
    game.effects.rolls.images = 0;
    CHECK(cok_effects_remove(&game.effects, b, NULL, 0x1c));
    /* Confusion (0x23): a d100, then a save of type 4 at -2. */
    b->record[0xd4] = 0xff; /* only a 20 saves */
    cok_effect *confused = cok_character_add_effect(b, 0x23, 0, 0, false);
    (void)confused;
    game.vm.seed = seed_for(100, 1, 10);
    b->combat->target = m->record;
    CHECK(run(b, 0x15) && cok_character_find_effect(b, 0x23) == NULL);
    CHECK(b->combat->forced == 1 && b->record[0x18b] == 1 && b->record[0xe7] == 0xb3);
    CHECK(b->combat->target == NULL && LOGGED("print: runs away;"));
    cok_effect *ran = cok_character_find_effect(b, 0x6f);
    CHECK(ran != NULL && ran->duration == 10 && ran->value == 0 && ran->on_remove);
    CHECK(cok_effects_remove(&game.effects, b, NULL, 0x6f));
    b->record[0x18b] = 0;
    CHECK(cok_character_add_effect(b, 0x23, 0, 0, false) != NULL);
    game.vm.seed = seed_for(100, 11, 11);
    b->combat->initiative = 3;
    CHECK(run(b, 0x15) && b->combat->initiative == 0 && LOGGED("print: is confused;"));
    game.vm.seed = seed_for(100, 61, 80);
    s.log[0] = '\0';
    CHECK(run(b, 0x15) && LOGGED("print: goes berserk;"));
    cok_effect *berserk = cok_character_find_effect(b, 0x4d);
    CHECK(berserk != NULL && berserk->duration == 1 && berserk->value == 0);
    /* Then 0x4d's handler, with no effect: it aims at the nearest other
     * and takes the side against it (see test_attack). */
    CHECK(b->combat->target == n->record && b->record[0x18a] == 0 && b->combat->may_cast == 0);
    CHECK(!LOGGED("unported"));
    CHECK(cok_effects_remove(&game.effects, b, NULL, 0x4d));
    game.vm.seed = seed_for(100, 81, 100);
    CHECK(run(b, 0x15) && LOGGED("print: is enraged;"));
    CHECK(cok_character_find_effect(b, 0x23) != NULL);
    /* The save at -2: 13 against 12 fails, 14 makes it. */
    b->record[0xd4] = 12;
    b->record[0x17c] = 0;
    game.vm.seed = seed_for2(100, 81, 100, 20, 13, 13);
    CHECK(run(b, 0x15) && cok_character_find_effect(b, 0x23) != NULL);
    game.vm.seed = seed_for2(100, 81, 100, 20, 14, 14);
    CHECK(run(b, 0x15) && cok_character_find_effect(b, 0x23) == NULL);
    CHECK(cok_character_add_effect(b, 0x23, 0, 0, false) != NULL);
    /* A throw of 0 saves but on a 1, and the effect goes. */
    b->record[0xd4] = 0;
    game.vm.seed = seed_for(100, 81, 100);
    CHECK(run(b, 0x15) && cok_character_find_effect(b, 0x23) == NULL);
    /* Spell turning (0x47): a d10, and kinds above 14 always turn back,
     * which is not ported. */
    CHECK(cok_character_add_effect(m, 0x47, 0, 0, false) != NULL);
    uint32_t seed = game.vm.seed = seed_for(10, 2, 10);
    game.effects.rolls.spell = 1;
    s.log[0] = '\0';
    CHECK(run(m, 9) && !LOGGED("unported"));
    cok_tp_random(&seed, 10);
    CHECK(game.vm.seed == seed);
    uint8_t spell = 0;
    for (unsigned k = 1; k < 0x6c && spell == 0; ++k) {
        uint8_t kind = 0;
        CHECK(cok_ds_byte((uint16_t)(0x31b9 + 16 * k), &kind));
        if (kind > 14) spell = (uint8_t)k;
    }
    CHECK(spell != 0);
    game.effects.rolls.spell = spell;
    CHECK(run(m, 9) && LOGGED("unported: effect 0x47 (3f44:23a4) on event 0x09;"));
    /* On a 1, a spell of kind 7 turns back; one of kind 8 does not. */
    uint8_t seven = 0, eight = 0;
    for (unsigned k = 1; k < 0x6c; ++k) {
        uint8_t kind = 0;
        CHECK(cok_ds_byte((uint16_t)(0x31b9 + 16 * k), &kind));
        if (kind == 7 && seven == 0) seven = (uint8_t)k;
        if (kind == 8 && eight == 0) eight = (uint8_t)k;
    }
    CHECK(seven != 0 && eight != 0);
    game.effects.rolls.spell = seven;
    game.vm.seed = seed_for(10, 1, 1);
    s.log[0] = '\0';
    CHECK(run(m, 9) && LOGGED("unported: effect 0x47"));
    game.effects.rolls.spell = eight;
    game.vm.seed = seed_for(10, 1, 1);
    s.log[0] = '\0';
    CHECK(run(m, 9) && !LOGGED("unported"));
    game.effects.rolls.spell = 0;
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x47));
    /* A terrible presence (0x48) terrifies an enemy within a cell that
     * fails its saving throw of type 4. */
    reset("");
    game.effects.in_battle = true;
    a = record('A', 0, 10, 10);
    b = record('B', 0, 12, 11);
    m = record('M', 1, 11, 10);
    n = record('N', 1, 12, 9);
    a->record[0xd4] = b->record[0xd4] = 0xff;
    CHECK(cok_character_add_effect(m, 0x48, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(b, 0x5c, 0, 0, false) != NULL);
    game.vm.seed = seed_for(20, 1, 19);
    a->combat->target = m->record;
    CHECK(run(m, 0x0f) && LOGGED("print: is terrified;"));
    cok_effect *terror = cok_character_find_effect(a, 0x6f);
    CHECK(terror != NULL && terror->value == 0 && terror->duration == 0);
    CHECK(a->combat->forced == 1 && a->record[0x18b] == 1 && a->combat->target == NULL);
    CHECK(cok_character_find_effect(b, 0x6f) == NULL && cok_character_find_effect(n, 0x6f) == NULL);
    /* One immune to it (0x3b cancels the effect pending) is unaffected and
     * keeps control. */
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x6f));
    a->record[0x18b] = 0;
    a->combat->forced = 0;
    a->combat->target = m->record;
    CHECK(cok_character_add_effect(a, 0x3b, 0, 0, false) != NULL);
    game.vm.seed = seed_for(20, 1, 19);
    s.log[0] = '\0';
    CHECK(run(m, 0x0f) && LOGGED("print: is Unaffected;"));
    CHECK(cok_character_find_effect(a, 0x6f) == NULL && a->record[0x18b] == 0);
    CHECK(a->combat->forced == 0 && a->combat->target == m->record);
    /* An evil stench (0x4f): who fails is affected, and 0x76 goes to the
     * holder, once for each. */
    reset("");
    game.effects.in_battle = true;
    a = record('A', 0, 10, 10);
    b = record('B', 0, 11, 11);
    m = record('M', 1, 11, 10);
    a->record[0xd4] = b->record[0xd4] = 0xff;
    CHECK(cok_character_add_effect(m, 0x4f, 0, 0, false) != NULL);
    game.vm.seed = 1;
    for (int k = 0; k < 2; ++k) {
        seed = game.vm.seed;
        unsigned first = cok_tp_random(&seed, 20) + 1u, second = cok_tp_random(&seed, 20) + 1u;
        if (first != 20 && second != 20) break;
        game.vm.seed = seed;
        k = -1;
    }
    s.log[0] = '\0';
    CHECK(run(m, 0x0f) && LOGGED("print: emits an evil stench;"));
    CHECK(cok_character_find_effect(a, 0x76) == NULL && cok_character_find_effect(b, 0x76) == NULL);
    unsigned count = 0;
    for (const cok_effect *e = m->effects; e != NULL; e = e->next) count += e->id == 0x76;
    CHECK(count == 2 && LOGGED("print: is affected;"));
    /* One that has 0x76 is not affected again. */
    CHECK(cok_character_add_effect(a, 0x76, 0, 0xff, false) != NULL);
    CHECK(cok_character_add_effect(b, 0x76, 0, 0xff, false) != NULL);
    game.vm.seed = 1;
    for (int k = 0; k < 2; ++k) {
        seed = game.vm.seed;
        unsigned first = cok_tp_random(&seed, 20) + 1u, second = cok_tp_random(&seed, 20) + 1u;
        if (first != 20 && second != 20) break;
        game.vm.seed = seed;
        k = -1;
    }
    s.log[0] = '\0';
    CHECK(run(m, 0x0f) && !LOGGED("is affected"));
    count = 0;
    for (const cok_effect *e = m->effects; e != NULL; e = e->next) count += e->id == 0x76;
    CHECK(count == 2);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x76));
    CHECK(cok_effects_remove(&game.effects, b, NULL, 0x76));
    /* 0x1a: against a target with +0x13f bit 7, the attack roll 1 better;
     * no die. */
    CHECK(cok_character_add_effect(a, 0x1a, 0, 0, false) != NULL);
    a->combat->target = m->record;
    m->record[0x13f] = 0x7f;
    game.effects.rolls.attack_roll = 10;
    game.effects.rolls.target = NULL;
    seed = game.vm.seed;
    CHECK(run(a, 0x0a) && game.effects.rolls.attack_roll == 10);
    CHECK(game.effects.rolls.target == m->record && game.vm.seed == seed);
    m->record[0x13f] = 0x80;
    CHECK(run(a, 0x0a) && game.effects.rolls.attack_roll == 11);
    a->combat->target = NULL;
    CHECK(!run(a, 0x0a) && strstr(game.effects.error, "0000:013f") != NULL);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x1a));
    m->record[0x13f] = 0;
    /* 0x4b: against a target with +0x140 bit 0, 3d12 + 4 and the
     * strength's bonus, the attack roll 2 better. */
    CHECK(cok_character_add_effect(a, 0x4b, 0, 0, false) != NULL);
    a->combat->target = m->record;
    game.effects.rolls.amount = 1;
    game.effects.rolls.attack_roll = 10;
    CHECK(run(a, 4) && game.effects.rolls.amount == 1 && game.effects.rolls.attack_roll == 10);
    CHECK(game.effects.rolls.target == m->record);
    m->record[0x140] = 1;
    a->record[0x114] = 1;
    a->record[0x11] = 18;
    a->record[0x1c] = 100; /* +6 damage */
    seed = game.vm.seed;
    unsigned d12 = cok_tp_random(&seed, 12) + 1u;
    CHECK(run(a, 4) && game.effects.rolls.amount == d12 * 3 + 4 + 6);
    CHECK(game.effects.rolls.attack_roll == 12);
    a->combat->target = NULL;
    CHECK(!run(a, 4) && strstr(game.effects.error, "0000:0140") != NULL);
    /* 0x29: an attacker with a weapon of no bonus more than a square off
     * misses: "Avoids it". */
    reset("");
    game.effects.in_battle = true;
    a = record('A', 0, 10, 10);
    m = record('M', 1, 13, 10);
    CHECK(cok_character_add_effect(m, 0x29, 0, 0, false) != NULL);
    give(a, 0x10, 0);
    CHECK(cok_character_stats(a, &game.item_types, why, sizeof why));
    game.vm.character = a->record;
    game.effects.rolls.amount = 7;
    game.effects.rolls.attack_roll = 9;
    game.effects.rolls.hits[0] = 2;
    CHECK(run(m, 5) && LOGGED("print: Avoids it;") && game.effects.rolls.amount == 0);
    CHECK(game.effects.rolls.attack_roll == 0xff && game.effects.rolls.hits[0] == 1);
    game.effects.rolls.amount = 7;
    game.combat.combatant[2].x = 11;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(run(m, 5) && game.effects.rolls.amount == 7);
    game.combat.combatant[2].x = 13;
    CHECK(cok_combat_occupy(&game.combat));
    a->items[0][0x32] = 1;
    CHECK(run(m, 5) && game.effects.rolls.amount == 7);
    /* Outside a battle the held handler writes through a stale or NULL
     * combat record: refused. */
    game.effects.in_battle = false;
    CHECK(cok_character_add_effect(m, 0x33, 0, 0, false) != NULL && !run(m, 7));
    /* Nor is the map there to measure or list on. */
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x33));
    a->items[0][0x32] = 0;
    CHECK(!run(m, 5) && strstr(game.effects.error, "6346:2888") != NULL);
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x29));
    CHECK(cok_character_add_effect(m, 0x4f, 0, 0, false) != NULL);
    game.effects.in_battle = true;
    game.combat.active = false;
    CHECK(!run(m, 0x0f) && strstr(game.effects.error, "no combat map") != NULL);
}

static void test_shared(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 0, 20, 20), *c = record('C', 0, 6, 6);
    /* Prayer (0x31) is shared within 6 cells of its holder in combat;
     * protection from evil (0x2d) within 1. B holds both. */
    CHECK(cok_character_add_effect(b, 0x31, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(b, 0x2d, 0, 0, false) != NULL);
    game.vm.character = a->record;
    game.combat.listed_count = 9;
    game.combat.listed[1] = (cok_combat_listing){0x55, 0x66, 0x77};
    game.effects.rolls.save_roll = 0;
    /* Event 0x0c runs 0x31 (+1 to a save) and 0x2d: out of range, none. */
    CHECK(cok_effects_dispatch(&game.effects, a, 0x0c) && game.effects.rolls.save_roll == 0);
    /* The list's count stays as the last listing left it. */
    CHECK(game.combat.listed_count == 1);
    /* The list's entries are put back. */
    CHECK(game.combat.listed[1].index == 0x55 && game.combat.listed[1].distance == 0x66);
    /* Within 6 for Prayer, a length of 13: four cells diagonally is 12;
     * five, 15, is too far. */
    game.combat.combatant[2].x = 10;
    game.combat.combatant[2].y = 10;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_effects_dispatch(&game.effects, a, 0x0c) && game.effects.rolls.save_roll == 0);
    game.combat.combatant[2].x = 9;
    game.combat.combatant[2].y = 9;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_effects_dispatch(&game.effects, a, 0x0c) && game.effects.rolls.save_roll == 1);
    /* A second holder in range counts when the first is not. */
    cok_character *d = record('D', 0, 30, 3);
    CHECK(cok_character_add_effect(d, 0x31, 0, 0, false) != NULL);
    game.combat.combatant[2].x = 40;
    CHECK(cok_combat_occupy(&game.combat));
    game.combat.combatant[4].x = 6;
    game.combat.combatant[4].y = 5;
    CHECK(cok_combat_occupy(&game.combat));
    game.effects.rolls.save_roll = 0;
    CHECK(cok_effects_dispatch(&game.effects, a, 0x0c) && game.effects.rolls.save_roll == 1);
    /* A holder off the map (size 0) lists none. */
    game.combat.combatant[4].size = 0;
    CHECK(cok_combat_occupy(&game.combat));
    game.effects.rolls.save_roll = 0;
    CHECK(cok_effects_dispatch(&game.effects, a, 0x0c) && game.effects.rolls.save_roll == 0);
    /* Outside combat any holder counts. */
    game.vm.mode = 4;
    game.combat.combatant[2].x = 40;
    game.effects.rolls.save_roll = 0;
    CHECK(cok_effects_dispatch(&game.effects, c, 0x0c) && game.effects.rolls.save_roll != 0);
}

/* Two party records, A and B, and COMBAT with two goblins loaded. */
static cok_ecl_status fight(const char *keys, cok_combat_stub stub, void (*before)(void))
{
    reset(keys);
    game.vm.mode = 4;
    game.vm.mem7c00[0x2c1] = 2;
    for (int i = 0; i < 2; ++i) {
        cok_character *c = calloc(1, sizeof *c);
        uint8_t *r = c->record;
        r[0] = 1;
        r[1] = (uint8_t)('A' + i);
        r[0x10] = r[0x11] = r[0x16] = r[0x17] = 12;
        r[0x62] = r[0x197] = 10;
        r[0xcf] = 1;
        r[0x189] = 1;
        r[0x198] = 12;
        r[0xf9] = 1;
        CHECK(cok_party_append(&game.party, c));
    }
    game.vm.mem7c00[0x33e] = 2;
    game.combat_stub = stub;
    if (before != NULL) before();
    static const uint8_t code[] = {COK_ECL_LOAD_MONSTER, 0, 9, 0, 2, 0, 9, COK_ECL_COMBAT};
    memset(game.vm.code, 0, sizeof game.vm.code);
    memcpy(game.vm.code, code, sizeof code);
    game.vm.size = sizeof code + 1;
    game.vm.depth = 0;
    s.battles = 0;
    s.targets_in_battle = false;
    return cok_ecl_run(&game.vm, COK_ECL_BASE);
}

static void surprised(void)
{
    game.vm.mem7c00[0x2cb] = 1;
    game.effects.rolls.damage_type = 9;
}

static void held(void)
{
    CHECK(cok_character_add_effect(game.party.members[0], 0x33, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(game.party.members[1], 0x4c, 0, 0, false) != NULL);
}

static void charmed(void)
{
    CHECK(cok_character_add_effect(game.party.members[1], 0x0b, 0, 0x20, false) != NULL);
}

/* The battle alone, after the goblins are loaded, ending before the end
 * of combat. */
static void charmed_battle(void)
{
    charmed();
    static const uint8_t code[] = {COK_ECL_LOAD_MONSTER, 0, 9, 0, 2, 0, 9, COK_ECL_EXIT};
    memcpy(game.vm.code, code, sizeof code);
    game.vm.size = sizeof code + 1;
    game.vm.depth = 0;
    CHECK(cok_ecl_run(&game.vm, COK_ECL_BASE) == COK_ECL_OK);
    CHECK(cok_combat_battle(&game));
    game.vm.abort = true; /* the COMBAT that follows is not run */
}

/* B, against the party, frightens A, of level 9, whose saving throw of
 * type 4 never fails but on a 1. */
static void fearful(void)
{
    uint8_t *a = game.party.members[0]->record, *b = game.party.members[1]->record;
    a[0xd6] = 9;
    a[0xd4] = 0;
    b[0x18a] = 1;
    game.vm.seed = 3;
    CHECK(cok_character_add_effect(game.party.members[1], 0x52, 0, 0, false) != NULL);
}

static void fearless(void)
{
    uint8_t *a = game.party.members[0]->record, *b = game.party.members[1]->record;
    a[0xd6] = 9;
    a[0xd4] = 0;
    b[0x18a] = 1;
    game.vm.seed = 3;
}

static void test_battle(void)
{
    /* COMBAT with no --combat: rounds until the limit, every turn passing,
     * the order of each round logged. A, surprised with B, has its first
     * turn in round 2; the damage type is cleared after every turn. */
    CHECK(fight("\rE", COK_COMBAT_UNPORTED, surprised) == COK_ECL_OK);
    CHECK(LOGGED("round: 1: GOBLIN ") && LOGGED("A 0") && LOGGED("round: 15: ") &&
          !LOGGED("round: 16: "));
    const char *two = strstr(s.log, "round: 2: ");
    CHECK(two != NULL && strstr(two, "turn: A (initiative ") != NULL);
    CHECK(strstr(s.log, "turn: A (initiative ") > two);
    CHECK(LOGGED("combat: removed 2 GOBLIN; 0 dropped;"));
    CHECK(!game.combat.active && !game.combat_targets && !game.effects.in_battle);
    CHECK(s.battles == 1 && s.targets_in_battle && game.effects.rolls.damage_type == 0);
    CHECK(game.effects.rolls.round == 15);
    /* Every die of the fifteen rounds, from seed 1: initiatives, the
     * order's ties and each turn's rolls. */
    CHECK(game.vm.seed == 0x329cf077);
    /* Fifteen minutes passed. */
    CHECK(game.vm.mem4b00[0xc7] == 5 && game.vm.mem4b00[0xc8] == 1);
    /* A held (0x33) has no turn; one gating (0x4c) at its full hit points
     * fights on. */
    CHECK(fight("\rE", COK_COMBAT_UNPORTED, held) == COK_ECL_OK);
    CHECK(LOGGED("round: 15: ") && !LOGGED("turn: A ") && LOGGED("turn: B "));
    CHECK(!LOGGED("unported:") && !LOGGED("gates in"));
    /* With won, the sides are counted again before the battle's end: a
     * charmed party member does not run, the enemy all down. */
    CHECK(fight("\rE", COK_COMBAT_WON, charmed) == COK_ECL_OK);
    CHECK(LOGGED("combat: won;") && !LOGGED("round: "));
    CHECK(LOGGED("print: The party has won.;"));
    CHECK(fight("", COK_COMBAT_WON, charmed_battle) == COK_ECL_OK);
    CHECK(game.party.members[1]->record[0x188] == 0 && game.combat.sides[1] == 0);
    /* A dragon's fear runs at setup (event 8) and again (event 0x18): two
     * saving throws. */
    CHECK(fight("\rE", COK_COMBAT_WON, fearless) == COK_ECL_OK);
    uint32_t seed = game.vm.seed;
    CHECK(fight("\rE", COK_COMBAT_WON, fearful) == COK_ECL_OK && !LOGGED("is afraid"));
    cok_tp_random(&seed, 20);
    cok_tp_random(&seed, 20);
    CHECK(game.vm.seed == seed);
    /* The battle's end gives spells their targets outside combat again. */
    reset("");
    game.combat_targets = true;
    CHECK(cok_combat_end(&game) && !game.combat_targets);
    /* Casting picks targets with the combat routine in a battle, which is
     * not ported. */
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    a->record[0x1e] = 3;
    a->record[0xf9] = 1;
    game.vm.character = a->record;
    game.vm.mode = 5;
    game.combat_targets = true;
    bool done = false;
    cok_cast_spell(&game, 3, 0, 1, &done);
    CHECK(game.vm.status == COK_ECL_EFFECT_FAILED && LOGGED("432f:2337"));
}

/* The combat screen draws with a battle's tiles (6d21:002c): a setup
 * loads them, and the fixtures after keep them. */
static void load_screen(void)
{
    reset("");
    record('A', 0, 0, 0);
    record('M', 1, 0, 0);
    game.vm.mode = 4;
    game.vm.mem7c00[0x33e] = 2;
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(cok_combat_setup(&game));
    CHECK(cok_combat_end(&game));
}

int main(void)
{
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .unported = unported, .battlefield = battlefield,
                                 .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    load_screen();
    test_tables();
    test_turn_order();
    test_round_start();
    test_movement();
    test_weapon();
    test_damage();
    test_helpers();
    test_leave();
    test_gods();
    test_explode();
    test_lines();
    test_sight();
    test_arc();
    test_list();
    test_turn();
    test_end_round();
    test_shared();
    test_handlers();
    test_battle();
    cok_adventure_close(&game);
    puts("round tests passed");
    return 0;
}
