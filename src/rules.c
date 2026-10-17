#include "rules.h"

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
