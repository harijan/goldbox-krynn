#ifndef COK_KEYMAP_H
#define COK_KEYMAP_H

#include <SDL.h>

#include <stdbool.h>
#include <stdint.h>

/* The codes a PC keyboard gives the game for a key, as Turbo Pascal's
 * Crt.ReadKey (1962:030f) reads them through INT 16h function 0: an ASCII
 * code, or 0 and then a scan code. Function 0 drops the keys only an
 * enhanced keyboard has (F11, F12, Alt with the arrows or Enter, Ctrl with
 * Up or Down), so they give the game nothing and are left to the front end.
 * Startup (3e99:005b) turns NumLock on (0040:0017 bit 5), so the keypad
 * types digits, as the game's keypad tables (DS:1fdf) expect, and with
 * Shift gives the cursor keys; the host's NumLock is not looked at (see
 * cok_keymap_event for the NumLock key).
 * Letters and the punctuation unshifted follow the host's layout (the
 * key's SDL keycode); shifted punctuation is the US layout's. */

/* The codes for key, its modifiers mod (KMOD_*): returns how many (0, 1 or
 * 2) were stored in codes. Alt (either) wins over Ctrl, Ctrl over Shift;
 * Shift and CapsLock make letters capitals; GUI or AltGr (KMOD_MODE) give
 * nothing. */
int cok_keymap_codes(SDL_Keycode key, SDL_Scancode scancode, Uint16 mod, int codes[2]);

/* What the BIOS's keyboard handler (INT 9) keeps between keys: NumLock
 * (0040:0017 bit 5), on at startup ({0}), and the number typed on the
 * keypad while Alt is held (0040:0019). */
typedef struct {
    bool numlock_off;
    uint8_t alt_code;
} cok_keymap_state;

/* A key going down (SDL_KEYDOWN, its repeats too) or up (SDL_KEYUP), as
 * INT 9 takes it: the NumLock key toggles NumLock (not as it repeats, nor
 * with Ctrl, which makes it Pause), and with it off the keypad gives the
 * cursor keys and with Shift the digits; Alt (with Ctrl too) and keypad
 * digits make a number (times ten plus the digit, as a byte) that an
 * Alt's release types as that character code if it is not 0, clearing
 * it; Shift, Ctrl, the lock keys and space leave it, any other key with
 * Alt starts it again at 0. Otherwise as
 * cok_keymap_codes, and nothing for a key going up. Returns how many
 * codes were stored. */
int cok_keymap_event(cok_keymap_state *state, const SDL_KeyboardEvent *event, int codes[2]);

/* A key, for a key script: a scancode, keycode (the US layout's) and
 * modifiers that cok_keymap_codes turns into code, or into 0 and scan when
 * code is 0. Returns false if no key gives them. */
bool cok_keymap_find(int code, int scan, SDL_Keysym *key);

#endif
