/* Death Knights' fight where it differs from Champions' (the descriptor's
 * fight section): its cloud and pool tiles, the death that finds a body
 * already there, turning undead's levels and count, the nightmares' smoke
 * (effect 0x6a), half damage from weapons of kind 0 or 1 (0x5d), Alt-A at
 * the player's turn, the computer's spell choice, aiming's range and first
 * target, attacking an ally, the movement a round, the clouds; on records
 * made by hand in its layout, the game opened on DKKAssets/ (skipped without). */
#include "adventure.h"
#include "ai.h"
#include "arena.h"
#include "attack.h"
#include "cast.h"
#include "cloud.h"
#include "combat.h"
#include "command.h"
#include "round.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_death_knights;
/* tests/test_fight.c's Faerie Fire count (test_faerie_fire). */
#define FAERIE_FIRE_HIGHLIGHTED 191u
static cok_adventure game;
static char log_text[1 << 16];

static int no_key(void *context)
{
    (void)context;
    return -1;
}

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g, (void)context;
    size_t used = strlen(log_text);
    snprintf(log_text + used, sizeof log_text - used, "%s: %s;", kind, text);
}

static void reset(void)
{
    cok_party_free(&game.party);
    game.vm.character = NULL;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    memset(&game.effects.rolls, 0, sizeof game.effects.rolls);
    game.vm.mode = 5;
    game.vm.seed = 1;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.effects.in_battle = true;
    game.speed = 0;
    cok_combat *c = &game.combat;
    memset(c->cells, COK_COMBAT_FLOOR, sizeof c->cells);
    memset(c->combatant, 0, sizeof c->combatant);
    memset(c->turn, 0, sizeof c->turn);
    memset(c->body, 0, sizeof c->body);
    c->count = 1;
    c->bodies = 0;
    c->view_x = c->view_y = 0;
    c->sides[0] = c->sides[1] = 0;
    c->round_limit = 15;
    c->active = true;
    log_text[0] = '\0';
}

/* A record of Death Knights' layout named by a letter, one cell, 10 hit
 * points, on side, able to act, with a zeroed combat record, a combatant
 * at x, y. */
static cok_character *record(char name, uint8_t side, int8_t x, int8_t y)
{
    const cok_character_layout *L = R->character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    cok_field_set8_at(L, r, COK_FIELD_NAME, 0, 1);
    cok_field_set8_at(L, r, COK_FIELD_NAME, 1, (uint8_t)name);
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 10);
    cok_field_set8(L, r, COK_FIELD_MAX_HIT_POINTS, 10);
    cok_field_set8(L, r, COK_FIELD_SIZE, 1);
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_HOSTILE, side);
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL && cok_party_append(&game.party, c));
    c->combat->not_party = side;
    cok_combat *k = &game.combat;
    cok_combatant *e = &k->combatant[k->count];
    e->x = x;
    e->y = y;
    e->size = 1;
    e->character = c;
    ++k->count;
    k->combatant[k->count].size = 0;
    k->combatant[k->count].character = NULL;
    CHECK(cok_combat_occupy(k, R));
    k->sides[side] = (uint8_t)(k->sides[side] + 1);
    return c;
}

/* A seed whose first rolls of the dice given (sides, lo, hi triples) are
 * in their bounds. */
static uint32_t seed_seq(unsigned n, const unsigned *triples)
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

/* 6f03:0e33: the cloud's tile 0x1d and the pool's 0x1c, marked. */
static void test_probe(void)
{
    reset();
    cok_character *a = record('A', 0, 5, 5);
    uint8_t occupant, terrain;
    bool cloud, pool;
    game.combat.cells[5][5] = 0x1d;
    CHECK(cok_combat_probe(&game.combat, R, a->record, 8, &occupant, &terrain, &cloud, &pool));
    CHECK(cloud && !pool);
    game.combat.cells[5][5] = 0x1c;
    CHECK(cok_combat_probe(&game.combat, R, a->record, 8, &occupant, &terrain, &cloud, &pool));
    CHECK(!cloud && pool);
}

/* 6f03:0fbd: a record already among the bodies does nothing. */
static void test_kill(void)
{
    reset();
    cok_character *a = record('A', 0, 5, 5);
    record('M', 1, 7, 5);
    cok_field_set8(R->character, a->record, COK_FIELD_ACTIVE, 0);
    game.combat.body[2].character = a;
    game.combat.bodies = 3;
    CHECK(cok_arena_kill(&game, a, COK_ARENA_STALE_UNKNOWN, NULL));
    CHECK(game.combat.bodies == 3 && strstr(log_text, "sound") == NULL);
}

/* 5340:119b: the d20, then at the first undead 1d6 + 6 of them where its
 * row's value is negative (to destroy), else 1d12; a cleric of level 13,
 * made 9. Kind 2 at level 9 is -1 (destroyed, 1d6 + 6, at least 6), kind 3
 * 0 (destroyed, 1d12; -1 at 10). */
static unsigned turned_by(int kind, unsigned *expected)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *c = record('C', 0, 2, 12);
    cok_field_set8_at(L, c->record, COK_FIELD_CLASS_LEVEL, 0, 13);
    for (int i = 0; i < 16; ++i) {
        cok_character *u =
            record((char)('a' + i), 1, (int8_t)(20 + i % 8), (int8_t)(5 + i / 8 * 2));
        cok_field_set8(L, u->record, COK_FIELD_UNDEAD, (uint8_t)kind);
    }
    const unsigned dice[] = {20, 2, 20};
    uint32_t seed = seed_seq(1, dice), copy = seed;
    cok_dice(&copy, 1, 20);
    *expected = kind == 2 ? cok_dice(&copy, 1, 6) + 6u : cok_dice(&copy, 1, 12);
    game.vm.seed = seed;
    game.undead = 1;
    if (!cok_combat_turn_undead(&game, c)) fprintf(stderr, "%s\n", game.error);
    CHECK(game.vm.status == COK_ECL_OK);
    unsigned destroyed = 0;
    for (size_t i = 1; i < game.party.count; ++i)
        destroyed += cok_field_u8(L, game.party.members[i]->record, COK_FIELD_STATUS) == 8;
    return destroyed;
}

static void test_turning(void)
{
    const cok_data_segment *D = R->data;
    CHECK((int8_t)cok_data_held(D, cok_data_offset2(D, COK_DATA_TURNING, 2, 9)) == -1 &&
          (int8_t)cok_data_held(D, cok_data_offset2(D, COK_DATA_TURNING, 3, 9)) == 0 &&
          (int8_t)cok_data_held(D, cok_data_offset2(D, COK_DATA_TURNING, 3, 10)) == -1);
    unsigned expected;
    CHECK(turned_by(2, &expected) == expected);
    CHECK(turned_by(3, &expected) == expected);
}

/* 504d:2448: the adjacent enemy of the most THAC0 plus attacks times six
 * times dice plus bonus, the first of equals, saves (type 3) or gets 0x6c. */
static void test_smoke(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *n = record('N', 1, 5, 5);
    cok_character *a = record('A', 0, 6, 5), *b = record('B', 0, 4, 5);
    cok_character *far = record('F', 0, 9, 5);
    for (unsigned k = 0; k < 3; ++k) {
        cok_character *e = k == 0 ? a : k == 1 ? b : far;
        cok_field_set8(L, e->record, COK_FIELD_THAC0, 40);
        cok_field_set8_at(L, e->record, COK_FIELD_ATTACKS_LEFT, 1, 2);
        cok_field_set8_at(L, e->record, COK_FIELD_DICE, 1, 1);
        for (unsigned t = 0; t < 5; ++t) cok_field_set8_at(L, e->record, COK_FIELD_SAVING_THROW, t, 21);
    }
    game.vm.character = n->record;
    CHECK(cok_character_add_effect(n, 0x6a, 0, 0, false) != NULL);
    CHECK(cok_effects_run(&game.effects, n, 0x6a, NULL));
    bool on_a = cok_character_find_effect(a, 0x6c) != NULL;
    bool on_b = cok_character_find_effect(b, 0x6c) != NULL;
    /* The first listed of equals (6e48:08d6 lists A before B). */
    CHECK(on_a && !on_b && cok_character_find_effect(far, 0x6c) == NULL);
    cok_character *second = b;
    /* That one saves against type 3 but not 4: a d20 not 1 makes it. */
    cok_field_set8_at(L, second->record, COK_FIELD_SAVING_THROW, 3, 2);
    const unsigned dice[] = {20, 2, 20};
    game.vm.seed = seed_seq(1, dice);
    CHECK(cok_effects_run(&game.effects, n, 0x6a, NULL));
    CHECK(cok_character_find_effect(second, 0x6c) == NULL && strstr(log_text, "is unaffected"));
}

/* 504d:2170: half damage from a striking item of kind (type byte 7) below
 * 2. */
static void test_half(void)
{
    reset();
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    int kind1 = -1;
    for (int t = 0; t < COK_ITEM_TYPES && kind1 < 0; ++t)
        if (game.item_types.type[t][0] == 0 && game.item_types.type[t][7] == 1) kind1 = t;
    CHECK(kind1 >= 0);
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = (uint8_t)kind1;
    item[0x34] = 1;
    CHECK(cok_character_insert_item(a, 0, item));
    a->slots[0] = 1;
    game.vm.character = a->record;
    CHECK(cok_character_add_effect(m, 0x5d, 0, 0, false) != NULL);
    game.effects.rolls.amount = 9;
    CHECK(cok_effects_dispatch(&game.effects, m, 5) && game.effects.rolls.amount == 4);
}

/* 65cd:1241 for the movement a round (5340:0000, 0cc0, 2afe, 3f63:1260):
 * the movement recomputed from the base with haste (0x27), slow (0x2a) and
 * 0x09 in it, var 0x7f72 added on the party's side, doubled; no event 0x12
 * doubles it again, and no bounds (Champions' 432f:014d). */
static void test_movement(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *a = record('A', 0, 5, 5), *m = record('M', 1, 6, 5);
    cok_field_set8(L, a->record, COK_FIELD_BASE_MOVEMENT, 12);
    cok_field_set8(L, m->record, COK_FIELD_BASE_MOVEMENT, 12);
    /* Hasted (value 0x17: aged already), the stats run since. */
    CHECK(cok_character_add_effect(a, 0x27, 0, 0x17, false) != NULL);
    cok_field_set8(L, a->record, COK_FIELD_MOVEMENT, 24);
    uint8_t move;
    CHECK(cok_combat_movement(&game, a, &move) && move == 48);
    CHECK(cok_field_u8(L, a->record, COK_FIELD_MOVEMENT) == 24);
    /* Hasted since the stats ran: the same. */
    cok_field_set8(L, a->record, COK_FIELD_MOVEMENT, 12);
    CHECK(cok_combat_movement(&game, a, &move) && move == 48);
    CHECK(cok_field_u8(L, a->record, COK_FIELD_MOVEMENT) == 24);
    /* Slowed, with no event: half. */
    CHECK(cok_character_add_effect(m, 0x2a, 0, 0, false) != NULL);
    CHECK(cok_combat_movement(&game, m, &move) && move == 12);
    /* The party's bonus, for the party's side only; 0 stays 0. */
    cok_ecl_set_var(&game.vm, COK_VAR_MOVE_BONUS, 2);
    CHECK(cok_combat_movement(&game, a, &move) && move == 52);
    CHECK(cok_combat_movement(&game, m, &move) && move == 12);
    cok_field_set8(L, m->record, COK_FIELD_BASE_MOVEMENT, 0);
    CHECK(cok_combat_movement(&game, m, &move) && move == 0);
    CHECK(game.vm.status == COK_ECL_OK && log_text[0] == '\0');
}

/* 3e05:08af: Alt-A at the player's turn puts the party on Auto, as
 * Alt-Q: the turn given back, initiative 0x14, every record the
 * computer's. */
static void test_alt_a(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *a = record('A', 0, 5, 5);
    record('B', 0, 6, 5);
    game.combat.turn_index = 3;
    a->combat->initiative = 5;
    CHECK(cok_combat_commands(&game, a, 0x1e) && game.vm.status == COK_ECL_OK);
    CHECK(game.combat.turn_index == 2 && a->combat->initiative == 0x14);
    for (size_t i = 0; i < game.party.count; ++i)
        CHECK(cok_field_u8(L, game.party.members[i]->record, COK_FIELD_COMPUTER) == 1);
}

/* 3f63:05d7 with 056c and 5340:43ea: Magic Missile (0x0f, of use against
 * one of the other side within its range, 6 + 4 * the caster level, 6 for
 * one of no levels) by an NPC: with none in range no candidate, no die;
 * with one, no cast where a d100 is at most four times the levels. */
static void test_cast_choice(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = record('W', 1, 2, 5);
    cok_character *a = record('A', 0, 45, 5);
    uint8_t spell = 0x0f;
    cok_field_set_spells(L, w->record, true);
    cok_field_set8_at(L, w->record, COK_FIELD_MEMORIZED, 1, spell);
    cok_field_set8(L, w->record, COK_FIELD_NPC, 0x80);
    w->combat->may_cast = 1;
    game.vm.character = w->record;
    game.vm.seed = 77;
    bool cast = true;
    CHECK(cok_ai_cast(&game, w, &cast) && !cast && game.vm.seed == 77);
    /* In range: the d100 (40, four times 10 levels), and no more. */
    game.combat.combatant[2].x = 8;
    CHECK(cok_combat_occupy(&game.combat, R));
    cok_field_set8_at(L, w->record, COK_FIELD_CLASS_LEVEL, 2, 10);
    const unsigned dice[] = {100, 40, 40};
    uint32_t seed = seed_seq(1, dice), copy = seed;
    cok_dice(&copy, 1, 100);
    game.vm.seed = seed;
    cast = true;
    CHECK(cok_ai_cast(&game, w, &cast) && !cast && game.vm.seed == copy);
    /* Slay Living (0x4c, first by priority) is of no use against one whose
     * creature flags' byte 1 has bit 3 (5340:3fd5): the next, Hold Person
     * (0x17), is cast. */
    cok_field_set8_at(L, w->record, COK_FIELD_MEMORIZED, 1, 0x17);
    cok_field_set8_at(L, w->record, COK_FIELD_MEMORIZED, 2, 0x4c);
    cok_field_set8_at(L, w->record, COK_FIELD_CLASS_LEVEL, 2, 0);
    cok_field_set8_at(L, a->record, COK_FIELD_CREATURE_FLAGS, 1, 8);
    game.combat.combatant[2].x = 4;
    CHECK(cok_combat_occupy(&game.combat, R));
    /* (Its casting begins: the spell is the turn's, cast at its next.) */
    CHECK(cok_ai_cast(&game, w, &cast) && cast && w->combat->spell == 0x17);
}

/* Keys for a test, from a string ('\x01' the 0 of an extended key). */
static const char *script;

static int scripted(void *context)
{
    (void)context;
    if (script == NULL || *script == '\0') return -1;
    char c = *script++;
    return c == 1 ? 0 : (unsigned char)c;
}

/* 5340:3888's targets (36f2) and the first shown (2bee): within the
 * weapon's range whole (a type of range 2, its own ammunition, the target
 * two cells off: Target offered, as Champions' range less 1 would not);
 * the one aimed at last shown first, Target then selected (DS:c0f7 4). */
static void test_aim(void)
{
    reset();
    cok_character *a = record('A', 0, 5, 5);
    cok_character *m = record('M', 1, 7, 5);
    uint8_t type = 0x40;
    uint8_t saved[16];
    memcpy(saved, game.item_types.type[type], sizeof saved);
    game.item_types.type[type][12] = 2;
    game.item_types.type[type][14] = 0x10;
    static uint8_t items[1][COK_ITEM_SIZE];
    items[0][0x2e] = type;
    a->items = items;
    a->item_count = 1;
    a->slots[0] = 1;
    game.keys.read = scripted;
    script = "n\x1b";
    game.selected = 1;
    bool done;
    cok_combat_aimed res;
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && game.vm.status == COK_ECL_OK);
    CHECK(strstr(log_text, "menu: Next Prev Manual Center Exit;menu: Next Prev Manual Target "
                           "Center Exit;") != NULL && !done);
    a->combat->target = m->record;
    script = "\x1b";
    game.selected = 1;
    log_text[0] = '\0';
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && game.selected == 4);
    CHECK(strstr(log_text, "menu: Next Prev Manual Target Center Exit;") != NULL);
    memcpy(game.item_types.type[type], saved, sizeof saved);
    a->items = NULL;
    a->item_count = 0;
    a->slots[0] = 0;
    game.keys.read = no_key;
}

/* 3e05:08af's hidden keys (the set at 3e05:0cac): the keypad's 5 (0x20)
 * gives every party member who is no NPC back to the player; End ends the
 * turn. */
static void test_hidden(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *a = record('A', 0, 5, 5);
    cok_character *b = record('B', 0, 6, 5);
    cok_field_set8(L, b->record, COK_FIELD_COMPUTER, 1);
    a->combat->movement = 0;
    game.keys.read = scripted;
    script = "5e";
    CHECK(cok_combat_commands(&game, a, 0) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_COMPUTER) == 0 && a->combat->initiative == 0);
    CHECK(strstr(log_text, "menu: Aim Guard Quick@Delay View Speed End;") != NULL);
    game.keys.read = no_key;
}

/* 5340:2b81: attacking one of its own side asks "attack ally? " (the
 * Yes/No by index, 6a69:2048); Yes sets var 0x7f33 and turns no NPC
 * against the party. */
static void test_ally(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *a = record('A', 0, 5, 5);
    cok_character *b = record('B', 0, 6, 5);
    cok_character *k = record('K', 0, 7, 5);
    cok_field_set8(L, k->record, COK_FIELD_NPC, 0x80);
    game.keys.read = scripted;
    script = "y";
    bool go = false;
    CHECK(cok_combat_ally(&game, a, b, &go) && go);
    CHECK(strstr(log_text, "menu: attack ally? ;choice: Y;") != NULL);
    CHECK(cok_ecl_var(&game.vm, COK_VAR_FLED) == 1 &&
          cok_field_u8(L, k->record, COK_FIELD_HOSTILE) == 0);
    game.keys.read = no_key;
}

/* Band B's spells (5dc5's handlers) on records made by hand: the
 * computer casts in battle (its targets its own picks, 5340:1eeb), or in
 * camp. */

/* c, the computer's, casts spell (5dc5:1298 in battle, Magic On). */
static void computer_casts(cok_character *c, uint8_t spell)
{
    game.vm.character = c->record;
    game.combat_targets = true;
    game.vm.mode = 5;
    game.vm.status = COK_ECL_OK;
    log_text[0] = '\0';
    bool done = false;
    cok_cast_spell(&game, spell, true, true, 0, &done);
}

/* The first seed from start whose first d-sided rolls (sides, lo, hi
 * triples) hold, as seed_seq. */
static uint32_t seed_from(uint32_t start, unsigned n, const unsigned *triples)
{
    for (;; ++start) {
        uint32_t seed = start;
        bool ok = true;
        for (unsigned i = 0; i < n && ok; ++i) {
            unsigned d = cok_dice(&seed, 1, (uint8_t)triples[3 * i]);
            ok = d >= triples[3 * i + 1] && d <= triples[3 * i + 2];
        }
        if (ok) return start;
    }
}

/* Charm Person (5dc5:2044): an elf (+0x20 7) whose creature flags lack 2
 * is unaffected, said without a saving throw ("is unaffected"); the same
 * elf with the flag, and one of another race with flags 2 (which
 * Champions' test, flags above 1, would leave unaffected), are charmed
 * (0x0b) where their save fails. */
static void test_charm_person(void)
{
    const cok_character_layout *L = R->character;
    for (int k = 0; k < 3; ++k) {
        bool charmed = false, immune = false;
        for (uint32_t seed = 1; seed < 400 && !charmed && !immune; ++seed) {
            reset();
            cok_character *w = record('W', 0, 5, 5), *t = record('T', 1, 7, 5);
            cok_field_set8_at(L, w->record, COK_FIELD_CLASS_LEVEL, 5, 9);
            cok_field_set8(L, w->record, COK_FIELD_COMPUTER, 1);
            cok_field_set8(L, t->record, COK_FIELD_RACE, k == 2 ? 1 : 7);
            cok_field_set8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0, k == 0 ? 0 : 2);
            game.vm.seed = seed;
            computer_casts(w, 0x0a);
            CHECK(game.vm.status == COK_ECL_OK);
            /* A save made says the same (638c:1f86, DS:1d0f): only the
             * elf without the flag is unaffected with no throw. */
            immune = strstr(log_text, "print: is unaffected;") != NULL &&
                     game.effects.rolls.save_roll == 0;
            charmed = cok_character_find_effect(t, 0x0b) != NULL;
        }
        CHECK(k == 0 ? immune && !charmed : charmed && !immune);
    }
}

/* Fire Shield (5dc5:468f) for the computer: 1d2, 1 the hot shield (0x32),
 * 2 the cold (0x36), each with 0x70, "is Protected". */
static void test_fire_shield(void)
{
    const cok_character_layout *L = R->character;
    for (unsigned d = 1; d <= 2; ++d) {
        reset();
        cok_character *w = record('W', 0, 5, 5);
        cok_field_set8_at(L, w->record, COK_FIELD_CLASS_LEVEL, 5, 9);
        cok_field_set8(L, w->record, COK_FIELD_COMPUTER, 1);
        const unsigned dice[] = {2, d, d};
        game.vm.seed = seed_from(1, 1, dice);
        game.vm.character = w->record;
        game.combat_targets = false;
        game.vm.mode = 4;
        bool done = false;
        cok_cast_spell(&game, 0x55, false, false, COK_CAST_CAMP, &done);
        CHECK(game.vm.status == COK_ECL_OK && cok_character_find_effect(w, 0x70) != NULL);
        CHECK((cok_character_find_effect(w, 0x32) != NULL) == (d == 1) &&
              (cok_character_find_effect(w, 0x36) != NULL) == (d == 2));
        CHECK(strstr(log_text, "print: is Protected;") != NULL);
    }
    game.vm.mode = 5;
}

/* Strength (5dc5:2bc9) on one of none of the classes it rolls for: from
 * an item (DS:c447) the handler's [bp-6] is the NULL segment 638c:13d4
 * left there (called at 5dc5:1557, at the handler's depth), so 0: Strength's
 * effect with a bonus of 0; without an item it is 1298's saved BP, and the run
 * stops. */
static void test_strength_unset(void)
{
    const cok_character_layout *L = R->character;
    for (int item = 0; item < 2; ++item) {
        reset();
        cok_character *w = record('W', 0, 5, 5);
        cok_field_set8(L, w->record, COK_FIELD_COMPUTER, 1);
        cok_field_set8_at(L, w->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 12);
        game.effects.rolls.item = (uint8_t)item;
        computer_casts(w, 0x23);
        if (item == 0) {
            CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "5dc5:2bc9") != NULL);
        } else {
            CHECK(game.vm.status == COK_ECL_OK);
            /* Better than the base (0): Strength's effect, its bonus 0. */
            const cok_effect *e =
                cok_character_find_effect(w, cok_game_effect(&game, COK_EFFECT_STRENGTH));
            CHECK(e != NULL && e->value == 100);
        }
    }
    game.effects.rolls.item = 0;
    game.vm.status = COK_ECL_OK;
}

/* Leadership (5340:4acf): a knight of no order rolls against the chance
 * the previous call left in 4a25's [bp-3] (nothing else is called between
 * two), here an order-2 knight's 40 that did not lead; as the first
 * caller, it stops. */
static void test_leadership_unset(void)
{
    const cok_character_layout *L = R->character;
    for (int first = 0; first < 2; ++first) {
        reset();
        cok_character *a = record('A', 0, 1, 1), *b = record('B', 0, 2, 1);
        cok_character *n = record('N', 0, 3, 1);
        cok_field_set8_at(L, a->record, COK_FIELD_CLASS_LEVEL, 7, 5);
        cok_field_set8(L, a->record, COK_FIELD_KNIGHT_ORDER, first ? 0 : 2);
        cok_field_set8_at(L, b->record, COK_FIELD_CLASS_LEVEL, 7, 5);
        cok_field_set8(L, b->record, COK_FIELD_KNIGHT_ORDER, 0);
        cok_field_set8(L, n->record, COK_FIELD_NPC, 0x80);
        cok_field_set8_at(L, n->record, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE, 10);
        const unsigned dice[] = {100, 40, 100, 100, 1, 39};
        game.vm.seed = seed_from(1, 2, dice);
        bool ok = cok_combat_leadership(&game);
        if (first) {
            CHECK(!ok && game.vm.status == COK_ECL_UNDEFINED &&
                  strstr(game.error, "5340:4a25") != NULL);
        } else {
            CHECK(ok && strstr(log_text, "B leads") != NULL &&
                  cok_field_u8(L, n->record, COK_FIELD_COMPUTER) == 0);
        }
    }
    /* After a lead is shown, 4a25's [bp-3] holds SS's high byte (push ss
     * at 4c42): the order-2 knight A fails for M, then leads N (shown);
     * the next call, the knight of no order B's for M, stops. */
    reset();
    cok_character *a = record('A', 0, 1, 1), *b = record('B', 0, 2, 1);
    cok_character *m = record('M', 0, 3, 1), *n = record('N', 0, 4, 1);
    cok_field_set8_at(L, a->record, COK_FIELD_CLASS_LEVEL, 7, 5);
    cok_field_set8(L, a->record, COK_FIELD_KNIGHT_ORDER, 2);
    cok_field_set8_at(L, b->record, COK_FIELD_CLASS_LEVEL, 7, 5);
    cok_field_set8(L, b->record, COK_FIELD_KNIGHT_ORDER, 0);
    cok_character *allies[2] = {m, n};
    for (int i = 0; i < 2; ++i) {
        cok_field_set8(L, allies[i]->record, COK_FIELD_NPC, 0x80);
        cok_field_set8_at(L, allies[i]->record, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE, 10);
    }
    const unsigned dice[] = {100, 40, 100, 100, 1, 39, 100, 1, 100};
    game.vm.seed = seed_from(1, 3, dice);
    CHECK(!cok_combat_leadership(&game) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "5340:4a25") != NULL && strstr(log_text, "A leads") != NULL);
    game.vm.status = COK_ECL_OK;
}

/* Remove Curse outside a cast (cok_cast_remove_curse: the temple's, camp's
 * Fix): Death Knights' cures 0x2b ("is uncursed"), not Bestow Curse's
 * 0x24 (5dc5:311b). */
static void test_remove_curse(void)
{
    reset();
    cok_character *t = record('T', 0, 5, 5);
    game.vm.mode = 4;
    CHECK(cok_character_add_effect(t, 0x24, 0, 5, false) != NULL);
    CHECK(cok_cast_remove_curse(&game, t) && cok_character_find_effect(t, 0x24) != NULL);
    CHECK(strstr(log_text, "uncursed") == NULL);
    CHECK(cok_character_add_effect(t, 0x2b, 0, 5, false) != NULL);
    CHECK(cok_cast_remove_curse(&game, t) && cok_character_find_effect(t, 0x2b) == NULL);
    CHECK(strstr(log_text, "print: is uncursed;") != NULL && cok_character_find_effect(t, 0x24));
    game.vm.mode = 5;
}

/* Fear (5dc5:4566): one that fails its throw but whose effects cancel the
 * pending 0x6f (event 9: 0x5c, free action) is "unaffected" and does not
 * flee: the computer's flag (+0xca), +0x3e and its target stay (65cd:2687
 * finds no 0x6f); one that takes 0x6f "runs away" and flees. T is in the
 * cone toward P, the computer's pick. */
static void test_fear(void)
{
    const cok_character_layout *L = R->character;
    for (int fearless = 1; fearless >= 0; --fearless) {
        bool seen = false;
        for (uint32_t seed = 1; seed < 400 && !seen; ++seed) {
            reset();
            cok_character *w = record('W', 0, 5, 5), *t = record('T', 1, 6, 5);
            record('P', 1, 8, 5); /* the computer's pick (5340:3fd5: T is of no use) */
            cok_field_set8_at(L, w->record, COK_FIELD_CLASS_LEVEL, 5, 9);
            cok_field_set8(L, w->record, COK_FIELD_COMPUTER, 1);
            if (fearless) CHECK(cok_character_add_effect(t, 0x5c, 0, 0xff, false) != NULL);
            /* Only a 20 saves: "is unaffected" is then 0x5c's (the save made
             * says the same). */
            cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, 21);
            t->combat->target = w->record;
            game.vm.seed = seed;
            computer_casts(w, 0x54);
            CHECK(game.vm.status == COK_ECL_OK);
            if (fearless && strstr(log_text, "print: T;print: is unaffected;") != NULL) {
                seen = true;
                CHECK(cok_field_u8(L, t->record, COK_FIELD_COMPUTER) == 0 &&
                      cok_field_u8(L, t->record, COK_FIELD_NPC) == 0 &&
                      t->combat->target == w->record && t->combat->forced == 0);
            } else if (!fearless && strstr(log_text, "print: T;print: runs away;") != NULL) {
                seen = true;
                CHECK(cok_field_u8(L, t->record, COK_FIELD_COMPUTER) == 1 &&
                      cok_field_u8(L, t->record, COK_FIELD_NPC) == 0xb3 &&
                      t->combat->target == NULL && t->combat->forced == 1);
            }
        }
        CHECK(seen);
    }
}

/* Faerie Fire (5dc5:4391, 1b20 with no bonus): of those picked, the last
 * counts as saved (kind 1, "Unaffected"); the others throw (no bonus,
 * against 11) and are highlighted (0x07) when they fail: over 200 seeds
 * FAERIE_FIRE_HIGHLIGHTED times (a bonus of 1 gives 169). */
static void test_faerie_fire(void)
{
    const cok_character_layout *L = R->character;
    unsigned highlighted = 0;
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        reset();
        cok_character *w = record('W', 0, 5, 5);
        cok_character *t[3] = {record('A', 1, 6, 5), record('B', 1, 6, 6), record('C', 1, 6, 4)};
        cok_field_set8_at(L, w->record, COK_FIELD_CLASS_LEVEL, 1, 9);
        cok_field_set8(L, w->record, COK_FIELD_COMPUTER, 1);
        for (int k = 0; k < 3; ++k)
            for (size_t type = 0; type < 5; ++type)
                cok_field_set8_at(L, t[k]->record, COK_FIELD_SAVING_THROW, type, 11);
        game.vm.seed = seed;
        computer_casts(w, 0x4f);
        CHECK(game.vm.status == COK_ECL_OK);
        for (int k = 0; k < 3; ++k) highlighted += cok_character_find_effect(t[k], 0x07) != NULL;
    }
    CHECK(highlighted == FAERIE_FIRE_HIGHLIGHTED);
}

/* The clouds (dkk 5dc5:2934, 638c:0c65, 21ec, 5dc5:2e5c): a terrain value
 * of the battlefield that costs 0xff (a wall). */
static uint8_t wall(void)
{
    for (uint8_t v = 1; v < cok_data_count(R->data, COK_DATA_TERRAIN, 0); ++v)
        if (cok_combat_terrain(R, v).cost == 0xff) return v;
    CHECK(false);
    return 0;
}

/* A terrain value from 0x20 (WILDCOM's) that may be walked on. */
static uint8_t ground(void)
{
    for (uint8_t v = 0x20; v < cok_data_count(R->data, COK_DATA_TERRAIN, 0); ++v)
        if (cok_combat_terrain(R, v).cost < 0xff) return v;
    CHECK(false);
    return 0;
}

/* W, a mage of level level, lays a cloud at x, y: Stinking Cloud's (0x22)
 * or Cloud Kill's (0x5b, kill). */
static bool lay(cok_character *w, int8_t x, int8_t y, bool kill)
{
    game.vm.character = w->record;
    game.effects.rolls.spell = kill ? 0x5b : 0x22;
    game.spell_x = x;
    game.spell_y = y;
    log_text[0] = '\0';
    return cok_cloud_stinking(&game, kill);
}

static cok_character *mage(char name, uint8_t level, int8_t x, int8_t y)
{
    cok_character *w = record(name, 0, x, y);
    cok_field_set8_at(R->character, w->record, COK_FIELD_CLASS_LEVEL, 5, level);
    return w;
}

/* 5dc5:2934: Stinking Cloud covers the cell and east, south-east and
 * south of it, Cloud Kill the nine around it; a wall and a cell off the
 * map are not covered; the node goes first, its rounds and level the
 * caster level, the cell under kept (a body's cell, an earlier cloud's
 * terrain); no word is said; each one on a cell, once, is in it. */
static void test_cloud_lay(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = mage('W', 7, 2, 2);
    cok_character *t = record('T', 1, 11, 11);
    cok_field_set8(L, t->record, COK_FIELD_UNDEAD, 1); /* undead: no throw */
    uint8_t v = wall(), g = ground();
    game.combat.cells[10][11] = v;
    game.combat.cells[11][10] = COK_COMBAT_BODY;
    game.combat.body[0] = (cok_combat_body){NULL, 10, 11, g};
    game.combat.bodies = 1;
    uint32_t seed = game.vm.seed;
    CHECK(lay(w, 10, 10, false));
    const cok_combat_cloud *p = game.combat.clouds;
    CHECK(p != NULL && p->next == NULL && p->x == 10 && p->y == 10 && p->caster == w->record);
    CHECK(p->age == 7 && p->level == 7 && p->tile == 0x1d);
    CHECK(p->covered[1] && !p->covered[2] && p->covered[3] && p->covered[4] && !p->covered[5]);
    CHECK(p->saved[1] == COK_COMBAT_FLOOR && p->saved[2] == v && p->saved[4] == g);
    CHECK(game.combat.cells[10][10] == 0x1d && game.combat.cells[10][11] == v &&
          game.combat.cells[11][11] == 0x1d && game.combat.cells[11][10] == 0x1d &&
          game.combat.cells[11][9] == COK_COMBAT_FLOOR);
    CHECK(strstr(log_text, "noxious") == NULL && t->effects == NULL && game.vm.seed == seed);
    /* Cloud Kill over it at 11, 11: its cells keep the terrain under the
     * first cloud, its seventh (north-west) the body's cell; one off the
     * map (x -1) is not covered. */
    CHECK(lay(w, 11, 11, true));
    const cok_combat_cloud *q = game.combat.clouds;
    CHECK(q != p && q->next == p && q->tile == 0x1c);
    for (unsigned k = 1; k <= 9; ++k) CHECK(q->covered[k] == (k != 8)); /* 8: north, the wall */
    CHECK(q->saved[1] == COK_COMBAT_FLOOR && q->saved[6] == g &&
          game.combat.cells[11][11] == 0x1c &&
          game.combat.cells[10][10] == 0x1c && game.combat.cells[12][12] == 0x1c);
    CHECK(lay(w, 0, 5, true));
    CHECK(!game.combat.clouds->covered[5] && !game.combat.clouds->covered[6] &&
          !game.combat.clouds->covered[7] && game.combat.clouds->covered[8]);
    cok_cloud_free(&game.combat);
}

/* 638c:0c65 in Stinking Cloud's, with the step: saved, "starts to cough",
 * 0x1e (its handler: "is coughing"), and one casting "lost a spell", the
 * spell marked and its turn over; failed, "chokes and gags from nausea",
 * 0x1f; the selected character kept; without the step nothing. */
static void test_cloud_cough(void)
{
    const cok_character_layout *L = R->character;
    for (int made = 1; made >= 0; --made) {
        bool seen = false;
        for (uint32_t s = 1; s < 200 && !seen; ++s) {
            reset();
            cok_character *w = mage('W', 7, 2, 2);
            cok_character *t = record('T', 1, 10, 10);
            for (size_t type = 0; type < 5; ++type)
                cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 11);
            cok_field_set8_at(L, t->record, COK_FIELD_MEMORIZED, 0, 0x0f);
            t->combat->spell = 0x0f;
            t->combat->initiative = 5;
            t->combat->may_use = 1;
            game.vm.seed = s;
            CHECK(lay(w, 10, 10, false) && game.vm.status == COK_ECL_OK);
            CHECK(game.vm.character == w->record);
            if (made && cok_character_find_effect(t, 0x1e) != NULL) {
                seen = true;
                CHECK(strstr(log_text, "print: T;print: starts to cough;") != NULL &&
                      strstr(log_text, "is coughing;") != NULL &&
                      strstr(log_text, "print: lost a spell;") != NULL);
                CHECK(cok_field_u8_at(L, t->record, COK_FIELD_MEMORIZED, 0) == 0x8f &&
                      t->combat->may_cast == 0 && t->combat->initiative == 0);
            } else if (!made && cok_character_find_effect(t, 0x1f) != NULL) {
                seen = true;
                CHECK(strstr(log_text, "print: chokes and gags from nausea;") != NULL &&
                      strstr(log_text, "lost a spell") == NULL);
            }
            /* Out of the step (the round's end), nothing more. */
            log_text[0] = '\0';
            uint32_t before = game.vm.seed;
            CHECK(cok_cloud_square(&game, t, false) && game.vm.seed == before &&
                  log_text[0] == '\0');
            cok_cloud_free(&game.combat);
        }
        CHECK(seen);
    }
}

/* 638c:0c65 in Cloud Kill's, with the step or not: levels 0-4 die ("is
 * poisoned", 0x37, "is killed", status 6), 5 throw at -4, 6 throw, 7 and
 * up are untouched. */
static void test_cloud_kill(void)
{
    const cok_character_layout *L = R->character;
    unsigned died[9] = {0};
    for (uint32_t s = 1; s <= 40; ++s)
        for (int8_t level = 0; level <= 8; ++level) {
            reset();
            cok_character *w = mage('W', 7, 2, 2);
            cok_character *t = record('T', 1, 20, 20);
            cok_field_set8(L, t->record, COK_FIELD_HIGHEST_LEVEL, (uint8_t)level);
            for (size_t type = 0; type < 5; ++type)
                cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, type, 11);
            game.vm.seed = s;
            CHECK(lay(w, 21, 21, true) && game.vm.status == COK_ECL_OK);
            bool dead = cok_field_u8(L, t->record, COK_FIELD_STATUS) == 6;
            CHECK(dead == (cok_character_find_effect(t, 0x37) != NULL));
            CHECK(!dead || (strstr(log_text, "print: T;print: is poisoned;") != NULL &&
                            strstr(log_text, "is killed") != NULL));
            died[level] += dead;
            if (dead) continue;
            /* The round's end: the same throw again for 5 and 6. */
            CHECK(cok_cloud_square(&game, t, false));
            cok_cloud_free(&game.combat);
        }
    for (int level = 0; level <= 4; ++level) CHECK(died[level] == 40);
    CHECK(died[5] > died[6] && died[6] > 0 && died[6] < 40);
    CHECK(died[7] == 0 && died[8] == 0);
}

/* 638c:21ec at the round's end: a cloud of level 2 lasts one round, then
 * its cells get back their terrain, a body's tile where a body lies, and
 * "the air clears a little..."; where an earlier cloud overlaps, its tile
 * is laid again (213c). */
static void test_cloud_age(void)
{
    reset();
    cok_character *w = mage('W', 9, 2, 2), *a = mage('A', 2, 3, 3);
    CHECK(lay(w, 10, 10, false));
    CHECK(lay(a, 11, 10, true));
    game.combat.body[0] = (cok_combat_body){w, 12, 11, COK_COMBAT_FLOOR};
    game.combat.bodies = 1;
    CHECK(game.combat.cells[10][11] == 0x1c);
    /* Under 11, 10, both clouds' cell: the last in the list's (the older)
     * kept terrain (6f03:152e). */
    cok_combat_cloud *older = game.combat.clouds->next;
    game.combat.clouds->saved[1] = 0x20;
    CHECK(cok_cloud_under(&game, 11, 10, 0x1c) == older->saved[2] && older->saved[2] != 0x20);
    log_text[0] = '\0';
    CHECK(cok_cloud_round(&game) && game.combat.clouds->age == 1 &&
          game.combat.clouds->next->age == 8 && log_text[0] == '\0');
    CHECK(cok_cloud_round(&game) && game.combat.clouds != NULL &&
          game.combat.clouds->next == NULL && game.combat.clouds->tile == 0x1d);
    CHECK(strstr(log_text, "print: the air clears a little...;") != NULL);
    CHECK(game.combat.cells[10][11] == 0x1d && game.combat.cells[11][11] == 0x1d &&
          game.combat.cells[10][12] == COK_COMBAT_FLOOR &&
          game.combat.cells[11][12] == COK_COMBAT_BODY);
    /* The cell under a cloud and a body (6f03:152e). */
    CHECK(cok_cloud_under(&game, 11, 11, 0x1d) == COK_COMBAT_FLOOR &&
          cok_cloud_under(&game, 12, 11, COK_COMBAT_BODY) == COK_COMBAT_FLOOR &&
          cok_cloud_under(&game, 13, 13, 0x1d) == 0x1d);
    cok_cloud_free(&game.combat);
}

/* 5dc5:2e5c's clouds: each cloud with a cell on a cloud's tile around the
 * spell's cell is tried, a d100 up to 50 + 5 a level above it; taken, it
 * goes after the walk; against a higher level the stale byte DS:8954
 * (the last effect level tried), unknown stopping the run. */
static void test_cloud_dispel(void)
{
    reset();
    cok_character *w = mage('W', 4, 2, 2);
    CHECK(lay(w, 10, 10, false));
    game.vm.character = w->record;
    game.spell_x = 10;
    game.spell_y = 10;
    /* Level 6 against 4: 60. The walk meets its four cells (1 and 2 of
     * the walk: the cells north-east and east of 10, 10 are not; 10, 10
     * is the walk's last). */
    const unsigned taken[] = {100, 1, 60};
    game.vm.seed = seed_seq(1, taken);
    uint32_t expect = game.vm.seed;
    for (int k = 0; k < 4; ++k) cok_dice(&expect, 1, 100);
    log_text[0] = '\0';
    CHECK(cok_cloud_dispel(&game, 6, -1) && game.combat.clouds == NULL);
    CHECK(game.vm.seed == expect && game.combat.cells[10][10] == COK_COMBAT_FLOOR);
    CHECK(strstr(log_text, "the air clears a little...") != NULL);
    /* Level 2 against 4 reads DS:8954: unknown, the run stops; 3 gives 48
     * (49 at each of its four cells keeps it). */
    CHECK(lay(w, 10, 10, false));
    game.vm.character = w->record;
    CHECK(!cok_cloud_dispel(&game, 2, -1) && strstr(game.error, "DS:8954") != NULL);
    game.vm.status = COK_ECL_OK;
    const unsigned kept[] = {100, 49, 100, 100, 49, 100, 100, 49, 100, 100, 49, 100};
    game.vm.seed = seed_seq(4, kept);
    CHECK(cok_cloud_dispel(&game, 2, 3) && game.combat.clouds != NULL);
    /* 47 or 48 takes it (the cloud's own level would give 46). */
    const unsigned gone[] = {100, 47, 48, 100, 49, 100, 100, 49, 100, 100, 49, 100};
    game.vm.seed = seed_seq(4, gone);
    CHECK(cok_cloud_dispel(&game, 2, 3) && game.combat.clouds == NULL);
    /* 59 or 60 takes a cloud of level 4 at 6 (5 a level). */
    CHECK(lay(w, 10, 10, false));
    game.vm.character = w->record;
    const unsigned five[] = {100, 59, 60, 100, 61, 100, 100, 61, 100, 100, 61, 100};
    game.vm.seed = seed_seq(4, five);
    CHECK(cok_cloud_dispel(&game, 6, -1) && game.combat.clouds == NULL);
    /* On Stinking Cloud's tile a Cloud Kill's four first cells are tried,
     * not its nine: its north-west cell (9, 9) given that tile is no try,
     * so eight d100s for the walk's eight others. */
    CHECK(lay(w, 10, 10, true));
    game.vm.character = w->record;
    game.combat.cells[9][9] = 0x1d;
    uint32_t start = 7, eight = start;
    for (int k = 0; k < 8; ++k) cok_dice(&eight, 1, 100);
    game.vm.seed = start;
    CHECK(cok_cloud_dispel(&game, 20, -1) && game.vm.seed == eight);
    cok_cloud_free(&game.combat);
}

/* The handlers (cast.c): Cloud Kill's entry (value 1) lays Cloud Kill's
 * cloud; Dispel Magic passes the level of the last effect its first part
 * tried as DS:8954 (3 here, so that a cloud of a higher level is tried,
 * 48, without the run stopping). */
static void test_cloud_handlers(void)
{
    reset();
    cok_character *w = mage('W', 4, 2, 2), *t = record('T', 1, 20, 20);
    game.vm.character = w->record;
    game.spell_x = 10;
    game.spell_y = 10;
    CHECK(cok_cast_on(&game, 0x5b, w, true) && game.combat.clouds != NULL &&
          game.combat.clouds->tile == 0x1c && game.combat.clouds->covered[9]);
    cok_cloud_free(&game.combat);
    CHECK(cok_cast_on(&game, 0x22, w, true) && game.combat.clouds != NULL &&
          game.combat.clouds->tile == 0x1d && !game.combat.clouds->covered[5]);
    /* NEACAIL-like cleric of level 2 against the cloud of 4. */
    cok_character *n = record('N', 0, 3, 3);
    cok_field_set8_at(R->character, n->record, COK_FIELD_CLASS_LEVEL, 0, 2);
    CHECK(cok_character_add_effect(t, 0x2f, 5, 0x03, false) != NULL);
    game.spell_targets[1] = t;
    game.spell_target_count = 1;
    game.vm.character = n->record;
    CHECK(cok_cast_on(&game, 0x29, t, true) && game.vm.status == COK_ECL_OK);
    cok_cloud_free(&game.combat);
}

/* 6f03:0fbd: one dying in a cloud keeps the cloud's tile, its body the
 * terrain under the cloud; the cloud gone (two rounds for level 2), the
 * body's tile. */
static void test_cloud_body(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = mage('W', 2, 2, 2), *a = record('A', 0, 10, 10);
    record('M', 1, 20, 20);
    CHECK(lay(w, 10, 10, true));
    cok_field_set8(L, a->record, COK_FIELD_ACTIVE, 0);
    cok_field_set8(L, a->record, COK_FIELD_HIT_POINTS, 0);
    CHECK(cok_arena_kill(&game, a, COK_ARENA_STALE_UNKNOWN, NULL));
    CHECK(game.combat.bodies == 1 && game.combat.body[0].cell == COK_COMBAT_FLOOR &&
          game.combat.cells[10][10] == 0x1c);
    CHECK(cok_cloud_round(&game) && cok_cloud_round(&game) && game.combat.clouds == NULL &&
          game.combat.cells[10][10] == COK_COMBAT_BODY);
}

/* The battle's pictures and tiles loaded as a setup loads them. */
static void load_screen(void)
{
    reset();
    record('A', 0, 0, 0);
    record('M', 1, 0, 0);
    game.vm.mode = 4;
    cok_ecl_set_var(&game.vm, COK_VAR_PARTY_SIZE, 2);
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(cok_combat_setup(&game));
    CHECK(cok_combat_end(&game));
    game.vm.mode = 5;
}

int main(void)
{
    struct stat st;
    if (stat("DKKAssets/START.EXE", &st) != 0) {
        puts("fight: no DKKAssets/START.EXE; Death Knights' fight not checked");
        return 0;
    }
    cok_keyboard keys = {no_key, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line};
    CHECK(cok_adventure_open(&game, R, "DKKAssets", &keys, &hooks));
    load_screen();
    test_probe();
    test_kill();
    test_turning();
    test_smoke();
    test_half();
    test_movement();
    test_alt_a();
    test_cast_choice();
    test_aim();
    test_hidden();
    test_ally();
    test_charm_person();
    test_fire_shield();
    test_remove_curse();
    test_fear();
    test_faerie_fire();
    test_cloud_lay();
    test_cloud_cough();
    test_cloud_kill();
    test_cloud_age();
    test_cloud_dispel();
    test_cloud_body();
    test_cloud_handlers();
    test_strength_unset();
    test_leadership_unset();
    cok_adventure_close(&game);
    puts("fight tests passed");
    return 0;
}
