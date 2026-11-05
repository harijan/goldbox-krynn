#include "queen.h"

#include "combat.h"
#include "data.h"
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
