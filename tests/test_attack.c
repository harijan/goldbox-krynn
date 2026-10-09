#include "attack.h"

#include "adventure.h"
#include "cast.h"
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

/* While rows are tracked, each line logged is also put in rows with the
 * lowest text row (from 1, of 21) in the side panel that is not blank: a
 * name said is drawn on its row, below it cleared, before the lines. */
static bool track_rows;
static char rows[1 << 14];

static int lowest_row(void)
{
    const cok_picture *p = &game.screen;
    size_t stride = (size_t)p->units * 4;
    for (int row = 21; row >= 1; --row)
        for (size_t y = (size_t)row * 8; y < (size_t)row * 8 + 8; ++y)
            for (size_t b = 0x17 * 4; b < 0x27 * 4; ++b)
                if (p->pixels[y * stride + b] != 0) return row;
    return 0;
}

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g;
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "%s: %s;", kind, text);
    if (!track_rows) return;
    used = strlen(rows);
    snprintf(rows + used, sizeof rows - used, "%s@%d;", text, lowest_row());
}

/* The waits asked for, each as ";N;". */
static char delays[1 << 14];

static void delay_line(cok_adventure *g, unsigned ms, void *context)
{
    (void)g, (void)context;
    size_t used = strlen(delays);
    snprintf(delays + used, sizeof delays - used, used == 0 ? ";%u;" : "%u;", ms);
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
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.input_ended = false;
    game.combat_stub = COK_COMBAT_UNPORTED;
    game.effects.in_battle = true;
    game.speed = 0;
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
    s.keys = keys;
    s.at = 0;
    s.length = strlen(keys);
    s.log[0] = '\0';
    delays[0] = '\0';
    rows[0] = '\0';
    track_rows = false;
}

/* A record named by a letter, one cell, dexterity 12, 10 hit points, on
 * side, armour class 10 (60 - 50), THAC0 20 (60 - 40), one attack a round
 * of 1d2 with slot 1, with a zeroed combat record, a combatant at x, y,
 * its stats worked out. */
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
    r[0x10b] = 2;
    r[0x10d] = 1;
    r[0x10f] = 2;
    r[0x113] = 50;
    r[0x189] = 1;
    r[0x18a] = side;
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL && cok_party_append(&game.party, c));
    c->combat->not_party = side;
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

/* Give c an item of type, readied if ready, with count (+0x39). */
static size_t give(cok_character *c, uint8_t type, bool ready, uint8_t count)
{
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = type;
    item[0x31] = type;
    item[0x34] = ready;
    item[0x39] = count;
    CHECK(cok_character_insert_item(c, c->item_count, item));
    char why[300];
    CHECK(cok_character_stats(c, &game.item_types, why, sizeof why));
    c->record[0x18f] = 1;
    return c->item_count;
}

/* A seed whose first value of range, from 1, is in lo-hi. */
static uint32_t seed_for(uint16_t range, unsigned lo, unsigned hi)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        unsigned d = cok_tp_random(&seed, range) + 1u;
        if (d >= lo && d <= hi) return start;
    }
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

static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("attack: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    for (size_t i = 0; i < cok_attack_table_count; ++i) {
        const cok_ds_table *t = &cok_attack_tables[i];
        for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    }
}

static void test_hit(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    bool hit;
    /* d20 + THAC0 (as 60 - THAC0, 40) reaches the armour class given. */
    game.vm.seed = seed_for(20, 10, 10);
    CHECK(cok_combat_hit(&game, a, m, 50, &hit) && hit && game.effects.rolls.attack_roll == 10);
    game.vm.seed = seed_for(20, 9, 9);
    CHECK(cok_combat_hit(&game, a, m, 50, &hit) && !hit);
    /* A 1 misses whatever; a 20 counts as 100. */
    game.vm.seed = seed_for(20, 1, 1);
    CHECK(cok_combat_hit(&game, a, m, 0, &hit) && !hit);
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(cok_combat_hit(&game, a, m, 140, &hit) && hit && game.effects.rolls.attack_roll == 100);
    /* Var 0x7f71 for the party's side, 0x7f70 for the other, signed. */
    game.vm.mem7c00[0x371] = 0xff;
    game.vm.mem7c00[0x370] = 5;
    game.vm.seed = seed_for(20, 10, 10);
    CHECK(cok_combat_hit(&game, a, m, 50, &hit) && !hit);
    game.vm.seed = seed_for(20, 10, 10);
    CHECK(cok_combat_hit(&game, m, a, 55, &hit) && hit);
    game.vm.seed = seed_for(20, 10, 10);
    CHECK(cok_combat_hit(&game, m, a, 56, &hit) && !hit);
    game.vm.mem7c00[0x371] = game.vm.mem7c00[0x370] = 0;
    /* The attacker's effects (event 0x0a), then the target's (0x10): a
     * displacement (0x59) makes the roll 0xff, negative, a miss. */
    CHECK(cok_character_add_effect(a, 0x01, 0, 0, false) != NULL); /* bless: +1 */
    game.vm.seed = seed_for(20, 9, 9);
    CHECK(cok_combat_hit(&game, a, m, 50, &hit) && hit);
    CHECK(cok_character_add_effect(m, 0x59, 0, 0, false) != NULL);
    game.effects.rolls.round = 1;
    game.vm.seed = seed_for(20, 19, 19);
    CHECK(cok_combat_hit(&game, a, m, 0, &hit) && !hit && game.effects.rolls.attack_roll == 0xff);
    /* The attacker's invisibility goes, every one. */
    CHECK(cok_character_add_effect(a, 0x19, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(a, 0x19, 5, 0, false) != NULL);
    CHECK(cok_combat_hit(&game, a, m, 50, &hit) && cok_character_find_effect(a, 0x19) == NULL);
}

static void test_book(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    /* The attacker lies west (6) of the defender: facing it, no turn;
     * facing away (2), 4; facing 1, 5, folded to 3. */
    m->combat->facing = 6;
    CHECK(cok_combat_book(&game, a, m) && m->combat->hits == 1 && m->combat->turning == 0);
    m->combat->facing = 2;
    CHECK(cok_combat_book(&game, a, m) && m->combat->hits == 2 && m->combat->turning == 4);
    m->combat->facing = 1;
    CHECK(cok_combat_book(&game, a, m) && m->combat->turning == 7);
    m->combat->facing = 7;
    CHECK(cok_combat_book(&game, a, m) && m->combat->turning == 0);
    /* A facing past 15 takes the idiv's negative remainder: -1, 0xff as a
     * byte, folded to 9. */
    m->combat->facing = 15;
    m->combat->turning = 0;
    CHECK(cok_combat_book(&game, a, m) && m->combat->turning == 1);
}

static void test_backstab(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    bool b;
    a->record[0xff] = 1; /* a thief */
    m->combat->hits = 2;
    m->combat->facing = 2; /* east, as the attacker sees it */
    CHECK(cok_combat_backstab(&game, a, m, &b) && b);
    m->combat->hits = 1;
    CHECK(cok_combat_backstab(&game, a, m, &b) && !b);
    m->combat->hits = 2;
    m->combat->facing = 6;
    CHECK(cok_combat_backstab(&game, a, m, &b) && !b);
    m->combat->facing = 2;
    m->record[0xcf] = 2;
    CHECK(cok_combat_backstab(&game, a, m, &b) && !b);
    m->record[0xcf] = 0x81; /* & 0x7f: one cell */
    CHECK(cok_combat_backstab(&game, a, m, &b) && b);
    /* A thief only, a former one only a human may use. */
    a->record[0xff] = 0;
    CHECK(cok_combat_backstab(&game, a, m, &b) && !b);
    a->record[0xff] = 1;
    /* Weapons: none, 0x43, 3, 4 and 0x11-0x13. */
    static const struct { uint8_t type; bool stabs; } weapons[] = {
        {0x43, true}, {3, true}, {4, true}, {0x10, false}, {0x11, true}, {0x13, true},
        {0x14, false}, {8, false},
    };
    for (size_t k = 0; k < sizeof weapons / sizeof *weapons; ++k) {
        give(a, weapons[k].type, true, 0);
        CHECK(a->slots[0] != 0 && cok_combat_backstab(&game, a, m, &b) && b == weapons[k].stabs);
        cok_character_remove_item(a, a->item_count - 1);
        char why[300];
        CHECK(cok_character_stats(a, &game.item_types, why, sizeof why));
    }
    /* A square away only. */
    game.combat.combatant[2].x = 7;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_combat_backstab(&game, a, m, &b) && !b);
}

static void test_range(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 10, 5);
    uint8_t ac = 50;
    /* Melee: never. */
    CHECK(cok_combat_range(&game, a, m, &ac) && ac == 50);
    /* A long bow (range 22): steps of 7; 5 squares, none; 12, medium
     * (+2); 15, long (+2 + 3); a bow of range 7 (a step of 2): 5
     * squares, +2 then +3. */
    give(a, 0x16, true, 0);
    CHECK(cok_combat_range(&game, a, m, &ac) && ac == 50);
    game.combat.combatant[2].x = 20;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_combat_range(&game, a, m, &ac) && ac == 55);
    ac = 50;
    game.combat.combatant[2].x = 17;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_combat_range(&game, a, m, &ac) && ac == 52);
    game.item_types.type[0x16][12] = 7;
    ac = 50;
    game.combat.combatant[2].x = 10;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(cok_combat_range(&game, a, m, &ac) && ac == 55);
    /* At 4 squares, a step of 2: +2 only. */
    game.combat.combatant[2].x = 9;
    CHECK(cok_combat_occupy(&game.combat));
    ac = 50;
    CHECK(cok_combat_range(&game, a, m, &ac) && ac == 52);
    game.item_types.type[0x16][12] = 22;
}

static void test_damage_roll(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    cok_rolls *r = &game.effects.rolls;
    /* 1d2 + (signed) bonus, as a byte: -3 wraps. */
    a->record[0x195] = 0xfd;
    game.effects.rolls.damage_type = 9;
    game.vm.seed = seed_for(2, 1, 1);
    CHECK(cok_combat_damage_roll(&game, a, m, 1) && r->amount == 0xfe && r->dice == 1);
    CHECK(r->damage_type == 0);
    a->record[0x195] = 3;
    a->record[0x191] = 3;
    a->record[0x193] = 4;
    uint32_t seed = game.vm.seed = 77;
    unsigned want = 3;
    for (int k = 0; k < 3; ++k) want += cok_tp_random(&seed, 4) + 1u;
    CHECK(cok_combat_damage_roll(&game, a, m, 1) && r->amount == want && r->dice == 3);
    CHECK(game.vm.seed == seed);
    /* Slot 2's dice. */
    a->record[0x192] = 1;
    a->record[0x194] = 1;
    a->record[0x196] = 9;
    CHECK(cok_combat_damage_roll(&game, a, m, 2) && r->amount == 10);
    /* Events 4 (attacker: 0x1d takes a quarter) and 5 (target: 0x3b
     * none). */
    CHECK(cok_character_add_effect(a, 0x1d, 0, 0, false) != NULL);
    CHECK(cok_combat_damage_roll(&game, a, m, 2) && r->amount == 8);
    CHECK(cok_character_add_effect(m, 0x3b, 0, 0, false) != NULL);
    CHECK(cok_combat_damage_roll(&game, a, m, 2) && r->amount == 0);
    cok_effects_remove(&game.effects, m, NULL, 0x3b);
    cok_effects_remove(&game.effects, a, NULL, 0x1d);
    /* A backstab: times 2 + (thief levels - 1) / 4, a byte. */
    a->record[0xff] = 13;
    m->combat->hits = 2;
    m->combat->facing = 2;
    a->record[0x196] = 99;
    CHECK(cok_combat_damage_roll(&game, a, m, 2) && r->amount == (uint8_t)(100 * 5));
    a->record[0xff] = 4;
    CHECK(cok_combat_damage_roll(&game, a, m, 2) && r->amount == 200);
    a->record[0xff] = 5;
    CHECK(cok_combat_damage_roll(&game, a, m, 2) && r->amount == (uint8_t)300);
    /* No slot past the two. */
    CHECK(!cok_combat_damage_roll(&game, a, m, 3) && game.vm.status == COK_ECL_UNDEFINED);
}

/* A seed for an attack roll of hit, then damage dice of sides of
 * damage. */
static uint32_t swing(unsigned roll, unsigned sides, unsigned damage)
{
    unsigned t[6] = {20, roll, roll, sides, damage, damage};
    return seed_seq(sides > 0 ? 2 : 1, t);
}

static void test_strike(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    cok_rolls *r = &game.effects.rolls;
    bool over;
    /* One attack of 1d2 against armour class 10 (50): a 10 hits for 2. */
    a->combat->initiative = 3;
    game.vm.seed = swing(10, 2, 2);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && over);
    CHECK(m->record[0x197] == 8 && r->hits[0] == 1 && c->swings[1] == 1 && c->swings[2] == 0);
    CHECK(a->combat->attacked == 1 && a->combat->attack_slot == 1 && a->record[0x18f] == 0);
    CHECK(a->combat->initiative == 0 && a->combat->sweeps == 0);
    CHECK(LOGGED("print: A;print: Attacks;attack: M Hitting for 2 points of damage;"));
    CHECK(LOGGED("sound: 7;"));
    /* A miss says "and Misses" once, sound 9. */
    s.log[0] = '\0';
    a->record[0x18f] = 2;
    a->combat->attack_slot = 2;
    unsigned two_misses[] = {20, 2, 2, 20, 3, 3};
    game.vm.seed = seed_seq(2, two_misses);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && over);
    CHECK(r->hits[0] == 0 && c->swings[1] == 2 && m->record[0x197] == 8);
    CHECK(LOGGED("sound: 9;print: A;print: Attacks;attack: M and Misses;"));
    CHECK(strstr(strstr(s.log, "Misses") + 1, "Misses") == NULL);
    /* "1 point"; slot 2's attacks first, then slot 1's. */
    s.log[0] = '\0';
    a->record[0x18f] = 1;
    a->record[0x190] = 1;
    a->record[0x192] = 1;
    a->record[0x194] = 1;
    a->combat->attack_slot = 2;
    unsigned both[] = {20, 15, 15, 1, 1, 1, 20, 15, 15, 2, 2, 2};
    game.vm.seed = seed_seq(4, both);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && over);
    CHECK(r->hits[0] == 1 && r->hits[1] == 1 && c->swings[1] == 1 && c->swings[2] == 1);
    CHECK(LOGGED("attack: M Hitting for 1 point of damage;"));
    CHECK(LOGGED("attack: M Hitting for 2 points of damage;"));
    CHECK(m->record[0x197] == 5 && a->combat->attack_slot == 1);
    /* The target drops: the attacks left stay, and the turn goes on. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->record[0x197] = 2;
    a->record[0x18f] = 2;
    a->combat->initiative = 3;
    game.vm.seed = swing(10, 2, 2);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && !over);
    CHECK(a->record[0x18f] == 1 && a->combat->initiative == 3 && m->record[0x188] == 4);
    CHECK(LOGGED("print: M;print: goes down;"));
    CHECK(c->combatant[2].size == 0 && c->sides[1] == 0);
    /* A target that can no longer act is not told the damage. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->record[0x189] = 0;
    game.vm.seed = swing(10, 2, 2);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && over && !LOGGED("Hitting"));
    CHECK(r->hits[0] == 1 && m->record[0x197] == 10);
    /* Only a hit stops the swings at a target down: a miss swings on. */
    a->record[0x18f] = 3;
    a->combat->attack_slot = 2;
    unsigned miss_hit[] = {20, 2, 2, 20, 15, 15, 2, 1, 1};
    game.vm.seed = seed_seq(3, miss_hit);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && !over);
    CHECK(c->swings[1] == 2 && r->hits[0] == 1 && a->record[0x18f] == 1);
    /* A helpless target: slain from the slot in progress down to one with
     * attacks left; hit points + 5, dying; no roll. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x33, 0, 0, false) != NULL);
    a->record[0x18f] = 1;
    a->record[0x190] = 0;
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_strike(&game, a, m, false, &over) && over && game.vm.seed == seed);
    CHECK(a->combat->attack_slot == 1 && c->swings[1] == 1 && a->record[0x18f] == 0);
    CHECK(m->record[0x188] == 5 && m->combat->dying == 5);
    CHECK(LOGGED("print: A;print: slays helpless;attack: M with one cruel blow;"));
    CHECK(LOGGED("print: M;print: goes down;attack: and is Dying;"));
    /* With none left, slot 0 counts, +0x18e, the armour class from behind
     * (DS:7194). */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x34, 0, 0, false) != NULL);
    a->record[0x18f] = 0;
    CHECK(cok_combat_strike(&game, a, m, false, &over) && a->combat->attack_slot == 0);
    CHECK(c->swings[0] == 1);
    /* And with that 0 too, past the record: refused. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x34, 0, 0, false) != NULL);
    a->record[0x18f] = 0;
    a->record[0x18e] = 0;
    CHECK(!cok_combat_strike(&game, a, m, false, &over) && game.vm.status == COK_ECL_UNDEFINED);
    /* A large target: the weapon's dice against large ones (bytes 2-4) for
     * slot 1, the small ones' bonus (byte 11) taken off. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    give(a, 0x12, true, 0); /* long sword: 1d8 small, 1d12 large */
    m->record[0xcf] = 2;
    m->record[0x197] = 100;
    a->record[0x195] = 4;
    const uint8_t *type = game.item_types.type[0x12];
    game.vm.seed = swing(15, type[3], 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over));
    CHECK(a->record[0x191] == type[2] && a->record[0x193] == type[3]);
    CHECK(a->record[0x195] == (uint8_t)(4 - (int8_t)type[11] + (int8_t)type[4]));
    CHECK(m->record[0x197] == (uint8_t)(100 - a->record[0x195] - 1));
    /* From behind: hit before, facing as the attacker's direction to it,
     * turned more than 4; its armour class from behind. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->combat->hits = 2;
    m->combat->facing = 2;
    m->combat->turning = 5;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over));
    CHECK(LOGGED("attack: M (from behind) Hitting for 1 point of damage;"));
    /* Turned 4: not from behind; the 10 against 50 still hits. */
    s.log[0] = '\0';
    m->combat->turning = 4;
    a->record[0x18f] = 1;
    a->combat->attack_slot = 2;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && LOGGED("attack: M Hitting"));
    /* Asked from behind, it is. */
    s.log[0] = '\0';
    a->record[0x18f] = 1;
    a->combat->attack_slot = 2;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, true, &over) && LOGGED("attack: M (from behind) Hit"));
    /* A backstab: from behind less 4, "-Backstabs-". */
    s.log[0] = '\0';
    a->record[0xff] = 1;
    a->record[0x18f] = 1;
    a->combat->attack_slot = 2;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && LOGGED("print: -Backstabs-;"));
    CHECK(LOGGED("attack: M Hitting for 2 points of damage;"));
    /* Hurt with a spell being cast: may not cast, "lost a spell", its
     * turn ends. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->combat->spell = 3;
    m->combat->initiative = 4;
    m->combat->may_cast = 1;
    m->record[0x1e] = 7;
    m->record[0x1f] = 3;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over));
    CHECK(m->combat->may_cast == 0 && m->combat->spell == 0 && m->combat->initiative == 0);
    CHECK(m->record[0x1e] == 7 && m->record[0x1f] == 0 && LOGGED("print: M;print: lost a spell;"));
    /* Events 2 (slot 1) on the attacker after damage, the target up: a
     * poison (0x40) makes it save. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(a, 0x40, 0, 0xff, false) != NULL);
    m->record[0xd0] = 0xff; /* only a 20 saves */
    unsigned poisoned[] = {20, 10, 10, 2, 1, 1, 20, 2, 19};
    game.vm.seed = seed_seq(3, poisoned);
    a->combat->target = m->record;
    CHECK(cok_combat_strike(&game, a, m, false, &over));
    CHECK(LOGGED("print: M;print: is Poisoned;") && LOGGED("print: is killed;"));
    CHECK(m->record[0x188] == 6 && cok_character_find_effect(m, 0x37) != NULL);
}

static void test_attack(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    bool over;
    /* The target off the view turns away from the attacker; the round
     * limit is the round + 15; the attacker aims at it; selected meanwhile
     * and back after. */
    c->view_x = 30;
    c->view_y = 15;
    CHECK(cok_combat_occupy(c));
    game.effects.rolls.round = 3;
    game.vm.character = m->record;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over) && over);
    CHECK(m->combat->facing == 2 && a->combat->facing == 2 && a->combat->target == m->record);
    CHECK(c->round_limit == 18 && game.vm.character == m->record);
    CHECK(c->show_actions && a->combat->initiative == 0);
    /* Shown, it faces the attacker. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    c->view_x = 2;
    c->view_y = 2;
    CHECK(cok_combat_occupy(c));
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over) && m->combat->facing == 6);
    /* Hit twice before and shown: it flips, then is drawn back; not
     * shown, it stays. Behind, it keeps its facing. */
    m->combat->hits = 2;
    m->combat->facing = 1;
    a->record[0x18f] = 1;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over) && m->combat->facing == 1);
    c->view_x = 30;
    CHECK(cok_combat_occupy(c));
    a->record[0x18f] = 1;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over) && m->combat->facing == 1);
    /* No attacks left: no strike, but the turn ends. */
    a->combat->initiative = 3;
    a->record[0x18f] = 0;
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over) && over && game.vm.seed == seed);
    CHECK(a->combat->initiative == 0);
    /* Arrows from a long bow: they fly, and the attacks made with slot 1
     * are taken from their count; at 0 they go. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 12, 5);
    m->record[0x197] = 100;
    give(a, 0x16, true, 0);
    size_t arrows = give(a, 0x1e, true, 3);
    CHECK(a->slots[11] == arrows);
    a->record[0x18f] = 2;
    s.log[0] = '\0';
    unsigned shots[] = {20, 2, 2, 20, 2, 2};
    game.vm.seed = seed_seq(2, shots);
    CHECK(cok_combat_attack(&game, a, m, false, arrows, &over));
    CHECK(a->items[arrows - 1][0x39] == 1 && LOGGED("sound: 12;sound: 12;"));
    a->record[0x18f] = 1;
    game.vm.seed = seed_seq(1, shots);
    CHECK(cok_combat_attack(&game, a, m, false, arrows, &over));
    CHECK(a->item_count == 1 && a->slots[11] == 0 && game.pool.item_count == 0);
    /* A thrown weapon (a hand axe, flags 0x14) at 0 goes into the pool,
     * unreadied, and is the missile recovered; the next goes after it.
     * Darts (flags 0x1a) are not thrown so: they go. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 8, 5);
    m->record[0x197] = 100;
    uint8_t loot[COK_ITEM_SIZE] = {0};
    loot[0x2e] = 0x50;
    CHECK(cok_pool_insert(&game.pool, 0, loot) && cok_pool_insert(&game.pool, 1, loot));
    size_t darts = give(a, 0x05, true, 1);
    a->record[0x18f] = 1;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, darts, &over));
    CHECK(a->item_count == 0 && game.pool.item_count == 2 && game.pool.missile == 0);
    size_t axe = give(a, 0x02, true, 1);
    a->record[0x18f] = 1;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, axe, &over));
    CHECK(a->item_count == 0 && game.pool.item_count == 3 && game.pool.missile == 3);
    CHECK(game.pool.items[2][0x2e] == 0x02 && game.pool.items[2][0x34] == 0);
    game.pool.missile = 1;
    axe = give(a, 0x02, true, 0);
    a->record[0x18f] = 1;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, axe, &over));
    CHECK(game.pool.item_count == 4 && game.pool.missile == 2 && game.pool.items[1][0x2e] == 2);
    /* A spiritual hammer (+0x3d 0x17) goes, not into the pool. */
    size_t hammer = give(a, 0x06, true, 0); /* a hammer: thrown, flags 0x14 */
    a->items[hammer - 1][0x3d] = 0x17;
    a->record[0x18f] = 1;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, hammer, &over));
    CHECK(a->item_count == 0 && game.pool.item_count == 4);
    /* A hoopak's is never used up. */
    size_t hoopak = give(a, 0x43, true, 0);
    a->record[0x18f] = 1;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, hoopak, &over) && a->item_count == 1);
    /* The count is a byte: two attacks made with one left wrap it. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 12, 5);
    m->record[0x197] = 100;
    give(a, 0x16, true, 0);
    arrows = give(a, 0x1e, true, 1);
    a->record[0x18f] = 2;
    game.vm.seed = seed_seq(2, shots);
    CHECK(cok_combat_attack(&game, a, m, false, arrows, &over));
    CHECK(a->items[arrows - 1][0x39] == 0xff);
    /* A sling's stone flies with no item given. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 5);
    give(a, 0x1c, true, 0);
    a->record[0x18f] = 1;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over) && LOGGED("sound: 12;sound: 6;"));
    /* An item that is not the attacker's: refused. */
    CHECK(!cok_combat_attack(&game, a, m, false, 5, &over));
}

static void test_sweep(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    cok_character *m1 = record('M', 1, 6, 5), *m2 = record('N', 1, 6, 6), *m3 = record('O', 1, 5, 6);
    record('P', 1, 4, 4)->record[0xd6] = 2;
    bool swept, over;
    (void)over;
    /* Fewer attacks than sweeps, a target below 1 hit die a square away,
     * and more such enemies around than attacks. */
    a->combat->sweeps = 2;
    a->record[0x18f] = 1;
    m3->record[0x197] = m2->record[0x197] = m1->record[0x197] = 50;
    unsigned two[] = {20, 2, 2, 20, 2, 2};
    game.vm.seed = seed_seq(2, two);
    CHECK(cok_combat_sweep(&game, a, m3, &swept) && swept);
    CHECK(LOGGED("print: A;print: sweeps;"));
    /* The target first, then the next listed, as many as the sweeps. */
    CHECK(m3->combat->hits == 1 && m3->combat->target == NULL);
    int hit = (m1->combat->hits != 0) + (m2->combat->hits != 0);
    CHECK(hit == 1 && game.combat.enemies[1] == 4);
    /* Not below 1 hit die, as many attacks as sweeps, or no more enemies
     * than attacks: none. */
    m1->record[0xd6] = 1;
    CHECK(cok_combat_sweep(&game, a, m1, &swept) && !swept);
    m1->record[0xd6] = 0;
    a->record[0x18f] = 2;
    CHECK(cok_combat_sweep(&game, a, m1, &swept) && !swept);
    a->combat->sweeps = 9;
    a->record[0x18f] = 3;
    CHECK(cok_combat_sweep(&game, a, m1, &swept) && !swept);
    /* Two squares away: none. */
    reset("");
    a = record('A', 0, 5, 5);
    m1 = record('M', 1, 7, 5);
    record('N', 1, 6, 6);
    record('O', 1, 5, 6);
    a->combat->sweeps = 3;
    CHECK(cok_combat_sweep(&game, a, m1, &swept) && !swept);
}

static void test_can_attack(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    bool can;
    CHECK(cok_combat_can_attack(&game, a, NULL, &can) && !can);
    CHECK(cok_combat_can_attack(&game, a, a->record, &can) && can);
    CHECK(cok_combat_can_attack(&game, a, m->record, &can) && can);
    /* Invisible to one selected without 0x18: not; its aim kept. */
    game.vm.character = a->record;
    a->combat->target = a->record;
    CHECK(cok_character_add_effect(m, 0x19, 0, 0, false) != NULL);
    CHECK(cok_combat_can_attack(&game, a, m->record, &can) && !can);
    CHECK(a->combat->target == a->record && game.effects.rolls.untargetable == 1);
    CHECK(cok_character_add_effect(a, 0x18, 0, 0, false) != NULL);
    CHECK(cok_combat_can_attack(&game, a, m->record, &can) && can);
    /* Blink (0x25): one that has had its turn cannot be attacked. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x25, 0, 0, false) != NULL);
    m->combat->initiative = 1;
    CHECK(cok_combat_can_attack(&game, a, m->record, &can) && can);
    m->combat->initiative = 0;
    CHECK(cok_combat_can_attack(&game, a, m->record, &can) && !can);
    CHECK(game.effects.rolls.attack_roll == 0xff);
}

static void test_pick_target(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5), *n = record('N', 1, 7, 7);
    bool found;
    /* A target that can be attacked is kept, with no roll. */
    a->combat->target = n->record;
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_pick_target(&game, a, 0xff, false, false, &found) && found);
    CHECK(a->combat->target == n->record && game.vm.seed == seed);
    /* Forced, or one on its side, or that cannot act: a d(the enemies in
     * range), nearest first. */
    for (int k = 0; k < 3; ++k) {
        a->combat->target = k == 1 ? a->record : n->record;
        n->record[0x189] = k == 2 ? 0 : 1;
        game.vm.seed = seed_for(2, 1, 1);
        CHECK(cok_combat_pick_target(&game, a, 0xff, false, k == 0, &found) && found);
        CHECK(a->combat->target == m->record);
        game.vm.seed = seed_for(2, 2, 2);
        a->combat->target = NULL;
        CHECK(cok_combat_pick_target(&game, a, 0xff, false, false, &found) && found);
        CHECK(a->combat->target == n->record);
    }
    /* Within range only: none. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 5);
    seed = game.vm.seed;
    CHECK(cok_combat_pick_target(&game, a, 1, false, false, &found) && !found);
    CHECK(a->combat->target == NULL && game.vm.seed == seed && game.combat.see_all == 0);
    /* One that cannot be attacked comes off the list; with flag, the
     * second pass, sight not blocked, takes it all the same. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    game.vm.character = a->record;
    CHECK(cok_character_add_effect(m, 0x19, 0, 0, false) != NULL);
    game.vm.seed = 5;
    CHECK(cok_combat_pick_target(&game, a, 0xff, false, false, &found) && !found);
    seed = 5;
    cok_tp_random(&seed, 1);
    cok_tp_random(&seed, 1);
    CHECK(game.vm.seed == seed);
    CHECK(cok_combat_pick_target(&game, a, 0xff, true, false, &found) && found);
    CHECK(a->combat->target == m->record && game.combat.see_all == 0);
    /* Forced, the second pass keeps sight blocked, and flag does not
     * take it. */
    CHECK(cok_combat_pick_target(&game, a, 0xff, true, true, &found) && !found);
    /* With 0x5b, the one who yelled, if listed first, at the first try
     * with no roll. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    n = record('N', 1, 9, 5);
    CHECK(cok_character_add_effect(a, 0x5b, 0, 0, false) != NULL);
    game.combat.yelled = n->record;
    seed = game.vm.seed;
    CHECK(cok_combat_pick_target(&game, a, 0xff, false, false, &found) && found);
    CHECK(a->combat->target == n->record && game.vm.seed == seed);
}

static void test_step(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 5, 5);
    record('M', 1, 20, 20);
    /* Floor costs 1: twice straight, three times diagonally; its hits
     * and turning cleared, sound 0x0a. */
    a->combat->movement = 6;
    a->combat->hits = 2;
    a->combat->turning = 3;
    CHECK(cok_combat_step(&game, a, 2) && a->combat->movement == 4);
    CHECK(c->combatant[1].x == 6 && c->combatant[1].y == 5 && c->occupant[5][6] == 1);
    CHECK(c->occupant[5][5] == 0 && a->combat->hits == 0 && a->combat->turning == 0);
    CHECK(LOGGED("sound: 10;"));
    CHECK(cok_combat_step(&game, a, 3) && a->combat->movement == 1);
    /* More than is left: none. A table (0x1a) costs 2. */
    c->cells[7][7] = 0x1a;
    a->combat->movement = 5;
    CHECK(cok_combat_step(&game, a, 4) && a->combat->movement == 1);
    a->combat->movement = 3;
    c->cells[8][7] = 0x1a;
    CHECK(cok_combat_step(&game, a, 4) && a->combat->movement == 0);
    /* A wall costs 0xff: twice, 0xfe as a byte; three times, 0xfd. */
    c->cells[9][7] = 0x01;
    a->combat->movement = 0xfe;
    CHECK(cok_combat_step(&game, a, 4) && a->combat->movement == 0);
    /* Off the map: the heap around it. */
    c->combatant[1].x = 49;
    c->combatant[1].y = 24;
    CHECK(cok_combat_occupy(c));
    CHECK(!cok_combat_step(&game, a, 3) && game.vm.status == COK_ECL_UNDEFINED);
    /* An enemy guarding beside the new cell attacks it, once, and stops
     * guarding; one that cannot act or is helpless after has no movement
     * left. */
    reset("");
    a = record('A', 0, 5, 5);
    cok_character *g = record('G', 1, 7, 5), *h = record('H', 1, 7, 6);
    g->combat->guarding = 1;
    h->combat->guarding = 1;
    CHECK(cok_character_add_effect(h, 0x1f, 0, 0, false) != NULL);
    a->combat->movement = 8;
    game.vm.seed = swing(15, 2, 2);
    CHECK(cok_combat_step(&game, a, 2));
    CHECK(g->combat->guarding == 0 && h->combat->guarding == 1 && a->combat->hits == 1);
    CHECK(LOGGED("print: G;print: Attacks;attack: A Hitting for 2 points of damage;"));
    CHECK(a->combat->movement == 6);
    CHECK(cok_character_add_effect(a, 0x34, 0, 0, false) != NULL);
    CHECK(cok_combat_step(&game, a, 6) && a->combat->movement == 0);
    /* Not a combatant: refused. */
    c->combatant[1].character = NULL;
    CHECK(!cok_combat_step(&game, a, 2) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_opportunity(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 5, 5), *e = record('E', 1, 6, 5), *f = record('F', 1, 6, 6);
    (void)c;
    /* Stepping west leaves E (east) and F (south-east): each attacks it
     * once from behind, E's slot 1 given an attack it lacked. */
    e->combat->initiative = 3;
    f->combat->initiative = 3;
    e->record[0x18f] = 0;
    unsigned two[] = {20, 15, 15, 2, 1, 1, 20, 15, 15, 2, 1, 1};
    game.vm.seed = seed_seq(4, two);
    CHECK(cok_combat_opportunity(&game, a, 6));
    CHECK(LOGGED("print: E;print: Attacks;attack: A (from behind) Hitting for 1 point of damage;"));
    CHECK(LOGGED("print: F;print: Attacks;attack: A (from behind)"));
    CHECK(e->combat->attack_slot == 1 && e->record[0x18f] == 0 && a->record[0x197] == 8);
    /* Their facings are kept (behind), and their aims put back. */
    CHECK(e->combat->target == NULL && f->combat->target == NULL);
    /* Stepping south keeps beside both: none. */
    s.log[0] = '\0';
    e->record[0x18f] = f->record[0x18f] = 1;
    CHECK(cok_combat_opportunity(&game, a, 4) && !LOGGED("Attacks"));
    /* One that has had its turn (initiative 0) and been hit attacks only
     * if the stepper lies in its arc from one of five directions from its
     * facing + 6; one not hit, at once. */
    reset("");
    a = record('A', 0, 5, 5);
    e = record('E', 1, 6, 5);
    e->combat->initiative = 0;
    e->combat->hits = 1;
    e->combat->facing = 2; /* east: the five from 0 to 4, A west of it in none */
    CHECK(cok_combat_opportunity(&game, a, 6) && !LOGGED("Attacks"));
    e->combat->facing = 6;
    game.vm.seed = swing(15, 2, 1);
    CHECK(cok_combat_opportunity(&game, a, 6) && LOGGED("print: E;print: Attacks;"));
    s.log[0] = '\0';
    e->combat->facing = 2;
    e->combat->hits = 0;
    e->record[0x18f] = 1;
    game.vm.seed = swing(15, 2, 1);
    CHECK(cok_combat_opportunity(&game, a, 6) && LOGGED("print: E;print: Attacks;"));
    /* Helpless, made to flee, or with a missile weapon not thrown: none. */
    s.log[0] = '\0';
    e->record[0x18f] = 1;
    e->combat->forced = 1;
    CHECK(cok_combat_opportunity(&game, a, 6) && !LOGGED("Attacks"));
    e->combat->forced = 0;
    give(e, 0x16, true, 0);
    CHECK(cok_combat_opportunity(&game, a, 6) && !LOGGED("Attacks"));
    cok_character_remove_item(e, 0);
    char why[300];
    CHECK(cok_character_stats(e, &game.item_types, why, sizeof why));
    CHECK(cok_character_add_effect(e, 0x35, 0, 0, false) != NULL);
    CHECK(cok_combat_opportunity(&game, a, 6) && !LOGGED("Attacks"));
}

static void test_flee(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    /* No enemy on the map: away, running, off the map, turn over. */
    a->combat->initiative = 4;
    CHECK(cok_combat_flee(&game, a) && a->record[0x188] == 3 && a->record[0x189] == 0);
    CHECK(LOGGED("print: A;print: Got Away;") && game.combat.combatant[1].size == 0);
    CHECK(a->combat->initiative == 0 && a->record[0x197] == 10);
    /* Faster than the fastest enemy that can act: away. Slower:
     * blocked. As fast: a d2 of 1. */
    for (int k = 0; k < 4; ++k) {
        reset("");
        a = record('A', 0, 5, 5);
        cok_character *m = record('M', 1, 9, 9), *n = record('N', 1, 19, 9);
        a->record[0x198] = 6;
        m->record[0x198] = k == 0 ? 5 : k == 1 ? 7 : 6;
        n->record[0x198] = 12;
        n->record[0x189] = 0;
        game.vm.seed = seed_for(2, k == 3 ? 2 : 1, k == 3 ? 2 : 1);
        uint32_t seed = game.vm.seed;
        CHECK(cok_combat_flee(&game, a));
        CHECK(a->record[0x188] == (k == 0 || k == 2 ? 3 : 0));
        CHECK(game.vm.seed != seed || k < 2);
        CHECK(k == 0 || k == 2 || LOGGED("print: Escape is blocked;"));
    }
}

static void test_kill(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x77, 0, 0, false) != NULL);
    m->combat->initiative = 3;
    CHECK(cok_combat_kill(&game, m, 6, "is killed"));
    CHECK(m->record[0x188] == 6 && m->record[0x189] == 0 && m->record[0x197] == 0);
    CHECK(cok_character_find_effect(m, 0x77) == NULL && c->combatant[2].size == 0);
    CHECK(LOGGED("print: M;print: is killed;") && LOGGED("sound: 5;"));
    /* Dead, stoned or gone already: only the text. */
    s.log[0] = '\0';
    m->record[0x188] = 7;
    CHECK(cok_combat_kill(&game, m, 4, "dies") && m->record[0x188] == 7 && LOGGED("print: dies;"));
    /* A party member leaves a body. */
    CHECK(cok_combat_kill(&game, a, 6, "dies from poison") && c->bodies == 1);
    CHECK(c->body[0].character == a && c->cells[5][5] == COK_COMBAT_BODY);
    /* Revived: back on its cell, its body taken back, okay, flashing. */
    bool placed;
    CHECK(cok_combat_revive(&game, a, 20, "goes mad!", &placed) && placed);
    CHECK(a->record[0x188] == 0 && a->record[0x189] == 1 && a->record[0x197] == 20);
    CHECK(c->combatant[1].size == 1 && c->cells[5][5] == COK_COMBAT_FLOOR);
    CHECK(LOGGED("print: A;print: goes mad!;") && c->sides[0] == 1);
    /* Not where something stands. */
    m->record[0x188] = 6;
    record('N', 1, 6, 5);
    CHECK(cok_combat_revive(&game, m, 20, "rises", &placed) && !placed);
    CHECK(m->record[0x188] == 6);
}

static void test_weapons(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5);
    bool thrown;
    CHECK(cok_combat_thrown(&game.item_types, a, &thrown) && !thrown && !cok_combat_hoopak(a));
    static const struct { uint8_t type; bool thrown; } kinds[] = {
        {0x02, true}, {0x03, true}, {0x05, false}, {0x16, false}, {0x43, true}, {0x12, false},
    };
    for (size_t k = 0; k < sizeof kinds / sizeof *kinds; ++k) {
        give(a, kinds[k].type, true, 0);
        CHECK(cok_combat_thrown(&game.item_types, a, &thrown) && thrown == kinds[k].thrown);
        CHECK(cok_combat_hoopak(a) == (kinds[k].type == 0x43));
        cok_character_remove_item(a, 0);
        char why[300];
        CHECK(cok_character_stats(a, &game.item_types, why, sizeof why));
    }
}

static void test_melee(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5), *n = record('N', 1, 4, 4);
    /* The nearest enemy it can attack, a square away: M (straight) before
     * N (diagonal); the view centred. */
    m->record[0x197] = 50;
    a->combat->initiative = 2;
    game.vm.seed = swing(15, 2, 2);
    CHECK(cok_combat_melee(&game, a) && m->record[0x197] == 48 && n->record[0x197] == 10);
    CHECK(a->combat->initiative == 0 && m->combat->hits == 1);
    /* One that drops leaves the turn going: the next is attacked. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    n = record('N', 1, 4, 4);
    m->record[0x197] = 1;
    a->record[0x18f] = 2;
    unsigned two[] = {20, 15, 15, 2, 2, 2, 20, 15, 15, 2, 2, 2};
    game.vm.seed = seed_seq(4, two);
    CHECK(cok_combat_melee(&game, a));
    CHECK(m->record[0x189] == 0 && n->record[0x197] == 8 && a->record[0x18f] == 0);
    /* None a square away: the turn ends, no roll. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 5);
    a->combat->initiative = 2;
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_melee(&game, a) && a->combat->initiative == 0 && game.vm.seed == seed);
    CHECK(m->combat->hits == 0);
    /* With a bow and arrows and none beside it, it shoots within range;
     * an enemy beside it is attacked with the bow in melee, with arrows. */
    size_t arrows;
    give(a, 0x16, true, 0);
    arrows = give(a, 0x1e, true, 5);
    game.vm.seed = swing(15, 6, 1);
    CHECK(cok_combat_melee(&game, a) && m->combat->hits == 1);
    CHECK(a->items[arrows - 1][0x39] == 4);
    /* One that cannot be attacked is passed over. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    n = record('N', 1, 4, 4);
    game.vm.character = a->record;
    CHECK(cok_character_add_effect(m, 0x19, 0, 0, false) != NULL);
    game.vm.seed = swing(15, 2, 2);
    CHECK(cok_combat_melee(&game, a) && n->combat->hits == 1 && m->combat->hits == 0);
    /* A thrown weapon a square away uses no ammunition. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    size_t axe = give(a, 0x02, true, 1);
    game.vm.seed = swing(15, 6, 1);
    CHECK(cok_combat_melee(&game, a) && a->item_count == 1 && a->items[axe - 1][0x39] == 1);
    /* Through a battle: eclplay's --combat melee. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->initiative = 2;
    game.combat_stub = COK_COMBAT_MELEE;
    game.vm.seed = swing(15, 2, 2);
    CHECK(cok_combat_turn(&game, a) && m->combat->hits == 1);
    CHECK(LOGGED("turn: A (initiative 2);"));
    game.combat_stub = COK_COMBAT_UNPORTED;
}

static void test_explode(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 0, 6, 6), *x = record('X', 1, 6, 5);
    cok_character *far = record('F', 0, 9, 9);
    /* A bozak's 0x44 at its death (event 0x0d): exploding, listed, its
     * 0x44 gone. */
    CHECK(cok_character_add_effect(x, 0x44, 0, 0xff, false) != NULL);
    x->record[0x197] = 2;
    game.vm.seed = swing(15, 2, 2);
    bool over;
    CHECK(cok_combat_strike(&game, a, x, false, &over));
    /* The strike ran the explosions after the blow: "explodes.", 1d6 to
     * each other listed within a cell (but those exploding), then it is
     * dead and leaves the map. */
    CHECK(x->record[0x188] == 6 && x->record[0x189] == 0 && c->combatant[3].size == 0);
    CHECK(cok_character_find_effect(x, 0x44) == NULL && c->exploding_count == 0);
    CHECK(LOGGED("print: X;print: explodes.;"));
    CHECK(a->record[0x197] < 10 && b->record[0x197] < 10 && far->record[0x197] == 10);
    CHECK(LOGGED("print: A;print: takes ") && LOGGED("sound: 3;"));
    CHECK(!c->exploding_now && c->exploding[1] == NULL);
    /* The others explode in turn; those exploding take none. */
    reset("");
    a = record('A', 0, 5, 5);
    x = record('X', 1, 6, 5);
    cok_character *y = record('Y', 1, 7, 5);
    CHECK(cok_character_add_effect(y, 0x44, 0, 0xff, false) != NULL);
    y->record[0x197] = 1;
    x->record[0x188] = 10;
    x->record[0x189] = 0;
    c->exploding[1] = x;
    c->exploding_count = 1;
    unsigned dice[] = {6, 1, 1, 6, 1, 1};
    game.vm.seed = seed_seq(2, dice);
    CHECK(cok_combat_explode(&game));
    CHECK(x->record[0x188] == 6 && y->record[0x188] == 6 && a->record[0x197] == 9);
    CHECK(strstr(strstr(s.log, "explodes.") + 1, "print: Y;print: explodes.;") != NULL);
    /* Nine around one overwrite its pointer: refused. */
    reset("");
    x = record('X', 1, 10, 10);
    record('A', 0, 9, 9);
    record('B', 0, 10, 9);
    record('C', 0, 11, 9);
    record('D', 0, 9, 10);
    record('E', 0, 11, 10);
    record('F', 0, 9, 11);
    record('G', 0, 10, 11);
    record('H', 0, 11, 11);
    c->exploding[1] = x;
    c->exploding_count = 1;
    x->record[0x188] = 10;
    x->record[0x189] = 0;
    CHECK(cok_combat_explode(&game));
    reset("");
    x = record('X', 1, 10, 10);
    x->record[0xcf] = 3; /* two across: twelve around */
    game.combat.combatant[1].size = 3;
    CHECK(cok_combat_occupy(&game.combat));
    for (int k = 0; k < 9; ++k) record((char)('A' + k), 0, (int8_t)(9 + k % 4), k < 4 ? 9 : 11);
    c->exploding[1] = x;
    c->exploding_count = 1;
    CHECK(!cok_combat_explode(&game) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_damage(void)
{
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    (void)a;
    /* In combat: a burst (kind 0) with the text, damage through 6346:24d7
     * in combat, may not cast; "lost a spell" for one casting. */
    m->combat->may_cast = 1;
    m->combat->spell = 4;
    m->combat->initiative = 3;
    m->record[0x1e] = 4;
    game.effects.rolls.damage_type = 1;
    CHECK(cok_cast_damage(&game, m, 3, 0, false) && m->record[0x197] == 7);
    CHECK(LOGGED("sound: 3;print: M;print: takes 3 points of damage from Fire;"));
    CHECK(LOGGED("print: M;print: lost a spell;") && m->record[0x1e] == 0);
    CHECK(m->combat->may_cast == 0 && m->combat->spell == 0 && m->combat->initiative == 0);
    /* Down: "Goes Down, and is Dying" from the row after the text, the
     * battle's effects gone, event 0x0d, off the map; counted out. */
    CHECK(cok_character_add_effect(m, 0x77, 0, 0, false) != NULL);
    game.effects.rolls.damage_type = 0;
    CHECK(cok_cast_damage(&game, m, 9, 0, false) && m->record[0x188] == 5);
    CHECK(LOGGED("print: M;print: Goes Down, and is Dying;"));
    CHECK(cok_character_find_effect(m, 0x77) == NULL && game.combat.combatant[2].size == 0);
    CHECK(game.combat.sides[1] == 0 && m->combat->dying == 2);
    /* Exploding: no death on the screen. */
    reset("");
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x44, 0, 0xff, false) != NULL);
    CHECK(cok_cast_damage(&game, m, 30, 0, false) && m->record[0x188] == 10);
    CHECK(game.combat.combatant[1].size == 1 && game.combat.exploding[1] == m);
}

static bool run(cok_character *c, uint8_t ev)
{
    return cok_effects_dispatch(&game.effects, c, ev);
}

static void test_handlers(void)
{
    reset("");
    cok_combat *c = &game.combat;
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5), *n = record('N', 1, 9, 5);
    cok_rolls *r = &game.effects.rolls;
    /* Charm (0x0b) taking hold: its value gains bit 5 and the side in bit
     * 6; the side becomes bit 7; the computer controls it, turned; no
     * target; the sides counted; morale 100. */
    cok_effect *charm = cok_character_add_effect(m, 0x0b, 0, 0x0c, false);
    m->combat->target = a->record;
    c->sides[0] = 9;
    CHECK(run(m, 0x13) && charm->value == 0x6c && m->record[0x18a] == 0);
    CHECK(m->record[0x18b] == 1 && m->record[0xe7] == 0xb3 && m->combat->target == NULL);
    CHECK(c->sides[0] == 2 && c->sides[1] == 1 && r->morale == 100);
    /* Taken hold, only morale; removed, back on its side. */
    r->morale = 0;
    m->record[0x18b] = 0;
    CHECK(run(m, 0x13) && charm->value == 0x6c && m->record[0x18b] == 0 && r->morale == 100);
    charm->on_remove = true;
    CHECK(cok_effects_remove(&game.effects, m, charm, 0x0b) && m->record[0x18a] == 1);
    CHECK(m->record[0xe7] == 0);
    /* A value of 0x80 or more keeps the party's side (bit 7). */
    charm = cok_character_add_effect(a, 0x0b, 0, 0x90, false);
    CHECK(run(a, 0x13) && a->record[0x18a] == 1 && charm->value == 0xb0);
    a->record[0x18a] = 0;
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x0b));
    /* Berserk (0x4d) in combat: the second listed around it (the nearest
     * other), the side against its, "goes berserk". */
    s.log[0] = '\0';
    m->combat->may_cast = 1;
    CHECK(cok_character_add_effect(m, 0x4d, 0, 0, false) != NULL);
    CHECK(run(m, 0x0f) && m->combat->target == a->record && m->record[0x18a] == 1);
    CHECK(m->combat->may_cast == 0 && LOGGED("print: M;print: goes berserk;"));
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x4d));
    m->record[0x18a] = 1;
    /* Alone, the second listed is the one an earlier list left. */
    c->combatant[1].size = c->combatant[3].size = 0;
    CHECK(cok_combat_occupy(c));
    c->listed[2].index = 3;
    CHECK(cok_character_add_effect(m, 0x4d, 0, 0, false) != NULL);
    CHECK(run(m, 0x0f) && m->combat->target == n->record && m->record[0x18a] == 0);
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x4d));
    c->listed[2].index = 0;
    CHECK(cok_character_add_effect(m, 0x4d, 0, 0, false) != NULL);
    CHECK(!run(m, 0x0f) && strstr(game.effects.error, "0000:018a") != NULL);
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x4d));

    /* Poisons on a hit (events 2, 3): the attacker's target saves, with
     * 0x74's value added to the bonus, or 0 for a negative bonus. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->target = m->record;
    static const struct { uint8_t id; int8_t bonus; } poisons[] = {
        {0x40, 0}, {0x41, 4}, {0x56, -2}, {0x57, 1},
    };
    for (size_t k = 0; k < sizeof poisons / sizeof *poisons; ++k) {
        cok_effect *p = cok_character_add_effect(a, poisons[k].id, 0, 0xff, false);
        m->record[0x17c] = 0;
        m->record[0xd0] = 12;
        /* A d20 + bonus against 12: 12 - bonus just makes it, one less
         * fails. */
        for (unsigned miss = 0; miss < 2; ++miss) {
            unsigned roll = (unsigned)(12 - poisons[k].bonus) - miss;
            game.vm.seed = seed_for(20, roll, roll);
            CHECK(run(a, 2) && r->target == m->record && r->save_type == 0);
            CHECK(m->record[0x188] == (miss ? 6 : 0));
            m->record[0x188] = 0;
            m->record[0x189] = 1;
            cok_effects_remove(&game.effects, m, NULL, 0x37);
        }
        CHECK(cok_effects_remove(&game.effects, a, p, poisons[k].id));
    }
    cok_effect *resist = cok_character_add_effect(m, 0x74, 0, 3, false);
    CHECK(cok_character_add_effect(a, 0x56, 0, 0xff, false) != NULL);
    game.vm.seed = seed_for(20, 12, 12);
    CHECK(run(a, 3) && m->record[0x188] == 0); /* -2 made 0, the 3 not added */
    cok_effects_remove(&game.effects, a, NULL, 0x56);
    CHECK(cok_character_add_effect(a, 0x40, 0, 0xff, false) != NULL);
    game.vm.seed = seed_for(20, 9, 9);
    CHECK(run(a, 2) && m->record[0x188] == 0); /* 0 + 3 */
    resist->value = 2;
    game.vm.seed = seed_for(20, 9, 9);
    s.log[0] = '\0';
    CHECK(run(a, 2) && m->record[0x188] == 6 && cok_character_find_effect(m, 0x37) != NULL);
    CHECK(LOGGED("print: M;print: is Poisoned;") && LOGGED("print: M;print: is killed;"));
    CHECK(game.combat.combatant[2].size == 0);
    /* With no target: refused. */
    a->combat->target = NULL;
    CHECK(!run(a, 2) && strstr(game.effects.error, "target with none") != NULL);
    /* The negative bonus made 0 has nothing added: an 11 fails. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->target = m->record;
    m->record[0xd0] = 12;
    CHECK(cok_character_add_effect(m, 0x74, 0, 3, false) != NULL);
    CHECK(cok_character_add_effect(a, 0x56, 0, 0xff, false) != NULL);
    game.vm.seed = seed_for(20, 11, 11);
    CHECK(run(a, 3) && m->record[0x188] == 6);

    /* Paralysis (0x45, 0x51 with no damage, 0x58 for races above 1): a
     * saving throw of type 0, or "is Paralyzed" and 0x34 for 100 minutes,
     * value 0x0c. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->target = m->record;
    m->record[0xd0] = 0xff;
    CHECK(cok_character_add_effect(a, 0x45, 0, 0xff, false) != NULL);
    game.vm.seed = seed_for(20, 2, 19);
    CHECK(run(a, 2) && LOGGED("print: M;print: is Paralyzed;") && LOGGED("sound: 4;"));
    cok_effect *held = cok_character_find_effect(m, 0x34);
    CHECK(held != NULL && held->duration == 100 && held->value == 0x0c && !held->on_remove);
    cok_effects_remove(&game.effects, m, held, 0x34);
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(run(a, 2) && cok_character_find_effect(m, 0x34) == NULL);
    cok_effects_remove(&game.effects, a, NULL, 0x45);
    CHECK(cok_character_add_effect(a, 0x51, 0, 0xff, false) != NULL);
    r->amount = 9;
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(run(a, 3) && r->amount == 0);
    cok_effects_remove(&game.effects, a, NULL, 0x51);
    CHECK(cok_character_add_effect(a, 0x58, 0, 0xff, false) != NULL);
    m->record[0x5a] = 1;
    uint32_t seed = game.vm.seed;
    CHECK(run(a, 2) && game.vm.seed == seed);
    m->record[0x5a] = 2;
    game.vm.seed = seed_for(20, 2, 19);
    CHECK(run(a, 2) && cok_character_find_effect(m, 0x34) != NULL);

    /* Molly's weapon (0x07): the THAC0 from its base; a square away,
     * strength's damage + 1d6 + 2 + the weapon's bonus, to hit with
     * strength's; farther, 1d4 + 1 + the bonus, to hit with dexterity's
     * missile bonus; at the attack roll too. No weapon: refused. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->target = m->record;
    CHECK(cok_character_add_effect(a, 0x07, 0, 0xff, false) != NULL);
    CHECK(!run(a, 4) && strstr(game.effects.error, "never set") != NULL);
    size_t hoopak = give(a, 0x43, true, 0);
    a->items[hoopak - 1][0x32] = 2;
    a->record[0x59] = 41;
    a->record[0x17] = 17; /* dexterity's missile bonus +2 */
    game.vm.seed = seed_for(6, 4, 4);
    CHECK(run(a, 4) && r->amount == 8 && a->record[0x18c] == 41 && r->dice == 1);
    game.combat.combatant[2].x = 8;
    CHECK(cok_combat_occupy(&game.combat));
    game.vm.seed = seed_for(4, 3, 3);
    CHECK(run(a, 0x0a) && r->amount == 6 && a->record[0x18c] == 43);
    /* With no target, the distance is the last listed's: M's, 3. */
    a->combat->target = NULL;
    game.vm.seed = seed_for(4, 3, 3);
    CHECK(run(a, 4) && r->amount == 6);

    /* Against the target of the attack: 0x69 (+0x13f bit 3) the holder's
     * +0xfd more; 0x72 (+0x140 bit 0) the holder's hit points; 0x73
     * (+0x13f bit 5) 3 more. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->target = m->record;
    a->record[0xfd] = 4;
    a->record[0x197] = 7;
    static const struct { uint8_t id, field, bit, want; } slayers[] = {
        {0x69, 0x3f, 8, 14}, {0x72, 0x40, 1, 7}, {0x73, 0x3f, 0x20, 13},
    };
    for (size_t k = 0; k < 3; ++k) {
        cok_effect *e = cok_character_add_effect(a, slayers[k].id, 0, 0xff, false);
        r->amount = 10;
        m->record[0x100 + slayers[k].field] = 0;
        CHECK(run(a, 4) && r->amount == 10);
        m->record[0x100 + slayers[k].field] = slayers[k].bit;
        CHECK(run(a, 4) && r->amount == slayers[k].want);
        a->combat->target = NULL;
        CHECK(!run(a, 4));
        a->combat->target = m->record;
        cok_effects_remove(&game.effects, a, e, slayers[k].id);
    }
    /* Disruption (0x75), in combat: an undead (+0x13f bit 1) of a kind
     * (+0xda) "is disrupted", gone; else twice the damage. */
    CHECK(cok_character_add_effect(a, 0x75, 0, 0xff, false) != NULL);
    m->record[0x13f] = 2;
    r->amount = 7;
    CHECK(run(a, 4) && r->amount == 14 && m->record[0x188] == 0);
    m->record[0xda] = 3;
    s.log[0] = '\0';
    CHECK(run(a, 4) && m->record[0x188] == 8 && LOGGED("print: M;print: is disrupted;"));
    game.vm.mode = 4;
    a->combat->target = NULL;
    CHECK(run(a, 4));
    game.vm.mode = 5;

    /* Fire shield (0x70): the attacker (selected) within a square "gets
     * zapped" for twice the damage as magic; damage and type put back. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x70, 0, 0xff, false) != NULL);
    game.vm.character = a->record;
    r->amount = 3;
    r->damage_type = 1;
    CHECK(run(m, 5) && a->record[0x197] == 4 && r->amount == 3 && r->damage_type == 1);
    CHECK(LOGGED("print: A;print: gets zapped;") && LOGGED("print: takes 6 points of damage from Magic;"));
    game.combat.combatant[1].x = 3;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(run(m, 5) && a->record[0x197] == 4);

    /* Losing a weapon (0x43) at the death: the killer (selected), a d20
     * of at least its dexterity - 3, not a spell, a square away. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    size_t sword = give(a, 0x12, true, 0);
    CHECK(a->slots[0] == sword);
    CHECK(cok_character_add_effect(m, 0x43, 0, 0xff, false) != NULL);
    game.vm.character = a->record;
    a->record[0x17] = 12;
    game.vm.seed = seed_for(20, 8, 8);
    CHECK(run(m, 0x0d) && a->item_count == 1);
    game.vm.seed = seed_for(20, 9, 9);
    CHECK(run(m, 0x0d) && LOGGED("print: A;print: loses his weapon;"));
    CHECK(a->item_count == 0 && a->slots[0] == 0 && game.lost_weapon_count == 1);
    CHECK(game.lost_weapons[0].owner == a->record && game.lost_weapons[0].item[0x2e] == 0x12);
    CHECK(game.lost_weapons[0].item[0x34] == 0);
    /* Not cursed, not by a spell, not while the dead explode, not two
     * squares away; a spiritual hammer is said lost and kept. */
    sword = give(a, 0x12, true, 0);
    a->items[sword - 1][0x36] = 1;
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(run(m, 0x0d) && a->item_count == 1);
    a->items[sword - 1][0x36] = 0;
    r->spell = 3;
    CHECK(run(m, 0x0d) && a->item_count == 1);
    r->spell = 0;
    game.combat.exploding_now = true;
    CHECK(run(m, 0x0d) && a->item_count == 1);
    game.combat.exploding_now = false;
    a->items[sword - 1][0x2e] = 6;
    a->items[sword - 1][0x31] = 0x79;
    s.log[0] = '\0';
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(run(m, 0x0d) && a->item_count == 1 && LOGGED("loses his weapon"));
    CHECK(game.lost_weapon_count == 1);
    /* Given back at the end of combat (351b:185f). */
    a->items[sword - 1][0x2e] = 0x12;
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(run(m, 0x0d) && game.lost_weapon_count == 2 && a->item_count == 0);
    /* Arrows struck with go, and the readied bow's slot is cleared all the
     * same. */
    give(a, 0x16, true, 0);
    size_t arrows = give(a, 0x1e, true, 9);
    CHECK(a->slots[0] == 1 && a->slots[11] == arrows);
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(run(m, 0x0d) && a->item_count == 1 && a->items[0][0x2e] == 0x16);
    CHECK(a->slots[0] == 0 && game.lost_weapons[2].item[0x2e] == 0x1e);
    /* Selected itself, it is no square away from itself: nothing lost. */
    sword = give(m, 0x12, true, 0);
    game.vm.character = m->record;
    m->record[0x17] = 12;
    game.vm.seed = seed_for(20, 20, 20);
    CHECK(run(m, 0x0d) && m->item_count == 1 && game.lost_weapon_count == 3);

    /* Exploding (0x44): only one that cannot act. */
    reset("");
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x44, 0, 0xff, false) != NULL);
    CHECK(run(m, 0x0d) && m->record[0x188] == 0 && game.combat.exploding_count == 0);
    m->record[0x189] = 0;
    m->record[0x188] = 6;
    CHECK(run(m, 0x0d) && m->record[0x188] == 10 && game.combat.exploding[1] == m);
    CHECK(cok_character_find_effect(m, 0x44) == NULL);
    /* The count is a byte, at least 1; past 20 none is listed. */
    game.combat.exploding_count = 20;
    CHECK(cok_character_add_effect(m, 0x44, 0, 0xff, false) != NULL);
    CHECK(run(m, 0x0d) && game.combat.exploding_count == 20 && game.combat.exploding[20] == NULL);
    game.combat.exploding_count = 0xff;
    CHECK(cok_character_add_effect(m, 0x44, 0, 0xff, false) != NULL);
    CHECK(run(m, 0x0d) && game.combat.exploding_count == 1);

    /* Goes mad (0x0d), then arises in a new form (0x20). */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x0d, 0, 0xff, false) != NULL);
    m->record[0x1e] = 3;
    CHECK(run(m, 0x0d) && cok_character_find_effect(m, 0x20) == NULL); /* status 0 */
    CHECK(cok_combat_kill(&game, m, 6, "is killed") && m->record[0x188] == 0);
    CHECK(m->record[0x197] == 20 && game.combat.combatant[2].size == 1);
    CHECK(LOGGED("print: M;print: goes mad!;") && cok_character_find_effect(m, 0x0d) == NULL);
    CHECK(m->record[0x1e] == 0 && cok_character_find_effect(m, 0x30) != NULL);
    cok_effect *throes = cok_character_find_effect(m, 0x3a);
    CHECK(throes != NULL && throes->duration == 6 && throes->on_remove);
    CHECK(cok_combat_kill(&game, m, 6, "is killed") && m->record[0x188] == 0);
    CHECK(LOGGED("print: M;print: Arises in a new form;"));
    CHECK(cok_character_find_effect(m, 0x20) == NULL && cok_character_find_effect(m, 0x3a) == NULL);
    CHECK(cok_character_find_effect(m, 0x42) != NULL && cok_character_find_effect(m, 0x3b) != NULL);
    CHECK(cok_character_find_effect(m, 0x30) == NULL && cok_character_find_effect(m, 0x39) == NULL);
    /* Where it cannot be put back, okay but off the map. */
    CHECK(cok_character_add_effect(m, 0x0d, 0, 0xff, false) != NULL);
    m->record[0x188] = 6;
    record('B', 0, 6, 5);
    game.combat.combatant[2].size = 0;
    CHECK(cok_combat_occupy(&game.combat));
    CHECK(run(m, 0x0d) && m->record[0x188] == 0 && m->record[0x189] == 1);
    CHECK(cok_character_find_effect(m, 0x0d) != NULL);
}

/* The quirks the tests above pass by, each pinned here by the case that
 * tells it apart. */
static void test_quirks(void)
{
    cok_combat *c = &game.combat;
    cok_rolls *r = &game.effects.rolls;
    bool over, b;

    /* 432f:19b6: a remainder of -5 (facing 19 against direction 6) is
     * 0xfb, folded to 13 as a byte, 5 after the modulo; taken unsigned it
     * would be 3. */
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    m->combat->facing = 19;
    CHECK(cok_combat_book(&game, a, m) && m->combat->turning == 5);

    /* 432f:2af0: one cell by +0xcf & 0x7f: 0x88 is eight. */
    a->record[0xff] = 1;
    m->combat->hits = 2;
    m->combat->facing = 2;
    m->record[0xcf] = 0x88;
    CHECK(cok_combat_backstab(&game, a, m, &b) && !b);
    m->record[0xcf] = 1;
    a->record[0xff] = 0;

    /* 432f:1ddc: a third of the range less 1: a range of 9 steps by 2, so
     * at 5 squares both. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 10, 5);
    give(a, 0x16, true, 0);
    game.item_types.type[0x16][12] = 9;
    uint8_t ac = 50;
    CHECK(cok_combat_range(&game, a, m, &ac) && ac == 55);
    game.item_types.type[0x16][12] = 22;

    /* 432f:01ba: events 4 then 5: 0x72 makes the damage the holder's hit
     * points, then the target's 0x3b none. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->combat->target = m->record;
    m->record[0x140] = 1;
    CHECK(cok_character_add_effect(a, 0x72, 0, 0xff, false) != NULL);
    CHECK(cok_combat_damage_roll(&game, a, m, 1) && r->amount == 10);
    CHECK(cok_character_add_effect(m, 0x3b, 0, 0, false) != NULL);
    CHECK(cok_combat_damage_roll(&game, a, m, 1) && r->amount == 0);

    /* 432f:033e: a target gone (8) is not told; "goes down" on the row
     * after the attack's text, "is killed" two below. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->record[0x188] = 8;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && !LOGGED("and Misses"));
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->record[0x195] = 30;
    track_rows = true;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && m->record[0x188] == 6);
    /* The text "Hitting for 31 / points of / damage" on rows 13-15. */
    CHECK(strstr(rows, "M Hitting for 31 points of damage@12;M@16;goes down@16;M@18;is killed@18;") !=
          NULL);
    /* "and is Dying" two below too, drawn after it is logged. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->record[0x197] = 1;
    a->record[0x195] = 1;
    track_rows = true;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && m->record[0x188] == 5);
    CHECK(!LOGGED("is killed"));
    CHECK(strstr(rows, "M@16;goes down@16;and is Dying@17;5@18;") != NULL);

    /* 60f4:1440's stale byte with 0x4d: unknown, which matters when the
     * dead one is among those 0e08 could find by it. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x4d, 0, 0, false) != NULL);
    c->exploding[1] = m;
    CHECK(!cok_combat_kill(&game, m, 6, "dies") && strstr(game.error, "stack holds") != NULL);

    /* 432f:1579: the cruel blow leaves no attacks in either slot. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(m, 0x33, 0, 0, false) != NULL);
    a->record[0x18f] = 1;
    a->record[0x190] = 1;
    a->combat->attack_slot = 1;
    CHECK(cok_combat_strike(&game, a, m, false, &over) && over);
    CHECK(a->record[0x18f] == 0 && a->record[0x190] == 0);
    /* Large above 0x80 or by & 7: 0x80 is not. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    give(a, 0x12, true, 0);
    m->record[0xcf] = 0x80;
    uint8_t dice = a->record[0x191], sides = a->record[0x193];
    game.vm.seed = swing(15, sides, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over));
    CHECK(a->record[0x191] == dice && a->record[0x193] == sides);
    /* The target's stats recomputed first; the sweeps used up. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->record[0x18d] = 0;
    a->combat->sweeps = 2;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over));
    CHECK(m->record[0x18d] == 50);
    CHECK(a->combat->sweeps == 0);
    /* A backstab: from behind less 4, so a 7 hits. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    a->record[0xff] = 1;
    m->combat->hits = 2;
    m->combat->facing = 2;
    game.vm.seed = swing(7, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && LOGGED("print: -Backstabs-;"));
    CHECK(r->hits[0] == 1);
    /* From behind only for one hit more than once. */
    a->record[0xff] = 0;
    m->combat->hits = 1;
    m->combat->turning = 5;
    a->record[0x18f] = 1;
    a->combat->attack_slot = 2;
    s.log[0] = '\0';
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && LOGGED("attack: M Hitting"));
    /* A target made helpless by the first blow (0x45) is hit by the next
     * whatever the roll. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(a, 0x45, 0, 0xff, false) != NULL);
    a->combat->target = m->record;
    m->record[0xd0] = 0xff;
    m->record[0x197] = 100;
    a->record[0x18f] = 2;
    unsigned held[] = {20, 10, 10, 2, 1, 1, 20, 2, 19, 20, 1, 1};
    game.vm.seed = seed_seq(4, held);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && r->hits[0] == 2);
    CHECK(cok_combat_helpless(m));
    /* Events 2 and 3 only with damage: none, no poison. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    CHECK(cok_character_add_effect(a, 0x40, 0, 0xff, false) != NULL);
    CHECK(cok_character_add_effect(m, 0x3b, 0, 0, false) != NULL);
    a->combat->target = m->record;
    m->record[0xd0] = 0xff;
    unsigned none[] = {20, 10, 10, 2, 1, 1, 20, 2, 19};
    game.vm.seed = seed_seq(3, none);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && r->hits[0] == 1);
    CHECK(!LOGGED("is Poisoned") && m->record[0x188] == 0);
    /* Slot 2's attacks left: not over. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    m->record[0x197] = 100;
    a->record[0x18f] = 1;
    a->record[0x190] = 1;
    a->combat->attack_slot = 1;
    game.vm.seed = swing(10, 2, 1);
    CHECK(cok_combat_strike(&game, a, m, false, &over) && !over && a->record[0x190] == 1);
    /* An attacker that drops (the target exploding) is over. */
    reset("");
    a = record('A', 0, 5, 5);
    cok_character *x = record('X', 1, 6, 5);
    CHECK(cok_character_add_effect(x, 0x44, 0, 0xff, false) != NULL);
    x->record[0x197] = 1;
    a->record[0x197] = 1;
    a->record[0x18f] = 2;
    a->record[0x190] = 1;
    a->combat->attack_slot = 1;
    game.vm.seed = swing(15, 2, 1);
    CHECK(cok_combat_strike(&game, a, x, false, &over) && over);
    CHECK(a->record[0x189] == 0 && a->record[0x18f] == 1);

    /* 432f:2beb: an arrow south-east, its attacking picture (slot 14's
     * second); south, slot 13's second. */
    for (int k = 0; k < 2; ++k) {
        reset("");
        a = record('A', 0, 5, 5);
        m = record('M', 1, k == 0 ? 8 : 5, k == 0 ? 8 : 9);
        give(a, 0x16, true, 0);
        size_t arrows = give(a, 0x1e, true, 3);
        game.vm.seed = swing(2, 0, 0);
        CHECK(cok_combat_attack(&game, a, m, false, arrows, &over));
        const cok_picture *icon = &game.icons[k == 0 ? 0x0e : 0x0d][1];
        CHECK(memcmp(c->flash.pixels, icon->pixels, c->flash.frame_size) == 0);
    }
    /* A sling's stone 10 ms a step, a thrown hammer 50. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 5);
    give(a, 0x1c, true, 0);
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over));
    CHECK(strstr(delays, ";10;") != NULL && strstr(delays, ";20;") == NULL);
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 5);
    size_t hammer = give(a, 0x06, true, 0);
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, hammer, &over) && strstr(delays, ";50;") != NULL);

    /* 432f:1a45: from behind, a target shown keeps its facing. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    c->view_x = 2;
    c->view_y = 2;
    CHECK(cok_combat_occupy(c));
    m->combat->facing = 1;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, true, 0, &over) && m->combat->facing == 1);
    /* The attacker is selected for the strike: the killer that 0x43 takes
     * the weapon from; the selection put back. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    give(a, 0x12, true, 0);
    CHECK(cok_character_add_effect(m, 0x43, 0, 0xff, false) != NULL);
    m->record[0x197] = 1;
    a->record[0x17] = 12;
    game.vm.character = m->record;
    unsigned lose[] = {20, 15, 15, 8, 1, 8, 20, 20, 20};
    game.vm.seed = seed_seq(3, lose);
    CHECK(cok_combat_attack(&game, a, m, false, 0, &over));
    CHECK(a->item_count == 0 && game.lost_weapon_count == 1 && game.vm.character == m->record);
    /* An archer kills a target with 0x43 a square away: the arrows it
     * shot are lost (3f44:13a9 takes them), and the original goes on with
     * the freed node: its count less the shots, then at 0 6346:1697 does
     * not find it, says so and waits for a key; the bow stays. */
    for (int k = 0; k < 2; ++k) {
        reset("\r");
        a = record('A', 0, 5, 5);
        m = record('M', 1, 6, 5);
        give(a, 0x16, true, 0);
        size_t arrows = give(a, 0x1e, true, k == 0 ? 1 : 5);
        CHECK(cok_character_add_effect(m, 0x43, 0, 0xff, false) != NULL);
        m->record[0x197] = 1;
        a->record[0x17] = 12;
        unsigned shot[] = {20, 15, 15, 6, 1, 6, 20, 20, 20};
        game.vm.seed = seed_seq(3, shot);
        CHECK(cok_combat_attack(&game, a, m, false, arrows, &over));
        CHECK(a->item_count == 1 && a->items[0][0x2e] == 0x16 && game.lost_weapon_count == 1);
        CHECK(game.lost_weapons[0].item[0x2e] == 0x1e);
        CHECK(game.lost_weapons[0].item[0x39] == (k == 0 ? 1 : 5));
        CHECK(LOGGED("print: Tried to Lose item & couldn't find it!;") == (k == 0));
        CHECK(s.at == (k == 0 ? 1u : 0u) && game.pool.item_count == 0);
    }
    /* Two shots, the first a miss: two from the freed count of 2, 0. */
    reset("\r");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    give(a, 0x16, true, 0);
    size_t two_arrows = give(a, 0x1e, true, 2);
    CHECK(cok_character_add_effect(m, 0x43, 0, 0xff, false) != NULL);
    m->record[0x197] = 1;
    a->record[0x17] = 12;
    a->record[0x18f] = 2;
    unsigned shots2[] = {20, 2, 2, 20, 15, 15, 6, 1, 6, 20, 20, 20};
    game.vm.seed = seed_seq(4, shots2);
    CHECK(cok_combat_attack(&game, a, m, false, two_arrows, &over) && c->swings[1] == 2);
    CHECK(game.lost_weapon_count == 1 && LOGGED("print: Tried to Lose item & couldn't find it!;"));
    /* Its stats recomputed after: a thrown axe gone. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 8, 5);
    m->record[0x197] = 100;
    size_t axe = give(a, 0x02, true, 1);
    uint8_t thac0 = a->record[0x18c];
    a->record[0x18c] = 0;
    game.vm.seed = swing(2, 0, 0);
    CHECK(cok_combat_attack(&game, a, m, false, axe, &over) && a->item_count == 0);
    CHECK(a->record[0x18c] == thac0);

    /* 432f:0fce: as many attacks as sweeps, none; each swept one attacked
     * with one attack, though none were left. */
    reset("");
    a = record('A', 0, 5, 5);
    cok_character *m1 = record('M', 1, 6, 5), *m2 = record('N', 1, 6, 6), *m3 = record('O', 1, 5, 6);
    m1->record[0x197] = m2->record[0x197] = m3->record[0x197] = 50;
    a->combat->sweeps = 2;
    a->record[0x18f] = 2;
    CHECK(cok_combat_sweep(&game, a, m1, &b) && !b);
    a->record[0x18f] = 0;
    unsigned two[] = {20, 15, 15, 2, 1, 1, 20, 15, 15, 2, 1, 1};
    game.vm.seed = seed_seq(4, two);
    CHECK(cok_combat_sweep(&game, a, m1, &b) && b);
    CHECK(m1->record[0x197] == 49);

    /* 432f:077a: the cost a byte: a wall's 0xff three times is 0xfd. */
    reset("");
    a = record('A', 0, 5, 5);
    record('M', 1, 20, 20);
    c->cells[6][6] = 0x01;
    a->combat->movement = 0xfe;
    CHECK(cok_combat_step(&game, a, 3) && a->combat->movement == 1);
    /* A guard stops guarding even with attacks left (slot 2's). */
    reset("");
    a = record('A', 0, 5, 5);
    cok_character *g = record('G', 1, 7, 5);
    g->combat->guarding = 1;
    g->record[0x190] = 1;
    g->combat->attack_slot = 1;
    a->combat->movement = 8;
    a->record[0x197] = 100;
    unsigned guard[] = {20, 15, 15, 2, 1, 1, 20, 15, 15, 2, 1, 1};
    game.vm.seed = seed_seq(4, guard);
    CHECK(cok_combat_step(&game, a, 2) && g->combat->guarding == 0);

    /* 432f:0986: the five directions from the facing + 6: the stepper
     * west (6) of it lies in the first for facing 0, the last for 4. */
    for (uint8_t facing = 0; facing < 8; facing = (uint8_t)(facing + 4)) {
        reset("");
        a = record('A', 0, 5, 5);
        cok_character *e = record('E', 1, 6, 5);
        e->combat->initiative = 0;
        e->combat->hits = 1;
        e->combat->facing = facing;
        game.vm.seed = swing(15, 2, 1);
        CHECK(cok_combat_opportunity(&game, a, 6));
        CHECK(LOGGED("print: E;print: Attacks;"));
    }

    /* 432f:0dc7: blocked, the turn ends all the same. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 9);
    a->record[0x198] = 6;
    m->record[0x198] = 7;
    a->combat->initiative = 4;
    CHECK(cok_combat_flee(&game, a) && a->combat->initiative == 0 && a->record[0x188] == 0);

    /* 60f4:00eb: dead already (6), only the text. */
    reset("");
    m = record('M', 1, 6, 5);
    m->record[0x188] = 6;
    CHECK(cok_combat_kill(&game, m, 4, "dies") && m->record[0x188] == 6 && m->record[0x197] == 10);

    /* 60f4:2375: one exploding is not hurt by another's explosion: with
     * no one else near, no die is rolled. */
    reset("");
    a = record('A', 0, 20, 20);
    x = record('X', 1, 6, 5);
    cok_character *z = record('Z', 1, 7, 5);
    x->record[0x188] = z->record[0x188] = 10;
    x->record[0x189] = z->record[0x189] = 0;
    c->exploding[1] = x;
    c->exploding[2] = z;
    c->exploding_count = 2;
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_explode(&game) && LOGGED("print: X;print: explodes.;"));
    CHECK(game.vm.seed == seed);
    CHECK(LOGGED("print: Z;print: explodes.;") && !LOGGED("print: Z;print: takes"));
    CHECK(!LOGGED("print: X;print: takes"));

    /* 60f4:1db7: "Goes Down" from the row after the text. */
    reset("");
    m = record('M', 1, 6, 5);
    track_rows = true;
    CHECK(cok_cast_damage(&game, m, 12, 0, false) && m->record[0x188] == 5);
    CHECK(strstr(rows, "M@10;takes 12 points of damage from Magic@10;M@14;Goes Down, and is Dying@14;") !=
          NULL);
}

/* The handlers the battle's start of a turn (event 0x0f) and endings
 * reach: 0x30, 0x3c, 0x42, 0x4c, 0x6a. */
static void test_turn_handlers(void)
{
    cok_combat *c = &game.combat;
    /* Immolation (0x30): "immolates", then 1d6 to each other within a
     * square that can act, in the order listed. */
    reset("");
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    cok_character *n = record('N', 0, 5, 6), *x = record('X', 1, 4, 4);
    record('F', 1, 9, 9);
    x->record[0x189] = 0;
    CHECK(cok_character_add_effect(a, 0x30, 0, 0, false) != NULL);
    unsigned six[] = {6, 2, 2, 6, 5, 5};
    game.vm.seed = seed_seq(2, six);
    uint32_t seed = game.vm.seed;
    for (int k = 0; k < 2; ++k) cok_tp_random(&seed, 6);
    CHECK(run(a, 0x0f) && LOGGED("print: A;print: immolates;"));
    CHECK(m->record[0x197] + n->record[0x197] == 13 && x->record[0x197] == 10);
    CHECK(a->record[0x197] == 10 && game.vm.seed == seed);
    CHECK(strstr(s.log, "immolates") < strstr(s.log, "print: M;print: takes"));
    /* Off the map, none listed: refused, as the original's count less 1
     * wraps to 255. */
    c->combatant[1].size = 0;
    CHECK(cok_combat_occupy(c));
    CHECK(!run(a, 0x0f) && strstr(game.effects.error, "3f44:163b") != NULL);
    /* More than eight around: refused, as the original copies the ninth
     * over its loop's count. */
    reset("");
    a = record('A', 0, 10, 10);
    a->record[0xcf] = 3;
    c->combatant[1].size = 3;
    CHECK(cok_combat_occupy(c));
    for (int k = 0; k < 9; ++k) record((char)('B' + k), 1, (int8_t)(9 + k % 4), k < 4 ? 9 : 11);
    CHECK(cok_character_add_effect(a, 0x30, 0, 0, false) != NULL);
    CHECK(!run(a, 0x0f) && strstr(game.effects.error, "3f44:15cd") != NULL);

    /* Bursting (0x3c) when it ends: those around listed, it "explodes!" and
     * is gone; each other that can act takes 3d6 and, unless it saves
     * (type 4), "is stunned" with 0x6a for good. */
    reset("");
    a = record('A', 0, 5, 5);
    m = record('M', 1, 6, 5);
    n = record('N', 1, 4, 5);
    m->record[0xd4] = 0xff;
    n->record[0xd4] = 0;
    m->record[0x197] = n->record[0x197] = 50;
    cok_effect *burst = cok_character_add_effect(a, 0x3c, 0, 0, true);
    /* Run but not ending (60f4:01a8 with flag 0): nothing. */
    seed = game.vm.seed;
    CHECK(cok_effects_run(&game.effects, a, 0x3c, burst) && a->record[0x188] == 0);
    CHECK(game.vm.seed == seed && !LOGGED("explodes!"));
    unsigned dice[] = {6, 1, 1, 6, 1, 1, 6, 1, 1, 20, 2, 19, 6, 2, 2, 6, 2, 2, 6, 2, 2, 20, 2, 19};
    game.vm.seed = seed_seq(8, dice);
    CHECK(cok_effects_remove(&game.effects, a, burst, 0x3c));
    CHECK(a->record[0x188] == 8 && LOGGED("print: A;print: explodes!;"));
    CHECK(m->record[0x197] == 47 && n->record[0x197] == 44);
    CHECK(LOGGED("print: M;print: is stunned;") && !LOGGED("print: N;print: is stunned;"));
    cok_effect *stun = cok_character_find_effect(m, 0x6a);
    CHECK(stun != NULL && stun->duration == 0 && stun->value == 0xff && !stun->on_remove);
    CHECK(cok_character_find_effect(n, 0x6a) == NULL);
    /* Stunned (0x6a): its handler ends the turn. */
    m->combat->initiative = 3;
    stun->on_remove = true;
    CHECK(cok_effects_remove(&game.effects, m, stun, 0x6a) && m->combat->initiative == 0);
    /* The dead that explode then go off: one the burst dropped with 0x44. */
    reset("");
    a = record('A', 0, 5, 5);
    n = record('N', 1, 4, 5);
    n->record[0x197] = 3;
    CHECK(cok_character_add_effect(n, 0x44, 0, 0xff, false) != NULL);
    burst = cok_character_add_effect(a, 0x3c, 0, 0, true);
    CHECK(cok_effects_remove(&game.effects, a, burst, 0x3c));
    CHECK(LOGGED("print: N;print: explodes.;") && n->record[0x188] == 6);
    CHECK(c->exploding_count == 0);
    /* The list is read after the death: one bursting that also bears 0x43
     * has the killer's distance worked out (6346:2888) at its death, which
     * lists all around the killer and puts back the entries, not the
     * count: the far one listed fourth before is hurt too. */
    reset("");
    a = record('A', 0, 5, 5);
    x = record('X', 1, 6, 5);
    n = record('N', 1, 7, 5);
    m = record('M', 1, 20, 20);
    give(a, 0x12, true, 0);
    a->record[0x17] = 3;
    game.vm.character = a->record;
    a->record[0x197] = n->record[0x197] = m->record[0x197] = 100;
    a->record[0xd4] = n->record[0xd4] = m->record[0xd4] = 0;
    CHECK(cok_combat_list(c, 5, 5, 0xff, 0xff, 1) && c->listed_count == 4);
    CHECK(c->listed[4].index == 4);
    CHECK(cok_character_add_effect(x, 0x43, 0, 0xff, false) != NULL);
    burst = cok_character_add_effect(x, 0x3c, 0, 0, true);
    CHECK(cok_effects_remove(&game.effects, x, burst, 0x3c) && x->record[0x188] == 8);
    CHECK(LOGGED("print: A;print: loses his weapon;"));
    CHECK(a->record[0x197] < 100 && n->record[0x197] < 100 && m->record[0x197] < 100);

    /* Zapping (0x42): the first listed within a square on another side
     * "gets zapped!", takes 2d6, and the holder's turn ends. */
    reset("");
    a = record('A', 0, 5, 5);
    cok_character *b = record('B', 0, 6, 5);
    m = record('M', 1, 5, 6);
    n = record('N', 1, 4, 6);
    a->combat->initiative = 3;
    CHECK(cok_character_add_effect(a, 0x42, 0, 0, false) != NULL);
    unsigned two[] = {6, 3, 3, 6, 4, 4};
    game.vm.seed = seed_seq(2, two);
    CHECK(run(a, 0x0f) && LOGGED("print: M;print: gets zapped!;"));
    CHECK(m->record[0x197] == 3 && n->record[0x197] == 10 && b->record[0x197] == 10);
    CHECK(a->combat->initiative == 0);
    /* None on another side: nothing. */
    reset("");
    a = record('A', 0, 5, 5);
    record('B', 0, 6, 5);
    record('M', 1, 9, 9);
    a->combat->initiative = 3;
    CHECK(cok_character_add_effect(a, 0x42, 0, 0, false) != NULL);
    seed = game.vm.seed;
    CHECK(run(a, 0x0f) && a->combat->initiative == 3 && game.vm.seed == seed);

    /* Gating (0x4c): below half its hit points, those waiting (status 9)
     * are put back with their full hit points around the first record on
     * another side, a direction each from north on; "gates in"; its 0x4c
     * goes. */
    reset("");
    a = record('A', 0, 5, 5);
    cok_character *l = record('L', 1, 12, 12);
    cok_character *w1 = record('W', 1, 20, 20), *w2 = record('V', 1, 21, 20);
    cok_character *w3 = record('U', 1, 22, 20);
    for (int k = 0; k < 3; ++k) {
        cok_character *w = k == 0 ? w1 : k == 1 ? w2 : w3;
        w->record[0x188] = 9;
        w->record[0x189] = 0;
        w->record[0x197] = 0;
        w->record[0x62] = 7;
        c->combatant[3 + k].size = 0;
    }
    w3->record[0x188] = 6; /* not waiting: the gating stops before it */
    CHECK(cok_combat_occupy(c));
    CHECK(cok_character_add_effect(l, 0x4c, 0, 0, false) != NULL);
    l->record[0x197] = 5; /* half: nothing */
    CHECK(run(l, 0x0f) && w1->record[0x188] == 9 && cok_character_find_effect(l, 0x4c) != NULL);
    l->record[0x197] = 4;
    CHECK(run(l, 0x0f) && cok_character_find_effect(l, 0x4c) == NULL);
    CHECK(w1->record[0x188] == 0 && w1->record[0x197] == 7 && w2->record[0x188] == 0);
    CHECK(c->combatant[3].x == 5 && c->combatant[3].y == 4 && c->combatant[3].size == 1);
    CHECK(c->combatant[4].x == 6 && c->combatant[4].y == 4 && c->combatant[4].size == 1);
    CHECK(w3->record[0x188] == 6 && c->combatant[5].size == 0);
    CHECK(LOGGED("print: W;print: gates in;") && LOGGED("print: V;print: gates in;"));
    /* A cell taken or not floor is passed by. */
    reset("");
    a = record('A', 0, 5, 5);
    record('B', 0, 5, 4);
    c->cells[4][6] = 0x1a;
    l = record('L', 1, 12, 12);
    w1 = record('W', 1, 20, 20);
    w1->record[0x188] = 9;
    w1->record[0x189] = 0;
    c->combatant[4].size = 0;
    CHECK(cok_combat_occupy(c));
    CHECK(cok_character_add_effect(l, 0x4c, 0, 0, false) != NULL);
    l->record[0x197] = 1;
    CHECK(run(l, 0x0f) && c->combatant[4].x == 6 && c->combatant[4].y == 5);
    /* Its enemy's eight cells all taken: the next record in the list is
     * on the holder's side, so lastly around the holder itself. */
    reset("");
    a = record('A', 0, 5, 5);
    record('K', 1, 30, 10);
    l = record('L', 1, 12, 12);
    w1 = record('W', 1, 20, 20);
    w1->record[0x188] = 9;
    w1->record[0x189] = 0;
    c->combatant[4].size = 0;
    CHECK(cok_combat_occupy(c));
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx != 0 || dy != 0) c->cells[5 + dy][5 + dx] = 0x1a;
    CHECK(cok_character_add_effect(l, 0x4c, 0, 0, false) != NULL);
    l->record[0x197] = 1;
    CHECK(run(l, 0x0f) && c->combatant[4].x == 12 && c->combatant[4].y == 11);
    /* One that does not fit (two across, over B) moves on all the same:
     * the last not put back, the next free cell places the record after
     * the last, through NULL: refused. */
    reset("");
    a = record('A', 0, 5, 5);
    record('B', 0, 6, 4);
    l = record('L', 1, 12, 12);
    w1 = record('W', 1, 20, 20);
    w1->record[0x188] = 9;
    w1->record[0x189] = 0;
    w1->record[0xcf] = 2;
    c->combatant[4].size = 0;
    c->combatant[3].size = 0;
    CHECK(cok_combat_occupy(c));
    CHECK(cok_character_add_effect(l, 0x4c, 0, 0, false) != NULL);
    l->record[0x197] = 1;
    CHECK(!run(l, 0x0f) && game.vm.status == COK_ECL_UNDEFINED);
    CHECK(strstr(game.error, "3f44:28e2") != NULL);
    /* Around one on the map's edge the original tries the cell off the
     * map forever: refused. */
    reset("");
    a = record('A', 0, 5, 0);
    l = record('L', 1, 12, 12);
    w1 = record('W', 1, 20, 20);
    w1->record[0x188] = 9;
    w1->record[0x189] = 0;
    CHECK(cok_character_add_effect(l, 0x4c, 0, 0, false) != NULL);
    l->record[0x197] = 1;
    CHECK(!run(l, 0x0f) && game.vm.status == COK_ECL_UNDEFINED);
}

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
    cok_adventure_hooks hooks = {.log = log_line, .delay = delay_line, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    load_screen();
    test_tables();
    test_hit();
    test_book();
    test_backstab();
    test_range();
    test_damage_roll();
    test_strike();
    test_attack();
    test_sweep();
    test_can_attack();
    test_pick_target();
    test_step();
    test_opportunity();
    test_flee();
    test_kill();
    test_weapons();
    test_melee();
    test_explode();
    test_damage();
    test_handlers();
    test_quirks();
    test_turn_handlers();
    cok_adventure_close(&game);
    puts("attack tests passed");
    return 0;
}
