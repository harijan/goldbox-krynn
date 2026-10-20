#ifndef COK_MENU_H
#define COK_MENU_H

#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Menus on row 24 from GAME.OVR: hotkeys in overlay 3775, layout, drawing
 * and keys in overlay 67b5, which also takes the mouse's clicks (DS:8987,
 * through cok_keyboard.pointer, with the game's regions) and can time out
 * (DS:6e11). */

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

/* Show prompt and the items of text, laid out as given, and read one key
 * (67b5:03e2, as overlay 475c calls it for the adventure's own menus). The
 * keys work as in cok_menu_horizontal, but the key itself is returned: an
 * upper-case character of text for its key or for Enter on its item, ' ',
 * or 0 for Escape. Extended keys, and those cok_menu_horizontal passes to
 * hooks->special, return their scan code with *special set. Returns -1 if
 * input ended.
 *
 * All of these take the mouse's clicks (Enter with keys->pointer->clicked,
 * 67b5:03e2's 065c-0a88): first the regions of the game's state
 * (pointer->click: the party list, the combat map, the view and map in
 * Move mode, Modify's sheet); then, on row 24, the item clicked, as Enter
 * on it (selecting it); else, in the columns of a list shown, a row at or
 * below one of its rows gives Enter; else nothing, and the menu reads on.
 * Once it has its key, it flushes the keyboard and waits for the buttons
 * (pointer->release, 67b5:0c2d). */
int cok_menu_read(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                  uint8_t prompt_color, uint8_t highlight, uint8_t normal, uint8_t *selected,
                  const cok_keyboard *keys, bool *special);

/* As cok_menu_read; with keypad clear, as the yes/no prompt (67b5:177f)
 * and the saved game letter call it, the digits are not directions: 4 and
 * 6 still move the selection, but with a single item they return
 * themselves, and the other digits are hotkeys or nothing. */
int cok_menu_ask(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                 uint8_t prompt_color, uint8_t highlight, uint8_t normal, bool keypad,
                 uint8_t *selected, const cok_keyboard *keys, bool *special);

/* cok_menu_ask with the digits as keys, and a timeout (67b5:03e2 with
 * DS:6e11 set, as the title menu calls it): when the keyboard gives
 * COK_KEY_TIMEOUT, the time passed with no key, it returns timeout
 * (DS:6e15). Special keys return their scan codes. */
int cok_menu_timed(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                   uint8_t prompt_color, uint8_t highlight, uint8_t normal, uint8_t *selected,
                   const cok_keyboard *keys, int timeout);

/* A row of a list: its text, and whether it is a heading (node byte
 * 0x29), which is drawn in the heading colour and cannot be picked. */
typedef struct {
    const char *text;
    bool heading;
} cok_menu_row;

/* How 67b5:1368 is called: the prompt, drawn at column 0 of row 24 in the
 * heading colour, the first menu item ("Select"), the colours, and whether
 * Exit is offered. */
typedef struct {
    const char *prompt;
    const char *base;
    uint8_t highlight, normal, heading;
    bool show_exit;
} cok_menu_style;

/* Pick from rows as 67b5:1368 does: as cok_menu_list below, but with the
 * style's prompt and first item, headings, and the list drawn only if
 * *redraw is set (or the first row shown moves to the item picked), which
 * clears *redraw. After each move the pick skips past headings in the
 * same direction, at most a page's rows, so a list that starts with a
 * heading starts on the last row shown. Prev is offered while rows other
 * than the leading headings are above the window. While it shows, its
 * window's columns take clicks (keys->pointer, DS:6e2f, 6e30, and its rows
 * DS:6e16): a click on a row shown picks it out, a heading too, and picks
 * nothing; Select does. */
int cok_menu_rows(cok_picture *dst, const cok_font *font, const cok_menu_row *rows,
                  size_t count, cok_text_window window, const cok_menu_style *style,
                  bool *redraw, int *index, int *top, uint8_t *selected,
                  const cok_keyboard *keys);

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
 * ended. ECL lists have no headings (see cok_menu_rows). */
int cok_menu_list(cok_picture *dst, const cok_font *font, const char *const *items,
                  size_t count, cok_text_window window, uint8_t highlight, uint8_t normal,
                  bool show_exit, int *index, int *top,
                  uint8_t *selected, const cok_keyboard *keys);

/* Death Knights' menus (dkk 6a69:0f1b): a text of every item the menu may
 * have and the list of those it shows, by index (DS:c120, 20 entries:
 * 6a69:00cb fills it with 0 to n, 6a69:00ab adds one); the list must be in
 * the text's order. */
typedef struct {
    uint8_t count;
    uint8_t index[COK_MENU_MAX_ITEMS];
} cok_menu_items;

/* The items 0 to last (6a69:00cb). */
void cok_menu_items_upto(cok_menu_items *items, uint8_t last);
/* Add item index (6a69:00ab), up to the list's 20. */
void cok_menu_items_add(cok_menu_items *items, uint8_t index);

/* What a menu by index shows (6a69:0d1e, 0135): each item of the text
 * starts at a letter or digit and runs to the next one, its spaces and an
 * '@' with it; those listed are copied in order, less one trailing space.
 * index and hotkey are the shown items' indexes and first characters
 * (DS:c120, c121), from [0]; first and last, from [1], the columns each
 * item covers in text, 1-based, as cok_menu's. An '@' puts the items after
 * it on a second row (row 24, the first on row 23): first_row is the
 * number of items before it. */
typedef struct {
    char text[256];
    size_t length, hotkeys;
    uint8_t index[COK_MENU_MAX_ITEMS], hotkey[COK_MENU_MAX_ITEMS];
    bool two_rows;
    size_t first_row, count;
    uint8_t first[COK_MENU_MAX_ITEMS + 1], last[COK_MENU_MAX_ITEMS + 1];
} cok_menu_indexed_layout;

void cok_menu_indexed(cok_menu_indexed_layout *out, const char *text, const cok_menu_items *items);

/* Read a menu by index (dkk 6a69:0f1b): prompt (cut to 40) at column 0 of
 * row 24 in prompt_color, the items shown after it, from the same column
 * on row 23 and then 24 for two rows, each row's rest cleared, drawn as
 * cok_menu_draw draws; *selected (DS:c0f7, 1 when 0 or past the items)
 * the selected one. Returns the index of the item whose hotkey was typed,
 * or of the selected one for Enter (0xff for a key no item has), with
 * *special clear (DS:c0f8); or a scan code with *special set: an extended
 * key's (Left and Right first moving the selection, wrapping, and Up and
 * Down to the other row, when there are items to move among), Escape's
 * 0x1b, and with keypad the keypad's digits and \ as their scan codes
 * (DS:5791: '5' is 0x20). 4 and 6 move the selection and 2, 8, H and P
 * change rows where no item has them as hotkeys; other keys, Space among
 * them, are read past. Once it has its key it flushes the keyboard. The
 * mouse's regions (6a69:0490-0ba7) are not taken: a click is a key.
 * Returns -1 if input ended. */
int cok_menu_pick(cok_picture *dst, const cok_font *font, const char *prompt, const char *text,
                  const cok_menu_items *items, uint8_t prompt_color, uint8_t highlight,
                  uint8_t normal, bool keypad, uint8_t *selected, const cok_keyboard *keys,
                  bool *special);

#endif
