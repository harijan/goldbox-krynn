/* Death Knights' band C spells and effects (dkk 5dc5's and 504d's
 * routines that have no Champions counterpart; DEVELOPERS.md's "Death
 * Knights' casting" and "Death Knights' effects"): each handler on records
 * made by hand in its layout, the game opened on DKKAssets/ (skipped
 * without); and the camp's Fix (4381:2fd4), which cures with them. */
#include "adventure.h"
#include "camp.h"
#include "cast.h"
#include "combat.h"
#include "effect.h"
#include "train.h"

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

#define LOGGED(text) (strstr(log_text, text) != NULL)

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
    memset(c->body, 0, sizeof c->body);
    c->count = 1;
    c->bodies = 0;
    c->view_x = c->view_y = 0;
    c->active = true;
    memset(game.spell_targets, 0, sizeof game.spell_targets);
    game.spell_target_count = 0;
    log_text[0] = '\0';
}

/* A record of Death Knights' layout named by a letter, one cell, 10 hit
 * points, on side, able to act, saving throws of 11, with a zeroed combat
 * record, a combatant at x, y. */
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
    cok_field_set8(L, r, COK_FIELD_RACE, 2);
    for (size_t k = 0; k < 5; ++k) cok_field_set8_at(L, r, COK_FIELD_SAVING_THROW, k, 11);
    for (size_t k = 0; k < 6; ++k) {
        cok_field_set8_at(L, r, COK_FIELD_SCORE_BASE, k, 12);
        cok_field_set8_at(L, r, COK_FIELD_SCORE, k, 12);
    }
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
    return c;
}

/* Spell's handler on targets (NULL-ended), as cok_cast_on runs it: its
 * caster the first target, the caster level the selected character's,
 * caster's (a mage of level 9 where it has none), the spell being cast
 * set. */
static bool cast_at(cok_character *caster, uint8_t spell, cok_character *const *targets)
{
    const cok_character_layout *L = R->character;
    if (cok_field_s8_at(L, caster->record, COK_FIELD_CLASS_LEVEL, 5) == 0)
        cok_field_set8_at(L, caster->record, COK_FIELD_CLASS_LEVEL, 5, 9);
    uint8_t n = 0;
    while (targets[n] != NULL) {
        game.spell_targets[n + 1] = targets[n];
        ++n;
    }
    game.spell_target_count = n;
    game.vm.character = caster->record;
    log_text[0] = '\0';
    return cok_cast_on(&game, spell, targets[0], true);
}

/* The first seed after start whose first d20 is within lo-hi. */
static uint32_t seed_d20(uint32_t start, unsigned lo, unsigned hi)
{
    for (;; ++start) {
        uint32_t copy = start;
        unsigned d = cok_dice(&copy, 1, 20);
        if (d >= lo && d <= hi) return start;
    }
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

static unsigned level_of(const cok_character *c, unsigned k)
{
    return (uint8_t)cok_field_s8_at(R->character, c->record, COK_FIELD_CLASS_LEVEL, k);
}

/* 5dc5:4248, Slay Living: an effect cancelling it on event 9 (0x57's
 * cancel(0)) "is unaffected"; a failed save (type 4) "is slain" (status
 * 6); a save made, or the creature flag 0x08 of +0x7b, 2d8 + 1 instead. */
static void test_slay_living(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = record('W', 0, 5, 5), *t = record('T', 1, 6, 5);
    CHECK(cok_character_add_effect(t, 0x57, 0, 0, false) != NULL);
    CHECK(cast_at(w, 0x4c, (cok_character *[]){t, NULL}));
    CHECK(LOGGED("print: is unaffected;") && cok_field_u8(L, t->record, COK_FIELD_STATUS) == 0);
    reset();
    w = record('W', 0, 5, 5), t = record('T', 1, 6, 5);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, 21); /* a 20 saves */
    cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, 40);
    game.vm.seed = seed_d20(1, 1, 19);
    CHECK(cast_at(w, 0x4c, (cok_character *[]){t, NULL}));
    CHECK(LOGGED("print: is slain;") && cok_field_u8(L, t->record, COK_FIELD_STATUS) == 6);
    for (int flag = 0; flag <= 1; ++flag) {
        reset();
        w = record('W', 0, 5, 5), t = record('T', 1, 6, 5);
        cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, 40);
        cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, flag ? 21 : 2);
        if (flag) cok_field_set8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 1, 8);
        uint32_t seed = seed_d20(5, 2, 19), copy = seed;
        (void)cok_dice(&copy, 1, 20); /* the save */
        unsigned d = cok_dice(&copy, 2, 8) + 1u;
        game.vm.seed = seed;
        CHECK(cast_at(w, 0x4c, (cok_character *[]){t, NULL}));
        CHECK(cok_field_u8(L, t->record, COK_FIELD_HIT_POINTS) == 40 - d &&
              cok_field_u8(L, t->record, COK_FIELD_STATUS) == 0 && !LOGGED("slain"));
    }
}

/* 5dc5:3bb9 and 40a6: a failed save of type 4 kills with the entry's
 * status and words; a save made does nothing. */
static void test_destroy(void)
{
    const cok_character_layout *L = R->character;
    for (int spell = 0; spell < 2; ++spell) {
        for (int saves = 0; saves <= 1; ++saves) {
            reset();
            cok_character *w = record('W', 0, 5, 5), *t = record('T', 1, 6, 5);
            cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, saves ? 1 : 21);
            game.vm.seed = seed_d20(9, 2, 19);
            CHECK(cast_at(w, spell ? 0x48 : 0x3c, (cok_character *[]){t, NULL}));
            uint8_t status = cok_field_u8(L, t->record, COK_FIELD_STATUS);
            CHECK(saves ? status == 0 : status == (spell ? 7 : 6));
            CHECK(LOGGED(spell ? "print: is turned to stone;" : "print: disentegrates;") != saves);
        }
    }
}

/* 5dc5:398d, Death Spell: 2 x 4d20 of hit dice by +0x30's weights, in
 * turn; the creature flag 0x08 spares one and it is taken off the list. */
static void test_death_spell(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = record('W', 0, 5, 5);
    cok_character *t[5];
    const int8_t hd[5] = {0, 6, 6, 9, 1};
    for (int i = 0; i < 5; ++i) {
        t[i] = record((char)('A' + i), 1, (int8_t)(7 + i), 5);
        cok_field_set8(L, t[i]->record, COK_FIELD_HIGHEST_LEVEL, (uint8_t)hd[i]);
    }
    cok_field_set8(L, t[2]->record, COK_FIELD_MAX_HIT_POINTS, 0x34); /* 20, not 16 */
    cok_field_set8_at(L, t[4]->record, COK_FIELD_CREATURE_FLAGS, 1, 8);
    uint32_t seed = 1, copy;
    unsigned pool;
    do {
        copy = ++seed;
        pool = (uint8_t)(cok_dice(&copy, 4, 20) * 2);
    } while (pool < 2 * 67); /* 3 x 4d20 would reach D's 162 too */
    game.vm.seed = seed;
    CHECK(cast_at(w, 0x38, (cok_character *[]){t[0], t[1], t[2], t[3], t[4], NULL}));
    for (int i = 0; i < 5; ++i)
        CHECK((cok_field_u8(L, t[i]->record, COK_FIELD_STATUS) == 6) == (i < 3));
    CHECK(game.spell_targets[4] == NULL && game.spell_targets[5] == NULL &&
          game.spell_targets[3] == t[2]);
}

/* 5dc5:2d8e, Harm: the hit points less 1d4, magic. */
static void test_harm(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = record('W', 0, 5, 5), *t = record('T', 1, 6, 5);
    cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, 30);
    uint32_t copy = 4;
    unsigned d = cok_dice(&copy, 1, 4);
    game.vm.seed = 4;
    CHECK(cast_at(w, 0x28, (cok_character *[]){t, NULL}));
    CHECK(cok_field_u8(L, t->record, COK_FIELD_HIT_POINTS) == d);
}

/* 5dc5:3848, Heal: blindness and 0x3d cured ("can see", "regains
 * intellect"), the hit points the most less 1d4, "is healed"; within 1d4
 * of the most, healed by none (638c:206f with 0), "is healed" too. */
static void test_heal(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *t = record('T', 0, 5, 5);
    cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, 2);
    cok_field_set8(L, t->record, COK_FIELD_MAX_HIT_POINTS, 30);
    CHECK(cok_character_add_effect(t, 0x21, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(t, 0x3d, 0, 0, false) != NULL);
    uint32_t copy = 7;
    unsigned d = cok_dice(&copy, 1, 4);
    game.vm.seed = 7;
    CHECK(cast_at(t, 0x35, (cok_character *[]){t, NULL}));
    CHECK(cok_field_u8(L, t->record, COK_FIELD_HIT_POINTS) == 30 - d &&
          cok_character_find_effect(t, 0x21) == NULL && cok_character_find_effect(t, 0x3d) == NULL);
    CHECK(LOGGED("print: can see;") && LOGGED("print: regains intellect;") &&
          LOGGED("print: is healed;"));
    cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, 29);
    uint32_t seed = 7;
    do {
        copy = ++seed;
    } while (cok_dice(&copy, 1, 4) < 2); /* 29 + 2 past 30 */
    game.vm.seed = seed;
    CHECK(cast_at(t, 0x35, (cok_character *[]){t, NULL}));
    CHECK(cok_field_u8(L, t->record, COK_FIELD_HIT_POINTS) == 29 && LOGGED("is healed") &&
          !LOGGED("can see"));
}

/* 5dc5:4196, Raise Dead (dead, a race but 0 and 1): okay with 1 hit point
 * and a point of constitution less, "is raised"; an elf (race 1) not.
 * 1f70, Ressurection (race above 1, not an elf either): the most hit
 * points. */
static void test_raise(void)
{
    const cok_character_layout *L = R->character;
    for (int k = 0; k < 4; ++k) {
        reset();
        cok_character *t = record('T', 0, 5, 5);
        cok_field_set8(L, t->record, COK_FIELD_STATUS, 6);
        cok_field_set8(L, t->record, COK_FIELD_ACTIVE, 0);
        cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, 0);
        cok_field_set8(L, t->record, COK_FIELD_RACE, k == 1 || k == 3 ? 1 : 2);
        cok_field_set8_at(L, t->record, COK_FIELD_CLASS_LEVEL, 2, 1); /* con's hit points */
        CHECK(cok_character_add_effect(t, 0x37, 0, 0, false) != NULL);
        game.vm.mode = 4;
        CHECK(cast_at(t, k >= 2 ? 0x07 : 0x4b, (cok_character *[]){t, NULL}));
        uint8_t *r = t->record;
        if (k == 1 || k == 3) {
            CHECK(cok_field_u8(L, r, COK_FIELD_STATUS) == 6 && !LOGGED("raised") && !LOGGED("ressurected"));
            continue;
        }
        CHECK(cok_field_u8(L, r, COK_FIELD_STATUS) == 0 && cok_field_u8(L, r, COK_FIELD_ACTIVE) == 1 &&
              cok_character_find_effect(t, 0x37) == NULL);
        CHECK(k == 2 ? cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == 10 && LOGGED("is ressurected") &&
                           cok_field_u8_at(L, r, COK_FIELD_SCORE_BASE, COK_SCORE_CONSTITUTION) == 12
                     : cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == 1 && LOGGED("print: is raised;") &&
                           cok_field_u8_at(L, r, COK_FIELD_SCORE_BASE, COK_SCORE_CONSTITUTION) == 11);
        /* The temple's cast leaves the spell set (dkk 3999:0293): a
         * Barkskin holder's next save, before any cast, is against Raise
         * Dead's type 4 (no 1 better), not against none. */
        CHECK(game.effects.rolls.spell == (k == 2 ? 0x07 : 0x4b));
        if (k == 0) {
            cok_character *b = record('B', 0, 6, 5);
            CHECK(cok_character_add_effect(b, 0x30, 0, 0, false) != NULL);
            uint32_t seed = seed_d20(1, 2, 19), copy = seed;
            unsigned d = cok_dice(&copy, 1, 20);
            game.vm.seed = seed;
            bool made;
            CHECK(cok_effects_save(&game.effects, b, 0, 0, &made) &&
                  game.effects.rolls.save_roll == d);
        }
    }
}

/* 5dc5:2326, Restoration: of a fighter and a mage drained of a level each,
 * the class whose next level asks the less experience rises, the most hit
 * points by the lack over the levels lost, the experience that level's;
 * none drained, nothing but may-train. */
static void test_restoration(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *t = record('T', 0, 5, 5);
    uint8_t *r = t->record;
    game.vm.mode = 4;
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 2, 4);     /* fighter 4 of 5 */
    cok_field_set8_at(L, r, COK_FIELD_UNDRAINED_LEVEL, 2, 5);
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 5, 4);     /* mage 4 of 5 */
    cok_field_set8_at(L, r, COK_FIELD_UNDRAINED_LEVEL, 5, 5);
    cok_field_set8(L, r, COK_FIELD_MAGIC_ORDER, 2);
    cok_field_set8(L, r, COK_FIELD_MAX_HIT_POINTS, 20);
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 20);
    cok_field_set8(L, r, COK_FIELD_UNDRAINED_MAX_HIT_POINTS, 30);
    cok_field_set8(L, r, COK_FIELD_BASE_HIT_POINTS, 15);
    cok_field_set32(L, r, COK_FIELD_EXPERIENCE, 100);
    cok_field_set32(L, r, COK_FIELD_UNDRAINED_EXPERIENCE, 900000);
    int32_t fighter, mage;
    CHECK(cok_train_experience(R, 2, 5, &fighter) && cok_train_experience(R, 8, 5, &mage));
    CHECK(fighter != mage);
    unsigned k = fighter < mage ? 2 : 5;
    CHECK(cast_at(t, 0x11, (cok_character *[]){t, NULL}));
    CHECK(game.vm.status == COK_ECL_OK && level_of(t, k) == 5 && level_of(t, 7 - k) == 4);
    CHECK(cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS) == 25 &&
          cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == 25 &&
          cok_field_u8(L, r, COK_FIELD_BASE_HIT_POINTS) == 20); /* con 12: no bonus */
    CHECK((int32_t)cok_field_u32(L, r, COK_FIELD_EXPERIENCE) == (k == 2 ? fighter : mage));
    /* The last level lost, alone: the experience before the drain. */
    CHECK(cast_at(t, 0x11, (cok_character *[]){t, NULL}));
    CHECK(level_of(t, 7 - k) == 5 && cok_field_u32(L, r, COK_FIELD_EXPERIENCE) == 900000 &&
          cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS) == 30);
    CHECK(cast_at(t, 0x11, (cok_character *[]){t, NULL}) && level_of(t, 2) == 5 &&
          level_of(t, 5) == 5 && cok_field_u32(L, r, COK_FIELD_EXPERIENCE) == 900000);
    /* Of equals the later: a mage of the order 2 and a knight of the Crown
     * at level 2 both ask 5000 (rows 8 and 3); the knight rises. */
    reset();
    t = record('T', 0, 5, 5);
    r = t->record;
    game.vm.mode = 4;
    cok_field_set8(L, r, COK_FIELD_MAGIC_ORDER, 2);
    cok_field_set8(L, r, COK_FIELD_KNIGHT_ORDER, 1);
    for (unsigned klass = 5; klass <= 7; klass += 2) {
        cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, klass, 1);
        cok_field_set8_at(L, r, COK_FIELD_UNDRAINED_LEVEL, klass, 2);
    }
    CHECK(cok_train_experience(R, 8, 2, &fighter) && cok_train_experience(R, 3, 2, &mage) &&
          fighter == mage);
    CHECK(cast_at(t, 0x11, (cok_character *[]){t, NULL}) && level_of(t, 7) == 2 &&
          level_of(t, 5) == 1);
}

/* 5dc5:2326's division by the levels lost, a byte: two classes 128 below
 * their undrained levels wrap it to 0, runtime error 200 (5dc5:248c), with
 * the most hit points below those not drained. */
static void test_restoration_wraps(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *t = record('T', 0, 5, 5);
    uint8_t *r = t->record;
    game.vm.mode = 4;
    cok_field_set8(L, r, COK_FIELD_MAGIC_ORDER, 2);
    for (unsigned klass = 2; klass <= 5; klass += 3) {
        cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, klass, (uint8_t)-100);
        cok_field_set8_at(L, r, COK_FIELD_UNDRAINED_LEVEL, klass, 28);
    }
    cok_field_set8(L, r, COK_FIELD_MAX_HIT_POINTS, 20);
    cok_field_set8(L, r, COK_FIELD_UNDRAINED_MAX_HIT_POINTS, 30);
    CHECK(!cast_at(t, 0x11, (cok_character *[]){t, NULL}));
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "5dc5:248c") != NULL);
}

/* 5dc5:4a04, Feeblemind: the save of type 4 harder by class while the
 * spell applies its 0x44, then put back; 0x44's handler run, which drains
 * the target of the holder's attack (504d:1836). */
static void test_feeblemind(void)
{
    const cok_character_layout *L = R->character;
    /* A roll of 11 against 12 - 1 (a cleric's) makes it, against 8 + 4 (a
     * mage's) and 10 + 2 (others') fails it. */
    const struct { uint8_t klass, throw_, fails; } cases[] = {{0, 12, 11}, {5, 8, 11}, {2, 10, 11}};
    for (size_t i = 0; i < 3; ++i) {
        reset();
        cok_character *w = record('W', 1, 5, 5), *t = record('T', 0, 6, 5);
        cok_field_set8(L, t->record, COK_FIELD_CLASS, cases[i].klass);
        cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, cases[i].throw_);
        /* A save roll of 11 (d20, no bonus): made against the throw, not
         * against it made harder. */
        uint32_t seed = 0, copy;
        do {
            copy = ++seed;
        } while (cok_dice(&copy, 1, 20) != cases[i].fails);
        game.vm.seed = seed;
        CHECK(cast_at(w, 0x5d, (cok_character *[]){t, NULL}));
        CHECK(game.vm.status == COK_ECL_OK);
        CHECK(cok_field_u8_at(L, t->record, COK_FIELD_SAVING_THROW, 4) == cases[i].throw_);
        CHECK((cok_character_find_effect(t, 0x44) != NULL) == (cases[i].klass != 0));
    }
}

/* 5dc5:4dd8, Otto's Irresistible Dance: a hit gives 6 and 0x39 at the
 * caster level, "begins to dance wildly"; a miss nothing. (The attacker
 * is the handler's caster, here the target, its THAC0 recomputed.) */
static void test_dance(void)
{
    const cok_character_layout *L = R->character;
    for (int hits = 0; hits <= 1; ++hits) {
        reset();
        cok_character *w = record('W', 1, 5, 5), *t = record('T', 0, 6, 5);
        cok_field_set8(L, t->record, COK_FIELD_BASE_THAC0, hits ? 80 : 0);
        cok_field_set8(L, t->record, COK_FIELD_BASE_AC, 70); /* AC -10, recomputed */
        game.vm.seed = seed_d20(11, 2, 19);
        CHECK(cast_at(w, 0x71, (cok_character *[]){t, NULL}));
        CHECK(game.vm.status == COK_ECL_OK);
        cok_effect *held = cok_character_find_effect(t, 6), *ac = cok_character_find_effect(t, 0x39);
        CHECK(hits ? held != NULL && ac != NULL && held->value == 9 && ac->value == 9 &&
                         LOGGED("print: begins to dance wildly;")
                   : held == NULL && ac == NULL);
    }
}

/* 5dc5:4c8a, Mass Charm: twice the caster level (9: 18) of hit dice, in
 * turn; one without the creature flag 2 or larger than a man "is
 * unaffected"; one too many for what is left passed over; 0x0b with the
 * caster's side in bit 7, as the save (of kind 1) allows. */
static void test_mass_charm(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = record('W', 1, 5, 5);
    cok_character *t[7];
    const uint8_t hd[7] = {8, 3, 12, 4, 2, 6, 0}, flags[7] = {2, 0, 2, 2, 2, 2, 2};
    for (int i = 0; i < 7; ++i) {
        t[i] = record((char)('A' + i), i == 0, (int8_t)(7 + i), 5); /* A the caster */
        cok_field_set8(L, t[i]->record, COK_FIELD_HIGHEST_LEVEL, hd[i]);
        cok_field_set8_at(L, t[i]->record, COK_FIELD_CREATURE_FLAGS, 0, flags[i]);
        cok_field_set8_at(L, t[i]->record, COK_FIELD_SAVING_THROW, 4, 21);
    }
    cok_field_set8(L, t[4]->record, COK_FIELD_SIZE, 2);
    CHECK(cast_at(w, 0x6f, (cok_character *[]){t[0], t[1], t[2], t[3], t[4], t[5], t[6], NULL}));
    CHECK(game.vm.status == COK_ECL_OK && LOGGED("print: is unaffected;"));
    /* 8 + 4 + 6 = 18: A, D and F; B and E (larger) unaffected, C (12) past
     * what is left; G (0) after the 18 used up exactly. */
    const bool charmed[7] = {true, false, false, true, false, true, false};
    for (int i = 0; i < 7; ++i) {
        cok_effect *e = cok_character_find_effect(t[i], 0x0b);
        CHECK((e != NULL) == charmed[i]);
        /* The caster's side (A's, 1) in bit 7, taken hold of (3f44:03cd adds
         * 0x20 and the side it had in bit 6). */
        if (e != NULL)
            CHECK((e->value & 0x9f) == 9 + 0x80 && e->on_remove &&
                  cok_field_u8(L, t[i]->record, COK_FIELD_HOSTILE) == 1);
    }
}

/* 5dc5:4bd9 and 4e81, the power words: Stun's minutes by the hit points
 * (4d4 to 30, none past 90); Blind's 100 most hit points in all. */
static void test_power_words(void)
{
    const cok_character_layout *L = R->character;
    const uint8_t hps[3] = {25, 75, 95};
    for (int i = 0; i < 3; ++i) {
        reset();
        cok_character *w = record('W', 1, 5, 5), *t = record('T', 0, 6, 5);
        cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, hps[i]);
        uint32_t copy = 13;
        unsigned minutes = i == 0 ? cok_dice(&copy, 4, 4) : i == 1 ? cok_dice(&copy, 1, 4) : 0;
        game.vm.seed = 13;
        CHECK(cast_at(w, 0x6e, (cok_character *[]){t, NULL}));
        cok_effect *e = cok_character_find_effect(t, 6);
        CHECK(e != NULL && e->duration == minutes && LOGGED("print: is stunned;"));
    }
    reset();
    cok_character *w = record('W', 1, 5, 5), *t[4];
    const uint8_t most[4] = {60, 50, 30, 120};
    for (int i = 0; i < 4; ++i) {
        t[i] = record((char)('A' + i), 0, (int8_t)(7 + i), 5);
        cok_field_set8(L, t[i]->record, COK_FIELD_MAX_HIT_POINTS, most[i]);
    }
    CHECK(cast_at(w, 0x72, (cok_character *[]){t[0], t[1], t[2], t[3], NULL}));
    for (int i = 0; i < 4; ++i)
        CHECK((cok_character_find_effect(t[i], 0x21) != NULL) == (i == 0 || i == 2));
}

/* 5dc5:1e4d, Charm Person/Mammal: one of race 7 without the creature flag
 * 0x10 "is not Charmed"; a large one of another race is charmed. */
static void test_charm_mammal(void)
{
    const cok_character_layout *L = R->character;
    for (int k = 0; k < 3; ++k) {
        reset();
        cok_character *w = record('W', 1, 5, 5), *t = record('T', 0, 6, 5);
        cok_field_set8(L, t->record, COK_FIELD_RACE, k == 1 ? 3 : 7);
        if (k == 2) cok_field_set8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0, 0x10);
        cok_field_set8(L, t->record, COK_FIELD_SIZE, 2);
        cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, 21);
        CHECK(cast_at(w, 0x04, (cok_character *[]){t, NULL}));
        CHECK(k == 0 ? LOGGED("print: is not Charmed;") && cok_character_find_effect(t, 0x0b) == NULL
                     : cok_character_find_effect(t, 0x0b) != NULL);
    }
}

/* 5dc5:42f1, Entangle: nothing in a 3D area; else 6 for a failed save. */
static void test_entangle(void)
{
    const cok_character_layout *L = R->character;
    for (int area = 0; area <= 1; ++area) {
        reset();
        cok_character *w = record('W', 1, 5, 5), *t = record('T', 0, 6, 5);
        cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, 21);
        cok_ecl_set_var(&game.vm, COK_VAR_3D, (uint16_t)area);
        CHECK(cast_at(w, 0x4e, (cok_character *[]){t, NULL}));
        CHECK((cok_character_find_effect(t, 6) != NULL) == (area == 0) &&
              LOGGED("print: is entangled;") == (area == 0));
    }
    /* A save made (kind 1): "is unaffected" (638c:1f86's DS:1d0f). */
    reset();
    cok_character *w = record('W', 1, 5, 5), *t = record('T', 0, 6, 5);
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, 1);
    game.vm.seed = seed_d20(1, 2, 20);
    CHECK(cast_at(w, 0x4e, (cok_character *[]){t, NULL}));
    CHECK(cok_character_find_effect(t, 6) == NULL && LOGGED("print: T;print: is unaffected;"));
}

/* 5dc5:4109, Dispel Evil: 0x04 and 0x22 on the caster. 504d:0219, 0x04: an
 * attacker of creature flag 1 is 7 worse to hit. 504d:0aee, 0x22: the target
 * of a hit, of creature flag 1, failing a save, "is dispelled" (status 8)
 * and the holder loses both; else "resists dispel evil". */
static void test_dispel_evil(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *c = record('C', 0, 5, 5), *e = record('E', 1, 6, 5);
    game.vm.mode = 4;
    CHECK(cast_at(c, 0x49, (cok_character *[]){c, NULL}));
    CHECK(cok_character_find_effect(c, 0x04) != NULL && cok_character_find_effect(c, 0x22) != NULL);
    game.vm.mode = 5;
    cok_field_set8_at(L, e->record, COK_FIELD_CREATURE_FLAGS, 0, 1);
    game.vm.character = e->record;
    game.effects.rolls.attack_roll = 20;
    CHECK(cok_effects_dispatch(&game.effects, c, 0x0b) && game.effects.rolls.attack_roll == 13);
    cok_field_set8_at(L, e->record, COK_FIELD_CREATURE_FLAGS, 0, 0);
    game.effects.rolls.attack_roll = 20;
    CHECK(cok_effects_dispatch(&game.effects, c, 0x0b) && game.effects.rolls.attack_roll == 20);
    /* The hit: none of flag 1 resists; one of it failing is dispelled. */
    c->combat->target = e->record;
    log_text[0] = '\0';
    CHECK(cok_effects_dispatch(&game.effects, c, 4) && LOGGED("print: resists dispel evil;"));
    cok_field_set8_at(L, e->record, COK_FIELD_CREATURE_FLAGS, 0, 1);
    cok_field_set8_at(L, e->record, COK_FIELD_SAVING_THROW, 4, 21);
    game.vm.seed = seed_d20(2, 2, 19);
    CHECK(cok_effects_dispatch(&game.effects, c, 4) && LOGGED("print: is dispelled;"));
    CHECK(cok_field_u8(L, e->record, COK_FIELD_STATUS) == 8 &&
          cok_character_find_effect(c, 0x04) == NULL && cok_character_find_effect(c, 0x22) == NULL);
}

/* 504d:05bf, 0x15 (silence): in a battle, "is silenced", items and spells
 * barred, and a spell being cast lost ("lost a spell", forgotten, the turn
 * over); outside a battle nothing. */
static void test_silence(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *c = record('C', 0, 5, 5);
    cok_field_set_spells(L, c->record, true);
    cok_field_set8_at(L, c->record, COK_FIELD_MEMORIZED, 0, 0x0f);
    CHECK(cok_character_add_effect(c, 0x15, 0, 0, false) != NULL);
    c->combat->spell = 0x0f;
    c->combat->may_use = 1;
    c->combat->may_cast = 1;
    c->combat->initiative = 5;
    game.vm.mode = 4;
    CHECK(cok_effects_dispatch(&game.effects, c, 0x0f) && c->combat->may_cast == 1);
    game.vm.mode = 5;
    CHECK(cok_effects_dispatch(&game.effects, c, 0x0f));
    CHECK(LOGGED("print: is silenced;") && LOGGED("print: lost a spell;"));
    CHECK(c->combat->may_use == 0 && c->combat->may_cast == 0 && c->combat->spell == 0 &&
          c->combat->initiative == 0 &&
          cok_field_u8_at(L, c->record, COK_FIELD_MEMORIZED, 0) != 0x0f);
}

/* 504d:0f4f, 0x30 (Barkskin's): the AC 1 better at each event, the save 1
 * better but against a spell of save type 4. */
static void test_barkskin(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *c = record('C', 0, 5, 5);
    CHECK(cok_character_add_effect(c, 0x30, 0, 0, false) != NULL);
    cok_field_set8(L, c->record, COK_FIELD_AC, 50);
    for (int spell = 0; spell <= 1; ++spell) {
        /* Stinking Cloud's save is of type 0, Slay Living's of 4. */
        game.effects.rolls.spell = spell ? 0x4c : 0x22;
        game.effects.rolls.save_roll = 10;
        CHECK(cok_effects_dispatch(&game.effects, c, 0x0b));
        CHECK(game.effects.rolls.save_roll == (spell ? 10 : 11));
    }
    CHECK(cok_field_u8(L, c->record, COK_FIELD_AC) == 52 &&
          cok_field_u8(L, c->record, COK_FIELD_AC_BEHIND) == 2);
    game.effects.rolls.spell = 0;
    game.effects.rolls.save_roll = 10;
    CHECK(cok_effects_dispatch(&game.effects, c, 0x0b) && game.effects.rolls.save_roll == 11);
}

/* 504d:0124, 0x02: a hit's target that fails a save of type 1 gets 6 for
 * 3 minutes, "is stunned"; outside a battle nothing. 504d:1063, 0x3a:
 * cancels a pending 0x0b, 0x23, 0x34, 0x3d or 0x6f. */
static void test_stun_and_blank(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *c = record('C', 0, 5, 5), *e = record('E', 1, 6, 5);
    CHECK(cok_character_add_effect(c, 0x02, 0, 0, false) != NULL);
    c->combat->target = e->record;
    cok_field_set8_at(L, e->record, COK_FIELD_SAVING_THROW, 1, 21);
    game.vm.seed = seed_d20(2, 2, 19);
    CHECK(cok_effects_dispatch(&game.effects, c, 2));
    cok_effect *held = cok_character_find_effect(e, 6);
    CHECK(held != NULL && held->duration == 3 && LOGGED("is stunned"));
    cok_character *f = record('F', 1, 4, 5);
    c->combat->target = f->record;
    cok_field_set8_at(L, f->record, COK_FIELD_SAVING_THROW, 1, 1);
    game.vm.seed = seed_d20(2, 2, 20);
    CHECK(cok_effects_dispatch(&game.effects, c, 2) && cok_character_find_effect(f, 6) == NULL);
    const uint8_t ids[] = {0x0b, 0x23, 0x34, 0x3d, 0x6f, 0x0c};
    for (size_t i = 0; i < sizeof ids; ++i) {
        game.effects.rolls.pending = ids[i];
        game.effects.rolls.amount = 5;
        CHECK(cok_effects_run(&game.effects, c, 0x3a, NULL));
        CHECK((game.effects.rolls.pending == 0) == (ids[i] != 0x0c));
    }
}

/* 5dc5:4962, Cone of Cold: from the caster toward the spell's cell a cone
 * (175d) of width 2 and (level + 1) / 2 long (a level 9 mage: 5, the line
 * walked to 10 half cells: cells 6-10 of row 5, not 11 or 12), level d4 +
 * level of cold and magic to each (the save of kind 2 failed). */
static void test_cone_of_cold(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *w = record('W', 1, 5, 5), *t[7];
    for (int i = 0; i < 7; ++i) {
        t[i] = record((char)('A' + i), 0, (int8_t)(6 + i), 5);
        cok_field_set8(L, t[i]->record, COK_FIELD_HIT_POINTS, 60);
        cok_field_set8(L, t[i]->record, COK_FIELD_MAX_HIT_POINTS, 60);
        cok_field_set8_at(L, t[i]->record, COK_FIELD_SAVING_THROW, 4, 21);
    }
    uint32_t seed = 0, copy;
    unsigned amount;
    bool twenty;
    do {
        copy = ++seed;
        amount = cok_dice(&copy, 9, 4) + 9u;
        twenty = false;
        for (int i = 0; i < 5; ++i) twenty |= cok_dice(&copy, 1, 20) == 20;
    } while (twenty);
    game.vm.seed = seed;
    game.spell_x = 6;
    game.spell_y = 5;
    cok_field_set8_at(L, w->record, COK_FIELD_CLASS_LEVEL, 5, 9);
    game.vm.character = w->record;
    game.effects.rolls.spell = 0x5c;
    log_text[0] = '\0';
    /* Its caster, the selected one, through a cast's targets: the cone's own. */
    CHECK(cok_cast_on(&game, 0x5c, w, true));
    CHECK(game.vm.status == COK_ECL_OK);
    for (int i = 0; i < 7; ++i)
        CHECK(cok_field_u8(L, t[i]->record, COK_FIELD_HIT_POINTS) == (i < 5 ? 60 - amount : 60));
}

/* 5dc5:4ad7, 0x60: the age 1d4 + 1 less, 18 at the least. */
static void test_youth(void)
{
    const cok_character_layout *L = R->character;
    for (int young = 0; young <= 2; ++young) {
        reset();
        cok_character *c = record('C', 0, 5, 5);
        /* 20 less a 1d4 of 1 and 1 is 18: younger, not made 18. */
        uint32_t seed = 2, copy;
        do {
            copy = ++seed;
        } while (young == 2 && cok_dice(&copy, 1, 4) != 1);
        copy = seed;
        unsigned d = cok_dice(&copy, 1, 4) + 1u;
        uint16_t age = young == 2 ? 20 : young ? 19 : 40;
        cok_field_set16(L, c->record, COK_FIELD_AGE, age);
        game.vm.seed = seed;
        game.vm.mode = 4;
        CHECK(cast_at(c, 0x60, (cok_character *[]){c, NULL}));
        CHECK(cok_field_u16(L, c->record, COK_FIELD_AGE) == (young == 1 ? 18 : age - d));
        CHECK(LOGGED(young == 1 ? "print: is now 18 years old;" : "print: becomes younger;"));
    }
}

/* 5dc5:3cd1, Stone to Flesh: a stoned party member okay with 1 hit point;
 * its body taken back, the entries after it moved down. */
static void test_stone_to_flesh(void)
{
    const cok_character_layout *L = R->character;
    reset();
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 0, 8, 5), *s = record('S', 0, 9, 9);
    cok_field_set8(L, s->record, COK_FIELD_STATUS, 7);
    cok_field_set8(L, s->record, COK_FIELD_ACTIVE, 0);
    cok_combat *k = &game.combat;
    k->body[0] = (cok_combat_body){a, 5, 5, 0x17};
    k->body[1] = (cok_combat_body){s, 9, 9, COK_COMBAT_FLOOR};
    k->body[2] = (cok_combat_body){b, 8, 5, 0x17};
    k->bodies = 3;
    k->cells[9][9] = 0x1f;
    k->combatant[3].size = 0;
    CHECK(cok_combat_occupy(k, R));
    CHECK(cast_at(a, 0x42, (cok_character *[]){s, NULL}));
    CHECK(game.vm.status == COK_ECL_OK && cok_field_u8(L, s->record, COK_FIELD_STATUS) == 0 &&
          cok_field_u8(L, s->record, COK_FIELD_HIT_POINTS) == 1);
    CHECK(k->bodies == 2 && k->body[0].character == a && k->body[1].character == b &&
          k->cells[9][9] == COK_COMBAT_FLOOR && k->combatant[3].size == 1);
}

/* Camp's Fix (dkk 4381:2fd4). */

/* A camper of Death Knights' layout named by a letter, okay, with hp of
 * most hit points, wisdom 12, a spells' block, in the party. */
static cok_character *camper(char name, uint8_t hp, uint8_t most)
{
    const cok_character_layout *L = R->character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL && cok_party_append(&game.party, c));
    uint8_t *r = c->record;
    cok_field_set8_at(L, r, COK_FIELD_NAME, 0, 1);
    cok_field_set8_at(L, r, COK_FIELD_NAME, 1, (uint8_t)name);
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, hp);
    cok_field_set8(L, r, COK_FIELD_MAX_HIT_POINTS, most);
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_RACE, 6);
    for (size_t k = 0; k < 6; ++k) {
        cok_field_set8_at(L, r, COK_FIELD_SCORE_BASE, k, 12);
        cok_field_set8_at(L, r, COK_FIELD_SCORE, k, 12);
    }
    cok_field_set_spells(L, r, true);
    return c;
}

static void camp_reset(void)
{
    reset();
    game.vm.mode = 2;
    game.effects.in_battle = false;
    memset(game.rest, 0, sizeof game.rest);
    game.heal_ticks = game.rest_ticks = 0;
}

static unsigned minutes_passed(void)
{
    const uint16_t *clock = &game.vm.mem4b00[0xc6];
    return clock[COK_CLOCK_MINUTES] + 10u * clock[COK_CLOCK_TENS] + 60u * clock[COK_CLOCK_HOURS] +
           1440u * clock[COK_CLOCK_DAYS];
}

/* The rest time a Fix sets, from an encounter at its first tick, which
 * leaves it less five minutes. */
static unsigned fix_time(void)
{
    game.vm.mem7c00[0x2d2] = 1;
    game.vm.mem7c00[0x2d3] = 100;
    CHECK(cok_camp_fix(&game));
    game.vm.mem7c00[0x2d2] = 0;
    CHECK(LOGGED("print: your repose is suddenly interrupted!;"));
    return game.rest[1] + 10u * game.rest[2] + 60u * game.rest[3] + 5;
}

/* 2537's time: the spells a day of the able members of wisdom above 3,
 * the estimate from 14 (the Move's count on the stack) and the ratio. */
static void test_fix_time(void)
{
    const cok_character_layout *L = R->character;
    /* A cleric with 2 spells of level 1 a day and Cure Light Wounds
     * memorized (2d8 + 1, as Serious), a fighter 2 points down: 14 + 27
     * = 41, / 2 = 20; 240 + 30 = 270 minutes, / 20 = 13, three ticks. */
    camp_reset();
    cok_character *c = camper('C', 8, 8), *f = camper('F', 6, 8);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 1, 2);
    cok_field_set8_at(L, c->record, COK_FIELD_MEMORIZED, 0, 0x03);
    memset(game.vm.mem4b00 + 0xc6, 0, 7 * sizeof *game.vm.mem4b00);
    CHECK(!cok_camp_fix(&game) && game.vm.status == COK_ECL_OK);
    CHECK(minutes_passed() == 15 && cok_field_u8(L, f->record, COK_FIELD_HIT_POINTS) == 8);
    CHECK(cok_field_u8_at(L, c->record, COK_FIELD_MEMORIZED, 0) == 0x03);
    for (size_t i = 0; i < 7; ++i) CHECK(game.rest[i] == 0);
    /* No one lacking: nothing. An area without magic (0x4be5): nothing. */
    CHECK(!cok_camp_fix(&game) && minutes_passed() == 15);
    cok_field_set8(L, f->record, COK_FIELD_HIT_POINTS, 6);
    cok_ecl_set_var(&game.vm, COK_VAR_NO_MAGIC, 1);
    CHECK(!cok_camp_fix(&game) && minutes_passed() == 15 &&
          cok_field_u8(L, f->record, COK_FIELD_HIT_POINTS) == 6);
    cok_ecl_set_var(&game.vm, COK_VAR_NO_MAGIC, 0);
    CHECK(fix_time() == 13);
    /* A druid's level 2 the same; level 6, 8 hours and 90 minutes each,
     * 90 to the estimate: 570 / (104 / 2) = 10. */
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 1, 0);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 1, 2, 1);
    CHECK(fix_time() == 13);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 1, 2, 0);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 6, 1);
    CHECK(fix_time() == 10);
    /* Levels 4 and 5: 6 hours, 60 and 75, 34 without level 5, 78 with:
     * (360 + 60) / (48 / 2) = 17; (360 + 135) / (92 / 2) = 10. */
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 6, 0);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 4, 1);
    CHECK(fix_time() == 17);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 5, 1);
    CHECK(fix_time() == 10);
    /* The fighter with no spells' block is not counted. Wisdom 3: the
     * cleric not counted, and the fighter after repeats nothing: no time.
     * Wisdom 4 counts; the fighter repeats its times: 14 + 78 + 78 = 170,
     * / 2 = 85, 495 / 85 = 5. */
    cok_field_set_spells(L, f->record, false);
    cok_field_set8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_WISDOM, 3);
    memset(game.vm.mem4b00 + 0xc6, 0, 7 * sizeof *game.vm.mem4b00);
    CHECK(!cok_camp_fix(&game) && minutes_passed() == 0);
    cok_field_set8(L, f->record, COK_FIELD_HIT_POINTS, 6);
    cok_field_set8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_WISDOM, 4);
    CHECK(fix_time() == 5);
    /* Not able without a spells' block, nor if not okay. */
    cok_field_set_spells(L, c->record, false);
    memset(game.vm.mem4b00 + 0xc6, 0, 7 * sizeof *game.vm.mem4b00);
    CHECK(!cok_camp_fix(&game) && minutes_passed() == 0 &&
          cok_field_u8(L, f->record, COK_FIELD_HIT_POINTS) == 6);
    /* An estimate 256 times the points lacking: a ratio of 0 as a byte,
     * runtime error 200 (4381:273b). 14 + 2 x 222 + 54 = 512. */
    camp_reset();
    for (int i = 0; i < 3; ++i) {
        cok_character *m = camper('M', 9, 9);
        for (uint8_t level = 1; level <= 6; ++level)
            if (level != 3 && (i < 2 || level == 1))
                cok_field_set8_at2(L, m->record, COK_FIELD_SPELLS_A_DAY, 0, level, 1);
        if (i == 2) cok_field_set8_at2(L, m->record, COK_FIELD_SPELLS_A_DAY, 1, 2, 1);
        if (i < 2) cok_field_set8_at2(L, m->record, COK_FIELD_SPELLS_A_DAY, 1, 2, 1);
    }
    cok_field_set8(L, game.party.members[0]->record, COK_FIELD_HIT_POINTS, 7);
    CHECK(!cok_camp_fix(&game) && game.vm.status == COK_ECL_DIVIDE_BY_ZERO);
}

/* 2a73's cures, twice: Heal for 0x3d (and its blindness), Stone to Flesh,
 * Neutralize Poison, Raise Dead (its constitution and hit points), Remove
 * Curse for 0x24, and Cure Blindness, which cures the selected character
 * and uses no spell; then 2ebc / 2779's second rest for the cures still
 * needed, and its cures. */
static void test_fix_cures(void)
{
    const cok_character_layout *L = R->character;
    camp_reset();
    cok_character *p = camper('P', 9, 9);
    static const uint8_t spells[] = {0x35, 0x42, 0x43, 0x4b, 0x2b, 0x25};
    for (size_t k = 0; k < sizeof spells; ++k)
        cok_field_set8_at(L, p->record, COK_FIELD_MEMORIZED, k, spells[k]);
    cok_character *d = camper('D', 0, 20);
    cok_field_set8(L, d->record, COK_FIELD_STATUS, 6);
    cok_field_set8(L, d->record, COK_FIELD_ACTIVE, 0);
    cok_field_set8(L, d->record, COK_FIELD_RACE, 3);
    cok_field_set8(L, d->record, COK_FIELD_BASE_HIT_POINTS, 10);
    cok_field_set8_at(L, d->record, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION, 15);
    cok_field_set8_at(L, d->record, COK_FIELD_CLASS_LEVEL, 0, 2);
    cok_character *s = camper('S', 5, 9);
    cok_field_set8(L, s->record, COK_FIELD_STATUS, 7);
    cok_field_set8(L, s->record, COK_FIELD_ACTIVE, 0);
    cok_character *x = camper('X', 9, 9);
    CHECK(cok_character_add_effect(x, 0x3d, 0, 0, false) != NULL &&
          cok_character_add_effect(x, 0x21, 0, 0, false) != NULL);
    cok_character *b = camper('B', 9, 9);
    CHECK(cok_character_add_effect(b, 0x21, 0, 0, false) != NULL);
    cok_character *y = camper('Y', 0, 9);
    CHECK(cok_character_add_effect(y, 0x37, 0, 0, false) != NULL &&
          cok_character_add_effect(y, 0x0f, 0, 0, false) != NULL);
    cok_character *z = camper('Z', 9, 9);
    CHECK(cok_character_add_effect(z, 0x24, 0, 0, false) != NULL);
    game.vm.character = p->record;
    memset(game.vm.mem4b00 + 0xc6, 0, 7 * sizeof *game.vm.mem4b00);
    CHECK(!cok_camp_fix(&game) && game.vm.status == COK_ECL_OK);
    static const char first[] = "print: D;print: has been raised from the dead;"
                                "print: S;print: is changed back to flesh;"
                                "print: X;print: is healed;"
                                "print: P;print: regains sight;"
                                "print: Y;print: is detoxified;"
                                "print: Z;print: is relieved of a curse;";
    const char *at = strstr(log_text, first);
    CHECK(at != NULL);
    at += sizeof first - 1;
    /* The second rest: the blind B still (45 minutes, its spell unused),
     * Z still cursed (0x24 stays: Remove Curse cures 0x2b) with no spell
     * left; P's sight cured again. */
    CHECK(strncmp(at, "print: P;print: regains sight;", 30) == 0 && strstr(at, "print: Z") == NULL);
    CHECK(minutes_passed() == 45);
    /* Raised: 1 hit point, constitution 14, its most less 10 / 2, then
     * healed from Heal's 4d8 + 5; the stoned at 1 and healed too. */
    CHECK(cok_field_u8(L, d->record, COK_FIELD_STATUS) == 0 &&
          cok_field_u8(L, d->record, COK_FIELD_ACTIVE) == 1 &&
          cok_field_u8_at(L, d->record, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION) == 14 &&
          cok_field_u8(L, d->record, COK_FIELD_MAX_HIT_POINTS) == 15);
    CHECK(cok_field_u8(L, s->record, COK_FIELD_STATUS) == 0 &&
          cok_field_u8(L, s->record, COK_FIELD_HIT_POINTS) > 1);
    CHECK(cok_character_find_effect(x, 0x3d) == NULL && cok_character_find_effect(x, 0x21) == NULL);
    CHECK(cok_character_find_effect(b, 0x21) != NULL);
    CHECK(cok_character_find_effect(y, 0x37) == NULL && cok_character_find_effect(y, 0x0f) == NULL &&
          cok_field_u8(L, y->record, COK_FIELD_HIT_POINTS) >= 1);
    CHECK(cok_character_find_effect(z, 0x24) != NULL);
    for (size_t k = 0; k < sizeof spells; ++k)
        CHECK(cok_field_u8_at(L, p->record, COK_FIELD_MEMORIZED, k) == spells[k]);

    /* A dead member poisoned meets Neutralize Poison first, which makes
     * it okay: "is detoxified", not raised, its Raise Dead unused. */
    camp_reset();
    p = camper('P', 9, 9);
    cok_field_set8_at(L, p->record, COK_FIELD_MEMORIZED, 0, 0x43);
    cok_field_set8_at(L, p->record, COK_FIELD_MEMORIZED, 1, 0x4b);
    d = camper('D', 0, 20);
    cok_field_set8(L, d->record, COK_FIELD_STATUS, 6);
    cok_field_set8(L, d->record, COK_FIELD_RACE, 3);
    CHECK(cok_character_add_effect(d, 0x37, 0, 0, false) != NULL);
    CHECK(!cok_camp_fix(&game) && LOGGED("print: D;print: is detoxified;") &&
          !LOGGED("raised") && cok_field_u8(L, d->record, COK_FIELD_STATUS) == 0 &&
          cok_field_u8(L, d->record, COK_FIELD_HIT_POINTS) == 1);

    /* Raise Dead's loss: constitution 17 left with no fighter, paladin or
     * knight level keeps the most; classes 2-4 count (con - 14) a level,
     * others two above 15. Not for race 2 (nor 0, 1). */
    for (int k = 0; k < 4; ++k) {
        camp_reset();
        cok_character *q = camper('Q', 9, 9);
        cok_field_set8_at(L, q->record, COK_FIELD_MEMORIZED, 0, 0x4b);
        cok_character *t = camper('T', 0, 40);
        cok_field_set8(L, t->record, COK_FIELD_STATUS, 6);
        cok_field_set8(L, t->record, COK_FIELD_BASE_HIT_POINTS, 10);
        cok_field_set8_at(L, t->record, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION, 18);
        cok_field_set8_at(L, t->record, COK_FIELD_CLASS_LEVEL, k == 1 ? 3 : 5, k == 1 ? 2 : 3);
        if (k == 2) cok_field_set8_at(L, t->record, COK_FIELD_CLASS_LEVEL, 7, 1);
        if (k == 3) cok_field_set8(L, t->record, COK_FIELD_RACE, 2);
        game.vm.character = q->record;
        cok_field_set8(L, q->record, COK_FIELD_HIT_POINTS, 8); /* lacking, but no cure */
        CHECK(!cok_camp_fix(&game) && game.vm.status == COK_ECL_OK);
        uint8_t most = cok_field_u8(L, t->record, COK_FIELD_MAX_HIT_POINTS);
        if (k == 3) {
            CHECK(cok_field_u8(L, t->record, COK_FIELD_STATUS) == 6 && most == 40);
            continue;
        }
        CHECK(cok_field_u8(L, t->record, COK_FIELD_STATUS) == 0 &&
              cok_field_u8_at(L, t->record, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION) == 17);
        /* 30 / (2 x 3) = 5 for the paladin (class 3), taken; 30 / 6 = 5
         * for the mage (5) with a knight level, taken; without, kept. */
        CHECK(most == (k == 0 ? 40 : 35));
    }
}

/* The pool: the memorized cures' dice in their order, then the spells a
 * day's (23f3: 1d8 for cleric level 1 and druid level 2 each, 2d8 + 1,
 * 3d8 + 3, 4d8 + 3), healing one lacking more. */
static void test_fix_pool(void)
{
    const cok_character_layout *L = R->character;
    camp_reset();
    cok_character *c = camper('C', 9, 9), *f = camper('F', 1, 255);
    static const uint8_t memorized[] = {0x03, 0x47, 0x35, 0x3a};
    for (size_t k = 0; k < sizeof memorized; ++k)
        cok_field_set8_at(L, c->record, COK_FIELD_MEMORIZED, k, memorized[k]);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 1, 2, 1);
    for (uint8_t level = 4; level <= 6; ++level)
        cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, level, 1);
    uint32_t seed = game.vm.seed = 77;
    unsigned pool = cok_dice(&seed, 2, 8) + 1u;
    pool += cok_dice(&seed, 3, 8) + 3u;
    pool += cok_dice(&seed, 4, 8) + 5u;
    pool += cok_dice(&seed, 2, 8) + 1u;
    pool += cok_dice(&seed, 1, 8) + cok_dice(&seed, 1, 8) + 0u;
    pool += cok_dice(&seed, 2, 8) + 1u;
    pool += cok_dice(&seed, 3, 8) + 3u;
    pool += cok_dice(&seed, 4, 8) + 3u;
    CHECK(!cok_camp_fix(&game) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(L, f->record, COK_FIELD_HIT_POINTS) == 1 + pool && game.vm.seed == seed);
}

/* 2779's second rest: as many as need a cure and its spells (here a day,
 * of one of wisdom 3, whom the first rest does not count) can cure, 60
 * minutes for Neutralize Poison, 75 Raise Dead, 90 Heal and Stone to
 * Flesh, 45 Remove Curse and Cure Blindness; the rest time from before
 * put back for it (days a rest's time leaves). */
static void test_fix_second(void)
{
    const cok_character_layout *L = R->character;
    static const struct {
        uint8_t row, level, minutes;
        int needy;
    } cases[] = {{0, 4, 60, 0}, {0, 5, 75, 1}, {0, 6, 90, 2}, {3, 6, 90, 3},
                 {3, 4, 45, 4}, {0, 3, 45, 4}, {0, 3, 45, 5}, {0, 3, 0, 6}};
    for (size_t k = 0; k < sizeof cases / sizeof *cases; ++k) {
        camp_reset();
        cok_character *q = camper('Q', 9, 9);
        cok_field_set8_at(L, q->record, COK_FIELD_SCORE, COK_SCORE_WISDOM, 3);
        cok_field_set8_at2(L, q->record, COK_FIELD_SPELLS_A_DAY, cases[k].row, cases[k].level, 1);
        cok_character *n = camper('N', 5, 9);
        switch (cases[k].needy) {
        case 0: CHECK(cok_character_add_effect(n, 0x37, 0, 0, false) != NULL); break;
        case 1:
            cok_field_set8(L, n->record, COK_FIELD_STATUS, 6);
            cok_field_set8(L, n->record, COK_FIELD_RACE, 2); /* counted, not raised */
            break;
        case 2: CHECK(cok_character_add_effect(n, 0x3d, 0, 0, false) != NULL); break;
        case 3: cok_field_set8(L, n->record, COK_FIELD_STATUS, 7); break;
        case 4: CHECK(cok_character_add_effect(n, 0x24, 0, 0, false) != NULL); break;
        case 5: CHECK(cok_character_add_effect(n, 0x21, 0, 0, false) != NULL); break;
        default: /* blind with 0x3d too: Heal's, not Cure Blindness's */
            CHECK(cok_character_add_effect(n, 0x21, 0, 0, false) != NULL &&
                  cok_character_add_effect(n, 0x3d, 0, 0, false) != NULL);
            break;
        }
        game.vm.character = q->record;
        if (cases[k].minutes == 0) {
            CHECK(!cok_camp_fix(&game) && !LOGGED("interrupted"));
            continue;
        }
        CHECK(fix_time() == cases[k].minutes);
    }
    /* A day left in the rest time before: both rests take it. */
    camp_reset();
    camper('Q', 9, 9);
    camper('N', 5, 9);
    game.rest[4] = 1;
    memset(game.vm.mem4b00 + 0xc6, 0, 7 * sizeof *game.vm.mem4b00);
    CHECK(!cok_camp_fix(&game) && minutes_passed() == 2 * 1440 && game.rest[4] == 1);
}

/* Fix's rests learn nothing (DS:c46f): a spell marked stays marked, the
 * hours of study count down; the day's healing in Death Knights' words. */
static void test_fix_no_learning(void)
{
    const cok_character_layout *L = R->character;
    camp_reset();
    cok_character *c = camper('C', 200, 250);
    cok_field_set8_at2(L, c->record, COK_FIELD_SPELLS_A_DAY, 0, 6, 1);
    cok_field_set8_at(L, c->record, COK_FIELD_MEMORIZED, 0, 0x81);
    cok_field_set8(L, c->record, COK_FIELD_STUDY_HOURS, 2);
    game.heal_ticks = 0x11f;
    CHECK(!cok_camp_fix(&game) && game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8_at(L, c->record, COK_FIELD_MEMORIZED, 0) == 0x81 &&
          cok_field_u8(L, c->record, COK_FIELD_STUDY_HOURS) == 0);
    CHECK(LOGGED("print: The whole party is healed;") && !LOGGED("has memorized"));
}

int main(void)
{
    struct stat st;
    if (stat("DKKAssets/START.EXE", &st) != 0) {
        puts("band: no DKKAssets/START.EXE; Death Knights' band C not checked");
        return 0;
    }
    cok_keyboard keys = {no_key, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line};
    CHECK(cok_adventure_open(&game, R, "DKKAssets", &keys, &hooks));
    load_screen();
    test_slay_living();
    test_destroy();
    test_death_spell();
    test_harm();
    test_heal();
    test_raise();
    test_restoration();
    test_restoration_wraps();
    test_feeblemind();
    test_dance();
    test_mass_charm();
    test_power_words();
    test_charm_mammal();
    test_entangle();
    test_dispel_evil();
    test_silence();
    test_barkskin();
    test_stun_and_blank();
    test_youth();
    test_cone_of_cold();
    test_stone_to_flesh();
    test_fix_time();
    test_fix_cures();
    test_fix_pool();
    test_fix_second();
    test_fix_no_learning();
    cok_adventure_close(&game);
    puts("band tests passed");
    return 0;
}
