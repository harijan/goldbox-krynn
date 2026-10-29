#include "data_dark_queen_modify.h"

/* The tables Modify Character (6a87) and the spell book it rebuilds read, as
 * the original's data segment (DS 0x422e in build/DQK_FULL.EXE) holds them;
 * tests/test_queen_modify.c checks every byte against the executable. */

/* DS:02b8: by race, 16 bytes: the least and most of strength by gender (+0
 * and +1 the least, +2 and +3 the most), the most exceptional strength by
 * gender (+4, +5), then the least and most of intelligence, wisdom,
 * dexterity, constitution and charisma. */
const uint8_t cok_queen_limits[7][16] = {
    {3, 3, 18, 16, 75, 0, 10, 18, 6, 18, 7, 19, 6, 18, 12, 18},
    {7, 7, 18, 16, 75, 0, 8, 18, 6, 18, 7, 19, 7, 18, 8, 18},
    {3, 3, 18, 17, 90, 0, 4, 18, 3, 18, 6, 18, 6, 18, 3, 18},
    {8, 8, 18, 17, 99, 0, 3, 18, 3, 18, 3, 17, 12, 19, 3, 16},
    {9, 9, 18, 17, 99, 0, 3, 18, 3, 18, 3, 17, 14, 19, 3, 12},
    {6, 6, 16, 16, 0, 0, 6, 18, 3, 16, 8, 19, 10, 18, 6, 18},
    {3, 3, 18, 18, 100, 50, 3, 18, 3, 18, 3, 18, 3, 18, 3, 18}
};

/* DS:04fd: by class (0-16), the least of each score a class takes. */
const uint8_t cok_queen_minimums[17][6] = {
    {6, 6, 9, 0, 6, 6},
    {10, 7, 10, 8, 10, 0},
    {9, 0, 6, 6, 7, 6},
    {12, 9, 13, 6, 9, 17},
    {13, 13, 14, 6, 14, 6},
    {3, 9, 6, 6, 6, 6},
    {6, 6, 0, 9, 6, 6},
    {10, 7, 10, 8, 10, 6},
    {9, 6, 9, 6, 7, 6},
    {9, 9, 9, 6, 7, 6},
    {13, 13, 14, 6, 14, 6},
    {6, 9, 9, 6, 6, 6},
    {6, 6, 9, 9, 6, 6},
    {9, 9, 6, 6, 6, 6},
    {9, 6, 6, 9, 6, 6},
    {9, 9, 6, 9, 6, 6},
    {6, 9, 6, 9, 6, 6}
};

/* DS:082d: the hit points a constitution adds a level (dqk 86b8:3a5b), by
 * score, 32 entries. */
const uint8_t cok_queen_constitution_bonus[32] = {
    47, 47, 47, 254, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1,
    255, 255, 2, 3
};

/* DS:27e9, 0x16 bytes a spell by its id (1-126; entry 0 is not a spell's):
 * the class (a word: 2 the mage's) and the level (a byte at +2). */
const uint16_t cok_queen_spell_class[127] = {
    12846, 0, 255, 0, 255, 0, 0, 255, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
    0, 2, 2, 2, 2, 2, 2, 2, 0, 0, 255, 0, 255, 0, 0, 0, 255, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 255,
    3, 0, 3, 3, 3, 3, 3, 3, 4, 255, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 1, 2, 2, 2, 2, 4, 1, 4, 1, 4, 2, 0, 0, 255, 255, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
};
const uint8_t cok_queen_spell_level[127] = {
    155, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 6, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 6, 0, 4, 0, 0, 0,
    0, 0, 0, 6, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 1, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 5, 5,
    5, 5, 6, 2, 6, 2, 6, 4, 6, 7, 7, 7, 7, 3, 3, 3, 3, 6, 6, 6, 6, 6, 7, 7, 7, 5, 5, 8, 8, 8, 8,
    9, 9, 9
};
