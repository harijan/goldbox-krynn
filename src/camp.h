#ifndef COK_CAMP_H
#define COK_CAMP_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Camp, from overlay 4888 (the camp menu and its items) and the rest loop
 * of overlay 57e4. The camp's mode is 2 (DS:4b49), which the status line
 * shows as "camping" and which keeps the effect timers from counting down
 * members with no timed effect (57e4:0171). Magic is in magic.h. */

/* The camp menu (4888:2c31): "Save View Magic Rest Alter Fix Exit" until
 * Exit or Escape. Returns true when a rest was interrupted by an
 * encounter, the flag 2fd3:3403 then runs the rest vector for. Stops early
 * when game->vm.abort is set: input ended, the player quit, or a spell
 * effect that ended needed what is not ported. */
bool cok_camp(cok_adventure *game);

/* Rest until the time in game->rest has passed (57e4:0e3e), five minutes
 * a tick: heal, learn spells and scribe scrolls, and roll for encounters.
 * interactive shows the rest time and its menu first, and lets a key stop
 * the rest; Fix rests without them. Returns true if an encounter
 * interrupted the rest. */
bool cok_camp_rest(cok_adventure *game, bool interactive);

/* The minutes c needs to finish learning and scribing the spells marked
 * for it (4888:0032), setting its hours of preparation (+0x58): 4, or 6
 * for a spell above level 2, plus 15 minutes for each level. */
uint16_t cok_camp_preparation(cok_character *c);

/* Unmark the spells c was to memorize (4888:05fd), which also clears its
 * hours of preparation, and the spells of its scrolls it was to scribe
 * (4888:0640). The camp does this for each member when it starts and ends
 * (4888:06b8). */
void cok_camp_forget(cok_character *c);

/* Take amount of unit (1 minutes ... 4 days) from the rest time, borrowing
 * from larger units; past the days it is all gone (57e4:05ee). Then carry
 * each full unit once into the next, as the clock does, and fold months
 * into days, at most 99 (57e4:0517). Carrying hours into a day moves the
 * moons a day, as the original's clock routine does. */
void cok_camp_subtract(cok_adventure *game, uint8_t amount, unsigned unit);
void cok_camp_normalize(cok_adventure *game);

/* Fix (4888:2b44): heal the party with the cure spells its clerics have
 * memorized and those they could memorize in the time it takes, resting
 * that long first without the rest menu. Returns true if an encounter
 * interrupted the rest; then no one is healed. Stops with
 * game->vm.status COK_ECL_DIVIDE_BY_ZERO where the original divides by
 * zero. */
bool cok_camp_fix(cok_adventure *game);

/* Save the game as letter 'A'-'J' (4b6d:22de, without its prompt): set the
 * speed (0x4bfc), pictures and animation (0x4bff) and ECL file (0x7f12),
 * then write SAVGAM<letter>.DAT and each member's CHRDAT<letter><n> files
 * in game->save_dir, and set DS:5885. As the original checks no write, a
 * file that cannot be written does not stop the others: each failure is
 * logged as "error", and the result is false with game->error the last.
 * With no directory, nothing is written and the result is false. */
bool cok_camp_save_game(cok_adventure *game, char letter);

/* Rest (4888:0f05, also Magic's Rest): as long as the member who needs
 * longest needs to learn and scribe the spells marked for it
 * (cok_camp_preparation), or as the player chooses. Returns true if an
 * encounter interrupted the rest. */
bool cok_camp_prepare(cok_adventure *game);

/* Helpers the camp's menus share. */

/* Say text about character record c in the text window (6346:1883 outside
 * combat): its name on row 18, or 19 while resting (DS:7138), light red if
 * it cannot act, else light cyan (6346:199d), and the text wrapped below
 * in light green. With wait, pause speed * 100 ms and clear rows 18-22
 * (6346:196a). */
void cok_camp_say(cok_adventure *game, const uint8_t *c, const char *text, bool wait);
/* Show text on row 24 in light green for a moment, then clear the row
 * (6346:1827). */
void cok_camp_notice(cok_adventure *game, const char *text);
/* Clear the text window below its first row, rows 18-22 (6346:196a outside
 * combat). */
void cok_camp_clear_text(cok_adventure *game);
/* Say that c did what with spell (5b04:57eb outside combat): its name and
 * what on row 19, the spell's name on row 20, then pause and clear rows
 * 18-22. Returns false, ending the run, where the original would print
 * other data as the name (ids 0 and 0x6c up). */
bool cok_camp_spell_message(cok_adventure *game, const uint8_t *c, const char *what,
                            uint8_t spell);
/* Ask prompt, drawn in prompt_color, until Yes or No (67b5:177f); No is
 * selected first. Returns 'Y', 'N', or -1 if input ended. */
int cok_camp_yes_no(cok_adventure *game, const char *prompt, uint8_t prompt_color);
/* One key from a menu of items on row 24 after prompt (67b5:03e2): prompt
 * in light magenta, hotkeys and the selection in white, other letters in
 * light green; keypad as cok_menu_ask. With show, the small picture's
 * current frame is drawn first. Returns as cok_menu_read. */
int cok_camp_menu(cok_adventure *game, const char *prompt, const char *items, bool keypad,
                  bool show, bool *special);
/* A special key picks a character (546c:3334); redraw the party list. */
void cok_camp_pick(cok_adventure *game, uint8_t scan);

#endif
