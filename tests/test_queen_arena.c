/* The Dark Queen of Krynn's battle ground's set and palette (the arena_tiles
 * hook, which combat.c's build calls in place of the 4-bit tiles, dqk
 * 59c7:0e70 and 204c:0007): the palette's colour cycles draw random(8) from
 * the seed (261b:00b1), one for each cycle the set has (DUNGCOM 3 and 6 have
 * one; DUNGCOM 4 and WILDCOM 4 none). The game opened on DQKAssets/ (skipped
 * without). */
#include "adventure.h"
#include "arena.h"
#include "cloud.h"
#include "data.h"
#include "ecl.h"
#include "field.h"
#include "hlib.h"
#include "party.h"
#include "queen.h"
#include "rules.h"
#include "step.h"
#include "vga.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;
static cok_adventure game;

/* Whether queen_icon's slot and image are empty. */
static bool icon_empty(unsigned slot, unsigned image)
{
    const uint8_t *got = NULL;
    size_t got_size = 1;
    return queen_icon(&game, slot, image, &got, &got_size) && got == NULL && got_size == 0;
}

/* Whether queen_icon's slot and image hold record id of the library file, byte for byte
 * (hlib's record). */
static bool icon_holds(unsigned slot, unsigned image, const char *file, unsigned id)
{
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library lib;
    const uint8_t *want = NULL, *got = NULL;
    size_t want_size = 0, got_size = 0;
    bool ok = hlib_read_file(file, &bytes, &n) == HLIB_OK &&
              hlib_parse(bytes, n, &lib) == HLIB_OK &&
              hlib_record(&lib, id, &want, &want_size) == HLIB_OK &&
              queen_icon(&game, slot, image, &got, &got_size) && got_size == want_size &&
              (want_size == 0 || memcmp(got, want, want_size) == 0);
    free(bytes);
    return ok;
}

/* The screen's pixel at x, y (VGA: a byte a pixel). */
static uint8_t screen_at(int x, int y)
{
    return game.screen.pixels[(size_t)y * (size_t)(game.screen.units * 8) + (size_t)x];
}

/* The picture of record id of library file, decoded (the caller frees it). */
static bool picture_of(const char *file, unsigned id, cok_vga_image *image)
{
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library lib;
    const uint8_t *record = NULL;
    size_t size = 0;
    bool ok = hlib_read_file(file, &bytes, &n) == HLIB_OK &&
              hlib_parse(bytes, n, &lib) == HLIB_OK &&
              hlib_record(&lib, id, &record, &size) == HLIB_OK &&
              cok_vga_decode(record, size, NULL, image) == COK_VGA_OK;
    free(bytes);
    return ok;
}

/* Whether the screen shows image drawn at x0, y0 (mirrored where mirror): its pixels where
 * they are not transparent (0x15's mask; the rest keep the ground), and nothing past the view
 * (x or y 176 and more: the ground). The image is 0x10 or 0x15 (no AND mask). */
static bool shows(const cok_vga_image *image, int x0, int y0, bool mirror, uint8_t ground)
{
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            int from = mirror ? image->width - 1 - x : x;
            size_t j = (size_t)y * image->width + (size_t)from;
            uint8_t want = image->pixels[j];
            if (image->mask != NULL && image->mask[j]) want = ground;
            if (x0 + x >= 176 || y0 + y >= 176) want = ground;
            if (screen_at(x0 + x, y0 + y) != want) return false;
        }
    return true;
}

/* Ground of colour 0x20 over the view's cell at column x, row y (48 by 48 pixels). */
static void ground_at(int x, int y)
{
    cok_vga_fill(&game.screen, x, y, 48, 48, 0x20);
}

/* The combat icons drawn (arena_icon, 030f; the cursor's box through arena.c's cursor). */
static void test_icons_drawn(const cok_drawing *d)
{
    const char *cbody = "DQKAssets/DISK3/CBODY.TLB", *cpic = "DQKAssets/DISK2/CPIC.TLB";
    const char *comspr = "DQKAssets/DISK2/COMSPR.TLB";
    const cok_character_layout *L = R->character;
    /* CBODY 14 for slot 5 (queen_added_icons, 604b:05d7: +0xc4 into +0xc5). */
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    cok_field_set8(L, c->record, COK_FIELD_ICON_BODY, 14);
    cok_field_set8(L, c->record, COK_FIELD_ICON, 5);
    CHECK(queen_added_icons(&game, c->record) && icon_holds(5, 0, cbody, 14));
    free(c);
    cok_vga_image body = {0};
    CHECK(picture_of(cbody, 14, &body) && body.format == COK_VGA_CLEAR && body.x == 0 &&
          body.y == 0);
    /* Cell 2, 3 facing 0: the pixels at 56, 80 and the ground where 0xff. */
    ground_at(56, 80);
    CHECK(d->arena_icon(&game, 2, 3, 0, 0, 5));
    CHECK(shows(&body, 56, 80, false, 0x20));
    /* Facing 7 (above 3): mirrored. */
    ground_at(56, 80);
    CHECK(d->arena_icon(&game, 2, 3, 7, 0, 5));
    CHECK(shows(&body, 56, 80, true, 0x20) && !shows(&body, 56, 80, false, 0x20));
    /* 48 by 48 CPIC 4 (slot 12) at cell 6, 6 (152, 152): cut at 176. */
    CHECK(d->arena_load_icon(&game, "CPIC.TLB", 4, 12));
    cok_vga_image monster = {0};
    CHECK(picture_of(cpic, 4, &monster) && monster.width == 48 && monster.height == 48);
    ground_at(152, 152);
    CHECK(d->arena_icon(&game, 6, 6, 0, 0, 12));
    CHECK(shows(&monster, 152, 152, false, 0x20));
    /* An empty slot (11: its loads failed) draws nothing. */
    ground_at(56, 80);
    CHECK(d->arena_icon(&game, 2, 3, 0, 0, 11));
    for (int y = 80; y < 104; ++y)
        for (int x = 56; x < 80; ++x) CHECK(screen_at(x, y) == 0x20);
    /* Faced above 3, the empty slot draws the scratch slot 0x98's stale picture in the
     * original (19a4:0881:08f5): a stop, cited. */
    CHECK(!d->arena_icon(&game, 2, 3, 7, 0, 11) && strstr(game.error, "0x98") != NULL);
    /* Image 2 and a slot past the table stop, citing the table. */
    CHECK(!d->arena_icon(&game, 2, 3, 0, 2, 5) && strstr(game.error, "DS:b17d") != NULL);
    CHECK(!d->arena_icon(&game, 2, 3, 0, 0, 0x26) && strstr(game.error, "DS:b17d") != NULL);
    /* The cursor's box (slot 0x1e, COMSPR 15): cok_arena_centre with the cursor on draws it
     * over cell 2, 3 (arena.c's cursor; no occupant). */
    cok_vga_image box = {0};
    CHECK(picture_of(comspr, 15, &box) && box.format == COK_VGA_RAW && box.width == 24);
    game.combat.active = true;
    game.combat.cursor = 1;
    game.combat.cursor_size = 1;
    CHECK(cok_arena_centre(&game, 2, 3, 0xff, 8));
    CHECK(shows(&box, 56, 80, false, 0));
    game.combat.cursor = 0;
    game.combat.active = false;
    cok_vga_free(&box);
    cok_vga_free(&body);
    cok_vga_free(&monster);
}

/* A cell's 24 by 24 pixels in the view at column, row (the screen's bytes). */
static void cell_pixels(int column, int row, uint8_t out[24 * 24])
{
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            out[y * 24 + x] = screen_at(8 + 24 * column + x, 8 + 24 * row + y);
}

/* The cell at column, row drawn over the ground (its 24 by 24 pixels): the overlay of value's tile
 * (COMSPR's record tile, slot tile + 0x0f, image 0) where it is not transparent, else the ground. */
static void check_overlay(const uint8_t ground[24 * 24], int column, int row, uint8_t value)
{
    uint8_t drawn[24 * 24];
    cell_pixels(column, row, drawn);
    uint8_t tile = cok_combat_terrain(R, value).tile;
    const uint8_t *record = NULL;
    size_t size = 0;
    CHECK(queen_icon(&game, tile + 0x0f, 0, &record, &size) && record != NULL);
    cok_vga_image over = {0};
    CHECK(cok_vga_decode(record, size, NULL, &over) == COK_VGA_OK && over.x == 0 && over.y == 0);
    CHECK(over.width == 24 && over.height == 24);
    size_t masked = 0, overlaid = 0;
    for (size_t j = 0; j < 24 * 24; ++j) {
        if (over.mask != NULL && over.mask[j] != 0) {
            ++masked;
            CHECK(drawn[j] == ground[j]);
        } else {
            ++overlaid;
            CHECK(drawn[j] == over.pixels[j]);
        }
    }
    CHECK(masked > 0 && overlaid > 0);
    cok_vga_free(&over);
}

/* The cells under a cloud or a body (5837:172c:17ac-18d4): a cell of value 0x1b-0x1f or 0x46 is the
 * ground under it (137a: the terrain a cloud kept, through the cloud list; for 0x1e and 0x1f the
 * last body's cell at x, y), with COMSPR's overlay of the cell's own tile drawn masked over it. The
 * ground is the plain cell of the same terrain. */
static void test_overlays(const cok_drawing *d)
{
    cok_combat *k = &game.combat;
    k->view_x = 0;
    k->view_y = 0;
    uint8_t ground[24 * 24];
    /* A body at 1, 1 over the cell 0x20: the ground is 0x20's, the overlay 0x1f's own tile. */
    k->cells[1][1] = 0x20;
    CHECK(d->arena_cell(&game, 1, 1));
    cell_pixels(1, 1, ground);
    k->cells[1][1] = 0x1f;
    k->bodies = 1;
    memset(k->body, 0, sizeof k->body);
    k->body[0].x = 1;
    k->body[0].y = 1;
    k->body[0].cell = 0x20;
    CHECK(d->arena_cell(&game, 1, 1) && game.vm.status == COK_ECL_OK);
    check_overlay(ground, 1, 1, 0x1f);
    /* 0x1e (a stone) looks through to the body's cell too. */
    k->cells[1][1] = 0x1e;
    CHECK(d->arena_cell(&game, 1, 1));
    check_overlay(ground, 1, 1, 0x1e);
    k->bodies = 0;

    /* A cloud's cell 0x1d: the cell under it is the terrain the cloud kept (137a through the list:
     * a node at 4, 4 covering its cell 1, which kept 0x2a). */
    static cok_combat_cloud node;
    memset(&node, 0, sizeof node);
    node.x = 4;
    node.y = 4;
    node.covered[1] = 1;
    node.saved[1] = 0x2a;
    int column = 4 + cok_step_x[cok_data_u8_at(R->data, COK_DATA_CLOUD_CELLS, 1)];
    int row = 4 + cok_step_y[cok_data_u8_at(R->data, COK_DATA_CLOUD_CELLS, 1)];
    k->cells[row][column] = 0x1d;
    k->clouds = &node;
    k->cells[5][5] = 0x2a;
    CHECK(d->arena_cell(&game, 5, 5));
    cell_pixels(5, 5, ground);
    CHECK(d->arena_cell(&game, column, row));
    check_overlay(ground, column, row, 0x1d);
    k->clouds = NULL;
    k->cells[row][column] = 0x20;
    k->cells[5][5] = 0x20;
    k->cells[1][1] = 0x20;
}

int main(void)
{
    struct stat st;
    if (stat("DQKAssets/DISK2/DUNGCOM.TLB", &st) != 0) {
        puts("queen arena: no DQKAssets/; the battle's set and palette not checked");
        return 0;
    }
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    const cok_drawing *d = cok_adventure_drawing(&game);
    CHECK(d != NULL && d->arena_tiles != NULL);
    static const uint32_t seeds[] = {0, 1, 0x1756582c, 12345};
    for (size_t i = 0; i < sizeof seeds / sizeof *seeds; ++i) {
        /* The one draw a set with a cycle makes, from the same seed. */
        uint32_t one = seeds[i];
        cok_tp_random(&one, 8);
        for (unsigned set = 1; set <= 6; ++set) {
            game.vm.seed = seeds[i];
            CHECK(d->arena_tiles(&game, false, set));
            uint32_t want = set == 3 || set == 6 ? one : seeds[i];
            CHECK(game.vm.seed == want);
        }
        /* WILDCOM 4 has no cycle. */
        game.vm.seed = seeds[i];
        CHECK(d->arena_tiles(&game, true, 4));
        CHECK(game.vm.seed == seeds[i]);
    }
    /* The wilderness' set 3 is not one of WILDCOM's (LOAD FILES' DS:ca03 is
     * the set): it fails, and the seed is kept. */
    game.vm.seed = 7;
    CHECK(!d->arena_tiles(&game, true, 3) && game.vm.seed == 7);

    /* The monsters' icons (204c:00df, CPIC): record id into slot and record id + 0x80 into
     * slot + 0x26 (DS:b17d). */
    const char *cpic = "DQKAssets/DISK2/CPIC.TLB", *comspr = "DQKAssets/DISK2/COMSPR.TLB";
    CHECK(d->arena_load_icon(&game, "CPIC.TLB", 64, 8));
    CHECK(icon_holds(8, 0, cpic, 64) && icon_holds(8, 1, cpic, 192));
    CHECK(d->arena_load_icon(&game, "CPIC.TLB", 49, 9));
    CHECK(icon_holds(9, 0, cpic, 49) && icon_holds(9, 1, cpic, 177));
    /* CPIC 72 has no record 72: pose 0 is 100 + id (172, 004b:0056-0087), but pose 1 (200)
     * is missing, which stops with 19a4:004b (Q1). Neither image is stored (004b fails at
     * 0193 before 0881), so the slot stays empty. */
    CHECK(!d->arena_load_icon(&game, "CPIC.TLB", 72, 10));
    CHECK(strstr(game.error, "19a4:004b") != NULL && icon_empty(10, 0) && icon_empty(10, 1));
    /* Another name than the rules' icons stops, cited 204c:00df; a slot past the table
     * stops, cited DS:b17d. */
    CHECK(!d->arena_load_icon(&game, "DUNGCOM.TLB", 64, 11) && strstr(game.error, "204c:00df"));
    CHECK(!d->arena_load_icon(&game, "CPIC.TLB", 64, 0x26) && strstr(game.error, "DS:b17d"));
    /* The missiles and cursor (59c7:0e80-0f06): arena_tiles loads COMSPR 15 and 0x8f into
     * slot 0x1e, the cursor's box and its image 1. */
    CHECK(d->arena_tiles(&game, false, 4));
    CHECK(icon_holds(0x1e, 0, comspr, 15) && icon_holds(0x1e, 1, comspr, 0x8f));
    test_icons_drawn(d);
    test_overlays(d);
    cok_adventure_close(&game);
    puts("queen arena tests passed");
    return 0;
}
