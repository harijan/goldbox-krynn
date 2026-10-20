#include "camp.h"

#include "arena.h"
#include "cast.h"
#include "icon.h"
#include "magic.h"
#include "roster.h"
#include "screen.h"
#include "sheet.h"

#include <stdio.h>
#include <string.h>

/* Screen helpers. */

/* Clear cells x1-x2 of rows y1-y2 (1128:07e6, 1521:0b60). */
static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    cok_picture_fill(&game->screen, x1, y1 * 8, (size_t)(x2 - x1 + 1), (size_t)(y2 - y1 + 1) * 8,
                     0);
}

/* Clear the text window below its first row (6346:196a, outside combat). */
static void clear_text(cok_adventure *game)
{
    clear_cells(game, 1, 0x12, 0x26, 0x16);
}

/* Blank row 24 (67b5:0c7b). */
static void clear_menu(cok_adventure *game)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
}

/* Draw text at x, y in fg on 0 (1521:0353), logging it if log is set. */
static void draw(cok_adventure *game, const char *text, int x, int y, uint8_t fg, bool log)
{
    if (log) cok_adventure_log(game, "print", text);
    cok_text_string(&game->screen, &game->font, text, x, y, fg, 0);
}

static void name_of(const cok_character_layout *L, const uint8_t *c, char out[16])
{
    uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(out, c + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    out[length] = '\0';
}

/* Draw c's name at x, y: light red if it cannot act, else light cyan
 * (6346:199d outside combat), or light magenta if it may train (dkk
 * 65cd:1b3b, +0xc6). */
static void draw_name(cok_adventure *game, const uint8_t *c, int x, int y)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    char name[16];
    name_of(L, c, name);
    bool may = cok_game_rules(game)->combat.may_train &&
               cok_field_u8(L, c, COK_FIELD_MAY_TRAIN) != 0;
    draw(game, name, x, y, cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0 ? 12 : may ? 13 : 11, true);
}

/* Say text about c in the text window (6346:1883 outside combat): its name
 * on the window's second row, or the third while resting (DS:7138), and
 * the text wrapped below it; with wait, pause and clear the window. */
void cok_camp_notice(cok_adventure *game, const char *text)
{
    cok_adventure_notice(game, text, 10);
}

void cok_camp_clear_text(cok_adventure *game)
{
    clear_text(game);
}

void cok_camp_say(cok_adventure *game, const uint8_t *c, const char *text, bool wait)
{
    int top = game->resting ? 0x12 : 0x11;
    clear_cells(game, 1, top, 0x26, 0x16);
    draw_name(game, c, 1, top + 1);
    game->vm.cursor = (cok_text_cursor){1, top + 2};
    cok_adventure_print(game, text, (cok_text_window){1, top + 2, 0x26, 0x16}, 10, true);
    if (!wait || game->vm.abort) return;
    cok_adventure_wait(game, game->speed * 100u);
    clear_text(game);
}

/* One key from a menu on row 24 (67b5:03e2): prompt in light magenta,
 * items in white and light green. With show, the small picture's frame is
 * drawn first, as the camp's menus do, and animated while the menu waits
 * (game->menu_animates). */
int cok_camp_menu(cok_adventure *game, const char *prompt, const char *items, bool keypad,
                bool show, bool *special)
{
    if (show) cok_adventure_show_picture(game);
    cok_adventure_log(game, "menu", items);
    cok_keyboard keys = cok_adventure_keyboard(game);
    game->menu_animates = show;
    int key = cok_menu_ask(&game->screen, &game->font, prompt, items, 13, 15, 10, keypad,
                           &game->selected, &keys, special);
    game->menu_animates = false;
    return key;
}

/* Ask prompt, in prompt_color, until Yes or No (67b5:177f); No is
 * selected first, once, so an item chosen with the arrows stays chosen
 * through Escape and other keys. Special keys are not told apart, so scan
 * codes 0x4e and 0x59 answer too. A game whose menus go by index (dkk
 * 6a69:2048) asks with its loop. Returns 'Y', 'N', or -1 if input
 * ended. */
int cok_camp_yes_no(cok_adventure *game, const char *prompt, uint8_t prompt_color)
{
    return cok_camp_ask(game, prompt, prompt_color, false);
}

int cok_camp_ask(cok_adventure *game, const char *prompt, uint8_t prompt_color, bool yes)
{
    cok_adventure_log(game, "menu", prompt);
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key;
    game->selected = yes ? 1 : 2;
    const cok_rules_menus *K = &cok_game_rules(game)->menus;
    if (K->routine == COK_MENUS_INDEXED) {
        /* dkk 6a69:2048 with DS:57c1 (No) or 57c0 (Yes): the items by index
         * until Yes (0) or No; special keys are read past. Its prompt is
         * always light magenta (DS:c149, set once at startup). */
        cok_menu_items items;
        cok_menu_items_upto(&items, 1);
        do {
            bool special;
            key = cok_menu_pick(&game->screen, &game->font, prompt, K->yes_no, &items, 13, 15, 10,
                                false, &game->selected, &keys, &special);
            if (key < 0) return -1;
            game->menu_special = special; /* DS:c0f8 */
        } while (key > 1);
        key = key == 0 ? 'Y' : 'N';
        char text[2] = {(char)key, '\0'};
        cok_adventure_log(game, "choice", text);
        return key;
    }
    do {
        bool special;
        key = cok_menu_ask(&game->screen, &game->font, prompt, "Yes No", prompt_color, 15, 10,
                           false, &game->selected, &keys, &special);
        if (key < 0) return -1;
    } while (key != 'Y' && key != 'N');
    char text[2] = {(char)key, '\0'};
    cok_adventure_log(game, "choice", text);
    return key;
}

/* A special key from a camp menu picks a character (546c:3334) and
 * redraws the party list. */
void cok_camp_pick(cok_adventure *game, uint8_t scan)
{
    game->vm.character = cok_adventure_special(game, scan);
    cok_adventure_party(game);
}

/* Quit to DOS (1614:0000): the run ends. */
void cok_camp_quit(cok_adventure *game)
{
    cok_adventure_log(game, "quit", "to DOS");
    cok_adventure_quit(game);
}

/* Preparing spells. */

uint16_t cok_camp_preparation(cok_character *character, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    uint8_t memorize_max = 0, memorize_sum = 0, scribe_max = 0, scribe_sum = 0;
    for (size_t i = 0; i < cok_field_count(L, COK_FIELD_MEMORIZED); ++i) {
        if (cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) <= 0x7f) continue;
        uint8_t level =
            cok_spell_level(rules, cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) & 0x7f);
        if (level == 0) level = 1;
        if (level > memorize_max) memorize_max = level;
        memorize_sum = (uint8_t)(memorize_sum + level);
    }
    for (size_t i = 0; i < character->item_count; ++i) {
        const uint8_t *item = character->items[i];
        if (!cok_item_is_scroll(item)) continue;
        for (size_t k = 1; k <= 3; ++k) {
            if (item[0x3b + k] <= 0x7f) continue;
            uint8_t level = cok_spell_level(rules, item[0x3b + k] & 0x7f);
            if (level > scribe_max) scribe_max = level;
            scribe_sum = (uint8_t)(scribe_sum + level);
        }
    }
    uint8_t hours = 0;
    if (memorize_sum > 0 || scribe_sum > 0) hours = 4;
    if (memorize_max > 2 || scribe_max > 2) hours = 6;
    cok_field_set8(L, c, COK_FIELD_STUDY_HOURS, hours);
    return (uint16_t)(scribe_sum * 15 + memorize_sum * 15 + hours * 60);
}

void cok_camp_forget(cok_character *character, const cok_rules *rules)
{
    const cok_character_layout *L = rules->character;
    uint8_t *c = character->record;
    /* dkk 4381:0626: the scrolls' marks alone (05b0). */
    if (rules->levels.camp_forgets_memorized) {
        for (size_t i = 0; i < cok_field_count(L, COK_FIELD_MEMORIZED); ++i)
            if (cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) > 0x7f)
                cok_field_set8_at(L, c, COK_FIELD_MEMORIZED, i, 0);
        cok_field_set8(L, c, COK_FIELD_STUDY_HOURS, 0);
    }
    for (size_t i = 0; i < character->item_count; ++i) {
        uint8_t *item = character->items[i];
        if (!cok_item_is_scroll(item)) continue;
        for (size_t k = 0x3c; k <= 0x3e; ++k) item[k] &= 0x7f;
    }
}

static void forget_all(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count; ++i)
        cok_camp_forget(game->party.members[i], cok_game_rules(game));
}

/* The rest time. */

void cok_camp_normalize(cok_adventure *game)
{
    uint16_t *rest = game->rest;
    cok_adventure_carry(game, rest);
    if (rest[5] > 0) {
        rest[4] = (uint16_t)(rest[4] + cok_clock_units[4] * rest[5]);
        rest[5] = 0;
        if (rest[4] > 99) rest[4] = 99;
    }
}

static bool resting_left(const cok_adventure *game)
{
    const uint16_t *rest = game->rest;
    return rest[4] != 0 || rest[3] != 0 || rest[2] != 0 || rest[1] != 0;
}

void cok_camp_subtract(cok_adventure *game, uint8_t amount, unsigned unit)
{
    uint16_t *rest = game->rest;
    if (!resting_left(game) || unit > 4) return;
    while (amount > rest[unit]) {
        unsigned k = unit + 1;
        while (rest[k] == 0 && k < 5) ++k;
        if (k == 5) {
            memset(rest, 0, sizeof game->rest);
            amount = 0;
        } else {
            for (unsigned j = k; j > unit; --j) {
                --rest[j];
                rest[j - 1] = (uint16_t)(rest[j - 1] + cok_clock_units[j - 1]);
            }
        }
    }
    rest[unit] = (uint16_t)(rest[unit] - amount);
    cok_camp_normalize(game);
}

/* A byte as at least two digits, cut to two (57e4:06d9). */
static void two_digits(char out[3], uint8_t value)
{
    char text[8];
    snprintf(text, sizeof text, "%s%u", value < 10 ? "0" : "", value);
    out[0] = text[0];
    out[1] = text[1];
    out[2] = '\0';
}

/* Show the rest time on row 17 as days:hours:minutes, unit selected in
 * white (57e4:0764); the row is not cleared first. */
static void show_rest(cok_adventure *game, unsigned selected)
{
    uint8_t color[7] = {10, 10, 10, 10, 10, 10, 10};
    if (selected < 7) color[selected] = 15;
    draw(game, "Rest Time:", 1, 0x11, 10, false);
    char text[3];
    int x = 0x0b;
    for (unsigned i = 4; i >= 3; --i) {
        two_digits(text, (uint8_t)game->rest[i]);
        draw(game, text, x + 1, 0x11, color[i], false);
        draw(game, ":", x + 3, 0x11, 10, false);
        x += 3;
    }
    two_digits(text, (uint8_t)(10 * game->rest[2] + game->rest[1]));
    draw(game, text, 0x12, 0x11, color[2], false);
}

/* Set the rest time from a number of minutes, unnormalized (4888:0f05,
 * 4888:28f9): hours may pass 23. */
static void set_rest(cok_adventure *game, uint16_t minutes)
{
    game->rest[3] = (uint16_t)(minutes / 60);
    game->rest[2] = (uint16_t)((minutes - 60 * game->rest[3]) / 10);
    game->rest[1] = (uint16_t)(minutes % 10);
}

/* The rest menu (57e4:08a0): pick days, hours or minutes, add to them or
 * subtract from them, then Rest or Exit. Returns 1 to rest, 0 not to, -1
 * if input ended. */
static int rest_menu(cok_adventure *game)
{
    int result = 0;
    unsigned selected = 2;
    int key;
    do {
        show_rest(game, selected);
        bool special;
        key = cok_camp_menu(game, "", "Rest Days Hours Mins Add Subtract Exit", true, false, &special);
        if (key < 0) return -1;
        if (special) {
            if (key == 0x48) {
                key = 'A';
            } else if (key == 0x50) {
                key = 'S';
            } else {
                /* Left and right, which the menu itself takes. */
                if (key == 0x4b) selected = selected == 4 ? 2 : selected + 1;
                if (key == 0x4d) selected = selected == 2 ? 4 : selected - 1;
                key = 'X';
            }
        }
        if (key == 0x0d) key = 'R';
        switch (key) {
        case 'R': result = 1; break;
        case 'D': selected = 4; break;
        case 'H': selected = 3; break;
        case 'M': selected = 2; break;
        case 'A':
            if (selected == 2)
                game->rest[1] = (uint16_t)(game->rest[1] + 5);
            else
                ++game->rest[selected];
            cok_camp_normalize(game);
            break;
        case 'S':
            if (selected == 2)
                cok_camp_subtract(game, 5, 1);
            else
                cok_camp_subtract(game, 1, selected);
            cok_camp_normalize(game);
            break;
        default: break;
        }
    } while (key != 0 && key != 'E' && key != 'R');
    return result;
}

bool cok_camp_spell_message(cok_adventure *game, const uint8_t *c, const char *what,
                            uint8_t spell)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const char *name = cok_spell_name(cok_game_rules(game), spell);
    if (name == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "spell %u has no name; 5b04:57eb would print the data at DS:%04x",
                           spell,
                           cok_game_rules(game)->spells.names_at +
                               cok_game_rules(game)->spells.name_size * spell);
        return false;
    }
    clear_cells(game, 1, 0x12, 0x26, 0x16);
    draw_name(game, c, 1, 0x13);
    draw(game, what, cok_field_u8_at(L, c, COK_FIELD_NAME, 0) + 2, 0x13, 10, true);
    draw(game, name, 1, 0x14, 10, true);
    cok_adventure_wait(game, game->speed * 100u);
    clear_text(game);
    return true;
}

/* Learn c's first marked spell (57e4:0aaf), unless *flag is set; then, or
 * once one is learned, return the level of the next marked spell, 0 if
 * none. */
static uint8_t memorize(cok_adventure *game, cok_character *character, bool *flag)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record, level = 0;
    for (size_t i = 0; i < cok_field_count(L, COK_FIELD_MEMORIZED) && level == 0; ++i) {
        if (cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) <= 0x7f) continue;
        if (*flag) {
            level = cok_spell_level(cok_game_rules(game),
                                    cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i) & 0x7f);
            continue;
        }
        cok_field_add8_at(L, c, COK_FIELD_MEMORIZED, i, -0x80);
        show_rest(game, 0);
        if (!cok_camp_spell_message(game, c, "has memorized",
                                    cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, i)))
            return 0;
        *flag = true;
    }
    return level;
}

/* Take spell off scroll item after scribing it (5b04:575d): clear the last
 * of its bytes holding the spell and count one use (+0x2f). Returns whether
 * the count has dropped below 100, when the original frees the scroll
 * (6346:1697) without the item count (+0x142) or the stats being
 * recomputed. */
static bool use_scroll(uint8_t *item, uint8_t spell)
{
    size_t found = 0;
    for (size_t k = 1; k <= 3; ++k)
        if ((item[0x3b + k] & 0x7f) == spell) found = k;
    if (found == 0) return false;
    item[0x3b + found] = 0;
    item[0x2f] = (uint8_t)(item[0x2f] - 1);
    return item[0x2f] < 100;
}

/* Remove a used-up scroll from c's items. Fails when it is readied, where
 * the original keeps a pointer to the freed item. */
static bool free_scroll(cok_adventure *game, cok_character *character, size_t index)
{
    for (size_t s = 0; s < COK_ITEM_SLOTS; ++s) {
        if (character->slots[s] != index + 1) continue;
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a scribed scroll that is used up is readied; the original keeps a "
                           "pointer to it after freeing it (6346:1697)");
        return false;
    }
    cok_character_remove_item(character, index);
    return true;
}

/* Scribe c's first marked scroll spell (57e4:0b71), unless *flag is set;
 * then, or once one is scribed, return the level of the next marked one
 * above 0x80 (not 0x7f, as 4888:0032 counts), 0 if none. A scroll that is
 * used up ends the walk, as the original reads the freed scroll's next
 * pointer, which 6346:1697 clears; it still reads the rest of the freed
 * scroll's spells. */
static uint8_t scribe(cok_adventure *game, cok_character *character, bool *flag)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record, level = 0;
    for (size_t i = 0; i < character->item_count && level == 0; ++i) {
        uint8_t *item = character->items[i], freed[COK_ITEM_SIZE];
        if (!cok_item_is_scroll(item)) continue;
        bool gone = false;
        for (size_t k = 1; k <= 3 && level == 0; ++k) {
            uint8_t b = item[0x3b + k];
            if (b <= 0x80) continue;
            if (*flag) {
                level = cok_spell_level(cok_game_rules(game), b & 0x7f);
                continue;
            }
            uint8_t spell = b & 0x7f;
            /* Spells 109-127 too, as the original's +0x62 + spell writes
             * them, past the known spells (into +0xcf-+0xe1, within the
             * record): unchecked. */
            c[cok_field_offset_unchecked(L, COK_FIELD_KNOWN_SPELL, 0, spell)] = 1;
            if (use_scroll(item, spell)) {
                memcpy(freed, item, sizeof freed);
                if (!free_scroll(game, character, i)) return 0;
                item = freed;
                gone = true;
            }
            show_rest(game, 0);
            if (!cok_camp_spell_message(game, c, "has scribed", spell)) return 0;
            *flag = true;
        }
        if (gone) break;
    }
    return level;
}

/* Each tick, count down each member's time to its next spell; at 0, once
 * its hours of preparation are over, scribe or learn one and time the next
 * at 15 minutes a level (57e4:0c9c). Unless learn: Death Knights' Fix sets
 * DS:c46f, which keeps 5b7d:0aa3 and 09b9 from scribing and learning (the
 * timer then set from 0aa3's uninitialized byte, which nothing reads: each
 * rest clears the timers first). */
static void learn_tick(cok_adventure *game, bool learn)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (size_t i = 0; i < game->party.count && !game->vm.abort; ++i) {
        cok_character *character = game->party.members[i];
        if (game->learn_ticks[i] > 0) --game->learn_ticks[i];
        if (game->learn_ticks[i] != 0 ||
            cok_field_u8(L, character->record, COK_FIELD_STUDY_HOURS) != 0)
            continue;
        if (!learn) continue;
        bool flag = false;
        uint8_t level = scribe(game, character, &flag);
        if (level == 0 && !game->vm.abort) level = memorize(game, character, &flag);
        game->learn_ticks[i] = (uint8_t)(level * 3);
    }
}

/* Each hour (*ticks reaching 12), count down each member's hours of
 * preparation, and when they end, time its first spell (57e4:0d52), unless
 * learn is clear (DS:c46f, see learn_tick): the hours count down all the
 * same. */
static void hourly(cok_adventure *game, uint8_t *ticks, bool learn)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (++*ticks < 12) return;
    *ticks = 0;
    for (size_t i = 0; i < game->party.count && !game->vm.abort; ++i) {
        cok_character *character = game->party.members[i];
        uint8_t *c = character->record;
        if (cok_field_s8(L, c, COK_FIELD_STUDY_HOURS) <= 0) continue;
        cok_field_add8(L, c, COK_FIELD_STUDY_HOURS, -1);
        if (cok_field_u8(L, c, COK_FIELD_STUDY_HOURS) != 0 || !learn) continue;
        bool flag = true;
        uint8_t level = scribe(game, character, &flag);
        if (level == 0) level = memorize(game, character, &flag);
        game->learn_ticks[i] = (uint8_t)(level * 3);
    }
}

/* Each day of rest (288 ticks, counted across rests in one camp), each
 * member gains a hit point (57e4:09f0). */
static void heal_tick(cok_adventure *game, bool interactive)
{
    if (++game->heal_ticks < 0x120) return;
    bool healed = false;
    for (size_t i = 0; i < game->party.count; ++i)
        if (cok_character_heal(game->party.members[i]->record, cok_game_rules(game), 1, false,
                               game->vm.mode))
            healed = true;
    if (interactive) show_rest(game, 0);
    draw(game, cok_game_rules(game)->levels.party_healed, 1, 0x13, 10, true);
    if (healed) cok_adventure_party(game);
    cok_adventure_wait(game, game->speed * 100u);
    clear_text(game);
    game->heal_ticks = 0;
}

/* cok_camp_rest; learn clear for Death Knights' Fix (DS:c46f set: no spell
 * is learned or scribed). */
static bool rest(cok_adventure *game, bool interactive, bool learn)
{
    cok_ecl *vm = &game->vm;
    bool interrupted = false, stop = false;
    uint8_t ticks = 0, hours = 0;
    /* The timers are by position in the whole list (DS:712d + 1 on); a
     * ninth record, a monster loaded and not yet fought, would write over
     * the healing count and the resting flag after them. */
    if (game->party.count > COK_PARTY_MAX) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "resting with %zu records in the party list overruns the learning "
                           "timers (57e4:0e3e, DS:712e)", game->party.count);
        return false;
    }
    for (size_t i = 0; i < game->party.count; ++i) game->learn_ticks[i] = 0;
    memset(game->effects.timed, 1, sizeof game->effects.timed);
    if (interactive) {
        clear_cells(game, 1, 0x11, 0x26, 0x16);
        show_rest(game, 0);
    }
    game->resting = true;
    if (interactive) {
        int rest = rest_menu(game);
        stop = rest != 1;
    }
    while (!stop && !vm->abort && resting_left(game)) {
        if (interactive && cok_adventure_key_pending(game)) {
            show_rest(game, 0);
            int answer = cok_camp_yes_no(game, "Stop Resting? ", 13);
            if (answer < 0) break;
            if (answer == 'Y') {
                stop = true;
                continue;
            }
            clear_menu(game);
        }
        cok_camp_subtract(game, 5, 1);
        if (interactive && ++ticks >= 5) {
            show_rest(game, 0);
            ticks = 0;
        }
        if (!cok_adventure_pass_time(game, 1, 5)) break;
        heal_tick(game, interactive);
        learn_tick(game, learn);
        if (!vm->abort) hourly(game, &hours, learn);
        if (vm->abort) break;
        uint16_t every = cok_ecl_var(vm, COK_VAR_REST_EVERY),
                 chance = cok_ecl_var(vm, COK_VAR_REST_CHANCE);
        if (every == 0 || ++game->rest_ticks < every) continue;
        game->rest_ticks = 0;
        if (cok_dice(&vm->seed, 1, 100) > chance) continue; /* 57e4:0fbe */
        clear_text(game);
        show_rest(game, 0);
        draw(game, cok_game_rules(game)->levels.interrupted, 1, 0x13, 15, true);
        stop = interrupted = true;
        cok_adventure_wait(game, game->speed * 100u);
    }
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    game->resting = false;
    return interrupted;
}

bool cok_camp_rest(cok_adventure *game, bool interactive)
{
    return rest(game, interactive, true);
}

/* Rest (4888:0f05): rest as long as the member who needs longest needs to
 * learn and scribe its marked spells, or as the player chooses. */
bool cok_camp_prepare(cok_adventure *game)
{
    uint16_t longest = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint16_t minutes = cok_camp_preparation(game->party.members[i], cok_game_rules(game));
        if (minutes > longest) longest = minutes;
    }
    set_rest(game, longest);
    bool interrupted = cok_camp_rest(game, true);
    memset(game->rest, 0, sizeof game->rest);
    cok_adventure_status(game);
    return interrupted;
}

/* Fix. */

/* The hit points the party lacks (4888:289b), as a word that wraps when a
 * member has more than its maximum. */
static uint16_t missing(const cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint16_t sum = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        sum = (uint16_t)(sum + cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) -
                         cok_field_u8(L, c, COK_FIELD_HIT_POINTS));
    }
    return sum;
}

/* The dice (60f4:1216). */
static uint8_t roll(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice(&game->vm.seed, count, sides);
}

/* The hit points cure spells heal: Cure Light Wounds 1d8, Cure Serious
 * 2d8+1, Cure Critical 3d8+3. */
static uint16_t cure(cok_adventure *game, unsigned kind)
{
    if (kind == 0) return roll(game, 1, 8);
    if (kind == 1) return (uint16_t)(roll(game, 2, 8) + 1);
    return (uint16_t)(roll(game, 3, 8) + 3);
}

/* Champions' (4888:2b44). */
static bool fix(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_ecl *vm = &game->vm;
    if (missing(game) == 0) return false;
    /* 4888:272e: the cure spells memorized by members who are okay; they
     * are not used up. */
    uint16_t pool = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        if (cok_field_u8(L, c, COK_FIELD_STATUS) != 0) continue;
        for (size_t s = 0; s < cok_field_count(L, COK_FIELD_MEMORIZED); ++s) {
            uint8_t spell = cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, s);
            const cok_rules *rules = cok_game_rules(game);
            if (cok_spell_is(rules, spell, COK_SPELL_CURE_LIGHT_WOUNDS))
                pool = (uint16_t)(pool + cure(game, 0));
            else if (cok_spell_is(rules, spell, COK_SPELL_CURE_SERIOUS_WOUNDS))
                pool = (uint16_t)(pool + cure(game, 1));
            else if (cok_spell_is(rules, spell, COK_SPELL_CURE_CRITICAL_WOUNDS))
                pool = (uint16_t)(pool + cure(game, 2));
        }
    }
    if (missing(game) == 0) {
        cok_adventure_party(game);
        cok_adventure_status(game);
        return false;
    }
    uint16_t saved[7];
    memcpy(saved, game->rest, sizeof saved);
    /* 4888:28f9: the cleric spells a day of levels 1, 4 and 5 of members
     * who are okay, and how long they take. The estimate starts with what
     * the stack holds, 14, the count of the Move before; a member who is
     * not okay repeats the times of the one before. */
    uint16_t counts[3] = {0, 0, 0}, estimate = 14, longest = 0;
    uint16_t a = 0, b = 0, d = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        uint16_t time = 0;
        if (cok_field_u8(L, c, COK_FIELD_STATUS) == 0) {
            uint8_t first = cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 1),
                    fourth = cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 4),
                    fifth = cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, 0, 5);
            counts[0] = (uint16_t)(counts[0] + first);
            a = (uint16_t)(first * 15);
            counts[1] = (uint16_t)(counts[1] + fourth);
            b = (uint16_t)(fourth * 60);
            counts[2] = (uint16_t)(counts[2] + fifth);
            d = (uint16_t)(fifth * 75);
        }
        if (a > 0) {
            time = 240;
            estimate = (uint16_t)(estimate + 27);
        }
        if ((uint16_t)(b + d) != 0) {
            time = 360;
            estimate = (uint16_t)(estimate + (d > 0 ? 78 : 34));
        }
        time = (uint16_t)(time + a + b + d);
        if (time > longest) longest = time;
    }
    uint16_t lacking = missing(game);
    if (lacking < estimate) {
        uint8_t ratio = (uint8_t)(estimate / lacking);
        if (ratio == 0) {
            cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                               "Fix divides by %u / %u as a byte, 0 (4888:2a4e)", estimate,
                               lacking);
            return false;
        }
        longest = (uint16_t)(longest / ratio);
    }
    set_rest(game, longest);
    bool interrupted = cok_camp_rest(game, false);
    if (interrupted || vm->abort) return interrupted;
    /* 4888:27e9: the spells memorized in that time. */
    for (unsigned kind = 0; kind < 3; ++kind)
        for (unsigned n = 1; n <= (uint8_t)counts[kind]; ++n)
            pool = (uint16_t)(pool + cure(game, kind));
    /* 4888:2a8c: heal each member in turn from the pool. */
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *c = game->party.members[i]->record;
        uint8_t most = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS),
                now = cok_field_u8(L, c, COK_FIELD_HIT_POINTS);
        if (most <= now) continue;
        uint16_t amount = (uint16_t)(most - now);
        if (amount > pool) amount = pool;
        if (amount == 0) continue;
        if (cok_character_heal(c, cok_game_rules(game), (uint8_t)amount, false, vm->mode) &&
            amount <= pool)
            pool = (uint16_t)(pool - amount);
    }
    cok_adventure_party(game);
    cok_adventure_status(game);
    memcpy(game->rest, saved, sizeof saved);
    return false;
}

/* Death Knights' Fix (dkk 4381:2fd4): its nested routines share its frame,
 * which dk_fix holds. Its constants are the routines' own. */

/* The cures, each with the spells for it (memorized, then the spells a day
 * as well, words of 2fd4's frame) and the members needing it (2ebc). */
enum { DK_HEAL, DK_STONE, DK_POISON, DK_RAISE, DK_CURSE, DK_SIGHT, DK_CURES };

typedef struct {
    uint16_t pool;          /* bp-2: hit points to heal. */
    uint16_t slots[4];      /* bp-4 to -0xa: spells a day of the cures, 1d8 to 4d8 + 3. */
    uint16_t spells[DK_CURES]; /* bp-0x18, -0x20, -0x1c, -0xc, -0x10, -0x14 */
    uint16_t needy[DK_CURES];  /* bp-0x1a, -0x22, -0x1e, -0xe, -0x12, -0x16 */
} dk_fix_frame;

/* 4381:22a9: a member who is okay and has a spells' block. */
static bool dk_able(const cok_character_layout *L, const uint8_t *c)
{
    return cok_field_u8(L, c, COK_FIELD_STATUS) == 0 && cok_field_has_spells(L, c);
}

/* 4381:22db: the able members' spells memorized, not used up: Cure Light
 * (0x03) and Serious Wounds (0x3a) 2d8 + 1, Cure Critical (0x47) 3d8 + 3,
 * Heal (0x35) 4d8 + 5, into the pool; Heal and the cures counted. */
static void dk_memorized(cok_adventure *game, dk_fix_frame *f)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint16_t pool = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        if (!dk_able(L, c)) continue;
        for (size_t k = 0; k < cok_field_count(L, COK_FIELD_MEMORIZED); ++k) {
            switch (cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, k)) {
            case 0x03:
            case 0x3a: pool = (uint16_t)(pool + roll(game, 2, 8) + 1); break;
            case 0x47: pool = (uint16_t)(pool + roll(game, 3, 8) + 3); break;
            case 0x35:
                pool = (uint16_t)(pool + roll(game, 4, 8) + 5);
                ++f->spells[DK_HEAL];
                break;
            case 0x43: ++f->spells[DK_POISON]; break;     /* Neutralize Poison */
            case 0x4b: ++f->spells[DK_RAISE]; break;      /* Raise Dead */
            case 0x2b: case 0x59: ++f->spells[DK_CURSE]; break; /* Remove Curse */
            case 0x42: ++f->spells[DK_STONE]; break;      /* Stone to Flesh */
            case 0x25: ++f->spells[DK_SIGHT]; break;      /* Cure Blindness */
            default: break;
            }
        }
    }
    f->pool = (uint16_t)(f->pool + pool);
}

/* 4381:2537, as 4888:28f9: the minutes the able members of wisdom above 3
 * take to memorize their spells a day of the cures (cleric level 1 and
 * druid level 2, 15 and 30 minutes each; cleric levels 4, 5 and 6, 60, 75
 * and 90; and 4, 4, 6 or 8 hours before), counting them, shortened by the
 * ratio of an estimate of the points healed to the points lacking. The
 * estimate starts from an uninitialized local, which holds the Move's
 * count before it, 14, as Champions'; a member not counted repeats the
 * times of the one before. False where the ratio is 0 as a byte, a
 * division by zero (4381:273b). */
static bool dk_time(cok_adventure *game, dk_fix_frame *f, uint16_t *minutes)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint16_t estimate = 14, longest = 0, t[5] = {0, 0, 0, 0, 0};
    memset(f->slots, 0, sizeof f->slots);
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        uint16_t time = 0;
        if (dk_able(L, c) && cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_WISDOM) > 3) {
            static const struct {
                uint8_t row, level, slot, minutes;
            } day[5] = {{0, 1, 0, 15}, {1, 2, 0, 30}, {0, 4, 1, 60}, {0, 5, 2, 75}, {0, 6, 3, 90}};
            for (size_t k = 0; k < 5; ++k) {
                uint8_t n = cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, day[k].row, day[k].level);
                f->slots[day[k].slot] = (uint16_t)(f->slots[day[k].slot] + n);
                t[k] = (uint16_t)(n * day[k].minutes);
            }
        }
        if (t[0] > 0) {
            time = 240;
            estimate = (uint16_t)(estimate + 27);
        }
        if (t[1] > 0) {
            time = 240;
            estimate = (uint16_t)(estimate + 27);
        }
        if ((uint16_t)(t[2] + t[3]) != 0) {
            time = 360;
            estimate = (uint16_t)(estimate + (t[3] > 0 ? 78 : 34));
        }
        if (t[4] > 0) {
            time = 480;
            estimate = (uint16_t)(estimate + 90);
        }
        time = (uint16_t)(time + t[0] + t[1] + t[2] + t[3] + t[4]);
        if (time > longest) longest = time;
    }
    uint16_t lacking = missing(game);
    if (lacking < estimate) {
        uint8_t ratio = (uint8_t)(estimate / lacking);
        if (ratio == 0) {
            cok_adventure_fail(game, COK_ECL_DIVIDE_BY_ZERO,
                               "Fix divides by %u / %u as a byte, 0 (dkk 4381:273b)", estimate,
                               lacking);
            return false;
        }
        longest = (uint16_t)(longest / ratio);
    }
    *minutes = longest;
    return true;
}

/* 4381:23f3: the spells a day's cures, 1d8, 2d8 + 1, 3d8 + 3 and 4d8 + 3
 * each, the counts as bytes. */
static void dk_slots(cok_adventure *game, dk_fix_frame *f)
{
    static const uint8_t dice[4] = {1, 2, 3, 4}, plus[4] = {0, 1, 3, 3};
    uint16_t pool = 0;
    for (unsigned k = 0; k < 4; ++k)
        for (unsigned n = 1; n <= (uint8_t)f->slots[k]; ++n)
            pool = (uint16_t)(pool + roll(game, dice[k], 8) + plus[k]);
    f->pool = (uint16_t)(f->pool + pool);
}

/* Remove c's first effect id (638c:01c6), ending the run where that fails. */
static bool dk_remove(cok_adventure *game, cok_character *c, uint8_t id)
{
    return cok_effects_remove(&game->effects, c, NULL, id) || cok_adventure_effect_failed(game);
}

/* 4381:2a73's Raise Dead on c (status 6, not an elf): its poison (0x37) goes
 * while curing (DS:be75); 1 hit point, okay, able to act, a point of
 * constitution less (with no recompute); and from a constitution left of
 * 14 or more, the maximum hit points above the base (+0x6c), divided by a
 * byte sum over class levels 0-6 (each level, two for one above 15, those
 * of classes 2-4 times the constitution less 14; none when the sum is 0),
 * come off the maximum, unless the constitution is 17 or more with no
 * level of class 2, 3 or 7: Champions' temple's (340d:0688). */
static bool dk_raise(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    game->effects.rolls.curing = 1;
    if (!dk_remove(game, character, cok_game_effect(game, COK_EFFECT_POISONED))) return false;
    game->effects.rolls.curing = 0;
    cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 1);
    cok_field_set8(L, c, COK_FIELD_STATUS, 0);
    cok_field_set8(L, c, COK_FIELD_ACTIVE, 1);
    if (cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION) > 0)
        cok_field_add8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION, -1);
    uint8_t most = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS),
            base = cok_field_u8(L, c, COK_FIELD_BASE_HIT_POINTS);
    uint8_t loss = most > base ? (uint8_t)(most - base) : 0, sum = 0;
    uint8_t con = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION);
    if (con < 14) return true;
    for (int i = 0; i <= 6; ++i) {
        int8_t level = cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i);
        if (level <= 0) continue;
        if (i >= 2 && i <= 4)
            sum = (uint8_t)(sum + (uint16_t)level * (uint16_t)(con - 14));
        else
            sum = (uint8_t)(sum + (con > 15 ? 2 * level : level));
    }
    if (sum > 0) loss = (uint8_t)(loss / sum);
    if (con < 17 || cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 2) > 0 ||
        cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 7) > 0 ||
        cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 3) > 0)
        cok_field_set8(L, c, COK_FIELD_MAX_HIT_POINTS, (uint8_t)(most - loss));
    return true;
}

/* 4381:2a73: each member in turn has the cures it needs while their
 * spells last, each said about it on row 10 with a pause (65cd:1a21; the
 * words the code segment's, 4381:29ff-2a65): Heal, for one with 0x3d,
 * which goes, and blindness (0x21) too, "is healed"; Stone to Flesh, for
 * one stoned (status 7): okay, able to act, 1 hit point, "is changed back
 * to flesh"; Neutralize Poison, for one poisoned (0x37): at least a hit
 * point, 0x37, 0x16 and 0x0f go while curing, okay and able to act, "is
 * detoxified"; Raise Dead, for one dead (status 6) and not an elf or a
 * half-elf (race 0, 1 or 2), "has been raised from the dead"; Remove
 * Curse, for one with Bestow Curse's 0x24: the spell's routine on it
 * (5dc5:311b, DS:c31d), "is relieved of a curse"; and Cure Blindness, for
 * one blind (0x21) whom Heal did not cure: the selected character's
 * blindness goes and it "regains sight", using no spell. */
static bool dk_cures(cok_adventure *game, dk_fix_frame *f)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t mind = 0x3d, blind = cok_game_effect(game, COK_EFFECT_BLIND);
    for (size_t i = 0; i < game->party.count && !game->vm.abort; ++i) {
        cok_character *m = game->party.members[i];
        uint8_t *c = m->record;
        bool healed = false;
        if (f->spells[DK_HEAL] > 0 && cok_character_find_effect(m, mind) != NULL) {
            if (!dk_remove(game, m, mind)) return false;
            if (cok_character_find_effect(m, blind) != NULL && !dk_remove(game, m, blind))
                return false;
            healed = true;
            if (!cok_arena_say(game, m, "is healed", 10, true)) return false;
            --f->spells[DK_HEAL];
        }
        if (f->spells[DK_STONE] > 0 && cok_field_u8(L, c, COK_FIELD_STATUS) == 7) {
            cok_field_set8(L, c, COK_FIELD_STATUS, 0);
            cok_field_set8(L, c, COK_FIELD_ACTIVE, 1);
            cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 1);
            if (!cok_arena_say(game, m, "is changed back to flesh", 10, true)) return false;
            --f->spells[DK_STONE];
        }
        if (f->spells[DK_POISON] > 0 &&
            cok_character_find_named(m, cok_game_rules(game), COK_EFFECT_POISONED) != NULL) {
            if (cok_field_u8(L, c, COK_FIELD_HIT_POINTS) == 0)
                cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 1);
            game->effects.rolls.curing = 1;
            const uint8_t poisons[3] = {cok_game_effect(game, COK_EFFECT_POISONED),
                                        cok_game_effect(game, COK_EFFECT_SLOW_POISON),
                                        cok_game_effect(game, COK_EFFECT_POISON)};
            for (size_t k = 0; k < sizeof poisons; ++k)
                if (!dk_remove(game, m, poisons[k])) return false;
            game->effects.rolls.curing = 0;
            cok_field_set8(L, c, COK_FIELD_ACTIVE, 1);
            cok_field_set8(L, c, COK_FIELD_STATUS, 0);
            if (!cok_arena_say(game, m, "is detoxified", 10, true)) return false;
            --f->spells[DK_POISON];
        }
        int8_t race = cok_field_s8(L, c, COK_FIELD_RACE);
        if (f->spells[DK_RAISE] > 0 && cok_field_u8(L, c, COK_FIELD_STATUS) == 6 && race != 2 &&
            race > 1) {
            if (!dk_raise(game, m) ||
                !cok_arena_say(game, m, "has been raised from the dead", 10, true))
                return false;
            --f->spells[DK_RAISE];
        }
        if (f->spells[DK_CURSE] > 0 &&
            cok_character_find_named(m, cok_game_rules(game), COK_EFFECT_CURSED) != NULL) {
            if (!cok_cast_on(game, cok_game_spell(game, COK_SPELL_REMOVE_CURSE), m, false) ||
                !cok_arena_say(game, m, "is relieved of a curse", 10, true))
                return false;
            --f->spells[DK_CURSE];
        }
        if (!healed && f->spells[DK_SIGHT] > 0 && cok_character_find_effect(m, blind) != NULL) {
            cok_character *selected = cok_adventure_selected(game);
            if (selected == NULL) {
                cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                   "Fix's Cure Blindness cures the selected character, and none "
                                   "is selected (dkk 4381:2e68)");
                return false;
            }
            if (!dk_remove(game, selected, blind) ||
                !cok_arena_say(game, selected, "regains sight", 10, true))
                return false;
        }
    }
    return !game->vm.abort;
}

/* 4381:2947, as 4888:2a8c: heal each member in turn from the pool. */
static void dk_heal(cok_adventure *game, dk_fix_frame *f)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *c = game->party.members[i]->record;
        uint8_t most = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS),
                now = cok_field_u8(L, c, COK_FIELD_HIT_POINTS);
        if (most <= now) continue;
        uint16_t amount = (uint16_t)(most - now);
        if (amount > f->pool) amount = f->pool;
        if (amount == 0) continue;
        if (cok_character_heal(c, cok_game_rules(game), (uint8_t)amount, false, game->vm.mode) &&
            amount <= f->pool)
            f->pool = (uint16_t)(f->pool - amount);
    }
}

/* 4381:2ebc: the members needing each cure: the dead (status 6) of race
 * above 1, the stoned, the cursed (0x24), the poisoned (0x37), those with
 * 0x3d and the blind without it. */
static void dk_needy(cok_adventure *game, dk_fix_frame *f)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_rules *R = cok_game_rules(game);
    memset(f->needy, 0, sizeof f->needy);
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *m = game->party.members[i];
        const uint8_t *c = m->record;
        if (cok_field_u8(L, c, COK_FIELD_STATUS) == 6 && cok_field_s8(L, c, COK_FIELD_RACE) > 1)
            ++f->needy[DK_RAISE];
        if (cok_field_u8(L, c, COK_FIELD_STATUS) == 7) ++f->needy[DK_STONE];
        if (cok_character_find_named(m, R, COK_EFFECT_CURSED) != NULL) ++f->needy[DK_CURSE];
        if (cok_character_find_named(m, R, COK_EFFECT_POISONED) != NULL) ++f->needy[DK_POISON];
        if (cok_character_find_effect(m, 0x3d) != NULL)
            ++f->needy[DK_HEAL];
        else if (cok_character_find_named(m, R, COK_EFFECT_BLIND) != NULL)
            ++f->needy[DK_SIGHT];
    }
}

/* 4381:2779: the able members' spells a day of the cures join those left
 * (Heal cleric level 6, Stone to Flesh magic-user 6, Raise Dead cleric 5,
 * Neutralize Poison cleric 4, Remove Curse magic-user 4 and cleric 3,
 * Cure Blindness cleric 3); the second rest takes, for as many members as
 * need each and its spells can cure (4381:0000, the smaller), 90 minutes
 * each for Heal and Stone to Flesh, 75 for Raise Dead, 60 for Neutralize
 * Poison and 45 for Cure Blindness and Remove Curse. */
static uint16_t dk_more(cok_adventure *game, dk_fix_frame *f)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        if (!dk_able(L, c)) continue;
#define DAY(row, level) cok_field_u8_at2(L, c, COK_FIELD_SPELLS_A_DAY, row, level)
        f->spells[DK_HEAL] = (uint16_t)(f->spells[DK_HEAL] + DAY(0, 6));
        f->spells[DK_STONE] = (uint16_t)(f->spells[DK_STONE] + DAY(3, 6));
        f->spells[DK_RAISE] = (uint16_t)(f->spells[DK_RAISE] + DAY(0, 5));
        f->spells[DK_POISON] = (uint16_t)(f->spells[DK_POISON] + DAY(0, 4));
        f->spells[DK_CURSE] = (uint16_t)(f->spells[DK_CURSE] + DAY(3, 4) + DAY(0, 3));
        f->spells[DK_SIGHT] = (uint16_t)(f->spells[DK_SIGHT] + DAY(0, 3));
#undef DAY
    }
    static const uint8_t minutes[DK_CURES] = {
        [DK_HEAL] = 90, [DK_STONE] = 90, [DK_RAISE] = 75, [DK_POISON] = 60,
        [DK_SIGHT] = 45, [DK_CURSE] = 45,
    };
    uint16_t total = 0;
    for (size_t k = 0; k < DK_CURES; ++k) {
        uint16_t n = f->spells[k] < f->needy[k] ? f->spells[k] : f->needy[k];
        total = (uint16_t)(total + n * minutes[k]);
    }
    return total;
}

/* 4381:2fd4: nothing where the area forbids magic (var 0x4be5) or no one
 * lacks hit points; else the memorized spells' cures are counted, the
 * party rests (DS:c46f set: learning nothing) as long as the spells a day
 * of the cures take, and then is cured and healed from them all; then,
 * the rest time restored, it rests again as long as the cures still
 * needed take, and those spells left and the spells a day cure again,
 * healing no one. An encounter ends it, leaving the rest time it set. */
static bool dk_fix(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (cok_ecl_var(vm, COK_VAR_NO_MAGIC) != 0 || missing(game) == 0) return false;
    dk_fix_frame f;
    memset(&f, 0, sizeof f);
    dk_memorized(game, &f);
    uint16_t saved[7], minutes;
    memcpy(saved, game->rest, sizeof saved);
    if (!dk_time(game, &f, &minutes)) return false;
    set_rest(game, minutes);
    bool interrupted = rest(game, false, false);
    if (interrupted || vm->abort) return interrupted;
    dk_slots(game, &f);
    if (!dk_cures(game, &f)) return false;
    dk_heal(game, &f);
    cok_adventure_party(game);
    cok_adventure_status(game);
    memcpy(game->rest, saved, sizeof saved);
    dk_needy(game, &f);
    set_rest(game, dk_more(game, &f));
    interrupted = rest(game, false, false);
    if (interrupted || vm->abort) return interrupted;
    if (!dk_cures(game, &f)) return false;
    cok_adventure_party(game);
    cok_adventure_status(game);
    memcpy(game->rest, saved, sizeof saved);
    return false;
}

bool cok_camp_fix(cok_adventure *game)
{
    if (cok_game_rules(game)->levels.fix == COK_FIX_DEATH_KNIGHTS) return dk_fix(game);
    return fix(game);
}

/* Alter. */

/* Party Order (4888:1fc1): Select a character, then up and down (8 and 2)
 * move it, and Place puts it down. */
static void order(cok_adventure *game)
{
    bool moving = false;
    int key = ' ';
    while (key != 0 && key != 'E' && !game->vm.abort) {
        bool special;
        key = cok_camp_menu(game, "Party Order: ", moving ? "Place Exit" : "Select Exit", true, true,
                   &special);
        if (key < 0) return;
        if (special) {
            if (!moving) {
                cok_camp_pick(game, (uint8_t)key);
                continue;
            }
            size_t i = cok_party_index(&game->party, game->vm.character);
            if (i == game->party.count) {
                cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                   "moving a character not in the party (4888:1cea)");
                return;
            }
            if (key == 0x48) cok_party_move(&game->party, i, false);
            else if (key == 0x50) cok_party_move(&game->party, i, true);
            cok_adventure_party(game);
            continue;
        }
        if (key != 0x0d && key != 'P' && key != 'S') continue;
        moving = !moving;
        if (moving && game->vm.character != NULL)
            cok_camp_say(game, game->vm.character, "has been selected", false);
        else if (!moving)
            clear_text(game);
    }
}

/* Remove the selected character from the party (4def:3b0a with 0, 1),
 * freeing its icons, counting it out of 0x7f3e, and select the one before
 * it, or the first. */
static void remove_selected(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_ecl *vm = &game->vm;
    size_t i = cok_party_index(&game->party, vm->character);
    if (i == game->party.count) return;
    /* The original keeps pointers to the record it frees in the spell
     * target and the trade partner (DS:710b, 46b0); the port forgets them. */
    uint8_t *gone = game->party.members[i]->record;
    if (game->spell_target == gone) game->spell_target = NULL;
    if (game->trade_partner == gone) game->trade_partner = NULL;
    uint8_t icon = cok_field_u8(L, gone, COK_FIELD_ICON);
    if (icon < cok_icon_slots(game)) cok_arena_free_icon(game, icon); /* 6d21:0156 */
    cok_cast_forget(game, game->party.members[i]);
    cok_party_remove(&game->party, i);
    cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, -1);
    vm->character = cok_party_record(&game->party, i > 0 ? i - 1 : 0);
}

/* Drop (4888:2126): drop the selected character after asking, or with the
 * party's last, quit. */
static void drop(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_ecl *vm = &game->vm;
    uint8_t *c = vm->character;
    if (c == NULL || cok_party_index(&game->party, c) == game->party.count) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "Drop with no character selected reads through NULL (4888:2126)");
        return;
    }
    if (game->party.count == 1) {
        if (cok_camp_yes_no(game, "quit TO DOS: ", 14) != 'Y') return;
        remove_selected(game);
        cok_camp_quit(game);
        return;
    }
    cok_camp_say(game, c, "will be gone", false);
    int answer = cok_camp_yes_no(game, "Drop from party? ", 14);
    if (answer < 0) return;
    if (answer != 'Y') {
        cok_camp_say(game, c, "Breathes A sigh of relief", true);
        return;
    }
    cok_camp_say(game, c,
                 cok_field_u8(L, c, COK_FIELD_ACTIVE) != 0 ? "bids you farewell"
                                                           : "is dumped in a ditch",
                 true);
    if (vm->abort) return;
    remove_selected(game);
    clear_cells(game, 0x11, 1, 0x26, 0x0b);
    cok_adventure_party(game);
}

/* Game Speed (4888:2384): Faster and Slower, or down and up (2 and 8),
 * from 0 to 9. */
static void speed(cok_adventure *game)
{
    int key;
    do {
        char text[48];
        snprintf(text, sizeof text, "Game Speed = %u (0=fastest 9=slowest)", game->speed);
        draw(game, text, 1, 0x12, 10, true);
        char items[41] = "";
        if (game->speed > 0) strcat(items, " Faster");
        if (game->speed < 9) strcat(items, " Slower");
        strcat(items, " Exit");
        bool special;
        key = cok_camp_menu(game, "Game Speed:", items, true, true, &special);
        if (key < 0) return;
        if (special) {
            if (key == 0x50 && game->speed > 0) --game->speed;
            else if (key == 0x48 && game->speed < 9) ++game->speed;
        } else if (key == 'F') {
            --game->speed;
        } else if (key == 'S') {
            ++game->speed;
        }
    } while (key != 0 && key != 'E');
    clear_text(game);
}

/* Pics (in 4888:2539): P toggles the pictures flag, which nothing else
 * reads, and A the animation, without reloading the picture shown. Special
 * keys are not told apart, so the down arrow (scan 0x50) is P and F7
 * (0x41) is A, even while Animation is not offered. */
static void pictures(cok_adventure *game)
{
    int key;
    do {
        char items[41];
        if (game->pictures != 0)
            snprintf(items, sizeof items, "Pics on  %sExit",
                     game->animate ? "Animation on  " : "Animation off  ");
        else
            snprintf(items, sizeof items, "Pics off  Exit");
        bool special;
        key = cok_camp_menu(game, "", items, false, true, &special);
        if (key < 0) return;
        if (key == 'P') game->pictures = game->pictures == 0;
        if (key == 'A') game->animate = !game->animate;
    } while (key != 0 && key != 'E');
}

/* Level (4888:2272): the difficulty, 1-5 in 0x4cf4, its item selected
 * first. One key; any other keeps it. */
static void level(cok_adventure *game)
{
    uint8_t value = (uint8_t)cok_ecl_var(&game->vm, COK_VAR_DIFFICULTY);
    game->selected = value;
    bool special;
    int key = cok_camp_menu(game, "", "Novice Squire Veteran Adept Champion", true, true, &special);
    if (key < 0) return;
    if (special) {
        cok_camp_pick(game, (uint8_t)key);
    } else {
        static const char keys[] = "NSVAC";
        const char *found = key > 0 ? strchr(keys, key) : NULL;
        if (found != NULL) value = (uint8_t)(found - keys + 1);
    }
    cok_ecl_set_var(&game->vm, COK_VAR_DIFFICULTY, value);
}

/* Alter (4888:2539): "Order Drop Speed Icon Pics Level Exit" until Exit or
 * Escape. Icon is the combat icon editor (4def:408b, see icon.h). */
static void alter(cok_adventure *game)
{
    int key = ' ';
    while (key != 0 && key != 'E' && !game->vm.abort) {
        bool special;
        key = cok_camp_menu(game, "", "Order Drop Speed Icon Pics Level Exit", true, true, &special);
        if (key < 0) return;
        if (special) {
            cok_camp_pick(game, (uint8_t)key);
            continue;
        }
        switch (key) {
        case 'L': level(game); break;
        case 'O': order(game); break;
        case 'D': drop(game); break;
        case 'S': speed(game); break;
        case 'I':
            /* 4888:25d9: the combat icon editor, then the screen redrawn. */
            if (cok_icon_edit(game)) cok_adventure_redraw(game);
            break;
        case 'P': pictures(game); break;
        default: break;
        }
    }
}

/* Saving. */

/* <vault><letter>.<extension> in the save directory. */
static void vault_path(cok_adventure *game, char letter, char *path, size_t size)
{
    const cok_rules_saves *s = &cok_game_rules(game)->saves;
    snprintf(path, size, "%s/%s%c.%s", game->save_dir, s->vault, letter, s->vault_extension);
}

/* The vault goes with the saved game (dkk 46b2:2808): unless its menu left
 * it in the temporary file, which then takes the letter's name (15fe:0195,
 * 01e4), the vault of the game loaded last (DS:fb79) is read (46b2:2e1d;
 * none: no coins, or steel a hundred times the savings, 0x4c33) and written
 * as the letter's (46b2:302d), unless it is that letter's already. */
static bool save_vault(cok_adventure *game, char letter)
{
    const cok_rules_saves *s = &cok_game_rules(game)->saves;
    char path[sizeof game->save_dir + 64], error[sizeof game->error];
    vault_path(game, letter, path, sizeof path);
    bool ok = true;
    if (game->vault_temp) {
        char temp[sizeof game->save_dir + 64];
        snprintf(temp, sizeof temp, "%s/%s", game->save_dir, s->vault_temp);
        remove(path);
        FILE *exists = fopen(temp, "rb");
        if (exists != NULL) {
            fclose(exists);
            if (rename(temp, path) != 0) {
                snprintf(error, sizeof error, "%.500s: cannot be renamed", temp);
                ok = false;
            }
        }
    } else if (game->vault_letter != letter) {
        char from[sizeof game->save_dir + 64];
        vault_path(game, game->vault_letter, from, sizeof from);
        cok_vault vault;
        bool missing;
        if (!cok_vault_read(from, &vault, &missing, error, sizeof error)) {
            if (!missing) ok = false;
            uint16_t savings = cok_ecl_var(&game->vm, COK_VAR_SAVINGS);
            if (savings > 0) {
                uint32_t steel = savings * 100u; /* 46b2:2e1d: a long, 1a07:0a14 */
                for (size_t i = 0; i < 4; ++i) vault.coins[i] = (uint8_t)(steel >> (8 * i));
            }
        }
        if (ok && !cok_vault_write(path, &vault, error, sizeof error)) ok = false;
        cok_vault_free(&vault);
    }
    if (!ok) {
        snprintf(game->error, sizeof game->error, "%s", error);
        cok_adventure_log(game, "error", error);
    }
    game->vault_letter = letter; /* DS:fb79 */
    game->vault_temp = false;    /* DS:fb78 */
    return ok;
}

bool cok_camp_save_game(cok_adventure *game, char letter)
{
    static cok_saved_game saved;
    cok_ecl *vm = &game->vm;
    const cok_rules *rules = cok_game_rules(game);
    game->error[0] = '\0';
    if (game->save_dir[0] == '\0') {
        snprintf(game->error, sizeof game->error, "no directory to save games in");
        return false;
    }
    cok_ecl_set_var(vm, COK_VAR_SPEED, game->speed);
    cok_ecl_set_var(vm, COK_VAR_PICTURES, (uint16_t)(game->pictures * 2 + game->animate));
    cok_ecl_set_var(vm, COK_VAR_ECL_FILE, vm->file);
    memset(&saved, 0, sizeof saved);
    saved.file = vm->file;
    memcpy(saved.mem4b00, vm->mem4b00, sizeof saved.mem4b00);
    memcpy(saved.mem7c00, vm->mem7c00, sizeof saved.mem7c00);
    memcpy(saved.mem7a00, vm->mem7a00, sizeof saved.mem7a00);
    saved.map_x = vm->map_x;
    saved.map_y = vm->map_y;
    saved.direction = vm->direction;
    saved.ahead = vm->ahead;
    saved.square = vm->square;
    saved.last_mode = vm->last_mode;
    saved.mode = vm->mode;
    for (size_t i = 0; i < rules->saves.walls; ++i) {
        saved.wall_ids[i] = game->wall_ids[i];
        saved.wall_slots[i] = game->wall_slots[i];
    }
    /* The count is that of the whole list (4b6d:22de), monsters loaded too;
     * a ninth name would be written over the routine's return address. */
    if (game->party.count > COK_PARTY_MAX) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "saving %zu records overruns the names on the stack (4b6d:22de)",
                           game->party.count);
        return false;
    }
    saved.count = (uint8_t)game->party.count;
    const cok_rules_saves *s = &rules->saves;
    for (size_t i = 0; i < game->party.count; ++i)
        snprintf(saved.names[i], sizeof saved.names[i], "%s%c%u", s->character, letter,
                 (unsigned)(i + 1));
    char path[sizeof game->save_dir + 32];
    snprintf(path, sizeof path, "%s/%s%c.%s", game->save_dir, s->game, letter, s->game_extension);
    /* The original checks no write; it writes every file and sets DS:5885
     * (4b6d:2809) whatever happened. */
    bool ok = true;
    char error[sizeof game->error];
    if (!cok_saved_game_write(path, rules, &saved, error, sizeof error)) {
        ok = false;
        snprintf(game->error, sizeof game->error, "%s", error);
        cok_adventure_log(game, "error", error);
    }
    for (size_t i = 0; i < game->party.count; ++i) {
        if (!cok_character_write(game->party.members[i], rules, game->save_dir, saved.names[i],
                                 error, sizeof error)) {
            ok = false;
            snprintf(game->error, sizeof game->error, "%s", error);
            cok_adventure_log(game, "error", error);
        }
        /* 4b6d:0a80: the roster's copy of the character goes. */
        cok_roster_erase(game, game->party.members[i]->record);
    }
    if (s->vault != NULL && !save_vault(game, letter)) ok = false;
    game->effects.rolls.saved = 1; /* DS:5885 */
    return ok;
}

/* Save (4b6d:22de; dkk 46b2:2808): ask which game, A-J, then save it.
 * Escape cancels. Special keys whose scan codes are letters A-J pick them
 * too: F7-F10 A-D, Home G, up H and PgUp I. The original's checks for the
 * disk, its volume label and free space are left out; it ignores write
 * errors, which are logged here. */
void cok_camp_save(cok_adventure *game)
{
    const cok_rules_start *start = &cok_game_rules(game)->start;
    int key;
    do {
        bool special;
        key = cok_camp_menu(game, start->save_prompt, "A B C D E F G H I J", false,
                            game->vm.mode == 2, &special);
        if (key <= 0) return;
    } while (key < 'A' || key > 'J');
    char letter[2] = {(char)key, '\0'};
    cok_adventure_log(game, "choice", letter);
    clear_menu(game);
    draw(game, "Saving...Please Wait", 0, 24, 10, false);
    if (game->party.count == 0) {
        /* Before the count and the names (4b6d:267d): 1521:096c, row 24 in
         * yellow, then a key. */
        clear_menu(game);
        draw(game, start->save_warning, 0, 24, 14, true);
        cok_keyboard keys = cok_adventure_keyboard(game);
        if (keys.read(keys.context) < 0) return;
    }
    if (game->save_dir[0] == '\0')
        cok_adventure_log(game, "error", "no directory to save games in");
    else
        cok_camp_save_game(game, (char)key);
    clear_menu(game);
}

/* The camp menu. */

bool cok_camp(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t mode = vm->mode;
    vm->mode = 2;
    game->heal_ticks = 0;
    memset(game->rest, 0, sizeof game->rest);
    uint8_t picture = game->picture_id; /* DS:6df7, 6e01 */
    bool sprite = game->picture_sprite;
    cok_adventure_redraw(game);
    /* Loading the camp picture zero-fills the stack where View's Trade
     * later finds its flag (see sheet.h); kept from before, the byte is
     * whatever was there. Each command after leaves it its own way. */
    cok_sheet_stale stale = picture != 0x3b && game->picture_id == 0x3b ? COK_SHEET_STALE_ZERO
                                                                       : COK_SHEET_STALE_UNKNOWN;
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    draw(game, cok_game_rules(game)->text.camp, 1, 0x12, 10, true);
    forget_all(game);
    bool interrupted = false;
    bool keeps = cok_game_rules(game)->menus.camp_keeps_selection;
    uint8_t own = 1;
    int key = ' ';
    while (!interrupted && key != 0 && key != 'E' && !vm->abort) {
        bool special;
        if (keeps) game->selected = own;
        key = cok_camp_menu(game, "", "Save View Magic Rest Alter Fix Exit", true, true, &special);
        if (keeps) own = game->selected;
        if (key < 0) break;
        if (special) {
            cok_camp_pick(game, (uint8_t)key);
            stale = COK_SHEET_STALE_UNKNOWN;
            continue;
        }
        cok_sheet_stale before = stale;
        stale = COK_SHEET_STALE_UNKNOWN;
        switch (key) {
        case 'S':
            cok_ecl_add_var(vm, COK_VAR_SAVES, 1);
            if (cok_ecl_var(vm, COK_VAR_SAVES) >= 10) {
                /* Every tenth save asks a word from the rule book; the
                 * original quits to DOS on a wrong answer. */
                cok_adventure_log(game, "unported", "copy protection (4888:0376)");
                cok_adventure_redraw(game);
                cok_ecl_set_var(vm, COK_VAR_SAVES, 0);
            }
            cok_camp_save(game);
            /* The questions' colour: Champions' yellow, dkk 6a69:2048's
             * light magenta (4381's Save). */
            if (!vm->abort &&
                cok_camp_yes_no(game, "Quit TO DOS ", cok_game_rules(game)->start.ask_colour) ==
                    'Y')
                cok_camp_quit(game);
            break;
        case 'V': {
            game->selected = 1;
            bool done;
            cok_sheet(game, before, &done);
            stale = COK_SHEET_STALE_SET;
            break;
        }
        case 'M':
            game->selected = 1;
            cok_magic(game, &interrupted);
            break;
        case 'R':
            game->selected = 1;
            interrupted = cok_camp_prepare(game);
            break;
        case 'F':
            interrupted = cok_camp_fix(game);
            break;
        case 'A':
            game->selected = 1;
            alter(game);
            break;
        default: break;
        }
    }
    if (vm->abort) {
        vm->mode = mode;
        return interrupted;
    }
    /* The picture shown before, if Copy(DS:6df7, 1, 3) is 'PIC'
     * (4888:2de7-2e09): not a sprite (SPRIT), which an encounter that
     * ended before its close-up leaves in the slot, and not an empty slot
     * (6961:0537 empties DS:6dee and sets DS:6e00 to 0xff); the camp's
     * picture then stays. 6961:00e4 keeps the camp's when it is the same.
     * Then the spell target is cleared (DS:710b, 4888:2e28). */
    if (!sprite && picture != COK_ADVENTURE_NO_PICTURE) cok_adventure_load_picture(game, picture);
    game->spell_target = NULL;
    forget_all(game);
    vm->mode = mode;
    cok_adventure_status(game);
    clear_text(game);
    clear_menu(game);
    return interrupted;
}
