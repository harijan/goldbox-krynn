/* Death Knights of Krynn's descriptor (src/rules_death_knights.c): what it
 * holds against the original's bytes in build/death-START_FULL.EXE (make
 * death-merged) and against the files in DKKAssets/; its 3D view's wall
 * sets and tiles (view.c with its shape); and what it leaves out. Without
 * DKKAssets/ the checks of its files and executable are skipped, with a
 * line saying so. */
#include "dax.h"
#include "ecl.h"
#include "rules.h"
#include "screen.h"
#include "view.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_death_knights;

/* The import's moves (46b2:10d7) against the two layouts: every field of
 * Champions' record whose bytes a move takes lies in Death Knights' where
 * the move puts it (but the former levels, which Death Knights holds as
 * the levels before a drain), and each field Death Knights has that
 * Champions' import fills is so filled; the spells' moves likewise, into
 * the block; the moves within both records. */
static void test_import_layout(void)
{
    const cok_rules_import *I = R->characters.import;
    const cok_character_layout *C = I->from->character, *D = R->character;
    CHECK(I->from == &cok_rules_champions);
    size_t moved = 0, block = 0;
    for (int f = 0; f < COK_FIELD_COUNT; ++f) {
        const cok_field_place *c = &C->field[f];
        if (c->width == 0) continue;
        size_t first = c->offset, last = first + cok_field_size(C, (cok_field)f) - 1;
        for (size_t i = 0; i < I->move_count; ++i) {
            const cok_rules_move *m = &I->moves[i];
            if (first < m->from || last >= m->from + m->count) continue;
            if (f == COK_FIELD_FORMER_LEVEL) {
                CHECK(!cok_field_present(D, (cok_field)f));
                CHECK(cok_field_offset(D, COK_FIELD_UNDRAINED_LEVEL) == m->to + first - m->from);
                continue;
            }
            if (cok_field_offset(D, (cok_field)f) != m->to + first - m->from)
                fprintf(stderr, "field %d: Champions' +0x%03zx moves to +0x%02zx, the layout's "
                                "+0x%02zx\n", f, first, m->to + first - m->from,
                        cok_field_offset(D, (cok_field)f));
            CHECK(cok_field_offset(D, (cok_field)f) == m->to + first - m->from);
            CHECK(D->field[f].width == c->width && D->field[f].stride == c->stride);
            ++moved;
        }
        for (size_t i = 0; i < I->spell_move_count; ++i) {
            const cok_rules_move *m = &I->spell_moves[i];
            if (first < m->from || first >= m->from + m->count) continue;
            CHECK(cok_field_in_spells(D, (cok_field)f));
            if (m->from == first)
                CHECK(cok_field_offset(D, (cok_field)f) == D->spells_offset + m->to);
            ++block;
        }
    }
    /* 64 fields moved, and into the block the memorized and known spells
     * and the spells a day (its first row's move). */
    CHECK(moved == 64 && block == 3);
    for (size_t i = 0; i < I->move_count; ++i)
        CHECK(I->moves[i].to + I->moves[i].count <= D->file_size &&
              I->moves[i].from + I->moves[i].count <= C->file_size);
    for (size_t i = 0; i < I->spell_move_count; ++i)
        CHECK(I->spell_moves[i].to + I->spell_moves[i].count <= D->spells_size);
}

static void test_lookup(void)
{
    CHECK(cok_rules_find("death-knights") == R);
    CHECK(strcmp(cok_rules_names(), "champions, death-knights") == 0);
    CHECK(strcmp(R->assets, "DKKAssets") == 0);
    CHECK(strcmp(R->text.name, "Death Knights of Krynn") == 0);
    CHECK(R->unplayable != NULL && cok_rules_champions.unplayable == NULL);
    /* The character record: 216 bytes in its files, the spells' block of
     * 220 after it (tests/test_layout.c checks each field's place). */
    CHECK(R->character->file_size == 0xd8 && R->character->spells_offset == 0xd8 &&
          R->character->spells_size == 0xdc && R->character->size == 0xd8 + 0xdc);
    CHECK(strcmp(R->saves.spells, "WIZ") == 0 && cok_rules_champions.saves.spells == NULL);
    CHECK(R->characters.stats == COK_STATS_DEATH_KNIGHTS &&
          R->characters.levels == COK_LEVELS_DEATH_KNIGHTS && R->characters.import != NULL);
    CHECK(cok_rules_champions.characters.stats == COK_STATS_CHAMPIONS &&
          cok_rules_champions.characters.levels == COK_LEVELS_CHAMPIONS &&
          cok_rules_champions.characters.import == NULL);
    /* Each field the ECL's table names is the record's. */
    for (size_t i = 0; i < R->ecl.character_count; ++i) {
        const cok_rules_character_var *e = &R->ecl.character[i];
        if (e->read == COK_ECL_FIELD_U8 || e->read == COK_ECL_FIELD_S8 ||
            e->read == COK_ECL_FIELD_U16 || e->read == COK_ECL_FIELD_NAME_BYTES)
            CHECK(cok_field_present(R->character, (cok_field)e->field));
    }
    test_import_layout();
    /* Every table of the data segment but the two it has no counterpart
     * of. */
    for (size_t t = 0; t < COK_DATA_TABLES; ++t)
        CHECK((R->data->table[t].width != 0) ==
              (t != COK_DATA_TREASURE_TEMPLATES && t != COK_DATA_DISEASES));
    /* The archives Death Knights has not. */
    const cok_rules_archives *a = &R->archives;
    CHECK(a->sky == NULL && a->head == NULL && a->body == NULL && a->random == NULL);
    CHECK(a->music_drivers == NULL && a->music_score == NULL && R->saves.debug == NULL);
    CHECK(cok_rules_champions.archives.ground == NULL &&
          cok_rules_champions.archives.backdrop == NULL &&
          cok_rules_champions.archives.wall_files == NULL);
    CHECK(a->file_digit(1) == '1' && a->file_digit(3) == '1');
}

static bool exists(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "DKKAssets/%s", name);
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Whether DKKAssets/<name> holds record id. */
static bool holds(const char *name, unsigned id)
{
    char path[256];
    snprintf(path, sizeof path, "DKKAssets/%s", name);
    dax_archive archive = {0};
    if (dax_open(path, &archive) != DAX_OK) return false;
    bool found = false;
    for (size_t i = 0; i < archive.count; ++i) {
        dax_record record;
        CHECK(dax_record_at(&archive, i, &record) == DAX_OK);
        found |= record.id == id;
    }
    dax_close(&archive);
    return found;
}

static void test_assets(void)
{
    const cok_rules_archives *a = &R->archives;
    char name[64];
    const char *const numbered[] = {a->ecl, a->geo, a->walldef, a->tiles, a->pic,
                                    a->sprite, a->big, a->icons, a->backdrop};
    for (size_t i = 0; i < sizeof numbered / sizeof *numbered; ++i) {
        snprintf(name, sizeof name, "%s1.DAX", numbered[i]);
        CHECK(exists(name));
    }
    snprintf(name, sizeof name, "%s1%s.DAX", a->monster, a->monster_record);
    CHECK(exists(name));
    const char *const singles[] = {a->missiles, a->icon_head, a->icon_body, a->ground,
                                   a->title,    a->dungeon,   a->wilderness};
    for (size_t i = 0; i < sizeof singles / sizeof *singles; ++i) {
        snprintf(name, sizeof name, "%s.DAX", singles[i]);
        CHECK(exists(name));
    }
    /* Champions' files it has not. */
    CHECK(!exists("SKY.DAX") && !exists("CURSOR.DAX") && !exists("RANDCOM.DAX"));
    CHECK(!exists("HEAD1.DAX") && !exists("BODY1.DAX") && !exists("TILES.DAX"));
    CHECK(exists(R->files.config) && exists(R->files.item_types));
    /* The font, the cursor and the startup's tile set; the ground. */
    CHECK(holds("8X8D1.DAX", R->archives.font.record) && R->archives.font.record == 201);
    CHECK(R->archives.cursor.file == 1 && R->archives.cursor.record == 204);
    CHECK(holds("8X8D1.DAX", R->archives.cursor.record));
    for (size_t i = 0; i < R->startup.tile_count; ++i)
        CHECK(holds("8X8D1.DAX", R->startup.tiles[i].record));
    CHECK(holds("SKYGRND.DAX", 1) && holds("SKYGRND.DAX", 2));
    /* Startup's icons: each COMSPR record in a slot of the game's. */
    for (size_t r = 0; r < R->startup.icon_ranges; ++r) {
        CHECK(R->startup.icons[r].slot + R->startup.icons[r].count <= R->startup.icon_slots);
        for (unsigned i = 0; i < R->startup.icons[r].count; ++i)
            CHECK(holds("COMSPR.DAX", R->startup.icons[r].first + i));
    }
    /* Each wall set by its id: WALLDEF, 8X8D and BACK of the file its id
     * names hold it, and no other file does. */
    for (unsigned id = 0; id < 256; ++id) {
        unsigned file = a->wall_files[id];
        for (unsigned n = 1; n <= 5; ++n) {
            const char *const stems[] = {a->walldef, a->tiles, a->backdrop};
            for (size_t s = 0; s < 3; ++s) {
                snprintf(name, sizeof name, "%s%u.DAX", stems[s], n);
                if (s == 1 && n == 1 && (id == 201 || id == 202 || id == 203 || id == 204))
                    continue; /* the font's file */
                CHECK(holds(name, id) == (file == n));
            }
        }
    }
    /* Each picture and sprite of PIC4-7 and SPRIT4 by its id, which the
     * ECL file does not choose. */
    for (unsigned id = 0; id < 256; ++id) {
        for (unsigned n = 4; n <= 7; ++n) {
            snprintf(name, sizeof name, "%s%u.DAX", a->pic, n);
            CHECK(holds(name, id) == (a->pic_files[id] == n));
        }
        snprintf(name, sizeof name, "%s4.DAX", a->sprite);
        CHECK(holds(name, id) == (a->sprite_files[id] == 4));
        CHECK(a->sprite_files[id] == 0 || a->sprite_files[id] == 4);
    }
    /* The archives the image tools export. */
    const char *const *lists[] = {R->images.delta, R->images.collection, R->images.single};
    size_t listed = 0;
    for (size_t i = 0; i < 3; ++i)
        for (const char *const *stem = lists[i]; *stem != NULL; ++stem, ++listed) {
            snprintf(name, sizeof name, "%s.DAX", *stem);
            CHECK(exists(name));
        }
    CHECK(listed == 30);
}

/* The original's bytes: data segment DS 0x1bce (image paragraph 0x0bce). */
static uint8_t exe[1 << 20];
static size_t exe_size, header;

static const uint8_t *at(unsigned segment, unsigned offset)
{
    size_t where = header + (size_t)(segment - 0x1000) * 16 + offset;
    CHECK(where < exe_size);
    return exe + where;
}

static const uint8_t *ds(unsigned offset)
{
    return at(0x1bce, offset);
}

/* The opcode's way through a chain of compares of AL (3c1e:1b48's, the
 * dispatcher's at 3632:2a54): cmp al, imm8 and the jumps on its flags,
 * mov al, imm8, push; it ends at the near call it makes (*call, *al the
 * count moved before it) or at 1b48's inc word [8c48] (*call 0). */
static void follow(unsigned segment, unsigned ip, uint8_t al, unsigned *call, int *count)
{
    bool zf = false, cf = false;
    *count = -1;
    for (int steps = 0; steps < 400; ++steps) {
        const uint8_t *b = at(segment, ip);
        if (b[0] == 0x3c) { /* cmp al, imm8 */
            zf = al == b[1];
            cf = al < b[1];
            ip += 2;
        } else if (b[0] == 0x74 || b[0] == 0x75 || b[0] == 0x72 || b[0] == 0x73 || b[0] == 0x76 ||
                   b[0] == 0x77 || b[0] == 0xeb) {
            bool take = b[0] == 0x74   ? zf
                        : b[0] == 0x75 ? !zf
                        : b[0] == 0x72 ? cf
                        : b[0] == 0x73 ? !cf
                        : b[0] == 0x76 ? cf || zf
                        : b[0] == 0x77 ? !(cf || zf)
                                       : true;
            ip += 2;
            if (take) ip = (ip + (unsigned)(int8_t)b[1]) & 0xffff;
        } else if (b[0] == 0xe9) { /* jmp near */
            ip = (ip + 3 + (unsigned)(int16_t)(b[1] | b[2] << 8)) & 0xffff;
        } else if (b[0] == 0xb0) { /* mov al, imm8 */
            *count = b[1];
            ip += 2;
        } else if (b[0] == 0x50 || b[0] == 0x0e) { /* push ax, push cs */
            ++ip;
        } else if (b[0] == 0xe8) { /* call near */
            *call = (ip + 3 + (unsigned)(int16_t)(b[1] | b[2] << 8)) & 0xffff;
            return;
        } else if (b[0] == 0xff && b[1] == 0x06) { /* inc word [8c48]: skip the opcode alone */
            *call = 0;
            *count = 0;
            return;
        } else {
            CHECK(!"an instruction the compares do not use");
        }
    }
    CHECK(!"no end to the compares");
}

/* The operands a handler at 3632:handler loads first: mov al, n; push ax;
 * call 0041:002a (3c1e:0000's stub), before its return; 0 if none. */
static int loads(unsigned handler)
{
    const uint8_t *b = at(0x3632, handler);
    static const uint8_t call[] = {0x50, 0x9a, 0x2a, 0x00, 0x41, 0x00};
    for (size_t i = 0; i < 0x60 && b[i] != 0xcb && b[i] != 0xc3; ++i)
        if (b[i] == 0xb0 && memcmp(b + i + 2, call, sizeof call) == 0) return b[i + 1];
    return 0;
}

/* The ECL's opcodes against the original's: each opcode's handler by the
 * dispatcher (3632:2a54), the operands it loads first (a list's header for
 * the lists), and those a failed IF skips (3c1e:1b48). */
static void test_opcodes(void)
{
    for (unsigned o = 0; o < COK_ECL_OPCODES; ++o) {
        const cok_rules_opcode *op = &R->ecl.opcodes[o];
        unsigned call = 0, handler = 0;
        int count, pushed;
        follow(0x3c1e, 0x1b5f, (uint8_t)o, &call, &count);
        CHECK(call == 0x0000); /* 3c1e:0000, which loads count, or the inc */
        CHECK(op->skip == count);
        follow(0x3632, 0x2a5a, (uint8_t)o, &handler, &pushed);
        int first = loads(handler);
        if (op->operands == COK_ECL_LIST)
            CHECK(op->header == first);
        else
            CHECK(op->operands == first);
    }
    /* An opcode past the table: 1b48 skips it alone. */
    unsigned call = 1;
    int count;
    follow(0x3c1e, 0x1b5f, 0x42, &call, &count);
    CHECK(call == 0 && count == 0);
}

/* The variables against the original's: the offsets of its 0x7c00 ones
 * (DS:836-864, where its code reads them through), and its special
 * addresses: 3c1e:0d85 compares 0x4bea and 0x4cf8 in the area's block,
 * 0bc1 stores to none at or below 49000. */
static void test_variables(void)
{
    uint16_t offsets[24];
    for (size_t i = 0; i < 24; ++i)
        offsets[i] = (uint16_t)(ds(0x836)[2 * i] | ds(0x836)[2 * i + 1] << 8);
    CHECK(offsets[0] == 0x2a8 && offsets[23] == 0x3f6);
    size_t found = 0;
    for (size_t v = 0; v < COK_VARS; ++v) {
        const cok_rules_var *var = &R->ecl.vars[v];
        if (var->address < 0x7c00 || var->address >= 0x8000) continue;
        bool listed = false;
        for (size_t i = 0; i < 24; ++i) listed |= offsets[i] == var->address - 0x7c00;
        /* Its ECL file is read at its offset in the code (0x312), the
         * block's variables by a loop (3c1e:01c1). */
        CHECK(listed || v == COK_VAR_ECL_FILE || v == COK_VAR_LOCALS_GAME);
        found += listed;
    }
    CHECK(found == 8);
    const uint8_t *get = at(0x3c1e, 0x0d85), *set = at(0x3c1e, 0x0bc1);
    size_t compared = 0;
    for (size_t i = 0; i < 0x190; ++i) {
        /* cmp word [bp+8], imm16 */
        if (get[i] != 0x81 || get[i + 1] != 0x7e || get[i + 2] != 0x08) continue;
        uint16_t address = (uint16_t)(get[i + 3] | get[i + 4] << 8);
        if (address < 0x4b00 || address >= 0x4f00) continue;
        CHECK(cok_ecl_special_read(R, address) != COK_ECL_SPECIAL_NONE);
        ++compared;
    }
    CHECK(compared == 2);
    for (size_t i = 0; i < R->ecl.read_count; ++i) {
        uint16_t a = R->ecl.reads[i].address;
        CHECK(a == 0x4bea || a == 0x4cf8 || a == 0x33d || (a >= 0xc04b && a <= 0xc04f));
    }
    bool bound = false;
    for (size_t i = 0; i < 0x1c0 && !bound; ++i)
        bound = memcmp(set + i, "\x81\x7e\x0a\x68\xbf", 5) == 0; /* cmp word [bp+0xa], 49000 */
    CHECK(bound);
    for (size_t i = 0; i < R->ecl.store_count; ++i) CHECK(R->ecl.stores[i].address > 49000);
}

/* The import (46b2:10d7) in the original's code: its ten Moves from the
 * Champions record's block ([bp-0x3f0]) to the record ([bp+0xa]), each
 * les di, the source (add di, from), les di, the record (add di, to),
 * mov ax, count, call Move (1a07:1ab1); the spells' Moves into the block
 * (+0xc2): the memorized and known spells and the loop of four spells a
 * day, 5 of +0x11c + 5 * i to +0xbc + 8 * i; the classes without a block;
 * the record and block sizes the native load and the save read and write
 * (0xd8, 0xdc); the set of the item types it drops (46b2:10b7). */
static void test_import(void)
{
    const cok_rules_import *I = R->characters.import;
    const uint8_t *code = at(0x46b2, 0x10d7);
    size_t found = 0;
    for (size_t i = 0; i + 32 < 0x480; ++i) {
        const uint8_t *p = code + i;
        if (memcmp(p, "\xc4\xbe\x10\xfc", 4) != 0) continue; /* les di, [bp-0x3f0] */
        p += 4;
        unsigned from = 0, to = 0;
        if (p[0] == 0x81 && p[1] == 0xc7) from = p[2] | p[3] << 8, p += 4;
        if (memcmp(p, "\x06\x57\xc4\x7e\x0a", 5) != 0) continue; /* push es, di; les di, [bp+0xa] */
        p += 5;
        if (p[0] == 0x81 && p[1] == 0xc7) to = p[2] | p[3] << 8, p += 4;
        if (memcmp(p, "\x06\x57\xb8", 3) != 0 || memcmp(p + 5, "\x50\x9a\xb1\x1a", 4) != 0)
            continue;
        unsigned count = p[3] | p[4] << 8;
        CHECK(found < I->move_count);
        CHECK(I->moves[found].from == from && I->moves[found].to == to &&
              I->moves[found].count == count);
        ++found;
    }
    CHECK(found == I->move_count);
    /* The block's moves: add di, from; ...; add di, to; mov ax, count. */
    static const uint8_t memorized[] = {0x81, 0xc7, 0x1e, 0x00, 0x06, 0x57, 0xc4, 0xbe},
                         known[] = {0x81, 0xc7, 0x63, 0x00, 0x06, 0x57, 0xc4, 0xbe};
    bool m = false, k = false;
    for (size_t i = 0; i + 40 < 0x480; ++i) {
        if (memcmp(code + i, memorized, sizeof memorized) == 0) m = true;
        if (memcmp(code + i, known, sizeof known) == 0) k = true;
    }
    CHECK(m && k);
    CHECK(I->spell_moves[0].from == 0x1e && I->spell_moves[0].to == 0 &&
          I->spell_moves[0].count == 0x3a);
    CHECK(I->spell_moves[1].from == 0x63 && I->spell_moves[1].to == 0x4a &&
          I->spell_moves[1].count == 0x6b);
    for (size_t g = 0; g < 4; ++g)
        CHECK(I->spell_moves[2 + g].from == 0x11c + 5 * g &&
              I->spell_moves[2 + g].to == 0xbc + 8 * g && I->spell_moves[2 + g].count == 5);
    bool counts[3] = {false, false, false}, classes = false;
    for (size_t i = 0; i + 8 < 0x480; ++i) {
        if (memcmp(code + i, "\xb8\x3a\x00", 3) == 0) counts[0] = true; /* mov ax, 0x3a */
        if (memcmp(code + i, "\xb8\x6b\x00", 3) == 0) counts[1] = true;
        if (memcmp(code + i, "\x81\xc7\xbc\x00", 4) == 0) counts[2] = true;
        /* cmp byte [es:di+0x21], 2 (then 6, 0x0e). */
        if (memcmp(code + i, "\x26\x80\x7d\x21\x02", 5) == 0) classes = true;
    }
    CHECK(counts[0] && counts[1] && counts[2] && classes);
    CHECK(I->no_spells[0] == 2 && I->no_spells[1] == 6 && I->no_spells[2] == 0x0e &&
          I->no_spells[3] == 0xff);
    /* The set of dropped item types, just before the routine. */
    CHECK(memcmp(at(0x46b2, 0x10b7), I->dropped_items, 32) == 0);
    /* The native load reads 0xd8 into the record and 0xdc into the block
     * (mov ax, 0xd8 / 0xdc before BlockRead, 1a07:1927). */
    bool record = false, spells = false;
    for (size_t i = 0; i + 16 < 0x900; ++i) {
        if (memcmp(code + i, "\xb8\xd8\x00\x50", 4) == 0) record = true;
        if (memcmp(code + i, "\xb8\xdc\x00\x50", 4) == 0) spells = true;
    }
    CHECK(record && spells);
    CHECK(R->character->file_size == 0xd8 && R->character->spells_size == 0xdc);
    /* Its extensions (DS:71c0-71d4) and the roster's and saved game's
     * (DS:57ac, 57b1). */
    CHECK(memcmp(ds(0x71c0), "\x04.wiz\x04.jnk\x04.eff\x04.stf\x04.sfx", 25) == 0);
    CHECK(memcmp(ds(0x57ac), "\x03.pc", 4) == 0 && memcmp(ds(0x57b1), "\x04.gam", 5) == 0);
}

/* The frames' tables: each line's tiles the bytes of a table 110f reads
 * by the cell's column or row (DS:070c, 0734 and 075c by column; 0784,
 * 07ac, 07d9, 07ef and 07fe by row), each table used; the icons startup
 * loads and the slots' table. */
static void test_frames(void)
{
    static const unsigned tables[] = {0x070c, 0x0734, 0x075c, 0x0784,
                                      0x07ac, 0x07d9, 0x07ef, 0x07fe};
    bool used[sizeof tables / sizeof *tables] = {false};
    for (size_t s = 0; s < COK_SCREENS; ++s)
        for (size_t i = 0; i < R->frame.screens[s].line_count; ++i) {
            const cok_rules_frame_line *l = &R->frame.screens[s].lines[i];
            if (l->tiles == NULL) continue;
            bool found = false;
            for (size_t t = 0; t < sizeof tables / sizeof *tables && !found; ++t)
                if (memcmp(ds(tables[t] + l->first), l->tiles, l->last - l->first + 1u) == 0)
                    found = used[t] = true;
            CHECK(found);
        }
    for (size_t t = 0; t < sizeof tables / sizeof *tables; ++t) CHECK(used[t]);
    /* 70eb:002f loads the frame's set 4 with colour 0 transparent, the
     * others with 13 (cmp [bp+6], 4; jnz; mov [bp-6], 0; jmp; mov [bp-6], 13). */
    CHECK(memcmp(at(0x70eb, 0x0049),
                 "\x80\x7e\x06\x04\x75\x06\xc6\x46\xfa\x00\xeb\x04\xc6\x46\xfa\x0d",
                 16) == 0);
    for (size_t s = 0; s < 5; ++s) CHECK(R->view.tile_transparent[s] == (s == 4 ? 0 : 13));
    /* 110f:0000's moons: + 0x15, 0x0d and 0x11 (add ax, imm16) on bytes
     * 0x1f9-0x1fb. */
    const uint8_t *frame = at(0x110f, 0);
    for (size_t i = 0; i < 3; ++i) {
        bool found = false;
        for (size_t k = 0; k + 2 < 0x160 && !found; ++k)
            found = frame[k] == 0x05 && frame[k + 1] == R->moons.tile[i] && frame[k + 2] == 0;
        CHECK(found);
    }
    /* 4f9a:00e1's COMSPR loop (at 07a6): ids from 0, each into slot id +
     * 0x0d (add ax, 0x0d before the call), to 0x15 (cmp byte [86f2], 0x15). */
    CHECK(R->startup.icon_ranges == 1 && R->startup.icons[0].first == 0 &&
          R->startup.icons[0].slot == 0x0d && R->startup.icons[0].count == 0x16);
    const uint8_t *loop = at(0x4f9a, 0x07a6);
    CHECK(memcmp(loop, "\x05\x0d\x00\x50\x9a", 5) == 0);
    CHECK(memcmp(loop + 9, "\x80\x3e\xf2\x86\x15", 5) == 0);
    /* 4f9a:0556 clears the slots' table, 8 bytes a slot: mov di, table;
     * push ds; push di; mov ax, slots * 8. */
    const cok_rules_startup *s = &R->startup;
    const uint8_t clear[] = {0xbf, (uint8_t)s->icon_table, (uint8_t)(s->icon_table >> 8), 0x1e,
                             0x57, 0xb8, (uint8_t)(s->icon_slots * 8),
                             (uint8_t)(s->icon_slots * 8 >> 8)};
    CHECK(memcmp(at(0x4f9a, 0x0556), clear, sizeof clear) == 0);
}

static void test_executable(void)
{
    FILE *f = fopen("build/death-START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("death knights: build/death-START_FULL.EXE not built (make death-merged); "
             "its tables not checked");
        return;
    }
    exe_size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(exe_size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    header = (size_t)(exe[8] | exe[9] << 8) * 16;
    /* The data segment's tables, byte for byte. */
    const cok_data_segment *D = R->data;
    for (size_t i = 0; i < D->table_count; ++i) {
        const cok_ds_table *t = &D->tables[i];
        for (unsigned k = 0; k < t->size; ++k) CHECK(*ds(t->offset + k * t->stride) == t->bytes[k]);
    }
    for (unsigned o = 0; o <= 0xffff; ++o) {
        uint8_t byte;
        if (cok_data_byte(D, (uint16_t)o, &byte)) CHECK(*ds(o) == byte);
    }
    /* The class and level tables (DS:71f2-82d3) follow each other without a
     * gap: each one's count and stride end where the next begins. */
    const cok_data_table *T = D->table;
    CHECK(T[COK_DATA_THAC0].from + T[COK_DATA_THAC0].size == T[COK_DATA_CLASS_BITS].from);
    CHECK(T[COK_DATA_THIEF_LEVEL].from + T[COK_DATA_THIEF_LEVEL].size ==
          T[COK_DATA_THIEF_RACE].from);
    CHECK(T[COK_DATA_THIEF_DEXTERITY].from + T[COK_DATA_THIEF_DEXTERITY].size ==
          T[COK_DATA_SCORE_LIMITS].from);
    CHECK(T[COK_DATA_SCORE_LIMITS].from + T[COK_DATA_SCORE_LIMITS].size ==
          T[COK_DATA_RACE_CLASSES].from);
    CHECK(T[COK_DATA_SAVING_THROWS].from + T[COK_DATA_SAVING_THROWS].size ==
          T[COK_DATA_SPELL_LISTS].from);
    CHECK(T[COK_DATA_SPELL_LISTS].from + T[COK_DATA_SPELL_LISTS].size == 0x82d4);
    CHECK(T[COK_DATA_SPELLS].from + T[COK_DATA_SPELLS].size == 0x71b0);
    /* A row of experience and spells a day: 17 levels of 4 bytes, then 17
     * of 8 (49cb:43b6, 69a8:0000). */
    CHECK(T[COK_DATA_EXPERIENCE].index[0].stride == 17 * 4 + 17 * 8);
    CHECK(T[COK_DATA_SPELLS_A_DAY].index[0].stride == T[COK_DATA_EXPERIENCE].index[0].stride);
    CHECK(T[COK_DATA_SPELLS_A_DAY].base + 2 * 8 + 1 ==
          T[COK_DATA_EXPERIENCE].base + 2 * 4 + 17 * 4);
    /* Each table's base in the code that reads it, as an immediate word. */
    static const struct {
        cok_data table;
        unsigned segment, offset, size;
        uint16_t base;
    } bases[] = {
        {COK_DATA_THAC0, 0x69a8, 0x050b, 0x400, 0},
        {COK_DATA_CLASS_BITS, 0x69a8, 0x050b, 0x400, 0},
        {COK_DATA_THIEF_LEVEL, 0x69a8, 0x0b3d, 0x200, 0},
        {COK_DATA_THIEF_RACE, 0x69a8, 0x0b3d, 0x200, 0},
        {COK_DATA_THIEF_DEXTERITY, 0x69a8, 0x0b3d, 0x200, 0},
        {COK_DATA_SAVING_THROWS, 0x69a8, 0x08ff, 0x200, 0},
        {COK_DATA_SPELLS_A_DAY, 0x69a8, 0x0000, 0x500, 0},
        {COK_DATA_SPELLS, 0x69a8, 0x0000, 0x500, 0},
        {COK_DATA_EXPERIENCE, 0x49cb, 0x43b6, 0xa00, 0},
        {COK_DATA_TURNING, 0x5340, 0x119b, 0x300, 0},
        {COK_DATA_SPELL_LISTS, 0x5dc5, 0x0a4e, 0x500, 0},
    };
    for (size_t i = 0; i < sizeof bases / sizeof *bases; ++i) {
        uint16_t base = T[bases[i].table].base;
        const uint8_t *code = at(bases[i].segment, bases[i].offset);
        bool found = false;
        for (unsigned k = 0; k + 1 < bases[i].size && !found; ++k)
            found = code[k] == (base & 0xff) && code[k + 1] == base >> 8;
        CHECK(found);
    }
    /* Names: the classes' and the item words, which lie apart. */
    char name[256];
    CHECK(cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_CLASS_NAMES, 0), name) &&
          strcmp(name, "Cleric") == 0);
    CHECK(cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_ITEM_WORDS, 1), name) &&
          strcmp(name, "Battle Axe") == 0);
    CHECK(cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_ITEM_WORDS, 125), name) &&
          strcmp(name, "Item") == 0);
    CHECK(!cok_data_string(D, 0x4240, name));
    /* The tile sets' first values and bounds (DS:43fa, DS:4404, 70eb:01f5). */
    for (size_t s = 0; s < 5; ++s) {
        CHECK(R->view.tile_first[s] == (ds(0x43fa)[2 * s] | ds(0x43fa)[2 * s + 1] << 8));
        CHECK(R->view.tile_last[s] == (ds(0x4404)[2 * s] | ds(0x4404)[2 * s + 1] << 8));
    }
    /* The wall sets' files by id: 6d21:0ec5's strings (and 70eb:002f's,
     * the same digits) for ids 1, 2, 3-4, 5-6, 7. */
    static const struct {
        uint8_t id;
        unsigned offset;
    } digits[] = {{1, 0}, {2, 2}, {3, 4}, {4, 4}, {5, 6}, {6, 6}, {7, 8}};
    for (size_t i = 0; i < sizeof digits / sizeof *digits; ++i) {
        const uint8_t *s = at(0x6d21, 0x0eb1 + digits[i].offset);
        const uint8_t *t = at(0x70eb, 0x0020 + digits[i].offset);
        CHECK(s[0] == 1 && t[0] == 1 && s[1] == t[1]);
        CHECK(R->archives.wall_files[digits[i].id] == (unsigned)(s[1] - '0'));
    }
    const uint8_t *walldef = at(0x6d21, 0x0ebb), *back = at(0x6d21, 0x0e49);
    CHECK(walldef[0] == 7 && memcmp(walldef + 1, "WALLDEF", 7) == 0);
    CHECK(back[0] == 4 && memcmp(back + 1, "back", 4) == 0);
    /* SPRIT4's ids: the set at 6ca1:006e. */
    const uint8_t *set = at(0x6ca1, 0x006e);
    for (unsigned id = 0; id < 256; ++id)
        CHECK((R->archives.sprite_files[id] == 4) == ((set[id / 8] >> (id % 8) & 1) != 0));
    /* MON's and CPIC's file digit, always 1 (46b2:198c, 7090:016d). */
    CHECK(memcmp(at(0x46b2, 0x198c), "\x01" "1" "\x03" "MON", 6) == 0);
    CHECK(memcmp(at(0x7090, 0x016d), "\x01" "1", 2) == 0);
    CHECK(R->archives.file_digit(2) == '1');
    /* The flags pictures load with: DS:42d0 (the ground's, the backdrops',
     * the cursor's) builds no mask, DS:42d1 (the tile sets') does. */
    CHECK(*ds(0x42d0) == 0 && *ds(0x42d1) == 1 && R->startup.cursor_transparent == -1);
    /* Startup's file names (4f9a:00c6-00db). */
    const uint8_t *names = at(0x4f9a, 0x00c6);
    CHECK(memcmp(names, "\x06" "COMSPR" "\x05" "8x8d1" "\x07" "skygrnd" "\x05" "ITEMS", 27) == 0);
    /* The version string. */
    const char *version = R->text.version;
    bool found = false;
    for (size_t i = 0; i + strlen(version) < exe_size && !found; ++i)
        found = exe[i] == strlen(version) && memcmp(exe + i + 1, version, strlen(version)) == 0;
    CHECK(found);
    test_frames();
    test_opcodes();
    test_variables();
    test_import();
    printf("death knights: %zu tables, tile sets, wall and sprite files, frames, opcodes, "
           "variables and the import checked against build/death-START_FULL.EXE\n",
           D->table_count);
}

/* Wall sets by slot, with the shape's first slot and no tiles moved; tile
 * values by the tile sets' bounds. */
static void test_view(void)
{
    static uint8_t record[3 * COK_VIEW_WALL_SET];
    for (size_t i = 0; i < sizeof record; ++i) record[i] = (uint8_t)(i * 7 + 0x30);
    cok_view view = {.shape = &R->view};
    CHECK(cok_view_set_walls(&view, 0, record, sizeof record) == 3);
    CHECK(memcmp(view.walls, record, sizeof record) == 0); /* nothing moved */
    CHECK(cok_view_set_walls(&view, 1, record, sizeof record) == 0); /* past the sets */
    CHECK(cok_view_set_walls(&view, 1, record, COK_VIEW_WALL_SET) == 1);
    CHECK(memcmp(view.walls + COK_VIEW_WALL_SET, record, COK_VIEW_WALL_SET) == 0);
    CHECK(cok_view_set_walls(&view, 4, record, COK_VIEW_WALL_SET) == 0);
    /* Champions': slot 1 is the first, values from 0x2d moved by slot 2's
     * offset. */
    cok_view champions = {.shape = &cok_rules_champions.view};
    CHECK(cok_view_set_walls(&champions, 0, record, COK_VIEW_WALL_SET) == 0);
    CHECK(cok_view_set_walls(&champions, 2, record, COK_VIEW_WALL_SET) == 1);
    CHECK(champions.walls[COK_VIEW_WALL_SET] == (uint8_t)(record[0] + 0x74 - 0x2e));

    /* A tile set of 256 tiles (a header's count 0xff) and the frame's: each
     * value from its set. */
    cok_picture screen = {0}, tiles = {0}, frame = {0};
    CHECK(cok_picture_create(&screen, 4, 8, 1, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&tiles, 1, 8, 256, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&frame, 1, 8, 45, 0) == COK_PICTURE_OK);
    for (size_t f = 0; f < 256; ++f) memset(tiles.pixels + f * 32, (int)(f & 15) * 0x11, 32);
    for (size_t f = 0; f < 45; ++f) memset(frame.pixels + f * 32, (int)((f + 3) & 15) * 0x11, 32);
    view.tiles[0] = tiles;
    view.tiles[4] = frame;
    cok_view_tile(&screen, &view, 0x4e, 0, 0, false);  /* set 0, tile 0x4e */
    cok_view_tile(&screen, &view, 0x110, 1, 0, false); /* the frame's 0x10 */
    cok_view_tile(&screen, &view, 0x12d, 2, 0, false); /* past the sets: nothing */
    cok_view_tile(&screen, &view, 0xff, 3, 0, false);  /* the 256th */
    CHECK(screen.pixels[0] == 0xee && screen.pixels[4] == 0x33 && screen.pixels[8] == 0);
    CHECK(screen.pixels[12] == 0xff);
    cok_picture_free(&screen);
    cok_picture_free(&tiles);
    cok_picture_free(&frame);

    /* The backdrop (6d21:0177): on a square below 0x80 the ground by day,
     * past hour 13 the night sky over it; on one of 0x80 and up the wall
     * set's; each opaque over the fills. */
    cok_view sky = {.shape = &R->view};
    sky.wrap = false;
    sky.map[0x200 + 5 * 16 + 5] = 0x10;
    sky.map[0x200 + 6 * 16 + 5] = 0x90;
    cok_picture *pictures[] = {&sky.ground[0], &sky.ground[1], &sky.back};
    const int colours[] = {2, 4, 6}, rows[] = {88, 44, 88};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(cok_picture_create(pictures[i], 11, (size_t)rows[i], 1, 0) == COK_PICTURE_OK);
        memset(pictures[i]->pixels, colours[i] * 0x11, pictures[i]->frame_size);
    }
    CHECK(cok_picture_create(&screen, 40, 200, 1, 0) == COK_PICTURE_OK);
    /* The colour at the view's top and bottom row (cells 3 and 13). */
    struct {
        int y, hour, top, bottom;
    } cases[] = {{5, 13, 2, 2}, {5, 14, 4, 2}, {6, 14, 6, 6}};
    for (size_t i = 0; i < 3; ++i) {
        cok_view_backdrop b = {.sky = 11, .ground = 8, .hour = (uint16_t)cases[i].hour};
        cok_view_draw(&screen, &sky, 5, cases[i].y, 0, &b);
        CHECK(screen.pixels[3 * 8 * 160 + 3 * 4] >> 4 == cases[i].top);
        CHECK(screen.pixels[(13 * 8 + 7) * 160 + 3 * 4] >> 4 == cases[i].bottom);
    }
    /* In CGA mode the same (6d21:0177 has no CGA case): the pictures, and
     * without them the fills of the other modes, 44 rows of sky and no line
     * (Champions' 69ea:0184 has one at row 0x3b). */
    screen.cga = true;
    cok_view_backdrop cga = {.sky = 11, .line = 15, .horizon = 7, .ground = 6, .hour = 3};
    cok_view_draw(&screen, &sky, 5, 5, 0, &cga);
    CHECK(screen.pixels[3 * 8 * 160 + 3 * 4] >> 4 == 2);
    cok_view bare = {.shape = &R->view};
    cok_view_draw(&screen, &bare, 5, 5, 0, &cga);
    const uint8_t *row = screen.pixels + (8 + 0x3b) * 160 + 3 * 4; /* the view from row 8 */
    CHECK(row[0] >> 4 == 1 && row[160] >> 4 == 2 && row[3 * 160] >> 4 == 1);
    cok_view_draw(&screen, &champions, 5, 5, 0, &cga);
    CHECK(row[0] >> 4 == 3);
    cok_picture_free(&screen);
    cok_view_free(&sky);
}

/* The frames (110f): synthetic tiles, tile t's bytes 0x40 + t, so each
 * cell shows the tile drawn there (0 where cleared). */
static uint8_t frame_cell(const cok_picture *p, int x, int y)
{
    return p->pixels[(size_t)y * 8 * 160 + (size_t)x * 4];
}

static void test_frame(void)
{
    cok_picture screen = {0}, tiles = {0};
    CHECK(cok_picture_create(&screen, 40, 200, 1, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&tiles, 1, 8, 45, 0) == COK_PICTURE_OK);
    for (size_t t = 0; t < 45; ++t) memset(tiles.pixels + t * 32, (int)(0x40 + t), 32);
    const uint16_t moons[3] = {1, 2, 3};
#define AT(x, y) frame_cell(&screen, x, y)
    memset(screen.pixels, 0x11, screen.frame_size);
    cok_screen_draw(&screen, &tiles, R, COK_SCREEN_ADVENTURE, moons);
    /* Row 0: corners 0 and 1, the moons 0x15 + 1, 0x0d + 2 and 0x11 + 3 between
     * tiles 5 and 6; row 23: 2, 4..., 3; the sides 7. */
    CHECK(AT(0, 0) == 0x40 && AT(39, 0) == 0x41 && AT(1, 0) == 0x44);
    CHECK(AT(7, 0) == 0x45 && AT(8, 0) == 0x40 + 0x16 && AT(9, 0) == 0x46);
    CHECK(AT(19, 0) == 0x40 + 0x0f && AT(30, 0) == 0x40 + 0x14);
    CHECK(AT(0, 23) == 0x42 && AT(20, 23) == 0x44 && AT(39, 23) == 0x43);
    CHECK(AT(0, 1) == 0x47 && AT(39, 22) == 0x47 && AT(0, 16) == 0x4a && AT(39, 16) == 0x4b);
    /* Row 16 a, 4..., b; column 16 8 at its top, 9 at its foot; the view's
     * box: corners 0-3, rows of 4 and columns of 7. */
    CHECK(AT(10, 16) == 0x44 && AT(16, 0) == 0x48 && AT(16, 8) == 0x47 && AT(16, 16) == 0x49);
    CHECK(AT(2, 2) == 0x40 && AT(14, 2) == 0x41 && AT(2, 14) == 0x42 && AT(14, 14) == 0x43);
    CHECK(AT(8, 2) == 0x44 && AT(8, 14) == 0x44 && AT(2, 8) == 0x47 && AT(14, 8) == 0x47);
    CHECK(AT(8, 8) == 0 && AT(20, 20) == 0 && AT(20, 24) == 0x11);
    /* The open frame is the frame; the spell list's row 2. */
    memset(screen.pixels, 0x11, screen.frame_size);
    cok_screen_draw(&screen, &tiles, R, COK_SCREEN_LIST, moons);
    CHECK(AT(0, 2) == 0x4a && AT(5, 2) == 0x44 && AT(39, 2) == 0x4b && AT(0, 15) == 0x47);
    CHECK(AT(0, 3) == 0x47 && AT(16, 16) == 0);
    /* Memorize's: rows 1-16 cleared, 17-22 kept. */
    memset(screen.pixels, 0x11, screen.frame_size);
    cok_screen_draw(&screen, &tiles, R, COK_SCREEN_SPELLS, moons);
    CHECK(AT(5, 10) == 0 && AT(5, 20) == 0x11 && AT(5, 16) == 0x44 && AT(0, 20) == 0x47);
    /* Combat: all but row 24 cleared, row 22 the bottom, column 22. */
    memset(screen.pixels, 0x11, screen.frame_size);
    cok_screen_draw(&screen, &tiles, R, COK_SCREEN_COMBAT, moons);
    CHECK(AT(0, 22) == 0x42 && AT(39, 22) == 0x43 && AT(5, 23) == 0 && AT(0, 23) == 0);
    CHECK(AT(22, 0) == 0x48 && AT(22, 10) == 0x47 && AT(22, 22) == 0x49 && AT(0, 21) == 0x47);
    /* The sheet: rows 8, 16 and 20 inside the sides, which join them;
     * column 19 from row 8 to 20. */
    memset(screen.pixels, 0x11, screen.frame_size);
    cok_screen_draw(&screen, &tiles, R, COK_SCREEN_SHEET, moons);
    CHECK(AT(0, 8) == 0x4a && AT(39, 8) == 0x4b && AT(5, 16) == 0x44 && AT(39, 20) == 0x4b);
    CHECK(AT(0, 9) == 0x47 && AT(19, 8) == 0x48 && AT(19, 16) == 0x4c && AT(19, 20) == 0x49);
    CHECK(AT(19, 12) == 0x47 && AT(19, 21) == 0 && AT(8, 0) == 0x56);
    /* The credits: the frame. */
    memset(screen.pixels, 0x11, screen.frame_size);
    cok_screen_draw(&screen, &tiles, R, COK_SCREEN_CREDITS, moons);
    CHECK(AT(0, 4) == 0x47 && AT(8, 0) == 0x56 && AT(5, 4) == 0);
#undef AT
    cok_picture_free(&screen);
    cok_picture_free(&tiles);
}

int main(void)
{
    test_lookup();
    test_view();
    test_frame();
    struct stat st;
    if (stat("DKKAssets/START.EXE", &st) != 0) {
        puts("death knights: DKKAssets not found; its files' and executable's checks skipped");
    } else {
        test_assets();
        test_executable();
    }
    puts("death knights: ok");
    return 0;
}
