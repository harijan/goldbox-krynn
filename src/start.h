#ifndef COK_START_H
#define COK_START_H

#include "adventure.h"

#include <stdbool.h>

/* The game's front end: main's loop (1000:0134) with the title screen
 * (2f55:063c) and its menu, the start menu (4def:01b4), the state the game
 * starts and ends with (3e99:005b, 3e99:0843), and the demonstration
 * (DS:4b4b). Characters are made in create.h, trained in train.h,
 * modified in modify.h and kept in the roster (roster.h). */

/* The text of the start menu's item i, 0-12 (DS:0878 + 0x2a * i), or
 * NULL. */
const char *cok_start_item(unsigned i);

/* Pick Character (546c:36cc): "Pick Character " and "Select Exit", with
 * the special keys picking the character (546c:3334) and the party list
 * redrawn after each. Returns true for Select; Exit or Escape returns
 * false, and space or other keys are ignored. */
bool cok_start_pick(cok_adventure *game);

/* The start menu (4def:01b4), with the mode set to 0, the party menu,
 * meanwhile: its items as the party and var 0x7ea8 allow them, in a list
 * in cells 9-38 by 12-22 with "Choose a FUNCTION " and "Select ", until
 * Begin Adventuring returns, restoring the mode and clearing var 0x7ea8
 * (or the player quits or input ends: game->vm.abort). The command is the
 * first letter of the row picked; Escape is the letter 0, which does
 * nothing. Every command but Save and Exit marks the game unsaved
 * (DS:5885). Main calls it before each game, and PROGRAM 0 from the
 * scripts' training halls. */
void cok_start_menu(cok_adventure *game);

/* Carry out the start menu's command letter, as the row it begins picked
 * it (4def:0383-0647): C D M T K V A R L S B I E, and J, Helm's free
 * training, which no row begins with. Returns true for Begin when it
 * returns. */
bool cok_start_command(cok_adventure *game, char letter);

/* The state the game starts with (3e99:005b) beyond what
 * cok_adventure_open sets: the variables cleared but the difficulty
 * (0x4cf4) 3, a 3D area (0x4be6), moons in phase 2, the party at 7, 13
 * facing north, and the first row a list shows 1 (DS:6e0d). */
void cok_start_startup(cok_adventure *game);
/* The end of a game (3e99:0843): the same, facing south, with the speed 4,
 * the mode 4, the selection and pictures forgotten, the ECL file 1 and
 * row 24 cleared. */
void cok_start_reset(cok_adventure *game);

/* The title screen (2f55:063c): TITLE.DAX records 1, then 2 with 3 over it,
 * then 4, 5, 10 and 10 seconds each, then the credits (2f55:0267) for 10;
 * a key pressed while one waits (hooks.key_pending) ends the title, and
 * is thrown away. */
void cok_start_title(cok_adventure *game);

/* Initialize Mouse/Joystick (4def:5cc7): its list of "mouse OFF",
 * "joystick OFF" and "disable both" until Exit, Escape or space; the
 * mouse and joystick are not ported, so turning either on finds none. */
void cok_start_mouse(cok_adventure *game);

/* The game as main runs it (1000:0134), after cok_adventure_open: the
 * startup state, the title unless woof (ParamStr(1) 'Woof'), the title
 * menu, whose timeout (COK_KEY_TIMEOUT) plays the demonstration; then
 * game after game: the start menu, the adventure from its first block
 * (2fd3:3c28; 0x24, or the block a loaded game was saved in) until the
 * party dies, then the end of the game. Returns when input ends, the
 * player quits or a run fails, with the run's status. */
cok_ecl_status cok_start_game(cok_adventure *game, bool woof);

#endif
