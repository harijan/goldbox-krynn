#include "shop.h"

#include "camp.h"
#include "cast.h"
#include "items.h"
#include "screen.h"
#include "sheet.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void put_word(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void name_of(const uint8_t *c, char out[16])
{
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(out, c + 1, length);
    out[length] = '\0';
}

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

static cok_character *selected(cok_adventure *game)
{
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* Draw text at x, y in fg on 0 (1521:0353), logged. */
static void draw(cok_adventure *game, const char *text, int x, int y, uint8_t fg)
{
    cok_adventure_log(game, "print", text);
    cok_text_string(&game->screen, &game->font, text, x, y, fg, 0);
}

/* Clear cells x1-x2 of rows y1-y2 (1521:0b60, 1128:07e6). */
static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    cok_picture_fill(&game->screen, x1, y1 * 8, (size_t)(x2 - x1 + 1), (size_t)(y2 - y1 + 1) * 8,
                     0);
}

/* Clear the frame, open (1128:0000 with 1). */
static void clear_frame(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t phase[3] = {vm->mem4b00[0x1f9], vm->mem4b00[0x1fa], vm->mem4b00[0x1fb]};
    cok_screen_frame(&game->screen, &game->view.tiles[4], phase, true);
}

/* 6346:0d20 on c, ending the run where it fails. */
static bool recompute(cok_adventure *game, cok_character *c)
{
    char error[300];
    if (cok_character_stats(c, &game->item_types, error, sizeof error)) return true;
    return undefined(game, error);
}

/* Name every item in the pool (6346:0488). */
static bool name_pool(cok_adventure *game)
{
    char name[41];
    for (size_t i = 0; i < game->pool.item_count; ++i)
        if (!cok_item_name(game, game->pool.items[i], false, name)) return false;
    return true;
}

bool cok_shop_price(uint16_t value, uint16_t factor, uint16_t *price)
{
    switch (factor) {
    case 1: *price = value >> 4; return true;
    case 2: *price = value >> 3; return true;
    case 4: *price = value >> 2; return true;
    case 8: *price = value >> 1; return true;
    case 0x10: *price = value; return true;
    case 0x20: *price = (uint16_t)(value << 1); return true;
    case 0x40: *price = (uint16_t)(value << 2); return true;
    case 0x80: *price = (uint16_t)(value << 3); return true;
    default: *price = value; return false;
    }
}

int32_t cok_shop_money(const uint8_t *c)
{
    uint32_t coins[COK_COINS] = {0};
    for (int k = 0; k < 5; ++k) coins[k] = word(c + 0xeb + 2 * k);
    return cok_pool_value(coins);
}

/* Say the leaving question in rows 17-22, the first text in light green,
 * the second in white after it (1521:04ac), and ask "~Yes ~No" (3775:1885,
 * the selection kept from the menu before). Returns 1 for No, 0 for Yes,
 * having cleared rows 17-22, or -1 if input ended. */
static int leave(cok_adventure *game, const char *said, const char *question)
{
    cok_text_window window = {1, 0x11, 0x26, 0x16};
    cok_adventure_print(game, said, window, 10, true);
    cok_adventure_print(game, question, window, 15, false);
    int answer = cok_adventure_horizontal(game, "", "~Yes ~No", 10, false);
    if (answer < 0) return -1;
    if (answer == 1) return 1;
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    return 0;
}

/* Buy (36d0:0484, with the list of 36d0:004e). */

/* 36d0:004e's lines: each pool item's line, 30 wide, written into its
 * name: the price at the factor of 0x7f6d right-aligned (the value itself
 * for a factor not in the table), and over it the name; an item worth 0
 * is made worth 1 first, for good. */
static void price_lines(cok_adventure *game)
{
    cok_pool *pool = &game->pool;
    uint16_t factor = game->vm.mem7c00[0x36d];
    for (size_t i = 0; i < pool->item_count; ++i) {
        uint8_t *item = pool->items[i];
        char line[31];
        memset(line, ' ', 30);
        if (word(item + 0x3a) == 0) put_word(item + 0x3a, 1);
        uint16_t price;
        cok_shop_price(word(item + 0x3a), factor, &price);
        char digits[8];
        int n = snprintf(digits, sizeof digits, "%u", price);
        memcpy(line + 30 - n, digits, (size_t)n);
        /* The name's characters past the 30th land in the stack beyond the
         * line, which the original does not show. */
        memcpy(line, item + 1, item[0] > 30 ? 30 : item[0]);
        item[0] = 30;
        memcpy(item + 1, line, 30);
    }
}

/* 36d0:004e: the lines, then the list of them, "Items: " and "Buy", in
 * cells 1-38 by 1-22, the menu selection (DS:6e0f) cleared first; then
 * every item in the pool is named again, which leaves the line's bytes
 * past the new name's length. Returns the key, 0 for Escape or Exit, or
 * -1 if the run ended. */
static int buy_list(cok_adventure *game, bool *redraw, int *index)
{
    cok_pool *pool = &game->pool;
    price_lines(game);
    size_t count = pool->item_count;
    char (*text)[31] = calloc(count + 1, sizeof *text);
    cok_menu_row *rows = calloc(count + 1, sizeof *rows);
    if (text == NULL || rows == NULL) {
        free(text);
        free(rows);
        undefined(game, "out of memory");
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        memcpy(text[i], pool->items[i] + 1, 30);
        rows[i] = (cok_menu_row){text[i], pool->items[i][0x29] != 0};
        if (i < 255) cok_adventure_log(game, rows[i].heading ? "heading" : "item", text[i]);
    }
    game->selected = 0;
    cok_menu_style style = {"Items: ", "Buy", 15, 10, 13, true};
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key = cok_menu_rows(&game->screen, &game->font, rows, count,
                            (cok_text_window){1, 1, 0x26, 0x16}, &style, redraw, index,
                            &game->list_top, &game->selected, &keys);
    if ((key == 'B' || key == 0x0d) && *index >= 0 && (size_t)*index < count)
        cok_adventure_log(game, "choice", text[*index]);
    free(text);
    free(rows);
    if (key < 0) return -1;
    if (!name_pool(game)) return -1;
    return key;
}

/* Say "NAME buys a ITEM", or "NAME buys ITEM" for an item with a count
 * (6346:1827). */
static void bought(cok_adventure *game, const uint8_t *c, const uint8_t *item)
{
    char name[16], text[80];
    name_of(c, name);
    size_t length = item[0] > 40 ? 40 : item[0];
    snprintf(text, sizeof text, "%s buys %s%.*s", name, item[0x39] > 0 ? "" : "a ", (int)length,
             (const char *)item + 1);
    cok_camp_notice(game, text);
}

/* Buy pool item index (36d0:0484) for its price at 0x7f6d: the selected
 * character pays if its money in steel (546c:3424, a word) covers it, all
 * its coins then steel (58e7:0155), else the pool if its worth in steel
 * (58e7:00d3) does (58e7:018f), else "Not enough Money."; a copy goes to
 * the character (36d0:034c) unless it would be overloaded, and the shop
 * keeps its own. For a factor not in the table the price is the word
 * Buy's stack holds: the colour 15 its caller pushed for the menu before
 * (36d0:08b2), so 15 steel. Returns false, ending the run, where it cannot
 * be carried out. */
static bool purchase(cok_adventure *game, size_t index)
{
    cok_pool *pool = &game->pool;
    uint8_t *item = pool->items[index];
    uint16_t price;
    if (!cok_shop_price(word(item + 0x3a), game->vm.mem7c00[0x36d], &price)) price = 15;
    cok_character *character = selected(game);
    if (character == NULL)
        return undefined(game, "Buy with no character selected reads through NULL (546c:3424)");
    uint8_t *c = character->record;
    uint16_t money = (uint16_t)cok_shop_money(c);
    char text[64];
    bool failed;
    if (price <= money) {
        if (!cok_treasure_take_item(game, item, &failed)) return false;
        if (failed) return true;
        cok_pool_pay(c, (uint16_t)(money - price));
        char name[16];
        name_of(c, name);
        snprintf(text, sizeof text, "%s pays %u steel", name, price);
    } else {
        int32_t value = cok_pool_value(pool->coins);
        if ((int32_t)price > value) {
            cok_camp_notice(game, "Not enough Money.");
            return true;
        }
        if (!cok_treasure_take_item(game, item, &failed)) return false;
        if (failed) return true;
        cok_pool_set_steel(pool, (uint16_t)(value - price));
        snprintf(text, sizeof text, "the pool pays %u steel", price);
    }
    cok_adventure_log(game, "shop", text);
    bought(game, c, item);
    return true;
}

/* Buy (36d0:0484): on a cleared frame, the list until it is left, each
 * item picked bought. */
static bool buy(cok_adventure *game)
{
    clear_frame(game);
    bool redraw = true; /* DS:43c6 */
    int index = 0;
    for (;;) {
        int key = buy_list(game, &redraw, &index);
        if (key < 0) return false;
        if (key != 'B' && key != 0x0d) return true;
        if (index < 0 || (size_t)index >= game->pool.item_count)
            return undefined(game, "Buy picked no item (36d0:0484)");
        if (!purchase(game, (size_t)index)) return false;
    }
}

/* Appraise (58e7:1929). */

/* A gem's value in steel: on a d100, 1-25 5, 26-50 25, 51-70 50, 71-90
 * 250, 91-99 500 and 100 2500. */
static uint16_t gem_value(uint32_t *seed)
{
    uint8_t roll = cok_dice(seed, 1, 100);
    return roll <= 25   ? 5
           : roll <= 50 ? 25
           : roll <= 70 ? 50
           : roll <= 90 ? 250
           : roll <= 99 ? 500
                        : 2500;
}

/* A jewel's value: a d100 picks a range, then Random within it (through
 * Turbo Pascal Reals, which change nothing): 1-10 50-499, 11-20 100-599,
 * 21-40 150-899, 41-50 250-1499, 51-70 500-2999, 71-90 1000-3999 and
 * 91-100 1000-5999. */
static uint16_t jewel_value(uint32_t *seed)
{
    static const struct { uint8_t top; uint16_t range, base; } ranges[] = {
        {10, 450, 50}, {20, 500, 100}, {40, 750, 150}, {50, 1250, 250},
        {70, 2500, 500}, {90, 3000, 1000}, {100, 5000, 1000},
    };
    uint8_t roll = cok_dice(seed, 1, 100);
    size_t i = 0;
    while (roll > ranges[i].top) ++i;
    return (uint16_t)(cok_tp_random(seed, ranges[i].range) + ranges[i].base);
}

/* Value one gem (part 0x7a) or jewel (0x7b) that c had and has just lost:
 * "The Gem is Valued at N steel." on row 12, then "You can : " with
 * "Sell Keep", or "Sell" alone if one more would overload c (58e7:006d,
 * against its weight before the coin went) or it has 16 items. Keep puts
 * a new item, type 0x2f, worth the value, weighing 1, unnamed, after c's
 * items, or loses it if c has none; any other key, Escape and special
 * keys too, sells it for the value in steel (58e7:01f2). */
static bool appraise_one(cok_adventure *game, cok_character *character, bool jewel)
{
    uint8_t *c = character->record;
    uint16_t value = jewel ? jewel_value(&game->vm.seed) : gem_value(&game->vm.seed);
    char text[48];
    snprintf(text, sizeof text, "The %s is Valued at %u steel.", jewel ? "Jewel" : "Gem", value);
    draw(game, text, 1, 0x0c, 15);
    bool full;
    uint16_t fits;
    if (!cok_pool_overloaded(game, c, 1, &full, &fits)) return false;
    bool keep = !full && c[0x142] < 16;
    bool special;
    int key = cok_camp_menu(game, "You can : ", keep ? "Sell Keep" : "Sell", true, false, &special);
    if (key < 0) return false;
    if (key == 'K' && keep) {
        /* The new item is linked after the last; with none, the original
         * keeps it in a local only (58e7:1df5, 213c), and it is lost. */
        if (character->item_count == 0) {
            cok_adventure_log(game, "shop", jewel ? "jewel kept and lost" : "gem kept and lost");
            return true;
        }
        uint8_t item[COK_ITEM_SIZE] = {0};
        item[0x31] = jewel ? 0x7b : 0x7a; /* "Jewelry", "Gem" */
        item[0x2e] = 0x2f;
        put_word(item + 0x3a, value);
        put_word(item + 0x37, 1);
        if (!cok_character_insert_item(character, character->item_count, item))
            return undefined(game, "out of memory");
        cok_adventure_log(game, "shop", jewel ? "jewel kept" : "gem kept");
        return true;
    }
    snprintf(text, sizeof text, "%s sold for %u steel", jewel ? "jewel" : "gem", value);
    cok_adventure_log(game, "shop", text);
    return cok_pool_give_steel(game, c, value);
}

bool cok_shop_appraise(cok_adventure *game, bool *shown)
{
    *shown = true;
    cok_character *character = selected(game);
    if (character == NULL)
        return undefined(game, "Appraise with no character selected reads through NULL "
                               "(58e7:1937)");
    uint8_t *c = character->record;
    if (word(c + 0xf5) == 0 && word(c + 0xf7) == 0) {
        cok_camp_notice(game, "No Gems or Jewelry");
        *shown = false;
        return true;
    }
    for (;;) {
        uint16_t gems = word(c + 0xf5), jewels = word(c + 0xf7);
        if (gems == 0 && jewels == 0) return true;
        char gem_text[32] = "", jewel_text[40] = "", menu[32] = "";
        if (gems > 0) snprintf(gem_text, sizeof gem_text, "%u Gem%s", gems, gems == 1 ? "" : "s");
        if (jewels > 0)
            snprintf(jewel_text, sizeof jewel_text, "%u %s of Jewelry", jewels,
                     jewels == 1 ? "piece" : "pieces");
        clear_cells(game, 1, 1, 0x26, 0x16);
        cok_item_draw_name(game, c, 1, 1, false); /* 6346:199d */
        draw(game, "You have a fine collection of:", 1, 7, 15);
        draw(game, gem_text, 1, 9, 15);
        draw(game, jewel_text, 1, 0x0a, 15);
        snprintf(menu, sizeof menu, "%s%s Exit", gems > 0 ? "  Gems" : "",
                 jewels > 0 ? "  Jewelry" : "");
        bool special;
        int key = cok_camp_menu(game, "Appraise : ", menu, true, false, &special);
        if (key < 0) return false;
        bool done = false;
        /* Special keys count by their scan codes' letters: Home (G) values
         * a gem. */
        if (key == 'G' && gems > 0) {
            put_word(c + 0xf5, (uint16_t)(gems - 1));
            if (!appraise_one(game, character, false)) return false;
        } else if (key == 'J' && jewels > 0) {
            put_word(c + 0xf7, (uint16_t)(jewels - 1));
            if (!appraise_one(game, character, true)) return false;
        } else if (key == 'E' || key == 0) {
            done = true;
        }
        if (!recompute(game, character)) return false;
        if (done) return true;
    }
}

/* The shop (36d0:07da). */

void cok_shop(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    vm->mode = 1;
    game->shop_frame = false; /* DS:883c */
    cok_adventure_redraw(game);
    game->shop_frame = true;
    cok_adventure_party(game);
    memset(game->pool.coins, 0, sizeof game->pool.coins);
    if (!name_pool(game)) return;
    char text[32];
    snprintf(text, sizeof text, "prices at %u", vm->mem7c00[0x36d]);
    cok_adventure_log(game, "shop", text);
    /* What Trade's first Escape tests (see sheet.h): on entry what the run
     * left there. A View leaves it set (the length of a string 6346:2d75
     * builds as View redraws), and so do Yes to going back for the money (a
     * string 3775:1885 builds) and Appraise when it shows the gems (the
     * return address 67b5:0c2d); Buy, Take, Pool, Share and picking a
     * character leave it as it was. An emulator run of each agreed. */
    cok_sheet_stale stale = COK_SHEET_STALE_UNKNOWN;
    for (;;) {
        bool money, items;
        cok_pool_status(&game->pool, &money, &items);
        bool special;
        int key = cok_camp_menu(game, "",
                                money ? "Buy View Take Pool Share Appraise Exit"
                                      : "Buy View Pool Appraise Exit",
                                true, false, &special);
        if (key < 0) return;
        bool done = false, shown = false;
        /* Special keys count by their scan codes' letters (F8 buys, Del
         * shares...); only P, the down arrow, is told apart. */
        switch (key) {
        case 'B':
            if (!buy(game)) return;
            break;
        case 'V': {
            bool used;
            cok_sheet(game, stale, &used);
            stale = COK_SHEET_STALE_SET;
            break;
        }
        case 'T':
            if (!cok_pool_take_money(game)) return;
            break;
        case 'P':
            if (special)
                vm->character = cok_party_special(&game->party, vm->character, (uint8_t)key);
            else
                cok_pool_gather(game);
            break;
        case 'S':
            if (!cok_pool_share(game)) return;
            break;
        case 'A':
            if (!cok_shop_appraise(game, &shown)) return;
            if (shown) stale = COK_SHEET_STALE_SET;
            break;
        case 'E':
            cok_pool_status(&game->pool, &money, &items);
            if (!money) {
                done = true;
                break;
            }
            switch (leave(game,
                          "As you Leave the Shopkeeper says, \"Excuse me but you have Left Some "
                          "Money here.\"  ",
                          "Do you want to go back and get your Money?")) {
            case -1: return;
            case 1: done = true; break;
            default: stale = COK_SHEET_STALE_SET; break;
            }
            break;
        case 'H':
            vm->character = cok_party_special(&game->party, vm->character, (uint8_t)key);
            break;
        default: break;
        }
        if (vm->abort || vm->status != COK_ECL_OK) return;
        if (key == 'B' || key == 'T' || (key == 'A' && shown)) cok_adventure_redraw(game);
        cok_adventure_party(game);
        if (done) return;
    }
}

/* The temple (overlay 340d). */

static bool effect_failed(cok_adventure *game)
{
    cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", game->effects.error);
    return false;
}

/* Remove c's first effect id (60f4:01e9), if any. */
static bool remove_effect(cok_adventure *game, cok_character *c, uint8_t id)
{
    return cok_effects_remove(&game->effects, c, NULL, id) || effect_failed(game);
}

/* Work out ability stat of c (60f4:1743). */
static bool ability(cok_adventure *game, cok_character *c, unsigned stat)
{
    return cok_effects_ability(&game->effects, c, stat) || effect_failed(game);
}

/* 340d:0045: say text about c in the text window, without a pause
 * (6346:1883), and ask "cast cure anyway: "; rows 18-22 are cleared after
 * (6346:196a). Returns 'Y', 'N' or -1 if input ended. */
static int anyway(cok_adventure *game, const uint8_t *c, const char *text)
{
    cok_camp_say(game, c, text, false);
    int answer = cok_camp_yes_no(game, "cast cure anyway: ", 13);
    if (answer >= 0) cok_camp_clear_text(game);
    return answer;
}

/* 340d:00f2: "CURE will only cost N steel pieces." in rows 17-22 and "pay
 * for cure "; Yes pays the price from the selected character's money in
 * steel (546c:3424, a word) if it covers it, all its coins then steel
 * (58e7:0155), else from the pool's worth (58e7:00d3, 018f), else "Not
 * enough money.". Paid, the character "is cured." whatever the cure then
 * does. Returns 'Y' when paid, 'N' when not, -1 if input ended. */
static int pay(cok_adventure *game, cok_character *character, const char *cure, uint16_t price)
{
    uint8_t *c = character->record;
    char text[96];
    snprintf(text, sizeof text, "%s will only cost %u steel pieces.", cure, price);
    cok_adventure_print(game, text, (cok_text_window){1, 0x11, 0x26, 0x16}, 10, true);
    int answer = cok_camp_yes_no(game, "pay for cure ", 13);
    if (answer != 'Y') return answer;
    uint16_t money = (uint16_t)cok_shop_money(c);
    char name[16];
    name_of(c, name);
    if (price <= money) {
        cok_pool_pay(c, (uint16_t)(money - price));
        snprintf(text, sizeof text, "%s pays %u steel", name, price);
    } else {
        int32_t value = cok_pool_value(game->pool.coins);
        if ((int32_t)price > value) {
            cok_camp_notice(game, "Not enough money.");
            return 'N';
        }
        cok_pool_set_steel(&game->pool, (uint16_t)(value - price));
        snprintf(text, sizeof text, "the pool pays %u steel", price);
    }
    cok_adventure_log(game, "shop", text);
    cok_camp_clear_text(game);
    cok_camp_say(game, c, "is cured.", true);
    return 'Y';
}

/* Ask for the cure: "anyway" with text unless ok, then the price. Returns
 * 'Y' when paid, 'N', or -1 if input ended. */
static int ask(cok_adventure *game, cok_character *c, bool ok, const char *text, const char *cure,
               uint16_t price)
{
    int answer = ok ? 'Y' : anyway(game, c->record, text);
    return answer == 'Y' ? pay(game, c, cure, price) : answer;
}

/* The diseases (DS:01c6-01cb). */
static const uint8_t diseases[6] = {0x1f, 0x22, 0x2b, 0x2c, 0x32, 0x39};

/* 340d:027f, Cure Blindness, 500: effect 0x21 goes. */
static int cure_blindness(cok_adventure *game, cok_character *c)
{
    int answer = ask(game, c, cok_character_find_effect(c, 0x21) != NULL, "is not blind.",
                     "Cure Blindness", 500);
    if (answer != 'Y') return answer;
    return remove_effect(game, c, 0x21) ? 'Y' : -1;
}

/* 340d:0323, Cure Disease, 500: while curing (DS:6b38), each disease goes,
 * then strength is recomputed (60f4:1743). */
static int cure_disease(cok_adventure *game, cok_character *c)
{
    bool diseased = false;
    for (size_t i = 0; i < sizeof diseases; ++i)
        if (cok_character_find_effect(c, diseases[i]) != NULL) diseased = true;
    int answer = ask(game, c, diseased, "is not Diseased.", "Cure Disease", 500);
    if (answer != 'Y') return answer;
    game->effects.rolls.curing = 1;
    for (size_t i = 0; i < sizeof diseases; ++i)
        if (!remove_effect(game, c, diseases[i])) return -1;
    if (!ability(game, c, 0)) return -1;
    game->effects.rolls.curing = 0;
    return 'Y';
}

/* 340d:044c: Cure Light (1d8, 50), Serious (2d8 + 1, 175) and Critical
 * Wounds (3d8 + 3, 300), rolled once paid, for anyone (60f4:21ea); or Heal
 * (2500): up to the maximum less 1d4, then blindness, the diseases (not
 * while curing, so a drain's handler adds it again) and 0x44 go, and
 * intelligence and wisdom are worked out, which keeps nothing. */
static int wounds(cok_adventure *game, cok_character *character, unsigned kind)
{
    static const char *const names[4] = {"Cure Light Wounds", "Cure Serious Wounds",
                                         "Cure Critical Wounds", "Heal"};
    static const uint16_t prices[4] = {50, 175, 300, 2500};
    uint8_t *c = character->record;
    int answer = pay(game, character, names[kind - 1], prices[kind - 1]);
    if (answer != 'Y') return answer;
    uint32_t *seed = &game->vm.seed;
    uint8_t amount;
    switch (kind) {
    case 1: amount = cok_dice(seed, 1, 8); break;
    case 2: amount = (uint8_t)(cok_dice(seed, 2, 8) + 1); break;
    case 3: amount = (uint8_t)(cok_dice(seed, 3, 8) + 3); break;
    default: {
        uint8_t d = cok_dice(seed, 1, 4);
        amount = c[0x197] + d > c[0x62] ? 0 : (uint8_t)(c[0x62] - c[0x197] - d);
        break;
    }
    }
    cok_character_heal(c, amount, false, game->vm.mode);
    if (kind != 4) return 'Y';
    if (!remove_effect(game, character, 0x21)) return -1;
    for (size_t i = 0; i < sizeof diseases; ++i)
        if (!remove_effect(game, character, diseases[i])) return -1;
    if (!remove_effect(game, character, 0x44)) return -1;
    for (unsigned stat = 1; stat <= 2; ++stat)
        if (!ability(game, character, stat)) return -1;
    return 'Y';
}

/* 340d:0688, Raise Dead, 2750: for one dead or animated (status 6 or 1)
 * and not an elf (race 0 or 1): while curing, 0x20 and 0x37 go; 1 hit
 * point, okay, able to act, a point of constitution less (with no
 * recompute); and from a constitution left of 14 or more, the maximum hit
 * points above +0x11b, divided by a byte sum over its class levels (each
 * level, two for one above 15, a fighter's times the constitution less 14;
 * none when the sum wraps to 0), come off the maximum, unless the
 * constitution is 17 or more, there is no fighter level and +0xfb is not
 * above +0xd7. Paid "anyway", it raises no one. */
static int raise_dead(cok_adventure *game, cok_character *character)
{
    uint8_t *c = character->record;
    bool dead = c[0x188] == 6 || c[0x188] == 1, elf = (int8_t)c[0x5a] < 2;
    int answer = ask(game, character, dead && !elf, elf ? "cannot be raised" : "is not dead.",
                     "Raise Dead", 2750);
    if (answer != 'Y' || !dead || elf) return answer;
    game->effects.rolls.curing = 1;
    if (!remove_effect(game, character, 0x20) || !remove_effect(game, character, 0x37)) return -1;
    game->effects.rolls.curing = 0;
    c[0x197] = 1;
    c[0x188] = 0;
    c[0x189] = 1;
    if (c[0x19] > 0) --c[0x19];
    uint8_t loss = c[0x62] > c[0x11b] ? (uint8_t)(c[0x62] - c[0x11b]) : 0, sum = 0;
    uint8_t con = c[0x19];
    if (con < 14) return 'Y';
    for (int i = 0; i <= 7; ++i) {
        int8_t level = (int8_t)c[0xf9 + i];
        if (level <= 0) continue;
        if (i == 2)
            sum = (uint8_t)(sum + (uint16_t)level * (uint16_t)(con - 14));
        else
            sum = (uint8_t)(sum + (con > 15 ? 2 * level : level));
    }
    if (sum > 0) loss = (uint8_t)(loss / sum);
    if (con < 17 || (int8_t)c[0xfb] > 0 || (int8_t)c[0xfb] > (int8_t)c[0xd7])
        c[0x62] = (uint8_t)(c[0x62] - loss);
    return 'Y';
}

/* 340d:0924, Neutralize Poison, 500: for one poisoned (0x37), whatever its
 * status: at least a hit point, 0x37, 0x16 and 0x0f go while curing, and
 * it is okay and can act. */
static int neutralize_poison(cok_adventure *game, cok_character *character)
{
    uint8_t *c = character->record;
    bool poisoned = cok_character_find_effect(character, 0x37) != NULL;
    int answer = ask(game, character, poisoned, "is not poisoned.", "Neutralize Poison", 500);
    if (answer != 'Y' || !poisoned) return answer;
    if (c[0x197] == 0) c[0x197] = 1;
    game->effects.rolls.curing = 1;
    static const uint8_t poisons[3] = {0x37, 0x16, 0x0f};
    for (size_t i = 0; i < sizeof poisons; ++i)
        if (!remove_effect(game, character, poisons[i])) return -1;
    game->effects.rolls.curing = 0;
    c[0x189] = 1;
    c[0x188] = 0;
    return 'Y';
}

/* 340d:0a44, Remove Curse, 1750: for one with a cursed item (+0x36) or
 * Bestow Curse (0x24), the spell's routine on it (5b04:35f5), which leaves
 * the item cursed. */
static int remove_curse(cok_adventure *game, cok_character *character)
{
    bool cursed = cok_character_find_effect(character, 0x24) != NULL;
    for (size_t i = 0; i < character->item_count && !cursed; ++i)
        cursed = character->items[i][0x36] != 0;
    int answer = ask(game, character, cursed, "is not cursed.", "Remove Curse", 1750);
    if (answer != 'Y') return answer;
    return cok_cast_remove_curse(game, character) ? 'Y' : -1;
}

/* 340d:0b32, Stone to Flesh, 1000: one stoned (status 7) is okay, can act
 * and has 1 hit point. */
static int stone_to_flesh(cok_adventure *game, cok_character *character)
{
    uint8_t *c = character->record;
    int answer = ask(game, character, c[0x188] == 7, "is not stoned.", "Stone to Flesh", 1000);
    if (answer != 'Y' || c[0x188] != 7) return answer;
    c[0x188] = 0;
    c[0x189] = 1;
    c[0x197] = 1;
    return 'Y';
}

/* Heal (340d:0be1): in the big frame, "NAME, how can we help you?" on row
 * 1 in white, and the cures (DS:01cc, 41 bytes each) in cells 2-38 by
 * 4-15 with "Heal Exit", for the selected character; until Exit or
 * Escape. The list is drawn once: each pass clears the frame, so only the
 * row picked comes back. Its first pick is the word the menu before left
 * on the stack, 0 (the flag pushed for 67b5:03e2 at 340d:0f47). Then the
 * screen is redrawn. */
static bool heal(cok_adventure *game)
{
    static const char *const cures[10] = {
        "Cure Blindness", "Cure Disease", "Cure Light Wounds", "Cure Serious Wounds",
        "Cure Critical Wounds", "Heal", "Neutralize Poison", "Raise Dead", "Remove Curse",
        "Stone to Flesh",
    };
    cok_menu_row rows[10];
    for (size_t i = 0; i < 10; ++i) rows[i] = (cok_menu_row){cures[i], false};
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 1521:0b60 */
    bool redraw = true;
    int index = 0;
    for (;;) {
        cok_character *character = selected(game);
        if (character == NULL)
            return undefined(game, "Heal with no character selected reads through NULL "
                                   "(340d:0c86)");
        cok_ecl *vm = &game->vm;
        uint16_t phase[3] = {vm->mem4b00[0x1f9], vm->mem4b00[0x1fa], vm->mem4b00[0x1fb]};
        cok_screen_big(&game->screen, &game->view.tiles[4], phase);
        char name[16], header[48];
        name_of(character->record, name);
        snprintf(header, sizeof header, "%s, how can we help you?", name);
        header[40] = '\0';
        draw(game, header, 1, 1, 15);
        if (redraw)
            for (size_t i = 0; i < 10; ++i) cok_adventure_log(game, "item", cures[i]);
        cok_menu_style style = {"", "Heal Exit", 15, 10, 13, false};
        cok_keyboard keys = cok_adventure_keyboard(game);
        int key = cok_menu_rows(&game->screen, &game->font, rows, 10,
                                (cok_text_window){2, 4, 0x26, 0x0f}, &style, &redraw, &index,
                                &game->list_top, &game->selected, &keys);
        if (key < 0) return false;
        if (key == 0) break;
        if (key != 'H' && key != 0x0d) continue;
        if (index < 0 || index > 9) continue;
        cok_adventure_log(game, "choice", cures[index]);
        int answer;
        switch (index) {
        case 0: answer = cure_blindness(game, character); break;
        case 1: answer = cure_disease(game, character); break;
        case 2: case 3: case 4: case 5:
            answer = wounds(game, character, (unsigned)index - 1);
            break;
        case 6: answer = neutralize_poison(game, character); break;
        case 7: answer = raise_dead(game, character); break;
        case 8: answer = remove_curse(game, character); break;
        default: answer = stone_to_flesh(game, character); break;
        }
        if (answer < 0 || game->vm.abort || game->vm.status != COK_ECL_OK) return false;
    }
    cok_adventure_redraw(game);
    cok_adventure_party(game);
    return true;
}

void cok_temple(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    vm->mode = 1;
    game->shop_frame = false; /* DS:883c */
    cok_adventure_redraw(game);
    game->shop_frame = true;
    cok_adventure_party(game);
    memset(game->pool.coins, 0, sizeof game->pool.coins);
    /* Trade's byte (see the shop): the temple's locals are 14 bytes fewer,
     * so every call made from its menu, and the byte, sit 14 bytes higher,
     * and the shop's commands leave it as there; Heal, which ends
     * redrawing the screen, sets it too. */
    cok_sheet_stale stale = COK_SHEET_STALE_UNKNOWN;
    for (;;) {
        bool money, items;
        cok_pool_status(&game->pool, &money, &items);
        bool special;
        int key = cok_camp_menu(game, "",
                                money ? "Heal View Take Pool Share Appraise Exit"
                                      : "Heal View Pool Appraise Exit",
                                true, false, &special);
        if (key < 0) return;
        bool done = false, shown = false;
        /* As the shop, but Escape does nothing, and only H and P, the up
         * and down arrows, are told apart from special keys. */
        switch (key) {
        case 'H':
            if (special)
                vm->character = cok_party_special(&game->party, vm->character, (uint8_t)key);
            else if (!heal(game))
                return;
            else
                stale = COK_SHEET_STALE_SET;
            break;
        case 'V': {
            bool used;
            cok_sheet(game, stale, &used);
            stale = COK_SHEET_STALE_SET;
            break;
        }
        case 'T':
            if (!cok_pool_take_money(game)) return;
            break;
        case 'P':
            if (special)
                vm->character = cok_party_special(&game->party, vm->character, (uint8_t)key);
            else
                cok_pool_gather(game);
            break;
        case 'S':
            if (!cok_pool_share(game)) return;
            break;
        case 'A':
            if (!cok_shop_appraise(game, &shown)) return;
            if (shown) stale = COK_SHEET_STALE_SET;
            break;
        case 'E': {
            cok_pool_status(&game->pool, &money, &items);
            if (!money) {
                done = true;
                break;
            }
            /* Both texts clear the window, so the first goes at once. */
            cok_text_window window = {1, 0x11, 0x26, 0x16};
            cok_adventure_print(game,
                                "As you leave a priest says, \"Excuse me but you have left some "
                                "money here\" ",
                                window, 10, true);
            cok_adventure_print(game, "Do you want to go back and retrieve your money?", window,
                                10, true);
            int answer = cok_adventure_horizontal(game, "", "~Yes ~No", 10, false);
            if (answer < 0) return;
            if (answer == 1) {
                done = true;
            } else {
                clear_cells(game, 1, 0x11, 0x26, 0x16);
                stale = COK_SHEET_STALE_SET;
            }
            break;
        }
        default: break;
        }
        if (vm->abort || vm->status != COK_ECL_OK) return;
        /* B, which no item offers here but F8 gives, redraws as the shop's
         * Buy did. */
        if (key == 'B' || key == 'T' || (key == 'A' && shown)) cok_adventure_redraw(game);
        cok_adventure_party(game);
        if (done) return;
    }
}
