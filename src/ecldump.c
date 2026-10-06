/* Disassemble ECL scripts: every instruction reachable from a block's five
 * vectors through jumps, calls, ON GOTO tables and both arms of each IF. */
#include "dax.h"
#include "ecl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t instructions, mismatched_skips;
    int failed;
} totals;

static void print_operand(const cok_ecl *vm, const cok_ecl_operand *o, size_t *string,
                          int target)
{
    unsigned address = (unsigned)(o->high << 8 | o->low);
    if (target) {
        printf(" @%04x", address);
        return;
    }
    switch (o->type) {
    case COK_ECL_BYTE: printf(" %u", o->low); break;
    case COK_ECL_WORD: printf(" $%04x", address); break;
    case COK_ECL_VAR: case COK_ECL_VAR_3: printf(" [%04x]", address); break;
    case COK_ECL_STRING: printf(" \"%s\"", vm->string[(*string)++]); break;
    case COK_ECL_STRVAR: printf(" s[%04x]", address); ++*string; break;
    default: printf(" ?%02x:%02x", o->type, o->low); break;
    }
}

static int is_jump(unsigned opcode)
{
    return opcode == COK_ECL_GOTO || opcode == COK_ECL_GOSUB;
}

static void print_instruction(const cok_ecl *vm, uint16_t address)
{
    const char *name = cok_ecl_opcode_name(vm->opcode);
    printf("%04x: ", address);
    if (name != NULL)
        printf("%s", name);
    else
        printf("OPCODE %02x", vm->opcode);
    size_t string = 0;
    if (vm->header_operands > 0) {
        /* The list overwrote the header's string slot; show the copy. */
        for (size_t i = 0; i < vm->header_operands; ++i) {
            if (vm->header[i].type == COK_ECL_STRING) {
                printf(" \"%s\"", vm->header_string);
            } else {
                size_t unused = 0;
                print_operand(vm, &vm->header[i], &unused, 0);
            }
        }
        printf(" :");
    }
    int targets = is_jump(vm->opcode) || vm->opcode == COK_ECL_ON_GOTO ||
                  vm->opcode == COK_ECL_ON_GOSUB;
    for (size_t i = 0; i < vm->operands; ++i)
        print_operand(vm, &vm->operand[i], &string, targets);
    putchar('\n');
}

/* Walk the block in vm, marking reached instructions in seen. */
static void walk(cok_ecl *vm, uint8_t *seen, uint16_t *next, totals *t, const char *where)
{
    uint16_t stack[4096];
    size_t depth = 0;
    for (size_t i = 0; i < 5; ++i) stack[depth++] = vm->vectors[i];
    while (depth > 0) {
        uint16_t address = stack[--depth];
        while (address - COK_ECL_BASE < COK_ECL_CODE_SIZE && !seen[address - COK_ECL_BASE]) {
            cok_ecl_status status = cok_ecl_decode(vm, address);
            if (status != COK_ECL_OK) {
                fprintf(stderr, "%s: %04x: %s\n", where, address, cok_ecl_status_string(status));
                t->failed = 1;
                break;
            }
            seen[address - COK_ECL_BASE] = 1;
            next[address - COK_ECL_BASE] = vm->ip;
            ++t->instructions;
            uint8_t opcode = vm->opcode;
            uint16_t after = vm->ip;
            if (is_jump(opcode) && depth < 4096) stack[depth++] = cok_ecl_address(vm, 0);
            if (opcode == COK_ECL_ON_GOTO || opcode == COK_ECL_ON_GOSUB)
                for (size_t i = 0; i < vm->operands && depth < 4096; ++i)
                    stack[depth++] = cok_ecl_address(vm, i);
            if (opcode >= COK_ECL_IF_EQUAL && opcode <= COK_ECL_IF_GREATER_EQUAL &&
                cok_ecl_decode(vm, after) == COK_ECL_OK) {
                int handler = cok_ecl_operand_count(vm->opcode);
                if (handler != cok_ecl_skip_count(vm->opcode)) {
                    fprintf(stderr, "%s: %04x: IF skips %s with %d operands; its handler loads %s\n",
                            where, address, cok_ecl_opcode_name(vm->opcode),
                            cok_ecl_skip_count(vm->opcode),
                            handler == COK_ECL_LIST ? "a list" : "a different count");
                    ++t->mismatched_skips;
                }
                if (depth < 4096) stack[depth++] = vm->ip;
            }
            if (opcode == COK_ECL_EXIT || opcode == COK_ECL_GOTO || opcode == COK_ECL_RETURN ||
                opcode == COK_ECL_NEWECL)
                break;
            address = after;
        }
    }
}

static void dump_block(const dax_record *record, const uint8_t *data, int list, totals *t,
                       const char *path)
{
    char where[300];
    snprintf(where, sizeof where, "%s: ID %u", path, (unsigned)record->id);
    static cok_ecl vm;
    cok_ecl_init(&vm, NULL);
    cok_ecl_status status = cok_ecl_load(&vm, data, record->decoded_size);
    if (status == COK_ECL_OK) status = cok_ecl_start(&vm, true);
    if (status != COK_ECL_OK) {
        fprintf(stderr, "%s: %s\n", where, cok_ecl_status_string(status));
        t->failed = 1;
        return;
    }
    static uint8_t seen[COK_ECL_CODE_SIZE];
    static uint16_t next[COK_ECL_CODE_SIZE];
    memset(seen, 0, sizeof seen);
    size_t before = t->instructions;
    walk(&vm, seen, next, t, where);
    if (!list) {
        printf("%s: %zu instructions, vectors %04x %04x %04x %04x %04x\n", where,
               t->instructions - before, vm.vectors[0], vm.vectors[1], vm.vectors[2],
               vm.vectors[3], vm.vectors[4]);
        return;
    }
    printf("; %s, %zu bytes, vectors %04x %04x %04x %04x %04x\n", where, vm.size, vm.vectors[0],
           vm.vectors[1], vm.vectors[2], vm.vectors[3], vm.vectors[4]);
    for (size_t i = 0; i < COK_ECL_CODE_SIZE; ++i) {
        if (!seen[i]) continue;
        uint16_t address = (uint16_t)(COK_ECL_BASE + i);
        cok_ecl_decode(&vm, address);
        print_instruction(&vm, address);
    }
}

int main(int argc, char **argv)
{
    int first = 1, list = 1;
    long only = -1;
    while (first < argc && strncmp(argv[first], "--", 2) == 0) {
        if (strcmp(argv[first], "--summary") == 0) {
            list = 0;
            ++first;
        } else if (strcmp(argv[first], "--block") == 0 && first + 1 < argc) {
            only = strtol(argv[first + 1], NULL, 0);
            first += 2;
        } else {
            break;
        }
    }
    if (argc <= first) {
        fprintf(stderr, "Usage: %s [--summary] [--block ID] ECLn.DAX [...]\n", argv[0]);
        return EXIT_FAILURE;
    }
    totals t = {0};
    for (int arg = first; arg < argc; ++arg) {
        dax_archive archive = {0};
        dax_status status = dax_open(argv[arg], &archive);
        if (status != DAX_OK) {
            fprintf(stderr, "%s: %s\n", argv[arg], dax_status_string(status));
            t.failed = 1;
            continue;
        }
        for (size_t i = 0; i < archive.count; ++i) {
            dax_record record;
            uint8_t *data = NULL;
            status = dax_record_at(&archive, i, &record);
            if (status == DAX_OK && (only < 0 || record.id == only)) {
                data = malloc(record.decoded_size == 0 ? 1 : record.decoded_size);
                status = data == NULL ? DAX_NO_MEMORY
                                      : dax_decode(record.packed, record.packed_size, data,
                                                   record.decoded_size);
                if (status == DAX_OK) dump_block(&record, data, list, &t, argv[arg]);
            }
            if (status != DAX_OK) {
                fprintf(stderr, "%s: entry %zu: %s\n", argv[arg], i, dax_status_string(status));
                t.failed = 1;
            }
            free(data);
        }
        dax_close(&archive);
    }
    if (!list)
        printf("Decoded %zu instructions; %zu IFs skip an instruction by a different length\n",
               t.instructions, t.mismatched_skips);
    return t.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
