#ifndef COK_ITEMS_H
#define COK_ITEMS_H

#include "adventure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Items: their names (6346:0488) and View's Items menu (546c:17f9), from
 * overlays 6346 and 546c. An item's name is built from up to three parts
 * (+0x2f, +0x30, +0x31) out of the name table (DS:1390, 21 bytes a part),
 * each hidden while its bit of +0x35 is set (0x04, 0x02, 0x01) until the
 * item is identified, and is kept in the item's first 41 bytes, a Pascal
 * string[40], as the original keeps it. */

/* The original's strings for names (DS:0f5a-1dd3): classes (DS:0f5a, 27
 * bytes each), races (1140, 16), alignments (11c0, 17), deities (1259, 12),
 * orders of magic (12b9, 7), genders (12d5, 7), coins (12e3, 11), statuses
 * (1330, 13), item name parts (1390, 21) and directions (1dbc, 3). */
extern const cok_ds_table cok_name_table;

/* The Pascal string at DS:offset, copied into out, or false if it is not
 * all within cok_name_table, where the original would read other data, or
 * holds a NUL, which out could not. */
bool cok_ds_string(uint16_t offset, char out[256]);

/* Build item's name into its first 41 bytes and out (6346:0488): " Yes  "
 * or " No   " by whether it is readied, if readied_column; "* " for an
 * item with a bonus (+0x32 or +0x33 above 0) or a curse (+0x36) while
 * anyone in the party has effect 5 (Detect Magic); its count (+0x39) if
 * not 0; then the parts shown, +0x31 first, each followed by a space, or
 * by "s " for the part that makes a count above 1 plural. Strings are cut
 * to 40 characters as they are joined. Returns false, ending the run with
 * COK_ECL_UNDEFINED, for a part past the name table. */
bool cok_item_name(cok_adventure *game, uint8_t *item, bool readied_column, char out[41]);

/* The Items menu (546c:17f9) for the selected character, until Exit or
 * Escape, *done is set, or it has no items: the list of its items with
 * "Ready Use Trade Drop Halve Join", as the character and the items allow
 * (see README). Use sets *done when it used an item, which only combat
 * keeps. The stats are recomputed (6346:0d20) after every key. */
void cok_items(cok_adventure *game, bool *done);

#endif
