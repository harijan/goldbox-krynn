#include "image.h"
#include "dax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static void test_frames_and_colors(void)
{
    /* 8x1, two frames; independently specified high/low nibble palette values. */
    uint8_t data[25] = {1, 0, 1, 0, 3, 0, 4, 0, 2};
    const uint8_t pixels[] = {0x05, 0x6f, 0x19, 0x2a, 0x43, 0x78, 0xbc, 0xde};
    memcpy(data + 17, pixels, sizeof pixels);
    cok_images list = {0};
    CHECK(cok_images_parse(data, sizeof data, 0, &list) == COK_IMAGE_OK);
    CHECK(list.count == 2);
    CHECK(list.images[0].width == 8 && list.images[0].height == 1);
    CHECK(list.images[0].x == 3 && list.images[0].y == 4);
    CHECK(list.images[1].frame == 1 && list.images[1].group == 0);
    uint8_t rgb[24];
    cok_image_rgb(&list.images[0], rgb);
    const uint8_t expected[] = {
        0,0,0, 170,0,170, 170,85,0, 255,255,255,
        0,0,170, 85,85,255, 0,170,0, 85,255,85
    };
    CHECK(memcmp(rgb, expected, sizeof expected) == 0);
    CHECK(cok_image_bmp(&list.images[0], "build/test-image.bmp") == COK_IMAGE_OK);
    FILE *f = fopen("build/test-image.bmp", "rb");
    CHECK(f != NULL);
    uint8_t bmp[78];
    CHECK(fread(bmp, 1, sizeof bmp, f) == sizeof bmp);
    CHECK(fgetc(f) == EOF);
    CHECK(fclose(f) == 0);
    CHECK(bmp[0] == 'B' && bmp[1] == 'M' && bmp[2] == 78 && bmp[10] == 54);
    CHECK(bmp[18] == 8 && bmp[22] == 1 && bmp[28] == 24);
    CHECK(bmp[54+6] == 0 && bmp[54+7] == 85 && bmp[54+8] == 170); /* Brown BGR. */
    CHECK(cok_image_bmp(&list.images[0], "build/no-such-dir/image.bmp") == COK_IMAGE_IO);
    cok_images_free(&list);
    CHECK(list.count == 0 && list.images == NULL);
    cok_images_free(&list);
    CHECK(cok_images_parse(data, 24, 0, &list) == COK_IMAGE_INVALID);
    data[8] = 0;
    CHECK(cok_images_parse(data, sizeof data, 0, &list) == COK_IMAGE_INVALID);
    data[8] = 2; data[0] = 0;
    CHECK(cok_images_parse(data, sizeof data, 0, &list) == COK_IMAGE_INVALID);
    data[0] = 255; data[1] = 255; data[2] = 255; data[3] = 255;
    CHECK(cok_images_parse(data, sizeof data, 0, &list) == COK_IMAGE_INVALID);
    CHECK(cok_images_parse(NULL, 0, 0, &list) == COK_IMAGE_INVALID);
}

static void test_collection(void)
{
    uint8_t bytes[51] = {2};
    bytes[1] = 20; /* Per-group metadata is not part of the image header. */
    bytes[5] = 1; bytes[7] = 1; bytes[13] = 1; bytes[22] = 0x55;
    bytes[30] = 1; bytes[32] = 1; bytes[38] = 1; bytes[50] = 0xff;
    cok_images list = {0};
    CHECK(cok_images_parse(bytes, sizeof bytes, 1, &list) == COK_IMAGE_OK);
    CHECK(list.count == 2 && list.images[1].group == 1);
    CHECK(list.images[0].pixels[0] == 0x55 && list.images[1].pixels[3] == 0xff);
    cok_images_free(&list);
    CHECK(cok_images_parse(bytes, 50, 1, &list) == COK_IMAGE_INVALID);
    CHECK(list.images == NULL && list.count == 0);
    bytes[0] = 1;
    CHECK(cok_images_parse(bytes, sizeof bytes, 1, &list) == COK_IMAGE_INVALID);
}

static void test_title(void)
{
    dax_archive archive = {0};
    dax_record record;
    CHECK(dax_open("Assets/TITLE.DAX", &archive) == DAX_OK);
    for (size_t i = 0; i < archive.count; ++i) {
        CHECK(dax_record_at(&archive, i, &record) == DAX_OK);
        uint8_t *data = malloc(record.decoded_size);
        CHECK(data != NULL);
        CHECK(dax_decode(record.packed, record.packed_size, data, record.decoded_size) == DAX_OK);
        cok_images list = {0};
        CHECK(cok_images_parse(data, record.decoded_size, 0, &list) == COK_IMAGE_OK);
        CHECK(list.count == 1);
        const size_t widths[] = {320, 320, 240, 320};
        const size_t heights[] = {200, 200, 88, 88};
        CHECK(list.images[0].width == widths[i] && list.images[0].height == heights[i]);
        if (i == 0) {
            uint8_t *rgb = malloc(320*200*3);
            CHECK(rgb != NULL);
            cok_image_rgb(&list.images[0], rgb);
            CHECK(rgb[0] == 170 && rgb[1] == 0 && rgb[2] == 170);
            free(rgb);
        }
        cok_images_free(&list);
        free(data);
    }
    dax_close(&archive);
}

int main(void)
{
    test_frames_and_colors();
    test_collection();
    test_title();
    puts("Image tests passed");
    return 0;
}
