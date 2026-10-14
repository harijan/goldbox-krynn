#include "cast.h"

#include "arena.h"
#include "cloud.h"
#include "command.h"
#include "effect.h"

#include "adventure.h"
#include "ai.h"
#include "attack.h"
#include "items.h"
#include "magic.h"
#include "monster.h"
#include "round.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                     \
    do {                                                             \
        if (!(x)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            dump();                                                  \
            exit(1);                                                 \
        }                                                            \
    } while (0)

static void dump(void);

typedef struct {
    const char *keys;
    size_t at, length;
    char log[1 << 17];
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    return (unsigned char)s->keys[s->at++];
}

static script s;
static cok_adventure game;

static uint32_t fold(uint32_t h, const cok_picture *p)
{
    for (size_t k = 0; k < p->frame_size; ++k) h = (h ^ p->pixels[k]) * 16777619u;
    return h;
}

/* The screens shown at each text printed, folded when fold_prints. */
static bool fold_prints;
static uint32_t print_screens;

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    if (fold_prints && strcmp(kind, "print") == 0) print_screens = fold(print_screens, &g->screen);
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "%s: %s;", kind, text);
}

#define LOGGED(text) (strstr(s.log, text) != NULL)

/* The waits of 30 ms, a spell's flight's steps, and of 100, a text's
 * pause at speed 1. */
static unsigned flight_steps, pauses;
/* The screens shown at those steps, folded (FNV-1a). */
static uint32_t flight_screens;

static void on_delay(cok_adventure *g, unsigned ms, void *context)
{
    if (ms == 30) {
        ++flight_steps;
        flight_screens = fold(flight_screens, &g->screen);
    }
    if (ms == 100) ++pauses;
    if (ms != 200) return; /* the 200 ms of Auto and Alt-Q only */
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "wait: %u;", ms);
}

static void dump(void)
{
    if (getenv("SPELL_LOG") != NULL) fprintf(stderr, "%s\n", s.log);
}

/* Keys as eclplay takes them: \0 an extended key's 0 (here "\x00"), so
 * the scripts give their length. */
static void keys(const char *k, size_t n)
{
    s.keys = k;
    s.at = 0;
    s.length = n;
}
#define KEYS(k) keys(k, sizeof k - 1)

static void reset(void)
{
    cok_cloud_free(&game.combat);
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    free(game.lost_weapons);
    game.lost_weapons = NULL;
    game.lost_weapon_count = 0;
    game.vm.character = game.vm.saved_character = NULL;
    game.vm.restore_character = false;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    memset(&game.effects.rolls, 0, sizeof game.effects.rolls);
    memset(game.spell_targets, 0, sizeof game.spell_targets); /* a new game's */
    game.spell_target_count = 0;
    game.vm.mode = 5;
    game.vm.file = 1;
    game.vm.seed = 1;
    game.vm.direction = 0;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.input_ended = false;
    game.combat_stub = COK_COMBAT_PLAYER;
    game.effects.in_battle = true;
    game.combat_targets = true;
    game.speed = 0;
    game.helm = false;
    game.undead = 0;
    game.selected = 7;
    cok_combat *c = &game.combat;
    memset(c->cells, COK_COMBAT_FLOOR, sizeof c->cells);
    memset(c->combatant, 0, sizeof c->combatant);
    memset(c->entry_held, 0, sizeof c->entry_held);
    memset(c->turn, 0, sizeof c->turn);
    memset(c->listed, 0, sizeof c->listed);
    memset(c->enemies, 0, sizeof c->enemies);
    memset(c->exploding, 0, sizeof c->exploding);
    memset(c->swings, 0, sizeof c->swings);
    memset(c->body, 0, sizeof c->body);
    c->listed_count = 0;
    c->count = 1;
    c->bodies = 0;
    c->view_x = c->view_y = 0;
    c->see_all = 0;
    c->cursor = 0;
    c->cursor_size = 1;
    c->round_limit = 15;
    c->sides[0] = c->sides[1] = 0;
    c->exploding_count = 0;
    c->exploding_now = false;
    c->yelled = NULL;
    c->show_actions = false;
    c->panel = false;
    c->active = true;
    c->magic_on = false;
    c->turn_index = 1;
    c->enemy_health = 0;
    KEYS("");
    s.log[0] = '\0';
}

/* A record named by a letter, a fighter of level 1, one cell, abilities
 * 12, 10 hit points, on side, armour class 10, one attack a round of 1d2,
 * the player's (+0x18b 0) on side 0, the computer's on 1, with a zeroed
 * combat record, initiative 5 and movement 12, a combatant at x, y. */
static cok_character *record(char name, uint8_t side, int8_t x, int8_t y)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = 1;
    r[1] = (uint8_t)name;
    for (int k = 0; k < 6; ++k) r[0x10 + 2 * k] = r[0x11 + 2 * k] = 12;
    r[0x13] = 12;
    r[0x59] = 40;
    r[0x62] = r[0x197] = r[0x11b] = 10;
    r[0x5a] = 6;
    r[0xcf] = 1;
    r[0xd5] = 12;
    r[0xd6] = 1;
    r[0xfb] = 1;
    r[0x11a] = 8;
    r[0x10b] = 2;
    r[0x10d] = 1;
    r[0x10f] = 2;
    r[0x113] = 50;
    r[0x189] = 1;
    r[0x18a] = side;
    r[0x18b] = side;
    if (side != 0) r[0xe7] = 0x80;
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL && cok_party_append(&game.party, c));
    c->combat->not_party = side;
    c->combat->initiative = 5;
    c->combat->movement = 12;
    c->combat->may_use = c->combat->may_cast = 1;
    char why[300];
    CHECK(cok_character_stats(c, &game.item_types, why, sizeof why));
    r[0x18f] = 1;
    r[0x190] = 0;
    c->combat->attack_slot = 2;
    cok_combat *k = &game.combat;
    cok_combatant *e = &k->combatant[k->count];
    e->x = x;
    e->y = y;
    e->size = 1;
    e->character = c;
    ++k->count;
    k->combatant[k->count].size = 0;
    k->combatant[k->count].character = NULL;
    CHECK(cok_combat_occupy(k));
    k->sides[side] = (uint8_t)(k->sides[side] + 1);
    return c;
}

/* Give c an item of type, readied if ready. */
static size_t give(cok_character *c, uint8_t type, bool ready)
{
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = type;
    item[0x31] = type;
    item[0x34] = ready;
    CHECK(cok_character_insert_item(c, c->item_count, item));
    char why[300];
    CHECK(cok_character_stats(c, &game.item_types, why, sizeof why));
    return c->item_count;
}

/* c's turn: selected, its turn the current one. */
static void turn_of(cok_character *c, uint8_t index)
{
    game.vm.character = c->record;
    game.combat.turn[index] = c;
    game.combat.turn_index = index;
}

static uint8_t spell_byte(uint8_t spell, unsigned column)
{
    uint8_t b = 0;
    CHECK(cok_ds_byte((uint16_t)(0x31b3u + 16u * spell + column), &b));
    return b;
}


/* A seed whose first values, of the ranges given, are in the bounds given:
 * n triples of range, lo and hi. */
static uint32_t seed_seq(unsigned n, const unsigned *triples)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        bool ok = true;
        for (unsigned i = 0; i < n && ok; ++i) {
            unsigned d = cok_tp_random(&seed, (uint16_t)triples[3 * i]) + 1u;
            ok = d >= triples[3 * i + 1] && d <= triples[3 * i + 2];
        }
        if (ok) return start;
    }
}
#define SEED(n, ...) (game.vm.seed = seed_seq(n, (const unsigned[]){__VA_ARGS__}))

/* The seed after n rolls of the ranges given from start. */
static uint32_t after(uint32_t start, unsigned n, const unsigned *ranges)
{
    for (unsigned i = 0; i < n; ++i) cok_tp_random(&start, (uint16_t)ranges[i]);
    return start;
}
#define AFTER(start, n, ...) after(start, n, (const unsigned[]){__VA_ARGS__})

/* Cast spell for c from memory, the computer's way if ai. */
static bool cast(cok_character *c, uint8_t spell, bool ai, bool *done)
{
    game.vm.character = c->record;
    c->record[0x1e] = spell;
    *done = false;
    cok_cast_spell(&game, spell, ai, true, 0, done);
    return game.vm.status == COK_ECL_OK;
}

#define T(i) game.spell_targets[i]
#define N game.spell_target_count

/* Whether the screen shows text at cell x, y in fg on 0, as 1521:0353
 * draws it. */
static bool shows(int x, int y, const char *text, uint8_t fg)
{
    static cok_picture expect;
    if (expect.pixels == NULL) CHECK(cok_picture_create(&expect, 40, 200, 1, 0) == COK_PICTURE_OK);
    cok_text_string(&expect, &game.font, text, x, y, fg, 0);
    size_t row = (size_t)game.screen.units * 4;
    for (size_t r = (size_t)y * 8; r < (size_t)y * 8 + 8; ++r)
        for (size_t k = (size_t)x * 4; k < (size_t)(x + (int)strlen(text)) * 4; ++k)
            if (game.screen.pixels[r * row + k] != expect.pixels[r * row + k]) return false;
    return true;
}

/* Whether row y of the screen is all colour 0. */
static bool blank_row(int y)
{
    size_t row = (size_t)game.screen.units * 4;
    for (size_t k = (size_t)y * 8 * row; k < (size_t)(y + 1) * 8 * row; ++k)
        if (game.screen.pixels[k] != 0) return false;
    return true;
}

static size_t count(const char *text)
{
    size_t n = 0;
    for (const char *p = s.log; (p = strstr(p, text)) != NULL; p += strlen(text)) ++n;
    return n;
}

static void load_screen(void)
{
    reset();
    record('A', 0, 0, 0);
    record('M', 1, 0, 0);
    game.vm.mode = 4;
    game.vm.mem7c00[0x33e] = 2;
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(cok_combat_setup(&game));
    CHECK(cok_combat_end(&game));
    game.vm.mode = 5;
}

/* The caster alone (byte 6 & 15 = 0): Detect Magic. In combat it "Casts
 * a Spell", "Spell:" and the name on row 23, flies its missile to its own
 * cell (sound 2), is affected; the spell forgotten, row 23 cleared. */
static void test_self(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    record('M', 1, 14, 10);
    bool done;
    CHECK(cast(a, 5, false, &done) && done);
    CHECK(LOGGED("cast: A: Detect Magic;print: A;print: Casts a Spell;print: Spell:Detect Magic;"));
    CHECK(LOGGED("sound: 2;sound: 4;print: A;print: is affected;"));
    CHECK(N == 1 && T(1) == a && a->record[0x1e] == 0 && cok_character_find_effect(a, 5) != NULL);
    CHECK(blank_row(0x17));
    /* No announcement for an item. */
    s.log[0] = '\0';
    game.effects.rolls.item = 1;
    CHECK(cast(a, 5, false, &done) && done && !LOGGED("Casts a Spell") && a->record[0x1e] == 5);
    game.effects.rolls.item = 0;
}


/* The caster, shown, is hidden after the flight and drawn back ready,
 * none of its casting pose left under the ready one, as the screens shown
 * while Detect Magic's texts print have it: pinned by their hash. */
static void test_redrawn(void)
{
    reset();
    cok_character *a = record('A', 0, 3, 3);
    record('M', 1, 5, 3);
    a->record[0x137] = 8;
    CHECK(cok_arena_load_icon(&game, "CPIC", 1, 8));
    print_screens = 2166136261u;
    fold_prints = true;
    bool done;
    CHECK(cast(a, 5, false, &done) && done && LOGGED("print: A;print: is affected;"));
    fold_prints = false;
    CHECK(print_screens == 0xb85ea1b6u);
    cok_arena_free_icon(&game, 8);
}

/* An area (byte 6 & 15 from 8 to 14): Bless aimed at A's own cell takes
 * everyone within 2 (6b30:08d8), either side, A first; Bless keeps those
 * on A's side, in combat only those with no enemy beside them. */
static void test_area(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 11, 10),
                  *m = record('M', 1, 12, 10);
    record('F', 1, 20, 10);
    KEYS("T");
    bool done;
    CHECK(cast(a, 1, false, &done) && done);
    /* B and M taken off the list. */
    CHECK(N == 3 && T(1) == a && T(2) == NULL && T(3) == NULL);
    CHECK(LOGGED("print: A;print: is Blessed;") && !LOGGED("print: B;print: is Blessed;"));
    CHECK(cok_character_find_effect(a, 1) != NULL && cok_character_find_effect(b, 1) == NULL);
    CHECK(game.effects.rolls.images == 0 && game.spell_x == 10 && game.spell_y == 10);
    /* Curse keeps those against the caster: M. */
    reset();
    a = record('A', 0, 10, 10);
    b = record('B', 0, 11, 10);
    m = record('M', 1, 12, 10);
    KEYS("T");
    CHECK(cast(a, 2, false, &done) && done && N == 3 && T(3) == m && T(1) == NULL);
    CHECK(LOGGED("print: M;print: is Cursed;") && !LOGGED("print: A;print: is Cursed;"));
    /* Nothing picked: Exit, then "Abort Spell? " No aims again; Yes, "Spell
     * Aborted", the spell forgotten. */
    reset();
    a = record('A', 0, 10, 10);
    KEYS("\x1bN\x1bY");
    CHECK(cast(a, 1, false, &done) && !done && s.at == s.length);
    CHECK(LOGGED("print: Spell Aborted;") && a->record[0x1e] == 0 && !LOGGED("Blessed"));
    /* The computer's Bless: on itself, at once. */
    reset();
    m = record('M', 1, 10, 10);
    record('N', 1, 11, 10);
    CHECK(cast(m, 1, true, &done) && done && N == 2 && T(1) == m);
    CHECK(LOGGED("print: M;print: is Blessed;") && LOGGED("print: N;print: is Blessed;"));
}

/* Separate picks (otherwise): Hold Person, three picks. The caster is
 * refused, a second pick of one taken already too, Exit ends the picks;
 * the one held flies no missile (it is the last), throws type 4 at -2. */
static void test_picks(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 12, 10);
    m->record[0xd4] = 30;
    KEYS("TNTNT\x1b");
    SEED(1, 20, 2, 19);
    bool done;
    CHECK(cast(a, 0x17, false, &done) && done && s.at == s.length);
    CHECK(LOGGED("print: Cannot target yourself;") && LOGGED("print: Already been targeted;"));
    CHECK(N == 1 && T(1) == m && game.spell_x == 12 && game.spell_y == 10);
    CHECK(LOGGED("print: M;print: is held;") && cok_character_find_effect(m, 0x34) != NULL);
    CHECK(count("sound: 2;") == 1); /* the cast's own flight */
    /* An elf always saves against it: "is Unaffected" (a save of kind 1). */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    m->record[0xd4] = 30;
    m->record[0x5a] = 7;
    KEYS("NT\x1b");
    SEED(1, 20, 2, 19);
    CHECK(cast(a, 0x17, false, &done) && done);
    CHECK(LOGGED("print: M;print: is Unaffected;") && cok_character_find_effect(m, 0x34) == NULL);
    /* Two held: a missile to the first picked (sound 2, then 1cbd's), the
     * last picked last. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    cok_character *n = record('N', 1, 13, 10);
    m->record[0xd4] = n->record[0xd4] = 30;
    KEYS("NTNNT\x1b");
    SEED(2, 20, 2, 19, 20, 2, 19);
    CHECK(cast(a, 0x17, false, &done) && done && N == 2 && T(1) == m && T(2) == n);
    CHECK(count("sound: 2;") == 2);
    CHECK(LOGGED("print: N;print: is held;sound: 2;sound: 4;print: M;print: is held;"));
    /* No pick at all: none, and the spell's cell is entry 0's. */
    reset();
    a = record('A', 0, 10, 10);
    KEYS("\x1bY");
    CHECK(cast(a, 0x17, false, &done) && !done && N == 0);
}

/* Silence (byte 6 & 15 = 15): its target alone; with none (Manual's Exit,
 * then Target) those at the cell: radius 0. */
static void test_silence(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 12, 10);
    m->record[0xd4] = 30;
    KEYS("NT");
    SEED(1, 20, 2, 19);
    bool done;
    CHECK(cast(a, 0x19, false, &done) && done && N == 1 && T(1) == m);
    CHECK(LOGGED("print: M;print: is silenced;"));
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    KEYS("NM\x1bT");
    CHECK(cast(a, 0x19, false, &done) && done && N == 1 && T(1) == m);
    /* The aim left: "Abort Spell? ", and Yes aborts it. */
    reset();
    a = record('A', 0, 10, 10);
    record('M', 1, 12, 10);
    KEYS("\x1bY");
    CHECK(cast(a, 0x19, false, &done) && !done && s.at == s.length);
    CHECK(LOGGED("print: Spell Aborted;") && a->record[0x1e] == 0);
}

/* Charm Monsters (byte 6 & 15 = 5): picks weighed by hit dice within 2d4;
 * the computer's second pick of one loses one of its allowance; the last
 * picked counts as saved against a save of kind 1. */
static void test_weighed(void)
{
    reset();
    cok_character *m = record('M', 1, 12, 10), *a = record('A', 0, 10, 10);
    SEED(2, 4, 1, 1, 4, 2, 2); /* an allowance of 3 */
    uint32_t start = game.vm.seed;
    bool done;
    CHECK(cast(m, 0x51, true, &done) && done && N == 1 && T(1) == a);
    CHECK(LOGGED("print: A;print: is Unaffected;") && cok_character_find_effect(a, 0x0b) == NULL);
    /* 2d4, then four picks of d1: one taken, three again. */
    CHECK(game.vm.seed == AFTER(start, 6, 4, 4, 1, 1, 1, 1));
    /* The player's: two picks within the allowance, the first charmed. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    cok_character *n = record('N', 1, 13, 10);
    m->record[0xd4] = 30;
    KEYS("NTNNT\x1b");
    SEED(3, 4, 4, 4, 4, 4, 4, 20, 2, 19); /* 8, then M's save */
    CHECK(cast(a, 0x51, false, &done) && done && N == 2 && T(1) == m && T(2) == n);
    CHECK(LOGGED("print: M;print: is charmed;") && LOGGED("print: N;print: is Unaffected;"));
    CHECK(s.at == s.length && m->record[0x18a] == 0); /* 0x0b took hold: the side of bit 7 */
}

/* The computer's targets: hostile spells at an enemy 432f:3f9f picks,
 * not one helpless that the spell would disable; others on itself. */
static void test_computer(void)
{
    reset();
    cok_character *m = record('M', 1, 12, 10), *a = record('A', 0, 10, 10);
    CHECK(cok_character_add_effect(a, 0x34, 0, 0, false) != NULL);
    bool done;
    CHECK(cast(m, 0x17, true, &done) && !done && N == 0);
    CHECK(LOGGED("print: Spell Aborted;") && m->record[0x1e] == 0);
    /* Not for Magic Missile. */
    s.log[0] = '\0';
    CHECK(cast(m, 0x0f, true, &done) && done && N == 1 && T(1) == a && LOGGED("takes"));
    CHECK(m->combat->target == a->record);
}

/* The flight: the caster turned toward the cell, Fireball's sound 0x0b,
 * Lightning Bolt's 8. A miscast still "Casts a Spell" in combat. */
static void test_flight(void)
{
    reset();
    cok_character *m = record('M', 1, 15, 10), *a = record('A', 0, 10, 10);
    bool done;
    a->record[0x197] = a->record[0x62] = 100;
    CHECK(cast(m, 0x2f, true, &done) && done && LOGGED("sound: 11;") && m->combat->facing == 6);
    s.log[0] = '\0';
    CHECK(cast(m, 0x33, true, &done) && done && LOGGED("sound: 8;"));
    s.log[0] = '\0';
    CHECK(cok_character_add_effect(m, 0x4a, 0, 0, false) != NULL);
    SEED(1, 2, 1, 1);
    CHECK(cast(m, 0x0f, true, &done) && !done && LOGGED("print: Casts a Spell;"));
    CHECK(!LOGGED("takes") && m->record[0x1e] == 0x0f);
}

/* "Spell:" and the name stay on row 23 while the spell is aimed (the keys
 * run out there); "Casts a Spell" paused before (at speed 1, 100 ms). */
static void test_row23(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    record('M', 1, 12, 10);
    bool done;
    KEYS("");
    game.vm.character = a->record;
    a->record[0x1e] = 0x0f;
    game.speed = 1;
    pauses = 0;
    cok_cast_spell(&game, 0x0f, false, true, 0, &done);
    CHECK(game.vm.abort && shows(0, 0x17, "Spell:Magic Missile", 10) && pauses == 1);
    game.vm.abort = false;
    game.input_ended = false;
}

/* 5b04:0ecb: byte 2 + byte 3 times the caster level, or 6 for an item;
 * 0 with byte 6 set, and touch (0xff), become 1. */
static void test_range(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    game.vm.character = a->record;
    a->record[0xfe] = 3; /* a mage of level 3 */
    uint8_t range;
    CHECK(cok_cast_range(&game, 0x0f, &range) && range == spell_byte(0x0f, 2) + 4 * 3);
    CHECK(cok_cast_range(&game, 0x14, &range) && range == 1);
    CHECK(cok_cast_range(&game, 0x21, &range) && range == 1 + 3);
    game.effects.rolls.item = 1;
    CHECK(cok_cast_range(&game, 0x21, &range) && range == 1 + 6);
    game.effects.rolls.item = 0;
    CHECK(cok_cast_range(&game, 0x53, &range) && range == 9); /* 0 + 3 x level */
    CHECK(cok_cast_range(&game, 0x2a, &range) && range == 0); /* byte 6 is 0 */
}

/* A spell begun at an earlier turn (3995:0573): cleared first, then cast,
 * the turn over. The computer's (3afb:004b) is not cleared while it casts:
 * one hurt by its own Fireball "lost a spell" and forgets another. */
static void test_delayed(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    turn_of(a, 1);
    a->combat->spell = 5;
    a->record[0x1e] = 5;
    CHECK(cok_combat_commands(&game, a, 0) && a->combat->spell == 0 && a->combat->initiative == 0);
    CHECK(LOGGED("print: A;print: is affected;") && !LOGGED("lost a spell"));
    /* Cleared first: a player's Fireball on its own cell (the aim starts
     * there) does not make it lose a spell; its second copy is kept. */
    reset();
    a = record('A', 0, 10, 10);
    record('M', 1, 11, 10);
    turn_of(a, 1);
    a->record[0xfe] = 5;
    a->record[0x197] = a->record[0x62] = 200;
    a->record[0x1e] = a->record[0x1f] = 0x2f;
    a->combat->spell = 0x2f;
    game.vm.mem4b00[0xe6] = 1;
    KEYS("T");
    CHECK(cok_combat_commands(&game, a, 0) && a->combat->spell == 0 && a->record[0x197] < 200);
    CHECK(!LOGGED("lost a spell") && a->record[0x1e] == 0 && a->record[0x1f] == 0x2f);
    reset();
    cok_character *m = record('M', 1, 10, 10);
    record('A', 0, 11, 10);
    turn_of(m, 1);
    m->record[0xfe] = 5;
    m->record[0x197] = m->record[0x62] = 200;
    m->record[0x1e] = m->record[0x1f] = 0x2f;
    m->combat->spell = 0x2f;
    game.vm.mem4b00[0xe6] = 1; /* a 3D area: radius 3 */
    CHECK(cok_combat_computer(&game, m) && m->combat->spell == 0);
    CHECK(LOGGED("print: M;print: lost a spell;") && m->record[0x1e] == 0 && m->record[0x1f] == 0);
}

/* The computer's item (3afb:04b2, 546c:24d7): Magic Missile from a wand,
 * a charge used, the turn over. */
static void test_item(void)
{
    reset();
    cok_character *m = record('M', 1, 12, 10), *a = record('A', 0, 10, 10);
    game.vm.character = m->record;
    m->record[0xfe] = 1;
    size_t wand = give(m, 0x34, true);
    m->items[wand - 1][0x3d] = 0x0f;
    m->items[wand - 1][0x3c] = 4;
    a->record[0x197] = a->record[0x62] = 100;
    SEED(1, 7, 3, 3);
    bool used;
    CHECK(cok_ai_use_item(&game, m, &used) && used && LOGGED("print: M;print: uses an item;"));
    CHECK(LOGGED("print: A;print: takes") && m->items[wand - 1][0x3c] == 3);
    CHECK(m->combat->initiative == 0 && !LOGGED("Casts a Spell"));
    /* An item's cast aborted keeps the user's own Magic Missile. */
    reset();
    a = record('A', 0, 10, 10);
    game.effects.rolls.item = 1;
    KEYS("\x1bY");
    bool done;
    CHECK(cast(a, 0x0f, false, &done) && !done && LOGGED("print: Spell Aborted;"));
    CHECK(a->record[0x1e] == 0x0f);
    game.effects.rolls.item = 0;
}

/* Damage: Magic Missile, (level + 1) / 2 missiles of 1d4 + 1, magic. */
static void test_damage(void)
{
    reset();
    cok_character *m = record('M', 1, 15, 10), *a = record('A', 0, 10, 10);
    m->record[0xfe] = 3;
    a->record[0x197] = a->record[0x62] = 100;
    SEED(3, 1, 1, 1, 4, 4, 4, 4, 4, 4); /* the pick, 2d4 */
    bool done;
    CHECK(cast(m, 0x0f, true, &done) && done);
    CHECK(LOGGED("print: A;print: takes 10 points of damage from Magic;"));
    CHECK(a->record[0x197] == 90);
    /* Ice Storm: 3d10 of cold, at a cell. */
    reset();
    m = record('M', 1, 15, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    SEED(4, 1, 1, 1, 10, 1, 1, 10, 2, 2, 10, 3, 3);
    CHECK(cast(m, 0x57, true, &done) && done);
    CHECK(LOGGED("print: A;print: takes 6 points of damage from Cold;") && a->record[0x197] == 94);
    /* 0x41: 2d4 + 2. */
    reset();
    m = record('M', 1, 15, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    SEED(3, 1, 1, 1, 4, 1, 1, 4, 1, 1);
    CHECK(cast(m, 0x41, true, &done) && done && a->record[0x197] == 96);
    /* Fireball: level d6, fire and magic, everyone within 3 of the cell in
     * a 3D area (var 0x4be6 1), within 2 otherwise; the screen centred. */
    reset();
    m = record('M', 1, 20, 10);
    a = record('A', 0, 10, 10);
    cok_character *b = record('B', 0, 13, 10);
    m->record[0xfe] = 1;
    a->record[0x197] = a->record[0x62] = b->record[0x197] = b->record[0x62] = 100;
    a->record[0xd3] = b->record[0xd3] = 30; /* fail their saves */
    game.vm.mem4b00[0xe6] = 1;
    CHECK(cast(m, 0x2f, true, &done) && done);
    if (game.spell_x == 10) {
        CHECK(N == 2 && T(1) == a && T(2) == b && b->record[0x197] < 100);
    } else {
        CHECK(game.spell_x == 13 && N == 2 && T(1) == b && a->record[0x197] < 100);
    }
    reset();
    m = record('M', 1, 20, 10);
    a = record('A', 0, 10, 10);
    b = record('B', 0, 13, 10);
    m->record[0xfe] = 1;
    a->record[0x197] = a->record[0x62] = b->record[0x197] = b->record[0x62] = 100;
    CHECK(cast(m, 0x2f, true, &done) && done && N == 1);
    CHECK(LOGGED("from Fire"));
}

/* Touch: Shocking Grasp, 1d8 + level, rolled before the attack roll
 * (60f4:1062), a miss leaving no damage. */
static void test_touch(void)
{
    reset();
    cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
    m->record[0xfe] = 2;
    a->record[0x197] = a->record[0x62] = 100;
    SEED(3, 1, 1, 1, 8, 8, 8, 20, 20, 20);
    bool done;
    CHECK(cast(m, 0x14, true, &done) && done && a->record[0x197] == 90);
    CHECK(LOGGED("takes 10 points of damage from Electricity"));
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    SEED(3, 1, 1, 1, 8, 8, 8, 20, 1, 1);
    CHECK(cast(m, 0x14, true, &done) && done && a->record[0x197] == 100 && !LOGGED("takes"));
    /* Cause Light Wounds: 1d8 of magic; Cause Serious 2d8 + 1, + 3 for
     * deity 4. */
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    SEED(3, 1, 1, 1, 8, 5, 5, 20, 20, 20);
    CHECK(cast(m, 4, true, &done) && done && a->record[0x197] == 95);
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    m->record[0x5d] = 4;
    SEED(4, 1, 1, 1, 8, 5, 5, 8, 5, 5, 20, 20, 20);
    CHECK(cast(m, 0x42, true, &done) && done && a->record[0x197] == 87);
}

/* Sleep: 4d4 hit dice, those within the area in turn while they last. */
static void test_sleep(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 11, 10),
                  *n = record('N', 1, 10, 11);
    n->record[0xd6] = 3; /* 4 dice */
    KEYS("T");
    SEED(4, 4, 1, 1, 4, 1, 1, 4, 1, 1, 4, 1, 1); /* 4 */
    bool done;
    CHECK(cast(a, 0x15, false, &done) && done && N == 3);
    CHECK(T(1) == a && T(2) != NULL && T(3) == NULL && T(2) == m);
    CHECK(cok_character_find_effect(a, 0x35) != NULL && cok_character_find_effect(m, 0x35) != NULL);
    CHECK(cok_character_find_effect(n, 0x35) == NULL && LOGGED("print: M;print: falls asleep;"));
    /* An elf of 5 dice weighs 10, others 20; one already asleep is left
     * out. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    m->record[0xd6] = 5;
    m->record[0x5a] = 7;
    CHECK(cok_character_add_effect(a, 0x35, 0, 0, false) != NULL);
    KEYS("T");
    SEED(4, 4, 3, 3, 4, 3, 3, 4, 2, 2, 4, 2, 2); /* 10 */
    CHECK(cast(a, 0x15, false, &done) && done && T(1) == NULL && T(2) == m);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    m->record[0xd6] = 5;
    KEYS("T");
    SEED(4, 4, 3, 3, 4, 3, 3, 4, 2, 2, 4, 2, 2);
    CHECK(cast(a, 0x15, false, &done) && done && T(1) == a && T(2) == NULL);
}

/* Confusion: at most 2d8 of the area, each throwing type 4. */
static void test_confusion(void)
{
    reset();
    cok_character *m = record('M', 1, 20, 10), *a = record('A', 0, 10, 10),
                  *b = record('B', 0, 11, 10), *c = record('C', 0, 10, 11);
    a->record[0xd4] = b->record[0xd4] = c->record[0xd4] = 30;
    (void)c;
    SEED(5, 3, 1, 1, 8, 1, 1, 8, 1, 1, 20, 2, 19, 20, 2, 19);
    bool done;
    CHECK(cast(m, 0x52, true, &done) && done && N == 2);
    CHECK(count("is confused") == 2 && cok_character_find_effect(T(1), 0x23) != NULL);
    (void)a, (void)b;
}

/* Dimension Door: the caster to an empty cell it aims at (Manual). */
static void test_door(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    record('M', 1, 20, 10);
    a->record[0xfe] = 3;
    KEYS("M66T");
    bool done;
    CHECK(cast(a, 0x53, false, &done) && done);
    CHECK(game.combat.combatant[1].x == 12 && game.combat.combatant[1].y == 10);
    CHECK(LOGGED("print: A;print: teleports;"));
}

/* Fear: a cone of 3 by 6 toward the cell, either side, the caster left
 * out; a failed throw (type 4) frightens: 0x6f, fleeing, the computer's,
 * a player's character turned, no target. */
static void test_fear(void)
{
    reset();
    cok_character *m = record('M', 1, 16, 10), *a = record('A', 0, 13, 10),
                  *b = record('B', 0, 12, 10), *n = record('N', 1, 15, 10);
    a->record[0xd4] = 30;
    b->record[0xd4] = 0;
    m->record[0xfe] = 1;
    SEED(4, 2, 1, 1, 20, 2, 19, 20, 2, 19, 20, 20, 20);
    bool done;
    a->combat->target = m->record;
    CHECK(cast(m, 0x54, true, &done) && done);
    CHECK(N == 3 && T(1) == n && T(2) == a && T(3) == b);
    CHECK(LOGGED("print: A;print: runs in terror;") && LOGGED("print: B;print: is unaffected;"));
    CHECK(a->combat->forced == 1 && a->record[0x18b] == 1 && a->record[0xe7] == 0xb3);
    CHECK(a->combat->target == NULL && cok_character_find_effect(a, 0x6f) != NULL);
    CHECK(b->record[0xe7] == 0 && b->combat->forced == 0);
    /* Aimed at its own cell, the cone takes as directions what drawing
     * the caster's icon left: its screen offsets. */
    reset();
    a = record('A', 0, 10, 10);
    for (int8_t y = 4; y <= 16; y += 2)
        for (int8_t x = 4; x <= 16; x += 2)
            if (x != 10 || y != 10) record((char)('a' + (x - 4) / 2), 1, x, y);
    KEYS("T");
    CHECK(cast(a, 0x54, false, &done) && done);
    CHECK(game.combat.view_x == 7 && game.combat.view_y == 7);
    CHECK(N == 1 && cok_combat_x(&game.combat, T(1)->record) == 12 &&
          cok_combat_y(&game.combat, T(1)->record) == 16);
    /* The same in EGA mode, whose blit (127f:24fd) leaves the same bytes
     * where the cone reads; in CGA mode (226c) its byte counts are halved
     * (2 bytes a unit). In CGA mode too with the caster at 6, 6 (the view
     * at 0, 0) and 12, 4 (9, 0), where the frame's 144 and the cell row's
     * 1008 (sy + 1) send the cone elsewhere than the Tandy's 288 and 2016
     * would. Monsters on every other cell within 6. */
    static const struct {
        cok_display display;
        int8_t x, y;
        unsigned n;
        int8_t hit[3][2];
    } fears[] = {
        {COK_DISPLAY_EGA, 10, 10, 1, {{12, 16}}},
        {COK_DISPLAY_CGA, 10, 10, 3, {{10, 12}, {10, 14}, {10, 16}}},
        {COK_DISPLAY_CGA, 6, 6, 1, {{10, 4}}},
        {COK_DISPLAY_CGA, 12, 4, 2, {{12, 6}, {14, 10}}},
        {COK_DISPLAY_TANDY, 6, 6, 3, {{8, 8}, {10, 10}, {12, 12}}},
    };
    for (size_t k = 0; k < sizeof fears / sizeof *fears; ++k) {
        reset();
        game.display = fears[k].display;
        int8_t cx = fears[k].x, cy = fears[k].y;
        a = record('A', 0, cx, cy);
        for (int8_t y = (int8_t)(cy - 6); y <= cy + 6; y += 2)
            for (int8_t x = (int8_t)(cx - 6); x <= cx + 6; x += 2)
                if ((x != cx || y != cy) && x > 0 && y > 0) record('a', 1, x, y);
        KEYS("T");
        CHECK(cast(a, 0x54, false, &done) && done);
        CHECK(N == fears[k].n);
        for (unsigned i = 1; i <= N; ++i)
            CHECK(cok_combat_x(&game.combat, T(i)->record) == fears[k].hit[i - 1][0] &&
                  cok_combat_y(&game.combat, T(i)->record) == fears[k].hit[i - 1][1]);
    }
    game.display = COK_DISPLAY_TANDY;
}

/* Lightning Bolt: level d6 on the cell, then on, away from the caster, 7
 * cells; each throws (type 4) for half. A wall in a 3D area turns it
 * back toward the caster. */
static void test_bolt(void)
{
    reset();
    cok_character *m = record('M', 1, 10, 10), *a = record('A', 0, 12, 10),
                  *b = record('B', 0, 16, 10), *c = record('C', 0, 20, 10);
    m->record[0xfe] = 2;
    a->record[0x197] = a->record[0x62] = b->record[0x197] = b->record[0x62] = 100;
    c->record[0x197] = c->record[0x62] = 100;
    a->record[0xd4] = b->record[0xd4] = c->record[0xd4] = 30;
    SEED(2, 2, 1, 1, 6, 6, 6); /* A of A and B within 6 */
    bool done;
    CHECK(cast(m, 0x33, true, &done) && done);
    CHECK(a->record[0x197] < 100 && b->record[0x197] < 100 && c->record[0x197] == 100);
    CHECK(LOGGED("from Electricity") && game.effects.rolls.images == 0);
    /* A wall beside A in a 3D area turns it back: A again, the 8 the
     * bounce charges near the caster and the length already counted
     * ending it there. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 10);
    m->record[0xfe] = 2;
    m->record[0x197] = m->record[0x62] = a->record[0x197] = a->record[0x62] = 100;
    game.combat.cells[10][13] = 0x01;
    game.vm.mem4b00[0xe6] = 1;
    SEED(1, 1, 1, 1);
    CHECK(cast(m, 0x33, true, &done) && done);
    CHECK(count("print: A;print: takes") == 2 && !LOGGED("print: M;print: takes"));
    CHECK(game.spell_x == 13 && game.spell_y == 10);
    /* 0x3c's bolt, with no such charge, comes back to M too; outside a 3D
     * area it goes through the wall. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 10);
    m->record[0x197] = m->record[0x62] = a->record[0x197] = a->record[0x62] = 100;
    game.combat.cells[10][13] = 0x01;
    game.vm.mem4b00[0xe6] = 1;
    SEED(1, 1, 1, 1);
    CHECK(cast(m, 0x3c, true, &done) && done);
    CHECK(count("print: A;print: takes") == 2 && count("print: M;print: takes") == 1);
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 10);
    b = record('B', 0, 14, 10);
    a->record[0x197] = a->record[0x62] = b->record[0x197] = b->record[0x62] = 100;
    game.combat.cells[10][13] = 0x01;
    SEED(1, 2, 1, 1);
    CHECK(cast(m, 0x3c, true, &done) && done);
    CHECK(count("print: A;print: takes") == 1 && count("print: B;print: takes") == 1);
    /* 0x3c: 1d6 + 20 on the cell, 20 after it, electricity alone. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 10);
    b = record('B', 0, 13, 10);
    a->record[0x197] = a->record[0x62] = b->record[0x197] = b->record[0x62] = 100;
    a->record[0xd4] = b->record[0xd4] = 30;
    SEED(4, 2, 1, 1, 6, 6, 6, 20, 2, 19, 20, 2, 19);
    CHECK(cast(m, 0x3c, true, &done) && done && a->record[0x197] == 74 && b->record[0x197] == 80);
}

/* Poison: the caster's target poisoned (0x40's handler) unless event 9
 * cancels it; Sticks to Snakes; Charm Person; Snake Charm; Fumble. */
static void test_others(void)
{
    reset();
    cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
    a->record[0xd0] = 30;
    SEED(2, 1, 1, 1, 20, 2, 19);
    bool done;
    CHECK(cast(m, 0x44, true, &done) && done && LOGGED("print: A;print: is Poisoned;"));
    CHECK(a->record[0x189] == 0 && cok_character_find_effect(a, 0x37) != NULL);
    /* Sticks to Snakes: fewer than 6 dice, effect 3 and its handler. */
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    CHECK(cast(m, 0x46, true, &done) && done && cok_character_find_effect(a, 3) != NULL);
    CHECK(LOGGED("is fighting with snakes"));
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd6] = 6;
    CHECK(cast(m, 0x46, true, &done) && done && LOGGED("print: A;print: smashes them flat;"));
    CHECK(cok_character_find_effect(a, 3) == NULL);
    /* Charm Person: not one larger than a man. */
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0xcf] = 2;
    CHECK(cast(m, 0x0a, true, &done) && done && LOGGED("print: A;print: is unaffected;"));
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd4] = 30;
    a->record[0x13f] = 1; /* above 1 only is a monster's */
    SEED(2, 1, 1, 1, 20, 2, 19);
    CHECK(cast(m, 0x0a, true, &done) && done && LOGGED("print: A;print: is charmed;"));
    cok_effect *e = cok_character_find_effect(a, 0x0b);
    CHECK(e != NULL && e->on_remove && (e->value & 0x80) != 0);
    /* Snake Charm: the snakes in the list within the caster's hit points. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 15, 10);
    cok_character *n = record('N', 1, 16, 10), *o = record('O', 1, 17, 10);
    m->record[0x13f] = n->record[0x13f] = o->record[0x13f] = 0x10;
    a->record[0x197] = 15;
    m->record[0x197] = 10;
    n->record[0x197] = 10;
    o->record[0x197] = 5;
    CHECK(cast(a, 0x1b, false, &done) && done && N == 2 && T(1) == m && T(2) == o);
    CHECK(cok_character_find_effect(n, 0x33) == NULL && cok_character_find_effect(o, 0x33) != NULL);
    /* Fumble: a failed throw, clumsy (0x1b), its turn over; then applied
     * again. */
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd4] = 30;
    SEED(3, 1, 1, 1, 20, 2, 19, 20, 2, 19);
    CHECK(cast(m, 0x56, true, &done) && done && count("print: A;print: is clumsy;") == 2);
    CHECK(LOGGED("effect: A loses 0x1b;"));
    CHECK(a->combat->initiative == 0 && game.effects.rolls.damage_type == 0);
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    SEED(3, 1, 1, 1, 20, 20, 20, 20, 20, 20);
    CHECK(cast(m, 0x56, true, &done) && done && LOGGED("print: A;print: is slowed;"));
    CHECK(cok_character_find_effect(a, 0x2a) != NULL);
}

/* Spells whose handlers do nothing: used up all the same. (Stinking
 * Cloud's clouds: test_cloud.) */
static void test_empty(void)
{
    static const uint8_t empty[] = {0x48, 0x49, 0x4a, 0x4c, 0x4e, 0x4f, 0x5b, 0x5c, 0x5d, 0x62};
    for (size_t i = 0; i < sizeof empty; ++i) {
        reset();
        cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
        a->record[0x197] = 50;
        bool done;
        CHECK(cast(m, empty[i], true, &done) && done && m->record[0x1e] == 0);
        CHECK(a->record[0x197] == 50 && a->effects == NULL);
    }
    reset();
    cok_character *m = record('M', 1, 11, 10);
    record('A', 0, 10, 10);
    bool done;
    CHECK(cast(m, 0x22, true, &done) && done && LOGGED("print: M;print: Creates a noxious cloud;"));
    CHECK(game.combat.clouds != NULL && game.combat.cells[10][10] == 0x1e);
    /* Dispel Magic tries it (test_cloud): A, of level 1, against M's of
     * level 20, 12, resisted by a d100 above. M's 0x28 is made
     * undispellable (0xff), so that only the cloud is tried. */
    cok_character *a = game.party.members[1];
    CHECK(a->record[1] == 'A');
    m->record[0xfe] = 20;
    a->record[0xfe] = 1;
    cok_character_find_effect(m, 0x28)->value = 0xff;
    CHECK(cast(a, 0x29, true, &done) && done && game.combat.clouds->resisted == 1);
}

/* Slow: those against the caster within the area, up to its level;
 * Haste cured in them instead. */
static void test_slow(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 11, 10),
                  *n = record('N', 1, 12, 10);
    a->record[0xfe] = 1;
    CHECK(cok_character_add_effect(n, 0x27, 10, 0, false) != NULL);
    KEYS("NT");
    bool done;
    CHECK(cast(a, 0x37, false, &done) && done);
    CHECK(cok_character_find_effect(m, 0x2a) != NULL && cok_character_find_effect(n, 0x2a) == NULL);
    CHECK(cok_character_find_effect(n, 0x27) != NULL && cok_character_find_effect(a, 0x2a) == NULL);
}

/* The computer's Cure Light Wounds (432f:1eed): the most hurt ally around
 * it, itself below half, or a body. */
static void test_wounded(void)
{
    reset();
    cok_character *m = record('M', 1, 10, 10), *n = record('N', 1, 11, 10),
                  *o = record('O', 1, 9, 10);
    record('A', 0, 12, 10);
    cok_character *ally;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == NULL);
    n->record[0x197] = 5;
    o->record[0x197] = 7;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == n);
    m->record[0x197] = 4;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == m);
    m->record[0x197] = 9;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == n);
    SEED(1, 8, 3, 3);
    bool done;
    CHECK(cast(m, 3, true, &done) && done && n->record[0x197] == 8);
    CHECK(LOGGED("print: N;print: is partially healed;"));
    /* A body beside it, none below 8. */
    n->record[0x197] = o->record[0x197] = 10;
    m->record[0x197] = 10;
    cok_character *b = record('B', 1, 30, 10);
    game.combat.cells[11][10] = COK_COMBAT_BODY;
    game.combat.body[0] = (cok_combat_body){b, 10, 11, COK_COMBAT_FLOOR};
    game.combat.bodies = 1;
    n->record[0x197] = 9;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == b);
    b->record[0x188] = 6;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == n);
}

/* Effect 0x4a (3f44:26a5) at event 0x0e: on a d20 of 1 Dispel Magic's
 * handler (5b04:320f) runs for the selected character, at the caster
 * level of the spell the last cast left (DS:6b33, which stays), on the
 * targets it left, each pass on the first. */
static void test_dispelling(void)
{
    reset();
    cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
    cok_character *b = record('B', 0, 12, 10);
    m->record[0xfe] = 9; /* a mage of 9: Dispel at 9, Bless (a cleric's) at 0 */
    CHECK(cok_character_add_effect(m, 0x4a, 0, 0xff, false) != NULL);
    CHECK(cok_character_add_effect(a, 0x01, 0, 3, false) != NULL);
    CHECK(cok_character_add_effect(b, 0x01, 0, 3, false) != NULL);
    game.vm.character = m->record;
    T(1) = a;
    T(2) = b;
    N = 2;
    game.effects.rolls.spell = 0x01;
    /* At level 0 against 3 the chance is 44 (50 less 2 a level): two
     * passes on A, d100s of 45 and 80 fail, where at Dispel's own level
     * (80) they would not. */
    uint32_t start = SEED(3, 20, 1, 1, 100, 45, 80, 100, 45, 80);
    CHECK(cok_effects_dispatch(&game.effects, m, 0x0e) && game.vm.status == COK_ECL_OK);
    CHECK(a->effects != NULL && b->effects != NULL && !LOGGED("is affected"));
    CHECK(game.vm.seed == AFTER(start, 3, 20, 100, 100) && game.effects.rolls.spell == 0x01);
    CHECK(game.effects.rolls.images == 1);
    /* A d20 of 2: nothing. */
    start = SEED(1, 20, 2, 2);
    CHECK(cok_effects_dispatch(&game.effects, m, 0x0e) && game.vm.seed == AFTER(start, 1, 20));
    /* 44 or less takes A's Bless, "is affected"; B keeps its. */
    SEED(2, 20, 1, 1, 100, 1, 44);
    CHECK(cok_effects_dispatch(&game.effects, m, 0x0e) && game.vm.status == COK_ECL_OK);
    CHECK(a->effects == NULL && b->effects != NULL && LOGGED("print: A;print: is affected;"));
    /* A first target freed since (an earlier battle's) stops the run. */
    T(1) = &cok_arena_freed;
    SEED(1, 20, 1, 1);
    CHECK(!cok_effects_dispatch(&game.effects, m, 0x0e) && game.vm.status == COK_ECL_UNDEFINED);
    CHECK(strstr(game.error, "freed since") != NULL);
    T(1) = NULL;
}

/* 0x47 turns a spell back on a d10 of 1: Magic Missile on its caster. */
static void test_reflect(void)
{
    reset();
    cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
    m->record[0x197] = m->record[0x62] = 100;
    CHECK(cok_character_add_effect(a, 0x47, 0, 0, false) != NULL);
    SEED(5, 1, 1, 1, 4, 2, 2, 4, 2, 2, 4, 2, 2, 10, 1, 1); /* 3d4 + 3 at level 6 */
    bool done;
    flight_screens = 2166136261u;
    CHECK(cast(m, 0x0f, true, &done) && done && LOGGED("print: A;print: reflects the spell;"));
    CHECK(flight_screens == 0xc99a884cu);
    CHECK(LOGGED("print: M;print: takes 9 points of damage") && m->record[0x197] == 91);
    CHECK(a->record[0x197] == 10 && T(1) == m && game.spell_x == 11);
    /* Outside a battle (a spell cast in camp) the turning back reads the
     * last battle's tables and freed map: a stop. */
    game.effects.in_battle = false;
    game.effects.rolls.spell = 0x0f;
    SEED(1, 10, 1, 1);
    s.log[0] = '\0';
    CHECK(!cok_effects_run(&game.effects, a, 0x47, cok_character_find_effect(a, 0x47)));
    CHECK(strstr(game.effects.error, "0x47 (3f44:23a4) turns a spell back outside") != NULL);
    CHECK(!LOGGED("reflects the spell"));
    game.effects.failed = false;
    game.effects.in_battle = true;
    game.effects.rolls.spell = 0;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
}

/* What 6beb:0e08's stale byte can read: the exploding list's 1, 8 and 15,
 * the spell targets' 1, 8, ..., 64. A record listed there, killed where
 * the byte is not known (a destroyed undead), stops the run. */
static void test_reachable(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 11, 10),
                  *c = record('C', 1, 12, 10), *d = record('D', 1, 13, 10);
    game.spell_targets[1] = a;
    game.spell_targets[8] = b;
    game.spell_targets[64] = c;
    game.spell_targets[2] = d;
    game.combat.exploding[15] = d;
    cok_arena_reachable r = cok_cast_reachable(&game);
    CHECK(r.targets[0] == a && r.targets[1] == b && r.targets[9] == c && r.targets[2] == NULL);
    CHECK(r.exploding[2] == d && r.exploding[0] == NULL);
    CHECK(!cok_arena_kill(&game, c, COK_ARENA_STALE_UNKNOWN, &r));
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    memset(game.spell_targets, 0, sizeof game.spell_targets);
    game.combat.exploding[15] = NULL;
    /* A record freed with its party member (cok_cast_forget) is marked in
     * both lists: a byte that could reach it, unknown, stops the run, and
     * so does 0x47 turning a spell toward it as the first target. */
    reset();
    a = record('A', 0, 10, 10);
    c = record('C', 1, 12, 10);
    b = record('B', 0, 11, 10);
    game.spell_targets[1] = b;
    game.spell_target_count = 1;
    game.combat.exploding[8] = b;
    cok_cast_forget(&game, b);
    CHECK(T(1) == &cok_arena_freed && game.combat.exploding[8] == &cok_arena_freed);
    r = cok_cast_reachable(&game);
    CHECK(r.targets[0] == &cok_arena_freed && r.exploding[1] == &cok_arena_freed);
    CHECK(!cok_arena_kill(&game, c, COK_ARENA_STALE_UNKNOWN, &r));
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.vm.character = c->record;
    cok_character *caster;
    CHECK(!cok_cast_reflect(&game, a, &caster) && game.vm.status == COK_ECL_UNDEFINED);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    memset(game.spell_targets, 0, sizeof game.spell_targets);
    game.combat.exploding[8] = NULL;
}

/* Cases for the mutants: Bless (0x69) in combat leaves out an ally with
 * an enemy beside it; touch reads the armour class after the target's
 * event 0x0b (Shield, 0x11, makes it at least 0x39) and a miss counts as
 * saved (Cause Blindness: "is Unaffected"); the player's pick becomes the
 * caster's target; Dimension Door cannot pick a creature. */
static void test_more(void)
{
    flight_steps = 0;
    reset();
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 11, 10);
    record('M', 1, 12, 10);
    KEYS("T");
    bool done;
    CHECK(cast(a, 0x69, false, &done) && done && T(2) == NULL);
    CHECK(LOGGED("print: A;print: is Blessed;") && !LOGGED("print: B;print: is Blessed;"));
    CHECK(cok_character_find_effect(b, 1) == NULL && game.combat.show_actions);
    /* The attack roll that hits armour class 0x32 but not Shield's 0x39. */
    uint8_t hit_at = 0;
    for (uint8_t d = 2; d <= 19 && hit_at == 0; ++d) {
        reset();
        cok_character *m = record('M', 1, 11, 10);
        a = record('A', 0, 10, 10);
        a->record[0x197] = a->record[0x62] = 100;
        SEED(3, 1, 1, 1, 8, 1, 1, 20, d, d);
        CHECK(cast(m, 4, true, &done) && done);
        if (a->record[0x197] < 100) hit_at = d;
    }
    CHECK(hit_at > 0 && hit_at < 19);
    reset();
    cok_character *m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    CHECK(a->record[0x18d] < 0x39);
    CHECK(cok_character_add_effect(a, 0x11, 0, 0, false) != NULL);
    a->record[0x18e] = a->record[0x18d];
    SEED(3, 1, 1, 1, 8, 1, 1, 20, hit_at, hit_at);
    CHECK(cast(m, 4, true, &done) && done && a->record[0x197] == 100 && !LOGGED("takes"));
    /* A miss with Cause Blindness: saved, against a save of kind 1. */
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd4] = 30;
    SEED(3, 1, 1, 1, 20, 2, 19, 20, 1, 1);
    CHECK(cast(m, 0x26, true, &done) && done && LOGGED("print: A;print: is Unaffected;"));
    CHECK(cok_character_find_effect(a, 0x21) == NULL);
    /* The player's pick is its target. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    KEYS("NT");
    flight_steps = 0;
    CHECK(cast(a, 0x0f, false, &done) && done && a->combat->target == m->record);
    CHECK(flight_steps > 0);
    /* A single target cannot be an empty cell. */
    reset();
    a = record('A', 0, 10, 10);
    record('M', 1, 14, 10);
    KEYS("M66T\x1b\x1bY");
    CHECK(cast(a, 0x0f, false, &done) && !done && LOGGED("print: Spell Aborted;"));
    /* Dimension Door: the cursor cannot pick the cell of a creature. */
    reset();
    a = record('A', 0, 10, 10);
    record('M', 1, 11, 10);
    a->record[0xfe] = 3;
    KEYS("M6T\x1b\x1bY");
    CHECK(cast(a, 0x53, false, &done) && !done && LOGGED("print: Spell Aborted;"));
    CHECK(game.combat.combatant[1].x == 10);
}

/* The computer's Cure Light Wounds, its ally: on its side, the first of
 * the fewest, itself below half though not the fewest, a body where none
 * has fewer than 8. */
static void test_wounded_more(void)
{
    reset();
    cok_character *m = record('M', 1, 10, 10), *n = record('N', 1, 10, 9),
                  *o = record('O', 1, 11, 10), *e = record('E', 0, 9, 10);
    cok_character *ally;
    e->record[0x197] = 1;
    n->record[0x197] = o->record[0x197] = 5;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == n);
    n->record[0x197] = 3;
    m->record[0x197] = 4;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == m);
    m->record[0x197] = 5;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == n);
    /* With a body beside it: below 8 the ally, at 8 the body. */
    n->record[0x197] = m->record[0x197] = 10;
    o->record[0x197] = 7;
    cok_character *b = record('B', 1, 30, 10);
    game.combat.cells[11][10] = COK_COMBAT_BODY;
    game.combat.body[0] = (cok_combat_body){b, 10, 11, COK_COMBAT_FLOOR};
    game.combat.bodies = 1;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == o);
    o->record[0x197] = 8;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == b);
    /* Two bodies, the second listed far off: the one beside it. */
    cok_character *f = record('F', 1, 31, 10);
    game.combat.body[1] = (cok_combat_body){f, 20, 20, COK_COMBAT_FLOOR};
    game.combat.bodies = 2;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == b);
    /* A body listed beside it on a cell that is not a body's (its tile
     * gone): not taken. */
    game.combat.cells[11][10] = COK_COMBAT_FLOOR;
    CHECK(cok_cast_wounded_ally(&game, m, &ally) && ally == o);
    /* The computer tests Cure Light Wounds (3afb:03b9) by it: with no
     * enemy in reach it suits only with an ally to heal. */
    reset();
    m = record('M', 1, 10, 10);
    n = record('N', 1, 11, 10);
    record('A', 0, 30, 10);
    game.vm.character = m->record;
    bool fits;
    CHECK(cok_ai_spell_fits(&game, m, 3, 0, &fits) && !fits);
    n->record[0x197] = 5;
    CHECK(cok_ai_spell_fits(&game, m, 3, 0, &fits) && fits);
}

/* 0x47 turning an effect (event 9): Ray of Enfeeblement weakens the
 * caster; the handlers after 0x47 run for the caster (Shield cancelling
 * Magic Missile); an area spell takes no mirror image (no die rolled). */
static void test_turned(void)
{
    reset();
    cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
    CHECK(cok_character_add_effect(a, 0x47, 0, 0, false) != NULL);
    a->record[0xd4] = 30;
    SEED(3, 1, 1, 1, 20, 2, 19, 10, 1, 1);
    bool done;
    CHECK(cast(m, 0x21, true, &done) && done && LOGGED("print: A;print: reflects the spell;"));
    CHECK(cok_character_find_effect(m, 0x1d) != NULL && cok_character_find_effect(a, 0x1d) == NULL);
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    m->record[0x197] = m->record[0x62] = 100;
    CHECK(cok_character_add_effect(a, 0x47, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(m, 0x11, 0, 0, false) != NULL);
    SEED(5, 1, 1, 1, 4, 2, 2, 4, 2, 2, 4, 2, 2, 10, 1, 1);
    CHECK(cast(m, 0x0f, true, &done) && done && LOGGED("reflects the spell"));
    CHECK(m->record[0x197] == 100 && !LOGGED("takes"));
    /* Fireball on one with images: their die (event 6) above 1 takes none,
     * for an area spell. */
    reset();
    m = record('M', 1, 15, 10);
    a = record('A', 0, 10, 10);
    m->record[0xfe] = 1;
    a->record[0x197] = a->record[0x62] = 100;
    CHECK(cok_character_add_effect(a, 0x1c, 0, 0x31, false) != NULL);
    SEED(4, 1, 1, 1, 6, 1, 6, 20, 1, 20, 4, 2, 4);
    CHECK(cast(m, 0x2f, true, &done) && done && a->record[0x197] < 100);
    CHECK(!LOGGED("lost an image"));
    /* Ice Storm too, whose handler leaves the area flag as the targets set
     * it (no save: 3d10, then the images' die). */
    reset();
    m = record('M', 1, 15, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    CHECK(cok_character_add_effect(a, 0x1c, 0, 0x31, false) != NULL);
    SEED(5, 1, 1, 1, 10, 1, 10, 10, 1, 10, 10, 1, 10, 4, 2, 4);
    CHECK(cast(m, 0x57, true, &done) && done && a->record[0x197] < 100);
    CHECK(!LOGGED("lost an image"));
    /* Magic resistance takes magic damage: Fireball, Ice Storm, Lightning
     * Bolt (with an effect pending, DS:6b2f). */
    static const uint8_t magic[3] = {0x2f, 0x57, 0x33};
    for (unsigned k = 0; k < 3; ++k) {
        reset();
        m = record('M', 1, 15, 10);
        a = record('A', 0, 10, 10);
        a->record[0x197] = a->record[0x62] = 100;
        a->record[0x187] = 100;
        game.effects.rolls.pending = 1;
        CHECK(cast(m, magic[k], true, &done) && done && a->record[0x197] == 100);
    }
    CHECK(game.effects.rolls.damage_type == 0x0c);
}

/* Weighed picks: the allowance (2d4) counts with two picks or more, a
 * pick of 3 hit dice weighs 4; Faerie Fire weighs size 4 at 4. */
static void test_weights(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 12, 10);
    record('N', 1, 13, 10);
    m->record[0xd6] = 3;
    KEYS("NT\x1b");
    SEED(2, 4, 1, 1, 4, 1, 1); /* 2: M's 4 is over, but it is the first */
    bool done;
    CHECK(cast(a, 0x51, false, &done) && done && N == 1 && s.at == s.length);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    cok_character *n = record('N', 1, 13, 10), *o = record('O', 1, 14, 10);
    (void)n, (void)o;
    KEYS("NTNNTNNNT\x1b");
    SEED(2, 4, 1, 1, 4, 1, 1); /* 2: two picks of 1 are not over it */
    CHECK(cast(a, 0x51, false, &done) && done && N == 3 && s.at == s.length - 1);
    /* Faerie Fire: the caster level (6, a fighter's) against sizes: 4, 1
     * and 2 are past it. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    n = record('N', 1, 13, 10);
    o = record('O', 1, 14, 10);
    m->record[0xcf] = 4;
    o->record[0xcf] = 2;
    KEYS("NTNNTNNNT\x1b");
    CHECK(cast(a, 0x4f, false, &done) && done && N == 3 && s.at == s.length - 1);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    n = record('N', 1, 13, 10);
    m->record[0xd6] = 3;
    KEYS("NTNNT\x1b");
    SEED(2, 4, 3, 3, 4, 2, 2); /* 5: 4 and 1 */
    CHECK(cast(a, 0x51, false, &done) && done && N == 2 && s.at == s.length);
}

/* The computer's Hold Person at a lone enemy: its second and third picks
 * of it are lost; Hold's saving throw bonus, -3 for 0x31, -1 for two. */
static void test_hold_more(void)
{
    reset();
    cok_character *m = record('M', 1, 12, 10), *a = record('A', 0, 10, 10);
    bool done;
    CHECK(cast(m, 0x17, true, &done) && done && N == 1 && T(1) == a);
    /* A d20 of 14 against 12: made at -2, not at -3. */
    reset();
    m = record('M', 1, 12, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd4] = 12;
    SEED(5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 20, 14, 14);
    CHECK(cast(m, 0x31, true, &done) && done && LOGGED("print: A;print: is held;"));
    reset();
    m = record('M', 1, 12, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd4] = 12;
    SEED(4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 20, 14, 14);
    CHECK(cast(m, 0x17, true, &done) && done && LOGGED("print: A;print: is Unaffected;"));
    /* Two: -1, a d20 of 12 against 12 fails. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 12, 10);
    cok_character *n = record('N', 1, 13, 10);
    m->record[0xd4] = n->record[0xd4] = 12;
    KEYS("NTNNT\x1b");
    SEED(2, 20, 12, 12, 20, 12, 12);
    CHECK(cast(a, 0x17, false, &done) && done && N == 2);
    CHECK(LOGGED("print: N;print: is held;") && LOGGED("print: M;print: is held;"));
    /* 0x31's four picks, at no bonus. */
    reset();
    a = record('A', 0, 10, 10);
    a->record[0xfe] = 9;
    cok_character *held[4];
    for (int k = 0; k < 4; ++k) {
        held[k] = record((char)('M' + k), 1, (int8_t)(12 + k), 10);
        held[k]->record[0xd4] = 12;
    }
    KEYS("NTNNTNNNTNNNNT");
    SEED(4, 20, 11, 11, 20, 11, 11, 20, 11, 11, 20, 11, 11);
    CHECK(cast(a, 0x31, false, &done) && done && N == 4 && s.at == s.length);
    for (int k = 0; k < 4; ++k)
        CHECK(T(k + 1) == held[k] && cok_character_find_effect(held[k], 0x34) != NULL);
}

/* Small ones: Cause Disease's effect runs when removed; 0x40's dice, 2 x
 * 1d3 + 1; Snake Charm skips snakes that cannot act or are charmed; Sleep
 * skips one unconscious, and an elf of 5 dice weighs 10, another 20; Slow
 * cures Haste; Confusion's save is of kind 1; Fumble's damage type; Fear
 * turns only a player's character and runs 0x6f when removed. */
static void test_small(void)
{
    reset();
    cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
    a->record[0xd4] = 30;
    SEED(3, 1, 1, 1, 20, 2, 19, 20, 20, 20);
    bool done;
    CHECK(cast(m, 0x28, true, &done) && done);
    cok_effect *e = cok_character_find_effect(a, 0x22);
    CHECK(e != NULL && e->on_remove);
    reset();
    m = record('M', 1, 15, 10);
    a = record('A', 0, 10, 10);
    a->record[0x197] = a->record[0x62] = 100;
    a->record[0xd4] = 30;
    SEED(6, 1, 1, 1, 3, 1, 1, 6, 6, 6, 6, 6, 6, 6, 6, 6, 20, 2, 19);
    CHECK(cast(m, 0x40, true, &done) && done && a->record[0x197] == 82);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 15, 10);
    cok_character *n = record('N', 1, 16, 10), *o = record('O', 1, 17, 10);
    m->record[0x13f] = n->record[0x13f] = o->record[0x13f] = 0x10;
    m->record[0x189] = 0;
    CHECK(cok_character_add_effect(n, 0x33, 0, 0, false) != NULL);
    CHECK(cast(a, 0x1b, false, &done) && done && N == 1 && T(1) == o);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    m->record[0x188] = 1;
    KEYS("T");
    SEED(4, 4, 1, 1, 4, 1, 1, 4, 1, 1, 4, 1, 1);
    CHECK(cast(a, 0x15, false, &done) && done && T(2) == NULL);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    m->record[0xd6] = 5;
    KEYS("T");
    SEED(4, 4, 3, 3, 4, 3, 3, 4, 3, 3, 4, 2, 2); /* 11: A 1, then 10 left */
    CHECK(cast(a, 0x15, false, &done) && done && T(1) == a && T(2) == NULL);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    a->record[0xfe] = 1;
    CHECK(cok_character_add_effect(m, 0x27, 10, 0, false) != NULL);
    KEYS("NT");
    CHECK(cast(a, 0x37, false, &done) && done && cok_character_find_effect(m, 0x27) == NULL);
    CHECK(cok_character_find_effect(m, 0x2a) == NULL && LOGGED("print: M;print: is Cured;"));
    reset();
    m = record('M', 1, 20, 10);
    a = record('A', 0, 10, 10);
    SEED(4, 1, 1, 1, 8, 1, 1, 8, 1, 1, 20, 20, 20);
    CHECK(cast(m, 0x52, true, &done) && done && LOGGED("print: A;print: is Unaffected;"));
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd4] = 30;
    SEED(2, 1, 1, 1, 20, 2, 19);
    game.effects.rolls.damage_type = 0;
    CHECK(cast(m, 0x56, true, &done) && done);
    reset();
    m = record('M', 1, 16, 10);
    a = record('A', 0, 13, 10);
    cok_character *b = record('B', 1, 14, 10);
    a->record[0xd4] = b->record[0xd4] = 30;
    a->record[0xe7] = 0x80;
    m->record[0xfe] = 1;
    SEED(3, 1, 1, 1, 20, 2, 19, 20, 2, 19);
    CHECK(cast(m, 0x54, true, &done) && done && a->record[0xe7] == 0x80);
    e = cok_character_find_effect(a, 0x6f);
    CHECK(e != NULL && e->on_remove);
}

/* Poison: magic resistance cancels 0x40 on event 9, and nothing is
 * poisoned. Fumble's save is against damage of type 0x40, not a type the
 * last spell left (fire: 0x14's +3 would make it). */
static void test_poison_fumble(void)
{
    reset();
    cok_character *m = record('M', 1, 11, 10), *a = record('A', 0, 10, 10);
    a->record[0xd0] = 30;
    a->record[0x187] = 100;
    bool done;
    CHECK(cast(m, 0x44, true, &done) && done && !LOGGED("is Poisoned"));
    CHECK(a->record[0x189] != 0);
    reset();
    m = record('M', 1, 11, 10);
    a = record('A', 0, 10, 10);
    a->record[0xd4] = 15;
    CHECK(cok_character_add_effect(a, 0x14, 0, 0, false) != NULL);
    game.effects.rolls.damage_type = 1;
    SEED(2, 1, 1, 1, 20, 13, 13);
    CHECK(cast(m, 0x56, true, &done) && done && LOGGED("print: A;print: is clumsy;"));
    CHECK(!LOGGED("is slowed") && cok_character_find_effect(a, 0x2a) == NULL);
}

/* Fear's cone: along a diagonal, 3 a step, to 12 (the aim pushed on two
 * steps); the sides by the last step that met one (east: south and north
 * of the end); with none on the line, the right side 4 east of the end;
 * after one met twice (two cells across), no one further is added; a
 * large caster left out. */
static void test_cones(void)
{
    reset();
    cok_character *m = record('M', 1, 10, 10), *a = record('A', 0, 11, 11),
                  *x = record('X', 1, 16, 16), *y = record('Y', 1, 14, 14);
    a->record[0xd4] = x->record[0xd4] = y->record[0xd4] = 30;
    m->record[0xfe] = 1;
    bool done;
    CHECK(cast(m, 0x54, true, &done) && done && N == 2 && T(1) == a && T(2) == y);
    (void)x;
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 13, 10);
    y = record('Y', 0, 16, 9);
    cok_character *z = record('Z', 0, 16, 11);
    m->record[0xfe] = 1;
    a->record[0xd4] = y->record[0xd4] = z->record[0xd4] = 30;
    SEED(1, 3, 1, 1);
    CHECK(cast(m, 0x54, true, &done) && done && N == 3 && T(1) == a);
    CHECK((T(2) == z && T(3) == y) || (T(2) == y && T(3) == z));
    /* The player's at an empty cell: the right side to 4 east of the end
     * (16, 10, pushed on from 13). */
    reset();
    cok_character *p = record('P', 0, 10, 10);
    x = record('X', 1, 18, 10);
    record('F', 1, 10, 20);
    x->record[0xd4] = 30;
    p->record[0xfe] = 1;
    KEYS("M666T");
    CHECK(cast(p, 0x54, false, &done) && done && N == 1 && T(1) == x);
    CHECK(LOGGED("print: X;print: runs in terror;"));
    /* X at that end itself, 20, 10. */
    reset();
    p = record('P', 0, 10, 10);
    x = record('X', 1, 20, 10);
    record('F', 1, 10, 20);
    x->record[0xd4] = 30;
    p->record[0xfe] = 1;
    KEYS("M666T");
    CHECK(cast(p, 0x54, false, &done) && done && N == 1 && T(1) == x);
    /* Two cells across met twice: X beyond is not added. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 10);
    x = record('X', 0, 15, 10);
    game.combat.combatant[2].size = 3;
    CHECK(cok_combat_occupy(&game.combat));
    m->record[0xfe] = 1;
    SEED(1, 2, 1, 1);
    CHECK(cast(m, 0x54, true, &done) && done && N == 1 && T(1) == a);
    (void)x;
    /* A wall on the line pulls the end back to it: X past it is not in
     * the cone. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 10);
    x = record('X', 1, 15, 10);
    game.combat.cells[10][14] = 0x01;
    m->record[0xfe] = 1;
    CHECK(cast(m, 0x54, true, &done) && done && N == 1 && T(1) == a);
    /* The line's own steps repeated (east, then south-east): the end at
     * 15, 13 with U beside it, not straight on. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 11);
    record('P', 1, 16, 11);
    record('Q', 1, 15, 12);
    cok_character *r = record('R', 1, 15, 13), *u = record('U', 1, 16, 13);
    record('S', 1, 15, 14);
    record('V', 1, 16, 12);
    m->record[0xfe] = 1;
    CHECK(cast(m, 0x54, true, &done) && done && N == 4 && T(1) == a && T(2) == r && T(4) == u);
    /* Toward the map's edge the aim is pushed on only to it. */
    reset();
    m = record('M', 1, 45, 14);
    a = record('A', 0, 49, 17);
    m->record[0xfe] = 1;
    CHECK(cast(m, 0x54, true, &done) && done && N == 1 && T(1) == a);
    /* A caster of two by two, its own cells on the lines. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 14, 10);
    game.combat.combatant[1].size = 4;
    CHECK(cok_combat_occupy(&game.combat));
    m->record[0xfe] = 1;
    CHECK(cast(m, 0x54, true, &done) && done && N == 1 && T(1) == a);
}

/* Bolts: one with no one in its way ends at its length (7 cells: X at
 * the 8th is not hit); a floor between two walls lets it bounce again
 * (from 13 back to the wall at 7). */
static void test_bolt_more(void)
{
    reset();
    cok_character *m = record('M', 1, 10, 10), *a = record('A', 0, 12, 10),
                  *x = record('X', 0, 20, 10);
    m->record[0xfe] = 2;
    a->record[0x197] = a->record[0x62] = x->record[0x197] = x->record[0x62] = 100;
    SEED(1, 1, 1, 1);
    bool done;
    CHECK(cast(m, 0x33, true, &done) && done && x->record[0x197] == 100);
    reset();
    cok_character *p = record('P', 0, 5, 10);
    record('F', 1, 5, 20);
    p->record[0xfe] = 3; /* a range of 7 */
    game.combat.cells[10][13] = game.combat.cells[10][7] = 0x01;
    game.vm.mem4b00[0xe6] = 1;
    game.combat.see_all = 1; /* the cell seen past the wall at 7 */
    KEYS("M6666666T");
    CHECK(cast(p, 0x33, false, &done) && done && game.spell_x == 7 && game.spell_y == 10);
    /* Back from a bounce (at 15, 4), the first cell (16, 5) a wall: the
     * flag still set, it does not bounce there. */
    reset();
    m = record('M', 1, 20, 12);
    a = record('A', 0, 16, 6);
    m->record[0xfe] = 9;
    m->record[0x197] = m->record[0x62] = a->record[0x197] = a->record[0x62] = 200;
    game.combat.cells[4][15] = game.combat.cells[5][16] = 0x01;
    game.vm.mem4b00[0xe6] = 1;
    game.combat.see_all = 1;
    CHECK(cast(m, 0x33, true, &done) && done && game.spell_x == 15 && game.spell_y == 4);
    /* A combatant two cells across met again where its second cell ends a
     * segment (terrain of cost 2): not hit twice. */
    reset();
    m = record('M', 1, 10, 10);
    a = record('A', 0, 12, 10);
    game.combat.combatant[2].size = 3;
    CHECK(cok_combat_occupy(&game.combat));
    game.combat.cells[10][13] = 0x30;
    m->record[0xfe] = 2;
    a->record[0x197] = a->record[0x62] = 200;
    CHECK(cast(m, 0x33, true, &done) && done && count("print: A;print: takes") == 1);
}

static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("spell: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    for (size_t i = 0; i < cok_cast_table_count; ++i) {
        const cok_ds_table *t = &cok_cast_tables[i];
        for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    }
}

int main(void)
{
    cok_keyboard keyboard = {scripted, &s, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .delay = on_delay, .context = &s};
    if (!cok_adventure_open(&game, "Assets", &keyboard, &hooks)) {
        printf("spell tests skipped: %s\n", game.error);
        return 0;
    }
    test_tables();
    load_screen();
    test_self();
    test_redrawn();
    test_area();
    test_picks();
    test_silence();
    test_weighed();
    test_computer();
    test_flight();
    test_row23();
    test_range();
    test_delayed();
    test_item();
    test_damage();
    test_touch();
    test_sleep();
    test_confusion();
    test_door();
    test_fear();
    test_bolt();
    test_others();
    test_empty();
    test_slow();
    test_wounded();
    test_reflect();
    test_dispelling();
    test_reachable();
    test_more();
    test_wounded_more();
    test_turned();
    test_weights();
    test_hold_more();
    test_small();
    test_poison_fumble();
    test_cones();
    test_bolt_more();
    cok_adventure_close(&game);
    puts("spell tests passed");
    return 0;
}
