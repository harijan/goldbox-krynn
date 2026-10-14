#include "crt.h"

#include <string.h>

/* Log row y if it was written since it was last logged. */
static void log_row(cok_crt *crt, int y)
{
    if (!crt->written[y]) return;
    crt->written[y] = false;
    if (crt->log == NULL) return;
    char text[COK_CRT_COLUMNS + 1];
    size_t n = COK_CRT_COLUMNS;
    while (n > 0 && crt->chars[y][n - 1] == ' ') --n;
    memcpy(text, crt->chars[y], n);
    text[n] = '\0';
    crt->log(text, crt->context);
}

void cok_crt_log_rows(cok_crt *crt)
{
    for (int y = 0; y < COK_CRT_ROWS; ++y) log_row(crt, y);
}

static void blank_row(cok_crt *crt, int y)
{
    memset(crt->chars[y], ' ', COK_CRT_COLUMNS);
    memset(crt->attrs[y], crt->attr, COK_CRT_COLUMNS);
    crt->written[y] = false;
}

void cok_crt_init(cok_crt *crt)
{
    void (*log)(const char *, void *) = crt->log;
    void *context = crt->context;
    memset(crt, 0, sizeof *crt);
    crt->log = log;
    crt->context = context;
    crt->attr = COK_CRT_ATTR;
    for (int y = 0; y < COK_CRT_ROWS; ++y) blank_row(crt, y);
}

void cok_crt_clear(cok_crt *crt)
{
    cok_crt_log_rows(crt);
    /* INT 10h AX=0600 over the window in TextAttr, then AH=02 to 0,0. */
    for (int y = 0; y < COK_CRT_ROWS; ++y) blank_row(crt, y);
    crt->x = crt->y = 0;
}

/* LineFeed (1962:04d7): a row down; past the last, INT 10h AX=0601 scrolls
 * the window up a row, the new one blank in TextAttr. */
static void line_feed(cok_crt *crt)
{
    crt->written[crt->y] = true; /* logged as it is left, written or not */
    log_row(crt, crt->y);
    if (crt->y < COK_CRT_ROWS - 1) {
        ++crt->y;
        return;
    }
    /* Row 0, written only with the cursor on it, was logged as it left. */
    memmove(crt->chars[0], crt->chars[1], sizeof crt->chars[0] * (COK_CRT_ROWS - 1));
    memmove(crt->attrs[0], crt->attrs[1], sizeof crt->attrs[0] * (COK_CRT_ROWS - 1));
    memmove(crt->written, crt->written + 1, sizeof crt->written[0] * (COK_CRT_ROWS - 1));
    blank_row(crt, COK_CRT_ROWS - 1);
}

/* One character as DirectWrite (1962:0506) and BiosWriteChar (047e),
 * which the line editor echoes through, write it: the same cells. */
static void put(cok_crt *crt, uint8_t c)
{
    switch (c) {
    case 7: break; /* INT 10h AX=0E07: the speaker beeps */
    case 8:
        if (crt->x > 0) --crt->x;
        break;
    case 10: line_feed(crt); break;
    case 13: crt->x = 0; break;
    default:
        crt->chars[crt->y][crt->x] = c;
        crt->attrs[crt->y][crt->x] = crt->attr;
        crt->written[crt->y] = true;
        if (++crt->x >= COK_CRT_COLUMNS) {
            line_feed(crt);
            crt->x = 0;
        }
        break;
    }
}

void cok_crt_write(cok_crt *crt, const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i) put(crt, (uint8_t)text[i]);
}

void cok_crt_write_string(cok_crt *crt, const char *text)
{
    cok_crt_write(crt, text, strlen(text));
}

void cok_crt_write_line(cok_crt *crt, const char *text)
{
    cok_crt_write_string(crt, text);
    cok_crt_write(crt, "\r\n", 2);
}

/* The line editor, CrtInput (1962:0394): fills Input's buffer with a line
 * and CR LF. Returns false if input ended. */
static bool edit(cok_crt *crt, const cok_keyboard *keys, size_t *length)
{
    size_t n = 0, recall = crt->line_end; /* bx, si: BufPos on entry */
    for (;;) {
        int key = keys->read(keys->context);
        if (key < 0 && key != COK_KEY_TIMEOUT) {
            *length = n;
            return false;
        }
        if (key == 0) {
            /* 1962:03aa clears ScanCode before each ReadKey: an extended
             * key's scan code, which the keyboard owes now, is dropped. */
            int scan = keys->read(keys->context);
            if (scan < 0 && scan != COK_KEY_TIMEOUT) {
                *length = n;
                return false;
            }
            continue;
        }
        if (key < 0) continue;
        uint8_t c = (uint8_t)key;
        if (c == 8 || c == 0x13 || c == 0x1b || c == 1) {
            /* Backspace, Ctrl-S: one; Escape, Ctrl-A: all (loop with CX 0). */
            bool all = c == 0x1b || c == 1;
            while (n > 0) {
                put(crt, 8);
                put(crt, ' ');
                put(crt, 8);
                --n;
                if (!all) break;
            }
        } else if (c == 4 || c == 6) {
            /* Ctrl-D: one character the buffer holds past n; Ctrl-F: all. */
            bool all = c == 6;
            while (n != recall && crt->line[n] >= 0x20) {
                put(crt, crt->line[n]);
                ++n;
                if (!all) break;
            }
        } else if (c == 0x1a) {
            /* Ctrl-Z ends the input only with CheckEOF, which is off. */
        } else if (c == 13) {
            put(crt, 13);
            put(crt, 10);
            crt->line[n] = 13;
            crt->line[n + 1] = 10;
            *length = n + 2;
            return true;
        } else if (c >= 0x20 && n < COK_CRT_LINE) {
            crt->line[n++] = c;
            put(crt, c);
            if (n > recall) recall = n;
        }
    }
}

bool cok_crt_read_line(cok_crt *crt, const cok_keyboard *keys, char *out, size_t max)
{
    size_t length;
    bool ended = !edit(crt, keys, &length);
    /* 1a46:08e1: up to max characters before the CR; 081c skips the rest
     * and the CR LF, so that Input's BufPos ends at the line's end. */
    size_t n = 0;
    while (n < length && n < max && crt->line[n] != 13) {
        out[n] = (char)crt->line[n];
        ++n;
    }
    out[n] = '\0';
    crt->line_end = length;
    return !ended;
}

/* Characters the game's font holds no glyph for, its places 0, 4, 5 and
 * 27-31 holding pieces of frames: drawn here (and lower case's neighbours
 * `{|}~ too). */
static const struct {
    char c;
    uint8_t bits[8];
} own[] = {
    {'$', {0x30, 0x7c, 0xc0, 0x78, 0x0c, 0xf8, 0x30, 0x00}},
    {'%', {0x00, 0xc6, 0xcc, 0x18, 0x30, 0x66, 0xc6, 0x00}},
    {'@', {0x7c, 0xc6, 0xde, 0xde, 0xde, 0xc0, 0x78, 0x00}},
    {'[', {0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c, 0x00}},
    {'\\', {0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x02, 0x00}},
    {']', {0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c, 0x00}},
    {'^', {0x10, 0x38, 0x6c, 0xc6, 0x00, 0x00, 0x00, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff}},
    {'`', {0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'{', {0x1c, 0x30, 0x30, 0xe0, 0x30, 0x30, 0x1c, 0x00}},
    {'|', {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}},
    {'}', {0xe0, 0x30, 0x30, 0x1c, 0x30, 0x30, 0xe0, 0x00}},
    {'~', {0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};

static const uint8_t *glyph_of(const cok_font *font, uint8_t c)
{
    for (size_t i = 0; i < sizeof own / sizeof own[0]; ++i)
        if ((uint8_t)own[i].c == c) return own[i].bits;
    unsigned glyph = cok_text_index((char)c);
    if (font == NULL || font->glyphs == NULL || glyph >= font->count) return NULL;
    return font->glyphs + glyph * 8;
}

void cok_crt_render(const cok_crt *crt, const cok_font *font, uint8_t *pixels)
{
    enum { ROW_BYTES = COK_CRT_WIDTH / 2 };
    for (int y = 0; y < COK_CRT_ROWS; ++y) {
        for (int x = 0; x < COK_CRT_COLUMNS; ++x) {
            unsigned attr = crt->attrs[y][x];
            unsigned fg = attr & 15, bg = (attr >> 4) & 7;
            const uint8_t *bits = glyph_of(font, crt->chars[y][x]);
            bool cursor = x == crt->x && y == crt->y;
            for (int row = 0; row < 8; ++row) {
                unsigned b = bits != NULL ? bits[row] : 0;
                if (cursor && row >= 6) b = 0xff;
                uint8_t *d = pixels + (size_t)(y * 8 + row) * ROW_BYTES + (size_t)x * 4;
                for (int i = 0; i < 4; ++i) {
                    unsigned two = b >> (6 - 2 * i);
                    d[i] = (uint8_t)(((two & 2u) ? fg : bg) << 4 | ((two & 1u) ? fg : bg));
                }
            }
        }
    }
}
