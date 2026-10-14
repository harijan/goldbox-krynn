#include "command.h"

#include "ai.h"
#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "cast.h"
#include "cloud.h"
#include "items.h"
#include "magic.h"
#include "round.h"
#include "sheet.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* DS:1ed6 and DS:1edf: a step in direction 0-7, north first, clockwise; 8
 * for none. */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};

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

static bool type_past(cok_adventure *game)
{
    return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
}

static bool missile_weapon(cok_adventure *game, const cok_character *c, bool *missile)
{
    if (cok_combat_missile_weapon(&game->item_types, c, missile)) return true;
    return type_past(game);
}

static bool thrown(cok_adventure *game, const cok_character *c, bool *yes)
{
    if (cok_combat_thrown(&game->item_types, c, yes)) return true;
    return type_past(game);
}

/* 6346:3111: whether c has ammunition for its weapon, and which. */
static bool ammunition(cok_adventure *game, const cok_character *c, size_t *item, bool *has)
{
    if (cok_combat_ammunition(&game->item_types, c, item, has)) return true;
    return type_past(game);
}

/* The cell of record (6beb:0bcb, 0bf3). For no record, 6beb:0c43 looks
 * for NULL among entries 1 to the count and finds it in the entry after
 * the last combatant, at the cell placement last tried there (a monster
 * it removed) or an earlier battle left there, unless an earlier battle's
 * record is still in that entry: then it finds none and reads entry 0's
 * cell (DS:623f, 6240), which is other data, and the run stops. */
bool cok_combat_cell_of(cok_adventure *game, const uint8_t *record, int8_t *x, int8_t *y)
{
    const cok_combat *combat = &game->combat;
    if (record != NULL || combat->count > COK_COMBATANTS) {
        *x = cok_combat_x(combat, record);
        *y = cok_combat_y(combat, record);
        return true;
    }
    if (combat->entry_held[combat->count])
        return undefined(game, "the cell of no target: the entry after the last combatant holds "
                               "an earlier battle's record, so 6beb:0c43 reads entry 0's cell, "
                               "other data (DS:623f)");
    *x = combat->combatant[combat->count].x;
    *y = combat->combatant[combat->count].y;
    return true;
}

/* Clear cells x1-x2 of rows y1-y2 (1128:07e6). */
static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    cok_picture_fill(&game->screen, x1, y1 * 8, (size_t)(x2 - x1 + 1), (size_t)(y2 - y1 + 1) * 8,
                     0);
}

/* Blank row 24 (67b5:0c7b). */
static void clear_row(cok_adventure *game)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
}

/* "Range = N  " on row 23 in light green (1521:0353). */
static void show_range(cok_adventure *game, unsigned range)
{
    char text[32];
    snprintf(text, sizeof text, "Range = %u  ", range);
    cok_text_string(&game->screen, &game->font, text, 0, 0x17, 10, 0);
}

/* The keypad's directions, as the menus give them (scan codes): 0x48 Up,
 * 0x49 PgUp, 0x4d Right, 0x51 PgDn, 0x50 Down, 0x4f End, 0x4b Left, 0x47
 * Home; 8 for any other key. */
static uint8_t direction_of(int key)
{
    static const uint8_t scans[8] = {0x48, 0x49, 0x4d, 0x51, 0x50, 0x4f, 0x4b, 0x47};
    for (uint8_t d = 0; d < 8; ++d)
        if (key == scans[d]) return d;
    return 8;
}

/* The keys a menu reads: first the scan code the caller presses for the
 * player, if any (a 0, then the code), then the adventure's. */
typedef struct {
    cok_keyboard keys;
    int queue[2];
    unsigned count, next;
} pressing;

static int press_read(void *context)
{
    pressing *p = context;
    if (p->next < p->count) return p->queue[p->next++];
    return p->keys.read(p->keys.context);
}

static void press_flush(void *context)
{
    pressing *p = context;
    p->next = p->count;
    if (p->keys.flush != NULL) p->keys.flush(p->keys.context);
}

/* One key from a menu on row 24 (67b5:03e2) after prompt, its items in
 * white and light green, the keypad's keys as directions if keypad;
 * *special is set for a scan code (DS:6e10). *pressed, if not 0, is read
 * first as a scan code, then cleared. Returns -1 if input ended. */
static int menu(cok_adventure *game, const char *prompt, const char *items, bool keypad,
                uint8_t *pressed, bool *special)
{
    cok_adventure_log(game, "menu", items);
    pressing p = {cok_adventure_keyboard(game), {0, 0}, 0, 0};
    if (pressed != NULL && *pressed != 0) {
        p.queue[0] = 0;
        p.queue[1] = *pressed;
        p.count = 2;
        *pressed = 0;
    }
    cok_keyboard keys = {press_read, &p, press_flush, p.keys.pointer};
    return cok_menu_ask(&game->screen, &game->font, prompt, items, 13, 15, 10, keypad,
                        &game->selected, &keys, special);
}

/* Shared helpers. */

void cok_combat_guard(cok_adventure *game, cok_character *c)
{
    cok_combat_end_turn(c);
    if (c->combat != NULL && c->combat->forced == 0) {
        c->combat->guarding = 1;
        cok_camp_notice(game, "Guarding"); /* 6346:1827 */
    }
}

/* The menu (3995:08d5). */

static void append(char out[41], const char *text)
{
    size_t length = strlen(out);
    while (*text != '\0' && length < 40) out[length++] = *text++;
    out[length] = '\0';
}

void cok_combat_command_text(cok_adventure *game, cok_character *c, char out[41])
{
    const uint8_t *r = c->record;
    const cok_combat_record *cr = cok_character_combat(c);
    out[0] = '\0';
    if (cr->movement > 0) append(out, "Move ");
    append(out, "View Aim ");
    if (r[0x142] > 0) append(out, "Use ");
    bool memorized = false;
    for (size_t i = 0; i <= 0x39; ++i)
        if (r[0x1e + i] > 0) memorized = true;
    if (memorized && cr->may_cast != 0 && game->vm.mem4b00[0xe5] == 0) append(out, "Cast ");
    bool cleric = (int8_t)r[0xf9] > 0 || ((int8_t)r[0x101] > 0 && cok_character_former_class(r));
    if (cleric && cr->turns < game->undead) append(out, "Turn ");
    if (r[0x5a] == 5 && game->combat.yelled == NULL) append(out, "Yell ");
    append(out, "Done");
    if (game->debug) append(out, "*"); /* 3995:0aa5, DS:4b51 */
}

/* 3995:08d5: the menu read until a command or a hidden key: scan codes
 * other than Alt-Q, Alt-R, Alt-A, Alt-X and Alt-M count as none. Row 24
 * is cleared after. Returns the key, -1 if input ended. */
static int read_command(cok_adventure *game, cok_character *c, uint8_t *pressed, bool *special)
{
    static const char commands[] = "ACDMQTUVY";
    char text[41];
    cok_combat_command_text(game, c, text);
    int key;
    for (;;) {
        key = menu(game, "", text, true, pressed, special);
        if (key < 0) return -1;
        bool hidden = key == 0x10 || key == 0x13 || key == 0x1e || key == 0x2d || key == 0x32;
        if (*special && !hidden) key = 0;
        if (hidden || key == 0x20 || (key != 0 && strchr(commands, key) != NULL)) break;
    }
    clear_row(game);
    return key;
}

/* Alt-Q or Alt-A (3995:078d): the turn is given back (DS:72ce less 1,
 * initiative 0x14), the computer takes every record in the list
 * (3995:1604), row 24 is cleared and after 200 ms the battle runs c's
 * turn again, the computer's now. */
static void quick(cok_adventure *game, cok_character *c)
{
    --game->combat.turn_index;
    c->combat->initiative = 0x14;
    for (size_t i = 0; i < game->party.count; ++i) cok_combat_auto(game->party.members[i]);
    clear_row(game);
    cok_adventure_wait(game, 200); /* 1962:029c */
}

/* View (546c:0d74) or Use (546c:17f9), then 432f:0e80 and, unless an item
 * used ended the turn, 6346:300f. */
static bool look(cok_adventure *game, cok_character *c, bool use, bool *done)
{
    if (use) {
        game->selected = 2;
        cok_items(game, done);
    } else {
        /* Trade, whose first Escape reads the stale byte, is not offered
         * in combat. */
        cok_sheet(game, COK_SHEET_STALE_UNKNOWN, done);
    }
    if (game->vm.abort) return game->vm.status == COK_ECL_OK;
    if (!cok_combat_weapon_attacks(game, c)) return false;
    return *done || cok_arena_redraw(game);
}

bool cok_combat_commands(cok_adventure *game, cok_character *c, uint8_t pressed)
{
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    if (r[0x189] == 0) {
        cok_combat_end_turn(c);
        return true;
    }
    if (cr->spell > 0) {
        uint8_t spell = cr->spell;
        cr->spell = 0;
        bool cast = false;
        cok_cast_spell(game, spell, false, true, 0, &cast); /* 5b04:1415 */
        if (game->vm.status != COK_ECL_OK) return false;
        cok_combat_end_turn(c);
        return true;
    }
    bool done = false;
    while (!done) {
        game->selected = 1;
        bool special;
        int key = read_command(game, c, &pressed, &special);
        if (key < 0) return true;
        if (!special) {
            switch (key) {
            case 'M':
                if (!cok_combat_move(game, c, &done)) return false;
                break;
            case 'V':
            case 'U':
                if (!look(game, c, key == 'U', &done)) return false;
                break;
            case 'A': {
                cok_combat_aimed res;
                if (!cok_combat_aim(game, c, 0xff, 1, 0, 1, &done, &res)) return false;
                break;
            }
            case 'C':
                if (!cok_combat_cast(game, &done, false, 0)) return false;
                break;
            case 'T':
                if (!cok_combat_turn_undead(game, c)) return false;
                cok_combat_end_turn(c);
                done = true;
                break;
            case 'Y':
                if (!cok_combat_yell(game, c)) return false;
                cok_combat_end_turn(c);
                done = true;
                break;
            case 'D':
                if (!cok_combat_done(game, c, &done)) return false;
                break;
            case ' ':
                for (size_t i = 0; i < game->party.count; ++i) {
                    uint8_t *m = game->party.members[i]->record;
                    if (m[0xe7] < 0x80) m[0x18b] = 0;
                }
                break;
            default: break; /* Q, which the menu never gives. */
            }
        } else if (key == 0x32) {
            combat->magic_on = !combat->magic_on;
            cok_camp_notice(game, combat->magic_on ? "Magic On" : "Magic Off"); /* 6346:1827 */
        } else if (key == 0x10 || key == 0x1e) {
            quick(game, c);
            done = true;
        } else if (key == 0x2d) {
            if (game->helm) {
                if (!cok_combat_gods(game) || !cok_arena_turn(game, c, 3, false)) return false;
                done = true;
            } else {
                cok_camp_notice(game, "That doesn't work"); /* 6346:1827 */
            }
        }
        if (game->vm.abort) return game->vm.status == COK_ECL_OK;
        if (!done) {
            if (!cok_arena_turn(game, c, 2, true)) return false;
            combat->panel = true;
            if (!cok_arena_panel(game, c)) return false;
        }
    }
    return true;
}

/* Move. */

/* 3995:0cad: undo the moves: the movement back, c erased and put back
 * where it started, the screen centred there, its facing back. */
static bool undo(cok_adventure *game, cok_character *c, uint8_t movement, uint8_t facing,
                 int8_t x0, int8_t y0, bool *done)
{
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = c->combat;
    cr->movement = movement;
    if (!cok_arena_erase(game, 0, 0, cok_combat_index(combat, c->record))) return false;
    bool placed;
    if (!cok_combat_place(combat, game->vm.mode, c, (uint8_t)x0, (uint8_t)y0, false, &placed))
        return undefined(game, "a combatant put back where it started reads or writes outside "
                               "its tables (6beb:10f3)");
    *done = !placed;
    if (!cok_arena_centre(game, cok_combat_x(combat, c->record), cok_combat_y(combat, c->record),
                          0, 8))
        return false;
    cr->facing = facing;
    return true;
}

bool cok_combat_charge(cok_adventure *game, cok_character *c, cok_character *target, bool *done)
{
    bool missile, yes = false;
    if (!missile_weapon(game, c, &missile)) return false;
    if (missile && !thrown(game, c, &yes)) return false;
    if (missile && !yes) {
        cok_camp_notice(game, "Not with that weapon"); /* 6346:1827 */
        return true;
    }
    bool go;
    if (!cok_combat_ally(game, c, target, &go)) return false;
    if (!go || game->vm.abort) return true;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    cr->target = target->record;
    bool swept;
    if (!cok_combat_sweep(game, c, target, &swept)) return false;
    if (swept) {
        *done = true;
        return true;
    }
    return cok_combat_book(game, c, target) &&
           cok_combat_attack(game, c, target, false, 0, done);
}

/* A step of Move in dir (0-7). */
static bool move_step(cok_adventure *game, cok_character *c, uint8_t dir, bool *done)
{
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = c->combat;
    if (!cok_arena_pose(game, c, dir, 0, false)) return false;
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, c->record, dir, &occupant, &terrain, &cloud, &puddle))
        return undefined(game, "a size past the footprints is read after them (6beb:0c9d)");
    if (occupant > 0) {
        cok_character *target = combatant(game, occupant, "3995:0f48");
        return target != NULL && cok_combat_charge(game, c, target, done);
    }
    if (terrain == 0) {
        int answer = cok_camp_yes_no(game, "Flee:", 13); /* 67b5:177f */
        if (answer == 'Y') {
            if (!cok_combat_flee(game, c)) return false; /* 432f:0dc7, which returns 1 */
            *done = true;
        } else if (answer == 'N') {
            *done = false;
        }
        return true;
    }
    if (terrain >= COK_COMBAT_TERRAINS)
        return undefined(game, "terrain 0x%02x is past the table (DS:1ee4, 3995:0fb6)", terrain);
    uint8_t base = cok_combat_terrain[terrain].cost;
    uint8_t cost = base == 0xff ? 0xff : (uint8_t)(base * ((dir & 1) != 0 ? 3 : 2));
    if (cost > cr->movement) {
        cok_camp_notice(game, "can't go there"); /* 6346:1827 */
        return true;
    }
    if (!cok_combat_opportunity(game, c, dir)) return false;
    if (c->record[0x189] == 0) {
        cok_combat_end_turn(c);
        *done = true;
        return true;
    }
    if (cr->movement > 0 && !cok_combat_step(game, c, dir)) return false;
    if (c->record[0x189] == 0) {
        cok_combat_end_turn(c);
        *done = true;
    }
    if (!cok_cloud_square(game, c, true)) return false; /* 60f4:0dc3 */
    if (cok_combat_helpless(c)) {
        cok_combat_end_turn(c);
        *done = true;
    }
    return true;
}

static bool combat_move(cok_adventure *game, cok_character *c, bool *done);

/* 3995:0d9c: while it runs, a click on the map is a step (DS:71a6, set at
 * 0da3, clear at 10f7). */
bool cok_combat_move(cok_adventure *game, cok_character *c, bool *done)
{
    game->combat_moving = true;
    bool ok = combat_move(game, c, done);
    game->combat_moving = false;
    return ok;
}

static bool combat_move(cok_adventure *game, cok_character *c, bool *done)
{
    cok_combat *combat = &game->combat;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    uint8_t movement = cr->movement, facing = cr->facing;
    int8_t x0 = cok_combat_x(combat, c->record), y0 = cok_combat_y(combat, c->record);
    *done = false;
    int key = 0x20;
    while (key != 0 && key != 0x0d && key != 'D' && !*done) {
        clear_cells(game, 0x18, 0x18, 0x27, 0x18);
        if (key == 0x20) {
            char prompt[48];
            snprintf(prompt, sizeof prompt, "Move/Attack, Move Left = %u", cr->movement >> 1);
            bool special; /* DS:7199, which nothing reads */
            key = menu(game, prompt, " Done", true, NULL, &special);
            if (key < 0) return true;
        }
        uint8_t dir = 8;
        if (key == 0) {
            if (!undo(game, c, movement, facing, x0, y0, done)) return false;
        } else {
            dir = direction_of(key);
        }
        if (dir < 8 && !move_step(game, c, dir, done)) return false;
        if (game->vm.abort) return game->vm.status == COK_ECL_OK;
        if (key != 0 && key != 0x0d && key != 'D') key = 0x20;
    }
    if (cr->movement < 2) cr->movement = 0;
    return true;
}

/* Attacking an ally (432f:2f12). */

bool cok_combat_ally(cok_adventure *game, cok_character *c, cok_character *target, bool *go)
{
    *go = true;
    if (cok_combat_opposite(target) == c->record[0x18a] || c->record[0x18b] != 0) return true;
    int answer = cok_camp_yes_no(game, "Attack Ally: ", 13); /* 67b5:177f */
    if (answer != 'Y') {
        *go = false;
        return true;
    }
    game->vm.mem7c00[0x333] = 1; /* var 0x7f33; DS:714a, which nothing reads, too */
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *m = game->party.members[i];
        uint8_t *r = m->record;
        if (r[0x188] != 0 || r[0xe7] == 0xb3 || r[0xe7] <= 0x7f) continue;
        r[0x18a] = 1;
        cok_combat_record *cr = record_of(game, m);
        if (cr == NULL) return false;
        cr->target = NULL;
    }
    if (!cok_combat_count_sides(&game->combat, &game->party))
        return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                               "DS:6b2d (6346:268a)");
    return true;
}

/* Aiming. */

/* 432f:3b80's frame: the targets listed (432f:38d2), their count and the
 * one shown, and the cursor's cell before and after a step (432f:39ad). */
typedef struct {
    cok_combat_listing list[COK_COMBATANTS + 1];
    uint8_t count, current;
    int8_t old_x, old_y, new_x, new_y;
} targets;

/* 432f:38d2: the combatants around c within 0x7f (6b30:08d8) that it can
 * attack (432f:11d4), nearest first. */
static bool list_targets(cok_adventure *game, cok_character *c, targets *t)
{
    cok_combat *combat = &game->combat;
    const uint8_t *r = c->record;
    if (!cok_combat_list(combat, cok_combat_x(combat, r), cok_combat_y(combat, r), 0x7f, 0xff,
                         cok_combat_size(combat, r)))
        return undefined(game, "the list of combatants around one (6b30:08d8) reads past its "
                               "tables");
    t->count = 0;
    uint8_t n = combat->listed_count;
    for (unsigned i = 1; i <= n; ++i) {
        cok_character *e = combatant(game, combat->listed[i].index, "432f:3959");
        bool can;
        if (e == NULL || !cok_combat_can_attack(game, c, e->record, &can)) return false;
        if (!can) continue;
        t->list[++t->count] = combat->listed[i];
    }
    return true;
}

/* 432f:39ad: step through the targets (wrapping), the cursor flying from
 * the last to the new one when show. */
static bool next_target(cok_adventure *game, targets *t, bool show, uint8_t step,
                        cok_character **target)
{
    cok_combat *combat = &game->combat;
    if (t->count == 0)
        return undefined(game, "no target is listed, so aiming reads one of its frame never "
                               "written (432f:39ad)");
    if (show) {
        const cok_combatant *e = &combat->combatant[t->list[t->current].index];
        t->old_x = e->x;
        t->old_y = e->y;
    } else if (!cok_arena_redraw_cell(game, t->old_x, t->old_y)) {
        return false;
    }
    t->current = (uint8_t)(t->current + step);
    if (t->current == 0) t->current = t->count;
    if (t->current > t->count) t->current = 1;
    uint8_t n = t->list[t->current].index;
    if (n > COK_COMBATANTS) return undefined(game, "combatant %u is past the table (432f:3a91)", n);
    *target = combatant(game, n, "432f:3a91");
    if (*target == NULL) return false;
    t->new_x = combat->combatant[n].x;
    t->new_y = combat->combatant[n].y;
    if (show) {
        if (!cok_arena_missile(game, t->old_x, t->old_y, t->new_x, t->new_y, 1, 0)) return false;
        t->old_x = t->new_x;
        t->old_y = t->new_y;
    }
    return true;
}

/* 432f:3026: show target (NULL for none) at x, y and read a key. */
static int show_target(cok_adventure *game, cok_character *c, cok_character *target, uint8_t range,
                       uint8_t attack, bool valid, int8_t x, int8_t y, bool *special)
{
    cok_combat *combat = &game->combat;
    char text[41] = "";
    uint16_t distance = 0xff;
    uint8_t see_all = combat->see_all;
    combat->see_all = 1;
    int8_t sx = x, sy = y;
    bool seen;
    bool ok = cok_combat_sight(combat, cok_combat_x(combat, c->record),
                               cok_combat_y(combat, c->record), &sx, &sy, &distance, &seen);
    combat->see_all = see_all;
    if (!ok) {
        undefined(game, "sight (6b30:03f1) reads a cell past the terrain table");
        return -2;
    }
    distance = (uint16_t)(distance >> 1);
    if (attack != 0) show_range(game, distance);
    if (range >= distance && valid) {
        if (attack == 0) {
            append(text, "Target ");
        } else if (target != c) {
            bool missile;
            if (!missile_weapon(game, c, &missile)) return -2;
            if (!missile) {
                append(text, "Target ");
            } else {
                size_t item;
                bool has;
                if (!ammunition(game, c, &item, &has)) return -2;
                if (has) {
                    uint8_t n;
                    bool yes = false;
                    if (!cok_combat_enemies(game, c, 1, &n)) return -2;
                    if (n != 0 && !thrown(game, c, &yes)) return -2;
                    if (n == 0 || yes) append(text, "Target ");
                }
            }
        }
    }
    char items[41] = "Next Prev Manual ";
    append(items, text);
    append(items, "Center Exit");
    if (target != NULL) {
        if (!cok_arena_turn(game, target, 3, true)) return -2;
        combat->panel = true;
        if (!cok_arena_panel(game, target)) return -2;
    }
    int key = menu(game, "Aim:", items, true, NULL, special);
    return key;
}

/* 432f:3219: the pick, with attack 1 attacked, unless it is an ally the
 * player does not want to attack, when *res is cleared. */
static bool strike(cok_adventure *game, cok_character *c, cok_character *target, uint8_t attack,
                   bool *done, cok_combat_aimed *res)
{
    cok_combat *combat = &game->combat;
    *done = true;
    if (attack == 1) {
        if (target == NULL)
            return undefined(game, "an attack aimed at no one reads its record through NULL "
                                   "(432f:2f12, 0000:018a)");
        bool go;
        if (!cok_combat_ally(game, c, target, &go)) return false;
        if (game->vm.abort) return true;
        if (!go) *done = false;
    }
    if (!*done) {
        *res = (cok_combat_aimed){NULL, 0, 0};
        return true;
    }
    res->target = target != NULL ? target->record : NULL;
    if (!cok_combat_cell_of(game, res->target, &res->x, &res->y)) return false;
    combat->cursor = 0;
    if (!cok_arena_centre(game, s8(combat->view_x + 3), s8(combat->view_y + 3), 3, 8))
        return false;
    if (attack != 1) return true;
    bool swept;
    if (!cok_combat_sweep(game, c, target, &swept)) return false;
    if (swept) {
        cok_combat_end_turn(c);
        *done = true;
        return true;
    }
    if (!cok_combat_book(game, c, target)) return false;
    size_t item = 0;
    bool missile, has, yes;
    if (!missile_weapon(game, c, &missile)) return false;
    if (missile) {
        if (!ammunition(game, c, &item, &has)) return false;
        if (has) {
            if (!thrown(game, c, &yes)) return false;
            if (yes) {
                uint8_t d;
                if (!cok_combat_distance(combat, c->record, target->record, &d))
                    return undefined(game, "the distance (6346:2888) lists the combatants around "
                                           "one (6b30:08d8) past its tables");
                if (d == 1) item = 0;
            }
        }
    }
    return cok_combat_attack(game, c, target, false, item, done);
}

/* 432f:33ce: the cursor from x, y, moved with the keypad, until Target (or
 * Enter on no items), Exit or Escape: *valid whether the cell under it
 * may be picked, *picked whether it was; *res the pick, or for none x, y. */
static bool manual_cursor(cok_adventure *game, cok_character *c, uint8_t range, uint8_t attack,
                          uint8_t empty, uint8_t creatures, bool *valid, bool *picked,
                          cok_combat_aimed *res, int8_t x, int8_t y);

/* While the cursor is up, a click on the map steps it (DS:71a5, set at
 * 432f:33d5, clear at 38c7). */
static bool manual(cok_adventure *game, cok_character *c, uint8_t range, uint8_t attack,
                   uint8_t empty, uint8_t creatures, bool *valid, bool *picked,
                   cok_combat_aimed *res, int8_t x, int8_t y)
{
    game->aiming = true;
    bool ok = manual_cursor(game, c, range, attack, empty, creatures, valid, picked, res, x, y);
    game->aiming = false;
    return ok;
}

static bool manual_cursor(cok_adventure *game, cok_character *c, uint8_t range, uint8_t attack,
                          uint8_t empty, uint8_t creatures, bool *valid, bool *picked,
                          cok_combat_aimed *res, int8_t x, int8_t y)
{
    cok_combat *combat = &game->combat;
    int8_t cx = x, cy = y; /* DS:71a3, 71a4 */
    *res = (cok_combat_aimed){NULL, 0, 0};
    combat->cursor = 1;
    combat->cursor_size = 1;
    int key = 0x20;
    uint8_t dir = 8;
    while (key != 0 && key != 0x0d && key != 'E' && key != 'T') {
        if (!cok_arena_centre(game, cx, cy, 2, dir)) return false;
        cx = s8(cx + step_x[dir]);
        cy = s8(cy + step_y[dir]);
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx > 0x31) cx = 0x31;
        if (cy > 0x18) cy = 0x18;
        cok_adventure_flush(game); /* 432f:34ae: after each step of the cursor */
        uint8_t occupant, terrain;
        cok_combat_cell(combat, cx, cy, &occupant, &terrain);
        *valid = false;
        uint16_t distance = 0xff;
        int8_t sx = cx, sy = cy;
        bool seen;
        if (!cok_combat_sight(combat, cok_combat_x(combat, c->record),
                              cok_combat_y(combat, c->record), &sx, &sy, &distance, &seen))
            return undefined(game, "sight (6b30:03f1) reads a cell past the terrain table");
        if (seen) {
            *valid = true;
            if (attack != 0) show_range(game, (uint16_t)(distance >> 1));
        } else if (attack != 0) {
            clear_cells(game, 0, 0x17, 0x27, 0x17);
        }
        distance = (uint16_t)(distance >> 1);
        cok_character *target = NULL;
        if (*valid) {
            if (occupant > 0) {
                target = combatant(game, occupant, "432f:3597");
                if (target == NULL) return false;
            } else if (terrain == COK_COMBAT_BODY) {
                for (unsigned k = 0; k < combat->bodies && k < COK_COMBAT_BODIES; ++k)
                    if (combat->body[k].x == cx && combat->body[k].y == cy)
                        target = combat->body[k].character;
            }
        }
        if (target != NULL) {
            combat->panel = true;
            if (!cok_arena_panel(game, target)) return false;
        } else {
            clear_cells(game, 0x17, 1, 0x26, 0x15);
        }
        if (terrain >= COK_COMBAT_TERRAINS)
            return undefined(game, "terrain 0x%02x is past the table (DS:1ee4, 432f:3650)",
                             terrain);
        if (range < distance || cok_combat_terrain[terrain].cost == 0xff) *valid = false;
        if (target != NULL) {
            bool can;
            if (!cok_combat_can_attack(game, c, target->record, &can)) return false;
            if (!can || creatures == 0) *valid = false;
            if (attack == 1) {
                if (target == c || (occupant == 0 && terrain == COK_COMBAT_BODY)) {
                    *valid = false;
                } else {
                    bool missile;
                    if (!missile_weapon(game, c, &missile)) return false;
                    if (missile) {
                        size_t item;
                        bool has;
                        if (!ammunition(game, c, &item, &has)) return false;
                        if (has) {
                            uint8_t n;
                            bool yes = false;
                            if (!cok_combat_enemies(game, c, 1, &n)) return false;
                            if (n > 0 && !thrown(game, c, &yes)) return false;
                            if (n > 0 && !yes) *valid = false;
                        } else {
                            *valid = false;
                        }
                    }
                }
            }
        } else if (empty == 0) {
            *valid = false;
        }
        bool special; /* DS:7199, which nothing reads */
        game->aim_x = cx; /* DS:71a3, 71a4: where a click steps from */
        game->aim_y = cy;
        key = menu(game, "(Use Cursor keys) ", *valid ? "Target" : "Exit", true, NULL, &special);
        if (key < 0) return true;
        dir = 8;
        if (key == 0x0d || key == 'T') {
            *picked = true;
            res->x = cx;
            res->y = cy;
            res->target = target != NULL ? target->record : NULL;
            combat->cursor = 0;
            if (!cok_arena_redraw_cell(game, cx, cy)) return false;
        } else if (direction_of(key) < 8) {
            dir = direction_of(key);
        } else if (key == 0x20) {
            if (!cok_arena_centre(game, cx, cy, 0, 8)) return false;
        } else if (key == 'E' || key == 0) {
            *picked = false;
            if (!cok_arena_redraw_cell(game, cx, cy)) return false;
            *res = (cok_combat_aimed){NULL, x, y};
            if (!cok_arena_centre(game, x, y, 0, 8)) return false;
            combat->cursor = 0;
            if (!cok_arena_redraw_cell(game, x, y)) return false;
        }
    }
    return true;
}

bool cok_combat_aim(cok_adventure *game, cok_character *c, uint8_t range, uint8_t attack,
                    uint8_t empty, uint8_t creatures, bool *done, cok_combat_aimed *res)
{
    cok_combat *combat = &game->combat;
    if (!cok_arena_missile_frame(game, 0x19, 0, 0, false)) return false; /* 6346:1a26 */
    *res = (cok_combat_aimed){NULL, 0, 0};
    *done = false;
    if (range == 0xff) {
        range = 1;
        size_t weapon = c->slots[0];
        if (weapon != 0 && weapon <= c->item_count) {
            uint8_t type = c->items[weapon - 1][0x2e];
            if (type >= COK_ITEM_TYPES) return type_past(game);
            range = (uint8_t)(game->item_types.type[type][12] - 1);
        }
    }
    if (range == 0 || range == 0xff) range = 1;
    targets t;
    if (!list_targets(game, c, &t)) return false;
    t.current = 1;
    uint8_t step = 0;
    cok_character *target = NULL;
    if (!next_target(game, &t, true, step, &target)) return false;
    int key = 0x20;
    int8_t x = cok_combat_x(combat, target->record), y = cok_combat_y(combat, target->record);
    int8_t x0 = x, y0 = y;
    bool valid = true;
    while (key != 0 && key != 0x0d && key != 'E' && key != 'T') {
        bool special;
        key = show_target(game, c, target, range, attack, valid, x, y, &special);
        if (key == -2) return false;
        if (key < 0) return true;
        if (!special) {
            if (key == 'N' || key == 'P') {
                step = key == 'N' ? 1 : 0xff;
                valid = true;
                if (!next_target(game, &t, true, step, &target)) return false;
            } else if (key == 'M' || key == 'T') {
                bool picked = key == 'T';
                if (key == 'M') {
                    cok_combat_aimed cursor;
                    if (!manual(game, c, range, attack, empty, creatures, &valid, &picked,
                                &cursor, x, y))
                        return false;
                    if (game->vm.abort) return game->vm.status == COK_ECL_OK;
                    *res = cursor;
                    target = cursor.target != NULL ? member_of(game, cursor.target) : NULL;
                    if (cursor.target != NULL && target == NULL)
                        return undefined(game, "the cursor's pick is not in the list (432f:3d51)");
                    x = cursor.x;
                    y = cursor.y;
                    if (valid && picked) {
                        key = 'T';
                        game->selected = 4;
                        res->target = target != NULL ? target->record : NULL;
                    } else {
                        game->selected = 3;
                    }
                } else {
                    res->x = x;
                    res->y = y;
                    res->target = target != NULL ? target->record : NULL;
                }
                if (key == 'T') {
                    *done = true;
                    if (attack == 1 && !strike(game, c, target, attack, done, res)) return false;
                    if (game->vm.abort) return game->vm.status == COK_ECL_OK;
                    if (!*done) {
                        combat->cursor = 0;
                        if (!cok_arena_centre(game, x, y, 0, 8)) return false;
                    }
                }
            } else if (key == 'C') {
                combat->cursor = 1;
                combat->cursor_size = 1;
                if (!cok_arena_centre(game, x, y, 0, 8)) return false;
                combat->cursor = 0;
            } else if (key == 'E') {
                combat->cursor = 0;
                if (!cok_arena_centre(game, x, y, 0, 8)) return false;
            } else if (key == 0) {
                combat->cursor = 0;
                if (!cok_arena_redraw_cell(game, x, y) || !cok_arena_centre(game, x0, y0, 0, 8))
                    return false;
            }
        }
        /* Special keys too: Down is P, keypad + N, shift-F1 T; with no
         * target (after the cursor's Escape), the cell of none. */
        if ((key == 'N' || key == 'P' || key == 'T') &&
            !cok_combat_cell_of(game, target != NULL ? target->record : NULL, &x, &y))
            return false;
    }
    if (attack != 0) clear_cells(game, 0, 0x17, 0x27, 0x17);
    return true;
}

/* Done. */

/* Delay: initiative 1, and c moved to the end of the round's order, each
 * after it moving up, as far as entry 72; the battle then goes on with the
 * one after it. */
static void delay(cok_adventure *game, cok_character *c)
{
    cok_combat *combat = &game->combat;
    c->combat->initiative = 1;
    unsigned b = combat->turn_index;
    /* The entry after 72 would be DS:72ce; the bound stops before it. */
    while (b < 0x48 && combat->turn[b + 1] != NULL) {
        combat->turn[b] = combat->turn[b + 1];
        ++b;
    }
    if (b <= COK_COMBATANTS) combat->turn[b] = c;
    --combat->turn_index;
}

void cok_combat_speed(cok_adventure *game)
{
    int key = ' ';
    while (key != 0 && key != 'E') {
        char prompt[41], items[41] = " ";
        snprintf(prompt, sizeof prompt, "GameSpeed (%u) :", game->speed);
        if (game->speed < 9) append(items, "Slower ");
        if (game->speed > 0) append(items, "Faster ");
        append(items, "Exit");
        bool special; /* DS:7199, which nothing reads */
        key = menu(game, prompt, items, false, NULL, &special);
        if (key < 0) return;
        if (key == 'S') ++game->speed;
        else if (key == 'F') --game->speed;
    }
}

bool cok_combat_done(cok_adventure *game, cok_character *c, bool *done)
{
    *done = false;
    char items[41] = "";
    bool missile, yes = false;
    if (!missile_weapon(game, c, &missile)) return false;
    if (missile && !thrown(game, c, &yes)) return false;
    if (!missile || yes) append(items, "Guard ");
    append(items, "Delay Quit ");
    if (cok_combat_bandage(game, false)) append(items, "Bandage ");
    append(items, "Auto Speed Exit");
    int key = ' ';
    while (key != 0 && key != 'E' && !*done) {
        bool special; /* DS:7199, which nothing reads */
        key = menu(game, "", items, false, NULL, &special);
        if (key < 0) return true;
        switch (key) {
        case 'G':
            cok_combat_guard(game, c);
            *done = true;
            break;
        case 'D':
            delay(game, c);
            *done = true;
            break;
        case 'Q':
            cok_combat_end_turn(c);
            *done = true;
            break;
        case 'B':
            (void)cok_combat_bandage(game, true);
            cok_combat_end_turn(c);
            *done = true;
            break;
        case 'S': cok_combat_speed(game); break;
        case 'A':
            cok_combat_auto(c);
            clear_row(game);
            cok_adventure_flush(game); /* 1614:045c */
            cok_adventure_wait(game, 200); /* 1962:029c */
            *done = true;
            if (!cok_combat_computer(game, c)) return false;
            break;
        default: break;
        }
        if (game->vm.abort) return game->vm.status == COK_ECL_OK;
    }
    return true;
}

/* Cast (432f:28bd). */

/* Byte column of the spell table (DS:31b3 + 16 * spell). */
static bool spell_byte(cok_adventure *game, uint8_t spell, unsigned column, uint8_t *byte)
{
    uint16_t at = (uint16_t)(0x31b3u + 16u * spell + column);
    if (cok_ds_byte(at, byte)) return true;
    return undefined(game, "spell 0x%02x reads DS:%04x past the spell table (432f:28bd)", spell,
                     at);
}

bool cok_combat_cast(cok_adventure *game, bool *done, bool redrawn, uint8_t spell)
{
    cok_combat *combat = &game->combat;
    cok_character *caster = member_of(game, game->vm.character);
    if (caster == NULL)
        return undefined(game, "casting with no character selected reads through NULL "
                               "(432f:28bd)");
    *done = false;
    if (spell == 0) {
        int picked = cok_magic_pick(game, caster);
        if (picked < 0) return game->vm.status == COK_ECL_OK;
        spell = (uint8_t)picked;
    }
    uint8_t byte;
    if (spell > 0) {
        if (!spell_byte(game, spell, 11, &byte)) return false;
        if (byte == 0) {
            cok_camp_notice(game, "Camp Only Spell"); /* 6346:1827 */
            spell = 0;
        }
    }
    if (!redrawn) {
        if (!cok_arena_redraw(game)) return false;
        combat->show_actions = true;
        combat->panel = true;
        if (!cok_arena_turn(game, caster, 3, true) || !cok_arena_panel(game, caster)) return false;
    }
    if (spell == 0) return true;
    if (!spell_byte(game, spell, 12, &byte)) return false;
    uint8_t time = (uint8_t)(byte / 3);
    if (time == 0) {
        cok_cast_spell(game, spell, redrawn, true, 0, done); /* 5b04:1415 */
        if (game->vm.status != COK_ECL_OK) return false;
        cok_combat_end_turn(caster);
        *done = true;
        return true;
    }
    *done = true;
    if (!cok_arena_say(game, caster, "Begins Casting", 10, true)) return false;
    cok_combat_record *cr = record_of(game, caster);
    if (cr == NULL) return false;
    cr->spell = spell;
    if (cr->initiative > (int8_t)time)
        cr->initiative = (int8_t)(cr->initiative - (int8_t)time);
    else
        cr->initiative = 1;
    unsigned b = combat->turn_index;
    for (;;) {
        if (b + 1 > COK_COMBATANTS)
            return undefined(game, "a full order moves a caster past entry 72, reading DS:72ce as "
                                   "a record (432f:2a34)");
        cok_character *next = combat->turn[b + 1];
        if (next == NULL) break;
        int8_t k = cok_character_combat(next)->initiative;
        if (!(k > cr->initiative ||
              (k == cr->initiative && cok_dice(&game->vm.seed, 1, 2) == 1))) /* 60f4:1216 */
            break;
        combat->turn[b] = next;
        ++b;
    }
    combat->turn[b] = caster;
    --combat->turn_index;
    return true;
}

/* Yell (60f4:0023). */

bool cok_combat_yell(cok_adventure *game, cok_character *c)
{
    game->combat.yelled = c->record;
    char name[16], text[40];
    name_of(c->record, name);
    snprintf(text, sizeof text, "wants to kill %s", name);
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *m = game->party.members[i];
        const uint8_t *r = m->record;
        if (r[0x188] != 0 || r[0x13] <= 3 || r[0x18a] == c->record[0x18a] ||
            cok_combat_helpless(m))
            continue;
        bool made;
        if (!cok_effects_save(&game->effects, m, 4, 0, &made))
            return cok_adventure_effect_failed(game);
        if (made) continue;
        if (!cok_cast_add_effect(game, m, 0x5b, 0, 0xff, false, 0, false, text)) return false;
    }
    return true;
}
