#ifndef COK_SHEET_H
#define COK_SHEET_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* View (546c:0d74): the selected character's sheet, from overlay 546c,
 * with its menu "Items Spells Trade Drop Exit" as the character allows:
 * its items (items.h), the spells it has memorized, and trading and
 * dropping its money. */

/* What the byte Trade's first Escape tests holds when View starts: the
 * flag at [bp-0x13c] of Trade (546c:2c75) is set only by an amount, and
 * before one is a byte of the stack that earlier calls left. Loading the
 * camp's or the treasure's picture zero-fills it, and any choice made in
 * View, or a View before in the same camp or treasure menu, leaves it
 * nonzero; elsewhere what it holds depends on the history of the run. */
typedef enum {
    COK_SHEET_STALE_UNKNOWN, /* Escape there stops the port. */
    COK_SHEET_STALE_ZERO,    /* Escape asks for another partner. */
    COK_SHEET_STALE_SET,     /* Escape leaves Trade. */
} cok_sheet_stale;

/* View the selected character (546c:0d74) until Exit or Escape, or until
 * an item used (*done, which View clears first) ends it, which only combat
 * keeps; then the screen is redrawn for the mode (6346:2c17). */
void cok_sheet(cok_adventure *game, cok_sheet_stale stale, bool *done);

/* Draw the selected character's sheet (546c:00a3), recomputing its stats
 * first (546c:07bb). Returns false, ending the run, where a name or a
 * stat cannot be had as the original has it. */
bool cok_sheet_draw(cok_adventure *game);

/* Parts of the sheet that character creation and Modify draw alone. */

/* Ability i (0 strength ... 5 charisma) on row 9 + i in color (546c:0b50):
 * the score from column 5, 6 below 10, "(NN)" at column 7 for an
 * exceptional strength, and "*" at column 12 while it differs from the
 * base; columns 5-11 are cleared first. */
void cok_sheet_ability(cok_adventure *game, const uint8_t *c, int i, uint8_t color);
/* The money on rows 9-15 (546c:0667). Returns false, ending the run, where
 * a coin's name lies past the name strings. */
bool cok_sheet_money(cok_adventure *game, const uint8_t *c);
/* The combat figures (546c:07bb), after recomputing the stats (6346:0d20).
 * Returns false, ending the run, where they cannot be worked out. */
bool cok_sheet_figures(cok_adventure *game, cok_character *character);
/* The hit points at x, y (6346:0a0d): yellow while below the maximum, else
 * light green, light magenta to highlight, then with show_max "/" and the
 * maximum in light green. Nothing is cleared first. */
void cok_sheet_hit_points(cok_adventure *game, const uint8_t *c, int x, int y, bool highlight,
                          bool show_max);

/* Ask an amount of at most max on row 24 after prompt (58e7:028d): digits,
 * Backspace, Enter or Escape (0). A number past max becomes max. Returns
 * -1 when input ended. */
int cok_sheet_amount(cok_adventure *game, const char *prompt, uint16_t max);

#endif
