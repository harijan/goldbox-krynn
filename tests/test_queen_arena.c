/* The Dark Queen of Krynn's battle ground's set and palette (the arena_tiles
 * hook, which combat.c's build calls in place of the 4-bit tiles, dqk
 * 59c7:0e70 and 204c:0007): the palette's colour cycles draw random(8) from
 * the seed (261b:00b1), one for each cycle the set has (DUNGCOM 3 and 6 have
 * one; DUNGCOM 4 and WILDCOM 4 none). The game opened on DQKAssets/ (skipped
 * without). */
#include "adventure.h"
#include "ecl.h"
#include "rules.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;
static cok_adventure game;

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
    cok_adventure_close(&game);
    puts("queen arena tests passed");
    return 0;
}
