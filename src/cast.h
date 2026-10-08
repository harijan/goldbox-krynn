#ifndef COK_CAST_H
#define COK_CAST_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Casting spells outside combat, from overlay 5b04: the cast itself
 * (5b04:1415), its targets (5b04:127e) and the handler of each spell, from
 * the table at DS:6e3a that 5b04:58ed fills at startup. Cast from a spell
 * list is cok_magic_cast (magic.h); items use them through View (items.h).
 *
 * Spells whose table byte 7 is 0 are refused outside combat, as the
 * original refuses them. The handlers that need what is not ported fail,
 * with game->vm.status COK_ECL_EFFECT_FAILED or COK_ECL_UNDEFINED, and end
 * the run. */

/* Cast spell 1-0x6b for the selected character (5b04:1415). Outside combat
 * a spell that can only be cast in combat (byte 7 of DS:31b3 + 16 * spell
 * is 0) asks "Lose it? " and forgets it on Yes, or, while an item is used
 * (game->effects.rolls.item), asks "Use it? " and sets *done on Yes. A
 * caster with effect 0x4a miscasts half the time. Otherwise, with
 * announce and no item, it says the character "casts" it, picks the
 * targets, and if there are any sets *done, ends the caster's
 * invisibility, forgets the memorized spell unless an item is used, and
 * runs the spell's handler. *done is left as it was otherwise. frame is
 * the low byte of 5b04:1415's frame pointer, which its caller's depth on
 * the stack fixes: COK_CAST_COMMANDS or COK_CAST_CAMP (see Strength). */
void cok_cast_spell(cok_adventure *game, uint8_t spell, bool announce, uint8_t frame, bool *done);

/* The low byte of 5b04:1415's frame pointer when Cast (4888:0a0d) calls
 * it: from the stack at 0x4000 (the MZ header's SP), main's frame and the
 * calls 2fd3:3c28, 475c:09ec, 4888:0a0d make BP 0x3d74; 2fd3:3c28,
 * 2fd3:3403, 4888:2c31, 4888:1c32 and 4888:0a0d make 0x3d9a. */
enum { COK_CAST_COMMANDS = 0x74, COK_CAST_CAMP = 0x9a };

/* How many minutes spell lasts (5b04:0f78): fixed or rolled for a few
 * spells and item powers, else the caster level (6346:29fe) times byte 5
 * of the spell table plus byte 4. Returns false, ending the run, where the
 * caster level cannot be had. */
bool cok_cast_duration(cok_adventure *game, uint8_t spell, uint16_t *minutes);

/* 60f4:1db7: amount of damage to c, of the type in DS:6b31, after its
 * event 6 (magic resistance first), then halved or none for a save of
 * kind 2 or 1 made, or its event 0x14 if not saved. If any is left and c
 * can act, it "takes N points of damage" and "from Fire", "from Cold",
 * "from Electricity", "from Acid" or "from Magic" by the type; then if it
 * drops it "Goes Down", ", and is Dying", or "is killed". In combat
 * (mode 5) the damage flashes on c (6346:228c, kind 0) and goes through
 * cok_combat_damage, one hurt may no longer cast, and one casting "lost a
 * spell" and its turn; one that drops loses its battle's effects
 * (60f4:1440), runs event 0x0d and, unless exploding, dies on the screen
 * (6beb:0e08). Returns false, ending the run, where it cannot be carried
 * out. */
bool cok_cast_damage(cok_adventure *game, cok_character *c, uint8_t amount, uint8_t save_kind,
                     bool saved);

/* Remove Curse's handler (5b04:35f5) on target, as the temple runs it with
 * the first target (DS:6feb) set: Bestow Curse (0x24) goes, or else the
 * first cursed item is unreadied, still cursed. Returns false, ending the
 * run, where it cannot be carried out. */
bool cok_cast_remove_curse(cok_adventure *game, cok_character *target);

#endif
