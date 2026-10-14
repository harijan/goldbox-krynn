#ifndef COK_CRT_H
#define COK_CRT_H

#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The PC's text screen as startup (11c7:031a) writes it before it sets a
 * graphics mode, through Turbo Pascal 6's Crt unit (1962) and System's
 * text files (1a46): 80 columns by 25 rows of characters and attributes,
 * the window the whole screen (nothing calls Window), every character in
 * TextAttr (DS:8a4a: the attribute under DOS's cursor, normally 07, light
 * grey on black; nothing changes it). The port keeps the cells and shows
 * them as the CGA's and the Tandy's 80-column mode did, 8x8 cells on
 * 640x200, in the game's font (8X8D1.DAX, upper case: the port has no PC
 * character ROM; $ % @ [ \ ] ^ _ and ` { | } ~, which it lacks, drawn
 * here). */
enum {
    COK_CRT_COLUMNS = 80,
    COK_CRT_ROWS = 25,
    COK_CRT_WIDTH = COK_CRT_COLUMNS * 8,
    COK_CRT_HEIGHT = COK_CRT_ROWS * 8,
    COK_CRT_ATTR = 0x07,
    /* Input's buffer holds 128 bytes; the line editor keeps two for CR LF. */
    COK_CRT_LINE = 126,
};

typedef struct {
    uint8_t chars[COK_CRT_ROWS][COK_CRT_COLUMNS];
    uint8_t attrs[COK_CRT_ROWS][COK_CRT_COLUMNS];
    int x, y;              /* The cursor (0040:0050). */
    uint8_t attr;          /* TextAttr. */
    /* Input's buffer as the line editor (1962:0394) leaves it, and how far
     * it has been filled, which ^D and ^F recall up to. */
    uint8_t line[COK_CRT_LINE + 2];
    size_t line_end;
    /* Rows written since they were last logged: each is logged, without
     * its trailing spaces, as a line feed leaves it (written or not: an
     * empty WriteLn logs an empty row) or a character wraps past it (so the
     * row a scroll drops is always logged), before ClrScr blanks it, and by
     * cok_crt_log_rows. */
    bool written[COK_CRT_ROWS];
    void (*log)(const char *row, void *context);
    void *context;
} cok_crt;

/* A blank screen in COK_CRT_ATTR, the cursor home, Input's buffer empty. */
void cok_crt_init(cok_crt *crt);
/* ClrScr (1962:01c0): every cell a space in TextAttr, the cursor home. */
void cok_crt_clear(cok_crt *crt);
/* Write characters through Output (1962:0506): BEL (7) beeps, BS (8)
 * moves back a column but not past the first, LF (10) a row down and CR
 * (13) to the first column; others are written in TextAttr, the cursor
 * moving on, to the next row past the last column; a row down from the
 * last scrolls the screen up, the new row blank in TextAttr. */
void cok_crt_write(cok_crt *crt, const char *text, size_t length);
void cok_crt_write_string(cok_crt *crt, const char *text);
/* WriteLn: text, then CR LF. */
void cok_crt_write_line(cok_crt *crt, const char *text);
/* ReadLn(Input, s) for a string[max] (1a46:08e1, 1a46:081c): Crt's line
 * editor (1962:0394) reads keys (Crt.ReadKey) until Enter, echoing them:
 * at most COK_CRT_LINE characters of 0x20 or more; Backspace or Ctrl-S
 * erases one, Escape or Ctrl-A the line; Ctrl-D recalls one character the
 * buffer still holds past the line, Ctrl-F all of them; other control
 * characters and extended keys (the 0 and its scan code, which ReadKey's
 * next call is made to drop) do nothing; Enter echoes CR LF. The string
 * gets the first max characters. Returns false, with what was typed, if
 * input ended. */
bool cok_crt_read_line(cok_crt *crt, const cok_keyboard *keys, char *out, size_t max);
/* Log the rows written and not logged yet. */
void cok_crt_log_rows(cok_crt *crt);

/* The screen as the PC showed it, COK_CRT_WIDTH by COK_CRT_HEIGHT pixels
 * of the 16 text colours, two a byte, the left one in the high nibble:
 * each cell's glyph in its attribute's foreground (low four bits) over its
 * background (bits 4-6; bit 7 blinks, which TextAttr never has), and the
 * cursor's two bottom lines in the foreground of its cell (it blinked;
 * here it stays). Without a font the cells show only their backgrounds. */
void cok_crt_render(const cok_crt *crt, const cok_font *font, uint8_t *pixels);

#endif
