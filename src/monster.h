#ifndef COK_MONSTER_H
#define COK_MONSTER_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Monsters and encounters, from the ECL handlers of overlay 2fd3 and their
 * helpers in overlays 3775, 4b6d, 6961 and 6d21. A monster is a 409-byte
 * character record, from MON<file>CHA.DAX, with its items (MON<file>ITM)
 * and effects (MON<file>SPC); LOAD MONSTER appends it to the end of the
 * party list, where it stays until the end of combat removes it. Records
 * a monster uses beyond those party.h lists:
 *
 *   +0x0cf  bits 0-2 the size of its combat icon, bit 7 a large creature
 *   +0x0da  its kind of undead, for turning; 0 for none
 *   +0x0e7  0x80 and up for a monster; its low bits are its morale
 *   +0x0eb  coins, seven words, for the treasure
 *   +0x10b  attacks a round, doubled, for its two attacks; +0x10d dice,
 *           +0x10f sides and +0x111 bonuses for each
 *   +0x11b  hit points at full; +0x130 (a word) and +0x132 its experience,
 *           +0x130 + +0x132 * +0x11b
 *   +0x187  magic resistance
 *   +0x18a  1 against the party
 *   +0x18b  1 while the computer controls it
 *
 * The opcodes here are run from cok_adventure's opcode hook. */

/* Carry out vm.opcode if it is one of LOAD MONSTER, CLEARMONSTERS, SETUP
 * MONSTER, APPROACH, SPRITE OFF, ENCOUNTER MENU, PARLAY, CHECKPARTY,
 * PARTYSTRENGTH, PARTY SURPRISE, SURPRISE, ROB or COMBAT, with its operands
 * decoded. Returns false for any other opcode. */
bool cok_monster_opcode(cok_adventure *game);

/* Reset the encounter's state as a block starting does (3775:01e8): the
 * next icon slot (DS:72eb) is 8 and no sprite is loaded (DS:8830). */
void cok_monster_reset(cok_adventure *game);

/* Empty the treasure pool. */
void cok_pool_free(cok_pool *pool);

/* The open squares ahead of x, y facing direction, 0-2 (3775:04a9): a step
 * at a time while the side faced has no wall at all, doors included; 2,
 * also stored in var 0x7ec1, where the area has no 3D view (0x4be6). */
uint8_t cok_monster_open_squares(cok_adventure *game, int x, int y, uint8_t direction);

/* The party's fastest and slowest movement (3775:1f8b): +0x198 of every
 * record in the list, doubled with haste (effect 0x27), else halved when
 * slowed (0x2a); both start from the first record's own +0x198. Returns
 * false for an empty list, where the original reads 0000:0198. */
bool cok_monster_movement(const cok_party *party, uint8_t *fastest, uint8_t *slowest);

/* Turbo Pascal's 6-byte Real, as its runtime library (1a46) works it:
 * byte 0 the exponent biased by 0x81, 0 for zero; bytes 1-5 the mantissa
 * below an implied leading 1, least significant first, the top bit of
 * byte 5 the sign. ROB works its money out in these, which round
 * differently from a C double: Trunc(530 * 0.9) is 476. */
typedef struct {
    uint8_t b[6];
} cok_real;

/* A LongInt as a Real (1a46:1153), exactly. */
cok_real cok_real_from_long(int32_t value);
/* a * b (1a46:113f) and a / b (1a46:1145), rounded on the 48th bit, half
 * up. When either mantissa's low 24 bits are 0, as for a word converted,
 * the multiply takes a short way that drops part of the low product, as
 * the original does. They return false where the original stops with a
 * runtime error: 205 for an overflow, 200 for division by zero. */
bool cok_real_multiply(cok_real a, cok_real b, cok_real *out);
bool cok_real_divide(cok_real a, cok_real b, cok_real *out);
/* Trunc (1a46:1157): toward zero; false where it does not fit a LongInt,
 * runtime error 207. */
bool cok_real_trunc(cok_real value, int32_t *out);

#endif
