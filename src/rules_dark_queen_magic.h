#ifndef COK_RULES_DARK_QUEEN_MAGIC_H
#define COK_RULES_DARK_QUEEN_MAGIC_H

#include "rules.h"

/* Dark Queen's spell and effect tables (rules_dark_queen_magic.c), which
 * its descriptor (rules_dark_queen.c) holds by pointer. */

extern const char *const cok_dark_queen_spell_names[0x8c];
extern const char *const cok_dark_queen_spell_levels[10];
extern const cok_rules_cast cok_dark_queen_spell_cast[0x8d];
extern const uint8_t cok_dark_queen_spell_ids[COK_SPELL_NAMES];
extern const uint8_t cok_dark_queen_effect_ids[COK_EFFECT_NAMES];
extern const uint8_t *const cok_dark_queen_event_lists[0x19];
extern const uint8_t cok_dark_queen_shared[];
extern const uint8_t cok_dark_queen_resisted_pending[];
extern const cok_rules_effect cok_dark_queen_effect_handlers[0xe6];

#endif
