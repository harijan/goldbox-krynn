#include "queen.h"

#include "ai.h"
#include "arena.h"
#include "attack.h"
#include "combat.h"
#include "command.h"
#include "dark_queen.h"
#include "ecl.h"
#include "field.h"
#include "party.h"
#include "round.h"

#include <stdio.h>
#include <string.h>

/* The party member's command menu of Dark Queen (dqk 5045:08cf's loop from 0925, its menu built
 * by 0cd7 and shown by 1bdb:1595): the items of DS:50b3 by index, Move 0 (never shown: only the
 * direction keys give it) to End 12. Each command's routine is ported or a cited stop. */
enum {
    ITEM_MOVE, ITEM_AIM, ITEM_USE, ITEM_CAST, ITEM_TURN, ITEM_GUARD, ITEM_QUICK, ITEM_DELAY,
    ITEM_BANDAGE, ITEM_YELL, ITEM_VIEW, ITEM_SPEED, ITEM_END, ITEM_COUNT,
};
static const char *const item_words[ITEM_COUNT] = {
    "Move", "Aim", "Use", "Cast", "Turn", "Guard", "Quick", "Delay", "Bandage", "Yell", "View",
    "Speed", "End",
};

/* The record's byte offsets the menu reads (Dark Queen's layout, rules_dark_queen.c): the race
 * (0x58, a word with 0x59 0 for a kender); the class levels are read by the field layout. */
enum { RACE = 0x58, RACE_HIGH = 0x59, MEMORIZED_BYTES = 0x89 };

static bool type_past(cok_adventure *game)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED,
                       "an item type past ITEMS reads past the table (DS:5886)");
    return false;
}

/* The class level byte k of c's record as 177e:2234 reads it: the byte unsigned (its max with 0 is
 * the same for an unsigned byte), so 0xff is a level 255. */
static unsigned class_level(const cok_character_layout *L, const uint8_t *r, size_t k)
{
    return cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, k);
}

/* Turn's test (0cd7 and 08cf's 0x9c7): a cleric (class level 0 above 0) or a paladin (class level
 * 3 above 2), with fewer turns (+0x12) than the undead count DS:c2ed (game->undead). */
static bool can_turn(cok_adventure *game, const cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    return (class_level(L, c->record, 0) > 0 || class_level(L, c->record, 3) > 2) &&
           c->combat->turns < game->undead;
}

/* 0cd7: the items the record may choose, in index order (Aim always; Use while it has items;
 * Cast while a memorised spell's byte (0x89 bytes from +0xce, low 7 bits) is set, it may cast
 * this round and var COK_VAR_NO_MAGIC is 0; Turn for a cleric (class level 0 above 0, or class 3
 * above 2) with fewer turns than undead; Guard unless the readied weapon is a missile weapon not
 * thrown; Bandage while cok_combat_bandage would; Yell for a kender (+0x58 5, +0x59 0) while no
 * one has yelled; View, Speed, End and Quick and Delay always). Quick's index is split. */
static bool build(cok_adventure *game, cok_character *c, int ids[ITEM_COUNT], size_t *count,
                  int *split)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    const cok_combat_record *cr = c->combat;
    size_t n = 0;
    ids[n++] = ITEM_AIM;
    if (cok_field_u8(L, r, COK_FIELD_ITEM_COUNT) > 0) ids[n++] = ITEM_USE;
    bool memorised = false;
    size_t at = cok_field_offset(L, COK_FIELD_MEMORIZED);
    for (size_t i = 0; i < MEMORIZED_BYTES; ++i)
        if ((r[at + i] & 0x7f) != 0) memorised = true;
    if (memorised && cr->may_cast != 0 && cok_ecl_var(&game->vm, COK_VAR_NO_MAGIC) == 0)
        ids[n++] = ITEM_CAST;
    if (can_turn(game, c)) ids[n++] = ITEM_TURN;
    bool missile = false, thrown = false;
    if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return type_past(game);
    if (missile && !cok_combat_thrown(&game->item_types, c, &thrown)) return type_past(game);
    if (!missile || thrown) ids[n++] = ITEM_GUARD;
    ids[n++] = ITEM_QUICK;
    ids[n++] = ITEM_DELAY;
    if (cok_combat_bandage(game, false)) ids[n++] = ITEM_BANDAGE;
    if (cok_field_u8(L, r, COK_FIELD_RACE) == 5 && r[RACE_HIGH] == 0 &&
        game->combat.yelled == NULL)
        ids[n++] = ITEM_YELL;
    ids[n++] = ITEM_VIEW;
    ids[n++] = ITEM_SPEED;
    ids[n++] = ITEM_END;
    *split = -1;
    for (size_t i = 0; i < n; ++i)
        if (ids[i] == ITEM_QUICK) *split = (int)i;
    *count = n;
    return true;
}

/* The menu's text as 0e46 builds it for the log: the words in order, each followed by a blank,
 * or by '@' after Quick (the row break, DS:50b3's '@'); none after the last. */
static void text_of(const int ids[ITEM_COUNT], size_t count, char out[64])
{
    out[0] = '\0';
    for (size_t i = 0; i < count; ++i) {
        strcat(out, item_words[ids[i]]);
        if (i + 1 < count) strcat(out, ids[i] == ITEM_QUICK ? "@" : " ");
    }
}

/* The codes that take a menu's result (0cd7's c2f5 filter: the extended keys' 0x81-0x88 and
 * 0x12, Alt-A and Alt-Q (0xc0, 0xd0), Alt-M (0xcc), Alt-X (0xd7)); others show the menu again. */
static bool taken_code(int code)
{
    return (code >= 0x81 && code <= 0x88) || code == 0x12 || code == 0xc0 || code == 0xd0 ||
           code == 0xcc || code == 0xd7;
}

/* 0c0f (Alt-A and Alt-Q): the turn given back (the list's index less one, initiative 0x14),
 * every record of the combat list (DS:c6e5, the monsters appended too, 0c20-0c4a) taken by the
 * computer (5045:1791), the bottom bar (1bdb:036c), and 200 ms (1df9:052a, one draw): command.c's
 * quick() with the bar for its clear_row. */
static void quick_all(cok_adventure *game, cok_character *c)
{
    --game->combat.turn_index;
    c->combat->initiative = 0x14;
    for (size_t i = 0; i < game->party.count; ++i)
        cok_combat_auto(game->party.members[i], cok_game_rules(game));
    queen_bottom_bar(game);
    cok_dark_queen_wait(game, 200);
}

/* Move (dqk 5045:1099's undo, 10bd's put back): the movement back, c erased and put back where the
 * turn began (done where it is not placed), the screen centred there (margin 0, dir 8), and the
 * facing back: 2 while the record holds effect 0xd5 (177e:1734), else the turn's. */
static bool undo(cok_adventure *game, cok_character *c, uint8_t movement, uint8_t facing, int8_t x0,
                 int8_t y0, bool *done)
{
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = c->combat;
    cr->movement = movement;
    if (!cok_arena_erase(game, 0, 0, cok_combat_index(combat, c->record))) return false;
    bool placed;
    if (!cok_combat_place(combat, game->vm.mode, c, cok_game_rules(game), (uint8_t)x0, (uint8_t)y0,
                          false, &placed)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a combatant put back where it started reads or writes outside its "
                           "tables (dqk 5837:10bd)");
        return false;
    }
    *done = !placed;
    if (!cok_arena_centre(game, cok_combat_x(combat, c->record), cok_combat_y(combat, c->record), 0,
                          8))
        return false;
    cr->facing = cok_character_find_effect(c, 0xd5) != NULL ? 2 : facing;
    return true;
}

/* Attacking an ally (dqk 52db:2aef): go where target is an enemy of c (1 where its word +0x66 is
 * 0, else 0, is c's word +0x66) or c is the computer's; else "attack ally? " (No or Escape: no;
 * Yes: var 0x36 (COK_VAR_FLED) set, the sides counted, and go). DS:af3e is set by Yes and read by
 * nothing here. The keys running out is a no (the run is ended by the next read). */
bool queen_attack_ally(cok_adventure *game, cok_character *c, cok_character *target, bool *go)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    /* 51c2:08fd tests the target's +0x66 as a word, and 2aef compares that with c's word +0x66
     * (cok_combat_field_word: Dark Queen's ints). */
    unsigned other = cok_combat_field_word(rules, target->record, COK_FIELD_HOSTILE) == 0 ? 1 : 0;
    *go = other == cok_combat_field_word(rules, c->record, COK_FIELD_HOSTILE) ||
          cok_field_u8(L, c->record, COK_FIELD_COMPUTER) != 0;
    if (*go) return true;
    if (queen_yes_no(game, "attack ally? ") != 1) {
        *go = false;
        return true;
    }
    cok_ecl_set_var(&game->vm, COK_VAR_FLED, 1);
    /* dqk 52db:2aef: the sides are counted (51c2:0920) whether or not anyone turns: none does. */
    if (!cok_combat_count_sides(&game->combat, &game->party, rules, game->ambush)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a record on a side other than 0 or 1 (+0x18a) counts past DS:6b2d "
                           "(dqk 51c2:0920)");
        return false;
    }
    *go = true;
    return true;
}

/* Melee by a step (dqk 5045:15a3): a missile weapon not thrown says "Can't use that underwater"
 * (var 0x3c, COK_VAR_RECORD_EFFECT) and "Not with that weapon", and no attack; else the ally
 * question, then the target kept (+0x0b), the book (52db:1853) and the attack (52db:18d6, one attack,
 * no sweep: Champions' charge sweeps first, Dark Queen's does not). */
static bool step_into(cok_adventure *game, cok_character *c, cok_character *target, bool *done)
{
    bool missile = false, thrown = false;
    if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return false;
    if (missile && !cok_combat_thrown(&game->item_types, c, &thrown)) return false;
    if (missile && !thrown) {
        if (cok_ecl_var(&game->vm, COK_VAR_RECORD_EFFECT) != 0)
            queen_bar_message(game, "Can't use that underwater");
        queen_bar_message(game, "Not with that weapon");
        return true;
    }
    bool go;
    if (!queen_attack_ally(game, c, target, &go)) return false;
    if (!go || game->vm.abort) return true;
    c->combat->target = target->record;
    return cok_combat_book(game, c, target) &&
           cok_combat_attack(game, c, target, false, 0, done);
}

/* One step of Move in dir (dqk 5045:1099's STEP): the pose and the probe; a combatant there is
 * melee (step_into); a wall in the cell asks "flee ? " (Yes flees and ends the turn; No or Escape
 * keeps the turn for the Move menu, which the caller shows); a cost above the movement says "can't
 * go there" (one draw); else the opportunity, the step, the round's square (148f:12ab) and the end
 * of the turn where c is inactive or helpless. */
static bool step(cok_adventure *game, cok_character *c, uint8_t dir, bool *done)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = c->combat;
    if (!cok_arena_pose(game, c, dir, 0, false)) return false;
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, rules, c->record, dir, &occupant, &terrain, &cloud, &puddle)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a size past the footprints is read after them (dqk 5837:0bd1)");
        return false;
    }
    if (occupant > 0) {
        cok_character *target =
            occupant <= COK_COMBATANTS ? combat->combatant[occupant].character : NULL;
        if (target == NULL) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "a combatant in the step's cell has no record (dqk 5045:1099)");
            return false;
        }
        return step_into(game, c, target, done);
    }
    if (terrain == 0) {
        int answer = queen_yes_no(game, "flee ? ");
        if (answer == 1) {
            if (!cok_combat_flee(game, c)) return false; /* 52db:0e26, which returns 1 */
            *done = true;
        }
        return true; /* No, Escape, or the keys ran out: done stays false */
    }
    uint8_t base = cok_combat_terrain(rules, terrain).cost;
    uint8_t cost = base == 0xff ? 0xff : (uint8_t)(base * ((dir & 1) != 0 ? 3 : 2));
    if (cost > cr->movement) {
        queen_bar_message(game, "can't go there");
        return true;
    }
    if (!cok_combat_opportunity(game, c, dir)) return false;
    if (cok_field_u8(L, c->record, COK_FIELD_ACTIVE) == 0) {
        cok_combat_end_turn(c);
        *done = true;
        return true;
    }
    if (cr->movement > 0 && !cok_combat_step(game, c, dir)) return false;
    if (cok_field_u8(L, c->record, COK_FIELD_ACTIVE) == 0) {
        cok_combat_end_turn(c);
        *done = true;
    }
    if (!cok_round_square(game, c, true)) return false; /* 148f:12ab(rec, 1, 0) */
    if (cok_combat_helpless(c, rules) || cok_field_u8(L, c->record, COK_FIELD_ACTIVE) == 0) {
        cok_combat_end_turn(c);
        *done = true;
    }
    return true;
}

/* Move (dqk 5045:1099) from a direction code (0x81-0x88, its first step taken at once) or the Move
 * menu: "Move/Attack, Move Left = N" with Undo and Done, the eight mouse areas round c. A direction
 * steps and shows the menu again; Undo (or Escape) puts c back; Done leaves. Keys running out leave
 * the turn not ended (vm.abort, so queen_commands returns). Movement under 2 is then 0. */
static bool move(cok_adventure *game, cok_character *c, int key, bool *done)
{
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = c->combat;
    uint8_t movement = cr->movement, facing = cr->facing;
    int8_t x0 = cok_combat_x(combat, c->record), y0 = cok_combat_y(combat, c->record);
    *done = false;
    bool flagged = true; /* DS:c2f5: the key is a code (a command's or the menu's filtered one) */
    while (!*done && key != 0 && key != 1 && key != 0x0d) {
        if (key == 0xff) {
            char prompt[48];
            snprintf(prompt, sizeof prompt, "Move/Attack, Move Left = %2u", cr->movement >> 1);
            int p = cok_combat_y(combat, c->record) - combat->view_y;
            int q = cok_combat_x(combat, c->record) - combat->view_x;
            static const char *const items[] = {"Undo", "Done"};
            int result = queen_combat_menu(game, prompt, items, 2, -1, p, q, 0);
            if (result < 0 || game->vm.abort) return true; /* the keys ran out: the turn not ended */
            flagged = result >= 2;
            key = result;
        }
        int dir = 8;
        if (flagged) {
            if (key == 0x1b) {
                if (!undo(game, c, movement, facing, x0, y0, done)) return false;
                key = 0;
                flagged = false;
            } else if (key >= 0x81 && key <= 0x87) {
                dir = key - 0x80;
            } else if (key == 0x88) {
                dir = 0;
            }
        } else if (key == 0) {
            if (!undo(game, c, movement, facing, x0, y0, done)) return false;
        }
        if (dir < 8) {
            if (!step(game, c, (uint8_t)dir, done)) return false;
            if (game->vm.abort) return true;
        }
        if (flagged) key = 0xff;
    }
    if (cr->movement < 2) cr->movement = 0;
    return true;
}

/* The turn's command routine by its item (08cf's table at 0925's jump table). done is set
 * where the turn ends; out is set where the keys ran out in the middle of a command (Speed, View);
 * the stops are cited (part numbers for the ones still to port). */
static bool command(cok_adventure *game, cok_character *c, int item, int code, bool *done,
                    bool *out)
{
    *done = false;
    *out = false;
    if (item < 0) {
        /* A key taken (0b3c's table): Move with a direction, Alt-A and Alt-Q, Alt-M, Alt-X. */
        if (code >= 0x81 && code <= 0x88) {
            /* The code a click on an area or an arrow key gives (its log: "code: 0x87"). */
            char text[8];
            snprintf(text, sizeof text, "0x%02x", code);
            cok_adventure_log(game, "code", text);
            return move(game, c, code, done);
        }
        if (code == 0xc0 || code == 0xd0) {
            quick_all(game, c); /* 0c0f */
            *done = true;
            return true;
        }
        if (code == 0xcc) {
            /* 0bd7: the magic toggled, its message ("magic on" DS:4a9d, "magic off" DS:4aa6);
             * the turn goes on (c8c). */
            game->combat.magic_on = !game->combat.magic_on;
            queen_bar_message(game, game->combat.magic_on ? "magic on" : "magic off");
            return true;
        }
        if (code == 0xd7) {
            /* 5045:0c5d, 52db:3feb: the gods answer only to the Helm's password "Gem" (eclplay's
             * --helm); else "That doesn't work" (177e:153c, DS:58f0) and the menu again (c8c).
             * The gods: the turn ends and the arena is centred (5837:12f5(c, 3, 0)). */
            if (!game->helm) {
                queen_bar_message(game, "That doesn't work");
                return true;
            }
            if (!cok_combat_gods(game) || !cok_arena_turn(game, c, 3, false)) return false;
            *done = true;
            return true;
        }
        return true; /* 0x12: nothing, the arena again (c8c) */
    }
    switch (item) {
    case ITEM_MOVE: return move(game, c, 0, done); /* index 0 is never built: the code 0 does nothing */
    case ITEM_AIM: {
        /* 08cf's 0x97c: 37dd(rec, 0xff, 1, 0, 1, &done, &res) (queen_aim.c), then the arena again
         * (c8c) unless the turn ended; the keys running out leave the turn not ended. */
        cok_combat_aimed res;
        return queen_aim(game, c, 0xff, 1, 0, 1, done, &res);
    }
    case ITEM_USE: return cok_adventure_not_yet(game, "Use", "dqk 86b8:0f41");
    case ITEM_CAST: return cok_adventure_not_yet(game, "Cast", "dqk 52db:2543 (part 28)");
    case ITEM_TURN: {
        /* 08cf's 0x9c7: the checks again, else the menu again (c8c); then 1117 (queen_turn_undead),
         * then End (0b29): the turn ended. */
        if (!can_turn(game, c)) return true;
        if (!queen_turn_undead(game, c)) return false;
        cok_combat_end_turn(c);
        *done = true;
        return true;
    }
    case ITEM_GUARD:
        /* 0a19: 51c2:0c52 (the end, then Guarding unless forced; its message 177e:153c). */
        cok_combat_end_turn(c);
        if (c->combat->forced == 0) {
            c->combat->guarding = 1;
            queen_bar_message(game, "Guarding");
        }
        *done = true;
        return true;
    case ITEM_QUICK:
        /* 0a2c: cok_combat_auto (1791), the bottom bar, the keys flushed (2093:0130), 200 ms
         * (one draw), done; then the computer's turn for c (2d99:0ff9 = 5bb8:0000, queen_computer).
         * The flush is the typed keys only. The original's 2093:0136 also drains them through
         * 2093:0078 (Alt-S and Alt-T toggle the sound, 2023:0247 and 2023:0272; Escape or '`'
         * with DS:ae59 set sets DS:a890), and then 22b7:235f(7) takes the pending press latch
         * (DS:cb49, set by the driver's press, which the port's cok_mouse_driver does not keep:
         * it holds the buttons' level). Nothing is left to drop in the port: Quick's own click
         * was read by the menu, and eclplay applies a \m only at the game's next read or poll,
         * so no click waits here. A press typed between Quick's read and this flush would be
         * swallowed by the original and kept here (open: a human's double click, not reproducible
         * by the keys). cok_adventure_flush drops the keys and leaves the sound and the latch. */
        cok_combat_auto(c, cok_game_rules(game));
        queen_bottom_bar(game);
        cok_adventure_flush(game);
        cok_dark_queen_wait(game, 200);
        *done = true;
        /* The computer's turn for c (2d99:0ff9 = 5bb8:0000, queen_computer: ai.c's
         * cok_combat_computer, as the Death Knights' Quick calls it). */
        return cok_combat_computer(game, c);
    case ITEM_DELAY:
        /* 0a60: command.c's delay (initiative 1, the turn list shifted). */
        cok_combat_delay(game, c);
        *done = true;
        return true;
    case ITEM_BANDAGE:
        /* 0adb: 51c2:0d63(1) (cok_combat_bandage: the dying member bandaged, its say), then 0b29
         * (End), whether one was bandaged or not. */
        (void)cok_combat_bandage(game, true);
        cok_combat_end_turn(c);
        *done = true;
        return true;
    case ITEM_YELL:
        /* 5045:0ae9: 148f:0005 (cok_combat_yell), with the damage type cleared first (148f:0020,
         * DS:bebd, before the saves: their event 0x0c handlers test it), then 0b29 (End). */
        game->effects.rolls.damage_type = 0;
        if (!cok_combat_yell(game, c)) return false;
        cok_combat_end_turn(c);
        *done = true;
        return true;
    case ITEM_VIEW: {
        /* 5045:0af8: 86b8:0b0c(&done) (queen_view_combat: the sheet, then its mode 5 tail), then
         * 52db:0edf(c) (cok_combat_weapon_attacks), then 51c2:0c94 (cok_arena_redraw) unless done;
         * the turn goes on (c8c). The original's 52db:0edf also stores DS:b7f5 = 0, read only by
         * effect 0xb2's handler (7e76:0dc6, COK_FX_MISSING here): nothing is lost by not storing it.
         * done (Items ended the turn) leaves the loop with no menu again, as the original's. */
        bool viewed;
        if (!queen_view_combat(game, &viewed)) return false;
        if (game->vm.abort) {
            *out = true; /* the keys ran out in the sheet: the turn not ended, no tail */
            return true;
        }
        if (!cok_combat_weapon_attacks(game, c)) return false;
        *done = viewed;
        return viewed || cok_arena_redraw(game);
    }
    case ITEM_SPEED:
        /* 166c: the speed menu (queen_speed); the turn goes on (c8c). */
        *out = !queen_speed(game);
        return true;
    case ITEM_END:
        cok_combat_end_turn(c); /* 0b29: 51c2:0c11 */
        *done = true;
        return true;
    default: break;
    }
    return true;
}

bool queen_commands(cok_adventure *game, cok_character *c, uint8_t pressed)
{
    if (c->combat == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "a combatant has no combat record");
        return false;
    }
    bool done = false;
    while (!done) {
        int ids[ITEM_COUNT], split;
        size_t count;
        if (!build(game, c, ids, &count, &split)) return false;
        char text[64];
        text_of(ids, count, text);
        cok_adventure_log(game, "menu", text);
        const char *names[ITEM_COUNT];
        for (size_t i = 0; i < count; ++i) names[i] = item_words[ids[i]];
        /* 0cd7's p and q (5837:0b19, 0aea): the combatant's row and column less the view's corner,
         * as the bytes the mouse's areas take (only with the mouse on). */
        int p = cok_combat_y(&game->combat, c->record) - game->combat.view_y;
        int q = cok_combat_x(&game->combat, c->record) - game->combat.view_x;
        int result;
        for (;;) {
            /* The pressed key is the first menu's first read only: the menu is drawn, then the
             * key is taken (1bdb:0d26 draws before 0e37's read). */
            result = queen_combat_menu(game, "", names, count, split, p, q, pressed);
            pressed = 0;
            if (result < 0 || game->vm.abort) break;
            if ((size_t)result < count || taken_code(result)) break;
        }
        if (game->vm.abort) return game->vm.status == COK_ECL_OK;
        if (result < 0) return true; /* the keys ran out: the turn not ended */
        queen_combat_menu_close(game);
        int item = (size_t)result < count ? ids[result] : -1;
        bool out;
        if (!command(game, c, item, item < 0 ? result : 0, &done, &out)) return false;
        if (game->vm.abort) return game->vm.status == COK_ECL_OK;
        if (out) return true; /* the keys ran out in Speed: the turn not ended */
        if (!done) {
            /* 08cf's c8c: the arena again, the panel, and the menu. */
            if (!cok_arena_turn(game, c, 2, true)) return false;
            game->combat.panel = true;
            if (!cok_arena_panel(game, c)) return false;
        }
    }
    return true;
}
