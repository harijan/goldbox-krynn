#include "sheet.h"

#include "camp.h"
#include "items.h"
#include "magic.h"
#include "screen.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

static cok_character *selected(cok_adventure *game)
{
    return cok_adventure_selected(game);
}

static void draw(cok_adventure *game, const char *text, int x, int y, uint8_t fg)
{
    cok_text_string(&game->screen, &game->font, text, x, y, fg, 0);
}

static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    cok_picture_fill(&game->screen, x1, y1 * 8, (size_t)(x2 - x1 + 1), (size_t)(y2 - y1 + 1) * 8,
                     0); /* 1128:07e6 */
}

/* The name index of table t (DS:base + stride * index), index a signed
 * byte, as the original reads it, or false where it would read past the
 * name strings. */
static bool table_name(cok_adventure *game, cok_data t, int8_t index, char out[256])
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    long at = cok_data_offset(D, t, index);
    if (at >= 0 && at <= 0xffff && cok_data_string(D, (uint16_t)at, out)) return true;
    char text[96];
    snprintf(text, sizeof text, "the sheet would show the data at DS:%04lx as a name",
             (unsigned long)(at & 0xffff));
    return undefined(game, text);
}

/* Append text to a string[255]. */
static void append(char *s, const char *text)
{
    size_t length = strlen(s);
    while (*text != '\0' && length < 255) s[length++] = *text++;
    s[length] = '\0';
}

/* The sheet. */

/* The coins the sheet shows, all but silver (0): from the layout's first,
 * or 1, to its last (1-6 in Champions). */
static int first_coin(const cok_character_layout *L)
{
    int first = (int)cok_field_first(L, COK_FIELD_COINS);
    return first > 1 ? first : 1;
}

static int end_coin(const cok_character_layout *L)
{
    return (int)(cok_field_first(L, COK_FIELD_COINS) + cok_field_count(L, COK_FIELD_COINS));
}

/* The class line (in 546c:00a3): the class (DS:0f5a by +0x5b), after the
 * deity (DS:1259 by +0x5d) and a space for a cleric; for a mage, the order
 * (DS:12b9 by +0x5e) and a space inserted before the first "M?g", which
 * the search may find in the bytes left past the string by the strings
 * shown before it in the same buffer (gender, alignment and race), and
 * otherwise after the end; for a knight, its order " of the Crown",
 * " of the Sword" or " of the Rose" (+0x5c 1-3). */
static bool class_line(cok_adventure *game, const uint8_t *c, const char *const before[3],
                       char out[256])
{
    /* The buffer at [bp-0x106] as the earlier strings left it. Bytes no
     * string reached are unknown. */
    char buffer[256];
    bool known[256] = {false};
    for (size_t k = 0; k < 3; ++k) {
        size_t length = strlen(before[k]);
        memcpy(buffer + 1, before[k], length);
        for (size_t i = 0; i <= length; ++i) known[i] = true;
    }
    const cok_character_layout *L = cok_game_rules(game)->character;
    char s[256];
    if (!table_name(game, COK_DATA_CLASS_NAMES, cok_field_s8(L, c, COK_FIELD_CLASS), s))
        return false;
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 0) > 0) {
        char deity[256], line[256];
        if (!table_name(game, COK_DATA_DEITY_NAMES, cok_field_s8(L, c, COK_FIELD_DEITY), deity))
            return false;
        line[0] = '\0';
        append(line, deity);
        append(line, " ");
        append(line, s);
        memcpy(s, line, sizeof s);
    }
    size_t length = strlen(s);
    memcpy(buffer + 1, s, length);
    for (size_t i = 0; i <= length; ++i) known[i] = true;
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0) {
        size_t at = 0;
        for (;;) {
            ++at;
            if (!known[at] || (buffer[at] == 'M' && !known[at + 2 < 256 ? at + 2 : 255]))
                return undefined(game, "the class line's search for \"Mage\" reads bytes the "
                                       "original leaves unset (546c:028f; dkk 580d:0256)");
            if (buffer[at] == 'M' && buffer[at + 2] == 'g') break;
            if (at > length) break;
        }
        char order[256], insert[256];
        const cok_data_segment *D = cok_game_rules(game)->data;
        if (!cok_data_string(D,
                             (uint16_t)cok_data_offset(D, COK_DATA_ORDER_NAMES,
                                                       cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER)),
                             order))
            return undefined(game, "the order of magic is past the name strings");
        insert[0] = '\0';
        append(insert, order);
        append(insert, " ");
        /* Insert (1a46:0c21) at position at, from 1; past the end, after it. */
        char line[256] = "";
        size_t keep = at - 1 < length ? at - 1 : length;
        memcpy(line, s, keep);
        line[keep] = '\0';
        append(line, insert);
        append(line, s + keep);
        memcpy(s, line, sizeof s);
    }
    if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > 0) {
        static const char *const orders[4] = {"", " of the Crown", " of the Sword", " of the Rose"};
        uint8_t order = cok_field_u8(L, c, COK_FIELD_KNIGHT_ORDER);
        if (order >= 1 && order <= 3) append(s, orders[order]);
    }
    snprintf(out, 256, "%s", s);
    return true;
}

/* 66c2:0eab: a human's level in its first class with one, the knight
 * level if none; 0 for others. */
static int8_t first_level(const cok_character_layout *L, const uint8_t *c)
{
    if (cok_field_u8(L, c, COK_FIELD_RACE) != 6) return 0;
    size_t i = 0;
    while (i < 7 && cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, i) == 0) ++i;
    return cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
}

/* Ability i on row 9 + i (546c:0b50): the current score from column 5, 6
 * below 10; exceptional strength at 18 as "(NN)", "(00)" for 100; and "*"
 * at column 12 when it differs from the base. */
void cok_sheet_ability(cok_adventure *game, const uint8_t *c, int i, uint8_t color)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t score = cok_field_u8_at(L, c, COK_FIELD_SCORE, i);
    uint8_t exceptional = cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL);
    char text[16];
    clear_cells(game, 5, 9 + i, 0x0b, 9 + i);
    snprintf(text, sizeof text, "%u", score);
    draw(game, text, score < 10 ? 6 : 5, 9 + i, color);
    if (i == 0 && score == 18 && exceptional > 0) {
        char ex[8];
        snprintf(ex, sizeof ex, "%s%u", exceptional < 10 ? "0" : "", exceptional);
        if (exceptional == 100) snprintf(ex, sizeof ex, "00");
        snprintf(text, sizeof text, "(%s)", ex);
        draw(game, text, 7, 9, color);
    }
    if (score != cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, i) ||
        (i == 0 && cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE) != exceptional))
        draw(game, "*", 12, 9 + i, color);
}

/* The money (546c:0667): each kind but silver that the character has, from
 * row 9, its name at column 20 and its amount ending at column 37. */
bool cok_sheet_money(cok_adventure *game, const uint8_t *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    clear_cells(game, 0x14, 9, 0x26, 0x0f);
    int row = 9;
    for (int i = first_coin(L); i < end_coin(L); ++i) {
        uint16_t amount = cok_field_u16_at(L, c, COK_FIELD_COINS, i);
        if (amount == 0) continue;
        char name[256], text[8];
        if (!table_name(game, COK_DATA_COIN_NAMES, (int8_t)i, name)) return false;
        draw(game, name, 0x14, row, 10);
        snprintf(text, sizeof text, "%u", amount);
        draw(game, text, 0x26 - (int)strlen(text), row, 10);
        ++row;
    }
    return true;
}

/* Text padded with spaces in front to width (546c:0736). */
static void pad(char *out, size_t size, const char *text, size_t width)
{
    size_t length = strlen(text), spaces = length < width ? width - length : 0;
    snprintf(out, size, "%*s%s", (int)spaces, "", text);
}

/* The combat figures (546c:07bb), after recomputing the stats (6346:0d20):
 * hit points, armour class, encumbrance, THAC0, movement (doubled by
 * haste, 0x27, halved by slow, 0x2a) and damage. */
void cok_sheet_hit_points(cok_adventure *game, const uint8_t *c, int x, int y, bool highlight,
                          bool show_max)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t hp = cok_field_u8(L, c, COK_FIELD_HIT_POINTS),
            max = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS);
    char text[8], value[8];
    snprintf(text, sizeof text, "%u", hp);
    draw(game, text, x, y, highlight ? 13 : hp < max ? 14 : 10);
    if (!show_max) return;
    snprintf(value, sizeof value, "/%u", max);
    draw(game, value, x + (int)strlen(text), y, 10);
}

bool cok_sheet_figures(cok_adventure *game, cok_character *character)
{
    char error[300], text[64], value[48];
    if (!cok_character_stats(character, cok_game_rules(game), &game->item_types, error,
                             sizeof error))
        return undefined(game, error);
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *c = character->record;
    draw(game, "Hit Points ", 0x14, 3, 15);
    clear_cells(game, 0x1f, 3, 0x25, 3);
    /* 6346:0a0d: hit points, yellow while below the maximum, "/" and it. */
    cok_sheet_hit_points(game, c, 0x1f, 3, false, true);
    draw(game, "Armor Class", 1, 0x11, 15);
    uint8_t ac = cok_field_u8(L, c, COK_FIELD_AC);
    int x = ac >= 1 && ac <= 0x32 ? 0x10 : ac >= 0x33 && ac <= 0x3c ? 0x11 : ac >= 0x3d && ac <= 0x45 ? 0x10 : 0x0f;
    clear_cells(game, 0x0f, 0x11, 0x11, 0x11);
    /* 6346:0984: |60 - AC field| as a byte, "-" before it above 60. */
    snprintf(text, sizeof text, "%s%u", ac > 60 ? "-" : "", (unsigned)abs(ac - 60) & 0xffu);
    draw(game, text, x, 0x11, 10);
    draw(game, "Encumbrance", 0x14, 0x11, 15);
    snprintf(value, sizeof value, "%u", cok_field_u16(L, c, COK_FIELD_WEIGHT));
    pad(text, sizeof text, value, 5);
    draw(game, text, 0x21, 0x11, 10);
    draw(game, "THAC0   ", 1, 0x12, 15);
    snprintf(value, sizeof value, "%u", (uint8_t)(60 - cok_field_u8(L, c, COK_FIELD_THAC0)));
    pad(text, sizeof text, value, 2);
    draw(game, text, 0x10, 0x12, 10);
    /* As the stats left it where they count haste and slow (dkk
     * 580d:0751). */
    uint8_t movement = cok_field_u8(L, c, COK_FIELD_MOVEMENT);
    if (!cok_stats_count_movement_effects(cok_game_rules(game))) {
        if (cok_character_find_named(character, cok_game_rules(game), COK_EFFECT_HASTED) != NULL)
            movement = (uint8_t)(movement * 2);
        if (cok_character_find_named(character, cok_game_rules(game), COK_EFFECT_SLOWED) != NULL)
            movement = (uint8_t)(movement >> 1);
    }
    draw(game, "Movement", 0x14, 0x12, 15);
    snprintf(value, sizeof value, "%u", movement);
    pad(text, sizeof text, value, 3);
    draw(game, text, 0x23, 0x12, 10);
    int8_t bonus = cok_field_s8_at(L, c, COK_FIELD_DAMAGE_BONUS, 1);
    snprintf(value, sizeof value, "%ud%u", cok_field_u8_at(L, c, COK_FIELD_DICE, 1),
             cok_field_u8_at(L, c, COK_FIELD_DICE_SIDES, 1));
    if (bonus > 0) snprintf(value + strlen(value), sizeof value - strlen(value), "+%u", (unsigned)bonus);
    if (bonus < 0) snprintf(value + strlen(value), sizeof value - strlen(value), "-%u", (unsigned)(-bonus) & 0xffu);
    draw(game, "Damage", 1, 0x13, 15);
    pad(text, sizeof text, value, 7);
    draw(game, text, 0x0b, 0x13, 10);
    return true;
}

bool cok_sheet_draw(cok_adventure *game)
{
    cok_character *character = selected(game);
    if (character == NULL)
        return undefined(game, "the sheet with no character selected reads through NULL (546c:00a3)");
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    cok_adventure_screen(game, COK_SCREEN_SHEET);
    char name[20], status[256], gender[256], alignment[256], race[256], line[256];
    uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(name, c + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    name[length] = '\0';
    cok_adventure_log(game, "print", name);
    uint8_t color = cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0 ? 12
                    : game->vm.mode == 5 && cok_field_u8(L, c, COK_FIELD_HOSTILE) == 1 ? 14
                                                                                       : 11;
    draw(game, name, 1, 1, color);
    draw(game, "Status:", 0x14, 1, 15);
    if (!table_name(game, COK_DATA_STATUS_NAMES, cok_field_s8(L, c, COK_FIELD_STATUS), status))
        return false;
    draw(game, status, 0x1b, 1, 10);
    if (!table_name(game, COK_DATA_GENDER_NAMES, cok_field_s8(L, c, COK_FIELD_GENDER), gender))
        return false;
    draw(game, gender, 1, 3, 15);
    snprintf(line, sizeof line, "%u years", cok_field_u16(L, c, COK_FIELD_AGE));
    draw(game, line, 8, 3, 15);
    if (!table_name(game, COK_DATA_ALIGNMENT_NAMES, cok_field_s8(L, c, COK_FIELD_ALIGNMENT),
                    alignment))
        return false;
    draw(game, alignment, 1, 4, 15);
    if (!table_name(game, COK_DATA_RACE_NAMES, cok_field_s8(L, c, COK_FIELD_RACE), race))
        return false;
    draw(game, race, 0x14, 4, 15);
    const char *before[3] = {gender, alignment, race};
    if (!class_line(game, c, before, line)) return false;
    cok_adventure_log(game, "print", line);
    draw(game, line, 1, 5, 15);
    draw(game, "Level", 1, 7, 15);
    /* Each class with a level, or with a former level below the first
     * class's (66c2:0eab), the sum of both as a byte; in a game without
     * former classes (dkk 580d:006e) each with a level, "0" for none. */
    char levels[256] = "";
    bool any = false, former = cok_levels_former_classes(cok_game_rules(game));
    for (int i = 0; i < 8; ++i) {
        int8_t now = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i),
               before_level = former ? cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, i) : 0;
        if (now <= 0 && !(former && before_level < first_level(L, c) && before_level > 0))
            continue;
        char value[8];
        snprintf(value, sizeof value, "%u", (uint8_t)(now + before_level));
        if (any) append(levels, "/");
        append(levels, value);
        any = true;
    }
    if (!any && !former) append(levels, "0");
    draw(game, levels, 7, 7, 15);
    uint32_t bits = cok_field_u32(L, c, COK_FIELD_EXPERIENCE);
    int32_t experience = bits > INT32_MAX ? -(int32_t)(~bits) - 1 : (int32_t)bits;
    snprintf(line, sizeof line, "Experience %ld", (long)experience);
    draw(game, line, 0x14, 7, 15);
    static const char *const labels[6] = {"STR ", "INT ", "WIS ", "DEX ", "CON ", "CHA "};
    for (int i = 0; i < 6; ++i) {
        draw(game, labels[i], 1, 9 + i, 10);
        cok_sheet_ability(game, c, i, 10);
    }
    if (!cok_sheet_money(game, c) || !cok_sheet_figures(game, character)) return false;
    char item_name[41];
    for (size_t k = 0; k < 2; ++k) {
        size_t slot = k == 0 ? 0 : 2;
        if (character->slots[slot] == 0) continue;
        uint8_t *item = character->items[character->slots[slot] - 1];
        if (!cok_item_name(game, item, false, item_name)) return false;
        draw(game, item_name, 1, 0x15 + (int)k, 10);
    }
    return true;
}

/* Money. */

int cok_sheet_amount(cok_adventure *game, const char *prompt, uint16_t max)
{
    char shown[41], input[7] = "", most[8];
    snprintf(shown, sizeof shown, "%s", prompt);
    cok_adventure_log(game, "menu", shown);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    draw(game, shown, 0, 24, 10);
    int start = (int)strlen(shown), cursor = start;
    snprintf(most, sizeof most, "%u", max);
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key;
    do {
        key = keys.read(keys.context);
        if (key < 0) return -1;
        if (key >= '0' && key <= '9') {
            size_t length = strlen(input);
            if (length < 6) {
                input[length] = (char)key;
                input[length + 1] = '\0';
            }
            if (strtoul(input, NULL, 10) > max) {
                memcpy(input, most, strlen(most) + 1);
                cursor = start + (int)strlen(input);
            } else {
                ++cursor;
            }
            draw(game, input, start, 24, 15);
        } else if (key == 8 && input[0] != '\0') {
            input[strlen(input) - 1] = '\0';
            /* The cell after the last digit is cleared, so the digit gone
             * stays on the screen. */
            cok_text_clear(&game->screen, &game->font, 1, cursor, 24, 0);
            --cursor;
        }
    } while (key != 0x0d && key != 0x1b);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    unsigned value = key == 0x1b ? 0 : (unsigned)(strtoul(input, NULL, 10) & 0xffff);
    char text[16];
    snprintf(text, sizeof text, "%u", value);
    cok_adventure_log(game, "input", text);
    return (int)value;
}

/* The coin a row of the coin list names, by its first letter (58e7:0b95;
 * dkk 5c72:09e4 by the same letters); the question's word for it is the
 * treasure section's (the same as 58e7:0b95's). */
static int coin_of(const char *row)
{
    while (*row == ' ') ++row;
    switch (row[0]) {
    case 'G': return 5;
    case 'P': return 3;
    case 'B': return 2;
    case 'S': return (row[1] == 'i' || row[1] == 'I') ? 0 : 4;
    case 'C': return 1;
    case 'J': return 6;
    default: return -1;
    }
}

/* Pick a kind of coin the character has (546c:2c75, 2fe2): a list over the
 * money, Copper first, each row the name, spaces and the amount, padded by
 * pad_to less the name's length (and the amount's, for Drop), the padding
 * at most 15. Returns the coin, 0x100 for none, or -1 when input ended. */
static int pick_coin(cok_adventure *game, const uint8_t *c, int pad_to, bool count_amount,
                     const char **coin_word)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    static char text[6][41];
    cok_menu_row rows[6];
    size_t count = 0;
    for (int i = first_coin(L); i < end_coin(L); ++i) {
        uint16_t amount = cok_field_u16_at(L, c, COK_FIELD_COINS, i);
        if (amount == 0) continue;
        char name[256], number[16], padding[16] = "";
        if (!table_name(game, COK_DATA_COIN_NAMES, (int8_t)i, name)) return -1;
        snprintf(number, sizeof number, "%u", amount);
        uint8_t spaces = (uint8_t)(pad_to - (int)strlen(name) - (count_amount ? (int)strlen(number) : 0));
        for (unsigned k = 0; k < spaces && k < 15; ++k) padding[k] = ' ', padding[k + 1] = '\0';
        text[count][0] = '\0';
        char row[256] = "";
        append(row, name);
        append(row, padding);
        append(row, number);
        snprintf(text[count], sizeof text[count], "%.40s", row);
        rows[count] = (cok_menu_row){text[count], false};
        ++count;
    }
    for (size_t i = 0; i < count; ++i) cok_adventure_log(game, "item", text[i]);
    cok_menu_style style = {cok_game_rules(game)->treasure.coin_prompt,
                            cok_game_rules(game)->items.coin_select, 15, 10, 13, true};
    bool redraw = true;
    int index = 0;
    cok_keyboard keys = cok_adventure_keyboard(game);
    cok_text_window window = {0x14, 9, 0x26, 0x0f};
    if (cok_game_rules(game)->items.routine == COK_ITEMS_DEATH_KNIGHTS) {
        /* dkk 580d:23a0, 26d0: the list 6a69:1b82, the selection as the
         * menu before left it, after Trade's "Select" alone (6a69:00cb with
         * 0: its Exit the list's own) and Drop's "Select" and the item
         * after (00cb with 1: the list's Exit). */
        cok_menu_items items;
        cok_menu_items_upto(&items, count_amount ? 1 : 0);
        bool special = false, picked;
        int key = cok_menu_indexed_rows(&game->screen, &game->font, rows, count, window, &style,
                                        style.base, &items, count_amount ? 1 : 0, &redraw,
                                        &index, &game->list_top, &game->selected, &keys,
                                        &special, &picked);
        if (key < 0) return -1;
        game->menu_special = special; /* DS:c0f8 */
        if (!picked || key == 1 || count == 0) return 0x100;
    } else {
        int key = cok_menu_rows(&game->screen, &game->font, rows, count, window, &style, &redraw,
                                &index, &game->list_top, &game->selected, &keys);
        if (key < 0) return -1;
        if (key == 0 || count == 0) return 0x100;
    }
    int coin = coin_of(text[index]);
    if (coin < 0) {
        undefined(game, "a coin's row starts with an unknown letter (58e7:0b95)");
        return -1;
    }
    *coin_word = cok_game_rules(game)->treasure.coin_words[coin];
    char choice[2] = {text[index][0], '\0'};
    cok_adventure_log(game, "choice", choice);
    return coin;
}

static bool has_money(const cok_character_layout *L, const uint8_t *c)
{
    for (int i = first_coin(L); i < end_coin(L); ++i)
        if (cok_field_u16_at(L, c, COK_FIELD_COINS, i) != 0) return true;
    return false;
}

/* Trade money (546c:2c75; dkk 580d:23a0): "Trade to?", in the game's
 * words, from the last partner (DS:46b0),
 * then the giver's sheet, and kinds of coin and amounts until it has none
 * left, or Escape in the coin list asks for another partner. */
static bool trade_money(cok_adventure *game, cok_character *giver, cok_sheet_stale stale)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *g = giver->record;
    for (;;) {
        cok_adventure_redraw(game);
        bool ended;
        uint8_t *who = cok_adventure_pick(game, cok_game_rules(game)->items.trade_to,
                                          game->trade_partner, true, &ended);
        if (ended) return false;
        if (who == NULL) return true;
        size_t i = cok_party_index(&game->party, who);
        if (i == game->party.count) return true;
        cok_character *receiver = game->party.members[i];
        if (!cok_sheet_draw(game)) return false;
        for (;;) {
            if (!cok_sheet_money(game, g)) return false;
            game->trade_partner = who;
            const char *coin_word;
            int coin = pick_coin(game, g, 14, false, &coin_word);
            if (coin < 0) return false;
            if (coin == 0x100) {
                /* The flag that would end Trade ([bp-0x13c], 546c:2f9b;
                 * dkk 580d:26c0) is set only by an amount, to whether it
                 * took the last coin; before any, it is the byte the stack
                 * held (see sheet.h). */
                if (stale == COK_SHEET_STALE_SET) return true;
                break;
            }
            char prompt[256];
            snprintf(prompt, sizeof prompt, "%s%swill you trade? ",
                     cok_game_rules(game)->treasure.how_much, coin_word);
            int amount =
                cok_sheet_amount(game, prompt, cok_field_u16_at(L, g, COK_FIELD_COINS, coin));
            if (amount < 0) return false;
            /* 58e7:044b: within what the receiver may carry, as words. */
            int16_t allowance;
            char error[300];
            if (!cok_character_allowance(receiver->record, cok_game_rules(game), &allowance, error,
                                         sizeof error))
                return undefined(game, error);
            uint8_t *r = receiver->record;
            if ((uint16_t)(cok_field_u16(L, r, COK_FIELD_WEIGHT) + amount) >
                (uint16_t)(allowance + 1500)) {
                cok_camp_notice(game, "Overloaded");
                cok_adventure_wait(game, game->speed * 100u);
                cok_adventure_flush(game); /* 58e7:0506 */
            } else {
                cok_field_set16_at(
                    L, g, COK_FIELD_COINS, coin,
                    (uint16_t)(cok_field_u16_at(L, g, COK_FIELD_COINS, coin) - amount));
                cok_field_set16(L, g, COK_FIELD_WEIGHT,
                                (uint16_t)(cok_field_u16(L, g, COK_FIELD_WEIGHT) - amount));
                cok_field_set16_at(
                    L, r, COK_FIELD_COINS, coin,
                    (uint16_t)(cok_field_u16_at(L, r, COK_FIELD_COINS, coin) + amount));
                cok_field_set16(L, r, COK_FIELD_WEIGHT,
                                (uint16_t)(cok_field_u16(L, r, COK_FIELD_WEIGHT) + amount));
            }
            stale = COK_SHEET_STALE_ZERO;
            if (!has_money(L, g)) return true;
        }
    }
}

/* Drop money (546c:2fe2; dkk 580d:26d0 with 1): kinds of coin and amounts
 * until it has none, or Escape; in shops and treasure (modes 1 and 6) and
 * the vault (7) the coins go to the pool, elsewhere they are gone. The
 * vault asks the deposit's question (the treasure section's). */
static bool drop_money(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    for (;;) {
        if (!cok_sheet_money(game, c)) return false;
        const char *coin_word;
        int coin = pick_coin(game, c, 18, true, &coin_word);
        if (coin < 0) return false;
        if (coin == 0x100) return true;
        char prompt[256];
        const cok_rules_treasure *T = &cok_game_rules(game)->treasure;
        snprintf(prompt, sizeof prompt, "%s%s%s", T->how_much, coin_word,
                 game->vm.mode == 7 && T->deposit != NULL ? T->deposit : "will you drop? ");
        int amount = cok_sheet_amount(game, prompt, cok_field_u16_at(L, c, COK_FIELD_COINS, coin));
        if (amount < 0) return false;
        cok_pool_drop(game, c, (uint16_t)amount, coin); /* 58e7:09fa */
        if (!has_money(L, c)) return true;
    }
}

/* Death Knights' View (COK_ITEMS_DEATH_KNIGHTS: dkk 580d:0c66). */

/* View's items by index (DS:3276). */
enum { DK_ITEMS, DK_SPELLS, DK_TRADE, DK_DEPOSIT, DK_DROP, DK_HEAL, DK_CURE, DK_EXIT };

/* Whether c may heal (580d:2e95) or cure (2ee0): of the class, outside
 * combat, okay, and for Heal without the effect it gives, for Cure with
 * +0x25 (the layout's knight tier) not 0. */
static bool may_heal(cok_adventure *game, cok_character *character, bool cure)
{
    const cok_rules_items *I = &cok_game_rules(game)->items;
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *c = character->record;
    if (I->heal_whom == NULL || cok_field_s8(L, c, COK_FIELD_CLASS) != (int8_t)I->heal_class ||
        game->vm.mode == 5 || cok_field_s8(L, c, COK_FIELD_STATUS) != 0)
        return false;
    if (cure) return cok_field_u8(L, c, COK_FIELD_KNIGHT_TIER) != 0;
    return cok_character_find_effect(character, I->heal_effect) == NULL; /* 65cd:2687 */
}

/* Heal (580d:2f47) or Cure (3018): the screen redrawn (65cd:2db0), one
 * picked from the party's first (65cd:33c9, with Exit); for Heal, the one
 * picked healed by twice the healer's level in the class (638c:206f, while
 * below its maximum), "NAME feels better" or "NAME is unaffected", and the
 * healer given the effect for its minutes (638c:1257); Cure does nothing
 * more. The sheet again. Returns false where the run ended. */
static bool heal(cok_adventure *game, cok_character *healer, bool cure)
{
    const cok_rules *R = cok_game_rules(game);
    const cok_rules_items *I = &R->items;
    const cok_character_layout *L = R->character;
    cok_adventure_redraw(game);
    bool ended;
    uint8_t *first = game->party.count > 0 ? game->party.members[0]->record : NULL; /* DS:a2c6 */
    uint8_t *who = cok_adventure_pick(game, cure ? I->cure_whom : I->heal_whom, first, true, &ended);
    if (ended) return false;
    if (cure) return cok_sheet_draw(game) && (who != NULL || cok_sheet_draw(game));
    if (who == NULL) return cok_sheet_draw(game);
    int8_t level = cok_field_s8_at(L, healer->record, COK_FIELD_CLASS_LEVEL, I->heal_class);
    bool healed = cok_character_heal(who, R, (uint8_t)(level * 2), true, game->vm.mode);
    char name[20], text[64];
    uint8_t stored = cok_field_u8_at(L, who, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(name, who + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    name[length] = '\0';
    snprintf(text, sizeof text, "%s%s", name, healed ? I->feels_better : I->unaffected);
    cok_camp_notice(game, text); /* 65cd:19c5 */
    if (cok_character_add_effect(healer, I->heal_effect, I->heal_minutes, 0, false) == NULL)
        return undefined(game, "out of memory");
    return cok_sheet_draw(game);
}

/* A menu by index on row 24 (6a69:0f1b), logged as it shows. */
static int pick(cok_adventure *game, const char *text, const cok_menu_items *items,
                bool *special)
{
    cok_menu_indexed_layout shown;
    cok_menu_indexed(&shown, text, items);
    cok_adventure_log(game, "menu", shown.text);
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key = cok_menu_pick(&game->screen, &game->font, "", text, items, 13, 15, 10, false,
                            &game->selected, &keys, special);
    game->menu_special = *special; /* DS:c0f8 */
    return key;
}

/* As Champions' View, but by index with its own selection, from the first
 * (DS:c0f7 put back before each ask), Escape as Exit; Trade, Deposit (in
 * the vault, mode 7) or Drop for steel, gems and jewelry; Heal and Cure;
 * the sheet drawn again after Items, Spells and Trade. Trade's flag
 * (580d:23a0's [bp-0x13c], tested at 26c0) holds what the stack held: the
 * byte View's menu (6a69:0f1b, 0x0c below View's call) left at its
 * [bp-0x130], 6a69:0fee's high word of the time it began, in hundredths of
 * a second since midnight by the DOS clock (14e4:09bd, GetTime) plus 30.
 * Its low byte is 0, so that Escape at the first coin list asks for
 * another partner, only in the 655 seconds after midnight (to 00:10:55);
 * the port has no clock of the day and takes any other time: Trade is
 * left. */
static void dk_view(cok_adventure *game, bool *done)
{
    cok_character *character = selected(game);
    *done = false;
    game->trade_partner = game->vm.character; /* DS:a2be */
    if (character == NULL) return; /* see cok_sheet */
    if (!cok_sheet_draw(game)) return; /* 580d:006e */
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    uint8_t *c = character->record, own = 1;
    int key = 0xff;
    while (key != DK_EXIT && !*done && !game->vm.abort) {
        bool memorized = false;
        if (cok_field_has_spells(L, c))
            for (size_t i = 0; i < cok_field_count(L, COK_FIELD_MEMORIZED); ++i)
                if ((cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) & 0x7f) != 0) memorized = true;
        bool money = has_money(L, c);
        bool may_trade = cok_field_u8(L, c, COK_FIELD_NPC) < 0x80 ||
                         cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0 ||
                         cok_field_u8(L, c, COK_FIELD_STATUS) == 1;
        cok_menu_items items = {0};
        if (character->item_count > 0) cok_menu_items_add(&items, DK_ITEMS);
        if (memorized) cok_menu_items_add(&items, DK_SPELLS);
        if (may_trade && money && game->vm.mode != 5) cok_menu_items_add(&items, DK_TRADE);
        if (money) cok_menu_items_add(&items, game->vm.mode == 7 ? DK_DEPOSIT : DK_DROP);
        if (may_heal(game, character, false)) cok_menu_items_add(&items, DK_HEAL);
        if (may_heal(game, character, true)) cok_menu_items_add(&items, DK_CURE);
        cok_menu_items_add(&items, DK_EXIT);
        game->selected = own;
        bool special;
        key = pick(game, R->items.view_menu, &items, &special);
        if (key < 0) return;
        if (special && key == 0x1b) key = DK_EXIT;
        own = game->selected;
        /* The index the menu gave, a special key's scan code alike. */
        bool ok = true;
        switch (key) {
        case DK_ITEMS: cok_items(game, done); ok = !game->vm.abort; break;
        case DK_SPELLS:
            /* 580d:2d2a: its list (5dc5:016e) ends only on its Exit, a
             * special key (DS:c0f8). */
            ok = cok_magic_memorized(game, character) >= 0;
            game->menu_special = true;
            break;
        case DK_TRADE: ok = trade_money(game, character, COK_SHEET_STALE_SET); break;
        case DK_DEPOSIT:
        case DK_DROP: ok = drop_money(game, character) && cok_sheet_money(game, c); break;
        case DK_HEAL: ok = heal(game, character, false); break;
        case DK_CURE: ok = heal(game, character, true); break;
        default: break;
        }
        if (!ok || game->vm.abort) return;
        if (!*done && key < DK_DEPOSIT && !cok_sheet_draw(game)) return;
    }
    cok_adventure_redraw(game); /* 65cd:2db0 */
}

/* View. */

void cok_sheet(cok_adventure *game, cok_sheet_stale stale, bool *done)
{
    if (cok_game_rules(game)->items.routine == COK_ITEMS_DEATH_KNIGHTS) {
        dk_view(game, done);
        return;
    }
    cok_character *character = selected(game);
    *done = false;
    game->trade_partner = game->vm.character;
    /* With nobody selected (nil, or a record freed: see ecl.h) the
     * original draws a sheet of whatever the pointer names, nil's the
     * interrupt table, and its menu writes there; the port leaves View at
     * once, a deliberate difference. */
    if (character == NULL) return;
    if (!cok_sheet_draw(game)) return;
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    int key = ' ';
    while (key != 0 && key != 'E' && !*done && !game->vm.abort) {
        char items[41] = "";
        bool memorized = false;
        for (size_t i = 0; i < cok_field_count(L, COK_FIELD_MEMORIZED); ++i)
            if (cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) != 0) memorized = true;
        bool may_trade = cok_field_u8(L, c, COK_FIELD_NPC) < 0x80 ||
                         cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0 ||
                         cok_field_u8(L, c, COK_FIELD_STATUS) == 1;
        if (character->item_count > 0) strcat(items, "Items ");
        if (memorized) strcat(items, "Spells ");
        if (may_trade && has_money(L, c) && game->vm.mode != 5) strcat(items, "Trade ");
        if (has_money(L, c)) strcat(items, "Drop ");
        strcat(items, "Exit");
        bool special;
        key = cok_camp_menu(game, "", items, false, false, &special);
        if (key < 0) return;
        /* Special keys count by their scan codes: PgUp is Items, Del
         * Spells, shift-F1 Trade and F10 Drop. */
        bool ok = true;
        switch (key) {
        case 'I': cok_items(game, done); ok = !game->vm.abort; break;
        case 'S': ok = cok_magic_memorized(game, character) >= 0; break;
        case 'T': ok = trade_money(game, character, stale); break;
        case 'D': ok = drop_money(game, character) && cok_sheet_money(game, c); break;
        default: break; /* Heal and Cure are never offered, and do nothing. */
        }
        if (!ok || game->vm.abort) return;
        /* A choice's calls leave the byte nonzero (checked for each). */
        if (key == 'I' || key == 'S' || key == 'T' || key == 'D') stale = COK_SHEET_STALE_SET;
        if (!*done && (key == 'I' || key == 'S' || key == 'T') && !cok_sheet_draw(game)) return;
    }
    cok_adventure_redraw(game);
}
