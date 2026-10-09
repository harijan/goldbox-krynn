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

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* The name at DS:base + stride * index, index a signed byte, as the
 * original reads it, or false where it would read past the name strings. */
static bool table_name(cok_adventure *game, uint16_t base, int stride, int8_t index, char out[256])
{
    long at = base + (long)stride * index;
    if (at >= 0 && at <= 0xffff && cok_ds_string((uint16_t)at, out)) return true;
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
    char s[256];
    if (!table_name(game, 0x0f5a, 27, (int8_t)c[0x5b], s)) return false;
    if ((int8_t)c[0xf9] > 0) {
        char deity[256], line[256];
        if (!table_name(game, 0x1259, 12, (int8_t)c[0x5d], deity)) return false;
        line[0] = '\0';
        append(line, deity);
        append(line, " ");
        append(line, s);
        memcpy(s, line, sizeof s);
    }
    size_t length = strlen(s);
    memcpy(buffer + 1, s, length);
    for (size_t i = 0; i <= length; ++i) known[i] = true;
    if ((int8_t)c[0xfe] > 0) {
        size_t at = 0;
        for (;;) {
            ++at;
            if (!known[at] || (buffer[at] == 'M' && !known[at + 2 < 256 ? at + 2 : 255]))
                return undefined(game, "the class line's search for \"Mage\" reads bytes the "
                                       "original leaves unset (546c:028f)");
            if (buffer[at] == 'M' && buffer[at + 2] == 'g') break;
            if (at > length) break;
        }
        char order[256], insert[256];
        if (!cok_ds_string((uint16_t)(0x12b9 + 7 * c[0x5e]), order))
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
    if ((int8_t)c[0x100] > 0) {
        static const char *const orders[4] = {"", " of the Crown", " of the Sword", " of the Rose"};
        if (c[0x5c] >= 1 && c[0x5c] <= 3) append(s, orders[c[0x5c]]);
    }
    snprintf(out, 256, "%s", s);
    return true;
}

/* 66c2:0eab: a human's level in its first class with one, the knight
 * level if none; 0 for others. */
static int8_t first_level(const uint8_t *c)
{
    if (c[0x5a] != 6) return 0;
    size_t i = 0;
    while (i < 7 && c[0xf9 + i] == 0) ++i;
    return (int8_t)c[0xf9 + i];
}

/* Ability i on row 9 + i (546c:0b50): the current score from column 5, 6
 * below 10; exceptional strength at 18 as "(NN)", "(00)" for 100; and "*"
 * at column 12 when it differs from the base. */
void cok_sheet_ability(cok_adventure *game, const uint8_t *c, int i, uint8_t color)
{
    uint8_t score = c[0x11 + 2 * i];
    char text[16];
    clear_cells(game, 5, 9 + i, 0x0b, 9 + i);
    snprintf(text, sizeof text, "%u", score);
    draw(game, text, score < 10 ? 6 : 5, 9 + i, color);
    if (i == 0 && score == 18 && c[0x1c] > 0) {
        char ex[8];
        snprintf(ex, sizeof ex, "%s%u", c[0x1c] < 10 ? "0" : "", c[0x1c]);
        if (c[0x1c] == 100) snprintf(ex, sizeof ex, "00");
        snprintf(text, sizeof text, "(%s)", ex);
        draw(game, text, 7, 9, color);
    }
    if (score != c[0x10 + 2 * i] || (i == 0 && c[0x1d] != c[0x1c])) draw(game, "*", 12, 9 + i, color);
}

/* The money (546c:0667): each kind but silver that the character has, from
 * row 9, its name at column 20 and its amount ending at column 37. */
bool cok_sheet_money(cok_adventure *game, const uint8_t *c)
{
    clear_cells(game, 0x14, 9, 0x26, 0x0f);
    int row = 9;
    for (int i = 1; i <= 6; ++i) {
        uint16_t amount = word(c + 0xeb + 2 * i);
        if (amount == 0) continue;
        char name[256], text[8];
        if (!table_name(game, 0x12e3, 11, (int8_t)i, name)) return false;
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
    char text[8], value[8];
    snprintf(text, sizeof text, "%u", c[0x197]);
    draw(game, text, x, y, highlight ? 13 : c[0x197] < c[0x62] ? 14 : 10);
    if (!show_max) return;
    snprintf(value, sizeof value, "/%u", c[0x62]);
    draw(game, value, x + (int)strlen(text), y, 10);
}

bool cok_sheet_figures(cok_adventure *game, cok_character *character)
{
    char error[300], text[64], value[48];
    if (!cok_character_stats(character, &game->item_types, error, sizeof error))
        return undefined(game, error);
    const uint8_t *c = character->record;
    draw(game, "Hit Points ", 0x14, 3, 15);
    clear_cells(game, 0x1f, 3, 0x25, 3);
    /* 6346:0a0d: hit points, yellow while below the maximum, "/" and it. */
    cok_sheet_hit_points(game, c, 0x1f, 3, false, true);
    draw(game, "Armor Class", 1, 0x11, 15);
    uint8_t ac = c[0x18d];
    int x = ac >= 1 && ac <= 0x32 ? 0x10 : ac >= 0x33 && ac <= 0x3c ? 0x11 : ac >= 0x3d && ac <= 0x45 ? 0x10 : 0x0f;
    clear_cells(game, 0x0f, 0x11, 0x11, 0x11);
    /* 6346:0984: |60 - AC field| as a byte, "-" before it above 60. */
    snprintf(text, sizeof text, "%s%u", ac > 60 ? "-" : "", (unsigned)abs(ac - 60) & 0xffu);
    draw(game, text, x, 0x11, 10);
    draw(game, "Encumbrance", 0x14, 0x11, 15);
    snprintf(value, sizeof value, "%u", word(c + 0x17d));
    pad(text, sizeof text, value, 5);
    draw(game, text, 0x21, 0x11, 10);
    draw(game, "THAC0   ", 1, 0x12, 15);
    snprintf(value, sizeof value, "%u", (uint8_t)(60 - c[0x18c]));
    pad(text, sizeof text, value, 2);
    draw(game, text, 0x10, 0x12, 10);
    uint8_t movement = c[0x198];
    if (cok_character_find_effect(character, 0x27) != NULL) movement = (uint8_t)(movement * 2);
    if (cok_character_find_effect(character, 0x2a) != NULL) movement = (uint8_t)(movement >> 1);
    draw(game, "Movement", 0x14, 0x12, 15);
    snprintf(value, sizeof value, "%u", movement);
    pad(text, sizeof text, value, 3);
    draw(game, text, 0x23, 0x12, 10);
    int8_t bonus = (int8_t)c[0x195];
    snprintf(value, sizeof value, "%ud%u", c[0x191], c[0x193]);
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
    uint8_t *c = character->record;
    uint16_t moons[3];
    for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
    cok_screen_sheet(&game->screen, &game->view.tiles[4], moons);
    char name[20], status[256], gender[256], alignment[256], race[256], line[256];
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(name, c + 1, length);
    name[length] = '\0';
    cok_adventure_log(game, "print", name);
    uint8_t color = c[0x189] == 0 ? 12 : game->vm.mode == 5 && c[0x18a] == 1 ? 14 : 11;
    draw(game, name, 1, 1, color);
    draw(game, "Status:", 0x14, 1, 15);
    if (!table_name(game, 0x1330, 13, (int8_t)c[0x188], status)) return false;
    draw(game, status, 0x1b, 1, 10);
    if (!table_name(game, 0x12d5, 7, (int8_t)c[0x109], gender)) return false;
    draw(game, gender, 1, 3, 15);
    snprintf(line, sizeof line, "%u years", word(c + 0x60));
    draw(game, line, 8, 3, 15);
    if (!table_name(game, 0x11c0, 17, (int8_t)c[0x10a], alignment)) return false;
    draw(game, alignment, 1, 4, 15);
    if (!table_name(game, 0x1140, 16, (int8_t)c[0x5a], race)) return false;
    draw(game, race, 0x14, 4, 15);
    const char *before[3] = {gender, alignment, race};
    if (!class_line(game, c, before, line)) return false;
    cok_adventure_log(game, "print", line);
    draw(game, line, 1, 5, 15);
    draw(game, "Level", 1, 7, 15);
    /* Each class with a level, or with a former level below the first
     * class's (66c2:0eab), the sum of both as a byte. */
    char levels[256] = "";
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        int8_t now = (int8_t)c[0xf9 + i], before_level = (int8_t)c[0x101 + i];
        if (now <= 0 && !(before_level < first_level(c) && before_level > 0)) continue;
        char value[8];
        snprintf(value, sizeof value, "%u", (uint8_t)(now + before_level));
        if (any) append(levels, "/");
        append(levels, value);
        any = true;
    }
    draw(game, levels, 7, 7, 15);
    uint32_t bits = (uint32_t)c[0x116] | (uint32_t)c[0x117] << 8 | (uint32_t)c[0x118] << 16 |
                    (uint32_t)c[0x119] << 24;
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

/* The coin a row of the coin list names, by its first letter (58e7:0b95),
 * and the word the question uses. */
static int coin_of(const char *row, const char **word_out)
{
    while (*row == ' ') ++row;
    static const struct { int coin; const char *word; } coins[] = {
        {5, "Gems "}, {3, "platinum "}, {2, "bronze "}, {0, "silver "},
        {4, "steel "}, {1, "Copper "}, {6, "Jewelry "},
    };
    int k;
    switch (row[0]) {
    case 'G': k = 0; break;
    case 'P': k = 1; break;
    case 'B': k = 2; break;
    case 'S': k = (row[1] == 'i' || row[1] == 'I') ? 3 : 4; break;
    case 'C': k = 5; break;
    case 'J': k = 6; break;
    default: return -1;
    }
    *word_out = coins[k].word;
    return coins[k].coin;
}

/* Pick a kind of coin the character has (546c:2c75, 2fe2): a list over the
 * money, Copper first, each row the name, spaces and the amount, padded by
 * pad_to less the name's length (and the amount's, for Drop), the padding
 * at most 15. Returns the coin, 0x100 for none, or -1 when input ended. */
static int pick_coin(cok_adventure *game, const uint8_t *c, int pad_to, bool count_amount,
                     const char **coin_word)
{
    static char text[6][41];
    cok_menu_row rows[6];
    size_t count = 0;
    for (int i = 1; i <= 6; ++i) {
        uint16_t amount = word(c + 0xeb + 2 * i);
        if (amount == 0) continue;
        char name[256], number[16], padding[16] = "";
        if (!table_name(game, 0x12e3, 11, (int8_t)i, name)) return -1;
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
    cok_menu_style style = {"Select type of coin ", " Select", 15, 10, 13, true};
    bool redraw = true;
    int index = 0;
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key = cok_menu_rows(&game->screen, &game->font, rows, count,
                            (cok_text_window){0x14, 9, 0x26, 0x0f}, &style, &redraw, &index,
                            &game->list_top, &game->selected, &keys);
    if (key < 0) return -1;
    if (key == 0 || count == 0) return 0x100;
    int coin = coin_of(text[index], coin_word);
    if (coin < 0) {
        undefined(game, "a coin's row starts with an unknown letter (58e7:0b95)");
        return -1;
    }
    char choice[2] = {text[index][0], '\0'};
    cok_adventure_log(game, "choice", choice);
    return coin;
}

static bool has_money(const uint8_t *c)
{
    for (int i = 1; i <= 6; ++i)
        if (word(c + 0xeb + 2 * i) != 0) return true;
    return false;
}

static void put_word(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* Trade money (546c:2c75): "Trade to?" from the last partner (DS:46b0),
 * then the giver's sheet, and kinds of coin and amounts until it has none
 * left, or Escape in the coin list asks for another partner. */
static bool trade_money(cok_adventure *game, cok_character *giver, cok_sheet_stale stale)
{
    uint8_t *g = giver->record;
    for (;;) {
        cok_adventure_redraw(game);
        bool ended;
        uint8_t *who = cok_adventure_pick(game, "Trade to?", game->trade_partner, true, &ended);
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
                /* The flag that would end Trade ([bp-0x13c], 546c:2f9b) is
                 * set only by an amount, to whether it took the last coin;
                 * before any, it is the byte the stack held. */
                if (stale == COK_SHEET_STALE_SET) return true;
                if (stale == COK_SHEET_STALE_UNKNOWN)
                    return undefined(game, "Escape at the first coin list tests an "
                                           "uninitialized byte (546c:2f9b)");
                break;
            }
            char prompt[256];
            snprintf(prompt, sizeof prompt, "How much %swill you trade? ", coin_word);
            int amount = cok_sheet_amount(game, prompt, word(g + 0xeb + 2 * coin));
            if (amount < 0) return false;
            /* 58e7:044b: within what the receiver may carry, as words. */
            int16_t allowance;
            char error[300];
            if (!cok_character_allowance(receiver->record, &allowance, error, sizeof error))
                return undefined(game, error);
            uint8_t *r = receiver->record;
            if ((uint16_t)(word(r + 0x17d) + amount) > (uint16_t)(allowance + 1500)) {
                cok_camp_notice(game, "Overloaded");
                cok_adventure_wait(game, game->speed * 100u);
            } else {
                put_word(g + 0xeb + 2 * coin, (uint16_t)(word(g + 0xeb + 2 * coin) - amount));
                put_word(g + 0x17d, (uint16_t)(word(g + 0x17d) - amount));
                put_word(r + 0xeb + 2 * coin, (uint16_t)(word(r + 0xeb + 2 * coin) + amount));
                put_word(r + 0x17d, (uint16_t)(word(r + 0x17d) + amount));
            }
            stale = COK_SHEET_STALE_ZERO;
            if (!has_money(g)) return true;
        }
    }
}

/* Drop money (546c:2fe2): kinds of coin and amounts until it has none, or
 * Escape; in shops and treasure (modes 1 and 6) the coins go to the pool,
 * elsewhere they are gone. */
static bool drop_money(cok_adventure *game, cok_character *character)
{
    uint8_t *c = character->record;
    for (;;) {
        if (!cok_sheet_money(game, c)) return false;
        const char *coin_word;
        int coin = pick_coin(game, c, 18, true, &coin_word);
        if (coin < 0) return false;
        if (coin == 0x100) return true;
        char prompt[256];
        snprintf(prompt, sizeof prompt, "How much %swill you drop? ", coin_word);
        int amount = cok_sheet_amount(game, prompt, word(c + 0xeb + 2 * coin));
        if (amount < 0) return false;
        cok_pool_drop(game, c, (uint16_t)amount, coin); /* 58e7:09fa */
        if (!has_money(c)) return true;
    }
}

/* View. */

void cok_sheet(cok_adventure *game, cok_sheet_stale stale, bool *done)
{
    cok_character *character = selected(game);
    *done = false;
    game->trade_partner = game->vm.character;
    if (character == NULL) {
        undefined(game, "View with no character selected reads through NULL (546c:0d74)");
        return;
    }
    if (!cok_sheet_draw(game)) return;
    uint8_t *c = character->record;
    int key = ' ';
    while (key != 0 && key != 'E' && !*done && !game->vm.abort) {
        char items[41] = "";
        bool memorized = false;
        for (size_t i = 0; i < 0x3a; ++i)
            if (c[0x1e + i] != 0) memorized = true;
        bool may_trade = c[0xe7] < 0x80 || c[0x189] == 0 || c[0x188] == 1;
        if (character->item_count > 0) strcat(items, "Items ");
        if (memorized) strcat(items, "Spells ");
        if (may_trade && has_money(c) && game->vm.mode != 5) strcat(items, "Trade ");
        if (has_money(c)) strcat(items, "Drop ");
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
        stale = key == 'I' || key == 'S' || key == 'T' || key == 'D' ? COK_SHEET_STALE_SET
                                                                     : COK_SHEET_STALE_UNKNOWN;
        if (!*done && (key == 'I' || key == 'S' || key == 'T') && !cok_sheet_draw(game)) return;
    }
    cok_adventure_redraw(game);
}
