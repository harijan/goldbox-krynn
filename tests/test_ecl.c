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
    test_random();
    test_newecl();
    test_errors();
    test_real_blocks();
    puts("ECL tests passed");
    return 0;
}
