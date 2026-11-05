#include "queen.h"

#include "ecl.h"
#include "field.h"
#include "party.h"

/* The computer's spell choice (dqk 5bb8:0306): the prefix that decides whether a computer
 * character casts at all, before the choice of the spell (5bb8:0355: 1b1e, 1a07, 19b0; part 29).
 * It casts nothing when it is fleeing, when var COK_VAR_NO_MAGIC is set, when it may not cast, or
 * when it is a monster (+0x9b below 0x7f) and Magic is off. */
bool queen_spell_choice(cok_adventure *game, cok_character *c, bool *cast)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_combat_record *cr = c->combat;
    *cast = false;
    if (cr == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a combatant has no combat record, read through NULL (5bb8:0306)");
        return false;
    }
    if (cr->fleeing != 0) return true;
    if (cok_ecl_var(&game->vm, COK_VAR_NO_MAGIC) != 0) return true;
    if (cr->may_cast == 0) return true;
    if (cok_field_u8(L, c->record, COK_FIELD_NPC) < 0x7f && !game->combat.magic_on) return true;
    return cok_adventure_not_yet(
        game, "the computer's spell choice",
        "dqk 5bb8:0306 (0355 on: the spell choice, 1b1e/1a07/19b0, part 29)");
}
