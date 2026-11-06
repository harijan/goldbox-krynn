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

#define LOGGED(text) (strstr(log_text, text) != NULL)

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
    char text[2] = {name, '\0'}; /* Dark Queen's name is a C string */
    cok_field_set_name(L, c->record, text, 1);
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
    CHECK(cok_combat_side_list(&game, c, 0xff, false, true, &n) && n == 2);
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

/* Slot 1's dice of c in Dark Queen's layout: count and sides, and the damage bonus (177e:0be2's
 * +0x192, +0x194, +0x196 for slot 1). */
static void dice(cok_character *c, uint8_t count, uint8_t sides, int8_t bonus)
{
    const cok_character_layout *L = R->character;
    c->record[cok_field_offset_unchecked(L, COK_FIELD_DICE, 0, 1)] = count;
    c->record[cok_field_offset_unchecked(L, COK_FIELD_DICE_SIDES, 0, 1)] = sides;
    c->record[cok_field_offset_unchecked(L, COK_FIELD_DAMAGE_BONUS, 0, 1)] = (uint8_t)bonus;
}

/* Dark Queen's to-hit bonus (dqk 148f:1771's step 4): a party member's var 0x2f plus the
 * difficulty's adjustment (1 +2, 2 +1, 3 0, 4 -1, 5 -2), a monster's var 0x01 with none. The roll
 * is 8 and thac0 10: a party member at difficulty 1 hits AC 20, at 5 hits 16 but not 20. A monster
 * with var 0x01 1 hits 19 but not 20, even at difficulty 1. A 17 that hits is critical (DS:c6b9), a
 * 17 that misses is not. */
static void test_hit_bonus(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0), *m = record('M', 1, 0);
    place(a, 5, 5);
    place(m, 6, 5);
    cok_field_set8(R->character, a->record, COK_FIELD_THAC0, 10);
    cok_field_set8(R->character, m->record, COK_FIELD_THAC0, 10);
    cok_ecl_set_var(&game.vm, COK_VAR_HIT_PARTY, 0);
    cok_ecl_set_var(&game.vm, COK_VAR_HIT_MONSTERS, 0);
    const unsigned eight[] = {20, 8, 8}, seventeen[] = {20, 17, 17};
    const struct { uint8_t difficulty, ac; bool hit; } party[] = {
        {1, 20, true}, {5, 20, false}, {5, 16, true}};
    for (size_t i = 0; i < sizeof party / sizeof *party; ++i) {
        cok_ecl_set_var(&game.vm, COK_VAR_DIFFICULTY, party[i].difficulty);
        game.vm.seed = seed_from(1, eight);
        bool hit = !party[i].hit;
        CHECK(cok_combat_hit(&game, a, m, party[i].ac, &hit) && hit == party[i].hit);
    }
    cok_ecl_set_var(&game.vm, COK_VAR_DIFFICULTY, 1);
    cok_ecl_set_var(&game.vm, COK_VAR_HIT_MONSTERS, 1);
    game.vm.seed = seed_from(1, eight);
    bool hit = false;
    CHECK(cok_combat_hit(&game, m, a, 19, &hit) && hit);
    game.vm.seed = seed_from(1, eight);
    CHECK(cok_combat_hit(&game, m, a, 20, &hit) && !hit);
    /* A 17: critical when it hits (AC 27 with thac0 10 and no bonus at difficulty 3), not when it
     * misses (AC 28). */
    cok_ecl_set_var(&game.vm, COK_VAR_DIFFICULTY, 3);
    cok_ecl_set_var(&game.vm, COK_VAR_HIT_MONSTERS, 0);
    game.effects.rolls.critical = false;
    game.vm.seed = seed_from(1, seventeen);
    CHECK(cok_combat_hit(&game, a, m, 27, &hit) && hit && game.effects.rolls.critical);
    game.vm.seed = seed_from(1, seventeen);
    CHECK(cok_combat_hit(&game, a, m, 28, &hit) && !hit && !game.effects.rolls.critical);
    cok_ecl_set_var(&game.vm, COK_VAR_DIFFICULTY, 1);
    battle_clear();
}

/* Dark Queen's damage roll (dqk 52db:01dd): a total below 0 is 0 (1d4 with bonus -5 is not a
 * wrapped byte, in amount16 or amount). A thief's backstab multiplies the word: 2 at level 1, 5 at
 * level 13 and at 17 (capped), 5 for level 0 by effect 0xbe, and 5 for level 0x80 (a non-zero
 * level, unsigned: 52db:278f's test and 01dd's zero-extended level). A target at 255 hit points with 300 dealt (a word)
 * dies (status 6). */
static void test_damage(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0), *t = record('T', 1, 0);
    place(a, 5, 5);
    place(t, 6, 5);
    dice(a, 1, 4, -5);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 1, 4});
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_damage_roll(&game, a, t, 1) && game.effects.rolls.amount16 == 0 &&
          game.effects.rolls.amount == 0);
    CHECK(game.vm.seed == after_draws(seed, 1, 4));
    /* The backstab: the target hit twice, a square away and turned to face the attacker. */
    struct { uint8_t level; bool effect; int16_t times; } thieves[] = {
        {1, false, 2}, {13, false, 5}, {17, false, 5}, {0, true, 5}, {0x80, false, 5}};
    for (size_t i = 0; i < sizeof thieves / sizeof *thieves; ++i) {
        battle_clear();
        a = record('A', 0, 0);
        place(a, 5, 5);
        t = record('T', 1, 0);
        place(t, 6, 5);
        dice(a, 1, 4, 0);
        cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 6, thieves[i].level);
        if (thieves[i].effect) CHECK(cok_character_add_effect(a, 0xbe, 0, 0, false) != NULL);
        t->combat->hits = 2;
        uint8_t d;
        CHECK(cok_combat_direction(&game.combat, a, t, &d));
        t->combat->facing = d;
        game.vm.seed = seed_from(1, (const unsigned[]){4, 3, 3});
        CHECK(cok_combat_damage_roll(&game, a, t, 1) &&
              game.effects.rolls.amount16 == 3 * thieves[i].times);
    }
    battle_clear();
    t = record('T', 1, 0);
    place(t, 6, 5);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 255);
    CHECK(cok_combat_damage(&game.combat, t, R, 300, 0) &&
          cok_combat_field_word(R, t->record, COK_FIELD_STATUS) == 6 &&
          cok_field_u8(R->character, t->record, COK_FIELD_ACTIVE) == 0);
    battle_clear();
}

/* Forgetting (dqk 177e:1346): the first of Dark Queen's 0x89 memorized bytes from +0xce that is the
 * spell is marked 0x80, the 0x50th byte of the field included (its count is 0x4a). */
static void test_forget(void)
{
    cok_character *c = record('C', 0, 0);
    uint8_t *r = c->record;
    memset(r + 0xce, 0, 0x89);
    r[0xce + 0x50] = 0x21;
    cok_combat_forget(R, r, 0x21);
    CHECK(r[0xce + 0x50] == 0xa1);
    r[0xce + 0x10] = 0x22;
    cok_combat_forget(R, r, 0x22);
    CHECK(r[0xce + 0x10] == 0xa2);
    battle_clear();
}

/* The icon slot the fight's records draw from: the monsters' CPIC 64 (both images), loaded by
 * main, so that a facing above 3 has a picture to mirror. */
#define ICON_SLOT 8

/* A swing's attacker: one party record with slot 1's two attacks, 1d4 with no bonus, thac0 10, and
 * a target beside it, a monster of 10 hit points, both up (record() leaves ACTIVE 0). Var 0x12 (the
 * speed) is 0, so the waits draw nothing. */
static void duel_at(cok_character **a, cok_character **t, int8_t tx, int8_t ty)
{
    battle_clear();
    *a = record('A', 0, 0);
    place(*a, 5, 5);
    *t = record('M', 1, 0);
    place(*t, tx, ty);
    cok_field_set8(R->character, (*a)->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(R->character, (*t)->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(R->character, (*a)->record, COK_FIELD_ICON, ICON_SLOT);
    cok_field_set8(R->character, (*t)->record, COK_FIELD_ICON, ICON_SLOT);
    (*a)->combat->attack_slot = 1;
    cok_field_set8_at(R->character, (*a)->record, COK_FIELD_ATTACKS_LEFT, 1, 2);
    dice(*a, 1, 4, 0);
    cok_field_set8(R->character, (*a)->record, COK_FIELD_THAC0, 10);
    cok_field_set8(R->character, (*t)->record, COK_FIELD_HIT_POINTS, 10);
    memset(game.combat.pending, 0, sizeof game.combat.pending);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    log_text[0] = '\0';
    game.error[0] = '\0';
    game.vm.status = COK_ECL_OK;
}

static void duel(cok_character **a, cok_character **t)
{
    duel_at(a, t, 6, 5);
}

/* Dark Queen's strike (dqk 52db:1417): a swing that misses (d20 1), then one that hits (d20 20,
 * then 1d4): the seed is after the three draws, the hit's sound is 8 (the miss's, 0x0a, is not
 * made), and the swings used up end the turn. Its sweeps are kept. */
static void test_strike(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    game.vm.seed = seed_from(3, (const unsigned[]){20, 1, 1, 20, 20, 20, 4, 1, 4});
    uint32_t seed = game.vm.seed;
    bool over = false;
    a->combat->sweeps = 2;
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
    uint32_t expect = after_draws(seed, 2, 20);
    cok_dice(&expect, 1, 4);
    CHECK(game.vm.seed == expect);
    CHECK(LOGGED("sound: 8;") && !LOGGED("sound: 10;") && !LOGGED("sound: 7;"));
    CHECK(a->combat->sweeps == 2);
    /* The miss with no item sounds 0x0a; with an item (the missile's) not. */
    duel(&a, &t);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
    over = false;
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over && LOGGED("sound: 10;") &&
          LOGGED("and Misses"));
    duel(&a, &t);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
    CHECK(cok_combat_strike(&game, a, t, false, true, &over) && !LOGGED("sound: 10;") &&
          LOGGED("and Misses"));
    /* A pending list stops the strike after its slot's attacks, before the miss is said. */
    duel(&a, &t);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
    game.combat.pending[1] = 1;
    CHECK(!cok_combat_strike(&game, a, t, false, false, &over) &&
          game.vm.status == COK_ECL_UNPORTED && strstr(game.error, "148f:2ab8") != NULL &&
          !LOGGED("Misses") && !LOGGED("sound: 10;"));
    battle_clear();
}

/* A hit for no damage (1d4 with bonus -5) by a holder of effect 0xb8 runs the next slot's event
 * (dqk 52db:1417's 0xb8 test): the effect has no handler yet (dqk 78e4:3d76), so the strike stops
 * there, cited; without the effect the same hit is no event. */
static void test_hit_event(void)
{
    for (int holds = 0; holds < 2; ++holds) {
        cok_character *a, *t;
        duel(&a, &t);
        dice(a, 1, 4, -5);
        cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
        if (holds) CHECK(cok_character_add_effect(a, 0xb8, 0, 0, false) != NULL);
        game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 1, 4});
        bool over = false;
        bool ok = cok_combat_strike(&game, a, t, false, false, &over);
        if (holds)
            CHECK(!ok && strstr(game.error, "78e4:3d76") != NULL);
        else
            CHECK(ok && over && LOGGED("sound: 8;"));
    }
    battle_clear();
}

/* A helpless target (effect 0x33) takes the cruel blow of its hit points + 5 as a word (dqk
 * 52db:1417): 260 on 255 hit points is 5 over, dying (status 5), where a byte would leave 4 of
 * them (status 0). */
static void test_helpless_blow(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 255);
    CHECK(cok_character_add_effect(t, 0x33, 0, 0, false) != NULL);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 1, 4});
    bool over = false;
    cok_combat_strike(&game, a, t, false, false, &over);
    CHECK(cok_combat_field_word(R, t->record, COK_FIELD_STATUS) == 5);
    CHECK(LOGGED("print: slays helpless;") && LOGGED("attack: M with one cruel blow;"));
    battle_clear();
}

/* Dark Queen's report (dqk 52db:02c0's words through cok_arena_say and the log): a hit for 1 and
 * for 5 (the point and the points), a miss, a backstab (the header "-Backstabs-") and one from
 * behind ("(from behind) "). The strikes are made with one swing: d20 20 hits (a critical), then
 * the 1d4 with bonus -3 (1 damage) or +4 (5 damage); a miss is a d20 of 1. */
static void test_report_text(void)
{
    cok_character *a, *t;
    struct { int bonus; uint8_t roll; bool behind; const char *text; } hits[] = {
        {-3, 4, false, "attack: M Hitting for 1 point of damage;"},
        {4, 1, false, "attack: M Hitting for 5 points of damage;"},
        {-3, 4, true, "attack: M (from behind) Hitting for 1 point of damage;"}};
    for (size_t i = 0; i < sizeof hits / sizeof *hits; ++i) {
        duel(&a, &t);
        dice(a, 1, 4, (int8_t)hits[i].bonus);
        cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
        /* The target's rear armour class is 254 here (its stats, with no armour): the blow needs
         * a thac0 of 0xff to reach it from behind. */
        cok_field_set8(R->character, a->record, COK_FIELD_THAC0, 0xff);
        game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, hits[i].roll, hits[i].roll});
        bool over = false;
        CHECK(cok_combat_strike(&game, a, t, hits[i].behind, false, &over) && over);
        CHECK(LOGGED(hits[i].text));
    }
    duel(&a, &t);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
    bool over = false;
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
    CHECK(LOGGED("attack: M and Misses;"));
    /* A backstab: a thief (class level 6), the target hit twice and facing the attacker. */
    duel(&a, &t);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 6, 1);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    cok_field_set8(R->character, a->record, COK_FIELD_THAC0, 0xff); /* against the rear AC, 250 */
    uint8_t d;
    CHECK(cok_combat_direction(&game.combat, a, t, &d));
    t->combat->hits = 2;
    t->combat->facing = d;
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 2, 2});
    over = false;
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
    CHECK(LOGGED("print: -Backstabs-;") && LOGGED("attack: M Hitting for "));
    battle_clear();
}

/* The report's cells (dqk 52db:02c0's 1a62:0311 from 0x17, 13 to 0x26, 16, in 177e:156e's
 * cleared rows 12-21): with the pause passing, the text is drawn on row 13 in cells 0x17-0x26, row
 * 12 past the name's cell is still the window's grey, and row 17 there too (the pixel of row 21's
 * cell). The screen is VGA: a byte a pixel, 8 units of 8 to a row. */
static int spy_armed;
static size_t spy_row12, spy_row13, spy_row17;

/* The pixels in row's cells from x1 to 0x26 that differ from grey. */
static size_t ink(const cok_adventure *g, int row, int x1, uint8_t grey)
{
    size_t n = 0;
    const cok_picture *p = &g->screen;
    for (int y = row * 8; y < row * 8 + 8; ++y)
        for (int x = x1 * 8; x < (0x26 + 1) * 8; ++x)
            n += p->pixels[(size_t)y * p->units * 8 + (size_t)x] != grey;
    return n;
}

static void delay_spy(cok_adventure *g, unsigned ms, void *context)
{
    (void)context;
    if (!spy_armed || ms == 0) return;
    spy_armed = 0;
    uint8_t grey = g->screen.pixels[21 * 8 * g->screen.units * 8 + 0x17 * 8];
    spy_row12 = ink(g, 12, 0x18, grey); /* past the name's cell 0x17 */
    spy_row13 = ink(g, 13, 0x17, grey);
    spy_row17 = ink(g, 17, 0x17, grey);
}

static void test_report_cells(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 4);
    dice(a, 1, 4, -3);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 4, 4});
    game.hooks.delay = delay_spy;
    spy_armed = 1;
    bool over = false;
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
    game.hooks.delay = NULL;
    CHECK(spy_row12 == 0 && spy_row13 > 0 && spy_row17 == 0);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    battle_clear();
}

/* The report's pause (dqk 1a62:0949: var 0x12 times 100 ms, one draw unless it is 0): one draw per
 * report, a hit (after its d20 and damage) or a miss (after its d20); none at speed 0. */
static void test_report_pause(void)
{
    for (int speed = 0; speed < 5; speed += 4) {
        cok_character *a, *t;
        duel(&a, &t);
        cok_ecl_set_var(&game.vm, COK_VAR_SPEED, (uint16_t)speed);
        dice(a, 1, 4, -3);
        cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
        game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 4, 4});
        uint32_t seed = game.vm.seed;
        bool over = false;
        CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
        CHECK(game.vm.seed == after_draws(seed, speed != 0 ? 3 : 2, 20));
        duel(&a, &t);
        cok_ecl_set_var(&game.vm, COK_VAR_SPEED, (uint16_t)speed);
        cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
        game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
        seed = game.vm.seed;
        CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
        CHECK(game.vm.seed == after_draws(seed, speed != 0 ? 2 : 1, 20));
    }
    battle_clear();
}

/* A hit on a target casting (dqk 52db:02c0's "lost a spell", 177e:156e with its wait, then
 * 177e:1346 and 51c2:0c11): the pause and the say's wait are two draws, the memorized byte is
 * marked 0x80 and the target's turn ends (spell and initiative 0). */
static void test_report_spell(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 4);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    dice(a, 1, 4, -3);
    memset(t->record + 0xce, 0, 0x89);
    t->record[0xce + 0x50] = 0x21;
    t->combat->spell = 0x21;
    t->combat->initiative = 5;
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 4, 4});
    uint32_t seed = game.vm.seed;
    bool over = false;
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
    CHECK(game.vm.seed == after_draws(seed, 4, 20));
    CHECK(LOGGED("print: lost a spell;") && t->record[0xce + 0x50] == 0xa1);
    CHECK(t->combat->spell == 0 && t->combat->initiative == 0);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    battle_clear();
}

/* A target brought down (dqk 52db:02c0's 5837:0d55 after "is killed" and the event 0x0d): the
 * report stops there, cited, as the death on the battlefield is not ported (part 16). */
static void test_report_kill(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    dice(a, 1, 4, 20);
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 1, 4});
    bool over = false;
    CHECK(!cok_combat_strike(&game, a, t, false, false, &over));
    CHECK(game.vm.status == COK_ECL_UNPORTED && strstr(game.error, "5837:0d55") != NULL);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_ACTIVE) == 0);
    battle_clear();
}

/* One brought down whose creature flags hold bit 8 (dqk 52db:0529-0532, +0xc8 & 8) only goes
 * down: "goes down" is said, but neither "and is Dying" nor "is killed", and the death still stops
 * at 5837:0d55. */
static void test_report_quiet(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1,
                      cok_field_u8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1) | 8);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    dice(a, 1, 4, 20);
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 1, 4});
    bool over = false;
    CHECK(!cok_combat_strike(&game, a, t, false, false, &over));
    CHECK(game.vm.status == COK_ECL_UNPORTED && strstr(game.error, "5837:0d55") != NULL);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_ACTIVE) == 0);
    CHECK(LOGGED("goes down;"));
    CHECK(!LOGGED("and is Dying") && !LOGGED("is killed"));
    battle_clear();
}

/* The target's facing (dqk 52db:18d6's 16dd: the direction from the target to the attacker, then
 * (d + 4) % 8), or 2 for a record with effect 0xd5 (177e:1734). */
static void test_attack_facing(void)
{
    /* The target below the attacker (the direction d from it is 0), hidden (its size 0: no pose
     * draws it, and a pose sets the facing as it is drawn): (d + 4) % 8, or 2 with effect 0xd5. */
    for (int with = 0; with < 2; ++with) {
        cok_character *a, *t;
        duel_at(&a, &t, 5, 6);
        cok_combat *k = &game.combat;
        k->combatant[cok_combat_index(k, t->record)].size = 0;
        dice(a, 1, 4, 0);
        cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
        if (with) CHECK(cok_character_add_effect(t, 0xd5, 0, 0, false) != NULL);
        uint8_t d;
        CHECK(cok_combat_direction(k, t, a, &d) && d == 0);
        game.vm.seed = 1;
        bool over = false;
        CHECK(cok_combat_attack(&game, a, t, false, 0, &over) && over);
        CHECK(t->combat->facing == (with ? 2 : 4));
    }
    battle_clear();
}

/* An attack's wait (dqk 52db:18d6's 1df9:052a, 100 ms): one draw before the swing's d20. The
 * swing is a miss (the d20 of 1), the seed after the wait's draw and that d20. */
static void test_attack_wait(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    dice(a, 1, 4, 0);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    uint32_t s = 1;
    while (draw_number(s, 2, 20) != 1) ++s;
    game.vm.seed = s;
    bool over = false;
    CHECK(cok_combat_attack(&game, a, t, false, 0, &over) && over);
    CHECK(game.vm.seed == after_draws(s, 2, 20) && LOGGED("attack: M and Misses;"));
    battle_clear();
}

/* An item or a readied weapon of type 0x1c (dqk 52db:18d6's 2888 calls, part 17): not ported, so
 * the attack stops, cited, after the wait. */
static void test_attack_missile(void)
{
    cok_character *a, *t;
    uint8_t item[COK_ITEM_SIZE] = {0};
    for (int weapon = 0; weapon < 2; ++weapon) {
        duel(&a, &t);
        CHECK(cok_character_insert_item(a, 0, item));
        if (weapon) {
            a->items[0][0x28] = 0x1c;
            a->slots[0] = 1;
        }
        bool over = false;
        CHECK(!cok_combat_attack(&game, a, t, false, weapon ? 0 : 1, &over));
        CHECK(game.vm.status == COK_ECL_UNPORTED && strstr(game.error, "52db:2888") != NULL);
    }
    battle_clear();
}

/* The sweep (dqk 52db:52e4): the list is not swapped for the one yelled at (54b5 has no swap), so
 * with the target second listed and the third yelled at, the swept are the target and the first
 * listed, the target put first; the third is not swept. Two sweeps with three listed and one attack
 * left: the sweep says "sweeps" first. */
static void test_sweep(void)
{
    cok_combat *k = &game.combat;
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 5, 5);
    cok_character *x = record('X', 1, 0), *y = record('Y', 1, 0), *z = record('Z', 1, 0);
    cok_field_set8(R->character, a->record, COK_FIELD_ICON, ICON_SLOT);
    cok_field_set8(R->character, x->record, COK_FIELD_ICON, ICON_SLOT);
    cok_field_set8(R->character, y->record, COK_FIELD_ICON, ICON_SLOT);
    cok_field_set8(R->character, z->record, COK_FIELD_ICON, ICON_SLOT);
    cok_field_set8(R->character, a->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(R->character, x->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(R->character, y->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(R->character, z->record, COK_FIELD_ACTIVE, 1);
    place(x, 6, 5);
    place(y, 5, 6);
    place(z, 4, 5);
    CHECK(cok_character_add_effect(a, 0x8c, 0, 0, false) != NULL);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    a->combat->sweeps = 2;
    dice(a, 1, 4, 0);
    uint8_t n;
    CHECK(cok_combat_side_list(&game, a, 1, false, false, &n) && n == 3);
    cok_character *target = k->combatant[k->enemies[2]].character;
    cok_character *first = k->combatant[k->enemies[1]].character;
    cok_character *third = k->combatant[k->enemies[3]].character;
    k->yelled = third->record;
    game.vm.seed = 1;
    bool swept = false;
    CHECK(cok_combat_sweep(&game, a, target, &swept) && swept);
    CHECK(k->combatant[k->enemies[1]].character == target);
    CHECK(target->combat->hits == 1 && first->combat->hits == 1 && third->combat->hits == 0);
    CHECK(LOGGED("print: sweeps;"));
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
    /* The battle's ground (set 1: no palette cycle, so no draw), for the report's window. */
    CHECK(cok_adventure_drawing(&game)->arena_tiles(&game, false, 1));
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, ICON_SLOT));
    test_leadership();
    test_fled();
    test_combat_records();
    test_aging();
    test_pick_listed();
    test_geometry();
    test_spell_choice();
    test_hit_bonus();
    test_damage();
    test_forget();
    test_strike();
    test_hit_event();
    test_helpless_blow();
    test_report_text();
    test_report_cells();
    test_report_pause();
    test_report_spell();
    test_report_kill();
    test_report_quiet();
    test_attack_facing();
    test_attack_wait();
    test_attack_missile();
    test_sweep();
    cok_party_free(&game.party);
    puts("queen fight tests passed");
    return 0;
}
