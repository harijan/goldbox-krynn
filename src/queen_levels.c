#include "queen_levels.h"

#include "data.h"
#include "ecl.h"

#include <stdio.h>
#include <string.h>

/* The record's bytes by the places dark_queen_character names (the
 * levels +0xa5, the levels before a change of class +0xac, the spells a
 * day by class and level from +0x166, the known spells' bits +0x157), as
 * the routines read them. */
#define LEVEL(c, k) ((c)[0xa5 + (k)])
#define FORMER(c, k) ((c)[0xac + (k)])
#define SLOT(c, row, l) ((c)[0x166 + 9 * (row) + (l)])

static uint8_t ds(const cok_rules *rules, long offset)
{
    return cok_data_held(rules->data, offset);
}

static uint32_t ds32(const cok_rules *rules, long offset)
{
    return (uint32_t)ds(rules, offset) | (uint32_t)ds(rules, offset + 1) << 8 |
           (uint32_t)ds(rules, offset + 2) << 16 | (uint32_t)ds(rules, offset + 3) << 24;
}

static void fail(char *error, size_t size, const char *what)
{
    if (error != NULL && size > 0) snprintf(error, size, "%s", what);
}

/* A spell known (the bit of id - 1, DS:68bc's). */
static void know(uint8_t *c, unsigned id)
{
    c[0x157 + (id - 1) / 8] |= (uint8_t)(1u << ((id - 1) % 8));
}

int8_t cok_queen_constitution(const cok_rules *rules, const uint8_t *c, unsigned class_)
{
    (void)rules;
    uint8_t con = c[0x81];
    if (con < 3 || con > 19) return 0; /* unset in the original */
    if (con == 3) return -2;
    if (con <= 6) return -1;
    if (con <= 14) return 0;
    if (con == 15) return 1;
    if (con == 16) return 2;
    return class_ >= 1 && class_ <= 4 ? (int8_t)(con - 14) : 2;
}

uint8_t cok_queen_most_hit_points(const cok_rules *rules, const uint8_t *c)
{
    unsigned counted = 0;
    uint16_t sum = 0;
    for (unsigned k = 0; k < 7; ++k) {
        uint8_t level = LEVEL(c, k), former = FORMER(c, k), top = ds(rules, 0x640a + (long)k);
        if (!((level > 0 && level > c[0x92]) || former > 0)) continue;
        int con = cok_queen_constitution(rules, c, k);
        uint8_t used;
        if (former > 0) {
            used = former;
        } else {
            ++counted;
            used = level;
        }
        if (top > used) {
            /* Below the dice's top: a level's dice most, a cleric of
             * Paladine, Majere, Kiri-Jolith or Mishakal's and a knight's
             * one level more, a ranger's one hit point. */
            static const uint8_t dice[7] = {8, 10, 8, 10, 8, 4, 6};
            unsigned n = level;
            if (k == 0 && (int8_t)c[0x19b] <= 4) ++n;
            if (k == 1) ++n;
            sum = (uint16_t)(sum + n * (unsigned)(con + dice[k]));
            if (k == 4) ++sum;
            continue;
        }
        /* The top and past it: the dice's most and so much a level. */
        unsigned extra = (unsigned)(uint8_t)(level - top + 1);
        static const struct { uint8_t con, extra, base; } past[7] = {
            {9, 2, 0x48}, {10, 2, 0x64}, {9, 3, 0x5a}, {9, 3, 0x5a},
            {11, 2, 0x58}, {11, 1, 0x2c}, {10, 2, 0x3c},
        };
        sum = (uint16_t)(sum + con * past[k].con + (int)extra * past[k].extra + past[k].base);
    }
    if (counted != 0)
        sum = (uint16_t)(sum / counted);
    else if (c[0x92] == 0)
        sum = 0;
    return (uint8_t)sum;
}

int cok_queen_experience_row(const cok_rules *rules, const uint8_t *c, unsigned class_)
{
    (void)rules;
    switch (class_) {
    case 0: return (int8_t)c[0x19b] <= 4 ? 0 : 1;
    case 1:
        return c[0x19a] == 1 ? 3 : c[0x19a] == 2 ? 4 : c[0x19a] == 3 ? 5 : -1;
    case 2: return 2;
    case 3: return 10;
    case 4: return 6;
    case 5: return c[0x19d] == 1 ? 7 : 8;
    case 6: return 9;
    default: return -1;
    }
}

bool cok_queen_experience(const cok_rules *rules, const uint8_t *c, unsigned level,
                          unsigned class_, int32_t *experience, char *error, size_t error_size)
{
    int row = cok_queen_experience_row(rules, c, class_);
    if (row < 0) {
        fail(error, error_size,
             "a knight of no order (or a class past 6) has no row of experience: 177e:1f39 "
             "leaves it unset");
        return false;
    }
    level &= 0xff;
    uint32_t value = 0x7fffffffu;
    if (level < 0x29) {
        if (level < 0x12)
            value = ds32(rules, 0x852 + row * 0x4c + (long)level * 4);
        else
            value = (uint32_t)(level - 0x11) * ds32(rules, 0x28c + row * 4L) +
                    ds32(rules, 0x852 + row * 0x4c + 0x44);
    }
    *experience = (int32_t)value;
    return true;
}

bool cok_queen_may_train(const cok_rules *rules, const uint8_t *c, bool *may, char *error,
                         size_t error_size)
{
    int32_t experience = (int32_t)((uint32_t)c[0x44] | (uint32_t)c[0x45] << 8 |
                                   (uint32_t)c[0x46] << 16 | (uint32_t)c[0x47] << 24);
    unsigned race = (unsigned)(c[0x58] | c[0x59] << 8);
    *may = false;
    for (unsigned k = 0; k <= 6 && !*may; ++k) {
        uint8_t level = LEVEL(c, k);
        if (level == 0 || level >= 0x28) continue;
        int32_t need;
        if (!cok_queen_experience(rules, c, (unsigned)level + 1, k, &need, error, error_size))
            return false;
        bool skip = false;
        if (!(k == 1 && c[0x19a] == 3 && level < 0x28))
            skip = ds(rules, 0x17dc + (long)race * 7 + k) <= level;
        if (experience >= need && need > 0 && !skip) *may = true;
    }
    return true;
}

void cok_queen_cleric_spells(const cok_rules *rules, uint8_t *c)
{
    uint8_t level = LEVEL(c, 0);
    if (level == 0) return;
    unsigned g = (int8_t)c[0x19b] > 4;
    for (unsigned l = 2; l < 8; ++l) SLOT(c, 0, l) = 0;
    SLOT(c, 0, 1) = g == 0 ? 1 : 2;
    if (level > 0x1d) level = 0x1d;
    for (unsigned l = 1; l < 8; ++l) {
        uint8_t table = ds(rules, 0x847 + (long)g);
        if (table == 0xff)
            SLOT(c, 0, l) = 0;
        else
            SLOT(c, 0, l) = ds(rules, 0xb95 + table * 0x105L + (level - 1) * 9L + (long)l);
    }
    /* A wisdom of 13 and up adds first-level spells, 15 and up second's,
     * 17 third's, 18 fourth's; below 17 and 18 there are none of the sixth
     * and seventh. */
    uint8_t wis = c[0x7d];
    if (wis > 0xc && SLOT(c, 0, 1) != 0) ++SLOT(c, 0, 1);
    if (wis > 0xd && SLOT(c, 0, 1) != 0) ++SLOT(c, 0, 1);
    if (wis > 0xe && SLOT(c, 0, 2) != 0) ++SLOT(c, 0, 2);
    if (wis > 0xf && SLOT(c, 0, 2) != 0) ++SLOT(c, 0, 2);
    if (wis > 0x10 && SLOT(c, 0, 3) != 0) ++SLOT(c, 0, 3);
    if (wis > 0x11 && SLOT(c, 0, 4) != 0) ++SLOT(c, 0, 4);
    if (wis < 0x11) SLOT(c, 0, 6) = 0;
    if (wis < 0x12) SLOT(c, 0, 7) = 0;
}

/* 8bcc:01a2: a mage's intelligence under 12, 14, 16 or 18 takes away the
 * spells a day of the sixth, seventh, eighth and ninth level. */
static void mage_intelligence(uint8_t *c)
{
    if (c[0x7b] < 0xc) SLOT(c, 2, 6) = 0;
    if (c[0x7b] < 0xe) SLOT(c, 2, 7) = 0;
    if (c[0x7b] < 0x10) SLOT(c, 2, 8) = 0;
    if (c[0x7b] < 0x12) SLOT(c, 2, 9) = 0;
}

/* The spells of the cleric's lists (DS:16ce, ten a level to the first 0)
 * of each level with spells a day in row 0. */
static void know_lists(const cok_rules *rules, uint8_t *c, unsigned row, unsigned from,
                       unsigned to)
{
    for (unsigned l = from; l <= to; ++l) {
        if (SLOT(c, row, l) == 0) continue;
        for (unsigned i = 1; i <= 10; ++i) {
            uint8_t id = ds(rules, 0x16ce + (long)(l - 1) * 10 + (long)i - 1);
            if (id == 0) break;
            know(c, id);
        }
    }
}

/* The spells a day a class's table gives at level for spell level l
 * (DS:0xb96 + 0x105 * table + 9 * (level - 1) + l - 1). */
static uint8_t per_day(const cok_rules *rules, unsigned table, unsigned level, unsigned l)
{
    return ds(rules, 0xb95 + (long)table * 0x105 + (long)(level - 1) * 9 + (long)l);
}

/* Adds a level's spells a day of a table (an index byte of DS:0847) to a
 * slot of the character's row. */
static void add_per_day(const cok_rules *rules, uint8_t *slot, uint8_t index, unsigned level,
                        unsigned l)
{
    *slot = (uint8_t)(*slot + per_day(rules, index, level, l));
}

/* The spell table's entry of spell id (DS:27e9 + 0x16 * id): its kind, a
 * word, and its level. */
static void spell_entry(const cok_rules *rules, unsigned id, unsigned *kind, unsigned *level)
{
    long at = 0x27e9 + (long)id * 0x16;
    *kind = (unsigned)(ds(rules, at) | ds(rules, at + 1) << 8);
    *level = ds(rules, at + 2);
}

void cok_queen_spells(const cok_character *character, const cok_rules *rules)
{
    uint8_t *c = (uint8_t *)character->record;
    const cok_rules_item_record *I = &rules->item_record;
    memset(c + 0x167, 0, 0x24);
    for (unsigned k = 0; k < 6; ++k) {
        uint8_t level = LEVEL(c, k);
        if (level > 0x1d) level = 0x1d;
        if (level == 0) continue;
        switch (k) {
        case 0: { /* a cleric: 8bcc:000f again over the first sums */
            unsigned g = (int8_t)c[0x19b] > 4;
            ++SLOT(c, 0, 1);
            if (ds(rules, 0x847 + (long)g) != 0xff)
                for (unsigned l = 1; l < 8; ++l)
                    add_per_day(rules, &SLOT(c, 0, l), ds(rules, 0x847 + (long)g), level, l);
            cok_queen_cleric_spells(rules, c);
            know_lists(rules, c, 0, 1, 7);
            break;
        }
        case 1: { /* a knight past the fifth level, of the Sword or the Rose */
            if (level <= 5 || c[0x19a] == 1) break;
            unsigned g = c[0x19a] == 2 ? 4 : 5;
            if (ds(rules, 0x847 + (long)g) != 0xff)
                for (unsigned l = 1; l < 8; ++l)
                    add_per_day(rules, &SLOT(c, 0, l), ds(rules, 0x847 + (long)g), level, l);
            know_lists(rules, c, 0, 1, 7);
            break;
        }
        case 3: /* a paladin past the eighth */
            if (level <= 8) break;
            for (unsigned l = 1; l < 5; ++l)
                add_per_day(rules, &SLOT(c, 0, l), ds(rules, 0x851), level, l);
            for (unsigned id = 1; id <= 0x7e; ++id) {
                unsigned kind, sl;
                spell_entry(rules, id, &kind, &sl);
                if (kind == 0 && SLOT(c, 0, sl) > 0) know(c, id);
            }
            break;
        case 4: /* a ranger past the seventh: the first four levels of row 1, the next of row 2 */
            if (level > 7) {
                for (unsigned l = 1; l < 5; ++l)
                    add_per_day(rules, &SLOT(c, 1, l), ds(rules, 0x84d), level, l);
                for (unsigned l = 5; l < 9; ++l)
                    add_per_day(rules, &SLOT(c, 2, l - 4), ds(rules, 0x84d), level, l);
            }
            for (unsigned id = 1; id <= 0x7e; ++id) {
                unsigned kind, sl;
                spell_entry(rules, id, &kind, &sl);
                if (((kind == 1 && SLOT(c, 1, sl) > 0) || (kind == 2 && SLOT(c, 2, sl) > 0)) &&
                    SLOT(c, 1, sl) > 0)
                    know(c, id);
            }
            break;
        case 5: { /* a mage of the White robes' table or the others' */
            unsigned g = c[0x19d] == 1 ? 7 : 8;
            ++SLOT(c, 2, 1);
            for (unsigned l = 1; l < 10; ++l) {
                if (ds(rules, 0x847 + (long)g) != 0xff)
                    add_per_day(rules, &SLOT(c, 2, l), ds(rules, 0x847 + (long)g), level, l);
                mage_intelligence(c);
            }
            break;
        }
        default: break;
        }
    }
    /* A readied ring of wizardry (power 0x81) doubles the fifth level's. */
    for (size_t i = 0; i < character->item_count; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x38] == 0x81 && item[I->readied] != 0) {
            SLOT(c, 2, 5) = (uint8_t)(SLOT(c, 2, 5) + SLOT(c, 2, 5));
            break;
        }
    }
}

void cok_queen_thief_skills(const cok_rules *rules, uint8_t *c)
{
    uint8_t level = LEVEL(c, 6);
    if (level > 0x12) level = 0x12;
    unsigned race = (unsigned)(c[0x58] | c[0x59] << 8);
    for (unsigned i = 1; i < 9; ++i) {
        int8_t adjust = (int8_t)ds(rules, 0x64b4 + (long)race * 8 + (long)i);
        uint8_t base = ds(rules, 0x6424 + (long)(level - 1) * 8 + (long)i);
        if (adjust < 0 && base < -adjust) {
            c[0x92 + i] = 0;
        } else {
            c[0x92 + i] = (uint8_t)(base + adjust);
            /* The first five also take dexterity's (DS:64ec + 5 * (dexterity - 9)). */
            if (i < 6)
                c[0x92 + i] = (uint8_t)(c[0x92 + i] +
                                        ds(rules, 0x64ec + ((long)c[0x7f] - 9) * 5 + (long)i));
        }
    }
}

void cok_queen_saving_throws(const cok_character *character, const cok_rules *rules)
{
    uint8_t *c = (uint8_t *)character->record;
    /* A readied item of power 6 (the last one found with a power). */
    bool ring = false;
    for (size_t i = 0; i < character->item_count && !ring; ++i) {
        const uint8_t *item = character->items[i];
        if (item[0x38] > 0x80 && item[rules->item_record.readied] != 0)
            ring = (item[0x38] & 0x7f) == 6;
    }
    unsigned race = (unsigned)(c[0x58] | c[0x59] << 8);
    for (unsigned k = 0; k < 5; ++k) {
        c[0x8b + k] = 0x14;
        for (unsigned class_ = 0; class_ < 7; ++class_) {
            uint8_t level = LEVEL(c, class_);
            if (level == 0) continue;
            if (level > 0x15) level = 0x15;
            uint8_t save = ds(rules, 0x13cc + (long)class_ * 0x6e + (long)level * 5 + (long)k);
            if (save < c[0x8b + k]) c[0x8b + k] = save;
            if (k == 0 && (race == 3 || race == 5 || race == 4 || ring)) {
                /* The constitution's: 1 for 4-6, 2 for 7-10, 3 for 11-13, 4 for 14-17, 5 for 18. */
                uint8_t con = c[0x81];
                if (con >= 4 && con <= 18)
                    c[0x8b + k] = (uint8_t)(c[0x8b + k] + (con <= 6 ? 1 : con <= 10 ? 2 :
                                                           con <= 13 ? 3 : con <= 17 ? 4 : 5));
            }
            if (k == 0) {
                uint8_t con = c[0x81];
                if (con >= 0x13 && con <= 0x19)
                    c[0x8b + k] = (uint8_t)(c[0x8b + k] + (con <= 0x14 ? 1 : con <= 0x16 ? 2 :
                                                           con <= 0x18 ? 3 : 4));
            }
        }
    }
}

/* 177e:21eb, 21bd: a human's level in the first class it has, which is
 * above the level it changed class at. */
static bool changed_class(const uint8_t *c)
{
    if ((int16_t)(c[0x58] | c[0x59] << 8) != 6) return false;
    unsigned k = 0;
    while (k < 6 && LEVEL(c, k) == 0) ++k;
    return LEVEL(c, k) > c[0x92];
}

bool cok_queen_levels(cok_character *character, const cok_rules *rules,
                      const cok_item_types *types, char *error, size_t error_size)
{
    uint8_t *c = character->record;
    const cok_rules_item_record *I = &rules->item_record;
    c[0x87] = 0;
    for (unsigned k = 0; k < 7; ++k) {
        uint8_t level = LEVEL(c, k), used = level > 0x15 ? 0x15 : level;
        uint8_t thac0 = ds(rules, 0x6359 + (long)k * 0x16 + used);
        if (thac0 > c[0x87]) c[0x87] = thac0;
        if (c[0x91] < level) c[0x91] = level;
        /* A knight, fighter, paladin or ranger gets a third attack past the
         * sixth (the ranger's seventh) level, a fourth past the twelfth
         * (fourteenth). */
        if (k >= 1 && k <= 3) {
            if (level > 6) c[0xb3] = 3;
            if (level > 0xc) c[0xb3] = 4;
        } else if (k == 4) {
            if (level > 7) c[0xb3] = 3;
            if (level > 0xe) c[0xb3] = 4;
        }
    }
    int16_t deity = (int16_t)(int8_t)c[0x19b], race = (int16_t)(c[0x58] | c[0x59] << 8);
    if ((deity == 6 && (race == 3 || race == 4)) || deity == 3) ++c[0x87];
    cok_queen_spells(character, rules);
    cok_queen_saving_throws(character, rules);
    if (c[0xab] > 0) cok_queen_thief_skills(rules, c);
    c[0xbf] = 0;
    for (unsigned k = 0; k < 7; ++k)
        if (LEVEL(c, k) > 0 || (FORMER(c, k) > 0 && FORMER(c, k) < c[0x91]))
            c[0xbf] = (uint8_t)(c[0xbf] + ds(rules, 0x63f3 + (long)k));
    /* A readied item of a class not among those usable is put by. */
    for (unsigned slot = 0; slot < COK_ITEM_SLOTS; ++slot) {
        size_t n = character->slots[slot];
        if (n == 0 || n > character->item_count) continue;
        uint8_t *item = character->items[n - 1];
        if (item[I->type] >= COK_ITEM_TYPES) continue;
        if ((types->type[item[I->type]][0xd] & c[0xbf]) != 0 || item[0x34] != 0 ||
            item[I->readied] == 0)
            continue;
        if ((item[0x38] & 0x80) != 0) {
            fail(error, error_size,
                 "an item of a power the class cannot use is readied: putting it by takes "
                 "its effect away (dqk 86b8:1965 and 16f7, the items')");
            return false;
        }
        item[I->readied] = 0;
    }
    if (changed_class(c)) {
        for (unsigned k = 0; k < 7; ++k) {
            uint8_t former = FORMER(c, k);
            switch (k) {
            case 2: case 3:
                if (former >= 0xd) {
                    c[0xb3] = 4;
                } else if (former >= 7) {
                    c[0xb3] = 3;
                }
                break;
            case 4:
                if (former >= 0xf)
                    c[0xb3] = 4;
                else if (former >= 8)
                    c[0xb3] = 3;
                break;
            case 6:
                if (former > 0) cok_queen_thief_skills(rules, c);
                break;
            default: break;
            }
        }
    }
    return true;
}

/* 86b8:3a5b: a level's extra hit points for the constitution (DS:082d by
 * the score), and for a knight, fighter, paladin or ranger of 17 to 25
 * so much more. */
static int8_t constitution_hit_points(const cok_rules *rules, const uint8_t *c, unsigned class_)
{
    uint8_t con = c[0x81];
    uint8_t bonus = ds(rules, 0x82d + (long)con);
    if (class_ >= 1 && class_ <= 4 && con >= 0x11 && con <= 0x19) {
        unsigned i = con - 0x11u;
        bonus = (uint8_t)(bonus + (i == 0 ? 1 : i == 1 ? 2 : i <= 3 ? 3 : i <= 6 ? 4 : 5));
    }
    return (int8_t)bonus;
}

void cok_queen_hit_points(cok_character *character, const cok_rules *rules, uint32_t *seed,
                          uint8_t classes, uint8_t divide, bool add)
{
    uint8_t *c = character->record;
    uint8_t hit = 0, bonus = 0;
    for (unsigned k = 0; k < 7; ++k) {
        uint8_t level = LEVEL(c, k);
        if (level == 0 || (classes & ds(rules, 0x63fa + (long)k)) == 0) continue;
        if (level < ds(rules, 0x640a + (long)k)) {
            /* A first level's dice, the others' one. */
            uint8_t count = level > 1 ? 1 : ds(rules, 0x6411 + (long)k);
            uint8_t sides = ds(rules, 0x6418 + (long)k);
            uint8_t first = cok_dice(seed, count, sides), second = cok_dice(seed, count, sides);
            if (second > first) first = second;
            hit = (uint8_t)(hit + first);
            bonus = (uint8_t)(bonus + (uint8_t)(constitution_hit_points(rules, c, k) * count));
        } else {
            /* The top level or past it: so many a level as a row has. */
            int row = cok_queen_experience_row(rules, c, k);
            if (row == 0 || row == 1 || row == 7 || row == 8)
                hit = 1;
            else if (row == 2 || row == 10)
                hit = 3;
            else if (row >= 0)
                hit = 2;
            /* A row the original leaves unset (a knight of no order) keeps the sum. */
        }
    }
    if (divide == 0) {
        c[0x198] = 0;
        c[0xc0] = 0;
        c[0x89] = 0;
        return;
    }
    hit = (uint8_t)((int)hit / divide);
    if (hit < 1) hit = 1;
    bonus = (uint8_t)((int)bonus / divide);
    uint8_t hurt = (uint8_t)(c[0x89] - c[0x198]);
    if (add) {
        c[0xc0] = (uint8_t)(c[0xc0] + hit);
        c[0x89] = (uint8_t)(c[0x89] + hit + bonus);
        c[0x198] = c[0x89] >= hurt ? (uint8_t)(c[0x89] - hurt) : 0;
        return;
    }
    c[0xc0] = c[0xc0] > hit ? (uint8_t)(c[0xc0] - hit) : 0;
    uint8_t lose = (uint8_t)(hit + bonus), most = cok_queen_most_hit_points(rules, c);
    if ((int)c[0x89] - lose > most) lose = (uint8_t)(c[0x89] - most);
    c[0x89] = c[0x89] < lose ? 0 : (uint8_t)(c[0x89] - lose);
    c[0x198] = c[0x89] >= hurt ? (uint8_t)(c[0x89] - hurt) : 0;
}

bool cok_queen_new_order(cok_character *character, const cok_rules *rules,
                         const cok_item_types *types, char *error, size_t error_size)
{
    uint8_t *c = character->record;
    ++c[0x19a];
    for (;;) {
        int32_t need;
        if (!cok_queen_experience(rules, c, LEVEL(c, 1), 1, &need, error, error_size)) return false;
        int32_t have = (int32_t)((uint32_t)c[0x44] | (uint32_t)c[0x45] << 8 |
                                 (uint32_t)c[0x46] << 16 | (uint32_t)c[0x47] << 24);
        if (need <= have) break;
        uint8_t old = LEVEL(c, 1), hit, bonus;
        --LEVEL(c, 1);
        if (old > 9) {
            hit = 2;
            bonus = 0;
        } else {
            hit = (uint8_t)((int)c[0xc0] / (old + 1));
            bonus = (uint8_t)cok_queen_constitution(rules, c, 1);
        }
        c[0xc0] = (uint8_t)(c[0xc0] - hit);
        c[0x89] = (uint8_t)(c[0x89] - (uint8_t)(hit + bonus));
        if (c[0x198] >= c[0x89]) c[0x198] = c[0x89];
    }
    if (!cok_queen_levels(character, rules, types, error, error_size)) return false;
    bool may;
    if (!cok_queen_may_train(rules, c, &may, error, error_size)) return false;
    c[0xcd] = may;
    return true;
}
