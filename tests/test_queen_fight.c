/* The Dark Queen of Krynn's battle where it differs from Champions' and Death
 * Knights' (dqk 52db:4dd2, 4d50, the computer flags and leadership before 59c7:1dd7;
 * its var 0x36, FLED): on records made by hand in its layout, the game opened on
 * DQKAssets/ (skipped without). */
#include "adventure.h"
#include "arena.h"
#include "attack.h"
#include "cloud.h"
#include "combat.h"
#include "data.h"
#include "ecl.h"
#include "party.h"
#include "queen.h"
#include "round.h"
#include "rules.h"
#include "step.h"
#include "treasure.h"
#include "vga.h"

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
          B->in_battle == 0x17 && B->fleeing == 0x15);
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
 * record of its own, its footprint of size cells (place: 1). */
static void place_sized(cok_character *c, int8_t x, int8_t y, uint8_t size)
{
    cok_combat *k = &game.combat;
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    cok_combatant *e = &k->combatant[k->count];
    e->x = x;
    e->y = y;
    e->size = size;
    e->character = c;
    ++k->count;
    k->combatant[k->count].size = 0;
    k->combatant[k->count].character = NULL;
    CHECK(cok_combat_occupy(k, R));
}
static void place(cok_character *c, int8_t x, int8_t y)
{
    place_sized(c, x, y, 1);
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
    /* The bodies' table is cleared too (the battle's setup clears only its count; a stale entry
     * would be a freed record's address here). */
    k->bodies = 0;
    memset(k->body, 0, sizeof k->body);
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

/* The computer's spell choice (dqk 5bb8:0306, queen_ai.c): it casts nothing when fleeing, when var
 * COK_VAR_NO_MAGIC is set, when it may not cast, or when it is a monster with Magic off; and nothing
 * with no memorised spell (no draw). A monster with Magic on, and an NPC (+0x9b 0x80) even with
 * Magic off, go on to the choice, which is no stop: with the bytes at +0xce empty it casts nothing. */
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
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
    game.combat.magic_on = false;
    n->combat->may_cast = 1;
    CHECK(queen_spell_choice(&game, n, &cast) && !cast && game.vm.status == COK_ECL_OK);
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

/* Dark Queen's ranger (dqk 78e4:1aa2, 0x69, at the damage): the holder's target within a square of
 * creature flag 0 bit 3 takes the holder's ranger level (10 here) more on the word, and the byte
 * amount with it. Two squares away, or no bit 3, gives the roll alone. The word: 250 + 10 is 260,
 * amount 4. A removal changes nothing. A NULL target within a square fails, cited (0000:00c7). */
static void test_ranger(void)
{
    game.effects.in_battle = true;
    battle_clear();
    cok_character *a = record('A', 0, 0), *t = record('T', 1, 0);
    place(a, 5, 5);
    place(t, 6, 5);
    dice(a, 1, 4, 0);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 4, 10);
    cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 0, 8);
    CHECK(cok_character_add_effect(a, 0x69, 0, 0, false) != NULL);
    a->combat->target = t->record;
    game.vm.seed = seed_from(1, (const unsigned[]){4, 3, 3});
    CHECK(cok_combat_damage_roll(&game, a, t, 1) && game.effects.rolls.amount16 == 13 &&
          game.effects.rolls.amount == 13);
    /* Two squares away, and a target without bit 3: the roll alone. */
    for (int k = 0; k < 2; ++k) {
        battle_clear();
        a = record('A', 0, 0);
        place(a, 5, 5);
        t = record('T', 1, 0);
        place(t, k == 0 ? 7 : 6, 5);
        if (k == 1) cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 0, 0);
        else cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 0, 8);
        cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 4, 10);
        CHECK(cok_character_add_effect(a, 0x69, 0, 0, false) != NULL);
        a->combat->target = t->record;
        dice(a, 1, 4, 0);
        game.vm.seed = seed_from(1, (const unsigned[]){4, 3, 3});
        CHECK(cok_combat_damage_roll(&game, a, t, 1) && game.effects.rolls.amount16 == 3 &&
              game.effects.rolls.amount == 3);
    }
    /* The word: a roll of 250 with the level 10 is 260 on the word, its low byte 4 the amount. */
    battle_clear();
    a = record('A', 0, 0);
    place(a, 5, 5);
    t = record('T', 1, 0);
    place(t, 6, 5);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 4, 10);
    cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 0, 8);
    CHECK(cok_character_add_effect(a, 0x69, 0, 0, false) != NULL);
    a->combat->target = t->record;
    dice(a, 1, 255, 0);
    game.vm.seed = seed_from(1, (const unsigned[]){255, 250, 250});
    CHECK(cok_combat_damage_roll(&game, a, t, 1) && game.effects.rolls.amount16 == 260 &&
          game.effects.rolls.amount == 4);
    /* A removal (the effect's on_remove set, flag 1) changes nothing: the amounts stay. */
    battle_clear();
    a = record('A', 0, 0);
    place(a, 5, 5);
    t = record('T', 1, 0);
    place(t, 6, 5);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 4, 10);
    cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 0, 8);
    a->combat->target = t->record;
    cok_effect *e = cok_character_add_effect(a, 0x69, 0, 0, true);
    CHECK(e != NULL);
    game.effects.rolls.amount16 = 3;
    game.effects.rolls.amount = 3;
    CHECK(cok_effects_remove(&game.effects, a, e, 0x69) && game.effects.rolls.amount16 == 3 &&
          game.effects.rolls.amount == 3 && game.vm.status == COK_ECL_OK);
    /* A NULL target within a square: the distance is the last listed entry's, within a square
     * here (the target placed beside it), so the target's +0xc7 is read at 0000:00c7: a cited
     * failure. */
    battle_clear();
    a = record('A', 0, 0);
    place(a, 5, 5);
    t = record('T', 1, 0);
    place(t, 6, 5);
    CHECK(cok_character_add_effect(a, 0x69, 0, 0, false) != NULL);
    a->combat->target = NULL;
    dice(a, 1, 4, 0);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 3, 3});
    CHECK(!cok_combat_damage_roll(&game, a, t, 1) && game.vm.status == COK_ECL_EFFECT_FAILED &&
          strstr(game.error, "0000:00c7") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.effects.in_battle = false;
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

/* A target brought down (dqk 52db:02c0's 5837:0d55 after "is killed" and the event 0x0d): the death
 * on the battlefield (cok_arena_kill) at no battle's mode: the sound 6, no body; the strike is over
 * (the attacks used up), and the target is not active. */
static void test_report_kill(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8_at(R->character, a->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    dice(a, 1, 4, 20);
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 4, 1, 4});
    bool over = false;
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
    CHECK(game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_ACTIVE) == 0);
    CHECK(LOGGED("sound: 6;"));
    battle_clear();
}

/* One brought down whose creature flags hold bit 8 (dqk 52db:0529-0532, +0xc8 & 8) only goes
 * down: "goes down" is said, but neither "and is Dying" nor "is killed", and the death is the same
 * (sound 6). */
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
    CHECK(cok_combat_strike(&game, a, t, false, false, &over) && over);
    CHECK(game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_ACTIVE) == 0);
    CHECK(LOGGED("goes down;") && LOGGED("sound: 6;"));
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

/* An item or a readied weapon of type 0x1c (dqk 52db:18d6's 2888 calls, part 17): its pictures are
 * chosen and it flies (51c2:00df, part 17b), a sling stone's two frames at 20 ms, then the
 * strike. */
static int enter_key(void *context)
{
    (void)context;
    return 13; /* the text's page after the attack's report waits for one */
}

static void test_attack_missile(void)
{
    cok_character *a, *t;
    uint8_t item[COK_ITEM_SIZE] = {0};
    game.vm.abort = false; /* an earlier test's stop leaves it set */
    game.keys.read = enter_key;
    for (int weapon = 0; weapon < 2; ++weapon) {
        duel(&a, &t);
        CHECK(cok_character_insert_item(a, 0, item));
        if (weapon) {
            a->items[0][0x28] = 0x1c;
            a->slots[0] = 1;
        }
        bool over = false;
        CHECK(cok_combat_attack(&game, a, t, false, weapon ? 0 : 1, &over));
        CHECK(game.vm.status == COK_ECL_OK && !game.vm.abort);
    }
    game.keys.read = no_key;
    battle_clear();
}

/* The missile's pictures (dqk 52db:2888): an arrow (the rules' list: 4, 5, 7, 0x0c, 0x1e, 0x42,
 * 0x4a) is one picture of slot 0x0f + d % 4 (image d / 4) for an even direction, and of slot 0x10
 * for an odd one (image 1 for 3 and 5, mirrored for 5 and 7; 51c2:0fc8). Its sound 0x0d, one frame,
 * 10 ms. Types 2, 3, 6 spin (51c2:0053: slot 0x12, the four pictures, 50 ms, sound 0x0a); a sling
 * stone (0x1c, 0x1d, 0x43) is slot 0x16's two (10 ms, sound 7); any other (0x36 too: no thrown
 * kind) slot 0x15's two (20 ms, sound 0x0a). No leading sound (the flight is not drawn here). */
static void test_missile_pictures(void)
{
    static const struct { uint8_t d, slot, image; bool mirror; } arrows[] = {
        {0, 0x0f, 0, false}, {1, 0x10, 0, false}, {2, 0x11, 0, false}, {3, 0x10, 1, false},
        {4, 0x0f, 1, false}, {5, 0x10, 1, true},   {6, 0x11, 1, false}, {7, 0x10, 0, true},
    };
    cok_missile_pictures p;
    static const uint8_t arrow_types[] = {4, 0x4a, 0x42, 0x0c, 0x1e, 5, 7};
    for (unsigned t = 0; t < sizeof arrow_types; ++t) {
        for (unsigned k = 0; k < sizeof arrows / sizeof *arrows; ++k) {
            cok_combat_missile_pictures(R, arrows[k].d, arrow_types[t], &p);
            const cok_missile_picture *m = &p.pictures[0];
            CHECK(p.count == 1 && p.flight_frames == 1 && p.delay == 10 && p.sound == 0x0d);
            CHECK(m->slot == arrows[k].slot && m->image == arrows[k].image &&
                  m->mirror == arrows[k].mirror && m->frame == 0);
        }
    }
    static const uint8_t spinning[] = {2, 3, 6};
    static const uint8_t images[4] = {0, 0, 1, 1}, mirrors[4] = {0, 1, 1, 0};
    for (unsigned t = 0; t < sizeof spinning; ++t) {
        cok_combat_missile_pictures(R, 1, spinning[t], &p);
        CHECK(p.count == 4 && p.flight_frames == 4 && p.delay == 0x32 && p.sound == 0x0a);
        for (uint8_t k = 0; k < 4; ++k) {
            const cok_missile_picture *m = &p.pictures[k];
            CHECK(m->slot == 0x12 && m->image == images[k] && m->frame == k &&
                  m->mirror == (mirrors[k] != 0));
        }
    }
    static const uint8_t stones[] = {0x1c, 0x1d, 0x43};
    for (unsigned t = 0; t < sizeof stones; ++t) {
        cok_combat_missile_pictures(R, 1, stones[t], &p);
        CHECK(p.count == 2 && p.flight_frames == 2 && p.delay == 10 && p.sound == 7);
        CHECK(p.pictures[0].slot == 0x16 && p.pictures[0].image == 0 && p.pictures[0].frame == 0);
        CHECK(p.pictures[1].slot == 0x16 && p.pictures[1].image == 1 && p.pictures[1].frame == 1 &&
              !p.pictures[1].mirror);
    }
    /* 0x36 is another's (the rules have no thrown kind): slot 0x15, 20 ms, sound 0x0a. */
    cok_combat_missile_pictures(R, 1, 0x36, &p);
    CHECK(p.count == 2 && p.delay == 0x14 && p.sound == 0x0a && p.pictures[0].slot == 0x15 &&
          p.pictures[1].slot == 0x15 && p.pictures[1].image == 1 && p.pictures[1].frame == 1);
}

/* The readied weapon's type is at the item record's type (+0x28, dqk 177e:0be2): a hoopak (0x43)
 * and a thrown axe (3, flags 0x14) read there, not at Champions' +0x2e; a dart (5, flags 0x1a) is
 * not thrown. */
/* The flight (dqk 51c2:00df, part 17b; cok_arena_missile on Dark Queen's ground). Its waits are
 * the hooks' delay: 2 * delay ms a frame, the target's too when delay > 0, each a RandSeed draw
 * (1df9:052a), and each frame drawn is a 20 ms wait for an arrow (delay 10). */
static unsigned waits;
static unsigned wait_ms[256];

static void wait_spy(cok_adventure *g, unsigned ms, void *context)
{
    (void)g, (void)context;
    if (waits < 256) wait_ms[waits] = ms;
    ++waits;
}

/* The screen (320 by 200, a byte a pixel) saved and put back, and the flight's start from a fresh
 * battle view. */
static uint8_t screen_a[64000], screen_b[64000];

static size_t screen_size(void)
{
    return (size_t)game.screen.units * 8 * game.screen.height;
}

static void flight_start(void)
{
    battle_clear();
    game.combat.view_x = 0;
    game.combat.view_y = 0;
    game.vm.mode = 5;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    waits = 0;
    game.hooks.delay = wait_spy;
}

static void test_flight(void)
{
    CHECK(screen_size() <= sizeof screen_a);
    /* With delay > 0 the flight puts back all it covered: each frame's 24 by 24 under it
     * (19a4:09eb, 0aba), and the target's. The view is drawn before the screen is saved (the
     * flight's centring would draw it, and that is not the flight's change). */
    flight_start();
    CHECK(cok_arena_missile_frame(&game, 0x0f, 0, 0, false));
    CHECK(cok_arena_centre(&game, 3, 3, 0xff, 8));
    memcpy(screen_a, game.screen.pixels, screen_size());
    CHECK(cok_arena_missile(&game, 1, 1, 5, 1, 1, 10) && game.vm.status == COK_ECL_OK);
    CHECK(memcmp(game.screen.pixels, screen_a, screen_size()) == 0);
    /* An arrow (one frame, delay 10) flies 1, 1 to 5, 1 on the view: a 20 ms wait a frame, each
     * step drawn (delay > 0), the target's too; one RandSeed draw for each wait. */
    flight_start();
    CHECK(cok_arena_missile_frame(&game, 0x0f, 0, 0, false));
    uint32_t want = game.vm.seed;
    CHECK(cok_arena_missile(&game, 1, 1, 5, 1, 1, 10) && game.vm.status == COK_ECL_OK);
    /* Twelve sub-cells east (3 to 15): eleven frames drawn along the line, and the target's. */
    CHECK(waits == 12);
    for (unsigned i = 0; i < waits; ++i) {
        CHECK(wait_ms[i] == 20);
        cok_tp_random(&want, 1000);
    }
    CHECK(game.vm.seed == want);
    /* A stone (two frames, delay 0x14: the 2 * delay is 40 ms) flies the same way. */
    flight_start();
    CHECK(cok_arena_missile_frame(&game, 0x16, 0, 0, false) &&
          cok_arena_missile_frame(&game, 0x16, 1, 1, false));
    want = game.vm.seed;
    CHECK(cok_arena_missile(&game, 1, 1, 5, 1, 2, 0x14) && game.vm.status == COK_ECL_OK);
    CHECK(waits == 12);
    for (unsigned i = 0; i < waits; ++i) {
        CHECK(wait_ms[i] == 40);
        cok_tp_random(&want, 1000);
    }
    CHECK(game.vm.seed == want);
    /* With delay 0 each frame waits 0 ms, which draws nothing: the seed stays. */
    flight_start();
    want = game.vm.seed;
    CHECK(cok_arena_missile(&game, 1, 1, 5, 1, 1, 0) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want);
    for (unsigned i = 0; i < waits; ++i) CHECK(wait_ms[i] == 0);
    /* The ends kept on the battlefield (51c2:00df's clamp, x 0-0x31, y 0-0x18): a flight to -5, 30
     * is the one to 0, 24, the same waits, draws and screen. */
    flight_start();
    CHECK(cok_arena_missile_frame(&game, 0x0f, 0, 0, false));
    memcpy(screen_a, game.screen.pixels, screen_size());
    uint32_t seed0 = game.vm.seed;
    CHECK(cok_arena_missile(&game, 1, 1, -5, 30, 1, 10) && game.vm.status == COK_ECL_OK);
    unsigned waits_a = waits;
    uint32_t seed_a = game.vm.seed;
    memcpy(screen_b, game.screen.pixels, screen_size());
    memcpy(game.screen.pixels, screen_a, screen_size());
    flight_start();
    game.vm.seed = seed0;
    CHECK(cok_arena_missile(&game, 1, 1, 0, 24, 1, 10) && game.vm.status == COK_ECL_OK);
    CHECK(waits == waits_a && game.vm.seed == seed_a);
    CHECK(memcmp(game.screen.pixels, screen_b, screen_size()) == 0);
    battle_clear();
    game.hooks.delay = NULL;
}

/* The frame's place (51c2:00a0's 4 * unit + 0x1f44, through 251e:0595): sub-cell 3, 3 is pixel
 * 8 + 8 * 3, 32, both ways, and the 24 by 24 under it is put back (19a4:09eb, 0aba). The arrow has
 * opaque pixels outside the top-left 12 by 12, so a save of 12 by 12 fails here. */
static void test_flight_place(void)
{
    flight_start();
    CHECK(cok_arena_missile_frame(&game, 0x0f, 0, 0, false));
    const uint8_t *record = NULL;
    size_t size = 0;
    CHECK(queen_icon(&game, 0x0f, 0, &record, &size) && record != NULL);
    cok_vga_image arrow = {0};
    CHECK(cok_vga_decode(record, size, NULL, &arrow) == COK_VGA_OK);
    CHECK(arrow.width == 24 && arrow.height == 24);
    uint8_t under[24][24];
    for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 24; ++x) under[y][x] = game.screen.pixels[(32 + y) * 320 + 32 + x];
    CHECK(cok_adventure_drawing(&game)->arena_missile_draw(&game, 0, 3, 3));
    unsigned shown = 0, outside = 0;
    for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 24; ++x) {
            size_t j = (size_t)y * 24 + x;
            if (arrow.mask != NULL && arrow.mask[j] != 0) continue;
            CHECK(game.screen.pixels[(32 + y) * 320 + 32 + x] == arrow.pixels[j]);
            ++shown;
            if (x >= 12 || y >= 12) ++outside;
        }
    CHECK(shown > 0 && outside > 0);
    CHECK(cok_adventure_drawing(&game)->arena_missile_restore(&game, 3, 3));
    for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 24; ++x)
            CHECK(game.screen.pixels[(32 + y) * 320 + 32 + x] == under[y][x]);
    cok_vga_free(&arrow);
    battle_clear();
    game.hooks.delay = NULL;
}

static void test_item_type_at(void)
{
    CHECK(game.item_types.record_type_at == 0x28);
    cok_character *a = record('A', 0, 0);
    uint8_t hoopak[COK_ITEM_SIZE] = {0}, decoy[COK_ITEM_SIZE] = {0};
    hoopak[0x28] = 0x43;
    decoy[0x28] = 5;
    decoy[0x2e] = 0x43;
    CHECK(cok_character_insert_item(a, 0, hoopak) && cok_character_insert_item(a, 1, decoy));
    a->slots[0] = 1;
    CHECK(cok_combat_hoopak(&game.item_types, a));
    a->slots[0] = 2;
    CHECK(!cok_combat_hoopak(&game.item_types, a));
    bool thrown;
    CHECK(cok_combat_thrown(&game.item_types, a, &thrown) && !thrown);
    uint8_t axe[COK_ITEM_SIZE] = {0};
    axe[0x28] = 3;
    CHECK(cok_character_insert_item(a, 2, axe));
    a->slots[0] = 3;
    CHECK(cok_combat_thrown(&game.item_types, a, &thrown) && thrown);
    battle_clear();
}

/* The use-up (dqk 52db:18d6's 1ad2-1c0d) at the record's count (+0x35), readied (+0x32) and effect
 * (+0x37): an arrow's count goes down by the attacks made with slot 1 and it stays; a thrown axe
 * (3) at 0 goes into the pool as the missile, unreadied, and is removed; a spiritual hammer (0x17)
 * is removed but not put in the pool; a hoopak is never used up. */
static void test_use_up(void)
{
    cok_character *a = record('A', 0, 0);
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x28] = 0x1e;
    item[0x35] = 3;
    CHECK(cok_character_insert_item(a, 0, item));
    game.combat.swings[1] = 1;
    CHECK(cok_combat_use_up(&game, a, 1) && a->items[0][0x35] == 2 && a->item_count == 1);
    cok_pool_free_items(&game.pool);
    memset(item, 0, sizeof item);
    item[0x28] = 3;
    item[0x35] = 1;
    item[0x32] = 1;
    /* The name's words 2-3 and the weight (+0x2a-+0x2d) are not a link here: the pool keeps
     * them. */
    item[0x2a] = 0x41;
    item[0x2b] = 0x42;
    item[0x2c] = 0x07;
    item[0x2d] = 0x01;
    CHECK(cok_character_insert_item(a, 1, item));
    a->slots[0] = 2;
    CHECK(cok_combat_use_up(&game, a, 2) && a->item_count == 1 && a->slots[0] == 0);
    CHECK(game.pool.item_count == 1 && game.pool.missile == 1);
    CHECK(game.pool.items[0][0x28] == 3 && game.pool.items[0][0x32] == 0);
    CHECK(game.pool.items[0][0x2a] == 0x41 && game.pool.items[0][0x2b] == 0x42);
    CHECK(game.pool.items[0][0x2c] == 0x07 && game.pool.items[0][0x2d] == 0x01);
    /* The hammer (+0x37 0x17) goes, not into the pool. */
    item[0x37] = 0x17;
    CHECK(cok_character_insert_item(a, 1, item));
    a->slots[0] = 2;
    CHECK(cok_combat_use_up(&game, a, 2) && a->item_count == 1 && game.pool.item_count == 1);
    /* A hoopak is never used up. */
    memset(item, 0, sizeof item);
    item[0x28] = 0x43;
    item[0x35] = 1;
    CHECK(cok_character_insert_item(a, 1, item));
    a->slots[0] = 2;
    CHECK(cok_combat_use_up(&game, a, 2) && a->item_count == 2);
    cok_pool_free_items(&game.pool);
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

/* The death on the battlefield (dqk 5837:0d55, cok_arena_kill under Dark Queen's descriptor): a
 * record on a battle of its own at x, y, mode 5 with the view at the map's corner (its footprint
 * shown whole: no centring) and speed 4 (the pause 400 ms, one draw; each flash 10 ms, one draw). */
static cok_character *slain(char name, int8_t x, int8_t y)
{
    battle_clear();
    cok_combat *k = &game.combat;
    cok_character *c = record(name, 0, 0);
    place(c, x, y);
    cok_field_set8(R->character, c->record, COK_FIELD_ACTIVE, 1);
    k->view_x = 0;
    k->view_y = 0;
    game.vm.mode = 5;
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 4);
    log_text[0] = '\0';
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    return c;
}

/* The flashes: each 10 ms wait of the death samples the screen at two pixels (the skull's point in
 * the footprint's first and second cells). */
static size_t flashes;
static uint8_t flash_at[2][9];
static size_t flash_pixel[2];

static void flash_spy(cok_adventure *g, unsigned ms, void *context)
{
    (void)context;
    if (ms != 10 || flashes >= 9) return;
    for (unsigned i = 0; i < 2; ++i) flash_at[i][flashes] = g->screen.pixels[flash_pixel[i]];
    ++flashes;
}

/* The screen's index of pixel (px, py) of cell (x, y) of the view at the corner. */
static size_t cell_pixel(int x, int y, int px, int py)
{
    return (size_t)(8 + 24 * y + py) * game.screen.units * 8 + (size_t)(8 + 24 * x + px);
}

/* A skull's pixel the two images differ at, both not transparent: its place in the 24 by 24. */
static bool skull_point(const cok_vga_image img[2], int *px, int *py)
{
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) {
            size_t j = (size_t)y * 24 + (size_t)x;
            bool shown0 = img[0].mask == NULL || img[0].mask[j] == 0;
            bool shown1 = img[1].mask == NULL || img[1].mask[j] == 0;
            if (shown0 && shown1 && img[0].pixels[j] != img[1].pixels[j]) {
                *px = x, *py = y;
                return true;
            }
        }
    return false;
}

/* The skull, its images decoded: the death's flashes are records 0 and 1 of icon slot 0x18. */
static void skull_images(cok_vga_image img[2])
{
    for (unsigned i = 0; i < 2; ++i) {
        const uint8_t *record = NULL;
        size_t size = 0;
        CHECK(queen_icon(&game, 0x18, i, &record, &size) && record != NULL);
        CHECK(cok_vga_decode(record, size, NULL, &img[i]) == COK_VGA_OK);
        CHECK(img[i].width == 24 && img[i].height == 24 && img[i].x == 0 && img[i].y == 0);
    }
}

static void test_kill_dqk(void)
{
    cok_vga_image skull[2] = {{0}, {0}};
    skull_images(skull);
    int px = -1, py = -1;
    CHECK(skull_point(skull, &px, &py));
    cok_combat *k = &game.combat;
    uint32_t want;

    /* A party record of footprint 4 (2 by 2) at 2, 3: the skull flashes on each of its shown
     * cells nine times, 10 ms apart, the image k & 1 ready then attacking; then one pause (400 ms)
     * and one draw, nine draws of the flashes and ten in all. The body lies on its top-left cell,
     * the floor, which becomes 0x1f; the spell is kept, the turn's initiative, movement and guard
     * are cleared, and the sound is 6. */
    cok_character *a = slain('A', 2, 3);
    cok_combatant *e = &k->combatant[cok_combat_index(k, a->record)];
    e->size = 4;
    CHECK(cok_combat_occupy(k, R));
    a->combat->initiative = 5;
    a->combat->movement = 6;
    a->combat->spell = 7;
    a->combat->guarding = 1;
    flash_pixel[0] = cell_pixel(2, 3, px, py);
    flash_pixel[1] = cell_pixel(3, 4, px, py);
    flashes = 0;
    want = game.vm.seed;
    for (unsigned i = 0; i < 10; ++i) cok_tp_random(&want, 1000);
    game.hooks.delay = flash_spy;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && game.vm.status == COK_ECL_OK);
    game.hooks.delay = NULL;
    CHECK(flashes == 9);
    for (unsigned f = 0; f < 9; ++f)
        for (unsigned i = 0; i < 2; ++i)
            CHECK(flash_at[i][f] == skull[f & 1].pixels[(size_t)py * 24 + (size_t)px]);
    CHECK(flash_at[0][0] != flash_at[0][1]);
    CHECK(game.vm.seed == want);
    CHECK(LOGGED("sound: 6;") && !LOGGED("sound: 5;"));
    CHECK(k->bodies == 1 && k->body[0].character == a && k->body[0].x == 2 && k->body[0].y == 3);
    CHECK(k->body[0].cell == COK_COMBAT_FLOOR && k->cells[3][2] == COK_COMBAT_BODY);
    CHECK(e->size == 0 && k->occupant[3][2] == 0);
    CHECK(a->combat->initiative == 0 && a->combat->movement == 0 && a->combat->guarding == 0);
    CHECK(a->combat->spell == 7);

    /* Outside combat: the sound 6 and one draw (the pause), nothing else. */
    a = slain('B', 4, 4);
    game.vm.mode = 4;
    want = game.vm.seed;
    cok_tp_random(&want, 1000);
    CHECK(cok_arena_kill(&game, a, 0, NULL) && game.vm.seed == want && k->bodies == 0);
    CHECK(LOGGED("sound: 6;") && k->combatant[1].size == 1);

    /* A record already a body (of the bodies 1-8, the search's entry): nothing, no draw, no sound. */
    a = slain('C', 5, 5);
    k->body[0].character = a;
    want = game.vm.seed;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && game.vm.seed == want && k->bodies == 0);
    CHECK(!LOGGED("sound:"));
    k->body[0].character = NULL;

    /* Status 8 leaves no body: the cell is kept and the record's footprint is gone all the same;
     * the nine flashes and the pause are the ten draws. */
    a = slain('D', 4, 1);
    cok_combat_set_field_word(R, a->record, COK_FIELD_STATUS, 8);
    want = game.vm.seed;
    for (unsigned i = 0; i < 10; ++i) cok_tp_random(&want, 1000);
    CHECK(cok_arena_kill(&game, a, 0, NULL) && k->bodies == 0);
    CHECK(game.vm.seed == want);
    CHECK(k->cells[1][4] == COK_COMBAT_FLOOR && k->combatant[1].size == 0);

    /* Status 7 lays a stone: 0x1e, and the body's cell is the floor. */
    a = slain('E', 5, 2);
    cok_combat_set_field_word(R, a->record, COK_FIELD_STATUS, 7);
    CHECK(cok_arena_kill(&game, a, 0, NULL) && k->bodies == 1);
    CHECK(k->cells[2][5] == 0x1e && k->body[0].cell == COK_COMBAT_FLOOR);

    /* The cells 0x1d, 0x1c and 0x46 are kept under a body; 0x1b is not (and its cell is the body's
     * cell). */
    a = slain('F', 1, 5);
    k->cells[5][1] = 0x46;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && k->bodies == 1);
    CHECK(k->cells[5][1] == 0x46 && k->body[0].cell == 0x46);
    a = slain('G', 3, 4);
    k->cells[4][3] = 0x1b;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && k->bodies == 1);
    CHECK(k->cells[4][3] == COK_COMBAT_BODY && k->body[0].cell == 0x1b);
    a = slain('H', 4, 4);
    k->cells[4][4] = 0x1d;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && k->bodies == 1 && k->cells[4][4] == 0x1d);
    a = slain('K', 6, 6);
    k->cells[6][6] = 0x1c;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && k->bodies == 1 && k->cells[6][6] == 0x1c);
    CHECK(k->body[0].cell == 0x1c);

    /* A cell under a body is the body's cell: 0x1f looks through to the last body there (137a). */
    a = slain('I', 0, 0);
    k->cells[0][0] = 0x1f;
    k->bodies = 1;
    k->body[0].character = NULL;
    k->body[0].x = 0;
    k->body[0].y = 0;
    k->body[0].cell = 0x1b;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && k->bodies == 2 && k->body[1].cell == 0x1b);

    /* A party member not in the party (+0x14, not_party) leaves no body; its turn still ends. */
    a = slain('J', 2, 2);
    a->combat->not_party = 1;
    want = game.vm.seed;
    for (unsigned i = 0; i < 10; ++i) cok_tp_random(&want, 1000);
    CHECK(cok_arena_kill(&game, a, 0, NULL) && game.vm.seed == want && k->bodies == 0);
    CHECK(k->cells[2][2] == COK_COMBAT_FLOOR);

    cok_vga_free(&skull[0]);
    cok_vga_free(&skull[1]);
    game.vm.mode = 0;
    battle_clear();
}

/* The death off the view (dqk 5837:0d55): a record whose footprint is not shown whole has the view centred on it
 * first (5837:0846(x, y, 3, 8), the margin 3), then the skull flashes; after the kill its body (the top-left
 * cell, 0x1f) is on the view, and the view has moved from the corner it was. The record is at 30, 20, a combatant
 * of size 1, with the view at 0, 0 (slain's), so the centring is what brings it on the view. */
static void test_kill_off_view(void)
{
    cok_character *a = slain('A', 30, 20);
    cok_combat *k = &game.combat;
    cok_combatant *e = &k->combatant[cok_combat_index(k, a->record)];
    CHECK(e->size == 1 && k->view_x == 0 && k->view_y == 0);
    a->combat->initiative = 5;
    a->combat->movement = 6;
    a->combat->guarding = 1;
    /* The skull flashes on the centred view (corner 27, 17: 30, 20 at its cell 3, 3), so the centring comes
     * before the flashes; the closing centring (view + 3) keeps that corner. */
    cok_vga_image skull[2] = {{0}, {0}};
    skull_images(skull);
    int px = -1, py = -1;
    CHECK(skull_point(skull, &px, &py));
    flash_pixel[0] = flash_pixel[1] = cell_pixel(3, 3, px, py);
    flashes = 0;
    game.hooks.delay = flash_spy;
    CHECK(cok_arena_kill(&game, a, 0, NULL) && game.vm.status == COK_ECL_OK);
    game.hooks.delay = NULL;
    CHECK(flashes == 9 && k->view_x == 27 && k->view_y == 17);
    for (unsigned f = 0; f < 9; ++f)
        CHECK(flash_at[0][f] == skull[f & 1].pixels[(size_t)py * 24 + (size_t)px]);
    cok_vga_free(&skull[0]);
    cok_vga_free(&skull[1]);
    CHECK(k->bodies == 1 && k->body[0].character == a && k->body[0].x == 30 && k->body[0].y == 20);
    CHECK(k->cells[20][30] == COK_COMBAT_BODY && e->size == 0 && k->occupant[20][30] == 0);
    CHECK(a->combat->initiative == 0 && a->combat->movement == 0 && a->combat->guarding == 0);
    game.vm.mode = 0;
    battle_clear();
}

/* 137a (cok_cloud_under) under Dark Queen's list (fight.under_tiles: 0x1d, 0x1c, 0x1b, 0x46): a
 * cloud node's cell k=1 holds the terrain it kept, given for those values only. */
static void test_under_cloud(void)
{
    battle_clear();
    static cok_combat_cloud node;
    memset(&node, 0, sizeof node);
    node.x = 10;
    node.y = 10;
    node.covered[1] = 1;
    node.saved[1] = 0x2a;
    int8_t x1 = (int8_t)(10 + cok_step_x[cok_data_u8_at(R->data, COK_DATA_CLOUD_CELLS, 1)]);
    int8_t y1 = (int8_t)(10 + cok_step_y[cok_data_u8_at(R->data, COK_DATA_CLOUD_CELLS, 1)]);
    game.combat.clouds = &node;
    CHECK(cok_cloud_under(&game, x1, y1, 0x1d) == 0x2a);
    CHECK(cok_cloud_under(&game, x1, y1, 0x1b) == 0x2a);
    CHECK(cok_cloud_under(&game, x1, y1, 0x1e) == 0x1e); /* not one of 137a's */
    CHECK(cok_cloud_under(&game, (int8_t)(x1 + 1), y1, 0x1d) == 0x1d || node.covered[2]);
    CHECK(cok_cloud_under(&game, 0, 0, 0x1d) == 0x1d);
    game.combat.clouds = NULL;
    battle_clear();
}

/* Battle-only effects (148f:1b66 = cok_combat_battle_only): the list's effect goes, and berserk
 * with the NPC's byte 0xb3 turns the record back to the party (hostile word 0); not without. Leaving
 * (148f:1a69 = cok_combat_leave): inactive, its status, hit points 0 (unless it runs, 3), it leaves
 * the map and loses the battle-only effect. Placing (5837:10bd = cok_combat_place): a free cell
 * takes it, an occupied one does not. */
static void test_battle_only_leave_place(void)
{
    const cok_character_layout *L = R->character;
    /* The list (DS:00a2, 16 ids) holds berserk (0x4d) as its 11th id (DS:00ac): 148f:1b66's loop
     * (148f:017d removes the first node of each id) takes one berserk before 177e:1734 looks for
     * one, so a turned record's single berserk is gone when the test runs, and the record stays
     * hostile; with a second berserk left, it goes back (+0x66 0). The removal's other id is the
     * list's second entry (0x03). */
    uint8_t only = cok_data_u8_at(R->data, COK_DATA_BATTLE_ONLY, 1);
    for (int berserk = 0; berserk < 3; ++berserk) {
        battle_clear();
        cok_character *c = record('T', 1, 0);
        CHECK(cok_character_add_effect(c, only, 0, 0, false) != NULL);
        for (int n = 0; n < berserk; ++n)
            CHECK(cok_character_add_effect(c, 0x4d, 0, 0, false) != NULL);
        cok_field_set8(L, c->record, COK_FIELD_NPC, berserk != 0 ? 0xb3 : 0);
        CHECK(cok_combat_battle_only(&game, c) && game.vm.status == COK_ECL_OK);
        CHECK(cok_character_find_effect(c, only) == NULL);
        CHECK((cok_character_find_effect(c, 0x4d) != NULL) == (berserk == 2));
        CHECK(cok_combat_field_word(R, c->record, COK_FIELD_HOSTILE) == (berserk == 2 ? 0u : 1u));
    }
    battle_clear();
    /* Leaving: a record on the map, its combatant off. */
    cok_character *c = slain('L', 3, 3);
    CHECK(cok_character_add_effect(c, cok_data_u8_at(R->data, COK_DATA_BATTLE_ONLY, 0), 0, 0,
                                   false) != NULL);
    CHECK(cok_combat_leave(&game, c, 4, "Surrenders") && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(L, c->record, COK_FIELD_ACTIVE) == 0 &&
          cok_combat_field_word(R, c->record, COK_FIELD_STATUS) == 4);
    CHECK(cok_combat_field_word(R, c->record, COK_FIELD_HIT_POINTS) == 0);
    CHECK(game.combat.combatant[1].size == 0 && game.combat.occupant[3][3] == 0);
    CHECK(cok_character_find_effect(c, cok_data_u8_at(R->data, COK_DATA_BATTLE_ONLY, 0)) == NULL);
    game.vm.mode = 0;
    battle_clear();
    /* Placing: a combatant of size 1 is put at (9, 9), which is free; another placed there after it
     * is not (its occupant), and its size is 0. */
    battle_clear();
    c = record('P', 0, 0);
    cok_field_set8(L, c->record, COK_FIELD_SIZE, 1);
    place(c, 1, 1);
    bool placed = false;
    CHECK(cok_combat_place(&game.combat, 5, c, R, 9, 9, false, &placed) && placed);
    CHECK(game.combat.occupant[9][9] == cok_combat_index(&game.combat, c->record));
    cok_character *d = record('Q', 0, 0);
    cok_field_set8(L, d->record, COK_FIELD_SIZE, 1);
    place(d, 2, 2);
    CHECK(cok_combat_place(&game.combat, 5, d, R, 9, 9, false, &placed) && !placed);
    CHECK(game.combat.combatant[cok_combat_index(&game.combat, d->record)].size == 0);
    game.vm.mode = 0;
    battle_clear();
}


/* The spell engine (queen_cast.c, dqk 177e:1900, 849a:0f34, 101b, 10f9, 148f:2214, 7fd8:00f8, 050a):
 * the spell records are the table's (data_dark_queen.c's), so the ids below are the table's kinds:
 * MM (0x0f) kind 2, CLW (0x03) kind 0, Curse (0x02) kind 0xff, a kind 1, a kind 3 and a kind 4 spell
 * by search. The caster is a record with the class levels set, selected (vm.character). */
static uint8_t spell_of_kind(unsigned kind)
{
    for (unsigned id = 1; id < 0x8d; ++id)
        if (queen_spell_word(&game, id, 0) == kind) return (uint8_t)id;
    return 0;
}

static void set_class(cok_character *c, unsigned k, uint8_t level)
{
    cok_field_set8_at(R->character, c->record, COK_FIELD_CLASS_LEVEL, k, level);
}

/* The caster level (dqk 177e:1900): the non-caster's 6, the kinds' (cleric and mage classes, the moon
 * of the order, the fixed 12), the item's 6, the cap at 0x1f and the undefined kind above 4. */
static void test_caster_level(void)
{
    cok_character *a = record('A', 0, 0);
    uint8_t lv = 0;
    uint8_t mm = 0x0f, clw = 0x03, k1 = spell_of_kind(1), k3 = spell_of_kind(3), k4 = spell_of_kind(4);
    CHECK(k1 != 0 && k3 != 0 && k4 != 0);
    set_class(a, 0, 0);
    set_class(a, 1, 0);
    set_class(a, 4, 0);
    set_class(a, 5, 0);
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 6); /* a non-caster */
    set_class(a, 0, 3);
    set_class(a, 1, 7);
    CHECK(queen_caster_level(&game, a->record, clw, false, &lv) && lv == 7); /* cleric 3, class 1 7 */
    set_class(a, 0, 0);
    set_class(a, 1, 9);
    set_class(a, 4, 6);
    CHECK(queen_caster_level(&game, a->record, k1, false, &lv) && lv == 0); /* class 4 6: -1, none */
    set_class(a, 4, 10);
    CHECK(queen_caster_level(&game, a->record, k1, false, &lv) && lv == 3); /* class 4 10 */
    set_class(a, 4, 0);
    set_class(a, 5, 9);
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 0);
    a->record[0x19d] = 1; /* magic order 1 */
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 8); /* moon 0: class 5 less 1 */
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 2);
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 10); /* moon 2: class 5 plus 1 */
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 1);
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 9); /* moon 1: no change */
    set_class(a, 5, 5);
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 2);
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 5); /* moon 2, class 5 of 5 */
    set_class(a, 5, 9);
    set_class(a, 4, 18);
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 0);
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 10); /* class 4 18: 10 over 8 */
    a->record[0x19d] = 0;
    set_class(a, 5, 0);
    set_class(a, 4, 10);
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 2); /* no order: class 4 less 8 */
    CHECK(queen_caster_level(&game, a->record, k4, false, &lv) && lv == 12);
    game.effects.rolls.item = 1;
    CHECK(queen_caster_level(&game, a->record, k4, false, &lv) && lv == 12); /* an item, kind 4 stays */
    CHECK(queen_caster_level(&game, a->record, mm, false, &lv) && lv == 6);  /* an item: 6 */
    game.effects.rolls.item = 0;
    /* The cap: class 0 of 0x25 (37) is 0x1f capped, 37 uncapped. */
    set_class(a, 0, 0x25);
    set_class(a, 1, 0);
    CHECK(queen_caster_level(&game, a->record, clw, true, &lv) && lv == 0x1f);
    CHECK(queen_caster_level(&game, a->record, clw, false, &lv) && lv == 0x25);
    /* The cap compares signed (177e:1900's jle): levels of 0x90 and 0x80 are -112 and -128, the
     * larger 0x90, and it is not capped. */
    set_class(a, 0, 0x90);
    set_class(a, 1, 0x80);
    CHECK(queen_caster_level(&game, a->record, clw, true, &lv) && lv == 0x90);
    /* The larger of classes 0 and 1 is signed (177e:1900's jl): 5 over 0x90 (-112). */
    set_class(a, 0, 5);
    set_class(a, 1, 0x90);
    CHECK(queen_caster_level(&game, a->record, clw, false, &lv) && lv == 5);
    set_class(a, 0, 0x25);
    set_class(a, 1, 0);
    /* A kind above 4 (Curse's 0xff): undefined for a caster, 6 with an item. */
    set_class(a, 0, 0);
    set_class(a, 1, 9);
    set_class(a, 4, 10);
    CHECK(!queen_caster_level(&game, a->record, 0x02, false, &lv) &&
          game.vm.status == COK_ECL_UNDEFINED);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.effects.rolls.item = 1;
    CHECK(queen_caster_level(&game, a->record, 0x02, false, &lv) && lv == 6);
    game.effects.rolls.item = 0;
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 0);
}

/* The range (dqk 849a:0f34): MM at level 2 is 14 (the base 6 and 4 a level), CLW 1 (its base 0 and a
 * target), an item's 4 * 6 + 6 (30), the quarter of spell 0x7d, and the caster-level-free kind 4. */
static void test_spell_range(void)
{
    cok_character *a = record('A', 0, 0);
    uint8_t range = 0;
    set_class(a, 4, 10); /* kind 2 at level 2 (class 4 less 8) */
    game.vm.character = a->record;
    CHECK(queen_spell_range(&game, 0x0f, &range) && range == 14);
    CHECK(queen_spell_range(&game, 0x03, &range) && range == 1);
    game.effects.rolls.item = 1;
    CHECK(queen_spell_range(&game, 0x0f, &range) && range == 30);
    game.effects.rolls.item = 0;
    CHECK(queen_spell_range(&game, 0x7d, &range) && range == 1); /* 2 / 4, with a target */
    game.vm.character = NULL;
    CHECK(!queen_spell_range(&game, 0x0f, &range) && game.vm.status == COK_ECL_UNDEFINED);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.vm.character = NULL;
    battle_clear();
}

/* The duration (dqk 849a:101b): the fixed ids, the dice (1d4 + 1, 5d4, 1d4 * 10 + 40) and the caster
 * level's default (level times the table's per-level byte, plus its base). */
static void test_spell_duration(void)
{
    cok_character *a = record('A', 0, 0);
    set_class(a, 4, 10);
    game.vm.character = a->record;
    uint16_t minutes = 0;
    game.vm.seed = 7;
    CHECK(queen_spell_duration(&game, 0x1a, &minutes) && minutes == 0x0ec4);
    CHECK(queen_spell_duration(&game, 0x7a, &minutes) && minutes == 0x5a0);
    uint32_t want = 7;
    CHECK(queen_spell_duration(&game, 0x79, &minutes) && minutes == cok_dice(&want, 1, 4) + 1);
    CHECK(game.vm.seed == want);
    game.vm.seed = 7;
    want = 7;
    CHECK(queen_spell_duration(&game, 0x7f, &minutes) && minutes == cok_dice(&want, 5, 4));
    CHECK(game.vm.seed == want);
    game.vm.seed = 7;
    want = 7;
    CHECK(queen_spell_duration(&game, 0x80, &minutes) && minutes == cok_dice(&want, 1, 4) * 10 + 40);
    /* Bless (0x01): the level 2 (kind 0 with no class 0 or 1 level: class 1 at 0, class 4 10, so the
     * class 4 level's 2 is not the cleric's; here the kind 0 level is max(0, 0) = 0). */
    set_class(a, 4, 0);
    set_class(a, 1, 0);
    CHECK(queen_spell_duration(&game, 0x01, &minutes) &&
          minutes == queen_spell_byte(&game, 0x01, 6) * 0 + queen_spell_byte(&game, 0x01, 5));
    game.vm.character = NULL;
    battle_clear();
}

/* Magic Missile (dqk 7fd8:050a, 849a:10f9) on a hostile in the battle: the level 2 caster's (class 4
 * 10) lv 3 gives one die, 1d4 + 1. The target loses it, says "takes N points of damage from Magic"
 * in the flash (one draw for the die, then the flash's four frames: five draws), and may no longer
 * cast. */
static void test_magic_missile(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    t->combat->may_cast = 1;
    set_class(a, 4, 10);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = 0x0f;
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 0x4e);
    cok_field_set8(R->character, t->record, COK_FIELD_MAX_HIT_POINTS, 0x4e);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 3, 3}); /* the die is 3: 4 points */
    uint32_t want = game.vm.seed;
    cok_dice(&want, 1, 4);
    for (unsigned k = 0; k < 4; ++k) cok_tp_random(&want, 1000);
    CHECK(queen_magic_missile(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 0x4e - 4);
    CHECK(LOGGED("takes 4 points of damage from Magic") && t->combat->may_cast == 0);
    CHECK(game.effects.rolls.damage_type == 0);
    battle_clear();
    game.spell_target_count = 0;
}

/* The caster's dice and the target's hit points in CLW (dqk 7fd8:00f8): a deity 4 caster rolls its
 * bonus d8 first, then the heal's d8; another rolls the heal's alone; none with no target. The heal
 * is "is partially healed"; the guard (effect 0xd7) keeps it. */
static void test_cure_light(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = 0x03;
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    cok_field_set8(R->character, t->record, COK_FIELD_MAX_HIT_POINTS, 40);
    a->record[0x19b] = 4;
    game.vm.seed = 11;
    uint32_t want = 11;
    uint8_t bonus = cok_dice(&want, 1, 8);
    uint8_t amount = (uint8_t)(cok_dice(&want, 1, 8) + bonus);
    cok_tp_random(&want, 1000); /* the say's speed wait (speed 0: 10 ms, one draw) */
    CHECK(queen_cure_light(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 1 + amount);
    CHECK(LOGGED("is partially healed"));
    a->record[0x19b] = 0;
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    game.vm.seed = 11;
    want = 11;
    amount = cok_dice(&want, 1, 8);
    cok_tp_random(&want, 1000);
    CHECK(queen_cure_light(&game) && game.vm.seed == want);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 1 + amount);
    /* Fully: the most is 5, the heal takes it there. */
    cok_field_set8(R->character, t->record, COK_FIELD_MAX_HIT_POINTS, 5);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    log_text[0] = '\0';
    CHECK(queen_cure_light(&game) && LOGGED("is fully healed"));
    /* The guard: effect 0xd7 holds the heal off. */
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    CHECK(cok_character_add_effect(t, 0xd7, 0, 0, false) != NULL);
    log_text[0] = '\0';
    CHECK(queen_cure_light(&game) && cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 1);
    CHECK(!LOGGED("healed"));
    /* No target: no roll at all. */
    game.spell_target_count = 0;
    want = game.vm.seed;
    CHECK(queen_cure_light(&game) && game.vm.seed == want);
    game.vm.character = NULL;
    battle_clear();
}

/* The saving throw (dqk 148f:1886, queen_save) against a type 4 saving throw of 10 (the record's
 * byte at +0x8f). A holder of 0x76 rolls 1 and fails with one draw and no event; a natural 1 fails
 * and a 20 saves, both with no event 0x0c (save_type keeps its sentinel). Otherwise the roll is d20
 * plus the save bonus (+0xcb, signed) plus the mage's moon (class 5 above 0 and order above 0, both
 * unsigned: order 1-3 reads COK_VAR_MOONS, 4-6 COK_VAR_MOON_DAYS; the byte 0 takes one off, 2 adds
 * one); the event 0x0c runs with save_type set. An order past the moons stops, cited. */
static void test_queen_save(void)
{
    const uint8_t type = 4;
    const cok_character_layout *L = R->character;
    cok_character *a, *t;
    bool made;
    game.vm.mode = 5; /* the battle */
    duel(&a, &t);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 10);
    CHECK(cok_character_add_effect(t, 0x76, 0, 0, false) != NULL);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 20, 20});
    uint32_t want = after_draws(game.vm.seed, 1, 20);
    game.effects.rolls.save_type = 0x55;
    made = true;
    CHECK(queen_save(&game, t, type, 0, &made) && !made && game.vm.seed == want);
    CHECK(game.effects.rolls.save_roll == 1 && game.effects.rolls.save_type == 0x55);
    CHECK(game.vm.status == COK_ECL_OK);
    /* Without the holder: a natural 20 saves and a natural 1 fails, both with no event. */
    duel(&a, &t);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 10);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 20, 20});
    game.effects.rolls.save_type = 0x55;
    made = false;
    CHECK(queen_save(&game, t, type, 0, &made) && made && game.effects.rolls.save_type == 0x55);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
    made = true;
    CHECK(queen_save(&game, t, type, 0, &made) && !made && game.effects.rolls.save_type == 0x55);
    /* A roll of 10 against 10 saves; the mage of order 1 with moon 0 rolls 9 (fails), moon 2 rolls
     * 11, and moon 1 or 3 leave the 10. Threshold 11 then saves only with moon 2. */
    for (unsigned threshold = 10; threshold <= 11; ++threshold)
        for (uint8_t phase = 0; phase <= 3; ++phase) {
            duel(&a, &t);
            set_class(t, 5, 3);
            cok_field_set8(L, t->record, COK_FIELD_MAGIC_ORDER, 1);
            cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, phase);
            cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, (uint8_t)threshold);
            game.vm.seed = seed_from(1, (const unsigned[]){20, 10, 10});
            made = false;
            CHECK(queen_save(&game, t, type, 0, &made) && game.vm.status == COK_ECL_OK);
            int roll = 10 + (phase == 0 ? -1 : phase == 2 ? 1 : 0);
            CHECK(made == (roll >= (int)threshold));
            CHECK(game.effects.rolls.save_roll == roll && game.effects.rolls.save_type == type);
        }
    /* Order 4 reads COK_VAR_MOON_DAYS[0] (0 takes one off), not the phases. */
    duel(&a, &t);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 10);
    set_class(t, 5, 3);
    cok_field_set8(L, t->record, COK_FIELD_MAGIC_ORDER, 4);
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOON_DAYS, 0, 0);
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 2);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 10, 10});
    made = true;
    CHECK(queen_save(&game, t, type, 0, &made) && !made && game.effects.rolls.save_roll == 9);
    /* Class 5 0x80 is above 0 (unsigned): the same moon 0 takes one off. */
    duel(&a, &t);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 10);
    set_class(t, 5, 0x80);
    cok_field_set8(L, t->record, COK_FIELD_MAGIC_ORDER, 1);
    cok_ecl_set_var_at(&game.vm, COK_VAR_MOONS, 0, 0);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 10, 10});
    made = true;
    CHECK(queen_save(&game, t, type, 0, &made) && !made && game.effects.rolls.save_roll == 9);
    /* The save bonus is signed: -2 (0xfe) takes 2 off the 10. */
    duel(&a, &t);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 9);
    cok_field_set8(L, t->record, COK_FIELD_SAVE_BONUS, 0xfe);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 10, 10});
    made = true;
    CHECK(queen_save(&game, t, type, 0, &made) && !made && game.effects.rolls.save_roll == 8);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 8);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 10, 10});
    made = false;
    CHECK(queen_save(&game, t, type, 0, &made) && made);
    /* The roll wraps as a byte and is compared unsigned (1886's jbe): -16 (0xf0) on a 10 is 0xfa,
     * at or above a saving throw of 10. */
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 10);
    cok_field_set8(L, t->record, COK_FIELD_SAVE_BONUS, 0xf0);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 10, 10});
    made = false;
    CHECK(queen_save(&game, t, type, 0, &made) && made && game.effects.rolls.save_roll == 0xfa);
    /* Order 7 is past the moons' days: stopped, cited. */
    duel(&a, &t);
    set_class(t, 5, 3);
    cok_field_set8(L, t->record, COK_FIELD_MAGIC_ORDER, 7);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 10, 10});
    made = true;
    CHECK(!queen_save(&game, t, type, 0, &made) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "148f:1886") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    battle_clear();
}

/* The apply's saving throw (dqk 849a:10f9) with Ray of Enfeeblement (0x21: save kind 1, type 4, effect
 * 0x1d "is weakened"): a natural 20 saves it (no effect, "is Unaffected"), a natural 1 fails it (the
 * effect is added with its text); status 10 saves it with no roll, so a roll of 1 does not fail it. */
static void test_spell_apply(void)
{
    const uint8_t ray = 0x21, type = 4;
    cok_character *a, *t;
    CHECK(queen_spell_word(&game, ray, 0x0a) == 1 && queen_spell_byte(&game, ray, 0x0c) == type &&
          queen_spell_byte(&game, ray, 0x0e) == 0x1d);
    game.vm.mode = 5; /* the battle */
    /* No target (count 0): the damage type it was given is kept (DS:bebd is set before 10f9's
     * count test). */
    game.spell_target_count = 0;
    game.effects.rolls.damage_type = 8;
    CHECK(queen_spell_apply(&game, 0x0f, 0, false, 3, 2, "") && game.effects.rolls.damage_type == 2);
    /* Saved by a natural 20. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = ray;
    cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, type, 10);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 20, 20});
    CHECK(queen_spell_apply(&game, ray, 0, false, 0, 0, "is weakened") && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x1d) == NULL && LOGGED("is Unaffected"));
    /* A natural 1 fails it: the effect is added and said. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = ray;
    cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, type, 10);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
    CHECK(queen_spell_apply(&game, ray, 0, false, 0, 0, "is weakened") && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x1d) != NULL && LOGGED("is weakened"));
    /* Status 10 saves it without a roll: the natural 1 that would fail it is not drawn. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = ray;
    cok_field_set8(R->character, t->record, COK_FIELD_STATUS, 10);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 1, 1});
    CHECK(queen_spell_apply(&game, ray, 0, false, 0, 0, "is weakened") && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x1d) == NULL && LOGGED("is Unaffected"));
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* The touch (dqk 849a:10f9's +3 0xff): each target's hit roll (148f:1771, the caster against its
 * armour class, 0 here: a natural 20 hits, a natural 1 misses). A hit applies the damage; a miss zeroes
 * the damage for this target and every later one, and saves this target. Invisibility to Animals (0x50,
 * save kind 0, effect 0x45) takes its effect either way; Cause Blindness (0x26, save kind 1, type 4,
 * effect 0x21) is refused once a miss has saved its target. */
static void test_spell_touch(void)
{
    const uint8_t invis = 0x50, blind = 0x26;
    cok_character *a, *t, *t2;
    game.vm.mode = 5; /* the battle */
    CHECK(queen_spell_byte(&game, invis, 0x03) == 0xff && queen_spell_word(&game, invis, 0x0a) == 0 &&
          queen_spell_byte(&game, invis, 0x0e) == 0x45);
    CHECK(queen_spell_byte(&game, blind, 0x03) == 0xff && queen_spell_word(&game, blind, 0x0a) == 1 &&
          queen_spell_byte(&game, blind, 0x0e) == 0x21);
    /* A hit: 3 points of damage, and the effect. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = invis;
    game.vm.seed = seed_from(1, (const unsigned[]){20, 20, 20});
    CHECK(queen_spell_apply(&game, invis, 0, false, 3, 8, "") && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 7);
    CHECK(LOGGED("takes 3 points of damage from Magic") && cok_character_find_effect(t, 0x45) != NULL);
    /* A miss on the first target and a hit on the second (armour class 0): no damage to either, since
     * the miss zeroed the amount; both take the kind-0 effect. */
    duel(&a, &t);
    game.vm.character = a->record;
    t2 = record('N', 1, 0);
    place(t2, 7, 5);
    cok_field_set8(R->character, t2->record, COK_FIELD_ACTIVE, 1);
    game.spell_targets[1] = t;
    game.spell_targets[2] = t2;
    game.spell_target_count = 2;
    game.effects.rolls.spell = invis;
    game.vm.seed = seed_from(2, (const unsigned[]){20, 1, 1, 20, 20, 20});
    CHECK(queen_spell_apply(&game, invis, 0, false, 3, 8, "") && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 10);
    CHECK(cok_field_u8(R->character, t2->record, COK_FIELD_HIT_POINTS) == 10);
    CHECK(!LOGGED("takes 3") && cok_character_find_effect(t, 0x45) != NULL &&
          cok_character_find_effect(t2, 0x45) != NULL);
    /* A miss saves its target: Cause Blindness's kind-1 effect is refused, with the save roll (1) first
     * and the touch's miss (1) after it. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = blind;
    cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, 4, 10);
    game.vm.seed = seed_from(2, (const unsigned[]){20, 1, 1, 20, 1, 1});
    CHECK(queen_spell_apply(&game, blind, 0, false, 0, 0, "is blind") && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x21) == NULL && LOGGED("is Unaffected"));
    /* A hit with the save failed: the kind-1 effect is added. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = blind;
    cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, 4, 10);
    game.vm.seed = seed_from(2, (const unsigned[]){20, 1, 1, 20, 20, 20});
    CHECK(queen_spell_apply(&game, blind, 0, false, 0, 0, "is blind") && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x21) != NULL && LOGGED("is blind"));
    /* The touch's event 0x0b runs on the target before the hit roll: a holder of 0x04 (its handler a
     * cited stop, 78e4:0168) stops there, with no draw. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = invis;
    CHECK(cok_character_add_effect(t, 0x04, 0, 0, false) != NULL);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 20, 20});
    uint32_t before = game.vm.seed;
    /* The stop is the effect's own (COK_ECL_EFFECT_FAILED, with the handler's text). */
    CHECK(!queen_spell_apply(&game, invis, 0, false, 3, 8, "") &&
          game.vm.status == COK_ECL_EFFECT_FAILED && strstr(game.error, "78e4:0168") != NULL &&
          game.vm.seed == before);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* The plain applies (queen_cast.c's table, 7fd8's routines): queen_spell_handler takes the plain ids
 * (their texts are checked against the routines in test_dark_queen.c) and CLW and MM, and no other id.
 * Shield (0x13: save kind 0, the caster alone) adds its effect to the caster with "is shielded" said. */
static void test_plain_apply(void)
{
    game.vm.mode = 5; /* the battle */
    uint16_t ds = 0;
    static const unsigned band_a2[] = {0x04, 0x09, 0x14, 0x25, 0x3a, 0x40, 0x42, 0x47, 0x48, 0x62};
    for (unsigned id = 0; id < 0x100; ++id) {
        bool plain = queen_plain_text((uint8_t)id, &ds) != NULL;
        bool a2 = false;
        for (size_t i = 0; i < sizeof band_a2 / sizeof *band_a2; ++i) a2 = a2 || band_a2[i] == id;
        bool a3 = id == 0x01 || id == 0x02 || id == 0x20 || id == 0x2a || id == 0x3e || id == 0x49 ||
                  id == 0x76;
        CHECK((queen_spell_handler((uint8_t)id) != NULL) ==
              (plain || a2 || id == 0x03 || id == 0x0f || a3 || id == 0x5c)); /* 0x5c: band b4 */
    }
    CHECK(queen_spell_handler(0x1c) == NULL); /* Spiritual Hammer: 7fd8:08e3, unported */
    cok_character *a, *t;
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = a;
    game.spell_target_count = 1;
    game.effects.rolls.spell = 0x13;
    uint8_t effect = queen_spell_byte(&game, 0x13, 0x0e);
    CHECK(effect != 0 && queen_spell_word(&game, 0x13, 0x0a) == 0);
    CHECK(queen_spell_handler(0x13) != NULL && queen_spell_handler(0x13)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(a, effect) != NULL && LOGGED("is shielded"));
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* The slot a test's handler of event 0x0b puts in place of the target (none: kept), and the count of
 * targets it leaves (0: kept). */
static cok_character *slot_to;
static uint8_t slot_count;

static bool swap_slot(cok_effects *fx, void *context)
{
    cok_adventure *g = context;
    if (slot_to != NULL) *fx->target = slot_to;
    if (slot_count != 0) g->spell_target_count = slot_count;
    return true;
}

/* The touch reads the slot again after its event 0x0b (dqk 849a:10f9 1247-133e) and the count each
 * pass (1347). No ported Dark Queen handler swaps a slot (0x47's turning is a cited stop), so a copy of
 * the rules puts effect 0x22 (in no list) on event 0x0b alone with Champions' 0x4a handler (3f44:26a5:
 * a d20 of 1 calls fx->dispel), whose hook here swaps the slot or cuts the count. The swap: the hit
 * roll (against its armour class, not recomputed), the damage and the effect go to the record put in
 * the slot. The cut to 1 on the first target's
 * event leaves the second untouched. */
static void test_spell_touch_slot(void)
{
    const uint8_t invis = 0x50, turn = 0x22;
    const cok_rules_effects *E = &R->effects;
    static cok_rules rules;
    static cok_rules_effect handlers[0x100];
    static const uint8_t *events[0x20];
    static const uint8_t on_touch[] = {turn, 0};
    CHECK(turn < E->count && E->count <= 0x100 && 0x0b < E->event_count && E->event_count <= 0x20);
    rules = *R;
    memcpy(handlers, E->handlers, E->count * sizeof *handlers);
    handlers[turn] = (cok_rules_effect){.handler = COK_FX_DISPELLING};
    for (unsigned e = 0; e < E->event_count; ++e) events[e] = E->events[e];
    events[0x0b] = on_touch;
    rules.effects.handlers = handlers;
    rules.effects.events = events;
    bool (*dispel)(cok_effects *, void *) = game.effects.dispel;
    game.rules = game.effects.rules = &rules;
    game.effects.dispel = swap_slot;
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t, *t2;
    for (int cut = 0; cut < 2; ++cut) {
        duel(&a, &t);
        t2 = record('N', 1, 0);
        place(t2, 7, 5);
        cok_field_set8(R->character, t2->record, COK_FIELD_ACTIVE, 1);
        CHECK(cok_character_add_effect(t, turn, 0, 0, false) != NULL);
        game.vm.character = a->record;
        game.spell_targets[1] = t;
        game.spell_targets[2] = cut ? t2 : NULL;
        game.spell_target_count = cut ? 2 : 1;
        game.effects.rolls.spell = invis;
        slot_to = cut ? NULL : t2;
        slot_count = cut ? 1 : 0;
        /* The stats (0be2) are recomputed for the target before the event, and the armour class is
         * read from the slot after it: the swap's t has 60 from its base and t2 keeps its 0 over a
         * base of 60; the cut's t has 0 from its base over a stale 60. The hit's 10 (with THAC0 10)
         * meets 0, not 60. */
        cok_field_set8(R->character, t->record, COK_FIELD_BASE_AC, cut ? 0 : 60);
        cok_field_set8(R->character, t->record, COK_FIELD_AC, 60);
        cok_field_set8(R->character, t2->record, COK_FIELD_BASE_AC, 60);
        /* The handler's d20 of 1, then the hit's 10. */
        game.vm.seed = seed_from(2, (const unsigned[]){20, 1, 1, 20, 10, 10});
        CHECK(queen_spell_apply(&game, invis, 0, false, 3, 8, "") && game.vm.status == COK_ECL_OK);
        cok_character *hit = cut ? t : t2, *left = cut ? t2 : t;
        CHECK(game.spell_targets[1] == hit && game.spell_target_count == 1);
        CHECK(cok_field_u8(R->character, hit->record, COK_FIELD_HIT_POINTS) == 7 &&
              cok_character_find_effect(hit, 0x45) != NULL);
        CHECK(cok_field_u8(R->character, left->record, COK_FIELD_HIT_POINTS) == 10 &&
              cok_character_find_effect(left, 0x45) == NULL);
    }
    game.rules = game.effects.rules = R;
    game.effects.dispel = dispel;
    slot_to = NULL;
    slot_count = 0;
    game.spell_targets[2] = NULL;
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* The damage's text and the death (dqk 148f:2214): a casting target "lost a spell" and its byte
 * marked 0x80; one brought down says "Goes Down and is Dying" (status 5), and quiet with bit 8 says
 * "Goes Down" alone; a hit 10 or more past the hit points leaves status 6 (177e:17a0), "is killed"
 * alone, without "Goes Down". */
static void test_spell_damage(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    memset(t->record + 0xce, 0, 0x89);
    t->record[0xce + 0x50] = 0x21;
    t->combat->spell = 0x21;
    t->combat->initiative = 5;
    game.vm.seed = seed_from(1, (const unsigned[]){4, 4, 4});
    uint32_t want = game.vm.seed;
    for (unsigned k = 0; k < 5; ++k) cok_tp_random(&want, 1000); /* 4 frames, "lost a spell"'s wait */
    CHECK(queen_cast_damage(&game, t, 2, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want);
    CHECK(LOGGED("lost a spell") && t->record[0xce + 0x50] == 0xa1);
    CHECK(t->combat->spell == 0 && t->combat->initiative == 0 && t->combat->may_cast == 0);
    CHECK(LOGGED("takes 2 points of damage from Magic"));
    battle_clear();
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    cok_field_set8(R->character, t->record, COK_FIELD_STATUS, 6);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 4, 4});
    /* The damage leaves the target dying (status 5) before the text is chosen (148f:2214 reads the
     * status after 177e:17a0): "Goes Down and is Dying", the death's sound 6 and the target inactive. */
    CHECK(queen_cast_damage(&game, t, 5, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("Goes Down and is Dying;") && LOGGED("sound: 6;"));
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_ACTIVE) == 0);
    battle_clear();
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    cok_field_set8(R->character, t->record, COK_FIELD_STATUS, 5);
    cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1, 8);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 4, 4});
    CHECK(queen_cast_damage(&game, t, 5, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("Goes Down;") && !LOGGED("and is Dying") && !LOGGED("is killed"));
    battle_clear();
    /* 12 points on 1: 11 past it, status 6 (177e:17a0's > 9), "is killed" alone. */
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 4, 4});
    CHECK(queen_cast_damage(&game, t, 12, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_STATUS) == 6);
    CHECK(LOGGED("is killed;") && !LOGGED("Goes Down") && !LOGGED("and is Dying"));
    battle_clear();
    /* A save of kind 2 halves the damage (signed: 5 is 2), and one point is the singular text. */
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 10);
    cok_field_set8(R->character, t->record, COK_FIELD_MAX_HIT_POINTS, 10);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    game.vm.seed = seed_from(1, (const unsigned[]){4, 4, 4});
    CHECK(queen_cast_damage(&game, t, 5, 2, true) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 8);
    CHECK(LOGGED("takes 2 points of damage from Magic"));
    CHECK(queen_cast_damage(&game, t, 1, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("takes 1 point of damage from Magic"));
    /* The source by the type less bit 8: 9 is Fire, 2 Cold; 0x10 has none (not 1, 2, 4, nor 8 alone). */
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 10);
    game.effects.rolls.damage_type = 9;
    CHECK(queen_cast_damage(&game, t, 1, 0, false) && LOGGED("takes 1 point of damage from Fire;"));
    game.effects.rolls.damage_type = 2;
    CHECK(queen_cast_damage(&game, t, 1, 0, false) && LOGGED("takes 1 point of damage from Cold;"));
    log_text[0] = '\0';
    game.effects.rolls.damage_type = 0x10;
    CHECK(queen_cast_damage(&game, t, 1, 0, false) && LOGGED("takes 1 point of damage ;"));
    game.effects.rolls.damage_type = 0;
    battle_clear();
}

/* A flash in the battle (dqk 51c2:0731, mode 5): kind 0 draws its four frames and the speed's pause
 * (var 0x12 at 2: a draw of 200 ms), kind 1 its passes of four frames (var 0x12 + 1 times) and no
 * pause. A target off the view is centred on (the view moves). */
static void test_flash_draws(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x17));
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 2);
    game.vm.seed = 5;
    uint32_t want = 5;
    for (unsigned k = 0; k < 5; ++k) cok_tp_random(&want, 1000);
    CHECK(cok_arena_flash(&game, t, 0, "burst") && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want && LOGGED("sound: 4;") && !LOGGED("sound: 5;"));
    log_text[0] = '\0';
    game.vm.seed = 5;
    want = 5;
    for (unsigned k = 0; k < 12; ++k) cok_tp_random(&want, 1000);
    CHECK(cok_arena_flash(&game, t, 1, "effect") && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want && LOGGED("sound: 5;") && !LOGGED("sound: 4;"));
    /* A target off the view (30, 20 from the corner 0, 0) is centred on first (5837:0846(x, y, 3,
     * 8): the corner 27, 17). */
    duel_at(&a, &t, 30, 20);
    CHECK(game.combat.view_x == 0 && game.combat.view_y == 0);
    CHECK(cok_arena_flash(&game, t, 0, "burst") && game.vm.status == COK_ECL_OK);
    CHECK(game.combat.view_x == 27 && game.combat.view_y == 17);
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 0);
    battle_clear();
}

/* Band a2's one-target damage and heals (queen_cast.c's, 7fd8's band a2): each handler's draws in the original's
 * order (the dice before the level; a deity 4 caster's bonus before the target test), the amount, the damage's
 * text, and the flash that follows a hit (four frames at speed 0). Their saves and touches are the apply's. */
static bool run_handler(uint8_t spell)
{
    queen_spell_fn *handler = queen_spell_handler(spell);
    return handler != NULL && handler(&game) && game.vm.status == COK_ECL_OK;
}

static void set_target(cok_character *a, cok_character *t, uint8_t spell)
{
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = spell;
}

static uint8_t hit_points(const cok_character *c)
{
    return cok_field_u8(R->character, c->record, COK_FIELD_HIT_POINTS);
}

static void set_hit_points(cok_character *c, uint8_t hp, uint8_t max)
{
    cok_field_set8(R->character, c->record, COK_FIELD_HIT_POINTS, hp);
    cok_field_set8(R->character, c->record, COK_FIELD_MAX_HIT_POINTS, max);
}

static void flash_draws(uint32_t *want, unsigned n)
{
    for (unsigned k = 0; k < n; ++k) cok_tp_random(want, 1000);
}

static void test_band_a2_damage(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    /* The apply reads the caster level for every spell (its effect's value, 849a:10f9), and the kind 255 of the
     * damage spells (Cause Light, Serious and Critical: the computer's only) has none for a caster with class
     * levels: the non-caster's 6 (class 0, 5 and 4 below 8, 1 below 9) is the level here. */
    set_class(a, 0, 0);
    set_class(a, 1, 0);
    set_class(a, 4, 0);
    set_class(a, 5, 0);
    uint8_t lv = 0;
    /* Cause Light (0x04, a touch): 1d8, then the hit roll (20 hits), the damage from magic; one die counted. */
    set_target(a, t, 0x04);
    set_hit_points(t, 0xf0, 0xf0);
    game.vm.seed = seed_from(2, (const unsigned[]){8, 1, 8, 20, 20, 20});
    uint32_t want = game.vm.seed;
    uint8_t amount = cok_dice(&want, 1, 8);
    cok_dice(&want, 1, 20);
    flash_draws(&want, 4);
    CHECK(run_handler(0x04) && game.vm.seed == want);
    CHECK(hit_points(t) == (uint8_t)(0xf0 - amount) && game.effects.rolls.dice == 1);
    CHECK(LOGGED("from Magic") && game.effects.rolls.damage_type == 0);
    /* Cause Serious (0x42, no touch): 2d8 + 1, two dice counted. */
    set_target(a, t, 0x42);
    set_hit_points(t, 0xf0, 0xf0);
    game.vm.seed = 13;
    want = 13;
    amount = (uint8_t)(cok_dice(&want, 2, 8) + 1);
    flash_draws(&want, 4);
    log_text[0] = '\0';
    CHECK(run_handler(0x42) && game.vm.seed == want && game.effects.rolls.dice == 2);
    CHECK(hit_points(t) == (uint8_t)(0xf0 - amount) && LOGGED("from Magic"));
    /* Cause Critical (0x48, a touch, save kind 1): 3d8 + 3 after the save (a natural 1 fails it: one roll) and
     * the hit roll (20). Three dice counted. */
    set_target(a, t, 0x48);
    set_hit_points(t, 0xf0, 0xf0);
    game.vm.seed = seed_from(5, (const unsigned[]){8, 1, 8, 8, 1, 8, 8, 1, 8, 20, 1, 1, 20, 20, 20});
    want = game.vm.seed;
    amount = (uint8_t)(cok_dice(&want, 3, 8) + 3);
    cok_dice(&want, 1, 20);
    cok_dice(&want, 1, 20);
    flash_draws(&want, 4);
    log_text[0] = '\0';
    CHECK(run_handler(0x48) && game.vm.seed == want && game.effects.rolls.dice == 3);
    CHECK(hit_points(t) == (uint8_t)(0xf0 - amount) && LOGGED("from Magic"));
    /* Shocking Grasp (0x14, a touch, the mage's kind 2): 1d8 + the level, the sum a byte. Class 5 at 250 is the
     * level 250 (-6 read as a byte, the floor at class 4's 0 less 8); 8 + 250 is 2 as a byte, and the sum as a
     * word would be 258. Electricity. */
    set_class(a, 0, 40);
    set_class(a, 1, 40);
    set_class(a, 4, 0);
    set_class(a, 5, 250);
    CHECK(queen_caster_level(&game, a->record, 0x14, false, &lv) && lv == 250);
    set_target(a, t, 0x14);
    set_hit_points(t, 0xf0, 0xf0);
    game.vm.seed = seed_from(2, (const unsigned[]){8, 8, 8, 20, 20, 20});
    want = game.vm.seed;
    cok_dice(&want, 1, 8);
    cok_dice(&want, 1, 20);
    flash_draws(&want, 4);
    log_text[0] = '\0';
    CHECK(run_handler(0x14) && game.vm.seed == want);
    CHECK(hit_points(t) == 0xf0 - 2 && LOGGED("takes 2 points of damage from Electricity"));
    /* Burning Hands (0x09 and 0x40, not a touch): the uncapped level (40, the cap would give 31), no dice, from
     * fire (type 9) for the mage's kind; the cleric's (0x40) is the same 40. */
    set_class(a, 0, 40);
    set_class(a, 1, 40);
    set_class(a, 5, 40);
    CHECK(queen_caster_level(&game, a->record, 0x09, false, &lv) && lv == 40);
    CHECK(queen_caster_level(&game, a->record, 0x09, true, &lv) && lv == 31); /* the cap: 0x1f */
    CHECK(queen_caster_level(&game, a->record, 0x40, false, &lv) && lv == 40);
    const uint8_t bh[] = {0x09, 0x40};
    for (unsigned i = 0; i < 2; ++i) {
        set_target(a, t, bh[i]);
        set_hit_points(t, 0xf0, 0xf0);
        game.vm.seed = 17;
        want = 17;
        flash_draws(&want, 4);
        log_text[0] = '\0';
        CHECK(run_handler(bh[i]) && game.vm.seed == want);
        CHECK(hit_points(t) == 0xf0 - 40 && LOGGED("takes 40 points of damage from Fire"));
    }
    /* Underwater: the bar says it has no effect (177e:153c's speed wait: one draw at speed 0) and nothing else: no
     * dice, no damage. */
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    set_target(a, t, 0x09);
    set_hit_points(t, 0xf0, 0xf0);
    game.vm.seed = 17;
    want = 17;
    cok_tp_random(&want, 1000);
    log_text[0] = '\0';
    CHECK(run_handler(0x09) && game.vm.seed == want);
    CHECK(LOGGED("That has no effect underwater!") && hit_points(t) == 0xf0 && !LOGGED("takes"));
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    /* Shocking Grasp's touch miss (a natural 1 on the hit roll): the dice drawn, nothing dealt, no flash. */
    set_class(a, 0, 0);
    set_class(a, 1, 0);
    set_class(a, 4, 0);
    set_class(a, 5, 0);
    set_target(a, t, 0x14);
    set_hit_points(t, 0xf0, 0xf0);
    game.vm.seed = seed_from(2, (const unsigned[]){8, 1, 8, 20, 1, 1});
    want = game.vm.seed;
    cok_dice(&want, 1, 8);
    cok_dice(&want, 1, 20);
    log_text[0] = '\0';
    CHECK(run_handler(0x14) && game.vm.seed == want);
    CHECK(hit_points(t) == 0xf0 && !LOGGED("takes") && game.effects.rolls.damage_type == 0);
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* The heals (7fd8's 1c2e, 2073 and 2c32; queen_cast.c's heal_first): Cure Serious takes the deity 4 caster's d8
 * first when it has a target, then 2d8, the bonus and 1; Cure Critical reads the bonus before the target test,
 * then 3d8 and the bonus; the druid's Cure Light draws one d8 and ignores the deity. Each heal is "is partially
 * healed" and waits once (the say, one draw at speed 0). */
static void test_band_a2_heals(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    uint32_t want;
    uint8_t amount;
    /* Cure Serious, deity 4: the bonus first (1d8), then 2d8, the bonus and 1. */
    a->record[0x19b] = 4;
    set_hit_points(t, 1, 40);
    game.effects.rolls.spell = 0x3a;
    game.vm.seed = 11;
    want = 11;
    uint8_t bonus = cok_dice(&want, 1, 8);
    amount = (uint8_t)(cok_dice(&want, 2, 8) + bonus + 1);
    cok_tp_random(&want, 1000);
    log_text[0] = '\0';
    CHECK(run_handler(0x3a) && game.vm.seed == want);
    CHECK(hit_points(t) == 1 + amount && LOGGED("is partially healed"));
    /* Deity 0: no bonus die, 2d8 + 1. */
    a->record[0x19b] = 0;
    set_hit_points(t, 1, 40);
    game.vm.seed = 11;
    want = 11;
    amount = (uint8_t)(cok_dice(&want, 2, 8) + 1);
    cok_tp_random(&want, 1000);
    CHECK(run_handler(0x3a) && game.vm.seed == want && hit_points(t) == 1 + amount);
    /* No target: nothing rolled (not even a deity 4 caster's bonus: the test is first, 7fd8:1c34), nothing
     * healed. */
    a->record[0x19b] = 4;
    game.spell_target_count = 0;
    set_hit_points(t, 1, 40);
    game.vm.seed = 11;
    want = 11;
    CHECK(run_handler(0x3a) && game.vm.seed == want && hit_points(t) == 1);
    game.spell_target_count = 1;
    /* Cure Critical, deity 4, no target: its bonus d8 still drawn (before the target test), nothing healed. */
    a->record[0x19b] = 4;
    game.spell_target_count = 0;
    game.vm.seed = 11;
    want = 11;
    cok_dice(&want, 1, 8);
    CHECK(run_handler(0x47) && game.vm.seed == want && hit_points(t) == 1);
    /* Deity 0, no target: no draw at all. */
    a->record[0x19b] = 0;
    game.vm.seed = 11;
    want = 11;
    CHECK(run_handler(0x47) && game.vm.seed == want);
    game.spell_target_count = 1;
    /* Cure Critical with a target: deity 4 is d8 + 3 (the bonus first), then 3d8 and the bonus; deity 0 is 3. */
    a->record[0x19b] = 4;
    set_hit_points(t, 1, 40);
    game.vm.seed = 11;
    want = 11;
    bonus = (uint8_t)(cok_dice(&want, 1, 8) + 3);
    amount = (uint8_t)(cok_dice(&want, 3, 8) + bonus);
    cok_tp_random(&want, 1000);
    CHECK(run_handler(0x47) && game.vm.seed == want && hit_points(t) == 1 + amount);
    a->record[0x19b] = 0;
    set_hit_points(t, 1, 40);
    game.vm.seed = 11;
    want = 11;
    amount = (uint8_t)(cok_dice(&want, 3, 8) + 3);
    cok_tp_random(&want, 1000);
    CHECK(run_handler(0x47) && game.vm.seed == want && hit_points(t) == 1 + amount);
    /* The druid's Cure Light (0x62): one d8, deity 4 or not; no target, no draw. */
    a->record[0x19b] = 4;
    set_hit_points(t, 1, 40);
    game.vm.seed = 11;
    want = 11;
    amount = cok_dice(&want, 1, 8);
    cok_tp_random(&want, 1000);
    CHECK(run_handler(0x62) && game.vm.seed == want && hit_points(t) == 1 + amount);
    game.spell_target_count = 0;
    game.vm.seed = 11;
    CHECK(run_handler(0x62) && game.vm.seed == 11);
    game.spell_target_count = 1;
    a->record[0x19b] = 0;
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Cure Blindness (7fd8:0ff6, 148f:1bc6): a holder of the blindness (0x21) is said "is Cured" (waited), loses it,
 * and the target "can see" is flashed (kind 1: one pass of four frames at speed 0). A target without it: nothing.
 * The effect's own handler runs at its removal as cok_effects_remove does, after the say: the blindness with
 * flag 1 stops at its missing handler (78e4:0672) with "is Cured" already said. */
static void test_band_a2_cure(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = 0x25;
    CHECK(cok_character_add_effect(t, 0x21, 0, 0, false) != NULL);
    game.vm.seed = 11;
    uint32_t want = 11;
    cok_tp_random(&want, 1000); /* the say's wait */
    flash_draws(&want, 4);      /* the flash's frames */
    CHECK(queen_spell_handler(0x25) != NULL && queen_spell_handler(0x25)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want && cok_character_find_effect(t, 0x21) == NULL);
    CHECK(LOGGED("is Cured") && LOGGED("can see"));
    /* A target without it: nothing said, no flash, no draw. */
    log_text[0] = '\0';
    game.vm.seed = 11;
    CHECK(queen_spell_handler(0x25)(&game) && game.vm.status == COK_ECL_OK && game.vm.seed == 11);
    CHECK(!LOGGED("is Cured") && !LOGGED("can see"));
    /* A blindness that asks for its handler at removal: "is Cured" is said first, then the removal stops. */
    CHECK(cok_character_add_effect(t, 0x21, 0, 0, true) != NULL);
    log_text[0] = '\0';
    game.vm.seed = 11;
    CHECK(!queen_spell_handler(0x25)(&game) && game.vm.status == COK_ECL_EFFECT_FAILED);
    CHECK(LOGGED("is Cured") && !LOGGED("can see") && strstr(game.error, "78e4:0672") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Bless (dqk 7fd8:0001, 00a2) and Curse (00c9): a target whose word +0x66 is not the side is cleared (the
 * whole word: a 0x0100 there is the other side). A Bless of spell 1 (not 0x3e) in a battle also clears one
 * of the side with an enemy within 1 (51c2:0980). The rest take the effect (1, 2) with "is Blessed" or
 * "is Cursed", and Bless sets the images flag. The caster is a (side 0), the enemy t (side 1) at 6, 5. */
static void test_bless_curse(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t, *near, *far, *wide;
    duel(&a, &t);
    near = record('N', 0, 0);
    place(near, 7, 5); /* beside t: an enemy within 1 */
    far = record('F', 0, 0);
    place(far, 1, 1);
    wide = record('W', 0, 0);
    place(wide, 1, 3);
    cok_combat_set_field_word(R, wide->record, COK_FIELD_HOSTILE, 0x0100); /* not side 0 */
    game.vm.character = a->record;
    uint8_t bless = 0x01, bless_power = 0x3e, curse = 0x02;
    CHECK(queen_spell_byte(&game, bless, 0x0e) == 1 && queen_spell_byte(&game, bless_power, 0x0e) == 1);
    CHECK(queen_spell_byte(&game, curse, 0x0e) == 2);
    /* Bless (0x01): near is cleared (beside the enemy), far kept, t and wide (the other side) cleared. */
    game.spell_targets[1] = near;
    game.spell_targets[2] = far;
    game.spell_targets[3] = t;
    game.spell_targets[4] = wide;
    game.spell_target_count = 4;
    game.effects.rolls.spell = bless;
    game.effects.rolls.images = 0;
    CHECK(queen_spell_handler(bless)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_targets[1] == NULL && game.spell_targets[2] == far && game.spell_targets[3] == NULL &&
          game.spell_targets[4] == NULL);
    CHECK(game.effects.rolls.images == 1);
    CHECK(cok_character_find_effect(far, 1) != NULL && cok_character_find_effect(near, 1) == NULL &&
          cok_character_find_effect(t, 1) == NULL && cok_character_find_effect(wide, 1) == NULL);
    CHECK(LOGGED("is Blessed"));
    /* Bless's power (0x3e): no melee test, so near is kept and takes the effect. */
    game.spell_targets[1] = near;
    game.spell_targets[2] = far;
    game.spell_targets[3] = t;
    game.spell_targets[4] = wide;
    game.spell_target_count = 4;
    game.effects.rolls.spell = bless_power;
    CHECK(queen_spell_handler(bless_power)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_targets[1] == near && game.spell_targets[2] == far && game.spell_targets[3] == NULL);
    CHECK(cok_character_find_effect(near, 1) != NULL);
    /* Outside a battle (mode 4) the melee test does not run: one beside the enemy (not yet blessed) is kept
     * for Bless too and takes the effect. */
    cok_character *beside = record('B', 0, 0);
    place(beside, 6, 6); /* beside t */
    game.vm.mode = 4;
    game.spell_targets[1] = beside;
    game.spell_targets[2] = NULL;
    game.spell_targets[3] = NULL;
    game.spell_targets[4] = NULL;
    game.spell_target_count = 1;
    game.effects.rolls.spell = bless;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_spell_handler(bless)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_targets[1] == beside && cok_character_find_effect(beside, 1) != NULL);
    game.vm.mode = 5;
    /* The caster's side is its whole word too: a caster of word 0x0100 blesses that side only (wide, not far)
     * and curses the side 0 (51c2:08fd: the word is not 0), so far is kept and t cleared. */
    duel(&a, &t);
    cok_combat_set_field_word(R, a->record, COK_FIELD_HOSTILE, 0x0100);
    game.vm.character = a->record;
    far = record('F', 0, 0);
    place(far, 1, 1);
    wide = record('W', 0, 0);
    place(wide, 1, 3);
    cok_combat_set_field_word(R, wide->record, COK_FIELD_HOSTILE, 0x0100);
    game.spell_targets[1] = far;
    game.spell_targets[2] = wide;
    game.spell_target_count = 2;
    game.effects.rolls.spell = bless;
    CHECK(queen_spell_handler(bless)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_targets[1] == NULL && game.spell_targets[2] == wide);
    CHECK(cok_character_find_effect(wide, 1) != NULL && cok_character_find_effect(far, 1) == NULL);
    game.spell_targets[1] = far;
    game.spell_targets[2] = t;
    game.spell_target_count = 2;
    game.effects.rolls.spell = curse;
    CHECK(queen_spell_handler(curse)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_targets[1] == far && game.spell_targets[2] == NULL);
    CHECK(cok_character_find_effect(far, 2) != NULL && cok_character_find_effect(t, 2) == NULL);
    /* Curse (0x02) of a side-0 caster: the other side (1) is kept; far and near are cleared. */
    duel(&a, &t);
    game.vm.character = a->record;
    far = record('F', 0, 0);
    place(far, 1, 1);
    game.spell_targets[1] = far;
    game.spell_targets[2] = t;
    game.spell_target_count = 2;
    game.effects.rolls.spell = curse;
    CHECK(queen_spell_handler(curse)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_targets[1] == NULL && game.spell_targets[2] == t);
    CHECK(cok_character_find_effect(t, 2) != NULL && cok_character_find_effect(far, 2) == NULL);
    CHECK(LOGGED("is Cursed"));
    /* A hostile caster (word 1) curses the side 0. */
    duel(&a, &t);
    cok_field_set8(R->character, a->record, COK_FIELD_HOSTILE, 1);
    game.vm.character = a->record;
    far = record('F', 0, 0);
    place(far, 1, 1);
    game.spell_targets[1] = far;
    game.spell_targets[2] = t;
    game.spell_target_count = 2;
    game.effects.rolls.spell = curse;
    CHECK(queen_spell_handler(curse)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_targets[1] == far && game.spell_targets[2] == NULL);
    CHECK(cok_character_find_effect(far, 2) != NULL);
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Prayer (dqk 7fd8:13b7): the caster's level (capped at 0x1f; class 0 here) plus 0x80 where bit 0 of the caster's
 * byte +0x66 is set, the effect (0x31) with "is praying". Bit 1 and bit 7 of that byte do nothing. */
static void test_prayer(void)
{
    game.vm.mode = 5; /* the battle */
    const uint8_t prayer = 0x2a;
    CHECK(queen_spell_byte(&game, prayer, 0x0e) == 0x31 && queen_spell_word(&game, prayer, 0x0a) == 0);
    static const struct { uint8_t hostile, level, value; } cases[] = {
        {0, 5, 5}, {1, 5, 0x85}, {2, 5, 5}, {0x80, 5, 5}, {0x81, 5, 0x85},
        {0, 40, 0x1f}, {1, 40, 0x9f}, /* the level capped at 0x1f (uncapped: 0x28, 0xa8) */
    };
    for (size_t k = 0; k < sizeof cases / sizeof *cases; ++k) {
        cok_character *a, *t;
        duel(&a, &t);
        set_class(a, 0, cases[k].level);
        cok_field_set8(R->character, a->record, COK_FIELD_HOSTILE, cases[k].hostile);
        game.vm.character = a->record;
        game.spell_targets[1] = t;
        game.spell_target_count = 1;
        game.effects.rolls.spell = prayer;
        CHECK(queen_spell_handler(prayer)(&game) && game.vm.status == COK_ECL_OK);
        cok_effect *e = cok_character_find_effect(t, 0x31);
        CHECK(e != NULL && e->value == cases[k].value);
        CHECK(LOGGED("is praying"));
    }
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Mirror Image (dqk 7fd8:0957): a d4 first, then the value d << 5 | the caster level (kind 2: BALTHIR's mage
 * level 2 from class 4 10: max(class 5 0, 10 - 8)), the effect 0x1c with "is duplicated". The level is capped
 * at 0x1f (class 5 40). A moon past the three (order 4) stops after the draw. */
static void test_mirror_image(void)
{
    game.vm.mode = 5; /* the battle */
    const uint8_t mirror = 0x20;
    CHECK(queen_spell_word(&game, mirror, 0x00) == 2 && queen_spell_byte(&game, mirror, 0x0e) == 0x1c);
    cok_character *a, *t;
    /* The level 2: d4 = the first draw, then 2. */
    duel(&a, &t);
    set_class(a, 4, 10);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = mirror;
    game.vm.seed = 77;
    uint32_t want = 77;
    uint8_t d = cok_dice(&want, 1, 4);
    CHECK(queen_spell_handler(mirror)(&game) && game.vm.status == COK_ECL_OK);
    cok_effect *e = cok_character_find_effect(t, 0x1c);
    CHECK(e != NULL && e->value == (uint8_t)((d << 5) | 2) && LOGGED("is duplicated"));
    /* The cap: class 5 40 (order 0) is level 0x1f. */
    duel(&a, &t);
    set_class(a, 4, 10);
    set_class(a, 5, 40);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.vm.seed = 77;
    CHECK(queen_spell_handler(mirror)(&game) && game.vm.status == COK_ECL_OK);
    e = cok_character_find_effect(t, 0x1c);
    CHECK(e != NULL && e->value == (uint8_t)((d << 5) | 0x1f));
    /* A magic order past the moons stops in the level, after the draw: one draw is made. */
    duel(&a, &t);
    set_class(a, 4, 10);
    set_class(a, 5, 2);
    cok_field_set8(R->character, a->record, COK_FIELD_MAGIC_ORDER, 4);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.vm.seed = 77;
    CHECK(!queen_spell_handler(mirror)(&game) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(game.vm.seed == after_draws(77, 1, 4));
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Dispel Evil (dqk 7fd8:210b): effect 4 on the caster for the spell's duration with value 0 (148f:252c,
 * no text), then the spell's effect (0x20) on the target with the level as its value and "is affected". */
static void test_dispel_evil(void)
{
    game.vm.mode = 5; /* the battle */
    const uint8_t dispel = 0x49;
    CHECK(queen_spell_byte(&game, dispel, 0x0e) == 0x20 && queen_spell_word(&game, dispel, 0x0a) == 0);
    cok_character *a, *t;
    duel(&a, &t);
    set_class(a, 0, 6);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = dispel;
    uint8_t lv = 0;
    uint16_t minutes = 0;
    CHECK(queen_caster_level(&game, a->record, dispel, true, &lv) &&
          queen_spell_duration(&game, dispel, &minutes));
    CHECK(queen_spell_handler(dispel)(&game) && game.vm.status == COK_ECL_OK);
    cok_effect *self = cok_character_find_effect(a, 4), *target = cok_character_find_effect(t, 0x20);
    CHECK(self != NULL && self->duration == minutes && self->value == 0 && !self->on_remove);
    CHECK(target != NULL && target->duration == minutes && target->value == lv);
    CHECK(self->made < target->made); /* the caster's first */
    CHECK(LOGGED("is affected"));
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Fire Touch (dqk 7fd8:36d6): underwater (var 0x3c) the bar's refusal only (its speed's wait, one draw), no
 * effect; else the effect (0x79) with "is covered in flame". */
static void test_fire_touch(void)
{
    game.vm.mode = 5; /* the battle */
    const uint8_t fire = 0x76;
    CHECK(queen_spell_byte(&game, fire, 0x0e) == 0x79 && queen_spell_byte(&game, fire, 0x03) != 0xff);
    cok_character *a, *t;
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = fire;
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    game.vm.seed = 5;
    uint32_t want = 5;
    cok_tp_random(&want, 1000); /* the bar's speed wait (177e:153c: one draw at speed 0) */
    CHECK(queen_spell_handler(fire)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == want && cok_character_find_effect(t, 0x79) == NULL && !LOGGED("is covered"));
    CHECK(LOGGED("That has no effect underwater!"));
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    CHECK(queen_spell_handler(fire)(&game) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x79) != NULL && LOGGED("is covered in flame"));
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* The cone (dqk 849a:19ff, queen_cone) on the made-up map: the caster's cell is 10, 10 unless the test places it.
 * Its walk: a straight step adds 2 to the walk, a diagonal one 3; the aim goes on along its own steps until the walk
 * is 2 * len. Each test checks the targets the cone lists (spell_targets 1 to the count). */
static cok_character *cone_target(char name, int8_t x, int8_t y)
{
    cok_character *t = record(name, 1, 0);
    place(t, x, y);
    return t;
}

static void test_cone(void)
{
    cok_combat *k = &game.combat;
    cok_character *c, *t, *u, *v, *x, *y, *z, *w, *p;
    /* The walk from 10, 10 at 11, 10 with len 4: 2, 4, 6, 8 (three steps on), the end 14, 10; 15, 10 is past it. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    t = cone_target('T', 14, 10);
    u = cone_target('U', 15, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 11, 10, 1, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t && u != NULL);
    /* A diagonal step adds 3: from 10, 10 at 11, 11 with len 4, 3, 6, 9: the end 13, 13; 14, 14 is past it. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    t = cone_target('T', 13, 13);
    u = cone_target('U', 14, 14);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 11, 11, 1, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t && u != NULL);
    /* The map's edge: from 46, 10 at 47, 10 the walk stops at 49, 10 (the edge, x1 0x31); the target there is taken. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 46, 10);
    t = cone_target('T', 49, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 46, 10, 47, 10, 1, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t);
    /* The sight: a wall at 12, 10 (value 1, block 2) stops it at its own cell, so 13, 10 is not reached. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    k->cells[10][12] = 1;
    t = cone_target('T', 11, 10);
    v = cone_target('V', 13, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 11, 10, 1, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t && v != NULL);
    k->cells[10][12] = 0;
    /* The sight's range is the doubled len: an aim 10 cells east (20, 10) with len 2 walks no further (its line is
     * 20 long, past the walk's 4), and the sight, of range 4 (limit 2 * 4 + 1 = 9 of length), stops at 15, 10 (length
     * 10). A range of len (limit 5) would stop at 13, 10 and leave 14, 10 out. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    t = cone_target('T', 14, 10);
    u = cone_target('U', 16, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 20, 10, 1, 2) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t && u != NULL);
    /* The centre line's quirk: a creature X of two cells (a footprint of 1 by 2: 12, 9 and 12, 10) on the line
     * from 12, 8 south meets it twice, so no later occupant on the line is added: Y at 12, 11 is not. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 12, 8);
    x = record('X', 1, 0);
    place_sized(x, 12, 9, 2);
    uint8_t xi = (uint8_t)(k->count - 1), occ, terrain;
    cok_combat_cell(k, 12, 9, &occ, &terrain);
    CHECK(occ == xi);
    cok_combat_cell(k, 12, 10, &occ, &terrain);
    CHECK(occ == xi);
    y = cone_target('Y', 12, 11);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 12, 8, 12, 9, 1, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == x && y != NULL);
    /* Width 2: the centre's end 14, 10 (P) and then the right side from 14, 11 (Z, the end's neighbour by east, 2);
     * width 3 adds the left at 14, 9 (W). Width 1 is the centre line alone. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    p = cone_target('P', 14, 10);
    z = cone_target('Z', 14, 11);
    w = cone_target('W', 14, 9);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 11, 10, 2, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 2 && game.spell_targets[1] == p && game.spell_targets[2] == z);
    CHECK(queen_cone(&game, 10, 10, 11, 10, 3, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 3 && game.spell_targets[1] == p && game.spell_targets[2] == z &&
          game.spell_targets[3] == w);
    CHECK(queen_cone(&game, 10, 10, 11, 10, 1, 4) && game.spell_target_count == 1);
    /* The caster is never a target: she stands on the centre line at 12, 10 and is listed, then left out. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 12, 10);
    p = cone_target('P', 14, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 11, 10, 1, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == p);
    /* The stops: aimed at the caster's own cell (the walk reads the directions of a line with no steps: 849a:1a9d);
     * width 2 with no one on the centre line (the side it reads was never set: 849a:1c4a). */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    CHECK(!queen_cone(&game, 10, 10, 10, 10, 1, 4) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "849a:1a9d") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    CHECK(!queen_cone(&game, 10, 10, 11, 10, 2, 4) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "849a:1c4a") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.character = NULL;
    battle_clear();
}

/* Cone of Cold (dqk 7fd8:299c) for the caster's mage level lv (class 5, no magic order: the level is lv): a cone of
 * width 2 and length (lv + 1) / 2, at least 1, from the caster at 5, 5 to the spell's cell 6, 5. The target at 6, 5
 * is on the centre line for every length (so the side is set); the target at 10, 5 is reached only by length 5 (the
 * walk 2 to 10). Then lv d4 + lv of cold, "from Cold", half on a save. The first saving throw after the dice is the
 * first target's (queen_save: a roll of 1 fails, a 20 saves, else the throw at most the roll): saving throw 1 saves
 * on any roll but 1, 21 saves on a 20 only. */
static void test_cone_of_cold(void)
{
    cok_character *a, *t, *u;
    const unsigned levels[3] = {1, 2, 9};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned save = 0; save < 2; ++save) {
            duel_at(&a, &t, 6, 5);
            u = cone_target('U', 10, 5);
            cok_field_set8(R->character, u->record, COK_FIELD_ACTIVE, 1);
            game.vm.mode = 5;
            set_class(a, 5, (uint8_t)levels[i]);
            a->record[0x19d] = 0;
            game.vm.character = a->record;
            game.spell_x = 6;
            game.spell_y = 5;
            game.effects.rolls.spell = 0x5c;
            cok_character *both[2] = {t, u};
            for (unsigned j = 0; j < 2; ++j) {
                cok_field_set8(R->character, both[j]->record, COK_FIELD_HIT_POINTS, 0x4e);
                cok_field_set8(R->character, both[j]->record, COK_FIELD_MAX_HIT_POINTS, 0x4e);
                cok_field_set8_at(R->character, both[j]->record, COK_FIELD_SAVING_THROW, 4, save ? 1 : 21);
            }
            uint8_t lv = 0;
            CHECK(queen_spell_level(&game, 0x5c, false, &lv) && lv == levels[i]);
            uint32_t seed0 = 7, after = seed0;
            uint8_t amount = (uint8_t)(cok_dice(&after, lv, 4) + lv);
            uint8_t roll = cok_dice(&after, 1, 20);
            bool made = save ? roll != 1 : roll == 20;
            game.vm.seed = seed0;
            game.vm.status = COK_ECL_OK;
            game.error[0] = '\0';
            log_text[0] = '\0';
            CHECK(queen_spell_handler(0x5c) != NULL && queen_spell_handler(0x5c)(&game) &&
                  game.vm.status == COK_ECL_OK);
            CHECK(made == (save != 0)); /* the seed's first save roll: the branch each variant tests */
            unsigned hit = made ? amount / 2u : amount;
            CHECK(LOGGED("takes ") && LOGGED("from Cold"));
            CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 0x4e - hit);
            /* Only length 5 (lv 9) reaches the target at 10, 5, whose own save is a later roll: full or half. */
            uint8_t hp_u = cok_field_u8(R->character, u->record, COK_FIELD_HIT_POINTS);
            if (levels[i] == 9)
                CHECK(hp_u == 0x4e - amount || hp_u == 0x4e - amount / 2u);
            else
                CHECK(hp_u == 0x4e);
            game.vm.character = NULL;
            battle_clear();
        }
    }
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
    test_ranger();
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
    test_flight();
    test_flight_place();
    test_missile_pictures();
    test_item_type_at();
    test_use_up();
    test_sweep();
    test_kill_dqk();
    test_kill_off_view();
    test_under_cloud();
    test_battle_only_leave_place();
    test_flash_draws();
    test_caster_level();
    test_spell_range();
    test_spell_duration();
    test_magic_missile();
    test_cure_light();
    test_queen_save();
    test_spell_apply();
    test_spell_touch();
    test_spell_touch_slot();
    test_plain_apply();
    test_bless_curse();
    test_prayer();
    test_mirror_image();
    test_dispel_evil();
    test_fire_touch();
    test_spell_damage();
    test_band_a2_damage();
    test_band_a2_heals();
    test_band_a2_cure();
    test_cone();
    test_cone_of_cold();
    cok_party_free(&game.party);
    puts("queen fight tests passed");
    return 0;
}
