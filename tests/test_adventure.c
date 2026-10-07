#define _POSIX_C_SOURCE 200809L /* mkdtemp */

#include "adventure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Colour of the top-left pixel of cell x, y. */
static unsigned cell(const cok_picture *p, int x, int y)
{
    return p->pixels[(size_t)y * 8 * p->units * 4 + (size_t)x * 4] >> 4;
}

/* A game with a screen and a frame tile set whose tile t is colour t % 16,
 * without the assets cok_adventure_open loads. */
static cok_adventure *game_with_tiles(void)
{
    static cok_adventure game;
    memset(&game, 0, sizeof game);
    cok_effects_init(&game.effects, &game.vm, &game.party, &game.item_types);
    CHECK(cok_picture_create(&game.screen, 40, 200, 1, 0) == COK_PICTURE_OK);
    cok_picture *tiles = &game.view.tiles[4];
    CHECK(cok_picture_create(tiles, 1, 8, 0x28, 0) == COK_PICTURE_OK);
    for (size_t t = 0; t < 0x28; ++t)
        memset(tiles->pixels + t * tiles->frame_size, (int)(t % 16 * 0x11), tiles->frame_size);
    return &game;
}

static void free_game(cok_adventure *game)
{
    cok_party_free(&game->party);
    cok_effects_free(&game->effects);
    cok_picture_free(&game->screen);
    cok_view_free(&game->view);
}

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[4096];
    unsigned waited;      /* The last delay, in ms. */
    unsigned yellow;      /* Yellow pixels on row 24 during it. */
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

/* Log lines other than the menus, as "kind: text;". */
static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    if (strcmp(kind, "menu") == 0 && (strcmp(text, "Exit") == 0 ||
                                      strncmp(text, "Move ", 5) == 0))
        return;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

/* A block whose vectors exit, except that the after-move vector saves
 * after_move to 0x7ec9, the location vector copies 0x7eca to 0x4c01, and
 * the camp vector saves 1 to 0x4c02. */
static void load_block(cok_adventure *game, uint8_t after_move)
{
    static const uint8_t vectors[] = {
        0, 0, /* Skipped. */
        1, 2, 0x15, 0x80, 1, 2, 0x1d, 0x80, 1, 2, 0x25, 0x80, 1, 2, 0x2c, 0x80, 1, 2, 0x14, 0x80,
        COK_ECL_EXIT,
    };
    uint8_t record[64] = {0};
    memcpy(record, vectors, sizeof vectors);
    const uint8_t code[] = {
        COK_ECL_SAVE, 0, after_move, 1, 0xc9, 0x7e, COK_ECL_EXIT, COK_ECL_EXIT,
        COK_ECL_SAVE, 1, 0xca, 0x7e, 1, 0x01, 0x4c, COK_ECL_EXIT,
        COK_ECL_SAVE, 0, 1, 1, 0x02, 0x4c, COK_ECL_EXIT,
        COK_ECL_EXIT,
    };
    memcpy(record + 2 + 0x15, code, sizeof code);
    CHECK(cok_ecl_load(&game->vm, record, sizeof record) == COK_ECL_OK);
    CHECK(cok_ecl_start(&game->vm, true) == COK_ECL_OK);
}

/* A block whose after-move vector moves the party off the map, to square
 * 16, and stops the step; the other vectors exit. */
static void load_block_off_map(cok_adventure *game)
{
    static const uint8_t record[] = {
        0, 0, /* Skipped. */
        1, 2, 0x15, 0x80, 1, 2, 0x14, 0x80, 1, 2, 0x14, 0x80, 1, 2, 0x14, 0x80, 1, 2, 0x14, 0x80,
        COK_ECL_EXIT,
        COK_ECL_SAVE, 0, 0xff, 1, 0xc9, 0x7e, COK_ECL_SAVE, 0, 16, 1, 0x4b, 0xc0, COK_ECL_EXIT,
    };
    CHECK(cok_ecl_load(&game->vm, record, sizeof record) == COK_ECL_OK);
    CHECK(cok_ecl_start(&game->vm, true) == COK_ECL_OK);
}

/* A block whose location vector sets the sky, 0x4bfd, to 8 (black), as
 * ECL2 block 48's does at 15:00 without showing the view again; the other
 * vectors exit. */
static void load_block_dusk(cok_adventure *game)
{
    static const uint8_t record[] = {
        0, 0, /* Skipped. */
        1, 2, 0x14, 0x80, 1, 2, 0x15, 0x80, 1, 2, 0x14, 0x80, 1, 2, 0x14, 0x80, 1, 2, 0x14, 0x80,
        COK_ECL_EXIT, COK_ECL_SAVE, 0, 8, 1, 0xfd, 0x4b, COK_ECL_EXIT,
    };
    CHECK(cok_ecl_load(&game->vm, record, sizeof record) == COK_ECL_OK);
    CHECK(cok_ecl_start(&game->vm, true) == COK_ECL_OK);
}

/* Play the test block from x, y facing north with keys, in a 3D area whose
 * map has walls only where the test sets them; return how it ended. */
static cok_ecl_status play_to(cok_adventure *game, script *s, const uint8_t *map, int x, int y,
                              const char *keys)
{
    s->keys = keys;
    s->at = 0;
    s->length = strlen(keys);
    s->log[0] = '\0';
    game->input_ended = false;
    game->vm.abort = false;
    game->vm.mode = game->vm.last_mode = 4;
    game->vm.mem4b00[0xe6] = 1;
    game->vm.map_x = (int8_t)x;
    game->vm.map_y = (int8_t)y;
    game->vm.direction = 0;
    game->view.wrap = true;
    CHECK(cok_view_set_map(&game->view, map, 0x402));
    return cok_adventure_play(game);
}

static void play(cok_adventure *game, script *s, const uint8_t *map, int x, int y,
                 const char *keys)
{
    CHECK(play_to(game, s, map, x, y, keys) == COK_ECL_OK);
    CHECK(game->input_ended);
}

static void test_play(void)
{
    static cok_adventure game;
    script s = {0};
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    load_block(&game, 0);
    uint8_t map[0x402] = {0};
    /* Square 5, 4: a solid wall north, a locked door east. */
    map[2 + 4 * 16 + 5] = 0x11;
    map[2 + 0x300 + 4 * 16 + 5] = 0x08;

    /* Move: a step, a bump into the wall, a turn, and two tries at the
     * door: Bash fails, and Exit leaves it. The step passed a minute. */
    play(&game, &s, map, 5, 5, "m\x01H\x01H\x01M\x01H" "b\x01He");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;menu: Bash Exit;menu: Bash Exit;") == 0);
    CHECK(game.vm.mem4b00[0xc7] == 1 && game.vm.mem4b00[0xf0] == 5 &&
          game.vm.mem4b00[0xf1] == 4);
    /* The door is offered again only after another step; until then, and
     * from the start, " Exit" alone shows no menu. */
    game.door_tries[0] = false;
    play(&game, &s, map, 5, 5, "m\x01H\x01M\x01H");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;menu: Bash Exit;") == 0);
    game.door_tries[0] = false;
    play(&game, &s, map, 5, 4, "m\x01M\x01H\x01P\x01K");
    CHECK(strcmp(s.log, "at: 5,4,2;at: 5,4,6;at: 5,4,4;") == 0);

    /* Searching makes a step take ten minutes; off the north edge the map
     * wraps, and 0x7ed5 marks the step. */
    game.vm.mem4b00[0xc7] = game.vm.mem4b00[0xc8] = 0;
    play(&game, &s, map, 5, 0, "sm\x01H");
    CHECK(game.vm.mem7c00[0x2ca] == 1);
    CHECK(strcmp(s.log, "at: 5,15,0;") == 0);
    CHECK(game.vm.mem7c00[0x2d5] == 1 && game.vm.mem4b00[0xc7] == 0 &&
          game.vm.mem4b00[0xc8] == 1);
    /* Look runs the location vector as if searching, takes ten minutes,
     * and leaves Search as it was. */
    game.vm.mem4b00[0xc8] = 0;
    play(&game, &s, map, 5, 5, "l");
    CHECK(game.vm.mem4b00[0x101] == 1 && game.vm.mem7c00[0x2ca] == 1 &&
          game.vm.mem4b00[0xc8] == 1);
    play(&game, &s, map, 5, 5, "sl");
    CHECK(game.vm.mem4b00[0x101] == 1 && game.vm.mem7c00[0x2ca] == 0);
    play(&game, &s, map, 5, 5, "m\x01H");
    CHECK(game.vm.mem4b00[0x101] == 0);
    /* Encamp runs the camp vector, then the camp menu until Exit (see
     * test_camp.c). */
    game.effects.rolls.saved = 1;
    play(&game, &s, map, 5, 5, "eea");
    /* The game counts as saved only in the camp it was saved in (DS:5885,
     * cleared at 2fd3:344d). */
    CHECK(game.effects.rolls.saved == 0);
    CHECK(strcmp(s.log, "print: The party makes camp...;menu: Save View Magic Rest Alter Fix Exit;"
                        "area: on;") == 0);
    game.overhead = false;
    CHECK(game.vm.mem4b00[0x102] == 1 && game.vm.mode == 4);

    /* Cast from the commands: a knight's Strength adds the low byte of
     * 5b04:1415's BP there, 0x74, so the effect's value is 0x74 + 100
     * (see test_cast.c). */
    cok_character *knight = calloc(1, sizeof *knight);
    CHECK(knight != NULL);
    memcpy(knight->record, "\x03SIR", 4);
    for (size_t i = 0; i < 6; ++i) knight->record[0x10 + 2 * i] = knight->record[0x11 + 2 * i] = 12;
    knight->record[0x10] = knight->record[0x11] = 3;
    knight->record[0x197] = knight->record[0x62] = 9;
    knight->record[0x189] = 1;
    knight->record[0x5a] = 6;
    knight->record[0x100] = 9;
    knight->record[0x1e] = 0x23;
    CHECK(cok_party_add(&game.party, knight));
    game.vm.mem7c00[0x33e] = 1;
    game.vm.character = knight->record;
    play(&game, &s, map, 5, 5, "c\rS");
    const cok_effect *strength = cok_character_find_effect(knight, 0x26);
    CHECK(strength != NULL && strength->value == 0xd8);
    cok_party_free(&game.party);
    game.vm.character = NULL;
    game.vm.mem7c00[0x33e] = 0;

    /* An after-move vector that sets 0x7ec9 to 0xff stops the step, and
     * 0x7ec9 is cleared. */
    load_block(&game, 0xff);
    play(&game, &s, map, 5, 5, "m\x01H");
    CHECK(strcmp(s.log, "") == 0 && game.vm.map_y == 5 && game.vm.mem7c00[0x2c9] == 0);

    /* Outside 3D areas without the overland menu (0x4cf7), and in any
     * other mode, the original's command is a byte it never set: the run
     * stops. */
    s.log[0] = '\0';
    game.vm.mode = 3;
    CHECK(cok_adventure_play(&game) == COK_ECL_UNDEFINED);
    CHECK(strstr(game.error, "475c:09ec") != NULL && strstr(game.error, "0x4cf7") != NULL);
    game.vm.mode = 1;
    CHECK(cok_adventure_play(&game) == COK_ECL_UNDEFINED);
    cok_adventure_close(&game);
}

/* A delay: how long, and the yellow pixels on row 24 meanwhile. */
static void delayed(cok_adventure *game, unsigned ms, void *context)
{
    script *s = context;
    s->waited = ms;
    s->yellow = 0;
    for (int x = 0; x < 320; ++x)
        for (int y = 24 * 8; y < 200; ++y) {
            uint8_t byte = game->screen.pixels[(size_t)y * 160 + (size_t)x / 2];
            if ((x % 2 == 0 ? byte >> 4 : byte & 15) == 14) ++s->yellow;
        }
}

static unsigned pixel_at(const cok_picture *p, int x, int y)
{
    uint8_t byte = p->pixels[(size_t)y * p->units * 4 + (size_t)x / 2];
    return x % 2 == 0 ? byte >> 4 : byte & 15u;
}

/* In the frame's tiles, the overhead map's party facing north has black at
 * pixel 4, 0 of its cell and facing east at 0, 2; a square is dark grey
 * there, light grey along a side with a wall. */
static bool arrow(const cok_picture *p, int x, int y, unsigned dir)
{
    return dir == 0 ? pixel_at(p, x * 8 + 4, y * 8) == 0 && pixel_at(p, x * 8, y * 8 + 2) == 8
                    : pixel_at(p, x * 8 + 4, y * 8) == 8 && pixel_at(p, x * 8, y * 8 + 2) == 0;
}

static void test_area(void)
{
    static cok_adventure game;
    script s = {0};
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .delay = delayed, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    load_block(&game, 0);
    uint8_t map[0x402] = {0};
    /* Square 5, 4 has a wall north. */
    map[2 + 4 * 16 + 5] = 0x10;

    /* Area turns the overhead map on in the view's place, the window from
     * square 0, 0 and the party at cell 8, 8; the status line is not
     * redrawn, so it keeps the time it showed. */
    game.vm.mode = 4;
    game.vm.map_x = game.vm.map_y = 5;
    cok_adventure_status(&game);
    uint8_t status[8 * 160];
    memcpy(status, game.screen.pixels + 15 * 8 * 160, sizeof status);
    game.vm.mem4b00[0xc8] = 3;
    play(&game, &s, map, 5, 5, "a");
    game.vm.mem4b00[0xc8] = 0;
    CHECK(strcmp(s.log, "area: on;") == 0 && game.overhead);
    CHECK(arrow(&game.screen, 8, 8, 0) && pixel_at(&game.screen, 8 * 8, 7 * 8) == 7 &&
          pixel_at(&game.screen, 8 * 8, 7 * 8 + 1) == 8 && pixel_at(&game.screen, 24, 24) == 8);
    CHECK(memcmp(status, game.screen.pixels + 15 * 8 * 160, sizeof status) == 0);
    /* It stays on through steps and turns. */
    play(&game, &s, map, 5, 5, "m\x01H\x01M");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;") == 0);
    CHECK(arrow(&game.screen, 8, 7, 2) && !arrow(&game.screen, 8, 8, 0));
    /* Area again shows the view: the sky (0x4bfd 0, black) at cell 3, 3. */
    play(&game, &s, map, 5, 5, "a");
    CHECK(strcmp(s.log, "area: off;") == 0 && !game.overhead);
    CHECK(pixel_at(&game.screen, 24, 24) == 0);

    /* Where the area hides the square (0x4bfb), Area says "Not Here" in
     * yellow on row 24 for speed * 100 ms, then clears the row. */
    game.vm.mem4b00[0xfb] = 1;
    play(&game, &s, map, 5, 5, "a");
    CHECK(strcmp(s.log, "print: Not Here;") == 0 && !game.overhead);
    CHECK(s.waited == game.speed * 100u && s.yellow > 0);
    delayed(&game, 0, &s);
    CHECK(s.yellow == 0 && pixel_at(&game.screen, 24, 24) == 0);
    /* Started with Helm, it goes on there all the same, and a turn keeps it,
     * but the next step's view (6945:00ba) turns it off. */
    game.helm = true;
    play(&game, &s, map, 5, 5, "am\x01M");
    CHECK(strcmp(s.log, "area: on;at: 5,5,2;") == 0 && arrow(&game.screen, 8, 8, 2));
    play(&game, &s, map, 5, 5, "m\x01H");
    CHECK(strcmp(s.log, "at: 5,4,0;area: off;") == 0 && !game.overhead);
    CHECK(pixel_at(&game.screen, 24, 24) == 0);
    /* Without Helm, a map turned on before the square was hidden stays on:
     * Area says "Not Here" and a turn keeps it. */
    game.vm.mem4b00[0xfb] = 0;
    play(&game, &s, map, 5, 5, "a");
    game.helm = false;
    game.vm.mem4b00[0xfb] = 1;
    play(&game, &s, map, 5, 5, "am\x01M");
    CHECK(strcmp(s.log, "print: Not Here;at: 5,5,2;") == 0 && game.overhead);
    CHECK(arrow(&game.screen, 8, 8, 2));
    game.vm.mem4b00[0xfb] = 0;

    /* The first sprite of an encounter turns it off (3775:0575) and shows
     * the view again; a map already off draws nothing. */
    cok_adventure_overhead_off(&game);
    CHECK(strcmp(s.log, "print: Not Here;at: 5,5,2;area: off;") == 0 && !game.overhead);
    CHECK(pixel_at(&game.screen, 24, 24) == 0);
    cok_picture_fill(&game.screen, 3, 24, 11, 88, 5);
    cok_adventure_overhead_off(&game);
    CHECK(pixel_at(&game.screen, 24, 24) == 5);
    /* Outside 3D areas, with DS:713a set, the big picture's frame shows. */
    game.overhead = true;
    game.vm.mem4b00[0xe6] = 0;
    cok_adventure_overhead_off(&game);
    CHECK(!game.overhead && pixel_at(&game.screen, 24, 24) != 5 && !game.redraw);
    game.vm.mem4b00[0xe6] = 1;

    /* A turn and Area keep the sky the step's view picked (DS:6d80), with
     * the sun of a sky of colour 11 (at cell 10, 5 facing east at 3:00),
     * though the location vector then set 0x4bfd to 8, black. */
    game.overhead = false;
    load_block_dusk(&game);
    game.vm.mem4b00[0xc9] = 3;
    game.vm.mem4b00[0xfd] = 11;
    play(&game, &s, map, 5, 5, "m\x01H\x01M");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;") == 0 && game.vm.mem4b00[0xfd] == 8);
    CHECK(game.sky == 11 && pixel_at(&game.screen, 24, 24) == 11);
    unsigned sun = 0;
    for (int y = 40; y < 48; ++y)
        for (int x = 80; x < 88; ++x) sun += pixel_at(&game.screen, x, y) != 11;
    CHECK(sun > 0);
    /* The step's view picks the sky with the map on too. */
    game.vm.mem4b00[0xfd] = 8;
    cok_adventure_view(&game);
    CHECK(game.sky == 0);
    game.vm.mem4b00[0xfd] = 11;
    play(&game, &s, map, 5, 5, "am\x01He" "a");
    CHECK(strcmp(s.log, "area: on;at: 5,4,0;area: off;") == 0 && game.vm.mem4b00[0xfd] == 8);
    CHECK(game.sky == 11 && pixel_at(&game.screen, 24, 24) == 11);
    /* The next view takes the new sky. */
    game.vm.mem4b00[0xfd] = 8;
    cok_adventure_view(&game);
    CHECK(game.sky == 0 && pixel_at(&game.screen, 24, 24) == 0);
    game.vm.mem4b00[0xc9] = 0;
    game.vm.mem4b00[0xfd] = 0;
    load_block(&game, 0);

    /* For a square off the map, the original draws the party outside the
     * map's window; the run stops, there at a turn, or after a step that
     * the after-move vector stopped. */
    game.overhead = true;
    game.vm.map_x = 16;
    cok_adventure_view(&game);
    CHECK(game.vm.status == COK_ECL_UNDEFINED && game.vm.abort &&
          strstr(game.error, "69ea:000f") != NULL);
    CHECK(play_to(&game, &s, map, 16, 5, "m\x01M\x01H") == COK_ECL_UNDEFINED && s.at == 3);
    load_block_off_map(&game);
    CHECK(play_to(&game, &s, map, 5, 5, "m\x01Hm") == COK_ECL_UNDEFINED && s.at == 3);
    CHECK(game.vm.map_x == 16 && game.vm.map_y == 5);
    /* A later play starts with no failure left over: on the overland map
     * it waits for a command. */
    game.vm.abort = false;
    game.vm.mode = 3;
    game.vm.mem4b00[0x1f7] = 1;
    s.at = s.length = 0;
    CHECK(cok_adventure_play(&game) == COK_ECL_OK && game.input_ended);
    cok_adventure_close(&game);
}

static void test_clock(void)
{
    cok_adventure *game = game_with_tiles();
    uint16_t *clock = &game->vm.mem4b00[0xc6];
    cok_adventure_pass_time(game, 1, 3);
    CHECK(clock[1] == 3 && clock[0] == 0 && clock[2] == 0);
    /* 23:59 and a minute is a new day; each moon counts the day. */
    clock[1] = 9;
    clock[2] = 5;
    clock[3] = 23;
    cok_adventure_pass_time(game, 1, 1);
    CHECK(clock[1] == 0 && clock[2] == 0 && clock[3] == 0 && clock[4] == 1);
    const uint16_t *moons = &game->vm.mem4b00[0x1f9];
    CHECK(moons[0] == 0 && moons[1] == 0 && moons[2] == 0);
    CHECK(moons[3] == 1 && moons[4] == 1 && moons[5] == 1);
    CHECK(cell(&game->screen, 19, 0) == 0);
    /* The second moon changes phase after a day, and is redrawn with tile
     * 0x14 + 6 + its phase. */
    cok_adventure_pass_time(game, 3, 24);
    CHECK(clock[3] == 0 && clock[4] == 2);
    CHECK(moons[1] == 1 && moons[4] == 0 && moons[3] == 2);
    CHECK(cell(&game->screen, 19, 0) == (0x14 + 6 + 1) % 16);
    CHECK(cell(&game->screen, 8, 0) == 0);
    /* Phases run 0-3. */
    game->vm.mem4b00[0x1fb] = 3;
    game->vm.mem4b00[0x1fe] = 6;
    cok_adventure_pass_time(game, 4, 0);
    CHECK(moons[2] == 3);
    cok_adventure_pass_time(game, 3, 24);
    CHECK(moons[2] == 0 && moons[5] == 0 && cell(&game->screen, 30, 0) == (0x14 + 14) % 16);
    /* Each step carries once: a unit far over its limit stays over it. */
    clock[0] = 25;
    cok_adventure_pass_time(game, 0, 1);
    CHECK(clock[0] == 16 && clock[1] == 1);
    /* A full last unit stays full. */
    clock[5] = 11;
    clock[6] = 255;
    CHECK(cok_adventure_pass_time(game, 5, 1));
    CHECK(clock[5] == 0 && clock[6] == 256);
    /* With 256 years, each unit that passes ages the party a year (+0x60);
     * the year before did not. */
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL && cok_party_add(&game->party, c));
    c->record[0x60] = 0xff;
    CHECK(cok_adventure_pass_time(game, 1, 2) && c->record[0x60] == 1 && c->record[0x61] == 1);
    clock[6] = 0;
    /* The party's spell effects lose the time that passes, and end. */
    cok_effect *e = cok_character_add_effect(c, 0x13, 15, 0, false);
    CHECK(e != NULL && cok_adventure_pass_time(game, 1, 5) && e->duration == 10);
    CHECK(cok_adventure_pass_time(game, 2, 1) && c->effects == NULL);
    /* One whose end is not ported stops the game. */
    CHECK(cok_character_add_effect(c, 0x1a, 1, 0xff, true) != NULL);
    CHECK(!cok_adventure_pass_time(game, 1, 1) && game->vm.status == COK_ECL_EFFECT_FAILED);
    CHECK(strstr(game->error, "3f44:09e8") != NULL && game->vm.abort);
    free_game(game);
}

/* A character who can act, with hit points hp, AC 5 and a level in
 * class. */
static cok_character *member(const char *name, uint8_t hp, size_t class)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    c->record[0x197] = c->record[0x62] = hp;
    c->record[0x18d] = 55;
    c->record[0x189] = 1;
    c->record[0xf9 + class] = 1;
    return c;
}

/* Run code from 0x8000 with keys, as a vector runs, keeping the selection
 * for EXIT to restore. */
static cok_ecl_status run_code(cok_adventure *game, script *s, const uint8_t *code, size_t size,
                               const char *keys)
{
    s->keys = keys;
    s->at = 0;
    s->length = strlen(keys);
    s->log[0] = '\0';
    game->input_ended = false;
    game->vm.abort = false;
    memset(game->vm.code, 0, sizeof game->vm.code);
    memcpy(game->vm.code, code, size);
    game->vm.saved_character = game->vm.character;
    return cok_ecl_run(&game->vm, COK_ECL_BASE);
}

/* A seed whose second Random number makes a d20 of roll. */
static uint32_t second_d20(unsigned roll)
{
    for (uint32_t seed = 1;; ++seed) {
        uint32_t t = seed;
        cok_tp_random(&t, 20);
        if (cok_tp_random(&t, 20) + 1u == roll) return seed;
    }
}

static uint32_t experience(const uint8_t *c)
{
    return (uint32_t)c[0x116] | (uint32_t)c[0x117] << 8 | (uint32_t)c[0x118] << 16 | (uint32_t)c[0x119] << 24;
}

/* The colour of a cell's set pixels, or 0 if none are set. */
static unsigned ink(const cok_picture *p, int x, int y)
{
    for (size_t i = 0; i < 8 * 4; ++i) {
        uint8_t pair = p->pixels[((size_t)y * 8 + i / 4) * p->units * 4 + (size_t)x * 4 + i % 4];
        if (pair >> 4) return pair >> 4;
        if (pair & 15) return pair & 15;
    }
    return 0;
}

static void test_party(void)
{
    static cok_adventure game;
    script s = {0};
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    cok_ecl *vm = &game.vm;
    vm->mode = vm->last_mode = 4;
    vm->mem4b00[0xe6] = 1;
    cok_character *a = member("AL", 20, 2), *b = member("BO", 20, 5), *c = member("CY", 20, 6);
    b->record[0xf9] = 3; /* two classes */
    c->record[0x189] = 0;
    CHECK(cok_party_add(&game.party, a) && cok_party_add(&game.party, b) &&
          cok_party_add(&game.party, c));
    vm->mem7c00[0x33e] = 3;
    vm->character = a->record;

    /* LOAD CHARACTER selects by position until EXIT; past the end the
     * selection stays and 0x7d00 reads 0. */
    const uint8_t load[] = {
        COK_ECL_LOAD_CHARACTER, 0, 1,
        COK_ECL_SAVE, 1, 0xb1, 0x7e, 2, 0x00, 0x4c,
        COK_ECL_LOAD_CHARACTER, 0, 7,
        COK_ECL_SAVE, 1, 0x00, 0x7d, 2, 0x01, 0x4c,
        COK_ECL_SAVE, 1, 0xb4, 0x7e, 2, 0x02, 0x4c,
        COK_ECL_EXIT,
    };
    vm->mem4b00[0x101] = 9;
    CHECK(run_code(&game, &s, load, sizeof load, "") == COK_ECL_OK);
    CHECK(vm->mem4b00[0x100] == 1 && vm->mem4b00[0x101] == 0 && vm->mem4b00[0x102] == 1);
    CHECK(vm->character == a->record);

    /* ADD EP: the selected character's points are shared among its
     * classes; for the party, only members who can act gain them. */
    const uint8_t add[] = {
        COK_ECL_LOAD_CHARACTER, 0, 1,
        COK_ECL_ADD_EP, 0, 0, 2, 0xb8, 0x0b,
        COK_ECL_ADD_EP, 0, 1, 2, 0xe8, 0x03,
        COK_ECL_EXIT,
    };
    CHECK(run_code(&game, &s, add, sizeof add, "") == COK_ECL_OK);
    CHECK(experience(a->record) == 1000 && experience(b->record) == 1500 + 500 &&
          experience(c->record) == 0);
    CHECK(strcmp(s.log, "print: Congratulations BO gains experience!;"
                        "print: Congratulations the party gains experience!;") == 0);
    /* A character with no class divides by zero. */
    a->record[0xfb] = 0;
    const uint8_t none[] = {COK_ECL_ADD_EP, 0, 0, 0, 10, COK_ECL_EXIT};
    CHECK(run_code(&game, &s, none, sizeof none, "") == COK_ECL_DIVIDE_BY_ZERO);
    a->record[0xfb] = 1;
    /* So does one that cannot act: the division comes first (2fd3:378a). */
    c->record[0xff] = 0;
    const uint8_t all[] = {COK_ECL_ADD_EP, 0, 1, 0, 10, COK_ECL_EXIT};
    CHECK(run_code(&game, &s, all, sizeof all, "") == COK_ECL_DIVIDE_BY_ZERO);
    c->record[0xff] = 1;

    /* WHO: down twice wraps to the first, up to the last; S picks. The
     * picked character stays selected, drawn white in the list. */
    const uint8_t who[] = {COK_ECL_WHO, 0x80, 0, COK_ECL_EXIT};
    CHECK(run_code(&game, &s, who, sizeof who, "\x01P\x01P\x01P\x01H\x01Hs") == COK_ECL_OK);
    CHECK(vm->character == b->record);
    CHECK(strcmp(s.log, "menu: Select;menu: Select;menu: Select;menu: Select;menu: Select;"
                        "menu: Select;who: BO;") == 0);
    CHECK(ink(&game.screen, 0x11, 5) == 15 && ink(&game.screen, 0x11, 4) == 0x0b &&
          ink(&game.screen, 0x11, 6) == 0x0c);
    /* Escape does not leave it. */
    CHECK(run_code(&game, &s, who, sizeof who, "\x1b\x01Hs") == COK_ECL_OK);
    CHECK(vm->character == a->record);

    /* A saving throw against DAMAGE runs the character's effects: 0x63
     * makes throws of type 0 (fifth operand 0x81: the selected character,
     * throw 1 - 1). A d20 of 1 fails before them, so the seed gives 10,
     * after the roll for a target. */
    a->record[0xd0] = 20;
    cok_effect *immune = cok_character_add_effect(a, 0x63, 0, 0xff, false);
    CHECK(immune != NULL);
    const uint8_t save[] = {COK_ECL_DAMAGE, 0, 0x80, 0, 0, 0, 0, 0, 4, 0, 0x81, COK_ECL_EXIT};
    vm->seed = second_d20(10);
    CHECK(run_code(&game, &s, save, sizeof save, "\r") == COK_ECL_OK && a->record[0x197] == 20);
    CHECK(game.effects.rolls.save_roll == 100 && game.effects.rolls.save_type == 0);
    /* An effect that is not ported stops the run: constitution 2 makes
     * 0x5e add an uninitialized local. */
    CHECK(cok_effects_remove(&game.effects, a, immune, 0x63));
    CHECK(cok_character_add_effect(a, 0x5e, 0, 0xff, false) != NULL);
    a->record[0x19] = 2;
    vm->seed = second_d20(10);
    CHECK(run_code(&game, &s, save, sizeof save, "\r") == COK_ECL_EFFECT_FAILED);
    CHECK(strstr(s.log, "error: effect 0x5e (3f44:32a8)") != NULL && a->record[0x197] == 20);
    CHECK(cok_effects_remove(&game.effects, a, NULL, 0x5e));
    a->record[0xd0] = 0;
    vm->abort = false;

    /* DAMAGE without dice: to the whole party with no save, then to the
     * selected character with throw type 0, which is no save. The list
     * shows the hit points. */
    const uint8_t hurt[] = {
        COK_ECL_DAMAGE, 0, 0xe0, 0, 0, 0, 0, 0, 5, 0, 0,
        COK_ECL_DAMAGE, 0, 0x80, 0, 0, 0, 0, 0, 3, 0, 0x80,
        COK_ECL_EXIT,
    };
    CHECK(run_code(&game, &s, hurt, sizeof hurt, "\r\r") == COK_ECL_OK);
    CHECK(a->record[0x197] == 12 && b->record[0x197] == 15 && c->record[0x197] == 15);
    CHECK(strcmp(s.log, "print:   AL is hit FOR 5 points of Damage.;"
                        "print:   BO is hit FOR 5 points of Damage.;"
                        "print:   CY is hit FOR 5 points of Damage.;"
                        "print:   AL is hit FOR 3 points of Damage.;") == 0);
    CHECK(ink(&game.screen, 0x25, 4) == 0x0e && s.at == 2);
    /* The dead take no more; when no member can act, the party is killed
     * and the run ends. */
    const uint8_t kill[] = {COK_ECL_DAMAGE, 0, 0xe0, 0, 0, 0, 0, 0, 40, 0, 0,
                            COK_ECL_DAMAGE, 0, 0xe0, 0, 0, 0, 0, 0, 40, 0, 0, COK_ECL_EXIT};
    CHECK(run_code(&game, &s, kill, sizeof kill, "\r\r") == COK_ECL_OK);
    CHECK(game.party_killed && vm->abort && vm->ip == 0x800b);
    CHECK(a->record[0x188] == 6 && a->record[0x197] == 0 && a->record[0x189] == 0);
    CHECK(strstr(s.log, "AL dies.") != NULL);
    CHECK(strstr(s.log, "print: The entire party is killed!;") != NULL);

    cok_adventure_close(&game);
}

static void test_doors(void)
{
    static cok_adventure game;
    script s = {0};
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    load_block(&game, 0);
    uint8_t map[0x402] = {0};
    /* Square 5, 4: a locked door east. */
    map[2 + 4 * 16 + 5] = 0x01;
    map[2 + 0x300 + 4 * 16 + 5] = 0x08;
    cok_character *strong = member("STRONG", 10, 2), *thief = member("THIEF", 10, 6);
    strong->record[0x11] = 25;
    thief->record[0xdc] = 100;
    thief->record[0x20] = 0x1f; /* Knock memorized */
    CHECK(cok_party_add(&game.party, strong) && cok_party_add(&game.party, thief));
    game.vm.character = strong->record;

    /* A strength of 25 bashes the door open, on both of its sides. */
    play(&game, &s, map, 5, 5, "m\x01H\x01M\x01H" "b");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;menu: Bash Pick Knock Exit;at: 6,4,2;") == 0);
    CHECK(game.view.map[0x300 + 4 * 16 + 5] == 0x04 && game.view.map[0x300 + 4 * 16 + 6] == 0x40);
    /* A thief with 100% picks it. */
    strong->record[0x11] = 3;
    play(&game, &s, map, 5, 5, "m\x01H\x01M\x01H" "p");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;menu: Bash Pick Knock Exit;at: 6,4,2;") == 0);
    /* Knock takes the party through once and is forgotten; the door stays
     * locked. */
    play(&game, &s, map, 5, 5, "m\x01H\x01M\x01H" "k");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;menu: Bash Pick Knock Exit;at: 6,4,2;") == 0);
    CHECK(thief->record[0x20] == 0 && game.view.map[0x300 + 4 * 16 + 5] == 0x08);
    /* A door that cannot be picked does not offer Pick again after it is
     * chosen. */
    map[2 + 0x300 + 4 * 16 + 5] = 0x0c;
    play(&game, &s, map, 5, 5, "m\x01H\x01M\x01H" "p\x01H");
    CHECK(strcmp(s.log, "at: 5,4,0;at: 5,4,2;menu: Bash Pick Exit;menu: Bash Exit;") == 0);
    cok_adventure_close(&game);
}

/* Whether cell x, y has a pixel that is not colour 0. */
static bool inked(const cok_picture *p, int x, int y)
{
    for (int row = 0; row < 8; ++row)
        for (int b = 0; b < 4; ++b)
            if (p->pixels[((size_t)y * 8 + (size_t)row) * p->units * 4 + (size_t)x * 4 +
                          (size_t)b] != 0)
                return true;
    return false;
}

static void write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(data, 1, size, f) == size);
    fclose(f);
}

/* Loading a saved game recomputes each character's derived fields from its
 * items (6346:0d20, 66c2:0433), so the party list shows the armour class
 * the items give, not the one saved, and DAMAGE's attacks roll against it.
 * An NPC is recomputed again as it joins (4b6d:1989). */
static void test_load_stats(void)
{
    char dir[] = "/tmp/cok_adventure_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[256];
    static uint8_t save[COK_SAVED_GAME_SIZE];
    save[0x1414] = 1;
    memcpy(save + 0x1415, "\x03" "NPC", 4);
    snprintf(path, sizeof path, "%s/SAVGAMC.DAT", dir);
    write_file(path, save, sizeof save);
    uint8_t record[COK_CHARACTER_SIZE] = {3, 'N', 'P', 'C'};
    record[0xe7] = 0x80;
    record[0x113] = 50;   /* AC 10 */
    record[0x18d] = 50;   /* saved as AC 10 */
    record[0x17] = 15;    /* dexterity: 1 better */
    record[0x11] = 12;
    record[0x197] = record[0x62] = 9;
    record[0x189] = 1;
    snprintf(path, sizeof path, "%s/NPC.SAV", dir);
    write_file(path, record, sizeof record);
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = 0x24; /* armour of 0x80 + 57 */
    item[0x34] = 1;
    item[0x37] = 200;
    snprintf(path, sizeof path, "%s/NPC.STF", dir);
    write_file(path, item, sizeof item);

    static cok_adventure game;
    script s = {0};
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    snprintf(path, sizeof path, "%s/SAVGAMC.DAT", dir);
    CHECK(cok_adventure_load_party(&game, path));
    CHECK(game.party.count == 1);
    const uint8_t *c = game.party.members[0]->record;
    CHECK(c[0x18d] == 58 && c[0x18e] == 55 && c[0x198] == 9);
    CHECK(game.party.members[0]->slots[2] == 1);
    /* The party list shows AC 2 in column 34; the saved 10 would also
     * fill column 33. */
    game.vm.mode = game.vm.last_mode = 4;
    game.vm.mem4b00[0xe6] = 1;
    cok_adventure_party(&game);
    CHECK(inked(&game.screen, 0x22, 4) && !inked(&game.screen, 0x21, 4));
    cok_adventure_close(&game);

    /* A readied item of a type past ITEMS's buffer stops the load. */
    item[0x2e] = 0x90;
    snprintf(path, sizeof path, "%s/NPC.STF", dir);
    write_file(path, item, sizeof item);
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    snprintf(path, sizeof path, "%s/SAVGAMC.DAT", dir);
    CHECK(!cok_adventure_load_party(&game, path));
    CHECK(strstr(game.error, "NPC.SAV") != NULL && strstr(game.error, "item type 144") != NULL);
    cok_adventure_close(&game);

    /* A level 1 mage with a readied item of power 1 that mages cannot use
     * (type 0x44, knights only): loading doubles its first level spells,
     * then unreadies the item. Joining as an NPC recomputes the levels
     * without the item, as 4b6d:1989 does; a PC keeps the doubled spells. */
    memset(save + 0x1415, 0, 41);
    save[0x1414] = 2;
    memcpy(save + 0x1415, "\x03" "WIZ", 4);
    memcpy(save + 0x1415 + 41, "\x05" "WIZPC", 6);
    snprintf(path, sizeof path, "%s/SAVGAMD.DAT", dir);
    write_file(path, save, sizeof save);
    memset(record, 0, sizeof record);
    memcpy(record, "\x03" "WIZ", 4);
    record[0xfe] = 1;
    record[0xe7] = 0x80;
    record[0x197] = record[0x62] = 4;
    record[0x189] = 1;
    snprintf(path, sizeof path, "%s/WIZ.SAV", dir);
    write_file(path, record, sizeof record);
    record[0xe7] = 0;
    snprintf(path, sizeof path, "%s/WIZPC.SAV", dir);
    write_file(path, record, sizeof record);
    item[0x2e] = 0x44;
    item[0x3e] = 0x81;
    snprintf(path, sizeof path, "%s/WIZ.STF", dir);
    write_file(path, item, sizeof item);
    snprintf(path, sizeof path, "%s/WIZPC.STF", dir);
    write_file(path, item, sizeof item);
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    snprintf(path, sizeof path, "%s/SAVGAMD.DAT", dir);
    CHECK(cok_adventure_load_party(&game, path));
    CHECK(game.party.count == 2);
    CHECK(game.party.members[0]->record[0x12b] == 1 && game.party.members[0]->items[0][0x34] == 0);
    CHECK(game.party.members[1]->record[0x12b] == 2 && game.party.members[1]->items[0][0x34] == 0);
    cok_adventure_close(&game);

    const char *files[] = {"SAVGAMC.DAT", "NPC.SAV", "NPC.STF", "SAVGAMD.DAT",
                           "WIZ.SAV", "WIZ.STF", "WIZPC.SAV", "WIZPC.STF"};
    for (size_t i = 0; i < sizeof files / sizeof *files; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        CHECK(remove(path) == 0);
    }
    CHECK(rmdir(dir) == 0);
}

/* A block for the overland map whose after-move vector saves after_move
 * to 0x7ec9, whose location vector copies 0x7ec9 to 0x4c01, and whose camp
 * vector saves 1 to 0x4c02; then from 0x8030 a script that runs PROGRAM
 * program, saves 1 to 0x4c03 and the selected character's position
 * (0x7eb1) to 0x4c04, and exits. */
static void load_overland_block(cok_adventure *game, uint8_t after_move, uint8_t program)
{
    static const uint8_t vectors[] = {
        0, 0, /* Skipped. */
        1, 2, 0x15, 0x80, 1, 2, 0x1d, 0x80, 1, 2, 0x25, 0x80, 1, 2, 0x2c, 0x80, 1, 2, 0x14, 0x80,
        COK_ECL_EXIT,
    };
    uint8_t record[0x50] = {0};
    memcpy(record, vectors, sizeof vectors);
    const uint8_t code[] = {
        COK_ECL_SAVE, 0, after_move, 1, 0xc9, 0x7e, COK_ECL_EXIT, COK_ECL_EXIT,
        COK_ECL_SAVE, 1, 0xc9, 0x7e, 1, 0x01, 0x4c, COK_ECL_EXIT,
        COK_ECL_SAVE, 0, 1, 1, 0x02, 0x4c, COK_ECL_EXIT,
        COK_ECL_EXIT,
    };
    memcpy(record + 2 + 0x15, code, sizeof code);
    const uint8_t program_code[] = {
        COK_ECL_PROGRAM, 0, program, COK_ECL_SAVE, 0, 1, 1, 0x03, 0x4c,
        COK_ECL_SAVE, 1, 0xb1, 0x7e, 1, 0x04, 0x4c, COK_ECL_EXIT,
    };
    memcpy(record + 2 + 0x30, program_code, sizeof program_code);
    CHECK(cok_ecl_load(&game->vm, record, sizeof record) == COK_ECL_OK);
    CHECK(cok_ecl_start(&game->vm, true) == COK_ECL_OK);
}

/* Travel from x, y on the overland map facing dir with keys. */
static cok_ecl_status travel_to(cok_adventure *game, script *s, uint16_t x, uint16_t y,
                                uint8_t dir, const char *keys)
{
    s->keys = keys;
    s->at = 0;
    s->length = strlen(keys);
    s->log[0] = '\0';
    game->input_ended = false;
    game->vm.abort = false;
    game->vm.mode = game->vm.last_mode = 3;
    game->vm.mem4b00[0xe6] = 0;
    game->vm.mem4b00[0x1f7] = 1;
    game->vm.mem4b00[0xc3] = x;
    game->vm.mem4b00[0xc4] = y;
    game->vm.direction = dir;
    return cok_adventure_play(game);
}

static void travel(cok_adventure *game, script *s, uint16_t x, uint16_t y, uint8_t dir,
                   const char *keys)
{
    CHECK(travel_to(game, s, x, y, dir, keys) == COK_ECL_OK);
    CHECK(game->input_ended);
}

/* Whether cell x, y shows the overland cursor: white around a black ring
 * around yellow. */
static bool cursor_at(const cok_picture *p, int x, int y)
{
    return pixel_at(p, x * 8, y * 8) == 15 && pixel_at(p, x * 8 + 7, y * 8 + 7) == 15 &&
           pixel_at(p, x * 8 + 1, y * 8 + 1) == 0 && pixel_at(p, x * 8 + 3, y * 8 + 4) == 14;
}

/* Whether every pixel of cell x, y is colour. */
static bool cell_is(const cok_picture *p, int x, int y, unsigned colour)
{
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
            if (pixel_at(p, x * 8 + col, y * 8 + row) != colour) return false;
    return true;
}

static unsigned unported_count;

static void count_unported(cok_adventure *game, void *context)
{
    (void)game;
    (void)context;
    ++unported_count;
}

static void test_overland(void)
{
    static cok_adventure game;
    script s = {0};
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .unported = count_unported, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    uint16_t *mem = game.vm.mem4b00;
    load_overland_block(&game, 0, 0);
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 5);

    /* Before the party was ever marked, the cell put back where it stood is
     * the one saved at startup, zeroed (DS:6162): black. */
    travel(&game, &s, 2, 3, 2, "m\x01M");
    CHECK(strcmp(s.log, "overland: 3,3,2;") == 0);
    CHECK(cell_is(&game.screen, 3, 4, 0) && cursor_at(&game.screen, 4, 4));
    /* A step keeps the place it left in 0x4bf0 and 0x4bf1 and passes twelve
     * hours (57e4:0549, unit 3); the location vector runs after it. */
    CHECK(mem[0xf0] == 2 && mem[0xf1] == 3 && mem[0xc9] == 12 && mem[0xca] == 0);
    CHECK(mem[0x101] == 0);
    /* The menu stays on Exit, Move mode kept, until Exit; another step is
     * another half day. */
    travel(&game, &s, 3, 3, 2, "m\x01Me" "m\x01P");
    CHECK(strcmp(s.log, "overland: 4,3,2;overland: 4,4,4;") == 0);
    CHECK(mem[0xc9] == 12 && mem[0xca] == 1 && cell_is(&game.screen, 4, 4, 5));
    CHECK(cell_is(&game.screen, 5, 4, 5) && cursor_at(&game.screen, 5, 5) &&
          cell_is(&game.screen, 3, 4, 0));

    /* The arrows and keypad keys face the eight ways, and any other special
     * key steps the way the party faces. */
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 5);
    cok_adventure_mark(&game);
    travel(&game, &s, 10, 7, 0, "m89632147" "5\x01S");
    CHECK(strcmp(s.log, "overland: 10,6,0;overland: 11,5,1;overland: 12,5,2;overland: 13,6,3;"
                        "overland: 13,7,4;overland: 12,8,5;overland: 11,8,6;overland: 10,7,7;"
                        "overland: 9,6,7;overland: 8,5,7;") == 0);
    CHECK(cursor_at(&game.screen, 9, 6) && cell_is(&game.screen, 11, 8, 5));
    /* The way stays within 0-37 across and 0-14 down. */
    travel(&game, &s, 37, 14, 0, "m3\x01M");
    CHECK(strcmp(s.log, "overland: 37,14,3;overland: 37,14,2;") == 0);
    travel(&game, &s, 0, 0, 0, "m7");
    CHECK(strcmp(s.log, "overland: 0,0,7;") == 0);
    /* The step is added to the low byte of each word: 0x4002 is 2, and so
     * is the cell it marks, as the column is a word times four. 0xffff is
     * -1, marked in column 0, and a step west from it stops at 0. */
    cok_adventure_mark(&game);
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 5);
    mem[0xc3] = 0x4002;
    mem[0xc4] = 3;
    cok_adventure_mark(&game);
    CHECK(cursor_at(&game.screen, 3, 4));
    travel(&game, &s, 0x4002, 3, 0, "m\x01M");
    CHECK(strcmp(s.log, "overland: 3,3,2;") == 0 && mem[0xc3] == 3 && cell_is(&game.screen, 3, 4, 5));
    mem[0xc3] = 0xffff;
    cok_adventure_mark(&game);
    CHECK(cursor_at(&game.screen, 0, 4) && game.vm.status == COK_ECL_OK);
    travel(&game, &s, 0xffff, 3, 0, "m\x01K");
    CHECK(strcmp(s.log, "overland: 0,3,6;") == 0 && cursor_at(&game.screen, 1, 4));

    /* Facing 8 does not move; past 8 the step is read from the bytes after
     * the tables, the combat terrain table from DS:1ee4: 9 takes -1 and
     * terrain 1's cost, 0xff; 40 terrain 6's blocking height, 2, and
     * terrain 8's tile, 7. */
    const uint8_t facings[3] = {8, 9, 40};
    const uint16_t to[3][2] = {{5, 5}, {4, 4}, {7, 12}};
    for (size_t i = 0; i < 3; ++i) {
        mem[0xc3] = mem[0xc4] = 5;
        game.vm.direction = facings[i];
        cok_adventure_travel(&game);
        CHECK(game.vm.status == COK_ECL_OK && mem[0xc3] == to[i][0] && mem[0xc4] == to[i][1]);
    }
    /* 249 reads past DS:1ed6 + 0xff: terrain 58's tile (DS:1fcf) and
     * terrain 61's cost (DS:1fd8), 4. */
    mem[0xc3] = mem[0xc4] = 5;
    game.vm.direction = 249;
    cok_adventure_travel(&game);
    int sx = 5 + (int8_t)cok_combat_terrain[58].tile, sy = 5 + (int8_t)cok_combat_terrain[61].cost;
    CHECK(cok_combat_terrain[61].cost == 4);
    CHECK(mem[0xc3] == (uint16_t)(sx < 0 ? 0 : sx > 37 ? 37 : sx) &&
          mem[0xc4] == (uint16_t)(sy < 0 ? 0 : sy > 14 ? 14 : sy));

    /* The cell put back is the one saved at the last mark, wherever the
     * party is now: a script that moves it leaves the old mark behind. */
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 5);
    cok_picture_fill(&game.screen, 3, 4 * 8, 1, 8, 9);
    mem[0xc3] = 2;
    mem[0xc4] = 3;
    cok_adventure_mark(&game);
    travel(&game, &s, 5, 3, 2, "m\x01M");
    CHECK(cursor_at(&game.screen, 3, 4) && cell_is(&game.screen, 6, 4, 9) &&
          cursor_at(&game.screen, 7, 4));
    /* A cell off the screen, which the original reads and writes outside
     * its rows, stops the run: here the mark at column 40, and the cell put
     * back at row 25 before the step. The last cell on the screen is
     * 39, 24. */
    mem[0xc3] = 38;
    mem[0xc4] = 23;
    cok_adventure_mark(&game);
    CHECK(game.vm.status == COK_ECL_OK && cursor_at(&game.screen, 39, 24));
    mem[0xc3] = 39;
    cok_adventure_mark(&game);
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "4877:0005") != NULL);
    game.vm.status = COK_ECL_OK;
    /* The column is a word: 0xff is 0x400 bytes across, off the screen. */
    mem[0xc3] = 0xff;
    mem[0xc4] = 3;
    cok_adventure_mark(&game);
    CHECK(game.vm.status == COK_ECL_UNDEFINED);
    game.vm.status = COK_ECL_OK;
    uint16_t hours = mem[0xc9];
    CHECK(travel_to(&game, &s, 3, 24, 0, "m\x01H") == COK_ECL_UNDEFINED);
    CHECK(strncmp(s.log, "error: ", 7) == 0 && mem[0xc4] == 24 && mem[0xc9] == hours);

    /* An after-move vector that sets 0x7ec9 to 0xff stops the step; there
     * 0x7ec9 stays set through the location vector, and 0x4bf0 and 0x4bf1
     * hold the party's square in the 3D map, as the loop set them. */
    load_overland_block(&game, 0xff, 0);
    game.vm.map_x = 7;
    game.vm.map_y = 9;
    travel(&game, &s, 4, 4, 2, "m\x01M");
    CHECK(strcmp(s.log, "") == 0 && mem[0xc3] == 4 && mem[0x101] == 0xff);
    CHECK(mem[0xf0] == 7 && mem[0xf1] == 9);
    load_overland_block(&game, 0, 0);

    /* Encamp camps; so does Ctrl-F8 in Move mode, whose scan code 0x65 is
     * 'e', the way unchanged and no step taken. Then the overland map is shown
     * and the party marked. Special keys in the commands pick no one. */
    cok_character *a = member("ANN", 5, 2), *b = member("BOB", 5, 2);
    CHECK(cok_party_add(&game.party, a) && cok_party_add(&game.party, b));
    game.vm.character = a->record;
    game.vm.mem7c00[0x33e] = 2;
    mem[0x102] = 0;
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 5);
    travel(&game, &s, 6, 6, 2, "\x01P" "e" "e");
    CHECK(game.vm.character == a->record);
    CHECK(strcmp(s.log, "print: The party makes camp...;menu: Save View Magic Rest Alter Fix Exit;")
          == 0);
    CHECK(mem[0x102] == 1 && cursor_at(&game.screen, 7, 7) && game.vm.mode == 3);
    /* Encamp selects the first item for the camp menu, where Enter then
     * picks Save. */
    game.selected = 3;
    travel(&game, &s, 6, 6, 2, "e\r");
    CHECK(strstr(s.log, "menu: A B C D E F G H I J;") != NULL);
    mem[0x102] = 0;
    travel(&game, &s, 6, 6, 2, "m\x01" "ee");
    CHECK(strcmp(s.log, "print: The party makes camp...;menu: Save View Magic Rest Alter Fix Exit;")
          == 0);
    CHECK(mem[0x102] == 1 && mem[0xc3] == 6 && game.vm.direction == 2);

    /* In a 3D area the camp marks no one. */
    uint8_t map[0x402] = {0};
    load_block(&game, 0);
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 5);
    mem[0xc3] = mem[0xc4] = 1;
    play(&game, &s, map, 5, 5, "ee");
    CHECK(mem[0x102] == 1 && !cursor_at(&game.screen, 2, 2));
    game.vm.mem4b00[0xe6] = 0;
    game.vm.mode = 3;
    mem[0xc4] = 6;
    load_overland_block(&game, 0, 0);

    /* PICTURE 0x79, the overland map, marks the party on it and does not
     * count as a big picture shown (DS:4b4e); any other big picture does. */
    const uint8_t picture[] = {COK_ECL_PICTURE, 0, 0x79, COK_ECL_EXIT};
    mem[0xc3] = 2;
    CHECK(run_code(&game, &s, picture, sizeof picture, "") == COK_ECL_OK);
    CHECK(game.big_id == 0x79 && game.big.pixels != NULL && cursor_at(&game.screen, 3, 7));
    CHECK(!game.big_shown && !cursor_at(&game.screen, 7, 7));
    const uint8_t caravan[] = {COK_ECL_PICTURE, 0, 0x72, COK_ECL_EXIT};
    CHECK(run_code(&game, &s, caravan, sizeof caravan, "") == COK_ECL_OK);
    CHECK(game.big_shown && !cursor_at(&game.screen, 3, 7));
    game.big_shown = false;

    /* PROGRAM 9 camps in the middle of a script, then exits unless 0x4c38
     * is set; PROGRAM 0, the training hall, is not ported; others do
     * nothing. Each first restores the selection LOAD CHARACTER changed. */
    load_overland_block(&game, 0, 9);
    mem[0x102] = mem[0x103] = 0;
    s.keys = "e";
    s.at = 0;
    s.length = 1;
    game.vm.mode = 3;
    game.vm.character = b->record;
    game.vm.saved_character = a->record;
    game.vm.restore_character = true;
    game.moving = false;
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 5);
    CHECK(cok_ecl_run(&game.vm, 0x8030) == COK_ECL_OK);
    CHECK(mem[0x102] == 1 && mem[0x103] == 0 && game.vm.character == a->record);
    CHECK(!game.moving && cursor_at(&game.screen, 3, 7));
    mem[0x102] = 0;
    mem[0x138] = 1;
    s.at = 0;
    game.moving = true;
    CHECK(cok_ecl_run(&game.vm, 0x8030) == COK_ECL_OK);
    CHECK(mem[0x102] == 1 && mem[0x103] == 1 && game.moving);
    mem[0x138] = 0;
    load_overland_block(&game, 0, 0);
    mem[0x103] = 0;
    unported_count = 0;
    CHECK(cok_ecl_run(&game.vm, 0x8030) == COK_ECL_OK && unported_count == 1 && mem[0x103] == 1);
    load_overland_block(&game, 0, 3);
    mem[0x103] = 0;
    game.vm.character = b->record;
    game.vm.restore_character = true;
    CHECK(cok_ecl_run(&game.vm, 0x8030) == COK_ECL_OK && unported_count == 1 && mem[0x103] == 1);
    CHECK(game.vm.character == a->record && mem[0x104] == 0);
    cok_adventure_close(&game);

    /* DESTROY ITEMS 63, which block 16's location vector runs on entering
     * it and after every step, takes the tomb's Long Sword +5 (type 63) from
     * every record, unreadied first, a cursed one after "It's Cursed", and
     * recomputes the stats; other items and the selection stay. */
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    cok_character *f = member("FIG", 9, 2), *g = member("GIL", 9, 2);
    for (size_t i = 0; i < 6; ++i)
        f->record[0x10 + 2 * i] = f->record[0x11 + 2 * i] = g->record[0x10 + 2 * i] =
            g->record[0x11 + 2 * i] = 12;
    uint8_t sword[COK_ITEM_SIZE] = {0}, other[COK_ITEM_SIZE] = {0};
    sword[0x2e] = 63;
    sword[0x34] = 1;
    sword[0x37] = 60;
    other[0x2e] = 0x12;
    other[0x37] = 60;
    CHECK(cok_character_insert_item(f, 0, sword) && cok_character_insert_item(f, 1, other) &&
          cok_character_insert_item(f, 2, sword));
    /* One with a power: its effect (0x13) goes from its owner, selected for
     * it, not from the character selected before. */
    uint8_t charm[COK_ITEM_SIZE];
    memcpy(charm, sword, sizeof charm);
    charm[0x3d] = 0x13;
    charm[0x3e] = 0x80;
    CHECK(cok_character_insert_item(f, 3, charm) &&
          cok_character_add_effect(f, 0x13, 0, 0xff, false) != NULL);
    sword[0x36] = 1;
    CHECK(cok_character_insert_item(g, 0, sword));
    CHECK(cok_party_add(&game.party, f) && cok_party_add(&game.party, g));
    CHECK(cok_character_add_effect(g, 0x13, 0, 0xff, false) != NULL);
    game.vm.mem7c00[0x33e] = 2;
    game.vm.character = f->record;
    game.vm.mem4b00[0xf2] = 0x11;
    game.vm.mem4b00[0xc3] = 1;
    game.vm.mem4b00[0xc4] = 3;
    game.vm.mem4b00[0x12d] = 1;
    s.keys = "n";
    s.at = 0;
    s.length = 1;
    s.log[0] = '\0';
    unported_count = 0;
    CHECK(cok_adventure_enter(&game, 16) == COK_ECL_OK);
    CHECK(f->item_count == 1 && f->items[0][0x2e] == 0x12 && g->item_count == 0);
    CHECK(f->record[0x142] == 1 && f->record[0x17d] == 60 && game.vm.character == f->record);
    CHECK(cok_character_find_effect(f, 0x13) == NULL && cok_character_find_effect(g, 0x13) != NULL);
    CHECK(strstr(s.log, "print: It's Cursed;") != NULL && unported_count == 0);
    /* After a step on the map. */
    sword[0x36] = 0;
    CHECK(cok_character_insert_item(g, 0, sword));
    s.keys = "m\x01M";
    s.at = 0;
    s.length = 3;
    CHECK(cok_adventure_play(&game) == COK_ECL_OK && game.vm.mem4b00[0xc3] == 2);
    CHECK(g->item_count == 0 && f->item_count == 1);
    /* The opcode alone: the effect of a power goes from the item's owner,
     * selected for it, and the selection is restored after, though the
     * last owner (here GIL) was selected last. */
    CHECK(cok_character_insert_item(f, 0, charm) && cok_character_insert_item(g, 0, sword));
    CHECK(cok_character_add_effect(f, 0x13, 0, 0xff, false) != NULL);
    const uint8_t destroy[] = {COK_ECL_DESTROY_ITEMS, 0, 63, COK_ECL_EXIT};
    game.vm.character = g->record;
    CHECK(run_code(&game, &s, destroy, sizeof destroy, "") == COK_ECL_OK);
    CHECK(cok_character_find_effect(f, 0x13) == NULL && cok_character_find_effect(g, 0x13) != NULL);
    CHECK(f->item_count == 1 && g->item_count == 0);
    CHECK(cok_character_insert_item(g, 0, sword));
    game.vm.character = f->record;
    CHECK(run_code(&game, &s, destroy, sizeof destroy, "") == COK_ECL_OK);
    CHECK(game.vm.character == f->record && g->item_count == 0);
    cok_adventure_close(&game);

    /* A saved game outside 3D areas loads the overland map, not drawn. */
    char dir[] = "/tmp/cok_overland_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[256];
    static uint8_t save[COK_SAVED_GAME_SIZE];
    save[1 + 0x400 * 2 + 0x312 * 2] = 1; /* 0x7f12, the ECL file */
    snprintf(path, sizeof path, "%s/SAVGAMA.DAT", dir);
    write_file(path, save, sizeof save);
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    CHECK(cok_adventure_restore(&game, path));
    CHECK(game.big_id == 0x79 && game.big.pixels != NULL && cell_is(&game.screen, 3, 3, 0));
    cok_adventure_close(&game);
    save[1 + 0xe6 * 2] = 1; /* 0x4be6 */
    write_file(path, save, sizeof save);
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    CHECK(cok_adventure_restore(&game, path) && game.big_id == COK_ADVENTURE_NO_PICTURE);
    cok_adventure_close(&game);
    CHECK(remove(path) == 0 && rmdir(dir) == 0);
}

int main(void)
{
    test_clock();
    test_play();
    test_area();
    test_party();
    test_doors();
    test_load_stats();
    test_overland();
    puts("adventure tests passed");
    return 0;
}
