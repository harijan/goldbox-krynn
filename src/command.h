#ifndef COK_COMMAND_H
#define COK_COMMAND_H

#include "adventure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The player's commands in a battle (overlay 3995 from 3995:0573, with the
 * aiming of overlay 432f, Yell of 60f4 and Guard of 6346): the menu "Move
 * View Aim Use Cast Turn Yell Done" a combatant the player controls gets
 * at its turn, and what each leads to. Keys come from the adventure's
 * keyboard through the menus on row 24 (67b5:03e2); the mouse is not
 * ported.
 *
 * The cast in combat (5b04:1415, with its targets 432f:2337) is
 * cok_cast_spell's (cast.h), from Cast, a spell begun earlier or an item.
 *
 * Functions take their records in the order the original pushes them.
 * Each returns false, with game->vm.status set and the run ended, where
 * the original would misbehave or where what it calls cannot be carried
 * out; when the keys run out, the run ends (game->vm.abort,
 * game->input_ended) and they return true at once. */

/* What aiming picked (432f:3b80's 7 bytes): the record aimed at, or NULL,
 * and the cell. */
typedef struct {
    uint8_t *target;
    int8_t x, y;
} cok_combat_aimed;

/* The cell of record (6beb:0bcb, 0bf3). For none, 6beb:0c43 finds NULL
 * in the entry after the last combatant: the cell placement last tried
 * there or an earlier battle left; where an earlier battle's record is
 * still in that entry it reads entry 0's cell, other data, and the run
 * stops. */
bool cok_combat_cell_of(cok_adventure *game, const uint8_t *record, int8_t *x, int8_t *y);

/* 3995:0573, c's turn when the player has it. One that cannot act ends its
 * turn; one casting a spell begun at an earlier turn clears it, casts it
 * (5b04:1415) and ends its turn. Otherwise, until the turn is over: the
 * menu's first item is selected (DS:6e0f) and the menu
 * (cok_combat_command_text) read until it gives one of its commands or the
 * hidden keys: Move (cok_combat_move), View (546c:0d74) or Use (the Items
 * menu, 546c:17f9, with its second item selected), each followed by c's
 * attacks recomputed (432f:0e80) and, unless an item used ended the turn,
 * the screen redrawn (6346:300f), Aim (cok_combat_aim with c's weapon's
 * range), Cast (cok_combat_cast), Turn (432f:12b7) and Yell
 * (cok_combat_yell), each ending the turn, and Done (cok_combat_done);
 * Space gives every party member (+0xe7 below 0x80) back to the player
 * (+0x18b); Alt-M turns Magic On or Off (DS:7198); Alt-Q or Alt-A give the
 * turn back (DS:72ce less 1, initiative 0x14), put every record under the
 * computer (3995:1604), clear row 24 and wait 200 ms, so that the battle
 * runs the turn again, the computer's; Alt-X runs the Helm cheat
 * (432f:41e2) in a game started with Helm, showing c after, else "That
 * doesn't work"; Q and Alt-R do nothing. While the turn goes on c is shown
 * (6beb:12ef) and its panel redrawn. pressed, if not 0, is a scan code the
 * first key read gives, as if the player pressed it: eclplay's --combat
 * gods presses Alt-X (0x2d), auto Alt-Q (0x10). */
bool cok_combat_commands(cok_adventure *game, cok_character *c, uint8_t pressed);

/* 3995:08d5's menu for c, cut to 40 characters: "Move " while it has
 * movement left, "View Aim ", "Use " while it has items (+0x142), "Cast "
 * with a spell memorized (+0x1e-+0x57) when it may cast this round
 * (combat record +0x01) and the area allows magic (var 0x4be5), "Turn "
 * for a cleric (+0xf9 above 0, or a former one, +0x101, that a human may
 * use) that has tried fewer times this battle (+0x11) than undead were
 * loaded (DS:8859), "Yell " for a kender (+0x5a 5) while no one has yelled
 * this battle (DS:71a7), and "Done", then "*" while the debug flag
 * (DS:4b51) is set. */
void cok_combat_command_text(cok_adventure *game, cok_character *c, char out[41]);

/* 3995:0d9c, Move: until Enter, D or Escape or the turn is over, "Move/
 * Attack, Move Left = N" with " Done" on row 24 and the keypad's eight
 * directions: c turns that way (6beb:0ad8); another combatant there is
 * attacked (cok_combat_charge); off the map, "Flee:" Yes tries to get
 * away (432f:0dc7), which ends the turn; else a step costing (the costliest
 * terrain under its footprint, twice or three times on a diagonal, as a
 * byte) more than its movement left says "can't go there"; otherwise the
 * enemies it leaves attack it (432f:0986), and unless it dropped it steps
 * (432f:077a) while it has movement, then the cloud under it acts
 * (60f4:0dc3); one that dropped or is helpless ends its turn. Escape first
 * undoes the moves (3995:0cad): the movement and facing come back and it
 * is put back where it started (6beb:10f3), which ends the turn if it
 * cannot be. Half a square left is lost. */
bool cok_combat_move(cok_adventure *game, cok_character *c, bool *done);

/* 3995:1117: c moves into target: with a missile weapon not thrown, "Not
 * with that weapon"; else unless it would attack an ally the player does
 * not want to (cok_combat_ally), it aims at target (+0x0a) and sweeps
 * (432f:0fce), which sets *done, or books and attacks it (432f:19b6,
 * 432f:1a45). */
bool cok_combat_charge(cok_adventure *game, cok_character *c, cok_character *target,
                       bool *done);

/* 432f:2f12: whether c may attack target: yes where c is on the side
 * opposite target's (cok_combat_opposite) or the computer controls c; else
 * "Attack Ally: " Yes, which also sets
 * var 0x7f33 and turns every NPC that is okay (+0xe7 from 0x80, but 0xb3)
 * against the party, forgetting its target, and counts the sides. */
bool cok_combat_ally(cok_adventure *game, cok_character *c, cok_character *target, bool *go);

/* 432f:3b80, aiming for c: range (0xff: its weapon's, type byte 12 less
 * 1; 1 for none, 0 or 0xff), attack (1: the pick is attacked), empty (an
 * empty cell may be picked) and creatures (a creature may be picked). The
 * combatants around c it can attack (6b30:08d8 within 0x7f, 432f:11d4),
 * nearest first, c itself among them, are its targets, the first the
 * nearest; the cursor flies between them (6346:1ba6). Each shows the
 * target (432f:3026: "Range = N" on row 23 when attacking, its panel) with
 * "Next Prev Manual Target Center Exit", Target only when it is within
 * range, the cell is valid and, attacking, it is not c and c has no
 * missile weapon or has ammunition and no enemy beside it or a thrown
 * one. Next and Prev step through the targets, Manual moves a cursor
 * (432f:33ce), Center centres the map there, Target (or a cursor's pick)
 * sets *res and *done and, attacking, attacks it (432f:3219), Exit and
 * Escape leave. Fails where the original reads a target it never listed
 * or a record through NULL. */
bool cok_combat_aim(cok_adventure *game, cok_character *c, uint8_t range, uint8_t attack,
                    uint8_t empty, uint8_t creatures, bool *done, cok_combat_aimed *res);

/* 3995:121c, Done: "Guard " (unless c has a missile weapon not thrown),
 * "Delay Quit ", "Bandage " while a party member is dying (6346:31e9) and
 * "Auto Speed Exit", until Exit or Escape or the turn is over: Guard
 * (6346:29b3), Delay (initiative 1, c moved to the end of the round's
 * order, at most to entry 72, and the turn ends), Quit (it ends), Bandage
 * (the first party member dying is bandaged, and it ends), Speed
 * (3995:14cf), Auto (c goes to the computer, 3995:1604, row 24 is cleared,
 * 200 ms pass and the computer plays the rest of the turn, 3afb:004b).
 * Special keys count by their scan codes, as letters. */
bool cok_combat_done(cok_adventure *game, cok_character *c, bool *done);

/* 3995:14cf: "GameSpeed (N) :" with "Slower " below 9 and "Faster "
 * above 0, and Exit, until Exit or Escape (DS:4b38, game->speed). */
void cok_combat_speed(cok_adventure *game);

/* 432f:28bd, Cast, for the selected character (DS:6096): spell, or if 0
 * one picked from its spells in memory (546c:34ec), refused with "Camp
 * Only Spell" where the spell table's byte 11 is 0; unless redrawn (the
 * computer's flag, which the cast then gets) the combat screen is redrawn
 * and the caster shown with its panel. A spell
 * whose casting time (byte 12, in thirds) is 0 is cast (5b04:1415) and the
 * turn ends; a longer one "Begins Casting" (row 10, with a pause): the
 * spell is kept (combat record +0x00) for the caster's next turn, its
 * initiative falls by the time, at least to 1, and it moves down the
 * round's order past those with more initiative and, a d2 of 1 each, those
 * with as much, without a bound; *done is set. Fails where the original
 * reads the byte after the order (DS:72ce) as a record. */
bool cok_combat_cast(cok_adventure *game, bool *done, bool redrawn, uint8_t spell);

/* 60f4:0023, a kender's Yell: c is the one who yelled this battle
 * (DS:71a7); every record that is okay, of intelligence (+0x13) above 3,
 * on another side and not helpless that fails a saving throw of type 4
 * (60f4:113a) gets effect 0x5b, "wants to kill NAME" (60f4:20f7). */
bool cok_combat_yell(cok_adventure *game, cok_character *c);

/* 6346:29b3: c's turn ends and, unless made to flee (combat record
 * +0x10), it guards, "Guarding" on row 24. */
void cok_combat_guard(cok_adventure *game, cok_character *c);

#endif
