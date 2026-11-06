/* The computer's step where the Dark Queen of Krynn's differs from Champions' and Death Knights'
 * (dqk 5bb8:0438 may it step, 06b0's step and its no-move tail, 52db:06c0's step sound, 0e26's
 * flight, 08e6's free attacks, and the ways table DS:5db4): on records made by hand in its layout,
 * the game opened on DQKAssets/ (skipped without). */
#include "adventure.h"
#include "ai.h"
#include "attack.h"
#include "combat.h"
#include "data.h"
#include "ecl.h"
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

/* queen_computer (dqk 5bb8:0000) on records made by hand: each step, its stop, and that the
 * pattern's draws are ai.c's (battle_clear between: the party is freed). */
static cok_character *computer_pair(uint8_t hostile_m, cok_character **a_out)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    cok_character *m = record('M', hostile_m, 0);
    place(a, 12, 10);
    place(m, 10, 10);
    game.undead = 5;
    m->combat->target = a->record;
    *a_out = a;
    return m;
}

/* 5bb8:0031: effect 0xd5 ends the turn with no draw (the facing effect never acts). */
static void test_computer_facing(void)
{
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    CHECK(cok_character_add_effect(m, 0xd5, 0, 0, false) != NULL);
    uint32_t seed = game.vm.seed;
    CHECK(queen_computer(&game, m) && m->combat->initiative == 0 && game.vm.seed == seed);
    CHECK(game.vm.status == COK_ECL_OK);
}

/* 5bb8:0074: a record that cannot act (+0x18b 0) ends its turn, and the way of moving is still
 * drawn (0074 falls through to 008d): the seed moves, the turn is over, and no stop. */
static void test_computer_inactive(void)
{
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    const cok_character_layout *L = R->character;
    cok_field_set8(L, m->record, COK_FIELD_ACTIVE, 0);
    m->combat->pattern = 1;
    uint32_t seed = game.vm.seed;
    CHECK(queen_computer(&game, m) && m->combat->initiative == 0 && game.vm.seed != seed);
    CHECK(game.vm.status == COK_ECL_OK);
}

/* 5bb8:0101 and 008d: the pattern's draws equal ai.c's (seeded: pattern 0 draws d8, then d2 or
 * d4), and the turn stops at 5bb8:0209 after them. */
static void test_computer_pattern(void)
{
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    uint32_t seed = game.vm.seed;
    m->combat->pattern = 0;
    CHECK(!queen_computer(&game, m) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "5bb8:0209") != NULL);
    uint8_t p1 = m->combat->pattern;
    uint32_t s1 = game.vm.seed;
    CHECK(s1 != seed);
    game.vm.seed = seed;
    game.vm.status = COK_ECL_OK;
    m->combat->pattern = 0;
    CHECK(cok_ai_pattern(&game, m) && m->combat->pattern == p1 && game.vm.seed == s1);
}

/* 5bb8:0116: one fleeing and not forced says "flees in panic" (177e:156e), even ended. */
static void test_computer_panic(void)
{
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    const cok_character_layout *L = R->character;
    cok_field_set8(L, m->record, COK_FIELD_ACTIVE, 0);
    m->combat->fleeing = 1;
    m->combat->forced = 0;
    CHECK(queen_computer(&game, m) && LOGGED("flees in panic"));
    battle_clear();
    a = record('A', 0, 0);
    m = record('M', 1, 0);
    place(m, 10, 10);
    game.undead = 5;
    m->combat->fleeing = 1;
    m->combat->forced = 1;
    CHECK(!queen_computer(&game, m) && LOGGED("is forced to flee") &&
          !LOGGED("flees in panic") && strstr(game.error, "5bb8:0209") != NULL);
    game.vm.status = COK_ECL_OK;
    (void)a;
}

/* 5bb8:1117 (morale): the roll is not capped at 0x66 (morale 0x80 from +0x9b 0xc0): an unhurt
 * monster keeps its nerve, where the cap would zero the roll and make it flee. */
static void test_morale_uncapped(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    const cok_character_layout *L = R->character;
    place(m, 10, 10);
    cok_field_set8(L, m->record, COK_FIELD_NPC, 0xc0);
    cok_field_set8(L, m->record, COK_FIELD_MAX_HIT_POINTS, 10);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 10);
    bool over = true;
    CHECK(cok_ai_morale(&game, m, &over) && !over && m->combat->fleeing == 0);
}

/* 5bb8:1117: var 0x1b (100 less it is the need, a byte, signed compare), not COK's 0x7ec6: with
 * 100 the morale of 5 holds (no flight); with 0 it does not. */
static void test_morale_var(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    const cok_character_layout *L = R->character;
    place(m, 10, 10);
    cok_field_set8(L, m->record, COK_FIELD_NPC, 0x81);
    cok_field_set8(L, m->record, COK_FIELD_MAX_HIT_POINTS, 10);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 5);
    game.combat.enemy_health = 5;
    bool over = true;
    /* 5bb8:11da reads the byte at DS:b20f + 0x1b (a byte: mov %es:0x1b(%bx),%al). */
    CHECK(cok_ecl_var_address(R, COK_VAR_MORALE, 0) == 0x1b);
    cok_ecl_set_var(&game.vm, COK_VAR_MORALE, 100);
    CHECK(cok_ai_morale(&game, m, &over) && !over && m->combat->fleeing == 0);
    cok_ecl_set_var(&game.vm, COK_VAR_MORALE, 0);
    CHECK(cok_ai_morale(&game, m, &over) && !over && m->combat->fleeing == 1);
}

/* 5bb8:1117's need is a signed word (dqk 5bb8:11da's jg, morale_signed): with var 0xc8 it is
 * 100 - 200 = -100, and a morale of 5 holds (no flight); as an unsigned word it would flee. */
static void test_morale_signed(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    const cok_character_layout *L = R->character;
    place(m, 10, 10);
    cok_field_set8(L, m->record, COK_FIELD_NPC, 0x81);
    cok_field_set8(L, m->record, COK_FIELD_MAX_HIT_POINTS, 10);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 5);
    game.combat.enemy_health = 5;
    bool over = true;
    cok_ecl_set_var(&game.vm, COK_VAR_MORALE, 0xc8);
    CHECK(cok_ai_morale(&game, m, &over) && !over && m->combat->fleeing == 0);
    cok_ecl_set_var(&game.vm, COK_VAR_MORALE, 100);
}

/* 5bb8:1117's surrender (a faster enemy, intelligence above 5): the stop cites 148f:1a69 (part 16),
 * not cok_combat_leave's own leave. */
static void test_morale_surrender(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    cok_character *m = record('M', 1, 0);
    const cok_character_layout *L = R->character;
    place(a, 12, 10);
    place(m, 10, 10);
    a->combat->movement = 20;
    m->combat->movement = 9;
    cok_field_set8(L, m->record, COK_FIELD_NPC, 0x81);
    cok_field_set8(L, m->record, COK_FIELD_MAX_HIT_POINTS, 10);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 5);
    cok_ecl_set_var(&game.vm, COK_VAR_MORALE, 0);
    game.combat.enemy_health = 0;
    bool over = false;
    CHECK(!cok_ai_morale(&game, m, &over) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "148f:1a69") != NULL);
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:1117's divide (most hit points 0, idiv at 11a6): undefined, cited there. */
static void test_morale_divide(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    const cok_character_layout *L = R->character;
    place(m, 10, 10);
    cok_field_set8(L, m->record, COK_FIELD_NPC, 0x81);
    cok_field_set8(L, m->record, COK_FIELD_MAX_HIT_POINTS, 0);
    bool over = false;
    CHECK(!cok_ai_morale(&game, m, &over) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "5bb8:11a6") != NULL);
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:0164: a pending spell (+0) is its cast's stop (849a:145b), after the pattern. */
static void test_computer_spell(void)
{
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    m->combat->spell = 3;
    CHECK(!queen_computer(&game, m) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "849a:145b") != NULL);
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:0268: the cleric level 3 of 3 (the undead search 52db:135e: its stop); a paladin above 2
 * too; a paladin at 2 not; a forced (fleeing) cleric not. */
static void test_computer_undead(void)
{
    const cok_character_layout *L = R->character;
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    CHECK(!queen_computer(&game, m) && strstr(game.error, "52db:135e") != NULL);
    game.vm.status = COK_ECL_OK;
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 3, 3);
    CHECK(!queen_computer(&game, m) && strstr(game.error, "52db:135e") != NULL);
    game.vm.status = COK_ECL_OK;
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 3, 2);
    CHECK(!queen_computer(&game, m) && strstr(game.error, "5bb8:0209") != NULL);
    game.vm.status = COK_ECL_OK;
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    m->combat->forced = 1;
    CHECK(!queen_computer(&game, m) && LOGGED("is forced to flee") &&
          strstr(game.error, "5bb8:0209") != NULL);
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:0268: a record that has turned as often as the undead count (+0x12 turns, unsigned, against
 * DS:c2ed) does not turn: at the count it goes on to the weapon's stop, one short of it searches. */
static void test_computer_undead_count(void)
{
    const cok_character_layout *L = R->character;
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    m->combat->turns = 4;
    CHECK(!queen_computer(&game, m) && strstr(game.error, "52db:135e") != NULL);
    game.vm.status = COK_ECL_OK;
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    m->combat->turns = 5;
    CHECK(!queen_computer(&game, m) && strstr(game.error, "52db:135e") == NULL &&
          strstr(game.error, "5bb8:0209") != NULL);
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:0000's first step: a record with no target picks one (52db:3d9e, the shared pick). Its only
 * enemy is a, so the pick sets a as the target, which computer_pair otherwise sets itself. */
static void test_computer_pick(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    cok_character *m = record('M', 1, 0);
    place(a, 12, 10);
    place(m, 10, 10);
    m->combat->target = NULL;
    CHECK(!queen_computer(&game, m) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "5bb8:0209") != NULL);
    CHECK(m->combat->target == a->record);
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:0306's prefix with Magic off: a monster (+0x9b below 0x7f) casts nothing; from 0x7f on it
 * is the spell choice's stop (1b1e, part 29). */
static void test_spell_choice(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    const cok_character_layout *L = R->character;
    place(m, 10, 10);
    m->combat->may_cast = 1;
    game.combat.magic_on = false;
    cok_field_set8(L, m->record, COK_FIELD_NPC, 0x7e);
    bool cast = true;
    CHECK(queen_spell_choice(&game, m, &cast) && !cast);
    cok_field_set8(L, m->record, COK_FIELD_NPC, 0x7f);
    CHECK(!queen_spell_choice(&game, m, &cast) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "5bb8:0306") != NULL);
    game.vm.status = COK_ECL_OK;
    battle_clear();
}

/* A battle with one fighter (party record, hostile 0) at 12, 10 and no items, its hands 0. */
static cok_character *fighter(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    cok_field_set8(R->character, a->record, COK_FIELD_USABLE_CLASSES, 0xff);
    place(a, 12, 10);
    return a;
}

/* A battle with a target that holds effect id (none for 0): 5bb8:1396's needs. */
static cok_character *target_with(uint8_t id)
{
    battle_clear();
    cok_character *t = record('T', 0, 0);
    if (id != 0) CHECK(cok_character_add_effect(t, id, 0, 0, false) != NULL);
    return t;
}

/* 5bb8:1396's needs from the target's effects (queen_weapon_needs_of): 0x7d sets need 2, 0x3c 3,
 * 0x50, 0x51 or 0x67 1 (last wins), 0x29 sets need_flagged 1 where need is 0, and 0x42, 0x43 are
 * the two halvings; no target, none. */
static void test_weapon_needs(void)
{
    queen_weapon_needs n;
    queen_weapon_needs_of(NULL, &n);
    CHECK(n.need == 0 && n.need_flagged == 0 && !n.effect_42 && !n.effect_43);
    queen_weapon_needs_of(target_with(0), &n);
    CHECK(n.need == 0 && n.need_flagged == 0 && !n.effect_42 && !n.effect_43);
    queen_weapon_needs_of(target_with(0x7d), &n);
    CHECK(n.need == 2 && n.need_flagged == 2);
    queen_weapon_needs_of(target_with(0x3c), &n);
    CHECK(n.need == 3 && n.need_flagged == 3);
    queen_weapon_needs_of(target_with(0x50), &n);
    CHECK(n.need == 1 && n.need_flagged == 1);
    queen_weapon_needs_of(target_with(0x51), &n);
    CHECK(n.need == 1 && n.need_flagged == 1);
    queen_weapon_needs_of(target_with(0x67), &n);
    CHECK(n.need == 1 && n.need_flagged == 1);
    queen_weapon_needs_of(target_with(0x29), &n);
    CHECK(n.need == 0 && n.need_flagged == 1);
    queen_weapon_needs_of(target_with(0x42), &n);
    CHECK(n.effect_42 && !n.effect_43 && n.need == 0);
    queen_weapon_needs_of(target_with(0x43), &n);
    CHECK(n.effect_43 && !n.effect_42 && n.need == 0);
    /* Later wins: 0x50 after 0x7d is 1; 0x3c with 0x29 leaves need_flagged at need (3). */
    cok_character *t = target_with(0x7d);
    CHECK(cok_character_add_effect(t, 0x50, 0, 0, false) != NULL);
    queen_weapon_needs_of(t, &n);
    CHECK(n.need == 1 && n.need_flagged == 1);
    t = target_with(0x3c);
    CHECK(cok_character_add_effect(t, 0x29, 0, 0, false) != NULL);
    queen_weapon_needs_of(t, &n);
    CHECK(n.need == 3 && n.need_flagged == 3);
    battle_clear();
}

/* 5bb8:1271 on a made-up type (dice 2 x 4, bonus +1, t[11] 2, flags 8 with t[5] 3, one hand): 27 by
 * hand. Each zeroing rule and the halvings, and a negative bonus passing the need (unsigned). */
static void test_weapon_score(void)
{
    uint8_t item[COK_ITEM_SIZE] = {0}, type[16] = {0};
    queen_weapon_needs n = {0};
    type[1] = 1, type[5] = 3, type[9] = 2, type[10] = 4, type[11] = 2, type[13] = 0xff;
    type[14] = 8, type[12] = 1;
    item[R->item_record.bonus] = 1;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 27);
    CHECK(queen_weapon_score(R, 1, item, type, &n, false) == 27);
    CHECK(queen_weapon_score(R, 2, item, type, &n, false) == 0); /* hands 2 + 1 >= 3 */
    item[R->item_record.cursed] = 1;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 0);
    item[R->item_record.cursed] = 0;
    n.need = 2;
    n.need_flagged = 2;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 0); /* bonus 1 below need 2 */
    item[R->item_record.bonus] = 2;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) != 0);
    n.need = 0;
    n.need_flagged = 2;
    item[R->item_record.bonus] = 1;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 0); /* flags 8: need_flagged */
    type[14] = 0;
    type[14] = 0; /* no flags 8: need_flagged is not the need, 0 passes */
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 23);
    n.need_flagged = 0;
    type[14] = 8;
    item[R->item_record.bonus] = 0xff; /* -1: no bonus added, and 255 passes need 2 unsigned */
    n.need = 2;
    n.need_flagged = 2;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 19);
    item[R->item_record.bonus] = 1;
    n.need = 0;
    n.need_flagged = 0;
    type[12] = 2;
    CHECK(queen_weapon_score(R, 0, item, type, &n, true) == 0);
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 27);
    type[12] = 1;
    type[7] = 1;
    n.effect_42 = true;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 13);
    type[7] = 0x80;
    n.effect_42 = false;
    n.effect_43 = true;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 13);
    n.effect_43 = false;
    CHECK(queen_weapon_score(R, 0, item, type, &n, false) == 27);
}

/* The made-up types of the weapon choice: 127 a dagger (one hand, 1d4, melee); 128 a bow (one
 * hand, 1d6, flags 9: arrows in slot 11); 126 a bow with flags 0x0a (no ammunition); 125 arrows
 * (type 2: neither weapon nor shield). Type 128 is made up too: Dark Queen's type table (64b2:0006,
 * 0x800 bytes) does not reach it, so it is only a type this test gives an item. */
static void weapon_types(void)
{
    uint8_t *t = game.item_types.type[127];
    memset(t, 0, 16);
    t[1] = 1, t[9] = 1, t[10] = 4, t[13] = 0xff;
    for (size_t k = 126; k <= 128; k += 2) {
        t = game.item_types.type[k];
        memset(t, 0, 16);
        t[1] = 1, t[5] = 1, t[9] = 1, t[10] = 6, t[12] = 1, t[13] = 0xff;
        t[14] = k == 128 ? 0x09 : 0x0a;
    }
    t = game.item_types.type[125];
    memset(t, 0, 16);
    t[0] = 2, t[13] = 0xff;
}

/* An item of type (its record's type byte), readied and cursed as given, inserted at the end of c's
 * items; its 1-based index. */
static size_t give(cok_character *c, uint8_t type, uint8_t readied, uint8_t cursed)
{
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[R->item_record.type] = type;
    item[R->item_record.readied] = readied;
    item[R->item_record.cursed] = cursed;
    CHECK(cok_character_insert_item(c, c->item_count, item));
    return c->item_count;
}

/* 5bb8:1396's choice: a bow (flags 9, arrows in slot 11) over a dagger with no enemy beside, the
 * dagger with one (the side list, 51c2:0980, of range 1); the bow with flags 0x0a readied out of
 * water, and the dagger underwater (var 0x3c); no draw, the View's Ready on the record shown. */
static void test_weapon_choice(void)
{
    weapon_types();
    cok_character *a = fighter();
    cok_field_set8_at(R->character, a->record, COK_FIELD_BASE_DICE, 1, 1);
    cok_field_set8_at(R->character, a->record, COK_FIELD_BASE_DICE_SIDES, 1, 2);
    size_t dagger = give(a, 127, 0, 0), bow = give(a, 128, 0, 0);
    size_t arrows = give(a, 125, 0, 0);
    a->slots[11] = arrows;
    game.vm.character = a->record;
    uint32_t seed = game.vm.seed;
    CHECK(queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == seed && a->slots[0] == bow);
    CHECK(a->items[bow - 1][R->item_record.readied] != 0);
    /* A monster beside: the dagger, the bow unreadied. */
    a = fighter();
    game.vm.character = a->record;
    dagger = give(a, 127, 0, 0), bow = give(a, 128, 0, 0);
    a->slots[11] = give(a, 125, 0, 0);
    cok_character *m = record('M', 1, 0);
    place(m, 13, 10);
    CHECK(queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(a->slots[0] == dagger && a->items[bow - 1][R->item_record.readied] == 0);
    /* Flags 0x0a: no ammunition, so the bow is chosen out of water, and the dagger underwater. */
    a = fighter();
    game.vm.character = a->record;
    dagger = give(a, 127, 0, 0), bow = give(a, 126, 0, 0);
    CHECK(queen_choose_weapon(&game, a, NULL) && a->slots[0] == bow);
    a = fighter();
    game.vm.character = a->record;
    dagger = give(a, 127, 0, 0), bow = give(a, 126, 0, 0);
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    CHECK(queen_choose_weapon(&game, a, NULL) && a->slots[0] == dagger);
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    /* With no ready's record shown: the bow is to be readied and the View's Ready is refused. */
    a = fighter();
    game.vm.character = NULL;
    give(a, 128, 0, 0);
    a->slots[11] = give(a, 125, 0, 0);
    CHECK(!queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "computer's weapon is readied for another") != NULL);
    game.vm.status = COK_ECL_OK;
    battle_clear();
}

/* A cursed readied weapon in slot 0 is kept (no swap, no Ready). It is two-handed, so the stats
 * (which count the readied items' hands) leave 2: no branch of the hands runs, nothing changed, and
 * the panel is not drawn. */
static void test_weapon_cursed(void)
{
    weapon_types();
    game.item_types.type[127][1] = 2;
    cok_character *a = fighter();
    cok_field_set8(R->character, a->record, COK_FIELD_HANDS, 3);
    size_t dagger = give(a, 127, 1, 1);
    a->slots[0] = dagger;
    game.vm.character = a->record;
    game.combat.panel = true;
    CHECK(queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(a->slots[0] == dagger && a->items[dagger - 1][R->item_record.readied] == 1);
    CHECK(game.combat.panel); /* not cleared: cok_arena_panel never ran */
    battle_clear();
}

/* The made-up types of the swap and keep tests: 121 a zero-hand weapon (melee, 1d4), 122 a
 * two-hander (2 hands, 2d6, melee), 123 a shield (one hand, bonus byte's score). */
static void swap_types(void)
{
    uint8_t *t = game.item_types.type[121];
    memset(t, 0, 16);
    t[1] = 0, t[9] = 1, t[10] = 4, t[13] = 0xff;
    t = game.item_types.type[122];
    memset(t, 0, 16);
    t[1] = 2, t[9] = 2, t[10] = 6, t[13] = 0xff;
    t = game.item_types.type[123];
    memset(t, 0, 16);
    t[0] = 1, t[1] = 1, t[13] = 0xff;
}

/* The record's hands from its readied items (177e:0be2, as the game's stats), before a choice. */
static void stats_of(cok_character *c)
{
    char error[300];
    CHECK(cok_character_stats(c, R, &game.item_types, error, sizeof error));
}

/* A readied item's flag, as the record holds it. */
static uint8_t readied(cok_character *c, size_t item)
{
    return c->items[item - 1][R->item_record.readied];
}

/* 5bb8:1396's swap, slot 1's hands (1851-188c): a one-handed dagger in slot 0 and a shield in slot 1
 * leave 1 hand after the swap unreadies the dagger, so the shield's hand is taken off (0 left) and
 * the two-hander (type 122) can be readied. Without that subtraction the Ready refuses it (1 + 2 >
 * 2), so the shield stays readied and slot 0 is empty. The hands end at 2 with the two-hander
 * readied, the shield unreadied (slot 1's Ready, the hands above 2), and the panel drawn. */
static void test_weapon_swap(void)
{
    weapon_types();
    swap_types();
    cok_character *a = fighter();
    size_t dagger = give(a, 127, 1, 0), shield = give(a, 123, 1, 0), two = give(a, 122, 0, 0);
    a->slots[0] = dagger;
    a->slots[1] = shield;
    stats_of(a);
    game.vm.character = a->record;
    game.combat.panel = true;
    CHECK(queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(a->slots[0] == two && readied(a, two) != 0);
    CHECK(readied(a, dagger) == 0 && readied(a, shield) == 0);
    CHECK(!game.combat.panel); /* changed: cok_arena_panel ran */
    battle_clear();
}

/* 5bb8:1396's keep (18d5): a cursed zero-hand weapon in slot 0 is kept (no swap), and the best
 * shield readied in slot 1 is kept too (free is false for the shield). The hands are 1, below 2,
 * but nothing changes, so the panel is not drawn. Without the shield's test the shield is
 * unreadied and readied again, and the panel is drawn. */
static void test_weapon_keep(void)
{
    weapon_types();
    swap_types();
    cok_character *a = fighter();
    size_t cursed = give(a, 121, 1, 1), shield = give(a, 123, 1, 0);
    a->slots[0] = cursed;
    a->slots[1] = shield;
    stats_of(a);
    game.vm.character = a->record;
    game.combat.panel = true;
    CHECK(queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(a->slots[0] == cursed && a->slots[1] == shield);
    CHECK(readied(a, cursed) != 0 && readied(a, shield) != 0);
    CHECK(game.combat.panel); /* nothing changed: cok_arena_panel did not run */
    battle_clear();
}

/* 5bb8:1396's hands below 2 (the free slot 1, 18f5-1950) readies the best shield, and a shield's
 * score is its bonus + 1, so a +0 shield (score 1) is chosen over none. A dagger readied in slot 0
 * (1 hand) leaves 1 hand and slot 1 free: the +0 shield is readied and the panel drawn. Then a +0
 * shield readied in slot 1 with a +2 unreadied: the +2 (score 3) is readied and the +0 unreadied. */
static void test_weapon_shield(void)
{
    weapon_types();
    swap_types();
    cok_character *a = fighter();
    size_t dagger = give(a, 127, 1, 0), shield = give(a, 123, 0, 0);
    a->slots[0] = dagger;
    stats_of(a);
    game.vm.character = a->record;
    game.combat.panel = true;
    CHECK(queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(readied(a, shield) != 0 && readied(a, dagger) != 0);
    CHECK(!game.combat.panel);
    a = fighter();
    size_t worn = give(a, 123, 1, 0);
    size_t better = give(a, 123, 0, 0);
    a->items[better - 1][R->item_record.bonus] = 2;
    a->slots[1] = worn;
    stats_of(a);
    game.vm.character = a->record;
    game.combat.panel = true;
    CHECK(queen_choose_weapon(&game, a, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(readied(a, better) != 0 && readied(a, worn) == 0);
    CHECK(!game.combat.panel);
    battle_clear();
}

/* A battle with one monster (hostile 1) at 10, 10 and no items. */
static cok_character *fighter_monster(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    return m;
}

/* A monster with no items (SHARKMAN's case): no Ready, the stats and attacks recomputed, and the
 * panel drawn (changed: the swap's 1 with slot 0 NULL). With hands past 2 and no item to ready
 * (the original's read through NULL), the choice stops. */
static void test_weapon_monster(void)
{
    weapon_types();
    battle_clear();
    cok_character *m = record('M', 1, 0);
    game.combat.panel = true;
    uint32_t seed = game.vm.seed;
    CHECK(queen_choose_weapon(&game, m, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(!game.combat.panel && game.vm.seed == seed && m->slots[0] == 0);
    /* Hands past 2 from a readied item (a helm of 3 hands, type 124 at slot 5: the stats count
     * it), and no weapon to ready: the original's Ready of NULL. */
    uint8_t *helm = game.item_types.type[124];
    memset(helm, 0, 16);
    helm[0] = 5, helm[1] = 3, helm[13] = 0xff;
    m = fighter_monster();
    give(m, 124, 1, 0);
    CHECK(!queen_choose_weapon(&game, m, NULL) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "Ready is given no item") != NULL);
    game.vm.status = COK_ECL_OK;
    /* With a shield readied at slot 1: cursed, slot 1 is kept and the chosen (none) is readied,
     * which stops; not cursed, the shield is unreadied (the View's Ready) and it goes on. */
    uint8_t *shield = game.item_types.type[123];
    memset(shield, 0, 16);
    shield[0] = 1, shield[13] = 0xff;
    m = fighter_monster();
    game.vm.character = m->record;
    give(m, 124, 1, 0);
    size_t held = give(m, 123, 1, 1);
    CHECK(!queen_choose_weapon(&game, m, NULL) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "Ready is given no item") != NULL);
    game.vm.status = COK_ECL_OK;
    m = fighter_monster();
    game.vm.character = m->record;
    give(m, 124, 1, 0);
    held = give(m, 123, 1, 0);
    CHECK(queen_choose_weapon(&game, m, NULL) && game.vm.status == COK_ECL_OK);
    CHECK(m->items[held - 1][R->item_record.readied] == 0);
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
    test_computer_facing();
    test_computer_inactive();
    test_computer_pattern();
    test_computer_panic();
    test_morale_uncapped();
    test_morale_var();
    test_morale_signed();
    test_morale_surrender();
    test_morale_divide();
    test_computer_spell();
    test_computer_undead();
    test_computer_undead_count();
    test_computer_pick();
    test_spell_choice();
    test_weapon_needs();
    test_weapon_score();
    test_weapon_choice();
    test_weapon_cursed();
    test_weapon_monster();
    test_weapon_swap();
    test_weapon_keep();
    test_weapon_shield();
    cok_party_free(&game.party);
    puts("queen ai tests passed");
    return 0;
}
