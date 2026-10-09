#include "train.h"

#include "camp.h"
#include "items.h"
#include "magic.h"
#include "screen.h"
#include "start.h"

#include <stdio.h>
#include <string.h>

/* The hit dice at level 1 by class (DS:0b6e), their sides (DS:0b76), and
 * the base scores a knight of the Crown (DS:0b7e) and of the Sword
 * (DS:0b84) needs for the next order. */
static const uint8_t level_dice[8] = {2, 1, 1, 1, 2, 1, 1, 2};
static const uint8_t die_sides[8] = {8, 8, 10, 10, 8, 4, 6, 10};
static const uint8_t knight_scores[2][6] = {{12, 9, 13, 9, 10, 3}, {15, 10, 13, 12, 15, 3}};

const cok_ds_table cok_train_tables[] = {
    {0x0b6e, sizeof level_dice, 1, level_dice},
    {0x0b76, sizeof die_sides, 1, die_sides},
    {0x0b7e, sizeof knight_scores, 1, knight_scores[0]},
};
const size_t cok_train_table_count = sizeof cok_train_tables / sizeof *cok_train_tables;

static uint8_t ds(uint16_t offset)
{
    uint8_t byte = 0;
    cok_ds_byte(offset, &byte);
    return byte;
}

/* The class bits of DS:38f2 and the top levels of DS:3903, by class. */
static uint8_t class_bit(unsigned class_)
{
    return ds((uint16_t)(0x38f2 + class_));
}

static uint8_t top_level(unsigned class_)
{
    return ds((uint16_t)(0x3903 + class_));
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

static int32_t get_long(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return v > INT32_MAX ? -(int32_t)(~v) - 1 : (int32_t)v;
}

static void put_long(uint8_t *p, int32_t value)
{
    uint32_t v = (uint32_t)value;
    for (size_t i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

bool cok_train_row(const uint8_t *c, unsigned class_, uint8_t *row)
{
    switch (class_) {
    case 0: *row = (int8_t)c[0x5d] > 4 ? 1 : 0; return true;
    case 2: *row = 2; return true;
    case 4: *row = 6; return true;
    case 5: *row = c[0x5e] == 1 ? 7 : 8; return true;
    case 6: *row = 9; return true;
    case 7:
        if (c[0x5c] < 1 || c[0x5c] > 3) return false;
        *row = (uint8_t)(c[0x5c] + 2);
        return true;
    default: return false;
    }
}

bool cok_train_experience(uint8_t row, int level, int32_t *need)
{
    uint8_t bytes[4];
    for (int i = 0; i < 4; ++i) {
        long at = (0x3c65 + 99L * row + 4L * level + i) & 0xffff;
        if (!cok_ds_byte((uint16_t)at, &bytes[i])) return false;
    }
    *need = get_long(bytes);
    return true;
}

int8_t cok_train_constitution(const uint8_t *c, unsigned class_)
{
    uint8_t con = c[0x19];
    uint8_t bonus = ds((uint16_t)(0x3c53 + con));
    if (class_ == 2 || class_ == 7 || class_ == 4) {
        if (con == 17) bonus = (uint8_t)(bonus + 1);
        else if (con == 18) bonus = (uint8_t)(bonus + 2);
        else if (con >= 19 && con <= 20) bonus = (uint8_t)(bonus + 3);
        else if (con >= 21 && con <= 23) bonus = (uint8_t)(bonus + 4);
        else if (con >= 24 && con <= 25) bonus = (uint8_t)(bonus + 5);
    }
    return (int8_t)bonus;
}

bool cok_train_hit_points(cok_adventure *game, uint8_t *c, uint8_t mask, uint8_t count)
{
    uint8_t sum = 0, con = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t level = (int8_t)c[0xf9 + i];
        if (level <= 0 || (class_bit(i) & mask) == 0) continue;
        if (c[0xf9 + i] < top_level(i)) {
            uint8_t dice = level > 1 ? 1 : i == 0 && (int8_t)c[0x5d] > 4 ? 1 : level_dice[i];
            uint8_t a = cok_dice(&game->vm.seed, dice, die_sides[i]);
            uint8_t b = cok_dice(&game->vm.seed, dice, die_sides[i]);
            sum = (uint8_t)(sum + (a > b ? a : b));
            con = (uint8_t)(con + dice * cok_train_constitution(c, i));
        } else if (i == 2 || i == 7) {
            sum = 3; /* assigned, not added */
        } else if (i == 0 || i == 4 || i == 6) {
            sum = 2;
        } else if (i == 5) {
            sum = 1;
        }
    }
    if (count == 0) {
        cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                           "hit points divided among no classes (4def:4b3a)");
        return false;
    }
    sum = (uint8_t)(sum / count);
    if (sum == 0) sum = 1;
    c[0x11b] = (uint8_t)(c[0x11b] + sum);
    /* The bonus divides as an unsigned byte: a negative one over two or
     * three classes becomes large. */
    con = (uint8_t)(con / count);
    uint8_t lacking = (uint8_t)(c[0x62] - c[0x197]);
    c[0x62] = (uint8_t)(c[0x62] + con + sum);
    c[0x197] = (uint8_t)(c[0x62] - lacking);
    return true;
}

/* Screen helpers. */

static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    cok_picture_fill(&game->screen, x1, y1 * 8, (size_t)(x2 - x1 + 1), (size_t)(y2 - y1 + 1) * 8,
                     0); /* 1128:07e6 */
}

static void draw(cok_adventure *game, const char *text, int x, int y)
{
    cok_adventure_log(game, "print", text);
    cok_text_string(&game->screen, &game->font, text, x, y, 10, 0);
}

static cok_character *selected(cok_adventure *game)
{
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

static bool levels(cok_adventure *game, cok_character *character)
{
    char error[300];
    if (cok_character_levels(character, &game->item_types, error, sizeof error)) return true;
    return undefined(game, error);
}

/* The row 6346:371e returns for a class without one, first in a call. */
#define STALE_ROW 0x74

static bool need_for(cok_adventure *game, uint8_t row, int level, int32_t *need)
{
    if (cok_train_experience(row, level, need)) return true;
    char text[120];
    snprintf(text, sizeof text,
             "the experience for level %d reads DS:%04lx, past the original's initialized data "
             "(4def:4d9e)", level, (0x3c65 + 99L * row + 4L * level) & 0xffff);
    return undefined(game, text);
}

/* 4def:4d9e on character; creating is DS:4b58. Returns false when the
 * run must stop; *none is DS:713f, set when no class could rise. */
static bool train(cok_adventure *game, cok_character *character, bool creating, bool *none)
{
    uint8_t *c = character->record;
    bool free_training = game->free_training;
    *none = false;
    if (c[0x188] != 0 && !free_training) {
        /* Creating too, though a new character is always able to act. */
        cok_adventure_notice(game, "we only train conscious people", 14);
        return true;
    }
    uint8_t present = 0, trainable = 0, hall = (uint8_t)game->vm.mem7c00[0x2a8];
    int32_t cap = 0;
    /* 6346:371e's uninitialized local: the previous class's row in the same
     * call, else 0x74, the 't' of "Pick Character " that Pick Character
     * (546c:36cc) copies to its frame on every key, at the slot 371e's
     * local takes (4def:4ea1). Row 0x74 reads the combatants' far pointers
     * (DS:6945 on), NULL or a segment above 0x2000: a need of 0 or more
     * than 0x20000000. */
    bool first = true, stale = false;
    uint8_t row = 0;
    int level = 0;
    int32_t experience = get_long(c + 0x116);
    for (unsigned i = 0; i < 8; ++i) {
        if ((int8_t)c[0xf9 + i] <= 0) continue;
        present = (uint8_t)(present + class_bit(i));
        level = (int8_t)c[0xf9 + i];
        bool kender = c[0x5a] == 5 && (i == 2 || i == 4) &&
                      (level == 7 || (level == 6 && c[0x10] == 17) || (level == 5 && c[0x10] < 17));
        if (cok_train_row(c, i, &row)) {
            stale = false;
        } else if (first) {
            if (creating)
                return undefined(game, "the experience table of a druid, a paladin or a knight "
                                       "of no order is 6346:371e's uninitialized local, which "
                                       "creation leaves");
            row = STALE_ROW;
            stale = true;
        }
        first = false;
        if (kender) continue;
        if (stale) {
            /* Never trainable, and no cap, unless the pointer is taken as
             * experience or the experience passes it. */
            if (free_training || experience >= 0x20000000)
                return undefined(game, "the experience table of a druid, a paladin or a knight "
                                       "of no order is row 0x74, the combatants' pointers "
                                       "(6346:371e)");
            continue;
        }
        int32_t need, next;
        if (!need_for(game, row, level + 1, &need)) return false;
        if (need > experience && !free_training) continue;
        if (need <= 0) continue;
        if (free_training && need > experience) {
            experience = need;
            put_long(c + 0x116, experience);
        }
        trainable = (uint8_t)(trainable + class_bit(i));
        if (!need_for(game, row, level + 2, &next)) return false;
        if (next > 0 && experience >= next && next > cap) cap = next - 1;
    }
    if (!creating && !stale) {
        /* The second pass reads the last class's row and level for every
         * class (4def:4d9e); from row 0x74 it changes nothing. */
        int32_t best = 0, need, next;
        for (unsigned i = 0; i < 8; ++i) {
            if ((class_bit(i) & trainable) == 0) continue;
            if (!need_for(game, row, level + 1, &need)) return false;
            if (need > best) best = need;
        }
        if (best > 0) {
            if (!need_for(game, row, level + 2, &next)) return false;
            if (next > 0 && experience >= next && next > cap) cap = next - 1;
        }
    }
    if (cap > 0 && !creating) {
        experience = cap;
        put_long(c + 0x116, experience);
    }
    if (!free_training) {
        if ((present & hall) == 0 && !creating) {
            cok_adventure_notice(game, "We don't train that class here", 14);
            return true;
        }
        if ((trainable & hall) == 0) {
            *none = true;
            if (!creating) cok_adventure_notice(game, "Not Enough Experience", 14);
            return true;
        }
    }
    uint8_t mask = free_training ? trainable : (uint8_t)(trainable & hall);
    if (!creating) {
        clear_cells(game, 1, 1, 0x26, 0x16);
        cok_item_draw_name(game, c, 4, 4, false);
        draw(game, " will become:", c[0] + 4, 4);
        int y = 4;
        for (unsigned i = 0; i < 8; ++i) {
            if ((int8_t)c[0xf9 + i] <= 0 || (class_bit(i) & mask) == 0) continue;
            ++y;
            char name[256], text[300];
            cok_ds_string((uint16_t)(0x0f5a + 27 * i), name);
            snprintf(text, sizeof text, "%s%u %s", y == 5 ? "    a level " : "and a level ",
                     (uint8_t)(c[0xf9 + i] + 1), name);
            draw(game, text, 6, y);
        }
        int answer = cok_camp_yes_no(game, "Do you wish to train? ", 13);
        if (answer != 'Y') return answer >= 0;
        cok_camp_notice(game, "Congratulations...");
    }
    uint8_t count = 0;
    int8_t mage = (int8_t)c[0xfe];
    c[0x11a] = 0;
    for (unsigned i = 0; i < 8; ++i) {
        if ((int8_t)c[0xf9 + i] <= 0) continue;
        ++count;
        if ((class_bit(i) & mask) == 0) continue;
        ++c[0xf9 + i];
        /* +0xd8 and +0xd9, which nothing else writes. */
        if (c[0xd8] > 0) {
            c[0xd9] = (uint8_t)(c[0xd9] - c[0xd9] / c[0xd8]);
            --c[0xd8];
        }
    }
    if (!levels(game, character)) return false;
    if (!creating && ((int8_t)c[0xfe] > mage || (int8_t)c[0xfd] > 8)) {
        if (cok_magic_learn(game, character) < 0) return false;
    } else if (creating) {
        /* The spells a mage of each level knows when it is made. */
        switch (c[0xfe]) {
        case 2: c[0x62 + 15] = 1; break;
        case 3: c[0x62 + 34] = c[0x62 + 10] = 1; break;
        case 4: c[0x62 + 31] = 1; break;
        case 5: c[0x62 + 47] = 1; break;
        default: break;
        }
    }
    if ((int8_t)c[0xd6] > (int8_t)c[0xd7] && !cok_train_hit_points(game, c, mask, count))
        return false;
    return true;
}

void cok_train(cok_adventure *game)
{
    if (!cok_start_pick(game)) return;
    cok_character *character = selected(game);
    if (character == NULL) {
        undefined(game, "training with no character selected reads through NULL (4def:4d9e)");
        return;
    }
    bool none;
    train(game, character, false, &none);
}

bool cok_train_silently(cok_adventure *game, cok_character *character)
{
    bool none;
    if (!train(game, character, true, &none)) return false;
    return !none;
}

bool cok_train_hit_die(const uint8_t *c, unsigned class_, int8_t *bonus)
{
    uint8_t con = c[0x19];
    if (con == 3) *bonus = -2;
    else if (con >= 4 && con <= 6) *bonus = -1;
    else if (con >= 7 && con <= 14) *bonus = 0;
    else if (con == 15) *bonus = 1;
    else if (con == 16) *bonus = 2;
    else if (con >= 17 && con <= 19)
        *bonus = class_ == 2 || class_ == 4 || class_ == 7 ? (int8_t)(con - 14) : 2;
    else return false;
    return true;
}

bool cok_train_promote(cok_adventure *game, cok_character *character)
{
    uint8_t *c = character->record;
    int8_t level = (int8_t)c[0x100];
    int32_t need;
    if (!need_for(game, c[0x5c] == 1 ? 4 : 5, level, &need)) return false;
    if (need > get_long(c + 0x116)) {
        --c[0x100];
        if (level + 1 == 0) {
            cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                               "hit points divided by a knight level of 0 (4def:567f)");
            return false;
        }
        uint8_t share = (uint8_t)(c[0x11b] / (level + 1));
        c[0x11b] = (uint8_t)(c[0x11b] - share);
        int8_t bonus;
        const uint8_t *s = game->vm.character;
        if (s == NULL || !cok_train_hit_die(s, 7, &bonus))
            return undefined(game, "4def:257c's constitution bonus is uninitialized for a "
                                   "constitution outside 3-19, or none selected (4def:567f)");
        c[0x62] = (uint8_t)(c[0x62] - (uint8_t)(share + bonus));
        if (c[0x197] >= c[0x62]) c[0x197] = c[0x62];
    }
    ++c[0x5c];
    return levels(game, character);
}

/* Draw text centred as 4def:5812 does: from column base - (length +
 * extra) / 2, at row y. */
static void centred(cok_adventure *game, const char *text, int base, int extra, int y,
                    size_t length)
{
    draw(game, text, base - (int)((length + (size_t)extra) >> 1), y);
}

void cok_train_knight(cok_adventure *game)
{
    if (!cok_start_pick(game)) return;
    cok_character *character = selected(game);
    if (character == NULL) {
        undefined(game, "Knight Change Classes with no character selected reads through NULL "
                        "(4def:5812)");
        return;
    }
    uint8_t *c = character->record;
    uint16_t moons[3];
    for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
    cok_screen_frame(&game->screen, &game->view.tiles[4], moons, true); /* 1128:0000 */
    if (c[0x100] == 0 || ((int8_t)c[0x100] > 0 && c[0x5c] == 3)) return;
    const char *menu = "Exit";
    char name[16], text[64];
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(name, c + 1, length);
    name[length] = '\0';
    draw(game, name, 19 - (int)(c[0] >> 1), 3);
    draw(game, "is petitioning to become a", 7, 5);
    /* For a knight of no order the suffix is a local never set, which the
     * start menu's freeing of its rows (4def:0381) leaves empty. */
    snprintf(text, sizeof text, "Knight Of The %s",
             c[0x5c] == 1 ? "sword" : c[0x5c] == 2 ? "rose" : "");
    draw(game, text, 10, 7);
    /* A knight of no order skips to the menu (4def:5aa0). */
    if (c[0x5c] == 1 || c[0x5c] == 2) {
        bool sword = c[0x5c] == 1;
        int32_t need;
        if (!need_for(game, sword ? 4 : 5, sword ? 3 : 4, &need)) return;
        int base = sword ? 19 : 20;
        if ((int8_t)c[0x100] < (sword ? 3 : 4) || get_long(c + 0x116) < need) {
            snprintf(text, sizeof text, "%s is too inexperienced%s", name, sword ? "." : "");
            centred(game, text, base, 21, 9, c[0]);
        } else {
            bool qualifies = true;
            for (size_t k = 0; k < 6; ++k)
                if (c[0x10 + 2 * k] < knight_scores[sword ? 0 : 1][k]) qualifies = false;
            if (!qualifies) {
                snprintf(text, sizeof text, "%s does not qualify.", name);
                centred(game, text, base, 18, 9, c[0]);
            } else {
                snprintf(text, sizeof text, "%s may become a", name);
                centred(game, text, 19, 13, 9, c[0]);
                draw(game, sword ? "Knight Of The Sword" : "Knight Of The Rose", 10, 11);
                menu = "Change Exit";
            }
        }
    }
    for (;;) {
        bool special;
        int key = cok_camp_menu(game, "", menu, false, false, &special);
        if (key < 0) return;
        /* The special flag is not tested: F9, scan code 0x43, is C. */
        if (key == 'C') {
            cok_train_promote(game, character);
            return;
        }
        if (key == 'E' || key == 0) return;
    }
}
