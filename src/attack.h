#ifndef COK_ATTACK_H
#define COK_ATTACK_H

#include "adventure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Attacks, damage, movement and death in a battle: the attacks of overlay
 * 432f (432f:1a45 and the routines it calls), sweeps, guards, attacks of
 * opportunity, a step on the map, fleeing and the choice of a target, the
 * attack roll and the deaths of overlay 60f4, and the weapon tests of
 * 6346. The computer's turns (3afb, P8) and the player's commands (3995,
 * P9) call these; until they are ported, eclplay's --combat melee has
 * every combatant attack through cok_combat_melee.
 *
 * Functions take their records in the order the original pushes them.
 * An item a routine is given is one of the attacker's, as 1 + its index
 * (0 for none). Each returns false, with game->vm.status set and the run
 * ended, where the original would misbehave: read or write through a
 * record that is not a combatant or has no combat record, past a table,
 * or a freed item; and where an effect's handler fails. Drawing goes
 * through the combat screen (arena.h), sounds are logged as "sound", and
 * the texts of an attack that do not go through 6346:1883 as "attack". */

/* The constant tables of the data segment that the port's code holds,
 * to check against the executable. */
extern const cok_ds_table cok_attack_tables[];
extern const size_t cok_attack_table_count;

/* 60f4:1062: whether attacker hits target, whose armour class (as 60 -
 * AC) the caller gives: attacker's invisibility ends (60f4:1408), a d20
 * (DS:6b3b) of 1 or less misses, 20 counts as 100, then attacker's
 * effects for event 0x0a and target's for event 0x10 change it; it hits
 * if it is not negative and, with attacker's THAC0 (+0x18c, as 60 -
 * THAC0) and var 0x7f71 (the party's side) or 0x7f70 (else) as a signed
 * byte, reaches ac. The original's adjustment by a constant 500 never
 * applies. */
bool cok_combat_hit(cok_adventure *game, cok_character *attacker, cok_character *target,
                    uint8_t ac, bool *hit);

/* 60f4:1408: c loses every effect 0x19 (invisibility). */
bool cok_combat_reveal(cok_adventure *game, cok_character *c);

/* 432f:19b6, before an attack on defender: its hits (combat record +0x0f)
 * count one more, and its turning (+0x12) the turn, 0-4, from its facing
 * to attacker, modulo 8. */
bool cok_combat_book(cok_adventure *game, cok_character *attacker, cok_character *defender);

/* 432f:2af0: whether attacker backstabs target: a thief (+0xff, or a
 * former one, +0x107, that a human may use) with no weapon or one of
 * type 0x43, 3, 4 or 0x11-0x13, against a target that has been hit more
 * than once (+0x0f), a square away (6346:2888), of one cell (+0xcf &
 * 0x7f 1 or less), facing as attacker's direction to it (6346:34e9). */
bool cok_combat_backstab(cok_adventure *game, cok_character *attacker, cok_character *target,
                         bool *backstab);

/* 432f:1ddc: the armour class against attacker from range (6346:2888):
 * each third of a missile weapon's range less 1 (its type's byte 12)
 * past it worsens *ac by 2, then 3; others, never. */
bool cok_combat_range(cok_adventure *game, cok_character *attacker, cok_character *target,
                      uint8_t *ac);

/* 432f:01ba: the damage of attacker's hit with slot (1 or 2) into
 * DS:6b30: its dice (+0x190 + slot, sides +0x192 + slot, 60f4:1261) plus
 * its bonus (+0x194 + slot), a byte (a negative total wraps); events 4
 * (attacker) and 5 (target); a backstab (cok_combat_backstab) multiplies
 * it by 2 to 5 by the thief levels, as a byte; the damage type is 0. */
bool cok_combat_damage_roll(cok_adventure *game, cok_character *attacker, cok_character *target,
                            uint8_t slot);

/* 432f:1579: attacker's attacks on target this round. One that is
 * helpless (6346:0cdb) is slain with one cruel blow (its hit points + 5,
 * a byte), from the slot in progress, counted, the first at 0 down; else
 * against a large target (+0xcf above 0x80 or & 7 above 1) the readied
 * weapon's dice for large ones replace slot 1's; the target's stats are
 * recomputed, its event 0x0b runs, and the armour class is its from
 * behind less 4 for a backstab, or from behind for behind or a target hit
 * more than once, facing as attacker's direction to it, turned more than
 * 4, else its armour class (cok_combat_range then changes it). Each slot
 * from the one in progress down attacks while it has attacks left and the
 * target has not dropped: a hit (or a helpless target) rolls damage, says
 * it (432f:033e) and runs event slot + 1 on attacker while the target can
 * act and some was dealt; the dead that explode after each (60f4:2375).
 * With no hit, "and Misses". *over is set when no attacks are left, or
 * attacker cannot act, and its turn then ends (6346:2964). */
bool cok_combat_strike(cok_adventure *game, cok_character *attacker, cok_character *target,
                       bool behind, bool *over);

/* 432f:1a45, an attack: a target hit fewer than twice turns away from
 * attacker (unless behind), but faces it if shown, which draws it
 * (6beb:0ad8); one hit twice that is shown flips and is drawn back;
 * attacker shows the panel, turns to target in its attacking image, aims
 * at it (combat record +0x0a), and after 100 ms the missile item, if any,
 * flies, then a sling's stone (weapon type 0x1c, 0x1d); with attacks left
 * (+0x18f, +0x190), selected (DS:6096) for the while, it strikes
 * (cok_combat_strike) and uses up item (but for a hoopak's): its count
 * (+0x39) less the attacks made with slot 1 (DS:7195), a byte; at 0, a
 * thrown weapon (6346:30bd) other than a spiritual hammer (+0x3d 0x17)
 * goes into the pool after the missile recovered (DS:60a2), or at its
 * end, unreadied, and becomes that missile; the item is removed and its
 * stats recomputed. *over is set unless attacks are left; then its turn
 * ends; if shown it is drawn back in its ready image. */
bool cok_combat_attack(cok_adventure *game, cok_character *attacker, cok_character *target,
                       bool behind, size_t item, bool *over);

/* 432f:0fce: a sweep by attacker, which has fewer attacks left (+0x18f)
 * than sweeps (combat record +0x05), on a target below 1 hit die (+0xd6
 * 0) a square away: if more of its enemies a square away (6346:26e2)
 * than its attacks are below 1 hit die, it "sweeps" (at most its sweeps),
 * the target first, each attacked once (cok_combat_book, then
 * cok_combat_attack with one attack). *swept says whether it did. Fails
 * where the target is not among those listed, whose place the original
 * takes from a byte it never set. */
bool cok_combat_sweep(cok_adventure *game, cok_character *attacker, cok_character *target,
                      bool *swept);

/* 432f:11d4: whether attacker can attack target: none for no target, yes
 * for itself; else unless target's effects for event 1 make it so it
 * cannot (DS:6b37), then attacker's for event 0, its target set to
 * target meanwhile. */
bool cok_combat_can_attack(cok_adventure *game, cok_character *attacker,
                           const uint8_t *target, bool *can);

/* 432f:3f9f: c keeps its target (combat record +0x0a) unless force, or
 * the target is on its side, cannot act or cannot be attacked; else up to
 * 20 times a d(enemies within range, 6346:26e2), or the one who yelled
 * first for one with effect 0x5b, picks one that can be attacked, or
 * takes it off the list; then again with sight not blocked (map +6, if
 * not force), where flag takes the pick without the test. */
bool cok_combat_pick_target(cok_adventure *game, cok_character *c, uint8_t range, bool flag,
                            bool force, bool *found);

/* 432f:077a: c steps in dir (0-7): its movement left (combat record +0x06)
 * less the cell's cost twice, or three times on a diagonal, as a byte, or
 * none past it; it is redrawn there, its hits and turning cleared, sound
 * 0x0a; enemies beside it that guard attack it (432f:068f); one that
 * cannot act or is helpless after has no movement left. */
bool cok_combat_step(cok_adventure *game, cok_character *c, uint8_t dir);

/* 432f:0986, before c steps in dir: each enemy beside it, not helpless or
 * made to flee, that it leaves, that can attack it and has no missile
 * weapon, or a thrown one, attacks it once from behind: if it has had its
 * turn and been hit, only where c lies in its arc from one of five
 * directions from its facing + 6; else at once. */
bool cok_combat_opportunity(cok_adventure *game, cok_character *c, uint8_t dir);

/* 432f:0dc7: c tries to flee: with no enemy on the map, or moving farther
 * than the fastest enemy (432f:2e82), or as far and a d2 of 1, it "Got
 * Away" (60f4:133c, running); else "Escape is blocked". Its turn ends
 * (6346:2964), whose 1 0dc7 returns either way. */
bool cok_combat_flee(cok_adventure *game, cok_character *c);

/* 60f4:00eb: say text about c (row 10), and unless it is dead, stoned or
 * gone (6-8), it takes status, cannot act, has no hit points, loses its
 * battle's effects (60f4:1440), runs event 0x0d and, if still down and
 * not exploding (10), dies on the screen (6beb:0e08); then a pause, the
 * text cleared and, outside combat, the party list redrawn. */
bool cok_combat_kill(cok_adventure *game, cok_character *c, uint8_t status, const char *text);

/* 60f4:22b7: put c back on the map where it was (6beb:10f3, its bodies
 * taken back); if it fits, it is okay and can act with hp hit points, is
 * shown (6beb:12ef, margin 3) in combat, flashes with text (6346:228c,
 * kind 1), the sides are counted and *placed is set. */
bool cok_combat_revive(cok_adventure *game, cok_character *c, uint8_t hp, const char *text,
                       bool *placed);

/* Effect 0x4c's gating (3f44:272a), below half c's hit points: the
 * waiting (status 9) in the party list (DS:609a) from the first, each
 * placed (its footprint from +0xcf & 7) on the first cell of floor (0x17)
 * free of combatants in the directions 0-7 around the first record on
 * another side (6beb:0493), then the next such record, and lastly c
 * itself, and put back there (60f4:22b7) with its full hit points
 * (+0x62) and "gates in"; the direction moves on after each cell tried,
 * the waiting after each one put. It stops at one not waiting, after c,
 * or when none are left. Fails where the original hangs on a cell off
 * the map, reads past the list, or places one that is not a combatant
 * through entry 0 (over the count). */
bool cok_combat_gate(cok_adventure *game, cok_character *c);

/* 6346:30bd: whether c's readied weapon is a missile weapon
 * (cok_combat_missile_weapon) that is thrown (its type's flags & 0x14 =
 * 0x14) or a hoopak (type 0x43). 6346:3086: whether it is a hoopak. */
bool cok_combat_thrown(const cok_item_types *types, const cok_character *c, bool *thrown);
bool cok_combat_hoopak(const cok_character *c);

/* eclplay's --combat melee, for c's turn until the computer's and the
 * player's are ported: the nearest enemy it can attack (6346:26e2,
 * 432f:11d4) a square away, or with a missile weapon and its ammunition
 * and none a square away within the weapon's range (byte 12 less 1), is
 * attacked as the player's Aim does (432f:3219: the cursor off, the view
 * centred, a sweep or an attack, the ammunition but for a thrown weapon a
 * square away); again while the attack leaves its turn going; with none,
 * the turn ends. */
bool cok_combat_melee(cok_adventure *game, cok_character *c);

#endif
