#ifndef COK_IMAGE_H
#define COK_IMAGE_H

#include "rules.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t width, height;
    uint16_t x, y;
    size_t group, frame;
    const uint8_t *pixels;
    const uint8_t *colors; /* Header bytes 9-16 (CGA colour map). */
    /* The colours pixel values show in: NULL for the 16 of the Tandy and
     * EGA modes; cok_cga_palette for a CGA screen's values 0-3. */
    const uint8_t (*palette)[3];
} cok_image;

/* CGA mode 4's colours as the game leaves them, the BIOS's palette 1 with
 * intensity (no code sets another: no INT 10h AH=0Bh, no port 0x3D9):
 * black, light cyan, light magenta and white, for the values 0-3 (and
 * value & 3 for the rest). */
extern const uint8_t cok_cga_palette[16][3];

/* Owns the image list, but borrows pixel bytes from the decoded DAX record. */
typedef struct {
    cok_image *images;
    size_t count;
} cok_images;

typedef enum { COK_IMAGE_OK, COK_IMAGE_INVALID, COK_IMAGE_MEMORY, COK_IMAGE_IO } cok_image_status;

typedef enum {
    COK_LAYOUT_UNSUPPORTED,
    COK_LAYOUT_SINGLE,
    COK_LAYOUT_COLLECTION,
    COK_LAYOUT_DELTA /* A collection whose later groups are XOR deltas. */
} cok_layout;

/* Select a layout from a known graphics archive name, ignoring directories:
 * the archives rules lists (images), with .DAX, in any case. */
cok_layout cok_archive_layout(const cok_rules *rules, const char *path);

/* Initialize result to {0}; free before reuse. collection=1 selects the
 * PIC/SPRIT layout (count byte, then four metadata bytes per image group).
 * collection=0 selects a single header with one or more pixel frames.
 * All bytes must match the selected layout; partial results are discarded. */
cok_image_status cok_images_parse(const uint8_t *data, size_t size,
                                  int collection, cok_images *result);
void cok_images_free(cok_images *images);
/* Decode a COK_LAYOUT_DELTA record in place, before parsing: as GAME.OVR does
 * for PIC archives, XOR each group after the first with the first group's
 * pixels. Every group must have one frame of the same size. */
cok_image_status cok_images_undelta(uint8_t *data, size_t size);
/* Render opaque packed 4-bit pixels using the standard 16-color EGA palette,
 * or image->palette. rgb must have at least width*height*3 bytes. */
void cok_image_rgb(const cok_image *image, uint8_t *rgb);
cok_image_status cok_image_bmp(const cok_image *image, const char *path);
const char *cok_image_status_string(cok_image_status status);

#endif
