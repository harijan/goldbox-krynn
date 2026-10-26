#include "effect.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint16_t cok_clock_units[7] = {10, 10, 6, 24, 30, 12, 256};

/* The first failure sets the error. */
/* A named effect's or spell's id in the game (spells.h), for code that has
 * the effects (fx). The ids messages cite are Champions', with its
 * handlers' addresses. */
#define EFFECT(name) cok_effect_id(fx->rules, COK_EFFECT_##name)
#define SPELL(name) cok_spell_id(fx->rules, COK_SPELL_##name)
#define IS_EFFECT(id, name) cok_effect_is(fx->rules, id, COK_EFFECT_##name)
#define IS_SPELL(id, name) cok_spell_is(fx->rules, id, COK_SPELL_##name)
#define FIND_EFFECT(c, name) cok_character_find_named(c, fx->rules, COK_EFFECT_##name)

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

void cok_effects_init(cok_effects *fx, const cok_rules *rules, cok_ecl *vm, cok_party *party,
                      const cok_item_types *types)
{
    memset(fx, 0, sizeof *fx);
    fx->rules = rules;
    fx->vm = vm;
    fx->party = party;
    fx->types = types;
}

static void release(cok_effects *fx)
{
    for (size_t i = 0; i < fx->removed_count; ++i) free(fx->removed[i].effect);
    fx->removed_count = 0;
}

void cok_effects_free(cok_effects *fx)
{
    release(fx);
    free(fx->removed);
    fx->removed = NULL;
    fx->removed_capacity = 0;
}

/* Start and end a public call. A call a hook makes from inside another
 * keeps its failure and the effects removed until the outermost ends. */
static void begin(cok_effects *fx)
{
    if (fx->depth++ > 0) return;
    fx->failed = false;
    fx->error[0] = '\0';
}

static bool finish(cok_effects *fx)
{
    if (--fx->depth == 0) release(fx);
    return !fx->failed;
}

static bool was_removed(const cok_effects *fx, const cok_effect *effect)
{
    for (size_t i = 0; i < fx->removed_count; ++i)
        if (fx->removed[i].effect == effect) return true;
    return false;
}

/* Whether effect was removed (and in the original freed) during this
 * call, and now was made after: then the original's comparison of their
 * addresses depends on where TP's GetMem (first fit) put now, perhaps in
 * the block effect left. */
static bool maybe_reused(const cok_effects *fx, const cok_effect *effect, const cok_effect *now)
{
    for (size_t i = 0; i < fx->removed_count; ++i)
        if (fx->removed[i].effect == effect) return now != NULL && now->made > fx->removed[i].made;
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

static void log_text(cok_effects *fx, const char *kind, const char *text)
{
    if (fx->log != NULL) fx->log(fx, kind, text, fx->context);
}

/* Say text about c (6346:1883, row 10, wait 1), or fail without the
 * hook; say_now without the wait. */
static bool say_waiting(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler,
                        const char *text, bool wait)
{
    if (fx->say == NULL)
        return fail(fx, "effect 0x%02x (3f44:%04x) says text, with no say hook", id, handler);
    fx->say(fx, c, text, wait, fx->context);
    return true;
}

static bool say(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler, const char *text)
{
    return say_waiting(fx, c, id, handler, text, true);
}

/* Say text about c with a flash of kind 1 on it (6346:228c), then if
 * clear clear it (6346:196a), or as say without the hook. */
static bool flash(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler, const char *text,
                  bool clear)
{
    if (fx->flash == NULL) return say(fx, c, id, handler, text);
    if (fx->flash(fx, c, 1, text, clear, fx->context)) return true;
    return fail(fx, "effect 0x%02x (3f44:%04x) could not show its text (6346:228c)", id, handler);
}

/* The spell table (DS:31b3, 16 bytes an id), as the data segment holds it. */
static uint8_t spell_byte(const cok_effects *fx, uint8_t spell, unsigned column)
{
    const cok_data_segment *D = fx->rules->data;
    uint8_t byte = 0;
    cok_data_byte(D, (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, column), &byte);
    return byte;
}

static bool caster_level(cok_effects *fx, uint8_t spell, uint8_t *level)
{
    const cok_character_layout *L = fx->rules->character;
    const cok_rules_effects *E = &fx->rules->effects;
    const uint8_t *c = fx->vm->character;
    if (c == NULL)
        return fail(fx, "the caster level of spell %u (6346:29fe) reads the selected character, "
                        "and none is selected", spell);
    uint8_t class_ = spell_byte(fx, spell, 0);
    /* 66c2:0efb, 0 or 1, multiplies the former levels as words. */
    int former = cok_character_former_class(c, fx->rules) ? 1 : 0;
#define LEVEL(k) ((int)cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, k))
#define FORMER(k) ((int)cok_character_former_level(fx->rules, c, k))
    if (LEVEL(0) == 0 && LEVEL(5) == 0 && LEVEL(7) < 9 && LEVEL(4) < 8) {
        *level = 6;
    } else if (class_ == 0 || class_ == 2) {
        int8_t a = (int8_t)(former * FORMER(0) + LEVEL(0));
        int8_t b = (int8_t)(former * FORMER(7) + LEVEL(7) - E->caster_knight_less);
        *level = (uint8_t)(a > b ? a : b);
    } else if (class_ == 1) {
        int8_t a = (int8_t)(former * FORMER(4) + LEVEL(4) - 7);
        *level = (uint8_t)(a > 0 ? a : 0);
    } else if (class_ == 3) {
        int8_t a = (int8_t)(former * FORMER(5) + LEVEL(5));
        int8_t b = (int8_t)(former * FORMER(4) + LEVEL(4) - E->caster_ranger_less);
        if (LEVEL(5) > 0 && cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER) != 0) {
            /* The moon of its order, word 0x4cf8 + order (2b96). */
            uint16_t moon = cok_ecl_var_at(fx->vm, COK_VAR_ORDER_MOON,
                                           cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER));
            if (moon == 0) --a;
            else if (moon == 2 && LEVEL(5) > 5) ++a;
        }
        *level = (uint8_t)(a > b ? a : b);
    } else if (class_ == 4) {
        *level = 12;
    } else {
        return fail(fx, "spell %u is of class %u; its caster level (6346:29fe) is an "
                        "uninitialized byte (dkk 65cd:2c33's [bp-1], read at 2da3)", spell,
                    class_);
    }
#undef LEVEL
#undef FORMER
    /* dkk 65cd:2c33: its spell by one holding its effect. */
    if (E->caster_spell != 0 && spell == E->caster_spell) {
        const cok_character *holder = NULL;
        for (size_t i = 0; i < fx->party->count && holder == NULL; ++i)
            if (fx->party->members[i]->record == c) holder = fx->party->members[i];
        if (holder != NULL && cok_character_find_effect(holder, E->caster_effect) != NULL)
            *level = E->caster_spell_level;
    }
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

static bool dispatch(cok_effects *fx, cok_character *target, uint8_t event);
static bool dispatch_at(cok_effects *fx, cok_character **target, uint8_t event);
static bool saving_throw(cok_effects *fx, cok_character *target, uint8_t type, uint8_t bonus,
                         bool *made);

/* 60f4:20f7: add effect id to c unless its effects or magic resistance,
 * on event 9, cancel the effect pending (DS:6b2f), or c saved against one
 * with a save of kind 1; then it "is Unaffected" (6346:1883). An effect of
 * the id with time left is removed first. Then text, if any, is said with
 * a flash on c (6346:228c, kind 1). cast.c has the same for spells. */
static bool add_with_text(cok_effects *fx, cok_character *c, uint8_t id, uint16_t minutes,
                          uint8_t value, bool on_remove, uint8_t save_kind, bool saved,
                          uint16_t handler, const char *text)
{
    cok_rolls *r = &fx->rolls;
    r->pending = id;
    if (!dispatch_at(fx, &c, 9)) return false;
    if (r->pending == 0 || (saved && save_kind == 1))
        return say(fx, c, id, handler, fx->rules->effects.unaffected);
    cok_effect *old = cok_character_find_effect(c, id);
    if (old != NULL && old->duration > 0 && !remove_effect(fx, c, old, id)) return false;
    if (!add(fx, c, id, minutes, value, on_remove)) return false;
    return text[0] == '\0' || flash(fx, c, id, handler, text, true);
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
    const cok_character_layout *L = fx->rules->character;
    cok_character *attacker = selected(fx, id);
    if (attacker == NULL) return false;
    uint8_t alignment = cok_field_u8(L, attacker->record, COK_FIELD_ALIGNMENT);
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

static cok_combat_record *combat_of(cok_effects *fx, cok_character *c, uint8_t id,
                                    uint16_t handler);
static bool call_handler(cok_effects *fx, uint8_t id, bool removing, cok_effect *effect,
                         cok_character *c);

/* 3f44:03cd, 0x0b, charm. Removing it puts the character back on the side
 * kept in bit 6 of the value and, if it was made an NPC (+0xe7 0xb3), back
 * to a player character. The first time it runs (bit 5 of the value
 * clear) it takes hold: the value gains bit 5 and its side in bit 6 (a
 * byte sum, so the side + 0x20 + the value, the caster's level), the side
 * becomes the value's bit 7, 0 for a value below 0x80, the computer
 * controls it, a player character is turned (+0xe7 0xb3), it forgets its
 * target and the sides are counted (6346:268a). Either way morale is
 * 100. */
static bool h_charm(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    uint8_t *r = c->record;
    if (removing) {
        cok_field_set8(L, r, COK_FIELD_HOSTILE, (uint8_t)((effect->value & 0x40) >> 6));
        if (cok_field_u8(L, r, COK_FIELD_NPC) == 0xb3) {
            cok_field_set8(L, r, COK_FIELD_NPC, 0);
            cok_field_set8(L, r, COK_FIELD_COMPUTER, 0);
        }
        return true;
    }
    if ((effect->value & 0x20) == 0) {
        effect->value = (uint8_t)(effect->value + 0x20 +
                                  cok_field_s8(L, r, COK_FIELD_HOSTILE) * 64);
        cok_field_set8(L, r, COK_FIELD_HOSTILE, (uint8_t)(effect->value >> 7));
        cok_field_set8(L, r, COK_FIELD_COMPUTER, 1);
        if (cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7f) cok_field_set8(L, r, COK_FIELD_NPC, 0xb3);
        cok_combat_record *cr = combat_of(fx, c, 0x0b, 0x03cd);
        if (cr == NULL) return false;
        cr->target = NULL;
        if (fx->count_sides == NULL || !fx->count_sides(fx, fx->context))
            return fail(fx, "effect 0x0b (3f44:03cd) could not count the sides (6346:268a)");
    }
    fx->rolls.morale = 100;
    return true;
}

/* 3f44:062c, 0x11, shield: AC at least 3 (60 - AC 0x39) until the stats
 * are next recomputed, saves +1, and no damage from spell 0x0f. */
static bool h_shield(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    if (cok_field_u8(L, c->record, COK_FIELD_AC) < 0x39)
        cok_field_set8(L, c->record, COK_FIELD_AC, 0x39);
    ++fx->rolls.save_roll;
    if (IS_SPELL(fx->rolls.spell, MAGIC_MISSILE)) fx->rolls.amount = 0;
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
    return resist_charm(fx, 30, EFFECT(CHARMED), EFFECT(ASLEEP));
}

static bool h_resist_charm_90(cok_effects *fx, bool removing, cok_effect *effect,
                              cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    return resist_charm(fx, 90, EFFECT(ASLEEP), EFFECT(CHARMED));
}

static bool h_resist_charm(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cancel(fx, EFFECT(CHARMED));
    cancel(fx, EFFECT(ASLEEP));
    return true;
}

/* 3f44:07b5, 0x17, spiritual hammer, on the first hammer in the items
 * (type 6, +0x31 0x79). Removing the effect frees it without unreadying it
 * (6346:1697). Applying it, with no hammer and fewer than 16 items as last
 * counted (+0x142), creates one at the end of the items: type 6, name parts
 * 6 and 0x79 (+0x30, +0x31), bonus 1, effect 0x17 and power 0 (+0x3d,
 * +0x3e), the rest 0, and says "Gains an item" (dkk 504d:06dc "gains an
 * item": the entry's words). It would ready it in an
 * empty weapon slot, but its search for the new hammer steps past it, so
 * it never does. Either way the stats are then recomputed. */
static bool h_hammer(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    size_t i = 0;
    while (i < c->item_count && (c->items[i][0x2e] != 6 || c->items[i][0x31] != 0x79)) ++i;
    if (removing && i < c->item_count) {
        cok_character_remove_item(c, i);
    } else if (!removing && i == c->item_count &&
               cok_field_u8(L, c->record, COK_FIELD_ITEM_COUNT) < 16) {
        uint8_t hammer[COK_ITEM_SIZE] = {0};
        hammer[0x2e] = 6;
        hammer[0x30] = 6;
        hammer[0x31] = 0x79;
        hammer[0x32] = 1;
        hammer[0x3d] = EFFECT(HAMMER);
        hammer[0x3e] = 0x80;
        if (!cok_character_insert_item(c, c->item_count, hammer))
            return fail(fx, "out of memory adding the hammer");
        if (!say(fx, c, 0x17, 0x07b5, fx->rules->effects.handlers[fx->running].text))
            return false;
    }
    char why[200];
    if (!cok_character_stats(c, fx->rules, fx->types, why, sizeof why))
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
    if (FIND_EFFECT(attacker, SEES_INVISIBLE) == NULL) {
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
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 4);
    cok_field_set8(L, c->record, COK_FIELD_AC,
                   (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_AC) - 4));
    cok_field_set8(L, c->record, COK_FIELD_AC_BEHIND,
                   (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_AC_BEHIND) - 4));
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
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    if ((effect->value & 0x10) == 0) {
        effect->value = (uint8_t)(effect->value + 0x10);
        if (!say(fx, c, 0x27, 0x0f78, "ages")) return false;
        uint16_t age = cok_field_u16(L, c->record, COK_FIELD_AGE);
        ++age;
        cok_field_set16(L, c->record, COK_FIELD_AGE, age);
    }
    fx->rolls.rate = (uint8_t)(fx->rolls.rate << 1);
    return true;
}

/* dkk 504d:0d88, 0x27: haste as 3f44:0f78 when not removed, the aging
 * asked for after the battle (DS:c463) the first time, and the movement
 * (DS:be6f) doubled too. */
static bool h_haste_aging(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    if (removing) return true;
    if ((effect->value & 0x10) == 0) {
        effect->value = (uint8_t)(effect->value + 0x10);
        if (!say(fx, c, 0x27, 0x0d88, "ages")) return false;
        uint16_t age = cok_field_u16(L, c->record, COK_FIELD_AGE);
        ++age;
        cok_field_set16(L, c->record, COK_FIELD_AGE, age);
        fx->aging = true;
    }
    fx->rolls.rate = (uint8_t)(fx->rolls.rate << 1);
    fx->rolls.movement = (uint8_t)(fx->rolls.movement << 1);
    return true;
}

/* 3f44:144c, 0x2a: half the rate. */
static bool h_slow(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.rate = (uint8_t)(fx->rolls.rate >> 1);
    return true;
}

/* 3f44:00ba: unless effects are being cured (DS:6b38), add id again for
 * minutes, with the value of the effect being run and its handler on
 * removal (60f4:1285, at the end of the list); *again says whether it
 * was. The handlers that call it read the value through the effect before
 * 00ba tests DS:6b38, which with none is 0000:0003, and used only when
 * not curing: the port stops then. (No caller in the game runs these
 * handlers with no effect; only cok_effects_run can.) */
static bool again(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler,
                  const cok_effect *effect, uint16_t minutes, bool *added)
{
    *added = false;
    if (fx->rolls.curing != 0) return true;
    if (effect == NULL)
        return fail(fx, "effect 0x%02x (3f44:%04x) is run with no effect, whose value it reads",
                    id, handler);
    *added = true;
    return add(fx, c, id, minutes, effect->value, true);
}

/* 6346:07ba outside combat: the party list is drawn again. */
static void party_list(cok_effects *fx)
{
    if (fx->vm->mode != 5 && fx->party_list != NULL) fx->party_list(fx, fx->context);
}

/* 3f44:1469, 0x2b, strength drain, with either flag: unless effects are
 * being cured (DS:6b38), it adds itself again for an hour, with its
 * handler; then a strength (+0x11) above 3 "is weakened" (row 10, with a
 * pause) and loses a point, and one of 3 or less brings effect 0x1f for
 * good, if the character lacks it. */
static bool h_drain(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    bool added;
    if (!again(fx, c, EFFECT(STRENGTH_DRAIN), 0x1469, effect, 0x3c, &added) || !added)
        return !fx->failed;
    if (cok_field_u8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH) > 3) {
        if (!say(fx, c, 0x2b, 0x1469, "is weakened")) return false;
        cok_field_add8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, -1);
        return true;
    }
    if (FIND_EFFECT(c, HELPLESS) == NULL)
        return add(fx, c, EFFECT(HELPLESS), 0, 0xff, false);
    return true;
}

/* 3f44:05c3 (0x0f, poison) and 1504 (0x2c, wasting), with either flag:
 * unless effects are being cured, each adds itself again for ten minutes;
 * then one with more than a hit point (+0x197) takes 1 of type 0
 * (DS:6b31) through 60f4:1db7, "from Magic", and outside combat the party
 * list is drawn again (6346:07ba). With a hit point or none, 0x2c brings
 * effect 0x1f for good, if the character lacks it; 0x0f nothing. */
static bool h_ticking(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    uint8_t id = fx->running;
    uint16_t handler = id == 0x0f ? 0x05c3 : 0x1504;
    bool added;
    if (!again(fx, c, id, handler, effect, 10, &added) || !added) return !fx->failed;
    if (cok_field_u8(L, c->record, COK_FIELD_HIT_POINTS) > 1) {
        fx->rolls.damage_type = 0;
        if (fx->damage == NULL || !fx->damage(fx, c, 1, 0, false, fx->context))
            return fail(fx, "effect 0x%02x (3f44:%04x) could not deal damage (60f4:1db7)", id,
                        handler);
        party_list(fx);
        return true;
    }
    if (IS_EFFECT(id, WASTING) && FIND_EFFECT(c, HELPLESS) == NULL)
        return add(fx, c, EFFECT(HELPLESS), 0, 0xff, false);
    return true;
}

/* 3f44:0725, 0x16 (Slow Poison's), with either flag: a poisoned character
 * (effect 0x37) "dies from poison" (60f4:00eb, status 6) and loses its
 * 0x37; then, effects being cured (DS:6b38 set, and cleared after, even
 * if it was set before), its first 0x0f goes. */
static bool h_poisoned(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if (FIND_EFFECT(c, POISONED) != NULL) {
        if (fx->kill == NULL || !fx->kill(fx, c, 6, "dies from poison", fx->context))
            return fail(fx, "effect 0x16 (3f44:0725) could not kill (60f4:00eb)");
        if (!remove_effect(fx, c, NULL, EFFECT(POISONED))) return false;
    }
    fx->rolls.curing = 1;
    bool ok = remove_effect(fx, c, NULL, EFFECT(POISON));
    fx->rolls.curing = 0;
    return ok;
}

/* 3f44:0d19, 0x22 (Cause Disease's), with either flag: the handlers of
 * 0x2b and 0x2c run (60f4:01a8) with its flag and this effect, whose value
 * they add again with. */
static bool h_disease(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    return call_handler(fx, EFFECT(STRENGTH_DRAIN), removing, effect, c) &&
           call_handler(fx, EFFECT(WASTING), removing, effect, c);
}

/* 3f44:1acd, 0x3e (a constitution of 20 or more), with either flag:
 * unless effects are being cured, it adds itself again for an hour; then
 * one that 60f4:21ea heals a hit point (of status 0, 1, 4 or 5, below its
 * most) "is fully healed" or "is partially healed" (6346:25f9, row 10,
 * with a pause), and outside combat the party list is drawn again. */
static bool h_regenerate(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    bool added;
    if (!again(fx, c, EFFECT(REGENERATING), 0x1acd, effect, 0x3c, &added) || !added)
        return !fx->failed;
    uint8_t *r = c->record;
    if (!cok_character_heal(r, fx->rules, 1, true, fx->vm->mode)) return true;
    if (!say(fx, c, 0x3e, 0x1acd,
             cok_field_u8(L, r, COK_FIELD_HIT_POINTS) ==
                     cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS)
                 ? "is fully healed"
                 : "is partially healed"))
        return false;
    party_list(fx);
    return true;
}

/* 3f44:26a5, 0x4a, with either flag (event 0x0e, the computer's turn): on
 * a d20 of 1, the handler of spell 0x29, Dispel Magic (5b04:320f, through
 * DS:6ede), runs as if cast by the selected character, on the targets,
 * spell (DS:6b33) and cell the last cast or breath left (fx->dispel). */
static bool h_dispelling(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if (roll(fx, 1, 20) != 1) return true;
    if (fx->dispel != NULL && fx->dispel(fx, fx->context)) return true;
    return fail(fx, "effect 0x4a (3f44:26a5) could not dispel (5b04:320f)");
}

/* 3f44:15a3, 0x2f: an attack roll 4 lower when the selected character (the
 * attacker) has bit 2 of +0x13f. */
static bool h_against_flag4(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)c;
    cok_character *attacker = selected(fx, effect->id);
    if (attacker == NULL) return false;
    if ((cok_field_u8_at(L, attacker->record, COK_FIELD_CREATURE_FLAGS, 0) & 4) != 0)
        fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 4);
    return true;
}

/* 3f44:16ef, 0x31, prayer, shared by the party: to hit and saves 1 better
 * for a character on the side in bit 4 of the value (+0x18a), else 1
 * worse. */
static bool h_prayer(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    if (cok_field_u8(L, c->record, COK_FIELD_HOSTILE) == (effect->value & 0x10) >> 4) {
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
    return add(fx, c, EFFECT(INVISIBLE), 1, 0x0c, false);
}

static bool h_add_invisible_255(cok_effects *fx, bool removing, cok_effect *effect,
                                cok_character *c)
{
    (void)removing, (void)effect;
    return add(fx, c, EFFECT(INVISIBLE), 255, 0xff, false);
}

/* 3f44:17c6, 0x39: to hit +2 and AC 2 better, in place each time. */
static bool h_plus_two(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll + 2);
    cok_field_set8(L, c->record, COK_FIELD_AC,
                   (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_AC) + 2));
    cok_field_set8(L, c->record, COK_FIELD_AC_BEHIND,
                   (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_AC_BEHIND) + 2));
    return true;
}

/* 3f44:17ea, 0x3a: when it ends, add 0x3b for good and 0x3c for three
 * minutes, with its handler, and remove the first of 4, 0x20, 0x30 and
 * 0x39. */
static bool h_death_throes(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect;
    if (!removing) return true;
    const uint8_t ids[] = {EFFECT(BOLTS), EFFECT(MAD), EFFECT(IMMOLATE), EFFECT(PLUS_TWO)};
    if (!add(fx, c, EFFECT(IMMUNE), 0, 0xff, false) || !add(fx, c, EFFECT(BURST), 3, 0xff, true))
        return false;
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
 * level (DS:31b4 + 16 * spell, signed) below the entry's value, 4 (dkk
 * 504d:108d, 0x3b, 5). */
static bool h_minor_globe(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if (fx->rolls.spell == 0) return true;
    const cok_data_segment *D = fx->rules->data;
    uint8_t level;
    uint16_t at = (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, fx->rolls.spell, 1);
    if (!cok_data_byte(D, at, &level))
        return fail(fx, "effect 0x3f reads DS:%04x, past the original's initialized data", at);
    if ((int8_t)level < (int8_t)fx->rules->effects.handlers[fx->running].value) cancel(fx, 0);
    return true;
}

/* 3f44:2665, 0x49 (a dragon's, against breath): against damage of a type
 * with bit 0x20 (the breath's: 0x20, 0x21, 0x22, 0x24, 0x30, or one left
 * in DS:6b31), none and no effect (3f44:009b), and "is unaffected"
 * (row 10, a pause). */
static bool h_unaffected(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if ((fx->rolls.damage_type & 0x20) == 0) return true;
    cancel(fx, 0);
    return say(fx, c, 0x49, 0x2665, "is unaffected");
}

/* 3f44:29d5, 0x4d, berserk: the character comes under the computer's
 * control (+0x18b) as an NPC (+0xe7 0xb3, or 0xb2 for another NPC). In
 * combat it then aims at the second combatant 6b30:08d8 lists around it
 * (DS:6a36: the nearest other, or with none, whatever an earlier list
 * left there), may not cast, takes the side against that one's (its
 * +0x18a with bit 0 flipped) and "goes berserk". Removing it gives a
 * player character back, and puts any character on the party's side. */
static bool h_berserk(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    uint8_t *r = c->record;
    if (removing) {
        if (cok_field_u8(L, r, COK_FIELD_NPC) == 0xb3) {
            cok_field_set8(L, r, COK_FIELD_NPC, 0);
            cok_field_set8(L, r, COK_FIELD_COMPUTER, 0);
        }
        cok_field_set8(L, r, COK_FIELD_HOSTILE, 0);
        return true;
    }
    cok_field_set8(L, r, COK_FIELD_COMPUTER, 1);
    cok_field_set8(L, r, COK_FIELD_NPC,
                   cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7f ||
                           cok_field_u8(L, r, COK_FIELD_NPC) == 0xb3
                       ? 0xb3
                       : 0xb2);
    if (fx->vm->mode != 5) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x4d, 0x29d5);
    if (cr == NULL) return false;
    cr->target = NULL;
    uint8_t *target = NULL;
    if (fx->second == NULL || !fx->second(fx, c, &target, fx->context))
        return fail(fx, "effect 0x4d (3f44:29d5) could not list the combatants around one "
                        "(6b30:08d8)");
    cr->target = target;
    cr->may_cast = 0;
    if (target == NULL)
        return fail(fx, "effect 0x4d (3f44:29d5) reads a target with none, at 0000:018a");
    cok_field_set8(L, r, COK_FIELD_HOSTILE,
                   (uint8_t)(cok_field_u8(L, target, COK_FIELD_HOSTILE) ^ 1));
    return say(fx, c, 0x4d, 0x29d5, "goes berserk");
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

/* dkk 504d:2170, 0x5d: half damage from items of kind 0 or 1 (byte 7 of
 * the item type). */
static bool h_half_weapons(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)c;
    const uint8_t *item;
    uint8_t kind = 0;
    if (!striking_kind(fx, effect->id, &item, &kind)) return false;
    if (item != NULL && kind < 2) fx->rolls.amount = (uint8_t)(fx->rolls.amount >> 1);
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
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)c;
    cok_character *attacker = selected(fx, effect->id);
    if (attacker == NULL) return false;
    const uint8_t *item = striking_item(fx, attacker);
    if (fx->failed) return false;
    if ((item == NULL || item[0x32] == 0) &&
        (cok_field_s8(L, attacker->record, COK_FIELD_RACE) < 7 ||
         cok_field_s8(L, attacker->record, COK_FIELD_HIGHEST_LEVEL) < 4))
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
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    uint8_t type = fx->rolls.save_type;
    if (type != 4 && type != 2 && type != 0) return true;
    uint8_t con = cok_field_u8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION), bonus;
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
                        "%u (dkk 504d:21bd's [bp-1], read at 2226)", con);
    fx->rolls.save_roll = (uint8_t)(fx->rolls.save_roll + bonus);
    return true;
}

/* 3f44:3361, 0x61: resist paralysis (effect 0x34) being added. */
static bool h_free_action(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cancel(fx, EFFECT(PARALYZED));
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
    cancel(fx, EFFECT(POISONED));
    cancel(fx, EFFECT(PARALYZED));
    if (fx->rolls.save_type == 0) fx->rolls.save_roll = 100;
    return true;
}

/* The combat record of c, which a handler writes through: with none the
 * original writes through NULL or a stale pointer. */
static cok_combat_record *combat_of(cok_effects *fx, cok_character *c, uint8_t id,
                                    uint16_t handler)
{
    if (c->combat == NULL)
        fail(fx, "effect 0x%02x (3f44:%04x) writes the combat record of a record with none",
             id, handler);
    return c->combat;
}

/* 6346:2964 for c, from a handler: its turn ends. */
static bool end_turn(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler)
{
    cok_combat_record *cr = combat_of(fx, c, id, handler);
    if (cr == NULL) return false;
    cr->initiative = 0;
    cr->spell = 0;
    cr->guarding = 0;
    cr->movement = 0;
    return true;
}

/* The combatants within radius of c on the map (6b30:08d8), or false. */
static bool around(cok_effects *fx, cok_character *c, uint8_t radius,
                   cok_character *listed[COK_PARTY_RECORDS + 1], uint8_t *count)
{
    char why[200] = "";
    if (!fx->in_battle || fx->around == NULL)
        return fail(fx, "the combatants around one (6b30:08d8) are listed outside combat, on "
                        "the map combat has freed");
    if (!fx->around(fx, c, radius, listed, count, why, sizeof why, fx->context))
        return fail(fx, "%s", why);
    return true;
}

/* 3f44:00f7, 0x1b, 0x1f and 0x33-0x35 (held, asleep, paralysed), with
 * either flag: the turn ends (6346:2964). Event 7 runs it at the start of
 * a turn. */
static bool h_held(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    uint8_t id = effect != NULL ? effect->id : 0;
    if (!fx->in_battle)
        return fail(fx, "effect 0x%02x (3f44:00f7) ends the turn outside combat, through the "
                        "record's combat record", id);
    return end_turn(fx, c, id, 0x00f7);
}

/* 3f44:016a, 0x03, with either flag: the value counts down by the
 * attacks of the round (+0x18f + +0x190, a byte), and the effect goes
 * when they reach it (the first of its id); then "is fighting with
 * snakes" and the turn ends. */
static bool h_snakes(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    if (effect == NULL) return fail(fx, "effect 0x03 (3f44:016a) is run with no effect");
    uint8_t n = (uint8_t)(cok_field_u8_at(L, c->record, COK_FIELD_ATTACKS_LEFT, 2) +
                          cok_field_u8_at(L, c->record, COK_FIELD_ATTACKS_LEFT, 1));
    if (effect->value > n)
        effect->value = (uint8_t)(effect->value - n);
    else if (removing && effect->on_remove &&
             FIND_EFFECT(c, SNAKES) == effect)
        /* Being removed, it removes itself again, and so on: the original
         * has no stack check, and at about 84 bytes a level its 16K stack
         * wraps to SS:FFFE after some 150-190 and writes over memory. */
        return fail(fx, "effect 0x03 (3f44:016a) removes itself on its removal without end; "
                        "the original recurses until its stack wraps (dkk 504d:018e)");
    else if (!remove_effect(fx, c, NULL, EFFECT(SNAKES)))
        return false;
    if (!flash(fx, c, 0x03, 0x016a, "is fighting with snakes", true)) return false;
    return end_turn(fx, c, 0x03, 0x016a);
}

/* 3f44:06b2, 0x15, silence, shared, with either flag: one that may use
 * items "is silenced"; it may neither use them nor cast. */
static bool h_silenced(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    cok_combat_record *cr = combat_of(fx, c, 0x15, 0x06b2);
    if (cr == NULL) return false;
    if (cr->may_use != 0 && !say(fx, c, 0x15, 0x06b2, "is silenced")) return false;
    cr->may_use = 0;
    cr->may_cast = 0;
    return true;
}

/* 3f44:0ae0, 0x1e, the stinking cloud's nausea, with either flag: one
 * that may use items "is coughing"; it may neither use them nor cast; its
 * stats are recomputed, then its armour class from behind (+0x18e) is 2
 * worse, or 0x32 (AC 10) for 0x34 or less, and its armour class that.
 * Then the side panel (6346:0af6, fx->panel) if it is selected (DS:6096),
 * which draws only while DS:71ac is set. */
static bool h_coughing(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_combat_record *cr = combat_of(fx, c, 0x1e, 0x0ae0);
    if (cr == NULL) return false;
    if (cr->may_use != 0 && !say(fx, c, 0x1e, 0x0ae0, "is coughing")) return false;
    cr->may_use = 0;
    cr->may_cast = 0;
    char why[200];
    if (!cok_character_stats(c, fx->rules, fx->types, why, sizeof why)) return fail(fx, "%s", why);
    uint8_t *r = c->record;
    cok_field_set8(L, r, COK_FIELD_AC_BEHIND,
                   cok_field_u8(L, r, COK_FIELD_AC_BEHIND) > 0x34
                       ? (uint8_t)(cok_field_u8(L, r, COK_FIELD_AC_BEHIND) - 2)
                       : 0x32);
    cok_field_set8(L, r, COK_FIELD_AC, cok_field_u8(L, r, COK_FIELD_AC_BEHIND));
    if (fx->panel == NULL || fx->vm->character != r || fx->panel(fx, c, fx->context)) return true;
    return fail(fx, "effect 0x1e (3f44:0ae0) could not draw the side panel (6346:0af6)");
}

/* 3f44:0a2f, 0x1c, Mirror Image, with either flag: a d(images + 1),
 * the images in the value's high nibble, above 1 takes an image unless a
 * spell that takes none (DS:6b39) is being cast: the damage and the
 * effect pending are cancelled, "lost an image", and the value counts
 * down by 1 (its low nibble first). Once the high nibble is 0 the die is
 * a d1, so the value stops at 0x0f-0x01 and the removal of the first 0x1c
 * at 0 (3f44:0aad) is never reached; it is kept as the original has it. */
static bool h_images(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    if (effect == NULL) return fail(fx, "effect 0x1c (3f44:0a2f) is run with no effect");
    if (roll(fx, 1, (uint8_t)((effect->value >> 4) + 1)) <= 1) return true;
    if (fx->rolls.spell > 0 && fx->rolls.images != 0) return true;
    cancel(fx, 0);
    if (!say(fx, c, 0x1c, 0x0a2f, "lost an image")) return false;
    --effect->value;
    if (effect->value == 0) return remove_effect(fx, c, NULL, EFFECT(MIRROR_IMAGES));
    return true;
}

/* 3f44:0d7c, 0x23, confusion, with either flag, a d100: 1-10 the first
 * 0x23 goes and the character, under the computer's control (+0x18b, a
 * player character turned, +0xe7 0xb3), forgets its target, is made to
 * flee and gets 0x6f for ten minutes, "runs away" (60f4:20f7); 11-60 "is
 * confused" and its turn ends; 61-80 it gets 0x4d, berserk, for a minute,
 * with its side as the value, "goes berserk", whose handler then runs;
 * 81-100 "is enraged". Then a saving throw of type 4 at -2 that is made
 * takes the first 0x23. */
static bool h_confused(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    uint8_t *r = c->record;
    uint8_t d = roll(fx, 1, 100);
    if (d >= 1 && d <= 10) {
        if (!remove_effect(fx, c, NULL, EFFECT(CONFUSED))) return false;
        cok_combat_record *cr = combat_of(fx, c, 0x23, 0x0d7c);
        if (cr == NULL) return false;
        cr->forced = 1;
        cok_field_set8(L, r, COK_FIELD_COMPUTER, 1);
        if (cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7f) cok_field_set8(L, r, COK_FIELD_NPC, 0xb3);
        cr->target = NULL;
        if (!add_with_text(fx, c, EFFECT(TERROR), 10, 0, true, 1, false, 0x0d7c, "runs away"))
            return false;
    } else if (d >= 11 && d <= 60) {
        if (!flash(fx, c, 0x23, 0x0d7c, "is confused", true) || !end_turn(fx, c, 0x23, 0x0d7c))
            return false;
    } else if (d >= 61 && d <= 80) {
        if (!add_with_text(fx, c, EFFECT(BERSERK), 1, cok_field_u8(L, r, COK_FIELD_HOSTILE), true,
                           1, false,
                           0x0d7c, "goes berserk") ||
            !call_handler(fx, EFFECT(BERSERK), false, NULL, c))
            return false;
    } else if (d >= 81 && d <= 100) {
        if (!flash(fx, c, 0x23, 0x0d7c, "is enraged", true)) return false;
    }
    bool made;
    if (!saving_throw(fx, c, 4, 0xfe, &made)) return false;
    return !made || remove_effect(fx, c, NULL, EFFECT(CONFUSED));
}

/* 3f44:1330 for 0x29: the attacker (the selected character) with a
 * readied weapon, more than a square from c, misses on a d100 up to
 * chance: "Avoids it", no damage, the attack roll 0xff, one hit less
 * with slot 1 (DS:6b3c). */
static bool avoid(cok_effects *fx, cok_character *attacker, cok_character *c, uint8_t chance)
{
    if (attacker->slots[0] == 0) return true;
    char why[200] = "";
    uint8_t d;
    if (!fx->in_battle || fx->distance == NULL)
        return fail(fx, "the distance (6346:2888) is worked out outside combat, on the map "
                        "combat has freed");
    if (!fx->distance(fx, attacker->record, c->record, &d, why, sizeof why, fx->context))
        return fail(fx, "%s", why);
    if (d <= 1) return true;
    if (roll(fx, 1, 100) > chance) return true;
    if (!say(fx, c, 0x29, 0x1412, "Avoids it")) return false;
    fx->rolls.amount = 0;
    fx->rolls.attack_roll = 0xff;
    --fx->rolls.hits[0];
    return true;
}

/* 3f44:1412, 0x29, with either flag: against an attacker striking with
 * an item of no bonus (+0x32 0; 3f44:13a9), 3f44:1330 with chance 100. */
static bool h_avoid(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    cok_character *attacker = selected(fx, 0x29);
    if (attacker == NULL) return false;
    const uint8_t *item = striking_item(fx, attacker);
    if (fx->failed) return false;
    if (item == NULL || item[0x32] != 0) return true;
    return avoid(fx, attacker, c, 100);
}

/* 3f44:23a4, 0x47, spell turning, with either flag: a d10; on 1 for a
 * spell whose kind of targets (spell table byte 6 of DS:6b33, spell 0's
 * all 0 when none is cast) is below 8, or for one above 14, c "reflects
 * the spell": the caster (the selected character) takes its place in the
 * spell's targets and in the record the event was run for, so that the
 * damage or effect goes to it (the reflect hook). */
static bool h_reflect(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    uint8_t d = roll(fx, 1, 10);
    uint8_t kind = spell_byte(fx, fx->rolls.spell, 6);
    if (!((d == 1 && kind < 8) || kind > 14)) return true;
    if (!fx->in_battle || fx->reflect == NULL)
        return fail(fx, "effect 0x47 (3f44:23a4) turns a spell back outside combat: the cells "
                        "(6beb:0bcb) are read from the last battle's tables, the missile's "
                        "pictures copied into the buffer (DS:719e, 6346:1b5b) and the view "
                        "centred (6346:1ba6, 6beb:096b) over the combat map that battle freed");
    cok_character *caster;
    char why[200] = "";
    if (!fx->reflect(fx, c, &caster, why, sizeof why, fx->context))
        return fx->failed ? false : fail(fx, "%s", why);
    if (fx->target != NULL) *fx->target = caster;
    return true;
}

/* 3f44:24f4, 0x48, a terrible presence, with either flag: each
 * combatant listed within a cell of c (6b30:08d8; from the second, the
 * first being c) on the other side and without 0x5c that fails a saving
 * throw of type 4 is terrified, effect 0x6f for good (60f4:20f7), and if
 * it took hold comes under the computer's control, a player character
 * turned (+0xe7 0xb3), forgets its target and is made to flee. The loop's
 * counter is DS:742f. */
static bool h_terrible(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_character *listed[COK_PARTY_RECORDS + 1];
    uint8_t n;
    if (!around(fx, c, 1, listed, &n)) return false;
    for (unsigned i = 2; i <= n; ++i) {
        cok_character *e = listed[i];
        uint8_t *r = e->record;
        if (cok_field_u8(L, r, COK_FIELD_HOSTILE) ==
                cok_field_u8(L, c->record, COK_FIELD_HOSTILE) ||
            FIND_EFFECT(e, FEARLESS) != NULL)
            continue;
        bool made;
        if (!saving_throw(fx, e, 4, 0, &made)) return false;
        if (made) continue;
        if (!add_with_text(fx, e, EFFECT(TERROR), 0, 0, true, 1, false, 0x24f4, "is terrified"))
            return false;
        if (FIND_EFFECT(e, TERROR) == NULL) continue;
        cok_combat_record *cr = combat_of(fx, e, 0x48, 0x24f4);
        if (cr == NULL) return false;
        cok_field_set8(L, r, COK_FIELD_COMPUTER, 1);
        if (cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7f) cok_field_set8(L, r, COK_FIELD_NPC, 0xb3);
        cr->target = NULL;
        cr->forced = 1;
    }
    return true;
}

/* 3f44:09e8, 0x1a, with either flag: against a target (combat record
 * +0x0a, kept in DS:6b3f) whose +0x13f has bit 7, the attack roll 1
 * better. */
static bool h_target_bonus(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_combat_record *cr = combat_of(fx, c, 0x1a, 0x09e8);
    if (cr == NULL) return false;
    fx->rolls.target = cr->target;
    if (cr->target == NULL)
        return fail(fx, "effect 0x1a (3f44:09e8) reads a target with none, at 0000:013f");
    if ((cok_field_u8_at(L, cr->target, COK_FIELD_CREATURE_FLAGS, 0) & 0x80) != 0)
        fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll + 1);
    return true;
}

/* 3f44:26bf, 0x4b, with either flag: against a target (combat record
 * +0x0a, kept in DS:6b3f) whose +0x140 has bit 0, the damage is 3d12 + 4
 * plus strength's damage bonus (6346:14b5), as a byte, and the attack
 * roll 2 better. */
static bool h_giant_slayer(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_combat_record *cr = combat_of(fx, c, 0x4b, 0x26bf);
    if (cr == NULL) return false;
    fx->rolls.target = cr->target;
    if (cr->target == NULL)
        return fail(fx, "effect 0x4b (3f44:26bf) reads a target with none, at 0000:0140");
    if ((cok_field_u8_at(L, cr->target, COK_FIELD_CREATURE_FLAGS, 1) & 1) == 0) return true;
    int8_t bonus;
    char why[200];
    if (!cok_character_strength_bonus(c->record, fx->rules, true, &bonus, why, sizeof why))
        return fail(fx, "%s", why);
    uint8_t d = roll(fx, 1, 12);
    fx->rolls.amount = (uint8_t)(d * 3 + 4 + bonus);
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll + 2);
    return true;
}

/* 3f44:2b5f, 0x4f, an evil stench, with either flag (dkk 504d:1e47 the
 * same, its words its entry's): "emits an evil stench"; each combatant
 * listed within a cell (from the second) on the other side that fails a
 * saving throw of type 4 and lacks 0x76 "is affected", and 0x76 (to hit 2
 * worse) for good is added, but to c, the holder, not to it: so each one
 * that fails adds another to c. */
static bool h_stench(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    /* 3f44:2b84: 6346:1883 with no wait. */
    if (!say_waiting(fx, c, 0x4f, 0x2b5f, "emits an evil stench", false)) return false;
    cok_character *listed[COK_PARTY_RECORDS + 1];
    uint8_t n;
    if (!around(fx, c, 1, listed, &n)) return false;
    for (unsigned i = 2; i <= n; ++i) {
        cok_character *e = listed[i];
        if (cok_field_u8(L, e->record, COK_FIELD_HOSTILE) ==
            cok_field_u8(L, c->record, COK_FIELD_HOSTILE))
            continue;
        bool made;
        if (!saving_throw(fx, e, 4, 0, &made)) return false;
        if (made || FIND_EFFECT(e, SICKENED) != NULL) continue;
        if (!flash(fx, e, 0x4f, 0x2b5f, fx->rules->effects.handlers[0x4f].text, false) ||
            !add(fx, c, EFFECT(SICKENED), 0, 0xff, false))
            return false;
    }
    return true;
}

/* The handlers of the attacks, with either flag (see attack.h). */

/* The member whose record a combat record's target (+0x0a) is: one a
 * handler reads through, which the original reads at 0000:offset when it
 * is NULL. */
static cok_character *target_of(cok_effects *fx, const uint8_t *target, uint8_t id,
                                uint16_t handler, uint16_t offset)
{
    if (target == NULL) {
        fail(fx, "effect 0x%02x (3f44:%04x) reads a target with none, at 0000:%04x", id, handler,
             offset);
        return NULL;
    }
    for (size_t i = 0; i < fx->party->count; ++i)
        if (fx->party->members[i]->record == target) return fx->party->members[i];
    fail(fx, "effect 0x%02x (3f44:%04x) reads a target not in the list", id, handler);
    return NULL;
}

/* 3f44:1b55, poison on the target of c's attack (combat record +0x0a,
 * kept in DS:6b3f): bonus, made 0 if negative and the target has effect
 * 0x74, else the value of its 0x74 added, to a saving throw of type 0;
 * failed, it "is Poisoned", gets 0x37 for good and "is killed" (60f4:00eb,
 * dead). The original says the first with no wait and pauses; the port
 * clears it too, as the second's text clears it at once. */
static bool poison(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler, uint8_t bonus)
{
    cok_combat_record *cr = combat_of(fx, c, id, handler);
    if (cr == NULL) return false;
    fx->rolls.target = cr->target;
    cok_character *t = target_of(fx, cr->target, id, 0x1b55, 0x00e3);
    if (t == NULL) return false;
    const cok_effect *resistance = FIND_EFFECT(t, POISON_RESISTANCE);
    if (resistance != NULL) bonus = (int8_t)bonus < 0 ? 0 : (uint8_t)(bonus + resistance->value);
    bool made;
    if (!saving_throw(fx, t, 0, bonus, &made)) return false;
    if (made) return true;
    if (!say(fx, t, id, 0x1b55, "is Poisoned") || !add(fx, t, EFFECT(POISONED), 0, 0xff, false))
        return false;
    if (fx->kill != NULL && fx->kill(fx, t, 6, "is killed", fx->context)) return true;
    return fail(fx, "effect 0x%02x (3f44:1b55) could not kill (60f4:00eb)", id);
}

/* 3f44:1c21 (0x40), 1c34 (0x41), 31bc (0x56) and 31cf (0x57), on a hit
 * with damage (events 2 and 3): poison with a bonus of 0, 4, -2 and 1. */
static bool h_poison(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    uint8_t id = fx->running;
    if (IS_EFFECT(id, POISON_TOUCH)) return poison(fx, c, id, 0x1c21, 0);
    if (IS_EFFECT(id, POISON_TOUCH_4)) return poison(fx, c, id, 0x1c34, 4);
    if (IS_EFFECT(id, POISON_TOUCH_MINUS_2)) return poison(fx, c, id, 0x31bc, 0xfe);
    if (IS_EFFECT(id, POISON_TOUCH_1)) return poison(fx, c, id, 0x31cf, 1);
    return fail(fx, "effect 0x%02x is not a poison", id);
}

/* 3f44:000d, paralysis on the target of c's attack (DS:6b3f): unless it
 * makes a saving throw of type 0, it flashes, "is Paralyzed", the text
 * not cleared, and gets 0x34 for 100 minutes, value 0x0c. */
static bool paralyse(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler)
{
    cok_combat_record *cr = combat_of(fx, c, id, handler);
    if (cr == NULL) return false;
    fx->rolls.target = cr->target;
    cok_character *t = target_of(fx, cr->target, id, 0x000d, 0x00e3);
    if (t == NULL) return false;
    bool made;
    if (!saving_throw(fx, t, 0, 0, &made)) return false;
    if (made) return true;
    return flash(fx, t, id, 0x000d, "is Paralyzed", false) &&
           add(fx, t, EFFECT(PARALYZED), 100, 0x0c, false);
}

/* 3f44:200b, 0x45: paralysis. 3009, 0x51: no damage, then paralysis.
 * 31e2, 0x58: paralysis of a target of race (+0x5a, signed) above 1. */
static bool h_paralysis(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    uint8_t id = effect != NULL ? effect->id : 0;
    if (IS_EFFECT(id, PARALYZING)) return paralyse(fx, c, id, 0x200b);
    if (IS_EFFECT(id, PARALYZING_HARMLESS)) {
        fx->rolls.amount = 0;
        return paralyse(fx, c, id, 0x3009);
    }
    if (!IS_EFFECT(id, PARALYZING_HUMANS))
        return fail(fx, "effect 0x%02x is not a paralysis", id);
    cok_combat_record *cr = combat_of(fx, c, id, 0x31e2);
    if (cr == NULL) return false;
    cok_character *t = target_of(fx, cr->target, id, 0x31e2, 0x005a);
    if (t == NULL) return false;
    return cok_field_s8(L, t->record, COK_FIELD_RACE) <= 1 || paralyse(fx, c, id, 0x31e2);
}

/* 3f44:0208, 0x07, Molly's weapon, at the attack roll and the damage
 * (events 0x0a and 4): the THAC0 (+0x18c) is the base one (+0x59); within
 * a square of its target (6346:2888) the damage is strength's (6346:14b5)
 * + 1d6 + 2 + the readied weapon's bonus (+0x32) and the THAC0 strength's
 * better (6346:1412), else 1d4 + 1 + the bonus and dexterity's missile
 * bonus (6346:12f8), bytes; the dice are rolled at both events. It
 * starts with the debug trace ("bad news", 6d7e:093e, shown only with the
 * debug flag). With no weapon the bonus is a local never set, which the
 * port does not know. */
static bool kender(cok_effects *fx, cok_character *c);

static bool h_kender(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if (fx->trace != NULL) fx->trace(fx, "bad news", 0, 0, 0, fx->context); /* 3f44:0226 */
    return kender(fx, c);
}

/* dkk 504d:0239, 0x07: the same, with no trace, and only in a battle
 * (DS:8c4e 5). */
static bool h_kender_battle(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    return !fx->in_battle || kender(fx, c);
}

static bool kender(cok_effects *fx, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    uint8_t *r = c->record;
    cok_rolls *rolls = &fx->rolls;
    if (c->combat == NULL && !fx->in_battle) {
        /* Outside combat (the item unreadied, which runs the handler as
         * the effect goes: 3f44:3888) the target is read through +0x183,
         * nil before any battle (0000:000a, the interrupt table) or the
         * record the last battle's end freed, and the distance to it
         * (6346:2888) picks the die: a d6 or a d4, one Random (DS:43ae)
         * either way, and the THAC0 (+0x18c): the base's plus strength's
         * (6346:1412) or dexterity's (12f8) bonus. Where the caller
         * recomputes the stats (6346:0d20) after the item, the THAC0 and
         * the amount (DS:6b30), with the weapon's bonus (a local never
         * set with none), are not read before they are set again (an
         * attack sets the amount): only the roll is kept. Elsewhere (ROB,
         * 3775:1da9) View would show that THAC0: the run stops. */
        if (!fx->stats_follow)
            return fail(fx, "effect 0x07 (3f44:0208) outside combat sets the THAC0 by the "
                            "distance to a target read through no combat record (+0x183), "
                            "and nothing recomputes it (3775:1da9)");
        (void)cok_dice_count(&fx->vm->seed, 1, 6, &rolls->dice);
        return true;
    }
    size_t weapon = c->slots[0];
    if (weapon == 0 || weapon > c->item_count)
        return fail(fx, "effect 0x07 (3f44:0208) adds a local never set for a holder with no "
                        "weapon (3f44:02c0; dkk 504d:0239's [bp-6], read at 02de and 0326)");
    uint8_t bonus = c->items[weapon - 1][0x32];
    cok_combat_record *cr = combat_of(fx, c, 0x07, 0x0208);
    if (cr == NULL) return false;
    /* With no target, 6346:2888 finds none listed and reads the last. */
    char why[200] = "";
    uint8_t d;
    if (!fx->in_battle || fx->distance == NULL)
        return fail(fx, "the distance (6346:2888) is worked out outside combat, on the map "
                        "combat has freed");
    if (!fx->distance(fx, r, cr->target, &d, why, sizeof why, fx->context))
        return fail(fx, "%s", why);
    cok_field_set8(L, r, COK_FIELD_THAC0, cok_field_u8(L, r, COK_FIELD_BASE_THAC0));
    int8_t strength;
    if (d < 2) {
        if (!cok_character_strength_bonus(r, fx->rules, true, &strength, why, sizeof why))
            return fail(fx, "%s", why);
        uint8_t die = cok_dice_count(&fx->vm->seed, 1, 6, &rolls->dice);
        rolls->amount = (uint8_t)(strength + die + 2 + bonus);
        if (!cok_character_strength_bonus(r, fx->rules, false, &strength, why, sizeof why))
            return fail(fx, "%s", why);
        cok_field_set8(L, r, COK_FIELD_THAC0,
                       (uint8_t)(cok_field_u8(L, r, COK_FIELD_THAC0) + strength));
    } else {
        uint8_t die = cok_dice_count(&fx->vm->seed, 1, 4, &rolls->dice);
        rolls->amount = (uint8_t)(die + 1 + bonus);
        cok_field_set8(L, r, COK_FIELD_THAC0,
                       (uint8_t)(cok_field_u8(L, r, COK_FIELD_THAC0) +
                                 cok_character_dexterity_missile(r, fx->rules)));
    }
    return true;
}

/* 3f44:0f50, 0x25, blink, on being targeted and attacked (events 1 and
 * 0x10): one that has had its turn (initiative 0) cannot be, and the
 * attack roll is 0xff. */
static bool h_blink(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    cok_combat_record *cr = combat_of(fx, c, 0x25, 0x0f50);
    if (cr == NULL) return false;
    if (cr->initiative == 0) {
        fx->rolls.untargetable = 1;
        fx->rolls.attack_roll = 0xff;
    }
    return true;
}

/* At the damage (event 4), against the target of the holder's attack:
 * 3f44:349c, 0x69: one with +0x13f bit 3 takes the holder's +0xfd more
 * (the target kept in DS:6b3f); 376f, 0x72: one with +0x140 bit 0 takes
 * the holder's own hit points; 37b0, 0x73: one with +0x13f bit 5 takes 3
 * more. */
static bool h_slayer(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    uint8_t id = effect != NULL ? effect->id : 0;
    uint16_t handler = id == 0x69 ? 0x349c : id == 0x72 ? 0x376f : 0x37b0;
    /* Outside combat (an item with one unreadied: 3f44:3888 runs the
     * handler as the effect goes) the target is read through +0x183, nil
     * or freed (see kender), and it changes only the target and amount
     * of an attack (DS:6b3f, 6b30), which the next attack sets before it
     * reads them: nothing. */
    if (c->combat == NULL && !fx->in_battle) return true;
    cok_combat_record *cr = combat_of(fx, c, id, handler);
    if (cr == NULL) return false;
    if (IS_EFFECT(id, RANGER)) fx->rolls.target = cr->target;
    if (cr->target == NULL)
        return fail(fx, "effect 0x%02x (3f44:%04x) reads a target with none, at 0000:%04x", id,
                    handler, id == 0x72 ? 0x140 : 0x13f);
    const uint8_t *t = cr->target;
    if (IS_EFFECT(id, RANGER) && (cok_field_u8_at(L, t, COK_FIELD_CREATURE_FLAGS, 0) & 8) != 0)
        fx->rolls.amount = (uint8_t)(fx->rolls.amount +
                                     cok_field_u8_at(L, c->record, COK_FIELD_CLASS_LEVEL, 4));
    else if (IS_EFFECT(id, DEADLY) &&
             (cok_field_u8_at(L, t, COK_FIELD_CREATURE_FLAGS, 1) & 1) != 0)
        fx->rolls.amount = cok_field_u8(L, c->record, COK_FIELD_HIT_POINTS);
    else if (IS_EFFECT(id, BANE) &&
             (cok_field_u8_at(L, t, COK_FIELD_CREATURE_FLAGS, 0) & 0x20) != 0)
        fx->rolls.amount = (uint8_t)(fx->rolls.amount + 3);
    return true;
}

/* 3f44:37fc, 0x75, at the damage (event 4), in combat, against an undead
 * target (+0x13f bit 1): one of a kind of undead (+0xda) "is disrupted"
 * (60f4:00eb, gone), else the damage is doubled. */
static bool h_disrupt(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    if (fx->vm->mode != 5) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x75, 0x37fc);
    if (cr == NULL) return false;
    cok_character *t = target_of(fx, cr->target, 0x75, 0x37fc, 0x013f);
    if (t == NULL) return false;
    if ((cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0) & 2) == 0) return true;
    if (cok_field_u8(L, t->record, COK_FIELD_UNDEAD) == 0) {
        fx->rolls.amount = (uint8_t)(fx->rolls.amount << 1);
        return true;
    }
    if (fx->kill != NULL && fx->kill(fx, t, 8, "is disrupted", fx->context)) return true;
    return fail(fx, "effect 0x75 (3f44:37fc) could not kill (60f4:00eb)");
}

/* 3f44:36e1, 0x70, fire shield, at the damage the holder takes (event
 * 5): an attacker (the selected character) within a square "gets zapped"
 * and takes twice the damage, as a byte, as magic (60f4:1db7); the damage
 * and its type are then put back. */
static bool h_fire_shield(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    cok_character *attacker = selected(fx, 0x70);
    if (attacker == NULL) return false;
    char why[200] = "";
    uint8_t d;
    if (!fx->in_battle || fx->distance == NULL)
        return fail(fx, "the distance (6346:2888) is worked out outside combat, on the map "
                        "combat has freed");
    if (!fx->distance(fx, attacker->record, c->record, &d, why, sizeof why, fx->context))
        return fail(fx, "%s", why);
    if (d >= 2) return true;
    cok_rolls *rolls = &fx->rolls;
    uint8_t amount = rolls->amount, type = rolls->damage_type;
    rolls->amount = (uint8_t)(amount << 1);
    rolls->damage_type = 8;
    if (!say(fx, attacker, 0x70, 0x36e1, "gets zapped")) return false;
    if (fx->damage == NULL ||
        !fx->damage(fx, attacker, rolls->amount, 0, false, fx->context))
        return fail(fx, "effect 0x70 (3f44:36e1) could not deal damage (60f4:1db7)");
    rolls->amount = amount;
    rolls->damage_type = type;
    return true;
}

/* 1db7 through the hook, or fail. */
static bool deal(cok_effects *fx, cok_character *c, uint8_t amount, uint8_t id, uint16_t handler)
{
    if (fx->damage != NULL && fx->damage(fx, c, amount, 0, false, fx->context)) return true;
    return fail(fx, "effect 0x%02x (3f44:%04x) could not deal damage (60f4:1db7)", id, handler);
}

/* The others listed around c within a square (6b30:08d8) as 3f44:15cd and
 * 18b9 copy them: listed[2] on into a local array whose ninth entry
 * would land on the loop's count. */
static bool neighbours(cok_effects *fx, cok_character *c, uint8_t id, uint16_t handler,
                       cok_character *listed[COK_PARTY_RECORDS + 1], uint8_t *count)
{
    if (!around(fx, c, 1, listed, count)) return false;
    if (*count > 9)
        return fail(fx, "effect 0x%02x (3f44:%04x) copies more than eight around one over its "
                        "loop's count",
                    id, handler);
    return true;
}

/* 3f44:15cd, 0x30, with either flag (event 0x0f at the start of a turn):
 * c "immolates" (row 10, with a pause), and each other within a square
 * (6b30:08d8, listed before) that can act takes 1d6 (60f4:1261) through
 * 60f4:1db7. */
static bool h_immolate(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_character *listed[COK_PARTY_RECORDS + 1];
    uint8_t count;
    if (!neighbours(fx, c, 0x30, 0x15cd, listed, &count) ||
        !say(fx, c, 0x30, 0x15cd, "immolates"))
        return false;
    /* One off the map lists none, not even itself: the count less 1, a
     * byte, is 255, and the original damages what the stack holds. */
    if (count == 0)
        return fail(fx, "effect 0x30 (3f44:163b) with none listed counts 255 records off the "
                        "stack");
    for (unsigned i = 2; i <= count; ++i) {
        if (cok_field_u8(L, listed[i]->record, COK_FIELD_ACTIVE) == 0) continue;
        uint8_t d = cok_dice_count(&fx->vm->seed, 1, 6, &fx->rolls.dice);
        if (!deal(fx, listed[i], d, 0x30, 0x15cd)) return false;
    }
    return true;
}

/* 3f44:18b9, 0x3c, when it is removed: c, with the others within a square
 * listed first, "explodes!" and is gone (60f4:00eb, status 8); each other
 * listed then (the count as its death left it) that can act takes 3d6 (60f4:1261) through 60f4:1db7 and, unless it
 * makes a saving throw of type 4 (60f4:113a), "is stunned" (row 10, with
 * a pause) and gets 0x6a for good; then the dead that explode go off
 * (60f4:2375). */
static bool h_burst(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    if (!removing) return true;
    cok_character *listed[COK_PARTY_RECORDS + 1];
    uint8_t count;
    if (!around(fx, c, 1, listed, &count)) return false;
    if (fx->kill == NULL || !fx->kill(fx, c, 8, "explodes!", fx->context))
        return fail(fx, "effect 0x3c (3f44:18b9) could not kill (60f4:00eb)");
    /* The list is read after the death (3f44:192d): its events can work out
     * a distance (6346:2888, as 0x43 does), which lists again and puts the
     * entries back, but not the count. */
    char why[200] = "";
    if (fx->listed == NULL || !fx->listed(fx, listed, &count, why, sizeof why, fx->context))
        return fail(fx, "effect 0x3c (3f44:192d) could not read the list: %s", why);
    if (count > 9)
        return fail(fx, "effect 0x3c (3f44:18b9) copies more than eight around one over its "
                        "loop's count");
    for (unsigned i = 2; i <= count; ++i) {
        cok_character *e = listed[i];
        if (cok_field_u8(L, e->record, COK_FIELD_ACTIVE) == 0) continue;
        uint8_t d = cok_dice_count(&fx->vm->seed, 3, 6, &fx->rolls.dice);
        bool made;
        if (!deal(fx, e, d, 0x3c, 0x18b9) || !saving_throw(fx, e, 4, 0, &made)) return false;
        if (made) continue;
        if (!say(fx, e, 0x3c, 0x18b9, "is stunned") || !add(fx, e, EFFECT(STUNNED), 0, 0xff, false))
            return false;
    }
    if (fx->explode != NULL && fx->explode(fx, fx->context)) return true;
    return fail(fx, "effect 0x3c (3f44:18b9) could not set off the dead that explode "
                    "(60f4:2375)");
}

/* 3f44:1c54, 0x42, with either flag (event 0x0f): the first listed within
 * a square of c (6b30:08d8) on another side "gets zapped!" (row 10, with a
 * pause), takes 2d6 (60f4:1261) through 60f4:1db7, and c's turn ends. */
static bool h_zap(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_character *listed[COK_PARTY_RECORDS + 1];
    uint8_t count;
    if (!around(fx, c, 1, listed, &count)) return false;
    cok_character *t = NULL;
    for (unsigned i = 2; i <= count && t == NULL; ++i)
        if (cok_field_u8(L, listed[i]->record, COK_FIELD_HOSTILE) !=
            cok_field_u8(L, c->record, COK_FIELD_HOSTILE))
            t = listed[i];
    if (t == NULL) return true;
    if (!say(fx, t, 0x42, 0x1c54, "gets zapped!")) return false;
    uint8_t d = cok_dice_count(&fx->vm->seed, 2, 6, &fx->rolls.dice);
    return deal(fx, t, d, 0x42, 0x1c54) && end_turn(fx, c, 0x42, 0x1c54);
}

/* 3f44:272a, 0x4c, with either flag (event 0x0f): below half its hit
 * points (+0x197 < +0x62 / 2), c gates in those waiting (status 9) around
 * its enemies (cok_combat_gate) and loses its first 0x4c. */
static bool h_gate(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    if (cok_field_u8(L, c->record, COK_FIELD_HIT_POINTS) >=
        cok_field_u8(L, c->record, COK_FIELD_MAX_HIT_POINTS) >> 1)
        return true;
    if (fx->gate == NULL || !fx->gate(fx, c, fx->context))
        return fail(fx, "effect 0x4c (3f44:272a) could not gate in (6beb:0493, 60f4:22b7)");
    return remove_effect(fx, c, NULL, EFFECT(GATE));
}

/* 3f44:0ff7 (0x28, a cloud's end), 2037 (0x46, acid at death, event
 * 0x0d) and 2cad (0x50, a pool's end), with either flag: the clouds and
 * pools of the combat map (fx->clouds, see cloud.h). Outside combat the
 * clouds' and pools' lists are empty: the ends find none. */
static bool h_cloud(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    uint8_t id = fx->running;
    uint16_t handler = id == 0x28 ? 0x0ff7 : id == 0x46 ? 0x2037 : 0x2cad;
    if (effect == NULL && !IS_EFFECT(id, ACID_DEATH))
        return fail(fx, "effect 0x%02x (3f44:%04x) is run with no effect, whose value it reads",
                    id, handler);
    if (fx->clouds == NULL || !fx->clouds(fx, id, effect, c, fx->context))
        return fail(fx, "effect 0x%02x (3f44:%04x) could not be carried out on the combat map",
                    id, handler);
    return true;
}

/* 5b04:4ab8 (4), 4dc2 (0x4e), 4eea (0x54), 50cf (0x55), 5227 (0x5a) and
 * 546e (6, 0x53, 0x68), with either flag (event 0x0e): a monster breathes
 * or attacks (fx->breathe, cok_cast_breath). */
static bool h_breath(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    uint8_t id = fx->running;
    uint16_t handler = id == 4      ? 0x4ab8
                       : id == 0x4e ? 0x4dc2
                       : id == 0x54 ? 0x4eea
                       : id == 0x55 ? 0x50cf
                       : id == 0x5a ? 0x5227
                                    : 0x546e;
    if (effect == NULL)
        return fail(fx, "effect 0x%02x (5b04:%04x) is run with no effect", id, handler);
    if (!fx->in_battle || fx->breathe == NULL)
        return fail(fx, "effect 0x%02x (5b04:%04x) breathes outside combat, through the combat "
                        "targets and the map", id, handler);
    if (!fx->breathe(fx, effect, c, fx->context))
        return fail(fx, "effect 0x%02x (5b04:%04x) could not breathe", id, handler);
    return true;
}

/* 3f44:34db, 0x6a (stunned), with either flag: the turn ends
 * (6346:2964). */
static bool h_stunned(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if (!fx->in_battle)
        return fail(fx, "effect 0x6a (3f44:34db) ends the turn outside combat, through the "
                        "record's combat record");
    return end_turn(fx, c, 0x6a, 0x34db);
}

/* 3f44:1f97, 0x44, at death (event 0x0d): one that can no longer act is
 * exploding (status 10) and listed with the dead that explode after the
 * turn (60f4:2375), and its first 0x44 goes. */
static bool h_explodes(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    if (cok_field_u8(L, c->record, COK_FIELD_ACTIVE) != 0) return true;
    cok_field_set8(L, c->record, COK_FIELD_STATUS, 10);
    bool now;
    if (fx->exploding == NULL || !fx->exploding(fx, c, &now, fx->context))
        return fail(fx, "effect 0x44 (3f44:1f97) could not list the dead that explode");
    return remove_effect(fx, c, NULL, EFFECT(EXPLODES));
}

/* 3f44:1dc5, 0x43, at death (event 0x0d): its killer (the selected
 * character) loses what it strikes with (3f44:13a9) on a d20 of at least
 * its dexterity - 3, unless a spell (DS:6b33) killed, it has none, it is
 * cursed (+0x36), the dead explode now (DS:6b96) or the killer is not a
 * square away (6346:2888): it "loses his weapon", and unless it is a
 * spiritual hammer (type 6, +0x31 0x79), the weapon goes to the weapons
 * lost in combat (DS:609e) and back at its end (351b:185f). */
static bool h_lose_weapon(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_character *killer = selected(fx, 0x43);
    if (killer == NULL) return false;
    const uint8_t *item = striking_item(fx, killer);
    if (fx->failed) return false;
    uint8_t d = roll(fx, 1, 20);
    if ((int)cok_field_u8_at(L, killer->record, COK_FIELD_SCORE, COK_SCORE_DEXTERITY) - 3 > d)
        return true;
    if (fx->rolls.spell != 0 || item == NULL || item[0x36] != 0) return true;
    bool now;
    if (fx->exploding == NULL || !fx->exploding(fx, NULL, &now, fx->context))
        return fail(fx, "effect 0x43 (3f44:1dc5) could not read whether the dead explode");
    if (now) return true;
    char why[200] = "";
    if (!fx->in_battle || fx->distance == NULL)
        return fail(fx, "the distance (6346:2888) is worked out outside combat, on the map "
                        "combat has freed");
    if (!fx->distance(fx, killer->record, c->record, &d, why, sizeof why, fx->context))
        return fail(fx, "%s", why);
    if (d != 1) return true;
    if (!say(fx, killer, 0x43, 0x1dc5, "loses his weapon")) return false;
    if (item[0x2e] == 6 && item[0x31] == 0x79) return true;
    size_t index = 0;
    while (killer->items[index] != item) ++index;
    if (fx->lose_weapon != NULL && fx->lose_weapon(fx, killer, index + 1, fx->context))
        return true;
    return fail(fx, "effect 0x43 (3f44:1dc5) could not take the weapon");
}

/* 3f44:04c2, 0x0d, and 0bc9, 0x20, at death (event 0x0d): one whose status
 * is not 0 is okay and can act, and if it is put back on the map
 * (60f4:22b7) with 20 hit points, it "goes mad!", gains 0x20, 0x30 and
 * 0x39 for good and 0x3a for six minutes (its handler on removal), loses
 * this 0x0d and its memorized spells; or it "Arises in a new form", gains
 * 0x3b and 0x42 for good and 0x3c for three minutes, and loses the first
 * 0x20, 4, 0x30, 0x39 and 0x3a. Not put back, it stays okay off the
 * map. */
static bool h_new_form(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing;
    uint8_t id = effect != NULL ? effect->id : 0;
    uint8_t *r = c->record;
    if (cok_field_u8(L, r, COK_FIELD_STATUS) == 0) return true;
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    bool placed;
    if (fx->revive == NULL ||
        !fx->revive(fx, c, 0x14, IS_EFFECT(id, NEW_FORM) ? "goes mad!" : "Arises in a new form",
                    &placed,
                    fx->context))
        return fail(fx, "effect 0x%02x (3f44:%04x) could not put it back (60f4:22b7)", id,
                    id == 0x0d ? 0x04c2 : 0x0bc9);
    if (!placed) return true;
    if (IS_EFFECT(id, NEW_FORM)) {
        if (!add(fx, c, EFFECT(MAD), 0, 0xff, false) ||
            !add(fx, c, EFFECT(IMMOLATE), 0, 0xff, false) ||
            !add(fx, c, EFFECT(PLUS_TWO), 0, 0xff, false) ||
            !add(fx, c, EFFECT(DEATH_THROES), 6, 0xff, true) ||
            !remove_effect(fx, c, effect, EFFECT(NEW_FORM)))
            return false;
        memset(r + cok_field_offset(L, COK_FIELD_MEMORIZED), 0,
               cok_field_size(L, COK_FIELD_MEMORIZED));
        return true;
    }
    if (!add(fx, c, EFFECT(IMMUNE), 0, 0xff, false) || !add(fx, c, EFFECT(BURST), 3, 0xff, false) ||
        !add(fx, c, EFFECT(ZAP), 0, 0xff, false))
        return false;
    const uint8_t ids[] = {EFFECT(MAD), EFFECT(BOLTS), EFFECT(IMMOLATE), EFFECT(PLUS_TWO),
                           EFFECT(DEATH_THROES)};
    for (size_t i = 0; i < sizeof ids; ++i)
        if (!remove_effect(fx, c, NULL, ids[i])) return false;
    return true;
}

/* 3f44:303c, 0x52, a dragon's fear, removing it or not: every record in
 * the list on the other side from c (+0x18a) without effect 0x5c, 0x6f or
 * 0x77 is terrified, effect 0x6f for good (60f4:20f7), if of level
 * (+0xd6) 0-3; then if it has 0x6f, the computer controls it (+0x18b), a
 * player character is turned (+0xe7 0xb3), its combat record's target is
 * cleared and it flees (+0x10). Others are afraid, 0x77, unless they make
 * a saving throw of type 4. The original writes through a record with no
 * combat record (0000:000a); the port fails. */
static bool h_fear(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_party *party = fx->party;
    for (size_t i = 0; i < party->count && i < COK_PARTY_RECORDS; ++i) {
        cok_character *other = party->members[i];
        uint8_t *r = other->record;
        if (cok_field_u8(L, r, COK_FIELD_HOSTILE) == cok_field_u8(L, c->record, COK_FIELD_HOSTILE))
            continue;
        if (FIND_EFFECT(other, FEARLESS) != NULL ||
            FIND_EFFECT(other, TERROR) != NULL ||
            FIND_EFFECT(other, AFRAID) != NULL)
            continue;
        int8_t level = cok_field_s8(L, r, COK_FIELD_HIGHEST_LEVEL);
        if (level >= 0 && level <= 3) {
            if (!add_with_text(fx, other, EFFECT(TERROR), 0, 0xff, true, 1, false, 0x303c,
                               "is terrified"))
                return false;
            if (FIND_EFFECT(other, TERROR) == NULL) continue;
            if (other->combat == NULL)
                return fail(fx, "effect 0x52 (3f44:303c) writes the combat record of a record "
                                "with none, at 0000:000a");
            cok_field_set8(L, r, COK_FIELD_COMPUTER, 1);
            if (cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7f)
                cok_field_set8(L, r, COK_FIELD_NPC, 0xb3);
            other->combat->target = NULL;
            other->combat->forced = 1;
            continue;
        }
        bool made;
        if (!saving_throw(fx, other, 4, 0, &made)) return false;
        if (!made &&
            !add_with_text(fx, other, EFFECT(AFRAID), 0, 0xff, true, 1, false, 0x303c,
                           "is afraid"))
            return false;
    }
    return true;
}

/* 3f44:3692, 0x6f, the terror 0x52 gives: when it goes, a player
 * character turned by it (+0xe7 0xb3) is given back (0, and +0x18b 0),
 * and its combat record's +0x10 is cleared. With no combat record the
 * original writes to 0000:0010; the port fails. */
static bool h_terror(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    if (!removing) return true;
    if (cok_field_u8(L, c->record, COK_FIELD_NPC) == 0xb3) {
        cok_field_set8(L, c->record, COK_FIELD_NPC, 0);
        cok_field_set8(L, c->record, COK_FIELD_COMPUTER, 0);
    }
    if (c->combat == NULL)
        return fail(fx, "effect 0x6f (3f44:3692) writes the combat record of a record with none, "
                        "at 0000:0010");
    c->combat->forced = 0;
    return true;
}

/* 3f44:34f9, 0x6b. Removing it gives a player character made an NPC back
 * (+0xe7 0xb3 to 0) and sets the side (+0x18a) to the value. Applied (no
 * event or spell runs it so; an effect a file gave), as berserk in combat
 * but in any mode and without its text: the computer controls the
 * character (+0x18b), made an NPC (+0xe7 0xb3, or 0xb2 for another NPC),
 * its combat record's target becomes the second combatant 6b30:08d8 lists
 * around it (DS:6a36), and its side the one against that one's. Outside
 * combat the original writes through the combat record that combat freed;
 * the port fails. */
static bool h_turn(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    uint8_t *r = c->record;
    if (removing) {
        /* A removal always gives the effect; this guards direct calls. */
        if (effect == NULL)
            return fail(fx, "effect 0x6b (3f44:34f9) is run with no effect, whose value it reads");
        if (cok_field_u8(L, r, COK_FIELD_NPC) == 0xb3) cok_field_set8(L, r, COK_FIELD_NPC, 0);
        cok_field_set8(L, r, COK_FIELD_HOSTILE, effect->value);
        return true;
    }
    cok_field_set8(L, r, COK_FIELD_COMPUTER, 1);
    cok_field_set8(L, r, COK_FIELD_NPC,
                   cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7f ||
                           cok_field_u8(L, r, COK_FIELD_NPC) == 0xb3
                       ? 0xb3
                       : 0xb2);
    cok_combat_record *cr = combat_of(fx, c, 0x6b, 0x34f9);
    if (cr == NULL) return false;
    cr->target = NULL;
    uint8_t *target = NULL;
    if (!fx->in_battle || fx->second == NULL || !fx->second(fx, c, &target, fx->context))
        return fail(fx, "effect 0x6b (3f44:34f9) could not list the combatants around one "
                        "(6b30:08d8)");
    cr->target = target;
    if (target == NULL)
        return fail(fx, "effect 0x6b (3f44:34f9) reads a target with none, at 0000:018a");
    cok_field_set8(L, r, COK_FIELD_HOSTILE,
                   (uint8_t)(cok_field_u8(L, target, COK_FIELD_HOSTILE) ^ 1));
    return true;
}

/* 3f44:3643, 0x6e: when it ends, +0x5f becomes (knight level + former
 * knight level, if it may be used, - 1) / 5 + 1. Below 1 the division
 * overflows: runtime error 200. */
static bool h_knight(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    if (!removing) return true;
    uint8_t *r = c->record;
    int16_t former = cok_character_former_class(r, fx->rules)
                         ? cok_field_s8_at(L, r, COK_FIELD_FORMER_LEVEL, 7)
                         : 0;
    int16_t levels = (int16_t)(former + cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 7) - 1);
    if (levels < 0) return fail(fx, "effect 0x6e (3f44:3643) overflows a division (runtime error 200)");
    cok_field_set8(L, r, COK_FIELD_KNIGHT_TIER, (uint8_t)(levels / 5 + 1));
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

/* dkk 504d:26e0, 0x6c, blinded by smoke: the damage and the attack roll 2
 * worse. */
static bool h_smoked(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    fx->rolls.amount = (uint8_t)(fx->rolls.amount - 2);
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 2);
    return true;
}

/* dkk 504d:2448, 0x6a, a nightmare's smoke: of the combatants listed
 * within a cell of c (6e48:08d6; from the second, the first being c) on
 * the other side and not yet blinded by smoke (0x6c), the one whose THAC0
 * plus its first slot's attacks times six times its dice plus its bonus
 * (words, compared signed) is the most, the first of equals, gets 0x6c for
 * good unless it saves (type 3) or something cancels it: "is blinded by
 * smoke". */
static bool h_smoke(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    cok_character *listed[COK_PARTY_RECORDS + 1], *best = NULL;
    uint8_t n;
    if (!around(fx, c, 1, listed, &n)) return false;
    int16_t most = 0;
    for (unsigned i = 2; i <= n; ++i) {
        cok_character *e = listed[i];
        const uint8_t *r = e->record;
        uint8_t side = cok_field_u8(L, c->record, COK_FIELD_HOSTILE);
        if (cok_field_u8(L, r, COK_FIELD_HOSTILE) == side ||
            cok_character_find_effect(e, 0x6c) != NULL)
            continue;
        uint16_t blow = (uint16_t)(cok_field_u8_at(L, r, COK_FIELD_DICE, 1) * 6 +
                                   cok_field_s8_at(L, r, COK_FIELD_DAMAGE_BONUS, 1));
        uint16_t attacks = cok_field_u8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1);
        int16_t score = (int16_t)(uint16_t)(cok_field_u8(L, r, COK_FIELD_THAC0) + attacks * blow);
        if (score > most) {
            most = score;
            best = e;
        }
    }
    if (best == NULL) return true;
    bool made;
    if (!saving_throw(fx, best, 3, 0, &made)) return false;
    return add_with_text(fx, best, 0x6c, 0, 0, true, 1, made, 0x2448, "is blinded by smoke");
}

/* dkk 504d:1f6b, 0x50, and 2162, 0x5c (504d:006d): cancel the damage and
 * the effect being added, if that is the entry's value (0x23, 0x6f), as
 * 0x61 cancels 0x34. */
static bool h_cancel(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    cancel(fx, fx->rules->effects.handlers[fx->running].value);
    return true;
}

/* Death Knights' own handlers (dkk 504d, 5dc5), each from its routine. */

/* The entry's value, a constant of the routine (cok_rules_effect). */
static uint8_t entry_value(const cok_effects *fx)
{
    return fx->rules->effects.handlers[fx->running].value;
}

/* dkk 504d:04a5 (0x0d), 1044 (0x39) and 10b4 (0x3c), with either flag: the
 * armour class, front and back, changed in place by the entry's value
 * (-2, -4, +4; 60 less the AC), until the stats are next recomputed. */
static bool h_armour(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    uint8_t by = entry_value(fx);
    cok_field_set8(L, c->record, COK_FIELD_AC, (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_AC) + by));
    cok_field_set8(L, c->record, COK_FIELD_AC_BEHIND,
                   (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_AC_BEHIND) + by));
    return true;
}

/* dkk 504d:0f0d, 0x2a, slow: with flag 0, the rate and the movement
 * (DS:be6f) halved. */
static bool h_slow_movement(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect, (void)c;
    if (removing) return true;
    fx->rolls.rate = (uint8_t)(fx->rolls.rate >> 1);
    fx->rolls.movement = (uint8_t)(fx->rolls.movement >> 1);
    return true;
}

/* dkk 504d:123d, 0x42: as 3f44:3450 (0x67), but an item helps only with a
 * bonus (+0x32, signed) of the entry's value (3) or more. */
static bool h_plus_weapons(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)c;
    cok_character *attacker = selected(fx, effect != NULL ? effect->id : fx->running);
    if (attacker == NULL) return false;
    const uint8_t *item = striking_item(fx, attacker);
    if (fx->failed) return false;
    if ((item == NULL || (int8_t)item[0x32] < (int8_t)entry_value(fx)) &&
        (cok_field_s8(L, attacker->record, COK_FIELD_RACE) < 7 ||
         cok_field_s8(L, attacker->record, COK_FIELD_HIGHEST_LEVEL) < 4))
        fx->rolls.amount = 0;
    return true;
}

/* dkk 504d:1f79 (0x53), 1c64 (0x4b) and 1228 (0x41): damage of a type in
 * the entry's value halved (electricity or cold), doubled (cold) or none
 * (electricity), the effect pending kept. */
static bool h_type_half(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if ((fx->rolls.damage_type & entry_value(fx)) != 0)
        fx->rolls.amount = (uint8_t)(fx->rolls.amount >> 1);
    return true;
}

static bool h_type_double(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if ((fx->rolls.damage_type & entry_value(fx)) != 0)
        fx->rolls.amount = (uint8_t)(fx->rolls.amount << 1);
    return true;
}

static bool h_type_none(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    if ((fx->rolls.damage_type & entry_value(fx)) != 0) fx->rolls.amount = 0;
    return true;
}

/* dkk 504d:1815 (0x43) and 1836 (0x44), on a hit (events 2 and 3): the
 * target of c's attack (combat record +0x0a) is drained of the entry's
 * levels (504d:14c7, the drain hook); none, nothing. */
static bool h_drain_levels(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    uint8_t id = fx->running;
    cok_combat_record *cr = combat_of(fx, c, id, fx->rules->effects.handlers[id].address);
    if (cr == NULL) return false;
    if (cr->target == NULL) return true;
    cok_character *t = target_of(fx, cr->target, id, fx->rules->effects.handlers[id].address, 0);
    if (t == NULL) return false;
    if (fx->drain != NULL && fx->drain(fx, t, entry_value(fx), fx->context)) return true;
    return fail(fx, "effect 0x%02x (dkk 504d:14c7) could not drain levels", id);
}

/* dkk 504d:1f9c, 0x54, at the start of a turn (event 0x0f): one below its
 * most hit points (+0x28) heals the entry's value, up to its most. */
static bool h_heal_round(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    uint8_t *r = c->record, most = cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS);
    if (cok_field_u8(L, r, COK_FIELD_HIT_POINTS) >= most) return true;
    uint8_t hp = (uint8_t)(cok_field_u8(L, r, COK_FIELD_HIT_POINTS) + entry_value(fx));
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, hp > most ? most : hp);
    return true;
}

/* Hit points healed by amount, as a byte, up to the most, after "is
 * healed" (DS:1f30; 65cd:1a21, row 10, a pause). */
static bool healed(cok_effects *fx, cok_character *c, uint8_t amount)
{
    const cok_character_layout *L = fx->rules->character;
    uint8_t *r = c->record;
    if (!say(fx, c, fx->running, fx->rules->effects.handlers[fx->running].address, "is healed"))
        return false;
    uint8_t hp = (uint8_t)(cok_field_u8(L, r, COK_FIELD_HIT_POINTS) + amount);
    uint8_t most = cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS);
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, most < hp ? most : hp);
    return true;
}

/* dkk 504d:1c7e, 0x4c (a fire minion's), against fire: magical fire heals
 * as much as it would hurt; any fire does no damage and no effect. */
static bool h_fire_heals(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if ((fx->rolls.damage_type & 1) == 0) return true;
    if ((fx->rolls.damage_type & 8) != 0 && !healed(fx, c, fx->rolls.amount)) return false;
    cancel(fx, 0);
    return true;
}

/* 1257 and "is " "Slowed" (DS:4570, 4bcb; row 10, a pause): effect 0x2a,
 * slow, for minutes with value 0xff. */
static bool slowed(cok_effects *fx, cok_character *c, uint16_t minutes)
{
    return add(fx, c, EFFECT(SLOWED), minutes, 0xff, false) &&
           say(fx, c, fx->running, fx->rules->effects.handlers[fx->running].address, "is Slowed");
}

/* dkk 504d:2005, 0x57 (an iron golem's), against a spell's damage (events
 * 6 and 9): magical fire heals as much as it would hurt; else electricity
 * slows it for 3 minutes; and no damage and no effect take hold. */
static bool h_iron_golem(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    uint8_t type = fx->rolls.damage_type;
    bool ok = true;
    if ((type & 1) != 0 && (type & 8) != 0) ok = healed(fx, c, fx->rolls.amount);
    else if ((type & 4) != 0) ok = slowed(fx, c, 3);
    if (ok) cancel(fx, 0);
    return ok;
}

/* dkk 504d:22bc, 0x65 (a flesh golem's): magical electricity heals as many
 * hit points as the damage had dice (DS:be71); else magical fire or cold
 * slows it for 2d6 minutes; and no damage and no effect take hold. */
static bool h_flesh_golem(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    uint8_t type = fx->rolls.damage_type;
    bool ok = true;
    if ((type & 4) != 0 && (type & 8) != 0) ok = healed(fx, c, fx->rolls.dice);
    else if ((type & 3) != 0 && (type & 8) != 0) ok = slowed(fx, c, roll(fx, 2, 6));
    if (ok) cancel(fx, 0);
    return ok;
}

/* dkk 504d:1bff, 0x4a (a fire minion's touch), on a hit (events 2 and 3),
 * in a battle: the target of c's attack, if any, takes 2d6 (638c:1233) of
 * type 0, half if it makes a saving throw of type 4 (638c:1c52). */
static bool h_fire_touch(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if (fx->vm->mode != 5) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x4a, 0x1bff);
    if (cr == NULL) return false;
    if (cr->target == NULL) return true;
    cok_character *t = target_of(fx, cr->target, 0x4a, 0x1bff, 0);
    if (t == NULL) return false;
    fx->rolls.damage_type = 0;
    uint8_t d = cok_dice_count(&fx->vm->seed, 2, 6, &fx->rolls.dice);
    bool made;
    if (!saving_throw(fx, t, 4, 0, &made)) return false;
    if (fx->damage != NULL && fx->damage(fx, t, d, 2, made, fx->context)) return true;
    return fail(fx, "effect 0x4a (dkk 504d:1bff) could not deal damage (638c:1c52)");
}

/* dkk 504d:26f8, 0x6e: when it ends, the knight tier (+0x25) becomes the
 * level of the entry's class (3) / 5 + 1. Below 0 the division overflows:
 * runtime error 200. */
static bool h_tier(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    if (!removing) return true;
    int8_t level = cok_field_s8_at(L, c->record, COK_FIELD_CLASS_LEVEL, entry_value(fx));
    if (level < 0)
        return fail(fx, "effect 0x6e (dkk 504d:26f8) overflows a division (runtime error 200)");
    cok_field_set8(L, c->record, COK_FIELD_KNIGHT_TIER, (uint8_t)(level / 5 + 1));
    return true;
}

/* dkk 504d:0a7e, 0x20, on being targeted and attacked (events 1, 0x10):
 * with flag 0, a holder whose creature flags (+0x7a) hold bit 6 and which
 * lacks 0x18 itself cannot be targeted, and the attack roll is 4 lower. */
static bool h_unseen(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    if (removing || (cok_field_u8_at(L, c->record, COK_FIELD_CREATURE_FLAGS, 0) & 0x40) == 0 ||
        FIND_EFFECT(c, SEES_INVISIBLE) != NULL)
        return true;
    fx->rolls.untargetable = 1;
    fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 4);
    return true;
}

/* dkk 504d:286b, 0x75, at the damage (event 4), in a battle, with flag 0:
 * against a target (combat record +0x0a) whose creature flags hold bit 1
 * the damage is doubled; one without that is of a kind of undead (+0x31)
 * "is disrupted" (638c:00c8, gone). */
static bool h_disrupt_undead(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    if (fx->vm->mode != 5 || removing) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x75, 0x286b);
    if (cr == NULL) return false;
    cok_character *t = target_of(fx, cr->target, 0x75, 0x286b, 0x007a);
    if (t == NULL) return false;
    if ((cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0) & 2) != 0) {
        fx->rolls.amount = (uint8_t)(fx->rolls.amount << 1);
        return true;
    }
    if (cok_field_u8(L, t->record, COK_FIELD_UNDEAD) == 0) return true;
    if (fx->kill != NULL && fx->kill(fx, t, 8, "is disrupted", fx->context)) return true;
    return fail(fx, "effect 0x75 (dkk 504d:286b) could not kill (638c:00c8)");
}

/* dkk 504d:25bb, 0x6b: 3f44:34f9's, but applied outside a battle it does
 * what its removal does. */
static bool h_turn_battle(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    return h_turn(fx, removing || fx->vm->mode != 5, effect, c);
}

/* dkk 504d:09d5, 0x1e, nausea, with either flag, in a battle: one that may
 * use items "is coughing" (DS:1e14); it may neither use them nor cast; its
 * stats are recomputed (65cd:0e46), then its armour class, front and back,
 * 2 worse; then the side panel if it is selected (65cd:0c44). */
static bool h_nausea(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    if (fx->vm->mode != 5) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x1e, 0x09d5);
    if (cr == NULL) return false;
    if (cr->may_use != 0 && !say(fx, c, 0x1e, 0x09d5, "is coughing")) return false;
    cr->may_use = 0;
    cr->may_cast = 0;
    char why[200];
    if (!cok_character_stats(c, fx->rules, fx->types, why, sizeof why)) return fail(fx, "%s", why);
    uint8_t *r = c->record;
    cok_field_set8(L, r, COK_FIELD_AC, (uint8_t)(cok_field_u8(L, r, COK_FIELD_AC) - 2));
    cok_field_set8(L, r, COK_FIELD_AC_BEHIND, (uint8_t)(cok_field_u8(L, r, COK_FIELD_AC_BEHIND) - 2));
    if (fx->panel == NULL || fx->vm->character != r || fx->panel(fx, c, fx->context)) return true;
    return fail(fx, "effect 0x1e (dkk 504d:09d5) could not draw the side panel (65cd:0c44)");
}

/* dkk 504d:067c, 0x16 (Slow Poison's), with either flag: a poisoned
 * character (0x37) "collapses from poison" (DS:1dcb; 638c:00c8, status 4),
 * keeping its 0x37; then, effects being cured (DS:be75 set, and cleared
 * after), its first 0x0f goes (638c:01c6). */
static bool h_poison_collapse(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if (FIND_EFFECT(c, POISONED) != NULL &&
        (fx->kill == NULL || !fx->kill(fx, c, 4, "collapses from poison", fx->context)))
        return fail(fx, "effect 0x16 (dkk 504d:067c) could not kill (638c:00c8)");
    fx->rolls.curing = 1;
    bool ok = remove_effect(fx, c, NULL, EFFECT(POISON));
    fx->rolls.curing = 0;
    return ok;
}

/* dkk 504d:0124, 0x02 (olin's quarterstaff), on a hit (event 2), in a
 * battle, with flag 0: unless the target of c's attack (combat record
 * +0x0a, read with none too) makes a saving throw of type 1, it gets the
 * entry's effect (6, held) for 3 minutes, "is stunned" (DS:1d82;
 * 638c:1f86). */
static bool h_stun_hit(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)effect;
    if (fx->vm->mode != 5 || removing) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x02, 0x0124);
    if (cr == NULL) return false;
    cok_character *t = target_of(fx, cr->target, 0x02, 0x0124, 0x00b6);
    if (t == NULL) return false;
    bool made;
    if (!saving_throw(fx, t, 1, 0, &made)) return false;
    return made || add_with_text(fx, t, entry_value(fx), 3, 0, false, 0, false, 0x0124,
                                 "is stunned");
}

/* dkk 504d:0219, 0x04 (Dispel Evil's, on its caster), being attacked
 * (event 0x0b), with flag 0: an attack roll 7 lower when the selected
 * character (the attacker) has creature flag 1 (+0x7a). */
static bool h_evil_avoid(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect, (void)c;
    if (removing) return true;
    cok_character *attacker = selected(fx, 0x04);
    if (attacker == NULL) return false;
    if ((cok_field_u8_at(L, attacker->record, COK_FIELD_CREATURE_FLAGS, 0) & 1) != 0)
        fx->rolls.attack_roll = (uint8_t)(fx->rolls.attack_roll - 7);
    return true;
}

/* dkk 504d:05bf, 0x15 (Silence's, shared within a square), at the start of
 * a turn (event 0x0f), with either flag, in a battle: 3f44:06b2's silence
 * ("is silenced" if it may use items; neither items nor spells), then one
 * casting (its combat record's spell) loses it (fx->lose_spell). */
static bool h_silence_spell(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect;
    if (fx->vm->mode != 5) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x15, 0x05bf);
    if (cr == NULL) return false;
    if (cr->may_use != 0 && !say(fx, c, 0x15, 0x05bf, "is silenced")) return false;
    cr->may_use = 0;
    cr->may_cast = 0;
    if (cr->spell == 0) return true;
    if (fx->lose_spell != NULL && fx->lose_spell(fx, c, fx->context)) return true;
    return fail(fx, "effect 0x15 (dkk 504d:05bf) could not lose the spell (65cd:17c4)");
}

/* dkk 504d:0aee, 0x22 (Dispel Evil's), at the damage (event 4), in a
 * battle, with flag 0: the target of c's attack (combat record +0x0a, kept
 * in DS:be7c, read with none too) of creature flag 1 that fails a saving
 * throw of type 4 "is dispelled" (DS:1e29; 638c:00c8, status 8), and c
 * loses its first 0x04 and 0x22 (638c:01c6); any other "resists dispel
 * evil" (DS:1e3e, row 10), then a pause. */
static bool h_dispel_evil(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)effect;
    if (removing || fx->vm->mode != 5) return true;
    cok_combat_record *cr = combat_of(fx, c, 0x22, 0x0aee);
    if (cr == NULL) return false;
    fx->rolls.target = cr->target;
    cok_character *t = target_of(fx, cr->target, 0x22, 0x0aee, 0x007a);
    if (t == NULL) return false;
    bool made = true;
    if ((cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0) & 1) != 0 &&
        !saving_throw(fx, t, 4, 0, &made))
        return false;
    if (made) {
        if (!say(fx, t, 0x22, 0x0aee, "resists dispel evil")) return false;
        if (fx->pause != NULL) fx->pause(fx, fx->context);
        return true;
    }
    if (fx->kill == NULL || !fx->kill(fx, t, 8, "is dispelled", fx->context))
        return fail(fx, "effect 0x22 (dkk 504d:0aee) could not kill (638c:00c8)");
    return remove_effect(fx, c, NULL, 0x04) && remove_effect(fx, c, NULL, 0x22);
}

/* dkk 504d:0f4f, 0x30 (Barkskin's), with either flag, being attacked and
 * at a saving throw (events 0x0b, 0x0c): the armour class, front and back,
 * 1 better in place, until the stats are next recomputed; and the saving
 * throw 1 better, but against a spell being cast (DS:be70) whose saving
 * throw is of type 4 (the spell table's column 9). */
static bool h_barkskin(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_character_layout *L = fx->rules->character;
    (void)removing, (void)effect;
    uint8_t *r = c->record;
    cok_field_set8(L, r, COK_FIELD_AC, (uint8_t)(cok_field_u8(L, r, COK_FIELD_AC) + 1));
    cok_field_set8(L, r, COK_FIELD_AC_BEHIND, (uint8_t)(cok_field_u8(L, r, COK_FIELD_AC_BEHIND) + 1));
    uint8_t spell = fx->rolls.spell;
    if (spell == 0 || spell_byte(fx, spell, 9) != 4) ++fx->rolls.save_roll;
    return true;
}

/* dkk 504d:1063, 0x3a (Mind Blank's): the damage and the effect a spell is
 * adding cancelled (504d:006d) where that is 0x0b (charmed), 0x23
 * (confused), 0x34 (paralysed), 0x3d or 0x6f (terror). No event runs it. */
static bool h_mind_blank(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    static const uint8_t ids[] = {0x0b, 0x23, 0x34, 0x3d, 0x6f};
    for (size_t i = 0; i < sizeof ids; ++i) cancel(fx, ids[i]);
    return true;
}

/* dkk 5dc5:51cc (0x5a, 0x64, 0x68), 4fd3 (0x51), 4f97 (0x4e) and 504d:186e
 * (0x46), with either flag (events 0x0e and 0x0f): a monster breathes,
 * gazes, casts or bursts into flames (fx->breathe, cok_cast_breath). */
static bool h_monster(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing;
    uint8_t id = fx->running;
    uint16_t at = fx->rules->effects.handlers[id].address;
    if (effect == NULL) return fail(fx, "effect 0x%02x (dkk %04x) is run with no effect", id, at);
    if (!fx->in_battle || fx->breathe == NULL)
        return fail(fx, "effect 0x%02x (dkk %04x) acts outside combat, through the combat "
                        "targets and the map", id, at);
    if (!fx->breathe(fx, effect, c, fx->context))
        return fail(fx, "effect 0x%02x (dkk %04x) could not act", id, at);
    return true;
}

/* 3f44:3888, 0x78: the item routine (see call_handler), which takes the
 * effect it is given for an item and adds (flag 0) or removes (flag 1) the
 * effect of id +0x3d. Run as an effect's own handler, which no item,
 * monster or spell gives (only a file could), it reads that id 61 bytes
 * into a 9-byte record: in the heap block three after it, or the slack of
 * one, or another record, as the heap was laid out; the port fails. */
static bool h_item(cok_effects *fx, bool removing, cok_effect *effect, cok_character *c)
{
    (void)removing, (void)effect, (void)c;
    return fail(fx, "effect 0x78 (3f44:3888) run for its own effect reads an id at +0x3d, past "
                    "the 9-byte record, from whatever the heap holds there (dkk 0x79, 504d:28e6)");
}

/* The handler table the original fills at DS:6b94 (3f44:38ea), by effect
 * id: the handler's address in overlay 3f44 (or 5b04, which fills a few
 * at startup, 5b04:58ed), 0 for none, and the port. */
/* The port's handler of each kind (spells.h), which the game's table maps
 * its effect ids to (its effects section; Champions' DS:6b94 + 4 * id). */
static handler_fn *const handlers[COK_FX_HANDLERS] = {
    [COK_FX_BLESS] = h_bless,
    [COK_FX_CURSE] = h_curse,
    [COK_FX_SNAKES] = h_snakes,
    [COK_FX_BREATH] = h_breath,
    [COK_FX_NOTHING] = h_none,
    [COK_FX_KENDER] = h_kender,
    [COK_FX_PROTECTION_EVIL] = h_protection_evil,
    [COK_FX_PROTECTION_GOOD] = h_protection_good,
    [COK_FX_RESIST_COLD] = h_resist_cold,
    [COK_FX_CHARM] = h_charm,
    [COK_FX_NEW_FORM] = h_new_form,
    [COK_FX_TICKING] = h_ticking,
    [COK_FX_SHIELD] = h_shield,
    [COK_FX_RESIST_CHARM_30] = h_resist_charm_30,
    [COK_FX_RESIST_FIRE] = h_resist_fire,
    [COK_FX_SILENCED] = h_silenced,
    [COK_FX_POISONED] = h_poisoned,
    [COK_FX_HAMMER] = h_hammer,
    [COK_FX_INVISIBLE] = h_invisible,
    [COK_FX_TARGET_BONUS] = h_target_bonus,
    [COK_FX_HELD] = h_held,
    [COK_FX_IMAGES] = h_images,
    [COK_FX_QUARTER_LESS] = h_quarter_less,
    [COK_FX_COUGHING] = h_coughing,
    [COK_FX_BLIND] = h_blind,
    [COK_FX_DISEASE] = h_disease,
    [COK_FX_CONFUSED] = h_confused,
    [COK_FX_MINUS_FOUR] = h_minus_four,
    [COK_FX_BLINK] = h_blink,
    [COK_FX_HASTE] = h_haste,
    [COK_FX_HASTE_AGING] = h_haste_aging,
    [COK_FX_CLOUD] = h_cloud,
    [COK_FX_AVOID] = h_avoid,
    [COK_FX_SLOW] = h_slow,
    [COK_FX_DRAIN] = h_drain,
    [COK_FX_AGAINST_FLAG4] = h_against_flag4,
    [COK_FX_IMMOLATE] = h_immolate,
    [COK_FX_PRAYER] = h_prayer,
    [COK_FX_COLD_RESISTANT] = h_cold_resistant,
    [COK_FX_FIRE_RESISTANT] = h_fire_resistant,
    [COK_FX_ADD_INVISIBLE_1] = h_add_invisible_1,
    [COK_FX_PLUS_TWO] = h_plus_two,
    [COK_FX_DEATH_THROES] = h_death_throes,
    [COK_FX_IMMUNE] = h_immune,
    [COK_FX_BURST] = h_burst,
    [COK_FX_FIRE_RESISTANCE] = h_fire_resistance,
    [COK_FX_REGENERATE] = h_regenerate,
    [COK_FX_MINOR_GLOBE] = h_minor_globe,
    [COK_FX_POISON] = h_poison,
    [COK_FX_ZAP] = h_zap,
    [COK_FX_LOSE_WEAPON] = h_lose_weapon,
    [COK_FX_EXPLODES] = h_explodes,
    [COK_FX_PARALYSIS] = h_paralysis,
    [COK_FX_REFLECT] = h_reflect,
    [COK_FX_TERRIBLE] = h_terrible,
    [COK_FX_UNAFFECTED] = h_unaffected,
    [COK_FX_DISPELLING] = h_dispelling,
    [COK_FX_GIANT_SLAYER] = h_giant_slayer,
    [COK_FX_GATE] = h_gate,
    [COK_FX_BERSERK] = h_berserk,
    [COK_FX_STENCH] = h_stench,
    [COK_FX_FEAR] = h_fear,
    [COK_FX_DISPLACEMENT] = h_displacement,
    [COK_FX_HALF_BLUNT] = h_half_blunt,
    [COK_FX_CONSTITUTION_SAVE] = h_constitution_save,
    [COK_FX_RESIST_CHARM_90] = h_resist_charm_90,
    [COK_FX_RESIST_CHARM] = h_resist_charm,
    [COK_FX_FREE_ACTION] = h_free_action,
    [COK_FX_COLD_IMMUNE] = h_cold_immune,
    [COK_FX_POISON_IMMUNE] = h_poison_immune,
    [COK_FX_HALF_EDGED] = h_half_edged,
    [COK_FX_HOLY_WATER] = h_holy_water,
    [COK_FX_MAGIC_WEAPONS] = h_magic_weapons,
    [COK_FX_SLAYER] = h_slayer,
    [COK_FX_STUNNED] = h_stunned,
    [COK_FX_TURN] = h_turn,
    [COK_FX_ADD_INVISIBLE_255] = h_add_invisible_255,
    [COK_FX_KNIGHT] = h_knight,
    [COK_FX_TERROR] = h_terror,
    [COK_FX_FIRE_SHIELD] = h_fire_shield,
    [COK_FX_DISRUPT] = h_disrupt,
    [COK_FX_MINUS_TWO] = h_minus_two,
    [COK_FX_MINUS_ONE] = h_minus_one,
    [COK_FX_ITEM] = h_item,
    [COK_FX_KENDER_BATTLE] = h_kender_battle,
    [COK_FX_HALF_WEAPONS] = h_half_weapons,
    [COK_FX_CANCEL] = h_cancel,
    [COK_FX_SMOKE] = h_smoke,
    [COK_FX_SMOKED] = h_smoked,
    [COK_FX_ARMOUR] = h_armour,
    [COK_FX_SLOW_MOVEMENT] = h_slow_movement,
    [COK_FX_PLUS_WEAPONS] = h_plus_weapons,
    [COK_FX_TYPE_HALF] = h_type_half,
    [COK_FX_TYPE_DOUBLE] = h_type_double,
    [COK_FX_TYPE_NONE] = h_type_none,
    [COK_FX_DRAIN_LEVELS] = h_drain_levels,
    [COK_FX_HEAL_ROUND] = h_heal_round,
    [COK_FX_FIRE_HEALS] = h_fire_heals,
    [COK_FX_IRON_GOLEM] = h_iron_golem,
    [COK_FX_FLESH_GOLEM] = h_flesh_golem,
    [COK_FX_FIRE_TOUCH] = h_fire_touch,
    [COK_FX_TIER] = h_tier,
    [COK_FX_UNSEEN] = h_unseen,
    [COK_FX_DISRUPT_UNDEAD] = h_disrupt_undead,
    [COK_FX_TURN_BATTLE] = h_turn_battle,
    [COK_FX_NAUSEA] = h_nausea,
    [COK_FX_POISON_COLLAPSE] = h_poison_collapse,
    [COK_FX_DRAGON_BREATH] = h_monster,
    [COK_FX_GAZE] = h_monster,
    [COK_FX_CAST] = h_monster,
    [COK_FX_FLAMES] = h_monster,
    [COK_FX_STUN_HIT] = h_stun_hit,
    [COK_FX_EVIL_AVOID] = h_evil_avoid,
    [COK_FX_SILENCE_SPELL] = h_silence_spell,
    [COK_FX_DISPEL_EVIL] = h_dispel_evil,
    [COK_FX_BARKSKIN] = h_barkskin,
    [COK_FX_MIND_BLANK] = h_mind_blank,
};

/* Run the handler of id (60f4:01a8). While DS:713c is set the original
 * calls 3f44:3888, entry 0x78 (DS:6d74), instead, with an item for the
 * effect: only readying or unreadying an item with a power (546c:1d96)
 * and Remove Curse (5b04:35f5) set it, just before their call, and 3888
 * clears it first; the port does what they do there (items.c, cast.c),
 * so it is never set here. */
static bool call_handler(cok_effects *fx, uint8_t id, bool removing, cok_effect *effect,
                         cok_character *c)
{
    const cok_rules_effects *E = &fx->rules->effects;
    if (id >= E->count) return fail(fx, "effect 0x%02x is past the handler table (DS:6b94)", id);
    const cok_rules_effect *h = &E->handlers[id];
    if (h->unported != NULL)
        return fail(fx, "not yet ported: %s's effect 0x%02x (%s)", fx->rules->text.name, id,
                    h->unported);
    if (h->handler == COK_FX_MISSING)
        return fail(fx, "effect 0x%02x has no handler; the original calls 0000:0000", id);
    fx->running = id;
    if (h->battle && fx->vm->mode != 5) return true; /* dkk DS:8c4e */
    return handlers[h->handler](fx, removing, effect, c);
}

/* 60f4:1628: add a class's hit points for level to *hp, as a byte: the
 * level is capped below the class's top (DS:3903), a ranger's raised by
 * one unless it has a former level other than its own (+0xd7), and
 * fighters, rangers and knights get 1-7 a level by constitution 15-25,
 * others 1 for 15 and 2 above it. */
static void class_hit_points(const cok_rules *rules, const cok_data_segment *D, const uint8_t *c,
                             unsigned klass, uint8_t level, uint8_t constitution, uint8_t *hp)
{
    uint8_t top = cok_data_u8_at(D, COK_DATA_TOP_LEVELS, klass);
    if (top <= level) level = (uint8_t)(top - 1);
    if (klass == 4 &&
        (cok_character_change_level(rules, c) == 0 ||
         cok_character_former_level(rules, c, 4) == cok_character_change_level(rules, c)))
        ++level;
    unsigned per = 0;
    if (klass < 8 && (rules->characters.fighter_classes >> klass & 1) != 0) {
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

/* 60f4:1743: the base score (+0x10 + 2 * stat) changed by the readied
 * items' powers (+0x3e 0x80 + power, +0x3d its kind) and effects, into the
 * current one; strength (0) cok_character_strength's. Intelligence (1)
 * and wisdom (2) are computed and not stored. */
static bool ability(cok_effects *fx, cok_character *ch, unsigned stat)
{
    if (stat == 0) {
        cok_character_strength(ch, fx->rules);
        return true;
    }
    const cok_character_layout *L = fx->rules->character;
    const cok_data_segment *D = fx->rules->data;
    uint8_t *c = ch->record;
    uint8_t score = cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, stat);
    for (size_t i = 0; i < ch->item_count; ++i) {
        const uint8_t *item = ch->items[i];
        if (item[0x3e] <= 0x80 || item[0x34] == 0) continue;
        uint8_t power = item[0x3e] & 0x7f, kind = item[0x3d];
        switch (stat) {
        case 1: /* 0x0c and 0x0d would set 7 and 3, but nothing is stored. */
        case 2:
            break;
        case 3: {
            uint8_t dexterity = cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, COK_SCORE_DEXTERITY);
            if (power == 2)
                score = (uint8_t)(score + (dexterity <= 6 ? 4 : dexterity <= 13 ? 2 : 1));
            else if (power == 8 && dexterity < 18 && kind == 3) ++score;
            else if (power == 10) score = (uint8_t)(score - 2);
            break;
        }
        case 4:
            if (power == 6 ||
                (power == 8 &&
                 cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, COK_SCORE_CONSTITUTION) < 18 &&
                 kind == 4))
                ++score;
            break;
        default:
            if (power == 6) --score;
            else if (power == 8 &&
                     cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, COK_SCORE_CHARISMA) < 18 &&
                     kind == 5)
                ++score;
            break;
        }
    }
    if (stat == 1 || stat == 2) return true;
    if (stat == 3) {
        cok_field_set8_at(L, c, COK_FIELD_SCORE, COK_SCORE_DEXTERITY, score);
        return true;
    }
    if (stat == 5) {
        const cok_effect *effect = FIND_EFFECT(ch, FRIENDLY);
        if (effect != NULL) score = (uint8_t)(score + effect->value);
        cok_field_set8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CHARISMA, score);
        return true;
    }
    if (stat == 4) {
        /* The maximum hit points from +0x11b (the first level's) and each
         * class's levels, former (+0x101) and current (+0xf9, above +0xd7,
         * capped by DS:3903), divided by the number of classes with a
         * level; the hit points change by as much. */
        uint8_t old = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS), hp = 0, classes = 0;
        cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS,
                       cok_field_u8(L, c, COK_FIELD_BASE_HIT_POINTS));
        for (unsigned k = 0; k < 8; ++k) {
            uint8_t former = (uint8_t)cok_character_former_level(fx->rules, c, k);
            if (former > 0) class_hit_points(fx->rules, D, c, k, former, score, &hp);
            uint8_t level = cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, k);
            if (level > 0) ++classes;
            if (cok_data_u8_at(D, COK_DATA_TOP_LEVELS, k) < level)
                level = cok_data_u8_at(D, COK_DATA_TOP_LEVELS, k);
            uint8_t change = (uint8_t)cok_character_change_level(fx->rules, c);
            if (level > change)
                class_hit_points(fx->rules, D, c, k, (uint8_t)(level - change), score, &hp);
        }
        if (classes == 0)
            return fail(fx, "constitution's hit points (60f4:1743) divide by no classes, runtime "
                            "error 200");
        cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS,
                       (uint8_t)(cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) + hp / classes));
        if (cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) > old)
            cok_field_set8(L, c, COK_FIELD_HIT_POINTS,
                           (uint8_t)(cok_field_u8(L, c, COK_FIELD_HIT_POINTS) +
                                     (cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) - old)));
        if (cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) < old) {
            if (cok_field_u8(L, c, COK_FIELD_HIT_POINTS) >
                old - cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))
                cok_field_set8(L, c, COK_FIELD_HIT_POINTS,
                               (uint8_t)(cok_field_u8(L, c, COK_FIELD_HIT_POINTS) -
                                         (old - cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS))));
            else cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 0);
        }
        cok_field_set8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION, score);
        if (cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION) >= 20) {
            if (FIND_EFFECT(ch, REGENERATING) == NULL &&
                cok_character_add_effect(ch, EFFECT(REGENERATING), 0x3c, 0xff, true) == NULL)
                return fail(fx, "out of memory adding effect 0x3e");
            return true;
        }
        return remove_effect(fx, ch, NULL, EFFECT(REGENERATING));
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
    const cok_character_layout *L = fx->rules->character;
    if (effect == NULL) effect = cok_character_find_effect(c, id);
    if (effect == NULL) return true;
    if (effect->on_remove) {
        /* A handler that removes its own effect again, as 0x23 may by
         * its dice, recurses until a pass does not; the original then
         * unlinks an effect already gone, writing to 0000:0005. It has
         * no stack check: past some 150 levels its stack wraps. The port
         * stops at 64 (0x03, which never ends, fails in its handler). */
        if (fx->removing == 64)
            return fail(fx, "effect 0x%02x is removed 64 removals deep; the port stops there, "
                            "where the original recurses on until it ends or its stack wraps",
                        effect->id);
        ++fx->removing;
        cok_character *who = c, **outer = fx->target;
        fx->target = &who;
        bool ok = call_handler(fx, id, true, effect, c);
        fx->target = outer;
        --fx->removing;
        if (!ok) return false;
    }
    if (fx->in_battle) {
        char text[64];
        uint8_t stored = cok_field_u8_at(L, c->record, COK_FIELD_NAME, 0);
        snprintf(text, sizeof text, "%.*s loses 0x%02x", stored > 15 ? 15 : stored,
                 (const char *)c->record + cok_field_offset_at(L, COK_FIELD_NAME, 1), effect->id);
        log_text(fx, "effect", text);
    }
    cok_effect **link = &c->effects;
    while (*link != NULL && *link != effect) link = &(*link)->next;
    if (*link == NULL)
        return fail(fx, "effect 0x%02x is removed but not in the list; the original writes to "
                        "0000:0005", effect->id);
    if (fx->removed_count == fx->removed_capacity) {
        size_t capacity = fx->removed_capacity == 0 ? 16 : fx->removed_capacity * 2;
        struct cok_removed *bigger = realloc(fx->removed, capacity * sizeof *bigger);
        if (bigger == NULL) return fail(fx, "out of memory");
        fx->removed = bigger;
        fx->removed_capacity = capacity;
    }
    *link = effect->next;
    fx->removed[fx->removed_count++] = (struct cok_removed){effect, cok_effects_made()};
    if (IS_EFFECT(id, FRIENDLY)) return ability(fx, c, 5);
    if (IS_EFFECT(id, ENLARGED) || IS_EFFECT(id, STRENGTH)) return ability(fx, c, 0);
    return true;
}

bool cok_effects_remove(cok_effects *fx, cok_character *character, cok_effect *effect,
                        uint8_t id)
{
    begin(fx);
    remove_effect(fx, character, effect, id);
    return finish(fx);
}

/* Whether id is one of ids, a 0-ended list. */
static bool listed(const uint8_t *ids, uint8_t id)
{
    for (; *ids != 0; ++ids)
        if (*ids == id) return true;
    return false;
}

/* Run the handler of id for target (60f4:0352): its own effect, or for an
 * effect the party shares, the first in the whole list, a monster's
 * too; in combat the first whose holder has target within 6 cells of it
 * for Prayer (0x31), 1 for the others (6b30:08d8 around the holder,
 * which the original runs with the list it keeps in DS:6a33 saved and
 * restored, but not its count). */
static bool dispatch_id(cok_effects *fx, cok_character *target, uint8_t id)
{
    cok_effect *effect = cok_character_find_effect(target, id);
    if (effect != NULL) return call_handler(fx, id, false, effect, target);
    const cok_rules_effects *E = &fx->rules->effects;
    if (!listed(E->shared, id)) return true; /* 60f4:0332 */
    for (size_t i = 0; i < fx->party->count; ++i) {
        cok_character *holder = fx->party->members[i];
        effect = cok_character_find_effect(holder, id);
        if (effect == NULL) continue;
        if (fx->vm->mode == 5) {
            bool in = false;
            if (fx->in_range == NULL)
                return fail(fx, "effect 0x%02x shared in combat needs the combat map", id);
            char why[200] = "";
            if (!fx->in_range(fx, holder, target, id == E->shared_far_id ? E->shared_far : 1, &in,
                              why, sizeof why, fx->context))
                return fail(fx, "%s", why);
            if (!in) continue;
        }
        return call_handler(fx, id, false, effect, target);
    }
    return true;
}

static bool dispatch_event(cok_effects *fx, cok_character **target, uint8_t event);

/* 60f4:057c, given the caller's pointer to its record, which a handler
 * (0x47's turning) may change: the handlers after it, and the caller, then
 * work on the new one. */
static bool dispatch_at(cok_effects *fx, cok_character **target, uint8_t event)
{
    uint8_t outer = fx->event;
    cok_character **outer_target = fx->target;
    fx->event = event;
    fx->target = target;
    bool ok = dispatch_event(fx, target, event);
    fx->event = outer;
    fx->target = outer_target;
    return ok;
}

static bool dispatch(cok_effects *fx, cok_character *target, uint8_t event)
{
    return dispatch_at(fx, &target, event);
}

static bool dispatch_event(cok_effects *fx, cok_character **target, uint8_t event)
{
    const cok_character_layout *L = fx->rules->character;
    const cok_rules_effects *E = &fx->rules->effects;
    if (event >= E->event_count || E->events[event] == NULL) return true;
    if (event == 6 || event == 9) {
        /* 60f4:04f3, magic resistance (+0x187), when an effect is pending
         * and no damage, or magic damage (dkk 638c:04c9, casting's
         * resist_damage: magic damage with no effect pending too): a d100
         * up to the resistance plus 5 for each caster level of the spell
         * (DS:6b33) below 11, as a byte, resists the damage and the effect
         * (but for 0x5b and 0x52). */
        cok_rolls *r = &fx->rolls;
        uint8_t resistance = cok_field_u8(L, (*target)->record, COK_FIELD_MAGIC_RESISTANCE), level;
        bool magic = (r->damage_type & 8) != 0;
        if (resistance != 0 &&
            (r->pending != 0 || (fx->rules->casting.resist_damage && magic)) &&
            (r->amount == 0 || magic)) {
            if (!caster_level(fx, r->spell, &level)) return false;
            uint8_t chance = (uint8_t)(resistance + 5 * (11 - level));
            if (roll(fx, 1, 100) <= chance) {
                r->amount = 0;
                if (!listed(E->resisted_pending, r->pending)) r->pending = 0;
            }
        }
    }
    for (const uint8_t *id = E->events[event]; *id != 0; ++id)
        if (!dispatch_id(fx, *target, *id)) return false;
    return true;
}

bool cok_effects_dispatch(cok_effects *fx, cok_character *target, uint8_t event)
{
    begin(fx);
    dispatch(fx, target, event);
    return finish(fx);
}

bool cok_effects_dispatch_at(cok_effects *fx, cok_character **target, uint8_t event)
{
    begin(fx);
    dispatch_at(fx, target, event);
    return finish(fx);
}

bool cok_effects_run(cok_effects *fx, cok_character *character, uint8_t id, cok_effect *effect)
{
    begin(fx);
    cok_character *who = character, **outer = fx->target;
    fx->target = &who;
    call_handler(fx, id, false, effect, character);
    fx->target = outer;
    return finish(fx);
}

bool cok_effects_attack(cok_effects *fx, cok_character *target, uint8_t bonus, bool *hit)
{
    const cok_character_layout *L = fx->rules->character;
    begin(fx);
    *hit = false;
    cok_rolls *r = &fx->rolls;
    r->attack_roll = roll(fx, 1, 20);
    if ((int8_t)r->attack_roll > 1) {
        if (r->attack_roll == 20) r->attack_roll = 100;
        if (dispatch(fx, target, 0x10))
            *hit = (int8_t)r->attack_roll >= 0 &&
                   (int)(int8_t)r->attack_roll + bonus >
                       cok_field_u8(L, target->record, COK_FIELD_AC);
    }
    return finish(fx);
}

static bool saving_throw(cok_effects *fx, cok_character *target, uint8_t type, uint8_t bonus,
                         bool *made)
{
    const cok_character_layout *L = fx->rules->character;
    cok_rolls *r = &fx->rolls;
    const uint8_t *c = target->record;
    r->save_made = 1;
    r->save_roll = roll(fx, 1, 20);
    /* dkk 638c:10f8: the holder of an effect rolls 1. */
    uint8_t fails = fx->rules->effects.save_fail_id;
    if (fails != 0 && cok_character_find_effect(target, fails) != NULL) r->save_roll = 1;
    if (r->save_roll == 1) {
        r->save_made = 0;
    } else if (r->save_roll != 20) {
        /* A mage of an order of magic (+0x5e) gets -1 or +1 from the word
         * at 0x4bf8 + order (the byte sum wraps), or (dkk 638c:10f8) the
         * moon's at 0x4cf8 + order: 0 or 2. */
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0 &&
            cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER) != 0) {
            uint8_t order = cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER);
            uint16_t moon =
                fx->rules->effects.save_moon_wraps
                    ? cok_ecl_var_at(fx->vm, COK_VAR_AREA, (uint8_t)(order + 0xf8))
                    : cok_ecl_var_at(fx->vm, COK_VAR_ORDER_MOON, order);
            if (moon == 0) --bonus;
            else if (moon == 2) ++bonus;
        }
        r->save_roll = (uint8_t)(r->save_roll + cok_field_s8(L, c, COK_FIELD_SAVE_BONUS) +
                                 (int8_t)bonus);
        r->save_type = type;
        if (dispatch(fx, target, 0x0c)) {
            /* The type, signed, indexes the saving throws unbounded: kept
             * within the record, not within the five. */
            long at = cok_field_offset_unchecked(L, COK_FIELD_SAVING_THROW, 0, (int8_t)type);
            if (at < 0 || at >= L->size)
                fail(fx, "saving throw type %u is outside the record", type);
            else
                r->save_made = c[at] <= r->save_roll;
        }
    }
    *made = r->save_made != 0;
    return !fx->failed;
}

bool cok_effects_save(cok_effects *fx, cok_character *target, uint8_t type, uint8_t bonus,
                      bool *made)
{
    begin(fx);
    saving_throw(fx, target, type, bonus, made);
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
        if (maybe_reused(fx, last, current))
            return fail(fx, "a handler removed effect 0x%02x, the last to count down, and effect "
                            "0x%02x made since may be where the heap put it, which the walk "
                            "would take for the last", last->id, current->id);
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
            if (maybe_reused(fx, before, member->effects))
                return fail(fx, "a handler removed effect 0x%02x, before the one counted down, "
                                "and effect 0x%02x made since, now the first, may be where the "
                                "heap put it", before->id, member->effects->id);
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
                fail(fx, "a party of %u reads past DS:46fb", cok_ecl_var(vm, COK_VAR_PARTY_SIZE));
                return finish(fx);
            }
            if (fx->timed[i - 1] != 0) any = true;
            ++i;
        } while (!any && i <= cok_ecl_var(vm, COK_VAR_PARTY_SIZE));
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
