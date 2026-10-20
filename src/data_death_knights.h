#ifndef COK_DATA_DEATH_KNIGHTS_H
#define COK_DATA_DEATH_KNIGHTS_H

#include <stdint.h>

/* Death Knights' data segment's tables (data_death_knights.c), which only
 * its descriptor (rules_death_knights.c) reads. */

extern const uint8_t cok_death_knights_combat[172];
extern const uint8_t cok_death_knights_alignment_codes[6];
extern const uint8_t cok_death_knights_turning[155];
extern const uint8_t cok_death_knights_cone[296];
extern const uint8_t cok_death_knights_names[1099];
extern const uint8_t cok_death_knights_item_words[2625];
extern const uint8_t cok_death_knights_line_directions[9];
extern const uint8_t cok_death_knights_colours[32];
extern const uint8_t cok_death_knights_terrain[350];
extern const uint8_t cok_death_knights_cloud_cells[9];
extern const uint8_t cok_death_knights_spells[1840];
extern const uint8_t cok_death_knights_classes[4322];
extern const uint8_t cok_death_knights_level_limits[56];
extern const uint8_t cok_death_knights_stock[85];

#endif
