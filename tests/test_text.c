#include "text.h"
#include "dax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* 64 glyphs; glyph g has every row set to g, except space (32), which is blank. */
static cok_font numbered_font(void)
{
    uint8_t data[64 * 8];
    for (size_t i = 0; i < sizeof data; ++i) data[i] = (uint8_t)(i / 8);
    memset(data + 32 * 8, 0, 8);
    cok_font font = {0};
    CHECK(cok_font_load(&font, data, sizeof data) == COK_PICTURE_OK);
    return font;
}

static cok_picture screen(size_t units, size_t height)
{
    cok_picture picture = {0};
    CHECK(cok_picture_create(&picture, units, height, 1, 0) == COK_PICTURE_OK);
    return picture;
}

/* Whether every byte of cell (x, y) equals byte. */
static int cell_is(const cok_picture *picture, size_t x, size_t y, uint8_t byte)
{
    for (size_t row = 0; row < 8; ++row)
        for (size_t i = 0; i < 4; ++i)
            if (picture->pixels[(y * 8 + row) * picture->units * 4 + x * 4 + i] != byte) return 0;
    return 1;
}

static void test_font_load(void)
{
    cok_font font = {0};
    const uint8_t data[16] = {1};
    CHECK(cok_font_load(&font, data, 16) == COK_PICTURE_OK);
    CHECK(font.count == 2 && font.glyphs != NULL && font.glyphs[0] == 1);
    cok_font_free(&font);
    CHECK(font.glyphs == NULL && font.count == 0);
    cok_font_free(&font);
    CHECK(cok_font_load(&font, data, 0) == COK_PICTURE_INVALID);
    CHECK(cok_font_load(&font, data, 12) == COK_PICTURE_INVALID);
    CHECK(cok_font_load(&font, NULL, 8) == COK_PICTURE_INVALID);
    CHECK(font.glyphs == NULL);
}

static void test_index(void)
{
    CHECK(cok_text_index('A') == 1 && cok_text_index('a') == 1);
    CHECK(cok_text_index('Z') == 26 && cok_text_index('z') == 26);
    CHECK(cok_text_index(' ') == 32 && cok_text_index('0') == 48);
    CHECK(cok_text_index('?') == 63 && cok_text_index('@') == 0);
    CHECK(cok_text_index('{') == 59); /* Not a letter: modulo 64 only. */
    CHECK(cok_text_index((char)0xe1) == 33);
}

static void test_glyph_pixels(void)
{
    cok_picture picture = screen(2, 16);
    uint8_t data[8] = {0x80, 0x01, 0xa5, 0xff, 0x00, 0x0f, 0xf0, 0x3c};
    cok_font font = {0};
    CHECK(cok_font_load(&font, data, 8) == COK_PICTURE_OK);
    cok_text_glyph(&picture, &font, 0, 1, 1, 1, 0x1e, 0x23);
    /* Foreground 14 on set bits, background 3, left pixel in the high nibble. */
    const uint8_t want[8][4] = {
        {0xe3, 0x33, 0x33, 0x33}, {0x33, 0x33, 0x33, 0x3e}, {0xe3, 0xe3, 0x3e, 0x3e},
        {0xee, 0xee, 0xee, 0xee}, {0x33, 0x33, 0x33, 0x33}, {0x33, 0x33, 0xee, 0xee},
        {0xee, 0xee, 0x33, 0x33}, {0x33, 0xee, 0xee, 0x33},
    };
    for (size_t row = 0; row < 8; ++row)
        CHECK(memcmp(picture.pixels + (8 + row) * 8 + 4, want[row], 4) == 0);
    CHECK(cell_is(&picture, 0, 0, 0) && cell_is(&picture, 1, 0, 0) && cell_is(&picture, 0, 1, 0));
    cok_font_free(&font);
    cok_picture_free(&picture);
}

static void test_glyph_runs_and_clipping(void)
{
    cok_font font = numbered_font();
    cok_picture picture = screen(4, 20); /* 4 columns, 2 whole cell rows. */
    cok_text_glyph(&picture, &font, 0xff, 1, 0, 0, 15, 0); /* Past the font. */
    cok_text_glyph(&picture, &font, 63, 0, 0, 0, 15, 0);   /* Zero count. */
    cok_text_glyph(&picture, &font, 63, 1, 0, 2, 15, 0);   /* Partial cell row. */
    cok_text_glyph(&picture, &font, 63, 1, 0, -1, 15, 0);
    for (size_t i = 0; i < picture.frame_size; ++i) CHECK(picture.pixels[i] == 0);

    /* Glyph 63 is 0x3f per row: two background pixels, then six foreground. */
    cok_text_glyph(&picture, &font, 63, 5, -2, 1, 15, 4);
    CHECK(cell_is(&picture, 0, 0, 0));
    for (size_t x = 0; x < 3; ++x) {
        const uint8_t *row = picture.pixels + 8 * 16 + x * 4;
        CHECK(row[0] == 0x44 && row[1] == 0xff && row[2] == 0xff && row[3] == 0xff);
    }
    CHECK(cell_is(&picture, 3, 1, 0));
    cok_text_glyph(&picture, &font, 32, 300, 0, 1, 15, 2); /* Long run stops at the edge. */
    for (size_t x = 0; x < 4; ++x) CHECK(cell_is(&picture, x, 1, 0x22));
    CHECK(picture.pixels[16 * 16] == 0);
    cok_picture_free(&picture);
    cok_font_free(&font);

    cok_font empty = {0};
    picture = screen(1, 8);
    cok_text_glyph(&picture, &empty, 0, 1, 0, 0, 15, 15);
    CHECK(cell_is(&picture, 0, 0, 0));
    cok_picture_free(&picture);
}

static void test_clear_and_string(void)
{
    cok_font font = numbered_font();
    cok_picture picture = screen(4, 8);
    cok_text_clear(&picture, &font, 2, 1, 0, 0x19);
    CHECK(cell_is(&picture, 0, 0, 0) && cell_is(&picture, 1, 0, 0x99));
    CHECK(cell_is(&picture, 2, 0, 0x99) && cell_is(&picture, 3, 0, 0));

    /* 'a' is clipped; 'B' through 'e' map to glyphs 2-5, each row equal to the index. */
    cok_text_string(&picture, &font, "aBcde", -1, 0, 7, 0);
    CHECK(picture.pixels[0] == 0 && picture.pixels[3] == 0x70);
    CHECK(picture.pixels[4 + 3] == 0x77 && picture.pixels[8 + 2] == 0x07);
    CHECK(picture.pixels[12 + 2] == 0x07 && picture.pixels[12 + 3] == 0x07);
    memset(picture.pixels, 0, picture.frame_size);
    cok_text_string(&picture, &font, "", 0, 0, 7, 0);
    cok_text_string(&picture, &font, "AB", 4, 0, 7, 0);
    cok_text_string(&picture, &font, "AB", -2, 0, 7, 0);
    for (size_t i = 0; i < picture.frame_size; ++i) CHECK(picture.pixels[i] == 0);
    cok_picture_free(&picture);
    cok_font_free(&font);
}

/* Characters in cells x..x+count-1 of row y for numbered_font drawn with a
 * nonzero foreground on background 0: letters print as capitals, other glyphs
 * as their ASCII code, and blank cells as spaces. */
static void row_text(const cok_picture *picture, int x, int y, size_t count, char *out)
{
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *d = picture->pixels + (size_t)y * 8 * picture->units * 4 +
                           ((size_t)x + i) * 4;
        unsigned glyph = 0;
        for (size_t j = 0; j < 4; ++j)
            glyph |= (d[j] >> 4 ? 2u : 0u) << (6 - 2 * j) | (d[j] & 15 ? 1u : 0u) << (6 - 2 * j);
        out[i] = glyph == 0 ? ' ' : glyph <= 26 ? (char)('A' + glyph - 1) : (char)glyph;
    }
    out[count] = '\0';
}

static int row_is(const cok_picture *picture, int x, int y, const char *expected)
{
    char text[64];
    row_text(picture, x, y, strlen(expected), text);
    if (strcmp(text, expected) == 0) return 1;
    fprintf(stderr, "row %d: \"%s\", expected \"%s\"\n", y, text, expected);
    return 0;
}

typedef struct {
    const cok_picture *picture;
    int pages, delays;
} wrap_log;

static void count_delay(void *context)
{
    ++((wrap_log *)context)->delays;
}

static void check_page(void *context)
{
    wrap_log *log = context;
    ++log->pages;
    CHECK(row_is(log->picture, 0, 0, "AA   ") && row_is(log->picture, 0, 1, "BB   "));
    CHECK(row_is(log->picture, 0, 24, "PRESS ANY KEY TO CONTINUE    "));
    CHECK(log->picture->pixels[24 * 8 * 160 + 4 * 1 + 3] == 0xd0);
}

static void test_wrap(void)
{
    cok_font font = numbered_font();
    cok_picture picture = screen(40, 200);
    wrap_log log = {&picture, 0, 0};
    const cok_text_hooks hooks = {check_page, count_delay, &log};

    /* Whole words move to the next line and keep their trailing space. */
    cok_text_cursor cursor = {0, 0};
    cok_text_wrap(&picture, &font, &cursor, "The quick brown fox jumps over",
                  (cok_text_window){1, 1, 10, 3}, 15, 0, false, &hooks);
    CHECK(row_is(&picture, 0, 1, " THE QUICK  ") && row_is(&picture, 0, 2, " BROWN FOX  "));
    CHECK(row_is(&picture, 0, 3, " JUMPS OVER "));
    CHECK(cursor.x == 1 && cursor.y == 4 && log.delays == 30 && log.pages == 0);

    /* Punctuation ends a word without a space and stays with it. */
    memset(picture.pixels, 0, picture.frame_size);
    cursor = (cok_text_cursor){0, 0};
    cok_text_wrap(&picture, &font, &cursor, "ab cd-ef.", (cok_text_window){0, 0, 4, 5}, 15, 0,
                  false, NULL);
    CHECK(row_is(&picture, 0, 0, "AB   ") && row_is(&picture, 0, 1, "CD-  "));
    CHECK(row_is(&picture, 0, 2, "EF.  ") && cursor.x == 3 && cursor.y == 2);

    /* A cursor inside the window continues; clear blanks the window and homes it. */
    cursor = (cok_text_cursor){3, 2};
    cok_text_wrap(&picture, &font, &cursor, "g", (cok_text_window){0, 0, 4, 5}, 15, 0, false,
                  NULL);
    CHECK(row_is(&picture, 0, 2, "EF.G ") && cursor.x == 4 && cursor.y == 2);
    cok_text_wrap(&picture, &font, &cursor, "h", (cok_text_window){0, 0, 4, 1}, 15, 0, true,
                  NULL);
    CHECK(row_is(&picture, 0, 0, "H    ") && row_is(&picture, 0, 1, "     "));
    CHECK(row_is(&picture, 0, 2, "EF.G ") && cursor.x == 1 && cursor.y == 0);

    /* A full window prompts, waits, clears and continues at its top left. */
    memset(picture.pixels, 0, picture.frame_size);
    cursor = (cok_text_cursor){0, 0};
    log.delays = 0;
    cok_text_wrap(&picture, &font, &cursor, "aa bb cc dd", (cok_text_window){0, 0, 4, 1}, 15,
                  0, false, &hooks);
    CHECK(log.pages == 1 && log.delays == 11);
    CHECK(row_is(&picture, 0, 0, "CC DD") && row_is(&picture, 0, 1, "     "));
    CHECK(cursor.x == 0 && cursor.y == 1);

    /* Windows past 40x25 cells print nothing; bottom may reach row 39. */
    memset(picture.pixels, 0, picture.frame_size);
    const cok_text_window bad[] = {{0, 0, 40, 1}, {0, 25, 4, 30}, {3, 0, 2, 1}, {-1, 0, 4, 1},
                                   {0, 0, 4, 40}};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; ++i) {
        cursor = (cok_text_cursor){7, 7};
        cok_text_wrap(&picture, &font, &cursor, "x", bad[i], 15, 0, true, NULL);
        CHECK(cursor.x == 7 && cursor.y == 7);
    }
    for (size_t i = 0; i < picture.frame_size; ++i) CHECK(picture.pixels[i] == 0);
    cok_text_wrap(&picture, &font, &cursor, "x", (cok_text_window){0, 20, 4, 39}, 15, 0, false,
                  NULL);
    CHECK(row_is(&picture, 0, 20, "X") && cursor.x == 1 && cursor.y == 20);
    cok_picture_free(&picture);
    cok_font_free(&font);
}

static void test_game_font(void)
{
    dax_archive archive = {0};
    dax_record record;
    CHECK(dax_open("Assets/8X8D1.DAX", &archive) == DAX_OK);
    CHECK(dax_record_at(&archive, 0, &record) == DAX_OK && record.id == 201);
    uint8_t *data = malloc(record.decoded_size);
    CHECK(data != NULL);
    CHECK(dax_decode(record.packed, record.packed_size, data, record.decoded_size) == DAX_OK);
    cok_font font = {0};
    CHECK(cok_font_load(&font, data, record.decoded_size) == COK_PICTURE_OK);
    CHECK(font.count == 177);
    const uint8_t a[8] = {0x0f, 0x36, 0x66, 0x7e, 0x66, 0x36, 0x77, 0x00};
    const uint8_t zero[8] = {0x1c, 0x36, 0x36, 0x36, 0x36, 0x36, 0x1c, 0x00};
    const uint8_t blank[8] = {0};
    CHECK(memcmp(font.glyphs + cok_text_index('a') * 8, a, 8) == 0);
    CHECK(memcmp(font.glyphs + cok_text_index('0') * 8, zero, 8) == 0);
    CHECK(memcmp(font.glyphs + cok_text_index(' ') * 8, blank, 8) == 0);
    for (char c = 'A'; c <= 'Z'; ++c)
        CHECK(memcmp(font.glyphs + cok_text_index(c) * 8, blank, 8) != 0);

    cok_picture picture = screen(40, 200);
    cok_text_string(&picture, &font, "0", 39, 24, 15, 1);
    CHECK(picture.pixels[192 * 160 + 156] == 0x11 && picture.pixels[192 * 160 + 157] == 0x1f);
    cok_picture_free(&picture);
    cok_font_free(&font);
    free(data);
    dax_close(&archive);
}

int main(void)
{
    test_font_load();
    test_index();
    test_glyph_pixels();
    test_glyph_runs_and_clipping();
    test_clear_and_string();
    test_wrap();
    test_game_font();
    puts("Text tests passed");
    return 0;
}
