#ifndef COK_TEXT_H
#define COK_TEXT_H

#include "picture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 8x8 text from the text unit in START.EXE (code segment 0x521), for the
 * linear Tandy layout. Glyphs are 1-bit, 8 bytes each, top row first, most
 * significant bit leftmost; set bits take the foreground colour. 8X8D1.DAX entry 0 (ID 201) holds 177 glyphs: 0-63 are
 * text, indexed by ASCII modulo 64; the rest are decorations. */
typedef struct {
    size_t count;
    uint8_t *glyphs; /* count * 8 bytes. */
} cok_font;

/* Initialize to {0}; free before reuse. Copies size bytes, which must be a
 * nonzero multiple of 8. */
cok_picture_status cok_font_load(cok_font *font, const uint8_t *data, size_t size);
void cok_font_free(cok_font *font);

/* Text glyph for a character: ASCII upper-cased, then modulo 64. */
uint8_t cok_text_index(char c);

/* Draw glyph count times along a row of 8x8 cells in frame 0 of dst
 * (521:01df). x and y are in cells, as for cok_picture_draw. Cells outside
 * dst, and glyphs past the end of the font, are skipped. Colours use their
 * low four bits. */
void cok_text_glyph(cok_picture *dst, const cok_font *font, unsigned glyph, size_t count,
                    int x, int y, uint8_t fg, uint8_t bg);
/* Fill count cells with colour by drawing spaces (521:030a). */
void cok_text_clear(cok_picture *dst, const cok_font *font, size_t count, int x, int y,
                    uint8_t color);
/* Draw a string, one cell per character, mapped by cok_text_index (521:0353). */
void cok_text_string(cok_picture *dst, const cok_font *font, const char *text, int x, int y,
                     uint8_t fg, uint8_t bg);

/* Where the next wrapped character goes, in cells (DS:6134, DS:6135). */
typedef struct {
    int x, y;
} cok_text_cursor;

/* Inclusive cell bounds of a text window. */
typedef struct {
    int left, top, right, bottom;
} cok_text_window;

/* Platform calls made while wrapping; either may be NULL. */
typedef struct {
    /* The window is full and "Press any key to continue" is on row 24: wait
     * for a key, then discard pending keys (1614:025b, 1614:045c). */
    void (*page)(void *context);
    /* After each character, when the game's text delay is on (the original
     * calls Delay(DS:4b38 * 3) if DS:4b59 is set). */
    void (*delay)(void *context);
    void *context;
} cok_text_hooks;

/* Print text word by word inside window from the cursor (521:04ac). A
 * cursor outside the window moves to its top left. clear first blanks the
 * window to colour 0 and homes the cursor. Words break after spaces and runs
 * of ! , - . : ; ? and keep their trailing space. A word that would pass the
 * right edge starts a new line; one that starts below the bottom row shows
 * the prompt, calls hooks->page, clears the window and prints from its top
 * left. The window must fit 40x25 cells, except that bottom may be up to 39,
 * as in the original; other windows print nothing. */
void cok_text_wrap(cok_picture *dst, const cok_font *font, cok_text_cursor *cursor,
                   const char *text, cok_text_window window, uint8_t fg, uint8_t bg,
                   bool clear, const cok_text_hooks *hooks);

/* Keys as the game reads them (Crt.ReadKey, 1962:030f): an ASCII code, or 0
 * and then a scan code for an extended key. read returns a negative value
 * when input has ended; routines waiting for keys then return what they
 * have. */
typedef struct {
    int (*read)(void *context);
    void *context;
} cok_keyboard;

/* Edit a line on row 24 after prompt, which is cut to 40 characters and
 * drawn at column 0 in fg on bg (521:0739). Keys 0x20-0x7a append while the
 * line is shorter than max; each is drawn in colour 15 one column further
 * right, so the first leaves a blank column after the prompt. Backspace
 * erases the last character; extended keys are ignored. Enter or Escape
 * ends the line; then row 24 is cleared and out gets the line upper-cased,
 * cut to 40 characters. Returns false if input ended first, with out set as
 * for Enter. */
bool cok_text_input(cok_picture *dst, const cok_font *font, const char *prompt, uint8_t fg,
                    uint8_t bg, size_t max, const cok_keyboard *keys, char out[41]);

/* Read lines of up to six characters until Turbo Pascal's Val reads one as a
 * number from 0 to 65536, and return its low word (521:08d9). Val skips
 * leading spaces, takes a sign and $ for hexadecimal, and rejects anything
 * else, including an empty line. Returns false if input ended first. */
bool cok_text_number(cok_picture *dst, const cok_font *font, const char *prompt, uint8_t fg,
                     uint8_t bg, const cok_keyboard *keys, uint16_t *value);

/* Turbo Pascal's Val for a LongInt (1a46:136a): returns the error position,
 * 0 when text is a number. */
unsigned cok_tp_val(const char *text, long *value);

#endif
