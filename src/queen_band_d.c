#include "queen.h"

#include "arena.h"
#include "attack.h"
#include "cast.h"
#include "combat.h"
#include "ecl.h"
#include "effect.h"
#include "field.h"
#include "party.h"

/* The Dark Queen of Krynn's band d spells (part 33): the 7fd8 handlers from 2cae on, as the briefs add them (d1 the kills,
 * then d3 to d6). Each handler reads the spell's record and the targets as queen_cast.c does, and shares the spell
 * engine's routines (queen_save, queen_cast_damage, the run of an effect's handler). queen_band_d_handler is the table: a
 * spell with no handler here is NULL, and queen_spell_handler then takes the plain table or stops at the descriptor's
 * citation. */

/* An undefined stop (the original reads unset memory, a NULL or a freed record): cok_adventure_fail. */
static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* The caster's record (DS:ad67): none is read through NULL, undefined. */
static uint8_t *caster_record(cok_adventure *game, const char *what)
{
    if (game->vm.character == NULL) undefined(game, what);
    return game->vm.character;
}

/* The character of a combatant's record (NULL for none, or a record not in the combat). */
static cok_character *combatant_of(cok_adventure *game, const uint8_t *record)
{
    if (record == NULL) return NULL;
    uint8_t n = cok_combat_index(&game->combat, record);
    return n == 0 ? NULL : game->combat.combatant[n].character;
}

/* The ids and the effect and damage types of band d1. Effect 0x65 is the level drain (7e76:093b, a cited row: part 38);
 * the damage types 0x48 (Destruction, Disintegrate) and 0x40 (Flesh to Stone) are the spells' own. */
enum {
    SPELL_ENERGY_DRAIN = 0x67,
    SPELL_DESTRUCTION = 0x68,
    SPELL_DISINTEGRATE = 0x6f,
    SPELL_FLESH_TO_STONE = 0x72,
    SPELL_ITEM_POWER = 0x8a,
    EFFECT_DRAIN_LEVEL = 0x65,
    DAMAGE_KILL = 0x48,
    DAMAGE_STONE = 0x40,
    KILL_AMOUNT = 0xff,
    SAVE_TYPE_KILL = 4,
    STATUS_DESTROYED = 8,
    STATUS_KILLED = 6,
    STATUS_STONE = 7,
};

/* d1: the kills (7fd8:2ebc, 2eee, 31cc, 344d, 41e7) */

/* 7fd8:2ebc, Energy Drain (0x67; computer only): the caster's combat target (block +0x0b) is set to the first target's
 * record, NULL allowed, then effect 0x65's handler runs on the caster with no node (7e76:093b: a row cited, part 38, so the
 * cast stops there). A caster with no record or no combat record is read through NULL: undefined. The target is stored as
 * it is; a first target freed since (&cok_arena_freed) is read by 093b, where the stop is. */
static bool energy_drain(cok_adventure *game)
{
    uint8_t *caster = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 2ebc)");
    if (caster == NULL) return false;
    cok_character *c = combatant_of(game, caster);
    if (c == NULL || c->combat == NULL)
        return undefined(game, "dqk 7fd8:2ec7 writes the target through NULL (DS:ad67 +0x40)");
    cok_character *t1 = game->spell_targets[1];
    c->combat->target = t1 == NULL ? NULL : t1->record;
    return cok_effects_run(&game->effects, c, EFFECT_DRAIN_LEVEL, NULL) || cok_adventure_effect_failed(game);
}

/* The kill's first step (2eee, 31cc, 344d): the damage type, the amount 0xff (the word and its byte) and event 9 on the
 * local copy t (a redirect by a handler changes t, not the slot). A NULL t after is read through NULL: undefined, cited.
 * False where the run stopped. */
static bool kill_event(cok_adventure *game, cok_character **t, uint8_t damage_type, const char *stop)
{
    cok_rolls *r = &game->effects.rolls;
    r->damage_type = damage_type;
    r->amount16 = KILL_AMOUNT;
    r->amount = KILL_AMOUNT;
    if (!cok_effects_dispatch_at(&game->effects, t, 9)) return cok_adventure_effect_failed(game);
    if (*t == NULL) return undefined(game, stop);
    return true;
}

/* 7fd8:2eee, Destruction (0x68; computer only): the first target t (a copy of the slot), damage type 0x48, event 9. A target
 * whose creature flags' second byte has the quiet bit (8: +0xc8, as Slay Living), or whose amount a handler cancelled to 0,
 * is "is unaffected" (row 10, waited, one draw); else the kill (cok_combat_kill, status 8, "is destroyed": no body). Unlike
 * Slay Living, the quiet holder gets "is unaffected", not nothing. */
static bool destruction(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    const cok_rules *R = cok_game_rules(game);
    if (!kill_event(game, &t, DAMAGE_KILL, "dqk 7fd8:2f1d reads +0xc8 through NULL")) return false;
    bool quiet = (cok_field_u8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1) & R->fight.quiet_flag) != 0;
    if (game->effects.rolls.amount16 == 0 || quiet) return cok_arena_say(game, t, "is unaffected", 10, true);
    return cok_combat_kill(game, t, STATUS_DESTROYED, "is destroyed");
}

/* 7fd8:31cc, Disintegrate (0x6f; the mage's spell, a player's cast too): the first target t, damage type 0x48,
 * event 9. A cancelled amount (0) is "is unaffected", as a save of type 4 (bonus 0, no roll for a 0 amount). Else the kill
 * (cok_combat_kill, status 6, "disintegrates"), then every item the target carries is removed, head first, readied ones
 * too (cok_character_remove_item: no unready, no stats). The removal runs also when the kill returned early (status 6-8). */
static bool disintegrate(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    if (!kill_event(game, &t, DAMAGE_KILL, "dqk 7fd8:320c reads the target through NULL")) return false;
    if (game->effects.rolls.amount16 == 0) return cok_arena_say(game, t, "is unaffected", 10, true);
    bool saved = false;
    if (!queen_save(game, t, SAVE_TYPE_KILL, 0, &saved)) return false;
    if (saved) return cok_arena_say(game, t, "is unaffected", 10, true);
    if (!cok_combat_kill(game, t, STATUS_KILLED, "disintegrates")) return false;
    while (t->item_count != 0) cok_character_remove_item(t, 0);
    return true;
}

/* 7fd8:344d, Flesh to Stone (0x72; the mage's spell, a player's cast too): the first target t, damage type 0x40, event 9.
 * A cancelled amount, or a save of type 4 (bonus 0), is silent. Else the stone (cok_combat_kill, status 7: the stone tile,
 * "is turned to stone"). Effect 0x41 (the stone's petrified node) is not added here: 7e76:069f is a monster's touch,
 * part 38. */
static bool flesh_to_stone(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    if (!kill_event(game, &t, DAMAGE_STONE, "dqk 7fd8:348d reads the target through NULL")) return false;
    if (game->effects.rolls.amount16 == 0) return true;
    bool saved = false;
    if (!queen_save(game, t, SAVE_TYPE_KILL, 0, &saved)) return false;
    if (saved) return true;
    return cok_combat_kill(game, t, STATUS_STONE, "is turned to stone");
}

/* 7fd8:41e7, the item power 0x8a (an item's or a monster's; computer and item use only): the caster's combat target, or
 * nothing when it has none (no draws). Two d6 are drawn: the first is the strength damage bonus's sum, which the second
 * damage overwrites, so its value is dead (177e:11f9 is not called: it draws nothing and only feeds that sum). The second
 * d6 plus 9 is the damage to the target through queen_cast_damage, with no save and the damage type left as it stands
 * (the stale one). The caster's record or combat record NULL is read through NULL: undefined (41eb). */
static bool item_power(cok_adventure *game)
{
    uint8_t *caster = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 41e7)");
    if (caster == NULL) return false;
    cok_character *c = combatant_of(game, caster);
    if (c == NULL || c->combat == NULL)
        return undefined(game, "dqk 7fd8:41eb reads the caster's combat record through NULL (DS:ad67 +0x40)");
    if (c->combat->target == NULL) return true;
    cok_character *t = combatant_of(game, c->combat->target);
    if (t == NULL) return undefined(game, "dqk 7fd8:41e7 reads the caster's target's combatant through NULL");
    (void)cok_dice(&game->vm.seed, 1, 6); /* 1968(1, 6): the first d6, its sum overwritten below (177e:11f9 is pure) */
    uint8_t d = cok_dice_count(&game->vm.seed, 1, 6, &game->effects.rolls.dice);
    return queen_cast_damage(game, t, (uint16_t)(d + 9), 0, false);
}

/* d3: Death Spell 0x6e (30ba), PW Kill 0x7d (3ea6), item powers 0x8b (4250) and 0x8c (4369) */

/* d4: Prot. from Fire 0x6d (301a), PW Stun 0x75 (3569), PW Blind 0x7b (396a) */

/* The ids and the class index of band d4: the ranger's class level is the one at +0xa5 + 4 (177e:2234). */
enum {
    SPELL_PROT_FIRE = 0x6d,
    EFFECT_RESIST = 0x14,
    EFFECT_FIRE_ABSORB = 0x77,
    CLASS_RANGER = 4,
};

/* 7fd8:301a, Prot. from Fire (0x6d): the first target t1 (no stop when NULL: the original tests it; a record freed since is
 * undefined, queen_first_target's). A target that is a ranger (its class level at +0xa9 above 0: 177e:2234 returns the byte, 0
 * for 0) takes effect 0x77, the fire absorb, valued at the caster's level for the spell uncapped, for no minutes. Any other
 * takes 0x14 (the resistance) for the spell's duration, value 0. Both "is Protected", unsaved. No draws. */
static bool prot_fire(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_character *t = game->spell_targets[1];
    if (t == NULL) return true;
    if (t == &cok_arena_freed)
        return undefined(game, "the first target is a record freed since, read where the heap may have put another "
                               "(DS:c5e3, 301a)");
    if (cok_field_u8_at(L, t->record, COK_FIELD_CLASS_LEVEL, CLASS_RANGER) > 0) {
        uint8_t lv = 0;
        if (!queen_spell_level(game, SPELL_PROT_FIRE, false, &lv)) return false;
        return cok_cast_add_effect(game, t, EFFECT_FIRE_ABSORB, 0, lv, false, 0, false, "is Protected");
    }
    uint16_t minutes = 0;
    if (!queen_spell_duration(game, SPELL_PROT_FIRE, &minutes)) return false;
    return cok_cast_add_effect(game, t, EFFECT_RESIST, minutes, 0, false, 0, false, "is Protected");
}

/* d5: Mass Charm 0x78 (372b), Otto's 0x79 (388c) */

/* d6: Monster Summoning 0x7e (3fa4), Stone to Flesh 0x71 (3297) */

queen_spell_fn *queen_band_d_handler(uint8_t id)
{
    switch (id) {
    /* d1: the kills (7fd8:2ebc, 2eee, 31cc, 344d, 41e7) */
    case SPELL_ENERGY_DRAIN: return energy_drain;
    case SPELL_DESTRUCTION: return destruction;
    case SPELL_DISINTEGRATE: return disintegrate;
    case SPELL_FLESH_TO_STONE: return flesh_to_stone;
    case SPELL_ITEM_POWER: return item_power;

    /* d3: Death Spell 0x6e (30ba), PW Kill 0x7d (3ea6), item powers 0x8b (4250) and 0x8c (4369) */
    /* d4: Prot. from Fire 0x6d (301a), PW Stun 0x75 (3569), PW Blind 0x7b (396a) */
    case SPELL_PROT_FIRE: return prot_fire;

    /* d5: Mass Charm 0x78 (372b), Otto's 0x79 (388c) */
    /* d6: Monster Summoning 0x7e (3fa4), Stone to Flesh 0x71 (3297) */

    default: return NULL;
    }
}
