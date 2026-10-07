#include "magic.h"

#include "camp.h"
#include "cast.h"
#include "screen.h"

#include <stdio.h>
#include <string.h>

/* Spells. */

static uint8_t spell_byte(uint8_t id, unsigned column)
{
    uint8_t byte = 0;
    cok_ds_byte((uint16_t)(0x31b3u + 16u * (id & 0x7fu) + column), &byte);
    return byte;
}

uint8_t cok_spell_class(uint8_t id)
{
    return spell_byte(id, 0);
}

uint8_t cok_spell_level(uint8_t id)
{
    return spell_byte(id, 1);
}

uint8_t cok_spell_effect(uint8_t id)
{
    return spell_byte(id, 10);
}

static const char *const spell_names[COK_SPELLS - 1] = { /* DS:20a0, string[40] each */
    "Bless",
    "Curse",
    "Cure Light Wounds",
    "Cause Light Wounds",
    "Detect Magic",
    "Protection From Evil",
    "Protection from Good",
    "Resist Cold",
    "Burning Hands",
    "Charm Person",
    "Detect Magic",
    "Enlarge",
    "Reduce",
    "Friends",
    "Magic Missile",
    "Protection From Evil",
    "Protection From Good",
    "Read Magic",
    "Shield",
    "Shocking Grasp",
    "Sleep",
    "Find Traps",
    "Hold Person",
    "Resist Fire",
    "Silence, 15' Radius",
    "Slow Poison",
    "Snake Charm",
    "Spiritual Hammer",
    "Detect Invisibility",
    "Invisibility",
    "Knock",
    "Mirror Image",
    "Ray of Enfeeblement",
    "Stinking Cloud",
    "Strength",
    "Animate Dead",
    "Cure Blindness",
    "Cause Blindness",
    "Cure Disease",
    "Cause Disease",
    "Dispel Magic",
    "Prayer",
    "Remove Curse",
    "Bestow Curse",
    "Blink",
    "Dispel Magic",
    "Fireball",
    "Haste",
    "Hold Person",
    "Invisibility, 10' Radius",
    "Lightning Bolt",
    "Protection From Evil, 10' Radius",
    "Protection From Good, 10' Radius",
    "Protection From Normal Missiles",
    "Slow",
    "Restoration",
    "",
    "Cure Serious Wounds",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "Cause Serious Wounds",
    "Neutralize Poison",
    "Poison",
    "Protection Evil, 10' Radius",
    "Sticks to Snakes",
    "Cure Critical Wounds",
    "Cause Critical Wounds",
    "Dispel Evil",
    "Flame Strike",
    "Raise Dead",
    "Slay Living",
    "Detect Magic",
    "Entangle",
    "Faerie Fire",
    "Invisibility to Animals",
    "Charm Monsters",
    "Confusion",
    "Dimension Door",
    "Fear",
    "Fire Shield",
    "Fumble",
    "Ice Storm",
    "Minor Globe Of Invulnerability",
    "Remove Curse",
    "Animate Dead",
    "Cloud Kill",
    "Cone of Cold",
    "Feeblemind",
    "Hold Monsters",
    "",
    "",
    "",
    "",
    "",
    "Bestow Curse",
    "Protection From Evil 10' Radius",
    "Silence 15' radius",
    "Detect Magic",
    "Remove Curse",
    "Bless",
    "Charm Person",
    "Burning Hands",
};

const char *cok_spell_name(uint8_t id)
{
    return id >= 1 && id < COK_SPELLS ? spell_names[id - 1] : NULL;
}

/* The headings of spell levels (DS:0bfa, string[40] each). */
static const char *const level_names[10] = {
    "Special", "1st Level", "2nd Level", "3rd Level", "4th Level",
    "5th Level", "6th Level", "7th Level", "8th Level", "9th Level",
};

bool cok_item_is_scroll(const uint8_t *item)
{
    return item[0x2e] == 0x27 || item[0x2e] == 0x28;
}

/* The selected character, if it is a party member. */
static cok_character *selected(cok_adventure *game)
{
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* Whether c can use spells of spell's class (5b04:0083): clerics' and
 * granted powers with wisdom above 8 for a cleric, or a knight above level
 * 5, now or before; druids' likewise for a ranger above level 6; and
 * magic-users' with intelligence above 8, unless in combat (mode 5) it is
 * a human in armour who is not a ranger above level 8, nor a former ranger
 * above level 8 that is or was a mage. */
static bool usable(const cok_adventure *game, const cok_character *character, uint8_t spell)
{
    const uint8_t *c = character->record;
    switch (cok_spell_class(spell & 0x7f)) {
    case 0: case 2:
        return c[0x15] > 8 &&
               ((int8_t)c[0xf9] > 0 ||
                (cok_character_former_class(c) && (int8_t)c[0x101] > 0) ||
                (int8_t)c[0x100] > 5 ||
                ((int8_t)c[0x108] > 5 && cok_character_former_class(c)));
    case 1:
        return c[0x15] > 8 &&
               ((int8_t)c[0xfd] > 6 || (cok_character_former_class(c) && (int8_t)c[0x105] > 6));
    case 3:
        if (c[0x13] <= 8) return false;
        if (c[0x5a] != 6 || character->slots[2] == 0 || game->vm.mode != 5) return true;
        if ((int8_t)c[0xfd] > 8) return true;
        if (!cok_character_former_class(c) || (int8_t)c[0x105] <= 8) return false;
        if ((int8_t)c[0xfe] > 0) return true;
        return cok_character_former_class(c) && (int8_t)c[0x106] > 0;
    default: return false;
    }
}

/* Whether spell is a granted power c has memorized, from its second byte
 * on (5b04:001e), which keeps it out of the grimoire's list. */
static bool granted(const uint8_t *c, uint8_t spell)
{
    spell &= 0x7f;
    if (cok_spell_class(spell) != 2) return false;
    for (size_t i = 1; i <= 0x39; ++i)
        if ((c[0x1e + i] & 0x7f) == spell) return true;
    return false;
}

/* Spell lists. */

enum {
    LIST_SPELLS = 0x3a,  /* DS:473a, the spells of a list in order. */
    LIST_SCROLLS = 0x31, /* DS:4774, the scroll of each. */
    LIST_ROWS = 2 * LIST_SPELLS,
};

/* A list of spells (DS:46fc): rows of text, some of them headings; the
 * spell of each row that is not a heading (DS:473a), how many of it
 * (DS:4700) and for scrolls, the scroll (DS:4774). */
typedef struct {
    char text[LIST_ROWS][41];
    bool heading[LIST_ROWS];
    size_t count;
    uint8_t spells[LIST_SPELLS + 1], counts[LIST_SPELLS];
    size_t scrolls[LIST_SCROLLS];
    size_t scroll_count;
} spell_list;

static bool list_fail(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* Insert an empty row at row. */
static bool insert_row(cok_adventure *game, spell_list *l, size_t row)
{
    if (l->count == LIST_ROWS) return list_fail(game, "a spell list longer than the port's");
    memmove(l->text[row + 1], l->text[row], (l->count - row) * sizeof l->text[0]);
    memmove(&l->heading[row + 1], &l->heading[row], (l->count - row) * sizeof l->heading[0]);
    l->text[row][0] = '\0';
    l->heading[row] = false;
    ++l->count;
    return true;
}

static void append_text(char *dst, size_t size, const char *text)
{
    size_t length = strlen(dst);
    while (*text != '\0' && length + 1 < size) dst[length++] = *text++;
    dst[length] = '\0';
}

/* Set row's text to b's name, after " *" if it is marked or two spaces. */
static bool name_row(cok_adventure *game, spell_list *l, size_t row, uint8_t b)
{
    const char *name = cok_spell_name(b & 0x7f);
    if (name == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "spell %u has no name; the list would show the data at DS:%04x",
                           b & 0x7f, 0x2077 + 41 * (b & 0x7f));
        return false;
    }
    snprintf(l->text[row], sizeof l->text[row], "%s", b > 0x7f ? " *" : "  ");
    append_text(l->text[row], sizeof l->text[row], name);
    l->heading[row] = false;
    return true;
}

/* Set row to the heading of level. */
static bool heading_row(cok_adventure *game, spell_list *l, size_t row, uint8_t level)
{
    if (level > 9) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "level %u has no heading; the list would show the data at DS:%04x",
                           level, 0x0bfa + 41 * (int8_t)level);
        return false;
    }
    snprintf(l->text[row], sizeof l->text[row], "%s", level_names[level]);
    l->heading[row] = true;
    return true;
}

/* Add spell b in level order after those of its level, or count it again
 * if it is there, showing the count after its name (5b04:0628). */
static bool add_sorted(cok_adventure *game, spell_list *l, uint8_t b)
{
    uint8_t spell = b & 0x7f, level = cok_spell_level(spell);
    size_t k = 0, row = 0;
    if (l->count == 0) {
        memset(l->counts, 0, sizeof l->counts);
        if (!insert_row(game, l, 0)) return false;
        l->counts[0] = 1;
    } else {
        bool found = false;
        while (row < l->count && !found) {
            if (k >= LIST_SPELLS) return list_fail(game, "a spell list of more than 58 spells");
            if (l->heading[row]) {
                ++row;
                ++k;
            } else if ((int8_t)cok_spell_level(l->spells[k]) > (int8_t)level ||
                       l->spells[k] == spell) {
                found = true;
            } else {
                ++row;
                ++k;
            }
        }
        if (k >= LIST_SPELLS) return list_fail(game, "a spell list of more than 58 spells");
        if (l->spells[k] == spell) {
            ++l->counts[k];
        } else {
            if (!insert_row(game, l, row)) return false;
            uint8_t carry = 1;
            for (size_t i = k; i < LIST_SPELLS; ++i) {
                uint8_t next = l->counts[i];
                l->counts[i] = carry;
                carry = next;
            }
        }
    }
    if (!name_row(game, l, row, b)) return false;
    if (l->counts[k] > 1) {
        char count[16];
        snprintf(count, sizeof count, " (%u)", l->counts[k]);
        append_text(l->text[row], sizeof l->text[row], count);
    }
    if (l->spells[k] != spell) {
        memmove(&l->spells[k + 1], &l->spells[k], LIST_SPELLS - 1 - k);
        l->spells[k] = spell;
    }
    return true;
}

/* Add spell b of a scroll at the end, after a heading when its level is
 * not the last spell's; the first spell's is compared with 0, so a first
 * granted power has none (5b04:045c). */
static bool add_scroll_spell(cok_adventure *game, spell_list *l, uint8_t b)
{
    size_t k = 0;
    uint8_t last = 0;
    for (size_t i = 0; i < l->count; ++i)
        if (!l->heading[i]) ++k;
    if (k > 0) last = cok_spell_level(l->spells[k - 1]);
    if (k >= LIST_SPELLS) return list_fail(game, "a spell list of more than 58 spells");
    uint8_t level = cok_spell_level(b & 0x7f);
    if (level != last) {
        if (!insert_row(game, l, l->count) || !heading_row(game, l, l->count - 1, level))
            return false;
    }
    if (!insert_row(game, l, l->count) || !name_row(game, l, l->count - 1, b)) return false;
    l->spells[k] = b & 0x7f;
    return true;
}

/* Add the spells of scroll item index (5b04:0981): a character with effect
 * 0x10, or a cleric (or one whose former cleric level passes +0xd7)
 * reading an item whose type's slot is 0x0c, clears the scroll's low three
 * bits of +0x35 for good; while they are clear, its spells (with only the
 * marked ones above 0x80) are added. */
static bool add_scroll(cok_adventure *game, spell_list *l, cok_character *character,
                       size_t index, bool marked)
{
    uint8_t *c = character->record, *item = character->items[index];
    if (cok_character_find_effect(character, 0x10) != NULL ||
        (((int8_t)c[0xf9] > 0 || (int8_t)c[0x101] > (int8_t)c[0xd7]) &&
         game->item_types.type[item[0x2e] < COK_ITEM_TYPES ? item[0x2e] : 0][0] == 0x0c))
        item[0x35] &= 0xf8;
    if ((item[0x35] & 7) != 0) return true;
    for (size_t k = 1; k <= 3; ++k) {
        uint8_t b = item[0x3b + k];
        if (marked ? b <= 0x80 : b == 0) continue;
        if (!add_scroll_spell(game, l, b)) return false;
        if (l->scroll_count == LIST_SCROLLS)
            return list_fail(game, "more than 49 scroll spells overwrite DS:4838");
        l->scrolls[l->scroll_count++] = index;
    }
    return true;
}

/* The scroll an item's Use reads (DS:6e36), as an index in the selected
 * character's items. */
static size_t used_scroll;

/* Build list kind for the selected character (5b04:0b21): 0 the spells it
 * has memorized, 1 those of its grimoire it may memorize, 2 those of the
 * scroll used (DS:6e36), 3 those of its scrolls, 5 those marked to
 * memorize, 6 those marked to scribe. Lists of memorized and grimoire
 * spells get a heading before each level. The scroll list counts its
 * spells in DS:4838, which only kinds 3 and 6 (5b04:0a6f) reset. */
static bool build_list(cok_adventure *game, spell_list *l, cok_character *character, int kind)
{
    const uint8_t *c = character->record;
    memset(l, 0, sizeof *l);
    if (kind == 0 || kind == 5) {
        for (size_t i = 0; i <= 0x39; ++i) {
            uint8_t b = c[0x1e + i];
            bool take = kind == 0 ? b > 0 && b < 0x80 : b > 0x7f;
            if (take && usable(game, character, b) && !add_sorted(game, l, b)) return false;
        }
    } else if (kind == 1) {
        for (uint8_t n = 1; n <= 0x6b; ++n)
            if (c[0x62 + n] != 0 && usable(game, character, n) && !granted(c, n) &&
                !add_sorted(game, l, n))
                return false;
    } else if (kind == 2) {
        /* 5b04:0981 on the scroll alone, whatever its order. */
        l->scroll_count = game->scroll_spells;
        bool ok = add_scroll(game, l, character, used_scroll, false);
        game->scroll_spells = (uint8_t)l->scroll_count;
        return ok;
    } else {
        /* 5b04:0a6f: scrolls of the character's order of magic (+0x5e): 1
         * reads those with +0x35 bit 0x20, 2 those with 0x10. */
        for (size_t i = 0; i < character->item_count; ++i) {
            const uint8_t *item = character->items[i];
            if (!cok_item_is_scroll(item)) continue;
            if (!((c[0x5e] == 1 && (item[0x35] & 0x20) != 0) ||
                  (c[0x5e] == 2 && (item[0x35] & 0x10) != 0)))
                continue;
            if (!add_scroll(game, l, character, i, kind == 6)) return false;
            game->scroll_spells = (uint8_t)l->scroll_count;
        }
        game->scroll_spells = (uint8_t)l->scroll_count;
        return true;
    }
    if (l->count == 0) return true;
    /* Headings: one before the first spell, then one before each spell of
     * a higher level than the one before it. */
    uint8_t level = cok_spell_level(l->spells[0]);
    if (!insert_row(game, l, 0) || !heading_row(game, l, 0, level)) return false;
    size_t k = 0;
    for (size_t row = 0; row < l->count; ++row, ++k) {
        uint8_t previous = level;
        if (k < LIST_SPELLS && l->spells[k] != 0) level = cok_spell_level(l->spells[k]);
        if ((int8_t)previous < (int8_t)level) {
            if (!insert_row(game, l, row + 1) || !heading_row(game, l, row + 1, level))
                return false;
            ++row;
        }
    }
    return true;
}

/* The heading of a spell list (546c:34ec): what kind shows. */
static const char *const kind_names[7] = {
    "in Memory", "in Grimoire", "on Scroll", "on Scrolls", "to Choose", "to Memorize", "to Scribe",
};

/* Show list kind and pick a spell (546c:34ec, 5b04:027a) in mode 1 Cast, 2
 * Memorize, 3 Scribe or 0 only to look. When *index is below 0, or to
 * cast, the frame is drawn first: for Memorize the frame with the row 16
 * divider and rows 17-22 kept for the table of spells left (1128:0384),
 * otherwise the open frame (1128:077c). The list shows "NAME's Spells
 * ..." on row 1 and rows 5-15 (Memorize) or 5-22. Returns the spell
 * picked, 0 for none or an empty list (*nonempty clear), or -1 when input
 * ended or the list cannot be built. */
static int choose(cok_adventure *game, cok_character *character, int kind, int mode, int *index,
                  bool *nonempty)
{
    static spell_list l;
    *nonempty = false;
    if (!build_list(game, &l, character, kind)) return -1;
    *nonempty = l.count > 0;
    if (!*nonempty) return 0;
    uint16_t moons[3];
    for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
    const uint8_t *c = character->record;
    if (*index < 0 || mode == 1) {
        if (mode == 2)
            cok_screen_spells(&game->screen, &game->view.tiles[4], moons);
        else
            cok_screen_list(&game->screen, &game->view.tiles[4], moons);
    }
    char name[20], text[64];
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(name, c + 1, length);
    snprintf(name + length, sizeof name - length, "'s");
    cok_text_string(&game->screen, &game->font, name, 1, 1, c[0x189] == 0 ? 12 : 11, 0);
    snprintf(text, sizeof text, "Spells %s", kind_names[kind]);
    cok_text_string(&game->screen, &game->font, text, c[0] + 4, 1, 10, 0);
    cok_adventure_log(game, "list", text);
    for (size_t i = 0; i < l.count; ++i)
        cok_adventure_log(game, l.heading[i] ? "heading" : "item", l.text[i]);

    static const char *const bases[5] = {"", "Cast", "Memorize", "Scribe", "Learn"};
    cok_menu_style style = {mode >= 1 && mode <= 4 ? "Choose Spell: " : "",
                            mode >= 1 && mode <= 4 ? bases[mode] : "", 15, 10, 13, mode != 4};
    int bottom = mode == 2 ? 0x0f : 0x16;
    bool redraw = mode == 2 && game->spells_changed;
    if (*index < 0) {
        redraw = true;
        *index = 0;
    }
    if (mode == 4 || mode == 1) redraw = true;
    cok_menu_row rows[LIST_ROWS];
    for (size_t i = 0; i < l.count; ++i) rows[i] = (cok_menu_row){l.text[i], l.heading[i]};
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key;
    do {
        key = cok_menu_rows(&game->screen, &game->font, rows, l.count,
                            (cok_text_window){1, 5, 0x26, bottom}, &style, &redraw, index,
                            &game->list_top, &game->selected, &keys);
        if (key < 0) return -1;
    } while (key != 0 && key != 0x0d && key != 'C' && key != 'E' && key != 'L' && key != 'M' &&
             key != 'S');
    if (key == 0 || key == 'E') return 0;
    size_t k = 0;
    for (int i = 0; i < *index && i < (int)l.count; ++i)
        if (!l.heading[i]) ++k;
    char picked[8];
    snprintf(picked, sizeof picked, "%u", l.spells[k]);
    cok_adventure_log(game, "choice", picked);
    return l.spells[k];
}

/* Memorizing. */

bool cok_magic_slots(const uint8_t *c, uint8_t level, uint8_t class_, int stale,
                     uint8_t *left, uint8_t *count)
{
    if (class_ == 2) {
        if ((int8_t)c[0xf9] <= 0 && stale < 0) return false;
        uint8_t avail = (int8_t)c[0xf9] <= 0 ? (uint8_t)stale
                        : c[0x5d] == 4                ? 3
                        : c[0x5d] == 6                ? 0
                                                      : 1;
        for (size_t i = 0; i <= 0x39; ++i) {
            uint8_t spell = c[0x1e + i] & 0x7f;
            if (spell >= 101 && spell <= 107) --avail;
        }
        *left = avail;
        return true;
    }
    uint8_t n = 0;
    for (size_t i = 0; i <= 0x39; ++i) {
        uint8_t b = c[0x1e + i];
        if (b == 0) continue;
        if (cok_spell_level(b & 0x7f) == level && cok_spell_class(b & 0x7f) == class_) ++n;
    }
    if (count != NULL) *count = n;
    /* Only levels and classes past the spell table's reach the far
     * pointers, which the port does not keep, or leave the record. */
    int at = 0x11b + (int8_t)class_ * 5 + (int8_t)level;
    if (at < 0 || at >= COK_CHARACTER_SIZE || (at >= 0xe3 && at < 0xe7) ||
        (at >= 0x143 && at < 0x17b) || (at >= 0x17f && at < 0x187))
        return false;
    uint8_t per_day = c[at];
    *left = (uint8_t)(per_day - (n < per_day ? n : per_day));
    return true;
}

void cok_magic_sort(uint8_t *c)
{
    for (size_t i = 0; i <= 0x38; ++i)
        for (size_t j = i; j <= 0x39; ++j) {
            if ((c[0x1e + i] & 0x7f) <= (c[0x1e + j] & 0x7f)) continue;
            uint8_t t = c[0x1e + i];
            c[0x1e + i] = c[0x1e + j];
            c[0x1e + j] = t;
        }
}

/* Whether the selected character may do kind of magic (4888:08d8): 1 cast,
 * which an area with 0x4be5 set forbids, 2 memorize, 3 scribe; not while
 * its status is 1 or it cannot act. If not, it says why. */
static bool can(cok_adventure *game, const uint8_t *c, int kind)
{
    static const char *const what[4] = {"", "cast any spells", "memorize spells",
                                        "scribe any scrolls"};
    char text[80] = "";
    if (kind == 1 && game->vm.mem4b00[0xe5] > 0)
        snprintf(text, sizeof text, "cannot cast spells in this area");
    else if (c[0x188] == 1 || c[0x189] == 0)
        snprintf(text, sizeof text, "is in no condition to %s", what[kind]);
    if (text[0] == '\0') return true;
    cok_camp_say(game, c, text, true);
    return false;
}

/* Show a number in a cell of the table, cut to two characters. */
static void cell(char out[3], uint8_t value)
{
    char text[4];
    snprintf(text, sizeof text, "%u", value);
    out[0] = text[0];
    out[1] = text[1];
    out[2] = '\0';
}

/* The table of spells left to memorize today (4888:0b24) on rows 18-22:
 * cleric, druid, granted and magic-user spells of levels 1-5, those with
 * none a day blank, then the bonus spells the moons give a magic-user of
 * an order (DS:7144). Returns whether the character has spells a day of
 * any kind, 0 if not, -1 on failure. */
static int table(cok_adventure *game, cok_character *character)
{
    static const char *const labels[4] = {
        "    Cleric Spells:", "     Druid Spells:", "   Special Spells:", "Magic-User Spells:",
    };
    const uint8_t *c = character->record;
    char cells[4][6][3];
    bool flag[4] = {false, false, false, false}, any = false;
    game->bonus_spells = 0;
    for (unsigned k = 0; k < 4; ++k) {
        if (k == 2) {
            if ((int8_t)c[0xf9] > 0 && c[0x5d] != 6) flag[2] = any = true;
            for (unsigned level = 1; level <= 5; ++level) snprintf(cells[2][level], 3, " ");
            /* Without a cleric level, 4888:0700 reads an uninitialized
             * byte, which only this row, then not shown, would use. */
            uint8_t left;
            if (flag[2] && cok_magic_slots(c, 0, 2, -1, &left, NULL) && (left > 0 || any))
                cell(cells[2][1], left);
            continue;
        }
        for (unsigned level = 1; level <= 5; ++level) {
            uint8_t left = 0;
            cok_magic_slots(c, (uint8_t)level, (uint8_t)k, -1, &left, NULL);
            cell(cells[k][level], left);
            if (c[0x11b + k * 5 + level] == 0)
                snprintf(cells[k][level], 3, " ");
            else
                flag[k] = any = true;
        }
    }
    if ((int8_t)c[0xfe] > 0 && c[0x5e] > 0) {
        /* Word 0x4cf8 + order: the moons' phases, from 0x4cf9. */
        uint16_t phase = game->vm.mem4b00[0x1f8 + c[0x5e]];
        game->bonus_spells = phase == 1 ? 1 : phase == 2 ? 2 : 0;
        for (unsigned level = 1; level <= 5 && game->bonus_spells > 0; ++level) {
            uint8_t per_day = c[0x12a + level], left, count = 0;
            cok_magic_slots(c, (uint8_t)level, 3, -1, &left, &count);
            if (per_day >= count) continue;
            if ((int16_t)(count - per_day) > (int16_t)game->bonus_spells)
                game->bonus_spells = 0;
            else
                game->bonus_spells = (uint8_t)(game->bonus_spells - (count - per_day));
        }
        if (game->bonus_spells > 0) any = true;
    }
    if (!any) return 0;
    cok_camp_say(game, c, "", false);
    int row = 0x12;
    for (unsigned k = 0; k < 4; ++k) {
        if (!flag[k]) continue;
        cok_text_string(&game->screen, &game->font, labels[k], 1, row, 10, 0);
        for (unsigned level = 1; level <= 5; ++level)
            cok_text_string(&game->screen, &game->font, cells[k][level], 20 + 3 * ((int)level - 1),
                            row, 10, 0);
        ++row;
    }
    if (game->bonus_spells > 0) {
        char text[8];
        snprintf(text, sizeof text, "%u", game->bonus_spells);
        cok_text_string(&game->screen, &game->font, "     Bonus Spells", 1, row, 10, 0);
        cok_text_string(&game->screen, &game->font, text, 20, row, 10, 0);
    }
    return 1;
}

/* Unmark the spells marked to memorize (4888:05fd). */
static void forget_memorized(uint8_t *c)
{
    for (size_t i = 0; i <= 0x39; ++i)
        if (c[0x1e + i] > 0x7f) c[0x1e + i] = 0;
    c[0x58] = 0;
}

/* Unmark the spells of scrolls marked to scribe (4888:0640). */
static void forget_scribed(cok_character *character)
{
    for (size_t i = 0; i < character->item_count; ++i) {
        uint8_t *item = character->items[i];
        if (!cok_item_is_scroll(item)) continue;
        for (size_t k = 0x3c; k <= 0x3e; ++k) item[k] &= 0x7f;
    }
}

/* Show the spells marked in list kind (5 to memorize, 6 to scribe) and ask
 * whether to keep them; No unmarks them all. Returns whether the list had
 * any, or -1 if input ended. */
static int keep_marked(cok_adventure *game, cok_character *character, int kind,
                       const char *prompt)
{
    int index = -1;
    bool nonempty;
    if (choose(game, character, kind, 0, &index, &nonempty) < 0) return -1;
    if (!nonempty) return 0;
    int answer = cok_camp_yes_no(game, prompt, 14);
    if (answer < 0) return -1;
    if (answer == 'N') {
        if (kind == 5)
            forget_memorized(character->record);
        else
            forget_scribed(character);
        return 2;
    }
    return 1;
}

/* Memorize (4888:1098): the spells already marked are shown, and kept
 * unless the player says No, which unmarks them and opens the grimoire,
 * where each spell picked is marked if one of its level and class is left
 * today, or for a magic-user's spell, the moons give a bonus. A full set
 * of 58 bytes would be written past, which fails here. */
static void memorize(cok_adventure *game)
{
    cok_character *character = selected(game);
    if (character == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "Memorize with no character selected");
        return;
    }
    uint8_t *c = character->record;
    if (!can(game, c, 2)) return;
    game->selected = 1;
    int marked = keep_marked(game, character, 5, "Memorize These Spells? ");
    if (marked < 0) return;
    bool done = marked == 1, redraw = marked != 0;
    int index = -1;
    while (!done) {
        int any = table(game, character);
        if (any < 0 || game->vm.abort) return;
        if (any == 0) {
            done = true;
            cok_camp_say(game, c, "cannot memorize any spells", true);
            continue;
        }
        bool nonempty;
        int spell = choose(game, character, 1, 2, &index, &nonempty);
        if (spell < 0) return;
        redraw = true;
        if (spell == 0) {
            done = true;
            continue;
        }
        uint8_t level = cok_spell_level((uint8_t)spell), class_ = cok_spell_class((uint8_t)spell);
        /* For a power granted to one without a cleric level, 4888:0700
         * starts from an uninitialized local, which holds the low byte of
         * the return offset of the 34ec call before it (1098:116c), 0x71. */
        uint8_t left;
        if (!cok_magic_slots(c, level, class_, 0x71, &left, NULL)) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "4888:0700 reads a far pointer for spell %d", spell);
            return;
        }
        if (left == 0 && !(class_ == 3 && game->bonus_spells > 0)) continue;
        game->spells_changed = class_ == 2;
        size_t j = 0;
        while (j <= 0x39 && c[0x1e + j] != 0) ++j;
        if (j > 0x39) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "no memorized spell byte is free; 4888:1098 writes past them");
            return;
        }
        c[0x1e + j] = (uint8_t)(spell + 0x80);
        cok_magic_sort(c);
    }
    if (index != -1 && keep_marked(game, character, 5, "Memorize these spells? ") < 0) return;
    if (redraw) cok_adventure_redraw(game);
}

/* Scribe (4888:132b): as Memorize, for the spells of scrolls. A spell
 * picked is marked on the first item with a byte equal to it, which need
 * not be a scroll, if the character has a spell a day of its class and
 * level; the scroll picked is not the one used. */
static void scribe(cok_adventure *game)
{
    cok_character *character = selected(game);
    if (character == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "Scribe with no character selected");
        return;
    }
    uint8_t *c = character->record;
    if (!can(game, c, 3)) return;
    int marked = keep_marked(game, character, 6, "Scribe These Spells? ");
    if (marked < 0) return;
    bool done = marked == 1, redraw = marked != 0;
    int index = -1;
    while (!done) {
        bool nonempty;
        int spell = choose(game, character, 3, 3, &index, &nonempty);
        if (spell < 0) return;
        if (spell == 0) {
            done = true;
            if (!nonempty)
                cok_camp_say(game, c, "has no copyable scrolls", true);
            else
                redraw = true;
            continue;
        }
        redraw = true;
        if (c[0x62 + spell] != 0) {
            cok_camp_notice(game, "You already know that spell");
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < character->item_count && !found; ++i) {
            const uint8_t *item = character->items[i];
            if (!cok_item_is_scroll(item)) continue;
            for (size_t k = 1; k <= 3; ++k) {
                if (item[0x3b + k] <= 0x7f || (item[0x3b + k] & 0x7f) != spell) continue;
                cok_camp_notice(game, "You are already scibing that spell");
                found = true;
            }
        }
        if (found) continue;
        uint8_t class_ = cok_spell_class((uint8_t)spell), level = cok_spell_level((uint8_t)spell);
        if (c[0x11b + (int8_t)class_ * 5 + (int8_t)level] == 0) {
            cok_camp_notice(game, "You can not scribe that spell.");
            continue;
        }
        for (size_t i = 0; i < character->item_count && !found; ++i)
            for (size_t k = 1; k <= 3 && !found; ++k)
                if (character->items[i][0x3b + k] == spell) {
                    character->items[i][0x3b + k] |= 0x80;
                    found = true;
                }
    }
    if (index != -1 && keep_marked(game, character, 6, "Scribe these spells? ") < 0) return;
    if (redraw) cok_adventure_redraw(game);
}

/* Display. */

/* What Display calls effect id: the name of the first spell 1-0x38 that
 * adds it, for the ids of spells, or its own text; NULL for those it does
 * not show (4888:17ae). */
static const char *effect_text(uint8_t id, char out[41])
{
    static const struct { uint8_t id; const char *text; } named[] = {
        {0x04, "Dispel Evil"}, {0x1b, "Fumbling"}, {0x1f, "Helpless"}, {0x23, "Confused"},
        {0x2c, "Cause Disease"}, {0x32, "Hot Fire Shield"}, {0x36, "Cold Fire Shield"},
        {0x37, "Poisoned"}, {0x3b, "Regenerating"}, {0x3d, "Fire Resistance"},
        {0x3f, "Minor Globe of Invulnerability"}, {0x44, "enfeebled"},
        {0x45, "invisible to animals"}, {0x47, "Invisible"}, {0x48, "Camouflaged"},
        {0x49, "protected from dragon breath"}, {0x4d, "berserk"}, {0x59, "Displaced"},
    };
    bool spell = id == 1 || id == 2 || id == 5 || (id >= 8 && id <= 0x0c) || id == 0x0e ||
                 id == 0x10 || id == 0x11 || (id >= 0x13 && id <= 0x19) || id == 0x1c ||
                 id == 0x1d || (id >= 0x20 && id <= 0x22) || (id >= 0x24 && id <= 0x27) ||
                 id == 0x29 || id == 0x2a || id == 0x2d || id == 0x2e || id == 0x31 ||
                 (id >= 0x33 && id <= 0x35);
    if (spell) {
        for (uint8_t n = 1; n <= 0x38; ++n)
            if (cok_spell_effect(n) == id) {
                snprintf(out, 41, "%s", cok_spell_name(n));
                return out;
            }
        snprintf(out, 41, "Funky--%u", id);
        return out;
    }
    for (size_t i = 0; i < sizeof named / sizeof *named; ++i)
        if (named[i].id == id) return named[i].text;
    return NULL;
}

/* Display (4888:17ae): each member's name as a heading, then its spell
 * effects, or " <No Spell Effects>", and a blank row, in the open frame;
 * Exit leaves. */
static void display(cok_adventure *game)
{
    static char text[LIST_ROWS * 4][41];
    static cok_menu_row rows[LIST_ROWS * 4];
    size_t count = 0;
#define ROW(t, h)                                                                                  \
    do {                                                                                           \
        if (count < sizeof rows / sizeof *rows) {                                                  \
            snprintf(text[count], sizeof text[count], "%s", t);                                    \
            rows[count] = (cok_menu_row){text[count], h};                                          \
            ++count;                                                                               \
        }                                                                                          \
    } while (0)
    ROW(" ", false);
    for (size_t i = 0; i < game->party.count; ++i) {
        const cok_character *character = game->party.members[i];
        char name[16];
        size_t length = character->record[0] > 15 ? 15 : character->record[0];
        memcpy(name, character->record + 1, length);
        name[length] = '\0';
        ROW(name, true);
        size_t shown = 0;
        for (const cok_effect *e = character->effects; e != NULL; e = e->next) {
            char buffer[41], line[41] = " ";
            const char *what = effect_text(e->id, buffer);
            if (what == NULL) continue;
            append_text(line, sizeof line, what);
            ROW(line, false);
            ++shown;
        }
        if (shown == 0) ROW(" <No Spell Effects>", false);
        ROW(" ", false);
    }
#undef ROW
    for (size_t i = 0; i < count; ++i)
        cok_adventure_log(game, rows[i].heading ? "heading" : "item", rows[i].text);
    uint16_t moons[3];
    for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
    cok_screen_frame(&game->screen, &game->view.tiles[4], moons, true);
    cok_menu_style style = {"", "", 15, 10, 11, true};
    bool redraw = true;
    int index = 0;
    cok_keyboard keys = cok_adventure_keyboard(game);
    if (cok_menu_rows(&game->screen, &game->font, rows, count,
                      (cok_text_window){1, 4, 0x26, 0x16}, &style, &redraw, &index,
                      &game->list_top, &game->selected, &keys) < 0)
        return;
    cok_adventure_redraw(game);
}

/* Casting. */

int cok_magic_memorized(cok_adventure *game, cok_character *character)
{
    int index = -1;
    bool nonempty;
    return choose(game, character, 0, 0, &index, &nonempty);
}

int cok_magic_scroll(cok_adventure *game, cok_character *character, size_t item)
{
    used_scroll = item;
    int index = -1;
    bool nonempty;
    return choose(game, character, 2, 1, &index, &nonempty);
}

void cok_magic_cast(cok_adventure *game, uint8_t frame)
{
    game->spell_target = NULL;
    game->selected = 1;
    cok_character *character = selected(game);
    if (character == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "Cast with no character selected");
        return;
    }
    uint8_t *c = character->record;
    if (!can(game, c, 1)) return;
    bool redraw = false;
    int index = -1;
    for (;;) {
        bool nonempty;
        int spell = choose(game, character, 0, 1, &index, &nonempty);
        if (spell < 0) return;
        if (spell == 0) {
            if (nonempty)
                redraw = true;
            else
                cok_camp_say(game, c, "has no spells memorized", true);
            break;
        }
        redraw = true;
        cok_picture_fill(&game->screen, 1, 0x11 * 8, 0x26, 6 * 8, 0); /* 1128:07e6 */
        bool done = false;
        cok_cast_spell(game, (uint8_t)spell, true, frame, &done);
        if (game->vm.abort) return;
    }
    if (redraw) cok_adventure_redraw(game);
}

/* The menu. */

void cok_magic(cok_adventure *game, bool *interrupted)
{
    int key = ' ';
    while (!*interrupted && key != 0 && key != 'E' && !game->vm.abort) {
        bool special;
        key = cok_camp_menu(game, "", "Cast Memorize Scribe Display Rest Exit", true, true,
                            &special);
        if (key < 0) return;
        if (special) {
            cok_camp_pick(game, (uint8_t)key);
            continue;
        }
        switch (key) {
        case 'C': cok_magic_cast(game, COK_CAST_CAMP); break;
        case 'M': memorize(game); break;
        case 'S': scribe(game); break;
        case 'D': display(game); break;
        case 'R': *interrupted = cok_camp_prepare(game); break;
        default: break;
        }
    }
}
