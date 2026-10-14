#include "cloud.h"

#include "arena.h"
#include "cast.h"
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

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g;
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "%s: %s;", kind, text);
}

#define LOGGED(text) (strstr(s.log, text) != NULL)

/* The waits of 100 ms: a text's pause at speed 1. */
static unsigned pauses;

/* At the first wait of 50 ms (a lightning missile's step) once sampled
 * is cleared: a pixel of the missile's first picture. */
static bool sampled = true;
static uint8_t sample_missile;

static uint8_t pixel(const cok_picture *p, size_t frame, int x, int y)
{
    uint8_t b = p->pixels[frame * p->frame_size + (size_t)y * p->units * 4 + (size_t)x / 2];
    return x % 2 == 0 ? b >> 4 : b & 15;
}

static void on_delay(cok_adventure *g, unsigned ms, void *context)
{
    (void)context;
    if (ms == 100) ++pauses;
    if (ms == 50 && !sampled) {
        sampled = true;
        sample_missile = pixel(&g->combat.flash, 0, 12, 12);
    }
}

static void dump(void)
{
    if (getenv("CLOUD_LOG") != NULL) fprintf(stderr, "%s\n%s\n", s.log, game.error);
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
    game.battle_sp = COK_BATTLE_SP;
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

#define T(i) game.spell_targets[i]
#define N game.spell_target_count

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

/* The bodies' table: a body of c's at x, y over terrain cell. */
static void body(cok_character *c, int8_t x, int8_t y, uint8_t cell)
{
    cok_combat *k = &game.combat;
    cok_combat_body *b = &k->body[k->bodies++];
    b->character = c;
    b->x = x;
    b->y = y;
    b->cell = cell;
    k->cells[y][x] = COK_COMBAT_BODY;
}

/* Lay Stinking Cloud for c at x, y, as 1415 runs its handler. */
static bool lay(cok_character *c, int8_t x, int8_t y)
{
    game.vm.character = c->record;
    game.spell_x = x;
    game.spell_y = y;
    game.effects.rolls.spell = 0x22;
    bool ok = cok_cloud_stinking(&game);
    game.effects.rolls.spell = 0;
    game.effects.rolls.images = 0;
    return ok && game.vm.status == COK_ECL_OK;
}

#define CELL(x, y) game.combat.cells[y][x]

/* The seed after two rolls, a d20 and a d4. */
static uint32_t after_save(uint32_t seed)
{
    cok_tp_random(&seed, 20);
    cok_tp_random(&seed, 4);
    return seed;
}

/* Stinking Cloud (5b04:29de): a node at the spell's cell with its four
 * cells, the caster's effect 0x28, the terrain kept under each, the text,
 * and each combatant in it throwing once. */
static void test_lay(void)
{
    reset();
    cok_character *m = record('M', 1, 20, 10), *a = record('A', 0, 10, 10),
                  *b = record('B', 0, 11, 10);
    cok_character *d = record('D', 0, 30, 3);
    m->record[0xfe] = 5; /* a magic-user of level 5 */
    CELL(11, 11) = 0x01; /* a wall */
    body(d, 10, 11, 0x30);
    /* A fails (d20 1) and chokes, B saves (d20 20) and coughs. */
    SEED(4, 20, 1, 1, 4, 1, 1, 20, 20, 20, 4, 3, 3);
    CHECK(lay(m, 10, 10));
    const cok_combat_cloud *p = game.combat.clouds;
    CHECK(p != NULL && p->next == NULL && game.combat.pools == NULL);
    CHECK(p->caster == m->record && p->x == 10 && p->y == 10 && p->index == 0 && !p->resisted);
    CHECK(p->covered[1] && p->covered[2] && !p->covered[3] && p->covered[4]);
    CHECK(p->saved[1] == COK_COMBAT_FLOOR && p->saved[2] == COK_COMBAT_FLOOR &&
          p->saved[3] == 0x01 && p->saved[4] == 0x30);
    CHECK(CELL(10, 10) == 0x1e && CELL(11, 10) == 0x1e && CELL(11, 11) == 0x01 &&
          CELL(10, 11) == 0x1e);
    cok_effect *e = cok_character_find_effect(m, 0x28);
    CHECK(e != NULL && e->duration == 5 && e->value == 5 && e->on_remove);
    CHECK(LOGGED("print: M;print: Creates a noxious cloud;"));
    CHECK(LOGGED("print: A;print: chokes and gags from nausea;"));
    CHECK(LOGGED("print: B;print: starts to cough;") && LOGGED("print: B;print: is coughing;"));
    cok_effect *ea = cok_character_find_effect(a, 0x1f), *eb = cok_character_find_effect(b, 0x1e);
    CHECK(ea != NULL && ea->duration == 2 && ea->value == 0xff && !ea->on_remove);
    CHECK(eb != NULL && eb->duration == 4 && eb->value == 0xff);
    /* 0x1f's handler ends A's turn; 0x1e's takes B's casting. */
    CHECK(a->combat->initiative == 0 && b->combat->may_cast == 0 && b->combat->may_use == 0);
    CHECK(game.vm.character == m->record && game.effects.rolls.images == 0);
    /* A second cloud of M's counts none before it: the list's last node is
     * never looked at. Over the first's cell (11, 10) it keeps what the
     * first kept there. Those in both already have an effect: A (0x1f)
     * throws no more; B throws again (d20 1: chokes). */
    s.log[0] = '\0';
    SEED(2, 20, 1, 1, 4, 1, 1);
    CHECK(lay(m, 11, 9));
    const cok_combat_cloud *q = p->next;
    CHECK(q != NULL && q->index == 0 && q->saved[3] == COK_COMBAT_FLOOR);
    CHECK(q->saved[4] == COK_COMBAT_FLOOR && CELL(11, 9) == 0x1e && CELL(12, 10) == 0x1e);
    CHECK(count("chokes") == 1 && LOGGED("print: B;print: chokes"));
    CHECK(cok_character_find_effect(m, 0x28)->next != NULL);
    /* A third counts the first: index 1, value 0x15. */
    CHECK(lay(m, 30, 20) && q->next != NULL && q->next->index == 1);
    e = m->effects;
    while (e->next != NULL) e = e->next;
    CHECK(e->id == 0x28 && e->value == 0x15);
}

/* 60f4:0dc3 and 0c95: who throws, the cloud's caster selected while its
 * effect is added, the puddle's 1d6, and the caster found with the cells'
 * flags read one cell along. */
static void test_square(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 20, 20),
                  *u = record('U', 1, 11, 10), *v = record('V', 1, 10, 11);
    CELL(10, 10) = CELL(11, 10) = CELL(10, 11) = 0x1e;
    u->record[0xda] = 1; /* undead */
    CHECK(cok_character_add_effect(v, 0x63, 0, 0, false) != NULL);
    /* No cloud in the list: the caster is the list's first record. */
    CHECK(cok_cloud_caster(&game, 10, 10) == a->record);
    /* Not undead, neither 0x1f nor 0x63: a d20 (made: 0x1e), a d4. */
    game.vm.character = a->record;
    SEED(2, 20, 20, 20, 4, 4, 4);
    uint32_t start = game.vm.seed;
    CHECK(cok_cloud_square(&game, a, true) && game.vm.character == a->record);
    CHECK(cok_character_find_effect(a, 0x1e)->duration == 5);
    CHECK(game.vm.seed == after_save(start));
    /* The undead and 0x63 throw nothing; nor any without the flag. */
    start = game.vm.seed;
    CHECK(cok_cloud_square(&game, u, true) && cok_cloud_square(&game, v, true));
    CHECK(cok_cloud_square(&game, a, false) && game.vm.seed == start);
    CHECK(u->effects == NULL && !LOGGED("print: U") && !LOGGED("print: V"));
    /* One that cannot act, nothing; with 0x1f, nothing either. */
    a->record[0x189] = 0;
    CHECK(cok_cloud_square(&game, a, true) && game.vm.seed == start);
    a->record[0x189] = 1;
    CHECK(cok_character_add_effect(a, 0x1f, 0, 0xff, false) != NULL);
    CHECK(cok_cloud_square(&game, a, true) && game.vm.seed == start);
    /* A puddle: 1d6 with or without the flag, no save, of the damage type
     * left (cold here: "from Cold"); with no other cloud cell. */
    CELL(11, 10) = 0x1d;
    game.effects.rolls.damage_type = 2;
    SEED(1, 6, 4, 4);
    CHECK(cok_cloud_square(&game, u, false) && u->record[0x197] == 6);
    CHECK(LOGGED("print: U;print: takes 4 points of damage from Cold"));
    /* The cloud's caster, found by position: a cloud at 30, 5 whose own
     * cell is a wall (not covered). 0c95 matches cell k's flag with the
     * direction of cell k + 1: the uncovered wall's cell finds it (by cell
     * 4's flag), the cell east of it does not (by cell 1's), the cell
     * south does (by cell 3's). */
    CELL(30, 5) = 0x01;
    CHECK(lay(m, 30, 5));
    const cok_combat_cloud *p = game.combat.clouds;
    CHECK(!p->covered[1] && p->covered[2] && p->covered[3] && p->covered[4]);
    CHECK(cok_cloud_caster(&game, 30, 5) == m->record);
    CHECK(cok_cloud_caster(&game, 31, 5) == a->record);
    CHECK(cok_cloud_caster(&game, 30, 6) == m->record);
    CHECK(cok_cloud_caster(&game, 31, 6) == m->record);
}

/* While a cloud's effect is added, the selected character is the cloud's
 * caster (60f4:0c95): its caster level sets a resistance's chance. A, of
 * magic resistance 50, against M's cloud (M a magic-user of level 1:
 * 50 + 5 * 10, all) resists a d100 of 80; against the first record's
 * (a fighter, level 6: 75) it would not. */
static void test_caster_selected(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 20, 20);
    m->record[0xfe] = 1;
    a->record[0x187] = 50;
    a->record[0x189] = 0; /* not in the cloud as it is laid */
    CHECK(lay(m, 9, 9) && CELL(10, 10) == 0x1e && a->effects == NULL);
    a->record[0x189] = 1;
    game.vm.character = a->record;
    game.effects.rolls.spell = 0x22;
    game.effects.rolls.amount = 0;
    SEED(3, 20, 20, 20, 4, 1, 1, 100, 80, 80);
    CHECK(cok_cloud_square(&game, a, true) && LOGGED("print: A;print: is Unaffected;"));
    CHECK(cok_character_find_effect(a, 0x1e) == NULL && game.vm.character == a->record);
    /* Off the cloud's cells (a green cloud poked at 12, 12), the first
     * record: A itself, a fighter. */
    s.log[0] = '\0';
    a->record[0x187] = 0;
    CELL(12, 12) = 0x1e;
    game.combat.combatant[1].x = 12;
    game.combat.combatant[1].y = 12;
    CHECK(cok_combat_occupy(&game.combat));
    a->record[0x187] = 50;
    SEED(3, 20, 20, 20, 4, 1, 1, 100, 80, 80);
    CHECK(cok_cloud_square(&game, a, true) && LOGGED("print: A;print: starts to cough;"));
    game.effects.rolls.spell = 0;
}

/* A node made by hand in the list of clouds, at x, y, covering cells
 * k 1-4 where covered[k], with what it kept. */
static cok_combat_cloud *node(cok_character *c, int8_t x, int8_t y, const bool covered[5],
                              const uint8_t saved[5])
{
    cok_combat_cloud *p = calloc(1, sizeof *p), **at = &game.combat.clouds;
    CHECK(p != NULL);
    p->caster = c->record;
    p->x = x;
    p->y = y;
    memcpy(p->covered, covered, 5);
    memcpy(p->saved, saved, 5);
    while (*at != NULL) at = &(*at)->next;
    *at = p;
    return p;
}

/* More of laying: the index counts the caster's own clouds only; a cell
 * already green keeps the terrain the last other cloud covering it kept,
 * unless that is green too; a body's cell by x and y; a cell off the map
 * not covered; one combatant on several cells throws once; the screen
 * centred on the spell's cell whatever the margin. */
static void test_lay_more(void)
{
    reset();
    cok_character *m = record('M', 1, 30, 20), *n = record('N', 1, 31, 20);
    CHECK(lay(n, 5, 5) && lay(m, 20, 5) && lay(m, 25, 5));
    CHECK(game.combat.clouds->next->next->index == 0);
    /* Green at 10, 10 under three of N's made by hand: one covering it
     * that kept 0x30, one not covering it that kept 0x31, then one
     * covering it that kept green: 0x30. M's index counts its own two. */
    const bool none[5] = {false}, all[5] = {false, true, true, true, true};
    const uint8_t kept31[5] = {0, 0x31, 0x31, 0x31, 0x31}, kept30[5] = {0, 0x30, 0x30, 0x30, 0x30},
                  green[5] = {0, 0x1e, 0x1e, 0x1e, 0x1e};
    node(n, 10, 10, all, kept30);
    node(n, 10, 10, none, kept31);
    node(n, 10, 10, all, green);
    CELL(10, 10) = 0x1e;
    CHECK(lay(m, 10, 10));
    cok_combat_cloud *p = game.combat.clouds;
    while (p->next != NULL) p = p->next;
    CHECK(p->saved[1] == 0x30 && p->index == 2);
    /* Bodies at 15, 10 and 15, 12: the cloud at 15, 10 keeps the first's. */
    reset();
    m = record('M', 1, 30, 20);
    cok_character *d = record('D', 0, 40, 20);
    body(d, 15, 10, 0x30);
    body(d, 15, 12, 0x31);
    CHECK(lay(m, 15, 10) && game.combat.clouds->saved[1] == 0x30);
    /* At the east edge: cell 2 (50, 5) is off the map, not covered, and
     * the next row's first cell is not written. */
    CHECK(lay(m, 49, 5));
    p = game.combat.clouds->next;
    CHECK(!p->covered[2] && !p->covered[3] && p->covered[1] && p->covered[4]);
    CHECK(CELL(0, 6) == COK_COMBAT_FLOOR && CELL(0, 7) == COK_COMBAT_FLOOR);
    /* A dragon two cells across, in two of the cells, throws once. */
    reset();
    m = record('M', 1, 30, 20);
    cok_character *w = record('W', 0, 10, 10);
    w->record[0xcf] = 3;
    game.combat.combatant[2].size = 3;
    CHECK(cok_combat_occupy(&game.combat));
    s.log[0] = '\0';
    CHECK(lay(m, 10, 10));
    CHECK(count("print: W;print: starts to cough;") + count("print: W;print: chokes") == 1);
    /* The view (5, 5: its centre 8, 8) moves to centre 10, 10 though it is
     * within 3 of the centre. */
    game.combat.view_x = game.combat.view_y = 5;
    CHECK(lay(m, 10, 10) && game.combat.view_x == 7 && game.combat.view_y == 7);
}

/* While a cloud is laid, the spell takes no mirror image (DS:6b39): a
 * dragon two cells across, one of its cells in the cloud and one on a
 * puddle, takes the puddle's damage and keeps its images. */
static void test_images(void)
{
    for (uint32_t seed = 1; seed < 20; ++seed) {
        reset();
        cok_character *m = record('M', 1, 30, 20), *w = record('W', 0, 10, 10);
        w->record[0xcf] = 3;
        w->record[0x197] = 100;
        game.combat.combatant[2].size = 3;
        CHECK(cok_combat_occupy(&game.combat));
        CELL(10, 10) = 0x1d;
        CHECK(cok_character_add_effect(w, 0x1c, 0, 0x40, false) != NULL);
        game.vm.seed = seed;
        CHECK(lay(m, 11, 10) && LOGGED("print: W;print: takes") && !LOGGED("lost an image"));
    }
}

/* Effect 0x28's end (3f44:0ff7): the first cloud of its caster's with the
 * value's index clears: the text, the terrain back (the body's tile where
 * a body lies), the node freed, the other clouds' cells and the pools'
 * covered again; the screen not redrawn. */
static void test_clear(void)
{
    reset();
    cok_character *m = record('M', 1, 20, 20), *d = record('D', 0, 40, 20);
    body(d, 11, 10, 0x30);
    CHECK(lay(m, 10, 10));
    CHECK(CELL(11, 10) == 0x1e && game.combat.clouds->saved[2] == 0x30);
    /* A second, overlapping at 11, 10 and 12, 10, and one of N's. */
    CHECK(lay(m, 11, 9));
    cok_character *n = record('N', 1, 30, 20);
    CHECK(lay(n, 40, 5));
    /* A pool over the first's cell 10, 11. */
    cok_combat_cloud *pool = calloc(1, sizeof *pool);
    CHECK(pool != NULL);
    pool->caster = n->record;
    pool->x = 10;
    pool->y = 11;
    pool->saved[1] = 0x1e;
    game.combat.pools = pool;
    CELL(10, 11) = 0x1d;
    /* M's first 0x28 (value index 0): the first cloud clears. */
    s.log[0] = '\0';
    unsigned tiles = game.combat.tiles_drawn;
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x28));
    CHECK(LOGGED("print: The air clears a little...;"));
    const cok_combat_cloud *p = game.combat.clouds;
    CHECK(p != NULL && p->x == 11 && p->y == 9 && p->next != NULL && p->next->caster == n->record);
    CHECK(CELL(10, 10) == COK_COMBAT_FLOOR && CELL(11, 11) == COK_COMBAT_FLOOR);
    /* 11, 10: the body's tile (it lies there); the second cloud then
     * covers it again; 10, 11 the pool's. */
    CHECK(CELL(11, 10) == 0x1e && CELL(10, 11) == 0x1d && game.combat.tiles_drawn == tiles);
    /* M's second 0x28 (index 0 too): the second cloud. Its cell 11, 10
     * kept the first's (the body's 0x30); a body lies there: 0x1f. */
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x28));
    CHECK(game.combat.clouds->caster == n->record && game.combat.clouds->next == NULL);
    CHECK(CELL(11, 10) == COK_COMBAT_BODY && CELL(11, 9) == COK_COMBAT_FLOOR);
    /* The body taken back (6beb:10f3 clears its entry, the tile stays):
     * the tile is the terrain kept. */
    memset(&game.combat.body[0], 0, sizeof game.combat.body[0]);
    cok_effect *e = cok_character_add_effect(n, 0x28, 0, 0, true);
    CHECK(e != NULL && lay(n, 11, 10) && game.combat.clouds->next->saved[1] == COK_COMBAT_BODY);
    /* N's first 0x28 is 40, 5's (index 0); its next (index 0 too, the
     * last node never counted), the one at 11, 10. */
    CHECK(cok_effects_remove(&game.effects, n, NULL, 0x28) && CELL(40, 5) == COK_COMBAT_FLOOR);
    CHECK(cok_effects_remove(&game.effects, n, NULL, 0x28));
    CHECK(CELL(11, 10) == COK_COMBAT_BODY && CELL(12, 10) == COK_COMBAT_FLOOR);
    /* One of no cloud (index 3): nothing, no text. */
    s.log[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, n, NULL, 0x28) && !LOGGED("clears"));
    CHECK(cok_character_add_effect(m, 0x28, 0, 0x30, true) != NULL);
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x28) && !LOGGED("clears"));
}

/* Effect 0x46 at death (3f44:2037): a pool at the dead one's cell, effect
 * 0x50 for 1d6 minutes valued its index, the text; 0x50's end (2cad):
 * the text, the terrain back. */
static void test_acid(void)
{
    reset();
    cok_character *a = record('A', 0, 9, 10), *k = record('K', 1, 10, 10);
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    /* Alive and able to act: nothing. */
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d) && game.combat.pools == NULL);
    /* Killed by 12 points of damage (its 10 hit points): it goes down,
     * then dissolves (d6 of 3: 3 minutes). */
    game.effects.rolls.damage_type = 0;
    SEED(1, 6, 3, 3);
    CHECK(cok_cast_damage(&game, k, 12, 0, false) && game.vm.status == COK_ECL_OK);
    const cok_combat_cloud *p = game.combat.pools;
    CHECK(p != NULL && p->caster == k->record && p->x == 10 && p->y == 10 && p->index == 0);
    CHECK(p->saved[1] == COK_COMBAT_FLOOR && CELL(10, 10) == 0x1d);
    cok_effect *e = cok_character_find_effect(k, 0x50);
    CHECK(e != NULL && e->duration == 3 && e->value == 0 && e->on_remove);
    CHECK(LOGGED("print: K;print: is killed;") || LOGGED("print: K;print: Goes Down"));
    CHECK(LOGGED("print: K;print: dissolves into acid!;"));
    /* Over a cloud: the cloud's kept terrain; a second pool of K's counts
     * none (the last node never counted). */
    cok_character *k2 = record('L', 1, 12, 10);
    k2->record[0x197] = 0;
    k2->record[0x189] = 0;
    CELL(12, 10) = 0x30;
    cok_character *m = record('M', 1, 30, 20);
    CHECK(lay(m, 12, 9));
    CHECK(cok_character_add_effect(k2, 0x46, 0, 0xff, false) != NULL);
    SEED(1, 6, 6, 6);
    CHECK(cok_effects_dispatch(&game.effects, k2, 0x0d));
    CHECK(p->next != NULL && p->next->saved[1] == 0x30 && CELL(12, 10) == 0x1d);
    /* 0x50 ends: K's pool drains to floor; the clouds and pools are
     * covered again. */
    s.log[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, k, NULL, 0x50));
    CHECK(LOGGED("print: The acid drains away..;") && CELL(10, 10) == COK_COMBAT_FLOOR);
    CHECK(game.combat.pools != NULL && game.combat.pools->caster == k2->record);
    CHECK(CELL(12, 10) == 0x1d && CELL(12, 9) == 0x1e);
    /* L's pool drains to what the cloud kept, then the cloud covers it. */
    CHECK(cok_effects_remove(&game.effects, k2, NULL, 0x50) && game.combat.pools == NULL);
    CHECK(CELL(12, 10) == 0x1e);
    /* A value matching no pool: nothing. */
    s.log[0] = '\0';
    CHECK(cok_character_add_effect(k, 0x50, 0, 7, true) != NULL);
    CHECK(cok_effects_remove(&game.effects, k, NULL, 0x50) && !LOGGED("drains"));
    (void)a;
}

/* 2037 over a body's tile reads a byte of the stack never set as a
 * direction from the spell's cell: the bodies give the same terrain for
 * every byte, or the pool's is unknown, which stops the run only if it is
 * put back where no body lies. */
static void test_acid_body(void)
{
    reset();
    cok_character *d = record('D', 0, 40, 20), *k = record('K', 1, 10, 10);
    body(d, 10, 10, 0x30);
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    k->record[0x197] = 0;
    k->record[0x189] = 0;
    /* The spell's cell far from the body: no byte reaches it. */
    game.spell_x = 40;
    game.spell_y = 2;
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d));
    CHECK(game.combat.pools->saved[1] == COK_COMBAT_BODY && !game.combat.pools->unknown);
    CHECK(cok_effects_remove(&game.effects, k, NULL, 0x50) && CELL(10, 10) == COK_COMBAT_BODY);
    /* The spell's cell beside the body: byte 4 (south) reaches it, others
     * not: unknown. Drained with the body there, its tile; */
    game.spell_x = 10;
    game.spell_y = 9;
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d) && game.combat.pools->unknown);
    CHECK(cok_effects_remove(&game.effects, k, NULL, 0x50) && CELL(10, 10) == COK_COMBAT_BODY);
    /* the body gone, a stop. */
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d) && game.combat.pools->unknown);
    memset(&game.combat.body[0], 0, sizeof game.combat.body[0]);
    CHECK(!cok_effects_remove(&game.effects, k, NULL, 0x50));
    cok_adventure_effect_failed(&game);
    CHECK(game.vm.status != COK_ECL_OK && strstr(game.error, "3f44:22a0") != NULL);
}

/* Dispel Magic's part for the clouds (5b04:3352): the cells around the
 * spell's, each a green cloud tried against each cloud with a cell there
 * that never resisted; a d100 up to 50, + 5 a level above the cloud's
 * caster's magic-user level (and former one, by whether the dispeller may
 * use its former class), - 2 a level below. A cloud that resists is never
 * tried again; one taken away is read on from, freed: a stop. */
static void test_dispel(void)
{
    reset();
    cok_character *c = record('C', 0, 30, 20), *m = record('M', 1, 31, 20);
    m->record[0xfe] = 4;
    CHECK(lay(m, 10, 10));
    game.vm.character = c->record;
    /* Level 5 against 4: 55. A d100 of 56 resists, no other d100 rolled
     * though the cloud has three more cells about the spell's cell. */
    game.spell_x = 10;
    game.spell_y = 10;
    SEED(1, 100, 56, 56);
    uint32_t start = game.vm.seed;
    CHECK(cok_cloud_dispel(&game, 5) && game.combat.clouds->resisted == 1);
    cok_tp_random(&start, 100);
    CHECK(game.vm.seed == start && CELL(10, 10) == 0x1e);
    CHECK(cok_cloud_dispel(&game, 20) && game.vm.seed == start);
    /* A fresh cloud: level 3 against 4 is 48; 48 takes it away (the text,
     * the cells), then the freed cloud's next is read: a stop. */
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x28) && game.combat.clouds == NULL);
    CHECK(lay(m, 10, 10));
    game.vm.character = c->record;
    s.log[0] = '\0';
    SEED(1, 100, 48, 48);
    CHECK(!cok_cloud_dispel(&game, 3) && strstr(game.error, "5b04:354a") != NULL);
    CHECK(LOGGED("The air clears a little") && CELL(10, 10) == COK_COMBAT_FLOOR);
    /* The cloud's level adds the former magic-user level when the
     * dispeller (not the caster) may use its former class: C may (a human
     * whose first class's level is above +0xd7), M may not: level 4 + 3 =
     * 7 against 7 is 50, and a d100 of 60 resists (by M's: 65). */
    reset();
    c = record('C', 0, 30, 20);
    m = record('M', 1, 31, 20);
    m->record[0xfe] = 4;
    m->record[0x106] = 3;
    m->record[0xd7] = 5;
    CHECK(cok_character_former_class(c->record) && !cok_character_former_class(m->record));
    CHECK(lay(m, 10, 10));
    game.vm.character = c->record;
    SEED(1, 100, 60, 60);
    CHECK(cok_cloud_dispel(&game, 7) && game.combat.clouds->resisted == 1);
    /* Outside combat no map: nothing. */
    game.combat.active = false;
    start = game.vm.seed;
    CHECK(cok_cloud_dispel(&game, 1) && game.vm.seed == start);
}

/* More of the ends: a body taken back lies nowhere, though its entry's
 * cell is 0, 0; a pool drains to a body's tile where one fell on it since;
 * the pool ended is the one of the effect's value (a third pool of K's has
 * index 1); the screen centred on one that dissolves. */
static void test_ends_more(void)
{
    reset();
    cok_character *m = record('M', 1, 30, 20), *d = record('D', 0, 40, 20);
    body(d, 0, 0, 0x30);
    memset(&game.combat.body[0], 0, sizeof game.combat.body[0]);
    CELL(0, 0) = COK_COMBAT_FLOOR;
    CHECK(lay(m, 0, 0) && cok_effects_remove(&game.effects, m, NULL, 0x28));
    CHECK(CELL(0, 0) == COK_COMBAT_FLOOR);
    /* M's 0x28 clears M's cloud, not N's laid before it, of the same
     * index. */
    cok_character *n = record('N', 1, 31, 20);
    CHECK(lay(n, 20, 5) && lay(m, 25, 5));
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x28));
    CHECK(game.combat.clouds->caster == n->record && game.combat.clouds->next == NULL);
    reset();
    cok_character *k = record('K', 1, 10, 10);
    d = record('D', 0, 40, 20);
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    k->record[0x197] = 0;
    k->record[0x189] = 0;
    game.combat.view_x = game.combat.view_y = 5;
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d));
    CHECK(game.combat.view_x == 7 && game.combat.view_y == 7);
    game.combat.bodies = 0;
    body(d, 10, 10, 0x30);
    CELL(10, 10) = 0x1d;
    CHECK(cok_effects_remove(&game.effects, k, NULL, 0x50) && CELL(10, 10) == COK_COMBAT_BODY);
    reset();
    k = record('K', 1, 10, 10);
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    k->record[0x197] = 0;
    k->record[0x189] = 0;
    for (int i = 0; i < 3; ++i) CHECK(cok_effects_dispatch(&game.effects, k, 0x0d));
    cok_combat_cloud *first = game.combat.pools, *third = first->next->next;
    CHECK(third->index == 1);
    cok_effect *e = k->effects;
    while (e != NULL && !(e->id == 0x50 && e->value == 1)) e = e->next;
    CHECK(e != NULL && cok_effects_remove(&game.effects, k, e, 0x50));
    CHECK(game.combat.pools == first && first->next->next == NULL);
}

/* More of Dispel Magic's part: 55 at 5 levels against 4 takes a cloud
 * away with a d100 of 55; the walk's south-east cell (a cloud at 11, 11
 * whose other cells are walls) is tried; a puddle over a cloud's cell is
 * not; the screen centred on the spell's cell only beyond 3 of the view's
 * centre. */
static void test_dispel_more(void)
{
    reset();
    cok_character *c = record('C', 0, 30, 20), *m = record('M', 1, 31, 20);
    m->record[0xfe] = 4;
    CHECK(lay(m, 10, 10));
    game.vm.character = c->record;
    game.spell_x = game.spell_y = 10;
    SEED(1, 100, 55, 55);
    CHECK(!cok_cloud_dispel(&game, 5) && strstr(game.error, "5b04:354a") != NULL);
    reset();
    c = record('C', 0, 30, 20);
    m = record('M', 1, 31, 20);
    CELL(12, 11) = CELL(12, 12) = CELL(11, 12) = 0x01;
    CHECK(lay(m, 11, 11) && game.combat.clouds->covered[1] && !game.combat.clouds->covered[4]);
    game.vm.character = c->record;
    game.spell_x = game.spell_y = 10;
    game.combat.view_x = game.combat.view_y = 5;
    SEED(1, 100, 100, 100);
    uint32_t start = game.vm.seed;
    CHECK(cok_cloud_dispel(&game, 1) && game.combat.clouds->resisted == 1);
    CHECK(game.vm.seed != start && game.combat.view_x == 5 && game.combat.view_y == 5);
    reset();
    c = record('C', 0, 30, 20);
    m = record('M', 1, 31, 20);
    CELL(11, 10) = CELL(11, 11) = CELL(10, 11) = 0x01;
    CHECK(lay(m, 10, 10));
    CELL(10, 10) = 0x1d;
    game.vm.character = c->record;
    start = game.vm.seed;
    CHECK(cok_cloud_dispel(&game, 1) && game.vm.seed == start && !game.combat.clouds->resisted);
}

static bool breathe(cok_character *c);

/* The review's cases. A cell under a cloud, then a pool, then a second
 * cloud keeps the puddle (the last cloud covering it that kept no green
 * cloud); a third laid there keeps the puddle too. */
static void test_under_last(void)
{
    reset();
    cok_character *m = record('M', 1, 30, 20), *k = record('K', 1, 10, 10);
    CHECK(lay(m, 10, 10) && game.combat.clouds->saved[1] == COK_COMBAT_FLOOR);
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    k->record[0x197] = 0;
    k->record[0x189] = 0;
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d) && CELL(10, 10) == 0x1d);
    CHECK(game.combat.pools->saved[1] == COK_COMBAT_FLOOR);
    CHECK(lay(m, 10, 10) && game.combat.clouds->next->saved[1] == 0x1d);
    CHECK(lay(m, 10, 10) && game.combat.clouds->next->next->saved[1] == 0x1d);
}

/* Clearing a cloud at the map's east or south edge writes back only its
 * covered cells: the next row's first cell is untouched, and nothing is
 * written past the map. */
static void test_clear_edge(void)
{
    reset();
    cok_character *m = record('M', 1, 30, 20);
    CELL(0, 11) = 0x30;
    CHECK(lay(m, 49, 10) && cok_effects_remove(&game.effects, m, NULL, 0x28));
    CHECK(CELL(0, 11) == 0x30 && CELL(0, 12) == COK_COMBAT_FLOOR && CELL(49, 10) == 0x17);
    CHECK(lay(m, 10, 24) && cok_effects_remove(&game.effects, m, NULL, 0x28));
    CHECK(game.vm.status == COK_ECL_OK && CELL(10, 24) == COK_COMBAT_FLOOR);
}

/* The cloud's caster is found at the victim's x, y (12, 10), not y, x
 * (10, 12, under no cloud, which would give the first record): A, of
 * magic resistance 50, resists M's level of 1 (all, 100) with a d100 of
 * 80, but not its own as a fighter of level 6 (75). */
static void test_caster_xy(void)
{
    reset();
    cok_character *a = record('A', 0, 12, 10), *m = record('M', 1, 20, 20);
    m->record[0xfe] = 1;
    a->record[0x187] = 50;
    a->record[0x189] = 0;
    CHECK(lay(m, 11, 9) && CELL(12, 10) == 0x1e && CELL(10, 12) == COK_COMBAT_FLOOR);
    a->record[0x189] = 1;
    game.vm.character = a->record;
    game.effects.rolls.spell = 0x22;
    game.effects.rolls.amount = 0;
    SEED(3, 20, 20, 20, 4, 1, 1, 100, 80, 80);
    CHECK(cok_cloud_square(&game, a, true) && LOGGED("print: A;print: is Unaffected;"));
    game.effects.rolls.spell = 0;
}

/* A puddle's 1d6 counts as one die (DS:6b34): with fire left in DS:6b31
 * (and magic, 0x29) and 5 dice counted before, one with fire resistance
 * (0x3d) takes 6 - 2 = 4, not 5 (a point a die at least). */
static void test_puddle_dice(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    a->record[0x197] = 50;
    CHECK(cok_character_add_effect(a, 0x3d, 0, 0xff, false) != NULL);
    CELL(10, 10) = 0x1d;
    game.effects.rolls.damage_type = 0x29;
    game.effects.rolls.dice = 5;
    SEED(1, 6, 6, 6);
    CHECK(cok_cloud_square(&game, a, false) && a->record[0x197] == 46);
}

/* A cloud whose caster has no 0x28 of its index left (its two clouds both
 * index 0: removing the first 0x28 cleared the first, the second clears
 * the other) is not marked resisted when the d100 is won: nothing is
 * removed. */
static void test_dispel_orphan(void)
{
    reset();
    cok_character *c = record('C', 0, 30, 20), *m = record('M', 1, 31, 20);
    CHECK(lay(m, 10, 10));
    cok_character_find_effect(m, 0x28)->value = 0xf1; /* index 15: none */
    game.vm.character = c->record;
    game.spell_x = game.spell_y = 10;
    SEED(1, 100, 1, 1);
    uint32_t start = game.vm.seed;
    CHECK(cok_cloud_dispel(&game, 9) && game.vm.seed != start);
    CHECK(game.combat.clouds != NULL && game.combat.clouds->resisted == 0);
    CHECK(CELL(10, 10) == 0x1e);
}

/* The aurak's use goes only when it breathes: with no one beside it, a
 * second d100 of 25 or less fires bolts and keeps the uses. */
static void test_aurak_keeps_use(void)
{
    reset();
    cok_character *x = record('X', 1, 10, 10), *a = record('A', 0, 13, 10);
    a->record[0x197] = 60;
    cok_effect *e = cok_character_add_effect(x, 4, 0, 2, false);
    game.effects.rolls.round = 1;
    SEED(3, 100, 10, 10, 100, 5, 5, 1, 1, 1);
    CHECK(breathe(x) && LOGGED("Fires An Energy Bolt!") && !LOGGED("Breathes!"));
    CHECK(e->value == 2 && a->record[0x197] < 60);
    game.effects.rolls.round = 0;
}

/* A fireball breathed takes no mirror image (DS:6b39): A, with images,
 * keeps them and takes the fireball's damage. (4dc2 sets the flag at
 * 4dc8, but the pick clears it, 432f:2349, and its area listing sets it
 * again.) */
static void test_fireball_images(void)
{
    for (uint32_t seed = 1; seed < 12; ++seed) {
        reset();
        cok_character *f = record('S', 1, 10, 10), *a = record('A', 0, 13, 10);
        f->record[0x197] = 120;
        a->record[0x197] = 200;
        CHECK(cok_character_add_effect(a, 0x1c, 0, 0x40, false) != NULL);
        CHECK(cok_character_add_effect(f, 0x4e, 0, 0xff, false) != NULL);
        game.vm.seed = seed;
        CHECK(breathe(f) && LOGGED("throws a fireball") && !LOGGED("lost an image"));
        CHECK(a->record[0x197] < 200);
        game.effects.rolls.spell = 0;
    }
}

/* 2037 never reads the terrain table: a pool on a cell of terrain past
 * it (0x50) keeps that and puts it back. */
static void test_acid_terrain(void)
{
    reset();
    cok_character *k = record('K', 1, 10, 10);
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    k->record[0x197] = 0;
    k->record[0x189] = 0;
    CELL(10, 10) = 0x50;
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d) && game.vm.status == COK_ECL_OK);
    CHECK(game.combat.pools != NULL && game.combat.pools->saved[1] == 0x50);
    CHECK(cok_effects_remove(&game.effects, k, NULL, 0x50) && CELL(10, 10) == 0x50);
}

/* Stinking Cloud over a cell of two bodies keeps the last's cell. */
static void test_two_bodies(void)
{
    reset();
    cok_character *m = record('M', 1, 30, 20), *d = record('D', 0, 40, 20);
    body(d, 10, 10, 0x30);
    body(d, 10, 10, 0x31);
    CHECK(lay(m, 10, 10) && game.combat.clouds->saved[1] == 0x31);
}

/* Run c's effects for event 0x0e, as the computer's attack (3afb:0d49)
 * does at the start of c's turn. */
static bool breathe(cok_character *c)
{
    game.vm.character = c->record;
    return cok_effects_dispatch(&game.effects, c, 0x0e) && game.vm.status == COK_ECL_OK;
}

/* 546e, a dragon's cold (0x53) and fire (0x68): 3 uses from the first
 * round, a target as the computer's spell 0x3d picks it, a cone of width 3
 * and length 7 (9 for fire) from the dragon; not with a friend in it. */
static void test_dragon(void)
{
    reset();
    cok_character *w = record('W', 1, 10, 10), *a = record('A', 0, 13, 10),
                  *b = record('B', 0, 16, 10), *c = record('C', 0, 17, 10),
                  *d = record('D', 0, 18, 10);
    w->record[0x62] = 30;
    a->record[0x197] = a->record[0x62] = b->record[0x197] = b->record[0x62] = 50;
    c->record[0x197] = d->record[0x197] = 50;
    /* Saves of type 3 need 12, of type 4 2. */
    for (int i = 0; i < 4; ++i) {
        cok_character *t = i == 0 ? a : i == 1 ? b : i == 2 ? c : d;
        t->record[0xd3] = 12;
        t->record[0xd4] = 2;
    }
    cok_effect *e = cok_character_add_effect(w, 0x53, 0, 0xff, false);
    CHECK(e != NULL);
    /* The pick: d2 of the two within 6, A. A fails (d20 8): 30; B saves
     * (d20 20): 15; C at the cone's end (length 7) is hit, D past it not. */
    SEED(3, 2, 1, 1, 20, 8, 8, 20, 20, 20);
    CHECK(breathe(w) && e->value == 2 && w->combat->initiative == 0);
    CHECK(LOGGED("print: W;print: breathes cold;"));
    CHECK(a->record[0x197] == 20 && b->record[0x197] == 35);
    CHECK(c->record[0x197] < 50 && d->record[0x197] == 50 && N == 3);
    CHECK(T(1) == a && T(2) == b && T(3) == c && game.effects.rolls.damage_type == 0x22);
    /* Past the first round its uses count down; at none it does nothing,
     * and no dice are rolled. */
    game.effects.rolls.round = 1;
    e->value = 0;
    uint32_t start = game.vm.seed;
    w->combat->initiative = 5;
    CHECK(breathe(w) && game.vm.seed == start && w->combat->initiative == 5);
    /* Above 3 they become 3 again, 1 for 6. */
    e->value = 4;
    cok_character *f = record('F', 1, 14, 10); /* a friend of W's in the cone */
    s.log[0] = '\0';
    SEED(1, 2, 1, 1);
    CHECK(breathe(w) && e->value == 3 && !LOGGED("breathes") && a->record[0x197] == 20);
    CHECK(w->combat->initiative == 5);
    e->id = 6;
    e->value = 9;
    f->record[0x18a] = 0; /* on A's side now: no friend */
    SEED(4, 3, 1, 1, 20, 20, 20, 20, 20, 20, 20, 20, 20);
    CHECK(breathe(w) && e->value == 0 && LOGGED("print: W;print: breathes cold;"));
    /* Effect 6's length is 8: D is hit. */
    CHECK(d->record[0x197] < 50);
    /* 0x68: fire, 0x21, length 9. */
    reset();
    w = record('W', 1, 10, 10);
    a = record('A', 0, 13, 10);
    b = record('B', 0, 19, 10);
    c = record('C', 0, 15, 10);
    w->record[0x62] = 30;
    a->record[0x197] = a->record[0x62] = b->record[0x197] = c->record[0x197] = 50;
    CHECK(cok_character_add_effect(c, 0x49, 0, 0xff, false) != NULL);
    e = cok_character_add_effect(w, 0x68, 0, 0xff, false);
    /* B at the end (length 9) is hit; C, fire bit 0x20 and 0x49, is
     * unaffected. */
    SEED(2, 2, 1, 1, 20, 1, 1);
    CHECK(breathe(w) && LOGGED("print: W;print: breathes Fire;") && a->record[0x197] == 20);
    CHECK(game.effects.rolls.damage_type == 0x21 && e->value == 2);
    CHECK(b->record[0x197] < 50 && c->record[0x197] == 50);
    CHECK(LOGGED("print: C;print: is unaffected;"));
}

/* 4ab8, effect 4 (the aurak's): a d100 up to 75 to act; with uses and a
 * d100 up to 25 it breathes on the last one beside it of another side
 * (10, or 20 and blind for good, the turn then over); else up to two
 * energy bolts of 1d8 + 2, a new pick for the second if the first drops. */
static void test_attack_4(void)
{
    reset();
    cok_character *x = record('X', 1, 10, 10), *a = record('A', 0, 11, 10);
    a->record[0x197] = a->record[0x62] = 60;
    cok_effect *e = cok_character_add_effect(x, 4, 0, 0xff, false);
    /* A d100 of 76: nothing, but the uses are 3. */
    SEED(1, 100, 76, 76);
    CHECK(breathe(x) && e->value == 3 && !LOGGED("print: X") && a->record[0x197] == 60);
    /* 75, then 25: it breathes; A saves (20): 10, the turn goes on. */
    e->value = 1;
    game.effects.rolls.round = 1;
    game.effects.rolls.damage_type = 0;
    SEED(3, 100, 75, 75, 100, 25, 25, 20, 20, 20);
    CHECK(breathe(x) && LOGGED("print: X;print: Breathes!;") && a->record[0x197] == 50);
    CHECK(e->value == 0 && x->combat->initiative == 5);
    CHECK(cok_character_find_effect(a, 0x21) == NULL);
    /* No uses left: one d100, then the bolts: the pick (d1), 1d8 + 2 (8),
     * and A still up, a second (5): the turn ends. */
    s.log[0] = '\0';
    SEED(4, 100, 1, 1, 1, 1, 1, 8, 6, 6, 8, 3, 3);
    CHECK(breathe(x) && count("print: X;print: Fires An Energy Bolt!;") == 1);
    CHECK(a->record[0x197] == 37 && x->combat->initiative == 0 && !LOGGED("Breathes"));
    /* With a use, 26: bolts too. */
    e->value = 1;
    x->combat->initiative = 5;
    s.log[0] = '\0';
    SEED(5, 100, 10, 10, 100, 26, 26, 1, 1, 1, 8, 1, 1, 8, 1, 1);
    CHECK(breathe(x) && LOGGED("Fires An Energy Bolt!") && a->record[0x197] == 31 && e->value == 1);
    /* A fails its save: 20 and blinded, X's turn over. */
    x->combat->initiative = 5;
    SEED(3, 100, 10, 10, 100, 2, 2, 20, 1, 1);
    CHECK(breathe(x) && a->record[0x197] == 11 && x->combat->initiative == 0);
    CHECK(cok_character_find_effect(a, 0x21) != NULL && LOGGED("print: A;print: is blinded;"));
    CHECK(cok_character_find_effect(a, 0x21)->duration == 0 && e->value == 0);
    /* The first bolt drops A (3 hit points, 8 + 2): a new pick, B, for the
     * second, which is the last whether or not B stands. */
    cok_character *b = record('B', 0, 14, 10);
    x->combat->initiative = 5;
    a->record[0x197] = 3;
    s.log[0] = '\0';
    SEED(5, 100, 10, 10, 2, 1, 1, 8, 8, 8, 1, 1, 1, 8, 1, 1);
    CHECK(breathe(x) && count("Fires An Energy Bolt!") == 2 && a->record[0x189] == 0);
    CHECK(b->record[0x197] == 7 && x->combat->initiative == 0);
}

/* 4dc2, 0x4e: a fireball at a target the computer's Fireball picks
 * (everyone within 3 of its cell, the thrower too), 15d6 of fire, the
 * spell left Fireball, the effect gone. */
static void test_attack_4e(void)
{
    reset();
    cok_character *f = record('S', 1, 10, 10), *a = record('A', 0, 13, 10);
    f->record[0x197] = 120;
    a->record[0x197] = 50;
    CHECK(cok_character_add_effect(f, 0x4e, 0, 0xff, false) != NULL);
    /* d1 picks A; 15d6; A saves, S does not. */
    SEED(18, 1, 1, 1, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6,
         6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 6, 1, 6, 20, 20, 20, 20, 1, 1);
    uint32_t seed = game.vm.seed;
    cok_tp_random(&seed, 1);
    unsigned d = 0;
    for (int i = 0; i < 15; ++i) d += cok_tp_random(&seed, 6) + 1u;
    CHECK(breathe(f) && LOGGED("print: S;print: throws a fireball;"));
    CHECK(a->record[0x197] == 50 - d / 2 && f->record[0x197] == 120 - d);
    CHECK(game.effects.rolls.spell == 0x2f && cok_character_find_effect(f, 0x4e) == NULL);
    CHECK(f->combat->initiative == 5);
    game.effects.rolls.spell = 0;
}

/* 4eea, 0x54: poison in a burst of 3 at the end of a cone of length 3,
 * either side; the cone's range is the x step of the last line the
 * target routine's listing looked along: 0 (the same column) cuts sight to
 * one cell, and the burst is then at the cell beside the breather. */
static void test_attack_54(void)
{
    for (int column = 0; column < 2; ++column) {
        reset();
        cok_character *p = record('P', 1, 10, 10), *a = record('A', 0, 10, 13),
                      *z = record('Z', 0, 9, 12), *b = record('B', 0, column ? 10 : 12, 15);
        p->record[0x62] = 20;
        a->record[0x197] = b->record[0x197] = z->record[0x197] = 50;
        CHECK(cok_character_add_effect(z, 0x49, 0, 0xff, false) != NULL);
        cok_effect *e = cok_character_add_effect(p, 0x54, 0, 0xff, false);
        /* The pick is A (second nearest after Z, of three within 5, or two
         * with B at 12, 15); A fails, Z is unaffected, B fails. The last
         * line the listing at A's cell looks along is to B. */
        unsigned n = column ? 3 : 2;
        SEED(4, n, 2, 2, 20, 1, 1, 20, 1, 1, 20, 1, 1);
        CHECK(breathe(p) && LOGGED("print: P;print: breathes poison;") && e->value == 2);
        CHECK(a->record[0x197] == 30 && z->record[0x197] == 50);
        CHECK(LOGGED("print: Z;print: is unaffected;") && p->combat->initiative == 0);
        CHECK(b->record[0x197] == (column ? 50 : 30));
        CHECK(game.effects.rolls.damage_type == 0x20);
    }
    /* Length 3: the burst's centre is 3 cells out; B at 8, 6 is within 3
     * of it, not of a cell further. */
    reset();
    cok_character *p = record('P', 1, 10, 10), *a = record('A', 0, 7, 9),
                  *b = record('B', 0, 8, 6);
    p->record[0x62] = 20;
    a->record[0x197] = b->record[0x197] = 50;
    CHECK(cok_character_add_effect(p, 0x54, 0, 0xff, false) != NULL);
    SEED(1, 2, 1, 1);
    CHECK(breathe(p) && a->record[0x197] < 50 && b->record[0x197] < 50);
}

/* 50cf, 0x55: lightning, a target as the computer's Lightning Bolt picks
 * it, its cell and a bolt of 10 on from it, each throwing (type 3) for
 * half of the breather's most hit points. */
static void test_attack_55(void)
{
    reset();
    cok_character *l = record('L', 1, 10, 10), *a = record('A', 0, 13, 10),
                  *b = record('B', 0, 16, 10);
    l->record[0x62] = 40;
    a->record[0x197] = b->record[0x197] = 60;
    a->record[0xd3] = 12;
    a->record[0xd4] = 2;
    cok_effect *e = cok_character_add_effect(l, 0x55, 0, 0xff, false);
    SEED(3, 2, 1, 1, 20, 10, 10, 20, 20, 20);
    CHECK(breathe(l) && LOGGED("print: L;print: Breathes!;") && e->value == 2);
    CHECK(a->record[0x197] == 20 && b->record[0x197] == 40 && l->combat->initiative == 0);
    CHECK(game.effects.rolls.damage_type == 0x24);
}

/* 5227, 0x5a: acid in a line of 6, not with a friend in it. */
static void test_attack_5a(void)
{
    reset();
    cok_character *k = record('K', 1, 10, 10), *a = record('A', 0, 13, 10),
                  *b = record('B', 0, 16, 10);
    k->record[0x62] = 16;
    a->record[0x197] = b->record[0x197] = 30;
    cok_effect *e = cok_character_add_effect(k, 0x5a, 0, 0xff, false);
    SEED(3, 2, 1, 1, 20, 1, 1, 20, 1, 1);
    CHECK(breathe(k) && LOGGED("print: K;print: breathes acid;") && e->value == 2);
    CHECK(a->record[0x197] == 14 && b->record[0x197] == 14 && k->combat->initiative == 0);
    k->combat->initiative = 5;
    cok_character *f = record('F', 1, 14, 10);
    s.log[0] = '\0';
    SEED(1, 2, 1, 1);
    /* (Still the first round: the uses are 3 again.) */
    CHECK(breathe(k) && !LOGGED("breathes") && e->value == 3 && k->combat->initiative == 5);
    CHECK(game.effects.rolls.damage_type == 0x30);
    (void)f;
}

/* The aurak's breath goes to the last listed beside it of another side:
 * of C (9, 10) and A (11, 10) beside, B two cells off never. */
static void test_aurak_target(void)
{
    reset();
    cok_character *x = record('X', 1, 10, 10), *a = record('A', 0, 11, 10),
                  *b = record('B', 0, 12, 10), *c = record('C', 0, 9, 10);
    a->record[0x197] = b->record[0x197] = c->record[0x197] = 60;
    CHECK(cok_character_add_effect(x, 4, 0, 0xff, false) != NULL);
    SEED(3, 100, 1, 1, 100, 1, 1, 20, 20, 20);
    CHECK(breathe(x) && LOGGED("Breathes!") && b->record[0x197] == 60);
    CHECK((a->record[0x197] == 50) != (c->record[0x197] == 50));
    cok_character *hit = a->record[0x197] == 50 ? a : c;
    /* Which: the last of 6b30:08d8's listing. */
    CHECK(cok_combat_list(&game.combat, 10, 10, 1, 0xff, 1));
    uint8_t last = game.combat.listed[game.combat.listed_count].index;
    CHECK(game.combat.combatant[last].character == hit);
    CHECK(game.combat.listed_count == 3);
}

/* 0x4e centres the screen on the thrower with no margin: the view at 7, 7
 * (centre 10, 10) moves to 8, 7 for a thrower at 11, 10, and stays for the
 * fireball at A, within 3. */
static void test_fireball_view(void)
{
    reset();
    cok_character *f = record('S', 1, 11, 10), *a = record('A', 0, 13, 10);
    f->record[0x197] = 120;
    a->record[0x197] = 50;
    CHECK(cok_character_add_effect(f, 0x4e, 0, 0xff, false) != NULL);
    game.combat.view_x = game.combat.view_y = 7;
    SEED(1, 1, 1, 1);
    CHECK(breathe(f) && LOGGED("throws a fireball"));
    CHECK(game.combat.view_x == 8 && game.combat.view_y == 7);
    game.effects.rolls.spell = 0;
}

/* Breath outside a battle: the handler fails (the map and the combat
 * targets are not there). */
static void test_breath_outside(void)
{
    reset();
    cok_character *w = record('W', 1, 10, 10);
    record('A', 0, 13, 10);
    CHECK(cok_character_add_effect(w, 0x53, 0, 0xff, false) != NULL);
    game.effects.in_battle = false;
    game.vm.character = w->record;
    CHECK(!cok_effects_dispatch(&game.effects, w, 0x0e));
    cok_adventure_effect_failed(&game);
    CHECK(strstr(game.error, "breathes outside combat") != NULL);
    game.effects.in_battle = true;
}

/* A breath aimed at its breather's own cell (a target sharing it, which
 * play never makes): the directions its cone then reads are not known. */
static void test_breath_own_cell(void)
{
    reset();
    cok_character *k = record('K', 1, 10, 10);
    record('A', 0, 10, 10);
    CHECK(cok_character_add_effect(k, 0x5a, 0, 0xff, false) != NULL);
    SEED(1, 1, 1, 1);
    CHECK(!breathe(k) && strstr(game.error, "5b04:19a5") != NULL);
}

/* 4eea with no one in the burst: P at 10, 10 picks A at 10, 15, the
 * target routine's last line ran down the column, which cuts the cone to
 * a cell: the burst beside P meets no one. No text, the uses kept, the
 * turn going on (4f62). */
static void test_poison_no_one(void)
{
    reset();
    cok_character *p = record('P', 1, 10, 10), *a = record('A', 0, 10, 15);
    p->record[0x62] = 20;
    a->record[0x197] = 50;
    cok_effect *e = cok_character_add_effect(p, 0x54, 0, 0xff, false);
    SEED(1, 1, 1, 1);
    CHECK(breathe(p) && game.combat.sight_step == 0 && !LOGGED("breathes poison"));
    CHECK(e->value == 3 && p->combat->initiative == 5 && a->record[0x197] == 50);
}

/* 5227 runs its cone only after a pick (5286): with no one in reach (A
 * 10 cells off) the pick leaves the spell's cell 0, 0 and no targets; a
 * cone toward 0, 0 would have taken K's friend F at 8, 8. */
static void test_acid_no_pick(void)
{
    reset();
    cok_character *k = record('K', 1, 10, 10), *a = record('A', 0, 20, 10);
    record('F', 1, 8, 8);
    a->record[0x197] = 30;
    cok_effect *e = cok_character_add_effect(k, 0x5a, 0, 0xff, false);
    SEED(1, 1, 1, 1);
    CHECK(breathe(k) && !LOGGED("breathes acid") && e->value == 3 && a->record[0x197] == 30);
    CHECK(game.spell_x == 0 && game.spell_y == 0 && N == 0);
}

/* 50cf's missile (6346:1b5b, 1ba6) is icon slot 0x13's: pictures made
 * here, slot 0x12 3s, 0x13 4s, clear in their masks. (The breather drawn
 * attacking first, 6346:0af6, is drawn over at once: the missile's
 * centring, margin 0xff, redraws the view and every combatant ready, before
 * any pause.) */
static void test_lightning_shown(void)
{
    reset();
    cok_character *l = record('L', 1, 10, 10), *a = record('A', 0, 13, 10);
    l->record[0x62] = 4;
    a->record[0x197] = 60;
    game.combat.view_x = game.combat.view_y = 7;
    game.combat.show_actions = true;
    CHECK(cok_combat_scroll(&game.combat, 10, 10, 0xff) || true);
    const struct { uint8_t slot, image, colour; } made[] = {
        {0x12, 0, 0x33}, {0x12, 1, 0x33}, {0x13, 0, 0x44}, {0x13, 1, 0x44}};
    cok_picture kept[4];
    for (int i = 0; i < 4; ++i) {
        cok_picture *icon = &game.icons[made[i].slot][made[i].image];
        kept[i] = *icon;
        *icon = (cok_picture){0};
        CHECK(cok_picture_create(icon, 3, 24, 1, 1) == COK_PICTURE_OK);
        memset(icon->pixels, made[i].colour, icon->frame_size);
        memset(icon->mask, 0xff, icon->frame_size);
    }
    CHECK(cok_character_add_effect(l, 0x55, 0, 0xff, false) != NULL);
    sampled = false;
    SEED(3, 1, 1, 1, 20, 20, 20, 20, 20, 20);
    CHECK(breathe(l) && LOGGED("print: L;print: Breathes!;") && sampled);
    CHECK(sample_missile == 4);
    for (int i = 0; i < 4; ++i) {
        cok_picture *icon = &game.icons[made[i].slot][made[i].image];
        cok_picture_free(icon);
        *icon = kept[i];
    }
}

/* The bytes a breath's cone reads from the stack, as 432f:2337's redraw of
 * its pick leaves them: with an icon mirrored, the mirror's last byte
 * (the first of the mask's last row, its nibbles swapped) twice as the
 * range, 0 cutting sight to one cell; the side, where no one is on the
 * line, 0 after a terrain tile drawn and no mirror, else the low byte of a
 * stack address: the run stops where the side's 256 values give different
 * targets. */
static void test_breath_bytes(void)
{
    /* Acid at A, on the view (8, 8) and facing 4: A's icon (slot 5) is
     * drawn mirrored. Its mask's byte 0: no breath; 0xf0: A is hit. */
    for (int k = 0; k < 2; ++k) {
        reset();
        cok_character *kk = record('K', 1, 10, 10), *a = record('A', 0, 13, 10);
        kk->record[0x62] = 16;
        a->record[0x197] = 30;
        a->record[0x137] = 5;
        a->combat->facing = 4;
        game.combat.view_x = game.combat.view_y = 8;
        CHECK(cok_combat_scroll(&game.combat, 11, 11, 0xff) || true);
        cok_picture *icon = &game.icons[5][0];
        cok_picture_free(icon);
        CHECK(cok_picture_create(icon, 3, 24, 1, 1) == COK_PICTURE_OK);
        memset(icon->mask, 0xff, icon->frame_size);
        icon->mask[23 * 12] = k == 0 ? 0x00 : 0xf0;
        cok_effect *e = cok_character_add_effect(kk, 0x5a, 0, 0xff, false);
        unsigned mirrors = game.combat.mirrors;
        SEED(2, 1, 1, 1, 20, 1, 1);
        CHECK(breathe(kk) && game.combat.mirrors > mirrors);
        CHECK(LOGGED("breathes acid") == (k == 1) && a->record[0x197] == (k == 0 ? 30 : 14));
        CHECK(e->value == (k == 0 ? 3 : 2));
        cok_picture_free(icon);
    }
    /* The same in the other modes (DS:4b76). EGA: 127f:0a51's byte is
     * the mask's of the last row's first unit, a bit for each of its 8
     * pixels transparent: pixels 0 and 1 opaque but 3 transparent give
     * sight (A hit), where the Tandy's 2-pixel byte gives none; all 8
     * opaque, none. CGA: the count 2 of 127f:0b94's last Move, whatever
     * the mask: sight to 3 cells (the line runs to a length past 5),
     * reaching A 3 away but not 4; the side byte is the heap's, not known,
     * and the 256 tried agree (A 4 away is reached with sight not cut, in
     * Tandy mode). */
    for (int k = 0; k < 6; ++k) {
        reset();
        game.display = k < 2 ? COK_DISPLAY_EGA : k == 2 || k == 5 ? COK_DISPLAY_TANDY
                                                                    : COK_DISPLAY_CGA;
        cok_character *kk = record('K', 1, 10, 10), *a = record('A', 0, k >= 4 ? 14 : 13, 10);
        kk->record[0x62] = 16;
        a->record[0x197] = 30;
        a->record[0x137] = 5;
        a->combat->facing = 4;
        game.combat.view_x = game.combat.view_y = 8;
        CHECK(cok_combat_scroll(&game.combat, 11, 11, 0xff) || true);
        cok_picture *icon = &game.icons[5][0];
        cok_picture_free(icon);
        CHECK(cok_picture_create(icon, 3, 24, 1, 1) == COK_PICTURE_OK);
        memset(icon->mask, 0xff, icon->frame_size);
        memset(icon->mask + 23 * 12, 0, 4);
        if (k != 1) icon->mask[23 * 12 + 1] = 0x0f;
        if (k == 5) icon->mask[23 * 12] = 0xf0;
        cok_effect *e = cok_character_add_effect(kk, 0x5a, 0, 0xff, false);
        unsigned mirrors = game.combat.mirrors;
        SEED(2, 1, 1, 1, 20, 1, 1);
        CHECK(breathe(kk) && game.combat.mirrors > mirrors);
        bool hit = k == 0 || k == 3 || k == 5;
        CHECK(game.combat.mirrored ==
              (k == 0 ? 0x0808 : k == 3 || k == 4 ? 2 : k == 5 ? 0x0f0f : 0));
        CHECK(LOGGED("breathes acid") == hit && a->record[0x197] == (hit ? 14 : 30));
        CHECK(e->value == (hit ? 2 : 3));
        cok_picture_free(icon);
    }
    game.display = COK_DISPLAY_TANDY;
    /* A dragon two cells across, a wall at 11, 11 below its first: its
     * cone's line stops there, meeting no one; A at 13, 12 seen from its
     * second cell. On the view, A's cell redrawn (a tile, no mirror): side
     * 0, its lines meet no one but the dragon: no breath. Off it: the low
     * byte of an address on the stack, 0xe0 in a battle 2fd3:3c28's script
     * starts, which sends the right line onto A; 0xda through 3b47, which
     * does not, nor 0xb8 in a battle of PROGRAM 9's camp (its vectors
     * COK_PROGRAM_CAMP_DEPTH below the script's). In CGA mode with no
     * mirror the byte is the same stack offset's, known (k 4 as k 1). */
    for (int k = 0; k < 5; ++k) {
        reset();
        game.display = k == 4 ? COK_DISPLAY_CGA : COK_DISPLAY_TANDY;
        game.battle_sp = k == 2   ? COK_BATTLE_SP - 6
                         : k == 3 ? COK_BATTLE_SP - COK_PROGRAM_CAMP_DEPTH
                                  : COK_BATTLE_SP;
        bool hit = k == 1 || k == 4;
        cok_character *w = record('W', 1, 10, 10), *a = record('A', 0, 13, 12);
        w->record[0xcf] = 3;
        game.combat.combatant[1].size = 3;
        CHECK(cok_combat_occupy(&game.combat));
        CELL(11, 11) = 0x01;
        a->record[0x197] = 50;
        if (k == 0) {
            game.combat.view_x = game.combat.view_y = 8;
            CHECK(cok_combat_scroll(&game.combat, 11, 11, 0xff) || true);
        }
        cok_effect *e = cok_character_add_effect(w, 0x53, 0, 0xff, false);
        unsigned tiles = game.combat.tiles_drawn;
        SEED(1, 1, 1, 1);
        CHECK(breathe(w) && LOGGED("breathes") == hit && e->value == (hit ? 2 : 3));
        CHECK((a->record[0x197] < 50) == hit && (k != 0 || game.combat.tiles_drawn > tiles));
    }
    game.display = COK_DISPLAY_TANDY;
    /* The same with A's icon drawn mirrored (on the view, facing 4, its
     * mask clear: no limit to sight): the side is 127f:0b94's frame
     * pointer's low byte, 0xea, which sends the line past A: no breath.
     * In CGA mode it is the low byte of the mirror's mask's offset in the
     * heap, not known, and some of the 256 bytes meet A: a stop. */
    for (int k = 0; k < 2; ++k) {
        reset();
        game.display = k == 0 ? COK_DISPLAY_TANDY : COK_DISPLAY_CGA;
        cok_character *w = record('W', 1, 10, 10), *a = record('A', 0, 13, 12);
        w->record[0xcf] = 3;
        game.combat.combatant[1].size = 3;
        CHECK(cok_combat_occupy(&game.combat));
        CELL(11, 11) = 0x01;
        a->record[0x197] = 50;
        a->record[0x137] = 5;
        a->combat->facing = 4;
        game.combat.view_x = game.combat.view_y = 8;
        CHECK(cok_combat_scroll(&game.combat, 11, 11, 0xff) || true);
        cok_picture *icon = &game.icons[5][0];
        cok_picture_free(icon);
        CHECK(cok_picture_create(icon, 3, 24, 1, 1) == COK_PICTURE_OK);
        memset(icon->mask, 0xff, icon->frame_size);
        cok_effect *e = cok_character_add_effect(w, 0x53, 0, 0xff, false);
        unsigned mirrors = game.combat.mirrors;
        SEED(1, 1, 1, 1);
        if (k == 1) {
            CHECK(!breathe(w) && strstr(game.error, "5b04:1b2a") != NULL);
            CHECK(game.combat.mirrors > mirrors && a->record[0x197] == 50);
            cok_picture_free(icon);
            game.display = COK_DISPLAY_TANDY;
            continue;
        }
        CHECK(breathe(w) && game.combat.mirrors > mirrors && !LOGGED("breathes"));
        CHECK(e->value == 3 && a->record[0x197] == 50);
        cok_picture_free(icon);
    }
}

/* The battle's end frees the clouds and pools (3995:004b); 0x28 ends
 * after it with nothing to clear. */
static void test_freed(void)
{
    reset();
    cok_character *m = record('M', 1, 20, 20), *k = record('K', 1, 12, 12);
    CHECK(lay(m, 5, 5) && lay(m, 8, 5));
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    k->record[0x189] = 0;
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d) && game.combat.pools != NULL);
    CHECK(cok_combat_end(&game) && game.combat.clouds == NULL && game.combat.pools == NULL);
    game.effects.in_battle = false;
    s.log[0] = '\0';
    CHECK(cok_effects_remove(&game.effects, m, NULL, 0x28) && !LOGGED("clears"));
    CHECK(cok_effects_remove(&game.effects, k, NULL, 0x50) && !LOGGED("drains"));
    game.effects.in_battle = true;
}

/* The pauses (speed 1: 100 ms): a cloud's text has none, then one; a
 * pool's text none. */
static void test_pauses(void)
{
    reset();
    game.speed = 1;
    cok_character *m = record('M', 1, 20, 20), *k = record('K', 1, 12, 12);
    pauses = 0;
    CHECK(lay(m, 5, 5) && pauses == 1);
    CHECK(cok_character_add_effect(k, 0x46, 0, 0xff, false) != NULL);
    k->record[0x189] = 0;
    pauses = 0;
    CHECK(cok_effects_dispatch(&game.effects, k, 0x0d) && pauses == 0);
    CHECK(LOGGED("print: K;print: dissolves into acid!;"));
    game.speed = 0;
}

/* A bolt between two walls as far apart as its line before was long is
 * charged nothing (the length before a bounce counts against the next
 * line) and bounces for ever, as the original does: the run stops. A
 * dragon two cells across at 10, 16 breathes lightning at A (11, 13, which
 * drops): the bolt bounces off 11, 12, then 10, 15 and 10, 11 in turn. */
static void test_endless_bolt(void)
{
    reset();
    game.vm.mem4b00[0xe6] = 1; /* a 3D area: walls bounce */
    cok_character *l = record('L', 1, 10, 16), *a = record('A', 0, 11, 13);
    l->record[0xcf] = 3;
    l->record[0x62] = 40;
    game.combat.combatant[1].size = 3;
    CHECK(cok_combat_occupy(&game.combat));
    a->record[0x197] = 1;
    CELL(11, 12) = CELL(10, 15) = CELL(10, 11) = 0x01;
    CHECK(cok_character_add_effect(l, 0x55, 0, 0xff, false) != NULL);
    SEED(2, 1, 1, 1, 20, 1, 1);
    CHECK(!breathe(l) && strstr(game.error, "bounces between walls for ever") != NULL);
    CHECK(a->record[0x189] == 0);
}

int main(void)
{
    cok_keyboard keyboard = {scripted, &s, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .delay = on_delay, .context = &s};
    if (!cok_adventure_open(&game, "Assets", &keyboard, &hooks)) {
        printf("cloud tests skipped: %s\n", game.error);
        return 0;
    }
    load_screen();
    test_lay();
    test_lay_more();
    test_images();
    test_square();
    test_caster_selected();
    test_clear();
    test_acid();
    test_acid_body();
    test_dispel();
    test_ends_more();
    test_dispel_more();
    test_under_last();
    test_clear_edge();
    test_caster_xy();
    test_puddle_dice();
    test_dispel_orphan();
    test_two_bodies();
    test_acid_terrain();
    test_dragon();
    test_attack_4();
    test_attack_4e();
    test_attack_54();
    test_attack_55();
    test_attack_5a();
    test_aurak_target();
    test_fireball_view();
    test_breath_outside();
    test_aurak_keeps_use();
    test_fireball_images();
    test_breath_own_cell();
    test_lightning_shown();
    test_poison_no_one();
    test_acid_no_pick();
    test_breath_bytes();
    test_freed();
    test_pauses();
    test_endless_bolt();
    cok_adventure_close(&game);
    puts("cloud tests passed");
    return 0;
}
