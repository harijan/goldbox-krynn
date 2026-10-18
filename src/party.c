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

bool cok_character_read(cok_character *character, const cok_rules *rules, const char *dir,
                        const char *base, const cok_item_types *types, char *error,
                        size_t error_size)
{
    return cok_character_read_file(character, rules, dir, base, rules->saves.record, types,
                                   error, error_size);
}

bool cok_character_read_file(cok_character *character, const cok_rules *rules, const char *dir,
                             const char *base, const char *extension,
                             const cok_item_types *types, char *error, size_t error_size)
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
    const cok_character_layout *L = rules->character;
    /* The original reads what there is into a zeroed record. */
    memcpy(character->record, data, size < L->file_size ? size : L->file_size);
    free(data);
    /* Pointers left in the file: effects, items, the combat record, next. */
    static const cok_field pointers[] = {COK_FIELD_EFFECTS, COK_FIELD_ITEMS, COK_FIELD_COMBAT,
                                         COK_FIELD_NEXT};
    for (size_t i = 0; i < sizeof pointers / sizeof *pointers; ++i)
        memset(character->record + cok_field_offset(L, pointers[i]), 0,
               cok_field_size(L, pointers[i]));
    uint8_t *items, *effects;
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, rules->saves.items);
    if (!read_records(path, COK_ITEM_SIZE, &items, &character->item_count, error, error_size))
        return false;
    character->items = (uint8_t (*)[COK_ITEM_SIZE])(void *)items;
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, rules->saves.effects);
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
    if (!cok_character_stats(character, rules, types, why, sizeof why) ||
        !cok_character_levels(character, rules, types, why, sizeof why)) {
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

static uint32_t effects_made;

uint32_t cok_effects_made(void)
{
    return effects_made;
}

cok_effect *cok_character_add_effect(cok_character *character, uint8_t id, uint16_t duration,
                                     uint8_t value, bool on_remove)
{
    cok_effect *effect = malloc(sizeof *effect);
    if (effect == NULL) return NULL;
    *effect = (cok_effect){id, duration, value, on_remove, NULL, ++effects_made};
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

/* A lookup in progress: the first lookup that fails sets error. The
 * tables are the game's data segment's (D). */
typedef struct {
    char *error;
    size_t error_size;
    bool failed;
    const cok_data_segment *D;
} lookup;

/* The byte at DS:offset, from whichever table holds it. Past DS:43bf lies
 * data the game sets as it runs, which only a saving throw for a level of 90
 * or more reaches; then fail. */
static uint8_t ds_byte(lookup *l, long offset, const char *what)
{
    uint8_t byte;
    assert(l->D != NULL);
    if (offset >= 0 && offset <= 0xffff && cok_data_byte(l->D, (uint16_t)offset, &byte))
        return byte;
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
static int8_t dexterity_defense(const cok_character_layout *L, const uint8_t *c)
{
    uint8_t dex = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_DEXTERITY);
    if (dex >= 1 && dex <= 3) return -4;
    if (dex >= 4 && dex <= 6) return (int8_t)(dex - 7);
    if (dex >= 15 && dex <= 18) return (int8_t)(dex - 14);
    if (dex >= 19 && dex <= 20) return 4;
    if (dex >= 21 && dex <= 23) return 5;
    if (dex >= 24 && dex <= 25) return 6;
    return 0;
}

int8_t cok_character_dexterity_missile(const uint8_t *c, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    uint8_t dex = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_DEXTERITY);
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
static uint8_t strength_row(lookup *l, const cok_character_layout *L, const uint8_t *c)
{
    uint8_t str = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_STRENGTH),
            extra = cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL);
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
static int8_t strength_hit(lookup *l, const cok_character_layout *L, const uint8_t *c)
{
    uint8_t row = strength_row(l, L, c);
    if (cok_field_u8(L, c, COK_FIELD_STRENGTH_BONUSES) == 0) return 0;
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
static int8_t strength_damage(lookup *l, const cok_character_layout *L, const uint8_t *c)
{
    uint8_t row = strength_row(l, L, c);
    if (cok_field_u8(L, c, COK_FIELD_STRENGTH_BONUSES) == 0) return 0;
    if (row >= 1 && row <= 2) return -2;
    if (row >= 3 && row <= 5) return -1;
    if (row == 16) return 1;
    if (row >= 17 && row <= 19) return (int8_t)(row - 16);
    if (row >= 20 && row <= 29) return (int8_t)(row - 17);
    if (row == 30) return 14;
    return 0;
}

bool cok_character_strength_bonus(const uint8_t *c, const cok_rules *rules, bool damage,
                                  int8_t *bonus, char *error, size_t error_size)
{
    lookup l = {error, error_size, false, rules->data};
    const cok_character_layout *L = rules->character;
    *bonus = damage ? strength_damage(&l, L, c) : strength_hit(&l, L, c);
    return !l.failed;
}

/* 6346:153b: the weight carried before strength slows a character. */
static int16_t strength_allowance(lookup *l, const cok_character_layout *L, const uint8_t *c)
{
    uint8_t row = strength_row(l, L, c);
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

bool cok_character_allowance(const uint8_t *c, const cok_rules *rules, int16_t *allowance,
                             char *error, size_t error_size)
{
    lookup l = {error, error_size, false, rules->data};
    *allowance = strength_allowance(&l, rules->character, c);
    return !l.failed;
}

/* 6346:0023: the readied weapon's to-hit (+0x18c), attacks, dice and
 * damage bonus, from the base THAC0, dexterity for a missile weapon,
 * strength when its type says, its bonus and that of the readied arrows or
 * quarrels it shoots, and a race's bonus with some types. */
static void weapon_stats(lookup *l, const cok_rules *rules, cok_character *character,
                         const cok_item_types *types)
{
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    const uint8_t *weapon = slot_item(character, 0);
    if (weapon == NULL) return;
    const uint8_t *type = item_type(l, types, weapon);
    if (type == NULL) return;
    uint8_t id = weapon[0x2e];
    cok_field_set8(L, c, COK_FIELD_THAC0, cok_field_u8(L, c, COK_FIELD_BASE_THAC0));
    if ((type[14] & 2) != 0)
        cok_field_add8(L, c, COK_FIELD_THAC0, cok_character_dexterity_missile(c, rules));
    cok_field_set8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, type[11]);
    if ((type[14] & 4) != 0) {
        cok_field_add8(L, c, COK_FIELD_THAC0, strength_hit(l, L, c));
        cok_field_add8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, strength_damage(l, L, c));
    }
    uint8_t bonus = weapon[0x32];
    const uint8_t *quarrels = slot_item(character, 12), *arrows = slot_item(character, 11);
    if (type[14] > 0x7f && quarrels != NULL) bonus = (uint8_t)(bonus + quarrels[0x32]);
    if ((type[14] & 1) != 0 && arrows != NULL) bonus = (uint8_t)(bonus + arrows[0x32]);
    cok_field_add8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, bonus);
    /* The race bonuses add to hit only. */
    uint8_t race = cok_field_u8(L, c, COK_FIELD_RACE);
    if ((race == 0 || race == 1) && ((id > 0x15 && id < 0x1b) || id == 0x13 || id == 0x12))
        ++bonus;
    if (race == 5 && id == 0x43) bonus = (uint8_t)(bonus + 2);
    cok_field_add8(L, c, COK_FIELD_THAC0, bonus);
    cok_field_set8_at(L, c, COK_FIELD_DICE, 1, type[9]);
    cok_field_set8_at(L, c, COK_FIELD_DICE_SIDES, 1, type[10]);
}

/* 6346:0240: readied armour (slot 2) sets the movement (+0x198) by its
 * weight: the base movement up to 150, 9 up to 399, else 6; armour with a
 * bonus other than 0 adds 3 to a movement up to 9. */
static void armour_movement(const cok_character_layout *L, const uint8_t *item, const uint8_t *type,
                            uint8_t *c)
{
    if (type[0] != 2) return;
    uint16_t weight = u16(item + 0x37);
    if (weight <= 0x96)
        cok_field_set8(L, c, COK_FIELD_MOVEMENT, cok_field_u8(L, c, COK_FIELD_BASE_MOVEMENT));
    else if (weight <= 0x18f)
        cok_field_set8(L, c, COK_FIELD_MOVEMENT, 9);
    else
        cok_field_set8(L, c, COK_FIELD_MOVEMENT, 6);
    if (item[0x32] != 0 && cok_field_u8(L, c, COK_FIELD_MOVEMENT) <= 9)
        cok_field_add8(L, c, COK_FIELD_MOVEMENT, 3);
}

/* 6346:02c8: a readied item's part of the armour class, for a type with
 * bit 7 of byte 6 set. ac holds the parts 6346:0d20 sums: [1] a shield's
 * class plus its bonus; [2] the bonuses of items whose type's armour class
 * is 0, and [3] the best bonus of such a ring, both of which add their saving
 * throw bonus (+0x33) to +0x17c; [4] the best armour class plus bonus.
 * Magic armour (slot 2, a positive bonus) setting [4] sets *magic. */
static void armour_class(const cok_character_layout *L, const uint8_t *item, const uint8_t *type,
                         int8_t ac[5], bool *magic, uint8_t *c)
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
        cok_field_add8(L, c, COK_FIELD_SAVE_BONUS, item[0x33]);
    } else if (base + bonus > ac[4]) {
        ac[4] = (int8_t)(uint8_t)(base + bonus);
        if (bonus > 0 && type[0] == 2) *magic = true;
    }
}

/* 6346:03e6: weight carried past the strength allowance limits the
 * movement: past 512 to 9, past 768 to 6, past 1,024 to 3. The difference
 * is taken as a signed word, so a load 32,768 or more past the allowance
 * counts as none. */
static void encumbrance(lookup *l, const cok_character_layout *L, uint8_t *c)
{
    int16_t over = (int16_t)(uint16_t)(cok_field_u16(L, c, COK_FIELD_WEIGHT) -
                                       (uint16_t)strength_allowance(l, L, c));
    if (over < 0) over = 0;
    uint8_t movement = cok_field_u8(L, c, COK_FIELD_MOVEMENT);
    uint8_t limit = over <= 0x200 ? movement : over <= 0x300 ? 9 : over <= 0x400 ? 6 : 3;
    if (limit < movement) cok_field_set8(L, c, COK_FIELD_MOVEMENT, limit);
}

bool cok_character_stats(cok_character *character, const cok_rules *rules,
                         const cok_item_types *types, char *error, size_t error_size)
{
    lookup l = {error, error_size, false, rules->data};
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    /* 6346:0d26 clears the far pointers; the port keeps them 0. */
    memset(character->slots, 0, sizeof character->slots);
    memset(c + cok_field_offset(L, COK_FIELD_ITEM_SLOTS), 0, cok_field_size(L,
                                                                            COK_FIELD_ITEM_SLOTS));
    cok_field_set8(L, c, COK_FIELD_ITEM_COUNT, 0);
    cok_field_set8(L, c, COK_FIELD_HANDS, 0);
    cok_field_set16(L, c, COK_FIELD_WEIGHT, 0);
    /* The original also sums the readied items' weight in DS:483c, which
     * nothing ported reads. */
    for (size_t i = 0; i < character->item_count; ++i) {
        const uint8_t *item = character->items[i];
        cok_field_add8(L, c, COK_FIELD_ITEM_COUNT, 1);
        uint16_t weight = u16(item + 0x37);
        if (item[0x39] > 0) weight = (uint16_t)(item[0x39] * weight);
        cok_field_set16(L, c, COK_FIELD_WEIGHT,
                        (uint16_t)(cok_field_u16(L, c, COK_FIELD_WEIGHT) + weight));
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
        cok_field_add8(L, c, COK_FIELD_HANDS, type[1]);
    }
    /* Coins but silver (0): 1-6 in Champions. */
    size_t coin = cok_field_first(L, COK_FIELD_COINS),
           coins = coin + cok_field_count(L, COK_FIELD_COINS);
    for (size_t i = coin > 1 ? coin : 1; i < coins; ++i)
        cok_field_set16(L, c, COK_FIELD_WEIGHT,
                        (uint16_t)(cok_field_u16(L, c, COK_FIELD_WEIGHT) +
                                   cok_field_u16_at(L, c, COK_FIELD_COINS, i)));
    for (size_t i = 1; i <= 2; ++i) {
        cok_field_set8_at(L, c, COK_FIELD_DICE, i, cok_field_u8_at(L, c, COK_FIELD_BASE_DICE, i));
        cok_field_set8_at(L, c, COK_FIELD_DICE_SIDES, i,
                          cok_field_u8_at(L, c, COK_FIELD_BASE_DICE_SIDES, i));
        cok_field_set8_at(L, c, COK_FIELD_DAMAGE_BONUS, i,
                          cok_field_u8_at(L, c, COK_FIELD_BASE_DAMAGE_BONUS, i));
    }
    int8_t ac[5] = {0};
    bool magic = false;
    cok_field_set8(L, c, COK_FIELD_SAVE_BONUS, 0);
    cok_field_set8(L, c, COK_FIELD_AC, cok_field_u8(L, c, COK_FIELD_BASE_AC));
    cok_field_set8(L, c, COK_FIELD_MOVEMENT, cok_field_u8(L, c, COK_FIELD_BASE_MOVEMENT));
    cok_field_set8(L, c, COK_FIELD_THAC0, cok_field_u8(L, c, COK_FIELD_BASE_THAC0));
    ac[0] = dexterity_defense(L, c);
    if (character->slots[0] == 0) {
        cok_field_add8(L, c, COK_FIELD_THAC0, strength_hit(&l, L, c));
        cok_field_add8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, strength_damage(&l, L, c));
    }
    weapon_stats(&l, rules, character, types);
    for (size_t i = 0; i < character->item_count && !l.failed; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x34] == 0) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) break;
        armour_movement(L, item, type, c);
        armour_class(L, item, type, ac, &magic, c);
    }
    /* Magic armour does not add to a ring's bonus. */
    if (magic) ac[3] = 0;
    encumbrance(&l, L, c);
    if (ac[4] < cok_field_u8(L, c, COK_FIELD_AC)) ac[4] = cok_field_s8(L, c, COK_FIELD_AC);
    cok_field_set8(L, c, COK_FIELD_AC, 0);
    for (size_t i = 0; i < 5; ++i) cok_field_add8(L, c, COK_FIELD_AC, ac[i]);
    /* From behind: no dexterity or shield, and 2 worse. */
    cok_field_set8(L, c, COK_FIELD_AC_BEHIND, (uint8_t)(ac[4] + ac[2] + ac[3] - 2));
    if ((cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 2) > 0 ||
         (cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 2) > 0 &&
          cok_character_former_class(c, rules)) ||
         cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > 0 ||
         cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 4) > 0) &&
        cok_field_s8(L, c, COK_FIELD_RACE) < 7) {
        cok_field_set8(L, c, COK_FIELD_FIGHTER_LEVEL, cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL,
                                                                      2));
        if (cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > cok_field_u8(L, c,
                                                                           COK_FIELD_FIGHTER_LEVEL))
            cok_field_set8(L, c, COK_FIELD_FIGHTER_LEVEL,
                           cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 7));
        if (cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 4) > cok_field_u8(L, c,
                                                                           COK_FIELD_FIGHTER_LEVEL))
            cok_field_set8(L, c, COK_FIELD_FIGHTER_LEVEL,
                           cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 4));
    } else {
        cok_field_set8(L, c, COK_FIELD_FIGHTER_LEVEL, 1);
    }
    return !l.failed;
}

bool cok_character_former_class(const uint8_t *c, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    int8_t level = 0;
    if (cok_field_u8(L, c, COK_FIELD_RACE) == 6) {
        size_t i = 0;
        while (i < 7 && cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, i) == 0) ++i;
        level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
    }
    return cok_field_s8(L, c, COK_FIELD_CHANGE_LEVEL) < level;
}

/* Mark the cleric spells of each spell level of the lists (1-4) with a
 * spell a day as known (from 66c2:000f): the spells of DS:4230 + level *
 * 10, to the first 0. The original counts in DS:742f, which other routines set before use. */
static void know_cleric_spells(lookup *l, const cok_character_layout *L, uint8_t *c)
{
    int levels = (int)cok_data_count(l->D, COK_DATA_SPELL_LISTS, 1),
        spells = (int)cok_data_count(l->D, COK_DATA_SPELL_LISTS, 2);
    for (int level = 1; level <= levels; ++level) {
        if (cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, level) == 0) continue;
        for (int i = 1; i <= spells; ++i) {
            uint8_t spell = ds_byte(l, cok_data_offset3(l->D, COK_DATA_SPELL_LISTS, 1, level, i),
                                    "a cleric spell");
            if (spell == 0) break;
            cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, spell, 1);
        }
    }
}

/* Add the spells a day of table (DS:3c8e + table * 99) for each level from
 * first to last to the spells a day of spell class row, columns from..to
 * of the table to spell levels from - shift..to - shift (66c2:000f). */
static void add_spells(lookup *l, const cok_character_layout *L, uint8_t *c, int table, int first,
                       int last,
                       int row, int shift, int from, int to)
{
    for (int level = first; level <= last; ++level)
        for (int k = from; k <= to; ++k)
            cok_field_add8_at2(
                L, c, COK_FIELD_SPELLS_A_DAY, row, k - shift,
                ds_byte(l, cok_data_offset3(l->D, COK_DATA_SPELLS_A_DAY, table, level, k),
                        "spells a day"));
}

/* A cleric's bonus spell of level, if wise enough and it has a spell of
 * that level a day. */
static void bonus_spell(const cok_character_layout *L, uint8_t *c, bool wise, int level)
{
    if (wise && cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, level) != 0)
        cok_field_add8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, level, 1);
}

/* 66c2:0722: a cleric's spells a day, with the bonus spells for wisdom
 * (+0x15) 13 and up. */
static void cleric_spells(lookup *l, const cok_character_layout *L, uint8_t *c)
{
    int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 0);
    if (level <= 0) return;
    int table = cok_field_s8(L, c, COK_FIELD_DEITY) > 4;
    memset(c + cok_field_offset_at2(L, COK_FIELD_SPELLS_A_DAY, 0, 2), 0, 4);
    cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1, (uint8_t)(table + 1));
    add_spells(l, L, c, table, 2, level, 0, 0, 1, 5);
    uint8_t wisdom = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_WISDOM);
    bonus_spell(L, c, wisdom > 12, 1);
    bonus_spell(L, c, wisdom > 13, 1);
    bonus_spell(L, c, wisdom > 14, 2);
    bonus_spell(L, c, wisdom > 15, 2);
    bonus_spell(L, c, wisdom > 16, 3);
    bonus_spell(L, c, wisdom > 17, 4);
}

/* 66c2:000f: spells a day and the spells known without learning: a
 * cleric's (DS:3c8e table 0, or 1 with +0x5d above 4), a knight's from
 * level 6 unless of order 1 (table 4 for order 2, else 5) and a ranger's
 * from level 8 (table 6: druid spells 1-3 at +0x121, mage spells 1-2 at
 * +0x12b), with all druid spells (DS:31b3) known; a mage's (table 7 for
 * moon 1, else 8). Each readied item of power 1 then doubles mage spells
 * 1-3. */
static void spells(lookup *l, const cok_character_layout *L, cok_character *character)
{
    uint8_t *c = character->record;
    memset(c + cok_field_offset(L, COK_FIELD_SPELLS_A_DAY), 0,
           cok_field_size(L, COK_FIELD_SPELLS_A_DAY));
    for (int class = 0; class <= 7; ++class) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class);
        if (level <= 0) continue;
        if (class == 0) {
            int table = cok_field_s8(L, c, COK_FIELD_DEITY) > 4;
            cok_field_add8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
            add_spells(l, L, c, table, 2, level, 0, 0, 1, 5);
            cleric_spells(l, L, c);
            know_cleric_spells(l, L, c);
        } else if (class == 7) {
            uint8_t order = cok_field_u8(L, c, COK_FIELD_KNIGHT_ORDER);
            if (level <= 5 || order == 1) continue;
            add_spells(l, L, c, order == 2 ? 4 : 5, 6, level, 0, 0, 1, 5);
            know_cleric_spells(l, L, c);
        } else if (class == 4) {
            if (level <= 7) continue;
            /* Table 6's columns 1-3 are druid spells, 4-5 mage spells 1-2. */
            add_spells(l, L, c, 6, 8, level, 1, 0, 1, 3);
            add_spells(l, L, c, 6, 8, level, 3, 3, 4, 5);
            for (int spell = 1; spell <= 100; ++spell)
                if (cok_data_u8_at2(l->D, COK_DATA_SPELLS, spell, 0) == 1)
                    cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, spell, 1);
        } else if (class == 5) {
            cok_field_add8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 3, 1, 1);
            add_spells(l, L, c, cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER) == 1 ? 7 : 8, 2, level, 3,
                       0, 1, 5);
        }
    }
    for (size_t i = 0; i < character->item_count; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x3e] != 0x81 || item[0x34] == 0) continue;
        for (size_t k = 1; k <= 3; ++k)
            cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 3, k,
                               (uint8_t)(cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 3, k) * 2));
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
static void saving_throws(lookup *l, const cok_character_layout *L, cok_character *character)
{
    uint8_t *c = character->record;
    bool ward = false;
    for (size_t i = 0; i < character->item_count && !ward; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x3e] > 0x80 && item[0x34] != 0) ward = (item[0x3e] & 0x7f) == 6;
    }
    for (int type = 0; type <= 4; ++type) {
        uint8_t *save = c + cok_field_offset_at(L, COK_FIELD_SAVING_THROW, type);
        *save = 20;
        for (int class = 0; class <= 7; ++class) {
            int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class);
            if (level <= 0) continue;
            uint8_t throw =
                ds_byte(l, cok_data_offset3(l->D, COK_DATA_SAVING_THROWS, class, level, type),
                        "a saving throw");
            if (*save > throw) *save = throw;
        }
        int8_t former = cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 7);
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > former) {
            uint8_t throw =
                ds_byte(l, cok_data_offset3(l->D, COK_DATA_SAVING_THROWS, 7, former, type),
                        "a saving throw");
            if (*save > throw) *save = throw;
        }
        if (type != 0) continue;
        uint8_t con = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION);
        uint8_t race = cok_field_u8(L, c, COK_FIELD_RACE);
        if (race == 3 || race == 5 || race == 4 || ward) {
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
static void thief_skills(lookup *l, const cok_rules *rules, cok_character *character,
                         uint8_t *extra)
{
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    bool power11 = false, power2 = false;
    for (size_t i = 0; i < character->item_count && !power11 && !power2; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x3e] <= 0x80 || item[0x34] == 0) continue;
        power11 = (item[0x3e] & 0x7f) == 11;
        power2 = (item[0x3e] & 0x7f) == 2;
    }
    int8_t base = (int8_t)(uint8_t)(cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 6) +
                                    cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 6) *
                                        (cok_character_former_class(c, rules) ? 1 : 0));
    if (base < 4 && power2) {
        base = 4;
        power2 = false;
    }
    int race = cok_field_s8(L, c, COK_FIELD_RACE);
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
        int8_t adjust = (int8_t)ds_byte(l, cok_data_offset2(l->D, COK_DATA_THIEF_RACE, race, skill),
                                       "a thief skill");
        uint8_t value = ds_byte(l, cok_data_offset2(l->D, COK_DATA_THIEF_LEVEL, level, skill),
                                "a thief skill");
        uint8_t *skill_value = c + cok_field_offset_at(L, COK_FIELD_THIEF_SKILL, skill);
        if (adjust < 0 && value < -adjust + *extra) {
            *skill_value = 0;
        } else {
            *skill_value = (uint8_t)(value + adjust + *extra);
            if (skill < 6) {
                uint8_t dex = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_DEXTERITY);
                long at = cok_data_offset2(l->D, COK_DATA_THIEF_DEXTERITY, dex, skill);
                *skill_value = (uint8_t)(*skill_value + ds_byte(l, at, "a thief skill"));
            }
        }
        if (power2) *skill_value = (uint8_t)(*skill_value + 10);
    }
}

bool cok_character_levels(cok_character *character, const cok_rules *rules,
                          const cok_item_types *types, char *error, size_t error_size)
{
    lookup l = {error, error_size, false, rules->data};
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    cok_field_set8(L, c, COK_FIELD_BASE_THAC0, 0);
    for (int class = 0; class <= 7; ++class) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class);
        uint8_t thac0 = ds_byte(&l, cok_data_offset2(l.D, COK_DATA_THAC0, class, level), "THAC0");
        if (thac0 > cok_field_u8(L, c, COK_FIELD_BASE_THAC0))
            cok_field_set8(L, c, COK_FIELD_BASE_THAC0, thac0);
        if (cok_field_s8(L, c, COK_FIELD_HIGHEST_LEVEL) < level)
            cok_field_set8(L, c, COK_FIELD_HIGHEST_LEVEL, (uint8_t)level);
        if ((class == 2 && level > 6) || (class == 4 && level > 7) || (class == 7 && level > 6))
            cok_field_set8_at(L, c, COK_FIELD_BASE_ATTACKS, 1, 3);
    }
    uint8_t deity = cok_field_u8(L, c, COK_FIELD_DEITY), race = cok_field_u8(L, c, COK_FIELD_RACE);
    if ((deity == 6 && (race == 3 || race == 4)) || deity == 3)
        cok_field_add8(L, c, COK_FIELD_BASE_THAC0, 1);
    spells(&l, L, character);
    saving_throws(&l, L, character);
    /* 66c2:0b9f's uninitialized local lies where 66c2:08a6 left its class
     * counter, 7. An interrupt between the calls could change it; the
     * characters the original saved show 7. */
    uint8_t extra = 7;
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 6) > 0)
        thief_skills(&l, rules, character, &extra);
    cok_field_set8(L, c, COK_FIELD_USABLE_CLASSES, 0);
    for (int class = 0; class <= 7; ++class) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class),
               former = cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, class);
        if (level > 0 ||
            (former > 0 && former < cok_field_s8(L, c, COK_FIELD_HIGHEST_LEVEL)))
            cok_field_add8(L, c, COK_FIELD_USABLE_CLASSES,
                           cok_data_u8_at(l.D, COK_DATA_CLASS_BITS, class));
    }
    for (size_t i = 0; i < COK_ITEM_SLOTS && !l.failed; ++i) {
        uint8_t *item = slot_item(character, i);
        if (item == NULL) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) break;
        if ((type[13] & cok_field_u8(L, c, COK_FIELD_USABLE_CLASSES)) == 0 && item[0x36] == 0)
            item[0x34] = 0;
    }
    if (!l.failed && cok_character_former_class(c, rules)) {
        for (int class = 0; class <= 7; ++class) {
            int8_t former = cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, class);
            if (((class == 2 || class == 7) && former > 6) || (class == 4 && former > 7))
                cok_field_set8_at(L, c, COK_FIELD_BASE_ATTACKS, 1, 3);
            uint8_t thac0 = ds_byte(&l, cok_data_offset2(l.D, COK_DATA_THAC0, class, former),
                                    "THAC0");
            if (thac0 > cok_field_u8(L, c, COK_FIELD_BASE_THAC0))
                cok_field_set8(L, c, COK_FIELD_BASE_THAC0, thac0);
        }
        if (cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 2) > 6 ||
            cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 4) > 7 ||
            cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 7) > 6)
            cok_field_set8_at(L, c, COK_FIELD_BASE_ATTACKS, 1, 3);
        /* Here the local holds what the first call left, or still 7. */
        if (cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 6) > 0)
            thief_skills(&l, rules, character, &extra);
    }
    return !l.failed;
}

bool cok_character_cleric_spells(cok_character *character, const cok_rules *rules, char *error,
                                 size_t error_size)
{
    lookup l = {error, error_size, false, rules->data};
    cleric_spells(&l, rules->character, character->record);
    return !l.failed;
}

bool cok_character_saving_throws(cok_character *character, const cok_rules *rules, char *error,
                                 size_t error_size)
{
    lookup l = {error, error_size, false, rules->data};
    saving_throws(&l, rules->character, character);
    return !l.failed;
}

bool cok_character_thief_skills(cok_character *character, const cok_rules *rules, uint8_t extra,
                                char *error, size_t error_size)
{
    lookup l = {error, error_size, false, rules->data};
    thief_skills(&l, rules, character, &extra);
    return !l.failed;
}

bool cok_party_add(cok_party *party, const cok_rules *rules, cok_character *character)
{
    const cok_character_layout *L = rules->character;
    if (party->count == COK_PARTY_RECORDS) return false;
    bool used[COK_PARTY_MAX] = {false};
    for (size_t i = 0; i < party->count; ++i) {
        uint8_t slot = cok_field_u8(L, party->members[i]->record, COK_FIELD_ICON);
        if (slot < COK_PARTY_MAX) used[slot] = true; /* the set at 4b6d:1969 is 0-7 */
    }
    uint8_t slot = 0;
    while (slot < COK_PARTY_MAX && used[slot]) ++slot;
    cok_field_set8(L, character->record, COK_FIELD_ICON, slot);
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
static void draw_name(cok_picture *dst, const cok_font *font, const cok_character_layout *L,
                      const uint8_t *record, int x, int y, uint8_t color)
{
    char name[16];
    uint8_t stored = cok_field_u8_at(L, record, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(name, record + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    name[length] = '\0';
    cok_text_string(dst, font, name, x, y, color, 0);
}

/* Clear cells from x to column 38 on row y (1128:07e6). */
static void clear_row(cok_picture *dst, int x, int y)
{
    if (x <= 0x26) cok_picture_fill(dst, x, y * 8, (size_t)(0x26 - x + 1), 8, 0);
}

void cok_party_draw(cok_picture *dst, const cok_font *font, const cok_party *party,
                    const cok_rules *rules, const uint8_t *selected, int x, bool combat)
{
    const cok_character_layout *L = rules->character;
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
        else if (cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0) color = 0x0c;
        else if (cok_field_u8(L, c, COK_FIELD_HOSTILE) == 1 && combat) color = 0x0e;
        draw_name(dst, font, L, c, x, y, color);
        /* Armour class, stored as 60 - AC, right-aligned for two digits
         * or a minus and one. */
        uint8_t ac = cok_field_u8(L, c, COK_FIELD_AC);
        int width = ac <= 0x32 ? 1 : ac <= 0x3c ? 2 : ac <= 0x45 ? 1 : 0;
        snprintf(text, sizeof text, ac > 60 ? "-%d" : "%d", ac > 60 ? ac - 60 : 60 - ac);
        cok_text_string(dst, font, text, 0x20 + width, y, 0x0a, 0);
        uint8_t hp = cok_field_u8(L, c, COK_FIELD_HIT_POINTS);
        width = hp <= 9 ? 2 : hp <= 99 ? 1 : 0;
        snprintf(text, sizeof text, "%u", hp);
        cok_text_string(dst, font, text, 0x24 + width, y,
                        hp < cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) ? 0x0e : 0x0a, 0);
    }
    clear_row(dst, x, y);
}

void cok_character_damage(uint8_t *c, const cok_rules *rules, uint8_t damage)
{
    const cok_character_layout *L = rules->character;
    uint8_t over = 0, left = 0, hp = cok_field_u8(L, c, COK_FIELD_HIT_POINTS);
    if (hp < damage)
        over = (uint8_t)(damage - hp);
    else
        left = (uint8_t)(hp - damage);
    if (over > 9 || (left == 0 && cok_field_u8(L, c, COK_FIELD_STATUS) == 1))
        cok_field_set8(L, c, COK_FIELD_STATUS, 6);
    else if (over > 0)
        cok_field_set8(L, c, COK_FIELD_STATUS, 5);
    else if (left == 0)
        cok_field_set8(L, c, COK_FIELD_STATUS, 4);
    uint8_t status = cok_field_u8(L, c, COK_FIELD_STATUS);
    if (status == 0 || status == 1) {
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, left);
    } else {
        cok_field_set8(L, c, COK_FIELD_ACTIVE, 0);
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 0);
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

bool cok_character_write(const cok_character *character, const cok_rules *rules, const char *dir,
                         const char *base, char *error, size_t error_size)
{
    return cok_character_write_file(character, rules, dir, base, rules->saves.record, error,
                                    error_size);
}

bool cok_character_write_file(const cok_character *character, const cok_rules *rules,
                              const char *dir, const char *base, const char *extension,
                              char *error, size_t error_size)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
    /* The record goes as it is; its far pointers, which the original
     * writes as they are in memory and its loader clears, are 0 here. */
    if (!write_file(path, character->record, rules->character->file_size, error, error_size))
        return false;
    /* 169c:0154 erases the items and effects, which are written again only
     * if there are any, each with its next pointer, 0 for the last. */
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, rules->saves.items);
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
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, rules->saves.effects);
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

bool cok_character_heal(uint8_t *c, const cok_rules *rules, uint8_t amount, bool only_hurt,
                        uint8_t mode)
{
    const cok_character_layout *L = rules->character;
    uint8_t status = cok_field_u8(L, c, COK_FIELD_STATUS);
    if (status != 0 && status != 1 && status != 4 && status != 5) return false; /* 60f4:21ca */
    uint8_t max = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS);
    if (only_hurt && cok_field_u8(L, c, COK_FIELD_HIT_POINTS) >= max) return false;
    cok_field_add8(L, c, COK_FIELD_HIT_POINTS, amount);
    if (cok_field_u8(L, c, COK_FIELD_HIT_POINTS) > max)
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, max);
    if (cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0) {
        if (cok_field_u8(L, c, COK_FIELD_STATUS) == 5) cok_field_set8(L, c, COK_FIELD_STATUS, 4);
        if (cok_field_u8(L, c, COK_FIELD_STATUS) == 4 && mode != 5) {
            cok_field_set8(L, c, COK_FIELD_STATUS, 0);
            cok_field_set8(L, c, COK_FIELD_ACTIVE, 1);
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
