#include "queen.h"

#include "data.h"
#include "field.h"
#include "party.h"
#include "queen_view.h"
#include "rules_dark_queen_magic.h"

#include <stdio.h>
#include <string.h>

/* The spell picker of Dark Queen's Cast (849a:2080 for kind 0 in mode 1): the memorised spells
 * the character may cast (849a:0b39 with 05cc, 849a:0031), in a list by level with a heading
 * before each level (849a:0b39's tail), under the menu "Choose Spell:" (849a:019c). Cast
 * (52db:2543, queen_cast.c's queen_cast_command) is the picker's one caller in a battle. */

enum {
    SPELL_BYTES = 0x89,       /* +0xce to +0x156: the memorised spell bytes (0b39's k 0-0x88) */
    SPELL_TABLE = 0x27e9,     /* DS:27e9: the spells, 0x16 bytes each by id */
    SPELL_RECORD = 0x16,
    CLASS_LEVELS = 0xa5,      /* +0xa5 + class: the levels (177e:2234) */
    WISDOM = 0x7d,
    INTELLIGENCE = 0x7b,
    RACE = 0x58,              /* a word: 6 is a human */
    ARMOUR = 2,               /* slot 2 (+0x14): the armour readied */
    MODE_BATTLE = 5,
    MENU_ROWS = 2 * SPELL_BYTES, /* spell rows and the headings among them */
};

/* The spell table's kind (a word at +0) and level (the byte at +2) of spell id. */
static unsigned spell_kind(const cok_rules *rules, unsigned id)
{
    long at = SPELL_TABLE + (long)id * SPELL_RECORD;
    return (unsigned)cok_data_held(rules->data, at) |
           (unsigned)cok_data_held(rules->data, at + 1) << 8;
}

static unsigned spell_level(const cok_rules *rules, unsigned id)
{
    return cok_data_held(rules->data, SPELL_TABLE + (long)id * SPELL_RECORD + 2);
}

/* 849a:0031's kind 2 (the mage's): intelligence above 8, then the class 5 level above 0 unless the
 * human with armour is in the battle (mode 5) at class 4 8 or less; class 4 above 8; with the
 * scroll flag, class 6 above 10. */
static bool kind_two(const cok_adventure *game, const cok_character *character, bool scroll)
{
    const uint8_t *c = character->record;
    if (c[INTELLIGENCE] <= 8) return false;
    if (c[CLASS_LEVELS + 5] > 0) {
        bool exempt = (c[RACE] | c[RACE + 1] << 8) == 6 && character->slots[ARMOUR] != 0 &&
                      c[CLASS_LEVELS + 4] <= 8 && game->vm.mode == MODE_BATTLE;
        if (!exempt) return true;
    }
    if (c[CLASS_LEVELS + 4] > 8) return true;
    return scroll && c[CLASS_LEVELS + 6] > 10;
}

bool queen_spell_usable(cok_adventure *game, const cok_character *character, uint8_t spell,
                        bool scroll)
{
    const uint8_t *c = character->record;
    const cok_rules *rules = cok_game_rules(game);
    unsigned id = spell & 0x7f;
    unsigned kind = spell_kind(rules, id);
    /* Class levels are bytes compared unsigned: a level of 0x80 or more is large. */
    switch (kind) {
    case 0: case 3:
        return c[WISDOM] > 8 && (c[CLASS_LEVELS] > 0 || c[CLASS_LEVELS + 1] > 5 ||
                                 c[CLASS_LEVELS + 3] > 8);
    case 1:
        return c[WISDOM] > 8 && c[CLASS_LEVELS + 4] > 7;
    case 2:
        return kind_two(game, character, scroll);
    default:
        return false; /* kind 4 and above: no class may use it here */
    }
}

/* A memorised spell in the list (05cc's row: the spell's id and how many are memorised). */
typedef struct {
    uint8_t spell;
    uint8_t count;
} spell_row;

/* 849a:05cc: the sorted insert. The spell goes before the first spell of a higher level, after
 * the spells of its own level, and the same spell adds one to its count. */
static void add_spell(const cok_rules *rules, spell_row *rows, size_t *n, uint8_t spell)
{
    unsigned level = spell_level(rules, spell);
    size_t at = 0;
    while (at < *n && spell_level(rules, rows[at].spell) <= level && rows[at].spell != spell)
        ++at;
    if (at < *n && rows[at].spell == spell) {
        rows[at].count++;
        return;
    }
    memmove(rows + at + 1, rows + at, (*n - at) * sizeof *rows);
    rows[at].spell = spell;
    rows[at].count = 1;
    ++*n;
}

/* 849a:0b39 (kind 0): the list of the spells memorised that the character may cast, in order. */
static size_t spell_list(cok_adventure *game, const cok_character *character, spell_row *rows)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const uint8_t *c = character->record + cok_field_offset(L, COK_FIELD_MEMORIZED);
    size_t n = 0;
    for (unsigned k = 0; k < SPELL_BYTES; ++k) {
        uint8_t b = c[k];
        if (b == 0 || b >= 0x80) continue; /* not memorised, or the bit 7 (not in kind 0) */
        if (queen_spell_usable(game, character, b, false)) add_spell(rules, rows, &n, b);
    }
    return n;
}

/* The menu's rows: a heading before the first spell and before each spell whose level is above
 * the one before it (849a:0b39's tail), the spells as a TAB and their name, and " (n)" for n above
 * one. Returns the number of rows. */
static size_t menu_rows(const cok_rules *rules, const spell_row *rows, size_t n,
                        const char **items, char (*text)[64], bool *heading,
                        uint8_t *spells)
{
    size_t m = 0;
    unsigned prev = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned level = spell_level(rules, rows[i].spell);
        if (i == 0 || level > prev) {
            items[m] = level < 10 ? cok_dark_queen_spell_levels[level] : "";
            heading[m] = true;
            spells[m++] = 0;
        }
        /* the names table starts at id 1 */
        snprintf(text[i], sizeof text[i], "\t%s", cok_dark_queen_spell_names[rows[i].spell - 1]);
        if (rows[i].count > 1) {
            size_t used = strlen(text[i]);
            snprintf(text[i] + used, sizeof text[i] - used, " (%u)", rows[i].count);
        }
        items[m] = text[i];
        heading[m] = false;
        spells[m++] = rows[i].spell;
        prev = level;
    }
    return m;
}

int queen_spell_pick(cok_adventure *game, cok_character *character, int mode)
{
    if (mode != 1) {
        char what[48];
        snprintf(what, sizeof what, "a spell list (mode %d)", mode);
        cok_adventure_not_yet(game, what, "dqk 849a:2080");
        return -1;
    }
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    spell_row rows[SPELL_BYTES];
    size_t n = spell_list(game, character, rows);
    if (n == 0) return 0; /* 2080's *nonempty 0: nothing drawn */
    /* 2080: the full window in the battle, else the title window, then the name (177e:1697). */
    if (game->vm.mode == MODE_BATTLE) queen_full_window(game);
    else queen_title_window(game);
    queen_name_line(game, character, 1, 1, true);
    char name[17];
    cok_field_name(L, character->record, name);
    queen_say(game, (int)strlen(name) + 4, 1, "Spells in Memory", 7);

    char text[SPELL_BYTES][64];
    const char *items[MENU_ROWS];
    bool heading[MENU_ROWS];
    uint8_t spells[MENU_ROWS];
    size_t count = menu_rows(rules, rows, n, items, text, heading, spells);
    for (size_t i = 0; i < count; ++i)
        cok_adventure_log(game, heading[i] ? "heading" : "item", items[i]);
    static const char *const bar[] = {"Cast", "Exit"}; /* 019c's "Cast Exit" */
    size_t at = 0;
    int picked = queen_list_menu_rows(game, "Choose Spell:", bar, 2, items, heading, count, &at,
                                      1, 5, 0x16, 0x26);
    if (picked < 0) return -1;
    if (picked != 0) return 0; /* Exit, or Escape (0x1b) */
    return spells[at];
}
