#ifndef COK_QUEEN_CREATE_H
#define COK_QUEEN_CREATE_H

#include "adventure.h"

/* The Dark Queen of Krynn's Create Character (dqk 66e5:2f95; the camp's
 * first button, 7368:046f): the form of race, gender, class and
 * alignment (66e5:2a6c), a cleric's deity (66e5:36b0), the rolls and the
 * sheet with Reroll Stats, Modify, Done and Exit (66e5:1f25), the name
 * (66e5:193f), the icon (66e5:0cf1), and the character saved as
 * <name>.QCH with the spells and items it starts with. False where the
 * run stopped. */
bool queen_create(cok_adventure *game);

#endif
