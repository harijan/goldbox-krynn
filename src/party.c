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

void cok_party_file_name(const char *name, char out[9])
{
    static const char removed[] = " .*,?/\\:;|"; /* DS:0f3c */
    size_t length = 0;
    for (; *name != '\0' && length < 8; ++name) {
        if (strchr(removed, *name) != NULL) continue;
        char c = *name;
        out[length++] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    out[length] = '\0';
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
                        char *error, size_t error_size)
{
    memset(character, 0, sizeof *character);
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.SAV", dir, base);
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
    if (!read_records(path, COK_EFFECT_SIZE, &effects, &character->effect_count, error,
                      error_size))
        return false;
    character->effects = (uint8_t (*)[COK_EFFECT_SIZE])(void *)effects;
    return true;
}

void cok_character_free(cok_character *character)
{
    free(character->items);
    free(character->effects);
    character->items = NULL;
    character->effects = NULL;
    character->item_count = character->effect_count = 0;
}

bool cok_party_add(cok_party *party, cok_character *character)
{
    if (party->count == COK_PARTY_MAX) return false;
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
        if (i == 0) return cok_party_record(party, party->count - 1);
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

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
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
