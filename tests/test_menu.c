#include "menu.h"
#include "screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* 64 glyphs; glyph g has its first row 0x81 and the rest g, except space
 * (32), which is blank. */
static cok_font numbered_font(void)
{
    uint8_t data[64 * 8];
    for (size_t i = 0; i < sizeof data; ++i) data[i] = i % 8 == 0 ? 0x81 : (uint8_t)(i / 8);
    memset(data + 32 * 8, 0, 8);
    cok_font font = {0};
    CHECK(cok_font_load(&font, data, sizeof data) == COK_PICTURE_OK);
    return font;
}

static cok_picture screen(void)
{
    cok_picture picture = {0};
    CHECK(cok_picture_create(&picture, 40, 200, 1, 0) == COK_PICTURE_OK);
    return picture;
}

/* Colours of cell (x, y) as drawn by the numbered font, from its first
 * row: the first pixel is set and the second clear. */
static unsigned cell_fg(const cok_picture *p, int x, int y)
{
    return p->pixels[(size_t)y * 8 * p->units * 4 + (size_t)x * 4] >> 4;
}

static unsigned cell_bg(const cok_picture *p, int x, int y)
{
    return p->pixels[(size_t)y * 8 * p->units * 4 + (size_t)x * 4] & 15u;
}

/* The glyph in a cell, from its second row. */
static unsigned cell_glyph(const cok_picture *p, int x, int y)
{
    unsigned glyph = 0;
    const uint8_t *row = p->pixels + ((size_t)y * 8 + 1) * p->units * 4 + (size_t)x * 4;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned pixel = ((unsigned)row[i / 2] >> (i % 2 == 0 ? 4 : 0)) & 15u;
        if (pixel == cell_fg(p, x, y) && pixel != cell_bg(p, x, y)) glyph |= 0x80u >> i;
    }
    return glyph;
}

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static cok_keyboard keyboard(script *s, const char *keys, size_t length)
{
    *s = (script){keys, 0, length};
    return (cok_keyboard){scripted, s, NULL, NULL};
}

/* A keyboard that counts its flushes and drops the keys left, as
 * 1614:045c does. */
static unsigned flushes;

static void flush_rest(void *context)
{
    script *s = context;
    s->at = s->length;
    ++flushes;
}

/* 67b5:03e2 flushes the keyboard once it has its key, not when input
 * ends; keys typed ahead are lost. */
static void test_flush(void)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    script s = {"\x01\x4dLx", 0, 4};
    cok_keyboard k = {scripted, &s, flush_rest, NULL};
    uint8_t selected = 1;
    bool special;
    flushes = 0;
    CHECK(cok_menu_read(&p, &font, "", "Move Area Look", 13, 15, 10, &selected, &k, &special) ==
          'L');
    CHECK(flushes == 1 && s.at == s.length);
    CHECK(cok_menu_read(&p, &font, "", "Move Area Look", 13, 15, 10, &selected, &k, &special) ==
          -1);
    CHECK(flushes == 1);
    /* An extended key is taken whole, then the rest goes. */
    s = (script){"\x01\x48\x01\x50", 0, 4};
    CHECK(cok_menu_read(&p, &font, "", "Move Area Look", 13, 15, 10, &selected, &k, &special) ==
              0x48 &&
          special && flushes == 2 && s.at == s.length);
    cok_picture_free(&p);
    cok_font_free(&font);
}

static void test_parse(void)
{
    cok_menu menu;
    cok_menu_parse(&menu, "~YES ~NO");
    CHECK(strcmp(menu.text, "Yes No") == 0);
    CHECK(strcmp(menu.keys, "YN") == 0);
    CHECK(menu.count == 2);
    CHECK(menu.first[1] == 1 && menu.last[1] == 3 && menu.first[2] == 5 && menu.last[2] == 6);

    /* Digits move up to P-Y unless marked, starting items of their own; a
     * mark mid-word starts its item there. */
    cok_menu_parse(&menu, "~A12 B~3 C~d");
    CHECK(strcmp(menu.text, "AQR b3 cD") == 0);
    CHECK(strcmp(menu.keys, "A3D") == 0);
    CHECK(menu.count == 5);
    CHECK(menu.first[2] == 2 && menu.last[1] == 0 && menu.first[4] == 6 && menu.last[3] == 4);
    CHECK(menu.first[5] == 9 && menu.last[5] == 9);

    /* Empty text still makes one item, 0 to 0. */
    cok_menu_parse(&menu, "");
    CHECK(menu.count == 1 && menu.first[1] == 0 && menu.last[1] == 0 && menu.keys[0] == '\0');

    /* A trailing mark takes the stale byte past the end, as Delete leaves it. */
    cok_menu_parse(&menu, "X~");
    CHECK(strcmp(menu.text, "x") == 0 && strcmp(menu.keys, "~") == 0);

    /* Shown text stops at 40 characters; hotkeys past it still count. */
    char long_items[80];
    snprintf(long_items, sizeof long_items, "~%s ~Z", "abcdefghijklmnopqrstuvwxyzabcdefghijklmn");
    cok_menu_parse(&menu, long_items);
    CHECK(strlen(menu.text) == 40 && strcmp(menu.keys, "AZ") == 0 && menu.count == 1);

    cok_menu_layout(&menu, "Select Next");
    CHECK(menu.count == 2 && menu.first[2] == 8 && menu.last[1] == 6);
}

static void test_draw(void)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    cok_picture_fill(&p, 0, 24 * 8, 40, 8, 7);
    cok_menu menu;
    cok_menu_parse(&menu, "~YES ~NO");
    cok_menu_draw(&p, &font, &menu, 2, 2, 15, 10);
    CHECK(cell_glyph(&p, 2, 24) == 25 && cell_fg(&p, 2, 24) == 15 && cell_bg(&p, 2, 24) == 0);
    CHECK(cell_glyph(&p, 3, 24) == 5 && cell_fg(&p, 3, 24) == 10);
    /* The selected item is inverted, the space after the first item is not. */
    CHECK(cell_bg(&p, 6, 24) == 15 && cell_fg(&p, 6, 24) == 0);
    CHECK(cell_bg(&p, 7, 24) == 15 && cell_fg(&p, 7, 24) == 0);
    CHECK(cell_bg(&p, 5, 24) == 0 && cell_fg(&p, 5, 24) == 0);
    /* The rest of the row is blanked; cells before x are not. */
    CHECK(cell_bg(&p, 8, 24) == 0 && cell_bg(&p, 39, 24) == 0 && cell_bg(&p, 0, 24) == 7);
    /* Highlight 0 shows no selection. */
    cok_menu_draw(&p, &font, &menu, 2, 2, 0, 10);
    CHECK(cell_bg(&p, 6, 24) == 0);
    cok_picture_free(&p);
    cok_font_free(&font);
}

static int specials[8];
static size_t special_count;

static void on_special(uint8_t scan, void *context)
{
    (void)context;
    if (special_count < 8) specials[special_count++] = scan;
}

static int run_menu(const char *items, const char *keys, size_t length, bool enter_returns,
                    uint8_t *selected)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    script s;
    cok_keyboard k = keyboard(&s, keys, length);
    cok_menu_hooks hooks = {on_special, NULL};
    special_count = 0;
    int result = cok_menu_horizontal(&p, &font, "", items, 13, 15, 10, enter_returns, selected,
                                     &k, &hooks);
    cok_picture_free(&p);
    cok_font_free(&font);
    return result;
}

static void test_horizontal(void)
{
    uint8_t selected = 1;
    CHECK(run_menu("~YES ~NO", "n", 1, false, &selected) == 1 && selected == 2);
    selected = 1;
    CHECK(run_menu("~YES ~NO", "\r", 1, false, &selected) == 0);
    /* Right arrow, then Enter on the selected item. */
    selected = 1;
    CHECK(run_menu("~YES ~NO", "\x01\x4d\r", 3, false, &selected) == 1 && selected == 2);
    /* 4 wraps left; Escape, space, unknown letters and punctuation do nothing. */
    selected = 1;
    CHECK(run_menu("~A ~B ~C", "4\x1b q!\r", 6, false, &selected) == 2 && selected == 3);
    /* A selection past the items starts over at the first. */
    selected = 9;
    CHECK(run_menu("~A ~B", "\r", 1, false, &selected) == 0 && selected == 1);
    /* Other extended keys and keypad digits are special and keep the menu up. */
    selected = 1;
    CHECK(run_menu("~A ~B", "\x01\x3b" "8b", 4, false, &selected) == 1);
    CHECK(special_count == 2 && specials[0] == 0x3b && specials[1] == 0x48);
    /* With one item, 4 and the arrows are special too. */
    selected = 1;
    CHECK(run_menu("~CONTINUE", "4\x01\x4d\r", 4, true, &selected) == 0);
    CHECK(special_count == 2 && specials[0] == 0x4b && specials[1] == 0x4d);
    /* Enter on an empty menu needs enter_returns. */
    selected = 1;
    CHECK(run_menu("", "\r", 1, true, &selected) == 0);
    CHECK(run_menu("", "\r", 1, false, &selected) == -1);
    /* A digit hotkey other than 0 cannot be typed; a moved-up digit gives 0xff. */
    selected = 1;
    CHECK(run_menu("~1 ~0", "10", 2, false, &selected) == 1 && special_count == 1);
    CHECK(run_menu("~A1", "q", 1, false, &selected) == 0xff && selected == 2);
    CHECK(run_menu("~A", "", 0, false, &selected) == -1);
}

static int read_key(const char *text, const char *keys, size_t length, uint8_t *selected,
                    bool *special)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    script s;
    cok_keyboard k = keyboard(&s, keys, length);
    int key = cok_menu_read(&p, &font, "", text, 13, 15, 10, selected, &k, special);
    cok_picture_free(&p);
    cok_font_free(&font);
    return key;
}

static void test_read(void)
{
    /* Text is laid out as given: each capital starts an item and is its key. */
    uint8_t selected = 1;
    bool special = true;
    CHECK(read_key("Move Area Look", "a", 1, &selected, &special) == 'A' && !special &&
          selected == 2);
    CHECK(read_key("Move Area Look", "\x01\x4d\r", 3, &selected, &special) == 'L' &&
          !special && selected == 3);
    CHECK(read_key("Move Area Look", "o\x1b", 2, &selected, &special) == 0);
    /* Special keys come back as scan codes; with one item the arrows do too. */
    CHECK(read_key("Move Area Look", "8", 1, &selected, &special) == 0x48 && special);
    selected = 3;
    CHECK(read_key("Exit", "\x01\x4b", 2, &selected, &special) == 0x4b && special &&
          selected == 1);
    CHECK(read_key("Exit", "\r", 1, &selected, &special) == 'E' && !special);
    CHECK(read_key("Exit", "", 0, &selected, &special) == -1);

    /* The prompt goes first, in its colour, and the items after it. */
    cok_font font = numbered_font();
    cok_picture p = screen();
    script s;
    cok_keyboard k = keyboard(&s, "\x1b", 1);
    selected = 1;
    CHECK(cok_menu_read(&p, &font, "AB ", "Bash Exit", 13, 15, 10, &selected, &k, &special) == 0);
    CHECK(cell_glyph(&p, 0, 24) == 1 && cell_fg(&p, 0, 24) == 13);
    CHECK(cell_glyph(&p, 3, 24) == 2 && cell_bg(&p, 3, 24) == 15);
    cok_picture_free(&p);
    cok_font_free(&font);
}

static void test_list(void)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    const char *items[] = {"ONE", " TWO ", "THREE", "FOUR", "FIVE"};
    cok_text_window w = {1, 18, 38, 19}; /* Two rows. */
    script s;
    int index = 0, top = 0;
    uint8_t selected = 1;

    /* Down wraps within the rows shown. */
    cok_keyboard k = keyboard(&s, "\x01\x50\x01\x50\r", 5);
    CHECK(cok_menu_list(&p, &font, items, 5, w, 15, 10, false, &index, &top, &selected, &k) == 'S');
    CHECK(index == 0 && top == 0);

    /* Next pages down keeping the row; the picked item is marked inside its spaces. */
    k = keyboard(&s, "2N", 2);
    CHECK(cok_menu_list(&p, &font, items, 5, w, 15, 10, false, &index, &top, &selected, &k) == -1);
    CHECK(index == 3 && top == 2);
    CHECK(cell_glyph(&p, 1, 18) == 20 && cell_glyph(&p, 1, 19) == 6);
    /* Next stays selected; Select shows its hotkey. */
    CHECK(cell_bg(&p, 7, 24) == 15 && cell_fg(&p, 0, 24) == 15 && cell_bg(&p, 0, 24) == 0);

    /* The last page stops at count - rows; PgUp goes back; space picks. */
    index = 3;
    top = 2;
    k = keyboard(&s, "\x01\x51\x01\x49 ", 5);
    CHECK(cok_menu_list(&p, &font, items, 5, w, 15, 10, false, &index, &top, &selected, &k) == ' ');
    CHECK(top == 1 && index == 2);

    /* Escape cancels, keeping the index. */
    index = 1;
    top = 0;
    k = keyboard(&s, "\x1b", 1);
    CHECK(cok_menu_list(&p, &font, items, 5, w, 15, 10, false, &index, &top, &selected, &k) == 0);
    CHECK(index == 1);
    /* Exit cancels when offered; Enter on Next pages. */
    k = keyboard(&s, "e", 1);
    CHECK(cok_menu_list(&p, &font, items, 5, w, 15, 10, true, &index, &top, &selected, &k) == 0);
    selected = 1;
    index = 0;
    top = 0;
    k = keyboard(&s, "\x01\x4d\r\x01\x4bs", 6);
    CHECK(cok_menu_list(&p, &font, items, 5, w, 15, 10, false, &index, &top, &selected, &k) == 'S');
    CHECK(top == 2 && index == 2);
    /* An index below the rows shown stays there, as in the original, which
     * then marks it outside the window. */
    index = 4;
    top = 0;
    k = keyboard(&s, "\r", 1);
    selected = 1;
    CHECK(cok_menu_list(&p, &font, items, 5, w, 15, 10, false, &index, &top, &selected, &k) == 'S');
    CHECK(index == 4 && top == 0);
    cok_picture_free(&p);
    cok_font_free(&font);
}

static void test_rows(void)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    cok_menu_row rows[] = {{"1ST", true}, {"A", false}, {"B", false},
                           {"2ND", true}, {"C", false}, {"D", false}};
    cok_text_window w = {1, 5, 38, 7}; /* Three rows. */
    cok_menu_style style = {"CHOOSE: ", "Memorize", 15, 10, 13, true};
    script s;
    int index = 0, top = 0;
    uint8_t selected = 1;
    bool redraw = true;
    /* A list that starts with a heading starts on the last row shown. */
    cok_keyboard k = keyboard(&s, "\r", 1);
    CHECK(cok_menu_rows(&p, &font, rows, 6, w, &style, &redraw, &index, &top, &selected, &k) == 'M');
    CHECK(index == 2 && !redraw);
    /* Headings show in their colour; the prompt too, then the items. */
    CHECK(cell_fg(&p, 1, 5) == 13 && cell_fg(&p, 1, 6) == 10 && cell_fg(&p, 0, 24) == 13);
    /* Down from the last row wraps to the first, a heading, and moves on. */
    k = keyboard(&s, "\x01\x50\r", 3);
    CHECK(cok_menu_rows(&p, &font, rows, 6, w, &style, &redraw, &index, &top, &selected, &k) == 'M');
    CHECK(index == 1);
    /* Prev is offered past the leading headings only: at row 1 it is not. */
    index = 1;
    top = 1;
    k = keyboard(&s, "p\r", 2);
    CHECK(cok_menu_rows(&p, &font, rows, 6, w, &style, &redraw, &index, &top, &selected, &k) == 'M');
    CHECK(top == 1);
    top = 2;
    index = 2;
    k = keyboard(&s, "pm", 2);
    CHECK(cok_menu_rows(&p, &font, rows, 6, w, &style, &redraw, &index, &top, &selected, &k) == 'M');
    CHECK(top == 0);
    /* Without redraw the window is left as it is. */
    cok_text_glyph(&p, &font, 9, 1, 30, 6, 4, 0);
    redraw = false;
    index = 1;
    top = 0;
    k = keyboard(&s, "\x1b", 1);
    CHECK(cok_menu_rows(&p, &font, rows, 6, w, &style, &redraw, &index, &top, &selected, &k) == 0);
    CHECK(cell_glyph(&p, 30, 6) == 9);
    cok_picture_free(&p);
    cok_font_free(&font);
}

static void test_keypad(void)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    script s;
    uint8_t selected = 1;
    bool special;
    /* Without the keypad, 8 is no direction; 4 still moves the selection,
     * and with one item comes back as itself. */
    cok_keyboard k = keyboard(&s, "84y", 3);
    CHECK(cok_menu_ask(&p, &font, "", "Yes No", 13, 15, 10, false, &selected, &k, &special) == 'Y');
    CHECK(selected == 1 && !special);
    k = keyboard(&s, "4", 1);
    CHECK(cok_menu_ask(&p, &font, "", "Exit", 13, 15, 10, false, &selected, &k, &special) == '4');
    CHECK(!special);
    k = keyboard(&s, "4", 1);
    CHECK(cok_menu_ask(&p, &font, "", "Exit", 13, 15, 10, true, &selected, &k, &special) == 0x4b);
    CHECK(special);
    cok_picture_free(&p);
    cok_font_free(&font);
}

static void test_input(void)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    script s;
    char line[41];
    /* ~ is past 0x7a and e past the limit of 3; both are ignored. */
    cok_keyboard k = keyboard(&s, "ab\x08" "c\x01\x47~de\r", 10);
    CHECK(cok_text_input(&p, &font, "NAME:", 10, 0, 3, &k, line));
    CHECK(strcmp(line, "ACD") == 0);
    CHECK(cell_bg(&p, 0, 24) == 0 && cell_glyph(&p, 0, 24) == 0); /* Row cleared. */

    /* Drawn one column past the prompt, without clearing on the way. */
    k = keyboard(&s, "x", 1);
    CHECK(!cok_text_input(&p, &font, "AB", 10, 0, 40, &k, line));
    CHECK(strcmp(line, "X") == 0);

    uint16_t value;
    k = keyboard(&s, "x\r70000\r $1F\r", 13);
    CHECK(cok_text_number(&p, &font, "", 10, 0, &k, &value) && value == 31);
    k = keyboard(&s, "\r-0\r", 4);
    CHECK(cok_text_number(&p, &font, "", 10, 0, &k, &value) && value == 0);
    k = keyboard(&s, "65536\x1b", 6);
    CHECK(cok_text_number(&p, &font, "", 10, 0, &k, &value) && value == 0);
    k = keyboard(&s, "12", 2);
    CHECK(!cok_text_number(&p, &font, "", 10, 0, &k, &value) && value == 12);

    long n;
    CHECK(cok_tp_val("", &n) == 1 && n == 0);
    CHECK(cok_tp_val("  ", &n) == 3);
    CHECK(cok_tp_val(" 42", &n) == 0 && n == 42);
    CHECK(cok_tp_val("42 ", &n) == 3 && n == 0);
    CHECK(cok_tp_val("-", &n) == 2);
    CHECK(cok_tp_val("+7", &n) == 0 && n == 7);
    CHECK(cok_tp_val("$", &n) == 2);
    CHECK(cok_tp_val("$ff", &n) == 0 && n == 255);
    CHECK(cok_tp_val("$FFFFFFFF", &n) == 0 && n == -1);
    CHECK(cok_tp_val("2147483648", &n) == 10);
    CHECK(cok_tp_val("12a", &n) == 3);
    cok_picture_free(&p);
    cok_font_free(&font);
}

static void test_screen(void)
{
    /* Tile t is filled with colour t % 16, so cells show which tile went where. */
    cok_picture tiles = {0};
    CHECK(cok_picture_create(&tiles, 1, 8, 38, 0) == COK_PICTURE_OK);
    for (size_t t = 0; t < 38; ++t) memset(tiles.pixels + t * 32, (int)(t % 16 * 0x11), 32);
    cok_picture p = screen();
    memset(p.pixels, 0x77, p.frame_size);
    const uint16_t moons[3] = {1, 2, 3};
    const cok_rules *R = &cok_rules_champions;
    cok_screen_draw(&p, &tiles, R, COK_SCREEN_ADVENTURE, moons);
#define TILE(x, y) cell_bg(&p, x, y)
    CHECK(TILE(0, 0) == 0x14 % 16 && TILE(7, 0) == 0x15 % 16 && TILE(9, 0) == 0x16 % 16);
    CHECK(TILE(8, 0) == (0x14 + 11) % 16 && TILE(19, 0) == (0x14 + 8) % 16 &&
          TILE(30, 0) == (0x14 + 17) % 16);
    CHECK(TILE(0, 1) == 0x17 % 16 && TILE(0, 2) == 0x18 % 16 && TILE(39, 15) == 0x19 % 16);
    CHECK(TILE(0, 16) == 0x14 % 16 && TILE(39, 17) == 0x17 % 16 && TILE(5, 23) == 0x14 % 16);
    CHECK(TILE(16, 1) == 0x17 % 16 && TILE(16, 16) == 0x14 % 16 && TILE(10, 16) == 0x14 % 16);
    CHECK(TILE(2, 3) == 0x17 % 16 && TILE(14, 13) == 0x19 % 16 && TILE(8, 14) == 0x14 % 16);
    CHECK(TILE(3, 3) == 0 && TILE(38, 22) == 0 && TILE(20, 24) == 7);
    memset(p.pixels, 0x77, p.frame_size);
    cok_screen_draw(&p, &tiles, R, COK_SCREEN_BIG, moons);
    CHECK(TILE(16, 1) == 0 && TILE(10, 16) == 0x14 % 16 && TILE(2, 3) == 0);
    cok_screen_draw(&p, &tiles, R, COK_SCREEN_OPEN, moons);
    CHECK(TILE(0, 15) == 0x18 % 16 && TILE(39, 17) == 0x18 % 16 && TILE(0, 18) == 0x18 % 16);
    CHECK(TILE(0, 16) == 0x18 % 16 && TILE(39, 16) == 0x18 % 16 && TILE(0, 14) == 0x18 % 16);
    /* Memorize's keeps rows 17-22; a moon's tile is its phase plus its
     * offset, a byte (0x80 + 14 is past the set: nothing drawn). */
    memset(p.pixels, 0x77, p.frame_size);
    const uint16_t far[3] = {1, 2, 0x80};
    cok_screen_draw(&p, &tiles, R, COK_SCREEN_SPELLS, far);
    CHECK(TILE(5, 10) == 0 && TILE(5, 17) == 7 && TILE(5, 22) == 7 && TILE(5, 16) == 0x14 % 16);
    CHECK(TILE(0, 20) == 0x18 % 16 && TILE(39, 22) == 0x19 % 16 && TILE(30, 0) == 7);
    /* The credits: three boxes, no moons. */
    memset(p.pixels, 0x77, p.frame_size);
    cok_screen_draw(&p, &tiles, R, COK_SCREEN_CREDITS, NULL);
    CHECK(TILE(0, 1) == 0x17 % 16 && TILE(39, 2) == 0x18 % 16 && TILE(0, 3) == 0x19 % 16);
    CHECK(TILE(5, 4) == 0x14 % 16 && TILE(0, 5) == 0x17 % 16 && TILE(39, 19) == 0x19 % 16);
    CHECK(TILE(8, 0) == 0x14 % 16 && TILE(0, 21) == 0x17 % 16 && TILE(39, 22) == 0x19 % 16);
    CHECK(TILE(5, 10) == 0 && TILE(5, 24) == 7);
#undef TILE
    cok_picture_free(&p);
    cok_picture_free(&tiles);
}


/* dkk 6a69:0f1b: menus by index. */
static int pick_key(const char *text, const cok_menu_items *items, const char *keys,
                    size_t length, bool keypad, uint8_t *selected, bool *special,
                    cok_picture *shown)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    script s = {keys, 0, length};
    cok_keyboard k = {scripted, &s, flush_rest, NULL};
    int key = cok_menu_pick(&p, &font, "", text, items, 13, 15, 10, keypad, selected, &k,
                            special);
    if (shown != NULL) *shown = p;
    else cok_picture_free(&p);
    cok_font_free(&font);
    return key;
}

static void test_indexed(void)
{
    static const char text[] =
        "Move Aim Use Cast Turn Guard Quick@Delay Bandage Yell View Speed End";
    /* 6a69:0d1e: the items listed, in order, with their spaces; the last
     * space deleted; '@' kept with its item. */
    cok_menu_items items = {0};
    static const uint8_t shown[] = {1, 2, 5, 6, 7, 10, 11, 12};
    for (size_t i = 0; i < sizeof shown; ++i) cok_menu_items_add(&items, shown[i]);
    cok_menu_indexed_layout m;
    cok_menu_indexed(&m, text, &items);
    CHECK(strcmp(m.text, "Aim Use Guard Quick@Delay View Speed End") == 0);
    CHECK(m.hotkeys == 8 && m.index[2] == 5 && m.hotkey[2] == 'G' && m.index[7] == 12 &&
          m.hotkey[7] == 'E');
    /* 6a69:0135: Quick ends two before Delay ('@' between), four items on
     * the first row. */
    CHECK(m.two_rows && m.first_row == 4 && m.count == 8);
    CHECK(m.first[4] == 15 && m.last[4] == 19 && m.first[5] == 21 && m.last[8] == 40);
    cok_menu_items_upto(&items, 2);
    cok_menu_indexed(&m, "Faster Slower Exit", &items);
    CHECK(strcmp(m.text, "Faster Slower Exit") == 0 && !m.two_rows && m.count == 3);
    cok_menu_items_upto(&items, 1);
    cok_menu_indexed(&m, "Faster Slower Exit", &items);
    CHECK(strcmp(m.text, "Faster Slower") == 0 && m.last[2] == 13);
    items = (cok_menu_items){0};
    cok_menu_items_add(&items, 1);
    cok_menu_indexed(&m, "Target Exit", &items);
    CHECK(strcmp(m.text, "Exit") == 0 && m.index[0] == 1);
    /* 00cb fills 0 to n; 00ab stops at the list's 20. */
    cok_menu_items_upto(&items, 30);
    CHECK(items.count == COK_MENU_MAX_ITEMS && items.index[19] == 19);

    items = (cok_menu_items){0};
    for (size_t i = 0; i < sizeof shown; ++i) cok_menu_items_add(&items, shown[i]);
    uint8_t selected = 1;
    bool special = true;
    /* A hotkey gives its index, Enter the selected one's; Escape is the
     * special key 0x1b; Space and a key no item has are read past. */
    CHECK(pick_key(text, &items, "g", 1, true, &selected, &special, NULL) == 5 && !special &&
          selected == 3);
    CHECK(pick_key(text, &items, " x\r", 3, true, &selected, &special, NULL) == 5 && !special);
    CHECK(pick_key(text, &items, "\x1b", 1, true, &selected, &special, NULL) == 0x1b && special);
    /* Left and Right move the selection, wrapping, and read on (6a69:0c26,
     * 0c59 clear the loop's flag). */
    selected = 1;
    CHECK(pick_key(text, &items, "\x01\x4b", 2, true, &selected, &special, NULL) == -1 &&
          selected == 8);
    CHECK(pick_key(text, &items, "\x01\x4d\r", 3, true, &selected, &special, NULL) == 1 &&
          !special && selected == 1);
    /* Up and Down: to the other row, as many items over as the first row
     * holds (6a69:0c8c), kept within the items. */
    selected = 2;
    CHECK(pick_key(text, &items, "\x01\x50", 2, true, &selected, &special, NULL) == -1 &&
          selected == 6);
    CHECK(pick_key(text, &items, "\x01\x48", 2, true, &selected, &special, NULL) == -1 &&
          selected == 2);
    selected = 4;
    CHECK(pick_key(text, &items, "\x01\x50", 2, true, &selected, &special, NULL) == -1 &&
          selected == 8);
    selected = 8;
    CHECK(pick_key(text, &items, "\x01\x48", 2, true, &selected, &special, NULL) == -1 &&
          selected == 4);
    /* Without the keypad 2 and 8 change rows and 4 and 6 move, all read
     * past; with it each is its scan code, the keypad's 5 0x20. */
    selected = 1;
    CHECK(pick_key(text, &items, "26\r", 3, false, &selected, &special, NULL) == 10 &&
          !special && selected == 6);
    CHECK(pick_key(text, &items, "5", 1, true, &selected, &special, NULL) == 0x20 && special);
    CHECK(pick_key(text, &items, "1", 1, true, &selected, &special, NULL) == 0x4f && special);
    CHECK(pick_key(text, &items, "\\", 1, true, &selected, &special, NULL) == 0x37 && special);
    CHECK(pick_key(text, &items, "4", 1, true, &selected, &special, NULL) == 0x4b && special &&
          selected == 5);
    /* H and P change rows where no item has them. */
    selected = 1;
    CHECK(pick_key(text, &items, "h\r", 2, false, &selected, &special, NULL) == 7 &&
          selected == 5);
    /* Two items: the arrows move between them. */
    cok_menu_items_upto(&items, 1);
    selected = 1;
    CHECK(pick_key("Yes No", &items, "\x01\x4b", 2, false, &selected, &special, NULL) == -1 &&
          selected == 2);
    /* One item: the arrows come back and move nothing. */
    cok_menu_items_upto(&items, 0);
    selected = 1;
    CHECK(pick_key("Done", &items, "\x01\x4b", 2, true, &selected, &special, NULL) == 0x4b &&
          selected == 1);
    CHECK(pick_key("Done", &items, "\r", 1, true, &selected, &special, NULL) == 0 && !special);
    CHECK(pick_key("Done", &items, "", 0, true, &selected, &special, NULL) == -1);
    /* A selection past the items, or 0, starts at the first. */
    selected = 9;
    CHECK(pick_key("Done", &items, "\r", 1, true, &selected, &special, NULL) == 0 &&
          selected == 1);

    /* Two rows: the first on row 23, the second from the same column on
     * row 24, each row's rest cleared; letters in highlight, the rest in
     * normal, the selected item in 0 on highlight. */
    items = (cok_menu_items){0};
    for (size_t i = 0; i < sizeof shown; ++i) cok_menu_items_add(&items, shown[i]);
    cok_picture p;
    selected = 5;
    CHECK(pick_key(text, &items, "x", 1, true, &selected, &special, &p) == -1);
    CHECK(cell_glyph(&p, 0, 23) == ('A' & 63) && cell_fg(&p, 0, 23) == 15);
    CHECK(cell_glyph(&p, 1, 23) == ('I' & 63) && cell_fg(&p, 1, 23) == 10);
    CHECK(cell_glyph(&p, 18, 23) == ('K' & 63) && cell_bg(&p, 19, 23) == 0);
    CHECK(cell_glyph(&p, 0, 24) == ('D' & 63) && cell_fg(&p, 0, 24) == 0 &&
          cell_bg(&p, 0, 24) == 15 && cell_bg(&p, 4, 24) == 15 && cell_bg(&p, 5, 24) == 0);
    CHECK(cell_glyph(&p, 6, 24) == ('V' & 63) && cell_fg(&p, 6, 24) == 15);
    cok_picture_free(&p);
    /* The prompt on row 24 in its colour, the items after it. */
    cok_font font = numbered_font();
    p = screen();
    script s = {"\x1b", 0, 1};
    cok_keyboard k = {scripted, &s, NULL, NULL};
    cok_menu_items_upto(&items, 0);
    selected = 1;
    CHECK(cok_menu_pick(&p, &font, "AB ", "Done", &items, 13, 15, 10, true, &selected, &k,
                        &special) == 0x1b);
    CHECK(cell_glyph(&p, 0, 24) == 1 && cell_fg(&p, 0, 24) == 13 && cell_bg(&p, 3, 24) == 15);
    cok_picture_free(&p);
    cok_font_free(&font);
    /* It flushes the keyboard once it has its key. */
    flushes = 0;
    CHECK(pick_key("Done", &items, "\rxy", 3, true, &selected, &special, NULL) == 0 &&
          flushes == 1);
}

/* dkk 6a69:1b82: lists by index. */
static void test_indexed_rows(void)
{
    cok_font font = numbered_font();
    cok_picture p = screen();
    cok_menu_row rows[] = {{"A", false}, {"B", false}, {"C", false}, {"D", false},
                           {"E", false}, {"F", false}, {"G", false}};
    cok_text_window w = {1, 5, 38, 7}; /* Three rows. */
    cok_menu_style style = {"", "", 15, 10, 13, true};
    script s;
    int index = 0, top = 0;
    uint8_t selected = 1;
    bool redraw = true, special = false, picked;
    cok_menu_items items;
    /* Trade money's (580d:23a0): "Select" alone, its last index 0: the
     * list's Next and Exit at 1 and 2. Enter picks the row, its index the
     * menu's: 0. */
    cok_menu_items_upto(&items, 0);
    cok_keyboard k = keyboard(&s, "\r", 1);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 7, w, &style, "Select", &items, 0, &redraw,
                                &index, &top, &selected, &k, &special, &picked) == 0);
    CHECK(picked && !special && index == 0 && !redraw);
    /* The rows in the normal colour, the one picked unmarked once picked;
     * the menu: Select Next Exit. */
    CHECK(cell_fg(&p, 1, 5) == 10 && cell_bg(&p, 1, 5) == 0 && cell_fg(&p, 1, 6) == 10);
    CHECK(cell_glyph(&p, 7, 24) == ('N' & 63) && cell_glyph(&p, 12, 24) == ('E' & 63));
    /* Next (its index 1) pages; then Prev; Exit returns 0x1b, nothing
     * picked. Down moves; 2 too, the keypad's. */
    k = keyboard(&s, "N\x01\x50" "2\x1b", 5);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 7, w, &style, "Select", &items, 0, &redraw,
                                &index, &top, &selected, &k, &special, &picked) == 0x1b);
    CHECK(!picked && special && top == 3 && index == 5);
    CHECK(cell_glyph(&p, 7, 24) == ('N' & 63) && cell_glyph(&p, 12, 24) == ('P' & 63));
    k = keyboard(&s, "P" "E", 2);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 7, w, &style, "Select", &items, 0, &redraw,
                                &index, &top, &selected, &k, &special, &picked) == 0x1b);
    CHECK(top == 0 && index == 2 && special && !picked);
    /* The items menu's (580d:0ffa): its last index 8, fewer items before
     * it: the list's own are not shown, Next's key does nothing; its Exit,
     * 9, is past 8 and taken as Next while there are rows below. */
    cok_menu_items_upto(&items, 0);
    cok_menu_items_add(&items, 9);
    static const char menu[] = "Ready Use Trade Deposit Drop Halve Join Sell Id Exit";
    index = 0;
    top = 0;
    k = keyboard(&s, "N" "E" "\x1b", 3);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 7, w, &style, menu, &items, 8, &redraw, &index,
                                &top, &selected, &k, &special, &picked) == 0x1b);
    CHECK(top == 3 && !picked && special && s.at == 3);
    CHECK(cell_glyph(&p, 6, 24) == ('E' & 63) && cell_glyph(&p, 11, 24) == 0);
    /* At the last page, the index Next took before is Prev's now: Exit
     * pages back up. The scan code 0x4e pages down. */
    index = 0;
    top = 0;
    k = keyboard(&s, "E" "E" "\x1b", 3);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 5, w, &style, menu, &items, 8, &redraw, &index,
                                &top, &selected, &k, &special, &picked) == 0x1b);
    CHECK(top == 0 && s.at == 3);
    k = keyboard(&s, "\x01\x4e\x1b", 3);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 5, w, &style, menu, &items, 8, &redraw, &index,
                                &top, &selected, &k, &special, &picked) == 0x1b);
    CHECK(top == 2);
    top = 0;
    /* With every row shown: Exit is the list's Exit; R picks the row. */
    k = keyboard(&s, "E", 1);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 3, w, &style, menu, &items, 8, &redraw, &index,
                                &top, &selected, &k, &special, &picked) == 0x1b);
    CHECK(top == 0 && special && !picked);
    k = keyboard(&s, "R", 1);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 3, w, &style, menu, &items, 8, &redraw, &index,
                                &top, &selected, &k, &special, &picked) == 0);
    CHECK(picked && !special);
    /* No rows: 0 at once, nothing read. */
    k = keyboard(&s, "R", 1);
    CHECK(cok_menu_indexed_rows(&p, &font, rows, 0, w, &style, menu, &items, 8, &redraw, &index,
                                &top, &selected, &k, &special, &picked) == 0);
    CHECK(!picked && index == 0 && s.at == 0);
    /* A heading marked (headings alone: the skip stops after a page) is
     * unmarked in the normal colour, not its own. */
    cok_menu_row headed[] = {{"H", true}, {"I", true}};
    cok_menu_items_upto(&items, 0);
    index = 0;
    redraw = true;
    k = keyboard(&s, "\x1b", 1);
    CHECK(cok_menu_indexed_rows(&p, &font, headed, 2, w, &style, "Select", &items, 0, &redraw,
                                &index, &top, &selected, &k, &special, &picked) == 0x1b);
    CHECK(index == 1 && cell_fg(&p, 1, 5) == 13 && cell_fg(&p, 1, 6) == 10);
    cok_picture_free(&p);
    cok_font_free(&font);
}

int main(void)
{
    test_parse();
    test_draw();
    test_horizontal();
    test_read();
    test_list();
    test_rows();
    test_keypad();
    test_input();
    test_screen();
    test_flush();
    test_indexed();
    test_indexed_rows();
    puts("menu tests passed");
    return 0;
}
