#include "queen.h"

#include "arena.h"
#include "attack.h"
#include "cast.h"
#include "combat.h"
#include "dark_queen.h"
#include "data.h"
#include "ecl.h"
#include "effect.h"
#include "field.h"
#include "party.h"
#include "round.h"

#include <stdio.h>
#include <string.h>

/* The Dark Queen of Krynn's spell engine (the spell's numbers, the flash's damage, CLW and MM): the
 * caster level (dqk 177e:1900), the range (849a:0f34), the duration (849a:101b), the apply of a
 * spell to its targets (849a:10f9), the damage (148f:2214), the healed text (177e:187a), Cure
 * Light Wounds (7fd8:00f8) and Magic Missile (7fd8:050a). The handler table is queen_spell_handler:
 * the spells ported here take precedence over rules_dark_queen_magic.c's unported entries.
 *
 * The spell records are DS:27e9 + 0x16 * id (data_dark_queen.c's table; the layout is the
 * original's, no COK_DATA_SPELLS shape). Every routine reads the selected character (vm.character,
 * DS:ad67) as the caster, the targets as spell_targets (DS:c5df, count DS:c6f4) and the spell being
 * cast as rolls.spell (DS:b2ea). Damage's type is rolls.damage_type (DS:bebd), a byte here: the
 * original's acid, 0x100 in a word, cannot be held, and no spell here has it. */

enum {
    SPELL_TABLE = 0x27e9, /* DS:27e9 */
    SPELL_RECORD = 0x16,
    SPELL_KIND = 0x00,         /* a word: 0 and 3 the cleric's, 1 the druid's, 2 the mage's, 4 fixed */
    SPELL_RANGE = 0x03,        /* the range's base; 0xff a touch */
    SPELL_RANGE_LEVEL = 0x04,  /* the range per caster level */
    SPELL_DURATION = 0x05,
    SPELL_DURATION_LEVEL = 0x06,
    SPELL_TARGETING = 0x07,    /* the low nibble the kind; 0 with a range of 0 is 1 */
    SPELL_SAVE_KIND = 0x0a,    /* a word: 0 none, 1 none on a save, 2 half */
    SPELL_TYPE = 0x0c,         /* a word: the saving throw's type, 0-4 (849a:10f9 passes it to 148f:1886) */
    SPELL_EFFECT = 0x0e,       /* the effect added, 0 none */
    SPELL_HALF_RANGE = 0x7d,   /* its range is a quarter (849a:0f34) */
    SPELL_ALLIES = 0x13,       /* a word: 0 for the caster's own side (the computer's aim, part 29b) */
    CLW = 0x03,
    MM = 0x0f,
    EFFECT_BLIND = 0x21,       /* the effect Cure Blindness takes (7fd8:0ff6, 148f:1bc6) */
    DEITY_WORD = 0x19b,        /* a word: 4 is the deity whose cleric adds a d8 (7fd8:00f8) */
    MAGIC_ORDER = 0x19d,
    EFFECT_96 = 0x96,          /* the effect 849a:10f9 checks on a target (with spell 0x37) */
    SPELL_37 = 0x37,
    SAVE_FAIL_EFFECT = 0x76,   /* the holder of this effect rolls 1 in 148f:1886 */
    TOUCH = 0xff,
    STATUS_TEN = 10,           /* the status word 0xa: saved by a spell (849a:10f9), no death */
};

/* The words of the spell engine and the fight, each at the DS offset cited (the test reads the
 * string there). The "%s" of the original's text is the text built so far: the suffixes here. */
typedef struct {
    const char *takes_one;     /* DS:13e "takes 1 point of damage " */
    const char *points;        /* DS:12b " points of damage " after "takes N" */
    const char *lost_spell;    /* DS:0c7 "lost a spell" */
    const char *goes_down;     /* DS:19b "Goes Down" */
    const char *dying;         /* DS:1a5 "%s and is Dying" (the suffix) */
    const char *killed;        /* DS:0fc "is killed" */
    const char *fully_healed;  /* DS:263 "is fully healed" */
    const char *partly_healed; /* DS:273 "is partially healed" */
    const char *from[4];       /* DS:157 "%sfrom Fire", 163 Cold, 16f Electricity, 182 Acid (the suffix) */
    const char *from_magic;    /* DS:18e "%sfrom Magic" (the suffix) */
    const char *underwater;    /* DS:87c9 "That has no effect underwater!" (the bar; Burning Hands) */
    const char *cured;         /* DS:0117 "is Cured" (the say of a cure, 148f:1bc6) */
    const char *can_see;       /* DS:88ea "can see" (the flash of Cure Blindness, 7fd8:0ff6) */
} queen_cast_words;

static const queen_cast_words words = {
    .takes_one = "takes 1 point of damage ",
    .points = " points of damage ",
    .lost_spell = "lost a spell",
    .goes_down = "Goes Down",
    .dying = " and is Dying",
    .killed = "is killed",
    .fully_healed = "is fully healed",
    .partly_healed = "is partially healed",
    .from = {"from Fire", "from Cold", "from Electricity", "from Acid"},
    .from_magic = "from Magic",
    .underwater = "That has no effect underwater!",
    .cured = "is Cured",
    .can_see = "can see",
};

/* The types the text names, by the DS word the test of each is (1, 2, 4, 0x100: acid, which the byte
 * type cannot hold; no spell here has it). */
static const unsigned from_type[4] = {1, 2, 4, 0x100};

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* The dice (148f:1968). */
static uint8_t roll(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice(&game->vm.seed, count, sides);
}

/* A call of an effect routine ended the run where it failed. */
static bool effect_failed(cok_adventure *game)
{
    return cok_adventure_effect_failed(game);
}

static bool dispatch_at(cok_adventure *game, cok_character **c, uint8_t event)
{
    return cok_effects_dispatch_at(&game->effects, c, event) || effect_failed(game);
}

uint8_t queen_spell_byte(cok_adventure *game, unsigned id, unsigned off)
{
    return cok_data_held(cok_game_rules(game)->data, SPELL_TABLE + (long)id * SPELL_RECORD + (long)off);
}

uint16_t queen_spell_word(cok_adventure *game, unsigned id, unsigned off)
{
    return (uint16_t)(queen_spell_byte(game, id, off) |
                      queen_spell_byte(game, id, off + 1) << 8);
}

/* The selected character's record as the caster (DS:ad67), which the original reads through NULL
 * without one. */
static uint8_t *caster_of(cok_adventure *game)
{
    if (game->vm.character == NULL)
        undefined(game, "the caster is read through NULL with no character selected (DS:ad67)");
    return game->vm.character;
}

/* The larger of two level bytes, compared as signed (177e:1900's jl). */
static uint8_t larger(uint8_t a, uint8_t b)
{
    return (int8_t)a < (int8_t)b ? b : a;
}

/* 177e:1900 for kind 2 (the mage's): class 5's level, the moon of the character's order (its phase
 * var 0x1f9 + order: COK_VAR_MOONS, element order - 1; 0 takes one off, 2 adds one with class 5 above
 * 5), and never below class 4's level less 8. Orders past the three moons read the moons' days in the
 * original: stopped. */
static bool moon_level(cok_adventure *game, const uint8_t *r, uint8_t *level)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t five = cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 5);
    int8_t value = (int8_t)five;
    int8_t floor = (int8_t)(uint8_t)(cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 4) - 8);
    if (five != 0) {
        uint8_t order = cok_field_u8(L, r, COK_FIELD_MAGIC_ORDER);
        if (order > 0) {
            if (order > 3)
                return cok_adventure_not_yet(game, "a magic order past the three moons (its days, "
                                                   "DS:b20f + 0x1fd)", "dqk 177e:1900");
            uint16_t phase = cok_ecl_var_at(&game->vm, COK_VAR_MOONS, order - 1);
            if (phase == 0) value = (int8_t)((uint8_t)value - 1);
            else if (phase == 2 && five > 5) value = (int8_t)((uint8_t)value + 1);
        }
    }
    if (value < floor) value = floor;
    *level = (uint8_t)value;
    return true;
}

bool queen_caster_level(cok_adventure *game, const uint8_t *r, uint8_t spell, bool capped,
                        uint8_t *level)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    unsigned kind = queen_spell_word(game, spell, SPELL_KIND);
    bool item = game->effects.rolls.item != 0;
    uint8_t lv = 0;
    bool above_four = false;
    /* Not a caster: class 0 and 5 at 0, class 4 below 8, class 1 below 9 (unsigned): 6 for any kind. */
    if (cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 0) == 0 &&
        cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 5) == 0 &&
        cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 4) < 8 &&
        cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 1) < 9) {
        lv = 6;
    } else {
        switch (kind) {
        case 0:
        case 3:
            lv = larger(cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 0),
                        cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 1));
            break;
        case 1: {
            int8_t value = (int8_t)(uint8_t)(cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 4) - 7);
            lv = value < 0 ? 0 : (uint8_t)value;
            break;
        }
        case 2:
            if (!moon_level(game, r, &lv)) return false;
            break;
        case 4:
            lv = 12;
            break;
        default:
            /* 177e:1900's [bp-2] is never set for a kind above 4: undefined unless an item gives 6. */
            above_four = true;
            break;
        }
    }
    /* With an item (rolls.item, DS:b7fc) the level is 6 unless the kind is 4. */
    if (item && kind != 4) lv = 6;
    else if (above_four)
        return undefined(game, "a spell kind above 4 leaves 177e:1900's level unset (dqk 177e:1900's "
                               "[bp-2])");
    if (capped && (int8_t)lv > 0x1f) lv = 0x1f;
    *level = lv;
    return true;
}

/* The caster level of spell for the selected character (uncapped or capped). */
static bool level_of(cok_adventure *game, uint8_t spell, bool capped, uint8_t *level)
{
    uint8_t *caster = caster_of(game);
    return caster != NULL && queen_caster_level(game, caster, spell, capped, level);
}

bool queen_spell_range(cok_adventure *game, uint8_t spell, uint8_t *range)
{
    uint8_t base = queen_spell_byte(game, spell, SPELL_RANGE);
    uint8_t per = queen_spell_byte(game, spell, SPELL_RANGE_LEVEL);
    uint8_t r;
    if (game->effects.rolls.item != 0) {
        r = (uint8_t)(per * 6 + base);
    } else {
        uint8_t lv;
        if (!level_of(game, spell, false, &lv)) return false;
        r = (uint8_t)(lv * per + base);
    }
    if (spell == SPELL_HALF_RANGE) r = (uint8_t)(r / 4);
    if (r == 0 && queen_spell_byte(game, spell, SPELL_TARGETING) != 0) r = 1;
    if (r == TOUCH) r = 1;
    *range = r;
    return true;
}

bool queen_spell_duration(cok_adventure *game, uint8_t spell, uint16_t *minutes)
{
    switch (spell) {
    case 0x1a:
        *minutes = 0x0ec4;
        return true;
    case 0x79:
        *minutes = (uint16_t)(roll(game, 1, 4) + 1);
        return true;
    case 0x7a:
        *minutes = 0x5a0;
        return true;
    case 0x7f:
    case 0x82:
        *minutes = roll(game, 5, 4);
        return true;
    case 0x80:
        *minutes = (uint16_t)(roll(game, 1, 4) * 10 + 40);
        return true;
    default: {
        uint8_t lv;
        if (!level_of(game, spell, false, &lv)) return false;
        *minutes = (uint16_t)(lv * queen_spell_byte(game, spell, SPELL_DURATION_LEVEL) +
                              queen_spell_byte(game, spell, SPELL_DURATION));
        return true;
    }
    }
}

/* The first target (DS:c5e3), which the handlers read; none, or one freed since a battle, is the
 * original's read through the table or the heap. */
static cok_character *first_target(cok_adventure *game)
{
    cok_character *t = game->spell_targets[1];
    if (t == NULL) {
        undefined(game, "the first target is read through NULL (DS:c5e3)");
        return NULL;
    }
    if (t == &cok_arena_freed) {
        undefined(game, "the first target is a record freed since, read where the heap may have put "
                        "another (DS:c5e3)");
        return NULL;
    }
    return t;
}

/* 177e:156e's "lost a spell" (row 12, with its wait), then 177e:1346 (the spell forgotten) and
 * 51c2:0c11 (the turn ends): a caster hurt in the battle (148f:2214). */
static bool lost_spell(cok_adventure *game, cok_character *c)
{
    if (!cok_arena_say(game, c, words.lost_spell, 12, true)) return false;
    cok_combat_forget(cok_game_rules(game), c->record, c->combat->spell);
    cok_combat_end_turn(c);
    return true;
}

bool queen_cast_damage(cok_adventure *game, cok_character *t, uint16_t amount, uint8_t save_kind,
                       bool saved)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_rules *R = cok_game_rules(game);
    cok_rolls *r = &game->effects.rolls;
    r->amount16 = (int16_t)amount;
    r->amount = (uint8_t)amount;
    /* 148f:2214: event 6 first (it may turn the damage on another); 0x14 is on &t too (225c). */
    if (!dispatch_at(game, &t, 6)) return false;
    if (saved) {
        if (save_kind == 1) r->amount16 = 0;
        else if (save_kind == 2) r->amount16 = (int16_t)(r->amount16 / 2);
    } else if (!dispatch_at(game, &t, 0x14)) {
        return false;
    }
    r->amount = (uint8_t)r->amount16;
    if (r->amount16 <= 0 || cok_field_u8(L, t->record, COK_FIELD_ACTIVE) == 0) return true;
    if (game->vm.mode != 5)
        return cok_adventure_not_yet(game, "damage outside combat (the camp's text and wait)",
                                     "dqk 148f:2214 outside combat");
    /* "takes N points of damage " (N as the word; 1 is "takes 1 point of damage "), then the
     * type's source. The type's byte is tested as the original tests it: 1, 2, 4 and 0x100 (acid,
     * which a byte cannot hold), then "from Magic" where (type & 8) == type. */
    char text[256];
    if (r->amount16 == 1) snprintf(text, sizeof text, "%s", words.takes_one);
    else snprintf(text, sizeof text, "takes %d%s", (int)r->amount16, words.points);
    unsigned type = r->damage_type;
    bool sourced = false;
    for (unsigned i = 0; i < 4 && !sourced; ++i)
        if ((type & 0xf7) == from_type[i]) {
            strcat(text, words.from[i]);
            sourced = true;
        }
    if (!sourced && (type & 8) == type) strcat(text, words.from_magic);
    /* 177e:085f's panel (the battle's: combat.panel is set in mode 5), the flash, then 177e:17a0. */
    game->combat.panel = true;
    if (!cok_arena_panel(game, t)) return false;
    if (!cok_arena_flash(game, t, 0, text)) return false;
    if (!cok_combat_damage(&game->combat, t, R, (uint16_t)r->amount16, 5))
        return undefined(game, "a record on a side other than 0 or 1 (+0x18a) is counted past DS:6b2d "
                               "(6346:24d7)");
    /* Mode 5: a hurt one may no longer cast, and one casting "lost a spell" and its turn ends. */
    if (t->combat == NULL)
        return undefined(game, "a record with no combat record is read through NULL in combat "
                               "(60f4:1f95)");
    t->combat->may_cast = 0;
    if (t->combat->spell > 0 && !lost_spell(game, t)) return false;
    if (cok_field_u8(L, t->record, COK_FIELD_ACTIVE) == 0) {
        /* The creature flags' quiet bit (8 in the second byte, +0xc8): "Goes Down" only. */
        bool quiet = (cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 1) &
                      R->fight.quiet_flag) != 0;
        uint8_t status = cok_field_u8(L, t->record, COK_FIELD_STATUS);
        char line[64];
        snprintf(line, sizeof line, "%s", words.goes_down);
        if (!quiet) {
            if (status == 5) strcat(line, words.dying);
            if (status >= 6 && status <= 8) snprintf(line, sizeof line, "%s", words.killed);
        }
        /* 148f:2214's say on the row after the cursor's (DS:ae4e, set by the panel to 5), no wait. */
        if (!cok_arena_say(game, t, line, (uint8_t)(game->vm.cursor.y + 1), false)) return false;
        if (!cok_combat_battle_only(game, t)) return false;
        int stale = cok_character_find_named(t, R, COK_EFFECT_BERSERK) != NULL
                        ? COK_ARENA_STALE_UNKNOWN
                        : 0;
        if (!dispatch_at(game, &t, 0x0d)) return false;
        if (cok_field_u8(L, t->record, COK_FIELD_ACTIVE) == 0 &&
            cok_field_u8(L, t->record, COK_FIELD_STATUS) != STATUS_TEN) {
            cok_arena_reachable also = cok_cast_reachable(game);
            if (!cok_arena_kill(game, t, stale, &also)) return false;
        } else {
            cok_dark_queen_speed_wait(game); /* 1a62:0960, the speed's wait (one draw) */
        }
    }
    cok_arena_clear_text(game);
    return true;
}

/* The character of a combatant's record (NULL for none, or a record not in the combat). */
static cok_character *combatant_of(cok_adventure *game, const uint8_t *record)
{
    uint8_t n = cok_combat_index(&game->combat, record);
    return n == 0 ? NULL : game->combat.combatant[n].character;
}

/* 177e:0be2 on c (cok_character_stats); its error ends the run as undefined. */
static bool recompute(cok_adventure *game, cok_character *c)
{
    char error[300];
    if (cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error))
        return true;
    return undefined(game, error);
}

/* The byte at var 0x1f9 + order: the moons' phases (orders 1-3, COK_VAR_MOONS), then their days
 * (orders 4-6, COK_VAR_MOON_DAYS); the caller stops past 6. */
static uint8_t moon_byte(cok_adventure *game, uint8_t order)
{
    return order <= 3 ? (uint8_t)cok_ecl_var_at(&game->vm, COK_VAR_MOONS, order - 1)
                      : (uint8_t)cok_ecl_var_at(&game->vm, COK_VAR_MOON_DAYS, order - 4);
}

/* 148f:1886 (t, type, bonus): the saving throw of t by type (the spell's +0xc word, 0-4), with
 * bonus (0 in the apply). The roll is d20; a holder of 0x76 rolls 1. A roll of 1 fails and a 20
 * saves, both with no event. Otherwise a mage (class 5 above 0, order above 0, both unsigned) gets
 * -1 or +1 from its moon's byte, then the roll is the save bonus (+0xcb, signed) plus the roll plus
 * the bonus, the type is kept for the event 0x0c on t (its slot, a copy here, may be swapped), and
 * the save is made when t's saving throw of that type (+0x8b + type, unbounded within the record)
 * is at most the roll. An order past the moons stops, cited. False where the run stopped. */
bool queen_save(cok_adventure *game, cok_character *t, uint8_t type, uint8_t bonus, bool *made)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_rolls *r = &game->effects.rolls;
    r->save_made = 1;
    r->save_roll = roll(game, 1, 20);
    if (cok_character_find_effect(t, SAVE_FAIL_EFFECT) != NULL) r->save_roll = 1;
    if (r->save_roll == 1) {
        r->save_made = 0;
    } else if (r->save_roll != 20) {
        if (cok_field_u8_at(L, t->record, COK_FIELD_CLASS_LEVEL, 5) > 0 &&
            cok_field_u8(L, t->record, COK_FIELD_MAGIC_ORDER) > 0) {
            uint8_t order = cok_field_u8(L, t->record, COK_FIELD_MAGIC_ORDER);
            if (order > 6)
                return cok_adventure_not_yet(game, "a saving throw by a mage past the moons (var "
                                                   "0x1f9 + order, past its days)",
                                             "dqk 148f:1886 reads var 0x1f9 + order past the moons' days");
            uint8_t moon = moon_byte(game, order);
            if (moon == 0) bonus = (uint8_t)(bonus - 1);
            else if (moon == 2) bonus = (uint8_t)(bonus + 1);
        }
        r->save_roll = (uint8_t)(cok_field_s8(L, t->record, COK_FIELD_SAVE_BONUS) + r->save_roll +
                                 bonus);
        r->save_type = type;
        cok_character *slot = t;
        if (!dispatch_at(game, &slot, 0x0c)) return false;
        t = slot;
        long at = cok_field_offset_unchecked(L, COK_FIELD_SAVING_THROW, 0, type);
        if (at < 0 || at >= L->size)
            return undefined(game, "a saving throw type outside the record (148f:1886's +0x8b + type)");
        r->save_made = t->record[at] <= r->save_roll;
    }
    *made = r->save_made != 0;
    return true;
}

bool queen_spell_apply(cok_adventure *game, uint8_t spell, uint8_t value, bool on_remove,
                       uint8_t amount, uint8_t type, const char *text)
{
    cok_rolls *r = &game->effects.rolls;
    r->damage_type = amount != 0 ? type : 0;
    if (game->spell_target_count == 0) return true;
    if (value == 0 && !level_of(game, spell, true, &value)) return false;
    uint8_t save_kind = (uint8_t)queen_spell_word(game, spell, SPELL_SAVE_KIND);
    uint8_t effect = queen_spell_byte(game, spell, SPELL_EFFECT);
    /* The count and each target are read again at every use: the touch's event may swap a slot. */
    for (uint8_t i = 1; i <= game->spell_target_count; ++i) {
        if (game->spell_targets[i] == NULL) continue;
        bool saved = false;
        if (queen_spell_word(game, spell, SPELL_SAVE_KIND) != 0) {
            const cok_character_layout *L = cok_game_rules(game)->character;
            cok_character *t = game->spell_targets[i];
            if (cok_field_u8(L, t->record, COK_FIELD_STATUS) == STATUS_TEN)
                saved = true;
            else if (cok_character_find_effect(t, EFFECT_96) != NULL && r->spell == SPELL_37)
                saved = true;
            else {
                bool made = false;
                if (!queen_save(game, t, (uint8_t)queen_spell_word(game, spell, SPELL_TYPE), 0,
                                &made))
                    return false;
                saved = made;
            }
        }
        if (queen_spell_byte(game, r->spell, SPELL_RANGE) == TOUCH) {
            /* 849a:10f9's touch: the stats recomputed (0be2), the event 0x0b on the slot, then the
             * hit roll (148f:1771) of the caster against the target's armour class. A miss zeroes
             * the amount for this and every later target, and saves this one. */
            const cok_character_layout *L = cok_game_rules(game)->character;
            if (!recompute(game, game->spell_targets[i])) return false;
            if (!dispatch_at(game, &game->spell_targets[i], 0x0b)) return false;
            if (game->spell_targets[i] == NULL)
                return undefined(game, "the touch's target slot is NULL after event 0x0b (DS:c5df)");
            uint8_t *caster = caster_of(game);
            if (caster == NULL) return false;
            cok_character *attacker = combatant_of(game, caster);
            if (attacker == NULL)
                return undefined(game, "the caster has no combatant for the touch's hit roll (DS:ad67, "
                                       "148f:1771)");
            cok_character *t = game->spell_targets[i];
            bool hit = false;
            if (!cok_combat_hit(game, attacker, t, cok_field_u8(L, t->record, COK_FIELD_AC), &hit))
                return false;
            if (!hit) {
                amount = 0;
                saved = true;
            }
        }
        if (amount != 0 && !queen_cast_damage(game, game->spell_targets[i], amount, save_kind, saved))
            return false;
        if (effect > 0) {
            uint16_t minutes;
            if (!queen_spell_duration(game, spell, &minutes) ||
                !cok_cast_add_effect(game, game->spell_targets[i], effect, minutes, value, on_remove,
                                     save_kind, saved, text))
                return false;
        }
    }
    r->damage_type = 0;
    return cok_combat_pending_lists(game, "dqk 849a:10f9's end (148f:2ab8)");
}

bool queen_healed(cok_adventure *game, cok_character *c)
{
    if (game->vm.mode != 5)
        return cok_adventure_not_yet(game, "a heal's text outside combat (the party list)",
                                     "dqk 5e22:0065");
    const cok_character_layout *L = cok_game_rules(game)->character;
    bool full = cok_field_u8(L, c->record, COK_FIELD_HIT_POINTS) ==
                cok_field_u8(L, c->record, COK_FIELD_MAX_HIT_POINTS);
    return cok_arena_say(game, c, full ? words.fully_healed : words.partly_healed, 10, true);
}

/* The caster's deity is 4 (the word at +0x19b: 7fd8:00f8, 1c2e, 2073), whose cleric adds a d8 to a heal. */
static bool deity_four(const uint8_t *caster)
{
    return (uint16_t)(caster[DEITY_WORD] | caster[DEITY_WORD + 1] << 8) == 4;
}

/* The heal of the first target by amount (148f:25f7 with flag 0, 177e:187a's text when healed): the tail of
 * every heal here. */
static bool heal_first(cok_adventure *game, uint8_t amount)
{
    cok_character *t = first_target(game);
    if (t == NULL) return false;
    bool healed = false;
    if (!cok_effects_heal(&game->effects, t, amount, false, &healed)) return effect_failed(game);
    return !healed || queen_healed(game, t);
}

bool queen_cure_light(cok_adventure *game)
{
    if (game->spell_target_count == 0) return true;
    uint8_t *caster = caster_of(game);
    if (caster == NULL) return false;
    /* The bonus first, a d8 for a caster of deity 4; then the heal's d8. */
    uint8_t bonus = 0;
    if (deity_four(caster)) bonus = roll(game, 1, 8);
    return heal_first(game, (uint8_t)(roll(game, 1, 8) + bonus));
}

bool queen_magic_missile(cok_adventure *game)
{
    uint8_t spell = game->effects.rolls.spell;
    uint8_t lv;
    if (!level_of(game, spell, false, &lv)) return false;
    lv = (uint8_t)(lv + 1);
    uint8_t n = (uint8_t)(lv / 2);
    uint8_t amount = (uint8_t)(cok_dice_count(&game->vm.seed, n, 4, &game->effects.rolls.dice) + n);
    return queen_spell_apply(game, spell, 0, false, amount, 8, "");
}

/* Var 0x3c (COK_VAR_RECORD_EFFECT): underwater. */
static bool underwater(cok_adventure *game)
{
    return cok_ecl_var(&game->vm, COK_VAR_RECORD_EFFECT) != 0;
}

/* 148f:199e (count, sides): the sum of the dice as cok_dice gives it, the count stored in rolls.dice (DS:b7f4). */
static uint8_t dice_count(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice_count(&game->vm.seed, count, sides, &game->effects.rolls.dice);
}

/* 7fd8:0162 (Cause Light, 0x04, a touch): 1d8 of damage from magic (type 8). */
static bool queen_cause_light(cok_adventure *game)
{
    uint8_t amount = dice_count(game, 1, 8);
    return queen_spell_apply(game, game->effects.rolls.spell, 0, false, amount, 8, "");
}

/* 7fd8:01e6 (Burning Hands, 0x09 and 0x40): underwater the bar says it has no effect and nothing else happens;
 * else the uncapped caster level of damage from fire (type 9). */
static bool queen_burning_hands(cok_adventure *game)
{
    if (underwater(game)) {
        queen_bar_message(game, words.underwater);
        return true;
    }
    uint8_t spell = game->effects.rolls.spell, lv;
    if (!level_of(game, spell, false, &lv)) return false;
    return queen_spell_apply(game, spell, 0, false, lv, 9, "");
}

/* 7fd8:0588 (Shocking Grasp, 0x14, a touch): 1d8, then the uncapped caster level added (a byte), damage of
 * electricity (type 0x0c). */
static bool queen_shocking_grasp(cok_adventure *game)
{
    uint8_t spell = game->effects.rolls.spell, lv;
    uint8_t roll_sum = dice_count(game, 1, 8);
    if (!level_of(game, spell, false, &lv)) return false;
    return queen_spell_apply(game, spell, 0, false, (uint8_t)(roll_sum + lv), 0x0c, "");
}

/* 7fd8:1e31 (Cause Serious, 0x42): 2d8 + 1 of damage from magic. */
static bool queen_cause_serious(cok_adventure *game)
{
    uint8_t amount = (uint8_t)(dice_count(game, 2, 8) + 1);
    return queen_spell_apply(game, game->effects.rolls.spell, 0, false, amount, 8, "");
}

/* 7fd8:20df (Cause Critical, 0x48, a touch, save kind 1): 3d8 + 3 of damage from magic. */
static bool queen_cause_critical(cok_adventure *game)
{
    uint8_t amount = (uint8_t)(dice_count(game, 3, 8) + 3);
    return queen_spell_apply(game, game->effects.rolls.spell, 0, false, amount, 8, "");
}

/* 7fd8:1c2e (Cure Serious, 0x3a): with no target nothing is rolled. Else a deity 4 caster's d8 first, then
 * 2d8, the bonus and 1 for the heal. */
static bool queen_cure_serious(cok_adventure *game)
{
    if (game->spell_target_count == 0) return true;
    uint8_t *caster = caster_of(game);
    if (caster == NULL) return false;
    uint8_t bonus = deity_four(caster) ? roll(game, 1, 8) : 0;
    return heal_first(game, (uint8_t)(roll(game, 2, 8) + bonus + 1));
}

/* 7fd8:2073 (Cure Critical, 0x47): the caster's bonus is read before the target test: a deity 4 caster's d8 + 3,
 * else 3. Then, with a target, 3d8 and the bonus. */
static bool queen_cure_critical(cok_adventure *game)
{
    uint8_t *caster = caster_of(game);
    if (caster == NULL) return false;
    uint8_t bonus = deity_four(caster) ? (uint8_t)(roll(game, 1, 8) + 3) : 3;
    if (game->spell_target_count == 0) return true;
    return heal_first(game, (uint8_t)(roll(game, 3, 8) + bonus));
}

/* 7fd8:2c32 (Cure Light Wounds, the druid's, 0x62): with a target, 1d8; the deity is not read. */
static bool queen_cure_druid(cok_adventure *game)
{
    if (game->spell_target_count == 0) return true;
    return heal_first(game, roll(game, 1, 8));
}

/* 148f:1bc6 (c, id, &cured): the first effect id on c (177e:1734) is said "is Cured" (row 10, waited) and removed
 * as cok_effects_remove does (its handler if the effect asks for one); cured is whether it was found. */
static bool queen_cure(cok_adventure *game, cok_character *c, uint8_t id, bool *cured)
{
    cok_effect *effect = cok_character_find_effect(c, id);
    *cured = effect != NULL;
    if (effect == NULL) return true;
    if (!cok_arena_say(game, c, words.cured, 10, true)) return false;
    return cok_effects_remove(&game->effects, c, effect, id) || effect_failed(game);
}

/* 7fd8:0ff6 (Cure Blindness, 0x25): no target test; the first target's blindness cured, then "can see" flashed
 * (kind 1) if it was. */
static bool queen_cure_blindness(cok_adventure *game)
{
    cok_character *t = first_target(game);
    if (t == NULL) return false;
    bool cured = false;
    if (!queen_cure(game, t, EFFECT_BLIND, &cured)) return false;
    return !cured || cok_arena_flash(game, t, 1, words.can_see);
}

/* The plain applies: 7fd8's 21 routines, 38 ids (plain.txt). Each is 849a:10f9(spell, 0, 0, 0, 0, text):
 * the spell's effect (its +0xe) for its duration with the text said, after the saves and the touch as the
 * spell's record gives. ds is the DS offset the routine pushes (the string there, checked by the tests); the
 * empty text at 879d adds the effect silently. 0x50 (Invisibility to Animals) is a touch castable in combat
 * and camp: outside combat its touch's combatant is undefined here (the original's 148f:1771 is well
 * defined), not reachable before part 46's camp Cast. 0x12, 0x16 and 0x1f are camp-only ("Camp Only Spell"
 * in a battle) and 0x82, 0x87 and 0x88 are items' (Use, part 50): the same routines, reached by those. */
typedef struct {
    uint8_t id;
    uint16_t ds;
    const char *text;
} queen_plain;

static const queen_plain plain_table[] = {
    /* 018c */
    {0x05, 0x879e, "is affected"}, {0x0b, 0x879e, "is affected"}, {0x12, 0x879e, "is affected"},
    {0x16, 0x879e, "is affected"}, {0x1d, 0x879e, "is affected"}, {0x3c, 0x879e, "is affected"},
    {0x4d, 0x879e, "is affected"},
    /* 01aa */
    {0x06, 0x87aa, "is protected"}, {0x07, 0x87aa, "is protected"}, {0x10, 0x87aa, "is protected"},
    {0x11, 0x87aa, "is protected"}, {0x34, 0x87aa, "is protected"}, {0x35, 0x87aa, "is protected"},
    {0x36, 0x87aa, "is protected"}, {0x39, 0x87aa, "is protected"}, {0x45, 0x87aa, "is protected"},
    {0x7a, 0x87aa, "is protected"},
    /* 01c8 */
    {0x08, 0x87b7, "is cold-resistant"},
    /* 056a */
    {0x13, 0x882a, "is shielded"},
    /* 073c */
    {0x18, 0x884b, "is fire resistant"},
    /* 075a */
    {0x19, 0x885d, "is silenced"}, {0x3b, 0x885d, "is silenced"},
    /* 091b */
    {0x1e, 0x8869, "is invisible"}, {0x32, 0x8869, "is invisible"},
    /* 0939 */
    {0x1f, 0x8876, "Knock-Knock"},
    /* 09a6 */
    {0x21, 0x8890, "is weakened"},
    /* 1026 */
    {0x26, 0x88f2, "is blind"},
    /* 14dd */
    {0x2c, 0x8929, "has been cursed!"},
    /* 14fb */
    {0x2d, 0x893a, "is blinking"},
    /* 1d39 */
    {0x82, 0x8964, "is paralyzed"},
    /* 23a7: a touch, castable in combat and camp (outside combat undefined: see above) */
    {0x50, 0x8869, "is invisible"},
    /* 2960 */
    {0x58, 0x87aa, "is protected"},
    /* 297e */
    {0x5a, 0x8a2b, "is toughened"},
    /* 2b1c, 2c14, 2cae: no text */
    {0x87, 0x879d, ""}, {0x88, 0x879d, ""}, {0x64, 0x879d, ""},
    /* 3279, 370d */
    {0x70, 0x87aa, "is protected"}, {0x77, 0x87aa, "is protected"},
};

static const queen_plain *plain_of(uint8_t id)
{
    for (size_t i = 0; i < sizeof plain_table / sizeof *plain_table; ++i)
        if (plain_table[i].id == id) return &plain_table[i];
    return NULL;
}

const char *queen_plain_text(uint8_t id, uint16_t *ds)
{
    const queen_plain *p = plain_of(id);
    if (p == NULL) return NULL;
    *ds = p->ds;
    return p->text;
}

/* 7fd8's plain apply of the spell being cast (rolls.spell). */
static bool queen_plain_apply(cok_adventure *game)
{
    const queen_plain *p = plain_of(game->effects.rolls.spell);
    if (p == NULL) return undefined(game, "a spell with no plain apply reaches its routine (7fd8)");
    return queen_spell_apply(game, p->id, 0, false, 0, 0, p->text);
}

/* The caster's side: its word +0x66 (148f's hostile flag; a word in Dark Queen's records, as
 * cok_combat_field_word reads it). False where the run stopped. */
static bool caster_side(cok_adventure *game, unsigned *side)
{
    uint8_t *caster = caster_of(game);
    if (caster == NULL) return false;
    *side = cok_combat_field_word(cok_game_rules(game), caster, COK_FIELD_HOSTILE);
    return true;
}

/* 7fd8:0001 (side, text): Bless and Curse. Each target whose word +0x66 is not side is cleared (NULL);
 * a Bless (spell 1 only, not 0x3e) in a battle clears one of the side with an enemy within 1 (51c2:0980,
 * cok_combat_enemies). The images flag is set first (no mirror image), before the clears; then the
 * apply of the spell with text.
 * The original reads a NULL target's +0x66 through NULL: undefined here. */
static bool bless(cok_adventure *game, unsigned side, const char *text)
{
    const cok_rules *R = cok_game_rules(game);
    cok_rolls *r = &game->effects.rolls;
    r->images = 1;
    for (uint8_t i = 1; i <= game->spell_target_count; ++i) {
        cok_character *t = game->spell_targets[i];
        if (t == NULL)
            return undefined(game, "Bless or Curse reads a target's side through NULL (DS:c5df)");
        if (cok_combat_field_word(R, t->record, COK_FIELD_HOSTILE) != side) {
            game->spell_targets[i] = NULL;
            continue;
        }
        if (r->spell == 1 && game->vm.mode == 5) {
            uint8_t n = 0;
            if (!cok_combat_enemies(game, t, 1, &n)) return false;
            if (n != 0) game->spell_targets[i] = NULL;
        }
    }
    return queen_spell_apply(game, r->spell, 0, false, 0, 0, text);
}

/* 7fd8:00a2 (Bless, 0x01 and 0x3e): the caster's side, "is Blessed". */
static bool bless_spell(cok_adventure *game)
{
    unsigned side;
    return caster_side(game, &side) && bless(game, side, "is Blessed");
}

/* 7fd8:00c9 (Curse, 0x02): the other side (51c2:08fd: 1 where the caster's word is 0, else 0), "is
 * Cursed". */
static bool curse_spell(cok_adventure *game)
{
    unsigned side;
    if (!caster_side(game, &side)) return false;
    return bless(game, side == 0 ? 1 : 0, "is Cursed");
}

/* 7fd8:13b7 (Prayer, 0x2a): the caster level (capped) plus 0x80 where the bit 0 of the caster's byte
 * +0x66 is set (shifted to bit 7), "is praying". */
static bool prayer(cok_adventure *game)
{
    uint8_t *caster = caster_of(game);
    uint8_t lv;
    if (caster == NULL || !level_of(game, 0x2a, true, &lv)) return false;
    uint8_t hostile = cok_field_u8(cok_game_rules(game)->character, caster, COK_FIELD_HOSTILE);
    uint8_t value = (uint8_t)(lv + ((hostile & 1) << 7));
    return queen_spell_apply(game, 0x2a, value, false, 0, 0, "is praying");
}

/* 7fd8:0957 (Mirror Image, 0x20): a d4, drawn before the level, then the value d << 5 | the caster level
 * (capped), "is duplicated". */
static bool mirror_image(cok_adventure *game)
{
    uint8_t d = roll(game, 1, 4);
    uint8_t lv;
    if (!level_of(game, 0x20, true, &lv)) return false;
    return queen_spell_apply(game, 0x20, (uint8_t)((d << 5) | lv), false, 0, 0, "is duplicated");
}

/* 7fd8:210b (Dispel Evil, 0x49): effect 4 on the caster for the spell's duration with value 0 and no text
 * (148f:252c, cok_cast_add_effect), then the spell's apply: its effect with "is affected". */
static bool dispel_evil(cok_adventure *game)
{
    uint8_t *caster = caster_of(game);
    if (caster == NULL) return false;
    cok_character *c = combatant_of(game, caster);
    if (c == NULL) return undefined(game, "Dispel Evil's caster has no combatant (DS:ad67, 148f:252c)");
    uint16_t minutes;
    if (!queen_spell_duration(game, 0x49, &minutes)) return false;
    if (!cok_cast_add_effect(game, c, 4, minutes, 0, false, 0, false, "")) return false;
    return queen_spell_apply(game, 0x49, 0, false, 0, 0, "is affected");
}

/* 7fd8:36d6 (Fire Touch, 0x76): underwater (var 0x3c) the bar's refusal only; else the apply, "is covered
 * in flame". */
static bool fire_touch(cok_adventure *game)
{
    if (underwater(game)) {
        queen_bar_message(game, words.underwater);
        return true;
    }
    return queen_spell_apply(game, 0x76, 0, false, 0, 0, "is covered in flame");
}

queen_spell_fn *queen_spell_handler(uint8_t id)
{
    switch (id) {
    case CLW: return queen_cure_light;
    case MM: return queen_magic_missile;
    /* The one-target damage and the heals (7fd8's band a2; part 30b). */
    case 0x04: return queen_cause_light;
    case 0x09:
    case 0x40: return queen_burning_hands;
    case 0x14: return queen_shocking_grasp;
    case 0x25: return queen_cure_blindness;
    case 0x3a: return queen_cure_serious;
    case 0x42: return queen_cause_serious;
    case 0x47: return queen_cure_critical;
    case 0x48: return queen_cause_critical;
    case 0x62: return queen_cure_druid;
    /* Bless (0x3e, Bless's power, the same routine), Curse, Prayer, Mirror Image, Dispel Evil, Fire Touch. */
    case 0x01:
    case 0x3e: return bless_spell;
    case 0x02: return curse_spell;
    case 0x2a: return prayer;
    case 0x20: return mirror_image;
    case 0x49: return dispel_evil;
    case 0x76: return fire_touch;
    default: return plain_of(id) != NULL ? queen_plain_apply : NULL;
    }
}

/* The Cast command and the cast (dqk 52db:2543, 849a:145b, 52db:1f78, 52db:1e78, 849a:21b5). The
 * caster is the selected member (vm.character, DS:ad67); the spell's targets are spell_targets
 * (DS:c5df, count DS:c6f4), its cell spell_x, spell_y (DS:bd2c, bd49). The computer's aim (ai) is
 * pick's computer branch: 52db:1d42 (computer_pick) and 52db:48aa (area_aim). */

/* The bar's and the battle's words of the command and the cast (DS:5c2e, 5c3e, 5c5d, 5c01, 5c17,
 * 8b17, and the abort's question and "Spell Aborted" in DS). */
static const char *const cast_camp_only = "Camp Only Spell";
static const char *const cast_begins = "Begins Casting";
static const char *const cast_abort = "Abort Spell";
static const char *const cast_aborted = "Spell Aborted";
static const char *const cast_already = "Already been targeted";
static const char *const cast_self = "Cannot target yourself";
static const char *const cast_casts = "Casts a Spell";

/* The selected member (the party's record vm.character), NULL with none. */
static cok_character *caster_member(cok_adventure *game)
{
    if (game->vm.character == NULL) return NULL;
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* Whether who is one of the targets 1 to picks. */
static bool listed_in(const cok_adventure *game, unsigned picks, const cok_character *who)
{
    for (unsigned i = 1; i <= picks; ++i)
        if (game->spell_targets[i] == who) return true;
    return false;
}

/* 52db:1d42 (c, spell, found, res): the computer's aim of a one-target spell. The pick is 3d9e's
 * (cok_combat_pick_target_side with no flag, force, and the allies test of the spell's word +0x13 0),
 * which leaves its target in c's combat record (+0x0b); that target gives res with its cell when its
 * worth to the spell's side (42cc, queen_spell_side) is above 0. False where the run stopped. */
static bool computer_pick(cok_adventure *game, cok_character *c, uint8_t spell, bool *found,
                          cok_combat_aimed *res)
{
    uint8_t range;
    if (!queen_spell_range(game, spell, &range)) return false;
    bool allies = queen_spell_word(game, spell, SPELL_ALLIES) == 0;
    bool picked = false;
    if (!cok_combat_pick_target_side(game, c, range, false, true, allies, &picked)) return false;
    if (!picked) return true;
    cok_combat_record *cr = c->combat;
    if (cr == NULL || cr->target == NULL)
        return undefined(game, "the computer's aim picked no target (52db:1d42)");
    cok_character *t = combatant_of(game, cr->target);
    if (t == NULL) return undefined(game, "the computer's aim's target has no combatant (52db:1d42)");
    int8_t worth = 0;
    if (!queen_target_worth(game, t, spell, queen_spell_side(game, c, spell), &worth)) return false;
    if (worth <= 0) return true;
    *found = true;
    res->target = cr->target;
    res->x = cok_combat_x(&game->combat, cr->target);
    res->y = cok_combat_y(&game->combat, cr->target);
    return true;
}

/* 52db:48aa (c, spell, found, res): the computer's aim of an area spell (kinds 8 to 0x0e that are
 * not a line; no pick). The radius is the spell's +7 less 8 (3 for 0x57 underwater, 2 for 0x33
 * underwater, one less for 0x2f and 0x73 with var 0x22 0). The reach is the range plus the radius,
 * clamped to 0x12 with the caster's target as the centre once above it (else the caster is the
 * centre); the inner box is the reach less the radius. Every active record of the party's list
 * (DS:c6e5, the monsters too, not combat->listed: 42cc's Bless lists again) within the outer box
 * (left and top at the inner box's for 0x22 and 0x65) adds its worth (42cc) to each cell of its
 * rows within the radius that the row's half width (DS:5a50) reaches, clipped to the inner box, as
 * words. The cell with the most (a sum above 0; a tie of positive sums is drawn, 25 in 100, before
 * the bounds test) is taken when it is inside the map (x 0..0x31, y 0..0x17) and the caster's cell is
 * seen from it within the range. found when a sum above 0 was taken. False where the run stopped. */
static bool area_aim(cok_adventure *game, cok_character *c, uint8_t spell, bool *found,
                     cok_combat_aimed *res)
{
    cok_combat *combat = &game->combat;
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    cok_combat_record *cr = c->combat;
    if (cr == NULL) return undefined(game, "the caster has no combat record (52db:48aa)");
    unsigned side = queen_spell_side(game, c, spell);
    int radius = queen_spell_byte(game, spell, 7) - 8;
    if (spell == 0x57 && underwater(game)) radius = 3;
    else if (spell == 0x33 && underwater(game)) radius = 2;
    if ((spell == 0x2f || spell == 0x73) && cok_ecl_load_var(&game->vm, 0x22) == 0) --radius;
    if (radius < 0 || radius > 3)
        return undefined(game, "the area's half widths (DS:5a50) are read past their rows (52db:48aa)");
    uint8_t range;
    if (!queen_spell_range(game, spell, &range)) return false;
    int reach = range + radius;
    const uint8_t *centre = c->record;
    if (reach > 0x12) {
        reach = 0x12;
        if (cr->target != NULL) centre = cr->target;
    }
    int inner = reach - radius;
    int cx = cok_combat_x(combat, centre), cy = cok_combat_y(combat, centre);
    bool asym = spell == 0x22 || spell == 0x65;
    int left = cx - (asym ? inner : reach), top = cy - (asym ? inner : reach);
    int right = cx + reach, bottom = cy + reach;
    int ilt = cx - inner, itop = cy - inner, irt = cx + inner, ibt = cy + inner;
    /* The sums over the inner box, indexed by the offset from the centre (18 is 0). */
    int16_t sums[37][37];
    memset(sums, 0, sizeof sums);
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *e = game->party.members[i];
        if (cok_field_u8(L, e->record, COK_FIELD_ACTIVE) == 0) continue;
        int ex = cok_combat_x(combat, e->record), ey = cok_combat_y(combat, e->record);
        if (ex < left || ex > right || ey < top || ey > bottom) continue;
        int8_t good = 0;
        if (!queen_target_worth(game, e, spell, side, &good)) return false;
        int lo = asym ? 0 : -radius, hi = radius;
        if (ey + lo < itop) lo = itop - ey;
        if (ey + hi > ibt) hi = ibt - ey;
        for (int dy = lo; dy <= hi; ++dy) {
            int half = (int16_t)cok_data_get16_(rules->data, 0x5a50 + 6 * dy + 2 * radius);
            int from = asym ? ex : ex - half, to = ex + half;
            if (from < ilt) from = ilt;
            if (to > irt) to = irt;
            for (int x = from; x <= to; ++x) {
                int16_t *sum = &sums[x - cx + 18][ey + dy - cy + 18];
                *sum = (int16_t)(*sum + good);
            }
        }
    }
    /* The cell: dx outer, dy inner, over the inner box; the sight is the cell's, to the caster's cell
     * (5d71:0328(cell x, cell y, &caster x, &caster y, &range): the cell's terrain gives the eye). */
    int ox = cok_combat_x(combat, c->record), oy = cok_combat_y(combat, c->record);
    int best = 0, bdx = 0, bdy = 0;
    for (int dx = -inner; dx <= inner; ++dx)
        for (int dy = -inner; dy <= inner; ++dy) {
            int v = sums[dx + 18][dy + 18];
            bool take = v > best;
            if (!take && v == best && v > 0) take = cok_tp_random(&game->vm.seed, 100) < 0x19;
            if (!take) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x > 0x31 || y < 0 || y > 0x17) continue;
            int8_t sx = (int8_t)ox, sy = (int8_t)oy;
            uint16_t sight = range;
            bool seen = false;
            if (!cok_combat_sight(combat, rules, (int8_t)x, (int8_t)y, &sx, &sy, &sight, &seen))
                return undefined(game, "sight (5d71:0328) reads a cell past the terrain table");
            if (!seen) continue;
            best = v;
            bdx = dx;
            bdy = dy;
        }
    if (best > 0) {
        *found = true;
        res->target = NULL;
        res->x = (int8_t)(cx + bdx);
        res->y = (int8_t)(cy + bdy);
    }
    return true;
}

/* 52db:1e78 (spell, ai, empty, found, res): the aim of a spell's one target or cell. The player's
 * (not ai) is Aim with attack 0 for the spell's range (849a:0f34), the creatures unless the spell is
 * 0x53, and empty as the caller gives (the cells with no one); the caster's target (+0x0b) is what
 * Aim gave, always. The computer's is computer_pick (empty 0, or a line: +7 8) or area_aim. found is
 * whether a pick was made (its cell is then spell_x, spell_y); keys running out leaves found clear
 * with vm.abort set. False where the run stopped. */
static bool pick(cok_adventure *game, cok_character *c, uint8_t spell, bool ai, uint8_t empty,
                 bool *found, cok_combat_aimed *res)
{
    *found = false;
    *res = (cok_combat_aimed){NULL, 0, 0};
    if (ai) {
        bool one = empty == 0 || queen_spell_byte(game, spell, 7) == 8;
        if (!(one ? computer_pick(game, c, spell, found, res) : area_aim(game, c, spell, found, res)))
            return false;
    } else {
        uint8_t range;
        if (!queen_spell_range(game, spell, &range)) return false;
        bool done = false;
        if (!queen_aim(game, c, range, 0, empty, spell != 0x53, &done, res)) return false;
        c->combat->target = res->target;
        if (game->vm.abort) return true;
        if (!done) {
            *res = (cok_combat_aimed){NULL, 0, 0};
            return true;
        }
        *found = true;
    }
    if (*found) {
        game->spell_x = res->x;
        game->spell_y = res->y;
    }
    return true;
}

/* The area of a spell cast at (spell_x, spell_y): the combatants listed around it (5d71:07d7, any
 * direction, a footprint of 1) become the targets 1 to the count, and the spell takes an image. */
static bool area(cok_adventure *game, uint16_t radius)
{
    cok_combat *combat = &game->combat;
    if (!cok_combat_list(combat, cok_game_rules(game), game->spell_x, game->spell_y, radius, 0xff, 1))
        return undefined(game, "the list of combatants around one (5d71:07d7) reads past its tables");
    uint8_t n = combat->listed_count;
    for (uint8_t i = 1; i <= n; ++i)
        game->spell_targets[i] = combat->combatant[combat->listed[i].index].character;
    game->spell_target_count = n;
    game->effects.rolls.images = 1;
    return true;
}

/* The area's radius of a spell of kind 8 to 0x0e (1f78's): 3 for 0x57 underwater, 2 for 0x33
 * underwater and for 0x73 or 0x2f with var 0x22 0, else the spell's +7 low nibble. */
static unsigned area_radius(cok_adventure *game, uint8_t spell)
{
    if (spell == 0x57 && underwater(game)) return 3;
    if ((spell == 0x33 && underwater(game)) ||
        ((spell == 0x73 || spell == 0x2f) && cok_ecl_load_var(&game->vm, 0x22) == 0))
        return 2;
    return queen_spell_byte(game, spell, 7) & 7;
}

/* The weight of a target against an allowance (1f78's kind 5): by the highest level (+0x91; 0 and 1
 * weigh 1, 2 weighs 2, 3 weighs 4, more 8), or for Faerie Fire 0x4f by the size (+0x8a) less one (0
 * weighs 1, 1 and 2 weigh 2, 3 weighs 4, more 0). */
static unsigned weight(cok_adventure *game, uint8_t spell, const cok_character *who)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (spell != 0x4f) {
        uint8_t level = cok_field_u8(L, who->record, COK_FIELD_HIGHEST_LEVEL);
        if (level <= 1) return 1;
        if (level == 2) return 2;
        return level == 3 ? 4 : 8;
    }
    uint8_t size = (uint8_t)(cok_field_u8(L, who->record, COK_FIELD_SIZE) - 1);
    if (size == 0) return 1;
    if (size <= 2) return 2;
    return size == 3 ? 4 : 0;
}

/* 1f78's kind 5 (Faerie Fire 0x4f and Charm Monsters 0x51): picks until one is refused, the total
 * weight passes the allowance (after a second pick), or, for the computer, the allowance runs out
 * (its repeats count down). The allowance is the caster's level for 0x4f, else 2d4. */
static bool several(cok_adventure *game, cok_character *c, uint8_t spell, bool ai)
{
    cok_combat *combat = &game->combat;
    uint8_t allowance;
    if (spell == 0x4f) {
        uint8_t lv;
        if (!level_of(game, spell, false, &lv)) return false;
        allowance = lv;
    } else {
        allowance = roll(game, 2, 4);
    }
    unsigned total = 0, picks = 0;
    bool stop = false;
    do {
        bool found;
        cok_combat_aimed res;
        if (!pick(game, c, spell, ai, 0, &found, &res)) return false;
        if (game->vm.abort) return true;
        if (!found) {
            stop = true;
            continue;
        }
        cok_character *who = combatant_of(game, res.target);
        if (who == NULL) return undefined(game, "a target picked with no record (52db:1e78)");
        bool already = listed_in(game, picks, who);
        if (!already) {
            game->spell_targets[++picks] = who;
            game->spell_target_count = (uint8_t)picks;
            game->spell_x = cok_combat_x(combat, who->record);
            game->spell_y = cok_combat_y(combat, who->record);
            total += weight(game, spell, who);
            if (picks > 1 && total > allowance) stop = true;
        }
        if (already && ai) --allowance;
        if (already && !ai) queen_bar_message(game, cast_already);
        if (!cok_arena_redraw_cell(game, cok_combat_x(combat, who->record),
                                   cok_combat_y(combat, who->record)))
            return false;
    } while (!stop && allowance != 0);
    return true;
}

/* 1f78's one-target kinds (1-4, 6, 7): 1 + (+7 & 3) picks, each not yet listed (the caster herself
 * only for 0x17 and 0x31 is refused: "Cannot target yourself"). Picks 0 leaves done clear and the
 * cell as it was. */
static bool single(cok_adventure *game, cok_character *c, uint8_t spell, bool ai, bool *done)
{
    cok_combat *combat = &game->combat;
    unsigned left = (unsigned)(queen_spell_byte(game, spell, 7) & 3) + 1;
    unsigned picks = 0;
    while (left != 0) {
        bool found;
        cok_combat_aimed res;
        if (!pick(game, c, spell, ai, 0, &found, &res)) return false;
        if (game->vm.abort) return true;
        if (!found) break;
        cok_character *who = combatant_of(game, res.target);
        if (who == NULL) return undefined(game, "a target picked with no record (52db:1e78)");
        bool again = listed_in(game, picks, who) ||
                     ((spell == 0x17 || spell == 0x31) && who == c);
        if (!again) {
            game->spell_targets[++picks] = who;
            --left;
            game->spell_x = cok_combat_x(combat, who->record);
            game->spell_y = cok_combat_y(combat, who->record);
        }
        if (again && !ai) queen_bar_message(game, who == c ? cast_self : cast_already);
        if (again && ai) --left;
        if (!cok_arena_redraw_cell(game, cok_combat_x(combat, who->record),
                                   cok_combat_y(combat, who->record)))
            return false;
    }
    game->spell_target_count = (uint8_t)picks;
    if (picks == 0) {
        *done = false;
        return true;
    }
    game->spell_x = cok_combat_x(combat, game->spell_targets[picks]->record);
    game->spell_y = cok_combat_y(combat, game->spell_targets[picks]->record);
    return true;
}

/* 52db:1f78 (spell, ai, done): the targets of a battle's spell by its +7 kind: the caster alone (0),
 * the 5 kind, the area (8 to 0x0e, picked as empty too), Silence (0x0f: one target or the area by
 * +7 >> 4), and the one-target kinds. done is set (true at the start) and cleared where no target
 * is picked. Entry 0 alone is cleared; a stale entry past the count is kept (as the original). */
bool queen_targets(cok_adventure *game, cok_character *c, uint8_t spell, bool ai, bool *done)
{
    cok_combat *combat = &game->combat;
    game->spell_targets[0] = NULL;
    game->spell_target_count = 0;
    game->effects.rolls.images = 0;
    *done = true;
    game->spell_x = cok_combat_x(combat, c->record);
    game->spell_y = cok_combat_y(combat, c->record);
    uint8_t kind = queen_spell_byte(game, spell, 7) & 0x0f;
    if (kind == 0) {
        game->spell_targets[1] = c;
        game->spell_target_count = 1;
        return true;
    }
    if (kind == 5) return several(game, c, spell, ai); /* done stays set: it is never cleared here */
    if (kind >= 8 && kind != 0x0f) {
        bool found;
        cok_combat_aimed res;
        if (!pick(game, c, spell, ai, 1, &found, &res)) return false;
        if (game->vm.abort) return true;
        if (!found) {
            *done = false;
            return true;
        }
        return area(game, (uint16_t)area_radius(game, spell));
    }
    if (kind == 0x0f) {
        bool found;
        cok_combat_aimed res;
        if (!pick(game, c, spell, ai, 0, &found, &res)) return false;
        if (game->vm.abort) return true;
        if (!found) {
            *done = false;
            return true;
        }
        /* The caster's target (+0x0b), which the computer's aim leaves in its record too. */
        if (c->combat->target != NULL) {
            cok_character *who = combatant_of(game, c->combat->target);
            if (who == NULL) return undefined(game, "a target picked with no record (52db:1e78)");
            game->spell_targets[1] = who;
            game->spell_target_count = 1;
            return true;
        }
        return area(game, (uint16_t)(queen_spell_byte(game, spell, 7) >> 4));
    }
    return single(game, c, spell, ai, done);
}

/* 849a:21b5 (dispatch): the spell's handler (queen_cast.c's table first, then nothing: the rest
 * stop, with the descriptor's citation of the original's routine), then the guard after it (849a:000c). */
static bool handle_spell(cok_adventure *game, uint8_t spell)
{
    queen_spell_fn *handler = queen_spell_handler(spell);
    if (handler == NULL) {
        const cok_rules *rules = cok_game_rules(game);
        cok_adventure_fail(game, COK_ECL_UNPORTED, "not yet ported: %s's spell %u, %s (%s)",
                           rules->text.name, spell, rules->spells.names[spell - 1],
                           rules->spells.cast[spell].unported);
        return false;
    }
    if (!handler(game)) return false;
    return cok_combat_pending_lists(game, "dqk 849a:000c (the guard after a spell's handler)");
}

/* The flight of a spell (mode 5, not 0x7c): the missile's pictures (slot 0x13, or 0x20 for the
 * Fireball family), the arc round the caster to the spell's cell (5d71:048f, the first facing whose
 * quarter holds it), the picture by direction (51c2:0fc8, cited), the pose, the sound, the missile
 * (4 frames, delay 0x1e; none to its own cell), the burst (51c2:0def, cited), then the caster's
 * visible pose again (5837:0626). False where the run stopped. */
static bool flight(cok_adventure *game, cok_character *c, uint8_t spell)
{
    cok_combat *combat = &game->combat;
    const cok_rules *rules = cok_game_rules(game);
    bool fireball = spell == 0x2f || spell == 0x73 || spell == 0x4a || spell == 0x85;
    if (!cok_arena_missile_frames(game, fireball ? 0x20 : 0x13)) return false;
    int8_t x = cok_combat_x(combat, c->record), y = cok_combat_y(combat, c->record);
    uint8_t dir = 0;
    bool in = false;
    while (!in) {
        if (!cok_combat_in_arc(x, y, game->spell_x, game->spell_y, dir, &in))
            return undefined(game, "the arc of the caster reads past the terrain (5d71:048f)");
        if (!in && ++dir == 8)
            return undefined(game, "no facing puts the spell's cell in the caster's quarter (5d71:048f)");
    }
    if (fireball)
        return cok_adventure_not_yet(game, "the Fireball family's picture by direction (51c2:0fc8)",
                                     "dqk 51c2:0fc8");
    combat->show_actions = true; /* DS:c3f5 */
    if (!cok_arena_pose(game, c, dir, 1, false)) return false; /* 5837:09ab(rec, dir, 1, 0) */
    cok_arena_sound(game, spell == 0x2f ? 0x0c : spell == 0x33 ? 9 : 3); /* 2023:0006 */
    if (!cok_arena_missile(game, x, y, game->spell_x, game->spell_y, 4, 0x1e)) return false;
    bool burst = spell == 0x22 || spell == 0x5b || spell == 0x2f || spell == 0x73 ||
                 spell == 0x85 || spell == 0x57 || (spell == 0x33 && underwater(game));
    if (burst) return cok_adventure_not_yet(game, "a spell's burst (51c2:0def)", "dqk 51c2:0def");
    bool shown = false;
    if (!cok_combat_visible(combat, rules, c->record, false, &shown))
        return undefined(game, "a size past the footprints is read after them (5837:0626)");
    if (shown) {
        uint8_t facing = c->combat->facing;
        if (!cok_arena_pose(game, c, facing, 1, true) || !cok_arena_pose(game, c, facing, 0, false))
            return false;
    }
    return true;
}

bool queen_cast(cok_adventure *game, uint8_t spell, bool ai, bool announce, bool *done)
{
    if (game->vm.mode != 5)
        return cok_adventure_not_yet(game, "a spell cast outside combat (camp, part 46)",
                                     "dqk 849a:145b outside combat (camp, part 46)");
    cok_character *c = caster_member(game);
    if (c == NULL) return undefined(game, "the caster is read through NULL with no character "
                                          "selected (DS:ad67)");
    const cok_rules *rules = cok_game_rules(game);
    bool item = game->effects.rolls.item != 0;
    if (announce && !item) {
        /* 7fd8:45d7 (the battle branch): "Casts a Spell" with its wait, the range line cleared, the
         * spell's name on row 23 (aim-a's cok_arena_text). */
        if (!cok_arena_say(game, c, cast_casts, 10, true)) return false;
        queen_range_line_clear(game);
        char text[48];
        snprintf(text, sizeof text, "Spell:%s", rules->spells.names[spell - 1]);
        if (!cok_arena_text(game, 0, 0x17, text, 7, 0)) return false;
    }
    bool go = true;
    while (go) {
        if (!queen_targets(game, c, spell, ai, done)) return false;
        if (game->vm.abort) return true; /* the keys ran out: the turn not ended */
        if (*done) {
            go = false;
            if (spell != 0x7c && !flight(game, c, spell)) return false;
            if (!cok_combat_reveal(game, c)) return false; /* 148f:1b2a */
            if (!item && spell > 0) cok_combat_forget(rules, c->record, spell);
            game->effects.rolls.spell = spell;
            if (!handle_spell(game, spell)) return false;
            game->effects.rolls.spell = 0;
            game->effects.rolls.images = 0;
        } else {
            /* Not done: an abort (the computer's, or Yes to "Abort Spell"), else the targets again. */
            int answer = ai ? 1 : queen_yes_no(game, cast_abort);
            if (answer < 0) return true; /* the keys ran out: the turn not ended */
            if (answer == 1) {
                queen_bar_message(game, cast_aborted);
                if (!item && spell > 0) cok_combat_forget(rules, c->record, spell);
                go = false;
            }
        }
    }
    cok_arena_clear_text(game);
    queen_range_line_clear(game);
    return cok_combat_pending_lists(game, "dqk 849a:000c (the guard after a cast)");
}

bool queen_cast_command(cok_adventure *game, cok_character *c, uint8_t spell, bool redrawn,
                        bool *done)
{
    cok_combat *combat = &game->combat;
    *done = false;
    if (spell == 0) {
        int picked = queen_spell_pick(game, c, 1);
        if (picked < 0) return game->vm.status == COK_ECL_OK; /* keys out, or another mode's stop */
        spell = (uint8_t)picked;
    }
    if (spell > 0 && queen_spell_word(game, spell, 0x0f) == 0) {
        queen_bar_message(game, cast_camp_only);
        spell = 0;
    }
    if (spell > 0 && underwater(game) &&
        (spell == 0x4a || spell == 0x09 || spell == 0x2f || spell == 0x76 || spell == 0x73 ||
         spell == 0x7c)) {
        queen_bar_message(game, words.underwater);
        spell = 0;
    }
    if (!redrawn) {
        if (!cok_arena_redraw(game)) return false;
        combat->show_actions = true; /* DS:c3f5 */
        combat->panel = true;        /* DS:c6b7 */
        if (!cok_arena_turn(game, c, 3, true) || !cok_arena_panel(game, c)) return false;
    }
    if (spell == 0) return true;
    uint8_t time = (uint8_t)(queen_spell_byte(game, spell, 0x11) / 3);
    if (time == 0) {
        /* The spell at once: queen_cast with the caller's redrawn as its ai (145b's second
         * argument is the same byte), then the turn ends whatever it gave. */
        if (!queen_cast(game, spell, redrawn, true, done)) return false;
        if (game->vm.abort) return true;
        cok_combat_end_turn(c);
        *done = true;
        return true;
    }
    *done = true;
    if (!cok_arena_say(game, c, cast_begins, 10, true)) return false;
    cok_combat_record *cr = c->combat;
    if (cr == NULL) return undefined(game, "a caster has no combat record (52db:2543)");
    cr->spell = spell;
    if (cr->initiative > (int8_t)time)
        cr->initiative = (int8_t)(cr->initiative - (int8_t)time);
    else
        cr->initiative = 1;
    /* The caster moves up the turn list past those with a lower initiative, and a tie is the 1d2's
     * (60f4:1216). */
    unsigned b = combat->turn_index;
    for (;;) {
        if (b + 1 > COK_COMBATANTS)
            return undefined(game, "a full order moves a caster past entry 72, reading DS:72ce as "
                                   "a record (52db:2543)");
        cok_character *next = combat->turn[b + 1];
        if (next == NULL) break;
        int8_t k = cok_character_combat(next)->initiative;
        if (!(k > cr->initiative ||
              (k == cr->initiative && cok_dice(&game->vm.seed, 1, 2) == 1)))
            break;
        combat->turn[b] = next;
        ++b;
    }
    combat->turn[b] = c;
    --combat->turn_index;
    return true;
}
