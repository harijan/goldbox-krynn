#include "party.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(char *error, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

/* 169c:050b: remove each occurrence of c from s, testing positions 1 to the
 * original length once each, so that the character that slides into a
 * deleted one's place is not tested. */
static void remove_char(char *s, char c)
{
    size_t bound = strlen(s);
    for (size_t i = 0; i < bound; ++i)
        if (i < strlen(s) && s[i] == c) memmove(s + i, s + i + 1, strlen(s + i));
}

void cok_party_file_name(const char *name, char out[9])
{
    static const char removed[] = " .*,?/\\:;|"; /* DS:0f3c */
    char s[256];
    snprintf(s, sizeof s, "%s", name);
    for (const char *c = removed; *c != '\0'; ++c) remove_char(s, *c);
    size_t length = 0;
    for (; s[length] != '\0' && length < 8; ++length) {
        char c = s[length];
        out[length] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    out[length] = '\0';
}

void cok_party_roster_name(const char *name, char out[9])
{
    char s[256];
    snprintf(s, sizeof s, "%s", name);
    remove_char(s, '.');
    cok_party_file_name(s, out);
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* Read a whole file; NULL if it cannot be opened. */
static uint8_t *read_file(const char *path, size_t *size, bool *missing)
{
    *missing = false;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        *missing = true;
        return NULL;
    }
    uint8_t *data = NULL;
    size_t length = 0, capacity = 0;
    for (;;) {
        if (length == capacity) {
            capacity = capacity == 0 ? 4096 : capacity * 2;
            uint8_t *bigger = realloc(data, capacity);
            if (bigger == NULL) {
                free(data);
                fclose(file);
                return NULL;
            }
            data = bigger;
        }
        size_t got = fread(data + length, 1, capacity - length, file);
        length += got;
        if (got == 0) break;
    }
    bool failed = ferror(file) != 0;
    fclose(file);
    if (failed) {
        free(data);
        return NULL;
    }
    *size = length;
    return data;
}

/* Read whole records of size from path into *out; a missing file has none,
 * and a partial last record is dropped, as the loader stops at it. */
static bool read_records(const char *path, size_t size, uint8_t **out, size_t *count,
                         char *error, size_t error_size)
{
    *out = NULL;
    *count = 0;
    bool missing;
    size_t length = 0;
    uint8_t *data = read_file(path, &length, &missing);
    if (data == NULL) {
        if (missing) return true;
        fail(error, error_size, "%s: cannot read", path);
        return false;
    }
    *count = length / size;
    if (*count == 0) {
        free(data);
        return true;
    }
    *out = data;
    return true;
}

bool cok_character_read(cok_character *character, const char *dir, const char *base,
                        const cok_item_types *types, char *error, size_t error_size)
{
    return cok_character_read_file(character, dir, base, "SAV", types, error, error_size);
}

bool cok_character_read_file(cok_character *character, const char *dir, const char *base,
                             const char *extension, const cok_item_types *types, char *error,
                             size_t error_size)
{
    memset(character, 0, sizeof *character);
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
    bool missing;
    size_t size = 0;
    uint8_t *data = read_file(path, &size, &missing);
    if (data == NULL) {
        fail(error, error_size, "%s: %s", path, missing ? "not found" : "cannot read");
        return false;
    }
    /* The original reads what there is into a zeroed record. */
    memcpy(character->record, data, size < COK_CHARACTER_SIZE ? size : COK_CHARACTER_SIZE);
    free(data);
    /* Pointers left in the file: effects, items, the combat record, next. */
    static const unsigned pointers[] = {0xe3, 0x143, 0x183, 0x17f};
    for (size_t i = 0; i < sizeof pointers / sizeof *pointers; ++i)
        memset(character->record + pointers[i], 0, 4);
    uint8_t *items, *effects;
    snprintf(path, sizeof path, "%s/%s.STF", dir, base);
    if (!read_records(path, COK_ITEM_SIZE, &items, &character->item_count, error, error_size))
        return false;
    character->items = (uint8_t (*)[COK_ITEM_SIZE])(void *)items;
    snprintf(path, sizeof path, "%s/%s.SFX", dir, base);
    size_t effect_count;
    if (!read_records(path, COK_EFFECT_SIZE, &effects, &effect_count, error, error_size))
        return false;
    /* 4b6d:11e5 links each record read, keeping its first five bytes. */
    for (size_t i = 0; i < effect_count; ++i) {
        const uint8_t *e = effects + i * COK_EFFECT_SIZE;
        if (cok_character_add_effect(character, e[0], u16(e + 1), e[3], e[4] != 0) == NULL) {
            free(effects);
            fail(error, error_size, "%s: out of memory", path);
            return false;
        }
    }
    free(effects);
    /* The original flushes the keyboard (1614:045c), then recomputes. */
    char why[200];
    if (!cok_character_stats(character, types, why, sizeof why) ||
        !cok_character_levels(character, types, why, sizeof why)) {
        snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
        fail(error, error_size, "%s: %s", path, why);
        return false;
    }
    return true;
}

void cok_character_free(cok_character *character)
{
    free(character->combat);
    character->combat = NULL;
    free(character->items);
    character->items = NULL;
    character->item_count = 0;
    while (character->effects != NULL) {
        cok_effect *next = character->effects->next;
        free(character->effects);
        character->effects = next;
    }
}

const cok_combat_record *cok_character_combat(const cok_character *character)
{
    static const cok_combat_record none;
    return character->combat != NULL ? character->combat : &none;
}

void cok_character_remove_item(cok_character *c, size_t index)
{
    memmove(c->items + index, c->items + index + 1, (c->item_count - index - 1) * sizeof *c->items);
    --c->item_count;
    for (size_t s = 0; s < COK_ITEM_SLOTS; ++s) {
        if (c->slots[s] == index + 1) c->slots[s] = 0;
        else if (c->slots[s] > index + 1) --c->slots[s];
    }
    if (c->held == index + 1) c->held = 0;
    else if (c->held > index + 1) --c->held;
}

bool cok_character_insert_item(cok_character *c, size_t index, const uint8_t *item)
{
    uint8_t(*items)[COK_ITEM_SIZE] = realloc(c->items, (c->item_count + 1) * sizeof *items);
    if (items == NULL) return false;
    c->items = items;
    memmove(c->items + index + 1, c->items + index, (c->item_count - index) * sizeof *c->items);
    memcpy(c->items[index], item, COK_ITEM_SIZE);
    ++c->item_count;
    for (size_t s = 0; s < COK_ITEM_SLOTS; ++s)
        if (c->slots[s] > index) ++c->slots[s];
    if (c->held > index) ++c->held;
    return true;
}

cok_effect *cok_character_add_effect(cok_character *character, uint8_t id, uint16_t duration,
                                     uint8_t value, bool on_remove)
{
    cok_effect *effect = malloc(sizeof *effect);
    if (effect == NULL) return NULL;
    *effect = (cok_effect){id, duration, value, on_remove, NULL};
    cok_effect **link = &character->effects;
    while (*link != NULL) link = &(*link)->next;
    *link = effect;
    return effect;
}

cok_effect *cok_character_find_effect(const cok_character *character, uint8_t id)
{
    cok_effect *effect = character->effects;
    while (effect != NULL && effect->id != id) effect = effect->next;
    return effect;
}

/* Derived stats. */

bool cok_item_types_read(const char *path, cok_item_types *types, char *error, size_t error_size)
{
    bool missing;
    size_t size = 0;
    uint8_t *data = read_file(path, &size, &missing);
    if (data == NULL) {
        fail(error, error_size, "%s: %s", path, missing ? "not found" : "cannot read");
        return false;
    }
    /* 3e99:005b seeks to offset 2 and reads up to 0x810 bytes, 129 types,
     * into DS:5886; ITEMS holds 128, so the 129th stays zero. */
    memset(types, 0, sizeof *types);
    if (size > 2) {
        size_t length = size - 2 < sizeof types->type ? size - 2 : sizeof types->type;
        memcpy(types->type, data + 2, length);
    }
    free(data);
    return true;
}

/* Tables from the original's data segment. The routines index them with
 * the original's arithmetic on DS offsets (see ds_byte). */

static const uint8_t thac0_table[104] = { /* DS:3882, by class and level 0-12 */
    /* 0 */ 40, 40, 40, 40, 42, 42, 42, 44, 44, 44, 46, 46, 46,
    /* 1 */ 40, 40, 40, 40, 42, 42, 42, 44, 44, 44, 46, 46, 46,
    /* 2 */ 39, 40, 40, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
    /* 3 */ 40, 40, 40, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
    /* 4 */ 40, 40, 40, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
    /* 5 */ 39, 39, 39, 39, 39, 39, 41, 41, 41, 41, 41, 43, 43,
    /* 6 */ 40, 40, 40, 40, 40, 41, 41, 41, 41, 44, 44, 44, 44,
    /* 7 */ 40, 40, 40, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
};
static const uint8_t class_bits[8] = { /* DS:38ea, by class */
    2, 32, 8, 64, 128, 1, 4, 16,
};
static const uint8_t thief_level_table[96] = { /* DS:3911, thief skills by level 1-12 */
    /*  1 */ 30, 25, 20, 15, 10, 10, 85, 0,
    /*  2 */ 35, 29, 25, 21, 15, 10, 86, 0,
    /*  3 */ 40, 33, 30, 27, 20, 15, 87, 0,
    /*  4 */ 45, 37, 35, 33, 25, 15, 88, 20,
    /*  5 */ 50, 42, 40, 40, 31, 20, 90, 25,
    /*  6 */ 55, 47, 45, 47, 37, 20, 92, 30,
    /*  7 */ 60, 52, 50, 55, 43, 25, 94, 35,
    /*  8 */ 65, 57, 55, 62, 49, 25, 96, 40,
    /*  9 */ 70, 62, 60, 70, 56, 30, 98, 45,
    /* 10 */ 80, 67, 65, 78, 63, 30, 99, 50,
    /* 11 */ 90, 72, 70, 86, 70, 35, 99, 60,
    /* 12 */ 100, 77, 75, 94, 77, 35, 99, 65,
};
static const int8_t thief_race_table[56] = { /* DS:3971, by race */
    /* 0 */ 0, 10, 15, 0, 0, 0, -10, -5,
    /* 1 */ 5, -5, 0, 5, 10, 5, 0, 0,
    /* 2 */ 0, 5, 10, 5, 5, 10, -15, 0,
    /* 3 */ 10, 0, 0, 0, 5, 0, 0, 0,
    /* 4 */ 5, 5, 5, 10, 15, 5, -15, -5,
    /* 5 */ -5, 5, 5, 0, 0, 5, 5, -10,
    /* 6 */ 0, 0, 0, 0, 0, 0, 0, 0,
};
static const int8_t thief_dex_table[55] = { /* DS:39a9, by dexterity 9-19, skills 1-5 */
    /*  9 */ -15, -10, -10, -20, -10,
    /* 10 */ -19, -5, -10, -15, -5,
    /* 11 */ -5, 0, -5, -10, 0,
    /* 12 */ 0, 0, 0, -5, 0,
    /* 13 */ 0, 0, 0, 0, 0,
    /* 14 */ 0, 0, 0, 0, 0,
    /* 15 */ 0, 0, 0, 0, 0,
    /* 16 */ 0, -5, 0, 0, 0,
    /* 17 */ 5, 10, 0, 5, 5,
    /* 18 */ 10, 15, 5, 10, 10,
    /* 19 */ 15, 20, 10, 12, 12,
};
static const uint8_t spell_slot_table[891] = { /* DS:3c99 */
    /* table 0: spells by level 2-12 */
    1, 0, 0, 0, 0,
    0, 1, 0, 0, 0,
    0, 1, 0, 0, 0,
    1, 1, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 1, 0,
    0, 0, 1, 1, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    184, 11, 0, 0, 112, 23, 0, 0, 200, 50, 0, 0, 108, 107, 0, 0,
    216, 214, 0, 0, 176, 173, 1, 0, 232, 110, 3, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 1: spells by level 2-12 */
    0, 1, 0, 0, 0,
    1, 1, 1, 0, 0,
    1, 0, 1, 0, 0,
    0, 1, 0, 0, 0,
    0, 0, 0, 1, 0,
    0, 1, 1, 0, 0,
    0, 0, 0, 1, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    209, 7, 0, 0, 161, 15, 0, 0, 65, 31, 0, 0, 81, 70, 0, 0,
    185, 136, 0, 0, 113, 17, 1, 0, 73, 232, 1, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 2: spells by level 2-12 */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    136, 19, 0, 0, 16, 39, 0, 0, 68, 72, 0, 0, 136, 144, 0, 0,
    8, 76, 1, 0, 224, 34, 2, 0, 96, 91, 3, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 3: spells by level 2-12 */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    136, 19, 0, 0, 224, 46, 0, 0, 192, 93, 0, 0, 200, 175, 0, 0,
    24, 115, 1, 0, 152, 171, 2, 0, 48, 87, 5, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 4: spells by level 2-12 */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    1, 0, 0, 0, 0,
    1, 0, 0, 0, 0,
    0, 1, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    136, 19, 0, 0, 224, 46, 0, 0, 120, 105, 0, 0, 96, 234, 0, 0,
    72, 232, 1, 0, 64, 13, 3, 0, 40, 124, 6, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 5: spells by level 2-12 */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    1, 0, 0, 0, 0,
    1, 0, 0, 0, 0,
    0, 1, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    203, 8, 0, 0, 149, 17, 0, 0, 17, 39, 0, 0, 33, 78, 0, 0,
    65, 156, 0, 0, 145, 95, 1, 0, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 6: spells by level 2-12 */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    136, 19, 0, 0, 16, 39, 0, 0, 32, 78, 0, 0, 112, 148, 0, 0,
    216, 214, 0, 0, 160, 134, 1, 0, 64, 13, 3, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 7: spells by level 2-12 */
    1, 0, 0, 0, 0,
    0, 1, 0, 0, 0,
    1, 1, 0, 0, 0,
    1, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 1, 0, 1, 0,
    0, 0, 1, 1, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    136, 19, 0, 0, 16, 39, 0, 0, 80, 70, 0, 0, 160, 140, 0, 0,
    80, 195, 0, 0, 144, 95, 1, 0, 32, 191, 2, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* table 8: spells by level 2-12 */
    1, 0, 0, 0, 0,
    0, 1, 0, 0, 0,
    1, 1, 1, 0, 0,
    1, 1, 0, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 1, 0,
    0, 0, 1, 1, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    /* then 32-bit experience totals */
    227, 4, 0, 0, 197, 9, 0, 0, 137, 19, 0, 0, 17, 39, 0, 0,
    33, 78, 0, 0, 5, 166, 0, 0, 113, 17, 1, 0, 177, 173, 1, 0,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};
static const uint8_t save_table[480] = { /* DS:405b */
    /* class 0 */
    10, 13, 14, 16, 15,  10, 13, 14, 16, 15,  10, 13, 14, 16, 15,
    9, 12, 13, 15, 14,  9, 12, 13, 15, 14,  9, 12, 13, 15, 14,
    7, 10, 11, 13, 12,  7, 10, 11, 13, 12,  7, 10, 11, 13, 12,
    6, 9, 10, 12, 11,  6, 9, 10, 12, 11,  6, 9, 10, 12, 11,
    /* class 1 */
    10, 13, 14, 16, 15,  10, 13, 14, 16, 15,  10, 13, 14, 16, 15,
    9, 12, 13, 15, 14,  9, 12, 13, 15, 14,  9, 12, 13, 15, 14,
    7, 10, 11, 13, 12,  7, 10, 11, 13, 12,  7, 10, 11, 13, 12,
    6, 9, 10, 12, 11,  6, 9, 10, 12, 11,  6, 9, 10, 12, 11,
    /* class 2 */
    14, 15, 16, 17, 17,  14, 15, 16, 17, 17,  13, 14, 15, 16, 16,
    13, 14, 15, 16, 16,  11, 12, 13, 13, 14,  11, 12, 13, 13, 14,
    10, 11, 12, 12, 13,  10, 11, 12, 12, 13,  8, 9, 10, 9, 11,
    8, 9, 10, 9, 11,  7, 8, 9, 8, 10,  7, 8, 9, 8, 10,
    /* class 3 */
    12, 13, 14, 15, 15,  12, 13, 14, 15, 15,  11, 12, 13, 14, 14,
    11, 12, 13, 14, 14,  9, 9, 11, 11, 12,  9, 9, 11, 11, 12,
    8, 9, 10, 10, 11,  9, 9, 10, 10, 11,  6, 7, 8, 7, 9,
    6, 7, 8, 7, 9,  5, 6, 7, 6, 8,  5, 6, 7, 6, 8,
    /* class 4 */
    14, 15, 16, 17, 17,  14, 15, 16, 17, 17,  13, 14, 15, 16, 16,
    13, 14, 15, 16, 16,  11, 12, 13, 13, 14,  11, 12, 13, 13, 14,
    10, 11, 12, 12, 13,  10, 11, 12, 12, 13,  8, 9, 10, 9, 11,
    8, 9, 10, 9, 11,  7, 8, 9, 8, 10,  7, 8, 9, 8, 10,
    /* class 5 */
    14, 13, 11, 15, 12,  14, 13, 11, 15, 12,  14, 13, 11, 15, 12,
    14, 13, 11, 15, 12,  14, 13, 11, 15, 12,  13, 11, 9, 13, 10,
    13, 11, 9, 13, 10,  13, 11, 9, 13, 10,  13, 11, 9, 13, 10,
    13, 11, 9, 13, 10,  11, 9, 7, 11, 8,  11, 9, 7, 11, 8,
    /* class 6 */
    13, 12, 14, 16, 15,  13, 12, 14, 16, 15,  13, 12, 14, 16, 15,
    13, 12, 14, 16, 15,  12, 11, 12, 15, 13,  12, 11, 12, 15, 13,
    12, 11, 12, 15, 13,  12, 11, 12, 15, 13,  11, 10, 10, 14, 11,
    11, 10, 10, 14, 11,  11, 10, 10, 14, 11,  11, 10, 10, 14, 11,
    /* class 7 */
    14, 15, 16, 17, 17,  14, 15, 16, 17, 17,  13, 14, 15, 16, 16,
    13, 14, 15, 16, 16,  11, 12, 13, 13, 14,  11, 12, 13, 13, 14,
    10, 11, 12, 12, 13,  10, 11, 12, 12, 13,  8, 9, 10, 9, 11,
    8, 9, 10, 9, 11,  7, 8, 9, 8, 10,  7, 8, 9, 8, 10,
};
static const uint8_t cleric_spell_table[40] = { /* DS:423b, by spell level 1-4 */
    /* 1 */ 1, 3, 5, 6, 8, 0, 0, 0, 0, 0,
    /* 2 */ 22, 23, 24, 25, 26, 27, 28, 0, 0, 0,
    /* 3 */ 37, 39, 41, 42, 43, 0, 0, 0, 0, 0,
    /* 4 */ 67, 70, 58, 69, 0, 0, 0, 0, 0, 0,
};
static const uint8_t spell_level_table[54] = { /* DS:31b4, every 16th byte, to DS:3509 */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 7, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};
static const uint8_t spell_class_table[100] = { /* DS:31c3, every 16th byte */
    0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 4, 0, 0, 0, 0,
    0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 4, 0, 4, 4,
    4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 3, 3, 3, 3, 4, 4, 4, 4, 4, 3,
};

/* The spell table (DS:31b3), 16 bytes for each spell id 0-0x6b, to the start
 * of data_3509: byte 0 the class (0 cleric, 1 druid, 2 a deity's granted
 * power, 3 magic-user, 4 none), 1 the level, 10 the effect it adds. */
static const uint8_t spell_table[854] = { /* DS:31b3-3508 */
    /*   0 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /*   1 */ 0x00, 0x01, 0x06, 0x00, 0x06, 0x00, 0x0a, 0x04, 0x00, 0x04, 0x01, 0x02, 0x0a, 0x01, 0x00, 0x00,
    /*   2 */ 0x00, 0x01, 0x06, 0x00, 0x06, 0x00, 0x0a, 0x00, 0x00, 0x04, 0x02, 0x01, 0x0a, 0x01, 0x01, 0x00,
    /*   3 */ 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x04, 0x00, 0x02, 0x05, 0x00, 0x00, 0x00,
    /*   4 */ 0x00, 0x01, 0xff, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x05, 0x01, 0x01, 0x00,
    /*   5 */ 0x00, 0x01, 0x03, 0x00, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x04, 0x05, 0x02, 0x01, 0x00, 0x00, 0x00,
    /*   6 */ 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x04, 0x02, 0x00, 0x04, 0x08, 0x02, 0x04, 0x01, 0x00, 0x00,
    /*   7 */ 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x04, 0x02, 0x00, 0x04, 0x09, 0x02, 0x04, 0x01, 0x00, 0x00,
    /*   8 */ 0x00, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x04, 0x02, 0x00, 0x04, 0x0a, 0x02, 0x0a, 0x00, 0x00, 0x00,
    /*   9 */ 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x01, 0x02, 0x01, 0x00,
    /*  10 */ 0x03, 0x01, 0x0c, 0x00, 0x3c, 0x3c, 0x04, 0x00, 0x01, 0x04, 0x0b, 0x01, 0x01, 0x05, 0x01, 0x00,
    /*  11 */ 0x03, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x04, 0x05, 0x02, 0x01, 0x00, 0x00, 0x00,
    /*  12 */ 0x03, 0x01, 0x00, 0x02, 0x00, 0x0a, 0x04, 0x02, 0x00, 0x04, 0x0c, 0x02, 0x01, 0x00, 0x00, 0x00,
    /*  13 */ 0x03, 0x01, 0x00, 0x02, 0x00, 0x0a, 0x04, 0x02, 0x01, 0x04, 0x0d, 0x02, 0x01, 0x00, 0x01, 0x00,
    /*  14 */ 0x03, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x04, 0x0e, 0x00, 0x01, 0x00, 0x00, 0x00,
    /*  15 */ 0x03, 0x01, 0x06, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x01, 0x05, 0x01, 0x00,
    /*  16 */ 0x03, 0x01, 0x00, 0x00, 0x00, 0x02, 0x04, 0x02, 0x00, 0x04, 0x08, 0x02, 0x01, 0x01, 0x00, 0x00,
    /*  17 */ 0x03, 0x01, 0x00, 0x00, 0x00, 0x02, 0x04, 0x02, 0x00, 0x04, 0x09, 0x02, 0x01, 0x01, 0x00, 0x00,
    /*  18 */ 0x03, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x04, 0x10, 0x00, 0x0a, 0x00, 0x00, 0x00,
    /*  19 */ 0x03, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x04, 0x11, 0x02, 0x01, 0x02, 0x00, 0x00,
    /*  20 */ 0x03, 0x01, 0xff, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x01, 0x02, 0x01, 0x00,
    /*  21 */ 0x03, 0x01, 0x03, 0x04, 0x00, 0x05, 0x09, 0x00, 0x00, 0x04, 0x35, 0x01, 0x01, 0x04, 0x01, 0x01,
    /*  22 */ 0x00, 0x02, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x01, 0x00, 0x04, 0x13, 0x00, 0x05, 0x00, 0x00, 0x00,
    /*  23 */ 0x00, 0x02, 0x06, 0x00, 0x04, 0x01, 0x06, 0x00, 0x01, 0x04, 0x34, 0x01, 0x05, 0x07, 0x01, 0x00,
    /*  24 */ 0x00, 0x02, 0x00, 0x00, 0x00, 0x0a, 0x04, 0x02, 0x00, 0x04, 0x14, 0x02, 0x05, 0x00, 0x00, 0x00,
    /*  25 */ 0x00, 0x02, 0x0c, 0x00, 0x00, 0x02, 0x1f, 0x00, 0x01, 0x04, 0x15, 0x01, 0x05, 0x02, 0x01, 0x01,
    /*  26 */ 0x00, 0x02, 0x00, 0x00, 0x00, 0x3c, 0x04, 0x02, 0x00, 0x04, 0x16, 0x00, 0x01, 0x00, 0x00, 0x00,
    /*  27 */ 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x04, 0x33, 0x01, 0x05, 0x00, 0x01, 0x00,
    /*  28 */ 0x00, 0x02, 0x03, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x04, 0x17, 0x02, 0x05, 0x01, 0x00, 0x00,
    /*  29 */ 0x03, 0x02, 0x00, 0x04, 0x00, 0x05, 0x00, 0x01, 0x00, 0x04, 0x18, 0x02, 0x02, 0x01, 0x00, 0x00,
    /*  30 */ 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x04, 0x19, 0x02, 0x02, 0x02, 0x00, 0x00,
    /*  31 */ 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    /*  32 */ 0x03, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x04, 0x1c, 0x02, 0x02, 0x03, 0x00, 0x00,
    /*  33 */ 0x03, 0x02, 0x01, 0x01, 0x00, 0x01, 0x04, 0x00, 0x01, 0x04, 0x1d, 0x01, 0x02, 0x01, 0x01, 0x00,
    /*  34 */ 0x03, 0x02, 0x03, 0x00, 0x00, 0x01, 0x09, 0x00, 0x03, 0x00, 0x1e, 0x01, 0x02, 0x04, 0x01, 0x01,
    /*  35 */ 0x03, 0x02, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x02, 0x00, 0x04, 0x26, 0x00, 0x0a, 0x00, 0x00, 0x00,
    /*  36 */ 0x04, 0x07, 0x05, 0x00, 0x00, 0x00, 0x08, 0x04, 0x01, 0x04, 0x00, 0x02, 0x00, 0x02, 0x01, 0x00,
    /*  37 */ 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x04, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x00,
    /*  38 */ 0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x04, 0x21, 0x01, 0x0a, 0x03, 0x01, 0x00,
    /*  39 */ 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
    /*  40 */ 0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x04, 0x22, 0x01, 0x64, 0x04, 0x01, 0x00,
    /*  41 */ 0x00, 0x03, 0x06, 0x00, 0x00, 0x00, 0x09, 0x02, 0x00, 0x04, 0x00, 0x02, 0x04, 0x01, 0x01, 0x01,
    /*  42 */ 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x04, 0x31, 0x02, 0x06, 0x05, 0x00, 0x00,
    /*  43 */ 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x04, 0x00, 0x02, 0x06, 0x00, 0x00, 0x00,
    /*  44 */ 0x00, 0x03, 0xff, 0x00, 0x00, 0x0a, 0x04, 0x00, 0x01, 0x04, 0x24, 0x01, 0x06, 0x02, 0x01, 0x00,
    /*  45 */ 0x03, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x25, 0x01, 0x01, 0x04, 0x00, 0x00,
    /*  46 */ 0x03, 0x03, 0x0c, 0x00, 0x00, 0x01, 0x09, 0x02, 0x00, 0x04, 0x00, 0x02, 0x03, 0x01, 0x01, 0x01,
    /*  47 */ 0x03, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x0b, 0x00, 0x02, 0x04, 0x00, 0x01, 0x03, 0x07, 0x01, 0x03,
    /*  48 */ 0x03, 0x03, 0x06, 0x00, 0x03, 0x01, 0x0a, 0x04, 0x00, 0x04, 0x27, 0x02, 0x03, 0x05, 0x00, 0x00,
    /*  49 */ 0x03, 0x03, 0x0c, 0x00, 0x00, 0x02, 0x07, 0x00, 0x01, 0x04, 0x34, 0x01, 0x03, 0x06, 0x01, 0x00,
    /*  50 */ 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x04, 0x19, 0x02, 0x03, 0x01, 0x00, 0x00,
    /*  51 */ 0x03, 0x03, 0x04, 0x01, 0x00, 0x00, 0x08, 0x00, 0x02, 0x04, 0x00, 0x01, 0x03, 0x07, 0x01, 0x00,
    /*  52 */ 0x03, 0x03, 0x00, 0x00, 0x00, 0x02, 0x04, 0x02, 0x00, 0x04, 0x2d, 0x02, 0x03, 0x01, 0x00, 0x00,
    /*  53 */ 0x03, 0x03, 0x00, 0x00, 0x00, 0x02,
};

/* The rest of the initialized data from DS:3509, the lowest an index can
 * reach, to its end at DS:43bf: what the original reads when an index runs
 * past the tables above. */
static const uint8_t data_3509[889] = { /* DS:3509-3881 */
    0x04, 0x02, 0x00, 0x04, 0x2e, 0x02, 0x03, 0x02, 0x00, 0x00, 0x03, 0x03,
    0x00, 0x00, 0x00, 0x0a, 0x04, 0x02, 0x00, 0x04, 0x29, 0x02, 0x03, 0x03,
    0x00, 0x00, 0x03, 0x03, 0x09, 0x01, 0x03, 0x01, 0x0a, 0x00, 0x00, 0x04,
    0x2a, 0x01, 0x03, 0x04, 0x01, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x02, 0x00, 0x04, 0x00, 0x02, 0x06, 0x00, 0x00, 0x00, 0x04, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x27, 0x02, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x04,
    0x00, 0x02, 0x07, 0x00, 0x00, 0x00, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x04, 0x26, 0x02, 0x00, 0x02, 0x00, 0x00, 0x04, 0x06,
    0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x07,
    0x01, 0x00, 0x04, 0x06, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x34, 0x01, 0x00, 0x06, 0x01, 0x00, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x04, 0x27, 0x02, 0x00, 0x00, 0x00, 0x00, 0x04, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x07, 0x04, 0x00, 0x04, 0x47, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x06, 0x07, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x02, 0x04,
    0x00, 0x01, 0x00, 0x07, 0x01, 0x03, 0x04, 0x06, 0x0c, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x07, 0x03,
    0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04,
    0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x00, 0x01, 0x07, 0x06, 0x01, 0x00, 0x00, 0x04,
    0x03, 0x00, 0x00, 0x0a, 0x04, 0x02, 0x00, 0x04, 0x2d, 0x02, 0x07, 0x01,
    0x00, 0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 0x04,
    0x03, 0x01, 0x07, 0x04, 0x01, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x02, 0x00, 0x04, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0x05,
    0xff, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x04, 0x00, 0x01, 0x08, 0x04,
    0x01, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x05, 0x06, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x02, 0x04, 0x00, 0x01, 0x08, 0x06, 0x01, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x0a, 0x00,
    0x00, 0x00, 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x04,
    0x00, 0x01, 0x0a, 0x07, 0x01, 0x00, 0x01, 0x01, 0x03, 0x00, 0x0c, 0x00,
    0x00, 0x01, 0x00, 0x04, 0x05, 0x02, 0x03, 0x01, 0x00, 0x00, 0x01, 0x01,
    0x08, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x01, 0x04, 0x00, 0x01, 0x03, 0x01,
    0x01, 0x00, 0x01, 0x01, 0x08, 0x00, 0x00, 0x04, 0x05, 0x00, 0x01, 0x04,
    0x07, 0x01, 0x03, 0x01, 0x01, 0x00, 0x01, 0x01, 0xff, 0x00, 0x0a, 0x01,
    0x04, 0x02, 0x00, 0x04, 0x45, 0x02, 0x04, 0x01, 0x01, 0x00, 0x03, 0x04,
    0x06, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x04, 0x0b, 0x01, 0x04, 0x06,
    0x01, 0x00, 0x03, 0x04, 0x0c, 0x00, 0x02, 0x01, 0x0b, 0x00, 0x01, 0x04,
    0x23, 0x01, 0x04, 0x07, 0x01, 0x00, 0x03, 0x04, 0x00, 0x03, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x04, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x03, 0x04,
    0x06, 0x00, 0x00, 0x01, 0x08, 0x00, 0x01, 0x04, 0x6f, 0x01, 0x04, 0x06,
    0x01, 0x00, 0x03, 0x04, 0x00, 0x00, 0x02, 0x01, 0x00, 0x01, 0x00, 0x04,
    0x00, 0x02, 0x04, 0x07, 0x00, 0x00, 0x03, 0x04, 0x00, 0x01, 0x00, 0x01,
    0x04, 0x00, 0x01, 0x04, 0x1b, 0x01, 0x04, 0x04, 0x01, 0x00, 0x03, 0x04,
    0x00, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x04, 0x00, 0x01, 0x04, 0x07,
    0x01, 0x00, 0x03, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x04,
    0x3f, 0x02, 0x04, 0x06, 0x00, 0x00, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x02, 0x00, 0x04, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x04, 0x05,
    0x01, 0x00, 0x00, 0x00, 0xf0, 0x04, 0x00, 0x04, 0x20, 0x02, 0x05, 0x00,
    0x00, 0x00, 0x03, 0x05, 0x02, 0x00, 0x00, 0x01, 0x09, 0x00, 0x00, 0x04,
    0x00, 0x02, 0x05, 0x05, 0x01, 0x00, 0x03, 0x05, 0x06, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x04, 0x00, 0x01, 0x05, 0x06, 0x01, 0x00, 0x03, 0x05,
    0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x04, 0x44, 0x01, 0x05, 0x05,
    0x01, 0x00, 0x03, 0x05, 0x00, 0x01, 0x00, 0x01, 0x07, 0x00, 0x01, 0x04,
    0x34, 0x01, 0x05, 0x07, 0x01, 0x00, 0x04, 0x06, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x04, 0x49, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x04, 0x06,
    0x00, 0x00, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x04, 0x61, 0x02, 0x0a, 0x01,
    0x00, 0x00, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04,
    0x19, 0x02, 0x00, 0x01, 0x00, 0x00, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x01, 0x04, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x04, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01,
    0x00, 0x00, 0x03, 0x04, 0x00, 0x00, 0x00, 0x0a, 0x04, 0x00, 0x01, 0x04,
    0x00, 0x01, 0x04, 0x04, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x04, 0x02, 0x00, 0x04, 0x2d, 0x02, 0x03, 0x01, 0x00, 0x00, 0x02, 0x00,
    0x0c, 0x00, 0x00, 0x02, 0x1f, 0x00, 0x01, 0x04, 0x15, 0x01, 0x05, 0x02,
    0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x04,
    0x05, 0x02, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x02, 0x00, 0x04, 0x02, 0x02, 0x06, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x06, 0x00, 0x06, 0x00, 0x0a, 0x04, 0x00, 0x04, 0x01, 0x02, 0x0a, 0x01,
    0x00, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x04,
    0x0b, 0x01, 0x01, 0x04, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x01, 0x00, 0x04, 0x00, 0x01, 0x01, 0x03, 0x01, 0x00, 0x00, 0x0a,
    0x00, 0x0a, 0x00, 0x06, 0x00, 0x18, 0x00, 0x1e, 0x00, 0x0c, 0x00, 0x00,
    0x01,
};
static const uint8_t data_38f2[31] = { /* DS:38f2-3910 */
    0x02, 0x02, 0x08, 0x10, 0x20, 0x01, 0x04, 0x10, 0x10, 0x10, 0x10, 0x20,
    0x20, 0x20, 0x40, 0x40, 0x40, 0x0a, 0x0f, 0x0a, 0x0a, 0x0b, 0x0c, 0x0b,
    0x0a, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07,
};
static const uint8_t data_39e0[697] = { /* DS:39e0-3c98 */
    0x03, 0x03, 0x12, 0x10, 0x4b, 0x00, 0x0a, 0x12, 0x06, 0x12, 0x07, 0x13,
    0x06, 0x12, 0x0c, 0x12, 0x07, 0x07, 0x12, 0x10, 0x4b, 0x00, 0x08, 0x12,
    0x03, 0x12, 0x07, 0x13, 0x07, 0x12, 0x08, 0x12, 0x03, 0x03, 0x12, 0x11,
    0x5a, 0x00, 0x04, 0x12, 0x03, 0x12, 0x06, 0x12, 0x06, 0x12, 0x03, 0x12,
    0x08, 0x08, 0x12, 0x11, 0x63, 0x00, 0x03, 0x12, 0x03, 0x12, 0x03, 0x11,
    0x0c, 0x13, 0x03, 0x10, 0x09, 0x09, 0x12, 0x11, 0x63, 0x00, 0x03, 0x12,
    0x03, 0x12, 0x03, 0x11, 0x0e, 0x13, 0x03, 0x0c, 0x06, 0x06, 0x10, 0x10,
    0x00, 0x00, 0x06, 0x12, 0x03, 0x10, 0x08, 0x13, 0x0a, 0x12, 0x06, 0x12,
    0x03, 0x03, 0x12, 0x12, 0x64, 0x32, 0x03, 0x12, 0x03, 0x12, 0x03, 0x12,
    0x03, 0x12, 0x03, 0x12, 0x09, 0x00, 0x02, 0x05, 0x04, 0x08, 0x09, 0x0a,
    0x0b, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x02, 0x05, 0x06,
    0x04, 0x08, 0x0a, 0x09, 0x0b, 0x0d, 0x0e, 0x0f, 0x10, 0x00, 0x0e, 0x00,
    0x02, 0x05, 0x06, 0x04, 0x07, 0x08, 0x0a, 0x09, 0x0b, 0x0d, 0x0e, 0x0f,
    0x10, 0x06, 0x00, 0x02, 0x06, 0x08, 0x0c, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x06, 0x04, 0x08, 0x0a, 0x0c,
    0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x06, 0x04,
    0x08, 0x0a, 0x0c, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
    0x02, 0x05, 0x06, 0x04, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xfa, 0x00, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x05,
    0x06, 0x96, 0x00, 0x05, 0x06, 0x96, 0x00, 0x05, 0x06, 0xfa, 0x00, 0x0a,
    0x0a, 0x96, 0x00, 0x05, 0x04, 0x82, 0x00, 0x05, 0x06, 0xfa, 0x00, 0x0a,
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x05, 0x06, 0x96, 0x00, 0x05,
    0x06, 0x96, 0x00, 0x05, 0x06, 0xfa, 0x00, 0x0a, 0x0a, 0x96, 0x00, 0x05,
    0x04, 0x82, 0x00, 0x05, 0x06, 0x28, 0x00, 0x03, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x16, 0x00, 0x03, 0x04, 0x16, 0x00, 0x03, 0x04, 0x16, 0x00, 0x03,
    0x04, 0x28, 0x00, 0x03, 0x06, 0x16, 0x00, 0x03, 0x08, 0x16, 0x00, 0x03,
    0x04, 0x3c, 0x00, 0x03, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x03,
    0x06, 0x3c, 0x00, 0x03, 0x06, 0x3c, 0x00, 0x03, 0x06, 0x3c, 0x00, 0x03,
    0x0a, 0x16, 0x00, 0x03, 0x04, 0x16, 0x00, 0x03, 0x06, 0x3c, 0x00, 0x03,
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x03, 0x06, 0x3c, 0x00, 0x03,
    0x06, 0x3c, 0x00, 0x03, 0x06, 0x3c, 0x00, 0x03, 0x0a, 0x16, 0x00, 0x03,
    0x04, 0x16, 0x00, 0x03, 0x06, 0x21, 0x00, 0x03, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x16, 0x00, 0x03, 0x04, 0x16, 0x00, 0x03, 0x04, 0x16, 0x00, 0x03,
    0x06, 0x21, 0x00, 0x03, 0x06, 0x14, 0x00, 0x02, 0x04, 0x16, 0x00, 0x01,
    0x04, 0x12, 0x00, 0x01, 0x04, 0x12, 0x00, 0x01, 0x04, 0x0f, 0x00, 0x01,
    0x04, 0x11, 0x00, 0x01, 0x04, 0x14, 0x00, 0x01, 0x04, 0x18, 0x00, 0x02,
    0x04, 0x12, 0x00, 0x01, 0x04, 0x14, 0x00, 0x01, 0x04, 0xfa, 0x00, 0x8a,
    0x02, 0xe8, 0x03, 0xdc, 0x05, 0xd0, 0x07, 0xfa, 0x00, 0x8a, 0x02, 0xe8,
    0x03, 0xdc, 0x05, 0xd0, 0x07, 0x28, 0x00, 0x64, 0x00, 0xaf, 0x00, 0xfa,
    0x00, 0x45, 0x01, 0x3c, 0x00, 0xaf, 0x00, 0x13, 0x01, 0x90, 0x01, 0x0d,
    0x02, 0x32, 0x00, 0x96, 0x00, 0xfa, 0x00, 0x5e, 0x01, 0xc2, 0x01, 0x21,
    0x00, 0x44, 0x00, 0x65, 0x00, 0x90, 0x00, 0xc7, 0x00, 0x14, 0x00, 0x28,
    0x00, 0x3c, 0x00, 0x5a, 0x00, 0x78, 0x00, 0x06, 0x06, 0x09, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x0f, 0x09, 0x00, 0x06, 0x06, 0x07,
    0x00, 0x0a, 0x07, 0x0a, 0x08, 0x0a, 0x00, 0x0d, 0x0d, 0x0e, 0x00, 0x0e,
    0x00, 0x00, 0x09, 0x06, 0x06, 0x00, 0x00, 0x06, 0x06, 0x00, 0x09, 0x00,
    0x00, 0x0a, 0x07, 0x0a, 0x08, 0x0a, 0x00, 0x09, 0x00, 0x09, 0x00, 0x00,
    0x00, 0x09, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x0e, 0x00, 0x0e,
    0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x09, 0x00,
    0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x09, 0x00,
    0x00, 0x09, 0x09, 0x00, 0x09, 0x00, 0x00, 0x00, 0x09, 0x00, 0x09, 0x00,
    0x00, 0x3f, 0x3f, 0x3f, 0x01, 0x15, 0x3f, 0x0e, 0x01, 0x3f, 0x3f, 0x15,
    0x3f, 0x0e, 0x3f, 0x0e, 0x0e, 0x0e, 0xfe, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0xa0, 0x0f, 0x00, 0x00, 0x4c, 0x1d, 0x00,
    0x00, 0x92, 0x3b, 0x00, 0x00, 0xa8, 0x61, 0x00, 0x00, 0x40, 0x9c, 0x00,
    0x00, 0x90, 0x5f, 0x01, 0x00, 0x00, 0x71, 0x02, 0x00, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff,
};
static const uint8_t data_3ff4[103] = { /* DS:3ff4-405a */
    0x11, 0x27, 0x00, 0x00, 0x21, 0x4e, 0x00, 0x00, 0x05, 0xa6, 0x00, 0x00,
    0x71, 0x11, 0x01, 0x00, 0xb1, 0xad, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x06, 0x03, 0x06, 0x05, 0x04, 0x05, 0x04, 0x05,
    0x04, 0x02, 0x04, 0x02, 0x06, 0x05, 0x04,
};
static const uint8_t data_4263[349] = { /* DS:4263-43bf */
    0x0a, 0x0b, 0x0e, 0x0f, 0x10, 0x12, 0x13, 0x15, 0x00, 0x00, 0x1d, 0x22,
    0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2e, 0x2f, 0x31, 0x33,
    0x34, 0x36, 0x00, 0x00, 0x00, 0x00, 0x55, 0x57, 0x51, 0x52, 0x56, 0x58,
    0x59, 0x00, 0x00, 0x00, 0x09, 0x0b, 0x0c, 0x0f, 0x12, 0x13, 0x14, 0x00,
    0x00, 0x00, 0x1d, 0x1e, 0x1f, 0x20, 0x22, 0x23, 0x00, 0x00, 0x00, 0x00,
    0x2d, 0x2f, 0x30, 0x32, 0x33, 0x37, 0x00, 0x00, 0x00, 0x00, 0x55, 0x57,
    0x53, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x3f, 0xff,
    0x1f, 0xff, 0x0f, 0xff, 0x07, 0xff, 0x03, 0xff, 0x01, 0xff, 0x00, 0x7f,
    0x00, 0x3f, 0x00, 0x3f, 0x00, 0xff, 0x01, 0xff, 0x00, 0xff, 0x10, 0x7f,
    0xf8, 0x7f, 0xf8, 0x7f, 0xfc, 0x00, 0x00, 0x00, 0x40, 0x00, 0x60, 0x00,
    0x70, 0x00, 0x78, 0x00, 0x7c, 0x00, 0x7e, 0x00, 0x7f, 0x80, 0x7f, 0x00,
    0x7c, 0x00, 0x6c, 0x00, 0x46, 0x00, 0x06, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x00, 0xff, 0x0f, 0xff, 0x03, 0xff, 0x00, 0x3f, 0x00, 0x0f, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x3f, 0x00, 0xff, 0x00,
    0xff, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
    0x30, 0x00, 0x3c, 0x00, 0x3f, 0xc0, 0x3f, 0xf0, 0x3f, 0xfc, 0x3f, 0xc0,
    0x3f, 0xf0, 0x3c, 0xf0, 0x00, 0x3c, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x01, 0x01, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x01, 0x06, 0x08, 0x08, 0x08, 0x08, 0x02, 0x06,
    0x08, 0x08, 0x08, 0x08, 0x02, 0x05, 0x08, 0x08, 0x08, 0x08, 0x03, 0x05,
    0x05, 0x04, 0x04, 0x03, 0x03, 0x38, 0x39, 0x36, 0x33, 0x32, 0x31, 0x34,
    0x37, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x01, 0xb7, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
};

const cok_ds_table cok_stat_tables[] = {
    {0x31b3, sizeof spell_table, 1, spell_table},
    {0x3509, sizeof data_3509, 1, data_3509},
    {0x3882, sizeof thac0_table, 1, thac0_table},
    {0x38ea, sizeof class_bits, 1, class_bits},
    {0x38f2, sizeof data_38f2, 1, data_38f2},
    {0x3911, sizeof thief_level_table, 1, thief_level_table},
    {0x3971, sizeof thief_race_table, 1, (const uint8_t *)thief_race_table},
    {0x39a9, sizeof thief_dex_table, 1, (const uint8_t *)thief_dex_table},
    {0x39e0, sizeof data_39e0, 1, data_39e0},
    {0x3c99, sizeof spell_slot_table, 1, spell_slot_table},
    {0x3ff4, sizeof data_3ff4, 1, data_3ff4},
    {0x405b, sizeof save_table, 1, save_table},
    {0x423b, sizeof cleric_spell_table, 1, cleric_spell_table},
    {0x4263, sizeof data_4263, 1, data_4263},
    {0x31c3, sizeof spell_class_table, 16, spell_class_table},
    {0x31b4, sizeof spell_level_table, 16, spell_level_table},
};
const size_t cok_stat_table_count = sizeof cok_stat_tables / sizeof *cok_stat_tables;

/* A lookup in progress: the first lookup that fails sets error. */
typedef struct {
    char *error;
    size_t error_size;
    bool failed;
} lookup;

/* The tables with a stride of 1 cover DS:31b3-43bf without a gap; the
 * strided ones are columns of the spell table. */
bool cok_ds_byte(uint16_t offset, uint8_t *out)
{
    for (unsigned stride = 1; stride <= 16; stride += 15)
        for (size_t i = 0; i < cok_stat_table_count; ++i) {
            const cok_ds_table *t = &cok_stat_tables[i];
            if (t->stride != stride || offset < t->offset) continue;
            size_t k = (size_t)(offset - t->offset);
            if (k % stride != 0 || k / stride >= t->size) continue;
            *out = t->bytes[k / stride];
            return true;
        }
    return false;
}

/* The byte at DS:offset, from whichever table holds it. Past DS:43bf lies
 * data the game sets as it runs, which only a saving throw for a level of 90
 * or more reaches; then fail. */
static uint8_t ds_byte(lookup *l, long offset, const char *what)
{
    uint8_t byte;
    if (offset >= 0 && offset <= 0xffff && cok_ds_byte((uint16_t)offset, &byte)) return byte;
    if (!l->failed)
        fail(l->error, l->error_size, "%s reads DS:%04lx, past the original's initialized data",
             what, (unsigned long)offset & 0xffff);
    l->failed = true;
    return 0;
}

/* The type of item, or NULL past the 129 records the original reads ITEMS
 * into; beyond them lie the party's pointers (DS:6096). */
static const uint8_t *item_type(lookup *l, const cok_item_types *types, const uint8_t *item)
{
    if (item[0x2e] < COK_ITEM_TYPES) return types->type[item[0x2e]];
    if (!l->failed)
        fail(l->error, l->error_size, "item type %u is past the %d ITEMS is read into",
             item[0x2e], COK_ITEM_TYPES);
    l->failed = true;
    return NULL;
}

/* The item in slot, or NULL. */
static uint8_t *slot_item(cok_character *character, size_t slot)
{
    size_t i = character->slots[slot];
    return i == 0 || i > character->item_count ? NULL : character->items[i - 1];
}

/* 6346:1276: the armour class bonus for dexterity (+0x17). */
static int8_t dexterity_defense(const uint8_t *c)
{
    uint8_t dex = c[0x17];
    if (dex >= 1 && dex <= 3) return -4;
    if (dex >= 4 && dex <= 6) return (int8_t)(dex - 7);
    if (dex >= 15 && dex <= 18) return (int8_t)(dex - 14);
    if (dex >= 19 && dex <= 20) return 4;
    if (dex >= 21 && dex <= 23) return 5;
    if (dex >= 24 && dex <= 25) return 6;
    return 0;
}

int8_t cok_character_dexterity_missile(const uint8_t *c)
{
    uint8_t dex = c[0x17];
    if (dex < 3) return -4;
    if (dex <= 5) return (int8_t)(dex - 6);
    if (dex >= 16 && dex <= 18) return (int8_t)(dex - 15);
    if (dex >= 19 && dex <= 20) return 3;
    if (dex >= 21 && dex <= 23) return 4;
    if (dex >= 24 && dex <= 25) return 5;
    return 0;
}

/* 6346:137a: strength (+0x11) as a row of the strength tables: 3-17 as
 * they are, 18 with exceptional strength (+0x1c) 0, 1-50, 51-75, 76-90,
 * 91-99 and 100 as 18-23, and 19-25 as 24-30. The original leaves the
 * result uninitialized for strength 26 and up or exceptional strength past
 * 100; the port fails. */
static uint8_t strength_row(lookup *l, const uint8_t *c)
{
    uint8_t str = c[0x11], extra = c[0x1c];
    if (str < 18) return str;
    if (str >= 19 && str <= 25) return (uint8_t)(str + 5);
    if (str == 18) {
        if (extra == 0) return 18;
        if (extra <= 50) return 19;
        if (extra <= 75) return 20;
        if (extra <= 90) return 21;
        if (extra <= 99) return 22;
        if (extra == 100) return 23;
    }
    if (!l->failed)
        fail(l->error, l->error_size, "strength %u/%u has no row in 6346:137a", str, extra);
    l->failed = true;
    return 0;
}

/* 6346:1412: the to-hit bonus for strength, if +0x114 is set. */
static int8_t strength_hit(lookup *l, const uint8_t *c)
{
    uint8_t row = strength_row(l, c);
    if (c[0x114] == 0) return 0;
    if (row >= 1 && row <= 3) return -3;
    if (row >= 4 && row <= 5) return -2;
    if (row >= 6 && row <= 7) return -1;
    if (row >= 17 && row <= 19) return 1;
    if (row >= 20 && row <= 22) return 2;
    if (row >= 23 && row <= 25) return 3;
    if (row >= 26 && row <= 27) return 4;
    if (row >= 28 && row <= 30) return (int8_t)(row - 23);
    return 0;
}

/* 6346:14b5: the damage bonus for strength, if +0x114 is set. */
static int8_t strength_damage(lookup *l, const uint8_t *c)
{
    uint8_t row = strength_row(l, c);
    if (c[0x114] == 0) return 0;
    if (row >= 1 && row <= 2) return -2;
    if (row >= 3 && row <= 5) return -1;
    if (row == 16) return 1;
    if (row >= 17 && row <= 19) return (int8_t)(row - 16);
    if (row >= 20 && row <= 29) return (int8_t)(row - 17);
    if (row == 30) return 14;
    return 0;
}

bool cok_character_strength_bonus(const uint8_t *c, bool damage, int8_t *bonus, char *error,
                                  size_t error_size)
{
    lookup l = {error, error_size, false};
    *bonus = damage ? strength_damage(&l, c) : strength_hit(&l, c);
    return !l.failed;
}

/* 6346:153b: the weight carried before strength slows a character. */
static int16_t strength_allowance(lookup *l, const uint8_t *c)
{
    uint8_t row = strength_row(l, c);
    if (row >= 1 && row <= 3) return -350;
    if (row >= 4 && row <= 5) return -250;
    if (row >= 6 && row <= 7) return -150;
    if (row >= 12 && row <= 13) return 100;
    if (row >= 14 && row <= 15) return 200;
    if (row == 16) return 350;
    if (row >= 17 && row <= 21) return (int16_t)((row - 17) * 250 + 500);
    if (row >= 22 && row <= 26) return (int16_t)((row - 22) * 1000 + 2000);
    if (row == 27) return 7500;
    if (row >= 28 && row <= 30) return (int16_t)((row - 28) * 3000 + 9000);
    return 0;
}

bool cok_character_allowance(const uint8_t *c, int16_t *allowance, char *error, size_t error_size)
{
    lookup l = {error, error_size, false};
    *allowance = strength_allowance(&l, c);
    return !l.failed;
}

/* 6346:0023: the readied weapon's to-hit (+0x18c), attacks, dice and
 * damage bonus, from the base THAC0, dexterity for a missile weapon,
 * strength when its type says, its bonus and that of the readied arrows or
 * quarrels it shoots, and a race's bonus with some types. */
static void weapon_stats(lookup *l, cok_character *character, const cok_item_types *types)
{
    uint8_t *c = character->record;
    const uint8_t *weapon = slot_item(character, 0);
    if (weapon == NULL) return;
    const uint8_t *type = item_type(l, types, weapon);
    if (type == NULL) return;
    uint8_t id = weapon[0x2e];
    c[0x18c] = c[0x59];
    if ((type[14] & 2) != 0) c[0x18c] = (uint8_t)(c[0x18c] + cok_character_dexterity_missile(c));
    c[0x195] = type[11];
    if ((type[14] & 4) != 0) {
        c[0x18c] = (uint8_t)(c[0x18c] + strength_hit(l, c));
        c[0x195] = (uint8_t)(c[0x195] + strength_damage(l, c));
    }
    uint8_t bonus = weapon[0x32];
    const uint8_t *quarrels = slot_item(character, 12), *arrows = slot_item(character, 11);
    if (type[14] > 0x7f && quarrels != NULL) bonus = (uint8_t)(bonus + quarrels[0x32]);
    if ((type[14] & 1) != 0 && arrows != NULL) bonus = (uint8_t)(bonus + arrows[0x32]);
    c[0x195] = (uint8_t)(c[0x195] + bonus);
    /* The race bonuses add to hit only. */
    if ((c[0x5a] == 0 || c[0x5a] == 1) && ((id > 0x15 && id < 0x1b) || id == 0x13 || id == 0x12))
        ++bonus;
    if (c[0x5a] == 5 && id == 0x43) bonus = (uint8_t)(bonus + 2);
    c[0x18c] = (uint8_t)(c[0x18c] + bonus);
    c[0x191] = type[9];
    c[0x193] = type[10];
}

/* 6346:0240: readied armour (slot 2) sets the movement (+0x198) by its
 * weight: the base movement up to 150, 9 up to 399, else 6; armour with a
 * bonus other than 0 adds 3 to a movement up to 9. */
static void armour_movement(const uint8_t *item, const uint8_t *type, uint8_t *c)
{
    if (type[0] != 2) return;
    uint16_t weight = u16(item + 0x37);
    if (weight <= 0x96)
        c[0x198] = c[0xd5];
    else if (weight <= 0x18f)
        c[0x198] = 9;
    else
        c[0x198] = 6;
    if (item[0x32] != 0 && c[0x198] <= 9) c[0x198] = (uint8_t)(c[0x198] + 3);
}

/* 6346:02c8: a readied item's part of the armour class, for a type with
 * bit 7 of byte 6 set. ac holds the parts 6346:0d20 sums: [1] a shield's
 * class plus its bonus; [2] the bonuses of items whose type's armour class
 * is 0, and [3] the best bonus of such a ring, both of which add their saving
 * throw bonus (+0x33) to +0x17c; [4] the best armour class plus bonus.
 * Magic armour (slot 2, a positive bonus) setting [4] sets *magic. */
static void armour_class(const uint8_t *item, const uint8_t *type, int8_t ac[5], bool *magic,
                         uint8_t *c)
{
    uint8_t base = type[6];
    if (base <= 0x7f) return;
    base &= 0x7f;
    int8_t bonus = (int8_t)item[0x32];
    if (type[0] == 1) {
        ac[1] = (int8_t)(uint8_t)(base + bonus);
    } else if (base == 0) {
        if (type[0] == 9) {
            if (bonus > ac[3]) ac[3] = bonus;
        } else {
            ac[2] = (int8_t)(uint8_t)(ac[2] + bonus);
        }
        c[0x17c] = (uint8_t)(c[0x17c] + item[0x33]);
    } else if (base + bonus > ac[4]) {
        ac[4] = (int8_t)(uint8_t)(base + bonus);
        if (bonus > 0 && type[0] == 2) *magic = true;
    }
}

/* 6346:03e6: weight carried past the strength allowance limits the
 * movement: past 512 to 9, past 768 to 6, past 1,024 to 3. The difference
 * is taken as a signed word, so a load 32,768 or more past the allowance
 * counts as none. */
static void encumbrance(lookup *l, uint8_t *c)
{
    int16_t over = (int16_t)(uint16_t)(u16(c + 0x17d) - (uint16_t)strength_allowance(l, c));
    if (over < 0) over = 0;
    uint8_t limit = over <= 0x200 ? c[0x198] : over <= 0x300 ? 9 : over <= 0x400 ? 6 : 3;
    if (limit < c[0x198]) c[0x198] = limit;
}

bool cok_character_stats(cok_character *character, const cok_item_types *types, char *error,
                         size_t error_size)
{
    lookup l = {error, error_size, false};
    uint8_t *c = character->record;
    /* 6346:0d26 clears the far pointers; the port keeps them 0. */
    memset(character->slots, 0, sizeof character->slots);
    memset(c + 0x147, 0, 4 * COK_ITEM_SLOTS);
    c[0x142] = 0;
    c[0x17b] = 0;
    put16(c + 0x17d, 0);
    /* The original also sums the readied items' weight in DS:483c, which
     * nothing ported reads. */
    for (size_t i = 0; i < character->item_count; ++i) {
        const uint8_t *item = character->items[i];
        ++c[0x142];
        uint16_t weight = u16(item + 0x37);
        if (item[0x39] > 0) weight = (uint16_t)(item[0x39] * weight);
        put16(c + 0x17d, (uint16_t)(u16(c + 0x17d) + weight));
        if (item[0x34] == 0) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) return false;
        /* The last readied item of a slot holds it; two rings fit. */
        if (type[0] <= 8)
            character->slots[type[0]] = i + 1;
        else if (type[0] == 9 && character->slots[9] == 0)
            character->slots[9] = i + 1;
        else if (type[0] == 9 && character->slots[10] == 0)
            character->slots[10] = i + 1;
        if (item[0x2e] == 0x1e) character->slots[11] = i + 1;
        if (item[0x2e] == 0x0c) character->slots[12] = i + 1;
        c[0x17b] = (uint8_t)(c[0x17b] + type[1]);
    }
    /* Coins: six words from +0xed. */
    for (size_t i = 1; i <= 6; ++i)
        put16(c + 0x17d, (uint16_t)(u16(c + 0x17d) + u16(c + 0xeb + 2 * i)));
    for (size_t i = 1; i <= 2; ++i) {
        c[0x190 + i] = c[0x10c + i];
        c[0x192 + i] = c[0x10e + i];
        c[0x194 + i] = c[0x110 + i];
    }
    int8_t ac[5] = {0};
    bool magic = false;
    c[0x17c] = 0;
    c[0x18d] = c[0x113];
    c[0x198] = c[0xd5];
    c[0x18c] = c[0x59];
    ac[0] = dexterity_defense(c);
    if (character->slots[0] == 0) {
        c[0x18c] = (uint8_t)(c[0x18c] + strength_hit(&l, c));
        c[0x195] = (uint8_t)(c[0x195] + strength_damage(&l, c));
    }
    weapon_stats(&l, character, types);
    for (size_t i = 0; i < character->item_count && !l.failed; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x34] == 0) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) break;
        armour_movement(item, type, c);
        armour_class(item, type, ac, &magic, c);
    }
    /* Magic armour does not add to a ring's bonus. */
    if (magic) ac[3] = 0;
    encumbrance(&l, c);
    if (ac[4] < c[0x18d]) ac[4] = (int8_t)c[0x18d];
    c[0x18d] = 0;
    for (size_t i = 0; i < 5; ++i) c[0x18d] = (uint8_t)(c[0x18d] + ac[i]);
    /* From behind: no dexterity or shield, and 2 worse. */
    c[0x18e] = (uint8_t)(ac[4] + ac[2] + ac[3] - 2);
    if (((int8_t)c[0xfb] > 0 || ((int8_t)c[0x103] > 0 && cok_character_former_class(c)) ||
         (int8_t)c[0x100] > 0 || (int8_t)c[0xfd] > 0) &&
        (int8_t)c[0x5a] < 7) {
        c[0xce] = c[0xfb];
        if (c[0x100] > c[0xce]) c[0xce] = c[0x100];
        if (c[0xfd] > c[0xce]) c[0xce] = c[0xfd];
    } else {
        c[0xce] = 1;
    }
    return !l.failed;
}

bool cok_character_former_class(const uint8_t *c)
{
    int8_t level = 0;
    if (c[0x5a] == 6) {
        size_t i = 0;
        while (i < 7 && c[0xf9 + i] == 0) ++i;
        level = (int8_t)c[0xf9 + i];
    }
    return (int8_t)c[0xd7] < level;
}

/* Mark the cleric spells of each spell level 1-4 with a spell a day as
 * known (from 66c2:000f): the spells of DS:4230 + level * 10, to the first
 * 0. The original counts in DS:742f, which other routines set before use. */
static void know_cleric_spells(lookup *l, uint8_t *c)
{
    for (int level = 1; level <= 4; ++level) {
        if (c[0x11b + level] == 0) continue;
        for (int i = 1; i <= 10; ++i) {
            uint8_t spell = ds_byte(l, 0x4230 + level * 10 + i, "a cleric spell");
            if (spell == 0) break;
            c[0x62 + spell] = 1;
        }
    }
}

/* Add the spells a day of table (DS:3c8e + table * 99) for each level from
 * first to last to the count bytes from +0x11c - 1 + offset: spell levels
 * from..to (66c2:000f). */
static void add_spells(lookup *l, uint8_t *c, int table, int first, int last, size_t at, int from,
                       int to)
{
    for (int level = first; level <= last; ++level)
        for (int k = from; k <= to; ++k)
            c[at + (size_t)k] = (uint8_t)(c[at + (size_t)k] +
                                          ds_byte(l, 0x3c8e + table * 99 + level * 5 + k,
                                                  "spells a day"));
}

/* 66c2:0722: a cleric's spells a day, with the bonus spells for wisdom
 * (+0x15) 13 and up. */
static void cleric_spells(lookup *l, uint8_t *c)
{
    int8_t level = (int8_t)c[0xf9];
    if (level <= 0) return;
    int table = (int8_t)c[0x5d] > 4;
    memset(c + 0x11d, 0, 4);
    c[0x11c] = (uint8_t)(table + 1);
    add_spells(l, c, table, 2, level, 0x11b, 1, 5);
    uint8_t wisdom = c[0x15];
    if (wisdom > 12 && c[0x11c] != 0) ++c[0x11c];
    if (wisdom > 13 && c[0x11c] != 0) ++c[0x11c];
    if (wisdom > 14 && c[0x11d] != 0) ++c[0x11d];
    if (wisdom > 15 && c[0x11d] != 0) ++c[0x11d];
    if (wisdom > 16 && c[0x11e] != 0) ++c[0x11e];
    if (wisdom > 17 && c[0x11f] != 0) ++c[0x11f];
}

/* 66c2:000f: spells a day and the spells known without learning: a
 * cleric's (DS:3c8e table 0, or 1 with +0x5d above 4), a knight's from
 * level 6 unless of order 1 (table 4 for order 2, else 5) and a ranger's
 * from level 8 (table 6: druid spells 1-3 at +0x121, mage spells 1-2 at
 * +0x12b), with all druid spells (DS:31b3) known; a mage's (table 7 for
 * moon 1, else 8). Each readied item of power 1 then doubles mage spells
 * 1-3. */
static void spells(lookup *l, cok_character *character)
{
    uint8_t *c = character->record;
    memset(c + 0x11c, 0, 0x14);
    for (int class = 0; class <= 7; ++class) {
        int8_t level = (int8_t)c[0xf9 + class];
        if (level <= 0) continue;
        if (class == 0) {
            int table = (int8_t)c[0x5d] > 4;
            ++c[0x11c];
            add_spells(l, c, table, 2, level, 0x11b, 1, 5);
            cleric_spells(l, c);
            know_cleric_spells(l, c);
        } else if (class == 7) {
            if (level <= 5 || c[0x5c] == 1) continue;
            add_spells(l, c, c[0x5c] == 2 ? 4 : 5, 6, level, 0x11b, 1, 5);
            know_cleric_spells(l, c);
        } else if (class == 4) {
            if (level <= 7) continue;
            add_spells(l, c, 6, 8, level, 0x120, 1, 3);
            add_spells(l, c, 6, 8, level, 0x127, 4, 5);
            for (int spell = 1; spell <= 100; ++spell)
                if (spell_class_table[spell - 1] == 1) c[0x62 + spell] = 1;
        } else if (class == 5) {
            ++c[0x12b];
            add_spells(l, c, c[0x5e] == 1 ? 7 : 8, 2, level, 0x12a, 1, 5);
        }
    }
    for (size_t i = 0; i < character->item_count; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x3e] != 0x81 || item[0x34] == 0) continue;
        for (size_t k = 1; k <= 3; ++k) c[0x12a + k] = (uint8_t)(c[0x12a + k] * 2);
    }
}

/* 66c2:08a6: each saving throw (+0xd0) is 20, lowered to the best for the
 * levels of each class (DS:4056 + class * 60 + level * 5). The check for a
 * former class follows the loop, so it only ever looks at class 7, and
 * with its index past the loop's: a knight whose level is above its former
 * level (+0x108) also gets the throws at that level, which for level 0 are
 * the thief's at level 12. Saving throw 0 is then raised by a constitution
 * (+0x19) of 4-18 for races 3, 4 and 5 or with an item of power 6 readied,
 * and by one of 19-25 for everyone. */
static void saving_throws(lookup *l, cok_character *character)
{
    uint8_t *c = character->record;
    bool ward = false;
    for (size_t i = 0; i < character->item_count && !ward; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x3e] > 0x80 && item[0x34] != 0) ward = (item[0x3e] & 0x7f) == 6;
    }
    for (int type = 0; type <= 4; ++type) {
        uint8_t *save = &c[0xd0 + type];
        *save = 20;
        for (int class = 0; class <= 7; ++class) {
            int8_t level = (int8_t)c[0xf9 + class];
            if (level <= 0) continue;
            uint8_t throw = ds_byte(l, 0x4056 + class * 60 + level * 5 + type, "a saving throw");
            if (*save > throw) *save = throw;
        }
        if ((int8_t)c[0x100] > (int8_t)c[0x108]) {
            uint8_t throw = ds_byte(l, 0x4056 + 7 * 60 + (int8_t)c[0x108] * 5 + type,
                                    "a saving throw");
            if (*save > throw) *save = throw;
        }
        if (type != 0) continue;
        uint8_t con = c[0x19];
        if (c[0x5a] == 3 || c[0x5a] == 5 || c[0x5a] == 4 || ward) {
            if (con >= 4 && con <= 6)
                *save = (uint8_t)(*save + 1);
            else if (con >= 7 && con <= 10)
                *save = (uint8_t)(*save + 2);
            else if (con >= 11 && con <= 13)
                *save = (uint8_t)(*save + 3);
            else if (con >= 14 && con <= 17)
                *save = (uint8_t)(*save + 4);
            else if (con == 18)
                *save = (uint8_t)(*save + 5);
        }
        if (con == 19 || con == 20)
            *save = (uint8_t)(*save + 1);
        else if (con == 21 || con == 22)
            *save = (uint8_t)(*save + 2);
        else if (con == 23 || con == 24)
            *save = (uint8_t)(*save + 3);
        else if (con == 25)
            *save = (uint8_t)(*save + 4);
    }
}

/* 66c2:0b9f: the thief skills (+0xdb-+0xe2) for the thief level, plus the
 * former thief level when it may be used, at least 4 with an item of power
 * 2 readied, from the tables by level (DS:3908), race (DS:3970) and, for
 * skills 1-5, dexterity (DS:397b). A skill whose race adjustment is
 * negative and, with *extra, not covered by the level's value is 0. An
 * item of power 11 raises the level used for skills 1 and 2 to 5 and 7,
 * adding 5 to them if it was already that high and nothing if not, and
 * one of power 2 adds 10 to each skill when the level was 4 or more. Only
 * the first readied item of power 2 or 11 counts. *extra is the original's
 * uninitialized local, the byte the previous call at that stack depth
 * left there, which the caller passes (see cok_character_levels); it is
 * set for power 11 and keeps its value for the next call. */
static void thief_skills(lookup *l, cok_character *character, uint8_t *extra)
{
    uint8_t *c = character->record;
    bool power11 = false, power2 = false;
    for (size_t i = 0; i < character->item_count && !power11 && !power2; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x3e] <= 0x80 || item[0x34] == 0) continue;
        power11 = (item[0x3e] & 0x7f) == 11;
        power2 = (item[0x3e] & 0x7f) == 2;
    }
    int8_t base = (int8_t)(uint8_t)((int8_t)c[0xff] +
                                    (int8_t)c[0x107] * (cok_character_former_class(c) ? 1 : 0));
    if (base < 4 && power2) {
        base = 4;
        power2 = false;
    }
    int race = (int8_t)c[0x5a];
    for (int skill = 1; skill <= 8; ++skill) {
        int8_t level = base;
        if (power11 && skill == 1) {
            if (level < 5) {
                level = 5;
                *extra = 0;
            } else {
                *extra = 5;
            }
        } else if (power11 && skill == 2) {
            if (level < 7) {
                level = 7;
                *extra = 0;
            } else {
                *extra = 5;
            }
        }
        int8_t adjust = (int8_t)ds_byte(l, 0x3970 + race * 8 + skill, "a thief skill");
        uint8_t value = ds_byte(l, 0x3908 + level * 8 + skill, "a thief skill");
        uint8_t *skill_value = &c[0xda + skill];
        if (adjust < 0 && value < -adjust + *extra) {
            *skill_value = 0;
        } else {
            *skill_value = (uint8_t)(value + adjust + *extra);
            if (skill < 6)
                *skill_value = (uint8_t)(*skill_value +
                                         ds_byte(l, 0x397b + c[0x17] * 5 + skill, "a thief skill"));
        }
        if (power2) *skill_value = (uint8_t)(*skill_value + 10);
    }
}

bool cok_character_levels(cok_character *character, const cok_item_types *types, char *error,
                          size_t error_size)
{
    lookup l = {error, error_size, false};
    uint8_t *c = character->record;
    c[0x59] = 0;
    for (int class = 0; class <= 7; ++class) {
        int8_t level = (int8_t)c[0xf9 + class];
        uint8_t thac0 = ds_byte(&l, 0x3882 + class * 13 + level, "THAC0");
        if (thac0 > c[0x59]) c[0x59] = thac0;
        if ((int8_t)c[0xd6] < level) c[0xd6] = (uint8_t)level;
        if ((class == 2 && level > 6) || (class == 4 && level > 7) || (class == 7 && level > 6))
            c[0x10b] = 3;
    }
    if ((c[0x5d] == 6 && (c[0x5a] == 3 || c[0x5a] == 4)) || c[0x5d] == 3) ++c[0x59];
    spells(&l, character);
    saving_throws(&l, character);
    /* 66c2:0b9f's uninitialized local lies where 66c2:08a6 left its class
     * counter, 7. An interrupt between the calls could change it; the
     * characters the original saved show 7. */
    uint8_t extra = 7;
    if ((int8_t)c[0xff] > 0) thief_skills(&l, character, &extra);
    c[0x11a] = 0;
    for (int class = 0; class <= 7; ++class) {
        int8_t level = (int8_t)c[0xf9 + class], former = (int8_t)c[0x101 + class];
        if (level > 0 || (former > 0 && former < (int8_t)c[0xd6]))
            c[0x11a] = (uint8_t)(c[0x11a] + class_bits[class]);
    }
    for (size_t i = 0; i < COK_ITEM_SLOTS && !l.failed; ++i) {
        uint8_t *item = slot_item(character, i);
        if (item == NULL) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) break;
        if ((type[13] & c[0x11a]) == 0 && item[0x36] == 0) item[0x34] = 0;
    }
    if (!l.failed && cok_character_former_class(c)) {
        for (int class = 0; class <= 7; ++class) {
            int8_t former = (int8_t)c[0x101 + class];
            if (((class == 2 || class == 7) && former > 6) || (class == 4 && former > 7))
                c[0x10b] = 3;
            uint8_t thac0 = ds_byte(&l, 0x3882 + class * 13 + former, "THAC0");
            if (thac0 > c[0x59]) c[0x59] = thac0;
        }
        if ((int8_t)c[0x103] > 6 || (int8_t)c[0x105] > 7 || (int8_t)c[0x108] > 6) c[0x10b] = 3;
        /* Here the local holds what the first call left, or still 7. */
        if ((int8_t)c[0x107] > 0) thief_skills(&l, character, &extra);
    }
    return !l.failed;
}

bool cok_character_cleric_spells(cok_character *character, char *error, size_t error_size)
{
    lookup l = {error, error_size, false};
    cleric_spells(&l, character->record);
    return !l.failed;
}

bool cok_character_saving_throws(cok_character *character, char *error, size_t error_size)
{
    lookup l = {error, error_size, false};
    saving_throws(&l, character);
    return !l.failed;
}

bool cok_character_thief_skills(cok_character *character, uint8_t extra, char *error,
                                size_t error_size)
{
    lookup l = {error, error_size, false};
    thief_skills(&l, character, &extra);
    return !l.failed;
}

bool cok_party_add(cok_party *party, cok_character *character)
{
    if (party->count == COK_PARTY_RECORDS) return false;
    bool used[COK_PARTY_MAX] = {false};
    for (size_t i = 0; i < party->count; ++i) {
        uint8_t slot = party->members[i]->record[0x137];
        if (slot < COK_PARTY_MAX) used[slot] = true; /* the set at 4b6d:1969 is 0-7 */
    }
    uint8_t slot = 0;
    while (slot < COK_PARTY_MAX && used[slot]) ++slot;
    character->record[0x137] = slot;
    party->members[party->count++] = character;
    return true;
}

bool cok_party_append(cok_party *party, cok_character *character)
{
    if (party->count == COK_PARTY_RECORDS) return false;
    party->members[party->count++] = character;
    return true;
}

void cok_party_remove(cok_party *party, size_t index)
{
    if (index >= party->count) return;
    cok_character_free(party->members[index]);
    free(party->members[index]);
    for (size_t i = index + 1; i < party->count; ++i) party->members[i - 1] = party->members[i];
    party->members[--party->count] = NULL;
}

void cok_party_free(cok_party *party)
{
    while (party->count > 0) cok_party_remove(party, party->count - 1);
}

uint8_t *cok_party_record(const cok_party *party, size_t index)
{
    return index < party->count ? party->members[index]->record : NULL;
}

size_t cok_party_index(const cok_party *party, const uint8_t *record)
{
    size_t i = 0;
    while (i < party->count && party->members[i]->record != record) ++i;
    return i;
}

uint8_t *cok_party_special(const cok_party *party, const uint8_t *selected, uint8_t scan)
{
    if (party->count == 0) return NULL;
    size_t i = cok_party_index(party, selected);
    if (scan == 0x48) {
        /* With none selected, the walk for the one before it stops at the
         * last, whose next is NULL. */
        if (i == 0 || selected == NULL) return cok_party_record(party, party->count - 1);
        /* The original walks to the member before selected, and off the
         * end of the list when selected is not a member. */
        return i < party->count ? cok_party_record(party, i - 1) : NULL;
    }
    if (scan == 0x50) {
        /* The original reads the next field of the selected record, NULL
         * or not; here a non-member has no next. */
        return i + 1 < party->count ? cok_party_record(party, i + 1) : cok_party_record(party, 0);
    }
    return cok_party_record(party, 0);
}

/* Draw a Pascal string[15] from a record. */
static void draw_name(cok_picture *dst, const cok_font *font, const uint8_t *record, int x, int y,
                      uint8_t color)
{
    char name[16];
    size_t length = record[0] > 15 ? 15 : record[0];
    memcpy(name, record + 1, length);
    name[length] = '\0';
    cok_text_string(dst, font, name, x, y, color, 0);
}

/* Clear cells from x to column 38 on row y (1128:07e6). */
static void clear_row(cok_picture *dst, int x, int y)
{
    if (x <= 0x26) cok_picture_fill(dst, x, y * 8, (size_t)(0x26 - x + 1), 8, 0);
}

void cok_party_draw(cok_picture *dst, const cok_font *font, const cok_party *party,
                    const uint8_t *selected, int x, bool combat)
{
    int y = 2;
    cok_text_string(dst, font, "Name", x, y, 15, 0);
    cok_text_string(dst, font, "AC  HP", 0x21, y, 15, 0);
    y += 2;
    char text[8];
    for (size_t i = 0; i < party->count; ++i, ++y) {
        const uint8_t *c = party->members[i]->record;
        clear_row(dst, x, y);
        uint8_t color = 0x0b;
        if (c == selected) color = 15;
        else if (c[0x189] == 0) color = 0x0c;
        else if (c[0x18a] == 1 && combat) color = 0x0e;
        draw_name(dst, font, c, x, y, color);
        /* Armour class, stored as 60 - AC, right-aligned for two digits
         * or a minus and one. */
        uint8_t ac = c[0x18d];
        int width = ac <= 0x32 ? 1 : ac <= 0x3c ? 2 : ac <= 0x45 ? 1 : 0;
        snprintf(text, sizeof text, ac > 60 ? "-%d" : "%d", ac > 60 ? ac - 60 : 60 - ac);
        cok_text_string(dst, font, text, 0x20 + width, y, 0x0a, 0);
        uint8_t hp = c[0x197];
        width = hp <= 9 ? 2 : hp <= 99 ? 1 : 0;
        snprintf(text, sizeof text, "%u", hp);
        cok_text_string(dst, font, text, 0x24 + width, y, hp < c[0x62] ? 0x0e : 0x0a, 0);
    }
    clear_row(dst, x, y);
}

void cok_character_damage(uint8_t *c, uint8_t damage)
{
    uint8_t over = 0, left = 0, hp = c[0x197];
    if (hp < damage)
        over = (uint8_t)(damage - hp);
    else
        left = (uint8_t)(hp - damage);
    if (over > 9 || (left == 0 && c[0x188] == 1))
        c[0x188] = 6;
    else if (over > 0)
        c[0x188] = 5;
    else if (left == 0)
        c[0x188] = 4;
    if (c[0x188] == 0 || c[0x188] == 1) {
        c[0x197] = left;
    } else {
        c[0x189] = 0;
        c[0x197] = 0;
    }
}

bool cok_saved_game_read(const char *path, cok_saved_game *game, char *error, size_t error_size)
{
    bool missing;
    size_t size = 0;
    uint8_t *data = read_file(path, &size, &missing);
    if (data == NULL) {
        fail(error, error_size, "%s: %s", path, missing ? "not found" : "cannot read");
        return false;
    }
    if (size < COK_SAVED_GAME_SIZE) {
        free(data);
        fail(error, error_size, "%s: %zu bytes, not a saved game", path, size);
        return false;
    }
    memset(game, 0, sizeof *game);
    const uint8_t *p = data;
    game->file = *p++;
    for (size_t i = 0; i < 0x400; ++i, p += 2) game->mem4b00[i] = u16(p);
    for (size_t i = 0; i < 0x400; ++i, p += 2) game->mem7c00[i] = u16(p);
    for (size_t i = 0; i < 0x200; ++i, p += 2) game->mem7a00[i] = u16(p);
    game->map_x = (int8_t)*p++;
    game->map_y = (int8_t)*p++;
    game->direction = *p++;
    game->ahead = *p++;
    game->square = *p++;
    game->last_mode = *p++;
    game->mode = *p++;
    for (size_t i = 0; i < 3; ++i, p += 4) {
        game->wall_ids[i] = (int16_t)u16(p);
        game->wall_slots[i] = (int16_t)u16(p + 2);
    }
    game->count = *p++;
    for (size_t i = 0; i < COK_PARTY_MAX; ++i, p += 41) {
        size_t length = p[0] > 40 ? 40 : p[0];
        memcpy(game->names[i], p + 1, length);
        game->names[i][length] = '\0';
    }
    free(data);
    return true;
}

/* Write size bytes to path, replacing it. */
static bool write_file(const char *path, const uint8_t *data, size_t size, char *error,
                       size_t error_size)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fail(error, error_size, "%s: cannot write", path);
        return false;
    }
    bool ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) ok = false;
    if (!ok) fail(error, error_size, "%s: cannot write", path);
    return ok;
}

bool cok_saved_game_write(const char *path, const cok_saved_game *game, char *error,
                          size_t error_size)
{
    static uint8_t data[COK_SAVED_GAME_SIZE];
    memset(data, 0, sizeof data);
    uint8_t *p = data;
    *p++ = game->file;
    for (size_t i = 0; i < 0x400; ++i, p += 2) put16(p, game->mem4b00[i]);
    for (size_t i = 0; i < 0x400; ++i, p += 2) put16(p, game->mem7c00[i]);
    for (size_t i = 0; i < 0x200; ++i, p += 2) put16(p, game->mem7a00[i]);
    *p++ = (uint8_t)game->map_x;
    *p++ = (uint8_t)game->map_y;
    *p++ = game->direction;
    *p++ = game->ahead;
    *p++ = game->square;
    *p++ = game->last_mode;
    *p++ = game->mode;
    for (size_t i = 0; i < 3; ++i, p += 4) {
        put16(p, (uint16_t)game->wall_ids[i]);
        put16(p + 2, (uint16_t)game->wall_slots[i]);
    }
    *p++ = game->count;
    /* The original leaves the bytes after each name, and the slots past
     * the party, as its stack held them; here they are 0. */
    for (size_t i = 0; i < COK_PARTY_MAX && i < game->count; ++i, p += 41) {
        size_t length = strlen(game->names[i]);
        if (length > 40) length = 40;
        p[0] = (uint8_t)length;
        memcpy(p + 1, game->names[i], length);
    }
    return write_file(path, data, sizeof data, error, error_size);
}

bool cok_character_write(const cok_character *character, const char *dir, const char *base,
                         char *error, size_t error_size)
{
    return cok_character_write_file(character, dir, base, "SAV", error, error_size);
}

bool cok_character_write_file(const cok_character *character, const char *dir, const char *base,
                              const char *extension, char *error, size_t error_size)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
    /* The record goes as it is; its far pointers, which the original
     * writes as they are in memory and its loader clears, are 0 here. */
    if (!write_file(path, character->record, COK_CHARACTER_SIZE, error, error_size)) return false;
    /* 169c:0154 erases the items and effects, which are written again only
     * if there are any, each with its next pointer, 0 for the last. */
    snprintf(path, sizeof path, "%s/%s.STF", dir, base);
    remove(path);
    if (character->item_count > 0) {
        uint8_t *items = malloc(character->item_count * COK_ITEM_SIZE);
        if (items == NULL) {
            fail(error, error_size, "%s: out of memory", path);
            return false;
        }
        memcpy(items, character->items, character->item_count * COK_ITEM_SIZE);
        for (size_t i = 0; i < character->item_count; ++i)
            memset(items + i * COK_ITEM_SIZE + 0x2a, 0, 4);
        bool ok = write_file(path, items, character->item_count * COK_ITEM_SIZE, error,
                             error_size);
        free(items);
        if (!ok) return false;
    }
    snprintf(path, sizeof path, "%s/%s.SFX", dir, base);
    remove(path);
    size_t count = 0;
    for (const cok_effect *e = character->effects; e != NULL; e = e->next) ++count;
    if (count == 0) return true;
    uint8_t *effects = calloc(count, COK_EFFECT_SIZE);
    if (effects == NULL) {
        fail(error, error_size, "%s: out of memory", path);
        return false;
    }
    uint8_t *p = effects;
    for (const cok_effect *e = character->effects; e != NULL; e = e->next, p += COK_EFFECT_SIZE) {
        p[0] = e->id;
        put16(p + 1, e->duration);
        p[3] = e->value;
        p[4] = e->on_remove;
    }
    bool ok = write_file(path, effects, count * COK_EFFECT_SIZE, error, error_size);
    free(effects);
    return ok;
}

bool cok_character_heal(uint8_t *c, uint8_t amount, bool only_hurt, uint8_t mode)
{
    uint8_t status = c[0x188];
    if (status != 0 && status != 1 && status != 4 && status != 5) return false; /* 60f4:21ca */
    if (only_hurt && c[0x197] >= c[0x62]) return false;
    c[0x197] = (uint8_t)(c[0x197] + amount);
    if (c[0x197] > c[0x62]) c[0x197] = c[0x62];
    if (c[0x189] == 0) {
        if (c[0x188] == 5) c[0x188] = 4;
        if (c[0x188] == 4 && mode != 5) {
            c[0x188] = 0;
            c[0x189] = 1;
        }
    }
    return true;
}

void cok_party_move(cok_party *party, size_t index, bool down)
{
    size_t n = party->count;
    if (index >= n || n < 2) return;
    cok_character *moving = party->members[index];
    if (!down && index == 0) {
        memmove(party->members, party->members + 1, (n - 1) * sizeof *party->members);
        party->members[n - 1] = moving;
    } else if (down && index == n - 1) {
        memmove(party->members + 1, party->members, (n - 1) * sizeof *party->members);
        party->members[0] = moving;
    } else {
        size_t other = down ? index + 1 : index - 1;
        party->members[index] = party->members[other];
        party->members[other] = moving;
    }
}
