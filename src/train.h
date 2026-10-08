#ifndef COK_TRAIN_H
#define COK_TRAIN_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Training (4def:4d9e) and Knight Change Classes (4def:5812) from the
 * start menu (overlay 4def), with the hit points a level brings
 * (4def:4b3a). Training is free: nothing is paid. */

/* The experience a level needs (DS:3c65, ten rows of 99 bytes, a LongInt
 * per level from the row's start): the row of class for c (6346:371e) is 0
 * or 1 for a cleric by its deity (+0x5d above 4), 2 a fighter, 3-5 a
 * knight by its order (+0x5c 1-3), 6 a ranger, 7 or 8 a mage by its order
 * of magic (+0x5e 1 or not) and 9 a thief. For a druid or paladin, or a
 * knight of another order, 6346:371e returns its uninitialized local:
 * *row is then left as it was and the result is false. Training gives
 * such a class the previous class's row, or for the first 0x74, which
 * Pick Character leaves (see train.c): never trainable, a stop under free
 * training. */
bool cok_train_row(const uint8_t *c, unsigned class_, uint8_t *row);
/* The LongInt of row for level, reading the original's data as it lies;
 * false where it lies past DS:43bf. */
bool cok_train_experience(uint8_t row, int level, int32_t *need);

/* 4def:4789: the constitution bonus (DS:3c53 by +0x19) to a hit die of
 * class, one more for a fighter, ranger or knight at 17, two at 18, three
 * at 19-20, four at 21-23 and five at 24-25. */
int8_t cok_train_constitution(const uint8_t *c, unsigned class_);

/* 4def:257c: the constitution bonus to a hit die of class, for the
 * selected character c: -2 at 3, -1 at 4-6, 0 at 7-14, 1 at 15, 2 at 16,
 * and at 17-19 2, or 3-5 for a fighter, ranger or knight. False for any
 * other constitution, where it returns an uninitialized local. */
bool cok_train_hit_die(const uint8_t *c, unsigned class_, int8_t *bonus);

/* 4def:4b3a: the hit points of the levels of the classes in mask (bits of
 * DS:38f2) with a level, divided among count classes. Below its top level
 * (DS:3903) each rolls its dice twice (at level 1 DS:0b6e of DS:0b76's
 * sides, a cleric of a deity above 4 one; above 1 one), keeping the better,
 * and its constitution bonus times the dice; at or above it gets a fixed
 * 1-3, which replaces what came before. The sum and the bonus, each
 * divided by count (the bonus unsigned, as a byte), go to the maximum
 * hit points (+0x62), the sum, at least 1, to +0x11b, and the hit points
 * (+0x197) keep their distance below the maximum. Returns false with
 * game->vm.status set where count is 0, a division by zero. */
bool cok_train_hit_points(cok_adventure *game, uint8_t *c, uint8_t mask, uint8_t count);

/* Train Character (4def:4d9e) from the start menu: pick a character
 * (cok_start_pick), then train the selected one as the hall allows (var
 * 0x7ea8) or free training (DS:7140) does. */
void cok_train(cok_adventure *game);

/* 4def:4d9e for character creation (DS:4b58 set): with no screen, raise
 * each class whose experience allows by one level. Returns false once no
 * class can rise (DS:713f), or when the run must stop (game->vm.abort). */
bool cok_train_silently(cok_adventure *game, cok_character *character);

/* Knight Change Classes (4def:5812): pick a character; a knight of the
 * Crown or the Sword petitions for the next order, granted with Change if
 * it has the level, the experience and the scores (4def:567f). */
void cok_train_knight(cok_adventure *game);

/* 4def:567f: raise c's knightly order (+0x5c) by one, losing a knight
 * level, and its hit points, if its experience does not reach that level
 * in the new order's table, then recompute its levels (66c2:0433). */
bool cok_train_promote(cok_adventure *game, cok_character *character);

#endif
