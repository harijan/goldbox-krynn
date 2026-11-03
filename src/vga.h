#ifndef COK_VGA_H
#define COK_VGA_H

#include "hlib.h"
#include "picture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The Dark Queen of Krynn's pictures and palette, in VGA's mode 13h: 320x200
 * in 256 colours, a byte a pixel, each colour 6-bit red, green and blue in
 * the DAC. Its pictures are records of its HLIB libraries (hlib.h), a byte a
 * pixel, stored plane by plane as VGA's unchained planes hold them (pixel x
 * in plane x % 4); its palettes records of 8-bit RGB the game writes to the
 * DAC shifted right by 2. The screen is a VGA cok_picture (picture.h). */

enum {
    COK_VGA_WIDTH = 320,
    COK_VGA_HEIGHT = 200,
    COK_VGA_COLOURS = 256,
    /* A picture record's header: u16 height, i16 x, i16 y, u8 width / 4,
     * u8 format. */
    COK_VGA_HEADER = 8,
    /* The formats (the header's last byte). */
    COK_VGA_RAW = 0x10,         /* each plane's rows in turn */
    COK_VGA_MASKED = 0x11,      /* as RAW twice: a mask (0xff clear), then the pixels */
    COK_VGA_PACKED = 0x12,      /* PackBits of the rows, each its four plane-rows */
    COK_VGA_CLEAR = 0x15,       /* as RAW, 0xff transparent */
    COK_VGA_RUNS = 0x17,        /* the planes' rows as runs, transparent ones too */
    COK_VGA_PALETTE = 0x18,     /* a palette, not a picture */
    COK_VGA_COMPOSED = 0x19,    /* drawn from the same library's records */
};

/* A colour cycle's slot (dqk DS:d445, 8 bytes each): when it steps next,
 * in the timer's ticks (72.8 Hz, DS:cda6), its flags (bit 0: down), its
 * rate in ticks, its first colour and its count. */
enum { COK_VGA_CYCLES = 12 };
typedef struct {
    bool used;
    uint32_t time;
    uint8_t flags, rate, first, count;
} cok_vga_cycle;

/* The display's colours: the DAC, 6 bits each, and each as the screen shows
 * it, 8 bits (rgb: what a screenshot holds): the live palette (dqk
 * DS:cdc5), which the cycles rotate. pending: the colours set (DS:d145,
 * never rotated), which a flush copies to the live ones where they
 * changed (dirty, DS:d0c5); and the cycles running. cok_vga_set and
 * cok_vga_load_palette set both at once and leave the cycles. */
typedef struct {
    uint8_t dac[COK_VGA_COLOURS][3];
    uint8_t rgb[COK_VGA_COLOURS][3];
    uint8_t pending[COK_VGA_COLOURS][3];
    bool dirty[COK_VGA_COLOURS];
    cok_vga_cycle cycles[COK_VGA_CYCLES];
} cok_vga_palette;

/* A decoded picture: width x height pixels, row by row, and where each is
 * transparent (mask non-zero), or no mask where none is; x and y are its
 * header's. Initialize to {0}; free before reuse. */
typedef struct {
    uint16_t width, height;
    int16_t x, y;
    uint8_t format;
    uint8_t *pixels;
    uint8_t *mask;
    /* Format 0x11's: mask holds bytes the screen is ANDed with before the
     * pixels are XORed on (dqk 10d7:02ba), not where it is transparent. */
    bool and_xor;
} cok_vga_image;

/* Where drawing reaches: rows top to bottom and columns left to right,
 * the ends excluded (dqk DS:cdae, cdb3, cdb9, cdb5: 251e:06cb sets it,
 * 075c resets it to the screen). */
typedef struct {
    int top, left, bottom, right;
} cok_vga_clip;

extern const cok_vga_clip cok_vga_screen;

typedef enum {
    COK_VGA_OK,
    COK_VGA_INVALID, /* not a picture record, or one shorter than it says */
    COK_VGA_MEMORY,
} cok_vga_status;

/* Whether record is a picture (a format above) or a palette. */
bool cok_vga_is_picture(const uint8_t *record, size_t size);
bool cok_vga_is_palette(const uint8_t *record, size_t size);

/* Decode a picture record. library: the library holding it, which format
 * 0x19 draws from (NULL fails it): its entries drawn as cok_vga_draw_record
 * draws them, the record placed at the picture's corner, transparent where
 * none draws. */
cok_vga_status cok_vga_decode(const uint8_t *record, size_t size, const hlib_library *library,
                              cok_vga_image *image);
void cok_vga_free(cok_vga_image *image);
/* Flip a decoded picture left to right, its pixels and mask (dqk 2796:000c, which
 * 204c:030f calls for a facing above 3; the header is kept). Only the formats
 * (format & 0xf) of 0x10, 0x11 and 0x15 are flipped: any other is refused, false
 * (2796:000c's "Invalid type (%x) to Mirror", DS:9db6). */
bool cok_vga_mirror(cok_vga_image *image);

/* A palette record's colours into the DAC (and rgb), as dqk 2796:0536 and
 * 261b:00b1 load it: u16 type (bit 0: first and count hold, else all 256;
 * 2: colour cycles follow the colours; 4: EGA nibbles), u16 first, u16
 * count, u8 the cycles, u8 0x18, then count 8-bit RGB triples, each >> 2
 * into the DAC. The cycles are not run. Returns false if the record
 * is short or past entry 255. */
bool cok_vga_load_palette(cok_vga_palette *palette, const uint8_t *record, size_t size);
/* dqk 261b:00b1 and the flush 261b:0455 after it: count colours from
 * first, 8-bit RGB triples from rgb (NULL: black), >> 2 into the pending
 * palette, and with them cycles (count of 4 bytes each: flags, rate,
 * first, count). It changes things when a colour differs from the
 * pending one, when the cycles running over the range are not as many as
 * given, or when one of them differs from the given one in order in all
 * four bytes (the original's test: one equal byte is "the same"); then
 * every cycle over the range stops, its colours flushed unrotated, and
 * the given ones start, each in the next free slot (none left: false, as
 * the original's "Out of color ranges!"), to step first at now +
 * random(8) + rate - 3 (random: the game's, 2102:0234). The changed
 * colours are flushed to the live palette at once (the original sends
 * them at its next page flip, 19a4:06d3, before any wait). Unchanged, the
 * cycles run on. */
bool cok_vga_set_colours(cok_vga_palette *palette, unsigned first, unsigned count,
                         const uint8_t *rgb, const uint8_t *cycles, unsigned cycle_count,
                         uint32_t now, const cok_random *random);
/* A palette record (as cok_vga_load_palette reads it, with its cycles
 * after the colours where its type's bit 1 is set: u8 at 6 their count,
 * 4 bytes each) set by cok_vga_set_colours, its colours and its cycles'
 * moved shift entries on (dqk 2796:0536). False if it is not one or is
 * short. */
bool cok_vga_apply_palette(cok_vga_palette *palette, const uint8_t *record, size_t size,
                           unsigned shift, uint32_t now, const cok_random *random);
/* dqk 261b:05be at the timer's tick now: each cycle whose time has come
 * rotates its live colours one step (up: each takes the one before, the
 * first the last; down the other way) for each rate passed, catching up.
 * Returns whether a colour changed. */
bool cok_vga_cycle_colours(cok_vga_palette *palette, uint32_t now);
/* When the next cycle steps (the earliest time of those running): false
 * if none runs. */
bool cok_vga_cycle_next(const cok_vga_palette *palette, uint32_t *time);
/* Every palette record of a library, in order (as its pictures load). */
void cok_vga_library_palette(cok_vga_palette *palette, const hlib_library *library);
/* The colour shown for a DAC value of 6 bits. */
uint8_t cok_vga_shown(uint8_t dac);
/* Set entry i's DAC colour and what it shows. */
void cok_vga_set(cok_vga_palette *palette, size_t i, uint8_t r, uint8_t g, uint8_t b);

/* Draw image onto screen (a VGA picture, frame 0) with its top left at x,
 * y in pixels, clipped to clip (NULL: the screen); transparent pixels keep
 * the screen's. remap, if not NULL, maps each pixel's colour first. */
void cok_vga_draw(cok_picture *screen, const cok_vga_image *image, int x, int y,
                  const cok_vga_clip *clip, const uint8_t *remap);
/* Draw record id of library as dqk 269b:0a9c does, at row, col less the
 * record's hotspot (its header's i16s at 2 and 4: the row's, then the
 * column's), the column taken down to a multiple of 4 (unchained VGA's
 * byte of four planes); a composed record (0x19) draws its entries, each
 * a record of the library at its place from there. False if a record is
 * missing or not a picture. */
bool cok_vga_draw_record(cok_picture *screen, const hlib_library *library, unsigned id,
                         int row, int col, const cok_vga_clip *clip, const uint8_t *remap);
/* Draw character ch of a GAME.FON font (96 glyphs of 12 rows, 0x20-0x7f,
 * the high bit dropped, below 0x20 a space) at row, col (the column down
 * to a multiple of 4), opaque: rows 0, 1, 3, 4, 6, 8, 9 and 10 of its 12,
 * set bits fg, the others bg (dqk 22b7:0c96, 10d7:2171). Nothing where
 * the cell would pass the screen's edge. */
void cok_vga_glyph(cok_picture *screen, const uint8_t *font, int row, int col, char ch,
                   uint8_t fg, uint8_t bg);
/* Fill a rectangle of screen, clipped, with colour. */
void cok_vga_fill(cok_picture *screen, int x, int y, int width, int height, uint8_t colour);
/* Unpack PackBits (dqk 10d7:0a1c, as pictures of format 0x12 are and the
 * monsters' records, 604b:257c) from src's size bytes into out, at most
 * need bytes; returns the bytes written. */
size_t cok_vga_unpack(const uint8_t *src, size_t size, uint8_t *out, size_t need);

#endif
