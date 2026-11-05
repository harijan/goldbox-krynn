/* The Dark Queen of Krynn's battle where it differs from Champions' and Death
 * Knights' (dqk 52db:4dd2, 4d50, the computer flags and leadership before 59c7:1dd7;
 * its var 0x36, FLED): on records made by hand in its layout, the game opened on
 * DQKAssets/ (skipped without). */
#include "adventure.h"
#include "attack.h"
#include "combat.h"
#include "ecl.h"
#include "party.h"
#include "queen.h"
#include "round.h"
#include "rules.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;
static cok_adventure game;
static char log_text[1 << 14];

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g, (void)context;
    size_t used = strlen(log_text);
    snprintf(log_text + used, sizeof log_text - used, "%s: %s;", kind, text);
}

static int no_key(void *context)
{
    (void)context;
    return -1;
}

/* A seed whose first d(sides) rolls are in their bounds (the triples: sides, low, high). */
static uint32_t seed_from(unsigned n, const unsigned *triples)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        bool ok = true;
        for (unsigned i = 0; i < n && ok; ++i) {
            unsigned d = cok_dice(&seed, 1, (uint8_t)triples[3 * i]);
            ok = d >= triples[3 * i + 1] && d <= triples[3 * i + 2];
        }
        if (ok) return start;
    }
}

/* A party record in Dark Queen's layout, named by a letter: fighting level 3 (its class
 * level 1), hostile or not, an NPC (the flag 0x80 at +0x9b) or not, intelligence 10. */
static cok_character *record(char name, uint8_t hostile, int npc)
{
    const cok_character_layout *L = R->character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL && cok_party_append(&game.party, c));
    cok_field_set8_at(L, c->record, COK_FIELD_NAME, 0, 1);
    cok_field_set8_at(L, c->record, COK_FIELD_NAME, 1, (uint8_t)name);
    cok_field_set8(L, c->record, COK_FIELD_HOSTILE, hostile);
    cok_field_set8(L, c->record, COK_FIELD_NPC, npc ? 0x80 : 0);
    cok_field_set8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE, 10);
    cok_field_set8_at(L, c->record, COK_FIELD_CLASS_LEVEL, 1, 3);
    cok_field_set8(L, c->record, COK_FIELD_HIT_POINTS, 10);
    return c;
}

static uint8_t computer(cok_character *c)
{
    return cok_field_u8(R->character, c->record, COK_FIELD_COMPUTER);
}

/* Leadership (dqk 52db:4dd2): the computer flags (+0x18c: 1 for a hostile, 0x80 for an NPC,
 * 0 else) before the leaders; a leader of order 1 (chance 20) leads the NPC on a roll below
 * 20 (no announcement: nothing logged), and the NPC's flag then clears; a roll of 60 leads
 * nothing and the flag holds. Dark Queen's 4d50 keeps no chance from one knight to the next
 * (its own frame): a knight of no order stops the run, where dkk's would use the last chance. */
static void test_leadership(void)
{
    const cok_character_layout *L = R->character;
    for (int lead = 0; lead < 2; ++lead) {
        cok_party_free(&game.party);
        log_text[0] = '\0';
        game.vm.status = COK_ECL_OK;
        cok_character *a = record('A', 0, 0), *n = record('N', 0, 1), *m = record('M', 1, 0);
        cok_field_set8(L, a->record, COK_FIELD_KNIGHT_ORDER, 1);
        const unsigned lead_dice[] = {100, 1, 19};
        const unsigned hold_dice[] = {100, 60, 100};
        game.vm.seed = seed_from(1, lead ? lead_dice : hold_dice);
        CHECK(cok_combat_leadership(&game) == true);
        CHECK(game.vm.status == COK_ECL_OK && strstr(log_text, "print:") == NULL);
        CHECK(computer(m) == 1);
        CHECK(computer(n) == (lead ? 0 : 0x80));
        CHECK(computer(a) == 0);
    }
    /* A leader of order 1 (its chance 20) does not lead the NPC on 100; then a leader of no
     * order: Dark Queen's chance is not kept from one call to the next, so it stops. */
    cok_party_free(&game.party);
    game.vm.status = COK_ECL_OK;
    cok_character *a = record('A', 0, 0), *b = record('B', 0, 0), *n = record('N', 0, 1);
    cok_field_set8(L, a->record, COK_FIELD_KNIGHT_ORDER, 1);
    cok_field_set8(L, b->record, COK_FIELD_KNIGHT_ORDER, 0);
    const unsigned dice[] = {100, 60, 100};
    game.vm.seed = seed_from(1, dice);
    CHECK(!cok_combat_leadership(&game) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "52db:4d50") != NULL && computer(n) == 0x80);
    game.vm.status = COK_ECL_OK;
    cok_party_free(&game.party);
}

/* Dark Queen's var 0x36 is FLED (59c7:1e3e clears it, 52db:2aef sets it). */
static void test_fled(void)
{
    CHECK(cok_ecl_has_var(R, COK_VAR_FLED) && R->ecl.vars[COK_VAR_FLED].address == 0x36);
    cok_ecl_set_var(&game.vm, COK_VAR_FLED, 1);
    CHECK(cok_ecl_var(&game.vm, COK_VAR_FLED) == 1);
    cok_ecl_set_var(&game.vm, COK_VAR_FLED, 0);
    CHECK(cok_ecl_var(&game.vm, COK_VAR_FLED) == 0);
}

/* The combat records (dqk 59c7:0f7b, its combat block): the party's direction's quadrant
 * (2 for direction 4), +4 for a hostile one; effect 0xd5 faces 2 whoever it is on; the
 * records past the party's size are not_party (block +0x14); every record is in_battle
 * (block +0x17); an NPC past the party gets the party size's byte + 0x80 (0f7b:10b1 reads
 * var 0x20); and with var 0x3c set, effect 0x95 goes to each record that is not hostile. */
static void test_combat_records(void)
{
    const cok_character_layout *L = R->character;
    const cok_combat_block *B = R->combat.combat_block;
    CHECK(B != NULL && B->spell == 0x00 && B->facing == 0x0a && B->not_party == 0x14 &&
          B->in_battle == 0x17);
    CHECK(cok_rules_champions.combat.combat_block == NULL &&
          cok_rules_death_knights.combat.combat_block == NULL);
    for (int gate = 0; gate < 2; ++gate) {
        cok_party_free(&game.party);
        game.vm.status = COK_ECL_OK;
        game.vm.direction = 4;
        cok_ecl_set_var(&game.vm, COK_VAR_PARTY_SIZE, 1);
        cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, (uint16_t)gate);
        cok_character *a = record('A', 0, 0), *n = record('N', 0, 1), *m = record('M', 1, 0);
        cok_character *x = record('X', 0, 0);
        cok_effect *fx = cok_character_add_effect(m, B->facing_effect, 0, 0, false);
        CHECK(fx != NULL);
        char error[300] = "";
        CHECK(cok_combat_records(&game, error, sizeof error));
        uint8_t quadrant = cok_data_u8_at(R->data, COK_DATA_QUADRANT_DIRECTIONS, 2);
        CHECK(a->combat->facing == quadrant && a->combat->not_party == 0);
        CHECK(n->combat->not_party == 1 && n->combat->facing == quadrant);
        CHECK(m->combat->facing == 2 && m->combat->not_party == 1);
        CHECK(x->combat->not_party == 1 && x->combat->in_battle == 1);
        CHECK(a->combat->in_battle == 1 && n->combat->in_battle == 1 && m->combat->in_battle == 1);
        CHECK(cok_field_u8(L, n->record, COK_FIELD_NPC) == 0x81);
        CHECK((cok_character_find_effect(a, B->gate_effect) != NULL) == (gate != 0));
        CHECK((cok_character_find_effect(n, B->gate_effect) != NULL) == (gate != 0));
        CHECK(cok_character_find_effect(m, B->gate_effect) == NULL);
    }
    /* Without the effect a hostile record faces the quadrant plus 4. */
    cok_party_free(&game.party);
    game.vm.direction = 4;
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    cok_character *h = record('H', 1, 0);
    char error[300] = "";
    CHECK(cok_combat_records(&game, error, sizeof error));
    CHECK(h->combat->facing == (cok_data_u8_at(R->data, COK_DATA_QUADRANT_DIRECTIONS, 2) + 4) % 8);
    cok_party_free(&game.party);
}

/* The aging report (dqk 5045:012f, the battle's end: rounds()'s aging_unported) is a cited stop
 * when effects.aging is set (the year's pass_time, 1396:0368), and the battle goes on past it when
 * not. The battle is over before its first round (COK_COMBAT_WON), so the stop is the one after
 * the loop; the battle's end (cok_combat_end) is run only when the report is not due. */
static void test_aging(void)
{
    for (int aged = 0; aged < 2; ++aged) {
        cok_party_free(&game.party);
        log_text[0] = '\0';
        game.vm.status = COK_ECL_OK;
        game.vm.abort = false;
        game.error[0] = '\0';
        game.combat_stub = COK_COMBAT_WON;
        game.effects.aging = aged != 0;
        /* A 3D area's battle ground: DUNGCOM's set 4 (test_dark_queen's arena_tiles). */
        cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
        game.wall_ids[0] = 4;
        /* Active (+0x18b 1): a record not active is a body, placed on its cell (1c0d). */
        const cok_character_layout *L = R->character;
        cok_field_set8(L, record('A', 0, 0)->record, COK_FIELD_ACTIVE, 1);
        cok_field_set8(L, record('B', 0, 0)->record, COK_FIELD_ACTIVE, 1);
        bool ok = cok_combat_battle(&game);
        if (aged) {
            CHECK(!ok && game.vm.status == COK_ECL_UNPORTED);
            CHECK(strstr(game.error, "the aging report") != NULL &&
                  strstr(game.error, "dqk 5045:012f") != NULL);
            CHECK(strstr(log_text, "combat: won;") != NULL);
        } else {
            CHECK(ok && game.vm.status == COK_ECL_OK && game.error[0] == '\0');
            CHECK(strstr(log_text, "combat: won;") != NULL);
        }
    }
    game.effects.aging = false;
    game.wall_ids[0] = 0;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
    game.combat_stub = COK_COMBAT_UNPORTED;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    cok_party_free(&game.party);
}

/* A record as a combatant at x, y on the battle's map (test_dark_queen's place), with a combat
 * record of its own. */
static void place(cok_character *c, int8_t x, int8_t y)
{
    cok_combat *k = &game.combat;
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    cok_combatant *e = &k->combatant[k->count];
    e->x = x;
    e->y = y;
    e->size = 1;
    e->character = c;
    ++k->count;
    k->combatant[k->count].size = 0;
    k->combatant[k->count].character = NULL;
    CHECK(cok_combat_occupy(k, R));
}

/* An empty battle: the floor everywhere, no combatant but the one that count 1 leaves (combatant 0
 * is no one's), no party, nothing listed, no one yelled. */
static void battle_clear(void)
{
    cok_combat *k = &game.combat;
    memset(k->cells, COK_COMBAT_FLOOR, sizeof k->cells);
    memset(k->occupant, 0, sizeof k->occupant);
    memset(k->combatant, 0, sizeof k->combatant);
    memset(k->enemies, 0, sizeof k->enemies);
    k->count = 1;
    k->listed_count = 0;
    k->see_all = 0;
    k->yelled = NULL;
    cok_party_free(&game.party);
}

/* The seed after n draws of d(sides) from seed, and the n-th draw's value (n from 1). */
static uint32_t after_draws(uint32_t seed, unsigned n, uint8_t sides)
{
    for (unsigned i = 0; i < n; ++i) cok_dice(&seed, 1, sides);
    return seed;
}

static uint8_t draw_number(uint32_t seed, unsigned n, uint8_t sides)
{
    seed = after_draws(seed, n - 1, sides);
    return cok_dice(&seed, 1, sides);
}

/* The pick of a target with flag (dqk 52db:3d9e, Dark Queen's pick_tries_listed): one draw first,
 * not used, then as many tries as listed. Three enemies, the first pick found: the seed is after
 * two draws (the unused one and the pick), and the target is the pick's combatant. With 20 listed
 * and the first of them yelled at (c holds effect 0x8c, COK_EFFECT_YELLED_AT), the yell takes the
 * head at tries 19 with no draw: the seed is after the unused draw only. With 19 listed the yell
 * does not apply: the try's pick is a draw, and a pick other than the head comes up here. */
static void test_pick_listed(void)
{
    cok_combat *k = &game.combat;
    bool found = false;
    battle_clear();
    cok_character *c = record('C', 0, 0);
    place(c, 5, 5);
    place(record('A', 1, 0), 8, 5);
    place(record('B', 1, 0), 5, 8);
    place(record('D', 1, 0), 2, 5);
    game.vm.seed = 0x1234;
    uint32_t want = after_draws(game.vm.seed, 2, 3);
    uint8_t pick = draw_number(game.vm.seed, 2, 3);
    CHECK(cok_combat_pick_target(&game, c, 0xff, true, false, &found) && found);
    CHECK(game.vm.seed == want);
    CHECK(c->combat->target == k->combatant[k->enemies[pick]].character->record);

    for (int listed = 20; listed >= 19; --listed) {
        battle_clear();
        c = record('C', 0, 0);
        place(c, 25, 12);
        cok_character *first = NULL;
        for (int i = 0; i < 20; ++i) {
            cok_character *m = record('M', 1, 0);
            place(m, (int8_t)(2 + i), 2);
            if (i == 0) first = m;
        }
        CHECK(cok_character_add_effect(c, 0x8c, 0, 0, false) != NULL);
        k->yelled = first->record;
        if (listed == 19) k->combatant[21].size = 0; /* the twentieth monster is not listed */
        /* A seed whose second draw, the pick of 19, is not the head (the 19 case only). */
        uint32_t seed = 1;
        while (draw_number(seed, 2, 19) == 1) ++seed;
        game.vm.seed = seed;
        found = false;
        if (listed == 20) {
            want = after_draws(seed, 1, 20);
            CHECK(cok_combat_pick_target(&game, c, 0xff, true, false, &found) && found);
            CHECK(game.vm.seed == want && c->combat->target == first->record);
        } else {
            want = after_draws(seed, 2, 19);
            uint8_t p = draw_number(seed, 2, 19);
            CHECK(cok_combat_pick_target(&game, c, 0xff, true, false, &found) && found);
            CHECK(game.vm.seed == want);
            CHECK(c->combat->target == k->combatant[k->enemies[p]].character->record);
            CHECK(c->combat->target != first->record);
        }
    }
    battle_clear();
}

/* The geometry of the battle's lists on a made-up map (dqk 5d71's routines, identical to the
 * shared ones: 0328 sight, 048f in_arc, 07d7 the list, 0b26 the distance, 5837:16dd the direction).
 * The map is floor with a wall at 11, 10 (value 1 of DS:6197: cost 255, eye 1, block 2, which
 * blocks the sight past eye 1). C is at 10, 10; A at 12, 10, behind the wall; B at 9, 8; D at
 * 13, 13. */
static void test_geometry(void)
{
    cok_combat *k = &game.combat;
    battle_clear();
    cok_character *c = record('C', 0, 0);
    cok_character *a = record('A', 1, 0), *b = record('B', 1, 0), *d = record('D', 1, 0);
    place(c, 10, 10);
    place(a, 12, 10);
    place(b, 9, 8);
    place(d, 13, 13);
    k->cells[10][11] = 1;
    /* The sight: the wall stops it at its cell (11, 10) after one step (2 half cells); with
     * see_all, the sight goes on to 12, 10 at 4 half cells. */
    int8_t x1 = 12, y1 = 10;
    uint16_t range = 0xff;
    bool seen = true;
    CHECK(cok_combat_sight(k, R, 10, 10, &x1, &y1, &range, &seen) && !seen);
    CHECK(x1 == 11 && y1 == 10 && range == 2);
    k->see_all = 1;
    x1 = 12;
    y1 = 10;
    range = 0xff;
    CHECK(cok_combat_sight(k, R, 10, 10, &x1, &y1, &range, &seen) && seen && range == 4);
    k->see_all = 0;
    /* The list: A is hidden, so B (5 half cells, north) and D (9, south-east) are listed nearest
     * first, in the order of the combatants and the directions 0 and 3. */
    uint8_t n = 0;
    CHECK(cok_combat_side_list(&game, c, 0xff, false, &n) && n == 2);
    CHECK(k->enemies[1] == 3 && k->enemies[2] == 4);
    CHECK(k->listed[1].distance == 5 && k->listed[1].direction == 0);
    CHECK(k->listed[2].distance == 9 && k->listed[2].direction == 3);
    /* The distance sees all: A at 4 half cells, so 2 cells; D 9, so 4. Its list holds C too (at 0,
     * the origin's own cell), so the count is 4; entry 1's index is put back after it (B's again),
     * and the count is not. */
    uint8_t dist = 0;
    CHECK(cok_combat_distance(k, R, c->record, a->record, &dist) && dist == 2);
    CHECK(cok_combat_distance(k, R, c->record, d->record, &dist) && dist == 4);
    CHECK(cok_combat_distance(k, R, c->record, b->record, &dist) && dist == 2);
    CHECK(k->listed[1].index == 3 && k->listed_count == 4);
    /* The direction of each wedge: a combatant four cells off on each of the eight lines. */
    static const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    battle_clear();
    cok_character *ring[8];
    c = record('C', 0, 0);
    place(c, 10, 10);
    for (unsigned w = 0; w < 8; ++w) {
        ring[w] = record('W', 1, 0);
        place(ring[w], (int8_t)(10 + 4 * dx[w]), (int8_t)(10 + 4 * dy[w]));
    }
    for (unsigned w = 0; w < 8; ++w) {
        uint8_t dir = 9;
        CHECK(cok_combat_direction(k, c, ring[w], &dir) && dir == w);
    }
    /* The arcs' corners: each neighbour of the combatant is in its own arc (and the cell itself)
     * and not in the opposite one. */
    for (unsigned w = 0; w < 8; ++w) {
        bool in = false;
        int8_t ahead_x = (int8_t)(10 + dx[w]), ahead_y = (int8_t)(10 + dy[w]);
        int8_t behind_x = (int8_t)(10 - dx[w]), behind_y = (int8_t)(10 - dy[w]);
        CHECK(cok_combat_in_arc(10, 10, ahead_x, ahead_y, (uint8_t)w, &in) && in);
        CHECK(cok_combat_in_arc(10, 10, 10, 10, (uint8_t)w, &in) && in);
        CHECK(cok_combat_in_arc(10, 10, behind_x, behind_y, (uint8_t)w, &in) && !in);
    }
    /* North's corner: (11, 8) is in the arc, (12, 8) is not; (9, 8) is, (8, 8) is not. */
    bool in = false;
    CHECK(cok_combat_in_arc(10, 10, 11, 8, 0, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 12, 8, 0, &in) && !in);
    CHECK(cok_combat_in_arc(10, 10, 9, 8, 0, &in) && in);
    CHECK(cok_combat_in_arc(10, 10, 8, 8, 0, &in) && !in);
    battle_clear();
}

/* The computer's spell choice's prefix (dqk 5bb8:0306, queen_ai.c): it casts nothing when fleeing,
 * when var COK_VAR_NO_MAGIC is set, when it may not cast, or when it is a monster with Magic off;
 * each of those returns with no stop. Anything else is the spell choice (part 29), a cited stop; so
 * is a monster with Magic on, and an NPC (+0x9b 0x80) even with Magic off. */
static void test_spell_choice(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 8, 5);
    cok_character *n = record('N', 0, 1);
    n->combat = calloc(1, sizeof *n->combat);
    CHECK(n->combat != NULL);
    bool cast = true;
    m->combat->may_cast = 1;
    game.combat.magic_on = false;
    cok_ecl_set_var(&game.vm, COK_VAR_NO_MAGIC, 0);
    /* Refusals: Magic off for a monster; fleeing; the var; may_cast 0 (with Magic on). */
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
    m->combat->fleeing = 1;
    game.combat.magic_on = true;
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
    m->combat->fleeing = 0;
    cok_ecl_set_var(&game.vm, COK_VAR_NO_MAGIC, 1);
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
    cok_ecl_set_var(&game.vm, COK_VAR_NO_MAGIC, 0);
    m->combat->may_cast = 0;
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
    /* Past them: the choice, a cited stop, for a monster with Magic on, and for an NPC with it
     * off. */
    m->combat->may_cast = 1;
    CHECK(!queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "dqk 5bb8:0306") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.combat.magic_on = false;
    n->combat->may_cast = 1;
    CHECK(!queen_spell_choice(&game, n, &cast) && !cast && game.vm.status == COK_ECL_UNPORTED);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.combat.magic_on = false;
    battle_clear();
}

int main(void)
{
    struct stat st;
    if (stat("DQKAssets/DISK2/DUNGCOM.TLB", &st) != 0) {
        puts("queen fight: no DQKAssets/; the battle's leadership not checked");
        return 0;
    }
    cok_keyboard keys = {no_key, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, &hooks));
    test_leadership();
    test_fled();
    test_combat_records();
    test_aging();
    test_pick_listed();
    test_geometry();
    test_spell_choice();
    cok_party_free(&game.party);
    puts("queen fight tests passed");
    return 0;
}
