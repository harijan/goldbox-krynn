#ifndef COK_MODIFY_H
#define COK_MODIFY_H

#include "adventure.h"

#include <stdint.h>

/* Modify Character (4def:28fa), from the start menu: a character that
 * has not yet adventured (its experience one a new character starts with,
 * and +0xd7 0) has its six scores, its name and its maximum hit points
 * changed within its race's and class's limits; Keep makes the scores its
 * base, Exit restores them. */
void cok_modify(cok_adventure *game);

/* The least (4def:4808) and the most (4def:48e9) maximum hit points the
 * class levels of c allow, with its constitution's bonus: each class's
 * level, a ranger's and a knight's one more, with the bonus (4def:4789),
 * divided among the classes; and each class's dice at their most, a
 * cleric's of a deity up to 4 and a ranger's and knight's a level more,
 * with the bonus of the selected character's constitution (4def:257c), or
 * a fixed amount at its top level. 48e9 returns false where 4def:257c's
 * result is uninitialized (a constitution outside 3-19). */
uint8_t cok_modify_least(const uint8_t *c);
bool cok_modify_most(const uint8_t *c, const uint8_t *selected, uint8_t *most);

#endif
