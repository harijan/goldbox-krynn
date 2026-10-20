#include "camp.h"
#include "items.h"
#include "magic.h"
#include "sheet.h"

#include "monster.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[32768];
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    if (strcmp(kind, "item") == 0 || strcmp(kind, "list") == 0) return;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static script s;
static cok_adventure game;

/* The keyboard's flushes (1614:045c), logged as "flush;" while asked. */
static bool log_flushes;

static void flushed(cok_adventure *g, void *context)
{
    (void)g;
    if (!log_flushes) return;
    script *sc = context;
    size_t used = strlen(sc->log);
    snprintf(sc->log + used, sizeof sc->log - used, "flush;");
}

static void keys(const char *k)
{
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.log[0] = '\0';
    game.input_ended = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
}

static size_t count(const char *text, const char *what)
{
    size_t n = 0;
    for (const char *p = strstr(text, what); p != NULL; p = strstr(p + 1, what)) ++n;
    return n;
}

static void empty_party(void)
{
    cok_party_free(&game.party);
    game.vm.character = NULL;
    game.spell_target = NULL;
    game.trade_partner = NULL;
    game.vm.mem7c00[0x33e] = 0;
}

static cok_character *add(const char *name, size_t class, uint8_t level)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    for (size_t i = 0; i < 6; ++i) c->record[0x10 + 2 * i] = c->record[0x11 + 2 * i] = 12;
    c->record[0x197] = c->record[0x62] = 9;
    c->record[0x189] = 1;
    c->record[0x5a] = 6;
    c->record[0x11a] = 0xff;
    c->record[0xf9 + class] = level;
    CHECK(cok_party_add(&game.party, &cok_rules_champions, c));
    ++game.vm.mem7c00[0x33e];
    if (game.vm.character == NULL) game.vm.character = c->record;
    return c;
}

/* An item of type with name parts a, b, c (+0x2f-+0x31). */
static uint8_t *give(cok_character *c, uint8_t type, uint8_t a, uint8_t b, uint8_t d)
{
    /* Room for 32; adding may move the items, so the tests take pointers
     * to them after. */
    c->items = realloc(c->items, 32 * sizeof *c->items);
    CHECK(c->items != NULL && c->item_count < 32);
    uint8_t *item = c->items[c->item_count++];
    memset(item, 0, COK_ITEM_SIZE);
    item[0x2e] = type;
    item[0x2f] = a;
    item[0x30] = b;
    item[0x31] = d;
    return item;
}

static bool stats(cok_character *c)
{
    char error[300];
    return cok_character_stats(c, &cok_rules_champions, &game.item_types, error, sizeof error);
}

/* The first colour other than black in cell x, y of the screen. */
static uint8_t ink(int x, int y)
{
    const cok_picture *p = &game.screen;
    for (int row = 0; row < 8; ++row)
        for (int k = 0; k < 4; ++k) {
            uint8_t b = p->pixels[(size_t)(y * 8 + row) * p->units * 4 + (size_t)x * 4 + (size_t)k];
            if (b >> 4) return b >> 4;
            if (b & 15) return b & 15;
        }
    return 0;
}

/* 6346:0488. */
static void test_names(void)
{
    empty_party();
    cok_character *c = add("KAL", 0, 1);
    char name[41];
    /* Parts +0x31, +0x30, +0x2f, each with a space. */
    uint8_t *sword = give(c, 0x12, 0x6f, 0x12, 0);
    CHECK(cok_item_name(&game, sword, false, name) && strcmp(name, "Long Sword +1 ") == 0);
    CHECK(sword[0] == 14 && memcmp(sword + 1, "Long Sword +1 ", 14) == 0);
    sword[0x34] = 1;
    CHECK(cok_item_name(&game, sword, true, name) && strcmp(name, " Yes  Long Sword +1 ") == 0);
    /* Unidentified, bit 2 of +0x35 hides +0x2f; the bytes after a shorter
     * name stay as they were. */
    sword[0x35] = 4;
    CHECK(cok_item_name(&game, sword, false, name) && strcmp(name, "Long Sword ") == 0);
    CHECK(sword[0] == 11 && sword[12] == 'S' && sword[20] == ' ');
    /* While anyone has Detect Magic (5), a bonus shows "* ". */
    sword[0x34] = 0;
    sword[0x32] = 1;
    cok_character *sir = add("SIRRION", 2, 1);
    cok_character_add_effect(sir, 5, 0, 0, false);
    CHECK(cok_item_name(&game, sword, true, name) && strcmp(name, " No   * Long Sword ") == 0);
    sword[0x32] = 0xff;
    CHECK(cok_item_name(&game, sword, false, name) && strcmp(name, "Long Sword ") == 0);
    /* A count, and the plural on the part that makes it: "Arrows". */
    uint8_t *arrows = give(c, 0x1e, 0, 0, 0x1e);
    arrows[0x39] = 60;
    CHECK(cok_item_name(&game, arrows, false, name) && strcmp(name, "60 Arrows ") == 0);
    /* A count of 1 shows too, without a plural. */
    arrows[0x39] = 1;
    CHECK(cok_item_name(&game, arrows, false, name) && strcmp(name, "1 Arrow ") == 0);
    /* Arrows of "Silver" (0x5d at +0x31) put the plural on the next part. */
    arrows[0x39] = 20;
    arrows[0x30] = 0x1e;
    arrows[0x31] = 0x5d;
    CHECK(cok_item_name(&game, arrows, false, name) && strcmp(name, "20 Silver Arrows ") == 0);
    /* Of three parts the plural goes on the first unless the type is 0x37,
     * where it goes on the last shown (+0x31). */
    uint8_t *wand = give(c, 0x37, 0x60, 0x5a, 0x33);
    wand[0x39] = 2;
    CHECK(cok_item_name(&game, wand, false, name) && strcmp(name, "2 Wands of Opening ") == 0);
    wand[0x2e] = 0x38;
    CHECK(cok_item_name(&game, wand, false, name) && strcmp(name, "2 Wand of Openings ") == 0);
    /* A part past the name table fails, as the original would show other
     * data. */
    wand[0x2f] = 200;
    CHECK(!cok_item_name(&game, wand, false, name) && game.vm.status == COK_ECL_UNDEFINED);
}

/* The Items menu (546c:17f9). */
static void test_items_menu(void)
{
    empty_party();
    game.vm.mode = 4;
    cok_character *c = add("KAL", 2, 3);
    cok_character *sir = add("SIRRION", 2, 3);
    give(c, 0x12, 0, 0x12, 0);
    give(c, 0x12, 0x6f, 0x12, 0);
    uint8_t *sword = c->items[0];
    CHECK(stats(c) && stats(sir));
    bool done = false;
    /* The menu, and Ready on the first: it is readied. */
    keys("r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "menu: Ready Use Trade Drop Halve Join;choice: R;") != NULL);
    CHECK(sword[0x34] == 1 && c->slots[0] == 1);
    /* Another weapon: "already using" the first, named with its space. */
    keys("\x01Pr\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: already using Long Sword ;") != NULL);
    /* Unready a cursed one: "It's Cursed". */
    sword[0x36] = 1;
    keys("r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: It's Cursed;") != NULL && sword[0x34] == 1);
    sword[0x36] = 0;
    keys("r\x1b");
    cok_items(&game, &done);
    CHECK(sword[0x34] == 0);
    /* Two hands: a bow takes both, with a shield readied; then wrong
     * class, wrong order of magic for a scroll, kender only. */
    give(c, 0x25, 0, 0x25, 0)[0x34] = 1;
    give(c, 0x19, 0, 0x19, 0);
    CHECK(stats(c));
    keys("\x01P\x01P\x01Pr\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Your hands are full!;") != NULL);
    c->record[0x11a] = 0;
    keys("r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Wrong Class;") != NULL);
    c->record[0x11a] = 0xff;
    give(c, 0x27, 0, 0, 0x27)[0x35] = 0x10;
    uint8_t *scroll = c->items[4];
    c->record[0x5e] = 1;
    keys("\x01Hr\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Wrong Magical Order;") != NULL);
    c->record[0x5e] = 2;
    keys("\x01Hr\x1b");
    cok_items(&game, &done);
    CHECK(scroll[0x34] == 1);
    give(c, 0x43, 0, 0, 0x43);
    uint8_t *hoopak = c->items[5];
    keys("\x01Hr\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Usable Only By Kender;") != NULL && hoopak[0x34] == 0);

    /* Drop: readied, it must be unreadied; a scroll marked to scribe asks;
     * otherwise "Your NAME will be gone forever" and Yes drops it. */
    keys("\x01P\x01P" "d\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Must be unreadied;") != NULL && c->item_count == 6);
    keys("\x01H" "d" "y\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Your Hoopak will be gone forever;menu: Drop It? ;choice: Y;") !=
          NULL);
    CHECK(c->item_count == 5);
    scroll = c->items[4];
    scroll[0x34] = 0;
    scroll[0x3c] = 0x80 | 0x12;
    keys("\x01H" "dn\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print:  was going to scribe from that scroll;"
                        "menu: is it Okay to lose it? ;choice: N;") != NULL);
    CHECK(c->item_count == 5);

    /* Halve: a copy with half, unreadied, after it; 1 cannot. */
    empty_party();
    c = add("KAL", 2, 3);
    sir = add("SIRRION", 2, 3);
    uint8_t *arrows = give(c, 0x1e, 0, 0, 0x1e);
    arrows[0x39] = 5;
    arrows[0x34] = 1;
    CHECK(stats(c) && stats(sir));
    keys("h\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 2 && c->items[0][0x39] == 3 && c->items[1][0x39] == 2);
    CHECK(c->items[0][0x34] == 1 && c->items[1][0x34] == 0);
    c->items[1][0x39] = 1;
    keys("\x01Ph\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Can't halve that;") != NULL);
    /* Join: the rest joins the first, whatever its +0x3d; past 255 the
     * first gets 255 and the other keeps the rest. */
    c->items[0][0x39] = 200;
    c->items[1][0x39] = 100;
    c->items[1][0x3d] = 9;
    keys("j\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 2 && c->items[0][0x39] == 255 && c->items[1][0x39] == 45);
    c->items[0][0x39] = 10;
    c->items[1][0x3c] = 2;
    keys("j\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 2);
    c->items[1][0x3c] = 1;
    keys("j\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 1 && c->items[0][0x39] == 55);

    /* Trade: "Trade with Whom?" from View's partner; the item goes to the
     * end of the receiver's items. 16 items are too many. */
    c->items[0][0x34] = 0;
    game.trade_partner = c->record;
    keys("t\x01PS\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "menu: Select Exit;") != NULL);
    CHECK(c->item_count == 0 && sir->item_count == 1 && sir->items[0][0x39] == 55);
    CHECK(game.trade_partner == sir->record);
    for (int i = 0; i < 15; ++i) give(sir, 0x12, 0, 0x12, 0);
    give(c, 0x12, 0, 0x12, 0);
    CHECK(stats(c));
    keys("tS\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Overloaded;") != NULL && sir->item_count == 16);
}

/* Use (546c:24d7). */
static void test_use(void)
{
    empty_party();
    game.vm.mode = 4;
    cok_character *c = add("KAL", 2, 3);
    /* A wand of Detect Magic with 2 charges: used up after two. */
    uint8_t *wand = give(c, 0x2b, 0, 0, 0x33);
    wand[0x3d] = 0x05;
    wand[0x3c] = 2;
    wand[0x34] = 1;
    CHECK(stats(c));
    bool done = false;
    keys("u\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: KAL;print: uses an item;print: Wand ;print: KAL;print: is affected;")
          != NULL);
    CHECK(!done && wand[0x3c] == 1 && cok_character_find_effect(c, 5) != NULL);
    keys("u\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 0);
    /* Unreadied: "Must be Readied". With no charges at all it lasts; a
     * stack loses one of the stack. */
    wand = give(c, 0x2b, 0, 0, 0x33);
    wand[0x3d] = 0x05;
    CHECK(stats(c));
    keys("u\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Must be Readied;") != NULL);
    wand[0x34] = 1;
    keys("u\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 1);
    wand[0x3c] = 1;
    wand[0x39] = 3;
    keys("u\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 1 && wand[0x39] == 2 && wand[0x3c] == 1);
    /* +0x3d of 0x80 is spell 0: nothing, but DS:711d stays set, so that
     * a spell cast from memory afterwards counts as an item's: no "casts"
     * and nothing forgotten. */
    wand[0x3d] = 0x80;
    keys("u\x1b");
    cok_items(&game, &done);
    CHECK(game.effects.rolls.item == 1);
    c->record[0x1e] = 0x13;
    c->record[0xfe] = 3;
    c->record[0x13] = 16;
    keys("c\re");
    bool interrupted = false;
    game.vm.mode = 2;
    cok_magic(&game, &interrupted);
    CHECK(strstr(s.log, "print: casts;") == NULL && c->record[0x1e] == 0x13);
    CHECK(cok_character_find_effect(c, 0x11) != NULL);
    game.effects.rolls.item = 0;
    game.vm.mode = 4;
    /* A scroll: a fighter cannot read it, "oops!"; a thief above level 9
     * reads it on a d100 up to 75; a magic-user always. */
    c->record[0xfe] = 0;
    c->item_count = 0;
    uint8_t *scroll = give(c, 0x27, 0, 0, 0x27);
    scroll[0x3c] = 0x13;
    scroll[0x2f] = 100;
    scroll[0x34] = 1;
    scroll[0x35] = 0x20;
    c->record[0x5e] = 1;
    CHECK(stats(c));
    keys("u\r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: KAL;print: oops!;") != NULL && scroll[0x3c] == 0x13);
    c->record[0xfe] = 1;
    keys("u\r\x1b");
    cok_items(&game, &done);
    /* Used below 100, the scroll is gone. */
    CHECK(c->item_count == 0);

    /* Dispel Magic from a wand after a Spiritual Hammer: the hammer's
     * effect 0x17 goes, and its handler frees the hammer, before the
     * wand; the wand, whose record the original holds, still loses its
     * charge, and with none left is gone. */
    for (uint8_t charges = 2; charges >= 1; --charges) {
        empty_party();
        c = add("KAL", 0, 3);
        uint8_t *hammer = give(c, 6, 0, 6, 0x79);
        hammer[0x32] = 1;
        hammer[0x3d] = 0x17;
        hammer[0x3e] = 0x80;
        CHECK(cok_character_add_effect(c, 0x17, 30, 3, true) != NULL);
        wand = give(c, 0x2b, 0, 0, 0x33);
        wand[0x3d] = 0x29;
        wand[0x3c] = charges;
        wand[0x34] = 1;
        CHECK(stats(c));
        game.vm.seed = 1;
        keys("\x01Pus\x1b\x1b");
        cok_items(&game, &done);
        CHECK(game.vm.status == COK_ECL_OK && cok_character_find_effect(c, 0x17) == NULL);
        CHECK(strstr(s.log, "print: KAL;print: is affected;") != NULL);
        CHECK(c->item_count == charges - 1u && c->held == 0);
        if (charges == 2) CHECK(c->items[0][0x2e] == 0x2b && c->items[0][0x3c] == 1);
        for (size_t slot = 0; slot < COK_ITEM_SLOTS; ++slot) CHECK(c->slots[slot] <= c->item_count);
    }
}

/* The sheet (546c:00a3). */
static void test_sheet(void)
{
    empty_party();
    game.vm.mode = 4;
    cok_character *c = add("KAL", 2, 3);
    uint8_t *r = c->record;
    r[0x5b] = 13;  /* Fighter/Mage */
    r[0xfe] = 2;
    r[0x5e] = 2;   /* Red */
    r[0x10] = r[0x11] = 18;
    r[0x1c] = 100;
    r[0x1d] = 100;
    r[0x113] = 61; /* AC -1 */
    r[0xf3] = 3;   /* steel */
    keys("");
    CHECK(cok_sheet_draw(&game));
    CHECK(strstr(s.log, "print: KAL;print: Fighter/Red Mage;") != NULL);
    /* Exceptional strength 100 shows as "(00)" at column 7, white names,
     * light green values. */
    CHECK(ink(7, 9) == 10 && ink(12, 9) == 0);
    CHECK(ink(1, 3) == 15 && ink(27, 1) == 10 && ink(20, 1) == 15);
    /* Order 0 inserts just a space; a Knight of the Rose; and with no
     * "Mage" in the class, the search runs past the end. */
    r[0x5e] = 0;
    CHECK(cok_sheet_draw(&game) && strstr(s.log, "print: Fighter/ Mage;") != NULL);
    memset(r + 0xf9, 0, 8);
    r[0x100] = 2;
    r[0x5b] = 7;
    r[0x5c] = 3;
    keys("");
    CHECK(cok_sheet_draw(&game) && strstr(s.log, "print: Knight of the Rose;") != NULL);
    /* A cleric: its deity first. A score below the base gets "*". */
    r[0x100] = 0;
    r[0xf9] = 1;
    r[0x5d] = 4;
    r[0x5b] = 0;
    r[0x13] = 11;
    keys("");
    CHECK(cok_sheet_draw(&game) && strstr(s.log, "print: Mishakal Cleric;") != NULL);
    CHECK(ink(12, 10) == 10 && ink(12, 11) == 0);
    /* The money, ending at column 37; the armour class right after the
     * label, ending at column 17 (60 - 61 = -1 at 16). */
    CHECK(ink(20, 9) == 10 && ink(37, 9) == 10 && ink(38, 9) == 0);
    CHECK(ink(16, 17) == 10 && ink(17, 17) == 10 && ink(18, 17) == 0);
    /* The age is a word: an elf of 350 shows "350 years", columns 8-16 of
     * row 3, where its low byte alone would end at 15. */
    r[0x60] = 350 & 0xff;
    r[0x61] = 350 >> 8;
    keys("");
    CHECK(cok_sheet_draw(&game) && ink(16, 3) == 15 && ink(17, 3) == 0);
    r[0x60] = 94;
    r[0x61] = 0;
    keys("");
    CHECK(cok_sheet_draw(&game) && ink(15, 3) == 15 && ink(16, 3) == 0);
    /* The money shows every kind but silver (+0xeb), copper to jewelry
     * (+0xed-+0xf8), a row each from row 9, the amounts ending at column
     * 37: six rows, 9-14, and none on row 15. */
    r[0xeb] = 7;
    for (unsigned k = 1; k <= 6; ++k) r[0xeb + 2 * k] = (uint8_t)k;
    keys("");
    CHECK(cok_sheet_draw(&game));
    for (int row = 9; row <= 14; ++row) CHECK(ink(37, row) == 10);
    CHECK(ink(37, 15) == 0 && ink(20, 15) == 0);
}

/* View (546c:0d74) and money. */
static void test_view(void)
{
    empty_party();
    game.vm.mode = 4;
    cok_character *c = add("KAL", 2, 3);
    cok_character *sir = add("SIRRION", 2, 3);
    c->record[0xf1] = 30; /* platinum */
    give(c, 0x12, 0, 0x12, 0);
    CHECK(stats(c) && stats(sir));
    bool done;
    /* The menu: Items, Trade and Drop; PgUp (scan 0x49, 'I') is Items. */
    keys("\x01I\x1b\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "menu: Items Trade Drop Exit;menu: Ready Use Trade Drop Halve Join;") !=
          NULL);
    /* An NPC that can act may not trade. */
    c->record[0xe7] = 0x80;
    keys("\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "menu: Items Drop Exit;") != NULL);
    c->record[0xe7] = 0;
    /* Drop: the coins listed, an amount past what it has is all it has,
     * Backspace takes a digit; Escape at the list ends. */
    keys("d\r" "9" "9" "\b" "\r" "\x1b\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "menu: How much platinum will you drop? ;input: 3;") != NULL);
    CHECK(c->record[0xf1] == 27);
    /* Backspace clears the cell after the last digit: of "27", all there
     * is, made from 99, the 7 stays on the screen. */
    keys("d\r" "99" "\b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(ink(33, 24) == 15 && ink(34, 24) == 15 && ink(35, 24) == 0);
    /* Trade: Escape at the first coin list reads a byte the original
     * leaves unset. */
    game.trade_partner = NULL;
    keys("t" "\x01PS" "\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "546c:2f9b") != NULL);
    /* As the camp picture leaves it, 0: Escape asks for a partner again
     * (the pick and its move, the coin list, which logs no menu, the pick
     * again). */
    keys("t" "\x01PS" "\x1b" "E" "\x1b");
    cok_sheet(&game, COK_SHEET_STALE_ZERO, &done);
    CHECK(game.vm.status == COK_ECL_OK && !game.input_ended && s.at == s.length);
    CHECK(count(s.log, "menu: Select Exit;") == 3 && count(s.log, "menu: Items Trade Drop Exit;") == 2);
    /* Nonzero, Trade ends: back to View's menu. */
    keys("t" "\x01PS" "\x1b" "\x1b");
    cok_sheet(&game, COK_SHEET_STALE_SET, &done);
    CHECK(game.vm.status == COK_ECL_OK && !game.input_ended && s.at == s.length);
    CHECK(count(s.log, "menu: Select Exit;") == 2 && count(s.log, "menu: Items Trade Drop Exit;") == 2);
    /* So after anything chosen before in the same View, even unknown on
     * entry: Drop, Items, or Trade left at its pick. */
    const char *const before[] = {"d\x1b" "t\x01PS\x1b\x1b", "i\x1b" "t\x01PS\x1b\x1b",
                                  "tE" "t\x01PS\x1b\x1b"};
    for (size_t i = 0; i < 3; ++i) {
        keys(before[i]);
        cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
        CHECK(game.vm.status == COK_ECL_OK && !game.input_ended && s.at == s.length);
        CHECK(count(s.log, "menu: Items Trade Drop Exit;") == 3);
    }
    /* After an amount, Escape asks for another partner; with no money
     * left, Trade ends. */
    keys("t" "\x01PS" "\r" "20\r" "\x1b" "E" "\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(game.vm.status == COK_ECL_OK && !game.input_ended && s.at == s.length);
    CHECK(sir->record[0xf1] == 20 && c->record[0xf1] == 7);
    CHECK(strstr(s.log, "menu: How much platinum will you trade? ;input: 20;") != NULL);
    /* More than the partner can carry: "Overloaded", the pause, and the
     * keyboard's flush (58e7:0506). */
    sir->record[0x17d] = 0xf0;
    sir->record[0x17e] = 0xff;
    keys("t" "\x01PS" "\r" "5\r" "\x1b" "E" "\x1b");
    log_flushes = true;
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    log_flushes = false;
    /* Then the coin list's flush (the list is not logged here). */
    CHECK(c->record[0xf1] == 7 &&
          strstr(s.log, "print: Overloaded;flush;flush;menu: Select Exit;") != NULL);
}

/* Death Knights' View and items menu (dkk 580d:0c66, 0ffa), on a game of
 * its own and records of its layout. */

/* A member of Death Knights' layout: class with level, active and okay,
 * scores 12, 9 hit points, any class's items. */
static cok_character *dk_add(const char *name, uint8_t class, uint8_t level)
{
    const cok_character_layout *L = cok_rules_death_knights.character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = (uint8_t)strlen(name);
    memcpy(r + 1, name, strlen(name));
    for (int i = 0; i < 6; ++i) {
        cok_field_set8_at(L, r, COK_FIELD_SCORE, i, 12);
        cok_field_set8_at(L, r, COK_FIELD_SCORE_BASE, i, 12);
    }
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_CLASS, class);
    cok_field_set8(L, r, COK_FIELD_RACE, 6);
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, class, level);
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 9);
    cok_field_set8(L, r, COK_FIELD_MAX_HIT_POINTS, 9);
    cok_field_set8(L, r, COK_FIELD_USABLE_CLASSES, 0xff);
    CHECK(cok_party_add(&game.party, &cok_rules_death_knights, c));
    cok_ecl_add_var(&game.vm, COK_VAR_PARTY_SIZE, 1);
    if (game.vm.character == NULL) game.vm.character = c->record;
    return c;
}

static bool dk_stats(cok_character *c)
{
    char error[300];
    return cok_character_stats(c, &cok_rules_death_knights, &game.item_types, error, sizeof error);
}

static void dk_empty(void)
{
    cok_party_free(&game.party);
    cok_pool_free(&game.pool);
    game.vm.character = NULL;
    game.trade_partner = NULL;
    cok_ecl_set_var(&game.vm, COK_VAR_PARTY_SIZE, 0);
    game.selected = 1;
}

static void test_dk_items(void)
{
    const cok_character_layout *L = cok_rules_death_knights.character;
    dk_empty();
    game.vm.mode = 2;
    cok_character *c = dk_add("KAL", 2, 3);
    cok_character *sir = dk_add("SIRRION", 2, 3);
    give(c, 0x12, 0, 0x12, 0);
    give(c, 0x12, 0x6f, 0x12, 0);
    uint8_t *sword = c->items[0];
    CHECK(dk_stats(c) && dk_stats(sir));
    bool done = false;
    /* By index: Ready's R, from the first; Escape is Exit. The readied
     * column a space shorter than Champions' (DS:2c02). */
    keys("r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "menu: Ready Use Trade Drop Halve Join Exit;choice: R;") != NULL);
    CHECK(sword[0x34] == 1 && memcmp(sword, "\x10 Yes Long Sword ", 17) == 0);
    CHECK(memcmp(c->items[1] + 1, " No  ", 5) == 0 && c->items[1][6] != ' ');
    /* Enter is the selection the menu keeps: Ready, from the first. */
    keys("\r\x1b");
    cok_items(&game, &done);
    CHECK(sword[0x34] == 0 && !game.input_ended && s.at == s.length);
    /* Exit, past the menu's last 8 (its own index 9), leaves too. */
    keys("e");
    cok_items(&game, &done);
    CHECK(!game.input_ended && s.at == s.length && game.vm.status == COK_ECL_OK);
    /* A scroll of another order: a thief of level 1-9 is told so; of 10
     * readies it (580d:1717); with no thief level, the order's word. */
    give(c, 0x27, 0, 0, 0x27)[0x35] = 0x10;
    uint8_t *scroll = c->items[2];
    cok_field_set8(L, c->record, COK_FIELD_MAGIC_ORDER, 1);
    CHECK(dk_stats(c));
    keys("\x01P\x01Pr\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Wrong Magical Order;") != NULL && scroll[0x34] == 0);
    cok_field_set8_at(L, c->record, COK_FIELD_CLASS_LEVEL, 6, 9);
    keys("\x01P\x01Pr\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Thief Level Too Low;") != NULL && scroll[0x34] == 0);
    cok_field_set8_at(L, c->record, COK_FIELD_CLASS_LEVEL, 6, 10);
    keys("\x01P\x01Pr\x1b");
    cok_items(&game, &done);
    CHECK(scroll[0x34] == 1);
    /* Use of one not readied: "Must be readied". */
    keys("u\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Must be readied;") != NULL);
    /* Drop: "Lose it? " from No: Enter keeps it, Y drops it. */
    keys("\x01P" "d\r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: Your Long Sword +1 will be gone forever;menu: Lose it? ;choice: N;") !=
          NULL && c->item_count == 3);
    keys("\x01P" "dy\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 2);
    /* Trade: from the last partner, or with none the selected (Select at
     * once trades it to itself: to the end of its items). */
    game.trade_partner = NULL;
    uint8_t first = c->items[0][0x2e], second = c->items[1][0x2e];
    CHECK(c->item_count == 2 && first != second);
    keys("tS\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 2 && c->items[1][0x2e] == first && c->items[0][0x2e] == second &&
          game.trade_partner == c->record);
    /* To another: the item to the end of the receiver's. */
    game.trade_partner = NULL;
    keys("\x01Pt\x01PS\x1b");
    cok_items(&game, &done);
    CHECK(c->item_count == 1 && sir->item_count == 1 && game.trade_partner == sir->record);
    /* The vault (mode 7): Deposit for Drop; the item to the end of the
     * pool, said. */
    game.vm.mode = 7;
    uint8_t loot[COK_ITEM_SIZE] = {0};
    loot[0x2e] = loot[0x31] = 0x11;
    CHECK(cok_pool_insert(&game.pool, 0, loot));
    sword = give(c, 0x12, 0, 0x12, 0);
    sword[0x34] = 0;
    CHECK(dk_stats(c));
    keys("\x01Pd\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "menu: Ready Trade Deposit Halve Join Exit;") != NULL);
    CHECK(strstr(s.log, "print: Your Long Sword will be left in the vault;") != NULL);
    CHECK(c->item_count == 1 && game.pool.item_count == 2 && game.pool.items[1][0x2e] == 0x12);
    /* Shops (mode 1): Sell's price is half the value times any count (no
     * 20th), the question from Yes; Id pays from the steel and the pool's. */
    game.vm.mode = 1;
    uint8_t *arrows = give(c, 0x1e, 0, 0, 0x1e);
    arrows[0x39] = 3;
    arrows[0x3a] = 10;
    CHECK(dk_stats(c));
    keys("\x01Ps\r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "menu: Ready Trade Drop Halve Join Sell Id Exit;") != NULL);
    CHECK(strstr(s.log, "print: I'll give you 15 steel pieces for your 3 Arrows ;"
                        "menu: is it a deal? ;choice: Y;print: sold!;") != NULL);
    CHECK(cok_field_u16_at(L, c->record, COK_FIELD_COINS, 4) == 15 && c->item_count == 1);
    c->items[0][0x35] = 1;
    game.pool.coins[4] = 90;
    keys("i\r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: for 100 steel pieces i'll identify your ") != NULL &&
          strstr(s.log, "print: it looks like some sort of ") != NULL);
    CHECK(cok_field_u16_at(L, c->record, COK_FIELD_COINS, 4) == 0 && game.pool.coins[4] == 5 &&
          (c->items[0][0x35] & 7) == 0);
    keys("i\r\x1b");
    cok_items(&game, &done);
    CHECK(strstr(s.log, "print: not enough money.;") != NULL);
    game.pool.coins[4] = 0;
}

static void test_dk_view(void)
{
    const cok_rules *R = &cok_rules_death_knights;
    const cok_character_layout *L = R->character;
    dk_empty();
    game.vm.mode = 2;
    cok_character *c = dk_add("SIR", 3, 4); /* a paladin of level 4 */
    cok_character *k = dk_add("KAL", 2, 3);
    cok_field_set8(L, k->record, COK_FIELD_HIT_POINTS, 2);
    cok_field_set16_at(L, c->record, COK_FIELD_COINS, 5, 7); /* gems */
    give(c, 0x12, 0, 0x12, 0);
    CHECK(dk_stats(c) && dk_stats(k));
    bool done;
    /* By index: Items Trade Drop Heal Exit; Escape is Exit. */
    keys("\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "menu: Items Trade Drop Heal Exit;") != NULL && !game.input_ended);
    /* Heal: from the party's first, Down to KAL: healed by twice the
     * level, said; the paladin gets effect 0x6d for a day, and Heal goes. */
    keys("h\x01PS\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "print: KAL feels better;") != NULL);
    CHECK(cok_field_u8(L, k->record, COK_FIELD_HIT_POINTS) == 9);
    cok_effect *e = cok_character_find_effect(c, 0x6d);
    CHECK(e != NULL && e->duration == 0x5a0);
    CHECK(strstr(s.log, "menu: Items Trade Drop Exit;") != NULL);
    /* Cure with +0x25 set: a pick, nothing more. */
    cok_field_set8(L, c->record, COK_FIELD_KNIGHT_TIER, 1);
    keys("cS\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "menu: Items Trade Drop Cure Exit;") != NULL &&
          strstr(s.log, "menu: Cure whom?  ") == NULL && !game.input_ended && s.at == s.length);
    /* Drop in the vault is Deposit: the gems into the pool, "will you
     * deposit? ". */
    game.vm.mode = 7;
    keys("d\r3\r\x1b\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "menu: Items Trade Deposit Cure Exit;") != NULL &&
          strstr(s.log, "menu: How much Gems will you deposit? ;input: 3;") != NULL);
    CHECK(game.pool.coins[5] == 3 && cok_field_u16_at(L, c->record, COK_FIELD_COINS, 5) == 4);
    game.pool.coins[5] = 0;
    /* Trade: Escape at the first coin list reads a byte the original leaves
     * unset. */
    game.vm.mode = 2;
    game.trade_partner = NULL;
    keys("t\x01PS\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "580d:26c0") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* After an amount: trade to?, the gems moved. */
    keys("t\x01PS\r2\r\x1b\x1b");
    cok_sheet(&game, COK_SHEET_STALE_UNKNOWN, &done);
    CHECK(strstr(s.log, "menu: How much Gems will you trade? ;input: 2;") != NULL);
    CHECK(cok_field_u16_at(L, k->record, COK_FIELD_COINS, 5) == 2);
}

static void test_death_knights(void)
{
    struct stat st;
    if (stat("DKKAssets/START.EXE", &st) != 0) {
        puts("items: DKKAssets not found; Death Knights' tests skipped");
        return;
    }
    cok_adventure_close(&game);
    cok_keyboard k = {scripted, &s, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .flush = flushed, .context = &s};
    CHECK(cok_adventure_open(&game, &cok_rules_death_knights, "DKKAssets", &k, &hooks));
    test_dk_items();
    test_dk_view();
}

int main(void)
{
    cok_keyboard k = {scripted, &s, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line, .flush = flushed, .context = &s};
    CHECK(cok_adventure_open(&game, &cok_rules_champions, "COKAssets", &k, &hooks));
    game.vm.mem4b00[0xe6] = 1;
    test_names();
    test_items_menu();
    test_use();
    test_sheet();
    test_view();
    test_death_knights();
    cok_adventure_close(&game);
    puts("items tests passed");
    return 0;
}
