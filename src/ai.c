#include "ai.h"

#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "cast.h"
#include "cloud.h"
#include "command.h"
#include "items.h"
#include "magic.h"
#include "round.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool undefined(cok_adventure *game, const char *format, ...)
{
    char text[300];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", text);
    return false;
}

static void name_of(const cok_character_layout *L, const uint8_t *r, char out[16])
{
    uint8_t stored = cok_field_u8_at(L, r, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(out, r + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    out[length] = '\0';
}

/* Run c's effects for event, ending the run where that fails. */
static bool event(cok_adventure *game, cok_character *c, uint8_t ev)
{
    if (cok_effects_dispatch(&game->effects, c, ev)) return true;
    return cok_adventure_effect_failed(game);
}

/* The dice (60f4:1216). */
static uint8_t roll(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice(&game->vm.seed, count, sides);
}

/* A record's combat record, which every record has in a battle; the
 * original reads through NULL for one without. */
static cok_combat_record *record_of(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (c->combat == NULL) {
        char name[16];
        name_of(L, c->record, name);
        undefined(game, "%s has no combat record, read through NULL in combat", name);
    }
    return c->combat;
}

/* The member whose record is record. */
static cok_character *member_of(cok_adventure *game, const uint8_t *record)
{
    size_t i = cok_party_index(&game->party, record);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* The record of combatant n, as DS:68d1 + 4n holds it. */
static cok_character *combatant(cok_adventure *game, uint8_t n, const char *where)
{
    cok_character *c = n <= COK_COMBATANTS ? game->combat.combatant[n].character : NULL;
    if (c == NULL) undefined(game, "combatant %u has no record (%s)", n, where);
    return c;
}

/* An item's type (DS:5886 + 16 * type). */
static bool item_type(cok_adventure *game, const uint8_t *item, const uint8_t **type)
{
    if (item[0x2e] >= COK_ITEM_TYPES)
        return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
    *type = game->item_types.type[item[0x2e]];
    return true;
}

static bool type_past(cok_adventure *game)
{
    return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
}

static bool enemies(cok_adventure *game, cok_character *c, uint8_t range, uint8_t *n)
{
    return cok_combat_enemies(game, c, range, n);
}

static bool distance(cok_adventure *game, const cok_character *origin, const cok_character *target,
                     uint8_t *d)
{
    if (cok_combat_distance(&game->combat, cok_game_rules(game), origin->record, target->record, d))
        return true;
    return undefined(game, "the distance (6346:2888) lists the combatants around one "
                           "(6b30:08d8) past its tables");
}

static bool direction(cok_adventure *game, const cok_character *a, const cok_character *b,
                      uint8_t *d)
{
    if (cok_combat_direction(&game->combat, a, b, d)) return true;
    return undefined(game, "no direction's wedge holds the other (6346:34e9), where the original "
                           "loops for ever");
}

static bool stats(cok_adventure *game, cok_character *c)
{
    char error[300];
    if (cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error))
        return true;
    return undefined(game, "%s", error);
}

/* Blank row 24 (67b5:0c7b). */
static void clear_row(cok_adventure *game)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
}

/* A spell table byte (DS:31b3 + 16 * spell + column), which the AI reads
 * for any byte it is given, marked spells past the table's end too. */
static bool spell_byte(cok_adventure *game, uint8_t spell, unsigned column, uint8_t *byte)
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint16_t at = (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, column);
    if (cok_data_byte(D, at, byte)) return true;
    return undefined(game, "the computer's choice of spell 0x%02x reads DS:%04x, which the port "
                           "does not hold (3afb:03b9)",
                     spell, at);
}

/* The keys. */

/* 3afb:1200 flushes the keyboard as it returns (3afb:12eb), whether or
 * not a key was waiting. */
static bool poll_keys(cok_adventure *game, cok_character *c, bool *back);

bool cok_ai_keys(cok_adventure *game, cok_character *c, bool *back)
{
    bool ok = poll_keys(game, c, back);
    if (ok) cok_adventure_flush(game);
    return ok;
}

static bool poll_keys(cok_adventure *game, cok_character *c, bool *back)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    *back = false;
    if (!cok_adventure_key_pending(game)) return true; /* 1614:03c2 */
    int key = cok_adventure_wait_key(game);
    if (key == 0 && !game->vm.abort) key = cok_adventure_wait_key(game);
    if (game->vm.abort) {
        *back = true;
        return true;
    }
    if (key == 0x32) {
        const cok_rules_fight_words *W = &cok_game_rules(game)->fight.words;
        combat->magic_on = !combat->magic_on;
        cok_camp_notice(game, combat->magic_on ? W->magic_on : W->magic_off); /* 6346:1827 */
    } else if (key == ' ') {
        for (size_t i = 0; i < game->party.count; ++i) {
            uint8_t *r = game->party.members[i]->record;
            if (cok_field_u8(L, r, COK_FIELD_NPC) < 0x80 &&
                cok_field_u8(L, r, COK_FIELD_STATUS) != 1)
                cok_field_set8(L, r, COK_FIELD_COMPUTER, 0);
        }
        if (cok_field_u8(L, c->record, COK_FIELD_COMPUTER) == 0) {
            cok_combat_record *cr = record_of(game, c);
            if (cr == NULL) return false;
            cr->initiative = 0x14;
            *back = true;
        }
    } else if (key == 0x2d && game->helm) {
        if (!cok_combat_gods(game)) return false; /* 432f:41e2 */
    }
    return true;
}

/* The way of moving. The die's sides are left in a byte of the stack that
 * 3afb:024b later reads (see cok_ai_turn_undead). */
static bool pattern(cok_adventure *game, cok_character *c, uint8_t *sides)
{
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    uint8_t row = cr->pattern;
    if (row >= 1 && row <= 4) {
        *sides = 4;
        if (roll(game, 1, 4) != 1) return true;
    }
    if (roll(game, 1, 8) == 8) {
        *sides = 2;
        row = (uint8_t)(roll(game, 1, 2) + 4);
    } else {
        *sides = 4;
        row = roll(game, 1, 4);
    }
    cr->pattern = row;
    return true;
}

bool cok_ai_pattern(cok_adventure *game, cok_character *c)
{
    uint8_t sides;
    return pattern(game, c, &sides);
}

/* Morale. */

/* 1316, with the byte its "is forced to flee" leaves at 024b's [bp-6]:
 * the string's 15th letter, 'l', copied to its [bp-0x15]. */
static bool morale(cok_adventure *game, cok_character *c, bool *over, uint8_t *stale)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    cok_rolls *rolls = &game->effects.rolls;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    *over = false;
    cr->fleeing = 0; /* 60f4:14a2 does nothing */
    if (cr->forced != 0) {
        cr->fleeing = 1;
        *stale = 'l';
        return cok_arena_say(game, c, "is forced to flee", 10, true);
    }
    if (cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7f) return true;
    uint8_t m = (uint8_t)((cok_field_u8(L, r, COK_FIELD_NPC) & 0x7f) * 2);
    rolls->morale = m > 0x66 ? 0 : m;
    if (!event(game, c, 0x11)) return false;
    if (cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS) == 0)
        return undefined(game, "morale with no most hit points divides by zero (3afb:13ca): "
                               "runtime error 200");
    int lost = 100 - (int16_t)(cok_field_u8(L, r, COK_FIELD_HIT_POINTS) * 100) /
                         cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS);
    if (rolls->morale >= lost && rolls->morale != 0) return true;
    rolls->morale = combat->enemy_health;
    if (!event(game, c, 0x11)) return false;
    const cok_rules_fight *F = &cok_game_rules(game)->fight;
    /* As an unsigned word, or (dkk 3f63:1260) signed. */
    uint16_t need = (uint16_t)(100u - cok_ecl_var(&game->vm, COK_VAR_MORALE)); /* var 0x7ec6 */
    bool below = F->morale_signed ? rolls->morale < (int16_t)need : rolls->morale < need;
    if (!below && rolls->morale != 0 && cok_field_u8(L, r, COK_FIELD_HOSTILE) != 0)
        return true;
    uint8_t fastest, mine;
    if (!cok_combat_fastest_enemy(game, c, &fastest) || !cok_combat_movement(game, c, &mine))
        return false;
    if (fastest > mine >> 1) {
        if (cok_field_u8_at(L, r, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE) <= 5) return true;
        if (!cok_combat_leave(game, c, 4, F->words.surrenders)) return false;
        cok_combat_end_turn(c);
        *over = true;
        return true;
    }
    cr->fleeing = 1;
    const cok_rules *rules = cok_game_rules(game);
    if (!F->flee_removes) return true;
    if (!cok_effects_remove(&game->effects, c, NULL,
                            cok_effect_id(rules, COK_EFFECT_DISPELLING)) ||
        !cok_effects_remove(&game->effects, c, NULL,
                            cok_effect_id(rules, COK_EFFECT_GIANT_SLAYER)))
        return cok_adventure_effect_failed(game);
    return true;
}

bool cok_ai_morale(cok_adventure *game, cok_character *c, bool *over)
{
    uint8_t stale;
    return morale(game, c, over, &stale);
}

/* Spells and items. */

/* 3afb:02b7, nested in 03b9: whether the area of spell around x, y takes in
 * one of the selected character's side that fails its saving throw; each
 * listed throws. */
static bool hurts_own(cok_adventure *game, int8_t x, int8_t y, uint8_t spell, bool *hurts)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    *hurts = false;
    cok_character *selected = member_of(game, game->vm.character);
    if (selected == NULL)
        return undefined(game, "an area spell's own side is read through no selected character "
                               "(3afb:02c5)");
    uint8_t bonus = cok_field_u8(L, selected->record, COK_FIELD_HOSTILE) == 0 ? 0xfe : 8;
    uint8_t radius, negates, type;
    if (!spell_byte(game, spell, 15, &radius) || !spell_byte(game, spell, 8, &negates) ||
        !spell_byte(game, spell, 9, &type))
        return false;
    if (!cok_combat_list(combat, cok_game_rules(game), x, y, radius, 0xff, 1))
        return undefined(game, "the list of combatants around one (6b30:08d8) reads past its "
                               "tables");
    unsigned n = combat->listed_count;
    for (unsigned k = 1; k <= n; ++k) {
        selected = member_of(game, game->vm.character);
        if (selected == NULL)
            return undefined(game, "an area spell's own side is read through no selected "
                                   "character (3afb:031c)");
        uint8_t side = cok_combat_opposite(selected, cok_game_rules(game));
        if (k > COK_COMBATANTS) return undefined(game, "the area's list is read past its end");
        cok_character *e = combatant(game, combat->listed[k].index, "3afb:0344");
        if (e == NULL) return false;
        if (cok_field_u8(L, e->record, COK_FIELD_HOSTILE) == side || negates == 1) continue;
        bool made;
        if (!cok_effects_save(&game->effects, e, type, bonus, &made))
            return cok_adventure_effect_failed(game);
        if (!made) *hurts = true;
    }
    return true;
}

/* 5b04:0ecb: the range of spell. */
static bool spell_range(cok_adventure *game, uint8_t spell, uint8_t *range)
{
    uint8_t base, per, kind, level = 6;
    if (!spell_byte(game, spell, 2, &base) || !spell_byte(game, spell, 3, &per) ||
        !spell_byte(game, spell, 6, &kind))
        return false;
    if (game->effects.rolls.item == 0 &&
        !cok_effects_caster_level(&game->effects, spell, &level))
        return cok_adventure_effect_failed(game);
    uint8_t r = (uint8_t)(base + per * level);
    if (r == 0 && kind != 0) r = 1;
    if (r == 0xff) r = 1;
    *range = r;
    return true;
}

bool cok_ai_spell_fits(cok_adventure *game, cok_character *c, uint8_t spell, uint8_t level,
                       bool *fits)
{
    cok_combat *combat = &game->combat;
    *fits = false;
    uint8_t priority, hostile, radius;
    if (!spell_byte(game, spell, 13, &priority)) return false;
    if (priority < level) return true;
    if (!spell_byte(game, spell, 14, &hostile)) return false;
    uint8_t cure_light = cok_game_spell(game, COK_SPELL_CURE_LIGHT_WOUNDS);
    if (spell != cure_light && hostile == 0) {
        *fits = true;
        return true;
    }
    if (spell == cure_light) {
        cok_character *ally;
        if (!cok_cast_wounded_ally(game, c, &ally)) return false;
        if (ally != NULL) {
            *fits = true;
            return true;
        }
    }
    uint8_t range = 0, n;
    if (!spell_range(game, spell, &range) || !enemies(game, c, range, &n)) return false;
    if (n == 0) return true;
    if (!spell_byte(game, spell, 15, &radius)) return false;
    if (radius == 0) {
        *fits = true;
        return true;
    }
    for (unsigned i = 1; i <= n; ++i) {
        const cok_combatant *e = &combat->combatant[combat->enemies[i]];
        bool hurts;
        if (!hurts_own(game, e->x, e->y, spell, &hurts)) return false;
        if (hurts) return true;
    }
    *fits = true;
    return true;
}

bool cok_ai_use_item(cok_adventure *game, cok_character *c, bool *used)
{
    cok_combat *combat = &game->combat;
    *used = false;
    uint8_t passes = roll(game, 1, 7), level = 7;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    /* dkk 3f63:02b8 walks the items after the die but uses none. */
    if (!cok_game_rules(game)->fight.computer_items) return true;
    if (cr->may_use == 0 || combat->sides[cok_combat_opposite(c, cok_game_rules(game))] == 0 ||
        cok_ecl_var(&game->vm, COK_VAR_NO_MAGIC) != 0) /* var 0x4be5 */
        return true;
    size_t found = 0;
    for (unsigned pass = 1; pass <= passes; ++pass, --level) {
        for (size_t i = 0; i < c->item_count && found == 0; ++i) {
            const uint8_t *it = c->items[i];
            uint8_t spell = it[0x3d];
            if (cok_item_is_scroll(it) || it[0x3e] >= 0x80 || it[0x34] == 0 || spell == 0)
                continue;
            const cok_rules_spells *S = &cok_game_rules(game)->spells;
            if (spell > S->item_last) spell = (uint8_t)(spell - S->item_shift);
            bool fits;
            if (!cok_ai_spell_fits(game, c, spell, level, &fits)) return false;
            if (fits) found = i + 1;
        }
    }
    if (found == 0) return true;
    bool done = false;
    if (!cok_item_use(game, c, found - 1, &done)) return false;
    *used = true;
    return true;
}

/* dkk 5340:3fd5: whether spell would do a target on side (its side,
 * else -10) any good: by the spell (Death Knights' ids), a test of the
 * target, else that it does not hold the spell's effect (the spell table's
 * column 10); then not against a globe (0x3b below level 5, 0x3f below 4)
 * nor magic resistance above 50. Above 0 it does. */
static bool worth(cok_adventure *game, uint8_t side, uint8_t spell, cok_character *t,
                  int8_t *good)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = t->record;
#define HOLDS(id) (cok_character_find_effect(t, id) != NULL)
    *good = -10;
    if (cok_field_u8(L, r, COK_FIELD_HOSTILE) != side) return true;
    uint8_t effect, level;
    if (!spell_byte(game, spell, 10, &effect) || !spell_byte(game, spell, 1, &level)) return false;
    int8_t race = cok_field_s8(L, r, COK_FIELD_RACE);
    int8_t highest = cok_field_s8(L, r, COK_FIELD_HIGHEST_LEVEL);
    uint8_t flags0 = cok_field_u8_at(L, r, COK_FIELD_CREATURE_FLAGS, 0);
    uint8_t flags1 = cok_field_u8_at(L, r, COK_FIELD_CREATURE_FLAGS, 1);
    int8_t v = 0;
    switch (spell) {
    case 0x69:
    case 0x01: {
        uint8_t n;
        if (!HOLDS(1)) {
            if (!cok_combat_enemies(game, t, 1, &n)) return false;
            if (n == 0) v = 1;
        }
        break;
    }
    case 0x2a: {
        cok_character *selected = member_of(game, game->vm.character);
        if (selected == NULL)
            return undefined(game, "spell 0x2a's worth (dkk 5340:3fd5) reads the selected "
                                   "character, and none is selected");
        v = cok_character_find_effect(selected, 0x31) == NULL;
        break;
    }
    case 0x03: case 0x3a: case 0x47: case 0x35:
        v = cok_field_u8(L, r, COK_FIELD_HIT_POINTS) < cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS);
        break;
    case 0x04: v = race < 7 || (flags0 & 0x10) != 0; break;
    case 0x6a: case 0x0a: v = (flags0 & 2) != 0 || cok_field_u8(L, r, COK_FIELD_SIZE) == 1; break;
    case 0x41: case 0x0f: v = !HOLDS(0x11); break;
    case 0x15: v = highest < 5 && !HOLDS(0x60) && !HOLDS(0x35); break;
    case 0x17: case 0x31: v = race < 7 && !HOLDS(0x61) && !HOLDS(0x34); break;
    case 0x25: v = HOLDS(0x21); break;
    case 0x68: case 0x2b: v = HOLDS(0x24); break;
    case 0x2f: case 0x40: case 0x4a: case 0x6c: v = HOLDS(0x4c) ? -10 : 1; break;
    case 0x33: v = !HOLDS(0x41); break;
    case 0x4c: case 0x38: v = (flags1 & 8) == 0; break;
    case 0x3d: case 0x5e: v = !HOLDS(0x61); break;
    case 0x44: v = !HOLDS(99); break;
    case 0x6f: case 0x51: v = (flags0 & 2) != 0; break;
    case 0x52: v = !HOLDS(0x50); break;
    case 0x54: v = !HOLDS(0x5c); break;
    case 0x5b: v = highest < 6; break;
    default: v = effect == 0 || !HOLDS(effect); break;
    }
    if (v > 0) {
        if ((int8_t)level < 5 && HOLDS(0x3b)) v = 0;
        if ((int8_t)level < 4 && HOLDS(0x3f)) v = 0;
        if (cok_field_u8(L, r, COK_FIELD_MAGIC_RESISTANCE) > 0x32) v = 0;
    }
#undef HOLDS
    *good = v;
    return true;
}

bool cok_ai_worth(cok_adventure *game, uint8_t side, uint8_t spell, cok_character *t,
                  int8_t *good)
{
    return worth(game, side, spell, t, good);
}

/* dkk 5340:43ea: whether spell would do any good: on c itself where the
 * spell table's column 6 is 0, else on the first of those within its
 * range (5dc5:0e48) around c (6e48:08d6) it does good to, of c's own side
 * for a spell whose column 14 is 0, else of the other. */
static bool of_use(cok_adventure *game, cok_character *c, uint8_t spell, bool *useful)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    *useful = false;
    uint8_t hostile, kind;
    if (!spell_byte(game, spell, 14, &hostile) || !spell_byte(game, spell, 6, &kind)) return false;
    uint8_t side = hostile == 0 ? cok_field_u8(L, c->record, COK_FIELD_HOSTILE)
                                : cok_combat_opposite(c, cok_game_rules(game));
    int8_t good = 0;
    if (kind == 0) {
        if (!worth(game, side, spell, c, &good)) return false;
    } else {
        uint8_t range;
        if (!spell_range(game, spell, &range)) return false;
        const uint8_t *r = c->record;
        if (!cok_combat_list(combat, cok_game_rules(game), cok_combat_x(combat, r),
                             cok_combat_y(combat, r), range, 0xff, cok_combat_size(combat, r)))
            return undefined(game, "the list of combatants around one (6e48:08d6) reads past "
                                   "its tables");
        unsigned n = combat->listed_count;
        for (unsigned k = 1; k <= n && good < 1; ++k) {
            cok_character *t = combatant(game, combat->listed[k].index, "dkk 5340:43ea");
            if (t == NULL || !worth(game, side, spell, t, &good)) return false;
        }
    }
    *useful = good > 0;
    return true;
}

/* dkk 3f63:056c with 03ee and 04f2: the spells memorized in slots 1-0x49
 * that the computer may cast (the spell table's column 11), in order of
 * priority (column 13, highest first, a later of equals before an
 * earlier); walked from the first, each step past the entries equal to
 * the one before, until one does some good (5340:43ea): that one, or 0. */
static bool best_spell(cok_adventure *game, cok_character *c, uint8_t *best)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    uint8_t list[0x4a] = {0};
    *best = 0;
    for (unsigned i = 1; i <= 0x49 && i < cok_field_count(L, COK_FIELD_MEMORIZED); ++i) {
        uint8_t spell = cok_field_u8_at(L, r, COK_FIELD_MEMORIZED, i), may, mine;
        if (spell == 0 || spell >= 0x80) continue;
        if (!spell_byte(game, spell, 11, &may)) return false;
        if (may == 0) continue;
        if (!spell_byte(game, spell, 13, &mine)) return false;
        /* Inserted before the first of lower priority, the rest moved up
         * to the list's end. */
        for (unsigned k = 0; k < 0x4a; ++k) {
            uint8_t theirs;
            if (list[k] == 0) {
                list[k] = spell;
                break;
            }
            if (!spell_byte(game, list[k], 13, &theirs)) return false;
            if (mine < theirs) continue;
            for (unsigned j = k; j < 0x4a && spell != 0; ++j) {
                uint8_t moved = list[j];
                list[j] = spell;
                spell = moved;
            }
            break;
        }
    }
    unsigned at = 0;
    uint8_t candidate = 0;
    bool useful = false;
    do {
        /* 04f2: past the entries equal to the candidate, to the next. */
        while (at < 0x4a && list[at] != 0 && list[at] == candidate) ++at;
        candidate = at < 0x4a ? list[at] : 0;
        if (candidate != 0 && !of_use(game, c, candidate, &useful)) return false;
    } while (!useful && candidate != 0);
    *best = candidate;
    return true;
}

/* dkk 3f63:05d7: with magic allowed (var COK_VAR_NO_MAGIC 0), a spells'
 * block, the turn's casting allowed and an NPC or Magic On, the best spell
 * is cast unless a d100 is at most four times the sum of c's mage,
 * paladin, ranger and knight levels (as a byte) or a d5 is above its
 * priority. */
static bool death_knights_cast(cok_adventure *game, cok_character *c, bool *cast)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    if (cok_ecl_var(&game->vm, COK_VAR_NO_MAGIC) != 0 || !cok_field_has_spells(L, r) ||
        cr->may_cast == 0 || (cok_field_u8(L, r, COK_FIELD_NPC) <= 0x7e && !game->combat.magic_on))
        return true;
    uint8_t spell, priority;
    if (!best_spell(game, c, &spell)) return false;
    if (spell == 0) return true;
    uint8_t levels = (uint8_t)(cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 2) +
                               cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 7) +
                               cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 3) +
                               cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 4));
    if ((uint8_t)(levels << 2) >= roll(game, 1, 100)) return true;
    if (!spell_byte(game, spell, 13, &priority)) return false;
    if (roll(game, 1, 5) > priority) return true;
    return cok_combat_cast(game, cast, true, spell); /* 5340:258e */
}

bool cok_ai_cast(cok_adventure *game, cok_character *c, bool *cast)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    const uint8_t *r = c->record;
    *cast = false;
    if (cok_game_rules(game)->fight.computer_cast == COK_COMPUTER_CAST_DEATH_KNIGHTS)
        return death_knights_cast(game, c, cast);
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    /* The memorized spells but the first, count - 1 of the layout's (57 in
     * Champions, 73 in Death Knights): a field's count never exceeds the
     * record's bytes, so the record's most holds any game's. */
    uint8_t spells[COK_CHARACTER_MAX];
    unsigned count = 0;
    if (cr->may_cast != 0)
        for (unsigned i = 1; i < cok_field_count(L, COK_FIELD_MEMORIZED); ++i)
            if (cok_field_u8_at(L, r, COK_FIELD_MEMORIZED, i) != 0)
                spells[count++] = cok_field_u8_at(L, r, COK_FIELD_MEMORIZED, i);
    uint8_t passes = roll(game, 1, 7), level = 7, chosen = 0;
    if (count > 0 && (cok_field_u8(L, r, COK_FIELD_NPC) > 0x7f || combat->magic_on) &&
        combat->sides[cok_combat_opposite(c, cok_game_rules(game))] > 0) {
        for (unsigned pass = 1; pass <= passes && chosen == 0; ++pass, --level)
            for (unsigned draw = 1; draw < 4 && chosen == 0; ++draw) {
                uint8_t spell = spells[roll(game, 1, (uint8_t)count) - 1];
                bool fits;
                if (!cok_ai_spell_fits(game, c, spell, level, &fits)) return false;
                if (fits) chosen = spell;
            }
    }
    if (chosen == 0) return true;
    return cok_combat_cast(game, cast, true, chosen); /* 432f:28bd */
}

/* Turning undead. */

bool cok_ai_turn_undead(cok_adventure *game, cok_character *c, uint8_t limit, bool *turned)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    *turned = false;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    if (cr->turns >= game->undead) return true;
    /* dkk 3f63:0251: a paladin above level 2 too, up to undead of kind
     * 0x0d. */
    const cok_rules_fight *F = &cok_game_rules(game)->fight;
    if (!(cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 0) > 0 ||
          (F->paladin_turns != 0 &&
           cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 3) > (int8_t)F->paladin_turns) ||
          cok_character_former_level(cok_game_rules(game), r, 0) >
              cok_character_change_level(cok_game_rules(game), r)))
        return true;
    if (F->turn_limit != 0) limit = F->turn_limit;
    cok_character *undead;
    bool found;
    if (!cok_combat_undead(game, c, &undead, &limit, &found)) return false;
    if (!found) return true;
    *turned = true;
    return cok_combat_turn_undead(game, c);
}

/* Weapons. */

bool cok_ai_weapon_score(cok_adventure *game, cok_character *c, size_t item, uint8_t *score)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record, *type = NULL;
    if (item == 0 || item > c->item_count)
        return undefined(game, "a weapon's worth is read through an item not the record's");
    const uint8_t *it = c->items[item - 1];
    if (!item_type(game, it, &type)) return false;
    uint8_t s = (uint8_t)(type[9] * type[10]);
    if ((int8_t)it[0x32] > 0) s = (uint8_t)(s + (int8_t)it[0x32] * 8);
    if ((int8_t)type[11] > 0) s = (uint8_t)(s + (int8_t)type[11] * 2);
    if (it[0x2e] == 0x36) {
        cok_combat_record *cr = record_of(game, c);
        if (cr == NULL) return false;
        if (cr->target != NULL && cok_field_s8(L, cr->target, COK_FIELD_UNDEAD) > 0) s = 8;
    }
    if ((type[14] & 8) != 0) s = (uint8_t)(s + (type[5] - 1) * 2);
    if (type[1] <= 1) s = (uint8_t)(s + 3);
    if (cok_field_u8(L, r, COK_FIELD_HANDS) + type[1] > 3) s = 0;
    if (it[0x3e] == 0x84 && (it[0x3d] & 0x0f) != cok_field_u8(L, r, COK_FIELD_ALIGNMENT)) s = 0;
    if (it[0x3d] == cok_game_rules(game)->spells.weapon_unscored) s = 0;
    if (it[0x36] != 0) s = 0;
    *score = s;
    return true;
}

/* 546c:1ea7 on item (1 + its index) of c, which must be the selected
 * character, whose record the original's Ready works on. */
static bool ready(cok_adventure *game, cok_character *c, size_t item)
{
    if (item == 0 || item > c->item_count)
        return undefined(game, "Ready is given no item, read through NULL (546c:1ea7)");
    if (game->vm.character != c->record)
        return undefined(game, "the computer's weapon is readied for another, the selected "
                               "character (546c:1ea7)");
    return cok_item_ready(game, c, item - 1);
}

/* The hands of item (1 + its index) of c, taken off those c's items hold
 * (+0x17b), a byte. */
static bool free_hands(cok_adventure *game, cok_character *c, size_t item)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *type = NULL;
    if (!item_type(game, c->items[item - 1], &type)) return false;
    cok_field_set8(L, c->record, COK_FIELD_HANDS,
                   (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_HANDS) - type[1]));
    return true;
}

/* 1608: the best missile weapon (byte 14 & 8 or & 0x10) and the best for
 * melee (& 8 clear: a thrown weapon is both) of those c may use (byte 13 &
 * +0x11a) in slot 0, by cok_ai_weapon_score, with their hands and the
 * shield's taken off +0x17b for the while; the missile above 1, the melee
 * above c's bare hands (+0x10d times +0x10f, plus twice +0x111 if
 * positive); the best shield (slot 1) by its bonus + 1 (0 below 0). The
 * missile weapon is taken if it is worth more than half the other, has
 * its ammunition (as 6346:3111 finds it, or flags 0x0a) and either is a
 * missile weapon of range above 1 thrown (flags 0x14) or no enemy is a
 * square away (6346:26e2). Unless the weapon readied is cursed or is the
 * one chosen, it is unreadied, the stats recomputed, the shield's hands
 * taken off again unless it is cursed, and the chosen one, if any,
 * readied. Then, its stats and attacks recomputed, with more than two
 * hands held, the shield, or with none or a cursed one the chosen weapon,
 * is toggled; with fewer than two, unless the shield is the best or is
 * cursed, it is unreadied and the best readied. */
bool cok_ai_choose_weapon(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *r = c->record;
    const uint8_t *type = NULL;
    if (c->slots[0] != 0 && !free_hands(game, c, c->slots[0])) return false;
    if (c->slots[1] != 0 && !free_hands(game, c, c->slots[1])) return false;
    size_t missile = 0, melee = 0, shield = 0;
    uint8_t missile_best = 1, shield_best = 0;
    uint8_t melee_best = (uint8_t)(cok_field_u8_at(L, r, COK_FIELD_BASE_DICE, 1) *
                                   cok_field_u8_at(L, r, COK_FIELD_BASE_DICE_SIDES, 1));
    if (cok_field_s8_at(L, r, COK_FIELD_BASE_DAMAGE_BONUS, 1) > 0)
        melee_best = (uint8_t)(melee_best +
                               cok_field_s8_at(L, r, COK_FIELD_BASE_DAMAGE_BONUS, 1) * 2);
    for (size_t i = 0; i < c->item_count; ++i) {
        const uint8_t *it = c->items[i];
        if (!item_type(game, it, &type)) return false;
        if (type[0] == 0 && (type[13] & cok_field_u8(L, r, COK_FIELD_USABLE_CLASSES)) != 0) {
            uint8_t score;
            if (!cok_ai_weapon_score(game, c, i + 1, &score)) return false;
            if (((type[14] & 8) != 0 || (type[14] & 0x10) != 0) && score > missile_best) {
                missile = i + 1;
                missile_best = score;
            }
            if ((type[14] & 8) == 0 && score > melee_best) {
                melee = i + 1;
                melee_best = score;
            }
        }
        if (type[0] == 1 && (type[13] & cok_field_u8(L, r, COK_FIELD_USABLE_CLASSES)) != 0) {
            uint8_t s = (int8_t)it[0x32] >= 0 ? (uint8_t)(it[0x32] + 1) : 0;
            if (s > shield_best) {
                shield = i + 1;
                shield_best = s;
            }
        }
    }
    bool two = false, has = false;
    uint8_t flags = 0;
    if (missile != 0) {
        if (!item_type(game, c->items[missile - 1], &type)) return false;
        two = type[12] > 1 && (type[14] & 0x14) == 0x14;
        flags = type[14];
        size_t ammunition = 0;
        if ((flags & 0x10) != 0) ammunition = missile;
        if ((flags & 8) != 0) {
            if ((flags & 1) != 0) ammunition = c->slots[11];
            if ((flags & 0x80) != 0) ammunition = c->slots[12];
        }
        has = ammunition != 0 || flags == 0x0a;
    }
    size_t chosen = melee;
    if (missile != 0 && missile_best > melee_best >> 1 && has) {
        uint8_t n = 0;
        if (!two && !enemies(game, c, 1, &n)) return false;
        if (two || n == 0) chosen = missile;
    }
    bool changed = false;
    size_t weapon = c->slots[0];
    if (weapon == 0 || (weapon != chosen && c->items[weapon - 1][0x36] == 0)) {
        if (weapon != 0 && !ready(game, c, weapon)) return false;
        if (!stats(game, c)) return false;
        size_t held = c->slots[1];
        if (held != 0 && c->items[held - 1][0x36] == 0 && !free_hands(game, c, held)) return false;
        if (chosen != 0 && !ready(game, c, chosen)) return false;
        changed = true;
    }
    if (!stats(game, c) || !cok_combat_weapon_attacks(game, c)) return false;
    size_t held = c->slots[1];
    bool swap = held == 0 || (held != shield && c->items[held - 1][0x36] == 0);
    if (cok_field_u8(L, r, COK_FIELD_HANDS) > 2) {
        if (held == 0 || c->items[held - 1][0x36] != 0) {
            if (!ready(game, c, chosen)) return false;
        } else if (!ready(game, c, held)) {
            return false;
        }
        changed = true;
    } else if (cok_field_u8(L, r, COK_FIELD_HANDS) < 2 && swap) {
        if (held != 0 && !ready(game, c, held)) return false;
        if (!stats(game, c)) return false;
        if (shield != 0 && !ready(game, c, shield)) return false;
        changed = true;
    }
    if (!stats(game, c)) return false;
    return !changed || cok_arena_panel(game, c);
}

/* Moving and attacking. */

bool cok_ai_guard(cok_adventure *game, cok_character *c)
{
    cok_arena_clear_text(game); /* 6346:196a */
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    bool missile = false;
    if (!cok_combat_helpless(c, cok_game_rules(game))) {
        if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return type_past(game);
        if (!missile && cr->initiative != 0) {
            cok_combat_guard(game, c); /* 6346:29b3 */
            return true;
        }
    }
    cok_combat_end_turn(c);
    return true;
}

/* The turn from try of c's way of moving (DS:0396 + 5 * way + try: how far
 * round from the direction wanted, mod 8, try 1-5 of each way of moving,
 * ways 1-6, steps: way 1 0, -1, -2, +1, +2; 2 0, +1, +2, -1, -2; 3 -1, +1,
 * 0, -2, +2; 4 +1, -1, 0, +2, -2; 5 0, -1, -2, -3, 4; 6 0, +1, +2, +3, 4.
 * The bytes before way 1's are another table's; a sixth try reads the next
 * way's first). */
static bool turned(cok_adventure *game, const cok_combat_record *cr, uint8_t try_, uint8_t *turn)
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    long at = cok_data_offset2(D, COK_DATA_WAYS, cr->pattern, try_);
    if (!cok_data_holds(D, COK_DATA_WAYS, at))
        return undefined(game, "way of moving %u reads past its table (DS:0396)", cr->pattern);
    *turn = cok_data_held(D, at);
    return true;
}

bool cok_ai_can_step(cok_adventure *game, cok_character *c, uint8_t try_, uint8_t dir,
                     bool *offmap, bool *ok)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    *offmap = false;
    *ok = false;
    cok_combat_record *cr = record_of(game, c);
    uint8_t turn = 0;
    if (cr == NULL || !turned(game, cr, try_, &turn)) return false;
    uint8_t d = (uint8_t)((turn + dir) % 8);
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(&game->combat, cok_game_rules(game), r, d, &occupant, &terrain, &cloud,
                          &puddle))
        return undefined(game, "a size past the footprints is read after them (6beb:0c9d)");
    if (terrain == 0) {
        *offmap = true;
        return true;
    }
    if (terrain >= cok_data_count(cok_game_rules(game)->data, COK_DATA_TERRAIN, 0))
        return undefined(game, "terrain 0x%02x is past the table (DS:1ee4, 3afb:07d2)", terrain);
    uint8_t cost = cok_combat_terrain(cok_game_rules(game), terrain).cost;
    if (cost == 0xff) return true;
    cost = (uint8_t)(cost * ((d & 1) != 0 ? 3 : 2));
    if (occupant != 0 || cost >= cr->movement) return true;
    const cok_rules *rules = cok_game_rules(game);
    if (cloud && cok_character_find_named(c, rules, COK_EFFECT_MAD) == NULL &&
        cok_character_find_named(c, rules, COK_EFFECT_NAUSEA) == NULL &&
        cok_character_find_named(c, rules, COK_EFFECT_POISON_IMMUNE) == NULL &&
        cok_character_find_named(c, rules, COK_EFFECT_MINOR_GLOBE) == NULL &&
        cr->forced == 0) {
        bool made;
        if (!cok_effects_save(&game->effects, c, 0, 0, &made))
            return cok_adventure_effect_failed(game);
        if (!made) cost = (uint8_t)(cr->movement + 1);
    }
    if (puddle && cok_field_s8(L, r, COK_FIELD_HIGHEST_LEVEL) < 7 &&
        cok_character_find_named(c, rules, COK_EFFECT_POISON_IMMUNE) == NULL &&
        cr->forced == 0)
        cost = (uint8_t)(cr->movement + 1);
    *ok = cr->movement >= cost;
    return true;
}

/* "Move/Attack, Move Left = N " on row 24 in light green (1521:0353). */
static void moves_left(cok_adventure *game, uint8_t movement)
{
    char text[48];
    snprintf(text, sizeof text, "Move/Attack, Move Left = %u ", movement >> 1);
    cok_text_string(&game->screen, &game->font, text, 0, 24, 10, 0);
}

bool cok_ai_step(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    cok_rolls *rolls = &game->effects.rolls;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    moves_left(game, cr->movement);
    bool back;
    if (!cok_ai_keys(game, c, &back)) return false;
    if (back) return true;
    if (cr->movement >> 1 == 0 || cr->initiative <= 0) return cok_ai_guard(game, c);
    if (cok_field_u8(L, r, COK_FIELD_NPC) >= 0x80) {
        unsigned d = roll(game, 1, 100);
        if (combat->enemy_health > d + rolls->morale && cok_field_u8(L, r, COK_FIELD_HOSTILE) != 1)
            return cok_ai_guard(game, c);
    }
    if (cr->fleeing == 0 && c->slots[2] == 0 && cok_field_u8(L, r, COK_FIELD_CLASS) == 5)
        return cok_ai_guard(game, c);
    uint8_t dir;
    if (cr->fleeing == 0) {
        cok_character *target = cr->target != NULL ? member_of(game, cr->target) : NULL;
        if (target == NULL)
            return undefined(game, "a step toward no target reads where none stands (6346:34e9)");
        if (!direction(game, c, target, &dir)) return false;
    } else {
        cr->pattern = roll(game, 1, 2);
        /* The party's facing turned back off the map: 7, 2, 3 or 6 for
         * the party's 0, 2, 4 or 6, and the other way for the party. */
        uint8_t f = game->vm.direction;
        dir = (uint8_t)(f - ((f + 2) % 4) / 2 + cok_game_rules(game)->fight.flee_turn);
        if (cok_field_u8(L, r, COK_FIELD_HOSTILE) == 0) dir = (uint8_t)(dir + 4);
        dir = (uint8_t)(dir % 8);
    }
    bool offmap = false, fled = false, ok = false;
    uint8_t try_ = 1;
    while (try_ < 6 && !fled) {
        if (!cok_ai_can_step(game, c, try_, dir, &offmap, &ok)) return false;
        if (ok) break;
        if (cr->fleeing != 0 && offmap) {
            if (!cok_combat_flee(game, c)) return false; /* 432f:0dc7 */
            fled = true;
        } else {
            ++try_;
        }
    }
    if (fled) {
        cr->movement = 0;
        cr->fleeing = 0;
        cok_combat_end_turn(c);
        return true;
    }
    uint8_t turn = 0, step = 0;
    /* dkk 3f63:0ad7 reads the way only for a try below 6 and a way below
     * 7; the step is not used past the last try. */
    if (!(cok_game_rules(game)->fight.ways_guarded && (try_ >= 6 || cr->pattern >= 7))) {
        if (!turned(game, cr, try_, &turn)) return false;
        step = (uint8_t)((dir + turn) % 8);
    } else if (try_ < 6) {
        return undefined(game, "way of moving %u leaves the step a byte of the stack "
                               "(dkk 3f63:0ad7)", cr->pattern);
    }
    bool stop = false;
    /* No way, or the way back: stuck. */
    if (try_ == 6 || (step + 4) % 8 == combat->last_step) {
        ++combat->stuck;
        cr->pattern = (uint8_t)(cr->pattern % 6 + 1);
        if (combat->stuck > 1) {
            cr->target = NULL;
            if (combat->stuck > 2) {
                cr->movement = 0;
                stop = true;
            } else {
                bool found;
                if (!cok_combat_pick_target(game, c, 0xff, true, false, &found)) return false;
                if (!found) {
                    if (!cok_ai_guard(game, c)) return false;
                    stop = true;
                }
            }
        }
    }
    if (try_ < 6)
        combat->last_step = step;
    else
        stop = true;
    if (stop) return true;
    bool shown = combat->in_reach;
    if (!shown && !cok_combat_visible(combat, cok_game_rules(game), r, false, &shown))
        return undefined(game, "a size past the footprints is read after them (6beb:06ef)");
    combat->show_actions = shown || cok_field_u8(L, r, COK_FIELD_HOSTILE) == 0;
    if (!cok_arena_pose(game, c, step, 0, false) || !cok_combat_opportunity(game, c, cr->facing))
        return false;
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) {
        cok_combat_end_turn(c);
        return true;
    }
    if (cr->movement > 0 && !cok_combat_step(game, c, cr->facing)) return false;
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0 || cok_combat_helpless(c, cok_game_rules(game)))
        cok_combat_end_turn(c);
    return cok_cloud_square(game, c, true);
}

/* 0d49: the last step (DS:43c8) none, not stuck (43c9, and 43ca), event
 * 0x0e; on the party's side one bandages the first dying member
 * (6346:31e9), which ends its turn. Then, at most 20 times (the 21st
 * guards, and goes on in that pass), while its turn goes on: one fleeing
 * steps while it has movement and initiative (but 0x14); with its reach
 * the weapon's range less 1 (1 for 0 or 0xff, or none), its target is kept
 * only if it can act and is on the side against the party (a monster's is
 * always dropped), can be attacked and is seen within reach with sight
 * blocked (6b30:03f1, map +6 cleared); else a d(enemies within reach)
 * picks one, or with none 432f:3f9f's pick is stepped toward or it guards;
 * with a missile weapon not thrown and an enemy a square away it readies
 * another (cok_ai_choose_weapon) and leaves the pass; the one picked is
 * attacked if a square away or it can be attacked. The attack: the icon
 * turned (6beb:096b), a sweep ends the turn, else it is booked and
 * attacked (432f:1a45) with the ammunition, none for a thrown weapon a
 * square away, DS:71ab then whether it has any; a target that drops ends
 * the call, its turn going on. */
bool cok_ai_fight(cok_adventure *game, cok_character *c, bool *over)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    combat->last_step = 8;
    combat->stuck = 0;
    combat->stuck_more = 0;
    unsigned count = 0;
    bool going = true, done = false;
    if (!event(game, c, 0x0e)) return false;
    if (cok_field_u8(L, r, COK_FIELD_HOSTILE) == 0 && cok_combat_bandage(game, true))
        cr->initiative = 0;
    if (cr->initiative == 0) going = false;
    while (!done && going) {
        while (cr->fleeing != 0 && cr->movement > 0 && cr->initiative > 0 && cr->initiative < 0x14)
            if (!cok_ai_step(game, c)) return false;
        if (cr->initiative == 0 || cr->initiative == 0x14) {
            going = false;
            continue;
        }
        if (++count > 20) {
            done = true;
            if (!cok_ai_guard(game, c)) return false;
            going = false;
        }
        combat->in_reach = false;
        uint8_t reach = 1;
        size_t weapon = c->slots[0];
        if (weapon != 0) {
            const uint8_t *type = NULL;
            if (!item_type(game, c->items[weapon - 1], &type)) return false;
            reach = (uint8_t)(type[12] - cok_game_rules(game)->fight.reach_less);
        }
        if (reach == 0 || reach == 0xff) reach = 1;
        /* The kept target; one that is none reads 0000:0189, harmlessly. */
        cok_character *target = NULL;
        const uint8_t *t = cr->target;
        if (t != NULL && cok_field_u8(L, t, COK_FIELD_ACTIVE) != 0 &&
            cok_field_u8(L, t, COK_FIELD_HOSTILE) != 0) {
            target = member_of(game, t);
            if (target == NULL) return undefined(game, "the target is not in the list (3afb:0ee8)");
            bool can;
            if (!cok_combat_can_attack(game, c, t, &can)) return false;
            if (can) {
                int8_t x1 = cok_combat_x(combat, t), y1 = cok_combat_y(combat, t);
                uint16_t range = reach;
                bool seen;
                combat->see_all = 0;
                if (!cok_combat_sight(combat, cok_game_rules(game), cok_combat_x(combat, r),
                                      cok_combat_y(combat, r), &x1,
                                      &y1, &range, &seen))
                    return undefined(game, "a cell's terrain is past the table (6b30:03f1)");
                if (seen && range >> 1 <= reach) combat->in_reach = true;
            }
        }
        if (!combat->in_reach) {
            uint8_t n;
            if (!enemies(game, c, reach, &n)) return false;
            if (n == 0) {
                bool found;
                if (!cok_combat_pick_target(game, c, 0xff, false, false, &found)) return false;
                if (found) {
                    if (!cok_ai_step(game, c)) return false;
                } else {
                    if (!cok_ai_guard(game, c)) return false;
                    done = true;
                }
            } else {
                uint8_t pick = roll(game, 1, n);
                target = combatant(game, combat->enemies[pick], "3afb:0fef");
                if (target == NULL) return false;
                bool missile, thrown = false;
                uint8_t near = 0;
                if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return type_past(game);
                if (missile && !cok_combat_thrown(&game->item_types, c, &thrown))
                    return type_past(game);
                if (missile && !thrown && !enemies(game, c, 1, &near)) return false;
                if (missile && !thrown && near > 0) {
                    if (!cok_ai_choose_weapon(game, c)) return false;
                    done = true;
                } else {
                    uint8_t d;
                    bool can = false;
                    if (!distance(game, c, target, &d)) return false;
                    if (d != 1 && !cok_combat_can_attack(game, c, target->record, &can))
                        return false;
                    if (d == 1 || can) combat->in_reach = true;
                }
            }
        }
        if (!combat->in_reach) continue;
        uint8_t d;
        if (!direction(game, c, target, &d) ||
            !cok_arena_centre(game, cok_combat_x(combat, r), cok_combat_y(combat, r), 2, d))
            return false;
        bool swept;
        if (!cok_combat_sweep(game, c, target, &swept)) return false;
        if (swept) {
            cok_combat_end_turn(c);
            done = true;
            continue;
        }
        if (!cok_combat_book(game, c, target)) return false;
        size_t ammunition = 0;
        bool missile;
        if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return type_past(game);
        if (missile) {
            bool has, thrown;
            if (!cok_combat_ammunition(&game->item_types, c, &ammunition, &has) ||
                !cok_combat_thrown(&game->item_types, c, &thrown))
                return type_past(game);
            combat->in_reach = has;
            if (thrown) {
                if (!distance(game, c, target, &d)) return false;
                if (d == 1) ammunition = 0;
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

/* The turn. */

/* 3afb:004b. The byte that 3afb:024b reads as the highest kind of undead
 * to turn ([bp-6] of its frame, which it never sets) is the last thing the
 * calls before it at the same depth wrote there: the sides of the last of
 * the way of moving's dice (60f4:1216 pushes them for Random at that
 * place), 2 or 4; 'l' (0x6c) after "is forced to flee" (3afb:1316 copies
 * the string over it), or 0x42 after "flees in panic", the low byte of the
 * return address 3afb:0142 that the call of 6346:1883 pushes there. An
 * interrupt between could leave another. */
bool cok_combat_computer(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    const cok_rules_fight *F = &cok_game_rules(game)->fight;
    bool over, done;
    /* dkk 3f63:0000: one with no target picks one first (5340:3cac). */
    if (F->computer_target_first && cr->target == NULL &&
        !cok_combat_pick_target(game, c, 0xff, true, false, &done))
        return false;
    if (!cok_ai_keys(game, c, &over)) return false;
    clear_row(game);
    cok_arena_clear_text(game);
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) {
        cok_combat_end_turn(c);
        over = true;
    }
    uint8_t stale;
    if (!pattern(game, c, &stale)) return false;
    if (!over && !morale(game, c, &over, &stale)) return false;
    if (cr->fleeing != 0 && cr->forced == 0) {
        if (!cok_arena_say(game, c, F->words.panic, 10, true)) return false;
        stale = 0x42;
    }
    if (over) return true;
    if (!cok_ai_use_item(game, c, &done)) return false;
    if (done) {
        cok_combat_end_turn(c);
        return true;
    }
    if (cr->spell > 0) {
        /* Unlike the player's turn (3995:0573), the spell is not cleared
         * first: one hurt while casting it, by its own fireball, say,
         * "lost a spell" and forgets it again. dkk 3f63:0000 picks a target
         * afresh first where it has none. */
        if (F->computer_target_first && cr->target == NULL &&
            !cok_combat_pick_target(game, c, 0xff, true, true, &done))
            return false;
        cok_cast_spell(game, cr->spell, true, true, 0, &done); /* 5b04:1415 */
        if (game->vm.status != COK_ECL_OK) return false;
        cok_combat_end_turn(c);
        return true;
    }
    if (!cok_ai_turn_undead(game, c, stale, &done)) return false;
    if (done) {
        cok_combat_end_turn(c);
        return true;
    }
    if (!cok_ai_cast(game, c, &done)) return false;
    if (done) return true;
    if (!cok_ai_choose_weapon(game, c) || !cok_ai_keys(game, c, &over)) return false;
    /* Each pass but the last fells a target or readies another weapon,
     * once: past that the original loops for ever, as with a cursed
     * missile weapon and an enemy a square away. */
    for (unsigned passes = 0; !over; ++passes) {
        if (passes > 2 * COK_COMBATANTS + 8)
            return undefined(game, "the computer's turn never ends (3afb:01ef), where the "
                                   "original loops for ever");
        bool found;
        if (!cok_combat_pick_target(game, c, 0xff, true, false, &found)) return false;
        if (found && cr->initiative >= 1 && cok_field_u8(L, r, COK_FIELD_ACTIVE) != 0) {
            if (!cok_ai_fight(game, c, &over)) return false;
        } else {
            if (!cok_ai_guard(game, c)) return false;
            over = true;
        }
        if (game->vm.abort) break;
    }
    return true;
}
