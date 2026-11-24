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
 * high byte cleared), active 0, hit points 0, its battle effects are removed (cok_combat_battle_only), then, while it is
 * inactive and its status word not 10 (read again: the removals run their handlers), the arena kill (its stale byte as
 * queen_cast's kill: the berserk node); the pause, and in mode 5 the text cleared. This is cok_combat_kill without event 0x0d and
 * the party list. */
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
    /* 43c6-43d7: inactive and status word not 10, read again after 1b66 (whose removals run their handlers) */
    if (cok_field_u8(R->character, record, COK_FIELD_ACTIVE) == 0 && cok_combat_field_word(R, record, COK_FIELD_STATUS) != 10) {
        int stale = cok_character_find_named(c, R, COK_EFFECT_BERSERK) != NULL ? COK_ARENA_STALE_UNKNOWN : 0;
        cok_arena_reachable also = cok_cast_reachable(game);
        if (!cok_arena_kill(game, c, stale, &also)) return false;
    }
    cok_arena_pause(game);
    if (game->vm.mode == 5) cok_arena_clear_text(game);
    return true;
}

/* d4: Prot. from Fire 0x6d (301a), PW Stun 0x75 (3569), PW Blind 0x7b (396a) */

/* The ids and the offsets of band d4: the ranger's class level is the one at +0xa5 + 4 (177e:2234); PW Stun's effect and
 * save kind are read from its own record (+0xe byte, +0xa word), as the original does (DS:3205, DS:3201). */
enum {
    SPELL_SAVE_KIND = 0x0a,
    SPELL_EFFECT = 0x0e,
    SPELL_PROT_FIRE = 0x6d,
    SPELL_PW_STUN = 0x75,
    SPELL_PW_BLIND = 0x7b,
    EFFECT_RESIST = 0x14,
    EFFECT_FIRE_ABSORB = 0x77,
    EFFECT_BLIND = 0x21,
    CLASS_RANGER = 4,
    BLIND_LIMIT = 100,
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

/* 7fd8:3569, PW Stun (0x75): one image. The first target t1 (queen_first_target's stop). Its rounds come from its current hit
 * points (+0x198): 1 to 30 is 4d4, 31 to 60 is 2d4, 61 to 90 is 1d4, and any other (0, 91 and up) is none, with no draw (a
 * jump table, 3597). No rounds: nothing, no text. Else effect 0x6a (the held row: the holder's turn ends, 78e4:1af9) is added
 * for the rounds, its value the caster's level capped, its kind the record's save kind, unsaved, "is STUNNED". The id and the
 * kind are 0x75's own record (+0xe, +0xa), the level is 0x75's. */
static bool pw_stun(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    game->effects.rolls.images = 1;
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    uint8_t hp = cok_field_u8(L, t->record, COK_FIELD_HIT_POINTS);
    uint8_t rounds = 0;
    if (hp >= 1 && hp <= 30) rounds = cok_dice(&game->vm.seed, 4, 4);
    else if (hp >= 31 && hp <= 60) rounds = cok_dice(&game->vm.seed, 2, 4);
    else if (hp >= 61 && hp <= 90) rounds = cok_dice(&game->vm.seed, 1, 4);
    if (rounds == 0) return true;
    uint8_t lv = 0;
    if (!queen_spell_level(game, SPELL_PW_STUN, true, &lv)) return false;
    uint8_t effect = queen_spell_byte(game, SPELL_PW_STUN, SPELL_EFFECT);
    uint8_t kind = (uint8_t)queen_spell_word(game, SPELL_PW_STUN, SPELL_SAVE_KIND);
    return cok_cast_add_effect(game, t, effect, rounds, lv, false, kind, false, "is STUNNED");
}

/* One walk of PW Blind (7fd8:396a): the targets t[1..count], read again at each pass while the running sum is at most 100.
 * A target whose maximum hit points (+0x89) are over 100, or would take the sum over 100, is skipped; else the sum takes its
 * maximum, and the second walk (add) gives the target effect 0x21 ("is blinded") for the spell's duration (101b: none). A NULL
 * target is read through NULL in either walk: undefined (3994). */
static bool blind_walk(cok_adventure *game, bool add)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    unsigned sum = 0;
    for (unsigned k = 1; k <= game->spell_target_count && sum <= BLIND_LIMIT; ++k) {
        cok_character *t = game->spell_targets[k];
        if (t == NULL) return undefined(game, "dqk 7fd8:3994 reads a NULL target's maximum hit points (DS:c5df)");
        unsigned max = cok_field_u8(L, t->record, COK_FIELD_MAX_HIT_POINTS);
        if (max > BLIND_LIMIT || max + sum > BLIND_LIMIT) continue;
        sum += max;
        if (!add) continue;
        uint16_t minutes = 0;
        if (!queen_spell_duration(game, game->effects.rolls.spell, &minutes)) return false;
        if (!cok_cast_add_effect(game, t, EFFECT_BLIND, minutes, 0, false, 0, false, "is blinded")) return false;
    }
    return true;
}

/* 7fd8:396a, PW Blind (0x7b): one image; the first walk (the sum only), then one d4 drawn and never read (396a's 1d4 + 1,
 * times 10 under a sum of 51, in -3(bp), dead: the draw is kept), then the second walk, which adds the blindness. */
static bool pw_blind(cok_adventure *game)
{
    game->effects.rolls.images = 1;
    if (!blind_walk(game, false)) return false;
    (void)cok_dice(&game->vm.seed, 1, 4);
    return blind_walk(game, true);
}

/* d5: Mass Charm 0x78 (372b), Otto's 0x79 (388c) */

enum {
    SPELL_MASS_CHARM = 0x78,
    SPELL_OTTO = 0x79,
    EFFECT_CHARMED = 0x0b,
    EFFECT_HELD_DANCE = 0x1b,
    EFFECT_DANCE = 0x76,
    EFFECT_96 = 0x96,
    CHARM_SAVE_TYPE = 4,
    CHARM_SAVE_BONUS = 0xfe,
    CHARM_KIND = 1,
    CHARM_FLAGS_BYTE = 1,
    CHARM_FLAG = 2,
    CHARM_CAP_SHIFT = 1,
};

/* The charm run on t (band c's run_charmed, copied: its 3 lines): effect 0x0b's handler on the holder's node, if t
 * holds it. */
static bool charm_run(cok_adventure *game, cok_character *t)
{
    cok_effect *node = cok_character_find_effect(t, EFFECT_CHARMED);
    if (node == NULL) return true;
    return cok_effects_run(&game->effects, t, EFFECT_CHARMED, node) || cok_adventure_effect_failed(game);
}

/* 7fd8:372b, Mass Charm (0x78, an area spell): one image. The cap is the spell's level uncapped,
 * doubled (a byte). The targets in list order, the count re-read at each pass, while the sum of their highest levels is
 * short of the cap: a NULL target is passed over. A target whose creature flags' second byte has no flag 2 (+0xc8, not
 * byte 0), or that is on the caster's side (the words at +0x66), is "is unaffected" (row 10, waited, one draw). A target
 * whose highest level (+0x91, unsigned) would take the sum over the cap is passed over with no text, and a later smaller
 * one may still fit. Else the sum takes it, the walk ends when the sum is the cap, and the save (type 4, bonus -2) is made
 * for the add: effect 0x0b for no minutes (the spell's duration), its value the caster's level (capped) with the caster's
 * side in bit 7, on remove, kind 1, "is charmed" (a saved target has no effect: cok_cast_add_effect's "is Unaffected").
 * A target holding 0x0b after the add is run (the charm). The caster is read (DS:ad67) only where the original reads it:
 * the side test and the value; NULL there is undefined. */
static bool mass_charm(cok_adventure *game)
{
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    uint8_t spell = game->effects.rolls.spell;
    game->effects.rolls.images = 1;
    uint8_t lv = 0;
    if (!queen_spell_level(game, spell, false, &lv)) return false;
    uint8_t cap = (uint8_t)(lv << CHARM_CAP_SHIFT);
    uint8_t sum = 0;
    bool done = false;
    for (unsigned k = 1; k <= game->spell_target_count && !done; ++k) {
        cok_character *t = game->spell_targets[k];
        if (t == NULL) continue;
        bool charmable =
            (cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, CHARM_FLAGS_BYTE) & CHARM_FLAG) != 0;
        if (charmable) {
            uint8_t *caster = caster_record(game, "the caster is read through NULL (DS:ad67, 372b)");
            if (caster == NULL) return false;
            charmable = cok_combat_field_word(R, t->record, COK_FIELD_HOSTILE) !=
                        cok_combat_field_word(R, caster, COK_FIELD_HOSTILE);
        }
        if (!charmable) {
            if (!cok_arena_say(game, t, "is unaffected", 10, true)) return false;
            continue;
        }
        uint8_t hd = cok_field_u8(L, t->record, COK_FIELD_HIGHEST_LEVEL);
        if ((unsigned)hd + sum > cap) continue;
        sum = (uint8_t)(sum + hd);
        done = sum == cap;
        bool made = false;
        if (!queen_save(game, t, CHARM_SAVE_TYPE, CHARM_SAVE_BONUS, &made)) return false;
        uint8_t lvc = 0;
        if (!queen_spell_level(game, spell, true, &lvc)) return false;
        uint8_t *caster = caster_record(game, "the caster is read through NULL (DS:ad67, 372b)");
        if (caster == NULL) return false;
        uint8_t value = (uint8_t)(lvc + (uint8_t)(cok_field_u8(L, caster, COK_FIELD_HOSTILE) << 7));
        uint16_t minutes = 0;
        if (!queen_spell_duration(game, spell, &minutes)) return false;
        if (!cok_cast_add_effect(game, t, EFFECT_CHARMED, minutes, value, true, CHARM_KIND, made, "is charmed"))
            return false;
        if (!charm_run(game, t)) return false;
    }
    return true;
}

/* 7fd8:388c, Otto's Irresistible Dance (0x79, a touch): the first target t1 (queen_first_target's stop). A holder of 0x96 is passed over (no draw, no text). Else queen_touch on a LOCAL copy of t1 (the stats recomputed,
 * event 0x0b on the copy, which a redirect may change, then the hit roll against the copy's armour class): a miss adds
 * nothing. A hit adds 0x1b (its held handler, no text, not made) for a first dur(0x79) (1d4 + 1), then 0x76 "begins to dance
 * wildly" for a second dur(0x79) (its own draw), both valued at the caster's level (capped) and both on the copy as the
 * redirect left it. No save. */
static bool otto(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    if (cok_character_find_effect(t, EFFECT_96) != NULL) return true;
    bool hit = false;
    if (!queen_touch(game, &t, &hit)) return false;
    if (!hit) return true;
    uint8_t lv = 0;
    if (!queen_spell_level(game, SPELL_OTTO, true, &lv)) return false;
    uint16_t minutes = 0;
    if (!queen_spell_duration(game, SPELL_OTTO, &minutes)) return false;
    if (!cok_cast_add_effect(game, t, EFFECT_HELD_DANCE, minutes, lv, false, 0, false, "")) return false;
    if (!queen_spell_duration(game, SPELL_OTTO, &minutes)) return false;
    return cok_cast_add_effect(game, t, EFFECT_DANCE, minutes, lv, false, 0, false, "begins to dance wildly");
}

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
    case SPELL_PROT_FIRE: return prot_fire;
    case SPELL_PW_STUN: return pw_stun;
    case SPELL_PW_BLIND: return pw_blind;

    /* d5: Mass Charm 0x78 (372b), Otto's 0x79 (388c) */
    case SPELL_MASS_CHARM: return mass_charm;
    case SPELL_OTTO: return otto;
    /* d6: Monster Summoning 0x7e (3fa4), Stone to Flesh 0x71 (3297) */

    default: return NULL;
    }
}
