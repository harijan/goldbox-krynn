#include "ecl.h"
#include "dax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static cok_ecl vm;

/* Script assembly: code is written at vm.code from address 0x8000. */
static size_t at;

static void byte(unsigned b)
{
    vm.code[at++] = (uint8_t)b;
}

static void op(cok_ecl_opcode opcode)
{
    byte(opcode);
}

static void imm(unsigned value)
{
    byte(COK_ECL_BYTE);
    byte(value);
}

static void word(unsigned value)
{
    byte(COK_ECL_WORD);
    byte(value & 0xff);
    byte(value >> 8);
}

static void var(unsigned address)
{
    byte(COK_ECL_VAR);
    byte(address & 0xff);
    byte(address >> 8);
}

static void strvar(unsigned address)
{
    byte(COK_ECL_STRVAR);
    byte(address & 0xff);
    byte(address >> 8);
}

/* Pack text six bits a character, four characters to three bytes. */
static void text(const char *s)
{
    size_t n = strlen(s), bytes = n - n / 4;
    uint8_t codes[256] = {0};
    for (size_t i = 0; i < n; ++i) codes[i] = (uint8_t)((unsigned char)s[i] & 0x3f);
    byte(COK_ECL_STRING);
    byte((unsigned)bytes);
    for (size_t i = 0, written = 0; written < bytes; i += 4) {
        uint8_t packed[3] = {
            (uint8_t)(codes[i] << 2 | codes[i + 1] >> 4),
            (uint8_t)((codes[i + 1] & 15) << 4 | codes[i + 2] >> 2),
            (uint8_t)((codes[i + 2] & 3) << 6 | codes[i + 3]),
        };
        for (size_t j = 0; j < 3 && written < bytes; ++j, ++written) byte(packed[j]);
    }
}

static unsigned here(void)
{
    return (unsigned)(COK_ECL_BASE + at);
}

typedef struct {
    int opcodes[COK_ECL_OPCODES];
    char printed[256];
    int stores;
    uint16_t last_address, last_value;
    bool stopped_at_exit;
} log_t;

static void on_opcode(cok_ecl *v, void *context)
{
    log_t *log = context;
    ++log->opcodes[v->opcode];
    if (v->opcode == COK_ECL_PRINT) strcpy(log->printed, v->string[0]);
    if (v->opcode == COK_ECL_EXIT || v->opcode == COK_ECL_RETURN) log->stopped_at_exit = v->stop;
}

static void on_store(cok_ecl *v, uint16_t address, uint16_t value, void *context)
{
    (void)v;
    log_t *log = context;
    ++log->stores;
    log->last_address = address;
    log->last_value = value;
}

static log_t start(void)
{
    cok_ecl_init(&vm, &cok_rules_champions, NULL);
    at = 0;
    return (log_t){0};
}

static cok_ecl_status run(log_t *log)
{
    vm.hooks = (cok_ecl_hooks){.opcode = on_opcode, .stored = on_store, .context = log};
    return cok_ecl_run(&vm, COK_ECL_BASE);
}

static const cok_rules *const C = &cok_rules_champions, *const DK = &cok_rules_death_knights;

static void test_names_and_counts(void)
{
    CHECK(strcmp(cok_ecl_opcode_name(C, COK_ECL_EXIT), "EXIT") == 0);
    CHECK(strcmp(cok_ecl_opcode_name(C, COK_ECL_NEWECL), "NEWECL") == 0);
    CHECK(strcmp(cok_ecl_opcode_name(C, COK_ECL_ADD_EP), "ADD EP") == 0);
    CHECK(cok_ecl_opcode_name(C, 0x1f) == NULL && cok_ecl_opcode_name(C, 0x42) == NULL);
    CHECK(cok_ecl_operand_count(C, COK_ECL_ENCOUNTER_MENU) == 14);
    CHECK(cok_ecl_operand_count(C, COK_ECL_ON_GOTO) == COK_ECL_LIST);
    CHECK(cok_ecl_operand_count(C, COK_ECL_PROTECTION) == COK_ECL_HANG);
    CHECK(cok_ecl_operand_count(C, 0x99) == COK_ECL_HANG);
    CHECK(cok_ecl_skip_count(C, COK_ECL_ECL_CLOCK) == 1 &&
          cok_ecl_operand_count(C, COK_ECL_ECL_CLOCK) == 2);
    CHECK(cok_ecl_skip_count(C, COK_ECL_VERTICAL_MENU) == 0);
    /* Champions' table, as the VM held it before the descriptor did: its
     * handlers' operands (2fd3:0050-36dc; -1 a list, -2 never advances)
     * and the failed IF's skips (3775:1e74), opcode by opcode. */
    static const signed char operands[COK_ECL_OPCODES] = {
        0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2, 2, 1, 1, 0, 4, -1,
        0, 0, 0, 0, 0, 0, 0, 1, 6, -2, 1, 3, 2, 4, 0, -1, -1, 8, 3, 14, 3, -1,
        6, 1, 5, 3, 3, 0, 1, 0, 2, 3, 2, 3, 1, 1, 0, 3, -2, 0, 0, 1, 1, 2};
    static const uint8_t skips[COK_ECL_OPCODES] = {
        0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2, 2, 1, 1, 0, 4, 0,
        0, 0, 0, 0, 0, 0, 0, 1, 6, 2, 1, 3, 2, 4, 0, 0, 0, 8, 3, 14, 3, 0,
        6, 1, 5, 3, 3, 0, 1, 0, 1, 3, 1, 3, 1, 1, 0, 3, 1, 0, 0, 1, 1, 2};
    CHECK(C->ecl.count == COK_ECL_OPCODES && C->ecl.variable_bytes == 2);
    for (unsigned o = 0; o < COK_ECL_OPCODES; ++o) {
        CHECK(cok_ecl_meaning_of(C, o) == o);
        CHECK(cok_ecl_operand_count(C, o) == operands[o]);
        CHECK(cok_ecl_skip_count(C, o) == skips[o]);
    }
    CHECK(C->ecl.opcodes[COK_ECL_VERTICAL_MENU].header == 3);
    CHECK(C->ecl.opcodes[COK_ECL_HORIZONTAL_MENU].header == 2);
    CHECK(C->ecl.opcodes[COK_ECL_ON_GOTO].header == 2 && C->ecl.opcodes[COK_ECL_ON_GOSUB].header == 2);
    CHECK(cok_ecl_meaning_of(C, 0x42) == COK_ECL_MEANINGS && cok_ecl_skip_count(C, 0x42) == 0);
    /* CALL's routines by address (2fd3:329b). */
    CHECK(cok_ecl_call_of(C, 0x2e10) == COK_ECL_CALL_REDRAW);
    CHECK(cok_ecl_call_of(C, 0xb203) == COK_ECL_CALL_SOUND);
    CHECK(cok_ecl_call_of(C, 0xc01e) == COK_ECL_CALL_STEP);
    CHECK(cok_ecl_call_of(C, 0xc018) == COK_ECL_CALL_AHEAD);
    CHECK(cok_ecl_call_of(C, 0x6803) == COK_ECL_CALL_ANIMATE);
    CHECK(cok_ecl_call_of(C, 0x2dcb) == COK_ECL_CALL_NONE && cok_ecl_call_of(C, 0) == COK_ECL_CALL_NONE);
}

/* Death Knights' table (dkk 3632's handlers, 3c1e:1b48's skips): Champions'
 * but for ten opcodes. */
static void test_death_knights_table(void)
{
    CHECK(DK->ecl.count == COK_ECL_OPCODES && DK->ecl.variable_bytes == 1);
    static const struct {
        uint8_t opcode;
        unsigned meaning;
        int operands, header, skip;
        const char *name;
    } changed[] = {
        {0x0c, COK_ECL_SETUP_MONSTERS, 4, 0, 3, "SETUP MONSTERS"},
        {0x1d, COK_ECL_PRESS_KEY, 0, 0, 0, "PRESS KEY"},
        {0x1f, COK_ECL_JOURNAL, 1, 0, 1, "JOURNAL"},
        {0x22, COK_ECL_YES_NO, 0, 0, 0, "YES NO"},
        {0x23, COK_ECL_CLEAR_EXIT, 0, 0, 0, "CLEAR EXIT"},
        {0x27, COK_ECL_TREASURE_ITEMS, COK_ECL_LIST, 4, 0, "TREASURE ITEMS"},
        {0x29, COK_ECL_NOTHING, 0, 0, 0, "NOTHING"},
        {0x2c, COK_ECL_MUSIC, 1, 0, 1, "MUSIC"},
        {0x31, COK_ECL_ASK_MENU, COK_ECL_LIST, 2, 0, "ASK MENU"},
        {0x3c, COK_ECL_STOCK, 1, 0, 1, "STOCK"},
    };
    size_t n = sizeof changed / sizeof *changed;
    for (unsigned o = 0; o < COK_ECL_OPCODES; ++o) {
        size_t k = 0;
        while (k < n && changed[k].opcode != o) ++k;
        const cok_rules_opcode *d = &DK->ecl.opcodes[o];
        if (k == n) {
            const cok_rules_opcode *c = &C->ecl.opcodes[o];
            CHECK(d->meaning == c->meaning && d->operands == c->operands &&
                  d->header == c->header && d->skip == c->skip);
            continue;
        }
        CHECK(d->meaning == changed[k].meaning && d->operands == changed[k].operands);
        CHECK(d->header == changed[k].header && d->skip == changed[k].skip);
        CHECK(strcmp(cok_ecl_opcode_name(DK, o), changed[k].name) == 0);
    }
    CHECK(cok_ecl_call_of(DK, 0x2dcb) == COK_ECL_CALL_REDRAW);
    CHECK(cok_ecl_call_of(DK, 0x2e10) == COK_ECL_CALL_NONE);
    CHECK(cok_ecl_call_of(DK, 0x6803) == COK_ECL_CALL_ANIMATE);

    /* A list of a header of 4 (0x27's), and an IF's skips by the table. */
    cok_ecl_init(&vm, DK, NULL);
    at = 0;
    byte(0x27); imm(1); imm(2); imm(3); imm(2); imm(7); imm(8);
    CHECK(cok_ecl_decode(&vm, COK_ECL_BASE) == COK_ECL_OK);
    CHECK(vm.header_operands == 4 && vm.operands == 2 && vm.ip == COK_ECL_BASE + 1 + 6 * 2);
    CHECK(vm.header[3].low == 2 && vm.operand[0].low == 7 && vm.operand[1].low == 8);
    log_t log = {0};
    at = 0;
    op(COK_ECL_COMPARE); imm(1); imm(2);
    op(COK_ECL_IF_EQUAL);
    byte(0x1d);                                             /* PRESS KEY: skips none */
    op(COK_ECL_SAVE); imm(5); var(0x4b01);
    op(COK_ECL_IF_EQUAL);
    byte(0x2c); imm(9);                                     /* MUSIC: skips one */
    op(COK_ECL_SAVE); imm(6); var(0x4b02);
    op(COK_ECL_EXIT);
    CHECK(run(&log) == COK_ECL_OK);
    CHECK(vm.mem4b00[1] == 5 && vm.mem4b00[2] == 6 && log.opcodes[0x1d] == 0 && log.opcodes[0x2c] == 0);
}

/* Byte variables (dkk 3c1e:0d85, 0bc1): an operand of type 0 or 1 reads and
 * writes a byte, 2 and up a word over two addresses; where each address
 * holds a word (Champions) a variable is that word, whatever the type. */
static void var3(unsigned address)
{
    byte(COK_ECL_VAR_3);
    byte(address & 0xff);
    byte(address >> 8);
}

static void test_byte_variables(void)
{
    const cok_rules *games[] = {C, DK};
    for (size_t g = 0; g < 2; ++g) {
        log_t log = {0};
        cok_ecl_init(&vm, games[g], NULL);
        at = 0;
        op(COK_ECL_SAVE); word(0x1234); var(0x4b10);        /* a byte */
        op(COK_ECL_SAVE); word(0x5678); var3(0x4b20);       /* a word over two */
        op(COK_ECL_ADD); var3(0x4b20); imm(1); var3(0x7a00);
        op(COK_ECL_ADD); var(0x4b20); imm(1); var(0x7a10);
        op(COK_ECL_SAVE); word(0xabcd); var3(0x9000);       /* the code too */
        op(COK_ECL_EXIT);
        CHECK(run(&log) == COK_ECL_OK);
        if (g == 0) {
            CHECK(vm.mem4b00[0x10] == 0x1234 && vm.mem4b00[0x20] == 0x5678 && vm.mem4b00[0x21] == 0);
            CHECK(vm.mem7a00[0] == 0x5679 && vm.mem7a00[0x10] == 0x5679);
            CHECK(vm.code[0x1000] == 0xcd && vm.code[0x1001] == 0);
        } else {
            CHECK(vm.mem4b00[0x10] == 0x34 && vm.mem4b00[0x11] == 0);
            CHECK(vm.mem4b00[0x20] == 0x78 && vm.mem4b00[0x21] == 0x56);
            CHECK(vm.mem7a00[0] == 0x79 && vm.mem7a00[1] == 0x56);
            CHECK(vm.mem7a00[0x10] == 0x79 && vm.mem7a00[0x11] == 0);
            CHECK(vm.code[0x1000] == 0xcd && vm.code[0x1001] == 0xab);
        }
        /* The stores' hook sees the value stored. */
        CHECK(log.last_address == 0x9000);
        CHECK(log.last_value == 0xabcd);
    }
    /* An immediate word (type 2) as the address stored to is wide too. */
    cok_ecl_init(&vm, DK, NULL);
    cok_ecl_store_as(&vm, &(cok_ecl_operand){COK_ECL_WORD, 0x30, 0x4b}, 0x0201);
    CHECK(vm.mem4b00[0x30] == 1 && vm.mem4b00[0x31] == 2);
    cok_ecl_store_as(&vm, &(cok_ecl_operand){COK_ECL_VAR, 0x40, 0x4b}, 0x0201);
    CHECK(vm.mem4b00[0x40] == 1 && vm.mem4b00[0x41] == 0);
    /* A special address takes the value whatever the width. */
    cok_ecl_init(&vm, DK, NULL);
    cok_ecl_store_as(&vm, &(cok_ecl_operand){COK_ECL_VAR_3, 0x4b, 0xc0}, 0x0105);
    CHECK(vm.map_x == 5 && cok_ecl_load_var(&vm, 0xc04b) == 5);
    /* A word at a store's last address: no byte past it (0 here). */
    at = 0;
    op(COK_ECL_SAVE); var3(0x4eff); var(0x7a00);
    CHECK(cok_ecl_decode(&vm, COK_ECL_BASE) == COK_ECL_OK);
    vm.mem4b00[0x3ff] = 0x44;
    CHECK(cok_ecl_value(&vm, 0) == 0x44);
}

static void test_operands_and_text(void)
{
    log_t log = start();
    vm.mem4b00[0x10] = 0x1234;
    vm.mem7a00[0] = 'H';
    vm.mem7a00[1] = 'I';
    op(COK_ECL_COMPARE_AND);
    imm(7);
    word(0xbeef);
    var(0x4b10);
    text("A, B");
    op(COK_ECL_PRINT);
    strvar(0x7a00);
    CHECK(cok_ecl_decode(&vm, COK_ECL_BASE) == COK_ECL_OK);
    CHECK(vm.operands == 4 && vm.strings == 1);
    CHECK(cok_ecl_value(&vm, 0) == 7 && cok_ecl_value(&vm, 1) == 0xbeef);
    CHECK(cok_ecl_value(&vm, 2) == 0x1234 && cok_ecl_address(&vm, 2) == 0x4b10);
    CHECK(strcmp(vm.string[0], "A, B") == 0);
    CHECK(vm.ip == COK_ECL_BASE + 1 + 2 + 3 + 3 + 2 + 3);
    CHECK(cok_ecl_decode(&vm, vm.ip) == COK_ECL_OK && strcmp(vm.string[0], "HI") == 0);

    /* Every text length packs and unpacks; codes below 0x20 are letters. */
    const char *samples[] = {"", "Z", "AB", "ABC", "ABCD", "HELLO, WORLD!", "0123456789?"};
    for (size_t i = 0; i < sizeof samples / sizeof *samples; ++i) {
        at = 0;
        op(COK_ECL_PRINT);
        text(samples[i]);
        CHECK(cok_ecl_decode(&vm, COK_ECL_BASE) == COK_ECL_OK);
        CHECK(strcmp(vm.string[0], samples[i]) == 0 && vm.ip == here());
    }
    (void)log;
}

static void test_arithmetic_and_flags(void)
{
    log_t log = start();
    vm.mem4b00[1] = 17;
    op(COK_ECL_ADD); var(0x4b01); imm(5); var(0x4b02);       /* 17 + 5 */
    op(COK_ECL_SUBTRACT); imm(5); var(0x4b01); var(0x4b03);  /* 17 - 5: second minus first */
    op(COK_ECL_DIVIDE); var(0x4b01); imm(5); var(0x4b04);    /* 17 / 5, remainder to 0x7f3f */
    op(COK_ECL_MULTIPLY); word(300); word(300); var(0x4b05); /* wraps to 16 bits */
    op(COK_ECL_AND); imm(0x0c); imm(0x03); var(0x4b06);
    op(COK_ECL_EXIT);
    vm.mem4b00[7] = 0xffff;
    CHECK(run(&log) == COK_ECL_OK);
    CHECK(vm.mem4b00[2] == 22 && vm.mem4b00[3] == 12 && vm.mem4b00[4] == 3);
    CHECK(vm.mem7c00[0x33f] == 2 && vm.mem4b00[5] == (uint16_t)90000);
    CHECK(vm.mem4b00[6] == 0 && vm.flags[0] && !vm.flags[1]);
    CHECK(log.opcodes[COK_ECL_EXIT] == 1 && log.stopped_at_exit && !vm.stop);
    CHECK(log.opcodes[COK_ECL_ADD] == 0 && log.stores == 5);

    /* OR's flags order 0 against its result; MULTIPLY wraps without overflow. */
    at = 0;
    op(COK_ECL_OR); imm(0x0c); imm(0x03); var(0x4b06);
    op(COK_ECL_MULTIPLY); var(0x4b07); var(0x4b07); var(0x4b08);
    op(COK_ECL_EXIT);
    CHECK(run(&log) == COK_ECL_OK && vm.mem4b00[6] == 0x0f && vm.mem4b00[8] == 1);
    const bool below[6] = {false, true, true, false, true, false};
    CHECK(memcmp(vm.flags, below, sizeof below) == 0);

    /* COMPARE orders the first operand against the second. */
    at = 0;
    op(COK_ECL_COMPARE); imm(3); imm(9);
    op(COK_ECL_EXIT);
    CHECK(run(&log) == COK_ECL_OK);
    const bool less[6] = {false, true, true, false, true, false};
    CHECK(memcmp(vm.flags, less, sizeof less) == 0);
    at = 0;
    op(COK_ECL_COMPARE); text("ABD"); text("ABC");
    op(COK_ECL_EXIT);
    CHECK(run(&log) == COK_ECL_OK && vm.flags[3] && vm.flags[5] && !vm.flags[4]);
    at = 0;
    op(COK_ECL_COMPARE); text("AB"); text("ABC");
    op(COK_ECL_EXIT);
    CHECK(run(&log) == COK_ECL_OK && vm.flags[2] && !vm.flags[0]);

    /* Division by zero stops with an error. */
    at = 0;
    op(COK_ECL_DIVIDE); imm(1); imm(0); var(0x4b04);
    CHECK(run(&log) == COK_ECL_DIVIDE_BY_ZERO);
}

static void test_if_skips(void)
{
    log_t log = start();
    op(COK_ECL_COMPARE); imm(1); imm(2);
    op(COK_ECL_IF_EQUAL);                                   /* false: skip the SAVE */
    op(COK_ECL_SAVE); imm(1); var(0x4b01);
    op(COK_ECL_IF_LESS);                                    /* true: run the SAVE */
    op(COK_ECL_SAVE); imm(2); var(0x4b02);
    op(COK_ECL_IF_GREATER);                                 /* false: skip a string */
    op(COK_ECL_PRINT); text("SKIPPED TEXT");
    /* The skip table gives ECL CLOCK one operand, so the run resumes on its
     * second operand's type byte, 0, which is EXIT. */
    op(COK_ECL_IF_GREATER);
    op(COK_ECL_ECL_CLOCK); imm(1); imm(2);
    op(COK_ECL_SAVE); imm(3); var(0x4b03);
    CHECK(run(&log) == COK_ECL_OK && log.opcodes[COK_ECL_EXIT] == 1);
    CHECK(vm.mem4b00[1] == 0 && vm.mem4b00[2] == 2 && vm.mem4b00[3] == 0);
    CHECK(log.opcodes[COK_ECL_PRINT] == 0 && log.opcodes[COK_ECL_ECL_CLOCK] == 0);
}

static void test_calls_and_tables(void)
{
    log_t log = start();
    unsigned sub = COK_ECL_BASE + 0x100;
    op(COK_ECL_GOSUB); word(sub);
    op(COK_ECL_ON_GOSUB); imm(1); imm(3); word(sub + 0x10); word(sub); word(sub + 0x10);
    op(COK_ECL_ON_GOTO); imm(5); imm(2); word(sub + 0x10); word(sub + 0x10);  /* no match */
    op(COK_ECL_GETTABLE); word(0x7a00); imm(2); var(0x4b03);
    op(COK_ECL_SAVE_TABLE); imm(9); word(0x7a00); imm(4);
    op(COK_ECL_RETURN);                                     /* no GOSUB pending: exits */
    at = 0x100;
    op(COK_ECL_ADD); var(0x4b01); imm(1); var(0x4b01);
    op(COK_ECL_RETURN);
    at = 0x110;
    op(COK_ECL_SAVE); imm(99); var(0x4b02);
    op(COK_ECL_EXIT);
    vm.mem7a00[2] = 42;
    CHECK(run(&log) == COK_ECL_OK);
    CHECK(vm.mem4b00[1] == 2 && vm.mem4b00[2] == 0 && vm.mem4b00[3] == 42);
    CHECK(vm.mem7a00[4] == 9 && vm.depth == 0 && log.stopped_at_exit);
    CHECK(log.opcodes[COK_ECL_RETURN] == 1 && vm.cursor.x == 1 && vm.cursor.y == 0x11);

    /* ON GOTO keeps its header; the list replaces the operands. */
    at = 0x200;
    op(COK_ECL_ON_GOTO); var(0x4b01); imm(3); word(sub + 0x10); word(sub + 0x10); word(sub + 0x10);
    CHECK(cok_ecl_decode(&vm, COK_ECL_BASE + 0x200) == COK_ECL_OK);
    CHECK(vm.header_operands == 2 && cok_ecl_address(&vm, 0) == sub + 0x10 && vm.operands == 3);
    CHECK(vm.header[0].type == COK_ECL_VAR && vm.header[1].low == 3);

    /* RETURN pops in order; deep GOSUB recursion is an error. */
    at = 0;
    op(COK_ECL_GOSUB); word(COK_ECL_BASE);
    CHECK(run(&log) == COK_ECL_DEEP_CALLS && vm.depth == COK_ECL_MAX_CALLS);
}

static void test_menus_and_hooks(void)
{
    log_t log = start();
    op(COK_ECL_VERTICAL_MENU); var(0x4b01); text("PICK ONE"); imm(2); text("UP"); text("DOWN");
    op(COK_ECL_PRINT); text("HELLO");
    op(COK_ECL_PRINT_RETURN);
    op(COK_ECL_EXIT);
    vm.cursor = (cok_text_cursor){5, 3};
    CHECK(run(&log) == COK_ECL_OK);
    CHECK(log.opcodes[COK_ECL_VERTICAL_MENU] == 1 && strcmp(log.printed, "HELLO") == 0);
    CHECK(log.opcodes[COK_ECL_PRINT_RETURN] == 1);
    CHECK(cok_ecl_decode(&vm, COK_ECL_BASE) == COK_ECL_OK);
    CHECK(vm.header_operands == 3 && strcmp(vm.header_string, "PICK ONE") == 0);
    CHECK(vm.operands == 2 && strcmp(vm.string[0], "UP") == 0 && strcmp(vm.string[1], "DOWN") == 0);

    /* PRINT RETURN moves to column 1 of the next line; EXIT moves to row 17. */
    CHECK(vm.cursor.x == 1 && vm.cursor.y == 0x11);
    at = 0;
    op(COK_ECL_PRINT_RETURN);
    op(COK_ECL_UNUSED_1F);
    vm.cursor = (cok_text_cursor){5, 3};
    CHECK(run(&log) == COK_ECL_BAD_OPCODE && vm.cursor.x == 1 && vm.cursor.y == 4);
}

/* The host's character values: the selected character is third. */
static uint16_t character_value(cok_ecl *vm_, uint16_t address, void *context)
{
    (void)vm_;
    (void)context;
    return address == 0x7eb1 || address == 0x7eb4 ? 2 : 0;
}

static void test_variables(void)
{
    log_t log = start();
    uint8_t record[COK_CHARACTER_SIZE] = {5, 'S', 'T', 'U', 'R', 'M'};
    record[0x1b] = 16;
    record[0x5a] = 0xfe;
    record[0xeb] = 0x34;
    record[0xec] = 0x12;
    record[0x188] = 3;
    vm.character = record;
    vm.hooks.character_value = character_value;
    vm.file = 1;

    /* Each store and the code itself. */
    cok_ecl_store(&vm, 0x4b00, 1);
    cok_ecl_store(&vm, 0x4eff, 2);
    cok_ecl_store(&vm, 0x7a00, 3);
    cok_ecl_store(&vm, 0x7fff, 4);
    cok_ecl_store(&vm, 0x9dff, 0x1ff);
    CHECK(vm.mem4b00[0] == 1 && vm.mem4b00[0x3ff] == 2 && vm.mem7a00[0] == 3);
    CHECK(vm.mem7c00[0x3ff] == 4 && vm.code[COK_ECL_CODE_SIZE - 1] == 0xff);
    CHECK(cok_ecl_load_var(&vm, 0x9dff) == 0xff && cok_ecl_load_var(&vm, 0x4eff) == 2);

    /* Map position and direction. */
    cok_ecl_store(&vm, 0xc04b, 0xfffe);
    cok_ecl_store(&vm, 0xc04d, 7);
    CHECK(vm.map_x == -2 && cok_ecl_load_var(&vm, 0xc04b) == 0xfffe && vm.view_changed);
    CHECK(vm.direction == 6 && cok_ecl_load_var(&vm, 0xc04d) == 6);
    CHECK(cok_ecl_load_var(&vm, 0x4bea) == 6 && cok_ecl_load_var(&vm, 0x33d) == 6);
    vm.mem4b00[0xe6] = 1;
    CHECK(cok_ecl_load_var(&vm, 0xc04d) == 3);
    cok_ecl_store(&vm, 0x4be6, 0);
    CHECK(vm.mode == 3);

    /* Character fields through 0x7c00-0x7fff. */
    CHECK(cok_ecl_load_var(&vm, 0x7c19) == 16 && cok_ecl_load_var(&vm, 0x7c72) == 0xfffe);
    CHECK(cok_ecl_load_var(&vm, 0x7cbb) == 0x1234 && cok_ecl_load_var(&vm, 0x7d00) == 6);
    CHECK(cok_ecl_load_var(&vm, 0x7eb1) == 2 && cok_ecl_load_var(&vm, 0x7ecf) == 50);
    CHECK(cok_ecl_load_var(&vm, 0x7f12) == 1);
    cok_ecl_store(&vm, 0x7c20, 77);
    cok_ecl_store(&vm, 0x7cb8, 0xc8);
    cok_ecl_store(&vm, 0x7f12, 4);
    CHECK(record[0x1e] == 77 && record[0xe7] == 0x96 && vm.mem7c00[0x20] == 77);
    CHECK(vm.file == 2 && vm.previous_file == 1);
    vm.missing_character = true;
    CHECK(cok_ecl_load_var(&vm, 0x7d00) == 0);

    /* Strings: one character per variable; 0x7c00 is the name. */
    char out[256];
    cok_ecl_load_string(&vm, 0x7c00, out);
    CHECK(strcmp(out, "STURM") == 0);
    cok_ecl_store_string(&vm, 0x7c00, "A VERY LONG NAME INDEED");
    CHECK(record[0] == 15 && memcmp(record + 1, "A VERY LONG NAM", 15) == 0);
    cok_ecl_store_string(&vm, 0x7a10, "KEY");
    CHECK(vm.mem7a00[0x10] == 'K' && vm.mem7a00[0x13] == 0);
    cok_ecl_load_string(&vm, 0x7a10, out);
    CHECK(strcmp(out, "KEY") == 0);
    cok_ecl_store_string(&vm, 0x7bfe, "OVERFLOW");
    CHECK(vm.mem7a00[0x1ff] == 'V' && vm.mem7c00[0] == 0);

    /* SAVE stores text when its first operand is a string. */
    at = 0;
    op(COK_ECL_SAVE); text("GATE"); word(0x4c00);
    op(COK_ECL_EXIT);
    CHECK(run(&log) == COK_ECL_OK);
    cok_ecl_load_string(&vm, 0x4c00, out);
    CHECK(strcmp(out, "GATE") == 0 && log.stores == 0); /* text bypasses the store hook */
}

static void test_random(void)
{
    uint32_t seed = 0;
    CHECK(cok_tp_random(&seed, 100) == 0 && seed == 1);
    CHECK(cok_tp_random(&seed, 100) == 0x0808 % 100 && seed == 0x08088406);
    CHECK(cok_tp_random(&seed, 0) == 0);

    log_t log = start();
    op(COK_ECL_RANDOM); imm(0); var(0x4b01);   /* Random(1) */
    op(COK_ECL_RANDOM); imm(255); var(0x4b02); /* stays Random(255) */
    op(COK_ECL_EXIT);
    vm.seed = 0x08088406;
    uint32_t expect = vm.seed;
    cok_tp_random(&expect, 1);
    uint16_t second = cok_tp_random(&expect, 255);
    CHECK(run(&log) == COK_ECL_OK && vm.mem4b00[1] == 0 && vm.mem4b00[2] == second);

    /* The dice (60f4:1216): Random(sides) + 1 per die, summed as a byte;
     * no dice draw nothing, and 0 sides still draw. */
    seed = 7;
    CHECK(cok_dice(&seed, 0, 6) == 0 && seed == 7);
    expect = 7;
    unsigned sum = cok_tp_random(&expect, 6) + 1u;
    sum += cok_tp_random(&expect, 6) + 1u;
    CHECK(cok_dice(&seed, 2, 6) == sum && seed == expect);
    CHECK(cok_dice(&seed, 3, 0) == 3 && seed != expect);
    /* The sum wraps as a byte. */
    seed = expect = 9;
    unsigned total = 0;
    for (int i = 0; i < 200; ++i) total += cok_tp_random(&expect, 6) + 1u;
    CHECK(total > 255 && cok_dice(&seed, 200, 6) == (uint8_t)total && seed == expect);
    /* 60f4:1261 keeps the count in DS:6b34. */
    uint8_t dice = 0;
    seed = expect = 5;
    CHECK(cok_dice_count(&seed, 4, 8, &dice) == cok_dice(&expect, 4, 8) && dice == 4);
}

typedef struct {
    const uint8_t *block;
    size_t size;
    int loads;
} loader;

static bool load(cok_ecl *v, uint8_t block, void *context)
{
    loader *l = context;
    ++l->loads;
    return block == 7 && cok_ecl_load(v, l->block, l->size) == COK_ECL_OK;
}

static void test_newecl(void)
{
    /* A block: two prefix bytes, five vector headers, then code. */
    uint8_t block[64] = {0};
    size_t n = 2;
    for (unsigned i = 0; i < 5; ++i) {
        block[n++] = COK_ECL_GOTO;
        block[n++] = COK_ECL_WORD;
        block[n++] = (uint8_t)(0x14 + i);
        block[n++] = 0x80;
    }
    block[n++] = COK_ECL_EXIT;

    start();
    loader l = {block, n, 0};
    vm.block = 3;
    vm.mem4b00[0x100] = 5;
    vm.mem7c00[0x382] = 6;
    vm.mem4b00[0xff] = 7;
    op(COK_ECL_NEWECL); imm(7);
    vm.hooks = (cok_ecl_hooks){.load = load, .context = &l};
    CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_OK);
    CHECK(l.loads == 1 && vm.block == 7 && vm.mem4b00[0xf2] == 3 && vm.reload);
    CHECK(vm.vectors[0] == 0x8014 && vm.vectors[4] == 0x8018 && vm.ip == 0x8014);
    CHECK(vm.mem4b00[0x100] == 0 && vm.mem7c00[0x382] == 0 && vm.mem4b00[0xff] == 7);
    CHECK(vm.code[0x14] == COK_ECL_EXIT && vm.size == n - 2);

    vm.keep_vars = true;
    vm.mem4b00[0x100] = 5;
    at = 0;
    op(COK_ECL_NEWECL); imm(7);
    CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_OK && vm.mem4b00[0x100] == 5);
    at = 0;
    op(COK_ECL_NEWECL); imm(8);
    CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_LOAD_FAILED);
    CHECK(cok_ecl_load(&vm, block, 1) == COK_ECL_BAD_BLOCK);
}

static void test_errors(void)
{
    log_t log = start();
    op(COK_ECL_UNUSED_1F);
    CHECK(run(&log) == COK_ECL_BAD_OPCODE);
    at = 0;
    op(COK_ECL_PROTECTION);
    CHECK(run(&log) == COK_ECL_BAD_OPCODE);
    at = 0;
    op(COK_ECL_GOTO); word(0x7fff);
    CHECK(run(&log) == COK_ECL_BAD_ADDRESS);
    at = 0;
    op(COK_ECL_GOTO); word(0x9dff);
    vm.code[COK_ECL_CODE_SIZE - 1] = COK_ECL_GOTO;
    CHECK(run(&log) == COK_ECL_BAD_ADDRESS);
    CHECK(strcmp(cok_ecl_status_string(COK_ECL_BAD_ADDRESS), "code address outside the ECL buffer") == 0);

    /* The host can end a run, as DS:4b57 does. */
    at = 0;
    op(COK_ECL_GOTO); word(COK_ECL_BASE);
    vm.abort = true;
    CHECK(run(&log) == COK_ECL_OK);
}

/* Run each real block's vectors with a budget, as a smoke test. NEWECL
 * loads from ECL<vm.file>.DAX, as 3775:0361 does. */
typedef struct {
    dax_archive archives[3];
    long steps;
    int loads;
} smoke;

static bool load_real(cok_ecl *v, uint8_t block, void *context)
{
    smoke *s = context;
    if (v->file < 1 || v->file > 3) return false;
    const dax_archive *archive = &s->archives[v->file - 1];
    for (size_t i = 0; i < archive->count; ++i) {
        dax_record record;
        if (dax_record_at(archive, i, &record) != DAX_OK || record.id != block) continue;
        uint8_t *data = malloc(record.decoded_size);
        bool ok = data != NULL &&
                  dax_decode(record.packed, record.packed_size, data, record.decoded_size) == DAX_OK &&
                  cok_ecl_load(v, data, record.decoded_size) == COK_ECL_OK;
        free(data);
        ++s->loads;
        return ok;
    }
    return false;
}

static void count_down(cok_ecl *v, void *context)
{
    smoke *s = context;
    if (--s->steps <= 0) v->abort = true;
}

static void test_real_blocks(void)
{
    static smoke s;
    const char *files[] = {"COKAssets/ECL1.DAX", "COKAssets/ECL2.DAX", "COKAssets/ECL3.DAX"};
    for (size_t f = 0; f < 3; ++f) CHECK(dax_open(files[f], &s.archives[f]) == DAX_OK);
    size_t blocks = 0;
    for (size_t f = 0; f < 3; ++f) {
        const dax_archive *archive = &s.archives[f];
        for (size_t i = 0; i < archive->count; ++i) {
            dax_record record;
            CHECK(dax_record_at(archive, i, &record) == DAX_OK);
            cok_ecl_init(&vm, &cok_rules_champions, &(cok_ecl_hooks){.load = load_real, .trace = count_down, .context = &s});
            vm.file = (uint8_t)(f + 1);
            CHECK(load_real(&vm, record.id, &s));
            CHECK(cok_ecl_start(&vm, true) == COK_ECL_OK && vm.vectors[4] == 0x8014);
            if (record.id == 16) {
                CHECK(cok_ecl_decode(&vm, 0x807f) == COK_ECL_OK);
                CHECK(vm.opcode == COK_ECL_PRINTCLEAR);
                CHECK(strcmp(vm.string[0], "MAYA LEADS YOU TO THE TOWN OF NERAKA.") == 0);
            }
            /* Load vector first, as 2fd3:3c28 does, then the others. */
            for (size_t v = 0; v < 5; ++v) {
                s.steps = 5000;
                vm.abort = false;
                uint16_t vector = vm.vectors[(v + 4) % 5];
                cok_ecl_status status = cok_ecl_run(&vm, vector);
                if (status != COK_ECL_OK && status != COK_ECL_DIVIDE_BY_ZERO) {
                    fprintf(stderr, "ECL %u vector %04x: %s at %04x\n", (unsigned)record.id, vector,
                            cok_ecl_status_string(status), vm.ip);
                    CHECK(0);
                }
            }
            ++blocks;
        }
    }
    for (size_t f = 0; f < 3; ++f) dax_close(&s.archives[f]);
    CHECK(blocks == 22 && s.loads > 22);
}

/* Every character field of 0x7c00-0x7fff (3775:07d9, 3775:0c00), against
 * the record's offsets in Champions: each read as its byte, signed byte or
 * word, and each store changing its bytes and no other. */
static void test_character_fields(void)
{
    start();
    uint8_t record[COK_CHARACTER_SIZE], expected[COK_CHARACTER_SIZE];
    for (size_t i = 0; i < sizeof record; ++i) record[i] = (uint8_t)(i * 37 + 11);
    vm.character = record;
    static const struct {
        uint16_t address, offset;
        uint8_t kind; /* 0 byte, 1 signed byte, 2 word */
    } reads[] = {
        {0x7c15, 0x13, 0}, {0x7c18, 0x19, 0}, {0x7c19, 0x1b, 0}, {0x7c61, 0x5c, 0},
        {0x7c72, 0x5a, 1}, {0x7c73, 0x5b, 1}, {0x7c76, 0x62, 0}, {0x7c9b, 0xd1, 0},
        {0x7ca0, 0xd6, 1}, {0x7cb8, 0xe7, 0}, {0x7cbb, 0xeb, 2}, {0x7cbd, 0xed, 2},
        {0x7cbf, 0xef, 2}, {0x7cc1, 0xf1, 2}, {0x7cc3, 0xf3, 2}, {0x7ccc, 0xfb, 1},
        {0x7ccd, 0x100, 1}, {0x7cd0, 0xfd, 1}, {0x7cd6, 0x109, 1}, {0x7cd8, 0x10a, 1},
        {0x7cf7, 0x130, 2}, {0x7cf9, 0x132, 0}, {0x7d19, 0x197, 0}, {0x7d1b, 0x198, 0},
        {0x7ca5, 0xdb, 0}, {0x7ca6, 0xdc, 0}, {0x7ca7, 0xdd, 0}, {0x7ca8, 0xde, 0},
        {0x7ca9, 0xdf, 0}, {0x7caa, 0xe0, 0}, {0x7cab, 0xe1, 0}, {0x7cac, 0xe2, 0},
    };
    /* Each with bit 7 set, so that a signed read differs from a plain one. */
    for (size_t i = 0; i < sizeof reads / sizeof *reads; ++i) record[reads[i].offset] |= 0x80;
    for (size_t i = 0; i < sizeof reads / sizeof *reads; ++i) {
        const uint8_t *b = record + reads[i].offset;
        uint16_t want = (uint16_t)(reads[i].kind == 0   ? b[0]
                                   : reads[i].kind == 1 ? (uint16_t)(int16_t)(int8_t)b[0]
                                                        : (uint16_t)(b[0] | b[1] << 8));
        CHECK(cok_ecl_load_var(&vm, reads[i].address) == want);
        vm.character = NULL;
        CHECK(cok_ecl_load_var(&vm, reads[i].address) == 0);
        vm.character = record;
    }
    /* The status as the scripts number it, and the sides. */
    static const uint8_t status[][2] = {{0, 1}, {1, 0}, {2, 0}, {3, 6}, {4, 5}, {5, 4},
                                        {6, 3}, {7, 7}, {8, 2}, {9, 0}};
    for (size_t i = 0; i < sizeof status / sizeof *status; ++i) {
        record[0x188] = status[i][0];
        CHECK(cok_ecl_load_var(&vm, 0x7d00) == status[i][1]);
    }
    static const uint8_t sides[][3] = {{0, 0, 0}, {0, 1, 0x80}, {1, 0, 0x81}, {1, 1, 0x81},
                                       {2, 1, 0}};
    for (size_t i = 0; i < sizeof sides / sizeof *sides; ++i) {
        record[0x18a] = sides[i][0];
        record[0x18b] = sides[i][1];
        CHECK(cok_ecl_load_var(&vm, 0x7d0c) == sides[i][2]);
    }
    /* The charisma's reaction table (0x7ecf), from +0x1b. */
    record[0x1b] = 3;
    CHECK(cok_ecl_load_var(&vm, 0x7ecf) == 0);
    record[0x1b] = 13;
    CHECK(cok_ecl_load_var(&vm, 0x7ecf) == 30);
    record[0x1b] = 25;
    CHECK(cok_ecl_load_var(&vm, 0x7ecf) == 60);
    record[0x1b] = 26;
    CHECK(cok_ecl_load_var(&vm, 0x7ecf) == 0);

    /* Stores: each changes its own bytes only. */
    memcpy(expected, record, sizeof record);
    for (uint16_t k = 0; k < 0x3a; ++k) {
        cok_ecl_store(&vm, (uint16_t)(0x7c20 + k), (uint16_t)(0x300 + k));
        expected[0x1e + k] = (uint8_t)k;
    }
    cok_ecl_store(&vm, 0x7cb8, 0xb2);
    expected[0xe7] = 0xb2;
    CHECK(memcmp(record, expected, sizeof record) == 0);
    cok_ecl_store(&vm, 0x7cb8, 0xb3);
    expected[0xe7] = 0x81;
    static const uint16_t coins[5] = {0x7cbb, 0x7cbd, 0x7cbf, 0x7cc1, 0x7cc3};
    for (size_t k = 0; k < 5; ++k) {
        cok_ecl_store(&vm, coins[k], (uint16_t)(0x1201 + 0x1111 * k));
        expected[0xeb + 2 * k] = (uint8_t)(0x01 + 0x11 * k);
        expected[0xec + 2 * k] = (uint8_t)(0x12 + 0x11 * k);
    }
    cok_ecl_store(&vm, 0x7cf7, 0xbeef);
    expected[0x130] = 0xef;
    expected[0x131] = 0xbe;
    cok_ecl_store(&vm, 0x7cf9, 0x1ab);
    expected[0x132] = 0xab;
    cok_ecl_store(&vm, 0x7d19, 0x2cd);
    expected[0x197] = 0xcd;
    CHECK(memcmp(record, expected, sizeof record) == 0);
    record[0x188] = expected[0x188] = 6;
    record[0x189] = expected[0x189] = 0;
    cok_ecl_store(&vm, 0x7d00, 2);
    CHECK(memcmp(record, expected, sizeof record) == 0);
    cok_ecl_store(&vm, 0x7d00, 1);
    expected[0x188] = 0;
    expected[0x189] = 1;
    CHECK(memcmp(record, expected, sizeof record) == 0);
    static const uint16_t side_stores[][3] = {{0x81, 1, 1}, {0x80, 0, 1}, {0x82, 0, 1}, {0, 0, 0}};
    for (size_t i = 0; i < sizeof side_stores / sizeof *side_stores; ++i) {
        cok_ecl_store(&vm, 0x7d0c, side_stores[i][0]);
        expected[0x18a] = (uint8_t)side_stores[i][1];
        expected[0x18b] = (uint8_t)side_stores[i][2];
        CHECK(memcmp(record, expected, sizeof record) == 0);
    }
    /* Fields the VM does not write keep their bytes; the words keep the
     * value. */
    cok_ecl_store(&vm, 0x7c15, 9);
    cok_ecl_store(&vm, 0x7d1b, 9);
    CHECK(memcmp(record, expected, sizeof record) == 0 && vm.mem7c00[0x15] == 9);
    vm.character = NULL;
}

/* The variables by name: Champions' each a word at its address (as code
 * read them before they had names); Death Knights' a byte, at the
 * addresses its code has (dkk 3c1e:01c1, 4381:3106, 6c84:00a6, ...). */
static void test_named_variables(void)
{
    static const struct {
        cok_var name;
        uint16_t champions, death_knights, count;
    } names[] = {
        {COK_VAR_OVERLAND_X, 0x4bc3, 0x4bc3, 1}, {COK_VAR_OVERLAND_Y, 0x4bc4, 0x4bc4, 1},
        {COK_VAR_MAP, 0x4bc5, 0x4bc5, 1},        {COK_VAR_CLOCK, 0x4bc6, 0x4bc6, 7},
        {COK_VAR_NO_MAGIC, 0x4be5, 0x4be5, 1},   {COK_VAR_3D, 0x4be6, 0x4be6, 1},
        {COK_VAR_PIECES, 0x4be7, 0x4be7, 2},     {COK_VAR_LAST_X, 0x4bf0, 0x4bf0, 1},
        {COK_VAR_LAST_Y, 0x4bf1, 0x4bf1, 1},     {COK_VAR_BLOCK, 0x4bf2, 0x4bf2, 1},
        {COK_VAR_HIDDEN, 0x4bfb, 0x4bfb, 1},     {COK_VAR_SPEED, 0x4bfc, 0x4bfc, 1},
        {COK_VAR_SKY_LOW, 0x4bfd, 0x4bfe, 1},    {COK_VAR_SKY_HIGH, 0x4bfe, 0x4bfd, 1},
        {COK_VAR_PICTURES, 0x4bff, 0x4bff, 1},   {COK_VAR_LOCALS, 0x4c00, 0x4c00, 0x20},
        {COK_VAR_SAVINGS, 0x4c33, 0x4c33, 1},
        {COK_VAR_OFF_MAP, 0x4c38, 0x4cf7, 1},    {COK_VAR_NO_VIEW, 0x4c38, 0, 1},
        {COK_VAR_SAVES, 0x4c3c, 0x4cf6, 1},
        {COK_VAR_DIFFICULTY, 0x4cf4, 0x4cf4, 1}, {COK_VAR_BANK, 0x4cf5, 0x4cf5, 1},
        {COK_VAR_OVERLAND, 0x4cf7, 0x4cf7, 1},   {COK_VAR_TREASURE_FIRST, 0x4cf8, 0x4cf8, 1},
        {COK_VAR_MOONS, 0x4cf9, 0x4cf9, 3},      {COK_VAR_MOON_DAYS, 0x4cfc, 0x4cfc, 3},
        {COK_VAR_TURN, 0x4cff, 0, 1},            {COK_VAR_ORDER_MOON, 0x4cf8, 0x4cf8, 0x100},
        {COK_VAR_AREA, 0x4b00, 0, 0x100},        {COK_VAR_HALL, 0x7ea8, 0x7ea8, 1},
        {COK_VAR_DISTANCE_WANTED, 0x7ec0, 0x7ec0, 1}, {COK_VAR_DISTANCE, 0x7ec1, 0x7ec1, 1},
        {COK_VAR_MORALE, 0x7ec6, 0x7ec6, 1},          {COK_VAR_OUTCOME, 0x7ec7, 0x7ec7, 1},
        {COK_VAR_DROPPED, 0x7ec8, 0x7ec8, 1},         {COK_VAR_STAY, 0x7ec9, 0x7ec9, 1},
        {COK_VAR_SEARCH, 0x7eca, 0x7eca, 1},     {COK_VAR_SURPRISE, 0x7ecb, 0x7ecb, 1},
        {COK_VAR_REST_EVERY, 0x7ed2, 0x7ed2, 1}, {COK_VAR_REST_CHANCE, 0x7ed3, 0x7ed3, 1},
        {COK_VAR_EDGE, 0x7ed5, 0x7ed5, 1},       {COK_VAR_PORTRAIT, 0x7ee1, 0, 1},
        {COK_VAR_TEMPLE, 0x7ee2, 0x7ee2, 1},          {COK_VAR_KEEP_ITEMS, 0x7ee3, 0x7ee3, 1},
        {COK_VAR_GENTLE, 0x7ee6, 0x7ee6, 1},          {COK_VAR_ECL_FILE, 0x7f12, 0x7f12, 1},
        {COK_VAR_FLED, 0x7f33, 0x7f33, 1},            {COK_VAR_PARTY_SIZE, 0x7f3e, 0x7f3e, 1},
        {COK_VAR_REMAINDER, 0x7f3f, 0x7f3f, 1},  {COK_VAR_SHOP, 0x7f6c, 0x7f6c, 1},
        {COK_VAR_PRICES, 0x7f6d, 0, 1},          {COK_VAR_HIT_BONUS, 0x7f70, 0x7f70, 2},
        {COK_VAR_MOVE_BONUS, 0x7f72, 0x7f72, 1}, {COK_VAR_LOCALS_GAME, 0x7f79, 0x7f79, 10},
        {COK_VAR_VAULT, 0, 0x4d00, 1},           {COK_VAR_RIVER, 0, 0x4bf3, 1},
    };
    CHECK(sizeof names / sizeof *names == COK_VARS);
    for (size_t i = 0; i < COK_VARS; ++i) {
        CHECK(names[i].name == i);
        const cok_rules_var *c = &C->ecl.vars[i], *d = &DK->ecl.vars[i];
        /* Champions has every variable but Death Knights' own. */
        CHECK(c->address == names[i].champions &&
              cok_ecl_has_var(C, (cok_var)i) == (c->address != 0));
        if (c->address != 0)
            CHECK(c->width == 2 && c->count == names[i].count && c->off == 0);
        CHECK(d->address == names[i].death_knights &&
              cok_ecl_has_var(DK, (cok_var)i) == (d->address != 0));
        if (d->address != 0)
            CHECK(d->width == (i == COK_VAR_SAVINGS ? 2 : 1) && d->count == names[i].count &&
                  d->off == (i == COK_VAR_OFF_MAP));
        for (size_t k = 0; k < names[i].count; ++k) {
            if (c->address != 0)
                CHECK(cok_ecl_var_address(C, (cok_var)i, k) == names[i].champions + k);
            if (d->address != 0)
                CHECK(cok_ecl_var_address(DK, (cok_var)i, k) == names[i].death_knights + k);
        }
    }

    /* Champions': the word itself, whatever its value. */
    cok_ecl_init(&vm, C, NULL);
    cok_ecl_set_var(&vm, COK_VAR_DIFFICULTY, 0x1234);
    CHECK(vm.mem4b00[0x1f4] == 0x1234 && cok_ecl_var(&vm, COK_VAR_DIFFICULTY) == 0x1234);
    cok_ecl_set_var_at(&vm, COK_VAR_CLOCK, COK_CLOCK_HOURS, 300);
    CHECK(vm.mem4b00[0xc9] == 300 && cok_ecl_var_at(&vm, COK_VAR_CLOCK, 3) == 300);
    cok_ecl_add_var(&vm, COK_VAR_PARTY_SIZE, -1);
    CHECK(vm.mem7c00[0x33e] == 0xffff);
    vm.mem4b00[0x138] = 2;
    CHECK(cok_ecl_flag(&vm, COK_VAR_OFF_MAP));
    vm.mem4b00[0x138] = 0;
    vm.mem4b00[0x1f7] = 1;
    CHECK(!cok_ecl_flag(&vm, COK_VAR_OFF_MAP));
    /* Without the stores' side effects: no mode, no hook. */
    log_t log = {0};
    vm.hooks = (cok_ecl_hooks){.stored = on_store, .context = &log};
    vm.mode = 4;
    cok_ecl_set_var(&vm, COK_VAR_3D, 0);
    CHECK(vm.mode == 4 && log.stores == 0);

    /* Death Knights': a byte; its off-map flag is 0x4cf7 not 1. */
    cok_ecl_init(&vm, DK, NULL);
    cok_ecl_set_var(&vm, COK_VAR_DIFFICULTY, 0x1234);
    CHECK(vm.mem4b00[0x1f4] == 0x34 && cok_ecl_var(&vm, COK_VAR_DIFFICULTY) == 0x34);
    cok_ecl_set_var_at(&vm, COK_VAR_CLOCK, COK_CLOCK_HOURS, 300);
    CHECK(vm.mem4b00[0xc9] == 44 && vm.mem4b00[0xca] == 0);
    cok_ecl_add_var(&vm, COK_VAR_PARTY_SIZE, -1);
    CHECK(vm.mem7c00[0x33e] == 0xff);
    vm.mem4b00[0x1f7] = 1;
    CHECK(!cok_ecl_flag(&vm, COK_VAR_OFF_MAP) && cok_ecl_flag(&vm, COK_VAR_OVERLAND));
    vm.mem4b00[0x1f7] = 0;
    CHECK(cok_ecl_flag(&vm, COK_VAR_OFF_MAP) && !cok_ecl_flag(&vm, COK_VAR_OVERLAND));
    /* A word over two addresses, where a game keeps one so. */
    cok_rules wide = *DK;
    cok_rules_var vars[COK_VARS];
    memcpy(vars, DK->ecl.vars, sizeof vars);
    vars[COK_VAR_CLOCK] = (cok_rules_var){.address = 0x4bc6, .width = 2, .count = 7};
    wide.ecl.vars = vars;
    cok_ecl_init(&vm, &wide, NULL);
    CHECK(cok_ecl_var_address(&wide, COK_VAR_CLOCK, 1) == 0x4bc8);
    cok_ecl_set_var_at(&vm, COK_VAR_CLOCK, 1, 0x1234);
    CHECK(vm.mem4b00[0xc8] == 0x34 && vm.mem4b00[0xc9] == 0x12 && vm.mem4b00[0xca] == 0);
    CHECK(cok_ecl_var_at(&vm, COK_VAR_CLOCK, 1) == 0x1234);

    /* A block's start: Champions' sets the portrait 0xff and allows magic;
     * Death Knights' leaves both. Both clear the encounters while resting
     * and, unless kept, the block's variables. */
    const cok_rules *games[] = {C, DK};
    for (size_t g = 0; g < 2; ++g) {
        cok_ecl_init(&vm, games[g], NULL);
        vm.mem4b00[0xe5] = 7;
        vm.mem7c00[0x2d2] = vm.mem7c00[0x2d3] = 9;
        vm.mem4b00[0x100] = vm.mem4b00[0x11f] = vm.mem7c00[0x379] = vm.mem7c00[0x382] = 5;
        vm.mem4b00[0x120] = vm.mem7c00[0x383] = 5;
        at = 0;
        for (int i = 0; i < 5; ++i) word(0x8000);
        CHECK(cok_ecl_start(&vm, true) == COK_ECL_OK);
        CHECK(vm.mem4b00[0xe5] == (g == 0 ? 0 : 7) && vm.mem7c00[0x2e1] == (g == 0 ? 0xff : 0));
        CHECK(vm.mem7c00[0x2d2] == 0 && vm.mem7c00[0x2d3] == 0);
        CHECK(vm.mem4b00[0x100] == 0 && vm.mem4b00[0x11f] == 0 && vm.mem4b00[0x120] == 5);
        CHECK(vm.mem7c00[0x379] == 0 && vm.mem7c00[0x382] == 0 && vm.mem7c00[0x383] == 5);
    }
}

/* The special addresses: Champions' (3775:0fca, 0e06) and Death Knights'
 * (dkk 3c1e:0d85, 0bc1: no values of the host's, 0x4cf8 reading 1, stores
 * only above 49000). */
static void test_special_addresses(void)
{
    const cok_rules *games[] = {C, DK};
    for (size_t g = 0; g < 2; ++g) {
        bool dkk = g == 1;
        cok_ecl_init(&vm, games[g], NULL);
        vm.value_b1 = 11;
        vm.value_fb = 12;
        vm.value_fc = 13;
        vm.mem4b00[0x1f8] = 5;
        vm.direction = 4;
        vm.ahead = 7;
        vm.square = 0x90;
        CHECK(cok_ecl_load_var(&vm, 0xb1) == (dkk ? 0 : 11));
        CHECK(cok_ecl_load_var(&vm, 0xfb) == (dkk ? 0 : 12));
        CHECK(cok_ecl_load_var(&vm, 0xfc) == (dkk ? 0 : 13));
        CHECK(cok_ecl_load_var(&vm, 0x4cf8) == (dkk ? 1 : 5));
        CHECK(cok_ecl_load_var(&vm, 0x4bea) == 4 && cok_ecl_load_var(&vm, 0x33d) == 4);
        CHECK(cok_ecl_load_var(&vm, 0xc04e) == 7 && cok_ecl_load_var(&vm, 0xc04f) == 0x90);
        CHECK(cok_ecl_load_var(&vm, 0xc04d) == 4 && cok_ecl_load_var(&vm, 0x1234) == 0);
        /* A wide read of 0x4cf8 or 0x4bea is not widened. */
        vm.mem4b00[0xeb] = vm.mem4b00[0x1f9] = 0x22;
        at = 0;
        op(COK_ECL_ADD); var3(0x4cf8); var3(0x4bea); var3(0x7a00);
        op(COK_ECL_EXIT);
        CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_OK);
        CHECK(vm.mem7a00[0] == (dkk ? 5 : 9) && vm.mem7a00[1] == 0);
        cok_ecl_store(&vm, 0x3de, 0x21);
        cok_ecl_store(&vm, 0xb8, 0x22);
        cok_ecl_store(&vm, 0xb9, 0x23);
        CHECK(vm.value_3de == (dkk ? 0 : 0x21) && vm.value_b8 == (dkk ? 0 : 0x22));
        CHECK(vm.value_b9 == (dkk ? 0 : 0x23));
        cok_ecl_store(&vm, 0xc04c, 3);
        cok_ecl_store(&vm, 0xc059, 0x0b);
        cok_ecl_store(&vm, 0xc05f, 8);
        cok_ecl_store(&vm, 0xc04d, 9);
        CHECK(vm.map_y == 3 && vm.c059 == 0x0b && vm.c05f == 8 && vm.direction == 2);
        CHECK(cok_ecl_special_read(games[g], 0x4cf8) ==
              (dkk ? COK_ECL_SPECIAL_ONE : COK_ECL_SPECIAL_NONE));
        CHECK(cok_ecl_special_store(games[g], 0x3de) ==
              (dkk ? COK_ECL_SPECIAL_NONE : COK_ECL_SPECIAL_3DE));
        CHECK(cok_ecl_special_store(games[g], 0x4bea) == COK_ECL_SPECIAL_NONE);
    }
}

/* Death Knights' handlers where they differ from Champions' in the VM:
 * SAVE TABLE as wide as its value's operand (3632:1018), GETTABLE's read as
 * wide as its third (0fb2), DIVIDE's remainder a byte (014a), COMPARE AND of
 * strings (0bed); and the handlers not carried out. */
static void test_death_knights_handlers(void)
{
    cok_ecl_init(&vm, DK, NULL);
    vm.mem4b00[0x20] = 0x34;
    vm.mem4b00[0x21] = 0x12;
    at = 0;
    op(COK_ECL_SAVE_TABLE); word(0x0201); var(0x4b10); imm(2); /* a word over two */
    op(COK_ECL_SAVE_TABLE); var(0x4b20); var3(0x4b30); imm(0); /* a byte */
    op(COK_ECL_GETTABLE); word(0x4b1e); imm(2); var3(0x7a00);  /* a word read */
    op(COK_ECL_DIVIDE); word(0x0123); imm(0x10); var(0x7a10);
    op(COK_ECL_COMPARE_AND); text("DURFEY"); text("DURFEY"); imm(1); imm(1);
    op(COK_ECL_EXIT);
    CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_OK);
    CHECK(vm.mem4b00[0x12] == 1 && vm.mem4b00[0x13] == 2);
    CHECK(vm.mem4b00[0x30] == 0x34 && vm.mem4b00[0x31] == 0);
    CHECK(vm.mem7a00[0] == 0x34 && vm.mem7a00[1] == 0x12);
    CHECK(vm.mem7a00[0x10] == 0x12 && vm.mem7c00[0x33f] == 3);
    CHECK(vm.flags[0] && !vm.flags[1]);
    at = 0;
    op(COK_ECL_COMPARE_AND); imm(1); imm(1); text("DURFEY"); text("STURM");
    op(COK_ECL_EXIT);
    CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_OK && !vm.flags[0] && vm.flags[1]);
    at = 0;
    op(COK_ECL_COMPARE_AND); text("A"); text("A"); text("B"); text("B");
    op(COK_ECL_EXIT);
    CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_OK && vm.flags[0]);
    /* Champions' compares the values, strings or not: equal stale ones. */
    cok_ecl_init(&vm, C, NULL);
    at = 0;
    op(COK_ECL_COMPARE_AND); text("A"); text("B"); imm(1); imm(1);
    op(COK_ECL_DIVIDE); word(0x0123); imm(0x10); var(0x7a10);
    op(COK_ECL_EXIT);
    CHECK(cok_ecl_run(&vm, COK_ECL_BASE) == COK_ECL_OK && vm.flags[0]);
    CHECK(vm.mem7c00[0x33f] == 3);

    /* Every opcode of both games is carried out now (TREASURE ITEMS,
     * DAMAGE, STOCK and ADD EP were the last of Death Knights'). */
    for (unsigned o = 0; o < COK_ECL_OPCODES; ++o) {
        CHECK(cok_ecl_unported(DK, o) == NULL);
        CHECK(cok_ecl_unported(C, o) == NULL);
    }
    CHECK(cok_ecl_unported(DK, 0x99) == NULL);
    CHECK(DK->ecl.compare_and_strings && !C->ecl.compare_and_strings);
    CHECK(DK->ecl.delay_ms == 600 && C->ecl.delay_ms == 0);
    CHECK(!DK->ecl.menu_single_item && C->ecl.menu_single_item);
}

/* Death Knights' character fields (dkk 3c1e:073a, 0a9a), on a record of
 * its own built by hand: each read and write at its offset; the spells
 * memorized in the spells' block. */
static void test_death_knights_fields(void)
{
    cok_ecl_init(&vm, DK, NULL);
    uint8_t record[COK_CHARACTER_MAX], expected[COK_CHARACTER_MAX];
    for (size_t i = 0; i < sizeof record; ++i) record[i] = (uint8_t)(i * 37 + 11);
    vm.character = record;
    static const struct {
        uint16_t address, offset;
        uint8_t kind; /* 0 byte, 1 signed byte, 2 word */
    } reads[] = {
        {0x7c14, 0x11, 0}, {0x7c15, 0x13, 0}, {0x7c19, 0x1b, 0}, {0x7c1a, 0x1d, 0},
        {0x7c65, 0x10, 0}, {0x7c6a, 0x1a, 0}, {0x7c6b, 0x1c, 0}, {0x7c71, 0x1f, 0},
        {0x7c72, 0x20, 1}, {0x7c74, 0x26, 2}, {0x7c76, 0x28, 0}, {0x7c9a, 0x2a, 0},
        {0x7c9e, 0x2e, 0}, {0x7c9f, 0xd7, 0}, {0x7ca5, 0x32, 0}, {0x7cac, 0x39, 0},
        {0x7cb8, 0x3e, 0}, {0x7cbb, 0x24, 0}, {0x7cbc, 0x23, 1}, {0x7cbf, 0x22, 0},
        {0x7cc3, 0x3f, 2}, {0x7cc4, 0x3f, 2}, {0x7cc5, 0x41, 2}, {0x7cc8, 0x43, 2},
        {0x7cd3, 0xc1, 0}, {0x7cd6, 0x55, 1}, {0x7cd8, 0x56, 1}, {0x7d0e, 0xcb, 0},
        {0x7d0f, 0xcc, 0}, {0x7d10, 0xcd, 0}, {0x7d19, 0xd6, 0}, {0x7d1a, 0xd7, 0},
    };
    for (size_t i = 0; i < sizeof reads / sizeof *reads; ++i) record[reads[i].offset] |= 0x80;
    for (size_t i = 0; i < sizeof reads / sizeof *reads; ++i) {
        const uint8_t *b = record + reads[i].offset;
        uint16_t want = (uint16_t)(reads[i].kind == 0   ? b[0]
                                   : reads[i].kind == 1 ? (uint16_t)(int16_t)(int8_t)b[0]
                                                        : (uint16_t)(b[0] | b[1] << 8));
        CHECK(cok_ecl_load_var(&vm, reads[i].address) == want);
        vm.character = NULL;
        CHECK(cok_ecl_load_var(&vm, reads[i].address) == 0);
        vm.character = record;
    }
    /* Not fields here: Champions' 0x7c73, 0x7ca0, 0x7cbd, 0x7cc9, 0x7cf7,
     * 0x7d0c, 0x7d1b, 0x7ecf and 0x7f12 read the variable. */
    static const uint16_t variables[] = {0x7c73, 0x7ca0, 0x7cbd, 0x7cc9, 0x7cf7,
                                         0x7d0c, 0x7d1b, 0x7ecf, 0x7f12};
    for (size_t i = 0; i < sizeof variables / sizeof *variables; ++i) {
        vm.mem7c00[variables[i] - 0x7c00] = 0x5a;
        CHECK(cok_ecl_load_var(&vm, variables[i]) == 0x5a);
    }
    /* The name's text by the size byte (+0x29): one byte, or a word. */
    memcpy(record, "\005DURFE", 6);
    record[0x29] = 1;
    CHECK(cok_ecl_load_var(&vm, 0x7c00) == 'D' && cok_ecl_load_var(&vm, 0x7c02) == 'R');
    record[0x29] = 2;
    CHECK(cok_ecl_load_var(&vm, 0x7c00) == ('D' | 'U' << 8));
    record[0x29] = 0;
    CHECK(cok_ecl_load_var(&vm, 0x7c01) == 0);
    /* Active: 1, else 0x80; the classes by bit. */
    record[0xc8] = 0;
    CHECK(cok_ecl_load_var(&vm, 0x7d00) == 0x80);
    record[0xc8] = 3;
    CHECK(cok_ecl_load_var(&vm, 0x7d00) == 1);
    memset(record + 0x45, 0, 8);
    record[0x4a] = 1;            /* class 5: bit 0 */
    record[0x47] = 2;            /* class 2: bit 3 */
    record[0x49] = 0x81;         /* class 4, negative: no bit 7 */
    record[0x46] = 9;            /* class 1: no bit */
    CHECK(cok_ecl_load_var(&vm, 0x7ceb) == 0x09);
    record[0x49] = 1;
    CHECK(cok_ecl_load_var(&vm, 0x7ceb) == 0x89);

    /* Stores: each changes its own bytes only. */
    memcpy(expected, record, sizeof record);
    cok_ecl_store(&vm, 0x7cb8, 0xb3); /* no 0x32 off */
    expected[0x3e] = 0xb3;
    vm.name_cleared = false;
    cok_ecl_store(&vm, 0x7c00, 5); /* any store clears the name's flag */
    CHECK(vm.name_cleared && memcmp(record, expected, sizeof record) == 0);
    cok_ecl_store_as(&vm, &(cok_ecl_operand){COK_ECL_VAR_3, 0xc6, 0x7c}, 0xbeef);
    expected[0x41] = 0xef;
    expected[0x42] = 0xbe;
    cok_ecl_store_as(&vm, &(cok_ecl_operand){COK_ECL_VAR_3, 0x19, 0x7d}, 0x2cd);
    expected[0xd6] = 0xcd;
    CHECK(memcmp(record, expected, sizeof record) == 0);
    record[0xc7] = expected[0xc7] = 6;
    record[0xc8] = expected[0xc8] = 0;
    cok_ecl_store(&vm, 0x7d00, 1);
    expected[0xc7] = 0;
    expected[0xc8] = 1;
    CHECK(memcmp(record, expected, sizeof record) == 0);
    static const uint16_t side_stores[][3] = {{0x81, 1, 1}, {0x80, 0, 1}, {0x82, 0, 1}, {0, 0, 0}};
    for (size_t i = 0; i < sizeof side_stores / sizeof *side_stores; ++i) {
        cok_ecl_store(&vm, 0x7d0c, side_stores[i][0]);
        expected[0xc9] = (uint8_t)side_stores[i][1];
        expected[0xca] = (uint8_t)side_stores[i][2];
        CHECK(memcmp(record, expected, sizeof record) == 0);
    }
    /* The ECL file is written, not read (0x7f12 reads the variable). */
    vm.file = 1;
    cok_ecl_store(&vm, 0x7f12, 6);
    CHECK(vm.file == 3 && vm.previous_file == 1 && cok_ecl_load_var(&vm, 0x7f12) == 6);
    /* Fields only read keep their bytes; the variable takes the value. */
    cok_ecl_store(&vm, 0x7c71, 9);
    cok_ecl_store(&vm, 0x7d1a, 9);
    CHECK(memcmp(record, expected, sizeof record) == 0 && vm.mem7c00[0x71] == 9);
    /* The spells memorized, 0x7c1b-0x7c64, in the spells' block after the
     * record (+0xd8): read and written while the record holds one; 0 and
     * no write without (3c1e:073a and 0a9a test the pointer +0xc2). */
    const cok_character_layout *L = DK->character;
    cok_field_set_spells(L, record, true);
    record[0xd8] = 0x21;
    record[0xd8 + 0x49] = 0x22;
    CHECK(cok_ecl_load_var(&vm, 0x7c1b) == 0x21 && cok_ecl_load_var(&vm, 0x7c64) == 0x22);
    cok_ecl_store(&vm, 0x7c1c, 0x33);
    CHECK(record[0xd9] == 0x33);
    cok_field_set_spells(L, record, false);
    record[0xd8] = 0x21;
    CHECK(cok_ecl_load_var(&vm, 0x7c1b) == 0 && cok_ecl_load_var(&vm, 0x7c1c) == 0);
    cok_ecl_store(&vm, 0x7c1c, 0x44);
    CHECK(record[0xd9] == 0);
    vm.character = NULL;
}

int main(void)
{
    test_names_and_counts();
    test_death_knights_table();
    test_byte_variables();
    test_operands_and_text();
    test_arithmetic_and_flags();
    test_if_skips();
    test_calls_and_tables();
    test_menus_and_hooks();
    test_variables();
    test_character_fields();
    test_named_variables();
    test_special_addresses();
    test_death_knights_handlers();
    test_death_knights_fields();
    test_random();
    test_newecl();
    test_errors();
    test_real_blocks();
    puts("ECL tests passed");
    return 0;
}
