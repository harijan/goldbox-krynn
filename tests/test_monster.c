#include "monster.h"

#include "camp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[8192];
} script;

/* The screen when the last key was read. */
static uint8_t shown[40 * 4 * 200];
static const cok_picture *watched;

static int scripted(void *context)
{
    script *s = context;
    if (watched != NULL) memcpy(shown, watched->pixels, sizeof shown);
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static void unported(cok_adventure *game, void *context)
{
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "[%s];", cok_ecl_opcode_name(game->vm.opcode));
}

static script s;
static cok_adventure game;

static void open_game(void)
{
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .unported = unported, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    watched = &game.screen;
}

static void reset(const char *k)
{
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    game.vm.character = game.vm.saved_character = NULL;
    game.vm.restore_character = false;
    game.combat_stub = COK_COMBAT_UNPORTED;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    game.vm.mode = 4;
    game.vm.mem4b00[0xe6] = 1;
    game.vm.mem7c00[0x2e1] = 0xff; /* no portrait */
    game.vm.file = 1;
    game.monsters = game.undead = 0;
    game.monsters_loaded = false;
    game.icon_slot = 8;
    game.sprite_loaded = game.closeup_shown = game.sprite_shown = false;
    game.picture_shown = false;
    game.quit = false;
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.log[0] = '\0';
    game.input_ended = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
}

/* Run one instruction: code bytes, then EXIT. */
static cok_ecl_status run(const uint8_t *code, size_t length)
{
    memset(game.vm.code, 0, sizeof game.vm.code);
    memcpy(game.vm.code, code, length);
    game.vm.size = length + 1;
    game.vm.depth = 0;
    return cok_ecl_run(&game.vm, COK_ECL_BASE);
}

#define RUN(...) run((const uint8_t[]){__VA_ARGS__}, sizeof (const uint8_t[]){__VA_ARGS__})

static cok_character *member(const char *name, uint8_t movement)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    c->record[0x198] = movement;
    c->record[0x189] = 1;
    CHECK(cok_party_add(&game.party, c));
    ++game.vm.mem7c00[0x33e];
    return c;
}

static void test_load_monster(void)
{
    reset("");
    member("KAL", 12);
    /* LOAD MONSTER 9 3 12: three goblins with the hobgoblins' icons. */
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 3, 0, 12) == COK_ECL_OK);
    CHECK(strstr(s.log, "monster: 3 GOBLIN, icon 12 in slot 8;") != NULL);
    CHECK(game.party.count == 4 && game.vm.mem7c00[0x33e] == 1);
    CHECK(game.monsters == 3 && game.icon_slot == 9 && game.monsters_loaded);
    CHECK(game.icons[8][0].pixels != NULL && game.icons[8][1].pixels != NULL);
    cok_character *first = game.party.members[1], *copy = game.party.members[3];
    CHECK(first->record[0x137] == 8 && copy->record[0x137] == 8);
    CHECK(first->record[0x197] == 4 && first->record[0x62] == 4);
    /* The copies hold the first one's items in the reverse order. */
    CHECK(first->item_count == 2 && copy->item_count == 2);
    CHECK(memcmp(first->items[0], copy->items[1], COK_ITEM_SIZE) == 0);
    CHECK(memcmp(first->items[1], copy->items[0], COK_ITEM_SIZE) == 0);
    CHECK(first->items[0][0x2e] != first->items[1][0x2e]);
    /* So do its effects: the skeleton's are 0x60 0x61 0x62 0x64 0x65. */
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 26, 0, 2, 0, 26) == COK_ECL_OK);
    CHECK(game.undead == 1 && game.icon_slot == 10 && game.party.count == 6);
    const cok_effect *e = game.party.members[4]->effects, *f = game.party.members[5]->effects;
    CHECK(e != NULL && e->id == 0x60 && f != NULL && f->id == 0x65);
    while (e->next != NULL) e = e->next;
    CHECK(e->id == 0x65);
    /* A count of 0 loads one; 63 records are the most between
     * CLEARMONSTERS. */
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 8, 0, 0, 0, 8) == COK_ECL_OK);
    CHECK(game.party.count == 7 && game.monsters == 6);
    game.monsters = 62;
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 8, 0, 5, 0, 8) == COK_ECL_OK);
    CHECK(game.party.count == 8 && game.monsters == 63 && game.icon_slot == 12);
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 8, 0, 5, 0, 8) == COK_ECL_OK);
    CHECK(game.party.count == 8 && game.icon_slot == 12);
    CHECK(strstr(s.log, "monster: none, 63 are loaded;") != NULL);
    /* CLEARMONSTERS forgets them but leaves them in the list. */
    game.pool.coins[3] = 7;
    CHECK(RUN(COK_ECL_CLEARMONSTERS) == COK_ECL_OK);
    CHECK(game.monsters == 0 && game.undead == 0 && !game.monsters_loaded);
    CHECK(game.icon_slot == 8 && game.pool.coins[3] == 0 && game.party.count == 8);
}

static void test_difficulty(void)
{
    /* Hit points scale by (d + 1) / 4 as bytes, either at 0 making both 1;
     * a monster on the party's side (KILDIRF, +0x18a 0) is not scaled. */
    static const struct { uint16_t level; uint8_t id, hp; } cases[] = {
        {1, 21, 16}, {2, 21, 24}, {3, 21, 33}, {4, 21, 41}, {5, 21, 49}, {6, 21, 33},
        {0, 21, 33}, {1, 8, 1}, {5, 15, 12},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        reset("");
        member("KAL", 12);
        game.vm.mem4b00[0x1f4] = cases[i].level;
        CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, cases[i].id, 0, 1, 0, 1) == COK_ECL_OK);
        const uint8_t *r = game.party.members[1]->record;
        CHECK(r[0x197] == cases[i].hp && r[0x62] == cases[i].hp);
    }
}

static void test_missing_monster(void)
{
    reset("\r");
    member("KAL", 12);
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 2, 0, 1, 0, 1) == COK_ECL_OK);
    CHECK(game.quit && game.vm.abort && game.party.count == 1);
    CHECK(strstr(s.log, "print: Unable to load monster;quit: to DOS;") != NULL);
    /* An icon slot past the table stops the port. */
    reset("");
    member("KAL", 12);
    game.icon_slot = COK_ICON_SLOTS;
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 8, 0, 1, 0, 8) == COK_ECL_UNDEFINED);
}

static void test_open_squares(void)
{
    reset("");
    uint8_t map[0x402] = {0};
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    game.view.wrap = true;
    CHECK(cok_monster_open_squares(&game, 7, 7, 0) == 2);
    /* A wall on the north side of the square ahead stops it at one. */
    map[2 + 6 * 16 + 7] = 0x10;
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    CHECK(cok_monster_open_squares(&game, 7, 7, 0) == 1);
    /* Any wall stops it, doors too: an east side. */
    map[2 + 7 * 16 + 7] = 0x03;
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    CHECK(cok_monster_open_squares(&game, 7, 7, 2) == 0);
    /* Outside 3D areas it is 2, stored in 0x7ec1 too. */
    game.vm.mem4b00[0xe6] = 0;
    CHECK(cok_monster_open_squares(&game, 7, 7, 0) == 2 && game.vm.mem7c00[0x2c1] == 2);
}

static void test_movement(void)
{
    reset("");
    uint8_t fast, slow;
    CHECK(!cok_monster_movement(&game.party, &fast, &slow));
    cok_character *a = member("A", 6), *b = member("B", 12), *c = member("C", 9);
    (void)b;
    CHECK(cok_monster_movement(&game.party, &fast, &slow) && fast == 12 && slow == 6);
    /* Haste doubles, slow halves, from the first's own movement. */
    CHECK(cok_character_add_effect(a, 0x27, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(c, 0x2a, 0, 0, false) != NULL);
    CHECK(cok_monster_movement(&game.party, &fast, &slow) && fast == 12 && slow == 4);
    a->record[0x198] = 200;
    CHECK(cok_monster_movement(&game.party, &fast, &slow) && fast == 200 && slow == 4);
}

static void test_sprite(void)
{
    reset("");
    member("KAL", 12);
    uint8_t map[0x402] = {0};
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    game.view.wrap = true;
    game.vm.map_x = game.vm.map_y = 7;
    /* SETUP MONSTER 35 2 41, APPROACH, APPROACH, APPROACH in one run. */
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 2, 0, 41, COK_ECL_APPROACH, COK_ECL_APPROACH,
              COK_ECL_APPROACH) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x2c1] == 0 && game.picture_id == 41 && !game.picture_sprite);
    CHECK(strcmp(s.log, "monster: sprite 35 at 2;monster: sprite 35 at 1;"
                        "monster: sprite 35 at 0;monster: picture 41;") == 0);
    /* EXIT forgets the sprite and the close-up (DS:8830, 8831). */
    CHECK(!game.sprite_loaded && !game.closeup_shown && game.sprite_shown);
    /* A wall ahead keeps it at the distance it can be; the sprite loads
     * once, then each move redraws the view first. */
    reset("");
    map[2 + 6 * 16 + 7] = 0x10;
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 2, 0, 41) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x2c1] == 1 && game.vm.mem7c00[0x2c0] == 2);
    CHECK(game.picture_sprite && game.picture_id == 35 && game.frame_count == 3);
    /* SPRITE OFF erases it, and so does PICTURE 255. */
    CHECK(RUN(COK_ECL_SPRITE_OFF) == COK_ECL_OK && !game.sprite_shown);
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 2, 0, 41) == COK_ECL_OK && game.sprite_shown);
    game.vm.last_mode = 4;
    CHECK(RUN(COK_ECL_PICTURE, 0, 0xff) == COK_ECL_OK && !game.sprite_shown);
    /* Outside 3D mode nothing shows; with no 3D view nothing loads. */
    reset("");
    game.vm.mode = 3;
    game.vm.mem4b00[0xe6] = 0;
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 41) == COK_ECL_OK);
    CHECK(s.log[0] == '\0' && !game.sprite_loaded && game.vm.mem7c00[0x2c1] == 0);
    /* The first sprite turns the overhead map off and shows the view, in
     * any area (3775:0575); once the sprite is loaded, the redraw that
     * erases it shows the map. */
    reset("");
    game.overhead = true;
    game.vm.map_x = game.vm.map_y = 7;
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 2, 0, 41) == COK_ECL_OK && !game.overhead);
    CHECK(strcmp(s.log, "area: off;monster: sprite 35 at 1;") == 0);
    reset("");
    game.overhead = true;
    game.sprite_loaded = true;
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 2, 0, 41) == COK_ECL_OK && game.overhead);
    reset("");
    game.overhead = true;
    game.vm.mode = 3;
    game.vm.mem4b00[0xe6] = 0;
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 41) == COK_ECL_OK && !game.overhead);
    CHECK(strcmp(s.log, "area: off;") == 0 && !game.sprite_loaded);
    /* A distance past 2 quits to DOS (6961:072e). */
    reset("");
    game.vm.mem7c00[0x2c1] = 4;
    CHECK(RUN(COK_ECL_APPROACH) == COK_ECL_OK && game.quit);
    CHECK(strcmp(s.log, "print: Illegal range in Show3DSprite.;quit: to DOS;") == 0);
    /* A portrait close-up is not ported. */
    reset("");
    game.vm.mem7c00[0x2e1] = 3;
    game.view_replaced = true;
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 41) == COK_ECL_OK);
    CHECK(strstr(s.log, "unported: the portrait close-up (3775:0538);") != NULL);
    CHECK(!game.view_replaced);
    /* A close-up shown stays until 0x7ee1 changes (DS:8854). */
    reset("");
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 41, COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 41,
              COK_ECL_SAVE, 0, 3, 1, 0xe1, 0x7e, COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0,
              41) == COK_ECL_OK);
    CHECK(strcmp(s.log, "monster: sprite 35 at 0;monster: picture 41;"
                        "unported: the portrait close-up (3775:0538);") == 0);
}

/* ENCOUNTER MENU 35 2 41 [7f79] c0 c1 c2 c3 c4 "NEAR" "MID" "FAR" flee speed,
 * with the keys given, from a square open two ahead. */
static bool flat = false; /* outside 3D areas */

static uint16_t encounter(const char *keys, const uint8_t reaction[5], uint8_t flee,
                          uint8_t speed, const char *texts[3])
{
    reset(keys);
    if (flat) {
        game.vm.mem4b00[0xe6] = 0;
        game.vm.mode = 3;
    }
    cok_picture_fill(&game.screen, 1, 21 * 8, 1, 8, 4); /* a mark in the text window */
    member("KAL", 12);
    member("ANN", 6);
    uint8_t map[0x402] = {0};
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    game.view.wrap = true;
    game.vm.map_x = game.vm.map_y = 7;
    game.vm.mem7c00[0x379] = 0x55;
    uint8_t code[128] = {COK_ECL_ENCOUNTER_MENU, 0, 35, 0, 2, 0, 41, 1, 0x79, 0x7f};
    size_t n = 10;
    for (size_t i = 0; i < 5; ++i) {
        code[n++] = 0;
        code[n++] = reaction[i];
    }
    for (size_t t = 0; t < 3; ++t) {
        /* Packed text, six bits a character. */
        size_t length = strlen(texts[t]), bytes = (length * 6 + 7) / 8;
        code[n++] = 0x80;
        code[n++] = (uint8_t)bytes;
        unsigned bits = 0, acc = 0;
        for (size_t i = 0; i < length; ++i) {
            acc = acc << 6 | ((unsigned)texts[t][i] & 0x3f);
            bits += 6;
            while (bits >= 8) {
                code[n++] = (uint8_t)(acc >> (bits - 8));
                bits -= 8;
            }
        }
        if (bits > 0) code[n++] = (uint8_t)(acc << (8 - bits));
    }
    code[n++] = 0;
    code[n++] = flee;
    code[n++] = 0;
    code[n++] = speed;
    CHECK(run(code, n) == COK_ECL_OK);
    CHECK(!game.in_encounter);
    return game.vm.mem7c00[0x379];
}

static void test_encounter_menu(void)
{
    const char *texts[3] = {"NEAR", "MID", "FAR"};
    /* Monsters that attack: Combat, Wait and Advance fight. */
    static const uint8_t attack[5] = {0, 0, 0, 0, 0};
    CHECK(encounter("C", attack, 12, 12, texts) == 1);
    CHECK(strstr(s.log, "monster: sprite 35 at 2;print: FAR;menu: ~COMBAT ~WAIT ~FLEE ~ADVANCE;"
                        "choice: 0;") != NULL);
    CHECK(encounter("W", attack, 12, 12, texts) == 1);
    /* Fleeing needs the slowest member, 6, to reach the threshold. */
    CHECK(encounter("F", attack, 7, 12, texts) == 1);
    CHECK(encounter("F", attack, 6, 12, texts) == 2);
    /* Holding monsters: waiting waits; advancing approaches, and at 0
     * the menu offers Parlay, which talks. */
    static const uint8_t hold[5] = {1, 1, 1, 1, 1};
    CHECK(encounter("WAAP", hold, 12, 12, texts) == 3);
    CHECK(strstr(s.log, "choice: 1;print: Both sides wait.;print: FAR;") != NULL);
    CHECK(strstr(s.log, "monster: sprite 35 at 0;print: NEAR;menu: ~COMBAT ~WAIT ~FLEE ~PARLAY;"
                        "choice: 3;") != NULL);
    /* No close-up inside the menu. */
    CHECK(strstr(s.log, "picture") == NULL);
    /* Timid monsters flee unless speed beats the fastest, 12. */
    static const uint8_t timid[5] = {2, 2, 2, 2, 2};
    CHECK(encounter("C", timid, 12, 12, texts) == 1);
    CHECK(encounter("C", timid, 12, 13, texts) == 0);
    CHECK(strstr(s.log, "print: The monsters flee.;") != NULL);
    CHECK(encounter("W", timid, 12, 12, texts) == 0);
    /* Monsters that talk: waiting approaches, then talks. */
    static const uint8_t talk[5] = {4, 4, 4, 4, 4};
    CHECK(encounter("WWW", talk, 12, 12, texts) == 3);
    /* A code of 5 or more stores nothing. */
    static const uint8_t none[5] = {5, 5, 5, 5, 5};
    CHECK(encounter("C", none, 12, 12, texts) == 0x55);
    /* The text window is cleared first in 3D areas only; outside them the
     * menu offers Parlay, and at distance 2 Parlay approaches. */
    CHECK(game.screen.pixels[21 * 8 * 160 + 4] == 0);
    flat = true;
    CHECK(encounter("PPP", hold, 12, 12, texts) == 3);
    flat = false;
    CHECK(game.screen.pixels[21 * 8 * 160 + 4] == 0x44);
    int parlays = 0;
    for (const char *p = s.log; (p = strstr(p, "~PARLAY;choice: 3;")) != NULL; ++p) ++parlays;
    CHECK(parlays == 3 && strstr(s.log, "ADVANCE") == NULL);
    CHECK(strstr(s.log, "monster: sprite") == NULL);
    /* The description is the first text not empty, from the distance's. */
    const char *some[3] = {"NEAR", "", ""};
    CHECK(encounter("C", attack, 12, 12, some) == 1);
    CHECK(strstr(s.log, "print: NEAR;") != NULL);
}

static void test_parlay(void)
{
    reset("S");
    game.vm.mem7c00[0x379] = 0x55;
    /* PARLAY 0 2 1 1 0 [7f79], Sly. */
    CHECK(RUN(COK_ECL_PARLAY, 0, 0, 0, 2, 0, 1, 0, 1, 0, 0, 1, 0x79, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 2);
    CHECK(strstr(s.log, "menu: ~HAUGHTY ~SLY ~NICE ~MEEK ~ABUSIVE;choice: 1;") != NULL);
}

static void test_check_party(void)
{
    reset("");
    cok_character *a = member("A", 12), *b = member("B", 6);
    a->record[0x1b] = 10;
    b->record[0x1b] = 17;
    a->record[0xdc] = 40;
    b->record[0xdc] = 25;
    for (size_t i = 0; i < 4; ++i) game.vm.mem7c00[0x379 + i] = 0x55;
    /* CHECKPARTY [7c19] 0 [7f79] [7f7a] [7f7b] [7f7c]: charisma. */
    CHECK(RUN(COK_ECL_CHECKPARTY, 1, 0x19, 0x7c, 0, 0, 1, 0x79, 0x7f, 1, 0x7a, 0x7f, 1, 0x7b, 0x7f,
              1, 0x7c, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 10 && game.vm.mem7c00[0x37a] == 17);
    CHECK(game.vm.mem7c00[0x37b] == 13 && game.vm.mem7c00[0x37c] == 0);
    /* The second thief skill, given as a word. */
    CHECK(RUN(COK_ECL_CHECKPARTY, 2, 0xa6, 0x7c, 0, 0, 1, 0x79, 0x7f, 1, 0x7a, 0x7f, 1, 0x7b, 0x7f,
              1, 0x7c, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 25 && game.vm.mem7c00[0x37a] == 40);
    /* Field 0: whether a record has the effect. */
    CHECK(cok_character_add_effect(b, 0x10, 0, 0, false) != NULL);
    CHECK(RUN(COK_ECL_CHECKPARTY, 0, 0, 0, 0x10, 1, 0x79, 0x7f, 1, 0x7a, 0x7f, 1, 0x7b, 0x7f,
              1, 0x7c, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 0 && game.vm.mem7c00[0x37c] == 1);
    /* [7d1b], movement as other code reads it, is not a field it knows:
     * nothing is stored (the minotaurs of block 80). */
    for (size_t i = 0; i < 4; ++i) game.vm.mem7c00[0x379 + i] = 0x55;
    CHECK(RUN(COK_ECL_CHECKPARTY, 1, 0x1b, 0x7d, 0, 0, 1, 0x79, 0x7f, 1, 0x7a, 0x7f, 1, 0x7b, 0x7f,
              1, 0x7c, 0x7f) == COK_ECL_OK);
    for (size_t i = 0; i < 4; ++i) CHECK(game.vm.mem7c00[0x379 + i] == 0x55);
    /* Monsters loaded count: a goblin's movement (0, not yet computed). */
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9) == COK_ECL_OK);
    CHECK(RUN(COK_ECL_CHECKPARTY, 1, 0x9f, 0x7c, 0, 0, 1, 0x79, 0x7f, 1, 0x7a, 0x7f, 1, 0x7b, 0x7f,
              1, 0x7c, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 0 && game.vm.mem7c00[0x37a] == 12);
    CHECK(game.vm.mem7c00[0x37b] == 6);
    /* An empty party divides by zero. */
    reset("");
    CHECK(RUN(COK_ECL_CHECKPARTY, 1, 0x9f, 0x7c, 0, 0, 1, 0x79, 0x7f, 1, 0x7a, 0x7f, 1, 0x7b, 0x7f,
              1, 0x7c, 0x7f) == COK_ECL_DIVIDE_BY_ZERO);
}

static void test_party_strength(void)
{
    reset("");
    cok_character *a = member("A", 12), *b = member("B", 6);
    /* A level 3 mage with 9 hit points, AC -2 and THAC0 19: (24 + 10 + 10
     * + 9) / 10 = 5. A level 2 cleric with 255 hit points: 263 / 10 = 26. */
    a->record[0xfe] = 3;
    a->record[0x197] = 9;
    a->record[0x18d] = 62;
    a->record[0x18c] = 41;
    b->record[0xf9] = 2;
    b->record[0x197] = 255;
    b->record[0x18d] = 60;
    b->record[0x18c] = 39;
    CHECK(RUN(COK_ECL_PARTYSTRENGTH, 1, 0x79, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 31);
    /* PARTY SURPRISE: a ranger (class 4) or cleric/ranger (10). */
    game.vm.mem7c00[0x37a] = 0x55;
    CHECK(RUN(COK_ECL_PARTY_SURPRISE, 1, 0x79, 0x7f, 1, 0x7a, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 0 && game.vm.mem7c00[0x37a] == 0);
    b->record[0x5b] = 10;
    CHECK(RUN(COK_ECL_PARTY_SURPRISE, 1, 0x79, 0x7f, 1, 0x7a, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 1);
}

static void test_surprise(void)
{
    /* Two d6 are rolled and nothing is kept: 0x7ecb is unchanged. */
    reset("");
    game.vm.seed = 1234;
    uint32_t seed = 1234;
    cok_dice(&seed, 2, 6);
    game.vm.mem7c00[0x2cb] = 0x55;
    CHECK(RUN(COK_ECL_SURPRISE, 0, 1, 0, 2, 0, 3, 0, 4) == COK_ECL_OK);
    CHECK(game.vm.seed == seed && game.vm.mem7c00[0x2cb] == 0x55);
}

static void test_call(void)
{
    reset("");
    member("KAL", 12);
    uint8_t map[0x402] = {0};
    map[2 + 3 * 16 + 4] = 0x20; /* a north wall at 4, 3 */
    map[2 + 0x200 + 3 * 16 + 4] = 0x85;
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    game.vm.map_x = 4;
    game.vm.map_y = 3;
    /* CALL [2e10]: the square always; with nothing changed, nothing else. */
    CHECK(RUN(COK_ECL_CALL, 1, 0x10, 0x2e) == COK_ECL_OK);
    CHECK(game.vm.square == 0x85 && game.vm.ahead == 0);
    game.sprite_shown = game.sprite_loaded = true;
    CHECK(RUN(COK_ECL_CALL, 1, 0x10, 0x2e) == COK_ECL_OK);
    CHECK(!game.sprite_shown && !game.sprite_loaded && game.vm.ahead == 2);
    /* Other addresses are not ported. */
    CHECK(RUN(COK_ECL_CALL, 1, 0x03, 0xb2) == COK_ECL_OK);
    CHECK(strcmp(s.log, "[CALL];") == 0);
}

static cok_real real(const char *hex)
{
    cok_real r;
    for (size_t i = 0; i < 6; ++i) {
        unsigned byte;
        CHECK(sscanf(hex + 2 * i, "%2x", &byte) == 1);
        r.b[i] = (uint8_t)byte;
    }
    return r;
}

static bool same(cok_real a, const char *hex)
{
    return memcmp(a.b, real(hex).b, 6) == 0;
}

static void test_real(void)
{
    cok_real r, ninety;
    CHECK(same(cok_real_from_long(100), "870000000048"));
    CHECK(same(cok_real_from_long(-1), "8100000000" "80"));
    CHECK(same(cok_real_from_long(0), "000000000000"));
    CHECK(same(cok_real_from_long(INT32_MIN), "a000000000" "80"));
    /* ROB's factors, (100 - percent) / 100. */
    CHECK(cok_real_divide(cok_real_from_long(90), cok_real_from_long(100), &ninety));
    CHECK(same(ninety, "806666666666"));
    CHECK(cok_real_divide(cok_real_from_long(75), cok_real_from_long(100), &r) &&
          same(r, "800000000040"));
    CHECK(cok_real_divide(cok_real_from_long(-155), cok_real_from_long(100), &r) &&
          same(r, "81666666" "66c6"));
    CHECK(cok_real_divide(cok_real_from_long(0), cok_real_from_long(100), &r) &&
          same(r, "000000000000"));
    /* 530 * 0.9 is 476.99..., where an exact product gives 477: the
     * multiply's short way, for a mantissa whose low 24 bits are 0, drops
     * part of the low product. */
    int32_t v;
    CHECK(cok_real_multiply(cok_real_from_long(530), ninety, &r) && cok_real_trunc(r, &v) &&
          v == 476);
    CHECK(cok_real_multiply(ninety, cok_real_from_long(530), &r) && cok_real_trunc(r, &v) &&
          v == 476);
    /* With neither short, the product is exact to 48 bits: 0.9 * 0.9. */
    CHECK(cok_real_multiply(ninety, ninety, &r) && same(r, "80c2f5285c4f"));
    CHECK(!cok_real_divide(ninety, cok_real_from_long(0), &r)); /* runtime error 200 */
    CHECK(!cok_real_multiply(real("ff0000000000"), real("ff0000000000"), &r)); /* 205 */
    CHECK(cok_real_multiply(real("010000000000"), real("010000000000"), &r) &&
          same(r, "000000000000"));
    /* Trunc goes toward 0 and fails at 2^31 and past (207). */
    CHECK(cok_real_trunc(real("810000000080"), &v) && v == -1);
    CHECK(cok_real_trunc(real("7f0000000000"), &v) && v == 0);
    CHECK(cok_real_trunc(cok_real_from_long(INT32_MAX), &v) && v == INT32_MAX);
    CHECK(!cok_real_trunc(cok_real_from_long(INT32_MIN), &v));
}

static void money(cok_character *c, uint16_t amount)
{
    for (size_t i = 0; i < COK_COINS; ++i) {
        c->record[0xeb + 2 * i] = (uint8_t)amount;
        c->record[0xec + 2 * i] = (uint8_t)(amount >> 8);
    }
}

static void item(cok_character *c, uint16_t weight, bool readied, bool cursed)
{
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[0x2e] = 1;
    it[0x37] = (uint8_t)weight;
    it[0x38] = (uint8_t)(weight >> 8);
    it[0x34] = readied;
    it[0x36] = cursed;
    CHECK(cok_character_insert_item(c, c->item_count, it));
}

static void test_rob(void)
{
    reset("");
    cok_character *a = member("A", 12), *b = member("B", 12);
    money(a, 530);
    money(b, 1000);
    game.vm.character = a->record;
    /* ROB 1 10 0: every record keeps 90% of its money, Trunc(530 * 0.9)
     * 476, and the d100 is rolled for each item even at a chance of 0. */
    item(a, 10, false, false);
    item(b, 10, false, false);
    game.vm.seed = 99;
    uint32_t seed = 99;
    cok_dice(&seed, 2, 100);
    CHECK(RUN(COK_ECL_ROB, 0, 1, 0, 10, 0, 0) == COK_ECL_OK);
    CHECK(a->record[0xeb] == (476 & 0xff) && a->record[0xec] == 476 >> 8);
    CHECK(a->record[0xf7] == (476 & 0xff) && b->record[0xeb] == (900 & 0xff));
    CHECK(game.vm.seed == seed && a->item_count == 1 && b->item_count == 1);
    /* A percent past 100 leaves a negative factor: 1 becomes 65535. */
    money(b, 1);
    game.vm.character = b->record;
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 255, 0, 0) == COK_ECL_OK);
    CHECK(b->record[0xeb] == 0xff && b->record[0xec] == 0xff);
    /* A heavy item lowers the chance for itself and those after it: 100
     * less 50 for 25 or more, so the second, light, goes on a d100 up to
     * 50. A cursed readied item says so and is taken all the same. */
    reset("");
    cok_character *c = member("C", 12);
    game.vm.character = c->record;
    item(c, 25, false, false);
    item(c, 1, false, false);
    for (uint32_t start = 1; start < 40; ++start) {
        seed = start;
        uint8_t first = cok_dice(&seed, 1, 100), second = cok_dice(&seed, 1, 100);
        if (first <= 50 && second > 50 && second <= 100) {
            game.vm.seed = start;
            break;
        }
    }
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 0, 0, 100) == COK_ECL_OK);
    CHECK(c->item_count == 1 && c->items[0][0x37] == 1);
    /* An item of 255 lowers it by 50, one of 256 by 90: with 95, a roll
     * from 6 to 45 takes the first and not the second. */
    reset("");
    c = member("C", 12);
    game.vm.character = c->record;
    for (uint32_t start = 1;; ++start) {
        seed = start;
        uint8_t roll = cok_dice(&seed, 1, 100);
        if (roll > 5 && roll <= 45) {
            game.vm.seed = start;
            break;
        }
    }
    item(c, 255, false, false);
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 0, 0, 95) == COK_ECL_OK && c->item_count == 0);
    item(c, 256, false, false);
    for (uint32_t start = 1;; ++start) {
        seed = start;
        uint8_t roll = cok_dice(&seed, 1, 100);
        if (roll > 5 && roll <= 45) {
            game.vm.seed = start;
            break;
        }
    }
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 0, 0, 95) == COK_ECL_OK && c->item_count == 1);
    cok_character_remove_item(c, 0);
    /* 255 takes these, 50 and 90 off included; a cursed readied
     * item says so and goes all the same. */
    item(c, 25, true, true);
    item(c, 300, true, false);
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 0, 0, 255) == COK_ECL_OK && c->item_count == 0);
    CHECK(strstr(s.log, "print: It's Cursed;") != NULL);
    /* With none selected the original reads through NULL. */
    game.vm.character = NULL;
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 0, 0, 255) == COK_ECL_UNDEFINED);
}

/* A party of two, A unable to act, with goblins and an ally loaded, then
 * COMBAT resolved by stub, with keys. */
static cok_ecl_status fight(cok_combat_stub stub, const char *keys)
{
    reset(keys);
    cok_character *a = member("A", 12);
    member("B", 12);
    a->record[0x189] = 0;
    a->record[0x188] = 5;
    /* A wall a square ahead: the monsters stand one square off. */
    uint8_t map[0x402] = {0};
    map[2 + 6 * 16 + 7] = 0x10;
    CHECK(cok_view_set_map(&game.view, map, sizeof map));
    game.vm.map_x = game.vm.map_y = 7;
    game.combat_stub = stub;
    game.vm.mem7c00[0x2c1] = 2;
    game.vm.mem7c00[0x2ca] = 3;
    game.vm.mem7c00[0x370] = 4;
    game.vm.mem7c00[0x2e3] = 1;
    game.vm.mem4b00[0x1f5] = 1;
    game.vm.mem4b00[0x1f8] = 0;
    /* CLEARMONSTERS, LOAD MONSTER 9 3 9 (goblins), 15 1 15 (KILDIRF, on
     * the party's side), COMBAT. */
    cok_ecl_status status = RUN(COK_ECL_CLEARMONSTERS, COK_ECL_LOAD_MONSTER, 0, 9, 0, 3, 0, 9,
                                COK_ECL_LOAD_MONSTER, 0, 15, 0, 1, 0, 15, COK_ECL_COMBAT);
    game.combat_stub = COK_COMBAT_UNPORTED;
    return status;
}

static void test_combat(void)
{
    /* Unresolved, the battle runs its rounds, every turn passing, until
     * the limit, and the monsters are removed as the end of combat removes
     * them: the goblins (against the party) and KILDIRF (past the party's
     * size). */
    CHECK(fight(COK_COMBAT_UNPORTED, "\rE") == COK_ECL_OK);
    CHECK(strstr(s.log, "[COMBAT]") == NULL && strstr(s.log, "round: 15:") != NULL &&
          strstr(s.log, "round: 16:") == NULL);
    CHECK(strstr(s.log, "combat: removed 3 GOBLIN, 1 KILDIRF; 0 dropped;") != NULL);
    CHECK(game.party.count == 2 && game.vm.mem7c00[0x33e] == 2);
    CHECK(game.party.members[0]->combat == NULL && game.party.members[1]->combat == NULL);
    CHECK(game.vm.mem7c00[0x2c7] == 0 && game.vm.mem7c00[0x2c8] == 0);
    CHECK(game.vm.mode == 4 && game.vm.mem7c00[0x2ca] == 1 && game.vm.mem7c00[0x370] == 0);
    CHECK(game.vm.mem7c00[0x2e3] == 0);
    CHECK(game.vm.mem4b00[0x1f5] == 0 && game.vm.mem7c00[0x2c1] == 1);
    CHECK(game.icons[8][0].pixels == NULL && game.vm.character == game.party.members[0]->record);
    /* Won: the goblins drop, the first enemy among them (0x4cf8); KILDIRF,
     * on the party's side, does not, though it goes. */
    CHECK(fight(COK_COMBAT_WON, "\rE") == COK_ECL_OK);
    CHECK(strstr(s.log, "combat: won;") != NULL && game.party.count == 2);
    CHECK(game.vm.mem7c00[0x2c7] == 0 && game.vm.mem7c00[0x2c8] == 3 && game.vm.mem4b00[0x1f8] == 1);
    /* So do the Gods, at B's first turn; then the battle may go on, but
     * does not. */
    game.helm = true;
    CHECK(fight(COK_COMBAT_GODS, "\r\rE") == COK_ECL_OK);
    game.helm = false;
    CHECK(strstr(s.log, "turn: B (initiative ") != NULL);
    CHECK(strstr(s.log, "print: The Gods intervene!;") != NULL);
    CHECK(strstr(s.log, "menu: Continue Battle:;choice: N;") != NULL);
    CHECK(game.vm.mem7c00[0x2c8] == 3);
    /* Fled: B comes back; A, who could not run, is left behind. */
    CHECK(fight(COK_COMBAT_FLED, "\rE") == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x2c7] == 0x81 && game.party.count == 1);
    CHECK(game.vm.mem7c00[0x33e] == 1 && game.party.members[0]->record[0x188] == 0 &&
          game.party.members[0]->record[0x189] == 1);
    /* Lost: the party is removed and the run ends. */
    CHECK(fight(COK_COMBAT_LOST, "\rE") == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x2c7] == 0x80 && game.party.count == 0 && game.vm.mem7c00[0x33e] == 0);
    CHECK(game.party_killed && game.vm.abort);
    CHECK(strstr(s.log, "print: The monsters rejoice for the party has been destroyed;") != NULL);
    /* In cells 2-37 from row 5, cleared first (351b:1aab), as the key is
     * awaited. */
    bool ink = false, above = false;
    for (size_t row = 0; row < 8; ++row)
        for (size_t x = 0; x < 4; ++x) {
            if (shown[(5 * 8 + row) * 160 + 2 * 4 + x] != 0) ink = true;
            if (shown[(4 * 8 + row) * 160 + 2 * 4 + x] != 0) above = true;
        }
    CHECK(ink && !above);
    game.party_killed = false;
    /* The run ends before the selection kept in DS:43bf is restored, so a
     * party member selected there may go. */
    reset("\r\r");
    game.vm.character = game.vm.saved_character = member("A", 12)->record;
    game.combat_stub = COK_COMBAT_LOST;
    CHECK(RUN(COK_ECL_LOAD_CHARACTER, 0, 0, COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9,
              COK_ECL_COMBAT) == COK_ECL_OK && game.party_killed);
    game.party_killed = false;
    /* Fled, with the selection that EXIT restores after LOAD CHARACTER
     * left behind, the original would select a record it freed. */
    reset("\r\r");
    cok_character *left = member("A", 12);
    left->record[0x189] = 0;
    left->record[0x188] = 5;
    member("B", 12);
    game.vm.character = game.vm.saved_character = left->record;
    game.combat_stub = COK_COMBAT_FLED;
    CHECK(RUN(COK_ECL_LOAD_CHARACTER, 0, 0, COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9,
              COK_ECL_COMBAT) == COK_ECL_UNDEFINED);
    /* A party member against the party (charmed), here dead, does not stop
     * the walk of those who fled, which ends at the first record past the
     * party's size; it goes with the enemies, counted out of 0x7f3e as A,
     * left behind, is. */
    reset("\rE");
    cok_character *dying = member("A", 12);
    dying->record[0x189] = 0;
    dying->record[0x188] = 5;
    cok_character *charmed = member("B", 12);
    charmed->record[0x18a] = 1;
    charmed->record[0x188] = 6;
    cok_character *c = member("C", 12);
    game.combat_stub = COK_COMBAT_FLED;
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9, COK_ECL_COMBAT) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x2c7] == 0x81 && game.party.count == 1);
    CHECK(game.party.members[0] == c && c->record[0x188] == 0 && c->record[0x189] == 1);
    CHECK(game.vm.mem7c00[0x33e] == 1);
    /* An enemy that fled (status 3) sets 0x7ec7 to 1 when it is 0
     * (351b:1493). */
    reset("\rE");
    member("A", 12);
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 2, 0, 9) == COK_ECL_OK);
    game.party.members[2]->record[0x188] = 3;
    CHECK(RUN(COK_ECL_COMBAT) == COK_ECL_OK && game.vm.mem7c00[0x2c7] == 1);
    /* The second module has no overland map: nothing is shown, and
     * nothing is said. */
    reset("\rE");
    member("A", 12);
    game.vm.file = 2;
    game.vm.mem4b00[0xe6] = 0;
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 8, 0, 1, 0, 8, COK_ECL_COMBAT) == COK_ECL_OK);
    CHECK(game.big_id == 0x79 && game.big.pixels == NULL && strstr(s.log, "error") == NULL);
    game.vm.file = 1;
    /* Outside 3D areas the monsters stand two squares off, and the
     * overland map is shown afterwards, the party marked on it at 0x4bc3
     * + 1, 0x4bc4 + 1 (4877:0005): the cursor's yellow in its middle. */
    reset("\rE");
    member("A", 12);
    game.vm.mem4b00[0xe6] = 0;
    game.vm.mem7c00[0x2c1] = 0;
    game.vm.mem4b00[0xc3] = game.vm.mem4b00[0xc4] = 1;
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9, COK_ECL_COMBAT) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x2c1] == 2 && game.vm.mode == 3 && game.big_id == 0x79);
    CHECK(strstr(s.log, "4877") == NULL);
    CHECK(game.screen.pixels[(2 * 8 + 4) * 160 + 2 * 4 + 1] == 0xee);
    CHECK(game.screen.pixels[(1 * 8 + 4) * 160 + 1 * 4 + 1] != 0xee);
    /* With no monsters loaded: treasure, whose items go; a shop; the
     * temple. */
    reset("\rEN");
    member("A", 12);
    game.pool.coins[0] = 50;
    game.pool.items = calloc(1, COK_ITEM_SIZE);
    game.pool.item_count = 1;
    CHECK(RUN(COK_ECL_COMBAT) == COK_ECL_OK);
    CHECK(game.pool.items == NULL && game.pool.coins[0] == 50);
    CHECK(strstr(s.log, "[COMBAT]") == NULL &&
          strstr(s.log, "print: The party has found Treasure!;") != NULL);
    reset("EE");
    member("A", 12);
    game.vm.mem7c00[0x36c] = 1;
    CHECK(RUN(COK_ECL_COMBAT) == COK_ECL_OK && game.vm.mem7c00[0x36c] == 0);
    CHECK(strstr(s.log, "shop: prices at 0;menu: Buy View Pool Appraise Exit;") != NULL);
    CHECK(game.vm.mode == 4);
    game.vm.mem7c00[0x2e2] = 1;
    CHECK(RUN(COK_ECL_COMBAT) == COK_ECL_OK && game.vm.mem7c00[0x2e2] == 0);
    CHECK(strstr(s.log, "menu: Heal View Pool Appraise Exit;") != NULL && s.at == s.length);
}

/* Walks of the party list that hold eight entries stop where nine records
 * would overrun them. */
static void test_full_list(void)
{
    reset("");
    for (int i = 0; i < 9; ++i) member("A", 12);
    snprintf(game.save_dir, sizeof game.save_dir, "/nonexistent");
    CHECK(!cok_camp_save_game(&game, 'A') && game.vm.status == COK_ECL_UNDEFINED);
    game.save_dir[0] = '\0';
    /* The party list draws a row for every record, from row 4. */
    game.vm.character = NULL;
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    cok_adventure_party(&game);
    const cok_picture *p = &game.screen;
    bool drawn = false;
    for (size_t row = 12 * 8; row < 13 * 8; ++row)
        for (size_t x = 0x11 * 4; x < 0x13 * 4; ++x)
            if (p->pixels[row * p->units * 4 + x] != 0) drawn = true;
    CHECK(drawn);
}

int main(void)
{
    open_game();
    test_load_monster();
    test_difficulty();
    test_missing_monster();
    test_open_squares();
    test_movement();
    test_sprite();
    test_encounter_menu();
    test_parlay();
    test_check_party();
    test_party_strength();
    test_surprise();
    test_call();
    test_real();
    test_rob();
    test_combat();
    test_full_list();
    cok_adventure_close(&game);
    puts("monster tests passed");
    return 0;
}
