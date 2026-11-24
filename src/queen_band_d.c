#include "queen.h"

#include "arena.h"
#include "attack.h"
#include "cast.h"
#include "combat.h"
#include "ecl.h"
#include "effect.h"
#include "field.h"
#include "party.h"
#include "round.h"

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

enum {
    SPELL_DEATH = 0x6e,
    SPELL_POWER_WORD_KILL = 0x7d,
    SPELL_STONE_GAZE = 0x8b,
    SPELL_STONE_SELF = 0x8c,
    DAMAGE_DEATH = 0x40,
    DEATH_AMOUNT = 100,
};

/* The party's character of a record (monsters are in the party list too), NULL when it is not one. */
static cok_character *party_character(cok_adventure *game, const uint8_t *record)
{
    size_t i = cok_party_index(&game->party, record);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* Event 9 on *t (the slot itself or a local copy, as the caller passes it) with amount in both bytes (DS:ae55); the damage type
 * is the caller's. False where the run stopped. */
static bool event_nine(cok_adventure *game, cok_character **t, uint8_t amount)
{
    cok_rolls *r = &game->effects.rolls;
    r->amount16 = amount;
    r->amount = amount;
    if (!cok_effects_dispatch_at(&game->effects, t, 9)) return cok_adventure_effect_failed(game);
    return true;
}

/* 7fd8:310e, Death Spell's weight by the record's highest level (+0x91, read as unsigned): 0 1, 1 2, 2-3 8, 4-5 16, 6 16 at
 * most hit points 51 or less else 20, 7-8 20, any other 162 (more than the pool's 160: never slain). */
static uint8_t death_weight(const cok_character_layout *L, const uint8_t *record)
{
    switch (cok_field_u8(L, record, COK_FIELD_HIGHEST_LEVEL)) {
    case 0: return 1;
    case 1: return 2;
    case 2:
    case 3: return 8;
    case 4:
    case 5: return 16;
    case 6: return cok_field_u8(L, record, COK_FIELD_MAX_HIT_POINTS) <= 0x33 ? 16 : 20;
    case 7:
    case 8: return 20;
    default: return 0xa2;
    }
}

/* 7fd8:30ba, Death Spell (0x6e; a mage's pending spell, area 1): the images flag is set and the damage type 0x40 (rolls.dice is
 * not written: cok_dice, no count). The pool is two 4d20 (cok_dice, 4 draws, times two). Each target in list order (the count
 * re-read): its weight is read from its record before event 9 (amount 100) on a local copy, and the event may redirect the copy.
 * Unless the quiet bit (+0xc8 & 8, after event 9) or a cancelled amount, the pool pays the weight when it holds it (unsigned
 * compare) and the target drops dead (cok_combat_kill, status 6, "drops dead"); else the slot is NULLed. The original's pool and
 * weight are scratch globals (DS:c7e4, c7e5): locals here. DS:c7e5, the last weight, is what Dispel's stale read takes (part 39);
 * nothing keeps it. A target NULL at the start is read through NULL (30f9); one NULL after event 9 too (316c). */
static bool death_spell(cok_adventure *game)
{
    const cok_rules *R = cok_game_rules(game);
    cok_rolls *r = &game->effects.rolls;
    r->images = 1;
    r->damage_type = DAMAGE_DEATH;
    uint8_t pool = (uint8_t)(cok_dice(&game->vm.seed, 4, 20) << 1);
    for (unsigned i = 1; i <= game->spell_target_count; ++i) {
        cok_character *t = game->spell_targets[i];
        if (t == NULL) return undefined(game, "dqk 7fd8:30f9 reads +0x91 through NULL (the target's record)");
        uint8_t weight = death_weight(R->character, t->record);
        if (!event_nine(game, &t, DEATH_AMOUNT)) return false;
        if (t == NULL) return undefined(game, "dqk 7fd8:316c reads the target through NULL after event 9");
        bool quiet = (cok_field_u8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1) & R->fight.quiet_flag) != 0;
        if (!quiet && pool >= weight && r->amount16 != 0) {
            pool = (uint8_t)(pool - weight);
            if (!cok_combat_kill(game, t, 6, "drops dead")) return false;
        } else {
            game->spell_targets[i] = NULL;
        }
    }
    return true;
}

/* 7fd8:3ea6, Power Word Kill (0x7d; a mage's immediate spell, area 1): the images flag is set; no target, nothing. The first
 * target, when its most hit points (+0x89: the maximum, not the current) are 11-60, is the only one: event 9 on the slot itself
 * with amount 0xff, and unless a handler cancelled it, "is slain" (cok_combat_kill, status 6) on the slot as event 9 left it.
 * Else every target of at most 10 most hit points, in list order: event 9 on its slot and the same kill. The damage type is not
 * set (the stale one stays), and nothing is drawn of its own. A slot NULL after event 9 with the amount left is read through
 * NULL (3f09, 3f89). */
static bool power_word_kill(cok_adventure *game)
{
    game->effects.rolls.images = 1;
    if (game->spell_target_count == 0) return true;
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_character **first = &game->spell_targets[1];
    if (*first != NULL) {
        uint8_t most = cok_field_u8(L, (*first)->record, COK_FIELD_MAX_HIT_POINTS);
        if (most > 10 && most <= 60) {
            if (!event_nine(game, first, 0xff)) return false;
            if (game->effects.rolls.amount16 == 0) return true;
            if (*first == NULL) return undefined(game, "dqk 7fd8:3f09 reads the target through NULL after event 9");
            return cok_combat_kill(game, *first, 6, "is slain");
        }
    }
    for (unsigned i = 1; i <= game->spell_target_count; ++i) {
        cok_character **slot = &game->spell_targets[i];
        if (*slot == NULL || cok_field_u8(L, (*slot)->record, COK_FIELD_MAX_HIT_POINTS) > 10) continue;
        if (!event_nine(game, slot, 0xff)) return false;
        if (game->effects.rolls.amount16 == 0) continue;
        if (*slot == NULL) return undefined(game, "dqk 7fd8:3f89 reads the target through NULL after event 9");
        if (!cok_combat_kill(game, *slot, 6, "is slain")) return false;
    }
    return true;
}

/* 7fd8:4250, the item power 0x8b (the stoning gaze; an item's Use only, part 50): rolls.target is the first target's record (a NULL
 * target is read through NULL, 4279). A holder of 0x48 reflects it: "reflects it!" on the target (row 12, no wait), the missile
 * from the target to the caster (4 frames, delay 0x2d), and the caster is the victim (rolls.target too). The victim saves (type 1,
 * bonus 0); a failed save of one holding neither 0x7e nor 0x4f is turned to stone (cok_combat_kill, status 7). The record's effect
 * 0x34 is not used, and nothing else is drawn. */
static bool stone_gaze(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    cok_character *t = game->spell_targets[1];
    if (t == NULL) return undefined(game, "dqk 7fd8:4279 reads the target through NULL (DS:c5e3)");
    r->target = t->record;
    if (cok_character_find_effect(t, 0x48) != NULL) {
        if (!cok_arena_say(game, t, "reflects it!", 12, false)) return false;
        uint8_t *record = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 4250)");
        cok_character *c = party_character(game, record);
        if (c == NULL) return undefined(game, "the caster is no party member (DS:ad67, 4250)");
        const cok_combat *combat = &game->combat;
        if (!cok_arena_missile(game, cok_combat_x(combat, t->record), cok_combat_y(combat, t->record),
                               cok_combat_x(combat, record), cok_combat_y(combat, record), 4, 0x2d))
            return false;
        t = c;
        r->target = record;
    }
    bool saved = false;
    if (!queen_save(game, t, 1, 0, &saved)) return false;
    if (saved || cok_character_find_effect(t, 0x7e) != NULL || cok_character_find_effect(t, 0x4f) != NULL) return true;
    return cok_combat_kill(game, t, 7, "is turned to stone");
}

/* 7fd8:4369, the item power 0x8c (the user turned to stone; readied in battle by 16f7, part 50): in mode 5 "is turned to stone" on
 * the caster (row 10, no wait). A caster of status 6-8 (dead, stoned or gone) is left as it is. Else its status word is 7 (the
 * high byte cleared), active 0, hit points 0, its battle effects are removed (cok_combat_battle_only), then the arena kill (its
 * stale byte as queen_cast's kill: the berserk node), the pause, and in mode 5 the text cleared. This is cok_combat_kill without
 * event 0x0d and the party list: the status is written just before, so the kill's test on active and status 10 always passes. */
static bool stone_self(cok_adventure *game)
{
    uint8_t *record = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 4369)");
    if (record == NULL) return false;
    cok_character *c = party_character(game, record);
    if (c == NULL) return undefined(game, "the caster is no party member (DS:ad67, 4369)");
    const cok_rules *R = cok_game_rules(game);
    if (game->vm.mode == 5 && !cok_arena_say(game, c, "is turned to stone", 10, false)) return false;
    unsigned status = cok_combat_field_word(R, record, COK_FIELD_STATUS);
    if (status >= 6 && status <= 8) return true;
    cok_combat_set_field_word(R, record, COK_FIELD_STATUS, 7);
    cok_field_set8(R->character, record, COK_FIELD_ACTIVE, 0);
    cok_field_set8(R->character, record, COK_FIELD_HIT_POINTS, 0);
    if (!cok_combat_battle_only(game, c)) return false;
    int stale = cok_character_find_named(c, R, COK_EFFECT_BERSERK) != NULL ? COK_ARENA_STALE_UNKNOWN : 0;
    cok_arena_reachable also = cok_cast_reachable(game);
    if (!cok_arena_kill(game, c, stale, &also)) return false;
    cok_arena_pause(game);
    if (game->vm.mode == 5) cok_arena_clear_text(game);
    return true;
}

/* d4: Prot. from Fire 0x6d (301a), PW Stun 0x75 (3569), PW Blind 0x7b (396a) */

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
    case SPELL_DEATH: return death_spell;
    case SPELL_POWER_WORD_KILL: return power_word_kill;
    case SPELL_STONE_GAZE: return stone_gaze;
    case SPELL_STONE_SELF: return stone_self;
    /* d4: Prot. from Fire 0x6d (301a), PW Stun 0x75 (3569), PW Blind 0x7b (396a) */
    /* d5: Mass Charm 0x78 (372b), Otto's 0x79 (388c) */
    /* d6: Monster Summoning 0x7e (3fa4), Stone to Flesh 0x71 (3297) */

    default: return NULL;
    }
}
