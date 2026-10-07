#include "cast.h"

#include "camp.h"
#include "magic.h"

#include <stdio.h>
#include <string.h>

/* A cast in progress: the caster (DS:6096) and the target list the target
 * routine fills (DS:6fea, the count, and the far pointers from DS:6feb),
 * whose entries handlers may clear. */
typedef struct {
    cok_adventure *game;
    cok_character *caster;
    uint8_t spell;
    uint8_t frame; /* The low byte of 5b04:1415's BP. */
    cok_character *targets[COK_PARTY_RECORDS]; /* DS:6feb holds 72. */
    size_t count;
} cast;

/* Byte column of the spell table (DS:31b3, 16 bytes an id): 2 range, 4 and
 * 5 the duration, 7 how it is targeted outside combat, 8 and 9 the saving
 * throw, 10 the effect it adds. */
static uint8_t spell_byte(uint8_t spell, unsigned column)
{
    uint8_t byte = 0;
    cok_ds_byte((uint16_t)(0x31b3u + 16u * spell + column), &byte);
    return byte;
}

static cok_character *member_of(cok_adventure *game, const uint8_t *record)
{
    size_t i = cok_party_index(&game->party, record);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* A spell effect could not be carried out: say why and end the run. */
static bool effect_failed(cok_adventure *game)
{
    cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", game->effects.error);
    return false;
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* The dice (60f4:1216). */
static uint8_t roll(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice(&game->vm.seed, count, sides);
}

static bool level_of(cok_adventure *game, uint8_t spell, uint8_t *level)
{
    return cok_effects_caster_level(&game->effects, spell, level) || effect_failed(game);
}

bool cok_cast_duration(cok_adventure *game, uint8_t spell, uint16_t *minutes)
{
    switch (spell) {
    case 0x1a: *minutes = 0xec4; return true;
    case 0x28: *minutes = (uint16_t)(roll(game, 1, 6) * 10); return true;
    case 0x39: case 0x3d: *minutes = roll(game, 5, 4); return true;
    case 0x3b: *minutes = (uint16_t)(roll(game, 1, 4) * 10 + 40); return true;
    case 0x3f:
        if (game->vm.mode == 5)
            *minutes = (uint16_t)(roll(game, 2, 10) * 10);
        else
            *minutes = (uint16_t)((roll(game, 1, 10) + 10) * 10);
        return true;
    case 0x43: *minutes = 0x5a0; return true;
    default: break;
    }
    uint8_t level;
    if (!level_of(game, spell, &level)) return false;
    *minutes = (uint16_t)(level * spell_byte(spell, 5) + spell_byte(spell, 4));
    return true;
}

/* Say text about c and wait (6346:1883, and 6346:228c outside combat). */
static void say(cok_adventure *game, cok_character *c, const char *text)
{
    cok_camp_say(game, c->record, text, true);
}

/* The rolls and effects of effect.h, ending the run when they fail. */

static bool save(cok_adventure *game, cok_character *c, uint8_t type, bool *made)
{
    return cok_effects_save(&game->effects, c, type, 0, made) || effect_failed(game);
}

static bool dispatch(cok_adventure *game, cok_character *c, uint8_t event)
{
    return cok_effects_dispatch(&game->effects, c, event) || effect_failed(game);
}

static bool remove_effect(cok_adventure *game, cok_character *c, cok_effect *effect, uint8_t id)
{
    return cok_effects_remove(&game->effects, c, effect, id) || effect_failed(game);
}

static bool ability(cok_adventure *game, cok_character *c, unsigned stat)
{
    return cok_effects_ability(&game->effects, c, stat) || effect_failed(game);
}

static bool add_raw(cok_adventure *game, cok_character *c, uint8_t id, uint16_t minutes,
                    uint8_t value, bool on_remove)
{
    if (cok_character_add_effect(c, id, minutes, value, on_remove) != NULL) return true;
    return undefined(game, "out of memory adding a spell's effect");
}

/* 60f4:14b7: if c has effect id, say it "is Cured" and remove it. */
static bool cure(cok_adventure *game, cok_character *c, uint8_t id, bool *cured)
{
    cok_effect *effect = cok_character_find_effect(c, id);
    *cured = effect != NULL;
    if (effect == NULL) return true;
    say(game, c, "is Cured");
    return remove_effect(game, c, effect, id);
}

/* 6346:25f9: say how far c was healed, and redraw the party list. */
static void healed(cok_adventure *game, cok_character *c)
{
    const uint8_t *r = c->record;
    say(game, c, r[0x197] == r[0x62] ? "is fully healed" : "is partially healed");
    if (game->vm.mode != 5) cok_adventure_party(game);
}

/* 60f4:20f7: add effect id to c unless its effects or magic resistance,
 * on event 9, cancel the effect pending (DS:6b2f), or c saved against one
 * with a save of kind 1; then it "is Unaffected". An effect of the id with
 * time left is removed first; permanent ones stay, and the new one is
 * added after them. Then text, if any, is said. */
static bool add_effect(cok_adventure *game, cok_character *c, uint8_t id, uint16_t minutes,
                       uint8_t value, bool on_remove, uint8_t save_kind, bool saved,
                       const char *text)
{
    cok_rolls *r = &game->effects.rolls;
    r->pending = id;
    if (!dispatch(game, c, 9)) return false;
    if (r->pending == 0 || (saved && save_kind == 1)) {
        say(game, c, "is Unaffected");
        return true;
    }
    cok_effect *old = cok_character_find_effect(c, id);
    if (old != NULL && old->duration > 0 && !remove_effect(game, c, old, id)) return false;
    if (!add_raw(game, c, id, minutes, value, on_remove)) return false;
    if (text[0] != '\0') {
        say(game, c, text);
        cok_camp_clear_text(game);
    }
    return true;
}

/* 60f4:1db7 outside combat: amount of damage to c, of the type in DS:6b31,
 * after event 6 (magic resistance first) and a save of kind 1 (none) or 2
 * (half), or event 0x14 when not saved. If any is left and c can act, it
 * "takes N points of damage" from the type, or "from Magic" when the
 * type's only bit is 8 or there is none; then if it drops it "Goes Down",
 * ", and is Dying", or "is killed". The party list is not redrawn. */
static bool damage(cok_adventure *game, cok_character *c, uint8_t amount, uint8_t save_kind,
                   bool saved)
{
    cok_rolls *r = &game->effects.rolls;
    uint8_t *record = c->record;
    r->amount = amount;
    if (!dispatch(game, c, 6)) return false;
    if (saved) {
        if (save_kind == 1) r->amount = 0;
        else if (save_kind == 2) r->amount = (uint8_t)(r->amount >> 1);
    } else if (!dispatch(game, c, 0x14)) {
        return false;
    }
    if (r->amount == 0 || record[0x189] == 0) return true;
    char text[256];
    if (r->amount == 1)
        snprintf(text, sizeof text, "takes 1 point of damage ");
    else
        snprintf(text, sizeof text, "takes %u points of damage ", r->amount);
    static const struct { uint8_t type; const char *text; } from[] = {
        {1, "from Fire"}, {2, "from Cold"}, {4, "from Electricity"}, {0x10, "from Acid"},
    };
    for (size_t i = 0; i < sizeof from / sizeof *from; ++i)
        if ((r->damage_type & 0xf7) == from[i].type) strcat(text, from[i].text);
    if ((r->damage_type & 8) == r->damage_type) strcat(text, "from Magic");
    say(game, c, text);
    cok_character_damage(record, r->amount);
    if (record[0x189] == 0) {
        snprintf(text, sizeof text, "Goes Down%s", record[0x188] == 5 ? ", and is Dying" : "");
        if (record[0x188] >= 6 && record[0x188] <= 8) snprintf(text, sizeof text, "is killed");
        say(game, c, text);
    }
    cok_camp_clear_text(game);
    return true;
}

/* 5b04:1071: for each target left in the list, a saving throw if the spell
 * has one (byte 8, of type byte 9), damage of type if any, and the spell's
 * effect (byte 10) for its duration, with value, or the caster level if
 * value is 0, saying text. */
static bool apply(cast *cx, uint8_t value, bool on_remove, uint8_t amount, uint8_t type,
                  const char *text)
{
    cok_adventure *game = cx->game;
    cok_rolls *r = &game->effects.rolls;
    r->damage_type = amount != 0 ? type : 0;
    if (cx->count == 0) return true;
    if (value == 0 && !level_of(game, cx->spell, &value)) return false;
    uint8_t save_kind = spell_byte(cx->spell, 8), effect = spell_byte(cx->spell, 10);
    for (size_t i = 0; i < cx->count && !game->vm.abort; ++i) {
        cok_character *c = cx->targets[i];
        if (c == NULL) continue;
        bool saved = false;
        if (save_kind != 0 && !save(game, c, spell_byte(cx->spell, 9), &saved)) return false;
        if (spell_byte(r->spell, 2) == 0xff)
            return undefined(game, "a spell cast by touch rolls to hit (60f4:1062), which is not "
                                   "ported");
        if (amount != 0 && !damage(game, c, amount, save_kind, saved)) return false;
        if (effect == 0) continue;
        uint16_t minutes;
        if (!cok_cast_duration(game, cx->spell, &minutes) ||
            !add_effect(game, c, effect, minutes, value, on_remove, save_kind, saved, text))
            return false;
    }
    r->damage_type = 0;
    return true;
}

/* 60f4:1592: whether strength and exceptional strength beat c's base ones
 * (+0x10, +0x1d), and the effect value that gives them (60f4:151a): the
 * exceptional strength + 1 at 18, else the strength + 100. */
static bool stronger(const uint8_t *c, uint8_t strength, uint8_t exceptional, uint8_t *value)
{
    if (!(strength > c[0x10] || (strength == 18 && exceptional > c[0x1d]))) return false;
    *value = (uint8_t)(strength == 18 ? exceptional + 1 : strength + 100);
    return true;
}

/* The handlers, by their address in overlay 5b04. */

typedef bool handler_fn(cast *cx);

/* 1e34: keep the targets on side, then apply with text. */
static bool sided(cast *cx, uint8_t side, const char *text)
{
    cx->game->effects.rolls.images = 1;
    for (size_t i = 0; i < cx->count; ++i)
        if (cx->targets[i] != NULL && cx->targets[i]->record[0x18a] != side) cx->targets[i] = NULL;
    return apply(cx, 0, false, 0, 0, text);
}

/* 1efb, Bless: the party on the caster's side. */
static bool bless(cast *cx)
{
    return sided(cx, cx->caster->record[0x18a], "is Blessed");
}

/* 1f57, 3ddc: heal the target count d8 + bonus, and a d8 more for a
 * caster of deity 4, rolled first. */
static bool heal(cast *cx, uint8_t count, uint8_t bonus)
{
    cok_adventure *game = cx->game;
    if (cx->count == 0) return true;
    uint8_t extra = cx->caster->record[0x5d] == 4 ? roll(game, 1, 8) : 0;
    uint8_t amount = (uint8_t)(roll(game, count, 8) + bonus + extra);
    cok_character *c = cx->targets[0];
    if (cok_character_heal(c->record, amount, false, game->vm.mode)) healed(game, c);
    return true;
}

static bool cure_light(cast *cx)
{
    return heal(cx, 1, 0);
}

static bool cure_serious(cast *cx)
{
    return heal(cx, 2, 1);
}

static bool affected(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is affected");
}

static bool protected(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is protected");
}

static bool cold_resistant(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is cold-resistant");
}

/* 20ab, Burning Hands: damage of the caster level, fire and magic. */
static bool burning_hands(cast *cx)
{
    uint8_t level;
    if (!level_of(cx->game, cx->game->effects.rolls.spell, &level)) return false;
    return apply(cx, 0, false, level, 9, "");
}

/* 21d7, Enlarge: a strength by the caster level (DS:4839, 483a), if better
 * than the target's base one, as effect 0x0c. */
static bool enlarge(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    uint8_t level, strength = 18, exceptional = 0, value;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    static const uint8_t by_level[7] = {0, 0, 1, 0x33, 0x4c, 0x5b, 0x64};
    if (level >= 1 && level <= 6) exceptional = by_level[level];
    else if (level >= 7 && level <= 9) strength = (uint8_t)(12 + level);
    else if (level == 10 || level == 11) strength = 0x16;
    if (!stronger(c->record, strength, exceptional, &value)) {
        say(game, c, "is unaffected");
        return true;
    }
    say(game, c, "is stronger");
    uint16_t minutes;
    return cok_cast_duration(game, game->effects.rolls.spell, &minutes) &&
           add_raw(game, c, 0x0c, minutes, value, true) && ability(game, c, 0);
}

/* 230c, Reduce: unless the target saves (type 4), its first 0x0c goes. */
static bool reduce(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    if (c == NULL || cx->count == 0) return true;
    bool made;
    if (!save(game, c, 4, &made)) return false;
    if (made || cok_character_find_effect(c, 0x0c) == NULL) return true;
    if (!remove_effect(game, c, NULL, 0x0c) || !ability(game, c, 0)) return false;
    say(game, c, "has been reduced");
    return true;
}

/* 23ac, Friends: charisma 2d4 more, rolled first, then recomputed. */
static bool friends(cast *cx)
{
    uint8_t value = roll(cx->game, 2, 4);
    return apply(cx, value, true, 0, 0, "is friendly") && ability(cx->game, cx->caster, 5);
}

static bool shielded(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is shielded");
}

static bool fire_resistant(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is fire resistant");
}

/* 26eb, Slow Poison: a poisoned (0x37) target with status 1 is dropped
 * from the list; any other gets at least 1 hit point, status 0 and can act,
 * dead or not, then effects 0x16 for 3780 minutes and 0x0f for 10. */
static bool slow_poison(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    uint8_t *r = c->record;
    if (r[0x188] == 1) {
        cx->targets[0] = NULL;
        return true;
    }
    if (cok_character_find_effect(c, 0x37) == NULL) return true;
    if (r[0x197] == 0) r[0x197] = 1;
    r[0x188] = 0;
    r[0x189] = 1;
    return apply(cx, 0xff, true, 0, 0, "is affected") && add_raw(game, c, 0x0f, 10, 0xff, true);
}

/* 2877, Spiritual Hammer: effect 0x17, then its handler, which gives the
 * caster a hammer. */
static bool hammer(cast *cx)
{
    cok_adventure *game = cx->game;
    if (!apply(cx, 0, true, 0, 0, "")) return false;
    return cok_effects_run(&game->effects, cx->targets[0], 0x17, NULL) || effect_failed(game);
}

static bool invisible(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is invisible");
}

/* 293b, Mirror Image: 1d4 images in the value's high nibble. */
static bool mirror_image(cast *cx)
{
    uint8_t level, images = roll(cx->game, 1, 4);
    if (!level_of(cx->game, cx->spell, &level)) return false;
    return apply(cx, (uint8_t)((uint8_t)(images << 4) | level), false, 0, 0, "is duplicated");
}

/* 2e4e, Strength: the target's current strength plus 1d4 for a mage, 1d6
 * for a cleric or thief, 1d8 for a fighter (each that applies is rolled,
 * the last kept; former classes count above +0xd7), past 18 into
 * exceptional strength for a fighter, knight or ranger, as effect 0x26 if
 * better than the base. With none of those classes the roll is the local
 * [bp-6] it never set, [BP-0x36] of 5b04:1415: from memory, the BP that
 * 6346:161b saved there (called at 5b04:16fc), whose low byte the caller's
 * depth fixes; while an item is used (DS:711d), 161b is not called and
 * the byte is 60f4:1408's effect pointer's segment, NULL once it has
 * found no more 0x19, so 0. */
static bool strength(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    const uint8_t *r = c->record;
#define HAS(now, before) ((int8_t)r[now] > 0 || (int8_t)r[before] > (int8_t)r[0xd7])
    int bonus = -1;
    if (HAS(0xfe, 0x106)) bonus = roll(game, 1, 4);
    if (HAS(0xf9, 0x101) || HAS(0xff, 0x107)) bonus = roll(game, 1, 6);
    if (HAS(0xfb, 0x103)) bonus = roll(game, 1, 8);
    if (bonus < 0) bonus = game->effects.rolls.item != 0 ? 0 : cx->frame;
    uint8_t value = (uint8_t)(r[0x11] + bonus), exceptional = 0;
    if (value > 18) {
        if (HAS(0xfb, 0x103) || HAS(0x100, 0x108) || HAS(0xfd, 0x105)) {
            exceptional = (uint8_t)((value - 18) * 10 + r[0x1c]);
            if (exceptional > 100) exceptional = 100;
        }
        value = 18;
    }
#undef HAS
    uint8_t ignored;
    if (!stronger(r, value, exceptional, &ignored)) return true;
    uint16_t minutes;
    return cok_cast_duration(game, game->effects.rolls.spell, &minutes) &&
           add_raw(game, c, 0x26, minutes, (uint8_t)(bonus + 100), true) && ability(game, c, 0);
}

/* 3000 (Animate Dead), 3d8c (Restoration), 42a7 (Cure Critical Wounds),
 * 42c0 (Raise Dead), 42e3 (Invisibility to Animals): nothing, though the
 * spell is used up. */
static bool nothing(cast *cx)
{
    (void)cx;
    return true;
}

/* 300d, Cure Blindness: 0x21 goes and the target "can see". */
static bool cure_blindness(cast *cx)
{
    bool cured;
    if (!cure(cx->game, cx->targets[0], 0x21, &cured)) return false;
    if (cured) say(cx->game, cx->targets[0], "can see");
    return true;
}

/* 3107 with 3080, Cure Disease: 0x22 goes; if 0x2b does, so do 0x2c and
 * 0x1f, and strength is recomputed; recurring effects are not added again
 * meanwhile (DS:6b38). */
static bool cure_disease(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    game->effects.rolls.curing = 1;
    bool cured, ok = cure(game, c, 0x22, &cured) && cure(game, c, 0x2b, &cured);
    if (ok && cured)
        ok = remove_effect(game, c, NULL, 0x2c) && remove_effect(game, c, NULL, 0x1f) &&
             ability(game, c, 0);
    game->effects.rolls.curing = 0;
    return ok;
}

/* 320f, Dispel Magic: each effect of the target with a value other than
 * 0xff, taken as the level that cast it in its low nibble, goes on a d100
 * up to 50, plus 5 a level the caster is above it or less 2 a level it is
 * below, as a byte (DS:4839, 483a). Each pass of its loop over the targets
 * works on the first. Its second part, the clouds on the combat map, runs
 * outside combat too, reading the map pointer (DS:6a2e) that combat has
 * freed; the port leaves it out. */
static bool dispel_magic(cast *cx)
{
    cok_adventure *game = cx->game;
    game->effects.rolls.images = 1;
    uint8_t level;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    for (size_t i = 0; i < cx->count; ++i) {
        cok_character *c = cx->targets[0];
        bool dispelled = false;
        for (cok_effect *e = c->effects; e != NULL;) {
            cok_effect *next = e->next;
            if (e->value != 0xff) {
                uint8_t of = e->value & 0x0f, chance = 50;
                if (level > of) chance = (uint8_t)(50 + 5 * (level - of));
                else if (level < of) chance = (uint8_t)(50 - 2 * (of - level));
                if (roll(game, 1, 100) <= chance) {
                    if (!remove_effect(game, c, e, e->id)) return false;
                    dispelled = true;
                    bool listed = next == NULL;
                    for (cok_effect *f = c->effects; f != NULL && !listed; f = f->next)
                        listed = f == next;
                    if (!listed)
                        return undefined(game, "dispelling an effect removed the next; the "
                                               "original reads it after freeing it");
                }
            }
            e = next;
        }
        if (dispelled) say(game, c, "is affected");
    }
    return true;
}

/* 358a, Prayer: the caster's side in the value's high nibble. */
static bool prayer(cast *cx)
{
    uint8_t level;
    if (!level_of(cx->game, cx->spell, &level)) return false;
    uint8_t value = (uint8_t)(level + (int8_t)cx->caster->record[0x18a] * 16);
    return apply(cx, value, false, 0, 0, "is praying");
}

/* 35f5, Remove Curse: Bestow Curse's 0x24 goes and the target "is
 * un-cursed"; or else its first cursed item (+0x36) is unreadied, though
 * still cursed and counted in the stats until they are next recomputed; an
 * item with a power loses the effect it gave (3f44:3888, the first of id
 * +0x3d) and the abilities are recomputed. */
static bool remove_curse(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    bool cured;
    if (!cure(game, c, 0x24, &cured)) return false;
    if (cured) {
        say(game, c, "is un-cursed");
        return true;
    }
    for (size_t i = 0; i < c->item_count; ++i) {
        uint8_t *item = c->items[i];
        if (item[0x36] == 0) continue;
        item[0x34] = 0;
        if (item[0x3e] > 0x7f) {
            if (!remove_effect(game, c, NULL, item[0x3d])) return false;
            for (unsigned stat = 0; stat <= 5; ++stat)
                if (!ability(game, c, stat)) return false;
        }
        say(game, c, "has an item un-cursed");
        return true;
    }
    return true;
}

bool cok_cast_remove_curse(cok_adventure *game, cok_character *target)
{
    cast cx = {.game = game, .caster = target, .spell = 0x2b, .targets = {target}, .count = 1};
    return remove_curse(&cx);
}

/* 3865: cure the first targets on side of id, up to the caster level of
 * them, dropping those cured and all others; apply; then event 0x12 on
 * those left. */
static bool hasten(cast *cx, uint8_t id, uint8_t side, const char *text)
{
    cok_adventure *game = cx->game;
    game->effects.rolls.images = 1;
    uint8_t left;
    if (!level_of(game, game->effects.rolls.spell, &left)) return false;
    for (size_t i = 0; i < cx->count; ++i) {
        cok_character *c = cx->targets[i];
        if (c != NULL && c->record[0x18a] == side && left > 0) {
            --left;
            bool cured;
            if (!cure(game, c, id, &cured)) return false;
            if (cured) cx->targets[i] = NULL;
        } else {
            cx->targets[i] = NULL;
        }
    }
    if (!apply(cx, 0, false, 0, 0, text)) return false;
    for (size_t i = 0; i < cx->count && !game->vm.abort; ++i)
        if (cx->targets[i] != NULL && !dispatch(game, cx->targets[i], 0x12)) return false;
    return true;
}

static bool haste(cast *cx)
{
    return hasten(cx, 0x2a, cx->caster->record[0x18a], "is Hasted");
}

/* 3d9b, 0x39: cure Slow, or else haste. */
static bool speedy(cast *cx)
{
    bool cured;
    if (!cure(cx->game, cx->targets[0], 0x2a, &cured)) return false;
    return cured || apply(cx, 0, false, 0, 0, "is Speedy");
}

/* 3e53, 0x3b: effect 0x71 for strength 21, saying "is stronger" if that
 * beats the base; if not, the effect's value is the local [bp-1] it never
 * set, [BP-0x31] of 5b04:1415: while an item is used (DS:711d), the high
 * byte of the return offset 0x16eb of the 60f4:1408 call at 5b04:16e6,
 * 0x16; from memory, the high byte of the return segment of the 6346:161b
 * call, wherever DOS loaded the overlay, which the port does not know. */
static bool giant_strength(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    uint8_t value;
    if (stronger(c->record, 0x15, 0, &value)) {
        say(game, c, "is stronger");
    } else if (game->effects.rolls.item != 0) {
        value = 0x16;
    } else {
        return undefined(game, "0x3b from memory on a base strength of 21 or more gives a value "
                               "of the overlay's segment (5b04:3eb8)");
    }
    uint16_t minutes;
    return cok_cast_duration(game, 0x3b, &minutes) && add_raw(game, c, 0x71, minutes, value, true) &&
           ability(game, c, 0);
}

/* 3f63, 4a40: heal 2d4 + 2, saying so without redrawing the party list. */
static bool potion(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    if (cok_character_heal(c->record, (uint8_t)(roll(game, 2, 4) + 2), false, game->vm.mode))
        say(game, c, "is Healed");
    return true;
}

/* 4098, Neutralize Poison: a target with status 1 is dropped; one that is
 * poisoned (0x37) gets at least 1 hit point, loses 0x37, 0x16 and 0x0f
 * (while curing, DS:6b38), is "unpoisoned" and okay, whatever its status. */
static bool neutralize_poison(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->targets[0];
    uint8_t *r = c->record;
    if (r[0x188] == 1) {
        cx->targets[0] = NULL;
        return true;
    }
    if (cok_character_find_effect(c, 0x37) == NULL) {
        say(game, c, "is unaffected");
        return true;
    }
    if (r[0x197] == 0) r[0x197] = 1;
    game->effects.rolls.curing = 1;
    bool ok = remove_effect(game, c, NULL, 0x37) && remove_effect(game, c, NULL, 0x16) &&
              remove_effect(game, c, NULL, 0x0f);
    game->effects.rolls.curing = 0;
    if (!ok) return false;
    say(game, c, "is unpoisoned");
    r[0x189] = 1;
    r[0x188] = 0;
    return true;
}

/* 464d, Fire Shield: "flame type: " Hot or Cold, chosen at random for a
 * character the computer controls; any other key asks "Abort spell? ",
 * and Yes ends it. The menus' special keys count by their scan codes. Hot
 * adds 0x32 and says so; Cold adds 0x36 silently; each also 0x70. */
static bool fire_shield(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->caster;
    for (;;) {
        int key;
        bool special;
        if (c->record[0x18b] != 0) {
            key = roll(game, 1, 10) > 5 ? 'H' : 'C';
        } else {
            key = cok_camp_menu(game, "flame type: ", "Hot Cold", false, false, &special);
            if (key < 0) return false;
        }
        if (key == 'H' || key == 'C') {
            uint16_t minutes;
            if (!cok_cast_duration(game, 0x55, &minutes) ||
                !add_effect(game, c, key == 'H' ? 0x32 : 0x36, minutes, 0, false, 0, false,
                            key == 'H' ? "is protected" : "") ||
                !cok_cast_duration(game, 0x55, &minutes) ||
                !add_effect(game, c, 0x70, minutes, 0, false, 0, false, ""))
                return false;
            return true;
        }
        key = cok_camp_menu(game, "Abort spell? ", "Yes No", false, false, &special);
        if (key < 0) return false;
        if (key == 'Y') return true;
    }
}

static bool empty_text(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "");
}

/* DS:6e3a + 4 * spell, as 5b04:58ed fills it. */
static const struct {
    uint16_t address;
    handler_fn *fn;
} handlers[COK_SPELLS] = {
    [0x01] = {0x1efb, bless},          [0x02] = {0x1f2c, NULL},
    [0x03] = {0x1f57, cure_light},     [0x04] = {0x1fc2, NULL},
    [0x05] = {0x2004, affected},       [0x06] = {0x203e, protected},
    [0x07] = {0x203e, protected},      [0x08] = {0x207d, cold_resistant},
    [0x09] = {0x20ab, burning_hands},  [0x0a] = {0x20f8, NULL},
    [0x0b] = {0x2004, affected},       [0x0c] = {0x21d7, enlarge},
    [0x0d] = {0x230c, reduce},         [0x0e] = {0x23ac, friends},
    [0x0f] = {0x23f3, NULL},           [0x10] = {0x203e, protected},
    [0x11] = {0x203e, protected},      [0x12] = {0x2004, affected},
    [0x13] = {0x2456, shielded},       [0x14] = {0x2484, NULL},
    [0x15] = {0x24d8, NULL},           [0x16] = {0x2004, affected},
    [0x17] = {0x2616, NULL},           [0x18] = {0x2679, fire_resistant},
    [0x19] = {0x26b2, NULL},           [0x1a] = {0x26eb, slow_poison},
    [0x1b] = {0x279a, NULL},           [0x1c] = {0x2877, hammer},
    [0x1d] = {0x2004, affected},       [0x1e] = {0x28c7, invisible},
    [0x1f] = {0x2900, NULL},           [0x20] = {0x293b, mirror_image},
    [0x21] = {0x2999, NULL},           [0x22] = {0x29de, NULL},
    [0x23] = {0x2e4e, strength},       [0x24] = {0x3000, nothing},
    [0x25] = {0x300d, cure_blindness}, [0x26] = {0x3053, NULL},
    [0x27] = {0x3107, cure_disease},   [0x28] = {0x3124, NULL},
    [0x29] = {0x320f, dispel_magic},   [0x2a] = {0x358a, prayer},
    [0x2b] = {0x35f5, remove_curse},   [0x2c] = {0x3705, NULL},
    [0x2d] = {0x373e, NULL},           [0x2e] = {0x320f, dispel_magic},
    [0x2f] = {0x376c, NULL},           [0x30] = {0x3991, haste},
    [0x31] = {0x2616, NULL},           [0x32] = {0x28c7, invisible},
    [0x33] = {0x3d04, NULL},           [0x34] = {0x203e, protected},
    [0x35] = {0x203e, protected},      [0x36] = {0x203e, protected},
    [0x37] = {0x3d5e, NULL},           [0x38] = {0x3d8c, nothing},
    [0x39] = {0x3d9b, speedy},         [0x3a] = {0x3ddc, cure_serious},
    [0x3b] = {0x3e53, giant_strength}, [0x3c] = {0x3ed8, NULL},
    [0x3d] = {0x3f2c, NULL},           [0x3e] = {0x3f63, potion},
    [0x3f] = {0x3fbd, invisible},      [0x40] = {0x376c, NULL},
    [0x41] = {0x3feb, NULL},           [0x42] = {0x4026, NULL},
    [0x43] = {0x4098, neutralize_poison}, [0x44] = {0x418e, NULL},
    [0x45] = {0x203e, protected},      [0x46] = {0x4214, NULL},
    [0x47] = {0x42a7, nothing},        [0x48] = {0x42b1, NULL},
    [0x49] = {0x42b6, NULL},           [0x4a] = {0x42bb, NULL},
    [0x4b] = {0x42c0, nothing},        [0x4c] = {0x42ca, NULL},
    [0x4d] = {0x2004, affected},       [0x4e] = {0x42d4, NULL},
    [0x4f] = {0x42de, NULL},           [0x50] = {0x42e3, nothing},
    [0x51] = {0x42f3, NULL},           [0x52] = {0x4382, NULL},
    [0x53] = {0x4446, NULL},           [0x54] = {0x44ed, NULL},
    [0x55] = {0x464d, fire_shield},    [0x56] = {0x4801, NULL},
    [0x57] = {0x4914, NULL},           [0x58] = {0x4957, protected},
    [0x59] = {0x35f5, remove_curse},   [0x5a] = {0x3000, nothing},
    [0x5b] = {0x4984, NULL},           [0x5c] = {0x498e, NULL},
    [0x5d] = {0x4998, NULL},           [0x5e] = {0x2616, NULL},
    [0x5f] = {0x49a3, empty_text},     [0x60] = {0x49d1, empty_text},
    [0x61] = {0x49ff, empty_text},     [0x62] = {0x4a2c, NULL},
    [0x63] = {0x4a40, potion},         [0x64] = {0x3705, NULL},
    [0x65] = {0x203e, protected},      [0x66] = {0x26b2, NULL},
    [0x67] = {0x2004, affected},       [0x68] = {0x35f5, remove_curse},
    [0x69] = {0x1efb, bless},          [0x6a] = {0x20f8, NULL},
    [0x6b] = {0x20ab, burning_hands},
};

/* 5b04:127e: the targets outside combat, by byte 7: 1 the caster; 2 one
 * picked ("Cast Spell on whom"), starting from the last target (DS:710b,
 * the caster if none); 4 the whole party. Returns whether there are any;
 * false with game->vm.abort set when input ended. */
static bool target(cast *cx)
{
    cok_adventure *game = cx->game;
    if (game->combat_targets) {
        /* DS:6e3a holds 432f:2337 while a battle runs. */
        cok_adventure_fail(game, COK_ECL_EFFECT_FAILED,
                           "spells cast in combat pick their targets with 432f:2337, which is "
                           "not ported");
        return false;
    }
    if (game->spell_target == NULL) game->spell_target = cx->caster->record;
    cx->targets[0] = cx->caster;
    cx->count = 1;
    switch (spell_byte(cx->spell, 7)) {
    case 1: return true;
    case 2: {
        cok_adventure_redraw(game);
        bool ended;
        uint8_t *picked = cok_adventure_pick(game, "Cast Spell on whom", game->spell_target, true,
                                             &ended);
        if (ended) return false;
        game->spell_target = picked;
        cx->targets[0] = member_of(game, picked);
        if (cx->targets[0] == NULL) {
            cx->count = 0;
            return false;
        }
        return true;
    }
    case 4:
        /* The whole list from DS:609a, monsters loaded too. */
        for (size_t i = 0; i < game->party.count; ++i) cx->targets[i] = game->party.members[i];
        cx->count = game->party.count;
        return true;
    default: return false;
    }
}

/* 6346:161b: forget the first memorized byte equal to spell; a spell
 * marked to be learned does not match. */
static void forget(uint8_t *c, uint8_t spell)
{
    for (size_t i = 0; i <= 0x39; ++i)
        if (c[0x1e + i] == spell) {
            c[0x1e + i] = 0;
            return;
        }
}

void cok_cast_spell(cok_adventure *game, uint8_t spell, bool announce, uint8_t frame, bool *done)
{
    cok_rolls *r = &game->effects.rolls;
    cast cx = {.game = game, .caster = member_of(game, game->vm.character), .spell = spell,
               .frame = frame};
    if (cx.caster == NULL) {
        undefined(game, "casting with no character selected reads through NULL (5b04:1415)");
        return;
    }
    if (spell == 0 || spell >= COK_SPELLS) {
        char text[96];
        snprintf(text, sizeof text, "spell %u is past the handler table (DS:6e3a)", spell);
        undefined(game, text);
        return;
    }
    uint8_t *c = cx.caster->record;
    bool go = true;
    if (game->vm.mode != 5 && spell_byte(spell, 7) == 0) {
        bool lose;
        if (r->item == 0) {
            const char *name = cok_spell_name(spell);
            cok_adventure_log(game, "print", name);
            cok_text_string(&game->screen, &game->font, name, 1, 0x13, 10, 0);
            cok_adventure_log(game, "print", "can't be cast here...");
            cok_text_string(&game->screen, &game->font, "can't be cast here...", 1, 0x14, 10, 0);
            int answer = cok_camp_yes_no(game, "Lose it? ", 13);
            if (answer < 0) return;
            lose = answer == 'Y';
            if (lose) forget(c, spell);
        } else {
            cok_adventure_log(game, "print", "That Item");
            cok_text_string(&game->screen, &game->font, "That Item", 1, 0x13, 10, 0);
            cok_adventure_log(game, "print", "is a combat-only item...");
            cok_text_string(&game->screen, &game->font, "is a combat-only item...", 1, 0x14, 10,
                            0);
            int answer = cok_camp_yes_no(game, "Use it? ", 13);
            if (answer < 0) return;
            if (answer == 'Y') *done = true;
        }
        announce = false;
        go = false;
    }
    if (cok_character_find_effect(cx.caster, 0x4a) != NULL && roll(game, 1, 2) == 1) {
        if (!cok_camp_spell_message(game, c, "miscasts", spell)) return;
        announce = false;
        go = false;
    }
    if (announce && r->item == 0 && !cok_camp_spell_message(game, c, "casts", spell)) return;
    if (go && target(&cx)) {
        *done = true;
        /* 60f4:1408: casting ends the caster's invisibility. */
        cok_effect *invisible;
        while ((invisible = cok_character_find_effect(cx.caster, 0x19)) != NULL)
            if (!remove_effect(game, cx.caster, invisible, 0x19)) return;
        if (r->item == 0) forget(c, spell);
        if (handlers[spell].fn == NULL) {
            char text[96];
            snprintf(text, sizeof text, "spell %u (5b04:%04x) is cast only in combat", spell,
                     handlers[spell].address);
            undefined(game, text);
            return;
        }
        r->spell = spell;
        handlers[spell].fn(&cx);
        r->spell = 0;
        r->images = 0;
    }
    if (!game->vm.abort) cok_camp_clear_text(game);
}
