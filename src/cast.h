#ifndef COK_CAST_H
#define COK_CAST_H

#include "adventure.h"
#include "arena.h"

#include <stdbool.h>
#include <stdint.h>

/* Casting spells, from overlay 5b04: the cast itself (5b04:1415), its
 * targets outside combat (5b04:127e) and in a battle (432f:2337, which the
 * battle puts in DS:6e3a), and the handler of each spell, from the table
 * at DS:6e3a that 5b04:58ed fills at startup, with the cones and bolts of
 * the spells cast only in combat. Cast from a spell list is cok_magic_cast
 * (magic.h); items use them through View (items.h); in a battle Cast is
 * cok_combat_cast (command.h).
 *
 * Spells whose table byte 7 is 0 are refused outside combat, as the
 * original refuses them. Each cast in a battle is logged as "cast: NAME:
 * SPELL". */

/* The constant tables of the data segment that the port's code holds,
 * to check against the executable. */
extern const cok_ds_table cok_cast_tables[];
extern const size_t cok_cast_table_count;

/* Cast spell 1-0x6b for the selected character (5b04:1415), the
 * computer picking its targets in a battle if ai. Outside combat a spell
 * that can only be cast in combat (byte 7 of DS:31b3 + 16 * spell is 0)
 * asks "Lose it? " and forgets it on Yes, or, while an item is used
 * (game->effects.rolls.item), asks "Use it? " and sets *done on Yes. A
 * caster with effect 0x4a miscasts half the time. Otherwise, with
 * announce and no item, it says the character "casts" it (in combat
 * "Casts a Spell", with the spell's name on row 23), picks the targets
 * (5b04:127e, or in a battle 432f:2337), and if there are any sets *done,
 * in combat flies the spell's missile from the caster to the spell's cell,
 * ends the caster's invisibility, forgets the memorized spell unless an
 * item is used, and runs the spell's handler. In combat with no target the
 * player is asked "Abort Spell? ", No picking again; Yes, and the
 * computer at once, "Spell Aborted", the spell forgotten unless an item is
 * used. *done is left as it was otherwise outside a battle; in one the
 * target routine sets it. frame is the low byte of 5b04:1415's frame
 * pointer, which its caller's depth on the stack fixes: COK_CAST_COMMANDS
 * or COK_CAST_CAMP (see Strength), 0 where it does not matter. */
void cok_cast_spell(cok_adventure *game, uint8_t spell, bool ai, bool announce, uint8_t frame,
                    bool *done);

/* 5b04:0ecb: spell's range for the selected character: byte 2 plus byte 3
 * times the caster level (6346:29fe), or times 6 while an item is used, as
 * a byte; 0 for a spell whose byte 6 is not 0, and 0xff (touch), become 1.
 * Returns false, ending the run, where the caster level cannot be had. */
bool cok_cast_range(cok_adventure *game, uint8_t spell, uint8_t *range);

/* 432f:1eed: the ally of c's that the computer's Cure Light Wounds heals:
 * of the cells around c and its own (DS:1ed6, 1edf), a combatant on
 * c's side that is hurt, the one with the fewest hit points, or c itself
 * below half; but if none has fewer than 8, the last body found (a cell of
 * 0x1f with a body, DS:69ee, whose record is neither dead, stoned nor gone)
 * in its place. *ally is NULL for none. Returns false, ending the run,
 * where the original reads past its tables. */
bool cok_cast_wounded_ally(cok_adventure *game, cok_character *c, cok_character **ally);

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

/* gone, a party member, is about to be freed (Alter's Drop, treasure's
 * removal): the spell targets and the exploding list that hold it hold
 * cok_arena_freed instead (the original keeps the freed pointer, which
 * matches whatever record the heap puts there). */
void cok_cast_forget(cok_adventure *game, const cok_character *gone);

/* The records outside the body table that 6beb:0e08's stale byte can read
 * (cok_arena_kill): the exploding list's entries 1, 8 and 15 and the spell
 * targets' entries 1, 8, ..., 64 (DS:6feb + 28j). */
cok_arena_reachable cok_cast_reachable(const cok_adventure *game);

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

/* 60f4:20f7: add effect id to c for minutes with value, unless its effects
 * on event 9 cancel it (DS:6b2f) or c saved against a spell whose save is
 * of kind 1, when it "is Unaffected"; an effect of the id with time left
 * is removed first, permanent ones stay. Then text, if any, is said with
 * a flash on c (6346:228c, kind 1) and the text cleared. Returns false,
 * ending the run, where it cannot be carried out. */
bool cok_cast_add_effect(cok_adventure *game, cok_character *c, uint8_t id, uint16_t minutes,
                         uint8_t value, bool on_remove, uint8_t save_kind, bool saved,
                         const char *text);

/* Effect 0x47 turning a spell back on its caster (3f44:23a4) in combat: c
 * "reflects the spell" (row 10, no wait), the spell's cell becomes the
 * selected character's, which takes c's place in the targets
 * (DS:6feb), and the spell's missile (icon slot 0x12, one picture) flies
 * from c to the first target; *caster is the selected character. Returns
 * false, ending the run, where it cannot be carried out, as for a first
 * target of an earlier battle, whose record is freed. */
bool cok_cast_reflect(cok_adventure *game, cok_character *c, cok_character **caster);

/* The breath or attack of a monster (the effect handlers of event 0x0e in
 * 5b04, DS:6b94) for c holding effect, the selected character casting
 * through the computer's targets (432f:2337, AI 1): 4 (4ab8) breath at
 * one beside or energy bolts, 0x4e (4dc2) a fireball, 0x54 (4eea) poison
 * in a burst, 0x55 (50cf) a lightning bolt, 0x5a (5227) acid in a line,
 * and 6, 0x53 and 0x68 (546e) cold or fire in a cone; each, used, ends
 * c's turn (6346:2964). A dragon's cone that meets no one on its line
 * takes its side lines from a byte of the stack, a fixed offset from the
 * battle's stack pointer (cok_adventure.battle_sp). Returns false, ending
 * the run, where it cannot be carried out. */
bool cok_cast_breath(cok_adventure *game, cok_character *c, cok_effect *effect);

/* The byte at DS:at, from 0x1ed6 to 0x1feb: the steps' tables (DS:1ed6,
 * 1edf) and the terrain table after them (DS:1ee4), as a signed byte,
 * which a step of any byte reads as a direction. */
int8_t cok_cast_step_byte(uint16_t at);

/* Remove Curse's handler (5b04:35f5) on target, as the temple runs it with
 * the first target (DS:6feb) set: Bestow Curse (0x24) goes, or else the
 * first cursed item is unreadied, still cursed. Returns false, ending the
 * run, where it cannot be carried out. */
bool cok_cast_remove_curse(cok_adventure *game, cok_character *target);

#endif
