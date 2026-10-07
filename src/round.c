#include "round.h"

#include "adventure.h"
#include "camp.h"

#include <stdio.h>
#include <string.h>

/* DS:1ed6 and DS:1edf: a step in direction 0-7, north first, clockwise; 8
 * for none. */
static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};

/* DS:1dd4: the direction of a line's step, by its y and x steps + 1. */
static const uint8_t line_direction[3][3] = {{7, 0, 1}, {6, 8, 2}, {5, 4, 3}};

/* DS:0db4: the effects that last only through the battle (60f4:1440). */
static const uint8_t battle_only[16] = {0x03, 0x0b, 0x15, 0x17, 0x1b, 0x1e, 0x1f, 0x33,
                                        0x34, 0x35, 0x5b, 0x6a, 0x6b, 0x6f, 0x76, 0x77};

/* DS:200c: the effects that leave a combatant helpless (6346:0cdb). */
static const uint8_t helpless[4] = {0x33, 0x34, 0x35, 0x1f};

static int8_t s8(int value)
{
    return (int8_t)(uint8_t)value;
}

static void name_of(const uint8_t *r, char out[16])
{
    size_t length = r[0] > 15 ? 15 : r[0];
    memcpy(out, r + 1, length);
    out[length] = '\0';
}

static void log_text(cok_adventure *game, const char *kind, const char *text)
{
    cok_adventure_log(game, kind, text);
}

/* Run c's effects for event, ending the run where that fails. */
static bool event(cok_adventure *game, cok_character *c, uint8_t ev)
{
    if (cok_effects_dispatch(&game->effects, c, ev)) return true;
    cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", game->effects.error);
    return false;
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* A record's combat record, which every record has during a battle. */
static cok_combat_record *record_of(cok_adventure *game, cok_character *c)
{
    if (c->combat == NULL)
        undefined(game, "a record with no combat record is read through NULL in combat");
    return c->combat;
}

/* The combat screen (6346 and 6beb), which is not ported: the drawing is
 * logged once a battle. What the drawing routines change in the state of
 * the battle, the view's scrolling, is done. */

enum {
    SCREEN_SHOW = 1,  /* 6beb:12ef */
    SCREEN_PANEL = 2, /* 6346:0af6 */
    SCREEN_CELL = 4,  /* 6beb:02aa */
    SCREEN_MAP = 8,   /* 6beb:096b */
    SCREEN_ERASE = 16, /* 6beb:0500 */
};

static void screen_unported(cok_adventure *game, uint8_t what, const char *text)
{
    if ((game->combat.screen_logged & what) != 0) return;
    game->combat.screen_logged |= what;
    log_text(game, "unported", text);
}

/* 6beb:096b: scroll toward x, y moved a step in dir (6beb:07a9) unless
 * within margin of the centre, then draw. */
static void screen_centre(cok_adventure *game, int8_t x, int8_t y, uint8_t margin, uint8_t dir)
{
    uint8_t d = dir > 8 ? 8 : dir;
    cok_combat_scroll(&game->combat, s8(x + step_x[d]), s8(y + step_y[d]), margin);
    screen_unported(game, SCREEN_MAP, "the combat map's drawing (6beb:096b)");
}

/* 6beb:12ef: show c's turn, with the cursor if asked: the map's cursor
 * takes its footprint (map +4, +5), and if its actions are shown
 * (DS:71ad) the map is centred on it; then the cursor is cleared. */
static void screen_show(cok_adventure *game, cok_character *c, uint8_t margin, uint8_t cursor)
{
    cok_combat *combat = &game->combat;
    combat->cursor = cursor;
    combat->cursor_size = cok_combat_size(combat, c->record);
    if (combat->show_actions)
        screen_centre(game, cok_combat_x(combat, c->record), cok_combat_y(combat, c->record),
                      margin, 8);
    combat->cursor = 0;
    combat->cursor_size = 1;
    screen_unported(game, SCREEN_SHOW, "the combatant's highlight (6beb:12ef)");
}

/* 6346:0af6: the side panel, drawn while DS:71ac is set, which it
 * clears. */
static void screen_panel(cok_adventure *game, cok_character *c)
{
    (void)c;
    game->combat.panel = false;
    screen_unported(game, SCREEN_PANEL, "the side panel (6346:0af6)");
}

/* 6beb:02aa: redraw cell x, y and the combatant on it. */
static void screen_cell(cok_adventure *game, int8_t x, int8_t y)
{
    (void)x, (void)y;
    screen_unported(game, SCREEN_CELL, "a cell's redraw (6beb:02aa)");
}

/* 6beb:0500: erase combatant n's icon. */
static void screen_erase(cok_adventure *game, uint8_t n)
{
    (void)n;
    screen_unported(game, SCREEN_ERASE, "an icon's erasing (6beb:0500)");
}

/* The shared helpers. */

void cok_combat_end_turn(cok_character *c)
{
    if (c->combat == NULL) return;
    c->combat->initiative = 0;
    c->combat->spell = 0;
    c->combat->guarding = 0;
    c->combat->movement = 0;
}

bool cok_combat_damage(cok_combat *combat, cok_character *c, uint8_t damage, uint8_t mode)
{
    uint8_t *r = c->record;
    uint8_t over = 0, left = 0, hp = r[0x197];
    if (hp < damage)
        over = (uint8_t)(damage - hp);
    else
        left = (uint8_t)(hp - damage);
    if (over > 9 || (left == 0 && r[0x188] == 1)) {
        r[0x188] = 6;
    } else if (over > 0) {
        r[0x188] = 5;
        if (mode == 5) {
            if (c->combat == NULL) return false;
            c->combat->dying = over;
        }
    } else if (left == 0) {
        r[0x188] = 4;
    }
    if (r[0x188] == 0 || r[0x188] == 1) {
        r[0x197] = left;
        return true;
    }
    r[0x189] = 0;
    r[0x197] = 0;
    if (mode != 5) return true;
    if (r[0x18a] > 1 || c->combat == NULL) return false;
    --combat->sides[r[0x18a]];
    c->combat->initiative = 0;
    return true;
}

bool cok_combat_bandage(cok_adventure *game, bool bandage)
{
    bool found = false;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (cok_character_combat(c)->not_party != 0 || r[0x18a] != 0 || r[0x188] != 5) continue;
        found = true;
        if (!bandage) continue;
        r[0x188] = 4;
        if (c->combat != NULL) c->combat->dying = 0;
        cok_camp_say(game, r, "is bandaged", true); /* 6346:1883 */
        bandage = false;
    }
    return found;
}

bool cok_combat_helpless(const cok_character *c)
{
    bool found = false;
    for (size_t k = 0; k < sizeof helpless; ++k)
        if (cok_character_find_effect(c, helpless[k]) != NULL) found = true;
    return found;
}

uint8_t cok_combat_opposite(const cok_character *c)
{
    return c->record[0x18a] == 0 ? 1 : 0;
}

bool cok_combat_movement(cok_adventure *game, cok_character *c, uint8_t *movement)
{
    const uint8_t *r = c->record;
    uint8_t m = r[0x198];
    if (r[0x18a] == 0) m = (uint8_t)(m + game->vm.mem7c00[0x372]); /* var 0x7f72 */
    if (m < 1 || m > 0x60) m = 1;
    game->effects.rolls.rate = (uint8_t)(m * 2);
    if (!event(game, c, 0x12)) return false;
    *movement = game->effects.rolls.rate;
    return true;
}

uint8_t cok_combat_half_attacks(uint8_t round, uint8_t n)
{
    if ((round & 1) != 0) ++n;
    return (uint8_t)(n >> 1);
}

/* The readied weapon's item type, or false for one past ITEMS. */
static bool weapon_type(const cok_item_types *types, const cok_character *c, const uint8_t **type)
{
    *type = NULL;
    size_t weapon = c->slots[0];
    if (weapon == 0 || weapon > c->item_count) return true;
    uint8_t t = c->items[weapon - 1][0x2e];
    if (t >= COK_ITEM_TYPES) return false;
    *type = types->type[t];
    return true;
}

bool cok_combat_missile_weapon(const cok_item_types *types, const cok_character *c,
                               bool *missile)
{
    const uint8_t *type;
    *missile = false;
    if (!weapon_type(types, c, &type)) return false;
    *missile = type != NULL && type[12] > 1;
    return true;
}

bool cok_combat_ammunition(const cok_item_types *types, const cok_character *c, size_t *item,
                           bool *has)
{
    const uint8_t *type;
    *item = 0;
    *has = false;
    if (!weapon_type(types, c, &type)) return false;
    uint8_t flags = type != NULL ? type[14] : 0;
    if (type != NULL) {
        if ((flags & 0x10) != 0) *item = c->slots[0];
        if ((flags & 8) != 0) {
            if ((flags & 1) != 0) *item = c->slots[11];
            if ((flags & 0x80) != 0) *item = c->slots[12];
        }
    }
    if (*item > c->item_count) *item = 0;
    *has = *item != 0 || flags == 0x0a;
    return true;
}

static bool item_type_past(cok_adventure *game)
{
    return undefined(game, "an item type past ITEMS reads past the table (DS:5886)");
}

bool cok_combat_weapon_attacks(cok_adventure *game, cok_character *c)
{
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    cok_rolls *rolls = &game->effects.rolls;
    uint8_t old = r[0x18f];
    r[0x18f] = r[0x10b];
    bool missile, has = false;
    size_t item = 0;
    if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return item_type_past(game);
    if (missile && !cok_combat_ammunition(&game->item_types, c, &item, &has))
        return item_type_past(game);
    if (has) {
        const uint8_t *type;
        weapon_type(&game->item_types, c, &type);
        rolls->rate = type[5] < 2 ? 2 : type[5];
    } else {
        rolls->rate = r[0x18f];
    }
    if (!event(game, c, 0x12)) return false;
    uint8_t n = cok_combat_half_attacks(rolls->round, rolls->rate);
    if (has && item != 0) {
        uint8_t count = c->items[item - 1][0x39], most = count > 1 ? count : 1;
        if (most < n && count > 0) n = most;
    }
    r[0x18f] = cr->attacked == 0 || n < old ? n : old;
    return true;
}

bool cok_combat_fastest_enemy(cok_adventure *game, cok_character *c, uint8_t *fastest)
{
    *fastest = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *other = game->party.members[i];
        if (cok_combat_opposite(c) != other->record[0x18a] || other->record[0x189] == 0)
            continue;
        uint8_t m;
        if (!cok_combat_movement(game, other, &m)) return false;
        m = (uint8_t)(m >> 1);
        if (m > *fastest) *fastest = m;
    }
    return true;
}

void cok_combat_auto(cok_character *c)
{
    c->record[0x18b] = 1;
    if (c->combat != NULL && c->combat->target != NULL &&
        c->combat->target[0x18a] == c->record[0x18a])
        c->combat->target = NULL;
}

bool cok_combat_battle_only(cok_adventure *game, cok_character *c)
{
    for (size_t k = 0; k < sizeof battle_only; ++k) {
        if (cok_effects_remove(&game->effects, c, NULL, battle_only[k])) continue;
        cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", game->effects.error);
        return false;
    }
    if (cok_character_find_effect(c, 0x4d) != NULL && c->record[0xe7] == 0xb3)
        c->record[0x18a] = 0;
    return true;
}

bool cok_combat_leave(cok_adventure *game, cok_character *c, uint8_t status, const char *text)
{
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    if (r[0x189] == 0) return true;
    uint8_t n = cok_combat_index(combat, r);
    screen_show(game, c, 3, 0);
    cok_camp_say(game, r, text, true); /* 6346:1883 */
    r[0x189] = 0;
    r[0x188] = status;
    if (status != 3) r[0x197] = 0;
    screen_erase(game, n);
    if (n == 0)
        return undefined(game, "a record that is not a combatant leaves the map: its size is "
                               "written over the count (60f4:133c)");
    combat->combatant[n].size = 0;
    if (!cok_combat_occupy(combat))
        return undefined(game, "a footprint is written outside the occupants (6beb:0375)");
    cok_combat_end_turn(c);
    return cok_combat_battle_only(game, c);
}

bool cok_combat_gods(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    cok_camp_notice(game, "The Gods intervene!"); /* 6346:1827 */
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        uint8_t *r = c->record;
        if (r[0x18a] == 1) {
            r[0x189] = 0;
            r[0x188] = 6;
            /* The occupants are not rebuilt: its cells keep its number. */
            uint8_t n = cok_combat_index(combat, r);
            if (n == 0)
                return undefined(game, "a record that is not a combatant is taken off the map: "
                                       "the count is cleared (432f:41e2)");
            combat->combatant[n].size = 0;
        }
        cok_combat_end_turn(c);
    }
    screen_centre(game, s8(combat->view_x + 3), s8(combat->view_y + 3), 0xff, 8);
    return true;
}

bool cok_combat_explode(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    combat->exploding_now = true;
    game->effects.rolls.damage_type = 0;
    if (combat->exploding_count > 0) {
        /* Effect 0x44's handler, which fills the list, is not ported. */
        cok_adventure_fail(game, COK_ECL_EFFECT_FAILED,
                           "the dead that explode (60f4:2375) deal damage in combat (60f4:1db7), "
                           "which is not ported");
        return false;
    }
    combat->exploding_now = false;
    return true;
}

/* The lines, sight and ranges of the map (6b30). */

/* 6b30:0005. */
static int16_t sign(int value)
{
    return (int16_t)(value < 0 ? -1 : value > 0 ? 1 : 0);
}

void cok_combat_line_start(cok_combat_line *line)
{
    line->x = line->x0;
    line->y = line->y0;
    line->dx = (int16_t)(line->x1 - line->x0 < 0 ? line->x0 - line->x1 : line->x1 - line->x0);
    line->dy = (int16_t)(line->y1 - line->y0 < 0 ? line->y0 - line->y1 : line->y1 - line->y0);
    line->step_x = sign(line->x1 - line->x0);
    line->step_y = sign(line->y1 - line->y0);
    line->err = 0;
    line->length = 0;
}

bool cok_combat_line_step(cok_combat_line *line)
{
    bool stepped = false;
    int moved_x = 1, moved_y = 1;
    if (line->dx >= line->dy) {
        if (line->x != line->x1) {
            line->x = (int16_t)(line->x + line->step_x);
            moved_x = line->step_x + 1;
            line->err = (int16_t)(line->err + 2 * line->dy);
            line->length = (uint8_t)(line->length + 2);
            if (line->err >= line->dx) {
                line->err = (int16_t)(line->err - 2 * line->dx);
                line->y = (int16_t)(line->y + line->step_y);
                moved_y = line->step_y + 1;
                ++line->length;
            }
            stepped = true;
        }
    } else if (line->y != line->y1) {
        line->y = (int16_t)(line->y + line->step_y);
        moved_y = line->step_y + 1;
        line->err = (int16_t)(line->err + 2 * line->dx);
        line->length = (uint8_t)(line->length + 2);
        if (line->err >= line->dy) {
            line->err = (int16_t)(line->err - 2 * line->dy);
            line->x = (int16_t)(line->x + line->step_x);
            moved_x = line->step_x + 1;
            ++line->length;
        }
        stepped = true;
    }
    line->direction = line_direction[moved_y][moved_x];
    return stepped;
}

/* The terrain of cell x, y, which must be on the map and in the table. */
static bool terrain_at(const cok_combat *combat, int x, int y, const cok_terrain **terrain)
{
    if (x < 0 || x >= COK_COMBAT_WIDTH || y < 0 || y >= COK_COMBAT_HEIGHT) return false;
    uint8_t t = combat->cells[y][x];
    if (t >= COK_COMBAT_TERRAINS) return false;
    *terrain = &cok_combat_terrain[t];
    return true;
}

bool cok_combat_sight(const cok_combat *combat, int8_t x0, int8_t y0, int8_t *x1, int8_t *y1,
                      uint16_t *range, bool *seen)
{
    uint16_t limit = (uint16_t)(*range * 2u + 1u);
    cok_combat_line line = {.x0 = x0, .y0 = y0, .x1 = *x1, .y1 = *y1};
    cok_combat_line_start(&line);
    /* The original runs a second line, at the eye's height, along the
     * first; it never leaves that height. */
    const cok_terrain *start;
    if (!terrain_at(combat, x0, y0, &start)) return false;
    *seen = false;
    for (;;) {
        const cok_terrain *t;
        if (!terrain_at(combat, line.x, line.y, &t)) return false;
        if ((combat->see_all == 0 && t->block > start->eye) || line.length > limit) {
            *x1 = s8(line.x);
            *y1 = s8(line.y);
            *range = line.length;
            return true;
        }
        if (!cok_combat_line_step(&line)) break;
    }
    *range = line.length;
    *seen = true;
    return true;
}

bool cok_combat_in_arc(int8_t x0, int8_t y0, int8_t x1, int8_t y1, uint8_t dir, bool *in)
{
    *in = false;
    if (x0 < 0 || x0 > 0x31 || y0 < 0 || y0 > 0x18 || x1 < 0 || x1 > 0x31 || y1 < 0 || y1 > 0x18)
        return true;
    if (dir == 0xff) dir = 8;
    if (dir > 8) return false;
    int fx = s8(x0 + step_x[dir]), fy = s8(y0 + step_y[dir]);
    int x = x1, y = y1;
    if ((x1 == x0 && y1 == y0) || (x == fx && y == fy)) {
        *in = true;
        return true;
    }
    switch (dir) {
    case 0:
        if (x >= fx && y <= fx - x + fy) *in = true;
        else *in = x <= fx && y <= x - fx + fy;
        break;
    case 1:
        if (x >= fx && y <= fx - x + fy) *in = true;
        else *in = x >= fx + fy - y && y <= fy;
        break;
    case 2:
        if (x >= fx + fy - y && y <= fy) *in = true;
        else *in = x >= fx + y - fy && y >= fy;
        break;
    case 3:
        if (x >= fx + y - fy && y >= fy) *in = true;
        else *in = x >= fx && y >= x - fx + fy;
        break;
    case 4:
        if (x >= fx && y >= x - fx + fy) *in = true;
        else *in = x <= fx && y >= fx - x + fy;
        break;
    case 5:
        if (x <= fx && y >= fx - x + fy) *in = true;
        else *in = x <= fx + fy - y && y >= fy;
        break;
    case 6:
        if (x <= fx + fy - y && y >= fy) *in = true;
        else *in = x <= fx + y - fy && y <= fy;
        break;
    case 7:
        if (x <= fx + y - fy && y <= fy) *in = true;
        else *in = x <= fx && y <= x - fx + fy;
        break;
    default: *in = true; break;
    }
    return true;
}

/* 6b30:0033: an exchange sort of the list, nearest first, then by
 * direction where the later's is lower and not odder. */
static void sort_listed(cok_combat *combat)
{
    uint8_t n = combat->listed_count;
    if (n <= 1) return;
    for (unsigned i = 1; i + 1 <= n; ++i) {
        for (unsigned j = i + 1; j <= combat->listed_count; ++j) {
            uint8_t di = combat->listed[i].direction, dj = combat->listed[j].direction;
            uint8_t ai = combat->listed[i].distance, aj = combat->listed[j].distance;
            if (aj < ai || (aj == ai && dj < di && dj % 2 <= di % 2)) {
                cok_combat_listing t = combat->listed[i];
                combat->listed[i] = combat->listed[j];
                combat->listed[j] = t;
            }
        }
    }
}

bool cok_combat_list(cok_combat *combat, int8_t x, int8_t y, uint16_t range, uint8_t dir,
                     uint8_t size)
{
    int8_t vx[4], vy[4];
    if (size >= COK_COMBAT_SIZES) return false;
    for (unsigned k = 0; k < 4; ++k) {
        int8_t dx, dy;
        if (cok_combat_footprint(size, k, &dx, &dy)) {
            vx[k] = s8(dx + x);
            vy[k] = s8(dy + y);
        } else {
            vx[k] = -1;
            vy[k] = 0;
        }
    }
    combat->listed_count = 0;
    for (unsigned n = 1; n <= combat->count && n <= COK_COMBATANTS; ++n) {
        const cok_combatant *e = &combat->combatant[n];
        if (e->size == 0) continue;
        if (e->size >= COK_COMBAT_SIZES) return false;
        int8_t tx[4], ty[4];
        for (unsigned k = 0; k < 4; ++k) {
            int8_t dx, dy;
            if (cok_combat_footprint(e->size, k, &dx, &dy)) {
                tx[k] = s8(dx + e->x);
                ty[k] = s8(dy + e->y);
            } else {
                tx[k] = -1;
                ty[k] = 0;
            }
        }
        bool found = false;
        uint16_t best = 0xff;
        unsigned bj = 4, bk = 4;
        for (unsigned k = 0; k < 4; ++k) {
            if (tx[k] < 0) continue;
            for (unsigned j = 0; j < 4; ++j) {
                if (vx[j] < 0) continue;
                bool in, seen;
                if (!cok_combat_in_arc(vx[j], vy[j], tx[k], ty[k], dir, &in)) return false;
                if (!in) continue;
                int8_t sx = tx[k], sy = ty[k];
                uint16_t r = range;
                if (!cok_combat_sight(combat, vx[j], vy[j], &sx, &sy, &r, &seen)) return false;
                if (!seen) continue;
                found = true;
                if (r < best) {
                    best = r;
                    bj = j;
                    bk = k;
                }
            }
        }
        if (!found) continue;
        /* With no length below 0xff the original takes the cells its
         * locals hold from before. */
        if (bj == 4 || combat->listed_count >= COK_COMBATANTS) return false;
        uint8_t d = 0;
        if (dir < 8) {
            d = dir;
        } else {
            /* The first direction whose arc holds the pair; 8 always does. */
            bool in = false;
            while (cok_combat_in_arc(vx[bj], vy[bj], tx[bk], ty[bk], d, &in) && !in && d < 8)
                ++d;
            if (!in) return false;
        }
        ++combat->listed_count;
        combat->listed[combat->listed_count].index = (uint8_t)n;
        combat->listed[combat->listed_count].distance = (uint8_t)best;
        combat->listed[combat->listed_count].direction = d;
    }
    sort_listed(combat);
    return true;
}

/* The combatant record of a listed entry. */
static const uint8_t *listed_record(const cok_combat *combat, unsigned i)
{
    uint8_t n = combat->listed[i].index;
    if (n > COK_COMBATANTS || combat->combatant[n].character == NULL) return NULL;
    return combat->combatant[n].character->record;
}

bool cok_combat_enemies(cok_adventure *game, cok_character *c, uint8_t range, uint8_t *count)
{
    cok_combat *combat = &game->combat;
    const uint8_t *r = c->record;
    *count = 0;
    if (!cok_combat_list(combat, cok_combat_x(combat, r), cok_combat_y(combat, r), range, 0xff,
                         cok_combat_size(combat, r)))
        return undefined(game, "the list of combatants around one (6b30:08d8) reads past its "
                               "tables");
    uint8_t n = combat->listed_count;
    if (n > 0) {
        uint8_t kept = 0;
        for (unsigned i = 1; i <= n; ++i) {
            const uint8_t *other = listed_record(combat, i);
            if (other == NULL)
                return undefined(game, "a listed combatant has no record (6346:26e2)");
            if (other[0x18a] != cok_combat_opposite(c)) continue;
            ++kept;
            combat->listed[kept] = combat->listed[i];
        }
        n = kept;
        combat->listed_count = kept;
    }
    for (unsigned i = 1; i <= n; ++i) combat->enemies[i] = combat->listed[i].index;
    if (cok_character_find_effect(c, 0x5b) != NULL) {
        bool swapped = false;
        for (unsigned i = 1; i <= n && !swapped; ++i) {
            uint8_t e = combat->enemies[i];
            const cok_character *other = combat->combatant[e].character;
            if ((other != NULL ? other->record : NULL) != combat->yelled) continue;
            combat->enemies[i] = combat->enemies[1];
            combat->enemies[1] = e;
            swapped = true;
        }
    }
    *count = n;
    return true;
}

bool cok_combat_distance(cok_combat *combat, const uint8_t *origin, const uint8_t *target,
                         uint8_t *distance)
{
    cok_combat_listing saved[COK_COMBATANTS + 1];
    memcpy(saved, combat->listed, sizeof saved);
    const uint8_t *r = origin;
    combat->see_all = 1;
    bool ok = cok_combat_list(combat, cok_combat_x(combat, r), cok_combat_y(combat, r), 0xff, 0xff,
                              cok_combat_size(combat, r));
    combat->see_all = 0;
    if (ok) {
        unsigned i = 1;
        while (listed_record(combat, i) != target && i < combat->listed_count) ++i;
        *distance = (uint8_t)(combat->listed[i].distance >> 1);
    }
    /* The entries after the count are put back, the count not. */
    memcpy(&combat->listed[1], &saved[1], sizeof saved - sizeof saved[0]);
    return ok;
}

bool cok_combat_direction(const cok_combat *combat, const cok_character *a,
                          const cok_character *b, uint8_t *direction)
{
    int ax = cok_combat_x(combat, a->record), ay = cok_combat_y(combat, a->record);
    int bx = cok_combat_x(combat, b->record), by = cok_combat_y(combat, b->record);
    int dx = bx - ax < 0 ? ax - bx : bx - ax, dy = by - ay < 0 ? ay - by : by - ay;
    /* 0x26a / 256 and 0x6a / 256 are tan 67.5 and 22.5 degrees; the
     * product is a word, taken as signed. */
    int steep = (int16_t)(uint16_t)(0x26a * dx) / 256, flat = (int16_t)(uint16_t)(0x6a * dx) / 256;
    bool diagonal = steep >= dy && flat <= dy;
    for (unsigned d = 0; d < 256; ++d) {
        bool found = false;
        switch (d & 0xff) {
        case 0: found = by <= ay && steep <= dy; break;
        case 2: found = bx >= ax && flat >= dy; break;
        case 4: found = by >= ay && steep <= dy; break;
        case 6: found = bx <= ax && flat >= dy; break;
        case 1: found = by < ay && bx > ax && diagonal; break;
        case 3: found = by > ay && bx > ax && diagonal; break;
        case 5: found = by > ay && bx < ax && diagonal; break;
        case 7: found = by < ay && bx < ax && diagonal; break;
        default: break;
        }
        if (found) {
            *direction = (uint8_t)d;
            return true;
        }
    }
    return false;
}

/* The round. */

bool cok_combat_round_start(cok_adventure *game, cok_character *c)
{
    cok_combat *combat = &game->combat;
    cok_rolls *rolls = &game->effects.rolls;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    cr->spell = 0;
    uint8_t occupant, terrain;
    bool cloud, puddle;
    if (!cok_combat_probe(combat, r, 8, &occupant, &terrain, &cloud, &puddle))
        return undefined(game, "a size past the footprints is read after them (6beb:0c9d)");
    cr->may_cast = !cloud;
    cr->may_use = 1;
    cr->attacked = 0;
    cr->attack_slot = 2;
    if (!cok_combat_weapon_attacks(game, c)) return false;
    rolls->rate = r[0x10c];
    if (!event(game, c, 0x12)) return false;
    r[0x190] = cok_combat_half_attacks(rolls->round, rolls->rate);
    cr->sweeps = r[0xce];
    if (r[0x189] != 0) {
        int8_t bonus = cok_character_dexterity_missile(r);
        uint8_t initiative = (uint8_t)(cok_dice(&game->vm.seed, 1, 6) + bonus);
        if ((int8_t)initiative < 1) initiative = 1;
        /* (side + 1) & var 0x7ecb: bit 0 the party's side surprised, bit 1
         * the enemy's. */
        if (((uint8_t)(r[0x18a] + 1) & (uint8_t)game->vm.mem7c00[0x2cb]) != 0)
            initiative = (uint8_t)(initiative - 6);
        if ((int8_t)initiative < 0 || (int8_t)initiative > 20) initiative = 0;
        cr->initiative = (int8_t)initiative;
    } else {
        cr->initiative = 0;
    }
    return cok_combat_movement(game, c, &cr->movement);
}

void cok_combat_turn_order(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    memset(combat->turn, 0, sizeof combat->turn);
    for (size_t m = 0; m < game->party.count; ++m) {
        cok_character *c = game->party.members[m];
        int8_t key = cok_character_combat(c)->initiative;
        unsigned i = 1;
        for (bool go = true; i < 0x41 && go;) {
            const cok_character *at = combat->turn[i];
            int8_t k = at != NULL ? cok_character_combat(at)->initiative : -1;
            if (key > k || (key == k && cok_dice(&game->vm.seed, 1, 2) == 1))
                go = false;
            else
                ++i;
        }
        cok_character *moved = combat->turn[i];
        combat->turn[i] = c;
        for (unsigned j = i; moved != NULL && j < COK_COMBATANTS;) {
            cok_character *next = combat->turn[++j];
            combat->turn[j] = moved;
            moved = next;
        }
    }
}

/* 3afb:004b, the computer's turn, and 3995:0573, the player's commands,
 * are not ported: the turn is logged and ends (6346:2964). The player's
 * commands, for one that can act and is not casting, start by selecting
 * the menu's first item (DS:6e0f). With --combat gods the player then
 * presses Alt-X (scan code 0x2d), and in a game started with Helm the
 * cheat runs (432f:41e2), showing the combatant (6beb:12ef) after;
 * without, 432f:41e2 returns at once. */
static bool act(cok_adventure *game, cok_character *c, bool computer)
{
    char name[16], text[48];
    name_of(c->record, name);
    snprintf(text, sizeof text, "%s (initiative %d)", name, c->combat->initiative);
    log_text(game, "turn", text);
    if (!computer && c->record[0x189] != 0 && c->combat->spell == 0) {
        game->selected = 1;
        if (game->combat_stub == COK_COMBAT_GODS && game->helm) {
            if (!cok_combat_gods(game)) return false;
            screen_show(game, c, 3, 0);
            return true;
        }
    }
    cok_combat_end_turn(c);
    return true;
}

bool cok_combat_turn(cok_adventure *game, cok_character *c)
{
    cok_combat *combat = &game->combat;
    uint8_t *r = c->record;
    cok_combat_record *cr = record_of(game, c);
    if (cr == NULL) return false;
    cr->hits = 0;
    cr->turning = 0;
    cr->guarding = 0;
    if (!event(game, c, 7)) return false;
    if (cr->initiative <= 0) return true;
    if (cr->initiative == 0x14) cr->initiative = 0x13;
    game->vm.character = r;
    bool visible = r[0x18a] == 0;
    if (!visible && !cok_combat_visible(combat, r, false, &visible))
        return undefined(game, "a size past the footprints is read after them (6beb:06ef)");
    combat->show_actions = visible;
    screen_show(game, c, 2, 1);
    char error[300];
    if (!cok_character_stats(c, &game->item_types, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    combat->panel = true;
    screen_panel(game, c);
    if (!event(game, c, 0x0f)) return false;
    if (cr->spell == 0 && !event(game, c, 0x15)) return false;
    if (cr->initiative > 0 && !act(game, c, r[0x18b] != 0)) return false;
    screen_cell(game, cok_combat_x(combat, r), cok_combat_y(combat, r));
    return true;
}

/* 60f4:0dc3 with flag 0, at the end of a round: one that can act and
 * stands in a damaging cloud (terrain 0x1d) takes 1d6 (60f4:1261) through
 * 60f4:1db7, which in combat is not ported. Only spells, not yet ported,
 * put clouds on the map. */
static bool cloud(cok_adventure *game, cok_character *c)
{
    if (c->record[0x189] == 0) return true;
    uint8_t occupant, terrain;
    bool green, puddle;
    if (!cok_combat_probe(&game->combat, c->record, 8, &occupant, &terrain, &green, &puddle))
        return undefined(game, "a size past the footprints is read after them (6beb:0c9d)");
    if (puddle) log_text(game, "unported", "a damaging cloud's damage (60f4:0dc3)");
    return true;
}

bool cok_combat_end_round(cok_adventure *game, bool *done)
{
    cok_combat *combat = &game->combat;
    if (!cok_adventure_pass_time(game, 1, 1)) return false; /* 57e4:0549 */
    ++game->effects.rolls.round;
    cok_combat_enemy_health(combat, &game->party);
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        if (!event(game, c, 0x13) || !cloud(game, c)) return false;
        if (c->record[0x188] != 5) continue;
        cok_combat_record *cr = record_of(game, c);
        if (cr == NULL) return false;
        if (++cr->dying > 9) c->record[0x188] = 6;
    }
    if (cok_combat_bandage(game, false)) cok_camp_notice(game, "Your Teammate is Dying");
    if (!cok_combat_count_sides(combat, &game->party))
        return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                               "DS:6b2d (6346:268a)");
    screen_centre(game, s8(combat->view_x + 3), s8(combat->view_y + 3), 0xff, 8);
    if (combat->sides[0] == 0 || combat->sides[1] == 0 ||
        game->effects.rolls.round >= combat->round_limit)
        *done = true;
    /* The demo (DS:4b4b), never set in CoK, would not ask. */
    if (combat->sides[0] > 0 && combat->sides[1] == 0) {
        int answer = cok_camp_yes_no(game, "Continue Battle:", 13); /* 67b5:177f */
        if (answer == 'Y') *done = false;
    }
    return true;
}

/* The round's order, logged. */
static void log_order(cok_adventure *game)
{
    char text[2048]; /* 72 records of up to 15 letters each */
    size_t used = (size_t)snprintf(text, sizeof text, "%u:", game->effects.rolls.round + 1u);
    const char *separator = " ";
    for (unsigned i = 1; i <= COK_COMBATANTS && game->combat.turn[i] != NULL; ++i) {
        const cok_character *c = game->combat.turn[i];
        char name[16];
        name_of(c->record, name);
        int n = snprintf(text + used, sizeof text - used, "%s%s %d", separator, name,
                         cok_character_combat(c)->initiative);
        if (n < 0 || used + (size_t)n >= sizeof text) break;
        used += (size_t)n;
        separator = ", ";
    }
    log_text(game, "round", text);
}

/* eclplay's --combat won, fled and lost, in place of the rounds: every
 * record against the party (+0x18a 1) drops, every party record that can
 * act flees, or the whole party dies. */
static void stub(cok_adventure *game)
{
    static const char *const outcome[] = {"", "won", "fled", "lost"};
    cok_combat_stub how = game->combat_stub;
    log_text(game, "combat", outcome[how]);
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *r = game->party.members[i]->record;
        bool member =
            cok_character_combat(game->party.members[i])->not_party != 1 && r[0x18a] != 1;
        if (how == COK_COMBAT_WON && r[0x18a] == 1) {
            r[0x188] = 6;
            r[0x189] = 0;
        } else if (how == COK_COMBAT_LOST && member) {
            r[0x188] = 6;
            r[0x189] = 0;
            r[0x197] = 0;
        } else if (how == COK_COMBAT_FLED && member && r[0x189] != 0) {
            r[0x188] = 3;
            r[0x189] = 0;
        }
    }
}

static bool rounds(cok_adventure *game)
{
    cok_combat *combat = &game->combat;
    cok_ecl *vm = &game->vm;
    if (!cok_combat_setup(game)) return false;
    if (game->hooks.battlefield != NULL) game->hooks.battlefield(game, game->hooks.context);
    bool done = combat->sides[0] == 0 || combat->sides[1] == 0;
    for (size_t i = 0; i < game->party.count; ++i)
        if (!event(game, game->party.members[i], 0x18)) return false;
    if (game->combat_stub == COK_COMBAT_WON || game->combat_stub == COK_COMBAT_FLED ||
        game->combat_stub == COK_COMBAT_LOST) {
        stub(game);
        /* As the end of the round would count them. */
        if (!cok_combat_count_sides(combat, &game->party))
            return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                                   "DS:6b2d (6346:268a)");
        done = true;
    }
    while (!done && !vm->abort) {
        if (!cok_combat_count_sides(combat, &game->party))
            return undefined(game, "a record on a side other than 0 or 1 (+0x18a) counts past "
                                   "DS:6b2d (6346:268a)");
        for (size_t i = 0; i < game->party.count; ++i)
            if (!cok_combat_round_start(game, game->party.members[i])) return false;
        vm->mem7c00[0x2cb] = 0; /* var 0x7ecb */
        cok_combat_turn_order(game);
        log_order(game);
        cok_character *c = combat->turn[1];
        combat->turn_index = 1;
        while (c != NULL && !vm->abort) {
            if (!cok_combat_turn(game, c) || !cok_combat_explode(game)) return false;
            ++combat->turn_index;
            c = combat->turn_index > COK_COMBATANTS ? NULL : combat->turn[combat->turn_index];
        }
        if (vm->abort || !cok_combat_end_round(game, &done)) break;
    }
    if (vm->status != COK_ECL_OK) return false;
    return cok_combat_end(game);
}

bool cok_combat_battle(cok_adventure *game)
{
    /* DS:6e3a; the mode is 5 from setup on. */
    game->combat_targets = true;
    game->effects.in_battle = true;
    game->combat.screen_logged = 0;
    bool ok = rounds(game);
    game->effects.in_battle = false;
    game->combat_targets = false;
    return ok;
}

const cok_ds_table cok_round_tables[] = {
    {0x1dd4, sizeof line_direction, 1, &line_direction[0][0]},
    {0x0db4, sizeof battle_only, 1, battle_only},
    {0x200c, sizeof helpless, 1, helpless},
};
const size_t cok_round_table_count = sizeof cok_round_tables / sizeof *cok_round_tables;
