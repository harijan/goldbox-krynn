/* The Dark Queen of Krynn's battle ground's set and palette (the arena_tiles
 * hook, which combat.c's build calls in place of the 4-bit tiles, dqk
 * 59c7:0e70 and 204c:0007): the palette's colour cycles draw random(8) from
 * the seed (261b:00b1), one for each cycle the set has (DUNGCOM 3 and 6 have
 * one; DUNGCOM 4 and WILDCOM 4 none). The game opened on DQKAssets/ (skipped
 * without). */
#include "adventure.h"
#include "ecl.h"
#include "hlib.h"
#include "queen.h"
#include "rules.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;
static cok_adventure game;

/* Whether queen_icon's slot and image hold record id of the library file, byte for byte
 * (hlib's record). */
static bool icon_holds(unsigned slot, unsigned image, const char *file, unsigned id)
{
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library lib;
    const uint8_t *want = NULL, *got = NULL;
    size_t want_size = 0, got_size = 0;
    bool ok = hlib_read_file(file, &bytes, &n) == HLIB_OK &&
              hlib_parse(bytes, n, &lib) == HLIB_OK &&
              hlib_record(&lib, id, &want, &want_size) == HLIB_OK &&
              queen_icon(&game, slot, image, &got, &got_size) && got_size == want_size &&
              (want_size == 0 || memcmp(got, want, want_size) == 0);
    free(bytes);
    return ok;
}

int main(void)
{
    struct stat st;
    if (stat("DQKAssets/DISK2/DUNGCOM.TLB", &st) != 0) {
        puts("queen arena: no DQKAssets/; the battle's set and palette not checked");
        return 0;
    }
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    const cok_drawing *d = cok_adventure_drawing(&game);
    CHECK(d != NULL && d->arena_tiles != NULL);
    static const uint32_t seeds[] = {0, 1, 0x1756582c, 12345};
    for (size_t i = 0; i < sizeof seeds / sizeof *seeds; ++i) {
        /* The one draw a set with a cycle makes, from the same seed. */
        uint32_t one = seeds[i];
        cok_tp_random(&one, 8);
        for (unsigned set = 1; set <= 6; ++set) {
            game.vm.seed = seeds[i];
            CHECK(d->arena_tiles(&game, false, set));
            uint32_t want = set == 3 || set == 6 ? one : seeds[i];
            CHECK(game.vm.seed == want);
        }
        /* WILDCOM 4 has no cycle. */
        game.vm.seed = seeds[i];
        CHECK(d->arena_tiles(&game, true, 4));
        CHECK(game.vm.seed == seeds[i]);
    }
    /* The wilderness' set 3 is not one of WILDCOM's (LOAD FILES' DS:ca03 is
     * the set): it fails, and the seed is kept. */
    game.vm.seed = 7;
    CHECK(!d->arena_tiles(&game, true, 3) && game.vm.seed == 7);

    /* The monsters' icons (204c:00df, CPIC): record id into slot and record id + 0x80 into
     * slot + 0x26 (DS:b17d). */
    const char *cpic = "DQKAssets/DISK2/CPIC.TLB", *comspr = "DQKAssets/DISK2/COMSPR.TLB";
    CHECK(d->arena_load_icon(&game, "CPIC.TLB", 64, 8));
    CHECK(icon_holds(8, 0, cpic, 64) && icon_holds(8, 1, cpic, 192));
    CHECK(d->arena_load_icon(&game, "CPIC.TLB", 49, 9));
    CHECK(icon_holds(9, 0, cpic, 49) && icon_holds(9, 1, cpic, 177));
    /* CPIC 72 has no record 72: pose 0 is 100 + id (172, 004b:0056-0087), and pose 1 (200)
     * is missing, which stops with 19a4:004b (Q1). */
    CHECK(!d->arena_load_icon(&game, "CPIC.TLB", 72, 10));
    CHECK(strstr(game.error, "19a4:004b") != NULL && icon_holds(10, 0, cpic, 172));
    /* Another name than the rules' icons stops, cited 204c:00df; a slot past the table
     * stops, cited DS:b17d. */
    CHECK(!d->arena_load_icon(&game, "DUNGCOM.TLB", 64, 11) && strstr(game.error, "204c:00df"));
    CHECK(!d->arena_load_icon(&game, "CPIC.TLB", 64, 0x26) && strstr(game.error, "DS:b17d"));
    /* The missiles and cursor (59c7:0e80-0f06): arena_tiles loads COMSPR 15 and 0x8f into
     * slot 0x1e, the cursor's box and its image 1. */
    CHECK(d->arena_tiles(&game, false, 4));
    CHECK(icon_holds(0x1e, 0, comspr, 15) && icon_holds(0x1e, 1, comspr, 0x8f));
    cok_adventure_close(&game);
    puts("queen arena tests passed");
    return 0;
}
