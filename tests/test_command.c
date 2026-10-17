#include "command.h"

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

static void on_delay(cok_adventure *g, unsigned ms, void *context)
{
    (void)g;
    if (ms != 200) return; /* the 200 ms of Auto and Alt-Q only */
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "wait: %u;", ms);
}

/* The keyboard's flushes (1614:045c), logged as "flush;" while asked. */
static bool log_flushes;

static void on_flush(cok_adventure *g, void *context)
{
    (void)g;
    if (!log_flushes) return;
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "flush;");
}

static void dump(void)
{
    if (getenv("COMMAND_LOG") != NULL) fprintf(stderr, "%s\n", s.log);
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
    CHECK(cok_character_stats(c, &cok_rules_champions, &game.item_types, why, sizeof why));
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
    CHECK(cok_character_stats(c, &cok_rules_champions, &game.item_types, why, sizeof why));
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

/* The first cleric spell from 1 whose bytes 11 and 12 pass test. */
static uint8_t find_spell(bool (*test)(uint8_t usable, uint8_t time))
{
    for (uint8_t id = 1; id < 0x6c; ++id)
        if (cok_spell_class(id) == 0 && test(spell_byte(id, 11), spell_byte(id, 12))) return id;
    CHECK(false);
    return 0;
}
static bool camp_only(uint8_t usable, uint8_t time) { (void)time; return usable == 0; }
static bool quick_spell(uint8_t usable, uint8_t time) { return usable != 0 && time / 3 == 0; }
static bool slow_spell(uint8_t usable, uint8_t time) { return usable != 0 && time / 3 >= 2; }

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

static void test_text(void)
{
    reset();
    cok_character *a = record('A', 0, 5, 5);
    uint8_t *r = a->record;
    char text[41];
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "Move View Aim Done") == 0);
    a->combat->movement = 0;
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "View Aim Done") == 0);
    a->combat->movement = 1;
    give(a, 0x12, true);
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "Move View Aim Use Done") == 0);
    /* Cast: a spell memorized, from +0x1e to +0x57, may cast this round,
     * the area allows magic. */
    r[0x57] = 3;
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "Move View Aim Use Cast Done") == 0);
    r[0x57] = 0;
    r[0x58] = 3;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Cast") == NULL);
    r[0x58] = 0;
    r[0x1e] = 0x83;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Cast ") != NULL);
    a->combat->may_cast = 0;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Cast") == NULL);
    a->combat->may_cast = 1;
    game.vm.mem4b00[0xe5] = 1;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Cast") == NULL);
    game.vm.mem4b00[0xe5] = 0;
    r[0x1d] = 3;
    r[0x1e] = 0;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Cast") == NULL);
    /* Turn: a cleric, while it has tried fewer times than undead loaded. */
    r[0xf9] = 1;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Turn") == NULL);
    game.undead = 2;
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "Move View Aim Use Turn Done") == 0);
    a->combat->turns = 1;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Turn") != NULL);
    a->combat->turns = 2;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Turn") == NULL);
    a->combat->turns = 0;
    r[0xf9] = 0x80; /* signed: no level */
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Turn") == NULL);
    /* A former cleric that a human may use (66c2:0efb). */
    r[0xf9] = 0;
    r[0x101] = 3;
    bool former = cok_character_former_class(r, &cok_rules_champions);
    cok_combat_command_text(&game, a, text);
    CHECK((strstr(text, "Turn") != NULL) == former);
    r[0x101] = 0x81;
    cok_combat_command_text(&game, a, text);
    CHECK(strstr(text, "Turn") == NULL);
    r[0x101] = 0;
    /* Yell: a kender, while no one has yelled this battle. */
    r[0x5a] = 5;
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "Move View Aim Use Yell Done") == 0);
    game.combat.yelled = r;
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "Move View Aim Use Done") == 0);
    game.combat.yelled = NULL;
    /* Everything. */
    r[0xf9] = 1;
    r[0x30] = 3;
    cok_combat_command_text(&game, a, text);
    CHECK(strcmp(text, "Move View Aim Use Cast Turn Yell Done") == 0);
}

/* The combat screen draws with a battle's tiles: a setup loads them. */
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

static void test_menu(void)
{
    reset();
    cok_character *a = record('A', 0, 5, 5);
    record('M', 1, 9, 9);
    turn_of(a, 1);
    a->combat->movement = 0;
    /* Escape gives 0, Q is no hotkey of the menu, Alt-R is taken and does
     * nothing, keypad 5 (scan 0x20) and F7 (scan 0x41, A) are scan codes
     * other than the hidden keys, so none; each reads the menu again.
     * Then Done, Quit. */
    KEYS("\x1bQ\x00\x13" "5\x00\x41" "DQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(count("menu: View Aim Done;") == 5 && LOGGED("menu: Guard Delay Quit Auto Speed Exit;"));
    CHECK(a->combat->initiative == 0 && s.at == s.length && !game.vm.abort);
    /* The first item is selected each time round: the Done menu takes the
     * main menu's selection (Done, the fifth), 4 moves it to Auto, Escape
     * goes back, and Enter picks Move, the first; Enter in Move's menu
     * picks Done, which leaves Move; then Done, Quit. */
    reset();
    a = record('A', 0, 5, 5);
    record('M', 1, 9, 9);
    turn_of(a, 1);
    give(a, 0x12, true);
    KEYS("D4\x1b\r\rDQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("menu: Guard Delay Quit Auto Speed Exit;menu: Move View Aim Use Done;"
                 "menu:  Done;menu: Move View Aim Use Done;menu: Guard"));
    CHECK(a->combat->initiative == 0 && game.combat.combatant[1].x == 5);
    /* Alt-R is taken: the turn's loop goes round, the first item selected
     * again; 6 had moved it to View. Enter then picks Move. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 1);
    KEYS("6\x00\x13\r" "DDQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("menu: Move View Aim Done;menu: Move View Aim Done;menu:  Done;"));
    /* A scan code other than the hidden keys (F7, 0x41) is none: the menu
     * is read again with its selection (View, after 6) kept. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 1);
    KEYS("6\x00\x41\rEDQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("menu: Move View Aim Done;menu: Move View Aim Done;print: A;"));
    /* Moving away and back with Aim's Center, the map is centred on the
     * combatant again (6beb:12ef) when the turn goes on. */
    reset();
    a = record('A', 0, 5, 5);
    record('M', 1, 40, 20);
    turn_of(a, 1);
    KEYS("ANCEDQ");
    game.combat.view_x = game.combat.view_y = 0;
    game.combat.show_actions = true;
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(game.combat.view_x == 2 && game.combat.view_y == 2);
    /* One that cannot act ends its turn without a menu. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 1);
    a->record[0x189] = 0;
    a->combat->movement = 4;
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(a->combat->initiative == 0 && a->combat->movement == 0 && !LOGGED("menu"));
    /* A spell begun at an earlier turn is cast and the turn ends, without
     * a menu. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 1);
    a->combat->spell = 5;
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("print: Casts a Spell;print: Spell:Detect Magic;") && !LOGGED("menu"));
    CHECK(LOGGED("print: A;print: is affected;"));
    CHECK(a->combat->spell == 0 && a->combat->initiative == 0 && a->combat->movement == 0);
    /* Keys running out end the run. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 1);
    CHECK(cok_combat_commands(&game, a, 0) && game.vm.abort && game.input_ended);
    CHECK(game.vm.status == COK_ECL_OK && a->combat->initiative == 5);
}

static void test_hidden_keys(void)
{
    reset();
    cok_character *a = record('A', 0, 5, 5), *b = record('B', 0, 6, 5),
                  *n = record('N', 0, 7, 5), *m = record('M', 1, 9, 9);
    n->record[0xe7] = 0x80;
    b->record[0x18b] = n->record[0x18b] = 1;
    b->record[0x188] = 4;
    turn_of(a, 2);
    /* Space: every party member (+0xe7 below 0x80), whatever its status,
     * back to the player; an NPC stays the computer's. Alt-M twice:
     * Magic On, then Off. Then Alt-Q: the turn given back. */
    KEYS(" \x00\x32\x00\x32\x00\x10");
    a->combat->target = b->record;
    m->combat->target = a->record;
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(b->record[0x18b] == 1 && n->record[0x18b] == 1); /* Alt-Q put all back */
    CHECK(LOGGED("print: Magic On;") && LOGGED("print: Magic Off;") && !game.combat.magic_on);
    CHECK(game.combat.turn_index == 1 && a->combat->initiative == 0x14);
    CHECK(a->record[0x18b] == 1 && m->record[0x18b] == 1 && count("wait: 200;") == 1);
    /* 3995:1604 forgets a target on its own side only. */
    CHECK(a->combat->target == NULL && m->combat->target == a->record);
    CHECK(s.at == s.length);
    /* Space alone. */
    reset();
    a = record('A', 0, 5, 5);
    b = record('B', 0, 6, 5);
    n = record('N', 0, 7, 5);
    n->record[0xe7] = 0x80;
    b->record[0x18b] = n->record[0x18b] = 1;
    b->record[0x188] = 1;
    turn_of(a, 1);
    KEYS(" DQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(b->record[0x18b] == 0 && n->record[0x18b] == 1);
    /* Alt-A as Alt-Q; Magic On stays. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 3);
    KEYS("\x00\x32\x00\x1e");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(game.combat.turn_index == 2 && a->combat->initiative == 0x14 && game.combat.magic_on);
    /* Pressed for the player: Alt-Q without a key read. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 3);
    CHECK(cok_combat_commands(&game, a, 0x10) && !game.vm.abort);
    CHECK(game.combat.turn_index == 2 && a->combat->initiative == 0x14);
    /* Alt-X: without Helm, "That doesn't work" and the menu goes on; with
     * it, the gods' cheat. */
    reset();
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 9);
    turn_of(a, 1);
    KEYS("\x00\x2d" "DQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("print: That doesn't work;") && m->record[0x188] == 0);
    reset();
    a = record('A', 0, 5, 5);
    m = record('M', 1, 9, 9);
    turn_of(a, 1);
    game.helm = true;
    KEYS("\x00\x2d");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("print: The Gods intervene!;") && m->record[0x188] == 6);
    CHECK(a->combat->initiative == 0 && s.at == s.length);
    CHECK(cok_combat_commands(&game, a, 0x2d) && !game.vm.abort);
    game.helm = false;
    /* Turn and Yell end the turn. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 1);
    a->record[0xf9] = 1;
    a->record[0x5a] = 5;
    game.undead = 1;
    KEYS("T");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("print: turns undead...;") && a->combat->initiative == 0 && a->combat->turns == 1);
    a->combat->initiative = 3;
    a->combat->movement = 4;
    KEYS("Y");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(game.combat.yelled == a->record && a->combat->initiative == 0 &&
          a->combat->movement == 0);
}

/* A terrain value whose cost (DS:1ee4) is cost. */
static uint8_t terrain_of_cost(uint8_t cost)
{
    for (uint8_t t = 1; t < COK_COMBAT_TERRAINS; ++t)
        if (cok_combat_terrain[t].cost == cost) return t;
    CHECK(false);
    return 0;
}

static void test_move(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    record('M', 1, 20, 20);
    turn_of(a, 1);
    bool done = true;
    /* East costs 2 of the 12, north-east 3, Home (north-west) 3: 4 left;
     * D leaves Move, the turn going on. */
    KEYS("69\x00\x47" "D");
    CHECK(cok_combat_move(&game, a, &done) && !done);
    CHECK(game.combat.combatant[1].x == 11 && game.combat.combatant[1].y == 8);
    CHECK(a->combat->movement == 4 && a->combat->facing == 7 && count("sound: 10;") == 3);
    CHECK(count("menu:  Done;") == 4 && a->combat->initiative == 5);
    /* 4 left: a diagonal (3) is allowed, then 1 left: "can't go there"
     * for any step; Enter leaves, and the half square is lost. */
    s.log[0] = '\0';
    KEYS("3" "2" "\r");
    CHECK(cok_combat_move(&game, a, &done) && !done);
    CHECK(game.combat.combatant[1].x == 12 && game.combat.combatant[1].y == 9);
    CHECK(LOGGED("print: can't go there;") && a->combat->movement == 0);
    /* 2 left: an orthogonal step, then none. */
    a->combat->movement = 2;
    s.log[0] = '\0';
    KEYS("8D");
    CHECK(cok_combat_move(&game, a, &done) && !done && a->combat->movement == 0);
    CHECK(game.combat.combatant[1].y == 8 && !LOGGED("can't"));
    /* 3 left, and 2 after a step: kept. */
    a->combat->movement = 5;
    KEYS("8D");
    CHECK(cok_combat_move(&game, a, &done) && a->combat->movement == 3);
    a->combat->movement = 4;
    KEYS("8D");
    CHECK(cok_combat_move(&game, a, &done) && a->combat->movement == 2);
    /* Rough ground costs its cost twice, three times diagonally; a cell
     * no one can enter "can't go there" whatever is left. */
    reset();
    a = record('A', 0, 10, 10);
    turn_of(a, 1);
    uint8_t rough = terrain_of_cost(2), wall = terrain_of_cost(0xff);
    game.combat.cells[10][11] = rough;
    game.combat.cells[9][11] = rough;
    game.combat.cells[11][10] = wall;
    a->combat->movement = 0xfe;
    KEYS("2" "6" "D");
    CHECK(cok_combat_move(&game, a, &done) && !done);
    CHECK(LOGGED("print: can't go there;") && a->combat->movement == 0xfe - 4);
    /* At 11,10 now: north is rough, 4 more than 3. */
    a->combat->movement = 3;
    s.log[0] = '\0';
    KEYS("8" "D");
    CHECK(cok_combat_move(&game, a, &done) && a->combat->movement == 3 && LOGGED("can't"));
    /* North-east rough too: 6 more than 5; with 6 it goes. */
    game.combat.cells[9][12] = rough;
    a->combat->movement = 5;
    KEYS("9" "D");
    CHECK(cok_combat_move(&game, a, &done) && a->combat->movement == 5);
    CHECK(game.combat.combatant[1].x == 11 && game.combat.combatant[1].y == 10);
    a->combat->movement = 6;
    KEYS("9" "D");
    CHECK(cok_combat_move(&game, a, &done) && a->combat->movement == 0);
    CHECK(game.combat.combatant[1].x == 12 && game.combat.combatant[1].y == 9);
    /* A footprint of 2 by 2 pays for the costliest cell it moves onto. */
    reset();
    a = record('A', 0, 10, 10);
    turn_of(a, 1);
    a->record[0xcf] = 4;
    game.combat.combatant[1].size = 4;
    CHECK(cok_combat_occupy(&game.combat));
    game.combat.cells[11][12] = rough;
    a->combat->movement = 3;
    s.log[0] = '\0';
    KEYS("6" "D");
    CHECK(cok_combat_move(&game, a, &done) && a->combat->movement == 3 && LOGGED("can't"));
    a->combat->movement = 4;
    KEYS("6" "D");
    CHECK(cok_combat_move(&game, a, &done) && game.combat.combatant[1].x == 11);
    /* Escape undoes the moves: back where it started, its movement and
     * facing as they were, and Move ends with the turn going on. */
    reset();
    a = record('A', 0, 10, 10);
    turn_of(a, 1);
    a->combat->facing = 2;
    KEYS("88\x1b");
    CHECK(cok_combat_move(&game, a, &done) && !done);
    CHECK(game.combat.combatant[1].x == 10 && game.combat.combatant[1].y == 10);
    CHECK(a->combat->movement == 12 && a->combat->facing == 2 && count("sound: 10;") == 2);
    CHECK(game.combat.occupant[10][10] == 1 && game.combat.occupant[8][10] == 0);
    /* Put back where another stands (here one already there): it cannot
     * be, so it is off the map (size 0), and the turn ends. */
    record('G', 1, 10, 10);
    KEYS("8\x1b");
    CHECK(cok_combat_move(&game, a, &done) && done && game.combat.combatant[1].size == 0);
    CHECK(a->combat->movement == 12);
}

static void test_charge(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 11, 10);
    turn_of(a, 1);
    give(a, 0x12, true);
    bool done = false;
    /* Moving into an enemy attacks it: one attack, and the turn is over. */
    KEYS("6");
    CHECK(cok_combat_move(&game, a, &done) && done);
    CHECK(LOGGED("print: Attacks;") && a->combat->target == m->record);
    CHECK(game.combat.combatant[1].x == 10 && a->combat->facing == 2);
    CHECK(m->combat->hits == 1);
    /* With a missile weapon not thrown: "Not with that weapon". */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    turn_of(a, 1);
    give(a, 0x17, true);
    bool missile = false, thrown = true;
    CHECK(cok_combat_missile_weapon(&game.item_types, a, &missile) && missile);
    CHECK(cok_combat_thrown(&game.item_types, a, &thrown) && !thrown);
    KEYS("6D");
    CHECK(cok_combat_move(&game, a, &done) && !done);
    CHECK(LOGGED("print: Not with that weapon;") && !LOGGED("Attacks") && m->combat->hits == 0);
    /* Into an ally the player controls: "Attack Ally: " No. */
    reset();
    a = record('A', 0, 10, 10);
    cok_character *b = record('B', 0, 11, 10);
    turn_of(a, 1);
    KEYS("6ND");
    CHECK(cok_combat_move(&game, a, &done) && !done);
    CHECK(LOGGED("menu: Attack Ally: ;choice: N;") && b->combat->hits == 0);
    /* Off the map: "Flee:" No goes on; Yes tries to get away, which ends
     * the turn either way. With no enemy on the map it gets away. */
    reset();
    a = record('A', 0, 0, 10);
    turn_of(a, 1);
    KEYS("4ND");
    CHECK(cok_combat_move(&game, a, &done) && !done && LOGGED("menu: Flee:;choice: N;"));
    KEYS("4Y");
    CHECK(cok_combat_move(&game, a, &done) && done);
    CHECK(LOGGED("print: Got Away;") && a->record[0x188] == 3 && a->record[0x189] == 0);
    /* An enemy faster than it: "Escape is blocked", its turn over. */
    reset();
    a = record('A', 0, 49, 10);
    m = record('M', 1, 30, 20);
    m->record[0x198] = 20;
    a->record[0x198] = 6;
    turn_of(a, 1);
    KEYS("6Y");
    CHECK(cok_combat_move(&game, a, &done) && done && LOGGED("print: Escape is blocked;"));
    CHECK(a->record[0x189] == 1 && a->combat->initiative == 0);
    /* Leaving an enemy beside it: it is attacked from behind, once. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    turn_of(a, 1);
    KEYS("4D");
    CHECK(cok_combat_move(&game, a, &done));
    CHECK(LOGGED("attack: A (from behind)") && game.combat.combatant[1].x == 9);
}

static void test_ally(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 11, 10),
                  *m = record('M', 1, 12, 10), *n = record('N', 0, 13, 10),
                  *t = record('T', 0, 14, 10), *d = record('D', 0, 15, 10);
    n->record[0xe7] = 0x80;
    t->record[0xe7] = 0xb3;
    d->record[0xe7] = 0x90;
    d->record[0x188] = 4;
    n->combat->target = m->record;
    bool go = false;
    /* Against the other side: no question. */
    CHECK(cok_combat_ally(&game, a, m, &go) && go && !LOGGED("menu"));
    /* The computer's own side too, attacking the party. */
    CHECK(cok_combat_ally(&game, m, a, &go) && go && !LOGGED("menu"));
    /* An ally of a player's: asked; No refuses. A target the computer
     * controls is still asked about: the test is the attacker's. */
    b->record[0x18b] = 1;
    KEYS("N");
    CHECK(cok_combat_ally(&game, a, b, &go) && !go && LOGGED("menu: Attack Ally: ;"));
    CHECK(game.vm.mem7c00[0x333] == 0 && n->record[0x18a] == 0);
    /* One the computer controls attacks an ally without a question. */
    s.log[0] = '\0';
    a->record[0x18b] = 1;
    CHECK(cok_combat_ally(&game, a, b, &go) && go && !LOGGED("menu"));
    a->record[0x18b] = 0;
    /* Yes: var 0x7f33 set, every NPC that is okay (but 0xb3) goes against
     * the party and forgets its target; the sides are counted. */
    KEYS("Y");
    CHECK(cok_combat_ally(&game, a, b, &go) && go);
    CHECK(game.vm.mem7c00[0x333] == 1 && n->record[0x18a] == 1 && n->combat->target == NULL);
    CHECK(t->record[0x18a] == 0 && d->record[0x18a] == 0 && b->record[0x18a] == 0);
    CHECK(game.combat.sides[0] == 4 && game.combat.sides[1] == 2);
}

static void test_aim(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 11, 10),
                  *b = record('B', 0, 10, 12), *f = record('F', 1, 14, 10);
    turn_of(a, 1);
    give(a, 0x12, true);
    bool done = true;
    cok_combat_aimed res = {a->record, 9, 9};
    /* The targets, nearest first: A itself, at 0, which it may not
     * target; Exit leaves. */
    KEYS("E");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !done);
    CHECK(res.target == NULL && res.x == 0 && res.y == 0);
    CHECK(LOGGED("menu: Next Prev Manual Center Exit;") && !LOGGED("Target"));
    /* Prev wraps to the last, F, 4 squares off, out of the sword's reach
     * (range 1); Next wraps to A, then M beside it: Target attacks. */
    s.log[0] = '\0';
    KEYS("PNNT");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && done);
    CHECK(count("menu: Next Prev Manual Center Exit;") == 3);
    CHECK(count("menu: Next Prev Manual Target Center Exit;") == 1);
    CHECK(res.target == m->record && res.x == 11 && res.y == 10);
    CHECK(m->combat->hits == 1 && f->combat->hits == 0 && LOGGED("print: Attacks;"));
    CHECK(a->combat->target == m->record);
    /* Within a range given, F too; B, an ally, asks: No refuses, clearing
     * what was picked, and aiming ends. */
    s.log[0] = '\0';
    KEYS("NNTN");
    CHECK(cok_combat_aim(&game, a, 2, 1, 0, 1, &done, &res) && !done);
    CHECK(LOGGED("menu: Attack Ally: ;choice: N;") && b->combat->hits == 0);
    CHECK(res.target == NULL && res.x == 0 && res.y == 0);
    s.log[0] = '\0';
    KEYS("NNNT");
    CHECK(cok_combat_aim(&game, a, 4, 1, 0, 1, &done, &res) && res.target == f->record);
    CHECK(count("menu: Next Prev Manual Target Center Exit;") == 3);
    /* Range 0 and 0xff become 1. */
    s.log[0] = '\0';
    KEYS("NNNE");
    CHECK(cok_combat_aim(&game, a, 0, 1, 0, 1, &done, &res) && !done);
    CHECK(count("Target") == 1);
    /* Not attacking (a spell's aiming): A itself may be picked, and nothing
     * is attacked; row 23 is left as it is. */
    s.log[0] = '\0';
    KEYS("T");
    CHECK(cok_combat_aim(&game, a, 1, 0, 0, 1, &done, &res) && done);
    CHECK(res.target == a->record && res.x == 10 && res.y == 10 && !LOGGED("Attacks"));
    /* Center: the map centred on the target with the cursor, then off. */
    game.combat.view_x = 20;
    game.combat.view_y = 15;
    KEYS("NNNCE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res));
    CHECK(game.combat.view_x == 11 && game.combat.view_y == 7 && game.combat.cursor == 0);
}

static void test_aim_missile(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 11, 10),
                  *f = record('F', 1, 14, 10);
    turn_of(a, 1);
    give(a, 0x17, true);
    size_t arrows = give(a, 0x1e, true);
    a->items[arrows - 1][0x39] = 5;
    char why[300];
    CHECK(cok_character_stats(a, &cok_rules_champions, &game.item_types, why, sizeof why));
    a->record[0x18f] = 2;
    bool done;
    cok_combat_aimed res;
    /* A bow, not thrown, with an enemy beside it: no Target at all. */
    KEYS("NNE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !done && !LOGGED("Target"));
    /* M gone from beside it: F, within the bow's range (18), is shot at
     * with the arrows, which are used up. */
    game.combat.combatant[2].x = 30;
    game.combat.combatant[2].y = 20;
    CHECK(cok_combat_occupy(&game.combat));
    s.log[0] = '\0';
    KEYS("NT");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && res.target == f->record);
    CHECK(LOGGED("Target") && a->items[arrows - 1][0x39] < 5);
    /* A thrown weapon (a hand axe, type 6, range 3) may be thrown with an
     * enemy beside: Target for both. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    f = record('F', 1, 13, 10);
    turn_of(a, 1);
    give(a, 0x06, true);
    KEYS("NNE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res));
    CHECK(count("menu: Next Prev Manual Target Center Exit;") == 2);
    /* Beside it the axe strikes, not thrown: it stays readied. */
    s.log[0] = '\0';
    KEYS("NT");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && res.target == m->record);
    CHECK(a->item_count == 1 && a->slots[0] == 1);
}

static void test_manual(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 11, 10),
                  *e = record('E', 1, 0, 10);
    turn_of(a, 1);
    bool done;
    cok_combat_aimed res;
    /* The cursor from A's cell: 6 onto M, Target attacks it. */
    KEYS("M6T");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && done);
    CHECK(res.target == m->record && res.x == 11 && res.y == 10 && game.selected == 4);
    CHECK(LOGGED("menu: (Use Cursor keys) ") || LOGGED("menu: Exit;"));
    CHECK(LOGGED("menu: Exit;menu: Target;") && m->combat->hits == 1);
    /* Exit from the cursor: back to the targets, Manual selected (3), what
     * is picked cleared but the cell where it started; with no target the
     * cell may still be valid, so Target is offered, and Target attacks
     * no one, through NULL: the run stops. */
    s.log[0] = '\0';
    KEYS("M6\x1b" "E");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !done);
    CHECK(res.target == NULL && res.x == 10 && res.y == 10);
    CHECK(LOGGED("menu: Target;menu: Next Prev Manual Target Center Exit;"));
    /* Enter there picks Manual, the third item, selected. */
    s.log[0] = '\0';
    KEYS("M6\x1b\rEE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !done);
    CHECK(count("menu: Exit;") == 2 && count("menu: Target;") == 1);
    KEYS("M6\x1b" "T");
    CHECK(!cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res));
    CHECK(game.vm.status == COK_ECL_UNDEFINED);
    /* 432f:34ae flushes after each step of the cursor, before its menu,
     * which flushes once it has its key. */
    game.vm.status = COK_ECL_OK;
    log_flushes = true;
    s.log[0] = '\0';
    KEYS("M6\x1b" "E");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !done);
    CHECK(LOGGED("flush;menu: Exit;flush;flush;menu: Target;flush;"));
    log_flushes = false;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* Down there (a scan code, P) reads the cell of no target: that of
     * the entry after the last combatant (6beb:0c43 finds NULL there),
     * here 0, 0, then 40, 20 as a placement that failed would leave it. */
    game.combat.view_x = 20;
    game.combat.view_y = 15;
    KEYS("M6\x1b\x00\x50" "CE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !done);
    CHECK(game.combat.view_x == 0 && game.combat.view_y == 0);
    game.combat.combatant[game.combat.count].x = 40;
    game.combat.combatant[game.combat.count].y = 20;
    KEYS("M6\x1b\x00\x50" "CE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !done);
    CHECK(game.combat.view_x == 37 && game.combat.view_y == 17);
    /* Off the map there, sight reads past it: the run stops. */
    game.combat.combatant[game.combat.count].y = 25;
    KEYS("M6\x1b\x00\x50" "CE");
    CHECK(!cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res));
    CHECK(game.vm.status == COK_ECL_UNDEFINED);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.combat.combatant[game.combat.count].x = 0;
    game.combat.combatant[game.combat.count].y = 0;
    /* The cursor stays on the map: 12 west from 10 is 0; a spell's aiming
     * (not attacking, range 20) picks E there. */
    KEYS("M444444444444T");
    CHECK(cok_combat_aim(&game, a, 20, 0, 0, 1, &done, &res) && done);
    CHECK(res.target == e->record && res.x == 0 && res.y == 10);
    /* Out of range: Exit only, Escape leaves the cursor as Exit does. */
    s.log[0] = '\0';
    KEYS("M444444444444\x1b" "E");
    CHECK(cok_combat_aim(&game, a, 9, 0, 0, 1, &done, &res) && !done);
    CHECK(count("menu: Target;") == 1); /* A's own cell only */
    /* An empty cell is valid only where empty cells may be picked. */
    s.log[0] = '\0';
    KEYS("M8T");
    CHECK(cok_combat_aim(&game, a, 4, 0, 1, 1, &done, &res) && done);
    CHECK(res.target == NULL && res.x == 10 && res.y == 9);
    KEYS("M8EE");
    CHECK(cok_combat_aim(&game, a, 4, 0, 0, 1, &done, &res) && !done);
    /* A creature only where creatures may be picked. */
    s.log[0] = '\0';
    KEYS("M6EE");
    CHECK(cok_combat_aim(&game, a, 4, 0, 1, 0, &done, &res) && !done && !LOGGED("menu: Target;"));
    /* A wall is never valid. */
    game.combat.cells[9][10] = terrain_of_cost(0xff);
    s.log[0] = '\0';
    KEYS("M8EE");
    CHECK(cok_combat_aim(&game, a, 4, 0, 1, 1, &done, &res));
    CHECK(count("menu: Target;") == 1 && LOGGED("menu: Target;menu: Exit;"));
    game.combat.cells[9][10] = COK_COMBAT_FLOOR;
    /* From a table (eye height 2) sight passes over a wall (block 2), but
     * a wall is never valid; from the floor, a monster behind one is not
     * seen, so not valid. */
    game.combat.cells[10][10] = 0x1a;
    game.combat.cells[9][10] = terrain_of_cost(0xff);
    s.log[0] = '\0';
    KEYS("M8EE");
    CHECK(cok_combat_aim(&game, a, 4, 0, 1, 1, &done, &res) && count("menu: Target;") == 1);
    game.combat.cells[10][10] = COK_COMBAT_FLOOR;
    cok_character *hidden = record('H', 1, 10, 8);
    s.log[0] = '\0';
    KEYS("M88EE");
    CHECK(cok_combat_aim(&game, a, 4, 0, 1, 1, &done, &res) && count("menu: Target;") == 1);
    (void)hidden;
    game.combat.cells[9][10] = COK_COMBAT_FLOOR;
    /* A body (terrain 0x1f) is its record's; attacking, never valid. */
    cok_character *dead = record('D', 0, 30, 20);
    cok_character *dead2 = record('Z', 0, 31, 20);
    game.combat.body[0] = (cok_combat_body){dead, 9, 10, COK_COMBAT_FLOOR};
    game.combat.body[1] = (cok_combat_body){dead2, 9, 12, COK_COMBAT_FLOOR};
    game.combat.bodies = 2;
    game.combat.cells[10][9] = COK_COMBAT_BODY;
    KEYS("M4T");
    CHECK(cok_combat_aim(&game, a, 4, 0, 0, 1, &done, &res) && done && res.target == dead->record);
    s.log[0] = '\0';
    KEYS("M4EE");
    CHECK(cok_combat_aim(&game, a, 4, 1, 0, 1, &done, &res) && !done);
    CHECK(!LOGGED("menu: Target;"));
    /* Space centres the map on the cursor. */
    game.combat.view_x = 30;
    game.combat.view_y = 15;
    KEYS("M8 T");
    CHECK(cok_combat_aim(&game, a, 4, 0, 1, 1, &done, &res) && done);
    CHECK(game.combat.view_x == 7 && game.combat.view_y == 6);
    /* Escape from the cursor centres it back where it started. */
    KEYS("M8 \x1b");
    CHECK(cok_combat_aim(&game, a, 4, 0, 1, 1, &done, &res) && game.vm.abort);
    CHECK(game.combat.view_x == 7 && game.combat.view_y == 7);
    game.vm.abort = game.input_ended = false;
}

static void test_done(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 11, 10),
                  *c = record('C', 1, 12, 10);
    give(a, 0x12, true);
    turn_of(a, 1);
    game.combat.turn[2] = b;
    game.combat.turn[3] = c;
    bool done = false;
    /* Guard: the turn ends and it guards, said on row 24. */
    KEYS("G");
    a->combat->movement = 4;
    CHECK(cok_combat_done(&game, a, &done) && done);
    CHECK(a->combat->guarding == 1 && a->combat->initiative == 0 && a->combat->movement == 0);
    CHECK(LOGGED("menu: Guard Delay Quit Auto Speed Exit;print: Guarding;"));
    /* Made to flee: the turn ends, no guard. */
    s.log[0] = '\0';
    a->combat->guarding = 0;
    a->combat->forced = 1;
    a->combat->initiative = 5;
    KEYS("G");
    CHECK(cok_combat_done(&game, a, &done) && done && a->combat->guarding == 0);
    CHECK(a->combat->initiative == 0 && !LOGGED("Guarding"));
    a->combat->forced = 0;
    /* Delay: initiative 1, to the end of the round's order, the battle
     * going on with the next (DS:72ce less 1). */
    a->combat->initiative = 5;
    KEYS("D");
    CHECK(cok_combat_done(&game, a, &done) && done && a->combat->initiative == 1);
    CHECK(game.combat.turn[1] == b && game.combat.turn[2] == c && game.combat.turn[3] == a);
    CHECK(game.combat.turn[4] == NULL && game.combat.turn_index == 0);
    /* Last already: it stays, and its turn comes again. */
    game.combat.turn_index = 3;
    KEYS("D");
    CHECK(cok_combat_done(&game, a, &done) && game.combat.turn[3] == a);
    CHECK(game.combat.turn[4] == NULL);
    CHECK(game.combat.turn_index == 2 && game.combat.turn[1] == b);
    /* At entry 72 (0x48) the order is full: no moving. */
    for (unsigned i = 1; i <= COK_COMBATANTS; ++i) game.combat.turn[i] = i % 2 ? b : c;
    game.combat.turn_index = 0x47;
    KEYS("D");
    CHECK(cok_combat_done(&game, a, &done));
    CHECK(game.combat.turn[0x47] == c && game.combat.turn[0x48] == a);
    CHECK(game.combat.turn_index == 0x46);
    game.combat.turn_index = 0x48;
    game.combat.turn[0x48] = b;
    KEYS("D");
    CHECK(cok_combat_done(&game, a, &done) && game.combat.turn[0x48] == a);
    CHECK(game.combat.turn_index == 0x47);
    memset(game.combat.turn, 0, sizeof game.combat.turn);
    /* Quit: the turn ends. */
    a->combat->initiative = 5;
    a->combat->spell = 3;
    KEYS("Q");
    CHECK(cok_combat_done(&game, a, &done) && done && a->combat->initiative == 0 &&
          a->combat->spell == 0);
    /* Exit and Escape go back, the turn going on. */
    a->combat->initiative = 5;
    KEYS("E");
    CHECK(cok_combat_done(&game, a, &done) && !done);
    KEYS("\x1b");
    CHECK(cok_combat_done(&game, a, &done) && !done && a->combat->initiative == 5);
    /* Bandage, offered while a party member is dying: the first is
     * bandaged, and the turn ends. */
    b->record[0x188] = 5;
    b->record[0x189] = 0;
    b->combat->dying = 3;
    s.log[0] = '\0';
    KEYS("B");
    CHECK(cok_combat_done(&game, a, &done) && done);
    CHECK(LOGGED("menu: Guard Delay Quit Bandage Auto Speed Exit;"));
    CHECK(b->record[0x188] == 4 && b->combat->dying == 0 && a->combat->initiative == 0);
    /* Special keys count as letters: F8 (scan 0x42) is B, Bandage, ending
     * the turn though none is offered; Home (0x47) is G, Guard. */
    a->combat->initiative = 5;
    s.log[0] = '\0';
    KEYS("\x00\x42");
    CHECK(cok_combat_done(&game, a, &done) && done && a->combat->initiative == 0);
    CHECK(!LOGGED("Bandage"));
    a->combat->initiative = 5;
    KEYS("\x00\x47");
    CHECK(cok_combat_done(&game, a, &done) && done && a->combat->guarding == 1);
    /* No Guard with a missile weapon not thrown. */
    reset();
    a = record('A', 0, 10, 10);
    turn_of(a, 1);
    give(a, 0x17, true);
    KEYS("E");
    CHECK(cok_combat_done(&game, a, &done) && LOGGED("menu: Delay Quit Auto Speed Exit;"));
    /* Auto: the computer has it (3995:1604) and plays the rest of the
     * turn at once, after 200 ms: here it attacks the monster beside it. */
    reset();
    a = record('A', 0, 10, 10);
    c = record('C', 1, 11, 10);
    turn_of(a, 1);
    give(a, 0x12, true);
    log_flushes = true;
    KEYS("A");
    CHECK(cok_combat_done(&game, a, &done) && done && a->record[0x18b] == 1);
    CHECK(LOGGED("wait: 200;") && LOGGED("print: A;print: Attacks;") && c->combat->hits == 1);
    /* 3995:145f: the menu's flush, then Auto's before its 200 ms. */
    CHECK(LOGGED("flush;flush;wait: 200;"));
    log_flushes = false;
    /* A thrown weapon allows Guard. */
    reset();
    a = record('A', 0, 10, 10);
    turn_of(a, 1);
    give(a, 0x06, true);
    KEYS("E");
    CHECK(cok_combat_done(&game, a, &done) && LOGGED("menu: Guard Delay Quit Auto Speed Exit;"));
}

static void test_speed(void)
{
    reset();
    game.speed = 0;
    /* Slower, then Faster, Exit. */
    KEYS("SSFE");
    cok_combat_speed(&game);
    CHECK(game.speed == 1);
    CHECK(LOGGED("menu:  Slower Exit;menu:  Slower Faster Exit;menu:  Slower Faster Exit;"));
    game.speed = 9;
    s.log[0] = '\0';
    KEYS("F\x1b");
    cok_combat_speed(&game);
    CHECK(game.speed == 8 && LOGGED("menu:  Faster Exit;menu:  Slower Faster Exit;"));
    /* Del (scan 0x53, S) slows past 9, as a byte. */
    game.speed = 9;
    KEYS("\x00\x53" "E");
    cok_combat_speed(&game);
    CHECK(game.speed == 10);
    game.speed = 0;
}

static void test_cast(void)
{
    uint8_t camp = find_spell(camp_only), quick = find_spell(quick_spell),
            slow = find_spell(slow_spell);
    reset();
    cok_character *a = record('A', 0, 10, 10), *b = record('B', 0, 11, 10),
                  *c = record('C', 1, 20, 10), *d = record('D', 1, 21, 10);
    turn_of(a, 1);
    game.combat.turn[2] = b;
    game.combat.turn[3] = c;
    game.combat.turn[4] = d;
    a->record[0xf9] = 1;
    a->record[0x1e] = camp;
    bool done = true;
    /* A spell only for camp: "Camp Only Spell", nothing cast. */
    KEYS("C");
    CHECK(cok_combat_cast(&game, &done, false, 0) && !done);
    CHECK(LOGGED("list: Spells in Memory;") && LOGGED("print: Camp Only Spell;"));
    CHECK(a->combat->initiative == 5 && !LOGGED("Casts a Spell"));
    /* None picked. */
    KEYS("\x1b");
    CHECK(cok_combat_cast(&game, &done, false, 0) && !done);
    /* At once (time 0): cast, and forgotten; the turn ends. */
    s.log[0] = '\0';
    a->record[0x1e] = quick;
    KEYS("C");
    CHECK(cok_combat_cast(&game, &done, false, 0) && done && a->combat->initiative == 0);
    CHECK(LOGGED("print: Casts a Spell;") && a->record[0x1e] == 0);
    /* A spell that takes time: "Begins Casting", kept for the next turn,
     * the initiative down by the time in thirds, and the caster moved down
     * the order past those of more initiative, and of as much where a d2
     * gives 1. */
    uint8_t t = (uint8_t)(spell_byte(slow, 12) / 3);
    a->combat->initiative = (int8_t)(t + 3);
    b->combat->initiative = 4;
    c->combat->initiative = 3;
    d->combat->initiative = 3;
    game.combat.turn_index = 1;
    s.log[0] = '\0';
    game.vm.seed = seed_seq(2, (const unsigned[]){2, 1, 1, 2, 2, 2});
    uint32_t start = game.vm.seed;
    CHECK(cok_combat_cast(&game, &done, true, slow) && done);
    CHECK(LOGGED("print: Begins Casting;") && a->combat->spell == slow);
    CHECK(a->combat->initiative == 3 && game.combat.turn_index == 0);
    CHECK(game.combat.turn[1] == b && game.combat.turn[2] == c && game.combat.turn[3] == a &&
          game.combat.turn[4] == d);
    cok_tp_random(&start, 2);
    cok_tp_random(&start, 2);
    CHECK(game.vm.seed == start);
    /* Not past one of less initiative, and no d2. */
    game.combat.turn[1] = a;
    game.combat.turn[2] = b;
    game.combat.turn[3] = NULL;
    game.combat.turn_index = 1;
    a->combat->initiative = (int8_t)(t + 5);
    start = game.vm.seed;
    CHECK(cok_combat_cast(&game, &done, true, slow) && a->combat->initiative == 5);
    CHECK(game.combat.turn[1] == a && game.vm.seed == start && game.combat.turn_index == 0);
    /* No more initiative than the time: 1. */
    a->combat->initiative = (int8_t)t;
    game.combat.turn_index = 1;
    CHECK(cok_combat_cast(&game, &done, true, slow) && a->combat->initiative == 1);
    CHECK(game.combat.turn[1] == b && game.combat.turn[2] == a);
    /* Moving past entry 72 reads DS:72ce as a record: the run stops. */
    for (unsigned i = 1; i <= COK_COMBATANTS; ++i) game.combat.turn[i] = b;
    game.combat.turn_index = 1;
    a->combat->initiative = (int8_t)(t + 1);
    CHECK(!cok_combat_cast(&game, &done, true, slow) && game.vm.status == COK_ECL_UNDEFINED);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    memset(game.combat.turn, 0, sizeof game.combat.turn);
    /* At its next turn the spell is cast: Bless, aimed at A's own cell
     * (Target), A and B beside it blessed. */
    s.log[0] = '\0';
    a->combat->spell = slow;
    a->combat->initiative = 1;
    a->record[0x1e] = slow;
    KEYS("T");
    CHECK(cok_combat_commands(&game, a, 0) && a->combat->spell == 0);
    CHECK(LOGGED("print: A;print: is Blessed;") && LOGGED("print: B;print: is Blessed;"));
    CHECK(a->combat->initiative == 0 && a->record[0x1e] == 0);
}

static void test_yell(void)
{
    reset();
    cok_character *k = record('K', 0, 10, 10), *a = record('A', 1, 11, 10),
                  *b = record('B', 1, 12, 10), *c = record('C', 1, 13, 10),
                  *d = record('D', 1, 14, 10), *f = record('F', 0, 15, 10),
                  *g = record('G', 1, 16, 10);
    k->record[0x5a] = 5;
    a->record[0xd4] = b->record[0xd4] = c->record[0xd4] = d->record[0xd4] = 30;
    f->record[0xd4] = g->record[0xd4] = 30;
    b->record[0x13] = 3;   /* too dull to be taunted */
    c->record[0x188] = 1;  /* not okay */
    CHECK(cok_character_add_effect(d, 0x33, 0, 0, false) != NULL); /* helpless */
    g->record[0xd4] = 0;   /* always saves (but a 1) */
    game.vm.seed = seed_seq(2, (const unsigned[]){20, 2, 19, 20, 2, 19});
    uint32_t start = game.vm.seed;
    CHECK(cok_combat_yell(&game, k));
    CHECK(game.combat.yelled == k->record);
    cok_effect *e = cok_character_find_effect(a, 0x5b);
    CHECK(e != NULL && e->value == 0xff && e->duration == 0);
    CHECK(cok_character_find_effect(b, 0x5b) == NULL && cok_character_find_effect(c, 0x5b) == NULL);
    CHECK(cok_character_find_effect(d, 0x5b) == NULL && cok_character_find_effect(f, 0x5b) == NULL);
    CHECK(cok_character_find_effect(g, 0x5b) == NULL);
    CHECK(LOGGED("print: wants to kill K;"));
    /* A's and G's saving throws only. */
    cok_tp_random(&start, 20);
    cok_tp_random(&start, 20);
    CHECK(game.vm.seed == start);
}

static void test_use(void)
{
    uint8_t quick = find_spell(quick_spell), camp = find_spell(camp_only);
    reset();
    cok_character *a = record('A', 0, 10, 10);
    record('M', 1, 20, 10);
    turn_of(a, 1);
    a->record[0xf9] = 1;
    size_t wand = give(a, 0x45, true);
    a->items[wand - 1][0x3d] = quick;
    a->items[wand - 1][0x3c] = 3;
    give(a, 0x12, false);
    /* Use, offered in combat while it may use items this round: the item
     * used ends the turn, and the Items menu with it; its spell is cast
     * (not ported: logged) and a charge used. */
    KEYS("UU");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("menu: Ready Use Drop Halve Join;") && LOGGED("print: uses an item;"));
    CHECK(LOGGED("print: Item:;"));
    CHECK(LOGGED("print: A;print: is affected;") && a->items[wand - 1][0x3c] == 2 &&
          a->combat->initiative == 0);
    CHECK(s.at == s.length && !game.vm.abort);
    /* A spell only for camp: cast (Read Magic, "is affected") and a charge
     * used, the turn going on (byte 11 is 0), but the cast's target sets
     * what Use and the turn's menus share, so the menus end. */
    a->combat->initiative = 5;
    a->items[wand - 1][0x3d] = camp;
    s.log[0] = '\0';
    KEYS("UU");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("print: A;print: is affected;") && a->items[wand - 1][0x3c] == 1 &&
          s.at == s.length && a->combat->initiative == 5);
    /* No Use when it may not use items this round, nor in View. */
    a->combat->may_use = 0;
    a->combat->initiative = 5;
    s.log[0] = '\0';
    KEYS("UEDQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("menu: Ready Drop Halve Join;") && !LOGGED("uses"));
    /* View: Items Spells Drop Exit as it has them, no Trade in combat. */
    a->combat->initiative = 5;
    a->record[0xeb + 8] = 5;
    a->record[0x1e] = quick;
    s.log[0] = '\0';
    KEYS("VEDQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("menu: Items Spells Drop Exit;") && a->combat->initiative == 0);
}

/* 3995:040b hands the player's turn to the commands, the computer's to
 * the computer, as eclplay's --combat says. */
static void test_turns(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10), *m = record('M', 1, 11, 10);
    turn_of(a, 1);
    give(a, 0x12, true);
    /* The player's: the menu, here Aim at M, attacking it. */
    KEYS("ANT");
    CHECK(cok_combat_turn(&game, a));
    CHECK(LOGGED("turn: A (initiative 5);menu: Move View Aim Use Done;") && m->combat->hits == 1);
    /* View recomputes the attacks (432f:0e80) and redraws the screen: the
     * panel is due no more. */
    a->combat->initiative = 5;
    a->record[0x18f] = 0x33;
    KEYS("VEDQ");
    CHECK(cok_combat_turn(&game, a) && a->record[0x18f] == 1);
    /* The computer's turns are the computer's. */
    s.log[0] = '\0';
    m->combat->initiative = 3;
    CHECK(cok_combat_turn(&game, m) && LOGGED("turn: M (initiative 3);") && !LOGGED("menu"));
    /* With the default the player's turn passes, its first item selected. */
    game.combat_stub = COK_COMBAT_UNPORTED;
    a->combat->initiative = 5;
    s.log[0] = '\0';
    game.selected = 4;
    CHECK(cok_combat_turn(&game, a) && a->combat->initiative == 0 && !LOGGED("menu"));
    CHECK(game.selected == 1);
    /* --combat auto presses Alt-Q: the turn given back to the computer. */
    game.combat_stub = COK_COMBAT_AUTO;
    a->combat->initiative = 5;
    game.combat.turn_index = 1;
    CHECK(cok_combat_turn(&game, a) && a->combat->initiative == 0x14 && a->record[0x18b] == 1);
    CHECK(game.combat.turn_index == 0);
    /* --combat pass passes the computer's too. */
    game.combat_stub = COK_COMBAT_PASS;
    m->combat->initiative = 3;
    s.log[0] = '\0';
    CHECK(cok_combat_turn(&game, m) && m->combat->initiative == 0 && !LOGGED("sound"));
    game.combat_stub = COK_COMBAT_PLAYER;
}

/* What Move and aiming do on the way: the cloud under a step, a drop or
 * helplessness ending the turn, sweeps, untargetable ones, the bounds of
 * the cursor, and where Exit and Escape leave the map. */
static void test_more(void)
{
    reset();
    cok_character *a = record('A', 0, 10, 10);
    turn_of(a, 1);
    bool done = false;
    /* A puddle under it after the step: 1d6 (60f4:0dc3). */
    game.combat.cells[10][11] = 0x1d;
    KEYS("6D");
    uint8_t hp = a->record[0x197];
    CHECK(cok_combat_move(&game, a, &done) && LOGGED("print: A;print: takes"));
    CHECK(a->record[0x197] < hp && !LOGGED("unported"));
    /* A green cloud: it throws (60f4:0dc3 with the flag). */
    game.combat.cells[10][12] = 0x1e;
    a->combat->movement = 12;
    a->combat->initiative = 5;
    KEYS("6D");
    CHECK(cok_combat_move(&game, a, &done));
    CHECK(LOGGED("print: A;print: starts to cough;") || LOGGED("print: A;print: chokes"));
    /* Helpless after a step: the turn ends there. */
    CHECK(cok_character_add_effect(a, 0x33, 0, 0, false) != NULL);
    a->combat->movement = 12;
    KEYS("6D");
    CHECK(cok_combat_move(&game, a, &done) && done && a->combat->initiative == 0);
    CHECK(s.at == 1);
    /* Dropped by the attack of opportunity: the turn ends, no step. */
    bool dropped = false;
    for (uint32_t seed = 1; seed < 400 && !dropped; ++seed) {
        reset();
        a = record('A', 0, 10, 10);
        cok_character *m = record('M', 1, 11, 10);
        turn_of(a, 1);
        a->record[0x197] = 1;
        m->record[0x18f] = 2;
        game.vm.seed = seed;
        KEYS("4D");
        CHECK(cok_combat_move(&game, a, &done));
        dropped = a->record[0x189] == 0;
    }
    CHECK(dropped && done && game.combat.combatant[1].x == 10 && a->combat->initiative == 0);
    /* Dropped by a guard beside where it steps (432f:068f): the turn ends
     * there, no key read after. */
    dropped = false;
    for (uint32_t seed = 1; seed < 400 && !dropped; ++seed) {
        reset();
        a = record('A', 0, 10, 10);
        cok_character *g = record('G', 1, 12, 10);
        turn_of(a, 1);
        a->record[0x197] = 1;
        g->combat->guarding = 1;
        g->record[0x18f] = 2;
        game.vm.seed = seed;
        KEYS("6");
        CHECK(cok_combat_move(&game, a, &done));
        dropped = a->record[0x189] == 0;
    }
    CHECK(dropped && done && !game.vm.abort && a->combat->initiative == 0);
    /* "Flee:" with the keys running out: no flight. */
    reset();
    a = record('A', 0, 0, 10);
    turn_of(a, 1);
    KEYS("4");
    CHECK(cok_combat_move(&game, a, &done) && game.vm.abort && a->record[0x189] == 1);
    /* Moving into one with a thrown weapon attacks it; a sweep of one
     * below 1 hit die sets the turn over, its initiative left. */
    reset();
    a = record('A', 0, 10, 10);
    cok_character *m = record('M', 1, 11, 10);
    turn_of(a, 1);
    give(a, 0x06, true);
    KEYS("6");
    CHECK(cok_combat_move(&game, a, &done) && done && m->combat->hits == 1);
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    cok_character *n = record('N', 1, 11, 11);
    turn_of(a, 1);
    give(a, 0x12, true);
    m->record[0xd6] = n->record[0xd6] = 0;
    a->combat->sweeps = 3;
    a->record[0x18f] = 1;
    KEYS("6");
    CHECK(cok_combat_move(&game, a, &done) && done && LOGGED("sweeps"));
    CHECK(m->combat->hits == 1 && n->combat->hits == 1);
    /* Aim's sweep ends the turn. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    n = record('N', 1, 11, 11);
    turn_of(a, 1);
    give(a, 0x12, true);
    m->record[0xd6] = n->record[0xd6] = 0;
    a->combat->sweeps = 3;
    a->record[0x18f] = 1;
    cok_combat_aimed res;
    KEYS("NT");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && done && LOGGED("sweeps"));
    CHECK(a->combat->initiative == 0);
    /* One that cannot be attacked (blinking, 0x25, with its turn over) is
     * not among the targets. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    cok_character *f = record('F', 1, 13, 10);
    turn_of(a, 1);
    CHECK(cok_character_add_effect(m, 0x25, 0, 0, false) != NULL);
    m->combat->initiative = 0;
    KEYS("NT");
    CHECK(cok_combat_aim(&game, a, 4, 1, 0, 1, &done, &res) && res.target == f->record);
    /* The hand axe's range is 3: F at 4 is out of it. */
    reset();
    a = record('A', 0, 10, 10);
    f = record('F', 1, 14, 10);
    turn_of(a, 1);
    give(a, 0x06, true);
    KEYS("NE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !LOGGED("Target"));
    /* The cursor stays on the map to the east and south too. */
    reset();
    a = record('A', 0, 47, 22);
    cok_character *e = record('E', 1, 49, 24);
    turn_of(a, 1);
    KEYS("M666333T");
    CHECK(cok_combat_aim(&game, a, 9, 0, 0, 1, &done, &res) && res.target == e->record);
    CHECK(res.x == 49 && res.y == 24);
    /* With a bow, the cursor on an enemy is valid only with none beside. */
    reset();
    a = record('A', 0, 10, 10);
    m = record('M', 1, 11, 10);
    f = record('F', 1, 14, 10);
    turn_of(a, 1);
    give(a, 0x17, true);
    size_t arrows = give(a, 0x1e, true);
    a->items[arrows - 1][0x39] = 5;
    KEYS("M6666\x1b" "E");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !LOGGED("menu: Target;"));
    game.combat.combatant[2].x = 30;
    CHECK(cok_combat_occupy(&game.combat));
    s.log[0] = '\0';
    KEYS("M6666T");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && res.target == f->record);
    /* Without ammunition, never. */
    a->items[arrows - 1][0x34] = 0;
    char why[300];
    CHECK(cok_character_stats(a, &cok_rules_champions, &game.item_types, why, sizeof why));
    s.log[0] = '\0';
    KEYS("M6666\x1b" "E");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && !LOGGED("menu: Target;"));
    /* Exit leaves the map centred on the target shown, Escape on the
     * first (the attacker). */
    reset();
    a = record('A', 0, 5, 5);
    f = record('F', 1, 40, 20);
    turn_of(a, 1);
    KEYS("NE");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res));
    CHECK(game.combat.view_x == 37 && game.combat.view_y == 17);
    KEYS("N\x1b");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res));
    CHECK(game.combat.view_x == 2 && game.combat.view_y == 2);
    /* Use selects the second item (DS:6e0f), but the list's menu
     * (67b5:1368) starts from the first again: Enter readies. */
    uint8_t quick = find_spell(quick_spell);
    reset();
    a = record('A', 0, 10, 10);
    turn_of(a, 1);
    a->record[0xf9] = 1;
    size_t wand = give(a, 0x45, true);
    a->items[wand - 1][0x3d] = quick;
    a->items[wand - 1][0x3c] = 3;
    KEYS("U\r");
    CHECK(cok_combat_commands(&game, a, 0) && LOGGED("choice: R;") && !LOGGED("uses"));
    /* Cast redraws the screen, the caster's moves shown. */
    reset();
    a = record('A', 0, 10, 10);
    turn_of(a, 1);
    a->record[0xf9] = 1;
    a->record[0x1e] = quick;
    game.combat.show_actions = false;
    KEYS("\x1b");
    CHECK(cok_combat_cast(&game, &done, false, 0) && game.combat.show_actions);
    game.combat.show_actions = false;
    KEYS("\x1b");
    CHECK(cok_combat_cast(&game, &done, true, 0) && !game.combat.show_actions);
}

/* What the review's mutants showed the tests did not look at. */
static void test_review(void)
{
    /* A scan code outside both sets (0x11, Alt-W) is none: 08d5 reads on,
     * the selection (View, after 6) kept, so Enter Views. */
    reset();
    cok_character *a = record('A', 0, 5, 5);
    turn_of(a, 1);
    KEYS("6\x00\x11\rEDQ");
    CHECK(cok_combat_commands(&game, a, 0));
    CHECK(LOGGED("menu: Move View Aim Done;menu: Move View Aim Done;print: A;"));
    /* The menu's row is cleared once a command is read: after Yell,
     * which writes nothing there, row 24 is blank. */
    reset();
    a = record('A', 0, 5, 5);
    turn_of(a, 1);
    a->record[0x5a] = 5;
    KEYS("Y");
    CHECK(cok_combat_commands(&game, a, 0) && blank_row(24));
    /* Aim's distance is worked out with sight not blocked (map +6): A, 2
     * by 2, sees F from its lower cells, but from its top-left cell a wall
     * stands between them; "Range = 4" all the same, not 2. */
    reset();
    a = record('A', 0, 10, 10);
    cok_character *f = record('F', 1, 14, 10);
    turn_of(a, 1);
    a->record[0xcf] = 4;
    game.combat.combatant[1].size = 4;
    CHECK(cok_combat_occupy(&game.combat));
    game.combat.cells[10][12] = terrain_of_cost(0xff);
    bool done;
    cok_combat_aimed res;
    KEYS("N");
    CHECK(cok_combat_aim(&game, a, 4, 1, 0, 1, &done, &res) && game.vm.abort);
    CHECK(shows(0, 0x17, "Range = 4  ", 10));
    game.vm.abort = game.input_ended = false;
    /* A thrown weapon at range is thrown: the hand axe leaves the hand
     * (1a45 uses it up, into the pool). */
    reset();
    a = record('A', 0, 10, 10);
    f = record('F', 1, 12, 10);
    turn_of(a, 1);
    give(a, 0x06, true);
    KEYS("NT");
    CHECK(cok_combat_aim(&game, a, 0xff, 1, 0, 1, &done, &res) && res.target == f->record);
    CHECK(a->item_count == 0);
    /* Use in combat: "Item:" and the item's name on row 23, there while
     * the spell is aimed (the keys run out there), until the cast's end
     * clears the row. */
    reset();
    a = record('A', 0, 10, 10);
    record('F', 1, 12, 10);
    turn_of(a, 1);
    a->record[0xf9] = 1;
    size_t wand = give(a, 0x45, true);
    a->items[wand - 1][0x3d] = 0x0f;
    KEYS("UU");
    CHECK(cok_combat_commands(&game, a, 0) && shows(0, 0x17, "Item:", 10));
    /* The cell of no target with an earlier battle's record left in the
     * entry after the last combatant: the original reads entry 0's cell,
     * other data, and the run stops. Two setups, the second with fewer
     * monsters. */
    reset();
    record('A', 0, 0, 0);
    record('M', 1, 0, 0);
    record('N', 1, 0, 0);
    record('O', 1, 0, 0);
    game.vm.mode = 4;
    game.vm.mem7c00[0x33e] = 1;
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(cok_combat_setup(&game) && cok_combat_end(&game));
    uint8_t first = game.combat.count;
    bool held[COK_COMBATANTS + 1];
    memcpy(held, game.combat.entry_held, sizeof held);
    reset(); /* a new battle: DS:68d1 is not cleared */
    memcpy(game.combat.entry_held, held, sizeof held);
    cok_character *b = record('B', 0, 0, 0);
    record('P', 1, 0, 0);
    game.vm.mode = 4;
    game.vm.mem7c00[0x33e] = 1;
    game.vm.character = b->record;
    game.combat.count = 1;
    CHECK(cok_combat_setup(&game));
    CHECK(game.combat.count < first && game.combat.entry_held[game.combat.count]);
    turn_of(b, 1);
    KEYS("M6\x1b\x00\x50");
    CHECK(!cok_combat_aim(&game, b, 0xff, 1, 0, 1, &done, &res));
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "earlier battle") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* With none left there (a first battle), the entry's own cell. */
    game.combat.entry_held[game.combat.count] = false;
    KEYS("M6\x1b\x00\x50" "E");
    CHECK(cok_combat_aim(&game, b, 0xff, 1, 0, 1, &done, &res));
    CHECK(cok_combat_end(&game));
    game.vm.mode = 5;
}

int main(void)
{
    cok_keyboard k = {scripted, &s, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .delay = on_delay, .flush = on_flush,
                                 .context = &s};
    if (!cok_adventure_open(&game, &cok_rules_champions, "COKAssets", &k, &hooks)) {
        puts("command: COKAssets not found; tests skipped");
        return 0;
    }
    load_screen();
    test_text();
    test_menu();
    test_hidden_keys();
    test_move();
    test_charge();
    test_ally();
    test_aim();
    test_aim_missile();
    test_manual();
    test_done();
    test_speed();
    test_cast();
    test_yell();
    test_use();
    test_turns();
    test_more();
    test_review();
    cok_party_free(&game.party);
    cok_adventure_close(&game);
    puts("command tests passed");
    return 0;
}
