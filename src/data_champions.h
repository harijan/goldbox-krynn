#ifndef COK_DATA_CHAMPIONS_H
#define COK_DATA_CHAMPIONS_H

#include <stdint.h>

/* Champions of Krynn's data segment as the port copied it
 * (data_champions.c), for its descriptor (rules.c) and the tests; shared
 * code reads it through the descriptor (data.h). */

extern const uint8_t cok_champions_ways[37];
extern const uint8_t cok_champions_quadrants[4][4];
extern const int8_t cok_champions_start_columns[2][4];
extern const int8_t cok_champions_start_rows[2][4];
extern const uint8_t cok_champions_moves[4][4];
extern const int8_t cok_champions_formations[5][6][2];
extern const uint8_t cok_champions_quadrant_directions[4];
extern const int8_t cok_champions_beside_x[4];
extern const int8_t cok_champions_beside_y[4];
extern const uint8_t cok_champions_terrain_flags[16];
extern const uint8_t cok_champions_turning[0x50f - 0x451 + 1];
extern const uint8_t cok_champions_alignment_codes[6];
extern const uint8_t cok_champions_level_dice[8];
extern const uint8_t cok_champions_die_sides[8];
extern const uint8_t cok_champions_knight_scores[2][6];
extern const uint8_t cok_champions_treasure_templates[0x72];
extern const uint8_t cok_champions_cone_sides[0x118];
extern const uint8_t cok_champions_battle_only[16];
extern const uint8_t cok_champions_names[3706];
extern const uint8_t cok_champions_line_directions[3][3];
extern const uint8_t cok_champions_same_colours[16];
extern const uint8_t cok_champions_black_colours[16];
extern const uint8_t cok_champions_sound_words[14];
extern const int8_t cok_champions_step_x[9];
extern const int8_t cok_champions_step_y[9];
extern const uint8_t cok_champions_terrain[66][4];
extern const int8_t cok_champions_footprints[8][8];
extern const uint8_t cok_champions_helpless[4];
extern const uint8_t cok_champions_cloud_cells[5];
extern const uint8_t cok_champions_spells[854];
extern const uint8_t cok_champions_data_3509[889];
extern const uint8_t cok_champions_spell_levels[54];
extern const uint8_t cok_champions_spell_classes[100];
extern const uint8_t cok_champions_thac0[104];
extern const uint8_t cok_champions_class_bits[8];
extern const uint8_t cok_champions_thief_level[96];
extern const int8_t cok_champions_thief_race[56];
extern const int8_t cok_champions_thief_dexterity[55];
extern const uint8_t cok_champions_data_38f2[31];
extern const uint8_t cok_champions_data_39e0[697];
extern const uint8_t cok_champions_icon_colours[6];
extern const uint8_t cok_champions_spells_a_day[891];
extern const uint8_t cok_champions_saving_throws[480];
extern const uint8_t cok_champions_cleric_spells[40];
extern const uint8_t cok_champions_data_3ff4[103];
extern const uint8_t cok_champions_data_4263[349];

#endif
