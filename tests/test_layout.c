/* The character record's layout (src/field.h, Champions' in src/rules.c):
 * every field where party.h's description and the code before the layout
 * put it, with its width, count, first index and sign; no two fields
 * sharing a byte but the one the table documents; the record within its
 * buffer; and the accessors: each width and sign, arrays and the table of
 * spells a day, the original's unchecked arithmetic, the far pointers, and
 * the assertions that stop a wrong access. */
#include "rules.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_character_layout *L;

/* What each field is in Champions, written out again here (not copied
 * from rules.c): offset, width, count, first index, signed, a far
 * pointer; stride and rows where they are not the plain ones. */
static const struct {
    cok_field field;
    const char *name;
    unsigned offset, width, count, first;
    bool is_signed, pointer;
    unsigned stride, rows, row_stride;
} expected[] = {
    {COK_FIELD_NAME, "NAME", 0x000, 1, 16, 0, false, false, 0, 0, 0},
    {COK_FIELD_SCORE_BASE, "SCORE_BASE", 0x010, 1, 6, 0, false, false, 2, 0, 0},
    {COK_FIELD_SCORE, "SCORE", 0x011, 1, 6, 0, false, false, 2, 0, 0},
    {COK_FIELD_STRENGTH_EXCEPTIONAL, "STRENGTH_EXCEPTIONAL", 0x01c, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_STRENGTH_EXCEPTIONAL_BASE, "STRENGTH_EXCEPTIONAL_BASE", 0x01d, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_MEMORIZED, "MEMORIZED", 0x01e, 1, 0x3a, 0, false, false, 0, 0, 0},
    {COK_FIELD_STUDY_HOURS, "STUDY_HOURS", 0x058, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_BASE_THAC0, "BASE_THAC0", 0x059, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_RACE, "RACE", 0x05a, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_CLASS, "CLASS", 0x05b, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_KNIGHT_ORDER, "KNIGHT_ORDER", 0x05c, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_DEITY, "DEITY", 0x05d, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_MAGIC_ORDER, "MAGIC_ORDER", 0x05e, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_KNIGHT_TIER, "KNIGHT_TIER", 0x05f, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_AGE, "AGE", 0x060, 2, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_MAX_HIT_POINTS, "MAX_HIT_POINTS", 0x062, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_KNOWN_SPELL, "KNOWN_SPELL", 0x063, 1, 108, 1, false, false, 0, 0, 0},
    {COK_FIELD_FIGHTER_LEVEL, "FIGHTER_LEVEL", 0x0ce, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_SIZE, "SIZE", 0x0cf, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_SAVING_THROW, "SAVING_THROW", 0x0d0, 1, 5, 0, false, false, 0, 0, 0},
    {COK_FIELD_BASE_MOVEMENT, "BASE_MOVEMENT", 0x0d5, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_HIGHEST_LEVEL, "HIGHEST_LEVEL", 0x0d6, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_CHANGE_LEVEL, "CHANGE_LEVEL", 0x0d7, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_UNDEAD, "UNDEAD", 0x0da, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_THIEF_SKILL, "THIEF_SKILL", 0x0db, 1, 8, 1, false, false, 0, 0, 0},
    {COK_FIELD_EFFECTS, "EFFECTS", 0x0e3, 4, 1, 0, false, true, 0, 0, 0},
    {COK_FIELD_NPC, "NPC", 0x0e7, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_COINS, "COINS", 0x0eb, 2, 7, 0, false, false, 0, 0, 0},
    {COK_FIELD_CLASS_LEVEL, "CLASS_LEVEL", 0x0f9, 1, 8, 0, true, false, 0, 0, 0},
    {COK_FIELD_FORMER_LEVEL, "FORMER_LEVEL", 0x101, 1, 8, 0, true, false, 0, 0, 0},
    {COK_FIELD_GENDER, "GENDER", 0x109, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_ALIGNMENT, "ALIGNMENT", 0x10a, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_BASE_ATTACKS, "BASE_ATTACKS", 0x10b, 1, 2, 1, false, false, 0, 0, 0},
    {COK_FIELD_BASE_DICE, "BASE_DICE", 0x10d, 1, 2, 1, false, false, 0, 0, 0},
    {COK_FIELD_BASE_DICE_SIDES, "BASE_DICE_SIDES", 0x10f, 1, 2, 1, false, false, 0, 0, 0},
    {COK_FIELD_BASE_DAMAGE_BONUS, "BASE_DAMAGE_BONUS", 0x111, 1, 2, 1, true, false, 0, 0, 0},
    {COK_FIELD_BASE_AC, "BASE_AC", 0x113, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_STRENGTH_BONUSES, "STRENGTH_BONUSES", 0x114, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_MONSTER_ID, "MONSTER_ID", 0x115, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_EXPERIENCE, "EXPERIENCE", 0x116, 4, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_USABLE_CLASSES, "USABLE_CLASSES", 0x11a, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_BASE_HIT_POINTS, "BASE_HIT_POINTS", 0x11b, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_SPELLS_A_DAY, "SPELLS_A_DAY", 0x11c, 1, 5, 1, false, false, 0, 4, 5},
    {COK_FIELD_ICON_HEAD, "ICON_HEAD", 0x135, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_ICON_BODY, "ICON_BODY", 0x136, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_ICON, "ICON", 0x137, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_ICON_SIZE, "ICON_SIZE", 0x138, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_ICON_COLOURS, "ICON_COLOURS", 0x139, 1, 6, 1, false, false, 0, 0, 0},
    {COK_FIELD_CREATURE_FLAGS, "CREATURE_FLAGS", 0x13f, 1, 2, 0, false, false, 0, 0, 0},
    {COK_FIELD_ITEM_COUNT, "ITEM_COUNT", 0x142, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_ITEMS, "ITEMS", 0x143, 4, 1, 0, false, true, 0, 0, 0},
    {COK_FIELD_ITEM_SLOTS, "ITEM_SLOTS", 0x147, 4, 13, 0, false, true, 0, 0, 0},
    {COK_FIELD_HANDS, "HANDS", 0x17b, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_SAVE_BONUS, "SAVE_BONUS", 0x17c, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_WEIGHT, "WEIGHT", 0x17d, 2, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_NEXT, "NEXT", 0x17f, 4, 1, 0, false, true, 0, 0, 0},
    {COK_FIELD_COMBAT, "COMBAT", 0x183, 4, 1, 0, false, true, 0, 0, 0},
    {COK_FIELD_MAGIC_RESISTANCE, "MAGIC_RESISTANCE", 0x187, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_STATUS, "STATUS", 0x188, 1, 1, 0, true, false, 0, 0, 0},
    {COK_FIELD_ACTIVE, "ACTIVE", 0x189, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_HOSTILE, "HOSTILE", 0x18a, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_COMPUTER, "COMPUTER", 0x18b, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_THAC0, "THAC0", 0x18c, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_AC, "AC", 0x18d, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_AC_BEHIND, "AC_BEHIND", 0x18e, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_ATTACKS_LEFT, "ATTACKS_LEFT", 0x18f, 1, 2, 1, false, false, 0, 0, 0},
    {COK_FIELD_DICE, "DICE", 0x191, 1, 2, 1, false, false, 0, 0, 0},
    {COK_FIELD_DICE_SIDES, "DICE_SIDES", 0x193, 1, 2, 1, false, false, 0, 0, 0},
    {COK_FIELD_DAMAGE_BONUS, "DAMAGE_BONUS", 0x195, 1, 2, 1, true, false, 0, 0, 0},
    {COK_FIELD_HIT_POINTS, "HIT_POINTS", 0x197, 1, 1, 0, false, false, 0, 0, 0},
    {COK_FIELD_MOVEMENT, "MOVEMENT", 0x198, 1, 1, 0, false, false, 0, 0, 0},
};
#define EXPECTED (sizeof expected / sizeof *expected)

static void test_table(void)
{
    CHECK(L == cok_rules_champions.character);
    CHECK(L->size == 0x199 && L->size == COK_CHARACTER_SIZE);
    CHECK(L->file_size == 0x199);
    CHECK(L->size <= COK_CHARACTER_MAX);
    /* Every field listed once, in the enum's order. */
    CHECK(EXPECTED == (size_t)COK_FIELD_COUNT);
    for (size_t k = 0; k < EXPECTED; ++k) {
        const cok_field_place *d = &L->field[expected[k].field];
        unsigned stride = expected[k].stride != 0 ? expected[k].stride : expected[k].width;
        unsigned rows = expected[k].rows != 0 ? expected[k].rows : 1;
        bool ok = expected[k].field == (cok_field)k && d->offset == expected[k].offset &&
                  d->width == expected[k].width && d->count == expected[k].count &&
                  d->first == expected[k].first && d->is_signed == expected[k].is_signed &&
                  d->pointer == expected[k].pointer && d->stride == stride && d->rows == rows &&
                  d->row_stride == expected[k].row_stride;
        if (!ok) {
            fprintf(stderr, "field %zu (COK_FIELD_%s): +0x%03x width %u count %u first %u "
                            "stride %u rows %u/%u%s%s, expected +0x%03x width %u count %u "
                            "first %u\n",
                    k, expected[k].name, d->offset, d->width, d->count, d->first, d->stride,
                    d->rows, d->row_stride, d->is_signed ? " signed" : "",
                    d->pointer ? " pointer" : "", expected[k].offset, expected[k].width,
                    expected[k].count, expected[k].first);
            exit(1);
        }
        /* Champions has every field, and each lies within the record. */
        CHECK(cok_field_present(L, expected[k].field));
        CHECK(d->offset + cok_field_size(L, expected[k].field) <= L->size);
        CHECK(d->width == 1 || d->width == 2 || d->width == 4);
        CHECK(d->stride >= d->width);
    }
}

/* No byte belongs to two fields, but the one the table documents: known
 * spell 108's byte is the highest fighter level's (+0xce). */
static void test_overlaps(void)
{
    int owner[COK_CHARACTER_MAX];
    for (size_t i = 0; i < COK_CHARACTER_MAX; ++i) owner[i] = -1;
    size_t shared = 0;
    for (int f = 0; f < COK_FIELD_COUNT; ++f) {
        const cok_field_place *d = &L->field[f];
        for (unsigned row = 0; row < d->rows; ++row)
            for (unsigned i = 0; i < d->count; ++i)
                for (unsigned b = 0; b < d->width; ++b) {
                    size_t at = d->offset + row * d->row_stride + i * d->stride + b;
                    CHECK(at < L->size);
                    if (owner[at] < 0) {
                        owner[at] = f;
                        continue;
                    }
                    bool documented = at == 0xce && owner[at] == COK_FIELD_KNOWN_SPELL &&
                                      f == COK_FIELD_FIGHTER_LEVEL;
                    if (!documented) {
                        fprintf(stderr, "+0x%03zx is both COK_FIELD_%s's and COK_FIELD_%s's\n", at,
                                expected[owner[at]].name, expected[f].name);
                        exit(1);
                    }
                    ++shared;
                }
    }
    CHECK(shared == 1);
    CHECK(cok_field_offset_at(L, COK_FIELD_KNOWN_SPELL, 108) == cok_field_offset(L, COK_FIELD_FIGHTER_LEVEL));
}

/* The fields where the code before the layout read them: each a byte
 * offset as the literal was. */
static void test_offsets(void)
{
    CHECK(cok_field_offset_at(L, COK_FIELD_NAME, 1) == 1);
    CHECK(cok_field_offset_at(L, COK_FIELD_SCORE, COK_SCORE_STRENGTH) == 0x11);
    CHECK(cok_field_offset_at(L, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE) == 0x13);
    CHECK(cok_field_offset_at(L, COK_FIELD_SCORE, COK_SCORE_WISDOM) == 0x15);
    CHECK(cok_field_offset_at(L, COK_FIELD_SCORE, COK_SCORE_DEXTERITY) == 0x17);
    CHECK(cok_field_offset_at(L, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION) == 0x19);
    CHECK(cok_field_offset_at(L, COK_FIELD_SCORE, COK_SCORE_CHARISMA) == 0x1b);
    CHECK(cok_field_offset_at(L, COK_FIELD_SCORE_BASE, COK_SCORE_CHARISMA) == 0x1a);
    CHECK(cok_field_offset_at(L, COK_FIELD_MEMORIZED, 0x39) == 0x57);
    CHECK(cok_field_offset_at(L, COK_FIELD_KNOWN_SPELL, 1) == 0x63);
    CHECK(cok_field_offset_at(L, COK_FIELD_KNOWN_SPELL, 0x6b) == 0xcd);
    CHECK(cok_field_offset_at(L, COK_FIELD_THIEF_SKILL, 1) == 0xdb);
    CHECK(cok_field_offset_at(L, COK_FIELD_THIEF_SKILL, 8) == 0xe2);
    CHECK(cok_field_offset_at(L, COK_FIELD_COINS, 1) == 0xed); /* c + 0xeb + 2 * i */
    CHECK(cok_field_offset_at(L, COK_FIELD_COINS, 4) == 0xf3); /* steel */
    CHECK(cok_field_offset_at(L, COK_FIELD_COINS, 6) == 0xf7);
    CHECK(cok_field_offset_at(L, COK_FIELD_CLASS_LEVEL, 7) == 0x100);
    CHECK(cok_field_offset_at(L, COK_FIELD_FORMER_LEVEL, 7) == 0x108);
    CHECK(cok_field_offset_at(L, COK_FIELD_BASE_DICE, 1) == 0x10d);
    CHECK(cok_field_offset_at(L, COK_FIELD_BASE_DAMAGE_BONUS, 2) == 0x112);
    CHECK(cok_field_offset_at(L, COK_FIELD_DICE, 2) == 0x192);
    CHECK(cok_field_offset_at(L, COK_FIELD_DAMAGE_BONUS, 1) == 0x195);
    CHECK(cok_field_offset_at(L, COK_FIELD_ITEM_SLOTS, 12) == 0x177);
    /* Spells a day, +0x11b + class * 5 + level. */
    for (int row = 0; row < 4; ++row)
        for (int level = 1; level <= 5; ++level)
            CHECK(cok_field_offset_at2(L, COK_FIELD_SPELLS_A_DAY, row, level) ==
                  (size_t)(0x11b + row * 5 + level));
    CHECK(cok_field_size(L, COK_FIELD_SPELLS_A_DAY) == 0x14);
    CHECK(cok_field_size(L, COK_FIELD_ITEM_SLOTS) == 4 * 13);
    CHECK(cok_field_size(L, COK_FIELD_SCORE) == 11);
    CHECK(cok_field_count(L, COK_FIELD_MEMORIZED) == 0x3a);
    CHECK(cok_field_first(L, COK_FIELD_MEMORIZED) == 0);
    CHECK(cok_field_first(L, COK_FIELD_THIEF_SKILL) == 1 &&
          cok_field_count(L, COK_FIELD_THIEF_SKILL) == 8);
    CHECK(cok_field_first(L, COK_FIELD_COINS) == 0 && cok_field_count(L, COK_FIELD_COINS) == 7);
    CHECK(cok_field_offset(L, COK_FIELD_BASE_HIT_POINTS) == 0x11b);
    CHECK(cok_field_offset_at(L, COK_FIELD_CREATURE_FLAGS, 1) == 0x140);
    /* The combat files' (attack.c ... cloud.c): attacks left by slot,
     * +0x18e + slot; the icon's colours, +0x138 + k (4b6d:0817). */
    CHECK(cok_field_offset_at(L, COK_FIELD_ATTACKS_LEFT, 1) == 0x18f);
    CHECK(cok_field_offset_at(L, COK_FIELD_ATTACKS_LEFT, 2) == 0x190);
    CHECK(cok_field_offset_unchecked(L, COK_FIELD_ATTACKS_LEFT, 0, 0) ==
          (long)cok_field_offset(L, COK_FIELD_AC_BEHIND));
    CHECK(cok_field_offset_at(L, COK_FIELD_ICON_COLOURS, 1) == 0x139);
    CHECK(cok_field_offset_at(L, COK_FIELD_ICON_COLOURS, 6) == 0x13e);
    CHECK(cok_field_offset(L, COK_FIELD_SIZE) == 0xcf);
    CHECK(cok_field_offset(L, COK_FIELD_UNDEAD) == 0xda);
    CHECK(cok_field_offset(L, COK_FIELD_MONSTER_ID) == 0x115);
}

static void test_accessors(void)
{
    uint8_t c[COK_CHARACTER_MAX];
    for (size_t i = 0; i < sizeof c; ++i) c[i] = (uint8_t)(i * 7 + 3);
    CHECK(cok_field_u8(L, c, COK_FIELD_HIT_POINTS) == c[0x197]);
    CHECK(cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 3) == c[0xfc]);
    CHECK(cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 3) == (int8_t)c[0xfc]);
    CHECK(cok_field_s8(L, c, COK_FIELD_SAVE_BONUS) == (int8_t)c[0x17c]);
    /* s8 reads any byte field as (int8_t)c[x] did, whatever the table's
     * is_signed (which records how the original reads it). */
    CHECK(cok_field_s8(L, c, COK_FIELD_AC) == (int8_t)c[0x18d]);
    CHECK(cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) == c[0x100]);
    CHECK(cok_field_u16(L, c, COK_FIELD_WEIGHT) == (uint16_t)(c[0x17d] | c[0x17e] << 8));
    CHECK(cok_field_u16_at(L, c, COK_FIELD_COINS, 4) == (uint16_t)(c[0xf3] | c[0xf4] << 8));
    CHECK(cok_field_u32(L, c, COK_FIELD_EXPERIENCE) ==
          ((uint32_t)c[0x116] | (uint32_t)c[0x117] << 8 | (uint32_t)c[0x118] << 16 |
           (uint32_t)c[0x119] << 24));
    CHECK(cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 3, 2) == c[0x12c]);

    uint8_t before[COK_CHARACTER_MAX];
    memcpy(before, c, sizeof c);
    cok_field_set8(L, c, COK_FIELD_STATUS, 6);
    cok_field_set8_at(L, c, COK_FIELD_THIEF_SKILL, 8, 0x55);
    cok_field_set16(L, c, COK_FIELD_AGE, 0x1234);
    cok_field_set16_at(L, c, COK_FIELD_COINS, 6, 0xbeef);
    cok_field_set32(L, c, COK_FIELD_EXPERIENCE, 0x89abcdefu);
    cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 1, 5, 9);
    before[0x188] = 6;
    before[0xe2] = 0x55;
    before[0x60] = 0x34, before[0x61] = 0x12;
    before[0xf7] = 0xef, before[0xf8] = 0xbe;
    before[0x116] = 0xef, before[0x117] = 0xcd, before[0x118] = 0xab, before[0x119] = 0x89;
    before[0x125] = 9;
    CHECK(memcmp(before, c, sizeof c) == 0);
    /* add8 wraps as a byte, as c[x] = (uint8_t)(c[x] + delta). */
    cok_field_set8(L, c, COK_FIELD_THAC0, 0xfe);
    cok_field_add8(L, c, COK_FIELD_THAC0, 3);
    CHECK(c[0x18c] == 1);
    cok_field_add8(L, c, COK_FIELD_THAC0, -2);
    CHECK(c[0x18c] == 0xff);
    cok_field_add8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, (int8_t)-1);
    CHECK(c[0x195] == (uint8_t)(before[0x195] - 1));
    cok_field_add8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
    CHECK(c[0x11c] == (uint8_t)(before[0x11c] + 1));
    /* Indices of any integer type, as subscripts. */
    size_t i = 2;
    int8_t k = 2;
    CHECK(cok_field_u8_at(L, c, COK_FIELD_SAVING_THROW, i) ==
          cok_field_u8_at(L, c, COK_FIELD_SAVING_THROW, k));
}

/* The original's arithmetic past a field, and the far pointers it may
 * reach (cok_magic_slots: +0xe3-+0xe6, +0x143-+0x17a, +0x17f-+0x186). */
static void test_unchecked(void)
{
    CHECK(cok_field_offset_unchecked(L, COK_FIELD_SPELLS_A_DAY, 4, 7) == 0x11b + 4 * 5 + 7);
    CHECK(cok_field_offset_unchecked(L, COK_FIELD_SPELLS_A_DAY, -128, 0) == 0x11b - 128 * 5);
    /* A table whose rows are longer than their elements, as Death Knights'
     * spells a day are (8 bytes a spell class, dkk 46b2:10d7): rows step by
     * the row stride, not the count. */
    cok_character_layout wide_rows = *L;
    wide_rows.field[COK_FIELD_SPELLS_A_DAY].row_stride = 8;
    CHECK(cok_field_offset_at2(&wide_rows, COK_FIELD_SPELLS_A_DAY, 3, 2) == 0x11c + 3 * 8 + 1);
    CHECK(cok_field_offset_unchecked(&wide_rows, COK_FIELD_SPELLS_A_DAY, 2, 1) == 0x11c + 2 * 8);
    CHECK(cok_field_size(&wide_rows, COK_FIELD_SPELLS_A_DAY) == 3 * 8 + 5);
    for (long at = -1; at <= 0x199; ++at) {
        bool pointer = (at >= 0xe3 && at < 0xe7) || (at >= 0x143 && at < 0x17b) ||
                       (at >= 0x17f && at < 0x187);
        CHECK(cok_field_pointer_at(L, at) == pointer);
    }
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

static uint8_t record[COK_CHARACTER_MAX];
static volatile long sink;

static void wide(void) { sink = cok_field_u8(L, record, COK_FIELD_WEIGHT); }
static void narrow(void) { sink = cok_field_u16(L, record, COK_FIELD_STATUS); }
static void wide_s8(void) { sink = cok_field_s8(L, record, COK_FIELD_AGE); }
static void past(void) { sink = cok_field_u8_at(L, record, COK_FIELD_THIEF_SKILL, 9); }
static void before_first(void) { sink = cok_field_u8_at(L, record, COK_FIELD_THIEF_SKILL, 0); }
static void array_single(void) { sink = cok_field_u8(L, record, COK_FIELD_SAVING_THROW); }
static void table_no_row(void) { sink = cok_field_u8_at(L, record, COK_FIELD_SPELLS_A_DAY, 1); }
static void past_row(void) { sink = cok_field_u8_at2(L, record, COK_FIELD_SPELLS_A_DAY, 4, 1); }

/* A game without a field: width 0 says so. */
static cok_character_layout without;
static void absent(void) { sink = cok_field_u8(&without, record, COK_FIELD_FIGHTER_LEVEL); }
static void absent_offset(void) { sink = (long)cok_field_offset(&without, COK_FIELD_FIGHTER_LEVEL); }

static void test_assertions(void)
{
    check_aborts(wide, "a byte read of a word");
    check_aborts(narrow, "a word read of a byte");
    check_aborts(wide_s8, "a signed byte read of a word");
    check_aborts(past, "an index past the elements");
    check_aborts(before_first, "an index before the first");
    check_aborts(array_single, "an array read as one value");
    check_aborts(table_no_row, "a table read without a row");
    check_aborts(past_row, "a row past the table");
    without = *L;
    without.field[COK_FIELD_FIGHTER_LEVEL].width = 0;
    CHECK(!cok_field_present(&without, COK_FIELD_FIGHTER_LEVEL));
    CHECK(cok_field_present(&without, COK_FIELD_HIT_POINTS));
    check_aborts(absent, "a field the game lacks");
    check_aborts(absent_offset, "the offset of a field the game lacks");
}

int main(void)
{
    L = cok_rules_champions.character;
    test_table();
    test_overlaps();
    test_offsets();
    test_accessors();
    test_unchecked();
    test_assertions();
    puts("test_layout: ok");
    return 0;
}
