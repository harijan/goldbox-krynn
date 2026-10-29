#include "queen_modify.h"

#include "data_dark_queen_modify.h"
#include "party.h"
#include "queen.h"

#include <stdio.h>
#include <string.h>

/* The fields Modify steps through, DS:748a in the original: the six scores
 * in the sheet's order, the name and the hit points. */
enum { SCORES = 6, NAME = 6, HIT_POINTS = 7 };

/* The keys the bar gives and what the loop calls them (6a87's tables):
 * the bar's items by index, and each extended key as the bar returns it
 * ((k & 0x7f) + 0x80): Right adds, Left subtracts, Down and End are Next,
 * Home and Up Previous. */
enum { KEY_NEXT = 0, KEY_PREVIOUS, KEY_ADD, KEY_SUB, KEY_KEEP, KEY_EXIT };

typedef struct {
    cok_adventure *game;
    cok_character *ch;
    uint8_t *c;
    const cok_character_layout *L;
    bool creating; /* 1bdb:21dd */
    bool done;     /* DS:c794 */
    unsigned mode; /* DS:748a */
    unsigned cursor; /* DS:c7bf: the name's */
    /* What Modify keeps to give back at Exit (DS:c7c0, c7be, c791, c795). */
    uint8_t scores[SCORES], exceptional, most;
    char name[16];
} modify;

/* The cok_character_layout's fields, by their word or byte. */
static uint8_t score(const modify *m, unsigned k)
{
    return cok_field_u8_at(m->L, m->c, COK_FIELD_SCORE, k);
}

static void set_score(modify *m, unsigned k, uint8_t v)
{
    cok_field_set8_at(m->L, m->c, COK_FIELD_SCORE, k, v);
}

static uint8_t level(const modify *m, unsigned k)
{
    return cok_field_u8_at(m->L, m->c, COK_FIELD_CLASS_LEVEL, k);
}

static unsigned race(const modify *m)
{
    return (unsigned)cok_field_s8(m->L, m->c, COK_FIELD_RACE) & 0xff;
}

static unsigned class_(const modify *m)
{
    return (unsigned)cok_field_s8(m->L, m->c, COK_FIELD_CLASS) & 0xff;
}

/* DS:02b8 + race * 16 and DS:04fd + class * 6: a race's limits and a
 * class's least, past the tables as the original reads them (the table of
 * the classes' ends at 0x563, the next one's): a race or class a record
 * does not have gives the last row. */
static const uint8_t *limits(const modify *m)
{
    unsigned r = race(m);
    return cok_queen_limits[r < 7 ? r : 6];
}

static const uint8_t *minimums(const modify *m)
{
    unsigned k = class_(m);
    return cok_queen_minimums[k < 17 ? k : 16];
}

/* 6a87:0b75 and the end of each field's handler: its row drawn again
 * (6a87:14e6), lit or not: a score by 86b8:096c; the name (cleared when lit,
 * 1a62:05a7, then drawn at 1, 1 in light cyan, else grey) and the hit
 * points (177e:07b4: the number alone, lit light cyan, else by its most). */
static void redraw(modify *m, unsigned field, bool lit)
{
    cok_adventure *game = m->game;
    if (field < SCORES) {
        queen_sheet_score(game, m->c, (int)field, lit);
    } else if (field == NAME) {
        char name[17];
        cok_field_name(m->L, m->c, name);
        if (lit) queen_clear_cells(game, 1, 1, 0x10, 1);
        queen_say(game, 1, 1, name, lit ? 0x0b : 0x07);
    } else if (field == HIT_POINTS) {
        uint8_t hp = cok_field_u8(m->L, m->c, COK_FIELD_HIT_POINTS),
                most = cok_field_u8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS);
        char line[8];
        snprintf(line, sizeof line, "%u", hp);
        queen_say(game, 0x1f, 3, line, lit || hp < most ? 0x0b : 0x07);
    }
}

/* 6a87:000f: the field being edited, with the name's prompt on the bar. */
static void set_mode(modify *m, unsigned mode)
{
    cok_adventure *game = m->game;
    if (mode == NAME && m->mode != NAME) {
        queen_cursor_show(game, false);
        queen_bottom_bar(game);
        queen_prompt_box(game, 0, "Edit Name: Press return when done.", 0x0f, 8);
        redraw(m, NAME, false);
    } else if (m->mode == NAME && mode != NAME) {
        queen_cursor_show(game, true);
    }
    m->mode = mode;
}

/* 6a87:007e: the hit points a character's levels give least (what Add
 * constitution and Sub hit points keep them above): the levels below the
 * top of the dice (DS:640a) added with their constitution's, the others
 * the top's less one, averaged over the classes. */
static uint8_t least_hit_points(const modify *m)
{
    static const uint8_t top[7] = {10, 10, 10, 10, 11, 12, 11}; /* DS:640a */
    int16_t sum = 0, bonus = 0;
    unsigned counted = 0;
    for (unsigned k = 0; k < 7; ++k) {
        unsigned lv = level(m, k);
        if (lv == 0) continue;
        /* 86b8:3a5b: the constitution's table, more for a knight, fighter,
         * paladin or ranger of 17 or more. */
        uint8_t con = score(m, 4), v = cok_queen_constitution_bonus[con & 31];
        if (k >= 1 && k <= 4 && con >= 0x11 && con - 0x11 <= 8) {
            static const uint8_t more[9] = {1, 2, 3, 3, 4, 4, 4, 5, 5};
            v = (uint8_t)(v + more[con - 0x11]);
        }
        int8_t b = (int8_t)v;
        ++counted;
        if (lv < top[k]) {
            sum = (int16_t)(sum + (int16_t)lv);
            if (k == 4 || k == 1) ++sum;
            bonus = (int16_t)(bonus + b * (int16_t)lv);
        } else {
            unsigned extra = (uint8_t)(lv - top[k] + 1);
            switch (k) {
            case 0: sum = (int16_t)(sum + (int16_t)(extra * 2 + 9)); break;
            case 2: case 3: sum = (int16_t)(sum + (int16_t)(extra * 3 + 9)); break;
            case 4: sum = (int16_t)(sum + (int16_t)(extra * 2 + 11)); break;
            case 5: sum = (int16_t)(sum + (int16_t)(extra + 11)); break;
            case 6: sum = (int16_t)(sum + (int16_t)(extra * 2 + 10)); break;
            default: break;
            }
            bonus = (int16_t)(bonus + b * (int16_t)(top[k] - 1));
        }
    }
    if (counted == 0) return 0;
    return (uint8_t)((bonus > 0 ? sum + bonus : sum) / (int16_t)counted);
}

static uint8_t most_hit_points(const modify *m)
{
    static const uint8_t top[7] = {10, 10, 10, 10, 11, 12, 11};
    return cok_character_queen_most_hit_points(m->L, m->c, top);
}

/* The hit points' most set, the hit points with it. */
static void set_most(modify *m, uint8_t v)
{
    cok_field_set8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS, v);
    cok_field_set8(m->L, m->c, COK_FIELD_HIT_POINTS, v);
}

/* 6a87:1599 and 66e5:30d3: the mage's spells. */
void queen_forget_mage_spells(const cok_character_layout *L, uint8_t *c)
{
    for (unsigned id = 1; id <= 0x7e; ++id)
        if (cok_queen_spell_class[id] == 2)
            c[cok_field_offset(L, COK_FIELD_KNOWN_SPELL_BITS) + (id - 1) / 8] &=
                (uint8_t)~(1u << (id - 1) % 8);
}

static void know(const cok_character_layout *L, uint8_t *c, unsigned id)
{
    c[cok_field_offset(L, COK_FIELD_KNOWN_SPELL_BITS) + (id - 1) / 8] |=
        (uint8_t)(1u << (id - 1) % 8);
}

void queen_mage_spells(const cok_character_layout *L, uint8_t *c)
{
    /* The mage's first level's. */
    if (cok_field_u8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0)
        for (unsigned id = 1; id <= 0x7e; ++id)
            if (cok_queen_spell_class[id] == 2 && cok_queen_spell_level[id] == 1) know(L, c, id);
    /* Those of the levels the mage has spells a day of (the mage's row of
     * the spells a day, +0x179, level 2 on), by its order of magic. */
    static const uint8_t first[7][8] = {
        {29, 33, 34}, {46, 47, 49, 52, 54}, {82, 88, 81, 85, 87}, {91, 92}, {112}, {115, 117},
        {121},
    };
    static const uint8_t second[7][8] = {
        {30, 31, 32, 34, 35}, {45, 47, 48, 50, 51, 55}, {84, 83, 85, 87}, {92, 118, 119},
        {113, 114, 111}, {117}, {123},
    };
    bool white = cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER) == 1;
    for (unsigned lv = 2; lv <= 8; ++lv)
        if (cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 2, lv) > 0)
            for (const uint8_t *id = (white ? first : second)[lv - 2]; *id != 0; ++id)
                know(L, c, *id);
    /* A ninth level's: every one. */
    if (cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 2, 9) > 0)
        for (unsigned id = 1; id <= 0x7e; ++id)
            if (cok_queen_spell_class[id] == 2) know(L, c, id);
}

/* The spells a day and the spells known as the levels and scores give
 * them (8bcc:01e7, which 6a87:025e calls first). */
static bool levels_spells(modify *m)
{
    return cok_adventure_not_yet(m->game, "Modify's Keep", "dqk 8bcc:01e7");
}

/* 6a87:025e, Keep: the spells a day recomputed, the mage's spell book made
 * again (the mage's spells forgotten first, 6a87:1599), the hit points'
 * base from their most less the levels' average of constitution's bonus
 * (177e:20ce a level, the top's less one at the top of the dice; a
 * ranger's one level more), the scores kept as the base scores. */
static bool keep(modify *m)
{
    static const uint8_t top[7] = {10, 10, 10, 10, 11, 12, 11}; /* DS:640a */
    if (!levels_spells(m)) return false;
    if (level(m, 5) > 0) {
        queen_forget_mage_spells(m->L, m->c);
        queen_mage_spells(m->L, m->c);
    }
    uint8_t sum = 0;
    unsigned counted = 0;
    for (unsigned k = 0; k < 7; ++k) {
        unsigned lv = level(m, k);
        if (lv == 0) continue;
        int con = cok_character_queen_constitution(m->L, m->c, k);
        unsigned times = lv < top[k] ? (k == 4 ? lv + 1 : lv) : (unsigned)top[k] - 1u;
        sum = (uint8_t)(sum + (uint8_t)(con * (int)times));
        ++counted;
    }
    if (counted == 0) {
        cok_adventure_fail(m->game, COK_ECL_UNDEFINED,
                           "Modify's Keep divides by the classes' count, 0 (dqk 6a87:036b)");
        return false;
    }
    sum = (uint8_t)(sum / counted);
    cok_field_set8(m->L, m->c, COK_FIELD_BASE_HIT_POINTS,
                   (uint8_t)(cok_field_u8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS) - sum));
    for (unsigned k = 0; k < SCORES; ++k)
        cok_field_set8_at(m->L, m->c, COK_FIELD_SCORE_BASE, k, score(m, k));
    cok_field_set8(m->L, m->c, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE,
                   cok_field_u8(m->L, m->c, COK_FIELD_STRENGTH_EXCEPTIONAL));
    m->done = true;
    return true;
}

/* 6a87:01e5: the scores, the strength's exceptional, the hit points and the
 * name as Modify found them, recomputed. */
static bool leave(modify *m)
{
    for (unsigned k = 0; k < SCORES; ++k) set_score(m, k, m->scores[k]);
    cok_field_set8(m->L, m->c, COK_FIELD_STRENGTH_EXCEPTIONAL, m->exceptional);
    cok_field_set8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS, m->most);
    cok_field_set8(m->L, m->c, COK_FIELD_HIT_POINTS, m->most);
    memcpy(m->c + cok_field_offset(m->L, COK_FIELD_NAME), m->name, 16);
    char error[300];
    if (!cok_character_stats(m->ch, cok_game_rules(m->game), &m->game->item_types, error,
                             sizeof error)) {
        cok_adventure_fail(m->game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    m->done = true;
    return true;
}

/* 6a87:03c7-0b84, 1188: a key at a field. next and previous are the fields
 * the original's Next and Previous go to. */
static bool field_key(modify *m, unsigned field, int key)
{
    const uint8_t *lim = limits(m), *min = minimums(m);
    unsigned next, previous;
    switch (field) {
    case 0: next = 1, previous = HIT_POINTS; break;
    case 5: next = m->creating ? HIT_POINTS : NAME, previous = 4; break;
    case HIT_POINTS: next = 0, previous = m->creating ? 5 : NAME; break;
    default: next = field + 1, previous = field - 1; break;
    }
    int what = key == 0x82 ? KEY_ADD
               : key == 0x86 ? KEY_SUB
               : key == 0x84 || key == 0x85 ? KEY_NEXT
               : key == 0x87 || key == 0x88 ? KEY_PREVIOUS
                                            : key;
    unsigned redraw_field = field;
    if (what == KEY_NEXT) {
        set_mode(m, next);
    } else if (what == KEY_PREVIOUS) {
        set_mode(m, previous);
    } else if (what == KEY_KEEP) {
        if (!keep(m)) return false;
    } else if (what == KEY_EXIT) {
        if (!leave(m)) return false;
    } else if (what == KEY_ADD || what == KEY_SUB) {
        unsigned g = (unsigned)cok_field_s8(m->L, m->c, COK_FIELD_GENDER) & 0xff;
        uint8_t v;
        if (field == 0) {
            if (what == KEY_ADD) {
                /* 6a87:042b: past the race's most (by gender) strength
                 * 18 adds an exceptional one for a knight, fighter,
                 * paladin or ranger, up to the race's. */
                v = (uint8_t)(score(m, 0) + 1);
                uint8_t most = lim[2 + (g & 1)];
                if (most < v) {
                    uint8_t ex = cok_field_u8(m->L, m->c, COK_FIELD_STRENGTH_EXCEPTIONAL);
                    if (v > 0x12 && (level(m, 2) > 0 || level(m, 3) > 0 || level(m, 4) > 0 ||
                                     level(m, 1) > 0) &&
                        lim[4 + (g & 1)] > ex)
                        cok_field_set8(m->L, m->c, COK_FIELD_STRENGTH_EXCEPTIONAL,
                                       (uint8_t)(ex + 1));
                    v = most;
                }
                set_score(m, 0, v);
            } else {
                /* 6a87:04f0: an exceptional strength first, else the least
                 * (by gender), then the class's. */
                v = (uint8_t)(score(m, 0) - 1);
                uint8_t ex = cok_field_u8(m->L, m->c, COK_FIELD_STRENGTH_EXCEPTIONAL);
                if (ex > 0) {
                    cok_field_set8(m->L, m->c, COK_FIELD_STRENGTH_EXCEPTIONAL, (uint8_t)(ex - 1));
                    ++v;
                } else if (lim[g & 1] > v) {
                    v = lim[g & 1];
                }
                if (v < min[0]) v = min[0];
                set_score(m, 0, v);
            }
        } else if (field < SCORES) {
            unsigned lo = 6 + 2 * (field - 1), hi = lo + 1; /* the table's least and most */
            if (what == KEY_ADD) {
                v = (uint8_t)(score(m, field) + 1);
                if (field == 5 && race(m) == 3 && class_(m) == 3) {
                    /* A mountain dwarf paladin's charisma is held at 17:
                     * Add stops at 17 and Sub at 17. */
                    if (v > 0x11) v = 0x11;
                } else if (v > lim[hi]) {
                    v = lim[hi];
                }
                set_score(m, field, v);
                if (field == 2 && cok_field_u8_at2(m->L, m->c, COK_FIELD_SPELLS_A_DAY, 0, 1) > 0)
                    cok_field_set8_at2(m->L, m->c, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
                if (field == 4) {
                    uint8_t least = least_hit_points(m);
                    if (least > cok_field_u8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS))
                        cok_field_set8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS, least);
                    cok_field_set8(m->L, m->c, COK_FIELD_HIT_POINTS,
                                   cok_field_u8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS));
                }
            } else {
                v = (uint8_t)(score(m, field) - 1);
                if (field == 5 && race(m) == 3 && class_(m) == 3) {
                    if (v < 0x11) v = 0x11;
                } else {
                    if (v < lim[lo]) v = lim[lo];
                    if (v < min[field]) v = min[field];
                }
                set_score(m, field, v);
                if (field == 2 && cok_field_u8_at2(m->L, m->c, COK_FIELD_SPELLS_A_DAY, 0, 1) > 0)
                    cok_field_set8_at2(m->L, m->c, COK_FIELD_SPELLS_A_DAY, 0, 1, 1);
                if (field == 4) {
                    uint8_t most = most_hit_points(m);
                    if (most < cok_field_u8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS))
                        cok_field_set8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS, most);
                    cok_field_set8(m->L, m->c, COK_FIELD_HIT_POINTS,
                                   cok_field_u8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS));
                    redraw(m, HIT_POINTS, false);
                }
            }
        } else if (field == HIT_POINTS) {
            uint8_t most = cok_field_u8(m->L, m->c, COK_FIELD_MAX_HIT_POINTS);
            if (what == KEY_ADD) {
                ++most;
                uint8_t cap = most_hit_points(m);
                if (cap < most) most = cap;
            } else {
                --most;
                uint8_t least = least_hit_points(m);
                if (least > most) most = least;
            }
            set_most(m, most);
        }
    }
    redraw(m, redraw_field, false);
    if (field == 4 && (what == KEY_ADD || what == KEY_SUB)) redraw(m, 4, false);
    return true;
}

/* 6a87:0d30: a key at the name: it is edited in place (up to 15
 * characters, the cursor a block, DS:c7bf), Return or Down or End goes to
 * the hit points, Home or Up to the charisma, unless another of the party
 * has that name. */
static bool edit_name(modify *m)
{
    cok_adventure *game = m->game;
    uint8_t *name = m->c + cok_field_offset(m->L, COK_FIELD_NAME);
    size_t length;
    for (;;) {
        for (length = 0; length < 15 && name[length] != 0; ++length) {}
        int cx = 8 + 8 * (int)m->cursor;
        queen_xor(game, 8, cx, 16, cx + 8, 0x0f);
        queen_input in;
        int key = queen_read_input(game, &in);
        queen_xor(game, 8, cx, 16, cx + 8, 0x0f);
        if (key < 0 && key != COK_KEY_TIMEOUT) return false;
        if (in.click || key == COK_KEY_TIMEOUT) continue;
        int k = in.key > 0xff ? (in.key & 0x7f) + 0x80 : in.key == 0x60 ? 0x1b : in.key;
        redraw(m, NAME, true);
        if (k >= 0x20 && k <= 0x7a) {
            if (length < 15) {
                for (size_t i = length + 1; i > m->cursor; --i) name[i] = name[i - 1];
                name[m->cursor] = (uint8_t)k;
                name[15] = 0;
                ++m->cursor;
                if (m->cursor > 15) m->cursor = 15;
            }
            return true;
        }
        switch (k) {
        case 0x08:
            if (m->cursor > 0) {
                --m->cursor;
                if (length - 1 > m->cursor) {
                    for (size_t i = m->cursor + 1; i <= length; ++i) name[i - 1] = name[i];
                } else if (length - 1 == m->cursor) {
                    name[m->cursor] = 0;
                }
            }
            break;
        case 0x8a: /* Del */
            if (length > 0 && length - 1 >= m->cursor) {
                if (length - 1 == m->cursor)
                    name[m->cursor] = 0;
                else
                    for (size_t i = m->cursor + 1; i <= length; ++i) name[i - 1] = name[i];
            }
            break;
        case 0x86: m->cursor = m->cursor == 0 ? (unsigned)length : m->cursor - 1; break;
        case 0x82: m->cursor = length > m->cursor ? m->cursor + 1 : 0; break;
        case 0x0d: case 0x84: case 0x85: case 0x87: case 0x88: {
            bool taken = false;
            for (size_t i = 0; i < game->party.count; ++i) {
                const uint8_t *other = game->party.members[i]->record;
                if (other != m->c &&
                    strncmp((const char *)other + cok_field_offset(m->L, COK_FIELD_NAME),
                            (const char *)name, 16) == 0)
                    taken = true;
            }
            if (taken) {
                char text[64];
                snprintf(text, sizeof text, "Already a %s in party!", (const char *)name);
                queen_message(game, text, 0, 0x0b, 8);
            } else {
                redraw(m, NAME, false);
                set_mode(m, k == 0x87 || k == 0x88 ? 5 : HIT_POINTS);
            }
            break;
        }
        default: break;
        }
        return true;
    }
}

/* The original's first test (6a87:1293): only a character of the three
 * experiences creation gives (a first level of each class count's, 1,000,001,
 * 500,000 or 333,333) is modified, but with "HALIBUT" the fourth word on
 * the command line (which the port has no way to give). */
static bool modifiable(const modify *m)
{
    uint32_t xp = cok_field_u32(m->L, m->c, COK_FIELD_EXPERIENCE);
    return xp == 0xf4241u || xp == 0x7a120u || xp == 0x51615u;
}

bool queen_modify(cok_adventure *game, cok_character *character, bool creating)
{
    modify m = {.game = game, .ch = character, .c = character->record,
                .L = cok_game_rules(game)->character, .creating = creating, .mode = 0xff};
    char name[17];
    cok_field_name(m.L, m.c, name);
    if (!modifiable(&m)) {
        char text[96];
        snprintf(text, sizeof text, "%s can't be modified.", name);
        queen_message(game, text, 0, 0x0b, 8);
        return true;
    }
    /* Each readied item is unreadied (86b8:1965 through 1d99:1107). */
    for (size_t i = 0; i < character->item_count; ++i) {
        uint8_t *item = character->items[i];
        const cok_rules_item_record *I = &cok_game_rules(game)->item_record;
        if (item[I->readied] == 0) continue;
        if (item[0x34] != 0) {
            queen_bar_message(game, "It's Cursed!");
            continue;
        }
        if ((item[0x38] & 0x80) != 0) {
            cok_adventure_not_yet(game, "Modify's readied item with a power", "dqk 86b8:16f7");
            return false;
        }
        item[I->readied] = 0;
    }
    if (!queen_sheet(game, character)) return false;
    for (unsigned k = 0; k < SCORES; ++k) {
        set_score(&m, k, cok_field_u8_at(m.L, m.c, COK_FIELD_SCORE_BASE, k));
        m.scores[k] = score(&m, k);
    }
    m.exceptional = cok_field_u8(m.L, m.c, COK_FIELD_STRENGTH_EXCEPTIONAL);
    m.most = cok_field_u8(m.L, m.c, COK_FIELD_MAX_HIT_POINTS);
    memcpy(m.name, m.c + cok_field_offset(m.L, COK_FIELD_NAME), 16);
    if (!creating) redraw(&m, NAME, false);
    set_mode(&m, 0);
    redraw(&m, m.mode, true);
    static const char *const items[] = {"Next", "Previous", "Add", "Sub", "Keep", "Exit"};
    /* The areas a click takes (1bdb:1429): the six scores' rows, the
     * name's (not while creating) and the hit points'. */
    queen_rect areas[8];
    for (int k = 0; k < SCORES; ++k) areas[k] = (queen_rect){72 + 8 * k, 0, 80 + 8 * k, 160};
    areas[NAME] = creating ? (queen_rect){0, 0, 0, 0} : (queen_rect){8, 0, 16, 160};
    areas[HIT_POINTS] = (queen_rect){24, 160, 32, 320};
    while (!m.done) {
        bool clicked = false;
        int key = 0;
        if (m.mode != NAME) {
            cok_adventure_log(game, "menu", "Next Previous Add Sub Keep Exit");
            key = queen_bar_menu_areas(game, "Modify:", items, 6, true, areas, 8);
            if (key < 0) return false;
            if (key >= QUEEN_AREA) {
                clicked = true;
                redraw(&m, m.mode, false);
                set_mode(&m, (unsigned)(key - QUEEN_AREA));
            }
        }
        if (!clicked) {
            if (m.mode == NAME) {
                if (!edit_name(&m)) return false;
            } else if (!field_key(&m, m.mode, key)) {
                return false;
            }
        }
        if (!queen_sheet_derived(game, character)) return false;
        redraw(&m, m.mode, true);
    }
    return true;
}
