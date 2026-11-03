#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* The Dark Queen of Krynn's levels (src/queen_levels.c) and the tables of
 * its data segment they read (src/data_dark_queen.c), against the original's
 * bytes in build/DQK_FULL.EXE (make dqk-merged) and the party of its shipped
 * saved game in DQKAssets/ (the checks needing either are skipped, with a
 * line saying so, where it is not there). */
#include "combat.h"
#include "ecl.h"
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

/* A spell's name (magic.c's cok_spell_name, not linked here). */
static const char *spell_name(unsigned id)
{
    return id >= 1 && id < R->spells.count ? R->spells.names[id - 1] : NULL;
}

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
        COK_DATA_HELPLESS,            COK_DATA_BATTLE_ONLY,    COK_DATA_CLOUD_CELLS,
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
    /* The shapes the code gives: 77 terrains, up to the footprints at DS:62cb
     * (the terrain reads are unbounded; the table's end is the data's);
     * 5 formations; 9 cloud cells from 1 (5837:13c4, 148f:27a7); 6 footprint
     * cells (5837:01b9); 4 helpless effects from 0 (177e:0a19); 16 battle-only
     * effects from 1 (148f:1b66). */
    CHECK(cok_data_count(D, COK_DATA_TERRAIN, 0) == 77);
    CHECK(D->table[COK_DATA_TERRAIN].from + D->table[COK_DATA_TERRAIN].size ==
          D->table[COK_DATA_FOOTPRINTS].from);
    CHECK(D->table[COK_DATA_TERRAIN].from + D->table[COK_DATA_TERRAIN].size == 0x62cb);
    CHECK(cok_data_count(D, COK_DATA_FORMATIONS, 0) == 5);
    CHECK(cok_data_count(D, COK_DATA_CLOUD_CELLS, 0) == 9);
    CHECK(cok_data_first(D, COK_DATA_CLOUD_CELLS, 0) == 1);
    CHECK(cok_data_count(D, COK_DATA_FOOTPRINTS, 1) == 6);
    CHECK(cok_data_count(D, COK_DATA_HELPLESS, 0) == 4);
    CHECK(cok_data_first(D, COK_DATA_BATTLE_ONLY, 0) == 1);
    CHECK(cok_data_count(D, COK_DATA_BATTLE_ONLY, 0) == 16);
    CHECK(cok_data_u8_at(D, COK_DATA_HELPLESS, 0) == 0x1f);
    CHECK(cok_data_u8_at(D, COK_DATA_BATTLE_ONLY, 1) == 0x03);
    CHECK(cok_data_u8_at(D, COK_DATA_BATTLE_ONLY, 16) == 0x76);
    CHECK(cok_data_u8_at(D, COK_DATA_CLOUD_CELLS, 9) == 0x01);
}

/* The spells' and effects' handler tables against the stores their fill
 * routines make in build/DQK_FULL.EXE, read again here: `movw $imm, disp16`
 * (c7 06) is an offset (or a segment given explicitly), `mov %cs, disp16`
 * (8c 0e) the routine's own segment. The routines are 7fd8:46bb-4892 and
 * 4892-4c2c for the spells (DS:af45 + 4 * id), 78e4:4fb2-549e and 549e-5a00
 * for the effects (DS:bee8 + 4 * id). Each id a store fills has the handler the
 * store gives, cited as its routine ("dqk seg:off", past an overlay stub), or
 * COK_FX_NOTHING where the routine is a bare retf; each id no store fills is
 * empty (a spell's stop cites its 0:0 bytes). */
struct fill_routine {
    uint16_t seg, start, end;
};
struct fill_cell {
    int stores;
    bool cs;
    uint16_t imm;
};

static void fill_check(const uint8_t *exe, size_t header, const struct fill_routine *routines,
                       size_t routine_count, uint16_t base, unsigned count, struct fill_cell *cells)
{
    size_t bytes = 4 * (size_t)count + 2;
    for (size_t r = 0; r < routine_count; ++r) {
        size_t at = header + (size_t)(routines[r].seg - 0x1000) * 16;
        for (size_t i = routines[r].start; i + 6 <= routines[r].end; ++i) {
            const uint8_t *p = exe + at + i;
            if (p[0] == 0xc7 && p[1] == 0x06) {
                uint16_t disp = (uint16_t)(p[2] | p[3] << 8);
                if (disp < base || (size_t)(disp - base) >= bytes) continue;
                struct fill_cell *c = &cells[disp - base];
                c->stores++;
                c->cs = false;
                c->imm = (uint16_t)(p[4] | p[5] << 8);
            } else if (p[0] == 0x8c && p[1] == 0x0e) {
                uint16_t disp = (uint16_t)(p[2] | p[3] << 8);
                if (disp < base || (size_t)(disp - base) >= bytes) continue;
                struct fill_cell *c = &cells[disp - base];
                c->stores++;
                c->cs = true;
                c->imm = routines[r].seg;
            }
        }
    }
}

/* The cell's routine: its own segment for mov %cs, else the explicit one
 * (Ghidra's numbering: the stored segment + 0x1000). */
static void fill_expect(const struct fill_cell *seg, uint16_t *out)
{
    *out = seg->cs ? seg->imm : (uint16_t)(seg->imm + 0x1000);
}

/* The routine a stored far pointer reaches: an overlay stub is an ljmp (0xea,
 * seg:off, the target in the map's numbering: +0x1000 here) that the merge
 * leaves at each stub's address (docs/disassembly.md). */
static void routine_of(const uint8_t *exe, size_t header, uint16_t *seg, uint16_t *off)
{
    for (int hop = 0; hop < 4; ++hop) {
        const uint8_t *p = exe + header + (size_t)(*seg - 0x1000) * 16 + *off;
        if (p[0] != 0xea) return;
        *off = (uint16_t)(p[1] | p[2] << 8);
        *seg = (uint16_t)((p[3] | p[4] << 8) + 0x1000);
    }
}

static void test_magic_ids(void)
{
    static uint8_t exe[1 << 20];
    size_t size = read_whole("build/DQK_FULL.EXE", exe, sizeof exe);
    if (size == 0) {
        puts("dark queen: build/DQK_FULL.EXE not built (make dqk-merged); spell and effect tables not checked");
        return;
    }
    size_t header = (size_t)(exe[8] | exe[9] << 8) * 16, ds = header + (size_t)0x322e * 16;
    CHECK(R->spells.count == 0x8d && R->effects.count == 0xe6);

    /* The names: the far pointers at DS:25c7 + 4 * id (the pushes at 8438:0009). */
    CHECK(R->spells.names_at == 0x25c7 && R->spells.name_size == 4);
    for (unsigned id = 1; id < R->spells.count; ++id) {
        uint16_t off = (uint16_t)(exe[ds + 0x25c7 + 4 * id] | exe[ds + 0x25c8 + 4 * id] << 8);
        uint16_t seg = (uint16_t)(exe[ds + 0x25c9 + 4 * id] | exe[ds + 0x25ca + 4 * id] << 8);
        const char *name = spell_name((uint8_t)id);
        if (off == 0) {
            CHECK(id >= 0x8a && name != NULL && name[0] == '\0'); /* 0000:0000: "" */
            continue;
        }
        CHECK(seg == 0x322e && name != NULL);
        CHECK(strcmp(name, (const char *)exe + ds + off) == 0);
    }
    CHECK(spell_name(0) == NULL && spell_name(0x8d) == NULL);

    /* The headings: 41 bytes a level, 'Special' for 0 (DS:8b2e). */
    CHECK(R->spells.level_count == 10 && R->spells.levels_at == 0x8b2e && R->spells.level_size == 41);
    for (unsigned level = 0; level < R->spells.level_count; ++level)
        CHECK(strcmp(R->spells.levels[level], (const char *)exe + ds + 0x8b2e + 41 * level) == 0);

    /* The spells' handlers. */
    static const struct fill_routine spell_routines[] = {{0x7fd8, 0x46bb, 0x4892}, {0x7fd8, 0x4892, 0x4c2c}};
    static struct fill_cell spell_cells[4 * 0x8d + 2];
    fill_check(exe, header, spell_routines, 2, 0xaf45, 0x8d, spell_cells);
    for (unsigned id = 0; id < R->spells.count; ++id) {
        const struct fill_cell *off = &spell_cells[4 * id], *seg = &spell_cells[4 * id + 2];
        const cok_rules_cast *h = &R->spells.cast[id];
        bool filled = off->stores != 0 || seg->stores != 0;
        CHECK(filled == (id != 0 && id != 0x41 && id != 0x5f && id != 0x61 && id != 0x63));
        if (!filled) {
            /* Never stored: DS:af45 + 4 * id holds 0:0 in the image (and the fills
             * above store nothing there), so the stop cites that. */
            const uint8_t *far = exe + ds + 0xaf45 + 4 * id;
            CHECK(far[0] == 0 && far[1] == 0 && far[2] == 0 && far[3] == 0);
            CHECK(h->address == 0 && h->handler == COK_CAST_NONE);
            if (id == 0) {
                CHECK(h->unported == NULL);
            } else {
                char text[64];
                snprintf(text, sizeof text, "DS:af45 + 0x%x is 0:0, never stored (7fd8:46bb, 7fd8:4892)",
                         4 * id);
                CHECK(h->unported != NULL && strcmp(h->unported, text) == 0);
            }
            continue;
        }
        CHECK(off->stores == 1 && seg->stores == 1 && !off->cs);
        uint16_t sg = 0, so = off->imm;
        fill_expect(seg, &sg);
        routine_of(exe, header, &sg, &so);
        char cite[32];
        snprintf(cite, sizeof cite, "dqk %04x:%04x", sg, so);
        CHECK(h->address == off->imm && h->handler == COK_CAST_NONE && h->unported != NULL);
        CHECK(strcmp(h->unported, cite) == 0);
    }

    /* The effects' handlers: a routine that is a bare retf (push bp; mov bp,sp;
     * pop bp; retf) does nothing, COK_FX_NOTHING; the rest are cited. */
    static const struct fill_routine effect_routines[] = {{0x78e4, 0x4fb2, 0x549e}, {0x78e4, 0x549e, 0x5a00}};
    static struct fill_cell effect_cells[4 * 0xe6 + 2];
    fill_check(exe, header, effect_routines, 2, 0xbee8, 0xe6, effect_cells);
    static const uint8_t retf_only[5] = {0x55, 0x8b, 0xec, 0x5d, 0xcb};
    for (unsigned id = 0; id < R->effects.count; ++id) {
        const struct fill_cell *off = &effect_cells[4 * id], *seg = &effect_cells[4 * id + 2];
        const cok_rules_effect *h = &R->effects.handlers[id];
        bool filled = off->stores != 0 || seg->stores != 0;
        if (id == 0x80 || id == 0x90 || id == 0x9d) CHECK(!filled);
        else CHECK(filled);
        if (!filled) {
            CHECK(h->address == 0 && h->handler == COK_FX_MISSING && h->unported == NULL);
            continue;
        }
        CHECK(off->stores == 1 && seg->stores == 1 && !off->cs);
        uint16_t sg = 0, so = off->imm;
        fill_expect(seg, &sg);
        CHECK(h->address == off->imm);
        routine_of(exe, header, &sg, &so);
        if (id == 0x75) CHECK(sg == 0x52db && so == 0x4fd1 && off->imm == 0x10a3); /* 2d99:10a3, a stub */
        if (memcmp(exe + header + (size_t)(sg - 0x1000) * 16 + so, retf_only, sizeof retf_only) == 0) {
            CHECK(h->handler == COK_FX_NOTHING && h->unported == NULL);
        } else {
            char cite[32];
            snprintf(cite, sizeof cite, "dqk %04x:%04x", sg, so);
            CHECK(h->handler == COK_FX_MISSING && h->unported != NULL && strcmp(h->unported, cite) == 0);
        }
    }
    /* No effect id is named yet (rules_dark_queen_magic.c): effects.ids stays NULL. */
    CHECK(R->effects.ids == NULL);

    /* The named spell ids: the Dark Queen names of the Champions spells the
     * shared code asks for; the names it has none of are 0. */
    static const struct {
        cok_named_spell spell;
        uint8_t id;
        const char *name;
    } named[] = {
        {COK_SPELL_BLESS, 0x01, "Bless"},
        {COK_SPELL_CURE_LIGHT_WOUNDS, 0x03, "Cure Light Wounds"},
        {COK_SPELL_MAGIC_MISSILE, 0x0f, "Magic Missile"},
        {COK_SPELL_HOLD_PERSON, 0x17, "Hold Person"},
        {COK_SPELL_KNOCK, 0x1f, "Knock"},
        {COK_SPELL_REMOVE_CURSE, 0x2b, "Remove Curse"},
        {COK_SPELL_FIREBALL, 0x2f, "Fireball"},
        {COK_SPELL_HOLD_PERSON_MAGE, 0x31, "Hold Person"},
        {COK_SPELL_LIGHTNING_BOLT, 0x33, "Lightning Bolt"},
        {COK_SPELL_CURE_SERIOUS_WOUNDS, 0x3a, "Cure Serious Wounds"},
        {COK_SPELL_CURE_CRITICAL_WOUNDS, 0x47, "Cure Critical Wounds"},
        {COK_SPELL_ENTANGLE, 0x4e, "Entangle"},
        {COK_SPELL_CHARM_MONSTERS, 0x51, "Charm Monsters"},
        {COK_SPELL_CONFUSION, 0x52, "Confusion"},
        {COK_SPELL_DIMENSION_DOOR, 0x53, "Dimension Door"},
        {COK_SPELL_FEAR, 0x54, "Fear"},
        {COK_SPELL_FIRE_SHIELD, 0x55, "Fire Shield"},
        {COK_SPELL_FUMBLE, 0x56, "Fumble"},
        {COK_SPELL_FIREBALL_ITEM, 0x85, "Necklace Of Missiles"},
        {COK_SPELL_BLESS_POWER, 0x3e, "Bless"},
    };
    for (size_t n = 0; n < sizeof named / sizeof *named; ++n) {
        CHECK(R->spells.ids[named[n].spell] == named[n].id);
        CHECK(strcmp(spell_name(named[n].id), named[n].name) == 0);
        CHECK(cok_spell_id(R, named[n].spell) == named[n].id);
    }
    /* The roles, from the executable. The item's Fireball: 7fd8:1545 is
     * `cmpb $id, DS:b2ea` (the spell cast) and takes the item's dice when it is
     * the Necklace's id. The granted Bless: of the spell records, the only one
     * that is kind 3 at level 0 and named Bless (kind word +0, level +2). */
    const uint8_t *cmp = exe + header + (size_t)(0x7fd8 - 0x1000) * 16 + 0x1545;
    CHECK(cmp[0] == 0x80 && cmp[1] == 0x3e && cmp[2] == 0xea && cmp[3] == 0xb2);
    CHECK(cmp[4] == R->spells.ids[COK_SPELL_FIREBALL_ITEM]);
    unsigned granted = 0, granted_id = 0;
    for (unsigned id = 1; id < R->spells.count; ++id) {
        const uint8_t *rec = exe + ds + 0x27e9 + 22 * id;
        if (rec[0] == 3 && rec[1] == 0 && rec[2] == 0 && strcmp(spell_name(id), "Bless") == 0) {
            granted++;
            granted_id = id;
        }
    }
    CHECK(granted == 1 && granted_id == R->spells.ids[COK_SPELL_BLESS_POWER]);
    for (unsigned name = 0; name < COK_SPELL_NAMES; ++name) {
        bool listed = false;
        for (size_t n = 0; n < sizeof named / sizeof *named; ++n) listed |= named[n].spell == name;
        CHECK(listed || R->spells.ids[name] == 0);
    }
}

/* The cells' hash (test_combat.c's). */
static uint32_t cells_hash(const cok_combat *c)
{
    uint32_t h = 0x811c9dc5u;
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y)
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) h = (h ^ c->cells[y][x]) * 0x01000193u;
    return h;
}

/* The draws from seed that reach target, one step each (every roll of the builders is above 1,
 * so each steps the seed once); ~0 if it is not reached. */
static uint32_t draws_to(uint32_t seed, uint32_t target)
{
    for (uint32_t n = 0; n < 100000; ++n) {
        if (seed == target) return n;
        cok_tp_random(&seed, 100);
    }
    return ~0u;
}

/* Dark Queen's open ground (dqk 59c7:0e3b): Death Knights' floor, river, trees and scatter, the
 * scatter's cell as 59c7:0ce3's, the flags of DS:5d7b's entry (flags 0x71: scatter's density 110,
 * tree chance 7, and flags 0x06: density 20, tree chance 1). The builders roll with Dark Queen's
 * generator (2102:0234, the descriptor's random), from seed 12345. The pinned hashes, seeds and
 * draw counts are the port's; the wild flow of tools/dosbox/checks/dqk_fight.py checked them
 * against the original's RandSeed at 59c7:0f7b when its grounds were these flags (terrain 12 and
 * 14, then 2629 and 2327 draws); the wild flow's grounds are now the shipped var 0x1a2 values 1 and 2
 * (flags 0x18 and 0x11), built by the same routines. */
static void test_wilderness(void)
{
    static cok_combat c;
    cok_random_select(R->random);
    uint8_t flags = cok_data_u8_at(R->data, COK_DATA_TERRAIN_FLAGS, 12);
    CHECK(flags == 0x71 && cok_data_u8_at(R->data, COK_DATA_TERRAIN_FLAGS, 14) == 0x06);
    uint32_t seed = 12345;
    CHECK(cok_ecl_var_address(R, COK_VAR_RIVER, 0) == 0x33 &&
          cok_ecl_var_address(R, COK_VAR_ABYSS, 0) == 0x1a2);
    cok_combat_wilderness(&c, R, flags, false, &seed);
    CHECK(cells_hash(&c) == 0x97ec18aau && seed == 2803431546u && draws_to(12345, seed) == 2629u);
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y)
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) CHECK(c.cells[y][x] != 0x34);
    seed = 12345;
    cok_combat_wilderness(&c, R, 0x06, true, &seed);
    CHECK(cells_hash(&c) == 0x4bcdffe2u && seed == 3122112960u && draws_to(12345, seed) == 2327u);
    /* The river: from a column 34 less 5d4, moved left to a multiple of 7 less 2 (on row 0), its
     * bank 0x34 and water or a ford 0x34-0x3d, the far bank 0x34 five on, a column right a row. */
    int x = -1;
    for (int k = 0; k < COK_COMBAT_WIDTH && x < 0; ++k)
        if (c.cells[0][k] == 0x34) x = k;
    CHECK(x >= 0 && (x + 2) % 7 == 0 && x >= 0x22 - 20 && x <= 0x22 - 5);
    for (int y = 0; y < COK_COMBAT_HEIGHT && x + 5 + y < COK_COMBAT_WIDTH - 1; ++y) {
        CHECK(c.cells[y][x + y] == 0x34 && c.cells[y][x + y + 5] == 0x34);
        for (int k = 1; k <= 4; ++k)
            CHECK(c.cells[y][x + y + k] >= 0x34 && c.cells[y][x + y + k] <= 0x3d);
    }
    cok_random_select(COK_RANDOM_PASCAL);
}

/* The placement's distance (dqk 59c7:16b3's flag): var 0x37, else 1 for a facing that is not
 * the party's, the same for the demonstration as none (its demonstration distance is 0). The
 * placement itself (59c7:1864) is not ported, so this checks the helper the layout shares. */
static void test_layout_distance(void)
{
    const cok_rules_combat *C = &R->combat;
    CHECK(cok_combat_layout_distance(C, 0, 2, 0, false) == 1);
    CHECK(cok_combat_layout_distance(C, 0, 0, 0, false) == 0);
    CHECK(cok_combat_layout_distance(C, 3, 2, 0, false) == 3);
    CHECK(cok_combat_layout_distance(C, 3, 0, 0, false) == 3);
    CHECK(cok_combat_layout_distance(C, 0, 2, 0, true) == 1);
    CHECK(cok_combat_layout_distance(C, 0, 0, 0, true) == 0);
}

int main(void)
{
    test_wilderness();
    test_layout_distance();
    test_tables();
    test_combat_tables();
    test_magic_ids();
    test_hit_points();
    test_experience();
    test_party_again();
    test_added();
    puts("dark queen tests passed");
    return 0;
}
