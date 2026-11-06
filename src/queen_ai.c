#include "queen.h"

#include "ai.h"
#include "arena.h"
#include "attack.h"
#include "combat.h"
#include "data.h"
#include "ecl.h"
#include "field.h"
#include "party.h"
#include "queen_view.h"
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

/* The record of a party member (or monster) at record, or NULL. */
static cok_character *member_at(cok_adventure *game, const uint8_t *record)
{
    size_t i = cok_party_index(&game->party, record);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* 5bb8:1396's needs (aca8-acab), from the target's effects (177e:1734 on each): 0x7d sets the need
 * 2, 0x3c 3, and 0x50, 0x51 or 0x67 1 (later wins); need_flagged is need, or 1 with effect 0x29
 * where need is 0; 0x42 and 0x43 are the two halvings. None without a target (149d). */
void queen_weapon_needs_of(const cok_character *target, queen_weapon_needs *needs)
{
    needs->need = needs->need_flagged = 0;
    needs->effect_42 = needs->effect_43 = false;
    if (target == NULL) return;
    if (holds(target, 0x7d)) needs->need = 2;
    if (holds(target, 0x3c)) needs->need = 3;
    if (holds(target, 0x50) || holds(target, 0x51) || holds(target, 0x67)) needs->need = 1;
    needs->need_flagged = needs->need;
    if (needs->need_flagged == 0 && holds(target, 0x29)) needs->need_flagged = 1;
    needs->effect_42 = holds(target, 0x42);
    needs->effect_43 = holds(target, 0x43);
}

/* 5bb8:1271, a weapon's worth (byte arithmetic throughout, as the original's al). The item's bonus
 * (signed, above 0) adds 8 times it; t[11] (signed, above 0) twice; with flags 8, twice t[5] less
 * 1; with t[1] 1 or less, 3. Hands held plus t[1] of 3 or more, or a cursed item: 0. Halved where
 * the needs say so (effect 0x42 with t[7] 1 or less, 0x43 with t[7] bit 0x80). 0 where the bonus,
 * unsigned, is below the need (need_flagged with flags 8 or 0x10), and 0 underwater with t[12]
 * above 1. */
uint8_t queen_weapon_score(const cok_rules *rules, uint8_t hands, const uint8_t *item,
                           const uint8_t *type, const queen_weapon_needs *needs, bool underwater)
{
    const cok_rules_item_record *I = &rules->item_record;
    uint8_t bonus = item[I->bonus];
    uint8_t s = (uint8_t)(type[10] * type[9]);
    if ((int8_t)bonus > 0) s = (uint8_t)(s + bonus * 8);
    if ((int8_t)type[11] > 0) s = (uint8_t)(s + type[11] * 2);
    if ((type[14] & 8) != 0) s = (uint8_t)(s + (type[5] - 1) * 2);
    if (type[1] <= 1) s = (uint8_t)(s + 3);
    if (hands + type[1] >= 3) s = 0;
    if (item[I->cursed] != 0) s = 0;
    if ((type[7] <= 1 && needs->effect_42) || ((type[7] & 0x80) != 0 && needs->effect_43))
        s = (uint8_t)(s / 2);
    uint8_t need = (type[14] & 0x18) != 0 ? needs->need_flagged : needs->need;
    if (bonus < need) s = 0;
    if (type[12] > 1 && underwater) s = 0;
    return s;
}

/* An item's type, which the item record's type byte (rules' item_record.type) names. */
static bool type_of(cok_adventure *game, const uint8_t *item, const uint8_t **type)
{
    uint8_t kind = item[cok_game_rules(game)->item_record.type];
    if (kind >= COK_ITEM_TYPES) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "an item type past ITEMS reads past the table (DS:ae3e)");
        return false;
    }
    *type = game->item_types.type[kind];
    return true;
}

/* The undefined stop of 5bb8:1396 (a message, and the run ends there). */
static bool undefined_weapon(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s (5bb8:1396)", what);
    return false;
}

/* Taking off the hands of item (1 + its index) of c: type[1] of its type (1396's sub at c+0xca). */
static bool take_hands(cok_adventure *game, cok_character *c, size_t item)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *type = NULL;
    if (!type_of(game, c->items[item - 1], &type)) return false;
    cok_field_set8(L, c->record, COK_FIELD_HANDS,
                   (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_HANDS) - type[1]));
    return true;
}

/* 86b8:1965 on item (1 + its index) of c, which must be the one the View shows (ad67, as the
 * shared ai.c's ready); the NULL item is the original's read through 0000:00xx. */
static bool ready(cok_adventure *game, cok_character *c, size_t item)
{
    if (item == 0 || item > c->item_count)
        return undefined_weapon(game, "Ready is given no item, read through NULL");
    if (game->vm.character != c->record)
        return undefined_weapon(game, "the computer's weapon is readied for another, the selected "
                                      "character");
    return queen_item_ready(game, c, item - 1);
}

/* 177e:0be2 on c. */
static bool stats(cok_adventure *game, cok_character *c)
{
    char error[300];
    if (cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error))
        return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
    return false;
}

/* 5bb8:1396, the computer's weapon choice for c with target (its record, or NULL). The best
 * missile weapon (flags 8 or 0x10) and the best melee one (flags 8 clear) of c's usable weapons in
 * list order, by queen_weapon_score with c's hands taken off slots 0 and 1 for the while (the
 * melee's start is the bare hands: +0xb7 * +0xb5, plus twice +0xb9 if positive; the shield's by its
 * bonus + 1, 0 below 0). A missile weapon is taken if it is worth more than half the melee one, has
 * ammunition (or flags 0x0a out of water) and is either two-handed (type[12] above 1, flags 0x14)
 * or no enemy is a square away (the side list, 51c2:0980, of range 1, its count 0). Then the slot 0
 * swap, the stats and attacks (52db:0edf, cok_combat_weapon_attacks, checked equal), slot 1's
 * hands, and the readying, as the original's branches; the panel when changed. A readied item's
 * power (86b8:16f7) is View's stop. */
bool queen_choose_weapon(cok_adventure *game, cok_character *c, const uint8_t *target)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_rules_item_record *I = &rules->item_record;
    const uint8_t *r = c->record;
    cok_character *t = NULL;
    queen_weapon_needs needs;
    if (target != NULL) {
        t = member_at(game, target);
        if (t == NULL) return undefined_weapon(game, "the target is no member of the party");
    }
    queen_weapon_needs_of(t, &needs);
    /* 14ac-150b: the hands of slots 0 and 1 are taken off. */
    if (c->slots[0] != 0 && !take_hands(game, c, c->slots[0])) return false;
    if (c->slots[1] != 0 && !take_hands(game, c, c->slots[1])) return false;
    bool underwater = cok_ecl_var(&game->vm, COK_VAR_RECORD_EFFECT) != 0; /* DS:b20f + 0x3c */
    uint8_t usable = cok_field_u8(L, r, COK_FIELD_USABLE_CLASSES);
    size_t missile = 0, melee = 0, shield = 0;
    uint8_t missile_best = 1, shield_best = 0;
    uint8_t melee_best = (uint8_t)(cok_field_u8_at(L, r, COK_FIELD_BASE_DICE, 1) *
                                   cok_field_u8_at(L, r, COK_FIELD_BASE_DICE_SIDES, 1));
    if (cok_field_s8_at(L, r, COK_FIELD_BASE_DAMAGE_BONUS, 1) > 0)
        melee_best = (uint8_t)(melee_best +
                               cok_field_s8_at(L, r, COK_FIELD_BASE_DAMAGE_BONUS, 1) * 2);
    for (size_t i = 0; i < c->item_count; ++i) {
        const uint8_t *it = c->items[i], *type = NULL;
        if (!type_of(game, it, &type)) return false;
        if (type[0] == 0 && (type[13] & usable) != 0) {
            uint8_t s = queen_weapon_score(rules, cok_field_u8(L, r, COK_FIELD_HANDS), it, type,
                                           &needs, underwater);
            if ((type[14] & 0x18) != 0 && s > missile_best) {
                missile = i + 1;
                missile_best = s;
            }
            if ((type[14] & 8) == 0 && s > melee_best) {
                melee = i + 1;
                melee_best = s;
            }
        }
        if (type[0] == 1 && (type[13] & usable) != 0) {
            uint8_t s = (int8_t)it[I->bonus] >= 0 ? (uint8_t)(it[I->bonus] + 1) : 0;
            if (s > shield_best) {
                shield = i + 1;
                shield_best = s;
            }
        }
    }
    /* 1706-1789: two (two-handed) and has (the ammunition, or flags 0x0a out of water). */
    bool two = false, has = false;
    if (missile != 0) {
        const uint8_t *type = NULL;
        if (!type_of(game, c->items[missile - 1], &type)) return false;
        two = type[12] > 1 && (type[14] & 0x14) == 0x14;
        uint8_t flags = type[14];
        size_t ammunition = 0;
        if ((flags & 0x10) != 0) ammunition = missile;
        if ((flags & 8) != 0) {
            if ((flags & 1) != 0) ammunition = c->slots[11];
            if ((flags & 0x80) != 0) ammunition = c->slots[12];
        }
        has = ammunition != 0 || (flags == 0x0a && !underwater);
    }
    /* 1792-17dd: the missile weapon if it is worth more than half the melee one and has ammunition,
     * and two-handed or no enemy is a square away (51c2:0980, its count). */
    size_t chosen = melee;
    if (missile != 0 && missile_best > melee_best / 2 && has) {
        bool take = two;
        if (!take) {
            uint8_t n = 0;
            if (!cok_combat_side_list(game, c, 1, false, true, &n)) return false;
            take = n == 0;
        }
        if (take) chosen = missile;
    }
    /* 17e6-1820: no swap when slot 0 is the chosen one or cursed. */
    bool changed = false;
    /* The slots are read where the original reads them: slot 0 here, slot 1 after each stats. */
    size_t slot0 = c->slots[0];
    bool cursed0 = slot0 != 0 && c->items[slot0 - 1][I->cursed] != 0;
    if (slot0 == 0 || (slot0 != chosen && !cursed0)) {
        if (slot0 != 0 && !ready(game, c, slot0)) return false;
        if (!stats(game, c)) return false;
        size_t held = c->slots[1];
        if (held != 0 && c->items[held - 1][I->cursed] == 0 && !take_hands(game, c, held))
            return false;
        if (chosen != 0 && !ready(game, c, chosen)) return false;
        changed = true;
    }
    if (!stats(game, c)) return false;
    if (!cok_combat_weapon_attacks(game, c)) return false;
    /* 18c8-18f5: slot 1 is kept (not free) when it is the shield or cursed. */
    size_t slot1 = c->slots[1];
    bool free1 = true;
    if (slot1 != 0 && (slot1 == shield || c->items[slot1 - 1][I->cursed] != 0)) free1 = false;
    uint8_t hands = cok_field_u8(L, r, COK_FIELD_HANDS);
    if (hands > 2) {
        if (slot1 == 0 || c->items[slot1 - 1][I->cursed] != 0) {
            if (!ready(game, c, chosen)) return false;
        } else if (!ready(game, c, slot1)) {
            return false;
        }
        changed = true;
    } else if (hands < 2 && free1) {
        if (slot1 != 0 && !ready(game, c, slot1)) return false;
        if (!stats(game, c)) return false;
        if (shield != 0 && !ready(game, c, shield)) return false;
        changed = true;
    }
    if (!stats(game, c)) return false;
    return !changed || cok_arena_panel(game, c); /* 177e:085f */
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
 * turn), and the weapon's choice (1396, queen_choose_weapon), then the keys again (1050: over ends
 * the turn). The loop of step 14 (0b3d, the fight) is the stop: its pick and fight are part 15c.
 * DS:ae48, the effect handlers' "hook done" byte, is written by 0000:01bf (0c11's result, 1) after
 * 0268 returns 1; 0268 itself does not write it. Each handler writes it before it reads it (scope
 * 0000, step 11), so it is not modelled. */
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
    if (!queen_choose_weapon(game, c, cr->target)) return false;
    if (!cok_ai_keys(game, c, &over)) return false; /* 5bb8:1050 again: over ends the turn */
    if (over) return true;
    return cok_adventure_not_yet(game, "the computer's fight", "dqk 5bb8:0b3d (part 15c)");
}
