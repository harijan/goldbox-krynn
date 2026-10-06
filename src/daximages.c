#include "dax.h"
#include "image.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--entry N [--frame N]] ARCHIVE OUTDIR\n"
                    "Exports BMP images and an index.html contact sheet.\n"
                    "Default: all entries and frames. Numbers are zero-based.\n", program);
}

static int number(const char *s, size_t *value)
{
    if (*s == '\0') return 0;
    for (const char *p = s; *p; ++p) if (*p < '0' || *p > '9') return 0;
    errno = 0;
    char *end;
    unsigned long long n = strtoull(s, &end, 10);
    if (errno || *end || n > SIZE_MAX) return 0;
    *value = (size_t)n;
    return 1;
}

static char *join(const char *directory, const char *name)
{
    size_t a = strlen(directory), b = strlen(name);
    if (a > SIZE_MAX - b - 2) return NULL;
    char *path = malloc(a + b + 2);
    if (path != NULL) snprintf(path, a + b + 2, "%s/%s", directory, name);
    return path;
}

static void html_text(FILE *file, const char *s)
{
    for (; *s; ++s) {
        switch (*s) {
        case '&': fputs("&amp;", file); break;
        case '<': fputs("&lt;", file); break;
        case '>': fputs("&gt;", file); break;
        case '"': fputs("&quot;", file); break;
        case '\'': fputs("&#39;", file); break;
        default: fputc((unsigned char)*s, file); break;
        }
    }
}

int main(int argc, char **argv)
{
    size_t entry_index = 0, frame_index = 0;
    int selected_entry = 0, selected_frame = 0;
    int arg = 1;
    while (arg < argc && argv[arg][0] == '-') {
        if (strcmp(argv[arg], "--entry") == 0 && !selected_entry && arg + 1 < argc) {
            selected_entry = 1;
            if (!number(argv[++arg], &entry_index)) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[arg], "--frame") == 0 && !selected_frame && arg + 1 < argc) {
            selected_frame = 1;
            if (!number(argv[++arg], &frame_index)) { usage(argv[0]); return 1; }
        } else {
            usage(argv[0]); return 1;
        }
        ++arg;
    }
    if (argc - arg != 2 || (selected_frame && !selected_entry)) { usage(argv[0]); return 1; }
    const char *input = argv[arg], *output = argv[arg + 1];
    cok_layout layout = cok_archive_layout(input);
    if (layout == COK_LAYOUT_UNSUPPORTED) {
        fprintf(stderr, "%s: unsupported graphics archive type\n", input);
        return 1;
    }
    dax_archive archive = {0};
    dax_status status = dax_open(input, &archive);
    if (status != DAX_OK) {
        fprintf(stderr, "%s: %s\n", input, dax_status_string(status)); return 1;
    }
    if (selected_entry && entry_index >= archive.count) {
        fprintf(stderr, "%s: entry %zu is out of range\n", input, entry_index);
        dax_close(&archive); return 1;
    }
    if (mkdir(output, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "%s: %s\n", output, strerror(errno));
        dax_close(&archive); return 1;
    }
    char *sheet_path = join(output, "index.html");
    FILE *sheet = sheet_path == NULL ? NULL : fopen(sheet_path, "w");
    if (sheet == NULL) {
        fprintf(stderr, "%s: cannot create contact sheet\n", output);
        free(sheet_path); dax_close(&archive); return 1;
    }
    fputs("<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
          "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
          "<title>DAX contact sheet</title><style>"
          "body{background:#191919;color:#eee;font:16px system-ui;margin:24px}"
          "main{display:flex;flex-wrap:wrap;gap:18px;align-items:start}"
          "figure{margin:0;background:#282828;padding:12px;border-radius:6px}"
          "img{image-rendering:pixelated;max-width:100%;height:auto}"
          "figcaption{margin-top:8px;font-size:13px}a{color:inherit}"
          "</style><h1>", sheet);
    html_text(sheet, input);
    fputs("</h1><p>Opaque EGA previews at native pixel dimensions. Click an image to open it.</p><main>\n", sheet);
    size_t exported = 0;
    int failed = 0;
    size_t begin = selected_entry ? entry_index : 0;
    size_t end = selected_entry ? entry_index + 1 : archive.count;
    for (size_t i = begin; i < end; ++i) {
        dax_record record;
        status = dax_record_at(&archive, i, &record);
        if (status != DAX_OK) { failed = 1; break; }
        uint8_t *data = malloc(record.decoded_size == 0 ? 1 : record.decoded_size);
        status = data == NULL ? DAX_NO_MEMORY :
            dax_decode(record.packed, record.packed_size, data, record.decoded_size);
        if (status != DAX_OK) {
            fprintf(stderr, "entry %zu: %s\n", i, dax_status_string(status));
            free(data); failed = 1; continue;
        }
        cok_images images = {0};
        cok_image_status image_status = layout == COK_LAYOUT_DELTA ?
            cok_images_undelta(data, record.decoded_size) : COK_IMAGE_OK;
        if (image_status == COK_IMAGE_OK)
            image_status = cok_images_parse(data, record.decoded_size,
                                            layout != COK_LAYOUT_SINGLE, &images);
        if (image_status != COK_IMAGE_OK || (selected_frame && frame_index >= images.count)) {
            fprintf(stderr, "entry %zu ID %u: %s\n", i, (unsigned)record.id,
                    image_status != COK_IMAGE_OK ? cok_image_status_string(image_status) : "frame out of range");
            cok_images_free(&images); free(data); failed = 1; continue;
        }
        for (size_t j = 0; j < images.count; ++j) {
            if (selected_frame && j != frame_index) continue;
            const cok_image *image = &images.images[j];
            char name[160];
            snprintf(name, sizeof name, "entry-%03zu-id-%03u-group-%03zu-frame-%03zu.bmp",
                     i, (unsigned)record.id, image->group, image->frame);
            char *path = join(output, name);
            image_status = path == NULL ? COK_IMAGE_MEMORY : cok_image_bmp(image, path);
            free(path);
            if (image_status != COK_IMAGE_OK) {
                fprintf(stderr, "%s: %s\n", name, cok_image_status_string(image_status));
                failed = 1; continue;
            }
            fprintf(sheet, "<figure><a href=\"%s\"><img src=\"%s\" width=\"%zu\" height=\"%zu\" "
                    "loading=\"lazy\" alt=\"Entry %zu ID %u image %zu\"></a>"
                    "<figcaption>Entry %zu · ID %u · image %zu<br>Group %zu · frame %zu · %zu×%zu</figcaption></figure>\n",
                    name, name, image->width, image->height, i, (unsigned)record.id, j,
                    i, (unsigned)record.id, j, image->group, image->frame, image->width, image->height);
            ++exported;
        }
        cok_images_free(&images);
        free(data);
    }
    fputs("</main></html>\n", sheet);
    if (ferror(sheet)) failed = 1;
    if (fclose(sheet) != 0) failed = 1;
    printf("Exported %zu images; contact sheet: %s%s\n", exported, sheet_path,
           failed ? " (errors found)" : "");
    free(sheet_path);
    dax_close(&archive);
    return failed || exported == 0 ? 1 : 0;
}
