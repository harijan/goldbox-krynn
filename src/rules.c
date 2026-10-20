#include "rules.h"

#include "data_champions.h"
#include "ecl.h"

#include <stdio.h>
#include <string.h>

/* Champions of Krynn (START.EXE and GAME.OVR, START_FULL.EXE's addresses). */

/* The first digit of the file's number, as Str with a width of 1 makes it
 * (4b6d:161b, 2fd3:1d21, 6961:05b9, 6d21:01d0). */
static char champions_file_digit(unsigned file)
{
    char text[16];
    snprintf(text, sizeof text, "%u", file);
    return text[0];
}

static const char *const champions_deltas[] = {"PIC1", "PIC2", "PIC3", NULL};
static const char *const champions_collections[] = {"SPRIT1", "SPRIT2", "SPRIT3", NULL};
static const char *const champions_singles[] = {
    "TITLE", "BIGPIC1", "BIGPIC2", "BIGPIC3", "CPIC1", "CPIC2", "CPIC3", "CBODY", "CHEAD",
    "BODY2", "BODY3", "HEAD2", "HEAD3", "COMSPR", "CURSOR", "SKY", "TILES", "8X8D1", "8X8D2",
    "8X8D3", NULL};

/* The character record's fields (field.h): a single value, an array of
 * count from index first, or a table of rows; signed, or a far pointer. */
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

/* Champions' record, 409 bytes, the original's party list's from DS:609a
 * (a linked list, next at +0x17f; the port's cok_party is an array in the
 * same order), as its files hold it. The derived fields are recomputed when
 * a character loads, by 6346:0d20 (items, abilities) and then 66c2:0433
 * (classes and levels): see party.c. No two fields share a byte but where
 * said (tests/test_layout.c). */
static const cok_character_layout champions_character = {
    .size = COK_CHARACTER_SIZE,
    .file_size = COK_CHARACTER_SIZE, /* 4b6d:11e5 reads, 4b6d:0bed writes it whole */
    .field =
        {
            /* A Pascal string[15] (169c:05da makes its file's name). */
            [COK_FIELD_NAME] = BYTES(0x000, 16, 0),
            /* Each score two bytes, the base then the current: strength
             * (+0x11 current), intelligence, wisdom (+0x15), dexterity
             * (+0x17), constitution (+0x19) and charisma (+0x1b);
             * exceptional strength current, then base (6346:137a). */
            [COK_FIELD_SCORE_BASE] = INTERLEAVED(0x010, 6, 2),
            [COK_FIELD_SCORE] = INTERLEAVED(0x011, 6, 2),
            [COK_FIELD_STRENGTH_EXCEPTIONAL] = BYTE(0x01c),
            [COK_FIELD_STRENGTH_EXCEPTIONAL_BASE] = BYTE(0x01d),
            /* 58 spell ids, 0x80 added while marked to memorize (2fd3:318b,
             * 475c:0683). */
            [COK_FIELD_MEMORIZED] = BYTES(0x01e, 0x3a, 0),
            /* The hours of rest that memorizing and scribing need, 0 once
             * none are marked (4888:05fd); read signed when counted down. */
            [COK_FIELD_STUDY_HOURS] = SBYTE(0x058),
            /* 60 - THAC0, the best of the classes' (66c2:0433). */
            [COK_FIELD_BASE_THAC0] = BYTE(0x059),
            /* 6 human. Signed here and below where the original reads it
             * so: 6346:0d20's race < 7, the sheet's names (546c:00a3),
             * the ECL's signed fields (ecl.c's kind 1). */
            [COK_FIELD_RACE] = SBYTE(0x05a),
            /* The class or combination, as character creation numbers
             * them (4def:06dd; its name DS:0f5a). */
            [COK_FIELD_CLASS] = SBYTE(0x05b),
            [COK_FIELD_KNIGHT_ORDER] = BYTE(0x05c),
            [COK_FIELD_DEITY] = SBYTE(0x05d),
            [COK_FIELD_MAGIC_ORDER] = BYTE(0x05e),
            /* (knight level + former knight level - 1) / 5 + 1, set when
             * effect 0x6e ends (3f44:3643). */
            [COK_FIELD_KNIGHT_TIER] = BYTE(0x05f),
            /* In years. */
            [COK_FIELD_AGE] = WORD(0x060),
            [COK_FIELD_MAX_HIT_POINTS] = BYTE(0x062),
            /* Set when spell id is known, +0x62 + id (66c2:000f); the byte of
             * spell 108 is also FIGHTER_LEVEL's. */
            [COK_FIELD_KNOWN_SPELL] = BYTES(0x063, 108, 1),
            /* The highest fighter, ranger or knight level, or 1 (6346:0d20). */
            [COK_FIELD_FIGHTER_LEVEL] = BYTE(0x0ce),
            /* Its size in combat: bits 0-2 its footprint (6beb:10f3 places it
             * by & 0x7f, 3cb2:17f7 by & 7), bit 7 a large creature (a weapon's
             * dice for large ones, 432f:1579; no backstab, 432f:2af0); 1 a
             * man's, which character creation gives (4def:06dd). */
            [COK_FIELD_SIZE] = BYTE(0x0cf),
            /* By type (66c2:08a6). */
            [COK_FIELD_SAVING_THROW] = BYTES(0x0d0, 5, 0),
            [COK_FIELD_BASE_MOVEMENT] = BYTE(0x0d5),
            /* The highest level of any class (66c2:0433). */
            [COK_FIELD_HIGHEST_LEVEL] = SBYTE(0x0d6),
            /* A human's level when it changed class, which its new class's
             * level must pass for it to use its former class (66c2:0efb). */
            [COK_FIELD_CHANGE_LEVEL] = SBYTE(0x0d7),
            /* A byte shared out over a count of levels: each class that
             * trains while the count is not 0 takes its share off it,
             * +0xd9 - +0xd9 / +0xd8, and counts one down (4def:4d9e).
             * Nothing else in START or the ECL reads or writes either, and
             * no monster or saved character sets them; Death Knights'
             * import drops both. */
            [COK_FIELD_SPREAD_LEVELS] = BYTE(0x0d8),
            [COK_FIELD_SPREAD] = BYTE(0x0d9),
            /* A monster's kind of undead, 0 for none: turning picks the
             * highest (432f:14a2) and reads its row of the table at DS:0444,
             * 13 a kind (432f:12b7, signed); a cloud does not choke one
             * (60f4:0dc3). */
            [COK_FIELD_UNDEAD] = SBYTE(0x0da),
            /* Eight, by skill 1-8 (66c2:0b9f). */
            [COK_FIELD_THIEF_SKILL] = BYTES(0x0db, 8, 1),
            /* The spell effects (see cok_effect). */
            [COK_FIELD_EFFECTS] = POINTER(0x0e3),
            /* 0x80 and up for NPCs; 0xb3 for a player character an effect
             * has turned. */
            [COK_FIELD_NPC] = BYTE(0x0e7),
            /* An NPC's shares of the coins after combat, bits 0-2, and,
             * set, it says it hides its share (351b:1618); Modify's Keep
             * sets it to 1 (4def:35ea), as every saved character has it. */
            [COK_FIELD_SHARES] = BYTE(0x0e8),
            /* Silver, copper, bronze, platinum, steel, gems, jewelry
             * (COK_COINS); 6346:0d20 weighs 1-6. */
            [COK_FIELD_COINS] = WORDS(0x0eb, 7, 0),
            /* Levels in each of eight classes: 0 cleric, 2 fighter, 4
             * ranger, 5 mage, 6 thief, 7 knight; and in the classes a human
             * had before it changed class, the same way (66c2:0433). */
            [COK_FIELD_CLASS_LEVEL] = SBYTES(0x0f9, 8, 0),
            [COK_FIELD_FORMER_LEVEL] = SBYTES(0x101, 8, 0),
            [COK_FIELD_GENDER] = SBYTE(0x109),
            /* 0-8: 2, 5 and 8 evil; 0, 3 and 6 good. */
            [COK_FIELD_ALIGNMENT] = SBYTE(0x10a),
            /* Two attacks' base: attacks a round, doubled (3 at high
             * fighter, ranger and knight levels, 66c2:0433), dice, their
             * sides and the damage bonus, which 6346:0d20 copies to
             * DICE, DICE_SIDES and DAMAGE_BONUS. */
            [COK_FIELD_BASE_ATTACKS] = BYTES(0x10b, 2, 1),
            [COK_FIELD_BASE_DICE] = BYTES(0x10d, 2, 1),
            [COK_FIELD_BASE_DICE_SIDES] = BYTES(0x10f, 2, 1),
            [COK_FIELD_BASE_DAMAGE_BONUS] = SBYTES(0x111, 2, 1),
            /* 60 - AC. */
            [COK_FIELD_BASE_AC] = BYTE(0x113),
            /* Set when strength adds to hit and damage (6346:1412). */
            [COK_FIELD_STRENGTH_BONUSES] = BYTE(0x114),
            /* The id of the monster an NPC was read as (4b6d:18fc), whose
             * combat icons, its file's CPIC records id and id + 0x80, it is
             * shown with (6d21:01d0); a random byte for a player character
             * (4def:06dd). */
            [COK_FIELD_MONSTER_ID] = BYTE(0x115),
            [COK_FIELD_EXPERIENCE] = DWORD(0x116),
            /* The classes whose items it can use, as bits (DS:38ea). */
            [COK_FIELD_USABLE_CLASSES] = BYTE(0x11a),
            /* The hit points the maximum is built from, with each class's
             * levels and the constitution's bonus: a first level's, which
             * training adds to (train.c), Modify sets (4def:28fa) and the
             * constitution's recompute starts from (effect.c). Unrelated to
             * the spells a day's +0x11b + class * 5 + level, which is the
             * original's base for the table after it. */
            [COK_FIELD_BASE_HIT_POINTS] = BYTE(0x11b),
            /* Spells a day by spell class (0 cleric, 1 druid, 2 a deity's
             * granted power, 3 magic-user) and spell level 1-5 (66c2:000f),
             * +0x11b + class * 5 + level. */
            [COK_FIELD_SPELLS_A_DAY] =
                {.offset = 0x11c, .width = 1, .count = 5, .first = 1, .stride = 1, .rows = 4,
                 .row_stride = 5},
            /* A monster's experience for the party that defeats it, +0x130
             * + +0x132 * BASE_HIT_POINTS (351b:0037); the ECL reads and
             * writes both (3775:07d9, 3775:0c00). */
            [COK_FIELD_MONSTER_EXPERIENCE] = WORD(0x130),
            [COK_FIELD_MONSTER_HIT_POINT_EXPERIENCE] = BYTE(0x132),
            /* A player character's combat icon, composed by 4b6d:0817: its
             * head and body (records of CHEAD and CBODY, with the size's
             * letter), its size (1 small, 2 tall), and colours 1-6, each byte
             * two (the low and high nibble; 4def:408b edits them). */
            [COK_FIELD_ICON_HEAD] = BYTE(0x135),
            [COK_FIELD_ICON_BODY] = BYTE(0x136),
            /* The combat icon's slot, 0-7 (4b6d:1989). */
            [COK_FIELD_ICON] = BYTE(0x137),
            [COK_FIELD_ICON_SIZE] = BYTE(0x138),
            [COK_FIELD_ICON_COLOURS] = BYTES(0x139, 6, 1),
            /* Creature flags that effects test, two bytes (+0x140's bit 0
             * too, 3f44:26bf). */
            [COK_FIELD_CREATURE_FLAGS] = BYTES(0x13f, 2, 0),
            /* The items: their number, the list, and the readied ones'
             * slots (6346:0d20). */
            [COK_FIELD_ITEM_COUNT] = BYTE(0x142),
            [COK_FIELD_ITEMS] = POINTER(0x143),
            [COK_FIELD_ITEM_SLOTS] = POINTERS(0x147, 13),
            [COK_FIELD_HANDS] = BYTE(0x17b),
            [COK_FIELD_SAVE_BONUS] = SBYTE(0x17c),
            [COK_FIELD_WEIGHT] = WORD(0x17d),
            [COK_FIELD_NEXT] = POINTER(0x17f),
            /* The combat record (cok_combat_record). */
            [COK_FIELD_COMBAT] = POINTER(0x183),
            [COK_FIELD_MAGIC_RESISTANCE] = BYTE(0x187),
            /* 0 okay, 4 unconscious, 5 dying, 6 dead. */
            [COK_FIELD_STATUS] = SBYTE(0x188),
            /* Set while the character can act; cleared when it drops. */
            [COK_FIELD_ACTIVE] = BYTE(0x189),
            /* 1 for a character fighting against the party. */
            [COK_FIELD_HOSTILE] = BYTE(0x18a),
            /* Set while the computer controls it. */
            [COK_FIELD_COMPUTER] = BYTE(0x18b),
            /* 60 - THAC0, 60 - AC, and from behind (6346:0d20). */
            [COK_FIELD_THAC0] = BYTE(0x18c),
            [COK_FIELD_AC] = BYTE(0x18d),
            [COK_FIELD_AC_BEHIND] = BYTE(0x18e),
            /* Two attacks' attacks left this round: the weapon's (432f:0e80,
             * from BASE_ATTACKS 1 or a missile's rate) and the others'
             * (432f:0000, from BASE_ATTACKS 2), each halved by the round
             * (432f:0f9e); a blow takes one (432f:1579, +0x18e + slot). */
            [COK_FIELD_ATTACKS_LEFT] = BYTES(0x18f, 2, 1),
            /* Two attacks' dice, sides and damage bonus (6346:0023). */
            [COK_FIELD_DICE] = BYTES(0x191, 2, 1),
            [COK_FIELD_DICE_SIDES] = BYTES(0x193, 2, 1),
            [COK_FIELD_DAMAGE_BONUS] = SBYTES(0x195, 2, 1),
            [COK_FIELD_HIT_POINTS] = BYTE(0x197),
            [COK_FIELD_MOVEMENT] = BYTE(0x198),
        },
};
_Static_assert(COK_CHARACTER_SIZE <= COK_CHARACTER_MAX, "Champions' record fits the buffer");

/* The ECL's opcodes (ecl.h): what each does, the operands its handler
 * loads (2fd3:0050-36dc, 3f44's for the arithmetic; none unless given) and
 * the operands a failed IF skips with it (3775:1e74). VERTICAL MENU loads a
 * header of 3 (its prompt, its result's address, the count), ON GOTO, ON
 * GOSUB and HORIZONTAL MENU of 2, then the list over it (2fd3:0f9d,
 * 2fd3:1c69, 2fd3:116d); 0x1f and PROTECTION never advance. A failed IF
 * skips none for the lists and opcodes without handlers, and one for ECL
 * CLOCK, ADD NPC and PROTECTION, whatever their handlers load. */
#define OP(m, n, k) [m] = {.meaning = m, .operands = n, .skip = k}
#define LISTED(m, h) [m] = {.meaning = m, .operands = COK_ECL_LIST, .header = h}
static const cok_rules_opcode champions_opcodes[COK_ECL_OPCODES] = {
    OP(COK_ECL_EXIT, 0, 0),
    OP(COK_ECL_GOTO, 1, 1),
    OP(COK_ECL_GOSUB, 1, 1),
    OP(COK_ECL_COMPARE, 2, 2),
    OP(COK_ECL_ADD, 3, 3),
    OP(COK_ECL_SUBTRACT, 3, 3),
    OP(COK_ECL_DIVIDE, 3, 3),
    OP(COK_ECL_MULTIPLY, 3, 3),
    OP(COK_ECL_RANDOM, 2, 2),
    OP(COK_ECL_SAVE, 2, 2),
    OP(COK_ECL_LOAD_CHARACTER, 1, 1),
    OP(COK_ECL_LOAD_MONSTER, 3, 3),
    OP(COK_ECL_SETUP_MONSTER, 3, 3),
    OP(COK_ECL_APPROACH, 0, 0),
    OP(COK_ECL_PICTURE, 1, 1),
    OP(COK_ECL_INPUT_NUMBER, 2, 2),
    OP(COK_ECL_INPUT_STRING, 2, 2),
    OP(COK_ECL_PRINT, 1, 1),
    OP(COK_ECL_PRINTCLEAR, 1, 1),
    OP(COK_ECL_RETURN, 0, 0),
    OP(COK_ECL_COMPARE_AND, 4, 4),
    LISTED(COK_ECL_VERTICAL_MENU, 3),
    OP(COK_ECL_IF_EQUAL, 0, 0),
    OP(COK_ECL_IF_NOT_EQUAL, 0, 0),
    OP(COK_ECL_IF_LESS, 0, 0),
    OP(COK_ECL_IF_GREATER, 0, 0),
    OP(COK_ECL_IF_LESS_EQUAL, 0, 0),
    OP(COK_ECL_IF_GREATER_EQUAL, 0, 0),
    OP(COK_ECL_CLEARMONSTERS, 0, 0),
    OP(COK_ECL_PARTYSTRENGTH, 1, 1),
    OP(COK_ECL_CHECKPARTY, 6, 6),
    OP(COK_ECL_UNUSED_1F, COK_ECL_HANG, 2),
    OP(COK_ECL_NEWECL, 1, 1),
    OP(COK_ECL_LOAD_FILES, 3, 3),
    OP(COK_ECL_PARTY_SURPRISE, 2, 2),
    OP(COK_ECL_SURPRISE, 4, 4),
    OP(COK_ECL_COMBAT, 0, 0),
    LISTED(COK_ECL_ON_GOTO, 2),
    LISTED(COK_ECL_ON_GOSUB, 2),
    OP(COK_ECL_TREASURE, 8, 8),
    OP(COK_ECL_ROB, 3, 3),
    OP(COK_ECL_ENCOUNTER_MENU, 14, 14),
    OP(COK_ECL_GETTABLE, 3, 3),
    LISTED(COK_ECL_HORIZONTAL_MENU, 2),
    OP(COK_ECL_PARLAY, 6, 6),
    OP(COK_ECL_CALL, 1, 1),
    OP(COK_ECL_DAMAGE, 5, 5),
    OP(COK_ECL_AND, 3, 3),
    OP(COK_ECL_OR, 3, 3),
    OP(COK_ECL_SPRITE_OFF, 0, 0),
    OP(COK_ECL_FIND_ITEM, 1, 1),
    OP(COK_ECL_PRINT_RETURN, 0, 0),
    OP(COK_ECL_ECL_CLOCK, 2, 1),
    OP(COK_ECL_SAVE_TABLE, 3, 3),
    OP(COK_ECL_ADD_NPC, 2, 1),
    OP(COK_ECL_LOAD_PIECES, 3, 3),
    OP(COK_ECL_PROGRAM, 1, 1),
    OP(COK_ECL_WHO, 1, 1),
    OP(COK_ECL_DELAY, 0, 0),
    OP(COK_ECL_SPELL, 3, 3),
    OP(COK_ECL_PROTECTION, COK_ECL_HANG, 1),
    OP(COK_ECL_CLEAR_BOX, 0, 0),
    OP(COK_ECL_DUMP, 0, 0),
    OP(COK_ECL_FIND_SPECIAL, 1, 1),
    OP(COK_ECL_DESTROY_ITEMS, 1, 1),
    OP(COK_ECL_ADD_EP, 2, 2),
};

/* The variables by name (ecl.h's cok_var): each a word at its address
 * (3775:0e06), read and written where the comments of ecl.h's names say;
 * the moon of an order and the area's first 256 by a byte index
 * (effect.c, magic.c). */
#define VAR(a) {.address = a, .width = 2, .count = 1}
#define VARS(a, n) {.address = a, .width = 2, .count = n}
static const cok_rules_var champions_vars[COK_VARS] = {
    [COK_VAR_OVERLAND_X] = VAR(0x4bc3),
    [COK_VAR_OVERLAND_Y] = VAR(0x4bc4),
    [COK_VAR_MAP] = VAR(0x4bc5),
    [COK_VAR_CLOCK] = VARS(0x4bc6, COK_CLOCK_UNITS), /* 57e4:0549 */
    [COK_VAR_NO_MAGIC] = VAR(0x4be5),
    [COK_VAR_3D] = VAR(0x4be6),
    [COK_VAR_PIECES] = VARS(0x4be7, 2),
    [COK_VAR_LAST_X] = VAR(0x4bf0),
    [COK_VAR_LAST_Y] = VAR(0x4bf1),
    [COK_VAR_BLOCK] = VAR(0x4bf2),
    [COK_VAR_HIDDEN] = VAR(0x4bfb),
    [COK_VAR_SPEED] = VAR(0x4bfc),
    [COK_VAR_SKY_LOW] = VAR(0x4bfd), /* 6945:00ba */
    [COK_VAR_SKY_HIGH] = VAR(0x4bfe),
    [COK_VAR_PICTURES] = VAR(0x4bff),
    [COK_VAR_LOCALS] = VARS(0x4c00, 0x20), /* 3775:01e8 */
    [COK_VAR_SAVINGS] = VAR(0x4c33), /* ECL1's bank (89a2) */
    [COK_VAR_OFF_MAP] = VAR(0x4c38),
    [COK_VAR_NO_VIEW] = VAR(0x4c38), /* 2fd3:3403, 6346:2c17's mode 4 */
    [COK_VAR_SAVES] = VAR(0x4c3c), /* 4888:2c31 */
    [COK_VAR_DIFFICULTY] = VAR(0x4cf4),
    [COK_VAR_BANK] = VAR(0x4cf5),
    [COK_VAR_OVERLAND] = VAR(0x4cf7), /* 475c:09ec */
    [COK_VAR_TREASURE_FIRST] = VAR(0x4cf8),
    [COK_VAR_MOONS] = VARS(0x4cf9, 3), /* 57e4:001e */
    [COK_VAR_MOON_DAYS] = VARS(0x4cfc, 3),
    [COK_VAR_TURN] = VAR(0x4cff), /* 6346:2d75 */
    [COK_VAR_ORDER_MOON] = VARS(0x4cf8, 0x100),
    [COK_VAR_AREA] = VARS(0x4b00, 0x100),
    [COK_VAR_HALL] = VAR(0x7ea8),
    [COK_VAR_DISTANCE_WANTED] = VAR(0x7ec0),
    [COK_VAR_DISTANCE] = VAR(0x7ec1),
    [COK_VAR_MORALE] = VAR(0x7ec6),
    [COK_VAR_OUTCOME] = VAR(0x7ec7),
    [COK_VAR_DROPPED] = VAR(0x7ec8),
    [COK_VAR_STAY] = VAR(0x7ec9),
    [COK_VAR_SEARCH] = VAR(0x7eca),
    [COK_VAR_SURPRISE] = VAR(0x7ecb),
    [COK_VAR_REST_EVERY] = VAR(0x7ed2),
    [COK_VAR_REST_CHANCE] = VAR(0x7ed3),
    [COK_VAR_EDGE] = VAR(0x7ed5),
    [COK_VAR_PORTRAIT] = VAR(0x7ee1),
    [COK_VAR_TEMPLE] = VAR(0x7ee2),
    [COK_VAR_KEEP_ITEMS] = VAR(0x7ee3),
    [COK_VAR_GENTLE] = VAR(0x7ee6),
    [COK_VAR_ECL_FILE] = VAR(0x7f12),
    [COK_VAR_FLED] = VAR(0x7f33),
    [COK_VAR_PARTY_SIZE] = VAR(0x7f3e),
    [COK_VAR_REMAINDER] = VAR(0x7f3f), /* 3f44:0a8a */
    [COK_VAR_SHOP] = VAR(0x7f6c),
    [COK_VAR_PRICES] = VAR(0x7f6d),
    [COK_VAR_HIT_BONUS] = VARS(0x7f70, 2),
    [COK_VAR_MOVE_BONUS] = VAR(0x7f72),
    [COK_VAR_LOCALS_GAME] = VARS(0x7f79, 10), /* 3775:01e8 */
};

/* A block's start (3775:01e8): no portrait, no encounters while resting,
 * magic allowed. */
static const struct cok_rules_var_value champions_start[] = {
    {COK_VAR_PORTRAIT, 0xff},
    {COK_VAR_REST_EVERY, 0},
    {COK_VAR_REST_CHANCE, 0},
    {COK_VAR_NO_MAGIC, 0},
};

/* The special addresses (3775:0fca reads, 3775:0e06 stores; others outside
 * the variables' stores read 0 and take nothing). */
static const cok_rules_special champions_reads[] = {
    {0x4bea, COK_ECL_SPECIAL_FACING}, {0x00b1, COK_ECL_SPECIAL_B1},
    {0x00fb, COK_ECL_SPECIAL_FB},     {0x00fc, COK_ECL_SPECIAL_FC},
    {0x033d, COK_ECL_SPECIAL_FACING}, {0xc04b, COK_ECL_SPECIAL_X},
    {0xc04c, COK_ECL_SPECIAL_Y},      {0xc04d, COK_ECL_SPECIAL_STEPS},
    {0xc04e, COK_ECL_SPECIAL_AHEAD},  {0xc04f, COK_ECL_SPECIAL_SQUARE},
};
static const cok_rules_special champions_stores[] = {
    {0x03de, COK_ECL_SPECIAL_3DE},   {0x00b8, COK_ECL_SPECIAL_B8},
    {0x00b9, COK_ECL_SPECIAL_B9},    {0xc04b, COK_ECL_SPECIAL_X},
    {0xc04c, COK_ECL_SPECIAL_Y},     {0xc04d, COK_ECL_SPECIAL_STEPS},
    {0xc059, COK_ECL_SPECIAL_C059},  {0xc05f, COK_ECL_SPECIAL_C05F},
};

/* The selected character's fields at 0x7c00-0x7fff (3775:07d9 reads,
 * 3775:0c00 writes): in the record +0x13, +0x19, +0x1b, +0x5c, +0x5a,
 * +0x5b, +0x62, +0xd1, +0xd6, +0xdb-+0xe2, +0xe7, +0xeb-+0xf3, +0xfb,
 * +0x100, +0xfd, +0x109, +0x10a, +0x130, +0x132, +0x197 and +0x198; the
 * spells memorized, written; the status, the side, the charisma's
 * reaction and the ECL file worked out. */
#define FIELD(o, r, w, f, i) {.first = o, .last = o, .read = r, .write = w, .field = f, .index = i}
#define READ(o, k, f, i) FIELD(o, COK_ECL_FIELD_##k, COK_ECL_FIELD_NONE, f, i)
#define BOTH(o, k, f, i) FIELD(o, COK_ECL_FIELD_##k, COK_ECL_FIELD_##k, f, i)
#define SPECIAL(o, r, w) FIELD(o, COK_ECL_FIELD_##r, COK_ECL_FIELD_##w, 0, 0)
static const cok_rules_character_var champions_character_vars[] = {
    SPECIAL(0x000, NONE, CLEARED),
    {.first = 0x020, .last = 0x059, .write = COK_ECL_FIELD_U8, .field = COK_FIELD_MEMORIZED,
     .step = 1},
    READ(0x015, U8, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE),
    READ(0x018, U8, COK_FIELD_SCORE, COK_SCORE_CONSTITUTION),
    READ(0x019, U8, COK_FIELD_SCORE, COK_SCORE_CHARISMA),
    READ(0x061, U8, COK_FIELD_KNIGHT_ORDER, 0),
    READ(0x072, S8, COK_FIELD_RACE, 0),
    READ(0x073, S8, COK_FIELD_CLASS, 0),
    READ(0x076, U8, COK_FIELD_MAX_HIT_POINTS, 0),
    READ(0x09b, U8, COK_FIELD_SAVING_THROW, 1),
    READ(0x0a0, S8, COK_FIELD_HIGHEST_LEVEL, 0),
    /* The thief skills 1-8. */
    {.first = 0x0a5, .last = 0x0ac, .read = COK_ECL_FIELD_U8, .field = COK_FIELD_THIEF_SKILL,
     .index = 1, .step = 1},
    FIELD(0x0b8, COK_ECL_FIELD_U8, COK_ECL_FIELD_NPC, COK_FIELD_NPC, 0),
    BOTH(0x0bb, U16, COK_FIELD_COINS, 0),
    BOTH(0x0bd, U16, COK_FIELD_COINS, 1),
    BOTH(0x0bf, U16, COK_FIELD_COINS, 2),
    BOTH(0x0c1, U16, COK_FIELD_COINS, 3),
    BOTH(0x0c3, U16, COK_FIELD_COINS, 4),
    SPECIAL(0x0c9, HOST, NONE),
    READ(0x0cc, S8, COK_FIELD_CLASS_LEVEL, 2),
    READ(0x0cd, S8, COK_FIELD_CLASS_LEVEL, 7),
    READ(0x0d0, S8, COK_FIELD_CLASS_LEVEL, 4),
    READ(0x0d6, S8, COK_FIELD_GENDER, 0),
    READ(0x0d8, S8, COK_FIELD_ALIGNMENT, 0),
    BOTH(0x0f7, U16, COK_FIELD_MONSTER_EXPERIENCE, 0),
    BOTH(0x0f9, U8, COK_FIELD_MONSTER_HIT_POINT_EXPERIENCE, 0),
    SPECIAL(0x100, STATUS, REVIVE),
    SPECIAL(0x10c, SIDE, SIDE),
    SPECIAL(0x10d, ZERO, NONE),
    BOTH(0x119, U8, COK_FIELD_HIT_POINTS, 0),
    READ(0x11b, U8, COK_FIELD_MOVEMENT, 0),
    SPECIAL(0x2b1, HOST, NONE),
    SPECIAL(0x2b4, HOST, NONE),
    SPECIAL(0x2cf, CHARISMA, NONE),
    SPECIAL(0x312, FILE, FILE),
    SPECIAL(0x33e, VARIABLE, NONE),
};
#undef FIELD
#undef READ
#undef BOTH
#undef SPECIAL

/* The data segment's tables (data.h): the base the original's arithmetic
 * adds to, each element's width, and each index's first, count and stride
 * (an innermost index's stride is the width). A table's own bytes are its
 * elements' unless given (_IN: DS:from on, size of them). */
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

/* Champions' data segment (DS 0x1bc6 in START_FULL.EXE): the tables the
 * port holds, as data_champions.c copies them: those with a stride of 1
 * by DS offset (the lookup's search needs the order), then the columns of
 * the spell table (a stride of 16). Those with a stride of 1 cover
 * DS:31b3-43bf without a gap, the lowest the stat routines' indices reach
 * to the end of the initialized data. Some overlap, where one table's copy
 * runs on into the next (the steps into the terrain, the terrain into the
 * footprints, the cone's sides into the battle's effects); test_data
 * checks that they agree, and every byte against the executable. */
static const cok_ds_table champions_tables[] = {
    {0x01c6, sizeof cok_champions_diseases, 1, cok_champions_diseases},
    {0x0396, sizeof cok_champions_ways, 1, cok_champions_ways},
    {0x03ba, sizeof cok_champions_moves, 1, &cok_champions_moves[0][0]},
    {0x03ca, sizeof cok_champions_quadrants, 1, &cok_champions_quadrants[0][0]},
    {0x03da, sizeof cok_champions_quadrant_directions, 1, cok_champions_quadrant_directions},
    {0x03de, sizeof cok_champions_start_columns, 1,
     (const uint8_t *)cok_champions_start_columns},
    {0x03e6, sizeof cok_champions_start_rows, 1, (const uint8_t *)cok_champions_start_rows},
    {0x03ee, sizeof cok_champions_formations, 1, (const uint8_t *)cok_champions_formations},
    {0x042a, sizeof cok_champions_beside_x, 1, (const uint8_t *)cok_champions_beside_x},
    {0x042e, sizeof cok_champions_beside_y, 1, (const uint8_t *)cok_champions_beside_y},
    {0x0432, sizeof cok_champions_terrain_flags, 1, cok_champions_terrain_flags},
    {0x0451, sizeof cok_champions_turning, 1, cok_champions_turning},
    {0x0a9a, sizeof cok_champions_alignment_codes, 1, cok_champions_alignment_codes},
    {0x0b6e, sizeof cok_champions_level_dice, 1, cok_champions_level_dice},
    {0x0b76, sizeof cok_champions_die_sides, 1, cok_champions_die_sides},
    {0x0b7e, sizeof cok_champions_knight_scores, 1, cok_champions_knight_scores[0]},
    {0x0b88, sizeof cok_champions_treasure_templates, 1, cok_champions_treasure_templates},
    {0x0d94, sizeof cok_champions_cone_sides, 1, cok_champions_cone_sides},
    {0x0db4, sizeof cok_champions_battle_only, 1, cok_champions_battle_only},
    {0x0f5a, sizeof cok_champions_names, 1, cok_champions_names},
    {0x1dd4, sizeof cok_champions_line_directions, 1, &cok_champions_line_directions[0][0]},
    {0x1e26, sizeof cok_champions_same_colours, 1, cok_champions_same_colours},
    {0x1e36, sizeof cok_champions_black_colours, 1, cok_champions_black_colours},
    /* Read by no name yet: attack.c's SOUND_* are these words as its own
     * constants. Listed so the copy is checked, for the part that names the
     * sound driver's commands (the spells' or the sound's). */
    {0x1e54, sizeof cok_champions_sound_words, 1, cok_champions_sound_words},
    {0x1ed6, sizeof cok_champions_step_x, 1, (const uint8_t *)cok_champions_step_x},
    {0x1edf, sizeof cok_champions_step_y, 1, (const uint8_t *)cok_champions_step_y},
    {0x1ee4, sizeof cok_champions_terrain, 1, &cok_champions_terrain[0][0]},
    {0x1fe4, sizeof cok_champions_footprints, 1, (const uint8_t *)cok_champions_footprints},
    {0x200c, sizeof cok_champions_helpless, 1, cok_champions_helpless},
    {0x2092, sizeof cok_champions_cloud_cells, 1, cok_champions_cloud_cells},
    {0x31b3, sizeof cok_champions_spells, 1, cok_champions_spells},
    {0x3509, sizeof cok_champions_data_3509, 1, cok_champions_data_3509},
    {0x3882, sizeof cok_champions_thac0, 1, cok_champions_thac0},
    {0x38ea, sizeof cok_champions_class_bits, 1, cok_champions_class_bits},
    {0x38f2, sizeof cok_champions_data_38f2, 1, cok_champions_data_38f2},
    {0x3911, sizeof cok_champions_thief_level, 1, cok_champions_thief_level},
    {0x3971, sizeof cok_champions_thief_race, 1, (const uint8_t *)cok_champions_thief_race},
    {0x39a9, sizeof cok_champions_thief_dexterity, 1,
     (const uint8_t *)cok_champions_thief_dexterity},
    {0x39e0, sizeof cok_champions_data_39e0, 1, cok_champions_data_39e0},
    {0x3c99, sizeof cok_champions_spells_a_day, 1, cok_champions_spells_a_day},
    {0x3ff4, sizeof cok_champions_data_3ff4, 1, cok_champions_data_3ff4},
    {0x405b, sizeof cok_champions_saving_throws, 1, cok_champions_saving_throws},
    {0x423b, sizeof cok_champions_cleric_spells, 1, cok_champions_cleric_spells},
    {0x4263, sizeof cok_champions_data_4263, 1, cok_champions_data_4263},
    {0x31c3, sizeof cok_champions_spell_classes, 16, cok_champions_spell_classes},
    {0x31b4, sizeof cok_champions_spell_levels, 16, cok_champions_spell_levels},
};

/* Where each table the code reads lies in Champions' data segment, and
 * the routines of the original that read it. */
static const cok_data_segment champions_data = {
    .tables = champions_tables,
    .table_count = sizeof champions_tables / sizeof *champions_tables,
    .table =
        {
            /* The name strings, DS:0f5a-1dd3: a name read past them fails
             * (the original reads on). Each a string[stride - 1]. */
            [COK_DATA_NAMES] = LIST(0x0f5a, 1, 0, 3706),
            /* The sheet (546c), Pick Class (4def:0ae0), training. */
            [COK_DATA_CLASS_NAMES] = LIST(0x0f5a, 27, 0, 17),
            /* The sheet, Pick Race (4def:07a0). */
            [COK_DATA_RACE_NAMES] = LIST(0x1140, 16, 0, 8),
            /* The sheet, Pick Alignment (4def:1222). */
            [COK_DATA_ALIGNMENT_NAMES] = LIST(0x11c0, 17, 0, 9),
            /* The sheet, Pick God (4def:0f8e). */
            [COK_DATA_DEITY_NAMES] = LIST(0x1259, 12, 0, 8),
            /* The sheet's class line. */
            [COK_DATA_ORDER_NAMES] = LIST(0x12b9, 7, 0, 4),
            /* The sheet, Pick Gender. */
            [COK_DATA_GENDER_NAMES] = LIST(0x12d5, 7, 0, 2),
            /* The sheet's money. */
            [COK_DATA_COIN_NAMES] = LIST(0x12e3, 11, 0, 7),
            /* The sheet, the combat panel (arena.c). */
            [COK_DATA_STATUS_NAMES] = LIST(0x1330, 13, 0, 9),
            /* An item's name (546c), by the words at +0x2f-+0x31; the directions
             * follow (DS:1dbc, 3 bytes a direction). */
            [COK_DATA_ITEM_WORDS] = LIST(0x1390, 21, 1, 123),
            /* DS:31b3 + 16 * spell + column, spell ids 0-0x6b: byte 0 the
             * class, 1 the level, 10 the effect, 11 whether combat ends the
             * turn (66c2:000f, 3afb:03b9, 432f:28bd, 546c:272c, 3f44:1b18). */
            [COK_DATA_SPELLS] = TABLE(0x31b3, 1, 0, 108, 16, 0, 16),
            /* DS:3882 + class * 13 + level, levels 0-12 (66c2:0433,
             * 4def:0ae0). */
            [COK_DATA_THAC0] = TABLE(0x3882, 1, 0, 8, 13, 0, 13),
            /* DS:38ea + class (66c2:0433, 4def:0ae0). */
            [COK_DATA_CLASS_BITS] = LIST(0x38ea, 1, 0, 8),
            /* DS:38f2 + class (4def:4d9e); 9 more bytes follow to DS:3902. */
            [COK_DATA_TRAINING_BITS] = LIST(0x38f2, 1, 0, 8),
            /* DS:3903 + class (4def:4b3a, 4def:35ea, 3f44's hit points). */
            [COK_DATA_TOP_LEVELS] = LIST(0x3903, 1, 0, 8),
            /* DS:390a + k, k 1-6: body, arm, leg, hair or face, shield,
             * weapon (4b6d:0817, 4def:06dd). */
            [COK_DATA_ICON_COLOURS] = LIST(0x390a, 1, 1, 6),
            /* DS:3908 + level * 8 + skill, levels 1-12 (66c2:0b9f). */
            [COK_DATA_THIEF_LEVEL] = TABLE(0x3908, 1, 1, 12, 8, 1, 8),
            /* DS:3970 + race * 8 + skill (66c2:0b9f). */
            [COK_DATA_THIEF_RACE] = TABLE(0x3970, 1, 0, 7, 8, 1, 8),
            /* DS:397b + dexterity * 5 + skill, dexterity 9-19 (66c2:0b9f). */
            [COK_DATA_THIEF_DEXTERITY] = TABLE(0x397b, 1, 9, 11, 5, 1, 5),
            /* DS:39e0 + race * 16: strength's least and most by gender,
             * exceptional strength's most by gender, then the other five
             * scores' least and most (4def:164d, 4def:278c). */
            [COK_DATA_SCORE_LIMITS] = TABLE(0x39e0, 1, 0, 7, 16, 0, 16),
            /* DS:3a50 + race * 15 (4def:0ae0). */
            [COK_DATA_RACE_CLASSES] = TABLE(0x3a50, 1, 0, 7, 15, 0, 15),
            /* DS:3ab9 + race * 32 + class * 4 (4def:1488). */
            [COK_DATA_AGE_DICE] = TABLE(0x3ab9, 4, 0, 7, 32, 0, 8),
            /* DS:3b99 + race * 10 + 2 * k (4def:164d). */
            [COK_DATA_AGES] = TABLE(0x3b99, 2, 0, 7, 10, 0, 5),
            /* DS:3bdf + class * 6 + score (4def:164d, 4def:278c). */
            [COK_DATA_CLASS_MINIMUMS] = TABLE(0x3bdf, 1, 0, 17, 6, 0, 6),
            /* DS:3c45 + class (4def:1222). */
            [COK_DATA_CLASS_ALIGNMENTS] = LIST(0x3c45, 1, 0, 17),
            /* DS:3c53 + constitution, 3-25 (4def:257c). */
            [COK_DATA_CONSTITUTION] = LIST(0x3c53, 1, 3, 23),
            /* DS:3c65 + 99 * row + 4 * level, rows 0-9 (6346:371e's) and
             * levels 2-12, after each row's spells a day (4def:4d9e). */
            [COK_DATA_EXPERIENCE] = TABLE(0x3c65, 4, 0, 10, 99, 2, 11),
            /* DS:3c8e + 99 * table + 5 * level + spell level, levels 2-12
             * (66c2:000f). */
            [COK_DATA_SPELLS_A_DAY] = TABLE3(0x3c8e, 1, 0, 9, 99, 2, 11, 5, 1, 5),
            /* DS:4056 + 60 * class + 5 * level + type, levels 1-12
             * (66c2:08a6). */
            [COK_DATA_SAVING_THROWS] = TABLE3(0x4056, 1, 0, 8, 60, 1, 12, 5, 0, 5),
            /* DS:4208 + 40 * list + 10 * level + k: 1 the cleric's, 2 the
             * White and 3 the Red mages' (66c2:000f, 5b04:0cb8, 4def:20e3,
             * the scrolls' 58e7). */
            [COK_DATA_SPELL_LISTS] = TABLE3(0x4208, 1, 1, 3, 40, 1, 4, 10, 1, 10),
            /* DS:0396 + 5 * way + try, ways 1-6 (3afb:0743, 3afb:095a): the port
             * holds DS:0396-03ba, the bytes before way 1's and the next
             * table's first, which a sixth try reads. */
            [COK_DATA_WAYS] = TABLE_IN(0x0396, 1, 1, 6, 5, 1, 5, 0x0396, 37),
            /* DS:03ba, 03ca, 03da, 03de, 03e6, 03ee: placement (3cb2:17f7). */
            [COK_DATA_MOVES] = TABLE(0x03ba, 1, 0, 4, 4, 0, 4),
            [COK_DATA_QUADRANTS] = TABLE(0x03ca, 1, 0, 4, 4, 0, 4),
            [COK_DATA_QUADRANT_DIRECTIONS] = LIST(0x03da, 1, 0, 4),
            [COK_DATA_START_COLUMNS] = TABLE(0x03de, 1, 0, 2, 4, 0, 4),
            [COK_DATA_START_ROWS] = TABLE(0x03e6, 1, 0, 2, 4, 0, 4),
            [COK_DATA_FORMATIONS] = TABLE3(0x03ee, 1, 0, 5, 12, 0, 6, 2, 0, 2),
            /* DS:042a, 042e: a dungeon's furniture (3cb2:0ca2). */
            [COK_DATA_BESIDE_X] = LIST(0x042a, 1, 0, 4),
            [COK_DATA_BESIDE_Y] = LIST(0x042e, 1, 0, 4),
            /* DS:0432 (3cb2:0fc8). */
            [COK_DATA_TERRAIN_FLAGS] = LIST(0x0432, 1, 0, 16),
            /* DS:0444 + 13 * kind + level, kinds 1-12 (432f:12b7): the port
             * holds DS:0451-050f, past the last kind's level 12 the camp
             * menu's strings. */
            [COK_DATA_TURNING] = TABLE_IN(0x0444, 1, 1, 12, 13, 0, 13, 0x0451, 191),
            /* DS:0d94 + 8 * part + side (5b04:18e7): the port holds the
             * bytes to DS:0eab, which a side byte past 7 reads. */
            [COK_DATA_CONE_SIDES] = TABLE_IN(0x0d94, 1, 0, 4, 8, 0, 8, 0x0d94, 0x118),
            /* DS:0db4 (60f4:1440). */
            [COK_DATA_BATTLE_ONLY] = LIST(0x0db4, 1, 0, 16),
            /* DS:1dd4 + 3 * (y step + 1) + x step + 1 (a line's step). */
            [COK_DATA_LINE_DIRECTIONS] = TABLE(0x1dd4, 1, 0, 3, 3, 0, 3),
            /* DS:1e26, 1e36: an icon's recolouring (6d21:0408, 4b6d:08e0). */
            [COK_DATA_SAME_COLOURS] = LIST(0x1e26, 1, 0, 16),
            [COK_DATA_BLACK_COLOURS] = LIST(0x1e36, 1, 0, 16),
            /* DS:1ed6, 1edf, 8 for none: read as bytes of the data segment
             * where a facing past 8 reads on (the overland map's 4877, a
             * cone's 5b04:19e7, a cloud's 60f4). */
            [COK_DATA_STEP_X] = LIST(0x1ed6, 1, 0, 9),
            [COK_DATA_STEP_Y] = LIST(0x1edf, 1, 0, 9),
            /* DS:1ee4 + 4 * value: cost, eye and blocking heights, tile
             * (6beb). */
            [COK_DATA_TERRAIN] = TABLE(0x1ee4, 1, 0, 66, 4, 0, 4),
            /* DS:1fe4 + 8 * size + 2 * cell + axis (6beb:000f): sizes 1-4,
             * and the bytes the original reads for 0 and 5-7. */
            [COK_DATA_FOOTPRINTS] = TABLE3(0x1fe4, 1, 0, 8, 8, 0, 4, 2, 0, 2),
            /* DS:200c (6346:0cdb, 432f:2235). */
            [COK_DATA_HELPLESS] = LIST(0x200c, 1, 0, 4),
            /* DS:2091 + k, k 1-4, and the byte after them, which 60f4:0c95
             * reads for cell 4. */
            [COK_DATA_CLOUD_CELLS] = LIST(0x2091, 1, 1, 5),
            /* DS:0a9a (4def:1222). */
            [COK_DATA_ALIGNMENT_CODES] = LIST(0x0a9a, 1, 0, 6),
            /* DS:0b6e, 0b76, 0b7e (4def:4b3a, the knights' orders). */
            [COK_DATA_LEVEL_DICE] = LIST(0x0b6e, 1, 0, 8),
            [COK_DATA_DIE_SIDES] = LIST(0x0b76, 1, 0, 8),
            [COK_DATA_KNIGHT_SCORES] = TABLE(0x0b7e, 1, 0, 2, 6, 0, 6),
            /* DS:0b88 + 2 * t (58e7:1039). */
            [COK_DATA_TREASURE_TEMPLATES] = LIST(0x0b88, 2, 0, 57),
            /* DS:01c6 (340d:0323, 0423). */
            [COK_DATA_DISEASES] = LIST(0x01c6, 1, 0, 6),
        },
};

/* The spells (magic.c, cast.c). */

/* Their names by id from 1, DS:2077 + 41 * id, string[40] each (2fd3,
 * 4888, 546c, 5b04 read them). */
static const char *const champions_spell_names[0x6b] = {
    "Bless",
    "Curse",
    "Cure Light Wounds",
    "Cause Light Wounds",
    "Detect Magic",
    "Protection From Evil",
    "Protection from Good",
    "Resist Cold",
    "Burning Hands",
    "Charm Person",
    "Detect Magic",
    "Enlarge",
    "Reduce",
    "Friends",
    "Magic Missile",
    "Protection From Evil",
    "Protection From Good",
    "Read Magic",
    "Shield",
    "Shocking Grasp",
    "Sleep",
    "Find Traps",
    "Hold Person",
    "Resist Fire",
    "Silence, 15' Radius",
    "Slow Poison",
    "Snake Charm",
    "Spiritual Hammer",
    "Detect Invisibility",
    "Invisibility",
    "Knock",
    "Mirror Image",
    "Ray of Enfeeblement",
    "Stinking Cloud",
    "Strength",
    "Animate Dead",
    "Cure Blindness",
    "Cause Blindness",
    "Cure Disease",
    "Cause Disease",
    "Dispel Magic",
    "Prayer",
    "Remove Curse",
    "Bestow Curse",
    "Blink",
    "Dispel Magic",
    "Fireball",
    "Haste",
    "Hold Person",
    "Invisibility, 10' Radius",
    "Lightning Bolt",
    "Protection From Evil, 10' Radius",
    "Protection From Good, 10' Radius",
    "Protection From Normal Missiles",
    "Slow",
    "Restoration",
    "",
    "Cure Serious Wounds",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "Cause Serious Wounds",
    "Neutralize Poison",
    "Poison",
    "Protection Evil, 10' Radius",
    "Sticks to Snakes",
    "Cure Critical Wounds",
    "Cause Critical Wounds",
    "Dispel Evil",
    "Flame Strike",
    "Raise Dead",
    "Slay Living",
    "Detect Magic",
    "Entangle",
    "Faerie Fire",
    "Invisibility to Animals",
    "Charm Monsters",
    "Confusion",
    "Dimension Door",
    "Fear",
    "Fire Shield",
    "Fumble",
    "Ice Storm",
    "Minor Globe Of Invulnerability",
    "Remove Curse",
    "Animate Dead",
    "Cloud Kill",
    "Cone of Cold",
    "Feeblemind",
    "Hold Monsters",
    "",
    "",
    "",
    "",
    "",
    "Bestow Curse",
    "Protection From Evil 10' Radius",
    "Silence 15' radius",
    "Detect Magic",
    "Remove Curse",
    "Bless",
    "Charm Person",
    "Burning Hands",
};

/* The spell lists' headings by spell level (DS:0bfa, string[40] each). */
static const char *const champions_spell_levels[10] = {
    "Special", "1st Level", "2nd Level", "3rd Level", "4th Level",
    "5th Level", "6th Level", "7th Level", "8th Level", "9th Level",
};

/* The handlers by spell id, DS:6e3a + 4 * spell, as 5b04:58ed fills it:
 * 5b04's routines. */
static const cok_rules_cast champions_cast[0x6c] = {
    [0x01] = {0x1efb, COK_CAST_BLESS},
    [0x02] = {0x1f2c, COK_CAST_CURSE},
    [0x03] = {0x1f57, COK_CAST_CURE_LIGHT},
    [0x04] = {0x1fc2, COK_CAST_CAUSE_LIGHT},
    [0x05] = {0x2004, COK_CAST_AFFECTED},
    [0x06] = {0x203e, COK_CAST_PROTECTED},
    [0x07] = {0x203e, COK_CAST_PROTECTED},
    [0x08] = {0x207d, COK_CAST_COLD_RESISTANT},
    [0x09] = {0x20ab, COK_CAST_BURNING_HANDS},
    [0x0a] = {0x20f8, COK_CAST_CHARM_PERSON},
    [0x0b] = {0x2004, COK_CAST_AFFECTED},
    [0x0c] = {0x21d7, COK_CAST_ENLARGE},
    [0x0d] = {0x230c, COK_CAST_REDUCE},
    [0x0e] = {0x23ac, COK_CAST_FRIENDS},
    [0x0f] = {0x23f3, COK_CAST_MAGIC_MISSILE},
    [0x10] = {0x203e, COK_CAST_PROTECTED},
    [0x11] = {0x203e, COK_CAST_PROTECTED},
    [0x12] = {0x2004, COK_CAST_AFFECTED},
    [0x13] = {0x2456, COK_CAST_SHIELDED},
    [0x14] = {0x2484, COK_CAST_SHOCKING_GRASP},
    [0x15] = {0x24d8, COK_CAST_SLEEP},
    [0x16] = {0x2004, COK_CAST_AFFECTED},
    [0x17] = {0x2616, COK_CAST_HOLD},
    [0x18] = {0x2679, COK_CAST_FIRE_RESISTANT},
    [0x19] = {0x26b2, COK_CAST_SILENCE},
    [0x1a] = {0x26eb, COK_CAST_SLOW_POISON},
    [0x1b] = {0x279a, COK_CAST_SNAKE_CHARM},
    [0x1c] = {0x2877, COK_CAST_HAMMER},
    [0x1d] = {0x2004, COK_CAST_AFFECTED},
    [0x1e] = {0x28c7, COK_CAST_INVISIBLE},
    [0x1f] = {0x2900, COK_CAST_KNOCK},
    [0x20] = {0x293b, COK_CAST_MIRROR_IMAGE},
    [0x21] = {0x2999, COK_CAST_WEAKENED},
    [0x22] = {0x29de, COK_CAST_STINKING_CLOUD},
    [0x23] = {0x2e4e, COK_CAST_STRENGTH},
    [0x24] = {0x3000, COK_CAST_NOTHING},
    [0x25] = {0x300d, COK_CAST_CURE_BLINDNESS},
    [0x26] = {0x3053, COK_CAST_BLINDED},
    [0x27] = {0x3107, COK_CAST_CURE_DISEASE},
    [0x28] = {0x3124, COK_CAST_DISEASED},
    [0x29] = {0x320f, COK_CAST_DISPEL_MAGIC},
    [0x2a] = {0x358a, COK_CAST_PRAYER},
    [0x2b] = {0x35f5, COK_CAST_REMOVE_CURSE},
    [0x2c] = {0x3705, COK_CAST_BESTOW_CURSE},
    [0x2d] = {0x373e, COK_CAST_BLINK},
    [0x2e] = {0x320f, COK_CAST_DISPEL_MAGIC},
    [0x2f] = {0x376c, COK_CAST_FIREBALL},
    [0x30] = {0x3991, COK_CAST_HASTE},
    [0x31] = {0x2616, COK_CAST_HOLD},
    [0x32] = {0x28c7, COK_CAST_INVISIBLE},
    [0x33] = {0x3d04, COK_CAST_LIGHTNING_BOLT},
    [0x34] = {0x203e, COK_CAST_PROTECTED},
    [0x35] = {0x203e, COK_CAST_PROTECTED},
    [0x36] = {0x203e, COK_CAST_PROTECTED},
    [0x37] = {0x3d5e, COK_CAST_SLOW},
    [0x38] = {0x3d8c, COK_CAST_NOTHING},
    [0x39] = {0x3d9b, COK_CAST_SPEEDY},
    [0x3a] = {0x3ddc, COK_CAST_CURE_SERIOUS},
    [0x3b] = {0x3e53, COK_CAST_GIANT_STRENGTH},
    [0x3c] = {0x3ed8, COK_CAST_ITEM_BOLT},
    [0x3d] = {0x3f2c, COK_CAST_PARALYZED},
    [0x3e] = {0x3f63, COK_CAST_POTION},
    [0x3f] = {0x3fbd, COK_CAST_INVISIBLE},
    [0x40] = {0x376c, COK_CAST_FIREBALL},
    [0x41] = {0x3feb, COK_CAST_ITEM_DAMAGE},
    [0x42] = {0x4026, COK_CAST_CAUSE_SERIOUS},
    [0x43] = {0x4098, COK_CAST_NEUTRALIZE_POISON},
    [0x44] = {0x418e, COK_CAST_POISON},
    [0x45] = {0x203e, COK_CAST_PROTECTED},
    [0x46] = {0x4214, COK_CAST_STICKS_TO_SNAKES},
    [0x47] = {0x42a7, COK_CAST_NOTHING},
    [0x48] = {0x42b1, COK_CAST_NOTHING},
    [0x49] = {0x42b6, COK_CAST_NOTHING},
    [0x4a] = {0x42bb, COK_CAST_NOTHING},
    [0x4b] = {0x42c0, COK_CAST_NOTHING},
    [0x4c] = {0x42ca, COK_CAST_NOTHING},
    [0x4d] = {0x2004, COK_CAST_AFFECTED},
    [0x4e] = {0x42d4, COK_CAST_NOTHING},
    [0x4f] = {0x42de, COK_CAST_NOTHING},
    [0x50] = {0x42e3, COK_CAST_NOTHING},
    [0x51] = {0x42f3, COK_CAST_CHARM_MONSTERS},
    [0x52] = {0x4382, COK_CAST_CONFUSION},
    [0x53] = {0x4446, COK_CAST_DIMENSION_DOOR},
    [0x54] = {0x44ed, COK_CAST_FEAR},
    [0x55] = {0x464d, COK_CAST_FIRE_SHIELD},
    [0x56] = {0x4801, COK_CAST_FUMBLE},
    [0x57] = {0x4914, COK_CAST_ICE_STORM},
    [0x58] = {0x4957, COK_CAST_PROTECTED},
    [0x59] = {0x35f5, COK_CAST_REMOVE_CURSE},
    [0x5a] = {0x3000, COK_CAST_NOTHING},
    [0x5b] = {0x4984, COK_CAST_NOTHING},
    [0x5c] = {0x498e, COK_CAST_NOTHING},
    [0x5d] = {0x4998, COK_CAST_NOTHING},
    [0x5e] = {0x2616, COK_CAST_HOLD},
    [0x5f] = {0x49a3, COK_CAST_EMPTY_TEXT},
    [0x60] = {0x49d1, COK_CAST_EMPTY_TEXT},
    [0x61] = {0x49ff, COK_CAST_EMPTY_TEXT},
    [0x62] = {0x4a2c, COK_CAST_NOTHING},
    [0x63] = {0x4a40, COK_CAST_POTION},
    [0x64] = {0x3705, COK_CAST_BESTOW_CURSE},
    [0x65] = {0x203e, COK_CAST_PROTECTED},
    [0x66] = {0x26b2, COK_CAST_SILENCE},
    [0x67] = {0x2004, COK_CAST_AFFECTED},
    [0x68] = {0x35f5, COK_CAST_REMOVE_CURSE},
    [0x69] = {0x1efb, COK_CAST_BLESS},
    [0x6a] = {0x20f8, COK_CAST_CHARM_PERSON},
    [0x6b] = {0x20ab, COK_CAST_BURNING_HANDS},
};

/* The durations 5b04:0f78 does not take from the table: Slow Poison's
 * 0xec4 minutes, Cause Disease's 1d6 * 10, the item powers 0x39 and
 * 0x3d's 5d4, 0x3b's 1d4 * 10 + 40, 0x3f's by combat, Neutralize
 * Poison's 0x5a0. */
static const cok_rules_duration champions_durations[] = {
    {0x1a, COK_DURATION_FIXED, 0xec4}, {0x28, COK_DURATION_D6_TENS, 0},
    {0x39, COK_DURATION_5D4, 0},       {0x3d, COK_DURATION_5D4, 0},
    {0x3b, COK_DURATION_D4_TENS_40, 0}, {0x3f, COK_DURATION_INVISIBLE, 0},
    {0x43, COK_DURATION_FIXED, 0x5a0},
};

/* The spell ids the code names (spells.h). */
static const uint8_t champions_spell_ids[COK_SPELL_NAMES] = {
    [COK_SPELL_BLESS] = 0x01,
    [COK_SPELL_CURE_LIGHT_WOUNDS] = 0x03,
    [COK_SPELL_MAGIC_MISSILE] = 0x0f,
    [COK_SPELL_HOLD_PERSON] = 0x17,
    [COK_SPELL_KNOCK] = 0x1f,
    [COK_SPELL_ANIMATE_DEAD] = 0x24,
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

/* The spell effects (effect.c). */

/* The handlers by effect id, DS:6b94 + 4 * id, as 5b04:58ed (4, 6, 0x4e,
 * 0x53-0x55, 0x5a, 0x68: 5b04's) and 3f44:38ea (the others, 3f44's)
 * fill it; none for 0. */
static const cok_rules_effect champions_effect_handlers[0x79] = {
    [0x01] = {0x0124, COK_FX_BLESS},
    [0x02] = {0x0134, COK_FX_CURSE},
    [0x03] = {0x016a, COK_FX_SNAKES},
    [0x04] = {0x4ab8, COK_FX_BREATH},
    [0x05] = {0x3881, COK_FX_NOTHING},
    [0x06] = {0x546e, COK_FX_BREATH},
    [0x07] = {0x0208, COK_FX_KENDER},
    [0x08] = {0x0344, COK_FX_PROTECTION_EVIL},
    [0x09] = {0x0379, COK_FX_PROTECTION_GOOD},
    [0x0a] = {0x03ae, COK_FX_RESIST_COLD},
    [0x0b] = {0x03cd, COK_FX_CHARM},
    [0x0c] = {0x04b1, COK_FX_NOTHING},
    [0x0d] = {0x04c2, COK_FX_NEW_FORM},
    [0x0e] = {0x05bc, COK_FX_NOTHING},
    [0x0f] = {0x05c3, COK_FX_TICKING},
    [0x10] = {0x0625, COK_FX_NOTHING},
    [0x11] = {0x062c, COK_FX_SHIELD},
    [0x12] = {0x065d, COK_FX_RESIST_CHARM_30},
    [0x13] = {0x3881, COK_FX_NOTHING},
    [0x14] = {0x0681, COK_FX_RESIST_FIRE},
    [0x15] = {0x06b2, COK_FX_SILENCED},
    [0x16] = {0x0725, COK_FX_POISONED},
    [0x17] = {0x07b5, COK_FX_HAMMER},
    [0x18] = {0x3881, COK_FX_NOTHING},
    [0x19] = {0x09b3, COK_FX_INVISIBLE},
    [0x1a] = {0x09e8, COK_FX_TARGET_BONUS},
    [0x1b] = {0x00f7, COK_FX_HELD},
    [0x1c] = {0x0a2f, COK_FX_IMAGES},
    [0x1d] = {0x0ab8, COK_FX_QUARTER_LESS},
    [0x1e] = {0x0ae0, COK_FX_COUGHING},
    [0x1f] = {0x00f7, COK_FX_HELD},
    [0x20] = {0x0bc9, COK_FX_NEW_FORM},
    [0x21] = {0x0cf0, COK_FX_BLIND},
    [0x22] = {0x0d19, COK_FX_DISEASE},
    [0x23] = {0x0d7c, COK_FX_CONFUSED},
    [0x24] = {0x0f3f, COK_FX_MINUS_FOUR},
    [0x25] = {0x0f50, COK_FX_BLINK},
    [0x26] = {0x04b1, COK_FX_NOTHING},
    [0x27] = {0x0f78, COK_FX_HASTE},
    [0x28] = {0x0ff7, COK_FX_CLOUD},
    [0x29] = {0x1412, COK_FX_AVOID},
    [0x2a] = {0x144c, COK_FX_SLOW},
    [0x2b] = {0x1469, COK_FX_DRAIN},
    [0x2c] = {0x1504, COK_FX_TICKING},
    [0x2d] = {0x0344, COK_FX_PROTECTION_EVIL},
    [0x2e] = {0x0379, COK_FX_PROTECTION_GOOD},
    [0x2f] = {0x15a3, COK_FX_AGAINST_FLAG4},
    [0x30] = {0x15cd, COK_FX_IMMOLATE},
    [0x31] = {0x16ef, COK_FX_PRAYER},
    [0x32] = {0x173a, COK_FX_COLD_RESISTANT},
    [0x33] = {0x00f7, COK_FX_HELD},
    [0x34] = {0x00f7, COK_FX_HELD},
    [0x35] = {0x00f7, COK_FX_HELD},
    [0x36] = {0x176b, COK_FX_FIRE_RESISTANT},
    [0x37] = {0x179c, COK_FX_NOTHING},
    [0x38] = {0x17a3, COK_FX_ADD_INVISIBLE_1},
    [0x39] = {0x17c6, COK_FX_PLUS_TWO},
    [0x3a] = {0x17ea, COK_FX_DEATH_THROES},
    [0x3b] = {0x1891, COK_FX_IMMUNE},
    [0x3c] = {0x18b9, COK_FX_BURST},
    [0x3d] = {0x1a72, COK_FX_FIRE_RESISTANCE},
    [0x3e] = {0x1acd, COK_FX_REGENERATE},
    [0x3f] = {0x1b18, COK_FX_MINOR_GLOBE},
    [0x40] = {0x1c21, COK_FX_POISON},
    [0x41] = {0x1c34, COK_FX_POISON},
    [0x42] = {0x1c54, COK_FX_ZAP},
    [0x43] = {0x1dc5, COK_FX_LOSE_WEAPON},
    [0x44] = {0x1f97, COK_FX_EXPLODES},
    [0x45] = {0x200b, COK_FX_PARALYSIS},
    [0x46] = {0x2037, COK_FX_CLOUD},
    [0x47] = {0x23a4, COK_FX_REFLECT},
    [0x48] = {0x24f4, COK_FX_TERRIBLE},
    [0x49] = {0x2665, COK_FX_UNAFFECTED},
    [0x4a] = {0x26a5, COK_FX_DISPELLING},
    [0x4b] = {0x26bf, COK_FX_GIANT_SLAYER},
    [0x4c] = {0x272a, COK_FX_GATE},
    [0x4d] = {0x29d5, COK_FX_BERSERK},
    [0x4e] = {0x4dc2, COK_FX_BREATH},
    [0x4f] = {0x2b5f, COK_FX_STENCH, .text = "is affected"}, /* CS:2b53 */
    [0x50] = {0x2cad, COK_FX_CLOUD},
    [0x51] = {0x3009, COK_FX_PARALYSIS},
    [0x52] = {0x303c, COK_FX_FEAR},
    [0x53] = {0x546e, COK_FX_BREATH},
    [0x54] = {0x4eea, COK_FX_BREATH},
    [0x55] = {0x50cf, COK_FX_BREATH},
    [0x56] = {0x31bc, COK_FX_POISON},
    [0x57] = {0x31cf, COK_FX_POISON},
    [0x58] = {0x31e2, COK_FX_PARALYSIS},
    [0x59] = {0x320f, COK_FX_DISPLACEMENT},
    [0x5a] = {0x5227, COK_FX_BREATH},
    [0x5b] = {0x3258, COK_FX_NOTHING},
    [0x5c] = {0x3881, COK_FX_NOTHING},
    [0x5d] = {0x325f, COK_FX_HALF_BLUNT},
    [0x5e] = {0x32a8, COK_FX_CONSTITUTION_SAVE},
    [0x5f] = {0x3328, COK_FX_RESIST_CHARM_90},
    [0x60] = {0x334c, COK_FX_RESIST_CHARM},
    [0x61] = {0x3361, COK_FX_FREE_ACTION},
    [0x62] = {0x336f, COK_FX_COLD_IMMUNE},
    [0x63] = {0x3386, COK_FX_POISON_IMMUNE},
    [0x64] = {0x33a7, COK_FX_HALF_EDGED},
    [0x65] = {0x3406, COK_FX_HOLY_WATER},
    [0x66] = {0x3449, COK_FX_NOTHING},
    [0x67] = {0x3450, COK_FX_MAGIC_WEAPONS},
    [0x68] = {0x546e, COK_FX_BREATH},
    [0x69] = {0x349c, COK_FX_SLAYER},
    [0x6a] = {0x34db, COK_FX_STUNNED},
    [0x6b] = {0x34f9, COK_FX_TURN},
    [0x6c] = {0x3619, COK_FX_ADD_INVISIBLE_255},
    [0x6d] = {0x363c, COK_FX_NOTHING},
    [0x6e] = {0x3643, COK_FX_KNIGHT},
    [0x6f] = {0x3692, COK_FX_TERROR},
    [0x70] = {0x36e1, COK_FX_FIRE_SHIELD},
    [0x71] = {0x3768, COK_FX_NOTHING},
    [0x72] = {0x376f, COK_FX_SLAYER},
    [0x73] = {0x37b0, COK_FX_SLAYER},
    [0x74] = {0x37e8, COK_FX_NOTHING},
    [0x75] = {0x37fc, COK_FX_DISRUPT},
    [0x76] = {0x386a, COK_FX_MINUS_TWO},
    [0x77] = {0x3876, COK_FX_MINUS_ONE},
    [0x78] = {0x3888, COK_FX_ITEM},
};

/* The effect ids each event runs, in order (60f4:057c). */
static const uint8_t *const champions_events[0x19] = {
    [0x01] = (const uint8_t[]){0x25, 0x19, 0},
    [0x02] = (const uint8_t[]){0x58, 0x57, 0x56, 0x51, 0x45, 0x40, 0x41, 0},
    [0x03] = (const uint8_t[]){0x45, 0x40, 0x41, 0x51, 0x56, 0x57, 0},
    [0x04] = (const uint8_t[]){0x07, 0x1d, 0x4b, 0x69, 0x72, 0x73, 0x75, 0},
    [0x05] = (const uint8_t[]){0x3b, 0x1c, 0x29, 0x64, 0x67, 0x5d, 0x65, 0x70, 0},
    [0x06] = (const uint8_t[]){0x47, 0x3b, 0x3d, 0x0a, 0x14, 0x66, 0x11, 0x1c, 0x62, 0x49, 0x3f, 0},
    [0x07] = (const uint8_t[]){0x33, 0x34, 0x35, 0x1f, 0x03, 0x1b, 0},
    [0x08] = (const uint8_t[]){0x52, 0x59, 0x38, 0},
    [0x09] = (const uint8_t[]){0x47, 0x74, 0x5c, 0x3b, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x12, 0x3f, 0},
    [0x0a] = (const uint8_t[]){0x07, 0x76, 0x77, 0x39, 0x01, 0x02, 0x21, 0x24, 0x31, 0x1a, 0x4b, 0},
    [0x0b] = (const uint8_t[]){0x21, 0x11, 0x08, 0x09, 0x2d, 0x2e, 0x1e, 0},
    [0x0c] = (const uint8_t[]){0x5e, 0x08, 0x09, 0x0a, 0x11, 0x14, 0x21, 0x24, 0x2d, 0x2e, 0x31,
                               0x3d, 0x63, 0x32, 0x36, 0},
    [0x0d] = (const uint8_t[]){0x44, 0x43, 0x0d, 0x20, 0x46, 0},
    [0x0e] = (const uint8_t[]){0x06, 0x55, 0x54, 0x53, 0x4e, 0x4a, 0x04, 0x5a, 0x68, 0},
    [0x0f] = (const uint8_t[]){0x4f, 0x4c, 0x48, 0x42, 0x30, 0x15, 0x1e, 0x0b, 0x4d, 0},
    [0x10] = (const uint8_t[]){0x19, 0x25, 0x2f, 0x59, 0},
    [0x11] = (const uint8_t[]){0x01, 0x02, 0x0b, 0},
    [0x12] = (const uint8_t[]){0x27, 0x2a, 0},
    [0x13] = (const uint8_t[]){0x17, 0x38, 0x0b, 0},
    [0x14] = (const uint8_t[]){0x32, 0x36, 0},
    [0x15] = (const uint8_t[]){0x23, 0},
    [0x18] = (const uint8_t[]){0x52, 0},
};

/* The ids the party shares (60f4:0332), Prayer's from 6 squares. */
static const uint8_t champions_shared[] = {0x15, 0x2d, 0x2e, 0x31, 0};

/* Magic resistance leaves 0x5b and 0x52 pending (60f4:04f3). */
static const uint8_t champions_resisted_pending[] = {0x5b, 0x52, 0};

/* The effects a party record loses after combat (351b:0574, DS:0390). */
/* The item types the missile's flight draws as arrows (432f:2beb): darts,
 * javelins, quarrels and arrows. */
static const uint8_t champions_arrows[] = {5, 7, 0x0c, 0x1e, 0};

static const uint8_t champions_combat_effects[] = {0x03, 0x0b, 0x15, 0x17, 0x1b, 0x23,
                                                   0x28, 0x33, 0x34, 0x35, 0x1f, 0};

/* The effect ids the code names (spells.h). */
static const uint8_t champions_effect_ids[COK_EFFECT_NAMES] = {
    [COK_EFFECT_SNAKES] = 0x03,
    [COK_EFFECT_BOLTS] = 0x04,
    [COK_EFFECT_DETECT_MAGIC] = 0x05,
    [COK_EFFECT_CHARMED] = 0x0b,
    [COK_EFFECT_ENLARGED] = 0x0c,
    [COK_EFFECT_NEW_FORM] = 0x0d,
    [COK_EFFECT_FRIENDLY] = 0x0e,
    [COK_EFFECT_POISON] = 0x0f,
    [COK_EFFECT_READ_MAGIC] = 0x10,
    [COK_EFFECT_RESIST_CHARM_30] = 0x12,
    [COK_EFFECT_SLOW_POISON] = 0x16,
    [COK_EFFECT_HAMMER] = 0x17,
    [COK_EFFECT_SEES_INVISIBLE] = 0x18,
    [COK_EFFECT_INVISIBLE] = 0x19,
    [COK_EFFECT_TARGET_BONUS] = 0x1a,
    [COK_EFFECT_MIRROR_IMAGES] = 0x1c,
    [COK_EFFECT_NAUSEA] = 0x1e,
    [COK_EFFECT_HELPLESS] = 0x1f,
    [COK_EFFECT_MAD] = 0x20,
    [COK_EFFECT_BLIND] = 0x21,
    [COK_EFFECT_DISEASE] = 0x22,
    [COK_EFFECT_CONFUSED] = 0x23,
    [COK_EFFECT_CURSED] = 0x24,
    [COK_EFFECT_STRENGTH] = 0x26,
    [COK_EFFECT_HASTED] = 0x27,
    [COK_EFFECT_CLOUD] = 0x28,
    [COK_EFFECT_SLOWED] = 0x2a,
    [COK_EFFECT_STRENGTH_DRAIN] = 0x2b,
    [COK_EFFECT_WASTING] = 0x2c,
    [COK_EFFECT_HARD_TO_HIT] = 0x2f,
    [COK_EFFECT_IMMOLATE] = 0x30,
    [COK_EFFECT_HOT_FIRE_SHIELD] = 0x32,
    [COK_EFFECT_HELD] = 0x33,
    [COK_EFFECT_PARALYZED] = 0x34,
    [COK_EFFECT_ASLEEP] = 0x35,
    [COK_EFFECT_COLD_FIRE_SHIELD] = 0x36,
    [COK_EFFECT_POISONED] = 0x37,
    [COK_EFFECT_PLUS_TWO] = 0x39,
    [COK_EFFECT_DEATH_THROES] = 0x3a,
    [COK_EFFECT_IMMUNE] = 0x3b,
    [COK_EFFECT_BURST] = 0x3c,
    [COK_EFFECT_REGENERATING] = 0x3e,
    [COK_EFFECT_MINOR_GLOBE] = 0x3f,
    [COK_EFFECT_POISON_TOUCH] = 0x40,
    [COK_EFFECT_POISON_TOUCH_4] = 0x41,
    [COK_EFFECT_ZAP] = 0x42,
    [COK_EFFECT_EXPLODES] = 0x44,
    [COK_EFFECT_PARALYZING] = 0x45,
    [COK_EFFECT_DISPELLING] = 0x4a,
    [COK_EFFECT_GIANT_SLAYER] = 0x4b,
    [COK_EFFECT_GATE] = 0x4c,
    [COK_EFFECT_BERSERK] = 0x4d,
    [COK_EFFECT_FIREBALLS] = 0x4e,
    [COK_EFFECT_ACID_DEATH] = 0x46,
    [COK_EFFECT_ACID_POOL] = 0x50,
    [COK_EFFECT_PARALYZING_HARMLESS] = 0x51,
    [COK_EFFECT_COLD_BREATH] = 0x53,
    [COK_EFFECT_POISON_BREATH] = 0x54,
    [COK_EFFECT_LIGHTNING_BREATH] = 0x55,
    [COK_EFFECT_POISON_TOUCH_MINUS_2] = 0x56,
    [COK_EFFECT_POISON_TOUCH_1] = 0x57,
    [COK_EFFECT_PARALYZING_HUMANS] = 0x58,
    [COK_EFFECT_ACID_BREATH] = 0x5a,
    [COK_EFFECT_YELLED_AT] = 0x5b,
    [COK_EFFECT_FEARLESS] = 0x5c,
    [COK_EFFECT_CONSTITUTION_SAVE] = 0x5e,
    [COK_EFFECT_RESIST_CHARM_90] = 0x5f,
    [COK_EFFECT_POISON_IMMUNE] = 0x63,
    [COK_EFFECT_FROST_BREATH] = 0x06,
    [COK_EFFECT_FIRE_BREATH] = 0x68,
    [COK_EFFECT_RANGER] = 0x69,
    [COK_EFFECT_BANE] = 0x73,
    [COK_EFFECT_STUNNED] = 0x6a,
    [COK_EFFECT_TERROR] = 0x6f,
    [COK_EFFECT_FIRE_SHIELD] = 0x70,
    [COK_EFFECT_GIANT_STRENGTH] = 0x71,
    [COK_EFFECT_DEADLY] = 0x72,
    [COK_EFFECT_POISON_RESISTANCE] = 0x74,
    [COK_EFFECT_SICKENED] = 0x76,
    [COK_EFFECT_AFRAID] = 0x77,
};

/* Display (4888:17ae): the effects named by the first spell 1-0x38 that
 * adds them, and the texts of others. */
static const uint8_t champions_display_spells[] = {
    0x01, 0x02, 0x05, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0e, 0x10, 0x11, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1c, 0x1d, 0x20, 0x21, 0x22, 0x24, 0x25, 0x26,
    0x27, 0x29, 0x2a, 0x2d, 0x2e, 0x31, 0x33, 0x34, 0x35, 0,
};
static const cok_rules_effect_text champions_display_texts[] = {
    {0x04, "Dispel Evil"}, {0x1b, "Fumbling"}, {0x1f, "Helpless"}, {0x23, "Confused"},
    {0x2c, "Cause Disease"}, {0x32, "Hot Fire Shield"}, {0x36, "Cold Fire Shield"},
    {0x37, "Poisoned"}, {0x3b, "Regenerating"}, {0x3d, "Fire Resistance"},
    {0x3f, "Minor Globe of Invulnerability"}, {0x44, "enfeebled"},
    {0x45, "invisible to animals"}, {0x47, "Invisible"}, {0x48, "Camouflaged"},
    {0x49, "protected from dragon breath"}, {0x4d, "berserk"}, {0x59, "Displaced"},
};

/* The screens' frames (1128:0000-07e6): tile values added to 0x114, from
 * the data segment and the routines' code. */
static const uint8_t champions_top_row[40] = { /* DS:0e3a */
    0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const uint8_t champions_side[24] = { /* DS:0e62, and DS:0e8b the same */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0, 3, 4, 4, 4, 4, 5, 0,
};
/* 1128:0000 with 1: DS:0e62 but tile 4 on rows 15-17. */
static const uint8_t champions_side_open[24] = {
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0,
};
static const uint8_t champions_divider[17] = { /* DS:0e7a, rows 0-16 */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0,
};
static const uint8_t champions_box_side[13] = { /* DS:0ea3 and DS:0eb0, rows 2-14 */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0,
};
static const uint8_t champions_combat_side[23] = { /* DS:0ebd, 0ed4 and 0eeb, which hold the same */
    0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0,
};
static const uint8_t champions_sheet_side[23] = { /* DS:0f02 */
    0, 3, 4, 4, 4, 4, 4, 5, 0, 3, 4, 4, 4, 4, 4, 5, 0, 3, 4, 5, 0, 3, 5,
};
static const uint8_t champions_sheet_middle[11] = { /* DS:0f19-0f23, rows 9-19 */
    3, 4, 4, 4, 4, 4, 5, 0, 3, 4, 5,
};
/* 1128:0130's sides, rows 1-22: 3 on rows 1, 5 and 21, 5 on 3, 19 and 22,
 * the row across on 4 and 20 (drawn over), 4 on the others. */
static const uint8_t champions_credits_side[22] = {
    3, 4, 5, 0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 0, 3, 5,
};

#define ROW(y, from, to, value) {false, y, from, to, value, NULL, false}
#define COLUMN(x, from, to, value) {true, x, from, to, value, NULL, false}
#define COLUMN_OF(x, from, to, table) {true, x, from, to, 0, table, false}
#define MOONS(table) {false, 0, 0, 39, 0, table, true}
#define LINES(lines) lines, sizeof lines / sizeof *lines

/* 1128:0000: row 0 with the moons, the sides, row 23. */
static const cok_rules_frame_line champions_frame[] = {
    MOONS(champions_top_row),
    COLUMN_OF(0, 0, 23, champions_side),
    COLUMN_OF(39, 0, 23, champions_side),
    ROW(23, 0, 39, 0),
};
static const cok_rules_frame_line champions_open[] = {
    MOONS(champions_top_row),
    COLUMN_OF(0, 0, 23, champions_side_open),
    COLUMN_OF(39, 0, 23, champions_side_open),
    ROW(23, 0, 39, 0),
};
/* 1128:0242: row 16, the column 16 divider above it, the 3D view's box. */
static const cok_rules_frame_line champions_adventure[] = {
    ROW(16, 0, 39, 0),
    COLUMN_OF(16, 0, 16, champions_divider),
    ROW(2, 2, 14, 0),
    ROW(14, 2, 14, 0),
    COLUMN_OF(2, 2, 14, champions_box_side),
    COLUMN_OF(14, 2, 14, champions_box_side),
};
static const cok_rules_frame_line champions_row_16[] = { /* 1128:0344 */
    ROW(16, 0, 39, 0),
};
/* 1128:0384: 1128:0000's lines, the right side from DS:0e8b, then row 16. */
static const cok_rules_frame_line champions_spells[] = {
    MOONS(champions_top_row),
    COLUMN_OF(0, 0, 23, champions_side),
    COLUMN_OF(39, 0, 23, champions_side),
    ROW(23, 0, 39, 0),
    ROW(16, 0, 39, 0),
};
/* 1128:077c: over the open frame, row 2 and tile 3 at each end of row 3. */
static const cok_rules_frame_line champions_list[] = {
    ROW(2, 0, 39, 0),
    COLUMN(0, 3, 3, 3),
    COLUMN(39, 3, 3, 3),
};
/* 1128:04c1: columns 0, 22 and 39 to row 22, then row 22 across. */
static const cok_rules_frame_line champions_combat[] = {
    MOONS(champions_top_row),
    COLUMN_OF(0, 0, 22, champions_combat_side),
    COLUMN_OF(22, 0, 22, champions_combat_side),
    COLUMN_OF(39, 0, 22, champions_combat_side),
    ROW(22, 0, 39, 0),
};
/* 1128:05fc: rows 8, 16, 20 and 23 across, the sides to row 22, column 19
 * from row 9 to 19. */
static const cok_rules_frame_line champions_sheet[] = {
    MOONS(champions_top_row),
    ROW(8, 0, 39, 0),
    ROW(16, 0, 39, 0),
    ROW(20, 0, 39, 0),
    ROW(23, 0, 39, 0),
    COLUMN_OF(0, 0, 22, champions_sheet_side),
    COLUMN_OF(39, 0, 22, champions_sheet_side),
    COLUMN_OF(19, 9, 19, champions_sheet_middle),
};
/* 1128:0130: no moons; the sides between rows 0, 4, 20 and 23 across. */
static const cok_rules_frame_line champions_credits[] = {
    COLUMN_OF(0, 1, 22, champions_credits_side),
    COLUMN_OF(39, 1, 22, champions_credits_side),
    ROW(0, 0, 39, 0),
    ROW(4, 0, 39, 0),
    ROW(20, 0, 39, 0),
    ROW(23, 0, 39, 0),
};

/* The credits' lines (2f55:0267): purple (5), yellow (14) and light red
 * (12). */
static const cok_rules_text_line champions_credit_lines[] = {
    {5, 1, 5, "based on an original story by:"}, {15, 2, 14, "Jim Ward"},
    {4, 3, 14, "Victor Penman,"}, {19, 3, 5, "and"}, {24, 3, 14, "Dave Shelley"},
    {2, 5, 5, "game created by:"}, {18, 5, 12, "SSI Special Projects"},
    {2, 7, 12, "Project Director:"}, {20, 7, 12, "Programming"},
    {2, 8, 14, "Victor Penman"}, {20, 8, 14, "Russ Brown"}, {20, 9, 14, "Scot Bayless"},
    {2, 10, 12, "Graphic Arts:"}, {2, 11, 14, "Tom Wahl"},
    {20, 11, 12, "Encounter Design"}, {2, 12, 14, "Fred Butts"},
    {20, 12, 14, "Dave Shelley"}, {2, 13, 14, "Mark Johnson"},
    {20, 13, 14, "Michael Mancuso"}, {2, 14, 14, "Cyrus Lum"},
    {20, 14, 14, "Ken Humphries"}, {2, 15, 14, "Susan Manley"}, {2, 17, 12, "playtest:"},
    {12, 17, 14, "Rick White,Don Mc Clure,"}, {2, 18, 14, "Mike Bench,Cliff Mann, Rick Wilson,"},
    {2, 19, 14, "Erik Flom,James Young,Graeme Bayless"}, {2, 21, 5, "Musical Score:"},
    {20, 21, 5, "Musical Driver:"}, {2, 22, 14, "John Halbleib"},
    {20, 22, 14, "Electronic Arts"},
};

const cok_rules cok_rules_champions = {
    .id = "champions",
    .assets = "COKAssets",
    .text =
        {
            .name = "Champions of Krynn",
            .version = "Champions of Krynn v1.2",       /* 1000:0134, at startup */
            .version_again = "Champions Of Krynn v1.2", /* 1000:0313's, after the demo */
            .camp = "The party makes camp...",
        },
    .archives =
        {
            .ecl = "ECL",
            .geo = "GEO",
            .walldef = "WALLDEF",
            .tiles = "8X8D",
            .font = {"8X8D", 1, 201}, /* 3e99:005b */
            .pic = "PIC",
            .sprite = "SPRIT",
            .big = "BIGPIC",
            .head = "HEAD",
            .body = "BODY",
            .icons = "CPIC",
            .missiles = "COMSPR",
            .icon_head = "CHEAD",
            .icon_body = "CBODY",
            .sky = "SKY",
            .ground = NULL,
            .backdrop = NULL,
            .cursor = {"CURSOR", 0, 1}, /* 3e99:01fd */
            .title = "TITLE",
            .treasure = "ITEM",
            .monster = "MON",
            .monster_record = "CHA",
            .monster_effects = "SPC",
            .monster_items = "ITM",
            .monster_spells = NULL,
            .dungeon = "DUNGCOM",
            .wilderness = "WILDCOM",
            .random = "RANDCOM",
            .music_drivers = "MIDI",
            .music_score = "SOUND",
            .file_digit = champions_file_digit,
            .pic_files = NULL,
            .sprite_files = NULL,
            .wall_files = NULL,
        },
    .files =
        {
            .config = "KRYNN.CFG",
            .item_types = "ITEMS",
            .install_dir = "C:\\COK\\",
            .save_dir = "SAVE",
        },
    .saves =
        {
            .game = "SAVGAM",
            .game_extension = "DAT",
            .character = "CHRDAT",
            .record = "SAV",
            .roster = "WHO",
            .items = "STF",
            .effects = "SFX",
            .spells = NULL, /* in the record */
            .debug = "DEBUG.TXT",
            .walls = 3,          /* DS:6d8a: slots 1-3 */
            .walls_restored = 3, /* 4b6d:1b34 */
            .roster_flag = COK_FIELD_COINS, /* 4b6d:0453 */
            .roster_flag_index = 6,
            .keep_last_mode = false,
            .vault = NULL,
            .vault_extension = NULL,
            .vault_temp = NULL,
        },
    .images =
        {
            .delta = champions_deltas,
            .collection = champions_collections,
            .single = champions_singles,
            .frames_ff = 255, /* 127f:1c2f */
        },
    .character = &champions_character,
    .characters =
        {
            .stats = COK_STATS_CHAMPIONS,   /* 6346:0d20 */
            .levels = COK_LEVELS_CHAMPIONS, /* 66c2:0433 */
            .import = NULL,
            .paladin_row = 0,         /* 6346:371e */
            .fighter_classes = 0x94, /* 60f4:1743, 1628: 2, 4, 7 */
        },
    .data = &champions_data,
    .ecl =
        {
            .opcodes = champions_opcodes,
            .count = COK_ECL_OPCODES,
            /* 2fd3:329b */
            .calls = {{0x2e10, COK_ECL_CALL_REDRAW},
                      {0xb203, COK_ECL_CALL_SOUND},
                      {0xc01e, COK_ECL_CALL_STEP},
                      {0xc018, COK_ECL_CALL_AHEAD},
                      {0x6803, COK_ECL_CALL_ANIMATE}},
            .variable_bytes = 2, /* 3775:0e06 */
            .vars = champions_vars,
            .start = champions_start,
            .start_count = sizeof champions_start / sizeof *champions_start,
            .reads = champions_reads,
            .read_count = sizeof champions_reads / sizeof *champions_reads,
            .stores = champions_stores,
            .store_count = sizeof champions_stores / sizeof *champions_stores,
            .character = champions_character_vars,
            .character_count = sizeof champions_character_vars / sizeof *champions_character_vars,
            .class_bits = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
            .compare_and_strings = false, /* 2fd3:0bbf */
            .delay_ms = 0,                /* 2fd3:2c33: speed * 100 */
            .menu_single_item = true,     /* 2fd3:116d */
        },
    .view =
        {
            /* DS:1ec2, and 6e22:01ab's bounds; a value past 0x127 halts. */
            .tile_first = {0x01, 0x2e, 0x74, 0xba, 0x100},
            .tile_last = {0x2d, 0x73, 0xb9, 0xff, 0x127},
            .tile_transparent = {13, 13, 13, 13, 13}, /* 6e22:0050 */
            .first_slot = 1,      /* 2fd3:0cf4 */
            .shared_tiles = false, /* 69ea:1025 */
            .move_tiles = true,    /* 69ea:1230 */
            .overhead_square = 0x104, /* 69ea:06a2 */
            .overhead_party = 0x100,  /* 69ea:000f */
            .overland_picture = 0x79, /* 2fd3:0914, 4877:0005 */
            .cga_backdrop = true,     /* 69ea:0184 */
        },
    .startup =
        {
            /* 3e99:005b: the frame's tile set and tile set 0. */
            .tiles = {{4, 202}, {0, 203}},
            .tile_count = 2,
            /* 3e99:06e0, 072a: COMSPR 0-11 in slots 13-24, 25 in 25. */
            .icons = {{0, 13, 12}, {25, 25, 1}},
            .icon_ranges = 2,
            .cursor_transparent = 13, /* 3e99:01fd */
            /* DS:6172: 0-7 the party's, 8 on monsters', 13-25 missiles. */
            .icon_slots = 26,
            .icon_table = 0x6172,
        },
    .spells =
        {
            .count = 0x6c,
            .names = champions_spell_names,
            .names_at = 0x2077,
            .name_size = 41,
            .levels = champions_spell_levels,
            .level_count = 10,
            .levels_at = 0x0bfa,
            .level_size = 41,
            .cast = champions_cast,
            .durations = champions_durations,
            .duration_count = sizeof champions_durations / sizeof *champions_durations,
            .ids = champions_spell_ids,
            .list_spells = 0x3a,  /* DS:473a */
            .list_scrolls = 0x31, /* DS:4774 */
            .granted_first = 101, /* 4888:0700 */
            .granted_last = 107,
            .item_last = 0x38, /* 3afb:04b2 */
            .item_shift = 0x17,
            .display_last = 0x38, /* 4888:17ae */
            .target_caster = 1,   /* 5b04:127e */
            .target_one = 2,
            .target_party = 4,
            .weapon_unscored = 0x53, /* 3afb:15e0 */
            /* 5b04:0083 */
            .usable_paladin = 0,
            .usable_ranger = 7,
            .usable_mage_class = false,
        },
    .effects =
        {
            .count = 0x79,
            .handlers = champions_effect_handlers,
            .events = champions_events,
            .event_count = 0x19,
            .shared = champions_shared,
            .shared_far_id = 0x31, /* 60f4:0352 */
            .shared_far = 6,
            .resisted_pending = champions_resisted_pending,
            .ids = champions_effect_ids,
            .display_spells = champions_display_spells,
            .display_texts = champions_display_texts,
            .display_text_count = sizeof champions_display_texts / sizeof *champions_display_texts,
            .save_fail_id = 0,
            .save_moon_wraps = true, /* 60f4:113a */
            .caster_knight_less = 8, /* 6346:29fe */
            .caster_ranger_less = 8,
            .unaffected = "is Unaffected", /* 60f4:20e9 */
        },
    .moons =
        {
            /* 57e4:0143's days; 57e4:001e's columns and tiles. */
            .days = {8, 1, 6},
            .column = {8, 19, 30},
            .tile = {10, 6, 14},
            .tile_base = 0x114,
        },
    .frame =
        {
            .base = 0x114, /* 1128's tiles: 0x114 + value */
            .screens =
                {
                    [COK_SCREEN_FRAME] = {COK_SCREENS, 1, 1, 38, 22, LINES(champions_frame)},
                    [COK_SCREEN_OPEN] = {COK_SCREENS, 1, 1, 38, 22, LINES(champions_open)},
                    [COK_SCREEN_ADVENTURE] = {COK_SCREEN_FRAME, 0, 0, 0, 0,
                                              LINES(champions_adventure)},
                    [COK_SCREEN_BIG] = {COK_SCREEN_FRAME, 0, 0, 0, 0, LINES(champions_row_16)},
                    [COK_SCREEN_SPELLS] = {COK_SCREENS, 1, 1, 38, 16, LINES(champions_spells)},
                    [COK_SCREEN_LIST] = {COK_SCREEN_OPEN, 0, 0, 0, 0, LINES(champions_list)},
                    [COK_SCREEN_COMBAT] = {COK_SCREENS, 0, 0, 39, 23, LINES(champions_combat)},
                    [COK_SCREEN_SHEET] = {COK_SCREENS, 0, 0, 39, 23, LINES(champions_sheet)},
                    [COK_SCREEN_CREDITS] = {COK_SCREENS, 1, 1, 38, 22, LINES(champions_credits)},
                },
            .menu_big = true,                  /* 1000:024b */
            .resumed = COK_RESUMED_BY_AREA, /* 4def:01b4 */
        },
    .start =
        {
            /* 2f55:063c: 1 at 0, 0 for 5 seconds; 2 at 0, 0 with 3 over it
             * at 5, 1 for 10; 4 at 0, 1 for 10; then the credits for 10
             * unless a key was pressed. */
            .pictures = {{1, 0, 0, false, 5}, {2, 0, 0, false, 0}, {3, 5, 1, false, 10},
                         {4, 0, 1, false, 10}},
            .picture_count = 4,
            .credits = champions_credit_lines,
            .credit_count = sizeof champions_credit_lines / sizeof *champions_credit_lines,
            .credits_wait = 1000, /* 2f55:0000 with 10 */
            .credits_always = false,
            .first_block = 0x24, /* 2fd3:3c28: Solace */
            .demo_file = 2,      /* 1000:0134 */
            .demo_block = 0x39,  /* 2fd3:3c28 */
            .items = {"Create New Character", "Drop Character", "Modify Character",
                      "Train Character", "Knight Change Classes", "View Character",
                      "Add Character to Party", "Remove Character from Party", "Load Saved Game",
                      "Save Current Game", "Begin Adventuring", "Initialize Mouse/Joystick",
                      "Exit to DOS"},
            .prompt = "Choose a FUNCTION ",
            .select = "Select ",
            .unported = {NULL},
            .pick_prompt = "Pick Character ", /* 546c:36cc */
            .pick_menu = "Select Exit",
            /* 4def:2668 */
            .drop = "Drop %s forever? ",
            .drop_sure = "Are you sure? ",
            .dumped = "You dump %s out back.",
            .farewell = "%s bids you farewell.",
            .relief = "%s breathes a sigh of relief.",
            .quit = "Quit to DOS ", /* 4def:055a */
            .unsaved = "Game NOT saved.  Quit anyway? ",
            .ask_colour = 14,
            .load_prompt = "Load Which Game: ", /* 4b6d:1b34 */
            .save_prompt = "Save Which Game: ", /* 4b6d:22de */
            .save_warning = "WARNING: Problem Saving Characters",
            .load_from = NULL, /* it imports no other game's */
            .add_from = NULL,
            .from_items = NULL,
            .overwrite = "Overwrite %s? ", /* 4b6d:0bed */
            .new_name = "New file name: ",
            .add_prompt = "Add a character: ", /* 4def:37a2 */
            .add_select = "Add ",
            .rangers = "too many rangers in party",
            .paladins = true,
            .add_space_leaves = true, /* 4def:3ae7 */
            .add_loading = true,
            .add_unmarks = true,
            .reset_when_empty = false,
            .quit_after_save = false,
            .begin_last_mode = false,
            .program_marks = false,
            .reset_wall = 0, /* 3e99:0843 */
        },
    .combat =
        {
            /* 2fd3:191c: a shop, the temple, else treasure. */
            .vault = false,
            .show_none = false, /* 3775:0575 */
            /* 3995:0172 */
            .leadership = false,
            .explode = true, /* 60f4:2375 */
            .ends_in_round = false,
            /* 3cb2:1c58, 1029 */
            .begins = "A battle begins...",
            .dungeon_tiles = 0x19,
            .wilderness_tiles = 0x21,
            .tiles_kept = false,
            .wilderness = COK_WILDERNESS_CHAMPIONS,
            .dungeon_fill = false,
            .door_corners = false,
            /* 3cb2:17f7 */
            .placement_clears = 255,
            .ambush = false,
            .ambush_distance = 0,
            .demo_distance = 0,
            .body_under = false,
            .status_9 = true,
            /* 351b:1968 */
            .weapons_back = true, /* 351b:185f */
            .shares = true,       /* 351b:1618 */
            .combat_effects = champions_combat_effects,
            .may_train = false, /* 351b:0379 */
            /* 351b:0b23 */
            .fled = "The party has fled.",
            .lost = "You have lost the fight.",
            .won = "The party has won.",
            .found = "The party has found Treasure!",
            .receives = "Each character receives ",
            .points = "experience points.",
            .found_none = NULL,
            .withdrawal = "The party makes a withdrawl.  A small",
            .fee = "fee has been assessed by the bank.",
            .results_key = "press <enter>/<return> to continue", /* 67b5:03e2 */
            .treasure_if_any = false,
            /* 351b:1aab */
            .rejoice = "The monsters rejoice for the party has been destroyed",
            .rejoice_key = "Press any key to continue",
        },
    .fight =
        {
            .words =
                {
                    /* 432f:033e's (CS:0277-0334) */
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
                    .got_away = "Got Away",           /* 432f:0dac */
                    .blocked = "Escape is blocked",   /* 432f:0db5 */
                    .destroyed = "Is destroyed",      /* 432f:12b7 */
                    .nothing = "Nothing Happens...",
                    .panic = "flees in panic",        /* 3afb:003c */
                    .surrenders = "Surrenders",       /* 3afb:130b */
                    .magic_on = "Magic On",           /* 3afb:11ed */
                    .magic_off = "Magic Off",
                    .teammate_dying = "Your Teammate is Dying", /* 3995:0b45 */
                    .continue_battle = "Continue Battle:",      /* 3995:0b5c */
                    .bandaged = "is bandaged",        /* 6346:31dd */
                    .hit_points = "Hitpoints",        /* 6346:0ad4 */
                    .helpless = "(Helpless)",
                    .casting = "(Casting)",
                    .guarding = "Guarding",           /* 6346:29aa */
                    .gods_refused = "That doesn't work", /* 3995:0561 */
                },
            .cloud_tile = 0x1e, /* 6beb:0c9d */
            .pool_tile = 0x1d,
            .clouds_terrain = false,
            .body_spares_pool = false, /* 6beb:0e08 */
            .kill_finds_body = false,
            .kill_clears_spell = true,
            /* 3afb:004b */
            .computer_target_first = false,
            .computer_items = true, /* 3afb:04b2 */
            .computer_cast = COK_COMPUTER_CAST_CHAMPIONS, /* 3afb:03b9 */
            .turn_limit = 0,     /* 3afb:024b's [bp-6] */
            .paladin_turns = 0,
            .reach_less = 1,     /* 3afb:0d49 */
            .morale_signed = false, /* 3afb:1316 */
            .flee_removes = true,
            .flee_turn = 0,      /* 3afb:095a */
            .ways_guarded = false,
            .attack_sides = false, /* 432f:11d4 */
            .free_attack_effect = 0, /* 432f:0986 */
            .quiet_flag = 0,
            .forget_mark = 0,    /* 6346:161b */
            .arrows = champions_arrows, /* 432f:2beb */
            .missile_clamped = false,
            .burst_slot = 0x17, /* 6346:228c */
            .turning = COK_TURNING_CHAMPIONS, /* 432f:12b7 */
        },
    .treasure =
        {
            /* None: TREASURE reads ITEM<file> records whole (2fd3:1d21). */
            .table = {NULL, 0, 0},
            .table_count = 0,
            /* 351b:118c, 0ff7, 0ec5; 58e7:0d01, 0b95 */
            .menus_first = false,
            .specials_apart = false,
            .left = "There is still treasure left.  ",                  /* 351b:1133 */
            .claim = "Do you want to go back and claim your treasure?", /* 351b:113c */
            .claim_no = false,
            .take_prompt = "Take: ",
            .items_prompt = "Items: ",
            .coin_prompt = "Select type of coin ",
            .how_many = "How Many ",
            .how_much = "How much ",
            .coin_words = {"silver ", "Copper ", "bronze ", "platinum ", "steel ", "Gems ",
                           "Jewelry "},
            /* 36d0:034c */
            .overloaded = "OverLoaded",
            .take_joins = false,
            .vault_items = NULL,
            /* 2fd3:2c80 */
            .damage_bonus = 0x1f,
            .damage_halves = false,
            .damage_key = "press <enter>/<return> to continue",
            /* 2fd3:36dc */
            .gains = "Congratulations ",
            .gains_end = " gains experience!",
            .party_gains = "Congratulations the party gains experience!",
            .gains_active_first = false,
        },
    .levels =
        {
            /* 4def:4d9e */
            .conscious = "we only train conscious people",
            .wrong_hall = "We don't train that class here",
            .no_experience = "Not Enough Experience",
            .will_become = " will become:",
            .a_level = "    a level ",
            .and_level = "and a level ",
            .question = "Do you wish to train? ",
            .congratulations = "Congratulations...",
            .question_yes = false, /* 67b5:177f */
            .free_training = true,
            .race_limits = false,
            .stale_row = 0x74, /* see train.c */
            .ranger_rises = false,
            .top_hit_points = {2, 0, 3, 0, 2, 1, 2, 3}, /* 4def:4b3a */
            .hit_points_floor = false,
            /* 4def:5812, 567f */
            .knight_of = "Knight Of The ",
            .granted = {"Sword", "Rose"},
            .inexperienced = {" is too inexperienced.", " is too inexperienced"},
            .no_order = "", /* the start menu's rows freed (4def:0381) */
            .change_always = false,
            .specials_apart = false,
            .promote_fixed = 0,
            /* No aging after a battle. */
            .ages = NULL,
            .age_races = 0,
            /* 5b04:027a, 4888:0b24 */
            .choose_prompt = "Choose Spell: ",
            .table_labels = {"    Cleric Spells:", "     Druid Spells:", "   Special Spells:",
                             "Magic-User Spells:"},
            .bonus_label = "     Bonus Spells",
            .table_column = 20,
            .bonus_column = 20,
            /* 4888:1098, 132b, 06b8 */
            .keep = NULL,
            .keep_prompt = NULL,
            .memorize_question = {"Memorize These Spells? ", "Memorize these spells? "},
            .scribe_question = {"Scribe These Spells? ", "Scribe these spells? "},
            .question_colour = 14,
            .questions_yes = false,
            .known = "You already know that spell",
            .scribing = "You are already scibing that spell",
            .cannot_scribe = "You can not scribe that spell.",
            .camp_forgets_memorized = true,
            .display_heading = 11, /* 4888:17ae */
            .display_blank_headings = false,
            .magic_selection = false, /* 4888:1c32 */
            .fix_unported = NULL,
        },
};

static const cok_rules *const games[] = {&cok_rules_champions, &cok_rules_death_knights};

const cok_rules *cok_rules_find(const char *id)
{
    for (size_t i = 0; i < sizeof games / sizeof *games; ++i)
        if (strcmp(id, games[i]->id) == 0) return games[i];
    return NULL;
}

const char *cok_rules_names(void)
{
    static char names[256];
    if (names[0] == '\0')
        for (size_t i = 0; i < sizeof games / sizeof *games; ++i) {
            size_t n = strlen(names);
            snprintf(names + n, sizeof names - n, "%s%s", i > 0 ? ", " : "", games[i]->id);
        }
    return names;
}
