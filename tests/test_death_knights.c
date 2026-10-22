/* Death Knights of Krynn's descriptor (src/rules_death_knights.c): what it
 * holds against the original's bytes in build/death-START_FULL.EXE (make
 * death-merged) and against the files in DKKAssets/; its 3D view's wall
 * sets and tiles (view.c with its shape); and what it leaves out. Without
 * DKKAssets/ the checks of its files and executable are skipped, with a
 * line saying so. */
#include "dax.h"
#include "ecl.h"
#include "adventure.h"
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
         * block's variables by a loop (3c1e:01c1), the shop's by COMBAT
         * (3632:16ec's 0x36c). */
        CHECK(listed || v == COK_VAR_ECL_FILE || v == COK_VAR_LOCALS_GAME || v == COK_VAR_SHOP);
        found += listed;
    }
    CHECK(found == 21);
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

/* Whether the Pascal string at p is text (any case where folded: the
 * original's file names, which DOS takes in any case). */
static bool pascal_is(const uint8_t *p, const char *text, bool folded)
{
    size_t n = strlen(text);
    if (p[0] != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = (char)p[1 + i], b = text[i];
        if (folded && a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (folded && b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return true;
}

/* A text with one %s, as the strings around the name: before at DS:first,
 * after at DS:second (0 for none). */
static bool around(const char *format, unsigned first, unsigned second)
{
    const char *name = strstr(format, "%s");
    if (name == NULL) return false;
    char before[64] = "", after[64] = "";
    snprintf(before, sizeof before, "%.*s", (int)(name - format), format);
    snprintf(after, sizeof after, "%s", name + 2);
    return (first == 0 ? before[0] == '\0' : pascal_is(ds(first), before, false)) &&
           (second == 0 ? after[0] == '\0' : pascal_is(ds(second), after, false));
}

/* The front end's section (start.c, roster.c, camp.c's Save): the start
 * menu's items and words, Drop's, Load's and Save's, Add's, the title's
 * pictures and waits and the credits, a new game's block and the
 * demonstration's, and the saved games' and vault's files and layout,
 * against the original's data segment and code. */
static void test_start(void)
{
    const cok_rules_start *S = &R->start;
    for (unsigned i = 0; i < 13; ++i) CHECK(pascal_is(ds(0x0cf8 + 0x2a * i), S->items[i], false));
    CHECK(pascal_is(ds(0x2fad), S->prompt, false) && pascal_is(ds(0x31d6), S->select, false));
    CHECK(pascal_is(ds(0x3018), S->pick_prompt, false) &&
          pascal_is(ds(0x31d6), S->pick_menu, false));
    CHECK(around(S->drop, 0x130a, 0x1315) && pascal_is(ds(0x1320), S->drop_sure, false));
    CHECK(around(S->dumped, 0, 0x1335) && around(S->farewell, 0, 0x134a) &&
          around(S->relief, 0, 0x135f));
    CHECK(pascal_is(ds(0x124c), S->quit, false) && pascal_is(ds(0x1261), S->unsaved, false));
    CHECK(pascal_is(ds(0x3081), S->load_prompt, false) &&
          pascal_is(ds(0x3096), S->save_prompt, false) &&
          pascal_is(ds(0x2610), S->save_warning, false));
    CHECK(pascal_is(ds(0x306c), S->load_from, false) && pascal_is(ds(0x1393), S->add_from, false) &&
          pascal_is(ds(0x3337), S->from_items, false));
    CHECK(around(S->overwrite, 0x164e, 0x1659) && pascal_is(ds(0x1664), S->new_name, false));
    CHECK(pascal_is(ds(0x13a8), S->add_prompt, false) &&
          pascal_is(ds(0x3356), S->add_select, false) &&
          pascal_is(ds(0x1523), S->rangers, false));
    CHECK(pascal_is(ds(0x16b7), R->text.camp, false));
    CHECK(pascal_is(ds(0x2fb0), R->text.version, false) &&
          pascal_is(ds(0x2fb0), R->text.version_again, false));
    /* The title (35c6:051c): each TITLE record (mov al), the screen
     * cleared before it (14e4:0a37), drawn at the two words pushed before
     * 123c:10fe (x, then y), then waited on (mov ax; push; push cs; call
     * 35c6:0000); the credits whatever was pressed (35c6:026e on both ways
     * out of every wait). */
    const uint8_t *b = at(0x35c6, 0x051c);
    unsigned pictures = 0, ax = 0, pushed[8] = {0}, pushes = 0, credits = 0;
    bool cleared = false, named = false;
    uint8_t record = 0;
    for (unsigned i = 0; i < 0x192; ++i) {
        /* The record: the first mov al after TITLE's name (mov di, 0x516). */
        if (b[i] == 0xbf && b[i + 1] == 0x16 && b[i + 2] == 0x05) named = true;
        if (b[i] == 0xb0 && named) {
            record = b[i + 1];
            named = false;
        }
        if (b[i] == 0x31 && b[i + 1] == 0xc0) ax = 0;
        if (b[i] == 0xb8) ax = (unsigned)(b[i + 1] | b[i + 2] << 8);
        if (b[i] == 0x50) pushed[pushes++ % 8] = ax;
        if (b[i] == 0x9a && b[i + 1] == 0x37 && b[i + 2] == 0x0a && b[i + 3] == 0xe4)
            cleared = true;
        if (b[i] == 0x9a && b[i + 1] == 0xfe && b[i + 2] == 0x10 && b[i + 3] == 0x3c) {
            CHECK(pictures < S->picture_count);
            const cok_rules_title_picture *p = &S->pictures[pictures];
            CHECK(p->record == record && p->clear == cleared);
            CHECK(p->x == pushed[(pushes - 2) % 8] && p->y == pushed[(pushes - 1) % 8]);
            cleared = false;
            ++pictures;
        }
        unsigned target = b[i] == 0x0e && b[i + 1] == 0xe8
                              ? (0x051c + i + 4 + (unsigned)(b[i + 2] | b[i + 3] << 8)) % 0x10000
                              : 1;
        if (target == 0x0000)
            CHECK(pictures > 0 && S->pictures[pictures - 1].seconds == pushed[(pushes - 1) % 8]);
        if (target == 0x026e) ++credits;
    }
    CHECK(pictures == S->picture_count && pictures == 4 && credits == 5 && S->credits_always);
    /* The credits' wait: 10000 seconds' hundredths as a word (35c6:0000's
     * mul, then xor dx, dx). */
    const uint8_t *wait = at(0x35c6, 0x0016);
    CHECK(memcmp(wait, "\xb8\x64\x00\xf7\x66\x06\x31\xd2", 8) == 0);
    const uint8_t *seconds = at(0x35c6, 0x0501); /* mov ax, 10000; push; call 0000 */
    CHECK(seconds[0] == 0xb8 && memcmp(seconds + 3, "\x50\x0e\xe8", 3) == 0);
    CHECK(S->credits_wait == (uint16_t)((unsigned)(seconds[1] | seconds[2] << 8) * 100u));
    /* The credits (35c6:026e): x, y, colour and black pushed, then the
     * string in its code segment. */
    const uint8_t *c = at(0x35c6, 0x026e);
    unsigned lines = 0, values[4] = {0}, count = 0;
    for (unsigned i = 0; i < 0x2ae; ++i) {
        if (c[i] == 0xb0) values[count++ % 4] = c[i + 1];
        if (c[i] == 0xbf && c[i + 3] == 0x0e && count >= 4) {
            unsigned off = (unsigned)(c[i + 1] | c[i + 2] << 8);
            CHECK(lines < S->credit_count);
            const cok_rules_text_line *l = &S->credits[lines];
            CHECK(l->x == values[(count - 4) % 4] && l->y == values[(count - 3) % 4] &&
                  l->fg == values[(count - 2) % 4] && values[(count - 1) % 4] == 0);
            CHECK(pascal_is(at(0x35c6, off), l->text, false));
            count = 0;
            ++lines;
        }
    }
    CHECK(lines == S->credit_count && lines == 20);
    /* A new game's block and the demonstration's file and block
     * (3632:2e06). */
    CHECK(memcmp(at(0x3632, 0x2e75), "\xc6\x06\x6c\xdb", 4) == 0 &&
          *at(0x3632, 0x2e79) == S->first_block);
    CHECK(memcmp(at(0x3632, 0x2e64), "\xc6\x06\x6c\xdb", 4) == 0 &&
          *at(0x3632, 0x2e68) == S->demo_block);
    CHECK(memcmp(at(0x3632, 0x2e69), "\xc6\x06\xa2\x98", 4) == 0 &&
          *at(0x3632, 0x2e6d) == S->demo_file);
    /* The end of a game's wall table (4f9a:08ad): mov [c078 + 4 * entry],
     * ax (0), the slot 1 after it. */
    CHECK(memcmp(at(0x4f9a, 0x0997), "\xa3", 1) == 0 &&
          (*at(0x4f9a, 0x0998) | *at(0x4f9a, 0x0999) << 8) == 0xc078 + 4 * S->reset_wall);
    /* The saved games: savgam (DS:71d9) and .pty (46b2:1ebf, 27c2), the
     * Champions' .dat (1ec4); the layout's wall entries, the save's loop to
     * 3 from 0 (46b2:2b3b), the loader's first alone (c07a, c078 pushed
     * for 6d21:0ec5); the vault's names. */
    CHECK(pascal_is(ds(0x71d9), R->saves.game, true));
    CHECK(pascal_is(at(0x46b2, 0x1ebf), ".PTY", true) &&
          pascal_is(at(0x46b2, 0x27c2), ".PTY", true) &&
          strcmp(R->saves.game_extension, "PTY") == 0);
    CHECK(pascal_is(at(0x46b2, 0x1ec4), ".DAT", true) &&
          strcmp(R->characters.import->from->saves.game_extension, "DAT") == 0);
    CHECK(memcmp(at(0x46b2, 0x2b3b), "\x80\xbe\xb7\xfd", 4) == 0 &&
          *at(0x46b2, 0x2b3f) + 1 == R->saves.walls);
    /* The loader (46b2:1ef0-26e7) loads the wall table's first entry
     * alone (6d21:0ec5 with DS:c078, c07a, 46b2:26bd), sets the mode to 0
     * and leaves the last mode as read (no store to DS:8c4f); outside 3D
     * areas the overland map, picture 0x70 (46b2:26cc). */
    const uint8_t *load = at(0x46b2, 0x1ef0);
    unsigned walls = 0, zeroed = 0, last = 0;
    for (unsigned i = 0; i < 0x7f7; ++i) {
        /* mov al, [c078]; push ax; mov al, [c07a]; push ax; call (the
         * thunk of 6d21:0ec5) */
        walls += memcmp(load + i, "\xa0\x78\xc0\x50\xa0\x7a\xc0\x50\x9a", 9) == 0;
        zeroed += memcmp(load + i, "\xc6\x06\x4e\x8c\x00", 5) == 0;
        last += memcmp(load + i, "\xa2\x4f\x8c", 3) == 0 ||
                memcmp(load + i, "\xc6\x06\x4f\x8c", 4) == 0;
    }
    CHECK(walls == R->saves.walls_restored && zeroed == 1 && last == 0 && R->saves.keep_last_mode);
    CHECK(memcmp(at(0x46b2, 0x26cc), "\xb0", 1) == 0 &&
          *at(0x46b2, 0x26cd) == R->view.overland_picture);
    /* A vault with no file: the word at 0x4c33 (add di, 0x133; Move 2) a
     * hundred times (46b2:2fed). */
    CHECK(memcmp(at(0x46b2, 0x2fed), "\x81\xc7\x33\x01", 4) == 0 &&
          memcmp(at(0x46b2, 0x2ff9), "\xb8\x02\x00", 3) == 0 &&
          memcmp(at(0x46b2, 0x300f), "\xb9\x64\x00", 3) == 0);
    CHECK(R->ecl.vars[COK_VAR_SAVINGS].address == 0x4c33 &&
          R->ecl.vars[COK_VAR_SAVINGS].width == 2);
    char vault[16];
    snprintf(vault, sizeof vault, ".%s", R->saves.vault_extension);
    CHECK(pascal_is(at(0x46b2, 0x2e12), R->saves.vault, true) &&
          pascal_is(at(0x46b2, 0x27f0), R->saves.vault, true) &&
          pascal_is(at(0x46b2, 0x2e18), vault, true) && pascal_is(at(0x46b2, 0x27f6), vault, true));
    CHECK(pascal_is(at(0x46b2, 0x2e05), R->saves.vault_temp, true) &&
          pascal_is(at(0x46b2, 0x27fb), R->saves.vault_temp, true));
    /* The roster's NPC flag by source, words by DS:c468 (DS:57a:
     * Champions' +0xe7, its own +0x3e), and the record sizes (DS:57e). */
    CHECK(*ds(0x57a) == 0xe7 && *ds(0x57c) == 0x3e);
    CHECK(R->saves.roster_flag == COK_FIELD_NPC &&
          cok_field_offset(R->character, COK_FIELD_NPC) == *ds(0x57c) &&
          cok_field_offset(R->characters.import->from->character, COK_FIELD_NPC) == *ds(0x57a));
    CHECK((ds(0x57e)[0] | ds(0x57e)[1] << 8) == R->characters.import->from->character->file_size &&
          (ds(0x57e)[2] | ds(0x57e)[3] << 8) == R->character->file_size);
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
    test_start();
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

/* Whether the bytes of pattern occur in the n bytes from segment:offset. */
static bool holds_bytes(unsigned segment, unsigned offset, size_t n, const uint8_t *pattern,
                        size_t size)
{
    const uint8_t *b = at(segment, offset);
    for (size_t i = 0; i + size <= n; ++i)
        if (memcmp(b + i, pattern, size) == 0) return true;
    return false;
}
#define HOLDS(seg, off, n, ...)                                                                    \
    holds_bytes(seg, off, n, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

/* The routine a far call (9a off seg) at p reaches through the thunk it
 * calls (ea off seg, the merged image's segments from 0x1000), as
 * seg << 16 | off. */
static unsigned long far_target(const uint8_t *p)
{
    unsigned off = (unsigned)(p[1] | p[2] << 8), seg = 0x1000u + (unsigned)(p[3] | p[4] << 8);
    const uint8_t *t = at(seg, off);
    if (t[0] != 0xea) return (unsigned long)seg << 16 | off;
    return (unsigned long)(0x1000u + (unsigned)(t[3] | t[4] << 8)) << 16 |
           (unsigned)(t[1] | t[2] << 8);
}

/* The far calls' routines in the n bytes from segment:offset, in order,
 * at most max; returns how many. */
static size_t far_calls(unsigned segment, unsigned offset, size_t n, unsigned long *out,
                        size_t max)
{
    const uint8_t *b = at(segment, offset);
    size_t found = 0;
    for (size_t i = 0; i + 5 <= n && found < max; ++i)
        if (b[i] == 0x9a) out[found++] = far_target(b + i);
    return found;
}

/* Whether the n bytes from segment:offset make a near call (push cs; call
 * rel16, a far routine's own) to segment:target. */
static bool near_call(unsigned segment, unsigned offset, size_t n, unsigned target)
{
    const uint8_t *b = at(segment, offset);
    for (size_t i = 0; i + 3 <= n; ++i)
        if (b[i] == 0xe8 &&
            ((offset + i + 3 + (size_t)(uint16_t)(b[i + 1] | b[i + 2] << 8)) & 0xffff) == target)
            return true;
    return false;
}

/* The encounter's section (combat) against the original's code: COMBAT's
 * branches in order (3632:16ec: the vault's var 0x4d00 first, the vault's
 * menu, a shop, the temple, the end of combat; the battle, then the end of
 * combat), SETUP MONSTERS' fourth operand into DS:c4cc (3632:038f), the
 * placement's ambush (410b:1aa4, 18c2: bits 0-3 of c4cc, the turn of
 * directions, the distance 1 and the demonstration's 2, the sizes it
 * clears, each record's facing from DS:0356, the cell under a body,
 * 6f03:152e, no status 9), the setup's line and the map's tiles
 * (410b:1e41, 10d7), leadership's tests, chances and words (5340:4acf,
 * 4a25), the battle's routines (3e05:00ec: leadership, setup, the turns'
 * 3e05:08af and 3f63:0000 by 3e05:076c, aging 3e05:0264), and the end of
 * combat's effects (3a37:0506, DS:455d), words (3a37:09b7) and treasure
 * menu (3a37:0eed). */
static void test_combat(void)
{
    const cok_rules_combat *C = &R->combat;
    unsigned long calls[24];
    size_t n = far_calls(0x3632, 0x16ec, 0x1b0, calls, 24);
    CHECK(n >= 6 && C->vault && C->vault_unported == NULL && calls[0] == 0x3948000e &&
          calls[1] == 0x3b8906f3 && calls[2] == 0x399907d3 && calls[3] == 0x3a371318 &&
          R->shops.routine == COK_SHOPS_DEATH_KNIGHTS);
    CHECK(HOLDS(0x3632, 0x16ec, 0x48, 0x26, 0x80, 0xbd, 0x00, 0x02, 0x01)); /* var 0x4d00 */
    CHECK(cok_ecl_var_address(R, COK_VAR_VAULT, 0) == 0x4d00);
    size_t battle = 0;
    while (battle < n && calls[battle] != 0x3e0500ec) ++battle;
    CHECK(battle + 1 < n && calls[battle + 1] == 0x3a371318);
    /* SETUP MONSTERS: mov [c4cc], al. */
    CHECK(HOLDS(0x3632, 0x038f, 0x60, 0xa2, 0xcc, 0xc4));
    /* The placement: mov al, [c4cc]; and al, 0xf; and ax, 3; cmp byte
     * [bp-9], 0x48; mov al, [di+0x356]; mov [es:di+9], al. */
    CHECK(HOLDS(0x410b, 0x1aa4, 0x80, 0xa0, 0xcc, 0xc4, 0x24, 0x0f));
    CHECK(HOLDS(0x410b, 0x1aa4, 0x1a0, 0x25, 0x03, 0x00));
    CHECK(C->ambush && HOLDS(0x410b, 0x1aa4, 0x40, 0x80, 0x7e, 0xf7, C->placement_clears));
    CHECK(HOLDS(0x410b, 0x1aa4, 0x200, 0x8a, 0x85, 0x56, 0x03) &&
          HOLDS(0x410b, 0x1aa4, 0x200, 0x26, 0x88, 0x45, 0x09));
    CHECK(!HOLDS(0x410b, 0x1aa4, 0x390, 0x26, 0x80, 0xbd, 0xc7, 0x00, 0x09) && !C->status_9);
    n = far_calls(0x410b, 0x1aa4, 0x390, calls, 24);
    bool under = false;
    for (size_t i = 0; i < n; ++i) under |= calls[i] == 0x6f03152e;
    CHECK(under && C->body_under);
    /* 18c2: mov byte [bp-4], 1 (no distance, another facing); cmp byte
     * [8c50], 0 then mov byte [bp-4], 2 (the demonstration). */
    CHECK(HOLDS(0x410b, 0x18c2, 0x30, 0xc6, 0x46, 0xfc, C->ambush_distance));
    CHECK(HOLDS(0x410b, 0x18c2, 0x50, 0x80, 0x3e, 0x50, 0x8c, 0x00, 0x74, 0x04, 0xc6, 0x46, 0xfc,
                C->demo_distance));
    /* The setup's line (410b:1e41: DS:2a07), the map's tile counts (10d7:
     * mov al, 0x26 for WILDCOM, 0x19 for DUNGCOM) and the dungeon's floor
     * (0892: FillChar with 0x16). */
    CHECK(pascal_is(ds(0x2a07), C->begins, false));
    CHECK(HOLDS(0x410b, 0x10d7, 0x70, 0xb0, C->wilderness_tiles) &&
          HOLDS(0x410b, 0x10d7, 0x70, 0xb0, C->dungeon_tiles));
    CHECK(C->dungeon_fill && HOLDS(0x410b, 0x0892, 0x20, 0xb0, C->dungeon_floor));
    CHECK(C->wilderness == COK_WILDERNESS_DEATH_KNIGHTS && C->tiles_kept && C->door_corners);
    CHECK(R->archives.random == NULL);
    /* The river's variable: 410b:0a4c's cmp byte [es:di+0xf3], 0 on the
     * 0x4b00 block. */
    CHECK(HOLDS(0x410b, 0x0a4c, 0x10, 0x26, 0x80, 0xbd, 0xf3, 0x00, 0x00) &&
          cok_ecl_var_address(R, COK_VAR_RIVER, 0) == 0x4bf3);
    /* Leadership: its words in its code, 5340:4aa9, 4ac5, 4acd; the side,
     * the NPC flag, the knight's level (+0x4c), okay, the computer's and
     * intelligence above 3; the chances 20, 40, 80 and charisma above 13. */
    CHECK(C->leadership && pascal_is(at(0x5340, 0x4aa9), C->leadership_text, false) &&
          pascal_is(at(0x5340, 0x4ac5), C->leads, false) &&
          pascal_is(at(0x5340, 0x4acd), C->leads_end, false));
    CHECK(HOLDS(0x5340, 0x4acf, 0x100, 0x26, 0x80, 0x7d, 0x4c, 0x00) &&
          HOLDS(0x5340, 0x4acf, 0x120, 0x26, 0x80, 0x7d, 0x13, 0x03) &&
          HOLDS(0x5340, 0x4acf, 0x60, 0x26, 0xc6, 0x85, 0xca, 0x00, 0x01) &&
          HOLDS(0x5340, 0x4acf, 0x1f0, 0xbf, 0x68, 0x08)); /* DS:0868 */
    CHECK(pascal_is(ds(0x0868), C->results_key, false));
    CHECK(HOLDS(0x5340, 0x4a25, 0x30, 0xc6, 0x46, 0xfd, 0x14) &&
          HOLDS(0x5340, 0x4a25, 0x30, 0xc6, 0x46, 0xfd, 0x28) &&
          HOLDS(0x5340, 0x4a25, 0x30, 0xc6, 0x46, 0xfd, 0x50) &&
          HOLDS(0x5340, 0x4a25, 0x40, 0x26, 0x80, 0x7d, 0x1b, 0x0d));
    /* The battle: leadership, then the setup (3e05:00ec); the turn's
     * routines (3e05:076c's near call to 08af, far to 3f63:0000), aging
     * (00ec's near call to 0264). */
    n = far_calls(0x3e05, 0x00ec, 0x60, calls, 24);
    CHECK(n >= 2 && calls[0] == 0x53404acf && calls[1] == 0x410b1e41);
    n = far_calls(0x3e05, 0x076c, 0x143, calls, 24);
    bool computer = false;
    for (size_t i = 0; i < n; ++i) computer |= calls[i] == 0x3f630000;
    CHECK(computer && near_call(0x3e05, 0x076c, 0x143, 0x08af));
    CHECK(HOLDS(0x3e05, 0x0248, 0x0a, 0x80, 0x3e, 0x63, 0xc4, 0x00) &&
          near_call(0x3e05, 0x0248, 0x0c, 0x0264)); /* cmp byte [c463], 0; call 0264 */
    CHECK(!C->explode && C->ends_in_round);
    /* The end of combat: the effects (DS:455d, 19), its words and the
     * treasure menu it reaches only with anything in the pool. */
    for (size_t i = 0; i < 19; ++i) CHECK(C->combat_effects[i] == *ds(0x455d + (unsigned)i));
    CHECK(C->combat_effects[19] == 0);
    /* mov al, [di+0x455c]; the loop's cmp byte [bp-6], 0x13. */
    CHECK(HOLDS(0x3a37, 0x0506, 0x200, 0x8a, 0x85, 0x5c, 0x45) &&
          HOLDS(0x3a37, 0x0506, 0x200, 0x80, 0x7e, 0xfa, 0x13));
    CHECK(pascal_is(ds(0x17fa), C->fled, false) && pascal_is(ds(0x180f), C->lost, false) &&
          pascal_is(ds(0x184d), C->won, false) && pascal_is(ds(0x18b4), C->found, false) &&
          pascal_is(ds(0x191b), C->receives, false) && pascal_is(ds(0x193a), C->points, false) &&
          pascal_is(ds(0x18d3), C->found_none, false) && C->withdrawal == NULL);
    CHECK(pascal_is(ds(0x2b9c), C->rejoice, false) && pascal_is(ds(0x2681), C->rejoice_key, false));
    /* The adventure's loop (3632:2e06): the load vector (push word
     * [DS:8c46]; call 2cd5), then the menu (4307:0257) before the first
     * test of DS:8c5a (cmp byte [8c5a], 0 at 2fa5): a party destroyed
     * there still gets the menu once. */
    {
        unsigned long loop[16];
        size_t k = far_calls(0x3632, 0x2eb4, 0x2fa5 - 0x2eb4, loop, 16);
        bool menu = false;
        for (size_t i = 0; i < k; ++i) menu |= loop[i] == 0x43070257;
        CHECK(C->killed_command && menu && HOLDS(0x3632, 0x2eb4, 8, 0xff, 0x36, 0x46, 0x8c) &&
              near_call(0x3632, 0x2eb4, 8, 0x2cd5) &&
              !HOLDS(0x3632, 0x2e06, 0x2fa5 - 0x2e06, 0x80, 0x3e, 0x5a, 0x8c) &&
              HOLDS(0x3632, 0x2fa5, 5, 0x80, 0x3e, 0x5a, 0x8c, 0x00));
    }
    CHECK(C->treasure_if_any && C->treasure_unported == NULL &&
          near_call(0x3a37, 0x1318, 0x200, 0x0eed));
    CHECK(!C->weapons_back && !C->shares && C->may_train && R->characters.paladin_row == 10);
    /* The paladin's experience row 10 (65cd:382c: cmp al, 3 then mov byte
     * [bp-2], 0x0a), the highest levels' table (65cd:38c7's [di-0x7d2c]:
     * DS:82d4). */
    CHECK(HOLDS(0x65cd, 0x382c, 0xa0, 0x3c, 0x03, 0x75, 0x04, 0xc6, 0x46, 0xfe,
                R->characters.paladin_row));
    CHECK(HOLDS(0x65cd, 0x38c7, 0xf0, 0x8a, 0x85, 0xd4, 0x82) &&
          cok_data_offset2(R->data, COK_DATA_LEVEL_LIMITS, 0, 0) == 0x82d4 &&
          cok_field_offset(R->character, COK_FIELD_MAY_TRAIN) == 0xc6 &&
          HOLDS(0x3a37, 0x02fa, 0x200, 0x26, 0x88, 0x85, 0xc6, 0x00));
    /* Who may use spells (5dc5:0078): cmp byte [es:di+0x48], 8 (paladins
     * from 9), [+0x49], 7 (rangers from 8), [+0x4a], 0 (a magic-user). */
    const cok_rules_spells *S = &R->spells;
    CHECK(HOLDS(0x5dc5, 0x0078, 0x60, 0x26, 0x80, 0x7d, 0x48, (uint8_t)(S->usable_paladin - 1)) &&
          HOLDS(0x5dc5, 0x0078, 0x90, 0x26, 0x80, 0x7d, 0x49, (uint8_t)(S->usable_ranger - 1)) &&
          HOLDS(0x5dc5, 0x0078, 0xe0, 0x26, 0x80, 0x7d, 0x4a, 0x00) && S->usable_mage_class);
    /* The fighters' classes: 638c:15e0's cmp byte [bp+0xa], 3 (a paladin's
     * hit points), 638c:16d8's cmp byte [es:di+0x48], 0 (exceptional
     * strength). */
    CHECK(R->characters.fighter_classes == 0x9c &&
          HOLDS(0x638c, 0x15e0, 0x40, 0x80, 0x7e, 0x0a, 0x03) &&
          HOLDS(0x638c, 0x16d8, 0x300, 0x26, 0x80, 0x7d, 0x48, 0x00));
    puts("death knights: the encounter's and the fight's routines checked");
}

/* The fight's section against the original's code: its words where the
 * routines read them, each stop's routine and where it is called, the
 * probe's tiles, the computer's turn (the target first, the limit and the
 * paladins, the items' die, the spell's columns, the reach, morale, the
 * flight's turn and the ways' guard), the attack's sides, the free
 * attacks' effect, the quiet bit, the forgotten spell's mark, the arrows,
 * the missile's clamp, turning undead (5340:119b: the paladin's level less
 * 2, deity 2, 8 to 9 below 14 else 10, var 0x4cff, rows of 11 from
 * DS:05a5, 1d6 + 6 or 1d12) and the saving throw's effect 0x39. */
static void test_fight(void)
{
    const cok_rules_fight *F = &R->fight;
    const cok_rules_fight_words *W = &F->words;
    const struct {
        unsigned seg, off;
        const char *word;
    } words[] = {
        {0, 0x2d37, W->attacks}, {0, 0x2d0d, W->backstabs}, {0, 0x2d22, W->slays},
        {0, 0x2d42, W->behind}, {0x5340, 0x01ef, W->cruel}, {0x5340, 0x0203, W->hitting},
        {0, 0x2d6c, W->point}, {0, 0x2d77, W->points}, {0, 0x2d82, W->damage},
        {0, 0x2d8d, W->misses}, {0, 0x1c3a, W->lost_spell}, {0, 0x1d04, W->goes_down},
        {0, 0x1cef, W->dying}, {0, 0x1ce4, W->killed}, {0, 0x2d98, W->got_away},
        {0, 0x2da3, W->blocked}, {0, 0x2de3, W->destroyed}, {0, 0x2df8, W->nothing},
        {0, 0x2b3e, W->panic}, {0, 0x2b68, W->surrenders}, {0, 0x16f7, W->magic_on},
        {0, 0x1702, W->magic_off}, {0, 0x2517, W->teammate_dying}, {0, 0x3129, W->continue_battle},
        {0, 0x2cf8, W->bandaged}, {0, 0x2c2e, W->hit_points}, {0, 0x2c44, W->helpless},
        {0, 0x2c4f, W->casting}, {0, 0x2cc2, W->guarding}, {0, 0x1d0f, R->effects.unaffected},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i)
        CHECK(pascal_is(words[i].seg != 0 ? at(words[i].seg, words[i].off) : ds(words[i].off),
                        words[i].word, false));
    CHECK(W->gods_refused == NULL);
    /* The clouds: aged at the round's end (3e05:0ed0's far calls, 21ec
     * before the cloud square), their words where 0c65 and 21ec load them
     * (mov di, offset), the levels Cloud Kill kills at (cmp al, 4, 5, 6;
     * the throw's -4), its effect 0x37 and status 6; the selection of
     * actions shown (DS:c4d4) and the undead's byte; the rounds (cmp
     * [es:di+0x1c], 1 and 0xff, dec), the body's tile, the cover (213c);
     * Dispel's stale byte DS:8954 (2dd7). */
    unsigned long calls[24];
    size_t n = far_calls(0x3e05, 0x0ed0, 0x60, calls, 24);
    size_t aged = n, square = n;
    for (size_t i = 0; i < n; ++i) {
        if (calls[i] == 0x638c21ec && aged == n) aged = i;
        if (calls[i] == 0x638c0c65 && square == n) square = i;
    }
    CHECK(F->clouds == COK_CLOUDS_DEATH_KNIGHTS && aged < square && square < n);
    CHECK(pascal_is(ds(0x1c06), F->cough, false) && pascal_is(ds(0x1c1b), F->choke, false) &&
          pascal_is(ds(0x1d4e), F->clears, false) && pascal_is(ds(0x1ccf), F->poisoned, false) &&
          F->noxious == NULL && HOLDS(0x638c, 0x0c65, 0x120, 0xbf, 0x06, 0x1c) &&
          HOLDS(0x638c, 0x0c65, 0x200, 0xbf, 0x1b, 0x1c) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0xbf, 0xcf, 0x1c) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0xbf, 0xe4, 0x1c) &&
          HOLDS(0x638c, 0x0c65, 0x200, 0xbf, 0x3a, 0x1c) &&
          HOLDS(0x638c, 0x21ec, 0x210, 0xbf, 0x4e, 0x1d));
    CHECK(HOLDS(0x638c, 0x0c65, 0x340, 0x26, 0x8a, 0x45, 0x30, 0x3c, 0x00, 0x7c) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0x3c, 0x04, 0x7f) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0x3c, 0x05, 0x75) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0x3c, 0x06, 0x75) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0xb0, 0x00, 0x50, 0xb0, 0xfc, 0x50) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0xb0, 0x37, 0x50) &&
          HOLDS(0x638c, 0x0c65, 0x340, 0xb0, 0x06, 0x50, 0xbf, 0xe4, 0x1c) &&
          cok_effect_id(R, COK_EFFECT_POISONED) == 0x37);
    CHECK(HOLDS(0x638c, 0x0c65, 0x80, 0x80, 0x3e, 0xd4, 0xc4, 0x00) &&
          HOLDS(0x638c, 0x0c65, 0x100, 0x26, 0x80, 0x7d, 0x31, 0x00) &&
          HOLDS(0x638c, 0x0c65, 0x100, 0xb0, 0x1f, 0x50) &&
          HOLDS(0x638c, 0x0c65, 0x100, 0xb0, 0x63, 0x50) &&
          cok_effect_id(R, COK_EFFECT_HELPLESS) == 0x1f &&
          cok_effect_id(R, COK_EFFECT_POISON_IMMUNE) == 0x63 &&
          cok_effect_id(R, COK_EFFECT_NAUSEA) == 0x1e);
    CHECK(HOLDS(0x638c, 0x21ec, 0x60, 0x26, 0x80, 0x7d, 0x1c, 0xff) &&
          HOLDS(0x638c, 0x21ec, 0x60, 0x26, 0x80, 0x7d, 0x1c, 0x01, 0xb0, 0x00, 0x7f) &&
          HOLDS(0x638c, 0x21ec, 0x210, 0x26, 0xfe, 0x4d, 0x1c) &&
          HOLDS(0x638c, 0x21ec, 0x210, 0x26, 0xc6, 0x45, 0x07, 0x1f) &&
          near_call(0x638c, 0x21ec, 0x210, 0x213c) &&
          HOLDS(0x638c, 0x213c, 0x40, 0x26, 0x8a, 0x5d, 0x1e) &&
          HOLDS(0x638c, 0x213c, 0x60, 0x8a, 0x85, 0x43, 0x58) &&
          cok_data_offset(R->data, COK_DATA_CLOUD_CELLS, 1) == 0x5844);
    /* The probe (6f03:0e33): cmp byte [bp-4], 0x1d and 0x1c before the
     * terrain's cost; the death's set at 6f03:0f9d, 0x1c and 0x1d. */
    CHECK(F->cloud_tile == 0x1d && F->pool_tile == 0x1c && F->clouds_terrain &&
          HOLDS(0x6f03, 0x0e33, 0x120, 0x80, 0x7e, 0xfc, 0x1d) &&
          HOLDS(0x6f03, 0x0e33, 0x120, 0x80, 0x7e, 0xfc, 0x1c));
    CHECK(F->body_spares_pool && at(0x6f03, 0x0f9d)[3] == 0x30 && F->kill_finds_body &&
          !F->kill_clears_spell);
    /* The computer's turn: its first far call picks a target (5340:3cac);
     * 0251's mov byte [bp-6], 0x0d and cmp byte [es:di+0x48], 2; the
     * spell's columns (05d7: DS:6a8d, the table's + 13) and the levels
     * (+0x47, +0x4c, +0x48, +0x49); 0ccc's range byte (type + 12) not less
     * 1; morale's jl; the flight's inc ax (08c6). */
    n = far_calls(0x3f63, 0x0000, 0x40, calls, 24);
    CHECK(F->computer_target_first && n >= 1 && calls[0] == 0x53403cac);
    CHECK(F->turn_limit == 0x0d && HOLDS(0x3f63, 0x0251, 0x10, 0xc6, 0x46, 0xfa, 0x0d));
    CHECK(F->paladin_turns == 2 && HOLDS(0x3f63, 0x0251, 0x40, 0x26, 0x80, 0x7d, 0x48, 0x02));
    CHECK(!F->computer_items && F->computer_cast == COK_COMPUTER_CAST_DEATH_KNIGHTS);
    CHECK(cok_data_offset2(R->data, COK_DATA_SPELLS, 0, 13) == 0x6a8d &&
          cok_data_offset2(R->data, COK_DATA_SPELLS, 0, 11) == 0x6a8b &&
          HOLDS(0x3f63, 0x05d7, 0xe0, 0x8a, 0x85, 0x8d, 0x6a) &&
          HOLDS(0x3f63, 0x03ee, 0x80, 0x80, 0xbd, 0x8b, 0x6a, 0x00) &&
          HOLDS(0x3f63, 0x05d7, 0x90, 0x26, 0x8a, 0x45, 0x47) &&
          HOLDS(0x3f63, 0x05d7, 0x90, 0x26, 0x8a, 0x45, 0x48) &&
          HOLDS(0x3f63, 0x05d7, 0x90, 0x26, 0x8a, 0x45, 0x49) &&
          HOLDS(0x3f63, 0x05d7, 0x90, 0x26, 0x8a, 0x45, 0x4c));
    CHECK(F->reach_less == 0 &&
          HOLDS(0x3f63, 0x0ccc, 0x180, 0x8a, 0x85, 0xb2, 0x9a, 0x88, 0x46, 0xfc));
    CHECK(F->morale_signed && !F->flee_removes &&
          HOLDS(0x3f63, 0x1260, 0x120, 0x3b, 0xc2, 0x7c));
    CHECK(F->flee_turn == 1 && HOLDS(0x3f63, 0x0a19, 0x10, 0x2b, 0xc2, 0x40, 0x88, 0x46, 0xff));
    CHECK(F->ways_guarded && HOLDS(0x3f63, 0x0ad7, 0x18, 0x26, 0x80, 0x7d, 0x15, 0x07));
    /* The attack's sides (5340:10ab: the attacker into DS:a2c2, the sides
     * compared); free attacks (5340:0860, effect 6); the quiet bit
     * (5340:0230: test of +0x7b with 8); the mark (65cd:17c4: or al, 0x80);
     * arrows (5340:28a0: cmp al, 0x42); the clamp (65cd:1d56: 0x31, 0x18). */
    CHECK(F->attack_sides && HOLDS(0x5340, 0x10ab, 0x50, 0xa3, 0xc2, 0xa2) &&
          HOLDS(0x5340, 0x10ab, 0x90, 0x26, 0x8a, 0x85, 0xc9, 0x00));
    CHECK(F->free_attack_effect == 6 && HOLDS(0x5340, 0x0860, 0xd0, 0xb0, 0x06, 0x50));
    CHECK(F->quiet_flag == 8 && HOLDS(0x5340, 0x0230, 0x2a0, 0x26, 0x8a, 0x45, 0x7b, 0x24, 0x08));
    CHECK(F->forget_mark == 0x80 && HOLDS(0x65cd, 0x17c4, 0x50, 0x0c, 0x80));
    bool found42 = false;
    for (const uint8_t *a = F->arrows; *a != 0; ++a) found42 |= *a == 0x42;
    CHECK(found42 && HOLDS(0x5340, 0x28a0, 0x60, 0x3c, 0x42));
    CHECK(F->missile_clamped && HOLDS(0x65cd, 0x1d56, 0x50, 0x26, 0xc7, 0x05, 0x31, 0x00) &&
          HOLDS(0x65cd, 0x1d56, 0x50, 0x26, 0xc7, 0x05, 0x18, 0x00));
    /* Turning undead. */
    CHECK(F->turning == COK_TURNING_DEATH_KNIGHTS);
    CHECK(HOLDS(0x5340, 0x119b, 0x80, 0x26, 0x8a, 0x45, 0x48, 0x98, 0x48, 0x48) &&
          HOLDS(0x5340, 0x119b, 0x90, 0x26, 0x80, 0x7d, 0x23, 0x02) &&
          HOLDS(0x5340, 0x119b, 0xa0, 0x80, 0x7e, 0xf5, 0x08) &&
          HOLDS(0x5340, 0x119b, 0xa0, 0x80, 0x7e, 0xf5, 0x0e) &&
          HOLDS(0x5340, 0x119b, 0xa0, 0xc6, 0x46, 0xf5, 0x09) &&
          HOLDS(0x5340, 0x119b, 0xa0, 0xc6, 0x46, 0xf5, 0x0a));
    CHECK(HOLDS(0x5340, 0x119b, 0xc0, 0x26, 0x8a, 0x85, 0xff, 0x01) &&
          cok_ecl_var_address(R, COK_VAR_TURNING, 0) == 0x4cff);
    CHECK(HOLDS(0x5340, 0x119b, 0x120, 0xba, 0x0b, 0x00) &&
          HOLDS(0x5340, 0x119b, 0x120, 0x8a, 0x85, 0xa5, 0x05) &&
          cok_data_offset2(R->data, COK_DATA_TURNING, 0, 0) == 0x05a5 &&
          cok_data_offset2(R->data, COK_DATA_TURNING, 1, 0) == 0x05a5 + 11);
    CHECK(HOLDS(0x5340, 0x119b, 0x160, 0xb0, 0x06, 0x50, 0x9a) &&
          HOLDS(0x5340, 0x119b, 0x160, 0x05, 0x06, 0x00) &&
          HOLDS(0x5340, 0x119b, 0x160, 0xb0, 0x0c, 0x50, 0x9a));
    /* The saving throw (638c:10f8: effect 0x39 makes the roll 1). */
    CHECK(R->effects.save_fail_id == 0x39 &&
          HOLDS(0x638c, 0x10f8, 0x40, 0xb0, 0x39, 0x50) &&
          HOLDS(0x638c, 0x10f8, 0x40, 0xc6, 0x06, 0x6a, 0xbe, 0x01));
}

/* The casting section against the original's code: the cast entry's
 * words and questions (5dc5:1298: mov di, DS:x; Yes first, mov al,
 * [57c0]), no miscast (no call of 65cd:2687), its bursts (cmp al, spell
 * ... mov al, n; push ax; mov al, slot; push ax; mov al, [4316]); the
 * targets cleared (5340:1ff7: mov di, c31d ... mov ax, 0x120), Silence's
 * radius (mov cx, 4; shr ax, cl), the computer's pick (5340:1eeb's near
 * calls of 1da5 and 44f8; 44f8's rows at DS:0648, its tie's 65cd:0000 with
 * 0x19); sided (5dc5:1cc6 tests no spell), Charm Monsters' side
 * (5dc5:1b20: cmp byte [be70], 0x51; mov cl, 7; shl ax, cl), the bolt
 * (5dc5:354a: the length doubled, at least 2 a line), magic resistance
 * (638c:04c9: jnz after the pending test), the damage's words and quiet
 * bit (638c:1c52), cure's (638c:1479), Cast's refusals (4381:07ff), band
 * A's dice (5dc5:4160, 48ef) and Enlarge (2100 has no cmp al, 0x0a), and
 * the load's strength (46b2:10d7: 65cd:0e46, 638c:16d8 with 0, 69a8:050b). */
static void test_casting(void)
{
    const cok_rules_casting *K = &R->casting;
    static const struct {
        unsigned seg, off, n, at;
        const char *const *text;
    } words[] = {
        {0x5dc5, 0x1298, 0x370, 0x0f25, &R->casting.cast_here},
        {0x5dc5, 0x1298, 0x370, 0x0f1a, &R->casting.lose_it},
        {0x5dc5, 0x1298, 0x370, 0x0f44, &R->casting.that_item},
        {0x5dc5, 0x1298, 0x370, 0x0f4f, &R->casting.combat_only},
        {0x5dc5, 0x1298, 0x370, 0x0f6e, &R->casting.use_it},
        {0x5dc5, 0x1298, 0x370, 0x0f84, &R->casting.abort},
        {0x5dc5, 0x1298, 0x370, 0x0f99, &R->casting.aborted},
        {0x5dc5, 0x1182, 0x100, 0x3192, &R->casting.on_whom},
        {0x5dc5, 0x552a, 0x100, 0x2304, &R->casting.casts_a_spell},
        {0x638c, 0x1c52, 0x340, 0x1c4f, &R->casting.takes},
        {0x638c, 0x1c52, 0x340, 0x1c5a, &R->casting.points},
        {0x638c, 0x1c52, 0x340, 0x1c6f, &R->casting.point},
        {0x638c, 0x1c52, 0x340, 0x1c8e, &R->casting.from[0]},
        {0x638c, 0x1c52, 0x340, 0x1c99, &R->casting.from[1]},
        {0x638c, 0x1c52, 0x340, 0x1ca4, &R->casting.from[2]},
        {0x638c, 0x1c52, 0x340, 0x1cb9, &R->casting.from[3]},
        {0x638c, 0x1c52, 0x340, 0x1cc4, &R->casting.from[4]},
        {0x638c, 0x1c52, 0x340, 0x1c3a, &R->casting.lost_spell},
        {0x638c, 0x1c52, 0x340, 0x1d04, &R->casting.goes_down},
        {0x638c, 0x1c52, 0x340, 0x1cef, &R->casting.dying},
        {0x638c, 0x1c52, 0x340, 0x1ce4, &R->casting.killed},
        {0x638c, 0x1479, 0x40, 0x19f3, &R->casting.cured},
        {0x4381, 0x07ff, 0x120, 0x2e0d, &R->casting.no_area},
        {0x4381, 0x07ff, 0x120, 0x2e2c, &R->casting.no_condition},
        {0x4381, 0x07ff, 0x120, 0x2e41, &R->casting.no_what[0]},
        {0x4381, 0x07ff, 0x120, 0x2e56, &R->casting.no_what[1]},
        {0x4381, 0x07ff, 0x120, 0x2e6b, &R->casting.no_what[2]},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i)
        CHECK(pascal_is(ds(words[i].at), *words[i].text, false) &&
              HOLDS(words[i].seg, words[i].off, words[i].n, 0xbf, (uint8_t)words[i].at,
                    (uint8_t)(words[i].at >> 8)));
    CHECK(K->ask_yes && *ds(0x57c0) == 0 && HOLDS(0x5dc5, 0x1298, 0x370, 0xa0, 0xc0, 0x57));
    unsigned long calls[64];
    size_t n = far_calls(0x5dc5, 0x1298, 0x370, calls, 64);
    bool miscast = false;
    for (size_t i = 0; i < n; ++i) miscast |= calls[i] == 0x65cd2687;
    CHECK(!K->miscast && !miscast);
    CHECK(K->burst_count == 6 && K->burst_sound == *ds(0x4316) && K->burst_sound == 0x0b);
    for (unsigned i = 0; i < K->burst_count; ++i) {
        const cok_rules_burst *b = &K->bursts[i];
        CHECK(HOLDS(0x5dc5, 0x1298, 0x370, 0x3c, b->spell));
        CHECK(HOLDS(0x5dc5, 0x1298, 0x370, 0xb0, b->n, 0x50, 0xb0, b->slot, 0x50, 0xa0, 0x16,
                    0x43) &&
              HOLDS(0x5dc5, 0x1298, 0x370, 0xb0, b->n_3d, 0x50, 0xb0, b->slot, 0x50, 0xa0, 0x16,
                    0x43));
    }
    CHECK(K->targets_cleared && HOLDS(0x5340, 0x1ff7, 0x20, 0xbf, 0x1d, 0xc3, 0x1e, 0x57, 0xb8,
                                      0x20, 0x01));
    CHECK(K->silence_radius && HOLDS(0x5340, 0x1ff7, 0x580, 0xb9, 0x04, 0x00, 0xd3, 0xe8));
    CHECK(K->computer_pick == COK_CAST_PICK_DEATH_KNIGHTS && near_call(0x5340, 0x1eeb, 0x100, 0x1da5) &&
          near_call(0x5340, 0x1eeb, 0x100, 0x44f8));
    CHECK(HOLDS(0x5340, 0x44f8, 0x530, 0x8a, 0x85, 0x48, 0x06) &&
          cok_data_offset2(R->data, COK_DATA_AREA_ROWS, 0, 0) == 0x0648 &&
          HOLDS(0x5340, 0x44f8, 0x530, 0xb0, 0x19, 0x50, 0x9a));
    CHECK(K->sided_all && !HOLDS(0x5dc5, 0x1cc6, 0xb0, 0x80, 0x3e, 0x70, 0xbe));
    CHECK(K->missiles_charm_side && HOLDS(0x5dc5, 0x1b20, 0x1a8, 0x80, 0x3e, 0x70, 0xbe, 0x51) &&
          HOLDS(0x5dc5, 0x1b20, 0x1a8, 0xb1, 0x07, 0xd3, 0xe0));
    CHECK(K->bolt_doubles &&
          HOLDS(0x5dc5, 0x354a, 0x2b0, 0x8a, 0x46, 0xfe, 0x30, 0xe4, 0xd1, 0xe0, 0x88, 0x46, 0xfe));
    CHECK(K->bolt_least == 2 && HOLDS(0x5dc5, 0x354a, 0x2b0, 0x83, 0x7e, 0xc2, 0x02));
    CHECK(K->resist_damage && HOLDS(0x638c, 0x04c9, 0x30, 0x80, 0x3e, 0x6b, 0xbe, 0x00, 0x75));
    CHECK(R->fight.quiet_flag == 8 &&
          HOLDS(0x638c, 0x1c52, 0x340, 0x26, 0x8a, 0x45, 0x7b, 0x24, 0x08, 0x08, 0xc0, 0x75));
    /* Band A. */
    const cok_rules_cast *flame = &R->spells.cast[0x4a], *ice = &R->spells.cast[0x57];
    CHECK(flame->count == 6 && flame->sides == 8 && flame->type == 9 &&
          HOLDS(0x5dc5, 0x4160, 0x30, 0xb0, 0x06, 0x50, 0xb0, 0x08, 0x50) &&
          HOLDS(0x5dc5, 0x4160, 0x30, 0xb0, 0x09, 0x50));
    CHECK(ice->count == 3 && ice->sides == 10 && ice->type == 8 &&
          HOLDS(0x5dc5, 0x48ef, 0x30, 0xb0, 0x03, 0x50, 0xb0, 0x0a, 0x50) &&
          HOLDS(0x5dc5, 0x48ef, 0x30, 0xb0, 0x08, 0x50));
    CHECK(R->spells.cast[0x0c].value == 1 && HOLDS(0x5dc5, 0x2100, 0x90, 0x3c, 0x09) &&
          !HOLDS(0x5dc5, 0x2100, 0x90, 0x3c, 0x0a));
    n = far_calls(0x5dc5, 0x37f8, 0x50, calls, 64);
    bool counted = false;
    for (size_t i = 0; i < n; ++i) counted |= calls[i] == 0x638c1233;
    CHECK(R->spells.cast[0x33].value == 1 && counted);
    /* The load: stats, strength (with 0), levels. */
    n = far_calls(0x46b2, 0x1960, 0x30, calls, 64);
    CHECK(K->load_strength && n == 3 && calls[0] == 0x65cd0e46 && calls[1] == 0x638c16d8 &&
          calls[2] == 0x69a8050b && HOLDS(0x46b2, 0x1970, 0x08, 0xb0, 0x00, 0x50, 0x9a));
    puts("death knights: the casting section checked");
}

/* The band B spells (5dc5's handlers) against the original's code: each
 * entry's words, the strings its routine loads (mov di, DS offset), one or
 * two run together; and the constants their kinds take (cok_rules_cast,
 * casting's cone, Fire Shield and Dispel Magic). */
static void test_spells(void)
{
    static const struct {
        uint8_t id;
        unsigned at, second, n;
    } words[] = {
        {0x02, 0x4570, 0x4caf, 0x40}, {0x18, 0x204c, 0, 0x20}, {0x2b, 0x20f4, 0, 0x40},
        {0x59, 0x20f4, 0, 0x40},      {0x68, 0x20f4, 0, 0x40}, {0x2c, 0x2128, 0, 0x20},
        {0x64, 0x2128, 0, 0x20},      {0x30, 0x2152, 0, 0x20}, {0x37, 0x217c, 0, 0x20},
        {0x39, 0x2152, 0, 0x30},      {0x3e, 0x1f30, 0, 0x40}, {0x63, 0x1f30, 0, 0x40},
        {0x4f, 0x2244, 0, 0x10},      {0x54, 0x1e5d, 0, 0x130}, {0x55, 0x4570, 0x4594, 0x170},
        {0x62, 0x2299, 0, 0x20},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i) {
        const cok_rules_cast *h = &R->spells.cast[words[i].id];
        const uint8_t *first = ds(words[i].at);
        char text[128];
        snprintf(text, sizeof text, "%.*s", first[0], (const char *)first + 1);
        if (words[i].second != 0) {
            const uint8_t *second = ds(words[i].second);
            snprintf(text + strlen(text), sizeof text - strlen(text), "%.*s", second[0],
                     (const char *)second + 1);
        }
        CHECK(h->unported == NULL && h->text != NULL && strcmp(h->text, text) == 0);
        CHECK(HOLDS(0x5dc5, h->address, words[i].n, 0xbf, (uint8_t)words[i].at,
                    (uint8_t)(words[i].at >> 8)));
        if (words[i].second != 0)
            CHECK(HOLDS(0x5dc5, h->address, words[i].n, 0xbf, (uint8_t)words[i].second,
                        (uint8_t)(words[i].second >> 8)));
    }
    /* Bless's "is " and "Blessed" are Champions' text. */
    CHECK(R->spells.cast[0x01].handler == COK_CAST_BLESS && R->spells.cast[0x01].text == NULL &&
          pascal_is(ds(0x4570), "is ", false) && pascal_is(ds(0x45b5), "Blessed", false) &&
          HOLDS(0x5dc5, 0x1d77, 0x30, 0xbf, 0x70, 0x45) && HOLDS(0x5dc5, 0x1d77, 0x30, 0xbf, 0xb5,
                                                                 0x45));
    /* Charm Person: cmp byte [es:di+0x20], 7; and al, 2 (the elf's flag). */
    CHECK(R->spells.cast[0x0a].value == 1 && R->spells.cast[0x6a].value == 1 &&
          HOLDS(0x5dc5, 0x2044, 0x40, 0x26, 0x80, 0x7d, 0x20, 0x07) &&
          HOLDS(0x5dc5, 0x2044, 0x40, 0x24, 0x02));
    /* Strength: the paladin's level (+0x48) among those rolling 1d8, the
     * fighters' classes of the characters section. */
    CHECK(R->spells.cast[0x23].value == 1 && R->characters.fighter_classes == 0x9c &&
          HOLDS(0x5dc5, 0x2bc9, 0x80, 0x26, 0x80, 0x7d, 0x48, 0x00) &&
          HOLDS(0x5dc5, 0x2bc9, 0x80, 0xb0, 0x01, 0x50, 0xb0, 0x08, 0x50));
    /* Remove Curse cures 0x2b (mov al, 0x2b; 638c:1479). */
    CHECK(R->spells.cast[0x2b].value == 0x2b && HOLDS(0x5dc5, 0x311b, 0x20, 0xb0, 0x2b, 0x50));
    /* Delay Blast Fireball: cmp byte [DS:be70], 0x6c, the level kept. */
    CHECK(R->spells.cast[0x6c].value == 1 && R->spells.cast[0x2f].value == 0 &&
          R->spells.cast[0x40].value == 0 &&
          HOLDS(0x5dc5, 0x3241, 0x120, 0x80, 0x3e, 0x70, 0xbe, 0x6c));
    /* Stinking Cloud and Cloud Kill (5dc5:2934): cmp byte [DS:be70],
     * 0x22, the tiles 0x1d and 0x1c (+0x1e) and their cells' counts 4 and
     * 9, the cells by DS:5843, on the map (x 0-0x31, y 0-0x18), the cell
     * under (6f03:152e), the centring, then the cloud square (638c:0c65). */
    CHECK(R->spells.cast[0x22].handler == COK_CAST_STINKING_CLOUD &&
          R->spells.cast[0x22].address == 0x2934 && R->spells.cast[0x22].value == 0 &&
          R->spells.cast[0x5b].handler == COK_CAST_STINKING_CLOUD &&
          R->spells.cast[0x5b].address == 0x2934 && R->spells.cast[0x5b].value == 1 &&
          HOLDS(0x5dc5, 0x2934, 0x100, 0x80, 0x3e, 0x70, 0xbe, 0x22) &&
          HOLDS(0x5dc5, 0x2934, 0x100, 0xc6, 0x46, 0xf6, 0x04, 0xc4, 0x7e, 0xe5, 0x26, 0xc6, 0x45,
                0x1e, R->fight.cloud_tile) &&
          HOLDS(0x5dc5, 0x2934, 0x100, 0xc6, 0x46, 0xf6, 0x09, 0xc4, 0x7e, 0xe5, 0x26, 0xc6, 0x45,
                0x1e, R->fight.pool_tile) &&
          HOLDS(0x5dc5, 0x2934, 0x100, 0x8a, 0x85, 0x43, 0x58) &&
          HOLDS(0x5dc5, 0x2934, 0x140, 0x83, 0x7e, 0xfe, 0x31) &&
          HOLDS(0x5dc5, 0x2934, 0x140, 0x83, 0x7e, 0xfc, 0x18));
    unsigned long calls[64];
    size_t n = far_calls(0x5dc5, 0x2934, 0x295, calls, 64);
    CHECK(n == 8 && calls[3] == 0x65cd2c33 && calls[4] == 0x6f0306d0 && calls[5] == 0x6f03152e &&
          calls[6] == 0x6f030b0e && calls[7] == 0x638c0c65);
    /* Fear: 65cd:2687 for 0x6f after it is added. */
    n = far_calls(0x5dc5, 0x4566, 0x128, calls, 64);
    bool found = false;
    for (size_t i = 0; i < n; ++i) found |= calls[i] == 0x65cd2687;
    CHECK(R->spells.cast[0x54].value == 1 && found);
    /* Fire Shield: 1d2, the menu by index and the abort question. */
    n = far_calls(0x5dc5, 0x468f, 0x16b, calls, 64);
    bool menu = false, ask = false;
    for (size_t i = 0; i < n; ++i) {
        menu |= calls[i] == 0x6a690f1b;
        ask |= calls[i] == 0x6a692048;
    }
    CHECK(R->spells.cast[0x55].value == 1 && menu && ask &&
          HOLDS(0x5dc5, 0x468f, 0x30, 0xb0, 0x01, 0x50, 0xb0, 0x02, 0x50) &&
          pascal_is(ds(0x2fee), R->casting.flame_type, false) &&
          HOLDS(0x5dc5, 0x468f, 0x40, 0xbf, 0xee, 0x2f) &&
          pascal_is(ds(0x3218), "Hot Cold", false) &&
          HOLDS(0x5dc5, 0x468f, 0x160, 0xbf, 0x84, 0x0f));
    /* Cure Critical Wounds: the deity (+0x23 4) tested before the count
     * (DS:c31c), 3d8 + 3. */
    const uint8_t *cc = at(0x5dc5, 0x4037);
    CHECK(R->spells.cast[0x47].handler == COK_CAST_CURE_CRITICAL && cc[10] == 0x26 &&
          cc[11] == 0x80 && cc[13] == 0x23 && cc[14] == 0x04 &&
          !HOLDS(0x5dc5, 0x4037, 0x10, 0x80, 0x3e, 0x1c, 0xc3, 0x00) &&
          HOLDS(0x5dc5, 0x4037, 0x40, 0x80, 0x3e, 0x1c, 0xc3, 0x00) &&
          HOLDS(0x5dc5, 0x4037, 0x50, 0xb0, 0x03, 0x50, 0xb0, 0x08, 0x50));
    /* Faerie Fire: 1b20 with no bonus. */
    CHECK(R->spells.cast[0x4f].handler == COK_CAST_MISSILES && near_call(0x5dc5, 0x4391, 0x10,
                                                                          0x1b20));
    /* The cone sets its range from the doubled length (mov al, [bp+6];
     * xor ah, ah; mov [bp-0x78], ax). */
    CHECK(R->casting.cone_side_unknown != NULL &&
          strstr(R->casting.cone_side_unknown, "dkk 5dc5:175d's [bp-0x76]") != NULL &&
          HOLDS(0x5dc5, 0x175d, 0x400, 0x88, 0x46, 0x8a));
    CHECK(R->casting.cone_range_length && R->casting.cone_stack_unknown &&
          HOLDS(0x5dc5, 0x175d, 0x80, 0x8a, 0x46, 0x06, 0x30, 0xe4, 0xd1, 0xe0, 0x88, 0x46, 0x06) &&
          HOLDS(0x5dc5, 0x175d, 0x140, 0x8a, 0x46, 0x06, 0x30, 0xe4, 0x89, 0x46, 0x88));
    /* Dispel Magic's clouds: the steps' tables, the cloud's and the
     * pool's tiles, their cells counted (4 and 9), 2dd7 with the cloud's
     * level (+0x1d), its mark (+0x1c 0xff), the stale byte DS:8954, and
     * the clouds marked going (638c:21ec). */
    CHECK(HOLDS(0x5dc5, 0x2f93, 0x80, 0xc6, 0x46, 0xec, 0x04) &&
          HOLDS(0x5dc5, 0x2f93, 0x80, 0xc6, 0x46, 0xec, 0x09) &&
          HOLDS(0x5dc5, 0x2f93, 0x110, 0x26, 0x8a, 0x45, 0x1d) &&
          HOLDS(0x5dc5, 0x2f93, 0x110, 0x26, 0xc6, 0x45, 0x1c, 0xff) &&
          HOLDS(0x5dc5, 0x2dd7, 0x60, 0xa0, 0x54, 0x89));
    n = far_calls(0x5dc5, 0x30d0, 0x10, calls, 64);
    CHECK(n == 1 && calls[0] == 0x638c21ec);
    CHECK(cok_data_offset(R->data, COK_DATA_STEP_X, 0) == 0x4412 &&
          HOLDS(0x5dc5, 0x2e5c, 0x200, 0x8a, 0x85, 0x12, 0x44) &&
          HOLDS(0x5dc5, 0x2e5c, 0x200, 0x80, 0x7e, 0xed, R->fight.cloud_tile) &&
          HOLDS(0x5dc5, 0x2e5c, 0x200, 0x80, 0x7e, 0xed, R->fight.pool_tile) &&
          near_call(0x5dc5, 0x2e5c, 0x280, 0x2dd7));
    puts("death knights: the band B spells checked");
}

/* The treasure section against the original's code: the item table's
 * loader (4f9a:0006, called at the end of startup 4f9a:00e1), TREASURE
 * ITEMS (3632:1933) and STOCK (3632:28d2) with their tables, the treasure
 * menu (3a37:0eed), Take (0dfc) and its items (0c26) and money (5c72:0ac4,
 * 09e4), taking an item (3b89:033b), the vault (3948:000e, 65cd:2db0,
 * 580d:26d0), DAMAGE (3632:1ca2) and ADD EP (3632:25f1). */
static void test_treasure(void)
{
    const cok_rules_treasure *T = &R->treasure;
    const cok_data_segment *D = R->data;
    /* 4f9a:0006: FillChar(DS:a2ca, 0x10de, 0xff), then ITEM0 (its name at
     * 4f9a:0000) record 1, ids from 1 at DS:a2b9 + 17 * id while at most
     * 0x7f. */
    CHECK(pascal_is(at(0x4f9a, 0x0000), T->table.stem, false) && T->table.file == 0 &&
          T->table.record == 1 && T->table_count == 0x7f);
    CHECK(HOLDS(0x4f9a, 0x0006, 0x20, 0xbf, 0xca, 0xa2, 0x1e, 0x57, 0xb8, 0xde, 0x10, 0x50, 0xb0,
                0xff) &&
          0xa2ca + 0x10de == 0xa2b9 + COK_ITEM_ENTRY * COK_ITEM_ENTRIES);
    CHECK(HOLDS(0x4f9a, 0x0006, 0x40, 0xbf, 0x22, 0x43, 0x1e, 0x57, 0xb0, T->table.record, 0x50) &&
          HOLDS(0x4f9a, 0x0006, 0xc0, 0x81, 0xc7, 0xb9, 0xa2) &&
          HOLDS(0x4f9a, 0x0006, 0xc0, 0x80, 0x7e, 0xfb, T->table_count) &&
          HOLDS(0x4f9a, 0x0006, 0xc0, 0xba, COK_ITEM_ENTRY, 0x00));
    CHECK(near_call(0x4f9a, 0x00e1, 0x800, 0x0006));
    /* 3632:1933: the header's three words added to DS:be58, 5c and 60
     * (steel, gems, jewelry: the pool's coins 4-6), each id's entry at
     * [di-0x5d47], 17 bytes, none for 0xff. */
    CHECK(HOLDS(0x3632, 0x1933, 0x50, 0x01, 0x06, 0x58, 0xbe) &&
          HOLDS(0x3632, 0x1933, 0x50, 0x01, 0x06, 0x5c, 0xbe) &&
          HOLDS(0x3632, 0x1933, 0x50, 0x01, 0x06, 0x60, 0xbe) &&
          HOLDS(0x3632, 0x1933, 0x100, 0x80, 0xbd, 0xb9, 0xa2, 0xff) &&
          cok_field_first(R->character, COK_FIELD_COINS) == 4);
    /* 3632:28d2: shop 1-8 (cmp 8, else 1), [di+7], [di+0xf], [di+0x17],
     * the item's +0x35 and 0xf8. */
    CHECK(HOLDS(0x3632, 0x28d2, 0x80, 0x80, 0x7e, 0xff, 0x08) &&
          HOLDS(0x3632, 0x28d2, 0x80, 0x8a, 0x85, 0x07, 0x00) &&
          HOLDS(0x3632, 0x28d2, 0x90, 0x8a, 0x85, 0x0f, 0x00) &&
          HOLDS(0x3632, 0x28d2, 0xc0, 0x8a, 0x85, 0x17, 0x00) &&
          HOLDS(0x3632, 0x28d2, 0x100, 0x24, 0xf8) &&
          cok_data_offset(D, COK_DATA_STOCK_FIRST, 0) == 0x0007 &&
          cok_data_offset(D, COK_DATA_STOCK_COUNTS, 0) == 0x000f &&
          cok_data_offset(D, COK_DATA_STOCK_ITEMS, 0) == 0x0017 &&
          cok_data_count(D, COK_DATA_STOCK_FIRST, 0) == 8);
    /* The tables reach no further than shop 8's last item. */
    unsigned last = 0;
    for (unsigned shop = 1; shop <= 8; ++shop) {
        unsigned end = ds(0x0007)[shop] + ds(0x000f)[shop] - 1u;
        if (end > last) last = end;
        CHECK(cok_data_u8_at(D, COK_DATA_STOCK_FIRST, shop) == ds(0x0007)[shop] &&
              cok_data_u8_at(D, COK_DATA_STOCK_COUNTS, shop) == ds(0x000f)[shop]);
    }
    CHECK(last == cok_data_count(D, COK_DATA_STOCK_ITEMS, 0));
    /* The treasure menu: its items DS:3560 (as Champions'), the words
     * of leaving (DS:194f, 1978), the Yes/No from No (6a69:2048 with
     * DS:57c1, 1); Take's (DS:3168, 354b), the items' (DS:315d, 3540), the
     * coins' (5c72:0ac4: DS:104c, 31d6; 0fae, 0fce, 0fb9; 09e4: DS:0c62,
     * 0c6d, 0c78, each and a space). */
    CHECK(pascal_is(ds(0x3560), "View Take Pool Share Detect Exit", false) &&
          HOLDS(0x3a37, 0x0eed, 0x300, 0xbf, 0x60, 0x35) &&
          pascal_is(ds(0x194f), T->left, false) && pascal_is(ds(0x1978), T->claim, false) &&
          HOLDS(0x3a37, 0x0eed, 0x300, 0xbf, 0x4f, 0x19) &&
          HOLDS(0x3a37, 0x0eed, 0x300, 0xbf, 0x78, 0x19) &&
          HOLDS(0x3a37, 0x0eed, 0x300, 0xa0, 0xc1, 0x57) && ds(0x57c1)[0] == 1 && T->claim_no &&
          T->menus_first && T->specials_apart);
    CHECK(pascal_is(ds(0x3168), T->take_prompt, false) &&
          pascal_is(ds(0x354b), "Money Items Exit", false) &&
          HOLDS(0x3a37, 0x0dfc, 0x60, 0xbf, 0x68, 0x31) &&
          pascal_is(ds(0x315d), T->items_prompt, false) && pascal_is(ds(0x3540), "Take", false) &&
          HOLDS(0x3a37, 0x0c26, 0x80, 0xbf, 0x5d, 0x31) &&
          pascal_is(ds(0x104c), T->coin_prompt, false) && pascal_is(ds(0x31d6), "Select", false) &&
          HOLDS(0x5c72, 0x0ac4, 0x200, 0xbf, 0x4c, 0x10) &&
          pascal_is(ds(0x0fae), T->how_many, false) && pascal_is(ds(0x0fce), T->how_much, false) &&
          pascal_is(ds(0x0fb9), " will you take? ", false));
    static const unsigned words[3] = {0x0c62, 0x0c6d, 0x0c78};
    for (unsigned k = 0; k < 3; ++k) {
        char word[16];
        const uint8_t *p = ds(words[k]);
        snprintf(word, sizeof word, "%.*s ", (int)p[0], (const char *)p + 1);
        CHECK(T->coin_words[4 + k] != NULL && strcmp(T->coin_words[4 + k], word) == 0 &&
              HOLDS(0x5c72, 0x09e4, 0xe0, 0xbf, (uint8_t)words[k], (uint8_t)(words[k] >> 8)));
    }
    /* 3b89:033b: 580d:2a0a with 1 (the join's rule), "Overloaded" (DS:2acc),
     * 580d:1be6 (Join) for an item with a count (+0x39). */
    CHECK(HOLDS(0x3b89, 0x033b, 0x20, 0xb0, 0x01, 0x50) &&
          pascal_is(ds(0x2acc), T->overloaded, false) &&
          HOLDS(0x3b89, 0x033b, 0x30, 0xbf, 0xcc, 0x2a) &&
          HOLDS(0x3b89, 0x033b, 0x110, 0x26, 0x80, 0x7d, 0x39, 0x00) && T->take_joins);
    unsigned long calls[24];
    size_t n = far_calls(0x3b89, 0x033b, 0x120, calls, 24);
    bool overload = false, join = false;
    for (size_t i = 0; i < n; ++i) {
        overload |= calls[i] == 0x580d2a0a;
        join |= calls[i] == 0x580d1be6;
    }
    CHECK(overload && join);
    /* The vault: its items (DS:35bd), mode 7's line (65cd:2db0, DS:2ccd),
     * the deposit's question (580d:26d0, DS:0fce and DS:1237), and the
     * routines its View and Items call. */
    char items[64] = "";
    for (size_t i = 0; i < 6; ++i)
        snprintf(items + strlen(items), sizeof items - strlen(items), "%s%s", i > 0 ? " " : "",
                 T->vault_items[i]);
    CHECK(pascal_is(ds(0x35bd), items, false) && HOLDS(0x3948, 0x000e, 0x180, 0xbf, 0xbd, 0x35) &&
          pascal_is(ds(0x2ccd), T->vault_line, false) &&
          HOLDS(0x65cd, 0x2db0, 0x80, 0xbf, 0xcd, 0x2c) &&
          pascal_is(ds(0x1237), T->deposit, false) &&
          HOLDS(0x580d, 0x26d0, 0x300, 0xbf, 0x37, 0x12));
    n = far_calls(0x3948, 0x000e, 0x260, calls, 24);
    bool view = false, list = false, take = false;
    for (size_t i = 0; i < n; ++i) {
        view |= calls[i] == 0x580d0c66;
        list |= calls[i] == 0x580d0ffa;
        take |= calls[i] == 0x3a370dfc;
    }
    CHECK(view && list && take);
    /* DAMAGE: and al, 0x0f (the bonus), shr ax, 1 (half when saved), its
     * key DS:0868. */
    CHECK(T->damage_bonus == 0x0f && HOLDS(0x3632, 0x1ca2, 0x100, 0x24, T->damage_bonus) &&
          T->damage_halves && HOLDS(0x3632, 0x1ca2, 0x140, 0x8b, 0x46, 0xf4, 0xd1, 0xe8) &&
          pascal_is(ds(0x0868), T->damage_key, false) &&
          HOLDS(0x3632, 0x1fd7, 0x20, 0xbf, 0x68, 0x08));
    /* ADD EP: its words (DS:1485, 2a64; 2a3b), +0xc6 from 65cd:38c7. */
    CHECK(pascal_is(ds(0x1485), T->gains, false) && pascal_is(ds(0x2a64), T->gains_end, false) &&
          pascal_is(ds(0x2a3b), T->party_gains, false) &&
          HOLDS(0x3632, 0x25f1, 0x140, 0xbf, 0x85, 0x14) &&
          HOLDS(0x3632, 0x25f1, 0x60, 0xbf, 0x3b, 0x2a) &&
          HOLDS(0x3632, 0x25f1, 0xe0, 0x26, 0x88, 0x85, 0xc6, 0x00) && T->gains_active_first);
    puts("death knights: the treasure section checked");
}

/* Whether the n bytes from segment:offset load DS:word (mov di, word). */
static bool loads_word(unsigned segment, unsigned offset, size_t n, unsigned word)
{
    return HOLDS(segment, offset, n, 0xbf, (uint8_t)word, (uint8_t)(word >> 8));
}

/* The bits of a Pascal set of chars at segment:offset (32 bytes). */
static bool set_is(unsigned segment, unsigned offset, const char *members)
{
    uint8_t set[32] = {0};
    for (const char *c = members; *c != '\0'; ++c)
        set[(uint8_t)*c >> 3] |= (uint8_t)(1u << ((uint8_t)*c & 7));
    return memcmp(at(segment, offset), set, sizeof set) == 0;
}

/* The menus' and the commands' sections against the original's code: the
 * menu loop's sets (6a69:0f1b: items start at 0-9 and A-Z, CS:0115, 023d,
 * 0cfe; '@' CS:0ed9; rows by 2 8 H P, CS:0edb; the keypad's 1-9 and \,
 * CS:0efb, through DS:5791, Champions' DS:1fdf), its colours (DS:c148,
 * c149, c14a set once to 10, 13, 15), the Yes/No's items and the typed
 * constants Yes and No (6a69:2048, DS:31cb, 57c0, 57c1); the turn's menu
 * (3e05:0ccc: DS:3453, the hidden keys' set CS:0cac), Move's, aiming's,
 * the cursor's and Speed's words where their routines load them (3e05:10bb,
 * 13fb, 5340:2b81, 2bee, 2f3d, 3e05:14aa), the cursor's icon slot (6f03:036c
 * and 5340:3888's 65cd:1bd5: 0x1c), aiming's range (5340:3888 reads the
 * type's byte 12 whole) and its list's (5340:36f2: 0xff, DS:4558 0xff),
 * and 580d:2bb2, which picks only for H and P. */
static void test_commands(void)
{
    const cok_rules_menus *M = &R->menus;
    const cok_rules_commands *K = &R->commands;
    CHECK(M->routine == COK_MENUS_INDEXED && K->routine == COK_COMMANDS_DEATH_KNIGHTS);
    static const char alnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHECK(set_is(0x6a69, 0x0115, alnum) && set_is(0x6a69, 0x023d, alnum) &&
          set_is(0x6a69, 0x0cfe, alnum) && pascal_is(at(0x6a69, 0x0ed9), "@", false) &&
          set_is(0x6a69, 0x0edb, "28HP") && set_is(0x6a69, 0x0efb, "123456789\\"));
    static const uint8_t keypad[9] = {0x4f, 0x50, 0x51, 0x4b, 0x20, 0x4d, 0x47, 0x48, 0x49};
    CHECK(memcmp(ds(0x5791 + (unsigned)'1'), keypad, sizeof keypad) == 0);
    CHECK(HOLDS(0x4f9a, 0, 0x100, 0xc6, 0x06, 0x48, 0xc1, 10, 0xc6, 0x06, 0x49, 0xc1, 13, 0xc6,
                0x06, 0x4a, 0xc1, 15));
    CHECK(pascal_is(ds(0x31cb), M->yes_no, false) && loads_word(0x6a69, 0x2048, 0x40, 0x31cb) &&
          ds(0x57c0)[0] == 0 && ds(0x57c1)[0] == 1);
    CHECK(M->adventure_keeps_selection && M->escape_leaves_move && M->special_keeps);
    /* Camp's menu (4381:3106): its own selection [bp-4] into DS:c0f7 before
     * the ask, and back after. */
    CHECK(M->camp_keeps_selection &&
          HOLDS(0x4381, 0x3106, 0x100, 0x8a, 0x46, 0xfc, 0xa2, 0xf7, 0xc0) &&
          HOLDS(0x4381, 0x3106, 0x100, 0xa0, 0xf7, 0xc0, 0x88, 0x46, 0xfc));
    /* 580d:2bb2: cmp al, 'H' and 'P', nothing for another key. */
    CHECK(HOLDS(0x580d, 0x2bb2, 0x60, 0x3c, 0x48) && HOLDS(0x580d, 0x2bb2, 0xe0, 0x3c, 0x50));
    const struct {
        unsigned seg, off, n, word;
        const char *text;
    } words[] = {
        {0x3e05, 0x0ccc, 0x200, 0x3453, K->menu},
        {0x3e05, 0x10bb, 0x100, 0x313e, K->move_prompt},
        {0x3e05, 0x10bb, 0x100, 0x34a4, K->move_menu},
        {0x3e05, 0x10bb, 0x200, 0x170d, K->flee},
        {0x3e05, 0x10bb, 0x280, 0x1718, K->cant_go},
        {0x3e05, 0x13fb, 0x40, 0x2536, K->not_with_weapon},
        {0x5340, 0x2b81, 0x40, 0x16d7, K->attack_ally},
        {0x5340, 0x2bee, 0x200, 0x3109, K->aim_prompt},
        {0x5340, 0x2bee, 0x200, 0x34af, K->aim_menu},
        {0x5340, 0x2bee, 0x100, 0x16ec, K->range},
        {0x5340, 0x2f3d, 0x400, 0x3114, K->cursor_prompt},
        {0x5340, 0x2f3d, 0x400, 0x34d8, K->cursor_menu},
        {0x3e05, 0x14aa, 0x40, 0x30f4, K->speed_prompt},
        {0x3e05, 0x14aa, 0x100, 0x340a, K->speed_menu},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i)
        CHECK(pascal_is(ds(words[i].word), words[i].text, false) &&
              loads_word(words[i].seg, words[i].off, words[i].n, words[i].word));
    CHECK(!K->ally_turns_npcs);
    /* The turn's hidden keys: Alt-Q, Alt-R, Alt-A, the keypad's 5, Alt-X,
     * Alt-M. */
    CHECK(set_is(0x3e05, 0x0cac, "\x10\x13\x1e \x2d\x32"));
    /* The cursor's box: push 0x1c at 6f03:036c's 7090:0484, and 5340:3888's
     * first call, 65cd:1bd5 with 0x1c. */
    CHECK(K->cursor_slot == 0x1c && HOLDS(0x6f03, 0x036c, 0x100, 0xb0, K->cursor_slot, 0x50) &&
          HOLDS(0x5340, 0x3888, 0x10, 0xb0, K->cursor_slot, 0x50));
    unsigned long calls[24];
    CHECK(far_calls(0x5340, 0x3888, 0x20, calls, 24) == 1 && calls[0] == 0x65cd1bd5);
    /* Aiming's range: the type's byte 12 (DS:9aa6 + 12) not less 1; the
     * list's range 0xff and direction DS:4558, 0xff. */
    CHECK(HOLDS(0x5340, 0x3888, 0x80, 0x8a, 0x85, 0xb2, 0x9a, 0x88) && ds(0x4558)[0] == 0xff &&
          HOLDS(0x5340, 0x36f2, 0x40, 0xb8, 0xff, 0x00, 0x50, 0xa0, 0x58, 0x45));
    puts("death knights: the menus' and commands' sections checked");
}

/* The items section against the original's code: View's and the items
 * menu's items (580d:0c66, DS:3276; 0ffa, DS:32c7, with the list 6a69:1b82
 * and its last index 8, DS:2fad's empty prompt), the readied column
 * (65cd:047f, DS:2c02, 2c0d), Ready's thief level and word (580d:1717,
 * 1703), the words where their routines load them (Use's DS:111f, the
 * scroll's and Drop's question DS:0f1a from No, Trade money's DS:1061,
 * the coin lists' DS:31d6, Deposit's 580d:0fda, 0fe0, Sell's and Id's
 * from Yes, Heal's and Cure's), Id's steel (+0x3f) and the pool's
 * (DS:be58), and Heal's class, effect and minutes (580d:2e95, 2f47). */
static void test_items(void)
{
    const cok_rules_items *I = &R->items;
    CHECK(I->routine == COK_ITEMS_DEATH_KNIGHTS);
    CHECK(pascal_is(ds(0x3276), I->view_menu, false) && loads_word(0x580d, 0x0c66, 0x200, 0x3276) &&
          pascal_is(ds(0x32c7), I->items_menu, false) &&
          loads_word(0x580d, 0x0ffa, 0x300, 0x32c7) && pascal_is(ds(0x2fad), "", false) &&
          loads_word(0x580d, 0x0ffa, 0x300, 0x2fad) &&
          HOLDS(0x580d, 0x0ffa, 0x2a0, 0xb0, 0x08, 0x50) && pascal_is(at(0x580d, 0x0c65), "", false));
    unsigned long calls[32];
    size_t n = far_calls(0x580d, 0x0ffa, 0x300, calls, 32);
    bool list = false;
    for (size_t i = 0; i < n; ++i) list |= calls[i] == 0x6a691b82;
    CHECK(list);
    CHECK(pascal_is(ds(0x2c02), I->readied_yes, false) && pascal_is(ds(0x2c0d), I->readied_no, false) &&
          loads_word(0x65cd, 0x047f, 0x40, 0x2c02) && loads_word(0x65cd, 0x047f, 0x40, 0x2c0d));
    CHECK(I->thief_scrolls == 10 &&
          HOLDS(0x580d, 0x1717, 0x180, 0x26, 0x80, 0x7d, 0x4b, I->thief_scrolls) &&
          pascal_is(at(0x580d, 0x1703), I->thief_too_low, false) &&
          loads_word(0x580d, 0x1717, 0x300, 0x1703));
    const struct {
        unsigned seg, off, n, word;
        bool code;
        const char *text;
    } words[] = {
        {0x580d, 0x0ffa, 0x700, 0x111f, false, I->must_ready},
        {0x580d, 0x0efb, 0xd0, 0x0f1a, false, I->lose_scroll},
        {0x580d, 0x0ffa, 0x700, 0x0f1a, false, I->drop_question},
        {0x580d, 0x23a0, 0x40, 0x1061, false, I->trade_to},
        {0x580d, 0x23a0, 0x200, 0x31d6, false, I->coin_select},
        {0x580d, 0x26d0, 0x200, 0x31d6, false, I->coin_select},
        {0x580d, 0x0ffa, 0x700, 0x0fda, true, I->deposit_before},
        {0x580d, 0x0ffa, 0x700, 0x0fe0, true, I->deposit_after},
        {0x580d, 0x2077, 0x100, 0x1134, false, I->offer},
        {0x580d, 0x2077, 0x100, 0x1149, false, I->offer_for},
        {0x580d, 0x2077, 0x100, 0x1168, false, I->deal},
        {0x580d, 0x2077, 0x100, 0x117d, false, I->sold},
        {0x580d, 0x2077, 0x150, 0x1188, false, I->sold_pool},
        {0x580d, 0x21d3, 0x80, 0x11b1, false, I->identify},
        {0x580d, 0x21d3, 0x200, 0x1168, false, I->deal},
        {0x580d, 0x21d3, 0x200, 0x11da, false, I->nothing_new},
        {0x580d, 0x21d3, 0x200, 0x1203, false, I->looks_like},
        {0x580d, 0x21d3, 0x200, 0x1743, false, I->no_money},
        {0x580d, 0x2f47, 0x40, 0x2f1e, true, I->heal_whom},
        {0x580d, 0x2f47, 0xc0, 0x2f2a, true, I->feels_better},
        {0x580d, 0x2f47, 0xc0, 0x2f38, true, I->unaffected},
        {0x580d, 0x3018, 0x40, 0x300c, true, I->cure_whom},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i) {
        bool is = pascal_is(words[i].code ? at(0x580d, words[i].word) : ds(words[i].word),
                            words[i].text, false),
             loaded = loads_word(words[i].seg, words[i].off, words[i].n, words[i].word);
        if (!is || !loaded) fprintf(stderr, "word %zu: %d %d\n", i, is, loaded);
        CHECK(is && loaded);
    }
    /* The Yes/No's first: No (DS:57c1) for the scroll and Drop, Yes
     * (DS:57c0) for Sell and Id. */
    CHECK(HOLDS(0x580d, 0x0efb, 0xd0, 0xa0, 0xc1, 0x57) && HOLDS(0x580d, 0x0ffa, 0x700, 0xa0, 0xc1, 0x57) &&
          I->deal_yes && HOLDS(0x580d, 0x2077, 0x100, 0xa0, 0xc0, 0x57) &&
          HOLDS(0x580d, 0x21d3, 0x80, 0xa0, 0xc0, 0x57));
    /* Sell multiplies by the count, divides by nothing; Id pays from the
     * steel (+0x3f) and the pool's (DS:be58), waiting twice when it shows. */
    CHECK(I->sell_divisor == 1 && !HOLDS(0x580d, 0x2077, 0x80, 0xb9, 0x14, 0x00) &&
          R->shops.steel && HOLDS(0x580d, 0x21d3, 0x80, 0x26, 0x8b, 0x45, 0x3f) &&
          HOLDS(0x580d, 0x21d3, 0x100, 0xa1, 0x58, 0xbe) && I->id_waits_twice);
    /* Heal: class 3, not in combat, okay, effect 0x6d; healing twice +0x48;
     * the effect for 0x5a0 minutes. Cure: +0x25. */
    CHECK(I->heal_class == 3 && HOLDS(0x580d, 0x2e95, 0x10, 0x26, 0x80, 0x7d, 0x21, I->heal_class) &&
          HOLDS(0x580d, 0x2e95, 0x30, 0xb0, I->heal_effect) &&
          HOLDS(0x580d, 0x2f47, 0x60, 0x26, 0x8a, 0x45, 0x45 + I->heal_class, 0x98, 0xd1, 0xe0) &&
          HOLDS(0x580d, 0x2f47, 0xc0, 0xb0, I->heal_effect, 0x50, 0xb8, (uint8_t)I->heal_minutes,
                (uint8_t)(I->heal_minutes >> 8)) &&
          HOLDS(0x580d, 0x2ee0, 0x30, 0x26, 0x80, 0x7d, 0x25, 0x00));
    CHECK(cok_field_offset(R->character, COK_FIELD_KNIGHT_TIER) == 0x25 &&
          cok_field_offset(R->character, COK_FIELD_CLASS) == 0x21);
    puts("death knights: the items section checked");
}

/* The shops section against the original's code: the shop's (3b89:06f3)
 * and the temple's (3999:07d3) menus and words of leaving money, Buy's
 * (0453) list (00b1), funds (0000) and words, the steel it pays from
 * (+0x3f, DS:be58) and the mode the shop puts back (DS:8c4f), Appraise's
 * (5c72:0dc2, 00ba) words, Heal's (3999:04e2) cures, the spells' names it
 * lists and its words, the cures' words and the spells' handlers Raise
 * Dead, Remove Curse and Restoration run (3999:01fc, 0368, 045f), and mode
 * 1's redraw (65cd:2db0: its branch with mode 7's, the small picture). */
static void test_shops(void)
{
    const cok_rules_shops *S = &R->shops;
    CHECK(S->routine == COK_SHOPS_DEATH_KNIGHTS && S->steel && !S->portrait);
    const struct {
        unsigned seg, off, n, word;
        const char *text;
    } words[] = {
        {0x3b89, 0x06f3, 0x100, 0x34f8, S->menu},
        {0x3999, 0x07d3, 0x100, 0x3594, S->temple_menu},
        {0x3b89, 0x06f3, 0x250, 0x1758, S->left},
        {0x3b89, 0x06f3, 0x250, 0x17a9, S->go_back},
        {0x3999, 0x07d3, 0x200, 0x1758, S->left},
        {0x3999, 0x07d3, 0x200, 0x17a9, S->go_back},
        {0x3b89, 0x00b1, 0x200, 0x34ed, S->buy_menu},
        {0x3b89, 0x00b1, 0x200, 0x315d, R->treasure.items_prompt},
        {0x3b89, 0x0000, 0xb1, 0x2aa2, S->personal_funds},
        {0x3b89, 0x0000, 0xb1, 0x2ab7, S->pooled_funds},
        {0x3b89, 0x0453, 0x2a0, 0x1738, S->buys_a},
        {0x3b89, 0x0453, 0x2a0, 0x172d, S->buys},
        {0x3b89, 0x0453, 0x2a0, 0x1743, R->items.no_money},
        {0x5c72, 0x0dc2, 0x80, 0x1081, S->no_gems},
        {0x5c72, 0x0dc2, 0x100, 0x0fd9, S->gems},
        {0x5c72, 0x0dc2, 0x100, 0x0fe4, S->jewelry},
        {0x5c72, 0x0dc2, 0x200, 0x0fef, S->collection},
        {0x5c72, 0x0dc2, 0x300, 0x3223, S->appraise_menu},
        {0x5c72, 0x0dc2, 0x300, 0x3003, S->appraise_prompt},
        {0x5c72, 0x0dc2, 0x400, 0x100e, S->gem_valued},
        {0x5c72, 0x0dc2, 0x700, 0x102d, S->jewel_valued},
        {0x5c72, 0x0dc2, 0x400, 0x1096, S->valued_end},
        {0x5c72, 0x0dc2, 0x500, 0x3238, S->sell_keep},
        {0x5c72, 0x00ba, 0x60, 0x1188, R->items.sold_pool},
        {0x3999, 0x04e2, 0x200, 0x3589, S->heal_menu},
        {0x3999, 0x04e2, 0x200, 0x1aa6, S->help},
        {0x3999, 0x0000, 0x53, 0x19c9, S->anyway},
        {0x3999, 0x0053, 0x58, 0x19fe, S->not_blind},
        {0x3999, 0x01fc, 0xa4, 0x1a28, S->cannot_raise},
        {0x3999, 0x01fc, 0xa4, 0x1a3d, S->not_dead},
        {0x3999, 0x02a0, 0xc8, 0x1a52, S->not_poisoned},
        {0x3999, 0x0368, 0x9c, 0x1a67, S->not_cursed},
        {0x3999, 0x0404, 0x5b, 0x1a7c, S->not_stoned},
        {0x3999, 0x045f, 0x83, 0x1a91, S->no_lost_levels},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i) {
        bool is = pascal_is(ds(words[i].word), words[i].text, false),
             loaded = loads_word(words[i].seg, words[i].off, words[i].n, words[i].word);
        if (!is || !loaded) fprintf(stderr, "shops word %zu: %d %d\n", i, is, loaded);
        CHECK(is && loaded);
    }
    /* The shop leaves from Yes (DS:57c0), the temple from No (57c1). */
    CHECK(HOLDS(0x3b89, 0x06f3, 0x250, 0xa0, 0xc0, 0x57) &&
          HOLDS(0x3999, 0x07d3, 0x200, 0xa0, 0xc1, 0x57));
    /* The mode kept (mov al, [8c4e]; mov [8c4f], al) and put back. */
    CHECK(HOLDS(0x3b89, 0x06f3, 0x10, 0xa0, 0x4e, 0x8c, 0xa2, 0x4f, 0x8c) &&
          HOLDS(0x3b89, 0x06f3, 0x250, 0xa0, 0x4f, 0x8c, 0xa2, 0x4e, 0x8c) &&
          !HOLDS(0x3999, 0x07d3, 0x200, 0xa2, 0x4f, 0x8c));
    /* Buy pays from the steel (+0x3f) and the pool's (DS:be58). */
    CHECK(HOLDS(0x3b89, 0x0453, 0x2a0, 0x26, 0x8b, 0x45, 0x3f) &&
          HOLDS(0x3b89, 0x0453, 0x2a0, 0xa1, 0x58, 0xbe));
    /* Heal's cures: DS:030c's ids (mov al, [di+0x30b], from 1), named
     * from DS:5825 + 41 * id. */
    CHECK(S->cure_count == 11 && memcmp(ds(0x030c), S->cures, S->cure_count) == 0 &&
          HOLDS(0x3999, 0x04e2, 0x80, 0x8a, 0x85, 0x0b, 0x03) &&
          HOLDS(0x3999, 0x04e2, 0x80, 0xba, 0x29, 0x00) &&
          HOLDS(0x3999, 0x04e2, 0x80, 0x81, 0xc7, 0x25, 0x58));
    for (size_t i = 0; i < S->cure_count; ++i)
        CHECK(S->cures[i] > 0 && S->cures[i] < R->spells.count &&
              pascal_is(ds(0x5825 + 41u * S->cures[i]), R->spells.names[S->cures[i] - 1], false));
    /* Raise Dead and Restoration: the spell (mov byte [be70], id) and its
     * handler from DS:c150 + 4 * id (call far [..]); Remove Curse calls
     * 5dc5:311b, the handler of its id. */
    CHECK(HOLDS(0x3999, 0x01fc, 0xa4, 0xc6, 0x06, 0x70, 0xbe, S->cures[7], 0xff, 0x1e,
                (uint8_t)(0xc150 + 4 * S->cures[7]), (uint8_t)((0xc150 + 4 * S->cures[7]) >> 8)));
    CHECK(HOLDS(0x3999, 0x045f, 0x83, 0xc6, 0x06, 0x70, 0xbe, S->cures[10], 0xff, 0x1e,
                (uint8_t)(0xc150 + 4 * S->cures[10]), (uint8_t)((0xc150 + 4 * S->cures[10]) >> 8)));
    unsigned long calls[8];
    size_t n = far_calls(0x3999, 0x0368, 0x9c, calls, 8);
    CHECK(n >= 1 && calls[n - 1] == 0x5dc5311b && R->spells.cast[S->cures[8]].address == 0x311b);
    /* Mode 1's redraw: one branch with mode 7's (cmp al, 7; jz; cmp al, 1),
     * the small picture always (6ca1:0000). */
    CHECK(HOLDS(0x65cd, 0x2db0, 0x30, 0x3c, 0x07, 0x74, 0x04, 0x3c, 0x01));
    n = far_calls(0x65cd, 0x2db0, 0x60, calls, 8);
    bool picture = false;
    for (size_t i = 0; i < n; ++i) picture |= calls[i] == 0x6ca10000;
    CHECK(picture);
    puts("death knights: the shops section checked");
}

/* The levels section against the original's code: Train (49cb:43b6) and
 * its hit points (580d:3193), may-train's (65cd:38c7) and Knight Change
 * Order's (49cb:4ea4, 4da1) tables and words, the aging after a battle
 * (3e05:0264), the memorize table (4381:09c7), the spell lists' prompts
 * (5dc5:016e), Scribe's (4381:115d), the camp's forget (0626) and
 * Display's texts and bound (15aa). */
static void test_levels(void)
{
    const cok_rules_levels *V = &R->levels;
    const cok_data_segment *D = R->data;
    /* Train: mov byte [c46a], 0 first (no free training); the highest
     * levels' table (mov al, [di-0x7d2c]) and the experience table's base
     * (mov ax, [di+0x7638]) in it and in may-train; its words (mov di,
     * DS:x); the question's Yes first (mov al, [57c0], DS:57c0 0); the
     * routines it calls. */
    CHECK(HOLDS(0x49cb, 0x43b6, 0x10, 0xc6, 0x06, 0x6a, 0xc4, 0x00) && !V->free_training);
    CHECK(HOLDS(0x49cb, 0x43b6, 0x100, 0x8a, 0x85, 0xd4, 0x82) && V->race_limits &&
          cok_data_offset2(D, COK_DATA_LEVEL_LIMITS, 0, 0) == 0x82d4);
    CHECK(HOLDS(0x49cb, 0x43b6, 0x200, 0x8b, 0x85, 0x38, 0x76) &&
          HOLDS(0x65cd, 0x38c7, 0xf0, 0x8b, 0x85, 0x38, 0x76) &&
          cok_data_offset2(D, COK_DATA_EXPERIENCE, 0, 0) == 0x7638);
    static const struct {
        unsigned at;
        const char *const *text;
    } words[] = {
        {0x1542, &R->levels.conscious},    {0x1561, &R->levels.wrong_hall},
        {0x1412, &R->levels.no_experience}, {0x1427, &R->levels.will_become},
        {0x143c, &R->levels.a_level},       {0x1451, &R->levels.and_level},
        {0x1466, &R->levels.question},      {0x1485, &R->levels.congratulations},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i)
        CHECK(pascal_is(ds(words[i].at), *words[i].text, false) &&
              HOLDS(0x49cb, 0x43b6, 0x830, 0xbf, (uint8_t)words[i].at,
                    (uint8_t)(words[i].at >> 8)));
    CHECK(HOLDS(0x49cb, 0x43b6, 0x830, 0xa0, 0xc0, 0x57) && *ds(0x57c0) == 0 && V->question_yes);
    /* (The far calls up to 4ac0 and from 4ad4: a mov byte [es:di+0x9a]
     * lies between.) */
    unsigned long calls[64];
    size_t n = far_calls(0x49cb, 0x43b6, 0x4ac0 - 0x43b6, calls, 32);
    n += far_calls(0x49cb, 0x4ad4, 0x4be6 - 0x4ad4, calls + n, 32);
    bool hp = false, may = false, learn = false;
    for (size_t i = 0; i < n; ++i) {
        hp |= calls[i] == 0x580d3193;
        may |= calls[i] == 0x65cd38c7;
        learn |= calls[i] == 0x580d2d2a;
    }
    CHECK(hp && may && learn && R->combat.may_train && V->stale_row == 0 && V->ranger_rises);
    /* The hit points' fixed amounts (580d:3193 from 3287: cmp al, class
     * ... mov byte [bp-3], amount), the floor (mov byte [es:di+0xd6], 0). */
    uint8_t fixed[8] = {0}, pending[8];
    size_t count = 0;
    const uint8_t *b = at(0x580d, 0x3287);
    for (size_t i = 0; i < 0x30; ++i) {
        if (b[i] == 0x3c && count < 8) pending[count++] = b[i + 1];
        if (b[i] == 0xc6 && b[i + 1] == 0x46 && b[i + 2] == 0xfd) {
            for (size_t k = 0; k < count; ++k) fixed[pending[k]] = b[i + 3];
            count = 0;
        }
    }
    CHECK(memcmp(fixed, V->top_hit_points, 8) == 0 && V->hit_points_floor &&
          HOLDS(0x580d, 0x3193, 0x2c1, 0x26, 0xc6, 0x85, 0xd6, 0x00, 0x00));
    /* Knight Change Order: its words, the menu always "Change Exit"
     * (DS:3361, the only menu), Escape apart (cmp byte [c0f8], 0); the
     * experience it needs (EXPERIENCE 4 and 5 at levels 3 and 4: cmp dx,
     * [7976], [7a46]) and the scores (DS:05a4, 05aa); 4da1's fixed loss
     * from level 10 (cmp byte [bp-1], 9; mov byte [bp-3], 2). */
    CHECK(pascal_is(ds(0x14cf), V->knight_of, false) && pascal_is(ds(0x14b9), V->granted[0], false) &&
          pascal_is(ds(0x14c4), V->granted[1], false) &&
          pascal_is(ds(0x14e4), V->inexperienced[0], false) &&
          pascal_is(ds(0x14e4), V->inexperienced[1], false) && V->no_order == NULL);
    CHECK(pascal_is(ds(0x3361), "Change Exit", false) && V->change_always && V->specials_apart &&
          HOLDS(0x49cb, 0x4ea4, 0x429, 0xbf, 0x61, 0x33) &&
          HOLDS(0x49cb, 0x4ea4, 0x429, 0x80, 0x3e, 0xf8, 0xc0, 0x00));
    CHECK(cok_data_offset2(D, COK_DATA_EXPERIENCE, 4, 3) == 0x7974 &&
          cok_data_offset2(D, COK_DATA_EXPERIENCE, 5, 4) == 0x7a44 &&
          HOLDS(0x49cb, 0x4ea4, 0x429, 0x3b, 0x16, 0x76, 0x79) &&
          HOLDS(0x49cb, 0x4ea4, 0x429, 0x3b, 0x16, 0x46, 0x7a) &&
          cok_data_offset2(D, COK_DATA_KNIGHT_SCORES, 0, 0) == 0x05a4 &&
          cok_data_offset2(D, COK_DATA_KNIGHT_SCORES, 1, 0) == 0x05aa);
    CHECK(HOLDS(0x49cb, 0x4da1, 0x103, 0x80, 0x7e, 0xff, (uint8_t)(V->promote_fixed - 1)) &&
          HOLDS(0x49cb, 0x4da1, 0x103, 0xc6, 0x46, 0xfd, 0x02) &&
          far_calls(0x49cb, 0x4da1, 0x103, calls, 64) >= 3 && calls[2] == 0x65cd38c7);
    /* The aging: each race's ages (cmp word [es:di+0x26], age, the byte
     * form below 0x80), its words. */
    CHECK(V->age_races == 7);
    for (unsigned r = 0; r < V->age_races; ++r)
        for (unsigned k = 0; k < 2; ++k) {
            uint16_t a = k == 0 ? V->ages[r].old : V->ages[r].dies;
            CHECK(a < 0x80 ? HOLDS(0x3e05, 0x0264, 0x140, 0x26, 0x83, 0x7d, 0x26, (uint8_t)a)
                           : HOLDS(0x3e05, 0x0264, 0x140, 0x26, 0x81, 0x7d, 0x26, (uint8_t)a,
                                   (uint8_t)(a >> 8)));
        }
    CHECK(pascal_is(ds(0x2319), V->very_old, false) && pascal_is(ds(0x2338), V->strain, false) &&
          pascal_is(at(0x3e05, 0x0260), V->strain_end, false) &&
          pascal_is(ds(0x23da), V->he, false) && pascal_is(ds(0x23e5), V->she, false) &&
          pascal_is(ds(0x2389), V->last_breath, false) && pascal_is(ds(0x23f0), V->died, false));
    /* The memorize table: labels (mov di, DS:x), 8 levels (cmp byte
     * [bp-0x90], 8), the first column (mov byte [bp-0x91], 0xd, + 1), the
     * bonus's (mov al, 0xe). */
    static const unsigned labels[4] = {0x2e9f, 0x2eb4, 0x2ec9, 0x2ede};
    for (unsigned k = 0; k < 4; ++k)
        CHECK(pascal_is(ds(labels[k]), V->table_labels[k], false));
    CHECK(pascal_is(ds(0x2ef3), V->bonus_label, false) &&
          HOLDS(0x4381, 0x09c7, 0x427, 0x80, 0xbe, 0x70, 0xff,
                (uint8_t)cok_field_count(R->character, COK_FIELD_SPELLS_A_DAY)) &&
          HOLDS(0x4381, 0x09c7, 0x427, 0xc6, 0x86, 0x6f, 0xff, (uint8_t)(V->table_column - 1)) &&
          HOLDS(0x4381, 0x09c7, 0x427, 0xb0, V->bonus_column));
    /* The table clears its rows (110f:0584) and says no name (no
     * 65cd:1a21); 4381:0664's granted powers' local is not known. */
    {
        size_t m = far_calls(0x4381, 0x09c7, 0x427, calls, 64);
        bool clears = false, says = false;
        for (size_t i = 0; i < m; ++i) {
            clears |= calls[i] == 0x110f0584;
            says |= calls[i] == 0x65cd1a21;
        }
        CHECK(clears && !says && !V->table_name && V->granted_stale == 0);
    }
    /* The lists' prompt, the marked spells' Keep and its prompt, Scribe's
     * question (Yes first) and words. */
    CHECK(pascal_is(ds(0x2fd9), V->choose_prompt, false) && pascal_is(ds(0x31f7), V->keep, false) &&
          pascal_is(ds(0x30ab), V->keep_prompt, false) &&
          HOLDS(0x5dc5, 0x016e, 0x210, 0xbf, 0xab, 0x30));
    CHECK(pascal_is(ds(0x30c0), V->scribe_question[0], false) &&
          pascal_is(ds(0x30c0), V->scribe_question[1], false) && V->questions_yes &&
          HOLDS(0x4381, 0x115d, 0x60, 0xa0, 0xc0, 0x57) &&
          pascal_is(ds(0x2f46), V->known, false) && pascal_is(ds(0x2f65), V->scribing, false) &&
          pascal_is(ds(0x2f8e), V->cannot_scribe, false));
    /* The camp's forget, 4381:0626: 05b0 (the scrolls) alone, not 0556. */
    CHECK(near_call(0x4381, 0x0626, 0x3e, 0x05b0) && !near_call(0x4381, 0x0626, 0x3e, 0x0556) &&
          !V->camp_forgets_memorized);
    /* Display: spells 1-0x3f (cmp byte [bp-0x12], 0x3f) by DS:6a8a's
     * effect; its texts in its code (4381:148c on). */
    CHECK(R->spells.display_last == 0x3f &&
          HOLDS(0x4381, 0x15aa, 0x200, 0x80, 0x7e, 0xee, R->spells.display_last) &&
          cok_data_offset2(D, COK_DATA_SPELLS, 0, 10) == 0x6a8a);
    static const unsigned texts[] = {0x148c, 0x149f, 0x14ab, 0x14ba, 0x14c3, 0x14cc, 0x14d5,
                                     0x14e3, 0x14f3, 0x1504, 0x150d, 0x1517, 0x1527, 0x1531,
                                     0x1546, 0x1550, 0x155c, 0x1579, 0x1584, 0x158c};
    CHECK(R->effects.display_text_count == sizeof texts / sizeof *texts);
    for (size_t i = 0; i < sizeof texts / sizeof *texts; ++i)
        CHECK(pascal_is(at(0x4381, texts[i]), R->effects.display_texts[i].text, false));
    /* The lists' heading colour, DS:c149 (startup 4f9a:00e1: 13; read by 6a69:15d0). */
    CHECK(V->display_heading == 13 && HOLDS(0x4f9a, 0x00e1, 0x800, 0xc6, 0x06, 0x49, 0xc1, 0x0d) &&
          HOLDS(0x6a69, 0x15bd, 0x20, 0xa0, 0x49, 0xc1));
    /* Camp's Fix (4381:2fd4): nothing where var 0x4be5 forbids magic (cmp
     * byte [es:di+0xe5], 0); DS:c46f set around its two rests, which
     * 5b7d:09b9 (memorize) and 0aa3 (scribe) test first; the spells it
     * counts (22db: cmp al, id), its times and estimates (2537: 15, 30,
     * 60, 75, 90 minutes, 240 / 360 / 480, 27, 34 / 78, 90), the second
     * rest's minutes (2779: 90, 90, 75, 60, 45, 45) and the cures' words
     * (its code's, 29ff on); the rest's words (5b7d:0912, 0d51). */
    CHECK(V->fix == COK_FIX_DEATH_KNIGHTS &&
          HOLDS(0x4381, 0x2fd4, 0x10, 0x26, 0x80, 0xbd, 0xe5, 0x00, 0x00));
    CHECK(HOLDS(0x4381, 0x303c, 0x5, 0xc6, 0x06, 0x6f, 0xc4, 0x01) &&
          HOLDS(0x4381, 0x30b1, 0x5, 0xc6, 0x06, 0x6f, 0xc4, 0x01) &&
          HOLDS(0x5b7d, 0x09b9, 0x10, 0x80, 0x3e, 0x6f, 0xc4, 0x00) &&
          HOLDS(0x5b7d, 0x0aa3, 0x10, 0x80, 0x3e, 0x6f, 0xc4, 0x00));
    static const uint8_t counted[] = {0x03, 0x3a, 0x47, 0x35, 0x43, 0x4b, 0x2b, 0x59, 0x42, 0x25};
    for (size_t i = 0; i < sizeof counted; ++i)
        CHECK(HOLDS(0x4381, 0x22db, 0x118, 0x3c, counted[i]));
    CHECK(HOLDS(0x4381, 0x2537, 0x240, 0xb8, 0x0f, 0x00) &&
          HOLDS(0x4381, 0x2537, 0x240, 0xb8, 0x1e, 0x00) &&
          HOLDS(0x4381, 0x2537, 0x240, 0xba, 0x3c, 0x00) &&
          HOLDS(0x4381, 0x2537, 0x240, 0xba, 0x4b, 0x00) &&
          HOLDS(0x4381, 0x2537, 0x240, 0xba, 0x5a, 0x00) &&
          HOLDS(0x4381, 0x2537, 0x240, 0xc7, 0x46, 0xec, 0xf0, 0x00) &&
          HOLDS(0x4381, 0x2537, 0x240, 0xc7, 0x46, 0xec, 0x68, 0x01) &&
          HOLDS(0x4381, 0x2537, 0x240, 0xc7, 0x46, 0xec, 0xe0, 0x01) &&
          HOLDS(0x4381, 0x2537, 0x240, 0x83, 0x46, 0xfe, 0x1b) &&
          HOLDS(0x4381, 0x2537, 0x240, 0x83, 0x46, 0xfe, 0x22) &&
          HOLDS(0x4381, 0x2537, 0x240, 0x83, 0x46, 0xfe, 0x4e) &&
          HOLDS(0x4381, 0x2537, 0x240, 0x83, 0x46, 0xfe, 0x5a));
    static const uint8_t second[] = {0x5a, 0x5a, 0x4b, 0x3c, 0x2d, 0x2d};
    for (size_t i = 0, from = 0x285b; i < sizeof second; ++i) {
        const uint8_t *code = at(0x4381, (unsigned)from);
        size_t k = 0;
        while (k < 0x40 && !(code[k] == 0xba && code[k + 1] == second[i] && code[k + 2] == 0))
            ++k;
        CHECK(k < 0x40);
        from += k + 3;
    }
    static const struct {
        unsigned at;
        const char *text;
    } cures[] = {{0x29ff, "is healed"},
                 {0x2a09, "is changed back to flesh"},
                 {0x2a22, "is detoxified"},
                 {0x2a30, "has been raised from the dead"},
                 {0x2a4e, "is relieved of a curse"},
                 {0x2a65, "regains sight"}};
    for (size_t i = 0; i < sizeof cures / sizeof *cures; ++i)
        CHECK(pascal_is(at(0x4381, cures[i].at), cures[i].text, false));
    CHECK(pascal_is(ds(0x2560), V->party_healed, false) &&
          HOLDS(0x5b7d, 0x0912, 0x90, 0xbf, 0x60, 0x25) &&
          pascal_is(ds(0x25a9), V->interrupted, false) &&
          HOLDS(0x5b7d, 0x0d51, 0x200, 0xbf, 0xa9, 0x25));
    /* The interactive rest: its menu (mov di, 324d in 081d, its items 0-6
     * by 6a69:00cb's push 6), "Stop resting? " (mov di, 10a1) from Yes
     * (mov al, [57c0], which holds 0, Yes's index), and the camp's Rest
     * (4381:3281) calling 0dee with DS:c0f7 left as it is. */
    CHECK(pascal_is(ds(0x324d), V->rest_menu, false) &&
          HOLDS(0x5b7d, 0x081d, 0x30, 0xb0, 0x06, 0x50, 0x9a) &&
          HOLDS(0x5b7d, 0x081d, 0x30, 0xbf, 0x4d, 0x32));
    CHECK(pascal_is(ds(0x10a1), V->stop_resting, false) &&
          HOLDS(0x5b7d, 0x0e30, 0x08, 0xbf, 0xa1, 0x10, 0x1e, 0x57, 0xa0, 0xc0, 0x57) &&
          *ds(0x57c0) == 0 && V->stop_resting_yes);
    CHECK(HOLDS(0x4381, 0x327d, 0x0e, 0x3c, 0x03, 0x75, 0x0b, 0xc4, 0x7e, 0x06, 0x06, 0x57, 0x0e,
                0xe8) &&
          !V->rest_from_first);
    /* The camp's Save asks its start menu's "Quit to DOS " (mov di, 124c)
     * from No (mov al, [57c1]). */
    CHECK(pascal_is(ds(0x124c), V->camp_quit, false) &&
          HOLDS(0x4381, 0x3243, 0x08, 0xbf, 0x4c, 0x12, 0x1e, 0x57, 0xa0, 0xc1, 0x57));
    /* The camp's menus by index (6a69:0f1b's last two arguments, keypad
     * and picture, pushed as mov al, n; push ax): the camp (31b3) and
     * Magic (1a9a) both, Alter (2217) and Level (2074) the picture alone,
     * Order (1dfb) and Speed (216d) both; their words. The camp's Escape
     * is Exit (31ce: cmp byte [bp-2], 1b; jnz; mov byte [bp-2], 6), Alter's
     * too (2232: ... 5), Alter's from its first item each time (21fe: mov
     * byte [c0f7], 1); Magic's own selection from Cast (1a3e: mov byte
     * [bp-2], 1). */
    CHECK(pascal_is(ds(0x36cc), "Save View Magic Rest Alter Fix Exit", false) &&
          HOLDS(0x4381, 0x31a9, 0x10, 0xbf, 0xcc, 0x36, 0x1e, 0x57, 0xb0, 0x01, 0x50, 0xb0, 0x01,
                0x50) &&
          HOLDS(0x4381, 0x31ce, 0x0a, 0x80, 0x7e, 0xfe, 0x1b, 0x75, 0x06, 0xc6, 0x46, 0xfe, 0x06));
    CHECK(pascal_is(ds(0x36f5), "Cast Memorize Scribe Display Rest Exit", false) &&
          HOLDS(0x4381, 0x1a95, 0x10, 0xbf, 0xf5, 0x36, 0x1e, 0x57, 0xb0, 0x01, 0x50, 0xb0, 0x01,
                0x50) &&
          HOLDS(0x4381, 0x1a3e, 0x04, 0xc6, 0x46, 0xfe, 0x01));
    CHECK(pascal_is(ds(0x371e), V->alter_menu, false) &&
          HOLDS(0x4381, 0x2212, 0x10, 0xbf, 0x1e, 0x37, 0x1e, 0x57, 0xb0, 0x00, 0x50, 0xb0, 0x01,
                0x50) &&
          HOLDS(0x4381, 0x21fe, 0x05, 0xc6, 0x06, 0xf7, 0xc0, 0x01) &&
          HOLDS(0x4381, 0x2232, 0x0a, 0x80, 0x7e, 0xff, 0x1b, 0x75, 0x06, 0xc6, 0x46, 0xff, 0x05));
    CHECK(pascal_is(ds(0x3747), "Novice Squire Veteran Adept Champion", false) &&
          HOLDS(0x4381, 0x206f, 0x10, 0xbf, 0x47, 0x37, 0x1e, 0x57, 0xb0, 0x00, 0x50, 0xb0, 0x01,
                0x50));
    CHECK(pascal_is(ds(0x30df), V->order_prompt, false) &&
          pascal_is(ds(0x33f5), V->order_menu, false) &&
          HOLDS(0x4381, 0x1df1, 0x12, 0xbf, 0xf5, 0x33, 0x1e, 0x57, 0xb0, 0x01, 0x50, 0xb0, 0x01,
                0x50));
    CHECK(pascal_is(ds(0x110a), V->drop_gone, false) &&
          HOLDS(0x4381, 0x1f71, 0x04, 0xbf, 0x0a, 0x11) &&
          pascal_is(ds(0x124c), V->drop_quit, false) &&
          HOLDS(0x4381, 0x1f3d, 0x04, 0xbf, 0x4c, 0x12));
    CHECK(pascal_is(ds(0x168e), V->speed_before, false) && V->speed_after[0] == '\0' &&
          pascal_is(ds(0x30f4), V->speed_prompt, false) &&
          pascal_is(ds(0x340a), V->speed_menu, false) &&
          HOLDS(0x4381, 0x20f0, 0x04, 0xbf, 0x8e, 0x16) &&
          HOLDS(0x4381, 0x2163, 0x14, 0xbf, 0x0a, 0x34, 0x1e, 0x57, 0xb0, 0x01, 0x50, 0xb0, 0x01,
                0x50));
    /* The Save's count at [8c2d] + 0x1f6, 0x4cf6 (inc byte [es:di+1f6]),
     * every tenth calling 0337 (push cs; call 0337). */
    CHECK(HOLDS(0x4381, 0x320d, 0x0a, 0x26, 0xfe, 0x85, 0xf6, 0x01) &&
          HOLDS(0x4381, 0x3222, 0x04, 0x0e, 0xe8, 0x11, 0xd1) &&
          strcmp(V->copy_protection, "4381:0337") == 0);
    puts("death knights: the levels' routines checked");
}

/* The effects part's handlers against the original's code: the constants
 * each entry gives its kind (cok_rules_effect's value and words), the
 * breaths' table (5dc5:51cc), the battle-only entries (a first cmp byte
 * [8c4e], 5), and the words the routines say. */
static void test_effects(void)
{
    const cok_rules_effects *E = &R->effects;
    const cok_rules_effect *h = E->handlers;
    /* The armour: sub/add byte [es:di+0xcc], n. */
    CHECK(h[0x0d].handler == COK_FX_ARMOUR && h[0x0d].value == 0xfe &&
          HOLDS(0x504d, 0x04a5, 0x20, 0x26, 0x80, 0xad, 0xcc, 0x00, 0x02));
    CHECK(h[0x39].handler == COK_FX_ARMOUR && h[0x39].value == 0xfc &&
          HOLDS(0x504d, 0x1044, 0x20, 0x26, 0x80, 0xad, 0xcc, 0x00, 0x04));
    CHECK(h[0x3c].handler == COK_FX_ARMOUR && h[0x3c].value == 4 &&
          HOLDS(0x504d, 0x10b4, 0x20, 0x26, 0x80, 0x85, 0xcc, 0x00, 0x04));
    /* The globes: cmp byte [di+0x6a81] (the spell's level), n. */
    CHECK(h[0x3b].handler == COK_FX_MINOR_GLOBE && h[0x3b].value == 5 &&
          HOLDS(0x504d, 0x108d, 0x20, 0x80, 0xbd, 0x81, 0x6a, 0x05));
    CHECK(h[0x3f].handler == COK_FX_MINOR_GLOBE && h[0x3f].value == 4 &&
          HOLDS(0x504d, 0x111e, 0x20, 0x80, 0xbd, 0x81, 0x6a, 0x04) &&
          cok_data_offset2(R->data, COK_DATA_SPELLS, 0, 1) == 0x6a81);
    /* 0x42: cmp byte [es:di+0x32], 3. */
    CHECK(h[0x42].handler == COK_FX_PLUS_WEAPONS && h[0x42].value == 3 &&
          HOLDS(0x504d, 0x123d, 0x30, 0x26, 0x80, 0x7d, 0x32, 0x03));
    /* The damage types: and al, mask of DS:be6d. */
    CHECK(h[0x41].handler == COK_FX_TYPE_NONE && h[0x41].value == 4 &&
          HOLDS(0x504d, 0x1228, 0x10, 0xa0, 0x6d, 0xbe, 0x24, 0x04));
    CHECK(h[0x4b].handler == COK_FX_TYPE_DOUBLE && h[0x4b].value == 2 &&
          HOLDS(0x504d, 0x1c64, 0x10, 0xa0, 0x6d, 0xbe, 0x24, 0x02));
    CHECK(h[0x53].handler == COK_FX_TYPE_HALF && h[0x53].value == 6 &&
          HOLDS(0x504d, 0x1f79, 0x10, 0x24, 0x04) && HOLDS(0x504d, 0x1f79, 0x18, 0x24, 0x02));
    /* The drains: mov al, levels; push ax before the far call of 14c7. */
    CHECK(h[0x43].handler == COK_FX_DRAIN_LEVELS && h[0x43].value == 1 &&
          HOLDS(0x504d, 0x1815, 0x20, 0xb0, 0x01, 0x50));
    CHECK(h[0x44].handler == COK_FX_DRAIN_LEVELS && h[0x44].value == 2 &&
          HOLDS(0x504d, 0x1836, 0x20, 0xb0, 0x02, 0x50));
    /* 0x54: add byte [es:di+0xd6], 3. 0x6e: mov al, [es:di+0x45 + class]. */
    CHECK(h[0x54].handler == COK_FX_HEAL_ROUND && h[0x54].value == 3 &&
          HOLDS(0x504d, 0x1f9c, 0x30, 0x26, 0x80, 0x85, 0xd6, 0x00, 0x03));
    CHECK(h[0x6e].handler == COK_FX_TIER && h[0x6e].value == 3 &&
          R->character->field[COK_FIELD_CLASS_LEVEL].offset + 3 == 0x48 &&
          HOLDS(0x504d, 0x26f8, 0x20, 0x26, 0x8a, 0x45, 0x48));
    /* 0x4e: mov al, 0x2f (Fireball), the cast entry; 0x51: mov al, 0x0a,
     * "gazes!" at DS:22cd; 0x55's words at DS:22d8; the hammer's DS:1dea. */
    CHECK(h[0x4e].handler == COK_FX_CAST && h[0x4e].value == 0x2f &&
          HOLDS(0x5dc5, 0x4f97, 0x10, 0xb0, 0x2f, 0x50));
    CHECK(h[0x51].handler == COK_FX_GAZE && h[0x51].value == 0x0a &&
          HOLDS(0x5dc5, 0x4fd3, 0x10, 0xb0, 0x0a, 0x50) &&
          HOLDS(0x5dc5, 0x4fd3, 0x40, 0xbf, 0xcd, 0x22) && pascal_is(ds(0x22cd), h[0x51].text, false));
    CHECK(h[0x55].handler == COK_FX_BREATH && HOLDS(0x5dc5, 0x508c, 0x80, 0xbf, 0xd8, 0x22) &&
          pascal_is(ds(0x22d8), h[0x55].text, false));
    CHECK(h[0x17].handler == COK_FX_HAMMER && HOLDS(0x504d, 0x06dc, 0x200, 0xbf, 0xea, 0x1d) &&
          pascal_is(ds(0x1dea), h[0x17].text, false));
    /* The breaths: cmp al, id; mov byte [be6d], type; mov byte [bp-0x0a],
     * width; mov byte [bp-0x0b], length; mov di, the word (DS:22d8's
     * "breathes " and it); the pick's mov al, 0x24. */
    CHECK(E->breath_count == 3 && pascal_is(ds(0x22d8), "breathes ", false) &&
          HOLDS(0x5dc5, 0x51cc, 0xd0, 0xb0, 0x24, 0x50, 0xb0, 0x01));
    for (unsigned i = 0; i < E->breath_count; ++i) {
        const cok_rules_breath *b = &E->breaths[i];
        static const uint16_t words[] = {0x22e3, 0x22ee, 0x22f9};
        CHECK(h[b->id].handler == COK_FX_DRAGON_BREATH && h[b->id].address == 0x51cc &&
              b->spell == 0x24 && HOLDS(0x5dc5, 0x51cc, 0x80, 0x3c, b->id) &&
              HOLDS(0x5dc5, 0x51cc, 0xa0, 0xc6, 0x06, 0x6d, 0xbe, b->damage_type, 0xc6, 0x46,
                    0xf6, b->width, 0xc6, 0x46, 0xf5, b->length, 0xbf, (uint8_t)words[i],
                    (uint8_t)(words[i] >> 8)) &&
              pascal_is(ds(words[i]), b->what, false));
    }
    /* The cone (5dc5:175d) doubles its length in place (mov al, [bp+6]; xor
     * ah, ah; shl ax, 1; mov [bp+6], al) before it copies it to the sight's
     * range (mov al, [bp+6]; xor ah, ah; mov [bp-0x78], ax): the breath's
     * range twice its length. */
    CHECK(HOLDS(0x5dc5, 0x17b9, 0x0a, 0x8a, 0x46, 0x06, 0x30, 0xe4, 0xd1, 0xe0, 0x88, 0x46, 0x06) &&
          HOLDS(0x5dc5, 0x1886, 0x08, 0x8a, 0x46, 0x06, 0x30, 0xe4, 0x89, 0x46, 0x88));
    /* Flames: the burst's n 1 and slot 0x18 with DS:4316's sound. */
    CHECK(h[0x46].handler == COK_FX_FLAMES &&
          HOLDS(0x504d, 0x186e, 0x40, 0xb0, 0x01, 0x50, 0xb0, 0x18, 0x50, 0xa0, 0x16, 0x43));
    /* The battle-only entries of Champions' kinds test cmp byte [8c4e], 5
     * in their first 12 bytes; the others of 504d do not. */
    for (unsigned id = 1; id < E->count; ++id) {
        if (h[id].unported != NULL || h[id].handler >= COK_FX_KENDER_BATTLE ||
            h[id].handler == COK_FX_BREATH || h[id].handler == COK_FX_ITEM)
            continue;
        bool tests = HOLDS(0x504d, h[id].address, 0x0c, 0x80, 0x3e, 0x4e, 0x8c, 0x05);
        if (tests != h[id].battle) printf("effect 0x%02x: battle %d, the routine %d\n", id,
                                          h[id].battle, tests);
        CHECK(tests == h[id].battle);
    }
    /* The words the routines say. */
    static const struct {
        uint16_t at;
        const char *text;
    } said[] = {
        {0x1f30, "is healed"},    {0x1e14, "is coughing"},   {0x1dcb, "collapses from poison"},
        {0x1f7a, "is disrupted"}, {0x4570, "is "},           {0x4bcb, "Slowed"},
        {0x1ebd, "gets drained"}, {0x1ed2, "dies from drainning"},
        {0x1ee7, "collapses from drainning"},
    };
    for (size_t i = 0; i < sizeof said / sizeof *said; ++i)
        CHECK(pascal_is(ds(said[i].at), said[i].text, false));
    /* The drain: 14c7's longint division by 2 (mov cx, 2; xor bx, bx),
     * the hit points' call with 0 (lose) and the experience table's base
     * at DS:7638 + 0xcc a row. */
    CHECK(HOLDS(0x504d, 0x14c7, 0x280, 0xb9, 0x02, 0x00, 0x31, 0xdb) &&
          HOLDS(0x504d, 0x14c7, 0x2c0, 0xb0, 0x00, 0x50, 0x9a) &&
          HOLDS(0x504d, 0x14c7, 0x200, 0x8b, 0x85, 0x38, 0x76) &&
          cok_data_offset2(R->data, COK_DATA_EXPERIENCE, 1, 2) == 0x7638 + 0xcc + 8);
    puts("death knights: the effects' routines checked");
}

/* The band C spells and effects (5dc5's and 504d's routines of their own)
 * against the original's code: every spell and every effect ported; each entry's constants and words where the routine loads
 * them (mov al, imm; push ax; mov di, DS offset), the strings the kinds
 * say, Restoration's experience bound (DS:7a6c). */
static void test_band_c(void)
{
    const cok_rules_cast *c = R->spells.cast;
    const cok_rules_effect *h = R->effects.handlers;
    for (unsigned id = 1; id < R->spells.count; ++id)
        CHECK(c[id].unported == NULL);
    for (unsigned id = 1; id < R->effects.count; ++id) CHECK(h[id].unported == NULL);
    /* Disintegrate and Flesh to Stone: the status and the words of the kill
     * (638c:00c8). */
    CHECK(c[0x3c].handler == COK_CAST_DESTROY && c[0x3c].value == 6 &&
          HOLDS(0x5dc5, 0x3bb9, 0x60, 0xb0, 0x06, 0x50, 0xbf, 0xa6, 0x21) &&
          pascal_is(ds(0x21a6), c[0x3c].text, false));
    CHECK(c[0x48].handler == COK_CAST_DESTROY && c[0x48].value == 7 &&
          HOLDS(0x5dc5, 0x40a6, 0x60, 0xb0, 0x07, 0x50, 0xbf, 0x04, 0x22) &&
          pascal_is(ds(0x2204), c[0x48].text, false));
    /* Heal cures 0x21, then the entry's 0x3d (638c:1479). */
    CHECK(c[0x35].value == 0x3d && HOLDS(0x5dc5, 0x3848, 0x40, 0xb0, 0x21, 0x50, 0x9a) &&
          HOLDS(0x5dc5, 0x3848, 0x70, 0xb0, 0x3d, 0x50, 0x9a));
    /* Dispel Evil's caster's effect, timed by 0x49 (5dc5:0ef5). */
    CHECK(c[0x49].value == 0x04 && HOLDS(0x5dc5, 0x4109, 0x20, 0xb0, 0x04, 0x50, 0xb0, 0x49, 0x50));
    /* Entangle's effect, timed by 0x4e. */
    CHECK(c[0x4e].value == 0x06 && HOLDS(0x5dc5, 0x42f1, 0x80, 0xb0, 0x06, 0x50, 0xb0, 0x4e, 0x50));
    /* Feeblemind's effect run (638c:0185). */
    CHECK(c[0x5d].value == 0x44 && HOLDS(0x5dc5, 0x4a04, 0x90, 0xb0, 0x44, 0x50, 0x8d));
    /* Otto's two effects, timed by 0x71. */
    CHECK(c[0x71].value == 0x06 && c[0x71].count == 0x39 &&
          HOLDS(0x5dc5, 0x4dd8, 0x60, 0xb0, 0x06, 0x50, 0xb0, 0x71, 0x50) &&
          HOLDS(0x5dc5, 0x4dd8, 0x90, 0xb0, 0x39, 0x50, 0xb0, 0x71, 0x50));
    /* Effect 0x02's: 6 for 3 minutes (mov al, 6; push ax; mov ax, 3). */
    CHECK(h[0x02].value == 0x06 && HOLDS(0x504d, 0x0124, 0x60, 0xb0, 0x06, 0x50, 0xb8, 0x03, 0x00));
    /* Restoration: the least experience starts at DS:7a6c's 3500000. */
    const uint8_t *least = ds(0x7a6c);
    CHECK(least[0] == 0xe0 && least[1] == 0x67 && least[2] == 0x35 && least[3] == 0 &&
          HOLDS(0x5dc5, 0x2326, 0x20, 0xa1, 0x6c, 0x7a));
    static const struct {
        uint16_t at;
        const char *text;
    } said[] = {
        {0x1f8f, "is ressurected"}, {0x2219, "is raised"},       {0x2224, "is slain"},
        {0x2191, "drops dead"},     {0x20a0, "can see"},         {0x2167, "regains intellect"},
        {0x21bb, "is no longer a status"},                      {0x222f, "is entangled"},
        {0x226f, "is now 18 years old"},                        {0x2284, "becomes younger"},
        {0x1d82, "is stunned"},     {0x1fb9, "is charmed"},      {0x22ae, "begins to dance wildly"},
        {0x20b5, "is blind"},       {0x1e29, "is dispelled"},    {0x1e3e, "resists dispel evil"},
        {0x1db6, "is silenced"},    {0x1c3a, "lost a spell"},    {0x1d0f, "is unaffected"},
        {0x4731, "Charmed"},        {0x4574, "not "},
    };
    for (size_t i = 0; i < sizeof said / sizeof *said; ++i)
        CHECK(pascal_is(ds(said[i].at), said[i].text, false));
    CHECK(strcmp(R->effects.unaffected, "is unaffected") == 0 &&
          strcmp(R->casting.lost_spell, "lost a spell") == 0);
    puts("death knights: the band C spells and effects checked");
}

/* The creation section against the original's code: Create New Character
 * (49cb:05c7: its headings, words and questions' first items, the class
 * switch from 0ba3, its levels, experience and effects, the classes with
 * no spells' block, the mage's spells, the steel), creation's training
 * (49cb:43b6 from 4a57: a mage's spells by order and level), the items by
 * kind (49cb:4bea), Modify (49cb:25d4: the experiences it allows, its
 * prompt, the charisma held at 17), the icon editor (49cb:3cac, 39d6: its
 * words, the frame's slot, the most head and body). */
static void test_creation(void)
{
    const cok_rules_creation *C = &R->creation;
    static const unsigned headings[5] = {0x1280, 0x128b, 0x12a0, 0x12ab, 0x12b6};
    for (size_t i = 0; i < 5; ++i)
        CHECK(pascal_is(ds(headings[i]), C->headings[i], false) &&
              HOLDS(0x49cb, 0x05c7, 0x1400, 0xbf, (uint8_t)headings[i],
                    (uint8_t)(headings[i] >> 8)));
    CHECK(pascal_is(ds(0x12cb), C->reroll, false) && pascal_is(ds(0x12e0), C->name_prompt, false) &&
          pascal_is(ds(0x12f5), C->save, false) && !C->save_names);
    /* reroll stats? from No (DS:57c1 1), save character? from Yes (57c0 0). */
    CHECK(*ds(0x57c0) == 0 && *ds(0x57c1) == 1);
    CHECK(HOLDS(0x49cb, 0x22a0, 0x10, 0xa0, 0xc1, 0x57) && HOLDS(0x49cb, 0x232f, 0x10, 0xa0, 0xc0, 0x57) &&
          C->save_yes);
    /* The class switch: cmp al, class; mov byte [es:di+0x45 + k], level;
     * mov word [es:di+0x62], low and [es:di+0x64], high; an effect's
     * mov al, id before 638c:1257's call. */
    uint8_t levels[17][8] = {{0}}, effect[17] = {0};
    uint32_t experience[17] = {0};
    bool seen[17] = {false};
    int class_ = -1;
    const uint8_t *b = at(0x49cb, 0x0ba3);
    for (size_t i = 0; i < 0x0e22 - 0x0ba3; ++i) {
        if (b[i] == 0x3c && b[i + 1] < 17 && b[i + 2] == 0x75) {
            class_ = b[i + 1];
            seen[class_] = true;
        } else if (class_ >= 0 && b[i] == 0x26 && b[i + 1] == 0xc6 && b[i + 2] == 0x45 &&
                   b[i + 3] >= 0x45 && b[i + 3] < 0x4d) {
            levels[class_][b[i + 3] - 0x45] = b[i + 4];
        } else if (class_ >= 0 && b[i] == 0x26 && b[i + 1] == 0xc7 && b[i + 2] == 0x45 &&
                   (b[i + 3] == 0x62 || b[i + 3] == 0x64)) {
            experience[class_] |= (uint32_t)(b[i + 4] | b[i + 5] << 8) << (b[i + 3] == 0x64 ? 16 : 0);
        } else if (class_ >= 0 && b[i] == 0xb0 && b[i + 2] == 0x50 && b[i + 3] == 0x31) {
            effect[class_] = b[i + 1];
        }
    }
    for (unsigned k = 0; k < 17; ++k) {
        if (!seen[k]) {
            CHECK(k == 1 && C->classes[k].experience == 0);
            continue;
        }
        CHECK(memcmp(levels[k], C->classes[k].levels, 8) == 0);
        CHECK(experience[k] == C->classes[k].experience);
        uint8_t want = k == 4 || k == 10 ? cok_effect_id(R, COK_EFFECT_RANGER)
                       : k == 3          ? C->paladin_effect
                                         : 0;
        CHECK(effect[k] == want);
    }
    /* The classes with no spells' block: cmp byte [es:di+0x21], class. */
    CHECK(HOLDS(0x49cb, 0x0b44, 0x20, 0x26, 0x80, 0x7d, 0x21, C->no_spells[0]) &&
          HOLDS(0x49cb, 0x0b44, 0x20, 0x26, 0x80, 0x7d, 0x21, C->no_spells[1]) &&
          HOLDS(0x49cb, 0x0b44, 0x20, 0x26, 0x80, 0x7d, 0x21, C->no_spells[2]) &&
          C->no_spells[3] == 0xff);
    /* A mage's spells: mov byte [es:di+0x49 + id], 1 in the block, from
     * 20ea; the steel, mov word [es:di+0x3f], 500. */
    uint8_t mage[8] = {0};
    size_t n = 0;
    b = at(0x49cb, 0x20ea);
    for (size_t i = 0; i < 0x2145 - 0x20ea && n < 8; ++i)
        if (b[i] == 0x26 && b[i + 1] == 0xc6 && b[i + 2] == 0x45 && b[i + 4] == 0x01)
            mage[n++] = (uint8_t)(b[i + 3] - 0x49);
    CHECK(memcmp(mage, C->mage_spells, 8) == 0);
    CHECK(HOLDS(0x49cb, 0x2151, 0x10, 0x26, 0xc7, 0x45, 0x3f, (uint8_t)C->steel,
                (uint8_t)(C->steel >> 8)));
    /* Creation's training: from 4a57, the White Robes' (cmp byte
     * [es:di+0x24], 1) to 4b01, then the others'; cmp al, level; each
     * mov byte [es:di+disp], 1 with disp = 0x49 + id. */
    cok_rules_created_spells found[8];
    size_t count = 0;
    b = at(0x49cb, 0x4a57);
    CHECK(b[0] == 0x26 && b[1] == 0x80 && b[2] == 0x7d && b[3] == 0x24 && b[4] == 1);
    for (size_t i = 5, k = 0; i < 0x4bb8 - 0x4a57 && count <= 8; ++i) {
        if (b[i] == 0x3c && i + 2 < 0x4bb8 - 0x4a57 && b[i + 2] == 0x75 && count < 8) {
            found[count] = (cok_rules_created_spells){b[i + 1], 0x4a57 + i < 0x4b01 ? 1 : 0, {0}};
            ++count;
            k = 0;
        } else if (count > 0 && b[i] == 0x26 && b[i + 1] == 0xc6 && k < 4) {
            unsigned disp = b[i + 2] == 0x45 ? b[i + 3] : b[i + 2] == 0x85 ? (unsigned)(b[i + 3] | b[i + 4] << 8) : 0;
            if (disp != 0) found[count - 1].spells[k++] = (uint8_t)(disp - 0x49);
        }
    }
    CHECK(count == C->created_count);
    for (size_t i = 0; i < count; ++i)
        CHECK(found[i].level == C->created[i].level && found[i].order == C->created[i].order &&
              memcmp(found[i].spells, C->created[i].spells, 4) == 0);
    /* The kits: DS:058b + 4 * kind + item, the thief's with a cleric level
     * DS:059c first (mov al, [0x59c]; mov al, [di+0x58b]), and the item
     * not readied (cmp byte [bp-0xa], 0x5b). */
    for (unsigned kind = 1; kind <= 5; ++kind)
        for (unsigned k = 0; k < 4; ++k) CHECK(*ds(0x058b + 4 * kind + k + 1) == C->kits[kind].items[k]);
    CHECK(*ds(0x059c) == C->cleric_thief_item && HOLDS(0x49cb, 0x4bea, 0x1b0, 0xa0, 0x9c, 0x05) &&
          HOLDS(0x49cb, 0x4bea, 0x1b0, 0x8a, 0x85, 0x8b, 0x05) &&
          HOLDS(0x49cb, 0x4bea, 0x1b0, 0x80, 0x7e, 0xf6, C->unready_item));
    /* Modify: cmp word [es:di+0x64], high; cmp word [es:di+0x62], low. */
    CHECK(C->fresh_count == 3);
    for (size_t i = 0; i < C->fresh_count; ++i)
        CHECK(HOLDS(0x49cb, 0x25e7, 0x40, 0x26, 0x83, 0x7d, 0x64, (uint8_t)(C->fresh[i] >> 16)) &&
              HOLDS(0x49cb, 0x25e7, 0x40, 0x26, 0x81, 0x7d, 0x62, (uint8_t)C->fresh[i],
                    (uint8_t)(C->fresh[i] >> 8)));
    CHECK(pascal_is(ds(0x302d), C->modify_prompt, false) && pascal_is(ds(0x3318), "Add Subtract Keep Exit", false));
    CHECK(HOLDS(0x49cb, 0x2d89, 0x40, 0x26, 0x80, 0x7d, 0x20, C->charisma_race) &&
          HOLDS(0x49cb, 0x2d89, 0x40, 0x26, 0x80, 0x7d, 0x21, C->charisma_class) &&
          HOLDS(0x49cb, 0x2d89, 0x40, 0x26, 0x80, 0x7d, 0x11, 0x11) && !C->keep_shares &&
          C->most == COK_MOST_DEATH_KNIGHTS);
    /* The icon editor: its words, mov al, 0x1c before 7090:0484 (39d6),
     * the head's and body's most (DS:72ac, 72ad). */
    CHECK(pascal_is(ds(0x13fd), C->icon_ok, false) && pascal_is(ds(0x13bd), C->no_cga, false) &&
          C->icon_ok_yes && HOLDS(0x49cb, 0x39d6, 0xdf, 0xb0, C->icon_frame) &&
          *ds(0x72ac) == C->heads && *ds(0x72ad) == C->bodies);
    puts("death knights: the creation section checked");
}

/* The mouse and the joystick (DEVELOPERS.md, Death Knights' front end and
 * The mouse and the joystick): Initialize Mouse/Joystick's words and file
 * (49cb:52cd, 58ec), DKK.CFG's two paths (1187:011b) and the code 6a69:0f1b's
 * clicks and 1b82's take their differences from. */
static void test_mouse(void)
{
    const cok_rules_mouse_menu *M = &R->start.mouse;
    CHECK(pascal_is(ds(0x3038), M->prompt, false) && pascal_is(ds(0x31d6), M->select, false) &&
          pascal_is(ds(0x3376), M->mouse_on, false) && pascal_is(ds(0x3381), M->mouse_off, false) &&
          pascal_is(ds(0x338c), M->stick_on, false) && pascal_is(ds(0x33a1), M->stick_off, false) &&
          pascal_is(ds(0x33b6), M->both, false));
    CHECK(pascal_is(at(0x49cb, 0x52bd), M->file, false) &&
          HOLDS(0x49cb, 0x52cd, 0x620, 0xbf, 0xbd, 0x52) &&
          HOLDS(0x49cb, 0x52cd, 0x620, 0xbf, 0x38, 0x30));
    /* The list by index: 6a69:00cb(0) (mov al, 0; push ax; call) before
     * 1b82, Exit shown (push 1 after the list), last 0. */
    CHECK(HOLDS(0x49cb, 0x542b, 0x10, 0xb0, 0x00, 0x50, 0x9a) &&
          HOLDS(0x49cb, 0x544c, 0x10, 0xb0, 0x01, 0x50, 0xb0, 0x00, 0x50));
    CHECK(pascal_is(ds(0x1b2e), M->corners[0][0], false) &&
          pascal_is(ds(0x1b57), M->corners[0][1], false) &&
          pascal_is(ds(0x1b9f), M->corners[1][0], false) &&
          pascal_is(ds(0x1bc8), M->corners[1][1], false) &&
          pascal_is(ds(0x1b80), M->no_button, false) &&
          pascal_is(ds(0x1b80), M->lost_lower_right, false) &&
          pascal_is(at(0x49cb, 0x58d4), M->lost_upper_left, false));
    /* 58ec: the no-button failures and the lower right's lost read from
     * DS:1b80 (mov di, 0x1b80; push ds), the upper left's from CS 58d4. */
    CHECK(HOLDS(0x49cb, 0x59f6, 4, 0xbf, 0x80, 0x1b) &&
          HOLDS(0x49cb, 0x5bc3, 4, 0xbf, 0x80, 0x1b) &&
          HOLDS(0x49cb, 0x5c4f, 4, 0xbf, 0x80, 0x1b) && HOLDS(0x49cb, 0x5a80, 0x10, 0xd4, 0x58));
    /* DKK.CFG: its save path and Champions' (mov di, 0x98a4; 0x99a4). */
    CHECK(R->files.config_paths == 2 && HOLDS(0x1187, 0x0aa8, 3, 0xbf, 0xa4, 0x98) &&
          HOLDS(0x1187, 0x0ac0, 3, 0xbf, 0xa4, 0x99));
    /* 6a69:0f1b's clicks: the party list kept from Move only in mode 4
     * (04b0: cmp byte [0xdb7c], 0; jz; cmp byte [0x8c4e], 4), the view's
     * likewise (080c), the battlefield's T and aiming's M (1286, 12a3,
     * aiming's prompt DS:3109 at 1297); 1b82 takes a row only after a
     * click that set DS:c0f9 (1f5f, 1f69). */
    CHECK(HOLDS(0x6a69, 0x04b0, 0x0e, 0x80, 0x3e, 0x7c, 0xdb, 0x00, 0x74, 0x07, 0x80, 0x3e, 0x4e,
                0x8c, 0x04) &&
          HOLDS(0x6a69, 0x080c, 5, 0x80, 0x3e, 0x4e, 0x8c, 0x04));
    CHECK(HOLDS(0x6a69, 0x1286, 5, 0xc6, 0x86, 0xc9, 0xfe, 'T') &&
          HOLDS(0x6a69, 0x12a3, 5, 0xc6, 0x86, 0xc9, 0xfe, 'M') &&
          HOLDS(0x6a69, 0x1297, 3, 0xbf, 0x09, 0x31) &&
          pascal_is(ds(0x3109), R->commands.aim_prompt, false));
    CHECK(HOLDS(0x6a69, 0x1f5f, 5, 0x80, 0x3e, 0xab, 0xdb, 0x00) &&
          HOLDS(0x6a69, 0x1f69, 5, 0x80, 0x3e, 0xf9, 0xc0, 0x00));
    /* 1882's cursors (DS:83ee, 83ae) are Champions' (42b4, 42f4): the
     * port's one arrow serves both. */
    static const uint8_t arrow[4] = {0xff, 0x3f, 0xff, 0x1f};
    CHECK(memcmp(ds(0x83ae), arrow, 4) == 0);
    puts("death knights: the mouse checked");
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
        if (exe_size > 0) test_combat();
        if (exe_size > 0) test_fight();
        if (exe_size > 0) test_treasure();
        if (exe_size > 0) test_commands();
        if (exe_size > 0) test_levels();
        if (exe_size > 0) test_items();
        if (exe_size > 0) test_shops();
        if (exe_size > 0) test_casting();
        if (exe_size > 0) test_spells();
        if (exe_size > 0) test_effects();
        if (exe_size > 0) test_band_c();
        if (exe_size > 0) test_creation();
        if (exe_size > 0) test_mouse();
    }
    puts("death knights: ok");
    return 0;
}
