#include "image.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const uint8_t ega[16][3] = {
    {0,0,0}, {0,0,170}, {0,170,0}, {0,170,170},
    {170,0,0}, {170,0,170}, {170,85,0}, {170,170,170},
    {85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
    {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
};

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void cok_images_free(cok_images *images)
{
    free(images->images);
    *images = (cok_images){0};
}

cok_image_status cok_images_parse(const uint8_t *data, size_t size,
                                  int collection, cok_images *result)
{
    *result = (cok_images){0};
    if (data == NULL || size == 0) return COK_IMAGE_INVALID;
    size_t groups = collection ? data[0] : 1;
    size_t pos = collection ? 1 : 0;
    cok_image_status status = COK_IMAGE_INVALID;
    if (groups == 0) return status;
    for (size_t group = 0; group < groups; ++group) {
        if (collection) {
            if (size - pos < 4) goto fail;
            pos += 4;
        }
        if (size - pos < 17) goto fail;
        const uint8_t *header = data + pos;
        size_t height = u16(header);
        size_t units = u16(header + 2);
        size_t frames = header[8];
        if (height == 0 || units == 0 || frames == 0 || units > SIZE_MAX / 8)
            goto fail;
        size_t row = units * 4;
        pos += 17;
        /* Bound the multiplication against the available input first. */
        if (height > (size - pos) / row / frames) goto fail;
        size_t frame_size = row * height;
        if (result->count > SIZE_MAX / sizeof(cok_image) - frames) goto fail;
        size_t count = result->count + frames;
        cok_image *images = realloc(result->images, count * sizeof *images);
        if (images == NULL) {
            status = COK_IMAGE_MEMORY;
            goto fail;
        }
        result->images = images;
        for (size_t frame = 0; frame < frames; ++frame) {
            images[result->count++] = (cok_image){
                units * 8, height, u16(header + 4), u16(header + 6),
                group, frame, data + pos, header + 9
            };
            pos += frame_size;
        }
    }
    if (pos != size) goto fail;
    return COK_IMAGE_OK;
fail:
    cok_images_free(result);
    return status;
}

cok_image_status cok_images_undelta(uint8_t *data, size_t size)
{
    cok_images list = {0};
    cok_image_status status = cok_images_parse(data, size, 1, &list);
    if (status != COK_IMAGE_OK) return status;
    const cok_image *base = &list.images[0];
    size_t bytes = base->width / 2 * base->height;
    for (size_t i = 1; i < list.count; ++i) {
        const cok_image *image = &list.images[i];
        if (image->frame != 0 || image->width != base->width || image->height != base->height)
            status = COK_IMAGE_INVALID;
    }
    for (size_t i = 1; i < list.count && status == COK_IMAGE_OK; ++i) {
        uint8_t *pixels = data + (list.images[i].pixels - data);
        for (size_t j = 0; j < bytes; ++j) pixels[j] ^= base->pixels[j];
    }
    cok_images_free(&list);
    return status;
}

/* Restrict format selection to known graphics archives rather than guessing
 * from arbitrary game data. */
cok_layout cok_archive_layout(const char *path)
{
    const char *name = strrchr(path, '/');
    name = name == NULL ? path : name + 1;
    const char *deltas[] = {"PIC1.DAX", "PIC2.DAX", "PIC3.DAX"};
    for (size_t i = 0; i < sizeof deltas / sizeof *deltas; ++i)
        if (strcasecmp(name, deltas[i]) == 0) return COK_LAYOUT_DELTA;
    const char *collections[] = {"SPRIT1.DAX", "SPRIT2.DAX", "SPRIT3.DAX"};
    for (size_t i = 0; i < sizeof collections / sizeof *collections; ++i)
        if (strcasecmp(name, collections[i]) == 0) return COK_LAYOUT_COLLECTION;
    const char *singles[] = {"TITLE.DAX", "BIGPIC1.DAX", "BIGPIC2.DAX", "BIGPIC3.DAX",
        "CPIC1.DAX", "CPIC2.DAX", "CPIC3.DAX", "CBODY.DAX", "CHEAD.DAX",
        "BODY2.DAX", "BODY3.DAX", "HEAD2.DAX", "HEAD3.DAX", "COMSPR.DAX",
        "CURSOR.DAX", "SKY.DAX", "TILES.DAX", "8X8D1.DAX", "8X8D2.DAX", "8X8D3.DAX"};
    for (size_t i = 0; i < sizeof singles / sizeof *singles; ++i)
        if (strcasecmp(name, singles[i]) == 0) return COK_LAYOUT_SINGLE;
    return COK_LAYOUT_UNSUPPORTED;
}

static const uint8_t *pixel_color(const cok_image *image, size_t pixel)
{
    uint8_t byte = image->pixels[pixel / 2];
    unsigned index = pixel % 2 == 0 ? byte >> 4 : byte & 15u;
    return ega[index];
}

void cok_image_rgb(const cok_image *image, uint8_t *rgb)
{
    for (size_t i = 0; i < image->width * image->height; ++i)
        memcpy(rgb + i * 3, pixel_color(image, i), 3);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

cok_image_status cok_image_bmp(const cok_image *image, const char *path)
{
    if (image->width == 0 || image->height == 0 || image->width > INT32_MAX ||
        image->height > INT32_MAX || image->width > (SIZE_MAX - 3) / 3)
        return COK_IMAGE_INVALID;
    size_t stride = (image->width * 3 + 3) & ~(size_t)3;
    if (image->height > (UINT32_MAX - 54u) / stride) return COK_IMAGE_INVALID;
    size_t payload = stride * image->height;
    uint8_t header[54] = {'B', 'M'};
    put32(header + 2, (uint32_t)(54 + payload));
    put32(header + 10, 54);
    put32(header + 14, 40);
    put32(header + 18, (uint32_t)image->width);
    put32(header + 22, (uint32_t)image->height);
    header[26] = 1;
    header[28] = 24;
    put32(header + 34, (uint32_t)payload);
    uint8_t *row = calloc(stride, 1);
    if (row == NULL) return COK_IMAGE_MEMORY;
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        free(row);
        return COK_IMAGE_IO;
    }
    int failed = fwrite(header, 1, sizeof header, file) != sizeof header;
    for (size_t y = image->height; y > 0 && !failed; --y) {
        for (size_t x = 0; x < image->width; ++x) {
            const uint8_t *color = pixel_color(image, (y - 1) * image->width + x);
            row[x*3] = color[2]; row[x*3+1] = color[1]; row[x*3+2] = color[0];
        }
        failed = fwrite(row, 1, stride, file) != stride;
    }
    free(row);
    if (fclose(file) != 0) failed = 1;
    return failed ? COK_IMAGE_IO : COK_IMAGE_OK;
}

const char *cok_image_status_string(cok_image_status status)
{
    switch (status) {
    case COK_IMAGE_OK: return "success";
    case COK_IMAGE_INVALID: return "unsupported or malformed image layout";
    case COK_IMAGE_MEMORY: return "out of memory";
    case COK_IMAGE_IO: return "image write failed";
    }
    return "unknown error";
}
