#include "menu.h"

#include <stdio.h>
#include <string.h>

/* Letters and digits: hotkeys (the sets at 3775:174e, 3775:1865, 67b5:00ba
 * and 67b5:01c9). */
static bool hotkey_char(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

static unsigned char upcase(unsigned char c)
{
    return c >= 'a' && c <= 'z' ? (unsigned char)(c - ('a' - 'A')) : c;
}

void cok_menu_parse(cok_menu *menu, const char *text)
{
    *menu = (cok_menu){0};
    /* A Pascal string: Delete leaves the byte past the new end in place,
     * and the loop runs to the original length. */
    unsigned char s[257] = {0};
    size_t length = strlen(text);
    if (length > 255) length = 255;
    memcpy(s + 1, text, length);
    s[0] = (unsigned char)length;
    size_t keys = 0;
    for (size_t i = 1; i <= length; ++i) {
        if (hotkey_char(s[i])) s[i] = (unsigned char)(s[i] + 0x20);
        if (s[i] != '~') continue;
        if (i <= s[0]) {
            memmove(s + i, s + i + 1, s[0] - i);
            --s[0];
        }
        s[i] = upcase(s[i]);
        if (keys < 40) menu->keys[keys++] = (char)s[i];
    }
    s[s[0] + 1u] = '\0';
    cok_menu_layout(menu, (const char *)s + 1);
}

void cok_menu_layout(cok_menu *menu, const char *text)
{
    size_t shown = strlen(text);
    if (shown > 40) shown = 40;
    memcpy(menu->text, text, shown);
    menu->text[shown] = '\0';
    memset(menu->first, 0, sizeof menu->first);
    memset(menu->last, 0, sizeof menu->last);

    size_t count = 1;
    for (size_t d = 1; d <= shown; ++d) {
        if (!hotkey_char((unsigned char)menu->text[d - 1])) continue;
        if (menu->first[count] == 0) {
            menu->first[count] = (uint8_t)d;
        } else if (count < COK_MENU_MAX_ITEMS) {
            /* More items overrun the original's arrays; here the last item
             * takes the rest. */
            menu->last[count] = (uint8_t)(d - 2);
            menu->first[++count] = (uint8_t)d;
        }
    }
    menu->last[count] = (uint8_t)shown;
    menu->count = count;
}

void cok_menu_draw(cok_picture *dst, const cok_font *font, const cok_menu *menu, int x,
                   size_t selected, uint8_t highlight, uint8_t normal)
{
    size_t length = strlen(menu->text);
    if (length == 0) return;
    for (size_t k = 1; k <= length; ++k) {
        unsigned char c = (unsigned char)menu->text[k - 1];
        int column = x + (int)k - 1;
        bool chosen = selected <= COK_MENU_MAX_ITEMS && k >= menu->first[selected] &&
                      k <= menu->last[selected] && highlight != 0;
        if (chosen)
            cok_text_glyph(dst, font, cok_text_index((char)c), 1, column, 24, 0, highlight);
        else
            cok_text_glyph(dst, font, cok_text_index((char)c), 1, column, 24,
                           hotkey_char(c) ? highlight : normal, 0);
    }
    if ((size_t)x + length < 0x27)
        cok_text_glyph(dst, font, cok_text_index(' '), 0x28 - ((size_t)x + length),
                       x + (int)length, 24, 0, 0);
}

static void append_text(char *dst, size_t size, const char *text)
{
    size_t length = strlen(dst);
    while (*text != '\0' && length + 1 < size) dst[length++] = *text++;
    dst[length] = '\0';
}

/* Keypad keys as directions (DS:1fdf), from '1'. */
static const uint8_t keypad_scans[9] = {0x4f, 0x50, 0x51, 0x4b, 0x20, 0x4d, 0x47, 0x48, 0x49};

static uint8_t keypad_scan(unsigned char c)
{
    return c == '\\' ? 0x37 : keypad_scans[c - '1'];
}

/* 67b5:03e2's click (Enter with DS:8987, 065c-0a88), in cell (x / 8,
 * y / 8): the game's regions first (pointer->click), then row 24's items,
 * an item's hotkey as Enter on it would give (it selected), then the
 * columns of a list shown (DS:6e2f, 6e30), where a row at or below one of
 * its rows gives Enter itself. Returns the key, or -1 to read on. */
static int click(cok_picture *dst, const cok_font *font, const cok_menu *menu, int x,
                 uint8_t *selected, uint8_t highlight, uint8_t normal, const cok_pointer *pointer,
                 bool *special)
{
    int col = pointer->x / 8, row = pointer->y / 8;
    if (pointer->click != NULL) {
        int key = pointer->click(pointer->context, col, row, special);
        if (key >= 0) return key;
        if (key == COK_CLICK_KEEP) return -1;
    }
    if (row == 24) {
        /* 091a: the position in the menu's text, items tried in turn. */
        int found = -1;
        if (x < col + 1) {
            int at = col + 1 - x;
            for (size_t item = 1; item <= menu->count; ++item) {
                if (menu->first[item] > at || at > menu->last[item]) continue;
                found = (unsigned char)menu->text[menu->first[item] - 1];
                *selected = (uint8_t)item;
                cok_menu_draw(dst, font, menu, x, *selected, highlight, normal);
            }
        }
        *special = false;
        return found;
    }
    if (pointer->list_left <= col && col <= pointer->list_right) {
        /* 0a37: a row at or below one shown, not the one itself. */
        for (size_t i = 0; i < 25 && pointer->list_rows[i] != 0xff; ++i) {
            if (pointer->list_rows[i] <= row) {
                *special = false;
                return 0x0d;
            }
        }
    }
    return -1;
}

/* One key from 67b5:03e2: returns the key, setting *special for scan codes. */
static int menu_wait(cok_picture *dst, const cok_font *font, const cok_menu *menu, int x,
                    uint8_t *selected, uint8_t highlight, uint8_t normal, bool keypad,
                    const cok_keyboard *keys, bool *special, int timeout)
{
    cok_menu_draw(dst, font, menu, x, *selected, highlight, normal);
    size_t length = strlen(menu->text);
    for (;;) {
        int key = keys == NULL || keys->read == NULL ? -1 : keys->read(keys->context);
        if (key == COK_KEY_TIMEOUT) {
            /* 67b5:0579: the time passed with no key; the result is
             * DS:6e15's, the selection as it was. */
            if (timeout != 0) {
                *special = false;
                return timeout;
            }
            continue;
        }
        if (key < 0) return -1;
        if (key == 0) {
            key = keys->read(keys->context);
            if (key < 0) return -1;
            *special = true;
            if (menu->count < 2) return key;
            if (key == 0x4b) {
                *selected = (uint8_t)(*selected == 1 ? menu->count : *selected - 1u);
            } else if (key == 0x4d) {
                *selected = (uint8_t)(*selected == menu->count ? 1 : *selected + 1u);
            } else {
                return key;
            }
            cok_menu_draw(dst, font, menu, x, *selected, highlight, normal);
            continue;
        }
        *special = false;
        if (key == 0x1b) return 0;
        if (key == 0x0d && keys->pointer != NULL && keys->pointer->clicked) {
            int result = click(dst, font, menu, x, selected, highlight, normal, keys->pointer,
                               special);
            if (result >= 0) return result;
            continue;
        }
        if (key == 0x0d) {
            uint8_t first = menu->first[*selected];
            int value = first == 0 ? (int)length : (unsigned char)menu->text[first - 1];
            return value == 0 ? 0x0d : value;
        }
        if (key == '4' || key == '6') {
            if (menu->count < 2) {
                if (!keypad) return key;
                *special = true;
                return keypad_scan((unsigned char)key);
            }
            if (key == '4')
                *selected = (uint8_t)(*selected == 1 ? menu->count : *selected - 1u);
            else
                *selected = (uint8_t)(*selected == menu->count ? 1 : *selected + 1u);
            cok_menu_draw(dst, font, menu, x, *selected, highlight, normal);
            continue;
        }
        unsigned char c = upcase((unsigned char)key);
        bool done = false;
        if (c == ' ') {
            done = true;
        } else if (hotkey_char(c)) {
            for (size_t j = 1; j <= length; ++j) {
                if ((unsigned char)menu->text[j - 1] != c) continue;
                done = true;
                for (size_t item = 1; item <= menu->count; ++item)
                    if (menu->first[item] == j) *selected = (uint8_t)item;
                cok_menu_draw(dst, font, menu, x, *selected, highlight, normal);
            }
        }
        if (keypad && ((c >= '1' && c <= '9') || c == '\\')) {
            *special = true;
            return keypad_scan(c);
        }
        if (done) return c;
    }
}

/* 67b5:03e2 flushes the keyboard (1614:045c) once it has its key, so keys
 * typed ahead are lost. */
static int menu_key(cok_picture *dst, const cok_font *font, const cok_menu *menu, int x,
                    uint8_t *selected, uint8_t highlight, uint8_t normal, bool keypad,
                    const cok_keyboard *keys, bool *special, int timeout)
{
    int key = menu_wait(dst, font, menu, x, selected, highlight, normal, keypad, keys, special,
                        timeout);
    if (key == -1) return key;
    if (keys->flush != NULL) keys->flush(keys->context);
    /* 67b5:0c2d: after a click, until the mouse's buttons are up; with the
     * joystick, its buttons. */
    if (keys->pointer != NULL && keys->pointer->release != NULL)
        keys->pointer->release(keys->pointer->context);
    return key;
}

int cok_menu_horizontal(cok_picture *dst, const cok_font *font, const char *prompt,
                        const char *items, uint8_t prompt_color, uint8_t highlight,
                        uint8_t normal, bool enter_returns, uint8_t *selected,
                        const cok_keyboard *keys, const cok_menu_hooks *hooks)
{
    char shown[41];
    snprintf(shown, sizeof shown, "%s", prompt);
    cok_menu menu;
    cok_menu_parse(&menu, items);
    int x = (int)strlen(shown);
    int key;
    for (;;) {
        if (menu.count < *selected) *selected = 1;
        if (x != 0) cok_text_string(dst, font, shown, 0, 24, prompt_color, 0);
        bool special = false;
        key = menu_key(dst, font, &menu, x, selected, highlight, normal, true, keys, &special, 0);
        if (key < 0) return -1;
        if (special) {
            if (hooks != NULL && hooks->special != NULL)
                hooks->special((uint8_t)key, hooks->context);
            continue;
        }
        if (hotkey_char((unsigned char)key) || (key == 0x0d && enter_returns)) break;
    }
    if (key == 0x0d) return 0;
    const char *found = strchr(menu.keys, key);
    return found == NULL ? 0xff : (int)(found - menu.keys);
}

int cok_menu_read(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                  uint8_t prompt_color, uint8_t highlight, uint8_t normal, uint8_t *selected,
                  const cok_keyboard *keys, bool *special)
{
    return cok_menu_ask(dst, font, prompt, text, prompt_color, highlight, normal, true, selected,
                        keys, special);
}

int cok_menu_ask(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                 uint8_t prompt_color, uint8_t highlight, uint8_t normal, bool keypad,
                 uint8_t *selected, const cok_keyboard *keys, bool *special)
{
    char shown[41];
    snprintf(shown, sizeof shown, "%s", prompt);
    cok_menu menu;
    cok_menu_layout(&menu, text);
    if (menu.count < *selected) *selected = 1;
    int x = (int)strlen(shown);
    if (x != 0) cok_text_string(dst, font, shown, 0, 24, prompt_color, 0);
    *special = false;
    return menu_key(dst, font, &menu, x, selected, highlight, normal, keypad, keys, special, 0);
}

int cok_menu_timed(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                   uint8_t prompt_color, uint8_t highlight, uint8_t normal, uint8_t *selected,
                   const cok_keyboard *keys, int timeout)
{
    char shown[41];
    snprintf(shown, sizeof shown, "%s", prompt);
    cok_menu menu;
    cok_menu_layout(&menu, text);
    if (menu.count < *selected) *selected = 1;
    int x = (int)strlen(shown);
    if (x != 0) cok_text_string(dst, font, shown, 0, 24, prompt_color, 0);
    bool special = false;
    return menu_key(dst, font, &menu, x, selected, highlight, normal, false, keys, &special,
                    timeout);
}

typedef struct {
    cok_picture *dst;
    const cok_font *font;
    const cok_menu_row *items;
    int count, rows;
    cok_text_window w;
    const cok_menu_style *style;
    int *index, *top;
    cok_pointer *pointer; /* NULL without the mouse's state */
    /* A heading unmarked in the normal colour (dkk 6a69:17ab), rather
     * than in its own (67b5:0fd0). */
    bool plain_unmark;
} list;

static bool heading(const list *l, int i)
{
    return i >= 0 && i < l->count && l->items[i].heading;
}

/* Clear the window and show the rows from *top, headings in their colour
 * (67b5:0d24). */
static void draw_list(const list *l)
{
    cok_picture_fill(l->dst, l->w.left, l->w.top * 8, (size_t)(l->w.right - l->w.left + 1),
                     (size_t)(l->w.bottom - l->w.top + 1) * 8, 0);
    /* DS:6e16-6e2e: the screen row of each row shown, for clicks. */
    if (l->pointer != NULL) memset(l->pointer->list_rows, 0xff, sizeof l->pointer->list_rows);
    size_t shown = 0;
    for (int item = *l->top, y = l->w.top; item >= 0 && item < l->count && y <= l->w.bottom;
         ++item, ++y) {
        if (l->pointer != NULL && shown < sizeof l->pointer->list_rows)
            l->pointer->list_rows[shown++] = (uint8_t)y;
        cok_text_string(l->dst, l->font, l->items[item].text, l->w.left, y,
                        l->items[item].heading ? l->style->heading : l->style->normal, 0);
    }
}

/* The list gone: its columns no longer take clicks (DS:6e2f 0x7f, 6e30
 * 0x80). */
static int list_end(cok_pointer *pointer, int result)
{
    if (pointer != NULL) {
        pointer->list_left = 0x7f;
        pointer->list_right = -0x80;
    }
    return result;
}

/* Redraw the picked item's text between its first and last non-space
 * characters (67b5:0f2e, 67b5:0fd0, with 67b5:0e9c and 67b5:0ee8). */
static void mark(const list *l, bool on)
{
    if (*l->index < 0 || *l->index >= l->count) return;
    char text[41];
    snprintf(text, sizeof text, "%s", l->items[*l->index].text);
    size_t length = strlen(text), first = 1, last = length;
    while (first < length && text[first - 1] == ' ') ++first;
    while (last > 1 && text[last - 1] == ' ') --last;
    if (last < first) return;
    text[last] = '\0';
    int x = l->w.left + (int)first - 1, y = l->w.top + (*l->index - *l->top);
    if (on)
        cok_text_string(l->dst, l->font, text + first - 1, x, y, 0, l->style->highlight);
    else if (l->items[*l->index].heading && !l->plain_unmark)
        cok_text_string(l->dst, l->font, text + first - 1, x, y, l->style->heading, 0);
    else
        cok_text_string(l->dst, l->font, text + first - 1, x, y, l->style->normal, 0);
}

/* Move past headings in the direction of the last move, at most a page's
 * rows, wrapping within the rows shown (67b5:10d1). */
static void skip(const list *l, bool down)
{
    int *i = l->index, top = *l->top;
    for (int k = 0; k < l->rows && heading(l, *i); ++k) {
        if (down) {
            if (++*i > top + l->rows - 1) *i = top;
            if (*i > l->count - 1) *i = top;
        } else {
            if (--*i < top) *i = top + l->rows - 1;
            if (*i > l->count - 1) *i = l->count - 1;
        }
    }
}

/* Move one row up or down among the rows shown, wrapping, then past
 * headings (67b5:1293). */
static void step(const list *l, bool down)
{
    int *i = l->index, top = *l->top;
    if (!down) {
        if (--*i < top) *i = top + l->rows - 1;
        if (*i > l->count - 1) *i = l->count - 1;
    } else {
        if (++*i > top + l->rows - 1) *i = top;
        if (*i > l->count - 1) *i = top;
    }
    skip(l, down);
}

/* Show the previous or next page, keeping the row picked (67b5:1201). */
static void page_list(const list *l, bool down)
{
    int row = *l->index - *l->top;
    if (!down) {
        *l->top -= l->rows;
        if (*l->top < 0) *l->top = 0;
    } else {
        *l->top += l->rows;
        if (*l->top > l->count - l->rows) *l->top = l->count - l->rows;
    }
    *l->index = *l->top + row;
    skip(l, down);
    draw_list(l);
}

int cok_menu_rows(cok_picture *dst, const cok_font *font, const cok_menu_row *items,
                  size_t count, cok_text_window window, const cok_menu_style *style,
                  bool *redraw, int *index, int *top, uint8_t *selected,
                  const cok_keyboard *keys)
{
    cok_pointer *pointer = keys != NULL ? keys->pointer : NULL;
    if (count == 0) {
        *index = 0;
        return list_end(pointer, 0);
    }
    list l = {dst, font, items, count > 255 ? 255 : (int)count, window.bottom - window.top + 1,
              window, style, index, top, pointer, false};
    if (pointer != NULL) {
        /* 67b5:13c5: the window's columns take clicks. */
        pointer->list_left = (int8_t)window.left;
        pointer->list_right = (int8_t)window.right;
    }
    *selected = 1;
    int leading = 0;
    while (heading(&l, leading)) ++leading;
    if (l.count <= l.rows) *top = 0;
    if (*index < *top) {
        *top = *index;
        *redraw = true;
    }
    if (l.count < *top) {
        *top = 0;
        *redraw = true;
    }
    ++*index;
    step(&l, false);
    if (*redraw) draw_list(&l);
    *redraw = false;
    char prompt[41];
    snprintf(prompt, sizeof prompt, "%s", style->prompt);
    int x = (int)strlen(prompt);
    for (;;) {
        mark(&l, true);
        char bar[41];
        snprintf(bar, sizeof bar, "%s", style->base);
        bool more = *top < l.count - l.rows, less = *top > leading;
        if (more) append_text(bar, sizeof bar, " Next");
        if (less) append_text(bar, sizeof bar, " Prev");
        if (style->show_exit) append_text(bar, sizeof bar, " Exit");
        cok_menu menu;
        cok_menu_layout(&menu, bar);
        if (menu.count < *selected) *selected = 1;
        if (x != 0) cok_text_string(dst, font, prompt, 0, 24, style->heading, 0);
        bool special = false;
        int key = menu_key(dst, font, &menu, x, selected, style->highlight, style->normal, true,
                           keys, &special, 0);
        mark(&l, false);
        if (key < 0) return list_end(pointer, -1);
        if (special) {
            if (key == 0x48) step(&l, false);
            else if (key == 0x50) step(&l, true);
            else if (key == 0x49 && less) page_list(&l, false);
            else if (key == 0x51 && more) page_list(&l, true);
            continue;
        }
        if (key == 'P') {
            page_list(&l, false);
        } else if (key == 'N') {
            page_list(&l, true);
        } else if (key == 0x1b || key == 'E' || key == 0) {
            return list_end(pointer, 0);
        } else if (key == 0x0d && pointer != NULL && pointer->clicked) {
            /* 67b5:167d: a click on a row shown picks it out, no more. */
            uint8_t row = (uint8_t)(pointer->y / 8), col = (uint8_t)(pointer->x / 8);
            if (pointer->list_left <= col && col <= pointer->list_right) {
                for (size_t i = 0; i < 25 && pointer->list_rows[i] != 0xff; ++i) {
                    if (pointer->list_rows[i] != row) continue;
                    mark(&l, false);
                    *index = (int)i + *top;
                    mark(&l, true);
                }
            }
        } else {
            return list_end(pointer, key);
        }
    }
}

int cok_menu_list(cok_picture *dst, const cok_font *font, const char *const *items,
                  size_t count, cok_text_window window, uint8_t highlight, uint8_t normal,
                  bool show_exit, int *index, int *top,
                  uint8_t *selected, const cok_keyboard *keys)
{
    cok_menu_row rows[255];
    if (count > 255) count = 255;
    for (size_t i = 0; i < count; ++i) rows[i] = (cok_menu_row){items[i], false};
    cok_menu_style style = {"", "Select", highlight, normal, normal, show_exit};
    bool redraw = true;
    return cok_menu_rows(dst, font, rows, count, window, &style, &redraw, index, top, selected,
                         keys);
}

/* Menus by index (dkk 6a69:0f1b). */

void cok_menu_items_upto(cok_menu_items *items, uint8_t last)
{
    *items = (cok_menu_items){0};
    for (unsigned i = 0; i <= last && i < COK_MENU_MAX_ITEMS; ++i)
        items->index[items->count++] = (uint8_t)i;
}

void cok_menu_items_add(cok_menu_items *items, uint8_t index)
{
    if (items->count < COK_MENU_MAX_ITEMS) items->index[items->count++] = index;
}

void cok_menu_indexed(cok_menu_indexed_layout *out, const char *text, const cok_menu_items *items)
{
    *out = (cok_menu_indexed_layout){0};
    /* 6a69:0d1e: each item of text starts at a letter or digit and runs to
     * the next; those the list names, in its order, are copied with the
     * spaces after them, their first characters the hotkeys (DS:c121 +
     * 2k); one trailing space is deleted. */
    size_t length = strlen(text), at = 0, shown = 0;
    if (length > 255) length = 255;
    unsigned item = 0;
    for (size_t i = 0; i < length;) {
        if (!hotkey_char((unsigned char)text[i])) {
            ++i;
            continue;
        }
        if (shown < items->count && items->index[shown] == item) {
            out->hotkey[shown] = (uint8_t)text[i];
            out->index[shown++] = (uint8_t)item;
            size_t start = i;
            do ++i;
            while (i < length && !hotkey_char((unsigned char)text[i]));
            for (size_t k = start; k < i && at < 255; ++k) out->text[at++] = text[k];
        } else {
            ++i;
        }
        ++item;
    }
    if (at > 0 && out->text[at - 1] == ' ') --at;
    out->text[at] = '\0';
    out->length = at;
    out->hotkeys = shown;
    /* 6a69:0135: the items laid out by the text's letters and digits, each
     * to two before the next, columns 1-based in the whole text ('@'
     * counted); the items before '@' are its first row's. */
    out->two_rows = strchr(out->text, '@') != NULL;
    size_t count = 1;
    for (size_t d = 1; d <= at; ++d) {
        unsigned char c = (unsigned char)out->text[d - 1];
        if (!hotkey_char(c)) {
            if (c == '@') out->first_row = count;
        } else if (out->first[count] == 0) {
            out->first[count] = (uint8_t)d;
        } else if (count < COK_MENU_MAX_ITEMS) {
            out->last[count] = (uint8_t)(d - 2);
            out->first[++count] = (uint8_t)d;
        }
    }
    out->last[count] = (uint8_t)at;
    out->count = count;
}

/* 6a69:025d: the items from column x of row 24, or of rows 23 and 24 with
 * '@' between, the rest of each row cleared; the selected one in colour 0
 * on highlight, letters and digits in highlight, the rest in normal. */
static void indexed_draw(cok_picture *dst, const cok_font *font, const cok_menu_indexed_layout *m,
                         int x, uint8_t selected, uint8_t highlight, uint8_t normal)
{
    int row = m->two_rows ? 23 : 24, column = x;
    for (size_t k = 1; k <= m->length; ++k) {
        unsigned char c = (unsigned char)m->text[k - 1];
        if (c == '@') {
            if (column <= 0x27)
                cok_text_clear(dst, font, (size_t)(0x28 - column), column, row, 0); /* 110f:0584 */
            ++row;
            column = x;
            continue;
        }
        bool chosen = selected <= m->count && k >= m->first[selected] && k <= m->last[selected] &&
                      highlight != 0;
        if (column <= 0x27) {
            if (chosen)
                cok_text_glyph(dst, font, cok_text_index((char)c), 1, column, row, 0, highlight);
            else
                cok_text_glyph(dst, font, cok_text_index((char)c), 1, column, row,
                               hotkey_char(c) ? highlight : normal, 0);
        }
        ++column;
    }
    if (column < 0x28) cok_text_clear(dst, font, (size_t)(0x28 - column), column, row, 0);
}

/* Move the selection back (6a69:0c26) or on (0c59) one, wrapping, or for
 * 0 to the other row (0c8c), as many items over as the first row holds,
 * kept within the items. Returns true. */
static bool indexed_move(const cok_menu_indexed_layout *m, uint8_t *selected, int by)
{
    size_t count = m->count, first = m->first_row;
    if (by < 0) {
        *selected = (uint8_t)(*selected == 1 ? count : *selected - 1u);
    } else if (by > 0) {
        *selected = (uint8_t)(*selected == count ? 1 : *selected + 1u);
    } else if (first < *selected) {
        *selected = (uint8_t)(*selected - first);
        if (first < *selected) *selected = (uint8_t)first;
    } else {
        *selected = (uint8_t)(*selected + first);
        if (count < *selected) *selected = (uint8_t)count;
    }
    return true;
}

/* 6a69:0e74: a hotkey's item index, or 0xff. */
static int indexed_result(const cok_menu_indexed_layout *m, int key)
{
    for (size_t k = 0; k < m->hotkeys; ++k)
        if (m->hotkey[k] == key) return m->index[k];
    return 0xff;
}

int cok_menu_pick(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                  const cok_menu_items *items, uint8_t prompt_color, uint8_t highlight,
                  uint8_t normal, bool keypad, uint8_t *selected, const cok_keyboard *keys,
                  bool *special)
{
    char shown[41];
    snprintf(shown, sizeof shown, "%s", prompt);
    cok_menu_indexed_layout m;
    cok_menu_indexed(&m, text, items);
    *special = false; /* DS:c0f8 */
    if (m.count < *selected || *selected == 0) *selected = 1;
    int x = (int)strlen(shown);
    if (x != 0) cok_text_string(dst, font, shown, 0, 24, prompt_color, 0);
    indexed_draw(dst, font, &m, x, *selected, highlight, normal);
    int result = -1;
    for (bool done = false; !done;) {
        int key = keys == NULL || keys->read == NULL ? -1 : keys->read(keys->context);
        if (key == COK_KEY_TIMEOUT) continue;
        if (key < 0) return -1;
        bool moved = false;
        if (key == 0) {
            key = keys->read(keys->context);
            if (key < 0) return -1;
            *special = true;
            done = true;
            result = key;
            /* A special key is returned, Left and Right moving the
             * selection first, and Up and Down between two rows. */
            if (m.count > 1) {
                if (key == 0x4b) moved = indexed_move(&m, selected, -1);
                else if (key == 0x4d) moved = indexed_move(&m, selected, 1);
                else if ((key == 0x48 || key == 0x50) && m.two_rows) moved = indexed_move(&m, selected, 0);
            }
        } else {
            *special = false;
            if (key == 0x1b) {
                *special = true;
                done = true;
                result = 0x1b;
            } else if (key == 0x0d) {
                /* 0442: the selected item's first character, 0x0d for none
                 * (the length byte of an empty text). */
                uint8_t first = m.first[*selected];
                int value = first == 0 ? (int)m.length : (unsigned char)m.text[first - 1];
                result = indexed_result(&m, value == 0 ? 0x0d : value);
                done = true;
            } else {
                unsigned char c = upcase((unsigned char)key);
                if (indexed_result(&m, c) != 0xff) {
                    for (size_t j = 1; j <= m.length; ++j) {
                        if ((unsigned char)m.text[j - 1] != c) continue;
                        done = true;
                        for (size_t item = 1; item <= m.count; ++item)
                            if (m.first[item] == j) *selected = (uint8_t)item;
                        indexed_draw(dst, font, &m, x, *selected, highlight, normal);
                    }
                }
                if (!done && m.count > 1) {
                    if (c == '4') moved = indexed_move(&m, selected, -1);
                    else if (c == '6') moved = indexed_move(&m, selected, 1);
                    else if ((c == '2' || c == '8' || c == 'H' || c == 'P') && m.two_rows)
                        moved = indexed_move(&m, selected, 0); /* the set at 6a69:0edb */
                }
                result = indexed_result(&m, c);
                if (keypad && ((c >= '1' && c <= '9') || c == '\\')) {
                    /* DS:5791 + the key, as Champions' DS:1fdf. */
                    *special = true;
                    done = true;
                    result = keypad_scan(c);
                }
            }
        }
        if (moved) indexed_draw(dst, font, &m, x, *selected, highlight, normal);
    }
    if (keys->flush != NULL) keys->flush(keys->context); /* 16ab:03b0 */
    if (keys->pointer != NULL && keys->pointer->release != NULL)
        keys->pointer->release(keys->pointer->context);
    return result;
}

/* Death Knights' lists (dkk 6a69:1b82). */

int cok_menu_indexed_rows(cok_picture *dst, const cok_font *font, const cok_menu_row *rows,
                          size_t count, cok_text_window window, const cok_menu_style *style,
                          const char *text, const cok_menu_items *items, uint8_t last,
                          bool *redraw, int *index, int *top, uint8_t *selected,
                          const cok_keyboard *keys, bool *special, bool *picked)
{
    *picked = false;
    if (count == 0) {
        *index = 0;
        return 0;
    }
    /* The rows' clicks (DS:c118, c119, c0fe) are not taken, as the
     * indexed loop takes none. */
    list l = {dst, font, rows, count > 255 ? 255 : (int)count, window.bottom - window.top + 1,
              window, style, index, top, NULL, true};
    int leading = 0;
    while (heading(&l, leading)) ++leading;
    if (l.count <= l.rows) *top = 0;
    if (*index < *top) {
        *top = *index;
        *redraw = true;
    }
    if (l.count < *top) {
        *top = 0;
        *redraw = true;
    }
    ++*index;
    step(&l, false);
    if (*redraw) draw_list(&l);
    *redraw = false;
    /* DS:c120 as the caller filled it, 0xff past its items (FillChar);
     * the list's own items are written from last + 1 on, each its own
     * index, and stay there from one round to the next. */
    uint8_t entries[COK_MENU_MAX_ITEMS];
    memset(entries, 0xff, sizeof entries);
    for (size_t k = 0; k < items->count && k < COK_MENU_MAX_ITEMS; ++k)
        entries[k] = items->index[k];
    /* The indexes the list's Next, Prev and Exit took last (bytes of the
     * stack until then, here none: every caller offers Exit). */
    unsigned at_next = 0x100, at_prev = 0x100, at_exit = 0x100;
    for (;;) {
        mark(&l, true);
        char bar[256];
        snprintf(bar, sizeof bar, "%s", text);
        unsigned at = (unsigned)last + 1;
        bool more = *top < l.count - l.rows, less = *top > leading;
        if (more) {
            append_text(bar, sizeof bar, " Next"); /* DS:37de */
            at_next = at;
            if (at < COK_MENU_MAX_ITEMS) entries[at] = (uint8_t)at;
            ++at;
        }
        if (less) {
            append_text(bar, sizeof bar, " Prev"); /* DS:37e9 */
            at_prev = at;
            if (at < COK_MENU_MAX_ITEMS) entries[at] = (uint8_t)at;
            ++at;
        }
        if (style->show_exit) {
            append_text(bar, sizeof bar, " Exit"); /* DS:37f4 */
            at_exit = at;
            if (at < COK_MENU_MAX_ITEMS) entries[at] = (uint8_t)at;
            ++at;
        }
        /* 6a69:0d1e shows the entries up to the first 0xff: past a gap
         * after the caller's, none of the list's own. */
        cok_menu_items shown = {0};
        for (size_t k = 0; k < COK_MENU_MAX_ITEMS && entries[k] != 0xff; ++k)
            cok_menu_items_add(&shown, entries[k]);
        int key = cok_menu_pick(dst, font, style->prompt, bar, &shown, style->heading,
                                style->highlight, style->normal, true, selected, keys, special);
        if (key < 0) return -1;
        /* An index past the caller's is the list's own, by the index each
         * took, while it is offered (Exit's whatever was offered). */
        if (!*special && key > (int)last) {
            *special = true;
            if ((unsigned)key == at_next && more) key = 0x51;
            else if ((unsigned)key == at_prev && less) key = 0x49;
            else if ((unsigned)key == at_exit) key = 0x1b;
            else *special = false;
        }
        mark(&l, false);
        if (!*special) {
            *picked = true;
            return key;
        }
        if (key == 0x48) {
            step(&l, false);
        } else if (key == 0x50) {
            step(&l, true);
        } else if (key == 0x49) {
            if (less) page_list(&l, false);
        } else if (key == 0x51) {
            if (more) page_list(&l, true);
        } else if (key == 0x4e) {
            page_list(&l, true);
        } else if (key == 0x1b || key == 0x45 || key == 0) {
            return 0x1b;
        }
    }
}
