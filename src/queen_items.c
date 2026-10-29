#include "queen_items.h"

#include "data_dark_queen_items.h"

#include <stdio.h>
#include <string.h>

/* An item's bytes (rules_dark_queen.c's item_record, dqk 177e:03fc). */
enum {
    ITEM_TYPE = 0x28, ITEM_WORD = 0x28, /* + 1-3 */
    ITEM_BONUS = 0x30, ITEM_SAVE = 0x31, ITEM_READIED = 0x32, ITEM_UNKNOWN = 0x33,
    ITEM_FLAG = 0x34, ITEM_COUNT = 0x35, SCROLL = 0x49,
};

const char *queen_item_word(unsigned id)
{
    return id < COK_QUEEN_ITEM_WORDS ? cok_queen_item_words[id].text : "";
}

/* name and its text (a string without bounds in the original's 35). */
static void append(char *out, size_t size, const char *text)
{
    size_t n = strlen(out);
    if (n + 1 < size) snprintf(out + n, size - n, "%s", text);
}

void queen_item_name(const cok_party *party, const uint8_t *item, bool ready_column, char *out,
                     size_t size)
{
    out[0] = '\0';
    if (ready_column) append(out, size, item[ITEM_READIED] != 0 ? " Yes\t" : " No\t ");
    bool detected = false;
    for (size_t i = 0; i < party->count && !detected; ++i)
        detected = cok_character_find_effect(party->members[i], 5) != NULL; /* 177e:1734 */
    if (detected && ((int8_t)item[ITEM_BONUS] > 0 || (int8_t)item[ITEM_SAVE] > 0 ||
                     item[ITEM_FLAG] != 0))
        append(out, size, "* ");
    char text[16];
    if (item[ITEM_COUNT] != 0 && item[ITEM_TYPE] != SCROLL) {
        snprintf(text, sizeof text, "%u", item[ITEM_COUNT]);
        append(out, size, text);
        append(out, size, " ");
    }
    /* The words not yet identified are not shown: bit 3 - i of +0x33. */
    unsigned shown = 0;
    for (unsigned i = 1; i <= 3; ++i)
        if (item[ITEM_WORD + i] != 0 && ((item[ITEM_UNKNOWN] >> (3 - i)) & 1) == 0)
            shown += 1u << (i - 1);
    bool plural = false;
    for (unsigned i = 3; i >= 1; --i) {
        if (((shown >> (i - 1)) & 1) == 0) continue;
        if (item[ITEM_TYPE] == SCROLL && i == 2) {
            snprintf(text, sizeof text, "%u", item[ITEM_COUNT]);
            append(out, size, text);
        } else {
            append(out, size, queen_item_word(item[ITEM_WORD + i]));
        }
        bool s = false;
        if (item[ITEM_COUNT] >= 2 && !plural) {
            uint8_t type = item[ITEM_TYPE];
            if (1u << (i - 1) == shown)
                s = true;
            else if (i == 1 && shown > 4)
                s = true;
            else if (i == 1 && type == SCROLL)
                s = true;
            else if (i == 2 && (shown & 1) == 0)
                s = true;
            else if (i == 3 && (type == 0x37 || type == 0x1e || type == 0x05 || type == 0x07 ||
                                type == 0x0c) &&
                     item[ITEM_WORD + 3] != 0x5d)
                s = true;
        }
        if (s) {
            append(out, size, "s ");
            plural = true;
        } else {
            append(out, size, " ");
        }
    }
}
