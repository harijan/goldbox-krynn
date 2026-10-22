#include "modify.h"

#include "camp.h"
#include "sheet.h"
#include "start.h"
#include "train.h"

#include <stdio.h>
#include <string.h>

/* The keypad as directions (DS:2010), from '1'. */
static const uint8_t keypad_scans[9] = {0x4f, 0x50, 0x51, 0x4b, 0x20, 0x4d, 0x47, 0x48, 0x49};

/* The byte at DS:offset in the game's data segment, 0 where it holds
 * none. */
static uint8_t ds(const cok_data_segment *D, uint16_t offset)
{
    uint8_t byte = 0;
    cok_data_byte(D, offset, &byte);
    return byte;
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

uint8_t cok_modify_least(const uint8_t *c, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    uint8_t levels = 0, classes = 0, con = 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t level = cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        if ((int8_t)level <= 0) continue;
        levels = (uint8_t)(levels + level);
        ++classes;
        if (i == 4 || i == 7) ++levels;
        con = (uint8_t)(con + (uint8_t)cok_train_constitution(c, rules, i));
    }
    int bonus = (int8_t)con;
    if (bonus < 0 && levels <= classes + -bonus) return 1;
    return (uint8_t)((levels + bonus) / classes);
}

bool cok_modify_most(const uint8_t *c, const cok_rules *rules, const uint8_t *selected,
                     uint8_t *most)
{
    const cok_character_layout *L = rules->character;
    const cok_data_segment *D = rules->data;
    uint8_t sum = 0, classes = 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t level = cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        if ((int8_t)level <= 0) continue;
        int8_t b;
        if (!cok_train_hit_die(selected, rules, i, &b)) return false;
        ++classes;
        uint8_t top = ds(D, (uint16_t)cok_data_offset(D, COK_DATA_TOP_LEVELS, i));
        if (level < top) {
            int dice;
            switch (i) {
            case 0:
                dice = (cok_field_s8(L, c, COK_FIELD_DEITY) <= 4 ? level + 1 : level) * (b + 8);
                break;
            case 1: dice = level * (b + 8); break;
            case 2: case 3: dice = level * (b + 10); break;
            case 7: dice = (level + 1) * (b + 10); break;
            case 4: dice = (level + 1) * (b + 8); break;
            case 5: dice = level * (b + 4); break;
            default: dice = level * (b + 6); break;
            }
            sum = (uint8_t)(sum + dice);
        } else {
            /* Set, not added: the classes before are lost. */
            uint8_t x = (uint8_t)(level - top + 1);
            static const uint8_t base[8] = {0x48, 0x70, 0x5a, 0x5a, 0x58, 0x2c, 0x3c, 0x5a};
            static const uint8_t times[8] = {2, 0, 3, 3, 2, 1, 2, 3};
            sum = (uint8_t)(x * times[i] + base[i]);
        }
    }
    *most = (uint8_t)(sum / classes);
    return true;
}

static bool most(cok_adventure *game, const uint8_t *c, uint8_t *value)
{
    if (cok_game_rules(game)->creation.most == COK_MOST_DEATH_KNIGHTS)
        return cok_train_most_hit_points(game, c, value); /* dkk 580d:3454 */
    if (cok_modify_most(c, cok_game_rules(game), game->vm.character, value)) return true;
    return undefined(game, "4def:257c's constitution bonus is uninitialized for a constitution "
                           "outside 3-19 (4def:48e9)");
}

/* The field being changed: 0-5 a score, 6 the name, 7 the hit points. */
typedef struct {
    cok_adventure *game;
    cok_character *character;
    uint8_t field, cursor;
} modify;

/* Draw the field (4def:278c), highlighted in light magenta or not. The
 * name shows the character at the cursor in white, a space as "%". */
static void draw_field(modify *m, bool highlight)
{
    const cok_character_layout *L = cok_game_rules(m->game)->character;
    cok_adventure *game = m->game;
    uint8_t *c = m->character->record;
    if (m->field <= 5) {
        cok_sheet_ability(game, c, m->field, highlight ? 13 : 10);
    } else if (m->field == 7) {
        cok_sheet_hit_points(game, c, 0x1f, 3, highlight, false);
    } else if (m->field == 6) {
        /* The name, a string[15], by the cursor and its length byte as the
         * editor indexes it. */
        const uint8_t *s = c + cok_field_offset(L, COK_FIELD_NAME);
        char name[16];
        size_t length = s[0] > 15 ? 15 : s[0];
        memcpy(name, s + 1, length);
        name[length] = '\0';
        if (highlight) {
            cok_text_clear(&game->screen, &game->font, 1, s[0] + 1, 1, 0); /* 1521:030a */
            cok_text_string(&game->screen, &game->font, name, 1, 1, 13, 0);
            char at[2] = {(char)s[m->cursor], '\0'};
            if (at[0] == ' ') at[0] = '%';
            cok_text_string(&game->screen, &game->font, at, m->cursor, 1, 15, 0);
        } else {
            cok_text_string(&game->screen, &game->font, name, 1, 1, 10, 0);
        }
    }
}

/* The race's limits (DS:39e0 + race * 16): strength's by gender. */
static uint8_t limit(const cok_character_layout *L, const cok_data_segment *D, const uint8_t *c,
                     unsigned offset)
{
    return ds(D, (uint16_t)cok_data_offset2(D, COK_DATA_SCORE_LIMITS,
                                            cok_field_s8(L, c, COK_FIELD_RACE), offset));
}

/* Whether c has a level of a class that counts as a fighter's
 * (characters.fighter_classes: fighters, rangers and knights; dkk
 * 49cb:25d4 paladins too). */
static bool fighter(const cok_rules *rules, const uint8_t *c)
{
    for (unsigned i = 0; i < 8; ++i)
        if ((rules->characters.fighter_classes >> i & 1) != 0 &&
            cok_field_s8_at(rules->character, c, COK_FIELD_CLASS_LEVEL, i) > 0)
            return true;
    return false;
}

/* Whether c's charisma is held at 17 (dkk 49cb:25d4: a hill dwarf
 * paladin, race 3 and class 3), in place of its race's least and most. */
static bool held_charisma(const cok_rules *rules, const uint8_t *c)
{
    const cok_character_layout *L = rules->character;
    return cok_field_u8(L, c, COK_FIELD_RACE) == rules->creation.charisma_race &&
           cok_field_u8(L, c, COK_FIELD_CLASS) == rules->creation.charisma_class;
}

static bool constitution_redraw(modify *m)
{
    m->field = 6;
    draw_field(m, false);
    m->field = 4;
    return true;
}

static bool subtract(modify *m)
{
    const cok_character_layout *L = cok_game_rules(m->game)->character;
    const cok_data_segment *D = cok_game_rules(m->game)->data;
    uint8_t *c = m->character->record;
    if (m->field <= 5) {
        unsigned a = m->field;
        uint8_t *score = c + cok_field_offset_at(L, COK_FIELD_SCORE, a);
        --*score;
        if (a == 0) {
            if (cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL) > 0) {
                cok_field_add8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL, -1);
                ++*score;
            } else if (*score < limit(L, D, c, cok_field_u8(L, c, COK_FIELD_GENDER))) {
                *score = limit(L, D, c, cok_field_u8(L, c, COK_FIELD_GENDER));
            }
        } else if (a == 5 && held_charisma(cok_game_rules(m->game), c)) {
            if (*score < 17) *score = 17;
        } else if (*score < limit(L, D, c, 4 + 2 * a)) {
            *score = limit(L, D, c, 4 + 2 * a);
        }
        uint8_t minimum = ds(D, (uint16_t)cok_data_offset2(D, COK_DATA_CLASS_MINIMUMS,
                                                           cok_field_s8(L, c, COK_FIELD_CLASS), a));
        if (*score < minimum) *score = minimum;
        if (a == 2 && cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1) > 0)
            cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
        if (a == 4) {
            uint8_t value;
            if (!most(m->game, c, &value)) return false;
            if (value < cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))
                cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, value);
            cok_field_set8(L, c, COK_FIELD_HIT_POINTS,
                           cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
            constitution_redraw(m);
        }
    } else if (m->field == 7) {
        cok_field_add8(L, c, COK_FIELD_MAX_HIT_POINTS, -1);
        uint8_t least = cok_modify_least(c, cok_game_rules(m->game));
        if (least > cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))
            cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, least);
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
    } else if (m->field == 6) {
        m->cursor =
            m->cursor == 1 ? cok_field_u8_at(L, c, COK_FIELD_NAME, 0) : (uint8_t)(m->cursor - 1);
    }
    return true;
}

static bool add(modify *m)
{
    const cok_character_layout *L = cok_game_rules(m->game)->character;
    const cok_data_segment *D = cok_game_rules(m->game)->data;
    uint8_t *c = m->character->record;
    if (m->field <= 5) {
        unsigned a = m->field;
        uint8_t *score = c + cok_field_offset_at(L, COK_FIELD_SCORE, a);
        ++*score;
        if (a == 0) {
            uint8_t high = limit(L, D, c, 2u + cok_field_u8(L, c, COK_FIELD_GENDER));
            if (*score > high) {
                if (*score > 18 && fighter(cok_game_rules(m->game), c) &&
                    limit(L, D, c, 4u + cok_field_u8(L, c, COK_FIELD_GENDER)) >
                    cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL))
                    cok_field_add8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL, 1);
                *score = high;
            }
        } else if (a == 5 && held_charisma(cok_game_rules(m->game), c)) {
            if (*score > 17) *score = 17;
        } else if (*score > limit(L, D, c, 5 + 2 * a)) {
            *score = limit(L, D, c, 5 + 2 * a);
        }
        if (a == 2 && cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1) > 0)
            cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
        if (a == 4) {
            uint8_t least = cok_modify_least(c, cok_game_rules(m->game));
            if (least > cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))
                cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, least);
            cok_field_set8(L, c, COK_FIELD_HIT_POINTS,
                           cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
            constitution_redraw(m);
        }
    } else if (m->field == 7) {
        cok_field_add8(L, c, COK_FIELD_MAX_HIT_POINTS, 1);
        uint8_t value;
        if (!most(m->game, c, &value)) return false;
        if (value < cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))
            cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, value);
        cok_field_set8(L, c, COK_FIELD_HIT_POINTS, cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
    } else if (m->field == 6) {
        m->cursor =
            m->cursor == cok_field_u8_at(L, c, COK_FIELD_NAME, 0) ? 1 : (uint8_t)(m->cursor + 1);
    }
    return true;
}

/* Keep (4def:35ea; dkk 49cb:25d4's): the spells a day for the wisdom,
 * SHARES (+0xe8) set where the creation section says, the hit points at
 * full less the constitution's average bonus, and the scores made the
 * base. The bonus's sum is a byte divided unsigned. */
static bool keep(modify *m)
{
    const cok_character_layout *L = cok_game_rules(m->game)->character;
    cok_adventure *game = m->game;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t *c = m->character->record;
    char error[300];
    if (!cok_character_cleric_spells(m->character, cok_game_rules(game), error, sizeof error))
        return undefined(game, error);
    if (cok_game_rules(game)->creation.keep_shares) cok_field_set8(L, c, COK_FIELD_SHARES, 1);
    uint8_t sum = 0, classes = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        if (level <= 0) continue;
        int8_t b;
        if (!cok_train_hit_die(game->vm.character, cok_game_rules(game), i, &b))
            return undefined(game, "4def:257c's constitution bonus is uninitialized for a "
                                   "constitution outside 3-19 (4def:35ea)");
        uint8_t top = ds(D, (uint16_t)cok_data_offset(D, COK_DATA_TOP_LEVELS, i));
        if ((uint8_t)level < top)
            sum = (uint8_t)(sum + b * (i == 4 ? level + 1 : level));
        else
            sum = (uint8_t)(sum + b * (top - 1));
        ++classes;
    }
    if (classes == 0) {
        cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                           "Keep divides by no classes (4def:36cb)");
        return false;
    }
    cok_field_set8(L, c, COK_FIELD_BASE_HIT_POINTS,
                   (uint8_t)(cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) - sum / classes));
    for (size_t a = 0; a < 6; ++a)
        cok_field_set8_at(L, c, COK_FIELD_SCORE_BASE, a, cok_field_u8_at(L, c, COK_FIELD_SCORE, a));
    cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE,
                   cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL));
    return true;
}

/* A special key (4def:2b1d; dkk 49cb:25d4's): Del in the name deletes at
 * the cursor; Down and Up move between the fields, wrapping; Home after a
 * click picks the field clicked. */
static void special_key(modify *m, int key)
{
    cok_adventure *game = m->game;
    uint8_t *s = m->character->record + cok_field_offset(cok_game_rules(game)->character,
                                                        COK_FIELD_NAME);
    if (key == 0x53 && m->field == 6 && s[0] > 1) {
        if (m->cursor == s[0]) {
            --s[0];
            m->cursor = s[0];
        } else {
            for (uint8_t i = m->cursor; i < s[0]; ++i) s[i] = s[i + 1];
            --s[0];
        }
    } else if (key == 0x50) {
        game->modify_field = 0xff;
        m->field = (uint8_t)(m->field == 7 ? 0 : m->field + 1);
    } else if (key == 0x48) {
        game->modify_field = 0xff;
        m->field = (uint8_t)(m->field == 0 ? 7 : m->field - 1);
    } else if (key == 0x47 && game->pointer.clicked && game->modify_field != 0xff) {
        /* 4def:2bc5: Home from a click picks the field it clicked
         * (the last one, for a click beside the fields). */
        m->field = game->modify_field;
    }
}

/* Backspace in the name: the characters from the cursor on move back one,
 * the length one less, the cursor no further than it; the cursor itself
 * does not move back. */
static void backspace(modify *m)
{
    uint8_t *s = m->character->record + cok_field_offset(cok_game_rules(m->game)->character,
                                                        COK_FIELD_NAME);
    if (m->cursor <= 1) return;
    uint8_t length = s[0];
    if (m->cursor <= length)
        for (uint8_t i = m->cursor; ; ++i) {
            s[i - 1] = s[i];
            if (i == length) break;
        }
    --s[0];
    if (m->cursor > s[0]) m->cursor = s[0];
}

/* A character typed in the name at the cursor, which moves on (not past
 * 15); typed at the end, the name grows by a space. */
static void type(modify *m, int key)
{
    uint8_t *s = m->character->record + cok_field_offset(cok_game_rules(m->game)->character,
                                                        COK_FIELD_NAME);
    if (m->cursor > 15) return;
    s[m->cursor] = (uint8_t)key;
    if (++m->cursor > 15) m->cursor = 15;
    if (m->cursor > s[0]) {
        ++s[0];
        s[m->cursor] = ' ';
    }
}

/* Exit: the scores, exceptional strength, maximum hit points and name
 * come back; the hit points become the maximum; +0x11c stays as a wisdom
 * changed it; the stats are recomputed. */
static void restore(modify *m, const uint8_t scores[6], uint8_t exceptional, uint8_t hit_points,
                    const uint8_t name[16])
{
    cok_adventure *game = m->game;
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = m->character->record, *s = c + cok_field_offset(L, COK_FIELD_NAME);
    for (size_t a = 0; a < 6; ++a) cok_field_set8_at(L, c, COK_FIELD_SCORE, a, scores[a]);
    cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL, exceptional);
    cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, hit_points);
    cok_field_set8(L, c, COK_FIELD_HIT_POINTS, cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
    /* As strings: the bytes past the old name keep what was typed. */
    memcpy(s, name, (size_t)(name[0] > 15 ? 15 : name[0]) + 1);
    char error[300];
    if (!cok_character_stats(m->character, cok_game_rules(game), &game->item_types, error,
                             sizeof error))
        undefined(game, error);
}

/* The name's raw key (1614:025b; dkk 16ab:01d4): the keypad's digits and
 * \ as their scan codes (DS:2010; dkk DS:57c2), an extended key's scan
 * code, both special; Escape as 0. Returns -1 when input ended. */
static int name_key(const cok_keyboard *keys, bool *special)
{
    *special = false;
    int key = keys->read(keys->context);
    if (key < 0) return -1;
    if ((key >= '1' && key <= '9') || key == '\\') {
        key = key == '\\' ? 0x37 : keypad_scans[key - '1'];
        *special = true;
    } else if (key == 0) {
        key = keys->read(keys->context);
        if (key < 0) return -1;
        *special = true;
    }
    if (key == 0x1b) key = 0;
    return key;
}

/* Death Knights' loop (dkk 49cb:25d4), its menu by index (6a69:0f1b with
 * the keypad, "Modify : " and items 0-3: Add, Subtract, Keep, Exit). What
 * it does is the menu's last index ([bp-0x35]), which the name's keys do
 * not change: in the name, letters are typed (A, S, K and E too), Enter
 * and Escape do nothing, and the index left by the special key that moved
 * there, never 0-3, does nothing. Escape in the menu exits as Exit does. */
static void indexed(modify *m, const cok_keyboard *keys, const uint8_t scores[6],
                    uint8_t exceptional, uint8_t hit_points, const uint8_t name[16])
{
    cok_adventure *game = m->game;
    const cok_rules *rules = cok_game_rules(game);
    static const char items_text[] = "Add Subtract Keep Exit"; /* DS:3318 */
    int last = -1; /* never read before the first menu: field 0 first */
    for (;;) {
        bool special = false;
        int key;
        if (m->field == 6) {
            key = name_key(keys, &special);
            if (key < 0) return;
        } else {
            cok_menu_items items;
            cok_menu_items_upto(&items, 3); /* 6a69:00cb(3) */
            cok_adventure_log(game, "menu", items_text);
            key = cok_menu_pick(&game->screen, &game->font, rules->creation.modify_prompt,
                                items_text, &items, 13, 15, 10, true, &game->selected, keys,
                                &special);
            if (key < 0) return;
            game->menu_special = special; /* DS:c0f8 */
            last = key;
        }
        draw_field(m, false);
        if (special) {
            if (key == 0x1b) {
                restore(m, scores, exceptional, hit_points, name);
                return;
            }
            special_key(m, key);
        } else {
            if (m->field == 6) {
                if (key == 0x08) backspace(m);
                else if (key >= 0x20 && key <= 0x7a) type(m, key);
            }
            if (last == 1) {
                if (!subtract(m)) return;
            } else if (last == 0) {
                if (!add(m)) return;
            } else if (last == 3) {
                restore(m, scores, exceptional, hit_points, name);
                return;
            }
        }
        char error[300];
        if (!cok_character_stats(m->character, rules, &game->item_types, error, sizeof error)) {
            undefined(game, error);
            return;
        }
        if (!cok_sheet_figures(game, m->character)) return;
        draw_field(m, true);
        if (!special && last == 2) {
            keep(m);
            return;
        }
    }
}

static void modify_sheet(cok_adventure *game, cok_character *character);

void cok_modify(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (!cok_start_pick(game)) return;
    cok_character *character = cok_adventure_selected(game);
    if (character == NULL) {
        undefined(game, "Modify with no character selected reads through NULL (4def:28fa)");
        return;
    }
    uint8_t *c = character->record;
    /* The experience of a new character (the creation section's: dkk
     * 210003, 105002 or 70001), and the level it changed class at 0
     * where its records have one (dkk's have none). */
    const cok_rules_creation *C = &cok_game_rules(game)->creation;
    uint32_t experience = cok_field_u32(L, c, COK_FIELD_EXPERIENCE);
    bool allowed = false;
    for (size_t i = 0; i < C->fresh_count; ++i) allowed = allowed || experience == C->fresh[i];
    if (!allowed || (cok_field_present(L, COK_FIELD_CHANGE_LEVEL) &&
                     cok_field_u8(L, c, COK_FIELD_CHANGE_LEVEL) != 0)) {
        char text[48];
        uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
        snprintf(text, sizeof text, "%.*s can't be modified.", stored > 15 ? 15 : stored,
                 (const char *)c + cok_field_offset_at(L, COK_FIELD_NAME, 1));
        cok_adventure_log(game, "print", text);
        cok_adventure_notice(game, text, 14);
        return;
    }
    /* 4def:29d8: clicks on the sheet pick its fields (DS:7141), none yet
     * (DS:6e35); off again however Modify ends (3539, 35b6, 3729). */
    game->modify_field = 0xff;
    game->modifying = true;
    modify_sheet(game, character);
    game->modifying = false;
}

static void modify_sheet(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    if (!cok_sheet_draw(game)) return;
    uint8_t scores[6], exceptional = cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL),
        hit_points = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS), name[16];
    for (size_t a = 0; a < 6; ++a) scores[a] = cok_field_u8_at(L, c, COK_FIELD_SCORE, a);
    /* The name, a string[15], which the editor indexes by the cursor and
     * its length byte. */
    uint8_t *s = c + cok_field_offset(L, COK_FIELD_NAME);
    memcpy(name, s, sizeof name);
    modify m = {game, character, 7, 1};
    draw_field(&m, false);
    m.field = 0;
    draw_field(&m, true);
    cok_keyboard keys = cok_adventure_keyboard(game);
    if (cok_game_rules(game)->menus.routine == COK_MENUS_INDEXED) {
        indexed(&m, &keys, scores, exceptional, hit_points, name);
        return;
    }
    for (;;) {
        bool special = false;
        int key;
        if (m.field == 6) {
            /* The name reads keys raw (1614:025b), the keypad digits as
             * directions. */
            key = name_key(&keys, &special);
            if (key < 0) return;
        } else {
            key = cok_camp_menu(game, cok_game_rules(game)->creation.modify_prompt,
                                "Add Subtract Keep Exit", true, false, &special);
            if (key < 0) return;
        }
        draw_field(&m, false);
        bool done = false;
        if (special) {
            special_key(&m, key);
        } else if (key == 'S') {
            if (!subtract(&m)) return;
        } else if (key == 'A') {
            if (!add(&m)) return;
        } else if (key == 0x0d) {
            m.field = (uint8_t)(m.field == 7 ? 0 : m.field + 1);
        } else if (key == 0x08) {
            if (m.field == 6) backspace(&m);
        } else if (key >= 0x20 && key <= 0x7a) {
            if (m.field == 6) {
                type(&m, key);
            } else if (key == 'E') {
                done = true;
            }
        } else if (key == 0) {
            done = true;
        }
        if (done) {
            restore(&m, scores, exceptional, hit_points, name);
            return;
        }
        char error[300];
        if (!cok_character_stats(character, cok_game_rules(game), &game->item_types, error,
                                 sizeof error)) {
            undefined(game, error);
            return;
        }
        if (!cok_sheet_figures(game, character)) return;
        draw_field(&m, true);
        if (!special && key == 'K') {
            keep(&m);
            return;
        }
    }
}
