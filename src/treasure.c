#include "treasure.h"

#include "camp.h"
#include "cast.h"
#include "effect.h"
#include "items.h"
#include "screen.h"
#include "sheet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_text(cok_adventure *game, const char *kind, const char *text)
{
    cok_adventure_log(game, kind, text);
}

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* A signed word as a LongInt (CWD), in the unsigned arithmetic the sums
 * use. */
static uint32_t extend(uint16_t w)
{
    return (uint32_t)(int32_t)(int16_t)w;
}

static void put_word(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* The coins' names (DS:12e3) and the words of 58e7:0b95's questions. */
static const char *const coin_names[COK_COINS] = {"Silver", "Copper", "Bronze", "Platinum",
                                                  "Steel", "Gems", "Jewelry"};
static const char *const coin_words[COK_COINS] = {"silver ", "Copper ", "bronze ", "platinum ",
                                                  "steel ", "Gems ", "Jewelry "};

/* The item generator (58e7:1039). */

/* The templates of the generator's potions, wand and javelin, eight words
 * from DS:0b88 + 2 * t for t 1, 9, 0x11, 0x21 and 0x31 (0x19 and 0x29 are
 * never chosen): the name parts, weight, value and +0x3c-+0x3e. */
static const uint8_t template_table[0x72] = { /* DS:0b88-0bf9 */
    0x0f, 0x03, 0x5f, 0x00, 0x5e, 0x00, 0x35, 0x00, 0x01, 0x00, 0x90, 0x01, 0x03, 0x00, 0x63, 0x00,
    0x00, 0x00, 0x50, 0x00, 0x5a, 0x00, 0x35, 0x00, 0x01, 0x00, 0x26, 0x02, 0x01, 0x00, 0x3b, 0x00,
    0x00, 0x00, 0x5f, 0x00, 0x5a, 0x00, 0x35, 0x00, 0x01, 0x00, 0xc8, 0x00, 0x01, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x5c, 0x00, 0x5a, 0x00, 0x35, 0x00, 0x01, 0x00, 0xe1, 0x00, 0x01, 0x00, 0x30, 0x00,
    0x00, 0x00, 0x62, 0x00, 0x5a, 0x00, 0x33, 0x00, 0x01, 0x00, 0x7c, 0x15, 0x1e, 0x00, 0x41, 0x00,
    0x00, 0x00, 0x4e, 0x00, 0x5a, 0x00, 0x48, 0x00, 0x0a, 0x00, 0x4c, 0x1d, 0x00, 0x00, 0x26, 0x00,
    0x83, 0x00, 0x58, 0x00, 0x5a, 0x00, 0x07, 0x00, 0x14, 0x00, 0xdc, 0x05, 0x01, 0x00, 0x33, 0x00,
    0x00, 0x00,
};

const cok_ds_table cok_treasure_templates = {0x0b88, sizeof template_table, 1, template_table};

static uint16_t template_word(unsigned index)
{
    return word(template_table + 2 * index);
}

/* The bonus of a magic weapon or armour (58e7:1005): +2 on a d10 of 10,
 * else +1. */
static uint8_t bonus_roll(uint32_t *seed)
{
    return cok_dice(seed, 1, 10) == 10 ? 2 : 1;
}

/* A spell for a scroll of order (0 cleric, 2 White, 3 Red) at level 1-4,
 * from the tables at DS:423a (cleric), 4262 (White) and 428a (Red), ten
 * bytes a level, indexed from 1 by a die of the level's size. */
static uint8_t scroll_spell(uint32_t *seed, uint8_t order, uint8_t level)
{
    static const uint8_t cleric[4] = {4, 7, 4, 4}, white[4] = {8, 3, 6, 7}, red[4] = {7, 6, 6, 4};
    const uint8_t *sides = order == 0 ? cleric : order == 2 ? white : red;
    uint16_t base = order == 0 ? 0x423a : order == 2 ? 0x4262 : 0x428a;
    uint8_t roll = cok_dice(seed, 1, sides[level - 1]), spell = 0;
    /* The stat tables hold every byte the dice reach (DS:423b-42ac,
     * checked by test_treasure); one they did not would read as 0. */
    if (!cok_ds_byte((uint16_t)(base + 10 * (level - 1) + roll), &spell)) return 0;
    return spell;
}

/* The weight (+0x37) of a weapon, armour, ring or bracers, and its count
 * (+0x39): 20 of darts and of anything not listed, which arrows and
 * quarrels are. */
static void weapon_weight(uint8_t *item)
{
    static const struct { uint8_t type; uint16_t weight; } weights[] = {
        {0x01, 75}, {0x11, 75}, {0x06, 50}, {0x0e, 50}, {0x0f, 50}, {0x17, 50}, {0x19, 50},
        {0x25, 50}, {0x1d, 50}, {0x43, 50}, {0x09, 125}, {0x08, 100}, {0x10, 100}, {0x18, 100},
        {0x46, 150}, {0x1f, 150}, {0x47, 175}, {0x04, 10}, {0x32, 10}, {0x0a, 60}, {0x12, 60},
        {0x16, 80}, {0x07, 20}, {0x0d, 40}, {0x13, 35}, {0x14, 250}, {0x20, 250}, {0x21, 400},
        {0x22, 300}, {0x23, 350}, {0x24, 450}, {0x1c, 1}, {0x3b, 1},
    };
    uint8_t type = item[0x2e];
    if (type == 5) {
        put_word(item + 0x37, 5);
        item[0x39] = 20;
        return;
    }
    for (size_t i = 0; i < sizeof weights / sizeof *weights; ++i) {
        if (weights[i].type != type) continue;
        put_word(item + 0x37, weights[i].weight);
        return;
    }
    put_word(item + 0x37, 4);
    item[0x39] = 20;
}

void cok_treasure_item(uint32_t *seed, uint8_t type, uint8_t item[COK_ITEM_SIZE])
{
    memset(item, 0, COK_ITEM_SIZE);
    item[0x35] = 6; /* +0x30 and +0x2f hidden */
    item[0x2e] = type;
    uint8_t template = 0;
    if ((type >= 1 && type <= 0x25) || type == 0x32 || type == 0x46 || type == 0x47 ||
        type == 0x3b) {
        item[0x32] = bonus_roll(seed);
        uint8_t plus = (uint8_t)(0x6e + item[0x32]); /* "+1", "+2" */
        if (type == 7 && cok_dice(seed, 1, 5) == 5) template = 0x31; /* of Lightning */
        if (type >= 0x1f && type <= 0x24) {
            item[0x31] = type;
            item[0x30] = type == 0x1f ? 0x30 : 0x2f; /* "Armor", "Mail" */
            item[0x2f] = plus;
            item[0x35] = 4;
        } else if (type == 0x32) {
            item[0x31] = 0x32; /* "Bracers" "of" "AC 6" */
            item[0x30] = 0x5a;
            item[0x32] = 4;
            item[0x2f] = 0x6b;
        } else if (type == 0x3b) {
            item[0x31] = 0x2e; /* "Ring" "Of Prot" */
            item[0x30] = 0x31;
            item[0x2f] = plus;
        } else {
            item[0x31] = type;
            item[0x30] = plus;
        }
        weapon_weight(item);
        uint16_t multiplier = type == 0x25 ? 1250 : type == 0x1e ? 75
                            : type == 0x20 || type == 0x21 || type == 0x32 ? 1500
                            : type == 0x22 ? 1750 : type == 0x23 ? 2000
                            : type == 0x24 ? 2500 : 1000;
        put_word(item + 0x3a, (uint16_t)((int8_t)item[0x32] * multiplier));
    } else if (type == 0x27 || type == 0x28) {
        item[0x35] = 4;
        uint8_t spells = cok_dice(seed, 1, 3), order = 0;
        if (type == 0x27) {
            order = cok_dice(seed, 1, 2) == 1 ? 3 : 2;
            item[0x31] = order == 3 ? 0x78 : 0x77; /* "Red", "White" */
            item[0x30] = 0x28;                     /* "MU Scroll" */
        } else {
            item[0x31] = 0x26; /* "Cler" "Scroll" */
            item[0x30] = 0x27;
        }
        item[0x2f] = (uint8_t)(0x63 + spells); /* "1 Spell", "2 Spells", "3 Spell" */
        item[0x32] = 1;
        put_word(item + 0x37, 25);
        for (uint8_t s = 1; s <= spells; ++s) {
            uint8_t level = cok_dice(seed, 1, 4);
            item[0x3b + s] = scroll_spell(seed, order, level);
            put_word(item + 0x3a, (uint16_t)(word(item + 0x3a) + level * 150u));
        }
        if (order == 2)
            item[0x35] |= 0x20;
        else if (order == 3)
            item[0x35] |= 0x10;
        else
            item[0x35] = 0;
    } else if (type == 0x34) {
        template = 0x21; /* Wand of Magic Missiles */
    } else if (type == 0x35) {
        template = 9; /* Potion of Giant Strength */
    } else if (type == 0x30) {
        template = cok_dice(seed, 1, 8) <= 5 ? 0x11 : 1; /* Healing, Extra Healing */
    }
    if (template == 0) return;
    /* The template keeps the type and the hidden parts, so the name shows
     * "Potion", "Wand" or "Javelin" alone. */
    for (unsigned i = 0; i < 3; ++i) item[0x2f + i] = (uint8_t)template_word(template + i);
    item[0x32] = 1;
    item[0x33] = 1;
    put_word(item + 0x37, template_word(template + 3u));
    item[0x39] = 0;
    put_word(item + 0x3a, template_word(template + 4u));
    for (unsigned i = 1; i <= 3; ++i) item[0x3b + i] = (uint8_t)template_word(template + 4u + i);
}

/* The pool. */

bool cok_pool_insert(cok_pool *pool, size_t index, const uint8_t *item)
{
    uint8_t (*items)[COK_ITEM_SIZE] = realloc(pool->items, (pool->item_count + 1) * sizeof *items);
    if (items == NULL) return false;
    pool->items = items;
    memmove(items + index + 1, items + index, (pool->item_count - index) * sizeof *items);
    memcpy(items[index], item, COK_ITEM_SIZE);
    memset(items[index] + 0x2a, 0, 4); /* the link */
    ++pool->item_count;
    if (pool->missile > index) ++pool->missile;
    return true;
}

void cok_pool_remove(cok_pool *pool, size_t index)
{
    memmove(pool->items + index, pool->items + index + 1,
            (pool->item_count - index - 1) * sizeof *pool->items);
    --pool->item_count;
    if (pool->missile == index + 1)
        pool->missile = 0;
    else if (pool->missile > index + 1)
        --pool->missile;
}

void cok_pool_free_items(cok_pool *pool)
{
    free(pool->items);
    pool->items = NULL;
    pool->item_count = 0;
    pool->missile = 0;
}

/* The weight c may still carry (58e7:0019): its allowance (6346:153b) +
 * 1500, as a word. Returns false, ending the run, where the allowance is
 * uninitialized in the original. */
static bool carry(cok_adventure *game, const uint8_t *c, uint16_t *out)
{
    int16_t allowance;
    char error[300];
    if (!cok_character_allowance(c, &allowance, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    *out = (uint16_t)(allowance + 1500);
    return true;
}

bool cok_pool_overloaded(cok_adventure *game, const uint8_t *c, uint16_t amount,
                         bool *overloaded, uint16_t *fits)
{
    uint16_t cap;
    if (!carry(game, c, &cap)) return false;
    uint16_t weight = word(c + 0x17d);
    *overloaded = (uint16_t)(weight + amount) > cap;
    *fits = *overloaded && weight <= cap ? (uint16_t)(cap - weight) : 0;
    return true;
}

/* Add amount of coin k to c's coins and weight (58e7:0053), as words. */
static void give(uint8_t *c, int k, uint16_t amount)
{
    put_word(c + 0xeb + 2 * k, (uint16_t)(word(c + 0xeb + 2 * k) + amount));
    put_word(c + 0x17d, (uint16_t)(word(c + 0x17d) + amount));
}

/* "Overloaded" on row 24 (6346:1827), then another pause (1521:0b4b); the
 * original then flushes the keyboard (1614:045c). */
static void overloaded_notice(cok_adventure *game, const char *text)
{
    cok_camp_notice(game, text);
    cok_adventure_wait(game, game->speed * 100u);
}

void cok_pool_status(const cok_pool *pool, bool *money, bool *items)
{
    *money = false;
    for (int k = 0; k < COK_COINS; ++k)
        if (pool->coins[k] != 0) *money = true;
    *items = pool->item_count > 0;
}

void cok_pool_gather(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *c = game->party.members[i]->record;
        if (c[0xe7] != 0 && c[0xe7] != 0xb3) continue;
        for (int k = 0; k < COK_COINS; ++k) {
            uint16_t coins = word(c + 0xeb + 2 * k);
            game->pool.coins[k] += coins;
            put_word(c + 0x17d, (uint16_t)(word(c + 0x17d) - coins)); /* 58e7:0039 */
        }
        memset(c + 0xeb, 0, 2 * COK_COINS);
    }
}

bool cok_pool_share(cok_adventure *game)
{
    cok_pool *pool = &game->pool;
    cok_party *party = &game->party;
    /* 58e7:05e0: the shares, a byte, count the player characters, turned
     * ones (+0xe7 0xb3) too. */
    uint8_t shares = 0;
    for (size_t i = 0; i < party->count; ++i) {
        uint8_t kind = party->members[i]->record[0xe7];
        if (kind == 0 || kind == 0xb3) ++shares;
    }
    uint16_t share[COK_COINS] = {0}, rest[COK_COINS] = {0};
    for (int k = 0; k < COK_COINS; ++k) {
        int32_t coins = (int32_t)pool->coins[k];
        if (coins <= 0) continue;
        if (shares == 0) {
            cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                               "Share divides the pool by no player characters (58e7:06a6)");
            return false;
        }
        share[k] = (uint16_t)(coins / shares);
        rest[k] = (uint16_t)(coins % shares);
    }
    /* Each record below 0x80 takes a share of each kind, from jewelry
     * down, and a coin of the rest while it has room; turned characters,
     * though counted, take none. */
    for (size_t i = 0; i < party->count; ++i) {
        uint8_t *c = party->members[i]->record;
        if (c[0xe7] >= 0x80) continue;
        for (int k = COK_COINS - 1; k >= 0; --k) {
            bool full;
            uint16_t fits;
            if (!cok_pool_overloaded(game, c, share[k], &full, &fits)) return false;
            if (full) {
                give(c, k, fits);
                rest[k] = (uint16_t)(rest[k] + share[k] - fits);
                continue;
            }
            give(c, k, share[k]);
            if (rest[k] == 0) continue;
            if (!cok_pool_overloaded(game, c, 1, &full, &fits)) return false;
            if (full) continue;
            give(c, k, 1);
            --rest[k];
        }
    }
    /* What is left goes to every record in turn, as much as each has room
     * for, which wraps for one carrying more than it may. */
    for (int k = COK_COINS - 1; k >= 0; --k) {
        if (rest[k] == 0) continue;
        for (size_t i = 0; i < party->count; ++i) {
            uint8_t *c = party->members[i]->record;
            uint16_t cap;
            if (!carry(game, c, &cap)) return false;
            uint16_t room = (uint16_t)(cap - word(c + 0x17d));
            if (room == 0) continue;
            uint16_t amount = rest[k] > room ? room : rest[k];
            give(c, k, amount);
            rest[k] = (uint16_t)(rest[k] - amount);
        }
    }
    for (int k = 0; k < COK_COINS; ++k) pool->coins[k] = rest[k];
    return true;
}

bool cok_pool_take(cok_adventure *game, uint8_t *c, uint16_t amount, int k)
{
    bool full;
    uint16_t fits;
    if (!cok_pool_overloaded(game, c, amount, &full, &fits)) return false;
    if (full) {
        overloaded_notice(game, "Overloaded");
        return true;
    }
    if ((int32_t)amount > (int32_t)game->pool.coins[k]) amount = (uint16_t)game->pool.coins[k];
    game->pool.coins[k] -= amount;
    give(c, k, amount);
    return true;
}

int32_t cok_pool_value(const uint32_t coins[COK_COINS])
{
    static const uint32_t rates[5] = {1, 5, 10, 25, 50}; /* DS:0f46, 0f56 */
    uint32_t sum = 0;
    for (int k = 0; k < 5; ++k) sum += coins[k] * rates[k];
    return (int32_t)sum / 50;
}

void cok_pool_set_steel(cok_pool *pool, uint16_t steel)
{
    for (int k = 0; k < 5; ++k) pool->coins[k] = 0;
    pool->coins[4] = steel;
}

void cok_pool_pay(uint8_t *c, uint16_t steel)
{
    memset(c + 0xeb, 0, 10);
    put_word(c + 0xeb + 8, steel);
}

bool cok_pool_give_steel(cok_adventure *game, uint8_t *c, uint16_t steel)
{
    bool full;
    uint16_t fits;
    if (!cok_pool_overloaded(game, c, steel, &full, &fits)) return false;
    if (!full) {
        give(c, 4, steel);
        return true;
    }
    overloaded_notice(game, "Overloaded.  Money will be put in Pool.");
    give(c, 4, fits);
    game->pool.coins[4] = game->pool.coins[4] + steel - fits;
    return true;
}

void cok_pool_drop(cok_adventure *game, uint8_t *c, uint16_t amount, int k)
{
    put_word(c + 0xeb + 2 * k, (uint16_t)(word(c + 0xeb + 2 * k) - amount));
    put_word(c + 0x17d, (uint16_t)(word(c + 0x17d) - amount));
    if (game->vm.mode == 6 || game->vm.mode == 1) game->pool.coins[k] += amount;
}

/* TREASURE (2fd3:1d21). */

/* The file's number as Str(DS:5782) into a string[1] makes it: its first
 * digit. */
static char file_digit(const cok_adventure *game)
{
    char text[8];
    snprintf(text, sizeof text, "%u", game->vm.file);
    return text[0];
}

/* Put the items of ITEM<file> record id first in the pool, each before the
 * one before it, so in the reverse order. A record that is not there says
 * "Unable to find item file" and quits to DOS; the original does the same
 * after copying one item from its empty buffer. A missing file makes it ask
 * for the disk and wait, and fails the run. Returns false, ending the run,
 * where the original would read past a record that is not whole items. */
static bool load_items(cok_adventure *game, uint8_t id)
{
    char name[16];
    snprintf(name, sizeof name, "ITEM%c", file_digit(game));
    bool no_file;
    size_t size = 0;
    uint8_t *data = cok_adventure_find_record(game, name, id, &size, &no_file);
    if (data == NULL && no_file) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    if (size == 0) {
        free(data);
        log_text(game, "print", "Unable to find item file");
        cok_adventure_prompt_key(game, "Unable to find item file");
        log_text(game, "quit", "to DOS");
        game->quit = true;
        game->vm.abort = true;
        return false;
    }
    if (size % COK_ITEM_SIZE != 0) {
        free(data);
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "%s record %u is not whole items; TREASURE reads past it (2fd3:1df9)",
                           name, id);
        return false;
    }
    for (size_t at = 0; at < size; at += COK_ITEM_SIZE) {
        if (cok_pool_insert(&game->pool, 0, data + at)) continue;
        free(data);
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
        return false;
    }
    free(data);
    return true;
}

/* The type of a random item, TREASURE's own tree of dice (2fd3:1ed7): a
 * d100 of 1-20 for a weapon or ammunition (a second d100 and a d10, d20,
 * or d4 and d20 or d8), 21-65 a magic-user's scroll, 66-70 a cleric's,
 * 71-80 a shield, 81-85 a ring of protection, 86-95 armour (d20) and
 * 96-100 bracers, potions or a wand (d6). */
static uint8_t random_type(uint32_t *seed)
{
    static const uint8_t blunt[10] = {6, 6, 8, 8, 8, 8, 0x46, 0x46, 0x0f, 0x1d};
    static const uint8_t missile[20] = {5, 5, 5, 7, 7, 0x18, 0x18, 0x18, 0x16, 0x16,
                                        0x19, 0x19, 0x17, 0x17, 0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 0x1e};
    static const uint8_t sword[20] = {0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x11, 0x11,
                                      0x11, 0x11, 0x11, 0x0d, 0x10, 0x13, 0x13, 0x13, 0x14, 0x14};
    static const uint8_t other[8] = {1, 4, 4, 8, 8, 0x0a, 0x0e, 0x47};
    static const uint8_t armour[20] = {0x1f, 0x1f, 0x1f, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
                                       0x23, 0x24, 0x24, 0x24, 0x24, 0x24, 0x20, 0x21, 0x22, 0x22};
    static const uint8_t misc[6] = {0x32, 0x32, 0x30, 0x30, 0x34, 0x35};
    uint8_t roll = cok_dice(seed, 1, 100);
    if (roll <= 20) {
        uint8_t kind = cok_dice(seed, 1, 100);
        if (kind <= 10) return blunt[cok_dice(seed, 1, 10) - 1];
        if (kind <= 25) return missile[cok_dice(seed, 1, 20) - 1];
        if (cok_dice(seed, 1, 4) < 4) return sword[cok_dice(seed, 1, 20) - 1];
        return other[cok_dice(seed, 1, 8) - 1];
    }
    if (roll <= 65) return 0x27;
    if (roll <= 70) return 0x28;
    if (roll <= 80) return 0x25;
    if (roll <= 85) return 0x3b;
    if (roll <= 95) return armour[cok_dice(seed, 1, 20) - 1];
    return misc[cok_dice(seed, 1, 6) - 1];
}

/* TREASURE silver copper bronze platinum steel gems jewelry items: the
 * coins replace the pool's, and by the items operand's low byte, the items
 * of ITEM<file> record items below 0x80, or items - 0x80 random ones up to
 * 0xfe, go first in the pool; then for random ones every item in the pool
 * is named (6346:0488). 0xff adds none. */
static void treasure(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_pool *pool = &game->pool;
    for (size_t k = 0; k < COK_COINS; ++k) pool->coins[k] = cok_ecl_value(vm, k);
    uint8_t items = (uint8_t)cok_ecl_value(vm, 7);
    size_t before = pool->item_count;
    if (items < 0x80) {
        if (!load_items(game, items)) return;
    } else if (items != 0xff) {
        for (uint8_t i = 1; i <= items - 0x80; ++i) {
            uint8_t item[COK_ITEM_SIZE];
            cok_treasure_item(&vm->seed, random_type(&vm->seed), item);
            if (cok_pool_insert(pool, 0, item)) continue;
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
            return;
        }
        char name[41];
        for (size_t i = 0; i < pool->item_count; ++i)
            if (!cok_item_name(game, pool->items[i], false, name)) return;
    }
    char text[160] = "";
    for (int k = 0; k < COK_COINS; ++k) {
        if (pool->coins[k] == 0) continue;
        size_t used = strlen(text);
        snprintf(text + used, sizeof text - used, "%s%lu %s", used > 0 ? ", " : "",
                 (unsigned long)pool->coins[k], coin_names[k]);
    }
    if (text[0] != '\0') log_text(game, "treasure", text);
    for (size_t i = 0; i < pool->item_count - before; ++i) {
        char name[41];
        uint8_t *item = pool->items[i];
        size_t length = item[0] > 40 ? 40 : item[0];
        memcpy(name, item + 1, length);
        name[length] = '\0';
        snprintf(text, sizeof text, "item %s", name);
        log_text(game, "treasure", text);
    }
}

bool cok_treasure_opcode(cok_adventure *game)
{
    if (game->vm.opcode != COK_ECL_TREASURE) return false;
    treasure(game);
    return true;
}

/* The end of combat (overlay 351b). */

static void name_of(const uint8_t *c, char out[16])
{
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(out, c + 1, length);
    out[length] = '\0';
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

static cok_character *selected(cok_adventure *game)
{
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* Whether record c is past the party in combat: its combat record's +0x13,
 * which reads as 0 with none (the original reads 0000:0013). */
static bool past_party(const cok_character *c)
{
    return cok_character_combat(c)->not_party == 1;
}

/* What the end of combat finds and its screens show: DS:43c4, 43c5, 8844,
 * 883f and 4b57. */
typedef struct {
    bool monsters;  /* DS:43c4: an enemy was defeated or removed. */
    bool fled;      /* DS:43c5: the party fled. */
    bool standing;  /* DS:8844: a party record has status 0 or 1. */
    uint8_t out;    /* DS:883f: party records that cannot act or are animated. */
    bool destroyed; /* DS:4b57. */
} outcome;

/* Remove record index from the list as 4def:3b0a does, freeing its icon
 * slot's pictures (6d21:0156), and unless it is a monster, counting it out
 * of the party's size (0x7f3e); the record before it is then selected, or
 * the first. The spell target and trade partner the original would keep
 * pointing at it are forgotten. The selection kept in DS:43bf is restored
 * by EXIT after LOAD CHARACTER (DS:43ba) and when a block's vectors end
 * (2fd3:3b47); where either would select the freed record, the original
 * would go on with it, and this returns false, ending the run, unless the
 * party was destroyed, where the run ends before either restore. */
static bool remove_record(cok_adventure *game, size_t index, bool monster, bool destroyed)
{
    cok_ecl *vm = &game->vm;
    uint8_t *gone = game->party.members[index]->record;
    if (vm->saved_character == gone && !destroyed &&
        (vm->restore_character || game->restoring)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "the selection kept in DS:43bf would be a record the end of combat "
                           "freed");
        return false;
    }
    if (vm->saved_character == gone) vm->saved_character = NULL;
    if (game->spell_target == gone) game->spell_target = NULL;
    if (game->trade_partner == gone) game->trade_partner = NULL;
    uint8_t slot = gone[0x137];
    if (slot < COK_ICON_SLOTS)
        for (size_t pose = 0; pose < 2; ++pose) cok_picture_free(&game->icons[slot][pose]);
    cok_party_remove(&game->party, index);
    if (!monster) --vm->mem7c00[0x33e];
    vm->character = cok_party_record(&game->party, index > 0 ? index - 1 : 0);
    return true;
}

bool cok_treasure_take_item(cok_adventure *game, const uint8_t *item, bool *failed)
{
    cok_character *c = selected(game);
    if (c == NULL)
        return undefined(game, "taking an item with no character selected reads through NULL "
                               "(36d0:034c)");
    int over = cok_item_overloaded(game, c, item);
    if (over < 0) return false;
    *failed = over != 0;
    if (*failed) {
        cok_camp_notice(game, "OverLoaded");
        return true;
    }
    if (!cok_character_insert_item(c, c->item_count, item))
        return undefined(game, "out of memory");
    char error[300];
    if (!cok_character_stats(c, &game->item_types, error, sizeof error))
        return undefined(game, error);
    return true;
}

/* 351b:185f: give each weapon lost in combat back to its owner, selected
 * for it (36d0:034c); one that would overload it is lost. The last owner
 * stays selected. */
static bool weapons_back(cok_adventure *game)
{
    bool ok = true;
    for (size_t i = 0; i < game->lost_weapon_count && ok; ++i) {
        cok_lost_weapon *lost = &game->lost_weapons[i];
        if (cok_party_index(&game->party, lost->owner) == game->party.count) {
            ok = undefined(game, "a weapon lost in combat goes back to a record no longer in the "
                                 "list (351b:185f)");
            break;
        }
        game->vm.character = lost->owner;
        bool failed;
        ok = cok_treasure_take_item(game, lost->item, &failed);
    }
    free(game->lost_weapons);
    game->lost_weapons = NULL;
    game->lost_weapon_count = 0;
    return ok;
}

/* 351b:0037: the experience each character gets, and the loot. Every
 * defeated enemy (+0x18a 1, status neither 0 nor 3) is counted
 * (DS:43c4) and gives +0x130 + +0x132 * +0x11b, the product a signed
 * word; a monster (past the party) puts its coins in the pool, and unless
 * monsters keep their items (0x7ee3 1) each enemy's items are named and
 * copied first into the pool, unreadied. Then the pool's coins count (1/100
 * of silver, 1/20 copper, 1/10 bronze, platinum, 2 steel, 250 gems and
 * 2200 jewelry), and 400 times the bonus of each magic item before the
 * last missile recovered (DS:60a2), as a signed word; the sum, a LongInt,
 * is divided by 0x7f3e less those out, a word. A divisor of 0 is runtime
 * error 200. */
static bool experience(cok_adventure *game, outcome *result)
{
    cok_ecl *vm = &game->vm;
    cok_pool *pool = &game->pool;
    uint32_t sum = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (r[0x18a] != 1 || r[0x188] == 0 || r[0x188] == 3) continue;
        result->monsters = true;
        if (past_party(c))
            for (int k = 0; k < COK_COINS; ++k) pool->coins[k] += word(r + 0xeb + 2 * k);
        sum += word(r + 0x130) + extend((uint16_t)(r[0x11b] * r[0x132]));
        if (vm->mem7c00[0x2e3] == 1) continue;
        for (size_t k = 0; k < c->item_count; ++k) {
            char name[41];
            if (!cok_item_name(game, c->items[k], false, name)) return false;
            uint8_t copy[COK_ITEM_SIZE];
            memcpy(copy, c->items[k], sizeof copy);
            copy[0x34] = 0;
            if (!cok_pool_insert(pool, 0, copy)) return undefined(game, "out of memory");
        }
    }
    const uint32_t *coins = pool->coins;
    sum += (uint32_t)((int32_t)coins[0] / 100) + (uint32_t)((int32_t)coins[1] / 20) +
           (uint32_t)((int32_t)coins[2] / 10) + coins[3] + coins[4] * 2u + coins[5] * 250u +
           coins[6] * 2200u;
    for (size_t i = 0; i < pool->item_count && i + 1 != pool->missile; ++i) {
        int8_t bonus = (int8_t)pool->items[i][0x32];
        if (bonus > 0) sum += extend((uint16_t)(bonus * 400));
    }
    uint16_t divisor = (uint16_t)(vm->mem7c00[0x33e] - result->out);
    if (divisor == 0) {
        cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                           "the experience is divided by the party less those out, 0 "
                           "(351b:0037)");
        return false;
    }
    game->experience = (int32_t)sum / divisor;
    game->experience_known = true;
    return true;
}

/* 351b:0379: unless the bank is paying out (0x4cf5), every record in the
 * list that can act and is not animated (status 1), monsters too, gets
 * the experience: 10% more for a cleric of wisdom above 14, or a fighter,
 * paladin, ranger, mage or thief whose prime requisites are above 15; half
 * for two classes, a third for three. */
static void award(cok_adventure *game)
{
    if (game->vm.mem4b00[0x1f5] != 0) return;
    int32_t xp = game->experience;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *r = game->party.members[i]->record;
        if (r[0x189] == 0 || r[0x188] == 1) continue;
        uint8_t str = r[0x11], intel = r[0x13], wis = r[0x15], dex = r[0x17], class_ = r[0x5b];
        bool bonus = (class_ == 0 && wis > 14) || (class_ == 2 && str > 15) ||
                     (class_ == 3 && str > 15 && wis > 15) ||
                     (class_ == 4 && str > 15 && intel > 15 && wis > 15) ||
                     (class_ == 5 && intel > 15) || (class_ == 6 && dex > 15);
        int32_t gain = xp;
        if (bonus)
            gain = (int32_t)((uint32_t)(xp / 10) + (uint32_t)xp);
        else if (class_ == 8 || (class_ >= 10 && class_ <= 14) || class_ == 0x10)
            gain = xp / 2;
        else if (class_ == 9 || class_ == 0x0f)
            gain = xp / 3;
        uint32_t total = (uint32_t)r[0x116] | (uint32_t)r[0x117] << 8 | (uint32_t)r[0x118] << 16 |
                         (uint32_t)r[0x119] << 24;
        total += (uint32_t)gain;
        for (int b = 0; b < 4; ++b) r[0x116 + b] = (uint8_t)(total >> (8 * b));
    }
}

/* 351b:0574: the party after combat. It is destroyed unless a party
 * record (those before the first past the party's size) has status 0, 1
 * or 3, is on the party's side and is not an NPC; or, in a fight that
 * cannot kill (0x7ee6), anyone in the list can act or is running,
 * unconscious or dying. Each party record loses the effects that last
 * only through combat (DS:0390: 0x03, 0x0b, 0x15, 0x17, 0x1b, 0x23, 0x28,
 * 0x33, 0x34, 0x35 and 0x1f, the first of each); if one stands, the
 * experience is worked out and given. A party that lives either comes
 * round (running and unconscious with hit points to okay, dying to
 * unconscious, or in a fight that cannot kill to okay with a hit point),
 * or if some fled and none stands (0x7ec7 0x81), those who fled come back
 * and the rest are left behind, removed; a destroyed party is removed and
 * 0x7f3e is 0. Returns false where it cannot be carried out. */
static bool after_combat(cok_adventure *game, outcome *result)
{
    cok_ecl *vm = &game->vm;
    cok_party *party = &game->party;
    bool gentle = vm->mem7c00[0x2e6] != 0;
    result->out = 0;
    result->destroyed = true;
    result->fled = false;
    for (size_t i = 0; i < party->count && !past_party(party->members[i]); ++i)
        if (party->members[i]->record[0x188] == 3) result->fled = true;
    bool found = false;
    for (size_t i = 0; i < party->count && !found; ++i) {
        const uint8_t *r = party->members[i]->record;
        found = r[0x189] != 0 || r[0x188] == 4 || r[0x188] == 3 || r[0x188] == 5;
    }
    if (gentle && found) result->destroyed = false;
    result->standing = false;
    static const uint8_t combat_only[11] = {0x03, 0x0b, 0x15, 0x17, 0x1b, 0x23,
                                            0x28, 0x33, 0x34, 0x35, 0x1f}; /* DS:0390 */
    for (size_t i = 0; i < party->count && !past_party(party->members[i]); ++i) {
        cok_character *c = party->members[i];
        uint8_t *r = c->record, status = r[0x188];
        if ((status == 3 || status == 1 || status == 0) && r[0x18a] == 0 && r[0xe7] < 0x80)
            result->destroyed = false;
        if (status == 1 || status == 0) {
            result->standing = true;
            result->fled = false;
        }
        if (r[0x189] == 0 || status == 1) ++result->out;
        for (size_t k = 0; k < sizeof combat_only; ++k) {
            if (cok_effects_remove(&game->effects, c, NULL, combat_only[k])) continue;
            cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", game->effects.error);
            return false;
        }
    }
    if (result->standing) {
        if (!experience(game, result)) return false;
        award(game);
    }
    if (result->destroyed) {
        while (party->count > 0 && !past_party(party->members[0]))
            if (!remove_record(game, 0, false, true)) return false;
        vm->mem7c00[0x33e] = 0;
        return true;
    }
    for (size_t i = 0; i < party->count && !past_party(party->members[i]);) {
        uint8_t *r = party->members[i]->record;
        if (result->fled) {
            vm->mem7c00[0x2c7] = 0x81;
            if (r[0x188] != 3) {
                if (!remove_record(game, i, false, false)) return false;
                continue;
            }
            r[0x188] = 0;
            r[0x189] = 1;
        } else if (r[0x188] == 3 || (r[0x188] == 4 && r[0x197] != 0)) {
            r[0x188] = 0;
            r[0x189] = 1;
        } else if ((r[0x188] == 5 || r[0x188] == 4) && gentle) {
            r[0x188] = 0;
            r[0x189] = 1;
            r[0x197] = 1;
        } else if (r[0x188] == 5) {
            r[0x188] = 4;
        }
        ++i;
    }
    return true;
}

/* 351b:1493: remove the enemies (DS:43c4), counting those that dropped
 * (0x7ec8) and whether the first did (0x4cf8, only ever set), and free the
 * others' combat records; the first record is then selected. */
static bool remove_enemies(cok_adventure *game, outcome *result)
{
    cok_ecl *vm = &game->vm;
    bool first = true;
    vm->mem7c00[0x2c8] = 0;
    char text[200] = "", name[16] = "", last[16] = "";
    unsigned same = 0;
    for (size_t i = 0; i < game->party.count;) {
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (!past_party(c) && r[0x18a] != 1) {
            free(c->combat);
            c->combat = NULL;
            ++i;
            continue;
        }
        result->monsters = true;
        if (r[0x189] != 1) {
            ++vm->mem7c00[0x2c8];
            if (first) vm->mem4b00[0x1f8] = 1;
        }
        if (r[0x188] == 3 && vm->mem7c00[0x2c7] == 0) vm->mem7c00[0x2c7] = 1;
        first = false;
        name_of(r, name);
        if (same > 0 && strcmp(name, last) != 0) {
            size_t used = strlen(text);
            snprintf(text + used, sizeof text - used, "%s%u %s", used > 0 ? ", " : "", same, last);
            same = 0;
        }
        snprintf(last, sizeof last, "%s", name);
        ++same;
        if (!remove_record(game, i, past_party(c), result->destroyed)) return false;
    }
    if (same > 0) {
        size_t used = strlen(text);
        snprintf(text + used, sizeof text - used, "%s%u %s", used > 0 ? ", " : "", same, last);
    }
    vm->character = cok_party_record(&game->party, 0);
    if (text[0] != '\0') {
        char line[sizeof text + 32];
        snprintf(line, sizeof line, "removed %s; %u dropped", text, vm->mem7c00[0x2c8]);
        log_text(game, "combat", line);
    }
    return true;
}

/* The screens. */

static void draw(cok_adventure *game, const char *text, int x, int y, uint8_t fg)
{
    log_text(game, "print", text);
    cok_text_string(&game->screen, &game->font, text, x, y, fg, 0); /* 1521:0353 */
}

/* Clear the frame (1128:0000), open or with the row 16 divider. */
static void clear_frame(cok_adventure *game, bool open)
{
    cok_ecl *vm = &game->vm;
    uint16_t phase[3] = {vm->mem4b00[0x1f9], vm->mem4b00[0x1fa], vm->mem4b00[0x1fb]};
    cok_screen_frame(&game->screen, &game->view.tiles[4], phase, open);
}

/* "press <enter>/<return> to continue" on row 24, the whole line its one
 * item, white (67b5:03e2 with colours 15, 15 and 15): Enter, space,
 * Escape and any special key end it. Returns false if input ended. */
static bool press_enter(cok_adventure *game)
{
    static const char *const text = "press <enter>/<return> to continue";
    log_text(game, "menu", text);
    cok_keyboard keys = cok_adventure_keyboard(game);
    bool special;
    return cok_menu_ask(&game->screen, &game->font, "", text, 15, 15, 15, true, &game->selected,
                        &keys, &special) >= 0;
}

/* 351b:1618: the NPCs take their shares. Each NPC (+0xe7 0x80 and up)
 * that is okay counts +0xe8 & 7 shares, every other record one, as bytes;
 * with any NPC shares, of each coin in the pool the NPCs take their
 * shares of the pool divided by all the shares, cut to a byte, and the
 * coins are gone. Then each okay NPC with +0xe8 set "takes and hides his
 * share." (or her, +0x109), two rows apart from row 5. A share count that
 * wraps to 0 divides by zero. */
static bool shares(cok_adventure *game)
{
    cok_party *party = &game->party;
    uint8_t npc = 0, total = 0;
    for (size_t i = 0; i < party->count; ++i) {
        const uint8_t *r = party->members[i]->record;
        if (r[0xe7] > 0x7f && r[0x188] == 0) {
            npc = (uint8_t)(npc + (r[0xe8] & 7));
            total = (uint8_t)(total + (r[0xe8] & 7));
        } else {
            ++total;
        }
    }
    if (npc == 0) return true;
    bool taken = false;
    for (int k = 0; k < COK_COINS; ++k) {
        if ((int32_t)game->pool.coins[k] <= 0) continue;
        if (total == 0) {
            cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                               "the NPCs' shares divide by shares that wrap to 0 (351b:1618)");
            return false;
        }
        uint8_t per = (uint8_t)((int32_t)game->pool.coins[k] / total);
        game->pool.coins[k] -= extend((uint16_t)(per * npc));
        taken = true;
    }
    if (!taken) return true;
    clear_frame(game, true);
    int row = 5;
    for (size_t i = 0; i < party->count; ++i) {
        const uint8_t *r = party->members[i]->record;
        if (r[0xe7] <= 0x7f || r[0x188] != 0 || r[0xe8] == 0) continue;
        char name[16], text[64];
        name_of(r, name);
        snprintf(text, sizeof text, "%s takes and hides %s share.", name,
                 r[0x109] == 0 ? "his" : "her");
        cok_adventure_print(game, text, (cok_text_window){5, row, 0x22, 0x16}, 10, true);
        row += 2;
    }
    return press_enter(game);
}

/* 351b:0b23: the results, in light green from column 1 of a cleared frame:
 * after a fight (an enemy was defeated or removed) "The party has fled."
 * with no experience and the pool's coins gone; in a fight that cannot
 * kill with none standing, "You have lost the fight." with none (0x7ec7
 * 0x80); else "The party has won."; without one, the bank's withdrawal
 * (0x4cf5) or "The party has found Treasure!". Then, but for the bank,
 * the experience each character receives. */
static bool results(cok_adventure *game, const outcome *result)
{
    cok_ecl *vm = &game->vm;
    int32_t xp = game->experience;
    bool shown = game->experience_known;
    clear_frame(game, true);
    if (result->monsters) {
        if (result->fled) {
            draw(game, "The party has fled.", 1, 3, 10);
            xp = 0;
            shown = true;
            cok_pool_free_items(&game->pool);
            memset(game->pool.coins, 0, sizeof game->pool.coins);
        } else if (!result->standing && vm->mem7c00[0x2e6] != 0) {
            vm->mem7c00[0x2c7] = 0x80;
            draw(game, "You have lost the fight.", 1, 3, 10);
            xp = 0;
            shown = true;
        } else {
            draw(game, "The party has won.", 1, 3, 10);
        }
    } else if (vm->mem4b00[0x1f5] != 0) {
        draw(game, "The party makes a withdrawl.  A small", 1, 5, 10);
        draw(game, "fee has been assessed by the bank.", 1, 7, 10);
    } else {
        draw(game, "The party has found Treasure!", 1, 3, 10);
    }
    if (vm->mem4b00[0x1f5] == 0) {
        if (!shown)
            return undefined(game, "the results show the experience of a combat before "
                                   "(DS:8840), and none was worked out");
        char text[48];
        snprintf(text, sizeof text, "Each character receives %ld", (long)xp);
        draw(game, text, 1, 5, 10);
        draw(game, "experience points.", 1, 7, 10);
    }
    return press_enter(game);
}

bool cok_pool_take_money(cok_adventure *game)
{
    cok_pool *pool = &game->pool;
    clear_frame(game, false);
    int index = 0;
    for (;;) {
        static char text[COK_COINS][41];
        cok_menu_row rows[COK_COINS];
        int kinds[COK_COINS];
        size_t count = 0;
        for (int k = COK_COINS - 1; k >= 0; --k) {
            if ((int32_t)pool->coins[k] <= 0) continue;
            snprintf(text[count], sizeof text[count], "%s %ld", coin_names[k],
                     (long)(int32_t)pool->coins[k]);
            rows[count] = (cok_menu_row){text[count], false};
            kinds[count++] = k;
        }
        for (size_t i = 0; i < count; ++i) log_text(game, "item", text[i]);
        cok_menu_style style = {"Select type of coin ", "Select", 15, 10, 13, true};
        bool redraw = true;
        cok_keyboard keys = cok_adventure_keyboard(game);
        int key = cok_menu_rows(&game->screen, &game->font, rows, count,
                                (cok_text_window){2, 2, 0x0f, 8}, &style, &redraw, &index,
                                &game->list_top, &game->selected, &keys);
        if (key < 0) return false;
        if (key == 0 || count == 0) return true;
        int k = kinds[index];
        char choice[2] = {text[index][0], '\0'};
        log_text(game, "choice", choice);
        char prompt[64];
        /* The words end in a space and the question starts with one. */
        snprintf(prompt, sizeof prompt, "%s%s will you take? ", k == 5 ? "How Many " : "How much ",
                 coin_words[k]);
        int amount = cok_sheet_amount(game, prompt, (uint16_t)pool->coins[k]);
        if (amount < 0) return false;
        cok_character *c = selected(game);
        if (c == NULL)
            return undefined(game, "taking money with no character selected reads through NULL "
                                   "(58e7:0a81)");
        if (!cok_pool_take(game, c->record, (uint16_t)amount, k)) return false;
        bool money = false;
        for (int j = 0; j < COK_COINS; ++j)
            if ((int32_t)pool->coins[j] > 0) money = true;
        if (!money) return true;
    }
}

/* Take items (351b:0ec5, 0df1): in the open frame, the pool's items, each
 * named first (6346:0488), in cells 1-38 by 1-22 with "Items: " and
 * "Take"; the item picked goes to the selected character (36d0:034c) and
 * leaves the pool, unless it would be overloaded. Again until the pool is
 * empty or none is picked; then the screen is redrawn. */
static bool take_items(cok_adventure *game)
{
    cok_pool *pool = &game->pool;
    int index = 0;
    char (*text)[41] = NULL;
    cok_menu_row *rows = NULL;
    bool ok = true;
    while (ok && pool->item_count > 0) {
        clear_frame(game, true);
        size_t count = pool->item_count;
        char (*names)[41] = realloc(text, count * sizeof *text);
        if (names != NULL) text = names;
        cok_menu_row *more = names != NULL ? realloc(rows, count * sizeof *rows) : NULL;
        if (more == NULL) {
            ok = undefined(game, "out of memory");
            break;
        }
        rows = more;
        for (size_t i = 0; i < count && ok; ++i) ok = cok_item_name(game, pool->items[i], false, text[i]);
        if (!ok) break;
        /* The list shows at most 255 rows (cok_menu_rows). */
        for (size_t i = 0; i < count; ++i) {
            rows[i] = (cok_menu_row){text[i], pool->items[i][0x29] != 0};
            if (i < 255) log_text(game, rows[i].heading ? "heading" : "item", text[i]);
        }
        cok_menu_style style = {"Items: ", "Take", 15, 10, 13, true};
        bool redraw = true;
        cok_keyboard keys = cok_adventure_keyboard(game);
        int key = cok_menu_rows(&game->screen, &game->font, rows, count,
                                (cok_text_window){1, 1, 0x26, 0x16}, &style, &redraw, &index,
                                &game->list_top, &game->selected, &keys);
        if (key < 0) {
            ok = false;
            break;
        }
        if (key != 'T' && key != 0x0d) break;
        if (index < 0 || (size_t)index >= pool->item_count) {
            ok = undefined(game, "Take picked no item (351b:0ec5)");
            break;
        }
        log_text(game, "choice", text[index]);
        bool failed;
        ok = cok_treasure_take_item(game, pool->items[index], &failed);
        if (ok && !failed) cok_pool_remove(pool, (size_t)index);
    }
    free(text);
    free(rows);
    if (ok) cok_adventure_redraw(game);
    return ok;
}

/* Take (351b:0ff7): items alone, money alone, or "Take: " with "Money
 * Items Exit" until either runs out or Exit or Escape. The menu takes
 * special keys by their scan codes' letters: up and down (H, P) pick a
 * character, PgUp (I) takes items and NumLock (E) leaves. *reached is
 * set when it took coins or items, or offered them. */
static bool take(cok_adventure *game, bool *reached)
{
    bool money, items;
    cok_pool_status(&game->pool, &money, &items);
    *reached = true;
    if (!money) return take_items(game);
    if (!items) {
        if (!cok_pool_take_money(game)) return false;
        cok_adventure_redraw(game);
        return true;
    }
    *reached = false;
    for (;;) {
        bool special;
        int key = cok_camp_menu(game, "Take: ", "Money Items Exit", true, true, &special);
        if (key < 0) return false;
        bool done = false;
        if (key == 'M') {
            *reached = true;
            if (!cok_pool_take_money(game)) return false;
            cok_adventure_redraw(game);
        } else if (key == 'I') {
            *reached = true;
            if (!take_items(game)) return false;
        } else if (key == 'E' || key == 0) {
            done = true;
        } else if (key == 'H' || key == 'P') {
            game->vm.character = cok_party_special(&game->party, game->vm.character,
                                                   (uint8_t)key); /* 546c:3334 */
        }
        if (game->vm.abort) return false;
        cok_adventure_party(game);
        cok_pool_status(&game->pool, &money, &items);
        if (done || !money || !items) return true;
    }
}

/* The Detect spell the selected character has memorized, if it can act:
 * the first byte of +0x1e-+0x57 that is 5, 0x0b, 0x4d or 0x67. */
static uint8_t detect_spell(cok_adventure *game)
{
    const uint8_t *c = game->vm.character;
    if (c == NULL || cok_party_index(&game->party, c) == game->party.count) return 0;
    for (size_t i = 0; i < 0x3a; ++i) {
        uint8_t b = c[0x1e + i];
        if ((b == 5 || b == 0x0b || b == 0x4d || b == 0x67) && c[0x189] != 0) return b;
    }
    return 0;
}

/* The treasure menu (351b:118c), after the screen is redrawn for mode 6:
 * "View Take Pool Share" with money in the pool, "View Take Pool" with
 * only items, else "View Pool", then "Detect" if there are items and the
 * selected character has a Detect spell memorized and can act, and
 * "Exit". View shows the selected character, Take takes (351b:0ff7), Pool
 * pools the party's money (58e7:0511), Share shares it (58e7:063a) and
 * Detect casts the spell, without "casts" (5b04:1415). Exit or Escape
 * leaves when the pool is empty; else "There is still treasure left." and
 * "Do you want to go back and claim your treasure?" with "~Yes ~No", No
 * leaving. Special keys pick a character. */
static bool treasure_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t picture = game->picture_id;
    cok_adventure_redraw(game);
    /* Loading the treasure's picture zero-fills the stack where View's
     * Trade later finds its flag (see sheet.h), as the camp's does; kept
     * from before, the byte is whatever was there. A View, or Yes to go
     * back, leaves it set; Take, through the coins or the items, leaves
     * the low byte of a segment there; Pool, Share, Detect and picking a
     * character leave it as it was. */
    cok_sheet_stale stale = picture != 0x3c && game->picture_id == 0x3c ? COK_SHEET_STALE_ZERO
                                                                       : COK_SHEET_STALE_UNKNOWN;
    for (;;) {
        bool money, items;
        cok_pool_status(&game->pool, &money, &items);
        uint8_t spell = items ? detect_spell(game) : 0;
        const char *tail = spell != 0 ? " Detect Exit" : " Exit";
        char menu[48];
        if (money)
            snprintf(menu, sizeof menu, "View Take Pool Share%s", tail);
        else if (items)
            snprintf(menu, sizeof menu, "View Take Pool%s", tail);
        else
            snprintf(menu, sizeof menu, "View Pool Exit");
        bool special;
        int key = cok_camp_menu(game, "", menu, true, true, &special);
        if (key < 0) return false;
        if (special) {
            cok_camp_pick(game, (uint8_t)key);
            continue;
        }
        bool done = false;
        switch (key) {
        case 'V': {
            bool used;
            cok_sheet(game, stale, &used);
            stale = COK_SHEET_STALE_SET;
            break;
        }
        case 'T': {
            bool reached;
            if (!take(game, &reached)) return false;
            if (reached) stale = COK_SHEET_STALE_UNKNOWN;
            break;
        }
        case 'P': cok_pool_gather(game); break;
        case 'S':
            if (!cok_pool_share(game)) return false;
            break;
        case 'D': {
            bool used = false;
            /* The frame only matters to Strength's roll (see cast.h). */
            cok_cast_spell(game, spell, false, 0, &used);
            break;
        }
        case 'E': case 0:
            cok_pool_status(&game->pool, &money, &items);
            if (!money && !items) {
                done = true;
                break;
            }
            vm->cursor = (cok_text_cursor){1, 0x11};
            cok_adventure_print(game, "There is still treasure left.  ",
                                (cok_text_window){1, 0x11, 0x26, 0x16}, 10, true);
            cok_adventure_print(game, "Do you want to go back and claim your treasure?",
                                (cok_text_window){1, 0x11, 0x26, 0x16}, 15, false);
            int answer = cok_adventure_horizontal(game, "", "~Yes ~No", 10, false);
            if (answer < 0) return false;
            if (answer == 1) {
                done = true;
            } else {
                cok_picture_fill(&game->screen, 1, 0x11 * 8, 0x26, 6 * 8, 0); /* 1521:0b60 */
                stale = COK_SHEET_STALE_SET;
            }
            break;
        default: break;
        }
        if (vm->abort || vm->status != COK_ECL_OK) return false;
        if (done) return true;
    }
}

void cok_treasure_end_of_combat(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (!weapons_back(game)) return;
    vm->mem7c00[0x2c7] = 0;
    outcome result = {0};
    if (!after_combat(game, &result)) return;
    vm->mode = 6;
    if (!remove_enemies(game, &result)) return;
    for (size_t i = 0; i < game->party.count; ++i) {
        if (cok_character_stats(game->party.members[i], &game->item_types, game->error,
                                sizeof game->error))
            continue;
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", game->error);
        return;
    }
    if (!result.destroyed) {
        if (result.fled) cok_pool_free_items(&game->pool);
        if (!shares(game) || !results(game, &result) || !treasure_menu(game)) return;
        cok_pool_free_items(&game->pool);
    } else {
        vm->mem7c00[0x2c7] = 0x80;
        clear_frame(game, true);
        /* 351b:1aab: in cells 2-0x25 by 5-0x16, cleared, which puts the
         * cursor at 2, 5. */
        vm->cursor = (cok_text_cursor){2, 5};
        cok_adventure_print(game, "The monsters rejoice for the party has been destroyed",
                            (cok_text_window){2, 5, 0x25, 0x16}, 10, true);
        cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
        cok_text_string(&game->screen, &game->font, "Press any key to continue", 0, 24, 13, 0);
        cok_keyboard keys = cok_adventure_keyboard(game);
        keys.read(keys.context);
        game->party_killed = true;
        vm->abort = true;
    }
    static const uint16_t cleared[] = {0x370, 0x371, 0x372, 0x2e3, 0x2e6};
    for (size_t i = 0; i < sizeof cleared / sizeof *cleared; ++i) vm->mem7c00[cleared[i]] = 0;
    vm->mem4b00[0x1f5] = 0;
}
