#ifndef COK_KEYMAP_H
#define COK_KEYMAP_H

#include <SDL.h>

#include <stdbool.h>

/* The codes a PC keyboard gives the game for a key, as Turbo Pascal's
 * Crt.ReadKey (1962:030f) reads them through INT 16h function 0: an ASCII
 * code, or 0 and then a scan code. Function 0 drops the keys only an
 * enhanced keyboard has (F11, F12, Alt with the arrows or Enter, Ctrl with
 * Up or Down), so they give the game nothing and are left to the front end.
 * Startup (3e99:005b) turns NumLock on (0040:0017 bit 5), so the keypad
 * types digits, as the game's keypad tables (DS:1fdf) expect, and with
 * Shift gives the cursor keys; the host's NumLock is not looked at.
 * Letters and the punctuation unshifted follow the host's layout (the
 * key's SDL keycode); shifted punctuation is the US layout's. */

/* The codes for key, its modifiers mod (KMOD_*): returns how many (0, 1 or
 * 2) were stored in codes. Alt (either) wins over Ctrl, Ctrl over Shift;
 * Shift and CapsLock make letters capitals; GUI or AltGr (KMOD_MODE) give
 * nothing. */
int cok_keymap_codes(SDL_Keycode key, SDL_Scancode scancode, Uint16 mod, int codes[2]);

/* A key, for a key script: a scancode, keycode (the US layout's) and
 * modifiers that cok_keymap_codes turns into code, or into 0 and scan when
 * code is 0. Returns false if no key gives them. */
bool cok_keymap_find(int code, int scan, SDL_Keysym *key);

#endif
