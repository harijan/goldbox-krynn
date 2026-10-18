#include "ecl.h"
#include "rules.h"


/* Death Knights of Krynn (its START.EXE and GAME.OVR; "dkk" addresses are
 * build/death-START_FULL.EXE's, its data segment DS 0x1bce). Read
 * headless so far (DEATH-KNIGHTS.md's step 1): its files, its ECL's
 * opcodes and byte variables, its pictures and wall sets by id and its 3D
 * view. What step 1 does not reach is left out: the character record's
 * layout and the data segment's tables are empty, so that any read of a
 * field or a table stops at its accessor's assertion ("the game's records
 * have no such field", "... no such table"), and an archive the game does
 * not have is NULL. */

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

/* Not read yet: the character record (216 bytes in its .GAM, step 2).
 * Every field has width 0. */
static const cok_character_layout death_knights_character = {0};

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
 * one with 0x1f and 0x2c. */
#define OP(o, m, n, k) [o] = {.meaning = m, .operands = n, .skip = k}
#define SAME(m, n, k) OP(m, m, n, k)
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
    SAME(COK_ECL_LOAD_MONSTER, 3, 3),
    OP(0x0c, COK_ECL_SETUP_MONSTERS, 4, 3), /* 3632:038f */
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
    SAME(COK_ECL_COMBAT, 0, 0),
    LISTED(COK_ECL_ON_GOTO, COK_ECL_ON_GOTO, 2, 0),
    LISTED(COK_ECL_ON_GOSUB, COK_ECL_ON_GOSUB, 2, 0),
    LISTED(0x27, COK_ECL_TREASURE_ITEMS, 4, 0), /* 3632:1933 */
    SAME(COK_ECL_ROB, 3, 3),
    OP(0x29, COK_ECL_NOTHING, 0, 0), /* 3632:28c9 */
    SAME(COK_ECL_GETTABLE, 3, 3),
    LISTED(COK_ECL_HORIZONTAL_MENU, COK_ECL_HORIZONTAL_MENU, 2, 0),
    OP(0x2c, COK_ECL_MUSIC, 1, 1), /* 3632:2878 */
    SAME(COK_ECL_CALL, 1, 1),
    SAME(COK_ECL_DAMAGE, 5, 5),
    SAME(COK_ECL_AND, 3, 3),
    SAME(COK_ECL_OR, 3, 3),
    LISTED(0x31, COK_ECL_ASK_MENU, 2, 0), /* 3632:11fd with 1 */
    SAME(COK_ECL_FIND_ITEM, 1, 1),
    SAME(COK_ECL_PRINT_RETURN, 0, 0),
    SAME(COK_ECL_ECL_CLOCK, 2, 1),
    SAME(COK_ECL_SAVE_TABLE, 3, 3),
    SAME(COK_ECL_ADD_NPC, 2, 1),
    SAME(COK_ECL_LOAD_PIECES, 3, 3),
    SAME(COK_ECL_PROGRAM, 1, 1),
    SAME(COK_ECL_WHO, 1, 1),
    SAME(COK_ECL_DELAY, 0, 0),
    SAME(COK_ECL_SPELL, 3, 3),
    OP(0x3c, COK_ECL_STOCK, 1, 1), /* 3632:28d2 */
    SAME(COK_ECL_CLEAR_BOX, 0, 0),
    SAME(COK_ECL_DUMP, 0, 0),
    SAME(COK_ECL_FIND_SPECIAL, 1, 1),
    SAME(COK_ECL_DESTROY_ITEMS, 1, 1),
    SAME(COK_ECL_ADD_EP, 2, 2),
};

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
