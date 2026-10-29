#ifndef COK_QUEEN_H
#define COK_QUEEN_H

#include "adventure.h"

/* The Dark Queen of Krynn's window library as dark_queen.c has it, for the
 * parts of its game that the camp's buttons call (queen_create.c's
 * Create Character, queen_modify.c's Modify Character, queen_train.c's
 * Train Character and Knight new Order): their screens and menus are made
 * of the same buttons, bottom bar and lists. Every one draws to the
 * game's screen as the original draws to its page (see dark_queen.h).
 * Positions are pixels (rows then columns) unless a name says cells (8 by 8). */

/* A rectangle of the screen, rows top to bottom and columns left to right,
 * the ends excluded. */
typedef struct {
    int top, left, bottom, right;
} queen_rect;

/* What a read gave (2aa6:15e3): a key's code (251e:00e7's: the ASCII byte,
 * or an extended key from 0x100: Up 0x108, Down 0x104, PgUp 0x101, PgDn
 * 0x103, Home 0x107, End 0x105, Left 0x106, Right 0x102, Alt and a
 * letter 0xff + the letter), or a click of either button at x, y. */
typedef struct {
    int key;
    bool click;
    int x, y;
} queen_input;

enum {
    QUEEN_BUTTON_DISABLED = 0x01, /* sunken in 0x80, no hotkey */
    QUEEN_BUTTON_PRESSED = 0x08,
    QUEEN_BUTTON_WHITE = 0x40,    /* its text white, with no hotkey letter */
    QUEEN_BAR_ROW = 188,          /* the bottom bar's buttons */
    QUEEN_WINDOW = 0x17,          /* the window's grey */
};

/* Read a key or a click: its code, 0 for a click, -1 when the keys ran out
 * (COK_KEY_TIMEOUT where a timed menu's time passed). */
int queen_read_input(cok_adventure *game, queen_input *in);
/* Whether hotkey h (2aa6:000e's '@' Return, '#' Escape, '*' any) takes key. */
bool queen_hotkey(int h, int key);
bool queen_inside(const queen_rect *r, int x, int y);
/* A key or click nothing takes (2102:0193). */
void queen_beep(cok_adventure *game);
/* The controls the mouse's cursor knows (the sword over one, the shield
 * elsewhere), cleared with the screen's. */
void queen_controls_clear(cok_adventure *game);
void queen_control_add(cok_adventure *game, queen_rect r);

/* A string at pixel row, col in the attribute's colours (2102:03a0: the
 * background in the high nibble); the same at cell x, y in colour fg on
 * the window's grey (1a62:0046). */
void queen_text(cok_adventure *game, int row, int col, const char *s, unsigned attribute);
void queen_say(cok_adventure *game, int x, int y, const char *s, unsigned fg);
/* Rows y0 to y1 and columns x0 to x1 (ends excluded) in a colour
 * (22b7:0078), and cells x1-x2 by y1-y2 (inclusive) in the window's grey
 * (1a62:09a1). */
void queen_fill(cok_adventure *game, int y0, int x0, int y1, int x1, uint8_t colour);
void queen_clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2);
/* The full window (20b2:0030) and the bottom bar (1bdb:036c). */
void queen_full_window(cok_adventure *game);
void queen_bottom_bar(cok_adventure *game);
/* A framed button of width characters (1bdb:008e) and the place a click on
 * it takes. */
void queen_button(cok_adventure *game, int row, int col, const char *s, int width,
                  unsigned flags);
queen_rect queen_button_rect(int row, int col, int width);
/* The prompt boxed on the bar from cell x + 1 (1a62:0046 on row 24). */
void queen_prompt_box(cok_adventure *game, int x, const char *s, unsigned fg, unsigned bg);
/* The bottom bar's menu (1bdb:1e29): the prompt and a button an item,
 * read by its first letter or a click; the index picked, a filtered key's
 * code (Escape; with dirs the extended keys and Alt's), or -1. */
int queen_bar_menu(cok_adventure *game, const char *prompt, const char *const *items,
                   size_t count, bool dirs);
/* A question and Yes or No (1bdb:0424): 1, 0 for No or Escape, -1. */
int queen_yes_no(cok_adventure *game, const char *prompt);
/* A line typed on the bar's field (1a62:05c9, the editor 6431:0001), at
 * most `most` characters: 1 for Return, 0 for Escape, -1. */
int queen_edit_line(cok_adventure *game, char *line, size_t most);
/* A frame record (FRAME.TLB) or a record of ALWAYS.TLB drawn at pixel
 * row, col. */
void queen_draw_frame(cok_adventure *game, unsigned id, int row, int col);
void queen_draw_always(cok_adventure *game, unsigned id, int row, int col);

/* The character sheet (86b8:0000) of a member or a character in the
 * making, which recomputes it first (177e:0be2). False where that stopped
 * the run. */
bool queen_sheet(cok_adventure *game, cok_character *character);

#endif
