#ifndef COK_PICTURE_H
#define COK_PICTURE_H

#include "image.h"

#include <stddef.h>
#include <stdint.h>

/* Runtime picture from the graphics unit in START.EXE (code segment 0x27f).
 * Pixels use the linear Tandy layout: packed 4-bit, high nibble first, rows
 * of units*4 bytes, frames stored back to back. The CGA and EGA layouts are
 * not ported. Treat fields as read-only except pixel and mask bytes. */
typedef struct {
    uint16_t height;    /* Rows. */
    uint16_t units;     /* Width in 8-pixel units. */
    uint16_t x, y;      /* Header placement fields; drawing does not use them. */
    uint8_t colors[8];  /* Header CGA colour map; unused by this renderer. */
    size_t frames;
    size_t frame_size;  /* height * units * 4 bytes. */
    uint8_t *pixels;    /* frames * frame_size bytes. */
    uint8_t *mask;      /* NULL, or the same size: set bits keep the destination. */
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
/* Load as the GAME.OVR loader does for masked pictures: colour 0 is
 * transparent, then colour 13 is drawn as opaque black. */
cok_picture_status cok_picture_load_sprite(cok_picture *picture, const cok_image *frames,
                                           size_t count);

/* Draw one src frame onto frame 0 of dst, clipped (27f:09c3). x is in 8-pixel
 * units and y in 8-row cells; both may be negative. MASKED is ignored when
 * src has no mask. An out-of-range frame draws nothing. */
void cok_picture_draw(cok_picture *dst, const cok_picture *src, size_t frame,
                      int x, int y, unsigned flags, cok_picture *save);

/* Fill a rectangle of frame 0, clipped (27f:1d71). x and units are in
 * 8-pixel units; y and rows are in pixel rows. */
void cok_picture_fill(cok_picture *dst, int x, int y, size_t units, size_t rows,
                      uint8_t color);

/* Replace colour from[i] with to[i] for every pair that differs (27f:15fd).
 * Matches test the original pixels, so swaps do not cascade. A negative frame
 * selects all frames. With random, each match changes only when next(4)
 * returns 0. The mask is unchanged. Colour values use their low four bits. */
cok_picture_status cok_picture_recolor(cok_picture *picture, const uint8_t from[16],
                                       const uint8_t to[16], int frame,
                                       const cok_random *random);

/* Write frame 0 of src mirrored left-to-right into frame 0 of dst (27f:0b94).
 * dst must match src's size and have a mask if src has one. */
cok_picture_status cok_picture_mirror(cok_picture *dst, const cok_picture *src);

/* Borrowed view of one frame, for cok_image_rgb and cok_image_bmp. */
cok_image cok_picture_frame(const cok_picture *picture, size_t frame);
const char *cok_picture_status_string(cok_picture_status status);

#endif
