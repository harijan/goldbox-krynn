#ifndef COK_QUEEN_LEVELS_H
#define COK_QUEEN_LEVELS_H

#include "party.h"

/* The Dark Queen of Krynn's levels (dqk 8bcc: DARK-QUEEN.md's step 2, its
 * training and creation; step 4 has its combat's and effects' use of them):
 * what a character's levels give, as its original works it out from its
 * data segment's tables (data_dark_queen.c). Each takes the game's rules
 * (Dark Queen's), and where a table cannot be read as the original reads
 * it (a knight of no order has no row of experience, 177e:1f39 leaves it
 * unset) fails with error set. */

/* 177e:20ce: the hit points a constitution adds a level of class 0-6. */
int8_t cok_queen_constitution(const cok_rules *rules, const uint8_t *c, unsigned class_);
/* 86b8:354b: the most hit points the levels give. */
uint8_t cok_queen_most_hit_points(const cok_rules *rules, const uint8_t *c);

/* 177e:1f39: the row of the experience table a class of the character
 * uses, or -1 (a knight of no order of the Crown, Sword or Rose). */
int cok_queen_experience_row(const cok_rules *rules, const uint8_t *c, unsigned class_);
/* 8bcc:0e9c: the experience a character needs for level (past 0x28 it
 * is 0x7fffffff) in class 0-6; false with error set for a class with no
 * row. */
bool cok_queen_experience(const cok_rules *rules, const uint8_t *c, unsigned level,
                          unsigned class_, int32_t *experience, char *error, size_t error_size);
/* 177e:1fe5: whether the experience takes some class a level up (not
 * past 0x27, nor its race's top level). */
bool cok_queen_may_train(const cok_rules *rules, const uint8_t *c, bool *may, char *error,
                         size_t error_size);

/* 8bcc:000f, 01a2, 01e7: the spells a day and the spells the classes
 * know (cleric, knight, paladin, ranger, mage). */
void cok_queen_cleric_spells(const cok_rules *rules, uint8_t *c);
void cok_queen_spells(const cok_character *character, const cok_rules *rules);
/* 8bcc:0af3, 0c72: the thief's skills, the saving throws; the character's
 * items count for the latter. */
void cok_queen_thief_skills(const cok_rules *rules, uint8_t *c);
void cok_queen_saving_throws(const cok_character *character, const cok_rules *rules);

/* 8bcc:07f1: the levels recomputed (THAC0, the highest level, the attacks,
 * the spells a day, the saving throws, the thief's skills, the classes'
 * items usable, a readied item unusable now put by), as the original
 * runs it when a character joins the party from the roster (604b:2021),
 * trains or changes its order: not at a file's read nor a saved game's
 * load. A readied item of a power put by stops (86b8:16f7, the
 * items'). */
bool cok_queen_levels(cok_character *character, const cok_rules *rules,
                      const cok_item_types *types, char *error, size_t error_size);

/* 86b8:380e: the hit points a class or classes gain: those of classes (a
 * mask of DS:63fa's bits) of the character's levels, each level's the best
 * of two rolls of its dice (the first level's several), the sum divided by
 * divide (the classes counted) if that is not 0, added to the most hit
 * points and the base, the hit points kept as they were below the most
 * (add), or taken away (as 86b8:354b's most allows). */
void cok_queen_hit_points(cok_character *character, const cok_rules *rules, uint32_t *seed,
                          uint8_t classes, uint8_t divide, bool add);

/* 7368:0fe0: a knight's next order: its level lowered while its experience
 * falls short, the hit points with it, and the levels and may-train state
 * recomputed. */
bool cok_queen_new_order(cok_character *character, const cok_rules *rules,
                         const cok_item_types *types, char *error, size_t error_size);

#endif
