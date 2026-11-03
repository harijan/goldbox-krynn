#define _POSIX_C_SOURCE 200809L /* mkdtemp, symlink */
#include "monster.h"

#include "camp.h"

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

static bool dq_open(cok_adventure *adv)
{
    static script quiet = {"", 0, 0, ""};
    static cok_keyboard keys;
    static cok_adventure_hooks hooks;
    keys = (cok_keyboard){scripted, &quiet, NULL, NULL};
    hooks = (cok_adventure_hooks){.log = log_line, .context = &quiet};
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

/* COMBAT with no monsters (dqk 5f07:081b's branches, in its order): the vault (var 0x31 1)
 * stops, cited (771f:09a0); the temple (var 0x14 1) runs its menu, with no key the run goes on
 * (the variable cleared, the mode 4 with no 3D area and variable 0x24 not 1, 3 where it is 1);
 * with none wanted the treasure stops (75cc:135c). */
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
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED && cok_ecl_var(&dqk.vm, COK_VAR_VAULT) == 0);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 771f:09a0") != NULL);
    CHECK(cok_ecl_var(&dqk.vm, COK_VAR_TEMPLE) == 1);

    /* The temple: no key, so its menu runs out and the branch ends with status OK. */
    cok_ecl_set_var(&dqk.vm, COK_VAR_VAULT, 0);
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_ecl_var(&dqk.vm, COK_VAR_TEMPLE) == 0);
    CHECK(dqk.vm.mode == 4); /* no 3D area (0x22 0) and variable 0x24 not 1 */
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    cok_ecl_store(&dqk.vm, 0x24, 1);
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.mode == 3); /* the overland */
    cok_ecl_set_var(&dqk.vm, COK_VAR_TEMPLE, 1);
    cok_ecl_store(&dqk.vm, 0x22, 1); /* a 3D area: fought (the redraw), mode 4 */
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.vm.mode == 4);
    cok_ecl_store(&dqk.vm, 0x22, 0);
    cok_ecl_store(&dqk.vm, 0x24, 0);

    /* The treasure: nothing wanted, cited (75cc:135c). */
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 75cc:135c") != NULL);
}

/* LOAD MONSTER 64 x2 and 49 x1 register, COMBAT appends the SHARKMEN and the
 * MINOTAUR, then 170e's ten GORGONs (the icon in slot 10), and stops at the
 * battle. CLEARMONSTERS then resets the slots and the undead count, as
 * 75cc:151b does (DS:ad6b to 8, DS:c2ed to 0). */
static void test_dark_queen_appends(void)
{
    static cok_adventure dqk;
    if (!dq_open(&dqk)) {
        puts("monster: DQKAssets not found; Dark Queen's appends not run");
        return;
    }
    const cok_character_layout *L = cok_rules_dark_queen.character;

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

    /* COMBAT appends them (2 SHARKMEN, 1 MINOTAUR, and 170e's ten GORGONs in
     * slot 10), then the battle is not ported: the run stops there. */
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 59c7:1864") != NULL);
    CHECK(dqk.monsters == 13 && dqk.icon_slot == 11 && dqk.party.count == 13);
    CHECK(dqk.undead == 0);
    static const char *names[] = {"SHARKMAN", "SHARKMAN", "MINOTAUR"};
    for (size_t i = 0; i < 13; ++i) {
        cok_character *m = dqk.party.members[i];
        char name[16];
        CHECK(cok_field_name(L, m->record, name) > 0);
        if (i < 3) CHECK(strcmp(name, names[i]) == 0);
        else CHECK(strcmp(name, "GORGON") == 0);
        /* The icon slots: 8 for the SHARKMEN, 9 for the MINOTAUR, 10 for 170e's. */
        CHECK(cok_field_u8(L, m->record, COK_FIELD_ICON) == (i < 2 ? 8 : i == 2 ? 9 : 10));
        if (i >= 3) { /* 170e's: status 9, the NPC bits, hostile 0 (var 0x3a is 0). */
            CHECK(m->record[0x64] == 9 && m->record[0x65] == 0 && m->record[0x66] == 0);
            CHECK((m->record[0x9b] & 0x32) == 0x32);
        }
    }

    /* CLEARMONSTERS (75cc:151b): the flag and the count, the undead count, and the
     * next icon slot back to 8 (the block's start, 6c70:0001, does the same). The
     * records stay in the list. The next COMBAT with none loaded and no vault or
     * temple wanted: the treasure stops, cited (5f07:081b's last branch, 75cc:135c). */
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    CHECK(dqk.vm.status == COK_ECL_OK && !dqk.monsters_loaded && dqk.monsters == 0);
    CHECK(dqk.icon_slot == 8 && dqk.undead == 0 && dqk.party.count == 13);
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 75cc:135c") != NULL);

    /* A new block's LOAD MONSTER takes slot 8 again, one SHARKMAN and 170e's ten
     * GORGONs: the difficulty scales the hostile SHARKMAN's hit points by
     * (d + 1) / 4 (6c70:29fc), at least 1; var 0x27 is the difficulty. */
    cok_character base;
    CHECK(cok_monster_read(&dqk, 64, &base));
    uint8_t hp = base.record[0x198], hostile = base.record[0x66];
    cok_character_free(&base);
    CHECK(hostile == 1 && hp > 1);
    size_t at = dqk.party.count;
    cok_ecl_set_var(&dqk.vm, COK_VAR_DIFFICULTY, 1);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 1, 0, 64);
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED && dqk.party.count == at + 11);
    CHECK(dqk.party.members[at]->record[0x198] == (uint8_t)(2 * hp / 4) &&
          dqk.party.members[at]->record[0x89] == (uint8_t)(2 * hp / 4));
    cok_ecl_set_var(&dqk.vm, COK_VAR_DIFFICULTY, 3);
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    DQK_RUN(&dqk, DQK_LOAD_MONSTER, 0, 64, 0, 1, 0, 64);
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.party.members[at + 11]->record[0x198] == hp);
    cok_adventure_close(&dqk);
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
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 59c7:1864") != NULL);
    CHECK(dqk.party.count == 11 && dqk.monsters == 11 && dqk.icon_slot == 10);
    char name[16];
    CHECK(cok_field_name(cok_rules_dark_queen.character, dqk.party.members[0]->record, name) > 0);
    CHECK(strcmp(name, "SHARKMAN") == 0);
    cok_adventure_close(&dqk);
}

/* Six LOAD MONSTERs (slots 8 to 13), then COMBAT: 170e's icon takes slot 14, so
 * the icon slots are 15 (the original's 22 records, c6f1 16, ad6b 15). The
 * monsters' icons are not loaded yet (204c:00df): no icon slot error. The ZOMBIE
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
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 59c7:1864") != NULL);
    CHECK(dqk.party.count == 16 && dqk.monsters == 16 && dqk.icon_slot == 15);
    CHECK(dqk.undead == 2);
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
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED && dqk.undead == 1);
    DQK_RUN(&dqk, DQK_CLEARMONSTERS);
    CHECK(dqk.vm.status == COK_ECL_OK && dqk.undead == 0 && dqk.icon_slot == 8);
    CHECK(dqk.party.count == 11); /* the ZOMBIE and 170e's ten GORGONs */
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
    CHECK(open_ground_fight(&a, 0) == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&a, a.vm.status), "dqk 59c7:1864") != NULL);
    CHECK(open_ground_fight(&b, 1) == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&b, b.vm.status), "dqk 59c7:1864") != NULL);
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
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(cok_adventure_why(&dqk, dqk.vm.status), "dqk 59c7:1864") != NULL);
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
        DQK_RUN(&dqk, DQK_COMBAT);
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
    DQK_RUN(&dqk, DQK_COMBAT);
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
    DQK_RUN(&dqk, DQK_COMBAT);
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
    DQK_RUN(&dqk, DQK_COMBAT);
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
    DQK_RUN(&dqk, DQK_COMBAT);
    CHECK(dqk.vm.status == COK_ECL_OK && cok_field_u8(L, m->record, COK_FIELD_STATUS) == 0);
    CHECK(cok_field_u8(L, m->record, COK_FIELD_HIT_POINTS) == 1);
    m->items = saved;
    m->item_count = saved_count;
    cok_adventure_close(&dqk);
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
    test_dark_queen_heal();
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
