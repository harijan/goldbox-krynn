#include "queen_train.h"

#include "data.h"
#include "queen.h"
#include "queen_levels.h"

#include <stdio.h>
#include <string.h>

/* The messages (DS:74fa-7597, 7f4f). */
static const char *const MSG_CONSCIOUS = "we only train conscious people";
static const char *const MSG_HALL = "we don't train that class here";
static const char *const MSG_EXPERIENCE = "Not Enough Experience";

static const char *const class_names[7] = {"Cleric", "Knight", "Fighter", "Paladin",
                                            "Ranger", "Magic-User", "Thief"};

static uint8_t ds(const cok_adventure *game, long offset)
{
    return cok_data_held(cok_game_rules(game)->data, offset);
}

static int32_t experience_of(const uint8_t *c)
{
    return (int32_t)((uint32_t)c[0x44] | (uint32_t)c[0x45] << 8 | (uint32_t)c[0x46] << 16 |
                     (uint32_t)c[0x47] << 24);
}

static void set_experience(uint8_t *c, int32_t value)
{
    c[0x44] = (uint8_t)value;
    c[0x45] = (uint8_t)((uint32_t)value >> 8);
    c[0x46] = (uint8_t)((uint32_t)value >> 16);
    c[0x47] = (uint8_t)((uint32_t)value >> 24);
}

/* 8bcc:0e9c for class k, the run stopped where it has no row. */
static bool need(cok_adventure *game, const uint8_t *c, unsigned level, unsigned k,
                 int32_t *out)
{
    char error[300];
    if (cok_queen_experience(cok_game_rules(game), c, level, k, out, error, sizeof error))
        return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
    return false;
}

/* The next level's cap on the experience (6be8:0022's DS and stack long
 * [bp-0x1e]): the need of the level after, which the experience has
 * reached, less one, where that is above the cap. */
static void cap_by(int32_t next, int32_t have, int32_t *cap)
{
    if (next <= 0 || have < next || next <= *cap) return;
    *cap = next - 1;
}

/* A mage's new spells in silent training (6be8:06b3): by its order of
 * magic and its level now, 3, 5 or 7, the bytes of the known spells' bits
 * +0x15a-+0x161 or'ed with DS:68bc's. */
static void silent_spells(uint8_t *c)
{
    static const struct { uint8_t order, level; uint16_t at; uint8_t bits; } grants[] = {
        {1, 3, 0x15a, 0x10}, {1, 3, 0x15b, 0x01},
        {1, 5, 0x15c, 0x20}, {1, 5, 0x15c, 0x40}, {1, 5, 0x15d, 0x04},
        {1, 7, 0x161, 0x01}, {1, 7, 0x161, 0x02}, {1, 7, 0x161, 0x10}, {1, 7, 0x161, 0x40},
        {2, 3, 0x15a, 0x20}, {2, 3, 0x15a, 0x40}, {2, 3, 0x15a, 0x80}, {2, 3, 0x15b, 0x02},
        {2, 5, 0x15c, 0x10}, {2, 5, 0x15c, 0x40}, {2, 5, 0x15c, 0x80}, {2, 5, 0x15d, 0x02},
        {2, 7, 0x161, 0x04}, {2, 7, 0x161, 0x10}, {2, 7, 0x161, 0x08}, {2, 7, 0x161, 0x40},
    };
    uint8_t order = c[0x19d] == 1 ? 1 : 2, level = c[0xaa];
    for (size_t i = 0; i < sizeof grants / sizeof *grants; ++i)
        if (grants[i].order == order && grants[i].level == level)
            c[grants[i].at] |= grants[i].bits;
}

bool queen_train(cok_adventure *game, cok_character *character, bool silent, bool *nothing)
{
    const cok_rules *rules = cok_game_rules(game);
    cok_ecl *vm = &game->vm;
    uint8_t *c = character->record;
    char error[300];
    *nothing = false;
    if (c[0x64] != 0 || c[0x65] != 0) {
        queen_bar_message_colour(game, MSG_CONSCIOUS, 0x0b);
        return true;
    }
    bool debug = queen_train_anywhere(game);
    uint8_t train = 0, classes = 0, hall = (uint8_t)cok_ecl_load_var(vm, 0x30), stale = 0;
    int32_t cap = 0;
    for (unsigned k = 0; k < 7; ++k) {
        uint8_t level = c[0xa5 + k];
        if (level == 0) continue;
        classes = (uint8_t)(classes + ds(game, 0x63fa + (long)k));
        stale = level;
        bool skip;
        if (k == 1 && c[0x19a] == 3)
            skip = level >= 0x28;
        else if (k == 1 && (c[0x19a] == 2 || c[0x19a] == 4))
            skip = level >= 0x12;
        else
            skip = ds(game, 0x17dc + (long)(c[0x58] | c[0x59] << 8) * 7 + (long)k) <= level;
        if (skip) continue;
        int32_t want;
        if (!need(game, c, (unsigned)level + 1, k, &want)) return false;
        if (want > experience_of(c) && !debug) continue;
        if (want <= 0) continue;
        /* Anywhere, the experience is set to what the level needs. */
        if (debug) set_experience(c, want);
        train = (uint8_t)(train + ds(game, 0x63fa + (long)k));
        int32_t next;
        if (!need(game, c, (unsigned)level + 2, k, &next)) return false;
        cap_by(next, experience_of(c), &cap);
    }
    if (!silent) {
        /* The best need among those that train, by the last class's level
         * (6be8:0294: its [bp-0x10] is the last the loop above read). */
        int32_t best = 0;
        unsigned best_k = 0;
        for (unsigned k = 0; k < 7; ++k) {
            if ((ds(game, 0x63fa + (long)k) & train) == 0) continue;
            int32_t want;
            if (!need(game, c, (unsigned)stale + 1, k, &want)) return false;
            if (want > best) {
                best = want;
                best_k = k;
            }
        }
        if (best > 0) {
            train = (uint8_t)(train | ds(game, 0x63fa + (long)best_k));
            int32_t next;
            if (!need(game, c, (unsigned)stale + 2, best_k, &next)) return false;
            cap_by(next, experience_of(c), &cap);
        }
        /* Training holds the experience below the level after's: not silent
         * (6be8:0385). */
        if (cap > 0 && experience_of(c) > cap) set_experience(c, cap);
    }
    if (!debug) {
        if ((classes & hall) == 0 && !silent) {
            queen_bar_message_colour(game, MSG_HALL, 0x0b);
            return true;
        }
        if ((train & hall) == 0) {
            *nothing = true;
            c[0xcd] = 0;
            if (!silent) queen_bar_message_colour(game, MSG_EXPERIENCE, 0x0b);
            return true;
        }
    }
    uint8_t shown = debug ? train : (uint8_t)(train & hall);
    if (!silent) {
        queen_clear_cells(game, 1, 1, 0x26, 0x16);
        char name[17];
        cok_field_name(rules->character, c, name);
        queen_say(game, 4, 4, name, c[0x18b] == 0 ? 0 : c[0xcd] != 0 ? 0x0d : 0x07);
        queen_say(game, 4 + (int)strlen(name), 4, " will become:", 7);
        cok_adventure_log(game, "print", " will become:");
        int row = 4;
        for (unsigned k = 0; k < 7; ++k) {
            if (c[0xa5 + k] == 0 || (ds(game, 0x63fa + (long)k) & shown) == 0) continue;
            ++row;
            char line[64];
            snprintf(line, sizeof line, "%s a level %u %s", row == 5 ? "   " : "and",
                     (unsigned)c[0xa5 + k] + 1, class_names[k]);
            queen_say(game, 6, row, line, 7);
            cok_adventure_log(game, "print", line);
        }
        int yes = queen_yes_no(game, "Do you wish to train? ");
        if (yes < 0) return false;
        if (!yes) return true;
        queen_bar_message_colour(game, "Congratulations...", 0x0f);
    }
    uint8_t old_ranger = c[0xa9], old_mage = c[0xaa];
    unsigned count = 0;
    c[0xbf] = 0;
    for (unsigned k = 0; k < 7; ++k) {
        if (c[0xa5 + k] == 0) continue;
        ++count;
        if ((ds(game, 0x63fa + (long)k) & train) != 0) ++c[0xa5 + k];
    }
    if (!cok_queen_levels(character, rules, &game->item_types, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    if (!silent) {
        if (c[0xaa] > old_mage || (c[0xa9] > 8 && c[0xa9] > old_ranger)) {
            cok_adventure_not_yet(game, "a new spell of the training's", "dqk 849a:2080");
            return false;
        }
    } else {
        silent_spells(c);
    }
    cok_queen_hit_points(character, rules, &vm->seed, train, (uint8_t)count, true);
    bool may;
    if (!cok_queen_may_train(rules, c, &may, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    c[0xcd] = may;
    return true;
}

bool queen_train_silently(cok_adventure *game, cok_character *character)
{
    cok_ecl *vm = &game->vm;
    uint16_t hall = cok_ecl_load_var(vm, 0x30);
    bool anywhere = queen_train_anywhere(game);
    cok_ecl_store(vm, 0x30, 0xff);
    queen_set_train_anywhere(game, false);
    bool ok = true, nothing = false;
    /* 66e5:1a90: Train again until DS:b183 says nothing trained (the
     * original's would run on past the levels' last). */
    for (unsigned n = 0; ok && !nothing; ++n) {
        if (n > 0x400) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "silent training does not end (6be8:0022 never sets DS:b183)");
            ok = false;
        } else {
            ok = queen_train(game, character, true, &nothing);
        }
    }
    queen_set_train_anywhere(game, anywhere);
    cok_ecl_store(vm, 0x30, hall);
    return ok;
}

/* A line centred as 7368:10c2 does: its first cell by the length of the
 * text (the name's and the words' that follow) about 0x13 (0x14 for the
 * Sword's), and the line. */
static void petition_line(cok_adventure *game, int middle, int y, const char *name,
                          const char *words)
{
    char line[96];
    int x = middle - ((int)strlen(name) + (int)strlen(words)) / 2;
    snprintf(line, sizeof line, "%s%s", name, words);
    queen_say(game, x, y, line, 7);
    cok_adventure_log(game, "print", line);
}

bool queen_knight_order(cok_adventure *game, cok_character *character)
{
    const cok_rules *rules = cok_game_rules(game);
    uint8_t *c = character->record;
    char error[300], name[17];
    static const uint16_t crown[6] = {12, 9, 13, 9, 10, 3};  /* DS:7d98 */
    static const uint16_t sword[6] = {15, 10, 13, 12, 15, 3}; /* DS:7da4 */
    queen_full_window(game);
    if (c[0xa6] == 0 || c[0x19a] == 3) return true;
    cok_field_name(rules->character, c, name);
    int x = 0x13 - (int)strlen(name) / 2;
    queen_say(game, x, 3, name, 7);
    cok_adventure_log(game, "print", name);
    queen_say(game, 7, 5, "is petitioning to become a", 7);
    const char *next = c[0x19a] == 1 ? "sword" : c[0x19a] == 2 ? "rose" : "";
    char line[64];
    snprintf(line, sizeof line, "knight of the %s", next);
    queen_say(game, 10, 7, line, 7);
    bool change = false;
    if (c[0x19a] == 1 || c[0x19a] == 2) {
        bool crown_order = c[0x19a] == 1;
        const uint16_t *least = crown_order ? crown : sword;
        int middle = crown_order ? 0x13 : 0x14;
        int32_t have = (int32_t)((uint32_t)c[0x44] | (uint32_t)c[0x45] << 8 |
                                 (uint32_t)c[0x46] << 16 | (uint32_t)c[0x47] << 24);
        long first = crown_order ? 0x98e : 0x9de;
        int32_t most = (int32_t)((uint32_t)cok_data_held(rules->data, first) |
                                 (uint32_t)cok_data_held(rules->data, first + 1) << 8 |
                                 (uint32_t)cok_data_held(rules->data, first + 2) << 16 |
                                 (uint32_t)cok_data_held(rules->data, first + 3) << 24);
        if (c[0xa6] < (crown_order ? 3 : 4) || have < most) {
            petition_line(game, middle, 9, name, " is too inexperienced");
        } else {
            bool qualifies = true;
            for (unsigned i = 0; i < 6; ++i)
                if (least[i] > c[0x78 + 2 * i]) qualifies = false;
            if (!qualifies) {
                petition_line(game, middle, 9, name, " does not qualify.");
            } else {
                petition_line(game, 0x13, 9, name, " may become a");
                snprintf(line, sizeof line, "knight of the %s", crown_order ? "sword" : "rose");
                queen_say(game, 10, 0xb, line, 7);
                cok_adventure_log(game, "print", line);
                change = true;
            }
        }
    }
    if (!change) return true;
    static const char *const items[2] = {"Change", "Exit"};
    int choice = queen_bar_menu(game, "", items, 2, false);
    if (choice < 0) return false;
    if (choice != 0) return true;
    if (!cok_queen_new_order(character, rules, &game->item_types, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    return true;
}
