#include "camp.h"

#include "magic.h"
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

static void name_of(const uint8_t *c, char out[16])
{
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(out, c + 1, length);
    out[length] = '\0';
}

/* Draw c's name at x, y: light red if it cannot act, else light cyan
 * (6346:199d outside combat). */
static void draw_name(cok_adventure *game, const uint8_t *c, int x, int y)
{
    char name[16];
    name_of(c, name);
    draw(game, name, x, y, c[0x189] == 0 ? 12 : 11, true);
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
 * drawn first, as the camp's menus do; they also animate it, which is not
 * ported. */
int cok_camp_menu(cok_adventure *game, const char *prompt, const char *items, bool keypad,
                bool show, bool *special)
{
    if (show) cok_adventure_show_picture(game);
    cok_adventure_log(game, "menu", items);
    cok_keyboard keys = cok_adventure_keyboard(game);
    return cok_menu_ask(&game->screen, &game->font, prompt, items, 13, 15, 10, keypad,
                        &game->selected, &keys, special);
}

/* Ask prompt, in prompt_color, until Yes or No (67b5:177f); No is
 * selected first, once, so an item chosen with the arrows stays chosen
 * through Escape and other keys. Special keys are not told apart, so scan
 * codes 0x4e and 0x59 answer too. Returns 'Y', 'N', or -1 if input
 * ended. */
int cok_camp_yes_no(cok_adventure *game, const char *prompt, uint8_t prompt_color)
{
    cok_adventure_log(game, "menu", prompt);
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key;
    game->selected = 2;
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
    game->vm.character = cok_party_special(&game->party, game->vm.character, scan);
    cok_adventure_party(game);
}

/* Quit to DOS (1614:0000): the run ends. */
static void quit(cok_adventure *game)
{
    cok_adventure_log(game, "quit", "to DOS");
    game->quit = true;
    game->vm.abort = true;
}

/* Preparing spells. */

uint16_t cok_camp_preparation(cok_character *character)
{
    uint8_t *c = character->record;
    uint8_t memorize_max = 0, memorize_sum = 0, scribe_max = 0, scribe_sum = 0;
    for (size_t i = 0; i < 0x3a; ++i) {
        if (c[0x1e + i] <= 0x7f) continue;
        uint8_t level = cok_spell_level(c[0x1e + i] & 0x7f);
        if (level == 0) level = 1;
        if (level > memorize_max) memorize_max = level;
        memorize_sum = (uint8_t)(memorize_sum + level);
    }
    for (size_t i = 0; i < character->item_count; ++i) {
        const uint8_t *item = character->items[i];
        if (!cok_item_is_scroll(item)) continue;
        for (size_t k = 1; k <= 3; ++k) {
            if (item[0x3b + k] <= 0x7f) continue;
            uint8_t level = cok_spell_level(item[0x3b + k] & 0x7f);
            if (level > scribe_max) scribe_max = level;
            scribe_sum = (uint8_t)(scribe_sum + level);
        }
    }
    uint8_t hours = 0;
    if (memorize_sum > 0 || scribe_sum > 0) hours = 4;
    if (memorize_max > 2 || scribe_max > 2) hours = 6;
    c[0x58] = hours;
    return (uint16_t)(scribe_sum * 15 + memorize_sum * 15 + hours * 60);
}

void cok_camp_forget(cok_character *character)
{
    uint8_t *c = character->record;
    for (size_t i = 0; i < 0x3a; ++i)
        if (c[0x1e + i] > 0x7f) c[0x1e + i] = 0;
    c[0x58] = 0;
    for (size_t i = 0; i < character->item_count; ++i) {
        uint8_t *item = character->items[i];
        if (!cok_item_is_scroll(item)) continue;
        for (size_t k = 0x3c; k <= 0x3e; ++k) item[k] &= 0x7f;
    }
}

static void forget_all(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count; ++i) cok_camp_forget(game->party.members[i]);
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
    const char *name = cok_spell_name(spell);
    if (name == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "spell %u has no name; 5b04:57eb would print the data at DS:%04x",
                           spell, 0x2077 + 41 * spell);
        return false;
    }
    clear_cells(game, 1, 0x12, 0x26, 0x16);
    draw_name(game, c, 1, 0x13);
    draw(game, what, c[0] + 2, 0x13, 10, true);
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
    uint8_t *c = character->record, level = 0;
    for (size_t i = 0; i <= 0x39 && level == 0; ++i) {
        if (c[0x1e + i] <= 0x7f) continue;
        if (*flag) {
            level = cok_spell_level(c[0x1e + i] & 0x7f);
            continue;
        }
        c[0x1e + i] = (uint8_t)(c[0x1e + i] - 0x80);
        show_rest(game, 0);
        if (!cok_camp_spell_message(game, c, "has memorized", c[0x1e + i])) return 0;
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
    uint8_t *c = character->record, level = 0;
    for (size_t i = 0; i < character->item_count && level == 0; ++i) {
        uint8_t *item = character->items[i], freed[COK_ITEM_SIZE];
        if (!cok_item_is_scroll(item)) continue;
        bool gone = false;
        for (size_t k = 1; k <= 3 && level == 0; ++k) {
            uint8_t b = item[0x3b + k];
            if (b <= 0x80) continue;
            if (*flag) {
                level = cok_spell_level(b & 0x7f);
                continue;
            }
            uint8_t spell = b & 0x7f;
            c[0x62 + spell] = 1;
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
 * at 15 minutes a level (57e4:0c9c). */
static void learn_tick(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count && !game->vm.abort; ++i) {
        cok_character *character = game->party.members[i];
        if (game->learn_ticks[i] > 0) --game->learn_ticks[i];
        if (game->learn_ticks[i] != 0 || character->record[0x58] != 0) continue;
        bool flag = false;
        uint8_t level = scribe(game, character, &flag);
        if (level == 0 && !game->vm.abort) level = memorize(game, character, &flag);
        game->learn_ticks[i] = (uint8_t)(level * 3);
    }
}

/* Each hour (*ticks reaching 12), count down each member's hours of
 * preparation, and when they end, time its first spell (57e4:0d52). */
static void hourly(cok_adventure *game, uint8_t *ticks)
{
    if (++*ticks < 12) return;
    *ticks = 0;
    for (size_t i = 0; i < game->party.count && !game->vm.abort; ++i) {
        cok_character *character = game->party.members[i];
        uint8_t *c = character->record;
        if ((int8_t)c[0x58] <= 0) continue;
        if (--c[0x58] != 0) continue;
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
        if (cok_character_heal(game->party.members[i]->record, 1, false, game->vm.mode))
            healed = true;
    if (interactive) show_rest(game, 0);
    draw(game, "The Whole Party Is Healed", 1, 0x13, 10, true);
    if (healed) cok_adventure_party(game);
    cok_adventure_wait(game, game->speed * 100u);
    clear_text(game);
    game->heal_ticks = 0;
}

bool cok_camp_rest(cok_adventure *game, bool interactive)
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
        learn_tick(game);
        if (!vm->abort) hourly(game, &hours);
        if (vm->abort) break;
        uint16_t every = vm->mem7c00[0x2d2], chance = vm->mem7c00[0x2d3];
        if (every == 0 || ++game->rest_ticks < every) continue;
        game->rest_ticks = 0;
        if (cok_dice(&vm->seed, 1, 100) > chance) continue; /* 57e4:0fbe */
        clear_text(game);
        show_rest(game, 0);
        draw(game, "Your repose is suddenly interrupted!", 1, 0x13, 15, true);
        stop = interrupted = true;
        cok_adventure_wait(game, game->speed * 100u);
    }
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    game->resting = false;
    return interrupted;
}

/* Rest (4888:0f05): rest as long as the member who needs longest needs to
 * learn and scribe its marked spells, or as the player chooses. */
bool cok_camp_prepare(cok_adventure *game)
{
    uint16_t longest = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint16_t minutes = cok_camp_preparation(game->party.members[i]);
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
    uint16_t sum = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        sum = (uint16_t)(sum + c[0x62] - c[0x197]);
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

bool cok_camp_fix(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (missing(game) == 0) return false;
    /* 4888:272e: the cure spells memorized by members who are okay; they
     * are not used up. */
    uint16_t pool = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        if (c[0x188] != 0) continue;
        for (size_t s = 0; s <= 0x39; ++s) {
            if (c[0x1e + s] == 3) pool = (uint16_t)(pool + cure(game, 0));
            else if (c[0x1e + s] == 0x3a) pool = (uint16_t)(pool + cure(game, 1));
            else if (c[0x1e + s] == 0x47) pool = (uint16_t)(pool + cure(game, 2));
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
        if (c[0x188] == 0) {
            counts[0] = (uint16_t)(counts[0] + c[0x11c]);
            a = (uint16_t)(c[0x11c] * 15);
            counts[1] = (uint16_t)(counts[1] + c[0x11f]);
            b = (uint16_t)(c[0x11f] * 60);
            counts[2] = (uint16_t)(counts[2] + c[0x120]);
            d = (uint16_t)(c[0x120] * 75);
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
        if (c[0x62] <= c[0x197]) continue;
        uint16_t amount = (uint16_t)(c[0x62] - c[0x197]);
        if (amount > pool) amount = pool;
        if (amount == 0) continue;
        if (cok_character_heal(c, (uint8_t)amount, false, vm->mode) && amount <= pool)
            pool = (uint16_t)(pool - amount);
    }
    cok_adventure_party(game);
    cok_adventure_status(game);
    memcpy(game->rest, saved, sizeof saved);
    return false;
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
 * counting it out of 0x7f3e, and select the one before it, or the first. */
static void remove_selected(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t i = cok_party_index(&game->party, vm->character);
    if (i == game->party.count) return;
    /* The original keeps pointers to the record it frees in the spell
     * target and the trade partner (DS:710b, 46b0); the port forgets them. */
    uint8_t *gone = game->party.members[i]->record;
    if (game->spell_target == gone) game->spell_target = NULL;
    if (game->trade_partner == gone) game->trade_partner = NULL;
    cok_party_remove(&game->party, i);
    --vm->mem7c00[0x33e];
    vm->character = cok_party_record(&game->party, i > 0 ? i - 1 : 0);
}

/* Drop (4888:2126): drop the selected character after asking, or with the
 * party's last, quit. */
static void drop(cok_adventure *game)
{
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
        quit(game);
        return;
    }
    cok_camp_say(game, c, "will be gone", false);
    int answer = cok_camp_yes_no(game, "Drop from party? ", 14);
    if (answer < 0) return;
    if (answer != 'Y') {
        cok_camp_say(game, c, "Breathes A sigh of relief", true);
        return;
    }
    cok_camp_say(game, c, c[0x189] != 0 ? "bids you farewell" : "is dumped in a ditch", true);
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
    uint16_t *difficulty = &game->vm.mem4b00[0x1f4];
    uint8_t value = (uint8_t)*difficulty;
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
    *difficulty = value;
}

/* Alter (4888:2539): "Order Drop Speed Icon Pics Level Exit" until Exit or
 * Escape. Icon, the combat icon editor (4def:408b), is not ported. */
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
        case 'I': cok_adventure_log(game, "unported", "Icon (4def:408b)"); break;
        case 'P': pictures(game); break;
        default: break;
        }
    }
}

/* Saving. */

bool cok_camp_save_game(cok_adventure *game, char letter)
{
    static cok_saved_game saved;
    cok_ecl *vm = &game->vm;
    game->error[0] = '\0';
    if (game->save_dir[0] == '\0') {
        snprintf(game->error, sizeof game->error, "no directory to save games in");
        return false;
    }
    vm->mem4b00[0xfc] = game->speed;
    vm->mem4b00[0xff] = (uint16_t)(game->pictures * 2 + game->animate);
    vm->mem7c00[0x312] = vm->file;
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
    memcpy(saved.wall_ids, game->wall_ids, sizeof saved.wall_ids);
    memcpy(saved.wall_slots, game->wall_slots, sizeof saved.wall_slots);
    /* The count is that of the whole list (4b6d:22de), monsters loaded too;
     * a ninth name would be written over the routine's return address. */
    if (game->party.count > COK_PARTY_MAX) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "saving %zu records overruns the names on the stack (4b6d:22de)",
                           game->party.count);
        return false;
    }
    saved.count = (uint8_t)game->party.count;
    for (size_t i = 0; i < game->party.count; ++i)
        snprintf(saved.names[i], sizeof saved.names[i], "CHRDAT%c%u", letter, (unsigned)(i + 1));
    char path[sizeof game->save_dir + 32];
    snprintf(path, sizeof path, "%s/SAVGAM%c.DAT", game->save_dir, letter);
    /* The original checks no write; it writes every file and sets DS:5885
     * (4b6d:2809) whatever happened. */
    bool ok = true;
    char error[sizeof game->error];
    if (!cok_saved_game_write(path, &saved, error, sizeof error)) {
        ok = false;
        snprintf(game->error, sizeof game->error, "%s", error);
        cok_adventure_log(game, "error", error);
    }
    for (size_t i = 0; i < game->party.count; ++i) {
        if (cok_character_write(game->party.members[i], game->save_dir, saved.names[i], error,
                                sizeof error))
            continue;
        ok = false;
        snprintf(game->error, sizeof game->error, "%s", error);
        cok_adventure_log(game, "error", error);
    }
    game->effects.rolls.saved = 1; /* DS:5885 */
    return ok;
}

/* Save (4b6d:22de): ask which game, A-J, then save it. Escape cancels.
 * Special keys whose scan codes are letters A-J pick them too: F7-F10 A-D,
 * Home G, up H and PgUp I. The original's checks for the disk, its volume
 * label and free space are left out; it ignores write errors, which are
 * logged here. */
static void save(cok_adventure *game)
{
    int key;
    do {
        bool special;
        key = cok_camp_menu(game, "Save Which Game: ", "A B C D E F G H I J", false,
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
        draw(game, "WARNING: Problem Saving Characters", 0, 24, 14, true);
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
    cok_adventure_redraw(game);
    /* Loading the camp picture zero-fills the stack where View's Trade
     * later finds its flag (see sheet.h); kept from before, the byte is
     * whatever was there. Each command after leaves it its own way. */
    cok_sheet_stale stale = picture != 0x3b && game->picture_id == 0x3b ? COK_SHEET_STALE_ZERO
                                                                       : COK_SHEET_STALE_UNKNOWN;
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    draw(game, "The party makes camp...", 1, 0x12, 10, true);
    forget_all(game);
    bool interrupted = false;
    int key = ' ';
    while (!interrupted && key != 0 && key != 'E' && !vm->abort) {
        bool special;
        key = cok_camp_menu(game, "", "Save View Magic Rest Alter Fix Exit", true, true, &special);
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
            if (++vm->mem4b00[0x13c] >= 10) {
                /* Every tenth save asks a word from the rule book; the
                 * original quits to DOS on a wrong answer. */
                cok_adventure_log(game, "unported", "copy protection (4888:0376)");
                cok_adventure_redraw(game);
                vm->mem4b00[0x13c] = 0;
            }
            save(game);
            if (!vm->abort && cok_camp_yes_no(game, "Quit TO DOS ", 14) == 'Y') quit(game);
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
        case 'F': interrupted = cok_camp_fix(game); break;
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
    /* The picture shown before, if Copy(DS:6df7, 1, 3) is 'PIC' (4888:2df0):
     * the port loads no other small pictures, and freeing one (6961:0537)
     * empties DS:6dee and sets DS:6e00 to 0xff, so that holds exactly when
     * one was loaded. 6961:00e4 keeps the camp's when it is the same. Then
     * the spell target is cleared (DS:710b, 4888:2e28). */
    if (picture != COK_ADVENTURE_NO_PICTURE) cok_adventure_load_picture(game, picture);
    game->spell_target = NULL;
    forget_all(game);
    vm->mode = mode;
    cok_adventure_status(game);
    clear_text(game);
    clear_menu(game);
    return interrupted;
}
