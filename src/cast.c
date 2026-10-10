#include "cast.h"

#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "command.h"
#include "magic.h"
#include "round.h"

#include <stdio.h>
#include <string.h>

/* A cast in progress: the caster (DS:6096), the spell, whether the
 * computer picks its targets (5b04:1415's flag), and the target list the
 * target routine fills (game->spell_targets from 1, DS:6fe7 + 4i, and its
 * count, DS:6fea), whose entries handlers may clear. */
typedef struct {
    cok_adventure *game;
    cok_character *caster;
    uint8_t spell;
    uint8_t frame; /* The low byte of 5b04:1415's BP. */
    bool ai;
    /* The caster was shown when its flight ended (6beb:06ef): the stack
     * then holds what drawing it back left, which the cone reads. */
    bool shown;
    cok_character **targets;
} cast;

/* The data segment's tables the combat targets and the cone read: the
 * steps by direction 0-8 (DS:1ed6, 1edf), the effects that make one
 * helpless (DS:200c), and the cone's neighbours of its end, left and
 * right, by direction (DS:0d94, 0d9c, 0da4, 0dac). */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};
static const uint8_t disabling[4] = {0x33, 0x34, 0x35, 0x1f};
static const int8_t cone_sides[4][8] = {
    {-1, 0, 0, 1, 1, 0, 0, -1}, /* left x */
    {0, -1, -1, 0, 0, 1, 1, 0}, /* left y */
    {1, 1, 0, 0, -1, -1, 0, 0}, /* right x */
    {0, 0, 1, 1, 0, 0, -1, -1}, /* right y */
};

const cok_ds_table cok_cast_tables[] = {
    {0x1ed6, sizeof step_x, 1, (const uint8_t *)step_x},
    {0x1edf, sizeof step_y, 1, (const uint8_t *)step_y},
    {0x200c, sizeof disabling, 1, disabling},
    {0x0d94, sizeof cone_sides, 1, (const uint8_t *)cone_sides},
};
const size_t cok_cast_table_count = sizeof cok_cast_tables / sizeof *cok_cast_tables;

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
    return cok_adventure_effect_failed(game);
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

/* 6346:0d20 on c, ending the run where it fails. */
static bool recompute(cok_adventure *game, cok_character *c)
{
    char error[300];
    if (cok_character_stats(c, &game->item_types, error, sizeof error)) return true;
    return undefined(game, error);
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

/* Say text about c and wait (6346:1883 on row 10): in combat in the side
 * panel, cleared after. */
static void say(cok_adventure *game, cok_character *c, const char *text)
{
    cok_arena_say(game, c, text, 10, true);
}

/* Say text about c with a flash on it (6346:228c, kind 1), not cleared;
 * outside combat as say. */
static bool flash(cok_adventure *game, cok_character *c, const char *text)
{
    return cok_arena_flash(game, c, 1, text);
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
bool cok_cast_add_effect(cok_adventure *game, cok_character *c, uint8_t id, uint16_t minutes,
                         uint8_t value, bool on_remove, uint8_t save_kind, bool saved,
                         const char *text)
{
    cok_rolls *r = &game->effects.rolls;
    r->pending = id;
    /* Event 9 can turn the effect on another (0x47). */
    if (!cok_effects_dispatch_at(&game->effects, &c, 9)) return effect_failed(game);
    if (r->pending == 0 || (saved && save_kind == 1)) {
        say(game, c, "is Unaffected");
        return true;
    }
    cok_effect *old = cok_character_find_effect(c, id);
    if (old != NULL && old->duration > 0 && !remove_effect(game, c, old, id)) return false;
    if (!add_raw(game, c, id, minutes, value, on_remove)) return false;
    if (text[0] == '\0') return true;
    /* 6346:228c, kind 1, then 6346:196a: in combat a flash on c, the view
     * centred on it unless shown whole; outside it said with a pause. */
    if (!cok_arena_flash(game, c, 1, text)) return false;
    cok_arena_clear_text(game);
    return true;
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

void cok_cast_forget(cok_adventure *game, const cok_character *gone)
{
    for (size_t i = 1; i <= COK_PARTY_RECORDS; ++i)
        if (game->spell_targets[i] == gone) game->spell_targets[i] = &cok_arena_freed;
    for (size_t i = 1; i <= 20; ++i)
        if (game->combat.exploding[i] == gone) game->combat.exploding[i] = &cok_arena_freed;
}

cok_arena_reachable cok_cast_reachable(const cok_adventure *game)
{
    const cok_combat *k = &game->combat;
    cok_arena_reachable also = {{k->exploding[1], k->exploding[8], k->exploding[15]}, {NULL}};
    for (unsigned j = 0; j < 10; ++j) also.targets[j] = game->spell_targets[1 + 7 * j];
    return also;
}

bool cok_cast_damage(cok_adventure *game, cok_character *c, uint8_t amount, uint8_t save_kind,
                     bool saved)
{
    cok_rolls *r = &game->effects.rolls;
    bool combat = game->vm.mode == 5;
    r->amount = amount;
    /* Event 6 can turn the damage on another (0x47). */
    if (!cok_effects_dispatch_at(&game->effects, &c, 6)) return effect_failed(game);
    uint8_t *record = c->record;
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
    if (!combat) {
        say(game, c, text);
        cok_character_damage(record, r->amount);
    } else {
        /* 6346:228c kind 0, a burst, then 6346:24d7 in combat; one casting
         * that is hurt "lost a spell" and its turn. */
        if (!cok_arena_flash(game, c, 0, text)) return false;
        if (c->combat == NULL)
            return undefined(game, "a record with no combat record is read through NULL in "
                                   "combat (60f4:1f95)");
        if (!cok_combat_damage(&game->combat, c, r->amount, 5))
            return undefined(game, "a record on a side other than 0 or 1 (+0x18a) is counted past "
                                   "DS:6b2d (6346:24d7)");
        c->combat->may_cast = 0;
        if (c->combat->spell > 0) {
            if (!cok_arena_say(game, c, "lost a spell", 12, true)) return false;
            forget(record, c->combat->spell);
            cok_combat_end_turn(c);
        }
    }
    if (record[0x189] == 0) {
        snprintf(text, sizeof text, "Goes Down%s", record[0x188] == 5 ? ", and is Dying" : "");
        if (record[0x188] >= 6 && record[0x188] <= 8) snprintf(text, sizeof text, "is killed");
        if (!combat) {
            say(game, c, text);
        } else {
            /* From the row after the text, then the battle's effects go
             * (60f4:1440), event 0x0d, and unless exploding the death
             * (6beb:0e08), whose stale byte is 1440's (see arena.h). */
            if (!cok_arena_say(game, c, text, (uint8_t)(game->vm.cursor.y + 1), false) ||
                !cok_combat_battle_only(game, c))
                return false;
            int stale = cok_character_find_effect(c, 0x4d) != NULL ? COK_ARENA_STALE_UNKNOWN : 0;
            if (!dispatch(game, c, 0x0d)) return false;
            if (record[0x189] == 0 && record[0x188] != 10) {
                cok_arena_reachable also = cok_cast_reachable(game);
                if (!cok_arena_kill(game, c, stale, &also)) return false;
            } else {
                cok_adventure_wait(game, game->speed * 100u); /* 1521:0b4b */
            }
        }
    }
    if (combat)
        cok_arena_clear_text(game);
    else
        cok_camp_clear_text(game);
    return true;
}

/* The cell of record (6beb:0bcb, 0bf3), none's as command.h has it. */
static bool cell(cok_adventure *game, const cok_character *c, int8_t *x, int8_t *y)
{
    return cok_combat_cell_of(game, c != NULL ? c->record : NULL, x, y);
}

/* The listed combatants (6b30:08d8 around the spell's cell, any
 * direction, a footprint of 1) as the targets. */
static bool area(cast *cx, uint8_t radius)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    if (!cok_combat_list(combat, game->spell_x, game->spell_y, radius, 0xff, 1))
        return undefined(game, "the list of combatants around one (6b30:08d8) reads past its "
                               "tables");
    uint8_t n = combat->listed_count;
    for (uint8_t i = 1; i <= n; ++i)
        cx->targets[i] = combat->combatant[combat->listed[i].index].character;
    game->spell_target_count = n;
    game->effects.rolls.images = 1;
    return true;
}

/* 5b04:1071: for each target left in the list, a saving throw if the spell
 * has one (byte 8, of type byte 9); for a spell cast by touch (byte 2 of
 * DS:6b33 0xff) the target's stats, its event 0x0b and the caster's
 * attack roll (60f4:1062) against its armour class, a miss counting as
 * saved and leaving no damage, for this target and the next; damage of
 * type if any, and the spell's effect (byte 10) for its duration, with
 * value, or the caster level if value is 0, saying text. Each step reads
 * the target from the list again, where an effect may have turned it. */
static bool apply(cast *cx, uint8_t value, bool on_remove, uint8_t amount, uint8_t type,
                  const char *text)
{
    cok_adventure *game = cx->game;
    cok_rolls *r = &game->effects.rolls;
    cok_character **t = cx->targets;
    r->damage_type = amount != 0 ? type : 0;
    uint8_t n = game->spell_target_count;
    if (n == 0) return true;
    if (value == 0 && !level_of(game, cx->spell, &value)) return false;
    uint8_t save_kind = spell_byte(cx->spell, 8), effect = spell_byte(cx->spell, 10);
    for (uint8_t i = 1; i <= n && !game->vm.abort; ++i) {
        if (t[i] == NULL) continue;
        bool saved = false;
        if (save_kind != 0 && !save(game, t[i], spell_byte(cx->spell, 9), &saved)) return false;
        if (spell_byte(r->spell, 2) == 0xff) {
            bool hit;
            if (!recompute(game, t[i]) || !dispatch(game, t[i], 0x0b) ||
                !cok_combat_hit(game, cx->caster, t[i], t[i]->record[0x18d], &hit))
                return false;
            if (!hit) {
                amount = 0;
                saved = true;
            }
        }
        if (amount != 0 && !cok_cast_damage(game, t[i], amount, save_kind, saved)) return false;
        if (effect == 0) continue;
        uint16_t minutes;
        if (!cok_cast_duration(game, cx->spell, &minutes) ||
            !cok_cast_add_effect(game, t[i], effect, minutes, value, on_remove, save_kind, saved,
                                 text))
            return false;
    }
    r->damage_type = 0;
    return true;
}

/* The first target (DS:6feb), which most handlers read through; for
 * none (in combat, a pick of no creature: Target after Manual's Exit) the
 * original reads the interrupt table at 0000:0000, and the run stops. */
static cok_character *first(cast *cx)
{
    cok_character *t = cx->targets[1];
    if (t == NULL)
        undefined(cx->game, "the first target is read through NULL (DS:6feb)");
    return t;
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
    cok_adventure *game = cx->game;
    cok_character **t = cx->targets;
    game->effects.rolls.images = 1;
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n; ++i) {
        if (t[i] == NULL)
            return undefined(game, "a target taken off the list is read through NULL "
                                   "(5b04:1e72)");
        if (t[i]->record[0x18a] == side) {
            /* In combat Bless leaves out an ally with an enemy beside it. */
            uint8_t spell = game->effects.rolls.spell;
            if (!((spell == 1 || spell == 0x69) && game->vm.mode == 5)) continue;
            uint8_t enemies;
            if (!cok_combat_enemies(game, t[i], 1, &enemies)) return false;
            if (enemies == 0) continue;
        }
        t[i] = NULL;
    }
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
    if (game->spell_target_count == 0) return true;
    uint8_t extra = cx->caster->record[0x5d] == 4 ? roll(game, 1, 8) : 0;
    uint8_t amount = (uint8_t)(roll(game, count, 8) + bonus + extra);
    cok_character *c = first(cx);
    if (c == NULL) return false;
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
    cok_character *c = first(cx);
    if (c == NULL) return false;
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
    cok_character *c = cx->targets[1];
    if (c == NULL || game->spell_target_count == 0) return true;
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
    cok_character *c = first(cx);
    if (c == NULL) return false;
    uint8_t *r = c->record;
    if (r[0x188] == 1) {
        cx->targets[1] = NULL;
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
    cok_character *c = first(cx);
    return c != NULL && (cok_effects_run(&game->effects, c, 0x17, NULL) || effect_failed(game));
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
    cok_character *c = first(cx);
    if (c == NULL) return false;
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
    cok_character *c = first(cx);
    if (c == NULL || !cure(cx->game, c, 0x21, &cured)) return false;
    return !cured || flash(cx->game, c, "can see");
}

/* 3107 with 3080, Cure Disease: 0x22 goes; if 0x2b does, so do 0x2c and
 * 0x1f, and strength is recomputed; recurring effects are not added again
 * meanwhile (DS:6b38). */
static bool cure_disease(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = first(cx);
    if (c == NULL) return false;
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
    for (uint8_t i = 1; i <= game->spell_target_count; ++i) {
        cok_character *c = first(cx);
        if (c == NULL) return false;
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
        if (dispelled && !flash(game, c, "is affected")) return false;
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
    cok_character *c = first(cx);
    if (c == NULL) return false;
    bool cured;
    if (!cure(game, c, 0x24, &cured)) return false;
    if (cured) return flash(game, c, "is un-cursed");
    for (size_t i = 0; i < c->item_count; ++i) {
        uint8_t *item = c->items[i];
        if (item[0x36] == 0) continue;
        item[0x34] = 0;
        if (item[0x3e] > 0x7f) {
            if (!remove_effect(game, c, NULL, item[0x3d])) return false;
            for (unsigned stat = 0; stat <= 5; ++stat)
                if (!ability(game, c, stat)) return false;
        }
        return flash(game, c, "has an item un-cursed");
    }
    return true;
}

bool cok_cast_remove_curse(cok_adventure *game, cok_character *target)
{
    game->spell_targets[1] = target; /* DS:6feb alone; the count stays. */
    cast cx = {.game = game, .caster = target, .spell = 0x2b, .targets = game->spell_targets};
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
    cok_character **t = cx->targets;
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n; ++i) {
        cok_character *c = t[i];
        if (c == NULL)
            return undefined(game, "a target taken off the list is read through NULL "
                                   "(5b04:38b6)");
        if (c->record[0x18a] == side && left > 0) {
            --left;
            bool cured;
            if (!cure(game, c, id, &cured)) return false;
            if (cured) t[i] = NULL;
        } else {
            t[i] = NULL;
        }
    }
    if (!apply(cx, 0, false, 0, 0, text)) return false;
    n = game->spell_target_count;
    for (uint8_t i = 1; i <= n && !game->vm.abort; ++i)
        if (t[i] != NULL && !dispatch(game, t[i], 0x12)) return false;
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
    cok_character *c = first(cx);
    if (c == NULL || !cure(cx->game, c, 0x2a, &cured)) return false;
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
    cok_character *c = first(cx);
    if (c == NULL) return false;
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
    cok_character *c = first(cx);
    if (c == NULL) return false;
    if (cok_character_heal(c->record, (uint8_t)(roll(game, 2, 4) + 2), false, game->vm.mode))
        return flash(game, c, "is Healed");
    return true;
}

/* 4098, Neutralize Poison: a target with status 1 is dropped; one that is
 * poisoned (0x37) gets at least 1 hit point, loses 0x37, 0x16 and 0x0f
 * (while curing, DS:6b38), is "unpoisoned" and okay, whatever its status. */
static bool neutralize_poison(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *c = first(cx);
    if (c == NULL) return false;
    uint8_t *r = c->record;
    if (r[0x188] == 1) {
        cx->targets[1] = NULL;
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
                !cok_cast_add_effect(game, c, key == 'H' ? 0x32 : 0x36, minutes, 0, false, 0,
                                     false, key == 'H' ? "is protected" : "") ||
                !cok_cast_duration(game, 0x55, &minutes) ||
                !cok_cast_add_effect(game, c, 0x70, minutes, 0, false, 0, false, ""))
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

/* The handlers of the spells cast only in combat. */

/* 60f4:1261: the dice of damage, count kept in DS:6b34. */
static uint8_t damage_roll(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice_count(&game->vm.seed, count, sides, &game->effects.rolls.dice);
}

/* Say text about c in the panel (6346:1883, row 10) and wait. */
static bool say_waiting(cok_adventure *game, cok_character *c, const char *text)
{
    return cok_arena_say(game, c, text, 10, true);
}

/* 1cbd: from the last target to the first, a missile to each but the last
 * (sound 2, icon slot 0x12), a saving throw of the spell's type (byte 9)
 * with bonus, an elf (+0x5a 7) always saving against Hold Person, Faerie
 * Fire and Charm Monsters counting the last picked as saved against a
 * save of kind 1; then the spell's effect for its duration, valued at the
 * caster level, with text. */
static bool missiles(cast *cx, uint8_t bonus, const char *text)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    cok_character **t = cx->targets;
    uint8_t spell = game->effects.rolls.spell;
    for (uint8_t i = game->spell_target_count; i >= 1 && !game->vm.abort; --i) {
        cok_character *p = t[i];
        if (p == NULL) continue;
        if (i < game->spell_target_count) {
            int8_t x, y;
            cok_arena_sound(game, 2); /* DS:1e4c */
            if (!cok_arena_missile_frames(game, 0x12) || !cell(game, p, &x, &y) ||
                !cok_arena_missile(game, cok_combat_x(combat, cx->caster->record),
                                   cok_combat_y(combat, cx->caster->record), x, y, 4, 0x1e))
                return false;
        }
        bool saved;
        uint8_t kind;
        if ((spell == 0x4f || spell == 0x51) && i == game->spell_target_count) {
            saved = true;
            kind = 1;
        } else {
            if (!cok_effects_save(&game->effects, p, spell_byte(spell, 9), bonus, &saved))
                return effect_failed(game);
            kind = spell_byte(spell, 8);
        }
        if (p->record[0x5a] == 7 && (spell == 0x17 || spell == 0x31)) saved = true;
        uint16_t minutes;
        uint8_t level;
        if (!cok_cast_duration(game, spell, &minutes) || !level_of(game, spell, &level) ||
            !cok_cast_add_effect(game, p, spell_byte(spell, 10), minutes, level, false, kind,
                                 saved, text))
            return false;
    }
    return true;
}

/* 1f2c, Curse: those on the side against the caster. */
static bool curse(cast *cx)
{
    return sided(cx, cok_combat_opposite(cx->caster), "is Cursed");
}

/* 1fc2, Cause Light Wounds (by touch): 1d8, magic. */
static bool cause_light(cast *cx)
{
    return apply(cx, 0, false, damage_roll(cx->game, 1, 8), 8, "");
}

/* 20f8, Charm Person: unless the first target is a monster of some kind
 * (+0x13f above 1) or larger than a man (+0xcf), effect 0x0b valued at the
 * caster level and the caster's side in bit 7, run when removed, "is
 * charmed"; then 0x0b's handler takes hold (3f44:03cd). */
static bool charm_person(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *t = cx->targets[1];
    if (t == NULL) return true;
    if (t->record[0x13f] > 1 || t->record[0xcf] > 1) return say_waiting(game, t, "is unaffected");
    uint8_t level;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    uint8_t value = (uint8_t)(level + (uint8_t)(cx->caster->record[0x18a] << 7));
    if (!apply(cx, value, true, 0, 0, "is charmed")) return false;
    cok_effect *charm = cok_character_find_effect(t, 0x0b);
    return charm == NULL || cok_effects_run(&game->effects, t, 0x0b, charm) || effect_failed(game);
}

/* 23f3, Magic Missile: (level + 1) / 2 missiles of 1d4 + 1, magic. */
static bool magic_missile(cast *cx)
{
    cok_adventure *game = cx->game;
    uint8_t level;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    uint8_t n = (uint8_t)((uint8_t)(level + 1) >> 1);
    return apply(cx, 0, false, (uint8_t)(damage_roll(game, n, 4) + n), 8, "");
}

/* 2484, Shocking Grasp (by touch): 1d8 + the caster level, electricity and
 * magic. */
static bool shocking_grasp(cast *cx)
{
    cok_adventure *game = cx->game;
    uint8_t level;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    return apply(cx, 0, false, (uint8_t)(damage_roll(game, 1, 8) + level), 0x0c, "");
}

/* 24d8, Sleep: of 4d4 hit dice (DS:4839), each target in turn by its own
 * (+0xd6, signed: up to 1 counts 1, 2 2, 3 4, 4 6, 5 10 for an elf and 20
 * for others, more or less than that 20, DS:483a) while they last; those
 * unconscious (status 1), asleep already (0x35) or past what is left are
 * taken off. The rest fall asleep, no saving throw. */
static bool sleep_spell(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character **t = cx->targets;
    game->effects.rolls.images = 1;
    uint8_t left = roll(game, 4, 4);
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n; ++i) {
        if (t[i] == NULL)
            return undefined(game, "a target taken off the list is read through NULL "
                                   "(5b04:2514)");
        const uint8_t *r = t[i]->record;
        int8_t hd = (int8_t)r[0xd6];
        uint8_t weight = hd >= 0 && hd <= 1 ? 1 : hd == 2 ? 2 : hd == 3 ? 4 : hd == 4 ? 6
                         : hd == 5 && r[0x5a] == 7 ? 10 : 20;
        if (r[0x188] != 1 && cok_character_find_effect(t[i], 0x35) == NULL && left >= weight)
            left = (uint8_t)(left - weight);
        else
            t[i] = NULL;
    }
    return apply(cx, 0, false, 0, 0, "falls asleep");
}

/* 2616, Hold Person and Hold Monsters: a save 2 easier for one target
 * (3 for 0x31 and 0x5e), 1 for two, none for three or four, through 1cbd:
 * "is held". The original's bonus for more, or none, is a local never
 * set: with none it is not read; more the targeting never gives. */
static bool hold(cast *cx)
{
    cok_adventure *game = cx->game;
    uint8_t n = game->spell_target_count, bonus;
    if (n == 1) bonus = game->effects.rolls.spell == 0x17 ? 0xfe : 0xfd;
    else if (n == 2) bonus = 0xff;
    else if (n <= 4) bonus = 0; /* with none never set, and never read */
    else
        return undefined(game, "Hold's saving throw bonus for more than four targets is a local "
                               "never set (5b04:264c)");
    return missiles(cx, bonus, "is held");
}

static bool silence(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is silenced");
}

/* 279a, Snake Charm: its targets are every snake in the list (+0x13f &
 * 0x10), either side, that can act, is not charmed already (0x33) and
 * whose hit points are within what is left of the caster's: "is charmed". */
static bool snake_charm(cast *cx)
{
    cok_adventure *game = cx->game;
    uint8_t left = cx->caster->record[0x197];
    game->spell_target_count = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *m = game->party.members[i];
        const uint8_t *r = m->record;
        if ((r[0x13f] & 0x10) == 0 || r[0x189] == 0 ||
            cok_character_find_effect(m, 0x33) != NULL || left < r[0x197])
            continue;
        left = (uint8_t)(left - r[0x197]);
        cx->targets[++game->spell_target_count] = m;
    }
    return apply(cx, 0, false, 0, 0, "is charmed");
}

/* 2900, Knock: no effect to add, its text unsaid. */
static bool knock(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "Knock-Knock");
}

static bool weakened(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is weakened");
}

static bool blinded(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is blind");
}

static bool diseased(cast *cx)
{
    return apply(cx, 0, true, 0, 0, "is diseased");
}

static bool bestow_curse(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "has been cursed!");
}

static bool blink(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is blinking");
}

/* 376c, Fireball and 0x40: dice of the caster level, or 2 x 1d3 + 1 for
 * 0x40; outside a 3D area (var 0x4be6 0) the targets are those within 2 of
 * the spell's cell, not 3; the screen centred there (6beb:096b); Nd6 fire
 * and magic, the spell's save of kind 2 halving it. */
static bool fireball(cast *cx)
{
    cok_adventure *game = cx->game;
    game->effects.rolls.images = 1;
    uint8_t dice;
    if (game->effects.rolls.spell == 0x40) {
        dice = (uint8_t)(roll(game, 1, 3) * 2 + 1);
    } else if (!level_of(game, game->effects.rolls.spell, &dice)) {
        return false;
    }
    if (game->vm.mem4b00[0xe6] == 0 && !area(cx, 2)) return false;
    return cok_arena_centre(game, game->spell_x, game->spell_y, 0, 8) &&
           apply(cx, 0, false, damage_roll(game, dice, 6), 9, "");
}

/* 39bb, a cell of a bolt: a wall (cost 0xff) in a 3D area (var 0x4be6 1)
 * sets *bounce if it was clear, any other cell clears it; a combatant there
 * other than previous throws (type, no bonus) and takes amount, half if
 * saved, and the bolt's pictures are built again (icon slot 0x13). */
static bool bolt_cell(cast *cx, int8_t x, int8_t y, uint8_t amount, uint8_t type,
                      uint8_t previous, bool *bounce)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    uint8_t occupant, terrain;
    cok_combat_cell(combat, x, y, &occupant, &terrain);
    if (terrain >= COK_COMBAT_TERRAINS)
        return undefined(game, "terrain past the table is read (DS:1ee4, 5b04:39e9)");
    *bounce = terrain > 0 && cok_combat_terrain[terrain].cost == 0xff &&
              game->vm.mem4b00[0xe6] == 1 && !*bounce;
    if (occupant == 0 || occupant == previous) return true;
    cok_character *o = combat->combatant[occupant].character;
    if (o == NULL) return undefined(game, "a cell's combatant has no record (5b04:3a23)");
    bool saved;
    if (!cok_effects_save(&game->effects, o, type, 0, &saved)) return effect_failed(game);
    return cok_cast_damage(game, o, amount, 2, saved) && cok_arena_missile_frames(game, 0x13);
}

/* 3a6f, the rest of a bolt: from the spell's cell on, away from the
 * caster, length cells (2 a step, 3 a diagonal one), in segments that end
 * at another combatant, off the map, at a cell costing more than 1, at the
 * line's end or the length; each flies the bolt (icon slot 0x13) and hits
 * its last cell (bolt_cell), not the combatant the last segment hit. A wall
 * that bounces makes it the spell's cell, turns the bolt back along the
 * line from it to the caster, charging 8 more for the first bounce of
 * Lightning Bolt (surcharge) where that line is 8 or shorter, and lets
 * anyone be hit again. The length of the line before a bounce is still
 * counted against the next line's first segment, as a byte. */
static bool bolt(cast *cx, uint8_t length, uint8_t amount, uint8_t type, bool surcharge)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    bool bounce = false;
    uint8_t previous, terrain, occupant = 0, last = 0;
    if (!cok_arena_missile_frames(game, 0x13)) return false;
    cok_combat_cell(combat, game->spell_x, game->spell_y, &previous, &terrain);
    int8_t sign = 1;
    const uint8_t *self = cx->caster->record;
    int8_t cx0 = cok_combat_x(combat, self), cy0 = cok_combat_y(combat, self);
    if (cx0 == game->spell_x && cy0 == game->spell_y) return true;
    uint8_t remaining = (uint8_t)(length * 2);
    game->effects.rolls.images = 1;
    cok_combat_line line;
    bool stepped = false;
    while (remaining > 0) {
        line = (cok_combat_line){0};
        line.x0 = game->spell_x;
        line.y0 = game->spell_y;
        /* Words: the low words of 16-bit products. */
        uint32_t way = (uint32_t)(uint16_t)sign, rem = remaining;
        uint32_t dx = (uint32_t)(uint16_t)(line.x0 - cx0), dy = (uint32_t)(uint16_t)(line.y0 - cy0);
        line.x1 = (int16_t)(uint16_t)((uint32_t)(uint16_t)line.x0 + dx * way * rem);
        line.y1 = (int16_t)(uint16_t)((uint32_t)(uint16_t)line.y0 + dy * way * rem);
        cok_combat_line_start(&line);
        do {
            int8_t sx = (int8_t)line.x, sy = (int8_t)line.y;
            if (!(line.x0 == line.x1 && line.y0 == line.y1)) {
                do {
                    stepped = cok_combat_line_step(&line);
                    cok_combat_cell(combat, (int8_t)line.x, (int8_t)line.y, &occupant, &terrain);
                    if (terrain >= COK_COMBAT_TERRAINS)
                        return undefined(game, "terrain past the table is read (DS:1ee4, "
                                               "5b04:3bb0)");
                    uint8_t cost = cok_combat_terrain[terrain].cost;
                    if (cost == 1) bounce = false;
                    if (!stepped || (occupant != 0 && occupant != previous) || terrain == 0 ||
                        cost > 1)
                        break;
                } while (line.length < remaining);
            }
            if (terrain == 0) remaining = 0;
            if (!cok_arena_missile(game, sx, sy, line.x, line.y, 4, 0x32) ||
                !bolt_cell(cx, (int8_t)line.x, (int8_t)line.y, amount, type, previous, &bounce))
                return false;
            previous = occupant;
            if (bounce) {
                game->spell_x = (int8_t)line.x;
                game->spell_y = (int8_t)line.y;
                cok_combat_line back = {0};
                back.x0 = game->spell_x;
                back.y0 = game->spell_y;
                back.x1 = cx0;
                back.y1 = cy0;
                cok_combat_line_start(&back);
                while (cok_combat_line_step(&back)) {}
                if (surcharge && back.length <= 8) line.length = (uint8_t)(line.length + 8);
                sign = (int8_t)-sign;
                surcharge = false;
                previous = 0;
            }
            uint8_t progress = (uint8_t)(line.length - last);
            remaining = progress < remaining ? (uint8_t)(remaining - progress) : 0;
            last = line.length;
            if (!bounce && remaining != 0 && !stepped && progress == 0)
                return undefined(game, "a bolt ends its line short of its length on a cell it "
                                       "does not bounce from, and the original passes there for "
                                       "ever (5b04:3ce7)");
        } while (!bounce && remaining != 0);
    }
    game->effects.rolls.images = 0;
    return true;
}

/* 3d04, Lightning Bolt: the caster level in d6 (60f4:1216), electricity and
 * magic, on the spell's cell, then a bolt of 7 with the surcharge. */
static bool lightning_bolt(cast *cx)
{
    cok_adventure *game = cx->game;
    uint8_t level;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    uint8_t amount = roll(game, level, 6);
    game->effects.rolls.damage_type = 0x0c;
    /* The original's flag is a local never set: 39bb reads it on a wall in
     * a 3D area, only to write it back to the same local, never read. */
    bool bounce = false;
    return bolt_cell(cx, game->spell_x, game->spell_y, amount, 4, 0, &bounce) &&
           bolt(cx, 7, amount, 4, true);
}

/* 3ed8, 0x3c: 1d6 + 20 of electricity on the spell's cell, then a bolt of 3
 * doing 20. */
static bool item_bolt(cast *cx)
{
    cok_adventure *game = cx->game;
    game->effects.rolls.damage_type = 4;
    uint8_t amount = (uint8_t)(roll(game, 1, 6) + 20);
    bool bounce = false;
    return bolt_cell(cx, game->spell_x, game->spell_y, amount, 4, 0, &bounce) &&
           bolt(cx, 3, 0x14, 4, false);
}

/* 3d5e, Slow: those against the caster, as many as its level, Haste cured
 * (0x27) in those that have it, the others slowed: "is Slowed". */
static bool slow(cast *cx)
{
    return hasten(cx, 0x27, cok_combat_opposite(cx->caster), "is Slowed");
}

static bool paralyzed(cast *cx)
{
    return apply(cx, 0, false, 0, 0, "is paralyzed");
}

/* 3feb, 0x41: 2d4 + 2, magic. */
static bool item_damage(cast *cx)
{
    return apply(cx, 0, false, (uint8_t)(damage_roll(cx->game, 2, 4) + 2), 8, "");
}

/* 4026, Cause Serious Wounds (by touch): 2d8 + 1, or + 3 for a caster of
 * deity 4, magic. */
static bool cause_serious(cast *cx)
{
    uint8_t bonus = cx->caster->record[0x5d] == 4 ? 3 : 1;
    return apply(cx, 0, false, (uint8_t)(damage_roll(cx->game, 2, 8) + bonus), 8, "");
}

/* 418e, Poison: apply (a save with no effect: 0x44's byte 10 is 0); then
 * the caster's target (+0x0a) runs event 9 for effect 0x40 pending, and
 * unless that cancels it, 0x40's handler poisons the caster's target
 * (3f44:1b55: a save of type 0, or "is Poisoned" and killed), whatever the
 * first save made. */
static bool poison_spell(cast *cx)
{
    cok_adventure *game = cx->game;
    if (!apply(cx, 0, false, 0, 8, "")) return false;
    uint8_t *record = cok_character_combat(cx->caster)->target;
    cok_character *t = record != NULL ? member_of(game, record) : NULL;
    if (t == NULL)
        return undefined(game, "Poison's target is read through NULL or a record not in the "
                               "list (5b04:41c0)");
    game->effects.rolls.pending = 0x40;
    if (!dispatch(game, t, 9)) return false;
    if (game->effects.rolls.pending != 0x40) return true;
    return cok_effects_run(&game->effects, cx->caster, 0x40, NULL) || effect_failed(game);
}

/* 4214, Sticks to Snakes: on a first target of fewer than 6 hit dice
 * (+0xd6, signed), effect 3 valued at the caster level, then its handler;
 * else it "smashes them flat". */
static bool sticks_to_snakes(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *t = cx->targets[1];
    if (t == NULL)
        return undefined(game, "the first target is read through NULL (5b04:421a)");
    if ((int8_t)t->record[0xd6] >= 6) return say_waiting(game, t, "smashes them flat");
    uint8_t level;
    if (!level_of(game, game->effects.rolls.spell, &level) ||
        !apply(cx, level, false, 0, 8, ""))
        return false;
    t = cx->targets[1];
    cok_effect *snakes = t != NULL ? cok_character_find_effect(t, 3) : NULL;
    return snakes == NULL || cok_effects_run(&game->effects, t, 3, snakes) || effect_failed(game);
}

/* 42f3, Charm Monsters: through 1cbd with no bonus, "is charmed"; then
 * each target that has 0x0b, its handler. */
static bool charm_monsters(cast *cx)
{
    cok_adventure *game = cx->game;
    if (!missiles(cx, 0, "is charmed")) return false;
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n && !game->vm.abort; ++i) {
        cok_character *t = cx->targets[i];
        if (t == NULL)
            return undefined(game, "a target taken off the list is read through NULL "
                                   "(5b04:4337)");
        cok_effect *charm = cok_character_find_effect(t, 0x0b);
        if (charm != NULL && !cok_effects_run(&game->effects, t, 0x0b, charm))
            return effect_failed(game);
    }
    return true;
}

/* 4382, Confusion: at most 2d8 of the targets, each throwing (type 4) or
 * getting 0x23 for the spell's duration, "is confused". */
static bool confusion(cast *cx)
{
    cok_adventure *game = cx->game;
    uint8_t most = roll(game, 2, 8);
    if (game->spell_target_count > most) game->spell_target_count = most;
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n && !game->vm.abort; ++i) {
        cok_character *t = cx->targets[i];
        if (t == NULL) continue;
        bool saved;
        uint16_t minutes;
        if (!cok_effects_save(&game->effects, t, 4, 0, &saved)) return effect_failed(game);
        if (!cok_cast_duration(game, 0x52, &minutes) ||
            !cok_cast_add_effect(game, t, 0x23, minutes, 0, false, 1, saved, "is confused"))
            return false;
    }
    return true;
}

/* 4446, Dimension Door: the caster is erased and put at the spell's cell
 * (6beb:10f3), which leaves it off the map where another stands; the screen
 * centred on it; "teleports". */
static bool dimension_door(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    cok_character *c = cx->caster;
    bool placed;
    if (!cok_arena_erase(game, 0, 0, cok_combat_index(combat, c->record))) return false;
    if (!cok_combat_place(combat, game->vm.mode, c, (uint8_t)game->spell_x,
                          (uint8_t)game->spell_y, false, &placed))
        return undefined(game, "a combatant put at the spell's cell reads or writes outside its "
                               "tables (6beb:10f3)");
    return cok_arena_centre(game, cok_combat_x(combat, c->record),
                            cok_combat_y(combat, c->record), 0, 8) &&
           say_waiting(game, c, "teleports");
}

/* 4801, Fumble: the first target throws (type 4) after the damage type
 * becomes 0x40; failing, it gets 0x1b for the spell's duration, "is
 * clumsy", and 0x1b's handler runs with no effect; else 0x2a, "is
 * slowed", and its handler. Then the spell is applied all the same: a
 * second save, and 0x1b run when removed, "is clumsy". */
static bool fumble(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_character *t = cx->targets[1];
    if (t == NULL) return undefined(game, "the first target is read through NULL (5b04:481f)");
    game->effects.rolls.damage_type = 0x40;
    bool saved;
    if (!cok_effects_save(&game->effects, t, 4, 0, &saved)) return effect_failed(game);
    uint8_t id = saved ? 0x2a : 0x1b;
    uint16_t minutes;
    if (!cok_cast_duration(game, 0x56, &minutes) ||
        !cok_cast_add_effect(game, t, id, minutes, 0, false, 0, false,
                             saved ? "is slowed" : "is clumsy"))
        return false;
    if (cok_character_find_effect(t, id) != NULL &&
        !cok_effects_run(&game->effects, t, id, NULL))
        return effect_failed(game);
    return apply(cx, 0, true, 0, 0, "is clumsy");
}

/* 4914, Ice Storm: 3d10 of cold. */
static bool ice_storm(cast *cx)
{
    return apply(cx, 0, false, damage_roll(cx->game, 3, 10), 0x0a, "");
}

/* 17eb: x and y kept on the map. */
static void clamp(int8_t *x, int8_t *y)
{
    if (*x > 0x31) *x = 0x31;
    else if (*x < 0) *x = 0;
    if (*y > 0x18) *y = 0x18;
    else if (*y < 0) *y = 0;
}

/* 1836: the combatants on the line's cells after its start, each added to
 * list where it is not yet. */
static bool add_line(cast *cx, cok_combat_line *line, uint8_t list[32])
{
    while (cok_combat_line_step(line)) {
        uint8_t occupant, terrain;
        cok_combat_cell(&cx->game->combat, (int8_t)line->x, (int8_t)line->y, &occupant, &terrain);
        if (occupant == 0) continue;
        unsigned i = 1;
        while (list[i] != occupant && list[i] != 0)
            if (++i > 30)
                return undefined(cx->game, "a cone's list of more than 30 runs into its "
                                           "directions (5b04:18e7)");
        list[i] = occupant;
    }
    return true;
}

/* The data segment's bytes from DS:1ed6 to 1fec, which a cone's step of
 * any byte reads as a direction (5b04:19e7): the steps' tables and the
 * terrain table after them (which share DS:1ee4-1ee7). */
static int8_t ds_step(unsigned at)
{
    if (at < 0x1edf) return step_x[at - 0x1ed6];
    if (at < 0x1ee4) return step_y[at - 0x1edf];
    const cok_terrain *t = &cok_combat_terrain[(at - 0x1ee4) / 4];
    const uint8_t bytes[4] = {t->cost, t->eye, t->block, t->tile};
    return (int8_t)bytes[(at - 0x1ee4) % 4];
}

/* The directions a cone aimed at its caster's own cell reads (dirs[0],
 * on), never written by 1903: the locals that drawing the caster's icon
 * at 1415's end (6beb:0ad8, 6d21:04b0, the Tandy blit's) left at that
 * depth: the screen offsets of its cell, sx and sy from the view's corner
 * (an emulator probe of 78 casts, every cell it reached, agrees; 6d21:04b0
 * computes them). Only bytes 0-12 are read before Fear's length (6) is
 * reached. A footprint other than one cell is not known: the run stops. */
static bool drawing_locals(cast *cx, int8_t x0, int8_t y0, uint8_t dirs[50])
{
    cok_combat *combat = &cx->game->combat;
    if (cok_combat_size(combat, cx->caster->record) != 1)
        return undefined(cx->game, "a cone aimed at the cell of a caster larger than one reads "
                                   "what drawing its icon left, which the port knows only for "
                                   "one cell (5b04:19a5)");
    int sx = x0 - combat->view_x, sy = y0 - combat->view_y;
    uint16_t offset = (uint16_t)(2016 * (sy + 1)); /* a cell row of the buffer, 24 x 84 */
    const uint8_t left[16] = {0,    (uint8_t)(12 * (6 - sx)), 0, 12, 0, 32, 1,
                              (uint8_t)offset, (uint8_t)(offset >> 8), 24, 0,
                              (uint8_t)(24 * sy), 0, (uint8_t)(12 * sx), 0, 0};
    memset(dirs, 0, 50);
    memcpy(dirs, left, sizeof left);
    return true;
}

/* 1903: the combatants in a cone from x0, y0 toward tx, ty, as the
 * targets, the selected character (the caster) left out. The aim is
 * pushed on, repeating the line's own steps, until the length walked (2 a
 * step, 3 a diagonal one) reaches len cells or it reaches the map's edge,
 * then kept on the map and pulled back to the first cell that blocks
 * sight (6b30:03f1). The line to it gives the combatants on its cells,
 * nearest first, each once, though none after one met a second time;
 * for width 2 or more a line to the end's neighbour on the right, by the
 * direction of the last step that met one, and for 3 or more the left,
 * add theirs; width 0xff those within 3 of the end (6b30:08d8) instead.
 *
 * Bytes come from the stack, never set. The sight's range: the return
 * offset 127f:2678's blit pushed there when the caster's terrain was
 * drawn back in its flight (0x2705 here, Tandy; 0x21d0 or 0x2458 in the
 * other modes: no limit in any). The last direction where no one is on
 * the line: the low byte of the data segment that blit pushed, which
 * depends on where DOS loaded the game (PSP + 0x10 + 0x0bc6): 0xc6 for a
 * PSP of 0xnnf0, as the emulator's (its load segment 0x1000), assumed here,
 * whose entries of the tables of neighbours (DS:0d94-0dac) are the screen
 * frame's bytes: the right 4 cells east of the end, the left the end. (A
 * probe of 88 Fear casts with no one on the line found the same targets
 * for all 256 bytes.) Aimed at the caster's own cell, the directions are
 * what drawing its icon left (drawing_locals). A caster not shown after
 * its flight leaves other bytes: the run stops. */
static bool cone(cast *cx, int8_t x0, int8_t y0, int8_t tx, int8_t ty, uint8_t width,
                 uint8_t len)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    if (!cx->shown)
        return undefined(game, "a cone's range and side come from the stack its caster's flight "
                               "left, the caster not shown (5b04:1a2c)");
    cok_combat_line line = {.x0 = x0, .y0 = y0, .x1 = tx, .y1 = ty};
    cok_combat_line_start(&line);
    uint8_t dirs[50], k = 0;
    while (cok_combat_line_step(&line)) {
        if (k == 50)
            return undefined(game, "a cone's line of more than 50 steps runs past its "
                                   "directions (5b04:193a)");
        dirs[k++] = line.direction;
    }
    uint8_t last = (uint8_t)(k - 1), dist = line.length;
    if (k == 0 && !drawing_locals(cx, x0, y0, dirs)) return false;
    k = 0;
    while (dist < (uint8_t)(len * 2) && tx < 0x31 && tx > 0 && ty < 0x18 && ty > 0) {
        uint8_t d = dirs[k];
        if (d <= 7) dist = (uint8_t)(dist + ((d & 1) != 0 ? 3 : 2));
        tx = (int8_t)(tx + ds_step(0x1ed6 + d));
        ty = (int8_t)(ty + ds_step(0x1edf + d));
        k = k == last ? 0 : (uint8_t)(k + 1);
    }
    clamp(&tx, &ty);
    uint16_t range = 0x2705;
    bool seen;
    if (!cok_combat_sight(combat, x0, y0, &tx, &ty, &range, &seen))
        return undefined(game, "terrain past the table is read (DS:1ee4, 6b30:03f1)");
    uint8_t list[32] = {0}, n = 1, side = 0xc6;
    bool again = false;
    line = (cok_combat_line){.x0 = x0, .y0 = y0, .x1 = tx, .y1 = ty};
    cok_combat_line_start(&line);
    while (cok_combat_line_step(&line)) {
        uint8_t occupant, terrain;
        cok_combat_cell(combat, (int8_t)line.x, (int8_t)line.y, &occupant, &terrain);
        if (occupant == 0) continue;
        if (n > 1) {
            for (uint8_t i = 1; i < n; ++i)
                if (list[i] == occupant) again = true;
        } else {
            again = false;
        }
        if (!again) {
            if (n > 30)
                return undefined(game, "a cone's list of more than 30 runs into its directions "
                                       "(5b04:1af3)");
            list[n++] = occupant;
        }
        side = line.direction;
    }
    if (width > 1 && width != 0xff) {
        int8_t left_x = tx, left_y = ty, right_x = (int8_t)(tx + 4), right_y = ty;
        if (side < 8) {
            left_x = (int8_t)(tx + cone_sides[0][side]);
            left_y = (int8_t)(ty + cone_sides[1][side]);
            right_x = (int8_t)(tx + cone_sides[2][side]);
            right_y = (int8_t)(ty + cone_sides[3][side]);
        } else if (side != 0xc6) {
            return undefined(game, "a cone's side past its tables (5b04:1b31)");
        }
        clamp(&left_x, &left_y);
        clamp(&right_x, &right_y);
        line = (cok_combat_line){.x0 = x0, .y0 = y0, .x1 = right_x, .y1 = right_y};
        cok_combat_line_start(&line);
        if (!add_line(cx, &line, list)) return false;
        if (width > 2) {
            line = (cok_combat_line){.x0 = x0, .y0 = y0, .x1 = left_x, .y1 = left_y};
            cok_combat_line_start(&line);
            if (!add_line(cx, &line, list)) return false;
        }
    }
    if (width == 0xff) {
        if (!cok_combat_list(combat, tx, ty, 3, 0xff, 1))
            return undefined(game, "the list of combatants around one (6b30:08d8) reads past "
                                   "its tables");
        if (combat->listed_count > 30)
            return undefined(game, "a cone's list of more than 30 runs into its directions "
                                   "(5b04:1c19)");
        for (uint8_t i = 1; i <= combat->listed_count; ++i) list[i] = combat->listed[i].index;
    }
    uint8_t m = 0;
    for (n = 1; list[n] != 0; ++n) {
        if (n > 30)
            return undefined(game, "a cone's list of 30 is read on into its directions "
                                   "(5b04:1cb1)");
        cok_character *p = combat->combatant[list[n]].character;
        if (p == cx->caster) continue;
        cx->targets[++m] = p;
    }
    game->spell_target_count = m;
    return true;
}

/* 44ed, Fear: a cone of width 3 and length 6 toward the spell's cell; each
 * in it, either side, throws (type 4); failing, effect 0x6f for the spell's
 * duration, run when removed, "runs in terror", and whatever became of the
 * effect it flees (+0x10), the computer controls it (+0x18b), a player's
 * character turned (+0xe7 0xb3), and it forgets its target; else it "is
 * unaffected". */
static bool fear(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    const uint8_t *self = cx->caster->record;
    if (!cone(cx, cok_combat_x(combat, self), cok_combat_y(combat, self), game->spell_x,
              game->spell_y, 3, 6))
        return false;
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n && !game->vm.abort; ++i) {
        cok_character *t = cx->targets[i];
        bool saved;
        if (!cok_effects_save(&game->effects, t, 4, 0, &saved)) return effect_failed(game);
        if (saved) {
            if (!say_waiting(game, t, "is unaffected")) return false;
            continue;
        }
        uint16_t minutes;
        if (!cok_cast_duration(game, 0x54, &minutes) ||
            !cok_cast_add_effect(game, t, 0x6f, minutes, 0, true, 1, false, "runs in terror"))
            return false;
        if (t->combat == NULL)
            return undefined(game, "a record with no combat record is written through NULL "
                                   "(5b04:45a7)");
        t->combat->forced = 1;
        t->record[0x18b] = 1;
        if (t->record[0xe7] <= 0x7f) t->record[0xe7] = 0xb3;
        t->combat->target = NULL;
    }
    return true;
}

/* 22: Stinking Cloud (5b04:29de) lays clouds on the map, which are not
 * ported: logged and skipped, the spell used up. */
static bool stinking_cloud(cast *cx)
{
    cok_adventure_log(cx->game, "unported", "Stinking Cloud's clouds (5b04:29de)");
    return true;
}

/* DS:6e3a + 4 * spell, as 5b04:58ed fills it. */
static const struct {
    uint16_t address;
    handler_fn *fn;
} handlers[COK_SPELLS] = {
    [0x01] = {0x1efb, bless},             [0x02] = {0x1f2c, curse},
    [0x03] = {0x1f57, cure_light},        [0x04] = {0x1fc2, cause_light},
    [0x05] = {0x2004, affected},          [0x06] = {0x203e, protected},
    [0x07] = {0x203e, protected},         [0x08] = {0x207d, cold_resistant},
    [0x09] = {0x20ab, burning_hands},     [0x0a] = {0x20f8, charm_person},
    [0x0b] = {0x2004, affected},          [0x0c] = {0x21d7, enlarge},
    [0x0d] = {0x230c, reduce},            [0x0e] = {0x23ac, friends},
    [0x0f] = {0x23f3, magic_missile},     [0x10] = {0x203e, protected},
    [0x11] = {0x203e, protected},         [0x12] = {0x2004, affected},
    [0x13] = {0x2456, shielded},          [0x14] = {0x2484, shocking_grasp},
    [0x15] = {0x24d8, sleep_spell},       [0x16] = {0x2004, affected},
    [0x17] = {0x2616, hold},              [0x18] = {0x2679, fire_resistant},
    [0x19] = {0x26b2, silence},           [0x1a] = {0x26eb, slow_poison},
    [0x1b] = {0x279a, snake_charm},       [0x1c] = {0x2877, hammer},
    [0x1d] = {0x2004, affected},          [0x1e] = {0x28c7, invisible},
    [0x1f] = {0x2900, knock},             [0x20] = {0x293b, mirror_image},
    [0x21] = {0x2999, weakened},          [0x22] = {0x29de, stinking_cloud},
    [0x23] = {0x2e4e, strength},          [0x24] = {0x3000, nothing},
    [0x25] = {0x300d, cure_blindness},    [0x26] = {0x3053, blinded},
    [0x27] = {0x3107, cure_disease},      [0x28] = {0x3124, diseased},
    [0x29] = {0x320f, dispel_magic},      [0x2a] = {0x358a, prayer},
    [0x2b] = {0x35f5, remove_curse},      [0x2c] = {0x3705, bestow_curse},
    [0x2d] = {0x373e, blink},             [0x2e] = {0x320f, dispel_magic},
    [0x2f] = {0x376c, fireball},          [0x30] = {0x3991, haste},
    [0x31] = {0x2616, hold},              [0x32] = {0x28c7, invisible},
    [0x33] = {0x3d04, lightning_bolt},    [0x34] = {0x203e, protected},
    [0x35] = {0x203e, protected},         [0x36] = {0x203e, protected},
    [0x37] = {0x3d5e, slow},              [0x38] = {0x3d8c, nothing},
    [0x39] = {0x3d9b, speedy},            [0x3a] = {0x3ddc, cure_serious},
    [0x3b] = {0x3e53, giant_strength},    [0x3c] = {0x3ed8, item_bolt},
    [0x3d] = {0x3f2c, paralyzed},         [0x3e] = {0x3f63, potion},
    [0x3f] = {0x3fbd, invisible},         [0x40] = {0x376c, fireball},
    [0x41] = {0x3feb, item_damage},       [0x42] = {0x4026, cause_serious},
    [0x43] = {0x4098, neutralize_poison}, [0x44] = {0x418e, poison_spell},
    [0x45] = {0x203e, protected},         [0x46] = {0x4214, sticks_to_snakes},
    [0x47] = {0x42a7, nothing},           [0x48] = {0x42b1, nothing},
    [0x49] = {0x42b6, nothing},           [0x4a] = {0x42bb, nothing},
    [0x4b] = {0x42c0, nothing},           [0x4c] = {0x42ca, nothing},
    [0x4d] = {0x2004, affected},          [0x4e] = {0x42d4, nothing},
    [0x4f] = {0x42de, nothing},           [0x50] = {0x42e3, nothing},
    [0x51] = {0x42f3, charm_monsters},    [0x52] = {0x4382, confusion},
    [0x53] = {0x4446, dimension_door},    [0x54] = {0x44ed, fear},
    [0x55] = {0x464d, fire_shield},       [0x56] = {0x4801, fumble},
    [0x57] = {0x4914, ice_storm},         [0x58] = {0x4957, protected},
    [0x59] = {0x35f5, remove_curse},      [0x5a] = {0x3000, nothing},
    [0x5b] = {0x4984, nothing},           [0x5c] = {0x498e, nothing},
    [0x5d] = {0x4998, nothing},           [0x5e] = {0x2616, hold},
    [0x5f] = {0x49a3, empty_text},        [0x60] = {0x49d1, empty_text},
    [0x61] = {0x49ff, empty_text},        [0x62] = {0x4a2c, nothing},
    [0x63] = {0x4a40, potion},            [0x64] = {0x3705, bestow_curse},
    [0x65] = {0x203e, protected},         [0x66] = {0x26b2, silence},
    [0x67] = {0x2004, affected},          [0x68] = {0x35f5, remove_curse},
    [0x69] = {0x1efb, bless},             [0x6a] = {0x20f8, charm_person},
    [0x6b] = {0x20ab, burning_hands},
};

/* 5b04:127e: the targets outside combat, by byte 7: 1 the caster; 2 one
 * picked ("Cast Spell on whom"), starting from the last target (DS:710b,
 * the caster if none); 4 the whole party. Returns whether there are any;
 * false with game->vm.abort set when input ended. */
static bool target(cast *cx)
{
    cok_adventure *game = cx->game;
    if (game->spell_target == NULL) game->spell_target = cx->caster->record;
    cx->targets[1] = cx->caster;
    game->spell_target_count = 1;
    switch (spell_byte(cx->spell, 7)) {
    case 1: return true;
    case 2: {
        cok_adventure_redraw(game);
        bool ended;
        uint8_t *picked = cok_adventure_pick(game, "Cast Spell on whom", game->spell_target, true,
                                             &ended);
        if (ended) return false;
        game->spell_target = picked;
        cx->targets[1] = member_of(game, picked);
        if (cx->targets[1] == NULL) {
            game->spell_target_count = 0;
            return false;
        }
        return true;
    }
    case 4:
        /* The whole list from DS:609a, monsters loaded too. */
        for (size_t i = 0; i < game->party.count; ++i) cx->targets[i + 1] = game->party.members[i];
        game->spell_target_count = (uint8_t)game->party.count;
        return true;
    default: return false;
    }
}

/* Combat: the targets (432f:2337 with 210b and 1eed) and the cast's
 * flight (5b04:1415). */

bool cok_cast_range(cok_adventure *game, uint8_t spell, uint8_t *range)
{
    uint8_t base = spell_byte(spell, 2), per = spell_byte(spell, 3), level = 6;
    if (game->effects.rolls.item == 0 && !level_of(game, spell, &level)) return false;
    uint8_t r = (uint8_t)(base + per * level);
    if (r == 0 && spell_byte(spell, 6) != 0) r = 1;
    if (r == 0xff) r = 1;
    *range = r;
    return true;
}

bool cok_cast_wounded_ally(cok_adventure *game, cok_character *c, cok_character **ally)
{
    cok_combat *combat = &game->combat;
    cok_character *best = NULL;
    uint8_t lowest = 0xff, body = 0;
    for (unsigned d = 0; d <= 8; ++d) {
        int8_t x = (int8_t)(cok_combat_x(combat, c->record) + step_x[d]);
        int8_t y = (int8_t)(cok_combat_y(combat, c->record) + step_y[d]);
        uint8_t occupant, terrain;
        cok_combat_cell(combat, x, y, &occupant, &terrain);
        if (occupant > 0) {
            cok_character *o = combat->combatant[occupant].character;
            if (o == NULL)
                return undefined(game, "a cell's combatant has no record (432f:1f8d)");
            const uint8_t *r = o->record;
            if (r[0x18a] != c->record[0x18a] || r[0x197] >= r[0x62]) continue;
            if (r[0x197] < lowest || (o == c && r[0x197] < r[0x62] >> 1)) {
                best = o;
                lowest = r[0x197];
            }
        } else if (terrain == COK_COMBAT_BODY) {
            for (uint8_t b = 1; b <= combat->bodies; ++b) {
                if (b > COK_COMBAT_BODIES)
                    return undefined(game, "a body past the table's eighth is read (432f:2043)");
                const cok_combat_body *e = &combat->body[b - 1];
                if (e->character == NULL) continue;
                uint8_t status = e->character->record[0x188];
                if (status >= 6 && status <= 8) continue; /* 432f:1ecd */
                if (e->x == x && e->y == y) body = b;
            }
        }
    }
    *ally = lowest >= 8 && body != 0 ? combat->body[body - 1].character : best;
    return true;
}

bool cok_cast_reflect(cok_adventure *game, cok_character *c, cok_character **caster)
{
    cok_combat *combat = &game->combat;
    cok_character *selected = member_of(game, game->vm.character);
    if (selected == NULL)
        return undefined(game, "a spell is turned back on no selected character (3f44:2404)");
    if (!cok_arena_say(game, c, "reflects the spell", 10, false)) return false;
    game->spell_x = cok_combat_x(combat, selected->record);
    game->spell_y = cok_combat_y(combat, selected->record);
    cok_character **t = game->spell_targets;
    for (uint8_t i = 1; i <= game->spell_target_count; ++i)
        if (t[i] == c) t[i] = selected;
    if (t[1] == &cok_arena_freed)
        return undefined(game, "a spell is turned back toward a first target freed since, whose "
                               "cell is read where the heap may have put another (3f44:24af)");
    int8_t x, y;
    if (!cok_arena_missile_frames(game, 0x12) || !cell(game, t[1], &x, &y) ||
        !cok_arena_missile(game, cok_combat_x(combat, c->record), cok_combat_y(combat, c->record),
                           x, y, 1, 0x1e))
        return false;
    *caster = selected;
    return true;
}

/* 432f:210b: one target for the cast, picked for the selected character
 * (the caster). The player aims (432f:3b80) within the spell's range
 * (5b04:0ecb), not attacking, at an empty cell too where square, at a
 * creature unless the spell is Dimension Door (0x53); the pick, or none,
 * becomes the caster's target (+0x0a). The computer takes itself for a
 * spell not hostile (byte 14), or for Cure Light Wounds (3) the ally
 * cok_cast_wounded_ally finds; else a target within the range that
 * 432f:3f9f picks afresh, unless that one is helpless (6346:0cdb) and the
 * spell's effect (byte 10) is one of those that make it so (DS:200c).
 * With a pick, the target's cell becomes the spell's (DS:710f, 7110);
 * with none, *res is cleared. */
static bool pick(cast *cx, bool square, bool *found, cok_combat_aimed *res)
{
    cok_adventure *game = cx->game;
    cok_character *c = cx->caster;
    *found = false;
    uint8_t range;
    if (!cx->ai) {
        if (!cok_cast_range(game, cx->spell, &range) ||
            !cok_combat_aim(game, c, range, 0, square, cx->spell != 0x53, found, res))
            return false;
        if (game->vm.abort) return true;
        cok_combat_record *cr = c->combat;
        if (cr == NULL)
            return undefined(game, "a caster with no combat record is written through NULL "
                                   "(432f:2165)");
        cr->target = res->target;
    } else if (spell_byte(cx->spell, 14) == 0) {
        cok_character *t = c;
        if (cx->spell == 3 && !cok_cast_wounded_ally(game, c, &t)) return false;
        if (t != NULL) {
            res->target = t->record;
            if (!cell(game, t, &res->x, &res->y)) return false;
            *found = true;
        }
    } else {
        if (!cok_cast_range(game, cx->spell, &range)) return false;
        bool picked;
        if (!cok_combat_pick_target(game, c, range, false, true, &picked)) return false;
        if (picked) {
            uint8_t *record = cok_character_combat(c)->target;
            cok_character *t = record != NULL ? member_of(game, record) : NULL;
            if (t == NULL)
                return undefined(game, "the computer's target is not in the list (432f:2235)");
            bool keep = true;
            if (cok_combat_helpless(t)) {
                for (unsigned k = 0; k < 4; ++k)
                    if (disabling[k] == spell_byte(cx->spell, 10)) keep = false;
            }
            if (keep) {
                res->target = record;
                if (!cell(game, t, &res->x, &res->y)) return false;
                *found = true;
            }
        }
    }
    if (*found) {
        game->spell_x = res->x;
        game->spell_y = res->y;
    } else {
        *res = (cok_combat_aimed){NULL, 0, 0};
    }
    return true;
}

/* A pick already in the list (entries 1 to n). */
static bool listed_already(const cast *cx, uint8_t n, const uint8_t *record)
{
    for (uint8_t i = 1; i <= n; ++i)
        if ((cx->targets[i] != NULL ? cx->targets[i]->record : NULL) == record) return true;
    return false;
}

/* The record a pick names, a member of the list, or NULL for none. */
static bool picked_member(cast *cx, const cok_combat_aimed *res, cok_character **t)
{
    *t = NULL;
    if (res->target == NULL) return true;
    *t = member_of(cx->game, res->target);
    if (*t == NULL) return undefined(cx->game, "a spell's target is not in the list (432f:2337)");
    return true;
}

/* Redraw the cell of a pick (6beb:02aa). */
static bool redraw_pick(cast *cx, cok_character *t)
{
    int8_t x, y;
    return cell(cx->game, t, &x, &y) && cok_arena_redraw_cell(cx->game, x, y);
}

/* 432f:2337, the targets in combat (DS:6e3a in a battle), by the low
 * nibble k of byte 6 of the spell table: 0 the caster; 5 picks (Faerie
 * Fire, Charm Monsters) while their weight is within an allowance, the
 * caster level for Faerie Fire, else 2d4; 0x0f (Silence) one pick, its
 * target alone, or with none those at the cell (radius (k >> 4), 0); 8 to
 * 0x0e a cell and everyone within byte 6 & 7 of it, either side; else up
 * to (byte 6 & 3) + 1 separate picks, Hold Person refusing the caster.
 * *done is cleared where none was picked, but never for k 5. */
static bool combat_targets(cast *cx, bool *done)
{
    cok_adventure *game = cx->game;
    cok_rolls *r = &game->effects.rolls;
    cok_character **t = cx->targets;
    *done = true;
    game->spell_target_count = 0;
    r->images = 0;
    if (!cell(game, cx->caster, &game->spell_x, &game->spell_y)) return false;
    uint8_t kind = spell_byte(cx->spell, 6) & 0x0f;
    cok_combat_aimed res = {NULL, 0, 0};
    bool found;
    cok_character *p;
    if (kind == 0) {
        t[1] = cx->caster;
        game->spell_target_count = 1;
        return true;
    }
    if (kind == 5) {
        uint8_t total = 0, picks = 0, allowance;
        if (cx->spell == 0x4f) {
            if (!level_of(game, 0x4f, &allowance)) return false;
        } else {
            allowance = roll(game, 2, 4);
        }
        bool stop = false;
        do {
            if (!pick(cx, false, &found, &res)) return false;
            if (game->vm.abort) return true;
            if (!found) {
                stop = true;
                continue;
            }
            if (!picked_member(cx, &res, &p)) return false;
            if (!listed_already(cx, picks, res.target)) {
                t[++picks] = p;
                if (!cell(game, p, &game->spell_x, &game->spell_y)) return false;
                ++game->spell_target_count;
                if (p == NULL)
                    return undefined(game, "a pick of no creature is weighed through NULL "
                                           "(432f:2484)");
                if (cx->spell != 0x4f) {
                    int8_t hd = (int8_t)p->record[0xd6];
                    total = (uint8_t)(total + (hd == 0 || hd == 1 ? 1
                                               : hd == 2             ? 2
                                               : hd == 3             ? 4
                                                                     : 8));
                } else {
                    uint8_t size = p->record[0xcf];
                    total = (uint8_t)(total + (size == 1                 ? 1
                                               : size == 2 || size == 3 ? 2
                                               : size == 4              ? 4
                                                                        : 0));
                }
                if (picks > 1 && total > allowance) stop = true;
            } else if (cx->ai) {
                --allowance;
            } else {
                cok_camp_notice(game, "Already been targeted"); /* 6346:1827 */
            }
            if (!redraw_pick(cx, p)) return false;
        } while (!stop && allowance != 0);
        return true;
    }
    if (kind == 0x0f) {
        if (!pick(cx, false, &found, &res)) return false;
        if (game->vm.abort) return true;
        if (!found) {
            *done = false;
            return true;
        }
        uint8_t *aimed = cok_character_combat(cx->caster)->target;
        if (aimed != NULL) {
            t[1] = member_of(game, aimed);
            if (t[1] == NULL)
                return undefined(game, "a spell's target is not in the list (432f:2580)");
            game->spell_target_count = 1;
            return true;
        }
        return area(cx, (uint8_t)((spell_byte(cx->spell, 6) & 0x0f) >> 4));
    }
    if (kind >= 8 && kind <= 0x0e) {
        if (!pick(cx, true, &found, &res)) return false;
        if (game->vm.abort) return true;
        if (!found) {
            *done = false;
            return true;
        }
        return area(cx, spell_byte(cx->spell, 6) & 7);
    }
    uint8_t left = (uint8_t)((spell_byte(cx->spell, 6) & 3) + 1), picks = 0;
    while (left > 0) {
        if (!pick(cx, false, &found, &res)) return false;
        if (game->vm.abort) return true;
        if (!found) {
            left = 0;
            continue;
        }
        if (!picked_member(cx, &res, &p)) return false;
        const uint8_t *self = cx->caster->record;
        bool again = listed_already(cx, picks, res.target) ||
                     ((cx->spell == 0x17 || cx->spell == 0x31) && res.target == self);
        if (!again) {
            t[++picks] = p;
            --left;
            if (!cell(game, p, &game->spell_x, &game->spell_y)) return false;
        } else if (!cx->ai) {
            cok_camp_notice(game, res.target == self ? "Cannot target yourself"
                                                     : "Already been targeted"); /* 6346:1827 */
        } else {
            --left;
        }
        if (!redraw_pick(cx, p)) return false;
    }
    game->spell_target_count = picks;
    if (picks == 0) {
        /* Entry 0 (DS:6fe7) is the handler table's last pointer's bytes
         * and the count: a pointer to no record, whose cell is entry 0's
         * of DS:623f, which nothing reads before the next cast sets it. */
        *done = false;
        game->spell_x = game->combat.combatant[0].x;
        game->spell_y = game->combat.combatant[0].y;
        return true;
    }
    return cell(game, t[picks], &game->spell_x, &game->spell_y);
}

/* 5b04:1415's flight in combat: the spell's missile (icon slot 0x12), the
 * caster turned toward the spell's cell in its casting pose, sound 0x0b
 * for Fireball, 8 for Lightning Bolt, else 2 (DS:1e5e, 1e58, 1e4c), the
 * missile flown, and the caster, if shown, drawn back ready. */
static bool fly(cast *cx)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    cok_character *c = cx->caster;
    if (!cok_arena_missile_frames(game, 0x12)) return false;
    int8_t x = cok_combat_x(combat, c->record), y = cok_combat_y(combat, c->record);
    uint8_t dir = 0;
    for (;;) {
        bool in;
        if (!cok_combat_in_arc(x, y, game->spell_x, game->spell_y, dir, &in))
            return undefined(game, "no direction holds the spell's cell (6b30:054a)");
        if (in) break;
        ++dir;
    }
    combat->show_actions = true;
    if (!cok_arena_pose(game, c, dir, 1, false)) return false;
    cok_arena_sound(game, cx->spell == 0x2f ? 0x0b : cx->spell == 0x33 ? 8 : 2);
    if (!cok_arena_missile(game, x, y, game->spell_x, game->spell_y, 4, 0x1e)) return false;
    bool shown;
    if (!cok_combat_visible(combat, c->record, false, &shown))
        return undefined(game, "a size past the footprints is read after them (6beb:06ef)");
    cx->shown = shown;
    if (!shown) return true;
    uint8_t facing = cok_character_combat(c)->facing;
    return cok_arena_pose(game, c, facing, 1, true) && cok_arena_pose(game, c, facing, 0, false);
}

/* 5b04:57eb: c "casts" or "miscasts" spell; in combat it "Casts a Spell"
 * whatever the verb (row 10, a pause), and "Spell:" and the spell's name
 * go on row 23. */
static bool announce_spell(cok_adventure *game, cok_character *c, const char *verb, uint8_t spell)
{
    if (game->vm.mode != 5) return cok_camp_spell_message(game, c->record, verb, spell);
    const char *name = cok_spell_name(spell);
    if (name == NULL)
        return undefined(game, "a spell with no name; 5b04:57eb would print the data after the "
                               "names");
    if (!cok_arena_say(game, c, "Casts a Spell", 10, true)) return false;
    cok_picture_fill(&game->screen, 0, 0x17 * 8, 40, 8, 0); /* 1128:07e6 */
    char text[64];
    snprintf(text, sizeof text, "Spell:%s", name);
    cok_adventure_log(game, "print", text);
    cok_text_string(&game->screen, &game->font, text, 0, 0x17, 10, 0); /* 1521:0353 */
    return true;
}

void cok_cast_spell(cok_adventure *game, uint8_t spell, bool ai, bool announce, uint8_t frame,
                    bool *done)
{
    cok_rolls *r = &game->effects.rolls;
    cast cx = {.game = game, .caster = member_of(game, game->vm.character), .spell = spell,
               .frame = frame, .ai = ai, .targets = game->spell_targets};
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
    bool combat = game->vm.mode == 5;
    uint8_t *c = cx.caster->record;
    if (combat) {
        const char *name = cok_spell_name(spell);
        char text[80];
        snprintf(text, sizeof text, "%.*s: %s", c[0] > 15 ? 15 : c[0], (const char *)c + 1,
                 name != NULL ? name : "");
        cok_adventure_log(game, "cast", text);
    }
    bool go = true;
    if (!combat && spell_byte(spell, 7) == 0) {
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
        if (!announce_spell(game, cx.caster, "miscasts", spell)) return;
        announce = false;
        go = false;
    }
    if (announce && r->item == 0 && !announce_spell(game, cx.caster, "casts", spell)) return;
    while (go) {
        bool targeted;
        if (game->combat_targets) {
            if (!combat_targets(&cx, &targeted)) return;
            *done = targeted;
        } else {
            targeted = target(&cx);
            if (targeted) *done = true;
        }
        if (game->vm.abort) return;
        if (targeted) {
            go = false;
            if (combat && !fly(&cx)) return;
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
        } else if (!combat) {
            go = false;
        } else {
            if (!ai) {
                int answer = cok_camp_yes_no(game, "Abort Spell? ", 14); /* 67b5:177f */
                if (answer < 0) return;
                if (answer != 'Y') continue;
            }
            cok_camp_notice(game, "Spell Aborted"); /* 6346:1827 */
            if (r->item == 0) forget(c, spell);
            go = false;
        }
    }
    if (game->vm.abort) return;
    cok_arena_clear_text(game); /* 6346:196a */
    if (combat) cok_picture_fill(&game->screen, 0, 0x17 * 8, 40, 8, 0); /* 1128:07e6 */
}
