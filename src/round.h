#ifndef COK_ROUND_H
#define COK_ROUND_H

#include "combat.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The battle's rounds: the battle itself (3995:0172) with its turn order,
 * turns, end of round and end (overlay 3995), the start of each round for
 * each record (432f:0000: initiative, attacks and movement), the lines,
 * sight and ranges of the combat map (overlay 6b30), and the helpers of
 * overlays 6346 and 60f4 the later parts of combat share.
 *
 * Each turn is logged as "turn: NAME (initiative N)". The computer's
 * (3afb:004b) is in ai.h; the player's commands (3995:0573) are not
 * ported: such a turn ends (6346:2964), so a battle runs its rounds until
 * a side can no longer act or 15 rounds pass with no attack. */

typedef struct cok_adventure cok_adventure;

/* The battle (3995:0172), as COMBAT starts it: the mode 5, the spell
 * target routine for combat (DS:6e3a), setup (3cb2:1c58), whose sides
 * decide whether any round is fought, effect event 0x18 for every record
 * (a dragon's fear), then rounds until one decides the battle is done,
 * and the battle's end (3995:004b). eclplay's --combat won, fled and
 * lost (game->combat_stub) decide the outcome instead of the rounds;
 * gods has the player's first turn of each round run the original's Helm
 * cheat (432f:41e2), auto press Alt-Q, putting the party on Auto, melee
 * every turn attack (cok_combat_melee) and pass every turn pass, the
 * computer's too. Unported effect handlers are logged and skipped
 * throughout (cok_effects.in_battle). Returns false, with game->vm.status
 * set, where the battle cannot be carried out; the end of combat
 * (351b:1968) is the caller's. */
bool cok_combat_battle(cok_adventure *game);

/* One round's pieces, for tests and the later parts. */

/* 432f:0000 for c at the start of a round: no spell being cast, may cast
 * unless it stands in a stinking cloud (terrain 0x1e), may use items, has
 * not attacked, attack slot 2, its attacks this round with its weapon
 * (cok_combat_weapon_attacks) and its others (+0x190, from +0x10c after
 * event 0x12), its sweeps (+0xce), its initiative and its movement
 * (cok_combat_movement). One that can act rolls 1d6 plus its dexterity's
 * missile bonus (6346:12f8), at least 1, less 6 when its side is
 * surprised (var 0x7ecb: bit 0 the party's, bit 1 the enemy's), and 0
 * outside 0-20; one that cannot gets 0 and rolls nothing. */
bool cok_combat_round_start(cok_adventure *game, cok_character *c);

/* 3995:02a0: the turn order, every record in the list by its initiative,
 * highest first, the dead too: each is put before the first entry from 1
 * with a lower one, an empty entry counting as -1, or before an equal one
 * for which a d2 rolls 1; the search stops at entry 65, and later entries
 * move down, the 72nd falling off. */
void cok_combat_turn_order(cok_adventure *game);

/* 3995:040b: c's turn. Its hits, turning and guard are cleared and event
 * 7 runs; if its initiative is then above 0 (0x14, a turn given back to
 * the computer, becoming 0x13), it is selected, its actions are shown
 * if it is on the party's side or on the view, the screen shows it
 * (6beb:12ef), its stats are recomputed (6346:0d20), events 0x0f and,
 * unless it is casting, 0x15 run, and if its initiative is still above
 * 0 the computer (+0x18b) or the player acts for it. */
bool cok_combat_turn(cok_adventure *game, cok_character *c);

/* 3995:0b6d: the end of a round. A minute passes (57e4:0549, every
 * record's effects counted down in mode 5), the round is counted
 * (DS:714b), the enemies' health worked out (432f:2df3); each record runs
 * event 0x13, is hurt by a damaging cloud it stands in (60f4:0dc3, not
 * ported: logged), and if dying (status 5) counts a round, dying at 10;
 * "Your Teammate is Dying" if a party member is (6346:31e9); the sides
 * are counted and the map redrawn around the view. *done is set when a
 * side cannot act or the rounds reach the limit; with the enemy's side
 * gone and the party's not, "Continue Battle:" asks, and Yes clears it. */
bool cok_combat_end_round(cok_adventure *game, bool *done);

/* The shared helpers. */

/* 6346:2964: end c's turn: no initiative, spell, guard or movement. */
void cok_combat_end_turn(cok_character *c);

/* 6346:24d7 in combat: damage as cok_character_damage deals it; one left
 * dying keeps the damage past its hit points as its rounds dying, and one
 * that drops loses its initiative and is counted out of its side
 * (DS:6b2c + side, a byte that can wrap), even if it had dropped before.
 * Returns false for a side other than 0 or 1, whose count the original
 * keeps in the bytes after. */
bool cok_combat_damage(cok_combat *combat, cok_character *c, uint8_t damage, uint8_t mode);

/* 6346:31e9: whether a party member on the party's side (+0x18a 0, not
 * past the party's size) is dying; with bandage, the first is bandaged:
 * unconscious, no rounds dying, and "is bandaged" said. */
bool cok_combat_bandage(cok_adventure *game, bool bandage);

/* 6346:0cdb: whether c has effect 0x33, 0x34, 0x35 or 0x1f (DS:200c). */
bool cok_combat_helpless(const cok_character *c);

/* 6346:2666: the side c's enemies are on, as the original works it out:
 * 1 for side 0, else 0. */
uint8_t cok_combat_opposite(const cok_character *c);

/* 432f:014d: c's movement this round in half squares: +0x198, plus var
 * 0x7f72 as a byte on the party's side, 1 for 0 or above 0x60, doubled
 * as a byte, then event 0x12 (haste and slow). */
bool cok_combat_movement(cok_adventure *game, cok_character *c, uint8_t *movement);

/* 432f:0f9e: half attacks as whole ones this round: (n + 1 on odd rounds)
 * / 2, n + 1 a byte. */
uint8_t cok_combat_half_attacks(uint8_t round, uint8_t n);

/* 432f:0e80: c's attacks this round with its weapon (+0x18f): +0x10b,
 * or a missile weapon's rate (its type's byte 5, at least 2) when it has
 * ammunition (cok_combat_ammunition), after event 0x12, halved
 * (cok_combat_half_attacks), at most the ammunition's count when that is
 * more than 1 and not 0; once c has attacked this round (+0x08), never
 * more than it had. */
bool cok_combat_weapon_attacks(cok_adventure *game, cok_character *c);

/* 6346:3043: whether c's readied weapon (slot 0) is a missile weapon,
 * its type's range (byte 12) above 1. Returns false for an item type
 * past those the game reads (ITEMS), where the original reads on. */
bool cok_combat_missile_weapon(const cok_item_types *types, const cok_character *c,
                               bool *missile);

/* 6346:3111: c's ammunition, by its readied weapon's type's flags (byte
 * 14): the weapon itself if thrown (0x10); for 8, its readied arrows (1,
 * slot 11) or quarrels (0x80, slot 12), the later winning. *item is 1 +
 * its index, 0 for none; *has whether it has any, or the flags are
 * exactly 0x0a. Returns false as cok_combat_missile_weapon does. */
bool cok_combat_ammunition(const cok_item_types *types, const cok_character *c, size_t *item,
                           bool *has);

/* 432f:2e82: the most the enemies of c that can act (on the side
 * cok_combat_opposite gives) move this round, in squares: each one's
 * cok_combat_movement halved, which runs its event 0x12. */
bool cok_combat_fastest_enemy(cok_adventure *game, cok_character *c, uint8_t *fastest);

/* 3995:1604: the computer controls c (+0x18b); it forgets a target on
 * its own side. */
void cok_combat_auto(cok_character *c);

/* 60f4:1440: c loses the first effect of each id that lasts only through
 * the battle (DS:0db4), and one berserk (0x4d) and turned (+0xe7 0xb3)
 * goes back to the party's side. */
bool cok_combat_battle_only(cok_adventure *game, cok_character *c);

/* 60f4:133c: c, if it can act, leaves the battle with text said (12ef
 * first): it can no longer act, its status becomes status, its hit points
 * 0 unless it runs (3), it leaves the map, its turn ends and it loses its
 * effects that last only through the battle. */
bool cok_combat_leave(cok_adventure *game, cok_character *c, uint8_t status, const char *text);

/* 432f:41e2, the Helm cheat, for the password given on the command line:
 * "The Gods intervene!", every record against the party (+0x18a 1) is
 * dead, cannot act and leaves the map, and every record's turn ends. */
bool cok_combat_gods(cok_adventure *game);

/* 60f4:2375, after each turn: each of the dead that explode says
 * "explodes.", hurts the others within a cell, and is dead; then the list
 * is emptied. The damage type (DS:6b31) is cleared whether or not any
 * do. */
bool cok_combat_explode(cok_adventure *game);

/* The combat map's lines, sight and ranges (overlay 6b30). */

/* A line between cells (6b30:01a5): set x0, y0, x1, y1 and start it;
 * each step (6b30:024c) moves cur one cell along the longer axis and,
 * when err reaches it, one along the shorter, adding 2 to length for the
 * one and 1 more for the other (a byte), and returns false once cur was
 * the end before the step; direction is that of the last step, from
 * DS:1dd4, 8 for none. */
typedef struct {
    int16_t x0, y0, x1, y1, err, dx, dy, x, y, step_x, step_y;
    uint8_t length, direction;
} cok_combat_line;
void cok_combat_line_start(cok_combat_line *line);
bool cok_combat_line_step(cok_combat_line *line);

/* 6b30:03f1: whether x1, y1 can be seen from x0, y0 within range cells:
 * along the line, until each cell's blocking height (the terrain's) is
 * above the eye height of the start's terrain, unless sight is not
 * blocked (map +6), or the length passes 2 * range + 1. Seen, *range is
 * the length; else x1, y1 the cell where it stopped and *range the length
 * there. Returns false where a cell's terrain is past the table. */
bool cok_combat_sight(const cok_combat *combat, int8_t x0, int8_t y0, int8_t *x1, int8_t *y1,
                      uint16_t *range, bool *seen);

/* 6b30:054a: whether x1, y1 lies in the quarter before x0, y0 facing dir
 * (0-7, 8 or 0xff any): the cell itself and the one ahead always, and
 * neither off the map. Returns false for a dir of 9-0xfe, where the
 * original returns an uninitialized byte. */
bool cok_combat_in_arc(int8_t x0, int8_t y0, int8_t x1, int8_t y1, uint8_t dir, bool *in);

/* 6b30:08d8: list (combat->listed) each combatant on the map that a
 * footprint of size at x, y, facing dir (0xff any), can see within range
 * from a cell of it to a cell of the other in its arc, with the least
 * length found and the direction (dir, or for 0xff the first 0-8 whose
 * arc holds the pair), the one at x, y too; sorted (6b30:0033) nearest
 * first, then by direction, odd ones last. Returns false where the
 * original reads past its tables. */
bool cok_combat_list(cok_combat *combat, int8_t x, int8_t y, uint16_t range, uint8_t dir,
                     uint8_t size);

/* 6346:26e2: the enemies of c (on cok_combat_opposite's side) within
 * range of it, into combat->enemies from 1, nearest first, keeping only
 * those in combat->listed; with effect 0x5b, the one who yelled (DS:71a7)
 * goes first. Returns false where the list cannot be made. */
bool cok_combat_enemies(cok_adventure *game, cok_character *c, uint8_t range, uint8_t *count);

/* 6346:2888: the distance from origin to target in squares (the listed
 * length / 2) with sight not blocked; the list it makes is put back but
 * for its count. A target not listed reads the last entry, or with none
 * the first, stale. */
bool cok_combat_distance(cok_combat *combat, const uint8_t *origin, const uint8_t *target,
                         uint8_t *distance);

/* 6346:34e9: the direction 0-7 from a to b on the map, the first whose
 * wedge holds b. Returns false where none does, and the original loops
 * for ever. */
bool cok_combat_direction(const cok_combat *combat, const cok_character *a,
                          const cok_character *b, uint8_t *direction);

/* The constant tables of the data segment that the port's code holds,
 * to check against the executable. */
extern const cok_ds_table cok_round_tables[];
extern const size_t cok_round_table_count;

#endif
