#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* The hooks a front end relies on beyond the keys: where the game flushes
 * the keyboard (1614:045c, hooks.flush) and which menus animate the small
 * picture while they wait (67b5:03e2's flag, game->menu_animates). */
#include "adventure.h"
#include "ai.h"
#include "arena.h"
#include "camp.h"
#include "monster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static cok_adventure game;

/* Keys from a string, \x01 the 0 of an extended key; each read notes
 * whether the menu reading it animates. */
typedef struct {
    const char *keys;
    size_t at, length;
    unsigned flushes, reads;
    bool animating;
} script;

static script s;

static int scripted(void *context)
{
    script *p = context;
    p->animating = game.menu_animates;
    ++p->reads;
    if (p->at == p->length) return -1;
    char c = p->keys[p->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static void flushed(cok_adventure *g, void *context)
{
    (void)g;
    ++((script *)context)->flushes;
}

static void keys(const char *k)
{
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.flushes = s.reads = 0;
    s.animating = false;
    game.input_ended = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
}

static cok_character *member(const char *name)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    c->record[0x189] = 1;
    CHECK(cok_party_add(&game.party, c));
    return c;
}

/* The loads that flush once they have loaded (127f:029a, 6961:0514,
 * 6961:06b2, 6d21:04a5, 4b6d:0a66, 4b6d:18ec), and only then. */
static void test_loads(void)
{
    cok_picture picture = {0};
    keys("");
    CHECK(cok_adventure_load_image(&game, "TITLE", 1, -1, &picture) && s.flushes == 1);
    keys("");
    CHECK(!cok_adventure_load_image(&game, "TITLE", 200, -1, &picture) && s.flushes == 0);
    cok_picture_free(&picture);

    game.vm.file = 1;
    cok_adventure_free_picture(&game);
    keys("");
    cok_adventure_load_picture(&game, 12);
    CHECK(game.frame_count == 4 && s.flushes == 1);
    keys("");
    cok_adventure_load_picture(&game, 12); /* held: 6961:011e returns at once */
    CHECK(s.flushes == 0);
    keys("");
    cok_adventure_load_sprite(&game, 12);
    CHECK(game.picture_sprite && s.flushes == 1);
    cok_adventure_free_picture(&game);

    /* The portrait: head and body through 127f:0111, then 6961:06b2 on
     * every path, the pieces held too. */
    game.vm.file = 2;
    cok_adventure_forget_portrait(&game);
    keys("");
    cok_adventure_portrait(&game, 69, 68);
    CHECK(game.head_id == 69 && s.flushes == 3);
    keys("");
    cok_adventure_portrait(&game, 69, 68);
    CHECK(s.flushes == 1);

    /* Combat icons: each image through 127f:0111, then 6d21:04a5. */
    game.vm.file = 1;
    keys("");
    CHECK(cok_arena_load_icon(&game, "CPIC", 1, 8) && s.flushes == 3);
    cok_character *c = member("ALDA");
    c->record[0x138] = 2;
    c->record[0x137] = 0;
    c->record[0x135] = 5;
    c->record[0x136] = 0x17;
    keys("");
    /* Head and body each through 6d21:01d0 (two images and its flush),
     * then 4b6d:0a66. */
    CHECK(cok_arena_party_icon(&game, c, true) && s.flushes == 7);

    /* A monster's records (4b6d:161b). */
    cok_character monster;
    keys("");
    CHECK(cok_monster_read(&game, 1, &monster) && s.flushes == 1);
    cok_character_free(&monster);
    cok_party_free(&game.party);
}

/* The computer's turn polls the keyboard and flushes as it returns
 * (3afb:12eb), with no key waiting too; paging text waits for a key and
 * flushes (1521:06ea). */
static void test_waits(void)
{
    cok_character *c = member("ALDA");
    bool back;
    keys("");
    CHECK(cok_ai_keys(&game, c, &back) && !back && s.flushes == 1 && s.reads == 0);
    char text[300];
    memset(text, 'A', sizeof text - 1);
    for (size_t i = 7; i < sizeof text - 1; i += 8) text[i] = ' ';
    text[sizeof text - 1] = '\0';
    game.vm.cursor = (cok_text_cursor){1, 17};
    keys("x");
    cok_adventure_print(&game, text, (cok_text_window){1, 17, 38, 22}, 10, true);
    CHECK(s.reads == 1 && s.flushes == 1);
    cok_party_free(&game.party);
}

/* A saved game's characters read (4b6d:15ce, each), then the game
 * loaded (4b6d:20ef). */
static void test_saved_game(void)
{
    cok_character *c = member("ALDA");
    c->record[0x138] = 2;
    c->record[0x135] = 5;
    c->record[0x136] = 0x17;
    game.vm.file = 1;
    game.vm.mem4b00[0xe6] = 0; /* outside 3D areas: the overland map, not walls */
    CHECK(cok_camp_save_game(&game, 'A'));
    cok_party_free(&game.party);
    char path[600];
    snprintf(path, sizeof path, "%s/SAVGAMA.DAT", game.save_dir);
    keys("");
    CHECK(cok_adventure_restore(&game, path) && game.party.count == 1);
    /* The character's files, its icon (head and body, two images and a
     * flush each, then its own), the overland map's picture (127f:029a)
     * and the game. */
    CHECK(s.flushes == 1 + 7 + 1 + 1);
    cok_party_free(&game.party);
}

/* The menus that animate the small picture while they wait. */
static void test_animating(void)
{
    uint8_t *who = member("ALDA")->record;
    bool ended;
    /* Pick Character in camp and treasure (6346:32c7's local). */
    for (uint8_t mode = 0; mode < 7; ++mode) {
        game.vm.mode = mode;
        keys("\x1b");
        cok_adventure_pick(&game, "Pick", who, true, &ended);
        CHECK(s.reads >= 1 && s.animating == (mode == 2 || mode == 6) && !game.menu_animates);
    }
    /* Save Which Game in camp only (4b6d:2322: DS:4b49 == 2). */
    for (uint8_t mode = 0; mode < 7; ++mode) {
        game.vm.mode = mode;
        keys("\x1b");
        cok_camp_save(&game);
        CHECK(s.reads == 1 && s.animating == (mode == 2) && !game.menu_animates);
    }
    /* HORIZONTAL MENU over a picture that replaced the view (2fd3:11f9):
     * ECL1 block 16's "YES" "NO" at 8dd5. */
    game.vm.file = 1;
    game.vm.mode = 4;
    CHECK(cok_adventure_load(&game, 16) == COK_ECL_OK);
    for (int shown = 0; shown < 4; ++shown) {
        game.picture_shown = shown & 1;
        game.view_replaced = shown & 2;
        keys("Y");
        cok_ecl_run(&game.vm, 0x8dd5);
        CHECK(s.reads >= 1 && s.animating == (shown == 3));
    }
    cok_party_free(&game.party);
}

int main(void)
{
    cok_keyboard k = {scripted, &s, NULL};
    cok_adventure_hooks hooks = {.flush = flushed, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &k, &hooks));
    char dir[] = "/tmp/cok-hooks-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
    test_loads();
    test_waits();
    test_saved_game();
    test_animating();
    cok_adventure_close(&game);
    DIR *d = opendir(dir);
    CHECK(d != NULL);
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (e->d_name[0] == '.') continue;
        char path[600];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        unlink(path);
    }
    closedir(d);
    rmdir(dir);
    puts("hooks tests passed");
    return 0;
}
