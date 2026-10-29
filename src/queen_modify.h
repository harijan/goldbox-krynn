#ifndef COK_QUEEN_MODIFY_H
#define COK_QUEEN_MODIFY_H

#include "adventure.h"

/* The Dark Queen of Krynn's Modify Character (dqk 6a87:1293), which the
 * camp's button and the Create Character sheet's Modify call: the sheet with
 * the bar "Modify: Next Previous Add Sub Keep Exit" and a score, the name or
 * the hit points lit, which Add and Sub change within the race's and the
 * class's limits (6a87:03c7-0b84, 1188), the name edited in place (0d30),
 * until Keep (6a87:025e) or Exit (6a87:01e5).
 *
 * character is the one selected (DS:ad67); creating is the original's
 * silent flag (1bdb:21d2, which Create sets): the name is then not edited
 * here. False only where the run stopped (the keys ran out, or a routine
 * not ported was reached: the game's error says which). */
bool queen_modify(cok_adventure *game, cok_character *character, bool creating);

/* 66e5:30d3: the mage's spell book as its levels and order of magic give it
 * (all those of the first level, then each level's by the spells a day;
 * every mage spell for a ninth level's), the known spells' bits added to,
 * and 6a87:1599: every mage spell forgotten first. */
void queen_forget_mage_spells(const cok_character_layout *L, uint8_t *c);
void queen_mage_spells(const cok_character_layout *L, uint8_t *c);

#endif
