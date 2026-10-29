#ifndef COK_DATA_DARK_QUEEN_ITEMS_H
#define COK_DATA_DARK_QUEEN_ITEMS_H

#include <stdint.h>

/* A word of an item's name and the data segment offset of its string. */
typedef struct {
    uint16_t offset;
    const char *text;
} cok_queen_word;

enum { COK_QUEEN_ITEM_WORDS = 155 };

extern const cok_queen_word cok_queen_item_words[COK_QUEEN_ITEM_WORDS];

#endif
