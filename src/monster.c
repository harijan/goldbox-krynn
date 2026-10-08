#include "monster.h"

#include "arena.h"
#include "camp.h"
#include "effect.h"
#include "items.h"
#include "round.h"
#include "screen.h"
#include "shop.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_text(cok_adventure *game, const char *kind, const char *text)
{
    cok_adventure_log(game, kind, text);
}

static void name_of(const uint8_t *c, char out[16])
{
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(out, c + 1, length);
    out[length] = '\0';
}

/* The file's number as Str(DS:5782) with a width of 1 makes it: its first
 * digit. */
static char file_digit(const cok_adventure *game)
{
    char text[8];
    snprintf(text, sizeof text, "%u", game->vm.file);
    return text[0];
}

void cok_pool_free(cok_pool *pool)
{
    free(pool->items);
    memset(pool, 0, sizeof *pool);
}

void cok_monster_reset(cok_adventure *game)
{
    game->icon_slot = 8;
    game->sprite_loaded = game->closeup_shown = false;
}

/* LOAD MONSTER. */

/* Read MON<file><kind>.DAX record id; NULL with *size 0 when the record is
 * missing, or for SPC and ITM the file (DS:5884 set). A missing CHA file
 * makes the original ask for the disk and wait: that fails the run. */
static bool monster_part(cok_adventure *game, const char *kind, uint8_t id, uint8_t **data,
                         size_t *size)
{
    char name[16];
    snprintf(name, sizeof name, "MON%c%s", file_digit(game), kind);
    bool no_file;
    *size = 0;
    *data = cok_adventure_find_record(game, name, id, size, &no_file);
    if (*data != NULL || !no_file || strcmp(kind, "CHA") != 0) return true;
    cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
    return false;
}

/* Read monster id into m (4b6d:161b): the record, its effects from SPC
 * and its items from ITM, both in file order. The pointers in the record
 * are cleared; the readied slots (+0x147) keep what the file holds, and
 * no stats are recomputed. Returns false, ending the run, when the
 * monster cannot be loaded: a missing record says "Unable to load
 * monster" and quits to DOS, as the original does. */
static bool read_monster(cok_adventure *game, uint8_t id, cok_character *m)
{
    memset(m, 0, sizeof *m);
    uint8_t *data;
    size_t size;
    if (!monster_part(game, "CHA", id, &data, &size)) return false;
    if (size == 0) {
        free(data);
        log_text(game, "print", "Unable to load monster");
        cok_adventure_prompt_key(game, "Unable to load monster");
        log_text(game, "quit", "to DOS");
        game->quit = true;
        game->vm.abort = true;
        return false;
    }
    if (size < COK_CHARACTER_SIZE) {
        free(data);
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "MON%cCHA record %u has %zu bytes; 4b6d:161b copies 409, past it",
                           file_digit(game), id, size);
        return false;
    }
    memcpy(m->record, data, COK_CHARACTER_SIZE);
    free(data);
    static const unsigned pointers[] = {0x143, 0xe3, 0x17f, 0x183};
    for (size_t i = 0; i < sizeof pointers / sizeof *pointers; ++i)
        memset(m->record + pointers[i], 0, 4);
    if (!monster_part(game, "SPC", id, &data, &size)) return false;
    if (size % COK_EFFECT_SIZE != 0) {
        free(data);
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "MON%cSPC record %u is not whole effects; 4b6d:161b reads past it",
                           file_digit(game), id);
        return false;
    }
    bool ok = true;
    for (size_t at = 0; at < size && ok; at += COK_EFFECT_SIZE) {
        const uint8_t *e = data + at;
        ok = cok_character_add_effect(m, e[0], (uint16_t)(e[1] | e[2] << 8), e[3], e[4] != 0) !=
             NULL;
    }
    free(data);
    if (ok && !monster_part(game, "ITM", id, &data, &size)) return false;
    if (ok && size % COK_ITEM_SIZE != 0) {
        free(data);
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "MON%cITM record %u is not whole items; 4b6d:161b reads past it",
                           file_digit(game), id);
        return false;
    }
    if (ok && size > 0) {
        m->items = (uint8_t (*)[COK_ITEM_SIZE])(void *)data;
        m->item_count = size / COK_ITEM_SIZE;
    } else if (ok) {
        free(data);
    }
    if (!ok) cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
    return ok;
}

/* A copy of the first record: the same bytes, its items and effects in the
 * reverse order, as each is put first in the copy's lists. */
static cok_character *copy_monster(const cok_character *first)
{
    cok_character *copy = calloc(1, sizeof *copy);
    if (copy == NULL) return NULL;
    memcpy(copy->record, first->record, sizeof copy->record);
    if (first->item_count > 0) {
        copy->items = malloc(first->item_count * sizeof *copy->items);
        if (copy->items == NULL) {
            free(copy);
            return NULL;
        }
        for (size_t i = 0; i < first->item_count; ++i)
            memcpy(copy->items[i], first->items[first->item_count - 1 - i], COK_ITEM_SIZE);
        copy->item_count = first->item_count;
    }
    for (const cok_effect *e = first->effects; e != NULL; e = e->next) {
        cok_effect *added = malloc(sizeof *added);
        if (added == NULL) {
            cok_character_free(copy);
            free(copy);
            return NULL;
        }
        *added = *e;
        added->next = copy->effects;
        copy->effects = added;
    }
    return copy;
}

/* Load the combat icons of id into slot (6d21:01d0): CPIC<file> records id
 * and id + 0x80, colour 0 transparent and colour 8 drawn black (see
 * arena.h). */
static bool load_icons(cok_adventure *game, uint8_t id, uint8_t slot)
{
    return cok_arena_load_icon(game, "CPIC", id, slot);
}

/* LOAD MONSTER (2fd3:0465): unless 63 records were loaded since
 * CLEARMONSTERS, append monster op1 (4b6d:161b), scaled by the
 * difficulty, and op2 - 1 copies of it (0 counting as 1), until 63 are
 * loaded, all with icon slot DS:72eb, whose icons it loads from CPIC
 * record op3. */
static void load_monster(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->monsters >= COK_MONSTERS_MAX) {
        log_text(game, "monster", "none, 63 are loaded");
        return;
    }
    uint8_t id = (uint8_t)cok_ecl_value(vm, 0);
    cok_character *first = malloc(sizeof *first);
    if (first == NULL) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
        return;
    }
    if (!read_monster(game, id, first)) {
        cok_character_free(first);
        free(first);
        return;
    }
    uint8_t *r = first->record;
    /* The difficulty, 1-5 (Novice to Champion), scales the hit points of a
     * monster against the party by (d + 1) / 4, as bytes; either at 0 makes
     * both 1. */
    uint16_t difficulty = vm->mem4b00[0x1f4];
    if (r[0x18a] == 1 && difficulty >= 1 && difficulty <= 5) {
        r[0x197] = (uint8_t)(((difficulty + 1u) * r[0x197]) >> 2);
        r[0x62] = (uint8_t)(((difficulty + 1u) * r[0x62]) >> 2);
        if (r[0x197] == 0 || r[0x62] == 0) r[0x197] = r[0x62] = 1;
    }
    if (r[0xda] != 0) ++game->undead;
    uint8_t count = (uint8_t)cok_ecl_value(vm, 1), icon = (uint8_t)cok_ecl_value(vm, 2);
    if (count == 0) count = 1;
    uint8_t slot = game->icon_slot;
    if (!load_icons(game, icon, slot)) {
        cok_character_free(first);
        free(first);
        return;
    }
    /* The original copies the record before it sets the icon slot, then
     * gives every copy the slot too. */
    cok_character *template = first;
    r[0x137] = slot;
    unsigned added = 0;
    cok_character *copy = first;
    for (uint8_t i = 1; copy != NULL; ++i) {
        if (!cok_party_append(&game->party, copy)) {
            cok_character_free(copy);
            free(copy);
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "the party list holds %u records; combat's tables hold no more",
                               COK_PARTY_RECORDS);
            return;
        }
        ++game->monsters;
        ++added;
        if (i >= count || game->monsters >= COK_MONSTERS_MAX) break;
        copy = copy_monster(template);
        if (copy == NULL) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
            return;
        }
        copy->record[0x137] = slot;
    }
    ++game->icon_slot;
    game->monsters_loaded = true;
    char name[16], text[80];
    name_of(r, name);
    snprintf(text, sizeof text, "%u %s, icon %u in slot %u", added, name, icon, slot);
    log_text(game, "monster", text);
}

/* CLEARMONSTERS (2fd3:12fe): forget the monsters loaded, but leave their
 * records in the list, and empty the treasure pool. */
static void clear_monsters(cok_adventure *game)
{
    game->undead = 0;
    game->monsters = 0;
    game->monsters_loaded = false;
    game->icon_slot = 8;
    cok_pool_free(&game->pool);
    log_text(game, "monster", "cleared");
}

/* Turbo Pascal's Real (1a46), in parts: the exponent, the sign and the
 * 40-bit mantissa with its leading 1. */
typedef struct {
    int exponent;
    bool negative;
    uint64_t mantissa;
} real_parts;

static real_parts real_split(cok_real r)
{
    uint64_t m = 0;
    for (int i = 5; i >= 1; --i) m = m << 8 | r.b[i];
    return (real_parts){r.b[0], (r.b[5] & 0x80) != 0, (m & 0x7fffffffffu) | 1ull << 39};
}

static cok_real real_join(real_parts p)
{
    cok_real r = {{0}};
    if (p.exponent == 0) return r;
    r.b[0] = (uint8_t)p.exponent;
    for (int i = 1; i <= 5; ++i) r.b[i] = (uint8_t)(p.mantissa >> (8 * (i - 1)));
    r.b[5] = (uint8_t)((r.b[5] & 0x7f) | (p.negative ? 0x80 : 0));
    return r;
}

cok_real cok_real_from_long(int32_t value)
{
    if (value == 0) return real_join((real_parts){0, false, 0});
    uint32_t n = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int exponent = 0xa0;
    while ((n & 0x80000000u) == 0) {
        n <<= 1;
        --exponent;
    }
    return real_join((real_parts){exponent, value < 0, (uint64_t)n << 8});
}

/* The common end of multiply and divide (1a46:0fc0): p holds the 48-bit
 * result with its top bit at bit 47 or 46, exponent that for bit 47; one
 * shift normalises it, then it rounds on bit 7, ties up. */
static bool real_finish(uint64_t p, int exponent, bool negative, cok_real *out)
{
    if (p < 1ull << 47) {
        p <<= 1;
        --exponent;
    }
    p += 0x80;
    if (p >= 1ull << 48) {
        p = 1ull << 47;
        ++exponent;
    }
    if (exponent <= 0) {
        *out = real_join((real_parts){0, false, 0}); /* underflow: no error */
        return true;
    }
    if (exponent >= 256) return false;
    *out = real_join((real_parts){exponent, negative, p >> 8});
    return true;
}

bool cok_real_multiply(cok_real a, cok_real b, cok_real *out)
{
    real_parts x = real_split(a), y = real_split(b);
    if (x.exponent == 0 || y.exponent == 0) {
        *out = real_join((real_parts){0, false, 0});
        return true;
    }
    uint64_t p;
    const uint64_t low = 0xffffff;
    if ((x.mantissa & low) == 0 || (y.mantissa & low) == 0) {
        /* 1a46:0f23: a 16-bit multiplier times the other's three parts,
         * the lowest byte only by the multiplier's high byte. */
        uint64_t s = (x.mantissa & low) == 0 ? x.mantissa >> 24 : y.mantissa >> 24;
        uint64_t l = (x.mantissa & low) == 0 ? y.mantissa : x.mantissa;
        p = ((s * (l >> 24)) << 16) + s * ((l >> 8) & 0xffff) + (s >> 8) * (l & 0xff);
    } else {
        /* The top 48 bits of the 80-bit product. */
        uint64_t xh = x.mantissa >> 32, xl = x.mantissa & 0xffffffffu;
        uint64_t yh = y.mantissa >> 32, yl = y.mantissa & 0xffffffffu;
        p = (xh * yh << 32) + xh * yl + xl * yh + (xl * yl >> 32);
    }
    return real_finish(p, x.exponent + y.exponent - 0x80, x.negative != y.negative, out);
}

bool cok_real_divide(cok_real a, cok_real b, cok_real *out)
{
    real_parts x = real_split(a), y = real_split(b);
    if (y.exponent == 0) return false;
    if (x.exponent == 0) {
        *out = real_join((real_parts){0, false, 0});
        return true;
    }
    /* 42 quotient bits of the mantissas, by restoring division. */
    uint64_t q = x.mantissa / y.mantissa, r = x.mantissa % y.mantissa;
    for (int i = 0; i < 41; ++i) {
        r <<= 1;
        q <<= 1;
        if (r >= y.mantissa) {
            r -= y.mantissa;
            q |= 1;
        }
    }
    return real_finish(q << 6, x.exponent - y.exponent + 0x81, x.negative != y.negative, out);
}

bool cok_real_trunc(cok_real value, int32_t *out)
{
    real_parts x = real_split(value);
    if (x.exponent > 0x9f) return false;
    if (x.exponent < 0x80) {
        *out = 0;
        return true;
    }
    /* The mantissa's lowest byte is not looked at. */
    uint32_t v = (uint32_t)((x.mantissa >> 8) >> (0xa0 - x.exponent));
    *out = x.negative ? -(int32_t)v : (int32_t)v;
    return true;
}

/* The encounter's sprite. */

uint8_t cok_monster_open_squares(cok_adventure *game, int x, int y, uint8_t direction)
{
    cok_ecl *vm = &game->vm;
    if (vm->mem4b00[0xe6] == 0) {
        vm->mem7c00[0x2c1] = 2;
        return 2;
    }
    /* The steps are bytes and do not wrap; the walls wrap the square. */
    uint8_t open = 0, bx = (uint8_t)x, by = (uint8_t)y;
    while (open < 2 && cok_view_wall(&game->view, direction, (int8_t)bx, (int8_t)by) == 0) {
        ++open;
        switch (direction) {
        case 0: --by; break;
        case 2: ++bx; break;
        case 4: ++by; break;
        case 6: --bx; break;
        default: break;
        }
    }
    return open;
}

/* Show the monster at distance (3775:0575): until the close-up is shown,
 * turn the overhead map off and load the sprite (in a 3D area), or redraw
 * the view to erase it, and in 3D mode draw the sprite's group distance +
 * 1; at distance 0 in 3D mode, outside ENCOUNTER MENU, replace it with the
 * close-up picture. A close-up of a portrait (var 0x7ee1 not 0xff,
 * 3775:0538) is not ported. */
static void show_monster(cok_adventure *game, uint8_t distance)
{
    cok_ecl *vm = &game->vm;
    char text[64];
    if (!game->closeup_shown) {
        if (!game->sprite_loaded) {
            cok_adventure_overhead_off(game); /* DS:6d84, in any area */
            if (vm->mem4b00[0xe6] != 0) {
                cok_adventure_load_sprite(game, game->sprite_id);
                game->sprite_loaded = true;
                game->sprite_shown = true;
            }
        } else {
            game->redraw = true;
            cok_adventure_view(game);
        }
        if (vm->mode == 4 && distance > 2) {
            /* 6961:072e takes groups 1-3 only; a script that set 0x7ec1
             * above 2 would make it say so and quit to DOS. */
            log_text(game, "print", "Illegal range in Show3DSprite.");
            log_text(game, "quit", "to DOS");
            game->quit = true;
            vm->abort = true;
            return;
        }
        if (vm->mode == 4) {
            cok_adventure_draw_sprite(game, distance + 1u);
            snprintf(text, sizeof text, "sprite %u at %u", game->sprite_id, distance);
            log_text(game, "monster", text);
        }
    }
    uint16_t head = vm->mem7c00[0x2e1];
    if (game->closeup_shown && game->closeup_head == head) return;
    /* DS:4b50, which would show it in ENCOUNTER MENU too, is never set. */
    if (distance != 0 || vm->mode != 4 || game->in_encounter) return;
    game->closeup_head = (uint8_t)head;
    game->picture_shown = true;
    game->closeup_shown = true;
    if (head != 0xff) {
        log_text(game, "unported", "the portrait close-up (3775:0538)");
        game->view_replaced = false;
        return;
    }
    cok_adventure_load_picture(game, game->closeup_id);
    cok_adventure_show_frame(game, 0);
    snprintf(text, sizeof text, "picture %u", game->closeup_id);
    log_text(game, "monster", text);
}

/* The distance wanted (var 0x7ec0), at most the open squares ahead, as
 * the current distance (var 0x7ec1); then show the monster there. */
static void place_monster(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    vm->mem7c00[0x2c1] = cok_monster_open_squares(game, vm->map_x, vm->map_y, vm->direction);
    if (vm->mem7c00[0x2c0] < vm->mem7c00[0x2c1]) vm->mem7c00[0x2c1] = vm->mem7c00[0x2c0];
    show_monster(game, (uint8_t)vm->mem7c00[0x2c1]);
}

/* SETUP MONSTER (2fd3:03c9): sprite op1, distance op2, close-up op3. */
static void setup_monster(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    game->sprite_id = (uint8_t)cok_ecl_value(vm, 0);
    vm->mem7c00[0x2c0] = cok_ecl_value(vm, 1);
    game->closeup_id = (uint8_t)cok_ecl_value(vm, 2);
    place_monster(game);
}

/* APPROACH (2fd3:08d6): one square nearer, unless it is at 0. */
static void approach(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (vm->mem7c00[0x2c1] == 0) return;
    --vm->mem7c00[0x2c1];
    show_monster(game, (uint8_t)vm->mem7c00[0x2c1]);
}

/* SPRITE OFF (2fd3:2fe1): erase a sprite drawn over the view. The sprite
 * stays loaded (DS:8830). */
static void sprite_off(cok_adventure *game)
{
    if (!game->sprite_shown) return;
    game->redraw = true;
    cok_adventure_view(game);
    game->sprite_shown = false;
    game->picture_shown = false;
}

bool cok_monster_movement(const cok_party *party, uint8_t *fastest, uint8_t *slowest)
{
    if (party->count == 0) return false;
    *fastest = *slowest = party->members[0]->record[0x198];
    for (size_t i = 0; i < party->count; ++i) {
        const cok_character *c = party->members[i];
        uint8_t m = c->record[0x198];
        if (cok_character_find_effect(c, 0x27) != NULL)
            m = (uint8_t)(m << 1);
        else if (cok_character_find_effect(c, 0x2a) != NULL)
            m >>= 1;
        if (m > *fastest) *fastest = m;
        if (m < *slowest) *slowest = m;
    }
    return true;
}

/* ENCOUNTER MENU (2fd3:23e5). */

/* The result codes stored (op4): the monsters fled, combat, the party got
 * away, talk. */
enum { FLED, FIGHT, AWAY, TALK };

/* Print one of the encounter's lines from the window's top, typed. */
static void encounter_line(cok_adventure *game, const char *text, bool clear)
{
    game->vm.cursor = (cok_text_cursor){1, 0x11};
    cok_adventure_type(game, text, 10, clear);
}

/* ENCOUNTER MENU sprite distance picture result c0 c1 c2 c3 c4 text0 text1
 * text2 flee speed: show the monster as SETUP MONSTER does, then until a
 * result, describe it at its distance (the first text of the distance's,
 * then the next ones, that is not empty) and offer Combat, Wait, Flee and
 * Advance, or Parlay at distance 0 or outside 3D areas. The monsters'
 * reaction to each (c0-c4) decides: 0 they attack, 1 they hold, 2 they are
 * timid, 3 they advance, 4 they want to talk; 5 and up end it with nothing
 * stored. Fleeing needs the party's slowest movement to reach flee, and
 * timid monsters flee from Combat when speed beats its fastest. */
static void encounter_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    game->in_encounter = true;
    game->text_shown = true;
    uint8_t fastest, slowest;
    if (!cok_monster_movement(&game->party, &fastest, &slowest)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "ENCOUNTER MENU with an empty party reads 0000:0198 (3775:1f8b)");
        game->in_encounter = false;
        return;
    }
    game->sprite_id = (uint8_t)cok_ecl_value(vm, 0);
    vm->mem7c00[0x2c0] = cok_ecl_value(vm, 1);
    game->closeup_id = (uint8_t)cok_ecl_value(vm, 2);
    uint16_t result = cok_ecl_address(vm, 3);
    uint8_t reaction[5];
    for (size_t i = 0; i < 5; ++i) reaction[i] = (uint8_t)cok_ecl_value(vm, 4 + i);
    char texts[3][256];
    for (size_t i = 0; i < 3; ++i) memcpy(texts[i], vm->string[i], sizeof texts[i]);
    uint8_t flee = (uint8_t)cok_ecl_value(vm, 12), speed = (uint8_t)cok_ecl_value(vm, 13);
    place_monster(game);
    bool again = true;
    while (again && !vm->abort) {
        again = false;
        bool area = vm->mem4b00[0xe6] != 0;
        uint8_t distance = (uint8_t)vm->mem7c00[0x2c1];
        const char *text = "";
        for (size_t k = 0; k < 3 && text[0] == '\0'; ++k) text = texts[(distance + k) % 3];
        /* Outside 3D areas the window is not cleared, and the text
         * overprints. */
        encounter_line(game, text, area && text[0] != '\0');
        bool parlay = distance == 0 || !area;
        if (game->picture_shown && game->view_replaced && area && game->picture_id != 9)
            cok_adventure_show_picture(game);
        int choice = cok_adventure_horizontal(
            game, "", parlay ? "~COMBAT ~WAIT ~FLEE ~PARLAY" : "~COMBAT ~WAIT ~FLEE ~ADVANCE", 10,
            false);
        if (choice < 0) break;
        char number[16];
        snprintf(number, sizeof number, "%d", choice);
        log_text(game, "choice", number);
        if (parlay && choice == 3) choice = 4;
        if (choice > 4) {
            /* 0xff, for a digit typed as P-Y, which these items have none
             * of, would read the stack below the table. */
            cok_adventure_fail(game, COK_ECL_UNDEFINED, "ENCOUNTER MENU choice %d", choice);
            break;
        }
        uint8_t code = reaction[choice];
        int stored = -1;
        bool nearer = false, wait = false;
        switch (code) {
        case 0: stored = choice == 2 && slowest >= flee ? AWAY : FIGHT; break;
        case 1: case 3: case 4:
            if (choice == 0) stored = FIGHT;
            else if (choice == 2) stored = AWAY;
            else if (distance > 0 && (choice != 1 || code != 1)) nearer = true;
            else if (choice == 4 || (choice == 1 && code == 4)) stored = TALK;
            else wait = true;
            break;
        case 2: stored = choice == 0 && speed <= fastest ? FIGHT : FLED; break;
        default: break;
        }
        if (stored == FLED) {
            cok_ecl_store(vm, result, FLED);
            encounter_line(game, "The monsters flee.", true);
        } else if (stored >= 0) {
            cok_ecl_store(vm, result, (uint16_t)stored);
        } else if (nearer) {
            --vm->mem7c00[0x2c1];
            show_monster(game, (uint8_t)vm->mem7c00[0x2c1]);
            again = true;
        } else if (wait) {
            encounter_line(game, "Both sides wait.", true);
            again = true;
        }
    }
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
    game->in_encounter = false;
}

/* PARLAY a b c d e result (2fd3:2adf): store the operand of the attitude
 * chosen. */
static void parlay(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    int choice = cok_adventure_horizontal(game, " ", "~HAUGHTY ~SLY ~NICE ~MEEK ~ABUSIVE", 10,
                                          false);
    if (choice < 0) return;
    char number[16];
    snprintf(number, sizeof number, "%d", choice);
    log_text(game, "choice", number);
    cok_ecl_store(vm, cok_ecl_address(vm, 5), (uint8_t)cok_ecl_value(vm, (size_t)choice));
}

/* The party's numbers. Each walks the whole list, monsters loaded too. */

/* CHECKPARTY field effect min max average found (2fd3:1517, 14b9): with
 * field 0, whether a record has effect; with the variable of charisma
 * (0x7c19), a thief skill (0x7ca5-0x7cac) or movement (0x7c9f), their
 * least, greatest and average. A field given as a variable (type 1) is
 * its address; anything else stores nothing. */
static void check_party(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t field = vm->operand[0].type == COK_ECL_VAR ? cok_ecl_address(vm, 0)
                                                        : cok_ecl_value(vm, 0);
    uint8_t effect = (uint8_t)cok_ecl_value(vm, 1);
    uint16_t offset = (uint16_t)(field - 0x7c00);
    uint8_t low = 0xff, high = 0, average = 0;
    bool found = false;
    const cok_party *party = &game->party;
    if (offset == 0x8400) {
        for (size_t i = 0; i < party->count && !found; ++i)
            found = cok_character_find_effect(party->members[i], effect) != NULL;
        low = high = 0;
    } else {
        size_t at;
        if (offset == 0x19) at = 0x1b;
        else if (offset >= 0xa5 && offset <= 0xac) at = 0xda + offset - 0xa4u;
        else if (offset == 0x9f) at = 0x198;
        else return;
        uint16_t sum = 0;
        uint8_t n = 0;
        for (size_t i = 0; i < party->count; ++i) {
            uint8_t value = party->members[i]->record[at];
            ++n;
            if (value > high) high = value;
            if (value < low) low = value;
            sum = (uint16_t)(sum + value);
        }
        if (n == 0) {
            vm->status = COK_ECL_DIVIDE_BY_ZERO;
            return;
        }
        average = (uint8_t)((int16_t)sum / n);
    }
    cok_ecl_store(vm, cok_ecl_address(vm, 2), low);
    cok_ecl_store(vm, cok_ecl_address(vm, 3), high);
    cok_ecl_store(vm, cok_ecl_address(vm, 4), average);
    cok_ecl_store(vm, cok_ecl_address(vm, 5), found);
}

/* PARTYSTRENGTH result (2fd3:136c): for each record, a tenth of its
 * mage levels times 8, its THAC0 better than 20 and armour class better
 * than 0 times 5 each, its hit points and its cleric levels times 4,
 * former levels counting for a human who may use them; summed as a byte. */
static void party_strength(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t sum = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        int former = cok_character_former_class(c) ? 1 : 0;
        uint8_t mage = (uint8_t)((int8_t)c[0xfe] + (int8_t)c[0x106] * former);
        uint8_t cleric = (uint8_t)((int8_t)c[0xf9] + (int8_t)c[0x101] * former);
        uint8_t ac = c[0x18d] > 0x3c ? (uint8_t)(c[0x18d] - 0x3c) : 0;
        uint8_t thac0 = c[0x18c] > 0x27 ? (uint8_t)(c[0x18c] - 0x27) : 0;
        unsigned total = c[0x197] + ac * 5u + thac0 * 5u + mage * 8u + cleric * 4u;
        sum = (uint8_t)(sum + (uint8_t)(total / 10));
    }
    cok_ecl_store(vm, cok_ecl_address(vm, 0), sum);
}

/* PARTY SURPRISE party monsters (2fd3:17b6): whether a record is a ranger
 * or a cleric/ranger (class +0x5b 4 or 10), and 0. */
static void party_surprise(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool ranger = false;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t class_ = game->party.members[i]->record[0x5b];
        if (class_ == 4 || class_ == 10) ranger = true;
    }
    cok_ecl_store(vm, cok_ecl_address(vm, 0), ranger);
    cok_ecl_store(vm, cok_ecl_address(vm, 1), 0);
}

/* SURPRISE a b c d (2fd3:1856): two d6 against d + 2 - a and b + 2 - c,
 * as signed bytes, for 1 (the monsters surprised), 2 (the party) or 0.
 * The original stores the result to address 0x2cb, which keeps nothing,
 * where it meant var 0x7ecb; only the dice remain. */
static void surprise(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t o[4];
    for (size_t i = 0; i < 4; ++i) o[i] = (uint8_t)cok_ecl_value(vm, i);
    int8_t party = (int8_t)(o[3] + 2 - o[0]), monsters = (int8_t)(o[1] + 2 - o[2]);
    int8_t first = (int8_t)cok_dice(&vm->seed, 1, 6), second = (int8_t)cok_dice(&vm->seed, 1, 6);
    uint16_t result = 0;
    if (first <= party) result = second > monsters ? 1 : 3;
    if (second <= monsters) result = 2;
    cok_ecl_store(vm, 0x2cb, result);
}

/* ROB (2fd3:229e). */

/* Take the robbed share of c's money and items (3775:1c7c, 1da9). Returns
 * false, ending the run, where the original would misbehave. */
static bool rob_one(cok_adventure *game, cok_character *c, cok_real factor, uint8_t chance)
{
    uint8_t *r = c->record;
    for (unsigned i = 0; i < COK_COINS; ++i) {
        uint8_t *w = r + 0xeb + 2 * i;
        cok_real money;
        int32_t left;
        if (!cok_real_multiply(cok_real_from_long(w[0] | w[1] << 8), factor, &money) ||
            !cok_real_trunc(money, &left)) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED, "ROB overflows a Real (3775:1c7c)");
            return false;
        }
        w[0] = (uint8_t)left;
        w[1] = (uint8_t)((uint32_t)left >> 8);
    }
    /* A heavy item lowers the chance for it and every item after it; each
     * item costs a d100 whether it can be taken or not. */
    unsigned taken = 0;
    for (size_t i = 0; i < c->item_count;) {
        uint8_t *item = c->items[i];
        uint16_t weight = (uint16_t)(item[0x37] | item[0x38] << 8);
        if (weight > 0xff)
            chance = chance > 90 ? (uint8_t)(chance - 90) : 0;
        else if (weight > 0x18)
            chance = chance > 50 ? (uint8_t)(chance - 50) : 0;
        if (cok_dice(&game->vm.seed, 1, 100) > chance) {
            ++i;
            continue;
        }
        if (item[0x34] != 0 && !cok_item_unready(game, item)) return false;
        cok_character_remove_item(c, i); /* 6346:1697, the stats left as they are */
        ++taken;
    }
    char name[16], text[64];
    name_of(r, name);
    snprintf(text, sizeof text, "%s robbed, %u items taken", name, taken);
    log_text(game, "monster", text);
    return true;
}

/* ROB all percent chance: take percent of the money of the selected
 * character, or with all of every record in the list, and each item with
 * the chance, less 50 from an item of 25 or more and 90 from one above
 * 255 on, in Turbo Pascal Reals. */
static void rob(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t all = (uint8_t)cok_ecl_value(vm, 0), percent = (uint8_t)cok_ecl_value(vm, 1);
    uint8_t chance = (uint8_t)cok_ecl_value(vm, 2);
    cok_real factor;
    if (!cok_real_divide(cok_real_from_long((int16_t)(100 - percent)), cok_real_from_long(100),
                         &factor)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "ROB's factor (2fd3:22c2)");
        return;
    }
    if (all == 0) {
        size_t i = cok_party_index(&game->party, vm->character);
        if (i == game->party.count) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "ROB with no character selected reads through NULL (2fd3:229e)");
            return;
        }
        rob_one(game, game->party.members[i], factor, chance);
        return;
    }
    for (size_t i = 0; i < game->party.count && !vm->abort; ++i)
        rob_one(game, game->party.members[i], factor, chance);
}

/* COMBAT (2fd3:191c). */

/* The battle (3995:0172, see round.h), then the end of combat
 * (351b:1968) unless the battle could not be carried out or input ended. */
static void battle(cok_adventure *game)
{
    if (!cok_combat_battle(game) || game->vm.abort) return;
    cok_treasure_end_of_combat(game);
}

/* COMBAT: fight the monsters loaded (DS:8851; a duel, DS:883e, is never
 * set in CoK), or open a shop (0x7f6c) or the temple (0x7ee2), or with
 * neither give out treasure; then the mode for the area, the Look bit of
 * 0x7eca cleared, the sprite forgotten and, unless a shop or temple
 * outside 3D areas, the screen redrawn. */
static void combat(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool area = vm->mem4b00[0xe6] != 0;
    game->big_shown = false;
    bool moving = game->moving;
    game->moving = false;
    bool fought = true;
    if (game->monsters_loaded) {
        uint8_t open = cok_monster_open_squares(game, vm->map_x, vm->map_y, vm->direction);
        if (open < vm->mem7c00[0x2c1]) vm->mem7c00[0x2c1] = open;
        battle(game);
        if (!area && vm->status == COK_ECL_OK) cok_adventure_load_big(game, 0x79);
    } else if (vm->mem7c00[0x36c] == 1) {
        vm->mem7c00[0x36c] = 0;
        cok_shop(game);
        fought = area;
    } else if (vm->mem7c00[0x2e2] == 1) {
        vm->mem7c00[0x2e2] = 0;
        cok_temple(game);
        fought = area;
    } else {
        cok_treasure_end_of_combat(game);
    }
    if (vm->status != COK_ECL_OK && vm->status != COK_ECL_EFFECT_FAILED) {
        game->moving = moving;
        return;
    }
    vm->mode = area ? 4 : 3;
    vm->mem7c00[0x2ca] &= 1;
    game->sprite_loaded = game->closeup_shown = false;
    game->picture_shown = false;
    if (fought) {
        cok_adventure_redraw(game);
        if (!area && !vm->abort && vm->mem4b00[0x138] == 0)
            cok_adventure_mark(game);
    }
    game->moving = moving;
}

bool cok_monster_opcode(cok_adventure *game)
{
    switch (game->vm.opcode) {
    case COK_ECL_LOAD_MONSTER: load_monster(game); return true;
    case COK_ECL_CLEARMONSTERS: clear_monsters(game); return true;
    case COK_ECL_SETUP_MONSTER: setup_monster(game); return true;
    case COK_ECL_APPROACH: approach(game); return true;
    case COK_ECL_SPRITE_OFF: sprite_off(game); return true;
    case COK_ECL_ENCOUNTER_MENU: encounter_menu(game); return true;
    case COK_ECL_PARLAY: parlay(game); return true;
    case COK_ECL_CHECKPARTY: check_party(game); return true;
    case COK_ECL_PARTYSTRENGTH: party_strength(game); return true;
    case COK_ECL_PARTY_SURPRISE: party_surprise(game); return true;
    case COK_ECL_SURPRISE: surprise(game); return true;
    case COK_ECL_ROB: rob(game); return true;
    case COK_ECL_COMBAT: combat(game); return true;
    default: return false;
    }
}
