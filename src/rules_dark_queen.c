#include "ecl.h"
#include "rules.h"

/* The Dark Queen of Krynn (version 1.1 of July 29, 1992; "dqk" addresses
 * are build/DQK_FULL.EXE's, Ghidra's numbering of its map segment + 0x1000,
 * its data segment DS 0x422e). Read headless so far (DARK-QUEEN.md's step
 * 1): its HLIB libraries, its pictures and palettes, its font, its ECL
 * and its 3D view, drawn by its own routines (dark_queen.c). What it has
 * not or the port does not reach yet is left out (its characters, data
 * segment, spells, combat), so that a read of it stops at its accessor's
 * assertion, and krynn refuses to play it. */

/* DISK1-3 of the installed game (it reads disk1\dqk.cfg, DS:608e). */
static const char *const dark_queen_folders[] = {"DISK1", "DISK2", "DISK3", NULL};

/* The ECL's opcodes (the dispatch dqk 5f07:0d1e, its handlers' table
 * 5f07:0d34, each loading its operands through 5f07:0013); a failed IF
 * skips as many as the handler loads, a list opcode's whole list
 * (6c70:1385, its table 6c70:13aa). The opcodes whose handlers need what
 * is not ported (characters, monsters, combat, treasure) stop the run,
 * citing them (DARK-QUEEN.md's step 3 goes through each). */
#define OP(o, m, n) [o] = {.meaning = m, .operands = n, .skip = n}
#define SAME(m, n) OP(m, m, n)
#define LISTED(m, h) [m] = {.meaning = m, .operands = COK_ECL_LIST, .header = h, .skip_list = true}
#define NOT(o, m, n, why) [o] = {.meaning = m, .operands = n, .skip = n, .unported = why}
static const cok_rules_opcode dark_queen_opcodes[COK_ECL_OPCODES] = {
    SAME(COK_ECL_EXIT, 0),
    SAME(COK_ECL_GOTO, 1),
    SAME(COK_ECL_GOSUB, 1),
    SAME(COK_ECL_COMPARE, 2),
    SAME(COK_ECL_ADD, 3),
    SAME(COK_ECL_SUBTRACT, 3),
    SAME(COK_ECL_DIVIDE, 3),
    SAME(COK_ECL_MULTIPLY, 3),
    SAME(COK_ECL_RANDOM, 2),
    SAME(COK_ECL_SAVE, 2),
    SAME(COK_ECL_LOAD_CHARACTER, 1), /* 5f07:0422 */
    NOT(0x0b, COK_ECL_LOAD_MONSTER, 3, "dqk 6c70:29a3"),
    NOT(0x0c, COK_ECL_SETUP_MONSTERS, 4, "dqk 6c70:2db1"),
    NOT(0x0d, COK_ECL_APPROACH, 0, "dqk 6c70:2e4f"),
    SAME(COK_ECL_PICTURE, 1),        /* 5f07:0552 */
    NOT(0x0f, COK_ECL_INPUT_NUMBER, 2, "dqk 6c70:2e7c"),
    NOT(0x10, COK_ECL_INPUT_STRING, 2, "dqk 6c70:2ec8"),
    SAME(COK_ECL_PRINT, 1),          /* 6c70:1cd3 */
    SAME(COK_ECL_PRINTCLEAR, 1),
    SAME(COK_ECL_RETURN, 0),
    SAME(COK_ECL_COMPARE_AND, 4),    /* 5f07:05be */
    LISTED(COK_ECL_VERTICAL_MENU, 3), /* 6c70:1e91 */
    SAME(COK_ECL_IF_EQUAL, 0),
    SAME(COK_ECL_IF_NOT_EQUAL, 0),
    SAME(COK_ECL_IF_LESS, 0),
    SAME(COK_ECL_IF_GREATER, 0),
    SAME(COK_ECL_IF_LESS_EQUAL, 0),
    SAME(COK_ECL_IF_GREATER_EQUAL, 0),
    SAME(COK_ECL_CLEARMONSTERS, 0),  /* 5f07:07fe */
    OP(0x1d, COK_ECL_PRESS_KEY, 0),  /* 5f07:1383 */
    OP(0x1e, COK_ECL_NOTHING, 6),    /* 5f07:0812: loads CHECKPARTY's six, does nothing */
    OP(0x1f, COK_ECL_JOURNAL, 1),    /* 6c70:32b2 */
    SAME(COK_ECL_NEWECL, 1),         /* 6c70:1d8d */
    OP(0x21, COK_ECL_LOAD_MAP, 2),   /* 6c70:1dec */
    OP(0x22, COK_ECL_YES_NO, 0),     /* 5f07:13c4 */
    OP(0x23, COK_ECL_CLEAR_EXIT, 0), /* 5f07:1402 */
    NOT(0x24, COK_ECL_COMBAT, 0, "dqk 5f07:081b"),
    LISTED(COK_ECL_ON_GOTO, 2),      /* 5f07:0991 */
    LISTED(COK_ECL_ON_GOSUB, 2),
    [0x27] = {.meaning = COK_ECL_TREASURE_ITEMS, .operands = COK_ECL_LIST, .header = 4,
              .skip_list = true, .unported = "dqk 6c70:2127"},
    NOT(0x28, COK_ECL_ROB, 3, "dqk 6c70:28f8"),
    OP(0x29, COK_ECL_NOTHING, 0),    /* 5f07:1449 */
    SAME(COK_ECL_GETTABLE, 3),
    LISTED(COK_ECL_HORIZONTAL_MENU, 2), /* 6c70:1ff7 with 0 */
    OP(0x2c, COK_ECL_MUSIC, 1),      /* 5f07:1420 */
    SAME(COK_ECL_CALL, 1),           /* 5f07:0aba */
    NOT(0x2e, COK_ECL_DAMAGE, 5, "dqk 6c70:22ac"),
    SAME(COK_ECL_AND, 3),
    SAME(COK_ECL_OR, 3),
    [0x31] = {.meaning = COK_ECL_ASK_MENU, .operands = COK_ECL_LIST, .header = 2,
              .skip_list = true}, /* 6c70:1ff7 with 1 */
    NOT(0x32, COK_ECL_FIND_ITEM, 1, "dqk 6c70:2f3b"),
    SAME(COK_ECL_PRINT_RETURN, 0),   /* 5f07:0a68 */
    NOT(0x34, COK_ECL_ECL_CLOCK, 2, "dqk 5f07:0a35"),
    SAME(COK_ECL_SAVE_TABLE, 3),
    NOT(0x36, COK_ECL_ADD_NPC, 2, "dqk 6c70:307b"),
    NOT(0x37, COK_ECL_FIND_NPC, 2, "dqk 6c70:31e8"),
    NOT(0x38, COK_ECL_PROGRAM, 1, "dqk 5f07:0baf"),
    NOT(0x39, COK_ECL_WHO, 1, "dqk 6c70:2fe0"),
    SAME(COK_ECL_DELAY, 0),          /* 5f07:0a26 */
    NOT(0x3b, COK_ECL_SPELL, 3, "dqk 6c70:260f"),
    NOT(0x3c, COK_ECL_STOCK_TREASURE, 1, "dqk 6c70:335e"),
    SAME(COK_ECL_CLEAR_BOX, 0),      /* 5f07:0a8d */
    NOT(0x3e, COK_ECL_DUMP, 0, "dqk 5f07:0c9b"),
    NOT(0x3f, COK_ECL_FIND_SPECIAL, 1, "dqk 5f07:0cc5"),
    NOT(0x40, COK_ECL_DESTROY_ITEMS, 1, "dqk 6c70:30ec"),
    NOT(0x41, COK_ECL_ADD_EP, 2, "dqk 6c70:2719"),
};

/* The variables by name: bytes of the 1 KB at far DS:b20f, 0x0001-0x0400
 * (6c70:0897, 06d2), as the code that reads them has them. */
#define VAR(a) {.address = a, .width = 1, .count = 1}
#define VARS(a, n) {.address = a, .width = 1, .count = n}
static const cok_rules_var dark_queen_vars[COK_VARS] = {
    [COK_VAR_MAP] = VAR(0x13),        /* 6c70:1dec */
    [COK_VAR_SPEED] = VAR(0x12),      /* 1a62:0311: the text's delay */
    [COK_VAR_BLOCK] = VAR(0x16),      /* 6c70:1d8d, 5f07:10cd */
    [COK_VAR_LAST_X] = VAR(0x17),     /* 5f07:10cd: after each step */
    [COK_VAR_LAST_Y] = VAR(0x18),
    [COK_VAR_EDGE] = VAR(0x21),       /* 5f07:0ff4 clears it as a block is entered */
    [COK_VAR_3D] = VAR(0x22),         /* 6c70:0728: a store switches the mode */
    [COK_VAR_REMAINDER] = VAR(0x29),  /* 5f07:02c2 */
    [COK_VAR_LOCALS] = VARS(0x97, 50), /* 6c70:0001 */
};

/* What a block's start sets besides (6c70:0001): 0x2b and 0x2c. */
static const struct cok_rules_address_value dark_queen_start[] = {{0x2b, 0}, {0x2c, 0}};

/* The special addresses: read through 6c70:08f3's table, stored through
 * 6c70:0728's, each store then kept in the variable too (6c70:0817). The
 * stores to 0x1a (the overhead map off) and 0x3a (the frame's colours,
 * read as they are drawn) keep the variable only. */
static const cok_rules_special dark_queen_reads[] = {
    {0x10, COK_ECL_SPECIAL_ZERO},  {0x11, COK_ECL_SPECIAL_STEPS}, {0x23, COK_ECL_SPECIAL_FACING},
    {0x2d, COK_ECL_SPECIAL_SQUARE}, {0x32, COK_ECL_SPECIAL_AHEAD}, {0x34, COK_ECL_SPECIAL_X},
    {0x35, COK_ECL_SPECIAL_Y},     {0x3d, COK_ECL_SPECIAL_HOST},
};
static const cok_rules_special dark_queen_stores[] = {
    {0x11, COK_ECL_SPECIAL_STEPS}, {0x34, COK_ECL_SPECIAL_X}, {0x35, COK_ECL_SPECIAL_Y},
};

const cok_rules cok_rules_dark_queen = {
    .id = "dark-queen",
    .assets = "DQKAssets",
    .unplayable = "The Dark Queen of Krynn is read headless only so far (DARK-QUEEN.md's step "
                  "1): its scripts, title and 3D view, with eclplay --game dark-queen",
    .drawing = COK_DRAWING_DARK_QUEEN,
    .text =
        {
            .name = "The Dark Queen of Krynn",
            .version = "The Dark Queen of Krynn", /* DS:4d73 */
            .version_again = "The Dark Queen of Krynn",
        },
    .archives =
        {
            .format = COK_ARCHIVE_HLIB,
            .folders = dark_queen_folders,
            .ecl = "ECL.GLB", /* 6c70:0118 */
            .geo = "GEO.GLB", /* 8a8b:12e3 */
            .title = "TITLE.TLB",
        },
    .files =
        {
            .config = "DQK.CFG",
            .item_types = "ITEMS.DAT",
        },
    .ecl =
        {
            .opcodes = dark_queen_opcodes,
            .count = COK_ECL_OPCODES,
            /* 5f07:0aba: 1 the view again if it changed (6c70:1685), 2 a
             * step (6c70:1186), 3 the picture's next frame (63ae:000c). */
            .calls = {{1, COK_ECL_CALL_REDRAW}, {2, COK_ECL_CALL_STEP}, {3, COK_ECL_CALL_ANIMATE}},
            /* 6c70:0377: the variables 0x0001-0x0400, the selected
             * character's fields 0x0401-0x0800 (6c70:040d, 1b5e: not read
             * yet, its record not ported), the block from 0x8000 (0x2ee3
             * bytes: 64b2:0131). */
            .area_first = 0x0001,
            .area_size = 0x400,
            .code_size = 0x2ee3,
            .block_header = 0,
            .name_address = 0x401, /* 6c70:0c18 */
            .special_stores_kept = true,
            .compare_and_unequal_strings = true, /* 5f07:05be */
            .variable_bytes = 1, /* 6c70:0897, 06d2 */
            .vars = dark_queen_vars,
            .start_stores = dark_queen_start,
            .start_store_count = sizeof dark_queen_start / sizeof *dark_queen_start,
            .reads = dark_queen_reads,
            .read_count = sizeof dark_queen_reads / sizeof *dark_queen_reads,
            .stores = dark_queen_stores,
            .store_count = sizeof dark_queen_stores / sizeof *dark_queen_stores,
            .delay_ms = 600, /* 5f07:0a26 */
        },
    .start =
        {
            .first_block = 4, /* 5f07:10cd: block 4 for a new game (the demonstration's 7) */
        },
};
