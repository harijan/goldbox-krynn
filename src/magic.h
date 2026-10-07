#ifndef COK_MAGIC_H
#define COK_MAGIC_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Spells, and the camp's Magic menu (overlay 4888, with the spell lists of
 * overlays 546c and 5b04). A character's 58 bytes from +0x1e hold the
 * spells it has memorized, by id; one with bit 7 set is marked to be
 * learned when the party rests. A scroll (item type 0x27 or 0x28) holds up
 * to three spells at +0x3c-+0x3e, bit 7 set while marked to be scribed.
 * Casting (4888:0a0d, 5b04:1415) is not ported. */

enum { COK_SPELLS = 0x6c }; /* Ids 1-0x6b have names. */

/* The spell table (DS:31b3, 16 bytes an id): its class (0 cleric, 1 druid,
 * 2 granted by a deity, 3 magic-user, 4 none), its level, and the effect
 * it adds (byte 10). Ids up to 0x7f read the original's data past the
 * table, as the original does. */
uint8_t cok_spell_class(uint8_t id);
uint8_t cok_spell_level(uint8_t id);
uint8_t cok_spell_effect(uint8_t id);
/* The name of spell id 1-0x6b (DS:2077 + 41 * id), or NULL for other ids,
 * whose names would be other data. */
const char *cok_spell_name(uint8_t id);

/* Whether an item is a scroll (5b04:5728): type 0x27 or 0x28. */
bool cok_item_is_scroll(const uint8_t *item);

/* The Magic menu (4888:1c32), "Cast Memorize Scribe Display Rest Exit",
 * for the selected character, until Exit or a rest is interrupted, which
 * sets *interrupted. Cast is logged as unported. */
void cok_magic(cok_adventure *game, bool *interrupted);

/* Sort c's memorized spells by id, ignoring the mark, so that empty bytes
 * come first (4888:0fb2). */
void cok_magic_sort(uint8_t *record);

/* How many more spells of level and class c may memorize (4888:0700),
 * counting those memorized and marked, and set *count to the number it
 * has. Class 2, the powers a deity grants, counts against 1, 3 for deity
 * 4 or 0 for deity 6 for a cleric; for anyone else, against an
 * uninitialized byte, whose value the caller's stack decides: stale, or if
 * it is below 0, unknown, and this returns false. It also returns false
 * where a level or class past the spell table's would read a far pointer
 * of the record. */
bool cok_magic_slots(const uint8_t *record, uint8_t level, uint8_t class_, int stale,
                     uint8_t *left, uint8_t *count);

#endif
