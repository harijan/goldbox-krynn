#include "queen.h"

#include "ai.h"
#include "arena.h"
#include "attack.h"
#include "combat.h"
#include "data.h"
#include "ecl.h"
#include "field.h"
#include "party.h"
#include "round.h"

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

/* Whether c holds effect id (177e:1734). */
static bool holds(const cok_character *c, uint8_t id)
{
    return cok_character_find_effect(c, id) != NULL;
}

/* 5bb8:0438, may it step (cok_ai_can_step's descriptor routine). The turn of try's way (DS:5db4
 * + 6 * pattern + try, read unbounded: ways 1-6 and tries 1-5 are all the table is read for)
 * turns dir to the square d. The probe gives its occupant and terrain (5837:0bd1), and the marks of
 * the same square its kind (the last of 0x1b-0x1d) and acid (0x46). Terrain 0 is off the map; a
 * terrain of cost 0xff, an occupant, or a cost (2 a step, 3 on a diagonal, a byte) of the movement or
 * more refuses. A cloud (kind 0x1b, 0x1c or 0x1d) or acid costs all the movement (movement + 1, a byte)
 * unless the record is forced or holds what frees it: kind 0x1c not 0x39 or 0x8f and +0xc8 not 0x10;
 * kind 0x1d not 0x1e, 0xa0, 0x8f, 0x3f or 0x39 and +0xc8 not 0x18; acid only while the highest level,
 * unsigned, is below 7. No saving throw, no draw. */
bool queen_can_step(cok_adventure *game, cok_character *c, uint8_t try_, uint8_t dir, bool *offmap,
                    bool *ok)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_data_segment *D = rules->data;
    const uint8_t *r = c->record;
    cok_combat_record *cr = c->combat;
    *offmap = false;
    *ok = false;
    if (cr == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a combatant has no combat record, read through NULL (5bb8:0438)");
        return false;
    }
    long way = cok_data_offset2(D, COK_DATA_WAYS, cr->pattern, try_);
    if (!cok_data_holds(D, COK_DATA_WAYS, way)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "way of moving %u reads past its table (DS:5db4, 5bb8:0438)",
                           cr->pattern);
        return false;
    }
    uint8_t turn = cok_data_held(D, way);
    uint8_t d = (uint8_t)((turn + dir) % 8);
    uint8_t occupant, terrain, kind;
    bool cloud, puddle, acid;
    if (!cok_combat_probe(&game->combat, rules, r, d, &occupant, &terrain, &cloud, &puddle) ||
        !cok_combat_marks(&game->combat, rules, r, d, &kind, &acid)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a size past the footprints is read after them (dqk 5837:0bd1)");
        return false;
    }
    if (terrain == 0) {
        *offmap = true;
        return true;
    }
    uint8_t cost = cok_combat_terrain(rules, terrain).cost;
    if (cost == 0xff) return true;
    cost = (uint8_t)(cost * ((d & 1) != 0 ? 3 : 2));
    if (occupant != 0 || cr->movement <= cost) return true;
    uint16_t flags_at = rules->effects.damage_flags_at;
    bool all = false;
    if (kind == 0x1b) {
        all = cr->forced == 0;
    } else if (kind == 0x1c) {
        all = cr->forced == 0 && !holds(c, 0x39) && !holds(c, 0x8f) && (r[flags_at] & 0x10) == 0;
    } else if (kind == 0x1d) {
        all = cr->forced == 0 && !holds(c, 0x1e) && !holds(c, 0xa0) && !holds(c, 0x8f) &&
              !holds(c, 0x3f) && !holds(c, 0x39) && (r[flags_at] & 0x18) == 0;
    }
    if (acid && cr->forced == 0 && cok_field_u8(L, r, COK_FIELD_HIGHEST_LEVEL) < 7) all = true;
    if (all) cost = (uint8_t)(cr->movement + 1);
    *ok = cr->movement >= cost;
    return true;
}

/* 5bb8:0268, turn undead: the decision. A fleeing record does not turn (0279); nor one that has
 * turned as often as the undead count DS:c2ed (+0x12 turns, unsigned); nor one with no cleric
 * level and no paladin level above the descriptor's paladin_turns (both bytes read unsigned,
 * 2234). The undead search 52db:135e, with the limit turn_limit (0x0d), is not ported for Dark
 * Queen: the stop. Its found branch is 52db:1117 (turning, part 26) and 0268 returns 1 for it. */
static bool turn_undead(cok_adventure *game, cok_character *c)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_rules_fight *F = &rules->fight;
    const uint8_t *r = c->record;
    if (c->combat->fleeing != 0) return true;
    if (c->combat->turns >= game->undead) return true;
    bool cleric = cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 0) > 0;
    bool paladin = cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 3) > F->paladin_turns;
    if (!cleric && !paladin) return true;
    return cok_adventure_not_yet(game, "turning undead",
                                 "dqk 52db:135e (the undead search, not ported)");
}

/* The computer's turn (dqk 5bb8:0000). ai.c's cok_combat_computer hands a Dark Queen record's turn
 * here (the descriptor's computer_routine). The steps in the original's order, cited by offset:
 * a record with no target picks one first (52db:3d9e with 0xff, 1, 0, 0; its result unused: the
 * shared pick, the tries listed); a record holding the battle's facing effect (0xd5, 177e:1734)
 * ends its turn and never acts; the keys (5bb8:1050: cok_ai_keys), then the bottom bar (1bdb:036c:
 * the arena's clear_bar) and the text (177e:1665); a record that cannot act (+0x18b 0) ends its
 * turn, and the way of moving is still drawn (0074 falls through to 008d); morale (1117: the
 * shared cok_ai_morale, unless over); one fleeing and not forced says "flees in panic"; over ends.
 * 5bb8:02f5 (cok_ai_use_item) is never taken: it returns 0 with no draw (DKK's draws its die;
 * the port's use_item must not run here). A pending spell's cast (849a:145b) is the stop; then
 * turn undead (0268, turn_undead), the spell choice (0306, queen_spell_choice; a cast ends the
 * turn), and the weapon's choice (1396, part 15b): the stop. DS:ae48, which 0268's end writes 1
 * to (0c11 and 0268's turn), is the effect handlers' "hook done" byte: each handler writes it
 * before it reads it (scope 0000, step 11), so it is not modelled. */
bool queen_computer(cok_adventure *game, cok_character *c)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_rules_fight *F = &rules->fight;
    const cok_combat_block *B = rules->combat.combat_block;
    uint8_t *r = c->record;
    cok_combat_record *cr = c->combat;
    bool over = false, found = false;
    if (cr == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a combatant has no combat record, read through NULL (5bb8:0000)");
        return false;
    }
    if (cr->target == NULL && !cok_combat_pick_target(game, c, 0xff, true, false, &found))
        return false;
    if (B != NULL && B->facing_effect != 0 &&
        cok_character_find_effect(c, B->facing_effect) != NULL) {
        cok_combat_end_turn(c);
        return true;
    }
    if (!cok_ai_keys(game, c, &over)) return false;
    cok_arena_clear_bar(game); /* 1bdb:036c; dqk 5bb8:006a */
    cok_arena_clear_text(game); /* 177e:1665 */
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) {
        cok_combat_end_turn(c); /* 51c2:0c11 */
        over = true;
    }
    if (!cok_ai_pattern(game, c)) return false;
    if (!over && !cok_ai_morale(game, c, &over)) return false;
    if (cr->fleeing != 0 && cr->forced == 0 &&
        !cok_arena_say(game, c, F->words.panic, 10, true)) /* 177e:156e, DS:5dde */
        return false;
    if (over) return true;
    if (cr->spell > 0) {
        cok_combat_end_turn(c); /* 0c11, then the stop */
        return cok_adventure_not_yet(game, "a pending spell's cast", rules->combat.spell_unported);
    }
    if (!turn_undead(game, c)) return false;
    bool cast = false;
    if (!queen_spell_choice(game, c, &cast)) return false;
    if (cast) return true;
    return cok_adventure_not_yet(game, "the computer's weapon", "dqk 5bb8:1396 (part 15b)");
}
