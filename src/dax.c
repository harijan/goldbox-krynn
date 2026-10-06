#include "dax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static dax_status validate(const uint8_t *data, size_t size, size_t *count)
{
    if (data == NULL || size < 2) return DAX_BAD_DIRECTORY;
    size_t directory_size = read_u16(data);
    if (directory_size % 9 != 0 || directory_size > size - 2)
        return DAX_BAD_DIRECTORY;
    size_t payload_size = size - 2 - directory_size;
    *count = directory_size / 9;
    for (size_t i = 0; i < *count; ++i) {
        const uint8_t *entry = data + 2 + i * 9;
        uint32_t offset = read_u32(entry + 1);
        uint16_t packed_size = read_u16(entry + 7);
        /* Subtract only after checking offset; never add unchecked offsets. */
        if (offset > payload_size || packed_size > payload_size - offset)
            return DAX_BAD_RECORD;
    }
    return DAX_OK;
}

dax_status dax_parse(const uint8_t *data, size_t size, dax_archive *archive)
{
    *archive = (dax_archive){0};
    size_t count;
    dax_status status = validate(data, size, &count);
    if (status != DAX_OK) return status;
    uint8_t *copy = malloc(size);
    if (copy == NULL) return DAX_NO_MEMORY;
    memcpy(copy, data, size);
    *archive = (dax_archive){copy, size, count};
    return DAX_OK;
}

dax_status dax_open(const char *path, dax_archive *archive)
{
    *archive = (dax_archive){0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) return DAX_IO_ERROR;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return DAX_IO_ERROR;
    }
    long length = ftell(file);
    if (length < 0 || (uintmax_t)length > SIZE_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return DAX_IO_ERROR;
    }
    size_t size = (size_t)length;
    uint8_t *data = malloc(size == 0 ? 1 : size);
    if (data == NULL) {
        fclose(file);
        return DAX_NO_MEMORY;
    }
    size_t bytes_read = fread(data, 1, size, file);
    int read_failed = ferror(file);
    int close_failed = fclose(file);
    if (bytes_read != size || read_failed || close_failed) {
        free(data);
        return DAX_IO_ERROR;
    }
    size_t count;
    dax_status status = validate(data, size, &count);
    if (status != DAX_OK) {
        free(data);
        return status;
    }
    *archive = (dax_archive){data, size, count};
    return DAX_OK;
}

void dax_close(dax_archive *archive)
{
    free(archive->data);
    *archive = (dax_archive){0};
}

dax_status dax_record_at(const dax_archive *archive, size_t index, dax_record *record)
{
    if (index >= archive->count) return DAX_BAD_RECORD;
    const uint8_t *entry = archive->data + 2 + index * 9;
    uint32_t offset = read_u32(entry + 1);
    *record = (dax_record){
        entry[0], offset, read_u16(entry + 5), read_u16(entry + 7),
        archive->data + 2 + archive->count * 9 + offset
    };
    return DAX_OK;
}

dax_status dax_decode(const uint8_t *packed, size_t packed_size,
                      uint8_t *decoded, size_t decoded_size)
{
    if ((packed == NULL && packed_size != 0) || (decoded == NULL && decoded_size != 0))
        return DAX_BAD_STREAM;
    size_t input = 0;
    size_t output = 0;
    while (input < packed_size) {
        uint8_t control = packed[input++];
        /* The original decoder skips both 0x7f and 0x80. */
        if (control == 127 || control == 128) continue;
        size_t count = control < 127 ? (size_t)control + 1 : 256u - control;
        size_t needed = control < 127 ? count : 1;
        if (needed > packed_size - input) return DAX_BAD_STREAM;
        if (count > decoded_size - output) return DAX_SIZE_MISMATCH;
        if (control < 127) {
            memcpy(decoded + output, packed + input, count);
        } else {
            memset(decoded + output, packed[input], count);
        }
        input += needed;
        output += count;
    }
    return output == decoded_size ? DAX_OK : DAX_SIZE_MISMATCH;
}

const char *dax_status_string(dax_status status)
{
    switch (status) {
    case DAX_OK: return "success";
    case DAX_IO_ERROR: return "file read failed";
    case DAX_NO_MEMORY: return "out of memory";
    case DAX_BAD_DIRECTORY: return "invalid or truncated directory";
    case DAX_BAD_RECORD: return "record outside archive bounds";
    case DAX_BAD_STREAM: return "invalid or truncated compressed stream";
    case DAX_SIZE_MISMATCH: return "decoded size does not match directory";
    }
    return "unknown error";
}
