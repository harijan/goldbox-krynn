#ifndef COK_QUEEN_TRAIN_H
#define COK_QUEEN_TRAIN_H

#include "adventure.h"

/* The Dark Queen of Krynn's Train Character (dqk 6be8:0022) and Knight new
 * Order (7368:10c2) of the camp, and the training its creation runs
 * silently (66e5:1a90). Each works on a character (the selected one, DS:ad67)
 * and returns false where the run stopped (cok_adventure_fail or
 * cok_adventure_not_yet said why). */

/* One Train Character: the classes of the character's experience takes up
 * a level (those of its race's top levels not past; a knight of the Crown,
 * Sword or Rose to the 18th, the 18th or the 40th), in the training hall
 * (variable 0x30's bits, or anywhere with queen_train_anywhere), each a
 * level and the hit points of 86b8:380e. Not silent, it asks (the screen
 * "will become: a level n class" and "Do you wish to train? "), says
 * "Congratulations...", and holds the experience below the next
 * level's after; silent, it asks nothing and a mage's new spells are
 * those the original gives the creation (by its order and level). *nothing is
 * set where nothing could train (DS:b183) and the may-train state is 0. A
 * mage's or ranger's new spell of the training stops cited (849a:2080). */
bool queen_train(cok_adventure *game, cok_character *character, bool silent, bool *nothing);

/* 66e5:1a90's loop: variable 0x30 0xff, DS:bd92 0, silent, Train again
 * and again until it trains nothing more, the variable and DS:bd92 put
 * back. */
bool queen_train_silently(cok_adventure *game, cok_character *character);

/* Knight new Order: a knight of the Crown or the Sword with levels and
 * the experience, scores and level asked for, "may become a knight of the
 * sword" and Change or Exit; Change gives the next order (7368:0fe0). */
bool queen_knight_order(cok_adventure *game, cok_character *character);

#endif
