#include "camp.h"
#include "items.h"
#include "magic.h"
#include "sheet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    CHECK(cok_party_add(&game.party, c));
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
    return cok_character_stats(c, &game.item_types, error, sizeof error);
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

/* The name strings match the original's data segment in
 * build/START_FULL.EXE, if it has been built. */
static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("items: build/START_FULL.EXE not built; names not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    for (size_t k = 0; k < cok_name_table.size; ++k)
        CHECK(exe[ds + cok_name_table.offset + k] == cok_name_table.bytes[k]);
    /* The hit points of a class's levels top out at DS:3903. */
    static const uint8_t top[8] = {10, 15, 10, 10, 11, 12, 11, 10};
    CHECK(memcmp(exe + ds + 0x3903, top, sizeof top) == 0);
    char name[256];
    CHECK(cok_ds_string(0x0f5a, name) && strcmp(name, "Cleric") == 0);
    CHECK(cok_ds_string(0x1390 + 21 * 123, name) && strcmp(name, "Jewelry") == 0);
    /* Status 9 reads the first item name part, as the original does. */
    CHECK(cok_ds_string(0x1330 + 13 * 9, name) && strcmp(name, "Battle Axe") == 0);
    CHECK(!cok_ds_string(0x1390 + 21 * 126, name) && !cok_ds_string(0x0f59, name));
    /* In the middle of the table: a NUL between names is an empty string,
     * but "Cleric"'s C (67) as a length takes in the NULs after it, which
     * the original would draw. */
    CHECK(cok_ds_string(0x0f5a + 20, name) && name[0] == '\0');
    CHECK(!cok_ds_string(0x0f5b, name));
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
}

int main(void)
{
    cok_keyboard k = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &k, &hooks));
    game.vm.mem4b00[0xe6] = 1;
    test_tables();
    test_names();
    test_items_menu();
    test_use();
    test_sheet();
    test_view();
    cok_adventure_close(&game);
    puts("items tests passed");
    return 0;
}
