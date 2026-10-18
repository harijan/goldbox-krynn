#ifndef COK_RULES_H
#define COK_RULES_H

#include "data.h"
#include "field.h"

#include <stdbool.h>
#include <stdint.h>

/* What differs between the games the port plays: one descriptor a game,
 * chosen once at startup (--game) and reached through cok_game_rules (and,
 * in the tools without a game, passed in). Shared code reads what it needs
 * here and never asks which game it is. Champions of Krynn's is
 * cok_rules_champions, Death Knights of Krynn's cok_rules_death_knights
 * (read headless so far); see DEVELOPERS.md's "Games".
 *
 * The descriptor holds the game's files and file sets, the saved games'
 * and characters' names, the titles, the character record's layout
 * (field.h), the data segment's tables (data.h), the ECL's opcodes and
 * variables, the 3D view's tile and wall sets and what startup loads.
 * Each concern is a section of its own, so that later sections (the
 * spells') are added beside these without moving them. A section a game
 * does not have yet is empty (a layout or data segment of width 0
 * everywhere, a NULL stem), and code that reaches it stops at an
 * assertion.
 *
 * A DAX archive is named by its stem: "GEO" and the ECL file's number
 * make GEO1.DAX; a stem the game reads with no number ("SKY") is the
 * whole name. The original spells some of them in lower case (Assign's
 * 'debug.txt', 'midi.dax'); DOS ignores the case, and the port, as the
 * files are shipped, uses upper case. A NULL stem: the game has no such
 * archive, and code that reads it must not be reached for that game.
 *
 * Most record ids are still in the code that reads them (SKY's 250-252,
 * SKYGRND's 1 and 2, RANDCOM's 1, the music drivers' 4 and 5); startup's
 * tile sets and icons are the startup section's; a record a game reads
 * from another file than the other game does is a cok_rules_record
 * here. */

/* One record of one archive: <stem><file>.DAX, or <stem>.DAX for file 0,
 * record id record. */
typedef struct {
    const char *stem;
    unsigned file;
    uint8_t record;
} cok_rules_record;

/* The archives the game reads, as stems (see above). */
typedef struct {
    /* The ECL files, ECL<file> (load_block's 3775:0361). */
    const char *ecl;
    /* The 3D maps, GEO<file> (69ea:130d). */
    const char *geo;
    /* The wall sets, WALLDEF<file> (69ea:1025). */
    const char *walldef;
    /* The tile sets, 8X8D<file> (6e22:0050), of which startup reads
     * records 202 and 203, the frame's and set 0's, from 8X8D1
     * (3e99:005b); and the font, 8X8D1 record 201, read there too. */
    const char *tiles;
    cok_rules_record font;
    /* The small pictures, PIC<file>, and the sprites, SPRIT<file>
     * (6961:00e4). */
    const char *pic, *sprite;
    /* The big pictures, BIGPIC<file> (127f:0111 from PICTURE, 2fd3:0914). */
    const char *big;
    /* The portraits' heads and bodies, HEAD<digit> and BODY<digit>
     * (6961:05b9, the digit file_digit's). */
    const char *head, *body;
    /* The combat icons of monsters and NPCs, CPIC<digit> (6d21:01d0). */
    const char *icons;
    /* The missiles', flashes' and cursor's icons, COMSPR, loaded at
     * startup (3e99:06e0), and the party's combat icons, a head over a
     * body, CHEAD and CBODY and a letter for the size (4b6d:0817); 6d21:01d0
     * loads these three by name, without the file's digit. */
    const char *missiles, *icon_head, *icon_body;
    /* The 3D view's skies (3e99:005b), and its ground and night sky
     * (Death Knights' SKYGRND, dkk 4f9a:00e1). A game has one or the other:
     * the view draws what was loaded (view.c). */
    const char *sky, *ground;
    /* The backdrop each wall set brings, BACK<file> (dkk 6d21:0e4e). */
    const char *backdrop;
    /* The overland map's cursor (3e99:01fd): CURSOR record 1. */
    cok_rules_record cursor;
    /* The title's pictures (2f55's). */
    const char *title;
    /* Treasure's items, ITEM<digit> (2fd3:1d21). */
    const char *treasure;
    /* The monsters, MON<digit> and a kind: their records, effects and
     * items (4b6d:161b). */
    const char *monster, *monster_record, *monster_effects, *monster_items;
    /* The battlefield's tiles: in a 3D area, outdoors, and the random
     * ones of both (3cb2:1029). */
    const char *dungeon, *wilderness, *random;
    /* The title's music: the drivers (1661's, record device + 1) and the
     * score (record 1). */
    const char *music_drivers, *music_score;
    /* The digit of the files chosen by the ECL file (DS:5782) whose names
     * hold one digit, e.g. MON1CHA: the number as Str into a string[1]
     * makes it (4b6d:161b, 2fd3:1d21, 6961:05b9, 6d21:01d0). */
    char (*file_digit)(unsigned file);
    /* The file a picture, a sprite or a wall set is read from, by its
     * record id, where a game chooses it so (dkk 6ca1:00a4 for PIC and
     * SPRIT, 6d21:0ec5 for WALLDEF, 8X8D and BACK): 256 file numbers, 0
     * for the ECL file's. NULL: always the ECL file's, as in Champions. */
    const uint8_t *pic_files, *sprite_files, *wall_files;
} cok_rules_archives;

/* The files besides the archives. */
typedef struct {
    /* The devices' file (11c7:031a), in the assets or the saves; the port
     * looks for its name in upper case, then in lower case. */
    const char *config;
    /* The item types (3e99:005b), a file of its own, not an archive. */
    const char *item_types;
    /* The directory the original runs in (FExpand('*.exe') split,
     * DS:4a80: the current directory), which the setup offers the save
     * path under: the port has no DOS directory and takes the one the
     * assets' own config says the game was installed in (its save path
     * C:\COK\SAVE\). And the save path's default under it, DS:4a25
     * (11c7:0388). */
    const char *install_dir, *save_dir;
} cok_rules_files;

/* The names of what the game writes in the save path. */
typedef struct {
    /* The saved games, <game><letter>.<game_extension> (4b6d:1b34 reads,
     * 4b6d:22de writes), and its characters' files, <character><letter><n>
     * (4b6d:22de). */
    const char *game, *game_extension, *character;
    /* A character's files, by extension: its record in a saved game
     * (4b6d:11e5) and in the roster (4def:37a2), its items and its
     * effects (169c:0154 erases them, 4b6d:11e5 reads them). */
    const char *record, *roster, *items, *effects;
    /* The debug mode's trace (Woof's Ctrl-D, 1a46:04f7's Assign). */
    const char *debug;
} cok_rules_saves;

/* The archives daximages and daxcompose export, by layout
 * (cok_archive_layout): each list stems, ended by NULL. */
typedef struct {
    /* PIC: groups after the first are XOR deltas of it. */
    const char *const *delta;
    /* SPRIT: groups of frames, a sprite's. */
    const char *const *collection;
    /* One image or one group of frames a record. */
    const char *const *single;
    /* The frames a header's count byte of 0xff gives: 255 where the
     * loader takes the byte (127f:1c2f), 256 where it reads 0xff as 256
     * (dkk 123c:1c58, 123c:029a: the 256-tile 8X8D sets). */
    uint16_t frames_ff;
} cok_rules_images;

/* One ECL opcode (ecl.c): what it does, the operands its handler loads
 * and those a failed IF skips with it. */
typedef struct {
    /* What it does, a cok_ecl_opcode (ecl.h): Champions' opcode for each
     * of Champions', and for a later game's the one it does the same as,
     * or a meaning of its own past Champions'. */
    uint8_t meaning;
    /* The operands its handler loads, or COK_ECL_LIST (-1): a header of
     * header operands, the last of which counts the list loaded over it;
     * or COK_ECL_HANG (-2): the original never advances. */
    int8_t operands;
    uint8_t header;
    /* The operands a failed IF skips with it (3775:1e74), which may differ
     * from the handler's. */
    uint8_t skip;
    /* The game's handler, where the port does not carry it out yet (it
     * needs what is not ported: characters, monsters, combat, spells),
     * as the message cites it ("dkk 3632:16ec"); NULL where it does. The
     * run stops there with COK_ECL_UNPORTED (adventure.c). Only an
     * opcode the VM hands to its hook can be one. */
    const char *unported;
} cok_rules_opcode;

/* A routine of the original that CALL runs, by its address (2fd3:329b). */
typedef struct {
    uint16_t address;
    /* What it does, a cok_ecl_call (ecl.h). */
    uint8_t routine;
} cok_rules_call;

/* A variable the code reads and writes by name (a cok_var, ecl.h), or a
 * run of them it indexes. */
typedef struct {
    /* Its ECL address, of the first element; 0: the game has no such
     * variable, and code that reaches it stops at an assertion. */
    uint16_t address;
    /* Its bytes: as many as a variable address holds (variable_bytes),
     * or 2 where each holds a byte, a little-endian word over two
     * addresses. */
    uint8_t width;
    /* Its elements, 1 for a single value; element i lies i * width /
     * variable_bytes addresses on. */
    uint16_t count;
    /* The value a flag reads as not set at (cok_ecl_flag): 0 but where a
     * game keeps the flag as one value of another variable. */
    uint16_t off;
} cok_rules_var;

/* An address the VM reads or stores as the game's state, not as a
 * variable (a cok_ecl_special, ecl.h). */
typedef struct {
    uint16_t address;
    uint8_t what;
} cok_rules_special;

/* An ECL address in 0x7c00-0x7fff that reads or writes the selected
 * character (ecl.c): offsets first to last from 0x7c00, element index +
 * (offset - first) / step of field, read and written as kinds read and
 * write (cok_ecl_character_kind, ecl.h; 0 none). */
typedef struct {
    uint16_t first, last;
    uint8_t read, write;
    uint8_t field; /* A cok_field (field.h). */
    uint8_t index, step;
} cok_rules_character_var;

/* The ECL's instructions and variables (ecl.c). */
typedef struct {
    /* By opcode byte, count of them; past them an opcode hangs. */
    const cok_rules_opcode *opcodes;
    unsigned count;
    /* The addresses CALL knows, and what each does; it does nothing for
     * others. */
    cok_rules_call calls[5];
    /* The bytes a variable address holds: 2 where each holds a word
     * (Champions, 3775:0e06), 1 where each holds a byte and an operand of
     * type 2 and up reads and writes a little-endian word over two
     * (Death Knights, dkk 3c1e:0d85 and 3c1e:0bc1). */
    uint8_t variable_bytes;
    /* The variables by name, COK_VARS of them (ecl.h's cok_var): where
     * the game keeps each that code outside the VM's operands reads or
     * writes. */
    const cok_rules_var *vars;
    /* What a block's start sets (3775:01e8), besides the VM's own state:
     * each variable named to its value, count of them. */
    const struct cok_rules_var_value {
        uint8_t var;
        uint16_t value;
    } *start;
    unsigned start_count;
    /* The addresses that read the game's state (3775:0fca's), and those a
     * store to sets it (3775:0e06's), count of each; another address
     * outside the variables' blocks reads 0 and a store to it does
     * nothing. */
    const cok_rules_special *reads, *stores;
    unsigned read_count, store_count;
    /* The selected character's fields at 0x7c00-0x7fff (3775:07d9 reads,
     * 3775:0c00 writes), count of them; another address there is a
     * variable. */
    const cok_rules_character_var *character;
    unsigned character_count;
    /* The classes whose levels above 0 make a character-field read of
     * kind COK_ECL_FIELD_CLASSES, bit by bit from bit 0; 0xff none. */
    uint8_t class_bits[8];
    /* Whether COMPARE AND compares a pair as strings where its first is
     * one (dkk 3632:0bed), rather than their values always (2fd3:0bbf). */
    bool compare_and_strings;
    /* DELAY's wait in milliseconds, fixed (dkk 3632:1c90: 600), or 0 for
     * the game's speed's, speed * 100 (2fd3:2c33). */
    uint16_t delay_ms;
    /* Whether HORIZONTAL MENU shows a single item in white, Enter taking
     * it, with "PRESS BUTTON OR RETURN TO CONTINUE." respelled, and
     * animates the picture only where it replaced the view (DS:884b), as
     * Champions' 2fd3:116d does; Death Knights' 3632:11fd has no such case
     * and animates any picture shown. */
    bool menu_single_item;
} cok_rules_ecl;

/* The 3D view's tile and wall sets (view.c, adventure.c). */
typedef struct {
    /* Each tile set's first and last tile value (6e22:01ab: DS:1ec2,
     * the bounds in its code); a value is drawn from the first set whose
     * last is not below it. */
    uint16_t tile_first[5], tile_last[5];
    /* The wall slot whose sets are wall types 1-5, which LOAD PIECES'
     * two-set path loads first (2fd3:0cf4 loads slot 1 and 3, 69ea:1025
     * copies slot n to set n - 1). */
    uint8_t first_slot;
    /* Whether a record's wall sets share one tile set, record id into the
     * slot's (dkk 6d21:0ec5), rather than one each, id, or id * 10 + 1 and
     * up for more than one set (69ea:1025). */
    bool shared_tiles;
    /* Whether tile values from 0x2d are moved up by the slot's tile set's
     * offset from set 1's (69ea:1230). */
    bool move_tiles;
    /* The overhead map's tiles (69ea:000f): a square with no walls, and
     * the party facing north. */
    uint16_t overhead_square, overhead_party;
    /* PICTURE's id for the overland map (2fd3:0914). */
    uint8_t overland_picture;
} cok_rules_view;

/* What the game loads once at startup (3e99:005b), past the font. */
typedef struct {
    /* Tile sets from <tiles>1: set, record; count of them. */
    struct {
        uint8_t set, record;
    } tiles[2];
    unsigned tile_count;
    /* The missiles' icons (3e99:06e0): COMSPR ids from first into icon
     * slots from slot, count of them, each range of ranges. */
    struct {
        uint8_t first, slot, count;
    } icons[2];
    unsigned icon_ranges;
    /* The overland map's cursor's transparent colour, -1 for none (it is
     * loaded without a mask). */
    int8_t cursor_transparent;
} cok_rules_startup;

/* The game's own words that are names: its title. */
typedef struct {
    /* The window's title. */
    const char *name;
    /* The title's menu's heading, at startup and after a demonstration
     * (1000:0134), which spell it differently. */
    const char *version, *version_again;
} cok_rules_text;

typedef struct cok_rules {
    /* --game's name. */
    const char *id;
    /* The assets' directory when --assets does not name one. */
    const char *assets;
    cok_rules_text text;
    cok_rules_archives archives;
    cok_rules_files files;
    cok_rules_saves saves;
    cok_rules_images images;
    /* Where each field of a character lies in its record (field.h). */
    const cok_character_layout *character;
    /* The tables of the original's data segment the port holds, and where
     * each the code reads lies and its shape (data.h). */
    const cok_data_segment *data;
    cok_rules_ecl ecl;
    cok_rules_view view;
    cok_rules_startup startup;
    /* Why cok cannot play the game yet, or NULL. */
    const char *unplayable;
} cok_rules;

extern const cok_rules cok_rules_champions, cok_rules_death_knights;

/* The descriptor named id (--game), or NULL. */
const cok_rules *cok_rules_find(const char *id);
/* The names cok_rules_find knows, comma-separated, for messages. */
const char *cok_rules_names(void);

#endif
