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
/* The command menu of a Dark Queen party member's turn (1bdb:1595 without its mouse areas,
 * dqk 5045:0cd7): the items on the bottom bar in one row (BAR_ROW) when split < 0, else in two,
 * the items up to split (the index of the '@' item) on row 178 and the rest from column 8 on row
 * 190; no prompt, the extended keys, Escape and '`' as codes, and Space as 0x20 (no beep). It
 * returns the item's index, a filtered key's code, or -1 when the keys ran out. */
int queen_combat_menu(cok_adventure *game, const char *const *items, size_t count, int split);
/* The command menu taken off (1bdb:03d7): the bottom bar, then FRAME 6 over the whole window. */
void queen_combat_menu_close(cok_adventure *game);
/* queen_bar_menu with areas of the screen after its buttons (1bdb:1429): a
 * click on one gives QUEEN_AREA + its index. */
enum { QUEEN_AREA = 0x100 };
int queen_bar_menu_areas(cok_adventure *game, const char *prompt, const char *const *items,
                         size_t count, bool dirs, const queen_rect *areas, size_t area_count);
/* A message boxed on the bar from cell x + 1 in fg on bg (1a62:0904, the
 * x of 0 and colours of its callers), for the speed's time; the same
 * message the way 177e:153c shows it (white on grey from cell 0). */
void queen_message(cok_adventure *game, const char *s, int x, unsigned fg, unsigned bg);
void queen_bar_message(cok_adventure *game, const char *s);
/* Rows y0 to y1 and columns x0 to x1 (ends excluded) XORed with mask (22b7:0078
 * with 0x100 added to its colour). */
void queen_xor(cok_adventure *game, int y0, int x0, int y1, int x1, uint8_t mask);
/* The mouse's pointer, hidden while a line is edited. */
void queen_cursor_show(cok_adventure *game, bool shown);
/* A question and Yes or No (1bdb:0424): 1, 0 for No or Escape, -1. */
int queen_yes_no(cok_adventure *game, const char *prompt);
/* A line typed on the bar's field (1a62:05c9, the editor 6431:0001), at
 * most `most` characters: 1 for Return, 0 for Escape, -1. */
int queen_edit_line(cok_adventure *game, char *line, size_t most);
/* The field with a prompt of its own (1a62:05c9): the prompt in fg on bg
 * from cell 1, the line typed after it. */
int queen_input_line(cok_adventure *game, const char *prompt, unsigned fg, unsigned bg,
                     char *line, size_t most);
/* Whether a mouse driver is installed. Not DS:c2f5, which 1bdb:1206 sets
 * on a mouse hit on a control. */
bool queen_mouse(const cok_adventure *game);
/* The character written to the save path as <name>.QCH (604b:23a7), asking
 * about a file there. False where the run stopped. */
bool queen_write_character(cok_adventure *game, const cok_character *character);
/* Add Character's icons for the member just joined (7368:0af4, 0ba7: 604b:05d7): CBODY into its
 * slot (+0xc5, set by the join: cok_party_add), with no NPC test. False where the run stopped. */
bool queen_added_icons(cok_adventure *game, const uint8_t *record);
/* A member removed (177e:1e31's flag 1, 204c:004d): its slot's icons emptied. */
void queen_remove_icons(cok_adventure *game, const uint8_t *record);
/* The combat icon of slot (0-0x25) and image (0 ready, 1 attacking) as the table holds it
 * (dqk 19a4:0881's copy, header included): record and size, NULL and 0 for an empty slot.
 * False for a game that has no such table (not Dark Queen's) or a slot or image past it. For
 * the tests and eclplay --icons. */
bool queen_icon(const cok_adventure *game, unsigned slot, unsigned image, const uint8_t **record,
                size_t *size);
/* A Dark Queen party member's turn (dqk 5045:08cf, queen_commands.c): the command menu until a
 * command ends the turn or a stop is reached (its routine cited by cok_adventure_not_yet). False
 * where the run stopped; true, with the turn not ended, when the keys ran out. */
bool queen_commands(cok_adventure *game, cok_character *c);
/* A frame record (FRAME.TLB) or a record of ALWAYS.TLB drawn at pixel
 * row, col. */
void queen_draw_frame(cok_adventure *game, unsigned id, int row, int col);
void queen_draw_always(cok_adventure *game, unsigned id, int row, int col);

/* The libraries (FRAME and ALWAYS have their own calls above): DUNGCOM.TLB
 * (its sets of tiles and palettes), CBODY.TLB (the party's bodies) and
 * WILDCOM.TLB (the wilderness' sets, the combat ground). */
enum { QUEEN_LIB_DUNGCOM = 12, QUEEN_LIB_CBODY = 13, QUEEN_LIB_WILDCOM = 14 };
/* A record of a library drawn at pixel row, col (as 269b:0a9c), and the
 * palette record of a library (set 0) or of its nested library set. */
void queen_draw_library(cok_adventure *game, unsigned which, unsigned id, int row, int col);
bool queen_set_palette(cok_adventure *game, unsigned which, unsigned set, unsigned record);
/* A message in the bar for the speed's time (177e:153c, 1a62:0904: the
 * text in colour fg), the bar again after. */
void queen_bar_message_colour(cok_adventure *game, const char *s, unsigned fg);
/* DS:bd92, which turns Train and Knight new Order on in the camp wherever
 * the party is: whether set, and set. */
bool queen_train_anywhere(const cok_adventure *game);
void queen_set_train_anywhere(cok_adventure *game, bool on);

/* The character sheet (86b8:0000) of a member or a character in the
 * making, which recomputes it first (177e:0be2). False where that stopped
 * the run. */
bool queen_sheet(cok_adventure *game, cok_character *character);
/* The sheet's score k (0-5) drawn lit or not (86b8:096c), and its lower
 * block after the recompute and the movement's (86b8:065e). */
void queen_sheet_score(cok_adventure *game, const uint8_t *c, int k, bool lit);
bool queen_sheet_derived(cok_adventure *game, cok_character *character);

#endif
