#include "picture.h"
#include "dax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Picture with each byte set to its index plus base, so offsets are visible. */
static cok_picture numbered(size_t units, size_t height, size_t frames, uint8_t base)
{
    cok_picture picture = {0};
    CHECK(cok_picture_create(&picture, units, height, frames, 0) == COK_PICTURE_OK);
    for (size_t i = 0; i < frames * picture.frame_size; ++i)
        picture.pixels[i] = (uint8_t)(base + i);
    return picture;
}

static void test_create(void)
{
    cok_picture picture = {0};
    CHECK(cok_picture_create(&picture, 3, 2, 2, 1) == COK_PICTURE_OK);
    CHECK(picture.frame_size == 24 && picture.frames == 2 && picture.mask != NULL);
    for (size_t i = 0; i < 48; ++i) CHECK(picture.pixels[i] == 0 && picture.mask[i] == 0);
    cok_picture_free(&picture);
    CHECK(picture.pixels == NULL && picture.mask == NULL && picture.frames == 0);
    cok_picture_free(&picture);
    CHECK(cok_picture_create(&picture, 0, 2, 1, 0) == COK_PICTURE_INVALID);
    CHECK(cok_picture_create(&picture, 1, 0, 1, 0) == COK_PICTURE_INVALID);
    CHECK(cok_picture_create(&picture, 1, 1, 0, 0) == COK_PICTURE_INVALID);
    CHECK(cok_picture_create(&picture, 65536, 1, 1, 0) == COK_PICTURE_INVALID);
    CHECK(picture.pixels == NULL);
}

static void test_load(void)
{
    const uint8_t pixels[] = {0x10, 0x02, 0x00, 0xd3, 0x45, 0x60, 0x0d, 0x78};
    const uint8_t colors[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    cok_image frames[2] = {
        {8, 1, 5, 6, 0, 0, pixels, colors},
        {8, 1, 5, 6, 0, 1, pixels + 4, colors},
    };
    cok_picture picture = {0};
    CHECK(cok_picture_load(&picture, frames, 2, -1) == COK_PICTURE_OK);
    CHECK(picture.mask == NULL && picture.frames == 2 && picture.units == 1);
    CHECK(picture.x == 5 && picture.y == 6 && memcmp(picture.colors, colors, 8) == 0);
    CHECK(memcmp(picture.pixels, pixels, 8) == 0);
    cok_picture_free(&picture);

    CHECK(cok_picture_load(&picture, frames, 2, 0) == COK_PICTURE_OK);
    const uint8_t mask[] = {0x0f, 0xf0, 0xff, 0x00, 0x00, 0x0f, 0xf0, 0x00};
    CHECK(memcmp(picture.mask, mask, 8) == 0);
    CHECK(memcmp(picture.pixels, pixels, 8) == 0); /* Transparent pixels are already 0. */
    cok_picture_free(&picture);

    CHECK(cok_picture_load(&picture, frames, 2, 13) == COK_PICTURE_OK);
    const uint8_t cleared[] = {0x10, 0x02, 0x00, 0x03, 0x45, 0x60, 0x00, 0x78};
    CHECK(memcmp(picture.pixels, cleared, 8) == 0);
    CHECK(picture.mask[3] == 0xf0 && picture.mask[6] == 0x0f && picture.mask[0] == 0);
    cok_picture_free(&picture);

    /* Colour 0 is transparent; colour 13 becomes opaque black. */
    CHECK(cok_picture_load_sprite(&picture, frames, 2) == COK_PICTURE_OK);
    const uint8_t sprite[] = {0x10, 0x02, 0x00, 0x03, 0x45, 0x60, 0x00, 0x78};
    CHECK(memcmp(picture.pixels, sprite, 8) == 0);
    CHECK(memcmp(picture.mask, mask, 8) == 0);
    cok_picture_free(&picture);

    frames[1].width = 16;
    CHECK(cok_picture_load(&picture, frames, 2, -1) == COK_PICTURE_INVALID);
    CHECK(cok_picture_load(&picture, frames, 1, 16) == COK_PICTURE_INVALID);
    CHECK(cok_picture_load(&picture, frames, 0, -1) == COK_PICTURE_INVALID);
    CHECK(picture.pixels == NULL);
}

static void test_draw_opaque_and_clipping(void)
{
    cok_picture screen = {0};
    CHECK(cok_picture_create(&screen, 4, 32, 1, 0) == COK_PICTURE_OK);
    cok_picture src = numbered(2, 16, 2, 1); /* 8 bytes per row, 128 per frame. */

    cok_picture_draw(&screen, &src, 1, 1, 1, 0, NULL);
    for (size_t row = 0; row < 32; ++row)
        for (size_t col = 0; col < 16; ++col) {
            uint8_t want = 0;
            if (row >= 8 && row < 24 && col >= 4 && col < 12)
                want = (uint8_t)(1 + 128 + (row - 8) * 8 + (col - 4));
            CHECK(screen.pixels[row * 16 + col] == want);
        }

    /* Clipped on the left and top: the visible part keeps source alignment. */
    memset(screen.pixels, 0, screen.frame_size);
    cok_picture_draw(&screen, &src, 0, -1, -1, 0, NULL);
    for (size_t row = 0; row < 8; ++row)
        for (size_t col = 0; col < 4; ++col)
            CHECK(screen.pixels[row * 16 + col] == (uint8_t)(1 + (row + 8) * 8 + 4 + col));
    CHECK(screen.pixels[4] == 0 && screen.pixels[8 * 16] == 0);

    /* Clipped on the right and bottom. */
    memset(screen.pixels, 0, screen.frame_size);
    cok_picture_draw(&screen, &src, 0, 3, 3, 0, NULL);
    for (size_t row = 24; row < 32; ++row)
        for (size_t col = 12; col < 16; ++col)
            CHECK(screen.pixels[row * 16 + col] == (uint8_t)(1 + (row - 24) * 8 + (col - 12)));
    CHECK(screen.pixels[23 * 16 + 12] == 0);

    /* Fully outside or bad frame: nothing drawn. */
    memset(screen.pixels, 0, screen.frame_size);
    cok_picture_draw(&screen, &src, 0, 4, 0, 0, NULL);
    cok_picture_draw(&screen, &src, 0, -2, 0, 0, NULL);
    cok_picture_draw(&screen, &src, 0, 0, 4, 0, NULL);
    cok_picture_draw(&screen, &src, 0, 0, -2, 0, NULL);
    cok_picture_draw(&screen, &src, 2, 0, 0, 0, NULL);
    for (size_t i = 0; i < screen.frame_size; ++i) CHECK(screen.pixels[i] == 0);

    /* MASKED without a mask falls back to an opaque draw. */
    cok_picture_draw(&screen, &src, 0, 0, 0, COK_DRAW_MASKED, NULL);
    CHECK(screen.pixels[0] == 1 && screen.pixels[17] == 10);
    cok_picture_free(&src);
    cok_picture_free(&screen);
}

static void test_draw_masked_and_save(void)
{
    cok_picture screen = numbered(2, 8, 1, 0x80);
    const uint8_t pixels[] = {0x00, 0x12, 0x30, 0x04};
    cok_image image = {8, 1, 0, 0, 0, 0, pixels, NULL};
    cok_picture sprite = {0}, save = {0};
    CHECK(cok_picture_load(&sprite, &image, 1, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&save, 1, 1, 1, 0) == COK_PICTURE_OK);
    uint8_t before[64];
    memcpy(before, screen.pixels, sizeof before);

    cok_picture_draw(&screen, &sprite, 0, 1, 0, COK_DRAW_MASKED | COK_DRAW_SAVE, &save);
    /* Destination row 0, bytes 4-7, hold 0x84..0x87 before the draw. */
    CHECK(screen.pixels[4] == 0x84 && screen.pixels[5] == 0x12);
    CHECK(screen.pixels[6] == 0x36 && screen.pixels[7] == 0x84);
    CHECK(memcmp(save.pixels, before + 4, 4) == 0);
    CHECK(memcmp(screen.pixels, before, 4) == 0 && memcmp(screen.pixels + 8, before + 8, 56) == 0);

    /* Restoring the saved bytes with an opaque draw undoes the sprite. */
    cok_picture_draw(&screen, &save, 0, 1, 0, 0, NULL);
    CHECK(memcmp(screen.pixels, before, sizeof before) == 0);

    /* SAVE needs a masked draw and a save picture the size of src. */
    memset(save.pixels, 0, 4);
    cok_picture_draw(&screen, &sprite, 0, 1, 0, COK_DRAW_SAVE, &save);
    CHECK(save.pixels[0] == 0 && screen.pixels[4] == 0x00);
    cok_picture wrong = {0};
    CHECK(cok_picture_create(&wrong, 2, 1, 1, 0) == COK_PICTURE_OK);
    cok_picture_draw(&screen, &sprite, 0, 0, 0, COK_DRAW_MASKED | COK_DRAW_SAVE, &wrong);
    CHECK(wrong.pixels[0] == 0);
    cok_picture_free(&wrong);
    cok_picture_free(&save);
    cok_picture_free(&sprite);
    cok_picture_free(&screen);
}

static void test_fill(void)
{
    cok_picture screen = {0};
    CHECK(cok_picture_create(&screen, 3, 4, 2, 0) == COK_PICTURE_OK);
    cok_picture_fill(&screen, 1, 1, 1, 2, 0x1a);
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 12; ++col) {
            int inside = row >= 1 && row < 3 && col >= 4 && col < 8;
            CHECK(screen.pixels[row * 12 + col] == (inside ? 0xaa : 0));
        }
    memset(screen.pixels, 0, 2 * screen.frame_size);
    cok_picture_fill(&screen, -1, -2, 3, 3, 5);
    CHECK(screen.pixels[0] == 0x55 && screen.pixels[7] == 0x55 && screen.pixels[8] == 0);
    CHECK(screen.pixels[12] == 0);
    cok_picture_fill(&screen, 2, 3, 5, 5, 7);
    CHECK(screen.pixels[3 * 12 + 8] == 0x77 && screen.pixels[3 * 12 + 11] == 0x77);
    CHECK(screen.pixels[3 * 12 + 7] == 0);
    for (size_t i = screen.frame_size; i < 2 * screen.frame_size; ++i)
        CHECK(screen.pixels[i] == 0); /* Frame 0 only. */
    cok_picture_fill(&screen, 3, 0, 1, 1, 9);
    cok_picture_fill(&screen, 0, 4, 1, 1, 9);
    cok_picture_fill(&screen, 0, 0, 0, 1, 9);
    CHECK(screen.pixels[0] == 0x55 && screen.pixels[11] == 0);
    cok_picture_free(&screen);
}

static unsigned sequence(void *context, unsigned range)
{
    unsigned *state = context;
    CHECK(range == 4);
    return (*state)++ % 4;
}

static void test_recolor(void)
{
    cok_picture picture = {0};
    CHECK(cok_picture_create(&picture, 1, 1, 2, 0) == COK_PICTURE_OK);
    const uint8_t pixels[] = {0x12, 0x21, 0x33, 0x1f, 0x12, 0x21, 0x33, 0x1f};
    memcpy(picture.pixels, pixels, 8);
    uint8_t from[16], to[16];
    for (uint8_t i = 0; i < 16; ++i) from[i] = to[i] = i;
    to[1] = 2; to[2] = 1; /* A swap must not cascade. */

    CHECK(cok_picture_recolor(&picture, from, to, 1, NULL) == COK_PICTURE_OK);
    const uint8_t frame1[] = {0x12, 0x21, 0x33, 0x1f, 0x21, 0x12, 0x33, 0x2f};
    CHECK(memcmp(picture.pixels, frame1, 8) == 0);
    CHECK(cok_picture_recolor(&picture, from, to, -1, NULL) == COK_PICTURE_OK);
    const uint8_t both[] = {0x21, 0x12, 0x33, 0x2f, 0x12, 0x21, 0x33, 0x1f};
    CHECK(memcmp(picture.pixels, both, 8) == 0);
    CHECK(cok_picture_recolor(&picture, from, to, 2, NULL) == COK_PICTURE_INVALID);

    /* Dithering draws once per matching nibble and recolours on 0. */
    memset(picture.pixels, 0x11, 8);
    for (uint8_t i = 0; i < 16; ++i) to[i] = i;
    to[1] = 5;
    unsigned state = 0;
    cok_random random = {sequence, &state};
    CHECK(cok_picture_recolor(&picture, from, to, 0, &random) == COK_PICTURE_OK);
    CHECK(state == 8);
    const uint8_t dithered[] = {0x51, 0x11, 0x51, 0x11, 0x11, 0x11, 0x11, 0x11};
    CHECK(memcmp(picture.pixels, dithered, 8) == 0);
    cok_picture_free(&picture);
}

static void test_mirror(void)
{
    const uint8_t pixels[] = {0x12, 0x34, 0x56, 0x70, 0x9a, 0xbc, 0xde, 0xf0};
    cok_image image = {8, 2, 0, 0, 0, 0, pixels, (const uint8_t *)"ABCDEFGH"};
    cok_picture src = {0}, dst = {0};
    CHECK(cok_picture_load(&src, &image, 1, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&dst, 1, 2, 1, 1) == COK_PICTURE_OK);
    CHECK(cok_picture_mirror(&dst, &src) == COK_PICTURE_OK);
    const uint8_t mirrored[] = {0x07, 0x65, 0x43, 0x21, 0x0f, 0xed, 0xcb, 0xa9};
    const uint8_t mask[] = {0xf0, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00};
    CHECK(memcmp(dst.pixels, mirrored, 8) == 0);
    CHECK(memcmp(dst.mask, mask, 8) == 0);
    CHECK(memcmp(dst.colors, "ABCDEFGH", 8) == 0);
    CHECK(cok_picture_mirror(&src, &src) == COK_PICTURE_INVALID);
    cok_picture unmasked = {0};
    CHECK(cok_picture_create(&unmasked, 1, 2, 1, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_mirror(&unmasked, &src) == COK_PICTURE_INVALID);
    cok_picture_free(&unmasked);
    cok_picture_free(&dst);
    cok_picture_free(&src);
}

static void test_undelta(void)
{
    /* Three 8x1 groups; later groups are XOR deltas against the first. */
    uint8_t data[1 + 3 * 25] = {3};
    for (size_t g = 0; g < 3; ++g) {
        uint8_t *header = data + 1 + g * 25 + 4;
        header[0] = 1; header[2] = 1; header[8] = 1;
    }
    const uint8_t base[] = {0x12, 0x34, 0x56, 0x78};
    memcpy(data + 1 + 4 + 17, base, 4);
    data[1 + 25 + 21] = 0x0f;
    data[1 + 50 + 24] = 0xf0;
    CHECK(cok_images_undelta(data, sizeof data) == COK_IMAGE_OK);
    const uint8_t second[] = {0x1d, 0x34, 0x56, 0x78}, third[] = {0x12, 0x34, 0x56, 0x88};
    CHECK(memcmp(data + 1 + 4 + 17, base, 4) == 0);
    CHECK(memcmp(data + 1 + 25 + 21, second, 4) == 0);
    CHECK(memcmp(data + 1 + 50 + 21, third, 4) == 0);
    data[1 + 25 + 4 + 2] = 2; /* Mismatched groups are rejected. */
    CHECK(cok_images_undelta(data, sizeof data) == COK_IMAGE_INVALID);

    CHECK(cok_archive_layout("Assets/pic2.dax") == COK_LAYOUT_DELTA);
    CHECK(cok_archive_layout("SPRIT1.DAX") == COK_LAYOUT_COLLECTION);
    CHECK(cok_archive_layout("x/TITLE.DAX") == COK_LAYOUT_SINGLE);
    CHECK(cok_archive_layout("GEO1.DAX") == COK_LAYOUT_UNSUPPORTED);

    /* In PIC1 entry 0, deltas are mostly zero; decoded frames are not. */
    dax_archive archive = {0};
    dax_record record;
    CHECK(dax_open("Assets/PIC1.DAX", &archive) == DAX_OK);
    CHECK(dax_record_at(&archive, 0, &record) == DAX_OK);
    uint8_t *bytes = malloc(record.decoded_size);
    CHECK(bytes != NULL);
    CHECK(dax_decode(record.packed, record.packed_size, bytes, record.decoded_size) == DAX_OK);
    CHECK(cok_images_undelta(bytes, record.decoded_size) == COK_IMAGE_OK);
    cok_images list = {0};
    CHECK(cok_images_parse(bytes, record.decoded_size, 1, &list) == COK_IMAGE_OK);
    CHECK(list.count == 8);
    for (size_t i = 0; i < list.count; ++i) {
        size_t size = list.images[i].width / 2 * list.images[i].height, zero = 0;
        for (size_t j = 0; j < size; ++j) zero += list.images[i].pixels[j] == 0;
        CHECK(zero < size / 2);
    }
    cok_images_free(&list);
    free(bytes);
    dax_close(&archive);
}

static void test_frame_view(void)
{
    cok_picture picture = numbered(1, 2, 2, 0);
    picture.x = 3;
    cok_image view = cok_picture_frame(&picture, 1);
    CHECK(view.width == 8 && view.height == 2 && view.x == 3 && view.frame == 1);
    CHECK(view.pixels == picture.pixels + 8 && view.pixels[0] == 8);
    cok_picture_free(&picture);
}

int main(void)
{
    test_create();
    test_load();
    test_draw_opaque_and_clipping();
    test_draw_masked_and_save();
    test_fill();
    test_recolor();
    test_mirror();
    test_undelta();
    test_frame_view();
    puts("Picture tests passed");
    return 0;
}
