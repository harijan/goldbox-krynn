/* The data segment's tables (src/data.h, Champions' in src/rules.c and
 * src/data_champions.c): every table the descriptor lists against the
 * original's data segment in build/START_FULL.EXE; every named table held
 * by those tables, its elements within its own bytes; each named lookup's
 * arithmetic against the expression the code had before it, written out
 * again here, over the whole range of its indices, the ones the original
 * reads past a table too; the reads that run on from one table into the
 * next; the names; and the assertions that stop a wrong access. */
#include "combat.h"
#include "rules.h"
#include "step.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_data_segment *D;

/* Every byte of every table the descriptor lists is the executable's at
 * its DS offset (DS 0x1bc6), if make merged has built it. */
static void test_executable(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("data: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    size_t bytes = 0;
    for (size_t i = 0; i < D->table_count; ++i) {
        const cok_ds_table *t = &D->tables[i];
        CHECK(t->size > 0 && (t->stride == 1 || t->stride == 16));
        for (size_t k = 0; k < t->size; ++k) {
            size_t at = ds + t->offset + k * t->stride;
            CHECK(at < size && exe[at] == t->bytes[k]);
            ++bytes;
        }
    }
    /* Every byte a lookup reads, by its DS offset, is the executable's. */
    for (unsigned at = 0; at <= 0xffff; ++at) {
        uint8_t byte;
        if (cok_data_byte(D, (uint16_t)at, &byte)) CHECK(exe[ds + at] == byte);
    }
    printf("data: %zu tables, %zu bytes checked against build/START_FULL.EXE\n", D->table_count,
           bytes);
}

/* The list's order, which the lookup's search relies on: the tables with
 * a stride of 1 first, by DS offset; then the columns. Where two tables
 * hold the same byte they agree (one table's copy runs on into another's),
 * so that whichever a lookup finds it in reads the same. */
static void test_order(void)
{
    size_t columns = 0;
    while (columns < D->table_count && D->tables[columns].stride == 1) ++columns;
    CHECK(columns > 0);
    for (size_t i = 1; i < columns; ++i) CHECK(D->tables[i - 1].offset <= D->tables[i].offset);
    for (size_t i = columns; i < D->table_count; ++i) CHECK(D->tables[i].stride != 1);
    static int seen[0x10000];
    for (size_t i = 0; i < 0x10000; ++i) seen[i] = -1;
    for (size_t i = 0; i < D->table_count; ++i) {
        const cok_ds_table *t = &D->tables[i];
        for (size_t k = 0; k < t->size; ++k) {
            size_t at = t->offset + k * t->stride;
            CHECK(at <= 0xffff);
            CHECK(seen[at] < 0 || seen[at] == t->bytes[k]);
            seen[at] = t->bytes[k];
        }
    }
    /* Every byte held is found, as that table holds it. */
    for (size_t at = 0; at <= 0xffff; ++at) {
        uint8_t byte;
        CHECK(cok_data_byte(D, (uint16_t)at, &byte) == (seen[at] >= 0));
        if (seen[at] >= 0) CHECK(byte == seen[at]);
    }
}

/* The tables with a stride of 1 cover DS:31b3-43bf without a gap, and
 * nothing past it: the stat routines' lookups fail there as the original's
 * initialized data ends. */
static void test_coverage(void)
{
    uint8_t byte;
    for (unsigned at = 0x31b3; at <= 0x43bf; ++at) CHECK(cok_data_byte(D, (uint16_t)at, &byte));
    for (unsigned at = 0x43c0; at <= 0xffff; ++at) CHECK(!cok_data_byte(D, (uint16_t)at, &byte));
    CHECK(!cok_data_byte(D, 0x0395, &byte) && !cok_data_byte(D, 0x2097, &byte));
    CHECK(!cok_data_byte(D, 0x31b2, &byte));
}

/* Each named table: present, its elements within its own bytes, which the
 * game's tables hold. */
static void test_shapes(void)
{
    for (unsigned t = 0; t < COK_DATA_TABLES; ++t) {
        const cok_data_table *s = &D->table[t];
        if (s->width == 0 && D != cok_rules_champions.data) continue; /* a table it has not */
        CHECK(s->width == 1 || s->width == 2 || s->width == 4 || t <= COK_DATA_ITEM_WORDS);
        CHECK(s->indices >= 1 && s->indices <= 3 && s->size > 0);
        uint8_t byte;
        for (unsigned k = 0; k < s->size; ++k)
            CHECK(cok_data_byte(D, (uint16_t)(s->from + k), &byte));
        long at[3] = {0, 0, 0};
        for (unsigned n = 0; n < s->indices; ++n) {
            CHECK(s->index[n].count > 0 && s->index[n].stride > 0);
            CHECK(cok_data_first(D, (cok_data)t, n) == (size_t)s->index[n].first);
            CHECK(cok_data_count(D, (cok_data)t, n) == s->index[n].count);
        }
        /* The first and the last element, and every one between. */
        unsigned total = 1;
        for (unsigned n = 0; n < s->indices; ++n) total *= s->index[n].count;
        for (unsigned e = 0; e < total; ++e) {
            unsigned rest = e;
            for (int n = s->indices - 1; n >= 0; --n) {
                at[n] = s->index[n].first + (long)(rest % s->index[n].count);
                rest /= s->index[n].count;
            }
            long offset = cok_data_offset_(D, (cok_data)t, s->indices, at[0], at[1], at[2]);
            CHECK(cok_data_holds(D, (cok_data)t, offset));
            CHECK(cok_data_holds(D, (cok_data)t, offset + s->width - 1));
            if (e == 0) CHECK(offset >= s->from);
        }
    }
    /* The names lie within the name strings (but Death Knights' item
     * words, which lie apart). */
    for (unsigned t = COK_DATA_CLASS_NAMES; t <= COK_DATA_ITEM_WORDS; ++t) {
        const cok_data_table *s = &D->table[t];
        CHECK(s->width == s->index[0].stride);
        if (t == COK_DATA_ITEM_WORDS && D != cok_rules_champions.data) continue;
        CHECK(cok_data_holds(D, COK_DATA_NAMES, s->from));
        CHECK(cok_data_holds(D, COK_DATA_NAMES, s->from + s->size - 1));
    }
}

/* The arithmetic the code had before the descriptor, as it had it. */
static void test_arithmetic(void)
{
    /* The spell table, any byte id (magic.c's id & 0x7f, the AI's and the
     * combat commands' any byte, items.c's int). */
    for (long spell = -2; spell <= 0x1ff; ++spell)
        for (long column = 0; column < 16; ++column) {
            CHECK(cok_data_offset2(D, COK_DATA_SPELLS, spell, column) ==
                  0x31b3 + 16 * spell + column);
            CHECK((uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, column) ==
                  (uint16_t)(0x31b3u + 16u * (unsigned)spell + (unsigned)column));
        }
    for (int spell = -2; spell <= 0x1ff; ++spell) {
        CHECK((uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, 11) ==
              (uint16_t)(0x31beu + 16u * (unsigned)spell));
        CHECK((uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, 1) ==
              (uint16_t)(0x31b4 + 16 * spell));
    }
    /* THAC0 by class and level, signed levels; the class bits; training's
     * bits and the top levels; the icon's colours. */
    for (long class_ = -1; class_ <= 17; ++class_) {
        for (long level = -128; level <= 127; ++level)
            CHECK(cok_data_offset2(D, COK_DATA_THAC0, class_, level) ==
                  0x3882 + class_ * 13 + level);
        CHECK(cok_data_offset(D, COK_DATA_CLASS_BITS, class_) == 0x38ea + class_);
        CHECK(cok_data_offset(D, COK_DATA_TRAINING_BITS, class_) == 0x38f2 + class_);
        CHECK(cok_data_offset(D, COK_DATA_TOP_LEVELS, class_) == 0x3903 + class_);
        CHECK(cok_data_offset(D, COK_DATA_ICON_COLOURS, class_) == 0x390a + class_);
    }
    /* The thief skills: signed levels and races, dexterity a byte. */
    for (long skill = 0; skill <= 9; ++skill) {
        for (long n = -128; n <= 255; ++n) {
            CHECK(cok_data_offset2(D, COK_DATA_THIEF_LEVEL, n, skill) == 0x3908 + n * 8 + skill);
            CHECK(cok_data_offset2(D, COK_DATA_THIEF_RACE, n, skill) == 0x3970 + n * 8 + skill);
            CHECK(cok_data_offset2(D, COK_DATA_THIEF_DEXTERITY, n, skill) ==
                  0x397b + n * 5 + skill);
        }
    }
    /* Creating and modifying: by a signed race or class id. */
    for (long n = -128; n <= 127; ++n) {
        for (long k = 0; k < 16; ++k)
            CHECK(cok_data_offset2(D, COK_DATA_SCORE_LIMITS, n, k) == 0x39e0 + n * 16 + k);
        for (long k = 0; k < 15; ++k)
            CHECK(cok_data_offset2(D, COK_DATA_RACE_CLASSES, n, k) == 0x3a50 + n * 15 + k);
        for (long class_ = -128; class_ <= 7; ++class_)
            CHECK((uint16_t)cok_data_offset2(D, COK_DATA_AGE_DICE, n, class_) ==
                  (uint16_t)((uint16_t)(0x3ab9 + n * 32) + class_ * 4));
        for (long k = 0; k < 4; ++k)
            CHECK(cok_data_offset2(D, COK_DATA_AGES, n, k) == 0x3b99 + n * 10 + 2 * k);
        for (long a = 0; a < 6; ++a)
            CHECK(cok_data_offset2(D, COK_DATA_CLASS_MINIMUMS, n, a) == 0x3bdf + n * 6 + a);
        CHECK(cok_data_offset(D, COK_DATA_CLASS_ALIGNMENTS, n) == 0x3c45 + n);
    }
    for (long con = 0; con <= 255; ++con)
        CHECK(cok_data_offset(D, COK_DATA_CONSTITUTION, con) == 0x3c53 + con);
    /* Experience by row (any byte: 0x74 is training's stale row) and
     * level, then a byte of the LongInt. */
    for (long row = 0; row <= 255; ++row)
        for (long level = -130; level <= 130; ++level)
            for (long i = 0; i < 4; ++i)
                CHECK(((cok_data_offset2(D, COK_DATA_EXPERIENCE, row, level) + i) & 0xffff) ==
                      ((0x3c65 + 99L * row + 4L * level + i) & 0xffff));
    /* Spells a day, saving throws (levels signed), the spell lists. */
    for (long table = 0; table <= 9; ++table)
        for (long level = -128; level <= 127; ++level)
            for (long k = 0; k <= 5; ++k) {
                CHECK(cok_data_offset3(D, COK_DATA_SPELLS_A_DAY, table, level, k) ==
                      0x3c8e + table * 99 + level * 5 + k);
                CHECK(cok_data_offset3(D, COK_DATA_SAVING_THROWS, table, level, k) ==
                      0x4056 + table * 60 + level * 5 + k);
            }
    for (long list = 0; list <= 4; ++list)
        for (long level = 0; level <= 5; ++level)
            for (long k = 0; k <= 11; ++k)
                CHECK(cok_data_offset3(D, COK_DATA_SPELL_LISTS, list, level, k) ==
                      0x4208 + list * 40 + level * 10 + k);
    for (long level = 1; level <= 4; ++level)
        for (long k = 1; k <= 10; ++k) {
            /* party.c's cleric spells, create.c's first level, treasure's
             * scrolls by order. */
            CHECK(cok_data_offset3(D, COK_DATA_SPELL_LISTS, 1, level, k) ==
                  0x4230 + level * 10 + k);
            CHECK(cok_data_offset3(D, COK_DATA_SPELL_LISTS, 1, 1, k) == 0x423a + k);
            CHECK(cok_data_offset3(D, COK_DATA_SPELL_LISTS, 1, level, k) ==
                  0x423a + 10 * (level - 1) + k);
            CHECK(cok_data_offset3(D, COK_DATA_SPELL_LISTS, 2, level, k) ==
                  0x4262 + 10 * (level - 1) + k);
            CHECK(cok_data_offset3(D, COK_DATA_SPELL_LISTS, 3, level, k) ==
                  0x428a + 10 * (level - 1) + k);
        }
    /* The names, by a signed byte as the sheet reads them, or a byte. */
    static const struct { cok_data t; long base, stride; } names[] = {
        {COK_DATA_CLASS_NAMES, 0x0f5a, 27},    {COK_DATA_RACE_NAMES, 0x1140, 16},
        {COK_DATA_ALIGNMENT_NAMES, 0x11c0, 17}, {COK_DATA_DEITY_NAMES, 0x1259, 12},
        {COK_DATA_ORDER_NAMES, 0x12b9, 7},     {COK_DATA_GENDER_NAMES, 0x12d5, 7},
        {COK_DATA_COIN_NAMES, 0x12e3, 11},     {COK_DATA_STATUS_NAMES, 0x1330, 13},
        {COK_DATA_ITEM_WORDS, 0x1390, 21},
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i)
        for (long n = -128; n <= 255; ++n)
            CHECK(cok_data_offset(D, names[i].t, n) == names[i].base + names[i].stride * n);
    /* Combat: the ways of moving, turning, the cone's sides, the steps,
     * the terrain, the footprints, the clouds' cells. */
    for (long way = 0; way <= 255; ++way)
        for (long try_ = 0; try_ <= 255; ++try_) {
            long at = cok_data_offset2(D, COK_DATA_WAYS, way, try_);
            CHECK(at == 0x0396 + 5 * way + try_);
            CHECK(cok_data_holds(D, COK_DATA_WAYS, at) == (5 * way + try_ < 37));
        }
    for (long kind = -128; kind <= 127; ++kind)
        for (long level = -128; level <= 127; ++level) {
            long at = cok_data_offset2(D, COK_DATA_TURNING, kind, level), was = 13 * kind + level;
            CHECK(at == 0x444 + was);
            CHECK(cok_data_holds(D, COK_DATA_TURNING, at) ==
                  !(was < 0x451 - 0x444 || was > 0x50f - 0x444));
        }
    for (long part = 0; part < 4; ++part)
        for (long side = 0; side <= 255; ++side) {
            long at = cok_data_offset2(D, COK_DATA_CONE_SIDES, part, side);
            CHECK(at == 0x0d94 + 8 * part + side && cok_data_holds(D, COK_DATA_CONE_SIDES, at));
        }
    for (long d = 0; d <= 255; ++d) {
        CHECK(cok_data_offset(D, COK_DATA_STEP_X, d) == 0x1ed6 + d);
        CHECK(cok_data_offset(D, COK_DATA_STEP_Y, d) == 0x1edf + d);
        CHECK(cok_data_offset(D, COK_DATA_STEP_X, 9 + d) == cok_data_offset(D, COK_DATA_STEP_Y, d));
    }
    for (long v = 0; v < 0x42; ++v)
        for (long k = 0; k < 4; ++k)
            CHECK(cok_data_offset2(D, COK_DATA_TERRAIN, v, k) == 0x1ee4 + 4 * v + k);
    for (long size = 0; size < 8; ++size)
        for (long k = 0; k < 4; ++k)
            for (long axis = 0; axis < 2; ++axis)
                CHECK(cok_data_offset3(D, COK_DATA_FOOTPRINTS, size, k, axis) ==
                      0x1fe4 + 8 * size + 2 * k + axis);
    for (long k = 1; k <= 5; ++k) CHECK(cok_data_offset(D, COK_DATA_CLOUD_CELLS, k) == 0x2091 + k);
    /* The tables the code read as its own arrays, from their first
     * element: placement's, the furniture's, open ground's, the line's
     * directions, the colours, training's and creating's, the templates. */
    for (long q = 0; q < 4; ++q)
        for (long k = 0; k < 4; ++k) {
            CHECK(cok_data_offset2(D, COK_DATA_MOVES, q, k) == 0x03ba + 4 * q + k);
            CHECK(cok_data_offset2(D, COK_DATA_QUADRANTS, q, k) == 0x03ca + 4 * q + k);
        }
    for (long k = 0; k < 4; ++k) {
        CHECK(cok_data_offset(D, COK_DATA_QUADRANT_DIRECTIONS, k) == 0x03da + k);
        CHECK(cok_data_offset(D, COK_DATA_BESIDE_X, k) == 0x042a + k);
        CHECK(cok_data_offset(D, COK_DATA_BESIDE_Y, k) == 0x042e + k);
        CHECK(cok_data_offset(D, COK_DATA_HELPLESS, k) == 0x200c + k);
        for (long other = 0; other < 2; ++other) {
            CHECK(cok_data_offset2(D, COK_DATA_START_COLUMNS, other, k) ==
                  0x03de + 4 * other + k);
            CHECK(cok_data_offset2(D, COK_DATA_START_ROWS, other, k) == 0x03e6 + 4 * other + k);
        }
    }
    for (long f = 0; f < 5; ++f)
        for (long row = 0; row < 6; ++row)
            for (long end = 0; end < 2; ++end)
                CHECK(cok_data_offset3(D, COK_DATA_FORMATIONS, f, row, end) ==
                      0x03ee + 12 * f + 2 * row + end);
    for (long k = 0; k < 16; ++k) {
        CHECK(cok_data_offset(D, COK_DATA_TERRAIN_FLAGS, k) == 0x0432 + k);
        CHECK(cok_data_offset(D, COK_DATA_BATTLE_ONLY, k) == 0x0db4 + k);
        CHECK(cok_data_offset(D, COK_DATA_SAME_COLOURS, k) == 0x1e26 + k);
        CHECK(cok_data_offset(D, COK_DATA_BLACK_COLOURS, k) == 0x1e36 + k);
    }
    for (long y = 0; y < 3; ++y)
        for (long x = 0; x < 3; ++x)
            CHECK(cok_data_offset2(D, COK_DATA_LINE_DIRECTIONS, y, x) == 0x1dd4 + 3 * y + x);
    for (long k = 0; k < 8; ++k) {
        CHECK(cok_data_offset(D, COK_DATA_LEVEL_DICE, k) == 0x0b6e + k);
        CHECK(cok_data_offset(D, COK_DATA_DIE_SIDES, k) == 0x0b76 + k);
    }
    for (long k = 0; k < 6; ++k) {
        CHECK(cok_data_offset(D, COK_DATA_ALIGNMENT_CODES, k) == 0x0a9a + k);
        for (long order = 0; order < 2; ++order)
            CHECK(cok_data_offset2(D, COK_DATA_KNIGHT_SCORES, order, k) == 0x0b7e + 6 * order + k);
    }
    for (long t = 0; t < 57; ++t)
        CHECK(cok_data_offset(D, COK_DATA_TREASURE_TEMPLATES, t) == 0x0b88 + 2 * t);
}

/* The counts the code bounds its reads by: the footprints' sizes and
 * cells, the terrain values, the lists of effects. */
static void test_counts(void)
{
    CHECK(cok_data_count(D, COK_DATA_TERRAIN, 0) == 0x42);
    CHECK(cok_data_count(D, COK_DATA_FOOTPRINTS, 0) == 8);
    CHECK(cok_data_count(D, COK_DATA_FOOTPRINTS, 1) == 4);
    CHECK(cok_data_count(D, COK_DATA_BATTLE_ONLY, 0) == 16);
    CHECK(cok_data_count(D, COK_DATA_HELPLESS, 0) == 4);
    CHECK(cok_data_count(D, COK_DATA_BESIDE_X, 0) == 4);
    CHECK(cok_data_count(D, COK_DATA_SPELLS, 0) == 0x6c);
    CHECK(cok_data_count(D, COK_DATA_SPELLS, 1) == 16);
    CHECK(cok_data_first(D, COK_DATA_THIEF_LEVEL, 0) == 1);
    CHECK(cok_data_first(D, COK_DATA_THIEF_LEVEL, 1) == 1);
    /* The arrays that hold a footprint's cells hold the game's. */
    CHECK(cok_data_count(D, COK_DATA_FOOTPRINTS, 1) <= COK_COMBAT_FOOTPRINT_CELLS);
}

/* The code's steps by direction (step.h) are the data segment's
 * (DS:1ed6, 1edf), which the executable check covers. */
static void test_steps(void)
{
    CHECK(cok_data_count(D, COK_DATA_STEP_X, 0) == 9 && cok_data_count(D, COK_DATA_STEP_Y, 0) == 9);
    for (unsigned d = 0; d < 9; ++d) {
        CHECK(cok_step_x[d] == cok_data_s8_at(D, COK_DATA_STEP_X, d));
        CHECK(cok_step_y[d] == cok_data_s8_at(D, COK_DATA_STEP_Y, d));
    }
}

/* Reads that run on: a step past 8 into the terrain, the terrain into the
 * footprints, the cone's sides into the battle's effects, the spell table
 * past spell 0x6b into THAC0, and the elements as the code read them. */
static void test_reads(void)
{
    uint8_t byte;
    /* The steps of directions 0-8, then the terrain table's bytes. */
    static const int8_t steps[14] = {0, 1, 1, 1, 0, -1, -1, -1, 0, -1, -1, 0, 1, 1};
    for (unsigned d = 0; d < 14; ++d)
        CHECK((int8_t)cok_data_held(D, cok_data_offset(D, COK_DATA_STEP_X, d)) == steps[d]);
    for (unsigned d = 14; d <= 9 + 255; ++d)
        CHECK(cok_data_held(D, cok_data_offset(D, COK_DATA_STEP_X, d)) ==
              cok_data_u8_at2(D, COK_DATA_TERRAIN, (d - 14) / 4, (d - 14) % 4));
    /* Terrain 0: no cost, eye 0, blocks 0xff; the floor's tile 0x16. */
    CHECK(cok_data_u8_at2(D, COK_DATA_TERRAIN, 0, 0) == 1);
    CHECK(cok_data_u8_at2(D, COK_DATA_TERRAIN, 0, 2) == 0xff);
    CHECK(cok_data_u8_at2(D, COK_DATA_TERRAIN, 0x17, 3) == 0x16);
    /* Footprint 0 is the terrain table's end, 5 the effects that leave one
     * helpless. */
    CHECK(cok_data_s8_at3(D, COK_DATA_FOOTPRINTS, 0, 3, 1) == 0x21);
    CHECK(cok_data_s8_at3(D, COK_DATA_FOOTPRINTS, 5, 0, 0) == 0x33);
    CHECK(cok_data_s8_at3(D, COK_DATA_FOOTPRINTS, 4, 3, 0) == 1);
    CHECK(cok_data_s8_at3(D, COK_DATA_FOOTPRINTS, 1, 1, 0) == -1);
    for (unsigned k = 0; k < 4; ++k)
        CHECK(cok_data_u8_at(D, COK_DATA_HELPLESS, k) ==
              (uint8_t)cok_data_s8_at3(D, COK_DATA_FOOTPRINTS, 5, k / 2, k % 2));
    /* The cone's sides from part 4 on are the battle's effects. */
    for (unsigned k = 0; k < 16; ++k)
        CHECK(cok_data_held(D, cok_data_offset2(D, COK_DATA_CONE_SIDES, 4, k)) ==
              cok_data_u8_at(D, COK_DATA_BATTLE_ONLY, k));
    /* Spell 0x6c's column 0 is a THAC0 byte's neighbour: the original reads
     * on; the strided columns agree with the table. */
    CHECK(cok_data_byte(D, (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, 0xff, 15), &byte));
    for (unsigned spell = 1; spell <= 100; ++spell) {
        uint8_t column;
        CHECK(cok_data_byte(D, (uint16_t)(0x31c3 + 16 * (spell - 1)), &column));
        CHECK(column == cok_data_u8_at2(D, COK_DATA_SPELLS, spell, 0));
    }
    /* Known values: Cleric's THAC0 at level 0, the thief's first skill at
     * level 1, a mage's top level, the cleric's first spell, an elf's
     * minimum strength, the template words of the potion of healing. */
    CHECK(cok_data_held(D, cok_data_offset2(D, COK_DATA_THAC0, 0, 0)) == 40);
    CHECK(cok_data_u8_at2(D, COK_DATA_THIEF_LEVEL, 1, 1) == 30);
    CHECK((int8_t)cok_data_u8_at2(D, COK_DATA_THIEF_RACE, 0, 7) == -10);
    CHECK(cok_data_s8_at2(D, COK_DATA_THIEF_DEXTERITY, 9, 1) == -15);
    CHECK(cok_data_u8_at(D, COK_DATA_TOP_LEVELS, 5) == 12);
    CHECK(cok_data_u8_at3(D, COK_DATA_SPELL_LISTS, 1, 1, 1) == 1);
    CHECK(cok_data_u8_at(D, COK_DATA_CLASS_BITS, 1) == 32);
    CHECK(cok_data_u8_at(D, COK_DATA_ICON_COLOURS, 5) == 6);
    CHECK(cok_data_u16_at(D, COK_DATA_TREASURE_TEMPLATES, 1) == 0x5f);
    CHECK(cok_data_s8_at3(D, COK_DATA_FORMATIONS, 0, 3, 1) == 9);
    CHECK(cok_data_u8_at2(D, COK_DATA_LINE_DIRECTIONS, 1, 1) == 8);
}

/* The names: a Pascal string within the name strings, with no NUL. */
static void test_names(void)
{
    char name[256];
    CHECK(cok_data_string(D, 0x0f5a, name) && strcmp(name, "Cleric") == 0);
    CHECK(cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_ITEM_WORDS, 123), name) &&
          strcmp(name, "Jewelry") == 0);
    /* Status 9 reads the first item name part, as the original does. */
    CHECK(cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_STATUS_NAMES, 9), name) &&
          strcmp(name, "Battle Axe") == 0);
    CHECK(!cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_ITEM_WORDS, 126), name));
    CHECK(!cok_data_string(D, 0x0f59, name));
    /* In the middle of the table: a NUL between names is an empty string,
     * but "Cleric"'s C (67) as a length takes in the NULs after it, which
     * the original would draw; out is left as it was. */
    CHECK(cok_data_string(D, 0x0f5a + 20, name) && name[0] == '\0');
    strcpy(name, "kept");
    CHECK(!cok_data_string(D, 0x0f5b, name) && strcmp(name, "kept") == 0);
    /* The last byte of the strings starts no name they hold. */
    CHECK(!cok_data_string(D, 0x1dd3, name));
    static const char *const coins[7] = {"Silver", "Copper", "Bronze", "Platinum",
                                         "Steel",  "Gems",   "Jewelry"};
    for (int k = 0; k < 7; ++k)
        CHECK(cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_COIN_NAMES, k), name) &&
              strcmp(name, coins[k]) == 0);
}

/* Run access in a child; it must stop on an assertion. */
static void check_aborts(void (*access)(void), const char *what)
{
    fflush(stderr);
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        freopen("/dev/null", "w", stderr);
        access();
        _exit(0);
    }
    int status;
    CHECK(waitpid(pid, &status, 0) == pid);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
        fprintf(stderr, "%s did not stop on an assertion\n", what);
        exit(1);
    }
}

static volatile long sink;
static cok_data_segment without;

static void past(void) { sink = cok_data_u8_at(D, COK_DATA_HELPLESS, 4); }
static void before_first(void) { sink = cok_data_u8_at2(D, COK_DATA_THIEF_LEVEL, 0, 1); }
static void too_few(void) { sink = cok_data_offset(D, COK_DATA_THAC0, 1); }
static void too_many(void) { sink = cok_data_offset2(D, COK_DATA_HELPLESS, 0, 1); }
static void wide(void) { sink = cok_data_u8_at(D, COK_DATA_TREASURE_TEMPLATES, 0); }
static void narrow(void) { sink = cok_data_u16_at(D, COK_DATA_HELPLESS, 0); }
static void absent(void) { sink = cok_data_offset(&without, COK_DATA_HELPLESS, 0); }
static void unheld(void) { sink = cok_data_held(D, 0x43c0); }

static void test_assertions(void)
{
    check_aborts(past, "an index past the elements");
    check_aborts(before_first, "an index before the first");
    check_aborts(too_few, "a table read with too few indices");
    check_aborts(too_many, "a list read with two");
    check_aborts(wide, "a byte read of a word");
    check_aborts(narrow, "a word read of a byte");
    without = *D;
    without.table[COK_DATA_HELPLESS].width = 0;
    check_aborts(absent, "a table the game lacks");
    check_aborts(unheld, "a byte the game does not hold");
}

int main(void)
{
    D = cok_rules_champions.data;
    test_executable();
    test_order();
    test_coverage();
    test_shapes();
    test_arithmetic();
    test_counts();
    test_steps();
    test_reads();
    test_names();
    test_assertions();
    /* Death Knights' tables: in order, agreeing where they overlap, each
     * named one's elements within its own bytes. */
    D = cok_rules_death_knights.data;
    test_order();
    test_shapes();
    D = cok_rules_champions.data;
    puts("test_data: ok");
    return 0;
}
