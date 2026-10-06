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
static const uint8_t keypad[9] = {0x4f, 0x50, 0x51, 0x4b, 0x20, 0x4d, 0x47, 0x48, 0x49};

static uint8_t keypad_scan(unsigned char c)
{
    return c == '\\' ? 0x37 : keypad[c - '1'];
}

/* One key from 67b5:03e2: returns the key, setting *special for scan codes. */
static int menu_key(cok_picture *dst, const cok_font *font, const cok_menu *menu, int x,
                    uint8_t *selected, uint8_t highlight, uint8_t normal,
                    const cok_keyboard *keys, bool *special)
{
    cok_menu_draw(dst, font, menu, x, *selected, highlight, normal);
    size_t length = strlen(menu->text);
    for (;;) {
        int key = keys == NULL || keys->read == NULL ? -1 : keys->read(keys->context);
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
        if (key == 0x0d) {
            uint8_t first = menu->first[*selected];
            int value = first == 0 ? (int)length : (unsigned char)menu->text[first - 1];
            return value == 0 ? 0x0d : value;
        }
        if (key == '4' || key == '6') {
            if (menu->count < 2) {
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
        if ((c >= '1' && c <= '9') || c == '\\') {
            *special = true;
            return keypad_scan(c);
        }
        if (done) return c;
    }
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
        key = menu_key(dst, font, &menu, x, selected, highlight, normal, keys, &special);
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

typedef struct {
    cok_picture *dst;
    const cok_font *font;
    const char *const *items;
    int count, rows;
    cok_text_window w;
    uint8_t highlight, normal;
    int *index, *top;
} list;

/* Clear the window and show the rows from *top (67b5:0d24). */
static void draw_list(const list *l)
{
    cok_picture_fill(l->dst, l->w.left, l->w.top * 8, (size_t)(l->w.right - l->w.left + 1),
                     (size_t)(l->w.bottom - l->w.top + 1) * 8, 0);
    for (int item = *l->top, y = l->w.top; item >= 0 && item < l->count && y <= l->w.bottom;
         ++item, ++y)
        cok_text_string(l->dst, l->font, l->items[item], l->w.left, y, l->normal, 0);
}

/* Redraw the picked item's text between its first and last non-space
 * characters (67b5:0f2e, 67b5:0fd0, with 67b5:0e9c and 67b5:0ee8). */
static void mark(const list *l, bool on)
{
    if (*l->index < 0 || *l->index >= l->count) return;
    char text[41];
    snprintf(text, sizeof text, "%s", l->items[*l->index]);
    size_t length = strlen(text), first = 1, last = length;
    while (first < length && text[first - 1] == ' ') ++first;
    while (last > 1 && text[last - 1] == ' ') --last;
    if (last < first) return;
    text[last] = '\0';
    int x = l->w.left + (int)first - 1, y = l->w.top + (*l->index - *l->top);
    if (on)
        cok_text_string(l->dst, l->font, text + first - 1, x, y, 0, l->highlight);
    else
        cok_text_string(l->dst, l->font, text + first - 1, x, y, l->normal, 0);
}

/* Move one row up or down among the rows shown, wrapping (67b5:1293). */
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
    draw_list(l);
}

int cok_menu_list(cok_picture *dst, const cok_font *font, const char *const *items,
                  size_t count, cok_text_window window, uint8_t highlight, uint8_t normal,
                  bool show_exit, int *index, int *top,
                  uint8_t *selected, const cok_keyboard *keys)
{
    if (count == 0) {
        *index = 0;
        return 0;
    }
    list l = {dst, font, items, count > 255 ? 255 : (int)count, window.bottom - window.top + 1,
              window, highlight, normal, index, top};
    *selected = 1;
    if (l.count <= l.rows) *top = 0;
    if (*index < *top) *top = *index;
    if (l.count < *top) *top = 0;
    ++*index;
    step(&l, false);
    draw_list(&l);
    for (;;) {
        mark(&l, true);
        char bar[41] = "Select";
        if (*top < l.count - l.rows) append_text(bar, sizeof bar, " Next");
        if (0 < *top) append_text(bar, sizeof bar, " Prev");
        if (show_exit) append_text(bar, sizeof bar, " Exit");
        bool more = *top < l.count - l.rows, less = 0 < *top;
        cok_menu menu;
        cok_menu_layout(&menu, bar);
        if (menu.count < *selected) *selected = 1;
        bool special = false;
        int key = menu_key(dst, font, &menu, 0, selected, highlight, normal, keys, &special);
        mark(&l, false);
        if (key < 0) return -1;
        if (special) {
            if (key == 0x48) step(&l, false);
            else if (key == 0x50) step(&l, true);
            else if (key == 0x49 && less) page_list(&l, false);
            else if (key == 0x51 && more) page_list(&l, true);
            continue;
        }
        if (key == 'P') page_list(&l, false);
        else if (key == 'N') page_list(&l, true);
        else if (key == 0x1b || key == 'E' || key == 0) return 0;
        else return key;
    }
}
