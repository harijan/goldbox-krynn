#include "rules.h"

#include "data_champions.h"

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
    {0x390b, sizeof cok_champions_icon_colours, 1, cok_champions_icon_colours},
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
        },
};

const cok_rules cok_rules_champions = {
    .id = "champions",
    .assets = "COKAssets",
    .text =
        {
            .name = "Champions of Krynn",
            .version = "Champions of Krynn v1.2",       /* 1000:0134, at startup */
            .version_again = "Champions Of Krynn v1.2", /* 1000:0313's, after the demo */
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
            .cursor = {"CURSOR", 0, 1}, /* 3e99:01fd */
            .title = "TITLE",
            .treasure = "ITEM",
            .monster = "MON",
            .monster_record = "CHA",
            .monster_effects = "SPC",
            .monster_items = "ITM",
            .dungeon = "DUNGCOM",
            .wilderness = "WILDCOM",
            .random = "RANDCOM",
            .music_drivers = "MIDI",
            .music_score = "SOUND",
            .file_digit = champions_file_digit,
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
            .debug = "DEBUG.TXT",
        },
    .images =
        {
            .delta = champions_deltas,
            .collection = champions_collections,
            .single = champions_singles,
        },
    .character = &champions_character,
    .data = &champions_data,
};

static const cok_rules *const games[] = {&cok_rules_champions};

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
