#include "picture.h"

#include <stdlib.h>
#include <string.h>

cok_picture_status cok_picture_create(cok_picture *picture, size_t units, size_t height,
                                      size_t frames, int masked)
{
    *picture = (cok_picture){0};
    if (units == 0 || height == 0 || frames == 0 || units > UINT16_MAX ||
        height > UINT16_MAX || units * 4 > SIZE_MAX / height)
        return COK_PICTURE_INVALID;
    size_t frame_size = units * 4 * height;
    if (frames > SIZE_MAX / frame_size) return COK_PICTURE_INVALID;
    uint8_t *pixels = calloc(frames, frame_size);
    uint8_t *mask = masked ? calloc(frames, frame_size) : NULL;
    if (pixels == NULL || (masked && mask == NULL)) {
        free(pixels);
        free(mask);
        return COK_PICTURE_MEMORY;
    }
    *picture = (cok_picture){(uint16_t)height, (uint16_t)units, 0, 0, {0},
                             frames, frame_size, pixels, mask};
    return COK_PICTURE_OK;
}

void cok_picture_free(cok_picture *picture)
{
    free(picture->pixels);
    free(picture->mask);
    *picture = (cok_picture){0};
}

cok_picture_status cok_picture_load(cok_picture *picture, const cok_image *frames,
                                    size_t count, int transparent)
{
    *picture = (cok_picture){0};
    if (frames == NULL || count == 0 || transparent < -1 || transparent > 15)
        return COK_PICTURE_INVALID;
    for (size_t i = 0; i < count; ++i)
        if (frames[i].width != frames[0].width || frames[i].height != frames[0].height ||
            frames[i].width % 8 != 0)
            return COK_PICTURE_INVALID;
    cok_picture_status status = cok_picture_create(picture, frames[0].width / 8,
                                                   frames[0].height, count, transparent >= 0);
    if (status != COK_PICTURE_OK) return status;
    picture->x = frames[0].x;
    picture->y = frames[0].y;
    if (frames[0].colors != NULL) memcpy(picture->colors, frames[0].colors, 8);
    for (size_t i = 0; i < count; ++i)
        memcpy(picture->pixels + i * picture->frame_size, frames[i].pixels, picture->frame_size);
    if (picture->mask != NULL) {
        unsigned t = (unsigned)transparent;
        for (size_t i = 0; i < count * picture->frame_size; ++i) {
            uint8_t byte = picture->pixels[i];
            uint8_t mask = (uint8_t)(((byte >> 4) == t ? 0xf0u : 0u) | ((byte & 15u) == t ? 0x0fu : 0u));
            picture->mask[i] = mask;
            picture->pixels[i] = (uint8_t)(byte & ~mask);
        }
    }
    return COK_PICTURE_OK;
}

cok_picture_status cok_picture_load_sprite(cok_picture *picture, const cok_image *frames,
                                           size_t count)
{
    cok_picture_status status = cok_picture_load(picture, frames, count, 0);
    if (status != COK_PICTURE_OK) return status;
    uint8_t from[16], to[16];
    for (uint8_t i = 0; i < 16; ++i) from[i] = to[i] = i;
    to[13] = 0;
    status = cok_picture_recolor(picture, from, to, -1, NULL);
    if (status != COK_PICTURE_OK) cok_picture_free(picture);
    return status;
}

/* Clip a span of length size placed at position within [0, limit). */
static int clip(long long position, long long size, long long limit,
                size_t *skip, size_t *length)
{
    long long begin = position < 0 ? -position : 0;
    long long end = position + size > limit ? limit - position : size;
    if (begin >= end) return 0;
    *skip = (size_t)begin;
    *length = (size_t)(end - begin);
    return 1;
}

void cok_picture_draw(cok_picture *dst, const cok_picture *src, size_t frame,
                      int x, int y, unsigned flags, cok_picture *save)
{
    size_t left, units, top, rows;
    if (frame >= src->frames ||
        !clip(x, src->units, dst->units, &left, &units) ||
        !clip((long long)y * 8, src->height, dst->height, &top, &rows))
        return;
    int masked = (flags & COK_DRAW_MASKED) && src->mask != NULL;
    if (!masked || !(flags & COK_DRAW_SAVE) || save == NULL || save->units != src->units ||
        save->height != src->height)
        save = NULL;
    size_t src_row = (size_t)src->units * 4, dst_row = (size_t)dst->units * 4;
    size_t bytes = units * 4;
    size_t dst_x = (size_t)((long long)x + (long long)left) * 4;
    size_t dst_y = (size_t)((long long)y * 8 + (long long)top);
    for (size_t row = 0; row < rows; ++row) {
        size_t s = (top + row) * src_row + left * 4;
        uint8_t *d = dst->pixels + (dst_y + row) * dst_row + dst_x;
        const uint8_t *pixels = src->pixels + frame * src->frame_size + s;
        if (!masked) {
            memmove(d, pixels, bytes);
            continue;
        }
        const uint8_t *mask = src->mask + frame * src->frame_size + s;
        for (size_t i = 0; i < bytes; ++i) {
            if (save != NULL) save->pixels[s + i] = d[i];
            d[i] = (uint8_t)((d[i] & mask[i]) | pixels[i]);
        }
    }
}

void cok_picture_fill(cok_picture *dst, int x, int y, size_t units, size_t rows,
                      uint8_t color)
{
    size_t left, width, top, height;
    if (units > INT32_MAX || rows > INT32_MAX ||
        !clip(x, (long long)units, dst->units, &left, &width) ||
        !clip(y, (long long)rows, dst->height, &top, &height))
        return;
    size_t row_bytes = (size_t)dst->units * 4;
    size_t dst_x = (size_t)((long long)x + (long long)left) * 4;
    size_t dst_y = (size_t)((long long)y + (long long)top);
    for (size_t row = 0; row < height; ++row)
        memset(dst->pixels + (dst_y + row) * row_bytes + dst_x, (color & 15) * 0x11, width * 4);
}

static int chance(const cok_random *random)
{
    return random == NULL || random->next(random->context, 4) == 0;
}

cok_picture_status cok_picture_recolor(cok_picture *picture, const uint8_t from[16],
                                       const uint8_t to[16], int frame,
                                       const cok_random *random)
{
    if (frame >= 0 && (size_t)frame >= picture->frames) return COK_PICTURE_INVALID;
    size_t first = frame < 0 ? 0 : (size_t)frame;
    size_t size = (frame < 0 ? picture->frames : 1) * picture->frame_size;
    uint8_t *original = picture->pixels + first * picture->frame_size;
    uint8_t *copy = malloc(size);
    if (copy == NULL) return COK_PICTURE_MEMORY;
    memcpy(copy, original, size);
    for (size_t i = 0; i < 16; ++i) {
        unsigned old = from[i] & 15u, new = to[i] & 15u;
        if (old == new) continue;
        for (size_t j = 0; j < size; ++j) {
            if ((original[j] >> 4) == old && chance(random))
                copy[j] = (uint8_t)((copy[j] & 0x0f) | (new << 4));
            if ((original[j] & 15u) == old && chance(random))
                copy[j] = (uint8_t)((copy[j] & 0xf0) | new);
        }
    }
    memcpy(original, copy, size);
    free(copy);
    return COK_PICTURE_OK;
}

static uint8_t swap_nibbles(uint8_t byte)
{
    return (uint8_t)((byte << 4) | (byte >> 4));
}

cok_picture_status cok_picture_mirror(cok_picture *dst, const cok_picture *src)
{
    if (dst == src || dst->units != src->units || dst->height != src->height ||
        (src->mask != NULL && dst->mask == NULL))
        return COK_PICTURE_INVALID;
    memcpy(dst->colors, src->colors, sizeof dst->colors);
    size_t row_bytes = (size_t)src->units * 4;
    for (size_t row = 0; row < src->height; ++row) {
        size_t start = row * row_bytes;
        for (size_t i = 0; i < row_bytes; ++i) {
            size_t from = start + row_bytes - 1 - i;
            dst->pixels[start + i] = swap_nibbles(src->pixels[from]);
            if (src->mask != NULL) dst->mask[start + i] = swap_nibbles(src->mask[from]);
        }
    }
    return COK_PICTURE_OK;
}

cok_image cok_picture_frame(const cok_picture *picture, size_t frame)
{
    return (cok_image){
        (size_t)picture->units * 8, picture->height, picture->x, picture->y, 0, frame,
        picture->pixels + frame * picture->frame_size, picture->colors
    };
}

const char *cok_picture_status_string(cok_picture_status status)
{
    switch (status) {
    case COK_PICTURE_OK: return "success";
    case COK_PICTURE_INVALID: return "invalid picture operation";
    case COK_PICTURE_MEMORY: return "out of memory";
    }
    return "unknown error";
}
