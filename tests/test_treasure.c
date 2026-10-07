#include "treasure.h"

#include "camp.h"
#include "items.h"
#include "monster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[16384];
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static void unported(cok_adventure *game, void *context)
{
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "[%s];", cok_ecl_opcode_name(game->vm.opcode));
}

static void say(cok_effects *fx, cok_character *c, const char *text, void *context)
{
    (void)fx;
    (void)c;
    log_line(NULL, "say", text, context);
}

static script s;
static cok_adventure game;

/* The missile the battle recovers (DS:60a2), as 1 + its index in the
 * pool, which combat setup clears (3cb2:1c58); 0 for none. */
static size_t battle_missile;

static void battlefield(cok_adventure *g, void *context)
{
    (void)context;
    g->pool.missile = battle_missile;
}

static void open_game(void)
{
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .unported = unported,
                                 .battlefield = battlefield, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    game.effects.say = say;
    game.effects.context = &s;
}

static void reset(const char *k)
{
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    game.vm.character = game.vm.saved_character = NULL;
    game.vm.restore_character = false;
    game.combat_stub = COK_COMBAT_UNPORTED;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    game.vm.mode = 4;
    game.vm.mem4b00[0xe6] = 1;
    game.vm.file = 1;
    game.monsters = game.undead = 0;
    game.monsters_loaded = false;
    game.icon_slot = 8;
    game.quit = game.party_killed = false;
    game.experience = 0;
    game.experience_known = false;
    game.selected = 1;
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.log[0] = '\0';
    game.input_ended = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
}

static cok_ecl_status run(const uint8_t *code, size_t length)
{
    memset(game.vm.code, 0, sizeof game.vm.code);
    memcpy(game.vm.code, code, length);
    game.vm.size = length + 1;
    game.vm.depth = 0;
    return cok_ecl_run(&game.vm, COK_ECL_BASE);
}

#define RUN(...) run((const uint8_t[]){__VA_ARGS__}, sizeof (const uint8_t[]){__VA_ARGS__})
#define LOGGED(text) (strstr(s.log, text) != NULL)

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void put_word(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint32_t experience_of(const uint8_t *r)
{
    return (uint32_t)r[0x116] | (uint32_t)r[0x117] << 8 | (uint32_t)r[0x118] << 16 |
           (uint32_t)r[0x119] << 24;
}

/* A character of class 7 (a knight, no bonus) with strength 18 that can
 * act, added to the party. */
static cok_character *member(const char *name)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = (uint8_t)strlen(name);
    memcpy(r + 1, name, strlen(name));
    r[0x189] = 1;
    r[0x5b] = 7;
    for (int i = 0; i < 6; ++i) r[0x10 + 2 * i] = r[0x11 + 2 * i] = 10;
    r[0x10] = r[0x11] = 18;
    CHECK(cok_party_add(&game.party, c));
    ++game.vm.mem7c00[0x33e];
    return c;
}

/* A monster against the party: +0x130 + +0x132 * +0x11b experience. */
static cok_character *monster(const char *name, uint16_t base, uint8_t per, uint8_t hp)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = (uint8_t)strlen(name);
    memcpy(r + 1, name, strlen(name));
    r[0x189] = 1;
    r[0x18a] = 1;
    r[0xe7] = 0x80;
    put_word(r + 0x130, base);
    r[0x132] = per;
    r[0x11b] = hp;
    r[0x137] = 8;
    CHECK(cok_party_append(&game.party, c));
    game.monsters_loaded = true;
    return c;
}

static uint8_t *item(cok_character *c, uint8_t type, int8_t bonus, uint16_t weight)
{
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[0x2e] = type;
    it[0x31] = type;
    it[0x32] = (uint8_t)bonus;
    put_word(it + 0x37, weight);
    CHECK(cok_character_insert_item(c, c->item_count, it));
    return c->items[c->item_count - 1];
}

/* A seed whose next d<sides> is roll. */
static uint32_t seed_for(uint8_t sides, uint8_t roll)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        if (cok_dice(&seed, 1, sides) == roll) return start;
    }
}

static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("treasure: build/START_FULL.EXE not built; templates not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    const cok_ds_table *t = &cok_treasure_templates;
    for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    /* The scrolls' spells (DS:423b-42ac), every byte the dice reach. */
    for (uint16_t at = 0x423b; at <= 0x42ac; ++at) {
        uint8_t byte;
        CHECK(cok_ds_byte(at, &byte) && byte == exe[ds + at]);
    }
    /* The coins' names. */
    static const char *const coins[7] = {"Silver", "Copper", "Bronze", "Platinum", "Steel", "Gems",
                                         "Jewelry"};
    char name[256];
    for (int k = 0; k < 7; ++k)
        CHECK(cok_ds_string((uint16_t)(0x12e3 + 11 * k), name) && strcmp(name, coins[k]) == 0);
}

static void test_generator(void)
{
    uint8_t it[COK_ITEM_SIZE];
    /* A long sword +1 on a d10 below 10, +2 on 10; the value 1000 a plus. */
    uint32_t seed = seed_for(10, 10);
    cok_treasure_item(&seed, 0x12, it);
    CHECK(it[0x2e] == 0x12 && it[0x32] == 2 && it[0x31] == 0x12 && it[0x30] == 0x70);
    CHECK(it[0x35] == 6 && word(it + 0x3a) == 2000 && word(it + 0x37) == 60 && it[0x39] == 0);
    seed = seed_for(10, 9);
    cok_treasure_item(&seed, 0x12, it);
    CHECK(it[0x32] == 1 && it[0x30] == 0x6f && word(it + 0x3a) == 1000);
    /* Bracers are AC 6 whatever the roll: a bonus of 4, worth 6000. */
    cok_treasure_item(&seed, 0x32, it);
    CHECK(it[0x32] == 4 && word(it + 0x3a) == 6000 && it[0x31] == 0x32 && it[0x30] == 0x5a &&
          it[0x2f] == 0x6b && word(it + 0x37) == 10);
    /* Armour shows only its plus hidden; leather is "Armor", the rest
     * "Mail". */
    cok_treasure_item(&seed, 0x1f, it);
    CHECK(it[0x30] == 0x30 && it[0x35] == 4 && word(it + 0x37) == 150);
    cok_treasure_item(&seed, 0x24, it);
    CHECK(it[0x30] == 0x2f && it[0x31] == 0x24 && word(it + 0x37) == 450 &&
          word(it + 0x3a) == 2500 * it[0x32]);
    /* Arrows, quarrels and darts come twenty to the item. */
    cok_treasure_item(&seed, 0x1e, it);
    CHECK(it[0x39] == 20 && word(it + 0x37) == 4 && word(it + 0x3a) == 75 * it[0x32]);
    cok_treasure_item(&seed, 5, it);
    CHECK(it[0x39] == 20 && word(it + 0x37) == 5);
    cok_treasure_item(&seed, 0x3b, it);
    CHECK(it[0x31] == 0x2e && it[0x30] == 0x31 && word(it + 0x37) == 1);
    /* A javelin is of lightning on a d5 of 5, after its bonus: the
     * template's parts, weight and value, its type kept, every part
     * but +0x31 hidden. */
    for (uint32_t start = 1;; ++start) {
        seed = start;
        cok_dice(&seed, 1, 10);
        if (cok_dice(&seed, 1, 5) != 5) continue;
        seed = start;
        break;
    }
    cok_treasure_item(&seed, 7, it);
    CHECK(it[0x2e] == 7 && it[0x2f] == 0x58 && it[0x30] == 0x5a && it[0x31] == 0x07);
    CHECK(word(it + 0x37) == 20 && word(it + 0x3a) == 1500 && it[0x3c] == 1 && it[0x3d] == 0x33);
    CHECK(it[0x32] == 1 && it[0x33] == 1 && it[0x39] == 0 && it[0x35] == 6);
    /* A healing potion on a d8 up to 5, else extra healing: type 0x30
     * kept, not the shipped potions' 0x2f. */
    seed = seed_for(8, 6);
    cok_treasure_item(&seed, 0x30, it);
    CHECK(it[0x2e] == 0x30 && it[0x3c] == 3 && it[0x3d] == 0x63 && word(it + 0x3a) == 400);
    seed = seed_for(8, 5);
    cok_treasure_item(&seed, 0x30, it);
    CHECK(it[0x3c] == 1 && it[0x3d] == 3 && word(it + 0x3a) == 200);
    cok_treasure_item(&seed, 0x34, it);
    CHECK(it[0x3c] == 0x1e && it[0x3d] == 0x41 && word(it + 0x3a) == 5500);
    cok_treasure_item(&seed, 0x35, it);
    CHECK(it[0x3d] == 0x3b && word(it + 0x3a) == 550);
    /* Scrolls: d3 spells, each a d4 level worth 150 a level; a mage's
     * order by a d2, Red (bit 0x10) on 1. "3 Spell" lacks its s. */
    for (int k = 0; k < 40; ++k) {
        cok_treasure_item(&seed, (uint8_t)(k & 1 ? 0x27 : 0x28), it);
        unsigned spells = (unsigned)(it[0x2f] - 0x63);
        CHECK(spells >= 1 && spells <= 3 && it[0x32] == 1 && word(it + 0x37) == 25);
        CHECK(word(it + 0x3a) % 150 == 0 && word(it + 0x3a) >= 150 * spells &&
              word(it + 0x3a) <= 600 * spells);
        for (unsigned i = 1; i <= 3; ++i) CHECK((it[0x3b + i] != 0) == (i <= spells));
        if (k & 1)
            CHECK((it[0x31] == 0x78 && it[0x35] == 0x14) || (it[0x31] == 0x77 && it[0x35] == 0x24));
        else
            CHECK(it[0x31] == 0x26 && it[0x35] == 0);
    }
    /* Seed 34: a White scroll of 0x0e and 0x0a (level 1) and 0x36 (level
     * 3), worth 150 + 150 + 450. */
    seed = 34;
    cok_treasure_item(&seed, 0x27, it);
    CHECK(it[0x31] == 0x77 && it[0x35] == 0x24 && it[0x3c] == 0x0e && it[0x3d] == 0x0a &&
          it[0x3e] == 0x36 && word(it + 0x3a) == 750);
    seed = seed_for(3, 3);
    cok_treasure_item(&seed, 0x28, it);
    char name[256];
    CHECK(it[0x2f] == 0x66 && cok_ds_string(0x1390 + 21 * 0x66, name) &&
          strcmp(name, "3 Spell") == 0);
    /* Other types are blank but for the type. */
    seed = 7;
    cok_treasure_item(&seed, 0x40, it);
    CHECK(seed == 7 && it[0x2e] == 0x40 && it[0x35] == 6 && it[0x32] == 0);
}

static void test_treasure_opcode(void)
{
    /* TREASURE 1 2 3 4 500 6 7 34: the coins replace the pool's; ITEM1
     * record 34's two items go first, in the reverse order. */
    reset("");
    game.pool.coins[0] = 99;
    uint8_t old[COK_ITEM_SIZE] = {0};
    CHECK(cok_pool_insert(&game.pool, 0, old));
    CHECK(RUN(COK_ECL_TREASURE, 0, 1, 0, 2, 0, 3, 0, 4, 2, 0xf4, 1, 0, 6, 0, 7, 0, 34) ==
          COK_ECL_OK);
    CHECK(game.pool.coins[0] == 1 && game.pool.coins[4] == 500 && game.pool.coins[6] == 7);
    CHECK(game.pool.item_count == 3);
    CHECK(game.pool.items[0][0x2e] == 0x1e && game.pool.items[1][0x2e] == 0x2f);
    CHECK(LOGGED("treasure: 1 Silver, 2 Copper, 3 Bronze, 4 Platinum, 500 Steel, 6 Gems, 7 "
                 "Jewelry;treasure: item 10 Arrow;treasure: item Potion;"));
    /* Loaded items keep their names: the old blank one is not named. */
    CHECK(game.pool.items[2][0] == 0);
    /* 0x80 adds none and 0xff none; the coins are still replaced. */
    CHECK(RUN(COK_ECL_TREASURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0x80) == COK_ECL_OK);
    CHECK(game.pool.item_count == 3 && game.pool.coins[0] == 0 && game.pool.coins[4] == 9);
    CHECK(RUN(COK_ECL_TREASURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff) == COK_ECL_OK);
    CHECK(game.pool.item_count == 3 && game.pool.coins[4] == 0);
    /* 0x82, two random items: then every item in the pool is named, the
     * old ones too. */
    game.pool.items[2][0x31] = 0x12; /* "Long Sword" */
    game.pool.items[2][0x35] = 0;
    uint32_t seed = game.vm.seed = 1234;
    uint8_t first[COK_ITEM_SIZE], second[COK_ITEM_SIZE];
    (void)first;
    CHECK(RUN(COK_ECL_TREASURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x82) == COK_ECL_OK);
    CHECK(game.pool.item_count == 5 && game.pool.items[4][0] == 11 &&
          memcmp(game.pool.items[4] + 1, "Long Sword ", 11) == 0);
    CHECK(game.vm.seed != seed);
    /* The second item made is first. */
    memcpy(second, game.pool.items[0], sizeof second);
    CHECK(second[0] > 0);
    /* TREASURE's own dice pick the types: 126 items from seed 99, in the
     * order made (the last first in the pool), as the original makes
     * them. */
    reset("");
    game.vm.seed = 99;
    CHECK(RUN(COK_ECL_TREASURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfe) == COK_ECL_OK);
    static const char types[] =
        "233427272727272724251f2711272727232727272714272727220827252708282524252713271e272713"
        "271104322725271227142507202712253b27080f272535272727270f2427202828342734253b28273b27"
        "2727122704272727122512241f27273025272527271e2712273247282727272727302727281112272746";
    CHECK(game.pool.item_count == 126 && game.vm.seed == 0x154b49ff);
    for (size_t i = 0; i < 126; ++i) {
        char hex[3];
        snprintf(hex, sizeof hex, "%02x", game.pool.items[125 - i][0x2e]);
        CHECK(memcmp(types + 2 * i, hex, 2) == 0);
    }
    /* A record that is not there: "Unable to find item file", and the
     * game quits to DOS. */
    reset("\r");
    CHECK(RUN(COK_ECL_TREASURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 99) == COK_ECL_OK);
    CHECK(game.quit && LOGGED("print: Unable to find item file;quit: to DOS;"));
}

/* The seed whose first d100 is first and, unless 0, whose second is
 * second and third d4 is third. */
static uint32_t seed_for_tree(uint8_t first, uint8_t second, uint8_t third)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        if (cok_dice(&seed, 1, 100) != first) continue;
        if (second != 0 && cok_dice(&seed, 1, 100) != second) continue;
        if (third != 0 && cok_dice(&seed, 1, 4) != third) continue;
        return start;
    }
}

/* The type of the item TREASURE 0x81 makes from seed. */
static uint8_t tree_type(uint32_t seed)
{
    reset("");
    game.vm.seed = seed;
    CHECK(RUN(COK_ECL_TREASURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x81) == COK_ECL_OK);
    CHECK(game.pool.item_count == 1);
    return game.pool.items[0][0x2e];
}

static bool one_of(uint8_t type, const char *types)
{
    return type != 0 && strchr(types, type) != NULL;
}

/* The edges of TREASURE's tree of dice. */
static void test_tree(void)
{
    static const struct { uint8_t roll, type; } plain[] = {
        {21, 0x27}, {65, 0x27}, {66, 0x28}, {70, 0x28},
        {71, 0x25}, {80, 0x25}, {81, 0x3b}, {85, 0x3b},
    };
    for (size_t i = 0; i < sizeof plain / sizeof *plain; ++i)
        CHECK(tree_type(seed_for_tree(plain[i].roll, 0, 0)) == plain[i].type);
    static const char armour[] = "\x1f\x20\x21\x22\x23\x24", misc[] = "\x32\x30\x34\x35";
    static const char blunt[] = "\x06\x08\x46\x0f\x1d";
    static const char missile[] = "\x05\x07\x18\x16\x19\x17\x1c\x1d\x1e";
    static const char sword[] = "\x12\x11\x0d\x10\x13\x14", other[] = "\x01\x04\x08\x0a\x0e\x47";
    CHECK(one_of(tree_type(seed_for_tree(86, 0, 0)), armour));
    CHECK(one_of(tree_type(seed_for_tree(95, 0, 0)), armour));
    CHECK(one_of(tree_type(seed_for_tree(96, 0, 0)), misc));
    CHECK(one_of(tree_type(seed_for_tree(20, 10, 0)), blunt));
    CHECK(one_of(tree_type(seed_for_tree(1, 11, 0)), missile));
    CHECK(one_of(tree_type(seed_for_tree(20, 25, 0)), missile));
    CHECK(one_of(tree_type(seed_for_tree(20, 26, 3)), sword));
    CHECK(one_of(tree_type(seed_for_tree(20, 100, 4)), other));
}

/* The pool's money (58e7). */
static void test_pool(void)
{
    reset("");
    cok_character *a = member("A"), *b = member("B"), *npc = member("C"), *turned = member("D");
    npc->record[0xe7] = 0x80;
    turned->record[0xe7] = 0xb3;
    put_word(a->record + 0xeb + 8, 100);
    put_word(a->record + 0x17d, 150);
    put_word(npc->record + 0xeb + 8, 50);
    put_word(turned->record + 0xeb + 2, 7);
    /* Pool: player characters' money, turned ones' too; NPCs keep theirs. */
    cok_pool_gather(&game);
    CHECK(game.pool.coins[4] == 100 && game.pool.coins[1] == 7 && word(a->record + 0xeb + 8) == 0);
    CHECK(word(a->record + 0x17d) == 50 && word(npc->record + 0xeb + 8) == 50);
    /* Share: three counted (A, B and the turned D), so 33 each and one
     * over, steel; the turned character's share is lost, the NPC gets
     * none. The coin over goes to the first. Copper 7: 2 each, 1 over. */
    CHECK(cok_pool_share(&game));
    CHECK(word(a->record + 0xeb + 8) == 34 && word(b->record + 0xeb + 8) == 33);
    CHECK(word(turned->record + 0xeb + 8) == 0 && word(npc->record + 0xeb + 8) == 50);
    CHECK(word(a->record + 0xeb + 2) == 3 && word(b->record + 0xeb + 2) == 2);
    CHECK(game.pool.coins[4] == 0 && game.pool.coins[1] == 0);
    CHECK(word(a->record + 0x17d) == 50 + 34 + 3);
    /* A remainder of two goes a coin each to the first two. */
    reset("");
    a = member("A");
    b = member("B");
    cok_character *c = member("C");
    game.pool.coins[4] = 101;
    CHECK(cok_pool_share(&game));
    CHECK(word(a->record + 0xeb + 8) == 34 && word(b->record + 0xeb + 8) == 34 &&
          word(c->record + 0xeb + 8) == 33);
    /* A record below 0x80 that is not 0 takes a share without being
     * counted: money is made. */
    reset("");
    a = member("A");
    b = member("B");
    b->record[0xe7] = 0x10;
    game.pool.coins[6] = 10;
    CHECK(cok_pool_share(&game));
    CHECK(word(a->record + 0xeb + 12) == 10 && word(b->record + 0xeb + 12) == 10);
    /* What one cannot carry goes back to the rest, then to anyone with
     * room, NPCs too; one past its allowance wraps and takes it all. */
    reset("");
    a = member("A");
    npc = member("C");
    npc->record[0xe7] = 0x80;
    int16_t allowance;
    char error[300];
    CHECK(cok_character_allowance(a->record, &allowance, error, sizeof error));
    uint16_t cap = (uint16_t)(allowance + 1500);
    put_word(a->record + 0x17d, (uint16_t)(cap - 10));
    game.pool.coins[4] = 25;
    CHECK(cok_pool_share(&game));
    CHECK(word(a->record + 0xeb + 8) == 10 && word(npc->record + 0xeb + 8) == 15);
    CHECK(game.pool.coins[4] == 0);
    put_word(a->record + 0x17d, (uint16_t)(cap + 1));
    put_word(npc->record + 0x17d, (uint16_t)(cap + 1));
    game.pool.coins[4] = 25;
    CHECK(cok_pool_share(&game));
    CHECK(word(a->record + 0xeb + 8) == 10 + 25 && game.pool.coins[4] == 0);
    /* With money and no one to count, Share divides by zero. */
    reset("");
    member("C")->record[0xe7] = 0x80;
    game.pool.coins[0] = 1;
    CHECK(!cok_pool_share(&game) && game.vm.status == COK_ECL_DIVIDE_BY_ZERO);
    /* No money, no division. */
    reset("");
    member("C")->record[0xe7] = 0x80;
    CHECK(cok_pool_share(&game));
    /* Take: overloaded by the amount asked, nothing; else at most the
     * pool's. */
    reset("");
    a = member("A");
    put_word(a->record + 0x17d, (uint16_t)(cap - 10));
    game.pool.coins[3] = 5;
    CHECK(cok_pool_take(&game, a->record, 11, 3));
    CHECK(game.pool.coins[3] == 5 && LOGGED("print: Overloaded;"));
    CHECK(cok_pool_take(&game, a->record, 10, 3));
    CHECK(game.pool.coins[3] == 0 && word(a->record + 0xeb + 6) == 5);
    CHECK(word(a->record + 0x17d) == cap - 5);
    /* Drop into the pool in treasure and shops only. */
    game.vm.mode = 6;
    cok_pool_drop(&game, a->record, 2, 3);
    CHECK(game.pool.coins[3] == 2 && word(a->record + 0xeb + 6) == 3);
    game.vm.mode = 4;
    cok_pool_drop(&game, a->record, 2, 3);
    CHECK(game.pool.coins[3] == 2 && word(a->record + 0xeb + 6) == 1);
    /* Value in steel: coins 0-4 at 1, 5, 10, 25 and 50, over 50. */
    uint32_t coins[7] = {50, 10, 5, 2, 1, 1000, 1000};
    CHECK(cok_pool_value(coins) == (50 + 50 + 50 + 50 + 50) / 50);
    coins[0] = 0x80000000u;
    CHECK(cok_pool_value(coins) < 0);
    cok_pool_set_steel(&game.pool, 77);
    CHECK(game.pool.coins[3] == 0 && game.pool.coins[4] == 77);
    put_word(a->record + 0xeb, 9);
    cok_pool_pay(a->record, 66);
    CHECK(word(a->record + 0xeb) == 0 && word(a->record + 0xeb + 8) == 66);
    /* Steel given beyond what one can carry goes to the pool. */
    s.log[0] = '\0';
    put_word(a->record + 0x17d, (uint16_t)(cap - 4));
    CHECK(cok_pool_give_steel(&game, a->record, 10));
    CHECK(word(a->record + 0xeb + 8) == 70 && game.pool.coins[4] == 77 + 6);
    CHECK(LOGGED("print: Overloaded.  Money will be put in Pool.;"));
}

/* A COMBAT with the battle resolved by stub, or with no monsters loaded,
 * the treasure alone. */
static cok_ecl_status combat(cok_combat_stub stub)
{
    game.combat_stub = stub;
    cok_ecl_status status = RUN(COK_ECL_COMBAT);
    game.combat_stub = COK_COMBAT_UNPORTED;
    return status;
}

/* 351b:0037 and 0379: the experience. */
static void test_experience(void)
{
    /* Two knights and a monster worth 100 + 2 * 10, with 10 steel (worth
     * 2 each), and the pool's 250 silver (1 a hundred): 142 / 2 each. */
    reset("\rEN");
    cok_character *a = member("A"), *b = member("B");
    cok_character *m = monster("GOBLIN", 100, 2, 10);
    put_word(m->record + 0xeb + 8, 10);
    game.pool.coins[0] = 250;
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_OK);
    CHECK(LOGGED("print: The party has won.;print: Each character receives 71;"));
    CHECK(experience_of(a->record) == 71 && experience_of(b->record) == 71);
    CHECK(game.pool.coins[4] == 10 && game.pool.coins[0] == 250);
    CHECK(game.experience == 71 && game.experience_known);
    /* The monster's product is a signed word: 200 * 200 counts as
     * -25536. */
    reset("\r");
    a = member("A");
    monster("DRAGON", 0, 200, 200);
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_OK);
    CHECK(LOGGED("print: Each character receives -25536;"));
    CHECK(experience_of(a->record) == (uint32_t)-25536);
    /* The divisor is the party's size less those who cannot act or are
     * animated, a word: none left divides by zero. */
    reset("\r");
    a = member("A");
    a->record[0x188] = 1; /* animated: stands, but counts out */
    monster("GOBLIN", 100, 0, 0);
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_DIVIDE_BY_ZERO);
    /* More out than the party's size wraps the divisor: nearly nothing. */
    reset("\rEN");
    a = member("A");
    member("B")->record[0x189] = 0;
    member("C")->record[0x189] = 0;
    game.vm.mem7c00[0x33e] = 1;
    game.pool.coins[3] = 5000;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("print: Each character receives 0;"));
    /* Coins count by the pool's signed LongInts. */
    reset("\rEN");
    a = member("A");
    game.pool.coins[1] = 0xffffffecu; /* -20 copper: -1 */
    game.pool.coins[3] = 11;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("print: The party has found Treasure!;print: Each character receives 10;"));
}

/* The loot: monsters' items, unreadied and named, go first in the pool;
 * magic items count 400 a plus up to the missile recovered. */
static void test_loot(void)
{
    reset("\rT\033EN");
    cok_character *a = member("A");
    cok_character *m = monster("GOBLIN", 0, 0, 0);
    uint8_t *sword = item(m, 0x12, 3, 60);
    sword[0x34] = 1;
    item(m, 0x11, 100, 75);
    uint8_t kept[COK_ITEM_SIZE] = {0};
    kept[0x2e] = kept[0x31] = 0x13; /* "Short Sword" */
    kept[0x32] = 5;
    CHECK(cok_pool_insert(&game.pool, 0, kept));
    battle_missile = 1;
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_OK);
    battle_missile = 0;
    /* 3 * 400, and 100 * 400 as a signed word, -25536; the short sword is
     * the missile, where the count stops. */
    CHECK(LOGGED("print: Each character receives -24336;"));
    CHECK(experience_of(a->record) == (uint32_t)-24336);
    CHECK(LOGGED("item: Broad Sword ;item: Long Sword ;item: Short Sword ;"));
    CHECK(game.pool.item_count == 0 && game.pool.missile == 0);
    /* The copy is unreadied: taken, it is not readied. */
    reset("\rTT\033EN");
    a = member("A");
    game.vm.character = a->record;
    item(monster("GOBLIN", 0, 0, 0), 0x12, 0, 60)[0x34] = 1;
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_OK);
    CHECK(a->item_count == 1 && a->items[0][0x34] == 0);
    /* Monsters keep their items with 0x7ee3 1, not 2. */
    reset("\rEN");
    member("A");
    item(monster("GOBLIN", 0, 0, 0), 0x12, 1, 60);
    game.vm.mem7c00[0x2e3] = 1;
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_OK);
    CHECK(LOGGED("menu: View Pool Exit;") && LOGGED("receives 0;"));
    CHECK(game.vm.mem7c00[0x2e3] == 0);
    reset("\rT\033EN");
    member("A");
    item(monster("GOBLIN", 0, 0, 0), 0x12, 1, 60);
    game.vm.mem7c00[0x2e3] = 2;
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_OK);
    CHECK(LOGGED("menu: View Take Pool Exit;") && LOGGED("receives 400;"));
    /* A party member against the party, dropped, gives its items but not
     * its coins. */
    reset("\rT\033EN");
    a = member("A");
    cok_character *charmed = member("B");
    charmed->record[0x18a] = 1;
    charmed->record[0x188] = 6;
    put_word(charmed->record + 0xeb + 8, 500);
    item(charmed, 0x12, 0, 60);
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("item: Long Sword ;") && LOGGED("print: The party has won.;"));
    CHECK(game.pool.coins[4] == 0 && game.party.count == 1);
}

/* 351b:0379: the award by class. */
static void test_award(void)
{
    reset("\rEN");
    cok_character *cleric = member("C"), *fighter = member("F"), *weak = member("W");
    cok_character *two = member("T"), *three = member("H"), *out = member("O");
    cleric->record[0x5b] = 0;
    cleric->record[0x15] = 15;
    fighter->record[0x5b] = 2;
    fighter->record[0x11] = 16;
    weak->record[0x5b] = 2;
    weak->record[0x11] = 15;
    two->record[0x5b] = 8;
    three->record[0x5b] = 9;
    out->record[0x189] = 0;
    out->record[0x188] = 4;
    put_word(out->record + 0x197, 3);
    /* 5000 platinum among five who can act: 1000 each. */
    game.pool.coins[3] = 5000;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("receives 1000;"));
    CHECK(experience_of(cleric->record) == 1100 && experience_of(fighter->record) == 1100);
    CHECK(experience_of(weak->record) == 1000 && experience_of(two->record) == 500);
    CHECK(experience_of(three->record) == 333 && experience_of(out->record) == 0);
    /* The unconscious one with hit points comes round. */
    CHECK(out->record[0x188] == 0 && out->record[0x189] == 1);
    /* A cleric needs wisdom above 14, the others above 15. */
    reset("\rEN");
    cleric = member("C");
    cleric->record[0x5b] = 0;
    cleric->record[0x15] = 14;
    cok_character *ranger = member("R");
    ranger->record[0x5b] = 4;
    ranger->record[0x11] = ranger->record[0x13] = ranger->record[0x15] = 16;
    game.pool.coins[3] = 2000;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(experience_of(cleric->record) == 1000 && experience_of(ranger->record) == 1100);
    /* The bank pays no experience, and shows none. */
    reset("\rEN");
    cleric = member("C");
    game.pool.coins[4] = 100;
    game.vm.mem4b00[0x1f5] = 1;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(experience_of(cleric->record) == 0 && !LOGGED("receives"));
    CHECK(LOGGED("print: The party makes a withdrawl.  A small;print: fee has been assessed by "
                 "the bank.;"));
    CHECK(game.vm.mem4b00[0x1f5] == 0);
}

/* 351b:0574: the party after combat. */
static void test_after_combat(void)
{
    /* Without a battle: running comes back, unconscious with hit points
     * comes round, without stays, dying becomes unconscious. */
    reset("\rE");
    cok_character *a = member("A"), *b = member("B"), *c = member("C"), *d = member("D");
    cok_character *e = member("E");
    b->record[0x188] = 3;
    b->record[0x189] = 0;
    c->record[0x188] = 4;
    c->record[0x189] = 0;
    put_word(c->record + 0x197, 2);
    d->record[0x188] = 4;
    d->record[0x189] = 0;
    e->record[0x188] = 5;
    e->record[0x189] = 0;
    /* Only the first effect of each combat-only id goes. */
    CHECK(cok_character_add_effect(a, 0x0b, 5, 0, false) != NULL);
    CHECK(cok_character_add_effect(a, 0x0b, 6, 0, false) != NULL);
    CHECK(cok_character_add_effect(a, 0x05, 7, 0, false) != NULL);
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(b->record[0x188] == 0 && b->record[0x189] == 1);
    CHECK(c->record[0x188] == 0 && c->record[0x189] == 1);
    CHECK(d->record[0x188] == 4 && d->record[0x189] == 0);
    CHECK(e->record[0x188] == 4 && e->record[0x189] == 0);
    CHECK(a->effects->id == 0x0b && a->effects->duration == 6 && a->effects->next->id == 0x05);
    /* A fight that cannot kill (0x7ee6) brings them round with a hit
     * point. */
    reset("\rE");
    member("A");
    d = member("D");
    e = member("E");
    d->record[0x188] = 4;
    d->record[0x189] = 0;
    e->record[0x188] = 5;
    e->record[0x189] = 0;
    game.vm.mem7c00[0x2e6] = 1;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(d->record[0x188] == 0 && d->record[0x189] == 1 && d->record[0x197] == 1);
    CHECK(e->record[0x188] == 0 && e->record[0x197] == 1 && game.vm.mem7c00[0x2e6] == 0);
    /* With none standing, it is lost, and no experience was worked out:
     * the results would show DS:8840 from before, here unset. */
    reset("\rE");
    d = member("D");
    d->record[0x188] = 4;
    d->record[0x189] = 0;
    game.vm.mem7c00[0x2e6] = 1;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_UNDEFINED);
    reset("\rE");
    d = member("D");
    d->record[0x188] = 4;
    d->record[0x189] = 0;
    game.vm.mem7c00[0x2e6] = 1;
    game.experience_known = true;
    game.experience = 42;
    monster("GOBLIN", 0, 0, 0);
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("print: You have lost the fight.;print: Each character receives 0;"));
    CHECK(game.vm.mem7c00[0x2c7] == 0x80 && d->record[0x188] == 0);
    /* The party survives only with a player character on its side
     * standing or running: NPCs alone are destroyed, standing or not. */
    reset("\r");
    member("N")->record[0xe7] = 0x80;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(game.party_killed && game.party.count == 0 && game.vm.mem7c00[0x33e] == 0);
    CHECK(LOGGED("print: The monsters rejoice for the party has been destroyed;"));
    CHECK(game.vm.mem7c00[0x2c7] == 0x80);
    reset("\r");
    member("A")->record[0x18a] = 1;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK && game.party_killed);
    game.party_killed = false;
    /* Fled: those who ran come back, the rest are left behind; nothing is
     * won and the pool is emptied. */
    reset("\rE");
    a = member("A");
    b = member("B");
    b->record[0x188] = 5;
    b->record[0x189] = 0;
    monster("GOBLIN", 1000, 0, 0);
    game.pool.coins[4] = 500;
    uint8_t loot[COK_ITEM_SIZE] = {0};
    CHECK(cok_pool_insert(&game.pool, 0, loot));
    CHECK(combat(COK_COMBAT_FLED) == COK_ECL_OK);
    CHECK(game.vm.mem7c00[0x2c7] == 0x81 && game.party.count == 1 && game.party.members[0] == a);
    CHECK(a->record[0x188] == 0 && a->record[0x189] == 1 && game.vm.mem7c00[0x33e] == 1);
    CHECK(LOGGED("print: The party has fled.;print: Each character receives 0;"));
    CHECK(LOGGED("menu: View Pool Exit;") && game.pool.coins[4] == 0);
    /* An enemy that fled makes 0x7ec7 1. */
    reset("\rE");
    member("A");
    monster("GOBLIN", 0, 0, 0)->record[0x188] = 3;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK && game.vm.mem7c00[0x2c7] == 1);
    /* It was removed, so the party has won, though none dropped; and the
     * treasure's picture (PIC record 0x3c) was shown. */
    CHECK(LOGGED("print: The party has won.;") && game.picture_id == 0x3c);
}

/* 351b:1618: the NPCs' shares. */
static void test_shares(void)
{
    reset("\r\rEN");
    member("A");
    cok_character *n1 = member("N"), *n2 = member("M"), *n3 = member("O");
    n1->record[0xe7] = n2->record[0xe7] = n3->record[0xe7] = 0x80;
    n1->record[0xe8] = 2;
    n2->record[0xe8] = 8; /* no share, but it speaks */
    n2->record[0x109] = 1;
    n3->record[0xe8] = 3;
    n3->record[0x188] = 6; /* counts one, takes none */
    n3->record[0x189] = 0;
    /* Four shares: steel 1000, 250 a share, two taken; gems 2000, 500 a
     * share cut to a byte, 244. */
    game.pool.coins[4] = 1000;
    game.pool.coins[5] = 2000;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("print: N takes and hides his share.;print: M takes and hides her share.;"
                 "menu: press <enter>/<return> to continue;print: The party has found Treasure!;"));
    CHECK(!LOGGED("O takes"));
    CHECK(game.pool.coins[4] == 500 && game.pool.coins[5] == 2000 - 488);
    /* No NPC shares: nothing taken, nothing said. */
    reset("\rEN");
    member("A");
    member("M")->record[0xe7] = 0x80;
    game.pool.coins[4] = 1000;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK && game.pool.coins[4] == 1000);
    CHECK(!LOGGED("takes and hides"));
    /* Shares that wrap to 0 as a byte divide by zero. */
    reset("\r");
    for (int i = 0; i < 4; ++i) member("A");
    for (int i = 0; i < 36; ++i) {
        cok_character *n = calloc(1, sizeof *n);
        CHECK(n != NULL);
        n->record[0xe7] = 0x80;
        n->record[0xe8] = 7;
        n->record[0x189] = 1;
        CHECK(cok_party_append(&game.party, n));
    }
    game.pool.coins[4] = 1;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_DIVIDE_BY_ZERO);
}

/* 351b:118c: the treasure menu, Take, Pool, Share and Detect. */
static void test_treasure_menu(void)
{
    /* Items and money: Take offers both; Money lists jewelry first and
     * asks how much, at most the pool's. */
    reset("\rTM\r5\r\033ITEN");
    cok_character *a = member("A");
    game.vm.character = a->record;
    item(a, 0x12, 0, 60);
    game.pool.coins[5] = 3;
    game.pool.coins[6] = 4;
    uint8_t loot[COK_ITEM_SIZE] = {0};
    loot[0x2e] = loot[0x31] = 0x11;
    put_word(loot + 0x37, 75);
    CHECK(cok_pool_insert(&game.pool, 0, loot));
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("menu: View Take Pool Share Exit;menu: Money Items Exit;item: Jewelry 4;"
                 "item: Gems 3;choice: J;menu: How much Jewelry  will you take? ;input: 4;"
                 "item: Gems 3;"));
    CHECK(word(a->record + 0xeb + 12) == 4 && word(a->record + 0xeb + 10) == 0);
    /* Items, then: the pick goes to the end of the selected character's. */
    CHECK(LOGGED("item: Broad Sword ;choice: Broad Sword ;"));
    CHECK(a->item_count == 2 && a->items[1][0x2e] == 0x11 && a->record[0x142] == 2);
    /* Exit with gems left: asked, and No leaves. */
    CHECK(LOGGED("print: There is still treasure left.  ;print: Do you want to go back and "
                 "claim your treasure?;menu: ~Yes ~No;"));
    CHECK(game.pool.item_count == 0);
    /* Yes goes back to the menu; Pool and Share empty the pool. */
    reset("\rEYPSE");
    a = member("A");
    cok_character *b = member("B");
    game.pool.coins[5] = 3;
    put_word(a->record + 0xeb + 10, 2);
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("menu: ~Yes ~No;menu: View Take Pool Share Exit;menu: View Take Pool Share "
                 "Exit;menu: View Pool Exit;"));
    CHECK(word(a->record + 0xeb + 10) == 3 && word(b->record + 0xeb + 10) == 2);
    /* Gems alone: Take goes straight to them, "How Many". */
    reset("\rT\r2\rE");
    a = member("A");
    game.vm.character = a->record;
    game.pool.coins[5] = 2;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("menu: How Many Gems  will you take? ;input: 2;"));
    /* At most the pool's low word. */
    reset("\rT\r9\r\033EN");
    a = member("A");
    game.vm.character = a->record;
    game.pool.coins[5] = 0x10003;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("item: Gems 65539;choice: G;menu: How Many Gems  will you take? ;input: 3;"));
    CHECK(game.pool.coins[5] == 0x10000 && word(a->record + 0xeb + 10) == 3);
    /* Items alone: Take lists them; one that would overload stays, said
     * so. */
    reset("\rTT\033EN");
    a = member("A");
    game.vm.character = a->record;
    loot[0x37] = 0xff;
    loot[0x38] = 0x7f;
    CHECK(cok_pool_insert(&game.pool, 0, loot));
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("menu: View Take Pool Exit;item: Broad Sword ;choice: Broad Sword ;print: "
                 "OverLoaded;item: Broad Sword ;"));
    CHECK(a->item_count == 0);
    /* Detect, with items and a Detect Magic memorized by one who can act. */
    reset("\rDTEN");
    a = member("A");
    game.vm.character = a->record;
    a->record[0x1e + 3] = 5;
    loot[0x37] = 1;
    loot[0x38] = 0;
    loot[0x32] = 1;
    CHECK(cok_pool_insert(&game.pool, 0, loot));
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("menu: View Take Pool Detect Exit;"));
    CHECK(LOGGED("item: * Broad Sword ;"));
    CHECK(a->record[0x1e + 3] == 0 && !LOGGED("casts"));
    /* Not without items. */
    reset("\rEN");
    a = member("A");
    game.vm.character = a->record;
    a->record[0x1e] = 5;
    game.pool.coins[0] = 1;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("menu: View Take Pool Share Exit;"));
    reset("\rEN");
    a = member("A");
    game.vm.character = a->record;
    a->record[0x1e] = 0x4d;
    a->record[0x189] = 0;
    member("B");
    CHECK(cok_pool_insert(&game.pool, 0, loot));
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(LOGGED("menu: View Take Pool Exit;"));
    /* View in treasure: Drop puts the money in the pool. */
    reset("\rVd\r9\r\033\033EN");
    a = member("A");
    game.vm.character = a->record;
    put_word(a->record + 0xeb + 2, 20);
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(game.pool.coins[1] == 9 && word(a->record + 0xeb + 2) == 11);
}

/* Count the times text appears in the log. */
static int logged_times(const char *text)
{
    int n = 0;
    for (const char *p = strstr(s.log, text); p != NULL; p = strstr(p + 1, text)) ++n;
    return n;
}

/* A pool of more items than a list shows: each is named, 255 rows. */
static void test_long_list(void)
{
    reset("\rTT\033EN");
    cok_character *a = member("A");
    game.vm.character = a->record;
    uint8_t loot[COK_ITEM_SIZE] = {0};
    loot[0x2e] = loot[0x31] = 0x11;
    put_word(loot + 0x37, 1);
    for (int i = 0; i < 300; ++i) CHECK(cok_pool_insert(&game.pool, 0, loot));
    game.pool.items[299][0x31] = 0x12;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(a->item_count == 1 && logged_times("item: Broad Sword ;") == 2 * 255);
    CHECK(!LOGGED("item: Long Sword ;"));
}

/* 351b:185f, 36d0:034c: weapons lost in combat go back to their owners. */
static void test_weapons_back(void)
{
    reset("\rE");
    cok_character *a = member("A"), *full = member("F");
    for (int i = 0; i < 16; ++i) item(full, 0x12, 0, 1);
    CHECK(cok_character_stats(full, &game.item_types, s.log, sizeof s.log));
    s.log[0] = '\0';
    game.lost_weapons = calloc(2, sizeof *game.lost_weapons);
    CHECK(game.lost_weapons != NULL);
    game.lost_weapon_count = 2;
    game.lost_weapons[0].item[0x2e] = game.lost_weapons[0].item[0x31] = 0x11;
    game.lost_weapons[0].owner = a->record;
    game.lost_weapons[1].item[0x2e] = 0x12;
    game.lost_weapons[1].owner = full->record;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_OK);
    CHECK(a->item_count == 1 && a->items[0][0x2e] == 0x11 && a->record[0x142] == 1);
    CHECK(full->item_count == 16 && LOGGED("print: OverLoaded;"));
    CHECK(game.lost_weapons == NULL && game.lost_weapon_count == 0);
    /* A weapon whose owner is gone cannot go back. */
    reset("\rE");
    member("A");
    game.lost_weapons = calloc(1, sizeof *game.lost_weapons);
    CHECK(game.lost_weapons != NULL);
    game.lost_weapon_count = 1;
    game.lost_weapons[0].owner = (uint8_t *)&game;
    CHECK(combat(COK_COMBAT_UNPORTED) == COK_ECL_UNDEFINED);
    CHECK(game.lost_weapons == NULL);
}

/* View's Trade from the treasure menu: Escape at the first coin list tests
 * a byte of the stack (see sheet.h), which loading the treasure's picture
 * zero-fills; a View or Yes leaves it set, Take unknown. */
static void test_trade_byte(void)
{
    static const struct {
        const char *keys;
        bool battle, loaded;
        int picks;               /* "Trade to?" asked, 0 for a stop */
    } cases[] = {
        /* After a battle, which frees the picture: zero, so Escape asks
         * for another partner, Exit leaves. */
        {"\rVTS\033EEEN", true, false, 2},
        {"\rVTS\033EEEN", true, true, 2},
        /* Without one, the picture loaded: zero too. */
        {"\rVTS\033EEEN", false, false, 2},
        /* With the treasure's picture already loaded: unknown. */
        {"\rVTS\033", false, true, 0},
        /* A View before: set, Escape leaves Trade. */
        {"\rVEVTS\033EEN", true, false, 1},
        /* Pool, Share and a pick leave it as it was. */
        {"\rPS\001PVTS\033EEEN", true, false, 2},
        /* Yes to go back: set. */
        {"\rEYVTS\033EEN", true, false, 1},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        reset(cases[i].keys);
        cok_character *a = member("A");
        member("B");
        put_word(a->record + 0xeb + 8, 5);
        game.vm.character = a->record;
        if (cases[i].battle) monster("GOBLIN", 0, 0, 0);
        if (cases[i].loaded) cok_adventure_load_picture(&game, 0x3c);
        else cok_adventure_free_picture(&game);
        uint8_t loot[COK_ITEM_SIZE] = {0};
        loot[0x2e] = loot[0x31] = 0x11;
        CHECK(cok_pool_insert(&game.pool, 0, loot));
        cok_ecl_status status = combat(cases[i].battle ? COK_COMBAT_WON : COK_COMBAT_UNPORTED);
        CHECK(LOGGED("print: Knight;item: Steel "));
        if (cases[i].picks == 0) {
            CHECK(status == COK_ECL_UNDEFINED && logged_times("menu: Select Exit;") == 1);
            continue;
        }
        CHECK(status == COK_ECL_OK && s.at == s.length && game.pool.item_count == 0);
        CHECK(logged_times("menu: Select Exit;") == cases[i].picks);
    }
    /* Take, after the items: unknown. */
    reset("\rTTVTS\033");
    cok_character *a = member("A");
    put_word(a->record + 0xeb + 8, 5);
    game.vm.character = a->record;
    monster("GOBLIN", 0, 0, 0);
    uint8_t loot[COK_ITEM_SIZE] = {0};
    CHECK(cok_pool_insert(&game.pool, 0, loot));
    CHECK(combat(COK_COMBAT_WON) == COK_ECL_UNDEFINED && LOGGED("item: Steel "));
}

int main(void)
{
    open_game();
    test_tables();
    test_generator();
    test_treasure_opcode();
    test_tree();
    test_pool();
    test_experience();
    test_loot();
    test_award();
    test_after_combat();
    test_shares();
    test_treasure_menu();
    test_long_list();
    test_weapons_back();
    test_trade_byte();
    cok_adventure_close(&game);
    puts("treasure tests passed");
    return 0;
}
