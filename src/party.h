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
 *   +0x062  maximum hit points
 *   +0x0d0  saving throws, by type
 *   +0x0e7  0x80 and up for NPCs
 *   +0x0f9  levels in each of eight classes (signed bytes)
 *   +0x116  experience, 32 bits
 *   +0x137  combat icon slot
 *   +0x17c  saving throw bonus (signed)
 *   +0x188  status: 0 okay, 4 unconscious, 5 dying, 6 dead
 *   +0x189  set while the character can act; cleared when it drops
 *   +0x18a  1 for a character fighting against the party
 *   +0x18d  armour class as 60 - AC
 *   +0x197  hit points
 *
 * Fields from +0x18c on are derived from the others and the items when a
 * character loads (6346:0d20, 66c2:0433); the port keeps the values in the
 * file. */

enum {
    COK_PARTY_MAX = 8,          /* The game adds characters while 0x7f3e < 8. */
    COK_ITEM_SIZE = 63,
    COK_EFFECT_SIZE = 9,
    COK_SAVED_GAME_SIZE = 5469,
};

typedef struct {
    uint8_t record[COK_CHARACTER_SIZE];
    /* Items (.STF) and spell effects (.SFX) as stored, in file order; the
     * original links them from +0x143 and +0xe3. */
    uint8_t (*items)[COK_ITEM_SIZE];
    size_t item_count;
    uint8_t (*effects)[COK_EFFECT_SIZE];
    size_t effect_count;
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
 * Returns false with error set. Free with cok_character_free. */
bool cok_character_read(cok_character *character, const char *dir, const char *base,
                        char *error, size_t error_size);
void cok_character_free(cok_character *character);

/* Add a character to the end of the party (4b6d:1989), giving it the lowest
 * combat icon slot (+0x137) no member uses. The party takes ownership of a
 * malloc'd character. Returns false, leaving it with the caller, when the
 * party is full. */
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
