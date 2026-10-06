#define _POSIX_C_SOURCE 200809L /* mkdtemp */

#include "party.h"

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
}

static void test_members(void)
{
    cok_party party = {0};
    CHECK(cok_party_special(&party, NULL, 0x48) == NULL);
    cok_character *a = make("A", 5, 5, 5), *b = make("B", 5, 5, 5), *c = make("C", 5, 5, 5);
    CHECK(cok_party_add(&party, a) && cok_party_add(&party, b) && cok_party_add(&party, c));
    CHECK(a->record[0x137] == 0 && b->record[0x137] == 1 && c->record[0x137] == 2);
    CHECK(cok_party_index(&party, b->record) == 1 && cok_party_index(&party, NULL) == 3);
    /* Up and down wrap; other keys pick the first. */
    CHECK(cok_party_special(&party, a->record, 0x48) == c->record);
    CHECK(cok_party_special(&party, c->record, 0x48) == b->record);
    CHECK(cok_party_special(&party, c->record, 0x50) == a->record);
    CHECK(cok_party_special(&party, a->record, 0x50) == b->record);
    CHECK(cok_party_special(&party, c->record, 0x4b) == a->record);
    CHECK(cok_party_special(&party, NULL, 0x48) == NULL);
    /* A removed member's icon slot is the next one given. */
    cok_party_remove(&party, 1);
    CHECK(party.count == 2 && cok_party_record(&party, 1) == c->record);
    cok_character *d = make("D", 5, 5, 5);
    CHECK(cok_party_add(&party, d) && d->record[0x137] == 1);
    for (int i = 0; i < 5; ++i) CHECK(cok_party_add(&party, make("E", 1, 1, 1)));
    cok_character *extra = make("F", 1, 1, 1);
    CHECK(!cok_party_add(&party, extra) && party.count == COK_PARTY_MAX);
    free(extra);
    cok_party_free(&party);
    CHECK(party.count == 0);
}

static void test_damage(void)
{
    cok_character *c = make("A", 10, 10, 5);
    uint8_t *r = c->record;
    cok_character_damage(r, 4);
    CHECK(r[0x197] == 6 && r[0x188] == 0 && r[0x189] == 1);
    cok_character_damage(r, 6);
    CHECK(r[0x197] == 0 && r[0x188] == 4 && r[0x189] == 0);
    r[0x197] = 10;
    r[0x188] = 0;
    r[0x189] = 1;
    cok_character_damage(r, 19);
    CHECK(r[0x197] == 0 && r[0x188] == 5 && r[0x189] == 0);
    r[0x197] = 10;
    r[0x188] = 0;
    cok_character_damage(r, 20);
    CHECK(r[0x188] == 6);
    /* Status 1 dies at its last hit point. */
    r[0x197] = 3;
    r[0x188] = 1;
    cok_character_damage(r, 3);
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
    CHECK(cok_party_add(&party, a) && cok_party_add(&party, b) && cok_party_add(&party, c));
    cok_picture_fill(&screen, 0, 7 * 8, 40, 8, 9); /* a stale row */
    cok_party_draw(&screen, &font, &party, a->record, 0x11, false);
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
    cok_party_draw(&screen, &font, &party, NULL, 1, true);
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
    char dir[] = "/tmp/cok_party_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[256];

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
    char error[256];
    CHECK(cok_saved_game_read(path, &game, error, sizeof error));
    CHECK(game.file == 2 && game.mem4b00[0xe6] == 1 && game.mem7c00[0x312] == 3);
    CHECK(game.mem7a00[0x1ff] == 0x1200);
    CHECK(game.map_x == -1 && game.map_y == 13 && game.direction == 2 && game.ahead == 4 &&
          game.square == 5 && game.last_mode == 3 && game.mode == 4);
    CHECK(game.wall_ids[0] == 7 && game.wall_slots[0] == 1 && game.wall_ids[1] == -1 &&
          game.wall_ids[2] == 9 && game.wall_slots[2] == 3);
    CHECK(game.count == 2 && strcmp(game.names[0], "CHRDATB1") == 0 &&
          strcmp(game.names[1], "OTHER") == 0);
    write_file(path, save, sizeof save - 1);
    CHECK(!cok_saved_game_read(path, &game, error, sizeof error));

    /* A character with two items, part of a third, and an effect. */
    uint8_t record[COK_CHARACTER_SIZE] = {3, 'K', 'A', 'L'};
    memset(record + 0x17f, 0xaa, 4);
    memset(record + 0xe3, 0xbb, 4);
    record[0x197] = 21;
    snprintf(path, sizeof path, "%s/KAL.SAV", dir);
    write_file(path, record, sizeof record);
    uint8_t items[2 * COK_ITEM_SIZE + 10] = {0};
    items[COK_ITEM_SIZE + 0x2e] = 0x1e;
    snprintf(path, sizeof path, "%s/KAL.STF", dir);
    write_file(path, items, sizeof items);
    uint8_t effects[COK_EFFECT_SIZE] = {0x2f, 10, 0, 0xff};
    snprintf(path, sizeof path, "%s/KAL.SFX", dir);
    write_file(path, effects, sizeof effects);
    cok_character c;
    CHECK(cok_character_read(&c, dir, "KAL", error, sizeof error));
    CHECK(c.record[0] == 3 && c.record[0x197] == 21 && c.record[0x17f] == 0 && c.record[0xe3] == 0);
    CHECK(c.item_count == 2 && c.items[1][0x2e] == 0x1e);
    CHECK(c.effect_count == 1 && c.effects[0][0] == 0x2f);
    cok_character_free(&c);
    /* Items and effects are optional. */
    snprintf(path, sizeof path, "%s/KAL.STF", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/KAL.SFX", dir);
    remove(path);
    CHECK(cok_character_read(&c, dir, "KAL", error, sizeof error));
    CHECK(c.item_count == 0 && c.items == NULL && c.effect_count == 0);
    cok_character_free(&c);
    CHECK(!cok_character_read(&c, dir, "NOBODY", error, sizeof error));
    CHECK(strstr(error, "NOBODY.SAV") != NULL);
    cok_character_free(&c);

    snprintf(path, sizeof path, "%s/KAL.SAV", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/SAVGAMB.DAT", dir);
    remove(path);
    CHECK(rmdir(dir) == 0);
}

int main(void)
{
    test_names();
    test_members();
    test_damage();
    test_draw();
    test_files();
    puts("party tests passed");
    return 0;
}
