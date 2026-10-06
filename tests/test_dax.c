#include "dax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

static void test_decode(void)
{
    /* Literal ABC, three repeated Zs, two no-ops, literal !. */
    const uint8_t packed[] = {2, 'A', 'B', 'C', 253, 'Z', 127, 128, 0, '!'};
    uint8_t out[8] = {0};
    CHECK(dax_decode(packed, sizeof packed, out, 7) == DAX_OK);
    CHECK(memcmp(out, "ABCZZZ!", 7) == 0);
    CHECK(out[7] == 0);
    const uint8_t literal_short[] = {2, 'A', 'B'};
    const uint8_t repeat_short[] = {255};
    const uint8_t repeat[] = {129, 42};
    uint8_t large[127];
    CHECK(dax_decode(literal_short, sizeof literal_short, out, 7) == DAX_BAD_STREAM);
    CHECK(dax_decode(repeat_short, sizeof repeat_short, out, 7) == DAX_BAD_STREAM);
    CHECK(dax_decode(packed, sizeof packed, out, 6) == DAX_SIZE_MISMATCH);
    CHECK(dax_decode(packed, sizeof packed, out, 8) == DAX_SIZE_MISMATCH);
    CHECK(dax_decode(repeat, sizeof repeat, large, sizeof large) == DAX_OK);
    for (size_t i = 0; i < sizeof large; ++i) CHECK(large[i] == 42);
    CHECK(dax_decode(NULL, 0, NULL, 0) == DAX_OK);
    CHECK(dax_decode(NULL, 0, out, 1) == DAX_SIZE_MISMATCH);
    CHECK(dax_decode(NULL, 1, out, 1) == DAX_BAD_STREAM);
    const uint8_t trailing[] = {0, 'A', 255};
    CHECK(dax_decode(trailing, sizeof trailing, out, 1) == DAX_BAD_STREAM);
}

static void test_directory(void)
{
    /* Two records with ID 9, deliberately stored in reverse payload order. */
    uint8_t bytes[] = {
        18, 0,
        9, 2, 0, 0, 0, 1, 0, 2, 0,
        9, 0, 0, 0, 0, 1, 0, 2, 0,
        0, 'A', 0, 'B'
    };
    dax_archive archive = {0};
    dax_record record;
    uint8_t out;
    CHECK(dax_parse(bytes, sizeof bytes, &archive) == DAX_OK);
    bytes[23] = 'X'; /* The archive owns a copy. */
    CHECK(archive.count == 2);
    CHECK(dax_record_at(&archive, 0, &record) == DAX_OK);
    CHECK(record.id == 9 && record.offset == 2 && record.decoded_size == 1);
    CHECK(dax_decode(record.packed, record.packed_size, &out, 1) == DAX_OK);
    CHECK(out == 'B');
    CHECK(dax_record_at(&archive, 1, &record) == DAX_OK);
    CHECK(record.id == 9);
    CHECK(dax_decode(record.packed, record.packed_size, &out, 1) == DAX_OK);
    CHECK(out == 'A');
    CHECK(dax_record_at(&archive, 2, &record) == DAX_BAD_RECORD);
    dax_close(&archive);
    CHECK(archive.data == NULL && archive.count == 0 && archive.size == 0);
    dax_close(&archive);

    CHECK(dax_parse(bytes, 1, &archive) == DAX_BAD_DIRECTORY);
    CHECK(dax_parse(bytes, 19, &archive) == DAX_BAD_DIRECTORY);
    bytes[0] = 17;
    CHECK(dax_parse(bytes, sizeof bytes, &archive) == DAX_BAD_DIRECTORY);
    bytes[0] = 18;
    bytes[3] = 255; bytes[4] = 255; bytes[5] = 255; bytes[6] = 255;
    CHECK(dax_parse(bytes, sizeof bytes, &archive) == DAX_BAD_RECORD);
    memset(bytes + 3, 0, 4);
    bytes[9] = 5;
    CHECK(dax_parse(bytes, sizeof bytes, &archive) == DAX_BAD_RECORD);
    CHECK(archive.data == NULL);
    const uint8_t empty[] = {0, 0};
    CHECK(dax_parse(empty, sizeof empty, &archive) == DAX_OK);
    CHECK(archive.count == 0);
    dax_close(&archive);
}

static void test_original_title(void)
{
    dax_archive archive = {0};
    dax_record record;
    CHECK(dax_open("Assets/TITLE.DAX", &archive) == DAX_OK);
    CHECK(archive.count == 4);
    CHECK(dax_record_at(&archive, 0, &record) == DAX_OK);
    CHECK(record.id == 1 && record.decoded_size == 32017 && record.packed_size == 9716);
    uint8_t *out = malloc(record.decoded_size);
    CHECK(out != NULL);
    CHECK(dax_decode(record.packed, record.packed_size, out, record.decoded_size) == DAX_OK);
    const uint8_t header[] = {200, 0, 40, 0, 0, 0, 0, 0, 1, 2, 34, 34, 17, 2, 17, 2, 51};
    CHECK(memcmp(out, header, sizeof header) == 0);
    CHECK(out[17] == 0x55);
    free(out);
    dax_close(&archive);
    CHECK(dax_open("tests/no-such-archive.dax", &archive) == DAX_IO_ERROR);
    CHECK(archive.data == NULL);
}

int main(void)
{
    test_decode();
    test_directory();
    test_original_title();
    puts("DAX tests passed");
    return EXIT_SUCCESS;
}
