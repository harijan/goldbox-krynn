#ifndef COK_DATA_DARK_QUEEN_H
#define COK_DATA_DARK_QUEEN_H

#include "data.h"

/* Dark Queen's data segment's tables (data_dark_queen.c), as runs of the
 * original's bytes by their DS offsets: cok_dark_queen_data holds them for
 * cok_data_byte and its kin (data.h). */

extern const cok_data_segment cok_dark_queen_data;

/* DS:631e: the 18 effects that the end of combat removes from each party record (75cc:0593's
 * loop 3, 148f:017d(record, id, NULL) for each). */
#define COK_DARK_QUEEN_END_EFFECTS 18
extern const uint8_t cok_dark_queen_end_effects[COK_DARK_QUEEN_END_EFFECTS];

#endif