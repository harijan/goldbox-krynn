#include "data_death_knights.h"
#include "ecl.h"
#include "rules.h"


/* Death Knights of Krynn (its START.EXE and GAME.OVR; "dkk" addresses are
 * build/death-START_FULL.EXE's, its data segment DS 0x1bce). Read
 * headless so far (DEATH-KNIGHTS.md's steps 1 and 2a): its files, its
 * ECL's opcodes, byte variables, named variables, special addresses and
 * character fields, its pictures and wall sets by id and its 3D view; its
 * characters' and monsters' records, their files, their recompute and the
 * import of Champions' characters. What it has not or the port does not
 * reach yet is left out: the record's fields it has not, the data
 * segment's tables but two, and the variables only combat, treasure,
 * monsters, shops and spells use, so that a read of one stops at its
 * accessor's assertion ("the game's records have no such field", "... no
 * such table", "... no such variable"), and an archive the game does not
 * have is NULL. */

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

/* The character record: 216 bytes, its .GAM (and roster .PC) file and its
 * monsters' MON1CHA records (46b2:0abc writes, 46b2:10d7 and 46b2:19a2
 * read 0xd8), with the spells in a 220-byte block of their own (.WIZ,
 * MON1WIZ: 0xdc), which the far pointer +0xc2 points to and the port holds
 * after the record, at 0xd8. Each field's place is where Champions' import
 * (46b2:10d7) moves Champions' field to and where the routines matching
 * Champions' read it (the offsets their code has where Champions' has the
 * field's, cited where one stands for all); the derived fields as 65cd:0e46
 * (Champions' 6346:0d20) and 69a8:050b (66c2:0433) set them. A field it has
 * not is left out: no fighter level (+0xce: 65cd:0e46 sets none), class
 * changed at (+0xd7), levels shared out (+0xd8, +0xd9), shares (+0xe8:
 * the import drops it and no routine reads one) or former levels (+0x101:
 * the import moves them to +0x4d, which holds the levels before a drain).
 * No two fields share a byte; +0xc6 is the import's 0 and no routine's. */
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
/* The spells' block: its fields at the block's offset in the record. */
#define SPELLS_AT 0xd8
static const cok_character_layout death_knights_character = {
    .size = SPELLS_AT + 0xdc,
    .file_size = SPELLS_AT, /* 46b2:0abc writes, 46b2:10d7 reads 0xd8 */
    .spells_offset = SPELLS_AT,
    .spells_size = 0xdc, /* 46b2:10d7 reads, 46b2:0abc writes 0xdc of +0xc2 */
    .field =
        {
            /* A Pascal string[15], as Champions' (the import's first move,
             * 0x1e bytes from +0x00, holds it and the scores). */
            [COK_FIELD_NAME] = BYTES(0x00, 16, 0),
            /* The scores, base then current, as Champions' (65cd:1525,
             * identical to 6346:137a). */
            [COK_FIELD_SCORE_BASE] = INTERLEAVED(0x10, 6, 2),
            [COK_FIELD_SCORE] = INTERLEAVED(0x11, 6, 2),
            [COK_FIELD_STRENGTH_EXCEPTIONAL] = BYTE(0x1c),
            [COK_FIELD_STRENGTH_EXCEPTIONAL_BASE] = BYTE(0x1d),
            /* The spells memorized: 74 in the spells' block (4381:0e9b's
             * +0xc2; the ECL's 0x7c1b-0x7c64, 3c1e:073a). */
            [COK_FIELD_MEMORIZED] = BYTES(SPELLS_AT + 0x00, 0x4a, 0),
            /* Champions' +0x58-+0x62 (0xb bytes) move to +0x1e (46b2:10d7):
             * the hours (4381:0556), base THAC0 (69a8:050b), race, class,
             * knight order, deity, order of magic, knight tier (504d:26f8),
             * age (5b7d:042e) and maximum hit points. */
            [COK_FIELD_STUDY_HOURS] = SBYTE(0x1e),
            [COK_FIELD_BASE_THAC0] = BYTE(0x1f),
            [COK_FIELD_RACE] = SBYTE(0x20),
            [COK_FIELD_CLASS] = SBYTE(0x21),
            [COK_FIELD_KNIGHT_ORDER] = BYTE(0x22),
            [COK_FIELD_DEITY] = SBYTE(0x23),
            [COK_FIELD_MAGIC_ORDER] = BYTE(0x24),
            [COK_FIELD_KNIGHT_TIER] = BYTE(0x25),
            [COK_FIELD_AGE] = WORD(0x26),
            [COK_FIELD_MAX_HIT_POINTS] = BYTE(0x28),
            /* Known spells by id 1-0x72 in the block, +0x49 + id (69a8:0000);
             * Champions' 107 move there. */
            [COK_FIELD_KNOWN_SPELL] = BYTES(SPELLS_AT + 0x4a, 0x72, 1),
            /* Champions' +0xcf-+0xd6 move to +0x29: the size (410b:1aa4),
             * saving throws (69a8:08ff), base movement (65cd:0246) and
             * highest level (69a8:050b). */
            [COK_FIELD_SIZE] = BYTE(0x29),
            [COK_FIELD_SAVING_THROW] = BYTES(0x2a, 5, 0),
            [COK_FIELD_BASE_MOVEMENT] = BYTE(0x2f),
            [COK_FIELD_HIGHEST_LEVEL] = SBYTE(0x30),
            /* Champions' +0xda-+0xe5 move to +0x31: the undead's kind
             * (5340:119b), the thief skills (69a8:0b3d), the effects'
             * pointer (46b2:10d7 zeroes +0x3a-+0x3d). */
            [COK_FIELD_UNDEAD] = SBYTE(0x31),
            [COK_FIELD_THIEF_SKILL] = BYTES(0x32, 8, 1),
            [COK_FIELD_EFFECTS] = POINTER(0x3a),
            /* ADD NPC's (3632:210f), 46b2:1d90's 0x80 and up; the import
             * leaves it 0. */
            [COK_FIELD_NPC] = BYTE(0x3e),
            /* Steel, gems and jewelry only: Champions' kinds 4-6, +0x37 +
             * 2 * kind (65cd:0e46); the import moves Champions' +0xf3-+0x119
             * (0x27 bytes) to +0x3f: these, the class levels, the levels
             * before a drain, gender to experience. */
            [COK_FIELD_COINS] = WORDS(0x3f, 3, 4),
            [COK_FIELD_CLASS_LEVEL] = SBYTES(0x45, 8, 0),
            /* The levels a drain took (504d:14c7 keeps the highest; Restoration's
             * test, 3999:045f, compares them with the levels now), and the
             * experience and maximum hit points before it (+0x66, +0x6a). */
            [COK_FIELD_UNDRAINED_LEVEL] = SBYTES(0x4d, 8, 0),
            [COK_FIELD_GENDER] = SBYTE(0x55),
            [COK_FIELD_ALIGNMENT] = SBYTE(0x56),
            /* The two attacks' base (69a8:050b, 65cd:0e46). */
            [COK_FIELD_BASE_ATTACKS] = BYTES(0x57, 2, 1),
            [COK_FIELD_BASE_DICE] = BYTES(0x59, 2, 1),
            [COK_FIELD_BASE_DICE_SIDES] = BYTES(0x5b, 2, 1),
            [COK_FIELD_BASE_DAMAGE_BONUS] = SBYTES(0x5d, 2, 1),
            [COK_FIELD_BASE_AC] = BYTE(0x5f),
            [COK_FIELD_STRENGTH_BONUSES] = BYTE(0x60), /* 65cd:15bd */
            [COK_FIELD_MONSTER_ID] = BYTE(0x61),       /* 46b2:1d02 */
            [COK_FIELD_EXPERIENCE] = DWORD(0x62),
            [COK_FIELD_UNDRAINED_EXPERIENCE] = DWORD(0x66),
            [COK_FIELD_UNDRAINED_MAX_HIT_POINTS] = BYTE(0x6a),
            /* Champions' +0x11a-+0x11b move to +0x6b (69a8:050b; 638c:16d8's
             * base hit points). */
            [COK_FIELD_USABLE_CLASSES] = BYTE(0x6b),
            [COK_FIELD_BASE_HIT_POINTS] = BYTE(0x6c),
            /* Spells a day in the block: 4 spell classes of 8 levels from 1,
             * +0xbb + 8 * class + level (69a8:0000). */
            [COK_FIELD_SPELLS_A_DAY] =
                {.offset = SPELLS_AT + 0xbc, .width = 1, .count = 8, .first = 1, .stride = 1,
                 .rows = 4, .row_stride = 8},
            /* Champions' +0x130-+0x132 move to +0x6d (3a37:0000). */
            [COK_FIELD_MONSTER_EXPERIENCE] = WORD(0x6d),
            [COK_FIELD_MONSTER_HIT_POINT_EXPERIENCE] = BYTE(0x6f),
            /* Champions' +0x135-+0x140 move to +0x70: the icon (46b2:078f,
             * 49cb:3cac), its slot (46b2:1d90), the creature flags
             * (504d:0900). */
            [COK_FIELD_ICON_HEAD] = BYTE(0x70),
            [COK_FIELD_ICON_BODY] = BYTE(0x71),
            [COK_FIELD_ICON] = BYTE(0x72),
            [COK_FIELD_ICON_SIZE] = BYTE(0x73),
            [COK_FIELD_ICON_COLOURS] = BYTES(0x74, 6, 1),
            [COK_FIELD_CREATURE_FLAGS] = BYTES(0x7a, 2, 0),
            /* Champions' +0x142-+0x187 move to +0x7c: the items (65cd:0e46),
             * the list's next (46b2:1d90) and combat record (410b:119e)
             * pointers, magic resistance (638c:04c9). */
            [COK_FIELD_ITEM_COUNT] = BYTE(0x7c),
            [COK_FIELD_ITEMS] = POINTER(0x7d),
            [COK_FIELD_ITEM_SLOTS] = POINTERS(0x81, 13),
            [COK_FIELD_HANDS] = BYTE(0xb5),
            [COK_FIELD_SAVE_BONUS] = SBYTE(0xb6),
            [COK_FIELD_WEIGHT] = WORD(0xb7),
            [COK_FIELD_NEXT] = POINTER(0xb9),
            [COK_FIELD_COMBAT] = POINTER(0xbd),
            [COK_FIELD_MAGIC_RESISTANCE] = BYTE(0xc1),
            /* The spells' block, nil for a fighter or thief (46b2:10d7). */
            [COK_FIELD_SPELLS] = POINTER(0xc2),
            /* Champions' +0x188-+0x198 move to +0xc7 (0x11 bytes). */
            [COK_FIELD_STATUS] = SBYTE(0xc7),
            [COK_FIELD_ACTIVE] = BYTE(0xc8),
            [COK_FIELD_HOSTILE] = BYTE(0xc9),
            [COK_FIELD_COMPUTER] = BYTE(0xca),
            [COK_FIELD_THAC0] = BYTE(0xcb),
            [COK_FIELD_AC] = BYTE(0xcc),
            [COK_FIELD_AC_BEHIND] = BYTE(0xcd),
            [COK_FIELD_ATTACKS_LEFT] = BYTES(0xce, 2, 1),
            [COK_FIELD_DICE] = BYTES(0xd0, 2, 1),
            [COK_FIELD_DICE_SIDES] = BYTES(0xd2, 2, 1),
            [COK_FIELD_DAMAGE_BONUS] = SBYTES(0xd4, 2, 1),
            [COK_FIELD_HIT_POINTS] = BYTE(0xd6),
            [COK_FIELD_MOVEMENT] = BYTE(0xd7),
        },
};
#undef SPELLS_AT

/* Champions' characters imported (46b2:10d7 with DS:c468 0, from the
 * Champions saved games' path, DKK.CFG's fifth line): its 409-byte record
 * read into a heap block zeroed, then ten Moves into the record zeroed
 * (0xd8), Champions' +0xce-, +0xd7-+0xd9, +0xe6-+0xf2, +0x133-+0x134 and
 * +0x141 dropped; for a class but fighter, thief and fighter/thief, a
 * spells' block zeroed (0xdc) with the memorized spells, the known ones
 * (ids 1-107) and four classes' spells a day, 5 levels of each into 8.
 * Spell, item and effect ids are moved as they are. */
static const cok_rules_move death_knights_import_moves[] = {
    {0x00, 0x000, 0x1e}, {0x1e, 0x058, 0x0b}, {0x29, 0x0cf, 0x08}, {0x31, 0x0da, 0x0c},
    {0x3f, 0x0f3, 0x27}, {0x6b, 0x11a, 0x02}, {0x6d, 0x130, 0x03}, {0x70, 0x135, 0x0c},
    {0x7c, 0x142, 0x46}, {0xc7, 0x188, 0x11},
};
static const cok_rules_move death_knights_import_spell_moves[] = {
    {0x00, 0x01e, 0x3a}, {0x4a, 0x063, 0x6b}, {0xbc, 0x11c, 5},
    {0xc4, 0x121, 5},    {0xcc, 0x126, 5},    {0xd4, 0x12b, 5},
};
static const uint8_t death_knights_import_no_spells[] = {2, 6, 0x0e, 0xff};
/* By race (+0x20): above 1200 for 0 and 1, 200 for 2, 300 for 3 and 4,
 * 50 for 6, the age is set to that; above 199 for 5, to 70. */
static const uint16_t death_knights_import_ages[][2] = {
    {1200, 1200}, {1200, 1200}, {200, 200}, {300, 300}, {300, 300}, {199, 70}, {50, 50},
};
static const cok_rules_import death_knights_import = {
    .from = &cok_rules_champions,
    .moves = death_knights_import_moves,
    .move_count = sizeof death_knights_import_moves / sizeof *death_knights_import_moves,
    .spell_moves = death_knights_import_spell_moves,
    .spell_move_count =
        sizeof death_knights_import_spell_moves / sizeof *death_knights_import_spell_moves,
    .no_spells = death_knights_import_no_spells,
    .ages = death_knights_import_ages,
    .race_count = sizeof death_knights_import_ages / sizeof *death_knights_import_ages,
    /* The set at 46b2:10b7: types 42, 47, 58, 60, 63, 69 and 72. */
    .dropped_items = {[5] = 0x84, [7] = 0x94, [8] = 0x20, [9] = 0x01},
};
#undef DWORD
#undef POINTER
#undef POINTERS
#undef BYTE
#undef SBYTE
#undef WORD
#undef BYTES
#undef SBYTES
#undef WORDS
#undef INTERLEAVED

/* The data segment's tables (data.h), as data_death_knights.c copies them,
 * by DS offset (each a run of the original's initialized data that holds
 * one table or several). Its tables of the characters' rules are
 * Champions' reshaped for levels to 18 (THAC0 rows of 19, saving throws 90
 * bytes a class, experience and spells a day rows of 0xcc with a spell
 * level's 8 bytes apart, three spell lists of 8 levels, turning undead
 * rows of 11). It has no treasure templates (58e7:1039 has no
 * counterpart) and no temple's list of diseases (340d:0323's has none):
 * those have width 0. */
static const cok_ds_table death_knights_tables[] = {
    {0x0312, sizeof cok_death_knights_combat, 1, cok_death_knights_combat},
    {0x0584, sizeof cok_death_knights_alignment_codes, 1, cok_death_knights_alignment_codes},
    {0x05a4, sizeof cok_death_knights_turning, 1, cok_death_knights_turning},
    {0x0656, sizeof cok_death_knights_cone, 1, cok_death_knights_cone},
    {0x08ad, sizeof cok_death_knights_names, 1, cok_death_knights_names},
    {0x37ff, sizeof cok_death_knights_item_words, 1, cok_death_knights_item_words},
    {0x4280, sizeof cok_death_knights_line_directions, 1, cok_death_knights_line_directions},
    {0x42de, sizeof cok_death_knights_colours, 1, cok_death_knights_colours},
    {0x4412, sizeof cok_death_knights_terrain, 1, cok_death_knights_terrain},
    {0x5844, sizeof cok_death_knights_cloud_cells, 1, cok_death_knights_cloud_cells},
    {0x6a80, sizeof cok_death_knights_spells, 1, cok_death_knights_spells},
    {0x71f2, sizeof cok_death_knights_classes, 1, cok_death_knights_classes},
};
#define LIST(b, w, f, n)                                                                           \
    {.base = b, .from = (b) + (f) * (w), .size = (n) * (w), .width = w, .indices = 1,              \
     .index = {{f, n, w}}}
#define TABLE(b, w, f0, n0, s0, f1, n1)                                                            \
    {.base = b, .from = (b) + (f0) * (s0) + (f1) * (w), .size = ((n0) - 1) * (s0) + (n1) * (w),    \
     .width = w, .indices = 2, .index = {{f0, n0, s0}, {f1, n1, w}}}
#define TABLE_IN(b, w, f0, n0, s0, f1, n1, from_, size_)                                           \
    {.base = b, .from = from_, .size = size_, .width = w, .indices = 2,                            \
     .index = {{f0, n0, s0}, {f1, n1, w}}}
#define TABLE3(b, w, f0, n0, s0, f1, n1, s1, f2, n2)                                               \
    {.base = b, .from = (b) + (f0) * (s0) + (f1) * (s1) + (f2) * (w),                              \
     .size = ((n0) - 1) * (s0) + ((n1) - 1) * (s1) + (n2) * (w), .width = w, .indices = 3,         \
     .index = {{f0, n0, s0}, {f1, n1, s1}, {f2, n2, w}}}
static const cok_data_segment death_knights_data = {
    .tables = death_knights_tables,
    .table_count = sizeof death_knights_tables / sizeof *death_knights_tables,
    .table =
        {
            /* The name strings, DS:08ad-0cf7, laid out as Champions'; the
             * item words lie apart (below). */
            [COK_DATA_NAMES] = LIST(0x08ad, 1, 0, 0x44b),
            [COK_DATA_CLASS_NAMES] = LIST(0x08ad, 27, 0, 17), /* 580d:006e */
            [COK_DATA_RACE_NAMES] = LIST(0x0a93, 16, 0, 8),
            [COK_DATA_ALIGNMENT_NAMES] = LIST(0x0b13, 17, 0, 9),
            [COK_DATA_DEITY_NAMES] = LIST(0x0bac, 12, 0, 8),
            [COK_DATA_ORDER_NAMES] = LIST(0x0c0c, 7, 0, 4),
            [COK_DATA_GENDER_NAMES] = LIST(0x0c28, 7, 0, 2),
            [COK_DATA_COIN_NAMES] = LIST(0x0c36, 11, 0, 7), /* 580d:05ff */
            [COK_DATA_STATUS_NAMES] = LIST(0x0c83, 13, 0, 9), /* 65cd:0c44 */
            /* DS:37ea + 21 * word, words 1-125 (65cd:03eb). */
            [COK_DATA_ITEM_WORDS] = LIST(0x37ea, 21, 1, 125),
            /* DS:6a80 + 16 * spell + column, ids 0-0x72 (69a8:0000). */
            [COK_DATA_SPELLS] = TABLE(0x6a80, 1, 0, 0x73, 16, 0, 16),
            /* DS:71f2 + class * 19 + level, levels 0-18 (69a8:050b). */
            [COK_DATA_THAC0] = TABLE(0x71f2, 1, 0, 8, 19, 0, 19),
            [COK_DATA_CLASS_BITS] = LIST(0x728a, 1, 0, 8),     /* 69a8:050b */
            [COK_DATA_TRAINING_BITS] = LIST(0x7293, 1, 0, 8),  /* 49cb:43b6 */
            [COK_DATA_TOP_LEVELS] = LIST(0x72a4, 1, 0, 8),     /* 580d:3193 */
            [COK_DATA_ICON_COLOURS] = LIST(0x72ad, 1, 1, 6),   /* 46b2:078f */
            /* 69a8:0b3d: by level 1-18, race and dexterity 9-19. */
            [COK_DATA_THIEF_LEVEL] = TABLE(0x72ab, 1, 1, 18, 8, 1, 8),
            [COK_DATA_THIEF_RACE] = TABLE(0x7343, 1, 0, 7, 8, 1, 8),
            [COK_DATA_THIEF_DEXTERITY] = TABLE(0x734e, 1, 9, 11, 5, 1, 5),
            /* 49cb:05c7, 49cb:25d4. */
            [COK_DATA_SCORE_LIMITS] = TABLE(0x73b3, 1, 0, 7, 16, 0, 16),
            [COK_DATA_RACE_CLASSES] = TABLE(0x7423, 1, 0, 7, 15, 0, 15),
            [COK_DATA_AGE_DICE] = TABLE(0x748c, 4, 0, 7, 32, 0, 8),
            [COK_DATA_AGES] = TABLE(0x756c, 2, 0, 7, 10, 0, 5),
            [COK_DATA_CLASS_MINIMUMS] = TABLE(0x75b2, 1, 0, 17, 6, 0, 6),
            [COK_DATA_CLASS_ALIGNMENTS] = LIST(0x7618, 1, 0, 17),
            [COK_DATA_CONSTITUTION] = LIST(0x7626, 1, 3, 23), /* 580d:310e */
            /* DS:7638 + 0xcc * row + 4 * level, rows 0-10 (10 the
             * paladin's) and levels 2-18, after each row's spells a day
             * (49cb:43b6). */
            [COK_DATA_EXPERIENCE] = TABLE(0x7638, 4, 0, 11, 0xcc, 2, 17),
            /* DS:7673 + 0xcc * table + 8 * level + spell level 1-8
             * (69a8:0000). */
            [COK_DATA_SPELLS_A_DAY] = TABLE3(0x7673, 1, 0, 11, 0xcc, 2, 17, 8, 1, 8),
            /* DS:7f0f + 90 * class + 5 * level + type, levels 1-18
             * (69a8:08ff). */
            [COK_DATA_SAVING_THROWS] = TABLE3(0x7f0f, 1, 0, 8, 90, 1, 18, 5, 0, 5),
            /* DS:8189 + 80 * list + 10 * level + k, levels 1-8: 1 the
             * cleric's (DS:81d9 + 10 * level, 69a8:0000), 2 the White and 3
             * the Red mages' (5dc5:0a4e). */
            [COK_DATA_SPELL_LISTS] = TABLE3(0x8189, 1, 1, 3, 80, 1, 8, 10, 1, 10),
            /* DS:0312 + 5 * way + try, ways 1-6 (3f63:05d7). */
            [COK_DATA_WAYS] = TABLE_IN(0x0312, 1, 1, 6, 5, 1, 5, 0x0312, 37),
            /* 410b:1444, 410b:18c2. */
            [COK_DATA_MOVES] = TABLE(0x0336, 1, 0, 4, 4, 0, 4),
            [COK_DATA_QUADRANTS] = TABLE(0x0346, 1, 0, 4, 4, 0, 4),
            [COK_DATA_QUADRANT_DIRECTIONS] = LIST(0x0356, 1, 0, 4),
            [COK_DATA_START_COLUMNS] = TABLE(0x035a, 1, 0, 2, 4, 0, 4),
            [COK_DATA_START_ROWS] = TABLE(0x0362, 1, 0, 2, 4, 0, 4),
            [COK_DATA_FORMATIONS] = TABLE3(0x036a, 1, 0, 5, 12, 0, 6, 2, 0, 2),
            [COK_DATA_BESIDE_X] = LIST(0x03a6, 1, 0, 4), /* 410b:008d */
            [COK_DATA_BESIDE_Y] = LIST(0x03aa, 1, 0, 4),
            [COK_DATA_TERRAIN_FLAGS] = LIST(0x03ae, 1, 0, 16), /* 410b:096c */
            /* DS:05a5 + 11 * kind + level, kinds 1-13 (5340:119b). */
            [COK_DATA_TURNING] = TABLE(0x05a5, 1, 1, 13, 11, 0, 11),
            /* DS:0666 + 8 * part + side (5dc5:1690), held to DS:077d. */
            [COK_DATA_CONE_SIDES] = TABLE_IN(0x0666, 1, 0, 4, 8, 0, 8, 0x0666, 0x118),
            [COK_DATA_BATTLE_ONLY] = LIST(0x0686, 1, 0, 16), /* 638c:140c */
            [COK_DATA_LINE_DIRECTIONS] = TABLE(0x4280, 1, 0, 3, 3, 0, 3), /* 6e48:0247 */
            [COK_DATA_SAME_COLOURS] = LIST(0x42de, 1, 0, 16),
            [COK_DATA_BLACK_COLOURS] = LIST(0x42ee, 1, 0, 16),
            [COK_DATA_STEP_X] = LIST(0x4412, 1, 0, 9),
            [COK_DATA_STEP_Y] = LIST(0x441b, 1, 0, 9),
            /* DS:4420 + 4 * value, values 0-69 (410b:008d, 6f03). */
            [COK_DATA_TERRAIN] = TABLE(0x4420, 1, 0, 70, 4, 0, 4),
            [COK_DATA_FOOTPRINTS] = TABLE3(0x4530, 1, 0, 8, 8, 0, 4, 2, 0, 2), /* 6f03:0000 */
            [COK_DATA_HELPLESS] = LIST(0x455a, 1, 0, 3), /* 65cd:0e01: 1f, 34, 35 */
            /* DS:5843 + k, k 1-9 (638c:213c). */
            [COK_DATA_CLOUD_CELLS] = LIST(0x5843, 1, 1, 9),
            [COK_DATA_ALIGNMENT_CODES] = LIST(0x0584, 1, 0, 6), /* 49cb:05c7 */
            [COK_DATA_LEVEL_DICE] = LIST(0x0656, 1, 0, 8),     /* 580d:3193 */
            [COK_DATA_DIE_SIDES] = LIST(0x065e, 1, 0, 8),
            [COK_DATA_KNIGHT_SCORES] = TABLE(0x05a4, 1, 0, 2, 6, 0, 6), /* 49cb:4ea4 */
        },
};
#undef LIST
#undef TABLE
#undef TABLE_IN
#undef TABLE3

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
    SAME(COK_ECL_PROGRAM, 1, 1), /* 3632:23dd: start menu 49cb:010b, camp 237e */
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
    /* A word over two (46b2:2e1d reads it with Move(2)): Champions' bank,
     * as 46b2:1ef0 copies it from Champions' saved game. */
    [COK_VAR_SAVINGS] = {.address = 0x4c33, .width = 2, .count = 1},
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
 * lie in the spells' block the record points to (+0xc2): with none, they
 * read 0 and take no write. */
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

/* The spells (magic.c, cast.c). Their names by id from 1, DS:5825 + 41 *
 * id (5dc5:0a4e lists them); the spell lists' headings, DS:3770 + 11 *
 * level (string[10]). */
static const char *const death_knights_spell_names[0x72] = {
    "Bless", /* 0x01 */
    "Barkskin", /* 0x02 */
    "Cure Light Wounds", /* 0x03 */
    "Charm Person/Mammal", /* 0x04 */
    "Detect Magic", /* 0x05 */
    "Protection From Evil", /* 0x06 */
    "Ressurection", /* 0x07 */
    "Resist Cold", /* 0x08 */
    "Burning Hands", /* 0x09 */
    "Charm Person", /* 0x0a */
    "Detect Magic", /* 0x0b */
    "Enlarge", /* 0x0c */
    "Reduce", /* 0x0d */
    "Friends", /* 0x0e */
    "Magic Missile", /* 0x0f */
    "Protection From Evil", /* 0x10 */
    "Restoration", /* 0x11 */
    "Read Magic", /* 0x12 */
    "Shield", /* 0x13 */
    "Shocking Grasp", /* 0x14 */
    "Sleep", /* 0x15 */
    "Find Traps", /* 0x16 */
    "Hold Person", /* 0x17 */
    "Resist Fire", /* 0x18 */
    "Silence, 15' Radius", /* 0x19 */
    "Slow Poison", /* 0x1a */
    "Snake Charm", /* 0x1b */
    "Spiritual Hammer", /* 0x1c */
    "Detect Invisibility", /* 0x1d */
    "Invisibility", /* 0x1e */
    "Knock", /* 0x1f */
    "Mirror Image", /* 0x20 */
    "Ray of Enfeeblement", /* 0x21 */
    "Stinking Cloud", /* 0x22 */
    "Strength", /* 0x23 */
    "", /* 0x24 */
    "Cure Blindness", /* 0x25 */
    "Cause Blindness", /* 0x26 */
    "Cure Disease", /* 0x27 */
    "Harm", /* 0x28 */
    "Dispel Magic", /* 0x29 */
    "Prayer", /* 0x2a */
    "Remove Curse", /* 0x2b */
    "Bestow Curse", /* 0x2c */
    "Blink", /* 0x2d */
    "Dispel Magic", /* 0x2e */
    "Fireball", /* 0x2f */
    "Haste", /* 0x30 */
    "Hold Person", /* 0x31 */
    "Invisibility, 10' Radius", /* 0x32 */
    "Lightning Bolt", /* 0x33 */
    "Protection From Evil, 10' Radius", /* 0x34 */
    "Heal", /* 0x35 */
    "Protection From Normal Missiles", /* 0x36 */
    "Slow", /* 0x37 */
    "Death Spell", /* 0x38 */
    "", /* 0x39 */
    "Cure Serious Wounds", /* 0x3a */
    "", /* 0x3b */
    "Disentegrate", /* 0x3c */
    "", /* 0x3d */
    "", /* 0x3e */
    "Globe Of Invulnerability", /* 0x3f */
    "", /* 0x40 */
    "", /* 0x41 */
    "Stone to Flesh", /* 0x42 */
    "Neutralize Poison", /* 0x43 */
    "Poison", /* 0x44 */
    "Protection Evil, 10' Radius", /* 0x45 */
    "Sticks to Snakes", /* 0x46 */
    "Cure Critical Wounds", /* 0x47 */
    "Flesh to Stone", /* 0x48 */
    "Dispel Evil", /* 0x49 */
    "Flame Strike", /* 0x4a */
    "Raise Dead", /* 0x4b */
    "Slay Living", /* 0x4c */
    "Detect Magic", /* 0x4d */
    "Entangle", /* 0x4e */
    "Faerie Fire", /* 0x4f */
    "Invisibility to Animals", /* 0x50 */
    "Charm Monsters", /* 0x51 */
    "Confusion", /* 0x52 */
    "Dimension Door", /* 0x53 */
    "Fear", /* 0x54 */
    "Fire Shield", /* 0x55 */
    "Fumble", /* 0x56 */
    "Ice Storm", /* 0x57 */
    "Minor Globe Of Invulnerability", /* 0x58 */
    "Remove Curse", /* 0x59 */
    "Iron Skin", /* 0x5a */
    "Cloud Kill", /* 0x5b */
    "Cone of Cold", /* 0x5c */
    "Feeblemind", /* 0x5d */
    "Hold Monsters", /* 0x5e */
    "", /* 0x5f */
    "", /* 0x60 */
    "", /* 0x61 */
    "Fire Touch", /* 0x62 */
    "", /* 0x63 */
    "Bestow Curse", /* 0x64 */
    "Protection From Evil 10' Radius", /* 0x65 */
    "Silence 15' radius", /* 0x66 */
    "Detect Magic", /* 0x67 */
    "Remove Curse", /* 0x68 */
    "Bless", /* 0x69 */
    "Charm Person", /* 0x6a */
    "Burning Hands", /* 0x6b */
    "Delay Blast Fireball", /* 0x6c */
    "Mass Invisibility", /* 0x6d */
    "Power Word Stun", /* 0x6e */
    "Mass Charm", /* 0x6f */
    "Mind Blank", /* 0x70 */
    "Otto's Irresitable Dance", /* 0x71 */
    "Power Word Blind", /* 0x72 */
};
static const char *const death_knights_spell_levels[10] = {
    "Special", "1st Level", "2nd Level", "3rd Level", "4th Level",
    "5th Level", "6th Level", "7th Level", "8th Level", "9th Level",
};

/* The handlers by spell id, DS:c150 + 4 * id as 5dc5:5622 fills it, 5dc5's
 * routines: each the port's handler whose Champions routine it is, but for
 * the data segment's addresses and the record's offsets (compared
 * instruction by instruction, the strings by their text), or a routine not
 * carried out yet. */
static const cok_rules_cast death_knights_cast[0x73] = {
    [0x01] = {0x1d77, .unported = "dkk 5dc5:1d77"}, /* Bless */
    [0x02] = {0x1daa, .unported = "dkk 5dc5:1daa"}, /* Barkskin */
    [0x03] = {0x1de3, COK_CAST_CURE_LIGHT}, /* Cure Light Wounds */
    [0x04] = {0x1e4d, .unported = "dkk 5dc5:1e4d"}, /* Charm Person/Mammal */
    [0x05] = {0x1f34, COK_CAST_AFFECTED}, /* Detect Magic */
    [0x06] = {0x1f52, COK_CAST_PROTECTED}, /* Protection From Evil */
    [0x07] = {0x1f70, .unported = "dkk 5dc5:1f70"}, /* Ressurection */
    [0x08] = {0x1ff1, COK_CAST_COLD_RESISTANT}, /* Resist Cold */
    [0x09] = {0x2010, COK_CAST_BURNING_HANDS}, /* Burning Hands */
    [0x0a] = {0x2044, .unported = "dkk 5dc5:2044"}, /* Charm Person */
    [0x0b] = {0x1f34, COK_CAST_AFFECTED}, /* Detect Magic */
    [0x0c] = {0x2100, .unported = "dkk 5dc5:2100"}, /* Enlarge */
    [0x0d] = {0x2208, COK_CAST_REDUCE}, /* Reduce */
    [0x0e] = {0x2292, COK_CAST_FRIENDS}, /* Friends */
    [0x0f] = {0x22cf, COK_CAST_MAGIC_MISSILE}, /* Magic Missile */
    [0x10] = {0x1f52, COK_CAST_PROTECTED}, /* Protection From Evil */
    [0x11] = {0x2326, .unported = "dkk 5dc5:2326"}, /* Restoration */
    [0x12] = {0x1f34, COK_CAST_AFFECTED}, /* Read Magic */
    [0x13] = {0x258b, COK_CAST_SHIELDED}, /* Shield */
    [0x14] = {0x25aa, COK_CAST_SHOCKING_GRASP}, /* Shocking Grasp */
    [0x15] = {0x25f1, COK_CAST_SLEEP}, /* Sleep */
    [0x16] = {0x1f34, COK_CAST_AFFECTED}, /* Find Traps */
    [0x17] = {0x271c, COK_CAST_HOLD}, /* Hold Person */
    [0x18] = {0x2763, .unported = "dkk 5dc5:2763"}, /* Resist Fire */
    [0x19] = {0x2781, COK_CAST_SILENCE}, /* Silence, 15' Radius */
    [0x1a] = {0x279f, COK_CAST_SLOW_POISON}, /* Slow Poison */
    [0x1b] = {0x2839, COK_CAST_NOTHING}, /* Snake Charm */
    [0x1c] = {0x283f, COK_CAST_HAMMER}, /* Spiritual Hammer */
    [0x1d] = {0x1f34, COK_CAST_AFFECTED}, /* Detect Invisibility */
    [0x1e] = {0x2882, COK_CAST_INVISIBLE}, /* Invisibility */
    [0x1f] = {0x28a1, COK_CAST_EMPTY_TEXT}, /* Knock */
    [0x20] = {0x28ce, COK_CAST_MIRROR_IMAGE}, /* Mirror Image */
    [0x21] = {0x2916, COK_CAST_WEAKENED}, /* Ray of Enfeeblement */
    [0x22] = {0x2934, .unported = "dkk 5dc5:2934"}, /* Stinking Cloud */
    [0x23] = {0x2bc9, .unported = "dkk 5dc5:2bc9"}, /* Strength */
    [0x24] = {0x2d1b, COK_CAST_NOTHING},
    [0x25] = {0x2d20, COK_CAST_CURE_BLINDNESS}, /* Cure Blindness */
    [0x26] = {0x2d4e, COK_CAST_BLINDED}, /* Cause Blindness */
    [0x27] = {0x2d7d, COK_CAST_CURE_DISEASE}, /* Cure Disease */
    [0x28] = {0x2d8e, .unported = "dkk 5dc5:2d8e"}, /* Harm */
    [0x29] = {0x2e5c, .unported = "dkk 5dc5:2e5c"}, /* Dispel Magic */
    [0x2a] = {0x30e2, COK_CAST_PRAYER}, /* Prayer */
    [0x2b] = {0x311b, .unported = "dkk 5dc5:311b"}, /* Remove Curse */
    [0x2c] = {0x3204, .unported = "dkk 5dc5:3204"}, /* Bestow Curse */
    [0x2d] = {0x3222, COK_CAST_BLINK}, /* Blink */
    [0x2e] = {0x2e5c, .unported = "dkk 5dc5:2e5c"}, /* Dispel Magic */
    [0x2f] = {0x3241, .unported = "dkk 5dc5:3241"}, /* Fireball */
    [0x30] = {0x347b, .unported = "dkk 5dc5:347b"}, /* Haste */
    [0x31] = {0x271c, COK_CAST_HOLD}, /* Hold Person */
    [0x32] = {0x2882, COK_CAST_INVISIBLE}, /* Invisibility, 10' Radius */
    [0x33] = {0x37f8, .unported = "dkk 5dc5:37f8"}, /* Lightning Bolt */
    [0x34] = {0x1f52, COK_CAST_PROTECTED}, /* Protection From Evil, 10' Radius */
    [0x35] = {0x3848, .unported = "dkk 5dc5:3848"}, /* Heal */
    [0x36] = {0x1f52, COK_CAST_PROTECTED}, /* Protection From Normal Missiles */
    [0x37] = {0x396e, .unported = "dkk 5dc5:396e"}, /* Slow */
    [0x38] = {0x398d, .unported = "dkk 5dc5:398d"}, /* Death Spell */
    [0x39] = {0x3aa1, .unported = "dkk 5dc5:3aa1"},
    [0x3a] = {0x3ad3, COK_CAST_CURE_SERIOUS}, /* Cure Serious Wounds */
    [0x3b] = {0x3b3e, COK_CAST_GIANT_STRENGTH},
    [0x3c] = {0x3bb9, .unported = "dkk 5dc5:3bb9"}, /* Disentegrate */
    [0x3d] = {0x3c1b, COK_CAST_PARALYZED},
    [0x3e] = {0x3c39, .unported = "dkk 5dc5:3c39"},
    [0x3f] = {0x3c77, COK_CAST_PROTECTED}, /* Globe Of Invulnerability */
    [0x40] = {0x3241, .unported = "dkk 5dc5:3241"},
    [0x41] = {0x3c96, COK_CAST_ITEM_DAMAGE},
    [0x42] = {0x3cd1, .unported = "dkk 5dc5:3cd1"}, /* Stone to Flesh */
    [0x43] = {0x3e59, COK_CAST_NEUTRALIZE_POISON}, /* Neutralize Poison */
    [0x44] = {0x3f3b, COK_CAST_POISON}, /* Poison */
    [0x45] = {0x1f52, COK_CAST_PROTECTED}, /* Protection Evil, 10' Radius */
    [0x46] = {0x3faf, COK_CAST_STICKS_TO_SNAKES}, /* Sticks to Snakes */
    [0x47] = {0x4037, .unported = "dkk 5dc5:4037"}, /* Cure Critical Wounds */
    [0x48] = {0x40a6, .unported = "dkk 5dc5:40a6"}, /* Flesh to Stone */
    [0x49] = {0x4109, .unported = "dkk 5dc5:4109"}, /* Dispel Evil */
    [0x4a] = {0x4160, .unported = "dkk 5dc5:4160"}, /* Flame Strike */
    [0x4b] = {0x4196, .unported = "dkk 5dc5:4196"}, /* Raise Dead */
    [0x4c] = {0x4248, .unported = "dkk 5dc5:4248"}, /* Slay Living */
    [0x4d] = {0x1f34, COK_CAST_AFFECTED}, /* Detect Magic */
    [0x4e] = {0x42f1, .unported = "dkk 5dc5:42f1"}, /* Entangle */
    [0x4f] = {0x4391, .unported = "dkk 5dc5:4391"}, /* Faerie Fire */
    [0x50] = {0x43a2, COK_CAST_INVISIBLE}, /* Invisibility to Animals */
    [0x51] = {0x43c0, COK_CAST_CHARM_MONSTERS}, /* Charm Monsters */
    [0x52] = {0x4439, .unported = "dkk 5dc5:4439"}, /* Confusion */
    [0x53] = {0x44e6, COK_CAST_DIMENSION_DOOR}, /* Dimension Door */
    [0x54] = {0x4566, .unported = "dkk 5dc5:4566"}, /* Fear */
    [0x55] = {0x468f, .unported = "dkk 5dc5:468f"}, /* Fire Shield */
    [0x56] = {0x47fa, COK_CAST_FUMBLE}, /* Fumble */
    [0x57] = {0x48ef, .unported = "dkk 5dc5:48ef"}, /* Ice Storm */
    [0x58] = {0x4925, COK_CAST_PROTECTED}, /* Minor Globe Of Invulnerability */
    [0x59] = {0x311b, .unported = "dkk 5dc5:311b"}, /* Remove Curse */
    [0x5a] = {0x4943, COK_CAST_PROTECTED}, /* Iron Skin */
    [0x5b] = {0x2934, .unported = "dkk 5dc5:2934"}, /* Cloud Kill */
    [0x5c] = {0x4962, .unported = "dkk 5dc5:4962"}, /* Cone of Cold */
    [0x5d] = {0x4a04, .unported = "dkk 5dc5:4a04"}, /* Feeblemind */
    [0x5e] = {0x271c, COK_CAST_HOLD}, /* Hold Monsters */
    [0x5f] = {0x4aaa, COK_CAST_EMPTY_TEXT},
    [0x60] = {0x4ad7, .unported = "dkk 5dc5:4ad7"},
    [0x61] = {0x4b50, COK_CAST_EMPTY_TEXT},
    [0x62] = {0x4b7d, .unported = "dkk 5dc5:4b7d"}, /* Fire Touch */
    [0x63] = {0x4b9b, .unported = "dkk 5dc5:4b9b"},
    [0x64] = {0x3204, .unported = "dkk 5dc5:3204"}, /* Bestow Curse */
    [0x65] = {0x1f52, COK_CAST_PROTECTED}, /* Protection From Evil 10' Radius */
    [0x66] = {0x2781, COK_CAST_SILENCE}, /* Silence 15' radius */
    [0x67] = {0x1f34, COK_CAST_AFFECTED}, /* Detect Magic */
    [0x68] = {0x311b, .unported = "dkk 5dc5:311b"}, /* Remove Curse */
    [0x69] = {0x1d77, .unported = "dkk 5dc5:1d77"}, /* Bless */
    [0x6a] = {0x2044, .unported = "dkk 5dc5:2044"}, /* Charm Person */
    [0x6b] = {0x2010, COK_CAST_BURNING_HANDS}, /* Burning Hands */
    [0x6c] = {0x3241, .unported = "dkk 5dc5:3241"}, /* Delay Blast Fireball */
    [0x6d] = {0x2882, COK_CAST_INVISIBLE}, /* Mass Invisibility */
    [0x6e] = {0x4bd9, .unported = "dkk 5dc5:4bd9"}, /* Power Word Stun */
    [0x6f] = {0x4c8a, .unported = "dkk 5dc5:4c8a"}, /* Mass Charm */
    [0x70] = {0x1f52, COK_CAST_PROTECTED}, /* Mind Blank */
    [0x71] = {0x4dd8, .unported = "dkk 5dc5:4dd8"}, /* Otto's Irresitable Dance */
    [0x72] = {0x4e81, .unported = "dkk 5dc5:4e81"}, /* Power Word Blind */
};

/* The durations 5dc5:0ef5 does not take from the table: as Champions'
 * (5b04:0f78) but for Cause Disease's, 0x3f's and Neutralize Poison's,
 * which it takes from the table. */
static const cok_rules_duration death_knights_durations[] = {
    {0x1a, COK_DURATION_FIXED, 0xec4}, {0x39, COK_DURATION_5D4, 0},
    {0x3d, COK_DURATION_5D4, 0},       {0x3b, COK_DURATION_D4_TENS_40, 0},
};

/* The spell ids the code names: Champions' (each the same name, class and
 * level in the spell table), but Animate Dead, which it has not. */
static const uint8_t death_knights_spell_ids[COK_SPELL_NAMES] = {
    [COK_SPELL_BLESS] = 0x01,
    [COK_SPELL_CURE_LIGHT_WOUNDS] = 0x03,
    [COK_SPELL_MAGIC_MISSILE] = 0x0f,
    [COK_SPELL_HOLD_PERSON] = 0x17,
    [COK_SPELL_KNOCK] = 0x1f,
    [COK_SPELL_ANIMATE_DEAD] = 0, /* no such spell: 0x24 has no name */
    [COK_SPELL_REMOVE_CURSE] = 0x2b,
    [COK_SPELL_FIREBALL] = 0x2f,
    [COK_SPELL_HOLD_PERSON_MAGE] = 0x31,
    [COK_SPELL_LIGHTNING_BOLT] = 0x33,
    [COK_SPELL_CURE_SERIOUS_WOUNDS] = 0x3a,
    [COK_SPELL_GIANT_STRENGTH] = 0x3b,
    [COK_SPELL_PARALYZE] = 0x3d,
    [COK_SPELL_FIREBALL_ITEM] = 0x40,
    [COK_SPELL_CURE_CRITICAL_WOUNDS] = 0x47,
    [COK_SPELL_ENTANGLE] = 0x4f,
    [COK_SPELL_CHARM_MONSTERS] = 0x51,
    [COK_SPELL_CONFUSION] = 0x52,
    [COK_SPELL_DIMENSION_DOOR] = 0x53,
    [COK_SPELL_FEAR] = 0x54,
    [COK_SPELL_FIRE_SHIELD] = 0x55,
    [COK_SPELL_FUMBLE] = 0x56,
    [COK_SPELL_BLESS_POWER] = 0x69,
};

/* The spell effects (effect.c). The handlers by effect id, DS:be7e + 4 *
 * id (638c:0185), as 504d:2bfc and 5dc5:5622 fill it: as the spells'. Its
 * 0x79 is the item routine, Champions' 0x78 (504d:28e6, 3f44:3888). */
static const cok_rules_effect death_knights_effect_handlers[0x7a] = {
    [0x01] = {0x0114, COK_FX_BLESS},
    [0x02] = {0x0124, .unported = "dkk 504d:0124"},
    [0x03] = {0x018e, COK_FX_SNAKES},
    [0x04] = {0x0219, .unported = "dkk 504d:0219"},
    [0x05] = {0x28df, COK_FX_NOTHING},
    [0x06] = {0x00e7, COK_FX_HELD},
    [0x07] = {0x0239, .unported = "dkk 504d:0239"},
    [0x08] = {0x0362, COK_FX_PROTECTION_EVIL},
    [0x09] = {0x0394, .unported = "dkk 504d:0394"},
    [0x0a] = {0x03a6, COK_FX_RESIST_COLD},
    [0x0b] = {0x03c5, COK_FX_CHARM},
    [0x0c] = {0x28df, COK_FX_NOTHING},
    [0x0d] = {0x04a5, .unported = "dkk 504d:04a5"},
    [0x0e] = {0x28df, COK_FX_NOTHING},
    [0x0f] = {0x04c4, .unported = "dkk 504d:04c4"},
    [0x10] = {0x28df, COK_FX_NOTHING},
    [0x11] = {0x0545, COK_FX_SHIELD},
    [0x12] = {0x0576, COK_FX_RESIST_CHARM_30},
    [0x13] = {0x28df, COK_FX_NOTHING},
    [0x14] = {0x059a, COK_FX_RESIST_FIRE},
    [0x15] = {0x05bf, .unported = "dkk 504d:05bf"},
    [0x16] = {0x067c, .unported = "dkk 504d:067c"},
    [0x17] = {0x06dc, .unported = "dkk 504d:06dc"},
    [0x18] = {0x28df, COK_FX_NOTHING},
    [0x19] = {0x08cb, COK_FX_INVISIBLE},
    [0x1a] = {0x0900, .unported = "dkk 504d:0900"},
    [0x1b] = {0x00e7, COK_FX_HELD},
    [0x1c] = {0x093f, COK_FX_IMAGES},
    [0x1d] = {0x09b9, COK_FX_QUARTER_LESS},
    [0x1e] = {0x09d5, .unported = "dkk 504d:09d5"},
    [0x1f] = {0x00c9, COK_FX_HELD},
    [0x20] = {0x0a7e, .unported = "dkk 504d:0a7e"},
    [0x21] = {0x0ac5, COK_FX_BLIND},
    [0x22] = {0x0aee, .unported = "dkk 504d:0aee"},
    [0x23] = {0x0bab, .unported = "dkk 504d:0bab"},
    [0x24] = {0x0d4d, COK_FX_MINUS_FOUR},
    [0x25] = {0x0d5e, .unported = "dkk 504d:0d5e"},
    [0x26] = {0x28df, COK_FX_NOTHING},
    [0x27] = {0x0d88, .unported = "dkk 504d:0d88"},
    [0x28] = {0x0df2, COK_FX_NOTHING},
    [0x29] = {0x0ed3, COK_FX_AVOID},
    [0x2a] = {0x0f0d, .unported = "dkk 504d:0f0d"},
    [0x2b] = {0x28df, COK_FX_NOTHING},
    [0x2c] = {0x28df, COK_FX_NOTHING},
    [0x2d] = {0x0362, COK_FX_PROTECTION_EVIL},
    [0x2e] = {0x28df, COK_FX_NOTHING},
    [0x2f] = {0x0f30, COK_FX_AGAINST_FLAG4},
    [0x30] = {0x0f4f, .unported = "dkk 504d:0f4f"},
    [0x31] = {0x0f89, COK_FX_PRAYER},
    [0x32] = {0x0fd4, COK_FX_COLD_RESISTANT},
    [0x33] = {0x28df, COK_FX_NOTHING},
    [0x34] = {0x00c9, COK_FX_HELD},
    [0x35] = {0x00c9, COK_FX_HELD},
    [0x36] = {0x1005, COK_FX_FIRE_RESISTANT},
    [0x37] = {0x1036, COK_FX_NOTHING},
    [0x38] = {0x103d, COK_FX_NOTHING},
    [0x39] = {0x1044, .unported = "dkk 504d:1044"},
    [0x3a] = {0x1063, .unported = "dkk 504d:1063"},
    [0x3b] = {0x108d, .unported = "dkk 504d:108d"},
    [0x3c] = {0x10b4, .unported = "dkk 504d:10b4"},
    [0x3d] = {0x28df, COK_FX_NOTHING},
    [0x3e] = {0x10d3, COK_FX_REGENERATE},
    [0x3f] = {0x111e, COK_FX_MINOR_GLOBE},
    [0x40] = {0x1215, COK_FX_POISON},
    [0x41] = {0x1228, .unported = "dkk 504d:1228"},
    [0x42] = {0x123d, .unported = "dkk 504d:123d"},
    [0x43] = {0x1815, .unported = "dkk 504d:1815"},
    [0x44] = {0x1836, .unported = "dkk 504d:1836"},
    [0x45] = {0x1857, COK_FX_PARALYSIS},
    [0x46] = {0x186e, .unported = "dkk 504d:186e"},
    [0x47] = {0x1940, COK_FX_REFLECT},
    [0x48] = {0x1a79, .unported = "dkk 504d:1a79"},
    [0x49] = {0x1bce, COK_FX_UNAFFECTED},
    [0x4a] = {0x1bff, .unported = "dkk 504d:1bff"},
    [0x4b] = {0x1c64, .unported = "dkk 504d:1c64"},
    [0x4c] = {0x1c7e, .unported = "dkk 504d:1c7e"},
    [0x4d] = {0x1cf2, .unported = "dkk 504d:1cf2"},
    [0x4e] = {0x4f97, .unported = "dkk 5dc5:4f97"},
    [0x4f] = {0x1e47, .unported = "dkk 504d:1e47"},
    [0x50] = {0x1f6b, .unported = "dkk 504d:1f6b"},
    [0x51] = {0x4fd3, .unported = "dkk 5dc5:4fd3"},
    [0x52] = {0x28df, COK_FX_NOTHING},
    [0x53] = {0x1f79, .unported = "dkk 504d:1f79"},
    [0x54] = {0x1f9c, .unported = "dkk 504d:1f9c"},
    [0x55] = {0x508c, .unported = "dkk 5dc5:508c"},
    [0x56] = {0x1ff2, COK_FX_POISON},
    [0x57] = {0x2005, .unported = "dkk 504d:2005"},
    [0x58] = {0x20e5, COK_FX_PARALYSIS},
    [0x59] = {0x2112, COK_FX_DISPLACEMENT},
    [0x5a] = {0x51cc, .unported = "dkk 5dc5:51cc"},
    [0x5b] = {0x215b, COK_FX_NOTHING},
    [0x5c] = {0x2162, .unported = "dkk 504d:2162"},
    [0x5d] = {0x2170, .unported = "dkk 504d:2170"},
    [0x5e] = {0x21bd, COK_FX_CONSTITUTION_SAVE},
    [0x5f] = {0x223d, COK_FX_RESIST_CHARM_90},
    [0x60] = {0x2261, COK_FX_RESIST_CHARM},
    [0x61] = {0x2276, COK_FX_FREE_ACTION},
    [0x62] = {0x2284, COK_FX_COLD_IMMUNE},
    [0x63] = {0x229b, COK_FX_POISON_IMMUNE},
    [0x64] = {0x51cc, .unported = "dkk 5dc5:51cc"},
    [0x65] = {0x22bc, .unported = "dkk 504d:22bc"},
    [0x66] = {0x28df, COK_FX_NOTHING},
    [0x67] = {0x23b9, COK_FX_MAGIC_WEAPONS},
    [0x68] = {0x51cc, .unported = "dkk 5dc5:51cc"},
    [0x69] = {0x2404, .unported = "dkk 504d:2404"},
    [0x6a] = {0x2448, .unported = "dkk 504d:2448"},
    [0x6b] = {0x25bb, .unported = "dkk 504d:25bb"},
    [0x6c] = {0x26e0, .unported = "dkk 504d:26e0"},
    [0x6d] = {0x26f1, COK_FX_NOTHING},
    [0x6e] = {0x26f8, .unported = "dkk 504d:26f8"},
    [0x6f] = {0x2721, COK_FX_TERROR},
    [0x70] = {0x2762, COK_FX_FIRE_SHIELD},
    [0x71] = {0x27df, COK_FX_NOTHING},
    [0x72] = {0x27e6, .unported = "dkk 504d:27e6"},
    [0x73] = {0x282d, .unported = "dkk 504d:282d"},
    [0x74] = {0x28df, COK_FX_NOTHING},
    [0x75] = {0x286b, .unported = "dkk 504d:286b"},
    [0x76] = {0x28d3, COK_FX_MINUS_TWO},
    [0x77] = {0x28df, COK_FX_NOTHING},
    [0x78] = {0x28df, COK_FX_NOTHING},
    [0x79] = {0x28e6, COK_FX_ITEM},
};

/* The effect ids each event runs, in order (638c:055b); none for events
 * 0x0d and 0x18. */
static const uint8_t *const death_knights_events[0x16] = {
    [0x01] = (const uint8_t[]){0x19, 0x20, 0x25, 0},
    [0x02] = (const uint8_t[]){0x02, 0x40, 0x43, 0x44, 0x45, 0x4a, 0x56, 0x58, 0},
    [0x03] = (const uint8_t[]){0x40, 0x43, 0x44, 0x45, 0x4a, 0x56, 0},
    [0x04] = (const uint8_t[]){0x07, 0x1d, 0x22, 0x69, 0x6c, 0x72, 0x73, 0x75, 0},
    [0x05] = (const uint8_t[]){0x1c, 0x29, 0x42, 0x67, 0x5d, 0x70, 0},
    [0x06] = (const uint8_t[]){0x0a, 0x11, 0x14, 0x1c, 0x3b, 0x3f, 0x41, 0x47, 0x49, 0x4b, 0x4c,
                               0x53, 0x57, 0x62, 0x65, 0},
    [0x07] = (const uint8_t[]){0x03, 0x06, 0x1b, 0x1f, 0x39, 0x34, 0x35, 0},
    [0x08] = (const uint8_t[]){0x59, 0},
    [0x09] = (const uint8_t[]){0x12, 0x39, 0x3b, 0x3f, 0x47, 0x50, 0x57, 0x5c, 0x5f, 0x60, 0x61,
                               0x62, 0x63, 0x65, 0x74, 0},
    [0x0a] = (const uint8_t[]){0x01, 0x07, 0x1a, 0x21, 0x24, 0x31, 0x6c, 0x76, 0},
    [0x0b] = (const uint8_t[]){0x04, 0x08, 0x0d, 0x11, 0x1e, 0x21, 0x2d, 0x30, 0x39, 0x3c, 0},
    [0x0c] = (const uint8_t[]){0x08, 0x0a, 0x11, 0x14, 0x21, 0x24, 0x2d, 0x30, 0x31, 0x32, 0x36,
                               0x39, 0x5e, 0x63, 0},
    [0x0e] = (const uint8_t[]){0x51, 0x55, 0x4e, 0x5a, 0x64, 0x68, 0x6a, 0},
    [0x0f] = (const uint8_t[]){0x0b, 0x15, 0x1e, 0x3d, 0x46, 0x48, 0x4f, 0x4d, 0x54, 0},
    [0x10] = (const uint8_t[]){0x19, 0x20, 0x25, 0x2f, 0x59, 0},
    [0x11] = (const uint8_t[]){0x01, 0x0b, 0},
    [0x12] = (const uint8_t[]){0x09, 0x27, 0x2a, 0},
    [0x13] = (const uint8_t[]){0x0b, 0x17, 0},
    [0x14] = (const uint8_t[]){0x32, 0x36, 0},
    [0x15] = (const uint8_t[]){0x23, 0},
};

/* The ids the party shares, the set at 638c:0307, Prayer's from 6 squares
 * (638c:0327), and the effects magic resistance leaves pending (638c:04c9),
 * all as Champions'. */
static const uint8_t death_knights_shared[] = {0x15, 0x2d, 0x2e, 0x31, 0};
static const uint8_t death_knights_resisted_pending[] = {0x5b, 0x52, 0};

/* The effect ids the code names: Champions', where the game's handler of
 * the id is Champions' routine of it (or its counterpart in the routine
 * matching), or where a shared handler adds or tests it by the same id;
 * 0 for the others, whose handlers here are not Champions' (step 3). */
static const uint8_t death_knights_effect_ids[COK_EFFECT_NAMES] = {
    [COK_EFFECT_SNAKES] = 0x03,
    [COK_EFFECT_BOLTS] = 0,
    [COK_EFFECT_DETECT_MAGIC] = 0x05,
    [COK_EFFECT_CHARMED] = 0x0b,
    [COK_EFFECT_ENLARGED] = 0x0c,
    [COK_EFFECT_NEW_FORM] = 0,
    [COK_EFFECT_FRIENDLY] = 0x0e,
    [COK_EFFECT_POISON] = 0x0f,
    [COK_EFFECT_READ_MAGIC] = 0x10,
    [COK_EFFECT_RESIST_CHARM_30] = 0x12,
    [COK_EFFECT_SLOW_POISON] = 0x16, /* used by a shared handler */
    [COK_EFFECT_HAMMER] = 0x17, /* used by a shared handler */
    [COK_EFFECT_SEES_INVISIBLE] = 0x18,
    [COK_EFFECT_INVISIBLE] = 0x19,
    [COK_EFFECT_TARGET_BONUS] = 0x1a,
    [COK_EFFECT_MIRROR_IMAGES] = 0x1c,
    [COK_EFFECT_NAUSEA] = 0,
    [COK_EFFECT_HELPLESS] = 0x1f,
    [COK_EFFECT_MAD] = 0,
    [COK_EFFECT_BLIND] = 0x21,
    [COK_EFFECT_DISEASE] = 0x22, /* used by a shared handler */
    [COK_EFFECT_CONFUSED] = 0,
    [COK_EFFECT_CURSED] = 0x24,
    [COK_EFFECT_STRENGTH] = 0x26,
    [COK_EFFECT_HASTED] = 0,
    [COK_EFFECT_CLOUD] = 0,
    [COK_EFFECT_SLOWED] = 0x2a,
    [COK_EFFECT_STRENGTH_DRAIN] = 0x2b, /* used by a shared handler */
    [COK_EFFECT_WASTING] = 0x2c, /* used by a shared handler */
    [COK_EFFECT_HARD_TO_HIT] = 0x2f,
    [COK_EFFECT_IMMOLATE] = 0,
    [COK_EFFECT_HOT_FIRE_SHIELD] = 0x32,
    [COK_EFFECT_HELD] = 0,
    [COK_EFFECT_PARALYZED] = 0x34,
    [COK_EFFECT_ASLEEP] = 0x35,
    [COK_EFFECT_COLD_FIRE_SHIELD] = 0x36,
    [COK_EFFECT_POISONED] = 0x37,
    [COK_EFFECT_PLUS_TWO] = 0,
    [COK_EFFECT_DEATH_THROES] = 0,
    [COK_EFFECT_IMMUNE] = 0,
    [COK_EFFECT_BURST] = 0,
    [COK_EFFECT_REGENERATING] = 0x3e,
    [COK_EFFECT_MINOR_GLOBE] = 0x3f,
    [COK_EFFECT_POISON_TOUCH] = 0x40,
    [COK_EFFECT_POISON_TOUCH_4] = 0,
    [COK_EFFECT_ZAP] = 0,
    [COK_EFFECT_EXPLODES] = 0,
    [COK_EFFECT_PARALYZING] = 0x45,
    [COK_EFFECT_DISPELLING] = 0,
    [COK_EFFECT_GIANT_SLAYER] = 0,
    [COK_EFFECT_GATE] = 0,
    [COK_EFFECT_BERSERK] = 0,
    [COK_EFFECT_FIREBALLS] = 0,
    [COK_EFFECT_ACID_DEATH] = 0,
    [COK_EFFECT_ACID_POOL] = 0,
    [COK_EFFECT_PARALYZING_HARMLESS] = 0,
    [COK_EFFECT_COLD_BREATH] = 0,
    [COK_EFFECT_POISON_BREATH] = 0,
    [COK_EFFECT_LIGHTNING_BREATH] = 0,
    [COK_EFFECT_POISON_TOUCH_MINUS_2] = 0x56,
    [COK_EFFECT_POISON_TOUCH_1] = 0,
    [COK_EFFECT_PARALYZING_HUMANS] = 0x58,
    [COK_EFFECT_ACID_BREATH] = 0,
    [COK_EFFECT_YELLED_AT] = 0x5b,
    [COK_EFFECT_FEARLESS] = 0,
    [COK_EFFECT_CONSTITUTION_SAVE] = 0x5e,
    [COK_EFFECT_RESIST_CHARM_90] = 0x5f,
    [COK_EFFECT_POISON_IMMUNE] = 0x63,
    [COK_EFFECT_FROST_BREATH] = 0,
    [COK_EFFECT_FIRE_BREATH] = 0,
    [COK_EFFECT_RANGER] = 0x69,
    [COK_EFFECT_BANE] = 0x73,
    [COK_EFFECT_STUNNED] = 0,
    [COK_EFFECT_TERROR] = 0x6f,
    [COK_EFFECT_FIRE_SHIELD] = 0x70,
    [COK_EFFECT_GIANT_STRENGTH] = 0x71,
    [COK_EFFECT_DEADLY] = 0x72,
    [COK_EFFECT_POISON_RESISTANCE] = 0x74,
    [COK_EFFECT_SICKENED] = 0x76,
    [COK_EFFECT_AFRAID] = 0,
};

/* The screens' frames (110f:0000-0584): tile values added to 0x100, from
 * the data segment and the routines' code. */
static const uint8_t death_knights_top_row[40] = { /* DS:070c */
    0, 4, 4, 4, 4, 4, 4, 5, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4,
    6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 6, 4, 4, 4, 4, 4, 4, 4, 1,
};
static const uint8_t death_knights_bottom_row[40] = { /* DS:0734 */
    2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3,
};
static const uint8_t death_knights_divider_row[40] = { /* DS:075c */
    0x0a, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0x0b,
};
static const uint8_t death_knights_divider[17] = { /* DS:0784, rows 0-16 */
    8, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 9,
};
static const uint8_t death_knights_combat_side[23] = { /* DS:07ac, rows 0-22 */
    8, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 9,
};
static const uint8_t death_knights_sheet_left[22] = { /* DS:07d9 + row, rows 1-22 */
    7, 7, 7, 7, 7, 7, 7, 0x0a, 7, 7, 7, 7, 7, 7, 7, 0x0a, 7, 7, 7, 0x0a,
    7, 7,
};
static const uint8_t death_knights_sheet_right[22] = { /* DS:07ef + row, rows 1-22 */
    7, 7, 7, 7, 7, 7, 7, 0x0b, 7, 7, 7, 7, 7, 7, 7, 0x0b, 7, 7, 7, 0x0b,
    7, 7,
};
static const uint8_t death_knights_sheet_middle[13] = { /* DS:07fe + row, rows 8-20 */
    8, 7, 7, 7, 7, 7, 7, 7, 0x0c, 7, 7, 7, 9,
};

#define ROW(y, from, to, value) {false, y, from, to, value, NULL, false}
#define ROW_OF(y, from, to, table) {false, y, from, to, 0, table, false}
#define COLUMN(x, from, to, value) {true, x, from, to, value, NULL, false}
#define COLUMN_OF(x, from, to, table) {true, x, from, to, 0, table, false}
#define MOONS(table) {false, 0, 0, 39, 0, table, true}
#define LINES(lines) lines, sizeof lines / sizeof *lines

/* 110f:0000 with 0: row 0 with the moons, row 23, the sides between (tile
 * 7). Every caller outside 110f passes 0: Champions' open frame has no
 * counterpart. */
static const cok_rules_frame_line death_knights_frame[] = {
    MOONS(death_knights_top_row),
    ROW_OF(23, 0, 39, death_knights_bottom_row),
    COLUMN(0, 1, 22, 7),
    COLUMN(39, 1, 22, 7),
};
/* 110f:0162: row 16, the column 16 divider above it, the 3D view's box:
 * its corners 0-3, rows of 4 and columns of 7 between. */
static const cok_rules_frame_line death_knights_adventure[] = {
    ROW_OF(16, 0, 39, death_knights_divider_row),
    COLUMN_OF(16, 0, 16, death_knights_divider),
    ROW(2, 2, 2, 0),
    ROW(2, 14, 14, 1),
    ROW(14, 2, 2, 2),
    ROW(14, 14, 14, 3),
    ROW(2, 3, 13, 4),
    ROW(14, 3, 13, 4),
    COLUMN(2, 3, 13, 7),
    COLUMN(14, 3, 13, 7),
};
static const cok_rules_frame_line death_knights_row_16[] = { /* 110f:02ab */
    ROW_OF(16, 0, 39, death_knights_divider_row),
};
/* 110f:02f8: 110f:0000 with 2 (rows 1-16 cleared), then row 16. */
static const cok_rules_frame_line death_knights_spells[] = {
    MOONS(death_knights_top_row),
    ROW_OF(23, 0, 39, death_knights_bottom_row),
    COLUMN(0, 1, 22, 7),
    COLUMN(39, 1, 22, 7),
    ROW_OF(16, 0, 39, death_knights_divider_row),
};
static const cok_rules_frame_line death_knights_list[] = { /* 110f:0537 */
    ROW_OF(2, 0, 39, death_knights_divider_row),
};
/* 110f:0345: 110f:0000 with 1 (all cleared, the bottom row 22), then
 * column 22. */
static const cok_rules_frame_line death_knights_combat[] = {
    MOONS(death_knights_top_row),
    ROW_OF(22, 0, 39, death_knights_bottom_row),
    COLUMN(0, 1, 21, 7),
    COLUMN(39, 1, 21, 7),
    COLUMN_OF(22, 0, 22, death_knights_combat_side),
};
/* 110f:0392: row 0, row 23, rows 8, 16 and 20 between the sides, the
 * sides, column 19 from row 8 to 20. */
static const cok_rules_frame_line death_knights_sheet[] = {
    MOONS(death_knights_top_row),
    ROW_OF(23, 0, 39, death_knights_bottom_row),
    ROW(8, 1, 38, 4),
    ROW(16, 1, 38, 4),
    ROW(20, 1, 38, 4),
    COLUMN_OF(0, 1, 22, death_knights_sheet_left),
    COLUMN_OF(39, 1, 22, death_knights_sheet_right),
    COLUMN_OF(19, 8, 20, death_knights_sheet_middle),
};

/* The credits' lines (35c6:026e, its code segment's strings from 35c6:008d):
 * yellow (14), white (15) and light green (10). */
static const cok_rules_text_line death_knights_credit_lines[] = {
    {2, 1, 14, "CREATED BY:SSI SPECIAL PROJECTS TEAM"},
    {11, 3, 15, "GAME DEVELOPMENT:"},
    {4, 4, 10, "KEN HUMPHRIES     DAVE SHELLEY"},
    {13, 5, 15, "PROGRAMMING:"},
    {14, 6, 10, "RUSS BROWN"},
    {1, 8, 15, "ENCOUNTER CODE:"},
    {4, 9, 10, "RHONDA GILBERT    KEN EKLUND"},
    {4, 10, 10, "CAROLYN BICKFORD  GARY SHOCKLEY"},
    {4, 11, 10, "CYRUS HARRIS      ALAN MARENCO"},
    {4, 12, 10, "TERREA THOMPSON"},
    {1, 13, 15, "GRAPHIC ARTS:"},
    {4, 14, 10, "MAURINE STARKEY   MIKE PROVENZA"},
    {4, 15, 10, "LAURA BOWEN       CYRUS LUM"},
    {4, 16, 10, "FRED BUTTS        DANIEL COLON,JR."},
    {1, 17, 15, "PLAYTEST:"},
    {4, 18, 10, "JOHN KIRK         JEFF SHOTWELL"},
    {4, 19, 10, "MIKE GILMARTIN    GLEN CURETON"},
    {4, 20, 10, "PHIL ALNE         ERIK FLOM"},
    {1, 21, 15, "MUSIC:"},
    {4, 22, 10, "\"THE FAT MAN\"     DAVE GOVETT"},
};

const cok_rules cok_rules_death_knights = {
    .id = "death-knights",
    .assets = "DKKAssets",
    .text =
        {
            .name = "Death Knights of Krynn",
            /* The title's menu's (DS:2fb0, both times: entry). */
            .version = "Death Knights Of Krynn 1.10 ",
            .version_again = "Death Knights Of Krynn 1.10 ",
            .camp = "the party makes camp", /* DS:16b7 */
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
            .monster_spells = "WIZ", /* MON1WIZ: the monsters' spells' blocks (46b2:19a2) */
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
            /* SAVGAM?.PTY (46b2:1ef0, 2808: DS:71d9, 46b2:1ebf); the
             * characters' files, DS:57ac-57b5 and DS:71c0-71ca: .GAM in a
             * saved game and .PC in the roster (46b2:0abc), .JNK, .EFF and
             * .WIZ beside it. */
            .game = "SAVGAM",
            .game_extension = "PTY",
            .character = "CHRDAT",
            .record = "GAM",
            .roster = "PC",
            .items = "JNK",
            .effects = "EFF",
            .spells = "WIZ",
            .debug = NULL, /* no debug mode */
            .walls = 4,          /* DS:c078: slots 0-3 */
            .walls_restored = 1, /* 46b2:1ef0: 6d21:0ec5 with DS:c07a, c078 */
            .roster_flag = COK_FIELD_NPC, /* 46b2:0342, DS:57a */
            .roster_flag_index = 0,
            .keep_last_mode = true,
            /* vault<letter>.dat (46b2:2e12, 2e18; 27f0, 27f6) and
             * tmpVault.dat (2e05, 27fb). */
            .vault = "VAULT",
            .vault_extension = "DAT",
            .vault_temp = "TMPVAULT.DAT",
        },
    .images =
        {
            .delta = death_knights_deltas,
            .collection = death_knights_collections,
            .single = death_knights_singles,
            .frames_ff = 256, /* 123c:1c58 */
        },
    .character = &death_knights_character,
    .characters =
        {
            .stats = COK_STATS_DEATH_KNIGHTS,   /* 65cd:0e46 */
            .levels = COK_LEVELS_DEATH_KNIGHTS, /* 69a8:050b */
            .import = &death_knights_import,
        },
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
            /* 70eb:002f: 13 but for the frame's set, 0 (its colour 13
             * drawn: the moons' and the frame's magenta). */
            .tile_transparent = {13, 13, 13, 13, 0},
            .first_slot = 0,      /* 3632:0daa */
            .shared_tiles = true, /* 6d21:0ec5 */
            .move_tiles = false,  /* 6d21:0ec5 computes the offset but adds nothing */
            .overhead_square = 0x119, /* 6d21:0000 */
            .overhead_party = 0x129,
            .overland_picture = 0x70, /* 3632:096c */
            .cga_backdrop = false,    /* 6d21:0177 */
        },
    .startup =
        {
            /* 4f9a:00e1: the frame's tile set only. */
            .tiles = {{4, 202}},
            .tile_count = 1,
            /* 4f9a:00e1: COMSPR 0-21 into slots 13-34. */
            .icons = {{0, 13, 22}},
            .icon_ranges = 1,
            /* Loaded without a mask (DS:42d0's 0), its colour 13 drawn. */
            .cursor_transparent = -1,
            /* DS:b474, 35 slots (4f9a:0556 clears 0x118 bytes). */
            .icon_slots = 35,
            .icon_table = 0xb474,
        },
    .spells =
        {
            .count = 0x73,
            .names = death_knights_spell_names,
            .names_at = 0x5825,
            .name_size = 41,
            .levels = death_knights_spell_levels,
            .level_count = 10,
            .levels_at = 0x3770,
            .level_size = 11, /* 5dc5:0380 */
            .cast = death_knights_cast,
            .durations = death_knights_durations,
            .duration_count = sizeof death_knights_durations / sizeof *death_knights_durations,
            .ids = death_knights_spell_ids,
            .list_spells = 0x4a,  /* 5dc5:054c's 74 */
            .list_scrolls = 0x31, /* 5dc5:08a5, as Champions' */
            .granted_first = 101, /* 4381:0664, as Champions' */
            .granted_last = 107,
            .item_last = 0x38, /* 3f63:02b8, as Champions' */
            .item_shift = 0x17,
            /* Display (4381:15aa) differs: not ported (display_spells NULL). */
            .display_last = 0,
            .target_caster = 1, /* 5dc5:1182: 3, not 4, for the party */
            .target_one = 2,
            .target_party = 3,
            .weapon_unscored = 0x53, /* 3f63:14f5: cmp byte [es:di+0x3d], 0x53 */
            /* 5dc5:1298, Champions' 5b04:1415 but for, among others, no
             * miscast under effect 0x4a: the cast stops until step 3. */
            .cast_unported = "dkk 5dc5:1298",
        },
    .effects =
        {
            .count = 0x7a,
            .handlers = death_knights_effect_handlers,
            .events = death_knights_events,
            .event_count = 0x16,
            .shared = death_knights_shared,
            .shared_far_id = 0x31,
            .shared_far = 6,
            .resisted_pending = death_knights_resisted_pending,
            .ids = death_knights_effect_ids,
            .display_spells = NULL,
            .display_texts = NULL,
            .display_text_count = 0,
            .caster_level_unported = "dkk 65cd:2c33",
            .save_unported = "dkk 638c:10f8",
            .resistance_unported = "dkk 638c:04c9",
        },
    .moons =
        {
            /* 5b7d:0117's days, 5b7d:0000's columns; its tiles 0x100 +
             * phase + 0x15, 0x0d and 0x11 of the frame's set (step 2). */
            .days = {8, 1, 6},
            .column = {8, 19, 30},
            .tile = {0x15, 0x0d, 0x11},
            .tile_base = 0x100,
        },
    .frame =
        {
            .base = 0x100, /* 110f's tiles: 0x100 + value */
            .screens =
                {
                    [COK_SCREEN_FRAME] = {COK_SCREENS, 1, 1, 38, 22,
                                          LINES(death_knights_frame)},
                    [COK_SCREEN_OPEN] = {COK_SCREENS, 1, 1, 38, 22, LINES(death_knights_frame)},
                    [COK_SCREEN_ADVENTURE] = {COK_SCREEN_FRAME, 0, 0, 0, 0,
                                              LINES(death_knights_adventure)},
                    [COK_SCREEN_BIG] = {COK_SCREEN_FRAME, 0, 0, 0, 0,
                                        LINES(death_knights_row_16)},
                    [COK_SCREEN_SPELLS] = {COK_SCREENS, 1, 1, 38, 16,
                                           LINES(death_knights_spells)},
                    [COK_SCREEN_LIST] = {COK_SCREEN_FRAME, 0, 0, 0, 0,
                                         LINES(death_knights_list)},
                    [COK_SCREEN_COMBAT] = {COK_SCREENS, 0, 0, 39, 23,
                                           LINES(death_knights_combat)},
                    [COK_SCREEN_SHEET] = {COK_SCREENS, 0, 0, 39, 23,
                                          LINES(death_knights_sheet)},
                    /* 35c6:026e: 110f:0000 with 0, then its own text. */
                    [COK_SCREEN_CREDITS] = {COK_SCREEN_FRAME, 0, 0, 0, 0, NULL, 0},
                },
            .menu_big = false,                  /* 1000:01d1 */
            .resumed = COK_RESUMED_BY_MODE, /* 49cb:010b */
        },
    .start =
        {
            /* 35c6:051c: on a cleared screen 1 at 0, 0 for 5 seconds; 2 at 0, 0 on a cleared
             * screen for 5; 3 at 0, 11 for 5; 4 there for 20; then the
             * credits whatever was pressed, 35c6:0000 with 10000 seconds,
             * whose hundredths it multiplies into a word: 16960. */
            .pictures = {{1, 0, 0, true, 5}, {2, 0, 0, true, 5}, {3, 0, 11, false, 5},
                         {4, 0, 11, false, 20}},
            .picture_count = 4,
            .credits = death_knights_credit_lines,
            .credit_count =
                sizeof death_knights_credit_lines / sizeof *death_knights_credit_lines,
            .credits_wait = 16960,
            .credits_always = true,
            .first_block = 0x10, /* 3632:2e06 */
            .demo_file = 5,      /* 3632:2e06: ECL5, which it does not ship */
            .demo_block = 2,
            /* DS:0cf8 */
            .items = {"Create New Character", "Drop Character", "Modify Character",
                      "Train Character", "Knight Change Order", "View Character",
                      "Add Character to Party", "Remove Character from Party", "Load Saved Game",
                      "Save Current Game", "Begin Adventuring", "Initialize Mouse/Joystick",
                      "Exit to DOS"},
            .prompt = "",       /* DS:2fad */
            .select = "Select", /* DS:31d6 */
            /* Creating and modifying a character, training and the
             * knights' orders are rewritten in Death Knights (step 3 and a
             * part of their own); Initialize Mouse/Joystick has its own
             * words ("Init mouse/joystick: ", DS:3038; "mouse on", 3376...)
             * and rewrites krynn.cfg (49cb:52bd), not DKK.CFG. */
            .unported = {[0] = "dkk 49cb:05c7", [2] = "dkk 49cb:25d4", [3] = "dkk 49cb:43b6",
                         [4] = "dkk 49cb:4ea4", [11] = "dkk 49cb:52cd"},
            .pick_prompt = "Pick Character ", /* 580d:3067: DS:3018, 31d6 */
            .pick_menu = "Select",
            /* 49cb:2394: DS:130a-135f */
            .drop = "drop %s forever ",
            .drop_sure = "Are you sure? ",
            .dumped = "%s is dumped out back.",
            .farewell = "%s bids you farewell.",
            .relief = "%s breathe a sigh of relief.",
            .quit = "Quit to DOS ", /* DS:124c */
            .unsaved = "game not saved. quit anyway? ", /* DS:1261 */
            .ask_colour = 13, /* 6a69:2048 */
            .load_prompt = "load which game: ", /* DS:3081 */
            .save_prompt = "save which game: ", /* DS:3096 */
            .save_warning = "warning: problem saving characters", /* DS:2610 */
            .load_from = "load from where? ", /* DS:306c */
            .add_from = "add from where? ",   /* DS:1393 */
            .from_items = "Death Champions Exit", /* DS:3337 */
            .overwrite = "overwrite %s? ", /* DS:164e, 1659 */
            .new_name = "new file name : ", /* DS:1664 */
            .add_prompt = "add a character: ", /* DS:13a8 */
            .add_select = "Add",               /* DS:3356 */
            .rangers = "too many rangers in your party", /* DS:1523 */
            .paladins = false,
            .add_space_leaves = false,
            .add_loading = false,
            .add_unmarks = false,
            .reset_when_empty = true, /* 49cb:010b */
            .quit_after_save = true,
            .begin_last_mode = true,
            .program_marks = true,
            .reset_wall = 1, /* 4f9a:08ad */
        },
};
