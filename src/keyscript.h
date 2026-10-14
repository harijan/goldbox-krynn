#ifndef COK_KEYSCRIPT_H
#define COK_KEYSCRIPT_H

#include "text.h"

#include <stdbool.h>

/* Key scripts, as eclplay's --keys and cok's --keys give them: each
 * character is its ASCII code, and backslash escapes give the rest:
 * \r Enter (13), \e Escape (27), \b Backspace (8), \< \> \^ \v the arrows
 * (0, then scan code 0x4b, 0x4d, 0x48 or 0x50), \@X Alt and letter X (0,
 * then the letter's scan code), \cX Ctrl and letter X (1-26: \cD, \cZ and
 * \cC are the debug mode's Ctrl-D, Ctrl-Z and Ctrl-C, \cS Ctrl-S), \#HH
 * the extended key of scan code HH in hex (0, then HH), \t the time running out in a menu that times out
 * (COK_KEY_TIMEOUT), \\ a backslash; \k presses the next key while the
 * game polls for one (1614:03c2) rather than reads it.
 *
 * Keys are typed one at a time, as the game asks for them, except a group
 * \[KEYS\], typed ahead: all of KEYS go into the keyboard's buffer at once
 * (15 keystrokes, as the BIOS's holds; more are lost), at the game's next
 * wait (a delay or a poll), or at its next key read, whichever comes
 * first; there they wait to be read, or are lost where the game flushes
 * the keyboard (1614:045c). \wN. (N in decimal) holds back what follows
 * until N more of the game's waits have passed, so that a group arrives
 * while the game is busy (a key pressed in the middle of the title's wait
 * or of a rest); a key read ends the wait at once. \pN. pauses N
 * hundredths of a second, in real time, before the next key is typed as
 * the game reads it, where there is a clock (cok; eclplay passes over it). */
typedef struct {
    const char *keys;     /* What is left of the script. */
    int pending;          /* The scan code after a 0, or -1. */
    unsigned long wait;   /* The game's waits left before what follows \w. */
} cok_key_script;

/* A keystroke: an ASCII code (scan -1), or 0 and the scan code. */
typedef struct {
    int code, scan;
} cok_keystroke;

enum { COK_KEYS_BUFFERED = 15 }; /* Keystrokes the BIOS's buffer holds. */

/* Start a script; NULL is an empty one. */
cok_key_script cok_key_script_start(const char *keys);

/* The next code the game reads (Crt.ReadKey): the scan code owed after a
 * 0 first, else the next key, skipping a \k before it, a wait (\wN.) and
 * a group's brackets; -1 when the script has ended. */
int cok_key_script_read(cok_key_script *script);

/* Whether a scan code is owed after a 0 that was read. */
bool cok_key_script_owed(const cok_key_script *script);

/* A poll for a key (KeyPressed): true, and the \k dropped, when the script
 * goes on with \k; the key after it is then read as any other. */
bool cok_key_script_poll(cok_key_script *script);

/* One of the game's waits (a delay or a poll) has passed: counts down \w. */
void cok_key_script_tick(cok_key_script *script);

/* Keys typed ahead now: when the script goes on with a group and no wait
 * holds it back (with now, as at a key read, the wait ends), its
 * keystrokes, of which the first max go in strokes and the rest are lost;
 * returns how many went in. */
size_t cok_key_script_ahead(cok_key_script *script, bool now, cok_keystroke *strokes,
                            size_t max);

/* At \pN.: N, a pause before the next key is typed, in hundredths of a
 * second, which only a front end with a clock (cok) keeps; reads skip it.
 * Returns false where the script goes on otherwise. */
bool cok_key_script_pause(cok_key_script *script, unsigned long *hundredths);

/* Whether the script has ended (nothing owed, nothing left). */
bool cok_key_script_done(const cok_key_script *script);

#endif
