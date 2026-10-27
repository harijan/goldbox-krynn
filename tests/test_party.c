#define _POSIX_C_SOURCE 200809L /* mkdtemp */

#include "party.h"
#include "test_temp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* A character who can act, with hit points of max. */
static cok_character *make(const char *name, uint8_t hp, uint8_t max, int ac)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    c->record[0x197] = hp;
    c->record[0x62] = max;
    c->record[0x18d] = (uint8_t)(60 - ac);
    c->record[0x189] = 1;
    return c;
}

/* Colour of the top-left pixel of cell x, y. */
static unsigned cell(const cok_picture *p, int x, int y)
{
    return p->pixels[(size_t)y * 8 * p->units * 4 + (size_t)x * 4] >> 4;
}

/* The colour a cell's set pixels have, or 0 if none are set. */
static unsigned ink(const cok_picture *p, int x, int y)
{
    for (int row = 0; row < 8; ++row)
        for (int b = 0; b < 4; ++b) {
            uint8_t pair = p->pixels[((size_t)y * 8 + (size_t)row) * p->units * 4 + (size_t)x * 4 + (size_t)b];
            if (pair >> 4) return pair >> 4;
            if (pair & 15) return pair & 15;
        }
    return 0;
}

static void test_names(void)
{
    char out[9];
    cok_party_file_name("SIR STRONGSWORD", out);
    CHECK(strcmp(out, "SIRSTRON") == 0);
    cok_party_file_name("rolandofdust", out);
    CHECK(strcmp(out, "ROLANDOF") == 0);
    cok_party_file_name("'C.I*N,D?E/R\\:;|", out);
    CHECK(strcmp(out, "'CINDER") == 0);
    cok_party_file_name("", out);
    CHECK(strcmp(out, "") == 0);
    /* 169c:050b does not test again the position a deletion fills. */
    cok_party_file_name("A  B", out);
    CHECK(strcmp(out, "A B") == 0);
    cok_party_file_name("A...B", out);
    CHECK(strcmp(out, "A.B") == 0);
    cok_party_roster_name("A...B", out);
    CHECK(strcmp(out, "AB") == 0);
    cok_party_roster_name("a.b c", out);
    CHECK(strcmp(out, "ABC") == 0);
}

static void test_members(void)
{
    cok_party party = {0};
    CHECK(cok_party_special(&party, NULL, 0x48) == NULL);
    cok_character *a = make("A", 5, 5, 5), *b = make("B", 5, 5, 5), *c = make("C", 5, 5, 5);
    CHECK(cok_party_add(&party, &cok_rules_champions, a) && cok_party_add(&party,
                                                                          &cok_rules_champions, b) && cok_party_add(&party, &cok_rules_champions, c));
    CHECK(a->record[0x137] == 0 && b->record[0x137] == 1 && c->record[0x137] == 2);
    CHECK(cok_party_index(&party, b->record) == 1 && cok_party_index(&party, NULL) == 3);
    /* Up and down wrap; other keys pick the first. */
    CHECK(cok_party_special(&party, a->record, 0x48) == c->record);
    CHECK(cok_party_special(&party, c->record, 0x48) == b->record);
    CHECK(cok_party_special(&party, c->record, 0x50) == a->record);
    CHECK(cok_party_special(&party, a->record, 0x50) == b->record);
    CHECK(cok_party_special(&party, c->record, 0x4b) == a->record);
    /* Up with none selected walks to the last record (546c:3334). */
    CHECK(cok_party_special(&party, NULL, 0x48) == c->record);
    /* A removed member's icon slot is the next one given. */
    cok_party_remove(&party, 1);
    CHECK(party.count == 2 && cok_party_record(&party, 1) == c->record);
    cok_character *d = make("D", 5, 5, 5);
    CHECK(cok_party_add(&party, &cok_rules_champions, d) && d->record[0x137] == 1);
    for (int i = 0; i < 5; ++i) CHECK(cok_party_add(&party, &cok_rules_champions, make("E", 1, 1,
                                                                                       1)));
    /* With slots 0-7 taken the next is 8; the list holds 72 records, the
     * party and the monsters after it. */
    cok_character *ninth = make("N", 1, 1, 1);
    CHECK(cok_party_add(&party, &cok_rules_champions, ninth) && ninth->record[0x137] == 8);
    while (party.count < COK_PARTY_RECORDS) CHECK(cok_party_append(&party, make("M", 1, 1, 1)));
    cok_character *extra = make("F", 1, 1, 1);
    CHECK(!cok_party_add(&party, &cok_rules_champions, extra) && !cok_party_append(&party, extra) &&
          party.count == COK_PARTY_RECORDS);
    free(extra);
    cok_party_free(&party);
    CHECK(party.count == 0);
}

static void test_damage(void)
{
    cok_character *c = make("A", 10, 10, 5);
    uint8_t *r = c->record;
    cok_character_damage(r, &cok_rules_champions, 4);
    CHECK(r[0x197] == 6 && r[0x188] == 0 && r[0x189] == 1);
    cok_character_damage(r, &cok_rules_champions, 6);
    CHECK(r[0x197] == 0 && r[0x188] == 4 && r[0x189] == 0);
    r[0x197] = 10;
    r[0x188] = 0;
    r[0x189] = 1;
    cok_character_damage(r, &cok_rules_champions, 19);
    CHECK(r[0x197] == 0 && r[0x188] == 5 && r[0x189] == 0);
    r[0x197] = 10;
    r[0x188] = 0;
    cok_character_damage(r, &cok_rules_champions, 20);
    CHECK(r[0x188] == 6);
    /* Status 1 dies at its last hit point. */
    r[0x197] = 3;
    r[0x188] = 1;
    cok_character_damage(r, &cok_rules_champions, 3);
    CHECK(r[0x188] == 6 && r[0x197] == 0);
    free(c);
}

static void test_draw(void)
{
    static const uint8_t glyph[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t glyphs[64 * 8];
    for (size_t i = 0; i < 64; ++i) memcpy(glyphs + i * 8, glyph, 8);
    memset(glyphs + 32 * 8, 0, 8); /* space */
    cok_font font = {0};
    CHECK(cok_font_load(&font, glyphs, sizeof glyphs) == COK_PICTURE_OK);
    cok_picture screen = {0};
    CHECK(cok_picture_create(&screen, 40, 200, 1, 0) == COK_PICTURE_OK);
    cok_party party = {0};
    cok_character *a = make("AB", 7, 9, 5), *b = make("C", 120, 120, -3);
    cok_character *c = make("D", 10, 10, 10);
    c->record[0x189] = 0;
    b->record[0x18a] = 1;
    CHECK(cok_party_add(&party, &cok_rules_champions, a) && cok_party_add(&party,
                                                                          &cok_rules_champions, b) && cok_party_add(&party, &cok_rules_champions, c));
    cok_picture_fill(&screen, 0, 7 * 8, 40, 8, 9); /* a stale row */
    cok_party_draw(&screen, &font, &party, &cok_rules_champions, a->record, 0x11, false);
    CHECK(ink(&screen, 0x11, 2) == 15 && ink(&screen, 0x21, 2) == 15);
    /* Selected white; others light cyan, or light red when they cannot act. */
    CHECK(ink(&screen, 0x11, 4) == 15 && ink(&screen, 0x11, 5) == 0x0b &&
          ink(&screen, 0x11, 6) == 0x0c);
    /* AC 5 ends at column 34, -3 is "-3" there; HP end at column 38,
     * yellow when hurt. */
    CHECK(ink(&screen, 0x22, 4) == 0x0a && ink(&screen, 0x21, 4) == 0);
    CHECK(ink(&screen, 0x21, 5) == 0x0a && ink(&screen, 0x22, 5) == 0x0a);
    CHECK(ink(&screen, 0x26, 4) == 0x0e && ink(&screen, 0x25, 4) == 0);
    CHECK(ink(&screen, 0x24, 5) == 0x0a && ink(&screen, 0x26, 5) == 0x0a);
    CHECK(ink(&screen, 0x24, 6) == 0 && ink(&screen, 0x25, 6) == 0x0a);
    /* The row after the list is cleared from x. */
    CHECK(cell(&screen, 0x11, 7) == 0 && cell(&screen, 0x10, 7) == 9);
    /* In combat, a member fighting the party is yellow. */
    cok_party_draw(&screen, &font, &party, &cok_rules_champions, NULL, 1, true);
    CHECK(ink(&screen, 1, 4) == 0x0b && ink(&screen, 1, 5) == 0x0e);
    cok_party_free(&party);
    cok_picture_free(&screen);
    cok_font_free(&font);
}

static void write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(data, 1, size, f) == size);
    fclose(f);
}

static void test_files(void)
{
    static cok_item_types types;
    char error[256];
    CHECK(cok_item_types_read("COKAssets/ITEMS", &types, error, sizeof error));
    char dir[512];
    CHECK(test_temp_dir(dir, sizeof dir, "cok_party") != NULL);
    char path[600];

    /* A saved game: the layout of 4b6d:1b34. */
    static uint8_t save[COK_SAVED_GAME_SIZE];
    save[0] = 2;
    save[1 + 2 * 0xe6] = 1;              /* 0x4be6 */
    save[0x801 + 2 * 0x312] = 3;         /* 0x7f12 */
    save[0x1001 + 2 * 0x1ff + 1] = 0x12; /* 0x7bff */
    memcpy(save + 0x1401, (uint8_t[]){0xff, 13, 2, 4, 5, 3, 4}, 7);
    memcpy(save + 0x1408, (uint8_t[]){7, 0, 1, 0, 0xff, 0xff, 0xff, 0xff, 9, 0, 3, 0}, 12);
    save[0x1414] = 2;
    memcpy(save + 0x1415, "\x08" "CHRDATB1", 9);
    memcpy(save + 0x1415 + 41, "\x05" "OTHER", 6);
    snprintf(path, sizeof path, "%s/SAVGAMB.DAT", dir);
    write_file(path, save, sizeof save);
    static cok_saved_game game;
    CHECK(cok_saved_game_read(path, &cok_rules_champions, &game, error, sizeof error));
    CHECK(game.file == 2 && game.mem4b00[0xe6] == 1 && game.mem7c00[0x312] == 3);
    CHECK(game.mem7a00[0x1ff] == 0x1200);
    CHECK(game.map_x == -1 && game.map_y == 13 && game.direction == 2 && game.ahead == 4 &&
          game.square == 5 && game.last_mode == 3 && game.mode == 4);
    CHECK(game.wall_ids[0] == 7 && game.wall_slots[0] == 1 && game.wall_ids[1] == -1 &&
          game.wall_ids[2] == 9 && game.wall_slots[2] == 3);
    CHECK(game.count == 2 && strcmp(game.names[0], "CHRDATB1") == 0 &&
          strcmp(game.names[1], "OTHER") == 0);
    write_file(path, save, sizeof save - 1);
    CHECK(!cok_saved_game_read(path, &cok_rules_champions, &game, error, sizeof error));

    /* A character with two items, part of a third, and an effect. */
    uint8_t record[COK_CHARACTER_SIZE] = {3, 'K', 'A', 'L'};
    memset(record + 0x17f, 0xaa, 4);
    memset(record + 0xe3, 0xbb, 4);
    memset(record + 0x147, 0xcc, 4 * COK_ITEM_SLOTS);
    record[0x197] = 21;
    snprintf(path, sizeof path, "%s/KAL.SAV", dir);
    write_file(path, record, sizeof record);
    uint8_t items[2 * COK_ITEM_SIZE + 10] = {0};
    items[COK_ITEM_SIZE + 0x2e] = 0x1e;
    snprintf(path, sizeof path, "%s/KAL.STF", dir);
    write_file(path, items, sizeof items);
    /* Two effects and part of a third; the stale next pointers are not kept. */
    uint8_t effects[2 * COK_EFFECT_SIZE + 4] = {0x2f, 10, 1, 0xff, 0, 0xaa, 0xbb, 0xcc, 0xdd,
                                                0x07, 0, 0, 0x12, 1};
    snprintf(path, sizeof path, "%s/KAL.SFX", dir);
    write_file(path, effects, sizeof effects);
    cok_character c;
    CHECK(cok_character_read(&c, &cok_rules_champions, dir, "KAL", &types, error, sizeof error));
    CHECK(c.record[0] == 3 && c.record[0x197] == 21 && c.record[0x17f] == 0 && c.record[0xe3] == 0);
    /* 6346:0d20 clears the slots' far pointers. */
    for (size_t k = 0; k < 4 * COK_ITEM_SLOTS; ++k) CHECK(c.record[0x147 + k] == 0);
    CHECK(c.item_count == 2 && c.items[1][0x2e] == 0x1e);
    const cok_effect *e = c.effects;
    CHECK(e != NULL && e->id == 0x2f && e->duration == 0x10a && e->value == 0xff && !e->on_remove);
    e = e->next;
    CHECK(e != NULL && e->id == 0x07 && e->duration == 0 && e->value == 0x12 && e->on_remove);
    CHECK(e->next == NULL && cok_character_find_effect(&c, 0x07) == e);
    CHECK(cok_character_find_effect(&c, 0x12) == NULL);
    /* Loading recomputes the derived fields, here of a record of zeros. */
    CHECK(c.record[0xce] == 1 && c.record[0x18e] == 0xfe && c.record[0x59] == 40);
    cok_character_free(&c);
    /* Type 128 is the zeroed record after ITEMS's 128; a readied item past
     * it stops the load. */
    items[0x2e] = 0x80;
    items[0x34] = 1;
    snprintf(path, sizeof path, "%s/KAL.STF", dir);
    write_file(path, items, sizeof items);
    CHECK(cok_character_read(&c, &cok_rules_champions, dir, "KAL", &types, error, sizeof error));
    CHECK(c.slots[0] == 1 && c.record[0x191] == 0 && c.record[0x193] == 0);
    cok_character_free(&c);
    items[0x2e] = 0x81;
    write_file(path, items, sizeof items);
    CHECK(!cok_character_read(&c, &cok_rules_champions, dir, "KAL", &types, error, sizeof error));
    CHECK(strstr(error, "KAL.SAV") != NULL && strstr(error, "item type 129") != NULL);
    cok_character_free(&c);
    /* Items and effects are optional. */
    snprintf(path, sizeof path, "%s/KAL.STF", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/KAL.SFX", dir);
    remove(path);
    CHECK(cok_character_read(&c, &cok_rules_champions, dir, "KAL", &types, error, sizeof error));
    CHECK(c.item_count == 0 && c.items == NULL && c.effects == NULL);
    cok_character_free(&c);
    CHECK(!cok_character_read(&c, &cok_rules_champions, dir, "NOBODY", &types, error,
                              sizeof error));
    CHECK(strstr(error, "NOBODY.SAV") != NULL);
    cok_character_free(&c);

    snprintf(path, sizeof path, "%s/KAL.SAV", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/SAVGAMB.DAT", dir);
    remove(path);
    CHECK(rmdir(dir) == 0);
}

/* An item of type, readied or not, with bonus and weight. */
static void set_item(uint8_t *item, uint8_t type, bool readied, int bonus, uint16_t weight)
{
    memset(item, 0, COK_ITEM_SIZE);
    item[0x2e] = type;
    item[0x34] = readied;
    item[0x32] = (uint8_t)bonus;
    item[0x37] = (uint8_t)weight;
    item[0x38] = (uint8_t)(weight >> 8);
}

static cok_item_types *load_types(void)
{
    static cok_item_types types;
    char error[256];
    CHECK(cok_item_types_read("COKAssets/ITEMS", &types, error, sizeof error));
    return &types;
}

static void test_items(void)
{
    const cok_item_types *types = load_types();
    /* Type 0x24: armour (slot 2), 0x80 + 57; 0x25 a shield of 0x80 + 1. */
    CHECK(types->type[0x24][0] == 2 && types->type[0x24][6] == 0xb9);
    CHECK(types->type[0x25][0] == 1 && types->type[0x25][6] == 0x81);
    static cok_item_types none;
    char error[256];
    CHECK(!cok_item_types_read("COKAssets/NO-ITEMS", &none, error, sizeof error));
    CHECK(strstr(error, "NO-ITEMS") != NULL);
}

/* A human level 1 fighter with strength 17 and dexterity 16, a sword +1,
 * plate +1 (450), a shield, a ring +2 and a cloak +1 of class 0, and 10
 * coins. */
static cok_character *fighter(void)
{
    cok_character *c = make("F", 10, 10, 10);
    uint8_t *r = c->record;
    r[0x5a] = 6;
    r[0xfb] = 1;
    r[0x11] = 17;
    r[0x114] = 1;
    r[0x17] = 16;
    r[0x59] = 40;
    r[0xd5] = 12;
    r[0x113] = 50;
    r[0x10d] = 1;
    r[0x10f] = 2;
    r[0xed] = 10;
    c->item_count = 5;
    c->items = calloc(5, COK_ITEM_SIZE);
    CHECK(c->items != NULL);
    set_item(c->items[0], 0x12, true, 1, 60);
    set_item(c->items[1], 0x24, true, 1, 450);
    set_item(c->items[2], 0x25, true, 0, 100);
    set_item(c->items[3], 0x3b, true, 2, 1);
    set_item(c->items[4], 0x3a, true, 1, 40);
    c->items[3][0x33] = 1;
    c->items[4][0x33] = 1;
    return c;
}

static void test_stats(void)
{
    const cok_item_types *types = load_types();
    char error[256];
    cok_character *c = fighter();
    uint8_t *r = c->record;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(c->slots[0] == 1 && c->slots[1] == 3 && c->slots[2] == 2 && c->slots[7] == 5 &&
          c->slots[9] == 4 && c->slots[10] == 0 && c->slots[11] == 0);
    CHECK(r[0x142] == 5 && r[0x17b] == 2 && (r[0x17d] | r[0x17e] << 8) == 661);
    /* Every coin but silver weighs, copper (+0xed) to jewelry (+0xf7), each
     * a word; silver (+0xeb) does not (6346:0d20). */
    r[0xeb] = 0x34;
    r[0xec] = 0x12;
    for (unsigned k = 2; k <= 6; ++k) r[0xeb + 2 * k] = (uint8_t)k;
    r[0xf8] = 1; /* jewelry 0x106 */
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK((r[0x17d] | r[0x17e] << 8) == 661 + 2 + 3 + 4 + 5 + 0x106);
    memset(r + 0xeb, 0, 14);
    r[0xed] = 10;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK((r[0x17d] | r[0x17e] << 8) == 661);
    /* To hit: 40, strength +1, sword +1; damage 1d8, strength +1, sword +1. */
    CHECK(r[0x18c] == 42 && r[0x191] == 1 && r[0x193] == 8 && r[0x195] == 2);
    /* Dexterity 2, shield 1, cloak 1, plate 58; magic armour drops the
     * ring's 2. From behind, plate and cloak, less 2. */
    CHECK(r[0x18d] == 62 && r[0x18e] == 57 && r[0x17c] == 2);
    /* Plate over 399 moves 6, magic plate 3 more; within the allowance. */
    CHECK(r[0x198] == 9 && r[0xce] == 1);
    /* Plain plate: the ring counts, and the movement is 6. */
    c->items[1][0x32] = 0;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0x18d] == 63 && r[0x18e] == 58 && r[0x198] == 6);
    /* Strength 3 allows -350: 1,011 over slows to 6 at most, and to 3
     * past 1,024. */
    c->items[1][0x32] = 1;
    r[0x11] = 3;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0x198] == 6);
    r[0xed] = 30;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0x198] == 3);
    /* 32,768 or more over the allowance counts as none. */
    r[0x11] = 17;
    r[0xed] = (uint8_t)39339;
    r[0xee] = 39339 >> 8;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0x198] == 9);
    /* Without a weapon, strength adds to the base attack. */
    c->items[0][0x34] = 0;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(c->slots[0] == 0 && r[0x18c] == 41 && r[0x191] == 1 && r[0x193] == 2 &&
          r[0x195] == 1 && r[0x17b] == 1);
    /* Strength past 25 has no row in the original's tables. */
    r[0x11] = 26;
    CHECK(!cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(strstr(error, "strength 26") != NULL);
    cok_character_free(c);
    free(c);
}

static void test_levels(void)
{
    const cok_item_types *types = load_types();
    char error[256];
    /* A level 1 knight saves as a level 12 thief: 66c2:08a6 checks the
     * former knight level after its loop, reading the entry before the
     * knight's level 1. */
    cok_character *c = make("K", 10, 10, 10);
    uint8_t *r = c->record;
    r[0x5a] = 6;
    r[0x100] = 1;
    c->item_count = 3;
    c->items = calloc(3, COK_ITEM_SIZE);
    CHECK(c->items != NULL);
    set_item(c->items[0], 0x44, true, 0, 10); /* classes 0x10: a knight's */
    set_item(c->items[1], 0x41, true, 0, 1);  /* classes 0x01 */
    set_item(c->items[2], 0x63, true, 0, 1);  /* classes 0x01, but stuck */
    c->items[2][0x36] = 1;
    CHECK(cok_character_stats(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0xd0] == 11 && r[0xd1] == 10 && r[0xd2] == 10 && r[0xd3] == 14 && r[0xd4] == 11);
    CHECK(r[0x59] == 40 && r[0x11a] == 0x10 && r[0xd6] == 1);
    CHECK(c->items[0][0x34] == 1 && c->items[1][0x34] == 0 && c->items[2][0x34] == 1);
    cok_character_free(c);
    free(c);
    /* A level 1 thief's skills include the 7 that 66c2:08a6 leaves where
     * 66c2:0b9f reads its uninitialized local; with a negative race
     * adjustment (race 5, read languages -10) the skill is 0. */
    c = make("T", 10, 10, 10);
    r = c->record;
    r[0x5a] = 6;
    r[0xff] = 1;
    r[0x17] = 13;
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    static const uint8_t human[8] = {37, 32, 27, 22, 17, 17, 92, 7};
    CHECK(memcmp(r + 0xdb, human, 8) == 0);
    r[0x5a] = 5;
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    static const uint8_t race5[8] = {32, 37, 32, 22, 17, 22, 97, 0};
    CHECK(memcmp(r + 0xdb, race5, 8) == 0);
    /* Indexes past a table read the data next to it, as the original's do:
     * dexterity 20 reads DS:39e0 (3, 3, 18, 16 and 75); a level of -1 reads
     * the byte before the THAC0 table, 1, and a former fighter level of -1
     * the THAC0 of class 1 at level 12, 46. */
    r[0x5a] = 6;
    r[0x17] = 20;
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    static const uint8_t dex20[8] = {40, 35, 45, 38, 92, 17, 92, 7};
    CHECK(memcmp(r + 0xdb, dex20, 8) == 0);
    r[0xff] = 0;
    r[0xf9] = 0xff;
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0x59] == 40);
    r[0xf9] = 0;
    r[0x103] = 0xff;
    r[0xd7] = 0x80; /* so that it may use its former classes */
    r[0xfa] = 1;
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0x59] == 46);
    /* A saving throw for level 100 reads past the initialized data. */
    r[0x100] = 100;
    CHECK(!cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(strstr(error, "saving throw reads DS:43ee") != NULL);
    memset(r + 0xf9, 0, 16);
    memset(r + 0x63, 0, 100); /* the spells it learned on the way */
    r[0xd7] = 0;
    /* A cleric of level 2 with wisdom 18: one spell for level 2, two for
     * wisdom 13 and 14; those of wisdom 15-18 need spells of levels 2-4 to
     * add to. It knows the first level spells of DS:423b. */
    memset(r + 0xd0, 0, 0x30);
    r[0x5a] = 6;
    r[0xff] = 0;
    r[0xf9] = 2;
    r[0x15] = 18;
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    CHECK(r[0x11c] == 4 && r[0x11d] == 0);
    CHECK(r[0x62 + 1] == 1 && r[0x62 + 8] == 1 && r[0x62 + 2] == 0 && r[0x62 + 22] == 0);
    CHECK(r[0xd0] == 10 && r[0xd4] == 15);
    /* A ranger of level 8 knows every druid spell: those whose class (byte
     * 0 of the spell table) is 1, ids 77-80; none of another class. */
    memset(r + 0xf9, 0, 8);
    memset(r + 0x63, 0, 0x6b);
    r[0xfd] = 8;
    CHECK(cok_character_levels(c, &cok_rules_champions, types, error, sizeof error));
    for (unsigned id = 1; id <= 0x6b; ++id) CHECK(r[0x62 + id] == (id >= 77 && id <= 80));
    cok_character_free(c);
    free(c);
}

/* Characters the original saved: the recomputed fields match the saved
 * ones, but for spell 8, which this executable's cleric table (DS:423b)
 * lists and the two clerics of SAVGAMA.DAT lack. */
static void test_saved(void)
{
    FILE *f = fopen("SAVE/CHRDATA1.SAV", "rb");
    if (f == NULL) {
        puts("party: no saved games in SAVE; skipped");
        return;
    }
    fclose(f);
    const cok_item_types *types = load_types();
    static const char *const names[] = {"CHRDATA1", "CHRDATA2", "CHRDATA3", "CHRDATA4",
                                        "CHRDATA5", "CHRDATA6", "CHRDATB1", "CHRDATB2",
                                        "CHRDATB3", "CHRDATB4", "CHRDATB5", "CHRDATB6"};
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i) {
        char path[64], error[256];
        snprintf(path, sizeof path, "SAVE/%s.SAV", names[i]);
        uint8_t saved[COK_CHARACTER_SIZE] = {0};
        f = fopen(path, "rb");
        CHECK(f != NULL && fread(saved, 1, sizeof saved, f) == sizeof saved);
        fclose(f);
        cok_character c;
        CHECK(cok_character_read(&c, &cok_rules_champions, "SAVE", names[i], types, error,
                                 sizeof error));
        for (size_t k = 0; k < COK_CHARACTER_SIZE; ++k) {
            /* Pointers: effects, items, slots, next, combat record. */
            if ((k >= 0xe3 && k < 0xe7) || (k >= 0x143 && k < 0x17b) || (k >= 0x17f && k < 0x187))
                continue;
            if (k == 0x62 + 8 && (i == 4 || i == 5)) {
                CHECK(saved[k] == 0 && c.record[k] == 1);
                continue;
            }
            if (saved[k] != c.record[k])
                fprintf(stderr, "%s +0x%03zx: saved %u, recomputed %u\n", names[i], k, saved[k],
                        c.record[k]);
            CHECK(saved[k] == c.record[k]);
        }
        cok_character_free(&c);
    }
}

/* A file's bytes, as many as fit; *size its length, 0 if missing. */
static size_t read_whole(const char *path, uint8_t *out, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    size_t n = fread(out, 1, size, f);
    fclose(f);
    return n;
}

/* Whether file a and b hold the same bytes but the far pointers the port
 * writes 0: those of the record (layout L) for a record, the next
 * pointers of items (+0x2a) and effects (+5). */
static bool same_file(const char *a, const char *b, const char *extension,
                      const cok_character_layout *L)
{
    static uint8_t x[4096], y[4096];
    size_t nx = read_whole(a, x, sizeof x), ny = read_whole(b, y, sizeof y);
    if (nx != ny) {
        fprintf(stderr, "%s: %zu bytes, %s %zu\n", a, nx, b, ny);
        return false;
    }
    bool same = true;
    for (size_t k = 0; k < nx; ++k) {
        bool pointer = strcmp(extension, "GAM") == 0   ? cok_field_pointer_at(L, (long)k)
                       : strcmp(extension, "JNK") == 0 ? k % COK_ITEM_SIZE >= 0x2a &&
                                                             k % COK_ITEM_SIZE < 0x2e
                       : strcmp(extension, "EFF") == 0 ? k % COK_EFFECT_SIZE >= 5
                                                       : false;
        if (pointer || x[k] == y[k]) continue;
        fprintf(stderr, "%s +0x%03zx: %02x, %s %02x\n", a, k, x[k], b, y[k]);
        same = false;
    }
    return same;
}

/* Death Knights' character files: the record (.GAM, 216 bytes) with its
 * spells' block (.WIZ, 220) after it in the port's record, its mark set
 * while held; the pointers cleared; a short block read as far as it
 * goes; written back with the block's file only for a record that holds
 * one, an old one erased. */
static void test_death_knights_files(void)
{
    const cok_rules *R = &cok_rules_death_knights;
    const cok_character_layout *L = R->character;
    const cok_item_types *types = load_types();
    char dir[512], path[600], error[256];
    CHECK(test_temp_dir(dir, sizeof dir, "cok_party_dkk") != NULL);
    uint8_t record[0xd8] = {3, 'K', 'A', 'L'}, block[0xdc] = {0};
    record[0x20] = 6;                     /* human */
    record[0x47] = 3;                     /* fighter level 3 */
    record[0x2f] = 12;                    /* base movement */
    memset(record + 0xc2, 0xbb, 4);       /* a stale pointer to the block */
    memset(record + 0x3a, 0xcc, 4);       /* to the effects */
    record[0xd6] = 20;
    block[0] = 0x11;                      /* a memorized spell */
    block[0x49 + 5] = 1;                  /* spell 5 known */
    snprintf(path, sizeof path, "%s/KAL.GAM", dir);
    write_file(path, record, sizeof record);
    cok_character c;
    /* No .WIZ: no block, the stale pointer cleared. */
    CHECK(cok_character_read(&c, R, dir, "KAL", types, error, sizeof error));
    CHECK(!cok_field_has_spells(L, c.record) && c.record[0xc2] == 0 && c.record[0xc5] == 0);
    CHECK(c.record[0x3a] == 0 && c.record[0xd6] == 20);
    CHECK(cok_field_u8_at(L, c.record, COK_FIELD_MEMORIZED, 0) == 0);
    /* The stats' movement and the levels' (dkk 65cd:0e46, 69a8:050b). */
    CHECK(cok_field_u8(L, c.record, COK_FIELD_MOVEMENT) == 12);
    CHECK(cok_field_u8(L, c.record, COK_FIELD_HIGHEST_LEVEL) == 3);
    CHECK(cok_field_u8(L, c.record, COK_FIELD_BASE_THAC0) == cok_data_u8_at2(R->data,
                                                                         COK_DATA_THAC0, 2, 3));
    cok_character_free(&c);
    snprintf(path, sizeof path, "%s/KAL.WIZ", dir);
    write_file(path, block, sizeof block);
    CHECK(cok_character_read(&c, R, dir, "KAL", types, error, sizeof error));
    CHECK(cok_field_has_spells(L, c.record) && c.record[0xc2] == 1);
    CHECK(cok_field_u8_at(L, c.record, COK_FIELD_MEMORIZED, 0) == 0x11);
    CHECK(cok_field_u8_at(L, c.record, COK_FIELD_KNOWN_SPELL, 5) == 1);
    /* Written back: the record with its pointers 0, the block. */
    char out[512];
    CHECK(test_temp_dir(out, sizeof out, "cok_party_dkk_out") != NULL);
    CHECK(cok_character_write(&c, R, out, "KAL", error, sizeof error));
    uint8_t read_back[0x200];
    snprintf(path, sizeof path, "%s/KAL.GAM", out);
    CHECK(read_whole(path, read_back, sizeof read_back) == 0xd8);
    CHECK(read_back[0xc2] == 0 && read_back[0xd6] == 20 && read_back[1] == 'K');
    snprintf(path, sizeof path, "%s/KAL.WIZ", out);
    CHECK(read_whole(path, read_back, sizeof read_back) == 0xdc);
    CHECK(read_back[0] == 0x11 && read_back[0x49 + 5] == 1);
    /* Without the block, its file is erased and not written again. */
    cok_field_set_spells(L, c.record, false);
    CHECK(cok_character_write(&c, R, out, "KAL", error, sizeof error));
    CHECK(read_whole(path, read_back, sizeof read_back) == 0);
    cok_character_free(&c);
    /* A short block: what there is, the rest 0. */
    snprintf(path, sizeof path, "%s/KAL.WIZ", dir);
    write_file(path, block, 3);
    CHECK(cok_character_read(&c, R, dir, "KAL", types, error, sizeof error));
    CHECK(cok_field_has_spells(L, c.record) &&
          cok_field_u8_at(L, c.record, COK_FIELD_MEMORIZED, 0) == 0x11 &&
          cok_field_u8_at(L, c.record, COK_FIELD_KNOWN_SPELL, 5) == 0);
    cok_character_free(&c);
    static const char *const files[] = {"KAL.GAM", "KAL.WIZ"};
    for (size_t i = 0; i < 2; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        remove(path);
        snprintf(path, sizeof path, "%s/%s", out, files[i]);
        remove(path);
    }
    CHECK(rmdir(dir) == 0 && rmdir(out) == 0);
}

/* Death Knights' recompute by hand: the movement with effects 0x27 and
 * 0x09 doubling it and 0x2a halving it (65cd:1241); a fighter's base
 * attacks, 3 from level 7 and 4 from 13 (69a8:050b); a ranger's spells a
 * day from level 8, table 6's druid and mage columns (69a8:0000). */
static void test_death_knights_recompute(void)
{
    const cok_rules *R = &cok_rules_death_knights;
    const cok_character_layout *L = R->character;
    const cok_data_segment *D = R->data;
    const cok_item_types *types = load_types();
    char error[256];
    cok_character c;
    memset(&c, 0, sizeof c);
    uint8_t *r = c.record;
    cok_field_set8(L, r, COK_FIELD_RACE, 6);
    cok_field_set8(L, r, COK_FIELD_BASE_MOVEMENT, 12);
    CHECK(cok_character_stats(&c, R, types, error, sizeof error));
    CHECK(cok_field_u8(L, r, COK_FIELD_MOVEMENT) == 12);
    CHECK(cok_character_add_effect(&c, 0x2a, 0, 0, false) != NULL);
    CHECK(cok_character_stats(&c, R, types, error, sizeof error));
    CHECK(cok_field_u8(L, r, COK_FIELD_MOVEMENT) == 6);
    CHECK(cok_character_add_effect(&c, 0x27, 0, 0, false) != NULL);
    CHECK(cok_character_stats(&c, R, types, error, sizeof error));
    CHECK(cok_field_u8(L, r, COK_FIELD_MOVEMENT) == 12);
    CHECK(cok_character_add_effect(&c, 0x09, 0, 0, false) != NULL);
    CHECK(cok_character_stats(&c, R, types, error, sizeof error));
    CHECK(cok_field_u8(L, r, COK_FIELD_MOVEMENT) == 24);
    cok_character_free(&c);
    /* The fighter's attacks. */
    static const struct { int8_t level; uint8_t attacks; } fighter[] = {
        {6, 0}, {7, 3}, {12, 3}, {13, 4}, {18, 4}};
    for (size_t i = 0; i < sizeof fighter / sizeof *fighter; ++i) {
        memset(&c, 0, sizeof c);
        cok_field_set8(L, r, COK_FIELD_RACE, 6);
        cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 2, (uint8_t)fighter[i].level);
        CHECK(cok_character_levels(&c, R, types, error, sizeof error));
        CHECK(cok_field_u8_at(L, r, COK_FIELD_BASE_ATTACKS, 1) == fighter[i].attacks);
        CHECK(cok_field_u8(L, r, COK_FIELD_HIGHEST_LEVEL) == (uint8_t)fighter[i].level);
    }
    /* The ranger of level 12, with a spells' block: druid spells 1-4
     * (row 1) and mage spells 1-4 (row 3) of DS:7673 + 6 * 0xcc + level
     * * 8 + k, levels 8-12. */
    memset(&c, 0, sizeof c);
    cok_field_set8(L, r, COK_FIELD_RACE, 6);
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 4, 12);
    cok_field_set_spells(L, r, true);
    CHECK(cok_character_levels(&c, R, types, error, sizeof error));
    long base = cok_data_offset3(D, COK_DATA_SPELLS_A_DAY, 6, 0, 0);
    for (int k = 1; k <= 8; ++k) {
        unsigned sum = 0;
        for (int level = 8; level <= 12; ++level) sum += cok_data_held(D, base + level * 8 + k);
        CHECK(cok_field_u8_at2(L, r, COK_FIELD_SPELLS_A_DAY, k <= 4 ? 1 : 3, k <= 4 ? k : k - 4) ==
              (uint8_t)sum);
    }
    /* Without a block, none. */
    memset(&c, 0, sizeof c);
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 4, 12);
    CHECK(cok_character_levels(&c, R, types, error, sizeof error));
    CHECK(cok_field_u8_at2(L, r, COK_FIELD_SPELLS_A_DAY, 1, 1) == 0);
}

/* Former classes: Champions' human whose first class's level passes the
 * level it changed class at may use its former levels (66c2:0efb); Death
 * Knights has none, its records neither field: the former levels and the
 * level read as 0 (cok_character_former_level, _change_level). */
static void test_former_classes(void)
{
    const cok_rules *C = &cok_rules_champions, *R = &cok_rules_death_knights;
    uint8_t c[COK_CHARACTER_MAX] = {0};
    const cok_character_layout *L = C->character;
    cok_field_set8(L, c, COK_FIELD_RACE, 6);
    cok_field_set8_at(L, c, COK_FIELD_CLASS_LEVEL, 2, 9);
    cok_field_set8_at(L, c, COK_FIELD_FORMER_LEVEL, 5, 7);
    cok_field_set8(L, c, COK_FIELD_CHANGE_LEVEL, 7);
    CHECK(cok_character_former_class(c, C) && cok_character_former_level(C, c, 5) == 7 &&
          cok_character_change_level(C, c) == 7 && cok_character_former_level(C, c, 2) == 0);
    cok_field_set8_at(L, c, COK_FIELD_CLASS_LEVEL, 2, 7);
    CHECK(!cok_character_former_class(c, C));
    memset(c, 0, sizeof c);
    L = R->character;
    cok_field_set8(L, c, COK_FIELD_RACE, 6);
    cok_field_set8_at(L, c, COK_FIELD_CLASS_LEVEL, 2, 9);
    CHECK(!cok_levels_former_classes(R) && cok_levels_former_classes(C));
    CHECK(!cok_character_former_class(c, R) && cok_character_change_level(R, c) == 0);
    for (size_t k = 0; k < 8; ++k) CHECK(cok_character_former_level(R, c, k) == 0);
}

/* Champions' character imported into Death Knights' (dkk 46b2:10d7): the
 * record's runs moved, the block made for a class but fighter, thief and
 * fighter/thief with the memorized and known spells, the age capped by
 * race, the items of the dropped types left out, the effects kept. */
static void test_import(void)
{
    const cok_rules *R = &cok_rules_death_knights;
    const cok_character_layout *L = R->character, *C = cok_rules_champions.character;
    const cok_item_types *types = load_types();
    char dir[512], path[600], error[256];
    CHECK(test_temp_dir(dir, sizeof dir, "cok_party_import") != NULL);
    uint8_t record[COK_CHARACTER_SIZE] = {4, 'C', 'E', 'R', 'A'};
    cok_field_set8(C, record, COK_FIELD_RACE, 2);       /* elf */
    cok_field_set8(C, record, COK_FIELD_CLASS, 5);      /* mage */
    cok_field_set16(C, record, COK_FIELD_AGE, 250);     /* past an elf's 200 */
    cok_field_set8_at(C, record, COK_FIELD_CLASS_LEVEL, 5, 4);
    cok_field_set8_at(C, record, COK_FIELD_FORMER_LEVEL, 2, 3);
    cok_field_set16_at(C, record, COK_FIELD_COINS, 0, 77); /* silver: dropped */
    cok_field_set16_at(C, record, COK_FIELD_COINS, 5, 9);  /* gems */
    cok_field_set32(C, record, COK_FIELD_EXPERIENCE, 12345);
    cok_field_set8(C, record, COK_FIELD_NPC, 0x81);     /* dropped */
    cok_field_set8(C, record, COK_FIELD_HIT_POINTS, 11);
    cok_field_set8(C, record, COK_FIELD_MAX_HIT_POINTS, 13);
    cok_field_set8_at(C, record, COK_FIELD_MEMORIZED, 0, 0x0f);
    cok_field_set8_at(C, record, COK_FIELD_KNOWN_SPELL, 0x0f, 1);
    cok_field_set8_at(C, record, COK_FIELD_KNOWN_SPELL, 107, 1);
    memset(record + cok_field_offset(C, COK_FIELD_ITEMS), 0xaa, 4);
    snprintf(path, sizeof path, "%s/CERA.WHO", dir);
    write_file(path, record, sizeof record);
    uint8_t items[3 * COK_ITEM_SIZE] = {0};
    items[0x2e] = 0x10;
    items[COK_ITEM_SIZE + 0x2e] = 42; /* dropped */
    items[2 * COK_ITEM_SIZE + 0x2e] = 0x11;
    snprintf(path, sizeof path, "%s/CERA.STF", dir);
    write_file(path, items, sizeof items);
    uint8_t effects[COK_EFFECT_SIZE] = {0x2f, 10, 0, 3, 1};
    snprintf(path, sizeof path, "%s/CERA.SFX", dir);
    write_file(path, effects, sizeof effects);
    cok_character c;
    CHECK(cok_character_import(&c, R, dir, "CERA", "WHO", types, error, sizeof error));
    const uint8_t *r = c.record;
    CHECK(r[0] == 4 && memcmp(r + 1, "CERA", 4) == 0);
    CHECK(cok_field_u8(L, r, COK_FIELD_RACE) == 2 && cok_field_u8(L, r, COK_FIELD_CLASS) == 5);
    CHECK(cok_field_u16(L, r, COK_FIELD_AGE) == 200);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 5) == 4);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_UNDRAINED_LEVEL, 2) == 3);
    CHECK(cok_field_u16_at(L, r, COK_FIELD_COINS, 5) == 9);
    CHECK(cok_field_u32(L, r, COK_FIELD_EXPERIENCE) == 12345);
    CHECK(cok_field_u8(L, r, COK_FIELD_NPC) == 0);
    CHECK(cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == 11);
    CHECK(cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS) == 13);
    CHECK(cok_field_has_spells(L, r));
    CHECK(cok_field_u8_at(L, r, COK_FIELD_MEMORIZED, 0) == 0x0f);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_KNOWN_SPELL, 0x0f) == 1);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_KNOWN_SPELL, 107) == 1);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_KNOWN_SPELL, 108) == 0);
    /* A mage of level 4's spells a day, by Death Knights' table 8. */
    CHECK(cok_field_u8_at2(L, r, COK_FIELD_SPELLS_A_DAY, 3, 1) != 0);
    CHECK(c.item_count == 2 && c.items[0][0x2e] == 0x10 && c.items[1][0x2e] == 0x11);
    CHECK(c.effects != NULL && c.effects->id == 0x2f && c.effects->duration == 10 &&
          c.effects->value == 3 && c.effects->on_remove && c.effects->next == NULL);
    cok_character_free(&c);
    /* A fighter gets no block; a dwarf's age is capped at 300. */
    cok_field_set8(C, record, COK_FIELD_CLASS, 2);
    cok_field_set8(C, record, COK_FIELD_RACE, 3);
    cok_field_set16(C, record, COK_FIELD_AGE, 301);
    snprintf(path, sizeof path, "%s/CERA.WHO", dir);
    write_file(path, record, sizeof record);
    CHECK(cok_character_import(&c, R, dir, "CERA", "WHO", types, error, sizeof error));
    CHECK(!cok_field_has_spells(L, c.record));
    CHECK(cok_field_u16(L, c.record, COK_FIELD_AGE) == 300);
    CHECK(cok_field_u8_at(L, c.record, COK_FIELD_MEMORIZED, 0) == 0);
    cok_character_free(&c);
    /* A half-elf (5) above 199 becomes 70; at 199 it stays. */
    cok_field_set8(C, record, COK_FIELD_RACE, 5);
    cok_field_set16(C, record, COK_FIELD_AGE, 200);
    write_file(path, record, sizeof record);
    CHECK(cok_character_import(&c, R, dir, "CERA", "WHO", types, error, sizeof error));
    CHECK(cok_field_u16(L, c.record, COK_FIELD_AGE) == 70);
    cok_character_free(&c);
    cok_field_set16(C, record, COK_FIELD_AGE, 199);
    write_file(path, record, sizeof record);
    CHECK(cok_character_import(&c, R, dir, "CERA", "WHO", types, error, sizeof error));
    CHECK(cok_field_u16(L, c.record, COK_FIELD_AGE) == 199);
    cok_character_free(&c);
    CHECK(!cok_character_import(&c, R, dir, "NOBODY", "WHO", types, error, sizeof error));
    CHECK(strstr(error, "NOBODY.WHO") != NULL);
    cok_character_free(&c);
    static const char *const files[] = {"CERA.WHO", "CERA.STF", "CERA.SFX"};
    for (size_t i = 0; i < 3; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        remove(path);
    }
    CHECK(rmdir(dir) == 0);
}

/* Death Knights' saved characters (DKKAssets/SAVE): each read and written
 * again byte for byte but the far pointers; and its recompute: their
 * derived fields cleared and recomputed (twice, as stats and levels read
 * each other's) give them back exactly. */
static void test_death_knights_saved(void)
{
    static cok_item_types types;
    char error[256];
    if (!cok_item_types_read("DKKAssets/ITEMS", &types, error, sizeof error) ||
        read_whole("DKKAssets/SAVE/CHRDATA1.GAM", (uint8_t[1]){0}, 1) == 0) {
        puts("party: no Death Knights saved games in DKKAssets/SAVE; skipped");
        return;
    }
    const cok_rules *R = &cok_rules_death_knights;
    const cok_character_layout *L = R->character;
    char out[512];
    CHECK(test_temp_dir(out, sizeof out, "cok_party_dkk_saved") != NULL);
    static const char *const names[] = {"CHRDATA1", "CHRDATA2", "CHRDATA3", "CHRDATA4",
                                        "CHRDATA5", "CHRDATA6", "CHRDATB1", "CHRDATB2",
                                        "CHRDATB3", "CHRDATB4", "CHRDATB5", "CHRDATB6"};
    static const char *const extensions[] = {"GAM", "WIZ", "JNK", "EFF"};
    static const cok_field derived[] = {
        COK_FIELD_BASE_THAC0, COK_FIELD_SAVING_THROW, COK_FIELD_HIGHEST_LEVEL,
        COK_FIELD_USABLE_CLASSES, COK_FIELD_SPELLS_A_DAY, COK_FIELD_ITEM_COUNT, COK_FIELD_HANDS,
        COK_FIELD_SAVE_BONUS, COK_FIELD_WEIGHT, COK_FIELD_THAC0, COK_FIELD_AC,
        COK_FIELD_AC_BEHIND, COK_FIELD_DICE, COK_FIELD_DICE_SIDES, COK_FIELD_DAMAGE_BONUS,
        COK_FIELD_MOVEMENT};
    size_t blocks = 0, thieves = 0;
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i) {
        cok_character c;
        CHECK(cok_character_read(&c, R, "DKKAssets/SAVE", names[i], &types, error,
                                 sizeof error));
        blocks += cok_field_has_spells(L, c.record);
        CHECK(cok_character_write(&c, R, out, names[i], error, sizeof error));
        for (size_t e = 0; e < 4; ++e) {
            char a[600], b[600];
            snprintf(a, sizeof a, "DKKAssets/SAVE/%s.%s", names[i], extensions[e]);
            snprintf(b, sizeof b, "%s/%s.%s", out, names[i], extensions[e]);
            CHECK(same_file(a, b, extensions[e], L));
            remove(b);
        }
        uint8_t kept[COK_CHARACTER_MAX];
        memcpy(kept, c.record, sizeof kept);
        for (size_t f = 0; f < sizeof derived / sizeof *derived; ++f)
            if (!cok_field_in_spells(L, derived[f]) || cok_field_has_spells(L, c.record))
                memset(c.record + cok_field_offset(L, derived[f]), 0x5a,
                       cok_field_size(L, derived[f]));
        if (cok_field_s8_at(L, c.record, COK_FIELD_CLASS_LEVEL, 6) > 0) {
            memset(c.record + cok_field_offset(L, COK_FIELD_THIEF_SKILL), 0x5a, 8);
            ++thieves;
        }
        for (int k = 0; k < 2; ++k)
            CHECK(cok_character_stats(&c, R, &types, error, sizeof error) &&
                  cok_character_levels(&c, R, &types, error, sizeof error));
        for (size_t k = 0; k < L->size; ++k) {
            if (kept[k] != c.record[k])
                fprintf(stderr, "%s +0x%03zx: saved %02x, recomputed %02x\n", names[i], k,
                        kept[k], c.record[k]);
            CHECK(kept[k] == c.record[k]);
        }
        cok_character_free(&c);
    }
    /* All but one fighter/thief hold a spells' block; three are thieves. */
    CHECK(blocks == 11 && thieves == 3);
    CHECK(rmdir(out) == 0);
}

/* Champions' saved characters (SAVE/, as roster files) imported into
 * Death Knights and written as its saved game writes them: the files the
 * original wrote (DOSBox-X, Add Character to Party, Champions, then Save
 * Current Game A and B, 2026-10-19), but for the far pointers, hashed
 * (FNV-1a over the .GAM, .WIZ, .JNK and .EFF, each pointer byte as 0). */
static void test_import_saved(void)
{
    static cok_item_types types;
    char error[256];
    if (read_whole("SAVE/CHRDATA1.SAV", (uint8_t[1]){0}, 1) == 0 ||
        !cok_item_types_read("DKKAssets/ITEMS", &types, error, sizeof error)) {
        puts("party: no Champions saved games in SAVE or no DKKAssets; import not checked");
        return;
    }
    static const struct {
        const char *base;
        uint32_t hash;
        size_t size;
    } original[] = {
        {"CHRDATA1", 0x0d7363e2u, 625}, {"CHRDATA2", 0xafc1a28au, 769},
        {"CHRDATA3", 0x58714e96u, 760}, {"CHRDATA4", 0x50c65c97u, 495},
        {"CHRDATA5", 0x30ea8301u, 697}, {"CHRDATA6", 0x1960b77cu, 760},
        {"CHRDATB1", 0xe2b78197u, 625}, {"CHRDATB2", 0x1abead51u, 445},
        {"CHRDATB3", 0x2a8939d6u, 445}, {"CHRDATB4", 0x890c94c6u, 454},
        {"CHRDATB5", 0x144bbbc8u, 243}, {"CHRDATB6", 0x2c214509u, 454},
    };
    const cok_rules *R = &cok_rules_death_knights;
    const cok_character_layout *L = R->character;
    char out[512];
    CHECK(test_temp_dir(out, sizeof out, "cok_party_import_saved") != NULL);
    for (size_t i = 0; i < sizeof original / sizeof *original; ++i) {
        cok_character c;
        CHECK(cok_character_import(&c, R, "SAVE", original[i].base, "SAV", &types, error,
                                   sizeof error));
        CHECK(cok_character_write(&c, R, out, original[i].base, error, sizeof error));
        cok_character_free(&c);
        static const char *const extensions[] = {"GAM", "WIZ", "JNK", "EFF"};
        uint32_t hash = 0x811c9dc5u;
        size_t size = 0;
        for (size_t e = 0; e < 4; ++e) {
            char path[600];
            static uint8_t data[4096];
            snprintf(path, sizeof path, "%s/%s.%s", out, original[i].base, extensions[e]);
            size_t n = read_whole(path, data, sizeof data);
            for (size_t k = 0; k < n; ++k) {
                bool pointer = e == 0   ? cok_field_pointer_at(L, (long)k)
                               : e == 2 ? k % COK_ITEM_SIZE >= 0x2a && k % COK_ITEM_SIZE < 0x2e
                               : e == 3 ? k % COK_EFFECT_SIZE >= 5
                                        : false;
                hash = (hash ^ (pointer ? 0 : data[k])) * 0x01000193u;
            }
            size += n;
            remove(path);
        }
        if (hash != original[i].hash || size != original[i].size)
            fprintf(stderr, "%s: 0x%08x, %zu bytes\n", original[i].base, hash, size);
        CHECK(hash == original[i].hash && size == original[i].size);
    }
    CHECK(rmdir(out) == 0);
}

/* Dark Queen's one-file characters (dqk 604b:05fd, 604b:2200), made up:
 * the record, its items' 17 bytes at +0x28 of the port's item, its
 * effects chained by their next pointers; the items' pointer holding
 * their count; a scroll's contents dropped with it; short data failing. */
static void test_dark_queen_files(void)
{
    const cok_rules *R = &cok_rules_dark_queen;
    const cok_character_layout *L = R->character;
    char error[256];
    cok_character c;
    memset(&c, 0, sizeof c);
    cok_field_set_name(L, c.record, "TANIS", 5);
    cok_field_set8(L, c.record, COK_FIELD_HIT_POINTS, 33);
    memset(c.record + cok_field_offset(L, COK_FIELD_ITEM_SLOTS), 0x77, 4); /* a stale pointer */
    uint8_t item[COK_ITEM_SIZE] = {0};
    for (int k = 0; k < 17; ++k) item[0x28 + k] = (uint8_t)(0x10 + k);
    CHECK(cok_character_insert_item(&c, 0, item));
    item[0x28] = 0x49; /* an empty scroll */
    item[0x35] = 0;
    CHECK(cok_character_insert_item(&c, 1, item));
    CHECK(cok_character_add_effect(&c, 0x5f, 300, 0xff, false) != NULL);
    CHECK(cok_character_add_effect(&c, 0x69, 0, 3, true) != NULL);
    static uint8_t bytes[1024];
    size_t size = cok_character_to_bytes(&c, R, bytes, sizeof bytes, error, sizeof error);
    CHECK(size == 0x19e + 2 * 17 + 2 * 9);
    CHECK(bytes[8] == 2 && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 0);
    CHECK(bytes[4] == 1 && bytes[5] == 0 && bytes[0xc] == 0); /* effects; no slot pointer */
    CHECK(memcmp(bytes + 0x68, "TANIS\0", 6) == 0 && bytes[0x198] == 33);
    CHECK(bytes[0x19e] == 0x10 && bytes[0x19e + 16] == 0x20 && bytes[0x19e + 17] == 0x49);
    const uint8_t *e = bytes + 0x19e + 34;
    CHECK(e[0] == 0x5f && e[1] == 0x2c && e[2] == 0x01 && e[3] == 0xff && e[4] == 0);
    CHECK(e[5] == 1 && e[6] == 0 && e[9] == 0x69 && e[13] == 1 && e[14] == 0 && e[17] == 0);
    /* Too small a buffer: the size needed, nothing written. */
    CHECK(cok_character_to_bytes(&c, R, bytes, 10, error, sizeof error) == size);
    cok_character d;
    size_t used;
    CHECK(cok_character_from_bytes(&d, R, bytes, size + 5, 0, &used, error, sizeof error));
    CHECK(used == size && !d.contents_dropped);
    CHECK(d.item_count == 2 && memcmp(d.items[0], c.items[0], COK_ITEM_SIZE) == 0);
    CHECK(d.items[1][0x28] == 0x49);
    CHECK(d.effects != NULL && d.effects->id == 0x5f && d.effects->duration == 300 &&
          d.effects->next != NULL && d.effects->next->on_remove && d.effects->next->next == NULL);
    for (size_t k = 0; k < L->file_size; ++k)
        CHECK(d.record[k] == (cok_field_pointer_at(L, (long)k) ? 0 : c.record[k]));
    /* The party's scrolls holding spells: an empty one is dropped too. */
    cok_character_free(&d);
    CHECK(cok_character_from_bytes(&d, R, bytes, size, 1, &used, error, sizeof error));
    CHECK(d.item_count == 1 && d.contents_dropped && used == size);
    cok_character_free(&d);
    /* A scroll with two spells: read past and dropped; the next item kept. */
    static uint8_t scroll[1024];
    memcpy(scroll, bytes, 0x19e);
    scroll[8] = 2;
    scroll[4] = 0;
    uint8_t *p = scroll + 0x19e;
    memset(p, 0, 17 * 4);
    p[0] = 0x49;
    p[0xd] = 2;
    p[17 * 3] = 0x22;
    CHECK(cok_character_from_bytes(&d, R, scroll, 0x19e + 17 * 4, 0, &used, error, sizeof error));
    CHECK(d.item_count == 1 && d.items[0][0x28] == 0x22 && d.contents_dropped && d.effects == NULL);
    CHECK(used == 0x19e + 17 * 4);
    /* The writer holds no contents to write. */
    d.items[0][0x28] = 0x49;
    d.items[0][0x35] = 1;
    CHECK(cok_character_to_bytes(&d, R, bytes, sizeof bytes, error, sizeof error) == 0);
    CHECK(strstr(error, "scroll") != NULL);
    cok_character_free(&d);
    /* A pointer whose low word is 0 still reads one item; short data fails. */
    scroll[8] = 0;
    scroll[10] = 1;
    p[0] = 0x22;
    CHECK(cok_character_from_bytes(&d, R, scroll, 0x19e + 17, 0, &used, error, sizeof error));
    CHECK(d.item_count == 1 && used == 0x19e + 17);
    cok_character_free(&d);
    CHECK(!cok_character_from_bytes(&d, R, scroll, 0x19e + 16, 0, &used, error, sizeof error));
    CHECK(!cok_character_from_bytes(&d, R, scroll, 0x19d, 0, &used, error, sizeof error));
    memcpy(scroll, bytes, size);
    CHECK(!cok_character_from_bytes(&d, R, scroll, size - 1, 0, &used, error, sizeof error));
    cok_character_free(&c);
}

/* Dark Queen's shipped characters (the .QCH files of DQKAssets/SAVE): each read and
 * written again byte for byte but the far pointers the original writes
 * as they are in memory (the items' holds their count, which the port
 * writes too); their names, items and effects. */
static void test_dark_queen_saved(void)
{
    static const struct {
        const char *base, *name;
        size_t items, effects, size;
    } shipped[] = {
        {"'CINDER'", "'CINDER'", 5, 1, 508},      {"BALTHIRN", "BALTHIR NOMOON", 5, 2, 517},
        {"ROARK'CR", "ROARK 'CROAKER'", 3, 3, 492}, {"ROLANDOF", "ROLAND OF DUST", 4, 0, 482},
        {"TIMASALI", "TIMASALIN", 5, 1, 508},     {"WALKERKE", "WALKER KEYS", 3, 2, 483},
    };
    const cok_rules *R = &cok_rules_dark_queen;
    const cok_character_layout *L = R->character;
    static cok_item_types types;
    char error[256];
    if (read_whole("DQKAssets/SAVE/ROLANDOF.QCH", (uint8_t[1]){0}, 1) == 0) {
        puts("party: no Dark Queen characters in DQKAssets/SAVE; skipped");
        return;
    }
    char out[512];
    CHECK(test_temp_dir(out, sizeof out, "cok_party_dqk_saved") != NULL);
    for (size_t i = 0; i < sizeof shipped / sizeof *shipped; ++i) {
        cok_character c;
        CHECK(cok_character_read(&c, R, "DQKAssets/SAVE", shipped[i].base, &types, error,
                                 sizeof error));
        char name[16];
        cok_field_name(L, c.record, name);
        size_t effects = 0;
        for (const cok_effect *e = c.effects; e != NULL; e = e->next) ++effects;
        CHECK(strcmp(name, shipped[i].name) == 0 && c.item_count == shipped[i].items &&
              effects == shipped[i].effects && !c.contents_dropped);
        CHECK(cok_field_u8(L, c.record, COK_FIELD_ITEM_COUNT) == c.item_count);
        CHECK(cok_character_write(&c, R, out, shipped[i].base, error, sizeof error));
        cok_character_free(&c);
        char a[600], b[600];
        static uint8_t x[1024], y[1024];
        snprintf(a, sizeof a, "DQKAssets/SAVE/%s.QCH", shipped[i].base);
        snprintf(b, sizeof b, "%s/%s.QCH", out, shipped[i].base);
        size_t nx = read_whole(a, x, sizeof x), ny = read_whole(b, y, sizeof y);
        CHECK(nx == shipped[i].size && ny == nx);
        size_t effects_at = nx - effects * COK_EFFECT_SIZE;
        for (size_t k = 0; k < nx; ++k) {
            bool items = k >= cok_field_offset(L, COK_FIELD_ITEMS) &&
                         k < cok_field_offset(L, COK_FIELD_ITEMS) + 4;
            bool pointer = k < L->file_size ? cok_field_pointer_at(L, (long)k) && !items
                                            : k >= effects_at && (k - effects_at) % 9 >= 5;
            if (!pointer && x[k] != y[k])
                fprintf(stderr, "%s +0x%03zx: %02x, written %02x\n", a, k, x[k], y[k]);
            CHECK(pointer || x[k] == y[k]);
        }
        remove(b);
    }
    CHECK(rmdir(out) == 0);
}

/* Death Knights' saved characters (DKKAssets/SAVE) imported into Dark
 * Queen (dqk 604b:0b82): the record's fields where the moves put them,
 * the name a C string, class 7's levels class 1's; the effects, the
 * items (but type 0x36) and the spells by the maps; written and read
 * back the same. */
static void test_dark_queen_import(void)
{
    static cok_item_types types;
    char error[256];
    if (read_whole("DKKAssets/SAVE/CHRDATA1.GAM", (uint8_t[1]){0}, 1) == 0) {
        puts("party: no Death Knights saved games in DKKAssets/SAVE; Dark Queen's import "
             "not checked");
        return;
    }
    const cok_rules *R = &cok_rules_dark_queen, *D = &cok_rules_death_knights;
    const cok_character_layout *L = R->character, *K = D->character;
    const cok_rules_import *I = R->characters.import;
    char out[512];
    CHECK(test_temp_dir(out, sizeof out, "cok_party_dqk_import") != NULL);
    static const char *const names[] = {"CHRDATA1", "CHRDATA2", "CHRDATA3", "CHRDATA4",
                                        "CHRDATA5", "CHRDATA6", "CHRDATB1", "CHRDATB2",
                                        "CHRDATB3", "CHRDATB4", "CHRDATB5", "CHRDATB6"};
    size_t known = 0, spells = 0, items = 0, effects = 0;
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i) {
        cok_character k, c;
        CHECK(cok_character_read(&k, D, "DKKAssets/SAVE", names[i], &types, error,
                                 sizeof error) ||
              (fprintf(stderr, "%s\n", error), false));
        CHECK(cok_character_import(&c, R, "DKKAssets/SAVE", names[i], "GAM", &types, error,
                                   sizeof error));
        char a[16], b[16];
        cok_field_name(K, k.record, a);
        cok_field_name(L, c.record, b);
        CHECK(strcmp(a, b) == 0 && strlen(b) < 16 && c.record[0x68 + strlen(b)] == 0);
        for (int s = 0; s < 6; ++s)
            CHECK(cok_field_u8_at(L, c.record, COK_FIELD_SCORE, s) ==
                      cok_field_u8_at(K, k.record, COK_FIELD_SCORE, s) &&
                  cok_field_u8_at(L, c.record, COK_FIELD_SCORE_BASE, s) ==
                      cok_field_u8_at(K, k.record, COK_FIELD_SCORE_BASE, s));
        CHECK(cok_field_u32(L, c.record, COK_FIELD_EXPERIENCE) ==
              cok_field_u32(K, k.record, COK_FIELD_EXPERIENCE));
        CHECK(cok_field_u8(L, c.record, COK_FIELD_HIT_POINTS) ==
              cok_field_u8(K, k.record, COK_FIELD_HIT_POINTS));
        uint8_t class_ = cok_field_u8(K, k.record, COK_FIELD_CLASS);
        CHECK(cok_field_u8(L, c.record, COK_FIELD_CLASS) == (class_ == 7 ? 1 : class_));
        for (int n = 0; n < 7; ++n) {
            CHECK(cok_field_u8_at(L, c.record, COK_FIELD_CLASS_LEVEL, n) ==
                  cok_field_u8_at(K, k.record, COK_FIELD_CLASS_LEVEL, n == 1 ? 7 : n));
            CHECK(cok_field_u8_at(L, c.record, COK_FIELD_FORMER_LEVEL, n) == 0);
        }
        CHECK(cok_field_u16_at(L, c.record, COK_FIELD_COINS, 4) ==
              cok_field_u16_at(K, k.record, COK_FIELD_COINS, 4));
        /* The spells: each one Death Knights knows known, by the map. */
        if (cok_field_has_spells(K, k.record)) {
            ++spells;
            for (unsigned id = 1; id <= 0x72; ++id) {
                if (cok_field_u8_at(K, k.record, COK_FIELD_KNOWN_SPELL, id) == 0) continue;
                unsigned to = id;
                for (size_t m = 0; m < I->spell_id_count; ++m)
                    if (I->spell_ids[m][0] == id) to = I->spell_ids[m][1];
                if (to > 128) continue;
                CHECK(c.record[0x157 + (to - 1) / 8] >> (to - 1) % 8 & 1);
                ++known;
            }
            static const int rows[4] = {0, 1, 3, 2};
            for (int r = 0; r < 4; ++r)
                for (int lv = 1; lv <= 8; ++lv)
                    CHECK(cok_field_u8_at2(L, c.record, COK_FIELD_SPELLS_A_DAY, rows[r], lv) ==
                          cok_field_u8_at2(K, k.record, COK_FIELD_SPELLS_A_DAY, r, lv));
        }
        size_t kept = 0;
        for (size_t n = 0; n < k.item_count; ++n) {
            uint8_t t = k.items[n][0x2e];
            if (t == 0x36) continue;
            CHECK(c.items[kept][0x28] == (t == 0x34 ? 0x33 : t));
            CHECK(memcmp(c.items[kept] + 0x2c, k.items[n] + 0x37, 2) == 0); /* weight */
            CHECK(c.items[kept][0x35] == k.items[n][0x39]);
            ++kept;
        }
        CHECK(c.item_count == kept);
        size_t ne = 0, ke = 0;
        for (const cok_effect *e = c.effects; e != NULL; e = e->next) ++ne;
        for (const cok_effect *e = k.effects; e != NULL; e = e->next) ++ke;
        CHECK(ne == ke);
        items += kept;
        effects += ne;
        /* Written as Dark Queen's file and read back the same. */
        CHECK(cok_character_write(&c, R, out, names[i], error, sizeof error));
        cok_character back;
        CHECK(cok_character_read(&back, R, out, names[i], &types, error, sizeof error));
        CHECK(memcmp(back.record, c.record, L->size) == 0 && back.item_count == c.item_count);
        for (size_t n = 0; n < c.item_count; ++n)
            CHECK(memcmp(back.items[n] + 0x28, c.items[n] + 0x28, 17) == 0);
        cok_character_free(&back);
        cok_character_free(&c);
        cok_character_free(&k);
        char path[600];
        snprintf(path, sizeof path, "%s/%s.QCH", out, names[i]);
        remove(path);
    }
    CHECK(spells > 0 && known > 0 && items > 0 && effects > 0);
    CHECK(rmdir(out) == 0);
}

/* Dark Queen's import made up (dqk 604b:0b82): a Death Knights record of
 * class 7 with levels in class 7; an item of type 0x36 dropped, 0x34 made
 * 0x33, a scroll's three spells and another's power by their maps, the
 * name's words by theirs; an effect's id; the spells. A short record or
 * too many effects. */
static void test_dark_queen_import_made(void)
{
    const cok_rules *R = &cok_rules_dark_queen;
    const cok_character_layout *L = R->character;
    static cok_item_types types;
    char error[256], dir[512], path[700];
    CHECK(test_temp_dir(dir, sizeof dir, "cok_party_dqk_made") != NULL);
    uint8_t gam[0xd8] = {0};
    memcpy(gam, "\003BOBXXXXXXXXXXXXX", 16);
    gam[0x21] = 7;    /* class */
    gam[0x45 + 7] = 9; /* its level */
    gam[0x4d + 7] = 8; /* before a drain */
    gam[0x45 + 2] = 3;
    gam[0xd6] = 44;   /* hit points */
    snprintf(path, sizeof path, "%s/BOB.PC", dir);
    write_file(path, gam, sizeof gam);
    uint8_t jnk[4 * 63] = {0};
    jnk[0 * 63 + 0x2e] = 0x36;
    jnk[1 * 63 + 0x2e] = 0x34;
    jnk[1 * 63 + 0x3e] = 0x80;
    uint8_t *scroll = jnk + 2 * 63, *sword = jnk + 3 * 63;
    scroll[0x2e] = 0x27;
    scroll[0x3c] = 0x82;
    scroll[0x3d] = 0x04;
    scroll[0x3e] = 0x07;
    memcpy(sword, "\005SWORD", 6);
    sword[0x29] = 0x5a;
    sword[0x2e] = 0x11;
    sword[0x2f] = 0x0c;
    sword[0x30] = 0x26;
    sword[0x31] = 0x0d;
    sword[0x37] = 0x34;
    sword[0x38] = 0x12;
    sword[0x39] = 3;
    for (int k = 0; k < 5; ++k) sword[0x32 + k] = (uint8_t)(0xa0 + k); /* +0x32-+0x36 */
    sword[0x3a] = 0x66;
    sword[0x3b] = 0x77;
    sword[0x3d] = 0x02;
    sword[0x3e] = 0x80;
    snprintf(path, sizeof path, "%s/BOB.JNK", dir);
    write_file(path, jnk, sizeof jnk);
    uint8_t eff[9] = {0x09, 0x10, 0x00, 0x05, 0x01, 0x12, 0x34, 0x56, 0x78};
    snprintf(path, sizeof path, "%s/BOB.EFF", dir);
    write_file(path, eff, sizeof eff);
    uint8_t wiz[0xdc] = {0};
    wiz[0] = 0x11;
    wiz[0x4a + 1] = 1; /* knows spell 2: 90 in Dark Queen */
    wiz[0x4a + 0] = 1; /* and 1, kept */
    for (int r = 0; r < 4; ++r) wiz[0xbc + 8 * r] = (uint8_t)(r + 1);
    snprintf(path, sizeof path, "%s/BOB.WIZ", dir);
    write_file(path, wiz, sizeof wiz);
    cok_character c;
    CHECK(cok_character_import(&c, R, dir, "BOB", "PC", &types, error, sizeof error));
    uint8_t *r = c.record;
    char name[16];
    CHECK(cok_field_name(L, r, name) == 3 && strcmp(name, "BOB") == 0 && r[0x6b] == 0);
    CHECK(r[0x5a] == 1 && r[0x5b] == 0); /* class 7 is 1 */
    CHECK(cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 1) == 9 &&
          cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 2) == 3 &&
          cok_field_u8_at(L, r, COK_FIELD_UNDRAINED_LEVEL, 1) == 8 &&
          cok_field_u8_at(L, r, COK_FIELD_FORMER_LEVEL, 0) == 0);
    CHECK(cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == 44);
    CHECK(c.item_count == 3);
    CHECK(c.items[0][0x28] == 0x33 && c.items[0][0x38] == 0);
    CHECK(c.items[1][0x28] == 0x27 && c.items[1][0x36] == (90 | 0x80) &&
          c.items[1][0x37] == 96 && c.items[1][0x38] == 105);
    const uint8_t *s = c.items[2];
    CHECK(s[4] == 0x5a && memcmp(s + 5, "SWORD", 6) == 0);
    CHECK(s[0x28] == 0x11 && s[0x29] == 0x8a && s[0x2a] == 0x8b && s[0x2b] == 0x0d);
    CHECK(s[0x2c] == 0x34 && s[0x2d] == 0x12 && s[0x35] == 3);
    CHECK(s[0x2e] == 0x66 && s[0x2f] == 0x77);
    for (int k = 0; k < 5; ++k) CHECK(s[0x30 + k] == 0xa0 + k);
    CHECK(s[0x37] == 0x81 && s[0x38] == 0x80); /* an effect: 604b:1cc7's 2 */
    CHECK(c.effects != NULL && c.effects->id == 0x4a && c.effects->duration == 0x10 &&
          c.effects->value == 5 && c.effects->on_remove && c.effects->next == NULL);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_MEMORIZED, 0) == 102);
    CHECK(r[0x157] == 0x01 && r[0x157 + 89 / 8] == 1 << 89 % 8);
    CHECK(cok_field_u8_at2(L, r, COK_FIELD_SPELLS_A_DAY, 0, 1) == 1 &&
          cok_field_u8_at2(L, r, COK_FIELD_SPELLS_A_DAY, 1, 1) == 2 &&
          cok_field_u8_at2(L, r, COK_FIELD_SPELLS_A_DAY, 3, 1) == 3 &&
          cok_field_u8_at2(L, r, COK_FIELD_SPELLS_A_DAY, 2, 1) == 4);
    cok_character_free(&c);
    /* Too many effects (0x8d bytes): none; a record short of 0xd8: no import. */
    uint8_t many[0x8d] = {0x09};
    snprintf(path, sizeof path, "%s/BOB.EFF", dir);
    write_file(path, many, sizeof many);
    CHECK(cok_character_import(&c, R, dir, "BOB", "PC", &types, error, sizeof error));
    CHECK(c.effects == NULL && c.item_count == 3);
    cok_character_free(&c);
    remove(path);
    snprintf(path, sizeof path, "%s/BOB.PC", dir);
    write_file(path, gam, sizeof gam - 1);
    CHECK(!cok_character_import(&c, R, dir, "BOB", "PC", &types, error, sizeof error));
    CHECK(strstr(error, "604b:0cd7") != NULL);
    static const char *const files[] = {"BOB.PC", "BOB.JNK", "BOB.WIZ"};
    for (size_t i = 0; i < 3; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        remove(path);
    }
    CHECK(rmdir(dir) == 0);
}

/* The six characters of Dark Queen's shipped saved game (SAVE/SAVGAMA.QSV:
 * 0x400 bytes of variables, 15 more, their count, then each as a .QCH,
 * dqk 604b:28f8) read as cok_character_from_bytes reads one: the
 * original's party in memory once it had loaded them (DOSBox-X, Load
 * from saved game A, 2026-10-27: each record at DS:c6e5's list, after its
 * recompute 177e:0be2), hashed (FNV-1a over each record, its far pointers
 * as 0, then its items' 17 bytes and its effects' 5), and what the port
 * reads, the same. */
static void test_dark_queen_party(void)
{
    static uint8_t data[8192];
    size_t size = read_whole("DQKAssets/SAVE/SAVGAMA.QSV", data, sizeof data);
    if (size == 0) {
        puts("party: no DQKAssets/SAVE/SAVGAMA.QSV; Dark Queen's party not checked");
        return;
    }
    const cok_rules *R = &cok_rules_dark_queen;
    const cok_character_layout *L = R->character;
    char error[256];
    size_t at = 0x40f, count = (size_t)(data[0x40d] | data[0x40e] << 8);
    CHECK(count == 6);
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = 0; i < count; ++i) {
        cok_character c;
        size_t used;
        CHECK(cok_character_from_bytes(&c, R, data + at, size - at, 0, &used, error,
                                       sizeof error));
        at += used;
        for (size_t k = 0; k < L->size; ++k)
            hash = (hash ^ (cok_field_pointer_at(L, (long)k) ? 0 : c.record[k])) * 0x01000193u;
        for (size_t n = 0; n < c.item_count; ++n)
            for (size_t k = 0; k < 17; ++k) hash = (hash ^ c.items[n][0x28 + k]) * 0x01000193u;
        for (const cok_effect *e = c.effects; e != NULL; e = e->next) {
            const uint8_t b[5] = {e->id, (uint8_t)e->duration, (uint8_t)(e->duration >> 8),
                                  e->value, e->on_remove};
            for (size_t k = 0; k < 5; ++k) hash = (hash ^ b[k]) * 0x01000193u;
        }
        cok_character_free(&c);
    }
    if (hash != 0x2077261du) fprintf(stderr, "Dark Queen's party: 0x%08x\n", hash);
    CHECK(hash == 0x2077261du);
    CHECK(at == 0xfbd); /* the item lists and vault after them */
}

int main(void)
{
    test_former_classes();
    test_names();
    test_members();
    test_damage();
    test_draw();
    test_files();
    test_items();
    test_stats();
    test_levels();
    test_saved();
    test_death_knights_files();
    test_death_knights_recompute();
    test_import();
    test_death_knights_saved();
    test_import_saved();
    test_dark_queen_files();
    test_dark_queen_saved();
    test_dark_queen_import();
    test_dark_queen_import_made();
    test_dark_queen_party();
    puts("party tests passed");
    return 0;
}
