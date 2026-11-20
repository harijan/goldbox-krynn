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
    /* A combat icon in slot 1 (CBODY 1, 604b:05d7), so that a pose is drawn, not stopped. */
    cok_field_set8(L, c->record, COK_FIELD_ICON_BODY, 1);
    cok_field_set8(L, c->record, COK_FIELD_ICON, 1);
    CHECK(queen_added_icons(&game, c->record));
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
 * Magic off (or may_cast 0) a monster guards; with Magic on and may_cast 1 and nothing memorised the
 * choice casts nothing (no draw), and the guard follows. */
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
    CHECK(cok_ai_step(&game, m) && game.vm.status == COK_ECL_OK && m->combat->guarding == 1);
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
 * d4), and the turn goes on past them to its end (the fight, step 14, runs; no stop). */
static void test_computer_pattern(void)
{
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    uint32_t seed = game.vm.seed;
    m->combat->pattern = 0;
    CHECK(cok_ai_pattern(&game, m) && game.vm.seed != seed);
    uint8_t p1 = m->combat->pattern;
    uint32_t s1 = game.vm.seed;
    game.vm.seed = seed;
    m->combat->pattern = 0;
    CHECK(cok_ai_pattern(&game, m) && m->combat->pattern == p1 && game.vm.seed == s1);
    m = computer_pair(1, &a);
    m->combat->pattern = 0;
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK);
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
    CHECK(queen_computer(&game, m) && LOGGED("is forced to flee") &&
          !LOGGED("flees in panic") && game.vm.status == COK_ECL_OK);
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

/* 5bb8:0170: a pending spell (+0) is cast at once (queen_cast, MM at the computer's aim: the stop
 * 52db:1d42/48aa), after the pattern; the spell is still kept at the stop. */
static void test_computer_spell(void)
{
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    game.vm.mode = 5;
    m->combat->spell = 0x0f;
    CHECK(!queen_computer(&game, m) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "52db:1d42/48aa") != NULL && m->combat->spell == 0x0f);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.vm.mode = 0;
}

/* 5bb8:0268: a cleric or a paladin above 2 searches for undead (52db:135e, limit 13); a paladin at
 * 2 does not, and a forced (fleeing) cleric does not turn. With no undead in sight the turn goes on
 * (the weapon's choice, the fight); with one that turns (a cleric of level 9: its value -1, destroyed)
 * the turn ends at once (queen_turn_undead). */
static void test_computer_undead(void)
{
    const cok_character_layout *L = R->character;
    const cok_rules *rules = R;
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK && !LOGGED("turns undead"));
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 3, 3);
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK && !LOGGED("turns undead"));
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 3, 2);
    cok_combat_set_field_word(rules, a->record, COK_FIELD_UNDEAD, 1);
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK && !LOGGED("turns undead"));
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    m->combat->forced = 1;
    CHECK(queen_computer(&game, m) && LOGGED("is forced to flee") && game.vm.status == COK_ECL_OK);
    game.vm.status = COK_ECL_OK;
    /* The cleric of 9 (level 9 in the table) against kind 1: value -1, destroyed on any d20: the turn
     * ends (initiative 0, the attack over flag set, the turn counted). */
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 9);
    cok_combat_set_field_word(rules, a->record, COK_FIELD_UNDEAD, 1);
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK);
    CHECK(LOGGED("turns undead...") && LOGGED("Is destroyed"));
    CHECK(cok_combat_field_word(rules, a->record, COK_FIELD_STATUS) == 8 &&
          cok_field_u8(L, a->record, COK_FIELD_ACTIVE) == 0);
    CHECK(m->combat->turns == 1 && m->combat->initiative == 0 && game.combat.attack_over);
    /* The search's limit is 13 (0280): a kind 13 is searched for and found. */
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 9);
    cok_combat_set_field_word(rules, a->record, COK_FIELD_UNDEAD, 13);
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK && LOGGED("turns undead..."));
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:0268: a record that has turned as often as the undead count (+0x12 turns, unsigned, against
 * DS:c2ed) does not turn: at 4 it turns (the fifth), at 5 it does not search. */
static void test_computer_undead_count(void)
{
    const cok_character_layout *L = R->character;
    cok_character *a;
    cok_character *m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 9);
    cok_combat_set_field_word(R, a->record, COK_FIELD_UNDEAD, 1);
    m->combat->turns = 4;
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK && m->combat->turns == 5);
    CHECK(LOGGED("turns undead..."));
    m = computer_pair(1, &a);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 9);
    cok_combat_set_field_word(R, a->record, COK_FIELD_UNDEAD, 1);
    m->combat->turns = 5;
    CHECK(queen_computer(&game, m) && strstr(game.error, "52db:135e") == NULL &&
          game.vm.status == COK_ECL_OK && !LOGGED("turns undead") && m->combat->turns == 5);
    game.vm.status = COK_ECL_OK;
}

/* 52db:135e (queen_undead): the highest kind of undead in sight, kind 0 and below skipped, forced
 * skipped, no higher than the limit; the limit is set to the kind found, or 0. */
static void test_undead_search(void)
{
    battle_clear();
    const cok_rules *rules = R;
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    cok_character *b = record('B', 0, 0), *c = record('C', 0, 0), *d = record('D', 0, 0);
    cok_character *e = record('E', 0, 0);
    place(b, 12, 10);
    place(c, 8, 10);
    place(d, 10, 12);
    place(e, 10, 8);
    cok_combat_set_field_word(rules, b->record, COK_FIELD_UNDEAD, 3);
    cok_combat_set_field_word(rules, c->record, COK_FIELD_UNDEAD, 7);
    c->combat->forced = 1;
    cok_combat_set_field_word(rules, d->record, COK_FIELD_UNDEAD, 0);
    cok_combat_set_field_word(rules, e->record, COK_FIELD_UNDEAD, 0xffff); /* -1 */
    cok_character *u = NULL;
    bool found = false;
    int16_t limit = 13;
    CHECK(queen_undead(&game, m, &u, &limit, &found) && found && u == b && limit == 3);
    limit = 2;
    CHECK(queen_undead(&game, m, &u, &limit, &found) && !found && u == NULL && limit == 0);
    c->combat->forced = 0;
    limit = 13;
    CHECK(queen_undead(&game, m, &u, &limit, &found) && found && u == c && limit == 7);
    /* A kind equal to the limit is kept; of two of the same kind the first listed (the nearer). */
    c->combat->forced = 1;
    cok_character *f = record('F', 0, 0);
    place(f, 13, 13);
    cok_combat_set_field_word(rules, f->record, COK_FIELD_UNDEAD, 3);
    limit = 3;
    CHECK(queen_undead(&game, m, &u, &limit, &found) && found && u == b && limit == 3);
    game.vm.status = COK_ECL_OK;
}

/* The first seed whose first draw is a d20 below 4 (a turn at value 4 or more fails on it). */
static uint32_t low_d20_seed(void)
{
    for (uint32_t seed = 1;; ++seed) {
        uint32_t copy = seed;
        if (cok_dice(&copy, 1, 20) < 4) return seed;
    }
}

/* Turning undead (52db:1117, queen_turn_undead) against one undead a square away. The table's
 * kind 1 by level (DS:5a6a, 11 a kind): 0x63 at 0 (never), 10, 7 and 4 at 1 to 3, 1 at 4 and 5
 * (turned: value above 0), 0 at 6 and 7 and -1 at 8 to 10 (destroyed); kind 3 is 4 at level 4. A
 * fresh battle with the cleric at ccl, the paladin at pal (-1 for none), the deity word deity, and
 * the undead's kind; the undead is returned in *undead. */
static cok_character *turn_fight(uint8_t ccl, int pal, unsigned deity, unsigned kind,
                                 cok_character **undead)
{
    const cok_character_layout *L = R->character;
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, ccl);
    if (pal >= 0) cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 3, (uint8_t)pal);
    cok_combat_set_field_word(R, m->record, COK_FIELD_DEITY, deity);
    cok_character *a = record('A', 0, 0);
    place(a, 12, 10);
    cok_combat_set_field_word(R, a->record, COK_FIELD_UNDEAD, kind);
    m->combat->turns = 0;
    game.undead = 5;
    game.combat.attack_over = false;
    *undead = a;
    return m;
}

/* How many of the n records u (a turning test's undead) are at status st. */
static unsigned at_status(cok_character *const *u, unsigned n, unsigned st)
{
    unsigned k = 0;
    for (unsigned i = 0; i < n; ++i)
        k += cok_combat_field_word(R, u[i]->record, COK_FIELD_STATUS) == st;
    return k;
}

/* Turning undead's count (52db:1117's 121f-124c and 12de-12fc): drawn at the first undead found,
 * after the d20 (1d12 where that one's value is above 0, else 1d6 + 6), one less for each turned;
 * while the extra 6 last, a count run out at a value below 0 is kept at 1. In the battle's mode
 * (5: one destroyed is taken off the map, so the search goes on to the next). */
static void test_turn_undead_count(void)
{
    static const int8_t at[8][2] = {{12, 10}, {8, 10}, {10, 12}, {10, 8},
                                    {12, 12}, {8, 8}, {12, 8}, {8, 12}};
    const cok_character_layout *L = R->character;
    uint8_t mode = game.vm.mode;
    game.vm.mode = 5;
    game.combat.bodies = 0;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        uint32_t s0 = seed * 0x2545f491u + 7, copy = s0;
        cok_dice(&copy, 1, 20);
        uint32_t after = copy;
        unsigned c12 = cok_dice(&copy, 1, 12);
        copy = after;
        unsigned c6 = cok_dice(&copy, 1, 6) + 6u;
        cok_character *u[8];
        /* (a) A paladin of 6 (level 4) and four of kind 1 (value 1: turned on any d20, each one
         * leaving): the 1d12 count, at most four. */
        battle_clear();
        cok_character *m = record('M', 1, 0);
        place(m, 10, 10);
        cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 3, 6);
        for (unsigned i = 0; i < 4; ++i) {
            u[i] = record('U', 0, 0);
            u[i]->combat->not_party = 1; /* a monster: no body */
            place(u[i], at[i][0], at[i][1]);
            cok_combat_set_field_word(R, u[i]->record, COK_FIELD_UNDEAD, 1);
        }
        game.undead = 5;
        game.vm.seed = s0;
        CHECK(queen_turn_undead(&game, m) && game.vm.status == COK_ECL_OK);
        unsigned want = c12 < 4 ? c12 : 4;
        if (at_status(u, 4, 3) != want) {
            fprintf(stderr, "seed %u: 1d12 %u, %u of 4 turned\n", (unsigned)seed, c12,
                    at_status(u, 4, 3));
            exit(1);
        }
        /* (b) A cleric of 15 (level 10) and eight of kind 1 (value -1: destroyed): 1d6 + 6. */
        battle_clear();
        m = record('M', 1, 0);
        place(m, 10, 10);
        cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 15);
        for (unsigned i = 0; i < 8; ++i) {
            u[i] = record('U', 0, 0);
            u[i]->combat->not_party = 1; /* a monster: no body */
            place(u[i], at[i][0], at[i][1]);
            cok_combat_set_field_word(R, u[i]->record, COK_FIELD_UNDEAD, 1);
        }
        game.vm.seed = s0;
        CHECK(queen_turn_undead(&game, m) && game.vm.status == COK_ECL_OK);
        want = c6 < 8 ? c6 : 8;
        if (at_status(u, 8, 8) != want) {
            fprintf(stderr, "seed %u: 1d6 + 6 %u, %u of 8 destroyed\n", (unsigned)seed, c6,
                    at_status(u, 8, 8));
            exit(1);
        }
        /* (c) A cleric of 8 (level 8), one of kind 5 (value 1: turned first, the count 1d12) and six
         * of kind 1 (value -1, destroyed): a count of 1 destroys none; from 2 to 6 the count is kept
         * at 1 while the extra 6 last (five destroyed, six turns in all); above 6 all six. */
        battle_clear();
        m = record('M', 1, 0);
        place(m, 10, 10);
        cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 0, 8);
        for (unsigned i = 0; i < 7; ++i) {
            u[i] = record('U', 0, 0);
            u[i]->combat->not_party = 1; /* a monster: no body */
            place(u[i], at[i][0], at[i][1]);
            cok_combat_set_field_word(R, u[i]->record, COK_FIELD_UNDEAD, i == 0 ? 5 : 1);
        }
        game.vm.seed = s0;
        CHECK(queen_turn_undead(&game, m) && game.vm.status == COK_ECL_OK);
        want = c12 == 1 ? 0 : c12 <= 6 ? 5 : 6;
        if (at_status(u, 1, 3) != 1 || at_status(u + 1, 6, 8) != want) {
            fprintf(stderr, "seed %u: 1d12 %u, kind 5 status %u, %u of 6 destroyed\n",
                    (unsigned)seed, c12, cok_combat_field_word(R, u[0]->record, COK_FIELD_STATUS),
                    at_status(u + 1, 6, 8));
            exit(1);
        }
    }
    game.vm.mode = mode;
    game.vm.status = COK_ECL_OK;
}

static void test_turn_undead_levels(void)
{
    cok_character *a;
    /* The cleric of 3 less var 0x0f 5: level 0, never (0x63). Nothing turns, and "Nothing Happens..."
     * is said once (one draw); the turn is over and counted. */
    cok_ecl_set_var(&game.vm, COK_VAR_TURNING, 5);
    cok_character *m = turn_fight(3, -1, 0, 1, &a);
    CHECK(queen_turn_undead(&game, m) && LOGGED("Nothing Happens...") && game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_field_word(R, a->record, COK_FIELD_STATUS) == 0 &&
          cok_field_u8(R->character, a->record, COK_FIELD_ACTIVE) == 1);
    CHECK(m->combat->turns == 1 && m->combat->initiative == 0 && game.combat.attack_over);
    cok_ecl_set_var(&game.vm, COK_VAR_TURNING, 0);
    /* The cleric of 2 with deity 2 is level 4: value 1, turned (status 3, the battle's leave) on
     * any d20; without the deity it is level 2, value 7, which a d20 below 4 does not turn. */
    m = turn_fight(2, -1, 2, 1, &a);
    game.vm.seed = low_d20_seed();
    CHECK(queen_turn_undead(&game, m) && LOGGED("is turned") && game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_field_word(R, a->record, COK_FIELD_STATUS) == 3);
    /* The cleric of 15 is level 10 (above 14: 10): value -1, destroyed; the sides are counted after
     * (51c2:0920: none left on the undead's). */
    m = turn_fight(15, -1, 0, 1, &a);
    game.combat.sides[0] = game.combat.sides[1] = 9;
    CHECK(queen_turn_undead(&game, m) && LOGGED("Is destroyed") && game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_field_word(R, a->record, COK_FIELD_STATUS) == 8 &&
          cok_field_u8(R->character, a->record, COK_FIELD_ACTIVE) == 0);
    CHECK(game.combat.sides[0] == 0 && game.combat.sides[1] == 1);
    /* A paladin of 6 beats a cleric of 3: level 4, turned as the cleric of 2 with deity 2 is. */
    m = turn_fight(3, 6, 0, 1, &a);
    game.vm.seed = low_d20_seed();
    CHECK(queen_turn_undead(&game, m) && LOGGED("is turned") && game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_field_word(R, a->record, COK_FIELD_STATUS) == 3);
    /* Above 8 the level is 9 below 14, else 10 (kind 7: 4 at level 8, 1 at 9, 0 at 10): a cleric
     * of 8 is level 8 (a d20 below 4 turns nothing), of 9 and 13 level 9 (turned), of 14 level 10
     * (destroyed). */
    static const struct { uint8_t cleric; unsigned status; } cap[] = {{8, 0}, {9, 3}, {13, 3}, {14, 8}};
    for (size_t i = 0; i < sizeof cap / sizeof *cap; ++i) {
        m = turn_fight(cap[i].cleric, -1, 0, 7, &a);
        game.vm.seed = low_d20_seed();
        CHECK(queen_turn_undead(&game, m) && game.vm.status == COK_ECL_OK);
        CHECK(cok_combat_field_word(R, a->record, COK_FIELD_STATUS) == cap[i].status);
    }
    /* The deity is a word (+0x19b): 0x0102 adds nothing (the cleric of 2 stays level 2: value 7,
     * which a d20 below 4 does not turn). */
    m = turn_fight(2, -1, 0x0102, 1, &a);
    game.vm.seed = low_d20_seed();
    CHECK(queen_turn_undead(&game, m) && LOGGED("Nothing Happens...") && game.vm.status == COK_ECL_OK);
    /* Var 0x0f is compared unsigned: 0x80 is above a level of 3 (level 0: nothing turns). */
    cok_ecl_set_var(&game.vm, COK_VAR_TURNING, 0x80);
    m = turn_fight(3, -1, 0, 1, &a);
    CHECK(queen_turn_undead(&game, m) && LOGGED("Nothing Happens...") && game.vm.status == COK_ECL_OK);
    cok_ecl_set_var(&game.vm, COK_VAR_TURNING, 0);
    game.vm.status = COK_ECL_OK;
}

/* The d20 is the first draw of queen_turn_undead (before the count): a cleric of 3 against kind 1 at
 * level 3 (value 4) is turned exactly where the d20 is 4 or more; below, the limit drops to 0 and
 * nothing turns. Checked on a run of seeds, each d20 worked out from the seed's first draw. */
static void test_turn_undead_d20(void)
{
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        cok_character *a;
        cok_character *m = turn_fight(3, -1, 0, 1, &a);
        uint32_t copy = seed * 0x9e3779b1u + 1;
        unsigned d20 = cok_dice(&copy, 1, 20);
        game.vm.seed = seed * 0x9e3779b1u + 1;
        CHECK(queen_turn_undead(&game, m) && game.vm.status == COK_ECL_OK);
        bool turned = cok_combat_field_word(R, a->record, COK_FIELD_STATUS) == 3;
        if (turned != (d20 >= 4)) {
            fprintf(stderr, "seed %u: d20 %u, turned %d\n", (unsigned)seed, d20, (int)turned);
            exit(1);
        }
        /* The limit starts at 13: a kind 13 to a cleric of 15 (level 10, value 13) is turned where
         * the d20 is 13 or more. */
        m = turn_fight(15, -1, 0, 13, &a);
        game.vm.seed = seed * 0x9e3779b1u + 1;
        CHECK(queen_turn_undead(&game, m) && game.vm.status == COK_ECL_OK);
        turned = cok_combat_field_word(R, a->record, COK_FIELD_STATUS) == 3;
        if (turned != (d20 >= 13)) {
            fprintf(stderr, "seed %u: d20 %u, kind 13 turned %d\n", (unsigned)seed, d20, (int)turned);
            exit(1);
        }
    }
}

/* Turning undead's limit (queen_turn_undead's search, 52db:135e): a paladin of 6 (level 4) against
 * kind 3 (value 4 at level 4), kind 2 and kind 1 (value 1): a d20 below 4 lowers the limit to 2 (one
 * less than kind 3), so kind 2 is turned and kind 3 is not; from 4 kind 3 is turned first. */
static void test_turn_undead_limit(void)
{
    for (uint32_t seed = 1; seed <= 16; ++seed) {
        battle_clear();
        cok_character *m = record('M', 1, 0);
        place(m, 10, 10);
        cok_field_set8_at(R->character, m->record, COK_FIELD_CLASS_LEVEL, 3, 6);
        cok_character *k2 = record('K', 0, 0), *k1 = record('J', 0, 0);
        place(k2, 12, 10);
        place(k1, 10, 12);
        cok_character *kb = record('L', 0, 0);
        place(kb, 8, 10);
        cok_combat_set_field_word(R, k2->record, COK_FIELD_UNDEAD, 3);
        cok_combat_set_field_word(R, k1->record, COK_FIELD_UNDEAD, 1);
        cok_combat_set_field_word(R, kb->record, COK_FIELD_UNDEAD, 2);
        m->combat->turns = 0;
        game.undead = 5;
        uint32_t copy = seed * 0x85ebca6bu + 3;
        unsigned d20 = cok_dice(&copy, 1, 20);
        game.vm.seed = seed * 0x85ebca6bu + 3;
        CHECK(queen_turn_undead(&game, m) && game.vm.status == COK_ECL_OK);
        unsigned st2 = cok_combat_field_word(R, k2->record, COK_FIELD_STATUS);
        unsigned stb = cok_combat_field_word(R, kb->record, COK_FIELD_STATUS);
        if (d20 < 4 ? (stb != 3 || st2 != 0) : st2 != 3) {
            fprintf(stderr, "seed %u: d20 %u, kind 3 status %u, kind 2 status %u\n", (unsigned)seed,
                    d20, st2, stb);
            exit(1);
        }
    }
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
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK);
    CHECK(m->combat->target == a->record);
    game.vm.status = COK_ECL_OK;
}

/* 5bb8:0306's prefix with Magic off: a monster (+0x9b below 0x7f) casts nothing; from 0x7f on it
 * goes on to the choice, which with nothing memorised casts nothing (no stop). */
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
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
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

/* 5bb8:0b3d, the fight's steps: a monster with its party's record 20 cells east (no enemy in reach)
 * steps toward it once a pass (6b0, the pick found) until its movement is spent (movement / 2 is 0);
 * then it guards each pass (no_move, 0fec) and the fight is over at the 21st pass (count above 20). */
static void test_fight_steps(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 30, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->target = a->record;
    game.vm.character = m->record;
    bool over = false;
    CHECK(queen_fight(&game, m, &over) && over && game.vm.status == COK_ECL_OK);
    /* Two half squares a straight step: five steps take it from 10 to 15 with movement 2 left. A
     * sixth needs more than its cost (5bb8:0438's movement above the cost), so it is stuck three
     * times (movement spent) and guards at the end of its turn. */
    CHECK(cok_combat_x(&game.combat, m->record) == 15 && m->combat->movement == 0 &&
          m->combat->initiative == 0);
    /* With no enemy at all the pick finds none: it guards, and the computer's turn is over. */
    battle_clear();
    m = record('M', 1, 0);
    place(m, 10, 10);
    CHECK(queen_computer(&game, m) && m->combat->guarding == 1 && game.vm.status == COK_ECL_OK);
    battle_clear();
}

/* 5bb8:0b3d's spell test: a step that began a spell (spell above 0 after 6b0) ends the fight, over,
 * after one step: the movement is spent by that one only. */
static void test_fight_spell(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 30, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->target = a->record;
    m->combat->spell = 3;
    game.vm.character = m->record;
    bool over = false;
    CHECK(queen_fight(&game, m, &over) && over && game.vm.status == COK_ECL_OK);
    CHECK(m->combat->movement > 0 && m->combat->movement < 12);
    battle_clear();
}

/* 5bb8:0b3d's underwater test: a monster with a bow (range 6, a missile weapon, no arrows) and a
 * party's record 4 cells away, in reach of the bow but not a square away: underwater (var 0x3c) the
 * weapon's choice is made with the one draw of the pick and no attack, and the pass is done (not
 * over: the loop of queen_computer picks again). Out of water the bow reaches the record, and an
 * attack is made. */
static void test_fight_underwater(void)
{
    weapon_types();
    game.item_types.type[128][12] = 6;
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    cok_character *a = record('A', 0, 0);
    place(a, 14, 10);
    m->slots[0] = give(m, 128, 1, 0);
    game.vm.character = m->record;
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 1);
    uint32_t seed = game.vm.seed;
    bool over = false;
    CHECK(queen_fight(&game, m, &over) && !over && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed == after_draws(seed, 1, 1) && !LOGGED("Misses") && !LOGGED("Hitting"));
    cok_ecl_set_var(&game.vm, COK_VAR_RECORD_EFFECT, 0);
    m->combat->initiative = 5;
    game.vm.seed = seed;
    over = false;
    CHECK(queen_fight(&game, m, &over) && game.vm.status == COK_ECL_OK);
    CHECK(game.vm.seed != after_draws(seed, 1, 1));
    battle_clear();
}

/* 5bb8:0b3d's count: a monster with movement left and a target 35 cells east steps once a pass
 * (straight east, way 1's first try, through 6b0); the guard of the 21st pass (count above 20) ends
 * its turn (a guard is cok_combat_end_turn, which clears the movement) before that pass's step, and
 * the fight is over: 20 steps, x 30. */
static void test_fight_count(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 45, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->target = a->record;
    m->combat->movement = 255;
    game.vm.character = m->record;
    bool over = false;
    CHECK(queen_fight(&game, m, &over) && over && game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_x(&game.combat, m->record) == 30 && m->combat->initiative == 0);
    battle_clear();
}

/* 52db:3d9e's pick among the enemies within reach (d(1, n), 148f:1968): a seed rolling 2 picks the
 * second enemy listed, which is booked (its hits +1, cok_combat_book) and not the first. */
static void test_fight_pick(void)
{
    battle_clear();
    cok_character *p = record('P', 0, 0), *q = record('Q', 0, 0);
    place(p, 10, 11);
    place(q, 9, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->target = NULL;
    uint32_t seed = 1;
    while (true) {
        uint32_t t = seed;
        if (cok_dice(&t, 1, 2) == 2) break;
        ++seed;
    }
    game.vm.seed = seed;
    uint8_t n = 0;
    CHECK(cok_combat_enemies(&game, m, 1, &n) && n == 2);
    cok_character *picked = game.combat.combatant[game.combat.enemies[2]].character;
    cok_character *other = picked == p ? q : p;
    bool over = false;
    CHECK(queen_fight(&game, m, &over) && game.vm.status == COK_ECL_OK);
    CHECK(picked->combat->hits >= 1 && other->combat->hits == 0);
    battle_clear();
}

/* 52e4's sweep (the side list of range 1, 51c2:0980, has more enemies than the attacks left): the
 * sweep ends the turn (51c2:0c11) and the fight's pass is done, not over. */
static void test_fight_sweep(void)
{
    battle_clear();
    cok_character *p = record('P', 0, 0), *q = record('Q', 0, 0);
    place(p, 10, 11);
    place(q, 9, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->target = p->record;
    m->combat->sweeps = 2;
    cok_field_set8_at(R->character, m->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    game.vm.character = m->record;
    bool over = true;
    CHECK(queen_fight(&game, m, &over) && !over && m->combat->initiative == 0);
    CHECK(LOGGED("sweeps"));
    battle_clear();
}

/* 51c2:0b26 (the distance) puts back only 4 bytes of the listing (distance_kept_bytes 4): entry 2,
 * which the listing wrote, is kept; with 0, the whole listing is put back, entry 2 as it was. */
static void test_distance_kept(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    cok_character *m = record('M', 1, 0);
    place(a, 12, 10);
    place(m, 10, 10);
    CHECK(R->combat.distance_kept_bytes == 4); /* rules_dark_queen.c: 51c2:0b26 keeps 4 bytes */
    static cok_rules four, whole;
    four = whole = *R;
    four.combat.distance_kept_bytes = 4;
    whole.combat.distance_kept_bytes = 0;
    uint8_t d = 0;
    game.combat.listed[2].index = 0x77;
    CHECK(cok_combat_distance(&game.combat, &four, m->record, a->record, &d) &&
          game.combat.listed_count >= 2 && game.combat.listed[2].index != 0x77);
    game.combat.listed[2].index = 0x77;
    CHECK(cok_combat_distance(&game.combat, &whole, m->record, a->record, &d) &&
          game.combat.listed[2].index == 0x77);
    battle_clear();
}

/* 5bb8:0209's initiative test: a monster with initiative 0 does not fight, even with a party record
 * 20 cells east (the pick finds it): its loop guards at once (cok_ai_guard, the end of its turn, which
 * spends its movement), with no step. */
static void test_loop_initiative(void)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 30, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    m->combat->initiative = 0;
    game.vm.character = m->record;
    CHECK(queen_computer(&game, m) && game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_x(&game.combat, m->record) == 10 && m->combat->movement == 0);
    battle_clear();
}

/* 5bb8:0b3d's prologue: a party record (hostile 0) is bandaged first (51c2:0d63, cok_combat_bandage: a
 * party record with status 5 is bandaged, status 4, "is bandaged"), and its initiative is 0, so its
 * fight is over at once. */
static void test_fight_bandage(void)
{
    battle_clear();
    cok_character *c = record('C', 0, 0);
    place(c, 10, 10);
    cok_character *d = record('D', 0, 0);
    place(d, 12, 10);
    cok_combat_set_field_word(R, d->record, COK_FIELD_STATUS, 5);
    d->combat->dying = 1;
    game.vm.character = c->record;
    bool over = false;
    CHECK(queen_fight(&game, c, &over) && over && game.vm.status == COK_ECL_OK);
    CHECK(c->combat->initiative == 0 && LOGGED("bandaged") && d->combat->dying == 0 &&
          cok_combat_field_word(R, d->record, COK_FIELD_STATUS) == 4);
    battle_clear();
}

/* 5bb8:0b3d's thrown weapon at distance 1 (a bow whose type has the thrown flags 0x14, readied, with
 * the ammunition in its own slot): the ammunition is dropped (f6a-f87), so the attack is the readied
 * weapon's blow. With the ammunition kept it would be a missile's flight, unported: not_yet. */
static void test_fight_thrown(void)
{
    weapon_types();
    game.item_types.type[128][12] = 6;
    game.item_types.type[128][14] = 0x14;
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    cok_character *a = record('A', 0, 0);
    place(a, 11, 10);
    m->slots[0] = give(m, 128, 1, 0);
    m->combat->target = a->record;
    game.vm.character = m->record;
    bool over = false;
    CHECK(queen_fight(&game, m, &over) && game.vm.status == COK_ECL_OK);
    battle_clear();
}

/* 51c2:10c3's ammunition (in_reach = has): a bow with no arrows (slot 11 empty) and a seen target two
 * cells away is in reach, then 10c3 finds no ammunition, so in_reach is false after the pass (the blow
 * is still made with no item, 18d6). The flag is the only thing it sets for Dark Queen: nothing else
 * reads it, so the test reads the flag. */
static void test_fight_no_ammunition(void)
{
    weapon_types();
    game.item_types.type[128][12] = 6;
    battle_clear();
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    cok_character *a = record('A', 0, 0);
    place(a, 12, 10);
    m->slots[0] = give(m, 128, 1, 0);
    cok_field_set8_at(R->character, m->record, COK_FIELD_ATTACKS_LEFT, 1, 1);
    m->combat->target = a->record;
    game.vm.character = m->record;
    bool over = false;
    CHECK(queen_fight(&game, m, &over) && game.vm.status == COK_ECL_OK);
    CHECK(!game.combat.in_reach);
    battle_clear();
}

/* One computer's pass (queen_fight) of M against A at 11, 10 with A at one hit point and three swings for M,
 * from seed. Returns the pass's over, and A's and M's state after it. */
static bool inactive_pass(uint32_t seed, bool *over, bool *active, uint8_t *guard, uint8_t *left)
{
    battle_clear();
    cok_character *a = record('A', 0, 0);
    place(a, 11, 10);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    cok_field_set8_at(R->character, m->record, COK_FIELD_ATTACKS_LEFT, 1, 3);
    cok_field_set8_at(R->character, m->record, COK_FIELD_DICE, 1, 1);
    cok_field_set8_at(R->character, m->record, COK_FIELD_DICE_SIDES, 1, 4);
    m->combat->attack_slot = 1;
    m->combat->target = a->record;
    cok_field_set8(R->character, a->record, COK_FIELD_HIT_POINTS, 1);
    game.vm.character = m->record;
    game.vm.seed = seed;
    *over = true;
    bool ok = queen_fight(&game, m, over);
    *active = cok_field_u8(R->character, a->record, COK_FIELD_ACTIVE) != 0;
    *guard = m->combat->guarding;
    *left = cok_field_u8_at(R->character, m->record, COK_FIELD_ATTACKS_LEFT, 1);
    return ok && game.vm.status == COK_ECL_OK;
}

/* queen_fight's "inactive target sets done" (dqk 5bb8:0e9c-0ee6, after the attack 18d6): the blow kills the
 * target (it goes inactive, +0x18b 0) and leaves the attacker swings (over is false: the turn is not ended).
 * The pass is then done and goes no further: going stays set, so no guard (0fec) and the pass returns over
 * false. The first seed (in order) whose swing lands does it: a natural 1 misses, any other swing hits a target
 * of 1 hit point. */
static void test_fight_inactive_target(void)
{
    bool over = true, active = true, found = false;
    uint8_t guard = 0, left = 0;
    for (uint32_t seed = 1; seed < 400 && !found; ++seed) {
        if (!inactive_pass(seed, &over, &active, &guard, &left)) continue;
        found = !active;
    }
    CHECK(found);
    /* The pass ends here: over false, the guard not taken, the swings left as the blow left them. */
    CHECK(!over && guard == 0 && left > 0);
    battle_clear();
}

/* The memorised spells of m (+0xce), the given ids in slots 0 up, the rest 0. */
static void memorize(cok_character *m, const uint8_t *spells, size_t n)
{
    memset(m->record + 0xce, 0, 0x89);
    memcpy(m->record + 0xce, spells, n);
}

static void clear_memorized(cok_character *m)
{
    memset(m->record + 0xce, 0, 0x89);
}

/* A monster m (hostile 1, magic on, a fighter of level 3, 10 of 12 hit points) and a party member a
 * beside it (hostile 0, adjacent to the east): the spell choice's caster and its target. */
static cok_character *caster(cok_character **a_out)
{
    const cok_character_layout *L = R->character;
    battle_clear();
    cok_character *a = record('A', 0, 0);
    cok_character *m = record('M', 1, 0);
    place(m, 10, 10);
    place(a, 10, 11);
    m->combat->may_cast = 1;
    game.combat.magic_on = true;
    game.undead = 5;
    cok_field_set8(L, m->record, COK_FIELD_MAX_HIT_POINTS, 12);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 10);
    game.vm.character = m->record; /* the turn's selected record (round.c:941) */
    *a_out = a;
    return m;
}

/* The seed (from 1) whose first d100 and then d5 draws satisfy want. */
typedef bool (*draws_fn)(uint8_t d100, uint8_t d5);
static uint32_t seed_where(draws_fn want)
{
    for (uint32_t s = 1; s < 1000000; ++s) {
        uint32_t t = s;
        uint8_t a = cok_dice(&t, 1, 100);
        uint8_t b = cok_dice(&t, 1, 5);
        if (want(a, b)) return s;
    }
    CHECK(!"no seed");
    return 0;
}
static bool d100_is_24(uint8_t a, uint8_t b) { (void)b; return a == 24; }
static bool d100_above_24(uint8_t a, uint8_t b) { (void)b; return a > 24; }
static bool d100_above_12(uint8_t a, uint8_t b) { (void)b; return a > 12; }
static bool d5_is_5_after_high(uint8_t a, uint8_t b) { return a > 24 && b == 5; }

/* 5bb8:1b1e's order (queen_best_spell): the memorised bytes by priority (+0x12), the later slot first
 * among equals (Fireball 0x2f and Hold Person 0x17 both 7, MM 0x0f 5), duplicates walked past, camp-only
 * (0x0e) and bytes of 0x80 or more and 0 skipped, a spell with nothing worth it passed over. */
static void test_best_spell_order(void)
{
    cok_character *a;
    cok_character *m = caster(&a);
    uint8_t spell = 0xee;
    CHECK(queen_spell_word(&game, 0x0e, 0x0f) == 0); /* a camp-only spell (+0x0f 0) */
    CHECK(queen_spell_byte(&game, 0x0f, 0x12) == 5 && queen_spell_byte(&game, 0x2f, 0x12) == 7 &&
          queen_spell_byte(&game, 0x17, 0x12) == 7);
    /* Hold Person needs a target with flags 2 (+0xc8); without it, Fireball is the best. */
    uint8_t all[] = {0x0f, 0x2f, 0x17};
    memorize(m, all, sizeof all);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x2f);
    CHECK(game.vm.status == COK_ECL_OK);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CREATURE_FLAGS, 1, 2);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x17);
    cok_field_set8_at(R->character, a->record, COK_FIELD_CREATURE_FLAGS, 1, 0);
    uint8_t two[] = {0x0f, 0x2f};
    memorize(m, two, sizeof two);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x2f);
    uint8_t mm[] = {0x0f};
    memorize(m, mm, sizeof mm);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x0f);
    /* Camp-only, a byte of 0x80 or more, and a zero slot are skipped. */
    uint8_t skipped[] = {0x0e, 0x8f, 0x00, 0x0f};
    memorize(m, skipped, sizeof skipped);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x0f);
    /* A duplicate of a spell worth nothing is walked past: MM's target holds 0x11 (nothing worth it). */
    uint8_t dup[] = {0x0f, 0x0f};
    memorize(m, dup, sizeof dup);
    cok_character_add_effect(a, 0x11, 0, 0, false);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0);
    CHECK(game.vm.status == COK_ECL_OK);
    uint8_t dup_then[] = {0x0f, 0x0f, 0x03};
    memorize(m, dup_then, sizeof dup_then);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x03); /* walked past both MMs */
    /* Then Cure Light Wounds, worth to the caster herself, behind MM (priority 5 over 0). */
    uint8_t fall[] = {0x0f, 0x03};
    memorize(m, fall, sizeof fall);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x03);
    clear_memorized(m);
    battle_clear();
}

/* The computer's choice's dice (5bb8:0306): p is the four class levels (2, 3, 4, 1) summed as bytes
 * and shifted two (a byte: 70 levels give 24). A d100 at or below p casts nothing (one draw); above it
 * a d5 above the priority casts nothing (CLW, priority 0, never); MM (priority 5) passes the d5 and
 * casts (its aim is the computer's, 52db:1d42, a stop). */
static void test_spell_choice_dice(void)
{
    const cok_character_layout *L = R->character;
    cok_character *a;
    cok_character *m = caster(&a);
    game.vm.mode = 5; /* the battle: the cast is a battle's */
    uint8_t mm[] = {0x0f};
    memorize(m, mm, sizeof mm);
    /* Classes 1 to 4 summing 70 (60, 4, 3, 3); class 0 (50) is not one of the four. */
    static const uint8_t levels[5] = {50, 60, 4, 3, 3};
    for (unsigned i = 0; i < 5; ++i) cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, i, levels[i]);
    bool cast = true;
    uint32_t s = seed_where(d100_is_24);
    game.vm.seed = s;
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
    uint32_t t = s;
    cok_dice(&t, 1, 100);
    CHECK(game.vm.seed == t); /* exactly one draw */
    s = seed_where(d100_above_24);
    game.vm.seed = s;
    CHECK(!queen_spell_choice(&game, m, &cast) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "52db:1d42/48aa") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    /* CLW: priority 0, so the d5 always passes it over: two draws, no cast. */
    uint8_t clw[] = {0x03};
    memorize(m, clw, sizeof clw);
    for (unsigned i = 0; i < 5; ++i) cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, i, 0);
    cok_field_set8_at(L, m->record, COK_FIELD_CLASS_LEVEL, 1, 3);
    s = seed_where(d100_above_12);
    game.vm.seed = s;
    CHECK(queen_spell_choice(&game, m, &cast) && !cast && game.vm.status == COK_ECL_OK);
    t = s;
    cok_dice(&t, 1, 100);
    cok_dice(&t, 1, 5);
    CHECK(game.vm.seed == t);
    /* MM's d5 of 5 is not above its priority 5: the cast goes on (the stop). */
    memorize(m, mm, sizeof mm);
    s = seed_where(d5_is_5_after_high);
    game.vm.seed = s;
    CHECK(!queen_spell_choice(&game, m, &cast) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "52db:1d42/48aa") != NULL);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    clear_memorized(m);
    game.vm.mode = 0;
    battle_clear();
}

/* 52db:42cc (queen_target_worth): -40 for the other side; the grades of magic resistance (3, 2, 1,
 * 0 at 0x29, 0x47, 0x65, 0x83; 4 at 0x28); the table's rows (MM not on a target holding 0x11, the
 * cures on hit points below the maximum, Bless with no enemy beside its target); the globe (0x39) below
 * level 5 and 0x3f below 4 (Fireball, level 3, and Heal, level 6, unaffected). Each group on fresh
 * records. */
static void test_target_worth(void)
{
    const cok_character_layout *L = R->character;
    cok_character *a;
    int8_t w = 0;
    cok_character *m = caster(&a);
    CHECK(queen_target_worth(&game, a, 0x0f, 1, &w) && w == -40);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 4);
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0x29);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 3);
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0x47);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 2);
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0x65);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 1);
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0x83);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 0);
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0x28);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 4);
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0);
    cok_character_add_effect(a, 0x11, 0, 0, false);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 0);
    /* The cures: hit points below the maximum (a fresh target, not holding 0x11). */
    m = caster(&a);
    cok_field_set8(L, a->record, COK_FIELD_MAX_HIT_POINTS, 10);
    CHECK(queen_target_worth(&game, a, 0x03, 0, &w) && w == 0);
    cok_field_set8(L, a->record, COK_FIELD_MAX_HIT_POINTS, 12);
    CHECK(queen_target_worth(&game, a, 0x03, 0, &w) && w == 4);
    /* The globe on a fresh target: MM (level 1) is cut, Heal (level 6) is not. */
    m = caster(&a);
    cok_character_add_effect(a, 0x39, 0, 0, false);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 0);
    /* The grade overwrites the globe's 0 (the original's tail tests the resistance on its own). */
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0x30);
    CHECK(queen_target_worth(&game, a, 0x0f, 0, &w) && w == 3);
    cok_field_set8(L, a->record, COK_FIELD_MAGIC_RESISTANCE, 0);
    cok_field_set8(L, a->record, COK_FIELD_MAX_HIT_POINTS, 12);
    CHECK(queen_target_worth(&game, a, 0x24, 0, &w) && w == 4);
    /* 0x3f: Fireball (level 3) is cut, Heal (level 6) is not. */
    m = caster(&a);
    cok_field_set8(L, a->record, COK_FIELD_MAX_HIT_POINTS, 12);
    cok_character_add_effect(a, 0x3f, 0, 0, false);
    CHECK(queen_target_worth(&game, a, 0x2f, 0, &w) && w == 0);
    CHECK(queen_target_worth(&game, a, 0x24, 0, &w) && w == 4);
    /* Bless (0x01): an enemy within 1 of the target gives nothing; with none, 4. */
    battle_clear();
    cok_character *p = record('P', 0, 0);
    place(p, 10, 10);
    cok_character *q = record('Q', 1, 0);
    place(q, 10, 11);
    CHECK(queen_target_worth(&game, p, 0x01, 0, &w) && w == 0);
    battle_clear();
    p = record('P', 0, 0);
    place(p, 10, 10);
    CHECK(queen_target_worth(&game, p, 0x01, 0, &w) && w == 4);
    battle_clear();
    (void)m;
}

/* 5bb8:4789 (queen_spell_worth): the other side's allies word (+0x13 1 for MM) finds the foe beside the
 * caster; the caster's own cure (CLW, +0x13 0) is worth the caster herself. */
static void test_spell_worth(void)
{
    cok_character *a;
    cok_character *m = caster(&a);
    int8_t w = 0;
    CHECK(queen_spell_worth(&game, m, 0x0f, &w) && w == 4);
    CHECK(queen_spell_worth(&game, m, 0x03, &w) && w == 4);
    cok_character_add_effect(a, 0x11, 0, 0, false);
    CHECK(queen_spell_worth(&game, m, 0x0f, &w) && w == 0);
    battle_clear();
}

/* 52db:42cc's edges: the globe (0x39) cuts below level 5 only (0x49, level 5, kept) and 0x3f below 4
 * only (the cure 0x3a, level 4, kept; cut by the globe); Prayer's test is the selected record's, not
 * the target's. */
static void test_target_worth_edges(void)
{
    const cok_character_layout *L = R->character;
    cok_character *a;
    int8_t w = 0;
    cok_character *m = caster(&a);
    /* Prayer: the caster (the selected record) holding 0x31 makes it worth nothing to anyone. */
    CHECK(queen_target_worth(&game, a, 0x2a, 0, &w) && w == 4);
    cok_character_add_effect(m, 0x31, 0, 0, false);
    CHECK(queen_target_worth(&game, a, 0x2a, 0, &w) && w == 0);
    CHECK(queen_spell_byte(&game, 0x49, 2) == 5 && queen_spell_byte(&game, 0x3a, 2) == 4);
    cok_field_set8(L, a->record, COK_FIELD_MAX_HIT_POINTS, 12);
    cok_character_add_effect(a, 0x3f, 0, 0, false);
    CHECK(queen_target_worth(&game, a, 0x3a, 0, &w) && w == 4);
    cok_character_add_effect(a, 0x39, 0, 0, false);
    CHECK(queen_target_worth(&game, a, 0x3a, 0, &w) && w == 0);
    CHECK(queen_target_worth(&game, a, 0x49, 0, &w) && w == 4);
    battle_clear();
}

/* 5bb8:1a07's edges: the last memorised slot (+0xce + 0x88) is read, and a byte of 0x80 (a spell's id
 * in the table, castable) is skipped like any byte from 0x80 on. */
static void test_best_spell_edges(void)
{
    cok_character *a;
    cok_character *m = caster(&a);
    uint8_t spell = 0xee;
    clear_memorized(m);
    m->record[0xce + 0x88] = 0x0f;
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0x0f);
    CHECK(queen_spell_word(&game, 0x80, 0x0f) != 0 && queen_spell_byte(&game, 0x80, 7) == 0);
    uint8_t high[] = {0x80};
    memorize(m, high, sizeof high);
    CHECK(queen_best_spell(&game, m, &spell) && spell == 0);
    clear_memorized(m);
    battle_clear();
}

/* 52db:4789 with a spell of kind 0 (+7 0: 0x1c, its effect 0x17, the caster's own side, a range of 3):
 * the worth is 42cc's on the caster alone, not on those around her (an ally beside her not holding 0x17). */
static void test_spell_worth_self(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    cok_character *b = record('B', 1, 0);
    place(m, 10, 10);
    place(b, 10, 11);
    game.vm.character = m->record;
    CHECK(queen_spell_byte(&game, 0x1c, 7) == 0 && queen_spell_byte(&game, 0x1c, 0x0e) == 0x17 &&
          queen_spell_byte(&game, 0x1c, 3) == 3);
    int8_t w = 0;
    CHECK(queen_spell_worth(&game, m, 0x1c, &w) && w == 4);
    cok_character_add_effect(m, 0x17, 0, 0, false);
    CHECK(queen_spell_worth(&game, m, 0x1c, &w) && w == 0);
    battle_clear();
}

/* 52db:4789's loop runs to the count it read once (DS:c2dd into a local, 4841): Bless's test (51c2:0980)
 * lists again around each target and leaves that target's enemies in the list with their count, which
 * the loop does not read again, so it walks on through the entries left after them: here past the
 * caster (an enemy beside her) and the enemy to the ally two cells away, worth 4. */
static void test_spell_worth_bless(void)
{
    battle_clear();
    cok_character *m = record('M', 1, 0);
    cok_character *a = record('A', 0, 0);
    cok_character *b = record('B', 1, 0);
    place(m, 10, 10);
    place(a, 10, 11);
    place(b, 10, 13);
    game.vm.character = m->record;
    uint8_t range = 0;
    CHECK(queen_spell_range(&game, 0x01, &range) && range >= 3);
    int8_t w = 0;
    CHECK(queen_spell_worth(&game, m, 0x01, &w) && w == 4);
    battle_clear();
}

/* 5bb8:0170 (the pending cast of a monster): MM is cast by the computer: the spell is still kept
 * while it casts (the stop comes before any change), the caster is the selected record, and the
 * computer's aim (52db:1d42/48aa) is the stop. */
static void test_pending_cast(void)
{
    cok_character *a;
    cok_character *m = caster(&a);
    game.vm.mode = 5;
    m->combat->spell = 0x0f;
    m->combat->target = a->record;
    game.vm.character = a->record; /* not the caster: the pending cast sets it and puts it back */
    CHECK(!queen_computer(&game, m) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "52db:1d42/48aa") != NULL);
    CHECK(m->combat->spell == 0x0f && game.vm.character == a->record);
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
    game.vm.mode = 0;
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
    test_undead_search();
    test_turn_undead_levels();
    test_turn_undead_d20();
    test_turn_undead_limit();
    test_turn_undead_count();
    test_computer_pick();
    test_spell_choice();
    test_best_spell_order();
    test_spell_choice_dice();
    test_target_worth();
    test_spell_worth();
    test_target_worth_edges();
    test_best_spell_edges();
    test_spell_worth_self();
    test_spell_worth_bless();
    test_pending_cast();
    test_weapon_needs();
    test_weapon_score();
    test_weapon_choice();
    test_weapon_cursed();
    test_weapon_monster();
    test_weapon_swap();
    test_weapon_keep();
    test_weapon_shield();
    test_fight_steps();
    test_fight_spell();
    test_fight_underwater();
    test_fight_count();
    test_fight_pick();
    test_fight_sweep();
    test_distance_kept();
    test_loop_initiative();
    test_fight_bandage();
    test_fight_thrown();
    test_fight_no_ammunition();
    test_fight_inactive_target();
    cok_party_free(&game.party);
    puts("queen ai tests passed");
    return 0;
}
