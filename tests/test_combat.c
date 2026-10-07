#include "combat.h"

#include "adventure.h"
#include "monster.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    const char *keys;
    size_t at, length;
    int pending;            /* Keys the rest loop's poll would see waiting. */
    unsigned battles;       /* Times the battlefield hook ran. */
    char log[16384];
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    return (unsigned char)s->keys[s->at++];
}

static bool pending(cok_adventure *game, void *context)
{
    (void)game;
    script *s = context;
    if (s->pending == 0) return false;
    --s->pending;
    return true;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static void battlefield(cok_adventure *game, void *context)
{
    (void)game;
    script *s = context;
    ++s->battles;
}

static script s;
static cok_adventure game;

static void reset(const char *keys)
{
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    game.vm.character = game.vm.saved_character = NULL;
    game.vm.restore_character = false;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    memset(game.view.map, 0, sizeof game.view.map);
    game.view.wrap = false;
    game.vm.mode = 4;
    game.vm.mem4b00[0xe6] = 1;
    game.vm.mem7c00[0x2e1] = 0xff;
    game.vm.file = 1;
    game.vm.map_x = game.vm.map_y = 7;
    game.vm.direction = 0;
    game.vm.seed = 1;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.input_ended = false;
    game.combat_stub = COK_COMBAT_UNPORTED;
    game.monsters = game.undead = 0;
    game.monsters_loaded = false;
    game.icon_slot = 8;
    s.keys = keys;
    s.at = 0;
    s.length = strlen(keys);
    s.pending = 0;
    s.log[0] = '\0';
}

/* A record as pins.py makes them for the original: named by a letter,
 * one cell, strength and dexterity 12, on side for the party or against
 * it. */
static cok_character *record(char name, uint8_t side, bool dead)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = 1;
    r[1] = (uint8_t)name;
    r[0x10] = r[0x11] = r[0x16] = r[0x17] = 12;
    r[0x62] = r[0x197] = 10;
    r[0xcf] = 1;
    r[0x189] = dead ? 0 : 1;
    r[0x188] = dead ? 6 : 0;
    r[0x18a] = side;
    CHECK(cok_party_append(&game.party, c));
    return c;
}

/* A party of party records and monsters against it, as pins.py lays them
 * out: the party at 7, 7 on a 3D map with no walls. */
static void fixture(unsigned party, unsigned monsters, uint8_t direction, uint16_t distance)
{
    for (unsigned i = 0; i < party + monsters; ++i) record((char)('A' + i), i >= party, false);
    game.vm.mem7c00[0x33e] = (uint16_t)party;
    game.vm.mem7c00[0x2c1] = distance;
    game.vm.direction = direction;
    game.vm.character = cok_party_record(&game.party, 0);
}

static bool at(unsigned n, int x, int y, uint8_t size)
{
    const cok_combatant *e = &game.combat.combatant[n];
    return e->x == x && e->y == y && e->size == size;
}

/* The tables match the original's data segment (DS 0x1bc6) in
 * build/START_FULL.EXE, if it has been built. */
static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("combat: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    for (size_t i = 0; i < cok_combat_table_count; ++i) {
        const cok_ds_table *t = &cok_combat_tables[i];
        for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    }
    /* Type 15's flags are 4: no river. */
    CHECK(exe[ds + 0x441] == 4);
}

static void test_footprints(void)
{
    int8_t dx, dy;
    CHECK(!cok_combat_footprint(0, 0, &dx, &dy));
    CHECK(cok_combat_footprint(1, 0, &dx, &dy) && dx == 0 && dy == 0);
    CHECK(!cok_combat_footprint(1, 1, &dx, &dy));
    /* 2 is a cell and the one below; 3 the one to the right; 4 both. */
    CHECK(cok_combat_footprint(2, 1, &dx, &dy) && dx == 0 && dy == 1);
    CHECK(!cok_combat_footprint(2, 2, &dx, &dy));
    CHECK(cok_combat_footprint(3, 1, &dx, &dy) && dx == 1 && dy == 0);
    CHECK(cok_combat_footprint(4, 3, &dx, &dy) && dx == 1 && dy == 1);
    /* Sizes 5-7 read the bytes after the table: far off any map. */
    CHECK(cok_combat_footprint(5, 0, &dx, &dy) && dx == 0x33 && dy == 0x34);
    CHECK(!cok_combat_footprint(8, 0, &dx, &dy));
}

/* Turbo Pascal's Random, count calls on. */
static uint32_t after(uint32_t seed, unsigned count)
{
    while (count-- > 0) seed = seed * 0x8088405u + 1u;
    return seed;
}

/* FNV-1a of a map's cells, to compare with maps the original built. */
static uint32_t hash(const cok_combat *c)
{
    uint32_t h = 0x811c9dc5u;
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y)
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) h = (h ^ c->cells[y][x]) * 0x01000193u;
    return h;
}

static void test_wilderness(void)
{
    cok_combat *c = &game.combat;
    /* Seed 12345, terrain type 15: 2,689 rolls, the river's d100 among
     * them though it draws none; trees column by column, a top above a
     * bottom. The original, in an emulator, gives the same map. */
    uint32_t seed = 12345;
    cok_combat_wilderness(c, 4, &seed);
    CHECK(seed == after(12345, 2689) && seed == 3846925982u);
    static const uint8_t row0[50] = {
        0x17, 0x17, 0x17, 0x17, 0x39, 0x37, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x23,
        0x17, 0x17, 0x17, 0x38, 0x17, 0x17, 0x17, 0x2d, 0x17, 0x20, 0x17, 0x17, 0x17,
        0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x34, 0x17, 0x17, 0x21,
        0x17, 0x17, 0x17, 0x17, 0x2f, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17};
    CHECK(memcmp(c->cells[0], row0, 50) == 0);
    unsigned tops = 0, bottoms = 0;
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y)
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) {
            uint8_t v = c->cells[y][x];
            CHECK(v == COK_COMBAT_FLOOR || (v >= 0x20 && v <= 0x3b));
            if (v >= 0x20 && v <= 0x25) {
                ++tops;
                CHECK(y < 24 && c->cells[y + 1][x] >= 0x26 && c->cells[y + 1][x] <= 0x29);
            }
            if (v >= 0x26 && v <= 0x29) ++bottoms;
        }
    CHECK(tops > 0 && tops == bottoms);
    /* The whole map, as the original builds it in an emulator. */
    CHECK(hash(c) == 0x1fbd3469u);
    /* With flag 0x10 a d100 up to 0x4b draws a river, a column of water
     * and rapids moving right a row (3cb2:0a00); 0x80 has no trees. */
    seed = 1;
    cok_combat_wilderness(c, 0x10, &seed);
    CHECK(seed == 3028203821u);
    CHECK(c->cells[0][19] >= 0x3c && c->cells[0][19] <= 0x3d && c->cells[0][20] >= 0x3e);
    CHECK(c->cells[1][20] >= 0x3c && c->cells[1][20] <= 0x3d && c->cells[2][21] <= 0x3d);
    CHECK(hash(c) == 0x2911f855u);
    /* One reaching column 49 at row 23, whose rapids then lie off the map. */
    seed = 22;
    cok_combat_wilderness(c, 0x10, &seed);
    CHECK(seed == 1675800179u && hash(c) == 0xc0e04a27u);
    CHECK(c->cells[23][49] >= 0x3c && c->cells[23][49] <= 0x3d && c->cells[24][49] == 0x17);
    seed = 7;
    cok_combat_wilderness(c, 0x80, &seed);
    CHECK(seed == 4213331734u);
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y)
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x)
            CHECK(c->cells[y][x] < 0x20 || c->cells[y][x] > 0x2a);
}

static void load_geo(unsigned file, uint8_t id)
{
    size_t size;
    uint8_t *data = cok_adventure_record(&game, "GEO", file, id, &size);
    CHECK(data != NULL && cok_view_set_map(&game.view, data, size));
    free(data);
}

static void test_dungeon(void)
{
    cok_combat *c = &game.combat;
    reset("");
    /* Throtl (GEO1 record 32) from 8, 8, as the original builds it in an
     * emulator: each square a slanted block of 6 by 5 cells. */
    load_geo(1, 32);
    uint32_t seed = 1;
    CHECK(cok_combat_dungeon(c, &game.view, 8, 8, &seed));
    CHECK(seed == 1); /* No rooms: no dice. */
    static const uint8_t row0[16] = {0x17, 0x17, 0x17, 0x17, 0x17, 0x02, 0x03, 0x06,
                                     0x06, 0x06, 0x06, 0x0a, 0x0e, 0x17, 0x17, 0x17};
    CHECK(memcmp(&c->cells[0][18], row0, 16) == 0);
    static const uint8_t row10[16] = {0x17, 0x17, 0x10, 0x0a, 0x0e, 0x17, 0x17, 0x17,
                                      0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x05, 0x0c};
    CHECK(memcmp(&c->cells[10][18], row10, 16) == 0);
    /* Whole maps the original builds (in an emulator, wrapping off the
     * map, the cells first 1): Throtl from two squares, Neraka, and
     * GEO3 record 99 and GEO2 record 48. */
    static const struct { unsigned file; uint8_t id; int8_t x, y; uint32_t hash; } maps[] = {
        {1, 32, 8, 8, 0x7a263b76u}, {1, 32, 3, 12, 0x39cfcd17u}, {2, 64, 2, 3, 0x8e2af30bu},
        {3, 99, 10, 4, 0xb7468523u}, {2, 48, 12, 14, 0x9f22d320u},
    };
    for (size_t i = 0; i < sizeof maps / sizeof *maps; ++i) {
        load_geo(maps[i].file, maps[i].id);
        game.view.wrap = true;
        memset(c->cells, 1, sizeof c->cells);
        seed = 1;
        CHECK(cok_combat_dungeon(c, &game.view, maps[i].x, maps[i].y, &seed));
        CHECK(seed == 1 && hash(c) == maps[i].hash);
    }
    /* Off the 3D map, a square's sides are walls but on the party's row,
     * where east and west are open (3cb2:0306): a party at the west edge
     * sees open floor beyond it, and a wall above and below. */
    reset("");
    seed = 1;
    CHECK(cok_combat_dungeon(c, &game.view, 0, 0, &seed));
    /* The party's row of blocks is one open corridor, walled to the
     * north; the row below, off the map, has its west walls. */
    for (int x = 0; x < COK_COMBAT_WIDTH; ++x) CHECK(c->cells[12][x] == COK_COMBAT_FLOOR);
    CHECK(c->cells[10][24] == 6 && c->cells[11][24] == 0x0b);
    CHECK(c->cells[17][10] == 4 && c->cells[17][11] == 0x0e);
    CHECK(c->cells[17][12] == COK_COMBAT_FLOOR);
    /* A room (square byte 0x40) with walls on two sides gets a table on a
     * d10 of 5 or less, and chairs beside it on 9 or less; its table
     * spots in the next block, not yet built, depend on what the heap
     * held: the port says so, and builds from what the cells held. */
    reset("");
    game.view.map[7 * 16 + 7] = 0x10;        /* a wall to the north */
    game.view.map[0x100 + 7 * 16 + 7] = 0x01; /* and to the west */
    game.view.map[0x200 + 7 * 16 + 7] = 0x40;
    memset(c->cells, 0x01, sizeof c->cells);
    seed = 2;
    CHECK(!cok_combat_dungeon(c, &game.view, 7, 7, &seed));
    /* With the unbuilt cells walls (1), as the original gives in an
     * emulator: a table at cells 4, 2 of the block, a chair to its east;
     * north of it the wall, west the room's west wall, south the floor
     * took none. */
    CHECK(c->cells[12][25] == 0x1a && c->cells[12][26] == 0x1b && c->cells[13][25] == 0x0e);
    CHECK(seed == 2336949529u && hash(c) == 0x75653472u);
    static const struct { uint32_t seed, after, hash; } rooms[] = {
        {3, 3803820438u, 0x4267e6ccu},  {4, 975724051u, 0x4267e6ccu},
        {5, 2442594960u, 0x33c41fd3u},  {6, 2857528395u, 0xfd075aa0u},
        {7, 275106944u, 0xfd075aa0u},   {8, 2548240391u, 0x33c41fd3u},
        {9, 4015111300u, 0x4267e6ccu},  {10, 1187014913u, 0x4267e6ccu},
        {11, 2653885822u, 0x75653472u}, {12, 4120756731u, 0x4267e6ccu},
        {13, 1292660344u, 0x33c41fd3u}, {14, 2759531253u, 0x75653472u},
        {15, 1106750779u, 0x9f100898u},
    };
    for (size_t i = 0; i < sizeof rooms / sizeof *rooms; ++i) {
        memset(c->cells, 0x01, sizeof c->cells);
        seed = rooms[i].seed;
        CHECK(!cok_combat_dungeon(c, &game.view, 7, 7, &seed));
        CHECK(seed == rooms[i].after && hash(c) == rooms[i].hash);
    }
    /* A west wall alone, open to the north and west: the corner opens
     * (3cb2:051e), as the original builds it. */
    reset("");
    game.view.map[0x100 + 7 * 16 + 7] = 0x01;
    memset(c->cells, 0x01, sizeof c->cells);
    seed = 1;
    CHECK(cok_combat_dungeon(c, &game.view, 7, 7, &seed) && hash(c) == 0x2c9a5542u);
    CHECK(c->cells[10][22] == 1 && c->cells[10][21] == COK_COMBAT_FLOOR);
    game.view.map[0x200 + 7 * 16 + 7] = 0x40;
    game.view.map[7 * 16 + 7] = 0x10;
    /* Without a wall it is no room: no dice, and nothing read. */
    game.view.map[7 * 16 + 7] = 0;
    game.view.map[0x100 + 7 * 16 + 7] = 0;
    seed = 4;
    CHECK(cok_combat_dungeon(c, &game.view, 7, 7, &seed) && seed == 4);
    CHECK(c->cells[12][25] == COK_COMBAT_FLOOR);
    /* Nor is a passage between two walls, or a square with a door. */
    game.view.map[7 * 16 + 7] = 0x10;
    game.view.map[0x100 + 7 * 16 + 7] = 0x10;
    seed = 4;
    CHECK(cok_combat_dungeon(c, &game.view, 7, 7, &seed) && seed == 4);
    game.view.map[0x100 + 7 * 16 + 7] = 0x01;
    game.view.map[0x300 + 7 * 16 + 7] = 0x40; /* the north wall a way through */
    seed = 4;
    CHECK(cok_combat_dungeon(c, &game.view, 7, 7, &seed) && seed == 4);
}

static void test_records(void)
{
    reset("");
    char error[300];
    cok_character *a = record('A', 0, false);
    cok_character *b = record('B', 1, false);
    cok_character *ally = record('C', 0, false);
    cok_character *keeps = record('D', 0, false);
    a->record[0x113] = 55; /* armour class 5 */
    ally->record[0xe7] = 0x80;
    keeps->record[0xe7] = 0x92;
    b->record[0xe7] = 0x80;
    /* The party is two records; the party faces east. */
    CHECK(cok_combat_records(&game.party, &game.item_types, 2, 2, 0x1c5, error, sizeof error));
    CHECK(a->combat->not_party == 0 && b->combat->not_party == 0 && ally->combat->not_party == 1);
    /* East is combat direction 2; against the party, 6. */
    CHECK(a->combat->facing == 2 && b->combat->facing == 6 && ally->combat->facing == 2);
    /* An ally past the party with no morale of its own gets var 0x7ec6 +
     * 0x80, as a byte; one with morale keeps it, as does an enemy. */
    CHECK(ally->record[0xe7] == 0x45 && keeps->record[0xe7] == 0x92 && b->record[0xe7] == 0x80);
    keeps->record[0xe7] = 0xe7;
    ally->record[0xe7] = 0xe6; /* 0x66, kept */
    CHECK(cok_combat_records(&game.party, &game.item_types, 2, 6, 1, error, sizeof error));
    CHECK(keeps->record[0xe7] == 0x81 && a->combat->facing == 6 && b->combat->facing == 2);
    CHECK(ally->record[0xe7] == 0xe6);
    /* The stats are recomputed (6346:0d20). */
    CHECK(a->record[0x18d] == 55);
    a->record[0x11] = 30; /* past the strength table */
    CHECK(!cok_combat_records(&game.party, &game.item_types, 2, 0, 0, error, sizeof error));
}

static void test_placement(void)
{
    /* As the original places them (pins.py): a party of four facing
     * north fills a rank of two and then the next, behind and to the
     * right; the monsters two squares ahead fill theirs facing south. */
    reset("");
    fixture(4, 4, 0, 2);
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 27, 13, 1) && at(2, 28, 13, 1) && at(3, 28, 14, 1) && at(4, 29, 14, 1));
    CHECK(at(5, 17, 2, 1) && at(6, 16, 2, 1) && at(7, 16, 1, 1) && at(8, 15, 1, 1));
    CHECK(game.combat.count == 9 && game.combat.combatant[9].size == 0);
    CHECK(game.combat.view_x == 24 && game.combat.view_y == 10);
    CHECK(game.combat.sides[0] == 4 && game.combat.sides[1] == 4);
    /* A party of two: the first rank is one wide, so the second is tried
     * at its centre only once the rank has ended there. */
    reset("");
    fixture(2, 2, 0, 2);
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 27, 13, 1) && at(2, 28, 14, 1) && at(3, 17, 2, 1) && at(4, 16, 1, 1));
    /* Facing east, a square with open sides skips the party's second
     * rank. */
    reset("");
    fixture(6, 6, 2, 1);
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 26, 12, 1) && at(2, 27, 13, 1) && at(3, 25, 11, 1) && at(4, 24, 12, 1));
    CHECK(at(5, 25, 13, 1) && at(6, 23, 11, 1));
    CHECK(at(7, 34, 13, 1) && at(8, 33, 12, 1) && at(9, 35, 14, 1) && at(12, 36, 14, 1));
    CHECK(game.combat.view_x == 23 && game.combat.view_y == 9);
    reset("");
    fixture(5, 2, 6, 2);
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 28, 13, 1) && at(2, 27, 12, 1) && at(5, 29, 12, 1) && at(6, 14, 12, 1));
    /* South at distance 0: both sides start from the party's square. */
    reset("");
    fixture(6, 3, 4, 0);
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 27, 12, 1) && at(6, 27, 11, 1) && at(7, 27, 13, 1) && at(9, 28, 14, 1));
    /* Thirty on the party's side fill its first square's formation and go
     * on to the next square behind, whose formation is the widest (4), as
     * the original places them. */
    reset("");
    fixture(30, 1, 0, 2);
    CHECK(cok_combat_setup(&game));
    CHECK(at(21, 27, 15, 1) && at(22, 32, 15, 1) && at(23, 26, 15, 1) && at(24, 33, 15, 1));
    CHECK(at(25, 31, 16, 1) && at(26, 32, 16, 1) && at(27, 30, 16, 1) && at(28, 33, 16, 1));
    CHECK(at(29, 29, 16, 1) && at(30, 34, 16, 1) && at(31, 17, 2, 1));
    /* 2 by 2 monsters. */
    reset("");
    fixture(2, 3, 0, 1);
    for (size_t i = 2; i < 5; ++i) game.party.members[i]->record[0xcf] = 0x84;
    CHECK(cok_combat_setup(&game));
    CHECK(at(3, 22, 7, 4) && at(4, 20, 6, 4) && at(5, 18, 6, 4));
    CHECK(game.combat.occupant[7][22] == 3 && game.combat.occupant[8][23] == 3);
    /* Outside 3D areas (mode 3) the walls of the 3D map still count, as
     * the mode is 5, never 3, when 3cb2:1379 asks: walled to the north,
     * east and south, the party facing west fills its second rank. With
     * no walls it skips it. */
    reset("");
    fixture(3, 2, 6, 2);
    game.vm.mem4b00[0xe6] = 0;
    game.vm.mode = 3;
    game.view.map[7 * 16 + 7] = 0x11;
    game.view.map[0x100 + 7 * 16 + 7] = 0x10;
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 28, 13, 1) && at(2, 29, 13, 1) && at(3, 28, 12, 1) && at(4, 14, 12, 1));
    reset("");
    fixture(3, 2, 6, 2);
    game.vm.mem4b00[0xe6] = 0;
    game.vm.mode = 3;
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 28, 13, 1) && at(2, 30, 13, 1) && at(3, 29, 12, 1) && at(4, 14, 12, 1));
}

static void test_fallen(void)
{
    /* A party member who cannot act is placed but leaves the map, a body
     * on its cell (the cell's terrain kept), and it counts for the width
     * of no rank: four records, three standing, make a rank of two. */
    reset("");
    fixture(4, 1, 0, 2);
    cok_character *down = game.party.members[1];
    down->record[0x189] = 0;
    down->record[0x188] = 6;
    CHECK(cok_combat_setup(&game));
    CHECK(at(1, 27, 13, 1) && at(2, 28, 13, 0) && at(3, 28, 14, 1) && at(4, 29, 14, 1));
    CHECK(game.combat.bodies == 1 && game.combat.body[0].character == down);
    CHECK(game.combat.body[0].x == 28 && game.combat.body[0].y == 13);
    CHECK(game.combat.body[0].cell == COK_COMBAT_FLOOR);
    CHECK(game.combat.cells[13][28] == COK_COMBAT_BODY && game.combat.occupant[13][28] == 0);
    CHECK(strstr(s.log, "combat: 2 B at 28,13, fallen, off the map;") != NULL);
    CHECK(game.combat.sides[0] == 3);
    /* A monster that cannot act leaves no body; status 9 cannot act and
     * leaves the map. */
    reset("");
    fixture(2, 2, 0, 2);
    game.party.members[2]->record[0x189] = 0;
    game.party.members[3]->record[0x188] = 9;
    CHECK(cok_combat_setup(&game));
    CHECK(game.combat.bodies == 0 && game.combat.combatant[3].size == 0);
    CHECK(game.combat.combatant[4].size == 0 && game.party.members[3]->record[0x189] == 0);
    /* A ninth body would overrun the table (DS:69ee). */
    reset("");
    fixture(9, 1, 0, 2);
    for (size_t i = 0; i < 9; ++i) game.party.members[i]->record[0x189] = 0;
    CHECK(!cok_combat_setup(&game) && game.vm.status == COK_ECL_UNDEFINED);
}

static void test_no_place(void)
{
    /* A monster with no place is removed, its icons kept for the others
     * of its slot and the party's size unchanged. Here the square ahead
     * is walled in but to the south, and the third of three 2 by 2
     * monsters finds no place, as in the original (pins.py). */
    reset("");
    fixture(2, 3, 0, 1);
    for (size_t i = 2; i < 5; ++i) {
        game.party.members[i]->record[0x137] = 8;
        game.party.members[i]->record[0xcf] = 0x84;
    }
    CHECK(cok_picture_create(&game.icons[8][0], 3, 24, 1, 0) == COK_PICTURE_OK);
    game.view.map[6 * 16 + 7] = 0x11;         /* north and east walls */
    game.view.map[0x100 + 6 * 16 + 7] = 0x01; /* west */
    cok_character *third = game.party.members[4];
    game.vm.character = game.party.members[3]->record;
    CHECK(cok_combat_setup(&game));
    CHECK(game.party.count == 4 && cok_party_index(&game.party, third->record) == 4);
    CHECK(at(3, 22, 7, 4) && at(4, 20, 6, 4) && game.combat.count == 5);
    CHECK(game.vm.mem7c00[0x33e] == 2 && game.icons[8][0].pixels != NULL);
    CHECK(strstr(s.log, "combat: E has no place and is removed;") != NULL);
    CHECK(game.combat.combatant[5].character == NULL);
    cok_picture_free(&game.icons[8][0]);
    /* A party member too big for anywhere stays, with no place: size 5's
     * cells lie far off the map. Its place is the last tried. */
    reset("");
    fixture(2, 1, 0, 2);
    game.party.members[1]->record[0xcf] = 5;
    CHECK(cok_combat_setup(&game));
    CHECK(game.party.count == 3 && at(2, 38, 15, 0) && at(3, 17, 2, 1));
    CHECK(strstr(s.log, "combat: 2 B has no place;") != NULL);
    /* Where the selection EXIT restores is a monster removed, the original
     * would go on with it freed. */
    reset("");
    fixture(1, 5, 0, 2);
    for (size_t i = 1; i < 6; ++i) game.party.members[i]->record[0xcf] = 7;
    game.vm.saved_character = game.party.members[3]->record;
    game.vm.restore_character = true;
    CHECK(!cok_combat_setup(&game) && game.vm.status == COK_ECL_UNDEFINED);
    game.vm.saved_character = NULL;
}

static void test_refusals(void)
{
    /* A record on a side other than 0 or 1 counts past DS:6b2d. */
    reset("");
    fixture(2, 1, 0, 2);
    game.party.members[2]->record[0x18a] = 2;
    CHECK(!cok_combat_setup(&game) && game.vm.status == COK_ECL_UNDEFINED);
    /* A 72nd combatant's next entry lies in the screen positions. */
    reset("");
    for (int i = 0; i < 72; ++i) record('M', 1, false);
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(!cok_combat_setup(&game) && strstr(game.error, "72nd") != NULL);
    /* A record on another side that cannot act passes 6346:268a but
     * would index past the formations. */
    reset("");
    fixture(2, 2, 0, 2);
    game.party.members[3]->record[0x18a] = 2;
    game.party.members[3]->record[0x189] = 0;
    game.party.members[3]->record[0x188] = 6;
    CHECK(!cok_combat_setup(&game) && strstr(game.error, "side 2") != NULL);
}

/* A room's tables read the cells of blocks not yet built, which hold
 * RANDCOM's record, as the heap normally leaves them: the map is the one
 * the original builds over that record in an emulator. */
static void test_room(void)
{
    reset("");
    fixture(1, 1, 0, 2);
    game.view.map[7 * 16 + 7] = 0x10;
    game.view.map[0x100 + 7 * 16 + 7] = 0x01;
    game.view.map[0x200 + 7 * 16 + 7] = 0x40;
    CHECK(cok_combat_setup(&game));
    CHECK(hash(&game.combat) == 0xd6c72e59u && game.vm.seed == 1172187917u);
    /* Empty from the start, the view is from 0, 0 (the original's entry 1
     * holds a record of an earlier battle). */
    reset("");
    game.vm.mem7c00[0x33e] = 0;
    CHECK(cok_combat_setup(&game) && game.combat.count == 1);
    CHECK(game.combat.view_x == -3 && game.combat.view_y == -3);
}

static cok_combatant entry(int8_t x, int8_t y, uint8_t size, cok_character *c)
{
    return (cok_combatant){.x = x, .y = y, .size = size, .character = c};
}

static void test_lookups(void)
{
    cok_combat *c = &game.combat;
    reset("");
    fixture(3, 0, 0, 0);
    memset(c->cells, COK_COMBAT_FLOOR, sizeof c->cells);
    c->count = 4;
    for (unsigned n = 1; n <= 3; ++n) c->combatant[n].character = game.party.members[n - 1];
    c->combatant[4].character = NULL;
    c->combatant[4].size = 0;
    const uint8_t *a = game.party.members[0]->record, *b = game.party.members[1]->record;
    /* A 2 by 2 footprint at x 49 runs into the next row (6beb:0375). */
    c->combatant[1] = entry(49, 3, 4, game.party.members[0]);
    c->combatant[2] = entry(10, 10, 3, game.party.members[1]);
    c->combatant[3] = entry(0, 24, 0, game.party.members[2]);
    c->view_x = 5;
    c->view_y = 6;
    CHECK(cok_combat_occupy(c));
    CHECK(c->occupant[3][49] == 1 && c->occupant[4][0] == 1 && c->occupant[4][49] == 1 &&
          c->occupant[5][0] == 1);
    CHECK(c->occupant[10][11] == 2);
    CHECK(c->combatant[2].screen_x == 5 && c->combatant[2].screen_y == 4);
    /* A tall one on the last row would write past the table. */
    c->combatant[3].size = 2;
    CHECK(!cok_combat_occupy(c));
    c->combatant[3].size = 0;
    CHECK(cok_combat_occupy(c));
    /* The lookups by record; the entry after the last counts as one. */
    CHECK(cok_combat_index(c, b) == 2 && cok_combat_x(c, b) == 10 && cok_combat_size(c, b) == 3);
    uint8_t outsider[COK_CHARACTER_SIZE];
    CHECK(cok_combat_index(c, outsider) == 0 && cok_combat_x(c, outsider) == 0);
    CHECK(cok_combat_size(c, outsider) == 4); /* the count */
    c->combatant[4].character = game.party.members[2];
    CHECK(cok_combat_index(c, game.party.members[2]->record) == 3);
    c->combatant[3].character = NULL;
    CHECK(cok_combat_index(c, game.party.members[2]->record) == 4);
    c->combatant[3].character = game.party.members[2];
    c->combatant[4].character = NULL;
    /* Off the map first, the probe stays 0 though later cells are on it. */
    c->combatant[3] = entry(0, 20, 4, game.party.members[2]);
    uint8_t first_who, first_terrain;
    bool first_cloud, first_puddle;
    CHECK(cok_combat_probe(c, game.party.members[2]->record, 6, &first_who, &first_terrain,
                           &first_cloud, &first_puddle));
    CHECK(first_terrain == 0);
    c->combatant[3] = entry(0, 24, 0, game.party.members[2]);
    /* Cells off the map are 0. */
    uint8_t who, terrain;
    cok_combat_cell(c, 10, 10, &who, &terrain);
    CHECK(who == 2 && terrain == COK_COMBAT_FLOOR);
    cok_combat_cell(c, 50, 10, &who, &terrain);
    CHECK(who == 0 && terrain == 0);
    cok_combat_cell(c, 10, -1, &who, &terrain);
    CHECK(who == 0 && terrain == 0);
    /* The probe: from plain floor, the costliest cell, the later on ties;
     * clouds and puddles do not count, and off the map is 0 for good. */
    bool cloud, puddle;
    c->cells[10][11] = 0x1d;
    CHECK(cok_combat_probe(c, b, 8, &who, &terrain, &cloud, &puddle));
    CHECK(who == 0 && terrain == COK_COMBAT_FLOOR && puddle && !cloud);
    c->cells[10][10] = 0x1e;
    CHECK(cok_combat_probe(c, b, 8, &who, &terrain, &cloud, &puddle));
    CHECK(terrain == COK_COMBAT_FLOOR && puddle && cloud);
    c->cells[10][10] = 0x30; /* cost 2 */
    c->cells[10][11] = 0x31;
    CHECK(cok_combat_probe(c, b, 8, &who, &terrain, &cloud, &puddle) && terrain == 0x31);
    c->cells[10][11] = 0x17;
    CHECK(cok_combat_probe(c, b, 8, &who, &terrain, &cloud, &puddle) && terrain == 0x30);
    /* Moved east onto combatant 1's cells at x 0 of rows 4-5? No: one step
     * west from 49 runs off the map, which wins over any cost. */
    CHECK(cok_combat_probe(c, a, 2, &who, &terrain, &cloud, &puddle) && terrain == 0);
    /* B moved north-west lands on itself: no occupant. */
    CHECK(cok_combat_probe(c, b, 6, &who, &terrain, &cloud, &puddle) && who == 0);
    c->combatant[3] = entry(12, 10, 1, game.party.members[2]);
    CHECK(cok_combat_occupy(c));
    CHECK(cok_combat_probe(c, b, 2, &who, &terrain, &cloud, &puddle) && who == 3);
    /* On the view: from the screen position as last worked out. */
    CHECK(cok_combat_on_view(0, 0) && cok_combat_on_view(6, 6) && !cok_combat_on_view(7, 0));
    bool visible;
    CHECK(cok_combat_visible(c, b, false, &visible) && visible);
    c->view_x = 20;
    CHECK(cok_combat_visible(c, b, false, &visible) && visible); /* not worked out again */
    c->view_x = 10;
    CHECK(cok_combat_scroll(c, 20, 10, 0xff));
    CHECK(c->view_x == 17 && c->combatant[2].screen_x == -7);
    CHECK(cok_combat_visible(c, b, false, &visible) && !visible);
    c->combatant[2].x = 22;
    CHECK(cok_combat_occupy(c) && cok_combat_visible(c, b, false, &visible) && visible);
    c->combatant[2].x = 23;
    CHECK(cok_combat_occupy(c));
    CHECK(cok_combat_visible(c, b, false, &visible) && visible);
    CHECK(cok_combat_visible(c, b, true, &visible) && !visible);
    c->combatant[2].size = 0;
    CHECK(cok_combat_visible(c, b, false, &visible) && !visible);
    c->combatant[2].size = 3;
    /* Scrolling: within the margin of the centre nothing moves; past it
     * the centre moves onto the target while within 3-0x2e and 3-0x15. */
    c->view_x = 10;
    c->view_y = 10;
    CHECK(!cok_combat_scroll(c, 15, 13, 2) && c->view_x == 10);
    CHECK(cok_combat_scroll(c, 16, 13, 2) && c->view_x == 13 && c->view_y == 10);
    CHECK(cok_combat_scroll(c, 60, -9, 0) && c->view_x == 0x2e - 3 && c->view_y == 0);
    /* A centre already outside those stays where the target is it. */
    c->view_x = -3;
    CHECK(cok_combat_scroll(c, 0, 3, 0xff) && c->view_x == -3);
}

static void test_place(void)
{
    cok_combat *c = &game.combat;
    reset("");
    fixture(2, 0, 0, 0);
    char error[300];
    CHECK(cok_combat_records(&game.party, &game.item_types, 2, 0, 0, error, sizeof error));
    memset(c->cells, COK_COMBAT_FLOOR, sizeof c->cells);
    memset(c->occupant, 0, sizeof c->occupant);
    cok_character *a = game.party.members[0], *b = game.party.members[1];
    c->count = 3;
    c->combatant[1] = (cok_combatant){.x = 5, .y = 5, .size = 1, .character = a};
    c->combatant[2] = (cok_combatant){.x = 8, .y = 8, .size = 0, .character = b};
    c->combatant[3] = (cok_combatant){0};
    c->bodies = 1;
    c->body[0] = (cok_combat_body){.character = b, .x = 8, .y = 8, .cell = 0x30};
    c->cells[8][8] = COK_COMBAT_BODY;
    CHECK(cok_combat_occupy(c));
    bool placed;
    /* Outside combat it is placed, and nothing changes. */
    CHECK(cok_combat_place(c, 4, b, 5, 5, false, &placed) && placed && c->combatant[2].size == 0);
    /* Onto another: not placed, its size 0, its place taken all the same. */
    CHECK(cok_combat_place(c, 5, b, 5, 5, false, &placed) && !placed);
    CHECK(c->combatant[2].size == 0 && c->combatant[2].x == 5);
    /* Raised where it fell: the body is taken back and its cell gets the
     * terrain the body covered; the count of bodies stays. */
    CHECK(cok_combat_place(c, 5, b, 8, 8, true, &placed) && placed);
    CHECK(c->cells[8][8] == 0x30 && c->body[0].character == NULL && c->bodies == 1);
    CHECK(c->occupant[8][8] == 2 && c->combatant[2].size == 1);
    /* Elsewhere with no body of its own, the cell gets the terrain the
     * probe found, the costliest under it. */
    c->cells[3][3] = 0x31;
    CHECK(cok_combat_place(c, 5, b, 3, 3, true, &placed) && placed && c->cells[3][3] == 0x31);
    b->record[0xcf] = 3;
    c->cells[3][4] = 0x32;
    CHECK(cok_combat_place(c, 5, b, 3, 3, true, &placed) && placed && c->cells[3][3] == 0x32);
    /* A body that lay on another keeps the terrain the probe found. */
    c->bodies = 1;
    c->body[0] = (cok_combat_body){.character = b, .x = 9, .y = 9, .cell = COK_COMBAT_BODY};
    c->cells[9][9] = 0x30;
    b->record[0xcf] = 1;
    CHECK(cok_combat_place(c, 5, b, 9, 9, true, &placed) && placed && c->cells[9][9] == 0x30);
    b->record[0xcf] = 3;
    /* Another's body there keeps the body. */
    c->body[0] = (cok_combat_body){.character = a, .x = 20, .y = 20, .cell = 0x17};
    c->cells[20][20] = COK_COMBAT_BODY;
    CHECK(cok_combat_place(c, 5, b, 20, 20, true, &placed) && placed);
    CHECK(c->cells[20][20] == COK_COMBAT_BODY);
    /* A monster (+0x13) takes no body back. */
    b->combat->not_party = 1;
    c->cells[30 / 2][30] = 0x31;
    CHECK(cok_combat_place(c, 5, b, 30, 15, true, &placed) && placed && c->cells[15][30] == 0x31);
    b->record[0xcf] = 1;
    /* Walls: not placed. */
    c->cells[12][12] = 2;
    CHECK(cok_combat_place(c, 5, b, 12, 12, false, &placed) && !placed);
    /* A record that is not a combatant would write over the count. */
    uint8_t count = c->count;
    cok_character outsider = {0};
    CHECK(!cok_combat_place(c, 5, &outsider, 1, 1, false, &placed) && c->count == count);
}

static void test_sides(void)
{
    reset("");
    cok_combat *c = &game.combat;
    record('A', 0, false);
    record('B', 0, true);
    cok_character *m = record('M', 1, false);
    cok_character *n = record('N', 1, true);
    CHECK(cok_combat_count_sides(c, &game.party) && c->sides[0] == 1 && c->sides[1] == 1);
    cok_character *third = record('T', 2, false);
    CHECK(!cok_combat_count_sides(c, &game.party));
    third->record[0x189] = 0;
    CHECK(cok_combat_count_sides(c, &game.party));
    /* The enemies' health: 20 times their hit points (those that can
     * act) as a word, over all their most, times 5. */
    m->record[0x197] = 10;
    m->record[0x62] = 40;
    n->record[0x197] = 30;
    n->record[0x62] = 40;
    c->enemy_health = 7;
    cok_combat_enemy_health(c, &game.party);
    CHECK(c->enemy_health == 10);
    /* The word wraps above 3,276 hit points: 3,510 of 3,580 give 5, not
     * 95. */
    for (int i = 0; i < 14; ++i) {
        cok_character *e = record('E', 1, false);
        e->record[0x197] = e->record[0x62] = 250;
    }
    cok_combat_enemy_health(c, &game.party);
    CHECK(c->enemy_health == 5);
    /* With no enemies the last value stays. */
    reset("");
    record('A', 0, false);
    c->enemy_health = 7;
    cok_combat_enemy_health(c, &game.party);
    CHECK(c->enemy_health == 7);
}

static void test_setup(void)
{
    reset("abcdefghij");
    fixture(2, 2, 0, 2);
    s.pending = 100; /* keys waiting: setup drops one at each of its 3 + 4 points */
    game.vm.file = 1;
    cok_adventure_load_big(&game, 0x79);
    CHECK(game.big.pixels != NULL);
    cok_character *aurak = game.party.members[3];
    CHECK(cok_character_add_effect(aurak, 0x38, 0, 0, false) != NULL);
    game.pool.missile = 1;
    game.vm.mem7c00[0x333] = 9;
    game.combat.enemy_health = 0;
    game.effects.rolls.round = 3;
    CHECK(cok_combat_setup(&game));
    CHECK(s.at == 7 && game.big.pixels == NULL);
    CHECK(game.vm.mode == 5 && !game.moving && game.combat.active);
    CHECK(game.pool.missile == 0 && game.vm.mem7c00[0x333] == 0 && game.effects.rolls.round == 0);
    CHECK(game.combat.round_limit == 15 && game.combat.enemy_health == 100);
    CHECK(game.big_id == COK_ADVENTURE_NO_PICTURE);
    /* Event 8: effect 0x38 adds 0x19 for a minute (3f44:17a3). */
    CHECK(cok_character_find_effect(aurak, 0x19) != NULL);
    static const char begins[] = "print: A battle begins...;combat: the 3D map around 7,7 "
                                 "facing N, the enemy 2 squares ahead;combat: 1 A at 27,13;";
    CHECK(strncmp(s.log, begins, sizeof begins - 1) == 0);
    CHECK(strstr(s.log, "combat: the view from 24,10;unported: the combat screen (6346:300f);") !=
          NULL);
    CHECK(game.combat.tiles.pixels != NULL && game.combat.tiles.frames == COK_COMBAT_TILES);
    /* The tiles: DUNGCOM's 25 from frame 0 and RANDCOM's 6 from 0x22;
     * frames 0x19-0x20 keep WILDCOM's from the battle on open ground
     * before, and 0x21 is never loaded. */
    const cok_picture *t = &game.combat.tiles;
    cok_picture wild = {0}, dung = {0}, rand = {0};
    CHECK(cok_adventure_load_image(&game, "WILDCOM", 1, -1, &wild));
    CHECK(cok_adventure_load_image(&game, "DUNGCOM", 1, -1, &dung));
    CHECK(cok_adventure_load_image(&game, "RANDCOM", 1, -1, &rand));
    size_t frame = t->frame_size;
    CHECK(frame == wild.frame_size && wild.frames == 35 && dung.frames == 25 && rand.frames == 6);
    CHECK(memcmp(t->pixels, dung.pixels, 25 * frame) == 0);
    CHECK(memcmp(t->pixels + 0x19 * frame, wild.pixels + 0x19 * frame, 8 * frame) == 0);
    CHECK(memcmp(t->pixels + 0x22 * frame, rand.pixels, 6 * frame) == 0);
    for (size_t k = 0; k < frame; ++k) CHECK(t->pixels[0x21 * frame + k] == 0);
    cok_picture_free(&wild);
    cok_picture_free(&dung);
    cok_picture_free(&rand);
    /* A battle on open ground. */
    reset("");
    fixture(1, 1, 0, 2);
    game.vm.mem4b00[0xe6] = 0;
    CHECK(cok_combat_setup(&game));
    CHECK(strstr(s.log, "combat: open ground facing N, the enemy 2 squares ahead;") != NULL);
    /* The one record removed, none left: the original's lookup of the
     * first, NULL, finds entry 1, which the removal cleared, at the last
     * cell tried for it (23, 0, as in an emulator). */
    reset("");
    fixture(0, 1, 0, 2);
    game.party.members[0]->record[0xcf] = 6;
    CHECK(cok_combat_setup(&game) && game.party.count == 0);
    CHECK(game.combat.view_x == 20 && game.combat.view_y == -3 && game.combat.count == 1);
    /* Two removed first: the original's walk goes back from the first,
     * freed, to the second for good. One kept before them, it does not. */
    reset("");
    fixture(0, 2, 0, 2);
    game.party.members[0]->record[0xcf] = game.party.members[1]->record[0xcf] = 6;
    CHECK(!cok_combat_setup(&game) && strstr(game.error, "for good") != NULL);
    reset("");
    fixture(0, 3, 0, 2);
    game.party.members[1]->record[0xcf] = game.party.members[2]->record[0xcf] = 6;
    CHECK(cok_combat_setup(&game) && game.party.count == 1);
    /* A facing past the tables. */
    reset("");
    fixture(1, 1, 0, 2);
    game.vm.direction = 9;
    CHECK(!cok_combat_setup(&game));
}

/* A red dragon's fear (effect 0x52, 3f44:303c) at setup's event 8: the
 * party's low levels are terrified (0x6f) and turned, higher ones afraid
 * (0x77) unless they save, those with 0x5c not at all; the battle's end
 * (3995:004b, 60f4:1440) takes the terror away again. */
static void test_fear(void)
{
    reset("");
    fixture(6, 1, 0, 2);
    cok_character *npc = game.party.members[4], *resists = game.party.members[5];
    npc->record[0xe7] = 0x85;
    resists->record[0x187] = 100; /* magic resistance: unaffected */
    game.effects.rolls.amount = 0;
    game.effects.rolls.spell = 1;
    cok_character *low = game.party.members[0], *calm = game.party.members[1];
    cok_character *high = game.party.members[2], *saves = game.party.members[3];
    cok_character *dragon = game.party.members[6];
    calm->record[0xd6] = 1;
    CHECK(cok_character_add_effect(calm, 0x5c, 0, 0xff, false) != NULL);
    low->record[0xd6] = 3;
    high->record[0xd6] = 4;
    high->record[0xd4] = 30; /* cannot make throw 4 */
    saves->record[0xd6] = 9;
    saves->record[0xd4] = 0;  /* always makes it, but on a 1 */
    CHECK(cok_character_add_effect(dragon, 0x52, 0, 0xff, false) != NULL);
    CHECK(cok_combat_setup(&game));
    const cok_effect *e = cok_character_find_effect(low, 0x6f);
    CHECK(e != NULL && e->duration == 0 && e->value == 0xff && e->on_remove);
    CHECK(low->record[0x18b] == 1 && low->record[0xe7] == 0xb3 && low->combat->forced == 1);
    CHECK(cok_character_find_effect(calm, 0x6f) == NULL && calm->record[0x18b] == 0);
    CHECK(cok_character_find_effect(high, 0x77) != NULL && high->record[0x18b] == 0);
    CHECK(cok_character_find_effect(saves, 0x77) == NULL);
    CHECK(cok_character_find_effect(dragon, 0x6f) == NULL);
    /* An NPC keeps its morale; magic resistance leaves one unaffected. */
    CHECK(cok_character_find_effect(npc, 0x6f) != NULL && npc->record[0xe7] == 0x85);
    CHECK(cok_character_find_effect(resists, 0x6f) == NULL && resists->record[0x18b] == 0);
    CHECK(strstr(s.log, "print: F;print: is Unaffected;") != NULL);
    CHECK(strstr(s.log, "print: A;print: is terrified;") != NULL);
    CHECK(strstr(s.log, "print: C;print: is afraid;") != NULL);
    CHECK(cok_combat_end(&game));
    CHECK(cok_character_find_effect(low, 0x6f) == NULL);
    CHECK(cok_character_find_effect(high, 0x77) == NULL);
    CHECK(low->record[0x18b] == 0 && low->record[0xe7] == 0 && low->combat->forced == 0);
    CHECK(!game.combat.active);
    /* Charmed and okay, a record runs when more than one enemy could act. */
    reset("");
    fixture(1, 2, 0, 2);
    CHECK(cok_character_add_effect(game.party.members[0], 0x0b, 0, 0, false) != NULL);
    CHECK(cok_combat_setup(&game) && cok_combat_end(&game));
    CHECK(game.party.members[0]->record[0x188] == 3);
    CHECK(cok_character_find_effect(game.party.members[0], 0x0b) == NULL);
}

static const uint8_t combat_code[] = {COK_ECL_LOAD_MONSTER, 0, 9, 0, 2, 0, 9, COK_ECL_COMBAT};

static void test_combat(void)
{
    /* COMBAT sets the battle up, shows it to the hook, and the stub then
     * ends it; the map goes. */
    reset("\rE");
    fixture(2, 0, 0, 2);
    s.battles = 0;
    memset(game.vm.code, 0, sizeof game.vm.code);
    memcpy(game.vm.code, combat_code, sizeof combat_code);
    game.vm.size = sizeof combat_code + 1;
    game.vm.depth = 0;
    CHECK(cok_ecl_run(&game.vm, COK_ECL_BASE) == COK_ECL_OK);
    CHECK(s.battles == 1 && !game.combat.active);
    CHECK(strstr(s.log, "combat: 3 GOBLIN at 17,2;combat: 4 GOBLIN at 16,1;") != NULL);
    CHECK(strstr(s.log, "[COMBAT]") == NULL && strstr(s.log, "combat: removed 2 GOBLIN") != NULL);
}

static void unported(cok_adventure *g, void *context)
{
    (void)g;
    (void)context;
}

int main(void)
{
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .unported = unported, .key_pending = pending,
                                 .battlefield = battlefield, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    test_tables();
    test_footprints();
    test_wilderness();
    test_dungeon();
    test_records();
    test_placement();
    test_fallen();
    test_no_place();
    test_refusals();
    test_room();
    test_lookups();
    test_place();
    test_sides();
    test_setup();
    test_fear();
    test_combat();
    cok_adventure_close(&game);
    puts("combat tests passed");
    return 0;
}
