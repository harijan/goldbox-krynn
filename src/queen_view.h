#ifndef COK_QUEEN_VIEW_H
#define COK_QUEEN_VIEW_H

#include "adventure.h"
#include "queen.h"

/* The sheet's menu (86b8:0b0c) of The Dark Queen of Krynn: Items
 * (86b8:0f41), Trade (86b8:286a), Deposit and Drop (86b8:2c32), Lay on
 * hands (86b8:3200) and Cure (86b8:3319), each of the character selected
 * (the party's member), drawn in the window the sheet left. They return
 * false where the run stopped (a routine of step 4's rules, cited). */
bool queen_view_items(cok_adventure *game, cok_character *character);

/* What dark_queen.c's window library lends them beyond queen.h. */

/* 1bdb:1f25: the list menu of a bar of items (list_menu): the prompt on the
 * bar, its buttons (the first taken by Return), the rows from row y1 to y2
 * in cells 1 to x2; *at the current row. The index of the button or 0x1b
 * for Escape, -1 where the keys ran out. */
int queen_list_menu(cok_adventure *game, const char *prompt, const char *const *bar,
                    size_t bar_count, const char *const *items, size_t count, size_t *at,
                    int y1, int y2, int x2);
/* 177e:153c: a message on the bar for the speed's time. */
void queen_bar_message(cok_adventure *game, const char *s);
/* 1a62:0311: text in cells x1-x2 by y1-y2. */
bool queen_print_window(cok_adventure *game, const char *s, int x1, int y1, int x2, int y2,
                        unsigned fg, bool clear, bool slow);
/* The party's list (5e22:023b) with the member `selected` (a record, or
 * NULL for none) in light cyan. */
void queen_party_list(cok_adventure *game, const uint8_t *selected);
/* A question with Yes and No (1bdb:0424), the small picture animating
 * where anim. */
int queen_ask(cok_adventure *game, const char *prompt, bool anim);
/* The title of a character's screen, 177e:1697: its name at cell x, y
 * (light grey; black where it cannot act, light magenta where it may
 * train) and, with "'s" set, those two characters after it. */
void queen_name_line(cok_adventure *game, const cok_character *character, int x, int y,
                     bool possessive);
/* 20b2:03b5: the full window with its title bar's line (FRAME 7 under row
 * 2). */
void queen_title_window(cok_adventure *game);

#endif
