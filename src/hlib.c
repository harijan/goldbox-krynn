#include "hlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t offset_at(const hlib_library *library, size_t i)
{
    return read_u32(library->data + 16 + 4 * i);
}

hlib_status hlib_parse(const uint8_t *data, size_t size, hlib_library *library)
{
    *library = (hlib_library){0};
    if (data == NULL || size < 16 || memcmp(data, "HLIB", 4) != 0) return HLIB_BAD_HEADER;
    size_t count = read_u16(data + 8);
    if ((size - 16) / 4 < count + 1) return HLIB_BAD_HEADER;
    hlib_library l = {data, size, count, read_u16(data + 10), {0}, 0};
    memcpy(l.type, data + 12, 4);
    uint32_t previous = (uint32_t)(16 + 4 * (count + 1));
    for (size_t i = 0; i <= count; ++i) {
        uint32_t offset = offset_at(&l, i);
        if (offset < previous || offset > size) return HLIB_BAD_RECORD;
        previous = offset;
    }
    if (l.flags & HLIB_ID_MAP) {
        const uint8_t *map;
        size_t map_size;
        if (count == 0 || hlib_record_at(&l, 0, &map, &map_size) != HLIB_OK || map_size < 2)
            return HLIB_BAD_RECORD;
        size_t n = read_u16(map);
        if ((map_size - 2) / 4 < n) return HLIB_BAD_RECORD;
        for (size_t i = 0; i < n; ++i)
            if (read_u16(map + 2 + 4 * i + 2) >= count) return HLIB_BAD_RECORD;
        l.ids = n;
    }
    *library = l;
    return HLIB_OK;
}

hlib_status hlib_read_file(const char *path, uint8_t **data, size_t *size)
{
    *data = NULL;
    *size = 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return HLIB_IO_ERROR;
    long length = -1;
    if (fseek(file, 0, SEEK_END) == 0) length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return HLIB_IO_ERROR;
    }
    uint8_t *bytes = malloc(length == 0 ? 1 : (size_t)length);
    if (bytes == NULL) {
        fclose(file);
        return HLIB_NO_MEMORY;
    }
    size_t got = fread(bytes, 1, (size_t)length, file);
    int failed = ferror(file);
    if (fclose(file) != 0 || failed || got != (size_t)length) {
        free(bytes);
        return HLIB_IO_ERROR;
    }
    *data = bytes;
    *size = (size_t)length;
    return HLIB_OK;
}

hlib_status hlib_record_at(const hlib_library *library, size_t index, const uint8_t **record,
                           size_t *size)
{
    if (index >= library->count) return HLIB_NO_RECORD;
    uint32_t from = offset_at(library, index), to = offset_at(library, index + 1);
    *record = library->data + from;
    *size = to - from;
    return HLIB_OK;
}

hlib_status hlib_id_at(const hlib_library *library, size_t i, unsigned *id, size_t *index)
{
    if (i >= library->ids) return HLIB_NO_RECORD;
    const uint8_t *map = library->data + offset_at(library, 0);
    *id = read_u16(map + 2 + 4 * i);
    *index = read_u16(map + 2 + 4 * i + 2);
    return HLIB_OK;
}

hlib_status hlib_record(const hlib_library *library, unsigned id, const uint8_t **record,
                        size_t *size)
{
    if (!(library->flags & HLIB_ID_MAP)) return hlib_record_at(library, id, record, size);
    for (size_t i = 0; i < library->ids; ++i) {
        unsigned key;
        size_t index;
        hlib_id_at(library, i, &key, &index);
        if (key == id) return hlib_record_at(library, index, record, size);
    }
    return HLIB_NO_RECORD;
}

bool hlib_is_library(const uint8_t *record, size_t size)
{
    return size >= 4 && memcmp(record, "HLIB", 4) == 0;
}

const char *hlib_status_string(hlib_status status)
{
    switch (status) {
    case HLIB_OK: return "success";
    case HLIB_IO_ERROR: return "file read failed";
    case HLIB_NO_MEMORY: return "out of memory";
    case HLIB_BAD_HEADER: return "not an HLIB library";
    case HLIB_BAD_RECORD: return "record offsets or id map out of bounds";
    case HLIB_NO_RECORD: return "no such record";
    }
    return "unknown error";
}
