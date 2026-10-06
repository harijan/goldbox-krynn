#ifndef COK_DAX_H
#define COK_DAX_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DAX_OK,
    DAX_IO_ERROR,
    DAX_NO_MEMORY,
    DAX_BAD_DIRECTORY,
    DAX_BAD_RECORD,
    DAX_BAD_STREAM,
    DAX_SIZE_MISMATCH
} dax_status;

/* Owns its bytes. Initialize to {0}; close before opening another archive.
 * Treat fields as read-only. Directory order and duplicate IDs are preserved. */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t count;
} dax_archive;

typedef struct {
    uint8_t id;
    uint32_t offset; /* Relative to the end of the directory. */
    uint16_t decoded_size;
    uint16_t packed_size;
    const uint8_t *packed; /* Borrowed until archive is closed. */
} dax_record;

/* On failure, archive remains empty. parse copies the supplied bytes. */
dax_status dax_parse(const uint8_t *data, size_t size, dax_archive *archive);
dax_status dax_open(const char *path, dax_archive *archive);
void dax_close(dax_archive *archive);
dax_status dax_record_at(const dax_archive *archive, size_t index, dax_record *record);

/* Decode exactly decoded_size bytes and consume the complete packed stream.
 * Buffers must not overlap. NULL is allowed only for a zero-length buffer. */
dax_status dax_decode(const uint8_t *packed, size_t packed_size,
                      uint8_t *decoded, size_t decoded_size);
const char *dax_status_string(dax_status status);

#endif
