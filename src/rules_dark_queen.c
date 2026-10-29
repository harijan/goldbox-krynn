#include "ecl.h"
#include "rules.h"

/* The Dark Queen of Krynn (version 1.1 of July 29, 1992; "dqk" addresses
 * are build/DQK_FULL.EXE's, Ghidra's numbering of its map segment + 0x1000,
 * its data segment DS 0x422e). Read so far (DARK-QUEEN.md's step 1)
 * and its step 2's parts: its HLIB libraries, its pictures and
 * palettes, its font, its ECL and its 3D view, its window library's
 * menus, its title, main menu, camp menu and DQK.CFG, drawn by its own
 * routines (dark_queen.c); its characters, items and monsters. What it
 * has not or the port does not reach yet is left out (its data segment,
 * spells, combat), so that a read of it stops at its accessor's
 * assertion; its saved games load from its start menu's camp. */

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
    OP(0x0f, COK_ECL_INPUT_NUMBER, 2), /* 6c70:2e7c */
    OP(0x10, COK_ECL_INPUT_STRING, 2), /* 6c70:2ec8 */
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
    OP(0x38, COK_ECL_PROGRAM, 1),      /* 5f07:0baf: 1 the ending, the others stop */
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

/* The character record's fields (field.h), as rules.c's. */
#define BYTE(at) {.offset = at, .width = 1, .count = 1, .stride = 1, .rows = 1}
#define SBYTE(at) {.offset = at, .width = 1, .count = 1, .stride = 1, .rows = 1, .is_signed = true}
#define WORD(at) {.offset = at, .width = 2, .count = 1, .stride = 2, .rows = 1}
#define DWORD(at) {.offset = at, .width = 4, .count = 1, .stride = 4, .rows = 1}
#define POINTER(at) {.offset = at, .width = 4, .count = 1, .stride = 4, .rows = 1, .pointer = true}
#define BYTES(at, n, from)                                                                         \
    {.offset = at, .width = 1, .count = n, .first = from, .stride = 1, .rows = 1}
#define SBYTES(at, n, from)                                                                        \
    {.offset = at, .width = 1, .count = n, .first = from, .stride = 1, .rows = 1, .is_signed = true}
#define WORDS(at, n, from)                                                                         \
    {.offset = at, .width = 2, .count = n, .first = from, .stride = 2, .rows = 1}
#define INTERLEAVED(at, n, step) {.offset = at, .width = 1, .count = n, .stride = step, .rows = 1}
#define POINTERS(at, n)                                                                            \
    {.offset = at, .width = 4, .count = n, .stride = 4, .rows = 1, .pointer = true}

/* Dark Queen's record, 0x19e bytes in memory and in its files (.QCH:
 * 604b:05fd reads and 604b:2200 writes it whole; a saved game's,
 * 604b:28f8; the party a list of far pointers from DS:c6e5, next at +0,
 * the selected one at DS:ad67). Each field's place is where Death Knights'
 * import moves Death Knights' field to (604b:0d65, as cok_rules_import's
 * moves), and where the ECL reads and writes it (6c70:040d, 1b5e: below),
 * the stats (177e:0be2: the readied items' pointers at +0xc, the arrows'
 * at +0x38 and the quarrels' at +0x3c, the count +0xc9, hands +0xca, the
 * weight +0x56 with the coins, the damage from the base, the save bonus,
 * AC, movement and THAC0 from theirs) and the monsters' reader (604b:257c:
 * the pointers, the item count, the usable classes +0xbf from the levels)
 * have it. Its C rewrite holds an enum as an int: race, class, undead,
 * gender, alignment, status, hostile and deity are words (the ECL reads
 * them so), named here by their low byte, the high one 0 (the import's;
 * every file's). Dark Queen has no fighter level (as Death Knights), levels
 * shared out, shares, colours of a player's icon (the import drops Death
 * Knights' +0x74), experience a monster's hit points add (drops +0x6f) or
 * spells' block; it has the former levels (+0xac, which the import clears)
 * and the level a human changed class at (+0x92, which 86b8:354b and
 * 9f2e's level checks compare the levels with), and knows its spells as
 * bits (KNOWN_SPELL_BITS). No routine found reads +0x5e-+0x5f, +0x9c,
 * +0x9d, +0xc1, +0xc2 or +0x118-+0x156 (every file's 0; the ECL's
 * 0x0519-0x0557 reach the last). */
static const cok_character_layout dark_queen_character = {
    .size = 0x19e,
    .file_size = 0x19e,
    .c_name = true, /* 604b:0d65 */
    .field =
        {
            [COK_FIELD_NEXT] = POINTER(0x00),      /* 604b:3491 links the party by it */
            [COK_FIELD_EFFECTS] = POINTER(0x04),   /* 604b:05fd */
            [COK_FIELD_ITEMS] = POINTER(0x08),     /* 604b:05fd, 177e:0be2 */
            [COK_FIELD_ITEM_SLOTS] = POINTERS(0x0c, 13), /* 177e:0be2 clears 13 */
            [COK_FIELD_COMBAT] = POINTER(0x40),    /* 604b:05fd and 257c clear it */
            [COK_FIELD_EXPERIENCE] = DWORD(0x44),
            [COK_FIELD_UNDRAINED_EXPERIENCE] = DWORD(0x48),
            /* Steel, gems and jewelry, as Death Knights' (the ECL's
             * 0x054c-0x0551). */
            [COK_FIELD_COINS] = WORDS(0x4c, 3, 4),
            [COK_FIELD_AGE] = WORD(0x52),          /* the ECL's 0x04b1 */
            [COK_FIELD_MONSTER_EXPERIENCE] = WORD(0x54),
            [COK_FIELD_WEIGHT] = WORD(0x56),       /* 177e:0be2 */
            [COK_FIELD_RACE] = SBYTE(0x58),        /* an int: the ECL's 0x04ae */
            [COK_FIELD_CLASS] = SBYTE(0x5a),       /* an int: 0x04af */
            [COK_FIELD_UNDEAD] = SBYTE(0x5c),      /* an int */
            [COK_FIELD_GENDER] = SBYTE(0x60),      /* an int: 0x0567 */
            [COK_FIELD_ALIGNMENT] = SBYTE(0x62),   /* an int: 0x0568 */
            [COK_FIELD_STATUS] = SBYTE(0x64),      /* an int: 0x05ee */
            [COK_FIELD_HOSTILE] = BYTE(0x66),      /* an int: 0x05f0 */
            /* A C string: 15 characters and a 0 (604b:0d65; the ECL's
             * s[0401], 6c70:0a59). */
            [COK_FIELD_NAME] = BYTES(0x68, 16, 0),
            [COK_FIELD_SCORE_BASE] = INTERLEAVED(0x78, 6, 2), /* the ECL's 0x0411-0x041c */
            [COK_FIELD_SCORE] = INTERLEAVED(0x79, 6, 2),
            [COK_FIELD_STRENGTH_EXCEPTIONAL] = BYTE(0x84),
            [COK_FIELD_STRENGTH_EXCEPTIONAL_BASE] = BYTE(0x85),
            [COK_FIELD_STUDY_HOURS] = SBYTE(0x86),
            [COK_FIELD_BASE_THAC0] = BYTE(0x87),   /* 177e:0be2 */
            [COK_FIELD_KNIGHT_TIER] = BYTE(0x88),
            [COK_FIELD_MAX_HIT_POINTS] = BYTE(0x89), /* 0x04b3 */
            [COK_FIELD_SIZE] = BYTE(0x8a),
            [COK_FIELD_SAVING_THROW] = BYTES(0x8b, 5, 0),
            [COK_FIELD_BASE_MOVEMENT] = BYTE(0x90), /* 177e:0be2, the ECL's 0x0538 */
            [COK_FIELD_HIGHEST_LEVEL] = SBYTE(0x91),
            [COK_FIELD_CHANGE_LEVEL] = SBYTE(0x92), /* 86b8:354b */
            [COK_FIELD_THIEF_SKILL] = BYTES(0x93, 8, 1), /* 0x053c-0x0543 */
            [COK_FIELD_NPC] = BYTE(0x9b),          /* 604b:2886 sets 0x80 */
            /* Seven classes (the import moves Death Knights' eighth, class
             * 7, to 1): the levels before a drain, now, and before a human
             * changed class (the ECL's 0x0552-0x0558, 0x0559-0x055f). */
            [COK_FIELD_UNDRAINED_LEVEL] = SBYTES(0x9e, 7, 0),
            [COK_FIELD_CLASS_LEVEL] = SBYTES(0xa5, 7, 0),
            [COK_FIELD_FORMER_LEVEL] = SBYTES(0xac, 7, 0),
            [COK_FIELD_BASE_ATTACKS] = BYTES(0xb3, 2, 1),
            [COK_FIELD_BASE_DICE] = BYTES(0xb5, 2, 1), /* 177e:0be2 */
            [COK_FIELD_BASE_DICE_SIDES] = BYTES(0xb7, 2, 1),
            [COK_FIELD_BASE_DAMAGE_BONUS] = SBYTES(0xb9, 2, 1),
            [COK_FIELD_BASE_AC] = BYTE(0xbb),      /* 177e:0be2, the ECL's 0x0571 */
            [COK_FIELD_STRENGTH_BONUSES] = BYTE(0xbc),
            [COK_FIELD_MONSTER_ID] = BYTE(0xbd),   /* 604b:2886 */
            [COK_FIELD_UNDRAINED_MAX_HIT_POINTS] = BYTE(0xbe),
            [COK_FIELD_USABLE_CLASSES] = BYTE(0xbf), /* 604b:257c, from DS:63f3 */
            [COK_FIELD_BASE_HIT_POINTS] = BYTE(0xc0),
            [COK_FIELD_ICON_HEAD] = BYTE(0xc3),
            [COK_FIELD_ICON_BODY] = BYTE(0xc4),
            [COK_FIELD_ICON] = BYTE(0xc5),         /* 604b:3491: the party's slot 0-7 */
            [COK_FIELD_ICON_SIZE] = BYTE(0xc6),
            [COK_FIELD_CREATURE_FLAGS] = BYTES(0xc7, 2, 0),
            [COK_FIELD_ITEM_COUNT] = BYTE(0xc9),   /* 177e:0be2, 604b:257c */
            [COK_FIELD_HANDS] = BYTE(0xca),        /* 177e:0be2 */
            [COK_FIELD_SAVE_BONUS] = SBYTE(0xcb),  /* 177e:0be2 */
            [COK_FIELD_MAGIC_RESISTANCE] = BYTE(0xcc), /* 0x05ec */
            [COK_FIELD_MAY_TRAIN] = BYTE(0xcd),
            /* 74, as Death Knights' (604b:2021; the ECL's 0x041f on). */
            [COK_FIELD_MEMORIZED] = BYTES(0xce, 0x4a, 0),
            /* 128 bits, by id - 1 (604b:2021 with DS:68bc's 1 << n). */
            [COK_FIELD_KNOWN_SPELL_BITS] = BYTES(0x157, 16, 0),
            /* Four spell classes of nine levels from 1 (604b:2021 moves
             * Death Knights' eight of rows 0-3 to rows 0, 1, 3, 2). */
            [COK_FIELD_SPELLS_A_DAY] =
                {.offset = 0x167, .width = 1, .count = 9, .first = 1, .stride = 1, .rows = 4,
                 .row_stride = 9},
            [COK_FIELD_ACTIVE] = BYTE(0x18b),      /* 6c70:1b5e: status 0 */
            [COK_FIELD_COMPUTER] = BYTE(0x18c),
            [COK_FIELD_THAC0] = BYTE(0x18d),       /* 177e:0be2 */
            [COK_FIELD_AC] = BYTE(0x18e),
            [COK_FIELD_AC_BEHIND] = BYTE(0x18f),
            [COK_FIELD_ATTACKS_LEFT] = BYTES(0x190, 2, 1),
            [COK_FIELD_DICE] = BYTES(0x192, 2, 1), /* 177e:0be2: from +0xb5 */
            [COK_FIELD_DICE_SIDES] = BYTES(0x194, 2, 1),
            [COK_FIELD_DAMAGE_BONUS] = SBYTES(0x196, 2, 1),
            [COK_FIELD_HIT_POINTS] = BYTE(0x198),  /* 0x05fd */
            [COK_FIELD_MOVEMENT] = BYTE(0x199),    /* 177e:0be2 */
            [COK_FIELD_KNIGHT_ORDER] = BYTE(0x19a), /* 0x04c9 */
            [COK_FIELD_DEITY] = SBYTE(0x19b),      /* an int: 0x04ca */
            [COK_FIELD_MAGIC_ORDER] = BYTE(0x19d), /* 0x04cb */
        },
};
#undef BYTE
#undef SBYTE
#undef WORD
#undef DWORD
#undef POINTER
#undef BYTES
#undef SBYTES
#undef WORDS
#undef INTERLEAVED
#undef POINTERS

/* The selected character's fields at 0x0401-0x0800 (6c70:040d reads,
 * 6c70:1b5e writes, at 0x401 + the offsets here): 0x041f-0x04ab the
 * record's bytes +0xce-+0x15a (the memorized spells and on), others by its
 * table (6c70:045a, 58 entries) to a field each; others read 0 and a store
 * to them does nothing. A store goes in whole (6c70:06d2). */
#define FIELD(o, r, w, f, i) {.first = o, .last = o, .read = r, .write = w, .field = f, .index = i}
#define READ(o, k, f, i) FIELD(o, COK_ECL_FIELD_##k, COK_ECL_FIELD_NONE, f, i)
#define BOTH(o, k, f, i) FIELD(o, COK_ECL_FIELD_##k, COK_ECL_FIELD_##k, f, i)
static const cok_rules_character_var dark_queen_character_vars[] = {
    READ(0x010, U8, COK_FIELD_SCORE_BASE, 0), READ(0x011, U8, COK_FIELD_SCORE, 0),
    READ(0x012, U8, COK_FIELD_SCORE_BASE, 1), READ(0x013, U8, COK_FIELD_SCORE, 1),
    READ(0x014, U8, COK_FIELD_SCORE_BASE, 2), READ(0x015, U8, COK_FIELD_SCORE, 2),
    READ(0x016, U8, COK_FIELD_SCORE_BASE, 3), READ(0x017, U8, COK_FIELD_SCORE, 3),
    READ(0x018, U8, COK_FIELD_SCORE_BASE, 4), READ(0x019, U8, COK_FIELD_SCORE, 4),
    READ(0x01a, U8, COK_FIELD_SCORE_BASE, 5), READ(0x01b, U8, COK_FIELD_SCORE, 5),
    READ(0x01c, U8, COK_FIELD_STRENGTH_EXCEPTIONAL, 0),
    READ(0x01d, U8, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE, 0),
    /* +0xce on, past the memorized spells to +0x15a: a store of 0xff
     * stores 0, of 0 forgets a spell there (| 0x80). */
    {.first = 0x01e, .last = 0x0aa, .read = COK_ECL_FIELD_RAW, .write = COK_ECL_FIELD_FORGET,
     .field = COK_FIELD_MEMORIZED, .step = 1},
    READ(0x0ad, INT, COK_FIELD_RACE, 0),
    READ(0x0ae, INT, COK_FIELD_CLASS, 0),
    BOTH(0x0b0, U16, COK_FIELD_AGE, 0),
    READ(0x0b1, U16, COK_FIELD_AGE, 0),
    READ(0x0b2, U8, COK_FIELD_MAX_HIT_POINTS, 0),
    READ(0x0c8, U8, COK_FIELD_KNIGHT_ORDER, 0),
    READ(0x0c9, INT, COK_FIELD_DEITY, 0),
    READ(0x0ca, U8, COK_FIELD_MAGIC_ORDER, 0),
    BOTH(0x137, U8, COK_FIELD_BASE_MOVEMENT, 0),
    {.first = 0x13b, .last = 0x142, .read = COK_ECL_FIELD_U8, .field = COK_FIELD_THIEF_SKILL,
     .index = 1, .step = 1},
    /* Each coin at two addresses. */
    {.first = 0x14b, .last = 0x150, .read = COK_ECL_FIELD_U16, .write = COK_ECL_FIELD_U16,
     .field = COK_FIELD_COINS, .index = 4, .step = 2},
    {.first = 0x151, .last = 0x157, .read = COK_ECL_FIELD_U8, .field = COK_FIELD_CLASS_LEVEL,
     .step = 1},
    {.first = 0x158, .last = 0x15e, .read = COK_ECL_FIELD_U8, .field = COK_FIELD_FORMER_LEVEL,
     .step = 1},
    READ(0x166, INT, COK_FIELD_GENDER, 0),
    BOTH(0x167, INT, COK_FIELD_ALIGNMENT, 0),
    BOTH(0x170, U8, COK_FIELD_BASE_AC, 0),
    BOTH(0x1eb, U8, COK_FIELD_MAGIC_RESISTANCE, 0),
    FIELD(0x1ed, COK_ECL_FIELD_INT, COK_ECL_FIELD_STATUS_INT, COK_FIELD_STATUS, 0),
    BOTH(0x1ef, INT, COK_FIELD_HOSTILE, 0),
    BOTH(0x1fc, U8, COK_FIELD_HIT_POINTS, 0),
};
#undef FIELD
#undef READ
#undef BOTH

/* Death Knights' characters imported (604b:0b82, DS:b7ac 4: the .pc files
 * of the Death Knights save path DQK.CFG names, DS:ace2; 604b:2bcf a Death
 * Knights saved game's party, SAVGAM<letter>.PTY's CHRDAT<letter><n>.GAM,
 * the same way, its NPCs left out): 604b:0d65's moves, each Death Knights'
 * field to Dark Queen's (+0x6f, +0x74-+0x79, +0x7d-+0xb4, +0xb9-+0xc0,
 * +0xc2-+0xc5 and +0xd7 dropped); the name's text (+0x01) and a 0 after its
 * length, at most 15. */
static const cok_rules_move dark_queen_import_moves[] = {
    {0x068, 0x01, 15}, {0x078, 0x10, 12}, {0x084, 0x1c, 1}, {0x085, 0x1d, 1}, {0x086, 0x1e, 1},
    {0x087, 0x1f, 1}, {0x058, 0x20, 1}, {0x05a, 0x21, 1}, {0x19a, 0x22, 1}, {0x19b, 0x23, 1},
    {0x19d, 0x24, 1}, {0x088, 0x25, 1}, {0x052, 0x26, 2}, {0x089, 0x28, 1}, {0x08a, 0x29, 1},
    {0x08b, 0x2a, 5}, {0x090, 0x2f, 1}, {0x091, 0x30, 1}, {0x05c, 0x31, 1}, {0x093, 0x32, 8},
    {0x09b, 0x3e, 1}, {0x04c, 0x3f, 6}, {0x09e, 0x4d, 8}, {0x0a5, 0x45, 8}, {0x060, 0x55, 1},
    {0x062, 0x56, 1}, {0x0b3, 0x57, 2}, {0x0b5, 0x59, 2}, {0x0b7, 0x5b, 2}, {0x0b9, 0x5d, 2},
    {0x0bb, 0x5f, 1}, {0x0bc, 0x60, 1}, {0x0bd, 0x61, 1}, {0x044, 0x62, 4}, {0x048, 0x66, 4},
    {0x0be, 0x6a, 1}, {0x0bf, 0x6b, 1}, {0x0c0, 0x6c, 1}, {0x054, 0x6d, 2}, {0x0c3, 0x70, 1},
    {0x0c4, 0x71, 1}, {0x0c5, 0x72, 1}, {0x0c6, 0x73, 1}, {0x0c7, 0x7a, 1}, {0x0c8, 0x7b, 1},
    {0x0c9, 0x7c, 1}, {0x0ca, 0xb5, 1}, {0x0cb, 0xb6, 1}, {0x056, 0xb7, 2}, {0x0cc, 0xc1, 1},
    {0x0cd, 0xc6, 1}, {0x064, 0xc7, 1}, {0x18b, 0xc8, 1}, {0x066, 0xc9, 1}, {0x18c, 0xca, 1},
    {0x18d, 0xcb, 1}, {0x18e, 0xcc, 2}, {0x190, 0xce, 2}, {0x192, 0xd0, 2}, {0x194, 0xd2, 2},
    {0x196, 0xd4, 2}, {0x198, 0xd6, 1},
};
/* 604b:1ea3's 37 cases. */
static const uint8_t dark_queen_import_spells[][2] = {
    {0x02, 0x5a}, {0x04, 0x60}, {0x07, 0x69}, {0x11, 0x66}, {0x28, 0x38}, {0x35, 0x24},
    {0x38, 0x6e}, {0x39, 0x7f}, {0x3b, 0x35}, {0x3c, 0x6f}, {0x3d, 0x82}, {0x3e, 0x83},
    {0x3f, 0x70}, {0x40, 0x85}, {0x41, 0x86}, {0x42, 0x71}, {0x48, 0x72}, {0x5a, 0x77},
    {0x5f, 0x87}, {0x60, 0x84}, {0x61, 0x88}, {0x62, 0x76}, {0x63, 0x89}, {0x65, 0x39},
    {0x66, 0x3b}, {0x67, 0x3c}, {0x68, 0x3d}, {0x69, 0x3e}, {0x6a, 0x3f}, {0x6b, 0x40},
    {0x6c, 0x73}, {0x6d, 0x74}, {0x6e, 0x75}, {0x6f, 0x78}, {0x70, 0x7a}, {0x71, 0x79},
    {0x72, 0x7b},
};
/* 604b:1cc7's (2-0x79 by a jump table, others kept). */
static const uint8_t dark_queen_import_effects[][2] = {
    {0x02, 0x81}, {0x06, 0x82}, {0x07, 0x83}, {0x09, 0x4a}, {0x0d, 0x47}, {0x20, 0x45},
    {0x22, 0x20}, {0x28, 0x00}, {0x30, 0x0d}, {0x39, 0x76}, {0x3a, 0x7b}, {0x3b, 0x39},
    {0x3c, 0x7a}, {0x3d, 0x44}, {0x41, 0x06}, {0x42, 0x3c}, {0x43, 0x65}, {0x44, 0x66},
    {0x45, 0x84}, {0x46, 0x85}, {0x4a, 0x79}, {0x4b, 0x86}, {0x4c, 0x87}, {0x4d, 0x88},
    {0x4e, 0x00}, {0x4f, 0x89}, {0x50, 0x8a}, {0x51, 0x7c}, {0x55, 0x00}, {0x57, 0x4f},
    {0x5a, 0x00}, {0x5b, 0x8c}, {0x5d, 0x42}, {0x60, 0x8d}, {0x61, 0x8e}, {0x63, 0x8f},
    {0x64, 0x00}, {0x68, 0x00}, {0x6a, 0x00}, {0x6c, 0x00}, {0x72, 0xc8}, {0x73, 0x91},
    {0x74, 0x92}, {0x75, 0x93}, {0x76, 0x94}, {0x78, 0x00}, {0x79, 0x00},
};
/* 604b:1bf1's 20 cases. */
static const uint8_t dark_queen_import_words[][2] = {
    {0x0c, 0x8a}, {0x26, 0x8b}, {0x28, 0x8c}, {0x34, 0x8d}, {0x3b, 0x8e}, {0x43, 0x7e},
    {0x44, 0x8f}, {0x45, 0x7f}, {0x4a, 0x90}, {0x4b, 0x91}, {0x4c, 0x92}, {0x4d, 0x81},
    {0x54, 0x93}, {0x55, 0x94}, {0x56, 0x95}, {0x60, 0x96}, {0x69, 0x87}, {0x77, 0x97},
    {0x78, 0x98}, {0x7c, 0x99},
};
static const uint8_t dark_queen_scrolls[] = {0x27, 0x28, 0x49, 0xff}; /* 6fd8:43f7 */
static const cok_rules_import dark_queen_import = {
    .routine = COK_IMPORT_DARK_QUEEN,
    .from = &cok_rules_death_knights,
    .moves = dark_queen_import_moves,
    .move_count = sizeof dark_queen_import_moves / sizeof *dark_queen_import_moves,
    .spell_ids = dark_queen_import_spells,
    .spell_id_count = sizeof dark_queen_import_spells / sizeof *dark_queen_import_spells,
    .effect_ids = dark_queen_import_effects,
    .effect_id_count = sizeof dark_queen_import_effects / sizeof *dark_queen_import_effects,
    .word_ids = dark_queen_import_words,
    .word_id_count = sizeof dark_queen_import_words / sizeof *dark_queen_import_words,
    .scroll_types = dark_queen_scrolls,
    .top_levels = {10, 10, 10, 10, 11, 12, 11}, /* DS:640a */
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
    [COK_VAR_PARTY_SIZE] = VAR(0x20), /* 604b:3491 adds 1 */
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
    .unplayable = "The Dark Queen of Krynn's saved games load from its start menu "
                  "(--start-menu: Play, Load Saved Game); --load, --party, --test-party "
                  "and --play are not ported yet",
    .drawing = COK_DRAWING_DARK_QUEEN,
    .random = COK_RANDOM_QUEEN, /* 2102:0234 */
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
            .monster_library = "MONCHA.GLB", /* 604b:257c, DS:5ffd */
            .ecl = "ECL.GLB", /* 6c70:0118 */
            .geo = "GEO.GLB", /* 8a8b:12e3 */
            .title = "TITLE.TLB",
        },
    .files =
        {
            .config = "DQK.CFG", /* disk1\dqk.cfg, DS:608e: 6478:0000 */
            .config_bytes = 0x90,
            .config_folder = "DISK1",
            .item_types = "ITEMS.DAT", /* 128 types of 16 bytes into far DS:ae3e (64b2:0006) */
            .item_types_at = 0,
            .item_table = "ITEM.DAT", /* 254 entries into DS:c6de + 17 (64b2:066c) */
        },
    .saves =
        {
            /* SAVGAM<letter>.QSV (DS:6046 "SavGam", 603e "qsv": 604b:28f8
             * reads, 31a7 writes it, 0x1f1d bytes), and the characters'
             * .QCH, the roster's too (604b:054a lists *.qch in the save
             * path's SAVE, DS:5f56), each one file (604b:05fd);
             * VAULT<letter>.DAT (DS:6067: 63ee:0239 reads, 00df writes). */
            .game = "SAVGAM",
            .game_extension = "QSV",
            .game_size = 0x1f1d,
            .vault_items = 50, /* 63ee:00df pads to 50 items */
            .record = "QCH",
            .roster = "QCH",
            .character_files = COK_CHARACTER_ONE_FILE,
            .vault = "VAULT",
            .vault_extension = "DAT",
        },
    .character = &dark_queen_character,
    .characters =
        {
            .stats = COK_STATS_DARK_QUEEN, /* 177e:0be2 */
            .levels = COK_LEVELS_NONE, /* step 4 */
            .import = &dark_queen_import,
            .class_usable = {0x02, 0x10, 0x08, 0x40, 0x80, 0x01, 0x04}, /* DS:63f3 */
            .arrows = 0x1e, .quarrels = 0x0c, .quarrels_also = 0x4a, /* 177e:0be2 */
            .elf_first = 0x14, .elf_last = 0x1c, .elf_a = 0x13, .elf_b = 0x12, /* 177e:0016 */
            .kender = 0x43,
        },
    .item_record =
        {
            .file_size = 17, /* 604b:05fd */
            .file_at = 0x28,
            .type = 0x28,    /* 177e:0be2 */
            .count = 0x35,
            .weight = 0x2c, .readied = 0x32, .bonus = 0x30, .save_bonus = 0x31,
            .container = 0x49, /* 604b:05fd: a scroll's spells */
        },
    .ecl =
        {
            .opcodes = dark_queen_opcodes,
            .count = COK_ECL_OPCODES,
            /* 5f07:0aba: 1 the view again if it changed (6c70:1685), 2 a
             * step (6c70:1186), 3 the picture's next frame (63ae:000c). */
            .calls = {{1, COK_ECL_CALL_REDRAW}, {2, COK_ECL_CALL_STEP}, {3, COK_ECL_CALL_ANIMATE}},
            /* 6c70:0377: the variables 0x0001-0x0400, the selected
             * character's fields 0x0401-0x0800 (6c70:040d, 1b5e), the
             * block from 0x8000 (0x2ee3 bytes: 64b2:0131). */
            .area_first = 0x0001,
            .area_size = 0x400,
            .game_first = 0x401,
            .game_size = 0x400,
            .game_fields_only = true,
            .character = dark_queen_character_vars,
            .character_count = sizeof dark_queen_character_vars / sizeof *dark_queen_character_vars,
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
