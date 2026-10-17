#ifndef COK_DATA_H
#define COK_DATA_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The original's data segment: the tables the port copied from it, as a
 * game's descriptor gives them (cok_rules' data; see DEVELOPERS.md's
 * "Games"). Code reads a table by its name through the lookups below,
 * which find it in the game's segment: the original's arithmetic on DS
 * offsets (base + index * stride), then the byte at that offset from
 * whichever of the game's tables holds it, so that an index that runs
 * past one table reads the next as the original does. Champions' bytes
 * are in data_champions.c, its tables and their shapes in rules.c. */

/* A run of the original's initialized data as the port holds it: the DS
 * offset of the first byte, the number of bytes, the stride between them
 * (1, or 16 for a column of the spell table), and the bytes. */
typedef struct {
    uint16_t offset, size, stride;
    const uint8_t *bytes;
} cok_ds_table;

/* The tables the code reads, named for what they are. An index is counted
 * from its first, as the original's code counts it (a Pascal array's
 * lower bound): the thief skills 1-8, the levels of the spells a day 2-12. */
typedef enum {
    /* Names: Pascal strings in the name strings (COK_DATA_NAMES), each
     * index's a string[stride - 1]. */
    COK_DATA_NAMES,           /* All the name strings, which a name is read within. */
    COK_DATA_CLASS_NAMES,     /* By class id, 0-16. */
    COK_DATA_RACE_NAMES,      /* By race, 0-7. */
    COK_DATA_ALIGNMENT_NAMES, /* By alignment, 0-8. */
    COK_DATA_DEITY_NAMES,     /* By deity, 0-7. */
    COK_DATA_ORDER_NAMES,     /* The orders of magic, 0-3: none, White, Red, Black. */
    COK_DATA_GENDER_NAMES,    /* 0-1. */
    COK_DATA_COIN_NAMES,      /* By coin, 0-6. */
    COK_DATA_STATUS_NAMES,    /* By status, 0-8. */
    COK_DATA_ITEM_WORDS,      /* An item's name parts, by word, 1-123. */
    /* The characters' rules. */
    COK_DATA_SPELLS,          /* By spell id and column: 16 bytes a spell. */
    COK_DATA_THAC0,           /* 60 - THAC0, by class and level. */
    COK_DATA_CLASS_BITS,      /* The usable classes' bit of each class. */
    COK_DATA_TRAINING_BITS,   /* The bit of each class a training hall's mask tests. */
    COK_DATA_TOP_LEVELS,      /* By class, the level above which hit dice stop. */
    COK_DATA_ICON_COLOURS,    /* The template's colours the six colour bytes replace, 1-6. */
    COK_DATA_THIEF_LEVEL,     /* Thief skills 1-8 by thief level. */
    COK_DATA_THIEF_RACE,      /* Their adjustments (signed) by race. */
    COK_DATA_THIEF_DEXTERITY, /* Skills 1-5's adjustments (signed) by dexterity. */
    COK_DATA_SCORE_LIMITS,    /* By race, 16 bytes: the scores' least and most. */
    COK_DATA_RACE_CLASSES,    /* By race: the count, then the class ids, 1-14. */
    COK_DATA_AGE_DICE,        /* By race and class, 4 bytes: a word, a count, sides. */
    COK_DATA_AGES,            /* By race, words: the ages a score changes at. */
    COK_DATA_CLASS_MINIMUMS,  /* By class id and score, the least. */
    COK_DATA_CLASS_ALIGNMENTS, /* By class id, the alignments allowed, as bits. */
    COK_DATA_CONSTITUTION,    /* The hit points (signed) a constitution adds. */
    COK_DATA_EXPERIENCE,      /* By row and level, 32-bit: the experience a level needs. */
    COK_DATA_SPELLS_A_DAY,    /* By table, level and spell level. */
    COK_DATA_SAVING_THROWS,   /* By class, level and type. */
    COK_DATA_SPELL_LISTS,     /* By list, spell level and k: spell ids, to the first 0. */
    /* Combat. */
    COK_DATA_WAYS,            /* By way of moving and try, the turn from the direction wanted. */
    COK_DATA_MOVES,           /* By quadrant and square, a 3D direction. */
    COK_DATA_QUADRANTS,       /* By quadrant and square, twice the quadrant laid out for. */
    COK_DATA_QUADRANT_DIRECTIONS, /* By quadrant, the combat direction. */
    COK_DATA_START_COLUMNS,   /* By other square and quadrant, the first rank's centre (signed). */
    COK_DATA_START_ROWS,
    COK_DATA_FORMATIONS,      /* By formation, row and end, the free columns (signed). */
    COK_DATA_BESIDE_X,        /* North, east, south, west (signed). */
    COK_DATA_BESIDE_Y,
    COK_DATA_TERRAIN_FLAGS,   /* By type of open ground. */
    COK_DATA_TURNING,         /* By kind of undead and level, the d20 to turn it (signed). */
    COK_DATA_CONE_SIDES,      /* By part (left x, y, right x, y) and side, a step (signed). */
    COK_DATA_BATTLE_ONLY,     /* The effects that last only through the battle. */
    COK_DATA_LINE_DIRECTIONS, /* By a line step's y and x + 1, its direction. */
    COK_DATA_SAME_COLOURS,    /* Every colour as itself. */
    COK_DATA_BLACK_COLOURS,   /* Colour 8 as black. */
    COK_DATA_STEP_X,          /* By direction 0-8, a step (signed). */
    COK_DATA_STEP_Y,
    COK_DATA_TERRAIN,         /* By terrain value, 4 bytes: cost, eye, block, tile. */
    COK_DATA_FOOTPRINTS,      /* By size, cell and axis, a cell (signed). */
    COK_DATA_HELPLESS,        /* The effects that leave a combatant helpless. */
    COK_DATA_CLOUD_CELLS,     /* A cloud's cells' directions, 1-4. */
    /* Training and creating. */
    COK_DATA_ALIGNMENT_CODES, /* The alignments the alignment list offers, by bit. */
    COK_DATA_LEVEL_DICE,      /* By class, the hit dice at level 1. */
    COK_DATA_DIE_SIDES,       /* By class, their sides. */
    COK_DATA_KNIGHT_SCORES,   /* By order and score, the least for the next order. */
    /* Treasure. */
    COK_DATA_TREASURE_TEMPLATES, /* Words: the generator's templates. */
    COK_DATA_TABLES
} cok_data;

/* One index of a table: its first, how many there are, and the bytes from
 * one to the next. */
typedef struct {
    int16_t first;
    uint16_t count, stride;
} cok_data_index;

/* Where a table lies and its shape. base is the DS offset the original's
 * arithmetic adds each index times its stride to (for an index counted
 * from 1, the element before the first: the thief skills' DS:3908 for
 * DS:3911), as its code has it; from and size the bytes that belong to the
 * table (by default the elements', from the first at every index's first):
 * the port's copy, which a read the original bounds by the table stays
 * within; width the bytes an element (a name's, its string[]'s; 0: the
 * game has no such table); indices how many index it, outermost first. */
typedef struct {
    uint16_t base, from, size;
    uint8_t width, indices;
    cok_data_index index[3];
} cok_data_table;

/* A game's data segment: the tables it holds (those with a stride of 1
 * first, in order of their DS offsets, then the columns), and the place
 * and shape of each the code reads. */
typedef struct {
    const cok_ds_table *tables;
    size_t table_count;
    cok_data_table table[COK_DATA_TABLES];
} cok_data_segment;

/* The lookups. Each takes the segment (named D by convention: const
 * cok_data_segment *D = cok_game_rules(game)->data; once at a routine's
 * top) and the table's name, and asserts that the game has the table and
 * that it is given as many indices as the table has.
 *
 * cok_data_offset (and 2, 3) is the original's arithmetic, unbounded, as
 * a long: a site converts and bounds it as the code before it did (a
 * uint16_t as the original's 16-bit arithmetic, or a check that it lies in
 * 0..0xffff). cok_data_byte reads the byte the game's tables hold at a DS
 * offset, whichever holds it, as the original reads on from one table into
 * the next; false for a byte none holds. cok_data_holds says whether an
 * offset is one of a table's own bytes, for a read the port bounds by it
 * as it bounded its copy of the table; cok_data_held reads a byte the
 * game's tables must hold (asserted), at an offset so bounded or known to
 * be held. cok_data_string reads the name at an offset.
 *
 * cok_data_u8_at (and s8, u16; 2, 3 for more indices) read an element
 * whose indices the code keeps within the table: each asserted to be one
 * of its elements, as a C array's bound did; cok_data_first and count give
 * an index's range, for a loop over the table or a bound the code checks. */

static inline const cok_data_table *cok_data_shape_(const cok_data_segment *D, cok_data t,
                                                    unsigned indices)
{
    assert(D != NULL && (unsigned)t < COK_DATA_TABLES);
    const cok_data_table *s = &D->table[t];
    assert(s->width != 0 && s->indices == indices);
    return s;
}

static inline long cok_data_offset_(const cok_data_segment *D, cok_data t, unsigned indices,
                                    long i, long j, long k)
{
    const cok_data_table *s = cok_data_shape_(D, t, indices);
    long at[3] = {i, j, k};
    long offset = s->base;
    for (unsigned n = 0; n < indices; ++n) offset += at[n] * (long)s->index[n].stride;
    return offset;
}

static inline bool cok_data_byte(const cok_data_segment *D, uint16_t offset, uint8_t *out)
{
    /* The tables with a stride of 1 come first, in order of DS offset: the
     * last that starts at or before offset, or one before it that runs on
     * past it (one table's copy may hold another's, byte for byte the
     * same). Then the columns. */
    size_t low = 0, high = D->table_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (D->tables[mid].stride == 1 && D->tables[mid].offset <= offset) low = mid + 1;
        else high = mid;
    }
    for (size_t n = low; n-- > 0;) {
        const cok_ds_table *t = &D->tables[n];
        if ((size_t)(offset - t->offset) < t->size) {
            *out = t->bytes[offset - t->offset];
            return true;
        }
    }
    for (size_t n = 0; n < D->table_count; ++n) {
        const cok_ds_table *t = &D->tables[n];
        if (t->stride == 1 || offset < t->offset) continue;
        size_t k = (size_t)(offset - t->offset);
        if (k % t->stride != 0 || k / t->stride >= t->size) continue;
        *out = t->bytes[k / t->stride];
        return true;
    }
    return false;
}

static inline bool cok_data_holds(const cok_data_segment *D, cok_data t, long offset)
{
    assert(D != NULL && (unsigned)t < COK_DATA_TABLES && D->table[t].width != 0);
    const cok_data_table *s = &D->table[t];
    return offset >= s->from && offset < (long)s->from + s->size;
}

static inline size_t cok_data_first(const cok_data_segment *D, cok_data t, unsigned index)
{
    assert(D != NULL && (unsigned)t < COK_DATA_TABLES && index < D->table[t].indices);
    return (size_t)D->table[t].index[index].first;
}

static inline size_t cok_data_count(const cok_data_segment *D, cok_data t, unsigned index)
{
    assert(D != NULL && (unsigned)t < COK_DATA_TABLES && index < D->table[t].indices);
    return D->table[t].index[index].count;
}

/* The offset of an element whose indices must be the table's, as wide as
 * width. */
static inline long cok_data_element_(const cok_data_segment *D, cok_data t, unsigned indices,
                                     long i, long j, long k, unsigned width)
{
    const cok_data_table *s = cok_data_shape_(D, t, indices);
    long at[3] = {i, j, k};
    for (unsigned n = 0; n < indices; ++n)
        assert(at[n] >= s->index[n].first && at[n] < (long)s->index[n].first + s->index[n].count);
    assert(s->width == width);
    long offset = cok_data_offset_(D, t, indices, i, j, k);
    assert(cok_data_holds(D, t, offset) && cok_data_holds(D, t, offset + width - 1));
    return offset;
}

/* The byte the game's tables hold at DS:offset, which they must: for an
 * offset the code keeps within a table's own bytes (cok_data_holds) or
 * within bytes the game is known to hold, as a C array's bound did. */
static inline uint8_t cok_data_held(const cok_data_segment *D, long offset)
{
    uint8_t byte = 0;
    bool held = cok_data_byte(D, (uint16_t)offset, &byte);
    assert(held);
    (void)held;
    return byte;
}

static inline uint16_t cok_data_get16_(const cok_data_segment *D, long offset)
{
    return (uint16_t)(cok_data_held(D, offset) | cok_data_held(D, offset + 1) << 8);
}

/* The name at DS:offset (a Pascal string) into out, NUL-ended: false
 * where the name would start or end outside the name strings
 * (COK_DATA_NAMES), or hold a NUL, which a string read from the middle of
 * the table may (the NULs between names, which the original draws as
 * characters). */
static inline bool cok_data_string(const cok_data_segment *D, uint16_t offset, char out[256])
{
    if (!cok_data_holds(D, COK_DATA_NAMES, offset)) return false;
    uint8_t length = cok_data_held(D, offset);
    if (!cok_data_holds(D, COK_DATA_NAMES, (long)offset + length)) return false;
    char text[256];
    for (unsigned n = 1; n <= length; ++n) {
        uint8_t byte = cok_data_held(D, (long)offset + n);
        if (byte == 0) return false;
        text[n - 1] = (char)byte;
    }
    memcpy(out, text, length);
    out[length] = '\0';
    return true;
}

#define cok_data_offset(D, t, i) cok_data_offset_((D), (t), 1, (long)(i), 0, 0)
#define cok_data_offset2(D, t, i, j) cok_data_offset_((D), (t), 2, (long)(i), (long)(j), 0)
#define cok_data_offset3(D, t, i, j, k)                                                            \
    cok_data_offset_((D), (t), 3, (long)(i), (long)(j), (long)(k))

#define cok_data_u8_at(D, t, i)                                                                    \
    cok_data_held((D), cok_data_element_((D), (t), 1, (long)(i), 0, 0, 1))
#define cok_data_s8_at(D, t, i) ((int8_t)cok_data_u8_at(D, t, i))
#define cok_data_u16_at(D, t, i)                                                                   \
    cok_data_get16_((D), cok_data_element_((D), (t), 1, (long)(i), 0, 0, 2))
#define cok_data_u8_at2(D, t, i, j)                                                                \
    cok_data_held((D), cok_data_element_((D), (t), 2, (long)(i), (long)(j), 0, 1))
#define cok_data_s8_at2(D, t, i, j) ((int8_t)cok_data_u8_at2(D, t, i, j))
#define cok_data_u8_at3(D, t, i, j, k)                                                             \
    cok_data_held((D), cok_data_element_((D), (t), 3, (long)(i), (long)(j), (long)(k), 1))
#define cok_data_s8_at3(D, t, i, j, k) ((int8_t)cok_data_u8_at3(D, t, i, j, k))

#endif
