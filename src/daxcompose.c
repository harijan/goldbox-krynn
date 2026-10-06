#include "dax.h"
#include "image.h"
#include "picture.h"
#include "text.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr,
        "Usage: %s OUTPUT.bmp COMMAND...\n"
        "Draws onto a black 320x200 screen, in order, and writes a BMP.\n"
        "  fill X Y UNITS ROWS COLOR       X and UNITS in 8-pixel units, Y and ROWS in rows\n"
        "  draw ARCHIVE ENTRY IMAGE X Y    opaque; X in 8-pixel units, Y in 8-row cells\n"
        "  sprite ARCHIVE ENTRY IMAGE X Y  colour 0 transparent, colour 13 drawn black\n"
        "  mirror ARCHIVE ENTRY IMAGE X Y  sprite, flipped left to right\n"
        "  text FONT ENTRY X Y FG BG STRING          X and Y in 8x8 cells\n"
        "  glyph FONT ENTRY GLYPH COUNT X Y FG BG    repeat a glyph along a row\n"
        "ENTRY and IMAGE are zero-based, as in daximages; X and Y may be negative.\n"
        "FONT is an archive whose entry holds 8x8 glyphs, such as 8X8D1.DAX entry 0.\n",
        program);
}

static int integer(const char *s, long min, long max, long *value)
{
    errno = 0;
    char *end;
    long n = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || errno || n < min || n > max) return 0;
    *value = n;
    return 1;
}

/* Decode one record; returns NULL after reporting an error. */
static uint8_t *read_record(const char *path, long entry, size_t *size)
{
    dax_archive archive = {0};
    dax_record record;
    dax_status status = dax_open(path, &archive);
    if (status == DAX_OK) status = dax_record_at(&archive, (size_t)entry, &record);
    if (status != DAX_OK) {
        fprintf(stderr, "%s entry %ld: %s\n", path, entry, dax_status_string(status));
        dax_close(&archive);
        return NULL;
    }
    uint8_t *data = malloc(record.decoded_size == 0 ? 1 : record.decoded_size);
    status = data == NULL ? DAX_NO_MEMORY :
        dax_decode(record.packed, record.packed_size, data, record.decoded_size);
    dax_close(&archive);
    if (status != DAX_OK) {
        fprintf(stderr, "%s entry %ld: %s\n", path, entry, dax_status_string(status));
        free(data);
        return NULL;
    }
    *size = record.decoded_size;
    return data;
}

/* Load one image as a picture; sprite selects the masked loader. */
static int load(const char *path, long entry, long index, int sprite, cok_picture *picture)
{
    cok_layout layout = cok_archive_layout(path);
    if (layout == COK_LAYOUT_UNSUPPORTED) {
        fprintf(stderr, "%s: unsupported graphics archive type\n", path);
        return 0;
    }
    size_t size;
    uint8_t *data = read_record(path, entry, &size);
    if (data == NULL) return 0;
    cok_images images = {0};
    cok_image_status image_status = layout == COK_LAYOUT_DELTA ?
        cok_images_undelta(data, size) : COK_IMAGE_OK;
    if (image_status == COK_IMAGE_OK)
        image_status = cok_images_parse(data, size,
                                        layout != COK_LAYOUT_SINGLE, &images);
    int ok = 0;
    if (image_status != COK_IMAGE_OK) {
        fprintf(stderr, "%s entry %ld: %s\n", path, entry, cok_image_status_string(image_status));
    } else if ((size_t)index >= images.count) {
        fprintf(stderr, "%s entry %ld: image %ld is out of range\n", path, entry, index);
    } else {
        const cok_image *image = &images.images[index];
        cok_picture_status picture_status = sprite ?
            cok_picture_load_sprite(picture, image, 1) : cok_picture_load(picture, image, 1, -1);
        if (picture_status == COK_PICTURE_OK) ok = 1;
        else fprintf(stderr, "%s: %s\n", path, cok_picture_status_string(picture_status));
    }
    cok_images_free(&images);
    free(data);
    return ok;
}

static int draw(cok_picture *screen, char **args, const char *command)
{
    long entry, index, x, y;
    if (!integer(args[1], 0, LONG_MAX, &entry) || !integer(args[2], 0, LONG_MAX, &index) ||
        !integer(args[3], INT_MIN / 8, INT_MAX / 8, &x) ||
        !integer(args[4], INT_MIN / 8, INT_MAX / 8, &y))
        return -1;
    int sprite = strcmp(command, "draw") != 0;
    cok_picture picture = {0};
    if (!load(args[0], entry, index, sprite, &picture)) return 0;
    if (strcmp(command, "mirror") == 0) {
        cok_picture flipped = {0};
        cok_picture_status status = cok_picture_create(&flipped, picture.units, picture.height, 1, 1);
        if (status == COK_PICTURE_OK) status = cok_picture_mirror(&flipped, &picture);
        cok_picture_free(&picture);
        if (status != COK_PICTURE_OK) {
            fprintf(stderr, "%s: %s\n", args[0], cok_picture_status_string(status));
            cok_picture_free(&flipped);
            return 0;
        }
        picture = flipped;
    }
    cok_picture_draw(screen, &picture, 0, (int)x, (int)y, sprite ? COK_DRAW_MASKED : 0, NULL);
    cok_picture_free(&picture);
    return 1;
}

static int load_font(const char *path, const char *entry_arg, cok_font *font)
{
    long entry;
    if (!integer(entry_arg, 0, LONG_MAX, &entry)) return -1;
    size_t size;
    uint8_t *data = read_record(path, entry, &size);
    if (data == NULL) return 0;
    cok_picture_status status = cok_font_load(font, data, size);
    free(data);
    if (status != COK_PICTURE_OK) {
        fprintf(stderr, "%s entry %ld: not an 8x8 font: %s\n", path, entry,
                cok_picture_status_string(status));
        return 0;
    }
    return 1;
}

/* text FONT ENTRY X Y FG BG STRING, or glyph FONT ENTRY GLYPH COUNT X Y FG BG. */
static int text(cok_picture *screen, char **args, int glyph)
{
    long number = 0, count = 0, x, y, fg, bg;
    char **rest = args + (glyph ? 4 : 2);
    if ((glyph && (!integer(args[2], 0, UINT8_MAX, &number) ||
                   !integer(args[3], 0, UINT8_MAX, &count))) ||
        !integer(rest[0], INT_MIN, INT_MAX, &x) || !integer(rest[1], INT_MIN, INT_MAX, &y) ||
        !integer(rest[2], 0, 15, &fg) || !integer(rest[3], 0, 15, &bg))
        return -1;
    cok_font font = {0};
    int ok = load_font(args[0], args[1], &font);
    if (ok <= 0) return ok;
    if (glyph)
        cok_text_glyph(screen, &font, (unsigned)number, (size_t)count, (int)x, (int)y,
                       (uint8_t)fg, (uint8_t)bg);
    else
        cok_text_string(screen, &font, rest[4], (int)x, (int)y, (uint8_t)fg, (uint8_t)bg);
    cok_font_free(&font);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { usage(argv[0]); return 1; }
    cok_picture screen = {0};
    if (cok_picture_create(&screen, 40, 200, 1, 0) != COK_PICTURE_OK) {
        fputs("out of memory\n", stderr);
        return 1;
    }
    int result = 1;
    for (int arg = 2; arg < argc; ) {
        const char *command = argv[arg];
        int ok = -1;
        if (strcmp(command, "fill") == 0 && argc - arg > 5) {
            long x, y, units, rows, color;
            if (integer(argv[arg + 1], INT_MIN, INT_MAX, &x) &&
                integer(argv[arg + 2], INT_MIN, INT_MAX, &y) &&
                integer(argv[arg + 3], 0, INT_MAX, &units) &&
                integer(argv[arg + 4], 0, INT_MAX, &rows) &&
                integer(argv[arg + 5], 0, 15, &color)) {
                cok_picture_fill(&screen, (int)x, (int)y, (size_t)units, (size_t)rows,
                                 (uint8_t)color);
                ok = 1;
            }
            arg += 6;
        } else if ((strcmp(command, "draw") == 0 || strcmp(command, "sprite") == 0 ||
                    strcmp(command, "mirror") == 0) && argc - arg > 5) {
            ok = draw(&screen, argv + arg + 1, command);
            arg += 6;
        } else if (strcmp(command, "text") == 0 && argc - arg > 7) {
            ok = text(&screen, argv + arg + 1, 0);
            arg += 8;
        } else if (strcmp(command, "glyph") == 0 && argc - arg > 8) {
            ok = text(&screen, argv + arg + 1, 1);
            arg += 9;
        }
        if (ok < 0) { usage(argv[0]); goto done; }
        if (ok == 0) goto done;
    }
    cok_image view = cok_picture_frame(&screen, 0);
    cok_image_status status = cok_image_bmp(&view, argv[1]);
    if (status != COK_IMAGE_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], cok_image_status_string(status));
        goto done;
    }
    result = 0;
done:
    cok_picture_free(&screen);
    return result;
}
