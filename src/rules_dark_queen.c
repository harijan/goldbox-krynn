#include "ecl.h"
#include "data_dark_queen.h"
#include "rules.h"
#include "rules_dark_queen_magic.h"

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

/* The icon and missile files have no digit (CPIC.TLB and COMSPR.TLB on DISK2,
 * CBODY.TLB on DISK3): the name is the file's. */
static char dark_queen_file_digit(unsigned file)
{
    (void)file;
    return 0;
}

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
    SAME(COK_ECL_LOAD_MONSTER, 3),   /* 6c70:29a3: registers (monster.c) */
    OP(0x0c, COK_ECL_SETUP_MONSTERS, 4), /* 6c70:2db1: monster.c, the sprite (dark_queen.c) */
    OP(0x0d, COK_ECL_APPROACH, 0),       /* 6c70:2e4f */
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
    SAME(COK_ECL_COMBAT, 0),         /* 5f07:081b: its monsters' appends (monster.c) */
    LISTED(COK_ECL_ON_GOTO, 2),      /* 5f07:0991 */
    LISTED(COK_ECL_ON_GOSUB, 2),
    [0x27] = {.meaning = COK_ECL_TREASURE_ITEMS, .operands = COK_ECL_LIST, .header = 4,
              .skip_list = true}, /* 6c70:2127: the coins (dark_queen.c's treasure_items) */
    OP(0x28, COK_ECL_ROB, 3),        /* 6c70:28f8 */
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
    OP(0x32, COK_ECL_FIND_ITEM, 1),  /* 6c70:2f3b */
    SAME(COK_ECL_PRINT_RETURN, 0),   /* 5f07:0a68 */
    OP(0x34, COK_ECL_ECL_CLOCK, 2),  /* 5f07:0a35 */
    SAME(COK_ECL_SAVE_TABLE, 3),
    NOT(0x36, COK_ECL_ADD_NPC, 2, "dqk 6c70:307b"),
    OP(0x37, COK_ECL_FIND_NPC, 2),   /* 6c70:31e8 */
    OP(0x38, COK_ECL_PROGRAM, 1),      /* 5f07:0baf: dark_queen.c's program() */
    SAME(COK_ECL_WHO, 1),            /* 6c70:2fe0 */
    SAME(COK_ECL_DELAY, 0),          /* 5f07:0a26 */
    NOT(0x3b, COK_ECL_SPELL, 3, "dqk 6c70:260f"),
    NOT(0x3c, COK_ECL_STOCK_TREASURE, 1, "dqk 6c70:335e"),
    SAME(COK_ECL_CLEAR_BOX, 0),      /* 5f07:0a8d */
    OP(0x3e, COK_ECL_DUMP, 0),       /* 5f07:0c9b */
    OP(0x3f, COK_ECL_FIND_SPECIAL, 1), /* 5f07:0cc5, the handler itself */
    OP(0x40, COK_ECL_DESTROY_ITEMS, 1), /* 6c70:30ec */
    OP(0x41, COK_ECL_ADD_EP, 2),     /* 6c70:2719 */
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
};

/* 6c70:170e's extra monsters by var 0x3a: the MONCHA id (DS:75aa[2 * var])
 * and the icon (DS:75b8[2 * var]), words of which the low byte is read; 32
 * pairs, read from the original's data (the high bytes are 0). */
static const uint8_t dark_queen_extra_monsters[][2] = {
    {0x14, 0x14}, {0x14, 0x14}, {0x2a, 0x2a}, {0x11, 0x11}, {0x3d, 0x3d}, {0x15, 0x15},
    {0x07, 0x07}, {0x14, 0x01}, {0x14, 0x20}, {0x2a, 0x26}, {0x11, 0x2e}, {0x3d, 0x36},
    {0x15, 0x3e}, {0x07, 0x43}, {0x01, 0x48}, {0x20, 0x4d}, {0x26, 0x1f}, {0x2e, 0x25},
    {0x36, 0x2d}, {0x3e, 0x35}, {0x43, 0x3d}, {0x48, 0x42}, {0x4d, 0x47}, {0x1f, 0x4c},
    {0x25, 0x79}, {0x2d, 0x01}, {0x35, 0x02}, {0x3d, 0x03}, {0x42, 0x04}, {0x47, 0x05},
    {0x4c, 0x06}, {0x79, 0x07},
};

/* The variables by name: bytes of the 1 KB at far DS:b20f, 0x0001-0x0400
 * (6c70:0897, 06d2), as the code that reads them has them. */
#define VAR(a) {.address = a, .width = 1, .count = 1}
#define VARS(a, n) {.address = a, .width = 1, .count = n}
static const cok_rules_var dark_queen_vars[COK_VARS] = {
    [COK_VAR_MAP] = VAR(0x13),        /* 6c70:1dec */
    [COK_VAR_DIFFICULTY] = VAR(0x27), /* 6c70:29fc: the monsters' hit points */
    [COK_VAR_SPEED] = VAR(0x12),      /* 1a62:0311: the text's delay */
    [COK_VAR_BLOCK] = VAR(0x16),      /* 6c70:1d8d, 5f07:10cd */
    [COK_VAR_LAST_X] = VAR(0x17),     /* 5f07:10cd: after each step */
    [COK_VAR_LAST_Y] = VAR(0x18),
    [COK_VAR_EDGE] = VAR(0x21),       /* 5f07:0ff4 clears it as a block is entered */
    [COK_VAR_3D] = VAR(0x22),         /* 6c70:0728: a store switches the mode */
    [COK_VAR_DISTANCE_WANTED] = VAR(0x38), /* 6c70:2db1: the distance wanted */
    [COK_VAR_DISTANCE] = VAR(0x37),   /* 6c70:01c3: the distance now (2e0a-2e18) */
    [COK_VAR_REMAINDER] = VAR(0x29),  /* 5f07:02c2 */
    [COK_VAR_LOCALS] = VARS(0x97, 50), /* 6c70:0001 */
    [COK_VAR_PARTY_SIZE] = VAR(0x20), /* 604b:3491 adds 1 */
    [COK_VAR_FLED] = VAR(0x36),       /* 59c7:1e3e clears it, 52db:2aef sets it */
    [COK_VAR_RECORD_EFFECT] = VAR(0x3c), /* 59c7:0f7b:10cf reads it (not named elsewhere yet) */
    [COK_VAR_MOVE_BONUS] = VAR(0x1c),    /* 177e:0a5f: the party's movement's bonus */
    [COK_VAR_SURPRISE] = VAR(0x2e),   /* 52db:0004 (initiative: a bit per side) and 5045:059d's
                                       * reset (DS:b20f+0x2e) */
    [COK_VAR_CLOCK] = VARS(0x0005, COK_CLOCK_UNITS), /* 1396:044d */
    [COK_VAR_MOONS] = VARS(0x01fa, 3), /* 1396:000e: the phases */
    [COK_VAR_MOON_DAYS] = VARS(0x01fd, 3), /* and the days in each */
    [COK_VAR_RIVER] = VAR(0x33),      /* 59c7:0860: a river on the open ground */
    [COK_VAR_ABYSS] = VAR(0x1a2),     /* 59c7:0e3b: the open ground's flags index */
    [COK_VAR_SEARCH] = VAR(0x19),     /* 5f07:0937 keeps bit 0 */
    [COK_VAR_VAULT] = VAR(0x31),      /* 5f07:084b: COMBAT opens the vault */
    [COK_VAR_TEMPLE] = VAR(0x14),     /* 5f07:0861: COMBAT opens the temple */
    [COK_VAR_NO_MAGIC] = VAR(0x02),   /* dqk 5045:0cd7's Cast: the area forbids casting */
    [COK_VAR_HIT_PARTY] = VAR(0x2f),    /* 148f:1771: the party's to-hit bonus */
    [COK_VAR_HIT_MONSTERS] = VAR(0x01), /* 148f:1771: the monsters' */
    [COK_VAR_MORALE] = VAR(0x1b),       /* 5bb8:1117: 100 less it is the morale's need, a byte */
    [COK_VAR_TURNING] = VAR(0x0f),      /* 52db:1117: the turning undead's level less, a byte */
};

/* The effects that bar a free attack's blow (52db:08e6's tests on each neighbour: 0xd2, 0x6a,
 * 0x1f, 0x33, 0x34, 0x35, 0x37, 0x76, 0x82, 0xd5), 0-ended. */
static const uint8_t dark_queen_free_barred[] = {0xd2, 0x6a, 0x1f, 0x33, 0x34, 0x35,
                                                 0x37, 0x76, 0x82, 0xd5, 0x00};

/* The readied weapon's item types that allow a backstab (dqk 52db:278f, at the record's type,
 * +0x28), 0-ended: 3, 4, 0x11-0x13, 0x3f and 0x43. */
static const uint8_t dark_queen_backstab_weapons[] = {3, 4, 0x11, 0x12, 0x13, 0x3f, 0x43, 0};

/* The item types drawn as arrows (dqk 52db:28cc's switch: 4, 5, 7, 0x0c, 0x1e, 0x42, 0x4a),
 * 0-ended. */
static const uint8_t dark_queen_arrows[] = {4, 5, 7, 0x0c, 0x1e, 0x42, 0x4a, 0};

/* The cells a body is not laid over (dqk 5837:0d55's 0x1d, 0x1c and 0x46, the cloud and the pool
 * and acid), 0-ended, and the cells 5837:137a looks through for the cell under a body (0x1d, 0x1c,
 * 0x1b and 0x46). */
static const uint8_t dark_queen_body_spared[] = {0x1d, 0x1c, 0x46, 0x00};
static const uint8_t dark_queen_under_tiles[] = {0x1d, 0x1c, 0x1b, 0x46, 0x00};

/* Dark Queen's combat block (59c7:0f7b; rules.h): its named bytes at 0x00 (spell), 0x0a, 0x14
 * and 0x17 (the others unnamed: read as 0), effect 0xd5 facing 2 (0f7b:0ff6), and effect 0x95
 * (148f:19c2) given to a record that is not hostile while var 0x3c is set (0f7b:10cf). */
static const cok_combat_block dark_queen_combat_block = {
    .spell = 0x00,
    .may_cast = 0x01,
    .may_use = 0x02,
    .initiative = 0x03,
    .initiative_high = 0x04,
    .attack_slot = 0x05,
    .movement = 0x07,
    .attacked = 0x09,
    .facing = 0x0a,
    .not_party = 0x14,
    .in_battle = 0x17,
    .guarding = 0x08,
    .dying = 0x0f,
    .hits = 0x10,
    .forced = 0x11,
    .turns = 0x12,
    .turning = 0x13,
    .facing_effect = 0xd5,
    .gate_effect = 0x95,
    .gate_var = COK_VAR_RECORD_EFFECT,
    .pattern = 0x16,
    .target = 0x0b,
    .fleeing = 0x15,
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
    .unplayable = "The Dark Queen of Krynn's --party and --test-party are not ported yet "
                  "(--load and --play are; its saved games also load from --start-menu)",
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
            /* The monsters' icons, CPIC (204c:00df, the string DS:778c), the
             * missiles' COMSPR, as the library files (their extension is the
             * port's, as dark_queen.c's); the party's head and body names are
             * not in Dark Queen's data (CHEAD, CBODY), named for the loader's
             * sake. */
            .icons = "CPIC.TLB",
            .missiles = "COMSPR.TLB",
            .icon_head = "CHEAD",
            .icon_body = "CBODY",
            .file_digit = dark_queen_file_digit,
        },
    /* The icon slots: LOAD MONSTER takes 8 to 13 (it stops at 0x0e, dqk
     * 6c70:29ac), and 170e's extra monsters take the next, 14 (6c70:17db, 1a3b):
     * six loads and COMBAT use up to 14. The port stops at the first battle, so
     * no later COMBAT (without a CLEARMONSTERS) takes more; the missiles' slots
     * are not traced yet. */
    .startup =
        {
            .icon_slots = 15,
            .icons_unrecoloured = true,
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
    .data = &cok_dark_queen_data,
    .items =
        {
            .destroy_magic = true,      /* 6c70:30ec: 0x80 on a type, bonus items only */
            .destroy_lost_only = true,  /* 6c70:31af: the stats of the members that lost */
            .rob_integer = true,        /* 6c70:124f */
            .unready = COK_UNREADY_QUEEN, /* 86b8:1965 */
        },
    .character = &dark_queen_character,
    .characters =
        {
            .stats = COK_STATS_DARK_QUEEN, /* 177e:0be2 */
            .levels = COK_LEVELS_DARK_QUEEN, /* 8bcc:07f1 */
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
            .cursed = 0x34, /* 5bb8:1271 (queen_ai.c) */
            .container = 0x49, /* 604b:05fd: a scroll's spells */
            .effect = 0x37,    /* 52db:18d6's spiritual hammer test (1b26) */
            .link = 0,         /* none: 52db:1b56 (see cok_rules_item_record) */
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
            .flags_cleared = 4,                  /* 5f07:05c4, 6c70:003b */
            .variable_bytes = 1, /* 6c70:0897, 06d2 */
            .vars = dark_queen_vars,
            .start_stores = dark_queen_start,
            .start_store_count = sizeof dark_queen_start / sizeof *dark_queen_start,
            .reads = dark_queen_reads,
            .read_count = sizeof dark_queen_reads / sizeof *dark_queen_reads,
            .stores = dark_queen_stores,
            .store_count = sizeof dark_queen_stores / sizeof *dark_queen_stores,
            .delay_ms = 600, /* 5f07:0a26 */
            /* 6c70:2fe0: 24de compares case-blind, 2543 is equal. */
            .who_prompt = "who will attempt to disarm the trap?",
            .who_prompt_instead = "Who will try to disarm?", /* DS:7889 */
        },
    .shops =
        {
            .routine = COK_SHOPS_DARK_QUEEN, /* the temple (dqk 7825:0af6, dark_queen.c) */
        },
    .start =
        {
            .first_block = 4, /* 5f07:10cd: block 4 for a new game (the demonstration's 7) */
        },
    /* The pick (WHO's, 76b8:3ae5) also moves on End and Home (its table at 3c12). */
    .menus =
        {
            .pick_end_home = true,
        },
    /* The words of ADD EP (6c70:2719, DS:7813, 783f: Champions' texts) in colour 7 (its
     * push 7), and its routine. */
    .treasure =
        {
            .gains = "Congratulations ",
            .gains_end = " gains experience!",
            .party_gains = "Congratulations the party gains experience!",
            .gains_active_first = true,
            .gains_colour = 7,
            .gains_routine = COK_GAINS_DARK_QUEEN,
        },
    /* The spells (magic.c, cast.c) and effects (effect.c): the names, the
     * headings and the handlers by id, all in rules_dark_queen_magic.c. The
     * records (DS:27e9, 22 bytes a spell) are data_dark_queen.c's. The
     * spells' list rows, targets and classes, and the effects' sharing and
     * display, are not read yet (their parts), so they stay zero or NULL. */
    .spells =
        {
            .count = 0x8d, /* ids 1-0x8c; 0x41, 0x5f, 0x61, 0x63 are never filled */
            .names = cok_dark_queen_spell_names,
            .names_at = 0x25c7, /* far pointers, 4 bytes a spell (7fd8:xxxx pushes them) */
            .name_size = 4,
            .levels = cok_dark_queen_spell_levels,
            .level_count = 10,
            .levels_at = 0x8b2e, /* 41 bytes a heading (the lists' 'Special' to '9th Level') */
            .level_size = 41,
            .cast = cok_dark_queen_spell_cast,
            .ids = cok_dark_queen_spell_ids,
        },
    /* COMBAT's monsters (monster.c): LOAD MONSTER registers them (dqk 6c70:29a3),
     * COMBAT appends them (6c70:2d82, 170e) and lowers the distance to the open
     * squares; the battle (5045:059d) starts (its setup, cok_combat_setup, builds
     * the battlefield: 59c7:1dd7's tiles and builders), makes its combat records
     * (59c7:0f7b, the combat block), places them (59c7:1864), and the view's origin
     * (59c7:1dd7), redraws the arena (51c2:0c94: the icons are not drawn yet), and
     * sets each record's effect 8 (1e99's tail). The round's start (5045:059d's loop: the
     * sides, 52db:0004 and the initiative, 5045:0447's order) is made, and the battle
     * stops at the first turn's side panel (177e:085f, parts 11-12; the cursor's box, 5837:00f4,
     * is drawn before it: 5045:0827, 5837:12f5) before the turn's dispatch (5045:078b). With no monsters, COMBAT's
     * vault (5f07:084b, 771f:09a0) and treasure (75cc:135c) stop, cited; the temple (7825:0af6)
     * runs (dark_queen.c). The encounter (SETUP MONSTERS, APPROACH, the sprite and close-up) is
     * ported. */
    .combat =
        {
            .begins = "A battle begins...", /* DS:5ed8 (1a62:0046) */
            .begins_colour = 7,             /* its push in 5f07:081b */
            .monsters_at_combat = true,
            /* No prelude: 5045:059d and 59c7:1dd7 free no picture, clear no row, wait
             * and print no "A battle begins..." (queen_monsters printed it). */
            .no_setup_prelude = true,
            /* Leadership (dqk 5045:059d -> 52db:4dd2, before 1dd7): no announcement;
             * the leader's class level 1 (177e:2234(r, 1)); an NPC's flag 0x80 at
             * +0x18c; the chance is not kept from one knight to the next (4d50's frame). */
            .leadership = true,
            .leadership_text = NULL,
            .leadership_level = 1,
            .leadership_computer = 0x80,
            .leadership_chance_kept = false,
            .monster_records_max = 0x28,    /* 29fc's cap (dqk 6c70:2a10) */
            .monster_records_extra = 0x32,  /* 170e's cap (dqk 6c70:1a31) */
            /* The distance (dqk 6c70:01c3, 2db1): with no 3D area the open squares
             * set var 0x38, the distance wanted, and a wanted 16 is 2 squares. */
            .open_squares_wanted = true,
            .distance_16_is_2 = true,
            /* 59c7:16b3's loop is a local: a record keeps its own side. */
            .layout_sets_side = false,
            /* The placement's distance where it is 0 and the facing is not the party's
             * (dqk 59c7:16b3's flag: var 0x37 when it is set, else 1). */
            .ambush_distance = 1,
            .extra_monsters = dark_queen_extra_monsters,
            .extra_monster_count = (uint8_t)(sizeof dark_queen_extra_monsters /
                                             sizeof *dark_queen_extra_monsters),
            .extra_monster_var = 0x3a,
            .extra_monsters_appended = 10, /* 170e's movb $0xa */
            /* The dungeon (59c7:06a7): the map filled with 0x16 first, the
             * floor, and its loop draws no tables and chairs. The corners:
             * 0285 (north-west) has no door as a corner, 049e (north-east)
             * does (see rules.h). */
            .dungeon_fill = true,
            .dungeon_floor = 0x16,
            .dungeon_no_tables = true,
            .door_corner_nw = false,
            .door_corner_ne = true,
            /* The vault's menu (5f07:084b, var 0x31) comes first, then the temple (var 0x14),
             * then the treasure; there is no shop branch (its shops open from STOCK). */
            .vault = true,
            .vault_unported = "dqk 771f:09a0 (the vault)",
            /* 75cc:135c, the treasure branch with no monsters (dark_queen.c). */
            .no_monsters_treasure = COK_NO_MONSTERS_TREASURE_DARK_QUEEN,
            /* The mode after COMBAT (5f07:0917) and the overland's mark (5f07:0964). */
            .overland_var = 0x24,
            .overland_mark_unported = "dqk 7065:0011 (the party's place on the overland)",
            .load_asked = COK_LOAD_ASKED_DARK_QUEEN,
            .temple_var_after = true, /* 5f07:0861-0871 clears var 0x14 after the temple's call */
            /* The open ground (59c7:0e3b: floor, river, trees, scatter, the
             * flags at var 0x1a2, the Abyss's state; see COK_WILDERNESS_DARK_QUEEN). */
            .wilderness = COK_WILDERNESS_DARK_QUEEN,
            /* The placement (59c7:1864, its own routine). */
            .placement = COK_PLACEMENT_DARK_QUEEN,
            /* 1dd7's tail (59c7:1e99): effect 8 only, the turning byte, Move mode off and the
             * cursors kept; the enemy health leaves out status 9 (52db:29f1). */
            .setup_effect_8_only = true,
            .setup_clears_turning = true,
            .setup_keeps_aim = true,
            .movement_from_base = true,
            .may_cast_needs_spell = true,
            .may_use_needs_no_cloud = true,
            .ammunition_gated = true,
            .ammunition_also = 0x0c,
            /* 51c2:0b26 (the distance) puts back only 4 bytes of the listing it saved. */
            .distance_kept_bytes = 4,
            .enemy_health_skip_status_9 = true,
            .sides_skip_status_9 = true,
            .sides_ambush_0x20 = true,
            /* After each turn (5045:0726): a side gone or the round limit ends the round, the
             * first time in a battle (ae4c, b45a, the [bp-6] flag), as DKK's ends_in_round. */
            .ends_in_round = true,
            /* Each turn (5045:078b): the computer's turn stops at 5bb8:0000, the party's at the
             * command menu (5045:08cf) for a record that can act, or at 849a:145b for one with a
             * spell pending (cast; not ported). A record that cannot act is ended at 08cf's
             * 51c2:0c11, which is cok_combat_end_turn. */
            /* The menu, verified by disassembly (part 22a): 5045:08cf's loop (0925) runs 0cd7,
             * which builds the menu at the combatant's cell (1bdb:0005 per item, the window
             * library's 1595 shows it: not ported) and gives the result (0-12, Move..End of
             * DS:50b3). The result jumps by 0925's table: Move 0b8b, Aim 0976, Use 099d, Cast
             * 09b1, Turn 09c7, Guard 0a19 (51c2:0c52, Guarding at DS:5a2a; reads +0x11, not
             * yet known for Dark Queen), Quick 0a2c, Delay 0a60 (initiative 1, the turn list
             * shifted: the shared delay), Bandage 0adb, Yell 0ae9, View 0af8, Speed 0b22
             * (166c), End 0b29 (51c2:0c11 = cok_combat_end_turn). With DS:c2f5 set the result
             * is a key code instead, matched by 0b3c's table: 0x20, 0x81-0x88, 0xc0, 0xcc, 0xd0,
             * 0xd7. Each item's stop, and the loop's redraw (12f5, 177e:085f), are cited. */
            .menu_first = true,
            /* The computer's turn is 5bb8:0000 (queen_ai.c's queen_computer, part 15a): nothing
             * stops at it now, so computer_unported (the stop the part-15 port had) is NULL; a
             * stop inside it cites its own step. Quick's hand-on (queen_commands.c) cites its own. */
            .computer_unported = NULL,
            .spell_unported = "dqk 849a:145b, a pending spell's cast (not ported)",
            /* After each turn (5045:059d's tail, round.c): the pending lists (148f:2ab8, 2c4f,
             * 2d9b; DS:bd4c, ad6e, ad6f: the counts stop it, cited), the demonstration's key test
             * (5045:06cf-070e, cited) and the colour cycles (261b:05be, the port's palette). */
            .pending_unported = "dqk 148f:2ab8, 2c4f, 2d9b (a pending list: 0x9a, 0xbc, 0xbd, part 39)",
            .demo_key_unported = "dqk 5045:06cf-070e (the demonstration's key test)",
            /* The round's end (5045:0f6c): the clock 1396:044d(1, 1) (dark_queen.c's pass_time),
             * the clouds 148f:2817 and the square test 148f:12ab (its cloud and terrain stops:
             * part 39), "Your Teammate is Dying" (177e:153c) and the Continue question
             * (1bdb:0424). */
            .int_status_hostile = true,
            .round_clouds_unported = "dqk 148f:2817 (the round's clouds, part 39)",
            .square_unported = "dqk 148f:12ab (a cloud or a pool's terrain at the square, part 39)",
            /* The battle's end (5045:0001): the clouds (round_clouds_unported), each record's
             * effects 0xb0 and 0xd0 (148f:017d, part 27b), the icons (204c:004d). The aging
             * report (5045:012f) is the battle's before it (rounds()). */
            .battle_end_unported = "dqk 148f:017d (an effect's end, part 27b)",
            .battle_end_effects = {0xb0, 0xd0},
            .aging_unported = "dqk 5045:012f (the aging report, part 27e)",
            .combat_block = &dark_queen_combat_block,
        },
    .commands =
        {
            /* COK_COMMANDS_DARK_QUEEN: the command menu (queen_commands.c, dqk 5045:08cf). */
            .routine = COK_COMMANDS_DARK_QUEEN,
            /* The cursor's box (5837:00f4:018b-01ae: COMSPR 15, slot 0x1e, loaded by arena_tiles).
             * The missiles' read of it (command.c, 6346:1a26) is reached only in a round's turn,
             * which stops before the first turn (round.c). */
            .cursor_slot = 0x1e,
        },
    .fight =
        {
            /* Bandaging (dqk 5045:0adb, 51c2:0d63's say at 177e:156e): the only fight word
             * Dark Queen's combat says so far (DS:5a33). The computer's: one in panic (5bb8:0000,
             * DS:5dde), Magic On and Off (5bb8:1050, DS:5e26, 5e2f) and Guarding (5bb8:0fec,
             * 51c2:0c52, DS:5a2a), by the bar message (177e:153c). */
            .words =
                {
                    .bandaged = "is bandaged",
                    /* The attack's message (52db:02c0's words, scope 2.7: 5af9-5b95; 14b ports the
                     * report's layout). */
                    .attacks = "Attacks",
                    .backstabs = "-Backstabs-",
                    .slays = "slays helpless",
                    .behind = "(from behind) ",
                    .cruel = "with one cruel blow",
                    .hitting = "Hitting for ",
                    .point = " point ",
                    .points = " points ",
                    .damage = "of damage",
                    .misses = "and Misses",
                    .lost_spell = "lost a spell",
                    .goes_down = "goes down",
                    .dying = "and is Dying",
                    .killed = "is killed",
                    .panic = "flees in panic",
                    .surrenders = "Surrenders",
                    .magic_on = "Magic On",
                    .magic_off = "Magic Off",
                    .guarding = "Guarding",
                    /* Fleeing (52db:0e26): away (DS:5bac), or blocked (DS:5bb5). */
                    .got_away = "Got Away",
                    .blocked = "Escape is blocked",
                    /* Turning undead (52db:1117): one destroyed, and none turned (DS:5be1, 5bee). */
                    .destroyed = "Is destroyed",
                    .nothing = "Nothing Happens...",
                },
            /* 5bb8:06b0's "Move/Attack, Move Left = N" (5ded, 5e0a): 7, which 1a62:0046 makes
             * white on the bar's grey. */
            .moves_left_colour = 7,
            .moves_left_bare_from_ten = true,
            /* 5bb8:06b0's step: the turn added to the way from the party (1), and the ways of
             * moving read only for a try below 6 (5bb8:0902's table, DS:5db4). Its sound is 0x0b
             * (52db:06c0), and a class 5 holds without a look at slot 2 (06b0). */
            .flee_turn = 1,
            .ways_guarded = true,
            .step_sound = 0x0b,
            .step_no_slot_test = true,
            /* 5bb8:0438 (queen_can_step) and the no-move tail's spell choice, 5bb8:0306's prefix
             * (queen_spell_choice). */
            .can_step = COK_CAN_STEP_DARK_QUEEN,
            .no_move = COK_NO_MOVE_DARK_QUEEN,
            /* 52db:0e26: the ambush byte at 0x20 blocks a flight. */
            .flee_blocked_by_ambush = true,
            /* 52db:08e6: a free attack is denied by effect 0x1b, and barred for the blow by the
             * effects in dark_queen_free_barred. */
            .free_attack_effect = 0x1b,
            .free_attack_barred = dark_queen_free_barred,
            /* 52db:1030: an enemy of the attacker's side is not a target (its hostile bytes). */
            .attack_sides = true,
            /* 5bb8:1050: the keys read through the Dark Queen's reader, not flushed after. */
            .keys = COK_KEYS_DARK_QUEEN,
            .keys_unflushed = true,
            /* 5bb8:0000 is queen_computer (queen_ai.c): its undead limit (0x0d, the search 52db:135e
             * and the turning 52db:1117, both in queen_ai.c) is not set here (turn_limit stays 0);
             * its paladin levels above 2 turn undead (5bb8:0268, read by queen_computer). Morale
             * (5bb8:1117): a signed 100 less var 0x1b, the roll not capped at 0x66, and no effects
             * removed on a flight (none for Dark Queen). The surrender is 148f:1a69 (part 16). */
            .computer_routine = COK_COMPUTER_DARK_QUEEN,
            .paladin_turns = 2,
            .morale_signed = true,
            .morale_uncapped = true,
            .flee_removes = false,
            .surrender_unported = "dqk 148f:1a69 (a computer's surrender: part 16)",
            /* 52db:3d9e: tries the number listed, after one draw unused. */
            .pick_tries_listed = true,
            /* 148f:1771: the party's var 0x2f and the monsters' 0x01, the difficulty's adjustment
             * for the party. */
            .hit_bonus = COK_HIT_BONUS_DARK_QUEEN,
            /* 52db:1417: sound 8 on a hit, 0x0a on a miss (only with no item). */
            .hit_sound = 8,
            .miss_sound = 0x0a,
            .miss_sound_no_item = true,
            /* 52db:01dd, 1417, 02c0: the damage is a signed word (DS:ae55). */
            .damage_word = true,
            /* 52db:278f: a thief by level or effect 0xbe; the weapon types above; 52db:01dd. */
            .backstab = COK_BACKSTAB_DARK_QUEEN,
            .backstab_weapons = dark_queen_backstab_weapons,
            .backstab_effect = 0xbe,
            /* 177e:1346: the 0x89 memorized bytes from +0xce, marked 0x80. */
            .forget_bytes = 0x89,
            .forget_mark = 0x80,
            /* 52db:1417's 0xb8 test: a holder gets event slot + 1 on a hit with no damage. */
            .hit_event_effect = 0xb8,
            /* 52db:1417 clears no sweeps. */
            .strike_keeps_sweeps = true,
            /* 1a62:0046's grey (7) for "and is Dying". */
            .dying_colour = 7,
            /* 52db:0529-0532: testb $8, +0xc8; jne 0x58f. One with bit 8 of its creature flags goes
             * down only: no "and is Dying", no "is killed". */
            .quiet_flag = 8,
            /* 5837:0d55 (the death on the battlefield, part 16): the body search is the bodies 1-8
             * (not a stale byte); the death sounds 6; the body is not laid over 0x1d, 0x1c or 0x46,
             * and a record at status 8 leaves none; at status 7 the body's cell is a stone, 0x1e;
             * 137a looks through 0x1d, 0x1c, 0x1b and 0x46 for the cell under a body; the spell is
             * kept. */
            .kill_finds_body = true,
            .kill_clears_spell = false,
            .kill_sound = 6,
            .body_spared = dark_queen_body_spared,
            .kill_no_body_status = 8,
            .kill_stone_status = 7,
            .kill_stone_tile = 0x1e,
            .under_tiles = dark_queen_under_tiles,
            /* 52db:2888: the arrows (0x0f, odd 0x10), spinning 0x12, sling stones 0x16, others
             * 0x15 (no thrown: 0x36 and 0x37 are others); the sounds 0x0d, 0x0a, 7, 0x0a; no
             * leading sound. Its flight, 51c2:00df: the ends kept on the battlefield, the frames
             * drawn by the drawing's hooks, and the wait 2 * delay (part 17b). */
            .arrows = dark_queen_arrows,
            .missile = {.arrow = {0x0f, 0x0d}, .spin = {0x12, 0x0a}, .stone = {0x16, 7},
                        .other = {0x15, 0x0a}},
            .missile_clamped = true,      /* 51c2:00df's clamp of its ends (00e8-0143) */
            .missile_wait_twice = true,   /* 51c2:00df's 1df9:052a(delay * 2) */
            /* 177e:13d2 (the removal of a strike's item, 52db:18d6's use-up): writes through NULL
             * for an item not in the list. */
            .freed_remove_unported =
                "dqk 177e:13d2 writes through NULL for an item not in the list",
            /* 52db:54b5 lists the enemies with no yell swap. */
            .sweep_no_yell = true,
        },
    .effects =
        {
            .count = 0xe6, /* ids 0-0xe5; 0x80, 0x90 and 0x9d are never filled */
            .handlers = cok_dark_queen_effect_handlers,
            /* 148f:0512's lists (events 1-0x18), 0369's shared ids (Prayer in 6 squares),
             * 02ed's pending list, 2a6d's immunity (the record's +0xc8), 0369's 0xd4 (the
             * selected record), the stat recompute (1dfc), and 19c2's pool of 400 nodes (DS:bd51). */
            .events = cok_dark_queen_event_lists,
            .event_count = 0x19,
            .shared = cok_dark_queen_shared,
            .shared_far_id = 0x31,
            .shared_far = 6,
            .resisted_pending = cok_dark_queen_resisted_pending,
            .resist_unported = "dqk 177e:1900 (Dark Queen's caster level, not ported)",
            .unaffected = "is Unaffected", /* DS:01b5, 148f:2561 */
            .damage_flags_at = 0xc8,
            .acting_id = 0xd4,
            .ability_routine = COK_ABILITY_DARK_QUEEN, /* 148f:1dfc */
            .ability_second_strength_id = 0x71,
            .ability_low_id = 0x44,
            .constitution_id = 0x3e,
            .heal_guard_id = 0xd7, /* 148f:25f7 */
            .pool_size = 400,
            .pool_unported = "dqk 19c2 links NULL and writes 0000:0000",
            .ids = cok_dark_queen_effect_ids, /* rules_dark_queen_magic.c */
        },
};
