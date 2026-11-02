#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* The Dark Queen of Krynn's levels (src/queen_levels.c) and the tables of
 * its data segment they read (src/data_dark_queen.c), against the original's
 * bytes in build/DQK_FULL.EXE (make dqk-merged) and the party of its shipped
 * saved game in DQKAssets/ (the checks needing either are skipped, with a
 * line saying so, where it is not there). */
#include "data_dark_queen.h"
#include "party.h"
#include "queen_levels.h"
#include "rules.h"
#include "test_temp.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;

static size_t read_whole(const char *path, uint8_t *out, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    size_t n = fread(out, 1, size, f);
    fclose(f);
    return n;
}

/* The tables byte for byte against the executable's data segment (DS
 * 0x422e: image paragraph 0x322e). */
static void test_tables(void)
{
    static uint8_t exe[1 << 20];
    size_t size = read_whole("build/DQK_FULL.EXE", exe, sizeof exe);
    if (size == 0) {
        puts("dark queen: build/DQK_FULL.EXE not built (make dqk-merged); its tables not checked");
        return;
    }
    CHECK(exe[0] == 'M' && exe[1] == 'Z');
    size_t header = (size_t)(exe[8] | exe[9] << 8) * 16, ds = header + (size_t)0x322e * 16;
    const cok_data_segment *D = R->data;
    CHECK(D == &cok_dark_queen_data && D->table_count > 0);
    for (size_t i = 0; i < D->table_count; ++i) {
        const cok_ds_table *t = &D->tables[i];
        CHECK(t->stride == 1 && ds + t->offset + t->size <= size);
        CHECK(memcmp(exe + ds + t->offset, t->bytes, t->size) == 0);
    }
}

/* The experience a level needs (8bcc:0e9c): the table's by level up to
 * 17, past it so much a level more, nothing past 0x28; a row for each
 * class by the deity, the order, the robes. */
static void test_experience(void)
{
    uint8_t c[COK_CHARACTER_MAX] = {0};
    char error[200];
    int32_t need;
    /* A fighter (row 2): 2,001 for the second level, 1,000,001 for the 12th. */
    CHECK(cok_queen_experience(R, c, 2, 2, &need, error, sizeof error) && need == 2001);
    CHECK(cok_queen_experience(R, c, 12, 2, &need, error, sizeof error) && need == 1000001);
    CHECK(cok_queen_experience(R, c, 17, 2, &need, error, sizeof error) && need == 2250001);
    CHECK(cok_queen_experience(R, c, 20, 2, &need, error, sizeof error) &&
          need == 2250001 + 3 * 250000);
    CHECK(cok_queen_experience(R, c, 0x29, 2, &need, error, sizeof error) && need == 0x7fffffff);
    /* A cleric of Paladine (deity 1) row 0, of Sirrion (5) row 1. */
    c[0x19b] = 1;
    CHECK(cok_queen_experience_row(R, c, 0) == 0);
    c[0x19b] = 5;
    CHECK(cok_queen_experience_row(R, c, 0) == 1);
    /* A knight's by the order; none has no row. */
    CHECK(cok_queen_experience_row(R, c, 1) == -1);
    CHECK(!cok_queen_experience(R, c, 3, 1, &need, error, sizeof error) && error[0] != '\0');
    for (int order = 1; order <= 3; ++order) {
        c[0x19a] = (uint8_t)order;
        CHECK(cok_queen_experience_row(R, c, 1) == order + 2);
    }
    CHECK(cok_queen_experience_row(R, c, 3) == 10 && cok_queen_experience_row(R, c, 4) == 6 &&
          cok_queen_experience_row(R, c, 6) == 9 && cok_queen_experience_row(R, c, 7) == -1);
    c[0x19d] = 1;
    CHECK(cok_queen_experience_row(R, c, 5) == 7);
    c[0x19d] = 2;
    CHECK(cok_queen_experience_row(R, c, 5) == 8);
}

/* The six characters of the shipped saved game, which the original had
 * recomputed (stats and levels) as it made them, recomputed again
 * (8bcc:07f1, 177e:0be2): every byte of each record the same but its
 * pointers. The hundred more cases of the original's own recompute
 * (the party of dqk_add's, a character trained) are the checks in
 * tools/dosbox/checks/. */
static void test_party_again(void)
{
    static uint8_t data[8192];
    static cok_item_types types;
    char error[300];
    if (read_whole("DQKAssets/SAVE/SAVGAMA.QSV", data, sizeof data) == 0 ||
        !cok_item_types_read_at("DQKAssets/DISK1/ITEMS.DAT", 0, &types, error, sizeof error)) {
        puts("dark queen: no DQKAssets/SAVE/SAVGAMA.QSV or ITEMS.DAT; its party's levels not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    size_t at = 0x40f, count = (size_t)(data[0x40d] | data[0x40e] << 8);
    CHECK(count == 6);
    for (size_t i = 0; i < count; ++i) {
        cok_character c;
        size_t used;
        CHECK(cok_character_from_bytes(&c, R, data + at, sizeof data - at, 0, &used, error,
                                       sizeof error));
        uint8_t before[COK_CHARACTER_MAX];
        memcpy(before, c.record, L->size);
        CHECK(cok_character_stats(&c, R, &types, error, sizeof error));
        CHECK(cok_character_levels(&c, R, &types, error, sizeof error));
        CHECK(cok_character_stats(&c, R, &types, error, sizeof error));
        for (size_t k = 0; k < L->size; ++k)
            if (!cok_field_pointer_at(L, (long)k) && before[k] != c.record[k])
                fprintf(stderr, "character %zu +0x%03zx: %02x, recomputed %02x\n", i, k,
                        before[k], c.record[k]);
        for (size_t k = 0; k < L->size; ++k)
            CHECK(cok_field_pointer_at(L, (long)k) || before[k] == c.record[k]);
        at += used;
        cok_character_free(&c);
    }
}

/* A copy of path as dir/name with the byte at offset made value (0 of
 * offset: unchanged). */
static void copy_patched(const char *path, const char *dir, const char *name, size_t offset,
                         uint8_t value)
{
    static uint8_t bytes[2048];
    size_t n = read_whole(path, bytes, sizeof bytes);
    CHECK(n > 0);
    if (offset != 0) bytes[offset] = value;
    char to[1300];
    snprintf(to, sizeof to, "%s/%s", dir, name);
    FILE *f = fopen(to, "wb");
    CHECK(f != NULL && fwrite(bytes, 1, n, f) == n);
    fclose(f);
}

static uint32_t hash_character(uint32_t hash, const cok_character *c)
{
    const cok_character_layout *L = R->character;
    /* But its pointers and its party slot (+0xc5: 604b:3491 sets it as it joins). */
    for (size_t k = 0; k < L->size; ++k)
        hash = (hash ^ (cok_field_pointer_at(L, (long)k) || k == 0xc5 ? 0 : c->record[k])) *
               0x01000193u;
    for (size_t n = 0; n < c->item_count; ++n)
        for (size_t k = 0; k < 17; ++k) hash = (hash ^ c->items[n][0x28 + k]) * 0x01000193u;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next) {
        const uint8_t b[5] = {e->id, (uint8_t)e->duration, (uint8_t)(e->duration >> 8), e->value,
                              e->on_remove};
        for (size_t k = 0; k < 5; ++k) hash = (hash ^ b[k]) * 0x01000193u;
    }
    return hash;
}

/* A character of a roster, added: its file in from (DQKAssets/SAVE's .QCH,
 * or DKKAssets/SAVE's CHRDATA*.GAM imported), a byte of it patched as the
 * check's files were (offset 0: none). */
typedef struct {
    const char *base, *from;
    bool import;
    size_t offset;
    uint8_t value;
} added;

/* Read (or import) one, with the siblings an import reads, patched in dir
 * where it is; its stats ran as it was read, then its levels (604b:2021). */
static void add(const added *w, const char *dir, const cok_item_types *types, cok_character *c)
{
    char error[300], from[700], to[700];
    const char *extension = w->import ? "GAM" : "QCH", *where = w->from;
    if (w->offset != 0) {
        static const char *const files[] = {"GAM", "QCH", "WIZ", "JNK", "EFF"};
        for (size_t i = 0; i < sizeof files / sizeof *files; ++i) {
            snprintf(from, sizeof from, "%s/%s.%s", w->from, w->base, files[i]);
            if (read_whole(from, (uint8_t[1]){0}, 1) == 0) continue;
            snprintf(to, sizeof to, "%s.%s", w->base, files[i]);
            bool main = strcmp(files[i], extension) == 0;
            copy_patched(from, dir, to, main ? w->offset : 0, main ? w->value : 0);
        }
        where = dir;
    }
    bool ok = w->import ? cok_character_import(c, R, where, w->base, extension, types, error,
                                               sizeof error)
                        : cok_character_read_file(c, R, where, w->base, extension, types, error,
                                                  sizeof error);
    if (!ok) fprintf(stderr, "%s: %s\n", w->base, error);
    CHECK(ok);
    CHECK(cok_character_levels(c, R, types, error, sizeof error));
}

/* Characters added from the camp (604b:2021: read, stats, levels) as the
 * original holds them (DOSBox-X, 2026-10-29, tools/dosbox/checks/dqk_add.py:
 * the parties it saved), hashed as test_party.c's: Roland of Dust and
 * Walker Keys from their .QCH, then Death Knights' Celeste, Flavius,
 * Lillith and Neacail imported; and a second party with a ranger, a
 * knight of the Sword, a paladin of the twelfth level. */
static void test_added(void)
{
    static cok_item_types types;
    char error[300], dir[512];
    if (read_whole("DQKAssets/SAVE/ROLANDOF.QCH", (uint8_t[1]){0}, 1) == 0 ||
        read_whole("DKKAssets/SAVE/CHRDATA6.GAM", (uint8_t[1]){0}, 1) == 0 ||
        !cok_item_types_read_at("DQKAssets/DISK1/ITEMS.DAT", 0, &types, error, sizeof error)) {
        puts("dark queen: no DQKAssets/ and DKKAssets/ saved characters; added ones not checked");
        return;
    }
    CHECK(test_temp_dir(dir, sizeof dir, "cok_dqk_added") != NULL);
    static const char *const dqk = "DQKAssets/SAVE", *const dkk = "DKKAssets/SAVE";
    const added parties[2][6] = {
        {{"ROLANDOF", dqk, false, 0, 0}, {"WALKERKE", dqk, false, 0, 0},
         {"CHRDATA6", dkk, true, 0, 0}, {"CHRDATA4", dkk, true, 0, 0},
         {"CHRDATA2", dkk, true, 0, 0}, {"CHRDATA5", dkk, true, 0, 0}},
        {{"'CINDER'", dqk, false, 0, 0}, {"BALTHIRN", dqk, false, 0, 0},
         {"ROARK'CR", dqk, false, 0, 0}, {"ROLANDOF", dqk, false, 0x19a, 2},
         {"CHRDATA6", dkk, true, 0, 0}, {"CHRDATA2", dkk, true, 0x48, 12}},
    };
    static const uint32_t expected[2] = {0x14d4d611u, 0x3f849c41u};
    for (int run = 0; run < 2; ++run) {
        uint32_t hash = 0x811c9dc5u;
        for (size_t i = 0; i < 6; ++i) {
            cok_character c;
            add(&parties[run][i], dir, &types, &c);
            hash = hash_character(hash, &c);
            cok_character_free(&c);
        }
        if (hash != expected[run]) fprintf(stderr, "Dark Queen's added party %d: 0x%08x\n", run, hash);
        CHECK(hash == expected[run]);
    }
}

/* Hit points (86b8:380e, 3a5b) of a fighter (class 2): the better of two
 * rolls of the class's dice (a first level's count, else one) and the
 * constitution's bonus a roll, a knight-like class's more at 17 to 25; at
 * the top level a flat 3 (a cleric's 1) over the sum. */
static void test_hit_points(void)
{
    static const uint8_t extra[] = {1, 2, 3, 3, 4, 4, 4, 5, 5};
    const cok_data_segment *D = R->data;
    const unsigned fighter = 2;
    uint8_t top = cok_data_held(D, 0x640a + fighter);
    /* The knight rolls two dice a first level, the fighter one. */
    for (unsigned k = 1; k <= fighter; ++k) {
        uint8_t mask = cok_data_held(D, 0x63fa + k), sides = cok_data_held(D, 0x6418 + k);
        for (unsigned level = 1; level <= 2; ++level)
            for (unsigned con = 3; con <= 25; ++con) {
                cok_character ch;
                memset(&ch, 0, sizeof ch);
                ch.record[0xa5 + k] = (uint8_t)level;
                ch.record[0x81] = (uint8_t)con;
                uint8_t count = level > 1 ? 1 : cok_data_held(D, 0x6411 + k);
                uint32_t seed = 12345 + con, mine = seed;
                uint8_t first = cok_dice(&mine, count, sides);
                uint8_t second = cok_dice(&mine, count, sides);
                uint8_t hit = first > second ? first : second;
                int bonus = cok_data_held(D, 0x82d + con);
                if (con >= 17) bonus += extra[con - 17];
                cok_queen_hit_points(&ch, R, &seed, mask, 1, true);
                CHECK(seed == mine);
                CHECK(ch.record[0xc0] == hit);
                CHECK(ch.record[0x89] == (uint8_t)(hit + bonus * count));
                CHECK(ch.record[0x198] == ch.record[0x89]);
            }
    }
    for (unsigned deity = 0; deity < 2; ++deity) {
        cok_character ch;
        memset(&ch, 0, sizeof ch);
        uint32_t seed = 99;
        unsigned class_ = deity ? 0 : fighter;
        ch.record[0xa5 + class_] = deity ? cok_data_held(D, 0x640a) : top;
        ch.record[0x19b] = 1;
        ch.record[0x81] = 17;
        cok_queen_hit_points(&ch, R, &seed, cok_data_held(D, 0x63fa + class_), 1, true);
        CHECK(seed == 99 && ch.record[0xc0] == (deity ? 1 : 3) && ch.record[0x89] == ch.record[0xc0]);
    }
}

/* The combat tables the port reads, under the descriptor's lookups: every
 * element at the offset its lookup gives is the executable's byte there (the
 * elements' bytes as their width), and a few the code's own arithmetic
 * reads hold the values the original's routines would fetch. */
static void test_combat_tables(void)
{
    static uint8_t exe[1 << 20];
    size_t size = read_whole("build/DQK_FULL.EXE", exe, sizeof exe);
    if (size == 0) {
        puts("dark queen: build/DQK_FULL.EXE not built (make dqk-merged); combat tables not checked");
        return;
    }
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + (size_t)0x322e * 16;
    const cok_data_segment *D = R->data;
    static const cok_data tables_checked[] = {
        COK_DATA_AREA_ROWS,           COK_DATA_TURNING,        COK_DATA_MOVES,
        COK_DATA_QUADRANTS,           COK_DATA_QUADRANT_DIRECTIONS,
        COK_DATA_START_COLUMNS,       COK_DATA_START_ROWS,     COK_DATA_FORMATIONS,
        COK_DATA_TERRAIN_FLAGS,       COK_DATA_LINE_DIRECTIONS, COK_DATA_STEP_X,
        COK_DATA_STEP_Y,              COK_DATA_TERRAIN,        COK_DATA_FOOTPRINTS,
        COK_DATA_CLOUD_CELLS,
    };
    for (size_t n = 0; n < sizeof tables_checked / sizeof *tables_checked; ++n) {
        cok_data t = tables_checked[n];
        const cok_data_table *s = &D->table[t];
        CHECK(s->width != 0 && s->indices >= 1 && s->indices <= 3);
        long at[3] = {0, 0, 0};
        unsigned total = 1;
        for (unsigned k = 0; k < s->indices; ++k) total *= s->index[k].count;
        for (unsigned e = 0; e < total; ++e) {
            unsigned rest = e;
            for (int k = s->indices - 1; k >= 0; --k) {
                at[k] = s->index[k].first + (long)(rest % s->index[k].count);
                rest /= s->index[k].count;
            }
            long offset = cok_data_offset_(D, t, s->indices, at[0], at[1], at[2]);
            CHECK(cok_data_holds(D, t, offset) && cok_data_holds(D, t, offset + s->width - 1));
            for (unsigned b = 0; b < s->width; ++b)
                CHECK(exe[ds + (size_t)offset + b] == cok_data_held(D, offset + (long)b));
        }
    }
    /* The original's own reads, by its code's arithmetic: the moves of a
     * quadrant's square 1 (59c7:1553), the turning of kind 1 at level 0 and
     * 1 (5045:3b6d), kind 13 at level 10, and the area of row -3 at radius
     * 1, a word (5045:74aa). */
    CHECK(cok_data_u8_at2(D, COK_DATA_MOVES, 1, 2) == 4);
    CHECK(cok_data_u8_at2(D, COK_DATA_MOVES, 1, 3) == 0);
    CHECK(cok_data_u8_at2(D, COK_DATA_TURNING, 1, 0) == 0x63);
    CHECK(cok_data_u8_at2(D, COK_DATA_TURNING, 1, 1) == 0x0a);
    CHECK(cok_data_u8_at2(D, COK_DATA_TURNING, 13, 10) == 0x0d);
    long area = cok_data_offset2(D, COK_DATA_AREA_ROWS, -3, 1);
    CHECK(cok_data_held(D, area) == 0xfd && cok_data_held(D, area + 1) == 0xff);
    /* The shapes the code gives: 66 terrains, 5 formations, the cloud's
     * cells from 1 (the fifth, DS:6346, is read by no routine found). */
    CHECK(cok_data_count(D, COK_DATA_TERRAIN, 0) == 66);
    CHECK(cok_data_count(D, COK_DATA_FORMATIONS, 0) == 5);
    CHECK(cok_data_first(D, COK_DATA_CLOUD_CELLS, 0) == 1);
}

int main(void)
{
    test_tables();
    test_combat_tables();
    test_hit_points();
    test_experience();
    test_party_again();
    test_added();
    puts("dark queen tests passed");
    return 0;
}
