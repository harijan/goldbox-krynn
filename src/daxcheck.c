#include "dax.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int first = 1;
    int list = 0;
    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        list = 1;
        first = 2;
    }
    if (argc <= first) {
        fprintf(stderr, "Usage: %s [--list] archive.dax [archive.dax ...]\n", argv[0]);
        return EXIT_FAILURE;
    }
    size_t archives = 0;
    size_t records = 0;
    uintmax_t total = 0;
    int failed = 0;
    for (int arg = first; arg < argc; ++arg) {
        dax_archive archive = {0};
        dax_status status = dax_open(argv[arg], &archive);
        if (status != DAX_OK) {
            fprintf(stderr, "%s: %s\n", argv[arg], dax_status_string(status));
            failed = 1;
            continue;
        }
        size_t validated = 0;
        for (size_t i = 0; i < archive.count; ++i) {
            dax_record record;
            status = dax_record_at(&archive, i, &record);
            if (status != DAX_OK) {
                fprintf(stderr, "%s: entry %zu: %s\n", argv[arg], i, dax_status_string(status));
                failed = 1;
                break;
            }
            uint8_t *decoded = malloc(record.decoded_size == 0 ? 1 : record.decoded_size);
            status = decoded == NULL ? DAX_NO_MEMORY :
                     dax_decode(record.packed, record.packed_size, decoded, record.decoded_size);
            free(decoded);
            if (status != DAX_OK) {
                fprintf(stderr, "%s: entry %zu, ID %u: %s\n",
                        argv[arg], i, (unsigned)record.id, dax_status_string(status));
                failed = 1;
                continue;
            }
            if (list) {
                printf("%s: entry=%zu id=%u offset=%" PRIu32 " packed=%u decoded=%u\n",
                       argv[arg], i, (unsigned)record.id, record.offset,
                       (unsigned)record.packed_size, (unsigned)record.decoded_size);
            }
            ++validated;
            ++records;
            total += record.decoded_size;
        }
        if (validated == archive.count) {
            ++archives;
            printf("%s: %zu records verified\n", argv[arg], validated);
        }
        dax_close(&archive);
    }
    printf("Verified %zu archives, %zu records, %" PRIuMAX " decoded bytes%s\n",
           archives, records, total, failed ? " (errors found)" : "");
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
