#include "attack.h"

#include "arena.h"
#include "camp.h"
#include "items.h"
#include "round.h"
#include "treasure.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* DS:1ed6 and DS:1edf: a step in direction 0-7, north first, clockwise; 8
 * for none. */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};

/* DS:1e54-1e61: the sound driver's commands for a thrown missile, a hit,
 * a miss or a missile, a step and an arrow, as words. */
static const uint8_t sound_words[14] = {6, 0, 7, 0, 8, 0, 9, 0, 0x0a, 0, 0x0b, 0, 0x0c, 0};
enum {
    SOUND_THROWN = 6,  /* DS:1e54 */
    SOUND_HIT = 7,     /* DS:1e56 */
    SOUND_MISS = 9,    /* DS:1e5a */
    SOUND_STEP = 0x0a, /* DS:1e5c */
    SOUND_ARROW = 0x0c /* DS:1e60 */
};

const cok_ds_table cok_attack_tables[] = {
    {0x1ed6, sizeof step_x, 1, (const uint8_t *)step_x},
    {0x1edf, sizeof step_y, 1, (const uint8_t *)step_y},
    {0x1e54, sizeof sound_words, 1, sound_words},
};
const size_t cok_attack_table_count = sizeof cok_attack_tables / sizeof *cok_attack_tables;

static int8_t s8(int value)
{
    return (int8_t)(uint8_t)value;
}

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

static void name_of(const uint8_t *r, char out[16])
{
    size_t length = r[0] > 15 ? 15 : r[0];
    memcpy(out, r + 1, length);
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
    if (c->combat == NULL) {
        char name[16];
        name_of(c->record, name);
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

/* c's combatant, which the original reads or writes as entry 0, the count,
 * for a record that is not one. */
static bool index_of(cok_adventure *game, const cok_character *c, uint8_t *n, const char *where)
{
    *n = cok_combat_index(&game->combat, c->record);
    if (*n != 0) return true;
    char name[16];
    name_of(c->record, name);
    return undefined(game, "%s is not a combatant: its entry is the count (6beb:0c43, %s)", name,
                     where);
}

static bool stats(cok_adventure *game, cok_character *c)
{
    char error[300];
    if (cok_character_stats(c, &game->item_types, error, sizeof error)) return true;
    return undefined(game, "%s", error);
}

static bool direction(cok_adventure *game, const cok_character *a, const cok_character *b,
                      uint8_t *d)
{
    if (cok_combat_direction(&game->combat, a, b, d)) return true;
    return undefined(game, "no direction's wedge holds the other (6346:34e9), where the original "
                           "loops for ever");
}

static bool distance(cok_adventure *game, const cok_character *origin, const cok_character *target,
                     uint8_t *d)
{
    if (cok_combat_distance(&game->combat, origin->record, target->record, d)) return true;
    return undefined(game, "the distance (6346:2888) lists the combatants around one "
                           "(6b30:08d8) past its tables");
}

static bool visible(cok_adventure *game, const cok_character *c, bool all, bool *shown)
{
    if (cok_combat_visible(&game->combat, c->record, all, shown)) return true;
    return undefined(game, "a size past the footprints is read after them (6beb:06ef)");
}

/* The readied weapon's item type (slot 0, +0x147), or NULL for none. */
static bool weapon_type(cok_adventure *game, const cok_character *c, const uint8_t **type)
{
    *type = NULL;
    size_t weapon = c->slots[0];
    if (weapon == 0 || weapon > c->item_count) return true;
    uint8_t t = c->items[weapon - 1][0x2e];
    if (t >= COK_ITEM_TYPES)
        return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
    *type = game->item_types.type[t];
    return true;
}

static bool missile_weapon(cok_adventure *game, const cok_character *c, bool *missile)
{
    if (cok_combat_missile_weapon(&game->item_types, c, missile)) return true;
    return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
}

/* What 6beb:0e08's stale byte can read outside the bodies (see arena.h):
 * the exploding list's entries 1, 8 and 15. The spell targets are not
 * held. */
static cok_arena_reachable reachable(const cok_adventure *game)
{
    const cok_combat *combat = &game->combat;
    cok_arena_reachable also = {{combat->exploding[1], combat->exploding[8],
                                 combat->exploding[15]},
                                {NULL}};
    return also;
}

/* 60f4:1440 for c, and the byte it leaves at [bp-3], which its callers'
 * 6beb:0e08 then reads (see arena.h): the low byte of the segment of the
 * effect 0x4d node 6346:2447 found, NULL when none, which depends on the
 * heap. */
static bool battle_only(cok_adventure *game, cok_character *c, int *stale)
{
    if (!cok_combat_battle_only(game, c)) return false;
    *stale = cok_character_find_effect(c, 0x4d) != NULL ? COK_ARENA_STALE_UNKNOWN : 0;
    return true;
}

static bool kill(cok_adventure *game, cok_character *c, int stale)
{
    cok_arena_reachable also = reachable(game);
    return cok_arena_kill(game, c, stale, &also);
}

static void wait_speed(cok_adventure *game)
{
    cok_adventure_wait(game, game->speed * 100u); /* 1521:0b4b */
}

/* The text's window is full: wait for a key (1614:025b), as PRINT pages. */
static void page(void *context)
{
    cok_keyboard keys = cok_adventure_keyboard(context);
    keys.read(keys.context);
}

/* 6346:161b: forget the first memorized byte equal to spell. */
static void forget(uint8_t *c, uint8_t spell)
{
    for (size_t i = 0; i <= 0x39; ++i)
        if (c[0x1e + i] == spell) {
            c[0x1e + i] = 0;
            return;
        }
}

/* The roll and its helpers. */

bool cok_combat_reveal(cok_adventure *game, cok_character *c)
{
    for (cok_effect *e; (e = cok_character_find_effect(c, 0x19)) != NULL;)
        if (!cok_effects_remove(&game->effects, c, e, 0x19))
            return cok_adventure_effect_failed(game);
    return true;
}

bool cok_combat_hit(cok_adventure *game, cok_character *attacker, cok_character *target,
                    uint8_t ac, bool *hit)
{
    cok_rolls *rolls = &game->effects.rolls;
    *hit = false;
    if (!cok_combat_reveal(game, attacker)) return false;
    rolls->attack_roll = roll(game, 1, 20);
    if ((int8_t)rolls->attack_roll <= 1) return true;
    if (rolls->attack_roll == 20) rolls->attack_roll = 100;
    if (!event(game, attacker, 0x0a) || !event(game, target, 0x10)) return false;
    /* Var 0x7f71 for the party's side, else 0x7f70, as a signed byte; the
     * party's then meets a case on the constant 500 (60f4:10c8) that never
     * matches. */
    uint16_t var = game->vm.mem7c00[attacker->record[0x18a] == 0 ? 0x371 : 0x370];
    int8_t bonus = s8(var);
    if ((int8_t)rolls->attack_roll < 0) return true;
    *hit = (int8_t)rolls->attack_roll + attacker->record[0x18c] + bonus >= ac;
    return true;
}

bool cok_combat_book(cok_adventure *game, cok_character *attacker, cok_character *defender)
{
    cok_combat_record *cr = record_of(game, defender);
    if (cr == NULL) return false;
    ++cr->hits;
    uint8_t d;
    if (!direction(game, defender, attacker, &d)) return false;
    /* An idiv: the remainder is negative for a facing past 8. */
    uint8_t turn = (uint8_t)(((int)d - cr->facing + 8) % 8);
    if (turn > 4) turn = (uint8_t)(8 - turn);
    cr->turning = (uint8_t)((cr->turning + turn) % 8);
    return true;
}

bool cok_combat_backstab(cok_adventure *game, cok_character *attacker, cok_character *target,
                         bool *backstab)
{
    const uint8_t *a = attacker->record;
    *backstab = false;
    bool thief = (int8_t)a[0xff] > 0 || ((int8_t)a[0x107] > 0 && cok_character_former_class(a));
    if (!thief) return true;
    size_t weapon = attacker->slots[0];
    if (weapon != 0) {
        uint8_t t = attacker->items[weapon - 1][0x2e];
        if (t != 0x43 && t != 3 && t != 4 && !(t > 0x10 && t < 0x14)) return true;
    }
    cok_combat_record *cr = record_of(game, target);
    if (cr == NULL) return false;
    if (cr->hits <= 1) return true;
    uint8_t d;
    if (!distance(game, attacker, target, &d)) return false;
    if (d >= 2 || (target->record[0xcf] & 0x7f) > 1) return true;
    if (!direction(game, attacker, target, &d)) return false;
    *backstab = d == cr->facing;
    return true;
}

bool cok_combat_range(cok_adventure *game, cok_character *attacker, cok_character *target,
                      uint8_t *ac)
{
    uint8_t d, step;
    bool missile;
    if (!distance(game, attacker, target, &d) || !missile_weapon(game, attacker, &missile))
        return false;
    if (missile) {
        const uint8_t *type;
        if (!weapon_type(game, attacker, &type)) return false;
        step = (uint8_t)((type[12] - 1) / 3);
    } else {
        step = d;
    }
    /* Medium range takes 2, long range 3 more, as signed bytes. */
    if ((int8_t)d - (int8_t)step > 0) {
        d = (uint8_t)((int8_t)d - (int8_t)step);
        *ac = (uint8_t)(*ac + 2);
    }
    if ((int8_t)d - (int8_t)step > 0) *ac = (uint8_t)(*ac + 3);
    return true;
}

bool cok_combat_damage_roll(cok_adventure *game, cok_character *attacker, cok_character *target,
                            uint8_t slot)
{
    cok_rolls *rolls = &game->effects.rolls;
    const uint8_t *a = attacker->record;
    if (slot > 2) return undefined(game, "attack slot %u reads past the two (432f:01ba)", slot);
    uint8_t dice = cok_dice_count(&game->vm.seed, a[0x190 + slot], a[0x192 + slot], &rolls->dice);
    /* The test for a negative total compares unsigned and never holds. */
    rolls->amount = (uint8_t)(dice + (int8_t)a[0x194 + slot]);
    if (!event(game, attacker, 4) || !event(game, target, 5)) return false;
    bool backstab;
    if (!cok_combat_backstab(game, attacker, target, &backstab)) return false;
    if (backstab) {
        /* 2 + (thief levels - 1) / 4 as words, the former ones counted when
         * a human may use them (66c2:0efb); the product a byte. */
        uint16_t former = cok_character_former_class(a) ? 1 : 0;
        uint16_t levels = (uint16_t)((uint16_t)(int16_t)(int8_t)a[0x107] * former +
                                     (uint16_t)(int16_t)(int8_t)a[0xff]);
        uint16_t times = (uint16_t)((uint16_t)(levels - 1) >> 2) + 2u;
        rolls->amount = (uint8_t)(rolls->amount * times);
    }
    rolls->damage_type = 0;
    return true;
}

/* 432f:033e: say attacker's attack on target: its name and "Attacks",
 * "-Backstabs-" (mode 2) or "slays helpless" (mode 3) from row 10, the
 * target's name on row 12 and below it "(from behind) " (mode 1) and
 * "Hitting for N points of damage", dealt to it (6346:24d7), "with one
 * cruel blow" (mode 3) or "and Misses", unless it is gone (status 8); a
 * target dealt any damage may not cast, and one casting "lost a spell"
 * and its turn; after a pause, one that dropped "goes down", "and is
 * Dying" or "is killed", loses its battle's effects (60f4:1440), runs
 * event 0x0d and, unless exploding (10), dies on the screen
 * (6beb:0e08). */
static bool report(cok_adventure *game, cok_character *attacker, cok_character *target,
                   uint8_t mode, uint8_t dealt, uint8_t shown, bool hit)
{
    uint8_t *t = target->record;
    const char *header = mode == 2 ? "-Backstabs-" : mode == 3 ? "slays helpless" : "Attacks";
    if (!cok_arena_say(game, attacker, header, 10, false)) return false;
    uint8_t row = 12;
    cok_item_draw_name(game, t, 0x17, row, false); /* 6346:199d */
    ++row;
    char text[96];
    snprintf(text, sizeof text, "%s", mode == 1 ? "(from behind) " : "");
    if (hit) {
        size_t used = strlen(text);
        if (mode == 3)
            snprintf(text, sizeof text, "with one cruel blow");
        else
            snprintf(text + used, sizeof text - used, "Hitting for %u%s", shown,
                     shown == 1 ? " point of damage" : " points of damage");
        if (!cok_combat_damage(&game->combat, target, dealt, 5))
            return undefined(game, "a record on a side other than 0 or 1 (+0x18a) is counted "
                                   "past DS:6b2d (6346:24d7)");
    } else {
        strcat(text, "and Misses");
    }
    if (t[0x188] != 8) {
        char name[16], line[128];
        name_of(t, name);
        snprintf(line, sizeof line, "%s %s", name, text);
        cok_adventure_log(game, "attack", line);
        cok_text_hooks hooks = {page, NULL, game};
        cok_text_wrap(&game->screen, &game->font, &game->vm.cursor, text,
                      (cok_text_window){0x17, row, 0x26, row + 3}, 10, 0, true, &hooks);
    }
    row = (uint8_t)(game->vm.cursor.y + 1); /* DS:6135 */
    cok_combat_record *cr = record_of(game, target);
    if (cr == NULL) return false;
    if (dealt > 0) {
        cr->may_cast = 0;
        wait_speed(game);
        if (cr->spell > 0) {
            if (!cok_arena_say(game, target, "lost a spell", 12, true)) return false;
            forget(t, cr->spell);
            cok_combat_end_turn(target);
        }
    } else {
        wait_speed(game);
    }
    if (t[0x189] == 0) {
        if (!cok_arena_say(game, target, "goes down", row, false)) return false;
        row = (uint8_t)(row + 2);
        if (t[0x188] == 5) {
            cok_adventure_log(game, "attack", "and is Dying");
            cok_text_string(&game->screen, &game->font, "and is Dying", 0x17, row, 10, 0);
        }
        if (t[0x188] >= 6 && t[0x188] <= 8 && !cok_arena_say(game, target, "is killed", row, false))
            return false;
        int stale;
        if (!battle_only(game, target, &stale) || !event(game, target, 0x0d)) return false;
        if (t[0x189] == 0 && t[0x188] != 10) {
            if (!kill(game, target, stale)) return false;
        } else {
            wait_speed(game);
        }
    }
    cok_arena_clear_text(game);
    return true;
}

/* An attack. */

bool cok_combat_strike(cok_adventure *game, cok_character *attacker, cok_character *target,
                       bool behind, bool *over)
{
    cok_combat *combat = &game->combat;
    cok_rolls *rolls = &game->effects.rolls;
    uint8_t *a = attacker->record, *t = target->record;
    cok_combat_record *ca = record_of(game, attacker);
    if (ca == NULL) return false;
    *over = false;
    rolls->hits[0] = rolls->hits[1] = 0;
    combat->swings[1] = combat->swings[2] = 0;
    bool any = false, down = false;
    rolls->amount = 0;
    ca->attacked = 1;
    if (cok_combat_helpless(target)) {
        cok_arena_sound(game, SOUND_HIT);
        /* From the slot in progress down to one with attacks left, past
         * slot 1 to 0, +0x18e, the armour class from behind. */
        for (;;) {
            if (0x18e + ca->attack_slot >= COK_CHARACTER_SIZE)
                return undefined(game, "a helpless target's blow reads attacks past the record "
                                       "(432f:15d5)");
            if (a[0x18e + ca->attack_slot] != 0) break;
            --ca->attack_slot;
        }
        if (ca->attack_slot > 2)
            return undefined(game, "attack slot %u counts its attacks past DS:7196 (432f:160e)",
                             ca->attack_slot);
        ++combat->swings[ca->attack_slot];
        if (!report(game, attacker, target, 3, (uint8_t)(t[0x197] + 5), 1, true) ||
            !cok_combat_reveal(game, attacker))
            return false;
        a[0x18f] = a[0x190] = 0;
        *over = true;
    } else {
        size_t weapon = attacker->slots[0];
        if (weapon != 0 && (t[0xcf] > 0x80 || (t[0xcf] & 7) > 1)) {
            /* Against a large target the weapon's dice for large ones
             * (bytes 2-4) replace slot 1's, the small ones' bonus (byte 11)
             * taken off. */
            const uint8_t *type;
            if (!weapon_type(game, attacker, &type)) return false;
            a[0x191] = type[2];
            a[0x193] = type[3];
            a[0x195] = (uint8_t)((int8_t)a[0x195] - (int8_t)type[11] + (int8_t)type[4]);
        }
        if (!stats(game, target) || !event(game, target, 0x0b)) return false;
        bool backstab;
        uint8_t ac;
        if (!cok_combat_backstab(game, attacker, target, &backstab)) return false;
        if (backstab) {
            ac = (uint8_t)(t[0x18e] - 4);
        } else {
            cok_combat_record *ct = record_of(game, target);
            if (ct == NULL) return false;
            if (ct->hits > 1) {
                uint8_t d;
                if (!direction(game, attacker, target, &d)) return false;
                if (d == ct->facing && ct->turning > 4) behind = true;
            }
            ac = t[0x18d + (behind ? 1 : 0)];
        }
        if (!cok_combat_range(game, attacker, target, &ac)) return false;
        uint8_t mode = behind ? 1 : 0;
        if (!cok_combat_backstab(game, attacker, target, &backstab)) return false;
        if (backstab) mode = 2;
        uint8_t slot = ca->attack_slot;
        for (; slot >= 1; --slot) {
            if (slot > 2)
                return undefined(game, "attack slot %u reads past the two (432f:17f2)", slot);
            while (a[0x18e + slot] > 0 && !down) {
                --a[0x18e + slot];
                ca->attack_slot = slot;
                ++combat->swings[slot];
                bool hit;
                if (!cok_combat_hit(game, attacker, target, ac, &hit)) return false;
                if (hit || cok_combat_helpless(target)) {
                    ++rolls->hits[slot - 1]; /* DS:6b3b + slot */
                    cok_arena_sound(game, SOUND_HIT);
                    any = true;
                    if (!cok_combat_damage_roll(game, attacker, target, slot)) return false;
                    if (t[0x189] != 0 &&
                        !report(game, attacker, target, mode, rolls->amount, rolls->amount, true))
                        return false;
                    if (t[0x189] != 0 && rolls->amount > 0 &&
                        !event(game, attacker, (uint8_t)(slot + 1)))
                        return false;
                    /* Only after a hit: a target that dropped otherwise
                     * (an explosion, an effect at the roll) is swung at
                     * until one lands. An attacker that drops loses this
                     * slot's attacks, not the next one's. */
                    if (t[0x189] == 0) down = true;
                    if (a[0x189] == 0) a[0x18e + slot] = 0;
                }
                if (!cok_combat_explode(game)) return false;
            }
        }
        if (!any) {
            cok_arena_sound(game, SOUND_MISS);
            if (!report(game, attacker, target, mode, 0, 0, false)) return false;
        }
        *over = a[0x18f] == 0 && a[0x190] == 0;
        ca->sweeps = 0;
    }
    if (a[0x189] == 0) *over = true;
    if (*over) cok_combat_end_turn(attacker);
    return true;
}

/* 432f:2beb: item flies from attacker to target (6346:1ba6), after sound
 * 0x0c: darts, javelins, quarrels and arrows (types 5, 7, 0x0c, 0x1e) as
 * one picture of COMSPR's arrows by the direction, ready or attacking and
 * mirrored (slots 13-15), sound 0x0c again, a step at a time, 10 ms
 * apart; hand axes, clubs and hammers (2, 3, 6) spinning (slot 0x10's four
 * pictures), sound 9, and 0x36 and 0x37 as slot 0x11's, sound 6, 50 ms
 * apart; sling stones (0x1c, 0x1d, 0x43) as slot 0x15's two, 10 ms apart,
 * sound 6; anything else as slot 0x14's two, 20 ms apart, sound 9. */
static bool missile(cok_adventure *game, cok_character *attacker, cok_character *target,
                    const uint8_t *item)
{
    cok_arena_sound(game, SOUND_ARROW);
    uint8_t d;
    if (!direction(game, attacker, target, &d)) return false;
    uint8_t frames = 1, delay = 10, type = item[0x2e];
    bool ok;
    if (type == 5 || type == 7 || type == 0x0c || type == 0x1e) {
        if ((d & 1) != 0) {
            if (d == 3 || d == 5)
                ok = cok_arena_missile_frame(game, 0x0e, 1, 0, d == 5);
            else
                ok = cok_arena_missile_frame(game, 0x0e, 0, 0, d == 7);
        } else {
            ok = cok_arena_missile_frame(game, (uint8_t)(0x0d + d % 4), (uint8_t)(d >> 2), 0,
                                         false);
        }
        cok_arena_sound(game, SOUND_ARROW);
    } else if (type == 2 || type == 3 || type == 6) {
        ok = cok_arena_missile_frames(game, 0x10);
        frames = 4;
        delay = 0x32;
        cok_arena_sound(game, SOUND_MISS);
    } else if (type == 0x36 || type == 0x37) {
        ok = cok_arena_missile_frames(game, 0x11);
        frames = 4;
        delay = 0x32;
        cok_arena_sound(game, SOUND_THROWN);
    } else {
        uint8_t slot = type == 0x1c || type == 0x1d || type == 0x43 ? 0x15 : 0x14;
        ok = cok_arena_missile_frame(game, slot, 0, 0, false) &&
             cok_arena_missile_frame(game, slot, 1, 1, false);
        frames = 2;
        delay = slot == 0x15 ? 10 : 0x14;
        cok_arena_sound(game, slot == 0x15 ? SOUND_THROWN : SOUND_MISS);
    }
    if (!ok) return false;
    const cok_combat *combat = &game->combat;
    return cok_arena_missile(game, cok_combat_x(combat, attacker->record),
                             cok_combat_y(combat, attacker->record),
                             cok_combat_x(combat, target->record),
                             cok_combat_y(combat, target->record), frames, delay);
}

/* The end of 432f:1a45 for the item it was given (1 + its index, the
 * attacker's): unless the weapon is a hoopak, its count less the attacks
 * made with slot 1, a byte; at 0 it goes, a thrown weapon (but a
 * spiritual hammer) into the pool after the missile recovered or at the
 * end, unreadied, as the new missile. */
static bool use_up(cok_adventure *game, cok_character *attacker, size_t item)
{
    if (cok_combat_hoopak(attacker)) return true;
    uint8_t *it = attacker->items[item - 1];
    if (it[0x39] > 0) it[0x39] = (uint8_t)(it[0x39] - game->combat.swings[1]);
    if (it[0x39] != 0) return true;
    bool thrown;
    if (!cok_combat_thrown(&game->item_types, attacker, &thrown))
        return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
    if (thrown && it[0x3d] != 0x17) {
        cok_pool *pool = &game->pool;
        size_t at = 0;
        if (pool->item_count > 0) {
            /* From the head, until the last or the missile recovered. */
            size_t i = 0;
            while (i + 1 < pool->item_count && i + 1 != pool->missile) ++i;
            at = i + 1;
        }
        uint8_t copy[COK_ITEM_SIZE];
        memcpy(copy, it, sizeof copy);
        copy[0x34] = 0;
        if (!cok_pool_insert(pool, at, copy)) return undefined(game, "out of memory");
        pool->missile = at + 1;
    }
    cok_character_remove_item(attacker, item - 1); /* 6346:1697 */
    return true;
}

/* The same end for an item an effect took during the strike (0x43 takes
 * the striking item, 3f44:1dc5), which the original has unlinked and freed
 * (6346:1697) and goes on using: its bytes stay as they were, but the
 * first eight, which the heap's free list takes, and nothing is allocated
 * meanwhile. Its count less the attacks made with slot 1; at 0 6346:1697,
 * not finding it among the attacker's items, says "Tried to Lose item &
 * couldn't find it!" (6346:1670, 1521:096c: row 24, yellow, a key). The
 * tests of a hoopak (6346:3086) and a thrown weapon (30bd) read the
 * readied weapon, which 0x43 has just cleared (3f44:1f7a): neither holds,
 * so the copy of the freed node into the pool (refused, its first eight
 * bytes the heap's) is never reached. */
static bool use_up_freed(cok_adventure *game, cok_character *attacker, uint8_t *it)
{
    if (cok_combat_hoopak(attacker)) return true;
    if (it[0x39] > 0) it[0x39] = (uint8_t)(it[0x39] - game->combat.swings[1]);
    if (it[0x39] != 0) return true;
    bool thrown;
    if (!cok_combat_thrown(&game->item_types, attacker, &thrown))
        return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
    if (thrown && it[0x3d] != 0x17)
        return undefined(game, "a freed item's copy goes into the pool with the heap's free list "
                               "in its first bytes (432f:1c8e)");
    cok_adventure_alert(game, "Tried to Lose item & couldn't find it!", 14);
    return true;
}

bool cok_combat_attack(cok_adventure *game, cok_character *attacker, cok_character *target,
                       bool behind, size_t item, bool *over)
{
    cok_combat *combat = &game->combat;
    uint8_t *a = attacker->record;
    cok_combat_record *ca = record_of(game, attacker), *ct = record_of(game, target);
    if (ca == NULL || ct == NULL) return false;
    if (item > attacker->item_count)
        return undefined(game, "the attack's item is not the attacker's (432f:1a45)");
    combat->show_actions = true;
    combat->panel = true;
    combat->round_limit = (uint8_t)(game->effects.rolls.round + 15);
    /* The target turns away from the attacker, or, if shown, faces it as
     * it is drawn; if shown and already hit twice, it flips and is drawn
     * back. */
    uint8_t d = 0;
    bool shown;
    if (ct->hits < 2 && !behind) {
        if (!direction(game, target, attacker, &d)) return false;
        ct->facing = (uint8_t)((d + 4) % 8);
    } else {
        if (!visible(game, target, false, &shown)) return false;
        if (shown) {
            d = ct->facing;
            if (!behind) ct->facing = (uint8_t)((d + 4) % 8);
        }
    }
    if (!visible(game, target, false, &shown)) return false;
    if (shown && !cok_arena_pose(game, target, d, 0, false)) return false;
    if (!direction(game, attacker, target, &d) || !cok_arena_panel(game, attacker) ||
        !cok_arena_pose(game, attacker, d, 1, false))
        return false;
    ca->target = target->record;
    cok_adventure_wait(game, 100); /* 1962:029c */
    /* The item is held across the strike: one that goes meanwhile, as an
     * effect can take it, the original goes on using freed, as its bytes
     * were. */
    size_t held = attacker->held;
    attacker->held = item;
    uint8_t freed[COK_ITEM_SIZE] = {0};
    if (item != 0) memcpy(freed, attacker->items[item - 1], sizeof freed);
    bool ok = item == 0 || missile(game, attacker, target, attacker->items[item - 1]);
    size_t weapon = attacker->slots[0];
    if (ok && weapon != 0 && (attacker->items[weapon - 1][0x2e] == 0x1c ||
                              attacker->items[weapon - 1][0x2e] == 0x1d))
        ok = missile(game, attacker, target, attacker->items[weapon - 1]);
    *over = true;
    if (ok && (a[0x18f] > 0 || a[0x190] > 0)) {
        uint8_t *selected = game->vm.character;
        game->vm.character = a;
        ok = cok_combat_strike(game, attacker, target, behind, over);
        if (ok && item != 0) {
            if (attacker->held == 0)
                ok = use_up_freed(game, attacker, freed);
            else
                ok = use_up(game, attacker, attacker->held);
        }
        ok = ok && stats(game, attacker);
        if (ok) game->vm.character = selected;
    }
    attacker->held = held;
    if (!ok) return false;
    if (*over) cok_combat_end_turn(attacker);
    if (!visible(game, attacker, false, &shown)) return false;
    return !shown || (cok_arena_pose(game, attacker, ca->facing, 1, true) &&
                      cok_arena_pose(game, attacker, ca->facing, 0, false));
}

bool cok_combat_sweep(cok_adventure *game, cok_character *attacker, cok_character *target,
                      bool *swept)
{
    cok_combat *combat = &game->combat;
    uint8_t *a = attacker->record;
    *swept = false;
    cok_combat_record *ca = record_of(game, attacker);
    if (ca == NULL) return false;
    if (a[0x18f] >= ca->sweeps || target->record[0xd6] != 0) return true;
    uint8_t d, n;
    if (!distance(game, attacker, target, &d)) return false;
    if (d != 1) return true;
    if (!cok_combat_enemies(game, attacker, 1, &n)) return false;
    uint8_t count = 0;
    unsigned at = 0;
    for (unsigned i = 1; i <= n; ++i) {
        cok_character *e = combatant(game, combat->enemies[i], "432f:0fce");
        if (e == NULL) return false;
        if (e == target) at = i;
        if (e->record[0xd6] == 0) ++count;
    }
    if (count <= a[0x18f]) return true;
    if (count > ca->sweeps) count = ca->sweeps;
    if (!cok_arena_say(game, attacker, "sweeps", 10, true)) return false;
    /* The target goes first, where it was listed; one not listed takes
     * the place a byte the original never set says. */
    if (combat->combatant[combat->enemies[1]].character != target) {
        if (at == 0)
            return undefined(game, "the swept target is not among those listed, whose place is "
                                   "a byte never set (432f:110b)");
        combat->enemies[at] = combat->enemies[1];
        combat->enemies[1] = cok_combat_index(combat, target->record);
    }
    for (unsigned i = 1; i <= n; ++i) {
        cok_character *e = combatant(game, combat->enemies[i], "432f:0fce");
        if (e == NULL) return false;
        if (count == 0 || e->record[0xd6] != 0) continue;
        bool over;
        if (!cok_combat_book(game, attacker, e)) return false;
        a[0x18f] = 1;
        if (!cok_combat_attack(game, attacker, e, false, 0, &over)) return false; /* DS:7199 */
        --count;
    }
    *swept = true;
    return true;
}

bool cok_combat_can_attack(cok_adventure *game, cok_character *attacker,
                           const uint8_t *target, bool *can)
{
    cok_rolls *rolls = &game->effects.rolls;
    *can = false;
    if (target == NULL) return true;
    if (target == attacker->record) {
        *can = true;
        return true;
    }
    cok_character *t = member_of(game, target);
    if (t == NULL) return undefined(game, "the target is not in the list (432f:11d4)");
    rolls->untargetable = 0;
    if (!event(game, t, 1)) return false;
    if (rolls->untargetable == 0) {
        /* Event 0 runs no handler, with the attacker aiming at it. */
        cok_combat_record *ca = record_of(game, attacker);
        if (ca == NULL) return false;
        uint8_t *aimed = ca->target;
        ca->target = (uint8_t *)target;
        bool ok = event(game, attacker, 0);
        ca->target = aimed;
        if (!ok) return false;
    }
    *can = rolls->untargetable == 0;
    return true;
}

bool cok_combat_pick_target(cok_adventure *game, cok_character *c, uint8_t range, bool flag,
                            bool force, bool *found)
{
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    *found = false;
    if (force) {
        cr->target = NULL;
    } else if (cr->target != NULL) {
        bool keep = false;
        const uint8_t *t = cr->target;
        if (t[0x18a] != c->record[0x18a] && t[0x189] != 0 &&
            !cok_combat_can_attack(game, c, t, &keep))
            return false;
        if (!keep) cr->target = NULL;
    }
    if (cr->target != NULL) *found = true;
    bool pass = false, last = false;
    while (!*found && !last) {
        last = pass;
        if (pass && !force) combat->see_all = 1;
        uint8_t tries = 20, n;
        if (!cok_combat_enemies(game, c, range, &n)) return false;
        while (tries > 0 && !*found && n > 0) {
            --tries;
            uint8_t pick;
            const cok_character *first = combat->combatant[combat->enemies[1]].character;
            if (cok_character_find_effect(c, 0x5b) != NULL &&
                (first != NULL ? first->record : NULL) == combat->yelled && tries == 19)
                pick = 1;
            else
                pick = roll(game, 1, n);
            if (combat->enemies[pick] == 0) continue;
            cok_character *t = combatant(game, combat->enemies[pick], "432f:3f9f");
            if (t == NULL) return false;
            bool can = flag && combat->see_all != 0;
            if (!can && !cok_combat_can_attack(game, c, t->record, &can)) return false;
            if (can) {
                *found = true;
                cr->target = t->record;
                continue;
            }
            combat->enemies[pick] = 0;
            bool left = false;
            for (unsigned i = 1; i <= n; ++i)
                if (combat->enemies[i] > 0) left = true;
            if (!left) n = 0;
        }
        pass = true;
    }
    combat->see_all = 0;
    return true;
}

/* Movement. */

/* 432f:068f: each enemy beside c (6346:26e2) that guards and is not
 * helpless, while c can act, is shown, stops guarding and attacks it. */
static bool guards(cok_adventure *game, cok_character *c)
{
    cok_combat *combat = &game->combat;
    uint8_t n;
    if (!cok_combat_enemies(game, c, 1, &n)) return false;
    for (unsigned i = 1; i <= n; ++i) {
        if (c->record[0x189] == 0) continue;
        cok_character *e = combatant(game, combat->enemies[i], "432f:068f");
        if (e == NULL) return false;
        cok_combat_record *ce = record_of(game, e);
        if (ce == NULL) return false;
        if (ce->guarding == 0 || cok_combat_helpless(e)) continue;
        if (!cok_arena_centre(game, cok_combat_x(combat, c->record),
                              cok_combat_y(combat, c->record), 2, 8))
            return false;
        ce->guarding = 0;
        bool over;
        if (!cok_combat_book(game, e, c) || !cok_combat_attack(game, e, c, false, 0, &over))
            return false;
    }
    return true;
}

bool cok_combat_step(cok_adventure *game, cok_character *c, uint8_t dir)
{
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = record_of(game, c);
    uint8_t n;
    if (cr == NULL || !index_of(game, c, &n, "432f:077a")) return false;
    if (dir > 8) return undefined(game, "direction %u is past the steps (DS:1ed6, 432f:077a)", dir);
    int8_t x = combat->combatant[n].x, y = combat->combatant[n].y;
    int8_t nx = s8(x + step_x[dir]), ny = s8(y + step_y[dir]);
    /* The cell is read without a check: off the map, the heap around it. */
    int at = ny * COK_COMBAT_WIDTH + nx;
    if (at < 0 || at >= COK_COMBAT_WIDTH * COK_COMBAT_HEIGHT)
        return undefined(game, "a step off the map reads the heap around it (432f:07fe)");
    uint8_t value = combat->cells[at / COK_COMBAT_WIDTH][at % COK_COMBAT_WIDTH];
    if (value >= COK_COMBAT_TERRAINS)
        return undefined(game, "terrain 0x%02x is past the table (DS:1ee4, 432f:077a)", value);
    uint8_t cost = (uint8_t)(cok_combat_terrain[value].cost * ((dir & 1) != 0 ? 3 : 2));
    cr->movement = cost > cr->movement ? 0 : (uint8_t)(cr->movement - cost);
    uint8_t margin = 1;
    if (c->record[0x18b] != 0) {
        margin = 3;
        if (!cok_combat_on_view(s8(nx - combat->view_x), s8(ny - combat->view_y)) &&
            combat->show_actions && !cok_arena_centre(game, x, y, 2, 8))
            return false;
    }
    if (combat->show_actions && !cok_arena_erase(game, 0, 0, n)) return false;
    combat->combatant[n].x = nx;
    combat->combatant[n].y = ny;
    if (!cok_combat_occupy(combat))
        return undefined(game, "a footprint is written outside the occupants (6beb:0375)");
    if (combat->show_actions && !cok_arena_centre(game, nx, ny, margin, 8)) return false;
    cr->hits = 0;
    cr->turning = 0;
    cok_arena_sound(game, SOUND_STEP);
    if (!guards(game, c)) return false;
    if (c->record[0x189] == 0 || cok_combat_helpless(c)) cr->movement = 0;
    return true;
}

bool cok_combat_opportunity(cok_adventure *game, cok_character *c, uint8_t dir)
{
    cok_combat *combat = &game->combat;
    uint8_t n0, count;
    if (!index_of(game, c, &n0, "432f:0986")) return false;
    if (dir > 8) return undefined(game, "direction %u is past the steps (DS:1ed6, 432f:0986)", dir);
    if (!cok_combat_enemies(game, c, 1, &count)) return false;
    if (count == 0) return true;
    /* Those beside it, not helpless or made to flee (the original's twelve
     * bytes hold any footprint's neighbours)... */
    uint8_t left[13] = {0};
    unsigned m = 0;
    for (unsigned i = 1; i <= count; ++i) {
        cok_character *e = combatant(game, combat->enemies[i], "432f:0986");
        if (e == NULL) return false;
        cok_combat_record *ce = record_of(game, e);
        if (ce == NULL) return false;
        if (cok_combat_helpless(e) || ce->forced != 0) continue;
        if (m == 12) return undefined(game, "more than twelve enemies beside one overrun the list "
                                            "(432f:0a3b)");
        left[++m] = combat->enemies[i];
    }
    /* ... that are not beside it after the step, its place moved for the
     * while. */
    cok_combatant *self = &combat->combatant[n0];
    self->x = s8(self->x + step_x[dir]);
    self->y = s8(self->y + step_y[dir]);
    uint8_t after;
    bool ok = cok_combat_enemies(game, c, 1, &after);
    self->x = s8(self->x - step_x[dir]);
    self->y = s8(self->y - step_y[dir]);
    if (!ok) return false;
    for (unsigned j = 1; j <= m; ++j)
        for (unsigned i = 1; i <= after; ++i)
            if (combat->enemies[i] == left[j]) left[j] = 0;
    for (unsigned j = 1; j <= m; ++j) {
        if (left[j] == 0 || c->record[0x189] == 0) continue;
        combat->panel = true;
        combat->show_actions = true;
        cok_character *e = combatant(game, left[j], "432f:0986");
        if (e == NULL) return false;
        cok_combat_record *ce = record_of(game, e);
        bool can, missile, thrown;
        if (ce == NULL || !cok_combat_can_attack(game, e, c->record, &can)) return false;
        if (!can) continue;
        if (!missile_weapon(game, e, &missile)) return false;
        if (missile) {
            if (!cok_combat_thrown(&game->item_types, e, &thrown))
                return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
            if (!thrown) continue;
        }
        bool done = false;
        /* Five directions from its facing + 6, as bytes. */
        uint8_t first = (uint8_t)(ce->facing + 6), last = (uint8_t)(ce->facing + 10);
        for (unsigned d = first; d <= last; ++d) {
            if (done) continue;
            if (ce->initiative <= 0 && ce->hits != 0) {
                bool in;
                if (!cok_combat_in_arc(cok_combat_x(combat, e->record),
                                       cok_combat_y(combat, e->record),
                                       cok_combat_x(combat, c->record),
                                       cok_combat_y(combat, c->record), (uint8_t)(d % 8), &in))
                    return undefined(game, "6b30:054a returns an uninitialized byte");
                if (!in) continue;
            }
            /* The last slot of the two with attacks left, else 1 (2 with
             * no attacks a round in the first), given one attack if it has
             * none. */
            uint8_t slot = e->record[0x10b] == 0 ? 2 : 1;
            for (uint8_t s = 1; s <= 2; ++s)
                if (e->record[0x18e + s] > 0) slot = s;
            if (e->record[0x18e + slot] == 0) e->record[0x18e + slot] = 1;
            ce->attack_slot = slot;
            uint8_t *aimed = ce->target;
            bool over;
            if (!cok_combat_attack(game, e, c, true, 0, &over)) return false; /* DS:7199 */
            done = true;
            ce->target = aimed;
            if (c->record[0x189] != 0) {
                combat->panel = true;
                if (!cok_arena_panel(game, c)) return false;
            }
        }
    }
    return true;
}

bool cok_combat_flee(cok_adventure *game, cok_character *c)
{
    uint8_t n;
    if (!cok_combat_enemies(game, c, 0xff, &n)) return false;
    bool away = n == 0;
    if (!away) {
        uint8_t mine, theirs;
        if (!cok_combat_movement(game, c, &mine) || !cok_combat_fastest_enemy(game, c, &theirs))
            return false;
        mine = (uint8_t)(mine >> 1);
        away = theirs < mine || (theirs == mine && roll(game, 1, 2) == 1);
    }
    if (away) {
        if (!cok_combat_leave(game, c, 3, "Got Away")) return false;
    } else {
        cok_camp_notice(game, "Escape is blocked"); /* 6346:1827 */
    }
    cok_combat_end_turn(c);
    return true;
}

/* 3f44:272a's gating. */
bool cok_combat_gate(cok_adventure *game, cok_character *c)
{
    cok_combat *combat = &game->combat;
    const cok_party *party = &game->party;
    size_t n = party->count, q = 0, p = 0;
    uint8_t side = c->record[0x18a];
    /* q: the first record of the party list (DS:609a, +0x17f) on another
     * side, n for none; p: the first waiting (status 9). */
    while (q < n && party->members[q]->record[0x18a] == side) ++q;
    while (p < n && party->members[p]->record[0x188] != 9) ++p;
    bool done = false, around_c = false;
    int8_t k = 0;
    for (;;) {
        if (k > 7) {
            if (!around_c && q == n) {
                around_c = done = true;
            } else if (!around_c) {
                ++q;
                if (q == n)
                    return undefined(game, "gating reads the side of the record after the last, "
                                           "at 0000:018a (3f44:2807)");
                if (party->members[q]->record[0x18a] == side) around_c = done = true;
            }
            k = 0;
        }
        if (p == n) break;
        /* With none on another side, NULL's place: entry 0's, 0, 0. */
        const uint8_t *qr = around_c ? c->record : q < n ? party->members[q]->record : NULL;
        int8_t x = qr != NULL ? cok_combat_x(combat, qr) : 0;
        int8_t y = qr != NULL ? cok_combat_y(combat, qr) : 0;
        bool placed = false;
        do {
            int8_t nx = (int8_t)(x + step_x[k]), ny = (int8_t)(y + step_y[k]);
            if (nx > 0x31 || nx < 0 || ny > 0x18 || ny < 0)
                return undefined(game, "gating tries a cell off the map, and never the next "
                                       "(3f44:2895): the game hangs");
            uint8_t occupant, terrain;
            cok_combat_cell(combat, nx, ny, &occupant, &terrain);
            if (occupant == 0 && terrain == 0x17) {
                /* One not put back moves on all the same: past the last, the
                 * next free cell places NULL's record. */
                if (p == n)
                    return undefined(game, "gating places the record after the last, through "
                                           "NULL (3f44:28e2)");
                cok_character *w = party->members[p];
                uint8_t i = cok_combat_index(combat, w->record);
                if (i == 0)
                    return undefined(game, "a waiting record that is not a combatant is placed "
                                           "through entry 0, over the count (3f44:2901)");
                combat->combatant[i].size = w->record[0xcf] & 7;
                combat->combatant[i].x = nx;
                combat->combatant[i].y = ny;
                if (!cok_combat_revive(game, w, w->record[0x62], "gates in", &placed))
                    return false;
                ++p;
                if (p == n || party->members[p]->record[0x188] != 9) done = true;
            }
            ++k;
        } while (!placed && k <= 7);
        if (done || around_c) break;
    }
    return true;
}

/* Deaths. */

bool cok_combat_kill(cok_adventure *game, cok_character *c, uint8_t status, const char *text)
{
    uint8_t *r = c->record;
    if (!cok_arena_say(game, c, text, 10, false)) return false;
    if (r[0x188] >= 6 && r[0x188] <= 8) return true;
    r[0x188] = status;
    r[0x189] = 0;
    r[0x197] = 0;
    int stale;
    if (!battle_only(game, c, &stale) || !event(game, c, 0x0d)) return false;
    if (r[0x189] == 0 && r[0x188] != 10 && !kill(game, c, stale)) return false;
    wait_speed(game);
    cok_arena_clear_text(game);
    if (game->vm.mode != 5) cok_adventure_party(game); /* 6346:07ba */
    return true;
}

bool cok_combat_revive(cok_adventure *game, cok_character *c, uint8_t hp, const char *text,
                       bool *placed)
{
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (!cok_combat_place(combat, game->vm.mode, c, (uint8_t)cok_combat_x(combat, r),
                          (uint8_t)cok_combat_y(combat, r), true, placed))
        return undefined(game, "a record is placed outside the map's tables (6beb:10f3)");
    if (!*placed) return true;
    r[0x188] = 0;
    r[0x189] = 1;
    r[0x197] = hp;
    if (game->vm.mode == 5) {
        bool shown = combat->show_actions;
        combat->show_actions = true;
        bool ok = cok_arena_turn(game, c, 3, false);
        combat->show_actions = shown;
        if (!ok) return false;
    }
    if (!cok_arena_flash(game, c, 1, text)) return false;
    if (!cok_combat_count_sides(combat, &game->party))
        return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                               "DS:6b2d (6346:268a)");
    return true;
}

bool cok_combat_hoopak(const cok_character *c)
{
    size_t weapon = c->slots[0];
    return weapon != 0 && weapon <= c->item_count && c->items[weapon - 1][0x2e] == 0x43;
}

bool cok_combat_thrown(const cok_item_types *types, const cok_character *c, bool *thrown)
{
    bool missile;
    *thrown = false;
    if (!cok_combat_missile_weapon(types, c, &missile)) return false;
    if (!missile) return true;
    uint8_t type = c->items[c->slots[0] - 1][0x2e];
    *thrown = (types->type[type][14] & 0x14) == 0x14 || type == 0x43;
    return true;
}

/* eclplay's --combat melee. */

/* 432f:3219 in attack mode, on an enemy (432f:2f12 asks only about an
 * ally): the cursor off, the view's centre redrawn, a sweep, or the
 * target booked and attacked with the ammunition, but none for a thrown
 * weapon a square away. */
static bool aimed(cok_adventure *game, cok_character *attacker, cok_character *target, bool *over)
{
    cok_combat *combat = &game->combat;
    combat->cursor = 0;
    if (!cok_arena_centre(game, s8(combat->view_x + 3), s8(combat->view_y + 3), 3, 8)) return false;
    bool swept;
    if (!cok_combat_sweep(game, attacker, target, &swept)) return false;
    if (swept) {
        cok_combat_end_turn(attacker);
        *over = true;
        return true;
    }
    if (!cok_combat_book(game, attacker, target)) return false;
    size_t item = 0;
    bool missile, has, thrown;
    if (!missile_weapon(game, attacker, &missile)) return false;
    if (missile) {
        if (!cok_combat_ammunition(&game->item_types, attacker, &item, &has) ||
            !cok_combat_thrown(&game->item_types, attacker, &thrown))
            return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
        if (has && thrown) {
            uint8_t d;
            if (!distance(game, attacker, target, &d)) return false;
            if (d == 1) item = 0;
        }
    }
    return cok_combat_attack(game, attacker, target, false, item, over);
}

bool cok_combat_melee(cok_adventure *game, cok_character *c)
{
    cok_combat *combat = &game->combat;
    /* An attack that leaves the turn going has felled its target with
     * attacks left; each such fells one, so the list runs out. */
    for (unsigned attacks = 0; attacks < COK_COMBATANTS; ++attacks) {
        if (c->record[0x189] == 0) break;
        bool missile, has = false;
        size_t ammunition = 0;
        uint8_t n, range = 1;
        if (!missile_weapon(game, c, &missile)) return false;
        if (missile && !cok_combat_ammunition(&game->item_types, c, &ammunition, &has))
            return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
        if (!cok_combat_enemies(game, c, 1, &n)) return false;
        if (n == 0 && missile && has) {
            const uint8_t *type;
            if (!weapon_type(game, c, &type)) return false;
            range = type[12] > 2 ? (uint8_t)(type[12] - 1) : 1;
            if (!cok_combat_enemies(game, c, range, &n)) return false;
        }
        cok_character *target = NULL;
        for (unsigned i = 1; i <= n && target == NULL; ++i) {
            cok_character *e = combatant(game, combat->enemies[i], "the melee rule");
            bool can;
            if (e == NULL || !cok_combat_can_attack(game, c, e->record, &can)) return false;
            if (can) target = e;
        }
        if (target == NULL) break;
        bool over;
        if (!aimed(game, c, target, &over)) return false;
        if (over) return true;
    }
    cok_combat_end_turn(c);
    return true;
}
