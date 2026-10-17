#include "items.h"

#include "arena.h"
#include "camp.h"
#include "cast.h"
#include "magic.h"
#include "round.h"
#include "screen.h"
#include "shop.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* Append text to a string[40], cut to 40 characters (1a46:0ade). */
static void join(char s[41], const char *text)
{
    size_t length = strlen(s);
    while (*text != '\0' && length < 40) s[length++] = *text++;
    s[length] = '\0';
}

bool cok_item_name(cok_adventure *game, uint8_t *item, bool readied_column, char out[41])
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    char s[41] = "";
    if (readied_column) join(s, item[0x34] != 0 ? " Yes  " : " No   ");
    bool detect = false;
    for (size_t i = 0; i < game->party.count && !detect; ++i)
        detect = cok_character_find_effect(game->party.members[i], 5) != NULL;
    if (detect && ((int8_t)item[0x32] > 0 || (int8_t)item[0x33] > 0 || item[0x36] != 0)) {
        join(s, "*");
        join(s, " ");
    }
    if (item[0x39] > 0) {
        char count[8];
        snprintf(count, sizeof count, "%u", item[0x39]);
        join(s, count);
        join(s, " ");
    }
    /* The parts shown, as bits 0-2 of mask for +0x2f-+0x31. */
    uint8_t mask = 0;
    for (unsigned i = 1; i <= 3; ++i)
        if (item[0x2e + i] != 0 && ((item[0x35] >> (3 - i)) & 1) == 0) mask = (uint8_t)(mask + (1u << (i - 1)));
    bool plural = false;
    for (unsigned i = 3; i >= 1; --i) {
        if (((mask >> (i - 1)) & 1) == 0) continue;
        char part[256];
        uint16_t at = (uint16_t)cok_data_offset(D, COK_DATA_ITEM_WORDS, item[0x2e + i]);
        if (!cok_data_string(D, at, part)) {
            char text[96];
            snprintf(text, sizeof text, "item name part %u reads past the name table (DS:%04x)",
                     item[0x2e + i], at);
            return undefined(game, text);
        }
        join(s, part);
        uint8_t type = item[0x2e];
        if (item[0x39] < 2 || plural) {
            join(s, " ");
        } else if ((1u << (i - 1)) == mask || (i == 1 && mask > 4 && type != 0x37) ||
                   (i == 2 && (mask & 1) == 0) || (i == 3 && type == 0x37) ||
                   ((type == 0x1e || type == 5 || type == 0x0c) && item[0x31] != 0x5d)) {
            join(s, "s ");
            plural = true;
        } else {
            join(s, " ");
        }
    }
    /* A Pascal string assignment: the bytes after it stay as they were. */
    size_t length = strlen(s);
    item[0] = (uint8_t)length;
    memcpy(item + 1, s, length);
    memcpy(out, s, length + 1);
    return true;
}

/* Build and draw item's name at x, y in light green (6346:0488 with draw). */
static bool draw_item(cok_adventure *game, uint8_t *item, int x, int y)
{
    char name[41];
    if (!cok_item_name(game, item, false, name)) return false;
    cok_adventure_log(game, "print", name);
    cok_text_string(&game->screen, &game->font, name, x, y, 10, 0);
    return true;
}

static cok_character *selected(cok_adventure *game)
{
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

static cok_character *member_of(cok_adventure *game, const uint8_t *record)
{
    size_t i = cok_party_index(&game->party, record);
    return i < game->party.count ? game->party.members[i] : NULL;
}

static bool effect_failed(cok_adventure *game)
{
    return cok_adventure_effect_failed(game);
}

/* 6346:0d20 on c, ending the run where it fails. */
static bool recompute(cok_adventure *game, cok_character *c)
{
    char error[300];
    if (cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error))
        return true;
    return undefined(game, error);
}

void cok_item_draw_name(cok_adventure *game, const uint8_t *c, int x, int y, bool possessive)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    char name[20];
    uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(name, c + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
    snprintf(name + length, sizeof name - length, "%s", possessive ? "'s" : "");
    uint8_t color = cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0                         ? 12
                    : game->vm.mode == 5 && cok_field_u8(L, c, COK_FIELD_HOSTILE) == 1 ? 14
                                                                                       : 11;
    cok_text_string(&game->screen, &game->font, name, x, y, color, 0);
}

static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    cok_picture_fill(&game->screen, x1, y1 * 8, (size_t)(x2 - x1 + 1), (size_t)(y2 - y1 + 1) * 8,
                     0); /* 1128:07e6 */
}

/* Print text in rows 21-22 in yellow, from the cursor, clearing them first
 * if clear (1521:04ac). */
static void print_low(cok_adventure *game, const char *text, bool clear)
{
    cok_adventure_print(game, text, (cok_text_window){1, 0x15, 0x26, 0x16}, 14, clear);
}

/* Item lists. */

static void delete_item(cok_character *c, size_t index)
{
    cok_character_remove_item(c, index);
}

static bool insert_item(cok_adventure *game, cok_character *c, size_t index, const uint8_t *item)
{
    return cok_character_insert_item(c, index, item) || undefined(game, "out of memory");
}

/* The power of an item readied or unreadied (546c:1d96): powers 0 and 2
 * add or remove the effect +0x3d (3f44:3888: permanent, value 0xff, its
 * handler run on removal), 3 and 5 recompute strength, 9 unreadied ends
 * Spiritual Hammer's 0x17. */
static bool power(cok_adventure *game, cok_character *c, const uint8_t *item, bool readying)
{
    uint8_t p = item[0x3e] & 0x7f;
    if (p == 0 || p == 2) {
        if (readying) {
            if (cok_character_add_effect(c, item[0x3d], 0, 0xff, true) == NULL)
                return undefined(game, "out of memory");
            return true;
        }
        return cok_effects_remove(&game->effects, c, NULL, item[0x3d]) || effect_failed(game);
    }
    if (p == 3 || p == 5) return cok_effects_ability(&game->effects, c, 0) || effect_failed(game);
    if (p == 9 && !readying)
        return cok_effects_remove(&game->effects, c, NULL, 0x17) || effect_failed(game);
    return true;
}

bool cok_item_unready(cok_adventure *game, uint8_t *item)
{
    if (item[0x36] != 0) {
        cok_camp_notice(game, "It's Cursed");
        return true;
    }
    item[0x34] = 0;
    if (item[0x3e] <= 0x7f) return true;
    cok_character *c = selected(game);
    if (c == NULL)
        return undefined(game, "an item's power with no character selected reads through NULL "
                               "(546c:1d96)");
    return power(game, c, item, false);
}

/* Ready or unready item index (546c:1ea7). */
bool cok_item_ready(cok_adventure *game, cok_character *character, size_t index)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record, *item = character->items[index];
    bool has_power = item[0x3e] > 0x7f;
    if (item[0x34] != 0) {
        if (item[0x36] != 0) {
            cok_camp_notice(game, "It's Cursed");
            return true;
        }
        item[0x34] = 0;
        return !has_power || power(game, character, item, false);
    }
    if (item[0x2e] >= COK_ITEM_TYPES)
        return undefined(game, "an item type past 128 reads data the game sets as it runs");
    const uint8_t *type = game->item_types.type[item[0x2e]];
    int error = 0;
    unsigned slot = type[0];
    if (cok_field_u8(L, c, COK_FIELD_HANDS) + type[1] > 2) error = 3;
    if (slot <= 8 && character->slots[slot] != 0) error = 2;
    if (slot == 9 && character->slots[10] != 0) error = 2;
    uint8_t order = cok_field_u8(L, c, COK_FIELD_MAGIC_ORDER);
    if (item[0x2e] == 0x27 &&
        !(order == 1 && (item[0x35] & 0x20) != 0) && !(order == 2 && (item[0x35] & 0x10) != 0))
        error = 4;
    if (item[0x2e] == 0x43 && cok_field_u8(L, c, COK_FIELD_RACE) != 5) error = 5;
    if (item[0x2e] == 0x1e && character->slots[11] != 0) {
        error = 2;
        slot = 11;
    }
    if (item[0x2e] == 0x0c && character->slots[12] != 0) {
        error = 2;
        slot = 12;
    }
    if ((type[13] & cok_field_u8(L, c, COK_FIELD_USABLE_CLASSES)) == 0) error = 1;
    switch (error) {
    case 0:
        item[0x34] = 1;
        return !has_power || power(game, character, item, true);
    case 1: cok_camp_notice(game, "Wrong Class"); return true;
    case 2: {
        char name[41], text[64];
        if (character->slots[slot] == 0)
            return undefined(game, "Ready names the item in an empty slot (546c:20c3)");
        if (!cok_item_name(game, character->items[character->slots[slot] - 1], false, name))
            return false;
        snprintf(text, sizeof text, "already using %s", name);
        cok_camp_notice(game, text);
        return true;
    }
    case 3:
        if (game->vm.mode != 5 || cok_field_u8(L, c, COK_FIELD_COMPUTER) == 0)
            cok_camp_notice(game, "Your hands are full!");
        return true;
    case 4: cok_camp_notice(game, "Wrong Magical Order"); return true;
    default: cok_camp_notice(game, "Usable Only By Kender"); return true;
    }
}

/* Whether c may part with item (546c:10cb): not while it is readied, and
 * for a scroll with a spell marked to scribe, only if the player says so.
 * Clears rows 21-22. Returns -1 when input ended. */
static int may_part(cok_adventure *game, const uint8_t *c, const uint8_t *item)
{
    int ok = 0;
    if (item[0x34] != 0) {
        cok_camp_notice(game, "Must be unreadied");
    } else if (!cok_item_is_scroll(item) ||
               (item[0x3c] <= 0x7f && item[0x3d] <= 0x7f && item[0x3e] <= 0x7f)) {
        ok = 1;
    } else {
        cok_item_draw_name(game, c, 1, 0x15, false);
        game->vm.cursor = (cok_text_cursor){
            cok_field_u8_at(cok_game_rules(game)->character, c, COK_FIELD_NAME, 0) + 2, 0x15};
        print_low(game, " was going to scribe from that scroll", false);
        int answer = cok_camp_yes_no(game, "is it Okay to lose it? ", 13);
        if (answer < 0) return -1;
        ok = answer == 'Y';
    }
    clear_cells(game, 1, 0x15, 0x26, 0x16);
    return ok;
}

int cok_item_overloaded(cok_adventure *game, cok_character *who, const uint8_t *item)
{
    if (!recompute(game, who)) return -1;
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *r = who->record;
    bool over = cok_field_u8(L, r, COK_FIELD_ITEM_COUNT) > 15;
    uint16_t weight = (uint16_t)(item[0x37] | item[0x38] << 8);
    if (item[0x39] > 0) weight = (uint16_t)(weight * item[0x39]);
    int16_t allowance;
    char error[300];
    if (!cok_character_allowance(r, cok_game_rules(game), &allowance, error, sizeof error)) {
        undefined(game, error);
        return -1;
    }
    uint16_t carried = (uint16_t)(cok_field_u16(L, r, COK_FIELD_WEIGHT) + weight);
    if ((int32_t)carried > (int32_t)(int16_t)(allowance + 1500)) over = true;
    return over;
}

/* Trade item index to another member (546c:2178): "Trade with Whom?",
 * from the last one traded with (DS:46b0). */
static bool trade(cok_adventure *game, cok_character *giver, size_t index)
{
    uint8_t *who = game->trade_partner;
    cok_adventure_redraw(game);
    bool ended;
    who = cok_adventure_pick(game, "Trade with Whom?", who, true, &ended);
    if (ended) return false;
    cok_character *receiver = member_of(game, who);
    if (receiver == NULL) return true;
    game->trade_partner = who;
    uint8_t item[COK_ITEM_SIZE];
    memcpy(item, giver->items[index], sizeof item);
    int over = cok_item_overloaded(game, receiver, item);
    if (over < 0) return false;
    if (over) {
        cok_camp_notice(game, "Overloaded");
        return true;
    }
    if (!insert_item(game, receiver, receiver->item_count, item)) return false;
    /* Trading with oneself, the copy goes to the end and the item goes. */
    delete_item(giver, index);
    return recompute(game, receiver);
}

/* Halve (546c:2234): a copy with half the count, unreadied, after it. */
static bool halve(cok_adventure *game, cok_character *c, size_t index)
{
    uint8_t *item = c->items[index], half = (uint8_t)(item[0x39] >> 1);
    if (half == 0) {
        cok_camp_notice(game, "Can't halve that");
        return true;
    }
    uint8_t copy[COK_ITEM_SIZE];
    memcpy(copy, item, sizeof copy);
    copy[0x39] = half;
    copy[0x34] = 0;
    item[0x39] = (uint8_t)(item[0x39] - half);
    return insert_item(game, c, index + 1, copy);
}

/* Join (546c:22e4): every other item with a count that matches the one
 * picked in type, name parts, bonuses, curse and weight, and whose +0x3c is
 * below 2, is added to it while the total stays within 255, and freed;
 * past that the one picked gets 255 and the other takes the rest and the
 * part of the one picked. The original compares +0x3c-+0x3e of the other
 * with itself, so they never differ. */
static void join_items(cok_character *c, size_t picked)
{
    size_t into = picked;
    uint8_t sum = c->items[into][0x39];
    for (size_t i = 0; i < c->item_count;) {
        uint8_t *other = c->items[i], *target = c->items[into];
        bool match = i != into && other[0x39] > 0 && other[0x2f] == target[0x2f] &&
                     other[0x30] == target[0x30] && other[0x31] == target[0x31] &&
                     other[0x2e] == target[0x2e] && other[0x32] == target[0x32] &&
                     other[0x33] == target[0x33] && other[0x36] == target[0x36] &&
                     other[0x37] == target[0x37] && other[0x38] == target[0x38] &&
                     other[0x3c] < 2;
        if (!match) {
            ++i;
        } else if (sum + other[0x39] <= 255) {
            sum = (uint8_t)(sum + other[0x39]);
            delete_item(c, i);
            if (into > i) --into;
        } else {
            target[0x39] = 0xff;
            other[0x39] = (uint8_t)(other[0x39] - (255 - sum));
            sum = other[0x39];
            into = i;
            ++i;
        }
    }
    c->items[into][0x39] = sum;
}

/* Drop (in 546c:17f9): "Your NAME will be gone forever", then "Drop It? ".
 * Returns -1 when input ended, 1 when dropped. */
static int drop(cok_adventure *game, cok_character *c, size_t index)
{
    char name[41], text[96];
    if (!cok_item_name(game, c->items[index], false, name)) return -1;
    snprintf(text, sizeof text, "Your %swill be gone forever", name);
    print_low(game, text, true);
    int answer = cok_camp_yes_no(game, "Drop It? ", 13);
    if (answer < 0) return -1;
    if (answer == 'Y') delete_item(c, index);
    clear_cells(game, 1, 0x15, 0x26, 0x16);
    return answer == 'Y';
}

/* The first class of a human with a level, in levels (66c2:0df3 for the
 * former levels +0x101, 66c2:0e4f for +0xf9), or 0x11 if none. */
static uint8_t first_class(const cok_character_layout *L, const uint8_t *c, cok_field levels)
{
    if (cok_field_u8(L, c, COK_FIELD_RACE) != 6) return 0x11;
    uint8_t i = 0;
    while (i < 7 && cok_field_u8_at(L, c, levels, i) == 0) ++i;
    return cok_field_s8_at(L, c, levels, i) > 0 ? i : 0x11;
}

/* Use (546c:24d7): a scroll's spell picked from it, or an item's spell
 * (+0x3d), cast as an item (DS:711d), which uses up the scroll's spell, or
 * one of a stack (+0x39) or a charge (+0x3c), the item going with the last;
 * an item with no charges lasts. A scroll needs a cleric or magic-user,
 * now or before, or a thief above level 9 who rolls up to 75, else "oops!".
 * An item whose spell is 0 leaves DS:711d set. */
bool cok_item_use(cok_adventure *game, cok_character *character, size_t index, bool *done)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t *c = character->record, *item = character->items[index];
    cok_rolls *r = &game->effects.rolls;
    r->item = 0;
    int spell = 0;
    bool scroll = cok_item_is_scroll(item);
    if (scroll) {
        spell = cok_magic_scroll(game, character, index);
        if (spell < 0) return false;
    } else if (item[0x3d] > 0 && item[0x3e] < 0x80) {
        r->item = 1;
        spell = item[0x3d] & 0x7f;
    }
    if (spell == 0) {
        *done = false;
        return true;
    }
    bool combat = game->vm.mode == 5;
    if (combat && cok_field_u8(L, c, COK_FIELD_COMPUTER) == 0 && !cok_arena_redraw(game))
        return false; /* 6346:300f */
    if (r->item != 0) {
        if (!cok_arena_say(game, character, "uses an item", 10, false)) return false;
        if (combat) {
            cok_adventure_log(game, "print", "Item:");
            cok_text_string(&game->screen, &game->font, "Item:", 0, 0x17, 10, 0); /* 1521:0353 */
            if (!draw_item(game, item, 5, 0x17)) return false;
        } else if (!draw_item(game, item, 1, 0x16)) {
            return false;
        }
        cok_adventure_wait(game, game->speed * 100u); /* 1521:0b4b */
        cok_arena_clear_text(game);
    }
    r->item = 1;
    bool cast = true;
    if (scroll) {
        uint8_t before = first_class(L, c, COK_FIELD_FORMER_LEVEL),
                now = first_class(L, c, COK_FIELD_CLASS_LEVEL);
        cast = before == 5 || now == 5 || before == 0 || now == 0 ||
               cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 5) > 0 ||
               cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 0) > 0;
        if (!cast && cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, 6) > 9) {
            uint8_t d100 = cok_dice(&game->vm.seed, 1, 100); /* 546c:26ac */
            cast = d100 <= 75;
        }
        if (!cast && !cok_arena_say(game, character, "oops!", (uint8_t)(game->vm.cursor.y + 1),
                                    true))
            return false;
    }
    /* The cast may take items away (Dispel Magic ending Spiritual Hammer)
     * or add them; the original holds the item by its pointer. */
    character->held = index + 1;
    if (cast)
        cok_cast_spell(game, (uint8_t)spell, cok_field_u8(L, c, COK_FIELD_COMPUTER) != 0, false, 0,
                       done);
    size_t held = character->held;
    character->held = 0;
    if (game->vm.abort) return false;
    r->item = 0;
    /* In combat a spell that can be cast there (byte 11) ends the turn. */
    uint8_t usable = 0;
    if (combat &&
        (!cok_data_byte(D, (uint16_t)cok_data_offset2(D, COK_DATA_SPELLS, spell, 11), &usable)))
        return undefined(game, "a spell past the spell table is read (DS:31be, 546c:272c)");
    if (combat && usable != 0) {
        cok_combat_end_turn(character);
        *done = true;
    }
    if (!*done) return true;
    if (held == 0)
        return undefined(game, "the item used was freed while its spell was cast; the original "
                               "goes on with the freed item (546c:2746)");
    index = held - 1;
    item = character->items[index];
    if (scroll) {
        /* 5b04:575d: the last of its bytes with the spell goes, and a use
         * (+0x2f); below 100 the scroll goes. */
        size_t found = 0;
        for (size_t k = 1; k <= 3; ++k)
            if ((item[0x3b + k] & 0x7f) == spell) found = k;
        if (found == 0) return true;
        item[0x3b + found] = 0;
        item[0x2f] = (uint8_t)(item[0x2f] - 1);
        if (item[0x2f] < 100) delete_item(character, index);
        return true;
    }
    if (item[0x3c] == 0) return true;
    if (item[0x39] > 1) {
        --item[0x39];
    } else if (--item[0x3c] == 0) {
        delete_item(character, index);
    }
    return true;
}

/* Sell (546c:2822), in shops: half the value (+0x3a), or for a count
 * above 1 the count times that, a word, over 20, offered in rows 21-22;
 * "Is It a Deal? " Yes says "Sold!", the item goes, and the price goes to
 * the character's steel as a word, or what would overload it (58e7:006d,
 * the item still weighed) "Overloaded.  Money will be put in pool." to
 * the pool's steel. Returns 1 when sold, 0 when not, -1 when the run
 * ended. */
static int sell(cok_adventure *game, cok_character *character, size_t index)
{
    uint8_t *c = character->record, *item = character->items[index];
    uint16_t price = (uint16_t)((item[0x3a] | item[0x3b] << 8) >> 1);
    /* A test of type 0x1e then 0x0c meant to multiply arrows and quarrels
     * instead can never pass (546c:2851). */
    if (item[0x39] > 1) price = (uint16_t)((uint16_t)(item[0x39] * price) / 20);
    char name[41], text[96];
    if (!cok_item_name(game, item, false, name)) return -1;
    snprintf(text, sizeof text, "I'll give you %u steel pieces for your %s", price, name);
    print_low(game, text, true);
    int answer = cok_camp_yes_no(game, "Is It a Deal? ", 13);
    if (answer < 0) return -1;
    if (answer == 'Y') {
        cok_camp_notice(game, "Sold!");
        delete_item(character, index); /* 6346:1697, the stats left as they are */
        bool full;
        uint16_t fits;
        if (!cok_pool_overloaded(game, c, price, &full, &fits)) return -1;
        const cok_character_layout *L = cok_game_rules(game)->character;
        uint16_t steel = cok_field_u16_at(L, c, COK_FIELD_COINS, 4);
        if (full) {
            /* No pause, and the coins' weight is left to the recompute. */
            cok_camp_notice(game, "Overloaded.  Money will be put in pool.");
            if (fits > price) {
                steel = (uint16_t)(steel + price);
            } else {
                steel = (uint16_t)(steel + fits);
                game->pool.coins[4] += (uint16_t)(price - fits);
            }
        } else {
            steel = (uint16_t)(steel + price);
        }
        cok_field_set16_at(L, c, COK_FIELD_COINS, 4, steel);
        snprintf(text, sizeof text, "sold for %u steel", price);
        cok_adventure_log(game, "shop", text);
    }
    clear_cells(game, 1, 0x15, 0x26, 0x16);
    return answer == 'Y';
}

/* Id (546c:2a59), in shops: "For 100 steel pieces I'll identify your
 * NAME" and "Is It a Deal? "; Yes pays 100 from the character's money in
 * steel (546c:3424), all its coins then steel (58e7:0155), or else from
 * the pool's (58e7:00d3, 018f), or says "Not Enough Money". Paid, an item
 * with hidden parts (+0x35 bits 0-2) shows them, "It looks like some sort
 * of NAME", and the list is redrawn; one with none says "I can't tell
 * anything new about your NAME" all the same. Returns false when the run
 * ended. */
static bool identify(cok_adventure *game, cok_character *character, size_t index, bool *redraw)
{
    uint8_t *c = character->record, *item = character->items[index];
    char name[41], text[96];
    if (!cok_item_name(game, item, false, name)) return false;
    snprintf(text, sizeof text, "For 100 steel pieces I'll identify your %s", name);
    print_low(game, text, true);
    int answer = cok_camp_yes_no(game, "Is It a Deal? ", 13);
    if (answer < 0) return false;
    bool paid = false;
    if (answer == 'Y') {
        int32_t money = cok_shop_money(c, cok_game_rules(game)), value;
        if (money >= 100) {
            paid = true;
            cok_pool_pay(c, cok_game_rules(game), (uint16_t)(money - 100));
            cok_adventure_log(game, "shop", "identified for 100 steel");
        } else if ((value = cok_pool_value(game->pool.coins)) >= 100) {
            paid = true;
            cok_pool_set_steel(&game->pool, (uint16_t)(value - 100));
            cok_adventure_log(game, "shop", "identified for 100 steel from the pool");
        } else {
            cok_camp_notice(game, "Not Enough Money");
        }
    }
    if (paid) {
        if ((item[0x35] & 7) == 0) {
            snprintf(text, sizeof text, "I can't tell anything new about your %s", name);
        } else {
            item[0x35] &= 0xf8;
            if (!cok_item_name(game, item, false, name)) return false;
            snprintf(text, sizeof text, "It looks like some sort of %s", name);
            *redraw = true;
        }
        print_low(game, text, true);
        cok_adventure_wait(game, game->speed * 100u); /* 1521:0b4b */
    }
    clear_cells(game, 1, 0x15, 0x26, 0x16);
    return true;
}

/* The menu. */

void cok_items(cok_adventure *game, bool *done)
{
    cok_character *character = selected(game);
    if (character == NULL) {
        undefined(game, "Items with no character selected reads through NULL (546c:17f9)");
        return;
    }
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = character->record;
    int key = ' ', index = 0;
    bool redraw = true, header = true;
    static char text[256][41];
    cok_menu_row rows[256];
    while (key != 0 && key != 'E' && !*done && cok_field_u8(L, c, COK_FIELD_ITEM_COUNT) > 0 &&
           !game->vm.abort) {
        uint8_t count = cok_field_u8(L, c, COK_FIELD_ITEM_COUNT);
        if (character->item_count == 0)
            undefined(game, "an item count with no items loops forever (546c:1878)");
        if (game->vm.abort) return;
        /* 546c:187b: View first while the debug flag (DS:4b51) is set. */
        char items[41];
        snprintf(items, sizeof items, "%s", game->debug ? "View Ready" : "Ready");
        if (cok_field_u8(L, c, COK_FIELD_ACTIVE) != 0 && game->vm.mem4b00[0xe5] == 0 &&
            (game->vm.mode == 2 || game->vm.mode == 3 || game->vm.mode == 4 ||
             (game->vm.mode == 5 && cok_character_combat(character)->may_use != 0)))
            join(items, " Use");
        bool may_trade = cok_field_u8(L, c, COK_FIELD_NPC) < 0x80 ||
                         cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0 ||
                         cok_field_u8(L, c, COK_FIELD_STATUS) == 1;
        if (may_trade && game->vm.mode != 5) join(items, " Trade");
        join(items, " Drop");
        if (cok_field_u8(L, c, COK_FIELD_ITEM_COUNT) < 16) join(items, " Halve");
        join(items, " Join");
        if (may_trade && game->vm.mode == 1) join(items, " Sell");
        if (game->vm.mode == 1) join(items, " Id");
        size_t n = character->item_count > 255 ? 255 : character->item_count;
        for (size_t i = 0; i < n; ++i) {
            if (!cok_item_name(game, character->items[i], true, text[i])) return;
            rows[i] = (cok_menu_row){text[i], character->items[i][0x29] != 0};
        }
        if (header) {
            uint16_t moons[3];
            for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
            cok_screen_list(&game->screen, &game->view.tiles[4], moons);
            cok_item_draw_name(game, c, 1, 1, true);
            cok_text_string(&game->screen, &game->font, "Items",
                            cok_field_u8_at(L, c, COK_FIELD_NAME, 0) + 4, 1, 10, 0);
            cok_text_string(&game->screen, &game->font, "Ready Item", 1, 3, 15, 0);
            redraw = true;
            header = false;
        }
        cok_adventure_log(game, "list", "Items");
        for (size_t i = 0; i < n; ++i) cok_adventure_log(game, "item", text[i]);
        cok_adventure_log(game, "menu", items);
        cok_menu_style style = {"", items, 15, 10, 13, true};
        cok_keyboard keys = cok_adventure_keyboard(game);
        key = cok_menu_rows(&game->screen, &game->font, rows, n,
                            (cok_text_window){1, 5, 0x26, 0x16}, &style, &redraw, &index,
                            &game->list_top, &game->selected, &keys);
        if (key < 0) return;
        if (key != 0 && index >= 0 && (size_t)index < n) {
            size_t picked = (size_t)index;
            uint8_t *item = character->items[picked];
            char choice[2] = {(char)key, '\0'};
            cok_adventure_log(game, "choice", choice);
            int ok = 1;
            switch (key) {
            case 'V':
                ok = cok_debug_item(game, item); /* 546c:12be */
                redraw = header = true;
                break;
            case 'R': ok = cok_item_ready(game, character, picked); break;
            case 'U':
                if (item[0x34] == 0) {
                    cok_camp_notice(game, "Must be Readied");
                    key = ' ';
                } else if (cok_item_is_scroll(item) || (item[0x3d] > 0 && item[0x3e] < 0x80)) {
                    ok = cok_item_use(game, character, picked, done);
                    if (game->vm.mode != 5) *done = false;
                    if (!*done) header = true;
                }
                break;
            case 'T':
                ok = may_part(game, c, item);
                if (ok == 1) ok = trade(game, character, picked);
                else if (ok == 0) key = ' ', ok = 1;
                header = true;
                break;
            case 'D':
                ok = may_part(game, c, item);
                if (ok == 1) {
                    ok = drop(game, character, picked);
                    if (ok == 1) redraw = true;
                    if (ok >= 0) ok = 1;
                } else if (ok == 0) {
                    key = ' ';
                    ok = 1;
                }
                break;
            case 'H': ok = halve(game, character, picked); break;
            case 'J': join_items(character, picked); break;
            case 'S':
                ok = may_part(game, c, item);
                if (ok == 1) ok = sell(game, character, picked) >= 0;
                else if (ok == 0) key = ' ', ok = 1;
                break;
            case 'I': ok = identify(game, character, picked, &redraw); break;
            default: break;
            }
            if (ok <= 0 || game->vm.abort) return;
        }
        if (!recompute(game, character)) return;
        if (cok_field_u8(L, c, COK_FIELD_ITEM_COUNT) != count) redraw = true;
    }
}
