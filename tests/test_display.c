/* The display modes against the original: each screen type's drawing
 * (the title, big and small pictures, sprites, portraits, the 3D view,
 * the overhead map, combat, menus, the icon editor, recolouring, edges),
 * replayed on the port's picture and text routines in CGA, EGA and Tandy
 * mode, must leave what the original's graphics and text units (127f,
 * 1521) left in an 8086 emulator: the screen, the view buffer, pictures
 * and masks, compared by digest (tests/test_display_data.h). The view
 * buffer is 21 units by 168 rows, copied to the screen a unit right and a
 * cell down, its dirty rows only, as 127f:12e8 does. */
#include "config.h"
#include "dax.h"
#include "image.h"
#include "picture.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    const char *name;
    int mode;
    const char *const *script; /* lines, then NULL */
    const uint32_t *digests;
    size_t count;
} display_case;

#include "test_display_data.h"

enum { SLOTS = 32, VIEW_UNITS = 21, VIEW_ROWS = 168 };

typedef struct {
    int mode;
    cok_picture slots[SLOTS], screen, view;
    cok_font font;
    int dirty_lo[VIEW_ROWS], dirty_hi[VIEW_ROWS]; /* DS:4c38-4ed8, in units */
    const uint32_t *digests;
    size_t count, at;
    const char *name;
} replay;

static uint8_t *record(const char *archive, long entry, size_t *size)
{
    char path[64];
    snprintf(path, sizeof path, "Assets/%s", archive);
    dax_archive dax = {0};
    dax_record r;
    CHECK(dax_open(path, &dax) == DAX_OK && dax_record_at(&dax, (size_t)entry, &r) == DAX_OK);
    uint8_t *data = malloc(r.decoded_size + 1u);
    CHECK(data != NULL && dax_decode(r.packed, r.packed_size, data, r.decoded_size) == DAX_OK);
    *size = r.decoded_size;
    dax_close(&dax);
    return data;
}

/* 127f:0111: the record's image, as the game loads it in the mode; a
 * sprite as 6961:00e4 does. */
static void load(replay *p, int slot, const char *archive, long entry, long index, int t,
                 int sprite)
{
    size_t size;
    uint8_t *data = record(archive, entry, &size);
    cok_layout layout = cok_archive_layout(archive);
    if (layout == COK_LAYOUT_DELTA) CHECK(cok_images_undelta(data, size) == COK_IMAGE_OK);
    cok_images images = {0};
    CHECK(cok_images_parse(data, size, layout != COK_LAYOUT_SINGLE, &images) == COK_IMAGE_OK);
    CHECK((size_t)index < images.count);
    cok_picture *q = &p->slots[slot];
    cok_picture_free(q);
    const cok_image *image = &images.images[index];
    cok_picture_status status;
    if (p->mode == COK_DISPLAY_CGA) status = cok_picture_load_cga(q, image, 1, sprite ? 0 : t);
    else if (sprite) status = cok_picture_load_sprite(q, image, 1);
    else status = cok_picture_load(q, image, 1, t);
    CHECK(status == COK_PICTURE_OK);
    cok_images_free(&images);
    free(data);
}

static void mark(replay *p, long x, long y, long units, long rows)
{
    for (long r = y < 0 ? 0 : y; r < y + rows && r < VIEW_ROWS; ++r) {
        long a = x < 0 ? 0 : x, b = x + units - 1 > VIEW_UNITS - 1 ? VIEW_UNITS - 1 : x + units - 1;
        if (a > b) continue;
        if (a < p->dirty_lo[r]) p->dirty_lo[r] = (int)a;
        if (b > p->dirty_hi[r]) p->dirty_hi[r] = (int)b;
    }
}

static void show(replay *p)
{
    for (int r = 0; r < VIEW_ROWS; ++r) {
        if (p->dirty_lo[r] <= p->dirty_hi[r])
            memcpy(p->screen.pixels + (size_t)(r + 8) * 160 + (size_t)(p->dirty_lo[r] + 1) * 4,
                   p->view.pixels + (size_t)r * VIEW_UNITS * 4 + (size_t)p->dirty_lo[r] * 4,
                   (size_t)(p->dirty_hi[r] - p->dirty_lo[r] + 1) * 4);
        p->dirty_lo[r] = VIEW_UNITS;
        p->dirty_hi[r] = -1;
    }
}

static uint32_t fnv(const uint8_t *bytes, size_t size)
{
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < size; ++i) h = (h ^ bytes[i]) * 0x01000193u;
    return h;
}

static void compare(replay *p, const uint8_t *bytes, size_t size, const char *line)
{
    CHECK(p->at < p->count);
    uint32_t got = bytes == NULL ? 0 : fnv(bytes, size);
    if (got != p->digests[p->at]) {
        fprintf(stderr, "%s, %s mode: output %zu (%s) differs from the original's\n", p->name,
                (const char *[]){"CGA", "EGA", "Tandy"}[p->mode], p->at, line);
        exit(1);
    }
    ++p->at;
}

static void run(const display_case *c)
{
    static replay p;
    memset(&p, 0, sizeof p);
    p.mode = c->mode;
    p.digests = c->digests;
    p.count = c->count;
    p.name = c->name;
    CHECK(cok_picture_create(&p.screen, 40, 200, 1, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&p.view, VIEW_UNITS, VIEW_ROWS, 1, 0) == COK_PICTURE_OK);
    p.screen.cga = p.view.cga = c->mode == COK_DISPLAY_CGA;
    for (int r = 0; r < VIEW_ROWS; ++r) p.dirty_lo[r] = VIEW_UNITS, p.dirty_hi[r] = -1;
    for (const char *const *script = c->script; *script != NULL; ++script) {
        const char *line = *script;
        char cmd[16], archive[32];
        long v[40];
        int k = 0, used;
        CHECK(sscanf(line, "%15s%n", cmd, &used) == 1);
        const char *rest = line + used;
        if (strcmp(cmd, "load") == 0 || strcmp(cmd, "sprite") == 0) {
            long slot, entry, index, t = -1;
            CHECK(sscanf(rest, "%ld %31s %ld %ld %ld", &slot, archive, &entry, &index, &t) >= 4);
            load(&p, (int)slot, archive, entry, index, (int)t, cmd[0] == 's');
            continue;
        }
        if (strcmp(cmd, "font") == 0) {
            size_t size;
            uint8_t *data = record("8X8D1.DAX", 0, &size);
            CHECK(cok_font_load(&p.font, data, size) == COK_PICTURE_OK);
            free(data);
            continue;
        }
        while (k < 40 && sscanf(rest, "%ld%n", &v[k], &used) == 1) rest += used, ++k;
        cok_picture *a = k > 0 && v[0] >= 0 && v[0] < SLOTS ? &p.slots[v[0]] : NULL;
        if (strcmp(cmd, "create") == 0) {
            cok_picture_free(a);
            CHECK(cok_picture_create(a, (size_t)v[1], (size_t)v[2], (size_t)v[3], (int)v[4]) ==
                  COK_PICTURE_OK);
            a->cga = c->mode == COK_DISPLAY_CGA;
        } else if (strcmp(cmd, "draw") == 0 || strcmp(cmd, "udraw") == 0) {
            cok_picture_draw(&p.view, a, (size_t)v[1], (int)v[2], (int)v[3], (unsigned)v[4], NULL);
            mark(&p, v[2], v[3] * 8, a->units, a->height);
        } else if (strcmp(cmd, "vfill") == 0) {
            cok_picture_fill(&p.view, (int)v[0], (int)v[1], (size_t)v[2], (size_t)v[3],
                             (uint8_t)v[4]);
            mark(&p, v[0], v[1], v[2], v[3]);
        } else if (strcmp(cmd, "vshow") == 0) {
            show(&p);
        } else if (strcmp(cmd, "put") == 0) {
            cok_picture_draw(&p.screen, a, 0, (int)v[1], (int)v[2], 0, NULL);
        } else if (strcmp(cmd, "save") == 0) {
            cok_picture_draw(a, &p.screen, 0, (int)-v[1], (int)-v[2], 0, NULL);
        } else if (strcmp(cmd, "text") == 0) {
            cok_text_glyph(&p.screen, &p.font, (unsigned)v[4], (size_t)v[5], (int)v[0], (int)v[1],
                           (uint8_t)v[2], (uint8_t)v[3]);
        } else if (strcmp(cmd, "clear") == 0) {
            cok_picture_fill(&p.screen, (int)v[0], (int)v[1] * 8, (size_t)(v[2] - v[0] + 1),
                             (size_t)(v[3] - v[1] + 1) * 8, 0);
        } else if (strcmp(cmd, "recolor") == 0) {
            uint8_t from[16], to[16];
            for (int i = 0; i < 16; ++i) from[i] = (uint8_t)v[1 + i], to[i] = (uint8_t)v[17 + i];
            CHECK(cok_picture_recolor(a, from, to, 0, NULL) == COK_PICTURE_OK);
        } else if (strcmp(cmd, "mirror") == 0) {
            CHECK(cok_picture_mirror(a, &p.slots[v[1]]) == COK_PICTURE_OK);
        } else if (strcmp(cmd, "dump") == 0) {
            compare(&p, p.screen.pixels, p.screen.frame_size, line);
        } else if (strcmp(cmd, "vdump") == 0) {
            compare(&p, p.view.pixels, p.view.frame_size, line);
        } else if (strcmp(cmd, "pdump") == 0) {
            compare(&p, a->pixels, a->frame_size, line);
            compare(&p, a->mask, a->frame_size, line);
        } else {
            fprintf(stderr, "test_display: unknown command %s\n", cmd);
            exit(1);
        }
    }
    CHECK(p.at == p.count);
    for (int i = 0; i < SLOTS; ++i) cok_picture_free(&p.slots[i]);
    cok_picture_free(&p.screen);
    cok_picture_free(&p.view);
    cok_font_free(&p.font);
}

int main(void)
{
    size_t outputs = 0;
    for (size_t i = 0; i < sizeof display_cases / sizeof *display_cases; ++i) {
        run(&display_cases[i]);
        outputs += display_cases[i].count;
    }
    printf("display: %zu screen types and modes, %zu outputs as the original's\n",
           sizeof display_cases / sizeof *display_cases, outputs);
    return 0;
}
