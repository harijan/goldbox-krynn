#ifndef COK_DEBUG_H
#define COK_DEBUG_H

#include "rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The original's debug mode (see Debug mode in DEVELOPERS.md). Its flag,
 * DS:4b51, is toggled by Ctrl-D, which the key reader (1614:025b) takes,
 * with Ctrl-Z and Ctrl-C, only in a game started with Woof (ParamStr(1),
 * 1614:024c). While it is set, DEBUG.TXT is open (the Text file at
 * DS:8864): Ctrl-Z writes the party's effects there and loading a tile
 * set (6e22:0050) its slot; the trace (6d7e:093e) shows three numbers on
 * row 0 and a name on row 24 and waits for a key, after every ECL opcode
 * (2fd3:3add), in SPELL, at the end of a battle's setup and in effect
 * 0x07; the status line and the combat menu end with "*", and Items
 * offers View (546c:12be). */

typedef struct cok_adventure cok_adventure;

enum { COK_DEBUG_BUFFER = 128 }; /* A Text file's buffer (TextRec.Buffer). */

/* DEBUG.TXT, written as Turbo Pascal writes a Text file on a disk: lines
 * end CR LF and go into the file's 128-byte buffer, which is written out
 * when full and when the file is closed. The port keeps it in the saves
 * directory; with none, its lines are only logged. */
typedef struct {
    FILE *file;      /* The handle Rewrite opened, kept after an abandon. */
    bool open;       /* Assigned and rewritten, not closed (fmOutput). */
    uint8_t buffer[COK_DEBUG_BUFFER];
    size_t used;     /* BufPos. */
} cok_debug_file;

/* 1614:025b's debug keys, after the key is read and Ctrl-S handled, in a
 * game started with Woof (game->woof): Ctrl-D (4) toggles the flag,
 * opening DEBUG.TXT anew (Assign, Rewrite) as it is set and closing it as
 * it is cleared; Ctrl-Z (0x1a) while it is set writes, for each record
 * of the list from its first and each of its effects in turn, "who: NAME
 * sp#: ID" (1614:007b); Ctrl-C (3) quits to DOS (1614:0000,
 * cok_adventure_quit). Returns the key, as the original does, or -1 once
 * Ctrl-C has quit. */
int cok_debug_key(cok_adventure *game, int key);

/* The trace (6d7e:093e): with the flag set, row 0 cleared and "A , B , C"
 * drawn there in yellow, text (cut to 40) on row 24 in yellow, a key
 * read (1521:096c), and row 0 cleared again. Nothing without the flag,
 * nor once the run has ended or failed. */
void cok_debug_trace(cok_adventure *game, const char *text, uint8_t a, uint8_t b, uint8_t c);

/* The name the trace gives an opcode (6d7e:024f): the trace table's
 * (cok_ecl_opcode_name, from 6d7e:0005-0244), whose IF = has a space
 * after it there, and "BULLSHIT N" (6d7e:0245) for the opcodes it does
 * not name. */
void cok_debug_opcode_name(const cok_rules *rules, unsigned opcode, char out[16]);

/* View's item (546c:12be): the open frame (1128:0000 with 1), its fields'
 * names from row 1 at column 1 and their values at column 20, in light
 * green, and "press a key" on row 24. Returns false, the run stopped,
 * for an item type past the table of types (DS:5886), which the original
 * reads past. */
bool cok_debug_item(cok_adventure *game, const uint8_t *item);

/* A record's name (its string, NAME, at +0) whole, as Write and the trace
 * take a string: not cut to the 15 characters the screens show (name_of in
 * the other modules), so that a record's length byte is followed as the
 * original follows it, past the name's 16 bytes. */
void cok_debug_name(const uint8_t *record, const cok_rules *rules, char out[256]);

/* WriteLn(DEBUG.TXT, line), logged as "debug.txt". Only while open. */
void cok_debug_write(cok_adventure *game, const char *line);

/* Close DEBUG.TXT (Close: the buffer written out). */
void cok_debug_close(cok_adventure *game);

/* The flag cleared by 3e99:0843 (main's reset after each adventure)
 * without closing the file: nothing writes to it while the flag is clear,
 * what its buffer holds is never written, and the next Ctrl-D assigns it
 * anew. */
void cok_debug_reset(cok_adventure *game);

/* The port's end of a run (cok_adventure_close): DEBUG.TXT closed if the
 * flag is set, as quitting would, else its handle dropped with what its
 * buffer holds. */
void cok_debug_end(cok_adventure *game);

#endif
