#include "shop.h"

#include "camp.h"
#include "items.h"
#include "monster.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[16384];
} script;

static cok_adventure game;
static int first_ink = -1; /* set ink of the top cell row at the first key read */
/* The screen as key number snap_key (from 0) was read; -1 for none. */
static uint8_t shown[40 * 4 * 200];
static long snap_key = -1;
static unsigned waits;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == 0 && first_ink < 0) {
        first_ink = 0;
        for (size_t i = 0; i < 8 * (size_t)game.screen.units * 4; ++i)
            if (game.screen.pixels[i] != 0) first_ink = 1;
    }
    if ((long)s->at == snap_key) memcpy(shown, game.screen.pixels, sizeof shown);
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g;
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static void unported(cok_adventure *g, void *context)
{
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "[%s];", cok_ecl_opcode_name(g->vm.opcode));
}

static void say(cok_effects *fx, cok_character *c, const char *text, bool wait, void *context)
{
    (void)fx;
    (void)c;
    (void)wait;
    log_line(NULL, "say", text, context);
}

static script s;

static void delayed(cok_adventure *g, unsigned ms, void *context)
{
    (void)g, (void)ms, (void)context;
    ++waits;
}

/* The first colour drawn in cell x, y of the screen snapped, or 0. */
static uint8_t ink(int x, int y)
{
    for (int row = 0; row < 8; ++row)
        for (int b = 0; b < 4; ++b) {
            uint8_t v = shown[(y * 8 + row) * 160 + x * 4 + b];
            if (v >> 4) return v >> 4;
            if (v & 15) return v & 15;
        }
    return 0;
}

/* Whether cells x, y1 and x, y2 of the screen snapped look the same. */
static bool same_cells(int x, int y1, int y2)
{
    for (int row = 0; row < 8; ++row)
        if (memcmp(shown + (y1 * 8 + row) * 160 + x * 4, shown + (y2 * 8 + row) * 160 + x * 4, 4))
            return false;
    return true;
}

/* The rows of y1-y2 in cells x1-x2 with any ink. */
static int inked_rows(int x1, int y1, int x2, int y2)
{
    int n = 0;
    for (int y = y1; y <= y2; ++y) {
        bool any = false;
        for (int x = x1; x <= x2 && !any; ++x) any = ink(x, y) != 0;
        n += any;
    }
    return n;
}

static void open_game(void)
{
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .unported = unported, .delay = delayed,
                                 .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    game.effects.say = say;
    game.effects.context = &s;
}

static void reset(const char *k)
{
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    game.vm.character = game.vm.saved_character = NULL;
    game.vm.restore_character = false;
    game.combat_stub = COK_COMBAT_UNPORTED;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    game.vm.mode = 4;
    game.vm.mem4b00[0xe6] = 1;
    game.vm.file = 1;
    /* Records 0 held with no pictures: these 3D shops, in ECL1, which has
     * no HEAD1.DAX, show the portrait last shown, 0 and 0, as nothing. */
    cok_adventure_forget_portrait(&game);
    game.head_id = game.body_id = game.portrait_head = game.portrait_body = 0;
    game.monsters = game.undead = 0;
    game.monsters_loaded = false;
    game.icon_slot = 8;
    game.quit = game.party_killed = false;
    game.experience = 0;
    game.experience_known = false;
    game.selected = 1;
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.log[0] = '\0';
    game.input_ended = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
    snap_key = -1;
}

static cok_ecl_status run(const uint8_t *code, size_t length)
{
    memset(game.vm.code, 0, sizeof game.vm.code);
    memcpy(game.vm.code, code, length);
    game.vm.size = length + 1;
    game.vm.depth = 0;
    return cok_ecl_run(&game.vm, COK_ECL_BASE);
}

#define RUN(...) run((const uint8_t[]){__VA_ARGS__}, sizeof (const uint8_t[]){__VA_ARGS__})
#define LOGGED(text) (strstr(s.log, text) != NULL)

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void put_word(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* A character of class 7 (a knight) with strength 18 that can act and
 * steel pieces, added to the party. */
static cok_character *member(const char *name, uint16_t steel)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = (uint8_t)strlen(name);
    memcpy(r + 1, name, strlen(name));
    r[0x189] = 1;
    r[0x5b] = 7;
    r[0x5a] = 6; /* human */
    for (int i = 0; i < 6; ++i) r[0x10 + 2 * i] = r[0x11 + 2 * i] = 10;
    r[0x10] = r[0x11] = 18;
    put_word(r + 0xf3, steel);
    put_word(r + 0x17d, steel); /* the weight, as the stats count it */
    CHECK(cok_party_add(&game.party, c));
    ++game.vm.mem7c00[0x33e];
    if (game.vm.character == NULL) game.vm.character = r;
    return c;
}

static uint8_t *give(cok_character *c, uint8_t type, uint16_t value)
{
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[0x2e] = type;
    it[0x31] = type;
    put_word(it + 0x3a, value);
    put_word(it + 0x37, 10);
    CHECK(cok_character_insert_item(c, c->item_count, it));
    return c->items[c->item_count - 1];
}

/* Stock an item of type, named by its type, worth value, with count. */
static void stock(uint8_t type, uint16_t value, uint8_t count)
{
    uint8_t it[COK_ITEM_SIZE] = {0};
    it[0x2e] = it[0x31] = type;
    put_word(it + 0x3a, value);
    put_word(it + 0x37, 10);
    it[0x39] = count;
    CHECK(cok_pool_insert(&game.pool, game.pool.item_count, it));
}

/* A seed whose next d<sides> is roll. */
static uint32_t seed_for(uint8_t sides, uint8_t roll)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        if (cok_dice(&seed, 1, sides) == roll) return start;
    }
}

static int logged_times(const char *text)
{
    int n = 0;
    for (const char *p = strstr(s.log, text); p != NULL; p = strstr(p + 1, text)) ++n;
    return n;
}

/* COMBAT with a shop (0x7f6c) at prices of factor, or the temple. */
static cok_ecl_status shop(uint16_t factor)
{
    game.vm.mem7c00[0x36c] = 1;
    game.vm.mem7c00[0x36d] = factor;
    return RUN(COK_ECL_COMBAT);
}

static cok_ecl_status temple(void)
{
    game.vm.mem7c00[0x2e2] = 1;
    return RUN(COK_ECL_COMBAT);
}

/* The line Buy lists: name, then the price ending at column 30. */
static const char *line(const char *name, unsigned price)
{
    static char text[64];
    char digits[8];
    snprintf(digits, sizeof digits, "%u", price);
    snprintf(text, sizeof text, "item: %-*s%s;", 30 - (int)strlen(digits), name, digits);
    return text;
}

static void test_prices(void)
{
    static const struct { uint16_t factor, value, price; } cases[] = {
        {1, 100, 6}, {2, 100, 12}, {4, 100, 25}, {8, 100, 50}, {0x10, 100, 100},
        {0x20, 100, 200}, {0x40, 100, 400}, {0x80, 100, 800}, {0x80, 0x2001, 8},
        {0x40, 0x4001, 4},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        uint16_t price = 0;
        CHECK(cok_shop_price(cases[i].value, cases[i].factor, &price));
        CHECK(price == cases[i].price);
    }
    /* Any other factor: the list shows the value; Buy charges 15. */
    static const uint16_t odd[] = {0, 3, 0x100, 0xffff};
    for (size_t i = 0; i < sizeof odd / sizeof *odd; ++i) {
        uint16_t price = 0;
        CHECK(!cok_shop_price(100, odd[i], &price) && price == 100);
    }
    /* Money in steel: 50 silver, 10 copper, 5 bronze and 2 platinum are a
     * steel piece each; 49 silver are none; the most is 119,273. */
    uint8_t r[COK_CHARACTER_SIZE] = {0};
    uint16_t coins[5] = {50, 10, 5, 2, 7};
    for (int k = 0; k < 5; ++k) put_word(r + 0xeb + 2 * k, coins[k]);
    CHECK(cok_shop_money(r) == 11);
    memset(r, 0, sizeof r);
    put_word(r + 0xeb, 49);
    put_word(r + 0xf5, 9);
    put_word(r + 0xf7, 9);
    CHECK(cok_shop_money(r) == 0);
    for (int k = 0; k < 5; ++k) put_word(r + 0xeb + 2 * k, 0xffff);
    CHECK(cok_shop_money(r) == 119273);
}

static void test_buy(void)
{
    /* Buy a long sword at 7: the character pays, all its coins becoming
     * steel, and gets a copy; the shop keeps its own. Arrows worth 0 are
     * worth 1 from then on, and bought by count, "buys 20 Arrows". */
    reset("BB\001PB\033E");
    cok_character *a = member("A", 20);
    put_word(a->record + 0xeb, 120); /* 2 steel more in silver */
    stock(0x12, 7, 0);
    stock(0x1e, 0, 20);
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(LOGGED("shop: prices at 16;menu: Buy View Pool Appraise Exit;"));
    CHECK(LOGGED(line("Long Sword ", 7)));
    CHECK(LOGGED(line("20 Arrows ", 1)));
    CHECK(LOGGED("choice: Long Sword                   7;shop: A pays 7 steel;"
                 "print: A buys a Long Sword ;"));
    CHECK(LOGGED("shop: A pays 1 steel;print: A buys 20 Arrows ;"));
    CHECK(word(a->record + 0xeb) == 0 && word(a->record + 0xf3) == 14);
    CHECK(a->item_count == 2 && a->items[0][0x2e] == 0x12 && a->items[1][0x39] == 20);
    CHECK(game.pool.item_count == 2 && word(game.pool.items[1] + 0x3a) == 1);
    /* The lines are written into the names, which keep them past their
     * new lengths, and so do the copies bought. */
    CHECK(game.pool.items[0][0] == 11 && game.pool.items[0][30] == '7');
    CHECK(a->items[0][30] == '7' && a->items[1][30] == '1');
    /* The pool's coins were emptied on entry; the mode is the area's after. */
    CHECK(game.vm.mode == 4 && game.shop_frame);

    /* Not enough money, then the pool's: Pool gathers it, and Buy pays
     * from it, leaving only steel; leaving with money left asks, and No
     * leaves it in the pool. The pool's coins before the shop are gone. */
    reset("BB\033PBB\033EN");
    a = member("A", 3);
    cok_character *b = member("B", 100);
    put_word(b->record + 0xeb, 75);
    stock(0x12, 7, 0);
    game.pool.coins[4] = 1000;
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(LOGGED("print: Not enough Money.;"));
    CHECK(LOGGED("menu: Buy View Take Pool Share Appraise Exit;"));
    CHECK(LOGGED("shop: the pool pays 7 steel;print: A buys a Long Sword ;"));
    /* Both pooled 75 silver and 103 steel, worth 104, less 7. */
    CHECK(game.pool.coins[0] == 0 && game.pool.coins[4] == 97 && word(a->record + 0xf3) == 0);
    CHECK(LOGGED("print: As you Leave the Shopkeeper says, \"Excuse me but you have Left Some "
                 "Money here.\"  ;print: Do you want to go back and get your Money?;menu: ~Yes "
                 "~No;"));

    /* Yes goes back. */
    reset("PEYEN");
    member("A", 3);
    CHECK(shop(0x10) == COK_ECL_OK && logged_times("menu: ~Yes ~No;") == 2);

    /* A factor not in the table: listed at the value, sold for 15. */
    reset("BB\033E");
    a = member("A", 20);
    stock(0x12, 500, 0);
    CHECK(shop(0) == COK_ECL_OK && LOGGED(line("Long Sword ", 500)));
    CHECK(LOGGED("shop: A pays 15 steel;") && word(a->record + 0xf3) == 5);

    /* Money worth 65,536 steel or more counts by its low word. */
    reset("BB\033E");
    a = member("A", 0xffff);
    put_word(a->record + 0xf1, 0xffff); /* 32767 steel in platinum */
    stock(0x12, 7, 0);
    CHECK(shop(0x10) == COK_ECL_OK && word(a->record + 0xf3) == 32759);
    CHECK(word(a->record + 0xf1) == 0);

    /* Worth 65,536 and 1: as a word, 0, too little, and the pool has none. */
    reset("BB\033E");
    a = member("A", 0xffff);
    put_word(a->record + 0xf1, 3);
    stock(0x12, 7, 0);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("print: Not enough Money.;") && a->item_count == 0);

    /* Exactly the price, from the character, then from the pool; a count
     * of 1 is bought without "a". */
    reset("BB\033PBB\033E");
    a = member("A", 7);
    member("B", 7);
    stock(0x12, 7, 1);
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(LOGGED("shop: A pays 7 steel;print: A buys 1 Long Sword ;"));
    CHECK(LOGGED("shop: the pool pays 7 steel;") && game.pool.coins[4] == 0 && a->item_count == 2);

    /* Buy's list starts on its first menu item, whatever the shop's menu
     * had selected: the right arrow selects View, F8 buys, Enter picks. */
    reset("\001M\001\x42\r\033E");
    a = member("A", 20);
    stock(0x12, 7, 0);
    CHECK(shop(0x10) == COK_ECL_OK && a->item_count == 1);

    /* Sixteen items: OverLoaded, and nothing paid. */
    reset("BB\033E");
    a = member("A", 20);
    for (int i = 0; i < 16; ++i) give(a, 4, 1);
    stock(0x12, 7, 0);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("print: OverLoaded;"));
    CHECK(word(a->record + 0xf3) == 20 && a->item_count == 16);
}

static void test_keys(void)
{
    /* Escape does nothing; F8 (scan code B) buys; down and up (P and H)
     * pick a character; plain P pools. */
    reset("\033\001\x42" "B\033\001P\001HE");
    cok_character *a = member("A", 20);
    cok_character *b = member("B", 20);
    stock(0x12, 7, 0);
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(logged_times("menu: Buy View Pool Appraise Exit;") == 5);
    CHECK(word(a->record + 0xf3) == 13 && word(b->record + 0xf3) == 20);
    CHECK(game.vm.character == a->record);
    /* The pick between them was B: buy then. */
    reset("\001PBB\033E");
    a = member("A", 20);
    b = member("B", 20);
    stock(0x12, 7, 0);
    CHECK(shop(0x10) == COK_ECL_OK && word(b->record + 0xf3) == 13 && b->item_count == 1);
}

/* Show the portrait of ECL2's head 69 over body 68, as the magic shop of
 * ECL2 block 50 does (8b47), and clear the screen. */
static void show_portrait(void)
{
    CHECK(RUN(COK_ECL_SAVE, 0, 69, 1, 0xe1, 0x7e, COK_ECL_PICTURE, 0, 68, COK_ECL_SAVE, 0, 0xff,
              1, 0xe1, 0x7e) == COK_ECL_OK);
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
}

static void test_screen(void)
{
    /* The shop's first redraw has no frame; later ones do, here after Buy
     * (with the portrait again, 0 and 0, showing nothing) but not after Appraise
     * with nothing to show. */
    reset("BB\033AE");
    member("A", 20);
    stock(0x12, 7, 0);
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    first_ink = -1;
    CHECK(shop(0x10) == COK_ECL_OK && first_ink == 0 && !LOGGED("unported"));
    game.vm.mode = 1;
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    first_ink = -1;
    s.at = 0;
    cok_adventure_redraw(&game);
    scripted(&s);
    CHECK(first_ink == 1);
    /* In a 3D area the shop shows the portrait last shown (6961:05b9,
     * 06bd), here ECL2's head 69 over body 68, as the magic shop of ECL2
     * block 50 shows it first; elsewhere the small picture, and no
     * portrait. */
    reset("E");
    game.vm.file = 2;
    member("A", 0);
    show_portrait();
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    snap_key = 0;
    CHECK(shop(0x10) == COK_ECL_OK && ink(8, 5) != 0 && ink(8, 10) != 0);
    reset("E");
    game.vm.mem4b00[0xe6] = 0;
    member("A", 0);
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    snap_key = 0;
    CHECK(shop(0x10) == COK_ECL_OK && ink(8, 5) == 0 && ink(8, 10) == 0);
}

static void test_appraise(void)
{
    /* None: said, and nothing redrawn. */
    reset("AE");
    member("A", 0);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("print: No Gems or Jewelry;"));

    /* A gem of 2500 kept, the next sold, then the jewel sold: each its
     * coin less; the shop's menu after. */
    reset("AGKGSJSE");
    cok_character *a = member("A", 0);
    give(a, 4, 1);
    put_word(a->record + 0xf5, 2);
    put_word(a->record + 0xf7, 1);
    uint32_t seed = seed_for(100, 100);
    game.vm.seed = seed;
    cok_dice(&seed, 1, 100);
    uint8_t second = cok_dice(&seed, 1, 100);
    uint16_t gem = second <= 25 ? 5 : second <= 50 ? 25 : second <= 70 ? 50 : second <= 90 ? 250
                 : second <= 99 ? 500 : 2500;
    uint8_t range = cok_dice(&seed, 1, 100);
    static const struct { uint8_t top; uint16_t range, base; } jewels[] = {
        {10, 450, 50}, {20, 500, 100}, {40, 750, 150}, {50, 1250, 250},
        {70, 2500, 500}, {90, 3000, 1000}, {100, 5000, 1000},
    };
    size_t j = 0;
    while (range > jewels[j].top) ++j;
    uint16_t jewel = (uint16_t)(cok_tp_random(&seed, jewels[j].range) + jewels[j].base);
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(LOGGED("print: You have a fine collection of:;print: 2 Gems;print: 1 piece of "
                 "Jewelry;menu:   Gems  Jewelry Exit;print: The Gem is Valued at 2500 steel.;"
                 "menu: Sell Keep;shop: gem kept;"));
    CHECK(LOGGED("print: 1 Gem;"));
    CHECK(a->item_count == 2 && a->items[1][0x2e] == 0x2f && a->items[1][0x31] == 0x7a);
    CHECK(word(a->items[1] + 0x3a) == 2500 && word(a->items[1] + 0x37) == 1);
    CHECK(a->record[0x142] == 2);
    CHECK(word(a->record + 0xf3) == gem + jewel && word(a->record + 0xf5) == 0 &&
          word(a->record + 0xf7) == 0);
    CHECK(seed == game.vm.seed);

    /* Each gem's value by its d100. */
    static const struct { uint8_t roll; uint16_t value; } gems[] = {
        {1, 5}, {25, 5}, {26, 25}, {50, 25}, {51, 50}, {70, 50}, {71, 250}, {90, 250},
        {91, 500}, {99, 500}, {100, 2500},
    };
    for (size_t i = 0; i < sizeof gems / sizeof *gems; ++i) {
        reset("AGSEN");
        a = member("A", 0);
        put_word(a->record + 0xf5, 1);
        game.vm.seed = seed_for(100, gems[i].roll);
        CHECK(shop(0x10) == COK_ECL_OK);
        CHECK(word(a->record + 0xf3) + game.pool.coins[4] == gems[i].value);
    }
    /* Each jewel's range by its d100, then Random within it. */
    static const uint8_t rolls[] = {1, 10, 11, 20, 21, 40, 41, 50, 51, 70, 71, 90, 91, 100};
    for (size_t i = 0; i < sizeof rolls; ++i) {
        reset("AJSEN");
        a = member("A", 0);
        put_word(a->record + 0xf7, 1);
        game.vm.seed = seed = seed_for(100, rolls[i]);
        cok_dice(&seed, 1, 100);
        j = 0;
        while (rolls[i] > jewels[j].top) ++j;
        jewel = (uint16_t)(cok_tp_random(&seed, jewels[j].range) + jewels[j].base);
        CHECK(shop(0x10) == COK_ECL_OK && word(a->record + 0xf3) + game.pool.coins[4] == jewel);
    }
    /* Too heavy for one more: Sell alone. */
    reset("AGKE");
    a = member("A", 0);
    give(a, 4, 1);
    put_word(a->record + 0x17d, 60000);
    put_word(a->record + 0xf5, 1);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("menu: Sell;") && a->item_count == 1);

    /* Escape sells too; with no items, Keep loses it; with 16, or one
     * more too heavy, only Sell is offered. */
    reset("AG\033E");
    a = member("A", 0);
    put_word(a->record + 0xf5, 1);
    game.vm.seed = seed_for(100, 1);
    CHECK(shop(0x10) == COK_ECL_OK && word(a->record + 0xf3) == 5);
    reset("AGKE");
    a = member("A", 0);
    put_word(a->record + 0xf5, 1);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("shop: gem kept and lost;"));
    CHECK(a->item_count == 0 && word(a->record + 0xf3) == 0 && word(a->record + 0xf5) == 0);
    reset("AGKE");
    a = member("A", 0);
    for (int i = 0; i < 16; ++i) give(a, 4, 1);
    a->record[0x142] = 16; /* as the stats last counted them */
    put_word(a->record + 0xf5, 1);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("menu: Sell;") && a->item_count == 16);
    /* Home (scan code G) values a gem; Escape leaves. */
    reset("A\001GS\033E");
    a = member("A", 0);
    put_word(a->record + 0xf5, 2);
    CHECK(shop(0x10) == COK_ECL_OK && word(a->record + 0xf5) == 1);
}

/* The temple: Heal's list, then cures picked by the arrows. */
static void test_temple(void)
{
    static const char *cures = "item: Cure Blindness;item: Cure Disease;item: Cure Light Wounds;"
                               "item: Cure Serious Wounds;item: Cure Critical Wounds;item: Heal;"
                               "item: Neutralize Poison;item: Raise Dead;item: Remove Curse;"
                               "item: Stone to Flesh;";
    /* Cure Light Wounds, 1d8 once paid. Escape does nothing. */
    reset("\033H\001P\001PHY\033E");
    cok_character *a = member("A", 100);
    a->record[0x62] = 20;
    a->record[0x197] = 1;
    game.vm.seed = seed_for(8, 5);
    CHECK(temple() == COK_ECL_OK);
    CHECK(logged_times("menu: Heal View Pool Appraise Exit;") == 3);
    CHECK(LOGGED("print: A, how can we help you?;") && LOGGED(cures));
    CHECK(LOGGED("choice: Cure Light Wounds;print: Cure Light Wounds will only cost 50 steel "
                 "pieces.;menu: pay for cure ;choice: Y;shop: A pays 50 steel;print: A;print: is "
                 "cured.;"));
    CHECK(a->record[0x197] == 6 && word(a->record + 0xf3) == 50);
    /* Exactly the price will do; up (scan code H) picks a character. */
    reset("\001HH\001P\001PHY\033E");
    a = member("A", 50);
    cok_character *b = member("B", 50);
    game.vm.character = b->record;
    CHECK(temple() == COK_ECL_OK && word(a->record + 0xf3) == 0 && word(b->record + 0xf3) == 50);

    /* Cure Blindness, not blind: "anyway", then paid for nothing; not
     * enough money says so. */
    reset("HHNHYYHYY\033E");
    a = member("A", 600);
    CHECK(temple() == COK_ECL_OK);
    CHECK(LOGGED("print: A;print: is not blind.;menu: cast cure anyway: ;choice: N;"));
    CHECK(LOGGED("choice: Y;print: Cure Blindness will only cost 500 steel pieces.;"));
    CHECK(LOGGED("print: Not enough money.;") && word(a->record + 0xf3) == 100);

    /* The pool pays when the character cannot. */
    reset("PHHY\033E");
    a = member("A", 0);
    member("B", 800);
    CHECK(cok_character_add_effect(a, 0x21, 0, 0, false) != NULL);
    CHECK(temple() == COK_ECL_OK);
    CHECK(LOGGED("shop: the pool pays 500 steel;") && game.pool.coins[4] == 300);
    CHECK(cok_character_find_effect(a, 0x21) == NULL);

    /* Heal: to the maximum less 1d4, or nothing within it; the diseases
     * and 0x44 go. */
    reset("H\001P\001P\001P\001P\001PHY\033E");
    a = member("A", 3000);
    a->record[0x62] = 20;
    a->record[0x197] = 3;
    CHECK(cok_character_add_effect(a, 0x22, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(a, 0x44, 0, 0, false) != NULL);
    game.vm.seed = seed_for(4, 2);
    CHECK(temple() == COK_ECL_OK && a->record[0x197] == 18 && a->effects == NULL);
    CHECK(word(a->record + 0xf3) == 500);
    reset("H\001P\001P\001P\001P\001PHY\033E");
    a = member("A", 3000);
    a->record[0x62] = 20;
    a->record[0x197] = 19;
    game.vm.seed = seed_for(4, 2);
    CHECK(temple() == COK_ECL_OK && a->record[0x197] == 19);

    /* Raise Dead: a dead fighter of constitution 15 at 30 hit points of 20
     * at full loses a point, and its bonus, 10, as the sum by which it
     * divides is 3 x (14 - 14), 0. */
    reset("H\001H\001H\001HHY\033E");
    a = member("A", 3000);
    a->record[0x188] = 6;
    a->record[0x189] = 0;
    a->record[0x19] = 15;
    a->record[0x62] = 30;
    a->record[0x11b] = 20;
    a->record[0xfb] = 3;
    CHECK(temple() == COK_ECL_OK);
    CHECK(a->record[0x188] == 0 && a->record[0x189] == 1 && a->record[0x197] == 1);
    CHECK(a->record[0x19] == 14 && a->record[0x62] == 20 && word(a->record + 0xf3) == 250);
    /* Animated is dead enough. At 17 a cleric of level 4 loses 10 / 8; at
     * 18, with no fighter level, nothing; below 14 left, nothing. */
    static const struct { uint8_t status, con, max; } raised[] = {{1, 16, 28}, {6, 17, 29},
                                                                 {6, 18, 30}, {6, 14, 30}};
    for (size_t i = 0; i < sizeof raised / sizeof *raised; ++i) {
        reset("H\001H\001H\001HHY\033E");
        a = member("A", 3000);
        a->record[0x188] = raised[i].status;
        a->record[0x19] = raised[i].con;
        a->record[0x62] = 30;
        a->record[0x11b] = 20;
        a->record[0xf9] = 4;
        CHECK(temple() == COK_ECL_OK && a->record[0x188] == 0 && a->record[0x62] == raised[i].max);
    }
    /* An elf "cannot be raised"; paid anyway, it stays dead. */
    reset("H\001H\001H\001HHYY\033E");
    a = member("A", 3000);
    a->record[0x188] = 6;
    a->record[0x5a] = 1;
    CHECK(temple() == COK_ECL_OK && LOGGED("print: cannot be raised;"));
    CHECK(a->record[0x188] == 6 && word(a->record + 0xf3) == 250);

    /* Neutralize Poison raises the dead who are poisoned. */
    reset("H\001H\001H\001H\001HHY\033E");
    a = member("A", 600);
    a->record[0x188] = 6;
    a->record[0x189] = 0;
    CHECK(cok_character_add_effect(a, 0x37, 0, 0, false) != NULL);
    CHECK(temple() == COK_ECL_OK);
    CHECK(a->record[0x188] == 0 && a->record[0x189] == 1 && a->record[0x197] == 1);

    /* Stone to Flesh leaves a hit point. */
    reset("H\001HHY\033E");
    a = member("A", 1000);
    a->record[0x188] = 7;
    a->record[0x197] = 12;
    CHECK(temple() == COK_ECL_OK && a->record[0x188] == 0 && a->record[0x197] == 1);

    /* Remove Curse unreadies a cursed item, which stays cursed. */
    reset("H\001H\001HHY\033E");
    a = member("A", 2000);
    uint8_t *it = give(a, 0x12, 1);
    it[0x34] = 1;
    it[0x36] = 1;
    CHECK(temple() == COK_ECL_OK && LOGGED("print: has an item un-cursed;"));
    CHECK(a->items[0][0x34] == 0 && a->items[0][0x36] == 1);

    /* Leaving with money: the second text clears the first. */
    reset("PEN");
    member("A", 10);
    CHECK(temple() == COK_ECL_OK);
    CHECK(LOGGED("print: As you leave a priest says, \"Excuse me but you have left some money "
                 "here\" ;print: Do you want to go back and retrieve your money?;menu: ~Yes ~No;"));
}

/* View's Sell and Id in shops. */
static void test_sell(void)
{
    /* Half the value; "Sold!", and the steel. */
    reset("VISY\033E");
    cok_character *a = member("A", 20);
    give(a, 0x12, 15);
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(LOGGED("menu: Ready Trade Drop Halve Join Sell Id;"));
    CHECK(LOGGED("print: I'll give you 7 steel pieces for your Long Sword ;menu: Is It a Deal? ;"
                 "choice: Y;print: Sold!;shop: sold for 7 steel;"));
    CHECK(a->item_count == 0 && word(a->record + 0xf3) == 27);
    /* An NPC that can act may not sell, as it may not trade; Id still. */
    reset("VI\033\033E");
    a = member("A", 20);
    a->record[0xe7] = 0x80;
    give(a, 0x12, 15);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("menu: Ready Drop Halve Join Id;"));
    /* A count: that times the half, a word, over 20. */
    reset("VISY\033E");
    a = member("A", 0);
    give(a, 0x1e, 100)[0x39] = 3;
    CHECK(shop(0x10) == COK_ECL_OK && word(a->record + 0xf3) == 7);
    /* Too heavy for the money: the pool's, "pool." in lower case. */
    reset("VISY\033E");
    a = member("A", 0);
    give(a, 0x12, 40);
    a->record[0x10] = a->record[0x11] = 3;
    put_word(a->record + 0xf1, 40000);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("print: Overloaded.  Money will be put in pool.;"));
    CHECK(word(a->record + 0xf3) == 0 && game.pool.coins[4] == 20);

    /* Id: 100 steel to show the hidden parts; nothing hidden is charged
     * all the same; without the money, from the pool, else not. */
    reset("VIIY\033\033E");
    a = member("A", 150);
    uint8_t *it = give(a, 0x12, 15);
    it[0x30] = 0x6f;
    it[0x35] = 0x22;
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(LOGGED("print: For 100 steel pieces I'll identify your Long Sword ;"));
    CHECK(LOGGED("print: It looks like some sort of Long Sword +1 ;"));
    CHECK(a->items[0][0x35] == 0x20 && word(a->record + 0xf3) == 50);
    reset("VIIY\033\033E");
    a = member("A", 150);
    give(a, 0x12, 15);
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(LOGGED("print: I can't tell anything new about your Long Sword ;"));
    CHECK(word(a->record + 0xf3) == 50);
    reset("VIIY\033\033E");
    a = member("A", 100);
    give(a, 0x12, 15);
    CHECK(shop(0x10) == COK_ECL_OK && word(a->record + 0xf3) == 0);
    reset("VIIY\033\033E");
    a = member("A", 99);
    give(a, 0x12, 15);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("print: Not Enough Money;"));
}

/* Trade's first Escape in a shop's View: unknown on entry, set after a
 * View. */
static void test_trade_byte(void)
{
    reset("VTS\033");
    member("A", 5);
    member("B", 0);
    CHECK(shop(0x10) == COK_ECL_UNDEFINED);
    reset("V\033VTS\033\033E");
    member("A", 5);
    member("B", 0);
    CHECK(shop(0x10) == COK_ECL_OK && logged_times("menu: Select Exit;") == 1);
    /* Buy, Take, and Appraise with nothing to show keep it. */
    reset("V\033B\033\001TAVTS\033\033E");
    member("A", 5);
    member("B", 0);
    stock(0x12, 7, 0);
    CHECK(shop(0x10) == COK_ECL_OK);
    reset("BB\033\001TAVTS\033");
    member("A", 5);
    member("B", 0);
    stock(0x12, 1, 0);
    CHECK(shop(0x10) == COK_ECL_UNDEFINED);
    /* Appraise showing the gems sets it, and so does Yes to going back,
     * here after Take gave back a coin. */
    reset("AGSVTS\033\033E");
    cok_character *a = member("A", 5);
    member("B", 0);
    put_word(a->record + 0xf5, 1);
    CHECK(shop(0x10) == COK_ECL_OK && logged_times("menu: Select Exit;") == 1);
    reset("PT\r1\r\033EYVTS\033\033EN");
    member("A", 5);
    member("B", 0);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("menu: ~Yes ~No;"));
    CHECK(logged_times("menu: Select Exit;") == 1 && s.at == s.length);
    /* In the temple, Heal sets it. */
    reset("H\033VTS\033\033E");
    member("A", 5);
    member("B", 0);
    CHECK(temple() == COK_ECL_OK);
    reset("VTS\033");
    member("A", 5);
    member("B", 0);
    CHECK(temple() == COK_ECL_UNDEFINED);
}


/* What review found the tests did not pin. */
static void test_more(void)
{
    /* Appraise with 15 items still offers Keep. */
    reset("AGKE");
    cok_character *a = member("A", 0);
    for (int i = 0; i < 15; ++i) give(a, 4, 1);
    a->record[0x142] = 15;
    put_word(a->record + 0xf5, 1);
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("menu: Sell Keep;") && a->item_count == 16);

    /* A count of 1 sells at half the value, as no count does. */
    reset("VISY\033E");
    a = member("A", 0);
    give(a, 0x12, 100)[0x39] = 1;
    CHECK(shop(0x10) == COK_ECL_OK && word(a->record + 0xf3) == 50);

    /* The temple's pool pays exactly the price. */
    reset("PH\001P\001PHY\033E");
    a = member("A", 0);
    member("B", 50);
    CHECK(temple() == COK_ECL_OK && LOGGED("shop: the pool pays 50 steel;"));
    CHECK(game.pool.coins[4] == 0);

    /* Cure Disease removes a drain while curing, so its handler adds
     * nothing; Heal does not, and it comes back with 0x1f. */
    reset("H\001PHY\033E");
    a = member("A", 600);
    a->record[0x11] = 3;
    CHECK(cok_character_add_effect(a, 0x2b, 0, 0, true) != NULL);
    CHECK(temple() == COK_ECL_OK && a->effects == NULL);
    reset("H\001P\001P\001P\001P\001PHY\033E");
    a = member("A", 3000);
    a->record[0x11] = 3;
    CHECK(cok_character_add_effect(a, 0x2b, 0, 0, true) != NULL);
    CHECK(temple() == COK_ECL_OK && cok_character_find_effect(a, 0x2b) != NULL &&
          cok_character_find_effect(a, 0x1f) != NULL);

    /* Take redraws the screen, here the portrait again over its list. */
    reset("\001TE");
    game.vm.file = 2;
    member("A", 0);
    show_portrait();
    snap_key = 2;
    CHECK(shop(0x10) == COK_ECL_OK && ink(8, 5) != 0);
    reset("\001TE");
    game.vm.file = 2;
    member("A", 0);
    show_portrait();
    snap_key = 2;
    CHECK(temple() == COK_ECL_OK && ink(8, 5) != 0);

    /* Id pauses once paid. */
    unsigned unpaid, paid;
    reset("VIIN\033\033E");
    a = member("A", 150);
    give(a, 0x12, 15);
    waits = 0;
    CHECK(shop(0x10) == COK_ECL_OK);
    unpaid = waits;
    reset("VIIY\033\033E");
    a = member("A", 150);
    give(a, 0x12, 15);
    waits = 0;
    CHECK(shop(0x10) == COK_ECL_OK && LOGGED("print: I can't tell anything new about your "));
    paid = waits;
    CHECK(paid == unpaid + 1);

    /* The temple's Yes to going back sets Trade's byte. */
    reset("PT\r1\r\033EYVTS\033\033EN");
    member("A", 5);
    member("B", 0);
    CHECK(temple() == COK_ECL_OK && logged_times("menu: Select Exit;") == 1 && s.at == s.length);
}

/* Where the screens put their text, and in what colours. */
static void test_cells(void)
{
    /* Buy's frame is open: on row 16 the sides go straight down. */
    reset("B\033E");
    member("A", 20);
    stock(0x12, 7, 0);
    snap_key = 1;
    CHECK(shop(0x10) == COK_ECL_OK && ink(1, 1) != 0 && ink(20, 16) == 0 && ink(0, 16) != 0);
    CHECK(same_cells(0, 15, 16) && same_cells(39, 15, 16));
    /* Appraise: the name on row 1, the collection in white from row 7. */
    reset("AE");
    cok_character *a = member("A", 0);
    put_word(a->record + 0xf5, 2);
    snap_key = 1;
    CHECK(shop(0x10) == COK_ECL_OK);
    CHECK(ink(1, 1) == 11 && ink(1, 7) == 15 && ink(1, 9) == 15 && inked_rows(1, 2, 0x26, 6) == 0);
    /* The shop's question: light green, then white after it. */
    reset("PEN");
    member("A", 5);
    snap_key = 2;
    CHECK(shop(0x10) == COK_ECL_OK && ink(1, 0x11) == 10);
    bool white = false;
    for (int y = 0x11; y <= 0x16; ++y)
        for (int x = 1; x <= 0x26; ++x) white = white || ink(x, y) == 15;
    CHECK(white && inked_rows(1, 0x11, 0x26, 0x16) >= 4);
    /* The temple's: the second clears the first, in light green. */
    reset("PEN");
    member("A", 5);
    snap_key = 2;
    CHECK(temple() == COK_ECL_OK && ink(1, 0x11) == 10 && inked_rows(1, 0x11, 0x26, 0x16) == 2);
    /* Heal: the name and question on row 1 in white, the cures in cells
     * 2-38 from row 4, the first picked, the last on row 13 in light
     * green; the price in rows 17-22 in light green. */
    reset("HHY\033E");
    member("A", 600);
    snap_key = 1;
    CHECK(temple() == COK_ECL_OK);
    CHECK(ink(1, 1) == 15 && ink(1, 4) == 0 && ink(2, 4) == 15 && ink(2, 13) == 10 &&
          ink(2, 14) == 0 && ink(2, 3) == 0);
    reset("H\001P\001PHY\033E");
    member("A", 600);
    snap_key = 6;
    CHECK(temple() == COK_ECL_OK && ink(1, 0x11) == 10 && LOGGED("pay for cure"));
    /* Heal clears row 24 before its list shows its own menu there. */
    reset("H\033E");
    member("A", 600);
    snap_key = 1;
    CHECK(temple() == COK_ECL_OK && ink(0x26, 24) == 0);
}

int main(void)
{
    open_game();
    test_prices();
    test_buy();
    test_keys();
    test_screen();
    test_appraise();
    test_temple();
    test_sell();
    test_trade_byte();
    test_more();
    test_cells();
    cok_adventure_close(&game);
    puts("shop tests passed");
    return 0;
}
