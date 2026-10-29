/* The Dark Queen of Krynn's silent training (66e5:1a90, over 6be8:0022:
 * src/queen_train.h's queen_train_silently) on characters of the shipped
 * saved game, the game opened on DQKAssets/ (skipped without): it trains
 * every class that can, whatever the hall, and puts the hall's variable
 * (0x30) and the debug state back. The camp's Train and Knight new Order are
 * checked against the original by tools/dosbox/checks/dqk_train.py and
 * tests/test_dark_queen.py. */
#include "adventure.h"
#include "queen.h"
#include "queen_levels.h"
#include "queen_train.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;
static cok_adventure game;

static int32_t experience(const uint8_t *c)
{
    return (int32_t)((uint32_t)c[0x44] | (uint32_t)c[0x45] << 8 | (uint32_t)c[0x46] << 16 |
                     (uint32_t)c[0x47] << 24);
}

/* The character of the saved game at index, with this experience. */
static void saved(size_t index, int32_t xp, cok_character *c)
{
    static uint8_t data[8192];
    FILE *f = fopen("DQKAssets/SAVE/SAVGAMA.QSV", "rb");
    CHECK(f != NULL);
    size_t n = fread(data, 1, sizeof data, f);
    fclose(f);
    size_t at = 0x40f, used = 0;
    char error[300];
    for (size_t i = 0; i <= index; ++i) {
        at += used;
        CHECK(cok_character_from_bytes(c, R, data + at, n - at, 0, &used, error, sizeof error));
        if (i < index) cok_character_free(c);
    }
    for (int k = 0; k < 4; ++k) c->record[0x44 + k] = (uint8_t)((uint32_t)xp >> (8 * k));
}

/* No class of c is left that its experience takes up a level in. */
static void nothing_left(const cok_character *c)
{
    const uint8_t *r = c->record;
    char error[300];
    for (unsigned k = 0; k < 7; ++k) {
        unsigned level = r[0xa5 + k];
        if (level == 0) continue;
        bool top;
        if (k == 1 && r[0x19a] == 3)
            top = level >= 0x28;
        else if (k == 1 && (r[0x19a] == 2 || r[0x19a] == 4))
            top = level >= 0x12;
        else
            top = cok_data_held(R->data, 0x17dc + (long)(r[0x58] | r[0x59] << 8) * 7 + (long)k) <= level;
        int32_t need;
        CHECK(cok_queen_experience(R, r, level + 1, k, &need, error, sizeof error));
        CHECK(top || need <= 0 || need > experience(r));
    }
}

static void test_silent(void)
{
    /* Roark 'Croaker' (a dwarf fighter and thief, index 4) with a million
     * and one: both classes train in the hall of knights alone (0x40),
     * which is put back, with the debug state (anywhere) as it was; the
     * experience is not held below the level after's. */
    cok_character roark;
    saved(4, 1000001, &roark);
    uint8_t before[7];
    memcpy(before, roark.record + 0xa5, 7);
    cok_ecl_store(&game.vm, 0x30, 0x40);
    queen_set_train_anywhere(&game, true);
    CHECK(queen_train_silently(&game, &roark));
    CHECK(cok_ecl_load_var(&game.vm, 0x30) == 0x40 && queen_train_anywhere(&game));
    CHECK(experience(roark.record) == 1000001);
    unsigned grew = 0;
    for (unsigned k = 0; k < 7; ++k) {
        CHECK(roark.record[0xa5 + k] >= before[k]);
        grew += roark.record[0xa5 + k] > before[k];
    }
    CHECK(grew >= 1);
    nothing_left(&roark);
    CHECK(roark.record[0xcd] == 0);
    /* Again: nothing more, nothing changed. */
    uint8_t levels[7];
    memcpy(levels, roark.record + 0xa5, 7);
    queen_set_train_anywhere(&game, false);
    CHECK(queen_train_silently(&game, &roark));
    CHECK(memcmp(levels, roark.record + 0xa5, 7) == 0 && !queen_train_anywhere(&game));
    cok_character_free(&roark);
}

int main(void)
{
    struct stat st;
    if (stat("DQKAssets/SAVE/SAVGAMA.QSV", &st) != 0) {
        puts("queen train: no DQKAssets/SAVE/SAVGAMA.QSV; silent training not checked");
        return 0;
    }
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    test_silent();
    cok_adventure_close(&game);
    puts("queen train tests passed");
    return 0;
}
