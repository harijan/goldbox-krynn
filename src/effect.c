#include "effect.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint16_t cok_clock_units[7] = {10, 10, 6, 24, 30, 12, 256};

/* The first failure sets the error. */
static bool fail(cok_effects *fx, const char *format, ...)
{
    if (!fx->failed) {
        va_list args;
        va_start(args, format);
        vsnprintf(fx->error, sizeof fx->error, format, args);
        va_end(args);
    }
    fx->failed = true;
    return false;
}

void cok_effects_init(cok_effects *fx, cok_ecl *vm, cok_party *party, const cok_item_types *types)
{
    memset(fx, 0, sizeof *fx);
    fx->vm = vm;
    fx->party = party;
    fx->types = types;
}

static void release(cok_effects *fx)
{
    for (size_t i = 0; i < fx->removed_count; ++i) free(fx->removed[i]);
    fx->removed_count = 0;
}

void cok_effects_free(cok_effects *fx)
{
    release(fx);
    free(fx->removed);
    fx->removed = NULL;
    fx->removed_capacity = 0;
}

/* Start and end a public call. */
static void begin(cok_effects *fx)
{
    fx->failed = false;
    fx->error[0] = '\0';
}

static bool finish(cok_effects *fx)
{
    release(fx);
    return !fx->failed;
}

static bool was_removed(const cok_effects *fx, const cok_effect *effect)
{
    for (size_t i = 0; i < fx->removed_count; ++i)
        if (fx->removed[i] == effect) return true;
    return false;
}

/* The dice (60f4:1216). */
static uint8_t roll(cok_effects *fx, uint8_t count, uint8_t sides)
{
    return cok_dice(&fx->vm->seed, count, sides);
}

/* The party member whose record is the selected character (DS:6096), or
 * NULL with an error: with none, the original reads through a NULL pointer. */
static cok_character *selected(cok_effects *fx, uint8_t id)
{
    for (size_t i = 0; i < fx->party->count; ++i)
        if (fx->party->members[i]->record == fx->vm->character) return fx->party->members[i];
    fail(fx, "effect 0x%02x reads the selected character, and none is selected", id);
    return NULL;
}

static bool remove_effect(cok_effects *fx, cok_character *character, cok_effect *effect,
                          uint8_t id);

/* Say text about c (6346:1883), or fail without the hook. */
static bool say(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler, const char *text)
{
    if (fx->say == NULL)
        return fail(fx, "effect 0x%02x (3f44:%04x) prints text, which is not ported", id, handler);
    fx->say(fx, c, text, fx->context);
    return true;
}

/* The spell table (DS:31b3, 16 bytes an id), as the stat tables hold it. */
static uint8_t spell_byte(uint8_t spell, unsigned column)
{
    uint8_t byte = 0;
    cok_ds_byte((uint16_t)(0x31b3u + 16u * spell + column), &byte);
    return byte;
}

static bool caster_level(cok_effects *fx, uint8_t spell, uint8_t *level)
{
    const uint8_t *c = fx->vm->character;
    if (c == NULL)
        return fail(fx, "the caster level of spell %u (6346:29fe) reads the selected character, "
                        "and none is selected", spell);
    uint8_t class_ = spell_byte(spell, 0);
    /* 66c2:0efb, 0 or 1, multiplies the former levels as words. */
    int former = cok_character_former_class(c) ? 1 : 0;
#define LEVEL(at) ((int)(int8_t)c[at])
    if (LEVEL(0xf9) == 0 && LEVEL(0xfe) == 0 && LEVEL(0x100) < 9 && LEVEL(0xfd) < 8) {
        *level = 6;
    } else if (class_ == 0 || class_ == 2) {
        int8_t a = (int8_t)(former * LEVEL(0x101) + LEVEL(0xf9));
        int8_t b = (int8_t)(former * LEVEL(0x108) + LEVEL(0x100) - 8);
        *level = (uint8_t)(a > b ? a : b);
    } else if (class_ == 1) {
        int8_t a = (int8_t)(former * LEVEL(0x105) + LEVEL(0xfd) - 7);
        *level = (uint8_t)(a > 0 ? a : 0);
    } else if (class_ == 3) {
        int8_t a = (int8_t)(former * LEVEL(0x106) + LEVEL(0xfe));
        int8_t b = (int8_t)(former * LEVEL(0x105) + LEVEL(0xfd) - 8);
        if (LEVEL(0xfe) > 0 && c[0x5e] != 0) {
            /* The moon of its order, word 0x4cf8 + order (2b96). */
            uint16_t moon = fx->vm->mem4b00[0x1f8 + c[0x5e]];
            if (moon == 0) --a;
            else if (moon == 2 && LEVEL(0xfe) > 5) ++a;
        }
        *level = (uint8_t)(a > b ? a : b);
    } else if (class_ == 4) {
        *level = 12;
    } else {
        return fail(fx, "spell %u is of class %u; its caster level (6346:29fe) is an "
                        "uninitialized byte", spell, class_);
    }
#undef LEVEL
    if (fx->rolls.item != 0 && class_ != 4) *level = 6;
    return true;
}

bool cok_effects_caster_level(cok_effects *fx, uint8_t spell, uint8_t *level)
{
    begin(fx);
    caster_level(fx, spell, level);
    return finish(fx);
}


/* Add an effect, or fail when out of memory. */
static bool add(cok_effects *fx, cok_character *c, uint8_t id, uint16_t duration, uint8_t value,
                bool on_remove)
{
    if (cok_character_add_effect(c, id, duration, value, on_remove) != NULL) return true;
    return fail(fx, "out of memory adding effect 0x%02x", id);
}

/* Handlers. Each takes flag 1 as removing, the effect (the holder's, for
 * an effect the party shares) and the character it acts on. */

typedef bool handler_fn(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c);

/* 3f44:009b: cancel the damage and the effect a spell is adding, if that
 * is id or id is 0. */
static void cancel(cok_effects *fx, uint8_t id)
{
    if (id == 0 || fx->rolls.pending == id) {
        fx->rolls.amount = 0;
        fx->rolls.pending = 0;
    }
}

/* Handlers of no effect: 3f44:3881 (ids 5, 0x13, 0x18, 0x5c), 04b1 (0x0c,
 * 0x26), 05bc (0x0e), 0625 (0x10), 179c (0x37), 3258 (0x5b), 3449 (0x66),
 * 363c (0x6d), 3768 (0x71), 37e8 (0x74). Other code tests for them. */
static bool h_none(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)fx, (void)removing, (void)effect, (void)c;
    return true;
}

/* 3f44:0124, 1: morale +5, to hit +1. */
static bool h_bless(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.morale = (uint8_t)(fx->rolls.morale + 5);
    ++fx->rolls.attack_roll;
    return true;
}

/* 3f44:0134, 2: morale -5 down to 0, to hit -1. */
static bool h_curse(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.morale = fx->rolls.morale < 5 ? 0 : (uint8_t)(fx->rolls.morale - 5);
    --fx->rolls.attack_roll;
    return true;
}

/* 3f44:0344 (8, 0x2d) and 0379 (9, 0x2e): when the selected character's
 * alignment (+0x10a) is one of three, saves +2 and to hit -2. The
 * selected character stands for the attacker even outside combat. */
static bool protection(cok_effects *fx, uint8_t id, uint8_t a, uint8_t b, uint8_t d)
{
    cok_character *attacker = selected(fx, id);
    if (attacker == NULL) return false;
    uint8_t alignment = attacker->record[0x10a];
    if (alignment == a || alignment == b || alignment == d) {
        fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll + 2);
        fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 2);
    }
    return true;
}

static bool h_protection_evil(cok_effects *fx, bool removing, cok_effect *effect,
                              cok_character *c)
{
    (void)removing, (void)c;
    return protection(fx, effect->id, 2, 5, 8);
}

static bool h_protection_good(cok_effects *fx, bool removing, cok_effect *effect,
                              cok_character *c)
{
    (void)removing, (void)c;
    return protection(fx, effect->id, 0, 3, 6);
}

/* 3f44:03ae, 0x0a, and 0681, 0x14 (only with flag 0): against cold, or
 * fire, half damage and saves +3. */
static bool resist(cok_effects *fx, uint8_t type)
{
    if ((fx->rolls.damage_type & type) != 0) {
        fx->rolls.amount = (uint8_t)(fx->rolls.amount >> 1);
        fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll + 3);
    }
    return true;
}

static bool h_resist_cold(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    return resist(fx, 2);
}

static bool h_resist_fire(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect, (void)c;
    return removing || resist(fx, 1);
}

/* 3f44:03cd, 0x0b, charm. Removing it puts the character back on the side
 * kept in bit 6 of the value and, if it was made an NPC (+0xe7 0xb3), back
 * to a player character. Applying it the first time needs the combat
 * record and is not ported; after that it only sets morale 100. */
static bool h_charm(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    uint8_t *r = c->record;
    if (removing) {
        r[0x18a] = (uint8_t)((effect->value & 0x40) >> 6);
        if (r[0xe7] == 0xb3) {
            r[0xe7] = 0;
            r[0x18b] = 0;
        }
        return true;
    }
    if ((effect->value & 0x20) == 0)
        return fail(fx, "effect 0x0b (3f44:03cd) taking hold needs combat, which is not ported");
    fx->rolls.morale = 100;
    return true;
}

/* 3f44:062c, 0x11, shield: AC at least 3 (60 - AC 0x39) until the stats
 * are next recomputed, saves +1, and no damage from spell 0x0f. */
static bool h_shield(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if (c->record[0x18d] < 0x39) c->record[0x18d] = 0x39;
    ++fx->rolls.save_roll;
    if (fx->rolls.spell == 0x0f) fx->rolls.amount = 0;
    return true;
}

/* 3f44:065d, 0x12, and 3328, 0x5f: on a d100 of at most percent, resist
 * the effects 0x0b and 0x35 being added; 334c, 0x60, always. */
static bool resist_charm(cok_effects *fx, uint8_t percent, uint8_t first, uint8_t second)
{
    if (percent < 100 && roll(fx, 1, 100) > percent) return true;
    cancel(fx, first);
    cancel(fx, second);
    return true;
}

static bool h_resist_charm_30(cok_effects *fx, bool removing, cok_effect *effect,
                              cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    return resist_charm(fx, 30, 0x0b, 0x35);
}

static bool h_resist_charm_90(cok_effects *fx, bool removing, cok_effect *effect,
                              cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    return resist_charm(fx, 90, 0x35, 0x0b);
}

static bool h_resist_charm(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cancel(fx, 0x0b);
    cancel(fx, 0x35);
    return true;
}

/* 3f44:07b5, 0x17, spiritual hammer, on the first hammer in the items
 * (type 6, +0x31 0x79). Removing the effect frees it without unreadying it
 * (6346:1697). Applying it, with no hammer and fewer than 16 items as last
 * counted (+0x142), creates one at the end of the items: type 6, name parts
 * 6 and 0x79 (+0x30, +0x31), bonus 1, effect 0x17 and power 0 (+0x3d,
 * +0x3e), the rest 0, and says "Gains an item". It would ready it in an
 * empty weapon slot, but its search for the new hammer steps past it, so
 * it never does. Either way the stats are then recomputed. */
static bool h_hammer(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect;
    size_t i = 0;
    while (i < c->item_count && (c->items[i][0x2e] != 6 || c->items[i][0x31] != 0x79)) ++i;
    if (removing && i < c->item_count) {
        cok_character_remove_item(c, i);
    } else if (!removing && i == c->item_count && c->record[0x142] < 16) {
        uint8_t hammer[COK_ITEM_SIZE] = {0};
        hammer[0x2e] = 6;
        hammer[0x30] = 6;
        hammer[0x31] = 0x79;
        hammer[0x32] = 1;
        hammer[0x3d] = 0x17;
        hammer[0x3e] = 0x80;
        if (!cok_character_insert_item(c, c->item_count, hammer))
            return fail(fx, "out of memory adding the hammer");
        if (!say(fx, c, 0x17, 0x07b5, "Gains an item")) return false;
    }
    char why[200];
    if (!cok_character_stats(c, fx->types, why, sizeof why))
        return fail(fx, "effect 0x17 (3f44:07b5): %s", why);
    return true;
}

/* 3f44:09b3, 0x19, invisibility: with flag 0, unless the selected character
 * (the attacker) has effect 0x18, the target cannot be targeted and the
 * attack roll is 4 lower. */
static bool h_invisible(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)c;
    if (removing) return true;
    cok_character *attacker = selected(fx, effect->id);
    if (attacker == NULL) return false;
    if (cok_character_find_effect(attacker, 0x18) == NULL) {
        fx->rolls.untargetable = 1;
        fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 4);
    }
    return true;
}

/* 3f44:0ab8, 0x1d: damage less a quarter. */
static bool h_quarter_less(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.amount = (uint8_t)(fx->rolls.amount - (fx->rolls.amount >> 2));
    return true;
}

/* 3f44:0cf0, 0x21: to hit, saves and AC (front and back) 4 worse. The AC
 * changes in place each time, until the stats are next recomputed. */
static bool h_blind(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 4);
    c->record[0x18d] = (uint8_t)(c->record[0x18d] - 4);
    c->record[0x18e] = (uint8_t)(c->record[0x18e] - 4);
    fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll - 4);
    return true;
}

/* 3f44:0f3f, 0x24: to hit and saves 4 worse. */
static bool h_minus_four(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 4);
    fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll - 4);
    return true;
}

/* 3f44:0f78, 0x27, haste: the rate doubled. The first time, bit 4 of the
 * value is set and the character "ages" a year (+0x60). */
static bool h_haste(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    if ((effect->value & 0x10) == 0) {
        effect->value = (uint8_t)(effect->value + 0x10);
        if (!say(fx, c, 0x27, 0x0f78, "ages")) return false;
        uint16_t age = (uint16_t)(c->record[0x60] | c->record[0x61] << 8);
        ++age;
        c->record[0x60] = (uint8_t)age;
        c->record[0x61] = (uint8_t)(age >> 8);
    }
    fx->rolls.rate = (uint8_t)(fx->rolls.rate << 1);
    return true;
}

/* 3f44:144c, 0x2a: half the rate. */
static bool h_slow(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.rate = (uint8_t)(fx->rolls.rate >> 1);
    return true;
}

/* 3f44:1469, 0x2b, strength drain, with either flag: unless effects are
 * being cured (DS:6b38), it adds itself again for an hour, with its
 * handler; then a strength (+0x11) above 3 loses a point, with a message,
 * which is not ported, and one of 3 or less brings effect 0x1f, if the
 * character lacks it. */
static bool h_drain(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    if (fx->rolls.curing != 0) return true; /* 3f44:00ba */
    if (c->record[0x11] > 3)
        return fail(fx, "effect 0x2b (3f44:1469) weakening the character prints text, which is "
                        "not ported");
    if (!add(fx, c, 0x2b, 0x3c, effect->value, true)) return false;
    if (cok_character_find_effect(c, 0x1f) == NULL) return add(fx, c, 0x1f, 0, 0xff, false);
    return true;
}

/* 3f44:15a3, 0x2f: an attack roll 4 lower when the selected character (the
 * attacker) has bit 2 of +0x13f. */
static bool h_against_flag4(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)c;
    cok_character *attacker = selected(fx, effect->id);
    if (attacker == NULL) return false;
    if ((attacker->record[0x13f] & 4) != 0)
        fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 4);
    return true;
}

/* 3f44:16ef, 0x31, prayer, shared by the party: to hit and saves 1 better
 * for a character on the side in bit 4 of the value (+0x18a), else 1
 * worse. */
static bool h_prayer(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    if (c->record[0x18a] == (effect->value & 0x10) >> 4) {
        ++fx->rolls.save_roll; /* 3f44:0115 */
        ++fx->rolls.attack_roll;
    } else {
        --fx->rolls.attack_roll;
        --fx->rolls.save_roll;
    }
    return true;
}

/* 3f44:173a, 0x32: saves +2 against cold; fire damage doubled when the save
 * was failed. 176b, 0x36, is the same with fire and cold swapped, but tests
 * DS:5885, set when the game was saved in the current camp, instead of the
 * save. */
static bool h_cold_resistant(cok_effects *fx, bool removing, cok_effect *effect,
                             cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if ((fx->rolls.damage_type & 2) != 0)
        fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll + 2);
    else if ((fx->rolls.damage_type & 1) != 0 && fx->rolls.save_made == 0)
        fx->rolls.amount = (uint8_t)(fx->rolls.amount << 1);
    return true;
}

static bool h_fire_resistant(cok_effects *fx, bool removing, cok_effect *effect,
                             cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if ((fx->rolls.damage_type & 1) != 0)
        fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll + 2);
    else if ((fx->rolls.damage_type & 2) != 0 && fx->rolls.saved == 0)
        fx->rolls.amount = (uint8_t)(fx->rolls.amount << 1);
    return true;
}

/* 3f44:17a3, 0x38: add effect 0x19 for a minute, value 0x0c. 3619, 0x6c:
 * add it for 255 minutes, value 0xff. Copies pile up. */
static bool h_add_invisible_1(cok_effects *fx, bool removing, cok_effect *effect,
                              cok_character *c)
{
    (void)removing, (void)effect;
    return add(fx, c, 0x19, 1, 0x0c, false);
}

static bool h_add_invisible_255(cok_effects *fx, bool removing, cok_effect *effect,
                                cok_character *c)
{
    (void)removing, (void)effect;
    return add(fx, c, 0x19, 255, 0xff, false);
}

/* 3f44:17c6, 0x39: to hit +2 and AC 2 better, in place each time. */
static bool h_plus_two(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll + 2);
    c->record[0x18d] = (uint8_t)(c->record[0x18d] + 2);
    c->record[0x18e] = (uint8_t)(c->record[0x18e] + 2);
    return true;
}

/* 3f44:17ea, 0x3a: when it ends, add 0x3b for good and 0x3c for three
 * minutes, with its handler, and remove the first of 4, 0x20, 0x30 and
 * 0x39. */
static bool h_death_throes(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect;
    if (!removing) return true;
    static const uint8_t ids[] = {0x04, 0x20, 0x30, 0x39};
    if (!add(fx, c, 0x3b, 0, 0xff, false) || !add(fx, c, 0x3c, 3, 0xff, true)) return false;
    for (size_t i = 0; i < sizeof ids; ++i)
        if (!remove_effect(fx, c, NULL, ids[i])) return false;
    return true;
}

/* 3f44:1891, 0x3b: no damage, and no effect from a spell. */
static bool h_immune(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cancel(fx, 0);
    fx->rolls.amount = 0;
    return true;
}

/* 3f44:1a72, 0x3d, fire resistance: against fire, 2 less damage a die, to
 * no less than a point a die; saves +4; and unless magical, no damage. With
 * 1 die and 1 damage the subtraction wraps to 255. */
static bool h_fire_resistance(cok_effects *fx, bool removing, cok_effect *effect,
                              cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cok_rolls *r = &fx->rolls;
    if ((r->damage_type & 1) == 0) return true;
    for (unsigned i = 1; i <= r->dice; ++i) {
        r->amount = (uint8_t)(r->amount - 2);
        if (r->amount < r->dice) r->amount = r->dice;
    }
    r->save_roll = (uint8_t)(r->save_roll + 4);
    if ((r->damage_type & 8) == 0) cancel(fx, 0);
    return true;
}

/* 3f44:1b18, 0x3f, minor globe: no damage or effect from a spell of a
 * level (DS:31b4 + 16 * spell, signed) below 4. */
static bool h_minor_globe(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if (fx->rolls.spell == 0) return true;
    uint8_t level;
    uint16_t at = (uint16_t)(0x31b4 + 16 * fx->rolls.spell);
    if (!cok_ds_byte(at, &level))
        return fail(fx, "effect 0x3f reads DS:%04x, past the original's initialized data", at);
    if ((int8_t)level < 4) cancel(fx, 0);
    return true;
}

/* 3f44:2665, 0x49: against damage of type 0x20, none, and a message, which
 * is not ported. */
static bool h_unaffected(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if ((fx->rolls.damage_type & 0x20) != 0)
        return fail(fx, "effect 0x49 (3f44:2665) prints text, which is not ported");
    return true;
}

/* 3f44:29d5, 0x4d, berserk: the character comes under the computer's
 * control (+0x18b) as an NPC (+0xe7 0xb3, or 0xb2 for another NPC); in
 * combat it then picks a target, which is not ported. Removing it gives a
 * player character back, and puts any character on the party's side. */
static bool h_berserk(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect;
    uint8_t *r = c->record;
    if (removing) {
        if (r[0xe7] == 0xb3) {
            r[0xe7] = 0;
            r[0x18b] = 0;
        }
        r[0x18a] = 0;
        return true;
    }
    r[0x18b] = 1;
    r[0xe7] = r[0xe7] <= 0x7f || r[0xe7] == 0xb3 ? 0xb3 : 0xb2;
    if (fx->vm->mode == 5)
        return fail(fx, "effect 0x4d (3f44:29d5) picks a target in combat, which is not ported");
    return true;
}

/* 3f44:320f, 0x59, displacement: the first attack roll on the character
 * each combat misses. Bit 4 of the value says it has; a roll of 0 in round
 * 0, as combat starts, clears it. */
static bool h_displacement(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)c;
    if (fx->rolls.round == 0 && fx->rolls.attack_roll == 0) {
        effect->value &= 0x0f;
    } else if ((effect->value & 0x10) == 0) {
        fx->rolls.attack_roll = 0xff;
        effect->value |= 0x10;
    }
    return true;
}

/* 3f44:13a9: what the character strikes with: the readied weapon (slot 0,
 * +0x147), or what 6346:3111 picks for it by its type's flags (byte 14):
 * itself if thrown (0x10); for a missile weapon (8), the readied arrows
 * (1, slot 11) or quarrels (0x80, slot 12), even if there are none, in
 * which case the weapon. NULL with none, or with fx->failed set for an
 * item type past ITEMS. */
static const uint8_t *striking_item(cok_effects *fx, const cok_character *c)
{
    size_t weapon = c->slots[0];
    if (weapon == 0 || weapon > c->item_count) return NULL;
    uint8_t type = c->items[weapon - 1][0x2e];
    if (type >= COK_ITEM_TYPES) {
        fail(fx, "item type %u is past the %d ITEMS is read into", type, COK_ITEM_TYPES);
        return NULL;
    }
    uint8_t flags = fx->types->type[type][14];
    size_t item = 0;
    if ((flags & 0x10) != 0) item = weapon;
    if ((flags & 8) != 0) {
        if ((flags & 1) != 0) item = c->slots[11];
        if ((flags & 0x80) != 0) item = c->slots[12];
    }
    if (item == 0 || item > c->item_count) item = weapon;
    return c->items[item - 1];
}

/* The kind of damage of the item the selected character (the attacker)
 * strikes with, byte 7 of its type (DS:588d): 0x80 blunt, 1 piercing, 0
 * edged. Sets *item NULL for none; fails with no character selected. */
static bool striking_kind(cok_effects *fx, uint8_t id, const uint8_t **item, uint8_t *kind)
{
    cok_character *attacker = selected(fx, id);
    if (attacker == NULL) return false;
    *item = striking_item(fx, attacker);
    if (fx->failed) return false;
    if (*item == NULL) return true;
    if ((*item)[0x2e] >= COK_ITEM_TYPES)
        return fail(fx, "item type %u is past the %d ITEMS is read into", (*item)[0x2e],
                    COK_ITEM_TYPES);
    *kind = fx->types->type[(*item)[0x2e]][7];
    return true;
}

/* 3f44:325f, 0x5d: half damage from blunt or piercing items. */
static bool h_half_blunt(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)c;
    const uint8_t *item;
    uint8_t kind = 0;
    if (!striking_kind(fx, effect->id, &item, &kind)) return false;
    if (item != NULL && (kind & 0x81) != 0) fx->rolls.amount = (uint8_t)(fx->rolls.amount >> 1);
    return true;
}

/* 3f44:33a7, 0x64: half damage from edged or piercing items. */
static bool h_half_edged(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)c;
    const uint8_t *item;
    uint8_t kind = 0;
    if (!striking_kind(fx, effect->id, &item, &kind)) return false;
    if (item != NULL && (kind == 0 || (kind & 1) != 0))
        fx->rolls.amount = (uint8_t)(fx->rolls.amount >> 1);
    return true;
}

/* 3f44:3406, 0x65: holy water (a readied weapon of type 0x36, not what
 * 3f44:13a9 picks) deals 1d6 + 1 (60f4:1261, which sets DS:6b34). */
static bool h_holy_water(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)c;
    cok_character *attacker = selected(fx, effect->id);
    if (attacker == NULL) return false;
    size_t weapon = attacker->slots[0];
    if (weapon == 0 || weapon > attacker->item_count || attacker->items[weapon - 1][0x2e] != 0x36)
        return true;
    fx->rolls.amount = (uint8_t)(cok_dice_count(&fx->vm->seed, 1, 6, &fx->rolls.dice) + 1);
    return true;
}

/* 3f44:3450, 0x67: no damage from an attacker of a player race (+0x5a below
 * 7) or below level 4 (+0xd6) unless it strikes with an item with a bonus
 * (+0x32, any but 0). */
static bool h_magic_weapons(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)c;
    cok_character *attacker = selected(fx, effect->id);
    if (attacker == NULL) return false;
    const uint8_t *item = striking_item(fx, attacker);
    if (fx->failed) return false;
    if ((item == NULL || item[0x32] == 0) &&
        ((int8_t)attacker->record[0x5a] < 7 || (int8_t)attacker->record[0xd6] < 4))
        fx->rolls.amount = 0;
    return true;
}

/* 3f44:32a8, 0x5e: saving throws of types 0, 2 and 4 better by the
 * constitution (+0x19): 1 for 4-6, 2 for 7-10, 3 for 11-13, 4 for 14-17 and
 * 5 for 18-20. For other constitutions the original adds an uninitialized
 * local, whatever earlier calls left on the stack; the port fails. */
static bool h_constitution_save(cok_effects *fx, bool removing, cok_effect *effect,
                                cok_character *c)
{
    (void)removing, (void)effect;
    uint8_t type = fx->rolls.save_type;
    if (type != 4 && type != 2 && type != 0) return true;
    uint8_t con = c->record[0x19], bonus;
    if (con >= 4 && con <= 6)
        bonus = 1;
    else if (con >= 7 && con <= 10)
        bonus = 2;
    else if (con >= 11 && con <= 13)
        bonus = 3;
    else if (con >= 14 && con <= 17)
        bonus = 4;
    else if (con >= 18 && con <= 20)
        bonus = 5;
    else
        return fail(fx, "effect 0x5e (3f44:32a8) adds an uninitialized local for constitution "
                        "%u", con);
    fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll + bonus);
    return true;
}

/* 3f44:3361, 0x61: resist paralysis (effect 0x34) being added. */
static bool h_free_action(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cancel(fx, 0x34);
    return true;
}

/* 3f44:336f, 0x62: no cold damage. */
static bool h_cold_immune(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if ((fx->rolls.damage_type & 2) != 0) cancel(fx, 0);
    return true;
}

/* 3f44:3386, 0x63: resist effects 0x37 and 0x34 being added, and always
 * make saving throws of type 0. */
static bool h_poison_immune(cok_effects *fx, bool removing, cok_effect *effect,
                            cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cancel(fx, 0x37);
    cancel(fx, 0x34);
    if (fx->rolls.save_type == 0) fx->rolls.save_roll = 100;
    return true;
}

/* 3f44:34f9, 0x6b. Removing it gives a player character made an NPC back
 * (+0xe7 0xb3 to 0) and sets the side (+0x18a) to the value. Applying it
 * turns the character on the nearest combatant, which is not ported. */
static bool h_turn(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    if (!removing)
        return fail(fx, "effect 0x6b (3f44:34f9) needs combat, which is not ported");
    if (c->record[0xe7] == 0xb3) c->record[0xe7] = 0;
    c->record[0x18a] = effect->value;
    return true;
}

/* 3f44:3643, 0x6e: when it ends, +0x5f becomes (knight level + former
 * knight level, if it may be used, - 1) / 5 + 1. Below 1 the division
 * overflows: runtime error 200. */
static bool h_knight(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect;
    if (!removing) return true;
    uint8_t *r = c->record;
    int16_t former = cok_character_former_class(r) ? (int8_t)r[0x108] : 0;
    int16_t levels = (int16_t)(former + (int8_t)r[0x100] - 1);
    if (levels < 0) return fail(fx, "effect 0x6e (3f44:3643) overflows a division (runtime error 200)");
    r[0x5f] = (uint8_t)(levels / 5 + 1);
    return true;
}

/* 3f44:386a, 0x76, and 3876, 0x77: to hit 2 and 1 worse. */
static bool h_minus_two(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 2);
    return true;
}

static bool h_minus_one(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    --fx->rolls.attack_roll;
    return true;
}

/* The handler table the original fills at DS:6b94 (3f44:38ea), by effect
 * id: the handler's address in overlay 3f44, 0 for none, and the port, or
 * NULL with what it needs. */
typedef struct {
    uint16_t address;
    handler_fn *fn;
    const char *needs;
    uint16_t segment; /* 0 for 3f44; 5b04 fills a few at startup (5b04:58ed). */
} handler;

#define COMBAT "combat"
#define TEXT "text output"
#define DAMAGE "dealing damage, with text"
#define ATTACK "a monster's attack in combat"

static const handler handlers[COK_EFFECT_IDS] = {
    [0x01] = {0x0124, h_bless, NULL},
    [0x02] = {0x0134, h_curse, NULL},
    [0x03] = {0x016a, NULL, COMBAT " and " TEXT},
    [0x04] = {0x4ab8, NULL, ATTACK, 0x5b04},
    [0x05] = {0x3881, h_none, NULL},
    [0x06] = {0x546e, NULL, ATTACK, 0x5b04},
    [0x07] = {0x0208, NULL, COMBAT},
    [0x08] = {0x0344, h_protection_evil, NULL},
    [0x09] = {0x0379, h_protection_good, NULL},
    [0x0a] = {0x03ae, h_resist_cold, NULL},
    [0x0b] = {0x03cd, h_charm, NULL},
    [0x0c] = {0x04b1, h_none, NULL},
    [0x0d] = {0x04c2, NULL, COMBAT " and " TEXT},
    [0x0e] = {0x05bc, h_none, NULL},
    [0x0f] = {0x05c3, NULL, DAMAGE},
    [0x10] = {0x0625, h_none, NULL},
    [0x11] = {0x062c, h_shield, NULL},
    [0x12] = {0x065d, h_resist_charm_30, NULL},
    [0x13] = {0x3881, h_none, NULL},
    [0x14] = {0x0681, h_resist_fire, NULL},
    [0x15] = {0x06b2, NULL, COMBAT " and " TEXT},
    [0x16] = {0x0725, NULL, "killing the character, with text"},
    [0x17] = {0x07b5, h_hammer, NULL},
    [0x18] = {0x3881, h_none, NULL},
    [0x19] = {0x09b3, h_invisible, NULL},
    [0x1a] = {0x09e8, NULL, COMBAT},
    [0x1b] = {0x00f7, NULL, COMBAT},
    [0x1c] = {0x0a2f, NULL, COMBAT " and " TEXT},
    [0x1d] = {0x0ab8, h_quarter_less, NULL},
    [0x1e] = {0x0ae0, NULL, COMBAT " and " TEXT},
    [0x1f] = {0x00f7, NULL, COMBAT},
    [0x20] = {0x0bc9, NULL, COMBAT " and " TEXT},
    [0x21] = {0x0cf0, h_blind, NULL},
    [0x22] = {0x0d19, NULL, DAMAGE},
    [0x23] = {0x0d7c, NULL, COMBAT " and " TEXT},
    [0x24] = {0x0f3f, h_minus_four, NULL},
    [0x25] = {0x0f50, NULL, COMBAT},
    [0x26] = {0x04b1, h_none, NULL},
    [0x27] = {0x0f78, h_haste, NULL},
    [0x28] = {0x0ff7, NULL, COMBAT " and " TEXT},
    [0x29] = {0x1412, NULL, COMBAT " and " TEXT},
    [0x2a] = {0x144c, h_slow, NULL},
    [0x2b] = {0x1469, h_drain, NULL},
    [0x2c] = {0x1504, NULL, DAMAGE},
    [0x2d] = {0x0344, h_protection_evil, NULL},
    [0x2e] = {0x0379, h_protection_good, NULL},
    [0x2f] = {0x15a3, h_against_flag4, NULL},
    [0x30] = {0x15cd, NULL, COMBAT " and " TEXT},
    [0x31] = {0x16ef, h_prayer, NULL},
    [0x32] = {0x173a, h_cold_resistant, NULL},
    [0x33] = {0x00f7, NULL, COMBAT},
    [0x34] = {0x00f7, NULL, COMBAT},
    [0x35] = {0x00f7, NULL, COMBAT},
    [0x36] = {0x176b, h_fire_resistant, NULL},
    [0x37] = {0x179c, h_none, NULL},
    [0x38] = {0x17a3, h_add_invisible_1, NULL},
    [0x39] = {0x17c6, h_plus_two, NULL},
    [0x3a] = {0x17ea, h_death_throes, NULL},
    [0x3b] = {0x1891, h_immune, NULL},
    [0x3c] = {0x18b9, NULL, COMBAT " and " TEXT},
    [0x3d] = {0x1a72, h_fire_resistance, NULL},
    [0x3e] = {0x1acd, NULL, "healing, with text"},
    [0x3f] = {0x1b18, h_minor_globe, NULL},
    [0x40] = {0x1c21, NULL, COMBAT " and " TEXT},
    [0x41] = {0x1c34, NULL, COMBAT " and " TEXT},
    [0x42] = {0x1c54, NULL, COMBAT " and " TEXT},
    [0x43] = {0x1dc5, NULL, COMBAT " and " TEXT},
    [0x44] = {0x1f97, NULL, COMBAT},
    [0x45] = {0x200b, NULL, COMBAT " and " TEXT},
    [0x46] = {0x2037, NULL, COMBAT " and " TEXT},
    [0x47] = {0x23a4, NULL, COMBAT " and " TEXT},
    [0x48] = {0x24f4, NULL, COMBAT " and " TEXT},
    [0x49] = {0x2665, h_unaffected, NULL},
    [0x4a] = {0x26a5, NULL, "spells"},
    [0x4b] = {0x26bf, NULL, COMBAT},
    [0x4c] = {0x272a, NULL, COMBAT " and " TEXT},
    [0x4d] = {0x29d5, h_berserk, NULL},
    [0x4e] = {0x4dc2, NULL, ATTACK, 0x5b04},
    [0x4f] = {0x2b5f, NULL, COMBAT " and " TEXT},
    [0x50] = {0x2cad, NULL, COMBAT " and " TEXT},
    [0x51] = {0x3009, NULL, COMBAT " and " TEXT},
    [0x52] = {0x303c, NULL, COMBAT " and " TEXT},
    [0x53] = {0x546e, NULL, ATTACK, 0x5b04},
    [0x54] = {0x4eea, NULL, ATTACK, 0x5b04},
    [0x55] = {0x50cf, NULL, ATTACK, 0x5b04},
    [0x56] = {0x31bc, NULL, COMBAT " and " TEXT},
    [0x57] = {0x31cf, NULL, COMBAT " and " TEXT},
    [0x58] = {0x31e2, NULL, COMBAT " and " TEXT},
    [0x59] = {0x320f, h_displacement, NULL},
    [0x5a] = {0x5227, NULL, ATTACK, 0x5b04},
    [0x5b] = {0x3258, h_none, NULL},
    [0x5c] = {0x3881, h_none, NULL},
    [0x5d] = {0x325f, h_half_blunt, NULL},
    [0x5e] = {0x32a8, h_constitution_save, NULL},
    [0x5f] = {0x3328, h_resist_charm_90, NULL},
    [0x60] = {0x334c, h_resist_charm, NULL},
    [0x61] = {0x3361, h_free_action, NULL},
    [0x62] = {0x336f, h_cold_immune, NULL},
    [0x63] = {0x3386, h_poison_immune, NULL},
    [0x64] = {0x33a7, h_half_edged, NULL},
    [0x65] = {0x3406, h_holy_water, NULL},
    [0x66] = {0x3449, h_none, NULL},
    [0x67] = {0x3450, h_magic_weapons, NULL},
    [0x68] = {0x546e, NULL, ATTACK, 0x5b04},
    [0x69] = {0x349c, NULL, COMBAT},
    [0x6a] = {0x34db, NULL, COMBAT},
    [0x6b] = {0x34f9, h_turn, NULL},
    [0x6c] = {0x3619, h_add_invisible_255, NULL},
    [0x6d] = {0x363c, h_none, NULL},
    [0x6e] = {0x3643, h_knight, NULL},
    [0x6f] = {0x3692, NULL, COMBAT},
    [0x70] = {0x36e1, NULL, COMBAT " and " TEXT},
    [0x71] = {0x3768, h_none, NULL},
    [0x72] = {0x376f, NULL, COMBAT},
    [0x73] = {0x37b0, NULL, COMBAT},
    [0x74] = {0x37e8, h_none, NULL},
    [0x75] = {0x37fc, NULL, COMBAT " and " TEXT},
    [0x76] = {0x386a, h_minus_two, NULL},
    [0x77] = {0x3876, h_minus_one, NULL},
    /* 3f44:3888 adds or removes an item's effect; reached as an effect's
     * own handler it reads +0x3d past the 9-byte record. */
    [0x78] = {0x3888, NULL, "an item, and reads past the effect record"},
};

/* Run the handler of id (60f4:01a8). The original calls 3f44:3888 instead
 * while DS:713c is set, which only readying items does; that is not
 * ported. */
static bool call_handler(cok_effects *fx, uint8_t id, bool removing, cok_effect *effect,
                         cok_character *c)
{
    if (id >= COK_EFFECT_IDS)
        return fail(fx, "effect 0x%02x is past the handler table (DS:6b94)", id);
    const handler *h = &handlers[id];
    if (h->address == 0)
        return fail(fx, "effect 0x%02x has no handler; the original calls 0000:0000", id);
    if (h->fn == NULL)
        return fail(fx, "effect 0x%02x (%04x:%04x) needs %s, which is not ported", id,
                    h->segment != 0 ? h->segment : 0x3f44, h->address, h->needs);
    return h->fn(fx, removing, effect, c);
}

/* 60f4:1543: the strength an effect's value gives, 18 with an exceptional
 * strength of value - 1 up to 101 (so 0 gives 18/255), above that value -
 * 100. */
static void effect_strength(const cok_effect *effect, uint8_t *strength, uint8_t *exceptional)
{
    *exceptional = 0;
    *strength = effect->value & 0x7f;
    if (*strength <= 0x65) {
        *exceptional = (uint8_t)(*strength - 1);
        *strength = 18;
    } else {
        *strength = (uint8_t)(*strength - 100);
    }
}

/* 60f4:15db: keep the better of the strength so far and a candidate; an
 * 18 with a higher exceptional strength counts as better even than 19 up. */
static void better(uint8_t *strength, uint8_t *exceptional, uint8_t candidate,
                   uint8_t candidate_exceptional)
{
    if (candidate > *strength || (candidate == 18 && candidate_exceptional > *exceptional)) {
        *strength = candidate;
        *exceptional = candidate_exceptional;
    }
}

/* 60f4:1628: add a class's hit points for level to *hp, as a byte: the
 * level is capped below the class's top (DS:3903), a ranger's raised by
 * one unless it has a former level other than its own (+0xd7), and
 * fighters, rangers and knights get 1-7 a level by constitution 15-25,
 * others 1 for 15 and 2 above it. */
static const uint8_t class_top[8] = {10, 15, 10, 10, 11, 12, 11, 10}; /* DS:3903 */

static void class_hit_points(const uint8_t *c, unsigned klass, uint8_t level,
                             uint8_t constitution, uint8_t *hp)
{
    if (class_top[klass] <= level) level = (uint8_t)(class_top[klass] - 1);
    if (klass == 4 && (c[0xd7] == 0 || c[0x105] == c[0xd7])) ++level;
    unsigned per = 0;
    if (klass == 2 || klass == 4 || klass == 7) {
        if (constitution >= 15 && constitution <= 19) per = constitution - 14u;
        else if (constitution == 20) per = 5;
        else if (constitution >= 21 && constitution <= 23) per = 6;
        else if (constitution >= 24 && constitution <= 25) per = 7;
    } else if (constitution > 15) {
        per = 2;
    } else if (constitution == 15) {
        per = 1;
    }
    *hp = (uint8_t)(*hp + level * per);
}

/* 60f4:1743: the base score (+0x10 + 2 * stat; +0x1d for exceptional
 * strength) changed by the readied items' powers (+0x3e 0x80 + power, +0x3d
 * its kind) and effects, into the current one. Intelligence (1) and wisdom
 * (2) are computed and not stored. */
static bool ability(cok_effects *fx, cok_character *ch, unsigned stat)
{
    uint8_t *c = ch->record;
    uint8_t score = c[0x10 + stat * 2], exceptional = c[0x1d];
    uint8_t candidate = 0, candidate_exceptional = 0, fixed = 0xff;
    for (size_t i = 0; i < ch->item_count; ++i) {
        const uint8_t *item = ch->items[i];
        if (item[0x3e] <= 0x80 || item[0x34] == 0) continue;
        uint8_t power = item[0x3e] & 0x7f, kind = item[0x3d];
        switch (stat) {
        case 0:
            if (power == 3) {
                candidate = 18;
                candidate_exceptional = 100;
            } else if (power == 5) {
                if (kind == 0) {
                    candidate = 18;
                    candidate_exceptional = 100;
                } else if (kind <= 6) {
                    candidate = (uint8_t)(18 + kind);
                }
            } else if (power == 8) {
                if (c[0x10] < 18 && kind == 0) {
                    candidate = (uint8_t)(c[0x10] + 1);
                    candidate_exceptional = 0;
                }
            } else if (power == 0x0d) {
                fixed = 3;
            }
            better(&score, &exceptional, candidate, candidate_exceptional);
            break;
        case 1: /* 0x0c and 0x0d would set 7 and 3, but nothing is stored. */
        case 2:
            break;
        case 3:
            if (power == 2) score = (uint8_t)(score + (c[0x16] <= 6 ? 4 : c[0x16] <= 13 ? 2 : 1));
            else if (power == 8 && c[0x16] < 18 && kind == 3) ++score;
            else if (power == 10) score = (uint8_t)(score - 2);
            break;
        case 4:
            if (power == 6 || (power == 8 && c[0x18] < 18 && kind == 4)) ++score;
            break;
        default:
            if (power == 6) --score;
            else if (power == 8 && c[0x1a] < 18 && kind == 5) ++score;
            break;
        }
    }
    if (stat == 1 || stat == 2) return true;
    if (stat == 3) {
        c[0x17] = score;
        return true;
    }
    if (stat == 5) {
        const cok_effect *effect = cok_character_find_effect(ch, 0x0e);
        if (effect != NULL) score = (uint8_t)(score + effect->value);
        c[0x1b] = score;
        return true;
    }
    if (stat == 4) {
        /* The maximum hit points from +0x11b (the first level's) and each
         * class's levels, former (+0x101) and current (+0xf9, above +0xd7,
         * capped by DS:3903), divided by the number of classes with a
         * level; the hit points change by as much. */
        uint8_t old = c[0x62], hp = 0, classes = 0;
        c[0x62] = c[0x11b];
        for (unsigned k = 0; k < 8; ++k) {
            if (c[0x101 + k] > 0) class_hit_points(c, k, c[0x101 + k], score, &hp);
            uint8_t level = c[0xf9 + k];
            if (level > 0) ++classes;
            if (class_top[k] < level) level = class_top[k];
            if (level > c[0xd7]) class_hit_points(c, k, (uint8_t)(level - c[0xd7]), score, &hp);
        }
        if (classes == 0)
            return fail(fx, "constitution's hit points (60f4:1743) divide by no classes, runtime "
                            "error 200");
        c[0x62] = (uint8_t)(c[0x62] + hp / classes);
        if (c[0x62] > old) c[0x197] = (uint8_t)(c[0x197] + (c[0x62] - old));
        if (c[0x62] < old) {
            if (c[0x197] > old - c[0x62]) c[0x197] = (uint8_t)(c[0x197] - (old - c[0x62]));
            else c[0x197] = 0;
        }
        c[0x19] = score;
        if (c[0x19] >= 20) {
            if (cok_character_find_effect(ch, 0x3e) == NULL &&
                cok_character_add_effect(ch, 0x3e, 0x3c, 0xff, true) == NULL)
                return fail(fx, "out of memory adding effect 0x3e");
            return true;
        }
        return remove_effect(fx, ch, NULL, 0x3e);
    }
    const cok_effect *effect = cok_character_find_effect(ch, 0x26);
    if (effect != NULL) {
        effect_strength(effect, &candidate, &candidate_exceptional);
        if (score < 19 && exceptional < 100) {
            candidate = (uint8_t)(candidate + score);
            if (candidate > 18) {
                /* Fighters, rangers and knights, now or before, get the
                 * excess as exceptional strength, added to the current
                 * one each time. */
                static const size_t classes[] = {0xfb, 0x103, 0x100, 0x108, 0xfd, 0x105};
                bool fighter = false;
                for (size_t k = 0; k < sizeof classes / sizeof *classes; ++k)
                    if ((int8_t)c[classes[k]] > 0) fighter = true;
                if (fighter) {
                    candidate_exceptional = (uint8_t)((candidate - 18) * 10 + c[0x1c]);
                    if (candidate_exceptional > 100) candidate_exceptional = 100;
                }
                candidate = 18;
            }
        }
        better(&score, &exceptional, candidate, candidate_exceptional);
    }
    static const uint8_t others[] = {0x71, 0x0c};
    for (size_t k = 0; k < sizeof others; ++k) {
        effect = cok_character_find_effect(ch, others[k]);
        if (effect == NULL) continue;
        effect_strength(effect, &candidate, &candidate_exceptional);
        better(&score, &exceptional, candidate, candidate_exceptional);
    }
    if (fixed != 0xff) {
        c[0x11] = fixed;
        c[0x1c] = 0;
    } else {
        c[0x11] = score;
        c[0x1c] = exceptional;
    }
    return true;
}

bool cok_effects_ability(cok_effects *fx, cok_character *character, unsigned stat)
{
    begin(fx);
    if (stat > 5)
        fail(fx, "ability %u is past charisma", stat);
    else
        ability(fx, character, stat);
    return finish(fx);
}

/* 60f4:01e9. The effect is kept in fx->removed until the public call
 * returns. */
static bool remove_effect(cok_effects *fx, cok_character *c, cok_effect *effect, uint8_t id)
{
    if (effect == NULL) effect = cok_character_find_effect(c, id);
    if (effect == NULL) return true;
    if (effect->on_remove && !call_handler(fx, id, true, effect, c)) return false;
    cok_effect **link = &c->effects;
    while (*link != NULL && *link != effect) link = &(*link)->next;
    if (*link == NULL)
        return fail(fx, "effect 0x%02x is removed but not in the list; the original writes to "
                        "0000:0005", effect->id);
    if (fx->removed_count == fx->removed_capacity) {
        size_t capacity = fx->removed_capacity == 0 ? 16 : fx->removed_capacity * 2;
        cok_effect **bigger = realloc(fx->removed, capacity * sizeof *bigger);
        if (bigger == NULL) return fail(fx, "out of memory");
        fx->removed = bigger;
        fx->removed_capacity = capacity;
    }
    *link = effect->next;
    fx->removed[fx->removed_count++] = effect;
    if (id == 0x0e) return ability(fx, c, 5);
    if (id == 0x0c || id == 0x26) return ability(fx, c, 0);
    return true;
}

bool cok_effects_remove(cok_effects *fx, cok_character *character, cok_effect *effect,
                        uint8_t id)
{
    begin(fx);
    remove_effect(fx, character, effect, id);
    return finish(fx);
}

/* Run the handler of id for target (60f4:0352): its own effect, or for an
 * effect the party shares, the first in the whole list, a monster's
 * too. In combat only a record in range counts, which needs the combat
 * map and is not ported. */
static bool dispatch_id(cok_effects *fx, cok_character *target, uint8_t id)
{
    cok_effect *effect = cok_character_find_effect(target, id);
    if (effect != NULL) return call_handler(fx, id, false, effect, target);
    if (id != 0x15 && id != 0x2d && id != 0x2e && id != 0x31) return true; /* 60f4:0332 */
    for (size_t i = 0; i < fx->party->count; ++i) {
        effect = cok_character_find_effect(fx->party->members[i], id);
        if (effect == NULL) continue;
        if (fx->vm->mode == 5)
            return fail(fx, "effect 0x%02x shared in combat needs the combat map, which is not "
                            "ported", id);
        return call_handler(fx, id, false, effect, target);
    }
    return true;
}

/* The effect ids each event runs, in order (60f4:057c). */
static const uint8_t *const events[0x19] = {
    [0x01] = (const uint8_t[]){0x25, 0x19, 0},
    [0x02] = (const uint8_t[]){0x58, 0x57, 0x56, 0x51, 0x45, 0x40, 0x41, 0},
    [0x03] = (const uint8_t[]){0x45, 0x40, 0x41, 0x51, 0x56, 0x57, 0},
    [0x04] = (const uint8_t[]){0x07, 0x1d, 0x4b, 0x69, 0x72, 0x73, 0x75, 0},
    [0x05] = (const uint8_t[]){0x3b, 0x1c, 0x29, 0x64, 0x67, 0x5d, 0x65, 0x70, 0},
    [0x06] = (const uint8_t[]){0x47, 0x3b, 0x3d, 0x0a, 0x14, 0x66, 0x11, 0x1c, 0x62, 0x49, 0x3f, 0},
    [0x07] = (const uint8_t[]){0x33, 0x34, 0x35, 0x1f, 0x03, 0x1b, 0},
    [0x08] = (const uint8_t[]){0x52, 0x59, 0x38, 0},
    [0x09] = (const uint8_t[]){0x47, 0x74, 0x5c, 0x3b, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x12, 0x3f, 0},
    [0x0a] = (const uint8_t[]){0x07, 0x76, 0x77, 0x39, 0x01, 0x02, 0x21, 0x24, 0x31, 0x1a, 0x4b, 0},
    [0x0b] = (const uint8_t[]){0x21, 0x11, 0x08, 0x09, 0x2d, 0x2e, 0x1e, 0},
    [0x0c] = (const uint8_t[]){0x5e, 0x08, 0x09, 0x0a, 0x11, 0x14, 0x21, 0x24, 0x2d, 0x2e, 0x31,
                               0x3d, 0x63, 0x32, 0x36, 0},
    [0x0d] = (const uint8_t[]){0x44, 0x43, 0x0d, 0x20, 0x46, 0},
    [0x0e] = (const uint8_t[]){0x06, 0x55, 0x54, 0x53, 0x4e, 0x4a, 0x04, 0x5a, 0x68, 0},
    [0x0f] = (const uint8_t[]){0x4f, 0x4c, 0x48, 0x42, 0x30, 0x15, 0x1e, 0x0b, 0x4d, 0},
    [0x10] = (const uint8_t[]){0x19, 0x25, 0x2f, 0x59, 0},
    [0x11] = (const uint8_t[]){0x01, 0x02, 0x0b, 0},
    [0x12] = (const uint8_t[]){0x27, 0x2a, 0},
    [0x13] = (const uint8_t[]){0x17, 0x38, 0x0b, 0},
    [0x14] = (const uint8_t[]){0x32, 0x36, 0},
    [0x15] = (const uint8_t[]){0x23, 0},
    [0x18] = (const uint8_t[]){0x52, 0},
};

static bool dispatch(cok_effects *fx, cok_character *target, uint8_t event)
{
    if (event >= sizeof events / sizeof *events || events[event] == NULL) return true;
    if (event == 6 || event == 9) {
        /* 60f4:04f3, magic resistance (+0x187), when an effect is pending
         * and no damage, or magic damage: a d100 up to the resistance plus
         * 5 for each caster level of the spell (DS:6b33) below 11, as a
         * byte, resists the damage and the effect (but for 0x5b and 0x52). */
        cok_rolls *r = &fx->rolls;
        uint8_t resistance = target->record[0x187], level;
        if (resistance != 0 && r->pending != 0 && (r->amount == 0 || (r->damage_type & 8) != 0)) {
            if (!caster_level(fx, r->spell, &level)) return false;
            uint8_t chance = (uint8_t)(resistance + 5 * (11 - level));
            if (roll(fx, 1, 100) <= chance) {
                r->amount = 0;
                if (r->pending != 0x5b && r->pending != 0x52) r->pending = 0;
            }
        }
    }
    for (const uint8_t *id = events[event]; *id != 0; ++id)
        if (!dispatch_id(fx, target, *id)) return false;
    return true;
}

bool cok_effects_dispatch(cok_effects *fx, cok_character *target, uint8_t event)
{
    begin(fx);
    dispatch(fx, target, event);
    return finish(fx);
}

bool cok_effects_run(cok_effects *fx, cok_character *character, uint8_t id, cok_effect *effect)
{
    begin(fx);
    call_handler(fx, id, false, effect, character);
    return finish(fx);
}

bool cok_effects_attack(cok_effects *fx, cok_character *target, uint8_t bonus, bool *hit)
{
    begin(fx);
    *hit = false;
    cok_rolls *r = &fx->rolls;
    r->attack_roll = roll(fx, 1, 20);
    if ((int8_t)r->attack_roll > 1) {
        if (r->attack_roll == 20) r->attack_roll = 100;
        if (dispatch(fx, target, 0x10))
            *hit = (int8_t)r->attack_roll >= 0 &&
                   (int)(int8_t)r->attack_roll + bonus > target->record[0x18d];
    }
    return finish(fx);
}

bool cok_effects_save(cok_effects *fx, cok_character *target, uint8_t type, uint8_t bonus,
                      bool *made)
{
    begin(fx);
    cok_rolls *r = &fx->rolls;
    const uint8_t *c = target->record;
    r->save_made = 1;
    r->save_roll = roll(fx, 1, 20);
    if (r->save_roll == 1) {
        r->save_made = 0;
    } else if (r->save_roll != 20) {
        /* A mage of an order of magic (+0x5e) gets -1 or +1 from the word
         * at 0x4bf8 + order (the byte sum wraps): 0 or 2. */
        if ((int8_t)c[0xfe] > 0 && c[0x5e] != 0) {
            uint16_t moon = fx->vm->mem4b00[(uint8_t)(c[0x5e] + 0xf8)];
            if (moon == 0) --bonus;
            else if (moon == 2) ++bonus;
        }
        r->save_roll = (uint8_t)(r->save_roll + (int8_t)c[0x17c] + (int8_t)bonus);
        r->save_type = type;
        if (dispatch(fx, target, 0x0c)) {
            int at = 0xd0 + (int8_t)type;
            if (at < 0 || at >= COK_CHARACTER_SIZE)
                fail(fx, "saving throw type %u is outside the record", type);
            else
                r->save_made = c[at] <= r->save_roll;
        }
    }
    *made = r->save_made != 0;
    return finish(fx);
}

/* Count down member's effects by minutes (57e4:0171), setting *timed if
 * one has time left. The walk stops after the effect that was last when it
 * began (02d3-02e3); the rest, added on the way, only set *timed. When the
 * effect that ends is second in the list and was not that last one, the
 * walk starts again from the first, which loses the minutes again; when it
 * was the last one, the walk has already stopped. */
static bool count_down(cok_effects *fx, cok_character *member, uint8_t minutes, uint8_t *timed)
{
    cok_effect *current = member->effects, *last = current, *before = current;
    if (last != NULL)
        while (last->next != NULL) last = last->next;
    for (bool done = false; current != NULL && !done;) {
        if (was_removed(fx, current))
            return fail(fx, "a handler removed effect 0x%02x as it was counted down; the original "
                            "reads it after freeing it", current->id);
        if (current == last) done = true;
        if (current->duration == 0) {
            current = current->next;
        } else if (minutes < current->duration) {
            current->duration = (uint16_t)(current->duration - minutes);
            *timed = 1;
            current = current->next;
        } else {
            cok_effect *next = current->next;
            if (!remove_effect(fx, member, current, current->id)) return false;
            current = before == member->effects ? member->effects : next;
        }
        before = member->effects;
        while (before != NULL && before != current && before->next != current)
            before = before->next;
    }
    for (; current != NULL; current = current->next) {
        if (was_removed(fx, current))
            return fail(fx, "a handler removed effect 0x%02x as it was counted down; the original "
                            "reads it after freeing it", current->id);
        if (current->duration > 0) *timed = 1;
    }
    return true;
}

bool cok_effects_pass_time(cok_effects *fx, unsigned unit, unsigned count)
{
    begin(fx);
    cok_ecl *vm = fx->vm;
    if (unit > 6) {
        fail(fx, "clock unit %u is past the clock", unit);
        return finish(fx);
    }
    if (vm->mode == 2) {
        bool any = false;
        unsigned i = 1;
        do {
            if (i > COK_EFFECT_TIMED) {
                fail(fx, "a party of %u reads past DS:46fb", vm->mem7c00[0x33e]);
                return finish(fx);
            }
            if (fx->timed[i - 1] != 0) any = true;
            ++i;
        } while (!any && i <= vm->mem7c00[0x33e]);
        if (!any) return finish(fx);
    } else {
        memset(fx->timed, 1, sizeof fx->timed);
    }
    /* Into minutes, as a word; unit 0 counts as minutes. */
    uint16_t minutes = (uint8_t)count;
    for (unsigned u = unit; u > 1; --u) minutes = (uint16_t)(minutes * cok_clock_units[u - 1]);
    while (minutes > 0 && !fx->failed) {
        uint8_t step = minutes > 10 ? 10 : (uint8_t)minutes;
        for (size_t i = 0; i < fx->party->count && i < COK_EFFECT_TIMED && !fx->failed; ++i) {
            if (fx->timed[i] == 0) continue;
            fx->timed[i] = 0;
            count_down(fx, fx->party->members[i], step, &fx->timed[i]);
        }
        minutes = minutes > 10 ? (uint16_t)(minutes - 10) : 0;
    }
    return finish(fx);
}
