#include "cast.h"

#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "cloud.h"
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
    /* A monster breathes (cok_cast_breath) rather than casts. */
    bool breath;
    cok_character **targets;
} cast;

/* The data segment's tables the combat targets and the cone read: the
 * steps by direction 0-8 (DS:1ed6, 1edf), the effects that make one
 * helpless (DS:200c), and the cone's neighbours of its end, left and
 * right, by direction (DS:0d94, 0d9c, 0da4, 0dac: x and y on the left,
 * x and y on the right, eight bytes each), with the bytes after them to
 * DS:0eab, which a side byte past 7 reads (the battle's effects, DS:0db4,
 * and constant tables of the screen's, DS:0dc4 on; nothing writes them). */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};
static const uint8_t disabling[4] = {0x33, 0x34, 0x35, 0x1f};
static const uint8_t cone_sides[0x118] = {
    0xff, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0x01, 0x01, 0x00,
    0x01, 0x01, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0xff,
    0x03, 0x0b, 0x15, 0x17, 0x1b, 0x1e, 0x1f, 0x33, 0x34, 0x35, 0x5b, 0x6a, 0x6b, 0x6f, 0x76, 0x77,
    0x00, 0x0f, 0x04, 0x0b, 0x0d, 0x02, 0x09, 0x0e, 0x00, 0x0f, 0x04, 0x0b, 0x0d, 0x02, 0x09, 0x0e,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x00, 0x0e, 0x0f,
    0x00, 0x02, 0x06, 0x0a, 0x16, 0x26, 0x36, 0x6e, 0x84, 0x9a, 0x01, 0x01, 0x01, 0x03, 0x02, 0x02,
    0x07, 0x02, 0x02, 0x01, 0x02, 0x04, 0x04, 0x04, 0x08, 0x08, 0x08, 0x0b, 0x0b, 0x02, 0x05, 0x00,
    0x04, 0x00, 0x06, 0x00, 0x04, 0x00, 0x02, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x09, 0x00,
    0x05, 0x00, 0x04, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x00, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x05, 0x00, 0x00, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x00, 0x00, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x00, 0x03, 0x04, 0x04, 0x04, 0x04, 0x05, 0x00, 0x00,
    0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
};

const cok_ds_table cok_cast_tables[] = {
    {0x1ed6, sizeof step_x, 1, (const uint8_t *)step_x},
    {0x1edf, sizeof step_y, 1, (const uint8_t *)step_y},
    {0x200c, sizeof disabling, 1, disabling},
    {0x0d94, sizeof cone_sides, 1, cone_sides},
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

/* What the port does not know, though the original does it the same way
 * each time. */
static bool unported(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", what);
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
    if (cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error))
        return true;
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    say(game, c,
        cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS)
            ? "is fully healed"
            : "is partially healed");
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
static void forget(const cok_character_layout *L, uint8_t *c, uint8_t spell)
{
    for (size_t i = 0; i < cok_field_count(L, COK_FIELD_MEMORIZED); ++i)
        if (cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) == spell) {
            cok_field_set8_at(L, c, COK_FIELD_MEMORIZED, i, 0);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
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
    if (r->amount == 0 || cok_field_u8(L, record, COK_FIELD_ACTIVE) == 0) return true;
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
        cok_character_damage(record, cok_game_rules(game), r->amount);
    } else {
        /* 6346:228c kind 0, a burst, then 6346:24d7 in combat; one casting
         * that is hurt "lost a spell" and its turn. */
        if (!cok_arena_flash(game, c, 0, text)) return false;
        if (c->combat == NULL)
            return undefined(game, "a record with no combat record is read through NULL in "
                                   "combat (60f4:1f95)");
        if (!cok_combat_damage(&game->combat, c, cok_game_rules(game), r->amount, 5))
            return undefined(game, "a record on a side other than 0 or 1 (+0x18a) is counted past "
                                   "DS:6b2d (6346:24d7)");
        c->combat->may_cast = 0;
        if (c->combat->spell > 0) {
            if (!cok_arena_say(game, c, "lost a spell", 12, true)) return false;
            forget(L, record, c->combat->spell);
            cok_combat_end_turn(c);
        }
    }
    if (cok_field_u8(L, record, COK_FIELD_ACTIVE) == 0) {
        snprintf(text, sizeof text, "Goes Down%s",
                 cok_field_u8(L, record, COK_FIELD_STATUS) == 5 ? ", and is Dying" : "");
        if (cok_field_u8(L, record, COK_FIELD_STATUS) >= 6 &&
            cok_field_u8(L, record, COK_FIELD_STATUS) <= 8)
            snprintf(text, sizeof text, "is killed");
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
            if (cok_field_u8(L, record, COK_FIELD_ACTIVE) == 0 &&
                cok_field_u8(L, record, COK_FIELD_STATUS) != 10) {
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
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
                !cok_combat_hit(game, cx->caster, t[i], cok_field_u8(L, t[i]->record, COK_FIELD_AC),
                                &hit))
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
    if (t == NULL) {
        undefined(cx->game, "the first target is read through NULL (DS:6feb)");
    } else if (t == &cok_arena_freed) {
        /* Only a list an earlier cast left (effect 0x4a's Dispel Magic). */
        undefined(cx->game, "the first target is a record freed since, read where the heap "
                            "may have put another (DS:6feb)");
        t = NULL;
    }
    return t;
}

/* 60f4:1592: whether strength and exceptional strength beat c's base ones
 * (+0x10, +0x1d), and the effect value that gives them (60f4:151a): the
 * exceptional strength + 1 at 18, else the strength + 100. */
static bool stronger(const cok_character_layout *L, const uint8_t *c, uint8_t strength,
                     uint8_t exceptional, uint8_t *value)
{
    if (!(strength > cok_field_u8_at(L, c, COK_FIELD_SCORE_BASE, COK_SCORE_STRENGTH) ||
          (strength == 18 &&
           exceptional > cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE))))
        return false;
    *value = (uint8_t)(strength == 18 ? exceptional + 1 : strength + 100);
    return true;
}

/* The handlers, by their address in overlay 5b04. */

typedef bool handler_fn(cast *cx);

/* 1e34: keep the targets on side, then apply with text. */
static bool sided(cast *cx, uint8_t side, const char *text)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character **t = cx->targets;
    game->effects.rolls.images = 1;
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n; ++i) {
        if (t[i] == NULL)
            return undefined(game, "a target taken off the list is read through NULL "
                                   "(5b04:1e72)");
        if (cok_field_u8(L, t[i]->record, COK_FIELD_HOSTILE) == side) {
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    return sided(cx, cok_field_u8(L, cx->caster->record, COK_FIELD_HOSTILE), "is Blessed");
}

/* 1f57, 3ddc: heal the target count d8 + bonus, and a d8 more for a
 * caster of deity 4, rolled first. */
static bool heal(cast *cx, uint8_t count, uint8_t bonus)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    if (game->spell_target_count == 0) return true;
    uint8_t extra =
        cok_field_u8(L, cx->caster->record, COK_FIELD_DEITY) == 4 ? roll(game, 1, 8) : 0;
    uint8_t amount = (uint8_t)(roll(game, count, 8) + bonus + extra);
    cok_character *c = first(cx);
    if (c == NULL) return false;
    if (cok_character_heal(c->record, cok_game_rules(game), amount, false, game->vm.mode))
        healed(game, c);
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *c = first(cx);
    if (c == NULL) return false;
    uint8_t level, strength = 18, exceptional = 0, value;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    static const uint8_t by_level[7] = {0, 0, 1, 0x33, 0x4c, 0x5b, 0x64};
    if (level >= 1 && level <= 6) exceptional = by_level[level];
    else if (level >= 7 && level <= 9) strength = (uint8_t)(12 + level);
    else if (level == 10 || level == 11) strength = 0x16;
    if (!stronger(L, c->record, strength, exceptional, &value)) {
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *c = first(cx);
    if (c == NULL) return false;
    uint8_t *r = c->record;
    if (cok_field_u8(L, r, COK_FIELD_STATUS) == 1) {
        cx->targets[1] = NULL;
        return true;
    }
    if (cok_character_find_effect(c, 0x37) == NULL) return true;
    if (cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == 0)
        cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 1);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *c = first(cx);
    if (c == NULL) return false;
    const uint8_t *r = c->record;
#define HAS(k)                                                                                     \
    (cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, k) > 0 ||                                        \
     cok_field_s8_at(L, r, COK_FIELD_FORMER_LEVEL, k) >                                            \
         cok_field_s8(L, r, COK_FIELD_CHANGE_LEVEL))
    int bonus = -1;
    if (HAS(5)) bonus = roll(game, 1, 4);
    if (HAS(0) || HAS(6)) bonus = roll(game, 1, 6);
    if (HAS(2)) bonus = roll(game, 1, 8);
    if (bonus < 0) bonus = game->effects.rolls.item != 0 ? 0 : cx->frame;
    uint8_t value = (uint8_t)(cok_field_u8_at(L, r, COK_FIELD_SCORE, COK_SCORE_STRENGTH) + bonus);
    uint8_t exceptional = 0;
    if (value > 18) {
        if (HAS(2) || HAS(7) || HAS(4)) {
            exceptional = (uint8_t)((value - 18) * 10 +
                                    cok_field_u8(L, r, COK_FIELD_STRENGTH_EXCEPTIONAL));
            if (exceptional > 100) exceptional = 100;
        }
        value = 18;
    }
#undef HAS
    uint8_t ignored;
    if (!stronger(L, r, value, exceptional, &ignored)) return true;
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
 * works on the first. Its second part tries the clouds around the spell's
 * cell (cok_cloud_dispel). */
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
    return cok_cloud_dispel(game, level);
}

/* 358a, Prayer: the caster's side in the value's high nibble. */
static bool prayer(cast *cx)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    uint8_t level;
    if (!level_of(cx->game, cx->spell, &level)) return false;
    uint8_t value = (uint8_t)(level + cok_field_s8(L, cx->caster->record, COK_FIELD_HOSTILE) * 16);
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
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
        if (cok_field_u8(L, c->record, COK_FIELD_HOSTILE) == side && left > 0) {
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    return hasten(cx, 0x2a, cok_field_u8(L, cx->caster->record, COK_FIELD_HOSTILE), "is Hasted");
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *c = first(cx);
    if (c == NULL) return false;
    uint8_t value;
    if (stronger(L, c->record, 0x15, 0, &value)) {
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
    if (cok_character_heal(c->record, cok_game_rules(game), (uint8_t)(roll(game, 2, 4) + 2), false,
                           game->vm.mode))
        return flash(game, c, "is Healed");
    return true;
}

/* 4098, Neutralize Poison: a target with status 1 is dropped; one that is
 * poisoned (0x37) gets at least 1 hit point, loses 0x37, 0x16 and 0x0f
 * (while curing, DS:6b38), is "unpoisoned" and okay, whatever its status. */
static bool neutralize_poison(cast *cx)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *c = first(cx);
    if (c == NULL) return false;
    uint8_t *r = c->record;
    if (cok_field_u8(L, r, COK_FIELD_STATUS) == 1) {
        cx->targets[1] = NULL;
        return true;
    }
    if (cok_character_find_effect(c, 0x37) == NULL) {
        say(game, c, "is unaffected");
        return true;
    }
    if (cok_field_u8(L, r, COK_FIELD_HIT_POINTS) == 0)
        cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 1);
    game->effects.rolls.curing = 1;
    bool ok = remove_effect(game, c, NULL, 0x37) && remove_effect(game, c, NULL, 0x16) &&
              remove_effect(game, c, NULL, 0x0f);
    game->effects.rolls.curing = 0;
    if (!ok) return false;
    say(game, c, "is unpoisoned");
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    return true;
}

/* 464d, Fire Shield: "flame type: " Hot or Cold, chosen at random for a
 * character the computer controls; any other key asks "Abort spell? ",
 * and Yes ends it. The menus' special keys count by their scan codes. Hot
 * adds 0x32 and says so; Cold adds 0x36 silently; each also 0x70. */
static bool fire_shield(cast *cx)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *c = cx->caster;
    for (;;) {
        int key;
        bool special;
        if (cok_field_u8(L, c->record, COK_FIELD_COMPUTER) != 0) {
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
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
        if (cok_field_u8(L, p->record, COK_FIELD_RACE) == 7 && (spell == 0x17 || spell == 0x31))
            saved = true;
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
    return sided(cx, cok_combat_opposite(cx->caster, cok_game_rules(cx->game)), "is Cursed");
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *t = cx->targets[1];
    if (t == NULL) return true;
    if (cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0) > 1 ||
        cok_field_u8(L, t->record, COK_FIELD_SIZE) > 1)
        return say_waiting(game, t, "is unaffected");
    uint8_t level;
    if (!level_of(game, game->effects.rolls.spell, &level)) return false;
    uint8_t value =
        (uint8_t)(level + (uint8_t)(cok_field_u8(L, cx->caster->record, COK_FIELD_HOSTILE) << 7));
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
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
        int8_t hd = cok_field_s8(L, r, COK_FIELD_HIGHEST_LEVEL);
        uint8_t weight = hd >= 0 && hd <= 1 ? 1 : hd == 2 ? 2 : hd == 3 ? 4 : hd == 4 ? 6
                         : hd == 5 && cok_field_u8(L, r, COK_FIELD_RACE) == 7 ? 10 : 20;
        if (cok_field_u8(L, r, COK_FIELD_STATUS) != 1 &&
            cok_character_find_effect(t[i], 0x35) == NULL && left >= weight)
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    uint8_t left = cok_field_u8(L, cx->caster->record, COK_FIELD_HIT_POINTS);
    game->spell_target_count = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *m = game->party.members[i];
        const uint8_t *r = m->record;
        if ((cok_field_u8_at(L, r, COK_FIELD_CREATURE_FLAGS, 0) & 0x10) == 0 ||
            cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0 ||
            cok_character_find_effect(m, 0x33) != NULL ||
            left < cok_field_u8(L, r, COK_FIELD_HIT_POINTS))
            continue;
        left = (uint8_t)(left - cok_field_u8(L, r, COK_FIELD_HIT_POINTS));
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
 * counted against the next line's first segment, as a byte: between two
 * walls as far apart as that line was long the bolt is charged nothing
 * and bounces for ever, as the original does; the run stops when a line
 * starts as one did since the bolt last hit anyone. */
static bool bolt(cast *cx, uint8_t length, uint8_t amount, uint8_t type, bool surcharge)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
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
    /* The state each line after a bounce starts from: the bolt's, the seed
     * and the combatants' hit points and conditions, on which the rest of
     * its way depends (a hit that kills frees the cell; one that does not
     * rolls a save). One met again goes round for ever. */
    typedef struct {
        int8_t x, y, sign;
        uint8_t remaining, last;
        bool surcharge;
        uint32_t seed, hash;
    } bolt_state;
    enum { BOLT_LINES = 4096 };
    static bolt_state seen[BOLT_LINES]; /* bolts do not nest */
    unsigned lines = 0;
    while (remaining > 0) {
        if (bounce) {
            bolt_state now = {game->spell_x, game->spell_y, sign, remaining, last, surcharge,
                              game->vm.seed, 2166136261u};
            for (unsigned n = 1; n <= combat->count && n <= COK_COMBATANTS; ++n) {
                const cok_character *o = combat->combatant[n].character;
                uint8_t bytes[3] = {combat->combatant[n].size, 0, 0};
                if (o != NULL)
                    bytes[1] = cok_field_u8(L, o->record, COK_FIELD_HIT_POINTS),
                    bytes[2] = cok_field_u8(L, o->record, COK_FIELD_ACTIVE);
                for (int k = 0; k < 3; ++k) now.hash = (now.hash ^ bytes[k]) * 16777619u;
            }
            for (unsigned k = 0; k < lines; ++k)
                if (seen[k].x == now.x && seen[k].y == now.y && seen[k].sign == now.sign &&
                    seen[k].remaining == now.remaining && seen[k].last == now.last &&
                    seen[k].surcharge == now.surcharge && seen[k].seed == now.seed &&
                    seen[k].hash == now.hash)
                    return undefined(game, "a bolt bounces between walls for ever, and the "
                                           "original with it (5b04:3aea)");
            if (lines == BOLT_LINES)
                return undefined(game, "a bolt bounces from 4096 walls (5b04:3aea)");
            seen[lines++] = now;
        }
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
    return hasten(cx, 0x27, cok_combat_opposite(cx->caster, cok_game_rules(cx->game)), "is Slowed");
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    uint8_t bonus = cok_field_u8(L, cx->caster->record, COK_FIELD_DEITY) == 4 ? 3 : 1;
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_character *t = cx->targets[1];
    if (t == NULL)
        return undefined(game, "the first target is read through NULL (5b04:421a)");
    if (cok_field_s8(L, t->record, COK_FIELD_HIGHEST_LEVEL) >= 6)
        return say_waiting(game, t, "smashes them flat");
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
    if (!cok_combat_place(combat, game->vm.mode, c, cok_game_rules(game), (uint8_t)game->spell_x,
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
 * list where it is not yet; false where the list would run past 30 into
 * the cone's directions (5b04:18e7). */
static bool add_line(const cok_combat *combat, cok_combat_line *line, uint8_t list[32])
{
    while (cok_combat_line_step(line)) {
        uint8_t occupant, terrain;
        cok_combat_cell(combat, (int8_t)line->x, (int8_t)line->y, &occupant, &terrain);
        if (occupant == 0) continue;
        unsigned i = 1;
        while (list[i] != occupant && list[i] != 0)
            if (++i > 30) return false;
        list[i] = occupant;
    }
    return true;
}

/* The cone's side lines from x0, y0 for width 2 or more, by side, the
 * direction of the centre line's last step that met a combatant, any byte:
 * to the end's right neighbour (DS:0da4 + side, 0dac + side), and for 3 or
 * more its left (DS:0d94 + side, 0d9c + side), each kept on the map;
 * their combatants added to list. False as add_line. */
static bool side_lines(const cok_combat *combat, int8_t x0, int8_t y0, int8_t tx, int8_t ty,
                       uint8_t width, uint8_t side, uint8_t list[32])
{
    int8_t left_x = (int8_t)(tx + (int8_t)cone_sides[side]);
    int8_t left_y = (int8_t)(ty + (int8_t)cone_sides[8 + side]);
    int8_t right_x = (int8_t)(tx + (int8_t)cone_sides[0x10 + side]);
    int8_t right_y = (int8_t)(ty + (int8_t)cone_sides[0x18 + side]);
    clamp(&left_x, &left_y);
    clamp(&right_x, &right_y);
    cok_combat_line line = {.x0 = x0, .y0 = y0, .x1 = right_x, .y1 = right_y};
    cok_combat_line_start(&line);
    if (!add_line(combat, &line, list)) return false;
    if (width <= 2) return true;
    line = (cok_combat_line){.x0 = x0, .y0 = y0, .x1 = left_x, .y1 = left_y};
    cok_combat_line_start(&line);
    return add_line(combat, &line, list);
}

/* The data segment's bytes from DS:1ed6 to 1fec, which a cone's step of
 * any byte reads as a direction (5b04:19e7): the steps' tables and the
 * terrain table after them (which share DS:1ee4-1ee7). */
int8_t cok_cast_step_byte(uint16_t at)
{
    if (at < 0x1edf) return step_x[at - 0x1ed6];
    if (at < 0x1ee4) return step_y[at - 0x1edf];
    const cok_terrain *t = &cok_combat_terrain[(at - 0x1ee4) / 4];
    const uint8_t bytes[4] = {t->cost, t->eye, t->block, t->tile};
    return (int8_t)bytes[(at - 0x1ee4) % 4];
}

/* The directions a cone aimed at its caster's own cell reads (dirs[0],
 * on), never written by 1903: for Fear, the locals that drawing the
 * caster's icon at 1415's end (6beb:0ad8, 6d21:04b0, the clipped blit's,
 * 127f:27af, 24fd or 226c by the display mode) left at that depth: the
 * screen offsets of its cell, sx and sy from the view's corner (an
 * emulator probe of 78 casts, every cell it reached, agrees; 6d21:04b0
 * computes them; the three blits' locals were compared in the emulator).
 * Only bytes 0-12 are read before Fear's length (6) is reached. A
 * footprint other than one cell is not known, nor what breath, which
 * never aims at its own cell, would leave: the run stops. */
static bool drawing_locals(cast *cx, int8_t x0, int8_t y0, uint8_t dirs[50])
{
    cok_combat *combat = &cx->game->combat;
    if (cx->breath)
        return undefined(cx->game, "a monster's breath aimed at its own cell reads directions "
                                   "432f:2337 left (5b04:19a5)");
    if (cok_combat_size(combat, cx->caster->record) != 1)
        return undefined(cx->game, "a cone aimed at the cell of a caster larger than one reads "
                                   "what drawing its icon left, which the port knows only for "
                                   "one cell (5b04:19a5)");
    int sx = x0 - combat->view_x, sy = y0 - combat->view_y;
    /* Byte counts: a unit is 4 bytes, 2 in CGA mode (127f:226c); the EGA's
     * blit (24fd) differs only in bytes 13-14, the unit x unscaled, which
     * no cone reads. */
    int unit = cx->game->display == COK_DISPLAY_CGA ? 2 : 4;
    uint16_t skip = (uint16_t)(3 * unit * (6 - sx)), width = (uint16_t)(3 * unit);
    uint16_t frame = (uint16_t)(24 * width);
    uint16_t offset = (uint16_t)(24 * 21 * unit * (sy + 1)); /* a cell row */
    uint8_t x_bytes = (uint8_t)(cx->game->display == COK_DISPLAY_EGA ? 3 * sx : 3 * unit * sx);
    const uint8_t left[16] = {0,    (uint8_t)skip, (uint8_t)(skip >> 8), (uint8_t)width, 0,
                              (uint8_t)frame, (uint8_t)(frame >> 8),
                              (uint8_t)offset, (uint8_t)(offset >> 8), 24, 0,
                              (uint8_t)(24 * sy), 0, x_bytes, 0, 0};
    memset(dirs, 0, 50);
    memcpy(dirs, left, sizeof left);
    return true;
}

/* 1903: the combatants in a cone from x0, y0 toward tx, ty, as the
 * targets, the selected character (the caster) left out. The aim is
 * pushed on, repeating the line's own steps, until the length walked (2 a
 * step, 3 a diagonal one) reaches len cells or it reaches the map's edge,
 * then kept on the map and pulled back to the first cell that blocks
 * sight (6b30:03f1) within range cells. The line to it gives the
 * combatants on its cells, nearest first, each once, though none after
 * one met a second time; for width 2 or more a line to the end's
 * neighbour on the right, by the direction of the last step that met one,
 * and for 3 or more the left, add theirs (side_lines); width 0xff those
 * within 3 of the end (6b30:08d8) instead.
 *
 * Bytes come from the stack, never set: the sight's range, and the side
 * where no one is on the line, which the caller gives (side -1 for one
 * not known: the run stops if the targets differ for any of its 256
 * values). For Fear (fear) they are what its flight left; for breath see
 * cok_cast_breath. Aimed at the caster's own cell, the directions are
 * what drawing its icon left (drawing_locals). */
static bool cone(cast *cx, int8_t x0, int8_t y0, int8_t tx, int8_t ty, uint8_t width,
                 uint8_t len, uint16_t range, int side)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
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
        tx = (int8_t)(tx + cok_cast_step_byte((uint16_t)(0x1ed6 + d)));
        ty = (int8_t)(ty + cok_cast_step_byte((uint16_t)(0x1edf + d)));
        k = k == last ? 0 : (uint8_t)(k + 1);
    }
    clamp(&tx, &ty);
    bool seen;
    if (!cok_combat_sight(combat, x0, y0, &tx, &ty, &range, &seen))
        return undefined(game, "terrain past the table is read (DS:1ee4, 6b30:03f1)");
    uint8_t list[32] = {0}, n = 1;
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
        static const char overrun[] = "a cone's list of more than 30 runs into its directions "
                                      "(5b04:18e7)";
        if (side >= 0) {
            if (!side_lines(combat, x0, y0, tx, ty, width, (uint8_t)side, list))
                return undefined(game, overrun);
        } else {
            /* No one met, the byte not known: the same targets for every
             * byte, or a stop. */
            uint8_t first[32];
            for (unsigned d = 0; d <= 0xff; ++d) {
                uint8_t each[32];
                memcpy(each, list, sizeof each);
                if (!side_lines(combat, x0, y0, tx, ty, width, (uint8_t)d, each))
                    return undefined(game, overrun);
                if (d == 0) memcpy(first, each, sizeof first);
                if (memcmp(first, each, sizeof first) != 0)
                    return unported(game, "a cone's side lines come from the low byte of "
                                          "the heap offset of the CGA mirror's mask, which its "
                                          "last Move took (127f:0b94), not known to the port, "
                                          "and which changes its "
                                          "targets (5b04:1b2a)");
            }
            memcpy(list, first, sizeof first);
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    const uint8_t *self = cx->caster->record;
    /* The cone's range and side, where no one is on its line: the return
     * offset 127f:2678's blit pushed there when the caster's terrain was
     * drawn back in its flight (0x2705, Tandy; 0x21d0 or 0x2458 in the
     * other modes: no limit in any), and the low byte of the data segment
     * that blit pushed, which depends on where DOS loaded the game (PSP +
     * 0x10 + 0x0bc6): 0xc6 for a PSP of 0xnnf0, as the emulator's (its load
     * segment 0x1000), assumed here, whose entries of the tables of
     * neighbours are the screen frame's bytes: the right 4 cells east of
     * the end, the left the end. (A probe of 88 Fear casts with no one on
     * the line found the same targets for all 256 bytes.) A caster not
     * shown after its flight leaves other bytes: the run stops. */
    if (!cx->shown)
        return undefined(game, "a cone's range and side come from the stack its caster's flight "
                               "left, the caster not shown (5b04:1a2c)");
    if (!cone(cx, cok_combat_x(combat, self), cok_combat_y(combat, self), game->spell_x,
              game->spell_y, 3, 6, 0x2705, 0xc6))
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
        cok_field_set8(L, t->record, COK_FIELD_COMPUTER, 1);
        if (cok_field_u8(L, t->record, COK_FIELD_NPC) <= 0x7f)
            cok_field_set8(L, t->record, COK_FIELD_NPC, 0xb3);
        t->combat->target = NULL;
    }
    return true;
}

/* 29de, Stinking Cloud: a cloud at the spell's cell (cok_cloud_stinking). */
static bool stinking_cloud(cast *cx)
{
    return cok_cloud_stinking(cx->game);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
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
            if (cok_field_u8(L, r, COK_FIELD_HOSTILE) !=
                    cok_field_u8(L, c->record, COK_FIELD_HOSTILE) ||
                cok_field_u8(L, r, COK_FIELD_HIT_POINTS) >=
                    cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS))
                continue;
            if (cok_field_u8(L, r, COK_FIELD_HIT_POINTS) < lowest ||
                (o == c && cok_field_u8(L, r, COK_FIELD_HIT_POINTS) <
                               cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS) >> 1)) {
                best = o;
                lowest = cok_field_u8(L, r, COK_FIELD_HIT_POINTS);
            }
        } else if (terrain == COK_COMBAT_BODY) {
            for (uint8_t b = 1; b <= combat->bodies; ++b) {
                if (b > COK_COMBAT_BODIES)
                    return undefined(game, "a body past the table's eighth is read (432f:2043)");
                const cok_combat_body *e = &combat->body[b - 1];
                if (e->character == NULL) continue;
                uint8_t status = cok_field_u8(L, e->character->record, COK_FIELD_STATUS);
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
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
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
                    int8_t hd = cok_field_s8(L, p->record, COK_FIELD_HIGHEST_LEVEL);
                    total = (uint8_t)(total + (hd == 0 || hd == 1 ? 1
                                               : hd == 2             ? 2
                                               : hd == 3             ? 4
                                                                     : 8));
                } else {
                    uint8_t size = cok_field_u8(L, p->record, COK_FIELD_SIZE);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
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
        uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
        snprintf(text, sizeof text, "%.*s: %s", stored > 15 ? 15 : stored,
                 (const char *)c + cok_field_offset_at(L, COK_FIELD_NAME, 1),
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
            if (lose) forget(L, c, spell);
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
            if (r->item == 0) forget(L, c, spell);
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
            if (r->item == 0) forget(L, c, spell);
            go = false;
        }
    }
    if (game->vm.abort) return;
    cok_arena_clear_text(game); /* 6346:196a */
    if (combat) cok_picture_fill(&game->screen, 0, 0x17 * 8, 40, 8, 0); /* 1128:07e6 */
}

/* The breath and attacks of monsters (5b04:4ab8 to 546e), the effect
 * handlers of event 0x0e. */

/* The targets as the computer's cast of spell picks them (432f:2337, AI
 * 1), for c's breath; *done whether there are any. */
static bool breath_targets(cast *cx, uint8_t spell, bool *done)
{
    cx->spell = spell;
    return combat_targets(cx, done);
}

/* The breath's uses, the effect's value, become 3 at the first round
 * (DS:714b 0). */
static void uses(cok_adventure *game, cok_effect *effect)
{
    if (game->effects.rolls.round == 0) effect->value = 3;
}

/* c turned toward the first target and drawn attacking, its panel drawn
 * and the missile (icon slot 0x12, one picture, 30 ms) flown to the first
 * target (6346:34e9, 0af6, 6beb:0ad8, 6346:1b5b, 1ba6), after text is said
 * about c (row 10, a pause). */
static bool breathe_at(cast *cx, cok_character *c, const char *text)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    cok_character *t = cx->targets[1];
    uint8_t dir;
    int8_t x, y;
    if (!say_waiting(game, c, text)) return false;
    if (t == NULL || !cok_combat_direction(combat, c, t, &dir))
        return undefined(game, "no direction holds the breath's first target, and the original "
                               "looks for ever (6346:34e9)");
    return cok_arena_panel(game, c) && cok_arena_pose(game, c, dir, 1, false) &&
           cok_arena_missile_frames(game, 0x12) && cell(game, t, &x, &y) &&
           cok_arena_missile(game, cok_combat_x(combat, c->record),
                             cok_combat_y(combat, c->record), x, y, 1, 0x1e);
}

/* Each target throws (type 3) and takes c's most hit points (+0x62), half
 * if saved; then c's uses count down and its turn ends. */
static bool breath_damage(cast *cx, cok_character *c, cok_effect *effect)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    uint8_t n = game->spell_target_count;
    for (uint8_t i = 1; i <= n && !game->vm.abort; ++i) {
        cok_character *t = cx->targets[i];
        if (t == NULL) continue;
        bool made;
        if (!cok_effects_save(&game->effects, t, 3, 0, &made)) return effect_failed(game);
        if (!cok_cast_damage(game, t, cok_field_u8(L, c->record, COK_FIELD_MAX_HIT_POINTS), 2,
                             made))
            return false;
    }
    --effect->value;
    cok_combat_end_turn(c);
    return true;
}

/* Whether every target is on the side against c (6346:2666); a target
 * taken off the list would be read through NULL. */
static bool no_friend(cast *cx, cok_character *c, bool *none)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    for (uint8_t i = 1; i <= cx->game->spell_target_count; ++i) {
        const cok_character *t = cx->targets[i];
        if (t == NULL)
            return undefined(cx->game, "a breath's target is read through NULL (5b04:52dc)");
        if (cok_field_u8(L, t->record, COK_FIELD_HOSTILE) !=
            cok_combat_opposite(c, cok_game_rules(cx->game)))
            *none = false;
    }
    return true;
}

/* What 432f:2337's pick left where the cone of 546e or 5227 reads its
 * range and side (the counts of mirrors and tiles from before it).
 *
 * The range: where its redraw of the pick (6beb:02aa) drew the icon
 * mirrored, the word the mirror left (cok_combat.mirrored: in the Tandy
 * and EGA modes the locals of 127f:0b63 or 0a51, a byte of the mask twice,
 * whose 0 cuts sight to one cell; in CGA mode the count 2); else the
 * segment 432f:11d4's far call pushed, which in DOS is where the overlay
 * manager loaded overlay 432f (GAME.OVR), or 0x005b of 6346:26e2's: either
 * far above any line's length, no limit.
 *
 * The side: where the redraw drew the cell's terrain (127f:08d7's flag of
 * a masked draw) and no mirror after, 0; where it mirrored, the low byte
 * of 127f:0b94's frame pointer (pushed by 0b63 or 0a51; in CGA mode the
 * low byte of the offset of the mirror's mask in the heap that its last
 * Move took, not known: -1), and where it drew
 * nothing, the low byte of an address of 432f:11d4's frame (pushed by
 * 124b). Those are offsets in the stack segment, the same wherever DOS
 * loads the game, and within a battle every breath is at the same depth
 * (546e at 0x16e below 3995:0172's SP): 0x16 and 0x0c above the battle's
 * stack pointer's low byte (sp), 0xea and 0xe0 for COK_BATTLE_SP. */
static void drawn_bytes(const cok_combat *combat, cok_display display, unsigned mirrors,
                        unsigned tiles, uint16_t sp, uint16_t *range, int *side)
{
    *range = 0x432f;
    *side = (uint8_t)(sp + 0x0c);
    if (combat->mirrors != mirrors) {
        *range = combat->mirrored;
        *side = display == COK_DISPLAY_CGA ? -1 : (uint8_t)(sp + 0x16);
    } else if (combat->tiles_drawn != tiles) {
        *side = 0;
    }
}

/* 4ab8, 4: d100 up to 75 to act. With uses and a d100 up to 25, the last
 * listed within a cell of c (6b30:08d8) on another side "Breathes!": it
 * throws (type 3) and takes 10, or 20 and is blinded for good (0x21, "is
 * blinded") and c's turn ends; a use goes. Else up to two energy bolts:
 * a target as the computer's spell 0x3d picks it, "Fires An Energy
 * Bolt!", the missile, 1d8 + 2, and while it can act a second 1d8 + 2;
 * one that drops gives a new pick the second; then the turn ends. The
 * damage type is what DS:6b31 holds. */
static bool attack_4(cast *cx, cok_character *c, cok_effect *effect)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    uses(game, effect);
    if (roll(game, 1, 100) > 75) return true;
    bool breathed = false;
    if (effect->value != 0 && roll(game, 1, 100) <= 25) {
        if (!cok_combat_list(combat, cok_combat_x(combat, r), cok_combat_y(combat, r), 1, 0xff, 1))
            return undefined(game, "the list of combatants around one (6b30:08d8) reads past "
                                   "its tables");
        cok_character *target = NULL;
        for (unsigned i = 2; i <= combat->listed_count; ++i) {
            cok_character *o = combat->combatant[combat->listed[i].index].character;
            if (o == NULL) return undefined(game, "a listed combatant has no record (5b04:4b77)");
            if (cok_field_u8(L, o->record, COK_FIELD_HOSTILE) !=
                cok_field_u8(L, r, COK_FIELD_HOSTILE))
                target = o;
        }
        if (target != NULL) {
            if (!say_waiting(game, c, "Breathes!")) return false;
            breathed = true;
            bool made;
            if (!cok_effects_save(&game->effects, target, 3, 0, &made)) return effect_failed(game);
            if (made) {
                if (!cok_cast_damage(game, target, 10, 0, true)) return false;
            } else {
                if (!cok_cast_damage(game, target, 20, 0, false) ||
                    !cok_cast_add_effect(game, target, 0x21, 0, 0, false, 0, false, "is blinded"))
                    return false;
                cok_combat_end_turn(c);
            }
            --effect->value;
        }
    }
    if (breathed) return true;
    bool fired = false;
    uint8_t bolts = 0;
    do {
        bool done;
        if (!breath_targets(cx, 0x3d, &done)) return false;
        cok_character *t = cx->targets[1];
        if (t == NULL || !done) {
            bolts = 2;
            continue;
        }
        ++bolts;
        int8_t x, y, x0 = cok_combat_x(combat, r), y0 = cok_combat_y(combat, r);
        if (!say_waiting(game, c, "Fires An Energy Bolt!") ||
            !cok_arena_missile_frames(game, 0x12) || !cell(game, t, &x, &y) ||
            !cok_arena_missile(game, x0, y0, x, y, 1, 0x1e) ||
            !cok_cast_damage(game, t, (uint8_t)(damage_roll(game, 1, 8) + 2), 0, false))
            return false;
        t = cx->targets[1];
        if (t == NULL) return undefined(game, "the first target is read through NULL (5b04:4d46)");
        if (cok_field_u8(L, t->record, COK_FIELD_ACTIVE) != 0 && bolts == 1) {
            if (!cok_cast_damage(game, t, (uint8_t)(damage_roll(game, 1, 8) + 2), 0, false))
                return false;
            ++bolts;
        }
        fired = true;
    } while (bolts < 2 && !game->vm.abort);
    if (fired) cok_combat_end_turn(c);
    return true;
}

/* 4dc2, 0x4e: a target as the computer's Fireball picks it (everyone
 * within 3 of its cell): the spell becomes Fireball (DS:6b33, left so), the
 * screen centred on c, "throws a fireball", the missile, and Fireball's
 * effect applied (5b04:1071) with 15d6 of fire and magic; 0x4e goes. */
static bool attack_4e(cast *cx, cok_character *c)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    game->effects.rolls.images = 1; /* 4dc8; the pick clears it (432f:2349), its area sets it */
    bool done;
    if (!breath_targets(cx, 0x2f, &done)) return false;
    if (!done) return true;
    game->effects.rolls.spell = 0x2f;
    cok_character *t = cx->targets[1];
    int8_t x, y, x0 = cok_combat_x(combat, c->record), y0 = cok_combat_y(combat, c->record);
    if (!cok_arena_centre(game, x0, y0, 0, 8) || !say_waiting(game, c, "throws a fireball") ||
        !cok_arena_missile_frames(game, 0x12) || !cell(game, t, &x, &y) ||
        !cok_arena_missile(game, x0, y0, x, y, 1, 0x1e) ||
        !apply(cx, 0, false, damage_roll(game, 15, 6), 9, ""))
        return false;
    return remove_effect(game, c, NULL, 0x4e);
}

/* 4eea, 0x54: with uses, a target as the computer's spell 0x24 picks it,
 * then a cone from c, a burst of 3 at its end, length 3; with anyone in it
 * (either side), the damage type 0x20, "breathes poison", the targets
 * take c's most hit points (breath_damage). The cone's range is the bytes
 * of the line the target routine last looked along at the picked cell
 * (6b30:01a5, its y's high byte, 0, and its x step's low byte): one
 * looking up or down the column cuts sight to one cell. */
static bool attack_54(cast *cx, cok_character *c, cok_effect *effect)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    uses(game, effect);
    if (effect->value == 0) return true;
    int8_t x0 = cok_combat_x(combat, c->record), y0 = cok_combat_y(combat, c->record);
    bool done;
    if (!breath_targets(cx, 0x24, &done)) return false;
    if (!done) return true;
    uint16_t range = (uint16_t)((uint8_t)combat->sight_step << 8);
    if (!cone(cx, x0, y0, game->spell_x, game->spell_y, 0xff, 3, range, -1)) return false;
    if (game->spell_target_count == 0) return true;
    game->effects.rolls.damage_type = 0x20;
    return breathe_at(cx, c, "breathes poison") && breath_damage(cx, c, effect);
}

/* 50cf, 0x55: with uses, the damage type 0x24; a target as the computer's
 * Lightning Bolt picks it; "Breathes!", c turned and drawn, the bolt's
 * missile (icon slot 0x13, four pictures, 50 ms) from c to the spell's
 * cell, then a bolt of c's most hit points (type 3 saves): the cell (5b04:
 * 39bb) and the rest, length 10 (3a6f); a use goes, the turn ends. */
static bool attack_55(cast *cx, cok_character *c, cok_effect *effect)
{
    const cok_character_layout *L = cok_game_rules(cx->game)->character;
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    uses(game, effect);
    if (effect->value == 0) return true;
    game->effects.rolls.damage_type = 0x24;
    int8_t x0 = cok_combat_x(combat, c->record), y0 = cok_combat_y(combat, c->record);
    bool done;
    if (!breath_targets(cx, 0x33, &done)) return false;
    if (!done) return true;
    cok_character *t = cx->targets[1];
    uint8_t dir;
    if (!say_waiting(game, c, "Breathes!")) return false;
    if (t == NULL || !cok_combat_direction(combat, c, t, &dir))
        return undefined(game, "no direction holds the breath's first target, and the original "
                               "looks for ever (6346:34e9)");
    uint8_t amount = cok_field_u8(L, c->record, COK_FIELD_MAX_HIT_POINTS);
    /* 39bb's flag is a local of 50cf never set: it reads it only on a wall
     * in a 3D area, to write it back to the same local, never read. */
    bool bounce = false;
    if (!cok_arena_panel(game, c) || !cok_arena_pose(game, c, dir, 1, false) ||
        !cok_arena_missile_frames(game, 0x13) ||
        !cok_arena_missile(game, x0, y0, game->spell_x, game->spell_y, 4, 0x32) ||
        !bolt_cell(cx, game->spell_x, game->spell_y, amount, 3, 0, &bounce) ||
        !bolt(cx, 10, amount, 3, false))
        return false;
    --effect->value;
    cok_combat_end_turn(c);
    return true;
}

/* 5227, 0x5a: with uses, the damage type 0x30; a target as the computer's
 * spell 0x3d picks it, then a cone from c, a line of 6; unless a target is
 * on c's side, "breathes acid" and the targets take c's most hit points
 * (breath_damage). */
static bool attack_5a(cast *cx, cok_character *c, cok_effect *effect)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    uses(game, effect);
    if (effect->value == 0) return true;
    game->effects.rolls.damage_type = 0x30;
    int8_t x0 = cok_combat_x(combat, c->record), y0 = cok_combat_y(combat, c->record);
    unsigned mirrors = combat->mirrors, tiles = combat->tiles_drawn;
    bool done;
    if (!breath_targets(cx, 0x3d, &done)) return false;
    uint16_t range;
    int side;
    drawn_bytes(combat, game->display, mirrors, tiles, game->battle_sp, &range, &side);
    if (done && !cone(cx, x0, y0, game->spell_x, game->spell_y, 1, 6, range, side)) return false;
    if (!no_friend(cx, c, &done)) return false;
    if (!done || game->spell_target_count == 0) return true;
    return breathe_at(cx, c, "breathes acid") && breath_damage(cx, c, effect);
}

/* 546e, 6, 0x53 and 0x68: the uses become 1 for 6, else 3, at the first
 * round or above 3; with uses, the damage type 0x22 and a cone of width 3
 * and length 8 (6) or 7 (0x53), "cold", or 0x21, 9, "Fire" (0x68); a
 * target as the computer's spell 0x3d picks it, then the cone from c;
 * unless a target is on c's side, "breathes cold" or "breathes Fire" and
 * the targets take c's most hit points (breath_damage). */
static bool attack_dragon(cast *cx, cok_character *c, cok_effect *effect)
{
    cok_adventure *game = cx->game;
    cok_combat *combat = &game->combat;
    uint8_t id = effect->id;
    if (game->effects.rolls.round == 0 || effect->value > 3) effect->value = id == 6 ? 1 : 3;
    if (effect->value == 0) return true;
    uint8_t len = 9;
    const char *what = "breathes Fire";
    if (id == 6 || id == 0x53) {
        game->effects.rolls.damage_type = 0x22;
        len = id == 6 ? 8 : 7;
        what = "breathes cold";
    } else {
        game->effects.rolls.damage_type = 0x21;
    }
    int8_t x0 = cok_combat_x(combat, c->record), y0 = cok_combat_y(combat, c->record);
    unsigned mirrors = combat->mirrors, tiles = combat->tiles_drawn;
    bool done;
    if (!breath_targets(cx, 0x3d, &done)) return false;
    if (!done) return true;
    uint16_t range;
    int side;
    drawn_bytes(combat, game->display, mirrors, tiles, game->battle_sp, &range, &side);
    if (!cone(cx, x0, y0, game->spell_x, game->spell_y, 3, len, range, side) ||
        !no_friend(cx, c, &done))
        return false;
    if (game->spell_target_count == 0 || !done) return true;
    return breathe_at(cx, c, what) && breath_damage(cx, c, effect);
}

bool cok_cast_dispel(cok_adventure *game)
{
    cast cx = {.game = game, .caster = member_of(game, game->vm.character), .ai = true,
               .spell = game->effects.rolls.spell, .targets = game->spell_targets};
    return dispel_magic(&cx);
}

bool cok_cast_breath(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    cast cx = {.game = game, .caster = member_of(game, game->vm.character), .ai = true,
               .breath = true, .targets = game->spell_targets};
    if (cx.caster == NULL)
        return undefined(game, "a monster breathes with no character selected, read through NULL "
                               "(432f:2337)");
    switch (effect->id) {
    case 4: return attack_4(&cx, c, effect);
    case 0x4e: return attack_4e(&cx, c);
    case 0x54: return attack_54(&cx, c, effect);
    case 0x55: return attack_55(&cx, c, effect);
    case 0x5a: return attack_5a(&cx, c, effect);
    case 6: case 0x53: case 0x68: return attack_dragon(&cx, c, effect);
    default: break;
    }
    return undefined(game, "an effect with no breath runs a breath's handler");
}
