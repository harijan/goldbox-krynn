#include "modify.h"

#include "camp.h"
#include "sheet.h"
#include "start.h"
#include "train.h"

#include <stdio.h>
#include <string.h>

/* The keypad as directions (DS:2010), from '1'. */
static const uint8_t keypad_scans[9] = {0x4f, 0x50, 0x51, 0x4b, 0x20, 0x4d, 0x47, 0x48, 0x49};

static uint8_t ds(uint16_t offset)
{
    uint8_t byte = 0;
    cok_ds_byte(offset, &byte);
    return byte;
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

uint8_t cok_modify_least(const uint8_t *c)
{
    uint8_t levels = 0, classes = 0, con = 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t level = c[0xf9 + i];
        if ((int8_t)level <= 0) continue;
        levels = (uint8_t)(levels + level);
        ++classes;
        if (i == 4 || i == 7) ++levels;
        con = (uint8_t)(con + (uint8_t)cok_train_constitution(c, i));
    }
    int bonus = (int8_t)con;
    if (bonus < 0 && levels <= classes + -bonus) return 1;
    return (uint8_t)((levels + bonus) / classes);
}

bool cok_modify_most(const uint8_t *c, const uint8_t *selected, uint8_t *most)
{
    uint8_t sum = 0, classes = 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t level = c[0xf9 + i];
        if ((int8_t)level <= 0) continue;
        int8_t b;
        if (!cok_train_hit_die(selected, i, &b)) return false;
        ++classes;
        uint8_t top = ds((uint16_t)(0x3903 + i));
        if (level < top) {
            int dice;
            switch (i) {
            case 0: dice = ((int8_t)c[0x5d] <= 4 ? level + 1 : level) * (b + 8); break;
            case 1: dice = level * (b + 8); break;
            case 2: case 3: dice = level * (b + 10); break;
            case 7: dice = (level + 1) * (b + 10); break;
            case 4: dice = (level + 1) * (b + 8); break;
            case 5: dice = level * (b + 4); break;
            default: dice = level * (b + 6); break;
            }
            sum = (uint8_t)(sum + dice);
        } else {
            /* Set, not added: the classes before are lost. */
            uint8_t x = (uint8_t)(level - top + 1);
            static const uint8_t base[8] = {0x48, 0x70, 0x5a, 0x5a, 0x58, 0x2c, 0x3c, 0x5a};
            static const uint8_t times[8] = {2, 0, 3, 3, 2, 1, 2, 3};
            sum = (uint8_t)(x * times[i] + base[i]);
        }
    }
    *most = (uint8_t)(sum / classes);
    return true;
}

static bool most(cok_adventure *game, const uint8_t *c, uint8_t *value)
{
    if (cok_modify_most(c, game->vm.character, value)) return true;
    return undefined(game, "4def:257c's constitution bonus is uninitialized for a constitution "
                           "outside 3-19 (4def:48e9)");
}

/* The field being changed: 0-5 a score, 6 the name, 7 the hit points. */
typedef struct {
    cok_adventure *game;
    cok_character *character;
    uint8_t field, cursor;
} modify;

/* Draw the field (4def:278c), highlighted in light magenta or not. The
 * name shows the character at the cursor in white, a space as "%". */
static void draw_field(modify *m, bool highlight)
{
    cok_adventure *game = m->game;
    uint8_t *c = m->character->record;
    if (m->field <= 5) {
        cok_sheet_ability(game, c, m->field, highlight ? 13 : 10);
    } else if (m->field == 7) {
        cok_sheet_hit_points(game, c, 0x1f, 3, highlight, false);
    } else if (m->field == 6) {
        char name[16];
        size_t length = c[0] > 15 ? 15 : c[0];
        memcpy(name, c + 1, length);
        name[length] = '\0';
        if (highlight) {
            cok_text_clear(&game->screen, &game->font, 1, c[0] + 1, 1, 0); /* 1521:030a */
            cok_text_string(&game->screen, &game->font, name, 1, 1, 13, 0);
            char at[2] = {(char)c[m->cursor], '\0'};
            if (at[0] == ' ') at[0] = '%';
            cok_text_string(&game->screen, &game->font, at, m->cursor, 1, 15, 0);
        } else {
            cok_text_string(&game->screen, &game->font, name, 1, 1, 10, 0);
        }
    }
}

/* The race's limits (DS:39e0 + race * 16): strength's by gender. */
static uint8_t limit(const uint8_t *c, unsigned offset)
{
    return ds((uint16_t)(0x39e0 + (int8_t)c[0x5a] * 16 + (int)offset));
}

static bool constitution_redraw(modify *m)
{
    m->field = 6;
    draw_field(m, false);
    m->field = 4;
    return true;
}

static bool subtract(modify *m)
{
    uint8_t *c = m->character->record;
    if (m->field <= 5) {
        unsigned a = m->field;
        uint8_t *score = &c[0x11 + 2 * a];
        --*score;
        if (a == 0) {
            if (c[0x1c] > 0) {
                --c[0x1c];
                ++*score;
            } else if (*score < limit(c, c[0x109])) {
                *score = limit(c, c[0x109]);
            }
        } else if (*score < limit(c, 4 + 2 * a)) {
            *score = limit(c, 4 + 2 * a);
        }
        uint8_t minimum = ds((uint16_t)(0x3bdf + (int8_t)c[0x5b] * 6 + (int)a));
        if (*score < minimum) *score = minimum;
        if (a == 2 && c[0x11c] > 0) c[0x11c] = 1;
        if (a == 4) {
            uint8_t value;
            if (!most(m->game, c, &value)) return false;
            if (value < c[0x62]) c[0x62] = value;
            c[0x197] = c[0x62];
            constitution_redraw(m);
        }
    } else if (m->field == 7) {
        --c[0x62];
        uint8_t least = cok_modify_least(c);
        if (least > c[0x62]) c[0x62] = least;
        c[0x197] = c[0x62];
    } else if (m->field == 6) {
        m->cursor = m->cursor == 1 ? c[0] : (uint8_t)(m->cursor - 1);
    }
    return true;
}

static bool add(modify *m)
{
    uint8_t *c = m->character->record;
    if (m->field <= 5) {
        unsigned a = m->field;
        uint8_t *score = &c[0x11 + 2 * a];
        ++*score;
        if (a == 0) {
            uint8_t high = limit(c, 2u + c[0x109]);
            if (*score > high) {
                if (*score > 18 &&
                    ((int8_t)c[0xfb] > 0 || (int8_t)c[0x100] > 0 || (int8_t)c[0xfd] > 0) &&
                    limit(c, 4u + c[0x109]) > c[0x1c])
                    ++c[0x1c];
                *score = high;
            }
        } else if (*score > limit(c, 5 + 2 * a)) {
            *score = limit(c, 5 + 2 * a);
        }
        if (a == 2 && c[0x11c] > 0) c[0x11c] = 1;
        if (a == 4) {
            uint8_t least = cok_modify_least(c);
            if (least > c[0x62]) c[0x62] = least;
            c[0x197] = c[0x62];
            constitution_redraw(m);
        }
    } else if (m->field == 7) {
        ++c[0x62];
        uint8_t value;
        if (!most(m->game, c, &value)) return false;
        if (value < c[0x62]) c[0x62] = value;
        c[0x197] = c[0x62];
    } else if (m->field == 6) {
        m->cursor = m->cursor == c[0] ? 1 : (uint8_t)(m->cursor + 1);
    }
    return true;
}

/* Keep (4def:35ea): the spells a day for the wisdom, +0xe8 set, the hit
 * points at full less the constitution's average bonus, and the scores
 * made the base. The bonus's sum is a byte divided unsigned. */
static bool keep(modify *m)
{
    cok_adventure *game = m->game;
    uint8_t *c = m->character->record;
    char error[300];
    if (!cok_character_cleric_spells(m->character, error, sizeof error))
        return undefined(game, error);
    c[0xe8] = 1;
    uint8_t sum = 0, classes = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int8_t level = (int8_t)c[0xf9 + i];
        if (level <= 0) continue;
        int8_t b;
        if (!cok_train_hit_die(game->vm.character, i, &b))
            return undefined(game, "4def:257c's constitution bonus is uninitialized for a "
                                   "constitution outside 3-19 (4def:35ea)");
        uint8_t top = ds((uint16_t)(0x3903 + i));
        if ((uint8_t)level < top)
            sum = (uint8_t)(sum + b * (i == 4 ? level + 1 : level));
        else
            sum = (uint8_t)(sum + b * (top - 1));
        ++classes;
    }
    if (classes == 0) {
        cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                           "Keep divides by no classes (4def:36cb)");
        return false;
    }
    c[0x11b] = (uint8_t)(c[0x62] - sum / classes);
    for (size_t a = 0; a < 6; ++a) c[0x10 + 2 * a] = c[0x11 + 2 * a];
    c[0x1d] = c[0x1c];
    return true;
}

void cok_modify(cok_adventure *game)
{
    if (!cok_start_pick(game)) return;
    cok_character *character = cok_adventure_selected(game);
    if (character == NULL) {
        undefined(game, "Modify with no character selected reads through NULL (4def:28fa)");
        return;
    }
    uint8_t *c = character->record;
    uint16_t low = (uint16_t)(c[0x116] | c[0x117] << 8);
    static const uint16_t fresh[7] = {1500, 1251, 2000, 2001, 2251, 2500, 5000};
    bool allowed = false;
    for (size_t i = 0; i < 7; ++i) allowed = allowed || low == fresh[i];
    if (!allowed || c[0x118] != 0 || c[0x119] != 0 || c[0xd7] != 0) {
        char text[48];
        snprintf(text, sizeof text, "%.*s can't be modified.", c[0] > 15 ? 15 : c[0],
                 (const char *)c + 1);
        cok_adventure_log(game, "print", text);
        cok_adventure_notice(game, text, 14);
        return;
    }
    if (!cok_sheet_draw(game)) return;
    uint8_t scores[6], exceptional = c[0x1c], hit_points = c[0x62], name[16];
    for (size_t a = 0; a < 6; ++a) scores[a] = c[0x11 + 2 * a];
    memcpy(name, c, sizeof name);
    modify m = {game, character, 7, 1};
    draw_field(&m, false);
    m.field = 0;
    draw_field(&m, true);
    cok_keyboard keys = cok_adventure_keyboard(game);
    for (;;) {
        bool special = false;
        int key;
        if (m.field == 6) {
            /* The name reads keys raw (1614:025b), the keypad digits as
             * directions. */
            key = keys.read(keys.context);
            if (key < 0) return;
            if ((key >= '1' && key <= '9') || key == '\\') {
                key = key == '\\' ? 0x37 : keypad_scans[key - '1'];
                special = true;
            } else if (key == 0) {
                key = keys.read(keys.context);
                if (key < 0) return;
                special = true;
            }
            if (key == 0x1b) key = 0;
        } else {
            key = cok_camp_menu(game, "Modify: ", "Add Subtract Keep Exit", true, false, &special);
            if (key < 0) return;
        }
        draw_field(&m, false);
        bool done = false;
        if (special) {
            if (key == 0x53 && m.field == 6 && c[0] > 1) {
                if (m.cursor == c[0]) {
                    --c[0];
                    m.cursor = c[0];
                } else {
                    for (uint8_t i = m.cursor; i < c[0]; ++i) c[i] = c[i + 1];
                    --c[0];
                }
            } else if (key == 0x50) {
                m.field = (uint8_t)(m.field == 7 ? 0 : m.field + 1);
            } else if (key == 0x48) {
                m.field = (uint8_t)(m.field == 0 ? 7 : m.field - 1);
            }
            /* Home (0x47) picks the field the mouse last clicked; with no
             * mouse, none. */
        } else if (key == 'S') {
            if (!subtract(&m)) return;
        } else if (key == 'A') {
            if (!add(&m)) return;
        } else if (key == 0x0d) {
            m.field = (uint8_t)(m.field == 7 ? 0 : m.field + 1);
        } else if (key == 0x08) {
            if (m.cursor > 1 && m.field == 6) {
                uint8_t length = c[0];
                if (m.cursor <= length)
                    for (uint8_t i = m.cursor; ; ++i) {
                        c[i - 1] = c[i];
                        if (i == length) break;
                    }
                --c[0];
                if (m.cursor > c[0]) m.cursor = c[0];
            }
        } else if (key >= 0x20 && key <= 0x7a) {
            if (m.field == 6) {
                if (m.cursor <= 15) {
                    c[m.cursor] = (uint8_t)key;
                    if (++m.cursor > 15) m.cursor = 15;
                    if (m.cursor > c[0]) {
                        ++c[0];
                        c[m.cursor] = ' ';
                    }
                }
            } else if (key == 'E') {
                done = true;
            }
        } else if (key == 0) {
            done = true;
        }
        if (done) {
            /* Exit: the scores, exceptional strength, maximum hit points and
             * name come back; the hit points become the maximum; +0x11c
             * stays as a wisdom changed it. */
            for (size_t a = 0; a < 6; ++a) c[0x11 + 2 * a] = scores[a];
            c[0x1c] = exceptional;
            c[0x62] = hit_points;
            c[0x197] = c[0x62];
            /* As strings: the bytes past the old name keep what was typed. */
            memcpy(c, name, (size_t)(name[0] > 15 ? 15 : name[0]) + 1);
            char error[300];
            if (!cok_character_stats(character, &game->item_types, error, sizeof error))
                undefined(game, error);
            return;
        }
        char error[300];
        if (!cok_character_stats(character, &game->item_types, error, sizeof error)) {
            undefined(game, error);
            return;
        }
        if (!cok_sheet_figures(game, character)) return;
        draw_field(&m, true);
        if (!special && key == 'K') {
            keep(&m);
            return;
        }
    }
}
