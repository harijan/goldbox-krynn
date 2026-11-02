/* Dark Queen's libraries (hlib.h) and pictures and palettes (vga.h), on
 * made-up records; and where DQKAssets/ holds the game, every picture of
 * its libraries (1,827, nested ones' too) decoded, skipped saying so
 * otherwise. tests/test_dark_queen.py runs eclplay on its files. */
#include "hlib.h"
#include "vga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

/* A library of records (sizes[i] bytes of records[i]) into out; returns its size. */
static size_t library(uint8_t *out, const char *type, unsigned flags,
                      const uint8_t *const *records, const size_t *sizes, size_t count)
{
    size_t at = 16 + 4 * (count + 1);
    memcpy(out, "HLIB", 4);
    put16(out + 8, (unsigned)count);
    put16(out + 10, flags);
    memcpy(out + 12, type, 4);
    for (size_t i = 0; i < count; ++i) {
        put32(out + 16 + 4 * i, (uint32_t)at);
        memcpy(out + at, records[i], sizes[i]);
        at += sizes[i];
    }
    put32(out + 16 + 4 * count, (uint32_t)at);
    put32(out + 4, (uint32_t)at);
    return at;
}

static void test_library(void)
{
    /* An id map: ids 7 and 9 to records 2 and 1. */
    const uint8_t map[] = {2, 0, 7, 0, 2, 0, 9, 0, 1, 0};
    const uint8_t a[] = {'a', 'b'}, b[] = {'c', 'd', 'e'};
    const uint8_t *records[] = {map, a, b};
    size_t sizes[] = {sizeof map, sizeof a, sizeof b};
    uint8_t bytes[256];
    size_t size = library(bytes, "DATA", HLIB_ID_MAP, records, sizes, 3);
    hlib_library lib;
    CHECK(hlib_parse(bytes, size, &lib) == HLIB_OK);
    CHECK(lib.count == 3 && lib.ids == 2 && strcmp(lib.type, "DATA") == 0);
    const uint8_t *r;
    size_t n;
    CHECK(hlib_record(&lib, 7, &r, &n) == HLIB_OK && n == 3 && memcmp(r, "cde", 3) == 0);
    CHECK(hlib_record(&lib, 9, &r, &n) == HLIB_OK && n == 2 && memcmp(r, "ab", 2) == 0);
    CHECK(hlib_record(&lib, 1, &r, &n) == HLIB_NO_RECORD);
    unsigned id;
    size_t index;
    CHECK(hlib_id_at(&lib, 1, &id, &index) == HLIB_OK && id == 9 && index == 1);
    CHECK(hlib_id_at(&lib, 2, &id, &index) == HLIB_NO_RECORD);
    /* Without a map, ids are indexes. */
    size = library(bytes, "DATA", 0, records + 1, sizes + 1, 2);
    CHECK(hlib_parse(bytes, size, &lib) == HLIB_OK && lib.ids == 0);
    CHECK(hlib_record(&lib, 1, &r, &n) == HLIB_OK && n == 3);
    CHECK(hlib_record(&lib, 2, &r, &n) == HLIB_NO_RECORD);
    /* Nested. */
    const uint8_t *outer[] = {bytes};
    size_t outer_sizes[] = {size};
    uint8_t nest[512];
    size_t nest_size = library(nest, "HLIB", 0, outer, outer_sizes, 1);
    CHECK(hlib_parse(nest, nest_size, &lib) == HLIB_OK);
    CHECK(hlib_record(&lib, 0, &r, &n) == HLIB_OK && hlib_is_library(r, n));
    hlib_library inner;
    CHECK(hlib_parse(r, n, &inner) == HLIB_OK && inner.count == 2);
    /* Broken ones. */
    CHECK(hlib_parse(bytes, 15, &lib) == HLIB_BAD_HEADER);
    bytes[0] = 'X';
    CHECK(hlib_parse(bytes, size, &lib) == HLIB_BAD_HEADER);
    bytes[0] = 'H';
    put32(bytes + 16 + 4 * 2, (uint32_t)size + 1); /* past the end */
    CHECK(hlib_parse(bytes, size, &lib) == HLIB_BAD_RECORD);
    put32(bytes + 16 + 4 * 2, (uint32_t)size);
    put32(bytes + 16, (uint32_t)size); /* out of order */
    CHECK(hlib_parse(bytes, size, &lib) == HLIB_BAD_RECORD);
    /* An id map naming a record past the count. */
    const uint8_t bad_map[] = {1, 0, 7, 0, 5, 0};
    const uint8_t *bad[] = {bad_map, a};
    size_t bad_sizes[] = {sizeof bad_map, sizeof a};
    size = library(bytes, "DATA", HLIB_ID_MAP, bad, bad_sizes, 2);
    CHECK(hlib_parse(bytes, size, &lib) == HLIB_BAD_RECORD);
}

/* A picture record's header. */
static size_t header(uint8_t *p, unsigned width, unsigned height, int x, int y, uint8_t format)
{
    put16(p, height);
    put16(p + 2, (unsigned)x & 0xffff);
    put16(p + 4, (unsigned)y & 0xffff);
    p[6] = (uint8_t)(width / 4);
    p[7] = format;
    return COK_VGA_HEADER;
}

static void test_pictures(void)
{
    /* 8x2 raw: plane p's rows (two of two pixels), so pixel x of row j is
     * at (x % 4) * 4 + j * 2 + x / 4. */
    uint8_t record[64];
    size_t n = header(record, 8, 2, -3, 5, COK_VGA_RAW);
    for (unsigned i = 0; i < 16; ++i) record[n + i] = (uint8_t)(0x10 + i);
    cok_vga_image image = {0};
    CHECK(cok_vga_decode(record, n + 16, NULL, &image) == COK_VGA_OK);
    CHECK(image.width == 8 && image.height == 2 && image.x == -3 && image.y == 5);
    CHECK(image.mask == NULL);
    for (unsigned j = 0; j < 2; ++j)
        for (unsigned x = 0; x < 8; ++x)
            CHECK(image.pixels[j * 8 + x] == 0x10 + (x % 4) * 4 + j * 2 + x / 4);
    cok_vga_free(&image);
    CHECK(cok_vga_decode(record, n + 15, NULL, &image) == COK_VGA_INVALID);
    /* 0x15: 0xff clear. */
    record[7] = COK_VGA_CLEAR;
    record[n] = 0xff;
    CHECK(cok_vga_decode(record, n + 16, NULL, &image) == COK_VGA_OK);
    CHECK(image.mask != NULL && image.mask[0] && !image.mask[1]);
    cok_vga_free(&image);
    /* 0x12: PackBits of the rows, each row its four plane-rows (w/4 each). */
    n = header(record, 4, 2, 0, 0, COK_VGA_PACKED);
    const uint8_t packed[] = {0xfd, 7, 1, 8, 9}; /* 7 7 7 7, then 8 9 */
    memcpy(record + n, packed, sizeof packed);
    CHECK(cok_vga_decode(record, n + 3, NULL, &image) == COK_VGA_INVALID);
    /* 0x80 does nothing (dqk 10d7:0a1c), where PackBits elsewhere
     * repeats 129 times. */
    const uint8_t packed2[] = {0xfd, 7, 0x80, 3, 1, 2, 3, 4};
    memcpy(record + n, packed2, sizeof packed2);
    CHECK(cok_vga_decode(record, n + sizeof packed2, NULL, &image) == COK_VGA_OK);
    CHECK(memcmp(image.pixels, "\7\7\7\7\1\2\3\4", 8) == 0);
    cok_vga_free(&image);
    /* 0x17: runs of each plane-row: skip 2, copy 1, end. Width 8: two a
     * plane-row. */
    n = header(record, 8, 1, 0, 0, COK_VGA_RUNS);
    const uint8_t runs[] = {1, 5, 0, 0xff, 1, 6, 0, 0, 0};
    memcpy(record + n, runs, sizeof runs);
    CHECK(cok_vga_decode(record, n + sizeof runs, NULL, &image) == COK_VGA_OK);
    /* Plane 0's first pixel (x 0) 5; plane 1 skips one, its second (x 5) 6. */
    CHECK(image.pixels[0] == 5 && !image.mask[0]);
    CHECK(image.pixels[5] == 6 && !image.mask[5]);
    for (unsigned x = 1; x < 8; ++x)
        if (x != 5) CHECK(image.mask[x]);
    cok_vga_free(&image);
    CHECK(cok_vga_decode(record, n + 4, NULL, &image) == COK_VGA_INVALID);
    /* 0x11: a mask (0xff clear), then the pixels. */
    n = header(record, 4, 1, 0, 0, COK_VGA_MASKED);
    const uint8_t masked[] = {0, 0xff, 0, 0xff, 1, 2, 3, 4};
    memcpy(record + n, masked, sizeof masked);
    CHECK(cok_vga_decode(record, n + sizeof masked, NULL, &image) == COK_VGA_OK);
    CHECK(image.pixels[0] == 1 && !image.mask[0] && image.mask[1] && image.pixels[2] == 3);
    cok_vga_free(&image);
    /* Not a picture. */
    record[7] = COK_VGA_PALETTE;
    CHECK(cok_vga_decode(record, 16, NULL, &image) == COK_VGA_INVALID);
}

static void test_composed(void)
{
    /* Record 0 a 4x1 raw picture, record 1 an 8x2 one drawn of it at
     * (y 1, x 4). */
    uint8_t part[16], whole[32];
    size_t n = header(part, 4, 1, 0, 0, COK_VGA_RAW);
    memcpy(part + n, "\1\2\3\4", 4);
    size_t m = header(whole, 8, 2, 0, 0, COK_VGA_COMPOSED);
    const uint8_t entry[] = {0, 1, 1, 0, 4, 0};
    memcpy(whole + m, entry, sizeof entry);
    const uint8_t *records[] = {part, whole};
    size_t sizes[] = {n + 4, m + sizeof entry};
    uint8_t bytes[128];
    size_t size = library(bytes, "TILE", 0, records, sizes, 2);
    hlib_library lib;
    CHECK(hlib_parse(bytes, size, &lib) == HLIB_OK);
    cok_vga_image image = {0};
    CHECK(cok_vga_decode(whole, sizes[1], &lib, &image) == COK_VGA_OK);
    for (unsigned i = 0; i < 16; ++i) {
        bool drawn = i >= 12;
        CHECK(!image.mask[i] == drawn);
        if (drawn) CHECK(image.pixels[i] == i - 11);
    }
    cok_vga_free(&image);
    CHECK(cok_vga_decode(whole, sizes[1], NULL, &image) == COK_VGA_INVALID);
    /* Decoded as drawn: an entry at column 3 lands at 0 (FRAME.TLB 21's). */
    whole[m + 4] = 3;
    CHECK(cok_vga_decode(whole, sizes[1], &lib, &image) == COK_VGA_OK);
    CHECK(memcmp(image.pixels + 8, "\1\2\3\4", 4) == 0 && !image.mask[8] && image.mask[12]);
    cok_vga_free(&image);
    whole[m + 4] = 4;
    /* Drawn, the parts at their places from the record's, the column taken
     * down to a multiple of 4 (unchained VGA's bytes): at row 10, column
     * 7, record 0 lands at 11, 4 + 4. */
    static const uint8_t colours[256][3];
    cok_picture screen = {0};
    CHECK(cok_picture_create_vga(&screen, 40, 200, 1, colours) == COK_PICTURE_OK);
    CHECK(cok_vga_draw_record(&screen, &lib, 1, 10, 7, NULL, NULL));
    CHECK(memcmp(screen.pixels + 11 * 320 + 8, "\1\2\3\4", 4) == 0);
    CHECK(screen.pixels[11 * 320 + 7] == 0 && screen.pixels[11 * 320 + 12] == 0);
    CHECK(screen.pixels[10 * 320 + 8] == 0);
    /* Clipped. */
    memset(screen.pixels, 0, screen.frame_size);
    const cok_vga_clip clip = {0, 0, 200, 10};
    CHECK(cok_vga_draw_record(&screen, &lib, 1, 10, 7, &clip, NULL));
    CHECK(memcmp(screen.pixels + 11 * 320 + 8, "\1\2\0\0", 4) == 0);
    CHECK(!cok_vga_draw_record(&screen, &lib, 5, 0, 0, NULL, NULL));
    cok_picture_free(&screen);
}

static void test_palette(void)
{
    uint8_t record[32];
    size_t n = header(record, 0, 0, 0, 0, COK_VGA_PALETTE);
    put16(record, 5);       /* partial */
    put16(record + 2, 254); /* first */
    put16(record + 4, 2);   /* count */
    const uint8_t rgb[] = {0xff, 0x80, 0x03, 0x04, 0x00, 0xfc};
    memcpy(record + n, rgb, sizeof rgb);
    cok_vga_palette palette = {0};
    CHECK(cok_vga_load_palette(&palette, record, n + sizeof rgb));
    CHECK(palette.dac[254][0] == 63 && palette.dac[254][1] == 32 && palette.dac[254][2] == 0);
    CHECK(palette.dac[255][0] == 1 && palette.dac[255][2] == 63);
    CHECK(palette.rgb[254][0] == cok_vga_shown(63) && palette.rgb[255][0] == cok_vga_shown(1));
    CHECK(!cok_vga_load_palette(&palette, record, n + sizeof rgb - 1));
    put16(record + 2, 255); /* past 255 */
    CHECK(!cok_vga_load_palette(&palette, record, n + sizeof rgb));
    CHECK(cok_vga_shown(0) == 0);
}

static unsigned fixed_random(void *context, unsigned range)
{
    (void)range;
    return *(const unsigned *)context;
}

/* 261b:00b1, 0455 and 05be: a record's cycles start at now + random(8) +
 * rate - 3, rotate their live colours up (or down) a step each rate,
 * catching up; the same palette again leaves them running, one changed
 * colour restarts them from the record's colours; the slots run out. */
static void test_cycles(void)
{
    uint8_t record[64];
    size_t n = header(record, 0, 0, 0, 3, COK_VGA_PALETTE);
    put16(record, 7); /* partial, with cycles */
    put16(record + 2, 0x10);
    put16(record + 4, 3);
    const uint8_t rgb[] = {4, 4, 4, 8, 8, 8, 12, 12, 12};
    memcpy(record + n, rgb, sizeof rgb);
    const uint8_t cycles[] = {0, 5, 0x10, 3};
    memcpy(record + n + sizeof rgb, cycles, sizeof cycles);
    record[6] = 1;
    size_t size = n + sizeof rgb + sizeof cycles;
    unsigned two = 2;
    cok_random random = {fixed_random, &two};
    cok_vga_palette palette = {0};
    CHECK(cok_vga_apply_palette(&palette, record, size, 0, 100, &random));
    uint32_t next = 0;
    CHECK(cok_vga_cycle_next(&palette, &next) && next == 100 + 2 + 5 - 3);
    CHECK(!cok_vga_cycle_colours(&palette, 103));
    CHECK(palette.dac[0x10][0] == 1 && palette.dac[0x12][0] == 3);
    CHECK(cok_vga_cycle_colours(&palette, 104)); /* up: each takes the one before */
    CHECK(palette.dac[0x10][0] == 3 && palette.dac[0x11][0] == 1 && palette.dac[0x12][0] == 2);
    CHECK(palette.rgb[0x10][0] == cok_vga_shown(3));
    CHECK(cok_vga_cycle_colours(&palette, 114)); /* two more steps, caught up */
    CHECK(palette.dac[0x10][0] == 1 && palette.cycles[0].time == 119);
    /* The same again: running on, unrotated colours not flushed. */
    CHECK(cok_vga_cycle_colours(&palette, 119));
    CHECK(cok_vga_apply_palette(&palette, record, size, 0, 120, &random));
    CHECK(palette.dac[0x10][0] == 3 && palette.cycles[0].time == 124);
    /* A colour changed: restarted from the record's colours. */
    record[n] = 40;
    CHECK(cok_vga_apply_palette(&palette, record, size, 0, 200, &random));
    CHECK(palette.dac[0x10][0] == 10 && palette.dac[0x11][0] == 2 && palette.cycles[0].time == 204);
    /* Down, moved 0x20 on with its colours. */
    record[n + sizeof rgb] = 1;
    record[n] = 4;
    cok_vga_palette moved = {0};
    CHECK(cok_vga_apply_palette(&moved, record, size, 0x20, 0, &random));
    CHECK(moved.cycles[0].first == 0x30 && moved.dac[0x30][0] == 1);
    CHECK(cok_vga_cycle_colours(&moved, 4));
    CHECK(moved.dac[0x30][0] == 2 && moved.dac[0x32][0] == 1);
    /* Black over the range stops it. */
    CHECK(cok_vga_set_colours(&moved, 0x30, 1, NULL, NULL, 0, 5, NULL));
    CHECK(!cok_vga_cycle_next(&moved, &next));
    CHECK(moved.dac[0x30][0] == 0 && moved.dac[0x31][0] == 2 && moved.dac[0x32][0] == 3);
    /* Twelve slots. */
    cok_vga_palette full = {0};
    uint8_t many[13 * 4];
    for (unsigned i = 0; i < 13; ++i) {
        many[4 * i] = 0;
        many[4 * i + 1] = 9;
        many[4 * i + 2] = (uint8_t)(0x40 + 2 * i);
        many[4 * i + 3] = 2;
    }
    CHECK(cok_vga_set_colours(&full, 0x40, 24, NULL, many, 12, 0, NULL));
    CHECK(!cok_vga_set_colours(&full, 0x80, 2, NULL, many, 13, 0, NULL));
}

static void test_draw(void)
{
    static const uint8_t colours[256][3];
    cok_picture screen = {0};
    CHECK(cok_picture_create_vga(&screen, 40, 200, 1, colours) == COK_PICTURE_OK);
    CHECK(screen.vga && screen.frame_size == 320 * 200 && screen.units == 40);
    uint8_t pixels[4] = {1, 2, 3, 4}, mask[4] = {0, 1, 0, 0};
    cok_vga_image image = {.width = 2, .height = 2, .pixels = pixels, .mask = mask};
    cok_vga_draw(&screen, &image, -1, 199, NULL, NULL);
    CHECK(screen.pixels[199 * 320] == 0 && screen.pixels[199 * 320 + 1] == 0);
    cok_vga_draw(&screen, &image, 318, 0, NULL, NULL);
    CHECK(screen.pixels[318] == 1 && screen.pixels[319] == 0 && screen.pixels[320 + 318] == 3);
    cok_vga_fill(&screen, 316, 198, 10, 10, 9);
    CHECK(screen.pixels[199 * 320 + 319] == 9 && screen.pixels[198 * 320 + 315] == 0);
    cok_image shown = cok_picture_frame(&screen, 0);
    CHECK(shown.vga && shown.palette == colours && shown.width == 320);
    /* The 4-bit routines leave a VGA picture alone. */
    cok_picture tile = {0};
    CHECK(cok_picture_create(&tile, 1, 8, 1, 0) == COK_PICTURE_OK);
    memset(tile.pixels, 0xff, tile.frame_size);
    cok_picture_draw(&screen, &tile, 0, 0, 0, 0, NULL);
    cok_picture_fill(&screen, 0, 0, 1, 1, 15);
    CHECK(screen.pixels[0] == 0);
    cok_picture_free(&tile);
    cok_picture_free(&screen);
}

/* Decode every picture of a library and the libraries nested in it;
 * returns how many, failing on one that does not decode. */
static size_t decode_all(const uint8_t *data, size_t size, const char *where)
{
    hlib_library lib;
    if (hlib_parse(data, size, &lib) != HLIB_OK) {
        fprintf(stderr, "%s: not a library\n", where);
        exit(EXIT_FAILURE);
    }
    size_t count = 0;
    for (size_t i = (lib.flags & HLIB_ID_MAP) ? 1 : 0; i < lib.count; ++i) {
        const uint8_t *record;
        size_t n;
        CHECK(hlib_record_at(&lib, i, &record, &n) == HLIB_OK);
        if (hlib_is_library(record, n)) {
            count += decode_all(record, n, where);
            continue;
        }
        if (strcmp(lib.type, "TILE") != 0 || !cok_vga_is_picture(record, n)) continue;
        cok_vga_image image = {0};
        if (cok_vga_decode(record, n, &lib, &image) != COK_VGA_OK) {
            fprintf(stderr, "%s: record %zu does not decode\n", where, i);
            exit(EXIT_FAILURE);
        }
        cok_vga_free(&image);
        ++count;
    }
    return count;
}

static void test_game(void)
{
    static const char *const files[] = {
        "DISK1/ALWAYS.TLB", "DISK1/TITLE.TLB", "DISK2/8X8DB.TLB", "DISK2/8X8DC.TLB",
        "DISK2/BACK.TLB",   "DISK2/BIGPIC.TLB", "DISK2/COMSPR.TLB", "DISK2/CPIC.TLB",
        "DISK2/DUNGCOM.TLB", "DISK2/PICA.TLB", "DISK2/PICB.TLB",   "DISK2/PICC.TLB",
        "DISK2/SPRIT.TLB",  "DISK2/TOPVIEW.TLB", "DISK2/WILDCOM.TLB", "DISK3/CBODY.TLB",
        "DISK3/FINAL.TLB",  "DISK3/FRAME.TLB",  "DISK3/GEN.TLB",
    };
    size_t total = 0;
    for (size_t f = 0; f < sizeof files / sizeof *files; ++f) {
        char path[64];
        snprintf(path, sizeof path, "DQKAssets/%s", files[f]);
        uint8_t *data;
        size_t size;
        hlib_status status = hlib_read_file(path, &data, &size);
        if (status == HLIB_IO_ERROR && f == 0) {
            puts("test_vga: no DQKAssets/: The Dark Queen of Krynn's pictures skipped");
            return;
        }
        CHECK(status == HLIB_OK);
        total += decode_all(data, size, path);
        free(data);
    }
    CHECK(total == 1827);
}

/* The combat libraries' pictures: DUNGCOM's and WILDCOM's 24 by 24 tiles
 * (format 0x10, their sets nested) and CPIC's icons (0x15). Each picture's
 * hash (its width and height, then each pixel, 256 where transparent) is
 * summed over the pictures, walked as tools/hlib.py walks them (index
 * records past an id map, nested libraries in turn), with their count. The
 * sums are tools/hlib.py's: with it, walk each library the same way and
 * hash each picture's rows (picture(), None as 256) as this does, with
 * FNV-1a's prime 1099511628211 and 64-bit wrap-around (DEVELOPERS.md). */
static uint64_t hash_picture(const cok_vga_image *image)
{
    uint64_t v = (uint64_t)image->width * 1000003u + image->height;
    for (size_t j = 0; j < image->height; ++j)
        for (size_t c = 0; c < image->width; ++c) {
            size_t k = j * image->width + c;
            unsigned p = image->mask != NULL && image->mask[k] != 0 ? 256u : image->pixels[k];
            v = v * 1099511628211u + p;
        }
    return v;
}

static void sum_pictures(const uint8_t *data, size_t size, uint64_t *sum, size_t *count)
{
    hlib_library lib;
    CHECK(hlib_parse(data, size, &lib) == HLIB_OK);
    for (size_t i = (lib.flags & HLIB_ID_MAP) ? 1 : 0; i < lib.count; ++i) {
        const uint8_t *record;
        size_t n;
        CHECK(hlib_record_at(&lib, i, &record, &n) == HLIB_OK);
        if (hlib_is_library(record, n)) {
            sum_pictures(record, n, sum, count);
            continue;
        }
        if (!cok_vga_is_picture(record, n)) continue;
        cok_vga_image image = {0};
        CHECK(cok_vga_decode(record, n, &lib, &image) == COK_VGA_OK);
        *sum += hash_picture(&image);
        ++*count;
        cok_vga_free(&image);
    }
}

static void test_combat(void)
{
    static const struct {
        const char *file;
        size_t count;
        uint64_t sum;
    } libs[] = {
        {"DISK2/DUNGCOM.TLB", 136, UINT64_C(0xcbaf1ae41cc79d8e)},
        {"DISK2/WILDCOM.TLB", 160, UINT64_C(0x69ca5d4bb0a1e798)},
        {"DISK2/CPIC.TLB", 190, UINT64_C(0x89684a0435cc6a15)},
    };
    for (size_t f = 0; f < sizeof libs / sizeof *libs; ++f) {
        char path[64];
        snprintf(path, sizeof path, "DQKAssets/%s", libs[f].file);
        uint8_t *data;
        size_t size;
        hlib_status status = hlib_read_file(path, &data, &size);
        if (status == HLIB_IO_ERROR && f == 0) {
            puts("test_vga: no DQKAssets/: the combat pictures skipped");
            return;
        }
        CHECK(status == HLIB_OK);
        uint64_t sum = 0;
        size_t count = 0;
        sum_pictures(data, size, &sum, &count);
        free(data);
        CHECK(count == libs[f].count && sum == libs[f].sum);
    }
}

int main(void)
{
    test_library();
    test_pictures();
    test_composed();
    test_palette();
    test_cycles();
    test_draw();
    test_game();
    test_combat();
    puts("test_vga: ok");
    return 0;
}
