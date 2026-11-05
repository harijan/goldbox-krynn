/* The computer's step where the Dark Queen of Krynn's differs from Champions' and Death Knights'
 * (dqk 5bb8:0438 may it step, 06b0's step and its no-move tail, 52db:06c0's step sound, 0e26's
 * flight, 08e6's free attacks, and the ways table DS:5db4): on records made by hand in its layout,
 * the game opened on DQKAssets/ (skipped without). */
#include "adventure.h"
#include "ai.h"
#include "attack.h"
#include "combat.h"
#include "data.h"
#include "field.h"
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

/* A party record (hostile 0) or a monster (hostile 1) in Dark Queen's layout, named by a letter,
 * a fighter of level 3, with a combat record of its own, one in battle, not an NPC. */
static cok_character *record(char name, uint8_t hostile, int npc)
{
    const cok_character_layout *L = R->character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL && cok_party_append(&game.party, c));
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    cok_field_set8_at(L, c->record, COK_FIELD_NAME, 0, 1);
    cok_field_set8_at(L, c->record, COK_FIELD_NAME, 1, (uint8_t)name);
    cok_field_set8(L, c->record, COK_FIELD_HOSTILE, hostile);
    cok_field_set8(L, c->record, COK_FIELD_NPC, npc ? 0x80 : 0);
    cok_field_set8(L, c->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE, 10);
    cok_field_set8_at(L, c->record, COK_FIELD_CLASS_LEVEL, 1, 3);
    cok_field_set8(L, c->record, COK_FIELD_HIT_POINTS, 10);
    c->combat->initiative = 5;
    c->combat->movement = 12;
    c->combat->pattern = 1;
    c->combat->may_cast = 0;
    return c;
}

/* A record placed at x, y on the battle's map as a combatant of size 1 (test_queen_fight's place). */
static void place(cok_character *c, int8_t x, int8_t y)
{
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
}

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
    k->active = true;
    k->stuck = k->last_step = 0;
    k->magic_on = false;
    game.ambush = 0;
    game.error[0] = '\0';
    game.vm.status = COK_ECL_OK;
    game.vm.character = NULL;
    log_text[0] = '\0';
    cok_party_free(&game.party);
}

/* The seed after n draws of d(sides) from seed. */
static uint32_t after_draws(uint32_t seed, unsigned n, uint8_t sides)
{
    for (unsigned i = 0; i < n; ++i) cok_dice(&seed, 1, sides);
    return seed;
}

/* A monster at x, y (hostile, movement 12, initiative 5) with the way's cell at x + 1, y (dir 2 from
 * try 1 of way 1, which turns straight on) set to cell. */
static cok_character *mover(uint8_t cell)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    game.combat.cells[10][11] = cell;
    m->combat->movement = 9;
    return m;
}

/* Whether a monster may step straight east (try 1 of way 1, dir 2: dqk 5bb8:0438), with no draw. */
static bool may_step_east(cok_character *m)
{
    bool off = true, ok = true;
    uint32_t seed = game.vm.seed;
    CHECK(cok_ai_can_step(&game, m, 1, 2, &off, &ok) && !off);
    CHECK(game.vm.seed == seed);
    return ok;
}

/* The way of moving's rows (DS:5db4 + 6 * way + try, 5bb8:0902's table; rows 1-6, tries 1-5). */
static void test_ways(void)
{
    const cok_data_segment *D = R->data;
    CHECK(cok_data_u8_at2(D, COK_DATA_WAYS, 1, 1) == 8);  /* way 1: 0, -1, -2, +1, +2 */
    CHECK(cok_data_u8_at2(D, COK_DATA_WAYS, 2, 3) == 2);  /* way 2: 0, +1, +2, -1, -2 */
    CHECK(cok_data_u8_at2(D, COK_DATA_WAYS, 3, 2) == 1);  /* way 3: -1, +1, 0, -2, +2 */
    CHECK(cok_data_u8_at2(D, COK_DATA_WAYS, 6, 5) == 4);  /* way 6: 0, +1, +2, +3, +4 */
}

/* 5bb8:0438: a step onto a cloud kind (0x1b, 0x1c, 0x1d) or acid (0x46) costs all the movement,
 * unless forced or made safe by the effects and bits the original tests (the record's +0xc8 for
 * kinds 0x1c and 0x1d). Floor is free, and a cloud is no bar for a forced record. */
static void test_can_step(void)
{
    cok_character *m = mover(0x17);
    CHECK(may_step_east(m));
    /* Kind 0x1b: all the movement, unless forced. */
    m = mover(0x1b);
    CHECK(!may_step_east(m));
    m->combat->forced = 1;
    CHECK(may_step_east(m));
    /* Kind 0x1c: not with 0x39 or 0x8f, nor +0xc8 & 0x10; forced frees it. */
    m = mover(0x1c);
    CHECK(!may_step_east(m));
    CHECK(cok_character_add_effect(m, 0x39, 0, 0, false) != NULL);
    CHECK(may_step_east(m));
    m = mover(0x1c);
    CHECK(cok_character_add_effect(m, 0x8f, 0, 0, false) != NULL);
    CHECK(may_step_east(m));
    m = mover(0x1c);
    m->record[R->effects.damage_flags_at] = 0x10;
    CHECK(may_step_east(m));
    m = mover(0x1c);
    m->combat->forced = 1;
    CHECK(may_step_east(m));
    /* Kind 0x1d: one of 0x1e, 0xa0, 0x8f, 0x3f, 0x39 frees it, or +0xc8 & 0x18 (0x04 is not
     * enough for it, 0x08 and 0x10 are); forced frees it. */
    static const uint8_t ids[] = {0x1e, 0xa0, 0x8f, 0x3f, 0x39};
    for (size_t i = 0; i < sizeof ids; ++i) {
        m = mover(0x1d);
        CHECK(!may_step_east(m));
        CHECK(cok_character_add_effect(m, ids[i], 0, 0, false) != NULL);
        CHECK(may_step_east(m));
    }
    m = mover(0x1d);
    m->record[R->effects.damage_flags_at] = 0x04;
    CHECK(!may_step_east(m));
    m->record[R->effects.damage_flags_at] = 0x08;
    CHECK(may_step_east(m));
    m = mover(0x1d);
    m->record[R->effects.damage_flags_at] = 0x10;
    CHECK(may_step_east(m));
    m = mover(0x1d);
    m->combat->forced = 1;
    CHECK(may_step_east(m));
    /* Acid: free only with a highest level of 7 or more (unsigned), or forced. */
    m = mover(0x46);
    CHECK(!may_step_east(m));
    cok_field_set8(R->character, m->record, COK_FIELD_HIGHEST_LEVEL, 7);
    CHECK(may_step_east(m));
    /* 5bb8:0438 tests the level with cmp $7 and jae: 0x80 is no less than 7, so it is free too
     * (a signed read would take it for -128 and refuse). */
    m = mover(0x46);
    cok_field_set8(R->character, m->record, COK_FIELD_HIGHEST_LEVEL, 0x80);
    CHECK(may_step_east(m));
    m = mover(0x46);
    m->combat->forced = 1;
    CHECK(may_step_east(m));
    /* A cost of the movement or more refuses, with no mark to pay. */
    m = mover(0x17);
    m->combat->movement = 2;
    CHECK(!may_step_east(m));
    battle_clear();
}

/* 5bb8:06b0 steps a monster east toward the party's record, to the sound of 52db:06c0 (0x0b). */
static void test_step_sound(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 13, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->target = a->record;
    game.vm.character = m->record;
    CHECK(cok_ai_step(&game, m) && game.combat.combatant[2].x == 11);
    CHECK(LOGGED("sound: 11;") && !LOGGED("sound: 10;"));
    battle_clear();
}

/* 5bb8:0af0: with no movement and not fleeing, the computer's spell choice (5bb8:0306) first. With
 * Magic off (or may_cast 0) a monster guards; with Magic on and may_cast 1 the choice is a cited stop. */
static void test_no_move(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->movement = 0;
    game.vm.character = m->record;
    CHECK(cok_ai_step(&game, m) && m->combat->guarding == 1 && m->combat->initiative == 0);
    m->combat->initiative = 5;
    m->combat->guarding = 0;
    m->combat->may_cast = 1;
    game.combat.magic_on = false;
    CHECK(cok_ai_step(&game, m) && m->combat->guarding == 1);
    /* A class 5 holds whatever its slot 2 (5bb8:06b0 has no slot test): with an item there it
     * still guards, and does not step toward the party's record east of it. */
    m->combat->initiative = 5;
    m->combat->guarding = 0;
    m->combat->may_cast = 0;
    m->combat->movement = 12;
    cok_field_set8(R->character, m->record, COK_FIELD_CLASS, 5);
    m->slots[2] = 1;
    CHECK(cok_ai_step(&game, m) && m->combat->guarding == 1 && game.combat.combatant[1].x == 10);
    m->slots[2] = 0;
    cok_field_set8(R->character, m->record, COK_FIELD_CLASS, 3);
    m->combat->initiative = 5;
    m->combat->guarding = 0;
    m->combat->movement = 0;
    m->combat->may_cast = 1;
    game.combat.magic_on = true;
    CHECK(!cok_ai_step(&game, m) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "dqk 5bb8:0306") != NULL && m->combat->guarding == 0);
    battle_clear();
}

/* 52db:0e26: an ambush byte of 0x20 blocks the flight ("Escape is blocked"), where no one else is
 * near enough to stop it; with it clear the flight is made ("Got Away", status 3). */
static void test_flee_ambush(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 5, 5);
    a->combat->fleeing = 1;
    game.ambush = 0x20;
    /* The notice (177e:153c) waits the speed's time once: one random(1000) (dark_queen.c's
     * pass). */
    game.vm.seed = 9;
    uint32_t one = 9;
    cok_tp_random(&one, 1000);
    CHECK(cok_combat_flee(&game, a) && LOGGED("message: Escape is blocked;") && !LOGGED("Got Away"));
    CHECK(game.vm.seed == one);
    CHECK(cok_field_u8(R->character, a->record, COK_FIELD_ACTIVE) == 1 && a->combat->initiative == 0);
    battle_clear();
    a = record('A', 0, 0);
    place(a, 5, 5);
    a->combat->fleeing = 1;
    CHECK(cok_combat_flee(&game, a) && LOGGED("Got Away") && !LOGGED("Escape is blocked"));
    CHECK(cok_field_u8(R->character, a->record, COK_FIELD_STATUS) == 3 &&
          cok_field_u8(R->character, a->record, COK_FIELD_ACTIVE) == 0);
    battle_clear();
}

/* 52db:08e6: a neighbour holding 0xd5 (one of the effects that bar the blow, free_attack_barred) is
 * not struck by the free attack: no attack is made, no draw, and its target is kept. A party record
 * steps south (dir 4) away from a monster beside it. */
static void test_free_attack(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 10, 10);
    cok_character *e = record('E', 1, 0);
    place(e, 10, 9);
    cok_character *x = record('X', 0, 0);
    e->combat->facing = 0;
    e->combat->target = x->record;
    CHECK(cok_character_add_effect(e, 0xd5, 0, 0, false) != NULL);
    uint32_t seed = game.vm.seed = 5;
    CHECK(cok_combat_opportunity(&game, a, 4));
    CHECK(!LOGGED("Attacks") && !LOGGED("Misses") && game.vm.seed == seed);
    CHECK(e->combat->target == x->record && e->combat->initiative == 5);
    battle_clear();
}

/* The seed whose first d(sides) draw is value. */
static uint32_t seed_with(uint8_t sides, uint8_t value)
{
    for (uint32_t seed = 1;; ++seed) {
        uint32_t s = seed;
        if (cok_dice(&s, 1, sides) == value) return seed;
    }
}

/* The unbarred free attack (52db:08e6 with no 0xd5 by the monster): the blow is made, its hit roll
 * drawn (Dark Queen's bonus, var 0x01, read: the port asserted here before the fight descriptor had
 * it), and the attack is said, a miss or a hit. A party record steps north (dir 0) away from a
 * monster to its south-west; both poses face 0 to 3 (the blow's pictures are not drawn yet: a
 * facing above 3 with no icon stops), the party record's from its facing (hit twice, so kept). */
static void test_free_attack_blow(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 10, 10);
    cok_character *e = record('E', 1, 0);
    place(e, 9, 11);
    cok_character *x = record('X', 0, 0);
    a->combat->hits = 2;
    a->combat->facing = 0;
    e->combat->facing = 0;
    e->combat->target = x->record;
    /* The attack's 100 ms wait draws before its d20, so the seed's first draw is the wait's: the
     * d20 is some later draw, not known to be 1. The check takes a hit or a miss. */
    game.vm.seed = seed_with(20, 1);
    uint32_t seed = game.vm.seed;
    CHECK(cok_combat_opportunity(&game, a, 0) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed != seed && (LOGGED("and Misses") || LOGGED("Hitting for")));
    battle_clear();
}

/* 5bb8:06b0's stuck counter, a monster walled in by eight party records (every try refused by an
 * occupant): the stuck count goes up and the way of moving on (pattern % 6 + 1); at 2 the target is
 * dropped and picked again among the eight (52db:3d9e: two draws, the unused one and the pick; one-
 * sided draws would leave the seed alone); at 3 the movement goes, with no draw. */
static void test_stuck(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 9, 5);
    cok_character *first = NULL;
    for (int y = 4; y <= 6; ++y)
        for (int x = 8; x <= 10; ++x)
            if (y != 5 || x != 9) {
                cok_character *a = record('A', 0, 0);
                place(a, (int8_t)x, (int8_t)y);
                if (first == NULL) first = a;
            }
    m->combat->target = first->record;
    game.vm.character = m->record;
    uint32_t seed = game.vm.seed = 3;
    CHECK(cok_ai_step(&game, m) && game.combat.stuck == 1 && m->combat->pattern == 2);
    CHECK(m->combat->target == first->record && game.vm.seed == seed);
    CHECK(cok_ai_step(&game, m) && game.combat.stuck == 2 && m->combat->pattern == 3);
    CHECK(m->combat->target != NULL && game.vm.seed == after_draws(seed, 2, 8));
    seed = game.vm.seed;
    CHECK(cok_ai_step(&game, m) && game.combat.stuck == 3 && m->combat->pattern == 4);
    CHECK(m->combat->movement == 0 && m->combat->target == NULL && game.vm.seed == seed);
    battle_clear();
}

/* 5bb8:06b0's square (148f:12ab(c, 0, 0)): a cloud of kind 0x1b, 0x1c or 0x1d, or acid, stops at a
 * step (cited); at the round's end (148f:12ab(rec, 0, 1)) kind 0x1d does not, and the rest do. */
static void test_square(void)
{
    static const uint8_t cells[] = {0x1b, 0x1c, 0x1d, 0x46};
    for (size_t i = 0; i < sizeof cells; ++i) {
        battle_clear();
        cok_character *m = record('M', 1, 0);
        place(m, 10, 10);
        game.combat.cells[10][10] = cells[i];
        CHECK(!cok_round_square(&game, m, true) && game.vm.status == COK_ECL_UNPORTED &&
              strstr(game.error, "148f:12ab") != NULL);
        game.vm.status = COK_ECL_OK;
        game.error[0] = '\0';
        bool stops = cells[i] != 0x1d;
        CHECK(cok_round_square(&game, m, false) == !stops &&
              game.vm.status == (stops ? COK_ECL_UNPORTED : COK_ECL_OK));
        game.vm.status = COK_ECL_OK;
        game.error[0] = '\0';
    }
    battle_clear();
}

int main(void)
{
    struct stat st;
    if (stat("DQKAssets/DISK2/DUNGCOM.TLB", &st) != 0) {
        puts("queen ai: no DQKAssets/; the computer's step not checked");
        return 0;
    }
    cok_keyboard keys = {no_key, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, &hooks));
    /* The battle's ground (set 1: no palette cycle, so no draw). */
    CHECK(cok_adventure_drawing(&game)->arena_tiles(&game, false, 1));
    test_ways();
    test_can_step();
    test_step_sound();
    test_no_move();
    test_flee_ambush();
    test_free_attack();
    test_free_attack_blow();
    test_stuck();
    test_square();
    cok_party_free(&game.party);
    puts("queen ai tests passed");
    return 0;
}
