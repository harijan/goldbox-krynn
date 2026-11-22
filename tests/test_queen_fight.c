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
    /* The touch's event 0x0b runs on the target with no stop: a holder of 0x04 (Dispel Evil's; 78e4:0168,
     * now ported as the row's COK_FX_EVIL_AVOID) makes an attacker of creature flag 1 7 worse. The touch's
     * hit roll (148f:1771) rolls its d20 after that event and overwrites the roll, so the touch hits as it
     * does without 0x04 (the d20 of 4 hits either way: thac0 0, AC 0). The -7 itself is checked at event
     * 0x0b (cok_effects_dispatch), with the attacker's flag 1 and without it. */
    duel(&a, &t);
    game.vm.character = a->record;
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.effects.rolls.spell = invis;
    cok_field_set8(R->character, a->record, COK_FIELD_THAC0, 0);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CREATURE_FLAGS, 0, 1);
    CHECK(cok_character_add_effect(t, 0x04, 0, 0, false) != NULL);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 4, 4});
    CHECK(queen_spell_apply(&game, invis, 0, false, 3, 8, "") && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 7 && game.effects.rolls.attack_roll == 4);
    game.effects.rolls.attack_roll = 9;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0b) && game.effects.rolls.attack_roll == 2);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CREATURE_FLAGS, 0, 0);
    game.effects.rolls.attack_roll = 9;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0b) && game.effects.rolls.attack_roll == 9);
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
        bool b2 = id == 0x2f || id == 0x73 || id == 0x85 || id == 0x4a || id == 0x57; /* queen_area.c's band b2 */
        bool b3 = id == 0x33 || id == 0x81; /* queen_area.c's band b3 */
        bool c2 = id == 0x15 || id == 0x4e || id == 0x52 || id == 0x54; /* queen_area.c's band c2 */
        CHECK((queen_spell_handler((uint8_t)id) != NULL) ==
              (plain || a2 || id == 0x03 || id == 0x0f || a3 || b2 || b3 || id == 0x5c || id == 0x7c || c2)); /* 0x5c: band b4, 0x7c: b5 */
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
 * The blindness's own handler (78e4:0672, applied_only) does nothing at its removal, flag 1: the cure says both
 * and the target's armour class stays as it was. */
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
    /* A blindness that asks for its handler at removal (flag 1): applied_only, so the removal runs nothing; the
     * cure says "is Cured" and "can see", the node is gone, and the target's AC (+0x18e) is unchanged. */
    CHECK(cok_character_add_effect(t, 0x21, 0, 0, true) != NULL);
    uint8_t ac = cok_field_u8(R->character, t->record, COK_FIELD_AC);
    log_text[0] = '\0';
    game.vm.seed = 11;
    want = 11;
    cok_tp_random(&want, 1000);
    flash_draws(&want, 4);
    CHECK(queen_spell_handler(0x25)(&game) && game.vm.status == COK_ECL_OK && game.vm.seed == want);
    CHECK(LOGGED("is Cured") && LOGGED("can see") && cok_character_find_effect(t, 0x21) == NULL);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_AC) == ac);
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* applied_only (rules.h): a flag-1 node of Bless (0x01, 78e4:0132) removed runs nothing, so the morale and the
 * attack roll stay; the same node held at event 0x0a adds 5 to the morale and 1 to the roll. */
static void test_applied_only(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_effect *e = cok_character_add_effect(t, 0x01, 0, 0, true);
    CHECK(e != NULL);
    game.effects.rolls.morale = 50;
    game.effects.rolls.attack_roll = 10;
    CHECK(cok_effects_remove(&game.effects, t, e, 0x01) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.morale == 50 && game.effects.rolls.attack_roll == 10 &&
          cok_character_find_effect(t, 0x01) == NULL);
    CHECK(cok_character_add_effect(t, 0x01, 0, 0, false) != NULL);
    game.effects.rolls.morale = 50;
    game.effects.rolls.attack_roll = 10;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0a) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.morale == 55 && game.effects.rolls.attack_roll == 11);
    battle_clear();
}

/* The seeing check (dqk 78e4:054a, 0x19, events 1 and 0x10): the selected character a without 0x18 cannot attack
 * the holder t (untargetable 1, the roll 4 worse at each of the two events); with 0x18 neither. */
static void test_invisible_sees(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_character_add_effect(t, 0x19, 0, 0, false) != NULL);
    game.vm.character = a->record;
    game.effects.rolls.untargetable = 0;
    game.effects.rolls.attack_roll = 10;
    CHECK(cok_effects_dispatch(&game.effects, t, 1) && game.effects.rolls.untargetable == 1 &&
          game.effects.rolls.attack_roll == 6);
    game.effects.rolls.attack_roll = 10;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x10) && game.effects.rolls.attack_roll == 6);
    CHECK(cok_character_add_effect(a, 0x18, 0, 0, false) != NULL);
    game.effects.rolls.untargetable = 0;
    game.effects.rolls.attack_roll = 10;
    CHECK(cok_effects_dispatch(&game.effects, t, 1) && game.effects.rolls.untargetable == 0 &&
          game.effects.rolls.attack_roll == 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 0x10) && game.effects.rolls.attack_roll == 10);
    game.vm.character = NULL;
    battle_clear();
}

/* 78e4:010d (0x1b, 0x1f, 0x33, 0x34, 0x35, HELD): at event 7 the holder's turn ends (its initiative, spell,
 * guarding and movement are 0), with either flag: the effect removed ends it too. */
static void test_held_ends_turn(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    game.effects.in_battle = true;
    static const uint8_t held[] = {0x1b, 0x1f, 0x33, 0x34, 0x35};
    for (size_t i = 0; i < sizeof held / sizeof *held; ++i)
        for (int removing = 0; removing < 2; ++removing) {
            cok_combat_record *cr = t->combat;
            cok_effect *e = cok_character_add_effect(t, held[i], 0, 0, removing != 0);
            CHECK(e != NULL);
            cr->initiative = 3;
            cr->spell = 2;
            cr->guarding = 7;
            cr->movement = 8;
            if (removing) CHECK(cok_effects_remove(&game.effects, t, e, held[i]));
            else CHECK(cok_effects_dispatch(&game.effects, t, 7));
            CHECK(game.vm.status == COK_ECL_OK && cr->initiative == 0 && cr->spell == 0 &&
                  cr->guarding == 0 && cr->movement == 0);
            if (!removing) CHECK(cok_effects_remove(&game.effects, t, e, held[i]));
        }
    game.effects.in_battle = false;
    battle_clear();
}

/* The cancel's word (dqk 78e4:00a1): a damage whose type the record is immune to (the 2a6d block: +0xc8 bit
 * 0x10 against 0x80) takes nothing, and the word is zeroed (amount16, not only its byte); a holder of 0x06
 * (78e4:0184, the cancel of type 4) hit by type 0x0c loses the damage and the pending effect; type 1 is not
 * cancelled. The racial cancel 0x5c (value 0x6f) zeroes the word at event 9. */
static void test_cancel_word(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    game.vm.character = a->record;
    game.effects.in_battle = true;
    cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1, 0x10);
    game.effects.rolls.damage_type = 0x80;
    CHECK(queen_cast_damage(&game, t, 5, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 10);
    cok_field_set8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1, 0);
    CHECK(cok_character_add_effect(t, 0x06, 0, 0, false) != NULL);
    game.effects.rolls.damage_type = 0x0c;
    game.effects.rolls.pending = 0x6f;
    CHECK(queen_cast_damage(&game, t, 5, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 10 && game.effects.rolls.pending == 0);
    game.effects.rolls.damage_type = 1;
    game.effects.rolls.pending = 0x6f;
    CHECK(queen_cast_damage(&game, t, 5, 0, false) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_HIT_POINTS) == 5 && game.effects.rolls.pending == 0x6f);
    /* The racial 0x5c (cancel 0x6f) at event 9: the pending 0x6f cancelled, its word zeroed. */
    cok_character *u = record('U', 0, 0);
    CHECK(cok_character_add_effect(u, 0x5c, 0, 0, false) != NULL);
    game.effects.rolls.amount16 = 5;
    game.effects.rolls.amount = 5;
    game.effects.rolls.pending = 0x6f;
    CHECK(cok_effects_dispatch(&game.effects, u, 9) && game.effects.rolls.amount16 == 0 &&
          game.effects.rolls.amount == 0 && game.effects.rolls.pending == 0);
    game.effects.rolls.damage_type = 0;
    game.effects.in_battle = false;
    game.vm.character = NULL;
    battle_clear();
}

/* A game with no hook for its own routines (effect.h's own, NULL) fails the run's call with the routine's
 * address (dqk 78e4:0184, 0x06), rather than calling NULL. */
static void test_own_hook_missing(void)
{
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_character_add_effect(t, 0x06, 0, 0, false) != NULL);
    __typeof__(game.effects.own) hook = game.effects.own;
    game.effects.own = NULL;
    CHECK(!cok_effects_run(&game.effects, t, 0x06, NULL) && strstr(game.effects.error, "0184") != NULL &&
          strstr(game.effects.error, "with no hook") != NULL);
    game.effects.own = hook;
    game.effects.failed = false;
    game.effects.error[0] = '\0';
    battle_clear();
}

/* effects-4: the 7e76 four (queen_effect.c's charm, silence, confusion and terror). Each returns at once with no
 * combat record (the original's rec+0x40 NULL), before its flag test. */
/* The whole side word (+0x66): the routines write its high byte 0. */
static unsigned side_of(const cok_character *c)
{
    return cok_combat_field_word(R, c->record, COK_FIELD_HOSTILE);
}

static uint8_t npc_of(const cok_character *c)
{
    return cok_field_u8(R->character, c->record, COK_FIELD_NPC);
}

static void set_side(cok_character *c, unsigned side)
{
    cok_combat_set_field_word(R, c->record, COK_FIELD_HOSTILE, side);
}

static void set_npc(cok_character *c, uint8_t npc, uint8_t computer)
{
    cok_field_set8(R->character, c->record, COK_FIELD_NPC, npc);
    cok_field_set8(R->character, c->record, COK_FIELD_COMPUTER, computer);
}

/* 7e76:007a (0x0b, charm; ev 0x0f, not applied_only): the first run takes hold (value 0x05 on side 0: value 0x25,
 * side 0, computer 1, NPC 0xb3, target NULL, the sides counted, morale 100); a second run with bit 5 set is the
 * morale only; a value 0x85 on side 1 gains the side and 0x20 (0xe5) and the side is 1; removing puts the side
 * back from bit 6 and an NPC 0xb3 back to a player (computer 0), an NPC not 0xb3 is left alone; with no block
 * nothing runs, either way (h_charm would fail). */
static void test_effects_four_charm(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    set_side(t, 0x0100); /* the low byte read (0), the whole word written */
    set_npc(t, 0, 0);
    t->combat->target = a->record;
    cok_effect *e = cok_character_add_effect(t, 0x0b, 0, 0x05, true);
    CHECK(e != NULL);
    game.combat.sides[0] = game.combat.sides[1] = 9;
    game.effects.rolls.morale = 50;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0x25 && side_of(t) == 0 && npc_of(t) == 0xb3 && computer(t) == 1);
    CHECK(t->combat->target == NULL && game.combat.sides[0] == 2 && game.combat.sides[1] == 0);
    CHECK(game.effects.rolls.morale == 100);
    /* Bit 5 set: the morale only. */
    t->combat->target = a->record;
    game.effects.rolls.morale = 50;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0x25 && t->combat->target == a->record && game.effects.rolls.morale == 100);
    /* Value 0x85 on side 1, NPC 0x80 (bit 7 kept, not made 0xb3): 0xe5 and side 1. */
    e->value = 0x85;
    set_side(t, 1);
    set_npc(t, 0x80, 0);
    t->combat->target = a->record;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0xe5 && side_of(t) == 1 && npc_of(t) == 0x80 && computer(t) == 1);
    CHECK(t->combat->target == NULL);
    /* Removing it (value 0xe5: side 1 in bit 6): the side is 1, the NPC 0x80 and computer 1 stay. */
    game.effects.rolls.morale = 50;
    CHECK(cok_effects_remove(&game.effects, t, e, 0x0b) && game.vm.status == COK_ECL_OK);
    CHECK(side_of(t) == 1 && npc_of(t) == 0x80 && computer(t) == 1 && game.effects.rolls.morale == 50);
    CHECK(cok_character_find_effect(t, 0x0b) == NULL);
    /* Removing one with NPC 0xb3 (value 0x05: side 0): side 0, NPC 0 and computer 0. */
    e = cok_character_add_effect(t, 0x0b, 0, 0x05, true);
    set_side(t, 0x0101);
    set_npc(t, 0xb3, 1);
    CHECK(cok_effects_remove(&game.effects, t, e, 0x0b) && game.vm.status == COK_ECL_OK);
    CHECK(side_of(t) == 0 && npc_of(t) == 0 && computer(t) == 0);
    /* No block: nothing, first run or removal. */
    cok_combat_record *saved = t->combat;
    t->combat = NULL;
    e = cok_character_add_effect(t, 0x0b, 0, 0x05, true);
    set_side(t, 1);
    set_npc(t, 0xb3, 1);
    game.effects.rolls.morale = 50;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0x05 && side_of(t) == 1 && npc_of(t) == 0xb3 && computer(t) == 1);
    CHECK(game.effects.rolls.morale == 50);
    CHECK(cok_effects_remove(&game.effects, t, e, 0x0b) && game.vm.status == COK_ECL_OK);
    CHECK(side_of(t) == 1 && npc_of(t) == 0xb3 && computer(t) == 1);
    t->combat = saved;
    battle_clear();
}

/* 7e76:012c (0x15, silence; ev 0x0f, applied_only; shared within 1): a block that may use items is said "is
 * Silenced" (row 10, waited); a pending spell is forgotten (cok_combat_forget, marked as Dark Queen's rules say:
 * 0x80); the spell, may_use and may_cast are 0 and no turn ends (initiative kept, unlike a "lost a spell"). A
 * holder within 1 silences a member who holds nothing. With no block, nothing. */
static void test_effects_four_silence(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_combat_record *cr = t->combat;
    cok_effect *e = cok_character_add_effect(t, 0x15, 0, 10, false);
    CHECK(e != NULL);
    cr->may_use = 1;
    cr->may_cast = 1;
    cr->spell = 0x0f;
    cr->initiative = 3;
    cok_field_set8_at(R->character, t->record, COK_FIELD_MEMORIZED, 0, 0x0f);
    log_text[0] = '\0';
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("is Silenced") && !LOGGED("lost a spell"));
    CHECK(cr->may_use == 0 && cr->may_cast == 0 && cr->spell == 0 && cr->initiative == 3);
    CHECK(cok_field_u8_at(R->character, t->record, COK_FIELD_MEMORIZED, 0) == (0x0f | R->fight.forget_mark));
    /* No block: nothing said. */
    cok_combat_record *saved = cr;
    t->combat = NULL;
    log_text[0] = '\0';
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0f) && game.vm.status == COK_ECL_OK && !LOGGED("is Silenced"));
    t->combat = saved;
    CHECK(cok_effects_remove(&game.effects, t, e, 0x15) && game.vm.status == COK_ECL_OK);
    /* Shared: a holder at 5, 5 silences t at 6, 5 (within 1), who holds nothing. */
    CHECK(cok_character_add_effect(a, 0x15, 0, 10, false) != NULL);
    cr->may_use = 1;
    log_text[0] = '\0';
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("is Silenced") && cr->may_use == 0);
    game.vm.character = NULL;
    battle_clear();
}

/* A seed whose first d100 is in low..high, whose next draws are the effect's flash (cok_arena_flash: its four
 * frames, one draw each; the text is said with no wait), and whose d20 after them, the save's, is save. */
static uint32_t confusion_seed(unsigned low, unsigned high, unsigned save)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t s = start;
        unsigned d = cok_dice(&s, 1, 100);
        if (d < low || d > high) continue;
        for (unsigned k = 0; k < 4; ++k) cok_tp_random(&s, 1000);
        if (cok_dice(&s, 1, 20) == save) return start;
    }
}

/* 7e76:033f (0x23, confusion; ev 0x15, applied_only): one d100 by the original's table. 1-10 runs away: the 0x23
 * removed, the block forced and computer controlled, its target NULL, and 0x6f added for ten minutes. 11-60 "is
 * confused" and the turn ends. 61-80 0x4d is added and run: its cited stop (7e76:081f) stops the run, before the
 * save. 81-100 "is enraged". Then the save (type 4, bonus -2): made removes the 0x23, failed keeps it; a d20 of 10
 * leaves the save roll at the save bonus + 10 - 2 (the bonus is -2). */
static void test_effects_four_confusion(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_combat_record *cr = t->combat;
    set_npc(t, 0, 0);
    cr->forced = 0;
    cr->target = a->record;
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    /* 1-10: runs away (the save failed: the 0x23 is gone by the run's own removal). */
    CHECK(cok_character_add_effect(t, 0x23, 0, 0, false) != NULL);
    game.vm.seed = confusion_seed(1, 10, 1);
    log_text[0] = '\0';
    CHECK(cok_effects_dispatch(&game.effects, t, 0x15) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x23) == NULL && cr->forced == 1 && computer(t) == 1 && npc_of(t) == 0xb3);
    CHECK(cr->target == NULL && LOGGED("runs away"));
    cok_effect *run = cok_character_find_effect(t, 0x6f);
    CHECK(run != NULL && run->duration == 10 && run->on_remove && run->value == 0);
    CHECK(cok_effects_remove(&game.effects, t, run, 0x6f) && game.vm.status == COK_ECL_OK);
    /* 11-60, saved: "is confused", the turn ends, and the 0x23 is removed. */
    cr->forced = 0;
    set_npc(t, 0, 0);
    cr->initiative = 3;
    CHECK(cok_character_add_effect(t, 0x23, 0, 0, false) != NULL);
    game.vm.seed = confusion_seed(11, 60, 20);
    log_text[0] = '\0';
    CHECK(cok_effects_dispatch(&game.effects, t, 0x15) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("is confused") && cr->initiative == 0 && cok_character_find_effect(t, 0x23) == NULL);
    CHECK(cr->forced == 0 && npc_of(t) == 0 && computer(t) == 0);
    /* 11-60, failed save: the 0x23 kept. */
    CHECK(cok_character_add_effect(t, 0x23, 0, 0, false) != NULL);
    game.vm.seed = confusion_seed(11, 60, 1);
    CHECK(cok_effects_dispatch(&game.effects, t, 0x15) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x23) != NULL);
    /* The save bonus: a d20 of 10 leaves the roll at the save bonus, 10 and -2. */
    game.vm.seed = confusion_seed(11, 60, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 0x15) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.save_roll ==
          (uint8_t)(cok_field_s8(R->character, t->record, COK_FIELD_SAVE_BONUS) + 10 + 0xfe));
    CHECK(cok_effects_remove(&game.effects, t, cok_character_find_effect(t, 0x23), 0x23) &&
          game.vm.status == COK_ECL_OK);
    /* 61-80: 0x4d added, run, and its cited stop ends the run before the save. */
    CHECK(cok_character_add_effect(t, 0x23, 0, 0, false) != NULL);
    game.vm.seed = confusion_seed(61, 80, 20);
    log_text[0] = '\0';
    game.effects.error[0] = '\0';
    CHECK(!cok_effects_dispatch(&game.effects, t, 0x15) && game.vm.status == COK_ECL_EFFECT_FAILED);
    CHECK(strstr(game.effects.error, "7e76:081f") != NULL && LOGGED("goes berserk"));
    CHECK(cok_character_find_effect(t, 0x4d) != NULL && cok_character_find_effect(t, 0x23) != NULL);
    game.vm.status = COK_ECL_OK;
    game.effects.failed = false;
    game.effects.error[0] = '\0';
    game.error[0] = '\0';
    /* 81-100: "is enraged" and the save: made removes it. */
    game.vm.seed = confusion_seed(81, 100, 20);
    log_text[0] = '\0';
    CHECK(cok_effects_dispatch(&game.effects, t, 0x15) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("is enraged") && cok_character_find_effect(t, 0x23) == NULL);
    game.vm.character = NULL;
    battle_clear();
}

/* 7e76:0b3c (0x6f, terror; no event, not applied_only): only removing does anything: an NPC 0xb3 goes back to a
 * player (computer 0) and the block's forced is 0; not removing, nothing; with no block, nothing (h_terror
 * fails there). */
static void test_effects_four_terror(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_combat_record *cr = t->combat;
    cok_effect *e = cok_character_add_effect(t, 0x6f, 0, 0, true);
    CHECK(e != NULL);
    set_npc(t, 0xb3, 1);
    cr->forced = 1;
    CHECK(cok_effects_run(&game.effects, t, 0x6f, e) && game.vm.status == COK_ECL_OK);
    CHECK(npc_of(t) == 0xb3 && computer(t) == 1 && cr->forced == 1);
    CHECK(cok_effects_remove(&game.effects, t, e, 0x6f) && game.vm.status == COK_ECL_OK);
    CHECK(npc_of(t) == 0 && computer(t) == 0 && cr->forced == 0);
    /* No block: nothing, removal included. */
    cok_combat_record *saved = cr;
    t->combat = NULL;
    e = cok_character_add_effect(t, 0x6f, 0, 0, true);
    set_npc(t, 0xb3, 1);
    CHECK(cok_effects_remove(&game.effects, t, e, 0x6f) && game.vm.status == COK_ECL_OK);
    CHECK(npc_of(t) == 0xb3 && computer(t) == 1);
    t->combat = saved;
    game.vm.character = NULL;
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

/* A seed whose draws are dice (count, sides), then rounds of a d20 with four flash frames after each (a target's save and
 * its damage's flash, queen_cast_damage): every d20 is 2 to 19, so a save is the saving throw's alone (a natural 1 fails,
 * a 20 saves). */
static uint32_t mid_seed(unsigned count, uint8_t sides, unsigned rounds)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t w = start;
        cok_dice(&w, (uint8_t)count, sides);
        bool ok = true;
        for (unsigned i = 0; i < rounds && ok; ++i) {
            uint8_t r = cok_dice(&w, 1, 20);
            ok = r >= 2 && r <= 19;
            flash_draws(&w, 4);
        }
        if (ok) return start;
    }
}

/* A target the spells of band b2 hit with no saving throw (30: above any roll) or with one (0: any roll but a 1 saves),
 * on the saving throw of type 4 (the spell records' type), at full hit points. */
static void b2_target(cok_character *t, uint8_t throw_value)
{
    cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, 4, throw_value);
    set_hit_points(t, 0xf0, 0xf0);
}

/* Band b2's Fireball family (dqk 7fd8:1519) on the non-caster's level 6 (class 0 and 5 at 0): nd6 of fire (type 9) at the
 * level, the save (kind 2: half on a save), the flash. Delayed Blast Fireball adds the level to the dice (before the
 * half); the Necklace rolls a d3 first and counts (2d + 1)d6 with no bonus. Underwater the bar says it has no effect
 * (its wait one draw) and nothing else. Lightning's underwater area (queen_fireball with 0x33) lists the two combatants at
 * radius 2, and each takes the one amount, nearest first. */
static void test_band_b2_fireball(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    uint8_t lv = 0;
    game.vm.character = a->record; /* the caster the level is read from (DS:ad67) */
    CHECK(queen_spell_level(&game, 0x2f, false, &lv) && lv == 6);
    /* The records: the save kind 2 (half) and type 4 for the three spells. */
    CHECK(queen_spell_word(&game, 0x2f, 0x0a) == 2 && queen_spell_word(&game, 0x73, 0x0a) == 2 &&
          queen_spell_word(&game, 0x85, 0x0a) == 2 && queen_spell_word(&game, 0x2f, 0x0c) == 4);
    for (unsigned k = 0; k < 2; ++k) {
        /* Fireball (0x2f): one target, a d20 for the save, then the damage's flash. */
        uint8_t throw_value = k == 0 ? 30 : 0;
        b2_target(t, throw_value);
        set_target(a, t, 0x2f);
        uint32_t seed = mid_seed(6, 6, 1);
        game.vm.seed = seed;
        uint32_t want = seed;
        uint8_t sum = cok_dice(&want, 6, 6);
        cok_dice(&want, 1, 20);
        flash_draws(&want, 4);
        uint8_t amount = k == 0 ? sum : (uint8_t)(sum / 2);
        log_text[0] = '\0';
        game.effects.rolls.images = 0;
        CHECK(run_handler(0x2f) && game.vm.seed == want && game.effects.rolls.dice == 6);
        CHECK(hit_points(t) == (uint8_t)(0xf0 - amount) && LOGGED("from Fire") && game.effects.rolls.images == 1);
        /* Delayed Blast Fireball (0x73): the level added to the dice, before the half. */
        b2_target(t, throw_value);
        set_target(a, t, 0x73);
        seed = mid_seed(6, 6, 1);
        game.vm.seed = seed;
        want = seed;
        uint8_t total = (uint8_t)(cok_dice(&want, 6, 6) + lv);
        cok_dice(&want, 1, 20);
        flash_draws(&want, 4);
        amount = k == 0 ? total : (uint8_t)(total / 2);
        log_text[0] = '\0';
        CHECK(run_handler(0x73) && game.vm.seed == want && game.effects.rolls.dice == 6);
        CHECK(hit_points(t) == (uint8_t)(0xf0 - amount) && LOGGED("from Fire"));
    }
    /* The Necklace (0x85): a d3 first, then (2d + 1) d6, the count stored, no bonus; not underwater the same. */
    b2_target(t, 30);
    set_target(a, t, 0x85);
    uint32_t seed = 1;
    for (;; ++seed) {
        uint32_t w = seed;
        uint8_t n = (uint8_t)(cok_dice(&w, 1, 3) * 2 + 1);
        cok_dice(&w, n, 6);
        uint8_t r = cok_dice(&w, 1, 20);
        if (r >= 2 && r <= 19) break;
    }
    game.vm.seed = seed;
    uint32_t want = seed;
    uint8_t n = (uint8_t)(cok_dice(&want, 1, 3) * 2 + 1);
    uint8_t sum = cok_dice(&want, n, 6);
    cok_dice(&want, 1, 20);
    flash_draws(&want, 4);
    log_text[0] = '\0';
    CHECK(run_handler(0x85) && game.vm.seed == want && game.effects.rolls.dice == n);
    CHECK(hit_points(t) == (uint8_t)(0xf0 - sum) && LOGGED("from Fire"));
    /* A mage of level 60 (class 5): the level is uncapped (not 31), and the Delayed Blast's 60d6 + 60 is a byte (the
     * seed's sum is past 255, so it wraps: 148f:199e's byte and 1625's byte add). */
    cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 5, 60);
    CHECK(queen_spell_level(&game, 0x73, false, &lv) && lv == 60);
    CHECK(queen_spell_level(&game, 0x73, true, &lv) && lv == 0x1f);
    b2_target(t, 30);
    set_target(a, t, 0x73);
    for (seed = 1;; ++seed) {
        uint32_t w = seed;
        unsigned whole = 60;
        for (unsigned i = 0; i < 60; ++i) whole += cok_tp_random(&w, 6) + 1u;
        uint8_t r = cok_dice(&w, 1, 20);
        if (whole > 0xff && (uint8_t)whole < 0xf0 && r >= 2 && r <= 19) break;
    }
    game.vm.seed = seed;
    want = seed;
    sum = (uint8_t)(cok_dice(&want, 60, 6) + 60);
    cok_dice(&want, 1, 20);
    flash_draws(&want, 4);
    log_text[0] = '\0';
    CHECK(run_handler(0x73) && game.vm.seed == want && game.effects.rolls.dice == 60);
    CHECK(hit_points(t) == (uint8_t)(0xf0 - sum) && LOGGED("from Fire"));
    cok_field_set8_at(R->character, a->record, COK_FIELD_CLASS_LEVEL, 5, 0);
    /* Underwater (var 0x3c), the Fireball is refused: the bar (one draw at speed 0), no damage. */
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    b2_target(t, 30);
    set_target(a, t, 0x2f);
    game.vm.seed = 17;
    want = 17;
    cok_tp_random(&want, 1000);
    log_text[0] = '\0';
    CHECK(run_handler(0x2f) && game.vm.seed == want);
    CHECK(LOGGED("That has no effect underwater!") && hit_points(t) == 0xf0 && !LOGGED("takes"));
    /* Lightning underwater (0x33) through queen_fireball: the area at radius 2 round (6, 5) lists the target (nearest)
     * and the caster, and each takes the one amount of nd6 (level 6): two saves (a natural 1 fails, a 20 saves, so the
     * seed is mid) and two flashes, nothing for the centre. */
    b2_target(t, 30);
    b2_target(a, 30);
    set_hit_points(a, 0xf0, 0xf0);
    game.spell_x = 6;
    game.spell_y = 5;
    set_target(a, t, 0x33);
    seed = mid_seed(6, 6, 2);
    game.vm.seed = seed;
    want = seed;
    sum = cok_dice(&want, 6, 6);
    for (unsigned i = 0; i < 2; ++i) {
        cok_dice(&want, 1, 20);
        flash_draws(&want, 4);
    }
    log_text[0] = '\0';
    CHECK(queen_fireball(&game) && game.vm.status == COK_ECL_OK && game.vm.seed == want);
    CHECK(game.spell_target_count == 2 && game.effects.rolls.images == 1);
    CHECK(hit_points(t) == (uint8_t)(0xf0 - sum) && hit_points(a) == (uint8_t)(0xf0 - sum) && LOGGED("from Fire"));
    /* The radius is 2: round (8, 5) the target (6, 5) is listed, the caster (5, 5) is not, and he takes nothing. */
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    b2_target(t, 30);
    b2_target(a, 30);
    game.spell_x = 8;
    game.spell_y = 5;
    set_target(a, a, 0x33);
    seed = mid_seed(6, 6, 1);
    game.vm.seed = seed;
    want = seed;
    sum = cok_dice(&want, 6, 6);
    cok_dice(&want, 1, 20);
    flash_draws(&want, 4);
    log_text[0] = '\0';
    CHECK(queen_fireball(&game) && game.vm.status == COK_ECL_OK && game.vm.seed == want);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t);
    CHECK(hit_points(t) == (uint8_t)(0xf0 - sum) && hit_points(a) == 0xf0);
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Band b2's Flame Strike (dqk 7fd8:2157): 6d8 of fire (type 9), halved by a save; underwater the bar only. */
static void test_band_b2_flame(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    CHECK(queen_spell_word(&game, 0x4a, 0x0a) == 2);
    for (unsigned k = 0; k < 2; ++k) {
        b2_target(t, k == 0 ? 30 : 0);
        set_target(a, t, 0x4a);
        uint32_t seed = mid_seed(6, 8, 1);
        game.vm.seed = seed;
        uint32_t want = seed;
        uint8_t sum = cok_dice(&want, 6, 8);
        cok_dice(&want, 1, 20);
        flash_draws(&want, 4);
        uint8_t amount = k == 0 ? sum : (uint8_t)(sum / 2);
        log_text[0] = '\0';
        game.effects.rolls.images = 0;
        CHECK(run_handler(0x4a) && game.vm.seed == want && game.effects.rolls.dice == 6);
        CHECK(hit_points(t) == (uint8_t)(0xf0 - amount) && LOGGED("from Fire") && game.effects.rolls.images == 1);
    }
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    b2_target(t, 30);
    set_target(a, t, 0x4a);
    game.vm.seed = 17;
    uint32_t want = 17;
    cok_tp_random(&want, 1000);
    log_text[0] = '\0';
    CHECK(run_handler(0x4a) && game.vm.seed == want);
    CHECK(LOGGED("That has no effect underwater!") && hit_points(t) == 0xf0 && !LOGGED("takes"));
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Band b2's Ice Storm (dqk 7fd8:28fb): 3d10 of magic (type 8), one roll for every target (the second target takes the
 * same), no save roll (save kind 0), and the image flag; underwater 1d10. */
static void test_band_b2_ice(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x19));
    CHECK(queen_spell_word(&game, 0x57, 0x0a) == 0);
    for (unsigned k = 0; k < 2; ++k) {
        b2_target(t, 30);
        b2_target(a, 30);
        set_target(a, t, 0x57);
        game.spell_targets[2] = a;
        game.spell_target_count = 2;
        uint32_t seed = 9 + k;
        game.vm.seed = seed;
        uint32_t want = seed;
        uint8_t amount = cok_dice(&want, k == 0 ? 3 : 1, 10);
        flash_draws(&want, 4);
        flash_draws(&want, 4);
        log_text[0] = '\0';
        game.effects.rolls.images = 0;
        CHECK(run_handler(0x57) && game.vm.seed == want && game.effects.rolls.dice == (k == 0 ? 3 : 1));
        CHECK(hit_points(t) == (uint8_t)(0xf0 - amount) && hit_points(a) == (uint8_t)(0xf0 - amount));
        CHECK(LOGGED("from Magic") && game.effects.rolls.images == 1 && game.effects.rolls.damage_type == 0);
        if (k == 0) cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1); /* the underwater round below */
    }
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    game.vm.character = NULL;
    game.spell_target_count = 0;
    battle_clear();
}

/* Band b3 (part 31d): the bolt (dqk 7fd8:1772, queen_bolt) and its hit (7fd8:1a54), Lightning Bolt (7fd8:1ac3) and Javelin
 * of Lightning (7fd8:1d20). The bolt walks 2 * len half-cells from the spell's cell away from the caster (vm.character): a
 * straight step is 2, a diagonal one 3. A segment ends at another combatant, a cell costing more than 1, or the line's end,
 * and hits its last cell's combatant once (the save of type 4, halved where saved). The walks below are the listing's
 * (7fd8:1772's charges and ends), worked out by hand. The missiles draw random numbers too (their waits), so a test
 * does not model the seed: it searches for a start seed whose saves roll what the expected outcome needs (a throw of 30
 * saves only a roll of 20, a throw of 0 only a roll of 1 fails), and checks the hit points of every combatant. */

/* The selected member, C, at x, y in a battle of its own: the bolt's missile icons (slot 0x14) loaded, its save of type 4 at
 * 30, full hit points (record()'s are 10), and the damage's type electricity (0x0c, as the Lightning and the Javelin set it). */
static cok_character *bolt_caster(int8_t x, int8_t y)
{
    battle_clear();
    cok_character *c = record('C', 0, 0);
    place(c, x, y);
    cok_field_set8(R->character, c->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8_at(R->character, c->record, COK_FIELD_SAVING_THROW, 4, 30);
    cok_field_set8(R->character, c->record, COK_FIELD_HIT_POINTS, 0xf0);
    cok_field_set8(R->character, c->record, COK_FIELD_MAX_HIT_POINTS, 0xf0);
    game.effects.rolls.damage_type = 0x0c; /* the Lightning's electricity: the text of the bolt's hits */
    game.vm.character = c->record;
    game.vm.mode = 5;
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    CHECK(cok_adventure_drawing(&game)->arena_load_icon(&game, "CPIC.TLB", 64, 0x14));
    return c;
}

/* A foe at x, y: full hit points, saving throws 30 of the other types and throw_value of type 4. */
static cok_character *bolt_foe(int8_t x, int8_t y, uint8_t throw_value)
{
    cok_character *t = record('M', 1, 0);
    place(t, x, y);
    cok_field_set8(R->character, t->record, COK_FIELD_ACTIVE, 1);
    for (unsigned k = 0; k < 4; ++k) cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, k, 30);
    b2_target(t, throw_value);
    return t;
}

/* The start seeds tried by a test (the first whose outcome is the test's). */
enum { BOLT_SEEDS = 4096 };

/* The hits of amount logged since log_text was cleared ("takes N points of damage", once a hit). */
static unsigned bolt_logged(unsigned amount)
{
    char text[40];
    snprintf(text, sizeof text, "takes %u points of damage", amount);
    unsigned n = 0;
    for (const char *p = strstr(log_text, text); p != NULL; p = strstr(p + 1, text)) ++n;
    return n;
}

/* queen_bolt(len, 20, flag) from a cleared log, true where it ran with hits hits of 20 and no saved one (10): a search's
 * seed then has every throw failed, so the hit points count the hits (an extra hit cannot pass as two saved halves). */
static bool bolt_run(uint8_t len, bool flag, unsigned hits)
{
    log_text[0] = '\0';
    return queen_bolt(&game, len, 20, flag) && bolt_logged(20) == hits && bolt_logged(10) == 0;
}

/* Four hits on a straight line east: the spell's cell 7, 10 and the foes at 9, 11, 13 (the charges 4, 4, 2, 2 walk the 14
 * half-cells to 13 and past). Each foe once, all 20 (throw 30); the caster at 5, 10 behind the spell is not in line. */
static void test_band_b3_straight(void)
{
    cok_character *c = NULL, *f[4] = {NULL, NULL, NULL, NULL};
    uint32_t start;
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        f[0] = bolt_foe(7, 10, 30);
        f[1] = bolt_foe(9, 10, 30);
        f[2] = bolt_foe(11, 10, 30);
        f[3] = bolt_foe(13, 10, 30);
        game.spell_x = 7;
        game.spell_y = 10;
        game.vm.seed = start;
        log_text[0] = '\0';
        if (bolt_run(7, false, 4) && hit_points(f[0]) == 0xf0 - 20 && hit_points(f[1]) == 0xf0 - 20 &&
            hit_points(f[2]) == 0xf0 - 20 && hit_points(f[3]) == 0xf0 - 20 && hit_points(c) == 0xf0)
            break;
    }
    CHECK(start < BOLT_SEEDS && game.vm.status == COK_ECL_OK && LOGGED("from Electricity"));
    battle_clear();
}

/* The lengths: a straight step is 2 half-cells, a diagonal one 3. len 3 (6 half-cells) from the spell 6, 9 with the caster at
 * 5, 10 walks the diagonal 7, 8 then 8, 7 (charges 3, 3): those two are hit and 9, 6 is not. Straight from 6, 10 it walks
 * 7, 8, 9 (charges 2, 2, 2): all three are hit and 10, 10 is not. len 1 (2 half-cells) walks one cell: 7, 10 is hit, 8, 10
 * is not. */
static void test_band_b3_lengths(void)
{
    cok_character *c = NULL, *d1 = NULL, *d2 = NULL, *d3 = NULL;
    uint32_t start;
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        d1 = bolt_foe(7, 8, 30);
        d2 = bolt_foe(8, 7, 30);
        d3 = bolt_foe(9, 6, 30);
        game.spell_x = 6;
        game.spell_y = 9;
        game.vm.seed = start;
        if (bolt_run(3, false, 2) && hit_points(d1) == 0xf0 - 20 && hit_points(d2) == 0xf0 - 20 &&
            hit_points(d3) == 0xf0 && hit_points(c) == 0xf0)
            break;
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();

    cok_character *s[4] = {NULL, NULL, NULL, NULL};
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        s[0] = bolt_foe(7, 10, 30);
        s[1] = bolt_foe(8, 10, 30);
        s[2] = bolt_foe(9, 10, 30);
        s[3] = bolt_foe(10, 10, 30);
        game.spell_x = 6;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(3, false, 3) && hit_points(s[0]) == 0xf0 - 20 && hit_points(s[1]) == 0xf0 - 20 &&
            hit_points(s[2]) == 0xf0 - 20 && hit_points(s[3]) == 0xf0 && hit_points(c) == 0xf0)
            break;
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();

    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        cok_character *one = bolt_foe(7, 10, 30), *two = bolt_foe(8, 10, 30);
        game.spell_x = 6;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(1, false, 1) && hit_points(one) == 0xf0 - 20 && hit_points(two) == 0xf0 &&
            hit_points(c) == 0xf0)
            break;
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();
}

/* One hit at the spell's cell with throw 0 (saved by any roll but a 1): half the damage, the save of type 4 (the other
 * types' throws are 30, so a save by any type but 4 takes all of it). */
static void test_band_b3_save(void)
{
    cok_character *c = NULL, *g = NULL;
    uint32_t start;
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        g = bolt_foe(7, 10, 0);
        game.spell_x = 7;
        game.spell_y = 10;
        game.vm.seed = start;
        log_text[0] = '\0';
        if (queen_bolt(&game, 1, 20, false) && hit_points(g) == 0xf0 - 10) break;
    }
    CHECK(start < BOLT_SEEDS && LOGGED("takes 10 points of damage from Electricity") && hit_points(c) == 0xf0);
    battle_clear();
}

/* A wall (terrain 1, cost 255) at 9, 10 between the spell (7, 10) and the caster (5, 10); the foe G at 2, 10 is beyond the
 * caster. The walk from 7 (charge 4) bounces at the wall, back toward the caster along the line (8 half-cells long: no
 * doubling without flag). The new line from 9 hits the spell's foe again (charge 0, so 2; the rest 8), then the caster at 5
 * (charge 4), and the walk ends at 3, short of G. With flag the walk back is doubled once (line 8, charge 8): the second hit
 * on the spell's foe comes with its first charge wrapped (4 - 8), which ends the bolt, so the caster is not hit. Any cell
 * costing more than 1 ends a segment and bounces (1772's DS:6197 > 1): the same with terrain 44 (cost 2) for the wall. */
static void test_band_b3_bounce(void)
{
    cok_character *c = NULL, *f = NULL, *g = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    CHECK(cok_ecl_load_var(&game.vm, 0x22) == 1);
    static const uint8_t walls[] = {1, 44};
    for (size_t w = 0; w < sizeof walls; ++w) {
        CHECK(cok_combat_terrain(R, walls[w]).cost > 1);
        for (start = 1; start < BOLT_SEEDS; ++start) {
            c = bolt_caster(5, 10);
            f = bolt_foe(7, 10, 30);
            g = bolt_foe(2, 10, 30);
            game.combat.cells[10][9] = walls[w];
            game.spell_x = 7;
            game.spell_y = 10;
            game.vm.seed = start;
            if (bolt_run(7, false, 3) && hit_points(f) == 0xf0 - 40 && hit_points(c) == 0xf0 - 20 && hit_points(g) == 0xf0)
                break;
        }
        CHECK(start < BOLT_SEEDS);
        battle_clear();
        for (start = 1; start < BOLT_SEEDS; ++start) {
            c = bolt_caster(5, 10);
            f = bolt_foe(7, 10, 30);
            g = bolt_foe(2, 10, 30);
            game.combat.cells[10][9] = walls[w];
            game.spell_x = 7;
            game.spell_y = 10;
            game.vm.seed = start;
            if (bolt_run(7, true, 2) && hit_points(f) == 0xf0 - 40 && hit_points(c) == 0xf0 && hit_points(g) == 0xf0) break;
        }
        CHECK(start < BOLT_SEEDS);
        battle_clear();
    }
}

/* flag is spent at the first bounce, doubled or not: len 13 (26 half-cells), the caster at 5, 10, the spell's foe F at 6, 10,
 * walls at 10, 10 and 3, 10. The walk 6 -> 10 (charge 8, the rest 18) bounces with the line back 10 long (no doubling) and
 * clears flag; the new line hits F at 6 (charge 2), the caster at 5 (2) and bounces at 3 (charge 4, the rest 10; the line back
 * 4 long, but flag is clear); the next line hits the caster at 5 again and its first charge wraps (4 - 14): the end. F and
 * the caster each twice. (flag kept to the second bounce would double its line to 28: charge 18, the rest 0, the caster
 * hit once.) */
static void test_band_b3_second_bounce(void)
{
    cok_character *c = NULL, *f = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        f = bolt_foe(6, 10, 30);
        game.combat.cells[10][10] = 1;
        game.combat.cells[10][3] = 1;
        game.spell_x = 6;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(13, true, 4) && hit_points(f) == 0xf0 - 40 && hit_points(c) == 0xf0 - 40) break;
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();
}

/* A bounce off the caster's own cell (terrain 44, cost 2, under the caster at 5, 10; var 0x22 1): the spell's foe F at 7,
 * 10, a wall at 9, 10. The walk bounces at 9 (charge 4, the rest 10), the new line hits F (2) and the caster (charge 4, the
 * rest 4), whose cell bounces it again: the next line is from the caster's cell to itself, zero long. Its segment walks
 * nothing and keeps the occupant last read (the caster's, 1772's bp-0x3e), so no hit, and its charge wraps (0 - 8): the end.
 * F twice, the caster once. */
static void test_band_b3_caster_bounce(void)
{
    cok_character *c = NULL, *f = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        f = bolt_foe(7, 10, 30);
        game.combat.cells[10][5] = 44;
        game.combat.cells[10][9] = 1;
        game.spell_x = 7;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(7, false, 3) && hit_points(f) == 0xf0 - 40 && hit_points(c) == 0xf0 - 20) break;
    }
    CHECK(start < BOLT_SEEDS && game.vm.status == COK_ECL_OK);
    battle_clear();
}

/* The wrapped first charge is the bolt's own: a foe M at 8, 10 between the spell and the wall at 9, 10 (the spell's foe F at
 * 7, 10). The first segment hits M (charge 2), the wall bounces the bolt (charge 4 - 2 = 2, the line 4 long), and the new
 * line's first segment hits M again: its charge is 2 - 4, which wraps, so the bolt ends there. The caster at 5, 10 is not
 * hit. (A charge reset after the bounce would charge the new line's first segment 2 - 0, so the bolt would go on: hit F at 7
 * and the caster at 5.) */
static void test_band_b3_wrap(void)
{
    cok_character *c = NULL, *f = NULL, *m = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        f = bolt_foe(7, 10, 30);
        m = bolt_foe(8, 10, 30);
        game.combat.cells[10][9] = 1;
        game.spell_x = 7;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(7, false, 3) && hit_points(f) == 0xf0 - 20 && hit_points(m) == 0xf0 - 40 &&
            hit_points(c) == 0xf0)
            break;
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();
}

/* Open ground (var 0x22 0): a wall never bounces; the walk goes on through it. The spell's foe F at 7, 10, the wall at 9, 10,
 * the foe G at 10, 10 beyond it: G is hit, the caster at 5, 10 is not. */
static void test_band_b3_open(void)
{
    cok_character *c = NULL, *f = NULL, *g = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
    CHECK(cok_ecl_load_var(&game.vm, 0x22) == 0);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        f = bolt_foe(7, 10, 30);
        g = bolt_foe(10, 10, 30);
        game.combat.cells[10][9] = 1;
        game.spell_x = 7;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(7, false, 2) && hit_points(f) == 0xf0 - 20 && hit_points(g) == 0xf0 - 20 &&
            hit_points(c) == 0xf0)
            break;
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();
}

/* Off the map: the walk from 48, 10 reaches 49 (F, hit), then 50 is off the map (no combatant, terrain 0): the end steps back
 * to 49 (rem 0), and the cell is hit again (the last occupant F, the off-map cell's 0 as the occupant now). No bounce, even
 * with var 0x22 1 and the caster at 47, 10 behind the spell in line. */
static void test_band_b3_offmap(void)
{
    cok_character *c = NULL, *f = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(47, 10);
        f = bolt_foe(49, 10, 30);
        game.spell_x = 48;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(3, false, 2) && hit_points(f) == 0xf0 - 40 && hit_points(c) == 0xf0) break;
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();
    /* Off the bottom edge on a diagonal step the walk ends there (terrain 0: rem 0), though the line's next step would come
     * back on the map: from the spell's (empty) cell 10, 23 away from the caster at 8, 22 the walk is 11, 24, 12, 24, then
     * 13, 25 off the map (stepped back to 12, 24); G at 13, 24, the step after it, is not hit. */
    cok_character *g = NULL;
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(8, 22);
        g = bolt_foe(13, 24, 30);
        game.spell_x = 10;
        game.spell_y = 23;
        game.vm.seed = start;
        if (bolt_run(7, false, 0) && hit_points(g) == 0xf0 && hit_points(c) == 0xf0) break;
    }
    CHECK(start == 1 && game.vm.status == COK_ECL_OK);
    battle_clear();
}

/* The own-cell aim: the spell's cell is the caster's (5, 10). The hit on the caster (type 4, throw 30), then the zero-length
 * line: its segments walk nothing (len 3: three, each charged 2), and the first compares the caster with the occupant
 * never read, 0 (measured: the poses before the handler leave it), so the caster is hit again; after that last is 0 too,
 * so no more hits. The caster twice, no stop. */
static void test_band_b3_own_cell(void)
{
    cok_character *c = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        game.spell_x = 5;
        game.spell_y = 10;
        game.vm.seed = start;
        if (bolt_run(3, false, 2) && hit_points(c) == 0xf0 - 40) break;
    }
    CHECK(start < BOLT_SEEDS && game.vm.status == COK_ECL_OK);
    battle_clear();
}

/* Lightning Bolt (0x33) dry: the caster level's d6 first (the sum is the damage, rolls.dice its count), then the bolt (its
 * own damage type 0x0c: the caster's battle leaves it 0). On the open ground the walk of len 7 from the foe F at the spell's
 * cell 7, 10 reaches 14, 10 (14 half-cells: G hit) and not 15, 10 (H). With var 0x22 1 and a wall at 9, 10 (the caster at
 * 5, 10) flag doubles the walk back: F is hit again and the bolt ends (test_band_b3_bounce's), the caster untouched. The
 * search needs the saves to roll no 20 after the dice. */
static void test_band_b3_lightning(void)
{
    cok_character *c = bolt_caster(5, 10);
    uint8_t lv = 0;
    CHECK(queen_spell_level(&game, 0x33, false, &lv) && lv > 0);
    battle_clear();
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        cok_character *f = bolt_foe(7, 10, 30), *g = bolt_foe(14, 10, 30), *h = bolt_foe(15, 10, 30);
        game.effects.rolls.damage_type = 0;
        game.spell_x = 7;
        game.spell_y = 10;
        game.effects.rolls.spell = 0x33;
        uint32_t w = start;
        uint8_t sum = cok_dice(&w, lv, 6);
        game.vm.seed = start;
        log_text[0] = '\0';
        if (run_handler(0x33) && hit_points(f) == (uint8_t)(0xf0 - sum) && hit_points(g) == (uint8_t)(0xf0 - sum)) {
            CHECK(game.effects.rolls.dice == lv && game.effects.rolls.damage_type == 0x0c);
            CHECK(LOGGED("from Electricity") && hit_points(c) == 0xf0 && hit_points(h) == 0xf0);
            break;
        }
    }
    CHECK(start < BOLT_SEEDS);
    battle_clear();
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        cok_character *f = bolt_foe(7, 10, 30);
        game.combat.cells[10][9] = 1;
        game.spell_x = 7;
        game.spell_y = 10;
        game.effects.rolls.spell = 0x33;
        uint32_t w = start;
        uint8_t sum = cok_dice(&w, lv, 6);
        game.vm.seed = start;
        log_text[0] = '\0';
        if (run_handler(0x33) && hit_points(f) == (uint8_t)(0xf0 - 2 * sum)) {
            CHECK(hit_points(c) == 0xf0);
            break;
        }
    }
    CHECK(start < BOLT_SEEDS);
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
    battle_clear();
}

/* Lightning Bolt underwater (var 0x3c): the Fireball family's area (queen_fireball, radius 2) with the spell kept at 0x33: the
 * level's d6 (the damage, type 9 "from Fire"), the foe at the spell's cell takes it, and the caster 5 away is not listed. */
static void test_band_b3_lightning_wet(void)
{
    cok_character *c = bolt_caster(5, 10);
    uint8_t lv = 0;
    CHECK(queen_spell_level(&game, 0x33, false, &lv) && lv > 0);
    battle_clear();
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    uint32_t start;
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        cok_character *f = bolt_foe(6, 5, 30);
        cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
        game.spell_x = 6;
        game.spell_y = 5;
        game.effects.rolls.spell = 0x33;
        uint32_t w = start;
        uint8_t sum = cok_dice(&w, lv, 6);
        game.vm.seed = start;
        log_text[0] = '\0';
        if (run_handler(0x33) && hit_points(f) == (uint8_t)(0xf0 - sum)) {
            CHECK(game.effects.rolls.spell == 0x33 && LOGGED("from Fire") && hit_points(c) == 0xf0);
            break;
        }
    }
    CHECK(start < BOLT_SEEDS);
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    battle_clear();
}

/* Javelin of Lightning (0x81): no dice, 20 points of electricity (its own type 0x0c: the battle leaves it 0), a bolt of 3
 * with no flag. On the open ground the walk from the foe F at the spell's cell 7, 10 reaches 10, 10 (6 half-cells: G hit)
 * and not 11, 10 (H). With var 0x22 1, a foe M at 8, 10 and a wall at 9, 10 (test_band_b3_wrap's): M (charge 2), the wall
 * (2, the rest 2) bounces undoubled, and the new line's first segment hits M again (its charge wraps: the end). (flag
 * would double the line back to 8: charge 6, the end at the wall, M hit once.) */
static void test_band_b3_javelin(void)
{
    cok_character *c = NULL, *f = NULL, *g = NULL, *h = NULL, *m = NULL;
    uint32_t start;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        f = bolt_foe(7, 10, 30);
        g = bolt_foe(10, 10, 30);
        h = bolt_foe(11, 10, 30);
        game.effects.rolls.damage_type = 0;
        game.spell_x = 7;
        game.spell_y = 10;
        game.vm.seed = start;
        log_text[0] = '\0';
        if (run_handler(0x81) && bolt_logged(20) == 2 && hit_points(f) == 0xf0 - 20 && hit_points(g) == 0xf0 - 20) break;
    }
    CHECK(start < BOLT_SEEDS && game.effects.rolls.damage_type == 0x0c && LOGGED("from Electricity"));
    CHECK(hit_points(c) == 0xf0 && hit_points(h) == 0xf0);
    battle_clear();
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    for (start = 1; start < BOLT_SEEDS; ++start) {
        c = bolt_caster(5, 10);
        f = bolt_foe(7, 10, 30);
        m = bolt_foe(8, 10, 30);
        game.combat.cells[10][9] = 1;
        game.spell_x = 7;
        game.spell_y = 10;
        game.vm.seed = start;
        log_text[0] = '\0';
        if (run_handler(0x81) && bolt_logged(20) == 3 && hit_points(f) == 0xf0 - 20 && hit_points(m) == 0xf0 - 40 &&
            hit_points(c) == 0xf0)
            break;
    }
    CHECK(start < BOLT_SEEDS);
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
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

/* The nth cell (1 on) of the line from x0, y0 to x1, y1 (cok_combat_line). */
static void line_cell(int8_t x0, int8_t y0, int8_t x1, int8_t y1, unsigned nth, int8_t *x, int8_t *y)
{
    cok_combat_line line = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
    cok_combat_line_start(&line);
    for (unsigned i = 0; i < nth; ++i) CHECK(cok_combat_line_step(&line, R));
    *x = (int8_t)line.x;
    *y = (int8_t)line.y;
}

/* effects-5: the timers' helpers. */
static unsigned effects_of(const cok_character *c, uint8_t id)
{
    unsigned n = 0;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next)
        if (e->id == id) ++n;
    return n;
}

static unsigned effects_with(const cok_character *c, uint8_t id, uint8_t value)
{
    unsigned n = 0;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next)
        if (e->id == id && e->value == value) ++n;
    return n;
}

/* Whether every id of c with value has flag 0 (no handler on its removal), and there is one. */
static bool held_for_good(const cok_character *c, uint8_t id, uint8_t value)
{
    unsigned n = 0;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next)
        if (e->id == id && e->value == value) {
            if (e->on_remove) return false;
            ++n;
        }
    return n > 0;
}

static uint8_t strength_of(const cok_character *c)
{
    return cok_field_u8_at(R->character, c->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH);
}

static uint8_t hit_points_of(const cok_character *c)
{
    return cok_field_u8(R->character, c->record, COK_FIELD_HIT_POINTS);
}

/* Removes every effect id of c with curing set, so that a timer adds nothing back. */
static void clear_effect(cok_character *c, uint8_t id)
{
    cok_effect *e;
    game.effects.rolls.curing = 1;
    while ((e = cok_character_find_effect(c, id)) != NULL)
        CHECK(cok_effects_remove(&game.effects, c, e, id) && game.vm.status == COK_ECL_OK);
    game.effects.rolls.curing = 0;
}

/* 78e4:023d (0x0f, poison; either flag): removed, it adds 0x1a for ten minutes with its value (the original's
 * re-add of 0x1a, not 0x0f) and takes a point ("takes 1 point of damage ") from a character with more than one hit
 * point; run (flag 0) the same; at one hit point, the 0x1a only; with rolls.curing set, nothing is added and nothing
 * hurts. Outside a battle, the damage's cited stop (148f:2214) stops the run; no node is a stop too. */
static void test_effects5_poison(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 10);
    cok_effect *e = cok_character_add_effect(t, 0x0f, 0, 0x05, true);
    CHECK(e != NULL);
    log_text[0] = '\0';
    game.effects.rolls.damage_type = 8; /* the timer's damage is of type 0 */
    CHECK(cok_effects_remove(&game.effects, t, e, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.damage_type == 0);
    cok_effect *p = cok_character_find_effect(t, 0x1a);
    CHECK(p != NULL && p->duration == 10 && p->value == 0x05 && p->on_remove);
    CHECK(effects_of(t, 0x0f) == 0 && hit_points_of(t) == 9 && LOGGED("takes 1 point of damage "));
    clear_effect(t, 0x1a);
    /* Run (flag 0) from a node of value 0x05: the same, 9 -> 8. */
    e = cok_character_add_effect(t, 0x0f, 0, 0x05, true);
    CHECK(cok_effects_run(&game.effects, t, 0x0f, e) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x1a) != NULL && hit_points_of(t) == 8);
    clear_effect(t, 0x1a);
    clear_effect(t, 0x0f);
    /* At two hit points: the point (more than one, unsigned). */
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 2);
    e = cok_character_add_effect(t, 0x0f, 0, 0x05, true);
    CHECK(cok_effects_remove(&game.effects, t, e, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(hit_points_of(t) == 1);
    clear_effect(t, 0x1a);
    /* At one hit point: the 0x1a, no damage. */
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    e = cok_character_add_effect(t, 0x0f, 0, 0x05, true);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x0f) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x1a) != NULL && hit_points_of(t) == 1 && !LOGGED("takes"));
    clear_effect(t, 0x1a);
    /* Curing: nothing added, no damage. */
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 10);
    game.effects.rolls.curing = 1;
    e = cok_character_add_effect(t, 0x0f, 0, 0x05, true);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x0f) && game.vm.status == COK_ECL_OK);
    game.effects.rolls.curing = 0;
    CHECK(cok_character_find_effect(t, 0x1a) == NULL && hit_points_of(t) == 10 && !LOGGED("takes"));
    /* Outside a battle: the damage's stop. */
    game.vm.mode = 2;
    e = cok_character_add_effect(t, 0x0f, 0, 0x05, true);
    CHECK(!cok_effects_remove(&game.effects, t, e, 0x0f) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.effects.error, "dqk 148f:2214 outside combat") != NULL);
    game.vm.mode = 5;
    game.vm.status = COK_ECL_OK;
    game.error[0] = game.effects.error[0] = '\0';
    /* No node: the stop that reads the node's value (0000:0003). */
    CHECK(!cok_effects_run(&game.effects, t, 0x0f, NULL) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.effects.error, "dqk 78e4:023d") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = game.effects.error[0] = '\0';
    clear_effect(t, 0x1a);
    clear_effect(t, 0x0f);
    battle_clear();
}

/* 78e4:08ba (0x2b, strength drain; either flag): again for an hour with its value (flag 1); a strength above 3 "is
 * weakened" (row 10) and loses a point; a strength of 3 or less brings 0x1f for good (flag 0, 0xff), and with a 0x1f
 * already held there are two (no holds-it test). Run (flag 0) from its node, the same. With no node, a cited stop. */
static void test_effects5_drain(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8_at(R->character, t->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 10);
    cok_effect *e = cok_character_add_effect(t, 0x2b, 0, 0x05, true);
    CHECK(e != NULL);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x2b) && game.vm.status == COK_ECL_OK);
    cok_effect *d = cok_character_find_effect(t, 0x2b);
    CHECK(d != NULL && d->duration == 60 && d->value == 0x05 && d->on_remove);
    CHECK(LOGGED("is weakened") && strength_of(t) == 9 && effects_of(t, 0x1f) == 0);
    /* Run (flag 0) from the node: 9 -> 8, a second 0x2b. */
    log_text[0] = '\0';
    CHECK(cok_effects_run(&game.effects, t, 0x2b, d) && game.vm.status == COK_ECL_OK);
    CHECK(strength_of(t) == 8 && effects_of(t, 0x2b) == 2 && LOGGED("is weakened"));
    clear_effect(t, 0x2b);
    /* Strength 4: still weakened, 4 -> 3. */
    cok_field_set8_at(R->character, t->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 4);
    e = cok_character_add_effect(t, 0x2b, 0, 0x05, true);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x2b) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("is weakened") && strength_of(t) == 3 && effects_of(t, 0x1f) == 0);
    clear_effect(t, 0x2b);
    /* Strength 3: 0x1f for good, even with one held (two then). */
    cok_field_set8_at(R->character, t->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 3);
    CHECK(cok_character_add_effect(t, 0x1f, 0, 0, false) != NULL);
    e = cok_character_add_effect(t, 0x2b, 0, 0x05, true);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x2b) && game.vm.status == COK_ECL_OK);
    CHECK(!LOGGED("is weakened") && strength_of(t) == 3);
    CHECK(effects_of(t, 0x1f) == 2 && effects_with(t, 0x1f, 0xff) == 1 && held_for_good(t, 0x1f, 0xff));
    CHECK(cok_character_find_effect(t, 0x2b) != NULL);
    clear_effect(t, 0x2b);
    /* Outside a battle the say (177e:156e in the camp's shape) is a cited stop; the 0x2b is added first. */
    game.vm.mode = 2;
    cok_field_set8_at(R->character, t->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 10);
    e = cok_character_add_effect(t, 0x2b, 0, 0x05, true);
    log_text[0] = '\0';
    CHECK(!cok_effects_remove(&game.effects, t, e, 0x2b) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "dqk 177e:156e outside combat") != NULL && !LOGGED("is weakened"));
    CHECK(strength_of(t) == 10);
    game.vm.mode = 5;
    game.vm.status = COK_ECL_OK;
    game.error[0] = game.effects.error[0] = '\0';
    clear_effect(t, 0x2b);
    /* No node: the stop that reads the node's value. */
    CHECK(!cok_effects_run(&game.effects, t, 0x2b, NULL) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.effects.error, "dqk 78e4:08ba") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = game.effects.error[0] = '\0';
    battle_clear();
}

/* 78e4:0920 (0x2c, wasting; either flag): again for ten minutes with its value (flag 1); a character with more than one
 * hit point takes a point ("takes 1 point of damage "); at one hit point or none, 0x1f for good (flag 0, 0xff), with
 * no test for one held (two then). */
static void test_effects5_wasting(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 10);
    cok_effect *e = cok_character_add_effect(t, 0x2c, 0, 0x05, true);
    CHECK(e != NULL);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x2c) && game.vm.status == COK_ECL_OK);
    cok_effect *d = cok_character_find_effect(t, 0x2c);
    CHECK(d != NULL && d->duration == 10 && d->value == 0x05 && d->on_remove);
    CHECK(hit_points_of(t) == 9 && LOGGED("takes 1 point of damage ") && effects_of(t, 0x1f) == 0);
    clear_effect(t, 0x2c);
    /* At two hit points: the point. */
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 2);
    e = cok_character_add_effect(t, 0x2c, 0, 0x05, true);
    CHECK(cok_effects_remove(&game.effects, t, e, 0x2c) && game.vm.status == COK_ECL_OK);
    CHECK(hit_points_of(t) == 1 && effects_of(t, 0x1f) == 0);
    clear_effect(t, 0x2c);
    /* At one hit point, with a 0x1f held: no damage, the 0x2c again, a second 0x1f. */
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 1);
    CHECK(cok_character_add_effect(t, 0x1f, 0, 0, false) != NULL);
    e = cok_character_add_effect(t, 0x2c, 0, 0x05, true);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x2c) && game.vm.status == COK_ECL_OK);
    CHECK(hit_points_of(t) == 1 && !LOGGED("takes") && cok_character_find_effect(t, 0x2c) != NULL);
    CHECK(effects_of(t, 0x1f) == 2 && effects_with(t, 0x1f, 0xff) == 1 && held_for_good(t, 0x1f, 0xff));
    clear_effect(t, 0x2c);
    battle_clear();
}

/* 78e4:0696 (0x22, cause disease; applied_only): run (flag 0) it runs 0x2b and 0x2c with its value: the strength drops
 * a point ("is weakened") and a point is taken (ten-minute wasting). Removed (flag 1), nothing. */
static void test_effects5_disease(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 10);
    cok_field_set8_at(R->character, t->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 10);
    cok_effect *e = cok_character_add_effect(t, 0x22, 0, 0x05, false);
    CHECK(e != NULL);
    log_text[0] = '\0';
    CHECK(cok_effects_run(&game.effects, t, 0x22, e) && game.vm.status == COK_ECL_OK);
    cok_effect *drain = cok_character_find_effect(t, 0x2b), *wasting = cok_character_find_effect(t, 0x2c);
    CHECK(drain != NULL && drain->duration == 60 && drain->value == 0x05);
    CHECK(wasting != NULL && wasting->duration == 10 && wasting->value == 0x05);
    CHECK(strength_of(t) == 9 && hit_points_of(t) == 9 && LOGGED("is weakened") && LOGGED("takes 1 point of damage "));
    /* 0x2b first: its say before the damage, its node before the 0x2c's. */
    CHECK(strstr(log_text, "is weakened") < strstr(log_text, "takes 1 point of damage "));
    const cok_effect *first = t->effects;
    while (first != NULL && first->id != 0x2b && first->id != 0x2c) first = first->next;
    CHECK(first != NULL && first->id == 0x2b);
    clear_effect(t, 0x2b);
    clear_effect(t, 0x2c);
    /* Removed (its node with flag 1): nothing, the strength and the hit points kept. */
    e = cok_character_add_effect(t, 0x22, 0, 0x05, true);
    CHECK(e != NULL);
    cok_field_set8_at(R->character, t->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 10);
    cok_field_set8(R->character, t->record, COK_FIELD_HIT_POINTS, 10);
    log_text[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, t, e, 0x22) && game.vm.status == COK_ECL_OK);
    CHECK(effects_of(t, 0x2b) == 0 && effects_of(t, 0x2c) == 0 && strength_of(t) == 10 && hit_points_of(t) == 10);
    CHECK(!LOGGED("is weakened") && !LOGGED("takes"));
    battle_clear();
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
    /* The walk takes the first line's steps in turn, from its first again after its last: from 10, 10 at 12, 11 (a
     * diagonal step, then a straight one: the walk 5) with len 6, the steps 3, 2, 3 (8, 10, 13): the end 15, 13.
     * Its first step alone would end at 15, 14, its last alone at 16, 11. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    t = cone_target('T', 15, 13);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 12, 11, 1, 6) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t);
    /* The side is the direction of the step that met the last occupant, not the line's last step: from 10, 10 at
     * 13, 9 with len 4 the end is 14, 9 and the line's cells 11, 10 (east), 12, 9 (north-east), 13, 9, 14, 9 (east).
     * O at 12, 9: the side 1, the right end 15, 9 (Q); east's right end 14, 10 (S) is not reached. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    p = cone_target('O', 12, 9);
    z = cone_target('Q', 15, 9);
    w = cone_target('S', 14, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 13, 9, 2, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 2 && game.spell_targets[1] == p && game.spell_targets[2] == z && w != NULL);
    /* The caster is never a target: she stands on the centre line at 12, 10 and is listed, then left out. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 12, 10);
    p = cone_target('P', 14, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 11, 10, 1, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == p);
    /* No one on the centre line (from 10, 10 at 11, 10 with len 4: the end 14, 10): the side 0xff (849a:1c4a reads a
     * local the line never wrote: the high byte of the caller's BP), the sides at DS:8dc7 on: the right end 14 + 0x53,
     * 10 + 0x43 clamped to 49, 24, the left 14 + 0x63, 10 clamped to 49, 10. R on the right line's third cell, L at
     * 20, 10 on the left line; D at 14, 9 (east's left side) on neither. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    int8_t rx, ry;
    line_cell(10, 10, 49, 24, 3, &rx, &ry);
    CHECK(ry != 10);
    t = cone_target('R', rx, ry);
    u = cone_target('L', 20, 10);
    v = cone_target('D', 14, 9);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 10, 10, 11, 10, 2, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t);
    CHECK(queen_cone(&game, 10, 10, 11, 10, 3, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 2 && game.spell_targets[1] == t && game.spell_targets[2] == u && v != NULL);
    /* From x 45 on the right end's x wraps (45 + 0x53 is -128 as a byte) and is clamped to 0: from 44, 10 at 45, 10
     * with len 1 (no walk on) the right end is 0, 24. */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 44, 10);
    line_cell(44, 10, 0, 24, 3, &rx, &ry);
    t = cone_target('R', rx, ry);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    CHECK(queen_cone(&game, 44, 10, 45, 10, 2, 1) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 1 && game.spell_targets[1] == t);
    /* The stop: aimed at the caster's own cell (the walk reads the directions of a line with no steps: 849a:1a9d). */
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    game.vm.character = c->record;
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    CHECK(!queen_cone(&game, 10, 10, 10, 10, 1, 4) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "849a:1a9d") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.character = NULL;
    battle_clear();
}

/* Cone of Cold (dqk 7fd8:299c) for the caster's mage level lv (class 5, no magic order: the level is lv): a cone of
 * width 2 and length (lv + 1) / 2, at least 1, from the caster at 5, 5 to the spell's cell 6, 5. The target at 6, 5
 * is on the centre line for every length (so the side is set); the targets at 7, 5 and 10, 5 are reached only by
 * length 5 (the walk 2 to 10; length 2 reaches 7, 5), the one at 11, 5 by none. Then lv d4 + lv of cold, "from Cold", half on a save. The first saving throw after the dice is the
 * first target's (queen_save: a roll of 1 fails, a 20 saves, else the throw at most the roll): saving throw 1 saves
 * on any roll but 1, 21 saves on a 20 only. */
static void test_cone_of_cold(void)
{
    cok_character *a, *t, *u, *n, *f;
    const unsigned levels[3] = {1, 2, 9};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned save = 0; save < 2; ++save) {
            duel_at(&a, &t, 6, 5);
            u = cone_target('U', 10, 5);
            n = cone_target('N', 7, 5);
            f = cone_target('F', 11, 5);
            cok_field_set8(R->character, u->record, COK_FIELD_ACTIVE, 1);
            cok_field_set8(R->character, n->record, COK_FIELD_ACTIVE, 1);
            cok_field_set8(R->character, f->record, COK_FIELD_ACTIVE, 1);
            game.vm.mode = 5;
            set_class(a, 5, (uint8_t)levels[i]);
            a->record[0x19d] = 0;
            game.vm.character = a->record;
            game.spell_x = 6;
            game.spell_y = 5;
            game.effects.rolls.spell = 0x5c;
            cok_character *both[4] = {t, u, n, f};
            for (unsigned j = 0; j < 4; ++j) {
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
            /* Only length 5 (lv 9) reaches the targets at 7, 5 and 10, 5, whose own saves are later rolls: full or
             * half; none reaches 11, 5. */
            cok_character *reached[2] = {n, u};
            for (unsigned j = 0; j < 2; ++j) {
                uint8_t hp = cok_field_u8(R->character, reached[j]->record, COK_FIELD_HIT_POINTS);
                if (levels[i] == 9)
                    CHECK(hp == 0x4e - amount || hp == 0x4e - amount / 2u);
                else
                    CHECK(hp == 0x4e);
            }
            CHECK(cok_field_u8(R->character, f->record, COK_FIELD_HIT_POINTS) == 0x4e);
            game.vm.character = NULL;
            battle_clear();
        }
    }
}

/* The length of the line from x0, y0 to x1, y1 (every step of cok_combat_line). */
static unsigned line_length(int8_t x0, int8_t y0, int8_t x1, int8_t y1)
{
    cok_combat_line line = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
    cok_combat_line_start(&line);
    while (cok_combat_line_step(&line, R)) {
    }
    return line.length;
}

/* Meteor Swarm's tries (dqk 7fd8:3a73, queen_meteor_try) on the made-up map: the caster at 10, 10 aims at 20, 12. The
 * steps of meteor m are {-1, 1, -1, 1} and {-1, -1, 1, 1} (DS:8788, DS:8790). Try 1 is the diagonal neighbour, try 2 the
 * aimed x with my kept, try 3 the neighbour's x with the aimed y, try 4 the aimed x with my kept. */
static void test_meteor_try(void)
{
    static const int8_t dx[4] = {-1, 1, -1, 1}, dy[4] = {-1, -1, 1, 1};
    cok_combat *k = &game.combat;
    cok_character *c;
    int8_t mx = 0, my = 0;
    uint16_t range = 0;
    bool seen = false;
    battle_clear();
    c = record('C', 0, 0);
    place(c, 10, 10);
    game.vm.character = c->record;
    for (unsigned m = 1; m <= 4; ++m) {
        CHECK(queen_meteor_try(&game, 10, 10, 20, 12, 1, m, &mx, &my, &range, &seen));
        CHECK(mx == 20 + dx[m - 1] && my == 12 + dy[m - 1] && seen);
        CHECK(range == line_length(10, 10, mx, my) / 2);
        int8_t first_y = my;
        CHECK(queen_meteor_try(&game, 10, 10, 20, 12, 2, m, &mx, &my, &range, &seen));
        CHECK(mx == 20 && my == first_y && seen && range == line_length(10, 10, 20, first_y) / 2);
        CHECK(queen_meteor_try(&game, 10, 10, 20, 12, 3, m, &mx, &my, &range, &seen));
        CHECK(mx == 20 + dx[m - 1] && my == 12 && seen);
        CHECK(queen_meteor_try(&game, 10, 10, 20, 12, 4, m, &mx, &my, &range, &seen));
        CHECK(mx == 20 && my == 12 && seen);
    }
    /* Try 2 keeps the y it is given (a mutant's my = ty would give 12). */
    my = 7;
    CHECK(queen_meteor_try(&game, 10, 10, 20, 12, 2, 1, &mx, &my, &range, &seen));
    CHECK(mx == 20 && my == 7 && seen);
    /* So does try 4 (as after a try 3 whose sight pulled the y back; a mutant's my = ty would give 12). */
    my = 7;
    CHECK(queen_meteor_try(&game, 10, 10, 20, 12, 4, 1, &mx, &my, &range, &seen));
    CHECK(mx == 20 && my == 7 && seen);
    /* The map's edges are on it (x 0 and 0x31, y 0 and 0x18): tried, and seen across the floor. */
    my = 12;
    CHECK(queen_meteor_try(&game, 10, 10, 0, 12, 2, 1, &mx, &my, &range, &seen) && mx == 0 && my == 12 && seen);
    CHECK(queen_meteor_try(&game, 10, 10, 0x31, 12, 2, 1, &mx, &my, &range, &seen) && mx == 0x31 && seen);
    CHECK(queen_meteor_try(&game, 10, 10, 20, 0, 3, 1, &mx, &my, &range, &seen) && mx == 19 && my == 0 && seen);
    CHECK(queen_meteor_try(&game, 10, 10, 20, 0x18, 3, 1, &mx, &my, &range, &seen) && my == 0x18 && seen);
    /* A cell off the map stops the try: no sight, the range 0xff halved to 0x7f, the cell kept (x -1 from 0; y -1). */
    CHECK(queen_meteor_try(&game, 10, 10, 0, 5, 1, 1, &mx, &my, &range, &seen));
    CHECK(mx == -1 && my == 4 && !seen && range == 0x7f);
    CHECK(queen_meteor_try(&game, 10, 10, 0, 0, 1, 2, &mx, &my, &range, &seen));
    CHECK(mx == 1 && my == -1 && !seen && range == 0x7f);
    /* A wall at 11, 10 stops the sight at its own cell: try 2 at 13, 10 with my 10 is pulled to 11, 10 (length 2, halved). */
    k->cells[10][11] = 1;
    my = 10;
    CHECK(queen_meteor_try(&game, 10, 10, 13, 12, 2, 1, &mx, &my, &range, &seen));
    CHECK(mx == 11 && my == 10 && !seen && range == 1);
    k->cells[10][11] = 0;
    /* The own cell at length 0: the sight gives range 0 there, so the cone's walk never runs (849a:1a9d is not reached). */
    CHECK(queen_cone(&game, 10, 10, 10, 10, 1, 0) && game.vm.status == COK_ECL_OK);
    CHECK(game.spell_target_count == 0);
    game.vm.character = NULL;
    battle_clear();
}

/* Meteor Swarm's fixture: the caster C at x, y (hit points 0xff, its record active), the spell's cell 20, 12 (the four
 * meteors land at 19, 11; 21, 11; 19, 13; 21, 13: each try 1 is seen across the open floor). */
static cok_character *meteor_field(int8_t x, int8_t y)
{
    battle_clear();
    cok_character *c = record('C', 0, 0);
    place(c, x, y);
    set_hit_points(c, 0xff, 0xff);
    cok_field_set8(R->character, c->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(R->character, c->record, COK_FIELD_ICON, ICON_SLOT);
    game.vm.mode = 5;
    game.vm.character = c->record;
    game.spell_x = 20;
    game.spell_y = 12;
    game.effects.rolls.spell = 0x7c;
    game.effects.rolls.images = 0;
    return c;
}

/* A hostile target at x, y: hit points 0xff, active or not, saving throw 4 of 21 (saved on a 20 only: the cone's test). */
static cok_character *meteor_target(char name, int8_t x, int8_t y, bool active)
{
    cok_character *t = record(name, 1, 0);
    place(t, x, y);
    set_hit_points(t, 0xff, 0xff);
    cok_field_set8(R->character, t->record, COK_FIELD_ACTIVE, active ? 1 : 0);
    cok_field_set8(R->character, t->record, COK_FIELD_ICON, ICON_SLOT);
    cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, 4, 21);
    return t;
}

/* The handler from seed: the final seed (the status and the bar's text checked by the caller). */
static uint32_t meteor_run(uint32_t seed)
{
    game.vm.seed = seed;
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    log_text[0] = '\0';
    CHECK(run_handler(0x7c));
    return game.vm.seed;
}

/* The draws of one hit: a saving throw's d20 first when in the area (the save before the dice), then 10d4 and the flash's
 * four draws. */
static void meteor_hit(uint32_t *s, bool area)
{
    if (area) cok_dice(s, 1, 20);
    cok_dice(s, 10, 4);
    flash_draws(s, 4);
}

/* Whether a and b are within one cell of each other. */
static bool within_one(int a, int b)
{
    return a - b <= 1 && b - a <= 1;
}

/* The hits of the four meteors on the fixture's map, counted as the handler lists them: each active target on a meteor's
 * line (the caster out, once each) takes a hit with no save, and each active combatant within 1 of the meteor's cell (the
 * caster in) takes a hit with a save. The draws are one stream, so the hits' draws advance the seed in any order. Returns
 * the number of hits in *hits. */
static void meteor_model(cok_character *c, uint32_t *s, unsigned *hits)
{
    static const int8_t dx[4] = {-1, 1, -1, 1}, dy[4] = {-1, -1, 1, 1};
    cok_combat *k = &game.combat;
    int8_t cx = cok_combat_x(k, c->record), cy = cok_combat_y(k, c->record);
    *hits = 0;
    for (unsigned m = 1; m <= 4; ++m) {
        int8_t mx = (int8_t)(20 + dx[m - 1]), my = (int8_t)(12 + dy[m - 1]);
        cok_character *on_line[30];
        unsigned n = 0;
        cok_combat_line line = {.x0 = cx, .y0 = cy, .x1 = mx, .y1 = my};
        cok_combat_line_start(&line);
        while (cok_combat_line_step(&line, R)) {
            uint8_t occ, terrain;
            cok_combat_cell(k, (int8_t)line.x, (int8_t)line.y, &occ, &terrain);
            if (occ == 0 || k->combatant[occ].character == c) continue;
            cok_character *t = k->combatant[occ].character;
            bool again = false;
            for (unsigned j = 0; j < n; ++j) again |= on_line[j] == t;
            if (!again) on_line[n++] = t;
        }
        for (unsigned j = 0; j < n; ++j)
            if (cok_field_u8(R->character, on_line[j]->record, COK_FIELD_ACTIVE) != 0) {
                meteor_hit(s, false);
                ++*hits;
            }
        for (unsigned i = 1; i <= k->count; ++i) {
            cok_character *t = k->combatant[i].character;
            if (t == NULL || !within_one(cok_combat_x(k, t->record), mx) ||
                !within_one(cok_combat_y(k, t->record), my))
                continue;
            if (cok_field_u8(R->character, t->record, COK_FIELD_ACTIVE) != 0) {
                meteor_hit(s, true);
                ++*hits;
            }
        }
    }
}

/* The seed after the meteors' arena alone: the caster at x, y, inactive (no one is hit), and no target. The view's first run
 * differs from the later ones (the centring), so the run is made twice, and the second is the baseline. */
static uint32_t meteor_base(uint32_t seed, int8_t x, int8_t y)
{
    for (unsigned k = 0; k < 2; ++k) {
        cok_character *c = meteor_field(x, y);
        cok_field_set8(R->character, c->record, COK_FIELD_ACTIVE, 0);
        meteor_run(seed);
        battle_clear();
    }
    cok_character *c = meteor_field(x, y);
    cok_field_set8(R->character, c->record, COK_FIELD_ACTIVE, 0);
    uint32_t base = meteor_run(seed);
    battle_clear();
    return base;
}

/* The first facing whose quarter holds x1, y1 from x0, y0 (5d71:048f). */
static uint8_t meteor_facing(int8_t x0, int8_t y0, int8_t x1, int8_t y1)
{
    uint8_t dir = 0;
    bool in = false;
    for (;;) {
        CHECK(dir < 8 && cok_combat_in_arc(x0, y0, x1, y1, dir, &in));
        if (in) return dir;
        ++dir;
    }
}

/* The four meteors' arena from the caster c at its cell, made of the shared calls with the original's arguments (7fd8:3b99:
 * 09ab(rec, dir, 1, 0), 0053(0x20), 0fc8(dir, 0x20), 00df(cx, cy, mx, my, 1, 0x1e), the cell, 0def(1, 0x19, 0x0c), then
 * 0626 and the poses), from seed; the meteors at 19, 11; 21, 11; 19, 13; 21, 13. Returns the seed after it. */
static uint32_t meteor_arena(cok_character *c, uint32_t seed)
{
    static const int8_t dx[4] = {-1, 1, -1, 1}, dy[4] = {-1, -1, 1, 1};
    cok_combat *k = &game.combat;
    int8_t cx = cok_combat_x(k, c->record), cy = cok_combat_y(k, c->record);
    game.vm.seed = seed;
    for (unsigned m = 1; m <= 4; ++m) {
        int8_t mx = (int8_t)(20 + dx[m - 1]), my = (int8_t)(12 + dy[m - 1]);
        uint8_t dir = meteor_facing(cx, cy, mx, my);
        k->show_actions = true;
        CHECK(cok_arena_pose(&game, c, dir, 1, false));
        CHECK(cok_arena_missile_frames(&game, 0x20) && queen_direction_picture(&game, dir, 0x20));
        CHECK(cok_arena_missile(&game, cx, cy, mx, my, 1, 0x1e));
        game.spell_x = mx;
        game.spell_y = my;
        CHECK(queen_burst(&game, 1, 0x19, 0x0c));
        bool shown = false;
        CHECK(cok_combat_visible(k, R, c->record, false, &shown));
        if (shown) {
            uint8_t facing = c->combat->facing;
            CHECK(cok_arena_pose(&game, c, facing, 1, true) && cok_arena_pose(&game, c, facing, 0, false));
        }
    }
    return game.vm.seed;
}

static void test_meteor_swarm(void)
{
    const uint32_t seed = 1234;
    uint32_t base, want, ran;
    unsigned hits = 0;
    cok_character *c, *t4, *l, *a, *inactive;
    /* Underwater the bar says so, its one draw (the bar's wait, as the Fireball's underwater test) and no meteor: no image,
     * the damage type left as it was. */
    c = meteor_field(10, 10);
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    game.vm.seed = seed;
    game.effects.rolls.damage_type = 0;
    uint32_t bar = seed;
    cok_tp_random(&bar, 1000);
    CHECK(run_handler(0x7c) && game.vm.seed == bar && game.effects.rolls.images == 0);
    CHECK(game.effects.rolls.damage_type == 0 && LOGGED("That has no effect underwater!"));
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    battle_clear();

    /* The caster alone at 10, 10: the four meteors' arena (the poses, the missiles, the bursts), and the image and type. */
    base = meteor_base(seed, 10, 10);
    c = meteor_field(10, 10);
    ran = meteor_run(seed);
    CHECK(ran == base && game.effects.rolls.images == 1 && game.effects.rolls.damage_type == 9);
    /* The spell's cell is the fourth meteor's, and the caster faces it (the pose before its missile). */
    CHECK(game.spell_x == 21 && game.spell_y == 13 && c->combat->facing == meteor_facing(10, 10, 21, 13));
    battle_clear();
    /* That arena's draws are the shared calls' with the original's arguments (the burst of radius 1, ...), from the
     * same view (the one meteor_base's runs leave). */
    c = meteor_field(10, 10);
    cok_field_set8(R->character, c->record, COK_FIELD_ACTIVE, 0);
    CHECK(meteor_arena(c, seed) == base);
    battle_clear();

    /* The tries in the handler: walls at 21, 13, at 20, 13 and at 21, 12 stop the fourth meteor's tries 1 to 3 (each sight
     * pulled back to the wall), so try 4 (20, the y of try 3's wall: 12) is seen and the meteor falls there. */
    c = meteor_field(10, 10);
    game.combat.cells[13][21] = 1;
    game.combat.cells[13][20] = 1;
    game.combat.cells[12][21] = 1;
    meteor_run(seed);
    CHECK(game.vm.status == COK_ECL_OK && game.spell_x == 20 && game.spell_y == 12);
    battle_clear();
    /* A wall next to the caster (11, 10) stops every try of every meteor: the last try's cell (the wall's, where its
     * sight stopped) is used. */
    c = meteor_field(10, 10);
    game.combat.cells[10][11] = 1;
    meteor_run(seed);
    CHECK(game.vm.status == COK_ECL_OK && game.spell_x == 11 && game.spell_y == 10);
    battle_clear();

    /* One target at 22, 14: within 1 of the fourth meteor's cell (21, 13) and of no other, nor on any line. Its hit comes
     * after the fourth meteor's arena: a d20 (the save, first), then 10d4 (half on a save), then the flash's draws. */
    /* Several seeds: a seed whose 10d4 is the same one draw later would not tell the orders apart. */
    for (uint32_t k = 0; k < 4; ++k) {
        uint32_t s0 = seed + 7 * k;
        base = meteor_base(s0, 10, 10);
        c = meteor_field(10, 10);
        t4 = meteor_target('T', 22, 14, true);
        uint8_t needed = k % 2 == 0 ? 21 : 2; /* 2: saved but on a 1 (half the damage) */
        cok_field_set8_at(R->character, t4->record, COK_FIELD_SAVING_THROW, 4, needed);
        ran = meteor_run(s0);
        uint32_t p = base;
        uint8_t roll = (uint8_t)cok_dice(&p, 1, 20);
        bool saved = roll == 20 || (roll != 1 && roll >= needed);
        uint8_t dealt = cok_dice(&p, 10, 4);
        flash_draws(&p, 4);
        CHECK(ran == p);
        CHECK(hit_points(t4) == (uint8_t)(0xff - (saved ? dealt / 2 : dealt)));
        battle_clear();
    }

    /* Line and area: the target on meteor 1's line next to its cell is hit twice (the line, then the area); the one at 18,
     * 10 in its area only; the inactive one at 19, 10 is skipped. The seed is the model's, its hits in any order. */
    base = meteor_base(seed, 10, 10);
    c = meteor_field(10, 10);
    cok_combat_line line = {.x0 = 10, .y0 = 10, .x1 = 19, .y1 = 11};
    cok_combat_line_start(&line);
    unsigned steps = 0;
    while (cok_combat_line_step(&line, R)) ++steps;
    int8_t lx = 0, ly = 0;
    line_cell(10, 10, 19, 11, steps - 1, &lx, &ly);
    l = meteor_target('L', lx, ly, true);
    a = meteor_target('A', 18, 10, true);
    inactive = meteor_target('I', 19, 10, false);
    ran = meteor_run(seed);
    want = base;
    meteor_model(c, &want, &hits);
    CHECK(ran == want && hits >= 3);
    CHECK(hit_points(l) < 0xff && hit_points(a) < 0xff && hit_points(inactive) == 0xff);
    battle_clear();

    /* The caster in the area: adjacent to meteor 1's cell (19, 11), so hit with a save; the line leaves her out. */
    base = meteor_base(seed, 18, 10);
    c = meteor_field(18, 10);
    inactive = meteor_target('I', 19, 10, false);
    ran = meteor_run(seed);
    want = base;
    meteor_model(c, &want, &hits);
    CHECK(ran == want && hits == 1);
    CHECK(hit_points(c) < 0xff && hit_points(inactive) == 0xff);
    battle_clear();
    game.vm.character = NULL;
}

/* Effects-2 (step 4, part 34b): the damage word and the rolls. Each routine runs on its holder at its event (6, 4, 0x0c) with
 * the damage (amount16 and its byte) and the rolls set by hand; the handler is the test's, not the spells'. A spell id of level
 * L is the first with its table byte 2 at L (the table's level), so the premises are checked. */
static unsigned spell_of_level(unsigned level)
{
    for (unsigned id = 1; id < 0x90; ++id)
        if (queen_spell_byte(&game, id, 2) == level) return id;
    return 0;
}

/* c2 (part 32b): Sleep (7fd8:05c4), Entangle (22f5), Confusion (243f), Fear (257a), in queen_area.c. The allowance and the
 * saves are the seed's: seed_for_dice and seed_from pick the seed; the draws are counted after the handler. */

/* The first seed whose count d sides sum to want (from 1 on). */
static uint32_t seed_for_dice(unsigned count, uint8_t sides, unsigned want)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        if (cok_dice(&seed, (uint8_t)count, sides) == want) return start;
    }
}

/* A record in the battle at x, y (combat record and cell), its saving throw of type 4 at 21 (it saves on a 20 only). */
static cok_character *c2_target(char name, uint8_t npc, int8_t x, int8_t y)
{
    cok_character *t = record(name, 1, npc);
    place(t, x, y);
    cok_field_set8_at(R->character, t->record, COK_FIELD_SAVING_THROW, 4, 21);
    return t;
}

/* Sleep's target: a record of highest level lv (+0x91), at x, y, with the race word's low byte race (+0x58). */
static cok_character *sleeper(char name, uint8_t lv, uint8_t race, int8_t x, int8_t y)
{
    cok_character *t = record(name, 1, 0);
    place(t, x, y);
    cok_field_set8(R->character, t->record, COK_FIELD_HIGHEST_LEVEL, lv);
    cok_field_set8(R->character, t->record, COK_FIELD_RACE, race);
    return t;
}

/* The caster for the c2 spells: a record at x, y (the selected member), the battle's mode 5, the spell's cell 6, 5. */
static cok_character *c2_caster(int8_t x, int8_t y)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, x, y);
    game.vm.character = a->record;
    game.vm.status = COK_ECL_OK;
    game.vm.mode = 5;
    game.spell_x = 6;
    game.spell_y = 5;
    game.effects.rolls.images = 0;
    game.error[0] = '\0';
    log_text[0] = '\0';
    return a;
}

/* Sleep (7fd8:05c4): the allowance 4d4 and the weights by highest level (+0x91, read unsigned: 1 1, 2 2, 3 4, 4 6, 5 10 for race
 * 7 and 20 otherwise, 0 and 6 and up 20), the status 1 and the 0x35 held dropped. The allowance is spent in list order: 10 at the
 * start goes 1, 6, 2 (9, 3, 1 left) and a 10 is dropped. The images flag stays set; no draw but the allowance's. */
static void test_band_c2_sleep(void)
{
    cok_character *a = c2_caster(5, 5);
    cok_character *w1 = sleeper('a', 1, 0, 20, 20), *w6 = sleeper('b', 4, 0, 21, 20), *w2 = sleeper('c', 2, 0, 22, 20);
    cok_character *w10 = sleeper('d', 5, 7, 23, 20);
    cok_character *list[4] = {w1, w6, w2, w10};
    for (unsigned i = 0; i < 4; ++i) game.spell_targets[i + 1] = list[i];
    game.spell_target_count = 4;
    game.effects.rolls.spell = 0x15;
    uint32_t seed0 = seed_for_dice(4, 4, 10);
    game.vm.seed = seed0;
    CHECK(run_handler(0x15) && game.vm.character == a->record);
    CHECK(game.spell_targets[1] == w1 && game.spell_targets[2] == w6 && game.spell_targets[3] == w2 &&
          game.spell_targets[4] == NULL);
    CHECK(cok_character_find_effect(w1, 0x35) != NULL && cok_character_find_effect(w6, 0x35) != NULL &&
          cok_character_find_effect(w2, 0x35) != NULL && cok_character_find_effect(w10, 0x35) == NULL);
    CHECK(game.effects.rolls.images == 1 && LOGGED("falls asleep"));
    /* One target at a time, each on a fresh record: the boundaries of the weight and the drops. */
    struct {
        uint8_t lv, race;
        uint16_t race_word; /* 0 for the byte alone */
        bool status, held;
        unsigned allowance;
        bool kept;
    } cases[] = {
        {0, 0, 0, false, false, 16, false},  /* level 0 weighs 20 (unsigned): dropped */
        {5, 7, 0x107, false, false, 16, false}, /* race word 0x107 is not 7: weighs 20, dropped */
        {5, 7, 0, false, false, 10, true},   /* 10 against 10: kept, the allowance 0 after */
        {5, 7, 0, false, false, 9, false},   /* 10 against 9: dropped */
        {1, 0, 0, true, false, 16, false},   /* status word 1 (asleep): dropped */
        {1, 0, 0, false, true, 16, false},   /* holds 0x35 already: dropped */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        c2_caster(5, 5);
        cok_character *s = sleeper('s', cases[i].lv, cases[i].race, 20, 20);
        if (cases[i].race_word != 0) {
            s->record[0x58] = (uint8_t)cases[i].race_word;
            s->record[0x59] = (uint8_t)(cases[i].race_word >> 8);
        }
        if (cases[i].status) cok_field_set8(R->character, s->record, COK_FIELD_STATUS, 1);
        if (cases[i].held) CHECK(cok_character_add_effect(s, 0x35, 0, 0, false) != NULL);
        game.spell_targets[1] = s;
        game.spell_target_count = 1;
        game.effects.rolls.spell = 0x15;
        uint32_t allowance_seed = seed_for_dice(4, 4, cases[i].allowance);
        game.vm.seed = allowance_seed;
        CHECK(run_handler(0x15));
        bool kept = game.spell_targets[1] != NULL;
        CHECK(kept == cases[i].kept);
        /* Dropped: the allowance's four draws and nothing more (a kept target's add draws too). */
        if (!kept) CHECK(game.vm.seed == after_draws(allowance_seed, 4, 4));
    }
    game.vm.character = NULL;
    battle_clear();
}

/* Entangle (7fd8:22f5): nothing with var 0x22 set (the seed kept, no text, the image cleared). Else each target saves by type 4
 * (its own d20), and unless it saved "is entangled" (0x1b, the duration of 0x4e). */
static void test_band_c2_entangle(void)
{
    c2_caster(5, 5);
    cok_character *t1 = c2_target('e', 0, 20, 20), *t2 = c2_target('f', 0, 21, 20);
    game.spell_targets[1] = t1;
    game.spell_targets[2] = t2;
    game.spell_target_count = 2;
    game.effects.rolls.spell = 0x4e;
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 1);
    game.vm.seed = 9;
    CHECK(run_handler(0x4e));
    CHECK(game.vm.seed == 9 && cok_character_find_effect(t1, 0x1b) == NULL && cok_character_find_effect(t2, 0x1b) == NULL);
    CHECK(game.effects.rolls.images == 0 && !LOGGED("is entangled"));
    /* Var 0x22 clear: the first target's d20 is 20 (saved), the second's 2 to 19 (not saved). */
    cok_ecl_set_var(&game.vm, COK_VAR_3D, 0);
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 20, 2, 19});
    CHECK(run_handler(0x4e));
    CHECK(cok_character_find_effect(t1, 0x1b) == NULL);
    const cok_effect *e = cok_character_find_effect(t2, 0x1b);
    uint16_t minutes = 0;
    CHECK(e != NULL && queen_spell_duration(&game, 0x4e, &minutes) && e->duration == minutes);
    CHECK(game.effects.rolls.images == 0 && LOGGED("is entangled"));
    game.vm.character = NULL;
    battle_clear();
}

/* Confusion (7fd8:243f): the count 2d8 is drawn first and the targets past it dropped (DS:c6f4 is the count kept); then each
 * target's d20 save and "is confused" (0x23, the duration of 0x52) unless it saved. The seed: 2d8 is 2, the first target saves
 * (20), the second and the two past the count do not (2 to 19): a cap after the saves would confuse the last two. */
static void test_band_c2_confusion(void)
{
    c2_caster(5, 5);
    cok_character *t[4];
    for (unsigned i = 0; i < 4; ++i) t[i] = c2_target('g', 0, (int8_t)(20 + i), 20);
    for (unsigned i = 0; i < 4; ++i) game.spell_targets[i + 1] = t[i];
    game.spell_target_count = 4;
    game.effects.rolls.spell = 0x52;
    uint32_t seed0 = 1;
    for (;; ++seed0) {
        uint32_t s = seed0;
        if (cok_dice(&s, 2, 8) != 2) continue;
        uint8_t r1 = cok_dice(&s, 1, 20);
        bool fail = true;
        for (unsigned k = 0; k < 3; ++k) {
            uint8_t r = cok_dice(&s, 1, 20);
            fail = fail && r >= 2 && r <= 19;
        }
        if (r1 == 20 && fail) break;
    }
    game.vm.seed = seed0;
    CHECK(run_handler(0x52));
    CHECK(game.spell_target_count == 2);
    CHECK(cok_character_find_effect(t[0], 0x23) == NULL && cok_character_find_effect(t[1], 0x23) != NULL);
    CHECK(cok_character_find_effect(t[2], 0x23) == NULL && cok_character_find_effect(t[3], 0x23) == NULL);
    /* The two past the count are not rolled: their saves would have failed (the seed), so no draw of theirs ran. */
    uint16_t minutes = 0;
    const cok_effect *e = cok_character_find_effect(t[1], 0x23);
    CHECK(e != NULL && queen_spell_duration(&game, 0x52, &minutes) && e->duration == minutes);
    CHECK(game.effects.rolls.images == 0 && LOGGED("is confused"));
    game.vm.character = NULL;
    battle_clear();
}

/* Fear (7fd8:257a): the cone of width 3 and length 6 from the caster (10, 10) at the spell's cell (11, 10), the centre line's
 * end 16, 10. A target on the centre line (11, 10, M) saves or not; one on the left line (16, 9, L; the side is east, 2, so
 * the left end is 0 and -1 from the end) is reached by width 3 only. A failed save adds 0x6f and makes the combat record run:
 * forced, computer, NPC 0xb3 (M's NPC kept at 0x80: bit 7), target NULL. A made save says "is unaffected" and writes nothing. */
static void fear_battle(cok_character **m, cok_character **l)
{
    c2_caster(10, 10);
    game.spell_x = 11;
    game.spell_y = 10;
    game.effects.rolls.spell = 0x54;
    *m = record('M', 1, 1);
    place(*m, 11, 10);
    cok_field_set8_at(R->character, (*m)->record, COK_FIELD_SAVING_THROW, 4, 21);
    *l = c2_target('L', 0, 16, 9);
}

static void test_band_c2_fear(void)
{
    static uint8_t dummy;
    cok_character *m, *l, *p;
    uint16_t minutes = 0;
    /* Both fail (rolls 2 to 19): the centre's and the left line's targets, in the cone's order (the centre line first). */
    fear_battle(&m, &l);
    m->combat->target = &dummy;
    game.vm.seed = seed_from(2, (const unsigned[]){20, 2, 19, 20, 2, 19});
    CHECK(run_handler(0x54));
    CHECK(game.effects.rolls.images == 0 && LOGGED("flees in terror") && !LOGGED("is unaffected"));
    CHECK(queen_spell_duration(&game, 0x54, &minutes));
    const cok_effect *e = cok_character_find_effect(m, 0x6f);
    CHECK(e != NULL && e->on_remove && e->duration == minutes);
    CHECK(m->combat->forced == 1 && computer(m) == 1 && cok_field_u8(R->character, m->record, COK_FIELD_NPC) == 0x80 &&
          m->combat->target == NULL);
    CHECK(cok_character_find_effect(l, 0x6f) != NULL && l->combat->forced == 1 && computer(l) == 1 &&
          cok_field_u8(R->character, l->record, COK_FIELD_NPC) == 0xb3 && l->combat->target == NULL);
    battle_clear();
    /* The centre's target saves (20) and the left line's fails: M untouched, "is unaffected" said, L's block written. */
    fear_battle(&m, &l);
    m->combat->target = &dummy;
    game.vm.seed = seed_from(2, (const unsigned[]){20, 20, 20, 20, 2, 19});
    CHECK(run_handler(0x54));
    CHECK(cok_character_find_effect(m, 0x6f) == NULL && m->combat->forced == 0 && computer(m) == 0 &&
          m->combat->target == &dummy && LOGGED("is unaffected"));
    CHECK(cok_character_find_effect(l, 0x6f) != NULL && l->combat->forced == 1 && computer(l) == 1);
    battle_clear();
    /* A cone whose centre line meets no one (side 0xff): the left line's end is 49, 10 (x 16 + 0x63, clamped) and the target
     * at 30, 10 is on it: reached by the 0xff side's left line alone. */
    c2_caster(10, 10);
    game.spell_x = 11;
    game.spell_y = 10;
    game.effects.rolls.spell = 0x54;
    p = c2_target('P', 0, 30, 10);
    game.vm.seed = seed_from(1, (const unsigned[]){20, 2, 19});
    CHECK(run_handler(0x54) && game.spell_target_count == 1 && game.spell_targets[1] == p);
    CHECK(cok_character_find_effect(p, 0x6f) != NULL && p->combat->forced == 1);
    battle_clear();
    /* Outside the battle (mode 4): nothing, the image flag still set, no draw. */
    fear_battle(&m, &l);
    game.vm.mode = 4;
    game.vm.seed = 5;
    CHECK(run_handler(0x54) && game.vm.seed == 5 && game.effects.rolls.images == 1);
    CHECK(cok_character_find_effect(m, 0x6f) == NULL && cok_character_find_effect(l, 0x6f) == NULL);
    game.vm.mode = 5;
    game.vm.character = NULL;
    battle_clear();
}

static void set_rolls(int16_t amount, uint8_t type, uint8_t save_roll)
{
    game.effects.rolls.amount16 = amount;
    game.effects.rolls.amount = (uint8_t)amount;
    game.effects.rolls.damage_type = type;
    game.effects.rolls.save_roll = save_roll;
}

/* Resist Cold (0x0a, 78e4:01f4) and Resist Fire (0x14, 0313): the type's damage halved as a signed word, the save 3 better.
 * 300 of fire is 150 (the byte alone would be 22). Each leaves the other type alone. */
static void test_effects2_resist(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_character *u = record('U', 0, 0);
    CHECK(cok_character_add_effect(t, 0x14, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(u, 0x0a, 0, 0, false) != NULL);
    set_rolls(7, 9, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.amount16 == 3 && game.effects.rolls.amount == 3 && game.effects.rolls.save_roll == 13);
    set_rolls(7, 2, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 7 && game.effects.rolls.save_roll == 10);
    set_rolls(300, 1, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 150 &&
          game.effects.rolls.amount == 150 && game.effects.rolls.save_roll == 13);
    set_rolls(7, 2, 10);
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 3 && game.effects.rolls.amount == 3 &&
          game.effects.rolls.save_roll == 13);
    set_rolls(7, 9, 10);
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 7 && game.effects.rolls.save_roll == 10);
    set_rolls(300, 2, 10);
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 150 &&
          game.effects.rolls.amount == 150 && game.effects.rolls.save_roll == 13);
    set_rolls(0, 0, 0);
    battle_clear();
}

/* Shield (0x11, 78e4:02b9): the holder's AC raised to 0x39 (not lowered from 0x40), the save 1 better, Magic Missile's damage
 * word zeroed with its pending effect kept; another spell's damage stays. */
static void test_effects2_shield(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_character_add_effect(t, 0x11, 0, 0, false) != NULL);
    cok_field_set8(R->character, t->record, COK_FIELD_AC, 0x20);
    set_rolls(5, 0, 10);
    game.effects.rolls.spell = 0x0f;
    game.effects.rolls.pending = 0x6f;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_AC) == 0x39 && game.effects.rolls.save_roll == 11 &&
          game.effects.rolls.amount16 == 0 && game.effects.rolls.amount == 0 && game.effects.rolls.pending == 0x6f);
    cok_field_set8(R->character, t->record, COK_FIELD_AC, 0x40);
    set_rolls(5, 0, 10);
    game.effects.rolls.spell = 0x09;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(R->character, t->record, COK_FIELD_AC) == 0x40 && game.effects.rolls.save_roll == 11 &&
          game.effects.rolls.amount16 == 5 && game.effects.rolls.amount == 5);
    game.effects.rolls.spell = 0;
    game.effects.rolls.pending = 0;
    set_rolls(0, 0, 0);
    battle_clear();
}

/* Barkskin (0x0d, 78e4:021d): the save 1 better unless the damage is magic (type 8), the attack roll 1 worse, always. The
 * Ray of Enfeeblement (0x1d, 0654): the holder's strike of 9 is 7 (9 - 9 / 4). */
static void test_effects2_barkskin_ray(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_character_add_effect(t, 0x0d, 0, 0, false) != NULL);
    set_rolls(5, 1, 10);
    game.effects.rolls.attack_roll = 10;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0c) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.save_roll == 11 && game.effects.rolls.attack_roll == 9);
    set_rolls(5, 8, 10);
    game.effects.rolls.attack_roll = 10;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x0c) && game.effects.rolls.save_roll == 10 &&
          game.effects.rolls.attack_roll == 9);
    battle_clear();
    duel(&a, &t);
    CHECK(cok_character_add_effect(a, 0x1d, 0, 0, false) != NULL);
    set_rolls(9, 0, 0);
    CHECK(cok_effects_dispatch(&game.effects, a, 4) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.amount16 == 7 && game.effects.rolls.amount == 7);
    set_rolls(0, 0, 0);
    battle_clear();
}

/* Globe (0x28, 78e4:073b): a spell of level below 8 cancelled (its damage and pending effect), level 8 not, and none (spell 0)
 * not. Globe of Invulnerability (0x39, 0b11): rolls.spell above 0, level 4 or below and the magic type (8): level 1 with type
 * 9 cancelled; the same with type 1 not; level 5 with type 8 not, level 4 with type 8 cancelled. */
static void test_effects2_globes(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_character *u = record('U', 0, 0);
    const unsigned below = spell_of_level(7), eight = spell_of_level(8);
    CHECK(below != 0 && eight != 0);
    CHECK(cok_character_add_effect(t, 0x28, 0, 0, false) != NULL);
    set_rolls(5, 1, 0);
    game.effects.rolls.spell = (uint8_t)below;
    game.effects.rolls.pending = 0x6f;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.amount16 == 0 && game.effects.rolls.amount == 0 && game.effects.rolls.pending == 0);
    set_rolls(5, 1, 0);
    game.effects.rolls.spell = (uint8_t)eight;
    game.effects.rolls.pending = 0x6f;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 5 && game.effects.rolls.pending == 0x6f);
    set_rolls(5, 1, 0);
    game.effects.rolls.spell = 0;
    game.effects.rolls.pending = 0x6f;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 5 && game.effects.rolls.pending == 0x6f);
    CHECK(queen_spell_byte(&game, 0x09, 2) == 1 && queen_spell_byte(&game, 0x5c, 2) == 5);
    CHECK(cok_character_add_effect(u, 0x39, 0, 0, false) != NULL);
    set_rolls(5, 9, 0);
    game.effects.rolls.spell = 0x09;
    game.effects.rolls.pending = 0x6f;
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.amount16 == 0 && game.effects.rolls.amount == 0 && game.effects.rolls.pending == 0);
    set_rolls(5, 1, 0);
    game.effects.rolls.spell = 0x09;
    game.effects.rolls.pending = 0x6f;
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 5 && game.effects.rolls.pending == 0x6f);
    set_rolls(5, 8, 0);
    game.effects.rolls.spell = 0x5c;
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 5);
    set_rolls(5, 8, 0);
    game.effects.rolls.spell = (uint8_t)spell_of_level(4); /* level 4, the last cancelled */
    game.effects.rolls.pending = 0x6f;
    CHECK(game.effects.rolls.spell != 0 && cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 0 &&
          game.effects.rolls.pending == 0);
    set_rolls(5, 8, 0);
    game.effects.rolls.spell = 0;
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 5);
    game.effects.rolls.pending = 0;
    set_rolls(0, 0, 0);
    game.effects.rolls.spell = 0;
    battle_clear();
}

/* Resistant (0x32, 78e4:0a22: cold, saved by 2; the fire of it is doubled unsaved) and 0x36 (0a7e: the mirror, fire saved and cold
 * doubled). A saved type adds 2 to the save roll; its damage is zeroed when the save is made, halved when not. */
static void test_effects2_resistant(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    cok_character *u = record('U', 0, 0);
    CHECK(cok_character_add_effect(t, 0x32, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(u, 0x36, 0, 0, false) != NULL);
    game.effects.rolls.save_made = 1;
    set_rolls(8, 2, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 0 && game.effects.rolls.save_roll == 12);
    game.effects.rolls.save_made = 0;
    set_rolls(8, 2, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 4 && game.effects.rolls.save_roll == 12);
    set_rolls(8, 1, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 16 && game.effects.rolls.save_roll == 10);
    game.effects.rolls.save_made = 1;
    set_rolls(8, 1, 10);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.effects.rolls.amount16 == 8 && game.effects.rolls.save_roll == 10);
    game.effects.rolls.save_made = 1;
    set_rolls(8, 1, 10);
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 0 && game.effects.rolls.save_roll == 12);
    game.effects.rolls.save_made = 0;
    set_rolls(8, 1, 10);
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 4 && game.effects.rolls.save_roll == 12);
    set_rolls(8, 2, 10);
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 16 && game.effects.rolls.save_roll == 10);
    game.effects.rolls.save_made = 1;
    set_rolls(8, 2, 10);
    CHECK(cok_effects_dispatch(&game.effects, u, 6) && game.effects.rolls.amount16 == 8);
    game.effects.rolls.save_made = 0;
    set_rolls(0, 0, 0);
    battle_clear();
}

/* Each of the nine is applied_only: a node of flag 1 removed changes no roll, even with the damage, the type and the spell that
 * would change them: the type 0x0b (fire, cold and magic) is one each of the nine acts on when applied (0x0a's cold and 0x32's
 * cold among them, which fire and magic alone leave alone). */
static void test_effects2_applied_only(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    static const uint8_t ids[] = {0x0a, 0x0d, 0x11, 0x14, 0x1d, 0x28, 0x32, 0x36, 0x39};
    for (size_t i = 0; i < sizeof ids / sizeof *ids; ++i) {
        cok_effect *e = cok_character_add_effect(t, ids[i], 0, 0, true);
        CHECK(e != NULL);
        set_rolls(9, 0x0b, 10);
        game.effects.rolls.spell = 0x0f;
        game.effects.rolls.attack_roll = 10;
        game.effects.rolls.pending = 0x6f;
        game.effects.rolls.save_made = 1;
        CHECK(cok_effects_remove(&game.effects, t, e, ids[i]) && game.vm.status == COK_ECL_OK);
        CHECK(game.effects.rolls.amount16 == 9 && game.effects.rolls.amount == 9 && game.effects.rolls.save_roll == 10 &&
              game.effects.rolls.attack_roll == 10 && game.effects.rolls.pending == 0x6f);
        CHECK(cok_character_find_effect(t, ids[i]) == NULL);
    }
    game.effects.rolls.save_made = 0;
    game.effects.rolls.spell = 0;
    game.effects.rolls.pending = 0;
    set_rolls(0, 0, 0);
    game.effects.rolls.attack_roll = 0;
    battle_clear();
}

/* effects-3 (queen_effect.c's images, haste, slow, prayer, missiles and add_invisible, dqk 78e4:0580, 06e8, 0877,
 * 09d3, 0832, 0acf): the first seed whose first draw of d(sides) is want (the handler's die). */
static uint32_t seed_showing(uint8_t sides, uint8_t want)
{
    for (uint32_t s = 1;; ++s) {
        uint32_t t = s;
        if (cok_dice(&t, 1, sides) == want) return s;
    }
}

/* 78e4:0580 (0x1c, Mirror Image; ev 5, 6, 9; applied_only): in mode 5 a d(value >> 5 + 1) above 1 takes an image: the
 * damage word and pending effect cancelled, "lost an image", the high nibble counts down (the first 0x1c goes at 0).
 * A d of 1 keeps all, one draw either way. With the images flag set, a spell of ALLIES 0 (Bless, 0x01) loses the image,
 * a spell of ALLIES 1 (Magic Missile, 0x0f) keeps it, and no spell keeps it; with the flag clear the spell is not tested. */
static void test_effects3_images(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    uint32_t big = seed_showing(4, 3), small = seed_showing(4, 1);
    CHECK(queen_spell_word(&game, 0x01, 0x13) == 0 && queen_spell_word(&game, 0x0f, 0x13) != 0);
    game.effects.rolls.images = 0; /* an earlier test's cone leaves the flag set */
    game.effects.rolls.spell = 0;
    /* A d4 of 3 at event 6: the damage cancelled, the pending effect too, the value 0x62 -> 0x42. */
    duel(&a, &t);
    cok_effect *e = cok_character_add_effect(t, 0x1c, 0, 0x62, false);
    CHECK(e != NULL);
    game.effects.rolls.amount16 = 300;
    game.effects.rolls.amount = 44;
    game.effects.rolls.pending = 0x6f;
    game.vm.seed = big;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.amount16 == 0 && game.effects.rolls.amount == 0 && game.effects.rolls.pending == 0);
    /* The d4, then the say's wait (one random(1000), 1df9:052a). */
    uint32_t want = big;
    (void)cok_dice(&want, 1, 4);
    cok_tp_random(&want, 1000);
    CHECK(e->value == 0x42 && LOGGED("lost an image") && game.vm.seed == want);
    /* The strike (event 5): the word zeroed, the image lost. */
    duel(&a, &t);
    e = cok_character_add_effect(t, 0x1c, 0, 0x62, false);
    game.effects.rolls.amount16 = 300;
    game.effects.rolls.amount = 44;
    game.vm.seed = big;
    CHECK(cok_effects_dispatch(&game.effects, t, 5) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.amount16 == 0 && game.effects.rolls.amount == 0 && e->value == 0x42);
    /* A d4 of 1: nothing but the draw. */
    duel(&a, &t);
    e = cok_character_add_effect(t, 0x1c, 0, 0x62, false);
    game.effects.rolls.amount16 = 300;
    game.effects.rolls.pending = 0x6f;
    game.vm.seed = small;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.amount16 == 300 && game.effects.rolls.pending == 0x6f && e->value == 0x62);
    CHECK(!LOGGED("lost an image") && game.vm.seed == after_draws(small, 1, 4));
    /* The last image: value 0x22 (one image) at a d2 of 2 goes, the node with it. */
    duel(&a, &t);
    CHECK(cok_character_add_effect(t, 0x1c, 0, 0x22, false) != NULL);
    game.vm.seed = seed_showing(2, 2);
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x1c) == NULL && LOGGED("lost an image"));
    /* The images flag and the spell: (flag, spell, lost). */
    static const struct { uint8_t images, spell; bool lost; } flagged[] = {
        {1, 0x01, true}, {1, 0x0f, false}, {1, 0, false}, {0, 0x0f, true},
    };
    for (size_t k = 0; k < sizeof flagged / sizeof *flagged; ++k) {
        duel(&a, &t);
        e = cok_character_add_effect(t, 0x1c, 0, 0x62, false);
        game.effects.rolls.images = flagged[k].images;
        game.effects.rolls.spell = flagged[k].spell;
        game.effects.rolls.amount16 = 300;
        game.vm.seed = big;
        CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
        if (flagged[k].lost) CHECK(e->value == 0x42 && game.effects.rolls.amount16 == 0 && game.vm.seed == want);
        else CHECK(e->value == 0x62 && game.effects.rolls.amount16 == 300 && game.vm.seed == after_draws(big, 1, 4));
        game.effects.rolls.images = 0;
        game.effects.rolls.spell = 0;
    }
    /* Outside battle (mode 4): nothing, no draw. */
    duel(&a, &t);
    e = cok_character_add_effect(t, 0x1c, 0, 0x62, false);
    game.vm.mode = 4;
    game.effects.rolls.amount16 = 300;
    game.vm.seed = big;
    CHECK(cok_effects_dispatch(&game.effects, t, 6) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0x62 && game.effects.rolls.amount16 == 300 && game.vm.seed == big);
    game.vm.mode = 5;
    /* Removed (flag 1): 0x1c, 0x31 and 0x29 are applied_only: no draw, no text, the rolls kept (0x29 run with no member
     * selected would stop). */
    static const uint8_t removed[] = {0x1c, 0x31, 0x29};
    for (size_t k = 0; k < sizeof removed / sizeof *removed; ++k) {
        duel(&a, &t);
        game.vm.character = NULL;
        e = cok_character_add_effect(t, removed[k], 0, 0x62, true);
        game.effects.rolls.amount16 = 300;
        game.effects.rolls.attack_roll = 10;
        game.vm.seed = big;
        CHECK(e != NULL && cok_effects_remove(&game.effects, t, e, removed[k]) && game.vm.status == COK_ECL_OK);
        CHECK(game.effects.rolls.amount16 == 300 && game.effects.rolls.attack_roll == 10 && game.vm.seed == big);
        CHECK(!LOGGED("lost an image"));
    }
    /* A terrible presence (0x64, effect.c's h_presence) sets the images flag across its walk (dqk 78e4:14d7, 1584): its
     * add's event 9 on a Mirror Image holder draws the d4 and, with no spell, keeps the image; the terror is added. */
    duel(&a, &t);
    e = cok_character_add_effect(a, 0x1c, 0, 0x62, false);
    CHECK(e != NULL && cok_character_add_effect(t, 0x64, 0, 0, false) != NULL);
    game.effects.rolls.images = 0;
    game.effects.rolls.spell = 0;
    game.vm.seed = big;
    CHECK(cok_effects_run(&game.effects, t, 0x64, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0x62 && !LOGGED("lost an image") && cok_character_find_effect(a, 0x78) != NULL);
    CHECK(game.effects.rolls.images == 0);
    /* No node: the port stops, cited. */
    CHECK(!cok_effects_run(&game.effects, t, 0x1c, NULL) && strstr(game.effects.error, "no effect") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.effects.failed = false;
    game.effects.error[0] = '\0';
    game.vm.character = NULL;
    battle_clear();
}

/* 78e4:06e8 (0x27, haste; ev 0x12; applied_only): the first run (bit 5 clear) gives 0x20, "ages", the age up a year and the
 * aging asked for; every run doubles the movement and the rate. A removal does nothing. */
static void test_effects3_haste(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    const cok_character_layout *L = R->character;
    cok_field_set16(L, t->record, COK_FIELD_AGE, 40);
    cok_effect *e = cok_character_add_effect(t, 0x27, 0, 0, false);
    CHECK(e != NULL);
    game.effects.aging = false;
    game.effects.rolls.rate = 3;
    game.effects.rolls.movement = 5;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x12) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0x20 && LOGGED("ages") && cok_field_u16(L, t->record, COK_FIELD_AGE) == 41 && game.effects.aging);
    CHECK(game.effects.rolls.rate == 6 && game.effects.rolls.movement == 10);
    /* Again: the value's bit 5 is set: no text, no year, the rates doubled once more. */
    log_text[0] = '\0';
    game.effects.aging = false;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x12) && game.vm.status == COK_ECL_OK);
    CHECK(e->value == 0x20 && !LOGGED("ages") && cok_field_u16(L, t->record, COK_FIELD_AGE) == 41 && !game.effects.aging);
    CHECK(game.effects.rolls.rate == 12 && game.effects.rolls.movement == 20);
    /* Removed (flag 1): applied_only, nothing. */
    e = cok_character_add_effect(t, 0x27, 0, 0x20, true);
    CHECK(e != NULL && cok_effects_remove(&game.effects, t, e, 0x27) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.rate == 12 && game.effects.rolls.movement == 20 && cok_field_u16(L, t->record, COK_FIELD_AGE) == 41);
    battle_clear();
}

/* 78e4:0877 (0x2a, slow; ev 0x12; applied_only): the movement and the rate halved, unless the holder holds effect 0x96
 * (177e:1734): then nothing. A removal does nothing. */
static void test_effects3_slow(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_character_add_effect(t, 0x2a, 0, 0, false) != NULL);
    game.effects.rolls.rate = 6;
    game.effects.rolls.movement = 10;
    CHECK(cok_effects_dispatch(&game.effects, t, 0x12) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.rate == 3 && game.effects.rolls.movement == 5);
    /* Removed (flag 1), 0x96 not held: applied_only, nothing. */
    cok_effect *e = cok_character_add_effect(t, 0x2a, 0, 0, true);
    CHECK(e != NULL && cok_effects_remove(&game.effects, t, e, 0x2a) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.rate == 3 && game.effects.rolls.movement == 5);
    CHECK(cok_character_add_effect(t, 0x96, 0, 0, false) != NULL);
    CHECK(cok_effects_dispatch(&game.effects, t, 0x12) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.rate == 3 && game.effects.rolls.movement == 5);
    e = cok_character_add_effect(t, 0x2a, 0, 0, true);
    CHECK(e != NULL && cok_effects_remove(&game.effects, t, e, 0x2a) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.rate == 3 && game.effects.rolls.movement == 5);
    battle_clear();
}

/* 78e4:09d3 (0x31, prayer; ev 0x0a and 0x0c; applied_only): the side in bit 7 of the value against the holder's side word:
 * equal, the attack roll and the save 1 better; else both 1 worse. Bit 4 is not the side (value 0x85 is side 1). */
static void test_effects3_prayer(void)
{
    game.vm.mode = 5; /* the battle */
    static const struct { unsigned side; uint8_t value; int change; } cases[] = {
        {1, 0x85, 1}, {0, 0x85, -1}, {0, 0x05, 1}, {1, 0x05, -1},
    };
    static const uint8_t events[] = {0x0a, 0x0c};
    for (size_t k = 0; k < sizeof cases / sizeof *cases; ++k)
        for (size_t v = 0; v < sizeof events / sizeof *events; ++v) {
            cok_character *a, *t;
            duel(&a, &t);
            set_side(t, cases[k].side);
            CHECK(cok_character_add_effect(t, 0x31, 0, cases[k].value, false) != NULL);
            game.effects.rolls.attack_roll = 10;
            game.effects.rolls.save_roll = 10;
            CHECK(cok_effects_dispatch(&game.effects, t, events[v]) && game.vm.status == COK_ECL_OK);
            CHECK(game.effects.rolls.attack_roll == 10 + cases[k].change && game.effects.rolls.save_roll == 10 + cases[k].change);
        }
    battle_clear();
}

/* 78e4:0832 (0x29, Protection from Normal Missiles; ev 5; applied_only): the selected member's missile item (07d4) with no
 * bonus (+0x30 byte 0) avoids a blow from more than a square off: "Avoids it", the damage 0, the attack roll 0xff, one hit
 * less, one d100 drawn. A bonus of 1, an adjacent attacker, or no readied weapon: nothing, no draw. */
static void test_effects3_missiles(void)
{
    game.vm.mode = 5; /* the battle */
    game.effects.in_battle = true;
    static const struct { int8_t tx; uint8_t bonus, weapon; bool avoided; } cases[] = {
        {7, 0, 1, true}, {7, 1, 1, false}, {6, 0, 1, false}, {7, 0, 0, false},
    };
    for (size_t k = 0; k < sizeof cases / sizeof *cases; ++k) {
        cok_character *a, *t;
        duel_at(&a, &t, cases[k].tx, 5);
        uint8_t axe[COK_ITEM_SIZE] = {0};
        axe[0x28] = 3; /* a thrown axe: its ammunition is the weapon itself */
        axe[0x30] = cases[k].bonus;
        CHECK(cok_character_insert_item(a, 0, axe));
        a->slots[0] = cases[k].weapon;
        CHECK(cok_character_add_effect(t, 0x29, 0, 0, false) != NULL);
        game.vm.character = a->record;
        game.effects.rolls.amount16 = 9;
        game.effects.rolls.amount = 9;
        game.effects.rolls.attack_roll = 10;
        game.effects.rolls.hits[0] = 2;
        game.vm.seed = 1234;
        CHECK(cok_effects_dispatch(&game.effects, t, 5) && game.vm.status == COK_ECL_OK);
        if (cases[k].avoided) {
            CHECK(LOGGED("Avoids it") && game.effects.rolls.amount16 == 0 && game.effects.rolls.amount == 0);
            CHECK(game.effects.rolls.attack_roll == 0xff && game.effects.rolls.hits[0] == 1);
            uint32_t want = 1234; /* the d100, then the say's wait (one random(1000)) */
            (void)cok_dice(&want, 1, 100);
            cok_tp_random(&want, 1000);
            CHECK(game.vm.seed == want);
        } else {
            CHECK(!LOGGED("Avoids it") && game.effects.rolls.amount16 == 9 && game.effects.rolls.attack_roll == 10);
            CHECK(game.effects.rolls.hits[0] == 2 && game.vm.seed == 1234);
        }
        game.vm.character = NULL;
    }
    /* A bow (its type's flags 9: arrows, slot 11): 07d4 takes the arrows, so the arrows' bonus decides, not the bow's. */
    uint8_t row[16];
    memcpy(row, game.item_types.type[3], sizeof row);
    game.item_types.type[3][14] = 9;
    static const struct { uint8_t bow, arrows; bool avoided; } shot[] = {{1, 0, true}, {0, 1, false}};
    for (size_t k = 0; k < sizeof shot / sizeof *shot; ++k) {
        cok_character *a, *t;
        duel_at(&a, &t, 7, 5);
        uint8_t bow[COK_ITEM_SIZE] = {0}, arrows[COK_ITEM_SIZE] = {0};
        bow[0x28] = 3;
        bow[0x30] = shot[k].bow;
        arrows[0x30] = shot[k].arrows;
        CHECK(cok_character_insert_item(a, 0, bow) && cok_character_insert_item(a, 1, arrows));
        a->slots[0] = 1;
        a->slots[11] = 2;
        CHECK(cok_character_add_effect(t, 0x29, 0, 0, false) != NULL);
        game.vm.character = a->record;
        game.effects.rolls.amount16 = 9;
        game.effects.rolls.attack_roll = 10;
        game.vm.seed = 1234;
        CHECK(cok_effects_dispatch(&game.effects, t, 5) && game.vm.status == COK_ECL_OK);
        CHECK(LOGGED("Avoids it") == shot[k].avoided && (game.effects.rolls.attack_roll == 0xff) == shot[k].avoided);
        game.vm.character = NULL;
    }
    memcpy(game.item_types.type[3], row, sizeof row);
    game.effects.in_battle = false;
    battle_clear();
}

/* 78e4:0acf (0x38, add invisibility; ev 8; not applied_only): event 8 adds 0x19 for a minute, value 0x0c, flag 0. Removed
 * in battle (mode 5) it takes the first 0x19; removed outside battle nothing goes. */
static void test_effects3_add_invisible(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    CHECK(cok_character_add_effect(t, 0x38, 0, 0, false) != NULL);
    CHECK(cok_effects_dispatch(&game.effects, t, 8) && game.vm.status == COK_ECL_OK);
    cok_effect *inv = cok_character_find_effect(t, 0x19);
    CHECK(inv != NULL && inv->duration == 1 && inv->value == 0x0c && !inv->on_remove);
    /* A second 0x19 after it: the removal takes the first, the later stays. */
    CHECK(cok_character_add_effect(t, 0x19, 1, 0x55, false) != NULL);
    cok_effect *gone = cok_character_add_effect(t, 0x38, 0, 0, true);
    CHECK(gone != NULL && cok_effects_remove(&game.effects, t, gone, 0x38) && game.vm.status == COK_ECL_OK);
    cok_effect *left = cok_character_find_effect(t, 0x19);
    CHECK(left != NULL && left->value == 0x55 && left != inv);
    /* Outside battle, nothing is removed. */
    game.vm.mode = 4;
    gone = cok_character_add_effect(t, 0x38, 0, 0, true);
    CHECK(gone != NULL && cok_effects_remove(&game.effects, t, gone, 0x38) && game.vm.status == COK_ECL_OK);
    CHECK(cok_character_find_effect(t, 0x19) == left);
    game.vm.mode = 5;
    battle_clear();
}

/* effects-3's add_raw stop (the effects' pool, effect.c's add): with the pool full, 0x19 is not added: the port stops,
 * cited as effect.c's (dqk 19c2). */
static void test_effects3_pool(void)
{
    game.vm.mode = 5; /* the battle */
    cok_character *a, *t;
    duel(&a, &t);
    unsigned n = 0;
    for (size_t i = 0; i < game.party.count; ++i)
        for (const cok_effect *e = game.party.members[i]->effects; e != NULL; e = e->next) ++n;
    for (; n + 1 < R->effects.pool_size; ++n) CHECK(cok_character_add_effect(t, 0x01, 0, 0, false) != NULL);
    /* One node short of full: the add is made (the pool then full). */
    CHECK(cok_effects_run(&game.effects, t, 0x38, NULL) && cok_character_find_effect(t, 0x19) != NULL);
    CHECK(!cok_effects_run(&game.effects, a, 0x38, NULL));
    CHECK(strstr(game.effects.error, "effect 0x19") != NULL && strstr(game.effects.error, "pool is full") != NULL &&
          strstr(game.effects.error, "19c2") != NULL);
    CHECK(cok_character_find_effect(a, 0x19) == NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.effects.failed = false;
    game.effects.error[0] = '\0';
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
    test_applied_only();
    test_invisible_sees();
    test_held_ends_turn();
    test_cancel_word();
    test_own_hook_missing();
    test_effects_four_charm();
    test_effects_four_silence();
    test_effects_four_confusion();
    test_effects_four_terror();
    test_band_b2_fireball();
    test_band_b2_flame();
    test_band_b2_ice();
    test_band_b3_straight();
    test_band_b3_lengths();
    test_band_b3_save();
    test_band_b3_bounce();
    test_band_b3_second_bounce();
    test_band_b3_caster_bounce();
    test_band_b3_wrap();
    test_band_b3_open();
    test_band_b3_offmap();
    test_band_b3_own_cell();
    test_band_b3_lightning();
    test_band_b3_lightning_wet();
    test_band_b3_javelin();
    test_effects5_poison();
    test_effects5_drain();
    test_effects5_wasting();
    test_effects5_disease();
    test_cone();
    test_cone_of_cold();
    test_meteor_try();
    test_meteor_swarm();
    test_band_c2_sleep();
    test_band_c2_entangle();
    test_band_c2_confusion();
    test_band_c2_fear();
    test_effects2_resist();
    test_effects2_shield();
    test_effects2_barkskin_ray();
    test_effects2_globes();
    test_effects2_resistant();
    test_effects2_applied_only();
    test_effects3_images();
    test_effects3_haste();
    test_effects3_slow();
    test_effects3_prayer();
    test_effects3_missiles();
    test_effects3_add_invisible();
    test_effects3_pool();
    cok_party_free(&game.party);
    puts("queen fight tests passed");
    return 0;
}
