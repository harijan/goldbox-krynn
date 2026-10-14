#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* The hooks a front end relies on beyond the keys: where the game flushes
 * the keyboard (1614:045c, hooks.flush), which menus animate the small
 * picture while they wait (67b5:03e2's flag, game->menu_animates), and
 * the calls to the sound driver (hooks.sound). */
#include "adventure.h"
#include "ai.h"
#include "arena.h"
#include "camp.h"
#include "monster.h"
#include "start.h"
#include "test_loading_data.h"

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

/* Set while test_loading records the screen at each read, flush and key. */
static void (*recording)(const char *kind);

static int scripted(void *context)
{
    script *p = context;
    if (recording != NULL) recording("key");
    p->animating = game.menu_animates;
    ++p->reads;
    if (p->at == p->length) return -1;
    char c = p->keys[p->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static void flushed(cok_adventure *g, void *context)
{
    (void)g;
    if (recording != NULL) recording("flush");
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

/* The sound driver's calls, as letters: C and the command, I install, O
 * and E an overlay's uninstall and reinstall, R a DAX record read, K
 * Ctrl-S, M and N the title's music and its end, Q quitting. */
static char calls[8192];

static void sounded(cok_adventure *g, cok_sound_call call, unsigned value, void *context)
{
    (void)g;
    (void)context;
    static const char letters[] = "CIOERKMNQ";
    if (recording != NULL && call == COK_SOUND_RECORD) recording("read");
    size_t used = strlen(calls);
    if (call == COK_SOUND_COMMAND)
        snprintf(calls + used, sizeof calls - used, "C%u ", value);
    else
        snprintf(calls + used, sizeof calls - used, "%c ", letters[call]);
}

static unsigned count(const char *text, const char *what)
{
    unsigned n = 0;
    for (const char *p = strstr(text, what); p != NULL; p = strstr(p + 1, what)) ++n;
    return n;
}

/* Where the game calls the sound driver: each DAX record read (169c:088e,
 * commands 0 and 1), the sounds the combat screen plays, Ctrl-S (or Alt-R's
 * scan code) read by a menu (1614:025b) but not by text input (Crt's
 * ReadKey), quitting (1614:0000), the title's music and the install after
 * it (1000:0160). */
static void test_sounds(void)
{
    cok_picture picture = {0};
    calls[0] = '\0';
    keys("");
    CHECK(cok_adventure_load_image(&game, "TITLE", 1, -1, &picture));
    CHECK(!cok_adventure_load_image(&game, "TITLE", 200, -1, &picture));
    CHECK(strcmp(calls, "R R ") == 0);
    cok_picture_free(&picture);

    calls[0] = '\0';
    cok_arena_sound(&game, 7);
    CHECK(strcmp(calls, "C7 ") == 0);

    uint8_t *who = member("ALDA")->record;
    bool ended;
    calls[0] = '\0';
    keys("\x13\x01\x13\x1b");
    cok_adventure_pick(&game, "Pick", who, true, &ended);
    CHECK(strcmp(calls, "K K ") == 0);
    cok_party_free(&game.party);

    char line[41];
    calls[0] = '\0';
    keys("\x13" "A\r");
    cok_keyboard raw = cok_adventure_keyboard_raw(&game);
    CHECK(cok_text_input(&game.screen, &game.font, "", 10, 0, 40, &raw, line));
    CHECK(calls[0] == '\0');
    keys("\x13" "A\r");
    cok_keyboard read = cok_adventure_keyboard(&game);
    CHECK(cok_text_input(&game.screen, &game.font, "", 10, 0, 40, &read, line));
    CHECK(strcmp(calls, "K ") == 0);

    calls[0] = '\0';
    cok_camp_quit(&game);
    CHECK(strcmp(calls, "Q ") == 0 && game.quit && game.vm.abort);
    game.quit = false;

    /* The title: its music first, its pictures read, its end; then the
     * driver hooked, and the title's menu (no keys: the run ends). */
    calls[0] = '\0';
    keys("");
    cok_start_game(&game, false);
    CHECK(strncmp(calls, "M R ", 4) == 0 && count(calls, "R ") == 4);
    CHECK(strcmp(calls + strlen(calls) - 4, "N I ") == 0);
    calls[0] = '\0';
    keys("");
    cok_start_game(&game, true);
    CHECK(strcmp(calls, "I ") == 0);
    game.vm.abort = false;
    game.input_ended = false;
}

/* "Loading...Please Wait" (and "PIC not found") against what the
 * original's loaders do (tests/test_loading_data.h): the screen at each
 * record read, flush and key and at the end, and what the slot holds. */
static char events[4096];
static const char *read_label;

static unsigned long long fnv(const uint8_t *p, size_t n)
{
    unsigned long long h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

static void record_screen(const char *kind)
{
    char hex[17];
    snprintf(hex, sizeof hex, "%016llx", fnv(game.screen.pixels, game.screen.frame_size));
    size_t used = strlen(events);
    snprintf(events + used, sizeof events - used, "%s%s:%.10s", used ? " " : "",
             strcmp(kind, "read") == 0 ? read_label : kind, hex);
}

static void loading_load(const char *kind, unsigned id)
{
    static char label[64];
    const char *name = strcmp(kind, "ecl") == 0 ? "ECL" : strcmp(kind, "pic") == 0 ? "PIC" : "SPRIT";
    snprintf(label, sizeof label, "read %s%u.dax %u", name, game.vm.file, id);
    read_label = label;
    if (strcmp(kind, "ecl") == 0)
        CHECK(cok_adventure_load(&game, (uint8_t)id) == COK_ECL_OK);
    else if (strcmp(kind, "pic") == 0)
        cok_adventure_load_picture(&game, (uint8_t)id);
    else
        cok_adventure_load_sprite(&game, (uint8_t)id);
}

/* Row 24's pixels as text alone would leave them on a black row. */
static bool row24_shows(const char *text, uint8_t fg)
{
    cok_picture row = {0};
    CHECK(cok_adventure_create_picture(&game, &row, 40, 200, 1, 0) == COK_PICTURE_OK);
    cok_text_string(&row, &game.font, text, 0, 24, fg, 0);
    bool same = memcmp(row.pixels + 192 * 160, game.screen.pixels + 192 * 160, 8 * 160) == 0;
    cok_picture_free(&row);
    return same;
}

static bool row24_at_read;

/* What the game logs, as "kind: text;". */
static char lines[1024];

static void logged(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g, (void)context;
    size_t used = strlen(lines);
    snprintf(lines + used, sizeof lines - used, "%s: %s;", kind, text);
}

static void note_row24(const char *kind)
{
    if (strcmp(kind, "read") == 0) row24_at_read = row24_shows("Loading...Please Wait", 10);
}

/* The cases of tests/test_loading_data.h, in the game's display mode; in
 * CGA mode a frame's mask is told only as there (m) or not. */
static void run_loading_cases(const loading_case *cases, size_t count, bool cga)
{
    recording = record_screen;
    for (size_t n = 0; n < count; ++n) {
        char kind[16], first[64];
        unsigned file, animate, id;
        CHECK(sscanf(cases[n].script, "%15s %u %u %63s %u", kind, &file, &animate, first,
                     &id) == 5);
        cok_adventure_free_picture(&game);
        memset(game.screen.pixels, 0, game.screen.frame_size);
        game.vm.file = (uint8_t)file;
        game.animate = animate != 0;
        keys("\r\r");
        if (strcmp(first, "-") != 0) {
            for (char *p = strtok(first, "+"); p != NULL; p = strtok(NULL, "+")) {
                char *colon = strchr(p, ':');
                *colon = '\0';
                loading_load(p, (unsigned)atoi(colon + 1));
            }
        }
        cok_text_string(&game.screen, &game.font, "Row 24 was here before the load:40 cells", 0, 24, 15, 0);
        cok_text_string(&game.screen, &game.font, "Row 23 stays", 0, 23, 14, 0);
        events[0] = '\0';
        loading_load(kind, id);
        record_screen("end");
        char got[8192];
        int used = snprintf(got, sizeof got, "%s | ", events);
        if (strcmp(kind, "ecl") == 0) {
            snprintf(got + used, sizeof got - (size_t)used, "code:%.10llx",
                     fnv(game.vm.code, sizeof game.vm.code) >> 24);
        } else {
            bool none = game.picture_id == COK_ADVENTURE_NO_PICTURE && !game.picture_sprite;
            used += snprintf(got + used, sizeof got - (size_t)used, "slot:%s/%u flag:%d frames:%zu",
                             none ? "" : game.picture_sprite ? "SPRIT" : "PIC",
                             none ? 0u : game.picture_id, game.frame_unset ? 0 : 1,
                             game.frame_count);
            for (size_t i = 0; i < game.frame_count; ++i) {
                const cok_picture *p = &game.frames[i];
                used += snprintf(got + used, sizeof got - (size_t)used, " %08llx/",
                                 fnv(p->pixels, p->frame_size) >> 32);
                if (p->mask != NULL && cga)
                    used += snprintf(got + used, sizeof got - (size_t)used, "m");
                else if (p->mask != NULL)
                    used += snprintf(got + used, sizeof got - (size_t)used, "%08llx",
                                     fnv(p->mask, p->frame_size) >> 32);
                else
                    used += snprintf(got + used, sizeof got - (size_t)used, "-");
            }
        }
        if (strcmp(got, cases[n].expect) != 0) {
            fprintf(stderr, "%s%s\n  original: %s\n  port:     %s\n", cga ? "CGA: " : "",
                    cases[n].script, cases[n].expect, got);
            exit(1);
        }
    }
    recording = NULL;
}

/* Read plainly: the message over a cleared row while the record is
 * read, and the row cleared after; "PIC not found" for a record that is
 * not there, which then counts as held; startup's message. */
static void plain_loading(void);

static void test_loading(void)
{
    run_loading_cases(loading_cases,
                      sizeof loading_cases / sizeof *loading_cases, false);
    plain_loading();
    /* The same in CGA mode (--display cga), its text in CGA's colours. */
    cok_keyboard keyboard = game.keys;
    cok_adventure_hooks hooks = game.hooks;
    char saves[sizeof game.save_dir];
    snprintf(saves, sizeof saves, "%s", game.save_dir);
    cok_adventure_close(&game);
    CHECK(cok_adventure_open_display(&game, "Assets", COK_DISPLAY_CGA, &keyboard, &hooks));
    snprintf(game.save_dir, sizeof game.save_dir, "%s", saves);
    CHECK(game.screen.cga);
    run_loading_cases(loading_cases_cga,
                      sizeof loading_cases_cga / sizeof *loading_cases_cga, true);
    plain_loading();
}

static void plain_loading(void)
{

    recording = note_row24;
    game.vm.file = 1;
    game.animate = true;
    cok_adventure_free_picture(&game);
    cok_text_string(&game.screen, &game.font, "Something on row 24", 0, 24, 15, 0);
    keys("");
    row24_at_read = false;
    cok_adventure_load_picture(&game, 12);
    CHECK(row24_at_read && row24_shows("", 0) && game.frame_count == 4);
    game.animate = false;
    cok_adventure_free_picture(&game);
    cok_text_string(&game.screen, &game.font, "Something on row 24", 0, 24, 15, 0);
    row24_at_read = true;
    cok_adventure_load_picture(&game, 12);
    CHECK(!row24_at_read && row24_shows("Something on row 24", 15) && game.frame_count == 1);
    row24_at_read = false;
    CHECK(cok_adventure_load(&game, 17) == COK_ECL_OK && row24_at_read && row24_shows("", 0));
    keys("\r");
    game.hooks.log = logged;
    lines[0] = '\0';
    cok_adventure_load_picture(&game, 2);
    game.hooks.log = NULL;
    CHECK(strcmp(lines, "print: PIC not found;") == 0);
    CHECK(row24_shows("PIC not found", 14) && game.picture_id == 2 && game.frame_count == 0);
    CHECK(s.reads == 1 && s.flushes == 0);
    keys("");
    cok_adventure_load_picture(&game, 2);
    CHECK(s.reads == 0 && game.picture_id == 2);
    recording = NULL;

    /* Startup leaves it for the title to draw over (3e99:06a4). */
    memset(game.screen.pixels, 0, game.screen.frame_size);
    cok_start_startup(&game);
    CHECK(row24_shows("Loading...Please Wait", 10));
    cok_adventure_free_picture(&game);
    game.animate = true;
}

int main(void)
{
    cok_keyboard k = {scripted, &s, NULL, NULL};
    cok_adventure_hooks hooks = {.flush = flushed, .sound = sounded, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &k, &hooks));
    char dir[] = "/tmp/cok-hooks-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
    test_loads();
    test_waits();
    test_saved_game();
    test_animating();
    test_sounds();
    test_loading();
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
