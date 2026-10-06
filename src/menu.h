#ifndef COK_MENU_H
#define COK_MENU_H

#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Menus on row 24 from GAME.OVR: hotkeys in overlay 3775, layout, drawing
 * and keys in overlay 67b5. Only the keyboard is ported; the original also
 * takes mouse clicks (DS:8987) and can time out (DS:6e11). */

enum { COK_MENU_MAX_ITEMS = 20 };

/* A row of items. Item text marks each hotkey with ~. */
typedef struct {
    /* Text after hotkey marking (3775:176e), cut to 40 characters as the
     * menu copies it: letters and digits are moved up by 0x20, so letters
     * show in lower case and digits become P-Y, except each hotkey, which
     * is upper-cased. */
    char text[41];
    char keys[41]; /* Hotkeys in order, at most 40. */
    /* Items (67b5:00da): each starts at an upper-case letter or digit of
     * text, which are the hotkeys and the moved-up digits, and ends two
     * columns before the next. first and
     * last are 1-based columns; there is always at least one item, which is
     * 0 to 0 for empty text. */
    size_t count;
    uint8_t first[COK_MENU_MAX_ITEMS + 1], last[COK_MENU_MAX_ITEMS + 1]; /* From [1]. */
} cok_menu;

/* Mark hotkeys and lay out items from up to 255 characters of text. */
void cok_menu_parse(cok_menu *menu, const char *text);
/* Lay out text as it is, without hotkey marks; keys is left unchanged. */
void cok_menu_layout(cok_menu *menu, const char *text);

/* Draw the items on row 24 from column x with selected (1-based) shown in
 * colour 0 on highlight, hotkeys in highlight and other characters in
 * normal, then blank the rest of the row if the items end before column 39
 * (67b5:01e9). A highlight of 0 shows no selection. */
void cok_menu_draw(cok_picture *dst, const cok_font *font, const cok_menu *menu, int x,
                   size_t selected, uint8_t highlight, uint8_t normal);

typedef struct {
    /* An extended key, or 1-9 or \ as a keypad direction, given as its scan
     * code (2fd3 menus pass these to 546c:3334 and redraw the party). NULL
     * ignores them. */
    void (*special)(uint8_t scan, void *context);
    void *context;
} cok_menu_hooks;

/* Show prompt (cut to 40 characters) at column 0 of row 24 in prompt_color,
 * the items after it, and wait for a hotkey (3775:1885 with 67b5:03e2).
 * Left and right arrows, and 4 and 6, move *selected (DS:6e0f) around the
 * items, and Enter picks the selected item's hotkey. A key matching an
 * upper-case character of the text returns the index of the first such
 * hotkey, or 0xff for a moved-up digit, which is not one; other keys are
 * ignored. Enter on an empty menu returns 0 if enter_returns is set. Escape and space do nothing. Other extended keys,
 * and 1-9 and \ as keypad directions, go to hooks->special, so no digit
 * hotkey but 0 can be picked by its key; with a single item, the arrows and
 * 4 and 6 do too. Returns -1 if input ended. */
int cok_menu_horizontal(cok_picture *dst, const cok_font *font, const char *prompt,
                        const char *items, uint8_t prompt_color, uint8_t highlight,
                        uint8_t normal, bool enter_returns, uint8_t *selected,
                        const cok_keyboard *keys, const cok_menu_hooks *hooks);

/* Pick from a list of items shown in window, one per row from the top
 * (67b5:1368 as 3775:1990 calls it). *index is the item picked, 0-based,
 * and *top the first item shown (DS:6e0d), which the game keeps between
 * lists. Up and down (or 8 and 2) move through the rows shown, wrapping;
 * the menu on row 24 offers Select, and Next and Prev when there is more
 * to see, which page the list, as do PgDn and PgUp (or 3 and 9); with
 * show_exit it also offers Exit. Items are drawn in normal on colour 0, the
 * picked one from its first to its last non-space character in colour 0 on
 * highlight, and the menu in highlight and normal with no prompt. Returns the key that picked (S, space or Enter on Select), 0 when Escape
 * or Exit cancelled, leaving *index on the last item picked, or -1 if input
 * ended. The original also shows items flagged as headings in another
 * colour and skips over them; ECL lists have none, and they are not
 * ported. */
int cok_menu_list(cok_picture *dst, const cok_font *font, const char *const *items,
                  size_t count, cok_text_window window, uint8_t highlight, uint8_t normal,
                  bool show_exit, int *index, int *top,
                  uint8_t *selected, const cok_keyboard *keys);

#endif
