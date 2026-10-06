#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cok_picture_status cok_font_load(cok_font *font, const uint8_t *data, size_t size)
{
    *font = (cok_font){0};
    if (data == NULL || size == 0 || size % 8 != 0) return COK_PICTURE_INVALID;
    uint8_t *glyphs = malloc(size);
    if (glyphs == NULL) return COK_PICTURE_MEMORY;
    memcpy(glyphs, data, size);
    *font = (cok_font){size / 8, glyphs};
    return COK_PICTURE_OK;
}

void cok_font_free(cok_font *font)
{
    free(font->glyphs);
    *font = (cok_font){0};
}

uint8_t cok_text_index(char c)
{
    unsigned byte = (unsigned char)c;
    if (byte >= 'a' && byte <= 'z') byte -= 'a' - 'A';
    return (uint8_t)(byte % 64);
}

/* One cell, as the Tandy blitter writes it: two pixels per byte, left pixel
 * in the high nibble. */
static void cell(cok_picture *dst, const uint8_t *glyph, size_t x, size_t y,
                 unsigned fg, unsigned bg)
{
    size_t row_bytes = (size_t)dst->units * 4;
    for (size_t row = 0; row < 8; ++row) {
        uint8_t *d = dst->pixels + (y * 8 + row) * row_bytes + x * 4;
        for (size_t i = 0; i < 4; ++i) {
            unsigned bits = (unsigned)glyph[row] >> (6 - 2 * i);
            d[i] = (uint8_t)(((bits & 2u) ? fg : bg) << 4 | ((bits & 1u) ? fg : bg));
        }
    }
}

void cok_text_glyph(cok_picture *dst, const cok_font *font, unsigned glyph, size_t count,
                    int x, int y, uint8_t fg, uint8_t bg)
{
    if (font->glyphs == NULL || glyph >= font->count || y < 0 ||
        (size_t)y >= dst->height / 8)
        return;
    const uint8_t *bits = font->glyphs + (size_t)glyph * 8;
    for (long long column = x < 0 ? 0 : x;
         column < dst->units && (unsigned long long)(column - x) < count; ++column)
        cell(dst, bits, (size_t)column, (size_t)y, fg & 15, bg & 15);
}

void cok_text_clear(cok_picture *dst, const cok_font *font, size_t count, int x, int y,
                    uint8_t color)
{
    cok_text_glyph(dst, font, cok_text_index(' '), count, x, y, color, color);
}

void cok_text_string(cok_picture *dst, const cok_font *font, const char *text, int x, int y,
                     uint8_t fg, uint8_t bg)
{
    size_t length = strlen(text);
    for (long long column = x < 0 ? 0 : x;
         column < dst->units && (unsigned long long)(column - x) < length; ++column)
        cok_text_glyph(dst, font, cok_text_index(text[column - x]), 1, (int)column, y, fg, bg);
}

/* Characters that end a word and stay with it (the set at DS:0f38). */
static bool joins(char c)
{
    return c != '\0' && strchr("!,-.:;?", c) != NULL;
}

/* Blank the window's cells to colour 0 (521:0b60). */
static void clear_window(cok_picture *dst, cok_text_window window)
{
    cok_picture_fill(dst, window.left, window.top * 8, (size_t)(window.right - window.left + 1),
                     (size_t)(window.bottom - window.top + 1) * 8, 0);
}

/* Print characters start..last (1-based, inclusive) at the cursor, leaving
 * start just past them (nested routine 521:03e2). */
static void print_word(cok_picture *dst, const cok_font *font, cok_text_cursor *cursor,
                       const char *text, size_t *start, size_t last, uint8_t fg, uint8_t bg,
                       const cok_text_hooks *hooks)
{
    for (; *start <= last; ++*start, ++cursor->x) {
        cok_text_glyph(dst, font, cok_text_index(text[*start - 1]), 1, cursor->x, cursor->y, fg,
                       bg);
        if (hooks != NULL && hooks->delay != NULL) hooks->delay(hooks->context);
    }
}

void cok_text_wrap(cok_picture *dst, const cok_font *font, cok_text_cursor *cursor,
                   const char *text, cok_text_window w, uint8_t fg, uint8_t bg, bool clear,
                   const cok_text_hooks *hooks)
{
    if (w.left < 0 || w.top < 0 || w.right < w.left || w.bottom < w.top || w.right > 39 ||
        w.top > 24 || w.bottom > 39)
        return;
    if (cursor->x < w.left || cursor->x > w.right || cursor->y < w.top || cursor->y > w.bottom)
        *cursor = (cok_text_cursor){w.left, w.top};
    if (clear) {
        clear_window(dst, w);
        *cursor = (cok_text_cursor){w.left, w.top};
    }

    /* Indexes are 1-based, as in the Pascal string. */
    size_t length = strlen(text);
    size_t start = 1;
    while (start <= length) {
        size_t last = start;
        while (last < length && joins(text[last - 1])) ++last;
        while (last < length && !joins(text[last - 1]) && text[last - 1] != ' ') ++last;
        if (text[last - 1] != ' ')
            while (last + 1 < length && joins(text[last])) ++last;

        if (cursor->x + (long long)(last - start) <= w.right) {
            print_word(dst, font, cursor, text, &start, last, fg, bg, hooks);
            continue;
        }
        /* The original then tests whether only a trailing space overflows,
         * but under a condition that can never hold, so that case is
         * omitted. Without a page break the word is retried on the new line. */
        *cursor = (cok_text_cursor){w.left, cursor->y + 1};
        while (start < length && text[start - 1] == ' ') ++start;
        if (cursor->y > w.bottom && start < length) {
            *cursor = (cok_text_cursor){w.left, w.top};
            cok_text_clear(dst, font, 40, 0, 24, 0);
            cok_text_string(dst, font, "Press any key to continue", 0, 24, 13, 0);
            if (hooks != NULL && hooks->page != NULL) hooks->page(hooks->context);
            clear_window(dst, w);
            print_word(dst, font, cursor, text, &start, last, fg, bg, hooks);
        }
    }
    if (cursor->x > w.right) *cursor = (cok_text_cursor){w.left, cursor->y + 1};
}

static int read_key(const cok_keyboard *keys)
{
    return keys == NULL || keys->read == NULL ? -1 : keys->read(keys->context);
}

bool cok_text_input(cok_picture *dst, const cok_font *font, const char *prompt, uint8_t fg,
                    uint8_t bg, size_t max, const cok_keyboard *keys, char out[41])
{
    char shown[41];
    snprintf(shown, sizeof shown, "%s", prompt);
    cok_text_clear(dst, font, 40, 0, 24, 0);
    cok_text_string(dst, font, shown, 0, 24, fg, bg);
    int column = (int)strlen(shown);
    char line[256];
    size_t length = 0;
    int key;
    do {
        key = read_key(keys);
        if (key < 0) break;
        if (key >= 0x20 && key <= 0x7a) {
            if (length < max && length < 255) {
                line[length++] = (char)key;
                ++column;
                cok_text_glyph(dst, font, cok_text_index((char)key), 1, column, 24, 15, 0);
            }
        } else if (key == 8) {
            if (length > 0) {
                --length;
                cok_text_clear(dst, font, 1, column, 24, 0);
                --column;
            }
        } else if (key == 0) {
            /* The scan code also ends the line if it is 13 or 27. */
            key = read_key(keys);
            if (key < 0) break;
        }
    } while (key != 13 && key != 27);
    cok_text_clear(dst, font, 40, 0, 24, 0);
    if (length > 40) length = 40;
    for (size_t i = 0; i < length; ++i)
        out[i] = line[i] >= 'a' && line[i] <= 'z' ? (char)(line[i] - ('a' - 'A')) : line[i];
    out[length] = '\0';
    return key >= 0;
}

unsigned cok_tp_val(const char *text, long *value)
{
    size_t i = 0, length = strlen(text);
    while (i < length && text[i] == ' ') ++i;
    bool negative = false;
    unsigned long n = 0;
    *value = 0;
    if (i < length && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    if (i == length) return (unsigned)(i + 1);
    if (text[i] == '$') {
        if (++i == length) return (unsigned)(i + 1);
        for (; i < length; ++i) {
            char c = text[i];
            unsigned digit;
            if (c >= '0' && c <= '9')
                digit = (unsigned)(c - '0');
            else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
                digit = (unsigned)((c | 0x20) - 'a' + 10);
            else
                return (unsigned)(i + 1);
            if (n > 0x0fffffffu) return (unsigned)(i + 1);
            n = n << 4 | digit;
        }
    } else {
        for (; i < length; ++i) {
            char c = text[i];
            if (c < '0' || c > '9') return (unsigned)(i + 1);
            if (n > 0x0fffffffu) return (unsigned)(i + 1);
            n = n * 10 + (unsigned)(c - '0');
            if (n > 0x7fffffffu) return (unsigned)(i + 1);
        }
    }
    n &= 0xffffffffu;
    long result = n > 0x7fffffffu ? (long)n - 0x100000000L : (long)n;
    *value = negative ? -result : result;
    return 0;
}

bool cok_text_number(cok_picture *dst, const cok_font *font, const char *prompt, uint8_t fg,
                     uint8_t bg, const cok_keyboard *keys, uint16_t *value)
{
    for (;;) {
        char line[41];
        bool more = cok_text_input(dst, font, prompt, fg, bg, 6, keys, line);
        long number;
        if (cok_tp_val(line, &number) == 0 && number >= 0 && number <= 0x10000) {
            *value = (uint16_t)number;
            return more;
        }
        if (!more) {
            *value = 0;
            return false;
        }
    }
}
