#ifndef COK_RULES_H
#define COK_RULES_H

#include "data.h"
#include "field.h"
#include "spells.h"

#include <assert.h>
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
    /* A fourth kind, the monsters' spells, where a game keeps them apart
     * from the record (Death Knights' MON1WIZ, as its characters' .WIZ);
     * NULL in Champions, whose record holds them. */
    const char *monster_spells;
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
     * effects (169c:0154 erases them, 4b6d:11e5 reads them); and its
     * spells' block, for a game that keeps one (the layout's spells_size;
     * NULL in Champions). */
    const char *record, *roster, *items, *effects, *spells;
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
    /* The colour each tile set is loaded with as transparent, cleared to 0
     * (6e22:0050: 13 for every set, 127f:0111). */
    uint8_t tile_transparent[5];
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
    /* Whether the backdrop in CGA mode is CGA's own, a row of line colour
     * and no pictures (69ea:0184), rather than the other modes' (dkk
     * 6d21:0177 has no CGA case). */
    bool cga_backdrop;
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
    /* The icon slots (each two pictures, ready and attacking), at most
     * COK_ICON_SLOTS_MAX (adventure.h), and the DS offset of the table
     * that holds them, 8 bytes a slot, for the messages. */
    uint8_t icon_slots;
    uint16_t icon_table;
} cok_rules_startup;

/* One spell id's handler (cast.c): the port's, a cok_cast_handler
 * (spells.h), and the original's routine, its offset in the handlers'
 * segment (Champions' 5b04, as the messages cite it); or, where the port
 * does not carry the game's routine out yet, unported, citing it ("dkk
 * 5dc5:1daa"): casting it stops the run (COK_ECL_UNPORTED). */
typedef struct {
    uint16_t address;
    uint8_t handler;
    const char *unported;
} cok_rules_cast;

/* A spell whose duration is not the table's (cok_cast_duration): its
 * rule, a cok_duration_rule (spells.h), and for a fixed one its
 * minutes. */
typedef struct {
    uint8_t spell, rule;
    uint16_t minutes;
} cok_rules_duration;

/* The spells (magic.c, cast.c): what is keyed by spell id. */
typedef struct {
    /* Spell ids 1 to count - 1 have names and handlers (Champions' 0x6c,
     * the name table DS:2077 + 41 * id and the handlers DS:6e3a + 4 *
     * id). */
    unsigned count;
    /* Their names, names[id - 1], as the name table holds them; and its
     * offset for id 0 and the bytes an id (DS:2077, 41: string[40]),
     * which the port's message cites for an id with none. */
    const char *const *names;
    uint16_t names_at;
    uint8_t name_size;
    /* The spell lists' headings by spell level, level_count of them, and
     * their table's offset and the bytes a heading (DS:0bfa, 41: string[40]
     * each; 546c:34ec). */
    const char *const *levels;
    unsigned level_count;
    uint16_t levels_at;
    uint8_t level_size;
    /* By spell id, count of them: its handler. */
    const cok_rules_cast *cast;
    /* The spells whose duration is not the table's (5b04:0f78),
     * duration_count of them. */
    const cok_rules_duration *durations;
    unsigned duration_count;
    /* The spell ids the code names (spells.h's cok_named_spell): 0 for one
     * the game has not. */
    const uint8_t *ids;
    /* The rows of a spell list (5b04:0b21): the spells it holds (DS:473a)
     * and the scroll spells it counts (DS:4774), past which the original
     * writes on. */
    uint8_t list_spells, list_scrolls;
    /* The granted powers' ids, first to last (4888:0700). */
    uint8_t granted_first, granted_last;
    /* An item's power above item_last is the spell item_shift below it
     * (3afb's item use). */
    uint8_t item_last, item_shift;
    /* Display names an effect a spell adds by the first spell 1 to
     * display_last that adds it (4888:17ae). */
    uint8_t display_last;
    /* The values of the spell table's byte 7 that target the caster, one
     * the player picks and the whole party outside combat (5b04:127e). */
    uint8_t target_caster, target_one, target_party;
    /* An item's byte +0x3d that makes the computer score a weapon 0
     * (3afb:14ca's test at 3afb:15e0): a value of the item's byte, not a
     * spell or effect id the game names. */
    uint8_t weapon_unscored;
    /* The cast's own routine where the game's differs and the port does
     * not carry it out yet, citing it, or NULL: casting any spell then
     * stops the run (COK_ECL_UNPORTED) before its handler. */
    const char *cast_unported;
} cok_rules_spells;

/* One effect id's handler (effect.c): the port's, a cok_effect_handler
 * (spells.h), and the original's routine, its offset (Champions' 3f44 or
 * 5b04); or, where the port does not carry the game's routine out yet,
 * unported, citing it: running it fails. */
typedef struct {
    uint16_t address;
    uint8_t handler;
    const char *unported;
} cok_rules_effect;

/* A text Display shows for an effect id (4888:17ae). */
typedef struct {
    uint8_t id;
    const char *text;
} cok_rules_effect_text;

/* The spell effects (effect.c, magic.c, cast.c): what is keyed by effect
 * id. */
typedef struct {
    /* Effect ids 0 to count - 1 have an entry in the handler table
     * (Champions' DS:6b94-6d77, 0x79 of them). */
    unsigned count;
    /* By effect id, count of them: its handler. */
    const cok_rules_effect *handlers;
    /* By event 1 to event_count - 1, the effect ids it runs, in order,
     * 0-ended, or NULL for none (60f4:057c). */
    const uint8_t *const *events;
    unsigned event_count;
    /* The ids the party shares, 0-ended (60f4:0332): a target without one
     * takes the first holder's; and the one whose holder may be as far as
     * shared_far squares in combat (Prayer's; the others 1). */
    const uint8_t *shared;
    uint8_t shared_far_id, shared_far;
    /* The effects magic resistance leaves pending when it resists
     * (60f4:04f3), 0-ended. */
    const uint8_t *resisted_pending;
    /* The effect ids the code names (spells.h's cok_named_effect): 0 for
     * one the game has not. */
    const uint8_t *ids;
    /* Display (4888:17ae): the ids it names by the spell that adds them,
     * 0-ended, and the texts of others; it shows no other. */
    const uint8_t *display_spells;
    const cok_rules_effect_text *display_texts;
    unsigned display_text_count;
    /* The rules' routines that differ in the game and the port does not
     * carry out for it yet, citing the game's routine, or NULL: the caster
     * level (6346:29fe), the saving throw (60f4:113a) and magic resistance
     * (60f4:04f3). Reaching one fails. */
    const char *caster_level_unported, *save_unported, *resistance_unported;
} cok_rules_effects;

/* The three moons (57e4:001e, 0143): the days each stays in a phase, and
 * on the frame each one's column and its tile, tile_base + (phase +
 * tile) & 0xff. */
typedef struct {
    uint8_t days[3], column[3], tile[3];
    uint16_t tile_base;
} cok_rules_moons;

/* The routines that recompute a character (party.c), where the games'
 * differ: each the port's routine of that game's original. */
typedef enum {
    /* 6346:0d20: the movement from the armour and the load, then the
     * highest fighter, ranger or knight level (+0xce). */
    COK_STATS_CHAMPIONS,
    /* dkk 65cd:0e46: the movement as 65cd:1241 gives it, with the effects
     * that double or halve it; no fighter level. */
    COK_STATS_DEATH_KNIGHTS,
} cok_stats_routine;

typedef enum {
    /* 66c2:0433: spells a day to level 5 (66c2:000f), the former classes
     * of a human who changed class. */
    COK_LEVELS_CHAMPIONS,
    /* dkk 69a8:050b: spells a day in the spells' block to level 8 and
     * paladins' (69a8:0000), a fourth attack, no former classes. */
    COK_LEVELS_DEATH_KNIGHTS,
} cok_levels_routine;

/* One run of bytes an import moves from the other game's record to the
 * game's (or to its spells' block). */
typedef struct {
    uint16_t to, from, count;
} cok_rules_move;

/* How a game imports another's characters (party.c's
 * cok_character_import), as Death Knights does Champions' (46b2:10d7 with
 * DS:c468 0): the other game's record, read into a zeroed record of its
 * size (from's layout) from the other game's saves (from's extensions),
 * moved run by run into a zeroed record; the spells' block moved too, for
 * a class not among no_spells; the age capped by race; the items read but
 * those of the dropped types, and the effects, as they are; then
 * recomputed as a load does. */
typedef struct {
    const struct cok_rules *from;
    const cok_rules_move *moves, *spell_moves;
    size_t move_count, spell_move_count;
    /* Classes (CLASS) that get no spells' block, ended by 0xff. */
    const uint8_t *no_spells;
    /* By race, the age above which it is set, and what to: race_count
     * races, those past them kept. */
    const uint16_t (*ages)[2];
    size_t race_count;
    /* The item types (+0x2e) not imported, as a Pascal set: bit t & 7 of
     * byte t >> 3. */
    uint8_t dropped_items[32];
} cok_rules_import;

/* The game's characters' routines that differ. */
typedef struct {
    cok_stats_routine stats;
    cok_levels_routine levels;
    /* The import of another game's characters, or NULL. */
    const cok_rules_import *import;
} cok_rules_characters;

/* The screens the port draws a frame for (screen.c), each one routine of
 * the original's (Champions' 1128, dkk 110f). */
typedef enum {
    COK_SCREEN_FRAME,     /* The frame alone (1128:0000 with 0; dkk 110f:0000 with 0). */
    COK_SCREEN_OPEN,      /* The open frame (1128:0000 with 1; dkk has none: 110f:0000 with 0). */
    COK_SCREEN_ADVENTURE, /* The adventure screen (1128:0242; dkk 110f:0162). */
    COK_SCREEN_BIG,       /* A big picture's (1128:0344; dkk 110f:02ab). */
    COK_SCREEN_SPELLS,    /* Memorize's lists (1128:0384; dkk 110f:02f8). */
    COK_SCREEN_LIST,      /* The other spell lists (1128:077c; dkk 110f:0537). */
    COK_SCREEN_COMBAT,    /* The combat screen (1128:04c1; dkk 110f:0345). */
    COK_SCREEN_SHEET,     /* The character sheet (1128:05fc; dkk 110f:0392). */
    COK_SCREEN_CREDITS,   /* The credits (1128:0130; dkk 35c6:026e's 110f:0000). */
    COK_SCREENS
} cok_screen;

/* A line of frame tiles across a row (cells first to last of row at) or
 * down a column (rows first to last of column at): each cell the frame's
 * base + tiles[cell - first], or + tile where tiles is NULL; on a row with
 * moons, the moons' columns their phases' tiles (the moons section's). */
typedef struct {
    bool column;
    uint8_t at, first, last;
    uint8_t tile;
    const uint8_t *tiles;
    bool moons;
} cok_rules_frame_line;

/* One screen's frame: the screen under it drawn first (as the routine
 * calls it), or COK_SCREENS for none; then the cells left to right, top to
 * bottom cleared to colour 0 (1128:07e6; dkk 110f:0584), unless right is
 * 0; then its lines, in order, each tile drawn opaque over what is
 * there. */
typedef struct {
    cok_screen under;
    uint8_t left, top, right, bottom;
    const cok_rules_frame_line *lines;
    unsigned line_count;
} cok_rules_screen;

/* What begin() draws for a game the start menu loaded, where the first
 * block keeps its variables (cok_rules_frame's resumed). */
typedef enum {
    /* In block 0 or off the map, the adventure screen and the party list
     * (4def:01b4's second branch). */
    COK_RESUMED_BY_AREA,
    /* By the mode: 0 and 4 the adventure screen and the party list, 3 the
     * big picture's frame alone, others nothing (dkk 49cb:010b). */
    COK_RESUMED_BY_MODE,
} cok_resumed;

/* The screens' frames (screen.c), drawn with the frame's tile set (set 4,
 * the view's tile_first[4] on): the value the lines' tiles are added to,
 * and each screen's by cok_screen. And which screen the start menu's
 * return draws. */
typedef struct {
    uint16_t base;
    cok_rules_screen screens[COK_SCREENS];
    /* Whether the frame main draws after the start menu is the big
     * picture's in mode 3 on the map (1000:024b), rather than the
     * adventure screen's always (dkk 1000:01d1). */
    bool menu_big;
    /* begin()'s for a loaded game, a cok_resumed. */
    uint8_t resumed;
} cok_rules_frame;

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
    cok_rules_characters characters;
    /* The tables of the original's data segment the port holds, and where
     * each the code reads lies and its shape (data.h). */
    const cok_data_segment *data;
    cok_rules_ecl ecl;
    cok_rules_view view;
    cok_rules_startup startup;
    cok_rules_spells spells;
    cok_rules_effects effects;
    cok_rules_moons moons;
    cok_rules_frame frame;
    /* Why cok cannot play the game yet, or NULL. */
    const char *unplayable;
} cok_rules;

extern const cok_rules cok_rules_champions, cok_rules_death_knights;

/* A named spell's or effect's id in the game (spells.h), which it must
 * have. */
static inline uint8_t cok_spell_id(const cok_rules *rules, cok_named_spell name)
{
    assert(rules != NULL && rules->spells.ids != NULL && (unsigned)name < COK_SPELL_NAMES);
    uint8_t id = rules->spells.ids[name];
    assert(id != 0 && "the game has no such spell");
    return id;
}

static inline uint8_t cok_effect_id(const cok_rules *rules, cok_named_effect name)
{
    assert(rules != NULL && rules->effects.ids != NULL && (unsigned)name < COK_EFFECT_NAMES);
    uint8_t id = rules->effects.ids[name];
    assert(id != 0 && "the game has no such effect");
    return id;
}

/* Whether id is the named spell or effect: never where the game has no
 * such spell or effect. */
static inline bool cok_spell_is(const cok_rules *rules, uint8_t id, cok_named_spell name)
{
    assert(rules != NULL && rules->spells.ids != NULL && (unsigned)name < COK_SPELL_NAMES);
    return rules->spells.ids[name] != 0 && id == rules->spells.ids[name];
}

static inline bool cok_effect_is(const cok_rules *rules, uint8_t id, cok_named_effect name)
{
    assert(rules != NULL && rules->effects.ids != NULL && (unsigned)name < COK_EFFECT_NAMES);
    return rules->effects.ids[name] != 0 && id == rules->effects.ids[name];
}

/* The descriptor named id (--game), or NULL. */
const cok_rules *cok_rules_find(const char *id);
/* The names cok_rules_find knows, comma-separated, for messages. */
const char *cok_rules_names(void);

#endif
