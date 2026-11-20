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
    SPELL_EFFECT = 0x0e,       /* the effect added, 0 none */
    SPELL_HALF_RANGE = 0x7d,   /* its range is a quarter (849a:0f34) */
    CLW = 0x03,
    MM = 0x0f,
    DEITY_WORD = 0x19b,        /* a word: 4 is the deity whose cleric adds a d8 (7fd8:00f8) */
    MAGIC_ORDER = 0x19d,
    EFFECT_96 = 0x96,          /* the effect 849a:10f9 checks on a target (with spell 0x37) */
    SPELL_37 = 0x37,
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

bool queen_spell_apply(cok_adventure *game, uint8_t spell, uint8_t value, bool on_remove,
                       uint8_t amount, uint8_t type, const char *text)
{
    cok_rolls *r = &game->effects.rolls;
    r->damage_type = amount != 0 ? type : 0;
    uint8_t n = game->spell_target_count;
    if (n == 0) return true;
    if (value == 0 && !level_of(game, spell, true, &value)) return false;
    uint8_t save_kind = (uint8_t)queen_spell_word(game, spell, SPELL_SAVE_KIND);
    uint8_t effect = queen_spell_byte(game, spell, SPELL_EFFECT);
    for (uint8_t i = 1; i <= n; ++i) {
        cok_character *t = game->spell_targets[i];
        if (t == NULL) continue;
        bool saved = false;
        if (queen_spell_word(game, spell, SPELL_SAVE_KIND) != 0) {
            const cok_character_layout *L = cok_game_rules(game)->character;
            if (cok_field_u8(L, t->record, COK_FIELD_STATUS) == STATUS_TEN)
                saved = true;
            else if (cok_character_find_effect(t, EFFECT_96) != NULL && r->spell == SPELL_37)
                saved = true;
            else
                return cok_adventure_not_yet(game, "a spell's saving throw (148f:1886)",
                                             "dqk 148f:1886 (the spell's saving throw, a band's)");
        }
        if (queen_spell_byte(game, r->spell, SPELL_RANGE) == TOUCH)
            return cok_adventure_not_yet(game, "a spell cast by touch (the hit roll, 148f:1771)",
                                         "dqk 849a:10f9 touch (148f:1771)");
        if (amount != 0 && !queen_cast_damage(game, t, amount, save_kind, saved)) return false;
        if (effect > 0) {
            uint16_t minutes;
            if (!queen_spell_duration(game, spell, &minutes) ||
                !cok_cast_add_effect(game, t, effect, minutes, value, on_remove, save_kind, saved,
                                     text))
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

bool queen_cure_light(cok_adventure *game)
{
    if (game->spell_target_count == 0) return true;
    uint8_t *caster = caster_of(game);
    if (caster == NULL) return false;
    /* The bonus first, a d8 for a caster of deity 4 (the word at +0x19b); then the heal's d8. */
    uint8_t bonus = 0;
    uint16_t deity = (uint16_t)(caster[DEITY_WORD] | caster[DEITY_WORD + 1] << 8);
    if (deity == 4) bonus = roll(game, 1, 8);
    uint8_t amount = (uint8_t)(roll(game, 1, 8) + bonus);
    cok_character *t = first_target(game);
    if (t == NULL) return false;
    bool healed = false;
    if (!cok_effects_heal(&game->effects, t, amount, false, &healed)) return effect_failed(game);
    return !healed || queen_healed(game, t);
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

queen_spell_fn *queen_spell_handler(uint8_t id)
{
    switch (id) {
    case CLW: return queen_cure_light;
    case MM: return queen_magic_missile;
    default: return NULL;
    }
}
