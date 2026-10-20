/* Death Knights' fight where it differs from Champions' (the descriptor's
 * fight section): its cloud and pool tiles, the death that finds a body
 * already there, turning undead's levels and count, the nightmares' smoke
 * (effect 0x6a), half damage from weapons of kind 0 or 1 (0x5d), Alt-A at
 * the player's turn, the computer's spell choice, aiming's range and first
 * target, attacking an ally, the movement a round; on records made by
 * hand in its layout, the game opened on DKKAssets/ (skipped without). */
#include "adventure.h"
#include "ai.h"
#include "arena.h"
#include "attack.h"
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
    cok_adventure_close(&game);
    puts("fight tests passed");
    return 0;
}
