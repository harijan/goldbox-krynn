#ifndef COK_CLOUD_H
#define COK_CLOUD_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Clouds and pools on the combat map: Stinking Cloud's clouds (5b04:29de)
 * and the acid pools of those that dissolve as they die (effect 0x46,
 * 3f44:2037), what they do to one who stands in them (60f4:0dc3), their
 * ends (effects 0x28 and 0x50, 3f44:0ff7 and 2cad), and Dispel Magic's
 * part for the clouds (5b04:3352).
 *
 * A cloud or pool is a node of the lists DS:7111 and DS:7119
 * (cok_combat.clouds, pools). A cloud covers four cells, a pool one: each
 * covered cell's terrain becomes 0x1e (green cloud) or 0x1d (puddle) and
 * the node keeps what was under it; a body's (0x1f) cell keeps the
 * terrain the body covered. The terrain is all they are to the rest of
 * combat: a step's cost leaves them out and notes them (6beb:0c9d), one
 * in a green cloud may not cast at the start of a round (432f:0000), the
 * computer steps into one only if it saves or is immune, and into a
 * puddle only from level 7 (3afb:0743), a body under a cloud is not seen
 * by the computer's Cure Light Wounds (432f:1eed), and one who drops in a
 * cloud leaves its body under it (6beb:0e08). Sight is not blocked: both
 * are as high as floor.
 *
 * Each returns false, with game->vm.status set and the run ended, where it
 * cannot be carried out or the original would misbehave (see each). */

/* 5b04:29de, Stinking Cloud, for the selected character (the caster), at
 * the spell's cell (DS:710f, 7110): the spell takes no mirror image
 * (DS:6b39); a cloud is added to the list, its index the caster's clouds
 * before the list's last node (the last is never counted), and the caster
 * gets effect 0x28 for the caster level (6346:29fe) in minutes, valued
 * the index * 16 + the level, run when removed. Each of the four cells
 * that is on the map and no wall is covered: the terrain under it is
 * kept, from the other cloud's for a cell already 0x1e (its last that
 * covers the cell and kept something else), or from a body's (any body
 * at the cell, also one taken back) for 0x1f; and it becomes 0x1e. Then
 * "Creates a noxious cloud" (row 10, no wait), the screen centred on the
 * cell (forced), a pause, the text cleared, and each combatant that stood
 * on a cell, once each, is in the cloud (cok_cloud_square with step). */
bool cok_cloud_stinking(cok_adventure *game);

/* 60f4:0dc3 for c, who can act: with step (after a step or a cloud laid),
 * standing in a green cloud (6beb:0c9d), not undead (+0xda) and without
 * effect 0x1f or 0x63, it throws (type 0); then, the selected character
 * being the cloud's caster meanwhile (cok_cloud_caster), saved it "starts
 * to cough", effect 0x1e, else it "chokes and gags from nausea", effect
 * 0x1f, either for 1d4 + 1 minutes, valued 0xff (60f4:20f7), and if it
 * then has the effect its handler runs (60f4:01a8), 0x1e's with no effect
 * (the pointer the search for 0x63 left). Standing in a puddle, with step
 * or not (the end of a round), it takes 1d6 (60f4:1261) of the damage
 * type DS:6b31 holds, no save (60f4:1db7). */
bool cok_cloud_square(cok_adventure *game, cok_character *c, bool step);

/* 60f4:0c95: the caster of the first cloud with a covered cell at x, y,
 * where cell k's covered flag is matched with the direction of cell k + 1
 * (DS:2092 + k: east, south-east, south, and the cell itself for k 4); for
 * none, the first record of the list (DS:609a), NULL with none. */
uint8_t *cok_cloud_caster(const cok_adventure *game, int8_t x, int8_t y);

/* 3f44:0ff7, effect 0x28 of c removed: the first cloud c laid whose
 * index is the effect's value / 16, if any (none outside combat, where the
 * list is empty), goes: "The air clears a little..." (6346:1827), each
 * covered cell gets back its terrain, or the body's tile where a body
 * (not taken back) lies, the node is freed and every other cloud's cells
 * and every pool's cell are covered again. The screen is not redrawn. */
bool cok_cloud_clear(cok_adventure *game, cok_character *c, const cok_effect *effect);

/* 3f44:2037, effect 0x46 for c on event 0x0d: unless it has hit points
 * and can act, c "dissolves into acid!": a pool is added to the list at
 * its cell, its index c's pools before the list's last node, and c gets
 * effect 0x50 for 1d6 minutes, valued the index, run when removed; the
 * cell's terrain is kept, from a cloud's as Stinking Cloud does for 0x1e;
 * for a body's tile 0x1f the original compares the bodies with the
 * spell's cell (DS:710f, 7110) moved a step in a direction from a byte of
 * the stack it never set: where the bytes 0-0xff give different terrains
 * the pool's is unknown, and the run stops if it is put back. The cell
 * becomes 0x1d, the screen is centred on c (forced) and the text said
 * (row 10, no wait). Outside combat, with no map, the run stops. */
bool cok_cloud_acid(cok_adventure *game, cok_character *c);

/* 3f44:2cad, effect 0x50 of c removed: the first pool c made whose index
 * is the effect's value, if any, goes: "The acid drains away.." (6346:1827),
 * its cell gets back its terrain, or the body's tile where a body (not
 * taken back) lies, the node is unlinked, every cloud's cells covered
 * again, the node freed, the clouds' cells covered once more and every
 * pool's cell covered again. */
bool cok_cloud_drain(cok_adventure *game, cok_character *c, const cok_effect *effect);

/* 5b04:3352, Dispel Magic's part for the clouds, the caster level level
 * (the selected character's): for the spell's cell and the eight around
 * it, a cell that is a green cloud is tried against each cloud in turn
 * that has a cell there and never resisted (+0x1d): a d100 up to 50, plus
 * 5 a level level is above the cloud's, or less 2 a level below, as a byte
 * (DS:4839, 483a), the cloud's level the caster's magic-user level (+0xfe)
 * plus its former one (+0x106) times whether the selected character (not
 * the cloud's caster) may use its former class (66c2:0efb), a byte: then
 * the caster's first effect 0x28 valued the cloud's index * 16 + a level
 * is removed (cok_cloud_clear, which may free another cloud of the same
 * index), else the cloud resists for good. After each such cell the
 * screen is centred on the spell's cell (margin 3). The original then
 * reads the next cloud from the one it freed, where FreeMem (1a46:03fc)
 * wrote the block's size, 0x20 rounded, as the pointer 0002:0000: the
 * interrupt vectors, and through them the BIOS's bytes, which differ from
 * machine to machine: the run stops. Outside combat the map pointer is NULL
 * and the original reads the cells from the interrupt table: the port
 * takes none to be 0x1e (nothing then happens). */
bool cok_cloud_dispel(cok_adventure *game, uint8_t level);

/* Free the clouds and the pools (3995:004b, and setup's clearing). */
void cok_cloud_free(cok_combat *combat);

/* The constant tables of the data segment that the port's code holds,
 * to check against the executable. */
extern const cok_ds_table cok_cloud_tables[];
extern const size_t cok_cloud_table_count;

#endif
