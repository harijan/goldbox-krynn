#include "screen.h"

/* Tile values from the data segment, added to 0x14. */
static const uint8_t top_row[40] = { /* DS:0e3a */
    0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const uint8_t side[24] = { /* DS:0e62 */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0, 3, 4, 4, 4, 4, 5, 0,
};
static const uint8_t divider[17] = { /* DS:0e7a */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0,
};
static const uint8_t box_side[13] = { /* DS:0ea3 and DS:0eb0, rows 2-14 */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0,
};

/* One frame tile (6e22:01ab, unmasked). */
static void tile(cok_picture *dst, const cok_picture *tiles, unsigned value, int x, int y)
{
    cok_picture_draw(dst, tiles, 0x14 + value, x, y, 0, NULL);
}

/* Row 0 with the moons. */
static void top(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3])
{
    for (int x = 0; x < 40; ++x) {
        unsigned value = top_row[x];
        if (x == 8) value = (moons[0] + 10u) & 0xff;
        if (x == 19) value = (moons[1] + 6u) & 0xff;
        if (x == 30) value = (moons[2] + 14u) & 0xff;
        tile(dst, tiles, value, x, 0);
    }
}

void cok_screen_frame(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3],
                      bool open)
{
    cok_picture_fill(dst, 1, 8, 38, 22 * 8, 0); /* 1128:07e6 */
    top(dst, tiles, moons);
    for (int y = 0; y < 24; ++y) {
        unsigned value = open && y >= 15 && y <= 17 ? COK_FRAME_VERTICAL : side[y];
        tile(dst, tiles, value, 0, y);
        tile(dst, tiles, value, 39, y);
    }
    for (int x = 0; x < 40; ++x) tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 23);
}

static void row_16(cok_picture *dst, const cok_picture *tiles)
{
    for (int x = 0; x < 40; ++x) tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 16);
}

void cok_screen_adventure(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3])
{
    cok_screen_frame(dst, tiles, moons, false);
    row_16(dst, tiles);
    for (int y = 0; y <= 16; ++y) tile(dst, tiles, divider[y], 16, y);
    for (int i = 2; i <= 14; ++i) {
        tile(dst, tiles, COK_FRAME_HORIZONTAL, i, 2);
        tile(dst, tiles, COK_FRAME_HORIZONTAL, i, 14);
    }
    for (int y = 2; y <= 14; ++y) {
        tile(dst, tiles, box_side[y - 2], 2, y);
        tile(dst, tiles, box_side[y - 2], 14, y);
    }
}

void cok_screen_big(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3])
{
    cok_screen_frame(dst, tiles, moons, false);
    row_16(dst, tiles);
}

void cok_screen_spells(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3])
{
    cok_picture_fill(dst, 1, 8, 38, 16 * 8, 0); /* 1128:07e6 */
    top(dst, tiles, moons);
    for (int y = 0; y < 24; ++y) {
        tile(dst, tiles, side[y], 0, y);
        tile(dst, tiles, side[y], 39, y);
    }
    for (int x = 0; x < 40; ++x) tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 23);
    row_16(dst, tiles);
}

void cok_screen_list(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3])
{
    cok_screen_frame(dst, tiles, moons, true);
    for (int x = 0; x < 40; ++x) tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 2);
    tile(dst, tiles, 3, 0, 3);
    tile(dst, tiles, 3, 39, 3);
}

static const uint8_t combat_side[23] = { /* DS:0ebd, 0ed4 and 0eeb */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0,
};

void cok_screen_combat(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3])
{
    cok_picture_fill(dst, 0, 0, 40, 24 * 8, 0); /* 1128:07e6 */
    top(dst, tiles, moons);
    for (int y = 0; y <= 22; ++y) {
        tile(dst, tiles, combat_side[y], 0, y);
        tile(dst, tiles, combat_side[y], 22, y);
        tile(dst, tiles, combat_side[y], 39, y);
    }
    for (int x = 0; x < 40; ++x) tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 22);
}

static const uint8_t sheet_side[23] = { /* DS:0f02 */
    0, 3, 4, 4, 4, 4, 4, 5, 0, 3, 4, 4, 4, 4, 4, 5, 0, 3, 4, 5, 0, 3, 5,
};
static const uint8_t sheet_middle[11] = { /* DS:0f19-0f23, rows 9-19 */
    3, 4, 4, 4, 4, 4, 5, 0, 3, 4, 5,
};

void cok_screen_sheet(cok_picture *dst, const cok_picture *tiles, const uint16_t moons[3])
{
    cok_picture_fill(dst, 0, 0, 40, 24 * 8, 0); /* 1128:07e6 */
    for (int x = 0; x < 40; ++x) {
        unsigned value = top_row[x];
        if (x == 8) value = (moons[0] + 10u) & 0xff;
        if (x == 19) value = (moons[1] + 6u) & 0xff;
        if (x == 30) value = (moons[2] + 14u) & 0xff;
        tile(dst, tiles, value, x, 0);
        tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 8);
        tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 16);
        tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 20);
        tile(dst, tiles, COK_FRAME_HORIZONTAL, x, 23);
    }
    for (int y = 0; y <= 22; ++y) {
        tile(dst, tiles, sheet_side[y], 0, y);
        tile(dst, tiles, sheet_side[y], 39, y);
    }
    for (int y = 9; y <= 19; ++y) tile(dst, tiles, sheet_middle[y - 9], 19, y);
}
