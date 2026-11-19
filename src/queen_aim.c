#include "queen.h"

#include "arena.h"
#include "attack.h"
#include "combat.h"
#include "command.h"
#include "party.h"
#include "round.h"

#include <stdio.h>
#include <string.h>

/* Aim (dqk 52db:37dd, from 08cf's ITEM_AIM and 1e78's spell targeting). The targets listed by
 * 36c6, the next one by 3465, the menu and the cursor's read by 2b60, all as Dark Queen's. */

/* Not modelled: DS:c6e4 (set to 1, 3 and 4 here; its readers are Use's and the spells'), DS:a93f
 * (Manual's first key: a parameter of its stop), DS:a898 (the step passed to 3465: always 0),
 * DS:ae26 (3465's loop counter: a local here) and the list's DS:a899, a93a and a939 (aim_targets). */

/* The menu's items in index order (DS:3ef2's "Next Prev Manual Target Center Exit"). */
enum { AIM_NEXT, AIM_PREV, AIM_MANUAL, AIM_TARGET, AIM_CENTER, AIM_EXIT, AIM_ITEMS };
static const char *const item_words[AIM_ITEMS] = {"Next", "Prev", "Manual", "Target",
                                                  "Center", "Exit"};

/* The range's text (DS:3d96, "Aim: range = "), the number after it and a blank. */
static const char range_words[] = "Aim: range = ";

/* The targets (DS:a899 + 3k, the entries from 1: a combatant's index, distance and direction as
 * cok_combat_list gives them), their count (DS:a93a) and the one shown (DS:a939). */
typedef struct {
    cok_combat_listing list[COK_COMBATANTS + 1];
    uint8_t count, current;
} aim_targets;

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* The record of combatant index (a listed one: one with a record, else NULL). */
static cok_character *record_of(cok_adventure *game, unsigned index)
{
    return index <= COK_COMBATANTS ? game->combat.combatant[index].character : NULL;
}

/* The record of entry k of the list (k from 1, at most the count). */
static cok_character *entry_of(cok_adventure *game, const aim_targets *t, unsigned k)
{
    return record_of(game, t->list[k].index);
}

/* The word +0x66 (COK_FIELD_HOSTILE): 0 for the party's side, 1 for an enemy. */
static unsigned hostile(cok_adventure *game, const cok_character *c)
{
    return cok_combat_field_word(cok_game_rules(game), c->record, COK_FIELD_HOSTILE);
}

/* DS:a939 moved by step, wrapping (0 is the count, past the count is 1). */
static void advance(aim_targets *t, uint8_t step)
{
    t->current = (uint8_t)(t->current + step);
    if (t->current == 0) t->current = t->count;
    if (t->current > t->count) t->current = 1;
}

/* 3465 (step, rec): the next target by step (1 Next, 0xff Prev, 0 the one shown again), the
 * cursor flying from the one shown to it when it is on the map (51c2:00df, one frame, no delay).
 * On a step the party's side (+0x66 0) is passed over while an enemy is listed; an entry off the
 * map keeps the one shown (the one before the step; c for none). With no entry the original quits
 * to DOS (2093:002d): the port's fail, unreachable (c is always listed). */
static bool next_target(cok_adventure *game, cok_character *c, aim_targets *t, uint8_t step,
                        cok_character **result)
{
    cok_combat *combat = &game->combat;
    if (t->count < 1) return undefined(game, "no target is listed, and aiming quits to DOS "
                                             "(52db:3465, 2093:002d)");
    cok_character *prev = entry_of(game, t, t->current);
    if (prev == NULL) prev = c;
    unsigned n = t->list[t->current].index;
    int8_t x0 = combat->combatant[n].x, y0 = combat->combatant[n].y;
    advance(t, step);
    cok_character *cand = entry_of(game, t, t->current);
    if (step != 0 && (cand == NULL || hostile(game, cand) != 1)) {
        /* 3465's loop (356f): an entry of the party's side (+0x66 0) is passed over, at most count
         * times; a NULL record (none in the list: the original reads address 0) is taken, as the
         * port's no-record fallback. */
        bool found = false;
        unsigned tries = 0;
        do {
            if (cand == NULL || hostile(game, cand) != 0) {
                found = true;
            } else {
                advance(t, step);
                cand = entry_of(game, t, t->current);
            }
            ++tries;
        } while (tries < t->count && !found);
    }
    cok_character *target = entry_of(game, t, t->current);
    n = t->list[t->current].index;
    int8_t x1 = combat->combatant[n].x, y1 = combat->combatant[n].y;
    if (target == NULL || x1 < 0 || y1 < 0) {
        *result = prev;
        return true;
    }
    if (!cok_arena_missile(game, x0, y0, x1, y1, 1, 0)) return false;
    *result = target;
    return true;
}

/* 36c6 (rec): the list around c (cok_combat_list at range 0xff, direction any), each entry c can
 * attack (cok_combat_can_attack, c itself among them: distance 0) appended; the target c kept (its
 * combat record's +0x0b) is the one shown, else c. Then the first target by 3465(0). */
static bool list_targets(cok_adventure *game, cok_character *c, aim_targets *t,
                         cok_character **target)
{
    cok_combat *combat = &game->combat;
    t->current = 1;
    if (!cok_combat_list(combat, cok_game_rules(game), cok_combat_x(combat, c->record),
                         cok_combat_y(combat, c->record), 0xff, 0xff,
                         cok_combat_size(combat, c->record)))
        return undefined(game, "the list of combatants around one (52db:36c6, 5d71:07d7) reads "
                               "past its tables");
    t->count = 0;
    for (unsigned i = 1; i <= combat->listed_count; ++i) {
        cok_character *e = record_of(game, combat->listed[i].index);
        if (e == NULL) return undefined(game, "a listed combatant has no record (52db:36c6)");
        bool can;
        if (!cok_combat_can_attack(game, c, e->record, &can)) return false;
        if (!can) continue;
        t->list[++t->count] = combat->listed[i];
        if (c->combat->target == e->record) t->current = t->count;
    }
    return next_target(game, c, t, 0, target);
}

/* The Target item's offer (52db:2b60's tests): within range and a cell that may be picked. With
 * attack 0 (a spell's) a creature shown or, with empty, no one; attacking, not c, and with a
 * missile weapon only with ammunition and no enemy beside c or a weapon thrown. Returns 1, 0, or
 * -1 where the run stopped. */
static int target_offered(cok_adventure *game, cok_character *c, const cok_character *target,
                          uint8_t range, uint8_t attack, uint8_t empty, uint8_t creatures,
                          bool valid, uint16_t distance)
{
    if (range < distance || !valid) return 0;
    if (attack == 0) return (creatures != 0 && target != NULL) || (empty != 0 && target == NULL);
    if (target == c) return 0;
    bool missile;
    if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) {
        undefined(game, "an item type past ITEMS reads past the table (52db:2b60)");
        return -1;
    }
    if (!missile) return 1;
    size_t item;
    bool has;
    if (!cok_combat_ammunition(game, c, &item, &has)) return -1;
    if (!has) return 0;
    uint8_t enemies;
    if (!cok_combat_enemies(game, c, 1, &enemies)) return -1;
    if (enemies == 0) return 1;
    bool thrown;
    if (!cok_combat_thrown(&game->item_types, c, &thrown)) return -1;
    return thrown ? 1 : 0;
}

/* 2b60 (rec, target, range, attack, empty, creatures, valid, x, y): the distance by sight (with
 * sight not blocked, halved) into the range's text on row 23 when attacking, the target's
 * turn and panel, and the menu of the items offered. x, y are the sight's (its stop, where it
 * does not see the cell). Returns the item's index (0-5), or with *special set the code of a key
 * or an area (Escape 0x1b, the extended keys 0x80-0x88), or -1 where the keys ran out, -2 where
 * the run stopped. */
static int show(cok_adventure *game, cok_character *c, cok_character *target, uint8_t range,
                uint8_t attack, uint8_t empty, uint8_t creatures, bool valid, int8_t *x, int8_t *y,
                bool *special)
{
    cok_combat *combat = &game->combat;
    const cok_rules *rules = cok_game_rules(game);
    uint16_t distance = 0xff;
    uint8_t see_all = combat->see_all;
    combat->see_all = 1;
    bool seen;
    bool ok = cok_combat_sight(combat, rules, cok_combat_x(combat, c->record),
                               cok_combat_y(combat, c->record), x, y, &distance, &seen);
    combat->see_all = see_all;
    if (!ok) {
        undefined(game, "sight (5d71:0328) reads a cell past the terrain table");
        return -2;
    }
    distance = (uint16_t)(distance >> 1);
    if (attack != 0) {
        char text[32];
        snprintf(text, sizeof text, "%s%u ", range_words, (unsigned)distance);
        if (!cok_arena_text(game, 0, 0x17, text, 7, 0)) return -2; /* 1a62:0046 */
    }
    int ids[AIM_ITEMS];
    const char *names[AIM_ITEMS];
    size_t count = 0;
    for (int i = 0; i < AIM_ITEMS; ++i) {
        if (i == AIM_TARGET) {
            int offer = target_offered(game, c, target, range, attack, empty, creatures, valid,
                                       distance);
            if (offer < 0) return -2;
            if (offer == 0) continue;
        }
        ids[count] = i;
        names[count++] = item_words[i];
    }
    if (target != NULL) {
        if (!cok_arena_turn(game, target, 3, true)) return -2; /* 5837:12f5 */
        combat->panel = true;
        if (!cok_arena_panel(game, target)) return -2; /* 177e:085f */
    }
    char text[64] = "";
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) strcat(text, " ");
        strcat(text, names[i]);
    }
    cok_adventure_log(game, "menu", text);
    int result = queen_bar_menu(game, "", names, count, true); /* 1bdb:1e29 */
    if (result < 0) return -1;
    if ((size_t)result < count) {
        *special = false;
        return ids[result];
    }
    *special = true;
    return result;
}

/* The readied weapon's range (byte 12 of its item type, 52db:37dd's a938): none is 1. The type
 * is the item's byte at record_type_at (round.c's weapon_type reads it the same way). False for
 * an item type past ITEMS. */
static bool readied_range(cok_adventure *game, const cok_character *c, uint8_t *range)
{
    const cok_item_types *types = &game->item_types;
    *range = 1;
    size_t weapon = c->slots[0];
    if (weapon == 0 || weapon > c->item_count) return true;
    size_t at = types->record_type_at != 0 ? types->record_type_at : 0x2e;
    uint8_t t = c->items[weapon - 1][at];
    if (t >= COK_ITEM_TYPES)
        return undefined(game, "an item type past ITEMS reads past the table (52db:37dd)");
    *range = types->type[t][12];
    return true;
}

bool queen_aim(cok_adventure *game, cok_character *c, uint8_t range, uint8_t attack, uint8_t empty,
               uint8_t creatures, bool *done, cok_combat_aimed *res)
{
    cok_combat *combat = &game->combat;
    if (!cok_arena_missile_frame(game, cok_game_rules(game)->commands.cursor_slot, 0, 0, false))
        return false; /* 51c2:0005(0x1e, 0, 0, 0): the cursor's pictures */
    *res = (cok_combat_aimed){NULL, 0, 0};
    *done = false;
    /* a938: 0xff is the readied weapon's range (whole, as DKK's), 0 or 0xff is 1. */
    if (range == 0xff && !readied_range(game, c, &range)) return false;
    if (range == 0 || range == 0xff) range = 1;
    int8_t ax = cok_combat_x(combat, c->record), ay = cok_combat_y(combat, c->record);
    if (!cok_arena_redraw_cell(game, ax, ay)) return false; /* 5837:025d */
    aim_targets t = {0};
    cok_character *target = NULL;
    if (!list_targets(game, c, &t, &target)) return false;
    int8_t x = cok_combat_x(combat, target->record), y = cok_combat_y(combat, target->record);
    bool valid = true;
    for (bool leave = false; !leave;) {
        bool special = false;
        int key = show(game, c, target, range, attack, empty, creatures, valid, &x, &y, &special);
        if (key == -2) return false;
        if (key < 0) return true; /* the keys ran out: the turn not ended */
        /* The menu's code (2b60's c2f5 test, 37dd 3905-3931): Escape is Exit, the keypad's 5
         * (0x80) Center, a direction code (0x81-0x88) is Manual with that key, and the other
         * codes do nothing (the tail below still runs). */
        int action = special ? -1 : key;
        if (special && key == 0x1b) action = AIM_EXIT;
        else if (special && key == 0x80) action = AIM_CENTER;
        else if (special && key >= 0x81 && key <= 0x88)
            return cok_adventure_not_yet(game, "Aim's Manual", "dqk 52db:2e9c (part 25)");
        switch (action) {
        case AIM_NEXT:
        case AIM_PREV:
            valid = true;
            if (!next_target(game, c, &t, action == AIM_NEXT ? 1 : 0xff, &target)) return false;
            break;
        case AIM_MANUAL:
            return cok_adventure_not_yet(game, "Aim's Manual", "dqk 52db:2e9c (part 25)");
        case AIM_TARGET:
            /* 37dd 3aec: the cell and the record, done; attacking, the strike (52db:2d46). */
            res->x = x;
            res->y = y;
            res->target = target->record;
            *done = true;
            if (attack == 1)
                return cok_adventure_not_yet(game, "Aim's Target", "dqk 52db:2d46 (part 24b)");
            leave = true;
            break;
        case AIM_CENTER:
            combat->cursor = 1;
            combat->cursor_size = 1;
            if (!cok_arena_centre(game, x, y, 0, 8)) return false; /* 5837:0846 */
            combat->cursor = 0;
            break;
        case AIM_EXIT: leave = true; break;
        default: break;
        }
        /* 37dd's tail, whatever the key: the shown target's cell redrawn, the next target by 0 (the
         * one shown again, and the cursor's flight to it); after 0, 1 and Target, the cell is the
         * target's. */
        if (!cok_arena_redraw_cell(game, cok_combat_x(combat, target->record),
                                   cok_combat_y(combat, target->record)))
            return false;
        if (!next_target(game, c, &t, 0, &target)) return false;
        if (action == AIM_NEXT || action == AIM_PREV || action == AIM_TARGET) {
            x = cok_combat_x(combat, target->record);
            y = cok_combat_y(combat, target->record);
        }
    }
    if (attack != 0) queen_range_line_clear(game); /* 1bdb:03a4 */
    return true;
}
