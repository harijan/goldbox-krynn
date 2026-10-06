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
    CHECK(cok_picture_create(&game.screen, 40, 200, 1, 0) == COK_PICTURE_OK);
    cok_picture *tiles = &game.view.tiles[4];
    CHECK(cok_picture_create(tiles, 1, 8, 0x28, 0) == COK_PICTURE_OK);
    for (size_t t = 0; t < 0x28; ++t)
        memset(tiles->pixels + t * tiles->frame_size, (int)(t % 16 * 0x11), tiles->frame_size);
    return &game;
}

static void free_game(cok_adventure *game)
{
    cok_picture_free(&game->screen);
    cok_view_free(&game->view);
}

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[4096];
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

/* Play the test block from x, y facing north with keys, in a 3D area whose
 * map has walls only where the test sets them. */
static void play(cok_adventure *game, script *s, const uint8_t *map, int x, int y,
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
    CHECK(cok_adventure_play(game) == COK_ECL_OK);
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
    /* Encamp runs the camp vector; the camp menu is not ported. */
    play(&game, &s, map, 5, 5, "eavc");
    CHECK(strcmp(s.log, "unported: Encamp;unported: Area;unported: View;unported: Cast;") == 0);
    CHECK(game.vm.mem4b00[0x102] == 1);

    /* An after-move vector that sets 0x7ec9 to 0xff stops the step, and
     * 0x7ec9 is cleared. */
    load_block(&game, 0xff);
    play(&game, &s, map, 5, 5, "m\x01H");
    CHECK(strcmp(s.log, "") == 0 && game.vm.map_y == 5 && game.vm.mem7c00[0x2c9] == 0);

    /* Outside 3D areas the loop stops. */
    s.log[0] = '\0';
    game.vm.mode = 3;
    CHECK(cok_adventure_play(&game) == COK_ECL_OK);
    CHECK(strcmp(s.log, "unported: travel outside 3D areas;") == 0);
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
    cok_adventure_pass_time(game, 5, 1);
    CHECK(clock[5] == 0 && clock[6] == 256);
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

int main(void)
{
    test_clock();
    test_play();
    test_party();
    test_doors();
    test_load_stats();
    puts("adventure tests passed");
    return 0;
}
