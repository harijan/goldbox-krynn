#include "ai.h"

#include "adventure.h"
#include "attack.h"
#include "monster.h"
#include "round.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    const char *keys;
    size_t at, length;
    bool pending; /* A key waits (1614:03c2) while keys are left. */
    char log[1 << 17];
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    return (unsigned char)s->keys[s->at++];
}

static script s;
static cok_adventure game;

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g;
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "%s: %s;", kind, text);
}

static bool key_pending(cok_adventure *g, void *context)
{
    (void)g;
    script *sc = context;
    return sc->pending && sc->at < sc->length;
}

#define LOGGED(text) (strstr(s.log, text) != NULL)

static void reset(const char *keys)
{
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    free(game.lost_weapons);
    game.lost_weapons = NULL;
    game.lost_weapon_count = 0;
    game.vm.character = game.vm.saved_character = NULL;
    game.vm.restore_character = false;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    memset(&game.effects.rolls, 0, sizeof game.effects.rolls);
    game.vm.mode = 5;
    game.vm.file = 1;
    game.vm.seed = 1;
    game.vm.direction = 0;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.input_ended = false;
    game.combat_stub = COK_COMBAT_UNPORTED;
    game.effects.in_battle = true;
    game.speed = 0;
    game.helm = false;
    game.undead = 0;
    cok_combat *c = &game.combat;
    memset(c->cells, COK_COMBAT_FLOOR, sizeof c->cells);
    memset(c->combatant, 0, sizeof c->combatant);
    memset(c->turn, 0, sizeof c->turn);
    memset(c->listed, 0, sizeof c->listed);
    memset(c->enemies, 0, sizeof c->enemies);
    memset(c->exploding, 0, sizeof c->exploding);
    memset(c->swings, 0, sizeof c->swings);
    memset(c->body, 0, sizeof c->body);
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
    c->exploding_now = false;
    c->yelled = NULL;
    c->show_actions = false;
    c->panel = false;
    c->active = true;
    c->magic_on = false;
    c->in_reach = false;
    c->last_step = c->stuck = c->stuck_more = 0;
    c->enemy_health = 0;
    s.keys = keys;
    s.at = 0;
    s.length = strlen(keys);
    s.pending = false;
    s.log[0] = '\0';
}

/* A record named by a letter, a fighter of level 1, one cell, abilities
 * 12, 10 hit points, on side, armour class 10, THAC0 20, one attack a
 * round of 1d2, with a zeroed combat record and initiative 5, a combatant
 * at x, y, its stats worked out. */
static cok_character *record(char name, uint8_t side, int8_t x, int8_t y)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = 1;
    r[1] = (uint8_t)name;
    for (int k = 0; k < 6; ++k) r[0x10 + 2 * k] = r[0x11 + 2 * k] = 12;
    r[0x59] = 40;
    r[0x62] = r[0x197] = r[0x11b] = 10;
    r[0x5a] = 6;
    r[0xcf] = 1;
    r[0xd5] = 12;
    r[0xd6] = 1;
    r[0xfb] = 1;
    r[0x11a] = 8; /* a fighter's items (66c2:0433) */
    r[0x10b] = 2;
    r[0x10d] = 1;
    r[0x10f] = 2;
    r[0x113] = 50;
    r[0x189] = 1;
    r[0x18a] = side;
    r[0x18b] = 1;
    if (side != 0) r[0xe7] = 0x80;
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL && cok_party_append(&game.party, c));
    c->combat->not_party = side;
    c->combat->initiative = 5;
    c->combat->may_use = c->combat->may_cast = 1;
    char why[300];
    CHECK(cok_character_stats(c, &game.item_types, why, sizeof why));
    r[0x18f] = 1; /* this round's attacks */
    r[0x190] = 0;
    c->combat->attack_slot = 2;
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
    k->sides[side] = (uint8_t)(k->sides[side] + 1);
    return c;
}

/* Give c an item of type, readied if ready, with bonus (+0x32). */
static size_t give(cok_character *c, uint8_t type, bool ready, int8_t bonus)
{
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = type;
    item[0x31] = type;
    item[0x32] = (uint8_t)bonus;
    item[0x34] = ready;
    CHECK(cok_character_insert_item(c, c->item_count, item));
    char why[300];
    CHECK(cok_character_stats(c, &game.item_types, why, sizeof why));
    return c->item_count;
}

/* A seed whose first values, of the ranges given, are in the bounds given:
 * n triples of range, lo and hi. */
static uint32_t seed_seq(unsigned n, const unsigned *triples)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        bool ok = true;
        for (unsigned i = 0; i < n && ok; ++i) {
            unsigned d = cok_tp_random(&seed, (uint16_t)triples[3 * i]) + 1u;
            ok = d >= triples[3 * i + 1] && d <= triples[3 * i + 2];
        }
        if (ok) return start;
    }
}

/* The seed after n rolls of the ranges given from start. */
static uint32_t after(uint32_t start, unsigned n, const unsigned *ranges)
{
    for (unsigned i = 0; i < n; ++i) cok_tp_random(&start, (uint16_t)ranges[i]);
    return start;
}

static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("ai: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    for (size_t i = 0; i < cok_ai_table_count; ++i) {
        const cok_ds_table *t = &cok_ai_tables[i];
        for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    }
    for (size_t i = 0; i < cok_attack_table_count; ++i) {
        const cok_ds_table *t = &cok_attack_tables[i];
        for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    }
}

static void test_pattern(void)
{
    reset("");
    cok_character *a = record('A', 1, 5, 5);
    cok_combat_record *cr = a->combat;
    /* A row of 1-4 stays unless its d4 is 1: one roll. */
    cr->pattern = 2;
    game.vm.seed = seed_seq(1, (const unsigned[]){4, 2, 4});
    uint32_t start = game.vm.seed;
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 2);
    CHECK(game.vm.seed == after(start, 1, (const unsigned[]){4}));
    cr->pattern = 4;
    game.vm.seed = seed_seq(1, (const unsigned[]){4, 2, 4});
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 4);
    /* A d4 of 1, then a d8 of 8: 4 + d2. */
    cr->pattern = 1;
    game.vm.seed = seed_seq(3, (const unsigned[]){4, 1, 1, 8, 8, 8, 2, 2, 2});
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 6);
    cr->pattern = 3;
    game.vm.seed = seed_seq(3, (const unsigned[]){4, 1, 1, 8, 8, 8, 2, 1, 1});
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 5);
    /* Else a d4. */
    cr->pattern = 3;
    game.vm.seed = seed_seq(3, (const unsigned[]){4, 1, 1, 8, 1, 7, 4, 3, 3});
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 3);
    /* Rows 0, 5 and 6 never stay: no d4 first. */
    cr->pattern = 5;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 1, 7, 4, 2, 2});
    start = game.vm.seed;
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 2);
    CHECK(game.vm.seed == after(start, 2, (const unsigned[]){8, 4}));
    cr->pattern = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 8, 8, 2, 1, 1});
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 5);
    cr->pattern = 6;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 8, 8, 2, 2, 2});
    CHECK(cok_ai_pattern(&game, a) && cr->pattern == 6);
}

static void test_keys(void)
{
    reset("2 -");
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 0, 6, 5);
    cok_character *m = record('M', 1, 8, 8);
    record('N', 1, 9, 8)->record[0xe7] = 0x80;
    game.vm.character = a->record;
    bool back;
    /* No key waiting: nothing read. */
    CHECK(cok_ai_keys(&game, a, &back) && !back && s.at == 0);
    s.pending = true;
    /* '2' toggles Magic On, and again Off. */
    CHECK(cok_ai_keys(&game, a, &back) && !back && game.combat.magic_on);
    CHECK(LOGGED("print: Magic On;"));
    /* Space: the party back to the player but one of status 1; a monster
     * whose morale is below 0x80 too. */
    b->record[0x188] = 1;
    m->record[0xe7] = 0x7f;
    CHECK(cok_ai_keys(&game, a, &back) && back && a->combat->initiative == 0x14);
    CHECK(a->record[0x18b] == 0 && b->record[0x18b] == 1 && m->record[0x18b] == 0);
    CHECK(game.party.members[3]->record[0x18b] == 1);
    /* '-' is the Helm cheat, only for a game started with it. */
    CHECK(cok_ai_keys(&game, b, &back) && !back && !LOGGED("Gods"));
    s.at = 2;
    game.helm = true;
    CHECK(cok_ai_keys(&game, b, &back) && !back && LOGGED("print: The Gods intervene!;"));
    CHECK(m->record[0x188] == 6 && m->record[0x189] == 0);
    /* A 0 is followed by the scan code: Alt-M. */
    reset("\0002");
    s.length = 2;
    s.pending = true;
    a = record('A', 0, 5, 5);
    CHECK(cok_ai_keys(&game, a, &back) && !back && game.combat.magic_on && s.at == 2);
    CHECK(cok_ai_keys(&game, a, &back) && !back && game.combat.magic_on);
}

static void test_morale(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    cok_combat_record *cr = m->combat;
    game.vm.character = m->record;
    bool over;
    /* Made to flee: "is forced to flee". */
    cr->forced = 1;
    CHECK(cok_ai_morale(&game, m, &over) && !over && cr->fleeing == 1);
    CHECK(LOGGED("print: M;print: is forced to flee;"));
    cr->forced = 0;
    /* The party's never test it, and are not fleeing. */
    a->combat->fleeing = 1;
    CHECK(cok_ai_morale(&game, a, &over) && !over && a->combat->fleeing == 0);
    /* Morale 2 * (+0xe7 & 0x7f) against 100 less the hit points' share. */
    m->record[0xe7] = 0x80 + 15;
    m->record[0x197] = 7;
    CHECK(cok_ai_morale(&game, m, &over) && !over && cr->fleeing == 0);
    CHECK(game.effects.rolls.morale == 30);
    /* At 6 of 10 it fails, then the enemies' health, 100 less var 0x7ec6,
     * holds on the monsters' side. */
    m->record[0x197] = 6;
    game.combat.enemy_health = 80;
    game.vm.mem7c00[0x2c6] = 20;
    CHECK(cok_ai_morale(&game, m, &over) && !over && cr->fleeing == 0);
    CHECK(game.effects.rolls.morale == 80);
    /* Below that, with the enemy no faster, it flees, losing 0x4a and
     * 0x4b. */
    game.combat.enemy_health = 79;
    CHECK(cok_character_add_effect(m, 0x4a, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(m, 0x4b, 0, 0, false) != NULL);
    CHECK(cok_ai_morale(&game, m, &over) && !over && cr->fleeing == 1);
    CHECK(cok_character_find_effect(m, 0x4a) == NULL && cok_character_find_effect(m, 0x4b) == NULL);
    /* Above 0x66 the morale is 0, which fails: an enemy faster makes one
     * of intelligence above 5 surrender. */
    m->record[0xe7] = 0x80 + 0x34;
    m->record[0x197] = 10;
    game.combat.enemy_health = 99;
    game.vm.mem7c00[0x2c6] = 0;
    a->record[0x198] = 13; /* 13 against its 12 */
    m->record[0x13] = 5;
    CHECK(cok_ai_morale(&game, m, &over) && !over && cr->fleeing == 0 && m->record[0x189] == 1);
    m->record[0x13] = 6;
    CHECK(cok_ai_morale(&game, m, &over) && over && m->record[0x189] == 0);
    CHECK(m->record[0x188] == 4 && m->record[0x197] == 0 && cr->initiative == 0);
    CHECK(LOGGED("print: M;print: Surrenders;"));
    /* An ally of the party passes the first test or flees: the second is
     * the monsters'. */
    reset("");
    record('A', 1, 5, 5)->record[0x198] = 2;
    cok_character *n = record('N', 0, 6, 5);
    n->record[0xe7] = 0x80 + 40;
    n->record[0x197] = 1;
    game.combat.enemy_health = 100;
    CHECK(cok_ai_morale(&game, n, &over) && !over && n->combat->fleeing == 1);
    n->combat->fleeing = 0;
    n->record[0x197] = 9;
    CHECK(cok_ai_morale(&game, n, &over) && !over && n->combat->fleeing == 0);
    /* Hit points above the most give a share below 0. */
    n->record[0x197] = 50;
    n->record[0xe7] = 0x80;
    CHECK(cok_ai_morale(&game, n, &over) && n->combat->fleeing == 1);
    n->record[0xe7] = 0x81;
    CHECK(cok_ai_morale(&game, n, &over) && n->combat->fleeing == 0);
    /* No most hit points divides by zero. */
    n->record[0x62] = 0;
    CHECK(!cok_ai_morale(&game, n, &over) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_weapon_score(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    size_t sword = give(a, 0x12, false, 0), bow = give(a, 0x16, false, 0);
    size_t club = give(a, 0x03, false, 2), water = give(a, 0x36, false, 0);
    size_t two = give(a, 0x10, false, -1);
    uint8_t score;
    /* 1d8, one hand: 8 + 3. */
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 11);
    /* 1d6, a missile weapon of rate 4 (+6), two hands. */
    CHECK(cok_ai_weapon_score(&game, a, bow, &score) && score == 12);
    /* 1d6 + 8 * 2, one hand. */
    CHECK(cok_ai_weapon_score(&game, a, club, &score) && score == 25);
    /* Holy water: 1d1, bonus for small -1, thrown, rate 2 (+2), no hands;
     * 8 against undead. */
    CHECK(cok_ai_weapon_score(&game, a, water, &score) && score == 6);
    a->combat->target = m->record;
    m->record[0xda] = 1;
    CHECK(cok_ai_weapon_score(&game, a, water, &score) && score == 13);
    m->record[0xda] = 0x80;
    CHECK(cok_ai_weapon_score(&game, a, water, &score) && score == 6);
    /* 2d4, two hands, a negative bonus counts nothing. */
    CHECK(cok_ai_weapon_score(&game, a, two, &score) && score == 8);
    /* Too many hands held, an alignment item of another's, item 0x53,
     * cursed: 0. */
    a->record[0x17b] = 2;
    CHECK(cok_ai_weapon_score(&game, a, two, &score) && score == 0);
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 11);
    a->record[0x17b] = 3;
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 0);
    a->record[0x17b] = 0;
    a->items[sword - 1][0x3e] = 0x84;
    a->items[sword - 1][0x3d] = 0x12;
    a->record[0x10a] = 2;
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 11);
    a->record[0x10a] = 3;
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 0);
    a->items[sword - 1][0x3e] = 0x83;
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 11);
    a->items[sword - 1][0x3d] = 0x53;
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 0);
    a->items[sword - 1][0x3d] = 0;
    a->items[sword - 1][0x36] = 1;
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == 0);
    /* A byte: 1d8 + 8 * 0x7f. */
    a->items[sword - 1][0x36] = 0;
    a->items[sword - 1][0x32] = 0x7f;
    CHECK(cok_ai_weapon_score(&game, a, sword, &score) && score == (uint8_t)(11 + 8 * 0x7f));
    a->items[sword - 1][0x2e] = 0x81;
    CHECK(!cok_ai_weapon_score(&game, a, sword, &score) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_choose_weapon(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    /* The best for melee readied, the one readied put away. */
    size_t dagger = give(a, 0x04, true, 0), sword = give(a, 0x12, false, 0);
    CHECK(a->slots[0] == dagger);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == sword);
    CHECK(a->items[dagger - 1][0x34] == 0 && a->record[0x17b] == 1);
    /* Not twice. */
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == sword);
    /* A bow with arrows beats half the sword: readied, with no enemy a
     * square away. */
    size_t bow = give(a, 0x16, false, 0);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == sword);
    size_t arrows = give(a, 0x1e, true, 0);
    CHECK(a->slots[11] == arrows);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == bow && a->record[0x17b] == 2);
    /* An enemy a square away: the sword again. */
    record('N', 1, 6, 5);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == sword);
    /* A cursed weapon stays. */
    a->items[sword - 1][0x36] = 1;
    a->items[bow - 1][0x32] = 9;
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == sword);
    /* With none to be had, bare hands: the weapon put away. */
    reset("");
    a = record('A', 0, 5, 5);
    game.vm.character = a->record;
    dagger = give(a, 0x04, true, 0);
    a->items[dagger - 1][0x3d] = 0x53;
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == 0 && a->items[dagger - 1][0x34] == 0);
    /* The best shield, by its bonus, readied only with fewer than two
     * hands held: not beside a sword. */
    reset("");
    a = record('A', 0, 5, 5);
    game.vm.character = a->record;
    size_t poor = give(a, 0x25, true, -1), good = give(a, 0x25, false, 1);
    sword = give(a, 0x12, true, 0);
    CHECK(a->slots[1] == poor && a->slots[0] == sword);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[1] == poor && a->slots[0] == sword);
    a->items[sword - 1][0x36] = 1; /* cursed, it stays */
    a->items[sword - 1][0x34] = 0;
    char why[300];
    CHECK(cok_character_stats(a, &game.item_types, why, sizeof why) && a->slots[0] == 0);
    a->items[sword - 1][0x2e] = 0x25; /* now a shield, cursed and not readied */
    CHECK(cok_ai_choose_weapon(&game, a));
    CHECK(a->slots[1] == good && a->items[poor - 1][0x34] == 0 && a->slots[0] == 0);
    /* A two-handed sword: the shield put away for it. */
    size_t two = give(a, 0x10, false, 3);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == two && a->slots[1] == 0);
    /* Readying for another than the selected stops. */
    game.vm.character = NULL;
    give(a, 0x04, false, 9);
    CHECK(!cok_ai_choose_weapon(&game, a) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_guard(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    CHECK(cok_ai_guard(&game, a) && a->combat->guarding == 1 && a->combat->initiative == 0);
    CHECK(LOGGED("print: Guarding;"));
    /* No initiative left, a missile weapon, helpless, or made to flee: the
     * turn ends, unguarded. */
    s.log[0] = '\0';
    CHECK(cok_ai_guard(&game, a) && a->combat->guarding == 0 && !LOGGED("Guarding"));
    a->combat->initiative = 3;
    a->combat->forced = 1;
    CHECK(cok_ai_guard(&game, a) && a->combat->guarding == 0 && a->combat->initiative == 0);
    a->combat->forced = 0;
    a->combat->initiative = 3;
    give(a, 0x16, true, 0);
    CHECK(cok_ai_guard(&game, a) && a->combat->guarding == 0 && a->combat->initiative == 0);
    reset("");
    a = record('A', 0, 5, 5);
    CHECK(cok_character_add_effect(a, 0x33, 0, 0, false) != NULL);
    CHECK(cok_ai_guard(&game, a) && a->combat->guarding == 0 && a->combat->initiative == 0);
}

static void test_can_step(void)
{
    reset("");
    cok_combat *k = &game.combat;
    cok_character *a = record('A', 1, 5, 5);
    cok_combat_record *cr = a->combat;
    bool off, ok;
    cr->pattern = 1;
    cr->movement = 5;
    /* Row 1's first try: straight on, 2 a step on floor. */
    CHECK(cok_ai_can_step(&game, a, 1, 2, &off, &ok) && ok && !off);
    /* A diagonal costs 3, needing more than that left. */
    CHECK(cok_ai_can_step(&game, a, 1, 1, &off, &ok) && ok);
    cr->movement = 3;
    CHECK(cok_ai_can_step(&game, a, 1, 1, &off, &ok) && !ok);
    CHECK(cok_ai_can_step(&game, a, 1, 2, &off, &ok) && ok);
    cr->movement = 2;
    CHECK(cok_ai_can_step(&game, a, 1, 2, &off, &ok) && !ok);
    /* Row 1's second try turns by 7: west of north. */
    cr->movement = 9;
    k->cells[4][4] = 0x01; /* a wall */
    CHECK(cok_ai_can_step(&game, a, 2, 0, &off, &ok) && !ok && !off);
    CHECK(cok_ai_can_step(&game, a, 3, 0, &off, &ok) && ok);
    cr->pattern = 2;
    CHECK(cok_ai_can_step(&game, a, 4, 0, &off, &ok) && !ok);
    CHECK(cok_ai_can_step(&game, a, 2, 0, &off, &ok) && ok);
    /* Held by another. */
    record('B', 0, 5, 4);
    CHECK(cok_ai_can_step(&game, a, 1, 0, &off, &ok) && !ok);
    /* Off the map. */
    k->combatant[1].x = 0;
    CHECK(cok_combat_occupy(k));
    CHECK(cok_ai_can_step(&game, a, 1, 6, &off, &ok) && !ok && off);
    /* A puddle for one below level 7, unless 0x63 or made to flee. */
    k->combatant[1].x = 5;
    CHECK(cok_combat_occupy(k));
    k->cells[5][6] = 0x1d;
    CHECK(cok_ai_can_step(&game, a, 1, 2, &off, &ok) && !ok);
    a->record[0xd6] = 7;
    CHECK(cok_ai_can_step(&game, a, 1, 2, &off, &ok) && ok);
    a->record[0xd6] = 1;
    cr->forced = 1;
    CHECK(cok_ai_can_step(&game, a, 1, 2, &off, &ok) && ok);
    cr->forced = 0;
    CHECK(cok_character_add_effect(a, 0x63, 0, 0, false) != NULL);
    CHECK(cok_ai_can_step(&game, a, 1, 2, &off, &ok) && ok);
    /* A green cloud: a saving throw of type 0, unless 0x20, 0x1e, 0x63,
     * 0x3f or made to flee. */
    k->cells[6][5] = 0x1e;
    uint32_t start = game.vm.seed = 5;
    CHECK(cok_ai_can_step(&game, a, 1, 4, &off, &ok) && ok && game.vm.seed == start);
    a->effects->id = 0x3f;
    CHECK(cok_ai_can_step(&game, a, 1, 4, &off, &ok) && ok && game.vm.seed == start);
    a->effects->id = 0x01;
    a->record[0xd0] = 21; /* a throw it never makes */
    CHECK(cok_ai_can_step(&game, a, 1, 4, &off, &ok) && !ok && game.vm.seed != start);
    a->record[0xd0] = 1;
    game.vm.seed = seed_seq(1, (const unsigned[]){20, 2, 19});
    CHECK(cok_ai_can_step(&game, a, 1, 4, &off, &ok) && ok);
    /* A movement of 0xff: the cost of 0x100 wraps to 0. */
    a->record[0xd0] = 21;
    cr->movement = 0xff;
    CHECK(cok_ai_can_step(&game, a, 1, 4, &off, &ok) && ok);
}

/* A, of the party, at 5, 5 against M at x, y. */
static cok_character *duel(cok_character **m, int8_t x, int8_t y)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    *m = record('M', 1, x, y);
    return a;
}

static void test_step(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 9, 5);
    cok_combat *k = &game.combat;
    cok_combat_record *cr = m->combat;
    game.vm.character = m->record;
    cr->target = a->record;
    cr->movement = 12;
    cr->pattern = 1;
    k->last_step = 8;
    k->stuck = 0;
    /* A step toward the target, west: the d100 every monster rolls. */
    uint32_t start = game.vm.seed;
    CHECK(cok_ai_step(&game, m) && k->combatant[2].x == 8 && k->combatant[2].y == 5);
    CHECK(cr->facing == 6 && k->last_step == 6 && cr->movement == 10);
    CHECK(game.vm.seed == after(start, 1, (const unsigned[]){100}));
    CHECK(LOGGED("sound: 10;"));
    /* Blocked straight on, the second try (row 1: -1): south-west, a
     * diagonal. */
    k->cells[5][7] = 0x01;
    CHECK(cok_ai_step(&game, m) && k->combatant[2].x == 7 && k->combatant[2].y == 6);
    CHECK(k->last_step == 5 && cr->movement == 7 && cr->facing == 5);
    /* No movement left: it guards. */
    cr->movement = 1;
    CHECK(cok_ai_step(&game, m) && cr->guarding == 1 && cr->initiative == 0);
    /* An NPC of the party's holds while the enemies' health passes its
     * d100 and the morale; a monster rolls but steps. */
    a = duel(&m, 9, 5);
    cok_character *n = record('N', 0, 4, 5);
    n->record[0xe7] = 0x80;
    n->combat->target = m->record;
    n->combat->movement = 12;
    n->combat->pattern = 1;
    game.vm.character = n->record;
    game.combat.enemy_health = 100;
    game.effects.rolls.morale = 0;
    game.vm.seed = seed_seq(1, (const unsigned[]){100, 1, 99});
    CHECK(cok_ai_step(&game, n) && n->combat->guarding == 1 && k->combatant[3].x == 4);
    n->combat->initiative = 5;
    n->combat->movement = 12;
    k->last_step = 8;
    game.vm.seed = seed_seq(1, (const unsigned[]){100, 100, 100});
    CHECK(cok_ai_step(&game, n) && k->combatant[3].x == 5 && k->combatant[3].y == 4);
    /* One not fleeing, with no armour and +0x5b 5, holds. */
    m->record[0x5b] = 5;
    m->combat->target = a->record;
    m->combat->movement = 12;
    m->combat->pattern = 1;
    game.vm.character = m->record;
    CHECK(cok_ai_step(&game, m) && m->combat->guarding == 1 && k->combatant[2].x == 9);
    (void)a;
}

static void test_flee_step(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 1, 5);
    cok_combat *k = &game.combat;
    cok_combat_record *cr = m->combat;
    game.vm.character = m->record;
    cr->fleeing = 1;
    cr->movement = 12;
    a->record[0x198] = 1;
    m->record[0x198] = 12;
    /* Facing north, the monsters flee to the north-west (7), the d2 the way
     * of moving: row 2 tries 0, +1, +2, -1, -2. */
    game.vm.direction = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){100, 1, 100, 2, 2, 2});
    CHECK(cok_ai_step(&game, m) && cr->pattern == 2 && k->combatant[2].x == 0 &&
          k->combatant[2].y == 4);
    /* At the edge, faster than the party: off the map. */
    game.vm.seed = seed_seq(2, (const unsigned[]){100, 1, 100, 1, 1, 1});
    CHECK(cok_ai_step(&game, m) && m->record[0x188] == 3 && m->record[0x189] == 0);
    CHECK(LOGGED("print: M;print: Got Away;") && cr->fleeing == 0 && cr->movement == 0);
    /* The party flees the other way: facing east, west (2 + 4 - 0). */
    reset("");
    a = record('A', 0, 0, 5);
    m = record('M', 1, 9, 5);
    a->combat->fleeing = 1;
    a->combat->movement = 12;
    a->record[0x198] = 12;
    game.vm.character = a->record;
    game.vm.direction = 2;
    game.vm.seed = seed_seq(2, (const unsigned[]){2, 1, 1, 2, 1, 1});
    CHECK(cok_ai_step(&game, a) && a->record[0x188] == 3 && LOGGED("print: A;print: Got Away;"));
    /* Slower, blocked. */
    reset("");
    a = record('A', 0, 0, 5);
    m = record('M', 1, 9, 5);
    a->combat->fleeing = 1;
    a->combat->movement = 12;
    m->record[0x198] = 12;
    game.vm.character = a->record;
    game.vm.direction = 2;
    CHECK(cok_ai_step(&game, a) && a->record[0x189] == 1 && LOGGED("print: Escape is blocked;"));
    CHECK(a->combat->initiative == 0 && a->combat->fleeing == 0);
    /* Facing south (4), monsters run north-east (3 - ... 4 - 1 = 3)... the
     * table: 0 7, 2 2, 4 3, 6 6. */
    reset("");
    m = record('M', 1, 49, 24);
    record('A', 0, 40, 10);
    m->combat->fleeing = 1;
    m->combat->movement = 12;
    m->record[0x198] = 12;
    game.vm.character = m->record;
    game.vm.direction = 4;
    CHECK(cok_ai_step(&game, m) && m->record[0x188] == 3);
}

static void test_stuck(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 9, 5);
    cok_combat *k = &game.combat;
    cok_combat_record *cr = m->combat;
    game.vm.character = m->record;
    cr->target = a->record;
    cr->movement = 40;
    cr->pattern = 1;
    k->last_step = 2; /* came from the west: west is back */
    k->stuck = 0;
    /* The way back counts as stuck: the next row, the step taken. */
    CHECK(cok_ai_step(&game, m) && k->stuck == 1 && cr->pattern == 2);
    CHECK(k->combatant[2].x == 8 && k->last_step == 6 && cr->target == a->record);
    /* Twice: the target forgotten and picked again (432f:3f9f). */
    k->last_step = 2;
    CHECK(cok_ai_step(&game, m) && k->stuck == 2 && cr->pattern == 3 && cr->target == a->record);
    /* Three times (row 3 tries -1 first: south-west, back north-east):
     * no movement left, though the step is recorded. */
    k->last_step = 1;
    unsigned x = (unsigned)k->combatant[2].x;
    CHECK(cok_ai_step(&game, m) && k->stuck == 3 && cr->movement == 0 && cr->target == NULL);
    CHECK((unsigned)k->combatant[2].x == x && k->last_step == 5 && cr->pattern == 4);
    /* Walled in: every try fails, stuck, and it stops. */
    reset("");
    a = record('A', 0, 2, 2);
    m = record('M', 1, 9, 5);
    for (int y = 4; y <= 6; ++y)
        for (int xx = 8; xx <= 10; ++xx)
            if (y != 5 || xx != 9) k->cells[y][xx] = 0x01;
    cr = m->combat;
    game.vm.character = m->record;
    cr->target = a->record;
    cr->movement = 40;
    cr->pattern = 6;
    k->last_step = 8;
    k->stuck = 1;
    CHECK(cok_ai_step(&game, m) && k->stuck == 2 && cr->pattern == 1 && k->last_step == 8);
    CHECK(k->combatant[2].x == 9 && cr->target == a->record);
}

static void test_fight(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 6, 5);
    cok_combat_record *cr = a->combat;
    game.vm.character = a->record;
    a->record[0x18c] = 0x60; /* hits */
    a->record[0x18f] = 2;
    m->record[0x197] = 1;
    bool over;
    /* An enemy a square away is picked and struck: the turn goes on, as it
     * fell with attacks left. */
    CHECK(cok_ai_fight(&game, a, &over) && !over && m->record[0x189] == 0);
    CHECK(LOGGED("print: A;print: Attacks;") && LOGGED("print: M;print: goes down;"));
    CHECK(game.combat.last_step == 8 && game.combat.stuck == 0 && game.combat.in_reach);
    /* With none it is the end: nothing listed, none to pick, it guards
     * and goes on (0d49's quirk). */
    s.log[0] = '\0';
    cr->initiative = 5;
    CHECK(cok_ai_fight(&game, a, &over) && !over && LOGGED("print: Guarding;"));
    CHECK(cr->initiative == 0);
    /* No initiative: nothing. */
    s.log[0] = '\0';
    CHECK(cok_ai_fight(&game, a, &over) && over && s.log[0] == '\0');
    /* A dying party member is bandaged by the party's: the turn ends. */
    a = duel(&m, 6, 5);
    cok_character *b = record('B', 0, 3, 3);
    b->record[0x189] = 0;
    b->record[0x188] = 5;
    b->combat->not_party = 0;
    game.vm.character = a->record;
    CHECK(cok_ai_fight(&game, a, &over) && over && b->record[0x188] == 4);
    CHECK(LOGGED("print: B;print: is bandaged;"));
    /* A monster steps toward one out of reach, a square for 4 half
     * squares, until it can step no more: then it guards. */
    a = duel(&m, 9, 5);
    game.vm.character = m->record;
    m->combat->movement = 4;
    m->combat->pattern = 1;
    CHECK(cok_ai_fight(&game, m, &over) && over && game.combat.combatant[2].x == 8);
    CHECK(m->combat->guarding == 1 && LOGGED("print: Guarding;"));
    CHECK(m->combat->target == a->record);
    /* An archer with an enemy a square away readies a weapon for melee and
     * leaves; with none, it shoots from afar. */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    size_t bow = give(a, 0x16, true, 0), arrows = give(a, 0x1e, true, 0);
    size_t sword = give(a, 0x12, false, 0);
    a->items[arrows - 1][0x39] = 10;
    CHECK(a->slots[0] == bow);
    CHECK(cok_ai_fight(&game, a, &over) && !over && a->slots[0] == sword && m->record[0x189] == 1);
    a = duel(&m, 10, 5);
    game.vm.character = a->record;
    bow = give(a, 0x16, true, 0);
    arrows = give(a, 0x1e, true, 0);
    a->items[arrows - 1][0x39] = 10;
    a->record[0x18c] = 0x60;
    a->record[0x18f] = 1;
    CHECK(cok_ai_fight(&game, a, &over) && LOGGED("print: A;print: Attacks;"));
    CHECK(LOGGED("sound: 12;") && a->items[arrows - 1][0x39] == 9);
}

static void test_spells(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 7, 5);
    game.vm.character = m->record;
    bool fits;
    /* Bless (1): priority 1, not aimed at enemies. */
    CHECK(cok_ai_spell_fits(&game, m, 1, 1, &fits) && fits);
    CHECK(cok_ai_spell_fits(&game, m, 1, 2, &fits) && !fits);
    /* Magic Missile (0x0f): priority 5, at an enemy within 6 + level. */
    m->record[0xfe] = 1;
    CHECK(cok_ai_spell_fits(&game, m, 0x0f, 5, &fits) && fits);
    CHECK(cok_ai_spell_fits(&game, m, 0x0f, 6, &fits) && !fits);
    game.combat.combatant[1].x = 30;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_ai_spell_fits(&game, m, 0x0f, 5, &fits) && !fits);
    /* Shocking Grasp (0x14): touch (0xff), range 1. */
    CHECK(cok_ai_spell_fits(&game, m, 0x14, 2, &fits) && !fits);
    game.combat.combatant[1].x = 6;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_ai_spell_fits(&game, m, 0x14, 2, &fits) && fits);
    /* Sleep (0x15): an area of 1 around the enemy, with M's ally beside it
     * throwing type 4 at +8: one failure rejects it. */
    cok_character *n = record('N', 1, 5, 6);
    n->record[0xd4] = 21; /* never makes it */
    uint32_t start = game.vm.seed;
    CHECK(cok_ai_spell_fits(&game, m, 0x15, 4, &fits) && !fits && game.vm.seed != start);
    n->record[0xd4] = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){20, 2, 20, 20, 2, 20});
    CHECK(cok_ai_spell_fits(&game, m, 0x15, 4, &fits) && fits);
    /* Its caster stands outside; on the party's side the bonus is -2. */
    (void)a;
    /* Cure Light Wounds is never tested: priority 0. */
    CHECK(cok_ai_spell_fits(&game, m, 3, 1, &fits) && !fits);
    /* The range reads the selected caster's level. */
    game.vm.character = NULL;
    CHECK(!cok_ai_spell_fits(&game, m, 0x15, 4, &fits) && game.vm.status != COK_ECL_OK);
}

static void test_cast(void)
{
    cok_character *m;
    duel(&m, 7, 5);
    game.vm.character = m->record;
    bool cast;
    /* None memorized: the d7 all the same. */
    uint32_t start = game.vm.seed;
    CHECK(cok_ai_cast(&game, m, &cast) && !cast && game.vm.seed == after(start, 1, (const unsigned[]){7}));
    /* Bless (priority 1) at pass 7 (level 1): seven passes of three
     * draws. */
    m->record[0x20] = 1;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 7, 7});
    CHECK(cok_ai_cast(&game, m, &cast) && cast && m->combat->initiative == 0);
    CHECK(LOGGED("unported: M casts spell 0x01 (432f:28bd);"));
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 1, 6});
    start = game.vm.seed;
    CHECK(cok_ai_cast(&game, m, &cast) && !cast);
    /* +0x1e is not a memorized spell's byte. */
    m->record[0x20] = 0;
    m->record[0x1e] = 1;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 7, 7});
    CHECK(cok_ai_cast(&game, m, &cast) && !cast);
    /* One may not cast this round, or a party member without Magic On:
     * none. */
    m->record[0x1e] = 0;
    m->record[0x57] = 1;
    m->combat->may_cast = 0;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 7, 7});
    CHECK(cok_ai_cast(&game, m, &cast) && !cast);
    m->combat->may_cast = 1;
    m->record[0xe7] = 0x7f;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 7, 7});
    CHECK(cok_ai_cast(&game, m, &cast) && !cast);
    game.combat.magic_on = true;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 7, 7});
    CHECK(cok_ai_cast(&game, m, &cast) && cast);
    /* No enemy that can act. */
    game.combat.sides[0] = 0;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 7, 7});
    CHECK(cok_ai_cast(&game, m, &cast) && !cast);
}

static void test_use_item(void)
{
    cok_character *m;
    duel(&m, 7, 5);
    game.vm.character = m->record;
    bool used;
    /* A wand of Magic Missile (0x0f) readied. */
    size_t wand = give(m, 0x34, true, 0);
    m->items[wand - 1][0x3d] = 0x0f;
    m->record[0xfe] = 1;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 3, 3});
    CHECK(cok_ai_use_item(&game, m, &used) && used);
    CHECK(LOGGED("unported: M uses an item's spell 0x0f (546c:24d7);"));
    /* At pass 2, level 6, it no longer suits. */
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 1, 1});
    CHECK(cok_ai_use_item(&game, m, &used) && !used);
    /* Not readied, a power (+0x3e from 0x80), no spell, a scroll, may not
     * use items, the area forbids magic. */
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 3, 3});
    m->items[wand - 1][0x34] = 0;
    CHECK(cok_ai_use_item(&game, m, &used) && !used);
    m->items[wand - 1][0x34] = 1;
    m->items[wand - 1][0x3e] = 0x80;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 3, 3});
    CHECK(cok_ai_use_item(&game, m, &used) && !used);
    m->items[wand - 1][0x3e] = 0;
    m->items[wand - 1][0x2e] = 0x27;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 3, 3});
    CHECK(cok_ai_use_item(&game, m, &used) && !used);
    m->items[wand - 1][0x2e] = 0x34;
    m->combat->may_use = 0;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 3, 3});
    CHECK(cok_ai_use_item(&game, m, &used) && !used);
    m->combat->may_use = 1;
    game.vm.mem4b00[0xe5] = 1;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 3, 3});
    CHECK(cok_ai_use_item(&game, m, &used) && !used);
    game.vm.mem4b00[0xe5] = 0;
    /* Spells above 0x38 are tested as 0x17 less: 0x4a as 0x33, of
     * priority 7. */
    m->items[wand - 1][0x3d] = 0x4a;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 1, 1});
    CHECK(cok_ai_use_item(&game, m, &used) && used);
    CHECK(LOGGED("uses an item's spell 0x4a"));
}

static void test_turn_undead(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 7, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 3;
    m->record[0xda] = 1;
    game.undead = 1;
    bool turned;
    /* A cleric of level 3 against a kind 1 (needs 4): a d12 of them, one
     * d20. Limit 2 allows kind 1. */
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 1, 1, 20, 4, 4});
    CHECK(cok_ai_turn_undead(&game, a, 2, &turned) && turned);
    CHECK(LOGGED("print: A;print: turns undead...;") && LOGGED("print: M;print: is turned;"));
    CHECK(m->record[0x188] == 3 && a->combat->turns == 1 && a->combat->initiative == 0);
    /* No more tries than undead loaded. */
    CHECK(cok_ai_turn_undead(&game, a, 2, &turned) && !turned);
    /* Below 4: nothing happens. */
    a = duel(&m, 7, 5);
    a->record[0xf9] = 3;
    m->record[0xda] = 1;
    game.undead = 1;
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 1, 1, 20, 3, 3});
    CHECK(cok_ai_turn_undead(&game, a, 4, &turned) && turned);
    CHECK(LOGGED("print: Nothing Happens...;") && m->record[0x189] == 1);
    /* Kinds above the limit are not tried. */
    a = duel(&m, 7, 5);
    a->record[0xf9] = 3;
    m->record[0xda] = 3;
    game.undead = 1;
    CHECK(cok_ai_turn_undead(&game, a, 2, &turned) && !turned);
    CHECK(cok_ai_turn_undead(&game, a, 4, &turned) && turned);
    /* No cleric. */
    a = duel(&m, 7, 5);
    m->record[0xda] = 1;
    game.undead = 1;
    CHECK(cok_ai_turn_undead(&game, a, 4, &turned) && !turned);
    a->record[0x101] = 1;
    CHECK(cok_ai_turn_undead(&game, a, 4, &turned) && turned);
}

static void test_destroy(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 7, 5);
    cok_character *n = record('N', 1, 8, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 7;
    a->record[0x5d] = 2; /* +2: level 9 */
    m->record[0xda] = 1;
    n->record[0xda] = 2;
    /* Kind 2 first (0 at level 9: destroyed), then kind 1 (-1): two of a
     * d12 of 1, the second kept by the bonus turns. */
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 2, 2, 20, 1, 1});
    CHECK(cok_combat_turn_undead(&game, a));
    CHECK(n->record[0x188] == 8 && n->record[0x189] == 0 && m->record[0x188] == 8);
    CHECK(LOGGED("print: N;print: Is destroyed;") && LOGGED("print: M;print: Is destroyed;"));
    CHECK(game.combat.sides[1] == 0 && a->combat->turns == 1);
    /* One of a kind out of reach of the d20 lowers the kinds tried. */
    a = duel(&m, 7, 5);
    n = record('N', 1, 8, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 1;
    m->record[0xda] = 1;
    n->record[0xda] = 6; /* 99 at level 1 */
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 3, 3, 20, 20, 20});
    CHECK(cok_combat_turn_undead(&game, a));
    CHECK(n->record[0x189] == 1 && m->record[0x188] == 3);
    /* Past the table the port stops. */
    a = duel(&m, 7, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 60;
    m->record[0xda] = 12;
    CHECK(!cok_combat_turn_undead(&game, a) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_computer(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 6, 5);
    game.vm.character = m->record;
    m->record[0xe7] = 0x80 + 0x30;
    CHECK(cok_character_add_effect(a, 0x33, 0, 0, false) != NULL); /* held */
    /* A monster's turn: the way of moving, its morale, its d7s, then the
     * attack, on one held: slain. */
    CHECK(cok_combat_computer(&game, m) && a->record[0x189] == 0);
    CHECK(LOGGED("print: M;print: slays helpless;") && !LOGGED("flees"));
    CHECK(m->combat->pattern >= 1 && m->combat->pattern <= 6);
    /* One that cannot act ends its turn, after its dice. */
    s.log[0] = '\0';
    m->record[0x189] = 0;
    m->combat->initiative = 5;
    m->combat->pattern = 2;
    game.vm.seed = seed_seq(1, (const unsigned[]){4, 2, 4});
    uint32_t start = game.vm.seed;
    CHECK(cok_combat_computer(&game, m) && m->combat->initiative == 0);
    CHECK(game.vm.seed == after(start, 1, (const unsigned[]){4}) && s.log[0] == '\0');
    /* A spell begun earlier: not ported, logged, the turn over. */
    a = duel(&m, 6, 5);
    game.vm.character = m->record;
    m->combat->spell = 0x0f;
    CHECK(cok_combat_computer(&game, m) && m->combat->initiative == 0 && a->record[0x189] == 1);
    CHECK(LOGGED("unported: M casts spell 0x0f at its turn (5b04:1415);"));
    /* Fleeing in panic, said each turn, then away from the party. */
    a = duel(&m, 6, 5);
    game.vm.character = m->record;
    m->record[0xe7] = 0x80;
    m->record[0x197] = 1;
    m->combat->movement = 6;
    a->record[0x198] = 1;
    m->record[0x198] = 12;
    CHECK(cok_combat_computer(&game, m) && LOGGED("print: M;print: flees in panic;"));
    CHECK(game.combat.combatant[2].x != 6 || game.combat.combatant[2].y != 5);
    /* Too slow, of intelligence 5: it fights on. */
    a = duel(&m, 6, 5);
    game.vm.character = m->record;
    m->record[0xe7] = 0x80;
    a->record[0x198] = 13;
    m->record[0x13] = 5;
    CHECK(cok_combat_computer(&game, m) && !LOGGED("flees") && LOGGED("print: M;print: Attacks;"));
    /* A party member on Auto: its morale untested, it attacks. */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    a->record[0xe7] = 0; /* morale 0, which only monsters test */
    CHECK(cok_character_add_effect(m, 0x33, 0, 0, false) != NULL);
    CHECK(cok_combat_computer(&game, a) && m->record[0x189] == 0 && !LOGGED("flees"));
    /* Space hands it back: the initiative 0x14, nothing done. */
    a = duel(&m, 6, 5);
    s.keys = " ";
    s.length = 1;
    s.pending = true;
    game.vm.character = a->record;
    a->record[0x18c] = 0x60;
    m->record[0x197] = 1;
    CHECK(cok_combat_computer(&game, a) && m->record[0x189] == 1);
    CHECK(a->combat->initiative == 0x14 && a->record[0x18b] == 0);
    /* A cleric on Auto turns undead (limit 4, the way of moving's d4). */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 3;
    m->record[0xda] = 1;
    game.undead = 1;
    a->combat->pattern = 1;
    game.vm.seed = seed_seq(6, (const unsigned[]){4, 2, 4, 7, 7, 7, 12, 1, 1, 20, 20, 20, 1, 1, 1,
                                                     1, 1, 1});
    CHECK(cok_combat_computer(&game, a) && LOGGED("print: A;print: turns undead...;"));
    CHECK(m->record[0x188] == 3);
}

/* Whole battles through the rounds (cok_combat_battle) with --combat
 * auto: the party's first turn presses Alt-Q. */
static void test_auto(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 7, 5);
    cok_combat *k = &game.combat;
    k->turn_index = 1;
    k->turn[1] = a;
    k->turn[2] = NULL;
    a->record[0x18b] = 0;
    game.combat_stub = COK_COMBAT_AUTO;
    game.vm.character = a->record;
    CHECK(cok_combat_turn(&game, a));
    CHECK(k->turn_index == 0 && a->combat->initiative == 0x14);
    CHECK(a->record[0x18b] == 1 && m->record[0x18b] == 1);
    CHECK(LOGGED("turn: A (initiative 5);"));
    /* Run again, the computer's: 0x14 becomes 0x13. */
    s.log[0] = '\0';
    CHECK(cok_combat_turn(&game, a) && LOGGED("turn: A (initiative 19);"));
    game.combat_stub = COK_COMBAT_UNPORTED;
}

/* Cases aimed at what the tests above leave open. */

static void test_pattern_more(void)
{
    reset("");
    cok_character *a = record('A', 1, 5, 5);
    /* A d8 of 7 is a d4's row. */
    a->combat->pattern = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 7, 7, 4, 1, 1});
    CHECK(cok_ai_pattern(&game, a) && a->combat->pattern == 1);
}

static void test_keys_more(void)
{
    reset("22");
    s.pending = true;
    cok_character *a = record('A', 0, 5, 5);
    bool back;
    CHECK(cok_ai_keys(&game, a, &back) && game.combat.magic_on);
    CHECK(cok_ai_keys(&game, a, &back) && !game.combat.magic_on && LOGGED("print: Magic Off;"));
}

static void test_morale_more(void)
{
    reset("");
    record('A', 0, 5, 5);
    cok_character *n = record('N', 0, 6, 5);
    bool over;
    /* +0xe7 0x7f is the party's: untested, though its morale would be 0. */
    n->record[0xe7] = 0x7f;
    n->record[0x197] = 1;
    CHECK(cok_ai_morale(&game, n, &over) && !over && n->combat->fleeing == 0);
    /* The enemies' health is the second test's morale. */
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    m->record[0xe7] = 0x80;
    game.combat.enemy_health = 30;
    game.vm.mem7c00[0x2c6] = 70;
    CHECK(cok_ai_morale(&game, m, &over) && m->combat->fleeing == 0 && game.effects.rolls.morale == 30);
    game.combat.enemy_health = 29;
    CHECK(cok_ai_morale(&game, m, &over) && m->combat->fleeing == 1);
    (void)a;
}

static void test_weapons_more(void)
{
    /* Weapons a fighter may not use are not taken: a cleric's sword. */
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t dagger = give(a, 0x04, false, 0), sword = give(a, 0x12, false, 0);
    a->record[0x11a] = 2;
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == 0 && !LOGGED("Wrong Class"));
    a->record[0x11a] = 8;
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == sword);
    /* Bare hands worth 1d2 + twice a bonus of 2: 6, above the dagger's 7?
     * no: of 3, 8, above it. */
    a->items[sword - 1][0x2e] = 0x81 - 1; /* a type of zeros: unusable */
    a->record[0x111] = 3;
    a->items[sword - 1][0x34] = 0;
    char why[300];
    CHECK(cok_character_stats(a, &game.item_types, why, sizeof why));
    a->record[0x10d] = 1;
    a->record[0x10f] = 2;
    a->record[0x111] = 3;
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == 0);
    (void)dagger;
    /* Of two alike, the first. */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t first = give(a, 0x04, false, 0);
    give(a, 0x04, false, 0);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == first);
    /* A missile weapon worth more than half the other, but less: the bow
     * (12) over a club +1 (17). */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t bow = give(a, 0x16, false, 0);
    give(a, 0x1e, true, 0);
    size_t mace = give(a, 0x12, false, 1); /* 8 + 8 + 3: 19 */
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == bow);
    /* A sling needs no ammunition. */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t sling = give(a, 0x1c, false, 0); /* 1d4 + 2 + 3 + 3 = 12 */
    give(a, 0x04, false, 0);                /* 7 */
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == sling);
    /* A thrown weapon is a missile: a club (17) over a sword +1 (19). */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t club = give(a, 0x03, false, 1);
    mace = give(a, 0x12, false, 1);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == club);
    /* Thrown, of range above 1, with an enemy a square away all the same. */
    record('N', 1, 6, 5);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == club);
    (void)mace;
    /* A kender's hoopak (flags 0x10): a missile and a melee weapon, not
     * thrown by its flags: kept with an enemy a square away. */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 6, 5);
    a->record[0x5a] = 5;
    a->record[0x11a] = 0xfe;
    game.vm.character = a->record;
    size_t hoopak = give(a, 0x43, true, 0);
    CHECK(a->slots[0] == hoopak);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == hoopak);
    /* A cursed weapon kept is not put away: no notice. */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t dag = give(a, 0x04, true, 0);
    give(a, 0x12, false, 0);
    a->items[dag - 1][0x36] = 1;
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == dag && !LOGGED("It's Cursed"));
    /* A shield cursed stays where a better one is had; one of -2 is never
     * the best. */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t held = give(a, 0x25, true, 0);
    give(a, 0x25, false, 2);
    a->items[held - 1][0x36] = 1;
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[1] == held && !LOGGED("It's Cursed"));
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    give(a, 0x25, false, -2);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[1] == 0);
    /* A wand readied takes a hand: with the sword's and the shield's taken
     * off, a two-handed sword +2 fits (24) and is chosen; the sword is put
     * away, but with the wand's and the shield's hands Ready finds them
     * full: neither is readied. */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    give(a, 0x12, true, 0);
    give(a, 0x25, true, 0);
    give(a, 0x34, true, 0);
    size_t shield = a->slots[1], two = give(a, 0x10, false, 2);
    CHECK(a->record[0x17b] == 3);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == 0 && a->slots[1] == shield);
    CHECK(a->items[two - 1][0x34] == 0 && !LOGGED("hands are full"));
    /* The panel is redrawn after a change, which clears it as due; with
     * none, not. */
    game.combat.panel = true;
    CHECK(cok_ai_choose_weapon(&game, a) && !game.combat.panel);
}

static void test_spells_more(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 20, 5);
    game.vm.character = m->record;
    bool fits;
    /* Magic Missile's range 6 + 4 per level: at level 3, 18. */
    m->record[0xfe] = 3;
    CHECK(cok_ai_spell_fits(&game, m, 0x0f, 5, &fits) && fits);
    m->record[0xfe] = 1;
    CHECK(cok_ai_spell_fits(&game, m, 0x0f, 5, &fits) && !fits);
    /* An ally two cells from the enemy is outside Sleep's radius of 1. */
    a = duel(&m, 7, 5);
    game.vm.character = m->record;
    cok_character *n = record('N', 1, 5, 7);
    n->record[0xd4] = 21;
    m->record[0xfe] = 1;
    CHECK(cok_ai_spell_fits(&game, m, 0x15, 4, &fits) && fits);
    /* At +8 on the monsters' side: a 10 makes a throw of 15. */
    n = record('O', 1, 5, 6);
    n->record[0xd4] = 15;
    game.vm.seed = seed_seq(1, (const unsigned[]){20, 10, 10});
    CHECK(cok_ai_spell_fits(&game, m, 0x15, 4, &fits) && fits);
    /* Silence (0x19), whose save negates it: no throw at all. */
    n->record[0xd4] = 21;
    uint32_t start = game.vm.seed;
    m->record[0xf9] = 1;
    CHECK(cok_ai_spell_fits(&game, m, 0x19, 2, &fits) && fits && game.vm.seed == start);
    (void)a;
}

static void test_cast_more(void)
{
    cok_character *m;
    duel(&m, 7, 5);
    game.vm.character = m->record;
    bool cast;
    /* Shocking Grasp (0x14, priority 2) only at passes up to 6: a d7 of
     * 5 tests it at levels 7-3, never 2. */
    m->record[0x20] = 0x14;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 5, 5});
    CHECK(cok_ai_cast(&game, m, &cast) && !cast);
}

static void test_turning_more(void)
{
    cok_character *m;
    bool turned;
    /* Kind 2 at a limit of 2. */
    cok_character *a = duel(&m, 7, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 3;
    m->record[0xda] = 2;
    game.undead = 1;
    CHECK(cok_ai_turn_undead(&game, a, 2, &turned) && turned);
    /* No more tries than undead loaded. */
    a = duel(&m, 7, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 3;
    m->record[0xda] = 1;
    game.undead = 1;
    a->combat->turns = 1;
    CHECK(cok_ai_turn_undead(&game, a, 2, &turned) && !turned);
    /* One made to flee is passed over. */
    a->combat->turns = 0;
    m->combat->forced = 1;
    CHECK(cok_ai_turn_undead(&game, a, 2, &turned) && !turned);
    /* Of two of a kind the first; level 4 against kind 1, 1: turned. */
    a = duel(&m, 7, 5);
    cok_character *n = record('N', 1, 8, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 4;
    m->record[0xda] = n->record[0xda] = 1;
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 1, 1, 20, 1, 1});
    CHECK(cok_combat_turn_undead(&game, a) && m->record[0x188] == 3 && n->record[0x189] == 1);
    CHECK(LOGGED("print: M;print: is turned;"));
    /* Deity 2: level 2 + 2, kind 4 needs 7 at 4, 13 at 2. */
    a = duel(&m, 7, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 2;
    a->record[0x5d] = 2;
    m->record[0xda] = 4;
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 1, 1, 20, 8, 8});
    CHECK(cok_combat_turn_undead(&game, a) && m->record[0x188] == 3);
    /* A value of 0 at the last of a d12 of 1: one destroyed, no more. */
    a = duel(&m, 7, 5);
    n = record('N', 1, 8, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 6;
    m->record[0xda] = n->record[0xda] = 1;
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 1, 1, 20, 1, 1});
    CHECK(cok_combat_turn_undead(&game, a) && m->record[0x188] == 8 && n->record[0x189] == 1);
    CHECK(!LOGGED("Nothing Happens"));
    /* Below 0 at the last: kept while the bonus turns last. */
    a = duel(&m, 7, 5);
    n = record('N', 1, 8, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 8;
    m->record[0xda] = n->record[0xda] = 1;
    game.vm.seed = seed_seq(2, (const unsigned[]){12, 1, 1, 20, 1, 1});
    CHECK(cok_combat_turn_undead(&game, a) && m->record[0x188] == 8 && n->record[0x188] == 8);
}

/* The stack's byte for turning, as 3afb:004b leaves it. */
static void test_turning_limit(void)
{
    cok_character *m;
    /* A row 5 kept from no row: a d8 of 8 and a d2, sides 2: kind 3 not
     * tried. */
    cok_character *a = duel(&m, 6, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 3;
    a->record[0xe7] = 0;
    m->record[0xda] = 3;
    game.undead = 1;
    a->combat->pattern = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 8, 8, 2, 1, 1});
    CHECK(cok_combat_computer(&game, a) && !LOGGED("turns undead"));
    /* A d4 last, sides 4: tried. */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    a->record[0xf9] = 3;
    m->record[0xda] = 3;
    game.undead = 1;
    a->combat->pattern = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 1, 7, 4, 1, 1});
    CHECK(cok_combat_computer(&game, a) && LOGGED("turns undead"));
    /* A monster cleric fleeing in panic (0x42) turns kind 5 at sides 2. */
    a = duel(&m, 6, 5);
    game.vm.character = m->record;
    m->record[0xf9] = 9;
    m->record[0xe7] = 0x80;
    m->record[0x198] = 12;
    a->record[0x198] = 1;
    a->record[0xda] = 5;
    game.undead = 1;
    m->combat->pattern = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 8, 8, 2, 1, 1});
    CHECK(cok_combat_computer(&game, m) && LOGGED("flees in panic") && LOGGED("turns undead"));
    /* Made to flee ('l'): the same, and no panic said. */
    a = duel(&m, 6, 5);
    game.vm.character = m->record;
    m->record[0xf9] = 9;
    m->combat->forced = 1;
    a->record[0xda] = 5;
    game.undead = 1;
    m->combat->pattern = 0;
    game.vm.seed = seed_seq(2, (const unsigned[]){8, 8, 8, 2, 1, 1});
    CHECK(cok_combat_computer(&game, m) && LOGGED("is forced to flee") && LOGGED("turns undead"));
    CHECK(!LOGGED("flees in panic"));
}

static void test_step_more(void)
{
    cok_character *m;
    /* A monster rolls its d100 but does not hold. */
    cok_character *a = duel(&m, 9, 5);
    game.vm.character = m->record;
    m->combat->target = a->record;
    m->combat->movement = 12;
    m->combat->pattern = 1;
    game.combat.last_step = 8;
    game.combat.enemy_health = 100;
    game.vm.seed = seed_seq(1, (const unsigned[]){100, 1, 1});
    CHECK(cok_ai_step(&game, m) && game.combat.combatant[2].x == 8);
    /* An NPC holds by the d100 plus the morale: 50 + 50 against 99. */
    a = duel(&m, 9, 5);
    cok_character *n = record('N', 0, 4, 5);
    n->record[0xe7] = 0x80;
    n->combat->target = m->record;
    n->combat->movement = 12;
    n->combat->pattern = 1;
    game.vm.character = n->record;
    game.combat.last_step = 8;
    game.combat.enemy_health = 99;
    game.effects.rolls.morale = 50;
    game.vm.seed = seed_seq(1, (const unsigned[]){100, 50, 50});
    CHECK(cok_ai_step(&game, n) && n->combat->guarding == 0 && game.combat.combatant[3].x == 5);
    /* Not fleeing, a step off the map is not taken: the next try. East is
     * walled, north-east and north off the map: south-east. */
    a = duel(&m, 30, 0);
    game.vm.character = a->record;
    a->combat->target = m->record;
    a->combat->movement = 12;
    a->combat->pattern = 1;
    game.combat.last_step = 8;
    game.combat.combatant[1].y = 0;
    game.combat.cells[0][6] = 0x01;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_ai_step(&game, a) && a->record[0x189] == 1 && game.combat.combatant[1].x == 6 &&
          game.combat.combatant[1].y == 1 && !LOGGED("Escape") && !LOGGED("Got Away"));
    /* A party member out of the view is shown as it steps: the view
     * follows. */
    a = duel(&m, 30, 20);
    game.vm.character = a->record;
    a->combat->target = m->record;
    a->combat->movement = 12;
    a->combat->pattern = 1;
    game.combat.last_step = 8;
    game.combat.view_x = 20;
    game.combat.view_y = 15;
    CHECK(cok_combat_occupy(&game.combat)); /* the screen positions */
    CHECK(cok_ai_step(&game, a) && game.combat.show_actions && game.combat.view_x != 20);
}

/* A guard's blow that paralyses (0x45) leaves the stepper helpless: its
 * turn ends. */
static void test_step_paralysed(void)
{
    cok_character *m;
    cok_character *a = duel(&m, 9, 5);
    cok_character *b = record('B', 0, 7, 4);
    CHECK(cok_character_add_effect(b, 0x45, 0, 0, false) != NULL);
    b->combat->guarding = 1;
    b->record[0x18c] = 0x60;
    for (int k = 0; k < 5; ++k) m->record[0xd0 + k] = 21;
    game.vm.character = m->record;
    m->combat->target = a->record;
    m->combat->movement = 12;
    m->combat->pattern = 1;
    game.combat.last_step = 8;
    CHECK(cok_ai_step(&game, m) && game.combat.combatant[2].x == 8);
    CHECK(cok_combat_helpless(m) && m->combat->initiative == 0 && m->combat->movement == 0);
}

static void test_fight_more(void)
{
    cok_character *m;
    bool over;
    /* A party member keeps its target, but out of reach steps toward it. */
    cok_character *a = duel(&m, 9, 5);
    game.vm.character = a->record;
    a->combat->target = m->record;
    a->combat->movement = 4;
    a->combat->pattern = 1;
    CHECK(cok_ai_fight(&game, a, &over) && game.combat.combatant[1].x == 6 && !LOGGED("Attacks"));
    /* The defender is booked. */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    CHECK(cok_ai_fight(&game, a, &over) && m->combat->hits == 1);
    /* Event 0x0e: a breath not ported, logged. */
    a = duel(&m, 6, 5);
    game.vm.character = m->record;
    CHECK(cok_character_add_effect(m, 0x04, 0, 0, false) != NULL);
    CHECK(cok_ai_fight(&game, m, &over) && LOGGED("on event 0x0e"));
    /* A thrown club a square away strikes, not readying another, and is
     * not thrown: it stays. */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    size_t club = give(a, 0x03, true, 0);
    give(a, 0x12, false, 5);
    CHECK(cok_ai_fight(&game, a, &over) && LOGGED("print: A;print: Attacks;"));
    CHECK(a->item_count == 2 && a->slots[0] == club && !LOGGED("sound: 12;"));
    /* A bow of range 22: reach 21. One 22 squares off is not reached: a
     * step first (sound 10), then the arrow (sound 12). */
    a = duel(&m, 27, 5);
    game.vm.character = a->record;
    give(a, 0x16, true, 0);
    size_t arrows = give(a, 0x1e, true, 0);
    a->items[arrows - 1][0x39] = 10;
    a->combat->movement = 4;
    a->combat->pattern = 1;
    CHECK(cok_ai_fight(&game, a, &over) && LOGGED("sound: 10;sound: 12;") &&
          game.combat.combatant[1].x == 6 && strstr(s.log, "sound: 12;") > strstr(s.log, "sound: 10;"));
    /* Without arrows a bow strikes on from afar, DS:71ab left clear. */
    a = duel(&m, 8, 5);
    game.vm.character = a->record;
    give(a, 0x16, true, 0);
    CHECK(cok_ai_fight(&game, a, &over) && LOGGED("print: A;print: Attacks;") && !game.combat.in_reach);
    /* A sweep: a fighter of three sweeps among two of no hit dice and 1
     * hit point, with no attacks with its weapon but two of its second
     * slot: "sweeps", each target felled by a blow of the second slot, the
     * one attack it is given (+0x18f) left; the turn ends all the same
     * (6346:2964), though the call says it goes on. */
    cok_character *n = NULL;
    for (uint32_t seed = 1; seed < 200; ++seed) {
        a = duel(&m, 6, 5);
        n = record('N', 1, 6, 4);
        m->record[0xd6] = n->record[0xd6] = 0;
        m->record[0x197] = n->record[0x197] = 1;
        game.vm.character = a->record;
        a->combat->sweeps = 3;
        a->record[0x18c] = 0x60;
        a->record[0x18f] = 0;
        a->record[0x190] = 2;
        a->record[0x10e] = a->record[0x192] = 1; /* the second slot's 1d6 + 10 */
        a->record[0x110] = a->record[0x194] = 6;
        a->record[0x112] = a->record[0x196] = 10;
        game.vm.seed = seed;
        CHECK(cok_ai_fight(&game, a, &over) && !over && LOGGED("print: A;print: sweeps;"));
        if (a->record[0x18f] == 1) break; /* both felled by the second slot */
    }
    CHECK(m->record[0x189] == 0 && n->record[0x189] == 0 && a->record[0x18f] == 1);
    CHECK(a->combat->initiative == 0);
}

static void test_computer_more(void)
{
    cok_character *m;
    /* No initiative: no fight (0d49 would clear the last step). */
    cok_character *a = duel(&m, 9, 5);
    game.vm.character = m->record;
    m->record[0xe7] = 0x80 + 0x30;
    m->combat->initiative = 0;
    game.combat.last_step = 3;
    CHECK(cok_combat_computer(&game, m) && game.combat.last_step == 3);
    /* An item used ends the turn. */
    a = duel(&m, 7, 5);
    game.vm.character = m->record;
    m->record[0xe7] = 0x80 + 0x30;
    size_t wand = give(m, 0x34, true, 0);
    m->items[wand - 1][0x3d] = 0x01; /* Bless: no target needed */
    game.vm.seed = seed_seq(4, (const unsigned[]){4, 2, 4, 7, 7, 7, 7, 7, 7, 7, 7, 7});
    CHECK(cok_combat_computer(&game, m) && LOGGED("uses an item's spell") &&
          m->combat->initiative == 0);
    /* Its weapon is readied first. */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    size_t sword = give(a, 0x12, false, 0);
    CHECK(cok_combat_computer(&game, a) && a->slots[0] == sword);
    /* An invisible enemy (0x19), which cannot be attacked, is taken on the
     * second pass untested (flag): the fight, where one a square away is
     * attacked all the same. */
    a = duel(&m, 6, 5);
    game.vm.character = a->record;
    CHECK(cok_character_add_effect(m, 0x19, 0, 0, false) != NULL);
    game.combat.last_step = 3;
    CHECK(cok_combat_computer(&game, a) && game.combat.last_step == 8 &&
          LOGGED("print: A;print: Attacks;"));
}

/* Cases for the review's mutants. */
static void test_review(void)
{
    cok_character *m;
    bool back, fits, used, cast;
    /* Space on a monster's turn gives the party back, not that turn. */
    cok_character *a = duel(&m, 7, 5);
    s.keys = " ";
    s.length = 1;
    s.pending = true;
    CHECK(cok_ai_keys(&game, m, &back) && !back && m->combat->initiative == 5);
    CHECK(a->record[0x18b] == 0 && m->record[0x18b] == 1);
    /* The party's own side throws against its area spells at -2: B beside
     * the enemy, with a d20 of 13, fails a throw it would make at +0, so
     * Sleep does not suit; one lower it makes. */
    a = duel(&m, 7, 5);
    cok_character *b = record('B', 0, 7, 6);
    game.vm.character = a->record;
    a->record[0xfe] = 1;
    /* The lowest throw a d20 of 13 misses at +0, with B's own bonus. */
    unsigned throw_ = 0;
    for (unsigned t = 1; t <= 20 && throw_ == 0; ++t) {
        b->record[0xd4] = (uint8_t)t;
        bool made;
        game.vm.seed = seed_seq(1, (const unsigned[]){20, 13, 13});
        CHECK(cok_effects_save(&game.effects, b, 4, 0, &made));
        if (!made) throw_ = t;
    }
    CHECK(throw_ > 0);
    b->record[0xd4] = (uint8_t)(throw_ - 2); /* 13 makes it at +0, not at -2 */
    game.vm.seed = seed_seq(1, (const unsigned[]){20, 13, 13});
    CHECK(cok_ai_spell_fits(&game, a, 0x15, 4, &fits) && !fits);
    b->record[0xd4] = (uint8_t)(throw_ - 3); /* made at -2 */
    game.vm.seed = seed_seq(1, (const unsigned[]){20, 13, 13});
    CHECK(cok_ai_spell_fits(&game, a, 0x15, 4, &fits) && fits);
    /* Of two items that suit, the first. */
    duel(&m, 7, 5);
    game.vm.character = m->record;
    m->record[0xfe] = 1;
    size_t w1 = give(m, 0x34, true, 0), w2 = give(m, 0x34, true, 0);
    m->items[w1 - 1][0x3d] = 0x0f;
    m->items[w2 - 1][0x3d] = 0x01;
    game.vm.seed = seed_seq(1, (const unsigned[]){7, 7, 7});
    CHECK(cok_ai_use_item(&game, m, &used) && used && LOGGED("uses an item's spell 0x0f"));
    CHECK(!LOGGED("spell 0x01"));
    /* The draw d(spells) - 1: a 1 takes the first memorized, 0x55
     * (priority 7), not 0x58 (6). */
    duel(&m, 7, 5);
    game.vm.character = m->record;
    m->record[0x1f] = 0x55;
    m->record[0x20] = 0x58;
    game.vm.seed = seed_seq(2, (const unsigned[]){7, 1, 7, 2, 1, 1});
    uint32_t start = game.vm.seed;
    CHECK(cok_ai_cast(&game, m, &cast) && cast && LOGGED("M casts spell 0x55 (432f:28bd)"));
    CHECK(game.vm.seed == after(start, 2, (const unsigned[]){7, 2})); /* the first draw */
    /* Three hands held, the shield cursed: the chosen weapon is toggled,
     * here refused ("already using"), the shield never ("It's Cursed"). */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 5);
    game.vm.character = a->record;
    size_t two = give(a, 0x10, true, 0), shield = give(a, 0x25, true, 0);
    a->items[two - 1][0x36] = a->items[shield - 1][0x36] = 1;
    char why[300];
    a->items[shield - 1][0x34] = 1;
    a->items[two - 1][0x34] = 1;
    CHECK(cok_character_stats(a, &game.item_types, why, sizeof why) && a->record[0x17b] == 3);
    give(a, 0x04, false, 0);
    CHECK(cok_ai_choose_weapon(&game, a) && a->slots[0] == two && a->slots[1] == shield);
    CHECK(LOGGED("print: already using") && !LOGGED("It's Cursed"));
    /* Space during a fleeing step: the turn given back (0x14) stops the
     * steps. */
    a = duel(&m, 9, 5);
    game.vm.character = a->record;
    a->combat->fleeing = 1;
    a->combat->movement = 12;
    a->record[0x198] = 12;
    s.keys = " ";
    s.length = 1;
    s.pending = true;
    bool over;
    CHECK(cok_ai_fight(&game, a, &over) && over && a->combat->initiative == 0x14);
    CHECK(game.combat.combatant[1].x == 5 && game.combat.combatant[1].y == 5);
}

/* A battle set up and ended, for the combat tile set and the screen's
 * pictures. */
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
    game.vm.mode = 5;
}

int main(void)
{
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .key_pending = key_pending, .context = &s};
    if (!cok_adventure_open(&game, "Assets", &keys, &hooks)) {
        puts("ai: Assets not found; tests skipped");
        return 0;
    }
    load_screen();
    test_tables();
    test_pattern();
    test_keys();
    test_morale();
    test_weapon_score();
    test_choose_weapon();
    test_guard();
    test_can_step();
    test_step();
    test_flee_step();
    test_stuck();
    test_fight();
    test_spells();
    test_cast();
    test_use_item();
    test_turn_undead();
    test_destroy();
    test_computer();
    test_auto();
    test_pattern_more();
    test_keys_more();
    test_morale_more();
    test_weapons_more();
    test_spells_more();
    test_cast_more();
    test_turning_more();
    test_turning_limit();
    test_step_more();
    test_fight_more();
    test_step_paralysed();
    test_computer_more();
    test_review();
    cok_adventure_close(&game);
    puts("ai tests passed");
    return 0;
}
