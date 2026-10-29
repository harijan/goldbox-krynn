#include "queen_view.h"

#include "queen_items.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The character sheet's menu of The Dark Queen of Krynn: Items
 * (dqk 86b8:0f41) and the routines it calls (see docs/games.md, "Dark
 * Queen's saved games and party"). */

enum {
    ITEM_TYPE = 0x28, ITEM_BONUS = 0x30, ITEM_READIED = 0x32, ITEM_UNKNOWN = 0x33,
    ITEM_FLAG = 0x34, ITEM_COUNT = 0x35, ITEM_WEIGHT = 0x2c, ITEM_SPECIAL = 0x36,
    SCROLL = 0x49,
};

/* The Items menu's buttons' numbers, by the order of DS:4fb0's words
 * "Rdy Use Trade Deposit Drop Halve Join Sell Id Exit" (1bdb:0005 adds
 * them). */
enum { B_READY, B_USE, B_TRADE, B_DEPOSIT, B_DROP, B_HALVE, B_JOIN, B_SELL, B_ID, B_EXIT };
static const char *const button_names[] = {"Rdy",  "Use",  "Trade", "Deposit", "Drop",
                                           "Halve", "Join", "Sell",  "Id",      "Exit"};

static bool stats(cok_adventure *game, cok_character *character)
{
    char error[300];
    if (cok_character_stats(character, cok_game_rules(game), &game->item_types, error,
                            sizeof error))
        return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
    return false;
}

/* The scrolls (6fd8:43f7: the types of the import's scroll_types). */
static bool is_scroll(const cok_rules *rules, const uint8_t *item)
{
    for (const uint8_t *t = rules->characters.import->scroll_types; *t != 0xff; ++t)
        if (item[rules->item_record.type] == *t) return true;
    return false;
}

/* 86b8:0da0: whether item may be given away or dropped: not readied ("Must
 * be unreadied"); a scroll with a spell to scribe (+0x36-+0x38's 0x80) asks
 * "<name> was going to scribe from that scroll" and "is it Okay to lose
 * it?". The cells 1-38 by 21-22 are cleared after. */
static int may_lose(cok_adventure *game, cok_character *character, const uint8_t *item)
{
    int answer = 1;
    const cok_rules *rules = cok_game_rules(game);
    if (item[ITEM_READIED] != 0) {
        queen_bar_message(game, "Must be unreadied");
        answer = 0;
    } else if (is_scroll(rules, item) &&
               ((item[ITEM_SPECIAL] | item[ITEM_SPECIAL + 1] | item[ITEM_SPECIAL + 2]) & 0x80) !=
                   0) {
        if (item[ITEM_TYPE] == SCROLL)
            return cok_adventure_not_yet(game, "an unscribed scroll bundle dropped", "dqk 86b8:0da0")
                       ? 1
                       : -1;
        queen_name_line(game, character, 1, 0x15, false);
        char name[17];
        cok_field_name(rules->character, character->record, name);
        game->vm.cursor = (cok_text_cursor){(int)strlen(name) + 2, 0x15};
        if (!queen_print_window(game, " was going to scribe from that scroll", 1, 0x15, 0x26, 0x16,
                                0x0b, false, false))
            return -1;
        answer = queen_ask(game, "is it Okay to lose it? ", false);
        if (answer < 0) return -1;
    }
    queen_clear_cells(game, 1, 0x15, 0x26, 0x16);
    return answer;
}

/* Whether the weight of item (its weight times its count) would overload
 * who (86b8:2fbf with no stacking: the sixteenth item, or past the
 * strength's allowance and 1,500 more). */
static bool overloads(cok_adventure *game, cok_character *who, const uint8_t *item, bool *failed)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    char error[300];
    *failed = !cok_character_stats(who, rules, &game->item_types, error, sizeof error);
    if (*failed) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    bool over = false;
    if (cok_field_u8(L, who->record, COK_FIELD_ITEM_COUNT) > 15) over = true;
    uint16_t weight = (uint16_t)(item[ITEM_WEIGHT] | item[ITEM_WEIGHT + 1] << 8);
    if (item[ITEM_COUNT] != 0) weight = (uint16_t)(weight * item[ITEM_COUNT]);
    int16_t allowance;
    if (!cok_character_allowance(who->record, rules, &allowance, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        *failed = true;
        return false;
    }
    uint16_t most = (uint16_t)((uint16_t)allowance + 0x5dc);
    if (most < (uint16_t)(cok_field_u16(L, who->record, COK_FIELD_WEIGHT) + weight)) over = true;
    return over;
}

/* 86b8:3ae5: which member to give to: the party's list with the current
 * one (the last asked, *to; the first where none) in light cyan, the bar
 * "prompt" over Select and Exit; Down or End the next member, Up or Home
 * the one before (wrapping), Select its member, Exit or Escape none.
 * NULL for none; *failed where the run stopped. */
static cok_character *which_member(cok_adventure *game, const char *prompt, cok_character *to,
                                   bool *failed)
{
    static const char *const items[] = {"Select", "Exit"};
    cok_party *party = &game->party;
    cok_ecl *vm = &game->vm;
    size_t at = to != NULL ? cok_party_index(party, to->record) : 0;
    if (at >= party->count) at = 0;
    char line[64];
    snprintf(line, sizeof line, "%s ", prompt);
    for (;;) {
        queen_party_list(game, party->members[at]->record);
        cok_adventure_log(game, "menu", "Select Exit");
        int choice = queen_bar_menu(game, line, items, 2, true);
        if (choice < 0) {
            *failed = true;
            (void)vm;
            return NULL;
        }
        if (choice == 0) return party->members[at];
        if (choice == 1 || choice == 0x1b) return NULL;
        if (choice == 0x84 || choice == 0x85) /* Down, End */
            at = at + 1 < party->count ? at + 1 : 0;
        else if (choice == 0x87 || choice == 0x88) /* Home, Up */
            at = at > 0 ? at - 1 : party->count - 1;
    }
}

/* 86b8:1965: ready or unready item. */
static bool ready_item(cok_adventure *game, cok_character *character, size_t index)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    uint8_t *item = character->items[index];
    const uint8_t *c = character->record;
    bool power = (item[0x38] & 0x80) != 0;
    if (item[ITEM_READIED] != 0) {
        if (item[ITEM_FLAG] != 0) {
            queen_bar_message(game, "It's Cursed!");
            return true;
        }
        item[ITEM_READIED] = 0;
        if (power) return cok_adventure_not_yet(game, "an item's power taken off", "dqk 86b8:16f7");
        return true;
    }
    const uint8_t *type = game->item_types.type[item[ITEM_TYPE]];
    unsigned code = 0, slot = type[0];
    if (cok_field_u8(L, c, COK_FIELD_HANDS) + type[1] > 2) code = 3;
    if (slot <= 8) {
        if (character->slots[slot] != 0) code = 2;
    } else if (slot == 9) {
        if (character->slots[10] != 0) code = 2;
    }
    if (item[ITEM_TYPE] == 0x27) {
        uint8_t order = cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER), thief = c[0xab];
        bool usable = (order == 2 && (item[ITEM_UNKNOWN] & 0x10) != 0) ||
                      (order == 1 && (item[ITEM_UNKNOWN] & 0x20) != 0) || thief > 9;
        if (!usable) code = thief > 0 && thief < 10 ? 6 : 4;
    }
    if (item[ITEM_TYPE] == 0x43 && cok_field_s8(L, c, COK_FIELD_RACE) != 5) code = 5;
    if (item[ITEM_TYPE] == rules->characters.arrows && character->slots[11] != 0) {
        code = 2;
        slot = 11;
    }
    if ((item[ITEM_TYPE] == rules->characters.quarrels ||
         item[ITEM_TYPE] == rules->characters.quarrels_also) &&
        character->slots[12] != 0) {
        code = 2;
        slot = 12;
    }
    if ((type[0xd] & cok_field_u8(L, c, COK_FIELD_USABLE_CLASSES)) == 0 &&
        !(item[ITEM_TYPE] == 0x43 && cok_field_s8(L, c, COK_FIELD_RACE) == 5))
        code = 1;
    char text[96], name[48];
    switch (code) {
    case 0:
        item[ITEM_READIED] = 1;
        if (power) return cok_adventure_not_yet(game, "an item's power put on", "dqk 86b8:16f7");
        break;
    case 1: queen_bar_message(game, "Wrong Class"); break;
    case 2:
        queen_item_name(&game->party, character->items[character->slots[slot] - 1], false, name,
                        sizeof name);
        snprintf(text, sizeof text, "already using %s", name);
        queen_bar_message(game, text);
        break;
    case 3:
        if (game->vm.mode == 5 && c[0x18c] != 0) break;
        queen_bar_message(game, "Your hands are full!");
        break;
    case 4: queen_bar_message(game, "Wrong Magical Order"); break;
    case 5: queen_bar_message(game, "Usable Only By Kender"); break;
    default: queen_bar_message(game, "Thief Level Too Low"); break;
    }
    return true;
}

/* 86b8:1d13: the count of item halved, the half a new item after it ("Can't
 * halve that" for one). */
static bool halve_item(cok_adventure *game, cok_character *character, size_t index)
{
    uint8_t *item = character->items[index];
    uint8_t half = (uint8_t)(item[ITEM_COUNT] / 2);
    if (half == 0) {
        queen_bar_message(game, "Can't halve that");
        return true;
    }
    if (item[ITEM_TYPE] == SCROLL)
        return cok_adventure_not_yet(game, "a bundle of scrolls halved", "dqk 86b8:1d13");
    uint8_t copy[COK_ITEM_SIZE];
    memcpy(copy, item, sizeof copy);
    copy[ITEM_COUNT] = half;
    copy[ITEM_READIED] = 0;
    item[ITEM_COUNT] = (uint8_t)(item[ITEM_COUNT] - half);
    if (!cok_character_insert_item(character, index + 1, copy)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
        return false;
    }
    return true;
}

/* 86b8:1fda: the other items of the same kind (the words, bits, type,
 * bonuses, flag, weight and a special below 2) joined to item, up to 255. */
static void join_items(cok_character *character, size_t chosen)
{
    uint8_t running = character->items[chosen][ITEM_COUNT];
    for (size_t j = 0; j < character->item_count;) {
        if (j == chosen) {
            ++j;
            continue;
        }
        const uint8_t *a = character->items[j], *b = character->items[chosen];
        bool same = a[ITEM_COUNT] > 0 && a[0x29] == b[0x29] && a[0x2a] == b[0x2a] &&
                    a[0x2b] == b[0x2b] && a[ITEM_UNKNOWN] == b[ITEM_UNKNOWN] &&
                    a[ITEM_TYPE] == b[ITEM_TYPE] && a[ITEM_BONUS] == b[ITEM_BONUS] &&
                    a[0x31] == b[0x31] && a[ITEM_FLAG] == b[ITEM_FLAG] &&
                    a[ITEM_WEIGHT] == b[ITEM_WEIGHT] && a[ITEM_WEIGHT + 1] == b[ITEM_WEIGHT + 1] &&
                    a[ITEM_SPECIAL] < 2;
        if (!same) {
            ++j;
            continue;
        }
        if (a[ITEM_COUNT] + running <= 0xff) {
            running = (uint8_t)(running + a[ITEM_COUNT]);
            cok_character_remove_item(character, j);
            if (j < chosen) --chosen;
        } else {
            character->items[chosen][ITEM_COUNT] = 0xff;
            character->items[j][ITEM_COUNT] = (uint8_t)(a[ITEM_COUNT] - (0xff - running));
            running = character->items[j][ITEM_COUNT];
            chosen = j;
            ++j;
        }
    }
    character->items[chosen][ITEM_COUNT] = running;
}

/* 86b8:1c58: item given to a member: "Trade with whom?", not "Overloaded". */
static bool trade_item(cok_adventure *game, cok_character *character, size_t index,
                       cok_character **last)
{
    queen_full_window(game); /* 177e:1ad1 in the camp */
    bool failed = false;
    cok_character *to = which_member(game, "Trade with whom?", *last, &failed);
    if (failed) return false;
    if (to == NULL) return true;
    *last = to;
    bool bad;
    if (overloads(game, to, character->items[index], &bad)) {
        queen_bar_message(game, "Overloaded");
        return true;
    }
    if (bad) return false;
    uint8_t item[COK_ITEM_SIZE];
    memcpy(item, character->items[index], sizeof item);
    if (item[ITEM_TYPE] == SCROLL)
        return cok_adventure_not_yet(game, "a bundle of scrolls traded", "dqk 86b8:1c58");
    if (!cok_character_insert_item(to, to->item_count, item)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
        return false;
    }
    cok_character_remove_item(character, index);
    return stats(game, to);
}

bool queen_view_items(cok_adventure *game, cok_character *character)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    cok_ecl *vm = &game->vm;
    static cok_character *last_target; /* DS:c3eb */
    size_t at = 0;
    bool heading = true;
    int choice = 0xff;
    do {
        uint8_t *c = character->record;
        if (character->item_count == 0) break;
        /* The menu: Rdy always; Use where the character can act and it
         * is not combat's or the party's turn; Trade where it is not an
         * NPC that can act and is not the party's; Drop (Deposit in the
         * vault); Halve below 16 items; Join; Sell and Id in a shop. */
        int list[10], n = 0;
        bool active = c[0x18b] != 0;
        bool npc = c[0x9b] >= 0x80;
        bool status_one = cok_field_s8(L, c, COK_FIELD_STATUS) == 1;
        list[n++] = B_READY;
        if (active && cok_ecl_load_var(vm, 2) == 0 &&
            (vm->mode == 2 || vm->mode == 3 || vm->mode == 4 ||
             (vm->mode == 5 && cok_character_combat(character)->may_use != 0)))
            list[n++] = B_USE;
        if ((!npc || !active || status_one) && vm->mode != 5) list[n++] = B_TRADE;
        list[n++] = vm->mode == 10 ? B_DEPOSIT : B_DROP;
        if (cok_field_u8(L, c, COK_FIELD_ITEM_COUNT) < 0x10) list[n++] = B_HALVE;
        list[n++] = B_JOIN;
        bool shop = vm->mode == 1 && cok_ecl_load_var(vm, 0x14) != 1;
        if ((!npc || !active || status_one) && shop) list[n++] = B_SELL;
        if (shop) list[n++] = B_ID;
        list[n++] = B_EXIT;
        const char *bar[10];
        char line[96] = "";
        for (int k = 0; k < n; ++k) {
            bar[k] = button_names[list[k]];
            size_t m = strlen(line);
            snprintf(line + m, sizeof line - m, "%s%s", m > 0 ? " " : "", bar[k]);
        }
        size_t rows = character->item_count;
        char **names = calloc(rows, sizeof *names);
        const char **shown = calloc(rows, sizeof *shown);
        if (names == NULL || shown == NULL) {
            free(names);
            free(shown);
            cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
            return false;
        }
        for (size_t i = 0; i < rows; ++i) {
            names[i] = malloc(96);
            if (names[i] == NULL) {
                cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
                return false;
            }
            queen_item_name(&game->party, character->items[i], true, names[i], 96);
            shown[i] = names[i];
        }
        if (heading) {
            queen_title_window(game);
            queen_name_line(game, character, 1, 1, true);
            char name[17];
            cok_field_name(L, c, name);
            queen_say(game, (int)strlen(name) + 4, 1, "Items", 7);
            queen_say(game, 1, 3, "Ready Item", 0x0b);
            heading = false;
        }
        for (size_t i = 0; i < rows; ++i) cok_adventure_log(game, "item", names[i]);
        cok_adventure_log(game, "menu", line);
        int picked = queen_list_menu(game, "", bar, (size_t)n, shown, rows, &at, 5, 0x16, 0x26);
        for (size_t i = 0; i < rows; ++i) free(names[i]);
        free(names);
        free(shown);
        if (picked < 0) return false;
        choice = picked == 0x1b ? B_EXIT : list[picked];
        size_t index = at < character->item_count ? at : 0;
        bool ok = true;
        switch (choice) {
        case B_READY: ok = ready_item(game, character, index); break;
        case B_USE:
            if (character->items[index][ITEM_READIED] == 0)
                queen_bar_message(game, "Must be readied");
            else
                ok = cok_adventure_not_yet(game, "an item used", "dqk 86b8:2175");
            break;
        case B_TRADE:
            if (may_lose(game, character, character->items[index]) > 0)
                ok = trade_item(game, character, index, &last_target);
            else if (game->vm.status != COK_ECL_OK)
                ok = false;
            heading = true;
            break;
        case B_DEPOSIT: ok = cok_adventure_not_yet(game, "an item left in the vault", "dqk 86b8:1315"); break;
        case B_DROP: {
            int may = may_lose(game, character, character->items[index]);
            if (may < 0) {
                ok = false;
                break;
            }
            if (may == 0) break;
            char name[48], text[160];
            queen_item_name(&game->party, character->items[index], false, name, sizeof name);
            snprintf(text, sizeof text, "Your %swill be gone forever", name);
            if (!queen_print_window(game, text, 1, 0x15, 0x26, 0x16, 0x0b, false, true)) {
                ok = false;
                break;
            }
            int yes = queen_ask(game, "Lose it? ", false);
            if (yes < 0) {
                ok = false;
                break;
            }
            if (yes) cok_character_remove_item(character, index);
            queen_clear_cells(game, 1, 0x15, 0x26, 0x16);
            break;
        }
        case B_HALVE: ok = halve_item(game, character, index); break;
        case B_JOIN: join_items(character, index); break;
        case B_SELL: ok = cok_adventure_not_yet(game, "an item sold", "dqk 771f:0be4"); break;
        case B_ID: ok = cok_adventure_not_yet(game, "an item identified", "dqk 771f:0e61"); break;
        default: break;
        }
        if (!ok || !stats(game, character)) return false;
    } while (choice != B_EXIT);
    return true;
}
