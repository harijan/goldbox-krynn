#ifndef COK_PARTY_H
#define COK_PARTY_H

#include "ecl.h"
#include "rules.h"
#include "text.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The party and its characters, from overlays 4b6d (character and saved
 * game files), 546c and 6346 (the party list). The original keeps the party
 * as a linked list of records from DS:609a; here it is an array in the
 * same order. A character's record is raw bytes, as the files hold it,
 * whose fields code reads by name through the game's layout (field.h;
 * Champions' fields, with what each is, in rules.c).
 *
 * The derived fields are recomputed when a character loads, by 6346:0d20
 * (items, abilities) and then 66c2:0433 (classes and levels). The routines
 * here that read a record take the game's rules for its layout. */

enum {
    COK_PARTY_MAX = 8,          /* The game adds characters while 0x7f3e < 8. */
    COK_PARTY_RECORDS = 72,     /* The party and the monsters after it (8 + 63); combat's
                                 * tables hold 72. */
    COK_ITEM_SIZE = 63,
    COK_EFFECT_SIZE = 9,
    COK_SAVED_GAME_SIZE = 5469, /* Champions', the most a layout makes (cok_saved_game_size) */
    COK_SAVED_WALLS = 4,        /* The most entries of a saved wall table (rules' saves.walls). */
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

/* A spell effect, a 9-byte record in the original (and in .SFX files):
 * +0 its id, +1 the minutes it has left as a word, 0 for one that does not
 * end (57e4:0171), +3 a value its handler uses, +4 set to call the handler
 * with flag 1 when it is removed, +5 a far pointer to the next. The port
 * keeps the list linked as the original does, and does not keep the stale
 * pointers of the files. */
typedef struct cok_effect {
    uint8_t id;
    uint16_t duration;
    uint8_t value;
    bool on_remove;
    struct cok_effect *next;
    /* When it was made, as cok_effects_made counted then: the timers'
     * walk (57e4:0171) compares addresses, which the heap may give an
     * effect made after another was freed. */
    uint32_t made;
} cok_effect;

/* How many effects cok_character_add_effect has made. */
uint32_t cok_effects_made(void);

/* The combat record (22 bytes at the far pointer +0x183), which combat
 * setup (3cb2:10d9) allocates zeroed for every record in the list and the
 * end of combat (351b:1493) frees; outside combat the pointer is NULL,
 * which the port reads as all zero (cok_character_combat), as the
 * original's end of combat reads 0000:0013 for +0x13 when no battle ran. */
typedef struct {
    uint8_t spell;       /* +0x00: a spell being cast, cast at its next turn. */
    uint8_t may_cast;    /* +0x01: may cast this round. */
    uint8_t may_use;     /* +0x02: may use items this round. */
    int8_t initiative;   /* +0x03: 0 once it has acted or cannot. */
    uint8_t attack_slot; /* +0x04: the attack slot in progress. */
    uint8_t sweeps;      /* +0x05: sweeps left this round. */
    uint8_t movement;    /* +0x06: movement left, in half squares. */
    uint8_t guarding;    /* +0x07. */
    uint8_t attacked;    /* +0x08: has attacked this round. */
    uint8_t facing;      /* +0x09: 0-7, north first, clockwise. */
    uint8_t *target;     /* +0x0a: a far pointer to the record it aims at. */
    uint8_t dying;       /* +0x0e: rounds dying. */
    uint8_t hits;        /* +0x0f: times attacked since its turn or move. */
    uint8_t forced;      /* +0x10: made to flee by an effect. */
    uint8_t turns;       /* +0x11: attempts to turn undead. */
    uint8_t turning;     /* +0x12: turning from attackers, for attacks from behind. */
    uint8_t not_party;   /* +0x13: set past the party's size (0x7f3e) in the list. */
    uint8_t fleeing;     /* +0x14. */
    uint8_t pattern;     /* +0x15: the AI's way of moving. */
} cok_combat_record;

typedef struct {
    /* The record, the layout's size of it (COK_CHARACTER_MAX holds any
     * game's). */
    uint8_t record[COK_CHARACTER_MAX];
    /* Items (.STF) as stored, in file order; the original links them from
     * +0x143. */
    uint8_t (*items)[COK_ITEM_SIZE];
    size_t item_count;
    /* Spell effects (.SFX), linked from +0xe3 in the original, in file order. */
    cok_effect *effects;
    /* The readied items 6346:0d20 finds, as 1 + their index in items, or 0
     * for none: the slots 0-8 of their types, two rings (9, 10), arrows
     * (11, type 0x1e) and quarrels (12, type 0x0c). The original keeps far
     * pointers at +0x147; the port sets those bytes to 0. */
    size_t slots[COK_ITEM_SLOTS];
    /* An item a routine holds across a call that may take items away or
     * add them, as 1 + its index, kept on the same item by
     * cok_character_remove_item and cok_character_insert_item; 0 for none,
     * or once that item is gone. The original holds a pointer. */
    size_t held;
    /* The combat record (+0x183), or NULL outside combat. */
    cok_combat_record *combat;
} cok_character;

/* The list from DS:609a: the party, then any monsters LOAD MONSTER
 * appended, which stay until the end of combat removes them. count is the
 * number of records in it; the party's size is var 0x7f3e. */
typedef struct {
    cok_character *members[COK_PARTY_RECORDS];
    size_t count;
} cok_party;

/* The file name the game makes from a character name (169c:05da): remove
 * " .*,?/\:;|", keep 8 characters and upper-case them. Each character is
 * removed by 169c:050b, which does not test again the position a deletion
 * slides the next one into, so of two alike in a row only the first goes:
 * "A  B" gives "A B". */
void cok_party_file_name(const char *name, char out[9]);
/* The file name of a roster character or a saved game's (4b6d:0bed,
 * 4b6d:1b34): its dots removed by 169c:050b first, then
 * cok_party_file_name. */
void cok_party_roster_name(const char *name, char out[9]);

/* Read character file base (with no extension) from dir: base.SAV, a
 * record of the layout's file_size (409 bytes in Champions, Death Knights'
 * .GAM 216), then, for a game that keeps the spells in a block of their
 * own, the block in base.WIZ if it exists (dkk 46b2:10d7), then the items
 * in base.STF and the effects in base.SFX if those exist (4b6d:11e5), with
 * the rules' extensions (saves.record, spells, items, effects). The
 * record's stale pointers are cleared. Then, as 4b6d:11e5 ends, its stats
 * are recomputed with cok_character_stats and cok_character_levels.
 * Returns false with error set. Free with cok_character_free. */
bool cok_character_read(cok_character *character, const cok_rules *rules, const char *dir,
                        const char *base, const cok_item_types *types, char *error,
                        size_t error_size);
/* As cok_character_read, from base.extension (4b6d:11e5 takes the file's
 * whole name): "WHO" for the start menu's roster (4def:37a2), Death
 * Knights' "PC". */
bool cok_character_read_file(cok_character *character, const cok_rules *rules, const char *dir,
                             const char *base, const char *extension,
                             const cok_item_types *types, char *error, size_t error_size);
/* Import another game's character into the game of rules, as its
 * rules.characters.import says (Death Knights' of Champions' characters,
 * dkk 46b2:10d7): base.extension from dir, with the other game's record
 * size, and its items and effects by the other game's extensions; the
 * record converted, the items of the types it drops left out; then
 * recomputed as cok_character_read does. The game must import. Returns
 * false with error set. */
bool cok_character_import(cok_character *character, const cok_rules *rules, const char *dir,
                          const char *base, const char *extension,
                          const cok_item_types *types, char *error, size_t error_size);
/* Free the items, effects and combat record. */
void cok_character_free(cok_character *character);

/* The character's combat record, or one of all zeros when it has none. */
const cok_combat_record *cok_character_combat(const cok_character *character);

/* Take item index from character and free it (6346:1697), which changes
 * nothing else in the original: the item count (+0x142) and the readied
 * slots wait for the stats to be recomputed. The port renumbers the slots,
 * which the original keeps as pointers, and the held item. */
void cok_character_remove_item(cok_character *character, size_t index);
/* Insert a copy of item at index of character's items (6346:178b appends
 * one), renumbering the slots and the held item. Returns false when out of
 * memory. */
bool cok_character_insert_item(cok_character *character, size_t index, const uint8_t *item);

/* Add an effect at the end of the character's list (60f4:1285): id, minutes
 * left (0 for none), its value and whether its handler runs when it is
 * removed. Returns the effect, or NULL when out of memory, where the
 * original does not check. Effects are removed with cok_effects_remove
 * (effect.h), which runs their handlers. */
cok_effect *cok_character_add_effect(cok_character *character, uint8_t id, uint16_t duration,
                                     uint8_t value, bool on_remove);
/* The first effect with id, or NULL (6346:2447). */
cok_effect *cok_character_find_effect(const cok_character *character, uint8_t id);

/* The first effect with the named id in the game (spells.h), or NULL,
 * also where the game has no such effect: no character has it. */
static inline cok_effect *cok_character_find_named(const cok_character *character,
                                                   const cok_rules *rules, cok_named_effect name)
{
    assert(rules != NULL && rules->effects.ids != NULL && (unsigned)name < COK_EFFECT_NAMES);
    uint8_t id = rules->effects.ids[name];
    return id == 0 ? NULL : cok_character_find_effect(character, id);
}

/* Whether the stats (cok_character_stats) count the effects that change
 * the movement (dkk 65cd:1241), so that what reads the movement takes it
 * as it is (dkk 580d:0751); Champions' (6346:0d20) leave them to its
 * readers (the sheet's 546c:07bb, combat's 432f:014d). */
static inline bool cok_stats_count_movement_effects(const cok_rules *rules)
{
    return rules->characters.stats == COK_STATS_DEATH_KNIGHTS;
}

/* Whether a character may have former classes, a human's who changed class
 * (FORMER_LEVEL, 66c2:0433, 66c2:0eab): Death Knights' have none. */
static inline bool cok_levels_former_classes(const cok_rules *rules)
{
    return rules->characters.levels == COK_LEVELS_CHAMPIONS;
}

/* A character's former level in class k (+0x101 + k) and the level it
 * changed class at (+0xd7), as signed bytes: 0 in a game without former
 * classes, whose records have neither (Death Knights), where the
 * Champions rules that read them read 0. */
static inline int8_t cok_character_former_level(const cok_rules *rules, const uint8_t *c, size_t k)
{
    return cok_levels_former_classes(rules)
               ? cok_field_s8_at(rules->character, c, COK_FIELD_FORMER_LEVEL, k)
               : 0;
}

static inline int8_t cok_character_change_level(const cok_rules *rules, const uint8_t *c)
{
    return cok_levels_former_classes(rules) ? cok_field_s8(rules->character, c, COK_FIELD_CHANGE_LEVEL)
                                            : 0;
}

/* The strength from the base scores (+0x10, +0x1d for exceptional
 * strength) changed by the readied items' powers (+0x3e 0x80 + power, +0x3d
 * its kind) and the effects STRENGTH, GIANT_STRENGTH and ENLARGED, into
 * the current one (60f4:1743 with 0; dkk 638c:16d8): an item of power 0x0d
 * makes it 3. */
void cok_character_strength(cok_character *character, const cok_rules *rules);

/* Recompute the stats that come from the items and abilities (6346:0d20):
 * the readied slots, item count, hands, weight carried, attacks and
 * damage, THAC0, armour class and movement. Each table lookup follows the
 * original's address arithmetic over the original's initialized data.
 * Where the original would read data the game sets as it runs (an item
 * type past 128) or leaves its result uninitialized (a strength past 25 or
 * past 18/00), it returns false with error set, and the record is left
 * part-way. Death Knights' (dkk 65cd:0e46, rules.characters.stats) also
 * counts the effects on the movement (65cd:1241) and sets no fighter
 * level. */
bool cok_character_stats(cok_character *character, const cok_rules *rules,
                         const cok_item_types *types, char *error, size_t error_size);

/* Recompute the stats that come from classes and levels (66c2:0433): base
 * THAC0, highest level, attacks, spells a day and known, saving throws,
 * thief skills and usable classes, then unready the items in the slots
 * (those cok_character_stats last found; none before it runs) that its
 * classes cannot use, unless their +0x36 is set. The stats of 6346:0d20
 * are not recomputed afterwards, as in the original. Returns false with
 * error set, as cok_character_stats does, where a saving throw for a level
 * of 90 or more reads past the initialized data. Death Knights' (dkk
 * 69a8:050b, rules.characters.levels) has its own spells a day, in the
 * spells' block, thief skills and attacks, and no former classes. */
bool cok_character_levels(cok_character *character, const cok_rules *rules,
                          const cok_item_types *types, char *error, size_t error_size);

/* The parts of 66c2:0433 that character creation (4def:06dd) and Modify
 * (4def:28fa) call alone: a cleric's spells a day with its wisdom bonus
 * (66c2:0722), the saving throws (66c2:08a6), and the thief skills
 * (66c2:0b9f) with extra, the byte its uninitialized local holds. Each
 * returns false with error set as cok_character_levels does. Death
 * Knights' (rules.characters.levels): 69a8:071e, 08ff and 0b3d, the thief
 * skills without extra. */
bool cok_character_cleric_spells(cok_character *character, const cok_rules *rules, char *error,
                                 size_t error_size);
bool cok_character_saving_throws(cok_character *character, const cok_rules *rules, char *error,
                                 size_t error_size);
bool cok_character_thief_skills(cok_character *character, const cok_rules *rules, uint8_t extra,
                                char *error, size_t error_size);

/* The weight c may carry before its strength slows it (6346:153b), which
 * can be negative. Returns false with error set where its strength row is
 * uninitialized (a strength past 25 or past 18/00). */
bool cok_character_allowance(const uint8_t *c, const cok_rules *rules, int16_t *allowance,
                             char *error, size_t error_size);

/* 66c2:0efb: whether a human's first class with a level is above its level
 * at +0xd7, so that it can use its former class (66c2:0eab). Never in a
 * game whose records have no such level (COK_FIELD_CHANGE_LEVEL): Death
 * Knights has no former classes. Its readers that then read the former
 * levels (COK_FIELD_FORMER_LEVEL), which such a game has not, read them
 * only when it holds. */
bool cok_character_former_class(const uint8_t *record, const cok_rules *rules);

/* Add a character to the end of the list (4b6d:1989), after any monsters,
 * giving it the lowest combat icon slot 0-7 (+0x137) no record uses, or 8
 * when all are. The party takes ownership of a malloc'd character. Returns
 * false, leaving it with the caller, when the list is full; the original's
 * callers add characters while var 0x7f3e is below 8, and the caller
 * counts it there. The original then runs 66c2:0433 on an NPC (+0xe7 0x80
 * and up); the caller does that with cok_character_levels. */
bool cok_party_add(cok_party *party, const cok_rules *rules, cok_character *character);
/* Append a record to the end of the list, as LOAD MONSTER does
 * (2fd3:0465), taking ownership of a malloc'd character. Returns false,
 * leaving it with the caller, when the list holds COK_PARTY_RECORDS. */
bool cok_party_append(cok_party *party, cok_character *character);
/* Move member index one place up (toward the first) or down, as Party
 * Order does (4888:1cea, 4888:1e42): the first moved up goes to the end and
 * the last moved down to the front. Nothing else changes. */
void cok_party_move(cok_party *party, size_t index, bool down);
/* Remove member index and free it. */
void cok_party_remove(cok_party *party, size_t index);
void cok_party_free(cok_party *party);

/* Member index's record, or NULL past the end. */
uint8_t *cok_party_record(const cok_party *party, size_t index);
/* Position of record in the party, from 0 (3775:0773); the party size if
 * it is not a member. */
size_t cok_party_index(const cok_party *party, const uint8_t *record);

/* The character a special key selects from a menu (546c:3334), from the
 * whole list, monsters loaded too: up (0x48) the one before selected,
 * wrapping to the last, or the last with none selected; down (0x50) the one
 * after, wrapping to the first; any other key the first. NULL for an empty
 * list, or for up when selected is a record not in it. */
uint8_t *cok_party_special(const cok_party *party, const uint8_t *selected, uint8_t scan);

/* Draw the party list (6346:07ba) with names from column x: "Name" and
 * "AC  HP" on row 2, then a row per member from row 4, clearing each row to
 * column 38 first and one row after the list. The selected member's name
 * is white; others are light cyan, light red when they cannot act, or
 * yellow in combat when they fight against the party (6346:199d). AC is
 * light green, ending at column 34 (6346:0984); hit points end at column
 * 38, yellow when below the maximum, light green otherwise (6346:0a0d). */
void cok_party_draw(cok_picture *dst, const cok_font *font, const cok_party *party,
                    const cok_rules *rules, const uint8_t *selected, int x, bool combat);

/* 6346:12f8: the to-hit bonus for dexterity (+0x17) with missile weapons,
 * which is also the bonus to initiative in combat: -4 below 3, dexterity
 * less 6 for 3-5, less 15 for 16-18, then 3, 4 and 5 for 19-20, 21-23 and
 * 24-25; else 0. */
int8_t cok_character_dexterity_missile(const uint8_t *c, const cok_rules *rules);

/* 6346:1412 (to hit) or, with damage, 6346:14b5: strength's bonus, 0
 * unless +0x114 is set. Returns false with error set for a strength the
 * original's table (6346:137a) has no row for. */
bool cok_character_strength_bonus(const uint8_t *c, const cok_rules *rules, bool damage,
                                  int8_t *bonus, char *error, size_t error_size);

/* Apply damage to a character's hit points and status (6346:24d7):
 * damage of exactly its hit points leaves it unconscious, up to 9 more
 * dying, and 10 or more past them dead, as is any damage that takes the
 * last hit point of a character with status 1. A character not okay
 * afterwards has 0 hit points and cannot act. In combat 6346:24d7 also
 * updates the combat records, which the battle's callers do (attack.h,
 * cast.c). */
void cok_character_damage(uint8_t *record, const cok_rules *rules, uint8_t damage);

/* Heal record by amount (60f4:21ea) if its status is 0, 1, 4 or 5, and if
 * only_hurt, it is below its maximum hit points. Hit points add as a byte,
 * so they can wrap, and are capped at the maximum. A character that cannot
 * act and is dying becomes unconscious, and an unconscious one recovers
 * and can act again outside combat (mode 5). Returns whether it was
 * healed. */
bool cok_character_heal(uint8_t *record, const cok_rules *rules, uint8_t amount, bool only_hurt,
                        uint8_t mode);

/* A saved game, SAVGAM<letter>.DAT (Death Knights' .PTY), as 4b6d:1b34
 * (dkk 46b2:1ef0) reads it, in the game's layout (rules' saves). */
typedef struct {
    uint8_t file;                /* DS:5782, the ECL file. */
    /* Variables 0x4b00-0x4eff, 0x7c00-0x7fff and 0x7a00-0x7bff, as the VM
     * keeps them: an address's word, or its byte in a game whose
     * addresses hold bytes. */
    uint16_t mem4b00[0x400];
    uint16_t mem7c00[0x400];
    uint16_t mem7a00[0x200];
    int8_t map_x, map_y;         /* DS:6d85-6d89. */
    uint8_t direction, ahead, square;
    uint8_t last_mode, mode;     /* DS:4b4a, 4b49. */
    /* DS:6d8a, 6d8e, 6d92 (dkk DS:c078 + 4i): the WALLDEF record or -1,
     * and the slot it loaded into; the layout's walls of them. */
    int16_t wall_ids[COK_SAVED_WALLS];
    int16_t wall_slots[COK_SAVED_WALLS];
    uint8_t count;               /* Characters. */
    char names[COK_PARTY_MAX][41]; /* Their files, e.g. CHRDATA1; extra entries are junk. */
} cok_saved_game;

/* The bytes of a saved game in the layout of rules (Champions' 5,469,
 * Death Knights' 2,913). */
size_t cok_saved_game_size(const cok_rules *rules);
/* Where the party's size lies in a saved game of rules' layout (dkk
 * 46b2:1ef0 seeks Champions' 0x1414), and an ECL variable address's cell
 * there (Champions' 0x4c33 at 0x267). */
size_t cok_saved_game_count_offset(const cok_rules *rules);
size_t cok_saved_game_var_offset(const cok_rules *rules, uint16_t address);

/* Read a saved game in the layout of rules. Returns false with error set
 * if the file cannot be read or is shorter than the layout's size; the
 * original does not check. */
bool cok_saved_game_read(const char *path, const cok_rules *rules, cok_saved_game *game,
                         char *error, size_t error_size);
/* Write a saved game as 4b6d:22de (dkk 46b2:2808) does, the inverse of
 * cok_saved_game_read. The bytes after each name and the slots past count,
 * which the original fills with whatever its stack held, are 0. Returns
 * false with error set if the file cannot be written; the original does
 * not check. */
bool cok_saved_game_write(const char *path, const cok_rules *rules, const cok_saved_game *game,
                          char *error, size_t error_size);

/* The bank vault (dkk VAULT<letter>.DAT, 46b2:2e1d and 302d): three
 * 32-bit counts of coins, as the treasure pool's (DS:be58), then items,
 * 63 bytes each. */
typedef struct {
    uint8_t coins[12];
    uint8_t *items;
    size_t item_count;
} cok_vault;

/* Read a vault (46b2:2e1d): 12 bytes of coins (what there is of them), then
 * each whole 63-byte item, their next pointers (+0x2a) cleared; a short
 * last one is left out. A file that is not there: no coins and no items,
 * and false with *missing set. Returns false with error set if it cannot
 * be read or memory runs out. Free with cok_vault_free. */
bool cok_vault_read(const char *path, cok_vault *vault, bool *missing, char *error,
                    size_t error_size);
/* Write a vault (46b2:302d): the coins, then the items, their next
 * pointers 0 (the original writes them as they are in memory). */
bool cok_vault_write(const char *path, const cok_vault *vault, char *error, size_t error_size);
void cok_vault_free(cok_vault *vault);

/* Write character files base in dir as 4b6d:0bed does, the inverse of
 * cok_character_read: base.SAV, the record as it is (the layout's
 * file_size); for a game that keeps a spells' block, base.WIZ, erased and
 * written if the record holds one (dkk 46b2:0abc); base.STF, the items,
 * and base.SFX, the effects, each erased first and written only if there
 * are any, with the rules' extensions. The far pointers the original
 * writes as they are in memory (+0xe3, +0x143, +0x147-+0x17a, +0x17f,
 * +0x183, the items' +0x2a and the effects' +5; Death Knights' +0x3a,
 * +0x7d-+0xb4, +0xb9, +0xbd, +0xc2) are 0 here, as the port keeps them in
 * the record (but the spells' block's, which it marks). Returns false with
 * error set if a file cannot be written. */
bool cok_character_write(const cok_character *character, const cok_rules *rules, const char *dir,
                         const char *base, char *error, size_t error_size);
/* As cok_character_write, the record to base.extension: "WHO" for the
 * roster (4b6d:0bed with no base given). */
bool cok_character_write_file(const cok_character *character, const cok_rules *rules,
                              const char *dir, const char *base, const char *extension,
                              char *error, size_t error_size);

#endif
