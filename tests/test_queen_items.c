#include "data_dark_queen_items.h"
#include "party.h"
#include "queen_items.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static size_t read_whole(const char *path, uint8_t *out, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    size_t n = fread(out, 1, size, f);
    fclose(f);
    return n;
}

/* The words' table (DS:3f55, DS 0x422e) and their strings, byte for byte
 * as build/DQK_FULL.EXE has them. */
static void test_words(void)
{
    static uint8_t exe[0x80000];
    size_t size = read_whole("build/DQK_FULL.EXE", exe, sizeof exe);
    if (size == 0) {
        puts("queen items: no build/DQK_FULL.EXE; the words not checked against it");
        return;
    }
    size_t ds = 2004 * 16 + (size_t)(0x422e - 0x1000) * 16;
    for (unsigned id = 0; id < COK_QUEEN_ITEM_WORDS; ++id) {
        const uint8_t *p = exe + ds + 0x3f55 + 4 * id;
        unsigned off = (unsigned)(p[0] | p[1] << 8);
        CHECK(p[2] == 0x2e && p[3] == 0x32); /* DS, before the loader's relocation */
        CHECK(off == cok_queen_item_words[id].offset);
        CHECK(strcmp((const char *)exe + ds + off, cok_queen_item_words[id].text) == 0);
    }
}

/* The words, the ready column and the counts. */
static void test_names(void)
{
    static uint8_t data[8192];
    if (read_whole("DQKAssets/SAVE/SAVGAMA.QSV", data, sizeof data) != 0) {
        /* Roland of Dust's four items as the original lists them. */
        cok_character c;
        size_t used;
        char error[200], out[96];
        CHECK(cok_character_from_bytes(&c, &cok_rules_dark_queen, data + 0x40f, sizeof data - 0x40f,
                                       0, &used, error, sizeof error));
        static const char *const names[] = {"Long Sword +2 ", "Helm +2 ", "Shield +2 ",
                                            "Solamnic Plate "};
        CHECK(c.item_count == 4);
        cok_party party = {0};
        for (size_t i = 0; i < 4; ++i) {
            queen_item_name(&party, c.items[i], false, out, sizeof out);
            CHECK(strcmp(out, names[i]) == 0);
        }
        cok_character_free(&c);
    }
    cok_party party = {0};
    char out[96];
    uint8_t item[COK_ITEM_SIZE] = {0};
    /* Long Sword +2 (the shipped saved game's first item: words 18 at +0x2b
     * and 112 at +0x2a, last to first). */
    item[0x28] = 0x12;
    item[0x2a] = 112;
    item[0x2b] = 18;
    queen_item_name(&party, item, false, out, sizeof out);
    CHECK(strcmp(out, "Long Sword +2 ") == 0);
    /* The ready column. */
    queen_item_name(&party, item, true, out, sizeof out);
    CHECK(strcmp(out, " No\t Long Sword +2 ") == 0);
    item[0x32] = 1;
    queen_item_name(&party, item, true, out, sizeof out);
    CHECK(strcmp(out, " Yes\tLong Sword +2 ") == 0);
    /* An unidentified word (+0x33's bit 3 - i) is left out. */
    item[0x33] = 1u << 1; /* the word +0x2a */
    queen_item_name(&party, item, false, out, sizeof out);
    CHECK(strcmp(out, "Long Sword ") == 0);
    item[0x33] = 0;
    /* A count, and the plural of what a count's items are: arrows (type
     * 0x1e), three words (+0x29-+0x2b), the last of them in the plural. */
    memset(item, 0, sizeof item);
    item[0x28] = 0x1e;
    item[0x2b] = 30; /* Arrow */
    item[0x35] = 12;
    queen_item_name(&party, item, false, out, sizeof out);
    CHECK(strcmp(out, "12 Arrows ") == 0);
    item[0x35] = 1;
    queen_item_name(&party, item, false, out, sizeof out);
    CHECK(strcmp(out, "1 Arrow ") == 0);
    /* A count's first word, plural: Darts, 3 of them (the word at +0x29
     * alone: 177e:03fc's "s " after it). */
    memset(item, 0, sizeof item);
    item[0x28] = 5;
    item[0x29] = 5; /* Dart */
    item[0x35] = 3;
    queen_item_name(&party, item, false, out, sizeof out);
    CHECK(strcmp(out, "3 Darts ") == 0);
    /* The word at +0x2b hidden (+0x33's bit 0) and the one at +0x2a not. */
    memset(item, 0, sizeof item);
    item[0x2a] = 112;
    item[0x2b] = 18;
    item[0x33] = 1;
    queen_item_name(&party, item, false, out, sizeof out);
    CHECK(strcmp(out, "+2 ") == 0);
    /* A scroll's count (of its spells) is printed as its second word, not
     * before the name; its first word is in the plural: 3 Scrolls (as
     * 177e:03fc reads; no shipped game has one to check against). */
    memset(item, 0, sizeof item);
    item[0x28] = 0x49;
    item[0x29] = 39; /* Scroll */
    item[0x2a] = 100; /* 1 Spell */
    item[0x35] = 3;
    queen_item_name(&party, item, false, out, sizeof out);
    CHECK(strcmp(out, "3 Scrolls ") == 0);
}

int main(void)
{
    test_words();
    test_names();
    puts("queen items tests passed");
    return 0;
}
