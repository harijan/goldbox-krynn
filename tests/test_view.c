#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static cok_picture screen(void)
{
    cok_picture picture = {0};
    CHECK(cok_picture_create(&picture, 40, 200, 1, 0) == COK_PICTURE_OK);
    return picture;
}

/* Colour of the top-left pixel of cell x, y. */
static unsigned cell(const cok_picture *p, int x, int y)
{
    return p->pixels[(size_t)y * 8 * p->units * 4 + (size_t)x * 4] >> 4;
}

static unsigned pixel(const cok_picture *p, int x, int y)
{
    uint8_t byte = p->pixels[(size_t)y * p->units * 4 + (size_t)x / 2];
    return x % 2 == 0 ? byte >> 4 : byte & 15u;
}

/* A tile set of count 8x8 tiles; tile t is colour colour(t), with colour 13
 * at its top-left pixel when hole is set. */
static cok_picture tile_set(size_t count, unsigned (*colour)(size_t), bool hole)
{
    cok_picture tiles = {0};
    CHECK(cok_picture_create(&tiles, 1, 8, count, 0) == COK_PICTURE_OK);
    for (size_t t = 0; t < count; ++t) {
        uint8_t value = (uint8_t)(colour(t) * 0x11);
        memset(tiles.pixels + t * tiles.frame_size, value, tiles.frame_size);
        if (hole) tiles.pixels[t * tiles.frame_size] = (uint8_t)(0xd0 | (value & 15));
    }
    /* Reload through cok_picture_load so colour 13 becomes transparent. */
    cok_image *frames = calloc(count, sizeof *frames);
    CHECK(frames != NULL);
    for (size_t t = 0; t < count; ++t)
        frames[t] = cok_picture_frame(&tiles, t);
    cok_picture loaded = {0};
    CHECK(cok_picture_load(&loaded, frames, count, 13) == COK_PICTURE_OK);
    free(frames);
    cok_picture_free(&tiles);
    return loaded;
}

static unsigned colour_by_index(size_t t) { return 1 + (unsigned)(t % 12); }
static unsigned colour_two(size_t t) { (void)t; return 2; }
static unsigned colour_three(size_t t) { (void)t; return 3; }
static unsigned colour_four(size_t t) { (void)t; return 4; }
static unsigned colour_five(size_t t) { (void)t; return 5; }

static void test_map(void)
{
    cok_view view = {0};
    uint8_t record[0x402] = {0};
    CHECK(!cok_view_set_map(&view, record, sizeof record - 1));
    /* Square 3, 2: north 1, east 2, south 3, west 4, square byte 0x90. */
    record[2 + 2 * 16 + 3] = 0x12;
    record[2 + 0x100 + 2 * 16 + 3] = 0x34;
    record[2 + 0x200 + 2 * 16 + 3] = 0x90;
    /* Square 15, 0 and 0, 15 for the edges. */
    record[2 + 15] = 0x50;
    record[2 + 15 * 16] = 0x60;
    CHECK(cok_view_set_map(&view, record, sizeof record));
    CHECK(cok_view_wall(&view, 0, 3, 2) == 1);
    CHECK(cok_view_wall(&view, 2, 3, 2) == 2);
    CHECK(cok_view_wall(&view, 4, 3, 2) == 3);
    CHECK(cok_view_wall(&view, 6, 3, 2) == 4);
    CHECK(cok_view_wall(&view, 1, 3, 2) == 0);
    CHECK(cok_view_square(&view, 3, 2) == 0x90);
    /* Off the map nothing, unless the map wraps: by one square only, with
     * coordinates taken as bytes. */
    CHECK(cok_view_wall(&view, 0, -1, 0) == 0);
    view.wrap = true;
    CHECK(cok_view_wall(&view, 0, -1, 0) == 5);
    CHECK(cok_view_wall(&view, 0, 15 + 256, 0) == 5);
    CHECK(cok_view_wall(&view, 0, 0, -1) == 6);
    CHECK(cok_view_wall(&view, 0, 17, 15) == 6);
    CHECK(cok_view_wall(&view, 0, -2, 0) == 5);
}

static void test_passage(void)
{
    cok_view view = {0};
    uint8_t record[0x402] = {0};
    /* Square 3, 2: walls on all four sides; the fourth table marks north
     * open (1), east locked (2), south unpickable (3) and west solid (0).
     * Square 4, 2 has no walls but door bits, which do not count. */
    record[2 + 2 * 16 + 3] = 0x12;
    record[2 + 0x100 + 2 * 16 + 3] = 0x34;
    record[2 + 0x300 + 2 * 16 + 3] = 0x39;
    record[2 + 0x300 + 2 * 16 + 4] = 0xff;
    CHECK(cok_view_set_map(&view, record, sizeof record));
    CHECK(cok_view_passage(&view, 0, 3, 2) == 1);
    CHECK(cok_view_passage(&view, 2, 3, 2) == 2);
    CHECK(cok_view_passage(&view, 4, 3, 2) == 3);
    CHECK(cok_view_passage(&view, 6, 3, 2) == 0);
    CHECK(cok_view_passage(&view, 6, 4, 2) == 1);
    /* Off the map: nothing to pass through unless the map wraps. */
    CHECK(cok_view_passage(&view, 0, -1, 2) == 0);
    view.wrap = true;
    CHECK(cok_view_passage(&view, 0, -1, 2) == 1);

    int x = 0, y = 0;
    cok_view_step(0, &x, &y);
    CHECK(x == 0 && y == -1);
    cok_view_step(3, &x, &y);
    CHECK(x == 1 && y == 0);
    cok_view_step(6 + 8, &x, &y);
    CHECK(x == 0 && y == 0);
}

static void test_walls(void)
{
    cok_view view = {0};
    uint8_t record[2 * COK_VIEW_WALL_SET] = {0};
    record[0] = 0x2c;
    record[1] = 0x2d;
    record[2] = 0x2e;
    record[COK_VIEW_WALL_SET - 1] = 0x73;
    record[COK_VIEW_WALL_SET] = 0x2e;
    CHECK(cok_view_set_walls(&view, 0, record, COK_VIEW_WALL_SET) == 0);
    CHECK(cok_view_set_walls(&view, 4, record, COK_VIEW_WALL_SET) == 0);
    CHECK(cok_view_set_walls(&view, 1, record, 0) == 0);
    CHECK(cok_view_set_walls(&view, 1, record, COK_VIEW_WALL_SET + 1) == 0);
    CHECK(cok_view_set_walls(&view, 3, record, sizeof record) == 0);
    /* Two sets from slot 2: the first moves to tile set 2, the second to 3. */
    CHECK(cok_view_set_walls(&view, 2, record, sizeof record) == 2);
    const uint8_t *set2 = view.walls + COK_VIEW_WALL_SET, *set3 = set2 + COK_VIEW_WALL_SET;
    CHECK(set2[0] == 0x2c && set2[1] == 0x2d + 0x46 && set2[2] == 0x74);
    CHECK(set2[COK_VIEW_WALL_SET - 1] == 0xb9);
    CHECK(set3[0] == 0xba);
    CHECK(cok_view_set_walls(&view, 1, record, COK_VIEW_WALL_SET) == 1);
    CHECK(view.walls[2] == 0x2e && view.walls[1] == 0x2d);
}

static void test_tile(void)
{
    cok_view view = {0};
    cok_picture p = screen();
    view.tiles[0] = tile_set(45, colour_by_index, false);
    view.tiles[1] = tile_set(70, colour_by_index, false);
    view.tiles[4] = tile_set(40, colour_by_index, true);
    cok_view_tile(&p, &view, 1, 0, 0, false);
    CHECK(cell(&p, 0, 0) == 1);
    cok_view_tile(&p, &view, 0x2d, 1, 0, false);
    CHECK(cell(&p, 1, 0) == colour_by_index(0x2c));
    cok_view_tile(&p, &view, 0x2e, 2, 0, false);
    CHECK(cell(&p, 2, 0) == 1);
    cok_view_tile(&p, &view, 0x73, 3, 0, false);
    CHECK(cell(&p, 3, 0) == colour_by_index(0x45));
    cok_view_tile(&p, &view, 0x127, 4, 0, false);
    CHECK(pixel(&p, 4 * 8 + 1, 0) == colour_by_index(0x27));
    /* Missing sets, 0 and values past 0x127 draw nothing. */
    cok_picture_fill(&p, 0, 8, 40, 8, 15);
    cok_view_tile(&p, &view, 0x74, 0, 1, false);
    cok_view_tile(&p, &view, 0, 1, 1, false);
    cok_view_tile(&p, &view, 0x128, 2, 1, false);
    CHECK(cell(&p, 0, 1) == 15 && cell(&p, 1, 1) == 15 && cell(&p, 2, 1) == 15);
    /* Colour 13 is transparent when masked and black when not. */
    cok_view_tile(&p, &view, 0x100, 3, 1, true);
    CHECK(cell(&p, 3, 1) == 15 && pixel(&p, 3 * 8 + 1, 8) == 1);
    cok_view_tile(&p, &view, 0x100, 4, 1, false);
    CHECK(cell(&p, 4, 1) == 0);
    cok_view_free(&view);
    cok_picture_free(&p);
}

/* A view with wall types 1 and 2 in tile sets 2 and 3, every place of
 * type 1 filled with tile 0x74 and of type 2 with 0xba. */
static cok_view walled_view(void)
{
    cok_view view = {0};
    view.tiles[2] = tile_set(70, colour_two, false);
    view.tiles[3] = tile_set(70, colour_three, false);
    memset(view.walls, 0x74, COK_VIEW_WALL_TYPE);
    memset(view.walls + COK_VIEW_WALL_TYPE, 0xba, COK_VIEW_WALL_TYPE);
    return view;
}

static void set_wall(cok_view *view, int x, int y, unsigned dir, uint8_t type)
{
    uint8_t *byte = view->map + (dir >= 4 ? 0x100 : 0) + y * 16 + x;
    if (dir % 4 == 0)
        *byte = (uint8_t)((*byte & 0x0f) | type << 4);
    else
        *byte = (uint8_t)((*byte & 0xf0) | type);
}

static void test_draw(void)
{
    cok_view_backdrop backdrop = {.sky = 11, .horizon = 0, .ground = 8, .hour = 0};
    cok_picture p = screen();
    cok_view view = walled_view();

    /* Backdrop only: sky, a two-row horizon line, then ground, in the view
     * at cells 3-13 across and rows 24-111 down. */
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(pixel(&p, 24, 24) == 11 && pixel(&p, 111, 67) == 11);
    CHECK(pixel(&p, 24, 68) == 0 && pixel(&p, 24, 69) == 0);
    CHECK(pixel(&p, 24, 70) == 8 && pixel(&p, 111, 111) == 8);
    CHECK(pixel(&p, 23, 24) == 0 && pixel(&p, 112, 24) == 0 && pixel(&p, 24, 112) == 0);

    /* Facing north from 5, 5: the wall north of the party's square fills
     * place 6, cells 5-11 by 4-11; its west wall place 7, cells 3-4 by
     * 3-13. */
    set_wall(&view, 5, 5, 0, 1);
    set_wall(&view, 5, 5, 6, 2);
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(cell(&p, 5, 4) == 2 && cell(&p, 11, 11) == 2);
    CHECK(cell(&p, 12, 4) == 11 && cell(&p, 5, 3) == 11);
    CHECK(cell(&p, 3, 3) == 3 && cell(&p, 4, 13) == 3);
    /* Facing east, the north wall is on the left and the west wall behind. */
    cok_view_draw(&p, &view, 5, 5, 2, &backdrop);
    CHECK(cell(&p, 3, 3) == 2 && cell(&p, 4, 13) == 2);
    CHECK(cell(&p, 8, 6) == 11 && cell(&p, 8, 12) == 8);

    /* Two squares ahead, a front wall is place 0, cells 8 by 7-8; beside
     * it to the left another at cell 6, with the edge between them (place
     * 9, in the nearer-the-middle wall's type) at cell 7. */
    cok_view_free(&view);
    view = walled_view();
    set_wall(&view, 5, 3, 0, 1);
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(cell(&p, 8, 7) == 2 && cell(&p, 8, 8) == 2);
    CHECK(cell(&p, 8, 6) == 11 && cell(&p, 8, 9) == 8);
    CHECK(cell(&p, 7, 7) == 11 && cell(&p, 6, 7) == 11);
    set_wall(&view, 4, 3, 0, 2);
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(cell(&p, 6, 7) == 3 && cell(&p, 7, 7) == 2 && cell(&p, 8, 7) == 2);

    /* The edge also shows where a front wall ends at a side wall. Type 2
     * has no tiles at place 1 here, so the side wall does not cover it. */
    cok_view_free(&view);
    view = walled_view();
    memset(view.walls + COK_VIEW_WALL_TYPE + 2, 0, 4);
    set_wall(&view, 5, 3, 0, 1);
    set_wall(&view, 5, 3, 6, 2);
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(cell(&p, 7, 7) == 2 && cell(&p, 7, 8) == 2 && cell(&p, 6, 7) == 11);

    /* Side walls two ahead: place 1 at cell 7 and, a square further left,
     * cell 4; place 2 at cells 9 and 12; all on rows 6-9. */
    cok_view_free(&view);
    view = walled_view();
    set_wall(&view, 5, 3, 6, 2);
    set_wall(&view, 4, 3, 6, 2);
    set_wall(&view, 5, 3, 2, 1);
    set_wall(&view, 6, 3, 2, 1);
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(cell(&p, 7, 6) == 3 && cell(&p, 7, 9) == 3 && cell(&p, 7, 5) == 11);
    CHECK(cell(&p, 4, 6) == 3 && cell(&p, 5, 6) == 11 && cell(&p, 6, 6) == 11);
    CHECK(cell(&p, 9, 6) == 2 && cell(&p, 9, 9) == 2 && cell(&p, 8, 6) == 11);
    CHECK(cell(&p, 12, 6) == 2 && cell(&p, 10, 6) == 11 && cell(&p, 11, 6) == 11);

    /* One ahead: the front wall is place 3, cells 7-9 by 6-9, and its west
     * side place 4, cells 5-6 by 4-11. */
    cok_view_free(&view);
    view = walled_view();
    set_wall(&view, 5, 4, 0, 1);
    set_wall(&view, 5, 4, 6, 2);
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(cell(&p, 7, 6) == 2 && cell(&p, 9, 9) == 2 && cell(&p, 10, 6) == 11);
    CHECK(cell(&p, 5, 4) == 3 && cell(&p, 6, 11) == 3 && cell(&p, 5, 3) == 11);

    /* Front walls beside those: one ahead and to the right, cells 10-12 by
     * 6-9, and to the left, cells 4-6; beside the party on the left, cells
     * 3-4 by 4-11. */
    cok_view_free(&view);
    view = walled_view();
    set_wall(&view, 6, 4, 0, 2);
    set_wall(&view, 4, 4, 0, 2);
    set_wall(&view, 4, 5, 0, 1);
    cok_view_draw(&p, &view, 5, 5, 0, &backdrop);
    CHECK(cell(&p, 10, 6) == 3 && cell(&p, 12, 9) == 3 && cell(&p, 9, 6) == 11);
    CHECK(cell(&p, 13, 6) == 11);
    CHECK(cell(&p, 5, 6) == 3 && cell(&p, 6, 9) == 3 && cell(&p, 7, 6) == 11);
    CHECK(cell(&p, 3, 4) == 2 && cell(&p, 4, 11) == 2 && cell(&p, 5, 4) == 11);
    cok_view_free(&view);
    cok_picture_free(&p);
}

static void test_sky(void)
{
    cok_view_backdrop backdrop = {.sky = 11, .horizon = 0, .ground = 8, .hour = 3};
    cok_picture p = screen();
    cok_view view = {0};
    view.sky[0] = tile_set(1, colour_four, false);
    view.sky[1] = tile_set(1, colour_five, false);
    view.sky[2] = tile_set(1, colour_two, false);
    /* The horizon picture always goes at cell 3, 8; facing east at hour 3
     * the sun is at cell 10, 5; facing north the strip is at 3, 3. */
    cok_view_draw(&p, &view, 0, 0, 2, &backdrop);
    CHECK(cell(&p, 3, 8) == 2 && cell(&p, 10, 5) == 5 && cell(&p, 3, 3) == 11);
    cok_view_draw(&p, &view, 0, 0, 0, &backdrop);
    CHECK(cell(&p, 10, 5) == 11 && cell(&p, 3, 3) == 4);
    /* Neither shows under another sky colour, or on squares from 0x80. */
    backdrop.sky = 9;
    cok_view_draw(&p, &view, 0, 0, 0, &backdrop);
    CHECK(cell(&p, 3, 3) == 9 && cell(&p, 3, 8) == 2);
    backdrop.sky = 11;
    view.map[0x200] = 0x80;
    cok_view_draw(&p, &view, 0, 0, 0, &backdrop);
    CHECK(cell(&p, 3, 3) == 11);
    CHECK(cok_view_sky_color(11) == 11 && cok_view_sky_color(8) == 0 &&
          cok_view_sky_color(9) == 15 && cok_view_sky_color(1000) == 0);
    cok_view_free(&view);
    cok_picture_free(&p);
}

/* A tile set whose tile t has colour 13, transparent, at its top-left
 * pixel, t >> 4 and t & 15 in the next two, and colour 15 elsewhere. */
static cok_picture numbered_set(size_t count)
{
    cok_picture tiles = {0};
    CHECK(cok_picture_create(&tiles, 1, 8, count, 0) == COK_PICTURE_OK);
    for (size_t t = 0; t < count; ++t) {
        uint8_t *pixels = tiles.pixels + t * tiles.frame_size;
        memset(pixels, 0xff, tiles.frame_size);
        pixels[0] = (uint8_t)(0xd0 | t >> 4);
        pixels[1] = (uint8_t)(t << 4 | 15);
        /* The arrows, tiles 0-3, have colour 13 at 4, 2 too. */
        if (t < 4) pixels[2 * 4 + 2] = 0xdf;
    }
    cok_image *frames = calloc(count, sizeof *frames);
    CHECK(frames != NULL);
    for (size_t t = 0; t < count; ++t) frames[t] = cok_picture_frame(&tiles, t);
    cok_picture loaded = {0};
    CHECK(cok_picture_load(&loaded, frames, count, 13) == COK_PICTURE_OK);
    free(frames);
    cok_picture_free(&tiles);
    return loaded;
}

/* The pixel at 4, 2 of cell x, y. */
static unsigned middle(const cok_picture *p, int x, int y)
{
    return pixel(p, x * 8 + 4, y * 8 + 2);
}

/* The frame's tile value drawn opaquely at cell x, y, or -1. */
static int tile_at(const cok_picture *p, int x, int y)
{
    if (pixel(p, x * 8, y * 8) != 0 || pixel(p, x * 8 + 3, y * 8 + 7) != 15) return -1;
    return 0x100 + (int)(pixel(p, x * 8 + 1, y * 8) << 4 | pixel(p, x * 8 + 2, y * 8));
}

static void test_overhead(void)
{
    cok_picture p = screen();
    cok_view view = {0};
    view.tiles[4] = numbered_set(0x28);

    /* An empty map from 7, 13 facing north: the window starts at square
     * 2, 5 and fills cells 3-13 across and down with tile 0x104; the
     * party's arrow, 0x100, is at cell 8, 11. Nothing else is drawn. */
    cok_picture_fill(&p, 0, 0, 40, 200, 6);
    CHECK(cok_view_overhead(&p, &view, 7, 13, 0));
    CHECK(tile_at(&p, 3, 3) == 0x104 && tile_at(&p, 13, 13) == 0x104);
    CHECK(tile_at(&p, 8, 11) == 0x100 && tile_at(&p, 8, 10) == 0x104);
    /* Opaque: colour 13 is black there, not the square beneath. */
    CHECK(middle(&p, 8, 11) == 0 && middle(&p, 8, 10) == 15);
    CHECK(cell(&p, 2, 3) == 6 && cell(&p, 14, 3) == 6 && cell(&p, 3, 2) == 6 &&
          cell(&p, 3, 14) == 6);
    /* The arrow faces dir / 2. */
    CHECK(cok_view_overhead(&p, &view, 7, 13, 2) && tile_at(&p, 8, 11) == 0x101);
    CHECK(cok_view_overhead(&p, &view, 7, 13, 4) && tile_at(&p, 8, 11) == 0x102);
    CHECK(cok_view_overhead(&p, &view, 7, 13, 6) && tile_at(&p, 8, 11) == 0x103);
    CHECK(cok_view_overhead(&p, &view, 7, 13, 5) && tile_at(&p, 8, 11) == 0x102);

    /* A square adds 1, 2, 4 and 8 for walls of any type on its north,
     * east, south and west sides; its neighbours' walls do not count. */
    set_wall(&view, 4, 7, 0, 1);
    set_wall(&view, 5, 7, 2, 15);
    set_wall(&view, 6, 7, 4, 3);
    set_wall(&view, 7, 7, 6, 9);
    set_wall(&view, 8, 7, 0, 2);
    set_wall(&view, 8, 7, 2, 2);
    set_wall(&view, 8, 7, 4, 2);
    set_wall(&view, 8, 7, 6, 2);
    CHECK(cok_view_overhead(&p, &view, 7, 13, 0));
    CHECK(tile_at(&p, 5, 5) == 0x105 && tile_at(&p, 6, 5) == 0x106 &&
          tile_at(&p, 7, 5) == 0x108 && tile_at(&p, 8, 5) == 0x10c &&
          tile_at(&p, 9, 5) == 0x113 && tile_at(&p, 10, 5) == 0x104);
    CHECK(tile_at(&p, 5, 4) == 0x104 && tile_at(&p, 5, 6) == 0x104);
    /* The arrow covers the party's own square. */
    CHECK(cok_view_overhead(&p, &view, 8, 7, 0) && tile_at(&p, 8, 8) == 0x100);

    /* The window keeps to the map: its first column and row are 0-5. */
    CHECK(cok_view_overhead(&p, &view, 0, 0, 0) && tile_at(&p, 3, 3) == 0x100);
    CHECK(cok_view_overhead(&p, &view, 5, 5, 0) && tile_at(&p, 8, 8) == 0x100);
    CHECK(cok_view_overhead(&p, &view, 6, 10, 0) && tile_at(&p, 8, 8) == 0x100);
    CHECK(tile_at(&p, 6, 5) == 0x105);
    CHECK(cok_view_overhead(&p, &view, 11, 15, 0) && tile_at(&p, 9, 13) == 0x100);
    CHECK(cok_view_overhead(&p, &view, 15, 15, 0) && tile_at(&p, 13, 13) == 0x100);
    CHECK(tile_at(&p, 3, 5) == 0x106 && tile_at(&p, 6, 5) == 0x113);

    /* Off the map, or an arrow past the frame's tiles, draws nothing. */
    cok_picture_fill(&p, 0, 0, 40, 200, 6);
    CHECK(!cok_view_overhead(&p, &view, -1, 0, 0) && !cok_view_overhead(&p, &view, 0, 16, 0));
    CHECK(!cok_view_overhead(&p, &view, 256 + 16, 0, 0));
    CHECK(!cok_view_overhead(&p, &view, 0, 0, 0x50));
    CHECK(cell(&p, 3, 3) == 6 && cell(&p, 8, 8) == 6);
    CHECK(cok_view_overhead(&p, &view, 256 + 15, 0, 0x4f) && tile_at(&p, 13, 3) == 0x127);
    cok_view_free(&view);
    cok_picture_free(&p);
}

int main(void)
{
    test_map();
    test_passage();
    test_walls();
    test_tile();
    test_draw();
    test_sky();
    test_overhead();
    puts("view tests passed");
    return 0;
}
