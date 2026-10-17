#include "create.h"

#include "camp.h"
#include "icon.h"
#include "items.h"
#include "roster.h"
#include "sheet.h"
#include "train.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The byte and the word at DS:offset in the game's data segment, 0 where
 * it holds none. */
static uint8_t ds(const cok_data_segment *D, uint16_t offset)
{
    uint8_t byte = 0;
    cok_data_byte(D, offset, &byte);
    return byte;
}

static uint16_t ds_word(const cok_data_segment *D, uint16_t offset)
{
    return (uint16_t)(ds(D, offset) | ds(D, (uint16_t)(offset + 1)) << 8);
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

void cok_create_icon(uint8_t *c, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    if (cok_field_u8(L, c, COK_FIELD_RACE) == 5)
        cok_field_set8(L, c, COK_FIELD_ICON_HEAD, 3);
    else if (cok_field_u8(L, c, COK_FIELD_GENDER) == 1)
        cok_field_set8(L, c, COK_FIELD_ICON_HEAD,
                       cok_field_u8(L, c, COK_FIELD_ICON_SIZE) == 2 ? 9 : 7);
    else
        cok_field_set8(L, c, COK_FIELD_ICON_HEAD,
                       cok_field_u8(L, c, COK_FIELD_ICON_SIZE) == 2 ? 5 : 0);
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 0) > 0)
        cok_field_set8(L, c, COK_FIELD_ICON_BODY, 0x17);
    else if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 4) > 0)
        cok_field_set8(L, c, COK_FIELD_ICON_BODY, 1);
    else if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > 0 ||
             cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 2) > 0)
        cok_field_set8(L, c, COK_FIELD_ICON_BODY, 0x18);
    else if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0)
        cok_field_set8(L, c, COK_FIELD_ICON_BODY, 0x1d);
    else
        cok_field_set8(L, c, COK_FIELD_ICON_BODY, 5);
}

bool cok_create_knight_items(cok_character *character, const cok_rules *rules)
{
    /* Type, name parts +0x31 and +0x30, weight. */
    static const struct { uint8_t type, part, armour; uint16_t weight; } items[3] = {
        {0x24, 0x24, 0x2f, 450}, {0x12, 0x12, 0, 50}, {0x25, 0x25, 0, 60},
    };
    for (size_t i = 0; i < 3; ++i) {
        uint8_t item[COK_ITEM_SIZE] = {0};
        item[0x2e] = items[i].type;
        item[0x31] = items[i].part;
        item[0x30] = items[i].armour;
        put16(item + 0x37, items[i].weight);
        /* The first heads the list; the others go after it. */
        if (!cok_character_insert_item(character, character->item_count == 0 ? 0 : 1, item))
            return false;
    }
    cok_field_set8(rules->character, character->record, COK_FIELD_ITEM_COUNT, 3);
    return true;
}

/* A list of creation (67b5:1368): heading, then each name after two
 * spaces, in cells 1-38 by 2-22, "Select" and "Exit". Returns 'S' with
 * *index the row picked (the first under the heading at first), 0 for
 * Escape or Exit, or -1 when input ended. Other keys pick again. */
static int pick(cok_adventure *game, const char *heading, char names[][41], size_t count,
                int *index)
{
    cok_menu_row rows[17];
    char text[17][41];
    rows[0] = (cok_menu_row){heading, true};
    cok_adventure_log(game, "list", heading);
    for (size_t i = 0; i < count && i < 16; ++i) {
        snprintf(text[i], sizeof text[i], "  %s", names[i]);
        rows[i + 1] = (cok_menu_row){text[i], false};
        cok_adventure_log(game, "item", text[i]);
    }
    cok_menu_style style = {"", "Select", 15, 10, 13, true};
    bool redraw = true;
    *index = 1;
    cok_keyboard keys = cok_adventure_keyboard(game);
    for (;;) {
        int key = cok_menu_rows(&game->screen, &game->font, rows, count + 1,
                                (cok_text_window){1, 2, 0x26, 0x16}, &style, &redraw, index,
                                &game->list_top, &game->selected, &keys);
        if (key <= 0) return key;
        if (key == 'S') {
            cok_adventure_log(game, "choice", names[*index - 1]);
            return key;
        }
    }
}

/* The name of entry index of table t (DS:base + stride * index). */
static void table_name(const cok_adventure *game, cok_data t, unsigned index, char out[41])
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    char name[256];
    if (!cok_data_string(D, (uint16_t)cok_data_offset(D, t, index), name)) name[0] = '\0';
    size_t n = strlen(name);
    if (n > 40) n = 40;
    memcpy(out, name, n);
    out[n] = '\0';
}

static void add_effect(cok_character *character, uint8_t id)
{
    cok_character_add_effect(character, id, 0, 0xff, false); /* 60f4:1285 */
}

/* Pick Race (4def:07a0): +0x5a, the icon's size and the race's effects. */
static int race(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    char names[7][41];
    for (unsigned i = 0; i < 7; ++i) table_name(game, COK_DATA_RACE_NAMES, i, names[i]);
    int index, key = pick(game, "Pick Race", names, 7, &index);
    if (key != 'S') return key;
    uint8_t *c = character->record;
    uint8_t race = (uint8_t)(index - 1);
    cok_field_set8(L, c, COK_FIELD_RACE, race);
    cok_field_set8(L, c, COK_FIELD_ICON_SIZE, race == 3 || race == 4 || race == 5 ? 1 : 2);
    if (race == 5) {
        add_effect(character, 0x5c);
        add_effect(character, 0x5e);
    } else if (race == 3 || race == 4) {
        add_effect(character, 0x5e);
        add_effect(character, 0x1a);
        add_effect(character, 0x2f);
    } else if (race == 0 || race == 1) {
        add_effect(character, 0x5f);
    } else if (race == 2) {
        add_effect(character, 0x12);
    }
    return key;
}

static int gender(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    char names[2][41];
    for (unsigned i = 0; i < 2; ++i) table_name(game, COK_DATA_GENDER_NAMES, i, names[i]);
    int index, key = pick(game, "Pick Gender", names, 2, &index);
    if (key == 'S') cok_field_set8(L, character->record, COK_FIELD_GENDER, (uint8_t)(index - 1));
    return key;
}

/* Pick Class (4def:0ae0): the classes of the race (DS:3a50, 15 bytes a
 * race: the count, then the classes), its levels and experience, and the
 * class-time THAC0, class bits, thief skills and saving throws. */
static int class_(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t *c = character->record;
    uint16_t base = (uint16_t)cok_data_offset2(D, COK_DATA_RACE_CLASSES,
                                               cok_field_s8(L, c, COK_FIELD_RACE), 0);
    uint8_t count = ds(D, base);
    char names[16][41];
    for (unsigned i = 1; i <= count && i <= 16; ++i)
        table_name(game, COK_DATA_CLASS_NAMES, ds(D, (uint16_t)(base + i)), names[i - 1]);
    int index, key = pick(game, "Pick Class", names, count, &index);
    if (key != 'S') return key;
    cok_field_set32(L, c, COK_FIELD_EXPERIENCE, 0);
    uint8_t class_id = ds(D, (uint16_t)(base + index));
    cok_field_set8(L, c, COK_FIELD_CLASS, class_id);
    /* Levels by class (+0xf9 cleric ... +0x100 knight) and experience. */
    static const struct { uint8_t levels[8]; uint16_t experience; } kinds[17] = {
        {{1, 0, 0, 0, 0, 0, 0, 0}, 0},    {{0}, 0},
        {{0, 0, 2, 0, 0, 0, 0, 0}, 2001}, {{0}, 0},
        {{0, 0, 0, 0, 2, 0, 0, 0}, 2251}, {{0, 0, 0, 0, 0, 2, 0, 0}, 5000},
        {{0, 0, 0, 0, 0, 0, 2, 0}, 1251}, {{0, 0, 0, 0, 0, 0, 0, 1}, 2500},
        {{1, 0, 1, 0, 0, 0, 0, 0}, 2001}, {{1, 0, 1, 0, 0, 1, 0, 0}, 5000},
        {{1, 0, 0, 0, 1, 0, 0, 0}, 2251}, {{1, 0, 0, 0, 0, 1, 0, 0}, 5000},
        {{1, 0, 0, 0, 0, 0, 1, 0}, 0},    {{0, 0, 1, 0, 0, 1, 0, 0}, 5000},
        {{0, 0, 1, 0, 0, 0, 1, 0}, 2001}, {{0, 0, 1, 0, 0, 1, 1, 0}, 5000},
        {{0, 0, 0, 0, 0, 1, 1, 0}, 5000},
    };
    if (class_id < 17) {
        memcpy(c + cok_field_offset(L, COK_FIELD_CLASS_LEVEL), kinds[class_id].levels, 8);
        cok_field_set32(L, c, COK_FIELD_EXPERIENCE, kinds[class_id].experience);
    }
    if (class_id == 4 || class_id == 10) add_effect(character, 0x69);
    if (class_id == 7) cok_field_set8(L, c, COK_FIELD_KNIGHT_ORDER, 1);
    char error[300];
    /* 66c2:0b9f's uninitialized local holds the normal colour, 0x0a, that
     * the class list's 67b5:1368 was given (4def:0ee7). Dexterity is 0. */
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 6) > 0 &&
        !cok_character_thief_skills(character, cok_game_rules(game), 0x0a, error, sizeof error))
        return undefined(game, error) ? 0 : -1;
    cok_field_set8(L, c, COK_FIELD_USABLE_CLASSES, 0);
    cok_field_set8(L, c, COK_FIELD_BASE_THAC0, 0);
    for (unsigned i = 0; i < 8; ++i) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        if (level <= 0) continue;
        uint8_t thac0 = ds(D, (uint16_t)cok_data_offset2(D, COK_DATA_THAC0, i, level));
        if (thac0 > cok_field_u8(L, c, COK_FIELD_BASE_THAC0))
            cok_field_set8(L, c, COK_FIELD_BASE_THAC0, thac0);
        cok_field_set8(L, c, COK_FIELD_USABLE_CLASSES,
                       (uint8_t)(cok_field_u8(L, c, COK_FIELD_USABLE_CLASSES) +
                                 ds(D, (uint16_t)cok_data_offset(D, COK_DATA_CLASS_BITS, i))));
    }
    if (!cok_character_saving_throws(character, cok_game_rules(game), error, sizeof error))
        return undefined(game, error) ? 0 : -1;
    return key;
}

/* Pick God (4def:0f8e), for clerics: the deity, its spell and the
 * alignments it allows (*mask). Dwarves have only Reorx. */
static int deity(cok_adventure *game, cok_character *character, uint8_t *mask)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 0) <= 0) {
        cok_field_set8(L, c, COK_FIELD_DEITY, 0);
        *mask = 0xff;
        return 'S';
    }
    bool dwarf = cok_field_u8(L, c, COK_FIELD_RACE) == 3 || cok_field_u8(L, c, COK_FIELD_RACE) == 4;
    char names[7][41];
    size_t count = dwarf ? 1 : 7;
    for (unsigned i = 0; i < count; ++i)
        table_name(game, COK_DATA_DEITY_NAMES, dwarf ? 6 : i + 1, names[i]);
    int index, key = pick(game, "Pick God", names, count, &index);
    if (key != 'S') return key;
    uint8_t god = dwarf ? 6 : (uint8_t)index;
    cok_field_set8(L, c, COK_FIELD_DEITY, god);
    *mask = god <= 4 ? 0x15 : 0x2e;
    switch (god) {
    case 1: cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 101, 1); break;
    case 2: cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 102, 1); break;
    case 3:
        cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 103, 1);
        cok_field_add8(L, c, COK_FIELD_BASE_THAC0, 1);
        break;
    case 4:
        cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 104, 1);
        cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 105, 1);
        cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 106, 1);
        break;
    case 5: cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 107, 1); break;
    case 6: if (dwarf) cok_field_add8(L, c, COK_FIELD_BASE_THAC0, 1); break;
    case 7: cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 106, 1); break;
    default: break;
    }
    return key;
}

/* Pick Alignment (4def:1222): those of the class (DS:3c45) the deity
 * allows; then a cleric's experience and a mage's order of magic. */
static int alignment(cok_adventure *game, cok_character *character, uint8_t mask)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t *c = character->record;
    long at = cok_data_offset(D, COK_DATA_CLASS_ALIGNMENTS, cok_field_s8(L, c, COK_FIELD_CLASS));
    uint8_t allowed = (uint8_t)(ds(D, (uint16_t)at) & mask);
    char names[6][41];
    uint8_t codes[6];
    size_t count = 0;
    for (unsigned j = 0; j < 6; ++j) {
        if ((allowed & 1u << j) == 0) continue;
        codes[count] = cok_data_u8_at(D, COK_DATA_ALIGNMENT_CODES, j);
        table_name(game, COK_DATA_ALIGNMENT_NAMES, cok_data_u8_at(D, COK_DATA_ALIGNMENT_CODES, j),
                   names[count++]);
    }
    int index, key = pick(game, "Pick Alignment", names, count, &index);
    if (key != 'S') return key;
    uint8_t code = codes[index - 1];
    cok_field_set8(L, c, COK_FIELD_ALIGNMENT, code);
    bool cleric = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 0) > 0,
         none = cok_field_u32(L, c, COK_FIELD_EXPERIENCE) == 0;
    if (code == 0 || code == 3 || code == 6) {
        if (none && cleric) cok_field_set32(L, c, COK_FIELD_EXPERIENCE, 2000);
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0)
            cok_field_set8(L, c, COK_FIELD_MAGIC_ORDER, 1);
    } else if (code == 1 || code == 4 || code == 7) {
        if (none && cleric) cok_field_set32(L, c, COK_FIELD_EXPERIENCE, 1500);
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0)
            cok_field_set8(L, c, COK_FIELD_MAGIC_ORDER, 2);
    }
    return key;
}

/* The age (4def:1488): a single class's dice from DS:3ab9 (race * 32 +
 * class * 4: a base word, a count and sides); a multi-class's is the most
 * one class's dice can give, with no roll: the cleric's for classes 8-11,
 * the mage's for 12, 13, 15 and 16, the fighter's for 14. */
static void age(cok_adventure *game, uint8_t *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    int8_t class_id = cok_field_s8(L, c, COK_FIELD_CLASS);
    uint16_t value;
    if (class_id <= 7) {
        uint16_t p = (uint16_t)cok_data_offset2(D, COK_DATA_AGE_DICE,
                                                cok_field_s8(L, c, COK_FIELD_RACE), class_id);
        value = (uint16_t)(ds_word(D, p) + cok_dice(&game->vm.seed, ds(D, (uint16_t)(p + 2)),
                                                    ds(D, (uint16_t)(p + 3))));
    } else {
        int entry = class_id >= 8 && class_id <= 11 ? 0 : class_id == 14 ? 2 : 5;
        uint16_t p = (uint16_t)cok_data_offset2(D, COK_DATA_AGE_DICE,
                                                cok_field_s8(L, c, COK_FIELD_RACE), entry);
        value = (uint16_t)(ds_word(D, p) + ds(D, (uint16_t)(p + 2)) * ds(D, (uint16_t)(p + 3)));
    }
    cok_field_set16(L, c, COK_FIELD_AGE, value);
}

/* Roll the six scores (4def:164d): each the best of six 3d6 + 1, then
 * changed by the age's thresholds (DS:3b99 + race * 10), kept within the
 * race's limits (DS:39e0 + race * 16; strength's by gender) and above the
 * class's minimum (DS:3bdf + class * 6), wisdom 13 for a cleric of more
 * than one class, and exceptional strength d100 at 18 for a fighter,
 * ranger or knight, at most the race's. The original's racial
 * adjustments test the race list's loop counter, always 6, the human,
 * which has none: no score is adjusted by race. */
static void abilities(cok_adventure *game, uint8_t *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t race = cok_field_u8(L, c, COK_FIELD_RACE), sex = cok_field_u8(L, c, COK_FIELD_GENDER);
    uint16_t limits = (uint16_t)cok_data_offset2(D, COK_DATA_SCORE_LIMITS, (int8_t)race, 0);
    uint16_t age_now = cok_field_u16(L, c, COK_FIELD_AGE);
    bool old[4];
    for (int i = 0; i < 4; ++i)
        old[i] =
            ds_word(D, (uint16_t)cok_data_offset2(D, COK_DATA_AGES, (int8_t)race, i)) < age_now;
    for (int a = 0; a < 6; ++a) {
        uint8_t s = 0;
        for (int k = 0; k < 6; ++k) {
            uint8_t v = (uint8_t)(cok_dice(&game->vm.seed, 3, 6) + 1);
            if (s < v) s = v;
        }
        uint8_t low, high;
        switch (a) {
        case 0:
            if (s > 0 && old[0]) ++s;
            if (s > 0 && old[1]) --s;
            if (s > 0 && old[2]) s = (uint8_t)(s - 2);
            if (s > 0 && old[3]) --s;
            low = ds(D, (uint16_t)(limits + sex));
            high = ds(D, (uint16_t)(limits + 2 + sex));
            break;
        case 1:
            if (old[1]) ++s;
            if (old[3]) ++s;
            low = ds(D, (uint16_t)(limits + 6)), high = ds(D, (uint16_t)(limits + 7));
            break;
        case 2:
            if (!old[0]) --s;
            if (old[1]) ++s;
            if (old[2]) ++s;
            if (old[3]) ++s;
            low = ds(D, (uint16_t)(limits + 8)), high = ds(D, (uint16_t)(limits + 9));
            break;
        case 3:
            if (old[2]) s = (uint8_t)(s - 2);
            if (old[3]) --s;
            low = ds(D, (uint16_t)(limits + 10)), high = ds(D, (uint16_t)(limits + 11));
            break;
        case 4:
            if (old[1]) --s;
            if (old[2]) --s;
            if (old[3]) --s;
            low = ds(D, (uint16_t)(limits + 12)), high = ds(D, (uint16_t)(limits + 13));
            break;
        default:
            low = ds(D, (uint16_t)(limits + 14)), high = ds(D, (uint16_t)(limits + 15));
            break;
        }
        if (s < low) s = low;
        if (s > high) s = high;
        uint8_t minimum = ds(D, (uint16_t)cok_data_offset2(D, COK_DATA_CLASS_MINIMUMS,
                                                           cok_field_s8(L, c, COK_FIELD_CLASS), a));
        if (s < minimum) s = minimum;
        if (a == 2 && s < 13 && cok_field_u8(L, c, COK_FIELD_CLASS) >= 8 &&
            cok_field_u8(L, c, COK_FIELD_CLASS) <= 12)
            s = 13;
        if (a == 0 && s == 18 &&
            (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 2) > 0 ||
             cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 4) > 0 ||
             cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > 0)) {
            cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL,
                           (uint8_t)(cok_tp_random(&game->vm.seed, 100) + 1));
            uint8_t most = ds(D, (uint16_t)(limits + 4 + sex));
            if (most < cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL))
                cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL, most);
        }
        cok_field_set8_at(L, c, COK_FIELD_SCORE, a, s);
        cok_field_set8_at(L, c, COK_FIELD_SCORE_BASE, a, s);
        if (a == 0)
            cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE,
                           cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL));
        cok_sheet_ability(game, c, a, 10);
    }
}

/* The base combat fields, money and spells (4def:20e3). */
static bool equip(cok_adventure *game, cok_character *character, uint8_t *classes)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t *c = character->record;
    cok_field_set8(L, c, COK_FIELD_HIT_POINTS, cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS));
    cok_field_set8_at(L, c, COK_FIELD_BASE_ATTACKS, 1, 2);
    cok_field_set8_at(L, c, COK_FIELD_BASE_DICE, 1, 1);
    cok_field_set8_at(L, c, COK_FIELD_BASE_DICE_SIDES, 1, 2);
    cok_field_set8(L, c, COK_FIELD_STRENGTH_BONUSES, 1);
    cok_field_set8(L, c, COK_FIELD_BASE_MOVEMENT, 12);
    uint8_t money = 0;
    *classes = 0;
    memset(c + cok_field_offset(L, COK_FIELD_SPELLS_A_DAY), 0,
           cok_field_size(L, COK_FIELD_SPELLS_A_DAY));
    for (unsigned i = 0; i < 8; ++i) {
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i) <= 0) continue;
        if (i == 0)
            cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1,
                               cok_field_s8(L, c, COK_FIELD_DEITY) <= 4 ? 1 : 2);
        if (i == 5) cok_field_set8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 3, 1, 1);
        uint32_t *seed = &game->vm.seed;
        if (i == 7) {
            money = cok_dice(seed, 1, 4);
        } else if (i == 2 || i == 4) {
            money = (uint8_t)(money + cok_dice(seed, 5, 4));
        } else if (i == 0) {
            money = (uint8_t)(money + cok_dice(seed, 3, 6));
            char error[300];
            if (!cok_character_cleric_spells(character, cok_game_rules(game), error, sizeof error))
                return undefined(game, error);
            uint8_t spell = 0xff;
            for (unsigned k = 1; k <= 10 && spell != 0; ++k) {
                spell = ds(D, (uint16_t)cok_data_offset3(D, COK_DATA_SPELL_LISTS, 1, 1, k));
                if (spell != 0) cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, spell, 1);
            }
        } else if (i == 5) {
            money = (uint8_t)(money + cok_dice(seed, 2, 4));
            cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 11, 1);
            cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 18, 1);
            cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 12, 1);
            cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 15, 1);
            cok_field_set8_at(L, c, COK_FIELD_KNOWN_SPELL, 21, 1);
        } else if (i == 6) {
            money = (uint8_t)(money + cok_dice(seed, 2, 6));
        }
        ++*classes;
    }
    /* The division of a multi-class's money tests the loop's counter, 7
     * after the loop, and never happens. */
    money = (uint8_t)(money + money * 20 / 100);
    cok_field_set16_at(L, c, COK_FIELD_COINS, 4, money);
    return true;
}

/* The levels on the sheet's row 7 (4def:232a), written over the sheet's
 * without clearing. */
static void level_line(cok_adventure *game, const uint8_t *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    int8_t first = 0;
    if (cok_field_u8(L, c, COK_FIELD_RACE) == 6) {
        size_t i = 0;
        while (i < 7 && cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, i) == 0) ++i;
        first = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
    }
    char line[64] = "";
    bool any = false;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t now = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i),
               before = cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, i);
        if (now <= 0 && !(before < first && before > 0)) continue;
        size_t length = strlen(line);
        snprintf(line + length, sizeof line - length, "%s%d", any ? "/" : "", now + before);
        any = true;
    }
    cok_adventure_log(game, "print", line);
    cok_text_string(&game->screen, &game->font, line, 7, 7, 15, 0);
}

/* From label R (4def:1614) to the figures: the rolls, money and spells,
 * hit points, and training as far as the experience allows. */
static bool roll(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    for (unsigned i = 0; i < 8; ++i)
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i) > 0)
            cok_field_set8_at(L, c, COK_FIELD_CLASS_LEVEL, i, 1);
    cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL, 0);
    abilities(game, c);
    uint8_t classes;
    if (!equip(game, character, &classes)) return false;
    cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 0);
    cok_field_set8(L, c, COK_FIELD_BASE_HIT_POINTS, 0);
    cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, 0);
    if (!cok_train_hit_points(game, c, 0xff, classes)) return false;
    /* 4def:22eb: train silently with every class allowed (var 0x7ea8 0xff,
     * its high byte lost afterwards). */
    if (game->free_training || cok_field_u8(L, c, COK_FIELD_STATUS) != 0)
        return undefined(game, "creation trains until no class can rise, which free training "
                               "or a status not okay never lets happen (4def:22eb)");
    uint16_t *hall = &game->vm.mem7c00[0x2a8];
    uint8_t kept = (uint8_t)*hall;
    *hall = 0xff;
    while (cok_train_silently(game, character)) {}
    *hall = kept;
    if (game->vm.abort) return false;
    level_line(game, c);
    return cok_sheet_figures(game, character) && cok_sheet_money(game, c);
}

/* Name the character (4def:245e): until a name is typed. */
static bool name(cok_adventure *game, uint8_t *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_keyboard keys = cok_adventure_keyboard_raw(game);
    char line[41] = "";
    while (line[0] == '\0') {
        cok_adventure_log(game, "input", "Character name: ");
        if (!cok_text_input(&game->screen, &game->font, "Character name: ", 13, 0, 15, &keys, line))
            return false;
    }
    cok_adventure_log(game, "input", line);
    size_t length = strlen(line);
    if (length > 15) length = 15;
    memset(c + cok_field_offset(L, COK_FIELD_NAME), 0, cok_field_size(L, COK_FIELD_NAME));
    cok_field_set8_at(L, c, COK_FIELD_NAME, 0, (uint8_t)length);
    memcpy(c + cok_field_offset_at(L, COK_FIELD_NAME, 1), line, length);
    return true;
}

static bool build(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t *c = character->record;
    for (size_t k = 1; k <= 6; ++k) {
        uint8_t colour = ds(D, (uint16_t)cok_data_offset(D, COK_DATA_ICON_COLOURS, k));
        cok_field_set8_at(L, c, COK_FIELD_ICON_COLOURS, k, (uint8_t)(((colour + 8) << 4) + colour));
    }
    cok_field_set8(L, c, COK_FIELD_BASE_AC, 0x32);
    cok_field_set8(L, c, COK_FIELD_BASE_THAC0, 0x28);
    cok_field_set8(L, c, COK_FIELD_STATUS, 0);
    cok_field_set8(L, c, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, c, COK_FIELD_SIZE, 1);
    cok_field_set8(L, c, COK_FIELD_MONSTER_ID, (uint8_t)cok_tp_random(&game->vm.seed, 0x100));
    cok_field_set8(L, c, COK_FIELD_ICON, 0x0a);
    uint8_t mask;
    int key;
    if ((key = race(game, character)) != 'S' || (key = gender(game, character)) != 'S' ||
        (key = class_(game, character)) != 'S' || (key = deity(game, character, &mask)) != 'S' ||
        (key = alignment(game, character, mask)) != 'S')
        return false;
    age(game, c);
    game->outside = character;
    game->vm.character = c;
    if (!cok_sheet_draw(game)) return false;
    int answer;
    do {
        if (!roll(game, character)) return false;
        answer = cok_camp_yes_no(game, "Reroll stats? ", 13);
        if (answer < 0) return false;
    } while (answer != 'N');
    if (!cok_sheet_draw(game) || !name(game, c)) return false;
    cok_create_icon(c, cok_game_rules(game));
    if (!cok_icon_edit(game)) return false;
    for (size_t a = 0; a < 6; ++a)
        cok_field_set8_at(L, c, COK_FIELD_SCORE_BASE, a, cok_field_u8_at(L, c, COK_FIELD_SCORE, a));
    cok_field_set8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE,
                   cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL));
    char prompt[48];
    uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
    snprintf(prompt, sizeof prompt, "Save %.*s? ", stored > 15 ? 15 : stored,
             (const char *)c + cok_field_offset_at(L, COK_FIELD_NAME, 1));
    answer = cok_camp_yes_no(game, prompt, 13);
    if (answer != 'Y') return false;
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > 0 &&
        !cok_create_knight_items(character, cok_game_rules(game)))
        return undefined(game, "out of memory");
    cok_roster_save(game, character);
    return true;
}

void cok_create(cok_adventure *game)
{
    uint8_t *saved = game->vm.character;
    cok_character *character = calloc(1, sizeof *character);
    if (character == NULL) {
        undefined(game, "out of memory");
        return;
    }
    build(game, character);
    /* 4def:003c frees the record, saved or not, but not its icons in slot
     * 10; the selection comes back. */
    game->outside = NULL;
    game->vm.character = saved;
    cok_character_free(character);
    free(character);
}
