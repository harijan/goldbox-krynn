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

/* The alignments the alignment list offers, by bit (DS:0a9a): lawful,
 * true and chaotic good and neutral; evil is never offered. */
static const uint8_t alignment_codes[6] = {0, 1, 3, 4, 6, 7};

const cok_ds_table cok_create_tables[] = {
    {0x0a9a, sizeof alignment_codes, 1, alignment_codes},
};
const size_t cok_create_table_count = sizeof cok_create_tables / sizeof *cok_create_tables;

static uint8_t ds(uint16_t offset)
{
    uint8_t byte = 0;
    cok_ds_byte(offset, &byte);
    return byte;
}

static uint16_t ds_word(uint16_t offset)
{
    return (uint16_t)(ds(offset) | ds((uint16_t)(offset + 1)) << 8);
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

void cok_create_icon(uint8_t *c)
{
    if (c[0x5a] == 5)
        c[0x135] = 3;
    else if (c[0x109] == 1)
        c[0x135] = c[0x138] == 2 ? 9 : 7;
    else
        c[0x135] = c[0x138] == 2 ? 5 : 0;
    if ((int8_t)c[0xf9] > 0)
        c[0x136] = 0x17;
    else if ((int8_t)c[0xfd] > 0)
        c[0x136] = 1;
    else if ((int8_t)c[0x100] > 0 || (int8_t)c[0xfb] > 0)
        c[0x136] = 0x18;
    else if ((int8_t)c[0xfe] > 0)
        c[0x136] = 0x1d;
    else
        c[0x136] = 5;
}

bool cok_create_knight_items(cok_character *character)
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
    character->record[0x142] = 3;
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

/* The name of table entry index at DS:base + stride * index. */
static void table_name(uint16_t base, unsigned stride, unsigned index, char out[41])
{
    char name[256];
    if (!cok_ds_string((uint16_t)(base + stride * index), name)) name[0] = '\0';
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
    char names[7][41];
    for (unsigned i = 0; i < 7; ++i) table_name(0x1140, 16, i, names[i]);
    int index, key = pick(game, "Pick Race", names, 7, &index);
    if (key != 'S') return key;
    uint8_t *c = character->record;
    uint8_t race = (uint8_t)(index - 1);
    c[0x5a] = race;
    c[0x138] = race == 3 || race == 4 || race == 5 ? 1 : 2;
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
    char names[2][41];
    for (unsigned i = 0; i < 2; ++i) table_name(0x12d5, 7, i, names[i]);
    int index, key = pick(game, "Pick Gender", names, 2, &index);
    if (key == 'S') character->record[0x109] = (uint8_t)(index - 1);
    return key;
}

/* Pick Class (4def:0ae0): the classes of the race (DS:3a50, 15 bytes a
 * race: the count, then the classes), its levels and experience, and the
 * class-time THAC0, class bits, thief skills and saving throws. */
static int class_(cok_adventure *game, cok_character *character)
{
    uint8_t *c = character->record;
    uint16_t base = (uint16_t)(0x3a50 + (int8_t)c[0x5a] * 15);
    uint8_t count = ds(base);
    char names[16][41];
    for (unsigned i = 1; i <= count && i <= 16; ++i)
        table_name(0x0f5a, 27, ds((uint16_t)(base + i)), names[i - 1]);
    int index, key = pick(game, "Pick Class", names, count, &index);
    if (key != 'S') return key;
    put32(c + 0x116, 0);
    uint8_t class_id = ds((uint16_t)(base + index));
    c[0x5b] = class_id;
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
        memcpy(c + 0xf9, kinds[class_id].levels, 8);
        put32(c + 0x116, kinds[class_id].experience);
    }
    if (class_id == 4 || class_id == 10) add_effect(character, 0x69);
    if (class_id == 7) c[0x5c] = 1;
    char error[300];
    /* 66c2:0b9f's uninitialized local holds the normal colour, 0x0a, that
     * the class list's 67b5:1368 was given (4def:0ee7). Dexterity is 0. */
    if ((int8_t)c[0xff] > 0 && !cok_character_thief_skills(character, 0x0a, error, sizeof error))
        return undefined(game, error) ? 0 : -1;
    c[0x11a] = 0;
    c[0x59] = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t level = (int8_t)c[0xf9 + i];
        if (level <= 0) continue;
        uint8_t thac0 = ds((uint16_t)(0x3882 + (int)i * 13 + level));
        if (thac0 > c[0x59]) c[0x59] = thac0;
        c[0x11a] = (uint8_t)(c[0x11a] + ds((uint16_t)(0x38ea + i)));
    }
    if (!cok_character_saving_throws(character, error, sizeof error))
        return undefined(game, error) ? 0 : -1;
    return key;
}

/* Pick God (4def:0f8e), for clerics: the deity, its spell and the
 * alignments it allows (*mask). Dwarves have only Reorx. */
static int deity(cok_adventure *game, cok_character *character, uint8_t *mask)
{
    uint8_t *c = character->record;
    if ((int8_t)c[0xf9] <= 0) {
        c[0x5d] = 0;
        *mask = 0xff;
        return 'S';
    }
    bool dwarf = c[0x5a] == 3 || c[0x5a] == 4;
    char names[7][41];
    size_t count = dwarf ? 1 : 7;
    for (unsigned i = 0; i < count; ++i) table_name(0x1259, 12, dwarf ? 6 : i + 1, names[i]);
    int index, key = pick(game, "Pick God", names, count, &index);
    if (key != 'S') return key;
    uint8_t god = dwarf ? 6 : (uint8_t)index;
    c[0x5d] = god;
    *mask = god <= 4 ? 0x15 : 0x2e;
    switch (god) {
    case 1: c[0xc7] = 1; break;
    case 2: c[0xc8] = 1; break;
    case 3: c[0xc9] = 1; ++c[0x59]; break;
    case 4: c[0xca] = c[0xcb] = c[0xcc] = 1; break;
    case 5: c[0xcd] = 1; break;
    case 6: if (dwarf) ++c[0x59]; break;
    case 7: c[0xcc] = 1; break;
    default: break;
    }
    return key;
}

/* Pick Alignment (4def:1222): those of the class (DS:3c45) the deity
 * allows; then a cleric's experience and a mage's order of magic. */
static int alignment(cok_adventure *game, cok_character *character, uint8_t mask)
{
    uint8_t *c = character->record;
    uint8_t allowed = (uint8_t)(ds((uint16_t)(0x3c45 + (int8_t)c[0x5b])) & mask);
    char names[6][41];
    uint8_t codes[6];
    size_t count = 0;
    for (unsigned j = 0; j < 6; ++j) {
        if ((allowed & 1u << j) == 0) continue;
        codes[count] = alignment_codes[j];
        table_name(0x11c0, 17, alignment_codes[j], names[count++]);
    }
    int index, key = pick(game, "Pick Alignment", names, count, &index);
    if (key != 'S') return key;
    uint8_t code = codes[index - 1];
    c[0x10a] = code;
    bool cleric = (int8_t)c[0xf9] > 0, none = c[0x116] == 0 && c[0x117] == 0 &&
                  c[0x118] == 0 && c[0x119] == 0;
    if (code == 0 || code == 3 || code == 6) {
        if (none && cleric) put32(c + 0x116, 2000);
        if ((int8_t)c[0xfe] > 0) c[0x5e] = 1;
    } else if (code == 1 || code == 4 || code == 7) {
        if (none && cleric) put32(c + 0x116, 1500);
        if ((int8_t)c[0xfe] > 0) c[0x5e] = 2;
    }
    return key;
}

/* The age (4def:1488): a single class's dice from DS:3ab9 (race * 32 +
 * class * 4: a base word, a count and sides); a multi-class's is the most
 * one class's dice can give, with no roll: the cleric's for classes 8-11,
 * the mage's for 12, 13, 15 and 16, the fighter's for 14. */
static void age(cok_adventure *game, uint8_t *c)
{
    int8_t class_id = (int8_t)c[0x5b];
    uint16_t base = (uint16_t)(0x3ab9 + (int8_t)c[0x5a] * 32);
    uint16_t value;
    if (class_id <= 7) {
        uint16_t p = (uint16_t)(base + class_id * 4);
        value = (uint16_t)(ds_word(p) + cok_dice(&game->vm.seed, ds((uint16_t)(p + 2)),
                                                 ds((uint16_t)(p + 3))));
    } else {
        int entry = class_id >= 8 && class_id <= 11 ? 0 : class_id == 14 ? 2 : 5;
        uint16_t p = (uint16_t)(base + entry * 4);
        value = (uint16_t)(ds_word(p) + ds((uint16_t)(p + 2)) * ds((uint16_t)(p + 3)));
    }
    put16(c + 0x60, value);
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
    uint8_t race = c[0x5a], sex = c[0x109];
    uint16_t limits = (uint16_t)(0x39e0 + (int8_t)race * 16);
    uint16_t age_now = (uint16_t)(c[0x60] | c[0x61] << 8);
    bool old[4];
    for (int i = 0; i < 4; ++i)
        old[i] = ds_word((uint16_t)(0x3b99 + (int8_t)race * 10 + 2 * i)) < age_now;
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
            low = ds((uint16_t)(limits + sex));
            high = ds((uint16_t)(limits + 2 + sex));
            break;
        case 1:
            if (old[1]) ++s;
            if (old[3]) ++s;
            low = ds((uint16_t)(limits + 6)), high = ds((uint16_t)(limits + 7));
            break;
        case 2:
            if (!old[0]) --s;
            if (old[1]) ++s;
            if (old[2]) ++s;
            if (old[3]) ++s;
            low = ds((uint16_t)(limits + 8)), high = ds((uint16_t)(limits + 9));
            break;
        case 3:
            if (old[2]) s = (uint8_t)(s - 2);
            if (old[3]) --s;
            low = ds((uint16_t)(limits + 10)), high = ds((uint16_t)(limits + 11));
            break;
        case 4:
            if (old[1]) --s;
            if (old[2]) --s;
            if (old[3]) --s;
            low = ds((uint16_t)(limits + 12)), high = ds((uint16_t)(limits + 13));
            break;
        default:
            low = ds((uint16_t)(limits + 14)), high = ds((uint16_t)(limits + 15));
            break;
        }
        if (s < low) s = low;
        if (s > high) s = high;
        uint8_t minimum = ds((uint16_t)(0x3bdf + (int8_t)c[0x5b] * 6 + a));
        if (s < minimum) s = minimum;
        if (a == 2 && s < 13 && c[0x5b] >= 8 && c[0x5b] <= 12) s = 13;
        if (a == 0 && s == 18 &&
            ((int8_t)c[0xfb] > 0 || (int8_t)c[0xfd] > 0 || (int8_t)c[0x100] > 0)) {
            c[0x1c] = (uint8_t)(cok_tp_random(&game->vm.seed, 100) + 1);
            uint8_t most = ds((uint16_t)(limits + 4 + sex));
            if (most < c[0x1c]) c[0x1c] = most;
        }
        c[0x10 + 2 * a] = c[0x11 + 2 * a] = s;
        if (a == 0) c[0x1d] = c[0x1c];
        cok_sheet_ability(game, c, a, 10);
    }
}

/* The base combat fields, money and spells (4def:20e3). */
static bool equip(cok_adventure *game, cok_character *character, uint8_t *classes)
{
    uint8_t *c = character->record;
    c[0x197] = c[0x62];
    c[0x10b] = 2;
    c[0x10d] = 1;
    c[0x10f] = 2;
    c[0x114] = 1;
    c[0xd5] = 12;
    uint8_t money = 0;
    *classes = 0;
    memset(c + 0x11c, 0, 0x14);
    for (unsigned i = 0; i < 8; ++i) {
        if ((int8_t)c[0xf9 + i] <= 0) continue;
        if (i == 0) c[0x11c] = (int8_t)c[0x5d] <= 4 ? 1 : 2;
        if (i == 5) c[0x12b] = 1;
        uint32_t *seed = &game->vm.seed;
        if (i == 7) {
            money = cok_dice(seed, 1, 4);
        } else if (i == 2 || i == 4) {
            money = (uint8_t)(money + cok_dice(seed, 5, 4));
        } else if (i == 0) {
            money = (uint8_t)(money + cok_dice(seed, 3, 6));
            char error[300];
            if (!cok_character_cleric_spells(character, error, sizeof error))
                return undefined(game, error);
            uint8_t spell = 0xff;
            for (unsigned k = 1; k <= 10 && spell != 0; ++k) {
                spell = ds((uint16_t)(0x423a + k));
                if (spell != 0) c[0x62 + spell] = 1;
            }
        } else if (i == 5) {
            money = (uint8_t)(money + cok_dice(seed, 2, 4));
            c[0x62 + 11] = c[0x62 + 18] = c[0x62 + 12] = c[0x62 + 15] = c[0x62 + 21] = 1;
        } else if (i == 6) {
            money = (uint8_t)(money + cok_dice(seed, 2, 6));
        }
        ++*classes;
    }
    /* The division of a multi-class's money tests the loop's counter, 7
     * after the loop, and never happens. */
    money = (uint8_t)(money + money * 20 / 100);
    put16(c + 0xf3, money);
    return true;
}

/* The levels on the sheet's row 7 (4def:232a), written over the sheet's
 * without clearing. */
static void level_line(cok_adventure *game, const uint8_t *c)
{
    int8_t first = 0;
    if (c[0x5a] == 6) {
        size_t i = 0;
        while (i < 7 && c[0xf9 + i] == 0) ++i;
        first = (int8_t)c[0xf9 + i];
    }
    char line[64] = "";
    bool any = false;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t now = (int8_t)c[0xf9 + i], before = (int8_t)c[0x101 + i];
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
    uint8_t *c = character->record;
    for (unsigned i = 0; i < 8; ++i)
        if ((int8_t)c[0xf9 + i] > 0) c[0xf9 + i] = 1;
    c[0x1c] = 0;
    abilities(game, c);
    uint8_t classes;
    if (!equip(game, character, &classes)) return false;
    c[0x62] = c[0x11b] = c[0x197] = 0;
    if (!cok_train_hit_points(game, c, 0xff, classes)) return false;
    /* 4def:22eb: train silently with every class allowed (var 0x7ea8 0xff,
     * its high byte lost afterwards). */
    if (game->free_training || c[0x188] != 0)
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
    cok_keyboard keys = cok_adventure_keyboard(game);
    char line[41] = "";
    while (line[0] == '\0') {
        cok_adventure_log(game, "input", "Character name: ");
        if (!cok_text_input(&game->screen, &game->font, "Character name: ", 13, 0, 15, &keys, line))
            return false;
    }
    cok_adventure_log(game, "input", line);
    size_t length = strlen(line);
    if (length > 15) length = 15;
    memset(c, 0, 16);
    c[0] = (uint8_t)length;
    memcpy(c + 1, line, length);
    return true;
}

static bool build(cok_adventure *game, cok_character *character)
{
    uint8_t *c = character->record;
    for (size_t k = 1; k <= 6; ++k) {
        uint8_t colour = ds((uint16_t)(0x390a + k));
        c[0x138 + k] = (uint8_t)(((colour + 8) << 4) + colour);
    }
    c[0x113] = 0x32;
    c[0x59] = 0x28;
    c[0x188] = 0;
    c[0x189] = 1;
    c[0xcf] = 1;
    c[0x115] = (uint8_t)cok_tp_random(&game->vm.seed, 0x100);
    c[0x137] = 0x0a;
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
    cok_create_icon(c);
    if (!cok_icon_edit(game)) return false;
    for (size_t a = 0; a < 6; ++a) c[0x10 + 2 * a] = c[0x11 + 2 * a];
    c[0x1d] = c[0x1c];
    char prompt[48];
    snprintf(prompt, sizeof prompt, "Save %.*s? ", c[0] > 15 ? 15 : c[0], (const char *)c + 1);
    answer = cok_camp_yes_no(game, prompt, 13);
    if (answer != 'Y') return false;
    if ((int8_t)c[0x100] > 0 && !cok_create_knight_items(character))
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
