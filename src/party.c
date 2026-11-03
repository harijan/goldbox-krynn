#include "party.h"

#include "queen_levels.h"

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

/* Read the spells' block of a game that keeps one from base's file of
 * its own (dkk 46b2:10d7, with a saved character's record: .WIZ), if
 * there is one: what there is of it, the rest 0 (the original reads it
 * into a block it does not clear, saying "problem openning magic file"
 * when it is short). The record then holds a block. */
static bool read_spells(cok_character *character, const cok_rules *rules, const char *dir,
                        const char *base, char *error, size_t error_size)
{
    const cok_character_layout *L = rules->character;
    if (L->spells_size == 0) return true;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, rules->saves.spells);
    bool missing;
    size_t size = 0;
    uint8_t *data = read_file(path, &size, &missing);
    if (data == NULL) {
        if (missing) return true;
        fail(error, error_size, "%s: cannot read", path);
        return false;
    }
    cok_field_set_spells(L, character->record, true);
    memcpy(character->record + L->spells_offset, data,
           size < L->spells_size ? size : L->spells_size);
    free(data);
    return true;
}

/* Read the items and effects of base (4b6d:11e5, dkk 46b2:10d7) with the
 * rules' extensions, the items but those of a type in dropped (a set of
 * 32 bytes, or NULL). */
static bool read_items_effects(cok_character *character, const cok_rules *rules, const char *dir,
                               const char *base, const uint8_t *dropped, char *error,
                               size_t error_size)
{
    char path[4096];
    uint8_t *items, *effects;
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, rules->saves.items);
    if (!read_records(path, COK_ITEM_SIZE, &items, &character->item_count, error, error_size))
        return false;
    character->items = (uint8_t (*)[COK_ITEM_SIZE])(void *)items;
    if (dropped != NULL) {
        size_t kept = 0;
        for (size_t i = 0; i < character->item_count; ++i) {
            uint8_t type = character->items[i][0x2e];
            if ((dropped[type >> 3] >> (type & 7) & 1) != 0) continue;
            if (kept != i) memcpy(character->items[kept], character->items[i], COK_ITEM_SIZE);
            ++kept;
        }
        character->item_count = kept;
    }
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
    return true;
}

/* 60f4:1543: the strength an effect's value gives, 18 with an exceptional
 * strength of value - 1 up to 101 (so 0 gives 18/255), above that value -
 * 100. */
static void effect_strength(const cok_effect *effect, uint8_t *strength, uint8_t *exceptional)
{
    *exceptional = 0;
    *strength = effect->value & 0x7f;
    if (*strength <= 0x65) {
        *exceptional = (uint8_t)(*strength - 1);
        *strength = 18;
    } else {
        *strength = (uint8_t)(*strength - 100);
    }
}

/* 60f4:15db: keep the better of the strength so far and a candidate; an
 * 18 with a higher exceptional strength counts as better even than 19 up. */
static void better(uint8_t *strength, uint8_t *exceptional, uint8_t candidate,
                   uint8_t candidate_exceptional)
{
    if (candidate > *strength || (candidate == 18 && candidate_exceptional > *exceptional)) {
        *strength = candidate;
        *exceptional = candidate_exceptional;
    }
}

void cok_character_strength(cok_character *ch, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    uint8_t *c = ch->record;
    uint8_t score = cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, COK_SCORE_STRENGTH),
            exceptional = cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE);
    uint8_t candidate = 0, candidate_exceptional = 0, fixed = 0xff;
    for (size_t i = 0; i < ch->item_count; ++i) {
        const uint8_t *item = ch->items[i];
        if (item[0x3e] <= 0x80 || item[0x34] == 0) continue;
        uint8_t power = item[0x3e] & 0x7f, kind = item[0x3d];
        if (power == 3) {
            candidate = 18;
            candidate_exceptional = 100;
        } else if (power == 5) {
            if (kind == 0) {
                candidate = 18;
                candidate_exceptional = 100;
            } else if (kind <= 6) {
                candidate = (uint8_t)(18 + kind);
            }
        } else if (power == 8) {
            uint8_t strength = cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, COK_SCORE_STRENGTH);
            if (strength < 18 && kind == 0) {
                candidate = (uint8_t)(strength + 1);
                candidate_exceptional = 0;
            }
        } else if (power == 0x0d) {
            fixed = 3;
        }
        better(&score, &exceptional, candidate, candidate_exceptional);
    }
    const cok_effect *effect = cok_character_find_named(ch, rules, COK_EFFECT_STRENGTH);
    if (effect != NULL) {
        effect_strength(effect, &candidate, &candidate_exceptional);
        if (score < 19 && exceptional < 100) {
            candidate = (uint8_t)(candidate + score);
            if (candidate > 18) {
                /* Fighters, rangers and knights, now or before, get the
                 * excess as exceptional strength, added to the current
                 * one each time. */
                bool fighter = false;
                for (unsigned k = 0; k < 8; ++k)
                    if ((rules->characters.fighter_classes >> k & 1) != 0 &&
                        (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, k) > 0 ||
                         cok_character_former_level(rules, c, k) > 0))
                        fighter = true;
                if (fighter) {
                    candidate_exceptional =
                        (uint8_t)((candidate - 18) * 10 +
                                  cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL));
                    if (candidate_exceptional > 100) candidate_exceptional = 100;
                }
                candidate = 18;
            }
        }
        better(&score, &exceptional, candidate, candidate_exceptional);
    }
    const uint8_t others[] = {cok_effect_id(rules, COK_EFFECT_GIANT_STRENGTH),
                             cok_effect_id(rules, COK_EFFECT_ENLARGED)};
    for (size_t k = 0; k < sizeof others; ++k) {
        effect = cok_character_find_effect(ch, others[k]);
        if (effect == NULL) continue;
        effect_strength(effect, &candidate, &candidate_exceptional);
        better(&score, &exceptional, candidate, candidate_exceptional);
    }
    if (fixed != 0xff) {
        cok_field_set8_at(L, c, COK_FIELD_SCORE, COK_SCORE_STRENGTH, fixed);
        cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL, 0);
    } else {
        cok_field_set8_at(L, c, COK_FIELD_SCORE, COK_SCORE_STRENGTH, score);
        cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL, exceptional);
    }
}

/* One-file characters (Dark Queen's .QCH). */

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Clear the record's far pointers, which the port does not keep. */
static void clear_pointers(const cok_character_layout *L, uint8_t *c)
{
    for (int f = 0; f < COK_FIELD_COUNT; ++f)
        if (cok_field_present(L, (cok_field)f) && L->field[f].pointer)
            memset(c + cok_field_offset(L, (cok_field)f), 0, cok_field_size(L, (cok_field)f));
}

/* Append item (COK_ITEM_SIZE bytes) to character's items. */
static bool append_item(cok_character *character, const uint8_t *item)
{
    return cok_character_insert_item(character, character->item_count, item);
}

bool cok_character_from_bytes(cok_character *character, const cok_rules *rules,
                              const uint8_t *data, size_t size, unsigned contents, size_t *used,
                              char *error, size_t error_size)
{
    const cok_character_layout *L = rules->character;
    const cok_rules_item_record *I = &rules->item_record;
    memset(character, 0, sizeof *character);
    *used = 0;
    if (size < L->file_size) {
        fail(error, error_size, "a record of %zu bytes, not %u (604b:05fd)", size,
             (unsigned)L->file_size);
        return false;
    }
    memcpy(character->record, data, L->file_size);
    size_t at = L->file_size;
    /* The items while the pointer is not nil, its low word counting them
     * down (604b:05fd's DI): one at least. */
    const uint8_t *c = character->record;
    bool more = u32(c + cok_field_offset(L, COK_FIELD_ITEMS)) != 0;
    /* DEC DI; JNG: stops once DI - 1 is not above 0, the overflow flag
     * counting (0x8000 stops after one), as a wider count does. */
    long left = (int16_t)u16(c + cok_field_offset(L, COK_FIELD_ITEMS));
    while (more) {
        if (size - at < I->file_size) {
            cok_character_free(character);
            fail(error, error_size, "the items end at byte %zu (604b:05fd)", size);
            return false;
        }
        uint8_t item[COK_ITEM_SIZE] = {0};
        memcpy(item + I->file_at, data + at, I->file_size);
        at += I->file_size;
        more = --left > 0;
        if (I->container != 0 && item[I->type] == I->container &&
            contents + item[I->count] > 0) {
            /* 86b8:1f75's count with its own and those kept (none: a kept
             * one has none) above 0: its contents are read past, and it is
             * freed. */
            size_t skip = (size_t)item[I->count] * I->file_size;
            if (size - at < skip) {
                cok_character_free(character);
                fail(error, error_size, "a scroll's contents end at byte %zu (604b:05fd)", size);
                return false;
            }
            at += skip;
            character->contents_dropped = true;
            continue;
        }
        if (!append_item(character, item)) {
            cok_character_free(character);
            fail(error, error_size, "out of memory");
            return false;
        }
    }
    /* The effects while each one's pointer to the next is not nil. */
    more = u32(c + cok_field_offset(L, COK_FIELD_EFFECTS)) != 0;
    while (more) {
        if (size - at < COK_EFFECT_SIZE) {
            cok_character_free(character);
            fail(error, error_size, "the effects end at byte %zu (604b:05fd)", size);
            return false;
        }
        const uint8_t *e = data + at;
        at += COK_EFFECT_SIZE;
        more = u32(e + 5) != 0;
        if (cok_character_add_effect(character, e[0], u16(e + 1), e[3], e[4] != 0) == NULL) {
            cok_character_free(character);
            fail(error, error_size, "out of memory");
            return false;
        }
    }
    clear_pointers(L, character->record);
    *used = at;
    return true;
}

size_t cok_character_to_bytes(const cok_character *character, const cok_rules *rules,
                              uint8_t *out, size_t capacity, char *error, size_t error_size)
{
    const cok_character_layout *L = rules->character;
    const cok_rules_item_record *I = &rules->item_record;
    size_t effects = 0;
    for (const cok_effect *e = character->effects; e != NULL; e = e->next) ++effects;
    for (size_t i = 0; i < character->item_count; ++i)
        if (I->container != 0 && character->items[i][I->type] == I->container &&
            character->items[i][I->count] != 0) {
            fail(error, error_size, "item %zu is a scroll with contents, which the port does not "
                                    "hold (604b:2200 writes them)", i + 1);
            return 0;
        }
    size_t size = L->file_size + character->item_count * I->file_size + effects * COK_EFFECT_SIZE;
    if (size > capacity) return size;
    uint8_t *c = out;
    memcpy(c, character->record, L->file_size);
    clear_pointers(L, c);
    /* 604b:2200 writes the count of items where their pointer is, a
     * long. */
    size_t items = cok_field_offset(L, COK_FIELD_ITEMS);
    put16(c + items, (uint16_t)character->item_count);
    if (effects > 0) c[cok_field_offset(L, COK_FIELD_EFFECTS)] = 1;
    uint8_t *p = out + L->file_size;
    for (size_t i = 0; i < character->item_count; ++i, p += I->file_size)
        memcpy(p, character->items[i] + I->file_at, I->file_size);
    for (const cok_effect *e = character->effects; e != NULL; e = e->next, p += COK_EFFECT_SIZE) {
        memset(p, 0, COK_EFFECT_SIZE);
        p[0] = e->id;
        put16(p + 1, e->duration);
        p[3] = e->value;
        p[4] = e->on_remove;
        if (e->next != NULL) p[5] = 1;
    }
    return size;
}

/* The recompute a loaded character ends with (4b6d:11e5; dkk 46b2:10d7):
 * its stats, then its levels; Death Knights' (casting's load_strength)
 * its strength between them (638c:16d8 with 0). */
static bool recompute(cok_character *character, const cok_rules *rules,
                      const cok_item_types *types, const char *path, char *error,
                      size_t error_size)
{
    char why[200];
    bool stats = cok_character_stats(character, rules, types, why, sizeof why);
    if (stats && rules->casting.load_strength) cok_character_strength(character, rules);
    /* Dark Queen's readers (.QCH 604b:05fd, import 604b:0b82, its saved
     * games' 28f8, 2bcf) run the stats alone; its roster's Add runs the
     * levels after (604b:2021: cok_character_levels, for the camp). */
    bool levels = rules->characters.levels != COK_LEVELS_DARK_QUEEN;
    if (!stats || (levels && !cok_character_levels(character, rules, types, why, sizeof why))) {
        fail(error, error_size, "%s: %s", path, why);
        return false;
    }
    return true;
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
    if (rules->saves.character_files == COK_CHARACTER_ONE_FILE) {
        /* dqk 604b:05fd, with the party's containers' contents counted as
         * none (the port reads a character without its party). */
        size_t used;
        char why[200];
        bool ok = cok_character_from_bytes(character, rules, data, size, 0, &used, why,
                                           sizeof why);
        free(data);
        if (!ok) fail(error, error_size, "%s: %s", path, why);
        return ok && recompute(character, rules, types, path, error, error_size);
    }
    const cok_character_layout *L = rules->character;
    /* The original reads what there is into a zeroed record. */
    memcpy(character->record, data, size < L->file_size ? size : L->file_size);
    free(data);
    /* Pointers left in the file: effects, items, the combat record, next;
     * and the spells' block's, set again if its file is there. */
    static const cok_field pointers[] = {COK_FIELD_EFFECTS, COK_FIELD_ITEMS, COK_FIELD_COMBAT,
                                         COK_FIELD_NEXT};
    for (size_t i = 0; i < sizeof pointers / sizeof *pointers; ++i)
        memset(character->record + cok_field_offset(L, pointers[i]), 0,
               cok_field_size(L, pointers[i]));
    if (L->spells_size != 0) cok_field_set_spells(L, character->record, false);
    if (!read_spells(character, rules, dir, base, error, error_size) ||
        !read_items_effects(character, rules, dir, base, NULL, error, error_size))
        return false;
    /* The original flushes the keyboard (1614:045c), then recomputes. */
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
    return recompute(character, rules, types, path, error, error_size);
}

/* Dark Queen's import of a Death Knights character (dqk 604b:0b82). */

/* An id by one of the import's maps: the case's, or the id kept. */
static uint8_t mapped(const uint8_t (*ids)[2], size_t count, uint8_t id)
{
    for (size_t i = 0; i < count; ++i)
        if (ids[i][0] == id) return ids[i][1];
    return id;
}

/* The other game's file base.extension of a character, NULL with *size 0
 * if it is missing; false with error set if it cannot be read. */
static bool read_sibling(const char *dir, const char *base, const char *extension,
                         uint8_t **data, size_t *size, char *error, size_t error_size)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
    bool missing;
    *size = 0;
    *data = read_file(path, size, &missing);
    if (*data == NULL && !missing) {
        fail(error, error_size, "%s: cannot read", path);
        return false;
    }
    return true;
}

/* The records 604b:16f8 and 181b take from a file of size bytes, each of
 * record bytes: those that start below size - 2 (their loops' test), the
 * last one past the end read from the zeroed rest. */
static size_t import_count(size_t size, size_t record)
{
    if (size == 0) return 0;
    return size <= 2 ? 1 : (size - 2 + record - 1) / record;
}

/* A Death Knights item (63 bytes, .JNK) as Dark Queen's (604b:181b). */
static void import_item(const cok_rules_import *I, const uint8_t *d, uint8_t *node)
{
    memset(node, 0, COK_ITEM_SIZE);
    node[4] = d[0x29];
    memcpy(node + 5, d + 1, 0x23);
    node[5 + (d[0] < 0x22 ? d[0] : 0x22)] = 0; /* a C string of at most 0x22 */
    node[0x28] = d[0x2e];
    for (int k = 0; k < 3; ++k) node[0x29 + k] = mapped(I->word_ids, I->word_id_count, d[0x2f + k]);
    node[0x30] = d[0x32];
    node[0x31] = d[0x33];
    node[0x32] = d[0x34];
    node[0x33] = d[0x35];
    node[0x34] = d[0x36];
    memcpy(node + 0x2c, d + 0x37, 2);
    node[0x35] = d[0x39];
    memcpy(node + 0x2e, d + 0x3a, 2);
    memcpy(node + 0x36, d + 0x3c, 3);
    bool scroll = false;
    for (const uint8_t *t = I->scroll_types; *t != 0xff; ++t)
        if (node[0x28] == *t) scroll = true;
    /* A scroll's three spells (6fd8:43f7); another's special power, an
     * effect where +0x38 is 0x80, else a spell. The top bit stays. */
    if (scroll) {
        for (int k = 0; k < 3; ++k)
            node[0x36 + k] = (uint8_t)(mapped(I->spell_ids, I->spell_id_count,
                                              node[0x36 + k] & 0x7f) |
                                       (node[0x36 + k] & 0x80));
    } else if (node[0x38] == 0x80) {
        node[0x37] = mapped(I->effect_ids, I->effect_id_count, node[0x37]);
    } else {
        node[0x37] = (uint8_t)(mapped(I->spell_ids, I->spell_id_count, node[0x37] & 0x7f) |
                               (node[0x37] & 0x80));
    }
    if (node[0x28] == 0x34) {
        node[0x28] = 0x33;
        node[0x38] = 0;
    }
}

void cok_character_default_body(const cok_character_layout *L, uint8_t *c)
{
    /* dqk 66e5:0001: the body icon (+0xc4) by gender, race, class and, for
     * some, alignment (0, 3 and 6 the lawful); a class past 16 sets none. */
    static const uint8_t kender_male[17] = {0x2f, 0, 0x2b, 0, 0x2a, 0, 0x2e, 0, 0x2b,
                                            0x2b, 0x2a, 0x2f, 0x2e, 0x2b, 0x2b, 0x2b, 0x2e};
    static const uint8_t dwarf_male[17] = {0x25, 0, 0x22, 0x22, 0x22, 0, 0x27, 0, 0x22,
                                           0x22, 0x22, 0x25, 0x27, 0x22, 0x22, 0x22, 0x27};
    /* 0xff: by the alignment, below. */
    static const uint8_t other_male[17] = {0xff, 0, 3, 2, 8, 0xff, 0x11, 1, 3,
                                           3, 8, 0xff, 0x11, 3, 3, 3, 0x11};
    static const uint8_t kender_female[17] = {0x2b, 0, 0x2d, 0, 0x2c, 0, 0x2d, 0, 0x2d,
                                              0x2d, 0x2c, 0x2b, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d};
    static const uint8_t dwarf_female[17] = {0x26, 0, 0x24, 0x24, 0x24, 0, 0x28, 0, 0x24,
                                             0x24, 0x24, 0x26, 0x28, 0x24, 0x24, 0x24, 0x28};
    static const uint8_t other_female[17] = {0xff, 0xc, 0xa, 0xb, 0xe, 0xff, 0x14, 0, 0xa,
                                             0xa, 0xe, 0xff, 0x14, 0xa, 0xa, 0xa, 0x14};
    int race = cok_field_s8(L, c, COK_FIELD_RACE), class_ = cok_field_s8(L, c, COK_FIELD_CLASS);
    bool male = cok_field_s8(L, c, COK_FIELD_GENDER) == 0;
    if (class_ < 0 || class_ > 16) return;
    const uint8_t *table = race == 5 ? (male ? kender_male : kender_female)
                           : race == 3 || race == 4 ? (male ? dwarf_male : dwarf_female)
                                                     : (male ? other_male : other_female);
    uint8_t body = table[class_];
    if (body == 0xff) {
        /* The cleric's and the mage's (and cleric/mage's) by alignment:
         * the lawful ones' (0, 3, 6) then the others'. */
        bool lawful = cok_field_s8(L, c, COK_FIELD_ALIGNMENT) % 3 == 0;
        if (class_ == 5)
            body = male ? (lawful ? 0x1b : 0x1c) : (lawful ? 0x1f : 0x20);
        else
            body = male ? (lawful ? 0x17 : 0x1a) : (lawful ? 0x18 : 0x19);
    }
    cok_field_set8(L, c, COK_FIELD_ICON_BODY, body);
}

static bool dark_queen_import(cok_character *character, const cok_rules *rules, const char *dir,
                              const char *base, const char *extension, char *error,
                              size_t error_size)
{
    const cok_rules_import *I = rules->characters.import;
    const cok_character_layout *L = rules->character, *from = I->from->character;
    const cok_rules_saves *S = &I->from->saves;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
    uint8_t *data;
    size_t size;
    if (!read_sibling(dir, base, extension, &data, &size, error, error_size)) return false;
    /* 604b:0cd7: the whole record or nothing. */
    if (size < from->file_size) {
        free(data);
        fail(error, error_size, "%s: %s", path,
             data == NULL ? "not found" : "shorter than a record (604b:0cd7)");
        return false;
    }
    uint8_t *c = character->record, other[COK_CHARACTER_MAX] = {0};
    memcpy(other, data, from->file_size);
    free(data);
    /* 604b:0d65: a record zeroed, the moves, a 0 after the name's text. */
    for (size_t i = 0; i < I->move_count; ++i) {
        const cok_rules_move *m = &I->moves[i];
        assert(m->to + m->count <= L->file_size && m->from + m->count <= from->file_size);
        memcpy(c + m->to, other + m->from, m->count);
    }
    size_t name = cok_field_offset(L, COK_FIELD_NAME);
    c[name + (other[cok_field_offset(from, COK_FIELD_NAME)] < 15
                  ? other[cok_field_offset(from, COK_FIELD_NAME)]
                  : 15)] = 0;
    /* Class 7 (an int) becomes 1, and the levels of class 7 class 1's: the
     * moves of eight levels each put the eighth where the next field
     * starts, which 0d65 moves to the second and clears (the former
     * levels, the eighth at +0xb3 moved over again). */
    size_t class_ = cok_field_offset(L, COK_FIELD_CLASS);
    if (u16(c + class_) == 7) put16(c + class_, 1);
    cok_field_set8_at(L, c, COK_FIELD_UNDRAINED_LEVEL, 1,
                      cok_field_u8_at(from, other, COK_FIELD_UNDRAINED_LEVEL, 7));
    cok_field_set8_at(L, c, COK_FIELD_CLASS_LEVEL, 1,
                      cok_field_u8_at(from, other, COK_FIELD_CLASS_LEVEL, 7));
    memset(c + cok_field_offset(L, COK_FIELD_FORMER_LEVEL), 0,
           cok_field_size(L, COK_FIELD_FORMER_LEVEL));
    /* 86b8:354b's caps on the hit points (+0x89, +0xbe, +0xc0, +0x198) and
     * the recompute 66e5:0001 are not ported. */
    clear_pointers(L, c);
    /* The effects (604b:16f8): at most 0x8c bytes ("Too many effects"
     * leaves none), each id by its map, its next pointer nil. */
    if (!read_sibling(dir, base, S->effects, &data, &size, error, error_size)) return false;
    if (size <= 0x8c) {
        size_t n = import_count(size, COK_EFFECT_SIZE);
        for (size_t i = 0; i < n; ++i) {
            uint8_t e[COK_EFFECT_SIZE] = {0};
            size_t at = i * COK_EFFECT_SIZE;
            memcpy(e, data + at, size - at < sizeof e ? size - at : sizeof e);
            if (cok_character_add_effect(character, mapped(I->effect_ids, I->effect_id_count, e[0]),
                                         u16(e + 1), e[3], e[4] != 0) == NULL) {
                free(data);
                fail(error, error_size, "%s: out of memory", path);
                return false;
            }
        }
    }
    free(data);
    /* The items (604b:181b): at most 0x3f0 bytes ("Too many items!"
     * leaves none), each converted; those of type 0x36 then taken away
     * (177e:13d2). The original then readies again each readied one of
     * type 0x3a whose +0x34 is 0 (86b8:1965 twice), not ported. */
    if (!read_sibling(dir, base, S->items, &data, &size, error, error_size)) return false;
    if (size <= 0x3f0) {
        size_t n = import_count(size, COK_ITEM_SIZE);
        for (size_t i = 0; i < n; ++i) {
            uint8_t d[COK_ITEM_SIZE] = {0}, node[COK_ITEM_SIZE];
            size_t at = i * COK_ITEM_SIZE;
            memcpy(d, data + at, size - at < sizeof d ? size - at : sizeof d);
            import_item(I, d, node);
            if (!append_item(character, node)) {
                free(data);
                fail(error, error_size, "%s: out of memory", path);
                return false;
            }
        }
        for (size_t i = character->item_count; i-- > 0;)
            if (character->items[i][rules->item_record.type] == 0x36)
                cok_character_remove_item(character, i);
    }
    free(data);
    /* The spells (604b:2021): under 0xdd bytes ("Too much magic!" leaves
     * none); the memorized and the known by the map, the known as bits
     * (an id past 128 reaching past them, as the original's arithmetic
     * does, into the spells a day, which then go over it), the spells a
     * day's rows 0-3 of eight to rows 0, 1, 3 and 2. */
    if (!read_sibling(dir, base, S->spells, &data, &size, error, error_size)) return false;
    if (size > 0 && size < 0xdd) {
        uint8_t w[0xdd] = {0};
        memcpy(w, data, size);
        for (size_t i = 0; i < 0x4a; ++i)
            cok_field_set8_at(L, c, COK_FIELD_MEMORIZED, i,
                              mapped(I->spell_ids, I->spell_id_count, w[i]));
        size_t bits = cok_field_offset(L, COK_FIELD_KNOWN_SPELL_BITS);
        memset(c + bits, 0, cok_field_size(L, COK_FIELD_KNOWN_SPELL_BITS));
        for (unsigned i = 0; i < 0x72; ++i) {
            unsigned id = mapped(I->spell_ids, I->spell_id_count, (uint8_t)(i + 1));
            if (w[0x4a + i] != 0 && id >= 1 && bits + (id - 1) / 8 < L->size)
                c[bits + (id - 1) / 8] |= (uint8_t)(1u << (id - 1) % 8);
        }
        static const int rows[4] = {0, 1, 3, 2};
        for (int r = 0; r < 4; ++r)
            memcpy(c + cok_field_offset_at2(L, COK_FIELD_SPELLS_A_DAY, rows[r], 1),
                   w + 0xbc + 8 * r, 8);
    }
    free(data);
    /* 604b:0d65's end: the hit points kept within 86b8:354b's most, and
     * the body icon (66e5:0001). */
    uint8_t most = cok_queen_most_hit_points(rules, c);
    static const cok_field capped[] = {COK_FIELD_MAX_HIT_POINTS, COK_FIELD_UNDRAINED_MAX_HIT_POINTS,
                                       COK_FIELD_BASE_HIT_POINTS, COK_FIELD_HIT_POINTS};
    for (size_t i = 0; i < sizeof capped / sizeof *capped; ++i)
        if (most < cok_field_u8(L, c, capped[i])) cok_field_set8(L, c, capped[i], most);
    cok_character_default_body(L, c);
    return true;
}

bool cok_character_import(cok_character *character, const cok_rules *rules, const char *dir,
                          const char *base, const char *extension,
                          const cok_item_types *types, char *error, size_t error_size)
{
    const cok_rules_import *I = rules->characters.import;
    assert(I != NULL && "the game imports no other game's characters");
    if (I->routine == COK_IMPORT_DARK_QUEEN) {
        memset(character, 0, sizeof *character);
        char path[4096];
        snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
        if (!dark_queen_import(character, rules, dir, base, extension, error, error_size)) {
            cok_character_free(character);
            return false;
        }
        return recompute(character, rules, types, path, error, error_size);
    }
    const cok_character_layout *L = rules->character, *from = I->from->character;
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
    /* Read into a block of the other game's record zeroed, as much as
     * there is. */
    uint8_t other[COK_CHARACTER_MAX] = {0};
    memcpy(other, data, size < from->file_size ? size : from->file_size);
    free(data);
    uint8_t *c = character->record;
    for (size_t i = 0; i < I->move_count; ++i) {
        const cok_rules_move *m = &I->moves[i];
        assert(m->to + m->count <= L->file_size && m->from + m->count <= from->file_size);
        memcpy(c + m->to, other + m->from, m->count);
    }
    /* The pointers moved with the rest: effects, items, next, combat and
     * the spells' block. */
    static const cok_field pointers[] = {COK_FIELD_ITEMS, COK_FIELD_EFFECTS, COK_FIELD_COMBAT,
                                         COK_FIELD_NEXT};
    for (size_t i = 0; i < sizeof pointers / sizeof *pointers; ++i)
        memset(c + cok_field_offset(L, pointers[i]), 0, cok_field_size(L, pointers[i]));
    cok_field_set_spells(L, c, false);
    uint8_t class_ = cok_field_u8(L, c, COK_FIELD_CLASS);
    bool spells = true;
    for (const uint8_t *k = I->no_spells; *k != 0xff; ++k)
        if (class_ == *k) spells = false;
    if (spells) {
        cok_field_set_spells(L, c, true);
        for (size_t i = 0; i < I->spell_move_count; ++i) {
            const cok_rules_move *m = &I->spell_moves[i];
            assert(m->to + m->count <= L->spells_size && m->from + m->count <= from->file_size);
            memcpy(c + L->spells_offset + m->to, other + m->from, m->count);
        }
    }
    uint8_t race = cok_field_u8(L, c, COK_FIELD_RACE);
    if (race < I->race_count && cok_field_u16(L, c, COK_FIELD_AGE) > I->ages[race][0])
        cok_field_set16(L, c, COK_FIELD_AGE, I->ages[race][1]);
    if (!read_items_effects(character, I->from, dir, base, I->dropped_items, error, error_size))
        return false;
    return recompute(character, rules, types, path, error, error_size);
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
    return cok_item_types_read_at(path, 2, types, error, error_size);
}

bool cok_item_types_read_at(const char *path, size_t at, cok_item_types *types, char *error,
                            size_t error_size)
{
    bool missing;
    size_t size = 0;
    uint8_t *data = read_file(path, &size, &missing);
    if (data == NULL) {
        fail(error, error_size, "%s: %s", path, missing ? "not found" : "cannot read");
        return false;
    }
    /* 3e99:005b seeks to offset 2 and reads up to 0x810 bytes, 129 types,
     * into DS:5886; ITEMS holds 128, so the 129th stays zero (dqk
     * 64b2:0006 reads 0x800 from the start of ITEMS.DAT). */
    memset(types, 0, sizeof *types);
    types->record_type_at = 0x2e;
    if (size > at) {
        size_t length = size - at < sizeof types->type ? size - at : sizeof types->type;
        memcpy(types->type, data + at, length);
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
    const cok_rules *rules;
} lookup;

static lookup new_lookup(const cok_rules *rules, char *error, size_t error_size)
{
    return (lookup){error, error_size, false, rules->data, rules};
}

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
    uint8_t id = item[l->rules->item_record.type];
    if (id < COK_ITEM_TYPES) return types->type[id];
    if (!l->failed)
        fail(l->error, l->error_size, "item type %u is past the %d ITEMS is read into", id,
             COK_ITEM_TYPES);
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
    if (l->rules->characters.strength_flag && cok_field_u8(L, c, COK_FIELD_STRENGTH_BONUSES) == 0)
        return 0;
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
    if (l->rules->characters.strength_flag && cok_field_u8(L, c, COK_FIELD_STRENGTH_BONUSES) == 0)
        return 0;
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
    lookup l = new_lookup(rules, error, error_size);
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
    lookup l = new_lookup(rules, error, error_size);
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
    const cok_rules_item_record *I = &rules->item_record;
    const cok_rules_characters *R = &rules->characters;
    const uint8_t *weapon = slot_item(character, 0);
    if (weapon == NULL) return;
    const uint8_t *type = item_type(l, types, weapon);
    if (type == NULL) return;
    uint8_t id = weapon[I->type];
    cok_field_set8(L, c, COK_FIELD_THAC0, cok_field_u8(L, c, COK_FIELD_BASE_THAC0));
    if ((type[14] & 2) != 0)
        cok_field_add8(L, c, COK_FIELD_THAC0, cok_character_dexterity_missile(c, rules));
    cok_field_set8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, type[11]);
    if ((type[14] & 4) != 0) {
        cok_field_add8(L, c, COK_FIELD_THAC0, strength_hit(l, L, c));
        cok_field_add8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, strength_damage(l, L, c));
    }
    uint8_t bonus = weapon[I->bonus];
    const uint8_t *quarrels = slot_item(character, 12), *arrows = slot_item(character, 11);
    if (type[14] > 0x7f && quarrels != NULL) bonus = (uint8_t)(bonus + quarrels[I->bonus]);
    if ((type[14] & 1) != 0 && arrows != NULL) bonus = (uint8_t)(bonus + arrows[I->bonus]);
    cok_field_add8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1, bonus);
    /* The race bonuses add to hit only. */
    uint8_t race = cok_field_u8(L, c, COK_FIELD_RACE);
    if ((race == 0 || race == 1) &&
        ((id > R->elf_first && id < R->elf_last) || id == R->elf_a || id == R->elf_b))
        ++bonus;
    if (race == 5 && id == R->kender) bonus = (uint8_t)(bonus + 2);
    cok_field_add8(L, c, COK_FIELD_THAC0, bonus);
    cok_field_set8_at(L, c, COK_FIELD_DICE, 1, type[9]);
    cok_field_set8_at(L, c, COK_FIELD_DICE_SIDES, 1, type[10]);
}

/* 6346:0240: readied armour (slot 2) sets the movement (+0x198) by its
 * weight: the base movement up to 150, 9 up to 399, else 6; armour with a
 * bonus other than 0 adds 3 to a movement up to 9. */
static void armour_movement(const cok_rules *rules, const uint8_t *item, const uint8_t *type,
                            uint8_t *c)
{
    const cok_character_layout *L = rules->character;
    const cok_rules_item_record *I = &rules->item_record;
    if (type[0] != 2) return;
    uint16_t weight = u16(item + I->weight);
    if (weight <= 0x96)
        cok_field_set8(L, c, COK_FIELD_MOVEMENT, cok_field_u8(L, c, COK_FIELD_BASE_MOVEMENT));
    else if (weight <= 0x18f)
        cok_field_set8(L, c, COK_FIELD_MOVEMENT, 9);
    else
        cok_field_set8(L, c, COK_FIELD_MOVEMENT, 6);
    if (item[I->bonus] != 0 && cok_field_u8(L, c, COK_FIELD_MOVEMENT) <= 9)
        cok_field_add8(L, c, COK_FIELD_MOVEMENT, 3);
}

/* 6346:02c8, dqk 177e:026e: a readied item's part of the armour class, for
 * a type with bit 7 of byte 6 set. ac holds the parts 6346:0d20 sums: [1]
 * a shield's class plus its bonus; [2] the bonuses of items whose type's
 * armour class is 0, and [3] the best bonus of such a ring, both of which
 * add their saving throw bonus to +0x17c; [4] the best armour class plus
 * bonus. Magic armour (slot 2, a positive bonus) setting [4] sets *magic.
 * Dark Queen's has a part more: [4] is the bonus of the item of type 7
 * with an armour class of 0 (a cloak's), and the best armour is [5]. */
static void armour_class(const cok_rules *rules, const uint8_t *item, const uint8_t *type,
                         int8_t ac[6], bool *magic, uint8_t *c)
{
    const cok_character_layout *L = rules->character;
    const cok_rules_item_record *I = &rules->item_record;
    bool queen = rules->characters.stats == COK_STATS_DARK_QUEEN;
    uint8_t base = type[6];
    if (base <= 0x7f) return;
    base &= 0x7f;
    int8_t bonus = (int8_t)item[I->bonus];
    size_t best = queen ? 5 : 4;
    if (type[0] == 1) {
        ac[1] = (int8_t)(uint8_t)(base + bonus);
    } else if (base == 0) {
        if (type[0] == 9) {
            if (bonus > ac[3]) ac[3] = bonus;
        } else if (queen && type[0] == 7) {
            ac[4] = bonus;
        } else {
            ac[2] = (int8_t)(uint8_t)(ac[2] + bonus);
        }
        cok_field_add8(L, c, COK_FIELD_SAVE_BONUS, item[I->save_bonus]);
    } else if (base + bonus > ac[best]) {
        ac[best] = (int8_t)(uint8_t)(base + bonus);
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

/* dkk 65cd:1241, which Death Knights' stats run where Champions' run
 * 6346:03e6, and its combat runs for the movement a round: the movement
 * again from the base movement, the readied armour and the load, doubled
 * by effect 0x27, halved by 0x2a and doubled by 0x09 (as bytes), left in
 * the record; returned (DS:be6f) for one on the party's side plus bonus
 * (var 0x7f72, DS:8c31 + DS:862's 0x372), doubled, as bytes. 65cd:0e46
 * keeps half of that: the movement & 0x7f. */
static uint8_t death_knights_movement(lookup *l, const cok_character_layout *L,
                                      cok_character *character, const cok_item_types *types,
                                      uint8_t bonus)
{
    const cok_rules_item_record *I = &l->rules->item_record;
    uint8_t *c = character->record;
    cok_field_set8(L, c, COK_FIELD_MOVEMENT, cok_field_u8(L, c, COK_FIELD_BASE_MOVEMENT));
    for (size_t i = 0; i < character->item_count && !l->failed; ++i) {
        const uint8_t *item = character->items[i];
        if (item[I->readied] == 0) continue;
        const uint8_t *type = item_type(l, types, item);
        if (type == NULL) return 0;
        armour_movement(l->rules, item, type, c);
    }
    encumbrance(l, L, c);
    uint8_t m = cok_field_u8(L, c, COK_FIELD_MOVEMENT);
    if (cok_character_find_effect(character, 0x27) != NULL) m = (uint8_t)(m << 1);
    if (cok_character_find_effect(character, 0x2a) != NULL) m = (uint8_t)(m >> 1);
    if (cok_character_find_effect(character, 0x09) != NULL) m = (uint8_t)(m << 1);
    cok_field_set8(L, c, COK_FIELD_MOVEMENT, m);
    if (cok_field_u8(L, c, COK_FIELD_HOSTILE) == 0) m = (uint8_t)(m + bonus);
    return (uint8_t)(m << 1);
}

/* dqk 177e:0a5f: the movement again from the base movement, the readied
 * armour (177e:01ef) and the load (177e:038c), doubled by effect 0x27,
 * halved by 0x2a and doubled by 0x4a (as bytes), left in the record; then
 * for a character on the party's side (+0x66 0) the party's variable 0x1c
 * (a signed byte; 1 where the sum is below 1), doubled, a third where
 * variable 0x3c is set and effect 0x96 is not, 0 with effect 0xb2:
 * returned (DS:b1c6). The port's stats have no ECL variables: they count
 * as 0, as Death Knights' 0x7f72 does (step 4 gives them). 177e:0be2 keeps
 * half of it: the movement & 0x7f. */
static uint8_t dark_queen_movement(lookup *l, const cok_character_layout *L,
                                   cok_character *character, const cok_item_types *types)
{
    const cok_rules_item_record *I = &l->rules->item_record;
    uint8_t *c = character->record;
    cok_field_set8(L, c, COK_FIELD_MOVEMENT, cok_field_u8(L, c, COK_FIELD_BASE_MOVEMENT));
    for (size_t i = 0; i < character->item_count && !l->failed; ++i) {
        const uint8_t *item = character->items[i];
        if (item[I->readied] == 0) continue;
        const uint8_t *type = item_type(l, types, item);
        if (type == NULL) return 0;
        armour_movement(l->rules, item, type, c);
    }
    encumbrance(l, L, c);
    uint8_t m = cok_field_u8(L, c, COK_FIELD_MOVEMENT);
    if (cok_character_find_effect(character, 0x27) != NULL) m = (uint8_t)(m << 1);
    if (cok_character_find_effect(character, 0x2a) != NULL) m = (uint8_t)(m >> 1);
    if (cok_character_find_effect(character, 0x4a) != NULL) m = (uint8_t)(m << 1);
    cok_field_set8(L, c, COK_FIELD_MOVEMENT, m);
    if (cok_field_u8(L, c, COK_FIELD_HOSTILE) == 0 && m == 0) m = 1;
    m = (uint8_t)(m << 1);
    if (cok_character_find_effect(character, 0xb2) != NULL) m = 0;
    return m;
}

bool cok_character_round_movement(cok_character *character, const cok_rules *rules,
                                  const cok_item_types *types, uint8_t bonus, uint8_t *movement,
                                  char *error, size_t error_size)
{
    lookup l = new_lookup(rules, error, error_size);
    *movement = death_knights_movement(&l, rules->character, character, types, bonus);
    return !l.failed;
}

bool cok_character_movement_now(cok_character *character, const cok_rules *rules,
                                const cok_item_types *types, char *error, size_t error_size)
{
    lookup l = new_lookup(rules, error, error_size);
    if (rules->characters.stats != COK_STATS_DARK_QUEEN) return true;
    (void)dark_queen_movement(&l, rules->character, character, types);
    return !l.failed;
}

bool cok_character_stats(cok_character *character, const cok_rules *rules,
                         const cok_item_types *types, char *error, size_t error_size)
{
    if (rules->characters.stats == COK_STATS_NONE) return true;
    lookup l = new_lookup(rules, error, error_size);
    const cok_character_layout *L = rules->character;
    const cok_rules_item_record *I = &rules->item_record;
    const cok_rules_characters *R = &rules->characters;
    bool queen = R->stats == COK_STATS_DARK_QUEEN;
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
        uint16_t weight = u16(item + I->weight);
        if (item[I->count] > 0) weight = (uint16_t)(item[I->count] * weight);
        cok_field_set16(L, c, COK_FIELD_WEIGHT,
                        (uint16_t)(cok_field_u16(L, c, COK_FIELD_WEIGHT) + weight));
        if (item[I->readied] == 0) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) return false;
        /* The last readied item of a slot holds it; two rings fit. */
        if (type[0] <= 8)
            character->slots[type[0]] = i + 1;
        else if (type[0] == 9 && character->slots[9] == 0)
            character->slots[9] = i + 1;
        else if (type[0] == 9 && character->slots[10] == 0)
            character->slots[10] = i + 1;
        if (item[I->type] == R->arrows) character->slots[11] = i + 1;
        if (item[I->type] == R->quarrels ||
            (R->quarrels_also != 0 && item[I->type] == R->quarrels_also))
            character->slots[12] = i + 1;
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
    int8_t ac[6] = {0};
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
        if (item[I->readied] == 0) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) break;
        armour_movement(rules, item, type, c);
        armour_class(rules, item, type, ac, &magic, c);
    }
    /* Magic armour does not add to a ring's bonus. Dark Queen's keeps a
     * cloak's (type 7) only where the item in that slot is 0x3a
     * (177e:0be2). */
    if (magic) {
        ac[3] = 0;
        if (queen && ac[4] > 0) {
            const uint8_t *cloak = slot_item(character, 7);
            if (cloak == NULL || cloak[I->type] != 0x3a) ac[4] = 0;
        }
    }
    /* The port's stats have no ECL variables: var 0x7f72 counts as 0,
     * which no Death Knights script changes and its saved games hold (step
     * 3 gives it). */
    if (R->stats == COK_STATS_DEATH_KNIGHTS)
        cok_field_set8(L, c, COK_FIELD_MOVEMENT,
                       (uint8_t)(death_knights_movement(&l, L, character, types, 0) >> 1));
    else if (queen)
        cok_field_set8(L, c, COK_FIELD_MOVEMENT,
                       (uint8_t)(dark_queen_movement(&l, L, character, types) / 2));
    else
        encumbrance(&l, L, c);
    size_t best = queen ? 5 : 4;
    /* Dark Queen's compares the bytes unsigned (177e:0f5f). */
    if (queen ? cok_field_u8(L, c, COK_FIELD_AC) > (uint8_t)ac[best]
              : ac[best] < cok_field_u8(L, c, COK_FIELD_AC))
        ac[best] = cok_field_s8(L, c, COK_FIELD_AC);
    cok_field_set8(L, c, COK_FIELD_AC, 0);
    for (size_t i = 0; i < best + 1; ++i) cok_field_add8(L, c, COK_FIELD_AC, ac[i]);
    /* From behind: no dexterity or shield, and 2 worse. */
    cok_field_set8(L, c, COK_FIELD_AC_BEHIND,
                   (uint8_t)(ac[best] + ac[2] + ac[3] + (queen ? ac[4] : 0) - 2));
    /* Death Knights' and Dark Queen's have no fighter level (dkk 65cd:0e46
     * and dqk 177e:0be2 end here). */
    if (R->stats != COK_STATS_CHAMPIONS) return !l.failed;
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
    /* A game without former classes (dkk 69a8:050b) has none to use. */
    if (!cok_levels_former_classes(rules)) return false;
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
 * and by one of 19-25 for everyone. Death Knights' (dkk 69a8:08ff) is the
 * same without the knight's former level (former_knight false). */
static void saving_throws(lookup *l, const cok_character_layout *L, cok_character *character,
                          bool former_knight)
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
        int8_t former = former_knight ? cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, 7) : 0;
        if (former_knight && cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > former) {
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
                                    cok_character_former_level(rules, c, 6) *
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

/* 66c2:0433. */
static bool champions_levels(cok_character *character, const cok_rules *rules,
                             const cok_item_types *types, char *error, size_t error_size)
{
    lookup l = new_lookup(rules, error, error_size);
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
    saving_throws(&l, L, character, true);
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

/* Death Knights' levels (dkk 69a8:050b and the routines it calls), where
 * the spells a day lie in the spells' block, 8 levels a spell class. */

/* Add, for each level from first to last, the bytes of DS:base + level *
 * 8 + k, k from..to, to the spells a day of spell class row, levels
 * from - shift..to - shift (69a8:0000's loops, DS:7673 + table * 0xcc). */
static void dkk_add_spells(lookup *l, const cok_character_layout *L, uint8_t *c, long base,
                           int first, int last, int row, int shift, int from, int to)
{
    for (int level = first; level <= last; ++level)
        for (int k = from; k <= to; ++k)
            cok_field_add8_at2(L, c, COK_FIELD_SPELLS_A_DAY, row, k - shift,
                               ds_byte(l, base + level * 8 + k, "spells a day"));
}

/* The spells a day's table (DS:7673 + table * 0xcc). */
static long dkk_spells_table(lookup *l, int table)
{
    return cok_data_offset3(l->D, COK_DATA_SPELLS_A_DAY, table, 0, 0);
}

/* Mark the cleric spells of the lists' levels 1 to last with a spell a
 * day as known: DS:81d9 + level * 10 + k (DS:c755), to the first 0. */
static void dkk_know_cleric_spells(lookup *l, const cok_character_layout *L, uint8_t *c, int last)
{
    for (int level = 1; level <= last; ++level) {
        if (cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, level) == 0) continue;
        for (int i = 1; i <= 10; ++i) {
            uint8_t spell = ds_byte(l, cok_data_offset3(l->D, COK_DATA_SPELL_LISTS, 1, level, i),
                                    "a cleric spell");
            if (spell == 0) break;
            cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, spell, 1);
        }
    }
}

/* 69a8:071e: a cleric's spells a day, levels 1-7, with the bonus spells
 * for wisdom 13 and up, as Champions' 66c2:0722; nothing without a spells'
 * block. */
static void dkk_cleric_spells(lookup *l, const cok_character_layout *L, uint8_t *c)
{
    int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 0);
    if (!cok_field_has_spells(L, c) || level <= 0) return;
    int table = cok_field_s8(L, c, COK_FIELD_DEITY) > 4;
    for (int k = 2; k <= 7; ++k) cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, k, 0);
    cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1, (uint8_t)(table + 1));
    dkk_add_spells(l, L, c, dkk_spells_table(l, table), 2, level, 0, 0, 1, 7);
    uint8_t wisdom = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_WISDOM);
    bonus_spell(L, c, wisdom > 12, 1);
    bonus_spell(L, c, wisdom > 13, 1);
    bonus_spell(L, c, wisdom > 14, 2);
    bonus_spell(L, c, wisdom > 15, 2);
    bonus_spell(L, c, wisdom > 16, 3);
    bonus_spell(L, c, wisdom > 17, 4);
}

/* 69a8:0000: the spells a day and the spells known without learning,
 * in the spells' block (nothing without one): a cleric's (table 0, or 1
 * with a deity above 4), levels 1-7, with all cleric spells of those
 * levels known; a paladin's (class 3) from level 9 (table 10, cleric
 * spells 1-4); a knight's from level 6 unless of order 1 (table 4 for
 * order 2, else 5), levels 1-7; a ranger's from level 8 (table 6: druid
 * spells 1-4, mage spells 1-4), with the druid spells of the levels it has
 * known (the spell table's class 1 and level, DS:6a80); a mage's (table 7
 * for order 1, else 8), levels 1-8. No item doubles them. */
static void dkk_spells(lookup *l, const cok_character_layout *L, uint8_t *c)
{
    if (!cok_field_has_spells(L, c)) return;
    memset(c + cok_field_offset(L, COK_FIELD_SPELLS_A_DAY), 0,
           cok_field_size(L, COK_FIELD_SPELLS_A_DAY));
    for (int class = 0; class <= 7; ++class) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class);
        if (level <= 0) continue;
        if (class == 0) {
            int table = cok_field_s8(L, c, COK_FIELD_DEITY) > 4;
            cok_field_add8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
            dkk_add_spells(l, L, c, dkk_spells_table(l, table), 2, level, 0, 0, 1, 7);
            dkk_cleric_spells(l, L, c);
            dkk_know_cleric_spells(l, L, c, 7);
        } else if (class == 3) {
            if (level <= 8) continue;
            dkk_add_spells(l, L, c, dkk_spells_table(l, 10), 9, level, 0, 0, 1, 4);
            dkk_know_cleric_spells(l, L, c, 4);
        } else if (class == 7) {
            uint8_t order = cok_field_u8(L, c, COK_FIELD_KNIGHT_ORDER);
            if (level <= 5 || order == 1) continue;
            dkk_add_spells(l, L, c, dkk_spells_table(l, order == 2 ? 4 : 5), 6, level, 0, 0, 1,
                           7);
            dkk_know_cleric_spells(l, L, c, 7);
        } else if (class == 4) {
            if (level <= 7) continue;
            /* Table 6's columns 1-4 are druid spells, 5-8 mage spells 1-4. */
            dkk_add_spells(l, L, c, dkk_spells_table(l, 6), 8, level, 1, 0, 1, 4);
            dkk_add_spells(l, L, c, dkk_spells_table(l, 6), 8, level, 3, 4, 5, 8);
            const cok_data_segment *D = l->D;
            for (size_t spell = 1; spell <= 0x72; ++spell) {
                if (cok_data_u8_at2(D, COK_DATA_SPELLS, spell, 0) != 1) continue;
                /* +0xc3 + the spell's level, signed: unbounded. */
                long at = cok_field_offset_unchecked(
                    L, COK_FIELD_SPELLS_A_DAY, 1,
                    (int8_t)cok_data_u8_at2(D, COK_DATA_SPELLS, spell, 1));
                if (at >= 0 && at < L->size && c[at] != 0)
                    cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, spell, 1);
            }
        } else if (class == 5) {
            int table = cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER) == 1 ? 7 : 8;
            cok_field_add8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 3, 1, 1);
            dkk_add_spells(l, L, c, dkk_spells_table(l, table), 2, level, 3, 0, 1, 8);
        }
    }
}

/* 69a8:0b3d: the thief skills for the thief level, from the tables by
 * level (DS:72ab), race (DS:7343) and, for skills 1-5, dexterity
 * (DS:734e), as bytes; no items count. */
static void dkk_thief_skills(lookup *l, const cok_character_layout *L, uint8_t *c)
{
    int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 6);
    int race = cok_field_s8(L, c, COK_FIELD_RACE);
    uint8_t dex = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_DEXTERITY);
    for (int skill = 1; skill <= 8; ++skill) {
        uint8_t value =
            (uint8_t)(ds_byte(l, cok_data_offset2(l->D, COK_DATA_THIEF_LEVEL, level, skill),
                              "a thief skill") +
                      ds_byte(l, cok_data_offset2(l->D, COK_DATA_THIEF_RACE, race, skill),
                              "a thief skill"));
        if (skill < 6)
            value = (uint8_t)(value + ds_byte(l, cok_data_offset2(l->D, COK_DATA_THIEF_DEXTERITY,
                                                                  dex, skill),
                                              "a thief skill"));
        cok_field_set8_at(L, c, COK_FIELD_THIEF_SKILL, skill, value);
    }
}

/* dkk 69a8:050b: as Champions' 66c2:0433 but that fighters, paladins
 * (class 3) and knights above level 6 get 3 base attacks, 4 from level
 * 13; the spells, saving throws and thief skills are its own; the usable
 * classes are those with a level; there are no former classes; and the
 * highest level is found again at the end. */
static bool death_knights_levels(cok_character *character, const cok_rules *rules,
                                 const cok_item_types *types, char *error, size_t error_size)
{
    lookup l = new_lookup(rules, error, error_size);
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    cok_field_set8(L, c, COK_FIELD_BASE_THAC0, 0);
    for (int class = 0; class <= 7; ++class) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class);
        uint8_t thac0 = ds_byte(&l, cok_data_offset2(l.D, COK_DATA_THAC0, class, level), "THAC0");
        if (thac0 > cok_field_u8(L, c, COK_FIELD_BASE_THAC0))
            cok_field_set8(L, c, COK_FIELD_BASE_THAC0, thac0);
        if (class == 2 || class == 7 || class == 3) {
            if (level > 6) cok_field_set8_at(L, c, COK_FIELD_BASE_ATTACKS, 1, level < 13 ? 3 : 4);
        } else if (class == 4 && level > 7) {
            cok_field_set8_at(L, c, COK_FIELD_BASE_ATTACKS, 1, 3);
        }
    }
    uint8_t deity = cok_field_u8(L, c, COK_FIELD_DEITY), race = cok_field_u8(L, c, COK_FIELD_RACE);
    if ((deity == 6 && (race == 3 || race == 4)) || deity == 3)
        cok_field_add8(L, c, COK_FIELD_BASE_THAC0, 1);
    dkk_spells(&l, L, c);
    saving_throws(&l, L, character, false);
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 6) > 0) dkk_thief_skills(&l, L, c);
    cok_field_set8(L, c, COK_FIELD_USABLE_CLASSES, 0);
    for (int class = 0; class <= 7; ++class)
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class) > 0)
            cok_field_add8(L, c, COK_FIELD_USABLE_CLASSES,
                           cok_data_u8_at(l.D, COK_DATA_CLASS_BITS, class));
    for (size_t i = 0; i < COK_ITEM_SLOTS && !l.failed; ++i) {
        uint8_t *item = slot_item(character, i);
        if (item == NULL) continue;
        const uint8_t *type = item_type(&l, types, item);
        if (type == NULL) break;
        if ((type[13] & cok_field_u8(L, c, COK_FIELD_USABLE_CLASSES)) == 0 && item[0x36] == 0)
            item[0x34] = 0;
    }
    cok_field_set8(L, c, COK_FIELD_HIGHEST_LEVEL, 0);
    for (int class = 0; class <= 7; ++class)
        if (cok_field_s8(L, c, COK_FIELD_HIGHEST_LEVEL) <
            cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class))
            cok_field_set8(L, c, COK_FIELD_HIGHEST_LEVEL,
                           cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, class));
    return !l.failed;
}

bool cok_character_levels(cok_character *character, const cok_rules *rules,
                          const cok_item_types *types, char *error, size_t error_size)
{
    if (rules->characters.levels == COK_LEVELS_NONE) return true;
    if (rules->characters.levels == COK_LEVELS_DARK_QUEEN)
        return cok_queen_levels(character, rules, types, error, error_size);
    if (rules->characters.levels == COK_LEVELS_DEATH_KNIGHTS)
        return death_knights_levels(character, rules, types, error, error_size);
    return champions_levels(character, rules, types, error, error_size);
}

bool cok_character_cleric_spells(cok_character *character, const cok_rules *rules, char *error,
                                 size_t error_size)
{
    lookup l = new_lookup(rules, error, error_size);
    if (rules->characters.levels == COK_LEVELS_DEATH_KNIGHTS)
        dkk_cleric_spells(&l, rules->character, character->record);
    else
        cleric_spells(&l, rules->character, character->record);
    return !l.failed;
}

bool cok_character_saving_throws(cok_character *character, const cok_rules *rules, char *error,
                                 size_t error_size)
{
    lookup l = new_lookup(rules, error, error_size);
    saving_throws(&l, rules->character, character,
                  rules->characters.levels == COK_LEVELS_CHAMPIONS);
    return !l.failed;
}

bool cok_character_thief_skills(cok_character *character, const cok_rules *rules, uint8_t extra,
                                char *error, size_t error_size)
{
    lookup l = new_lookup(rules, error, error_size);
    if (rules->characters.levels == COK_LEVELS_DEATH_KNIGHTS)
        dkk_thief_skills(&l, rules->character, character->record);
    else
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
    cok_field_name(L, record, name);
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

/* A saved game's parts in rules' layout: the ECL file, the three blocks
 * of variables, an address's cell variable_bytes wide, then 7 bytes, the
 * wall table, the count and the names. */
static size_t variable_bytes(const cok_rules *rules)
{
    assert(rules != NULL && (rules->ecl.variable_bytes == 1 || rules->ecl.variable_bytes == 2));
    assert(rules->saves.walls >= 1 && rules->saves.walls <= COK_SAVED_WALLS);
    return rules->ecl.variable_bytes;
}

/* Dark Queen's (saves.game_size): the 1 KB of variables, 13 bytes of
 * the place, the modes and the wall set, then the party's size. */
enum { QUEEN_VARS = 0x400, QUEEN_COUNT = 0x40d, QUEEN_CHARACTERS = 0x40f, QUEEN_MOST = 8 };

size_t cok_saved_game_count_offset(const cok_rules *rules)
{
    if (rules->saves.game_size != 0) return QUEEN_COUNT;
    return 1 + 0xa00 * variable_bytes(rules) + 7 + 4u * rules->saves.walls;
}

size_t cok_saved_game_size(const cok_rules *rules)
{
    if (rules->saves.game_size != 0) return rules->saves.game_size;
    return cok_saved_game_count_offset(rules) + 1 + COK_PARTY_MAX * 41;
}

size_t cok_saved_game_var_offset(const cok_rules *rules, uint16_t address)
{
    if (rules->saves.game_size != 0) {
        assert(address >= 1 && address <= QUEEN_VARS);
        return address - 1u;
    }
    size_t width = variable_bytes(rules);
    if (address >= 0x4b00 && address < 0x4f00) return 1 + (address - 0x4b00u) * width;
    if (address >= 0x7c00 && address < 0x8000) return 1 + (0x400 + address - 0x7c00u) * width;
    assert(address >= 0x7a00 && address < 0x7c00);
    return 1 + (0x800 + address - 0x7a00u) * width;
}

/* A block of cells, each width bytes, little-endian. */
static const uint8_t *get_cells(const uint8_t *p, uint16_t *cells, size_t count, size_t width)
{
    for (size_t i = 0; i < count; ++i, p += width) cells[i] = width == 2 ? u16(p) : p[0];
    return p;
}

static uint8_t *put_cells(uint8_t *p, const uint16_t *cells, size_t count, size_t width)
{
    for (size_t i = 0; i < count; ++i, p += width) {
        if (width == 2)
            put16(p, cells[i]);
        else
            p[0] = (uint8_t)cells[i];
    }
    return p;
}

unsigned cok_party_contents(const cok_party *party, const cok_rules *rules)
{
    const cok_rules_item_record *I = &rules->item_record;
    unsigned contents = 0;
    for (size_t m = 0; I->container != 0 && m < party->count; ++m)
        for (size_t i = 0; i < party->members[m]->item_count; ++i)
            if (party->members[m]->items[i][I->type] == I->container)
                contents += party->members[m]->items[i][I->count];
    return contents;
}

void cok_saved_game_free(cok_saved_game *game)
{
    for (size_t i = 0; i < COK_PARTY_RECORDS; ++i) {
        if (game->characters[i] == NULL) continue;
        cok_character_free(game->characters[i]);
        free(game->characters[i]);
        game->characters[i] = NULL;
    }
}

static bool write_file(const char *path, const uint8_t *data, size_t size, char *error,
                       size_t error_size);

/* dqk 604b:28f8: the header's reads (failing the load where one is short,
 * but going on to fill what it reads), then the characters while the
 * count says and each reads whole (604b:05fd). */
static bool queen_read(const char *path, const cok_rules *rules, cok_saved_game *game,
                       const uint8_t *data, size_t size, char *error, size_t error_size)
{
    memset(game, 0, sizeof *game);
    if (size < QUEEN_CHARACTERS) {
        fail(error, error_size, "%s: %zu bytes, not a saved game (604b:28f8)", path, size);
        return false;
    }
    for (size_t i = 0; i < QUEEN_VARS; ++i) game->mem4b00[i] = data[i];
    const uint8_t *p = data + QUEEN_VARS;
    game->map_x = (int8_t)p[0];
    game->map_y = (int8_t)p[1];
    game->direction = p[2];
    game->ahead = p[3];
    game->square = p[4];
    game->last_mode = (uint8_t)u16(p + 5);
    game->mode = (uint8_t)u16(p + 7);
    for (size_t i = 0; i < COK_SAVED_WALLS; ++i) game->wall_ids[i] = game->wall_slots[i] = -1;
    game->wall_ids[0] = (int16_t)u16(p + 9);
    game->extra = u16(p + 11);
    int16_t count = (int16_t)u16(data + QUEEN_COUNT);
    size_t at = QUEEN_CHARACTERS;
    /* The party is a list in the original: its loader takes as many as
     * the count says, keeping those it read before one that fails (the
     * port fails the whole); the port holds COK_PARTY_RECORDS. */
    for (int16_t i = 0; i < count; ++i) {
        if (game->count >= COK_PARTY_RECORDS) {
            cok_saved_game_free(game);
            fail(error, error_size, "%s: a party of %d (604b:28f8)", path, count);
            return false;
        }
        cok_character *c = malloc(sizeof *c);
        if (c == NULL) {
            cok_saved_game_free(game);
            fail(error, error_size, "%s: out of memory", path);
            return false;
        }
        cok_party read = {.count = game->count};
        memcpy(read.members, game->characters, sizeof read.members);
        unsigned contents = cok_party_contents(&read, rules);
        size_t used;
        char why[200];
        if (!cok_character_from_bytes(c, rules, data + at, size - at, contents, &used, why,
                                      sizeof why)) {
            free(c);
            cok_saved_game_free(game);
            fail(error, error_size, "%s: character %d: %s", path, i + 1, why);
            return false;
        }
        if (c->contents_dropped) game->contents_dropped = true;
        at += used;
        game->characters[game->count++] = c;
    }
    return true;
}

static bool queen_write(const char *path, const cok_rules *rules, const cok_saved_game *game,
                        char *error, size_t error_size)
{
    size_t size = QUEEN_CHARACTERS, capacity = rules->saves.game_size;
    for (size_t i = 0; i < game->count && i < QUEEN_MOST; ++i) {
        char why[200];
        size_t n = cok_character_to_bytes(game->characters[i], rules, NULL, 0, why, sizeof why);
        if (n == 0) {
            fail(error, error_size, "%s: character %zu: %s", path, i + 1, why);
            return false;
        }
        size += n;
    }
    if (size > capacity) capacity = size;
    uint8_t *data = calloc(1, capacity);
    if (data == NULL) {
        fail(error, error_size, "%s: out of memory", path);
        return false;
    }
    for (size_t i = 0; i < QUEEN_VARS; ++i) data[i] = (uint8_t)game->mem4b00[i];
    uint8_t *p = data + QUEEN_VARS;
    p[0] = (uint8_t)game->map_x;
    p[1] = (uint8_t)game->map_y;
    p[2] = game->direction;
    p[3] = game->ahead;
    p[4] = game->square;
    put16(p + 5, game->last_mode);
    put16(p + 7, game->mode);
    put16(p + 9, (uint16_t)game->wall_ids[0]);
    put16(p + 11, game->extra);
    /* 604b:31a7 counts the list's members, at most 8, and writes as
     * many. */
    size_t count = game->count < QUEEN_MOST ? game->count : QUEEN_MOST;
    put16(data + QUEEN_COUNT, (uint16_t)count);
    size_t at = QUEEN_CHARACTERS;
    for (size_t i = 0; i < count; ++i) {
        char why[200];
        at += cok_character_to_bytes(game->characters[i], rules, data + at, capacity - at, why,
                                     sizeof why);
    }
    /* Then DS:c6de's bytes up to the size, the item table first. */
    for (size_t i = 0; at + i < capacity && i < game->fill_size; ++i) data[at + i] = game->fill[i];
    bool ok = write_file(path, data, capacity, error, error_size);
    free(data);
    return ok;
}

bool cok_saved_game_read(const char *path, const cok_rules *rules, cok_saved_game *game,
                         char *error, size_t error_size)
{
    bool missing;
    size_t size = 0;
    uint8_t *data = read_file(path, &size, &missing);
    if (data == NULL) {
        fail(error, error_size, "%s: %s", path, missing ? "not found" : "cannot read");
        return false;
    }
    if (rules->saves.game_size != 0) {
        bool ok = queen_read(path, rules, game, data, size, error, error_size);
        free(data);
        return ok;
    }
    size_t need = cok_saved_game_size(rules), width = variable_bytes(rules);
    if (size < need) {
        free(data);
        fail(error, error_size, "%s: %zu bytes, not a saved game", path, size);
        return false;
    }
    memset(game, 0, sizeof *game);
    const uint8_t *p = data;
    game->file = *p++;
    p = get_cells(p, game->mem4b00, 0x400, width);
    p = get_cells(p, game->mem7c00, 0x400, width);
    p = get_cells(p, game->mem7a00, 0x200, width);
    game->map_x = (int8_t)*p++;
    game->map_y = (int8_t)*p++;
    game->direction = *p++;
    game->ahead = *p++;
    game->square = *p++;
    game->last_mode = *p++;
    game->mode = *p++;
    for (size_t i = 0; i < COK_SAVED_WALLS; ++i) game->wall_ids[i] = game->wall_slots[i] = -1;
    for (size_t i = 0; i < rules->saves.walls; ++i, p += 4) {
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

bool cok_saved_game_write(const char *path, const cok_rules *rules, const cok_saved_game *game,
                          char *error, size_t error_size)
{
    if (rules->saves.game_size != 0) return queen_write(path, rules, game, error, error_size);
    static uint8_t data[COK_SAVED_GAME_SIZE];
    size_t size = cok_saved_game_size(rules), width = variable_bytes(rules);
    assert(size <= sizeof data);
    memset(data, 0, sizeof data);
    uint8_t *p = data;
    *p++ = game->file;
    p = put_cells(p, game->mem4b00, 0x400, width);
    p = put_cells(p, game->mem7c00, 0x400, width);
    p = put_cells(p, game->mem7a00, 0x200, width);
    *p++ = (uint8_t)game->map_x;
    *p++ = (uint8_t)game->map_y;
    *p++ = game->direction;
    *p++ = game->ahead;
    *p++ = game->square;
    *p++ = game->last_mode;
    *p++ = game->mode;
    for (size_t i = 0; i < rules->saves.walls; ++i, p += 4) {
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
    return write_file(path, data, size, error, error_size);
}

void cok_vault_free(cok_vault *vault)
{
    free(vault->items);
    vault->items = NULL;
    vault->item_count = 0;
}

bool cok_vault_read(const char *path, cok_vault *vault, bool *missing, char *error,
                    size_t error_size)
{
    memset(vault, 0, sizeof *vault);
    size_t size = 0;
    uint8_t *data = read_file(path, &size, missing);
    if (data == NULL) {
        fail(error, error_size, "%s: %s", path, *missing ? "not found" : "cannot read");
        return false;
    }
    /* BlockRead of 12 into DS:be58, what there is of them; then 63 at a
     * time while a whole item comes. */
    memcpy(vault->coins, data, size < 12 ? size : 12);
    size_t count = size > 12 ? (size - 12) / COK_ITEM_SIZE : 0;
    if (count > 0) {
        vault->items = malloc(count * COK_ITEM_SIZE);
        if (vault->items == NULL) {
            free(data);
            fail(error, error_size, "%s: out of memory", path);
            return false;
        }
        memcpy(vault->items, data + 12, count * COK_ITEM_SIZE);
        for (size_t i = 0; i < count; ++i) memset(vault->items + i * COK_ITEM_SIZE + 0x2a, 0, 4);
        vault->item_count = count;
    }
    free(data);
    return true;
}

bool cok_vault_write(const char *path, const cok_vault *vault, char *error, size_t error_size)
{
    size_t size = 12 + vault->item_count * COK_ITEM_SIZE;
    uint8_t *data = malloc(size);
    if (data == NULL) {
        fail(error, error_size, "%s: out of memory", path);
        return false;
    }
    memcpy(data, vault->coins, 12);
    if (vault->item_count > 0) memcpy(data + 12, vault->items, vault->item_count * COK_ITEM_SIZE);
    for (size_t i = 0; i < vault->item_count; ++i)
        memset(data + 12 + i * COK_ITEM_SIZE + 0x2a, 0, 4);
    bool ok = write_file(path, data, size, error, error_size);
    free(data);
    return ok;
}

bool cok_vault_read_game(const char *path, const cok_rules *rules, cok_vault *vault,
                         bool *missing, char *error, size_t error_size)
{
    if (rules->saves.vault_items == 0)
        return cok_vault_read(path, vault, missing, error, error_size);
    const cok_rules_item_record *I = &rules->item_record;
    memset(vault, 0, sizeof *vault);
    size_t size = 0;
    uint8_t *data = read_file(path, &size, missing);
    if (data == NULL) {
        fail(error, error_size, "%s: %s", path, *missing ? "not found" : "cannot read");
        return false;
    }
    if (size < 16) {
        free(data);
        fail(error, error_size, "%s: %zu bytes, not a vault (63ee:0239)", path, size);
        return false;
    }
    memcpy(vault->coins, data, 12);
    size_t count = u16(data + 14), at = 16;
    vault->items = count > 0 ? calloc(count, COK_ITEM_SIZE) : NULL;
    if (count > 0 && vault->items == NULL) {
        free(data);
        fail(error, error_size, "%s: out of memory", path);
        return false;
    }
    for (size_t i = 0; i < count; ++i, at += I->file_size) {
        if (size - at < I->file_size) {
            free(data);
            cok_vault_free(vault);
            fail(error, error_size, "%s: item %zu is short (63ee:0239)", path, i + 1);
            return false;
        }
        uint8_t *item = vault->items + i * COK_ITEM_SIZE;
        memcpy(item + I->file_at, data + at, I->file_size);
        vault->item_count = i + 1;
        if (I->container != 0 && item[I->type] == I->container && item[I->count] != 0) {
            free(data);
            cok_vault_free(vault);
            fail(error, error_size, "%s: item %zu is a scroll with contents, which the port does "
                                    "not hold (63ee:0239 reads them)", path, i + 1);
            return false;
        }
    }
    free(data);
    return true;
}

bool cok_vault_write_game(const char *path, const cok_rules *rules, const cok_vault *vault,
                          const uint8_t *fill, size_t fill_size, char *error, size_t error_size)
{
    if (rules->saves.vault_items == 0) return cok_vault_write(path, vault, error, error_size);
    const cok_rules_item_record *I = &rules->item_record;
    size_t most = rules->saves.vault_items;
    size_t items = vault->item_count > most ? vault->item_count : most;
    size_t size = 16 + items * I->file_size;
    uint8_t *data = calloc(1, size);
    if (data == NULL) {
        fail(error, error_size, "%s: out of memory", path);
        return false;
    }
    memcpy(data, vault->coins, 12);
    put16(data + 12, 0xffff);
    put16(data + 14, (uint16_t)vault->item_count);
    size_t at = 16;
    for (size_t i = 0; i < vault->item_count; ++i, at += I->file_size) {
        const uint8_t *item = vault->items + i * COK_ITEM_SIZE;
        if (I->container != 0 && item[I->type] == I->container && item[I->count] != 0) {
            free(data);
            fail(error, error_size, "%s: item %zu is a scroll with contents, which the port "
                                    "does not hold (63ee:00df writes them)", path, i + 1);
            return false;
        }
        memcpy(data + at, item + I->file_at, I->file_size);
    }
    /* The items short of 50 from DS:c6de's buffer. */
    for (size_t i = 0; at + i < size && i < fill_size; ++i) data[at + i] = fill[i];
    bool ok = write_file(path, data, size, error, error_size);
    free(data);
    return ok;
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
    const cok_character_layout *L = rules->character;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.%s", dir, base, extension);
    if (rules->saves.character_files == COK_CHARACTER_ONE_FILE) {
        char why[200];
        size_t size = cok_character_to_bytes(character, rules, NULL, 0, why, sizeof why);
        uint8_t *bytes = size > 0 ? malloc(size) : NULL;
        if (size == 0 || bytes == NULL) {
            fail(error, error_size, "%s: %s", path, size == 0 ? why : "out of memory");
            return false;
        }
        cok_character_to_bytes(character, rules, bytes, size, why, sizeof why);
        bool ok = write_file(path, bytes, size, error, error_size);
        free(bytes);
        return ok;
    }
    /* The record goes as it is; its far pointers, which the original
     * writes as they are in memory and its loader clears, are 0 here (the
     * spells' block's, which the port marks, too). */
    uint8_t record[COK_CHARACTER_MAX];
    memcpy(record, character->record, L->file_size);
    if (L->spells_size != 0)
        memset(record + cok_field_offset(L, COK_FIELD_SPELLS), 0,
               cok_field_size(L, COK_FIELD_SPELLS));
    if (!write_file(path, record, L->file_size, error, error_size)) return false;
    /* dkk 46b2:0abc erases the spells' file and writes the block again if
     * the record has one. */
    if (L->spells_size != 0) {
        snprintf(path, sizeof path, "%s/%s.%s", dir, base, rules->saves.spells);
        remove(path);
        if (cok_field_has_spells(L, character->record) &&
            !write_file(path, character->record + L->spells_offset, L->spells_size, error,
                        error_size))
            return false;
    }
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
