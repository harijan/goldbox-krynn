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
    CHECK(strcmp(R->effects.display_unported, "dkk 4381:15aa") == 0 &&
          cok_rules_champions.effects.display_unported == NULL);
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
    CHECK(found == 20);
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

static bool cites(const char *cited, unsigned long routine)
{
    char text[32];
    snprintf(text, sizeof text, "dkk %04lx:%04lx", routine >> 16, routine & 0xffff);
    return cited != NULL && strcmp(cited, text) == 0;
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
          cites(C->shop_unported, calls[1]) && cites(C->temple_unported, calls[2]) &&
          calls[3] == 0x3a371318);
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
    CHECK(cites(C->player_unported, 0x3e0508af) && cites(C->computer_unported, 0x3f630000) &&
          cites(C->aging_unported, 0x3e050264));
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
        view |= cites(T->vault_view_unported, calls[i]);
        list |= cites(T->vault_items_unported, calls[i]);
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
        if (exe_size > 0) test_treasure();
    }
    puts("death knights: ok");
    return 0;
}
