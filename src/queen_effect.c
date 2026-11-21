#include "dark_queen.h"

#include "queen.h"

/* The Dark Queen of Krynn's own effect routines (dqk 78e4 and 7e76), the handlers the descriptor's
 * table maps to the kinds of spells.h's COK_FX_QUEEN_*: effect.c has no handler for them, so it runs
 * them through the game's own hook (effect.h's own), which is cok_dark_queen_effect here. Each case is
 * one routine, cited by its address in the descriptor's row (rules_dark_queen_magic.c), with the
 * original's behaviour read from its disassembly. The damage is the word rolls.amount16 (DS:ae55), with
 * its byte rolls.amount kept as the handlers read it (set_damage). */

/* 78e4:00a1 (id): the cancel. Id 0, or the pending effect is id: the damage (the word and its byte) and
 * the pending effect are cleared. */
static void cancel(cok_adventure *game, uint8_t id)
{
    cok_rolls *r = &game->effects.rolls;
    if (id == 0 || r->pending == id) {
        r->amount16 = 0;
        r->amount = 0;
        r->pending = 0;
    }
}

/* The damage word and its byte together (the writes of DS:ae55 and of the byte the handlers read). */
static void set_damage(cok_adventure *game, int16_t v)
{
    game->effects.rolls.amount16 = v;
    game->effects.rolls.amount = (uint8_t)v;
}

/* The level of the spell being cast (DS:27e9 + 0x16 * spell + 2). */
static uint8_t spell_level(cok_adventure *game)
{
    return queen_spell_byte(game, game->effects.rolls.spell, 2);
}

/* 148f:19c2 (c, id, minutes, value, flag): the effect added. An id of 0 adds nothing. Out of memory
 * fails. The effects' pool (effect.c's add, its full stop) is not checked here: the callers that need
 * it come with their own parts. */
static bool __attribute__((unused)) add_raw(cok_adventure *game, cok_character *c, uint8_t id,
                                            uint16_t minutes, uint8_t value, bool on_remove)
{
    if (id == 0) return true;
    if (cok_character_add_effect(c, id, minutes, value, on_remove) != NULL) return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory adding effect 0x%02x", id);
    return false;
}

/* 78e4:00bf (c, id, value, minutes), "again": while the curing flag (DS:b184) is set, nothing is added
 * (*added false); otherwise the effect is added with flag 1 (add_raw). */
static bool __attribute__((unused)) again(cok_adventure *game, cok_character *c, uint8_t id,
                                          uint8_t value, uint16_t minutes, bool *added)
{
    *added = false;
    if (game->effects.rolls.curing != 0) return true;
    if (!add_raw(game, c, id, minutes, value, true)) return false;
    *added = true;
    return true;
}

/* c's combat record: NULL outside a battle, where the 7e76 routines return at once (c->combat NULL).
 * Unused until effects-4. */
static __attribute__((unused)) cok_combat_record *block(cok_character *c)
{
    return c->combat;
}

bool cok_dark_queen_effect(cok_adventure *game, uint8_t kind, bool removing, cok_effect *effect,
                           cok_character *c)
{
    cok_effects *fx = &game->effects;
    const cok_rules_effect *h = &cok_game_rules(game)->effects.handlers[fx->running];
    cok_rolls *r = &fx->rolls;
    const cok_character_layout *L = cok_game_rules(game)->character;
    (void)removing, (void)effect;
    switch (kind) {
    /* effects-2: damage and rolls */
    case COK_FX_QUEEN_TYPE_CANCEL:
        /* 78e4:0184 -> 00f7(4): the damage of the entry's type (DS:bebd & the value) cancelled. */
        if ((r->damage_type & h->value) != 0) cancel(game, 0);
        return true;
    case COK_FX_QUEEN_RESIST: {
        /* 78e4:01f4 (0x0a, value 2, cold) and 0313 (0x14, value 1, fire): the damage of the type
         * halved as a signed word (idiv), and the save 3 better. */
        if ((r->damage_type & h->value) != 0) {
            set_damage(game, (int16_t)(r->amount16 / 2));
            r->save_roll = (uint8_t)(r->save_roll + 3);
        }
        return true;
    }
    case COK_FX_QUEEN_BARKSKIN:
        /* 78e4:021d (0x0d): the save 1 better unless the damage is magic (type 8); the attack roll
         * 1 worse, always. */
        if ((r->damage_type & 8) == 0) r->save_roll = (uint8_t)(r->save_roll + 1);
        r->attack_roll = (uint8_t)(r->attack_roll - 1);
        return true;
    case COK_FX_QUEEN_SHIELD:
        /* 78e4:02b9 (0x11): the holder's AC raised to at least 0x39 (+0x18e, unsigned), the save 1
         * better, and Magic Missile's (spell 0x0f) damage word zeroed (the pending effect kept). */
        if (cok_field_u8(L, c->record, COK_FIELD_AC) < 0x39) cok_field_set8(L, c->record, COK_FIELD_AC, 0x39);
        r->save_roll = (uint8_t)(r->save_roll + 1);
        if (r->spell == 0x0f) set_damage(game, 0);
        return true;
    case COK_FX_QUEEN_QUARTER_LESS:
        /* 78e4:0654 (0x1d): the damage a quarter less (the quarter as a signed word: idiv). */
        set_damage(game, (int16_t)(r->amount16 - r->amount16 / 4));
        return true;
    case COK_FX_QUEEN_GLOBE:
        /* 78e4:073b (0x28): a spell of level below 8 (the byte, unsigned) cancelled, if one is cast. */
        if (r->spell != 0 && spell_level(game) < 8) cancel(game, 0);
        return true;
    case COK_FX_QUEEN_GLOBE_MAGIC:
        /* 78e4:0b11 (0x39): a spell of level 4 or below (the byte, zero-extended and compared as
         * a signed word: 0 to 4) with the magic bit (type 8) cancelled. */
        if (r->spell > 0 && spell_level(game) <= 4 && (r->damage_type & 8) != 0) cancel(game, 0);
        return true;
    case COK_FX_QUEEN_RESISTANT: {
        /* 78e4:0a22 (0x32, value 2: the damage of cold is saved; fire, the other, doubled when not
         * saved) and 0a7e (0x36, value 1: the mirror). A saved type takes 2 from the save, and its
         * damage is zeroed when the save was made, else halved. */
        unsigned other = 3 - (unsigned)h->value;
        if ((r->damage_type & h->value) != 0) {
            r->save_roll = (uint8_t)(r->save_roll + 2);
            if (r->save_made != 0) set_damage(game, 0);
            else set_damage(game, (int16_t)(r->amount16 / 2));
        } else if ((r->damage_type & other) != 0 && r->save_made == 0) {
            set_damage(game, (int16_t)(r->amount16 * 2));
        }
        return true;
    }
    /* effects-3: images, haste, slow, prayer, missiles, 0x38 */
    /* effects-4: the 7e76 four */
    /* effects-5: timers */
    default:
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "effect kind %u (dqk %04x) has no Dark Queen routine", kind, h->address);
        return false;
    }
}
