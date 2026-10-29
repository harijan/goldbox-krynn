#ifndef COK_DATA_DARK_QUEEN_MODIFY_H
#define COK_DATA_DARK_QUEEN_MODIFY_H

#include <stdint.h>

/* Tables of The Dark Queen of Krynn's data segment, DS offsets in the
 * comments of data_dark_queen_modify.c. */
extern const uint8_t cok_queen_limits[7][16];
extern const uint8_t cok_queen_minimums[17][6];
extern const uint8_t cok_queen_constitution_bonus[32];
extern const uint16_t cok_queen_spell_class[127];
extern const uint8_t cok_queen_spell_level[127];

#endif
