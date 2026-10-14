#ifndef COK_PICTURE_H
#define COK_PICTURE_H

#include "image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Runtime picture from the graphics unit in START.EXE (code segment 0x27f).
 * Pixels use the linear Tandy layout: packed 4-bit, high nibble first, rows
 * of units*4 bytes, frames stored back to back. The EGA's planes hold the
 * same pixels (every routine of the unit gives the Tandy's 16-colour result
 * for what the game passes), so EGA pictures are Tandy ones. A CGA picture
 * (cga set) holds the CGA colours 0-3, each pixel in a nibble: the unit's
 * CGA routines are the Tandy ones on 2-bit pixels, but for loading, fills,
 * text and recolouring, which map colours (cok_picture_load_cga,
 * cok_picture_fill, cok_text_glyph, cok_picture_recolor). Treat fields as
 * read-only except pixel and mask bytes, and cga, which a picture created
 * for CGA mode sets. Only fills, text, recolouring and cok_picture_frame
 * read cga: draws, mirrors and saves copy whatever the pixels hold. */
typedef struct {
    uint16_t height;    /* Rows. */
    uint16_t units;     /* Width in 8-pixel units. */
    uint16_t x, y;      /* Header placement fields; drawing does not use them. */
    uint8_t colors[8];  /* Header CGA colour map: colour n's CGA colour in nibble n,
                           the high nibble first (127f:007b). */
    size_t frames;
    size_t frame_size;  /* height * units * 4 bytes. */
    uint8_t *pixels;    /* frames * frame_size bytes. */
    uint8_t *mask;      /* NULL, or the same size: set bits keep the destination. */
    bool cga;           /* Pixels are CGA colours (DS:4b76 0). */
} cok_picture;

typedef enum { COK_PICTURE_OK, COK_PICTURE_INVALID, COK_PICTURE_MEMORY } cok_picture_status;

/* Draw flags, numbered as in the original. SAVE copies the destination bytes
 * under a masked draw into frame 0 of a picture the same size as src. */
enum { COK_DRAW_MASKED = 1, COK_DRAW_SAVE = 4 };

/* Returns a value below range. */
typedef struct {
    unsigned (*next)(void *context, unsigned range);
    void *context;
} cok_random;

/* Initialize to {0}; free before reuse. Pixels and mask start zeroed. */
cok_picture_status cok_picture_create(cok_picture *picture, size_t units, size_t height,
                                      size_t frames, int masked);
void cok_picture_free(cok_picture *picture);

/* Copy count consecutive same-sized frames (27f:02a5). transparent 0-15
 * builds a mask and clears those pixels; -1 builds no mask. */
cok_picture_status cok_picture_load(cok_picture *picture, const cok_image *frames,
                                    size_t count, int transparent);
/* As cok_picture_load in CGA mode (127f:02a5 with DS:4b76 0): each pixel
 * becomes its colour's CGA colour in the record's map (colors), the mask
 * comes from the colour itself, and transparent pixels are 0. A map value
 * above 3 ORs bytes of the tables after DS:1e0e into the four pixels of
 * its byte, as the original does; no record in the game has one. Sets
 * cga. */
cok_picture_status cok_picture_load_cga(cok_picture *picture, const cok_image *frames,
                                        size_t count, int transparent);
/* Load as the GAME.OVR loader does for masked pictures outside CGA mode
 * (6961:00e4): colour 0 is transparent, then colour 13 is drawn as opaque
 * black (6961:0497). In CGA mode the loader takes cok_picture_load_cga
 * with transparent 0 alone. */
cok_picture_status cok_picture_load_sprite(cok_picture *picture, const cok_image *frames,
                                           size_t count);

/* Draw one src frame onto frame 0 of dst, clipped (27f:09c3). x is in 8-pixel
 * units and y in 8-row cells; both may be negative. MASKED is ignored when
 * src has no mask. An out-of-range frame draws nothing. */
void cok_picture_draw(cok_picture *dst, const cok_picture *src, size_t frame,
                      int x, int y, unsigned flags, cok_picture *save);

/* Fill a rectangle of frame 0, clipped (27f:1d71). x and units are in
 * 8-pixel units; y and rows are in pixel rows. In a CGA picture the
 * colour is its nibble in the map every caller passes, DS:1e1e (colours
 * 0-15 to 0 0 1 1 2 2 1 2 0 1 2 1 3 0 3 3). Colours use their low four
 * bits. */
void cok_picture_fill(cok_picture *dst, int x, int y, size_t units, size_t rows,
                      uint8_t color);

/* Replace colour from[i] with to[i] for every pair that differs (27f:15fd).
 * Matches test the original pixels, so swaps do not cascade. A negative frame
 * selects all frames. With random, each match changes only when next(4)
 * returns 0 (as the Tandy and CGA modes draw; the EGA's draws differ, but
 * no caller passes random). The mask is unchanged. Colour values use their
 * low four bits. In a CGA picture both colours go through its map
 * (colors): every pixel of from[i]'s CGA colour, whatever colour it was
 * loaded from, takes to[i]'s, transparent pixels too where that is 0. */
cok_picture_status cok_picture_recolor(cok_picture *picture, const uint8_t from[16],
                                       const uint8_t to[16], int frame,
                                       const cok_random *random);

/* Write frame 0 of src mirrored left-to-right into frame 0 of dst (27f:0b94).
 * dst must match src's size and have a mask if src has one. The map and
 * cga go with it. */
cok_picture_status cok_picture_mirror(cok_picture *dst, const cok_picture *src);

/* False where a CGA picture's frame 0 holds a value above 3, which only a
 * picture drawn on it without CGA's colours (a bug) leaves; *pixel then
 * says where. True for the others. */
bool cok_picture_cga_valid(const cok_picture *picture, size_t *pixel);

/* Borrowed view of one frame, for cok_image_rgb and cok_image_bmp: a CGA
 * picture's in cok_cga_palette. */
cok_image cok_picture_frame(const cok_picture *picture, size_t frame);
const char *cok_picture_status_string(cok_picture_status status);

#endif
