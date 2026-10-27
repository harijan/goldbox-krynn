#ifndef COK_HLIB_H
#define COK_HLIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The Dark Queen of Krynn's libraries, "HLIB" (.TLB pictures, .GLB data;
 * dqk 293f_00d5 LBLoad, 19a4:01dc opens <name>.tlb, 5e98:0130 <name>.glb),
 * in place of the earlier games' DAX archives (dax.h): "HLIB", u32 size,
 * u16 count, u16 flags, 4 bytes type (TILE, DATA, DIG4, HLIB), then count +
 * 1 u32 offsets from the library's start; the records lie between them,
 * stored as they are (no compression). Flag 0x100: record 0 maps ids (u16
 * n, then n of u16 id, u16 index). A record that starts "HLIB" is a
 * library nested in it. tools/hlib.py is the same reader in Python. */

typedef enum {
    HLIB_OK,
    HLIB_IO_ERROR,
    HLIB_NO_MEMORY,
    HLIB_BAD_HEADER,  /* not "HLIB", or the offsets past the end */
    HLIB_BAD_RECORD,  /* offsets out of order, or a bad id map */
    HLIB_NO_RECORD    /* no such id or index */
} hlib_status;

enum { HLIB_ID_MAP = 0x100 };

/* A library over borrowed bytes (a file's, or a record of another's):
 * valid while they are. Treat fields as read-only. */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t count;      /* Records, the id map's too. */
    uint16_t flags;
    char type[5];      /* "TILE", "DATA", "DIG4", "HLIB", NUL-ended. */
    size_t ids;        /* The id map's entries; 0 without one. */
} hlib_library;

/* Check data as a library: its header, its offsets in order and inside it,
 * and the id map's indexes. */
hlib_status hlib_parse(const uint8_t *data, size_t size, hlib_library *library);
/* Read a whole file into *data (malloc'd, the caller frees it). */
hlib_status hlib_read_file(const char *path, uint8_t **data, size_t *size);

/* Record index (0-based, the id map's 0 too), borrowed from the library. */
hlib_status hlib_record_at(const hlib_library *library, size_t index, const uint8_t **record,
                           size_t *size);
/* A record by its id, through the id map; without one, by its index (as
 * LBLoad takes both). */
hlib_status hlib_record(const hlib_library *library, unsigned id, const uint8_t **record,
                        size_t *size);
/* The id map's entry i: its id and index. */
hlib_status hlib_id_at(const hlib_library *library, size_t i, unsigned *id, size_t *index);
/* Whether a record is a nested library. */
bool hlib_is_library(const uint8_t *record, size_t size);

const char *hlib_status_string(hlib_status status);

#endif
