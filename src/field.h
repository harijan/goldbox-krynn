#ifndef COK_FIELD_H
#define COK_FIELD_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The character record's layout: where each field of a character lies in
 * its record, as a game's descriptor gives it (cok_rules' character; see
 * docs/games.md's "Games"). The record stays raw bytes, as the files and
 * the original hold it, and code reads and writes a field by its name
 * through the accessors below, which find its offset in the layout.
 * Champions' table, with what each field is and where the original reads
 * it, is in rules.c. */

enum {
    /* Champions' record, 409 bytes, as in its files (.SAV, .WHO) and its
     * monsters' (MON<n>CHA); its layout's size. */
    COK_CHARACTER_SIZE = 0x199,
    /* The largest record of any game's: the buffer a character holds.
     * Death Knights' 216-byte record and its 220-byte spells' block
     * (.WIZ) after it. */
    COK_CHARACTER_MAX = 0xd8 + 0xdc,
};

/* The fields, named for what they are. An array's elements are counted
 * from the field's first index, as the original's code counts them (a
 * Pascal array's lower bound): the thief skills 1-8, the class levels
 * 0-7. */
typedef enum {
    COK_FIELD_NAME,                      /* A Pascal string: 0 the length, 1-15 the text. */
    COK_FIELD_SCORE_BASE,                /* The scores before effects, by COK_SCORE_. */
    COK_FIELD_SCORE,                     /* The scores now. */
    COK_FIELD_STRENGTH_EXCEPTIONAL,      /* Exceptional strength now, 18/xx. */
    COK_FIELD_STRENGTH_EXCEPTIONAL_BASE, /* And before effects. */
    COK_FIELD_MEMORIZED,                 /* The spells memorized, from 0. */
    COK_FIELD_STUDY_HOURS,               /* The hours memorizing and scribing need. */
    COK_FIELD_BASE_THAC0,                /* 60 - THAC0, by class and level. */
    COK_FIELD_RACE,
    COK_FIELD_CLASS,                     /* The class or classes, as one id. */
    COK_FIELD_KNIGHT_ORDER,
    COK_FIELD_DEITY,
    COK_FIELD_MAGIC_ORDER,
    COK_FIELD_KNIGHT_TIER,               /* Set from the knight levels by effect 0x6e. */
    COK_FIELD_AGE,
    COK_FIELD_MAX_HIT_POINTS,
    COK_FIELD_KNOWN_SPELL,               /* By spell id, from 1. */
    COK_FIELD_FIGHTER_LEVEL,             /* The highest fighter, ranger or knight level. */
    COK_FIELD_SIZE,                      /* Its size in combat: 1 a man's; larger above. */
    COK_FIELD_SAVING_THROW,              /* By type, 0-4. */
    COK_FIELD_BASE_MOVEMENT,
    COK_FIELD_HIGHEST_LEVEL,
    COK_FIELD_CHANGE_LEVEL,              /* The level a human changed class at. */
    COK_FIELD_SPREAD_LEVELS,             /* Levels SPREAD is shared out over. */
    COK_FIELD_SPREAD,                    /* Shrinks by a share at each level trained. */
    COK_FIELD_UNDEAD,                    /* The kind of undead, for turning; 0 none. */
    COK_FIELD_THIEF_SKILL,               /* 1-8. */
    COK_FIELD_EFFECTS,                   /* A far pointer: the spell effects. */
    COK_FIELD_NPC,                       /* 0x80 and up for an NPC. */
    COK_FIELD_SHARES,                    /* An NPC's shares of the treasure. */
    COK_FIELD_COINS,                     /* By kind, 0-6, words. */
    COK_FIELD_CLASS_LEVEL,               /* By class, 0-7. */
    COK_FIELD_FORMER_LEVEL,              /* By class, 0-7. */
    COK_FIELD_UNDRAINED_LEVEL,           /* By class, 0-7: the levels before a drain. */
    COK_FIELD_UNDRAINED_EXPERIENCE,      /* And the experience, */
    COK_FIELD_UNDRAINED_MAX_HIT_POINTS,  /* and the maximum hit points. */
    COK_FIELD_GENDER,
    COK_FIELD_ALIGNMENT,
    COK_FIELD_BASE_ATTACKS,              /* By attack, 1-2: attacks a round, doubled. */
    COK_FIELD_BASE_DICE,                 /* By attack, 1-2: dice of damage. */
    COK_FIELD_BASE_DICE_SIDES,           /* By attack, 1-2. */
    COK_FIELD_BASE_DAMAGE_BONUS,         /* By attack, 1-2. */
    COK_FIELD_BASE_AC,                   /* 60 - AC. */
    COK_FIELD_STRENGTH_BONUSES,          /* Set when strength adds to hit and damage. */
    COK_FIELD_MONSTER_ID,                /* The monster an NPC was read as: its icons. */
    COK_FIELD_EXPERIENCE,
    COK_FIELD_USABLE_CLASSES,            /* The classes whose items it can use, as bits. */
    COK_FIELD_BASE_HIT_POINTS,           /* The hit points the maximum is built from. */
    COK_FIELD_SPELLS_A_DAY,              /* By spell class (rows 0-3) and level, from 1. */
    COK_FIELD_MONSTER_EXPERIENCE,        /* The experience a monster is worth, */
    COK_FIELD_MONSTER_HIT_POINT_EXPERIENCE, /* and more for each hit point. */
    COK_FIELD_ICON_HEAD,                 /* A player's combat icon's head (CHEAD). */
    COK_FIELD_ICON_BODY,                 /* And its body (CBODY). */
    COK_FIELD_ICON,                      /* The combat icon's slot. */
    COK_FIELD_ICON_SIZE,                 /* A player's icon's size: 1 small, 2 tall. */
    COK_FIELD_ICON_COLOURS,              /* And its six colours, 1-6, two to a byte. */
    COK_FIELD_CREATURE_FLAGS,            /* Two bytes of flags. */
    COK_FIELD_ITEM_COUNT,
    COK_FIELD_ITEMS,                     /* A far pointer: the items. */
    COK_FIELD_ITEM_SLOTS,                /* Far pointers: the readied items, 0-12. */
    COK_FIELD_HANDS,                     /* Hands in use. */
    COK_FIELD_SAVE_BONUS,
    COK_FIELD_WEIGHT,                    /* Weight carried. */
    COK_FIELD_NEXT,                      /* A far pointer: the next record in the list. */
    COK_FIELD_COMBAT,                    /* A far pointer: the combat record. */
    COK_FIELD_MAGIC_RESISTANCE,
    COK_FIELD_SPELLS,                    /* A far pointer: the spells' block. */
    COK_FIELD_MAY_TRAIN,                 /* Set when a class may train (dkk +0xc6). */
    COK_FIELD_STATUS,                    /* 0 okay, 4 unconscious, 5 dying, 6 dead. */
    COK_FIELD_ACTIVE,                    /* Set while it can act. */
    COK_FIELD_HOSTILE,                   /* 1 for one fighting against the party. */
    COK_FIELD_COMPUTER,                  /* Set while the computer controls it. */
    COK_FIELD_THAC0,                     /* 60 - THAC0. */
    COK_FIELD_AC,                        /* 60 - AC. */
    COK_FIELD_AC_BEHIND,                 /* 60 - AC from behind. */
    COK_FIELD_ATTACKS_LEFT,              /* By attack, 1-2: attacks left this round. */
    COK_FIELD_DICE,                      /* By attack, 1-2. */
    COK_FIELD_DICE_SIDES,                /* By attack, 1-2. */
    COK_FIELD_DAMAGE_BONUS,              /* By attack, 1-2. */
    COK_FIELD_HIT_POINTS,
    COK_FIELD_MOVEMENT,
    COK_FIELD_COUNT
} cok_field;

/* The scores' indices in COK_FIELD_SCORE and SCORE_BASE. */
enum {
    COK_SCORE_STRENGTH,
    COK_SCORE_INTELLIGENCE,
    COK_SCORE_WISDOM,
    COK_SCORE_DEXTERITY,
    COK_SCORE_CONSTITUTION,
    COK_SCORE_CHARISMA,
};

/* Where one field lies. width 0: the game has no such field, and code
 * that reads it must not be reached for that game. */
typedef struct {
    uint16_t offset;  /* Of the first element, from the record's start. */
    uint8_t width;    /* Bytes: 1, 2 or 4 (little-endian). */
    uint8_t count;    /* Elements: 1 for a single value. */
    uint8_t first;    /* The index of the first element. */
    uint8_t stride;   /* Bytes from one element to the next: the width, or
                       * more where two arrays interleave (the scores). */
    uint8_t rows;     /* Rows of count elements: 1, or more for a table
                       * (spells a day, by spell class and level). */
    uint8_t row_stride; /* Bytes from one row to the next. */
    bool is_signed;   /* The original reads it as signed (documentation: the
                       * accessors read a byte either way, u8 or s8). */
    bool pointer;     /* A far pointer: meaningless in a file, 0 in the port. */
} cok_field_place;

typedef struct {
    /* The record's bytes in memory, at most COK_CHARACTER_MAX, and the bytes
     * of its file (.SAV, .WHO), from the record's start. */
    uint16_t size, file_size;
    /* Where a game keeps the spells in a block of their own, which the
     * record points to (SPELLS) and a file of its own holds (Death
     * Knights' .WIZ, MON1WIZ): the block's bytes, held in the record past
     * its file's bytes, at spells_offset; 0 where the record holds them. */
    uint16_t spells_offset, spells_size;
    cok_field_place field[COK_FIELD_COUNT];
} cok_character_layout;

/* The accessors. Each takes the layout first (code names it L: the game's,
 * cok_game_rules(game)->character, or rules->character where a routine is
 * handed the rules), then the record, then the field, then for an array
 * the element's index, of any integer type as a subscript, and for a table
 * its row first (the _at and _at2 forms; a single value's form asserts the
 * field is one). Each asserts that the game has the field, that the index
 * is one of its elements and that the field is as wide as the access:
 *
 *   u8   the byte as it is, as c[x] reads it;
 *   s8   the byte as signed, as (int8_t)c[x] reads it, of any byte field
 *        (the layout's is_signed records how the original reads a field,
 *        but code reads it as the code before the layout did: a site that
 *        cast to int8_t uses s8, one that did not uses u8);
 *   u16, u32   a little-endian word or double word;
 *   set8, set16, set32   write one;
 *   add8   c[x] = (uint8_t)(c[x] + delta), wrapping as a byte;
 *   cok_field_offset(_at, _at2)   where it lies, for a pointer or a span
 *          (a name's text, a memset), with cok_field_size its bytes. */

/* Whether the game's records have field f. */
static inline bool cok_field_present(const cok_character_layout *L, cok_field f)
{
    return L->field[f].width != 0;
}

/* The offset of element i of row row of field f. */
static inline size_t cok_field_offset_at2_(const cok_character_layout *L, cok_field f, long row,
                                           long i)
{
    const cok_field_place *d = &L->field[f];
    assert(d->width != 0 && "the game's records have no such field");
    assert(row >= 0 && row < d->rows && "past the field's rows");
    assert(i >= d->first && i < (long)d->first + d->count && "past the field's elements");
    return d->offset + (size_t)row * d->row_stride + (size_t)(i - d->first) * d->stride;
}

/* The offset of element i of field f, which is not a table. */
static inline size_t cok_field_offset_at_(const cok_character_layout *L, cok_field f, long i)
{
    assert(L->field[f].rows == 1 && "a table: give the row");
    return cok_field_offset_at2_(L, f, 0, i);
}

/* The offset of field f, or of its first element. */
static inline size_t cok_field_offset(const cok_character_layout *L, cok_field f)
{
    return cok_field_offset_at2_(L, f, 0, L->field[f].first);
}

/* The index of the first element of field f: a loop over it runs from
 * first to first + count - 1. */
static inline size_t cok_field_first(const cok_character_layout *L, cok_field f)
{
    assert(L->field[f].width != 0 && "the game's records have no such field");
    return L->field[f].first;
}

/* The number of elements of field f (of a row, in a table). */
static inline size_t cok_field_count(const cok_character_layout *L, cok_field f)
{
    assert(L->field[f].width != 0 && "the game's records have no such field");
    return L->field[f].count;
}

/* The bytes field f spans, from its first element to the end of its last. */
static inline size_t cok_field_size(const cok_character_layout *L, cok_field f)
{
    const cok_field_place *d = &L->field[f];
    assert(d->width != 0 && "the game's records have no such field");
    return (size_t)(d->rows - 1) * d->row_stride + (size_t)(d->count - 1) * d->stride + d->width;
}

/* Where element i of row row of field f would lie, unchecked: the
 * original's own arithmetic for an index it does not bound, which past the
 * field reaches the record's other bytes, or leaves it (the caller checks
 * against the layout's size). The game must have the field. */
static inline long cok_field_offset_unchecked(const cok_character_layout *L, cok_field f, long row,
                                              long i)
{
    const cok_field_place *d = &L->field[f];
    assert(d->width != 0 && "the game's records have no such field");
    return (long)d->offset + row * d->row_stride + (i - d->first) * d->stride;
}

/* Whether field f lies in the spells' block (spells_offset), which a
 * record may not hold. */
static inline bool cok_field_in_spells(const cok_character_layout *L, cok_field f)
{
    return L->spells_size != 0 && L->field[f].offset >= L->spells_offset;
}

/* Whether record c holds its spells' block: always where the record holds
 * the spells itself; where they lie in a block of their own, while its
 * far pointer (SPELLS) is not nil, which the port marks with the
 * pointer's first byte 1 (cok_field_set_spells) and keeps 0 in files. */
static inline bool cok_field_has_spells(const cok_character_layout *L, const uint8_t *c)
{
    if (L->spells_size == 0) return true;
    assert(L->field[COK_FIELD_SPELLS].pointer && "the spells' block has no pointer");
    return c[L->field[COK_FIELD_SPELLS].offset] != 0;
}

/* Give record c a spells' block (zeroed) or take it away, where the game
 * keeps one. */
static inline void cok_field_set_spells(const cok_character_layout *L, uint8_t *c, bool held)
{
    assert(L->spells_size != 0 && L->field[COK_FIELD_SPELLS].pointer);
    for (size_t i = 0; i < L->spells_size; ++i) c[L->spells_offset + i] = 0;
    for (size_t i = 0; i < 4; ++i) c[L->field[COK_FIELD_SPELLS].offset + i] = 0;
    if (held) c[L->field[COK_FIELD_SPELLS].offset] = 1;
}

/* Whether the byte at offset belongs to a far pointer, which the port does
 * not keep. */
static inline bool cok_field_pointer_at(const cok_character_layout *L, long offset)
{
    for (int f = 0; f < COK_FIELD_COUNT; ++f) {
        const cok_field_place *d = &L->field[f];
        if (d->pointer && offset >= d->offset &&
            offset < (long)(d->offset + cok_field_size(L, (cok_field)f)))
            return true;
    }
    return false;
}

/* Where an access lies, checked: width is the access's, a constant at
 * every use, so that the checks fold to one or two compares: the field is
 * that wide (which also says the game has it), and, for one value,
 * neither an array nor a table; for element i, not a table and i one of
 * its elements; for element i of row row, both in it. */
static inline size_t cok_field_one_(const cok_character_layout *L, cok_field f, int width)
{
    const cok_field_place *d = &L->field[f];
    assert(d->width == width && "the field is not that wide, or the game has no such field");
    assert(d->count == 1 && d->rows == 1 && "an array: give the element");
    return d->offset;
}

static inline size_t cok_field_el_(const cok_character_layout *L, cok_field f, long i, int width)
{
    const cok_field_place *d = &L->field[f];
    assert(d->width == width && "the field is not that wide, or the game has no such field");
    assert(d->rows == 1 && "a table: give the row");
    assert((unsigned long)(i - d->first) < d->count && "past the field's elements");
    return d->offset + (size_t)(i - d->first) * d->stride;
}

static inline size_t cok_field_el2_(const cok_character_layout *L, cok_field f, long row, long i,
                                    int width)
{
    const cok_field_place *d = &L->field[f];
    assert(d->width == width && "the field is not that wide, or the game has no such field");
    assert((unsigned long)row < d->rows && "past the field's rows");
    assert((unsigned long)(i - d->first) < d->count && "past the field's elements");
    return d->offset + (size_t)row * d->row_stride + (size_t)(i - d->first) * d->stride;
}

/* The reads and writes at an offset the above found. */
static inline uint8_t cok_field_get8_(const uint8_t *c, size_t at)
{
    return c[at];
}

static inline uint16_t cok_field_get16_(const uint8_t *c, size_t at)
{
    return (uint16_t)(c[at] | c[at + 1] << 8);
}

static inline uint32_t cok_field_get32_(const uint8_t *c, size_t at)
{
    return (uint32_t)c[at] | (uint32_t)c[at + 1] << 8 | (uint32_t)c[at + 2] << 16 |
           (uint32_t)c[at + 3] << 24;
}

static inline void cok_field_put8_(uint8_t *c, size_t at, uint8_t value)
{
    c[at] = value;
}

static inline void cok_field_add8_(uint8_t *c, size_t at, int delta)
{
    c[at] = (uint8_t)(c[at] + delta);
}

static inline void cok_field_put16_(uint8_t *c, size_t at, uint16_t value)
{
    c[at] = (uint8_t)value;
    c[at + 1] = (uint8_t)(value >> 8);
}

static inline void cok_field_put32_(uint8_t *c, size_t at, uint32_t value)
{
    for (int k = 0; k < 4; ++k) c[at + (size_t)k] = (uint8_t)(value >> 8 * k);
}

/* The accessors by name: a single value, element i, element i of row row. */
#define COK_FIELD_ONE_(L, f, width) cok_field_one_((L), (f), (width))
#define COK_FIELD_EL_(L, f, i, width) cok_field_el_((L), (f), (long)(i), (width))
#define COK_FIELD_EL2_(L, f, row, i, width)                                                        \
    cok_field_el2_((L), (f), (long)(row), (long)(i), (width))

#define cok_field_offset_at(L, f, i) cok_field_offset_at_((L), (f), (long)(i))
#define cok_field_offset_at2(L, f, row, i) cok_field_offset_at2_((L), (f), (long)(row), (long)(i))

#define cok_field_u8(L, c, f) cok_field_get8_((c), COK_FIELD_ONE_(L, f, 1))
#define cok_field_s8(L, c, f) ((int8_t)cok_field_get8_((c), COK_FIELD_ONE_(L, f, 1)))
#define cok_field_u16(L, c, f) cok_field_get16_((c), COK_FIELD_ONE_(L, f, 2))
#define cok_field_u32(L, c, f) cok_field_get32_((c), COK_FIELD_ONE_(L, f, 4))
#define cok_field_set8(L, c, f, v) cok_field_put8_((c), COK_FIELD_ONE_(L, f, 1), (v))
#define cok_field_add8(L, c, f, d) cok_field_add8_((c), COK_FIELD_ONE_(L, f, 1), (d))
#define cok_field_set16(L, c, f, v) cok_field_put16_((c), COK_FIELD_ONE_(L, f, 2), (v))
#define cok_field_set32(L, c, f, v) cok_field_put32_((c), COK_FIELD_ONE_(L, f, 4), (v))

#define cok_field_u8_at(L, c, f, i) cok_field_get8_((c), COK_FIELD_EL_(L, f, i, 1))
#define cok_field_s8_at(L, c, f, i) ((int8_t)cok_field_get8_((c), COK_FIELD_EL_(L, f, i, 1)))
#define cok_field_u16_at(L, c, f, i) cok_field_get16_((c), COK_FIELD_EL_(L, f, i, 2))
#define cok_field_u32_at(L, c, f, i) cok_field_get32_((c), COK_FIELD_EL_(L, f, i, 4))
#define cok_field_set8_at(L, c, f, i, v) cok_field_put8_((c), COK_FIELD_EL_(L, f, i, 1), (v))
#define cok_field_add8_at(L, c, f, i, d) cok_field_add8_((c), COK_FIELD_EL_(L, f, i, 1), (d))
#define cok_field_set16_at(L, c, f, i, v) cok_field_put16_((c), COK_FIELD_EL_(L, f, i, 2), (v))
#define cok_field_set32_at(L, c, f, i, v) cok_field_put32_((c), COK_FIELD_EL_(L, f, i, 4), (v))

#define cok_field_u8_at2(L, c, f, row, i) cok_field_get8_((c), COK_FIELD_EL2_(L, f, row, i, 1))
#define cok_field_s8_at2(L, c, f, row, i)                                                          \
    ((int8_t)cok_field_get8_((c), COK_FIELD_EL2_(L, f, row, i, 1)))
#define cok_field_set8_at2(L, c, f, row, i, v)                                                     \
    cok_field_put8_((c), COK_FIELD_EL2_(L, f, row, i, 1), (v))
#define cok_field_add8_at2(L, c, f, row, i, d)                                                     \
    cok_field_add8_((c), COK_FIELD_EL2_(L, f, row, i, 1), (d))

#endif
