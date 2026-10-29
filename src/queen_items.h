#ifndef COK_QUEEN_ITEMS_H
#define COK_QUEEN_ITEMS_H

#include "party.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The words of an item's name, as an item of The Dark Queen of Krynn
 * (rules' item_record: 17 bytes of its file at +0x28 of the port's item)
 * holds them: +0x29-+0x2b the ids of up to three, +0x33 the bits (3, 2 and
 * 1 for the words +0x29-+0x2b) of those not yet identified. */

/* The name of an item as dqk 177e:03fc builds it: with ready_column the
 * column " Yes\\t" or " No\\t " by whether it is readied (+0x32); then "* "
 * where an effect 5 is on some member of the party and the item has a
 * bonus (+0x30, +0x31 above 0) or +0x34; its count (+0x35) and a blank,
 * but a scroll's (type 0x49); then its words last to first, those that are
 * identified, each with a blank after it, and "s " in place of it after the
 * word that a count of 2 or more makes plural (where the words are a
 * count's items). Written to out (size bytes, NUL-ended). */
void queen_item_name(const cok_party *party, const uint8_t *item, bool ready_column, char *out,
                     size_t size);

/* The word of an item's name by id, "" for none. */
const char *queen_item_word(unsigned id);

#endif
