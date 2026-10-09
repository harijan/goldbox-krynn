#ifndef COK_AI_H
#define COK_AI_H

#include "adventure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The computer's turns in a battle (overlay 3afb): monsters, NPCs and
 * party members on Auto (+0x18b). A turn (3afb:004b) polls the keyboard,
 * picks its way of moving, tests its morale, then uses a wand, casts,
 * turns undead or, failing those, readies its best weapon and moves on its
 * target and attacks, through the attacks, steps and targets of attack.h.
 *
 * Casting in combat (432f:28bd, 5b04:1415) and using an item there
 * (546c:24d7) are not ported: where the computer would cast or use an item
 * it has chosen (by the original's own tests, with their dice and saving
 * throws), the port logs it as unported and its turn ends, as the cast or
 * use would end it.
 *
 * Functions take their records in the order the original pushes them.
 * Each returns false, with game->vm.status set and the run ended, where the
 * original would misbehave or where what it calls cannot be carried out. */

/* The constant tables of the data segment that the port's code holds,
 * to check against the executable. */
extern const cok_ds_table cok_ai_tables[];
extern const size_t cok_ai_table_count;

/* 3afb:004b, c's turn when the computer has it: the keys (cok_ai_keys),
 * row 24 and the panel's text cleared; one that cannot act ends its turn;
 * its way of moving (cok_ai_pattern); unless a key took the turn back or
 * it cannot act, its morale (cok_ai_morale); one fleeing of its own
 * accord "flees in panic". Then, unless the turn is over, the first that
 * applies: an item's spell (cok_ai_use_item), a spell begun earlier,
 * turning undead (cok_ai_turn_undead) or a spell (cok_ai_cast); else it
 * readies its weapon (cok_ai_choose_weapon), polls the keys again and,
 * while the turn goes on, picks a target (432f:3f9f with flag) and, with
 * one, its initiative above 0 and able to act, moves and attacks
 * (cok_ai_fight), else guards or ends its turn (cok_ai_guard). Fails where
 * the original would loop for ever. */
bool cok_combat_computer(cok_adventure *game, cok_character *c);

/* 3afb:1200: a key waiting is read (with a second read after a 0, as scan
 * codes come): 0x32 (Alt-M, or '2') toggles Magic On (DS:7198) with "Magic
 * On" or "Magic Off" on row 24; Space gives every party member (+0xe7
 * below 0x80) whose status is not 1 back to the player (+0x18b), and if c
 * is one of them its initiative becomes 0x14 and *back is set; 0x2d
 * (Alt-X, or '-') runs the Helm cheat when the game was started with it
 * (432f:41e2). The keyboard's flush after (1614:045c) is not ported. */
bool cok_ai_keys(cok_adventure *game, cok_character *c, bool *back);

/* 004b's way of moving (combat record +0x15, a row of DS:0396): a row 1-4
 * stays unless a d4 rolls 1; then, or for any other row, a d8 of 8 gives
 * 4 + d2, else a d4. */
bool cok_ai_pattern(cok_adventure *game, cok_character *c);

/* 3afb:1316, morale: c is not fleeing; one made to flee (combat record
 * +0x10) flees, "is forced to flee". An NPC or monster (+0xe7 from 0x80)
 * holds while its morale ((+0xe7 & 0x7f) * 2 as a byte, 0 above 0x66, in
 * DS:6b3e, changed by its effects for event 0x11) is not 0 and reaches 100
 * less its hit points as a percentage of its most (a signed division; none
 * most divides by zero, where the port stops); else, on the side against
 * the party, while the enemies' health (DS:7197, through event 0x11 again)
 * is not 0 and reaches 100 less var 0x7ec6 (as words). Failing both: if
 * the fastest of its enemies (432f:2e82) moves farther than it (432f:014d,
 * halved), one with an intelligence (+0x13) above 5 "Surrenders"
 * (60f4:133c, status 4) and its turn ends, setting *over; else it flees,
 * losing effects 0x4a and 0x4b. */
bool cok_ai_morale(cok_adventure *game, cok_character *c, bool *over);

/* 3afb:03b9: whether spell suits c at level (1-7): its priority (spell
 * table byte 13) reaches level and either it is not aimed at enemies (byte
 * 14) or some enemy is within its range (5b04:0ecb: byte 2 plus byte 3
 * times the caster level, 6346:29fe, or 6 while an item is used; 0 is 1 if
 * byte 6 is set, and 0xff 1) and, for an area (byte 15 the radius), none
 * of those enemies stands where the area would take in one of the
 * selected character's own side that, unless the spell's save (byte 8)
 * negates it, fails a saving throw of type byte 9 (60f4:113a, -2 on the
 * party's side, +8 on the other): each such one throws, failures or not.
 * Spells marked to be learned (+0x80) read the table past its end, as the
 * original does; the port stops where it does not hold those bytes. Cure
 * Light Wounds (3), whose priority is 0, never reaches its own test
 * (432f:1eed). */
bool cok_ai_spell_fits(cok_adventure *game, cok_character *c, uint8_t spell, uint8_t level,
                       bool *fits);

/* 3afb:04b2: a d7, then, if c may use items this round (combat record
 * +0x02), some enemy can act and the area allows magic (var 0x4be5), for
 * passes 1 to the d7, at levels 7, 6 and so on, the first readied item
 * that is no scroll, casts a spell (+0x3e below 0x80) and has one (+0x3d,
 * less 0x17 above 0x38 for the test) that suits c (cok_ai_spell_fits).
 * The use itself (546c:24d7) is not ported: logged, *used set. */
bool cok_ai_use_item(cok_adventure *game, cok_character *c, bool *used);

/* 3afb:0613: the spells c has memorized (+0x1f-+0x57, those marked to be
 * learned too) if it may cast this round (combat record +0x01), and a d7;
 * if any, c is an NPC or monster or Magic is On, and some enemy can act:
 * for passes 1 to the d7, at levels 7, 6 and so on, three draws of a
 * d(spells) each, the first that suits c (cok_ai_spell_fits). The cast
 * (432f:28bd) is not ported: logged, the turn ends and *cast is set. */
bool cok_ai_cast(cok_adventure *game, cok_character *c, bool *cast);

/* 3afb:024b: a cleric (+0xf9 above 0, or +0x101 above +0xd7) that has
 * turned fewer times this battle (combat record +0x11) than there are
 * undead (DS:8859) turns undead (432f:12b7, *turned) if an enemy undead
 * (432f:14a2) is of a kind no higher than limit, a byte of 024b's frame
 * it never sets, which the calls before it leave (see ai.c). */
bool cok_ai_turn_undead(cok_adventure *game, cok_character *c, uint8_t limit, bool *turned);

/* 3afb:14ca: the worth to c of weapon item (1 + its index), a byte: its
 * type's dice (byte 9 times byte 10), plus 8 times its bonus (+0x32) if
 * positive and twice the bonus for small targets (byte 11) if positive;
 * holy water (type 0x36) 8 against a target that is undead (+0xda); a
 * missile weapon (byte 14 & 8) twice its rate (byte 5) less 2; 3 more for
 * a weapon of one hand (byte 1); 0 if its hands and those c's other items
 * hold (+0x17b) pass 3, for an item of an alignment (+0x3e 0x84, +0x3d's
 * low nibble) other than c's (+0x10a), for +0x3d 0x53, or for a cursed one
 * (+0x36). */
bool cok_ai_weapon_score(cok_adventure *game, cok_character *c, size_t item, uint8_t *score);

/* 3afb:1608: c readies its best weapon and shield (see ai.c), with Ready's
 * toggle (546c:1ea7) for the selected character, which must be c, its
 * stats and attacks recomputed (6346:0d20, 432f:0e80), and its panel
 * redrawn if anything was readied. */
bool cok_ai_choose_weapon(cok_adventure *game, cok_character *c);

/* 3afb:0d49: c moves toward its target and attacks (see ai.c), after its
 * effects for event 0x0e (breath); *over is set when its turn is over. */
bool cok_ai_fight(cok_adventure *game, cok_character *c, bool *over);

/* 3afb:095a: one step of c: "Move/Attack, Move Left = N" on row 24, the
 * keys, then it holds (cok_ai_guard) with no move or initiative left, or
 * if it is an NPC on the party's side when the enemies' health (DS:7197)
 * passes a d100 (which every NPC and monster rolls) plus the morale, or if
 * it is not fleeing, wears no armour and +0x5b is 5; else it tries five
 * directions around its target's, or for one fleeing a d2's row and the
 * party's facing's way off the map (see ai.c), stepping where it can. */
bool cok_ai_step(cok_adventure *game, cok_character *c);

/* 3afb:0743: whether c can step in dir turned by try (1-5) of its way of
 * moving (DS:0396): not off the map (*offmap), onto a cell it can walk on
 * and no one holds, costing (twice, three times on a diagonal, a byte)
 * less than its movement left; a green cloud costs all unless c has effect
 * 0x20, 0x1e, 0x63 or 0x3f, is made to flee or makes a saving throw of
 * type 0, and a puddle for one below level 7 unless 0x63 or made to flee. */
bool cok_ai_can_step(cok_adventure *game, cok_character *c, uint8_t try_, uint8_t dir,
                     bool *offmap, bool *ok);

/* 3afb:118e: the panel's text cleared; one not helpless, without a missile
 * weapon and with initiative left guards (6346:29b3: its turn ends and,
 * unless made to flee, it guards, "Guarding" on row 24); else its turn
 * ends. */
bool cok_ai_guard(cok_adventure *game, cok_character *c);

#endif
