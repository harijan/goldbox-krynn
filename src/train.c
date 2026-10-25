#include "train.h"

#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "items.h"
#include "magic.h"
#include "screen.h"
#include "start.h"
#include "treasure.h"

#include <stdio.h>
#include <string.h>

/* The byte at DS:offset in the game's data segment, 0 where it holds
 * none. */
static uint8_t ds(const cok_data_segment *D, uint16_t offset)
{
    uint8_t byte = 0;
    cok_data_byte(D, offset, &byte);
    return byte;
}

/* The class bits of DS:38f2 and the top levels of DS:3903, by class. */
static uint8_t class_bit(const cok_adventure *game, unsigned class_)
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    return ds(D, (uint16_t)cok_data_offset(D, COK_DATA_TRAINING_BITS, class_));
}

static uint8_t top_level(const cok_adventure *game, unsigned class_)
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    return ds(D, (uint16_t)cok_data_offset(D, COK_DATA_TOP_LEVELS, class_));
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

static int32_t signed_long(uint32_t v)
{
    return v > INT32_MAX ? -(int32_t)(~v) - 1 : (int32_t)v;
}

static int32_t get_long(const uint8_t *p)
{
    return signed_long((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                       (uint32_t)p[3] << 24);
}

/* The experience, a LongInt. */
static int32_t experience_of(const cok_character_layout *L, const uint8_t *c)
{
    return signed_long(cok_field_u32(L, c, COK_FIELD_EXPERIENCE));
}

bool cok_train_row(const uint8_t *c, const cok_rules *rules, unsigned class_, uint8_t *row)
{
    const cok_character_layout *L = rules->character;
    switch (class_) {
    case 0: *row = cok_field_s8(L, c, COK_FIELD_DEITY) > 4 ? 1 : 0; return true;
    case 2: *row = 2; return true;
    case 4: *row = 6; return true;
    case 5: *row = cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER) == 1 ? 7 : 8; return true;
    case 6: *row = 9; return true;
    case 3: /* dkk 65cd:382c; 6346:371e has none */
        if (rules->characters.paladin_row == 0) return false;
        *row = rules->characters.paladin_row;
        return true;
    case 7: {
        uint8_t order = cok_field_u8(L, c, COK_FIELD_KNIGHT_ORDER);
        if (order < 1 || order > 3) return false;
        *row = (uint8_t)(order + 2);
        return true;
    }
    default: return false;
    }
}

bool cok_train_experience(const cok_rules *rules, uint8_t row, int level, int32_t *need)
{
    const cok_data_segment *D = rules->data;
    uint8_t bytes[4];
    for (int i = 0; i < 4; ++i) {
        long at = (cok_data_offset2(D, COK_DATA_EXPERIENCE, row, level) + i) & 0xffff;
        if (!cok_data_byte(D, (uint16_t)at, &bytes[i])) return false;
    }
    *need = get_long(bytes);
    return true;
}

int8_t cok_train_constitution(const uint8_t *c, const cok_rules *rules, unsigned class_)
{
    const cok_character_layout *L = rules->character;
    const cok_data_segment *D = rules->data;
    uint8_t con = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION);
    uint8_t bonus = ds(D, (uint16_t)cok_data_offset(D, COK_DATA_CONSTITUTION, con));
    /* Fighters, rangers and knights (dkk 580d:310e paladins too). */
    if (class_ < 8 && (rules->characters.fighter_classes >> class_ & 1) != 0) {
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t sum = 0, con = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        if (level <= 0 || (class_bit(game, i) & mask) == 0) continue;
        if (cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, i) < top_level(game, i)) {
            bool druid = i == 0 && cok_field_s8(L, c, COK_FIELD_DEITY) > 4;
            uint8_t dice = level > 1 ? 1 : druid ? 1 : cok_data_u8_at(D, COK_DATA_LEVEL_DICE, i);
            uint8_t a = cok_dice(&game->vm.seed, dice, cok_data_u8_at(D, COK_DATA_DIE_SIDES, i));
            uint8_t b = cok_dice(&game->vm.seed, dice, cok_data_u8_at(D, COK_DATA_DIE_SIDES, i));
            sum = (uint8_t)(sum + (a > b ? a : b));
            con = (uint8_t)(con + dice * cok_train_constitution(c, cok_game_rules(game), i));
        } else if (cok_game_rules(game)->levels.top_hit_points[i] != 0) {
            sum = cok_game_rules(game)->levels.top_hit_points[i]; /* assigned, not added */
        }
    }
    if (count == 0) {
        cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                           "hit points divided among no classes (4def:4b3a)");
        return false;
    }
    sum = (uint8_t)(sum / count);
    if (sum == 0) sum = 1;
    cok_field_set8(L, c, COK_FIELD_BASE_HIT_POINTS,
                   (uint8_t)(cok_field_u8(L, c, COK_FIELD_BASE_HIT_POINTS) + sum));
    /* The bonus divides as an unsigned byte: a negative one over two or
     * three classes becomes large. */
    con = (uint8_t)(con / count);
    uint8_t lacking = (uint8_t)(cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) -
                                cok_field_u8(L, c, COK_FIELD_HIT_POINTS));
    cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS,
                   (uint8_t)(cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) + con + sum));
    uint8_t most = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS);
    /* dkk 580d:3193: no lower than 0. */
    if (most < lacking && cok_game_rules(game)->levels.hit_points_floor)
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 0);
    else
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, (uint8_t)(most - lacking));
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
    if (cok_character_levels(character, cok_game_rules(game), &game->item_types, error,
                             sizeof error))
        return true;
    return undefined(game, error);
}

static bool need_for(cok_adventure *game, uint8_t row, int level, int32_t *need)
{
    const cok_rules *rules = cok_game_rules(game);
    if (cok_train_experience(rules, row, level, need)) return true;
    char text[120];
    snprintf(text, sizeof text,
             "the experience for level %d reads DS:%04lx, past the original's initialized data "
             "(4def:4d9e)", level,
             cok_data_offset2(rules->data, COK_DATA_EXPERIENCE, row, level) & 0xffff);
    return undefined(game, text);
}

/* Whether c may train (dkk 65cd:38c7, +0xc6), where the game keeps it
 * (combat.may_train), set after training and Knight Change. */
static bool set_may_train(cok_adventure *game, uint8_t *c)
{
    const cok_rules *rules = cok_game_rules(game);
    if (!rules->combat.may_train) return true;
    bool may;
    if (!cok_treasure_may_train(rules, c, &may))
        return undefined(game, "may-train (dkk 65cd:38c7) reads a byte the port does not hold, "
                               "the stack's or past DS:82d4's table");
    cok_field_set8(rules->character, c, COK_FIELD_MAY_TRAIN, may);
    return true;
}

/* Whether class_ (of level) of c is at its race's highest level
 * (COK_DATA_LEVEL_LIMITS, dkk 49cb:43b6's DS:82d4 + race * 8 + class), or
 * where the game has the kender's limits instead, a kender fighter or
 * ranger at 5 below strength 17, at 6 with 17 or at 7 (4def:4d9e). Returns
 * false where the table does not hold the race. */
static bool at_limit(cok_adventure *game, const uint8_t *c, unsigned class_, int level, bool *at)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    if (rules->levels.race_limits) {
        const cok_data_segment *D = rules->data;
        long offset = cok_data_offset2(D, COK_DATA_LEVEL_LIMITS, cok_field_s8(L, c, COK_FIELD_RACE),
                                       class_);
        uint8_t limit;
        if (!cok_data_holds(D, COK_DATA_LEVEL_LIMITS, offset) ||
            !cok_data_byte(D, (uint16_t)offset, &limit))
            return undefined(game, "training reads a race's highest level past DS:82d4's table "
                                   "(dkk 49cb:43b6)");
        *at = limit <= (uint8_t)level;
        return true;
    }
    uint8_t strength = cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, COK_SCORE_STRENGTH);
    *at = cok_field_u8(L, c, COK_FIELD_RACE) == 5 && (class_ == 2 || class_ == 4) &&
          (level == 7 || (level == 6 && strength == 17) || (level == 5 && strength < 17));
    return true;
}

/* 4def:4d9e on character (dkk 49cb:43b6, as the levels section words and
 * bounds it); creating is DS:4b58. Returns false when the run must stop;
 * *none is DS:713f, set when no class could rise. */
static bool train(cok_adventure *game, cok_character *character, bool creating, bool *none)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_rules_levels *V = &cok_game_rules(game)->levels;
    uint8_t *c = character->record;
    if (!V->free_training) game->free_training = false; /* dkk 49cb:43b6: DS:c46a */
    bool free_training = game->free_training;
    *none = false;
    if (cok_field_u8(L, c, COK_FIELD_STATUS) != 0 && !free_training) {
        /* Creating too, though a new character is always able to act. */
        cok_adventure_notice(game, V->conscious, 14);
        return true;
    }
    uint8_t present = 0, trainable = 0, hall = (uint8_t)cok_ecl_var(&game->vm, COK_VAR_HALL);
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
    int32_t experience = experience_of(L, c);
    for (unsigned i = 0; i < 8; ++i) {
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i) <= 0) continue;
        present = (uint8_t)(present + class_bit(game, i));
        level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        bool limited;
        if (!at_limit(game, c, i, level, &limited)) return false;
        if (cok_train_row(c, cok_game_rules(game), i, &row)) {
            stale = false;
        } else if (first) {
            if (creating || V->stale_row == 0)
                return undefined(game, "the experience table of a druid, a paladin or a knight "
                                       "of no order is 6346:371e's uninitialized local (dkk "
                                       "65cd:382c's), which creation leaves or the port does "
                                       "not know");
            row = V->stale_row;
            stale = true;
        }
        first = false;
        if (limited) continue;
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
            cok_field_set32(L, c, COK_FIELD_EXPERIENCE, (uint32_t)experience);
        }
        trainable = (uint8_t)(trainable + class_bit(game, i));
        if (!need_for(game, row, level + 2, &next)) return false;
        if (next > 0 && experience >= next && next > cap) cap = next - 1;
    }
    if (!creating && !stale) {
        /* The second pass reads the last class's row and level for every
         * class (4def:4d9e); from row 0x74 it changes nothing. */
        int32_t best = 0, need, next;
        for (unsigned i = 0; i < 8; ++i) {
            if ((class_bit(game, i) & trainable) == 0) continue;
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
        cok_field_set32(L, c, COK_FIELD_EXPERIENCE, (uint32_t)experience);
    }
    if (!free_training) {
        if ((present & hall) == 0 && !creating) {
            cok_adventure_notice(game, V->wrong_hall, 14);
            return true;
        }
        if ((trainable & hall) == 0) {
            *none = true;
            if (!creating) cok_adventure_notice(game, V->no_experience, 14);
            return true;
        }
    }
    uint8_t mask = free_training ? trainable : (uint8_t)(trainable & hall);
    if (!creating) {
        clear_cells(game, 1, 1, 0x26, 0x16);
        cok_item_draw_name(game, c, 4, 4, false);
        draw(game, V->will_become, cok_field_u8_at(L, c, COK_FIELD_NAME, 0) + 4, 4);
        int y = 4;
        for (unsigned i = 0; i < 8; ++i) {
            if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i) <= 0 ||
                (class_bit(game, i) & mask) == 0)
                continue;
            ++y;
            char name[256], text[300];
            const cok_data_segment *D = cok_game_rules(game)->data;
            cok_data_string(D, (uint16_t)cok_data_offset(D, COK_DATA_CLASS_NAMES, i), name);
            snprintf(text, sizeof text, "%s%u %s", y == 5 ? V->a_level : V->and_level,
                     (uint8_t)(cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, i) + 1), name);
            draw(game, text, 6, y);
        }
        int answer = cok_camp_ask(game, V->question, 13, V->question_yes);
        if (answer != 'Y') return answer >= 0;
        cok_camp_notice(game, V->congratulations);
    }
    uint8_t count = 0;
    int8_t mage = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5),
           ranger = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 4);
    cok_field_set8(L, c, COK_FIELD_USABLE_CLASSES, 0);
    for (unsigned i = 0; i < 8; ++i) {
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i) <= 0) continue;
        ++count;
        if ((class_bit(game, i) & mask) == 0) continue;
        cok_field_add8_at(L, c, COK_FIELD_CLASS_LEVEL, i, 1);
        /* SPREAD_LEVELS and SPREAD (+0xd8, +0xd9), which nothing else
         * writes. */
        uint8_t over = cok_field_present(L, COK_FIELD_SPREAD_LEVELS)
                           ? cok_field_u8(L, c, COK_FIELD_SPREAD_LEVELS)
                           : 0;
        if (over > 0) {
            uint8_t spread = cok_field_u8(L, c, COK_FIELD_SPREAD);
            cok_field_set8(L, c, COK_FIELD_SPREAD, (uint8_t)(spread - spread / over));
            cok_field_add8(L, c, COK_FIELD_SPREAD_LEVELS, -1);
        }
    }
    if (!levels(game, character)) return false;
    /* A ranger above level 8 every time it trains, or (dkk) when it rises. */
    int8_t ranger_now = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 4);
    if (!creating && (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > mage ||
                      (ranger_now > 8 && (!V->ranger_rises || ranger_now > ranger)))) {
        if (cok_magic_learn(game, character) < 0) return false;
    } else if (creating) {
        /* The spells a mage of each level knows when it is made (the
         * creation section's: by level, and in dkk 49cb:43b6 by order). */
        const cok_rules_creation *C = &cok_game_rules(game)->creation;
        uint8_t mage_level = cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 5),
                order = cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER);
        for (size_t k = 0; k < C->created_count; ++k) {
            const cok_rules_created_spells *e = &C->created[k];
            if (e->level != mage_level || (e->order != 0 && e->order != order)) continue;
            for (size_t n = 0; n < 4 && e->spells[n] != 0; ++n)
                cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, e->spells[n], 1);
            break;
        }
    }
    if (cok_field_s8(L, c, COK_FIELD_HIGHEST_LEVEL) >
            cok_character_change_level(cok_game_rules(game), c) &&
        !cok_train_hit_points(game, c, mask, count))
        return false;
    return set_may_train(game, c);
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

bool cok_train_hit_die(const uint8_t *c, const cok_rules *rules, unsigned class_, int8_t *bonus)
{
    const cok_character_layout *L = rules->character;
    uint8_t con = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION);
    if (con == 3) *bonus = -2;
    else if (con >= 4 && con <= 6) *bonus = -1;
    else if (con >= 7 && con <= 14) *bonus = 0;
    else if (con == 15) *bonus = 1;
    else if (con == 16) *bonus = 2;
    else if (con >= 17 && con <= 19)
        /* Fighters, rangers and knights (dkk 65cd:3c02 paladins too). */
        *bonus = class_ < 8 && (rules->characters.fighter_classes >> class_ & 1) != 0
                     ? (int8_t)(con - 14)
                     : 2;
    else return false;
    return true;
}

bool cok_train_promote(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7);
    int32_t need;
    if (!need_for(game, cok_field_u8(L, c, COK_FIELD_KNIGHT_ORDER) == 1 ? 4 : 5, level, &need))
        return false;
    if (need > experience_of(L, c)) {
        cok_field_add8_at(L, c, COK_FIELD_CLASS_LEVEL, 7, -1);
        /* dkk 49cb:4da1: from level 10, 2 and no constitution. */
        uint8_t fixed = cok_game_rules(game)->levels.promote_fixed;
        bool by_level = fixed == 0 || level < (int8_t)fixed;
        if (by_level && level + 1 == 0) {
            cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                               "hit points divided by a knight level of 0 (4def:567f)");
            return false;
        }
        uint8_t share = 2;
        if (by_level) share = (uint8_t)(cok_field_u8(L, c, COK_FIELD_BASE_HIT_POINTS) / (level + 1));
        cok_field_set8(L, c, COK_FIELD_BASE_HIT_POINTS,
                       (uint8_t)(cok_field_u8(L, c, COK_FIELD_BASE_HIT_POINTS) - share));
        int8_t bonus = 0;
        const uint8_t *s = game->vm.character;
        if (by_level && (s == NULL || !cok_train_hit_die(s, cok_game_rules(game), 7, &bonus)))
            return undefined(game, "4def:257c's constitution bonus is uninitialized for a "
                                   "constitution outside 3-19, or none selected (4def:567f)");
        cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS,
                       (uint8_t)(cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) -
                                 (uint8_t)(share + bonus)));
        if (cok_field_u8(L, c, COK_FIELD_HIT_POINTS) >=
            cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))
            cok_field_set8(L, c, COK_FIELD_HIT_POINTS,
                           cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
    }
    cok_field_add8(L, c, COK_FIELD_KNIGHT_ORDER, 1);
    return levels(game, character) && set_may_train(game, c);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (!cok_start_pick(game)) return;
    cok_character *character = selected(game);
    if (character == NULL) {
        undefined(game, "Knight Change Classes with no character selected reads through NULL "
                        "(4def:5812)");
        return;
    }
    uint8_t *c = character->record;
    cok_adventure_screen(game, COK_SCREEN_OPEN); /* 1128:0000 */
    uint8_t order = cok_field_u8(L, c, COK_FIELD_KNIGHT_ORDER);
    if (cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) == 0 ||
        (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > 0 && order == 3))
        return;
    const cok_rules_levels *V = &cok_game_rules(game)->levels;
    const char *menu = V->change_always ? "Change Exit" : "Exit";
    char name[16], text[64];
    uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(name, c + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    name[length] = '\0';
    draw(game, name, 19 - (int)(stored >> 1), 3);
    draw(game, "is petitioning to become a", 7, 5);
    /* For a knight of no order the suffix is a local never set, which the
     * start menu's freeing of its rows (4def:0381) leaves empty. */
    if (order != 1 && order != 2 && V->no_order == NULL) {
        undefined(game, "a knight of no order petitions for an uninitialized local's order "
                        "(dkk 49cb:4ea4)");
        return;
    }
    snprintf(text, sizeof text, "%s%s", V->knight_of,
             order == 1 ? "sword" : order == 2 ? "rose" : V->no_order);
    draw(game, text, 10, 7);
    /* A knight of no order skips to the menu (4def:5aa0). */
    if (order == 1 || order == 2) {
        bool sword = order == 1;
        int32_t need;
        if (!need_for(game, sword ? 4 : 5, sword ? 3 : 4, &need)) return;
        int base = sword ? 19 : 20;
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) < (sword ? 3 : 4) ||
            experience_of(L, c) < need) {
            snprintf(text, sizeof text, "%s%s", name, V->inexperienced[sword ? 0 : 1]);
            centred(game, text, base, 21, 9, stored);
        } else {
            bool qualifies = true;
            for (size_t k = 0; k < 6; ++k)
                if (cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, k) <
                    cok_data_u8_at2(cok_game_rules(game)->data, COK_DATA_KNIGHT_SCORES,
                                    sword ? 0 : 1, k))
                    qualifies = false;
            if (!qualifies) {
                snprintf(text, sizeof text, "%s does not qualify.", name);
                centred(game, text, base, 18, 9, stored);
            } else {
                snprintf(text, sizeof text, "%s may become a", name);
                centred(game, text, 19, 13, 9, stored);
                snprintf(text, sizeof text, "%s%s", V->knight_of, V->granted[sword ? 0 : 1]);
                draw(game, text, 10, 11);
                menu = "Change Exit";
            }
        }
    }
    for (;;) {
        bool special;
        int key;
        if (cok_game_rules(game)->menus.routine == COK_MENUS_INDEXED) {
            /* dkk 49cb:5261-52b5: by index (6a69:00cb with 1, 0f1b over
             * DS:3361 with no prompt, keypad or picture) until Change (0),
             * Exit (1) or Escape; other special keys and indexes ask
             * again. */
            cok_menu_items items;
            cok_menu_items_upto(&items, 1);
            key = cok_camp_pick_menu(game, "", menu, &items, false, false, &special);
            if (key < 0) return;
            if (special && key == 0x1b) return;
            if (special || key > 1) continue;
            key = key == 0 ? 'C' : 'E';
        } else {
            key = cok_camp_menu(game, "", menu, false, false, &special);
        }
        if (key < 0) return;
        /* The special flag is not tested: F9, scan code 0x43, is C (dkk
         * DS:c0f8: special keys do nothing). */
        if (special && V->specials_apart) continue;
        if (key == 'C') {
            cok_train_promote(game, character);
            return;
        }
        if (key == 'E' || key == 0) return;
    }
}

/* Print text in the combat panel's lower window (dkk 14e4:04ab with 0x17,
 * 0x0e, 0x26, 0x15 and 15), then wait for the results' key (14e4:096a
 * with DS:0868) and clear the window (110f:0584). */
static void panel_text(cok_adventure *game, const char *text)
{
    cok_text_window window = {0x17, 0x0e, 0x26, 0x15};
    cok_adventure_print(game, text, window, 15, true);
    cok_adventure_alert(game, cok_game_rules(game)->combat.results_key, 13);
    clear_cells(game, window.left, window.top, window.right, window.bottom);
}

bool cok_train_aging(cok_adventure *game)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_rules_levels *V = &rules->levels;
    if (!game->effects.aging) return true;
    game->effects.aging = false; /* DS:c463, at the end */
    if (V->ages == NULL) return true;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *character = game->party.members[i];
        uint8_t *c = character->record;
        if (cok_field_s8(L, c, COK_FIELD_STATUS) >= 6 ||
            cok_character_combat(character)->not_party == 1)
            continue;
        int8_t race = cok_field_s8(L, c, COK_FIELD_RACE);
        if (race < 0 || race >= V->age_races) continue;
        uint16_t age = cok_field_u16(L, c, COK_FIELD_AGE);
        if (age <= V->ages[race].old) continue;
        if (age <= V->ages[race].dies) {
            if (!cok_arena_say(game, character, V->very_old, 10, true)) return false;
            continue;
        }
        char name[16], text[128];
        uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
        size_t length = stored > 15 ? 15 : stored;
        memcpy(name, c + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
        name[length] = '\0';
        clear_cells(game, 0x17, 0x0e, 0x26, 0x15);
        snprintf(text, sizeof text, "%s%s%s", V->strain, name, V->strain_end);
        panel_text(game, text);
        snprintf(text, sizeof text, "%s%s",
                 cok_field_s8(L, c, COK_FIELD_GENDER) == 0 ? V->he : V->she, V->last_breath);
        panel_text(game, text);
        if (!cok_combat_kill(game, character, 8, V->died)) return false;
        return undefined(game, "the aging's count of those who witness a death starts from an "
                               "uninitialized local, [bp-0x10] (dkk 3e05:0264)");
    }
    return game->vm.status == COK_ECL_OK;
}

/* Death Knights' level drain. */

/* dkk 580d:3454: the most hit points c's levels give: for each class with a
 * level, below its top (DS:72a4) its hit die's bonus (65cd:3c02) plus 8
 * (fighters and paladins 10, a mage's 4, a thief's 6) times the level (a
 * cleric of a deity below 5, a knight and a ranger one more), at or above
 * it a fixed sum by the bonus and the levels past the top (a druid's 0x70),
 * words, divided by the classes counted, as a byte. */
bool cok_train_most_hit_points(cok_adventure *game, const uint8_t *c, uint8_t *most)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    uint16_t sum = 0;
    uint8_t count = 0;
    for (unsigned k = 0; k < 8; ++k) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, k);
        if (level <= 0) continue;
        int8_t bonus;
        if (!cok_train_hit_die(c, rules, k, &bonus))
            return undefined(game, "a constitution past 65cd:3c02's cases gives an uninitialized "
                                   "bonus (dkk 580d:3454)");
        int b = bonus;
        ++count;
        if ((uint8_t)level < top_level(game, k)) {
            int times = level;
            int die = k == 2 || k == 3 || k == 7 ? 10 : k == 5 ? 4 : k == 6 ? 6 : 8;
            if ((k == 0 && cok_field_s8(L, c, COK_FIELD_DEITY) < 5) || k == 7 || k == 4) ++times;
            sum = (uint16_t)(sum + (uint16_t)((b + die) * times));
        } else {
            int extra = (uint8_t)(level - top_level(game, k) + 1);
            switch (k) {
            case 0: sum = (uint16_t)(sum + b * 9 + 0x48 + extra * 2); break;
            case 1: sum = (uint16_t)(sum + 0x70); break;
            case 4: sum = (uint16_t)(sum + b * 0xb + 0x58 + extra * 2); break;
            case 5: sum = (uint16_t)(sum + b * 0xb + 0x2c + extra); break;
            case 6: sum = (uint16_t)(sum + b * 10 + 0x3c + extra * 2); break;
            default: sum = (uint16_t)(sum + b * 9 + 0x5a + extra * 3); break;
            }
        }
    }
    *most = (uint8_t)(count == 0 ? 0 : sum / count);
    return true;
}

/* dkk 580d:3193 losing a level of the classes in mask, among count classes:
 * the dice and constitution bonuses as gaining one rolls them (or the fixed
 * sums at the top), the sum (at least 1) and the bonus divided by count;
 * the sum off the base hit points (+0x6c, to 0), both off the most (no
 * lower than what 580d:3454 gives for the levels now, nor 0), the hit points
 * keeping their distance below it, to 0. None counted: all three 0. */
static bool lose_hit_points(cok_adventure *game, uint8_t *c, uint8_t mask, uint8_t count)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_data_segment *D = rules->data;
    uint8_t sum = 0, con = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        if (level <= 0 || (class_bit(game, i) & mask) == 0) continue;
        if ((uint8_t)level < top_level(game, i)) {
            bool druid = i == 0 && cok_field_s8(L, c, COK_FIELD_DEITY) > 4;
            uint8_t dice = level > 1 ? 1 : druid ? 1 : cok_data_u8_at(D, COK_DATA_LEVEL_DICE, i);
            uint8_t a = cok_dice(&game->vm.seed, dice, cok_data_u8_at(D, COK_DATA_DIE_SIDES, i));
            uint8_t b = cok_dice(&game->vm.seed, dice, cok_data_u8_at(D, COK_DATA_DIE_SIDES, i));
            sum = (uint8_t)(sum + (a > b ? a : b));
            con = (uint8_t)(con + dice * cok_train_constitution(c, rules, i));
        } else if (rules->levels.top_hit_points[i] != 0) {
            sum = rules->levels.top_hit_points[i];
        }
    }
    if (count == 0) {
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 0);
        cok_field_set8(L, c, COK_FIELD_BASE_HIT_POINTS, 0);
        cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, 0);
        return true;
    }
    sum = (uint8_t)(sum / count);
    if (sum == 0) sum = 1;
    con = (uint8_t)(con / count);
    uint8_t max = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS);
    uint8_t lacking = (uint8_t)(max - cok_field_u8(L, c, COK_FIELD_HIT_POINTS));
    uint8_t base = cok_field_u8(L, c, COK_FIELD_BASE_HIT_POINTS);
    cok_field_set8(L, c, COK_FIELD_BASE_HIT_POINTS, base > sum ? (uint8_t)(base - sum) : 0);
    uint8_t loss = (uint8_t)(sum + con), most;
    if (!cok_train_most_hit_points(game, c, &most)) return false;
    if ((int)max - (int)loss > (int)most) loss = (uint8_t)(max - most);
    max = max < loss ? 0 : (uint8_t)(max - loss);
    cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, max);
    cok_field_set8(L, c, COK_FIELD_HIT_POINTS, max < lacking ? 0 : (uint8_t)(max - lacking));
    return true;
}

/* dkk 504d:1288: the memorized spells c's spells a day no longer hold are
 * forgotten: those memorized first (below 0x80), counted by spell list and
 * level (the spell table's columns 0 and 1) up to the spells a day, a mage
 * keeping one or two more of the mage list by its order's moon (var
 * 0x4cf8 + order 1 or 2), then those to memorize (0x80 and up). */
static bool forget_over(cok_adventure *game, uint8_t *c)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_data_segment *D = rules->data;
    if (!cok_field_has_spells(L, c)) return true;
    uint8_t counts[33] = {0};
    int extra = -1; /* local_23[0], never set for one without a mage level */
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0) {
        uint16_t moon = cok_ecl_var_at(&game->vm, COK_VAR_ORDER_MOON,
                                       cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER));
        extra = moon == 1 ? 1 : moon == 2 ? 2 : 0;
    }
    size_t at = cok_field_offset(L, COK_FIELD_MEMORIZED), n = cok_field_size(L, COK_FIELD_MEMORIZED);
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < n; ++i) {
            uint8_t s = c[at + i];
            if (pass == 0 ? s == 0 || s >= 0x80 : s < 0x80) continue;
            uint8_t id = s & 0x7f, list = 0, level = 0;
            cok_data_byte(D, (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, id, 0), &list);
            cok_data_byte(D, (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, id, 1), &level);
            /* Counted at list * 8 + level of the local's 33 bytes and the
             * block's +0xbb (the spells a day from list 0's level 1 on):
             * a level 0 spell's at the list before's level 8. */
            int index = (int8_t)list * 8 + (int8_t)level;
            if (index < 1 || index > 32) {
                char text[120];
                snprintf(text, sizeof text, "memorized spell %u's list %u and level %u count "
                         "outside the local (dkk 504d:1288)", id, list, level);
                return undefined(game, text);
            }
            uint8_t a_day = c[cok_field_offset(L, COK_FIELD_SPELLS_A_DAY) + (size_t)index - 1];
            if (counts[index] < a_day) {
                ++counts[index];
            } else if (pass == 1 || a_day == 0) {
                c[at + i] = 0;
            } else if (extra < 0 && list == 3) {
                return undefined(game, "a mage spell over the count without a mage level reads "
                                       "an uninitialized local (dkk 504d:1288)");
            } else if (extra == 0 || list != 3) {
                c[at + i] = 0;
            } else {
                --extra;
            }
        }
    return true;
}

bool cok_train_drain(cok_adventure *game, cok_character *character, uint8_t drained)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    if (!cok_arena_say(game, character, "gets drained", 10, false)) return false; /* DS:1ebd */
    cok_adventure_wait(game, game->speed * 100u);                                /* 14e4:0b56 */
    int k = -1; /* kept from one level to the next, as the local is */
    for (unsigned n = 1; n <= drained; ++n) {
        uint8_t count = 0;
        int8_t best = 0;
        for (unsigned i = 0; i < 8; ++i) {
            int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
            if (level > 0 || cok_field_s8_at(L, c, COK_FIELD_UNDRAINED_LEVEL, i) > 0) ++count;
            if (level > best) {
                best = level;
                k = (int)i;
            } else if (level == best && best != 0) {
                /* The class whose level asks the more experience. */
                uint8_t row_k, row_i;
                int32_t xp_k, xp_i;
                if (!cok_train_row(c, rules, (unsigned)k, &row_k) ||
                    !cok_train_row(c, rules, i, &row_i))
                    return undefined(game, "a class with no experience row reads 65cd:382c's "
                                           "uninitialized local (dkk 504d:156d)");
                if (!need_for(game, row_k, best, &xp_k) || !need_for(game, row_i, level, &xp_i))
                    return false;
                if (xp_i > xp_k) k = (int)i;
            }
        }
        if (k < 0)
            return undefined(game, "a record with no level drains the class of a local never "
                                   "set (dkk 504d:15f0)");
        if (cok_field_s8_at(L, c, COK_FIELD_UNDRAINED_LEVEL, (size_t)k) <= best)
            cok_field_set8_at(L, c, COK_FIELD_UNDRAINED_LEVEL, (size_t)k, (uint8_t)best);
        uint32_t xp = cok_field_u32(L, c, COK_FIELD_EXPERIENCE);
        if (signed_long(cok_field_u32(L, c, COK_FIELD_UNDRAINED_EXPERIENCE)) <= signed_long(xp))
            cok_field_set32(L, c, COK_FIELD_UNDRAINED_EXPERIENCE, xp);
        uint8_t row;
        if (!cok_train_row(c, rules, (unsigned)k, &row))
            return undefined(game, "a class with no experience row reads 65cd:382c's "
                                   "uninitialized local (dkk 504d:1664)");
        if (cok_field_u8(L, c, COK_FIELD_UNDRAINED_MAX_HIT_POINTS) <=
            cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))
            cok_field_set8(L, c, COK_FIELD_UNDRAINED_MAX_HIT_POINTS,
                           cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
        /* The experience halfway into the level it falls to (a LongInt
         * division by 2, 1a07:0a2f). */
        int32_t low = 0, high = 0;
        if (best == 2) {
            if (!need_for(game, row, 2, &high)) return false;
        } else if (best != 1) {
            if (!need_for(game, row, best - 1, &low) || !need_for(game, row, best, &high))
                return false;
        }
        cok_field_set32(L, c, COK_FIELD_EXPERIENCE,
                        (uint32_t)(low + (int32_t)((uint32_t)high - (uint32_t)low) / 2));
        cok_field_set8_at(L, c, COK_FIELD_CLASS_LEVEL, (size_t)k, (uint8_t)(best - 1));
        if (!lose_hit_points(game, c, class_bit(game, (unsigned)k), count) ||
            !levels(game, character) || !forget_over(game, c))
            return false;
    }
    bool any = false;
    for (unsigned i = 0; i < 8; ++i)
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i) > 0) any = true;
    if (!any || cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) == 0)
        return cok_combat_kill(game, character, 8, "dies from drainning"); /* DS:1ed2 */
    if (cok_field_u8(L, c, COK_FIELD_HIT_POINTS) == 0)
        return cok_combat_kill(game, character, 4, "collapses from drainning"); /* DS:1ee7 */
    return true;
}

bool cok_train_restore(cok_adventure *game, cok_character *character)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    uint8_t best = 0x11, lost = 0, drained = 0;
    int32_t least = 3500000; /* DS:7a6c */
    for (unsigned k = 0; k < 8; ++k)
        if (cok_field_s8_at(L, c, COK_FIELD_UNDRAINED_LEVEL, k) > 0) ++drained;
    for (unsigned k = 0; k < 8; ++k) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, k);
        int8_t was = cok_field_s8_at(L, c, COK_FIELD_UNDRAINED_LEVEL, k);
        if (level >= was) continue;
        lost = (uint8_t)(lost + (uint8_t)(was - level));
        uint8_t row;
        int32_t need;
        if (!cok_train_row(c, rules, k, &row))
            return undefined(game, "a class with no experience row reads 65cd:382c's "
                                   "uninitialized local (dkk 5dc5:23d3)");
        if (!need_for(game, row, level + 1, &need)) return false;
        if (need <= least) {
            least = need;
            best = (uint8_t)k;
        }
    }
    if (best != 0x11) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, best);
        uint8_t most = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS);
        uint8_t was = cok_field_u8(L, c, COK_FIELD_UNDRAINED_MAX_HIT_POINTS);
        if (most < was) {
            /* The levels lost, a byte, can wrap to 0 (two classes 128
             * below): the division overflows. */
            if (lost == 0)
                return undefined(game, "the most hit points lacking are divided by the levels "
                                       "lost, a byte wrapped to 0: runtime error 200 (dkk "
                                       "5dc5:248c)");
            uint8_t gain = (uint8_t)((was - most) / lost), con = 0;
            if ((uint8_t)level + 1 < top_level(game, best)) {
                int8_t bonus;
                if (!cok_train_hit_die(c, rules, best, &bonus))
                    return undefined(game, "a constitution past 65cd:3c02's cases gives an "
                                           "uninitialized bonus (dkk 5dc5:24c3)");
                if (drained == 0)
                    return undefined(game, "the hit die's bonus is divided by no drained class: "
                                           "runtime error 200 (dkk 5dc5:24cb)");
                con = (uint8_t)(bonus / drained);
            }
            cok_field_set8(L, c, COK_FIELD_HIT_POINTS,
                           (uint8_t)(cok_field_u8(L, c, COK_FIELD_HIT_POINTS) + gain));
            cok_field_set8(L, c, COK_FIELD_BASE_HIT_POINTS,
                           (uint8_t)(cok_field_u8(L, c, COK_FIELD_BASE_HIT_POINTS) + gain - con));
            cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, (uint8_t)(most + gain));
        }
        cok_field_set8_at(L, c, COK_FIELD_CLASS_LEVEL, best, (uint8_t)(level + 1));
        if (!levels(game, character)) return false;
        uint32_t xp = cok_field_u32(L, c, COK_FIELD_EXPERIENCE);
        uint32_t before = cok_field_u32(L, c, COK_FIELD_UNDRAINED_EXPERIENCE);
        cok_field_set32(L, c, COK_FIELD_EXPERIENCE,
                        lost == 1 && signed_long(xp) < signed_long(before) ? before
                                                                           : (uint32_t)least);
    }
    return set_may_train(game, c);
}
