#include "vga.h"

#include <stdlib.h>
#include <string.h>

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

bool cok_vga_is_picture(const uint8_t *record, size_t size)
{
    if (size < COK_VGA_HEADER) return false;
    uint8_t f = record[7];
    return f == COK_VGA_RAW || f == COK_VGA_MASKED || f == COK_VGA_PACKED ||
           f == COK_VGA_CLEAR || f == COK_VGA_RUNS || f == COK_VGA_COMPOSED;
}

bool cok_vga_is_palette(const uint8_t *record, size_t size)
{
    return size >= COK_VGA_HEADER && record[7] == COK_VGA_PALETTE;
}

void cok_vga_free(cok_vga_image *image)
{
    free(image->pixels);
    free(image->mask);
    *image = (cok_vga_image){0};
}

/* Pixel (c, j) of a picture stored plane by plane: plane c % 4's rows in
 * turn, a quarter row each. */
static size_t planar(size_t c, size_t j, size_t width, size_t height)
{
    size_t w4 = width / 4;
    return (c % 4) * w4 * height + j * w4 + c / 4;
}

/* PackBits (dqk 10d7:0a1c): a byte n below 0x80 copies n + 1 bytes, 0x80
 * does nothing, others repeat the next 257 - n times. Returns the bytes
 * written (need at most). */
static size_t unpack(const uint8_t *src, size_t size, uint8_t *out, size_t need)
{
    size_t i = 0, o = 0;
    while (o < need && i < size) {
        uint8_t n = src[i++];
        if (n == 0x80) continue; /* a no-op (dqk 10d7:0a1c) */
        if (n < 0x80) {
            for (size_t k = 0; k <= n && o < need && i < size; ++k) out[o++] = src[i++];
        } else {
            if (i >= size) break;
            uint8_t v = src[i++];
            for (size_t k = 0; k < 257u - n && o < need; ++k) out[o++] = v;
        }
    }
    return o;
}

static cok_vga_status decode(const uint8_t *record, size_t size, const hlib_library *library,
                             cok_vga_image *image, unsigned depth);

/* Where pictures are placed: the screen's pixels, or a picture's being
 * composed, with where it is still transparent (mask non-zero). */
typedef struct {
    uint8_t *pixels, *mask;
    int width, height;
} surface;

static bool draw_bytes(const surface *to, const hlib_library *library, const uint8_t *record,
                       size_t size, int row, int col, const cok_vga_clip *clip,
                       const uint8_t *remap, unsigned depth);

/* Format 0x19 as a picture of its own: drawn as on the screen (draw_bytes)
 * with the record at the picture's corner (its hotspot the place given);
 * what no entry draws stays transparent. */
static cok_vga_status composed(const uint8_t *record, size_t size, const hlib_library *library,
                               cok_vga_image *image, unsigned depth)
{
    if (library == NULL) return COK_VGA_INVALID;
    memset(image->mask, 1, (size_t)image->width * image->height);
    const surface canvas = {image->pixels, image->mask, image->width, image->height};
    return draw_bytes(&canvas, library, record, size, (int16_t)u16(record + 2),
                      (int16_t)u16(record + 4), NULL, NULL, depth)
               ? COK_VGA_OK
               : COK_VGA_INVALID;
}

static cok_vga_status decode(const uint8_t *record, size_t size, const hlib_library *library,
                             cok_vga_image *image, unsigned depth)
{
    *image = (cok_vga_image){0};
    if (!cok_vga_is_picture(record, size)) return COK_VGA_INVALID;
    size_t height = u16(record), width = (size_t)record[6] * 4;
    uint8_t format = record[7];
    const uint8_t *body = record + COK_VGA_HEADER;
    size_t body_size = size - COK_VGA_HEADER, n = width * height;
    image->width = (uint16_t)width;
    image->height = (uint16_t)height;
    image->x = (int16_t)u16(record + 2);
    image->y = (int16_t)u16(record + 4);
    image->format = format;
    image->pixels = malloc(n == 0 ? 1 : n);
    bool masked = format != COK_VGA_RAW && format != COK_VGA_PACKED;
    if (masked) image->mask = calloc(n == 0 ? 1 : n, 1);
    if (image->pixels == NULL || (masked && image->mask == NULL)) {
        cok_vga_free(image);
        return COK_VGA_MEMORY;
    }
    cok_vga_status status = COK_VGA_OK;
    switch (format) {
    case COK_VGA_RAW:
    case COK_VGA_CLEAR:
        if (body_size < n) {
            status = COK_VGA_INVALID;
            break;
        }
        for (size_t j = 0; j < height; ++j)
            for (size_t c = 0; c < width; ++c) {
                uint8_t v = body[planar(c, j, width, height)];
                image->pixels[j * width + c] = v;
                if (format == COK_VGA_CLEAR) image->mask[j * width + c] = v == 0xff;
            }
        break;
    case COK_VGA_MASKED:
        if (body_size / 2 < n) {
            status = COK_VGA_INVALID;
            break;
        }
        for (size_t j = 0; j < height; ++j)
            for (size_t c = 0; c < width; ++c) {
                size_t at = planar(c, j, width, height);
                image->pixels[j * width + c] = body[n + at];
                image->mask[j * width + c] = body[at];
            }
        image->and_xor = true;
        break;
    case COK_VGA_PACKED: {
        uint8_t *rows = malloc(n == 0 ? 1 : n);
        if (rows == NULL) {
            status = COK_VGA_MEMORY;
            break;
        }
        if (unpack(body, body_size, rows, n) < n) status = COK_VGA_INVALID;
        /* Row j holds its four plane-rows in turn. */
        size_t w4 = width / 4;
        for (size_t j = 0; j < height && status == COK_VGA_OK; ++j)
            for (size_t c = 0; c < width; ++c)
                image->pixels[j * width + c] = rows[j * width + (c % 4) * w4 + c / 4];
        free(rows);
        break;
    }
    case COK_VGA_RUNS: {
        size_t w4 = width / 4, i = 0;
        memset(image->mask, 1, n);
        memset(image->pixels, 0, n);
        for (size_t row = 0; row < 4 * height && status == COK_VGA_OK; ++row) {
            size_t plane = row / height, j = row % height, at = 0;
            for (;;) {
                if (i >= body_size) {
                    status = COK_VGA_INVALID;
                    break;
                }
                uint8_t c = body[i++];
                if (c == 0) break;
                if (c >= 0x80) {
                    at += 256u - c;
                    continue;
                }
                if (body_size - i < c) {
                    status = COK_VGA_INVALID;
                    break;
                }
                for (size_t k = 0; k < c; ++k, ++at) {
                    size_t x = at * 4 + plane;
                    if (at < w4) {
                        image->pixels[j * width + x] = body[i + k];
                        image->mask[j * width + x] = 0;
                    }
                }
                i += c;
            }
        }
        break;
    }
    case COK_VGA_COMPOSED:
        status = composed(record, size, library, image, depth);
        break;
    }
    if (status != COK_VGA_OK) cok_vga_free(image);
    return status;
}

cok_vga_status cok_vga_decode(const uint8_t *record, size_t size, const hlib_library *library,
                              cok_vga_image *image)
{
    return decode(record, size, library, image, 0);
}

uint8_t cok_vga_shown(uint8_t dac)
{
    dac &= 0x3f;
    return (uint8_t)(dac << 2 | dac >> 4);
}

void cok_vga_set(cok_vga_palette *palette, size_t i, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t v[3] = {r, g, b};
    for (size_t k = 0; k < 3; ++k) {
        palette->dac[i][k] = (uint8_t)(v[k] & 0x3f);
        palette->rgb[i][k] = cok_vga_shown(v[k]);
    }
}

bool cok_vga_load_palette(cok_vga_palette *palette, const uint8_t *record, size_t size)
{
    if (!cok_vga_is_palette(record, size)) return false;
    size_t first = u16(record + 2), count = u16(record + 4);
    if (!(u16(record) & 1)) first = 0, count = COK_VGA_COLOURS; /* 2796:0536 */
    if (first + count > COK_VGA_COLOURS || (size - COK_VGA_HEADER) / 3 < count) return false;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *p = record + COK_VGA_HEADER + 3 * i;
        cok_vga_set(palette, first + i, p[0] >> 2, p[1] >> 2, p[2] >> 2);
    }
    return true;
}

void cok_vga_library_palette(cok_vga_palette *palette, const hlib_library *library)
{
    for (size_t i = 0; i < library->count; ++i) {
        const uint8_t *record;
        size_t size;
        if (hlib_record_at(library, i, &record, &size) == HLIB_OK &&
            !(i == 0 && (library->flags & HLIB_ID_MAP)) && strcmp(library->type, "TILE") == 0)
            cok_vga_load_palette(palette, record, size);
    }
}

const cok_vga_clip cok_vga_screen = {0, 0, COK_VGA_HEIGHT, COK_VGA_WIDTH};

static surface screen_of(cok_picture *screen)
{
    return (surface){screen->pixels, NULL, screen->units * 8, screen->height};
}

static void place(const surface *to, const cok_vga_image *image, int x, int y,
                  const cok_vga_clip *clip, const uint8_t *remap)
{
    cok_vga_clip c0 = clip != NULL ? *clip : cok_vga_screen;
    if (c0.bottom > to->height) c0.bottom = to->height;
    if (c0.right > to->width) c0.right = to->width;
    if (c0.top < 0) c0.top = 0;
    if (c0.left < 0) c0.left = 0;
    for (int j = 0; j < image->height; ++j) {
        int dy = y + j;
        if (dy < c0.top || dy >= c0.bottom) continue;
        for (int c = 0; c < image->width; ++c) {
            int dx = x + c;
            size_t from = (size_t)j * image->width + (size_t)c;
            if (dx < c0.left || dx >= c0.right) continue;
            uint8_t v = image->pixels[from];
            if (remap != NULL) v = remap[v];
            size_t at = (size_t)dy * (size_t)to->width + (size_t)dx;
            if (image->and_xor) {
                to->pixels[at] = (uint8_t)((to->pixels[at] & image->mask[from]) ^ v);
                if (to->mask != NULL && image->mask[from] != 0xff) to->mask[at] = 0;
            } else if (image->mask == NULL || !image->mask[from]) {
                to->pixels[at] = v;
                if (to->mask != NULL) to->mask[at] = 0;
            }
        }
    }
}

void cok_vga_draw(cok_picture *screen, const cok_vga_image *image, int x, int y,
                  const cok_vga_clip *clip, const uint8_t *remap)
{
    if (!screen->vga) return;
    const surface to = screen_of(screen);
    place(&to, image, x, y, clip, remap);
}

/* A floor division by 4, times 4: VGA's unchained byte column. */
static int snap(int x)
{
    return x >= 0 ? x & ~3 : -((-x + 3) & ~3);
}

static bool draw_bytes(const surface *to, const hlib_library *library, const uint8_t *record,
                       size_t size, int row, int col, const cok_vga_clip *clip,
                       const uint8_t *remap, unsigned depth)
{
    if (depth > 4 || !cok_vga_is_picture(record, size)) return false;
    row -= (int16_t)u16(record + 2);
    col -= (int16_t)u16(record + 4);
    if (record[7] == COK_VGA_COMPOSED) {
        /* 269b:0aec: each entry a record of the library where it says from
         * this one's place, the count re-read from each. */
        size_t n = 1;
        for (size_t i = 0, k = COK_VGA_HEADER; i < n && k + 6 <= size; ++i, k += 6) {
            const uint8_t *part;
            size_t part_size;
            n = record[k + 1];
            if (hlib_record(library, record[k], &part, &part_size) != HLIB_OK ||
                !draw_bytes(to, library, part, part_size, row + (int16_t)u16(record + k + 2),
                            col + (int16_t)u16(record + k + 4), clip, remap, depth + 1))
                return false;
        }
        return true;
    }
    cok_vga_image image;
    if (decode(record, size, library, &image, depth + 1) != COK_VGA_OK) return false;
    place(to, &image, snap(col), row, clip, remap);
    cok_vga_free(&image);
    return true;
}

bool cok_vga_draw_record(cok_picture *screen, const hlib_library *library, unsigned id,
                         int row, int col, const cok_vga_clip *clip, const uint8_t *remap)
{
    const uint8_t *record;
    size_t size;
    if (!screen->vga || hlib_record(library, id, &record, &size) != HLIB_OK) return false;
    const surface to = screen_of(screen);
    return draw_bytes(&to, library, record, size, row, col, clip, remap, 0);
}

void cok_vga_glyph(cok_picture *screen, const uint8_t *font, int row, int col, char ch,
                   uint8_t fg, uint8_t bg)
{
    if (!screen->vga || row < 0 || col < 0 || row + 8 > COK_VGA_HEIGHT ||
        col + 8 > COK_VGA_WIDTH)
        return;
    static const uint8_t rows[8] = {0, 1, 3, 4, 6, 8, 9, 10};
    unsigned c = (unsigned char)ch & 0x7fu;
    const uint8_t *glyph = font + (c < 0x20 ? 0 : (c - 0x20) * 12u);
    int x = snap(col);
    int sw = screen->units * 8;
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i)
            screen->pixels[(size_t)(row + j) * (size_t)sw + (size_t)(x + i)] =
                glyph[rows[j]] & (0x80 >> i) ? fg : bg;
}

void cok_vga_fill(cok_picture *screen, int x, int y, int width, int height, uint8_t colour)
{
    if (!screen->vga) return;
    int sw = screen->units * 8, sh = screen->height;
    for (int dy = y < 0 ? 0 : y; dy < y + height && dy < sh; ++dy)
        for (int dx = x < 0 ? 0 : x; dx < x + width && dx < sw; ++dx)
            screen->pixels[(size_t)dy * (size_t)sw + (size_t)dx] = colour;
}
