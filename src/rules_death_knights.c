#include "ecl.h"
#include "rules.h"


/* Death Knights of Krynn (its START.EXE and GAME.OVR; "dkk" addresses are
 * build/death-START_FULL.EXE's, its data segment DS 0x1bce). Read
 * headless so far (DEATH-KNIGHTS.md's step 1): its files, its ECL's
 * opcodes, byte variables, named variables, special addresses and
 * character fields, its pictures and wall sets by id and its 3D view. What
 * step 1 does not reach is left out: the character record's layout but
 * the fields the ECL names, the data segment's tables but two, and the
 * variables only combat, treasure, monsters, shops and spells use, so that
 * a read of one stops at its accessor's assertion ("the game's records
 * have no such field", "... no such table", "... no such variable"), and
 * an archive the game does not have is NULL. */

/* The digit of the files whose names hold one: always 1, whatever the ECL
 * file (MON1, 46b2:198c; CPIC1, 7090:016d); the treasure's ITEM file
 * (step 3) is not read yet. */
static char death_knights_file_digit(unsigned file)
{
    (void)file;
    return '1';
}

/* The files a picture is read from by its id (dkk 6ca1:00a4, its sets of
 * ids for PIC4-7 and SPRIT4); others are the ECL file's. */
static const uint8_t death_knights_pic_files[256] = {
    [59] = 4, [60] = 4, [78] = 4,
    [5] = 5, [24] = 5, [77] = 5, [96] = 5,
    [16] = 6, [17] = 6, [19] = 6, [74] = 6, [80] = 6,
    [83] = 7,
};
static const uint8_t death_knights_sprite_files[256] = {
    [2] = 4, [64] = 4, [66] = 4, [70] = 4,
};

/* The file of a wall set's WALLDEF, its tile set's 8X8D (tile set 0's,
 * 70eb:002f) and its BACK, by the wall set's id (dkk 6d21:0ec5, the
 * strings at 6d21:0eb1-0eb9). */
static const uint8_t death_knights_wall_files[256] = {
    [1] = 1, [2] = 4, [3] = 2, [4] = 2, [5] = 3, [6] = 3, [7] = 5,
};

static const char *const death_knights_deltas[] = {"PIC1", "PIC2", "PIC3", "PIC4",
                                                   "PIC5", "PIC6", "PIC7", NULL};
static const char *const death_knights_collections[] = {"SPRIT1", "SPRIT2", "SPRIT3", "SPRIT4",
                                                        NULL};
static const char *const death_knights_singles[] = {
    "TITLE", "BIGPIC1", "BIGPIC2", "BIGPIC3", "CPIC1", "CBODY", "CHEAD", "COMSPR",
    "BACK1", "BACK2", "BACK3", "BACK4", "BACK5", "SKYGRND", "8X8D1", "8X8D2", "8X8D3",
    "8X8D4", "8X8D5", NULL};

/* The character record (216 bytes in its .GAM, step 2): only the fields
 * the ECL's table below names, at their offsets in the record (3c1e:073a,
 * 0a9a; the import 46b2:10d7 maps Champions' fields to them). The record's
 * size and its other fields are left out: size 0 keeps a monster's read
 * stopped (monster.c), and a field of width 0 stops at its accessor's
 * assertion. */
#define BYTE(at) {.offset = at, .width = 1, .count = 1, .stride = 1, .rows = 1}
#define SBYTE(at) {.offset = at, .width = 1, .count = 1, .stride = 1, .rows = 1, .is_signed = true}
#define WORD(at) {.offset = at, .width = 2, .count = 1, .stride = 2, .rows = 1}
#define BYTES(at, n, from)                                                                         \
    {.offset = at, .width = 1, .count = n, .first = from, .stride = 1, .rows = 1}
#define SBYTES(at, n, from)                                                                        \
    {.offset = at, .width = 1, .count = n, .first = from, .stride = 1, .rows = 1, .is_signed = true}
#define WORDS(at, n, from)                                                                         \
    {.offset = at, .width = 2, .count = n, .first = from, .stride = 2, .rows = 1}
#define INTERLEAVED(at, n, step) {.offset = at, .width = 1, .count = n, .stride = step, .rows = 1}
static const cok_character_layout death_knights_character = {
    .field =
        {
            [COK_FIELD_NAME] = BYTES(0x00, 16, 0),
            /* The scores, base then current, as Champions'. */
            [COK_FIELD_SCORE_BASE] = INTERLEAVED(0x10, 6, 2),
            [COK_FIELD_SCORE] = INTERLEAVED(0x11, 6, 2),
            [COK_FIELD_STRENGTH_EXCEPTIONAL] = BYTE(0x1c),
            [COK_FIELD_STRENGTH_EXCEPTIONAL_BASE] = BYTE(0x1d),
            [COK_FIELD_BASE_THAC0] = BYTE(0x1f),
            [COK_FIELD_RACE] = SBYTE(0x20),
            [COK_FIELD_KNIGHT_ORDER] = BYTE(0x22),
            [COK_FIELD_DEITY] = SBYTE(0x23),
            [COK_FIELD_MAGIC_ORDER] = BYTE(0x24),
            [COK_FIELD_AGE] = WORD(0x26),
            [COK_FIELD_MAX_HIT_POINTS] = BYTE(0x28),
            [COK_FIELD_SIZE] = BYTE(0x29),
            [COK_FIELD_SAVING_THROW] = BYTES(0x2a, 5, 0),
            [COK_FIELD_THIEF_SKILL] = BYTES(0x32, 8, 1),
            [COK_FIELD_NPC] = BYTE(0x3e),
            /* Steel, gems and jewelry only: Champions' kinds 4-6. */
            [COK_FIELD_COINS] = WORDS(0x3f, 3, 4),
            [COK_FIELD_CLASS_LEVEL] = SBYTES(0x45, 8, 0),
            [COK_FIELD_GENDER] = SBYTE(0x55),
            [COK_FIELD_ALIGNMENT] = SBYTE(0x56),
            [COK_FIELD_MAGIC_RESISTANCE] = BYTE(0xc1),
            [COK_FIELD_STATUS] = BYTE(0xc7),
            [COK_FIELD_ACTIVE] = BYTE(0xc8),
            [COK_FIELD_HOSTILE] = BYTE(0xc9),
            [COK_FIELD_COMPUTER] = BYTE(0xca),
            [COK_FIELD_THAC0] = BYTE(0xcb),
            [COK_FIELD_AC] = BYTE(0xcc),
            [COK_FIELD_AC_BEHIND] = BYTE(0xcd),
            [COK_FIELD_HIT_POINTS] = BYTE(0xd6),
            [COK_FIELD_MOVEMENT] = BYTE(0xd7),
        },
};
#undef BYTE
#undef SBYTE
#undef WORD
#undef BYTES
#undef SBYTES
#undef WORDS
#undef INTERLEAVED

/* The data segment's tables a headless run reads: an icon's recolouring
 * (DS:42de, 42ee, as Champions' DS:1e26, 1e36; dkk 6d21's icon loads).
 * The others (step 3) have width 0. */
static const uint8_t death_knights_same_colours[16] = {0, 1, 2,  3,  4,  5,  6,  7,
                                                       8, 9, 10, 11, 12, 13, 14, 15};
static const uint8_t death_knights_black_colours[16] = {0, 1, 2,  3,  4,  5,  6,  7,
                                                        0, 9, 10, 11, 12, 13, 14, 15};
static const cok_ds_table death_knights_tables[] = {
    {0x42de, sizeof death_knights_same_colours, 1, death_knights_same_colours},
    {0x42ee, sizeof death_knights_black_colours, 1, death_knights_black_colours},
};
#define LIST(b, w, f, n)                                                                           \
    {.base = b, .from = (b) + (f) * (w), .size = (n) * (w), .width = w, .indices = 1,              \
     .index = {{f, n, w}}}
static const cok_data_segment death_knights_data = {
    .tables = death_knights_tables,
    .table_count = sizeof death_knights_tables / sizeof *death_knights_tables,
    .table =
        {
            [COK_DATA_SAME_COLOURS] = LIST(0x42de, 1, 0, 16),
            [COK_DATA_BLACK_COLOURS] = LIST(0x42ee, 1, 0, 16),
        },
};

/* The ECL's opcodes (dkk 3632's handlers, the dispatch at 3632:2a54; the
 * IF's skips at 3c1e:1b48). The same as Champions' (rules.c) but for ten:
 * 0x0c loads 4 operands; 0x1d, 0x1f, 0x22, 0x23, 0x29, 0x2c, 0x31 and 0x3c
 * do other things (ecl.h's meanings past Champions'), 0x27 loads its items
 * as a list. 0x1f and PROTECTION's 0x3c advance. A failed IF skips 3 with
 * 0x0c (its handler loads 4), none with 0x1d, 0x22, 0x23, 0x27 and 0x29,
 * one with 0x1f and 0x2c. The handlers that keep Champions' meaning but
 * differ from Champions' where they need characters, monsters, combat or
 * the camp (LOAD MONSTER, COMBAT, DAMAGE, PROGRAM, ADD EP), and three new
 * ones, are not carried out yet: their entries cite them. SPELL (3632:2177)
 * differs only for each character it searches (the memorized spells in its
 * WIZ record, 74 of them, of those who can act): Champions' runs, and a
 * character stops it at the layout's MEMORIZED, which is left out.
 * DEVELOPERS.md's "Games" lists every handler's comparison. */
#define OP(o, m, n, k) [o] = {.meaning = m, .operands = n, .skip = k}
#define SAME(m, n, k) OP(m, m, n, k)
/* An opcode whose handler is not carried out yet: the run stops there. */
#define UNPORTED(o, m, n, k, r) [o] = {.meaning = m, .operands = n, .skip = k, .unported = "dkk " r}
#define LISTED(o, m, h, k) [o] = {.meaning = m, .operands = COK_ECL_LIST, .header = h, .skip = k}
static const cok_rules_opcode death_knights_opcodes[COK_ECL_OPCODES] = {
    SAME(COK_ECL_EXIT, 0, 0),
    SAME(COK_ECL_GOTO, 1, 1),
    SAME(COK_ECL_GOSUB, 1, 1),
    SAME(COK_ECL_COMPARE, 2, 2),
    SAME(COK_ECL_ADD, 3, 3),
    SAME(COK_ECL_SUBTRACT, 3, 3),
    SAME(COK_ECL_DIVIDE, 3, 3),
    SAME(COK_ECL_MULTIPLY, 3, 3),
    SAME(COK_ECL_RANDOM, 2, 2),
    SAME(COK_ECL_SAVE, 2, 2),
    SAME(COK_ECL_LOAD_CHARACTER, 1, 1),
    UNPORTED(COK_ECL_LOAD_MONSTER, COK_ECL_LOAD_MONSTER, 3, 3, "3632:0472"),
    UNPORTED(0x0c, COK_ECL_SETUP_MONSTERS, 4, 3, "3632:038f"),
    SAME(COK_ECL_APPROACH, 0, 0),
    SAME(COK_ECL_PICTURE, 1, 1),
    SAME(COK_ECL_INPUT_NUMBER, 2, 2),
    SAME(COK_ECL_INPUT_STRING, 2, 2),
    SAME(COK_ECL_PRINT, 1, 1),
    SAME(COK_ECL_PRINTCLEAR, 1, 1),
    SAME(COK_ECL_RETURN, 0, 0),
    SAME(COK_ECL_COMPARE_AND, 4, 4),
    LISTED(COK_ECL_VERTICAL_MENU, COK_ECL_VERTICAL_MENU, 3, 0),
    SAME(COK_ECL_IF_EQUAL, 0, 0),
    SAME(COK_ECL_IF_NOT_EQUAL, 0, 0),
    SAME(COK_ECL_IF_LESS, 0, 0),
    SAME(COK_ECL_IF_GREATER, 0, 0),
    SAME(COK_ECL_IF_LESS_EQUAL, 0, 0),
    SAME(COK_ECL_IF_GREATER_EQUAL, 0, 0),
    SAME(COK_ECL_CLEARMONSTERS, 0, 0),
    OP(0x1d, COK_ECL_PRESS_KEY, 0, 0), /* 3632:1b5f */
    SAME(COK_ECL_CHECKPARTY, 6, 6),
    OP(0x1f, COK_ECL_JOURNAL, 1, 1), /* 3632:279f */
    SAME(COK_ECL_NEWECL, 1, 1),
    SAME(COK_ECL_LOAD_FILES, 3, 3),
    OP(0x22, COK_ECL_YES_NO, 0, 0),     /* 3632:1b9e */
    OP(0x23, COK_ECL_CLEAR_EXIT, 0, 0), /* 3632:2859 */
    UNPORTED(COK_ECL_COMBAT, COK_ECL_COMBAT, 0, 0, "3632:16ec"),
    LISTED(COK_ECL_ON_GOTO, COK_ECL_ON_GOTO, 2, 0),
    LISTED(COK_ECL_ON_GOSUB, COK_ECL_ON_GOSUB, 2, 0),
    [0x27] = {.meaning = COK_ECL_TREASURE_ITEMS, .operands = COK_ECL_LIST, .header = 4,
              .unported = "dkk 3632:1933"},
    SAME(COK_ECL_ROB, 3, 3),
    OP(0x29, COK_ECL_NOTHING, 0, 0), /* 3632:28c9 */
    SAME(COK_ECL_GETTABLE, 3, 3),
    LISTED(COK_ECL_HORIZONTAL_MENU, COK_ECL_HORIZONTAL_MENU, 2, 0),
    OP(0x2c, COK_ECL_MUSIC, 1, 1), /* 3632:2878 */
    SAME(COK_ECL_CALL, 1, 1),
    UNPORTED(COK_ECL_DAMAGE, COK_ECL_DAMAGE, 5, 5, "3632:1ca2"),
    SAME(COK_ECL_AND, 3, 3),
    SAME(COK_ECL_OR, 3, 3),
    LISTED(0x31, COK_ECL_ASK_MENU, 2, 0), /* 3632:11fd with 1 */
    SAME(COK_ECL_FIND_ITEM, 1, 1),
    SAME(COK_ECL_PRINT_RETURN, 0, 0),
    SAME(COK_ECL_ECL_CLOCK, 2, 1),
    SAME(COK_ECL_SAVE_TABLE, 3, 3),
    SAME(COK_ECL_ADD_NPC, 2, 1),
    SAME(COK_ECL_LOAD_PIECES, 3, 3),
    UNPORTED(COK_ECL_PROGRAM, COK_ECL_PROGRAM, 1, 1, "3632:23dd"),
    SAME(COK_ECL_WHO, 1, 1),
    SAME(COK_ECL_DELAY, 0, 0),
    SAME(COK_ECL_SPELL, 3, 3), /* 3632:2177: per character, see below */
    UNPORTED(0x3c, COK_ECL_STOCK, 1, 1, "3632:28d2"),
    SAME(COK_ECL_CLEAR_BOX, 0, 0),
    SAME(COK_ECL_DUMP, 0, 0),
    SAME(COK_ECL_FIND_SPECIAL, 1, 1),
    SAME(COK_ECL_DESTROY_ITEMS, 1, 1),
    UNPORTED(COK_ECL_ADD_EP, COK_ECL_ADD_EP, 2, 2, "3632:25f1"),
};

/* The variables by name (ecl.h's cok_var): each a byte, at Champions'
 * address but where said, as the counterparts of the routines that read
 * and write them do (the clock 5b7d:0523, the moons 5b7d:0000, the view
 * 6c84:00a6, the block's start 3c1e:01c1); the 0x7c00 ones' addresses are
 * also its table at DS:836-864. Left out (code that reaches them stops at
 * an assertion): the turn of the status line's names, which its status
 * line (65cd:2eea) has not; the portrait, which it has not; the view's
 * own 0x4c38 test, which it has not; the moon of an
 * order and the area by a byte (the spells', R5); and those only combat,
 * treasure, monsters and shops read (step 2 and 3). */
#define VAR(a) {.address = a, .width = 1, .count = 1}
#define VARS(a, n) {.address = a, .width = 1, .count = n}
static const cok_rules_var death_knights_vars[COK_VARS] = {
    [COK_VAR_OVERLAND_X] = VAR(0x4bc3),
    [COK_VAR_OVERLAND_Y] = VAR(0x4bc4),
    [COK_VAR_MAP] = VAR(0x4bc5),
    [COK_VAR_CLOCK] = VARS(0x4bc6, COK_CLOCK_UNITS),
    [COK_VAR_NO_MAGIC] = VAR(0x4be5),
    [COK_VAR_3D] = VAR(0x4be6),
    [COK_VAR_PIECES] = VARS(0x4be7, 2),
    [COK_VAR_LAST_X] = VAR(0x4bf0),
    [COK_VAR_LAST_Y] = VAR(0x4bf1),
    [COK_VAR_BLOCK] = VAR(0x4bf2),
    [COK_VAR_HIDDEN] = VAR(0x4bfb),
    [COK_VAR_SPEED] = VAR(0x4bfc),
    /* Champions' two swapped (6c84:00a6). */
    [COK_VAR_SKY_LOW] = VAR(0x4bfe),
    [COK_VAR_SKY_HIGH] = VAR(0x4bfd),
    [COK_VAR_PICTURES] = VAR(0x4bff),
    [COK_VAR_LOCALS] = VARS(0x4c00, 0x20),
    /* Off the overland map unless 0x4cf7 is 1, where Champions' code
     * tests 0x4c38 (6c84:00a6, 3632:16ec, 237e, 23dd, 49cb:010b,
     * 65cd:07a5, 2db0). */
    [COK_VAR_OFF_MAP] = {.address = 0x4cf7, .width = 1, .count = 1, .off = 1},
    /* None: where Champions tests 0x4c38 alone (after camp, a mode-4
     * redraw), its code always draws the view (3632:237e, 65cd:2db0). */
    [COK_VAR_SAVES] = VAR(0x4cf6), /* 4381:3106 */
    [COK_VAR_DIFFICULTY] = VAR(0x4cf4),
    [COK_VAR_BANK] = VAR(0x4cf5),
    [COK_VAR_OVERLAND] = VAR(0x4cf7),
    [COK_VAR_TREASURE_FIRST] = VAR(0x4cf8),
    [COK_VAR_MOONS] = VARS(0x4cf9, 3),
    [COK_VAR_MOON_DAYS] = VARS(0x4cfc, 3),
    [COK_VAR_HALL] = VAR(0x7ea8),
    [COK_VAR_STAY] = VAR(0x7ec9),
    [COK_VAR_SEARCH] = VAR(0x7eca),
    [COK_VAR_REST_EVERY] = VAR(0x7ed2),
    [COK_VAR_REST_CHANCE] = VAR(0x7ed3),
    [COK_VAR_EDGE] = VAR(0x7ed5),
    [COK_VAR_ECL_FILE] = VAR(0x7f12),
    [COK_VAR_PARTY_SIZE] = VAR(0x7f3e),
    [COK_VAR_REMAINDER] = VAR(0x7f3f), /* 3632:014a */
    [COK_VAR_LOCALS_GAME] = VARS(0x7f79, 10),
};
#undef VAR
#undef VARS

/* A block's start (3c1e:01c1): no encounters while resting; no portrait
 * to forget, and the area's magic kept. */
static const struct cok_rules_var_value death_knights_start[] = {
    {COK_VAR_REST_EVERY, 0},
    {COK_VAR_REST_CHANCE, 0},
};

/* The special addresses (3c1e:0d85 reads; 3c1e:0bc1 stores, only above
 * 49000): 0x4cf8 always reads 1; no values of the host's at 0xb1, 0xfb,
 * 0xfc, 0xb8, 0xb9, 0x3de. */
static const cok_rules_special death_knights_reads[] = {
    {0x4bea, COK_ECL_SPECIAL_FACING}, {0x4cf8, COK_ECL_SPECIAL_ONE},
    {0x033d, COK_ECL_SPECIAL_FACING}, {0xc04b, COK_ECL_SPECIAL_X},
    {0xc04c, COK_ECL_SPECIAL_Y},      {0xc04d, COK_ECL_SPECIAL_STEPS},
    {0xc04e, COK_ECL_SPECIAL_AHEAD},  {0xc04f, COK_ECL_SPECIAL_SQUARE},
};
static const cok_rules_special death_knights_stores[] = {
    {0xc04b, COK_ECL_SPECIAL_X},    {0xc04c, COK_ECL_SPECIAL_Y},
    {0xc04d, COK_ECL_SPECIAL_STEPS}, {0xc059, COK_ECL_SPECIAL_C059},
    {0xc05f, COK_ECL_SPECIAL_C05F},
};

/* The selected character's fields at 0x7c00-0x7fff (3c1e:073a reads,
 * 3c1e:0a9a writes), in its record. The spells memorized (0x7c1b-0x7c64)
 * lie in the WIZ record the GAM record points to (+0xc2), not ported: with
 * a character they stop at the layout's assertion. */
#define FIELD(o, l, r, w, f, i, s)                                                                 \
    {.first = o, .last = l, .read = r, .write = w, .field = f, .index = i, .step = s}
#define READ(o, k, f, i) FIELD(o, o, COK_ECL_FIELD_##k, COK_ECL_FIELD_NONE, f, i, 1)
#define READS(o, l, k, f, i) FIELD(o, l, COK_ECL_FIELD_##k, COK_ECL_FIELD_NONE, f, i, 1)
#define SPECIAL(o, r, w) FIELD(o, o, COK_ECL_FIELD_##r, COK_ECL_FIELD_##w, 0, 0, 1)
static const cok_rules_character_var death_knights_character_vars[] = {
    /* The name's text, as many bytes as the size byte says (073a's Move). */
    READS(0x000, 0x00f, NAME_BYTES, COK_FIELD_NAME, 1),
    SPECIAL(0x000, NONE, CLEARED_ANY),
    READS(0x014, 0x019, U8, COK_FIELD_SCORE, 0),
    READ(0x01a, U8, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE, 0),
    FIELD(0x01b, 0x064, COK_ECL_FIELD_U8, COK_ECL_FIELD_U8, COK_FIELD_MEMORIZED, 0, 1),
    READS(0x065, 0x06a, U8, COK_FIELD_SCORE_BASE, 0),
    READ(0x06b, U8, COK_FIELD_STRENGTH_EXCEPTIONAL, 0),
    READ(0x071, U8, COK_FIELD_BASE_THAC0, 0),
    READ(0x072, S8, COK_FIELD_RACE, 0),
    READ(0x074, U16, COK_FIELD_AGE, 0),
    READ(0x076, U8, COK_FIELD_MAX_HIT_POINTS, 0),
    READS(0x09a, 0x09e, U8, COK_FIELD_SAVING_THROW, 0),
    READ(0x09f, U8, COK_FIELD_MOVEMENT, 0),
    READS(0x0a5, 0x0ac, U8, COK_FIELD_THIEF_SKILL, 1),
    FIELD(0x0b8, 0x0b8, COK_ECL_FIELD_U8, COK_ECL_FIELD_U8, COK_FIELD_NPC, 0, 1),
    READ(0x0bb, U8, COK_FIELD_MAGIC_ORDER, 0),
    READ(0x0bc, S8, COK_FIELD_DEITY, 0),
    READ(0x0bf, U8, COK_FIELD_KNIGHT_ORDER, 0),
    /* Each coin's word at two addresses, the second its own too. */
    FIELD(0x0c3, 0x0c8, COK_ECL_FIELD_U16, COK_ECL_FIELD_U16, COK_FIELD_COINS, 4, 2),
    READ(0x0d3, U8, COK_FIELD_MAGIC_RESISTANCE, 0),
    READ(0x0d6, S8, COK_FIELD_GENDER, 0),
    READ(0x0d8, S8, COK_FIELD_ALIGNMENT, 0),
    SPECIAL(0x0eb, CLASSES, NONE),
    SPECIAL(0x100, ACTIVE, REVIVE),
    SPECIAL(0x10c, NONE, SIDE),
    READ(0x10e, U8, COK_FIELD_THAC0, 0),
    READ(0x10f, U8, COK_FIELD_AC, 0),
    READ(0x110, U8, COK_FIELD_AC_BEHIND, 0),
    FIELD(0x119, 0x119, COK_ECL_FIELD_U8, COK_ECL_FIELD_U8, COK_FIELD_HIT_POINTS, 0, 1),
    READ(0x11a, U8, COK_FIELD_MOVEMENT, 0),
    SPECIAL(0x2b1, HOST, NONE),
    SPECIAL(0x2b4, HOST, NONE),
    SPECIAL(0x312, NONE, FILE),
};
#undef FIELD
#undef READ
#undef READS
#undef SPECIAL

const cok_rules cok_rules_death_knights = {
    .id = "death-knights",
    .assets = "DKKAssets",
    .text =
        {
            .name = "Death Knights of Krynn",
            /* The title's menu is not read yet (step 2); its version is
             * GAME.OVR's string. */
            .version = "Death Knights Of Krynn 1.10 ",
            .version_again = "Death Knights Of Krynn 1.10 ",
        },
    .archives =
        {
            .ecl = "ECL",
            .geo = "GEO",
            .walldef = "WALLDEF",
            .tiles = "8X8D",
            .font = {"8X8D", 1, 201},
            .pic = "PIC",
            .sprite = "SPRIT",
            .big = "BIGPIC",
            .head = NULL, /* no HEAD or BODY: portraits are not read yet */
            .body = NULL,
            .icons = "CPIC",
            .missiles = "COMSPR",
            .icon_head = "CHEAD",
            .icon_body = "CBODY",
            .sky = NULL,
            .ground = "SKYGRND", /* 4f9a:00e1 */
            .backdrop = "BACK",  /* 6d21:0e4e */
            .cursor = {"8X8D", 1, 204}, /* 4f9a:00e1 */
            .title = "TITLE",
            .treasure = "ITEM",
            .monster = "MON",
            .monster_record = "CHA",
            .monster_effects = "SPC",
            .monster_items = "ITM",
            .dungeon = "DUNGCOM",
            .wilderness = "WILDCOM",
            .random = NULL,
            /* MUSIC.DAX's driver and songs (step 4): silent until then. */
            .music_drivers = NULL,
            .music_score = NULL,
            .file_digit = death_knights_file_digit,
            .pic_files = death_knights_pic_files,
            .sprite_files = death_knights_sprite_files,
            .wall_files = death_knights_wall_files,
        },
    .files =
        {
            .config = "DKK.CFG",
            .item_types = "ITEMS",
            .install_dir = "C:\\DKK\\",
            .save_dir = "SAVE",
        },
    .saves =
        {
            /* As its files are named (step 2 reads them). */
            .game = "SAVGAM",
            .game_extension = "PTY",
            .character = "CHRDAT",
            .record = "GAM",
            .roster = "PC",
            .items = "JNK",
            .effects = "EFF",
            .debug = NULL, /* no debug mode */
        },
    .images =
        {
            .delta = death_knights_deltas,
            .collection = death_knights_collections,
            .single = death_knights_singles,
            .frames_ff = 256, /* 123c:1c58 */
        },
    .character = &death_knights_character,
    .data = &death_knights_data,
    .ecl =
        {
            .opcodes = death_knights_opcodes,
            .count = COK_ECL_OPCODES,
            /* 3632:2294: Champions' but the redraw's, 3c1e:1dd9 at 2dcb. */
            .calls = {{0x2dcb, COK_ECL_CALL_REDRAW},
                      {0xb203, COK_ECL_CALL_SOUND},
                      {0xc01e, COK_ECL_CALL_STEP},
                      {0xc018, COK_ECL_CALL_AHEAD},
                      {0x6803, COK_ECL_CALL_ANIMATE}},
            .variable_bytes = 1, /* 3c1e:0d85, 3c1e:0bc1 */
            .vars = death_knights_vars,
            .start = death_knights_start,
            .start_count = sizeof death_knights_start / sizeof *death_knights_start,
            .reads = death_knights_reads,
            .read_count = sizeof death_knights_reads / sizeof *death_knights_reads,
            .stores = death_knights_stores,
            .store_count = sizeof death_knights_stores / sizeof *death_knights_stores,
            .character = death_knights_character_vars,
            .character_count =
                sizeof death_knights_character_vars / sizeof *death_knights_character_vars,
            /* 073a's 0x7ceb, bit by bit: the levels of classes 5, 0, 6,
             * 2, 7, none, 3 and 4 (+0x4a, 45, 4b, 47, 4c, 48, 49). */
            .class_bits = {5, 0, 6, 2, 7, 0xff, 3, 4},
            .compare_and_strings = true, /* 3632:0bed */
            .delay_ms = 600,             /* 3632:1c90 */
            .menu_single_item = false,   /* 3632:11fd */
        },
    .view =
        {
            /* DS:43fa and DS:4404 (70eb:01f5): 1-0xff all from tile set 0,
             * the frame's from 0x100; past 0x12c nothing is drawn. */
            .tile_first = {0x00, 0x2e, 0x74, 0xba, 0x100},
            .tile_last = {0xff, 0x73, 0xb9, 0xff, 0x12c},
            .first_slot = 0,      /* 3632:0daa */
            .shared_tiles = true, /* 6d21:0ec5 */
            .move_tiles = false,  /* 6d21:0ec5 computes the offset but adds nothing */
            .overhead_square = 0x119, /* 6d21:0000 */
            .overhead_party = 0x129,
            .overland_picture = 0x70, /* 3632:096c */
        },
    .startup =
        {
            /* 4f9a:00e1: the frame's tile set only. */
            .tiles = {{4, 202}},
            .tile_count = 1,
            /* 4f9a:00e1 loads COMSPR 0-21 into slots 13-34; the port's
             * icon slots end at 25 until combat is read (step 3). */
            .icons = {{0, 13, 13}},
            .icon_ranges = 1,
            /* Loaded without a mask (DS:42d0's 0), its colour 13 drawn. */
            .cursor_transparent = -1,
        },
    .unplayable = "Death Knights of Krynn is not playable yet: eclplay --game death-knights "
                  "shows its scripts and 3D view (see DEATH-KNIGHTS.md)",
};
