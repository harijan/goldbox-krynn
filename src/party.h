#ifndef COK_PARTY_H
#define COK_PARTY_H

#include "ecl.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The party and its characters, from overlays 4b6d (character and saved
 * game files), 546c and 6346 (the party list). The original keeps the party
 * as a linked list of 409-byte records from DS:609a, next at +0x17f; here
 * it is an array in the same order. Record fields (offsets):
 *
 *   +0x000  name, a Pascal string[15]
 *   +0x011  strength (+0x1c exceptional strength), +0x015 wisdom,
 *           +0x017 dexterity, +0x019 constitution
 *   +0x059  base THAC0 as 60 - THAC0 (66c2:0433)
 *   +0x05a  race (6 human), +0x05c knightly order, +0x05d deity,
 *           +0x05e order of magic
 *   +0x062  maximum hit points; +0x062 + N is set when spell N is known
 *   +0x0ce  highest fighter, ranger or knight level (6346:0d20); it is
 *           also the byte of spell 108
 *   +0x0d0  saving throws, by type (66c2:08a6)
 *   +0x0d5  base movement
 *   +0x0d6  highest level; +0x0d7 a human's level must pass to use its
 *           former class (66c2:0efb)
 *   +0x0db  thief skills, eight (66c2:0b9f)
 *   +0x0e7  0x80 and up for NPCs
 *   +0x0ed  coins, six words, weighed with the items
 *   +0x0f9  levels in each of eight classes (signed bytes): 0 cleric,
 *           2 fighter, 4 ranger, 5 mage, 6 thief, 7 knight
 *   +0x101  levels in former classes, the same way
 *   +0x10b  set to 3 at high fighter, ranger and knight levels
 *   +0x10d  base attacks, dice and damage bonus, two of each
 *   +0x113  base armour class as 60 - AC
 *   +0x114  set when strength adds to hit and damage
 *   +0x116  experience, 32 bits
 *   +0x11a  the classes whose items it can use, as bits (DS:38ea)
 *   +0x11c  spells a day: cleric levels 1-5, ranger druid 1-3 (+0x121),
 *           mage 1-5 (+0x12b)
 *   +0x137  combat icon slot
 *   +0x142  number of items, +0x17b hands in use, +0x17d weight carried
 *   +0x17c  saving throw bonus (signed)
 *   +0x188  status: 0 okay, 4 unconscious, 5 dying, 6 dead
 *   +0x189  set while the character can act; cleared when it drops
 *   +0x18a  1 for a character fighting against the party
 *   +0x18c  THAC0 as 60 - THAC0
 *   +0x18d  armour class as 60 - AC, +0x18e from behind
 *   +0x191  attacks, +0x193 dice sides, +0x195 damage bonus, two of each
 *   +0x197  hit points
 *   +0x198  movement
 *
 * The derived fields are recomputed when a character loads, by 6346:0d20
 * (items, abilities) and then 66c2:0433 (classes and levels). */

enum {
    COK_PARTY_MAX = 8,          /* The game adds characters while 0x7f3e < 8. */
    COK_ITEM_SIZE = 63,
    COK_EFFECT_SIZE = 9,
    COK_SAVED_GAME_SIZE = 5469,
};

/* An item record (.STF): +0x2e its type, +0x32 its bonus (signed), +0x33
 * a saving throw bonus, +0x34 set while readied, +0x36 set to keep
 * 66c2:0433 from unreadying it, +0x37 its weight (a word), +0x39 how many,
 * +0x3e 0x80 plus a special power (1 doubles mage spells 1-3, 2 and 11
 * raise thief skills, 6 adds the constitution bonus to saving throw 0). */
enum {
    COK_ITEM_TYPES = 129,       /* 0x810 bytes; ITEMS fills 128. */
    COK_ITEM_SLOTS = 13,        /* Far pointers at +0x147 in the original. */
};

/* The item types (DS:5886), 16 bytes each, read from ITEMS at startup
 * (3e99:005b). Bytes 0 the slot it is readied in (0 weapon, 1 shield, 2
 * armour, 9 ring, 10 none), 1 the hands it takes, 6 0x80 plus its armour
 * class bonus, 9-11 attacks, dice sides and damage bonus, 13 the classes
 * that can use it (bits as +0x11a), 14 flags: 1 adds the bonus of readied
 * arrows (type 0x1e), 2 dexterity to hit, 4 strength, 0x80 the bonus of
 * readied quarrels (type 0x0c). */
typedef struct {
    uint8_t type[COK_ITEM_TYPES][16];
} cok_item_types;

/* Read the item types from path. A short file leaves the rest zero, as the
 * original reads what there is into its zeroed data. Returns false with
 * error set if it cannot be read. */
bool cok_item_types_read(const char *path, cok_item_types *types, char *error, size_t error_size);

typedef struct {
    uint8_t record[COK_CHARACTER_SIZE];
    /* Items (.STF) and spell effects (.SFX) as stored, in file order; the
     * original links them from +0x143 and +0xe3. */
    uint8_t (*items)[COK_ITEM_SIZE];
    size_t item_count;
    uint8_t (*effects)[COK_EFFECT_SIZE];
    size_t effect_count;
    /* The readied items 6346:0d20 finds, as 1 + their index in items, or 0
     * for none: the slots 0-8 of their types, two rings (9, 10), arrows
     * (11, type 0x1e) and quarrels (12, type 0x0c). The original keeps far
     * pointers at +0x147; the port sets those bytes to 0. */
    size_t slots[COK_ITEM_SLOTS];
} cok_character;

typedef struct {
    cok_character *members[COK_PARTY_MAX];
    size_t count;
} cok_party;

/* The file name the game makes from a character name (169c:05da): remove
 * " .*,?/\:;|", keep 8 characters and upper-case them. */
void cok_party_file_name(const char *name, char out[9]);

/* Read character file base (with no extension) from dir: base.SAV, a
 * 409-byte record, then the items in base.STF and the effects in base.SFX
 * if those exist (4b6d:11e5). The record's stale pointers are cleared.
 * Then, as 4b6d:11e5 ends, its stats are recomputed with
 * cok_character_stats and cok_character_levels. Returns false with error
 * set. Free with cok_character_free. */
bool cok_character_read(cok_character *character, const char *dir, const char *base,
                        const cok_item_types *types, char *error, size_t error_size);
void cok_character_free(cok_character *character);

/* Recompute the stats that come from the items and abilities (6346:0d20):
 * the readied slots, item count, hands, weight carried, attacks and
 * damage, THAC0, armour class and movement. Each table lookup follows the
 * original's address arithmetic over the original's initialized data.
 * Where the original would read data the game sets as it runs (an item
 * type past 128) or leaves its result uninitialized (a strength past 25 or
 * past 18/00), it returns false with error set, and the record is left
 * part-way. */
bool cok_character_stats(cok_character *character, const cok_item_types *types, char *error,
                         size_t error_size);

/* Recompute the stats that come from classes and levels (66c2:0433): base
 * THAC0, highest level, attacks, spells a day and known, saving throws,
 * thief skills and usable classes, then unready the items in the slots
 * (those cok_character_stats last found; none before it runs) that its
 * classes cannot use, unless their +0x36 is set. The stats of 6346:0d20
 * are not recomputed afterwards, as in the original. Returns false with
 * error set, as cok_character_stats does, where a saving throw for a level
 * of 90 or more reads past the initialized data. */
bool cok_character_levels(cok_character *character, const cok_item_types *types, char *error,
                          size_t error_size);

/* 66c2:0efb: whether a human's first class with a level is above its level
 * at +0xd7, so that it can use its former class (66c2:0eab). */
bool cok_character_former_class(const uint8_t *record);

/* The original's data the stat routines read, to check against the
 * executable: the DS offset of the first byte, the number of bytes, and
 * the stride between them (1, or 16 for a column of the spell table). */
typedef struct {
    uint16_t offset, size, stride;
    const uint8_t *bytes;
} cok_ds_table;
extern const cok_ds_table cok_stat_tables[];
extern const size_t cok_stat_table_count;

/* Add a character to the end of the party (4b6d:1989), giving it the lowest
 * combat icon slot (+0x137) no member uses. The party takes ownership of a
 * malloc'd character. Returns false, leaving it with the caller, when the
 * party is full. The original then runs 66c2:0433 on an NPC (+0xe7 0x80
 * and up); the caller does that with cok_character_levels. */
bool cok_party_add(cok_party *party, cok_character *character);
/* Remove member index and free it. */
void cok_party_remove(cok_party *party, size_t index);
void cok_party_free(cok_party *party);

/* Member index's record, or NULL past the end. */
uint8_t *cok_party_record(const cok_party *party, size_t index);
/* Position of record in the party, from 0 (3775:0773); the party size if
 * it is not a member. */
size_t cok_party_index(const cok_party *party, const uint8_t *record);

/* The character a special key selects from a menu (546c:3334): up (0x48)
 * the one before selected, wrapping to the last; down (0x50) the one after,
 * wrapping to the first; any other key the first. NULL for an empty party,
 * or for up when selected is not a member. */
uint8_t *cok_party_special(const cok_party *party, const uint8_t *selected, uint8_t scan);

/* Draw the party list (6346:07ba) with names from column x: "Name" and
 * "AC  HP" on row 2, then a row per member from row 4, clearing each row to
 * column 38 first and one row after the list. The selected member's name
 * is white; others are light cyan, light red when they cannot act, or
 * yellow in combat when they fight against the party (6346:199d). AC is
 * light green, ending at column 34 (6346:0984); hit points end at column
 * 38, yellow when below the maximum, light green otherwise (6346:0a0d). */
void cok_party_draw(cok_picture *dst, const cok_font *font, const cok_party *party,
                    const uint8_t *selected, int x, bool combat);

/* Apply damage to a character's hit points and status (6346:24d7):
 * damage of exactly its hit points leaves it unconscious, up to 9 more
 * dying, and 10 or more past them dead, as is any damage that takes the
 * last hit point of a character with status 1. A character not okay
 * afterwards has 0 hit points and cannot act. The original also updates
 * the combat records in combat; combat is not ported. */
void cok_character_damage(uint8_t *record, uint8_t damage);

/* A saved game, SAVGAM<letter>.DAT, as 4b6d:1b34 reads it. */
typedef struct {
    uint8_t file;                /* DS:5782, the ECL file. */
    uint16_t mem4b00[0x400];     /* Variables 0x4b00-0x4eff. */
    uint16_t mem7c00[0x400];     /* 0x7c00-0x7fff. */
    uint16_t mem7a00[0x200];     /* 0x7a00-0x7bff. */
    int8_t map_x, map_y;         /* DS:6d85-6d89. */
    uint8_t direction, ahead, square;
    uint8_t last_mode, mode;     /* DS:4b4a, 4b49. */
    int16_t wall_ids[3];         /* DS:6d8a, 6d8e, 6d92: WALLDEF record or -1. */
    int16_t wall_slots[3];       /* DS:6d8c, 6d90, 6d94: the slot it loaded into. */
    uint8_t count;               /* Characters. */
    char names[COK_PARTY_MAX][41]; /* Their files, e.g. CHRDATA1; extra entries are junk. */
} cok_saved_game;

/* Read a saved game. Returns false with error set if the file cannot be
 * read or is shorter than COK_SAVED_GAME_SIZE; the original does not check. */
bool cok_saved_game_read(const char *path, cok_saved_game *game, char *error, size_t error_size);

#endif
