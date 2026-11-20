#include "queen.h"

#include "ai.h"
#include "arena.h"
#include "attack.h"
#include "cast.h"
#include "combat.h"
#include "data.h"
#include "ecl.h"
#include "field.h"
#include "party.h"
#include "queen_view.h"
#include "round.h"

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

/* The highest kind of undead the computer's turn and the party's Turn turn (dqk 5bb8:0268 and
 * 52db:1117 set 0x0d). */
enum { QUEEN_TURN_LIMIT = 13 };

/* 5bb8:0268, turn undead: the decision. A fleeing record does not turn (0279); nor one that has
 * turned as often as the undead count DS:c2ed (+0x12 turns, unsigned); nor one with no cleric
 * level and no paladin level above the descriptor's paladin_turns (both bytes read unsigned,
 * 2234). The undead search 52db:135e with the limit 13 (queen_undead); found, 52db:1117 turns
 * (queen_turn_undead) and 0268 returns 1: *turned. */
static bool turn_undead(cok_adventure *game, cok_character *c, bool *turned)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_rules_fight *F = &rules->fight;
    const uint8_t *r = c->record;
    *turned = false;
    if (c->combat->fleeing != 0) return true;
    if (c->combat->turns >= game->undead) return true;
    bool cleric = cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 0) > 0;
    bool paladin = cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 3) > F->paladin_turns;
    if (!cleric && !paladin) return true;
    cok_character *undead;
    bool found;
    int16_t limit = QUEEN_TURN_LIMIT;
    if (!queen_undead(game, c, &undead, &limit, &found)) return false;
    if (!found) return true;
    if (!queen_turn_undead(game, c)) return false;
    *turned = true;
    return true;
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

/* Event ev for c (148f:0512; ai.c's event). */
static bool event(cok_adventure *game, cok_character *c, uint8_t ev)
{
    if (cok_effects_dispatch(&game->effects, c, ev)) return true;
    return cok_adventure_effect_failed(game);
}

/* An undefined stop of the computer's fight (a message, and the run ends there). */
static bool undefined_fight(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* The item type of the item past the types' end (DS:ae3e + 16 * type, 5bb8:0b3d's reads). */
static bool type_past(cok_adventure *game)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED,
                       "an item type past ITEMS reads past the table (DS:ae3e, 5bb8:0b3d)");
    return false;
}

/* The distance of 5bb8:0b3d's 0b26 (cok_combat_distance, which puts the listing back: 4 bytes of it
 * for Dark Queen). */
static bool distance_to(cok_adventure *game, const cok_character *origin,
                        const cok_character *target, uint8_t *d)
{
    if (cok_combat_distance(&game->combat, cok_game_rules(game), origin->record, target->record, d))
        return true;
    return undefined_fight(game, "the distance (51c2:0b26, 5bb8:0b3d) lists the combatants "
                                 "around one (6b30:08d8) past its tables");
}

/* The direction of 5bb8:0b3d's 16dd from a to b (6346:34e9). */
static bool direction_to(cok_adventure *game, const cok_character *a, const cok_character *b,
                         uint8_t *d)
{
    if (cok_combat_direction(&game->combat, a, b, d)) return true;
    return undefined_fight(game, "no direction's wedge holds the other (6346:34e9, 5bb8:0b3d), "
                                 "where the original loops for ever");
}

/* The record of combatant n (DS:b2f8 + 4n, the combatant the pick names). */
static bool combatant_of(cok_adventure *game, uint8_t n, cok_character **c)
{
    *c = n <= COK_COMBATANTS ? game->combat.combatant[n].character : NULL;
    if (*c != NULL) return true;
    return undefined_fight(game, "a combatant picked has no record, read through NULL (5bb8:0b3d)");
}

/* The undead search (dqk 52db:135e): the enemies of c within sight (51c2:0980 with range 0xff, in
 * the list's order, nearest first), the first of the highest kind (the signed word +0x5c, above 0
 * and no higher than *limit, read as signed words) not forced (+0x11); *limit is that kind, 0 for
 * none. A kind equal to the best found earlier keeps that one. False where the run stopped. */
bool queen_undead(cok_adventure *game, cok_character *c, cok_character **undead, int16_t *limit,
                  bool *found)
{
    const cok_rules *rules = cok_game_rules(game);
    cok_combat *combat = &game->combat;
    int16_t best = 0;
    uint8_t n;
    *undead = NULL;
    *found = false;
    if (!cok_combat_enemies(game, c, 0xff, &n)) return false;
    for (unsigned i = 1; i <= n; ++i) {
        cok_character *e;
        if (!combatant_of(game, combat->enemies[i], &e)) return false;
        if (e->combat == NULL)
            return undefined_fight(game, "a combatant in sight has no combat record, read through "
                                         "NULL (52db:135e)");
        int16_t kind = (int16_t)cok_combat_field_word(rules, e->record, COK_FIELD_UNDEAD);
        if (e->combat->forced != 0 || kind <= 0 || kind <= best || kind > *limit) continue;
        best = kind;
        *undead = e;
        *found = true;
    }
    *limit = best;
    return true;
}

/* Turning undead (dqk 52db:1117): c's "turns undead..." (row 10), the bottom bar blanked and a
 * pause; c's turn count (+0x12) up. The d20 is drawn first, then the level: the cleric's level
 * (signed), or the paladin's less 2 where that is higher; deity word 2 adds 2; above 8 it is 9
 * below 14, else 10; var COK_VAR_TURNING is taken from it (0 where the var is above it, unsigned).
 * Then each undead the search (queen_undead, from the limit 13) finds, with the count (1d12, or
 * 1d6 + 6 at the first one whose table value is negative): the value of its kind and the level
 * (DS:5a6a, cok_data_offset2) with |value| above the d20 lowers the limit and is not turned; else
 * it is turned away (value above 0: "is turned", leaves the battle with status 3) or destroyed
 * (value 0 or below: "Is destroyed", its status word 8, inactive). The count drops by one each
 * time, the bonus of 6 turns ("extra") with it; the text is cleared after each. None turned:
 * "Nothing Happens..." (one draw). Then the sides are counted, c's turn is ended (51c2:0c11) and
 * the attack-over flag DS:bd2e is set. False where the run stopped. */
bool queen_turn_undead(cok_adventure *game, cok_character *c)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_data_segment *D = rules->data;
    const cok_rules_fight *F = &rules->fight;
    const uint8_t *r = c->record;
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = c->combat;
    if (cr == NULL)
        return undefined_fight(game, "a combatant has no combat record, read through NULL "
                                     "(dqk 52db:1117)");
    if (!cok_arena_say(game, c, "turns undead...", 10, false)) return false;
    cok_arena_clear_bar(game); /* 1bdb:036c */
    cok_arena_pause(game);     /* 1a62:0949 */
    bool any = false;
    ++cr->turns;
    int16_t limit = QUEEN_TURN_LIMIT;
    uint8_t extra = 6, count = 0xff;
    uint8_t d20 = cok_dice(&game->vm.seed, 1, 20);
    /* The level (52db:1117's 1190-11f7): the cleric's, or the paladin's less 2 where higher. */
    int8_t lv = cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 0);
    int paladin = (int)cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 3) - 2;
    if (paladin > lv) lv = (int8_t)paladin;
    if (cok_combat_field_word(rules, r, COK_FIELD_DEITY) == 2) lv = (int8_t)(lv + 2);
    if (lv > 8) lv = lv < 14 ? 9 : 10;
    uint8_t less = (uint8_t)cok_ecl_var(&game->vm, COK_VAR_TURNING);
    uint8_t level = (uint8_t)lv < less ? 0 : (uint8_t)((uint8_t)lv - less);
    for (;;) {
        cok_character *u;
        bool found;
        if (!queen_undead(game, c, &u, &limit, &found)) return false;
        if (!found || count == 0) break;
        int16_t kind = (int16_t)cok_combat_field_word(rules, u->record, COK_FIELD_UNDEAD);
        long at = cok_data_offset2(D, COK_DATA_TURNING, kind, (int8_t)level);
        if (!cok_data_holds(D, COK_DATA_TURNING, at))
            return undefined_fight(game, "turning undead reads a kind or level past DS:5a6a's "
                                         "table (dqk 52db:1117)");
        int8_t v = (int8_t)cok_data_held(D, at);
        if (count == 0xff) count = v < 0 ? (uint8_t)(cok_dice(&game->vm.seed, 1, 6) + 6)
                                         : cok_dice(&game->vm.seed, 1, 12);
        int magnitude = v < 0 ? -v : v;
        if (d20 < magnitude) {
            --limit;
            continue;
        }
        any = true;
        if (!cok_arena_turn(game, u, 3, false)) return false;
        combat->panel = true;
        if (!cok_arena_panel(game, u)) return false;
        if (v > 0) {
            if (!cok_combat_leave(game, u, 3, "is turned")) return false;
        } else {
            /* The stale byte is not read (dqk 5837:0d55's kill_finds_body, as attack.c's). */
            cok_arena_reachable also = cok_cast_reachable(game);
            if (!cok_arena_say(game, u, F->words.destroyed, 10, false) ||
                !cok_arena_kill(game, u, COK_ARENA_STALE_UNKNOWN, &also))
                return false;
            cok_combat_set_field_word(rules, u->record, COK_FIELD_STATUS, 8);
            cok_field_set8(L, u->record, COK_FIELD_ACTIVE, 0);
        }
        if (extra > 0) --extra;
        --count;
        if (count == 0 && extra > 0 && v < 0) ++count;
        cok_arena_clear_text(game);
    }
    if (!any) queen_bar_message(game, F->words.nothing); /* 177e:153c: one draw */
    if (!cok_combat_count_sides(combat, &game->party, rules, game->ambush))
        return undefined_fight(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                                     "DS:6b2d (dqk 51c2:0920)");
    cok_combat_end_turn(c); /* 51c2:0c11 (its result, 1, is DS:bd2e) */
    combat->attack_over = true;
    cok_arena_clear_text(game);
    return true;
}

/* The computer's fight of one pass (dqk 5bb8:0b3d), with the target the loop of queen_computer
 * picked (its record found and active; the fight is entered only then). The original's frame: the
 * last step 8 and stuck, stuck_more 0 (aca5-aca7), the pass's count, going and done. The steps, by
 * offset:
 * 0b43-0b5e: the prologue, event 0x0e; a party record (hostile word 0) bandaged first
 *   (51c2:0d63(1), cok_combat_bandage) ends its initiative; initiative 0 is not going.
 * 0b6e-0bec: while going and not done, the fleeing test once (+0x15); then, fleeing with movement
 *   left and an initiative of 0 to 0x13, 6b0 (cok_ai_step) until one of those ends.
 * 0bec-0c21: initiative 0 or 0x14 ends going; the loop goes on while going and not done.
 * 0c24-0c44: the count (a byte) above 20: done, the guard (0fec), going 0; the pass goes on.
 * 0c47-0c8f: the reach, the weapon's type[12] (1 for 0 or 0xff; no reach_less for Dark Queen).
 * 0c88-0cba: the kept target, dropped where it is inactive (+0x18b 0) or on the other side
 *   (hostile word 0); one kept and able to be attacked (52db:1030) is seen within the reach
 *   (5d71:0328, see_all 0, map +8): in reach if its range halved is at most the reach.
 * 0d56-0d7b: not in reach, the enemies within the reach (51c2:0980); none: the pick (52db:3d9e
 *   with 0xff, 0, 0, 0), found: 6b0 and, a spell begun (spell > 0), going 0; not found: the guard
 *   and done. Some: d(1, n) (148f:1968) picks combatant bce7[pick] (its record, the target).
 * 0dd0-0e62: the missile weapon not thrown (51c2:0cbf, 0cfe) with an enemy a square away (0980 with
 *   range 1), or underwater (var 0x3c): 1396 with the block's target (queen_choose_weapon), done.
 *   Otherwise the distance (0b26); one a square away, or one able to be attacked, is in reach.
 * 0e9c-0ee6: in reach: the direction (16dd) and centre (5837:0846, margin 2); a sweep (52e4) ends
 *   the turn and done; else booked (1853) and, a missile weapon, its ammunition (10c3: in reach is
 *   its has); a thrown one a square away has none; the attack (18d6) with done. Then done: going 0;
 *   a target that is inactive is done.
 * Returns !going, the loop of queen_computer's over. Its pick draws come from the caller. */
bool queen_fight(cok_adventure *game, cok_character *c, bool *over)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    cok_combat *combat = &game->combat;
    const uint8_t *r = c->record;
    cok_combat_record *cr = c->combat;
    uint8_t count = 0;
    bool going = true, done = false;
    if (cr == NULL)
        return undefined_fight(game, "a combatant has no combat record, read through NULL (5bb8:0b3d)");
    combat->last_step = 8;
    combat->stuck = 0;
    combat->stuck_more = 0;
    if (!event(game, c, 0x0e)) return false;
    if (cok_combat_field_word(rules, r, COK_FIELD_HOSTILE) == 0 && cok_combat_bandage(game, true))
        cr->initiative = 0;
    if (cr->initiative == 0) going = false;
    while (!done && going) {
        bool fleeing = cr->fleeing != 0; /* 0b6e: tested once per pass */
        while (fleeing && cr->movement > 0 && cr->initiative > 0 && cr->initiative < 0x14)
            if (!cok_ai_step(game, c)) return false;
        if (cr->initiative == 0 || cr->initiative == 0x14) going = false;
        if (!going) break;
        if (++count > 20) { /* 0c24 */
            done = true;
            if (!cok_ai_guard(game, c)) return false; /* 0fec */
            going = false;
        }
        combat->in_reach = false;
        uint8_t reach = 1;
        if (c->slots[0] != 0) {
            const uint8_t *type = NULL;
            if (!type_of(game, c->items[c->slots[0] - 1], &type)) return false;
            reach = type[12];
        }
        if (reach == 0 || reach == 0xff) reach = 1;
        /* The kept target; a NULL one reads nothing in the port (dropped). */
        cok_character *target = NULL;
        const uint8_t *t = cr->target;
        if (t != NULL && cok_field_u8(L, t, COK_FIELD_ACTIVE) != 0 &&
            cok_combat_field_word(rules, t, COK_FIELD_HOSTILE) != 0) {
            target = member_at(game, t);
            if (target == NULL) return undefined_fight(game, "the target is not in the list (5bb8:0b3d)");
            bool can = false;
            if (!cok_combat_can_attack(game, c, t, &can)) return false;
            if (can) {
                int8_t x1 = cok_combat_x(combat, t), y1 = cok_combat_y(combat, t);
                uint16_t range = reach;
                bool seen = false;
                combat->see_all = 0; /* DS:bedb + 8 */
                if (!cok_combat_sight(combat, rules, cok_combat_x(combat, r),
                                      cok_combat_y(combat, r), &x1, &y1, &range, &seen))
                    return undefined_fight(game, "a cell's terrain is past the table (5d71:0328, 5bb8:0b3d)");
                if (seen && range >> 1 <= reach) combat->in_reach = true;
            }
        }
        if (!combat->in_reach) {
            uint8_t n = 0;
            if (!cok_combat_enemies(game, c, reach, &n)) return false; /* 51c2:0980 */
            if (n == 0) {
                bool found = false;
                if (!cok_combat_pick_target(game, c, 0xff, false, false, &found)) return false;
                if (found) {
                    if (!cok_ai_step(game, c)) return false;
                    if (cr->spell > 0) going = false; /* 0d9b: 6b0 began a spell */
                } else {
                    if (!cok_ai_guard(game, c)) return false;
                    done = true;
                }
            } else {
                uint8_t pick = cok_dice(&game->vm.seed, 1, n); /* 148f:1968(1, n) */
                if (!combatant_of(game, combat->enemies[pick], &target)) return false;
                bool missile = false, thrown = false;
                if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return type_past(game);
                bool use = false;
                if (missile) {
                    if (!cok_combat_thrown(&game->item_types, c, &thrown)) return type_past(game);
                    if (!thrown) {
                        uint8_t near = 0;
                        if (!cok_combat_enemies(game, c, 1, &near)) return false;
                        /* DS:b20f + 0x3c, underwater: the weapon's choice whether or not one is near */
                        use = near > 0 || cok_ecl_var(&game->vm, COK_VAR_RECORD_EFFECT) != 0;
                    }
                }
                if (use) {
                    if (!queen_choose_weapon(game, c, cr->target)) return false;
                    done = true;
                } else {
                    uint8_t d = 0;
                    bool can = false;
                    if (!distance_to(game, c, target, &d)) return false;
                    if (d != 1 && !cok_combat_can_attack(game, c, target->record, &can)) return false;
                    if (d == 1 || can) combat->in_reach = true;
                }
            }
        }
        if (!combat->in_reach) continue;
        uint8_t d = 0;
        if (!direction_to(game, c, target, &d) ||
            !cok_arena_centre(game, cok_combat_x(combat, r), cok_combat_y(combat, r), 2, d))
            return false;
        bool swept = false;
        if (!cok_combat_sweep(game, c, target, &swept)) return false;
        if (swept) {
            cok_combat_end_turn(c); /* 51c2:0c11 */
            done = true;
            continue;
        }
        if (!cok_combat_book(game, c, target)) return false;
        size_t ammunition = 0;
        bool missile = false;
        if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return type_past(game);
        if (missile) {
            bool has = false, thrown = false;
            if (!cok_combat_ammunition(game, c, &ammunition, &has)) return type_past(game);
            combat->in_reach = has;
            if (!cok_combat_thrown(&game->item_types, c, &thrown)) return type_past(game);
            if (thrown) {
                uint8_t dd = 0;
                if (!distance_to(game, c, target, &dd)) return false;
                if (dd == 1) ammunition = 0;
            }
        }
        if (!cok_combat_attack(game, c, target, false, ammunition, &done)) return false;
        if (done) {
            going = false;
            continue;
        }
        if (cok_field_u8(L, target->record, COK_FIELD_ACTIVE) == 0) done = true;
    }
    *over = !going;
    return true;
}

/* The spell record and the memorised bytes the computer's choice reads (DS:27e9 + 0x16 * id, and
 * the character's +0xce to +0x156, 0x89 bytes; part 29). */
enum {
    SPELL_LEVEL = 0x02,
    SPELL_TARGETING = 0x07, /* the low nibble the kind; 0 the caster alone */
    SPELL_EFFECT = 0x0e,
    SPELL_CASTABLE = 0x0f,  /* a word: 0 is camp-only */
    SPELL_PRIORITY = 0x12,  /* the computer's priority: it casts when a d5 is not above it */
    SPELL_ALLIES = 0x13,    /* a word: 0 for the caster's own side */
    MEMORIZED_AT = 0xce,
    MEMORIZED_COUNT = 0x89,
    MEMORIZED_LAST = MEMORIZED_COUNT - 1,
};

/* 52db:42cc (t, spell, side): the worth of spell to t, a signed byte (part 29). -40 where t's word
 * +0x66 is not side (the other side); else 4 where the spell's test for its id holds, 0 where it does
 * not, and then, if 4: 0 for a spell of level below 5 on a t holding 0x39 (the globe) and below 4 on
 * one holding 0x3f; the magic resistance (+0xcc) above 0x28 grades it 3 (0x29-0x46), 2 (0x47-0x64),
 * 1 (0x65-0x82) or 0 (above 0x82), overwriting the earlier 0 (the original's tail, as written). The
 * selected record is the 2a Prayer's test (vm.character, DS:ad67). The enemies within 1 of t are
 * listed without a draw. */
bool queen_target_worth(cok_adventure *game, cok_character *t, uint8_t spell, unsigned side,
                        int8_t *worth)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const uint8_t *r = t->record;
    *worth = 0;
    if (cok_combat_field_word(rules, r, COK_FIELD_HOSTILE) != side) {
        *worth = -40;
        return true;
    }
    uint8_t flags = cok_field_u8_at(L, r, COK_FIELD_CREATURE_FLAGS, 1);    /* +0xc8 */
    uint8_t flags7 = cok_field_u8_at(L, r, COK_FIELD_CREATURE_FLAGS, 0);   /* +0xc7 */
    uint8_t hit_max = cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS);         /* +0x89 */
    uint8_t hit = cok_field_u8(L, r, COK_FIELD_HIT_POINTS);                 /* +0x198 */
    uint8_t highest = cok_field_u8(L, r, COK_FIELD_HIGHEST_LEVEL);          /* +0x91 */
    bool race_low = (int16_t)cok_combat_field_word(rules, r, COK_FIELD_RACE) < 7; /* +0x58 */
    bool hit4 = false;
    switch (spell) {
    case 0x01: { /* Bless: t not holding 1, and no enemy within 1 of it */
        if (holds(t, 1)) break;
        uint8_t n = 0;
        if (!cok_combat_enemies(game, t, 1, &n)) return false;
        hit4 = n == 0;
        break;
    }
    case 0x03: case 0x3a: case 0x47: /* the cures: hit points below the maximum */
        hit4 = hit_max > hit;
        break;
    case 0x0a: /* +0x8a is 1 (size) and flags 2 */
        hit4 = (flags & 2) != 0 && cok_field_u8(L, r, COK_FIELD_SIZE) == 1;
        break;
    case 0x0f: case 0x86: /* Magic Missile: not holding 0x11 */
        hit4 = !holds(t, 0x11);
        break;
    case 0x15: /* Sleep: highest level below 5, not holding 0x8d or 0x35 */
        hit4 = highest < 5 && !holds(t, 0x8d) && !holds(t, 0x35);
        break;
    case 0x17: case 0x31: /* race below 7, not holding 0x8e, flags 2, not holding 0x34 */
        hit4 = race_low && !holds(t, 0x8e) && (flags & 2) != 0 && !holds(t, 0x34);
        break;
    case 0x24: /* Heal: hit points below the maximum, or holding 0xb9 */
        hit4 = hit_max > hit || holds(t, 0xb9);
        break;
    case 0x25: /* holding 0x21 */
        hit4 = holds(t, 0x21);
        break;
    case 0x2a: { /* Prayer: the selected record not holding 0x31 */
        cok_character *sel = member_at(game, game->vm.character);
        if (sel == NULL)
            return undefined_fight(game, "the selected record of Prayer's test is read through "
                                         "NULL (52db:42cc)");
        hit4 = !holds(sel, 0x31);
        break;
    }
    case 0x2b: /* holding 0x24 */
        hit4 = holds(t, 0x24);
        break;
    case 0x2f: case 0x4a: case 0x73: case 0x85: /* not holding 0x5d */
        hit4 = !holds(t, 0x5d);
        break;
    case 0x33: /* not holding 6 */
        hit4 = !holds(t, 6);
        break;
    case 0x44: /* flags 0x10 clear, not holding 0x8f */
        hit4 = (flags & 0x10) == 0 && !holds(t, 0x8f);
        break;
    case 0x4c: case 0x6e: /* flags 8 clear */
        hit4 = (flags & 8) == 0;
        break;
    case 0x51: case 0x78: /* flags 2 */
        hit4 = (flags & 2) != 0;
        break;
    case 0x52: /* not holding 0x8a */
        hit4 = !holds(t, 0x8a);
        break;
    case 0x54: /* not holding 0x5c */
        hit4 = !holds(t, 0x5c);
        break;
    case 0x5b: /* highest level below 6 */
        hit4 = highest < 6;
        break;
    case 0x5e: /* flags 2, not holding 0x8e */
        hit4 = (flags & 2) != 0 && !holds(t, 0x8e);
        break;
    case 0x60: /* race below 7, or flags7 2 */
        hit4 = race_low || (flags7 & 2) != 0;
        break;
    case 0x82: /* not holding 0x8e */
        hit4 = !holds(t, 0x8e);
        break;
    default: { /* the spell's effect: none, or t not holding it */
        uint8_t effect = queen_spell_byte(game, spell, SPELL_EFFECT);
        hit4 = effect == 0 || !holds(t, effect);
        break;
    }
    }
    int v = hit4 ? 4 : 0;
    if (v > 0) {
        uint8_t level = queen_spell_byte(game, spell, SPELL_LEVEL);
        if (level < 5 && holds(t, 0x39)) v = 0;
        if (level < 4 && holds(t, 0x3f)) v = 0;
        uint8_t res = cok_field_u8(L, r, COK_FIELD_MAGIC_RESISTANCE);
        if (res > 0x28) v = res > 0x82 ? 0 : res > 0x64 ? 1 : res > 0x46 ? 2 : 3;
    }
    *worth = (int8_t)v;
    return true;
}

/* 52db:4789 (c, spell): the worth of spell cast by c, a signed byte (part 29). The side is c's own
 * word +0x66 where the spell's allies word (+0x13) is 0, else the other side (51c2:08fd). A spell
 * of kind 0 (+7 0) is worth queen_target_worth to c alone; else the combatants listed around c
 * within the spell's range (849a:0f34, cok_combat_list with 0xff and c's size), nearest first, the
 * first with a worth above 0 (0 for none). */
bool queen_spell_worth(cok_adventure *game, cok_character *c, uint8_t spell, int8_t *worth)
{
    const cok_rules *rules = cok_game_rules(game);
    cok_combat *combat = &game->combat;
    unsigned hostile = cok_combat_field_word(rules, c->record, COK_FIELD_HOSTILE);
    unsigned side = queen_spell_word(game, spell, SPELL_ALLIES) == 0 ? hostile
                                                                     : (hostile == 0 ? 1 : 0);
    *worth = 0;
    if (queen_spell_byte(game, spell, SPELL_TARGETING) == 0)
        return queen_target_worth(game, c, spell, side, worth);
    uint8_t range = 0;
    if (!queen_spell_range(game, spell, &range)) return false;
    if (!cok_combat_list(combat, rules, cok_combat_x(combat, c->record),
                         cok_combat_y(combat, c->record), range, 0xff, cok_combat_size(combat, c->record)))
        return undefined_fight(game, "the list of combatants around the caster (52db:4789, 5837:0b19) "
                                     "reads past its tables");
    for (unsigned k = 1; k <= combat->listed_count && *worth <= 0; ++k) {
        cok_character *e = combat->combatant[combat->listed[k].index].character;
        if (e == NULL)
            return undefined_fight(game, "a combatant listed has no record, read through NULL "
                                         "(52db:4789)");
        if (!queen_target_worth(game, e, spell, side, worth)) return false;
    }
    return true;
}

/* 5bb8:1a07 (list, c): the spells c has memorised (+0xce, the 0x89 bytes; a byte 0, or 0x80 or more,
 * is none) into list, sorted by priority (+0x12), highest first: each goes in before the first
 * entry (0 for the end) whose priority is not above its own, so equal priorities put the later spell
 * first, and the rest move up. Camp-only spells (word +0x0f 0) are not listed; duplicates are. */
static void sort_spells(cok_adventure *game, const cok_character *c, uint16_t list[MEMORIZED_COUNT])
{
    for (unsigned k = 0; k <= MEMORIZED_LAST; ++k) {
        uint8_t s = c->record[MEMORIZED_AT + k];
        if (s == 0 || s >= 0x80) continue;
        if (queen_spell_word(game, s, SPELL_CASTABLE) == 0) continue;
        unsigned j = 0;
        while (list[j] != 0 && queen_spell_byte(game, list[j], SPELL_PRIORITY) >
                                   queen_spell_byte(game, s, SPELL_PRIORITY))
            ++j;
        /* The search stops at the first empty slot or entry not above s: the nonzero entries are a
         * prefix of the list, and there are at most 0x88 of them before an insert, so j stays in it. */
        uint16_t carry = s;
        for (;;) {
            uint16_t moved = list[j];
            list[j] = carry;
            carry = moved;
            ++j;
            if (carry == 0 || j > MEMORIZED_LAST) break;
        }
    }
}

/* 5bb8:19b0 (the walk of 1b1e): from *index, the next candidate of the sorted list into *spell: an
 * empty slot ends the walk with 0; an entry other than *spell is the candidate (the index stays);
 * one equal to *spell moves the index on. Past the list's last slot the original reads DS:aca5, which
 * is undefined here. */
static bool next_candidate(cok_adventure *game, const uint16_t *list, unsigned *index, uint8_t *spell)
{
    for (;;) {
        if (*index > MEMORIZED_LAST)
            return undefined_fight(game, "the spell walk reads past the spell list (DS:aca5, "
                                         "5bb8:19b0)");
        uint16_t entry = list[*index];
        if (entry == 0) {
            *spell = 0;
            return true;
        }
        if (entry != *spell) {
            *spell = (uint8_t)entry;
            return true;
        }
        ++*index;
    }
}

/* 5bb8:1b1e (c, *spell): the computer's spell, the first of the sorted list (sort_spells) that
 * queen_spell_worth gives above 0, walking past each one not worth it (next_candidate); 0 for none.
 * False where the run stopped. */
bool queen_best_spell(cok_adventure *game, cok_character *c, uint8_t *spell)
{
    uint16_t list[MEMORIZED_COUNT] = {0};
    unsigned index = 0;
    uint8_t candidate = 0;
    sort_spells(game, c, list);
    *spell = 0;
    for (;;) {
        if (!next_candidate(game, list, &index, &candidate)) return false;
        if (candidate == 0) return true;
        int8_t worth = 0;
        if (!queen_spell_worth(game, c, candidate, &worth)) return false;
        if (worth > 0) {
            *spell = candidate;
            return true;
        }
    }
}

/* 5bb8:0306 (queen_spell_choice): the computer's spell choice (part 29). It casts nothing when it is
 * fleeing, when var COK_VAR_NO_MAGIC is set, when it may not cast, or when it is a monster (+0x9b
 * below 0x7f) and Magic is off (*cast false, true). Else its spell is queen_best_spell's: none, no
 * draw. With one, p is the four class levels (2, 3, 4, 1) summed as bytes and shifted two (a byte); a
 * d100 at or below p casts nothing (one draw), and a d5 above the spell's priority (+0x12) casts
 * nothing (two draws). Else 52db:2543 (queen_cast_command, redrawn): *cast is its done, which ends the
 * turn. The caster is the selected record (vm.character, the turn's). False where the run stopped. */
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
    uint8_t spell = 0;
    if (!queen_best_spell(game, c, &spell)) return false;
    if (spell == 0) return true;
    uint8_t levels = 0;
    static const unsigned classes[4] = {2, 3, 4, 1}; /* 5bb8:0306's 177e:2234 calls, in order */
    for (unsigned i = 0; i < 4; ++i)
        levels = (uint8_t)(levels + cok_field_u8_at(L, c->record, COK_FIELD_CLASS_LEVEL, classes[i]));
    uint8_t p = (uint8_t)(levels << 2);
    if (cok_dice(&game->vm.seed, 1, 100) <= p) return true;
    if (cok_dice(&game->vm.seed, 1, 5) > queen_spell_byte(game, spell, SPELL_PRIORITY)) return true;
    bool done = false;
    if (!queen_cast_command(game, c, spell, true, &done)) return false;
    *cast = done;
    return true;
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
 * the port's use_item must not run here). A pending spell's cast (0170: queen_cast with the computer's
 * ai, the spell kept until the turn's end) comes first; then
 * turn undead (0268, turn_undead: found, queen_turn_undead turns them and the turn ends), the spell
 * choice (0306, queen_spell_choice; a cast ends the turn), and the weapon's choice (1396,
 * queen_choose_weapon), then the keys again (1050: over ends the turn). Then the loop of step 14
 * (0209: its pick, 52db:3d9e, and the fight, 0b3d, queen_fight).
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
        /* 5bb8:0170: the pending cast with the spell still kept (0c11 clears it after). The caster
         * is the selected record (DS:ad67): the turn has set it (round.c:941); a Quick turn has not,
         * so it is set for the cast alone and put back. */
        uint8_t *selected = game->vm.character;
        bool done = false;
        game->vm.character = r;
        bool ok = queen_cast(game, cr->spell, true, true, &done);
        game->vm.character = selected;
        if (!ok) return false;
        if (game->vm.abort) return true;
        cok_combat_end_turn(c);
        return true;
    }
    bool turned = false;
    if (!turn_undead(game, c, &turned)) return false;
    if (turned) {
        cok_combat_end_turn(c); /* 5bb8:01b8: 0c11, then the turn is over */
        return true;
    }
    /* The choice's caster is the selected record too (see the pending cast above). */
    uint8_t *selected = game->vm.character;
    bool cast = false;
    game->vm.character = r;
    bool ok = queen_spell_choice(game, c, &cast);
    game->vm.character = selected;
    if (!ok) return false;
    if (cast) return true;
    if (!queen_choose_weapon(game, c, cr->target)) return false;
    if (!cok_ai_keys(game, c, &over)) return false; /* 5bb8:1050 again: over ends the turn */
    if (over) return true;
    /* 5bb8:0209-0262, step 14: while not over, pick a target (52db:3d9e with 0xff, 1, 0, 0); one
     * found, with initiative above 0 and +0x18b, fights (queen_fight, 0b3d) and is over when it
     * says so; else it guards and is over. Each pass but the last fells a target or readies
     * another weapon, as ai.c's loop: past 2 * COK_COMBATANTS + 8 passes the original loops for
     * ever (the bound is the port's). */
    for (unsigned passes = 0; !over; ++passes) {
        if (passes > 2 * COK_COMBATANTS + 8)
            return undefined_fight(game, "the computer's turn never ends (5bb8:0209), where the "
                                         "original loops for ever");
        bool picked = false;
        if (!cok_combat_pick_target(game, c, 0xff, true, false, &picked)) return false;
        if (picked && cr->initiative > 0 && cok_field_u8(L, r, COK_FIELD_ACTIVE) != 0) {
            if (!queen_fight(game, c, &over)) return false;
        } else {
            if (!cok_ai_guard(game, c)) return false; /* 5bb8:0fec */
            over = true;
        }
        if (game->vm.abort) break;
    }
    return true;
}
