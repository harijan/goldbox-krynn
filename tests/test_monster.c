#define _POSIX_C_SOURCE 200809L /* mkdtemp, symlink */
#include "dark_queen.h"
#include "hlib.h"
#include "monster.h"
#include "queen.h"

#include "camp.h"
#include "shop.h"
#include "treasure.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* Whether the menu reading the last key animates the small picture. */
static bool read_animating;
static const cok_adventure *reading;

static int scripted(void *context)
{
    script *s = context;
    if (watched != NULL) memcpy(shown, watched->pixels, sizeof shown);
    if (reading != NULL) read_animating = reading->menu_animates;
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
    snprintf(s->log + used, sizeof s->log - used, "[%s];", cok_ecl_opcode_name(game->vm.rules, game->vm.opcode));
}

static script s;
static cok_adventure game;

/* The delays of the speed times 3 ms, a typed character's (1521:042e). */
static unsigned typed;

static void delayed(cok_adventure *g, unsigned ms, void *context)
{
    (void)context;
    if (ms == g->speed * 3u) ++typed;
}

static void open_game(void)
{
    cok_keyboard keys = {scripted, &s, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .unported = unported, .delay = delayed,
                                 .context = &s};
    CHECK(cok_adventure_open(&game, &cok_rules_champions, "COKAssets", &keys, &hooks));
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
    /* Records 0 held with no pictures: the 3D shops here, in ECL1, which
     * has no HEAD1.DAX, show the portrait last shown, 0 and 0, as
     * nothing. */
    cok_adventure_forget_portrait(&game);
    game.head_id = game.body_id = game.portrait_head = game.portrait_body = 0;
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
    CHECK(cok_party_add(&game.party, &cok_rules_champions, c));
    ++game.vm.mem7c00[0x33e];
    return c;
}

/* Monster id of MON1CHA as cok_monster_read reads it: the record's first
 * 409 bytes, but for the far pointers to the effects, the items, the next
 * record and the combat record, which are cleared; the readied items'
 * slots (+0x147) are kept. Returns the record's bytes (free them). */
static uint8_t *check_read(uint8_t id)
{
    size_t size = 0;
    bool no_file;
    uint8_t *data = cok_adventure_find_record(&game, "MON1CHA", id, &size, &no_file);
    CHECK(data != NULL && size >= 409);
    cok_character read;
    CHECK(cok_monster_read(&game, id, &read));
    for (size_t i = 0; i < 409; ++i) {
        bool pointer = (i >= 0xe3 && i < 0xe7) || (i >= 0x143 && i < 0x147) ||
                       (i >= 0x17f && i < 0x187);
        CHECK(read.record[i] == (pointer ? 0 : data[i]));
    }
    cok_character_free(&read);
    return data;
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
    /* The record read (check_read); the skeleton's has an effects pointer. */
    uint8_t *data = check_read(26);
    CHECK(data[0xe3] != 0);
    free(data);
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
    game.icon_slot = (uint8_t)cok_icon_slots(&game);
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 8, 0, 1, 0, 8) == COK_ECL_UNDEFINED);
}

/* A copy of COKAssets in a temporary directory, its files linked, but
 * file, which links to another of them, from: an archive whose records
 * are not what the game reads there. game.assets points at it until
 * undamaged. */
static char damaged_dir[256];

static void damage(const char *file, const char *from)
{
    char cwd[1024];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    const char *temp = getenv("TMPDIR");
    snprintf(damaged_dir, sizeof damaged_dir, "%s/test_monster-XXXXXX",
             temp != NULL && temp[0] != '\0' ? temp : "/tmp");
    CHECK(mkdtemp(damaged_dir) != NULL);
    DIR *dir = opendir("COKAssets");
    CHECK(dir != NULL);
    for (struct dirent *e; (e = readdir(dir)) != NULL;) {
        if (e->d_name[0] == '.') continue;
        char target[2048], link[512];
        bool replaced = strcmp(e->d_name, file) == 0;
        snprintf(target, sizeof target, "%s/COKAssets/%s", cwd, replaced ? from : e->d_name);
        snprintf(link, sizeof link, "%s/%s", damaged_dir, e->d_name);
        CHECK(symlink(target, link) == 0);
    }
    closedir(dir);
    snprintf(game.assets, sizeof game.assets, "%s", damaged_dir);
}

static void undamage(void)
{
    DIR *dir = opendir(damaged_dir);
    CHECK(dir != NULL);
    for (struct dirent *e; (e = readdir(dir)) != NULL;) {
        if (e->d_name[0] == '.') continue;
        char link[512];
        snprintf(link, sizeof link, "%s/%s", damaged_dir, e->d_name);
        CHECK(unlink(link) == 0);
    }
    closedir(dir);
    CHECK(rmdir(damaged_dir) == 0);
    snprintf(game.assets, sizeof game.assets, "COKAssets");
}

/* The names in the messages of files whose records are not what the game
 * reads there: the archive with another's records, MON1CHA.DAX's of 409
 * bytes, or MON1SPC.DAX's of 9. */
static void test_damaged_files(void)
{
    uint8_t *data;
    /* MON1SPC with whole records: an effect is 9 bytes. */
    reset("");
    member("KAL", 12);
    damage("MON1SPC.DAX", "MON1CHA.DAX");
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9) == COK_ECL_UNDEFINED);
    CHECK(strstr(game.error, "MON1SPC record 9 is not whole effects; 4b6d:161b reads past it") !=
          NULL);
    undamage();
    reset("");
    member("KAL", 12);
    damage("MON1ITM.DAX", "MON1CHA.DAX");
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9) == COK_ECL_UNDEFINED);
    CHECK(strstr(game.error, "MON1ITM record 9 is not whole items; 4b6d:161b reads past it") !=
          NULL);
    undamage();
    reset("");
    member("KAL", 12);
    damage("MON1CHA.DAX", "MON1SPC.DAX");
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 1, 0, 1, 0, 1) == COK_ECL_UNDEFINED);
    CHECK(strstr(game.error, "MON1CHA record 1 has 9 bytes; 4b6d:161b copies 409, past it") !=
          NULL);
    undamage();
    /* A longer record (a map of GEO1 as MON1CHA): its first 409 bytes, the
     * combat record's pointer and the movement (+0x198) included, which no
     * monster of the game's sets. */
    reset("");
    damage("MON1CHA.DAX", "GEO1.DAX");
    data = check_read(32);
    CHECK((data[0x183] | data[0x184] | data[0x185] | data[0x186]) != 0 && data[0x198] != 0 &&
          (data[0x17f] | data[0x180] | data[0x181] | data[0x182]) != 0);
    free(data);
    undamage();
    /* LOAD FILES and LOAD PIECES: a map is 1026 bytes; a wall set's
     * record holds whole sets. */
    reset("");
    damage("GEO1.DAX", "MON1CHA.DAX");
    CHECK(RUN(COK_ECL_LOAD_FILES, 0, 9, 0, 0xff, 0, 0xff) == COK_ECL_LOAD_FAILED);
    CHECK(strstr(s.log, "error: GEO1.DAX record 9 is not a map;") != NULL);
    undamage();
    reset("");
    damage("WALLDEF1.DAX", "MON1SPC.DAX");
    CHECK(RUN(COK_ECL_LOAD_PIECES, 0, 1, 0, 0xff, 0, 0xff) == COK_ECL_LOAD_FAILED);
    CHECK(strstr(s.log, "error: WALLDEF1.DAX record 1 does not fit from wall set 1;") != NULL);
    undamage();
    /* A sprite's record that is not one. */
    reset("");
    damage("SPRIT1.DAX", "MON1SPC.DAX");
    cok_adventure_load_sprite(&game, 1);
    CHECK(strstr(s.log, "error: SPRIT1.DAX record 1: ") != NULL);
    undamage();
    reset("");
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
    CHECK(!cok_monster_movement(&game.party, &cok_rules_champions, &fast, &slow));
    cok_character *a = member("A", 6), *b = member("B", 12), *c = member("C", 9);
    (void)b;
    CHECK(cok_monster_movement(&game.party, &cok_rules_champions, &fast, &slow) && fast == 12 && slow == 6);
    /* Haste doubles, slow halves, from the first's own movement. */
    CHECK(cok_character_add_effect(a, 0x27, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(c, 0x2a, 0, 0, false) != NULL);
    CHECK(cok_monster_movement(&game.party, &cok_rules_champions, &fast, &slow) && fast == 12 && slow == 4);
    a->record[0x198] = 200;
    CHECK(cok_monster_movement(&game.party, &cok_rules_champions, &fast, &slow) && fast == 200 && slow == 4);
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
    /* With 0x7ee1 set, the close-up is a portrait (3775:0538): that head
     * over the close-up's body, HEAD2 record 69 and BODY2 record 68 at
     * cells 3, 3 and 3, 8, and the view is not replaced. */
    reset("");
    game.vm.file = 2;
    game.vm.mem7c00[0x2e1] = 69;
    game.view_replaced = true;
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 68) == COK_ECL_OK);
    CHECK(strstr(s.log, "monster: portrait 69 and 68;") != NULL && !game.view_replaced);
    CHECK(game.head_id == 69 && game.body_id == 68 && game.portrait_body == 68);
    /* A close-up shown stays until 0x7ee1 changes (DS:8854): here to a
     * head HEAD1.DAX, which ECL1 lacks, would have, where the original
     * asks for its disk. */
    reset("");
    cok_adventure_forget_portrait(&game);
    CHECK(RUN(COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 41, COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0, 41,
              COK_ECL_SAVE, 0, 3, 1, 0xe1, 0x7e, COK_ECL_SETUP_MONSTER, 0, 35, 0, 0, 0,
              41) == COK_ECL_LOAD_FAILED);
    CHECK(strncmp(s.log, "monster: sprite 35 at 0;monster: picture 41;error: ", 51) == 0);
    CHECK(strstr(game.error, "HEAD1.DAX") != NULL);
}

/* ENCOUNTER MENU 35 2 41 [7f79] c0 c1 c2 c3 c4 "NEAR" "MID" "FAR" flee speed,
 * with the keys given, from a square open two ahead. */
static bool flat = false; /* outside 3D areas */
static bool replaced = false; /* a picture replaced the view (DS:884a, 884b) */

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
    game.picture_shown = game.view_replaced = replaced;
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
    /* 2fd3:23e5 animates the small picture while it waits over a picture
     * that replaced the view, in a 3D area, unless it is PIC 9. */
    {
        static const uint8_t hold[5] = {1, 1, 1, 1, 1};
        reading = &game;
        for (int r = 0; r < 2; ++r) {
            for (int f = 0; f < 2; ++f) {
                replaced = r;
                flat = f;
                read_animating = !replaced;
                encounter("\x1b", hold, 0, 0, texts);
                CHECK(read_animating == (replaced && !flat) && !game.menu_animates);
            }
        }
        replaced = flat = false;
        reading = NULL;
    }
    /* Monsters that attack: Combat, Wait and Advance fight. */
    static const uint8_t attack[5] = {0, 0, 0, 0, 0};
    static const uint8_t hold[5] = {1, 1, 1, 1, 1};
    CHECK(encounter("C", attack, 12, 12, texts) == 1);
    CHECK(strstr(s.log, "monster: sprite 35 at 2;print: FAR;menu: ~COMBAT ~WAIT ~FLEE ~ADVANCE;"
                        "choice: 0;") != NULL);
    CHECK(encounter("W", attack, 12, 12, texts) == 1);
    /* Its texts are typed, the text delay on from its start (2fd3:23f6)
     * whatever it was, and off once it ends (2aae). */
    for (int on = 0; on < 2; ++on) {
        game.text_delay = on;
        typed = 0;
        CHECK(encounter("WC", hold, 12, 12, texts) == 1);
        CHECK(typed == strlen("FAR") + strlen("Both sides wait.") + strlen("FAR"));
        CHECK(!game.text_delay);
    }
    /* Fleeing needs the slowest member, 6, to reach the threshold. */
    CHECK(encounter("F", attack, 7, 12, texts) == 1);
    CHECK(encounter("F", attack, 6, 12, texts) == 2);
    /* Holding monsters: waiting waits; advancing approaches, and at 0
     * the menu offers Parlay, which talks. */
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
    /* A human fighter of 5 who was a cleric of 4 and a mage of 7 before it
     * changed class at 3 may use them: (4 * 4 + 7 * 8) / 10 = 7 more. */
    cok_character *c = member("C", 6);
    c->record[0x5a] = 6;
    c->record[0xfb] = 5;
    c->record[0xd7] = 3;
    c->record[0x101] = 4;
    c->record[0x106] = 7;
    CHECK(RUN(COK_ECL_PARTYSTRENGTH, 1, 0x79, 0x7f) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x379] == 38);
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
    /* The address is the operand's, whatever its type: 0xb203 a sound. */
    CHECK(RUN(COK_ECL_CALL, 1, 0x03, 0xb2) == COK_ECL_OK);
    CHECK(strcmp(s.log, "sound: 10;") == 0);
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
    /* Each kind of coin keeps its own share: 100, 200, ... 700 halve. */
    for (unsigned k = 0; k < 7; ++k) {
        a->record[0xeb + 2 * k] = (uint8_t)(100 * (k + 1));
        a->record[0xec + 2 * k] = (uint8_t)(100 * (k + 1) >> 8);
    }
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 50, 0, 0) == COK_ECL_OK);
    for (unsigned k = 0; k < 7; ++k)
        CHECK((a->record[0xeb + 2 * k] | a->record[0xec + 2 * k] << 8) == (int)(50 * (k + 1)));
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
    /* Molly's readied Hoopak (power 0, effect 0x07) robbed outside combat:
     * its handler sets the THAC0 by a distance read through no combat
     * record, and no recompute (6346:0d20) follows: the run stops. */
    item(c, 25, true, false);
    c->items[0][0x3d] = 0x07;
    c->items[0][0x3e] = 0x80;
    c->slots[0] = 1;
    CHECK(cok_character_add_effect(c, 0x07, 0, 0xff, true) != NULL);
    CHECK(RUN(COK_ECL_ROB, 0, 0, 0, 0, 0, 255) != COK_ECL_OK);
    CHECK(strstr(game.effects.error, "3775:1da9") != NULL);
    game.effects.failed = false;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* With none selected the original reads through NULL. */
    reset("");
    c = member("C", 12);
    game.vm.character = NULL;
    item(c, 25, false, false);
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
    /* Unresolved, the battle runs its rounds, the player's turns passing:
     * with the party off the map the goblins find no one and guard, and
     * KILDIRF, an ally of no morale, flees in panic and gets away, which
     * counts as one dropped (351b:1493). */
    CHECK(fight(COK_COMBAT_UNPORTED, "\rE") == COK_ECL_OK);
    CHECK(strstr(s.log, "round: 15:") != NULL && strstr(s.log, "round: 16:") == NULL);
    CHECK(strstr(s.log, "turn: GOBLIN (initiative 5);print: Guarding;") != NULL);
    CHECK(strstr(s.log, "print: KILDIRF;print: flees in panic;") != NULL);
    CHECK(strstr(s.log, "print: KILDIRF;print: Got Away;") != NULL);
    CHECK(strstr(s.log, "combat: removed 3 GOBLIN, 1 KILDIRF; 1 dropped;") != NULL);
    CHECK(game.vm.mem7c00[0x2c7] == 1 && game.vm.mem7c00[0x2c8] == 1);
    /* With pass every turn passes until the limit, and the monsters are
     * removed as the end of combat removes them: the goblins (against the
     * party) and KILDIRF (past the party's size). */
    CHECK(fight(COK_COMBAT_PASS, "\rE") == COK_ECL_OK);
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
     * left behind, the original selects a record it freed: nobody is then
     * selected for the party list and the pick keys, and what reads its
     * bytes stops the run. */
    reset("\rE");
    cok_character *left = member("A", 12);
    left->record[0x189] = 0;
    left->record[0x188] = 5;
    member("B", 12);
    game.vm.character = game.vm.saved_character = left->record;
    game.combat_stub = COK_COMBAT_FLED;
    CHECK(RUN(COK_ECL_LOAD_CHARACTER, 0, 0, COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9,
              COK_ECL_COMBAT) == COK_ECL_OK);
    CHECK(s.at == s.length && cok_ecl_selection_freed(&game.vm));
    /* Kept again, it stays the freed record's. */
    cok_ecl_keep_selection(&game.vm);
    CHECK(game.vm.saved_freed && game.vm.saved_character == NULL);
    /* A script's read of its fields stops. */
    (void)cok_ecl_load_var(&game.vm, 0x7c72); /* the race */
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.vm.why, "4def:3b0a") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.vm.why = NULL;
    /* Down reads its next field: stops; Up finds none before it: nil. */
    CHECK(cok_adventure_special(&game, 0x50) == NULL && game.vm.status == COK_ECL_UNDEFINED);
    CHECK(strstr(game.error, "546c:3334") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.error[0] = '\0';
    CHECK(cok_adventure_special(&game, 0x48) == NULL && !game.vm.selection_freed);
    CHECK(game.vm.status == COK_ECL_OK);
    game.vm.saved_freed = false;
    /* Kept again before an EXIT, by a command or a block's entry, it is
     * never restored: the run goes on. */
    reset("\rE");
    left = member("A", 12);
    left->record[0x189] = 0;
    left->record[0x188] = 5;
    member("B", 12);
    game.vm.character = game.vm.saved_character = left->record;
    game.combat_stub = COK_COMBAT_FLED;
    CHECK(RUN(COK_ECL_LOAD_MONSTER, 0, 9, 0, 1, 0, 9, COK_ECL_COMBAT) == COK_ECL_OK);
    CHECK(game.vm.saved_freed && game.vm.saved_character == NULL);
    cok_ecl_keep_selection(&game.vm);
    CHECK(!game.vm.saved_freed && game.vm.saved_character == game.vm.character);
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

/* Death Knights' monsters (dkk 46b2:19a2): every record of MON1CHA, its
 * 216 bytes but the far pointers; the spells' block from MON1WIZ's record
 * of its id where there is one (none for the others); its effects (SPC)
 * and items (ITM). Skipped without DKKAssets. */
static void test_death_knights_monsters(void)
{
    static cok_adventure dkk;
    script quiet = {"", 0, 0, ""};
    cok_keyboard keys = {scripted, &quiet, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .context = &quiet};
    FILE *f = fopen("DKKAssets/MON1WIZ.DAX", "rb");
    if (f == NULL) {
        puts("monster: DKKAssets not found; Death Knights' monsters not read");
        return;
    }
    fclose(f);
    CHECK(cok_adventure_open(&dkk, &cok_rules_death_knights, "DKKAssets", &keys, &hooks));
    const cok_character_layout *L = cok_rules_death_knights.character;
    size_t read_count = 0, blocks = 0, items = 0, effects = 0;
    for (unsigned id = 0; id < 256; ++id) {
        size_t size = 0, wiz_size = 0, spc = 0, itm = 0;
        bool no_file;
        uint8_t *data = cok_adventure_find_record(&dkk, "MON1CHA", (uint8_t)id, &size, &no_file);
        if (data == NULL) continue;
        CHECK(size == 0xd8);
        uint8_t *wiz = cok_adventure_find_record(&dkk, "MON1WIZ", (uint8_t)id, &wiz_size, &no_file);
        free(cok_adventure_find_record(&dkk, "MON1SPC", (uint8_t)id, &spc, &no_file));
        free(cok_adventure_find_record(&dkk, "MON1ITM", (uint8_t)id, &itm, &no_file));
        cok_character m;
        CHECK(cok_monster_read(&dkk, (uint8_t)id, &m));
        for (size_t i = 0; i < 0xd8; ++i)
            CHECK(m.record[i] == (cok_field_pointer_at(L, (long)i) ? (i == 0xc2 && wiz != NULL)
                                                                   : data[i]));
        CHECK(cok_field_has_spells(L, m.record) == (wiz != NULL));
        for (size_t i = 0; i < 0xdc; ++i) CHECK(m.record[0xd8 + i] == (wiz != NULL ? wiz[i] : 0));
        size_t n = 0;
        for (const cok_effect *e = m.effects; e != NULL; e = e->next) ++n;
        CHECK(n == spc / COK_EFFECT_SIZE && m.item_count == itm / COK_ITEM_SIZE);
        ++read_count;
        blocks += wiz != NULL;
        items += m.item_count;
        effects += n;
        cok_character_free(&m);
        free(data);
        free(wiz);
    }
    /* MON1CHA's 64 records, 14 with a block of MON1WIZ's, 56 items and
     * 198 effects. */
    CHECK(read_count == 64 && blocks == 14 && items == 56 && effects == 198);
    cok_adventure_close(&dkk);
}

/* Dark Queen's monsters (dqk 604b:257c): every record of MONCHA.GLB, a
 * big-endian size and PackBits of the record (0x19e bytes, its pointers
 * cleared, its usable classes from its levels), its effects of 5 bytes and
 * its items of 17. Skipped without DQKAssets. */
static void test_dark_queen_monsters(void)
{
    static cok_adventure dqk;
    script quiet = {"", 0, 0, ""};
    cok_keyboard keys = {scripted, &quiet, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .context = &quiet};
    FILE *f = fopen("DQKAssets/DISK2/MONCHA.GLB", "rb");
    if (f == NULL) {
        puts("monster: DQKAssets not found; Dark Queen's monsters not read");
        return;
    }
    fclose(f);
    CHECK(cok_adventure_open(&dqk, &cok_rules_dark_queen, "DQKAssets", &keys, &hooks));
    /* Its item types, ITEMS.DAT from its first byte (dqk 64b2:0006), read
     * at startup as the other games' are. */
    size_t set = 0;
    for (size_t t = 0; t < 128; ++t)
        for (size_t b = 0; b < 16; ++b) set += dqk.item_types.type[t][b] != 0;
    CHECK(set > 0);
    {
        uint8_t first[16];
        FILE *items = fopen("DQKAssets/DISK1/ITEMS.DAT", "rb");
        CHECK(items != NULL && fread(first, 1, 16, items) == 16);
        fclose(items);
        CHECK(memcmp(dqk.item_types.type[0], first, 16) == 0);
    }
    const cok_character_layout *L = cok_rules_dark_queen.character;
    size_t read_count = 0, items = 0, effects = 0;
    for (unsigned id = 0; id < 256; ++id) {
        size_t size = 0;
        bool no_file;
        uint8_t *data = cok_adventure_find_record(&dqk, "MONCHA.GLB", (uint8_t)id, &size, &no_file);
        if (data == NULL) continue;
        cok_character m;
        CHECK(cok_monster_read(&dqk, (uint8_t)id, &m));
        free(data);
        char name[16];
        CHECK(cok_field_name(L, m.record, name) > 0);
        for (size_t i = 0; i < 0x44; ++i)
            if (cok_field_pointer_at(L, (long)i)) CHECK(m.record[i] == 0);
        uint8_t usable = 0;
        static const uint8_t bits[7] = {0x02, 0x10, 0x08, 0x40, 0x80, 0x01, 0x04};
        for (int k = 0; k < 7; ++k)
            if (cok_field_u8_at(L, m.record, COK_FIELD_CLASS_LEVEL, k) > 0) usable += bits[k];
        CHECK(cok_field_u8(L, m.record, COK_FIELD_USABLE_CLASSES) == usable);
        CHECK(cok_field_u8(L, m.record, COK_FIELD_ITEM_COUNT) == m.item_count);
        size_t n = 0;
        for (const cok_effect *e = m.effects; e != NULL; e = e->next) ++n;
        if (id == 1) {
            /* The black pudding: four effects, 0x62, 0x06, 0xc1, 0xba. */
            CHECK(strcmp(name, "BLACK PUDDING") == 0 && n == 4 && m.item_count == 0);
            CHECK(m.effects->id == 0x62 && m.effects->value == 0xff &&
                  m.effects->next->id == 0x06);
        }
        if (id == 50) {
            CHECK(strcmp(name, "DARK WIZARD") == 0 && n == 10 && m.item_count == 2);
            CHECK(m.items[0][0x28] == 0x0f && m.items[0][0x28 + 3] == 0x0f && m.items[1][0x35] == 0x1e);
        }
        ++read_count;
        items += m.item_count;
        effects += n;
        cok_character_free(&m);
    }
    /* MONCHA's 98, with 106 items and 285 effects. */
    CHECK(read_count == 98 && items == 106 && effects == 285);
    cok_adventure_close(&dqk);
}

/* Dark Queen's blocks (6c70:29a3's LOAD MONSTER registers, 5f07:081b's
 * COMBAT appends and stops at the battle's placement (59c7:1864, after the open ground
 * (59c7:0e3b) or the dungeon is built, 3D or not); 5f07:07fe's CLEARMONSTERS (75cc:151b,
 * 63ee:0002) forgets the slots). Each test opens its own adventure from DQKAssets, and
 * skips without it. */
#define DQK_LOAD_MONSTER 0x0b
#define DQK_CLEARMONSTERS 0x1c
#define DQK_COMBAT 0x24

/* dq_open's keys: none unless a test types some (dq_keys). */
static script dq_quiet = {"", 0, 0, ""};

/* Types keys for the next COMBAT run of dq_open's adventure: "\r" ends the results' bar (the
 * wait after 075cc:091f) and the lost party's continue prompt (1bdb:0495). */
static void dq_keys(const char *keys)
{
    dq_quiet.keys = keys;
    dq_quiet.at = 0;
    dq_quiet.length = strlen(keys);
}

/* The Dark Queen screen (320 by 200 pixels) when the last key was read, for the results' lines:
 * dq_read copies dq_watch's screen at each read, as scripted does for watched. */
static uint8_t dq_shown[320 * 200];
static const cok_adventure *dq_watch;

/* The picture checks' keys: \x02 lets the front end's idle work run (the small picture's next
 * frame, when the menu waiting animates it: dq_frames), \x03 records whether the menu waiting
 * now animates the picture (dq_animating). Neither is a key the menu reads. */
static cok_adventure *dq_frames;
static bool dq_animating;

static int dq_read(void *context)
{
    if (dq_watch != NULL) memcpy(dq_shown, dq_watch->screen.pixels, sizeof dq_shown);
    int key = scripted(context);
    while (key == 2 || key == 3) {
        if (key == 3 && dq_frames != NULL) {
            dq_animating = cok_dark_queen_picture_animating(dq_frames);
        } else if (key == 2 && dq_frames != NULL) {
            unsigned before = cok_dark_queen_picture_frame(dq_frames);
            uint32_t ms;
            for (int i = 0; i < 10 && cok_dark_queen_picture_frame(dq_frames) == before; ++i)
                if (cok_adventure_menu_animates(dq_frames, &ms)) cok_adventure_menu_frame(dq_frames);
        }
        key = scripted(context);
    }
    return key;
}

static bool dq_open(cok_adventure *adv)
{
    static cok_keyboard keys;
    static cok_adventure_hooks hooks;
    dq_keys("");
    keys = (cok_keyboard){dq_read, &dq_quiet, NULL, NULL};
    hooks = (cok_adventure_hooks){.log = log_line, .context = &dq_quiet};
    FILE *f = fopen("DQKAssets/DISK2/MONCHA.GLB", "rb");
    if (f == NULL) return false;
    fclose(f);
    CHECK(cok_adventure_open(adv, &cok_rules_dark_queen, "DQKAssets", &keys, &hooks));
    adv->vm.file = 1;
    adv->vm.status = COK_ECL_OK;
    /* The battle's tile set is the wall set LOAD FILES names (DS:ca03; the
     * fixtures' LOAD FILES 4 4 is DUNGCOM 4): set as the block would. */
    adv->wall_ids[0] = 4;
    return true;
}

/* Runs the opcode bytes code (size of them) as a block of Dark Queen's. */
static cok_ecl_status dq_run(cok_adventure *adv, const uint8_t *code, size_t size)
{
    adv->vm.status = COK_ECL_OK;
    adv->vm.abort = false;
    memset(adv->vm.code, 0, sizeof adv->vm.code);
    memcpy(adv->vm.code, code, size);
    adv->vm.size = size + 1;
    adv->vm.depth = 0;
    adv->vm.status = cok_ecl_run(&adv->vm, COK_ECL_BASE);
    return adv->vm.status;
}
#define DQK_RUN(adv, ...) \
    dq_run(adv, (const uint8_t[]){__VA_ARGS__}, sizeof (const uint8_t[]){__VA_ARGS__})

/* COMBAT with the treasure's keys: "\r" ends the results' bar and the lost party's continue
 * prompt (the second, if unread, is none). */
static cok_ecl_status dq_combat(cok_adventure *adv)
{
    dq_keys("\r\r");
    return DQK_RUN(adv, DQK_COMBAT);
}

/* COMBAT to the treasure menu's Exit (e) and the claim question's No (n): the results' bar
 * ("\r"), the menu's Return (it beeps), then the run goes on past the treasure. */
static cok_ecl_status dq_combat_leave(cok_adventure *adv)
{
    dq_keys("\r\ren");
    return DQK_RUN(adv, DQK_COMBAT);
}

/* COMBAT with no monsters (dqk 5f07:081b's branches, in its order): the vault (var 0x31 1)
 * stops, cited (771f:09a0); the temple (var 0x14 1) runs its menu, with no key the run goes on
 * (the variable cleared, the mode 4 with no 3D area and variable 0x24 not 1, 3 where it is 1);
 * with none wanted the treasure's start runs (75cc:135c's 1297, var 0x2e, the flags) and 0593
 * (the party's after combat) stops at its call of the experience or its rest (test_dark_queen_
 * after_combat). */
static void test_dark_queen_no_monsters(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's no-monster COMBAT not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    cok_ecl_set_var(&dqk.vm, COK_VAR_VAULT, 1);
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED && cok_ecl_var(&dqk.vm, COK_VAR_VAULT) == 0);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 771f:09a0") != NULL);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_TEMPLE) == 1);

    /* The temple: no key, so its menu runs out and the branch ends with status OK. */
    cok_ecl_set_var(&dqk.vm, COK_VAR_VAULT, 0);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_ecl_var(&dqk.vm, COK_VAR_TEMPLE) == 0);
    CHECK(dqk.vm.mode == 4); /* no 3D area (0x22 0) and variable 0x24 not 1 */
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    cok_ecl_store(&dqk.vm, 0x24, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.mode == 3); /* the overland */
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    cok_ecl_store(&dqk.vm, 0x22, 1); /* a 3D area: fought (the redraw), mode 4 */
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.mode == 4);
    cok_ecl_store(&dqk.vm, 0x22, 0);
    cok_ecl_store(&dqk.vm, 0x24, 0);

    /* The treasure: nothing wanted. 75cc:135c's start clears the surprise byte (var 0x2e),
     * then 0593 runs over the party (none here) and stops at its rest, cited. A weapon lost
     * in combat stops at 1297 first (no battle runs here, so the list is empty unless the
     * test adds one). */
    cok_ecl_set_var(&dqk.vm, COK_VAR_SURPRISE, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_ecl_var(&dqk.vm, COK_VAR_SURPRISE) == 0);
    CHECK(cok_dark_queen_end_state_of(&dqk).b7a7); /* no party: lost, the continue prompt answered */
    dqk.lost_weapons = calloc(1, sizeof *dqk.lost_weapons);
    dqk.lost_weapon_count = 1;
    cok_ecl_set_var(&dqk.vm, COK_VAR_SURPRISE, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED && cok_ecl_var(&dqk.vm, COK_VAR_SURPRISE) == 1);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 75cc:1297") != NULL);
    free(dqk.lost_weapons);
    dqk.lost_weapons = NULL;
    dqk.lost_weapon_count = 0;
}

/* Adds a party member with status and active, as the temple's test does. */
static cok_character *dq_member(cok_adventure *adv, uint8_t status, uint8_t active)
{
    const cok_character_layout *L = cok_rules_dark_queen.character;
    cok_character *m = calloc(1, sizeof *m);
    CHECK(m != NULL && cok_party_add(&adv->party, &cok_rules_dark_queen, m));
    cok_field_set8(L, m->record, COK_FIELD_STATUS, status);
    cok_field_set8(L, m->record, COK_FIELD_ACTIVE, active);
    return m;
}

/* 75cc:01fb and 0427 (part 19b), with nothing past the party: the share is the pool's coins
 * (steel 2, gems 250, jewelry 2200) over the party less those out (var 0x20 less DS:b1da, which
 * 01fb decrements where var 0x20 is no more), and the award goes to each standing member by its
 * class (10% more above the prime requisites, half or a third for the classes 8-16), unless the
 * bank pays out (var 0x1f). The pool's items and the records past the party are in
 * test_dark_queen_pool_and_past_party. */
static void test_dark_queen_experience(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's experience not run");
        return;
    }
    const cok_character_layout *L = cok_rules_dark_queen.character;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    cok_character *a = dq_member(&dqk, 0, 1), *b = dq_member(&dqk, 0, 1);
    /* 2452 (1 steel, 1 gems, 1 jewelry) over two members: 1226 each. */
    dqk.pool.coins[4] = dqk.pool.coins[5] = dqk.pool.coins[6] = 1;
    cok_ecl_store(&dqk.vm, 0x20, 2);
    dq_combat_leave(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 4);
    CHECK(cok_field_u32(L, a->record, COK_FIELD_EXPERIENCE) == 1226);
    CHECK(cok_field_u32(L, b->record, COK_FIELD_EXPERIENCE) == 1226);
    CHECK(cok_dark_queen_end_state_of(&dqk).out == 0);

    /* A fighter (class 2, strength 16) gets 10% more: 122 + 1226; a class 8 gets half: 613. */
    cok_field_set32(L, a->record, COK_FIELD_EXPERIENCE, 0);
    cok_field_set32(L, b->record, COK_FIELD_EXPERIENCE, 0);
    cok_field_set8(L, a->record, COK_FIELD_CLASS, 2);
    cok_field_set8_at(L, a->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 16);
    cok_field_set8(L, b->record, COK_FIELD_CLASS, 8);
    dqk.pool.coins[4] = dqk.pool.coins[5] = dqk.pool.coins[6] = 1; /* the pool is freed at Exit */
    dq_combat_leave(&dqk);
    CHECK(cok_field_u32(L, a->record, COK_FIELD_EXPERIENCE) == 1348);
    CHECK(cok_field_u32(L, b->record, COK_FIELD_EXPERIENCE) == 613);

    /* The bank pays out (var 0x1f): 0427 gives nothing. */
    cok_field_set32(L, a->record, COK_FIELD_EXPERIENCE, 0);
    cok_field_set32(L, b->record, COK_FIELD_EXPERIENCE, 0);
    cok_ecl_store(&dqk.vm, 0x1f, 1);
    dqk.pool.coins[4] = dqk.pool.coins[5] = dqk.pool.coins[6] = 1; /* the pool is freed at Exit */
    dq_combat_leave(&dqk);
    CHECK(cok_field_u32(L, a->record, COK_FIELD_EXPERIENCE) == 0);
    cok_ecl_store(&dqk.vm, 0x1f, 0);

    /* A member out (status 1 counts it out, and it is standing too) and var 0x20 at 0: 0593's
     * out is 1, 01fb takes it down to 0 (var 0x20 is no more), and the divisor is 0. LDIV@ gives
     * 0xffffffff for the pool's 2452 (2a72: no error). a (class 2, str 16) gets LDIV(-1, 10) + -1
     * = -1, so 0xffffffff; b, out, is not awarded. The stop is at 0736. */
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 1);
    cok_ecl_store(&dqk.vm, 0x20, 0);
    cok_field_set32(L, a->record, COK_FIELD_EXPERIENCE, 0);
    cok_field_set32(L, b->record, COK_FIELD_EXPERIENCE, 0);
    dqk.pool.coins[4] = dqk.pool.coins[5] = dqk.pool.coins[6] = 1;
    dq_combat_leave(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 4);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 0xffffffffu);
    CHECK(cok_field_u32(L, a->record, COK_FIELD_EXPERIENCE) == 0xffffffffu);
    CHECK(cok_field_u32(L, b->record, COK_FIELD_EXPERIENCE) == 0);
    CHECK(cok_dark_queen_end_state_of(&dqk).out == 0);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 0);
    cok_ecl_store(&dqk.vm, 0x20, 2);
    cok_field_set8(L, a->record, COK_FIELD_CLASS, 0);
    cok_field_set8(L, b->record, COK_FIELD_CLASS, 0);
    cok_field_set8_at(L, a->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 0);
    dqk.pool.coins[6] = 0;
}

/* Adds a record past the party (not_party 1, a combat record) with hostile 1, the status given,
 * its coins (steel 5, gems 7, jewelry 11) and its experience (+0x54, 100). */
static cok_character *dq_past_party(cok_adventure *adv, uint8_t status)
{
    const cok_character_layout *L = cok_rules_dark_queen.character;
    cok_character *p = dq_member(adv, status, 1);
    p->combat = calloc(1, sizeof *p->combat);
    CHECK(p->combat != NULL);
    p->combat->not_party = 1;
    cok_field_set8(L, p->record, COK_FIELD_HOSTILE, 1);
    cok_field_set16_at(L, p->record, COK_FIELD_COINS, 4, 5);
    cok_field_set16_at(L, p->record, COK_FIELD_COINS, 5, 7);
    cok_field_set16_at(L, p->record, COK_FIELD_COINS, 6, 11);
    cok_field_set16(L, p->record, COK_FIELD_MONSTER_EXPERIENCE, 100);
    return p;
}

/* Empties the pool (its coins and its items). */
static void dq_clear_pool(cok_adventure *adv)
{
    cok_pool_free_items(&adv->pool);
    for (int k = 0; k < COK_COINS; ++k) adv->pool.coins[k] = 0;
}

/* Puts an item of type 0x27 with the bonus at the head of the pool. */
static void dq_pool_item(cok_adventure *adv, uint8_t bonus)
{
    const cok_rules_item_record *ir = &cok_rules_dark_queen.item_record;
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[ir->type] = 0x27;
    it[ir->bonus] = bonus;
    CHECK(cok_pool_insert(&adv->pool, 0, it));
}

/* 75cc:01fb's pool items, the division and the records past the party (part 19b). An item
 * with a bonus above 0 adds 400 times it (a signed word: 82 is -32736), one with 0 or below
 * adds nothing; the last missile recovered (pool.missile, 1 + its index) stops the walk before
 * its item. The share is LDIV@ (2a72): a divisor of 0 gives 0xffffffff, a negative dividend 1.
 * A record past the party (not_party 1, hostile 1, status not 0, 3 or 9) sets DS:c7df, adds its
 * coins to the pool's 4-6 and its +0x54 to the sum, unless var 0x1e keeps its items (2a5); an
 * item whose +0x28 is not 0x69 stops at 01fb's item move (02cb, part 19b, cited). */
static void test_dark_queen_pool_and_past_party(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's pool items and past party not run");
        return;
    }
    const cok_character_layout *L = cok_rules_dark_queen.character;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    cok_character *a = dq_member(&dqk, 0, 1), *b = dq_member(&dqk, 0, 1);
    cok_ecl_store(&dqk.vm, 0x20, 1);

    /* Pool items over a and b, both standing (out 0), var 0x20 1: the divisor is 1. */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dq_combat_leave(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 4);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 1200); /* 3 * 400 */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 82);
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == (uint32_t)-32736); /* 32800 as a word */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 0xff); /* -1: nothing */
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 0);
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 0); /* 0: nothing */
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 0);

    /* The missile: 1 + its index. Missile 1 stops before the first item; with two items and
     * missile 2 the first counts and the second is stopped before. */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dqk.pool.missile = 1;
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 0);
    dq_pool_item(&dqk, 3); /* now two items; the missile's index moves on with the insert */
    dqk.pool.missile = 2;
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 1200);

    /* Division by LDIV@ (no error): one out (b) and var 0x20 0 is a divisor of 0 (01fb's
     * decrement, 0 - 0). The pool's nothing is 0xffffffff; a negative dividend over 0 is 1;
     * over 2, the signed quotient. (With nobody out, the decrement wraps 0 to 255: -255.) */
    dq_clear_pool(&dqk);
    cok_ecl_store(&dqk.vm, 0x20, 0);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 0xffffffffu);
    dq_pool_item(&dqk, 82); /* -32736 over 0 */
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == 1);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 0);
    cok_ecl_store(&dqk.vm, 0x20, 2);
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == (uint32_t)-16368);

    /* A divisor of -1: var 0x20 0 with two out (b and a third, c) is 0 - 1 after 01fb's
     * decrement, and the pool's 1226 steel is 2452 over -1. */
    dq_clear_pool(&dqk);
    cok_ecl_store(&dqk.vm, 0x20, 0);
    dqk.pool.coins[4] = 1226;
    cok_character *c = dq_member(&dqk, 1, 1);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 1);
    cok_field_set8(L, a->record, COK_FIELD_CLASS, 8); /* half: LDIV@ of the negative share */
    cok_field_set32(L, a->record, COK_FIELD_EXPERIENCE, 0);
    dq_combat(&dqk);
    CHECK(cok_dark_queen_end_state_of(&dqk).share == (uint32_t)-2452);
    CHECK(cok_field_u32(L, a->record, COK_FIELD_EXPERIENCE) == (uint32_t)-1226);
    cok_field_set8(L, a->record, COK_FIELD_CLASS, 0);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 0);
    cok_party_remove(&dqk.party, dqk.party.count - 1); /* c */
    CHECK(c != NULL && dqk.party.count == 2);

    /* A record past the party: not hostile, or status 0, 3 or 9, adds no coins and no sum (01fb).
     * 11b0 still removes it (its not_party 1, part 19d) and sets DS:c7df. a and b stand, so the
     * experience runs and its share is 0 with nothing in the pool. */
    cok_ecl_store(&dqk.vm, 0x20, 2);
    static const uint8_t quiet[] = {0, 3, 9, 2};
    for (size_t k = 0; k < sizeof quiet; ++k) {
        dq_clear_pool(&dqk);
        cok_dark_queen_set_monsters_present_for_test(&dqk, false);
        cok_character *p = dq_past_party(&dqk, quiet[k]);
        if (k == sizeof quiet - 1) cok_field_set8(L, p->record, COK_FIELD_HOSTILE, 0);
        dq_combat(&dqk);
        CHECK(dqk.pool.coins[4] == 0 && cok_dark_queen_end_state_of(&dqk).monsters_present);
        CHECK(dqk.party.count == 2); /* 11b0 removed p */
    }

    /* A record past the party, hostile with status 2: its coins (5, 7, 11) go to the pool's 4-6
     * and its +0x54 (100) joins the sum: 100 + 5 * 2 + 7 * 250 + 11 * 2200 over var 0x20 2. The
     * party's out is 0 (the loops stop at it), and DS:c7df is set. */
    dq_clear_pool(&dqk);
    cok_dark_queen_set_monsters_present_for_test(&dqk, false);
    cok_character *p = dq_past_party(&dqk, 2);
    dq_combat(&dqk); /* the menu's keys run out: the pool is kept for the checks below */
    CHECK(dqk.pool.coins[4] == 5 && dqk.pool.coins[5] == 7 && dqk.pool.coins[6] == 11);
    CHECK(cok_dark_queen_end_state_of(&dqk).monsters_present);
    CHECK(cok_dark_queen_end_state_of(&dqk).share ==
          (uint32_t)((100 + 5 * 2 + 7 * 250 + 11 * 2200) / 2));

    /* Its item: var 0x1e keeps it (2a5, no stop); without, its first item not 'i' (+0x28 not
     * 0x69) stops at 01fb's item move (02cb, cited) before 11b0 removes the record; 'i' is no
     * move. Each run after one that removes the record is given a new one. */
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[cok_rules_dark_queen.item_record.type] = 0x27;
    p = dq_past_party(&dqk, 2);
    CHECK(cok_character_insert_item(p, p->item_count, it));
    cok_ecl_store(&dqk.vm, 0x1e, 1);
    dq_combat_leave(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 4);
    cok_ecl_store(&dqk.vm, 0x1e, 0);
    p = dq_past_party(&dqk, 2);
    CHECK(cok_character_insert_item(p, p->item_count, it));
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED &&
          strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 75cc:02cb") != NULL);
    p->items[0][cok_rules_dark_queen.item_record.type] = 0x69;
    dq_combat_leave(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 4);
}

/* Whether item's file bytes (the item record's file_size at file_at) are the hex string. */
static bool dq_file_is(const uint8_t *item, const char *hex)
{
    const cok_rules_item_record *ir = &cok_rules_dark_queen.item_record;
    for (size_t k = 0; k < ir->file_size; ++k) {
        unsigned byte;
        if (sscanf(hex + 2 * k, "%2x", &byte) != 1 || item[ir->file_at + k] != byte) return false;
    }
    return hex[2 * ir->file_size] == '\0';
}

/* TREASURE ITEMS (6c70:2127, dark_queen.c's treasure_items): the header's three words (word 1000,
 * then imm 3 and 1: steel, gems, jewelry) add to the pool's coins 4-6; the fourth (n) is the list's
 * count. The items (218f-22a3): id 80 and 1 from the table, 0xff the random scroll (1a83: a
 * White Mage's and a Red Mage's, drawn from RandSeed), 0 and an id whose entry starts 0xff none;
 * each goes first in the pool, its bytes at +0x28 as the table has them, its weight (+0x2c) and
 * words 2-3 kept (pool_put puts back the bytes cok_pool_insert clears). */
static void test_dark_queen_treasure_items(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's TREASURE ITEMS not run");
        return;
    }
    dq_clear_pool(&dqk);
    DQK_RUN(&dqk, 0x27, 2, 0xe8, 0x03, 0, 3, 0, 1, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(dqk.pool.coins[4] == 1000 && dqk.pool.coins[5] == 3 && dqk.pool.coins[6] == 1);
    CHECK(dqk.pool.item_count == 0);

    /* TREASURE ITEMS 0 0 0 5 : 1 0 255 255 80 from RandSeed 12345: the pool, head first, is id 80,
     * the Red Mage's scroll (the second 0xff), the White Mage's (the first), id 1; 16 draws. */
    dq_clear_pool(&dqk);
    dqk.vm.seed = 12345;
    DQK_RUN(&dqk, 0x27, 0, 0, 0, 0, 0, 0, 0, 5, 0, 1, 0, 0, 0, 255, 0, 255, 0, 80);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(dqk.pool.item_count == 4);
    CHECK(dq_file_is(dqk.pool.items[0], "1e00701e0200dc0502000006000a000000"));
    CHECK(dq_file_is(dqk.pool.items[1], "276627880100b80b0100001000002e5b0f"));
    CHECK(dq_file_is(dqk.pool.items[2], "276627890100b80b0100002000005c540a"));
    CHECK(dq_file_is(dqk.pool.items[3], "1e00001e0100000000000000000a000000"));
    CHECK(dqk.vm.seed == 0x476726e9);
    CHECK(dqk.pool.coins[4] == 0 && dqk.pool.coins[5] == 0 && dqk.pool.coins[6] == 0);

    /* The weight of id 80 (+0x2c) is 2, kept after the insert (it clears +0x2a-+0x2d). */
    CHECK(dqk.pool.items[0][0x2c] == 2);

    /* An id whose table entry starts 0xff adds nothing; the coins still add. */
    dq_clear_pool(&dqk);
    dqk.item_table[7][0] = 0xff;
    DQK_RUN(&dqk, 0x27, 0, 1, 0, 0, 0, 0, 0, 1, 0, 7);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(dqk.pool.item_count == 0 && dqk.pool.coins[4] == 1);
    dq_clear_pool(&dqk);
}

/* The random scroll (6c70:1a83, 1a4e) from RandSeed 12345: a White Mage's scroll (r 1) with the
 * spells 0x5c, 0x54 and 0x0a, 9 draws; then a Red Mage's (r 2, 7 draws) and RandSeed 0x476726e9. */
static void test_dark_queen_random_scroll(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's random scroll not run");
        return;
    }
    uint8_t entry[COK_ITEM_ENTRY];
    uint32_t seed = 12345;
    cok_dark_queen_random_scroll(cok_game_rules(&dqk), &seed, entry);
    CHECK(seed == 0x44bac876);
    CHECK(memcmp(entry, "\x27\x66\x27\x89\x01\x00\xb8\x0b\x01\x00\x00\x20\x00\x00\x5c\x54\x0a",
                 COK_ITEM_ENTRY) == 0);
    cok_dark_queen_random_scroll(cok_game_rules(&dqk), &seed, entry);
    CHECK(seed == 0x476726e9);
    CHECK(memcmp(entry, "\x27\x66\x27\x88\x01\x00\xb8\x0b\x01\x00\x00\x10\x00\x00\x2e\x5b\x0f",
                 COK_ITEM_ENTRY) == 0);
}

/* 75cc:135c's treasure branch with no monsters, through 0593 (the party's after combat). The
 * flags = <> < > are cleared and <= >= kept (2093:0015 with DS:b2f5 and 4), var 0x2e is cleared;
 * 0593 sets b7a7, c7de, af3f and b1da from the records (its loops 1 to 3), removes the effects that
 * end with the battle (DS:631e; a record holding one stops, cited, part 27); when a member stands
 * it runs 01fb and 0427 (the experience and the award), then stops at its rest (75cc:0736). The
 * demonstration (DS:ae59) skips 0593 (1390 on, cited). */
static void test_dark_queen_after_combat(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's after combat not run");
        return;
    }
    const cok_character_layout *L = cok_rules_dark_queen.character;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    cok_character *a = dq_member(&dqk, 0, 1), *b = dq_member(&dqk, 0, 1);
    cok_dark_queen_end_state st;

    /* Standing (status 0): 01fb and 0427, then 0736; the flags: four cleared, [4] and [5] kept.
     * DS:c7df is cleared by 1380 (set before; 11b0 would set it, part 19c). The party is a and
     * b, both standing: nothing is in the pool, so the share is 0 and the award adds nothing. */
    cok_dark_queen_set_monsters_present_for_test(&dqk, true);
    for (int k = 0; k < 6; ++k) dqk.vm.flags[k] = true;
    cok_ecl_set_var(&dqk.vm, COK_VAR_SURPRISE, 1);
    cok_ecl_store(&dqk.vm, 0x20, 2);
    cok_field_set8(L, a->record, COK_FIELD_AC, 99); /* 177e:0be2 recomputes the record's AC */
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(cok_field_u8(L, a->record, COK_FIELD_AC) != 99);
    CHECK(cok_field_u32(L, a->record, COK_FIELD_EXPERIENCE) == 0);
    CHECK(dqk.vm.flags[0] && !dqk.vm.flags[1] && !dqk.vm.flags[2] && !dqk.vm.flags[3]); /* 1427 */
    CHECK(dqk.vm.flags[4] && dqk.vm.flags[5]);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_SURPRISE) == 0);
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(st.standing && !st.fled && st.out == 0 && !st.b7a7 && !st.monsters_present);
    CHECK(dqk.vm.mode == 4); /* COMBAT's ending after 135c: no 3D area, var 0x24 not 1 */

    /* Fled (status 3): 0736; c7de from loop 1, b7a7 cleared by loop 3 (67d-68f). */
    cok_field_set8(L, a->record, COK_FIELD_STATUS, 3);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 3);
    dq_pool_item(&dqk, 3); /* 135c's fled: the pool's items are freed (13e7), the flags 0 kept, 1 set */
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(dqk.pool.items == NULL && dqk.pool.item_count == 0);
    CHECK(!dqk.vm.flags[0] && dqk.vm.flags[1]); /* 13ae sets flag 1; 1427 is not run */
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(!st.standing && st.fled && st.out == 0 && !st.b7a7);
    dq_clear_pool(&dqk);

    /* Lost (status 2, inactive): nobody found, b7a7 stays set, both count out. The load is
     * asked, so 0736 removes the party (818-86e: 1e31 each, the count to 0) and the members
     * are made again for the rest of the test (a and b are freed). */
    cok_field_set8(L, a->record, COK_FIELD_STATUS, 2);
    cok_field_set8(L, a->record, COK_FIELD_ACTIVE, 0);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 2);
    cok_field_set8(L, b->record, COK_FIELD_ACTIVE, 0);
    dq_combat(&dqk);
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the continue prompt's key */
    CHECK(!st.standing && !st.fled && st.out == 2 && st.b7a7);
    CHECK(dqk.party.count == 0 && cok_ecl_var(&dqk.vm, COK_VAR_PARTY_SIZE) == 0);
    a = dq_member(&dqk, 2, 0);
    b = dq_member(&dqk, 2, 0);

    /* The gentle variable (0x1d) clears b7a7 when a member is found (active, status 2). A
     * b7a7 that stays set removes the party (818), so the members are made again for each run. */
    cok_field_set8(L, a->record, COK_FIELD_ACTIVE, 1);
    dq_combat(&dqk);
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(st.b7a7 && st.out == 1 && dqk.party.count == 0);
    a = dq_member(&dqk, 2, 1);
    b = dq_member(&dqk, 2, 0);
    cok_ecl_store(&dqk.vm, 0x1d, 1);
    dq_combat(&dqk);
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(!st.b7a7 && st.out == 1 && dqk.party.count == 2);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    cok_ecl_store(&dqk.vm, 0x1d, 0);

    /* The status and hostile are words (0593's cmpw, 5b7 and 680): a high byte of 1 makes
     * a's status 0x0100, not standing, and its hostile 0x0100, not 0, so loop 3 does not
     * clear b7a7 for it. Read as bytes, a would stand and b7a7 would clear. */
    cok_party_free(&dqk.party);
    a = dq_member(&dqk, 0, 1);
    b = dq_member(&dqk, 2, 0);
    cok_field_set8(L, a->record, COK_FIELD_STATUS, 0);
    cok_field_set8(L, a->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 2);
    cok_field_set8(L, b->record, COK_FIELD_ACTIVE, 0);
    a->record[0x65] = 1;
    a->record[0x67] = 1;
    dq_combat(&dqk);
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the continue prompt's key */
    CHECK(!st.standing && st.b7a7 && st.out == 1); /* b is inactive: out; a is not */

    /* An effect that ends with the battle (0x03, the first of DS:631e) on a record is removed
     * (017d, no flag: no handler); one that does not (0x01) stays. */
    cok_party_free(&dqk.party);
    a = dq_member(&dqk, 0, 1);
    b = dq_member(&dqk, 0, 1);
    cok_field_set8(L, a->record, COK_FIELD_STATUS, 0);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 0);
    cok_field_set8(L, b->record, COK_FIELD_ACTIVE, 1);
    CHECK(cok_character_add_effect(b, 0x01, 0, 0, false) != NULL);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(cok_character_add_effect(a, 0x03, 0, 0, false) != NULL);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_character_find_effect(a, 0x03) == NULL);
    CHECK(cok_character_find_effect(b, 0x01) != NULL);

    /* The demonstration skips 0593: the stop is at 1390 (19c), and 0593's state stays. */
    st = cok_dark_queen_end_state_of(&dqk);
    dqk.demo = true;
    for (int k = 0; k < 6; ++k) dqk.vm.flags[k] = true;
    dq_combat(&dqk);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 75cc:1390") != NULL);
    CHECK(!dqk.vm.flags[0] && !dqk.vm.flags[3] && dqk.vm.flags[4] && dqk.vm.flags[5]);
    CHECK(cok_dark_queen_end_state_of(&dqk).b7a7 == st.b7a7 &&
          cok_dark_queen_end_state_of(&dqk).out == st.out);
    dqk.demo = false;
    cok_party_free(&dqk.party);
}

/* 75cc:11b0, after 0593 (135c, part 19d): a record past the party (its combat record's not_party
 * 1) is removed without the party's size (177e:1e31's first argument 1), and a hostile record in
 * the party (hostile word 1, inactive) is removed with it one less (its variable 0x20 counts 3
 * down to 2 once; the record past the party leaves it at 2). DS:c7df is set by either, variable
 * 0x15 counts the inactive one, the selection ends on the first record (1287), and a record that
 * stays loses its combat block (123b-126a). The stop is at 135c's 13a4. */
static void test_dark_queen_enemies(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's enemies not run");
        return;
    }
    const cok_character_layout *L = cok_rules_dark_queen.character;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    cok_party_free(&dqk.party);
    cok_character *a = dq_member(&dqk, 0, 1);
    cok_character *h = dq_member(&dqk, 2, 0);
    cok_field_set8(L, h->record, COK_FIELD_HOSTILE, 1);
    cok_character *b = dq_member(&dqk, 0, 1);
    dq_past_party(&dqk, 0); /* m: a monster past the party, active */
    a->combat = calloc(1, sizeof *a->combat);
    CHECK(a->combat != NULL);
    cok_ecl_store(&dqk.vm, 0x20, 3);
    cok_dark_queen_set_monsters_present_for_test(&dqk, false);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(dqk.party.count == 2 && dqk.party.members[0] == a && dqk.party.members[1] == b);
    CHECK(cok_ecl_load_var(&dqk.vm, 0x15) == 1); /* h, inactive; m is active */
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_PARTY_SIZE) == 2); /* h counted, m not */
    CHECK(cok_dark_queen_end_state_of(&dqk).monsters_present);
    CHECK(dqk.vm.character == a->record); /* 1287, after b's selection (1f07) */
    CHECK(a->combat == NULL);
    cok_party_free(&dqk.party);

    /* REVIEW: var 0x15 cleared first (11ba) and counted for the inactive alone (11f4); the
     * hostile test a word (11dd: 0x0101 stays); loop 2 walks past the party (5e5, no bound). */
    a = dq_member(&dqk, 0, 1);
    cok_character *hw = dq_member(&dqk, 0, 1);
    hw->record[0x66] = 1;
    hw->record[0x67] = 1;
    cok_character *p1 = dq_past_party(&dqk, 0);
    cok_field_set8(L, p1->record, COK_FIELD_ACTIVE, 0);
    cok_character *p2 = dq_past_party(&dqk, 0);
    cok_field_set8(L, p2->record, COK_FIELD_ACTIVE, 0);
    dq_past_party(&dqk, 0); /* active */
    cok_ecl_store(&dqk.vm, 0x15, 7);
    cok_ecl_store(&dqk.vm, 0x20, 2);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(cok_ecl_load_var(&dqk.vm, 0x15) == 2);
    CHECK(dqk.party.count == 2 && dqk.party.members[1] == hw);
    cok_party_free(&dqk.party);
    dq_past_party(&dqk, 0);
    cok_ecl_store(&dqk.vm, 0x1d, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(!cok_dark_queen_end_state_of(&dqk).b7a7 && dqk.party.count == 0);
    cok_ecl_store(&dqk.vm, 0x1d, 0);
}

/* Whether cells x1-x2 of the text row of the snapshot hold ink: a pixel other than the window's
 * grey (0x17), which full_window clears the cells to before a line is drawn on it. */
static bool dq_ink(const uint8_t *snap, int row, int x1, int x2)
{
    for (int y = 8 * row; y < 8 * row + 8; ++y)
        for (int x = 8 * x1; x < 8 * (x2 + 1); ++x)
            if (snap[y * 320 + x] != 0x17) return true;
    return false;
}

/* Whether the text row is the same pixel for pixel in two snapshots. */
static bool dq_same_line(const uint8_t *a, const uint8_t *b, int row)
{
    for (int y = 8 * row; y < 8 * row + 8; ++y)
        for (int x = 8; x < 8 * 39; ++x)
            if (a[y * 320 + x] != b[y * 320 + x]) return false;
    return true;
}

/* One results scene in adv (a new adventure: the screen keeps its lines): a member of status and
 * active, a monster past the party (with the experience) when monster is set, the vars 0x1d and
 * 0x1f, then COMBAT with keys. The screen when the last key is read is snap: the results' bar
 * (or the lost party's continue prompt) waits for the first key, which keys does not give. */
static void dq_results_scene(cok_adventure *adv, uint8_t status, uint8_t active, bool monster,
                             uint16_t experience, uint16_t var1d, uint16_t var1f, uint8_t *snap)
{
    DQK_RUN(adv, DQK_CLEARMONSTERS);
    cok_party_free(&adv->party);
    dq_member(adv, status, active);
    if (monster) {
        cok_character *m = dq_past_party(adv, 0);
        cok_field_set16(cok_rules_dark_queen.character, m->record, COK_FIELD_MONSTER_EXPERIENCE,
                        experience);
    }
    cok_ecl_store(&adv->vm, 0x1d, var1d);
    cok_ecl_store(&adv->vm, 0x1f, var1f);
    cok_ecl_store(&adv->vm, 0x20, 2);
    dq_keys("");
    memset(dq_shown, 0, sizeof dq_shown);
    dq_watch = adv;
    DQK_RUN(adv, DQK_COMBAT);
    dq_watch = NULL;
    memcpy(snap, dq_shown, sizeof dq_shown);
}

/* A fled party (a status 3 member, nobody standing: a standing one would clear the fled flag,
 * 694-6aa) with a killed monster of the given experience: the share is the experience over the
 * party's count (var 0x20), which the results print. */
static void share_scene(cok_adventure *adv, uint16_t experience, uint8_t *snap)
{
    DQK_RUN(adv, DQK_CLEARMONSTERS);
    cok_party_free(&adv->party);
    dq_member(adv, 3, 1);
    cok_character *m = dq_past_party(adv, 2); /* a killed monster: its experience counts */
    cok_field_set16(cok_rules_dark_queen.character, m->record, COK_FIELD_MONSTER_EXPERIENCE,
                    experience);
    cok_ecl_store(&adv->vm, 0x1d, 0);
    cok_ecl_store(&adv->vm, 0x1f, 0);
    cok_ecl_store(&adv->vm, 0x20, 1);
    dq_keys("");
    memset(dq_shown, 0, sizeof dq_shown);
    dq_watch = adv;
    DQK_RUN(adv, DQK_COMBAT);
    dq_watch = NULL;
    memcpy(snap, dq_shown, sizeof dq_shown);
}

/* COMBAT's results and the lost party's lines (75cc:091f, 1460), on the screen when the bar or
 * the continue prompt waits for its key (dq_read). The rows: 3 the result, 5 and 7 "each
 * character receives" and "experience points." (unless var 0x1f), 10 the treasure. The fled
 * share is 0 whatever the monster's experience; the fled coins are cleared before the treasure
 * menu. The vars and the monsters are cleared at the end (14eb-151b). */
static void test_dark_queen_results(void)
{
    static cok_adventure won, won_1f, lost_fight, lost_party, all, share_a, share_b, coins;
    static uint8_t snap[7][320 * 200];
    cok_adventure *every[] = {&won, &won_1f, &lost_fight, &lost_party, &all, &share_a, &share_b,
                              &coins};
    for (size_t k = 0; k < sizeof every / sizeof every[0]; ++k)
        if (!dq_open(every[k])) {
            puts("monster: DQKAssets not found; Dark Queen's results not run");
            return;
        }

    /* The won result: a standing member and a monster past the party (var 0x1d set: the member
     * is standing, so the result is won). Rows 3, 5, 7 and 10. */
    dq_results_scene(&won, 0, 1, true, 100, 1, 0, snap[0]);
    CHECK(dq_ink(snap[0], 3, 1, 16) && dq_ink(snap[0], 5, 1, 16) && dq_ink(snap[0], 7, 1, 16));
    CHECK(dq_ink(snap[0], 10, 1, 16));
    /* Var 0x1f set: rows 5 and 7 are not printed, the result still is. */
    dq_results_scene(&won_1f, 0, 1, true, 100, 1, 1, snap[1]);
    CHECK(dq_ink(snap[1], 3, 1, 16) && !dq_ink(snap[1], 5, 1, 16) && !dq_ink(snap[1], 7, 1, 16));
    CHECK(dq_same_line(snap[0], snap[1], 3));

    /* The lost result: nobody standing (a status 2 and found by var 0x1d, so b7a7 is clear) and
     * a monster past the party: "you have lost the fight." on row 3. */
    dq_results_scene(&lost_fight, 2, 1, true, 100, 1, 0, snap[2]);
    CHECK(dq_ink(snap[2], 3, 1, 16) && !cok_dark_queen_end_state_of(&lost_fight).b7a7);

    /* The lost party (b7a7: the member out, no monster): the three words on rows 5 to 7, then
     * the continue prompt, which waits. */
    cok_ecl_store(&lost_party.vm, 0x12, 4); /* the text's speed: 12 ms a character */
    lost_party.vm.seed = 12345;
    dq_results_scene(&lost_party, 2, 0, false, 0, 0, 0, snap[3]);
    /* DS:ae65 is set (14eb, the start): each of the 98 characters draws random(1000) (1a62:0311's
     * 1df9:052a), the original's RandSeed at 5f07:0987 in dqk_treasure's lost flow. */
    CHECK(lost_party.vm.seed == 0xfc7bdf17u);
    CHECK(dq_ink(snap[3], 5, 2, 16) && dq_ink(snap[3], 6, 2, 16) && dq_ink(snap[3], 7, 2, 16));
    CHECK(cok_dark_queen_end_state_of(&lost_party).b7a7);

    /* The end of the results with the key: vars 0x01, 0x2f, 0x1c, 0x1e, 0x1d and 0x1f are
     * cleared (14eb-150d), and 151b clears the monsters: the undead, the loaded flag and count,
     * and the next icon slot (8). */
    static const uint16_t vars[] = {0x01, 0x2f, 0x1c, 0x1e, 0x1d, 0x1f};
    DQK_RUN(&all, DQK_CLEARMONSTERS);
    cok_party_free(&all.party);
    dq_member(&all, 0, 1);
    dq_past_party(&all, 0);
    for (size_t k = 0; k < sizeof vars / sizeof vars[0]; ++k) cok_ecl_store(&all.vm, vars[k], 1);
    all.undead = 3;
    all.icon_slot = 12;
    cok_ecl_store(&all.vm, 0x20, 2);
    dq_combat(&all);
    CHECK(all.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the result's key, no more */
    for (size_t k = 0; k < sizeof vars / sizeof vars[0]; ++k)
        CHECK(cok_ecl_load_var(&all.vm, vars[k]) == 0);
    CHECK(all.undead == 0 && all.icon_slot == 8);

    /* The fled party with a monster's experience (100) and with none: the line "each character
     * receives" is the same pixel for pixel, the share is 0 when fled. */
    /* DS:ae4f is written by 01fb alone (75cc:0716: a member standing), so a fled COMBAT after a
     * won one keeps the won share (here it stops at the treasure menu, its share made): 091f's
     * 957-95c prints 0 all the same. */
    DQK_RUN(&share_a, DQK_CLEARMONSTERS);
    cok_party_free(&share_a.party);
    dq_member(&share_a, 0, 1);
    cok_field_set16(cok_rules_dark_queen.character, dq_past_party(&share_a, 2)->record,
                    COK_FIELD_MONSTER_EXPERIENCE, 100);
    cok_ecl_store(&share_a.vm, 0x1d, 0);
    cok_ecl_store(&share_a.vm, 0x1f, 0);
    cok_ecl_store(&share_a.vm, 0x20, 2);
    dq_combat(&share_a);
    uint32_t won_share = cok_dark_queen_end_state_of(&share_a).share;
    CHECK(won_share != 0);
    share_scene(&share_a, 100, snap[4]);
    CHECK(cok_dark_queen_end_state_of(&share_a).share == won_share);
    share_scene(&share_b, 0, snap[5]);
    CHECK(dq_ink(snap[4], 5, 1, 16) && dq_ink(snap[5], 5, 1, 16));
    CHECK(dq_same_line(snap[4], snap[5], 5));

    /* The lost fight after a won one in the same adventure: 9d7-9dc sets the share 0 (the line
     * "each character receives 0", rows 5 and 7), though DS:ae4f keeps the won share (01fb alone
     * writes it). */
    DQK_RUN(&lost_fight, DQK_CLEARMONSTERS);
    cok_party_free(&lost_fight.party);
    dq_member(&lost_fight, 0, 1);
    cok_field_set16(cok_rules_dark_queen.character, dq_past_party(&lost_fight, 2)->record,
                    COK_FIELD_MONSTER_EXPERIENCE, 100);
    cok_ecl_store(&lost_fight.vm, 0x1d, 0);
    cok_ecl_store(&lost_fight.vm, 0x1f, 0);
    cok_ecl_store(&lost_fight.vm, 0x20, 2);
    dq_combat(&lost_fight);
    CHECK(cok_dark_queen_end_state_of(&lost_fight).share != 0);
    dq_results_scene(&lost_fight, 2, 1, true, 100, 1, 0, snap[6]);
    CHECK(dq_same_line(snap[2], snap[6], 5) && dq_same_line(snap[2], snap[6], 7));

    /* The fled coins (4-6) and items go before the treasure menu (091f's 934 and 13e7): with
     * the key, the run goes on past the results (no 0e93 stop), and the coins are 0 at the end. */
    DQK_RUN(&coins, DQK_CLEARMONSTERS);
    cok_party_free(&coins.party);
    dq_member(&coins, 3, 1);
    dq_past_party(&coins, 2);
    coins.pool.coins[4] = 5;
    coins.pool.coins[6] = 7;
    dq_combat(&coins);
    CHECK(coins.vm.status == COK_ECL_OK && dq_quiet.at == 1);
    CHECK(coins.pool.coins[4] == 0 && coins.pool.coins[5] == 0 && coins.pool.coins[6] == 0);
    CHECK(coins.pool.items == NULL);

    for (size_t k = 0; k < sizeof every / sizeof every[0]; ++k) cok_adventure_close(every[k]);
}

/* 75cc:0736's loop A with no load asked (DS:b7a7 clear), as 0593 leaves the party: a standing
 * member (a) keeps b7a7 clear, so the loop walks the party. Status 4 is revived (7bb) when its hit
 * points (+0x198) are above 0, or with var 0x1d (777: hit points 1), else it stays 4; status 5
 * is revived with var 0x1d and becomes 4 without it (76c, 78e). Fled (DS:c7de, from a status 3
 * member) removes each member not status 3 (7d8, with the count, var 0x20, one less). */
static void test_dark_queen_rest(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's rest after combat not run");
        return;
    }
    const cok_character_layout *L = cok_rules_dark_queen.character;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    cok_character *a = dq_member(&dqk, 0, 1), *b = dq_member(&dqk, 4, 0);
    cok_dark_queen_end_state st;
    cok_field_set8(L, b->record, COK_FIELD_HIT_POINTS, 0);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 4);
    cok_ecl_store(&dqk.vm, 0x20, 2);

    /* Status 4, no hit points, var 0x1d clear: stays 4 (7a4, 7ca). */
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(cok_field_u8(L, b->record, COK_FIELD_STATUS) == 4);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_ACTIVE) == 0);

    /* Status 4 with var 0x1d set: revived (777): status 0, active, hit points 1. */
    cok_ecl_store(&dqk.vm, 0x1d, 1);
    dq_combat(&dqk);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_STATUS) == 0);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_ACTIVE) == 1);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_HIT_POINTS) == 1);
    cok_ecl_store(&dqk.vm, 0x1d, 0);

    /* Status 4 with hit points: revived (7bb), the hit points kept. */
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 4);
    cok_field_set8(L, b->record, COK_FIELD_ACTIVE, 0);
    cok_field_set8(L, b->record, COK_FIELD_HIT_POINTS, 3);
    dq_combat(&dqk);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_STATUS) == 0);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_HIT_POINTS) == 3);

    /* Status 5 without var 0x1d becomes 4 (78e); with it, revived with 1 hit point. */
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 5);
    dq_combat(&dqk);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_STATUS) == 4);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 5);
    cok_ecl_store(&dqk.vm, 0x1d, 1);
    dq_combat(&dqk);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_STATUS) == 0);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_HIT_POINTS) == 1);
    cok_ecl_store(&dqk.vm, 0x1d, 0);

    /* Fled: a status 2 member (c, active) is removed, the count one less (7d8, 1e31 with 0, 1);
     * the status 3 member is revived (7bb). b7a7 stays clear: the status 3 member is not hostile
     * with status 3 (loop 3). */
    cok_character *c = dq_member(&dqk, 2, 1);
    cok_field_set8(L, a->record, COK_FIELD_STATUS, 3);
    cok_field_set8(L, b->record, COK_FIELD_STATUS, 3);
    cok_ecl_store(&dqk.vm, 0x20, 3);
    size_t before = dqk.party.count;
    dqk.vm.character = c->record; /* the selection on the member removed (7d8) */
    dq_combat(&dqk);
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(st.fled && !st.b7a7 && dqk.party.count == before - 1);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_PARTY_SIZE) == 2);
    CHECK(cok_field_u8(L, a->record, COK_FIELD_STATUS) == 0);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_STATUS) == 0);
    /* 1f07-1f31 leave the selection on b (the one before c), but 11b0 then sets the first
     * record (1287: a), which the stop shows. */
    CHECK(dqk.vm.character == a->record);
    cok_party_free(&dqk.party);

    /* m1: a lost party (818-86e) leaves var 0x20 at 0 whatever it was: 9 less two removals
     * would be 7. Both are out (status 2, inactive) and b7a7 stays set. */
    a = dq_member(&dqk, 2, 0);
    b = dq_member(&dqk, 2, 0);
    cok_ecl_store(&dqk.vm, 0x20, 9);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the continue prompt's key */
    CHECK(dqk.party.count == 0 && cok_ecl_var(&dqk.vm, COK_VAR_PARTY_SIZE) == 0);
    CHECK(cok_dark_queen_end_state_of(&dqk).b7a7);
    cok_party_free(&dqk.party);

    /* m2: a standing member (a) clears the fled flag (694-6aa), so the status 3 member (b)
     * is revived (7bb) and kept, not removed. */
    a = dq_member(&dqk, 0, 1);
    b = dq_member(&dqk, 3, 0);
    cok_ecl_store(&dqk.vm, 0x20, 2);
    dq_combat(&dqk);
    st = cok_dark_queen_end_state_of(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the results' key, no more */
    CHECK(!st.fled && !st.b7a7 && dqk.party.count == 2);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_STATUS) == 0);
    CHECK(cok_field_u8(L, b->record, COK_FIELD_ACTIVE) == 1);
    cok_party_free(&dqk.party);

    /* m4: loop A stops at a record past the party (804-810): a status 4 member with hit points
     * after it is not revived. The record past the party is removed by 11b0 (part 19d), so the
     * member after it, c, shows the stop: c keeps status 4. */
    a = dq_member(&dqk, 0, 1);
    dq_past_party(&dqk, 4);
    cok_character *k4 = dq_member(&dqk, 4, 1);
    cok_field_set8(L, k4->record, COK_FIELD_HIT_POINTS, 5);
    dq_combat_leave(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 4);
    CHECK(dqk.party.count == 2 && dqk.party.members[1] == k4);
    CHECK(cok_field_u8(L, k4->record, COK_FIELD_STATUS) == 4);
    cok_party_free(&dqk.party);

    /* A knight of no order (+0xa6 1, order +0x19a 0): the award's may-train (177e:1fe5) stops
     * at its row, 177e:1f39, cited (part 19c). */
    a = dq_member(&dqk, 0, 1);
    a->record[0xa6] = 1; /* the knight's level (LEVEL(c, 1)) */
    cok_field_set8(L, a->record, COK_FIELD_KNIGHT_ORDER, 0);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED &&
          strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 177e:1f39") != NULL);
    cok_party_free(&dqk.party);
}

/* The party at the battle's set-up (the battlefield hook, 3cb2:1c58, after the appends): the
 * battle's end (5045:0001, then 135c's 11b0) removes the monsters, so the checks read this copy. */
static struct {
    size_t count;
    char names[16][16];
    uint8_t icon[16], hp[16], hp89[16], hostile[16], status[16], npc[16];
    unsigned monsters, icon_slot, undead;
    bool cpic8; /* slot 8 holds CPIC 64 and 64 + 0x80 */
} at_setup;

static bool cpic_holds(const cok_adventure *adv, unsigned slot, unsigned image, unsigned id);

static void battlefield_copy(cok_adventure *adv, void *context)
{
    (void)context;
    const cok_character_layout *L = cok_rules_dark_queen.character;
    at_setup.count = adv->party.count;
    at_setup.monsters = adv->monsters;
    at_setup.icon_slot = adv->icon_slot;
    at_setup.undead = adv->undead;
    at_setup.cpic8 = cpic_holds(adv, 8, 0, 64) && cpic_holds(adv, 8, 1, 64 + 0x80);
    for (size_t i = 0; i < adv->party.count && i < 16; ++i) {
        const uint8_t *r = adv->party.members[i]->record;
        cok_field_name(L, r, at_setup.names[i]);
        at_setup.icon[i] = cok_field_u8(L, r, COK_FIELD_ICON);
        at_setup.hp[i] = r[0x198];
        at_setup.hp89[i] = r[0x89];
        at_setup.hostile[i] = r[0x66];
        at_setup.status[i] = r[0x64];
        at_setup.npc[i] = r[0x9b];
    }
}

/* LOAD MONSTER 64 x2 and 49 x1 register, COMBAT appends the SHARKMEN and the MINOTAUR, then
 * 170e's ten GORGONs (the icon in slot 10). The battle is over before any round (no party), so
 * 5045:0001 runs and 135c's treasure branch (11b0 removes the monsters) finishes it; the checks
 * read the party at the set-up. CLEARMONSTERS resets the slots and the undead count, as
 * 75cc:151b does (DS:ad6b to 8, DS:c2ed to 0). */
static bool r9b_bits(size_t i)
{
    return (at_setup.npc[i] & 0x32) == 0x32;
}

static void test_dark_queen_appends(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's appends not run");
        return;
    }
    /* Registered only: no record yet, the flag set, the slots taken. */
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.party.count == 0);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 2, 0, 64);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 49, 0, 1, 0, 49);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.party.count == 0 && dqk.monsters_loaded);
    CHECK(dqk.icon_slot == 10 && dqk.monsters == 0);
    CHECK(dqk.monster_slots[8].id == 64 && dqk.monster_slots[8].count == 2 &&
          dqk.monster_slots[8].icon == 64);
    CHECK(dqk.monster_slots[9].id == 49 && dqk.monster_slots[9].count == 1);

    /* COMBAT appends them (the setup's event 8 runs the SHARKMEN's 0x64 and the GORGONS' 0xc3,
     * 78e4:14d1 and 4949), the battle's end runs, and the run goes on to its end. */
    memset(&at_setup, 0, sizeof at_setup);
    dqk.hooks.battlefield = battlefield_copy;
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.party.count == 0 && !dqk.monsters_loaded);
    CHECK(at_setup.count == 13 && at_setup.monsters == 13 && at_setup.icon_slot == 11);
    CHECK(at_setup.undead == 0);
    static const char *names[] = {"SHARKMAN", "SHARKMAN", "MINOTAUR"};
    for (size_t i = 0; i < 13; ++i) {
        if (i < 3) CHECK(strcmp(at_setup.names[i], names[i]) == 0);
        else CHECK(strcmp(at_setup.names[i], "GORGON") == 0);
        /* The icon slots: 8 for the SHARKMEN, 9 for the MINOTAUR, 10 for 170e's. */
        CHECK(at_setup.icon[i] == (i < 2 ? 8 : i == 2 ? 9 : 10));
        if (i >= 3) { /* 170e's: status 9, the NPC bits, hostile 0 (var 0x3a is 0). */
            CHECK(at_setup.status[i] == 9 && at_setup.hostile[i] == 0 && r9b_bits(i));
        }
    }

    /* CLEARMONSTERS (75cc:151b): the flag and the count, the undead count, and the next icon slot
     * back to 8 (the block's start, 6c70:0001, does the same). The next COMBAT with none loaded
     * and no vault or temple wanted: the treasure runs and the run goes on (5f07:081b's last
     * branch, 75cc:135c). */
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    CHECK(dqk.vm.status == COK_ECL_OK && !dqk.monsters_loaded && dqk.monsters == 0);
    CHECK(dqk.icon_slot == 8 && dqk.undead == 0 && dqk.party.count == 0);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 1); /* the lost party's continue prompt */

    /* A new block's LOAD MONSTER takes slot 8 again, one SHARKMAN and 170e's ten GORGONs: the
     * difficulty scales the hostile SHARKMAN's hit points by (d + 1) / 4 (6c70:29fc), at least
     * 1; var 0x27 is the difficulty. */
    cok_character base;
    CHECK(cok_monster_read(&dqk, 64, &base));
    uint8_t hp = base.record[0x198], hostile = base.record[0x66];
    cok_character_free(&base);
    CHECK(hostile == 1 && hp > 1);
    cok_ecl_set_var(&dqk.vm, COK_VAR_DIFFICULTY, 1);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 1, 0, 64);
    memset(&at_setup, 0, sizeof at_setup);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && at_setup.count == 11);
    CHECK(at_setup.hp[0] == (uint8_t)(2 * hp / 4) && at_setup.hp89[0] == (uint8_t)(2 * hp / 4));
    cok_ecl_set_var(&dqk.vm, COK_VAR_DIFFICULTY, 3);
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 1, 0, 64);
    memset(&at_setup, 0, sizeof at_setup);
    dq_combat(&dqk);
    CHECK(at_setup.hp[0] == hp);
    dqk.hooks.battlefield = NULL;
    cok_adventure_close(&dqk);
}

/* Whether queen_icon's slot and image hold record id of the CPIC file, byte for byte. */
static bool cpic_holds(const cok_adventure *adv, unsigned slot, unsigned image, unsigned id)
{
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library lib;
    const uint8_t *want = NULL, *got = NULL;
    size_t want_size = 0, got_size = 0;
    bool ok = hlib_read_file("DQKAssets/DISK2/CPIC.TLB", &bytes, &n) == HLIB_OK &&
              hlib_parse(bytes, n, &lib) == HLIB_OK &&
              hlib_record(&lib, id, &want, &want_size) == HLIB_OK &&
              queen_icon(adv, slot, image, &got, &got_size) && got_size == want_size &&
              memcmp(got, want, want_size) == 0;
    free(bytes);
    return ok;
}

/* The block of the load check (dqk_fight.py's flow clear): LOAD MONSTER 49 x1,
 * CLEARMONSTERS, LOAD MONSTER 64 x1, COMBAT. The first monster's slot and record
 * are forgotten, so the SHARKMAN takes slot 8, and 170e's icon takes slot 9 (the
 * original's 17 records, c6f1 11, ad6b 10). */
static void test_dark_queen_clear_between(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) return;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 49, 0, 1, 0, 49);
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.icon_slot == 8 && !dqk.monsters_loaded);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 1, 0, 64);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.icon_slot == 9);
    CHECK(dqk.monster_slots[8].id == 64 && dqk.monster_slots[8].count == 1);
    memset(&at_setup, 0, sizeof at_setup);
    dqk.hooks.battlefield = battlefield_copy;
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(at_setup.count == 11 && at_setup.monsters == 11 && at_setup.icon_slot == 10);
    /* LOAD MONSTER 64 loaded its icon into slot 8 (29fc:2b0b): CPIC 64 and 64 + 0x80, at the
     * set-up (11b0 removes the monsters' icons after it). */
    CHECK(at_setup.cpic8);
    CHECK(strcmp(at_setup.names[0], "SHARKMAN") == 0);
    cok_adventure_close(&dqk);
}

/* Six LOAD MONSTERs (slots 8 to 13), then COMBAT: 170e's icon takes slot 14, so
 * the icon slots are 15 (the original's 22 records, c6f1 16, ad6b 15). The
 * monsters' icons load as they register (204c:00df, CPIC): no icon slot error. The ZOMBIE
 * and the WRAITH are undead, one count each (29fc's 2ad6, once per slot). */
static void test_dark_queen_six_loads(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) return;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    static const uint8_t ids[6] = {64, 49, 41, 37, 36, 64};
    for (size_t i = 0; i < 6; ++i)
        DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, ids[i], 0, 1, 0, ids[i]);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.icon_slot == 14);
    memset(&at_setup, 0, sizeof at_setup);
    dqk.hooks.battlefield = battlefield_copy;
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(at_setup.count == 16 && at_setup.monsters == 16 && at_setup.icon_slot == 15);
    CHECK(at_setup.undead == 2);
    cok_adventure_close(&dqk);
}

/* One ZOMBIE (undead) fought, then CLEARMONSTERS: the undead count goes to 0
 * (75cc:151b's movb 0 to DS:c2ed); the records stay. */
static void test_dark_queen_undead_clear(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) return;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 37, 0, 1, 0, 37);
    memset(&at_setup, 0, sizeof at_setup);
    dqk.hooks.battlefield = battlefield_copy;
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && at_setup.undead == 1);
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.undead == 0 && dqk.icon_slot == 8);
    CHECK(dqk.party.count == 0); /* the battle's end removed the ZOMBIE and 170e's ten GORGONs */
    cok_adventure_close(&dqk);
}
/* One battle outside 3D from a fresh adventure's block: CLEARMONSTERS, LOAD MONSTER 64, with
 * var 0x1a2 (the Abyss's state, the open ground's flags index) TERRAIN, then COMBAT. Returns the
 * status. */
static cok_ecl_status open_ground_fight(cok_adventure *adv, uint16_t terrain)
{
    DQK_RUN(adv, DQK_CLEARMONSTERS);
    DQK_RUN(adv, DQK_LOAD_MONSTER, 0, 64, 0, 1, 0, 64);
    cok_ecl_set_var(&adv->vm, COK_VAR_ABYSS, terrain);
    DQK_RUN(adv, DQK_COMBAT);
    return adv->vm.status;
}

/* The open ground's flags index (combat.c build(): var 0x1a2 indexes DS:5d7b, 32 entries,
 * 59c7:0783).
 * Var 0 (flags 0x00) and 1 (0x18, a value blocks 64 and 66 store) give different cells from the
 * same seed; 32 is past the table, so the battle stops with COK_ECL_UNDEFINED. A build() that used
 * type 15 for every index would fail the first check (the same cells) and the last (no stop). */
static void test_dark_queen_open_ground_flags(void)
{
    static cok_adventure a, b, c;
    if (!dq_open(&a)) return;
    CHECK(dq_open(&b) && dq_open(&c));
    CHECK(open_ground_fight(&a, 0) == COK_ECL_OK);
    CHECK(open_ground_fight(&b, 1) == COK_ECL_OK);
    CHECK(memcmp(a.combat.cells, b.combat.cells, sizeof a.combat.cells) != 0);
    CHECK(open_ground_fight(&c, 32) == COK_ECL_UNDEFINED);
    CHECK(strstr(cok_adventure_why(&c, c.vm.status), "var 0x1a2 is 32, past its flags") != NULL);
    cok_adventure_close(&a);
    cok_adventure_close(&b);
    cok_adventure_close(&c);
}

/* The encounter (dqk_fight.py's flow encounter): SETUP MONSTERS (sprite, distance
 * wanted, close-up, ambush) puts the distance wanted in var 0x38 and the distance in
 * 0x37, the open squares ahead (6c70:01c3) or the wanted, whichever is less; a wanted
 * 16 is 2 in both (2db1's 2e1c); with no 3D area the open squares set 0x38 to 2
 * (01d9-01e4). APPROACH steps one square nearer, not past 0. COMBAT lowers 0x37 to the
 * open squares before its text (5f07:088f-08ba). The sprite and close-up flags are the
 * drawing's (dark_queen.c's encounter). Skipped without DQKAssets. */
#define DQK_SETUP 0x0c
#define DQK_APPROACH 0x0d
static void test_dark_queen_encounter(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) return;
    static uint8_t map[0x402];
    memset(map, 0, sizeof map);
    CHECK(cok_view_set_map(&dqk.view, map, sizeof map));
    dqk.view.wrap = true;
    dqk.vm.map_x = 7;
    dqk.vm.map_y = 7;
    dqk.vm.direction = 0;
    cok_ecl_set_var(&dqk.vm, COK_VAR_3D, 1);
    cok_ecl_set_var(&dqk.vm, COK_VAR_DISTANCE, 9);
    /* Sprite 82, a distance wanted of 5 (the open squares are 2), no close-up. */
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 5, 0, 0xff, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.sprite_id == 82 && dqk.closeup_id == 0xff);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE_WANTED) == 5);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE) == 2);
    /* A wanted 16 is 2 squares, in both vars. */
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 16, 0, 0xff, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE_WANTED) == 2 &&
          cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE) == 2);
    /* No 3D area: the open squares set the distance wanted to 2, as they set 0x38 in
     * the original, whatever the wanted was. */
    cok_ecl_set_var(&dqk.vm, COK_VAR_3D, 0);
    cok_ecl_set_var(&dqk.vm, COK_VAR_DISTANCE, 9);
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 9, 0, 0xff, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE_WANTED) == 2 &&
          cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE) == 2);
    cok_ecl_set_var(&dqk.vm, COK_VAR_3D, 1);
    /* APPROACH steps one square at a time, and stops at 0. */
    DQK_RUN(&dqk, DQK_APPROACH);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE) == 1);
    DQK_RUN(&dqk, DQK_APPROACH);
    DQK_RUN(&dqk, DQK_APPROACH);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE) == 0);
    /* In mode 4 the sprite is loaded and drawn at distance 0, with its close-up (PICTURE
     * 14, PICA's record 14), which comes at 0 only. The block's end (EXIT, which dq_run
     * adds) clears the sprite's and close-up's flags as the original's EXIT does, so
     * the sprite shown (DS:884d, which EXIT keeps) is what is checked. */
    dqk.vm.mode = 4;
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 0, 0, 14, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.sprite_shown && dqk.closeup_id == 14);
    CHECK(dqk.error[0] == '\0');
    /* COMBAT lowers the distance to the open squares (2) before its text, and the
     * battle's records are made (59c7:0f7b), and the battle stops at its placement (1864). */
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 1, 0, 64);
    cok_ecl_set_var(&dqk.vm, COK_VAR_DISTANCE, 5);
    dq_combat(&dqk);
    /* The fixture has no party, so the view's origin is the sharkman's (x - 3, y - 3) and y 2
     * gives -1: the arena's redraw draws the view past the map's top edge, which the port
     * refuses (queen_arena.c), so the battle stops there (UNDEFINED, not 5045:078b). */
    CHECK(dqk.vm.status == COK_ECL_UNDEFINED);
    CHECK(strstr(dqk.error, "past the map") != NULL);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_DISTANCE) == 2);
    cok_adventure_close(&dqk);
}
/* PICTURE 0xff (5f07:0511-054b) draws the view again where a sprite or a picture covers
 * it (DS:b419, ae64), then forgets the sprite and close-up (DS:c6f6), so a SETUP after it
 * loads its own sprite (SPRIT 5 is missing: the load fails). PICTURE n sets the close-up
 * (DS:c6f7, 5f07:04ae), so a SETUP after it draws no sprite (6c70:0255). Each block ends
 * with EXIT, which forgets DS:c6f6 and ae64 but keeps DS:b419, so the checks read what
 * the block left. */
#define DQK_PICTURE 0x0e
static void test_dark_queen_picture_flags(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) return;
    static uint8_t map[0x402];
    memset(map, 0, sizeof map);
    CHECK(cok_view_set_map(&dqk.view, map, sizeof map));
    dqk.view.wrap = true;
    dqk.vm.map_x = 7;
    dqk.vm.map_y = 7;
    dqk.vm.direction = 0;
    cok_ecl_set_var(&dqk.vm, COK_VAR_3D, 1);
    dqk.vm.mode = 4;
    /* A sprite, PICTURE 0xff (the sprite forgotten, the view drawn again), then a
     * SETUP with a sprite that is not in SPRIT.TLB: it is loaded (and fails), not skipped. */
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 16, 0, 0xff, 0, 0,
            DQK_PICTURE, 0, 0xff, DQK_SETUP, 0, 5, 0, 16, 0, 0xff, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_LOAD_FAILED);
    /* The same sprite forgotten by PICTURE 0xff, shown again by a second sprite; the last
     * PICTURE 0xff takes it off the view (DS:b419 clear). */
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 16, 0, 0xff, 0, 0,
            DQK_PICTURE, 0, 0xff, DQK_SETUP, 0, 2, 0, 16, 0, 0xff, 0, 0,
            DQK_PICTURE, 0, 0xff);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.sprite_id == 2 && !dqk.sprite_shown);
    /* PICTURE 21 (a small picture, one frame) sets the close-up flag: the SETUP after it
     * draws no sprite (the block's EXIT forgets the picture and close-up flags). */
    DQK_RUN(&dqk, DQK_PICTURE, 0, 21, DQK_SETUP, 0, 82, 0, 16, 0, 0xff, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_OK && !dqk.sprite_shown);
    /* A close-up at distance 0 (the sprite and its close-up shown), then PICTURE 0xff
     * forgets the close-up: the next SETUP loads its sprite (missing: the load fails). */
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 0, 0, 21, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.sprite_shown && dqk.closeup_id == 21);
    DQK_RUN(&dqk, DQK_SETUP, 0, 82, 0, 0, 0, 21, 0, 0,
            DQK_PICTURE, 0, 0xff, DQK_SETUP, 0, 5, 0, 16, 0, 0xff, 0, 0);
    CHECK(dqk.vm.status == COK_ECL_LOAD_FAILED);
    cok_adventure_close(&dqk);
}
/* The scripted keys for a temple test: s is read from the start. */
static void keys_to(const char *keys)
{
    s.keys = keys;
    s.at = 0;
    s.length = strlen(keys);
    s.log[0] = '\0';
}

/* The temple's keys: the bar's Heal, the list's Down (an extended key: 1 then 'P') down times,
 * then end (Enter, Escape, No ...), in buf. */
static void temple_keys(char *buf, size_t size, unsigned down, const char *end)
{
    size_t at = 0;
    buf[at++] = 'h';
    for (unsigned d = 0; d < down && at + 3 < size; ++d) {
        buf[at++] = 1;
        buf[at++] = 'P';
    }
    snprintf(buf + at, size - at, "%s", end);
}

/* dq_open with the scripted keys s: the adventure opened for a test that types the temple's keys. */
static bool dq_open_keys(cok_adventure *adv)
{
    static cok_keyboard keys;
    static cok_adventure_hooks hooks;
    keys = (cok_keyboard){scripted, &s, NULL, NULL};
    hooks = (cok_adventure_hooks){.log = log_line, .context = &s};
    FILE *f = fopen("DQKAssets/DISK2/MONCHA.GLB", "rb");
    if (f == NULL) return false;
    fclose(f);
    CHECK(cok_adventure_open(adv, &cok_rules_dark_queen, "DQKAssets", &keys, &hooks));
    adv->vm.file = 1;
    adv->vm.status = COK_ECL_OK;
    adv->wall_ids[0] = 4;
    return true;
}

/* The temple's Heal (dqk 7825:0615): Heal picks the list's current cure, and each cure but Stone
 * to Flesh stops, cited with its routine (0045, 00b8, 0193, 0394, 02c0, 0479, 059e). Stone to Flesh
 * (0524): a member not stoned is asked and, for No, nothing changes and the list comes back; a
 * stoned one is cured (status 0, active, 1 hit point); with a readied item of type 0x8c it stays
 * stoned. */
/* Whether an 8 by 8 cell of the adventure's screen (pixels at x, y) holds colour c. */
static bool cell_holds(const cok_adventure *adv, int x, int y, uint8_t c)
{
    const cok_picture *pic = &adv->screen;
    size_t stride = (size_t)pic->units * 8;
    for (int dy = 0; dy < 8; ++dy)
        for (int dx = 0; dx < 8; ++dx)
            if (pic->pixels[((size_t)y + (size_t)dy) * stride + (size_t)x + (size_t)dx] == c)
                return true;
    return false;
}

static void test_dark_queen_heal(void)
{
    static cok_adventure dqk;
    static const struct {
        unsigned down;
        const char *cite;
    } stops[] = {{0, "dqk 7825:0045"}, {1, "dqk 7825:00b8"}, {2, "dqk 7825:0193"},
                 {6, "dqk 7825:0394"}, {7, "dqk 7825:02c0"}, {8, "dqk 7825:0479"},
                 {10, "dqk 7825:059e"}};
    char keys[32];
    static const char *const escape = "\x1b\x1b\x1b\x1b";
    if (!dq_open_keys(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's Heal not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    const cok_character_layout *L = cok_game_rules(&dqk)->character;
    cok_character *m = calloc(1, sizeof *m);
    CHECK(m != NULL);
    cok_field_set8(L, m->record, COK_FIELD_MAX_HIT_POINTS, 9);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 3);
    cok_field_set8(L, m->record, COK_FIELD_ACTIVE, 1);
    CHECK(cok_party_add(&dqk.party, &cok_rules_dark_queen, m));
    dqk.vm.character = m->record; /* the member the temple's Heal works on (the selection) */
    for (size_t n = 0; n < sizeof stops / sizeof *stops; ++n) {
        temple_keys(keys, sizeof keys, stops[n].down, "\r");
        keys_to(keys);
        cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
        dq_combat(&dqk);
        CHECK(dqk.vm.status == COK_ECL_UNPORTED);
        CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), stops[n].cite) != NULL);
        /* The wounded member's hit points (3 of 9) are black on the window's grey (0x17, bg 8),
         * as 7825:0737-074b's colour byte 0x00 is drawn: the digit's cell holds the grey. */
        if (n == 0) CHECK(cell_holds(&dqk, 8 * 0x12, 8 * 0x13, 0x17));
    }

    /* Stone to Flesh, not stoned: the question, No, then Escape leaves the list and the temple. */
    temple_keys(keys, sizeof keys, 9, "\r");
    snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "n%s", escape);
    keys_to(keys);
    cok_field_set8(L, m->record, COK_FIELD_STATUS, 0);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 3);
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.mode == 4);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_HIT_POINTS) == 3);

    /* Stoned: cured, and the list comes back (Escape ends it, then the temple). */
    temple_keys(keys, sizeof keys, 9, "\r");
    snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%s", escape);
    keys_to(keys);
    cok_field_set8(L, m->record, COK_FIELD_STATUS, 7);
    m->record[0x65] = 1; /* the status word's high byte: 7825:0588 writes it too */
    cok_field_set8(L, m->record, COK_FIELD_ACTIVE, 0);
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_STATUS) == 0);
    CHECK(m->record[0x65] == 0);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_ACTIVE) == 1);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_HIT_POINTS) == 1);

    /* Stoned with a readied item of type 0x8c (+0x37, readied +0x32): "spell dosn't work!",
     * and the member stays stoned with its hit points. */
    static uint8_t item[1][COK_ITEM_SIZE];
    uint8_t (*saved)[COK_ITEM_SIZE] = m->items;
    size_t saved_count = m->item_count;
    memset(item, 0, sizeof item);
    item[0][0x37] = 0x8c;
    item[0][0x32] = 1;
    m->items = item;
    m->item_count = 1;
    temple_keys(keys, sizeof keys, 9, "\r");
    snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%s", escape);
    keys_to(keys);
    cok_field_set8(L, m->record, COK_FIELD_STATUS, 7);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 3);
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_STATUS) == 7);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_HIT_POINTS) == 3);

    /* The same item not readied (+0x32 0): it does not stop the cure. */
    item[0][0x32] = 0;
    keys_to(keys);
    cok_field_set8(L, m->record, COK_FIELD_STATUS, 7);
    cok_field_set8(L, m->record, COK_FIELD_HIT_POINTS, 3);
    cok_field_set8(L, m->record, COK_FIELD_ACTIVE, 0);
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    dq_combat(&dqk);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_field_u8(L, m->record, COK_FIELD_STATUS) == 0);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_HIT_POINTS) == 1);
    m->items = saved;
    m->item_count = saved_count;
    cok_adventure_close(&dqk);
}

/* The placement's jump table (dqk 59c7:1abf): with SETUP MONSTERS' ambush DS:c2e7 at 0x20,
 * each hostile record of the first six takes its case's position (1abf's cases 0-5, 1acb to
 * 1b8e), and the battle, with no party, is over before any round (5045:0001). Six
 * SHARKMEN are hostile and come first in the list; 170e's GORGONs (hostile 0) come after and
 * are placed by the search. */
/* The jump table's checks at the battle's set-up (the battlefield hook, after 1abf's placement). */
static void jump_checks(cok_adventure *dqk, void *context)
{
    (void)context;
    static const int8_t at[6][2] = {{0x1e, 0x10}, {0x1d, 0x10}, {0x1e, 0x0e},
                                    {0x1f, 0x0e}, {0x21, 0x0f}, {0x20, 0x0e}};
    for (unsigned n = 1; n <= 6; ++n) {
        CHECK(dqk->combat.combatant[n].x == at[n - 1][0]);
        CHECK(dqk->combat.combatant[n].y == at[n - 1][1]);
    }
    /* 51c2:0920's 0x20 quirk (dqk's sides_ambush_0x20) takes one from side 1 on every count
     * with the ambush byte at 0x20, and no other: Champions' count ignores the byte. */
    uint8_t plain[2];
    CHECK(cok_combat_count_sides(&dqk->combat, &dqk->party, &cok_rules_dark_queen, 0));
    plain[0] = dqk->combat.sides[0];
    plain[1] = dqk->combat.sides[1];
    CHECK(plain[1] >= 1);
    CHECK(cok_combat_count_sides(&dqk->combat, &dqk->party, &cok_rules_dark_queen, 0x20));
    CHECK(dqk->combat.sides[0] == plain[0] && dqk->combat.sides[1] == plain[1] - 1);
    uint8_t champions[2];
    CHECK(cok_combat_count_sides(&game.combat, &game.party, &cok_rules_champions, 0));
    champions[0] = game.combat.sides[0];
    champions[1] = game.combat.sides[1];
    CHECK(cok_combat_count_sides(&game.combat, &game.party, &cok_rules_champions, 0x20));
    CHECK(game.combat.sides[0] == champions[0] && game.combat.sides[1] == champions[1]);
}

static void test_dark_queen_jump_table(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) return;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 6, 0, 64);
    dqk.ambush = 0x20;
    dqk.hooks.battlefield = jump_checks;
    CHECK(dq_combat(&dqk) == COK_ECL_OK); /* the battle's end runs after the set-up's checks */
}

/* The GORGONS' cells at the battle's set-up (the battlefield hook): each consecutive pair differs. */
static void gorgon_checks(cok_adventure *adv, void *context)
{
    (void)context;
    const cok_character_layout *L = cok_rules_dark_queen.character;
    /* The party's records are the combatants 1 on (the placement's d, from 1), none removed. */
    unsigned gorgons = 0, previous = 0;
    for (size_t i = 0; i < adv->party.count; ++i) {
        char name[16];
        CHECK(cok_field_name(L, adv->party.members[i]->record, name) > 0);
        if (strcmp(name, "GORGON") != 0) continue;
        ++gorgons;
        unsigned n = (unsigned)i + 1;
        if (previous != 0) {
            const cok_combatant *a = &adv->combat.combatant[previous], *b = &adv->combat.combatant[n];
            CHECK(a->x != b->x || a->y != b->y);
        }
        previous = n;
    }
    CHECK(gorgons == 10);
}

/* 170e's ten GORGONs (status 9, hostile 0, so all on the party's facing) are placed one after
 * another with the ambush directions 0xf: each lays out its formations again only when its facing
 * changes (16b3), so the cells a GORGON takes are not free for the next (59c7:16b3, 16e1). The
 * consecutive GORGONs' cells differ. */
static void test_dark_queen_ambush_gorgons(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) return;
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 6, 0, 64);
    dqk.ambush = 0x0f;
    dqk.hooks.battlefield = gorgon_checks;
    CHECK(dq_combat(&dqk) == COK_ECL_OK);
    cok_adventure_close(&dqk);
}

/* How many times needle is in hay. */
static int dq_count(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += strlen(needle)) ++n;
    return n;
}

/* The treasure menu (75cc:0e93, treasure_menu) from COMBAT: the results' Return, then the keys
 * at the menu. The log is dq_quiet.log. */
static cok_ecl_status dq_menu(cok_adventure *adv, const char *keys)
{
    static char menu_keys[32];
    snprintf(menu_keys, sizeof menu_keys, "\r%s", keys);
    dq_keys(menu_keys);
    dq_quiet.log[0] = '\0';
    return DQK_RUN(adv, DQK_COMBAT);
}

/* The treasure menu's items by the pool (View; Take with money or items; Pool; Share with
 * money; Detect with a spell found; Exit), Exit's question with Yes and No, Escape, the stop of
 * Take's items (part 21) and of Detect (part 28), and the member keys (Down and Up wrap,
 * 5e22:067c). Take's money, Pool and Share are test_dark_queen_treasure_take and its neighbours. */
static void test_dark_queen_treasure_menu(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's treasure menu not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    cok_character *a = dq_member(&dqk, 0, 1);
    dq_member(&dqk, 0, 1);
    const char *money_menu = "menu: ~View ~Take ~Pool ~Share ~Exit;";
    const char *items_menu = "menu: ~View ~Take ~Pool ~Exit;";

    /* Money: Exit asks, No leaves, and the pool is freed (63ee:0002). */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dq_menu(&dqk, "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 3);
    CHECK(dq_count(dq_quiet.log, money_menu) == 1 && dq_count(dq_quiet.log, "choice: 5;") == 1);
    CHECK(dqk.pool.coins[4] == 0);

    /* Escape is Exit. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dq_menu(&dqk, "\x1b" "n");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 3);
    CHECK(dq_count(dq_quiet.log, "choice: 5;") == 1);

    /* Yes clears the question and the menu comes again: Exit, Yes, Exit, No. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dq_menu(&dqk, "eyen");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 5);
    CHECK(dq_count(dq_quiet.log, money_menu) == 2 && dq_count(dq_quiet.log, "choice: 5;") == 2);
    CHECK(dqk.pool.coins[4] == 0);

    /* Items with no money: no Share; Exit asks as well. */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dq_menu(&dqk, "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_quiet.at == 3);
    CHECK(dq_count(dq_quiet.log, items_menu) == 1);

    /* Take with items and no money is the list (75cc:0bbe, part 21b): Escape leaves it, then
     * Exit asks (the item is still there). */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dq_menu(&dqk, "t\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_count(dq_quiet.log, "item: ") == 1);

    /* Detect is listed with an item and a spell found: 0x3c at +0xce+i with +0x18b set. Its stop
     * is cited (part 28). Without +0x18b, or with the byte 0x67, it is not listed. */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dqk.vm.character = a->record;
    a->record[0xce + 0x10] = 0x3c;
    a->record[0x18b] = 1;
    dq_menu(&dqk, "d");
    CHECK(dqk.vm.status == COK_ECL_UNPORTED &&
          strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 849a:145b (Detect's cast, "
                                                          "part 28)") != NULL);
    a->record[0x18b] = 0;
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dq_menu(&dqk, "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_count(dq_quiet.log, items_menu) == 1);
    a->record[0x18b] = 1;
    a->record[0xce + 0x10] = 0x67;
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dq_menu(&dqk, "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_count(dq_quiet.log, items_menu) == 1);
    a->record[0xce + 0x10] = 0;
    a->record[0x18b] = 0;

    /* The member keys: Up from the first member picks the last, Down from the last the first. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    cok_character *first = dqk.party.members[0];
    cok_character *last = dqk.party.members[dqk.party.count - 1];
    dqk.vm.character = first->record;
    dq_menu(&dqk, "\x01H" "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.character == last->record);
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dq_menu(&dqk, "\x01P\x01P" "en"); /* COMBAT leaves the first member selected: Down, Down wraps */
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.party.count == 2 && dqk.vm.character == first->record);

    /* End and Home do nothing (75cc:1034-103c): the first member stays selected. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dqk.vm.character = first->record;
    dq_menu(&dqk, "\x01O" "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.character == first->record);
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dqk.vm.character = first->record;
    dq_menu(&dqk, "\x01G" "en");
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.character == first->record);
}

/* The treasure menu's Take with money (part 20b, 75cc:0d01 and 74b9:0683): the coin list (Jewelry
 * first), Return takes the first row, an amount past the count takes the count, and the list
 * ends on Escape. Then the overload: 1600 more than a weight of 64000 is 65600, past 0xffff (the
 * 16-bit sum would be 64 and let it through), so "Overloaded" and nothing is taken. Take's bar
 * with money and items: Items stops (part 21), Money takes, Escape leaves it. */
static void test_dark_queen_treasure_take(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's treasure take not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    const cok_character_layout *L = cok_rules_dark_queen.character;
    cok_character *a = dq_member(&dqk, 0, 1);
    dq_member(&dqk, 0, 1);

    /* Jewelry by Return; the amount 5 is past the count 1 and takes 1. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dqk.pool.coins[6] = 1;
    dqk.vm.character = a->record;
    dq_menu(&dqk, "t\r" "5\r" "\x1b" "en");
    /* No is the claim's answer: the pool is freed on leaving (treasure_end), so its counts are
     * not looked at after the menu; the member's jewelry is. */
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(cok_field_u16_at(L, a->record, COK_FIELD_COINS, 6) == 1);

    /* Steel (the last row, Down twice) taken whole leaves the cursor past the list's end:
     * 1b05:0055 keeps it on the last row (Gems), so Return asks for Gems. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dqk.pool.coins[5] = 3;
    dqk.pool.coins[6] = 1;
    dqk.vm.character = a->record;
    dq_menu(&dqk, "t\x01P\x01P\r" "1000\r" "\r" "3\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(cok_field_u16_at(L, a->record, COK_FIELD_COINS, 5) == 3);
    CHECK(cok_field_u16_at(L, a->record, COK_FIELD_COINS, 6) == 1);

    /* Overloaded: nothing is taken and the list comes again (Escape ends it). */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 2000;
    cok_field_set16_at(L, a->record, COK_FIELD_COINS, 4, 64000);
    dqk.vm.character = a->record;
    dq_menu(&dqk, "t\r" "1600\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(cok_field_u16_at(L, a->record, COK_FIELD_COINS, 4) == 64000);

    /* With items too, Take asks Money or Items: Money takes (the list, Escape, then Take's bar,
     * Escape), Items takes (test_dark_queen_take_items). */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dq_pool_item(&dqk, 3);
    dq_menu(&dqk, "tm\x1b\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dq_pool_item(&dqk, 3);
    /* End in Take's bar picks the next member (75cc:0e15 -> 5e22:067c); with the money gone the
     * bar is "Items Exit" (0d61-0dd2: DS:7fda, index + 1), logged; its Items' list is left by
     * Escape (the bar again) and Escape ends Take. */
    cok_character *b = dqk.party.count > 1 ? dqk.party.members[1] : NULL;
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 10;
    dq_pool_item(&dqk, 3);
    dqk.vm.character = a->record;
    dq_menu(&dqk, "t\x01O" "m\r" "10\r" "i\x1b\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(b != NULL && dqk.vm.character == b->record);
    CHECK(cok_field_u16_at(L, b->record, COK_FIELD_COINS, 4) == 10);
    CHECK(strstr(dq_quiet.log, "menu: ~Money ~Items ~Exit;menu: ~Items ~Exit;") != NULL);
}

/* A pool item of type 0x27 with a count and a weight, at index (the count is what Take's items
 * joins and the bundles count: 771f:0295). */
static void dq_pool_counted(cok_adventure *adv, size_t index, uint8_t count, uint8_t weight)
{
    const cok_rules_item_record *ir = &cok_rules_dark_queen.item_record;
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[ir->type] = 0x27;
    it[ir->bonus] = 5;
    it[ir->count] = count;
    it[ir->weight] = weight;
    CHECK(cok_pool_insert(&adv->pool, index, it));
}

/* A scroll bundle (type 0x49) with its count, at the head of the pool. */
static void dq_pool_bundle(cok_adventure *adv, uint8_t count)
{
    const cok_rules_item_record *ir = &cok_rules_dark_queen.item_record;
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[ir->type] = 0x49;
    it[ir->count] = count;
    CHECK(cok_pool_insert(&adv->pool, 0, it));
}

/* An item's 17 bytes (the file's, at +0x28) from hex: the joins' items of 771f:0295's tests. */
static void dq_item_hex(uint8_t *item, const char *hex)
{
    memset(item, 0, COK_ITEM_SIZE);
    for (size_t k = 0; k < 17; ++k) {
        unsigned byte;
        CHECK(sscanf(hex + 2 * k, "%2x", &byte) == 1);
        item[0x28 + k] = (uint8_t)byte;
    }
}

/* A pool item (its 17 bytes as the file's, from item) at index, its link bytes kept as
 * TREASURE's pool_put keeps them (cok_pool_insert clears +0x2a-+0x2d). */
static void dq_pool_put(cok_adventure *adv, size_t index, const uint8_t *item)
{
    CHECK(cok_pool_insert(&adv->pool, index, item));
    memcpy(adv->pool.items[index] + 0x2a, item + 0x2a, 4);
}

/* Gives member m an item (a copy of item) last. */
static void dq_hold(cok_character *m, const uint8_t *item)
{
    CHECK(cok_character_insert_item(m, m->item_count, item));
}

/* Take's items (part 21b, 75cc:0bbe and 771f:0295, dark_queen.c's treasure_take_items and
 * queen_view.c's queen_take_item) with no money: the list's Take gives the row to the selected
 * member, last of its items, joined with the others of its kind (1fda) when it has a count, and
 * its stats are recomputed; a refused item (a bundle with the party's bundles, or the 16th item
 * that does not join, or the weight) stays with "OverLoaded" or "Too many Bundles!" on the bar.
 * The list comes back with the rows left, the cursor kept (clamped to the last row) and ends on
 * Escape or when no item is left. A member with no items taking a counted item is a cited stop
 * (771f:03b4). */
static void test_dark_queen_take_items(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's Take's items not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    const cok_character_layout *L = cok_rules_dark_queen.character;
    const cok_rules_item_record *ir = &cok_rules_dark_queen.item_record;
    cok_character *a = dq_member(&dqk, 0, 1), *b = dq_member(&dqk, 0, 1);
    uint8_t item[COK_ITEM_SIZE];

    /* The head (uncounted, weight 10) goes to a, last; the list comes back with one row, the
     * second (a counted item) is left; Escape leaves, then Exit asks. The member's weight is up
     * by the head's weight (10, one of it). */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dqk.pool.items[0][ir->weight] = 10;
    dq_pool_counted(&dqk, 1, 4, 5);
    dqk.vm.character = a->record;
    uint16_t before = cok_field_u16(L, a->record, COK_FIELD_WEIGHT);
    dq_menu(&dqk, "t\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == 1 && a->items[0][ir->bonus] == 3);
    CHECK(cok_field_u16(L, a->record, COK_FIELD_WEIGHT) == before + 10);
    CHECK(dq_count(dq_quiet.log, "item: ") == 3); /* two rows, then the one left */

    /* Down, then Return takes the second row: a counted one, to b, which holds a plain item
     * (so no stop). The pool keeps the head. */
    uint8_t plain[COK_ITEM_SIZE] = {0};
    plain[ir->type] = 0x27;
    plain[ir->bonus] = 9;
    dq_hold(b, plain);
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dq_pool_counted(&dqk, 1, 4, 5);
    dq_menu(&dqk, "\x01P" "t\x01P\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(b->item_count == 2 && b->items[1][ir->count] == 4);
    CHECK(dq_count(dq_quiet.log, "item: ") == 3);
    dq_clear_pool(&dqk);

    /* Return on the last row, taken, leaves the cursor on the new last row (the clamp, 1b05:0055,
     * where the list would reset it to the first): the pool is 5, 4, 3 (head first); Down twice
     * and Return takes 3, then Return takes 4 (the clamp to the last row), and Escape leaves 5. */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    dq_pool_item(&dqk, 4);
    dq_pool_item(&dqk, 5);
    while (a->item_count > 0) cok_character_remove_item(a, 0);
    dqk.vm.character = a->record;
    dq_menu(&dqk, "t\x01P\x01P\r\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == 2 && a->items[0][ir->bonus] == 3 && a->items[1][ir->bonus] == 4);
    CHECK(dq_count(dq_quiet.log, "item: ") == 3 + 2 + 1); /* rows: 3, then 2, then 1 */

    /* OverLoaded: a's steel 64000 (its weight over the allowance and 1,500 with the item): the
     * bar says so and nothing moves; Escape leaves. */
    dq_clear_pool(&dqk);
    dq_pool_item(&dqk, 3);
    cok_field_set16_at(L, a->record, COK_FIELD_COINS, 4, 64000);
    dqk.vm.character = a->record;
    size_t held = a->item_count;
    dq_menu(&dqk, "t\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == held && dq_count(dq_quiet.log, "item: ") == 2);
    cok_field_set16_at(L, a->record, COK_FIELD_COINS, 4, 0);

    /* Too many Bundles!: a scroll bundle with a count and none held by the party. Nothing moves;
     * a bundle with no count (0) is taken as any item. */
    dq_clear_pool(&dqk);
    dq_pool_bundle(&dqk, 1);
    dqk.vm.character = a->record;
    held = a->item_count;
    dq_menu(&dqk, "t\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == held && dq_count(dq_quiet.log, "item: ") == 2);
    dq_clear_pool(&dqk);
    dq_pool_bundle(&dqk, 0);
    dq_menu(&dqk, "t\r" "e");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == held + 1);

    /* Joining (86b8:2fbf with stack, 1fda): a holds the joiner (count 40, +0x33 0) and the pool
     * has ITEM.DAT 80 (count 10, +0x33 6): the 2fbf test joins them, the 1fda test (+0x33
     * compared) does not, so two items. */
    dq_item_hex(item, "1e00701e0200dc05020001000028000000");
    dq_hold(a, item);
    dq_item_hex(item, "1e00701e0200dc0502000006000a000000");
    dq_clear_pool(&dqk);
    dq_pool_put(&dqk, 0, item);
    held = a->item_count;
    dq_menu(&dqk, "t\r" "e");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == held + 1 && a->items[held][ir->count] == 10);
    CHECK(a->items[held - 1][ir->count] == 40);
    dq_clear_pool(&dqk);

    /* With +0x33 equal, 1fda joins them: the count 40 + 10 is one item of 50. */
    dq_clear_pool(&dqk);
    while (a->item_count > 0) cok_character_remove_item(a, 0);
    dq_item_hex(item, "1e00701e0200dc05020001000028000000");
    item[0x33] = 6;
    dq_hold(a, item);
    dq_item_hex(item, "1e00701e0200dc0502000006000a000000");
    dq_pool_put(&dqk, 0, item);
    dq_menu(&dqk, "t\r" "e");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == 1 && a->items[0][ir->count] == 50);

    /* The sixteenth item: a holds 15 plain ones and the joiner (16). The pool's head (not joined)
     * is OverLoaded; ITEM.DAT 80 joins the joiner (+0x33 equal) and is taken: the member keeps
     * sixteen, the joiner's count 50, and the head stays in the pool. */
    dq_clear_pool(&dqk);
    while (a->item_count > 0) cok_character_remove_item(a, 0);
    for (int k = 0; k < 15; ++k) {
        dq_pool_item(&dqk, 9);
        CHECK(dqk.pool.item_count == 1);
        dq_hold(a, dqk.pool.items[0]);
        dq_clear_pool(&dqk);
    }
    dq_item_hex(item, "1e00701e0200dc05020001000028000000");
    item[0x33] = 6;
    dq_hold(a, item);
    dq_pool_item(&dqk, 3);
    dq_item_hex(item, "1e00701e0200dc0502000006000a000000");
    dq_pool_put(&dqk, 1, item);
    dqk.vm.character = a->record;
    dq_menu(&dqk, "t\r" "\x01P\r" "\x1b" "en");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(a->item_count == 16 && a->items[15][ir->count] == 50);
    CHECK(dq_count(dq_quiet.log, "item: ") == 5); /* 2 rows, 2 rows, then the head's 1 */
    dq_clear_pool(&dqk);

    /* With money too, Items takes the last item and Take ends at once (done when no items is
     * left: pool_flags reads the count, so the Take bar is not drawn again); the menu's Exit then
     * asks, the money being left. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 10;
    dq_pool_item(&dqk, 3);
    dq_menu(&dqk, "\x01P" "ti\r" "e" "n");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(dq_count(dq_quiet.log, "menu: ~Money ~Items ~Exit;") == 1);
    /* the item's list (one row, taken), then the menu at once: the Take bar is not drawn again */
    CHECK(strstr(dq_quiet.log, "menu: ~Take ~Exit;menu: ~View ~Take ~Pool ~Share ~Exit;choice: 5;")
          != NULL);

    /* Escape in the list is the Take bar again; its Exit leaves Take with the item still there. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 10;
    dq_pool_item(&dqk, 3);
    dq_menu(&dqk, "\x01P" "ti\x1b" "ee" "n");
    CHECK(dqk.vm.status == COK_ECL_OK && dq_count(dq_quiet.log, "item: ") == 1);
    CHECK(dq_count(dq_quiet.log, "menu: ~Money ~Items ~Exit;") == 2);
    dq_clear_pool(&dqk);

    /* A member with no items taking a counted item stops (771f:03b4: 1fda's node unset). */
    dq_clear_pool(&dqk);
    dq_pool_counted(&dqk, 0, 2, 5);
    while (b->item_count > 0) cok_character_remove_item(b, 0);
    dq_menu(&dqk, "\x01P" "t\r");
    CHECK(dqk.vm.status == COK_ECL_UNPORTED &&
          strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 771f:03b4") != NULL);
    dq_clear_pool(&dqk);
}

/* Share (part 20b, 74b9:01ee) over six standing members with 200 steel each, the pool 1000 steel,
 * 3 gems, 1 jewelry: the probe's shares (steel 367 four times, 366 twice; gems 1 for the first
 * three; jewelry 1 for the first), the pool empty. */
static void test_dark_queen_treasure_share(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's treasure share not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    const cok_character_layout *L = cok_rules_dark_queen.character;
    cok_character *m[6];
    for (int i = 0; i < 6; ++i) {
        m[i] = dq_member(&dqk, 0, 1);
        cok_field_set16_at(L, m[i]->record, COK_FIELD_COINS, 4, 200);
    }
    CHECK(dqk.party.count == 6);
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dqk.pool.coins[5] = 3;
    dqk.pool.coins[6] = 1;
    dqk.vm.character = m[0]->record;
    dq_menu(&dqk, "se");
    CHECK(dqk.vm.status == COK_ECL_OK);
    for (int i = 0; i < 6; ++i) {
        CHECK(cok_field_u16_at(L, m[i]->record, COK_FIELD_COINS, 4) == (i < 4 ? 367 : 366));
        CHECK(cok_field_u16_at(L, m[i]->record, COK_FIELD_COINS, 5) == (i < 3 ? 1 : 0));
        CHECK(cok_field_u16_at(L, m[i]->record, COK_FIELD_COINS, 6) == (i == 0 ? 1 : 0));
    }
    CHECK(dqk.pool.coins[4] == 0 && dqk.pool.coins[5] == 0 && dqk.pool.coins[6] == 0);
}

/* Share with no counted member (NPC 0x01: it takes a share but is not counted, so n is 0): each
 * share is 0xffff, and each record over the cap takes the fit (the cap 1500 less its weight, which
 * holds its 200 steel: 1300), not a divide by zero. The rest is not given (no room is left) and the
 * pool is lost on leaving (treasure_end's free). */
static void test_dark_queen_treasure_share_none_counted(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's share with none counted not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    const cok_character_layout *L = cok_rules_dark_queen.character;
    cok_character *m[2];
    for (int i = 0; i < 2; ++i) {
        m[i] = dq_member(&dqk, 0, 1);
        cok_field_set8(L, m[i]->record, COK_FIELD_NPC, 0x01);
        cok_field_set16_at(L, m[i]->record, COK_FIELD_COINS, 4, 200);
    }
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dqk.vm.character = m[0]->record;
    dq_menu(&dqk, "sen");
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.party.count == 2);
    CHECK(cok_field_u16_at(L, m[0]->record, COK_FIELD_COINS, 4) == 1500);
    CHECK(cok_field_u16_at(L, m[1]->record, COK_FIELD_COINS, 4) == 1500);
}

/* A 0xb3 member is counted in the shares but takes none: six members, five normal and one 0xb3,
 * 1000 steel: the five take 166 each (the first four one more), the 0xb3 keeps its 200. Then Pool
 * and Share: the pool gathers the members' steel, and the 1201 over six is 200 each and 1 more for
 * the first. */
static void test_dark_queen_treasure_pool_and_share(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's pool and share not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    const cok_character_layout *L = cok_rules_dark_queen.character;
    cok_character *m[6];
    for (int i = 0; i < 6; ++i) {
        m[i] = dq_member(&dqk, 0, 1);
        cok_field_set16_at(L, m[i]->record, COK_FIELD_COINS, 4, 200);
    }
    cok_field_set8(L, m[5]->record, COK_FIELD_NPC, 0xb3);
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dqk.vm.character = m[0]->record;
    dq_menu(&dqk, "se");
    CHECK(dqk.vm.status == COK_ECL_OK);
    for (int i = 0; i < 5; ++i)
        CHECK(cok_field_u16_at(L, m[i]->record, COK_FIELD_COINS, 4) == (i < 4 ? 367 : 366));
    CHECK(cok_field_u16_at(L, m[5]->record, COK_FIELD_COINS, 4) == 200);
    CHECK(dqk.pool.coins[4] == 0);

    /* Pool then Share: the pool holds 1 steel to start (money, so the menu offers Share), the
     * members' 1200 come in with it: 1201 over six is 200 each and 1 more for the first. Share
     * again after the pool, Exit with no money leaves without a question. */
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1;
    for (int i = 0; i < 6; ++i) cok_field_set16_at(L, m[i]->record, COK_FIELD_COINS, 4, 200);
    cok_field_set8(L, m[5]->record, COK_FIELD_NPC, 0);
    dqk.vm.character = m[0]->record;
    dq_menu(&dqk, "pse");
    CHECK(dqk.vm.status == COK_ECL_OK);
    CHECK(cok_field_u16_at(L, m[0]->record, COK_FIELD_COINS, 4) == 201);
    for (int i = 1; i < 6; ++i) CHECK(cok_field_u16_at(L, m[i]->record, COK_FIELD_COINS, 4) == 200);
    CHECK(dqk.pool.coins[4] == 0);
}

/* The claim question's Yes/No bar animates the small picture by DS:ae64 (1bdb:0424 passes it):
 * with a picture shown before COMBAT the question's wait animates it, with none it does not. */
static void test_dark_queen_claim_animation(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's claim animation not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    dq_member(&dqk, 0, 1);
    dq_member(&dqk, 0, 1);
    dq_frames = &dqk;
    for (int flag = 0; flag < 2; ++flag) {
        dq_clear_pool(&dqk);
        dqk.pool.coins[4] = 1000;
        dqk.picture_shown = flag;
        dq_animating = true;
        dq_menu(&dqk, "e\x03n"); /* Exit, the question's wait, No */
        CHECK(dqk.vm.status == COK_ECL_OK && dq_animating == (bool)flag);
    }
    dq_frames = NULL;
}

/* The mode-6 redraw keeps the small picture's frame (63ae:00c4-00d9 skips the load and the
 * reset of DS:b20e): the menu's wait advances to frame 1, View and Escape show the menu again,
 * and it still has frame 1. */
static void test_dark_queen_picture_frame_kept(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's picture frame not checked");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    dq_member(&dqk, 0, 1);
    dq_member(&dqk, 0, 1);
    dq_clear_pool(&dqk);
    dqk.pool.coins[4] = 1000;
    dq_frames = &dqk;
    dq_menu(&dqk, "\x02v\x1b");
    CHECK(cok_dark_queen_picture_frame(&dqk) == 1);
    dq_frames = NULL;
}

/* The temple's Down picks the next member (7825:0af6 calls 5e22:067c): the Dark Queen temple's
 * menu from the first member, Down, Exit. */
static void test_dark_queen_temple_member_keys(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's temple member keys not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    dq_member(&dqk, 0, 1);
    cok_character *b = dq_member(&dqk, 0, 1);
    dqk.vm.character = dqk.party.members[0]->record;
    dq_keys("\x01P" "e");
    cok_dark_queen_temple(&dqk);
    CHECK(dqk.vm.character == b->record);
}

/* The temple's bar does not animate the small picture (7825:0b43 pushes 0 for the anim): with
 * picture 0x81 loaded, the temple's wait is not animating. */
static void test_dark_queen_temple_no_animation(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's temple animation not run");
        return;
    }
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    dq_member(&dqk, 0, 1);
    DQK_RUN(&dqk, DQK_PICTURE, 0, 0x81);
    CHECK(dqk.vm.status == COK_ECL_OK);
    dq_frames = &dqk;
    dq_animating = true;
    dq_keys("\x03" "e");
    cok_dark_queen_temple(&dqk);
    CHECK(!dq_animating);
    dq_frames = NULL;
}

#undef DQK_PICTURE
#undef DQK_SETUP
#undef DQK_APPROACH

#undef DQK_LOAD_MONSTER
#undef DQK_CLEARMONSTERS
#undef DQK_COMBAT

int main(void)
{
    open_game();
    test_load_monster();
    test_difficulty();
    test_missing_monster();
    test_damaged_files();
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
    test_death_knights_monsters();
    test_dark_queen_monsters();
    test_dark_queen_appends();
    test_dark_queen_no_monsters();
    test_dark_queen_after_combat();
    test_dark_queen_enemies();
    test_dark_queen_results();
    test_dark_queen_rest();
    test_dark_queen_experience();
    test_dark_queen_pool_and_past_party();
    test_dark_queen_treasure_menu();
    test_dark_queen_treasure_take();
    test_dark_queen_take_items();
    test_dark_queen_treasure_share();
    test_dark_queen_treasure_share_none_counted();
    test_dark_queen_treasure_pool_and_share();
    test_dark_queen_claim_animation();
    test_dark_queen_picture_frame_kept();
    test_dark_queen_temple_member_keys();
    test_dark_queen_temple_no_animation();
    test_dark_queen_treasure_items();
    test_dark_queen_random_scroll();
    test_dark_queen_heal();
    test_dark_queen_jump_table();
    test_dark_queen_ambush_gorgons();
    test_dark_queen_clear_between();
    test_dark_queen_six_loads();
    test_dark_queen_undead_clear();
    test_dark_queen_open_ground_flags();
    test_dark_queen_encounter();
    test_dark_queen_picture_flags();
    cok_adventure_close(&game);
    puts("monster tests passed");
    return 0;
}
