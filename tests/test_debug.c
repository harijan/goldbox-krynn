#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* The original's debug mode (debug.h): the keys 1614:025b takes in a game
 * started with Woof, debug.txt as Turbo Pascal writes it, the trace
 * (6d7e:093e) and its callers, View's item (546c:12be), and the "*" of
 * the status line and the combat menu. */
#include "adventure.h"
#include "command.h"
#include "debug.h"
#include "items.h"
#include "start.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static cok_adventure game;

/* Keys from a string, each read counted, and the screen as it was at the
 * last read. */
typedef struct {
    const char *keys;
    size_t at, length;
    unsigned reads;
    char log[16384];
    cok_picture seen;
} script;

static script s;

static int scripted(void *context)
{
    script *p = context;
    ++p->reads;
    memcpy(p->seen.pixels, game.screen.pixels, game.screen.frame_size);
    if (p->at == p->length) return -1;
    return (unsigned char)p->keys[p->at++];
}

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g;
    script *p = context;
    size_t used = strlen(p->log);
    snprintf(p->log + used, sizeof p->log - used, "%s: %s;", kind, text);
}

static void keys(const char *k)
{
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.reads = 0;
    s.log[0] = '\0';
    game.input_ended = game.quit = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
}

static char dir[] = "/tmp/cok-debug-XXXXXX";

/* DEBUG.TXT as it is on the disk, or "none". */
static const char *on_disk(void)
{
    static char text[4096];
    char path[sizeof dir + 16];
    snprintf(path, sizeof path, "%s/DEBUG.TXT", dir);
    FILE *f = fopen(path, "rb");
    if (f == NULL) return "none";
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    return text;
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

/* Whether any pixel of cell x, y has colour. */
static bool cell_has(const cok_picture *p, int x, int y, uint8_t colour)
{
    for (int row = 0; row < 8; ++row)
        for (int k = 0; k < 4; ++k) {
            uint8_t b = p->pixels[(size_t)(y * 8 + row) * p->units * 4 + (size_t)x * 4 + (size_t)k];
            if ((b >> 4) == colour || (b & 15) == colour) return true;
        }
    return false;
}

static bool cell_blank(const cok_picture *p, int x, int y)
{
    for (int row = 0; row < 8; ++row)
        for (int k = 0; k < 4; ++k)
            if (p->pixels[(size_t)(y * 8 + row) * p->units * 4 + (size_t)x * 4 + (size_t)k] != 0)
                return false;
    return true;
}

/* 1614:025b's keys: only with Woof; Ctrl-D opening and closing debug.txt,
 * Ctrl-Z writing the effects through the 128-byte buffer, the reset
 * dropping what it holds, Ctrl-C quitting. */
static void test_keys(void)
{
    cok_character *alda = member("ALDA");
    cok_character *bram = member("BRAMBLEWOOD");
    CHECK(cok_character_add_effect(alda, 0x22, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(alda, 7, 0, 0, false) != NULL);
    for (int i = 0; i < 3; ++i) CHECK(cok_character_add_effect(bram, (uint8_t)(200 + i), 0, 0, false));

    game.woof = false;
    keys("");
    CHECK(cok_debug_key(&game, 4) == 4 && !game.debug && strcmp(on_disk(), "none") == 0);
    CHECK(cok_debug_key(&game, 3) == 3 && !game.quit);

    /* Through the key reader (1614:025b), as every menu reads. */
    game.woof = true;
    keys("\x04");
    CHECK(cok_adventure_wait_key(&game) == 4 && game.debug);
    CHECK(strcmp(on_disk(), "") == 0);
    CHECK(strstr(s.log, "debug: on;") != NULL);
    /* Five lines, 123 bytes, stay in the buffer. */
    CHECK(cok_debug_key(&game, 0x1a) == 0x1a);
    CHECK(strcmp(on_disk(), "") == 0);
    CHECK(strstr(s.log, "debug.txt: who: ALDA  sp#: 34;debug.txt: who: ALDA  sp#: 7;"
                        "debug.txt: who: BRAMBLEWOOD  sp#: 200;") != NULL);
    /* Ten lines: the first 128 bytes are written. */
    CHECK(cok_debug_key(&game, 0x1a) == 0x1a);
    static const char five[] = "who: ALDA  sp#: 34\r\nwho: ALDA  sp#: 7\r\nwho: BRAMBLEWOOD  sp#: 200\r\n"
                               "who: BRAMBLEWOOD  sp#: 201\r\nwho: BRAMBLEWOOD  sp#: 202\r\n";
    CHECK(strlen(five) == 123);
    char expect[300];
    snprintf(expect, sizeof expect, "%s%s", five, five);
    CHECK(strlen(on_disk()) == 128 && strncmp(on_disk(), expect, 128) == 0);
    /* Off: closed, all of it written. */
    CHECK(cok_debug_key(&game, 4) == 4 && !game.debug);
    CHECK(strcmp(on_disk(), expect) == 0);
    CHECK(cok_debug_key(&game, 0x1a) == 0x1a && strcmp(on_disk(), expect) == 0);

    /* On again: made anew. The reset (3e99:0843) clears the flag and drops
     * the buffer; the next Ctrl-D makes the file anew. */
    CHECK(cok_debug_key(&game, 4) == 4 && game.debug && strcmp(on_disk(), "") == 0);
    cok_debug_key(&game, 0x1a);
    cok_debug_reset(&game);
    CHECK(!game.debug);
    cok_debug_key(&game, 0x1a);
    CHECK(strcmp(on_disk(), "") == 0);
    cok_debug_key(&game, 4);
    cok_debug_key(&game, 4);
    CHECK(strcmp(on_disk(), "") == 0);
    /* Main's reset after a game (3e99:0843) clears the flag. */
    cok_debug_key(&game, 4);
    cok_start_reset(&game);
    CHECK(!game.debug);

    /* Ctrl-C quits (1614:0000), closing debug.txt while the flag is set. */
    cok_debug_key(&game, 4);
    cok_debug_key(&game, 0x1a);
    keys("");
    CHECK(cok_debug_key(&game, 3) == -1 && game.quit && game.vm.abort && !game.debug_file.open);
    CHECK(strcmp(on_disk(), five) == 0);
    game.debug = false;
    /* Other keys pass. */
    CHECK(cok_debug_key(&game, 'a') == 'a' && cok_debug_key(&game, 0) == 0);

    /* With no saves directory the file is open all the same, its lines
     * logged, and nothing is made (the last file is left as it was). */
    char saves[sizeof game.save_dir];
    memcpy(saves, game.save_dir, sizeof saves);
    game.save_dir[0] = '\0';
    keys("");
    cok_debug_key(&game, 4);
    cok_debug_key(&game, 0x1a);
    CHECK(strstr(s.log, "debug.txt: who: ALDA  sp#: 34;") != NULL);
    cok_debug_key(&game, 4);
    CHECK(strcmp(on_disk(), five) == 0);
    memcpy(game.save_dir, saves, sizeof saves);
    cok_party_free(&game.party);

    /* Exactly 128 bytes, four lines of 32: written as the 128th is put. */
    cok_character *long_name = member("ABCDEFGHIJKLMNO");
    for (int i = 0; i < 4; ++i) CHECK(cok_character_add_effect(long_name, (uint8_t)(100 + i), 0, 0, false));
    cok_debug_key(&game, 4);
    cok_debug_key(&game, 0x1a);
    CHECK(strlen(on_disk()) == 128 && strncmp(on_disk(), "who: ABCDEFGHIJKLMNO  sp#: 100\r\n", 32) == 0);
    cok_debug_key(&game, 4);
    CHECK(strlen(on_disk()) == 128);
    cok_party_free(&game.party);

    /* A name is written whole, past the 15 characters screens show. */
    cok_character *longer = member("ABCDEFGHIJKLMNOPQRST");
    CHECK(cok_character_add_effect(longer, 9, 0, 0, false) != NULL);
    keys("");
    cok_debug_key(&game, 4);
    cok_debug_key(&game, 0x1a);
    cok_debug_key(&game, 4);
    CHECK(strcmp(on_disk(), "who: ABCDEFGHIJKLMNOPQRST  sp#: 9\r\n") == 0);
    cok_party_free(&game.party);
}

/* The trace: nothing without the flag; with it the numbers on row 0 and
 * the text on row 24 in yellow while a key is read, then row 0 cleared. */
static void test_trace(void)
{
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 3);
    game.debug = false;
    keys("x");
    cok_debug_trace(&game, "GOTO", 1, 2, 3);
    CHECK(s.reads == 0 && cell_has(&game.screen, 0, 0, 3));
    game.debug = true;
    keys("x");
    cok_debug_trace(&game, "0123456789012345678901234567890123456789XYZ", 255, 0, 17);
    CHECK(s.reads == 1);
    CHECK(strstr(s.log, "debug: 255 , 0 , 17 | 0123456789012345678901234567890123456789;"));
    /* "255 , 0 , 17": 12 cells, then cleared. */
    CHECK(cell_has(&s.seen, 0, 0, 14) && cell_has(&s.seen, 11, 0, 14) && cell_blank(&s.seen, 12, 0));
    CHECK(cell_blank(&s.seen, 3, 0)); /* the spaces */
    CHECK(cell_has(&s.seen, 39, 24, 14) && !cell_has(&s.seen, 0, 23, 14));
    for (int x = 0; x < 40; ++x) CHECK(cell_blank(&game.screen, x, 0));
    CHECK(cell_has(&game.screen, 0, 1, 3));
    /* A short text: row 24 cleared before it (1521:096c). */
    cok_picture_fill(&game.screen, 0, 24 * 8, 40, 8, 3);
    keys("x");
    cok_debug_trace(&game, "GOTO", 0, 0, 0);
    CHECK(cell_has(&s.seen, 0, 24, 14) && cell_has(&s.seen, 3, 24, 14) && cell_blank(&s.seen, 10, 24));
    /* Not once the keys have run out. */
    keys("");
    game.input_ended = true;
    cok_debug_trace(&game, "EXIT", 0, 0, 0);
    CHECK(s.reads == 0);
    game.debug = false;
}

/* 6d7e:024f's names. */
static void test_names(void)
{
    char name[16];
    cok_debug_opcode_name(COK_ECL_IF_EQUAL, name);
    CHECK(strcmp(name, "IF = ") == 0);
    cok_debug_opcode_name(COK_ECL_IF_NOT_EQUAL, name);
    CHECK(strcmp(name, "IF <>") == 0);
    cok_debug_opcode_name(0x1f, name);
    CHECK(strcmp(name, "BULLSHIT 31") == 0);
    cok_debug_opcode_name(0xff, name);
    CHECK(strcmp(name, "BULLSHIT 255") == 0);
    cok_debug_opcode_name(COK_ECL_ADD_EP, name);
    CHECK(strcmp(name, "ADD EP") == 0);
}

/* View's item: its fields, the type's row, and a type past the table. */
static void test_item(void)
{
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = 0x12;
    item[0x2f] = 1;
    item[0x32] = 0xff;
    item[0x3a] = 0x01;
    item[0x3b] = 0x02;
    item[0x3e] = 0x86;
    game.item_types.type[0x12][0] = 0;
    game.item_types.type[0x12][12] = 7;
    game.item_types.type[0x12][14] = 4;
    keys("x");
    CHECK(cok_debug_item(&game, item));
    CHECK(s.reads == 1);
    CHECK(strstr(s.log, "debug: itemptr:      18;debug: namenum(1):   1;") != NULL);
    CHECK(strstr(s.log, "debug: plus:         255;") != NULL);
    CHECK(strstr(s.log, "debug: value:        513;") != NULL);
    CHECK(strstr(s.log, "debug: special(3):   134;") != NULL);
    CHECK(strstr(s.log, "debug: missile type: 4;debug: max range :   7;debug: item type :    0;"
                        "debug: press a key;") != NULL);
    CHECK(cell_has(&s.seen, 1, 1, 10) && cell_has(&s.seen, 20, 16, 10) && cell_has(&s.seen, 0, 24, 10));
    /* The labels from column 1: "itemptr:" ends at 8. */
    CHECK(cell_has(&s.seen, 8, 1, 10) && cell_blank(&s.seen, 9, 1));
    /* The frame cleared (1128:0000): blank inside, the side columns drawn. */
    CHECK(cell_blank(&s.seen, 30, 20) && !cell_blank(&s.seen, 0, 10) && !cell_has(&s.seen, 0, 10, 3));
    /* Type 128 is the table's last row (zeroed past what ITEMS fills). */
    item[0x2e] = 128;
    game.item_types.type[128][0] = 9;
    game.item_types.type[128][12] = 3;
    game.item_types.type[128][14] = 2;
    keys("x");
    CHECK(cok_debug_item(&game, item) && s.reads == 1);
    CHECK(strstr(s.log, "debug: itemptr:      128;") != NULL);
    CHECK(strstr(s.log, "debug: missile type: 2;debug: max range :   3;debug: item type :    9;")
          != NULL);
    memset(game.item_types.type[128], 0, 16);
    item[0x2e] = 129;
    keys("x");
    CHECK(!cok_debug_item(&game, item) && game.vm.status == COK_ECL_UNDEFINED && s.reads == 0);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
}

/* The "*" of the status line (6346:2d75) and of the combat menu
 * (3995:08d5), and Items' View (546c:17f9). */
static void test_stars(void)
{
    cok_ecl *vm = &game.vm;
    vm->mode = 4;
    vm->map_x = 7;
    vm->map_y = 15;
    vm->direction = 0;
    vm->mem4b00[0xc9] = 0;
    vm->mem4b00[0xc8] = 1;
    vm->mem4b00[0xc7] = 5;
    vm->mem4b00[0xfb] = 0;
    game.debug = false;
    cok_adventure_status(&game);
    /* "7,15 N 00:15" from column 17: the cell after it stays blank. */
    CHECK(cell_has(&game.screen, 0x11 + 11, 15, 10) && cell_blank(&game.screen, 0x11 + 12, 15));
    game.debug = true;
    cok_adventure_status(&game);
    CHECK(cell_has(&game.screen, 0x11 + 12, 15, 10));
    vm->mode = 2;
    cok_adventure_status(&game);
    CHECK(cell_has(&game.screen, 0x11 + 12, 15, 10) && cell_blank(&game.screen, 0x11 + 13, 15) &&
          cell_has(&game.screen, 0x11 + 14, 15, 10));

    cok_character *c = member("TAS");
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    c->record[0x5a] = 5;
    char text[41];
    cok_combat_command_text(&game, c, text);
    CHECK(strcmp(text, "View Aim Yell Done*") == 0);
    game.debug = false;
    cok_combat_command_text(&game, c, text);
    CHECK(strcmp(text, "View Aim Yell Done") == 0);

    /* Items with View first; V shows the item picked, then the list again. */
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = 0x12;
    item[0x31] = 0x12;
    CHECK(cok_character_insert_item(c, 0, item));
    c->record[0x142] = 1;
    vm->character = c->record;
    vm->mode = 4;
    game.selected = 1;
    game.list_top = 1;
    game.debug = true;
    keys("Vx\x1b");
    bool done = false;
    cok_items(&game, &done);
    CHECK(strstr(s.log, "menu: View Ready Use Trade Drop Halve Join;choice: V;debug: itemptr:      18;")
          != NULL);
    CHECK(strstr(s.log, "debug: press a key;list: Items;item:  No   Long Sword ;"
                        "menu: View Ready Use Trade Drop Halve Join;") != NULL);
    CHECK(s.reads == 3 && !game.vm.abort);
    /* The list's header drawn again after View: "Ready Item" in white. */
    CHECK(cell_has(&s.seen, 1, 3, 15) && !cell_has(&s.seen, 1, 3, 10));
    game.debug = false;
    keys("\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "menu: Ready Use Trade Drop Halve Join;") != NULL);
    vm->character = NULL;
    cok_party_free(&game.party);
}

/* The ECL's trace: after each opcode (2fd3:3add), SPELL's (2fd3:3292),
 * and debug.txt's line for each tile set LOAD PIECES loads (6e22:0050). */
static void test_ecl(void)
{
    cok_ecl *vm = &game.vm;
    cok_character *c = member("ALDA");
    c->record[0x1e + 5] = 0x16;
    member("BRAM");
    const uint8_t code[] = {COK_ECL_SPELL, 0, 0x16, 1, 0x05, 0x4c, 1, 0x06, 0x4c,
                            COK_ECL_IF_EQUAL, COK_ECL_GOTO, 2, 0x00, 0x80, COK_ECL_EXIT};
    memset(vm->code, 0, sizeof vm->code);
    memcpy(vm->code, code, sizeof code);
    vm->size = sizeof vm->code;
    memset(vm->flags, 0, sizeof vm->flags);
    game.debug = true;
    keys("xxxxx");
    CHECK(cok_ecl_run(vm, 0x8000) == COK_ECL_OK);
    CHECK(cok_ecl_load_var(vm, 0x4c05) == 5 && cok_ecl_load_var(vm, 0x4c06) == 0);
    /* The failed IF's skip leaves GOTO the opcode last fetched. */
    CHECK(strcmp(s.log, "debug: 5 , 0 , 22 | spell;debug: 0 , 0 , 0 | SPELL;"
                        "debug: 0 , 0 , 0 | GOTO;debug: 0 , 0 , 0 | EXIT;") == 0);
    CHECK(s.reads == 4);
    game.debug = false;
    keys("");
    CHECK(cok_ecl_run(vm, 0x8000) == COK_ECL_OK && s.reads == 0 && s.log[0] == '\0');

    /* LOAD PIECES 1 255 255 with debug.txt open: WALLDEF1 record 1 holds
     * two wall sets, which load tile sets 1 and 2. */
    const uint8_t pieces[] = {COK_ECL_LOAD_PIECES, 0, 1, 0, 0xff, 0, 0xff, COK_ECL_EXIT};
    memset(vm->code, 0, sizeof vm->code);
    memcpy(vm->code, pieces, sizeof pieces);
    vm->file = 1;
    vm->mem4b00[0xe7] = 0;
    game.woof = true;
    keys("\x04xx");
    cok_adventure_wait_key(&game);
    CHECK(game.debug);
    CHECK(cok_ecl_run(vm, 0x8000) == COK_ECL_OK);
    CHECK(strstr(s.log, "debug: on;debug.txt: slotnum = 1;debug.txt: slotnum = 2;"
                        "debug: 0 , 0 , 0 | LOAD PIECES;debug: 0 , 0 , 0 | EXIT;") != NULL);
    cok_debug_key(&game, 4);
    CHECK(strcmp(on_disk(), "slotnum = 1\r\nslotnum = 2\r\n") == 0);
    game.woof = false;
    cok_party_free(&game.party);
}

int main(void)
{
    cok_keyboard k = {scripted, &s, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .context = &s};
    CHECK(cok_adventure_open(&game, "COKAssets", &k, &hooks));
    CHECK(cok_picture_create(&s.seen, game.screen.units, game.screen.height, 1, 0) == COK_PICTURE_OK);
    CHECK(mkdtemp(dir) != NULL);
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
    test_keys();
    test_trace();
    test_names();
    test_item();
    test_stars();
    test_ecl();
    /* The end of a run closes debug.txt while the flag is set, writing
     * what its buffer holds; with the flag clear it is dropped. */
    game.woof = true;
    keys("");
    cok_debug_key(&game, 4);
    cok_debug_write(&game, "kept");
    cok_picture_free(&s.seen);
    cok_adventure_close(&game);
    CHECK(strcmp(on_disk(), "kept\r\n") == 0);
    game.debug = false;
    cok_debug_key(&game, 4);
    cok_debug_write(&game, "dropped");
    cok_debug_reset(&game);
    cok_debug_end(&game);
    CHECK(strcmp(on_disk(), "") == 0);
    char path[sizeof dir + 16];
    snprintf(path, sizeof path, "%s/DEBUG.TXT", dir);
    unlink(path);
    rmdir(dir);
    puts("debug tests passed");
    return 0;
}
